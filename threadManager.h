//
// Created by macskas on 12/14/20.
//

#ifndef NSCA_THREADMANAGER_H
#define NSCA_THREADMANAGER_H


#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include "log.h"
#include "network_client.h"
#include "nsca_utils.h"
#include "crypt_thread_t.h"

class threadManager {
private:
    static threadManager		*instance;
    int                         max_crypt_threads = 4;
    crypt_thread_t              **crypt_threads = nullptr;
    int                         lazy_job_counter = 0;
    char                        shared_transmitted_iv[TRANSMITTED_IV_SIZE];

public:
    threadManager();
    ~threadManager();
    static threadManager		*getInstance();

    void start();
    void join();

    void add_job(int, network_client *);
    char *get_shared_transmitted_iv();
};



#endif //NSCA_THREADMANAGER_H
