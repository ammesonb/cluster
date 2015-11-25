#ifndef CLUSTER_COMMON_H
#define CLUSTER_COMMON_H

#define   PRINTD(level, indent, d_str, args...) if (debug >= level) {\
          time_t printd_now = time(0);\
          char *printd_time = asctime(localtime(&printd_now));\
          printd_time[strlen(printd_time) - 1] = '\0';\
          PRINTD_INDENT_LEVEL = indent; printf("%s %sDEBUG%d: ", printd_time, string(indent * 4, ' ').c_str(), level);\
          printf(d_str, ##args); printf("\n");}
#define   PRINTDI(level, d_str, args...) if (debug >= level) {\
          time_t printd_now = time(0);\
          char *printd_time = asctime(localtime(&printd_now));\
          printd_time[strlen(printd_time) - 1] = '\0';\
          printf("%s %sDEBUG%d: ", printd_time, string(PRINTD_INDENT_LEVEL * 4, ' ').c_str(), level);\
          printf(d_str, ##args); printf("\n");}
#define   PRINTDR(level, indent_offset, d_str, args...) if (debug >= level) {\
          time_t printd_now = time(0);\
          char *printd_time = asctime(localtime(&printd_now));\
          printd_time[strlen(printd_time) - 1] = '\0';\
          printf("%s %sDEBUG%d: ", printd_time, string((PRINTD_INDENT_LEVEL + indent_offset) * 4, ' ').c_str(), level);\
          printf(d_str, ##args); printf("\n");}
#define   DIE(str, args...) \
          time_t printd_now = time(0);\
          char *printd_time = asctime(localtime(&printd_now));\
          printd_time[strlen(printd_time) - 1] = '\0';\
          fprintf(stderr, "%s  FATAL: " str "\n", printd_time, ##args); exit(1);
#define   STRLITFIX(str) (char*)string(str).c_str()

#define ITERVECTOR(arr, var) for (auto var = arr.begin(); var != arr.end(); var++)
#define VECTORFIND(arr, var) arr.begin(), arr.end(), var
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
#include <confuse.h>
#include <vector>
#include <map>

#include "host.h"
#include "service.h"

using std::map;
using std::string;

namespace Cluster {
    extern cfg_opt_t config[];
    
    extern string my_id;
    extern int int_id;
    extern int port;
    extern int PRINTD_INDENT_LEVEL;
    extern DBusConnection *conn;

    extern map<int, Host> host_list;
    extern map<int, Service> serv_list;
    extern vector<int> hosts_online;
    extern vector<int> hosts_busy;
    extern vector<int> running_services;
    extern map<int, vector<string>> send_message_queue;

    extern vector<string> sync_files;
    extern map<string, string> sync_checksums;
    extern map<string, time_t> sync_timestamps;

    extern bool keep_running;
    extern int debug;
    extern int interval;

    bool validate_host_config();
    void load_host_config();
    bool validate_service_config();
    void load_service_config();

    void check_services(int hostid, bool online);

    bool is_ip(string s);

    vector<string> get_directory_files(char *dir);

    void start_split(string source, string delim);
    int get_split_level();
    void set_split_level(int l);
    string get_split();
    void end_split(int level);

    char* create_str(int length);
    time_t get_cur_time();

    string hexlify(string data);
    string hexlify(unsigned char *data, int len);
    string unhexlify(string data);
    string hash_file(char *name);
    string filename(string fname);
    string dirname(string fname);
    string read_file(char *name);
    time_t get_file_mtime(char *name);

    string ltrim(string s);
    string rtrim(string s);
    string trim(string s);
}
#endif
