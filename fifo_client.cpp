//
// Created by macskas on 12/10/20.
//

#include "fifo_client.h"

#include <utility>

#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstdio>

#include "common.h"
#include "log.h"

#include "nsca_common.h"

fifo_client::fifo_client(std::string command_file) {
    this->fifo_path = std::move(command_file);
    this->fd = -1;
    this->fp = nullptr;
    debug_sprintf("[%s]", __PRETTY_FUNCTION__);
}
fifo_client::~fifo_client() {
    debug_sprintf("[%s]", __PRETTY_FUNCTION__);
}

void fifo_client::take_variables(network *cm) {
    this->parent = cm;
}

int fifo_client::init_events()
{
    struct stat st{};

    if (this->fd >= 0) {
        return -1;
    }
    if (this->fifo_path.length() == 0)
        return 0;

    if (stat(this->fifo_path.c_str(), &st) == 0) {
        if ((st.st_mode & S_IFMT) != S_IFIFO) {
            warning_sprintf("[%s] %s", __PRETTY_FUNCTION__ , "fifo_path is not a fifo file.");
            this->block_fifo_until = this->parent->now+60;
            return -1;
        }
    } else {
        warning_sprintf("[%s] stat error: %s", __PRETTY_FUNCTION__ , strerror(errno));
        this->block_fifo_until = this->parent->now+60;
        return -1;
    }

    this->fd = open(this->fifo_path.c_str(), O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (this->fd < 0) {
        switch (errno) {
            case ENXIO:
                warning_sprintf("[%s] unable to open fifo: %s (no one is listening for incoming connection)", __PRETTY_FUNCTION__ , strerror(errno));
                this->block_fifo_until = this->parent->now+3;
                break;
            case ENOENT:
                warning_sprintf("[%s] unable to open fifo: %s", __PRETTY_FUNCTION__ , strerror(errno));
                this->block_fifo_until = this->parent->now+10;
                break;
            case EACCES:
                warning_sprintf("[%s] unable to open fifo: %s", __PRETTY_FUNCTION__ , strerror(errno));
                this->block_fifo_until = this->parent->now+10;
                break;
            default:
                warning_sprintf("[%s] unable to open fifo: %s", __PRETTY_FUNCTION__ , strerror(errno));
        }
        return -1;
    }

    this->fp = fdopen(this->fd, "w");
    if (!this->fp) {
        this->fp = nullptr;
        if (this->fd >= 0) {
            close(this->fd);
        }
    }
    this->block_fifo_until = 0;
    return 0;
}


int fifo_client::command(const std::string& hostname, const std::string& service, int return_code, const std::string& plugin_output) {
    int rc = -1;
    if (this->block_fifo_until > this->parent->now) {
        return rc;
    }
    this->init_events();

    if (this->fp) {
        size_t maxlen = MAX_HOSTNAME_LENGTH + MAX_DESCRIPTION_LENGTH + MAX_PLUGINOUTPUT_LENGTH + 256;
        char *tmpbuf = (char*)calloc(maxlen, 1);
        size_t wlen = 0;
        if (service.length()) {
            wlen = snprintf(tmpbuf, maxlen, "[%lu] PROCESS_SERVICE_CHECK_RESULT;%s;%s;%d;%s\n",
                            (uint64_t) (this->parent->now), hostname.c_str(), service.c_str(), return_code,
                            plugin_output.c_str());
        } else {
            wlen = snprintf(tmpbuf, maxlen, "[%lu] PROCESS_HOST_CHECK_RESULT;%s;%d;%s\n",
                            (uint64_t) (this->parent->now), hostname.c_str(), return_code,
                            plugin_output.c_str());
        }
        int wsize = write(this->fd, tmpbuf, wlen);
        if (wsize == -1) {
            if (errno == EPIPE) {
                fclose(this->fp);
                this->fp = nullptr;
                this->fd = -1;
            }
        } else if (wsize > 0) {
            fflush(this->fp);
            rc = 0;
        }
        free(tmpbuf);
        tmpbuf = nullptr;
        return rc;
    }
    return rc;
}