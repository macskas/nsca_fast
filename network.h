//
// Created by macskas on 12/9/20.
//

#ifndef NSCA_NETWORK_H
#define NSCA_NETWORK_H

#define RPS_RESOLUTION 5

#include <string>

#include "nsca_utils.h"
#include "stat_writer.h"
#include "network_client.h"
#include "fifo_client.h"
#include "result_path_client.h"

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
}


class network {
private:
    struct event_base       *ev_base;
    struct evconnlistener	*listener;
    struct event			timerevent{};

    volatile bool           shutdown_requested;

public:
    time_t                  now = 0;
    std::string             cnow;
    uint32_t                connections = 0;
    uint64_t                counter = 0;
    uint64_t                counter_prev = 0;
    class fifo_client       *fifoClient = nullptr;
    class result_path_client *resultPathClient = nullptr;
    class stat_writer       StatWriter;

    std::string            command_file;
    std::string            check_result_path;
    std::string            password;
    int                    decryption_method = 0;
    int                    max_packet_age = 0;
    int                    max_packet_age_enabled = 0;
    int                    max_checks_per_connection = 0;
    int                    nsca_threads_per_worker = 0;
    int                    decryption_mode = 0;
    bool                   use_network_CI = false;

    char*                  shared_transmission_iv = nullptr;
    uint64_t               counter_rps[RPS_RESOLUTION];
    uint64_t               message_success = 0;
    uint64_t               message_failed = 0;

    struct crypt_instance  *CI = nullptr;
public:
    network();
    ~network();

    static
    void listener_proxy(struct evconnlistener *, evutil_socket_t,struct sockaddr *, ev_socklen_t, void *);

    static
    void timer_proxy(evutil_socket_t , short , void *);

    void start_timer();

    int stop();
    void run();

    event_base *getBufferEventBase();

    void sigint();

    static
    void sigint_proxy(evutil_socket_t fd, short what, void *arg);

    // not threadsafe, and we dont care. only stats
    void report_success_failed(uint16_t success, uint16_t failed);
    void save_statistics();
};


#endif //NSCA_NETWORK_H
