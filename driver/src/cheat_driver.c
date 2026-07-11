/*
 *  CheatDriver.c — Stealth kernel driver for R6 external cheat
 *
 *  Communication: shared memory via Windows Section + Events
 *  No device object, no symbolic link, no IOCTLs.
 *
 *  STEALTH MEASURES:
 *    - Driver unlinked from IoDriverList
 *    - Driver object strings zeroed (DriverName, ServiceKeyName, etc.)
 *    - MajorFunction dispatch table zeroed (no callbacks to scan)
 *    - Thread unlinked from active list after creation
 *    - Named objects use innocuous names (no "Cheat" anywhere)
 *    - Event names randomized per-load
 *    - Section backing is non-paged pool (not pageable)
 */

#include <ntddk.h>
#include "../include/cheat_shm.h"

/* ------------------------------------------------------------------ */
/*  NT kernel function types                                           */
/* ------------------------------------------------------------------ */

typedef NTSTATUS (NTAPI *PNtCreateSection)(
    PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, PLARGE_INTEGER,
    ULONG, ULONG, HANDLE);

typedef NTSTATUS (NTAPI *PNtCreateEvent)(
    PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, EVENT_TYPE, BOOLEAN);

typedef NTSTATUS (NTAPI *PNtClose)(HANDLE);

typedef NTSTATUS (NTAPI *PNtMapViewOfSection)(
    HANDLE, HANDLE, PVOID*, ULONG_PTR, SIZE_T, PLARGE_INTEGER,
    PSIZE_T, ULONG, ULONG, ULONG);

typedef NTSTATUS (NTAPI *PZwUnmapViewOfSection)(HANDLE, PVOID);

typedef NTSTATUS (NTAPI *PMmCopyVirtualMemory)(
    PEPROCESS, PVOID, PEPROCESS, PVOID, SIZE_T, KPROCESSOR_MODE, PSIZE_T);

typedef NTSTATUS (NTAPI *PPsLookupProcessByProcessId)(HANDLE, PEPROCESS*);

/* ------------------------------------------------------------------ */
/*  Globals                                                            */
/* ------------------------------------------------------------------ */

static PSharedBuffer       gSharedBuffer     = NULL;
static HANDLE              gSectionHandle    = NULL;
static HANDLE              gRequestEvent     = NULL;
static HANDLE              gResponseEvent    = NULL;
static HANDLE              gDispatcherThread = NULL;

static volatile BOOLEAN    gStopping         = FALSE;

static PDRIVER_OBJECT      gDriverObject     = NULL;

static PNtCreateSection                  g_fnNtCreateSection                 = NULL;
static PNtCreateEvent                    g_fnNtCreateEvent                   = NULL;
static PNtClose                          g_fnNtClose                         = NULL;
static PNtMapViewOfSection               g_fnNtMapViewOfSection              = NULL;
static PZwUnmapViewOfSection             g_fnZwUnmapViewOfSection            = NULL;
static PMmCopyVirtualMemory              g_fnMmCopyVirtualMemory             = NULL;
static PPsLookupProcessByProcessId       g_fnPsLookupProcessByProcessId      = NULL;

static VOID DispatcherThread(PVOID startContext);
static VOID ProcessRequest(PSharedBuffer buf);
static BOOLEAN ValidateRequest(PSharedRequest req);
static VOID HideDriver(PDRIVER_OBJECT driver);
static VOID HideThread(PETHREAD thread);
static VOID ShowDriver(PDRIVER_OBJECT driver);
static VOID CleanupDriver(VOID);

/* KDMapper passes: driver=NULL, registryPath=allocation_ptr
 * (when --PassAllocationPtr is used).
 * This Unload export is called by KDMapper on exit.             */
PVOID gAllocationPtr = NULL;
VOID Unload(VOID) { CleanupDriver(); }

/* ------------------------------------------------------------------ */
/*  Hide driver from IoDriverList and clear identifying strings        */
/* ------------------------------------------------------------------ */

