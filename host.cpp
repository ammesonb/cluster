#include "common.h"
#include "host.h"

namespace Cluster {
    bool Host::authenticate(int hostid, string identifier, string ip) {
        // TODO check if identifier resolves to ip
        if (host_list[hostid].address != identifier) return false;
        return true;
    }
}
