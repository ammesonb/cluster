#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

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
                    // TODO Does this block?
                    send(h.socket, msg.c_str(), msg.length(), 0);
                }
            }
        }
        PRINTD(1, 0, "Sender for host %s terminating", host->address.c_str());
        return NULL;
    }/*}}}*/

    void* accept_conn(void *arg) {/*{{{*/
        while (keep_running) {
            struct sockaddr_in client_addr;
            int client_len = sizeof(client_addr);
            int client_fd = accept(acceptfd, (struct sockaddr*)&client_addr, (socklen_t*)&client_len);
            char* addr = inet_ntoa(client_addr.sin_addr);
            if (client_fd < 0) {PRINTD(1, 0, "Failed to accept a connection from %s", addr); continue;}
            // TODO will this block?
            string hostdata = srecv(client_fd);
            start_split(hostdata, "--");
            int level = get_split_level();
            int hostid = stoi(get_split());
            string hostname = get_split();
            while (get_split_level() == level)
                hostname.append(get_split());
            PRINTD(3, 0, "Got host connection from ID %d: %s", hostid, hostname.c_str());
            // TODO Blocking here again
            string passwd = srecv(client_fd);
            string ip;
            ip.assign(addr);

            Host h = host_list.at(hostid);
            if (h.authenticate(hostname, passwd, ip)) {
                PRINTD(4, 0, "Host %s connection authenticated", hostname.c_str());
            } else {
                PRINTD(1, 0, "Host %s connection didn't authenticate!", hostname.c_str());
                continue;
            }

            h.socket = client_fd;
            h.last_msg = get_cur_time();
            hosts_online.push_back(h);
            pthread_t sender_thread;
            pthread_create(&sender_thread, NULL, sender_loop, &h);
        }

        PRINTD(1, 0, "Accept thread is exiting");
        return NULL;
    }/*}}}*/

    void start_accept_thread(int port) {/*{{{*/
        PRINTDI(3, "Configuring connection accept socket");
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

    void connect_to_host(Host h) {
        // TODO write this
    }

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
        timeout.tv_sec = 2;
        timeout.tv_usec = 0;
        if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout)) == -1) {
            DIE("Failed to set option RCVTIMEO");
        }
        if (setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeout, sizeof(timeout)) == -1) {
            DIE("Failed to set option SNDTIMEO");
        }

        int flags = fcntl(sockfd, F_GETFL);
        flags |= O_NONBLOCK;
        fcntl(sockfd, F_SETFL, flags);
    } /*}}}*/
}
