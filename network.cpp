#include "network.h"

namespace Cluster {
    bool verify_connectivity() {
        return (system("curl -s www.google.com -o /dev/null") == 0);
    }

    void start_accept_thread() {
    }

    void connect_to_host(Host h) {
    }
}
