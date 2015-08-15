#ifndef CLUSTER_SERVICE_H
#define CLUSTER_SERVICE_H

#include <string>
#include <vector>

#include "host.h"

using std::string;
using std::vector;

namespace Cluster {
    class Host;

    class Service {
        public:
            int id;
            string name;
            vector<Host> hosts;

            void start_stop(int hostid, bool online);
            void start();
            void stop();
    };
}
#endif
