#ifndef UNICODE
#define UNICODE 1
#endif
#ifndef _UNICODE
#define _UNICODE 1
#endif
#define WIN32_LEAN_AND_MEAN
#include "transport.h"
#include "log.h"
#include <setupapi.h>
#include <newdev.h>
#include <stdio.h>
#include <wchar.h>
#include <stdarg.h>

static int report_message(FILE *stream,const char *level,DWORD error,const wchar_t *format,...) {
    DWORD saved=GetLastError();wchar_t text[4096];
    va_list args;va_start(args,format);vswprintf(text,4096,format,args);va_end(args);
    xlog_write_w(level,error,text);
    int result=fputws(text,stream);fflush(stream);SetLastError(saved);return result;
}
#define wprintf(...) report_message(stdout,"INFO",0,__VA_ARGS__)
#define fwprintf(stream,...) report_message(stream,"ERROR",GetLastError(),__VA_ARGS__)

static BOOL register_interface(HDEVINFO set,SP_DEVINFO_DATA *device) {
    /* DIREG_DEV already opens the hardware's Device Parameters key. */
    HKEY parameters=SetupDiOpenDevRegKey(set,device,DICS_FLAG_GLOBAL,0,DIREG_DEV,KEY_READ|KEY_WRITE);
    if(parameters==INVALID_HANDLE_VALUE) return FALSE;
    /* Preserve existing interface GUIDs rather than overwriting other software. */
    wchar_t existing[4096]={0};DWORD bytes=sizeof(existing)-sizeof(wchar_t),type=0;
    LONG status=RegQueryValueExW(parameters,L"DeviceInterfaceGUIDs",NULL,&type,(BYTE *)existing,&bytes);
    if(status!=ERROR_SUCCESS && status!=ERROR_FILE_NOT_FOUND) {
        RegCloseKey(parameters);SetLastError(status);return FALSE;
    }
    if(status==ERROR_SUCCESS && type!=REG_MULTI_SZ) {
        RegCloseKey(parameters);SetLastError(ERROR_INVALID_DATA);return FALSE;
    }
    size_t used=0;
    if(status==ERROR_SUCCESS) {
        while(existing[used]) {
            if(!_wcsicmp(existing+used,XELTEK_GUID_TEXT)) {RegCloseKey(parameters);return TRUE;}
            used+=wcslen(existing+used)+1;
            if(used>=4000) {RegCloseKey(parameters);SetLastError(ERROR_INSUFFICIENT_BUFFER);return FALSE;}
        }
    }
    size_t length=wcslen(XELTEK_GUID_TEXT);
    if(used+length+2>=4096) {RegCloseKey(parameters);SetLastError(ERROR_INSUFFICIENT_BUFFER);return FALSE;}
    wcscpy(existing+used,XELTEK_GUID_TEXT);existing[used+length+1]=0;
    status=RegSetValueExW(parameters,L"DeviceInterfaceGUIDs",0,REG_MULTI_SZ,
                       (BYTE *)existing,(DWORD)((used+length+2)*sizeof(wchar_t)));
    RegCloseKey(parameters);if(status) SetLastError(status);
    return status==ERROR_SUCCESS;
}

static BOOL select_inbox_winusb(HDEVINFO set,SP_DEVINFO_DATA *device,SP_DRVINFO_DATA_W *selected) {
    SP_DEVINSTALL_PARAMS_W parameters={.cbSize=sizeof(parameters)};
    if(!SetupDiGetDeviceInstallParamsW(set,device,&parameters)) return FALSE;
    parameters.Flags|=DI_ENUMSINGLEINF;
    parameters.FlagsEx|=DI_FLAGSEX_ALLOWEXCLUDEDDRVS;
    UINT count=GetWindowsDirectoryW(parameters.DriverPath,MAX_PATH);
    if(!count || count+16>=MAX_PATH) {SetLastError(ERROR_BUFFER_OVERFLOW);return FALSE;}
    wcscat(parameters.DriverPath,L"\\INF\\winusb.inf");
    if(!SetupDiSetDeviceInstallParamsW(set,device,&parameters) ||
       !SetupDiBuildDriverInfoList(set,device,SPDIT_CLASSDRIVER)) return FALSE;
    for(DWORD index=0;;++index) {
        SP_DRVINFO_DATA_W driver={.cbSize=sizeof(driver)};
        if(!SetupDiEnumDriverInfoW(set,device,SPDIT_CLASSDRIVER,index,&driver)) break;
        BYTE storage[8192]={0};PSP_DRVINFO_DETAIL_DATA_W detail=(void *)storage;
        detail->cbSize=sizeof(*detail);
        if(!SetupDiGetDriverInfoDetailW(set,device,&driver,detail,sizeof(storage),NULL)) continue;
        if(_wcsicmp(detail->SectionName,L"WINUSB")) continue;
        *selected=driver;
        wprintf(L"Windows driver: %ls, section: %ls, provider: %ls\n",
                detail->InfFileName,detail->SectionName,driver.ProviderName);
        return TRUE;
    }
    SetLastError(ERROR_NOT_FOUND);return FALSE;
}

