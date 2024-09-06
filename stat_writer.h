//
// Created by macskas on 1/11/21.
//
#ifndef NSCA_STAT_WRITER_H
#define NSCA_STAT_WRITER_H

#include <string>
#include <cstdint>

class stat_writer {
private:
    std::string     path_stat;
    int             internal_process_id = 0;
    int             mypid = 0;
    time_t          started = 0;

public:
    stat_writer();
    ~stat_writer();

public:
    void save_stats(time_t now, uint64_t cps, uint64_t connections, uint64_t counter, uint64_t report_success, uint64_t report_failed);
    void cleanup();
};


#endif //NSCA_STAT_WRITER_H
