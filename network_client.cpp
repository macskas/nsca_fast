//
// Created by macskas on 12/9/20.
//

#include <string>
#include "network_client.h"
#include "common.h"
#include "log.h"

#include "nsca_common.h"
#include "nsca_utils.h"
#include "result_path_writer.h"
#include "threadManager.h"


network_client::network_client() {
    this->client_id = 0;
    this->clientPort = 0;
    this->read_timeout = 8;
    this->write_timeout = 3;
    this->connection_state = 0;
    this->CI = nullptr;

    this->mcrypt_state = nullptr;
    debug_sprintf("[%s]", __PRETTY_FUNCTION__ );
}
network_client::~network_client() {
    if (this->CI && this->has_own_CI) {
        encrypt_cleanup(this->parent->decryption_method, this->CI);
        this->CI = nullptr;
    }

    if (this->mcrypt_state) {
        free(this->mcrypt_state);
        this->mcrypt_state = nullptr;
    }

    while (!this->data_packets.empty()) {
        auto *dp = this->data_packets.front();
        free(dp);
        this->data_packets.pop_front();
    }

    if (this->transmission_iv) {
        free(this->transmission_iv);
        this->transmission_iv = nullptr;
    }

    debug_sprintf("[%s]", __PRETTY_FUNCTION__ );
}

void network_client::debug_message(const std::string& msg) {
    debug_sprintf("[network_client] [client_id=%d, %s:%d] %s", this->client_id, this->clientIp.c_str(), this->clientPort, msg.c_str());
}

void network_client::take_variables(network *cm, evutil_socket_t myfd, struct sockaddr *sa, ev_socklen_t sl) {
    this->parent = cm;
    this->fd = myfd;
    this->socklen = sl;

    DMEMZERO(&(this->ss), sizeof (this->ss));
    memcpy(&(this->ss), sa, sl);
    this->client_id = this->parent->counter++;
    this->parent->connections++;
    this->clientPort = ((struct sockaddr_in *)&this->ss)->sin_port;
    char* ipbuf = nullptr;
    switch(*(&((struct sockaddr *)&this->ss)->sa_family)) {
        case AF_INET:
            ipbuf = (char*)malloc(INET_ADDRSTRLEN);
            DMEMZERO(ipbuf,INET_ADDRSTRLEN);
            inet_ntop(PF_INET, &(((struct sockaddr_in *)&this->ss)->sin_addr), ipbuf, INET_ADDRSTRLEN);
            break;
        case AF_INET6:
            ipbuf = (char*)malloc(INET6_ADDRSTRLEN);
            DMEMZERO(ipbuf,INET6_ADDRSTRLEN);
            inet_ntop(PF_INET6, &(((struct sockaddr_in6 *)&this->ss)->sin6_addr), ipbuf, INET6_ADDRSTRLEN);
            break;
        default:
            ipbuf = (char*)malloc(DBUFFER_4K_SIZE);
            DMEMZERO(ipbuf,DBUFFER_4K_SIZE);
            strncpy(ipbuf, "Unknown AF", DBUFFER_4K_SIZE);
    }
    this->clientIp.assign(ipbuf);
    free(ipbuf);


    if (this->parent->use_network_CI && this->parent->CI) {
        this->set_CI(this->parent->CI);
    }
    if ((this->parent->decryption_mode & DECRYPTION_MODE_SHARED_CRYPT_INSTANCE) == 0) {
        this->transmission_iv = (char*)malloc(TRANSMITTED_IV_SIZE);
        generate_transmitted_iv(this->transmission_iv);
    }
    this->init_events();

    this->debug_message("Client connected");
}

int network_client::init_events()
{
    struct bufferevent *bev = bufferevent_socket_new(this->parent->getBufferEventBase(), this->fd, BEV_OPT_CLOSE_ON_FREE);

    if (!bev) {
        event_base_loopbreak(this->parent->getBufferEventBase());
        this->proper_destroy();
        return -1;
    }

    this->client_bev = bev;

    struct timeval rt_read{};
    struct timeval rt_write{};
    rt_read.tv_sec = this->read_timeout;
    rt_read.tv_usec = 0;
    rt_write.tv_sec = this->write_timeout;
    rt_write.tv_usec = 0;
    bufferevent_set_timeouts(bev,&rt_read,&rt_write);
    bufferevent_setcb(bev, network_client::readcb_proxy, network_client::writecb_proxy, network_client::eventcb_proxy, reinterpret_cast<network_client*>(this));
    bufferevent_enable(bev, EV_READ|EV_WRITE);
    return 0;
}

