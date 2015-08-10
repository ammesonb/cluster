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
        protected:
            string password;
    };
}
#endif