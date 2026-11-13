#define WIN32_LEAN_AND_MEAN
#include "log.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <wchar.h>

static wchar_t log_path[MAX_PATH];
static const char *log_component="unknown";

BOOL xlog_init(HMODULE module,BOOL module_in_bin,const char *component) {
    DWORD saved=GetLastError();
    DWORD count=GetModuleFileNameW(module,log_path,MAX_PATH);
    wchar_t *slash=count && count<MAX_PATH?wcsrchr(log_path,L'\\'):NULL;
    const wchar_t *suffix=module_in_bin?L"superpro9000u.log":L"SP9000U\\bin\\superpro9000u.log";
    if(!slash || (size_t)(slash-log_path)+1+wcslen(suffix)>=MAX_PATH) {
        log_path[0]=0;SetLastError(saved);return FALSE;
    }
    wcscpy(slash+1,suffix);log_component=component;
    SetLastError(saved);return TRUE;
}
const wchar_t *xlog_path(void) {return log_path;}

BOOL xlog_write(const char *level,DWORD error,const char *format,...) {
    DWORD saved=GetLastError();
    char message[8192],record[9216],reason[512]="";
    va_list args;va_start(args,format);
    vsnprintf(message,sizeof(message),format,args);va_end(args);
    /* One physical line per event, even for console messages or Windows errors. */
    for(char *p=message;*p;++p) if(*p=='\n' || *p=='\r') *p=' ';
    if(error) {
        FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,NULL,error,
            MAKELANGID(LANG_ENGLISH,SUBLANG_ENGLISH_US),reason,sizeof(reason),NULL);
        for(char *p=reason;*p;++p) if(*p=='\n' || *p=='\r') *p=' ';
    }
    SYSTEMTIME now;GetSystemTime(&now);
    int count=snprintf(record,sizeof(record),
        "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ [%s] [%s] pid=%lu tid=%lu error=%lu %s%s%s\r\n",
        now.wYear,now.wMonth,now.wDay,now.wHour,now.wMinute,now.wSecond,now.wMilliseconds,
        level,log_component,GetCurrentProcessId(),GetCurrentThreadId(),error,message,
        error && *reason?" | ":"",reason);
    BOOL ok=FALSE;
    if(!*log_path || count<0 || (size_t)count>=sizeof(record)) goto finish;
    HANDLE file=CreateFileW(log_path,FILE_APPEND_DATA|GENERIC_READ,
        FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,NULL,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
    if(file==INVALID_HANDLE_VALUE) goto finish;
    /* All C and Python writers use the first byte as a cross-process append lock. */
    OVERLAPPED lock={0};
    if(LockFileEx(file,LOCKFILE_EXCLUSIVE_LOCK,0,1,0,&lock)) {
        DWORD written=0;
        ok=WriteFile(file,record,(DWORD)count,&written,NULL) && written==(DWORD)count;
        UnlockFileEx(file,0,1,0,&lock);
    }
    CloseHandle(file);
finish:
    SetLastError(saved);return ok;
}
BOOL xlog_write_w(const char *level,DWORD error,const wchar_t *message) {
    DWORD saved=GetLastError();char utf8[8192];
    int count=WideCharToMultiByte(CP_UTF8,0,message,-1,utf8,sizeof(utf8),NULL,NULL);
    BOOL ok=xlog_write(level,error,"%s",count?utf8:"Message could not be converted to UTF-8.");
    SetLastError(saved);return ok;
}
