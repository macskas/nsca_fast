//
// Created by macskas on 12/9/20.
//

#include "network.h"
#include "network_client.h"
#include "common.h"

#include <csignal>
#include <cassert>

// #include <chrono>

extern "C"
{
#include <event2/bufferevent.h>
#include <event2/event.h>
#include <event2/util.h>
#include <event2/listener.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/un.h>
}
#include "config.h"
#include "log.h"

#include "nsca_utils.h"
#include "threadManager.h"
#include "stat_writer.h"
#include "result_path_client.h"

network::network() : ev_base(nullptr), listener(nullptr), shutdown_requested(false), now(0) {
    this->connections = 0;
    this->counter = 0;
    this->fifoClients = nullptr;
    this->resultPathClient = nullptr;
    time(&(this->now));
    this->cnow.assign(ctime((&this->now)));

    auto *cfg = config::getInstance();

    this->decryption_method = (int)(cfg->GetInt("decryption_method", 0));
    this->password = cfg->Get("password", "");
    this->command_file = cfg->Get("command_file", "");
    this->check_result_path = cfg->Get("check_result_path", "");
    this->max_packet_age = (int)(cfg->GetInt("max_packet_age", 0));
    this->max_packet_age_enabled = (int)(cfg->GetInt("max_packet_age_enabled", 0));
    this->max_checks_per_connection = (int)(cfg->GetInt("max_checks_per_connection", 1));
    this->nsca_threads_per_worker = (int)(cfg->GetInt("nsca_threads_per_worker", 0));
    this->decryption_mode = (int)(cfg->GetInt("decryption_mode", 0));

    if (!this->command_file.empty()) {
        auto iters = this->nsca_threads_per_worker > 0 ? this->nsca_threads_per_worker : 1;
        this->fifoClients = new fifo_client*[this->nsca_threads_per_worker];
        for (auto i=0; i<iters; i++) {
            this->fifoClients[i] = new fifo_client(this->command_file);
        }
    }
    if (!this->check_result_path.empty()) {
        this->resultPathClient = new result_path_client(this);
    }

    for (int i=0; i<RPS_RESOLUTION; i++) {
        counter_rps[i] = 0;
    }


    /*
    std::chrono::steady_clock::time_point t1 = std::chrono::steady_clock::now();
    const int e=30000;
    for (int i=0; i<e;i++) {
        encrypt_init(this->password.c_str(), this->decryption_method, nullptr, &(this->CI));
        encrypt_cleanup(this->decryption_method, this->CI);
    }
    std::chrono::steady_clock::time_point t2 = std::chrono::steady_clock::now();
    std::chrono::duration<double> time_span = std::chrono::duration_cast<std::chrono::duration<double>>(t2 - t1);
    double totalTime = time_span.count();
    debug_sprintf("[***] t=%.2f | %.2f init/sec",totalTime, 1.0/totalTime*(double)e);
     */


    //encrypt_init(this->password.c_str(), this->decryption_method, nullptr, &(this->CI));
    this->shared_transmission_iv = threadManager::getInstance()->get_shared_transmitted_iv();
    if (nsca_threads_per_worker < 1) {
        debug_sprintf("[%s] %s", __PRETTY_FUNCTION__, "threads_disabled + use shared crypt_instance");
        if ((this->decryption_mode & DECRYPTION_MODE_SHARED_CRYPT_INSTANCE) != 0) {
            if (encrypt_init(this->password.c_str(), this->decryption_method, this->shared_transmission_iv, &(this->CI)) == OK) {
                this->use_network_CI = true;
            } else {
                throw std::runtime_error("Unable to generate crypt_instance in parent::parent().");
            }
        }
    }
    signal(SIGPIPE, SIG_IGN);
}

network::~network() {
    if (this->fifoClients != nullptr) {
        auto iters = this->nsca_threads_per_worker > 0 ? this->nsca_threads_per_worker : 1;
        for (auto i=0; i<iters; i++) {
            delete this->fifoClients[i];
        }
        delete [] this->fifoClients;
        this->fifoClients = nullptr;
    }

    if (this->resultPathClient != nullptr) {
        delete this->resultPathClient;
        this->resultPathClient = nullptr;
    }

    if (this->CI) {
        encrypt_cleanup(this->decryption_method, this->CI);
        this->CI = nullptr;
    }
}
void network::listener_proxy(struct evconnlistener *listener, evutil_socket_t fd,struct sockaddr *sa, ev_socklen_t socklen, void *user_data) {
    auto *client = new network_client;
    assert(client);
    client->take_variables(reinterpret_cast<network *>(user_data), fd, sa, socklen);
}

void network::timer_proxy(evutil_socket_t fd, short event, void *user_data) {
    auto		        *n = reinterpret_cast<network *>(user_data);
    struct timeval		tv{};
    int					rc = 0;
    evutil_timerclear(&tv);
    tv.tv_sec = 1;
    tv.tv_usec = 0;

    time(&(n->now));

    n->cnow.clear();
    n->cnow.assign(ctime(&(n->now)));

    int bucket_id = n->now % RPS_RESOLUTION;
    int diff = n->counter - n->counter_prev;
    n->counter_prev = n->counter;
    n->counter_rps[bucket_id] = diff;

    if (bucket_id == 0) {
        n->save_statistics();
    }

    if (!n->shutdown_requested && n->resultPathClient) {
        n->resultPathClient->check();
    }

    if (n->shutdown_requested) {
        rc = n->stop();
        if (rc != 0) {
            event_add(&n->timerevent, &tv);
        }
    } else {
        event_add(&n->timerevent, &tv);
    }
}

