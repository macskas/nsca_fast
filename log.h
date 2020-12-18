//
// Created by macskas on 12/9/20.
//

#ifndef NSCA_LOG_H
#define NSCA_LOG_H
#include <string>

void log_output_check();
void debug_sprintf(const char*, ...);
void warning_sprintf(const char*, ...);
void error_sprintf(const char*, ...);
void info_sprintf(const char*, ...);
void debug_enable();
void debug_setpid();
bool is_debug();


#endif //NSCA_LOG_H
