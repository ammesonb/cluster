#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <dbus/dbus.h>
#include <event.h>
#include <confuse.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netdb.h>

#include <algorithm>
#include <string>
#include <ctime>
#include <iostream>
#include <fstream>
#include <vector>
#include <map>
#include <openssl/rand.h>

#include "dbus_common.h"
#include "common.h"
#include "host.h"
#include "service.h"
#include "network.h"

 // Introspection for DBus/*{{{*/
 const char *introspec_xml =
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

using std::vector;
using std::map;
using std::string;
using std::cout;
using std::cerr;
using std::endl;

namespace Cluster {/*{{{*/
    int handler_pid;
    pthread_t dbus_dispatcher;
    DBusConnection *conn;

    string my_id;
    int int_id;
    string ping_msg;

    int port;

    bool keep_running = true;

    // Config variables/*{{{*/
    cfg_t *cfg;
    cfg_bool_t text = cfg_false;
    int debug = 2, interval, dead;
    char *email = NULL, *crit_files = NULL, *crit_dirs = NULL;/*}}}*/

    cfg_opt_t config[] = {
        CFG_SIMPLE_INT("beat_interval", &interval),
        CFG_SIMPLE_INT("dead_time", &dead),
        CFG_SIMPLE_STR("email", &email),
        CFG_SIMPLE_BOOL("text_alerts", &text),
        CFG_SIMPLE_INT("verbosity", &debug),
        CFG_SIMPLE_STR("critical_files", &crit_files),
        CFG_SIMPLE_STR("critical_dirs", &crit_dirs),
        CFG_END()
    };

    map<int, Host> host_list;
    map<int, Service> serv_list;
    vector<int> hosts_busy;
    vector<int> hosts_online;
    vector<int> running_services;
    map<int, vector<string>> send_message_queue;

    vector<string> sync_files;
    map<string, string> sync_checksums;
    map<string, time_t> sync_timestamps;

