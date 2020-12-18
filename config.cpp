//
// Created by macskas on 12/10/20.
//

#include "config.h"
#include "log.h"
#include "common.h"
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cassert>
#include <utility>
#include <sys/stat.h>
#include <unistd.h>


char *ltrim(char *str, const char *seps)
{
    size_t totrim;
    if (seps == nullptr) {
        seps = "\t\n\v\f\r ";
    }
    totrim = strspn(str, seps);
    if (totrim > 0) {
        size_t len = strlen(str);
        if (totrim == len) {
            str[0] = '\0';
        }
        else {
            memmove(str, str + totrim, len + 1 - totrim);
        }
    }
    return str;
}

char *rtrim(char *str, const char *seps)
{
    size_t i;
    if (seps == nullptr) {
        seps = "\t\n\v\f\r ";
    }
    i = strlen(str) - 1;
    while (i >= 0 && strchr(seps, str[i]) != nullptr) {
        str[i] = '\0';
        i--;
    }
    return str;
}

char *trim(char *str, const char *seps)
{
    return ltrim(rtrim(str, seps), seps);
}

config* config::instance = nullptr;
config* config::getInstance()
{
	if (config::instance == nullptr) {
		config::instance = new config;
		assert(config::instance);
	}
	return config::instance;
}

void config::setConfigPath(std::string path_config) {
    this->pathConfig = std::move(path_config);
}

int config::Set(const std::string &Key, std::string Value)
{
	this->data[Key] = std::move(Value);
	return 0;
}

std::string config::Get(const std::string &Key, std::string Default)
{
    configdata_t::iterator it;
    it = this->data.find(Key);
	if (it == this->data.end())
		return Default;
	return it->second;
}

long config::GetInt(const std::string &Key, int Default)
{
    configdata_t::iterator it;
    it = this->data.find(Key);
    if (it == this->data.end())
        return Default;
    return strtol(it->second.c_str(), nullptr, 10);
}

void config::read_config() {
    FILE *fp = nullptr;
    char buf[DBUFFER_4K_SIZE];
    int i = 0;
    int llen = 0;
    std::string opt_key;
    std::string opt_val;
    char *helper = nullptr;
    DMEMZERO(buf, DBUFFER_4K_SIZE);
    fp = fopen(this->pathConfig.c_str(), "r");
    if (!fp) {
        error_sprintf("[%s] enable to open: %s for reading (%s)", __PRETTY_FUNCTION__, this->pathConfig.c_str(), strerror(errno));
        exit(1);
    }


    while (fgets(buf, DBUFFER_4K_SIZE, fp)) {
        opt_key.clear();
        opt_val.clear();

        trim(buf, "\r\n\t ");
        llen = strlen(buf);
        if (llen < 1)
            continue;

        for (i=llen-1; i>=0; i--) {
            if (buf[i] == '#') {
                buf[i] = 0;
                i = 0;
            }
        }

        if ( strlen(buf) == 0)
            continue;
        trim(buf, "\r\n\t ");

        helper = nullptr;
        helper = strchr(buf, '=');
        if (helper) {
            if (strlen(helper) > 1) {
                buf[helper-buf] = 0;
                opt_key.append(buf);
                opt_val.append(helper+1);
                if (this->valid_keywords.find(opt_key) != this->valid_keywords.end()) {
                    this->Set(opt_key, opt_val);
                } else {
                    warning_sprintf("unknown variable in config: %s", opt_key.c_str());
                }
            }
        }
    }

    fclose(fp);
}

void config::set_allowed_config_keys() {
    this->valid_keywords.insert("check_result_path");
    this->valid_keywords.insert("pid_file");
    this->valid_keywords.insert("command_file");
    this->valid_keywords.insert("nsca_user");
    this->valid_keywords.insert("nsca_group");
    this->valid_keywords.insert("server_address");
    this->valid_keywords.insert("server_port");
    this->valid_keywords.insert("debug");
    this->valid_keywords.insert("max_packet_age");
    this->valid_keywords.insert("password");
    this->valid_keywords.insert("decryption_method");

    this->valid_keywords.insert("nsca_workers");
    this->valid_keywords.insert("nsca_threads_per_worker");
    this->valid_keywords.insert("max_checks_per_connection");
    this->valid_keywords.insert("max_packet_age_enabled");
    this->valid_keywords.insert("decryption_mode");
}

