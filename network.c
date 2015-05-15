#include "common.h"
#include "network.h"
#include "dbus_common.h"

int id = -1, max_id = -1, am_dynamic = 0;
int MIN_PASS_LENGTH = 8;

void quit(int);

int update_host_state(int host, int state) {/*{{{*/
    // Update host status and dispatch necessary signals
    if (status[host] == state) return 0;
    status[host] = state;
    char *func = state ? "hostOnline" : "hostOffline";
    char *host_str = create_str(4);
    sprintf(host_str, "%d", host);
    PRINTD(3, "Dispatching DBus call for host %d with state %d", host, state);
    DBUS_INIT_METHOD_CALL(DBUS_HANDLER_NAME, DBUS_HANDLER_PATH, DBUS_HANDLER_NAME, func);
    DBUS_ADD_ARGS_SIMPLE(db_call_msg, DBUS_TYPE_STRING, &host_str);
    DBUS_REPLY_SEND(db_call_msg);
    if (!state) {
        last_msg[host] = 0;
        sockets[host] = -1;
    }
    return 0;
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

void send_keepalive(int fd, short ev, void* arg) {/*{{{*/
    // Send a keepalive packet to a given host
    int length = strlen(ping_msg);
    PRINTD(3, "Sending keepalive to live clients");
    for (cur_host = 0; cur_host < num_hosts; cur_host++) {
        if (!status[cur_host]) {continue;}
        int char_count = write(sockets[cur_host], ping_msg, length);
        if (char_count < 1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK ||
                errno == EINTR) {continue;}
            PRINTD(1, "Keepalive packet to host %s (%d) failed!", addresses[cur_host], cur_host);
            update_host_state(cur_host, 0);
        }
    }
}/*}}}*/

void recv_data(int fd, short ev, void *arg) {/*{{{*/
    // Read and parse data from host
    struct timeout_args *args = (struct timeout_args*)arg;
    int host = args->host;
    PRINTD(3, "Receiving data from host %s (%d)", addresses[host], host);
    char *buf = create_str(MAX_MSG_LEN);
    int count = read(sockets[host], buf, MAX_MSG_LEN);
    if (count) {
        PRINTD(3, "Received %s from host %s (%d)", buf, addresses[host], host);
        last_msg[host] = get_cur_time();
    } else {
        PRINTD(3, "Data received had no length! Testing for closed connection....");
        cur_host = host;
        char_count = write(sockets[host], "ping", 4);
        if (host_error[host]) {PRINTD(1, "Connection broken with host %d", host); free(buf); return;}
    }
    free(buf);
}/*}}}*/

void write_err(int sig) {/*{{{*/
    if (cur_host < 0) return;
    host_error[cur_host] = 1;
    PRINTD(1, "Connection with host %s (%d) broken!", addresses[cur_host], cur_host);
    update_host_state(cur_host, 0);
}/*}}}*/

