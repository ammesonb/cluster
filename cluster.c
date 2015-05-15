/*{{{ Includes */
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
#include "network.h"
/*}}}*/
/*{{{ Global variables */
// Config/*{{{*/
char* str_id = NULL;
cfg_t *cfg;
cfg_bool_t alert = cfg_false;
int port, debug, interval, dead;
char *email = NULL, *crit_files = NULL, *crit_dirs = NULL;
/*}}}*/

// DBus/*{{{*/
DBusConnection *conn;
int handler_pid = -1;
pthread_t dbus_dispatcher;/*}}}*/

// Network/*{{{*/
int num_hosts = 0;
int num_services = 0;
int cur_host = -1;
int char_count = -1;
/*}}}*/

// Events/*{{{*/
struct event_base *base;
struct event *accept_conn, *keepalive;
struct event client_events[MAX_HOSTS];
int BASE_INITED = 0;/*}}}*/

// Introspection for DBus/*{{{*/
char *introspec_xml = 
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
/*}}}*/
/*{{{ Macros */
#define   DIE(str) fprintf(stderr, "%s\n", str); exit(1);
/*}}}*/
/*{{{ Function templates*/
void quit(int);
char* create_str(int);
char* read_file(char*);
void configure_socket(int);
void send_keepalive(int, short, void*);
void recv_data(int, short, void*);
void load_hosts(char*);
void load_services(char*);
void dbus_loop(void*);
int init_dbus();
void register_event_base();
void register_host_events();
void accept_connection(int, short, void*);
void connect_to_host(int);
void connection_timeout(int, short, void*);
int update_host_state(int, int);
/*}}}*/

void quit(int sig) {/*{{{*/
    PRINTD(1, "Received exit signal! Cleaning up.");
    // Clean up
    free(str_id);
    free(ping_msg);
    free(addresses);
    free(passphrases);
    free(services);
    int i;
    for (i = 0; i <= num_services; i++) free(service_hosts[i]);
    free(service_hosts);
    event_base_loopexit(base, NULL);
    event_base_free(base);
    dbus_connection_unref(conn);

    exit(0);
}/*}}}*/

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

void dbus_loop(void* args) {/*{{{*/
    PRINTD(1, "Entering main DBus loop");
    while (dbus_connection_read_write_dispatch(conn, -1));
}/*}}}*/

int init_dbus() {/*{{{*/
    PRINTD(2, "Initializing DBus");
    DBUS_INIT(DBUS_NAME, DBUS_PATH, dbus_handler)
    PRINTD(2, "Dispatching DBus thread");
    pthread_create(&dbus_dispatcher, NULL, (void*)dbus_loop, NULL);
    return EXIT_SUCCESS;
}/*}}}*/

void register_event_base() {/*{{{*/
    PRINTD(2, "Registering base events");
    // Register all events
    if (BASE_INITED) event_base_loopexit(base, NULL);
    base = event_base_new();

    accept_conn = event_new(base, accept_fd, EV_READ|EV_PERSIST, accept_connection, NULL);
    event_add(accept_conn, NULL);

    keepalive = event_new(base, -1, EV_PERSIST, send_keepalive, NULL);
    struct timeval keepalive_tv;
    keepalive_tv.tv_sec = interval;
    keepalive_tv.tv_usec = 0;
    event_add(keepalive, &keepalive_tv);

    BASE_INITED = 1;

    register_host_events();
    PRINTD(1, "Entering events loop");
    event_base_dispatch(base);
}/*}}}*/

void register_host_events() {/*{{{*/
    PRINTD(2, "Registering host events");
    // Register events for all hosts
    int h;
    for (h = 0; h < num_hosts; h++) {
        if (h == id || !status[h]) {continue;}
        struct timeout_args args;
        args.host = h;
        struct event *host_event = event_new(base, sockets[h], EV_READ|EV_PERSIST, recv_data, &args);
        struct event *host_time_event = event_new(base, -1, EV_READ|EV_PERSIST, connection_timeout, &args);
        struct timeval host_timeout;
        host_timeout.tv_sec = dead;
        host_timeout.tv_usec = 0;
        event_add(host_event, NULL);
        event_add(host_time_event, &host_timeout);
    }
}/*}}}*/

