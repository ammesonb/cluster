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
#include <fstream>

#include "common.h"
#include "network.h"
#include "totp.h"

using std::ofstream;
using std::to_string;

namespace Cluster {
    int acceptfd;

    bool verify_connectivity() {/*{{{*/
        return (system("curl -s www.google.com -o /dev/null") == 0);
    }/*}}}*/

    string srecv(int sock) {/*{{{*/
        PRINTDI(5, "NET", "Waiting to receive size of data from socket");
        char *buf = create_str(8);
        recv(sock, buf, 8, 0);
        string size;
        size.reserve(8);
        size.assign(buf, 8);
        PRINTDI(5, "NET", "Got size %d", stoi(size));
        free(buf);

        PRINTDI(5, "NET", "Waiting to receive data from socket");
        char *data = create_str(stoi(size));
        recv(sock, data, stoi(size), 0);
        string str;
        str.reserve(stoi(size));
        str.assign(data, stoi(size));
        return str;
    }/*}}}*/

    string enc_msg(string msg, string passwd) {/*{{{*/
        // Keep an extra 10% of space available for padding
        // and at least 16 bytes for PKCS7 padding
        unsigned char outbuf[int(msg.length() * 1.1) + 16];
        int outlen, secondlen;

        unsigned char iv[32];
        RAND_load_file("/dev/urandom", 128);
        RAND_bytes(iv, 32);
        EVP_CIPHER_CTX ctx;
        EVP_CIPHER_CTX_init(&ctx);
        EVP_EncryptInit_ex(&ctx, EVP_aes_128_cbc(), NULL, (const unsigned char*)passwd.c_str(), iv);
        if (!EVP_EncryptUpdate(&ctx, outbuf, &outlen, (const unsigned char*)msg.c_str(), msg.length())) {
            PRINTD(1, 0, "NET", "Encryption of message failed in EncryptUpdate");
            return string("");
        }

        if (!EVP_EncryptFinal_ex(&ctx, outbuf + outlen, &secondlen)) {
            PRINTD(1, 0, "NET", "Encryption of message failed in EncryptFinal");
            return string("");
        }
        outlen += secondlen;
        EVP_CIPHER_CTX_cleanup(&ctx);
        PRINTDI(5, "NET", "Encrypt using IV %s", hexlify(iv, 32).c_str());
        PRINTDI(5, "NET", "Encrypt using pass %s", passwd.c_str());
        PRINTDI(5, "NET", "Encrypting %s", hexlify(outbuf, outlen).c_str());
        PRINTDI(5, "NET", "Encrypted value: %s",  hexlify(iv, 32).append(hexlify(outbuf, outlen)).c_str());

        return hexlify(iv, 32).append(hexlify(outbuf, outlen));
    }/*}}}*/

    string dec_msg(string msg, vector<string> passwd_opts) {/*{{{*/
        // Multiple passwords to account for clock skew

        // Keep an extra 10% of space available for padding
        // and at least 16 bytes for PKCS7 padding
        unsigned char outbuf[int(msg.length() * 1.1) + 16];
        int outlen, secondlen;

        PRINTDI(5, "NET", "Decrypt using IV %s", msg.substr(0, 64).c_str());
        PRINTDI(5, "NET", "Decrypting %s", msg.substr(64, msg.length() - 64).c_str());

        string iv = unhexlify(msg.substr(0, 64));
        string data = unhexlify(msg.substr(64, msg.length() - 64));

        int i;
        size_t pass_length = passwd_opts.size();
        for (i = 0; i < passwd_opts.size(); i++) {
            PRINTDI(5, "NET", "Trying password %s\n", passwd_opts[i].c_str());
            EVP_CIPHER_CTX ctx;
            EVP_CIPHER_CTX_init(&ctx);
            EVP_DecryptInit_ex(&ctx, EVP_aes_128_cbc(), NULL,
                               (const unsigned char*)passwd_opts[i].c_str(),
                               (const unsigned char*)iv.c_str());
            if (!EVP_DecryptUpdate(&ctx, outbuf, &outlen, (const unsigned char*)data.c_str(), data.length())) {
                if (i == pass_length - 1) {
                    PRINTD(1, 0, "NET", "Decryption of message failed in DecryptUpdate");
                    return string("");
                } else {
                    continue;
                }
            }

            if (!EVP_DecryptFinal_ex(&ctx, outbuf + outlen, &secondlen)) {
                if (i == pass_length - 1) {
                    PRINTD(1, 0, "NET", "Decryption of message failed in DecryptFinal");
                    return string("");
                } else {
                    continue;
                }
            }
            outlen += secondlen;
            EVP_CIPHER_CTX_cleanup(&ctx);
            return string((char*)outbuf, outlen);
        }
        return "";
    }/*}}}*/

