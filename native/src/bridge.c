#define WIN32_LEAN_AND_MEAN
#include "transport.h"
#include "log.h"
#include <setupapi.h>
#include <winusb.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

typedef struct {
    HANDLE file;
    WINUSB_INTERFACE_HANDLE winusb;
    UsbTransport usb;
} Connection;
static SRWLOCK connections_lock=SRWLOCK_INIT;
static Connection connections[16];

static void log_line(const char *operation,DWORD code,DWORD length,DWORD result,DWORD error) {
    xlog_write(error?"ERROR":"INFO",error,"operation=%s code=0x%08lx length=%lu result=%lu",
               operation,code,length,result);
}
static BOOL control(void *context,UsbSetup s,void *buffer,ULONG length,ULONG *done) {
    Connection *c=context;
    WINUSB_SETUP_PACKET packet={s.type,s.request,s.value,s.index,s.length};
    return WinUsb_ControlTransfer(c->winusb,packet,buffer,length,done,NULL);
}
static BOOL transfer(void *context,UCHAR pipe,void *buffer,ULONG length,ULONG *done) {
    Connection *c=context;
    if(pipe&0x80) return WinUsb_ReadPipe(c->winusb,pipe,buffer,length,done,NULL);
    return WinUsb_WritePipe(c->winusb,pipe,buffer,length,done,NULL);
}
static BOOL allowed_device(HDEVINFO set,SP_DEVINFO_DATA *dev) {
    wchar_t instance[512];
    return SetupDiGetDeviceInstanceIdW(set,dev,instance,512,NULL) && xeltek_hardware_id(instance);
}

static HANDLE open_programmer(void) {
    HDEVINFO set=SetupDiGetClassDevsW(&XELTEK_INTERFACE_GUID,NULL,NULL,
                                    DIGCF_PRESENT|DIGCF_DEVICEINTERFACE);
    if(set==INVALID_HANDLE_VALUE) {
        log_line("enumerate",0,0,0,GetLastError());return INVALID_HANDLE_VALUE;
    }
    DWORD final_error=ERROR_FILE_NOT_FOUND;
    HANDLE answer=INVALID_HANDLE_VALUE;
    AcquireSRWLockExclusive(&connections_lock);
    Connection *c=NULL;
    for(unsigned i=0;i<16;++i) if(!connections[i].file) {c=&connections[i]; break;}
    if(!c) {final_error=ERROR_TOO_MANY_OPEN_FILES; goto finish;}
    /* Never silently choose between multiple devices with shared legacy IDs. */
    DWORD matches=0;
    for(DWORD i=0;;++i) {
        SP_DEVICE_INTERFACE_DATA it={.cbSize=sizeof(it)};
        if(!SetupDiEnumDeviceInterfaces(set,NULL,&XELTEK_INTERFACE_GUID,i,&it)) break;
        SP_DEVINFO_DATA dev={.cbSize=sizeof(dev)};
        DWORD needed=0;
        SetupDiGetDeviceInterfaceDetailW(set,&it,NULL,0,&needed,NULL);
        PSP_DEVICE_INTERFACE_DETAIL_DATA_W detail=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,needed);
        if(!detail) {final_error=ERROR_NOT_ENOUGH_MEMORY; goto finish;}
        detail->cbSize=sizeof(*detail);
        BOOL ok=SetupDiGetDeviceInterfaceDetailW(set,&it,detail,needed,NULL,&dev);
        if(ok && allowed_device(set,&dev)) ++matches;
        HeapFree(GetProcessHeap(),0,detail);
    }
    if(matches>1) {final_error=ERROR_DUP_NAME; goto finish;}
    for(DWORD i=0;;++i) {
        SP_DEVICE_INTERFACE_DATA it={.cbSize=sizeof(it)};
        if(!SetupDiEnumDeviceInterfaces(set,NULL,&XELTEK_INTERFACE_GUID,i,&it)) break;
        SP_DEVINFO_DATA dev={.cbSize=sizeof(dev)};
        DWORD needed=0;
        SetupDiGetDeviceInterfaceDetailW(set,&it,NULL,0,&needed,NULL);
        PSP_DEVICE_INTERFACE_DETAIL_DATA_W detail=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,needed);
        if(!detail) {final_error=ERROR_NOT_ENOUGH_MEMORY;break;}
        detail->cbSize=sizeof(*detail);
        if(!SetupDiGetDeviceInterfaceDetailW(set,&it,detail,needed,NULL,&dev) || !allowed_device(set,&dev)) {
            HeapFree(GetProcessHeap(),0,detail);continue;
        }
        HANDLE file=CreateFileW(detail->DevicePath,GENERIC_READ|GENERIC_WRITE,
                FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,NULL);
        HeapFree(GetProcessHeap(),0,detail);
        if(file==INVALID_HANDLE_VALUE) {final_error=GetLastError();continue;}
        c->file=file;
        if(!WinUsb_Initialize(file,&c->winusb)) {
            final_error=GetLastError();CloseHandle(file);ZeroMemory(c,sizeof(*c));continue;
        }
        USB_DEVICE_DESCRIPTOR descriptor;
        ULONG done=0;
        if(!WinUsb_GetDescriptor(c->winusb,USB_DEVICE_DESCRIPTOR_TYPE,0,0,
                 (PUCHAR)&descriptor,sizeof(descriptor),&done) || done!=sizeof(descriptor) ||
             !((descriptor.idVendor==0x0547 && descriptor.idProduct==0x1002) ||
               (descriptor.idVendor==0x6022 && descriptor.idProduct==0x0001))) {
            final_error=ERROR_BAD_DEVICE; goto close_connection;
        }
        USB_INTERFACE_DESCRIPTOR iface;
        if(!WinUsb_QueryInterfaceSettings(c->winusb,0,&iface) || iface.bNumEndpoints>32) {
            final_error=ERROR_BAD_DEVICE;goto close_connection;
        }
        c->usb.context=c;c->usb.control=control;c->usb.transfer=transfer;
        c->usb.pipe_count=iface.bNumEndpoints;
        for(UCHAR index=0;index<iface.bNumEndpoints;++index) {
            WINUSB_PIPE_INFORMATION pipe;
            if(!WinUsb_QueryPipe(c->winusb,0,index,&pipe)) {
                final_error=GetLastError();goto close_connection;
            }
            c->usb.pipes[index]=(UsbPipe){pipe.PipeId,(UCHAR)pipe.PipeType};
            ULONG timeout=10000;
            if(!WinUsb_SetPipePolicy(c->winusb,pipe.PipeId,PIPE_TRANSFER_TIMEOUT,sizeof(timeout),&timeout)) {
                final_error=GetLastError();goto close_connection;
            }
            log_line("pipe",pipe.PipeId,index,pipe.PipeType,0);
        }
        answer=file;break;
