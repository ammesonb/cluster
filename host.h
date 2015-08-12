#ifndef CLUSTER_HOST_H
#define CLUSTER_HOST_H

#include <string>
#include <vector>

#include "service.h"

using std::string;
using std::vector;

namespace Cluster {
    class Host {
        public:
            int id;
            string address;
            int port;
            int socket;
            unsigned long long last_msg = 0;
            bool online = false;
            bool dynamic;
            vector<Service> services;
            string password;

            // TODO this function should use pass as an AES 128 key, so simply valid decryption
            // proves authentication
            // TODO failed fqdn->ip mapping should only disallow config updates, if decryption works
            // still reasonably accurate
                // TODO recheck fqdn->ip every 15 minutes if fail
            bool authenticate(string fqdn, string pass, string ip);
    };
}
#endif
