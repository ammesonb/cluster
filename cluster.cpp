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
    int port, debug = 2, interval, dead;
    char *email = NULL, *crit_files = NULL, *crit_dirs = NULL;/*}}}*/

    map<int, Host> host_list;
    map<int, Service> serv_list;

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
        CFG_SIMPLE_INT("port", &port),
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
    cfg_parse(cfg, "cluster.conf");/*}}}*/

    PRINTD(1, 1, "Found debug level %d", debug);
    PRINTD(3, 1, "Loading hosts");/*{{{*/
    string hosts = read_file(STRLITFIX("hosts"));
    start_split(hosts, "\n");
    string ho = get_split();
    int host_num = 0;
    while (ho.length() > 0) {
        Host host;
        start_split(ho, " ");
        string hostname = get_split();
        if (hostname.length() == 0) {DIE("Bad formatting for host on line %d, requires hostname", host_num);}
        PRINTD(3, 2, "Parsing host %s", hostname.c_str());
        host.address = hostname;
        host.id = host_num;
        string host_port = get_split();
        if (host_port.length() == 0) {DIE("Bad formatting for host on line %d, requires port", host_num);}
        host.port = stoi(host_port);
        PRINTD(4, 3, "Host is on port %d", host.port);
        bool dyn = false;
        if (is_ip(hostname)) {dyn = true; PRINTDI(3, "Host is dynamic");}
        host.dynamic = dyn;
        if (dyn) {
            string host_pass = get_split();
            if (host_pass.length() == 0) {DIE("Bad formatting for host on line %d, dynamic host requires password for validation", host_num);}
        }
        if (get_split().length() != 0) {DIE("Bad formatting for host on line %d, extra data found", host_num);}
        end_split(1);
        ho = get_split();
        host_list[host_num] = host;
        host_num++;
    }/*}}}*/

    PRINTD(3, 1, "Loading services");/*{{{*/
    string services = read_file(STRLITFIX("services"));
    start_split(services, "\n");
    string serv = get_split();
    int serv_num = 0;
    while (serv.length() > 0) {
        Service service;
        vector<Host> serv_hosts;
        start_split(serv, " ");
        string servname = get_split();
        if (servname.length() == 0) {DIE("Bad formatting for service on line %d, requires name", serv_num);}
        PRINTD(3, 2, "Parsing service %s", servname.c_str());
        string host1 = get_split();
        if (host1.length() == 0) {DIE("Bad formatting for service on line %d, requires at least one host", serv_num);}
        PRINTD(5, 3, "Host %s is subscribed", host_list[stoi(host1)].address.c_str());
        serv_hosts.push_back(host_list[stoi(host1)]);
        int hosts_found = 1;
        while (get_split_level() == 1) {
            string host = get_split();
            if (hosts_found == 1 && host.length() == 0) PRINTD(2, 3, "Service %s only has one host! Consider adding another for redundancy.", servname.c_str());
            if (host.length() == 0) break;
            PRINTDI(5, "Host %s is subscribed", host_list[stoi(host)].address.c_str());
            serv_hosts.push_back(host_list[stoi(host)]);
            hosts_found++;
        }
        PRINTD(4, 3, "Found %d hosts", hosts_found);
        end_split(1);
        serv = get_split();
        service.hosts = serv_hosts;
        serv_list[serv_num] = service;
        serv_num++;
    }/*}}}*/

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
        PRINTD(3, 2, "Connecting to host %s", h.address.c_str());
        connect_to_host(h);
    }

    // TODO need some sort of main loop
    // TODO maybe have this as my keepalive loop?
    // TODO work in a way to do the file updates - checksum in keepalive loop?
    // TODO or file time stamps maybe?
    
    return 0;
}
