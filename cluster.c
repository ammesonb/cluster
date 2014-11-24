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
#include <sys/socket.h>
#include <event.h>
#include <confuse.h>
#include <sys/stat.h>
#include <sys/types.h>
/*}}}*/
/*{{{ Constants */
#define   MAX_HOSTS    100
#define   MAX_SERVICES 50
#define   DBUSPATH     "/com/bammeson/cluster"
#define   DBUSNAME     "com.bammeson.cluster"
/*}}}*/
/*{{{ Global variables */
// Config
char* str_id = NULL;
int id = -1, am_dynamic = 0;
cfg_t *cfg;
cfg_bool_t alert = cfg_false;
int port, debug, interval, dead;
char *email = NULL, *crit_files = NULL, *crit_dirs = NULL;
int MIN_PASS_LENGTH = 8;

// DBus
char *DBUS_PATH = "/com/bammeson/cluster/";
char *DBUS_NAME = "com.bammeson.cluster";
DBusConnection *dbus;

// Network
int accept_fd;
char **addresses;
char **passphrases;
int sockets[MAX_HOSTS];
int status[MAX_HOSTS];
int num_hosts = 0;
char **services;
int **service_hosts;
int num_services = 0;
char *ping_msg;
char *my_pass;

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

void quit(int sig) {/*{{{*/
    // Clean up
    free(str_id);
    free(addresses);
    free(passphrases);
    free(services);
    int i;
    for (i = 0; i < num_services; i++) free(service_hosts[i]);
    free(service_hosts);

    exit(0);
}/*}}}*/

char* create_str(int length) {/*{{{*/
    char *s = (char*)malloc(sizeof(char) * (length + 1));
    memset(s, '\0', sizeof(&s));
    return s;
}/*}}}*/

char* read_file(char *name) {/*{{{*/
    struct stat *s = NULL;
    s = (struct stat*)malloc(sizeof(struct stat));
    stat(name, s);
    int file = open(name, O_RDONLY);
    char *data = create_str(s->st_size);
    read(file, data, s->st_size);
    close(file);
    free(s);
    return data;
}/*}}}*/

void configure_socket(int sockfd) {/*{{{*/
    // Set various socket options for address reuse, timeout, etc
    int flags = fcntl(sockfd, F_GETFL);
    flags |= O_NONBLOCK;
    fcntl(sockfd, F_SETFL, flags);

    int value = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &value, sizeof(value)) == -1) {
        fprintf(stderr, "Failed to set option");
        exit(1);
    }

    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, &value, sizeof(value)) == -1) {
        fprintf(stderr, "Failed to set option");
        exit(1);
    }

    struct linger so_linger;
    so_linger.l_onoff = 1;
    so_linger.l_linger = 0;
    if (setsockopt(sockfd, SOL_SOCKET, SO_LINGER, &so_linger, sizeof(so_linger)) == -1) {
        fprintf(stderr, "Failed to set option");
        exit(1);
    }

    struct timeval timeout;
    timeout.tv_sec = 2;
    timeout.tv_usec = 0;
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout)) == -1) {
        fprintf(stderr, "Failed to set option");
        exit(1);
    }
    if (setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeout, sizeof(timeout)) == -1) {
        fprintf(stderr, "Failed to set option");
        exit(1);
    }

}/*}}}*/

void send_keepalive(int host, short ev, void* arg) {/*{{{*/
    // Send a keepalive packet to a given host
}/*}}}*/

void recv_data(int host) {/*{{{*/
    // Read and parse data from host
}/*}}}*/

void load_hosts(char *hosts) {/*{{{*/
    addresses = (char**)malloc(sizeof(char*) * MAX_HOSTS);
    passphrases = (char**)malloc(sizeof(char*) * MAX_HOSTS);
    char *host = strtok(hosts, "\n");
    int i = 0;
    while (host != NULL) {
        // If host is not dynamic
        if (strstr(host, ".") != NULL) {
            addresses[i] = host;
        } else {
            if (i == id) am_dynamic = 1;
            char *addr = strtok(host, "___");
            char *pass = strtok(host, "___");
            if (strlen(pass) < MIN_PASS_LENGTH) {
                char *err = create_str(500);
                sprintf(err, "Passphrase on line %d must be at least %d characters!\n", i, MIN_PASS_LENGTH);
                fprintf(stderr, "%s", err);
                free(err);
                quit(0);
            }
            addresses[i] = addr;
            passphrases[i] = pass;
        }
        i++;
        host = strtok(NULL, "\n");
    }
    num_hosts = i;
}/*}}}*/

