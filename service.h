#include <string>
#include <vector>

using std::string;
using std::vector;

namespace Cluster {
    class Service {
        public:
            string name;
            vector<Host> hosts;
    }
}
