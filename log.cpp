//
// Created by macskas on 12/9/20.
//

#include "log.h"


#include <string>
#include <cstring>
#include <ctime>
#include <cstdio>
#include <cstdarg>
#include <unistd.h>
#include <fcntl.h>

#include "common.h"


static int debug_enabled = -1;
static int log_output_enabled_stdout = -1;
static int log_output_enabled_stderr = -1;

static int debug_pid = getpid();

bool is_debug()
{
    if (debug_enabled == 1)
        return true;
    return false;
}

void log_output_check()
{
    if (fcntl(fileno(stderr), F_GETFD) == 0) {
        log_output_enabled_stderr = 1;
    } else {
        log_output_enabled_stderr = 0;
    }
    if (fcntl(fileno(stdout), F_GETFD) == 0) {
        log_output_enabled_stdout = 1;
    } else {
        log_output_enabled_stdout = 0;
    }
}
void debug_enable()
{
    debug_enabled = 1;
}

void debug_setpid()
{
    debug_pid = getpid();
}

static
void global_sprintf(const char *type, const char *fmt, va_list argptr) {
    if (log_output_enabled_stderr != 1 && log_output_enabled_stdout != 1)
        return;

    char            buffer[512];
    char            timebuffer[256];
    time_t			now = time(nullptr);
    struct tm		*timeinfo = nullptr;

    DMEMZERO(buffer,512);
    DMEMZERO(timebuffer, 256);

    timeinfo = localtime( &now );
    vsnprintf(buffer, 512, fmt, argptr);
    strftime(timebuffer, 256, "%Y-%m-%d %H:%M:%S",timeinfo);
    if (type[0] == 'D' || type[0] == 'I') {
        if (log_output_enabled_stdout) {
            fprintf(stdout, "%s %-5s [%-5d] > %s\n", timebuffer, type, debug_pid, buffer);
        }
    } else {
        if (log_output_enabled_stderr) {
            fprintf(stderr, "%s %-5s [%-5d] > %s\n", timebuffer, type, debug_pid, buffer);
        }
    }
}

void debug_sprintf(const char *fmt, ...) {
    if (debug_enabled != 1)
        return;

    va_list         argptr;
    va_start(argptr, fmt);
    global_sprintf("DEBUG", fmt, argptr);
    va_end(argptr);
}

void info_sprintf(const char *fmt, ...) {
    va_list         argptr;
    va_start(argptr, fmt);
    global_sprintf("INFO", fmt, argptr);
    va_end(argptr);
}

void warning_sprintf(const char *fmt, ...) {
    va_list         argptr;
    va_start(argptr, fmt);
    global_sprintf("WARN", fmt, argptr);
    va_end(argptr);
}

void error_sprintf(const char *fmt, ...) {
    va_list         argptr;
    va_start(argptr, fmt);
    global_sprintf("ERROR", fmt, argptr);
    va_end(argptr);
}