static BOOL install_winusb(HDEVINFO set,SP_DEVINFO_DATA *device) {
    wchar_t service[128]={0};
    SetupDiGetDeviceRegistryPropertyW(set,device,SPDRP_SERVICE,NULL,(BYTE *)service,sizeof(service),NULL);
    if(_wcsicmp(service,L"WinUSB")) {
        /* DiInstallDevice requires a driver node from this device's own list.
         * A node from the global list is not valid here (ERROR_INVALID_PARAMETER).
         * Use the signed inbox INF, without installing an unsigned custom INF. */
        SP_DRVINFO_DATA_W driver={.cbSize=sizeof(driver)};
        if(!select_inbox_winusb(set,device,&driver)) return FALSE;
        BOOL reboot=FALSE;
        BOOL ok=DiInstallDevice(NULL,set,device,&driver,0,&reboot);
        DWORD error=GetLastError();
        SetupDiDestroyDriverInfoList(set,device,SPDIT_CLASSDRIVER);
        if(!ok) {SetLastError(error);return FALSE;}
        wprintf(L"Microsoft WinUSB installed. Computer restart required: %ls\n",reboot?L"yes":L"no");
    }
    if(!register_interface(set,device)) return FALSE;
    SP_PROPCHANGE_PARAMS change={0};
    change.ClassInstallHeader.cbSize=sizeof(change.ClassInstallHeader);
    change.ClassInstallHeader.InstallFunction=DIF_PROPERTYCHANGE;
    change.StateChange=DICS_PROPCHANGE;
    change.Scope=DICS_FLAG_CONFIGSPECIFIC;
    if(SetupDiSetClassInstallParamsW(set,device,&change.ClassInstallHeader,sizeof(change)) &&
       SetupDiCallClassInstaller(DIF_PROPERTYCHANGE,set,device))
        wprintf(L"WinUSB interface registered; USB device restarted.\n");
    else report_message(stdout,"WARNING",GetLastError(),L"WinUSB interface registered. Disconnect and reconnect the USB cable (device restart error %lu).\n",GetLastError());
    return TRUE;
}

static int test_driver_selection(void) {
    /* An in-memory element only: never register this test device with PnP. */
    static const GUID usb_device_class={0x88bae032,0x5a81,0x49f0,{0xbc,0x3d,0xa4,0xff,0x13,0x82,0x16,0xd6}};
    HDEVINFO set=SetupDiCreateDeviceInfoList(NULL,NULL);
    SP_DEVINFO_DATA device={.cbSize=sizeof(device)};
    SP_DRVINFO_DATA_W global_driver={.cbSize=sizeof(global_driver)};
    SP_DRVINFO_DATA_W device_driver={.cbSize=sizeof(device_driver)};
    BOOL ok=FALSE;
    if(set==INVALID_HANDLE_VALUE) goto finish;
    if(!SetupDiCreateDeviceInfoW(set,L"XELTEK_WINUSB_SELECTION_TEST",&usb_device_class,
           L"In-memory selection regression test",NULL,DICD_GENERATE_ID,&device)) {
        if(GetLastError()==ERROR_ACCESS_DENIED) {
            SetupDiDestroyDeviceInfoList(set);
            wprintf(L"SKIP: The in-memory regression test requires administrator privileges.\n");return 77;
        }
        goto finish;
    }
    if(!select_inbox_winusb(set,NULL,&global_driver)) goto finish;
    if(SetupDiSetSelectedDriverW(set,&device,&global_driver) || GetLastError()!=ERROR_INVALID_PARAMETER) {
        fwprintf(stderr,L"The original error 87 was not reproduced as expected.\n");goto finish;
    }
    wprintf(L"PASS: A global driver node used for a device reproduces error 87.\n");
    if(!select_inbox_winusb(set,&device,&device_driver)) goto finish;
    if(!SetupDiSetSelectedDriverW(set,&device,&device_driver)) goto finish;
    SP_DRVINFO_DATA_W selected={.cbSize=sizeof(selected)};
    if(!SetupDiGetSelectedDriverW(set,&device,&selected) || selected.Reserved!=device_driver.Reserved) goto finish;
    wprintf(L"PASS: The device-specific driver node is accepted. No PnP device was registered or installed.\n");
    ok=TRUE;
finish:
    if(!ok) fwprintf(stderr,L"Driver selection self-test failed. Windows error %lu.\n",GetLastError());
    if(set!=INVALID_HANDLE_VALUE) SetupDiDestroyDeviceInfoList(set);
    return ok?0:1;
}

