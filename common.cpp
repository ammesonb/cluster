#include "common.h"

#include <string>
#include <fstream>
#include <streambuf>
#include <vector>
#include <algorithm>
#include <functional>
#include <cctype>
#include <locale>

using std::vector;
using std::string;
using std::ifstream;

namespace Cluster {
    int PRINTD_INDENT_LEVEL = 0;
    const char *DBUS_PATH = "/com/bammeson/cluster";
    const char *DBUS_NAME = "com.bammeson.cluster";
    const char *DBUS_HANDLER_PATH = "/com/bammeson/clusterhandler";
    const char *DBUS_HANDLER_NAME = "com.bammeson.clusterhandler";

    int string_split_level = -1;
    vector<int> last_string_split_offset;
    vector<int> string_split_offset;
    vector<string> string_split_source;
    vector<string> string_split_delim;

    bool validate_host_config() { /*{{{*/
        bool valid = true;
        string hosts = read_file(STRLITFIX("hosts"));
        start_split(hosts, "\n");
        string ho = get_split();
        int host_num = 0;
        while (ho.length() > 0) {
            start_split(ho, " ");
            string hostname = get_split();
            if (hostname.length() == 0) {valid = false; PRINTD(0, 0, "Bad formatting for host on line %d, requires hostname", host_num);}
            string host_port = get_split();
            if (host_port.length() == 0) {valid = false; PRINTD(0, 0, "Bad formatting for host on line %d, requires port", host_num);}
            string host_pass = get_split();
            if (host_pass.length() == 0) {valid = false; PRINTD(0, 0, "Bad formatting for host on line %d, host requires password for validation", host_num);}

            if (get_split().length() != 0) {valid = false; PRINTD(0, 0, "Bad formatting for host on line %d, extra data found", host_num);}
            end_split(1);
            ho = get_split();
            host_num++;
        }
        return valid;
    } /*}}}*/

    void load_host_config() { /*{{{*/
        if (!validate_host_config()) {PRINTD(0, 0, "Found invalid host configuration!"); return;}
        string hosts = read_file(STRLITFIX("hosts"));
        start_split(hosts, "\n");
        string ho = get_split();
        int host_num = 0;
        while (ho.length() > 0) {
            Host host;
            start_split(ho, " ");
            string hostname = get_split();
            PRINTD(3, 2, "Parsing host %s", hostname.c_str());
            host.address = hostname;
            host.id = host_num;
            string host_port = get_split();
            host.port = stoi(host_port);
            PRINTD(4, 3, "Host is on port %d", host.port);
            bool dyn = false;
            if (is_ip(hostname)) {dyn = true; PRINTDI(3, "Host is dynamic");}
            host.dynamic = dyn;
            string host_pass = get_split();

            host.password = host_pass;
            end_split(1);
            ho = get_split();
            host_list[host_num] = host;
            host_num++;
        } 
    } /*}}}*/

    bool validate_service_config() {/*{{{*/
        bool valid = true;
        string services = read_file(STRLITFIX("services"));
        start_split(services, "\n");
        string serv = get_split();
        int serv_num = 0;
        while (serv.length() > 0) {
            string servname = get_split();
            if (servname.length() == 0) {valid = false; PRINTD(0, 0, "Bad formatting for service on line %d, requires name", serv_num);}                     
            string host1 = get_split();
            if (host1.length() == 0) {valid = false; PRINTD(0, 0, "Bad formatting for service on line %d, requires at least one host", serv_num);}
            int hosts_found = 1;
            while (get_split_level() == 1) {
                string host = get_split();
                if (hosts_found == 1 && host.length() == 0) PRINTD(2, 3, "Service %s only has one host! Consider adding another for redundancy.", servname.c_str());
                if (host.length() == 0) break;
                hosts_found++;
            }
            end_split(1);
            serv = get_split();
            serv_num++;
        }
        return valid;
    }/*}}}*/

    void load_service_config() {/*{{{*/
        if (!validate_service_config()) {PRINTD(0, 0, "Found invalid service configuration!"); return;}
        string services = read_file(STRLITFIX("services"));
        start_split(services, "\n");
        string serv = get_split();
        int serv_num = 0;
        while (serv.length() > 0) {
            Service service;
            vector<Host> serv_hosts;
            start_split(serv, " ");
            string servname = get_split();
            PRINTD(3, 2, "Parsing service %s", servname.c_str());
            string host1 = get_split();
            PRINTD(5, 3, "Host %s is subscribed", host_list[stoi(host1)].address.c_str());
            serv_hosts.push_back(host_list[stoi(host1)]);
            int hosts_found = 1;
            while (get_split_level() == 1) {
                string host = get_split();
                if (host.length() == 0) break;
                PRINTDI(5, "Host %s is subscribed", host_list[stoi(host)].address.c_str());
                serv_hosts.push_back(host_list[stoi(host)]);
                hosts_found++;
            }
            PRINTD(4, 3, "Found %d hosts", hosts_found);
            end_split(1);
            serv = get_split();
            service.hosts = serv_hosts;
            serv_list[serv_num] = service;
            serv_num++;
        }
    } /*}}}*/