void load_services(char *service_data) {/*{{{*/
    services = (char**)malloc(sizeof(char*) * MAX_SERVICES);
    service_hosts = (int**)malloc(sizeof(int*) * MAX_SERVICES * MAX_HOSTS);
    char *service = strtok(service_data, "\n");
    int i = 0;
    while (service != NULL) {
        char *service_name = strtok(service, " ");
        services[i] = service_name;

        int j = 0;
        char *service_host = strtok(NULL, " ");
        while (service_host != NULL) {
            int h = atoi(service_host);
            if (j == 0) {
                service_hosts[i] = (int*)malloc(sizeof(int) * num_hosts);
            }
            service_hosts[i][j] = h;
            j++;
            service_host = strtok(NULL, " ");
        }
        i++;
        service = strtok(NULL, "\n");
    }
}/*}}}*/

void init_dbus() {/*{{{*/
    DBusError dberr;
    dbus_error_init(&dberr);
    dbus = dbus_bus_get(DBUS_BUS_SESSION, &dberr);
    if (!dbus || dbus == NULL) {
        fprintf(stderr, "Failed to connect to DBus: %s", dberr.message);
        dbus_error_free(&dberr);
        quit(0);
    } 

    int owner = dbus_bus_request_name(dbus, DBUS_NAME, DBUS_NAME_FLAG_REPLACE_EXISTING, &dberr);
    if (dbus_error_is_set(&dberr)) {
        fprintf(stderr, "DBus Name Error (%s)\n", dberr.message);
        dbus_error_free(&dberr);
        quit(0);
    } 

    if (DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER != owner) {
        fprintf(stderr, "Not Primary Owner (%d)\n", owner);
        quit(0);
    }
    dbus_error_free(&dberr);
}/*}}}*/

void load_dbus_functions() {/*{{{*/
}/*}}}*/

void accept_connection(int, short, void*);
void register_host_events();
void register_event_base() {/*{{{*/
    // Register all events
    if (BASE_INITED) event_base_loopexit(base, NULL);
    base = event_base_new();

    accept_conn = event_new(base, accept_fd, EV_READ|EV_PERSIST, accept_connection, NULL);
    event_add(accept_conn, NULL);

    keepalive = event_new(base, -1, EV_PERSIST, send_keepalive, NULL);
    struct timeval keepalive_tv;
    keepalive_tv.tv_sec = 5;
    keepalive_tv.tv_usec = 0;
    event_add(keepalive, &keepalive_tv);

    BASE_INITED = 1;

    register_host_events();
    event_base_dispatch(base);
}/*}}}*/

void register_host_events() {/*{{{*/
    // Register events for a new host
}/*}}}*/

void accept_connection(int fd, short ev, void *arg) {/*{{{*/
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
    cfg_parse(cfg, "cluster.conf");/*}}}*/

    PRINTD(3, "Loading hosts")/*{{{*/
    // Load secondary config files (hosts, services, etc)
    char *hosts = read_file("hosts");
    load_hosts(hosts);

    PRINTD(3, "Loading services");
    char *service_data = read_file("services");
    load_services(service_data);/*}}}*/

    // Register signals/*{{{*/
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
    }/*}}}*/

    // Register DBus handlers/*{{{*/
    PRINTD(3, "Registering DBus functions")
    init_dbus();
    load_dbus_functions();
    // Will dbus connection work as socket for event callback?
/*}}}*/

    // Launch python script to handle extraneous requests such as file transfers/*{{{*/
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
    PRINTD(3, "Creating server socket");
    accept_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (accept_fd < 0) {
        DIE("Failed to create bound socket");
    }
    configure_socket(accept_fd);
    struct sockaddr_in addr_in;
    memset(&addr_in, '\0', sizeof(addr_in));
    addr_in.sin_family = AF_INET;
    addr_in.sin_addr.s_addr = INADDR_ANY;
    addr_in.sin_port = htons(port);

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
    for (i = 0; i < num_hosts; i++) {
        printf("Attempting to connect to host %d at %s\n", i, addresses[i]);
        if (strcmp(addresses[i], "dyn") != 0) connect_to_host(i);
    }/*}}}*/

    // Register event handlers for bind and any connected hosts/*{{{*/
    register_event_base();
    register_host_events();/*}}}*/

    return 0;
}/*}}}*/
