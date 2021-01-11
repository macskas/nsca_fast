//
// Created by macskas on 1/11/21.
//

#include "stat_writer.h"
#include "processManager.h"
#include "config.h"
#include "log.h"
#include <memory>
#include <unistd.h>

stat_writer::stat_writer() {
    this->internal_process_id = processManager::getInstance()->getInternalProcessId();
    this->started = processManager::getInstance()->getStarted();

    std::unique_ptr<char[]> path_stat_u(new char[FILENAME_MAX]());
    char                    *lPathStat = path_stat_u.get();
    std::string             dir_stats = config::getInstance()->Get("dir_stats", "/tmp");
    snprintf(lPathStat, FILENAME_MAX, "%s/nsca-stats-%d.txt", dir_stats.c_str(), this->internal_process_id);
    this->path_stat = lPathStat;
    this->mypid = (int)getpid();
}

stat_writer::~stat_writer() {
    this->cleanup();
}

void stat_writer::cleanup() {
    if (this->path_stat.empty())
        return;

    if (access(this->path_stat.c_str(), R_OK) == 0) {
        int rc = unlink(this->path_stat.c_str());
        if (rc != 0) {
            debug_sprintf("[%s] Unable to unlink file: %s (%s)", __PRETTY_FUNCTION__, this->path_stat.c_str(), strerror(rc));
        } else {
            debug_sprintf("[%s] Stat file removed (%s)", __PRETTY_FUNCTION__, this->path_stat.c_str());
        }
    }
}

void stat_writer::save_stats(time_t now, uint64_t cps, uint64_t connections, uint64_t counter, uint64_t report_success, uint64_t report_failed) {
    if (this->path_stat.empty())
        return;
    FILE *fn = fopen(this->path_stat.c_str(), "w");
    if (fn == nullptr) {
        warning_sprintf("[%s] Unable to save stats to: %s", __PRETTY_FUNCTION__, this->path_stat.c_str());
        return;
    }

    fprintf(fn, "[nsca stats]\n");
    fprintf(fn, "pid=%d\n", this->mypid);
    fprintf(fn, "process_started_at=%ld\n", this->started);
    fprintf(fn, "file_created_at=%ld\n", now);
    fprintf(fn, "connections_per_sec=%lu\n", cps);
    fprintf(fn, "connections_current=%lu\n", connections);
    fprintf(fn, "connections_counter=%lu\n", counter);
    fprintf(fn, "report_success=%lu\n", report_success);
    fprintf(fn, "report_failed=%lu\n", report_failed);
    fclose(fn);
}
