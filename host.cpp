#include "common.h"
#include "host.h"

namespace Cluster {
    bool Host::authenticate(int hostid, string identifier, string ip) {
        // If hostname doesn't match the given address, then this isn't the host
        // it claims to be
        if (host_list[hostid].address != identifier) return false;
        // TODO check if identifier resolves to ip
        return true;
    }
}