static VOID HideDriver(PDRIVER_OBJECT driver)
{
    if (!driver) return;

    /* --- Zero all identifying unicode strings --- */
    /* DriverName (e.g. "\Driver\MyDriver") */
    RtlZeroMemory(&driver->DriverName, sizeof(driver->DriverName));

    /* HardwareDatabase (registry key path) */
    RtlZeroMemory(&driver->HardwareDatabase, sizeof(driver->HardwareDatabase));

    /* ServiceKeyName (registry key name) */
    RtlZeroMemory(&driver->ServiceKeyName, sizeof(driver->DriverName));

    /* DriverSection (pointer to loaded module section) */
    driver->DriverSection = NULL;

    /* DriverStart / DriverSize (module base/size in kernel map) */
    driver->DriverStart  = 0;
    driver->DriverSize   = 0;

    /* --- Zero MajorFunction dispatch table (10 entries) --- */
    /* An EAC/BE scan walks these to find hooked IRPs.           */
    RtlZeroMemory(driver->MajorFunction,
                  sizeof(PDRIVER_MAJOR_FUNCTION) * IRP_MJ_MAXIMUM_FUNCTION);

    /* --- Unlink from IoDriverList --- */
    /* The kernel maintains a doubly-linked list of loaded       */
    /* drivers. Removing from this list is the single most       */
    /* important step for hiding.                                */
    driver->DriverListEntry.Blink->Flink = driver->DriverListEntry.Flink;
    driver->DriverListEntry.Flink->Blink = driver->DriverListEntry.Blink;

    /* Clear the link pointers themselves so they don't dangle   */
    driver->DriverListEntry.Blink = NULL;
    driver->DriverListEntry.Flink = NULL;
}

/* ------------------------------------------------------------------ */
/*  Restore driver list linkage (called on unload)                     */
/* ------------------------------------------------------------------ */

static VOID ShowDriver(PDRIVER_OBJECT driver)
{
    if (!driver) return;

    /* Re-insert into IoDriverList via IobDriverList */
    PLIST_ENTRY listHead = *(PLIST_ENTRY*)((PUCHAR)IoGetCurrentProcess() + 0x88);

    /* Fallback: use the known IoDriverListHead address */
    if (!listHead) {
        listHead = IoGetDriverListHead();
    }

    if (listHead) {
        driver->DriverListEntry.Flink = listHead->Flink;
        driver->DriverListEntry.Blink = listHead;
        listHead->Flink = &driver->DriverListEntry;
    }
}

/* ------------------------------------------------------------------ */
/*  Hide dispatcher thread from active thread list                     */
/* ------------------------------------------------------------------ */

static VOID HideThread(PETHREAD thread)
{
    if (!thread) return;

    /* Unlink from the system's active thread list.
     * Threads are linked in the TCB at offset 0x248 (Flink/Blink
     * of the ActiveThreads list on x64).                       */
    PLIST_ENTRY activeList = (PLIST_ENTRY)((PUCHAR)thread + 0x248);

    activeList->Blink->Flink = activeList->Flink;
    activeList->Flink->Blink = activeList->Blink;

    activeList->Blink = NULL;
    activeList->Flink = NULL;

    /* Clear the thread's ClientId so it doesn't appear in
     * thread enumeration tools.                                */
    PCLIENT_ID cid = IoThreadToClientId(thread);
    if (cid) {
        cid->UniqueProcess = NULL;
        cid->UniqueThread  = NULL;
    }

    /* Clear the thread object name */
    RtlZeroMemory(&((PETHREAD)thread)->Tcb.CrossThreadFlags,
                  sizeof(ULONG));
}

/* ------------------------------------------------------------------ */
/*  DriverEntry                                                        */
/* ------------------------------------------------------------------ */

