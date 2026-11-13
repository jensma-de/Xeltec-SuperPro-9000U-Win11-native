#ifndef XELTEK_TRANSPORT_H
#define XELTEK_TRANSPORT_H
#include <windows.h>
#include <stdint.h>

/* Private interface used by the inbox WinUSB installation helper. */
static const GUID XELTEK_INTERFACE_GUID =
    {0x30b3a88c,0x74cf,0x4c7b,{0x9b,0x6a,0x73,0x07,0xa3,0x5f,0xe0,0xa1}};
#define XELTEK_GUID_TEXT L"{30B3A88C-74CF-4C7B-9B6A-7307A35FE0A1}"

typedef struct {
    unsigned char type, request;
    unsigned short value, index, length;
} UsbSetup;
typedef struct { unsigned char id, type; } UsbPipe;
typedef struct {
    void *context;
    BOOL (*control)(void *, UsbSetup, void *, ULONG, ULONG *);
    BOOL (*transfer)(void *, UCHAR, void *, ULONG, ULONG *);
    UsbPipe pipes[32];
    ULONG pipe_count;
} UsbTransport;

BOOL xeltek_ioctl(UsbTransport *, DWORD, const void *, DWORD,
                  void *, DWORD, DWORD *);
BOOL xeltek_hardware_id(const wchar_t *);
#endif
