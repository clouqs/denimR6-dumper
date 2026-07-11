#include "kernel.h"

#include <winnt.h>
#include <iostream>
#include <winternl.h>
#include <thread>
#include <chrono>

/* NT constants not exposed by the usermode SDK */
#ifndef ViewUnmap
#define ViewUnmap  1
#endif

#ifndef EVENT_WAIT
#define EVENT_WAIT 0x0001
#endif

#pragma comment(lib, "ntdll.lib")

/* ------------------------------------------------------------------ */
/*  NT API types                                                       */
/* ------------------------------------------------------------------ */

typedef NTSTATUS (NTAPI *PNtOpenSection)(
    PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES);

typedef NTSTATUS (NTAPI *PNtOpenEvent)(
    PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES);

typedef NTSTATUS (NTAPI *PNtMapViewOfSection)(
    HANDLE, HANDLE, PVOID*, ULONG_PTR, SIZE_T, PLARGE_INTEGER,
    PSIZE_T, ULONG, ULONG, ULONG);

typedef NTSTATUS (NTAPI *PNtUnmapViewOfSection)(HANDLE, PVOID);

typedef NTSTATUS (NTAPI *PNtClose)(HANDLE);

/* ------------------------------------------------------------------ */
/*  Shared buffer layout (matches driver/include/cheat_shm.h)          */
/* ------------------------------------------------------------------ */

#define SHM_BUFFER_SIZE 4096
#define CHEAT_MAX_NAME_LEN 64

typedef enum REQUEST_TYPE {
    REQ_NONE         = 0,
    REQ_READ_MEMORY  = 1,
    REQ_WRITE_MEMORY = 2,
    REQ_ALLOC_MEMORY = 3,
} REQUEST_TYPE;

typedef struct {
    WCHAR requestEventName[CHEAT_MAX_NAME_LEN];
    WCHAR responseEventName[CHEAT_MAX_NAME_LEN];
    ULONG initialized;
    ULONG pad;
} ShmHeader;

#pragma pack(push, 1)

typedef struct {
    REQUEST_TYPE type;
    ULONG        pid;
    ULONGLONG    address;
    ULONG        size;
    ULONG        padding;
} SharedRequest;

typedef struct {
    BOOLEAN      success;
    ULONG        bytesTransferred;
    ULONGLONG    allocatedAddress;
    UCHAR        data[SHM_BUFFER_SIZE
                      - sizeof(ShmHeader)
                      - sizeof(SharedRequest)
                      - sizeof(BOOLEAN)
                      - sizeof(ULONG)
                      - sizeof(ULONGLONG)];
} SharedResponse;

#pragma pack(pop)

typedef struct {
    ShmHeader       header;
    SharedRequest   request;
    SharedResponse  response;
} SharedBuffer;

/* ------------------------------------------------------------------ */
/*  Named objects                                                      */
/* ------------------------------------------------------------------ */

static LPCWSTR kSectionName = L"\\BaseNamedObjects\\SessionPool";

/* ------------------------------------------------------------------ */
/*  Implementation                                                     */
/* ------------------------------------------------------------------ */

Kernel::~Kernel() {
    close();
}