void network_client::eventcb(struct bufferevent *bev, short events)
{
    char		betype		= 0;
    if (events & BEV_EVENT_READING)
        betype = 1;
    else if (events & BEV_EVENT_WRITING)
        betype = 2;

    if (events & BEV_EVENT_EOF) {
        debug_sprintf("[%s] [%s] Connection closed. (EOF)", __PRETTY_FUNCTION__, betype == 1 ? "READ" : "WRITE");
    } else if (events & BEV_EVENT_ERROR) {
        warning_sprintf("[%s] [%s] Connection closed. (Unknown error - 0)", __PRETTY_FUNCTION__,
                        betype == 1 ? "READ" : "WRITE");
    } else if (events & BEV_EVENT_TIMEOUT) {
        warning_sprintf("[%s] [%s] Connection closed. (Timeout)", __PRETTY_FUNCTION__,
                      betype == 1 ? "READ" : "WRITE");
    } else {
        warning_sprintf("[%s] [%s] Connection closed. (Unknown error - 1)", __PRETTY_FUNCTION__,
                      betype == 1 ? "READ" : "WRITE");
    }
    if (this->bytes_received == OLD_PACKET_LENGTH && this->bytes_drained == 0) {
        struct evbuffer			*input = bufferevent_get_input(bev);
        ev_ssize_t				rsize = evbuffer_get_length(input);
        auto                    *data_packet_pair = (data_packet_pair_t *)calloc(1, sizeof(data_packet_pair_t));
        if (!data_packet_pair) {
            bufferevent_free(bev);
            this->connection_closed();
            return;
        }
        int remove_size = evbuffer_remove(input, &data_packet_pair->receive_packet, rsize);
        if (remove_size < (int)rsize) {
            this->d_failed++;
            free(data_packet_pair);
            bufferevent_free(bev);
            this->connection_closed();
            return;
        }
        data_packet_pair->packet_length = rsize;
        this->data_packets.push_back(data_packet_pair);
    }
    bufferevent_free(bev);
    this->connection_closed();
}

void network_client::readcb_proxy(struct bufferevent *bev, void *user_data)
{
    reinterpret_cast<network_client *>(user_data)->readcb(bev);
}

void network_client::writecb_proxy(struct bufferevent *bev, void *user_data)
{
    reinterpret_cast<network_client *>(user_data)->writecb(bev);
}

void network_client::eventcb_proxy(struct bufferevent *bev, short myevents, void *user_data)
{
    reinterpret_cast<network_client *>(user_data)->eventcb(bev, myevents);
}

void network_client::proper_destroy() {
    this->debug_message("Client disconnected");

    this->parent->connections--;
    this->client_bev = nullptr;
    this->fd = -1;

    delete this;
};

void network_client::connection_closed() {
    this->debug_message("Client disconnected");

    this->parent->connections--;
    this->client_bev = nullptr;
    this->fd = -1;

    if ((this->parent->decryption_mode & DECRYPTION_MODE_SHARED_CRYPT_INSTANCE) == 0) {
        // shared_crypt_instance disabled
        if (encrypt_init(this->parent->password.c_str(), this->parent->decryption_method, this->transmission_iv, &(this->CI)) == OK) {
            this->has_own_CI = true;
        } else {
            error_sprintf("[%s] encrypt_init error. bailing out.", __PRETTY_FUNCTION__);
            delete this;
            return;
        }
    }
    if (!this->data_packets.empty()) {
        if (this->parent->nsca_threads_per_worker > 0) {
            threadManager::getInstance()->add_job(THREADMANAGER_METHOD_DECRYPT_PACKET, this);
        } else {
            this->process_queue();
        }
    } else {
        delete this;
    }
}
void network_client::writecb(struct bufferevent *bev)
{
    struct evbuffer                                         *output	= bufferevent_get_output(bev);
    this->debug_message(__PRETTY_FUNCTION__ );

    if ((this->connection_state & CONNECTION_STATE_INIT_SENT) == 0) {
        size_t                  bytes_to_send;
        init_packet             send_packet{};
        if (this->transmission_iv) {
            memcpy(&send_packet.iv[0], this->transmission_iv, TRANSMITTED_IV_SIZE);
        } else {
            memcpy(&send_packet.iv[0], this->parent->shared_transmission_iv, TRANSMITTED_IV_SIZE);
        }
        send_packet.timestamp=(u_int32_t)htonl(this->parent->now);
        bytes_to_send=sizeof(send_packet);
        evbuffer_add(output, &send_packet, bytes_to_send);
        this->connection_state |= CONNECTION_STATE_INIT_SENT;
    } else {
        bufferevent_flush(bev, EV_WRITE, BEV_FINISHED);
        bufferevent_disable(bev, EV_WRITE);
    }
}

