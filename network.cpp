#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>
#include <netinet/ip.h>
#include <arpa/inet.h>

#include "common.h"
#include "network.h"

namespace Cluster {
    int acceptfd;

    bool verify_connectivity() {/*{{{*/
        return (system("curl -s www.google.com -o /dev/null") == 0);
    }/*}}}*/

    string srecv(int sock) {/*{{{*/
        PRINTDI(5, "Waiting to receive size of data from socket");
        char *buf = create_str(8);
        recv(sock, buf, 8, 0);
        string size;
        size.reserve(8);
        size.assign(buf, 8);
        PRINTDI(5, "Got size %d", stoi(size));
        free(buf);

        PRINTDI(5, "Waiting to receive data from socket");
        char *data = create_str(stoi(size));
        recv(sock, data, stoi(size), 0);
        string str;
        str.reserve(stoi(size));
        str.assign(data, stoi(size));
        return str;
    }/*}}}*/

    void* accept_thread() {
        while (keep_running) {
            struct sockaddr_in client_addr;
            int client_len = sizeof(client_addr);
            int client_fd = accept(acceptfd, (struct sockaddr*)&client_addr, (socklen_t*)&client_len);
            char* addr = inet_ntoa(client_addr.sin_addr);
            if (client_fd < 0) {PRINTD(1, 0, "Failed to accept a connection from %s", addr); continue;}
            // TODO will this block?
            string hostdata = srecv(client_fd);
            start_split(hostdata, "--");
            int level = get_split_level();
            int hostid = stoi(get_split());
            string hostname = get_split();
            while (get_split_level() == level)
                hostname.append(get_split());
            PRINTD(3, 0, "Got host connection from ID %d: %s", hostid, hostname.c_str());
            // TODO Blocking here again
            string passwd = srecv(client_fd);
            string ip;
            ip.assign(addr);
            //if (host_list.at(hostid).authenticate(hostname, passwd, ip)) {
                //PRINTD(4, 0, "Host %s connection authenticated", hostname.c_str());
            //} else {
                //PRINTD(1, 0, "Host %s connection didn't authenticate!", hostname.c_str());
                //continue;
            //}

            // TODO update host status
            // TODO spawn threads for checking keepalive status/validating commands
            // TODO or maybe have one master thread for all of them? That'd make more sense
        }

        PRINTD(1, 0, "Accept thread is exiting");
        return NULL;
    }

    void start_accept_thread(int port) {
        PRINTDI(3, "Configuring connection accept socket");
        acceptfd = socket(AF_INET, SOCK_STREAM, 0);
        set_sock_opts(acceptfd);

        // Create and fill address struct/*{{{*/
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port);/*}}}*/

        if (bind(acceptfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            DIE("Failed to bind accept socket");
        }

        if (listen(acceptfd, 10) < 0) {DIE("Failed to listen on accept socket");}

        // TODO write accept function and start thread
        // TODO need to set hosts/services variables as extern in common?
    }

    void connect_to_host(Host h) {
        // TODO write this
    }

    void set_sock_opts(int sockfd) { /*{{{*/
        int value = 1;
        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &value, sizeof(value)) == -1) {
            DIE("Failed to set option REUSEADDR");
        }

        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, &value, sizeof(value)) == -1) {
            DIE("Failed to set option REUSEPORT");
        }

        struct linger so_linger;
        so_linger.l_onoff = 1;
        so_linger.l_linger = 0;
        if (setsockopt(sockfd, SOL_SOCKET, SO_LINGER, &so_linger, sizeof(so_linger)) == -1) {
            DIE("Failed to set option LINGER");
        }

        struct timeval timeout;
        timeout.tv_sec = 2;
        timeout.tv_usec = 0;
        if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout)) == -1) {
            DIE("Failed to set option RCVTIMEO");
        }
        if (setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeout, sizeof(timeout)) == -1) {
            DIE("Failed to set option SNDTIMEO");
        }

        int flags = fcntl(sockfd, F_GETFL);
        flags |= O_NONBLOCK;
        fcntl(sockfd, F_SETFL, flags);
    } /*}}}*/
}
