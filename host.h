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
            string address;
            int port;
            int socket;
            unsigned long long last_msg; 
            bool online;
            bool dynamic;
            vector<Service> services;
        private:
            string password;
    };
}
#endif
