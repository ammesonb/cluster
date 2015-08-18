#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>
#include <netinet/ip.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <errno.h>
#include <string.h>

#include <algorithm>

#include "common.h"
#include "network.h"

namespace Cluster {
    int acceptfd;

    bool verify_connectivity() {/*{{{*/
        return (system("curl -s www.google.com -o /dev/null") == 0);
    }/*}}}*/

    string srecv(int sock) {/*{{{*/
        PRINTDI(5, "Waiting to receive size of data from socket");
        char *buf = create_str(8);
        recv(sock, buf, 8, 0);
        string size;
        size.reserve(8);
        size.assign(buf, 8);
        PRINTDI(5, "Got size %d", stoi(size));
        free(buf);

        PRINTDI(5, "Waiting to receive data from socket");
        char *data = create_str(stoi(size));
        recv(sock, data, stoi(size), 0);
        string str;
        str.reserve(stoi(size));
        str.assign(data, stoi(size));
        return str;
    }/*}}}*/

    string enc_msg(string msg, string passwd) {/*{{{*/
        // TODO what if msg is larger than this?
        unsigned char outbuf[4096];
        int outlen, secondlen;

        unsigned char iv[32];
        RAND_load_file("/dev/urandom", 128);
        RAND_bytes(iv, 32);
        EVP_CIPHER_CTX ctx;
        EVP_CIPHER_CTX_init(&ctx);
        EVP_EncryptInit_ex(&ctx, EVP_aes_128_cbc(), NULL, (const unsigned char*)passwd.c_str(), iv);
        if (!EVP_EncryptUpdate(&ctx, outbuf, &outlen, (const unsigned char*)msg.c_str(), msg.length())) {
            PRINTD(1, 0, "Encryption of message failed in EncryptUpdate");
            return string("");
        }

        if (!EVP_EncryptFinal_ex(&ctx, outbuf + outlen, &secondlen)) {
            PRINTD(1, 0, "Encryption of message failed in EncryptFinal");
            return string("");
        }
        outlen += secondlen;
        EVP_CIPHER_CTX_cleanup(&ctx);
        PRINTDI(5, "Using IV %s", hexlify(iv, 32).c_str());
        PRINTDI(5, "Data is %s", hexlify(outbuf, outlen).c_str());

        return hexlify(iv, 32).append(hexlify(outbuf, outlen));
    }/*}}}*/

    string dec_msg(string msg, string passwd) {/*{{{*/
        // TODO what is msg is larger than this?
        unsigned char outbuf[4096];
        int outlen, secondlen;

        PRINTDI(5, "Found IV %s", msg.substr(0, 64).c_str());
        PRINTDI(5, "Data is %s", msg.substr(64, msg.length() - 64).c_str());

        string iv = unhexlify(msg.substr(0, 64));
        string data = unhexlify(msg.substr(64, msg.length() - 64));

        EVP_CIPHER_CTX ctx;
        EVP_CIPHER_CTX_init(&ctx);
        EVP_DecryptInit_ex(&ctx, EVP_aes_128_cbc(), NULL,
                           (const unsigned char*)passwd.c_str(),
                           (const unsigned char*)iv.c_str());
        if (!EVP_DecryptUpdate(&ctx, outbuf, &outlen, (const unsigned char*)data.c_str(), data.length())) {
            PRINTD(1, 0, "Decryption of message failed in DecryptUpdate");
            return string("");
        }

        if (!EVP_DecryptFinal_ex(&ctx, outbuf + outlen, &secondlen)) {
            PRINTD(1, 0, "Decryption of message failed in DecryptFinal");
            return string("");
        }
        outlen += secondlen;
        EVP_CIPHER_CTX_cleanup(&ctx);
        return string((char*)outbuf, outlen);
    }/*}}}*/

    void* sender_loop(void *arg) {/*{{{*/
        Host *host = (Host*)arg;
        while (keep_running) {
            sleep(interval / 2);
            PRINTD(5, 0, "Checking message queue for %s", host->address.c_str());
            for (auto it = send_message_queue[host->id].begin();
                      it != send_message_queue[host->id].end(); it++) {
                string msg = *it;
                PRINTD(5, 1, "Sending %s to all hosts", msg.c_str());
                for (auto it2 = hosts_online.begin(); it2 != hosts_online.end(); it2++) {
                    Host h = *it2;
                    string ctxt = enc_msg(msg, h.password);
                    // TODO Does this block?
                    send(h.socket, ctxt.c_str(), ctxt.length(), 0);
                }
            }
        }
        PRINTD(1, 0, "Sender for host %s terminating", host->address.c_str());
        return NULL;
    }/*}}}*/

