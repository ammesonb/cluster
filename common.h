#ifndef CLUSTER_COMMON_H
#define CLUSTER_COMMON_H

#define   PRINTD(level, d_str, args...) if (debug >= level) printf("DEBUG%d: ", level); printf(d_str, ##args); printf("\n");

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <string.h>
#include <sys/time.h>
#include <dbus/dbus.h>

namespace Cluster {
    extern DBusConnection *conn;
    extern const char *DBUS_PATH;
    extern const char *DBUS_NAME;
    extern const char *DBUS_HANDLER_PATH;
    extern const char *DBUS_HANDLER_NAME;

    extern int debug;
    char* create_str(int length);
    unsigned long long get_cur_time();
    char* read_file(char *name);
}
#endif