    bool rem_true(string s) {/*{{{*/
        return true;
    }/*}}}*/

    void* sender_loop(void *arg) {/*{{{*/
        int hostid = *((int*)arg);
        while (keep_running) {
            usleep((float)interval / 3.0 * 100000.0);
            if (sem_locked(hosts_busy[hostid])) {
            //if (std::find(VECTORFIND(hosts_busy, hostid)) != hosts_busy.end()) {
                PRINTD(5, 0, "SEND", "Host %d is busy", hostid);
                continue;
            }
            PRINTD(5, 0, "SEND", "Checking message queue for %s", host_list[hostid].address.c_str());
            ITERVECTOR(send_message_queue[hostid], it) {
                string msg = *it;
                PRINTD(5, 1, "SEND", "Sending %s to %d", msg.c_str(), hostid);
                string ctxt = enc_msg(msg, get_totp(host_list[hostid].password, host_list[hostid].address, time(NULL))).append(MSG_DELIM);
                // This blocks
                send(host_list[hostid].socket, ctxt.c_str(), ctxt.length(), 0);
                PRINTDI(5, "SEND", "Sent");
            }
            send_message_queue[hostid].erase(
                std::remove_if(VECTORFIND(send_message_queue[hostid],
                                          rem_true)),
                send_message_queue[hostid].end()
                                            );
        }
        PRINTD(1, 0, "SEND", "Sender for host %s terminating", host_list[hostid].address.c_str());
        free(arg);
        return NULL;
    }/*}}}*/

    void queue_msg(string msg) {/*{{{*/
        ITERVECTOR(hosts_online, it) {
            if (*it == int_id) continue;
            send_message_queue[*it].push_back(msg);
        }
    }/*}}}*/

    void* send_file(string path) {/*{{{*/
        // TODO verify this works
        string fdata = read_file((char*)path.c_str());
        // Length of IV + data + message delimiter
        int length = 32 + path.length() + 5;
        // PKCS7 padding
        length += 16 - (length % 16);
        // Base64 encoding
        length = (length / 3) * 4 + 4;
        ITERVECTOR(hosts_online, it) {
            // TODO add file permissions here?
            if (*it == int_id) continue;
            PRINTD(4, 0, "NET", "Sending file %s to host %d", path.c_str(), *it);
            int ret = sem_wait(&hosts_busy[*it]);
            if (ret) {
                PRINTD(1, 0, "NET", "Failed to lock semaphore");
            }
            // Inform receiver we are sending file
            string info = string("fs").append("--").append(my_id);
            string cinfo = enc_msg(info, get_totp(host_list[*it].password, host_list[*it].address, time(NULL))).append(MSG_DELIM);
            send(host_list[*it].socket, cinfo.c_str(), cinfo.length(), 0);

            char *buf = create_str(256);
            memset(buf, '\0', 256);
            PRINTDI(3, "NET", "Sent initial message, waiting for acknowledgement");
            recv(host_list[*it].socket, buf, 256, 0);
            string msg = dec_msg(string(buf), calculate_totp(host_list[*it].password, host_list[*it].address));
            if (msg.compare("GO") != 0) {
                PRINTD(1, 0, "NET", "Received unknown response in sending file %s to host %d: '%s'", path.c_str(), *it, msg.c_str());
                if (ret != 0) {
                    PRINTD(1, 0, "NET", "Failed to release semaphore");
                }
                PRINTD(1, 0, "NET", "Failed to send file %s to host %d", path.c_str(), *it);
                continue;
            }

            // Start sending file data
            string metadata = path.append("::").append(to_string(length));
            string ctxt = enc_msg(metadata, get_totp(host_list[int_id].password, host_list[int_id].address, time(NULL)));
            PRINTD(5, 1, "NET", "Sending metadata");
            send(host_list[*it].socket, ctxt.c_str(), ctxt.length(), 0);
            recv(host_list[*it].socket, buf, 256, 0);
            msg = dec_msg(buf, calculate_totp(host_list[*it].password, host_list[*it].address));
            if (msg.compare("FAIL") == 0) {
                // TODO what should happen?
                int ret = sem_post(&hosts_busy[*it]);
                if (ret != 0) {
                    PRINTD(1, 0, "NET", "Failed to release semaphore");
                }
                PRINTD(1, 0, "NET", "Failed to send file %s to host %d", path.c_str(), *it);
                continue;
            } else if (msg.compare("OK") != 0) {
                // TODO um....
                PRINTD(1, 0, "NET", "Received unknown response in sending file %s to host %d: '%s'", path.c_str(), *it, msg.c_str());
                int ret = sem_post(&hosts_busy[*it]);
                if (ret != 0) {
                    PRINTD(1, 0, "NET", "Failed to release semaphore");
                }
                continue;
            }
            free(buf);

            ctxt = enc_msg(fdata, get_totp(host_list[*it].password, host_list[*it].address, time(NULL)));
            send(host_list[*it].socket, ctxt.c_str(), ctxt.length(), 0);
        }

        return NULL;
    }/*}}}*/