    DBUS_FUNC(dbus_handler) {/*{{{*/
        int handled = 0;
        const char *iface = dbus_message_get_interface(dbmsg);
        PRINTD(3, 0, "Received DBus message on %s", iface);
        if (!strcmp(iface, DBUS_INTERFACE_INTROSPECTABLE)) {
            DBUS_INTROSPEC
            handled = 1;
        } else if (!strcmp(iface, DBUS_NAME)) {
            if (dbus_message_is_method_call(dbmsg, DBUS_NAME, "updateHandlerPID")) {
                DBUS_GET_ARGS(DBUS_TYPE_INT32, &handler_pid);
                PRINTD(3, 0, "Handler pid set to %d", handler_pid);
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

    void* dbus_loop(void* args) {/*{{{*/
        PRINTD(1, 0, "Entering main DBus loop");
        while (dbus_connection_read_write_dispatch(conn, -1));
        PRINTD(1, 0, "Exiting main DBus loop");
        return NULL;
    }/*}}}*/

    int init_dbus() {/*{{{*/
        PRINTD(2, 0, "Initializing DBus on %s", DBUS_PATH);
        DBUS_INIT(DBUS_NAME, DBUS_PATH, dbus_handler)
        PRINTD(2, 1, "Dispatching DBus thread");
        pthread_create(&dbus_dispatcher, NULL, dbus_loop, NULL);
        return EXIT_SUCCESS;
    }/*}}}*/

    void queue_keepalive() {/*{{{*/
        PRINTD(4, 0, "Queueing keepalive to %lu hosts", hosts_online.size());
        for (auto it = hosts_online.begin(); it != hosts_online.end(); it++) {
            Host h = host_list[*it];
            // If host is not otherwise occupied, queue keepalive
            if (std::find(hosts_busy.begin(), hosts_busy.end(), *it) == hosts_busy.end())
                send_message_queue[h.id].push_back(ping_msg);
        }
    }/*}}}*/
}/*}}}*/

using namespace Cluster;

int main(int argc, char *argv[]) {/*{{{*/
    struct timeval init_start_tv, init_end_tv;
    gettimeofday(&init_start_tv, NULL);
    PRINTD(1, 0, "Loading config file");
    // Load configuration /*{{{*/
    

    cfg = cfg_init(config, 0);
    cfg_parse(cfg, "cluster.conf");
    // For some reason this variable is corrupted in cfg_parse, specifically
    // set to 0 in cfg_set_opt, so need to restore it to avoid attempting to
    // check a nonexistent level 0 split, causing a segfault
    set_split_level(-1);

    PRINTD(1, 1, "Found debug level %d", debug);

    // Read in machine number and set ping message
    PRINTD(3, 1, "Determining my ID");/*{{{*/
    my_id = read_file(STRLITFIX("/var/opt/cluster/id"));
    my_id.assign(trim(my_id));
    int_id = stoi(my_id);
    PRINTDI(3, "My ID is %s", my_id.c_str());
    ping_msg.reserve(my_id.length() + 5);
    ping_msg.append(my_id).append("-ping");
    if (ping_msg.length() < 6) {cerr <<  "Failed to parse id" << endl; exit(1);}/*}}}*/

    PRINTD(3, 1, "Loading hosts");
    if (!validate_host_config()) {DIE("Found invalid host configuration file!");}
    load_host_config(); 

    PRINTD(3, 1, "Loading services");
    if (!validate_service_config()) {DIE("Found invalid service configuration file!");}
    load_service_config(); /*}}}*/

    PRINTD(3, 1, "Creating list of synchronized files");/*{{{*/
    string c_files = trim(string(crit_files));
    start_split(c_files, "\n");
    string file = trim(get_split());
    while (file.length() != 0) {
        sync_files.push_back(file);
        file = trim(get_split());
    }
    end_split(0);

    string c_dirs = string(crit_dirs);
    start_split(c_dirs, ",");
    string dir = trim(get_split());
    while (dir.length() != 0) {
        vector<string> f = get_directory_files((char*)dir.c_str());
        sync_files.insert(sync_files.end(), f.begin(), f.end());
        dir = trim(get_split());
    }

    for (auto it = serv_list.begin(); it != serv_list.end(); it++) {
        Service s = (*it).second;
        string dir = string("/home/brett/Programming/cluster/").append(s.name).append("/");
        vector<string> f = get_directory_files((char*)dir.c_str());
        sync_files.insert(sync_files.end(), f.begin(), f.end());
    }/*}}}*/

    PRINTD(3, 1, "Getting sync'ed file data");/*{{{*/
    for (auto it = sync_files.begin(); it != sync_files.end(); it++) {
        string name = (string)*it;
        sync_checksums[name] = hash_file((char*)name.c_str());
        sync_timestamps[name] = get_file_mtime((char*)name.c_str());
        PRINTD(4, 1, "    Sync file: %s", name.c_str());
        PRINTD(4, 1, "     Checksum: %s", sync_checksums[name].c_str());
        PRINTD(4, 1, "Last modified: %lu", sync_timestamps[name]);
    }/*}}}*/

    PRINTD(2, 0, "Initializing session");
    PRINTD(3, 1, "Opening DBus");
    // Open DBus connection/*{{{*/
    DBusError dberror;

    dbus_error_init(&dberror);
    conn = dbus_bus_get(DBUS_BUS_SESSION, &dberror);
    if (!conn || conn == NULL) {
        PRINTD(1, 0, "Connection to D-BUS daemon failed: %s", dberror.message);
    } else {
        init_dbus();
    }
    dbus_error_free(&dberror);/*}}}*/

    PRINTD(3, 0, "Performing crypto sanity check");/*{{{*/
    PRINTD(3, 1, "Plain: %s", ping_msg.c_str());
    string out = enc_msg(ping_msg, string("password"));
    PRINTD(3, 1, "Enc: %s", out.c_str());
    string pt = dec_msg(out, string("password"));
    PRINTD(3, 1, "Dec: %s", pt.c_str());
    if (pt != ping_msg) {DIE("AES encrypt/decrypt didn't return same value!");}/*}}}*/

    PRINTD(2, 0, "Starting networking services");/*{{{*/
    bool online = verify_connectivity();
    if (online) {
        PRINTD(3, 1, "I am online");
        port = host_list[int_id].port;
    } else {
        DIE("I am not online");
    }

    start_accept_thread(port);

    PRINTD(3, 1, "Attempting to connect to all hosts");
    for (auto it = host_list.begin(); it != host_list.end(); it++) {
        Host h = (*it).second;
        if (h.id == int_id) continue;
        PRINTD(3, 2, "Connecting to host %s", h.address.c_str());
        connect_to_host((*it).first);
    }
    PRINTD(2, 1, "Found %lu alive hosts", hosts_online.size());

    PRINTD(3, 1, "Creating receive thread");
    pthread_t recv_thread;
    pthread_create(&recv_thread, NULL, recv_loop, NULL);/*}}}*/

    gettimeofday(&init_end_tv, NULL);

    unsigned long long diff = init_end_tv.tv_usec;
    diff /= 1000;
    diff += (init_end_tv.tv_sec * 1000);

    unsigned long long diff2 = init_start_tv.tv_usec;
    diff2 /= 1000;
    diff2 += (init_start_tv.tv_sec * 1000);

    diff -= diff2;
    PRINTD(3, 0, "Initialization took %llu milliseconds", diff);

    // TODO need termination condition
    time_t last_keepalive_update = 0;
    time_t last_key_update = get_cur_time();
    PRINTD(1, 0, "Entering service loop");
    while (keep_running) {/*{{{*/
        vector<int> now_offline;
        // Check that all hosts are actually online/*{{{*/
        if (hosts_online.size() > 0) {
            for (auto it = hosts_online.begin(); it != hosts_online.end(); it++) {
                Host h = host_list[*it];
                if (get_cur_time() - h.last_msg > dead) {
                    if (!verify_connectivity()) {
                        PRINTD(1, 0, "I am offline!");
                        // TODO do something here
                        break;
                    }
                    time_t l = h.last_msg;
                    struct tm *t = localtime(&l);
                    PRINTD(2, 0, "Host %s is offline, last heard from at %s", h.address.c_str(), asctime(t));
                    h.online = false;
                    now_offline.push_back(h.id);
                    check_services(h.id, false);
                }
            }
        }

        if (now_offline.size() > 0) {
            for (auto it = now_offline.begin(); it != now_offline.end(); it++)
            hosts_online.erase(std::remove(hosts_online.begin(), hosts_online.end(), *it), hosts_online.end());
        }/*}}}*/

        // Check keepalive timer/*{{{*/
        if (get_cur_time() - last_keepalive_update > interval) {
            PRINTD(5, 0, "Sending keepalive");
            last_keepalive_update = get_cur_time();
            queue_keepalive();
        }/*}}}*/

        // Check key update interval/*{{{*/
        // Should update between 3 * host_num and 3 * (host_num + 1) once per hour
        long seconds = get_cur_time() % 3600;
        if ((3 * 60 * int_id) < seconds && seconds < (3 * 60 * (int_id + 1)) && (last_key_update - get_cur_time() > 1800)) {
            time_t now = time(0);
            struct tm *tmn = localtime(&now);
            char *buf = create_str(100);
            strftime(buf, 100, "%F %T", tmn);
            PRINTD(2, 0, "Updating encryption key at %s", buf);
            free(buf);
            last_key_update = get_cur_time();
            unsigned char *key = (unsigned char*)create_str(16);
            RAND_load_file("/dev/urandom", 128);
            RAND_bytes(key, 16);
            string passwd = hexlify(key, 16);
            host_list[int_id].password.assign(passwd);
            string data = read_file(STRLITFIX("hosts"));
            int pos = data.find(host_list[int_id].address, 0);
            pos += host_list[int_id].address.length() +
                   std::to_string(host_list[int_id].port).length() + 2;
            int endpos = data.find("\n", pos);
            data.replace(pos, endpos - pos, passwd);
            PRINTD(3, 0, "Updating hosts file");
            std::ofstream f;
            f.open("hosts");
            f << data;
            f.close();
        }/*}}}*/

        // Check file timestamps /*{{{*/
        for (auto it = sync_files.begin(); it != sync_files.end(); it++) {
            string name = *it;
            if (get_file_mtime((char*)name.c_str()) != sync_timestamps[name]) {
                PRINTD(3, 0, "Detected change in file %s", name.c_str());
                sync_timestamps[name] = get_file_mtime((char*)name.c_str());
                // TODO push updated file
            }
        }/*}}}*/

        usleep((float)interval / 2.0 * 100000.0);
    }/*}}}*/

    PRINTD(1, 0, "Exiting main loop");
    
    return 0;
}/*}}}*/
