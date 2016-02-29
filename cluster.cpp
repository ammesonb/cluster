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

// TODO crashes if a host drops offline and reconnects before other host notices it is offline
//      Hard to reproduce
// TODO detect new files in sync'ed folders
// TODO file sending loop seems to not clear busy flag
// TODO dual auth, if started simultaneously will connect by accept then in the connect loop

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
        CFG_SIMPLE_INT((char*)"beat_interval", &interval),
        CFG_SIMPLE_INT((char*)"dead_time", &dead),
        CFG_SIMPLE_STR((char*)"email", &email),
        CFG_SIMPLE_BOOL((char*)"text_alerts", &text),
        CFG_SIMPLE_INT((char*)"verbosity", &debug),
        CFG_SIMPLE_STR((char*)"critical_files", &crit_files),
        CFG_SIMPLE_STR((char*)"critical_dirs", &crit_dirs),
        CFG_END()
    };

    map<int, Host> host_list;
    map<int, Service> serv_list;
    map<int, sem_t> hosts_busy;
    vector<int> hosts_online;
    vector<int> running_services;
    map<int, vector<string>> send_message_queue;

    vector<string> sync_files;
    map<string, string> sync_checksums;
    map<string, time_t> sync_timestamps;

    DBUS_FUNC(dbus_handler) {/*{{{*/
        int handled = 0;
        const char *iface = dbus_message_get_interface(dbmsg);
        PRINTD(3, 0, "DBUS", "Received DBus message on %s", iface);
        if (!strcmp(iface, DBUS_INTERFACE_INTROSPECTABLE)) {
            DBUS_INTROSPEC
            handled = 1;
        } else if (!strcmp(iface, DBUS_NAME)) {
            if (dbus_message_is_method_call(dbmsg, DBUS_NAME, "updateHandlerPID")) {
                DBUS_GET_ARGS(DBUS_TYPE_INT32, &handler_pid);
                PRINTD(3, 0, "DBUS", "Handler pid set to %d", handler_pid);
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
        PRINTD(1, 0, "DBUS", "Entering main DBus loop");
        while (dbus_connection_read_write_dispatch(conn, -1));
        PRINTD(1, 0, "DBUS", "Exiting main DBus loop");
        return NULL;
    }/*}}}*/

    int init_dbus() {/*{{{*/
        PRINTD(2, 0, "DBUS", "Initializing DBus on %s", DBUS_PATH);
        DBUS_INIT(DBUS_NAME, DBUS_PATH, dbus_handler)
        PRINTD(2, 1, "DBUS", "Dispatching DBus thread");
        pthread_create(&dbus_dispatcher, NULL, dbus_loop, NULL);
        return EXIT_SUCCESS;
    }/*}}}*/

    void queue_keepalive() {/*{{{*/
        PRINTD(4, 0, "MAIN", "Queueing keepalive to %lu hosts", hosts_online.size());
        ITERVECTOR(hosts_online, it) {
            if (sem_locked(hosts_busy[*it])) continue;
            Host h = host_list[*it];
            send_message_queue[h.id].push_back(ping_msg);
        }
    }/*}}}*/
}/*}}}*/

using namespace Cluster;

int main(int argc, char *argv[]) {/*{{{*/
    struct timeval init_start_tv, init_end_tv;
    gettimeofday(&init_start_tv, NULL);
    PRINTD(1, 0, "MAIN", "Loading config file");
    // Load configuration /*{{{*/
    cfg = cfg_init(config, 0);
    cfg_parse(cfg, "cluster.conf");

    PRINTD(1, 1, "MAIN", "Found debug level %d", debug);

    // Read in machine number and set ping message
    PRINTD(3, 1, "MAIN", "Determining my ID");/*{{{*/
    my_id = read_file(STRLITFIX("/var/opt/cluster/id"));
    my_id.assign(trim(my_id));
    int_id = stoi(my_id);
    PRINTDI(3, "MAIN", "My ID is %s", my_id.c_str());
    ping_msg.reserve(my_id.length() + 5);
    ping_msg.append(my_id).append("-ping");
    if (ping_msg.length() < 6) {cerr <<  "Failed to parse id" << endl; exit(1);}/*}}}*/

    PRINTD(3, 1, "MAIN", "Loading hosts");
    if (!validate_host_config()) {DIE("Found invalid host configuration file!");}
    load_host_config(); 

    PRINTD(3, 1, "MAIN", "Loading services");
    if (!validate_service_config()) {DIE("Found invalid service configuration file!");}
    load_service_config(); /*}}}*/

    PRINTD(3, 1, "MAIN", "Creating list of synchronized files");/*{{{*/
    string c_files = trim(string(crit_files));
    start_split(c_files, "\n", STRLITFIX("cfiles"));
    string file = trim(get_split(STRLITFIX("cfiles")));
    while (file.length() != 0) {
        sync_files.push_back(file);
        file = trim(get_split(STRLITFIX("cfiles")));
    }
    end_split(STRLITFIX("cfiles"));

    string c_dirs = string(crit_dirs);
    start_split(c_dirs, ",", STRLITFIX("cdirs"));
    string dir = trim(get_split(STRLITFIX("cdirs")));
    while (dir.length() != 0) {
        vector<string> f = get_directory_files((char*)dir.c_str());
        sync_files.insert(sync_files.end(), f.begin(), f.end());
        dir = trim(get_split(STRLITFIX("cdirs")));
    }
    end_split(STRLITFIX("cdirs"));

    ITERVECTOR(serv_list, it) {
        Service s = (*it).second;
        string dir = string("/home/brett/Programming/cluster/").append(s.name).append("/");
        vector<string> f = get_directory_files((char*)dir.c_str());
        sync_files.insert(sync_files.end(), f.begin(), f.end());
    }/*}}}*/

    PRINTD(3, 1, "MAIN", "Getting sync'ed file data");/*{{{*/
    // TODO need to consider files that aren't on remote machine
    // TODO really should use checksums since may have been overwritten while offline
    // TODO prioritize files on online machines over offline?
    // TODO need to store checksums after quit, maybe could use only when file modification state
    // unknown?
        // TODO would need protocol to check file checksums then
    ITERVECTOR(sync_files, it) {
        string name = (string)*it;
        sync_checksums[name] = hash_file((char*)name.c_str());
        sync_timestamps[name] = get_file_mtime((char*)name.c_str());
        PRINTD(4, 1, "MAIN", "    Sync file: %s", name.c_str());
        PRINTD(4, 1, "MAIN", "     Checksum: %s", sync_checksums[name].c_str());
        PRINTD(4, 1, "MAIN", "Last modified: %lu", sync_timestamps[name]);
    }/*}}}*/

    PRINTD(2, 0, "MAIN", "Initializing session");
    PRINTD(3, 1, "MAIN", "Opening DBus");
    // Open DBus connection/*{{{*/
    DBusError dberror;

    dbus_error_init(&dberror);
    conn = dbus_bus_get(DBUS_BUS_SESSION, &dberror);
    if (!conn || conn == NULL) {
        PRINTD(0, 0, "MAIN", "Connection to D-BUS daemon failed: %s", dberror.message);
    } else {
        init_dbus();
    }
    dbus_error_free(&dberror);/*}}}*/

    PRINTD(3, 0, "MAIN", "Performing crypto sanity check");/*{{{*/
    PRINTD(3, 1, "MAIN", "Plain: %s", ping_msg.c_str());
    string out = enc_msg(ping_msg, string("password"));
    PRINTD(3, 1, "MAIN", "Enc: %s", out.c_str());
    vector<string> p;
    p.push_back("password");
    string pt = dec_msg(out, p);
    PRINTD(3, 1, "MAIN", "Dec: %s", pt.c_str());
    if (pt != ping_msg) {DIE("AES encrypt/decrypt didn't return same value!");}/*}}}*/

    PRINTD(2, 0, "MAIN", "Starting networking services");/*{{{*/
    bool online = verify_connectivity();
    if (online) {
        PRINTD(3, 1, "MAIN", "I am online");
        port = host_list[int_id].port;
    } else {
        DIE("I am not online");
    }

    // Ensure DNS records are up to date
    update_dns();

    start_accept_thread(port);

    PRINTD(3, 1, "MAIN", "Attempting to connect to all hosts");
    ITERVECTOR(host_list, it) {
        Host h = (*it).second;
        if (h.id == int_id) continue;
        PRINTD(3, 2, "MAIN", "Connecting to host %s", h.address.c_str());
        connect_to_host((*it).first);
    }
    PRINTD(2, 1, "MAIN", "Found %lu alive hosts", hosts_online.size());

    PRINTD(3, 1, "MAIN", "Creating receive thread");
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
    PRINTD(3, 0, "MAIN", "Initialization took %llu milliseconds", diff);

    // TODO need termination condition
    time_t last_keepalive_update = 0;
    PRINTD(1, 0, "MAIN", "Entering service loop");
    while (keep_running) {/*{{{*/
        vector<int> now_offline;
        // Check that all hosts are actually online/*{{{*/
        if (hosts_online.size() > 0) {
            ITERVECTOR(hosts_online, it) {
                Host h = host_list[*it];
                if (get_cur_time() - h.last_msg > dead) {
                    if (!verify_connectivity()) {
                        PRINTD(1, 0, "MAIN", "I am offline!");
                        // TODO do something here
                        // or maybe not? If you're offline, just stop your current services?
                        // What about unsychronized files?
                        // Exponential backoff? To 1 minute?
                        break;
                    }
                    time_t l = h.last_msg;
                    struct tm *t = localtime(&l);
                    PRINTD(1, 0, "MAIN", "Host %s is offline, last heard from at %s", h.address.c_str(), asctime(t));
                    h.online = false;
                    now_offline.push_back(h.id);
                    check_services(h.id, false);
                }
            }
        }

        if (now_offline.size() > 0) {
            ITERVECTOR(now_offline, it)
                hosts_online.erase(std::remove(VECTORFIND(hosts_online, *it)), hosts_online.end());
        }/*}}}*/

        // Check keepalive timer/*{{{*/
        if (get_cur_time() - last_keepalive_update > interval) {
            PRINTD(5, 0, "MAIN", "Sending keepalive");
            last_keepalive_update = get_cur_time();
            queue_keepalive();
        }/*}}}*/

        // Check file timestamps /*{{{*/
        ITERVECTOR(sync_files, it) {
            string name = *it;
            if (get_file_mtime((char*)name.c_str()) != sync_timestamps[name]) {
                PRINTD(3, 0, "MAIN", "Detected change in file %s", name.c_str());
                sync_timestamps[name] = get_file_mtime((char*)name.c_str());
                send_file(name);
            }
        }/*}}}*/

        usleep((float)interval / 2.0 * 100000.0);
    }/*}}}*/

    PRINTD(1, 0, "MAIN", "Exiting main loop");
    
    return 0;
}/*}}}*/
