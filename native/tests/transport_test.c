#include "../src/transport.h"
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <stdlib.h>
#include "log_output.h"

typedef struct {
    unsigned calls;UsbSetup setup[8];ULONG sizes[8];UCHAR pipe;
    unsigned char data[512];BOOL failure;LONG short_by;
} Mock;
#define CHECK(condition) do {if(!(condition)) {fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#condition);exit(1);}}while(0)
static BOOL ctrl(void *ctx,UsbSetup s,void *data,ULONG length,ULONG *done) {
    Mock *m=ctx;CHECK(m->calls<8);
    m->setup[m->calls]=s;m->sizes[m->calls++]=length;
    if(m->failure) {SetLastError(ERROR_SEM_TIMEOUT);return FALSE;}
    if(length) {if(s.type&0x80) memset(data,0x5a,length);else memcpy(m->data,data,length);}
    *done=length-m->short_by;return TRUE;
}
static BOOL bulk(void *ctx,UCHAR pipe,void *data,ULONG length,ULONG *done) {
    Mock *m=ctx;m->calls++;m->pipe=pipe;
    if(m->failure) {SetLastError(ERROR_DEVICE_NOT_CONNECTED);return FALSE;}
    if(pipe&0x80) memset(data,0xa5,length);
    *done=length-m->short_by;return TRUE;
}
int main(void) {
    CHECK(xlog_init(NULL,FALSE,"transport-test"));
    Mock m={0};UsbTransport usb={.context=&m,.control=ctrl,.transfer=bulk,
        .pipes={{0x82,2},{0x06,2},{0x83,3},{0x84,1}},.pipe_count=4};
    unsigned char in[10]={0},out[160]={0};DWORD n=999;
    CHECK(xeltek_hardware_id(L"USB\\VID_0547&PID_1002\\A"));
    CHECK(xeltek_hardware_id(L"usb\\vid_6022&pid_0001\\B"));
    CHECK(!xeltek_hardware_id(L"USB\\VID_0547&PID_1002&MI_00\\A"));
    CHECK(!xeltek_hardware_id(L"USB\\VID_0547&PID_10020\\A"));
    CHECK(!xeltek_hardware_id(L"USB\\VID_045E&PID_0823\\A"));
    CHECK(xeltek_ioctl(&usb,0x222051,in,4,out,16,&n));
    CHECK(n==16 && m.pipe==0x82 && out[0]==0xa5);
    in[0]=1;CHECK(xeltek_ioctl(&usb,0x22204e,in,4,out,16,&n));CHECK(m.pipe==6);
    m.short_by=3;CHECK(xeltek_ioctl(&usb,0x222051,in,4,out,16,&n));CHECK(n==13);
    m.short_by=0;m.failure=TRUE;
    CHECK(!xeltek_ioctl(&usb,0x222051,in,4,out,16,&n));CHECK(n==0 && GetLastError()==ERROR_DEVICE_NOT_CONNECTED);
    m.failure=FALSE;unsigned before=m.calls;
    in[0]=4;CHECK(!xeltek_ioctl(&usb,0x222051,in,4,out,16,&n));CHECK(m.calls==before);
    in[0]=3;CHECK(!xeltek_ioctl(&usb,0x222051,in,4,out,16,&n));CHECK(m.calls==before);
    memset(&m,0,sizeof(m));memset(in,0,10);
    in[0]=0xa0;in[2]=0x92;in[3]=0x7f;in[6]=1;in[9]=1;
    CHECK(xeltek_ioctl(&usb,0x222014,in,10,NULL,0,&n));
    CHECK(n==0 && m.setup[0].type==0x40 && m.setup[0].request==0xa0 &&
          m.setup[0].value==0x7f92 && m.setup[0].length==1 && m.data[0]==1);
    m.failure=TRUE;CHECK(!xeltek_ioctl(&usb,0x222014,in,10,NULL,0,&n));CHECK(GetLastError()==ERROR_SEM_TIMEOUT);
    memset(&m,0,sizeof(m));in[6]=11;in[8]=1;
    CHECK(!xeltek_ioctl(&usb,0x222014,in,10,out,160,&n));CHECK(m.calls==0);
    memset(in,0,10);in[0]=0x34;in[1]=0x12;
    CHECK(xeltek_ioctl(&usb,0x22206d,in,2,out,130,&n));
    CHECK(m.calls==3 && m.sizes[0]==64 && m.sizes[1]==64 && m.sizes[2]==2 && n==0);
    CHECK(m.setup[0].value==0x1234 && m.setup[1].value==0x1274 && m.setup[2].value==0x12b4);
    CHECK(m.setup[2].request==0xa0 && m.setup[2].type==0x40);
    memset(&m,0,sizeof(m));m.short_by=1;
    CHECK(!xeltek_ioctl(&usb,0x22206d,in,2,out,130,&n));CHECK(m.calls==1 && GetLastError()==ERROR_WRITE_FAULT);
    memset(&m,0,sizeof(m));in[0]=0xf0;in[1]=0xff;
    CHECK(!xeltek_ioctl(&usb,0x22206d,in,2,out,130,&n));CHECK(m.calls==0);
    memset(in,0,10);in[0]=1;in[1]=2;in[2]=0;in[4]=0xa3;
    in[6]=0x34;in[7]=0x12;in[8]=0xef;in[9]=0xbe;
    CHECK(xeltek_ioctl(&usb,0x222059,in,10,out,128,&n));
    CHECK(n==128 && out[0]==0x5a && m.setup[0].type==0xc0 &&
          m.setup[0].request==0xa3 && m.setup[0].value==0x1234 && m.setup[0].index==0xbeef);
    memset(&m,0,sizeof(m));in[1]=3;
    CHECK(!xeltek_ioctl(&usb,0x222059,in,10,out,128,&n));CHECK(m.calls==0);
    in[1]=2;CHECK(!xeltek_ioctl(&usb,0x222059,in,10,out,4097,&n));
    CHECK(GetLastError()==ERROR_NOT_SUPPORTED && m.calls==0);
    CHECK(!xeltek_ioctl(&usb,0xdeadbeef,in,10,out,128,&n));CHECK(GetLastError()==ERROR_NOT_SUPPORTED && m.calls==0);
    CHECK(!xeltek_ioctl(&usb,0x222051,NULL,4,out,10,&n));CHECK(m.calls==0);
    CHECK(!xeltek_ioctl(&usb,0x222051,in,4,NULL,10,&n));CHECK(m.calls==0);
    puts("PASS: hardware ID isolation, endpoint mapping, bulk I/O, short transfers, errors,");
    puts("      vendor reset, firmware chunks, address bounds, control setup, invalid requests.");
    return 0;
}