    void* recv_file(void *arg) {/*{{{*/
        int hid = *((int*)arg);
        string smsg = enc_msg("GO", get_totp(host_list[int_id].password, host_list[int_id].address, time(NULL)));
        send(host_list[hid].socket, smsg.c_str(), smsg.length(), 0);
        char *buf = create_str(1024);
        PRINTD(4, 0, "NET", "Waiting to receive filedata from host %d", hid);
        while (recv(host_list[hid].socket, buf, 1024, MSG_DONTWAIT) <= 0) usleep(10000);
        host_list[hid].last_msg = get_cur_time();
        string fdata = dec_msg(buf, calculate_totp(host_list[hid].password, host_list[hid].address));
        start_split(fdata, "::", STRLITFIX("rfile"));
        string fname = get_split(STRLITFIX("rfile"));
        const char *name = filename(fname).c_str();
        char *dircmd = create_str(fname.length() + 20);
        PRINTD(3, 0, "NET", "Receiving file %s, path %s", name, (char*)dirname(fname).c_str());
        sprintf(dircmd, "mkdir -p %s", dirname(fname).c_str());
        system(dircmd);
        free(dircmd);

        int dlen = std::stoi(get_split(STRLITFIX("rfile")));
        end_split(STRLITFIX("rfile"));
        if (dlen <= 0) {
            PRINTD(1, 0, "NET", "File transfer of %s from %d failed", fname.c_str(), hid);
            send(host_list[hid].socket, "FAIL", 4, 0);
            free(arg);
            int ret = sem_post(&hosts_busy[hid]);
            if (ret != 0) {
                PRINTD(1, 0, "NET", "Failed to release semaphore");
            }
            //hosts_busy.erase(std::remove(VECTORFIND(hosts_busy, hid)), hosts_busy.end());
            return NULL;
        }
        free(buf);
        PRINTD(5, 0, "NET", "Preparing for file transfer");
        string cok = enc_msg("OK", get_totp(host_list[int_id].password, host_list[int_id].address, time(NULL)));
        send(host_list[hid].socket, cok.c_str(), cok.length(), 0);
        char *data = create_str(dlen);
        usleep(500000);
        while (recv(host_list[hid].socket, data, dlen, MSG_DONTWAIT) <= 0) usleep(100000);
        string ptxt = dec_msg(data, calculate_totp(host_list[int_id].password, host_list[int_id].address));
        PRINTD(4, 0, "NET", "Storing file");
        host_list[hid].last_msg = get_cur_time();
        ofstream ofile;
        ofile.open(STRLITFIX(fname.c_str()));
        ofile << ptxt << std::endl;
        ofile.close();
        free(data);

        sync_timestamps[fname] = get_file_mtime((char*)fname.c_str());

        if (strcmp(name, "hosts") == 0) {
            PRINTD(2, 0, "NET", "Reloading host config");
            load_host_config();
        } else if (strcmp(name, "services") == 0) {
            PRINTD(2, 0, "NET", "Reloading service config");
            load_service_config();
        } else if (strcmp(name, "cluster.conf") == 0) {
            PRINTD(2, 0, "NET", "Reloading main config");
            cfg_t *cfg = cfg_init(config, 0);
            cfg_parse(cfg, "cluster.conf");
        }

        free(arg);
        int ret = sem_post(&hosts_busy[hid]);
        if (ret != 0) {
            PRINTD(1, 0, "NET", "Failed to release semaphore");
        }
        //hosts_busy.erase(std::remove(VECTORFIND(hosts_busy, hid)), hosts_busy.end());
        return NULL;
    }/*}}}*/