int config::check_config() {
    int max_packet_age = this->GetInt("max_packet_age", 0);
    int max_packet_age_enabled = this->GetInt("max_packet_age_enabled", 0);
    int debug = this->GetInt("debug", 0);
    int decryption_method = this->GetInt("decryption_method", 0);
    int nsca_workers = this->GetInt("nsca_workers", 0);
    int nsca_threads_per_worker = this->GetInt("nsca_threads_per_worker", 4);
    int max_checks_per_connection = this->GetInt("max_checks_per_connection", 500);
    int decryption_mode = this->GetInt("decryption_mode", DECRYPTION_MODE_SHARED_CRYPT_INSTANCE);

    std::string password = this->Get("password", "");
    std::string server_address = this->Get("server_address", "0.0.0.0");
    int server_port = this->GetInt("server_port", 5667);
    std::string nsca_user = this->Get("nsca_user", "nagios");
    std::string nsca_group = this->Get("nsca_group", "nogroup");
    std::string pid_file = this->Get("pid_file", "/tmp/nsca.pid");

    std::string check_result_path = this->Get("check_result_path", "");
    std::string command_file = this->Get("command_file", "");

    struct stat st{};

    if (debug == 1) {
        debug_enable();
    }

    if (max_packet_age < 0 || max_packet_age > 604800*52) {
        this->Set("max_packet_age", "30");
        max_packet_age = 30;
    }

    if (decryption_method < 0 || decryption_method > 26) {
        error_sprintf("[config] Invalid decryption method: %d", decryption_method);
        return -1;
    }

    if (!password.length()) {
        warning_sprintf("[config] %s", "You better set passwords");
    }

    if (!check_result_path.empty()) {
        if (access(check_result_path.c_str(), W_OK) == 0) {

        } else {
            warning_sprintf("[config] %s", "check_result_path is not writeable");
        }
    }

    if (!command_file.empty()) {
        if (stat(command_file.c_str(), &st) == 0) {
            if ((st.st_mode & S_IFMT) != S_IFIFO) {
                warning_sprintf("[config] command_file is not fifo (%s)", command_file.c_str());
            }
        } else {
            warning_sprintf("[config] unable to stat command_file(%s)", command_file.c_str());
        }
    }

    if (nsca_workers <= 0 || nsca_workers > 2000) {
        warning_sprintf("[config] nsca_workers(%d) <= 0 || > 2000, new value=%d", nsca_workers, 4);
        nsca_workers = 4;
        this->Set("nsca_workers", "4");
    }
    if (nsca_threads_per_worker < 0) {
        warning_sprintf("[config] nsca_threads_per_worker < 0 (%d), new value=%d", nsca_threads_per_worker, 4);
        this->Set("nsca_threads_per_worker", "4");
    }
    if (nsca_threads_per_worker > 1000) {
        warning_sprintf("[config] nsca_threads_per_worker > 1000 (%d), new value=%d", nsca_threads_per_worker, 4);
        this->Set("nsca_threads_per_worker", "4");
    }
    if (max_checks_per_connection < 1) {
        warning_sprintf("[config] max_checks_per_connection < 1 (%d), new value=%d", max_checks_per_connection, 1);
        max_checks_per_connection = 1;
        this->Set("max_checks_per_connection", "1");
    }

    debug_sprintf("[config] debug=%d", debug);
    debug_sprintf("[config] pid_file='%s'", pid_file.c_str());

    debug_sprintf("[config] server_address='%s'", server_address.c_str());
    debug_sprintf("[config] server_port=%d", server_port);

    debug_sprintf("[config] nsca_user='%s'", nsca_user.c_str());
    debug_sprintf("[config] nsca_group='%s'", nsca_group.c_str());

    debug_sprintf("[config] command_file='%s'", command_file.c_str());
    debug_sprintf("[config] check_result_path='%s'", check_result_path.c_str());

    debug_sprintf("[config] max_packet_age=%d", max_packet_age);
    debug_sprintf("[config] max_packet_age_enabled=%d", max_packet_age_enabled);

    debug_sprintf("[config] decryption_method=%d", decryption_method);
    debug_sprintf("[config] password='%s'", password.c_str());

    debug_sprintf("[config] nsca_workers='%d'", nsca_workers);
    debug_sprintf("[config] nsca_threads_per_worker='%d'", nsca_threads_per_worker);
    debug_sprintf("[config] max_checks_per_connection='%d'", max_checks_per_connection);
    debug_sprintf("[config] decryption_mode/SHARED_CRYPT_INSTANCE=%d", (decryption_mode & DECRYPTION_MODE_SHARED_CRYPT_INSTANCE) ? 1 : 0);

    return 0;
}

config::config() {
    this->set_allowed_config_keys();
}
