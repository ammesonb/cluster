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
/*}}}*/
/*{{{ Macros */
#define   DIE(str) fprintf(stderr, "%s\n", str); exit(1);
#define   PRINTD(level, str) if (debug >= level) printf("DEBUG%d: %s\n", level, str);
/*}}}*/
/*{{{ Constants */
#define   MAX_HOSTS   100
#define   DBUSPATH    "/com/bammeson/cluster"
#define   DBUSNAME    "com.bammeson.cluster"
/*}}}*/
/*{{{ Global variables */
// Config
cfg_t *cfg;
cfg_bool_t alert = cfg_false;
int port, debug, interval, dead;
char *email = NULL, *crit_files = NULL, *crit_dirs = NULL;

// Network
int accept_fd;
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

void quit() {/*{{{*/
    // Clean up
    exit(0);
}/*}}}*/

char* create_str(int length) {/*{{{*/
    char *s = (char*)malloc(sizeof(char) * (length + 1));
    memset(s, '\0', sizeof(s));
    return s;
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

void register_host_events() {/*{{{*/
    // Register events for a new host
}/*}}}*/

void register_event_base() {/*{{{*/
    // Register all events
}/*}}}*/

void accept_connection(int fd) {/*{{{*/
    // Establish new connection
}/*}}}*/

void connect_to_host(int host) {/*{{{*/
    // Attempt to connect to a given host and register events for it
}/*}}}*/

void update_host_state(int host, int state) {/*{{{*/
    // Update host status and dispatch necessary signals
}/*}}}*/

int main(int argc, char *argv[]) {/*{{{*/
    // Load configuration

    // Load secondary config files (hosts, services, etc)
    // Register DBus handlers

    // Launch python script to handle extraneous requests such as file transfers

    // Bind socket and listen

    // Attempt to connect to all other hosts

    // Register event handlers for bind and any connected hosts

    return 0;
}/*}}}*/
