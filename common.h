#ifndef CLUSTER_COMMON_H
#define CLUSTER_COMMON_H

#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_ORANGE  "\x1b[38;5;202m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_YELLOW  "\x1b[33m"
#define ANSI_COLOR_BLUE    "\x1b[34m"
#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_CYAN    "\x1b[36m"
#define ANSI_COLOR_RESET   "\x1b[0m"

static const char *colors[] = {ANSI_COLOR_RED, ANSI_COLOR_MAGENTA, ANSI_COLOR_ORANGE, ANSI_COLOR_YELLOW, ANSI_COLOR_CYAN, ANSI_COLOR_GREEN};

#define   PRINTD(level, indent, label, d_str, args...) if (debug >= level) {\
          time_t printd_now = time(0);\
          char *printd_time = asctime(localtime(&printd_now));\
          printd_time[strlen(printd_time) - 1] = '\0';\
          PRINTD_INDENT_LEVEL = indent; printf("%s %s %sDEBUG%d (%s): ", printd_time, colors[level], string(indent * 4, ' ').c_str(), level, label);\
          printf(d_str, ##args); printf("%s\n", ANSI_COLOR_RESET);}
#define   PRINTDI(level, label, d_str, args...) if (debug >= level) {\
          time_t printd_now = time(0);\
          char *printd_time = asctime(localtime(&printd_now));\
          printd_time[strlen(printd_time) - 1] = '\0';\
          printf("%s %s %sDEBUG%d (%s): ", printd_time, colors[level], string(PRINTD_INDENT_LEVEL * 4, ' ').c_str(), level, label);\
          printf(d_str, ##args); printf("%s\n", ANSI_COLOR_RESET);}
#define   PRINTDR(level, indent_offset, label, d_str, args...) if (debug >= level) {\
          time_t printd_now = time(0);\
          char *printd_time = asctime(localtime(&printd_now));\
          printd_time[strlen(printd_time) - 1] = '\0';\
          printf("%s %s %sDEBUG%d (%s): ", printd_time, colors[level], string((PRINTD_INDENT_LEVEL + indent_offset) * 4, ' ').c_str(), level, label);\
          printf(d_str, ##args); printf("%s\n", ANSI_COLOR_RESET);}
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
#include <semaphore.h>
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
    extern map<int, sem_t> hosts_busy;
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

    void start_split(string source, string delim, char *key);
    string get_split(char *key);
    bool split_active(char *key);
    void end_split(char *key);

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
    
    bool sem_locked(sem_t sem);
}
#endif
