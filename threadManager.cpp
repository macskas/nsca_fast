//
// Created by macskas on 12/14/20.
//

#include <thread>
#include <mutex>
#include <condition_variable>
#include <cassert>
#include <string>

#include "threadManager.h"
#include "common.h"
#include "config.h"

threadManager* threadManager::instance = nullptr;
threadManager* threadManager::getInstance()
{
    if (threadManager::instance == nullptr) {
        threadManager::instance = new threadManager;
        assert(threadManager::instance);
    }
    return threadManager::instance;
}

threadManager::threadManager() {
    this->max_crypt_threads = config::getInstance()->GetInt("nsca_threads_per_worker", 0);
    DMEMZERO(this->shared_transmitted_iv, TRANSMITTED_IV_SIZE);
    generate_transmitted_iv_secure(this->shared_transmitted_iv);
};

threadManager::~threadManager() {
    if (this->crypt_threads != nullptr) {
        for (int i = 0; i < this->max_crypt_threads; i++) {
            delete this->crypt_threads[i];
            this->crypt_threads[i] = nullptr;
        }
        delete[] this->crypt_threads;
        this->crypt_threads = nullptr;
    }
}

char* threadManager::get_shared_transmitted_iv() {
    return this->shared_transmitted_iv;
}

void threadManager::start() {
    int             decryption_method = config::getInstance()->GetInt("decryption_method", 0);
    std::string     password = config::getInstance()->Get("password", "");
    struct
    crypt_instance  *lCI = nullptr;
    crypt_thread_t  *lct = nullptr;

    if (this->crypt_threads == nullptr) {
        this->crypt_threads = new crypt_thread_t *[this->max_crypt_threads];
        assert(this->crypt_threads);
        for (int i=0; i<this->max_crypt_threads; i++) {
            lCI = nullptr;
            encrypt_init(password.c_str(), decryption_method, this->shared_transmitted_iv, &lCI);
            lct = new crypt_thread_t;
            assert(lct);
            lct->set_CI(lCI);
            this->crypt_threads[i] = lct;
        }
    }
}

void threadManager::join() {
    crypt_thread_t          *lct = nullptr;
    struct crypt_instance   *lCI = nullptr;
    int        decryption_method = config::getInstance()->GetInt("decryption_method", 0);

    if (this->crypt_threads == nullptr) {
        return;
    }
    for (int i=0; i<this->max_crypt_threads; i++) {
        lct = this->crypt_threads[i];
        lct->join();
        lCI = lct->get_CI();
        if (lCI) {
            encrypt_cleanup(decryption_method, lCI);
        }
    }
}

void threadManager::add_job(int method, network_client *nc) {
    if (this->crypt_threads == nullptr) {
        return;
    }
    if (this->lazy_job_counter >= this->max_crypt_threads) {
        this->lazy_job_counter = 0;
    }
    this->crypt_threads[this->lazy_job_counter]->add(method, nc);
    this->lazy_job_counter++;
}