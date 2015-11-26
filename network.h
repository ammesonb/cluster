#ifndef CLUSTER_NETWORK_H
#define CLUSTER_NETWORK_H

#include "host.h"

#define MSG_DELIM "#EOM#"

namespace Cluster {
    bool verify_connectivity();

    string srecv(int sock);

    string enc_msg(string msg, string passwd);
    string dec_msg(string msg, string passwd);

    void* send_file(string path);

    void* recv_loop(void *arg);
    void* notify_offline(void *arg);
    void start_accept_thread(int port);
    void connect_to_host(int hostid);
    void update_dns();
    void set_sock_opts(int fd);
}
#endif
