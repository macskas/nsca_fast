//
// Created by macskas on 12/12/20.
//

#include "processManager.h"

#include <cassert>
#include <csignal>

#include <sys/wait.h>
#include <sys/resource.h>
#include <pwd.h>
#include <grp.h>
#include <sys/prctl.h>
#include <unistd.h>

#include "log.h"
#include "network.h"
#include "config.h"
#include "common.h"

#include <sys/file.h>
#include <fcntl.h>

processManager* processManager::instance = nullptr;
processManager* processManager::getInstance()
{
    if (processManager::instance == nullptr) {
        processManager::instance = new processManager;
        assert(processManager::instance);
    }
    return processManager::instance;
}

processManager::processManager() {
    this->process_mode = 0;
    this->myPid = 0;
    this->parentPid = 0;
    this->workers_running = 0;
    this->maxWorker = 4;
    this->shutdown_requested = false;
    this->nw = nullptr;
    this->lockfd = -1;
}

processManager::~processManager() {
    for (size_t i=0; i<this->workers.size(); i++) {
        delete this->workers[i];
        this->workers[i] = nullptr;
    }
    this->workers.clear();

    if (this->nw) {
        delete this->nw;
        this->nw = nullptr;
    }

    if (this->lockfd != -1) {
        if (this->process_mode == PROCESS_MAIN) {
            close(lockfd);
        }
    }
}

void processManager::reset() {
    this->process_mode = 0;
    this->myPid = 0;
    this->parentPid = 0;
    this->workers_running = 0;
    this->shutdown_requested = false;
    this->nw = nullptr;
    this->lockfd = -1;
}

void processManager::setProcessMode(int m) {
    this->process_mode = m;
}

void processManager::setPids() {
    this->myPid = getpid();
    if (this->process_mode == PROCESS_CHILD) {
        this->parentPid = getppid();
    }
}

void processManager::setMaxWorker(int mw) {
    this->maxWorker = mw;
    child_t *chld = nullptr;
    for (int i=0; i<this->maxWorker; i++) {
        chld = new child_t;
        assert(chld);
        this->workers.push_back(chld);
    }
}

void processManager::startChild(child_t *chld, int wid) {
    if (this->process_mode != PROCESS_MAIN)
        return;

    if (chld->process_state == PROCESS_STATE_RUNNING)
        return;

    pid_t pid = fork();
    if (pid == 0) {
        debug_setpid();
        log_output_check();
        this->reset();
        this->setProcessMode(PROCESS_CHILD);
        this->setPids();
        signal(SIGUSR1, processManager::sighandler_proxy);
        signal(SIGINT, SIG_IGN);
        signal(SIGTERM, SIG_IGN);
        ::prctl(PR_SET_PDEATHSIG, SIGKILL);
        this->super_limits();
        this->super_downgrade();
        this->work();
        return;
    } else if (pid > 0) {
        this->setProcessMode(PROCESS_MAIN);
        time(&(chld->started));
        chld->process_state = PROCESS_STATE_RUNNING;
        chld->pid = pid;
        debug_sprintf("[%s] child started: %d", __FUNCTION__, pid);
        this->workers_running++;
    } else {
        error_sprintf("[%s] fork failed. (%s)", __PRETTY_FUNCTION__ , strerror(errno));
        return;
    }
}

void processManager::work()
{
    if (this->process_mode != PROCESS_CHILD && this->maxWorker != 1)
        return;

    auto *tm = threadManager::getInstance();
    debug_sprintf("[%s] started", __PRETTY_FUNCTION__ );

    tm->start();
    this->nw = new network();
    assert(nw);
    this->nw->run();
    delete this->nw;
    this->nw = nullptr;

    tm->join();
    delete tm;
    tm = nullptr;
}

void processManager::runWorkers() {
    if (this->process_mode != PROCESS_MAIN)
        return;

    child_t *chld = nullptr;

    if (this->shutdown_requested)
        return;

    for (int i = 0; i < this->maxWorker; i++) {
        chld = this->workers[i];
        this->startChild(chld, i);
    }
}

