#include "transport.h"
#include <string.h>
#include <wchar.h>

static USHORT word_at(const unsigned char *p) {
    return (USHORT)(p[0] | ((USHORT)p[1] << 8));
}
static DWORD dword_at(const unsigned char *p) {
    return (DWORD)p[0] | ((DWORD)p[1]<<8) | ((DWORD)p[2]<<16) | ((DWORD)p[3]<<24);
}
static BOOL fail(DWORD code) { SetLastError(code); return FALSE; }

BOOL xeltek_hardware_id(const wchar_t *id) {
    const wchar_t *ids[] = {L"USB\\VID_0547&PID_1002", L"USB\\VID_6022&PID_0001"};
    for (unsigned i=0; i<2; ++i) {
        size_t n=wcslen(ids[i]);
        if (!_wcsnicmp(id,ids[i],n) && (id[n]==L'\\' || id[n]==0)) return TRUE;
    }
    return FALSE;
}

/* The five IOCTLs used by this exact SP9000.EXE, recovered from XEUSB.SYS.
 * All lengths are checked before touching the USB device. Unknown operations
 * fail explicitly; this layer never fabricates device replies. */
BOOL xeltek_ioctl(UsbTransport *usb, DWORD code, const void *input, DWORD ilen,
                  void *output, DWORD olen, DWORD *returned) {
    DWORD ignored=0;
    if (!returned) returned=&ignored;
    *returned=0;
    if (!usb || !usb->control || !usb->transfer || (!input && ilen) || (!output && olen))
        return fail(ERROR_INVALID_PARAMETER);
    const unsigned char *in=(const unsigned char *)input;
    ULONG done=0;
    UsbSetup setup={0};
    switch(code) {
    case 0x22204e: /* METHOD_OUT_DIRECT */
    case 0x222051: { /* METHOD_IN_DIRECT; endpoint descriptor determines direction */
        if (ilen!=4) return fail(ERROR_INVALID_PARAMETER);
        DWORD pipe=dword_at(in);
        if (pipe>=usb->pipe_count || pipe>=32 ||
            (usb->pipes[pipe].type!=2 && usb->pipes[pipe].type!=3))
            return fail(ERROR_INVALID_PARAMETER);
        if (!usb->transfer(usb->context,usb->pipes[pipe].id,output,olen,&done)) return FALSE;
        if (done>olen) return fail(ERROR_INVALID_DATA);
        *returned=done;
        return TRUE;
    }
    case 0x222014: { /* vendor request: req,pad,value,index,length,in,data */
        if (ilen!=10) return fail(ERROR_INVALID_PARAMETER);
        setup.type=in[8]?0xc0:0x40;
        setup.request=in[0]; setup.value=word_at(in+2);
        setup.index=word_at(in+4); setup.length=word_at(in+6);
        if(setup.length>4096) return fail(ERROR_NOT_SUPPORTED);
        if (in[8] && (setup.length>ilen || setup.length>olen))
            return fail(ERROR_INSUFFICIENT_BUFFER);
        unsigned char *buffer=setup.length?HeapAlloc(GetProcessHeap(),0,setup.length):NULL;
        if (setup.length && !buffer) return fail(ERROR_NOT_ENOUGH_MEMORY);
        if (!in[8]) {
            if (setup.length==1) buffer[0]=in[9];
            else for (ULONG i=0; i<setup.length; ++i) buffer[i]=(UCHAR)i;
        }
        BOOL ok=usb->control(usb->context,setup,buffer,setup.length,&done);
        DWORD error=GetLastError();
        if (ok && done>setup.length) { ok=FALSE; error=ERROR_INVALID_DATA; }
        if (ok && in[8]) {
            /* Original METHOD_BUFFERED output aliases the input; use the caller's
             * output only when it can actually hold the received bytes. */
            if (done>olen) { ok=FALSE; error=ERROR_INSUFFICIENT_BUFFER; }
            else { if(done) memcpy(output,buffer,done); *returned=done; }
        } else if (ok && done!=setup.length) { ok=FALSE; error=ERROR_WRITE_FAULT; }
        if (buffer) HeapFree(GetProcessHeap(),0,buffer);
        if (!ok) SetLastError(error);
        return ok;
    }
    case 0x22206d: { /* 8051 RAM download: 64-byte A0 vendor writes */
        if (ilen!=2 || olen>0x10000UL-(ULONG)word_at(in))
            return fail(ERROR_INVALID_PARAMETER);
        ULONG address=word_at(in);
        for (ULONG offset=0; offset<olen;) {
            ULONG count=olen-offset;
            if(count>64) count=64;
            setup=(UsbSetup){0x40,0xa0,(USHORT)(address+offset),0,(USHORT)count};
            if (!usb->control(usb->context,setup,(UCHAR *)output+offset,count,&done)) return FALSE;
            if(done!=count) return fail(ERROR_WRITE_FAULT);
            offset+=count;
        }
        return TRUE; /* XEUSB.SYS reports zero bytes for this operation. */
    }
    case 0x222059: { /* generalized USB class/vendor control request */
        if(ilen!=10 || in[1]<1 || in[1]>2 || in[2]>3 || olen>0xffff)
            return fail(ERROR_INVALID_PARAMETER);
        /* WinUSB's documented control-transfer limit. Do not split arbitrary
         * vendor commands: their packet boundaries can carry protocol meaning. */
        if(olen>4096) return fail(ERROR_NOT_SUPPORTED);
        setup.type=(in[0]?0x80:0) | (in[1]<<5) | in[2];
        setup.request=in[4]; setup.value=word_at(in+6);
        setup.index=word_at(in+8); setup.length=(USHORT)olen;
        if(!usb->control(usb->context,setup,output,olen,&done)) return FALSE;
        if(done>olen) return fail(ERROR_INVALID_DATA);
        *returned=done;
        return TRUE;
    }
    default: return fail(ERROR_NOT_SUPPORTED);
    }
}
