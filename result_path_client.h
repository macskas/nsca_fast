//
// Created by macskas on 4/16/21.
//

#ifndef NSCA_RESULT_PATH_CLIENT_H
#define NSCA_RESULT_PATH_CLIENT_H

extern "C" {
#include <event2/util.h>
#include <sys/inotify.h>
}
#include <string>
#include "network.h"

// 16 should be NAME_MAX, but we dont need that
#define INOTIFY_BUF_LEN (128 * (sizeof(struct inotify_event) + 16 + 1))

class result_path_client {

private:
    int                         inotifyFd            = -1;
    int                         wd                   = -1;
    struct bufferevent          *client_bev          = nullptr;
    uint64_t                    dir_inode            = 0;
    int64_t                     files_in_directory   = 0;
    class network               *parent              = nullptr;
    bool                        dir_watched          = false;

    bool                        write_enabled        = true;
    int                         check_result_path_max_files = 0;

public:
    result_path_client(class network *network);
    ~result_path_client();

    void readcb(bufferevent *bev);
    static void readcb_proxy(bufferevent *, void *);

    void watch_directory();
    void init();
    void uninit();
    void unwatch_directory();

    void count_files();
    void check();

    int write_result(const std::string& check_result_path, time_t now, const std::string& cnow, const std::string& hostname, const std::string& service, int return_code, const std::string& plugin_output) const;
private:
    void event_received(struct inotify_event *);
};


#endif //NSCA_RESULT_PATH_CLIENT_H
