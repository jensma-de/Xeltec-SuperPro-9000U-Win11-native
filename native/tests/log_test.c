#include "../src/log.h"
#include <stdio.h>

int main(int argc,char **argv) {
    if(!xlog_init(NULL,FALSE,"logging-test")) return 1;
    const char *token=argc>1?argv[1]:"standalone";
    for(unsigned i=0;i<100;++i) {
        SetLastError(0x12345678);
        BOOL ok=xlog_write(i%2?"ERROR":"INFO",i%2?ERROR_INVALID_PARAMETER:0,
                          "Concurrent append test token=%s sequence=%u",token,i);
        if(!ok || GetLastError()!=0x12345678) return 1;
    }
    if(!xlog_write_w("INFO",0,L"UTF-8 text test: caf\u00e9")) return 1;
    return 0;
}
