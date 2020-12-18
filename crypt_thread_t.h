//
// Created by macskas on 12/17/20.
//

#ifndef NSCA_CRYPT_THREAD_T_H
#define NSCA_CRYPT_THREAD_T_H

#include <mutex>
#include <condition_variable>
#include <queue>

#include "network_client.h"
#include "nsca_utils.h"

#define THREADMANAGER_METHOD_COMPUTE_HANDSHAKE 1
#define THREADMANAGER_METHOD_DECRYPT_PACKET 2

class crypt_queue_item_t {
public:
    int                     method = 0;
    network_client          *networkClient;

    crypt_queue_item_t(int method, network_client *nc) {
        this->method = method;
        this->networkClient = nc;
    }
};

class crypt_thread_t {
private:
    std::thread                 *myThread = nullptr;
    struct crypt_instance       *CI = nullptr;
    std::mutex                  mtx;
    std::condition_variable     cv;
    volatile bool               shutdown_requested;
    std::queue<crypt_queue_item_t> queue_items;
public:
    crypt_thread_t();
    ~crypt_thread_t();

    void loop();
    void add(int method, network_client *nc);

    static
    void loop_proxy(class crypt_thread_t *ct);

    void join();
    void set_CI(struct crypt_instance *);
    struct crypt_instance *get_CI();
};



#endif //NSCA_CRYPT_THREAD_T_H
