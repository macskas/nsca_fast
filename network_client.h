//
// Created by macskas on 12/9/20.
//

#ifndef NSCA_NETWORK_CLIENT_H
#define NSCA_NETWORK_CLIENT_H

#define CONNECTION_STATE_INIT_SENT 1

#include "network.h"
#include <event2/util.h>
#include <string>
#include <deque>

typedef struct {
    data_packet receive_packet;
    size_t      packet_length;
} data_packet_pair_t;

class network_client {
private:
    class network               *parent{};
    evutil_socket_t             fd{};
    struct sockaddr_storage     ss{};
    ev_socklen_t                socklen{};

    uint32_t                    client_id;
    std::string                 clientIp;
    uint16_t                    clientPort;

    int                         read_timeout;
    int                         write_timeout;

    int                         connection_state;

    char                        *mcrypt_state = nullptr;
    int                         mcrypt_state_size = 16;

    struct crypt_instance       *CI;
    struct bufferevent          *client_bev = nullptr;

    bool                        has_own_CI = false;
    char*                       transmission_iv = nullptr;

    int                         d_failed = 0;
    int                         d_success = 0;
    size_t                      bytes_received = 0;
    size_t                      bytes_drained = 0;

    std::deque<data_packet_pair_t *> data_packets;
public:
    network_client();
    ~network_client();

    void take_variables(network*, evutil_socket_t, struct sockaddr*, ev_socklen_t);
    int init_events();
    void proper_destroy();
    void connection_closed();

    void debug_message(const std::string&);

    void writecb(bufferevent *bev);
    void readcb(bufferevent *bev);
    void eventcb(bufferevent *bev, short events);

    static void readcb_proxy(bufferevent *, void *);
    static void writecb_proxy(bufferevent *, void *);
    static void eventcb_proxy(struct bufferevent *, short , void *);

    void process_queue(int thread_id);
    void process_queue_mcrypt(int thread_id);
    void process_queue_nomcrypt(int thread_id);

    void set_CI(struct crypt_instance *);
    void send_receive_message(data_packet*, int thread_id);
};

#endif //NSCA_NETWORK_CLIENT_H
