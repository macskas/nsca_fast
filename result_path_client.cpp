//
// Created by macskas on 4/16/21.
//

/**
 * This class is used from network main thread.
 * If nagios is not running check result path directory would fill up with small files eventually filling the device.
 * If it is a size limited tmpfs its not a problem. But if someone forgets to set limits or use a simple directory on
 * rootfs then it could be a problem.
 *
 *  init() should be called after the class is created
 *  uninit() when its not needed
 *  check() is running every X seconds from network/timer proxy
 *    - check if inode changed for directory.
 *    - check if inotify needs to be reinitialized (directory removed, renamed, remounted, etc)
 */

#include "result_path_client.h"

#include "common.h"
#include "log.h"
#include "config.h"

#include "nsca_common.h"

extern "C" {
#include <event.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>

#include <sys/types.h>
}
#include <cstdlib>

/**
 *
 * @param network
 */
result_path_client::result_path_client(class network *network) {
    this->parent = network;

    auto *cfg = config::getInstance();
    if (cfg) {
        this->check_result_path_max_files = (int)(cfg->GetInt("check_result_path_max_files", 0));
    }

    if (this->check_result_path_max_files) {
        this->inotifyFd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
        if (this->inotifyFd == -1) {
            warning_sprintf("%s inotify_init() failed: %s", __PRETTY_FUNCTION__, strerror(errno));
            return;
        }
    }
}

result_path_client::~result_path_client() {
    this->uninit();
}

/**
 * Just a static proxy to the client class
 * @param bev
 * @param user_data
 */
void result_path_client::readcb_proxy(struct bufferevent *bev, void *user_data)
{
    reinterpret_cast<result_path_client *>(user_data)->readcb(bev);
}

/**
 * receive inotify events.
 * @param bev
 */
void result_path_client::readcb(struct bufferevent *bev)
{
    char buf[INOTIFY_BUF_LEN];
    char *p = nullptr;

    DMEMZERO(buf, INOTIFY_BUF_LEN);

    size_t num_read = bufferevent_read(bev, buf, INOTIFY_BUF_LEN);
    for (p = buf; p < buf + num_read; ) {
        struct inotify_event *event = (struct inotify_event*) p;
        this->event_received(event);
        p += sizeof(struct inotify_event) + event->len;
    }
}

/**
 * Inotify event handler, to handle file counting
 * @param i
 */
void result_path_client::event_received(struct inotify_event *i) {
    if (i == nullptr)
        return;

    if (!(i->mask & IN_ISDIR)) {
        if (i->mask & IN_DELETE) {
            this->files_in_directory--;
            //debug_sprintf("%s file_deleted(%s)", __PRETTY_FUNCTION__ , i->name);
        }
        if (i->mask & IN_CREATE) {
            this->files_in_directory++;
            //debug_sprintf("%s file_created(%s) - %d", __PRETTY_FUNCTION__ , i->name, this->files_in_directory);
        }
    }
    if (i->mask & IN_IGNORED) {
        // dir removed, umounted, etc
        this->dir_watched = false;
        this->wd = -1;
        this->dir_inode = 0;
        debug_sprintf("%s watch_removed", __PRETTY_FUNCTION__ );
    }
}

/**
 * Initialize inotify watch if we have to
 */
void result_path_client::watch_directory()
{
    if (this->inotifyFd == -1)
        return;

    if (!dir_watched) {
        this->count_files();
        int rc = inotify_add_watch(this->inotifyFd, this->parent->check_result_path.c_str(),
                                   IN_CREATE|IN_DELETE|IN_ONLYDIR);
        if (rc == -1) {
            warning_sprintf("%s inotify_add_watch() failed: %s", __PRETTY_FUNCTION__, strerror(errno));
            return;
        }
        {
            //set watched inode
            struct stat st{};
            if (stat(this->parent->check_result_path.c_str(), &st) == 0) {
                this->dir_inode = st.st_ino;
            }
        }
        this->wd = rc;
        this->dir_watched = true;
        debug_sprintf("%s - %s", __PRETTY_FUNCTION__, this->parent->check_result_path.c_str());
    }
}

/**
 * Try to remove inotify watches
 */
void result_path_client::unwatch_directory() {
    if (this->inotifyFd == -1)
        return;

    if (this->wd == -1) {
        return;
    }

    int rc = inotify_rm_watch(this->inotifyFd, this->wd);
    if (rc == -1) {
        warning_sprintf("%s inotify_rm_watch() failed: %s", __PRETTY_FUNCTION__, strerror(errno));
        return;
    }
    debug_sprintf("%s - %s", __PRETTY_FUNCTION__, this->parent->check_result_path.c_str());
}

