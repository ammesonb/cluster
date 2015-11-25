#include "service.h"

#include <algorithm>

#include "common.h"

namespace Cluster {
    // Online is true if a host coming online prompted the function,
    // false if a host dropped offline
    void Service::start_stop(int hostid, bool online) {/*{{{*/
        bool running = (std::find(VECTORFIND(running_services, id)) != running_services.end());
        // If this service is running and a host dropped offline, do nothing
        if (running && !online) return;
        // If the service is not running and a host came online, do nothing
        else if (!running && online) return;

        // If the service is running and another host comes online, check if they have priority
        else if (running && online) {
            ITERVECTOR(hosts, it) {
                // If the first element found is me, then I should keep running the service
                if ((*it).id == int_id) return;
                else if ((*it).id == hostid) {stop(); return;}
            }
        }

        // If the service is not running and a host drops offline, check if we have priority
        else if (!running && !online) {
            // Essentially, just need to find the first online host in the list, or maybe it's me
            ITERVECTOR(hosts, it) {
                if ((*it).id == int_id) {start(); return;}
                if (host_list[(*it).id].online) return;
            }
        }


    }/*}}}*/

    void Service::start() {/*{{{*/
        // TODO write me
        PRINTD(2, 0, "Starting service %s", name.c_str());
    }/*}}}*/

    void Service::stop() {/*{{{*/
        // TODO write me
        PRINTD(2, 0, "Stopping service %s", name.c_str());
    }/*}}}*/
}