static int run_setup(int argc,wchar_t **argv) {
    if(argc==2 && !wcscmp(argv[1],L"--selftest-selection")) return test_driver_selection();
    if(argc==2 && !wcscmp(argv[1],L"--check-driver")) {
        HDEVINFO set=SetupDiCreateDeviceInfoList(NULL,NULL);
        SP_DRVINFO_DATA_W driver={.cbSize=sizeof(driver)};
        BOOL ok=set!=INVALID_HANDLE_VALUE && select_inbox_winusb(set,NULL,&driver);
        if(!ok) fwprintf(stderr,L"WinUSB driver check failed. Windows error %lu.\n",GetLastError());
        if(set!=INVALID_HANDLE_VALUE) SetupDiDestroyDeviceInfoList(set);
        return ok?0:1;
    }
    BOOL install_console=argc==2 && !wcscmp(argv[1],L"--install-console");
    BOOL install=install_console || (argc==2 && !wcscmp(argv[1],L"--install"));
    BOOL check_device=argc==2 && !wcscmp(argv[1],L"--check-device-driver");
    if(argc>2 || (argc==2 && !install && !check_device && wcscmp(argv[1],L"--list"))) {
        fwprintf(stderr,L"Usage: XeltekUsbSetup.exe [--list | --install | --install-console | --check-driver | --check-device-driver | --selftest-selection]\n");return 2;
    }
    HDEVINFO set=SetupDiGetClassDevsW(NULL,L"USB",NULL,DIGCF_PRESENT|DIGCF_ALLCLASSES);
    if(set==INVALID_HANDLE_VALUE) {fwprintf(stderr,L"SetupAPI error %lu\n",GetLastError());return 1;}
    SP_DEVINFO_DATA selected={0};DWORD matches=0;
    for(DWORD index=0;;++index) {
        SP_DEVINFO_DATA device={.cbSize=sizeof(device)};
        if(!SetupDiEnumDeviceInfo(set,index,&device)) break;
        wchar_t id[512],service[128]={0};
        if(!SetupDiGetDeviceInstanceIdW(set,&device,id,512,NULL) || !xeltek_hardware_id(id)) continue;
        SetupDiGetDeviceRegistryPropertyW(set,&device,SPDRP_SERVICE,NULL,(BYTE *)service,sizeof(service),NULL);
        wprintf(L"Device found: %ls\nDriver: %ls\n",id,*service?service:L"(none)");
        selected=device;++matches;
    }
    int result=0;DWORD windows_error=0;
    if(!matches) {
        wprintf(L"No matching USB device is connected (0547:1002 or 6022:0001).\n");result=3;
    } else if((install || check_device) && matches!=1) {
        fwprintf(stderr,L"Multiple Xeltek devices found. Connect only the 9000U. No changes were made.\n");result=4;
    } else if(check_device) {
        SP_DRVINFO_DATA_W driver={.cbSize=sizeof(driver)};
        if(!select_inbox_winusb(set,&selected,&driver)) {
            fwprintf(stderr,L"Device-specific driver selection failed. Windows error %lu.\n",GetLastError());result=1;
        } else wprintf(L"The driver node belongs to this device. No installation was performed.\n");
        SetupDiDestroyDriverInfoList(set,&selected,SPDIT_CLASSDRIVER);
    } else if(install && !install_winusb(set,&selected)) {
        windows_error=GetLastError();
        fwprintf(stderr,L"Setup failed. Windows error %lu.\n",windows_error);result=1;
    }
    SetupDiDestroyDeviceInfoList(set);
    if(install_console) wprintf(L"RESULT exit_code=%d windows_error=%lu\n",result,windows_error);
    if(install && !install_console) {
        const wchar_t *message=result==0?
            L"WinUSB is configured. Start Start-SUPERPRO.exe. If the device is not detected, disconnect and reconnect its USB cable.\n\nIf the device returns with a different USB ID during firmware startup, run Setup-USB.exe again.":
            result==3?L"No matching device is connected. Connect and power on the 9000U, then try again. No changes were made.":
            result==4?L"Multiple Xeltek devices are connected. Connect only the 9000U and try again. No changes were made.":
            L"Setup failed. See SP9000U\\bin\\superpro9000u.log for the error details and README.md for troubleshooting.";
        MessageBoxW(NULL,message,L"SUPERPRO 9000U - WinUSB",result?MB_ICONWARNING:MB_ICONINFORMATION);
    }
    return result;
}

int wmain(int argc,wchar_t **argv) {
    if(!xlog_init(NULL,FALSE,"usb-setup") ||
       !xlog_write("INFO",0,"Session started; command=%ls",argc>1?argv[1]:L"--list")) {
        fputws(L"Cannot write SP9000U\\bin\\superpro9000u.log. Make sure the software folder is writable.\n",stderr);
        return 1;
    }
    int result=run_setup(argc,argv);
    xlog_write(result==0?"INFO":result==77?"WARNING":"ERROR",0,"Session finished; exit_code=%d",result);
    return result;
}