    void* recv_loop(void *arg) {/*{{{*/
        while (keep_running) {
            usleep((float)interval / 3.0 * 100000.0);
            ITERVECTOR(hosts_online, it) {
                if (!sem_locked(hosts_busy[*it])) {
                    // Read all available data
                    string data;
                    char *buf = create_str(1024);
                    while (recv(host_list[*it].socket, buf, 1024, MSG_DONTWAIT) > 0) {
                        data.append(buf);
                        memset(buf, '\0', 1024);
                        usleep(10000);
                    }
                    free(buf);
                    
                    // If data is actually found
                    if (data.length() > 0) {
                        PRINTD(4, 0, "RECV", "Received %lu bytes", data.length());
                        // TODO check this works
                        start_split(data, MSG_DELIM, STRLITFIX("cmd"));
                        string msg = get_split(STRLITFIX("cmd"));
                        while (msg.length() > 0) {
                            PRINTD(5, 0, "RECV", "Found command with length %lu", msg.length());
                            host_list[*it].last_msg = get_cur_time();
                            msg = dec_msg(msg, calculate_totp(host_list[int_id].password, host_list[int_id].address));
                            // Check if ping
                            if (std::to_string(*it).append("-ping").compare(msg) == 0) {
                                PRINTD(4, 0, "RECV", "Got ping message from %d", *it);
                                msg = get_split(STRLITFIX("cmd"));
                                continue;
                            }

                            // Parse command
                            start_split(msg, "--", STRLITFIX("subcmd"));
                            string command = get_split(STRLITFIX("subcmd"));
                            PRINTD(4, 0, "RECV", "Received command %s from host %d", command.c_str(), *it);
                            if (strcmp(command.c_str(), "fs") == 0) {
                                pthread_t file_thread;
                                int *hid = (int*)malloc(sizeof(int*));
                                *hid = *it;
                                //hosts_busy.push_back(*hid);
                                int ret = sem_wait(&hosts_busy[*hid]);
                                if (ret) {
                                    PRINTD(1, 0, "NET", "Failed to lock semaphore");
                                }
                                pthread_create(&file_thread, NULL, recv_file, hid);
                            } else if (command == "dyn") {
                                // TODO dynamic host
                            } else if (command == "off") {
                                pthread_t off_t;
                                int hid = *it;
                                pthread_create(&off_t, NULL, notify_offline, &hid);
                            }
                            end_split(STRLITFIX("subcmd"));
                            msg = get_split(STRLITFIX("cmd"));
                        }
                        end_split(STRLITFIX("cmd"));
                    }
                } else {
                    PRINTD(5, 0, "RECV", "Host %d is busy", *it);
                }
            }
        }
        PRINTD(1, 0, "RECV", "Receive thread is terminating");
        return NULL;
    }/*}}}*/

    void* notify_offline(void *arg) {/*{{{*/
        int *hid = (int*)arg;
        PRINTD(2, 0, "NET", "Host %d is going offline", *hid);
        Host h = host_list[*hid];
        h.online = false;
        hosts_online.erase(std::remove(VECTORFIND(hosts_online, *hid)), hosts_online.end());
        check_services(*hid, false);
        // TODO will this work with file sync? Such as pidgin logs
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
            if (client_fd < 0) {PRINTD(1, 0, "NET", "Failed to accept a connection from %s", addr); continue;}
            PRINTD(1, 0, "NET", "Waiting to receive auth from %s", addr);
            char *data = create_str(1024);
            string hostdata;
            int read = recv(client_fd, data, 1024, 0);
            if (read <= 0) {PRINTD(1, 0, "NET", "Received no data from connection with %s, aborting", addr); continue;}
            hostdata.reserve(read);
            hostdata.append(data);
            free(data);
            start_split(hostdata, "--", STRLITFIX("hostacc"));
            int hostid = stoi(get_split(STRLITFIX("hostacc")));
            string hostname = dec_msg(get_split(STRLITFIX("hostacc")), calculate_totp(host_list[hostid].password, host_list[hostid].address));
            end_split(STRLITFIX("hostacc"));
            PRINTD(3, 0, "NET", "Got attempted host connection from ID %d: %s", hostid, hostname.c_str());
            string ip;
            ip.assign(addr);

            if (host_list[hostid].authenticate(hostid, hostname, ip)) {
                PRINTD(4, 0, "NET", "Host %d: %s connection authenticated", hostid, hostname.c_str());
                send(client_fd, "auth", 4, 0);
            } else {
                PRINTD(1, 0, "NET", "Host %d: %s connection didn't authenticate!", hostid, hostname.c_str());
                send(client_fd, "noauth", 6, 0);
                continue;
            }

            host_list[hostid].socket = client_fd;
            host_list[hostid].last_msg = get_cur_time();
            host_list[hostid].online = true;
            hosts_online.push_back(hostid);
            pthread_t sender_thread;
            int *hid = (int*)malloc(sizeof(int*));
            *hid = hostid;
            pthread_create(&sender_thread, NULL, sender_loop, hid);
            PRINTD(1, 0, "NET", "Host %d: %s connected", hostid, hostname.c_str());
            
            check_services(hostid, true);
        }

