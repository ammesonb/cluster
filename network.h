#ifndef CLUSTER_NETWORK_H
#define CLUSTER_NETWORK_H

#include "host.h"

namespace Cluster {
    bool verify_connectivity();

    string srecv(int sock);

    void start_accept_thread(int port);
    void connect_to_host(Host h);
    void set_sock_opts(int fd);
}
#endif