    char* create_str(int length) {/*{{{*/
        char *s = (char*)malloc(sizeof(char) * (length + 1));
        memset(s, '\0', length + 1);
        return s;
    }/*}}}*/

    bool is_ip(string s) {/*{{{*/
        bool ip = true;
        for (int i = 0; i < s.length(); i++) {
            ip &= (s[i] == '.' || ('0' <= s[i] && s[i] <= '9'));
            if (!ip) return false;
        }
        return ip;
    }/*}}}*/

    // String split getters/*{{{*/
    string sp_getsrc() {
        return string_split_source[string_split_level];
    }

    string sp_getdel() {
        return string_split_delim[string_split_level];
    }

    int sp_getoff() {
        return string_split_offset[string_split_level];
    }

    int sp_getlastoff() {
        return last_string_split_offset[string_split_level];
    }/*}}}*/

    int get_split_level() {/*{{{*/
        return string_split_level;
    }/*}}}*/

    void start_split(string s, string d) {/*{{{*/
        if (string_split_level != -1 && string_split_offset[string_split_level] != 0) string_split_level++;
        if (string_split_level == -1) string_split_level = 0;
        PRINTDI(5, "Starting split level %d", string_split_level);
        string_split_source.push_back(s);
        string_split_delim.push_back(d);
        string_split_offset.push_back(0);
        last_string_split_offset.push_back(0);
    }/*}}}*/

    string get_split() {/*{{{*/
        if (sp_getsrc().find(sp_getdel(), sp_getoff()) > sp_getsrc().length() && sp_getsrc().length() - sp_getoff() == 0) {
            end_split(string_split_level);
            return "";
        }
        //PRINTDI(5, 0, "Found token at %d for delimiter %d", sp_getsrc().find(sp_getdel(), sp_getoff()), sp_getdel().c_str()[0]);
        last_string_split_offset[string_split_level] = sp_getoff();
        string_split_offset[string_split_level] = sp_getsrc().find(sp_getdel(), sp_getoff());
        if (sp_getoff() > sp_getsrc().length()) string_split_offset[string_split_level] = sp_getsrc().length();
        PRINTDI(5, "Returning substr from %d to %d", sp_getlastoff(), sp_getoff());
        string string_split_ret = sp_getsrc().substr(sp_getlastoff(), sp_getoff() - sp_getlastoff());
        if (sp_getoff() < sp_getsrc().length())
            string_split_offset[string_split_level] = sp_getoff() + 1;
        return string_split_ret;
    }/*}}}*/

    void end_split(int level) {/*{{{*/
        if (string_split_level != level) return;
        PRINTDI(5, "Ending split level %d", string_split_level);
        last_string_split_offset.pop_back();
        string_split_offset.pop_back();
        string_split_source.pop_back();
        string_split_delim.pop_back();
        string_split_level--;
    }/*}}}*/

    unsigned long long get_cur_time() {/*{{{*/
        struct timeval cur_time;
        gettimeofday(&cur_time, NULL);
        unsigned long long cur = cur_time.tv_sec * 1000;
        cur += cur_time.tv_usec;
        return cur;
    }/*}}}*/

    string read_file(char *name) {/*{{{*/
        PRINTDI(5, "Attempting to read file %s", name);
        ifstream f(name);
        string str;
        f.seekg(0, std::ios::end);
        str.reserve(f.tellg());
        f.seekg(0, std::ios::beg);

        str.assign((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        return trim(str);
    }/*}}}*/

    static inline std::string &ltrim(std::string &s) {/*{{{*/
            s.erase(s.begin(), std::find_if(s.begin(), s.end(), std::not1(std::ptr_fun<int, int>(std::isspace))));
            return s;
    }/*}}}*/

    static inline std::string &rtrim(std::string &s) {/*{{{*/
            s.erase(std::find_if(s.rbegin(), s.rend(), std::not1(std::ptr_fun<int, int>(std::isspace))).base(), s.end());
            return s;
    }/*}}}*/

    static inline std::string &trim(std::string &s) {/*{{{*/
            return ltrim(rtrim(s));
    }/*}}}*/
}
