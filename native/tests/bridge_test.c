#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "log_output.h"
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL line %d error %lu: %s\n",__LINE__,GetLastError(),#x);return 1;}}while(0)
typedef HANDLE (WINAPI *OpenFn)(LPCSTR,DWORD,DWORD,LPSECURITY_ATTRIBUTES,DWORD,DWORD,HANDLE);
typedef BOOL (WINAPI *CloseFn)(HANDLE);
typedef BOOL (WINAPI *IoctlFn)(HANDLE,DWORD,LPVOID,DWORD,LPVOID,DWORD,LPDWORD,LPOVERLAPPED);
int main(void) {
    CHECK(xlog_init(NULL,FALSE,"bridge-test"));
    char path[MAX_PATH],file[MAX_PATH],directory[MAX_PATH];
    CHECK(GetModuleFileNameA(NULL,path,MAX_PATH));
    char *slash=strrchr(path,'\\');CHECK(slash);strcpy(slash+1,"SP9000U\\bin\\xusb32.dll");
    HMODULE dll=LoadLibraryA(path);CHECK(dll);
    OpenFn open=(OpenFn)GetProcAddress(dll,"CreateFileA");
    CloseFn close=(CloseFn)GetProcAddress(dll,"CloseHandle");
    IoctlFn ioctl=(IoctlFn)GetProcAddress(dll,"DeviceIoControl");
    CHECK(open && close && ioctl);
    CHECK(GetProcAddress(dll,"GetTickCount")==GetProcAddress(GetModuleHandleA("kernel32.dll"),"GetTickCount"));
    CHECK(GetTempPathA(MAX_PATH,directory));CHECK(GetTempFileNameA(directory,"xel",0,file));
    HANDLE h=open(file,GENERIC_READ|GENERIC_WRITE,0,NULL,OPEN_EXISTING,0,NULL);CHECK(h!=INVALID_HANDLE_VALUE);
    DWORD count=0;char value[]="forwarding check",readback[32]={0};
    CHECK(WriteFile(h,value,sizeof(value),&count,NULL));CHECK(count==sizeof(value));
    CHECK(SetFilePointer(h,0,NULL,FILE_BEGIN)==0);
    CHECK(ReadFile(h,readback,sizeof(value),&count,NULL));CHECK(!memcmp(value,readback,sizeof(value)));
    CHECK(!ioctl(h,0xdeadbeef,NULL,0,NULL,0,&count,NULL));
    CHECK(close(h));CHECK(DeleteFileA(file));
    h=open("\\\\.\\Xeltekusb-0",GENERIC_WRITE,FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
    if(h==INVALID_HANDLE_VALUE) {
        CHECK(GetLastError()==ERROR_FILE_NOT_FOUND || GetLastError()==ERROR_NO_MORE_ITEMS ||
              GetLastError()==ERROR_ACCESS_DENIED || GetLastError()==ERROR_DUP_NAME);
        puts("PASS: real x86 DLL loading, KERNEL32 forwarder, normal file I/O, error forwarding,");
        puts("      absent programmer reported without a fabricated connection.");
    } else {CHECK(close(h));puts("PASS: file forwarding and read-only USB open/close.");}
    CHECK(FreeLibrary(dll));return 0;
}
