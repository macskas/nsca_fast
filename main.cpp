#include <iostream>
#include <string>
#include <cinttypes>
#include <cassert>

extern "C"
{
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/event.h>
#include <event2/event_struct.h>
#include <event2/util.h>
#include <event2/listener.h>
#include <event2/thread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/prctl.h>
}

#include <csignal>
#include <ctime>
#include "common.h"
#include "network.h"
#include "log.h"
#include "nsca_common.h"
#include "nsca_utils.h"
#include "config.h"

#include <getopt.h>
#include <pwd.h>
#include <grp.h>
#include <sys/resource.h>
#include <unistd.h>
#include "processManager.h"

#include <fcntl.h>
#include <sys/file.h>

void writepid()
{
    int				mypid = getpid();
    char			buf[FILENAME_MAX];
    std::string     pidfile = config::getInstance()->Get("pid_file", "/var/run/nagios/nsca.pid");
    const char		*pf = pidfile.c_str();
    assert(pf);

    FILE			*myf;
    DMEMZERO(buf, FILENAME_MAX);

    if (mypid <= 0) {
        error_sprintf("[%s] pid should always be greater than 0", __PRETTY_FUNCTION__ );
        exit(1);
    }
    myf = fopen(pf, "w");
    if (myf == nullptr) {
        error_sprintf("[%s] unable to write pid file to: %s (%s)", __PRETTY_FUNCTION__, pf, strerror(errno) );
        exit(1);
    }
    snprintf(buf, FILENAME_MAX, "%d\n", mypid);
    fwrite(buf, 1, strlen(buf), myf);
    fclose(myf);
    debug_sprintf("[%s] pidfile=%s, pid=%d", __PRETTY_FUNCTION__ , pf, mypid);
}

void do_help(const std::string &programName)
{
    std::cout << "Usage: " << programName << " [OPTIONS]" << std::endl;
    std::cout << "nsca." << std::endl;
    std::cout << std::endl;
    std::cout << "Mandatory arguments to long options are mandatory for short options too." << std::endl;
    std::cout << "  -h                    this screen" << std::endl;
    std::cout << "  -c [FILE]             configfile" << std::endl;
    std::cout << "  -d                    verbose output" << std::endl;
    std::cout << "  -f                    foreground" << std::endl;
#ifdef WORKERS_ENABLED
    std::cout << "  -n [MAX_WORKERS]      max workers - between 0 and 100" << std::endl;
#endif
    std::cout << "  -t [THREADS]          max_threads_per_worker - between 0 and 1000" << std::endl;
    std::cout << std::endl;
    std::cout << "{ \"author\": \"" AUTHOR "\", \"version\": \"" VERSION_DATE_LONG"\", gcc: \"" << GCC_VERSION << "\" };" << std::endl;
}

void main_cleanup()
{
    auto *cfg = config::getInstance();
    auto *pm = processManager::getInstance();
    delete pm;
    delete cfg;

    pm = nullptr;
    cfg = nullptr;
}

void signal_atexit(void)
{
    main_cleanup();
}

int mainloop(char **argv, bool daemonize)
{
    auto                    *pm = processManager::getInstance();
    class config            *cfg = config::getInstance();

    signal(SIGCHLD, processManager::sighandler_proxy);
    signal(SIGINT, processManager::sighandler_proxy);
    signal(SIGTERM, processManager::sighandler_proxy);
    atexit(signal_atexit);

    int workers = (int)cfg->GetInt("nsca_workers", 4);
    pm->setProcessMode(PROCESS_MAIN);
    pm->setPids();
    if (pm->lock(argv[0])) {
        return 1;
    }
    pm->setMaxWorker(workers);
    if (workers == 1) {
        writepid();
        pm->loop();
    } else {
        pm->runWorkers();
        if (pm->getProcessMode() == PROCESS_MAIN) {
            writepid();
            pm->loop();
        }
    }
    return 0;
}



int main(int argc, char **argv) {

    char					*pname = argv[0];
    int                     c = 0;
    bool                    daemonize = true;
    class config            *cfg = config::getInstance();
    int                     rc = 0;
    cfg->setConfigPath("/etc/nagios/nsca.cfg");
    int                     max_workers = 0;
    int                     nsca_threads_per_worker = -1;
    auto                   *pm = processManager::getInstance();
    // evthread_use_pthreads();
    log_output_check();

    while ((c = getopt(argc, argv, "hc:dfn:t:")) != -1)
    {
        switch (c)
        {
            case 'h':
                do_help(pname);
                return 1;
                break;
            case 'f':
                daemonize = false;
                break;
            case 'd':
                cfg->Set("debug", "1");
                debug_enable();
                break;
            case 'c':
                if (optarg != nullptr) {
                    cfg->setConfigPath(optarg);
                }
                break;
            case 'n':
                if (optarg != nullptr) {
                    max_workers = (int)strtol(optarg, nullptr, 10);
                    if (max_workers < 0 || max_workers > 100) {
                        error_sprintf("[%s] %s", __PRETTY_FUNCTION__ , "Invalid max worker option. Should be between 0 and 100");
                        return 1;
                    }
                }
                break;
            case 't':
                if (optarg != nullptr) {
                    nsca_threads_per_worker = (int)strtol(optarg, nullptr, 10);
                    if (nsca_threads_per_worker < 0 || nsca_threads_per_worker > 1000) {
                        error_sprintf("[%s] %s", __PRETTY_FUNCTION__ , "Invalid threads option. Should be between 0 and 1000");
                        return 1;
                    }
                }
                break;
        }
    }
    if (daemonize) {
        pm->devnull_output();
    }

    generate_crc32_table();

    //cfg->setConfigPath("/home/macskas/CLionProjects/nsca/nsca.cfg");
    //daemonize = false;
    cfg->read_config();

    if (max_workers) {
        char tmpbuf[10];
        memset(tmpbuf, 0, 10);
        snprintf(tmpbuf, 10, "%d", max_workers);
        cfg->Set("nsca_workers", tmpbuf);
    }
    if (nsca_threads_per_worker != -1) {
        char tmpbuf[10];
        memset(tmpbuf, 0, 10);
        snprintf(tmpbuf, 10, "%d", nsca_threads_per_worker);
        cfg->Set("nsca_threads_per_worker", tmpbuf);
    }

    rc = cfg->check_config();

    if (rc != 0) {
        return 1;
    }

    if (daemonize) {
        pid_t mypid = fork();
        if (mypid > 0) {
            return 0;
        } else if (mypid == 0) {
            return mainloop(argv, daemonize);
        } else {
            error_sprintf("[%s] fork error: %s", __PRETTY_FUNCTION__, strerror(errno));
            return 2;
        }
    } else {
        return mainloop(argv, daemonize);
    }

    return 0;
}
