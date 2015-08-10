#ifndef CLUSTER_NETWORK_H
#define CLUSTER_NETWORK_H

#include "host.h"

namespace Cluster {
    bool verify_connectivity();
    void start_accept_thread();
    void connect_to_host(Host h);
}
#endif