bool Kernel::open() {
    if (isOpen()) {
        return true;
    }

    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) {
        std::cerr << "[Kernel] ntdll.dll not found.\n";
        return false;
    }

    PNtOpenSection        pNtOpenSection        = (PNtOpenSection)GetProcAddress(ntdll, "NtOpenSection");
    PNtOpenEvent          pNtOpenEvent          = (PNtOpenEvent)GetProcAddress(ntdll, "NtOpenEvent");
    PNtMapViewOfSection   pNtMapViewOfSection   = (PNtMapViewOfSection)GetProcAddress(ntdll, "NtMapViewOfSection");
    PNtUnmapViewOfSection pNtUnmapViewOfSection = (PNtUnmapViewOfSection)GetProcAddress(ntdll, "NtUnmapViewOfSection");
    PNtClose              pNtClose              = (PNtClose)GetProcAddress(ntdll, "NtClose");

    if (!pNtOpenSection || !pNtOpenEvent || !pNtMapViewOfSection ||
        !pNtUnmapViewOfSection || !pNtClose)
    {
        std::cerr << "[Kernel] Missing NT API functions in ntdll.dll.\n";
        return false;
    }

    /* --- Open named section --- */
    UNICODE_STRING sectionName;
    RtlInitUnicodeString(&sectionName, kSectionName);

    OBJECT_ATTRIBUTES sectionAttr;
    InitializeObjectAttributes(&sectionAttr, &sectionName,
                               OBJ_CASE_INSENSITIVE, NULL, NULL);

    NTSTATUS status = pNtOpenSection(&sectionHandle_, SECTION_MAP_READ | SECTION_MAP_WRITE, &sectionAttr);
    if (!NT_SUCCESS(status)) {
        std::cerr << "[Kernel] NtOpenSection failed: 0x" << std::hex << status << std::dec << "\n";
        return false;
    }

    /* --- Map view --- */
    SIZE_T viewSize = SHM_BUFFER_SIZE;
    status = pNtMapViewOfSection(
        sectionHandle_,
        GetCurrentProcess(),
        &sectionView_,
        0,
        0,
        NULL,
        &viewSize,
        ViewUnmap,
        0,
        PAGE_READWRITE);

    if (!NT_SUCCESS(status) || !sectionView_) {
        std::cerr << "[Kernel] NtMapViewOfSection failed: 0x" << std::hex << status << std::dec << "\n";
        pNtClose(sectionHandle_);
        sectionHandle_ = INVALID_HANDLE_VALUE;
        return false;
    }

    /* --- Wait for the driver to set initialized = 1 --- */
    SharedBuffer* shm = static_cast<SharedBuffer*>(sectionView_);
    for (int i = 0; i < 20; ++i) {
        if (shm->header.initialized) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    if (!shm->header.initialized) {
        std::cerr << "[Kernel] Driver shared buffer not initialized after timeout.\n";
        pNtUnmapViewOfSection(GetCurrentProcess(), sectionView_);
        sectionView_ = nullptr;
        pNtClose(sectionHandle_);
        sectionHandle_ = INVALID_HANDLE_VALUE;
        return false;
    }

    /* --- Open request event (name discovered from header) --- */
    UNICODE_STRING reqEventName;
    RtlInitUnicodeString(&reqEventName, shm->header.requestEventName);

    OBJECT_ATTRIBUTES reqEventAttr;
    InitializeObjectAttributes(&reqEventAttr, &reqEventName,
                               OBJ_CASE_INSENSITIVE, NULL, NULL);

    status = pNtOpenEvent(&requestEvent_, EVENT_MODIFY_STATE, &reqEventAttr);
    if (!NT_SUCCESS(status)) {
        std::cerr << "[Kernel] NtOpenEvent (request) failed: 0x" << std::hex << status << std::dec
                  << " name=" << shm->header.requestEventName << "\n";
        pNtUnmapViewOfSection(GetCurrentProcess(), sectionView_);
        sectionView_ = nullptr;
        pNtClose(sectionHandle_);
        sectionHandle_ = INVALID_HANDLE_VALUE;
        return false;
    }

    /* --- Open response event (name discovered from header) --- */
    UNICODE_STRING respEventName;
    RtlInitUnicodeString(&respEventName, shm->header.responseEventName);

    OBJECT_ATTRIBUTES respEventAttr;
    InitializeObjectAttributes(&respEventAttr, &respEventName,
                               OBJ_CASE_INSENSITIVE, NULL, NULL);

    status = pNtOpenEvent(&responseEvent_, EVENT_WAIT, &respEventAttr);
    if (!NT_SUCCESS(status)) {
        std::cerr << "[Kernel] NtOpenEvent (response) failed: 0x" << std::hex << status << std::dec
                  << " name=" << shm->header.responseEventName << "\n";
        pNtClose(requestEvent_);
        requestEvent_ = INVALID_HANDLE_VALUE;
        pNtUnmapViewOfSection(GetCurrentProcess(), sectionView_);
        sectionView_ = nullptr;
        pNtClose(sectionHandle_);
        sectionHandle_ = INVALID_HANDLE_VALUE;
        return false;
    }

    return true;
}

void Kernel::close() {
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) return;

    PNtClose pNtClose = (PNtClose)GetProcAddress(ntdll, "NtClose");
    PNtUnmapViewOfSection pNtUnmapViewOfSection = (PNtUnmapViewOfSection)GetProcAddress(ntdll, "NtUnmapViewOfSection");

    if (responseEvent_ != INVALID_HANDLE_VALUE && pNtClose) {
        pNtClose(responseEvent_);
        responseEvent_ = INVALID_HANDLE_VALUE;
    }

    if (requestEvent_ != INVALID_HANDLE_VALUE && pNtClose) {
        pNtClose(requestEvent_);
        requestEvent_ = INVALID_HANDLE_VALUE;
    }

    if (sectionView_ && pNtUnmapViewOfSection) {
        pNtUnmapViewOfSection(GetCurrentProcess(), sectionView_);
        sectionView_ = nullptr;
    }

    if (sectionHandle_ != INVALID_HANDLE_VALUE && pNtClose) {
        pNtClose(sectionHandle_);
        sectionHandle_ = INVALID_HANDLE_VALUE;
    }
}

bool Kernel::read(uint32_t pid, uint64_t address, void* buffer, size_t size) const {
    if (!isOpen() || !buffer || size == 0 || size > 2048) {
        return false;
    }

    SharedBuffer* shm = static_cast<SharedBuffer*>(sectionView_);

    shm->request.type    = REQ_READ_MEMORY;
    shm->request.pid     = pid;
    shm->request.address = address;
    shm->request.size    = static_cast<ULONG>(size);

    /* Signal the driver to process the request */
    SetEvent(requestEvent_);

    /* Wait for the driver to write the response */
    WaitForSingleObjectEx(responseEvent_, INFINITE, FALSE);

    if (!shm->response.success || shm->response.bytesTransferred != size) {
        return false;
    }

    RtlCopyMemory(buffer, shm->response.data, size);
    return true;
}

bool Kernel::write(uint32_t pid, uint64_t address, const void* buffer, size_t size) const {
    if (!isOpen() || !buffer || size == 0 || size > 2048) {
        return false;
    }

    SharedBuffer* shm = static_cast<SharedBuffer*>(sectionView_);

    /* Copy user data into shared buffer for the driver to read */
    RtlCopyMemory(shm->response.data, buffer, size);

    shm->request.type    = REQ_WRITE_MEMORY;
    shm->request.pid     = pid;
    shm->request.address = address;
    shm->request.size    = static_cast<ULONG>(size);

    SetEvent(requestEvent_);
    WaitForSingleObjectEx(responseEvent_, INFINITE, FALSE);

    return shm->response.success && shm->response.bytesTransferred == size;
}

std::vector<uint8_t> Kernel::readBytes(uint32_t pid, uint64_t address, size_t size) const {
    std::vector<uint8_t> buffer(size);
    if (!read(pid, address, buffer.data(), size)) {
        buffer.clear();
    }
    return buffer;
}