    void *recv_loop(void *arg) {/*{{{*/
        while (keep_running) {
            sleep(interval / 2);
            for (auto it = hosts_online.begin(); it != hosts_online.end(); it++) {
                Host h = *it;
                char *buf = create_str(1024);
                if (recv(h.socket, buf, 1024, MSG_DONTWAIT) > 0) {
                    h.last_msg = get_cur_time();
                    if (strcmp(buf, std::to_string(h.id).append("-ping").c_str()) == 0) continue;
                    start_split(string(buf), "--");
                    string command = get_split();
                    PRINTD(4, 0, "Received command %s from host %d", command.c_str(), h.id);
                    if (command == "fs") {
                        // TODO handle file
                    } else if (command == "dyn") {
                        // TODO dynamic host
                    } else if (command == "off") {
                        pthread_t off_t;
                        int hid = h.id;
                        pthread_create(&off_t, NULL, notify_offline, &hid);
                        // TODO host is going offline
                    }
                }
                free(buf);
            }
        }
        PRINTD(1, 0, "Receive thread is terminating");
        return NULL;
    }/*}}}*/

    void* notify_offline(void *arg) {/*{{{*/
        int *hid = (int*)arg;
        PRINTD(2, 0, "Host %d is going offline", *hid);
        Host h = host_list[*hid];
        h.online = false;
        hosts_online.erase(std::remove(hosts_online.begin(), hosts_online.end(), h), hosts_online.end());
        check_services(*hid, false);
        return NULL;
    }/*}}}*/

    void* accept_conn(void *arg) {/*{{{*/
        int client_len = sizeof(struct sockaddr_in);
        while (keep_running) {
            struct sockaddr_in client_addr;
            // Ensure address is zero, since struct is re-used each loop
            client_addr.sin_addr.s_addr = 0;
            int client_fd = accept(acceptfd, (struct sockaddr*)&client_addr, (socklen_t*)&client_len);
            char* addr = inet_ntoa(client_addr.sin_addr);
            // If s_addr is 0 then no connection was actually attempted and the call simply timed out
            if (client_addr.sin_addr.s_addr == 0) continue;
            if (client_fd < 0) {PRINTD(1, 0, "Failed to accept a connection from %s", addr); continue;}
            PRINTD(1, 0, "Waiting to receive auth from %s", addr);
            char *data = create_str(1024);
            string hostdata;
            int read = recv(client_fd, data, 1024, 0);
            if (read <= 0) {PRINTD(1, 0, "Received no data from connection with %s, aborting", addr); continue;}
            hostdata.reserve(read);
            hostdata.append(data);
            free(data);
            start_split(hostdata, "--");
            int level = get_split_level();
            int hostid = stoi(get_split());
            string hostname = dec_msg(get_split(), host_list.at(hostid).password);
            end_split(level);
            PRINTD(3, 0, "Got attempted host connection from ID %d: %s", hostid, hostname.c_str());
            string ip;
            ip.assign(addr);

            Host h = host_list.at(hostid);
            if (h.authenticate(hostid, hostname, ip)) {
                PRINTD(4, 0, "Host %d: %s connection authenticated", hostid, hostname.c_str());
                send(client_fd, "auth", 4, 0);
            } else {
                PRINTD(1, 0, "Host %d: %s connection didn't authenticate!", hostid, hostname.c_str());
                send(client_fd, "noauth", 6, 0);
                continue;
            }

            h.socket = client_fd;
            h.last_msg = get_cur_time();
            h.online = true;
            hosts_online.push_back(h);
            pthread_t sender_thread;
            pthread_create(&sender_thread, NULL, sender_loop, &h);
            
            check_services(h.id, true);
        }

        PRINTD(1, 0, "Accept thread is exiting");
        return NULL;
    }/*}}}*/

