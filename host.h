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
            bool dynamic;
            vector<Service> services;
            string password;

            // TODO since identifier is decrypted, check for empty string for failure case
            // TODO failed fqdn->ip mapping should only disallow config updates, if decryption works
            // still reasonably accurate
                // TODO recheck identifier->ip every 15 minutes if fail
            bool authenticate(int id, string identifier, string ip);

            bool operator==(const Host &other) {
                return this->address == other.address && this->port == other.port
                    && this->password == other.password;
            }
    };
}
#endif
