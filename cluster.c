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
#include <pthread.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "dbus_common.h"
/*}}}*/
/*{{{ Constants */
#define   MAX_HOSTS    100
#define   MAX_SERVICES 50
#define   MAX_MSG_LEN  65535
#define   DBUSPATH     "/com/bammeson/cluster"
#define   DBUSNAME     "com.bammeson.cluster"
/*}}}*/
/*{{{ Global variables */
// Config/*{{{*/
char* str_id = NULL;
int id = -1, max_id = -1, am_dynamic = 0;
cfg_t *cfg;
cfg_bool_t alert = cfg_false;
int port, debug, interval, dead;
char *email = NULL, *crit_files = NULL, *crit_dirs = NULL;
int MIN_PASS_LENGTH = 8;/*}}}*/

// DBus/*{{{*/
char *DBUS_PATH = "/com/bammeson/cluster";
char *DBUS_NAME = "com.bammeson.cluster";
char *DBUS_HANDLER_PATH = "/com/bammeson/clusterHandler";
char *DBUS_HANDLER_NAME = "com.bammeson.clusterHandler";
DBusConnection *conn;
int handler_pid = -1;
pthread_t dbus_dispatcher;/*}}}*/

// Network/*{{{*/
int accept_fd;
char **addresses;
char **passphrases;
int sockets[MAX_HOSTS];
unsigned long long last_msg[MAX_HOSTS];
int status[MAX_HOSTS];
int dynamic[MAX_HOSTS];
int num_hosts = 0;
char **services;
int **service_hosts;
int num_services = 0;
char *ping_msg;
char *my_pass;/*}}}*/

// Events/*{{{*/
struct event_base  *base;
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
#define   PRINTD(level, str) if (debug >= level) printf("DEBUG%d: %s\n", level, str);
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

struct timeout_args {/*{{{*/
    int host;
};/*}}}*/

void quit(int sig) {/*{{{*/
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

char* create_str(int length) {/*{{{*/
    char *s = (char*)malloc(sizeof(char) * (length + 1));
    memset(s, '\0', length + 1);
    return s;
}/*}}}*/

unsigned long long get_cur_time() {/*{{{*/
    struct timeval cur_time;
    gettimeofday(&cur_time, NULL);
    unsigned long long cur = cur_time.tv_sec * 1000;
    cur += cur_time.tv_usec;
    return cur;
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
    int i;
    int length = strlen(ping_msg);
    PRINTD(3, "Sending keepalive to live clients");
    for (i = 0; i < num_hosts; i++) {
        if (!status[i]) {continue;}
        int out = write(sockets[i], ping_msg, length);
        if (out < 1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK ||
                errno == EINTR) {continue;}
            update_host_state(i, 0);
        }
    }
}/*}}}*/

void recv_data(int fd, short ev, void *arg) {/*{{{*/
    // Read and parse data from host
    struct timeout_args *args = (struct timeout_args*)arg;
    last_msg[args->host] = get_cur_time();
}/*}}}*/