int processManager::getProcessMode() {
    return this->process_mode;
}

void processManager::sighandler_proxy(int s)
{
    auto *pm = processManager::getInstance();
    if (s == SIGCHLD) {
        pm->sigchld_handler(s);
    } else if (s == SIGINT || s == SIGTERM) {
        pm->sigint_handler(s);
    } else if (s == SIGUSR1) {
        pm->sigusr1_handler(s);
    }
}

void processManager::sigint_handler(int s) {
    if (this->process_mode != PROCESS_MAIN)
        return;

    debug_sprintf("[%s] signal=%d, pid=%d, mode=%d", __PRETTY_FUNCTION__ , s, this->myPid, this->process_mode);
    if (this->shutdown_requested) {
        exit(2);
    }

    this->killChilds(SIGUSR1);
    this->shutdown();
}

void processManager::sigchld_handler(int s)
{
    pid_t pid = 0;
    int status = 0;
    int i;
    while((pid = waitpid(-1, &status, WNOHANG)) > 0)
    {
        if (!pid) {
            continue;
        }
        debug_sprintf("[%s] pid=%d, ppid=%d, reap_pid=%d, pmode=%d", __FUNCTION__, this->myPid, this->parentPid, pid, this->process_mode);
        for (i = 0; i<this->maxWorker; i++) {
            if (this->workers[i]->pid == pid) {
                this->workers[i]->reset();
                this->workers_running--;
                break;
            }
        }
    }
}

void processManager::sigusr1_handler(int s) {
    if (this->process_mode != PROCESS_CHILD)
        return;

    this->shutdown();
}


void processManager::super_limits()
{
    struct rlimit		newlimit;
    int					maxconn = 65535;
    int					rc = 0;

    newlimit.rlim_cur = maxconn;
    newlimit.rlim_max = maxconn;
    rc = setrlimit(RLIMIT_NOFILE, &newlimit);
    if (rc != 0)
    {
        if (getrlimit(RLIMIT_NOFILE, &newlimit) == 0) {
            warning_sprintf("[%s] setrlimit(RLIMIT_NOFILE, &newlimit) failed: %s (soft=%d, hard=%d)", __PRETTY_FUNCTION__ , strerror(errno), newlimit.rlim_cur, newlimit.rlim_max);
        } else {
            warning_sprintf("[%s] setrlimit(RLIMIT_NOFILE, &newlimit) failed: %s", __PRETTY_FUNCTION__ , strerror(errno));
        }
        return;
    }
    debug_sprintf("[super_limits] Changed RLIMIT_NOFILE=%d", maxconn);
}