/**
 * Initialize a new libevent bufferevent reader for inotify.
 */
void result_path_client::init() {
    if (this->inotifyFd == -1)
        return;

    if (this->client_bev == nullptr) {
        struct event_base *tmp_base = this->parent->getBufferEventBase();
        if (tmp_base == nullptr) {
            warning_sprintf("%s parent event base is null", __PRETTY_FUNCTION__);
            return;
        }

        this->client_bev = bufferevent_socket_new(tmp_base, this->inotifyFd, 0);
        if (this->client_bev == nullptr) {
            warning_sprintf("%s cannot create bufferevent socket", __PRETTY_FUNCTION__);
            return;
        }

        bufferevent_setcb(this->client_bev, result_path_client::readcb_proxy, nullptr, nullptr, (void*)this);
        bufferevent_enable(this->client_bev, EV_READ);
    }

    this->watch_directory();
}

/**
 * Just cleanup it only runs once in a process lifetime, but still.
 */
void result_path_client::uninit() {
    if (this->client_bev) {
        bufferevent_disable(this->client_bev, EV_READ);
        bufferevent_free(this->client_bev);
        this->client_bev = nullptr;
    }

    this->unwatch_directory();

    if (this->inotifyFd != -1) {
        close(this->inotifyFd);
        this->inotifyFd = -1;
    }
}


/**
 * We check inode changes and file number thresholds for watch directory.
 */
void result_path_client::check() {
    if (this->check_result_path_max_files == 0)
        return;

    if (this->inotifyFd == -1)
        return;

    if (this->parent->check_result_path.empty())
        return;

    if (this->check_result_path_max_files > this->files_in_directory) {
        if (!this->write_enabled) {
            warning_sprintf("%s check_result_path write is enabled. (file count: %d)", __PRETTY_FUNCTION__, this->files_in_directory );
        }
        this->write_enabled = true;
    } else {
        if (this->write_enabled) {
            warning_sprintf("%s check_result_path write is disabled. (file count: %d)", __PRETTY_FUNCTION__, this->files_in_directory );
        }
        this->write_enabled = false;
    }

    struct stat st{};
    int rc = 0;

    rc = stat(this->parent->check_result_path.c_str(), &st);
    if (rc != 0)
        return;

    if (!S_ISDIR(st.st_mode)) {
        return;
    }

    if (this->dir_watched && this->dir_inode != st.st_ino) {
        debug_sprintf("%s - dir_inode != st.st_ino (%llu != %llu)", __PRETTY_FUNCTION__, this->dir_inode, st.st_ino );
        this->unwatch_directory();
        this->watch_directory();
        return;
    }

    if (this->dir_watched)
        return;

    this->unwatch_directory();
    this->watch_directory();
}

/**
 * If its not a tmpfs it costs unnecessary IO compared to inotify file tracking. But directory has to be
 * checked before inotify is initialized. Based on the number of files we can disable/enable saving results.
 */
void result_path_client::count_files() {
    this->files_in_directory = 0;
    DIR *dirp = nullptr;
    struct dirent *entry = nullptr;
    dirp = opendir(this->parent->check_result_path.c_str());
    if (dirp == nullptr)
        return;
    while ( (entry = readdir(dirp)) ) {
        if (entry->d_type == DT_REG) {
            this->files_in_directory++;
        }
    }
    closedir(dirp);
}

/**
 * Save results if write is not disabled.
 *
 * @param check_result_path
 * @param now
 * @param cnow
 * @param hostname
 * @param service
 * @param return_code
 * @param plugin_output
 * @return
 */
