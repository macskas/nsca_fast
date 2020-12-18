//
// Created by macskas on 12/17/20.
//

#include <thread>

#include "log.h"
#include "crypt_thread_t.h"

crypt_thread_t::crypt_thread_t() {
    this->shutdown_requested = false;
    this->myThread = new std::thread(crypt_thread_t::loop_proxy, this);
}

crypt_thread_t::~crypt_thread_t() {
    if (this->myThread) {
        delete this->myThread;
        this->myThread = nullptr;
    }
}

void crypt_thread_t::join() {
        this->shutdown_requested = true;
        this->cv.notify_one();
        debug_sprintf("[%s] join.", __PRETTY_FUNCTION__ );
        if (this->myThread) {
            this->myThread->join();
        }
        debug_sprintf("[%s] joined.", __PRETTY_FUNCTION__ );
}

void crypt_thread_t::add(int method, network_client *nc) {
        std::unique_lock<std::mutex> lck(mtx);
        this->queue_items.push(crypt_queue_item_t(method, nc));
        mtx.unlock();
        this->cv.notify_one();
}

void crypt_thread_t::loop_proxy(class crypt_thread_t *ct) {
    ct->loop();
}

void crypt_thread_t::loop() {
        debug_sprintf("[%s] started.", __PRETTY_FUNCTION__ );
        do {
            std::unique_lock<std::mutex> lck(mtx);
            cv.wait(lck, [this]{
                return (!this->queue_items.empty() || this->shutdown_requested);
            });
            if (this->shutdown_requested && this->queue_items.empty())
                continue;

            crypt_queue_item_t queueItem = std::move(this->queue_items.front());
            this->queue_items.pop();
            mtx.unlock();
            if (queueItem.method == THREADMANAGER_METHOD_DECRYPT_PACKET) {
                if (this->CI) {
                    queueItem.networkClient->set_CI(this->CI);
                    queueItem.networkClient->process_queue();
                }
            }
            mtx.lock();
        } while (!this->shutdown_requested);
        debug_sprintf("[%s] finished.", __PRETTY_FUNCTION__ );
}

void crypt_thread_t::set_CI(struct crypt_instance *myCI) {
    this->CI = myCI;
}
struct crypt_instance * crypt_thread_t::get_CI() {
    return this->CI;
}
