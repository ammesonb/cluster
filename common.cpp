#include "common.h"

#include <string>
#include <fstream>
#include <streambuf>
#include <vector>
#include <algorithm>
#include <functional>
#include <cctype>
#include <locale>
#include <openssl/evp.h>
#include <dirent.h>

using std::vector;
using std::string;
using std::ifstream;

namespace Cluster {
    int PRINTD_INDENT_LEVEL = 0;

    int string_split_level = -1;
    vector<int> last_string_split_offset;
    vector<int> string_split_offset;
    vector<string> string_split_source;
    vector<string> string_split_delim;

    static const char *hex_alpha = "0123456789ABCDEF";

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
        map<int, vector<Service>> host_services;
        while (serv.length() > 0) {
            // Get service details
            Service service;
            vector<Host> serv_hosts;
            start_split(serv, " ");
            string servname = get_split();
            service.name = servname;
            PRINTD(3, 2, "Parsing service %s", servname.c_str());
            string host1 = get_split();
            PRINTD(5, 3, "Host %s is subscribed", host_list[stoi(host1)].address.c_str());
            serv_hosts.push_back(host_list[stoi(host1)]);
            // For each host registered with the service
            while (get_split_level() == 1) {
                string host = get_split();
                if (host.length() == 0) break;
                PRINTDI(5, "Host %s is subscribed", host_list[stoi(host)].address.c_str());
                serv_hosts.push_back(host_list[stoi(host)]);
            }
            PRINTD(4, 3, "Found %lu hosts", serv_hosts.size());
            end_split(1);
            serv = get_split();
            service.hosts = serv_hosts;
            serv_list[serv_num] = service;
            // For each host this service has registered
            // Add this service to its list of services
            for (auto it = serv_hosts.begin(); it != serv_hosts.end(); it++)
                host_services[(*it).id].push_back(service);
            serv_num++;
        }

        // Once all services are parsed, attribute services to hosts
        PRINTD(5, 3, "Adding services to hosts");
        for (auto it = host_services.begin(); it != host_services.end(); it++) {
            host_list[(*it).first].services = (*it).second;
        }
    } /*}}}*/

    char* create_str(int length) {/*{{{*/
        char *s = (char*)malloc(sizeof(char) * (length + 1));
        memset(s, '\0', length + 1);
        return s;
    }/*}}}*/

    void check_services(int hostid, bool online) {/*{{{*/
        Host h = host_list[int_id];
        for (auto it = h.services.begin(); it != h.services.end(); it++) {
            Service s = *it;
            s.start_stop(hostid, online);
        }
    }/*}}}*/

    bool is_ip(string s) {/*{{{*/
        bool ip = true;
        for (int i = 0; i < s.length(); i++) {
            ip &= (s[i] == '.' || ('0' <= s[i] && s[i] <= '9'));
            if (!ip) return false;
        }
        return ip;
    }/*}}}*/

    vector<string> get_directory_files(char *dir) {/*{{{*/
        PRINTDI(5, "Getting directory list for %s", dir);
        vector<string> files;

        DIR *d;
        struct dirent *ent;
        if ((d = opendir(dir)) != NULL) {
            while ((ent = readdir(d)) != NULL) {
                if (ent->d_type == DT_DIR) {
                    if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
                    PRINTDR(5, 1, "Found directory %s", ent->d_name);
                    vector<string> sub_files = get_directory_files(ent->d_name);
                    files.insert(files.end(), sub_files.begin(), sub_files.end());
                    continue;
                }
                if (strcmp(ent->d_name, "start") == 0 || strcmp(ent->d_name, "stop") == 0) continue;
                PRINTDR(5, 1, "Found file %s", ent->d_name);
                files.push_back(string(dir).append(string(ent->d_name)));
            }
        } else {
            PRINTDI(1, "Failed to list directory %s", dir);
        }
        closedir(d);
        PRINTDI(5, "Directory %s had %lu files", dir, files.size());
        return files;
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

    void set_split_level(int l) {/*{{{*/
        string_split_level = l;
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

    string hexlify(string data) {/*{{{*/
        int len = data.length();
        string out;
        out.reserve(2 * len);
        for (int i = 0; i < len; i++) {
            const unsigned char c = data[i];
            out.push_back(hex_alpha[c >> 4]);
            out.push_back(hex_alpha[c & 0x0F]);
        }
        return out;
    }/*}}}*/

    string hexlify(unsigned char *data, int len) {/*{{{*/
        string out;
        out.reserve(2 * len);
        for (int i = 0; i < len; i++) {
            const unsigned char c = data[i];
            out.push_back(hex_alpha[c >> 4]);
            out.push_back(hex_alpha[c & 0x0F]);
        }
        return out;
    }/*}}}*/

    string unhexlify(string data) {/*{{{*/
        if (data.length() & 1) {PRINTD(1, 0, "Invalid length for unhexlify"); return string("");}
        string out;
        out.reserve(data.length() / 2);
        for (int i = 0; i < data.length(); i += 2) {
            const char *p = std::lower_bound(hex_alpha, hex_alpha + 16, data[i]);
            const char *q = std::lower_bound(hex_alpha, hex_alpha + 16, data[i + 1]);
            out.push_back(((p - hex_alpha) << 4) | (q - hex_alpha));
        }
        return out;
    }/*}}}*/

    string hash_file(char *name) {/*{{{*/
        string data = read_file(name);
        EVP_MD_CTX *mdctx;
        unsigned char *md_value = (unsigned char*)create_str(EVP_MAX_MD_SIZE);
        unsigned int md_len;

        OpenSSL_add_all_digests();
        mdctx = EVP_MD_CTX_create();
        EVP_DigestInit_ex(mdctx, EVP_sha256(), NULL);
        EVP_DigestUpdate(mdctx, data.c_str(), data.length());
        EVP_DigestFinal_ex(mdctx, md_value, &md_len);
        EVP_MD_CTX_destroy(mdctx);
        string hash;
        hash.reserve(md_len);
        hash.assign((char*)md_value);
        return hexlify(hash);
    }/*}}}*/

    string read_file(char *name) {/*{{{*/
        PRINTDI(5, "Attempting to read file %s", name);
        ifstream f(name);
        string str;
        f.seekg(0, std::ios::end);
        str.reserve(f.tellg());
        f.seekg(0, std::ios::beg);

        str.assign((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        PRINTDI(5, "Read %lu bytes", str.length());
        return str;
    }/*}}}*/

    time_t get_file_mtime(char *name) {/*{{{*/
        struct stat sb;
        stat(name, &sb);
        return sb.st_mtime;
    }/*}}}*/

    string ltrim(string s) {/*{{{*/
            s.erase(s.begin(), std::find_if(s.begin(), s.end(), std::not1(std::ptr_fun<int, int>(std::isspace))));
            return s;
    }/*}}}*/

    string rtrim(string s) {/*{{{*/
            s.erase(std::find_if(s.rbegin(), s.rend(), std::not1(std::ptr_fun<int, int>(std::isspace))).base(), s.end());
            return s;
    }/*}}}*/

    string trim(string s) {/*{{{*/
            return ltrim(rtrim(s));
    }/*}}}*/
}
