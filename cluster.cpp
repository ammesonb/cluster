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

#include "dbus_common.h"
#include "common.h"

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

namespace Cluster {
    int debug = 0;
    int handler_pid;
    pthread_t dbus_dispatcher;

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

int main(int argc, char *argv[]) {
   return 0;
}