void network_client::readcb(struct bufferevent *bev)
{
    struct evbuffer			*input = bufferevent_get_input(bev);
    ev_ssize_t				rsize = evbuffer_get_length(input);
    size_t                  packet_length = sizeof(data_packet);
    //this->debug_message(__PRETTY_FUNCTION__ );

    if (rsize > 0) {
        this->bytes_received += rsize;
    }

    //this->debug_message(__PRETTY_FUNCTION__ );
    //debug_sprintf("%s %d", __PRETTY_FUNCTION__, rsize);
    if (rsize < (ssize_t)packet_length) {
        return;
    }

    if (this->data_packets.size() > this->parent->max_checks_per_connection) {
        warning_sprintf("[%s] data_packets.size() > max_checks_per_connection (%d)", __PRETTY_FUNCTION__, this->parent->max_checks_per_connection);
        bufferevent_disable(bev, EV_READ);
        bufferevent_free(bev);
        this->proper_destroy();
        return;
    }

    auto *data_packet_pair = (data_packet_pair_t *)calloc(1, sizeof(data_packet_pair_t));
    if (!data_packet_pair)
        return;

    int remove_size = evbuffer_remove(input, &data_packet_pair->receive_packet, packet_length);
    if (remove_size < (int)packet_length) {
        this->d_failed++;
        free(data_packet_pair);
        bufferevent_disable(bev, EV_READ);
        bufferevent_free(bev);
        this->proper_destroy();
        return;
    }
    this->bytes_drained += (size_t)remove_size;
    data_packet_pair->packet_length = packet_length;
    this->data_packets.push_back(data_packet_pair);
}

void network_client::process_queue() {
    if (this->parent->decryption_method > 1) {
        this->process_queue_mcrypt();
    } else {
        this->process_queue_nomcrypt();
    }

    debug_sprintf("[%s] [success=%d failed=%d]", __PRETTY_FUNCTION__, d_success, d_failed );
    this->parent->report_success_failed(d_success, d_failed);
    delete this;
}

void network_client::process_queue_mcrypt() {
    this->debug_message(__PRETTY_FUNCTION__ );

    int                 i = 1;
    u_int32_t           packet_crc32 = 0;
    u_int32_t           calculated_crc32 = 0;
    int                 packet_diff = 0;
    uint64_t            received_timestamp = 0;
    int                 max_packet_age = this->parent->max_packet_age;
    data_packet_pair_t  *data_packet_pair = nullptr;
    data_packet         *receive_packet = nullptr;

    this->mcrypt_state = (char*)calloc(1, 128);
    if (!this->mcrypt_state) {
        return;
    }
    this->mcrypt_state_size = this->CI->iv_size;
    memcpy(this->mcrypt_state, this->CI->IV, this->CI->iv_size);

    //mcrypt_enc_get_state(this->CI->td, this->CI->IV, this->CI->iv_size);
    while (!this->data_packets.empty()) {
        data_packet_pair = data_packets.front();
        receive_packet = &data_packet_pair->receive_packet;
        if (i == 1) {
            mcrypt_enc_set_state(this->CI->td, this->CI->IV, this->CI->iv_size);
        } else {
            mcrypt_enc_set_state(this->CI->td, this->mcrypt_state, this->mcrypt_state_size);
        }
        decrypt_buffer((char *)receive_packet,sizeof(data_packet),this->parent->password.c_str(),this->parent->decryption_method,this->CI);
        mcrypt_enc_get_state(this->CI->td, this->mcrypt_state, &this->mcrypt_state_size);

        if(ntohs(receive_packet->packet_version) != NSCA_PACKET_VERSION_3) {
            this->debug_message("Invalid packet version");
            d_failed++;
            break;
        }
        packet_crc32 = ntohl(receive_packet->crc32_value);
        receive_packet->crc32_value = 0L;
        calculated_crc32 = calculate_crc32((char *)receive_packet,data_packet_pair->packet_length);

        if (calculated_crc32 != packet_crc32) {
            this->debug_message("Invalid CRC");
            d_failed++;
            break;
        }

        if (this->parent->max_packet_age_enabled && max_packet_age > 0) {
            received_timestamp = htonl(receive_packet->timestamp);
            if (received_timestamp > this->parent->now) {
                packet_diff = (int) (received_timestamp - this->parent->now);
            } else {
                packet_diff = (int) (this->parent->now - received_timestamp);
            }

            if (packet_diff > max_packet_age) {
                this->debug_message("max_packet_age reached. dropping packet. dont process more packet on this connection");
                d_failed++;
                break;
            }
        }

        this->send_receive_message(receive_packet);

        i++;
        free(data_packet_pair);
        data_packets.pop_front();
    }
    this->debug_message("process stop");
}

