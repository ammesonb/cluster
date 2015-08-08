#include <string>
#include <vector>

using std::string;
using std::vector;

namespace Cluster {
    class Host {
        public:
            string address;
            int socket;
            unsigned long long last_msg; 
            bool online;
            bool dynamic;
            vector<Service> services;
        private:
            string password;
    }
}
