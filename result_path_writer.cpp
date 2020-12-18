//
// Created by macskas on 12/10/20.
//

#include "result_path_writer.h"
#include "log.h"
#include "common.h"
#include <cstdlib>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

int write_result_path(const std::string& check_result_path, time_t now, const std::string& cnow, const std::string& hostname, const std::string& service, int return_code, const std::string& plugin_output) {
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