void load_hosts(char *hosts) {/*{{{*/
    addresses = (char**)malloc(sizeof(char*) * MAX_HOSTS);
    passphrases = (char**)malloc(sizeof(char*) * MAX_HOSTS);
    char *host = strtok(hosts, "\n");
    int i = 0;
    while (host != NULL) {
        PRINTD(3, "Loading host %d", i);
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
                fprintf(stderr, "Passphrase on line %d must be at least %d characters!\n", i, MIN_PASS_LENGTH);
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
        PRINTD(3, "Loading service %d", i);
        char *service_name = strtok(service, " ");
        services[i] = service_name;

        int j = 0;
        char *service_host = strtok(NULL, " ");
        while (service_host != NULL) {
            PRINTD(3, "Storing service host %d", j);
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

void accept_connection(int fd, short ev, void *arg) {/*{{{*/
    /* Establish new connection
       Ensure sent ID within bounds and 
       IP being connected from matches hosts
       address/DNS name
    */
    PRINTD(2, "Received connection request");
    struct sockaddr client_addr;
    memset(&client_addr, '\0', sizeof(client_addr));
    socklen_t addr_size = sizeof(client_addr);
    int newfd = accept(accept_fd, &client_addr, &addr_size);
    if (newfd < 0) {
        fprintf(stderr, "Failed to establish connection: %s\n", strerror(errno));
        return;
    }

    // Set up socket and get IP address/DNS name/*{{{*/
    PRINTD(3, "Looking up DNS name");
    configure_socket(newfd);
    char *client_host = create_str(500);
    char *client_num_host = create_str(500);
    char *client_serv = create_str(500);
    getnameinfo(&client_addr, sizeof(client_addr), client_host, 500, client_serv, 500, 0);
    getnameinfo(&client_addr, sizeof(client_addr), client_num_host, 500, client_serv, 500, NI_NUMERICHOST);
    PRINTD(3, "Connection from %s (%s)", client_host, client_num_host);/*}}}*/

    // Authenticate host/*{{{*/
    PRINTD(3, "Verifying ID")
    char *buffer = create_str(MAX_MSG_LEN);
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
            cur_host = -1;
            host_error[client_id] = 0;
            char_count = write(newfd, r, strlen(r));
            if (host_error[client_id]) return;
            fprintf(stderr, "ID sent (%d) was higher than allowed range!\n", client_id);
            return;
        } else {
            char *r = "200 CONTINUE";
            cur_host = client_id;
            char_count = write(newfd, r, strlen(r));
            if (host_error[client_id]) return;
        }/*}}}*/

        // Check ID matches connection address/*{{{*/
        PRINTD(3, "Verifying connected address matches ID");
        if (!dynamic[client_id] && strcmp(client_host, addresses[client_id]) &&
                    strcmp(client_num_host, addresses[client_id])) {
                char *r = "403 FORBIDDEN";
                cur_host = client_id;
                char_count = write(newfd, r, strlen(r));
                if (host_error[client_id]) return;
                if (host_error[client_id]) return;
                fprintf(stderr, "IP address host connected from (%s or %s) does not match config file\n",
                        client_host, client_num_host);
                return;/*}}}*/
        } else { // Verify name/pass matches sent ID/*{{{*/
            // Check client sends matching name to ID/*{{{*/
            PRINTD(3, "Checking name and passphrase");
            char *r = "200 CONTINUE";
            cur_host = client_id;
            char_count = write(newfd, r, strlen(r));
            if (host_error[client_id]) return;
            memset(buffer, '\0', 501);
            read(newfd, buffer, MAX_MSG_LEN);
            char *client_name = create_str(500);
            strcpy(client_name, buffer);
            if (strcmp(buffer, addresses[client_id])) {
                r = "404 NOT FOUND";
                cur_host = client_id;
                char_count = write(newfd, r, strlen(r));
                if (host_error[client_id]) return;
                fprintf(stderr, "Name %s does not exist\n", buffer);
                return;
            } else {
                cur_host = client_id;
                char_count = write(newfd, r, strlen(r));
                if (host_error[client_id]) return;
            }/*}}}*/

            // Verify password against name/*{{{*/
            memset(buffer, '\0', 501);
            read(newfd, buffer, 500);
            if (strcmp(buffer, passphrases[client_id])) {
                r = "401 UNAUTHORIZED";
                cur_host = client_id;
                char_count = write(newfd, r, strlen(r));
                if (host_error[client_id]) return;
                fprintf(stderr, "Host %s sent invalid passphrase\n", client_name);
                return;
            } else {
                char_count = write(newfd, r, strlen(r));
                if (host_error[client_id]) return;
            }/*}}}*/

            free(client_name);
            free(buffer);
            free(client_serv);
            free(client_num_host);
            free(client_host);
        }/*}}}*/
    }/*}}}*/

    PRINTD(2, "Host %s is online", addresses[client_id]);
    update_host_state(client_id, 1);
}/*}}}*/

void connect_to_host(int host) {/*{{{*/
    // Attempt to connect to a given host and register events for it
    // Host cannot be dynamic
    if (!dynamic[host]) {
        PRINTD(3, "Running connection function for host %s", addresses[host]);

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
            PRINTD(3, "Trying next address");
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
        PRINTD(3, "Sending ID");
        char *id_auth = create_str(10);
        char *response = create_str(100);
        sprintf(id_auth, "id:%d", id);
        cur_host = host;
        char_count = write(newfd, id_auth, strlen(id_auth));
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
            PRINTD(3, "Sending name and password auth");
            char *name_str = create_str(100);
            char *pass_str = create_str(100);
            memset(&response, '\0', 101);
            sprintf(name_str, "name:%s", name_str);
            sprintf(pass_str, "pass:%s", pass_str);
            cur_host = host;
            char_count = write(newfd, name_str, strlen(name_str) + 1);
            if (host_error[host]) return;
            read(newfd, response, MAX_MSG_LEN);
            if (!strcmp(response, "404 NOT FOUND")) {
                fprintf(stderr, "Name rejected by %s!\n", address);
                return;
            }
            memset(&response, '\0', 101);
            cur_host = host;
            char_count = write(newfd, pass_str, strlen(pass_str) + 1);
            if (host_error[host]) return;
            read(newfd, response, MAX_MSG_LEN);
            if (!strcmp(response, "401 UNAUTHORIZED")) {
                fprintf(stderr, "Password rejected by %s!\n", address);
                return;
            }
            free(name_str);
            free(pass_str);
        }/*}}}*/
        free(response);

        PRINTD(2, "Host %s is online", addresses[host]);
        update_host_state(host, 1);
        sockets[host] = newfd;
        free(port_str);
    }
}/*}}}*/

void connection_timeout(int fd, short ev, void *arg) {/*{{{*/
    struct timeout_args *args = (struct timeout_args*)arg;
    if (last_msg[args->host] + (dead * 1000) < get_cur_time()) {
        PRINTD(2, "Host %d has not responded in %llu ms (cur time %llu)", args->host, get_cur_time() - last_msg[args->host], get_cur_time());
        update_host_state(args->host, 0);
    }
}/*}}}*/
