#ifndef CLUSTER_NETWORK_H
#define CLUSTER_NETWORK_H
#include <errno.h>
#include <sys/socket.h>
#include <netdb.h>

#define MAX_HOSTS 100
#define MAX_SERVICES 50
#define MAX_MSG_LEN 65535

struct timeout_args {
    int host;
};

int accept_fd;
char **addresses;
char **passphrases;
int sockets[MAX_HOSTS];
unsigned long long last_msg[MAX_HOSTS];
int status[MAX_HOSTS];
int host_error[MAX_HOSTS];
int dynamic[MAX_HOSTS];
int num_hosts;
char **services;
int **service_hosts;
int num_services;
char *ping_msg;
char *my_pass;
int cur_host;
int char_count;
extern int id;
extern int max_id;
extern int am_dynamic;
extern int MIN_PASS_LENGTH;
extern int port;
extern int dead;

void configure_socket(int sockfd);
void send_keepalive(int fd, short ev, void *arg);
void recv_data(int fd, short ev, void *arg);
void write_err(int sig);
void load_hosts(char *hosts);
void load_services(char *services);
int update_host_state(int host, int state);
void accept_connection(int fd, short ev, void *arg);
void connect_to_host(int host);
void connection_timeout(int fd, short ev, void *arg);
#endif