        PRINTD(1, 0, "NET", "Accept thread is exiting");
        return NULL;
    }/*}}}*/

    void start_accept_thread(int port) {/*{{{*/
        PRINTDI(3, "NET", "Configuring connection accept socket on %d", port);
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

    void connect_to_host(int hostid) {/*{{{*/
        // TODO this won't work if two dynamic hosts are present in the config file
        // TODO maybe add a startup DDNS update command to ensure that dns addresses resolve properly?
        Host client = host_list[hostid];
        PRINTDI(3, "NET", "Starting connection to %s:%d", client.address.c_str(), client.port);
        struct addrinfo *h = (struct addrinfo*)malloc(sizeof(struct addrinfo) + 1);
        struct addrinfo *res, *rp;
        memset(h, '\0', sizeof(*h));
        h->ai_family = AF_INET;
        h->ai_socktype = SOCK_STREAM;
        h->ai_protocol = 0;

        if (getaddrinfo(client.address.c_str(), std::to_string(client.port).c_str(), h, &res)) {
            PRINTDR(1, 1, "NET", "Failed to get address info for %s", client.address.c_str());
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
            ((struct sockaddr_in*)rp->ai_addr)->sin_port = htons(client.port);
            PRINTDI(3, "NET", "Resolved to %s", inet_ntoa(((struct sockaddr_in*)rp->ai_addr)->sin_addr));
            if (connect(sock, rp->ai_addr, rp->ai_addrlen) != -1) {
                PRINTDI(3, "NET", "Connect succeeded");
                succeed = true;
                break;
            } else {
                PRINTDI(1, "NET", "Connection failed with error %d, %s", errno, strerror(errno));
            }
        }
        if (!succeed) {
            PRINTD(1, 0, "NET", "Failed to connect to %s", client.address.c_str());
            return;
        }
        set_sock_opts(sock);
        free(h);

        PRINTDI(4, "NET", "Authenticating with %s", client.address.c_str());

        host_list[hostid].online = true;
        hosts_online.push_back(client.id);
        host_list[hostid].socket = sock;

        string msg = enc_msg(host_list[int_id].address, get_totp(host_list[int_id].password, host_list[int_id].address, time(NULL)));
        string data;
        data.reserve(my_id.length() + 3 + msg.length());
        data.append(my_id).append("--").append(msg);
        send(sock, data.c_str(), data.length(), 0);
        char *buf = create_str(8);
        recv(sock, buf, 8, 0);
        if (strcmp(buf, "auth") == 0) {
            PRINTDR(1, 1, "NET", "Successfully connected to %s", client.address.c_str());
        } else {
            PRINTD(1, 0, "NET", "Failed to authenticate with %s, sent %s and received %s", client.address.c_str(), data.c_str(), buf);
            update_dns();
            // TODO Should try to authenticate again somehow?
        }

        host_list[hostid].last_msg = get_cur_time();
        pthread_t sender_thread;
        int* hid = (int*)malloc(sizeof(int*));
        *hid = hostid;
        pthread_create(&sender_thread, NULL, sender_loop, hid);
        check_services(client.id, true);
    }/*}}}*/

    void update_dns() {/*{{{*/
        PRINTD(3, 0, "NET", "Updating DNS records");
        // TODO this
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
