#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <dbus/dbus.h>
#include <event.h>
#include <confuse.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string>
#include <iostream>
#include <vector>
#include <map>

#include "dbus_common.h"
#include "common.h"
#include "host.h"
#include "service.h"
#include "network.h"

 // Introspection for DBus/*{{{*/
 const char *introspec_xml =
 "<!DOCTYPE node PUBLIC \"-//freedesktop//DTD D-BUS Object Introspection 1.0//EN\"\n"
 "\"http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd\">\n"
 "<node name=\"/com/bammeson/cluster\">\n"
 "  <interface name=\"org.freedesktop.DBus.Introspectable\">\n"
 "    <method name=\"Introspect\">\n"
 "      <arg name=\"data\" direction=\"out\" type=\"s\"/>\n"
 "    </method>\n"
 "  </interface>\n"
 "  <interface name=\"com.bammeson.cluster\">\n"
 "    <method name=\"updateHandlerPID\">\n"
 "      <arg type=\"i\" direction=\"in\"/>\n"
 "      <arg type=\"b\" direction=\"out\"/>\n"
 "    </method>\n"
 "  </interface>\n"
 "</node>\n";/*}}}*/

using std::vector;
using std::map;
using std::string;
using std::cout;
using std::cerr;
using std::endl;

namespace Cluster {
    int handler_pid;
    pthread_t dbus_dispatcher;
    DBusConnection *conn;
    string ping_msg;

    bool keep_running = true;

    // Config variables/*{{{*/
    cfg_t *cfg;
    cfg_bool_t text = cfg_false;
    int debug = 2, interval, dead;
    char *email = NULL, *crit_files = NULL, *crit_dirs = NULL;/*}}}*/

    map<int, Host> host_list;
    map<int, Service> serv_list;
    vector<Host> hosts_online;
    map<int, vector<string>> send_message_queue;

    DBUS_FUNC(dbus_handler) {/*{{{*/
        int handled = 0;
        const char *iface = dbus_message_get_interface(dbmsg);
        PRINTD(3, 0, "Received DBus message on %s", iface);
        if (!strcmp(iface, DBUS_INTERFACE_INTROSPECTABLE)) {
            DBUS_INTROSPEC
            handled = 1;
        } else if (!strcmp(iface, DBUS_NAME)) {
            if (dbus_message_is_method_call(dbmsg, DBUS_NAME, "updateHandlerPID")) {
                DBUS_GET_ARGS(DBUS_TYPE_INT32, &handler_pid);
                PRINTD(3, 0, "Handler pid set to %d", handler_pid);
                int ret = 1;
                DBUS_REPLY_INIT
                DBUS_ADD_ARGS(db_reply_msg)
                DBUS_ADD_BOOL(&ret)
                DBUS_REPLY_SEND(db_reply_msg)
                handled = 1;
            }
        }

        return handled ? DBUS_HANDLER_RESULT_HANDLED : DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }/*}}}*/

    void* dbus_loop(void* args) {/*{{{*/
        PRINTD(1, 0, "Entering main DBus loop");
        while (dbus_connection_read_write_dispatch(conn, -1));
        return NULL;
    }/*}}}*/

    int init_dbus() {/*{{{*/
        PRINTD(2, 0, "Initializing DBus");
        DBUS_INIT(DBUS_NAME, DBUS_PATH, dbus_handler)
        PRINTD(2, 1, "Dispatching DBus thread");
        pthread_create(&dbus_dispatcher, NULL, dbus_loop, NULL);
        return EXIT_SUCCESS;
    }/*}}}*/
}

using namespace Cluster;

int main(int argc, char *argv[]) {
    PRINTD(1, 0, "Loading config file");
    // Load configuration/*{{{*/
    cfg_opt_t config[] = {
        CFG_SIMPLE_INT("beat_interval", &interval),
        CFG_SIMPLE_INT("dead_time", &dead),
        CFG_SIMPLE_STR("email", &email),
        CFG_SIMPLE_BOOL("text_alerts", &text),
        CFG_SIMPLE_INT("verbosity", &debug),
        CFG_SIMPLE_STR("critical_files", &crit_files),
        CFG_SIMPLE_STR("critical_dirs", &crit_dirs),
        CFG_END()
    };

    cfg = cfg_init(config, 0);
    cfg_parse(cfg, "cluster.conf");

    PRINTD(1, 1, "Found debug level %d", debug);
    PRINTD(3, 1, "Calculating hashes");
    // TODO this should probably be made dynamic after parsing critical files/dirs
    string main_conf_md = hash_file("cluster.conf");
    PRINTD(4, 2, "Main configuration hash: %s", main_conf_md.c_str());
    string host_conf_md = hash_file("hosts");
    PRINTD(4, 2, "Host configuration hash: %s", host_conf_md.c_str());
    string serv_conf_md = hash_file("services");
    PRINTD(4, 2, "Service configuration hash: %s", serv_conf_md.c_str());
    
    PRINTD(3, 1, "Loading hosts");
    if (!validate_host_config()) {DIE("Found invalid host configuration file!");}
    load_host_config(); 

    PRINTD(3, 1, "Loading services");
    if (!validate_service_config()) {DIE("Found invalid service configuration file!");}
    load_service_config();/*}}}*/

    PRINTD(2, 0, "Initializing session");
    PRINTD(3, 1, "Opening DBus");
    // Open DBus connection/*{{{*/
    DBusError dberror;

    dbus_error_init(&dberror);
    conn = dbus_bus_get(DBUS_BUS_SESSION, &dberror);
    if (!conn || conn == NULL) {
        DIE("Connection to D-BUS daemon failed: %s", dberror.message);
    }
    dbus_error_free(&dberror);/*}}}*/

    PRINTD(3, 1, "Determining my ID");/*{{{*/
    // Read in machine number and set ping message
    string my_id = read_file(STRLITFIX("/var/opt/cluster/id"));
    my_id.assign(trim(my_id));
    int int_id = stoi(my_id);
    int port = host_list[int_id].port;
    PRINTDI(3, "My ID is %s", my_id.c_str());
    ping_msg.reserve(my_id.length() + 5);
    ping_msg.append(my_id).append("-ping");
    if (ping_msg.length() < 6) {cerr <<  "Failed to parse id" << endl; exit(1);}/*}}}*/

    PRINTD(2, 0, "Starting networking services");
    bool online = verify_connectivity();
    if (online) {
        PRINTD(3, 1, "I am online");
    } else {
        DIE("I am not online");
    }

    start_accept_thread(port);

    PRINTD(3, 1, "Attempting to connect to all hosts");
    for (auto it = host_list.begin(); it != host_list.end(); it++) {
        Host h = (*it).second;
        if (h.id == int_id) continue;
        PRINTD(3, 2, "Connecting to host %s", h.address.c_str());
        connect_to_host(h);
    }

    // TODO need some sort of main loop
    // TODO sender will be individual per connection, so dispatch keepalive messages
    // to those senders here
    // TODO check file time stamps to ensure no changes
    // TODO mark hosts as offline if timed out
    
    return 0;
}