void network::start_timer()
{
    struct timeval	tv{};
    if (this->ev_base == nullptr)
        return;
    evutil_timerclear(&tv);
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    event_assign(&this->timerevent, this->ev_base, -1, 0, &network::timer_proxy, (void *)this);
    event_add(&this->timerevent, &tv);
}

int network::stop() {
    struct timeval delay{};

    debug_sprintf("[%s]", __PRETTY_FUNCTION__);
    if (this->ev_base == nullptr)
        return 1;

    if (this->listener != nullptr) {
        evconnlistener_disable(this->listener);
    }
    if (this->connections != 0) {
        this->shutdown_requested = true;
        return 1;
    }

    delay.tv_sec = 1;
    delay.tv_usec = 0;
    event_base_loopexit(this->ev_base, &delay);
    return 0;
}

void network::run()
{
    uint16_t                port = 5667;
    class config            *cfg = config::getInstance();
    struct sockaddr_storage ss{};
    ev_socklen_t            socklen = sizeof (ss);
    std::string             server_address;
    char                    port_buffer[6];
    DMEMZERO(&ss, sizeof (ss));
    DMEMZERO(port_buffer, 6);

    debug_sprintf("[%s]", __PRETTY_FUNCTION__);

    this->ev_base = event_base_new();
    assert(this->ev_base);


    port = cfg->GetInt("server_port", 5667);
    server_address = cfg->Get("server_address", "0.0.0.0");
    if (strstr(server_address.c_str(), ":")) {
        ((struct sockaddr_in6*)&ss)->sin6_family = PF_INET6;
        inet_pton(AF_INET6,server_address.c_str(),&((struct sockaddr_in6*)&ss)->sin6_addr);
        ((struct sockaddr_in6*)&ss)->sin6_port = htons(port);
    } else {
        ((struct sockaddr_in*)&ss)->sin_family = PF_INET;
        inet_pton(AF_INET,server_address.c_str(),&((struct sockaddr_in*)&ss)->sin_addr);
        ((struct sockaddr_in*)&ss)->sin_port = htons(port);
    }

    this->listener = evconnlistener_new_bind(this->ev_base,
                                             reinterpret_cast<evconnlistener_cb>(network::listener_proxy),
                                             (void *)this,
#ifdef WORKERS_ENABLED
            LEV_OPT_REUSEABLE | LEV_OPT_CLOSE_ON_FREE | LEV_OPT_REUSEABLE_PORT,
#else
                                             LEV_OPT_REUSEABLE | LEV_OPT_CLOSE_ON_FREE,
#endif
                                             -1,
                                             (struct sockaddr*)&ss,
                                             socklen);
    if (!this->listener) {
        error_sprintf("[%s] evconnlistener_new_bind(): %s", __PRETTY_FUNCTION__, strerror(errno));
    }
    assert(this->listener);

    if (this->fifoClients) {
        auto iters = this->nsca_threads_per_worker > 0 ? this->nsca_threads_per_worker : 1;
        for (auto i=0; i<iters; i++) {
            this->fifoClients[i]->take_variables(this);
        }
    }

    this->start_timer();

    struct event* sigint_event;
    sigint_event = evsignal_new(ev_base, SIGINT, network::sigint_proxy, this);
    assert(sigint_event);
    event_add(sigint_event, nullptr);

    struct event* sigterm_event;
    sigterm_event = evsignal_new(ev_base, SIGTERM, network::sigint_proxy, this);
    assert(sigterm_event);
    event_add(sigterm_event, nullptr);

    if (this->resultPathClient) {
        this->resultPathClient->init();
    }

    event_base_loop(this->ev_base, 0);
    evconnlistener_free(this->listener);
    this->listener = nullptr;
    event_free(sigint_event);
    sigint_event = nullptr;
    event_free(sigterm_event);
    sigterm_event = nullptr;

    if (this->resultPathClient) {
        this->resultPathClient->uninit();
    }

    event_base_free(this->ev_base);
    this->ev_base = nullptr;
    debug_sprintf("[%s] finished.", __PRETTY_FUNCTION__);
}

event_base* network::getBufferEventBase()
{
    return this->ev_base;
}

void network::sigint_proxy(int fd, short what, void *arg) {
    ((network*)arg)->sigint();
}

void network::sigint() {
    this->shutdown_requested = true;
}

void network::report_success_failed(uint16_t success, uint16_t failed) {
    if (success)
        this->message_success += success;
    if (failed)
        this->message_failed += failed;
}

void network::save_statistics() {
    uint64_t sum = 0;
    for (int i=0; i<RPS_RESOLUTION; i++) {
        sum += this->counter_rps[i];
    }
    uint64_t rps = sum/RPS_RESOLUTION;

    this->StatWriter.save_stats(this->now, rps, this->connections, this->counter, this->message_success, this->message_failed);
}