void load_hosts(char *hosts) {/*{{{*/
    addresses = (char**)malloc(sizeof(char*) * MAX_HOSTS);
    passphrases = (char**)malloc(sizeof(char*) * MAX_HOSTS);
    char *host = strtok(hosts, "\n");
    int i = 0;
    while (host != NULL) {
        max_id++;
        // If host is not dynamic
        if (strstr(host, ".") != NULL) {
            addresses[i] = host;
            dynamic[i] = 0;
        } else {
            if (i == id) am_dynamic = 1;
            char *name = strtok(host, "___");
            char *pass = strtok(host, "___");
            dynamic[i] = 1;
            if (strlen(pass) < MIN_PASS_LENGTH) {
                char *err = create_str(500);
                sprintf(err, "Passphrase on line %d must be at least %d characters!\n", i, MIN_PASS_LENGTH);
                fprintf(stderr, "%s", err);
                free(err);
                quit(0);
            }
            addresses[i] = name;
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

DBUS_FUNC(dbus_handler) {/*{{{*/
    int handled = 0;
    const char *iface = dbus_message_get_interface(dbmsg);
    if (!strcmp(iface, DBUS_INTERFACE_INTROSPECTABLE)) {
        DBUS_INTROSPEC
        handled = 1;
    } else if (!strcmp(iface, DBUS_NAME)) {
        if (dbus_message_is_method_call(dbmsg, DBUS_NAME, "updateHandlerPID")) {
            DBUS_GET_ARGS(DBUS_TYPE_INT32, &handler_pid);
            char *s = create_str(100);
            sprintf(s, "Handler pid set to %d", handler_pid);
            PRINTD(3, s);
            free(s);
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
    while (dbus_connection_read_write_dispatch(conn, -1));
}/*}}}*/

int init_dbus() {/*{{{*/
    DBUS_INIT(DBUS_NAME, DBUS_PATH, dbus_handler)
    pthread_create(&dbus_dispatcher, NULL, (void*)dbus_loop, NULL);
    return EXIT_SUCCESS;
}/*}}}*/

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
    // Register events for all hosts
    int h;
    for (h = 0; h < num_hosts; h++) {
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

void accept_connection(int fd, short ev, void *arg) {/*{{{*/
    /* Establish new connection
       Ensure sent ID within bounds and 
       IP being connected from matches hosts
       address/DNS name
    */
    struct sockaddr client_addr;
    memset(&client_addr, '\0', sizeof(client_addr));
    socklen_t addr_size = sizeof(client_addr);
    int newfd = accept(accept_fd, &client_addr, &addr_size);
    if (newfd < 0) {
        fprintf(stderr, "Failed to establish connection: %s\n", strerror(errno));
        return;
    }

    // Set up socket and get IP address/DNS name/*{{{*/
    configure_socket(newfd);
    char *client_host = create_str(500);
    char *client_num_host = create_str(500);
    char *client_serv = create_str(500);
    getnameinfo(&client_addr, sizeof(client_addr), client_host, 500, client_serv, 500, 0);
    getnameinfo(&client_addr, sizeof(client_addr), client_num_host, 500, client_serv, 500, NI_NUMERICHOST);/*}}}*/

    // Authenticate host/*{{{*/
    char *buffer = create_str(MAX_MSG_LEN);
    memset(buffer, '\0', MAX_MSG_LEN + 1);
    read(newfd, buffer, MAX_MSG_LEN);
    int client_id = -1;
    // Check ID is within reasonable bounds/*{{{*/
    if (!strstr(buffer, "id:")) {
        buffer += 3;
        client_id = atoi(buffer);
        buffer -= 3;
        memset(buffer, '\0', 501);
        if (client_id > max_id) {
            char *r = "400 BAD REQUEST";
            write(newfd, r, strlen(r));
            fprintf(stderr, "ID sent (%d) was higher than allowed range!\n", client_id);
            return;
        } else {
            char *r = "200 CONTINUE";
            write(newfd, r, strlen(r));
        }/*}}}*/

        // Check ID matches connection address/*{{{*/
        if (!dynamic[client_id] && strcmp(client_host, addresses[client_id]) &&
                    strcmp(client_num_host, addresses[client_id])) {
                char *r = "403 FORBIDDEN";
                write(newfd, r, strlen(r));
                fprintf(stderr, "IP address host connected from (%s or %s) does not match config file\n",
                        client_host, client_num_host);
                return;/*}}}*/
        } else {
            // Check client sends matching name to ID/*{{{*/
            char *r = "200 CONTINUE";
            write(newfd, r, strlen(r));
            memset(buffer, '\0', 501);
            read(newfd, buffer, MAX_MSG_LEN);
            char *client_name = create_str(500);
            strcpy(client_name, buffer);
            if (strcmp(buffer, addresses[client_id])) {
                r = "404 NOT FOUND";
                write(newfd, r, strlen(r));
                fprintf(stderr, "Name %s does not exist\n", buffer);
                return;
            } else {
                write(newfd, r, strlen(r));
            }/*}}}*/

            // Verify password against name/*{{{*/
            memset(buffer, '\0', 501);
            read(newfd, buffer, 500);
            if (strcmp(buffer, passphrases[client_id])) {
                r = "401 UNAUTHORIZED";
                write(newfd, r, strlen(r));
                fprintf(stderr, "Host %s sent invalid passphrase\n", client_name);
                return;
            } else {
                write(newfd, r, strlen(r));
            }/*}}}*/

            free(client_name);
            free(buffer);
            free(client_serv);
            free(client_num_host);
            free(client_host);
        }
    }/*}}}*/

    update_host_state(client_id, 1);
}/*}}}*/

void connect_to_host(int host) {/*{{{*/
    // Attempt to connect to a given host and register events for it
    // Host cannot be dynamic
    if (!dynamic[host]) {
        char *s = create_str(250);
        sprintf(s, "Attempting to connect to host %s", addresses[host]);
        PRINTD(3, s);
        free(s);

        // Establish connection to host/*{{{*/
        struct addrinfo *h = (struct addrinfo*)malloc(sizeof(struct addrinfo) + 1);
        struct addrinfo *res, *rp;
        memset(h, '\0', sizeof(*h));
        h->ai_family = AF_INET;
        h->ai_socktype = SOCK_STREAM;
        h->ai_protocol = 0;
        char* port_str = create_str(6);
        sprintf(port_str, "%d", port);

        char *address = addresses[host];
        int r = getaddrinfo(address, port_str, h, &res);
        if (r) {
            fprintf(stderr, "Failed to get info about %s: %s\n", address, gai_strerror(r));
            return;
        }

        int newfd = -1;
        for (rp = res; rp != NULL; rp = rp->ai_next) {
            newfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
            if (newfd == -1) continue;
            if (connect(newfd, rp->ai_addr, rp->ai_addrlen) != -1)
                break;
        }
        if (rp == NULL) {
            if (debug) fprintf(stderr, "DEBUG1: Failed to connect to %s\n", address);
            return;
        }
        configure_socket(newfd);/*}}}*/

        // Send host ID for verification purposes/*{{{*/
        char *id_auth = create_str(10);
        char *response = create_str(100);
        sprintf(id_auth, "id:%d", id);
        write(newfd, id_auth, strlen(id_auth));
        read(newfd, response, MAX_MSG_LEN);
        if (!strcmp(response, "400 BAD REQUEST")) {
            fprintf(stderr, "ID %d does not exist!\n", id);
            return;
        } else if (!strcmp(response, "403 FORBIDDEN")) {
            fprintf(stderr, "Current address does not match config file for my ID!\n");
            return;
        }/*}}}*/

        // If I am dynamic, need to send name/pass authentication/*{{{*/
        if (am_dynamic) {
            char *name_str = create_str(100);
            char *pass_str = create_str(100);
            memset(&response, '\0', 101);
            sprintf(name_str, "name:%s", name_str);
            sprintf(pass_str, "pass:%s", pass_str);
            write(newfd, name_str, strlen(name_str) + 1);
            read(newfd, response, MAX_MSG_LEN);
            if (!strcmp(response, "404 NOT FOUND")) {
                fprintf(stderr, "Name rejected by %s!\n", address);
                return;
            }
            memset(&response, '\0', 101);
            write(newfd, pass_str, strlen(pass_str) + 1);
            read(newfd, response, MAX_MSG_LEN);
            if (!strcmp(response, "401 UNAUTHORIZED")) {
                fprintf(stderr, "Password rejected by %s!\n", address);
                return;
            }
            free(name_str);
            free(pass_str);
        }/*}}}*/
        free(response);

        update_host_state(host, 1);
        sockets[host] = newfd;
        free(port_str);
    }
}/*}}}*/

void connection_timeout(int fd, short ev, void *arg) {/*{{{*/
    struct timeout_args *args = (struct timeout_args*)arg;
    if (last_msg[args->host] + (dead * 1000) < get_cur_time()) {
        char *s = create_str(150);
        sprintf(s, "Host %d has not responded in %llu ms", args->host, get_cur_time() - last_msg[args->host]);
        PRINTD(2, s);
        free(s);
        update_host_state(args->host, 0);
    }
}/*}}}*/

int update_host_state(int host, int state) {/*{{{*/
    // Update host status and dispatch necessary signals
    if (status[host] == state) return 0;
    status[host] = state;
    char *func = state ? "hostOnline" : "hostOffline";
    DBUS_INIT_METHOD_CALL(DBUS_HANDLER_NAME, DBUS_HANDLER_PATH, DBUS_HANDLER_NAME, func);
    DBUS_ADD_ARGS(db_call_msg)
    DBUS_ADD_INT32(&state);
    DBUS_REPLY_SEND(db_call_msg);
    if (!state) {
        last_msg[host] = 0;
        sockets[host] = -1;
    }
    return 0;
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
    cfg_parse(cfg, "cluster.conf");
    ping_msg = create_str(9);
    sprintf(ping_msg, "%d-ping", id);/*}}}*/

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
    PRINTD(3, "Registering DBus functions");
    int failed = init_dbus();
    if (failed) {
        DIE("Couldn't establish DBus connection");
    }
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
        if (i == id) continue;
        printf("Attempting to connect to host %d at %s\n", i, addresses[i]);
        if (strcmp(addresses[i], "dyn") != 0) connect_to_host(i);
    }/*}}}*/

    // Register event handlers for bind and any connected hosts
    register_event_base();
    register_host_events();

    return 0;
}/*}}}*/