    void start_accept_thread(int port) {/*{{{*/
        PRINTDI(3, "Configuring connection accept socket on %d", port);
        acceptfd = socket(AF_INET, SOCK_STREAM, 0);
        set_sock_opts(acceptfd);

        // Create and fill address struct/*{{{*/
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port);/*}}}*/

        if (bind(acceptfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            DIE("Failed to bind accept socket");
        }

        if (listen(acceptfd, 10) < 0) {DIE("Failed to listen on accept socket");}

        pthread_t accept_thread;
        pthread_create(&accept_thread, NULL, accept_conn, NULL);
    }/*}}}*/

    void connect_to_host(Host host) {/*{{{*/
        // TODO this won't work if two dynamic hosts are present in the config file
        // TODO maybe add a startup DDNS update command to ensure that dns addresses resolve properly?
        PRINTDI(3, "Starting connection to %s:%d", host.address.c_str(), host.port);
        struct addrinfo *h = (struct addrinfo*)malloc(sizeof(struct addrinfo) + 1);
        struct addrinfo *res, *rp;
        memset(h, '\0', sizeof(*h));
        h->ai_family = AF_INET;
        h->ai_socktype = SOCK_STREAM;
        h->ai_protocol = 0;

        if (getaddrinfo(host.address.c_str(), std::to_string(host.port).c_str(), h, &res)) {
            PRINTDR(1, 1, "Failed to get address info for %s", host.address.c_str());
            return;
        }

        int sock;
        struct timeval to;
        // Timeout after 5 seconds
        to.tv_sec = 5;
        to.tv_usec = 0;
        bool succeed = false;
        for (rp = res; rp != NULL; rp = rp->ai_next) {
            sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
            setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (char*)&to, sizeof(to));
            setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&to, sizeof(to));
            if (sock == -1) continue;
            ((struct sockaddr_in*)rp->ai_addr)->sin_port = htons(host.port);
            PRINTDI(3, "Resolved to %s", inet_ntoa(((struct sockaddr_in*)rp->ai_addr)->sin_addr));
            if (connect(sock, rp->ai_addr, rp->ai_addrlen) != -1) {
                PRINTDI(3, "Connect succeeded");
                succeed = true;
                break;
            } else {
                PRINTDI(1, "Connection failed with error %d, %s", errno, strerror(errno));
            }
        }
        if (!succeed) {
            PRINTD(1, 0, "Failed to connect to %s", host.address.c_str());
            return;
        }
        set_sock_opts(sock);
        free(h);

        PRINTDI(4, "Authenticating with %s", host.address.c_str());

        host.online = true;
        hosts_online.push_back(host);
        host.socket = sock;
        string msg = enc_msg(host_list[int_id].address, host_list[int_id].password);
        string data;
        data.reserve(my_id.length() + 3 + msg.length());
        data.append(my_id).append("--").append(msg);
        send(sock, data.c_str(), data.length(), 0);
        char *buf = create_str(8);
        recv(sock, buf, 8, 0);
        if (strcmp(buf, "auth") == 0) {
            PRINTDR(1, 1, "Successfully connected to %s", host.address.c_str());
        } else {
            PRINTD(1, 0, "Failed to authenticate with %s, sent %s and received %s", host.address.c_str(), data.c_str(), buf);
        }

        check_services(host.id, true);
    }/*}}}*/

    void set_sock_opts(int sockfd) { /*{{{*/
        int value = 1;
        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &value, sizeof(value)) == -1) {
            DIE("Failed to set option REUSEADDR");
        }

        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, &value, sizeof(value)) == -1) {
            DIE("Failed to set option REUSEPORT");
        }

        struct linger so_linger;
        so_linger.l_onoff = 1;
        so_linger.l_linger = 0;
        if (setsockopt(sockfd, SOL_SOCKET, SO_LINGER, &so_linger, sizeof(so_linger)) == -1) {
            DIE("Failed to set option LINGER");
        }

        struct timeval timeout;
        timeout.tv_sec = 5;
        timeout.tv_usec = 0;
        if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout)) == -1) {
            DIE("Failed to set option RCVTIMEO");
        }
        if (setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeout, sizeof(timeout)) == -1) {
            DIE("Failed to set option SNDTIMEO");
        }

    } /*}}}*/
}
