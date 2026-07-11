/*
 *  CheatDriver.c — Minimal kernel driver for R6 external cheat
 *
 *  Communication: shared memory via Windows Section + Events
 *  No device object, no symbolic link, no IOCTLs.
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

static PSharedBuffer       gSharedBuffer     = NULL;  /* kernel section view */
static HANDLE              gSectionHandle    = NULL;
static HANDLE              gRequestEvent     = NULL;
static HANDLE              gResponseEvent    = NULL;
static HANDLE              gDispatcherThread = NULL;

static volatile BOOLEAN    gStopping         = FALSE;

/* Cached function pointers (resolved once in DriverEntry) */
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
static VOID CleanupDriver(VOID);

/* KDMapper passes: driver=NULL, registryPath=allocation_ptr
 * (when --PassAllocationPtr is used).
 * This Unload export is called by KDMapper on exit.             */
PVOID gAllocationPtr = NULL;
VOID Unload(VOID) { CleanupDriver(); }

/* ------------------------------------------------------------------ */
/*  DriverEntry                                                        */
/* ------------------------------------------------------------------ */

NTSTATUS DriverEntry(PDRIVER_OBJECT driver, PUNICODE_STRING registryPath)
{
    NTSTATUS status;
    UNICODE_STRING routineName;
    UNICODE_STRING objectName;

    /* KDMapper: driver=NULL, registryPath=kernel allocation ptr
     * (when --PassAllocationPtr is used).                       */
    gAllocationPtr = (PVOID)(ULONG_PTR)registryPath;

    (void)driver;

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
     *     can open it via OpenFileMapping / NtOpenSection.           --- */
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
             L"\\BaseNamedObjects\\Cheat_Req_%08x",
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
             L"\\BaseNamedObjects\\Cheat_Rsp_%08x",
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

    /* Mark header as ready (user-mode spins on this) */
    gSharedBuffer->header.initialized = 1;

    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ */
/*  Dispatcher thread — loops waiting for requests                     */
/* ------------------------------------------------------------------ */

static VOID DispatcherThread(PVOID startContext)
{
    (void)startContext;

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
            /* Timeout or error — loop again */
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
    /* Signal the dispatcher to exit its loop */
    gStopping = TRUE;
    KeSetEvent(gRequestEvent, IO_NO_INCREMENT, FALSE);

    /* Close kernel handles — named objects persist until the last
     * reference drops, so user-mode handles remain valid.           */
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
}