void network_client::process_queue_nomcrypt() {
    this->debug_message(__PRETTY_FUNCTION__ );

    int                 i = 1;
    u_int32_t           packet_crc32 = 0;
    u_int32_t           calculated_crc32 = 0;
    unsigned long       max_packet_age = this->parent->max_packet_age;
    int                 packet_diff = 0;
    uint64_t            received_timestamp = 0;
    data_packet_pair_t  *data_packet_pair = nullptr;
    data_packet         *receive_packet = nullptr;


    while (!this->data_packets.empty()) {
        data_packet_pair = data_packets.front();
        receive_packet = &data_packet_pair->receive_packet;
        decrypt_buffer((char *)receive_packet,sizeof(data_packet),this->parent->password.c_str(),this->parent->decryption_method,this->CI);

        if(ntohs(receive_packet->packet_version) != NSCA_PACKET_VERSION_3) {
            this->debug_message("Invalid packet version");
            d_failed++;
            break;
        }
        packet_crc32 = ntohl(receive_packet->crc32_value);
        receive_packet->crc32_value = 0L;
        calculated_crc32 = calculate_crc32((char *)receive_packet,data_packet_pair->packet_length);

        if (calculated_crc32 != packet_crc32) {
            this->debug_message("Invalid CRC");
            d_failed++;
            break;
        }

        if (this->parent->max_packet_age_enabled && max_packet_age > 0) {
            received_timestamp = htonl(receive_packet->timestamp);
            if (received_timestamp > this->parent->now) {
                packet_diff = (int) (received_timestamp - this->parent->now);
            } else {
                packet_diff = (int) (this->parent->now - received_timestamp);
            }

            if (packet_diff > max_packet_age) {
                this->debug_message("max_packet_age reached. dropping packet. dont process more packet on this connection");
                d_failed++;
                break;
            }
        }

        this->send_receive_message(receive_packet);

        i++;
        free(data_packet_pair);
        data_packets.pop_front();
    }
    this->debug_message("process stop");
}

void network_client::set_CI(struct crypt_instance *myCI) {
    if (this->has_own_CI) {
        return;
    }
    this->CI = myCI;
    //encrypt_init(this->parent->password.c_str(), this->parent->decryption_method, this->parent->shared_transmission_iv, &(this->CI));
}

void network_client::send_receive_message(data_packet *receive_packet)
{
    int rc = 0;
    int return_code = ntohs(receive_packet->return_code);

    rc = 0;
    if (this->parent->fifoClient && !this->parent->command_file.empty()) {
        if (this->parent->fifoClient->command(receive_packet->host_name, receive_packet->svc_description,
                                              return_code, receive_packet->plugin_output) == 0) {
            rc++;
        }
    }
    if (!rc && !this->parent->check_result_path.empty()) {
        if (write_result_path(this->parent->check_result_path, this->parent->now, this->parent->cnow,
                              receive_packet->host_name, receive_packet->svc_description, return_code,
                              receive_packet->plugin_output) == 0) {
            rc++;
        }
    }

    if (rc) {
        d_success++;
    } else {
        d_failed++;
    }
}