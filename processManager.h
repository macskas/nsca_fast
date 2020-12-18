//
// Created by macskas on 12/12/20.
//

#ifndef NSCA_PROCESSMANAGER_H
#define NSCA_PROCESSMANAGER_H

#include <cstdlib>
#include <ctime>
#include <vector>

#include "network.h"
#include "threadManager.h"

#define PROCESS_MAIN 1
#define PROCESS_CHILD 2

#define PROCESS_STATE_RUNNING 1
#define PROCESS_STATE_NOTRUNNING 2

class child_t {
public:
    pid_t   pid;
    int     process_state;
    time_t  started;

    child_t() {
        this->pid = 0;
        this->process_state = PROCESS_STATE_NOTRUNNING;
        this->started = 0;
    };

    void reset() {
        this->setPid(0);
        this->setProcessState(PROCESS_STATE_NOTRUNNING);
        this->started = 0;
    }

    void setPid(pid_t mpid) {
        this->pid = mpid;
    }

    void setProcessState(int pstate) {
        this->process_state = pstate;
    }

    void updateStarted() {
        time(&this->started);
    }

};

class processManager {
private:
    static processManager		*instance;
    int                         process_mode;
    pid_t                       myPid;
    pid_t                       parentPid;
    int                         maxWorker;
    std::vector<child_t*>       workers;
    int                         workers_running;
    class network*              nw;
    int                         lockfd;

public:
    volatile bool               shutdown_requested;

public:
    processManager();
    ~processManager();
    static processManager		*getInstance();

    void setProcessMode(int);
    void setPids();
    void setMaxWorker(int);

    void startChild(child_t *, int);
    void runWorkers();
    void work();

    int getProcessMode();

    static
    void sighandler_proxy(int);

    void sigchld_handler(int);
    void sigint_handler(int);
    void sigusr1_handler(int);

    void super_limits();
    void super_downgrade();

    void shutdown();
    void killChilds(int);
    void loop();
    void reset();
    void devnull_output();
    int lock(const char*);
};


#endif //NSCA_PROCESSMANAGER_H
