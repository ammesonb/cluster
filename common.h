#ifndef CLUSTER_COMMON_H
#define CLUSTER_COMMON_H

#define   PRINTD(level, d_str, args...) if (debug >= level) {printf("DEBUG%d: ", level); printf(d_str, ##args); printf("\n");}
#define   DIE(str, args...) fprintf(stderr, " FATAL: " str "\n", ##args); exit(1);
#define   STRLITFIX(str) (char*)string(str).c_str()

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <string.h>
#include <string>
#include <sys/time.h>
#include <dbus/dbus.h>

using std::string;

namespace Cluster {
    extern DBusConnection *conn;
    extern const char *DBUS_PATH;
    extern const char *DBUS_NAME;
    extern const char *DBUS_HANDLER_PATH;
    extern const char *DBUS_HANDLER_NAME;

    bool is_ip(string s);

    extern int debug;
    void start_split(string source, string delim);
    string get_split();
    void end_split(int level);
    char* create_str(int length);
    unsigned long long get_cur_time();
    string read_file(char *name);
}
#endif
