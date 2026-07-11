#pragma once
#include <ntddk.h>

/* ------------------------------------------------------------------ */
/*  Shared names — kernel creates, usermode opens                      */
/*    Section name is fixed (user-mode discovers it first).            */
/*    Event names are randomized at load time to avoid detection.      */
/*    User-mode reads randomized event names from the section header.  */
/* ------------------------------------------------------------------ */

#define CHEAT_SECTION_NAME   L"\\BaseNamedObjects\\Cheat_Section"

/* Max length for a single named object path */
#define CHEAT_MAX_NAME_LEN   64

/* ------------------------------------------------------------------ */
/*  Shared buffer layout                                               */
/* ------------------------------------------------------------------ */

#define SHM_BUFFER_SIZE  4096

/* Header stored at the top of the shared section.                     */
/* User-mode maps the section, reads the event names, then proceeds.   */
typedef struct _ShmHeader {
    WCHAR requestEventName[CHEAT_MAX_NAME_LEN];
    WCHAR responseEventName[CHEAT_MAX_NAME_LEN];
    ULONG initialized;          /* set to 1 when driver is ready        */
    ULONG pad;
} ShmHeader, *PShmHeader;

typedef enum REQUEST_TYPE {
    REQ_NONE         = 0,
    REQ_READ_MEMORY  = 1,
    REQ_WRITE_MEMORY = 2,
    REQ_ALLOC_MEMORY = 3,
} REQUEST_TYPE;

#pragma pack(push, 1)

typedef struct _SharedRequest {
    REQUEST_TYPE type;
    ULONG        pid;
    ULONGLONG    address;
    ULONG        size;
    ULONG        padding;
} SharedRequest, *PSharedRequest;

typedef struct _SharedResponse {
    BOOLEAN      success;
    ULONG        bytesTransferred;
    ULONGLONG    allocatedAddress;
    /* Remaining bytes in the 4096-page for payload */
    UCHAR        data[SHM_BUFFER_SIZE
                      - sizeof(ShmHeader)
                      - sizeof(SharedRequest)
                      - sizeof(BOOLEAN)
                      - sizeof(ULONG)
                      - sizeof(ULONGLONG)];
} SharedResponse, *PSharedResponse;

#pragma pack(pop)

/* ------------------------------------------------------------------ */
/*  Shared buffer (top-level container)                                */
/* ------------------------------------------------------------------ */

typedef struct _SharedBuffer {
    ShmHeader       header;
    SharedRequest   request;
    SharedResponse  response;
} SharedBuffer, *PSharedBuffer;

/* ------------------------------------------------------------------ */
/*  Pool tag                                                           */
/* ------------------------------------------------------------------ */

#define CHEAT_POOL_TAG 'teC'  /* "Cheat" reversed */
