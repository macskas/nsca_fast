//
// Created by macskas on 12/10/20.
//

#ifndef NSCA_RESULT_PATH_WRITER_H
#define NSCA_RESULT_PATH_WRITER_H

#include <ctime>
#include <string>

int write_result_path(const std::string& check_result_path, time_t now, const std::string& cnow, const std::string& hostname, const std::string& service, int return_code, const std::string& plugin_output);


#endif //NSCA_RESULT_PATH_WRITER_H