int main(int argc, char *argv[]) {/*{{{*/
    PRINTD(1, "Loading cluster configuration");
    if (access("cluster.conf", R_OK)) {
        DIE("Couldn't read cluster configuration file");
    } else if (access("hosts", R_OK)) {
        DIE("Couldn't read hosts configuration file");
    } else if (access("services", R_OK)) {
        DIE("Couldn't read services configuration file");
    }
    // Load configuration/*{{{*/
    cfg_opt_t config[] = {
        CFG_SIMPLE_INT("port", &port),
        CFG_SIMPLE_INT("id", &id),
        CFG_SIMPLE_INT("beat_interval", &interval),
        CFG_SIMPLE_INT("dead_time", &dead),
        CFG_SIMPLE_STR("email", &email),
        CFG_SIMPLE_BOOL("alert", &alert),
        CFG_SIMPLE_INT("verbosity", &debug),
        CFG_SIMPLE_STR("critical_files", &crit_files),
        CFG_SIMPLE_STR("critical_dirs", &crit_dirs),
        CFG_END()
    };

    cfg = cfg_init(config, 0);
    cfg_parse(cfg, "cluster.conf");
    ping_msg = create_str(9);
    sprintf(ping_msg, "%d-ping", id);/*}}}*/

    // Load secondary config files (hosts, services, etc)/*{{{*/
    PRINTD(2, "Loading secondary configuration files");
    PRINTD(3, "Loading hosts")
    char *hosts = read_file("hosts");
    load_hosts(hosts);

    PRINTD(3, "Loading services");
    char *service_data = read_file("services");
    load_services(service_data);/*}}}*/

    // Register signals/*{{{*/
    PRINTD(2, "Registering signal handlers");
    if (signal(SIGINT, quit) == SIG_ERR) {
        fprintf(stderr, "Can't catch SIGINT");
        quit(0);
    }
    if (signal(SIGQUIT, quit) == SIG_ERR) {
        fprintf(stderr, "Can't catch SIGQUIT");
        quit(0);
    }
    if (signal(SIGTERM, quit) == SIG_ERR) {
        fprintf(stderr, "Can't catch SIGTERM");
        quit(0);
    }
    if (signal(SIGPIPE, write_err) == SIG_ERR) {
        fprintf(stderr, "Can't catch SIGPIPE");
        quit(0);
    }/*}}}*/

    // Register DBus handlers/*{{{*/
    PRINTD(2, "Registering DBus functions");
    int failed = init_dbus();
    if (failed) {
        DIE("Couldn't establish DBus connection");
    }
/*}}}*/

    // Launch python script to handle extraneous requests such as file transfers/*{{{*/
    PRINTD(2, "Forking python DBus handler");
    int is_parent = fork();
    if (!is_parent) {
        char *args[] = {};
        args[0] = "python";
        args[1] = "handler.py";
        args[2] = NULL;
        int val = execvp(args[0], &args[0]);
        if (val) {
            fprintf(stderr, "Failed to launch python script: %s!\n", strerror(errno));
            exit(1);
        }
    }/*}}}*/

    // Bind socket and listen/*{{{*/
    PRINTD(2, "Creating listening socket");
    accept_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (accept_fd < 0) {
        DIE("Failed to create bound socket");
    }
    PRINTD(3, "Configuring listening ocket");
    configure_socket(accept_fd);
    struct sockaddr_in addr_in;
    memset(&addr_in, '\0', sizeof(addr_in));
    addr_in.sin_family = AF_INET;
    addr_in.sin_addr.s_addr = INADDR_ANY;
    addr_in.sin_port = htons(port);

    PRINTD(3, "Binding listening socket");
    int out = bind(accept_fd, (struct sockaddr*)&addr_in, sizeof(addr_in));
    if (out < 0) {
        DIE("Failed to bind socket");
    }

    out = listen(accept_fd, 10);
    if (out < 0) {
        DIE("Failed to listen on socket");
    }/*}}}*/

    // Attempt to connect to all other hosts/*{{{*/
    int i;
    PRINTD(1, "Attempting to establish connection with other hosts");
    for (i = 0; i < num_hosts; i++) {
        if (i == id) continue;
        PRINTD(2, "Attempting to connect to host %d at %s", i, addresses[i]);
        if (strcmp(addresses[i], "dyn") != 0) connect_to_host(i);
    }/*}}}*/

    // Register event handlers for bind and any connected hosts
    PRINTD(2, "Registering events");
    register_event_base();
    PRINTD(1, "Main events loop broken! Exiting.");

    quit(0);

    return 0;
}/*}}}*/
