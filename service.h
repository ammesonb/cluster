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
            string name;
            vector<Host> hosts;
    };
}
#endif