int result_path_client::write_result(const std::string& check_result_path, time_t now, const std::string& cnow, const std::string& hostname, const std::string& service, int return_code, const std::string& plugin_output) const {
    if (!this->write_enabled)
        return -5;
    int rc = 0;
    size_t tmpbuf_size_max = 4096+512;
    size_t bytes_written = 0;
    mode_t new_umask=077;
    mode_t old_umask;
    char *checkresult_file = nullptr;
    char *checkresult_file_ok = nullptr;
    char *tmpbuf = nullptr;
    int checkresult_file_fd = 0;
    FILE *checkresult_file_fp = nullptr;
    std::string fbuf;

    old_umask = umask(new_umask);

    checkresult_file = (char *)calloc(FILENAME_MAX, 1);
    if (!checkresult_file)
        goto writer_cleanup;

    checkresult_file_ok = (char *)calloc(FILENAME_MAX, 1);
    if (!checkresult_file_ok)
        goto writer_cleanup;

    tmpbuf = (char*)calloc(tmpbuf_size_max, 1);
    if (!tmpbuf)
        goto writer_cleanup;

    DMEMZERO(checkresult_file, FILENAME_MAX);
    DMEMZERO(checkresult_file_ok, FILENAME_MAX);
    fbuf.reserve(4096);


    snprintf(checkresult_file, FILENAME_MAX, "%s/cXXXXXX", check_result_path.c_str());
    checkresult_file_fd = mkstemp(checkresult_file);
    if (!checkresult_file_fd) {
        warning_sprintf("[%s] mkstemp error: %s", __PRETTY_FUNCTION__, strerror(errno) );
        rc = -4;
        goto writer_cleanup;
    }

    if (checkresult_file_fd > 0) {
        checkresult_file_fp = fdopen(checkresult_file_fd, "w");
        if (!checkresult_file_fp) {
            warning_sprintf("[%s] fdopen error: %s", __PRETTY_FUNCTION__, strerror(errno) );
            rc = -2;
            goto writer_cleanup;
        }
    } else {
        warning_sprintf("[%s] mkstemp error: %s", __PRETTY_FUNCTION__, strerror(errno) );
        rc = -1;
        goto writer_cleanup;
    }

    snprintf(tmpbuf,tmpbuf_size_max,"### NSCA Passive Check Result ###\n"); fbuf.append(tmpbuf);
    snprintf(tmpbuf,tmpbuf_size_max,"# Time: %s",cnow.c_str()); fbuf.append(tmpbuf);
    snprintf(tmpbuf,tmpbuf_size_max,"file_time=%ld\n\n", now); fbuf.append(tmpbuf);
    snprintf(tmpbuf,tmpbuf_size_max,"### %s Check Result ###\n",service.length() ? "Service" : "Host"); fbuf.append(tmpbuf);
    snprintf(tmpbuf,tmpbuf_size_max,"host_name=%s\n",hostname.c_str()); fbuf.append(tmpbuf);
    if(service.length()) {
        snprintf(tmpbuf, tmpbuf_size_max, "service_description=%s\n", service.c_str());
        fbuf.append(tmpbuf);
    }
    snprintf(tmpbuf,tmpbuf_size_max,"check_type=1\n"); fbuf.append(tmpbuf);
    snprintf(tmpbuf,tmpbuf_size_max,"scheduled_check=0\n"); fbuf.append(tmpbuf);
    snprintf(tmpbuf,tmpbuf_size_max,"reschedule_check=0\n"); fbuf.append(tmpbuf);
    /* We have no latency data at this point. */
    snprintf(tmpbuf,tmpbuf_size_max,"latency=0\n"); fbuf.append(tmpbuf);
    snprintf(tmpbuf,tmpbuf_size_max,"start_time=%lu.%lu\n",now,0L); fbuf.append(tmpbuf);
    snprintf(tmpbuf,tmpbuf_size_max,"finish_time=%lu.%lu\n",now,0L); fbuf.append(tmpbuf);
    snprintf(tmpbuf,tmpbuf_size_max,"return_code=%d\n",return_code); fbuf.append(tmpbuf);
    /* newlines in output are already escaped */
    snprintf(tmpbuf,tmpbuf_size_max,"output=%s\n", plugin_output.length() ? plugin_output.c_str() : ""); fbuf.append(tmpbuf);
    snprintf(tmpbuf,tmpbuf_size_max,"\n"); fbuf.append(tmpbuf);

    bytes_written = fwrite(fbuf.c_str(), fbuf.length(), 1, checkresult_file_fp);
    fclose(checkresult_file_fp);
    checkresult_file_fp = nullptr;

    if (bytes_written != 1) {
        warning_sprintf("[%s] cannot write full buffer to file.", __PRETTY_FUNCTION__ );
        unlink(checkresult_file);
        goto writer_cleanup;
    }
    snprintf(checkresult_file_ok, FILENAME_MAX, "%s.ok", checkresult_file);
    checkresult_file_fp = fopen(checkresult_file_ok, "w");
    if (checkresult_file_fp) {
        fclose(checkresult_file_fp);
    } else {
        warning_sprintf("[%s] - unable to open checkresult_file_ok, unlink original check_result_file too (%s)", __PRETTY_FUNCTION__, strerror(errno));
        unlink((checkresult_file));
    }

    writer_cleanup:
    umask(old_umask);
    if (checkresult_file)
        free(checkresult_file);
    if (checkresult_file_ok)
        free(checkresult_file_ok);
    if (tmpbuf)
        free(tmpbuf);
    return rc;
}
