#ifndef CLUSTER_COMMON_H
#define CLUSTER_COMMON_H

#define   PRINTD(level, indent, d_str, args...) if (debug >= level) {\
          PRINTD_INDENT_LEVEL = indent; printf("%sDEBUG%d: ", string(indent * 4, ' ').c_str(), level);\
          printf(d_str, ##args); printf("\n");}
#define   PRINTDI(level, d_str, args...) if (debug >= level) {\
          printf("%sDEBUG%d: ", string(PRINTD_INDENT_LEVEL * 4, ' ').c_str(), level);\
          printf(d_str, ##args); printf("\n");}
#define   PRINTDR(level, indent_offset, d_str, args...) if (debug >= level) {\
          printf("%sDEBUG%d: ", string((PRINTD_INDENT_LEVEL + indent_offset) * 4, ' ').c_str(), level);\
          printf(d_str, ##args); printf("\n");}
#define   DIE(str, args...) fprintf(stderr, " FATAL: " str "\n", ##args); exit(1);
#define   STRLITFIX(str) (char*)string(str).c_str()

#define DBUS_PATH "/com/bammeson/cluster"
#define DBUS_NAME "com.bammeson.cluster"
#define DBUS_HANDLER_PATH "/com/bammeson/clusterhandler"
#define DBUS_HANDLER_NAME "com.bammeson.clusterhandler"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <string.h>
#include <string>
#include <sys/time.h>
#include <dbus/dbus.h>
#include <vector>
#include <map>

#include "host.h"
#include "service.h"

using std::map;
using std::string;

namespace Cluster {
    extern string my_id;
    extern int int_id;
    extern int port;
    extern int PRINTD_INDENT_LEVEL;
    extern DBusConnection *conn;

    extern map<int, Host> host_list;
    extern map<int, Service> serv_list;
    extern vector<Host> hosts_online;
    extern vector<string> sync_files;
    extern map<int, vector<string>> send_message_queue;
    extern map<string, string> sync_checksums;

    extern bool keep_running;
    extern int debug;
    extern int interval;

    bool validate_host_config();
    void load_host_config();
    bool validate_service_config();
    void load_service_config();

    bool is_ip(string s);

    vector<string> get_directory_files(char *dir);

    void start_split(string source, string delim);
    int get_split_level();
    void set_split_level(int l);
    string get_split();
    void end_split(int level);

    char* create_str(int length);
    unsigned long long get_cur_time();

    string hexlify(string data);
    string hexlify(unsigned char *data, int len);
    string unhexlify(string data);
    string hash_file(char *name);
    string read_file(char *name);
    time_t get_file_mtime(char *name);

    string ltrim(string s);
    string rtrim(string s);
    string trim(string s);
}
#endif
