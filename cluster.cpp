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

#include "dbus_common.h"
#include "common.h"
#include "host.h"
#include "service.h"

#define   DIE(str) fprintf(stderr, "%s\n", str); exit(1);
#define   PRINTD(level, d_str, args...) if (debug >= level) printf("DEBUG%d: ", level); printf(d_str, ##args); printf("\n");

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

using std::string;
using std::cerr;
using std::endl;

namespace Cluster {
    int handler_pid;
    pthread_t dbus_dispatcher;
    DBusConnection *conn;

    // Config variables/*{{{*/
    cfg_t *cfg;
    cfg_bool_t text = cfg_false;
    int PORT, debug, interval, dead;
    char *email = NULL, *crit_files = NULL, *crit_dirs = NULL;/*}}}*/


    DBUS_FUNC(dbus_handler) {/*{{{*/
        int handled = 0;
        const char *iface = dbus_message_get_interface(dbmsg);
        PRINTD(3, "Received DBus message on %s", iface);
        if (!strcmp(iface, DBUS_INTERFACE_INTROSPECTABLE)) {
            DBUS_INTROSPEC
            handled = 1;
        } else if (!strcmp(iface, DBUS_NAME)) {
            if (dbus_message_is_method_call(dbmsg, DBUS_NAME, "updateHandlerPID")) {
                DBUS_GET_ARGS(DBUS_TYPE_INT32, &handler_pid);
                PRINTD(3, "Handler pid set to %d", handler_pid);
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
        PRINTD(1, "Entering main DBus loop");
        while (dbus_connection_read_write_dispatch(conn, -1));
        return NULL;
    }/*}}}*/

    int init_dbus() {/*{{{*/
        PRINTD(2, "Initializing DBus");
        DBUS_INIT(DBUS_NAME, DBUS_PATH, dbus_handler)
        PRINTD(2, "Dispatching DBus thread");
        pthread_create(&dbus_dispatcher, NULL, dbus_loop, NULL);
        return EXIT_SUCCESS;
    }/*}}}*/
}

using namespace Cluster;

int main(int argc, char *argv[]) {
    PRINTD(2, "Loading config");
    // Load configuration/*{{{*/
    cfg_opt_t config[] = {
        CFG_SIMPLE_INT(STRLITFIX("port"), &PORT),
        CFG_SIMPLE_INT(STRLITFIX("beat_interval"), &interval),
        CFG_SIMPLE_INT(STRLITFIX("dead_time"), &dead),
        CFG_SIMPLE_STR(STRLITFIX("email"), &email),
        CFG_SIMPLE_BOOL(STRLITFIX("text_alerts"), &text),
        CFG_SIMPLE_INT(STRLITFIX("verbosity"), &debug),
        CFG_SIMPLE_STR(STRLITFIX("critical_files"), &crit_files),
        CFG_SIMPLE_STR(STRLITFIX("critical_dirs"), &crit_dirs),
        CFG_END()
    };

    cfg = cfg_init(config, 0);
    cfg_parse(cfg, "cluster.conf");/*}}}*/

    PRINTD(3, "Opening DBus");
    // Open DBus connection/*{{{*/
    DBusError dberror;

    dbus_error_init(&dberror);
    conn = dbus_bus_get(DBUS_BUS_SESSION, &dberror);
    if (!conn || conn == NULL) {
        printf("Connection to D-BUS daemon failed: %s", dberror.message);

        dbus_error_free(&dberror);
        return 1;
    }
    dbus_error_free(&dberror);/*}}}*/

    PRINTD(3, "Determining my ID");
    // Read in machine number and set ping message/*{{{*/
    string my_id = read_file(STRLITFIX("/var/opt/cluster/id"));
    string ping_msg;
    ping_msg.reserve(my_id.length() + 5);
    ping_msg.append(my_id).append("-ping");

    if (ping_msg.length() < 6) {cerr <<  "Failed to parse id" << endl; exit(1);}
    /*}}}*/

    
   return 0;
}
