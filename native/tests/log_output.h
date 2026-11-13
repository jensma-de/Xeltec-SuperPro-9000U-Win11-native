#ifndef XELTEK_TEST_OUTPUT_H
#define XELTEK_TEST_OUTPUT_H
#include "../src/log.h"
#include <stdio.h>
#include <stdarg.h>

static int test_output(FILE *stream,const char *format,...) {
    DWORD saved=GetLastError();char text[4096];va_list args;
    va_start(args,format);vsnprintf(text,sizeof(text),format,args);va_end(args);
    xlog_write(stream==stderr?"ERROR":"INFO",0,"%s",text);
    int result=fputs(text,stream);SetLastError(saved);return result;
}
static int test_puts(const char *text) {return test_output(stdout,"%s\n",text);}
#define fprintf(stream,...) test_output(stream,__VA_ARGS__)
#define puts(text) test_puts(text)
#endif
