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

            bool validate(string fqdn, string pass, string ip);
    };
}
#endif
