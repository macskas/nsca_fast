//
// Created by macskas on 12/10/20.
//

#ifndef NSCA_FIFO_CLIENT_H
#define NSCA_FIFO_CLIENT_H

#include <event2/util.h>
#include <string>
#include <cstdio>

#include "network.h"

class fifo_client {
private:
    class network               *parent = nullptr;
    int                         fd = -1;
    FILE                        *fp = nullptr;

    std::string                 fifo_path;
    time_t                      block_fifo_until = 0;

public:
    fifo_client(std::string);
    ~fifo_client();

    void take_variables(network*);
    int init_events();
    int command(const std::string&, const std::string&, int, const std::string&);

};


#endif //NSCA_FIFO_CLIENT_H
