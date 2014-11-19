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
#include <socket.h>
#include <event.h>
#include <confuse.h>
#include <sys/stat.h>
#include <sys/types.h>
/*}}}*/
/*{{{ Constants */
#define   MAX_HOSTS   100
#define   DBUSPATH    "/com/bammeson/cluster"
#define   DBUSNAME    "com.bammeson.cluster"
/*}}}*/
/*{{{ Global variables */
// Config
char* str_id = NULL;
int id = -1;
cfg_t *cfg;
cfg_bool_t alert = cfg_false;
int port, debug, interval, dead;
char *email = NULL, *crit_files = NULL, *crit_dirs = NULL;

// DBus
char *DBUS_PATH = "/com/bammeson/cluster/"
char *DBUS_NAME = "com.bammeson.cluster"
DBusConnection *dbus;

// Network
int accept_fd;
char *addresses[MAX_HOSTS];
int sockets[MAX_HOSTS];
int status[MAX_HOSTS];
char *ping_msg;
int my_id;

// Events
struct event_base  *base;
struct event *accept_conn, *keepalive;
struct event client_events[MAX_HOSTS];
int BASE_INITED = 0;
/*}}}*/
/*{{{ Macros */
#define   DIE(str) fprintf(stderr, "%s\n", str); exit(1);
#define   PRINTD(level, str) if (debug >= level) printf("DEBUG%d: %s\n", level, str);
/*}}}*/

void quit() {/*{{{*/
    // Clean up
    free(str_id);
    exit(0);
}/*}}}*/

char* create_str(int length) {/*{{{*/
    char *s = (char*)malloc(sizeof(char) * (length + 1));
    memset(s, '\0', sizeof(s));
    return s;
}/*}}}*/

char* read_file(char *name) {/*{{{*/
    struct stat *s;
    stat(name, s);
    int file = open(name);
    char *data = create_str(s->st_size);
    read(file, data, s->st_size);
    close(file);
    return data;
}/*}}}*/

void configure_socket(int sockfd) {/*{{{*/
    // Set various socket options for address reuse, timeout, etc
}/*}}}*/

void send_keepalive(int host) {/*{{{*/
    // Send a keepalive packet to a given host
}/*}}}*/

void recv_data(int host) {/*{{{*/
    // Read and parse data from host
}/*}}}*/

void load_hosts(char *hosts) {/*{{{*/
}/*}}}*/

void load_services(char *services) {/*{{{*/
}/*}}}*/

void init_dbus() {/*{{{*/
    DBusError dberr;
    dbus_error_init(&dberr);
    dbus = dbus_bus_get(DBUS_BUS_SESSION, &dberr);
    if (!connection || dbus == NULL) {
        fprintf(stderr, "Failed to connect to DBus: %s", dberr.message);
        dbus_error_free(&dberr);
        quit();
    } 


    int owner = dbus_bus_request_name(dbus, DBUS_NAME, DBUS_NAME_FLAG_REPLACE_EXISTING, &dberr);
    if (dbus_error_is_set(&dberr)) {
        fprintf(stderr, "DBus Name Error (%s)\n", dberr.message);
        dbus_error_free(&dberr);
        quit();
    } 

    if (DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER != ret) {
        fprintf(stderr, "Not Primary Owner (%d)\n", owner);
        quit()
    }
    dbus_error_free(&dberr);
}/*}}}*/

void load_dbus_functions() {/*{{{*/
}/*}}}*/

void register_host_events() {/*{{{*/
    // Register events for a new host
}/*}}}*/

void register_event_base() {/*{{{*/
    // Register all events
}/*}}}*/

void accept_connection(int fd) {/*{{{*/
    /* Establish new connection
       Ensure sent ID within bounds and 
       IP being connected from matches hosts
       address/DNS name
    */
}/*}}}*/

void connect_to_host(int host) {/*{{{*/
    // Attempt to connect to a given host and register events for it
}/*}}}*/

void update_host_state(int host, int state) {/*{{{*/
    // Update host status and dispatch necessary signals
}/*}}}*/

int main(int argc, char *argv[]) {/*{{{*/
    // Load configuration/*{{{*/
    cfg_opt_t config[] = {
        CFG_SIMPLE_INT("port", &PORT),
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

    PRINTD(3, "Loading configuration")/*{{{*/
    // Determine my ID
    str_id = read_file("/var/opt/cluster/id");
    id = atoi(str_id);

    // Load secondary config files (hosts, services, etc)
    char *hosts = read_file("hosts");
    load_hosts(hosts);

    char *services = read_file("services");
    load_services(services);/*}}}*/

    // Register signals

    // Register DBus handlers/*{{{*/
    PRINTD(3, "Registering DBus functions")
    init_dbus();
    load_dbus_functions();
    // Will dbus connection work as socket for event callback?
/*}}}*/

    // Launch python script to handle extraneous requests such as file transfers

    // Bind socket and listen

    // Attempt to connect to all other hosts

    // Register event handlers for bind and any connected hosts

    return 0;
}/*}}}*/