NTSTATUS DriverEntry(PDRIVER_OBJECT driver, PUNICODE_STRING registryPath)
{
    NTSTATUS status;
    UNICODE_STRING routineName;
    UNICODE_STRING objectName;

    gDriverObject = driver;

    /* KDMapper: driver=NULL, registryPath=kernel allocation ptr
     * (when --PassAllocationPtr is used).                       */
    gAllocationPtr = (PVOID)(ULONG_PTR)registryPath;

    /* --- Resolve kernel function pointers once --- */

    RtlInitUnicodeString(&routineName, L"NtCreateSection");
    g_fnNtCreateSection = (PNtCreateSection)
        MmGetSystemRoutineAddress(&routineName);

    RtlInitUnicodeString(&routineName, L"NtCreateEvent");
    g_fnNtCreateEvent = (PNtCreateEvent)
        MmGetSystemRoutineAddress(&routineName);

    RtlInitUnicodeString(&routineName, L"NtClose");
    g_fnNtClose = (PNtClose)
        MmGetSystemRoutineAddress(&routineName);

    RtlInitUnicodeString(&routineName, L"NtMapViewOfSection");
    g_fnNtMapViewOfSection = (PNtMapViewOfSection)
        MmGetSystemRoutineAddress(&routineName);

    RtlInitUnicodeString(&routineName, L"ZwUnmapViewOfSection");
    g_fnZwUnmapViewOfSection = (PZwUnmapViewOfSection)
        MmGetSystemRoutineAddress(&routineName);

    RtlInitUnicodeString(&routineName, L"MmCopyVirtualMemory");
    g_fnMmCopyVirtualMemory = (PMmCopyVirtualMemory)
        MmGetSystemRoutineAddress(&routineName);

    RtlInitUnicodeString(&routineName, L"PsLookupProcessByProcessId");
    g_fnPsLookupProcessByProcessId = (PPsLookupProcessByProcessId)
        MmGetSystemRoutineAddress(&routineName);

    if (!g_fnNtCreateSection || !g_fnNtCreateEvent || !g_fnNtClose ||
        !g_fnNtMapViewOfSection || !g_fnZwUnmapViewOfSection ||
        !g_fnMmCopyVirtualMemory || !g_fnPsLookupProcessByProcessId)
    {
        return STATUS_ENTRYPOINT_NOT_FOUND;
    }

    /* --- Create named Section (no OBJ_KERNEL_HANDLE) so user-mode
     *     can open it via OpenFileMapping / NtOpenSection.
     *     Name is innocuous — "SessionPool" looks like a system   */
    /*     object, not a cheat.                                     --- */
    UNICODE_STRING sectionName;
    RtlInitUnicodeString(&sectionName, CHEAT_SECTION_NAME);

    OBJECT_ATTRIBUTES sectionAttr;
    InitializeObjectAttributes(&sectionAttr, &sectionName,
                               OBJ_CASE_INSENSITIVE,
                               NULL, NULL);

    LARGE_INTEGER maxSize;
    maxSize.QuadPart = SHM_BUFFER_SIZE;

    status = g_fnNtCreateSection(
        &gSectionHandle,
        SECTION_MAP_WRITE | SECTION_QUERY,
        &sectionAttr,
        &maxSize,
        PAGE_READWRITE,
        SEC_COMMIT,
        NULL);

    if (!NT_SUCCESS(status)) {
        return status;
    }

    /* --- Map the section in kernel space. This mapping persists
     *     for the driver's lifetime (never unmapped in DriverEntry). */
    PVOID sectionView = NULL;
    SIZE_T viewSize = SHM_BUFFER_SIZE;

    status = g_fnNtMapViewOfSection(
        gSectionHandle,
        (HANDLE)-1,
        &sectionView,
        0,
        0,
        NULL,
        &viewSize,
        ViewUnmap,
        0,
        PAGE_READWRITE);

    if (!NT_SUCCESS(status) || !sectionView) {
        g_fnNtClose(gSectionHandle);
        gSectionHandle = NULL;
        return status;
    }

    RtlZeroMemory(sectionView, SHM_BUFFER_SIZE);
    gSharedBuffer = (PSharedBuffer)sectionView;

    /* --- Create request event with randomized name --- */
    WCHAR reqName[CHEAT_MAX_NAME_LEN] = { 0 };
    swprintf(reqName, CHEAT_MAX_NAME_LEN,
             L"\\BaseNamedObjects\\Spool_%08x",
             (ULONG)KeQueryPerformanceCounter(NULL).LowPart);

    RtlInitUnicodeString(&objectName, reqName);

    OBJECT_ATTRIBUTES eventAttr;
    InitializeObjectAttributes(&eventAttr, &objectName,
                               OBJ_CASE_INSENSITIVE,
                               NULL, NULL);

    status = g_fnNtCreateEvent(
        &gRequestEvent,
        EVENT_ALL_ACCESS,
        &eventAttr,
        SynchronizationEvent,
        FALSE);

    if (!NT_SUCCESS(status)) {
        g_fnZwUnmapViewOfSection((HANDLE)-1, sectionView);
        gSharedBuffer = NULL;
        g_fnNtClose(gSectionHandle);
        gSectionHandle = NULL;
        return status;
    }

    /* --- Create response event with randomized name --- */
    WCHAR rspName[CHEAT_MAX_NAME_LEN] = { 0 };
    swprintf(rspName, CHEAT_MAX_NAME_LEN,
             L"\\BaseNamedObjects\\Spool_%08x",
             (ULONG)KeQueryTimeIncrement());

    RtlInitUnicodeString(&objectName, rspName);

    InitializeObjectAttributes(&eventAttr, &objectName,
                               OBJ_CASE_INSENSITIVE,
                               NULL, NULL);

    status = g_fnNtCreateEvent(
        &gResponseEvent,
        EVENT_ALL_ACCESS,
        &eventAttr,
        SynchronizationEvent,
        FALSE);

    if (!NT_SUCCESS(status)) {
        g_fnNtClose(gRequestEvent);
        gRequestEvent = NULL;
        g_fnZwUnmapViewOfSection((HANDLE)-1, sectionView);
        gSharedBuffer = NULL;
        g_fnNtClose(gSectionHandle);
        gSectionHandle = NULL;
        return status;
    }

    /* --- Publish event names in the shared buffer header
     *     so user-mode can discover them.                           --- */
    wcsncpy(gSharedBuffer->header.requestEventName,
            reqName, CHEAT_MAX_NAME_LEN - 1);
    wcsncpy(gSharedBuffer->header.responseEventName,
            rspName, CHEAT_MAX_NAME_LEN - 1);

    /* --- Start dispatcher thread then exit quickly --- */
    HANDLE threadHandle = NULL;
    status = PsCreateSystemThread(
        &threadHandle,
        THREAD_ALL_ACCESS,
        NULL,
        (HANDLE)-1,
        NULL,
        (PKSTART_ROUTINE)DispatcherThread,
        NULL);

    if (!NT_SUCCESS(status)) {
        g_fnNtClose(gResponseEvent);
        gResponseEvent = NULL;
        g_fnNtClose(gRequestEvent);
        gRequestEvent = NULL;
        g_fnZwUnmapViewOfSection((HANDLE)-1, sectionView);
        gSharedBuffer = NULL;
        g_fnNtClose(gSectionHandle);
        gSectionHandle = NULL;
        return status;
    }

    gDispatcherThread = threadHandle;

    /* --- Mark header as ready (user-mode spins on this) --- */
    gSharedBuffer->header.initialized = 1;

    /* --- STEALTH: Hide the driver AFTER everything is set up --- */
    HideDriver(driver);

    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ */
/*  Dispatcher thread — loops waiting for requests                     */
/* ------------------------------------------------------------------ */

static VOID DispatcherThread(PVOID startContext)
{
    (void)startContext;

    PETHREAD thread = NULL;
    PsGetThreadThread(&thread);

    /* Hide the thread from enumeration */
    if (thread) {
        HideThread(thread);
    }

    LARGE_INTEGER timeout;
    timeout.QuadPart = -500LL * 1000 * 10; /* -500 ms */

    for (;;) {
        if (gStopping) {
            break;
        }

        NTSTATUS status = KeWaitForSingleObject(
            gRequestEvent,
            Executive,
            KernelMode,
            FALSE,
            &timeout);

        if (status != STATUS_SUCCESS && status != STATUS_WAIT_1) {
            continue;
        }

        if (gSharedBuffer) {
            ProcessRequest(gSharedBuffer);
        }

        /* Signal usermode that the response is ready */
        KeSetEvent(gResponseEvent, IO_NO_INCREMENT, FALSE);
    }

    PsTerminateSystemThread(STATUS_SUCCESS);
}

/* ------------------------------------------------------------------ */
/*  Validate request fields                                            */
/* ------------------------------------------------------------------ */

static BOOLEAN ValidateRequest(PSharedRequest req)
{
    if (!req) return FALSE;

    if (req->type > REQ_ALLOC_MEMORY) return FALSE;
    if (req->size == 0) return FALSE;
    if (req->size > 2048) return FALSE;

    if (req->type != REQ_ALLOC_MEMORY) {
        if (req->pid == 0) return FALSE;
        if (req->address > 0x7FFFFFFFFFFFULL) return FALSE;
    }

    return TRUE;
}

/* ------------------------------------------------------------------ */
/*  Process one request from the shared buffer                         */
/* ------------------------------------------------------------------ */

static VOID ProcessRequest(PSharedBuffer buf)
{
    if (!buf) return;

    PSharedRequest  req  = &buf->request;
    PSharedResponse resp = &buf->response;

    resp->success          = FALSE;
    resp->bytesTransferred = 0;
    resp->allocatedAddress = 0;

    if (!ValidateRequest(req)) {
        return;
    }

    SIZE_T transferred = 0;
    NTSTATUS status;

    switch (req->type) {

    case REQ_READ_MEMORY: {
        PEPROCESS process = NULL;
        status = g_fnPsLookupProcessByProcessId(
            (HANDLE)(ULONG_PTR)req->pid,
            &process);

        if (!NT_SUCCESS(status)) {
            return;
        }

        status = g_fnMmCopyVirtualMemory(
            process,
            (PVOID)(ULONG_PTR)req->address,
            PsInitialSystemProcess,
            resp->data,
            req->size,
            UserMode,
            &transferred);

        ObDereferenceObject(process);
        break;
    }

    case REQ_WRITE_MEMORY: {
        PEPROCESS process = NULL;
        status = g_fnPsLookupProcessByProcessId(
            (HANDLE)(ULONG_PTR)req->pid,
            &process);

        if (!NT_SUCCESS(status)) {
            return;
        }

        status = g_fnMmCopyVirtualMemory(
            PsInitialSystemProcess,
            resp->data,
            process,
            (PVOID)(ULONG_PTR)req->address,
            req->size,
            UserMode,
            &transferred);

        ObDereferenceObject(process);
        break;
    }

    case REQ_ALLOC_MEMORY: {
        PVOID allocation = ExAllocatePool2(
            POOL_FLAG_NON_PAGED | POOL_FLAG_UNINITIALIZED,
            req->size,
            CHEAT_POOL_TAG);

        if (allocation) {
            resp->allocatedAddress = (ULONGLONG)allocation;
            resp->success          = TRUE;
            status                 = STATUS_SUCCESS;
        } else {
            status = STATUS_INSUFFICIENT_RESOURCES;
        }
        break;
    }

    default:
        status = STATUS_INVALID_PARAMETER;
        break;
    }

    if (req->type != REQ_ALLOC_MEMORY) {
        resp->success = NT_SUCCESS(status);
    }
    resp->bytesTransferred = (ULONG)transferred;
}

/* ------------------------------------------------------------------ */
/*  Cleanup / Unload                                                   */
/* ------------------------------------------------------------------ */

static VOID CleanupDriver(VOID)
{
    /* Restore driver visibility before cleanup so the kernel
     * doesn't crash when handles are closed.                     */
    ShowDriver(gDriverObject);

    /* Signal the dispatcher to exit its loop */
    gStopping = TRUE;
    KeSetEvent(gRequestEvent, IO_NO_INCREMENT, FALSE);

    /* Close kernel handles — named objects persist until the last
     * reference drops, so user-mode handles remain valid.         */
    if (gDispatcherThread) {
        g_fnNtClose(gDispatcherThread);
        gDispatcherThread = NULL;
    }

    if (gResponseEvent) {
        g_fnNtClose(gResponseEvent);
        gResponseEvent = NULL;
    }

    if (gRequestEvent) {
        g_fnNtClose(gRequestEvent);
        gRequestEvent = NULL;
    }

    if (gSharedBuffer) {
        g_fnZwUnmapViewOfSection((HANDLE)-1, (PVOID)gSharedBuffer);
        gSharedBuffer = NULL;
    }

    if (gSectionHandle) {
        g_fnNtClose(gSectionHandle);
        gSectionHandle = NULL;
    }

    gDriverObject = NULL;
}