close_connection:
        WinUsb_Free(c->winusb);CloseHandle(file);ZeroMemory(c,sizeof(*c));
    }
finish:
    ReleaseSRWLockExclusive(&connections_lock);
    SetupDiDestroyDeviceInfoList(set);
    if(answer==INVALID_HANDLE_VALUE) SetLastError(final_error);
    log_line("open",0,0,answer!=INVALID_HANDLE_VALUE,answer==INVALID_HANDLE_VALUE?final_error:0);
    return answer;
}

HANDLE WINAPI XCreateFileA(LPCSTR path,DWORD access,DWORD sharing,LPSECURITY_ATTRIBUTES security,
                           DWORD disposition,DWORD attributes,HANDLE template) {
    if(path && !_stricmp(path,"\\\\.\\Xeltekusb-0")) return open_programmer();
    return CreateFileA(path,access,sharing,security,disposition,attributes,template);
}
BOOL WINAPI XCloseHandle(HANDLE file) {
    BOOL programmer=FALSE;
    AcquireSRWLockExclusive(&connections_lock);
    for(unsigned i=0;i<16;++i) if(connections[i].file==file && file) {
        programmer=TRUE;
        WinUsb_Free(connections[i].winusb);
        ZeroMemory(&connections[i],sizeof(connections[i]));break;
    }
    ReleaseSRWLockExclusive(&connections_lock);
    BOOL ok=CloseHandle(file);
    DWORD error=ok?0:GetLastError();
    if(programmer) log_line("close",0,0,ok,error);
    if(!ok) SetLastError(error);
    return ok;
}
BOOL WINAPI XDeviceIoControl(HANDLE file,DWORD code,LPVOID input,DWORD ilen,
                             LPVOID output,DWORD olen,LPDWORD returned,LPOVERLAPPED overlapped) {
    AcquireSRWLockShared(&connections_lock);
    for(unsigned i=0;i<16;++i) if(connections[i].file==file && file) {
        if(overlapped) {
            ReleaseSRWLockShared(&connections_lock);
            if(returned) *returned=0;
            log_line("unsupported-overlapped-ioctl",code,olen,0,ERROR_NOT_SUPPORTED);
            SetLastError(ERROR_NOT_SUPPORTED);return FALSE;
        }
        BOOL ok=xeltek_ioctl(&connections[i].usb,code,input,ilen,output,olen,returned);
        DWORD error=ok?0:GetLastError();
        ReleaseSRWLockShared(&connections_lock);
        log_line("ioctl",code,olen,returned?*returned:0,error);
        if(!ok) SetLastError(error);
        return ok;
    }
    ReleaseSRWLockShared(&connections_lock);
    return DeviceIoControl(file,code,input,ilen,output,olen,returned,overlapped);
}
BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID reserved) {
    (void)reserved;
    if(reason==DLL_PROCESS_ATTACH) {
        xlog_init(instance,TRUE,"usb-bridge");DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}
