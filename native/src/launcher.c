#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <string.h>
#include "log.h"

int WINAPI WinMain(HINSTANCE instance,HINSTANCE previous,LPSTR command,int show) {
    (void)instance;(void)previous;(void)command;(void)show;
    if(!xlog_init(NULL,FALSE,"launcher") || !xlog_write("INFO",0,"Launcher started.")) {
        MessageBoxA(NULL,"Cannot write SP9000U\\bin\\superpro9000u.log. Make sure the software folder is writable.","SUPERPRO 9000U",MB_ICONERROR);return 1;
    }
    char root[MAX_PATH],exe[MAX_PATH];
    DWORD count=GetModuleFileNameA(NULL,root,MAX_PATH);
    if(!count || count>=MAX_PATH-48) {xlog_write("ERROR",ERROR_BUFFER_OVERFLOW,"Executable path is too long.");return 1;}
    char *last=strrchr(root,'\\');if(!last) {xlog_write("ERROR",ERROR_BAD_PATHNAME,"Cannot locate software directory.");return 1;}*last=0;
#ifdef USB_SETUP_LAUNCHER
    snprintf(exe,sizeof(exe),"%s\\XeltekUsbSetup.exe",root);
    xlog_write("INFO",0,"Starting WinUSB setup with administrator privileges.");
    HINSTANCE result=ShellExecuteA(NULL,"runas",exe,"--install",root,SW_SHOWNORMAL);
#else
    char working[MAX_PATH],ini[MAX_PATH],library[MAX_PATH],algorithms[MAX_PATH];
    snprintf(working,sizeof(working),"%s\\SP9000U\\bin",root);
    snprintf(exe,sizeof(exe),"%s\\sp9000-win11.exe",working);
    snprintf(ini,sizeof(ini),"%s\\agspedit.ini",working);
    snprintf(library,sizeof(library),"%s\\SP9000U\\lib",root);
    snprintf(algorithms,sizeof(algorithms),"%s\\SP9000U\\algo9",root);
    if(!WritePrivateProfileStringA("Path","PofDllDir",library,ini) ||
       !WritePrivateProfileStringA("Path","AlgoDir",algorithms,ini) ||
       !WritePrivateProfileStringA("Path","AlgoDir_",algorithms,ini) ||
       !WritePrivateProfileStringA("Algo","Ext",".usb",ini)) {
        xlog_write("ERROR",GetLastError(),"Cannot save device library paths.");
        MessageBoxA(NULL,"The device library paths could not be saved. See superpro9000u.log.","SUPERPRO 9000U",MB_ICONERROR);return 1;
    }
    HINSTANCE result=ShellExecuteA(NULL,"open",exe,NULL,working,SW_SHOWNORMAL);
#endif
    if((INT_PTR)result<=32) {
        xlog_write("ERROR",GetLastError(),"Application could not be started; shell_result=%lld.",(long long)(INT_PTR)result);
        MessageBoxA(NULL,"The application could not be started. See superpro9000u.log.","SUPERPRO 9000U",MB_ICONERROR);return 1;
    }
    xlog_write("INFO",0,"Application launch requested successfully.");return 0;
}