void processManager::super_downgrade()
{
    struct passwd		pwd;
    struct passwd		*pwd_result = nullptr;

    struct group        group;
    struct group        *group_result = nullptr;

    char				*buf = nullptr;
    char				pbuf[DBUFFER_4K_SIZE];
    int					bufsize;
    int					s;
    std::string         username = config::getInstance()->Get("nsca_user", "");
    std::string         groupname = config::getInstance()->Get("nsca_group", "");
    const char			*uname = username.c_str();
    const char          *gname = groupname.c_str();
    DMEMZERO(pbuf,DBUFFER_4K_SIZE);

    if (!groupname.empty()) {
        bufsize = sysconf(_SC_GETGR_R_SIZE_MAX);
        if (bufsize == -1)
            bufsize = DBUFFER_32K_SIZE;

        buf = new char[bufsize];
        s = getgrnam_r(gname, &group, buf, bufsize, &group_result);
        if (group_result == nullptr) {
            if (s == 0) {
                error_sprintf("[%s] Group '%s' not found.", __PRETTY_FUNCTION__ , gname);
            } else {
                errno = s;
                error_sprintf("[%s] getgrnam_r error for group: %s (%s).", __PRETTY_FUNCTION__ , gname, strerror(errno));
            }
            exit(1);
        }
        snprintf(pbuf, DBUFFER_4K_SIZE, "%d", group.gr_gid);
        config::getInstance()->Set("gid", pbuf);
        delete[] buf;
        buf = nullptr;

        s = setgid(group.gr_gid);
        if (s != 0) {
            error_sprintf("[%s] setgid failed to gid: %d (%s).", __PRETTY_FUNCTION__ , group.gr_gid, strerror(errno));
            exit(1);
        }
        debug_sprintf("[super_downgrade] Changed groupname=%s, gid=%d", gname, group.gr_gid);
    }

    if (!username.empty()) {
        bufsize = sysconf(_SC_GETPW_R_SIZE_MAX);
        if (bufsize == -1)
            bufsize = DBUFFER_32K_SIZE;
        buf = new char[bufsize];
        s = getpwnam_r(uname, &pwd, buf, bufsize, &pwd_result);
        if (pwd_result == nullptr) {
            if (s == 0) {
                error_sprintf("[%s] User '%s' not found.", __PRETTY_FUNCTION__ , uname);
            } else {
                errno = s;
                error_sprintf("[%s] getpwnam_r error for user: %s (%s).", __PRETTY_FUNCTION__ , uname, strerror(errno));
            }
            exit(1);
        }
        snprintf(pbuf, DBUFFER_4K_SIZE, "%d", pwd.pw_uid);
        config::getInstance()->Set("uid", pbuf);
        delete[] buf;
        buf = nullptr;
        s = setuid(pwd.pw_uid);
        if (s != 0) {
            error_sprintf("[%s] setuid failed to uid: %d (%s).", __PRETTY_FUNCTION__ , pwd.pw_uid, strerror(errno));
            exit(1);
        }
        debug_sprintf("[super_downgrade] Changed username=%s, uid=%d", uname, pwd.pw_uid);
    }

}

void processManager::shutdown() {
    this->shutdown_requested = true;
    if (this->nw) {
        this->nw->stop();
    }
}

void processManager::killChilds(int s) {
    for (int i=0; i<this->maxWorker; i++) {
        kill(this->workers[i]->pid, s);
    }
}

void processManager::loop() {
    debug_sprintf("[processManager::loop] %s", "started");
    int wait_for_shutdown = 0;
    int max_wait_for_shutdown = 15;
    if (this->maxWorker == 1) {
        super_limits();
        super_downgrade();
        this->work();
    } else {
        while (1) {
            debug_sprintf("[wr=%d]", this->workers_running);
            if (this->shutdown_requested) {
                wait_for_shutdown++;
                if (this->workers_running == 0) {
                    break;
                } else {
                    if (wait_for_shutdown >= max_wait_for_shutdown) {
                        this->killChilds(SIGKILL);
                        break;
                    }
                }
            } else {
                this->runWorkers();
            }
            sleep(1);
        }
    }
    debug_sprintf("[processManager::loop] %s", "finished");
}

int processManager::lock(const char *fn) {
    if (this->process_mode != PROCESS_MAIN)
        return 0;

    this->lockfd = open(fn, O_RDONLY|O_CLOEXEC, S_IRUSR);
    if (this->lockfd < 0) {
        error_sprintf("[%s] unable to open file: %s (%s)", __PRETTY_FUNCTION__, fn, strerror(errno) );
        return -1;
    }
    if (flock(this->lockfd, LOCK_EX|LOCK_NB) != 0) {
        error_sprintf("[%s] unable to lock file: %s (%s)", __PRETTY_FUNCTION__, fn, strerror(errno) );
        return -2;
    }
    return 0;
}

void processManager::devnull_output() {
    FILE *reopen_rc = nullptr;
    reopen_rc = freopen("/dev/null", "r", stdin);
    assert(reopen_rc);
    reopen_rc = freopen("/dev/null", "w+", stdout);
    assert(reopen_rc);
    reopen_rc = freopen("/dev/null", "w+", stderr);
    assert(reopen_rc);
}