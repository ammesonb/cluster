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

using std::map;
using std::vector;
using std::string;
using std::ifstream;

namespace Cluster {
    int PRINTD_INDENT_LEVEL = 0;

    // TODO consider using keys instead of numeric arrays for split level
    map<string, int> last_string_split_offset;
    map<string, int> string_split_offset;
    map<string, string> string_split_source;
    map<string, string> string_split_delim;

    static const char *hex_alpha = "0123456789ABCDEF";

    bool validate_host_config() { /*{{{*/
        bool valid = true;
        string hosts = read_file(STRLITFIX("hosts"));
        start_split(hosts, "\n", STRLITFIX("valhost"));
        string ho = get_split(STRLITFIX("valhost"));
        int host_num = 0;
        while (ho.length() > 0) {
            start_split(ho, " ", STRLITFIX("checkhost"));
            string hostname = get_split(STRLITFIX("checkhost"));
            if (hostname.length() == 0) {valid = false; PRINTD(0, 0, "CONF", "Bad formatting for host on line %d, requires hostname", host_num);}
            string host_port = get_split(STRLITFIX("checkhost"));
            if (host_port.length() == 0) {valid = false; PRINTD(0, 0, "CONF", "Bad formatting for host on line %d, requires port", host_num);}
            string host_pass = get_split(STRLITFIX("checkhost"));
            if (host_pass.length() == 0) {valid = false; PRINTD(0, 0, "CONF", "Bad formatting for host on line %d, host requires password for validation", host_num);}

            if (get_split(STRLITFIX("checkhost")).length() != 0) {valid = false; PRINTD(0, 0, "CONF", "Bad formatting for host on line %d, extra data found", host_num);}
            end_split(STRLITFIX("checkhost"));
            ho = get_split(STRLITFIX("valhost"));
            host_num++;
        }
        end_split(STRLITFIX("valhost"));
        return valid;
    } /*}}}*/

    void load_host_config() { /*{{{*/
        if (!validate_host_config()) {PRINTD(0, 0, "CONF", "Found invalid host configuration!"); return;}
        string hosts = read_file(STRLITFIX("hosts"));
        start_split(hosts, "\n", STRLITFIX("loadhosts"));
        string ho = get_split(STRLITFIX("loadhosts"));
        int host_num = 0;
        while (ho.length() > 0) {
            Host host;
            start_split(ho, " ", STRLITFIX("loadhost"));
            string hostname = get_split(STRLITFIX("loadhost"));
            PRINTD(3, 2, "CONF", "Parsing host %s", hostname.c_str());
            host.address = hostname;
            host.id = host_num;
            string host_port = get_split(STRLITFIX("loadhost"));
            host.port = stoi(host_port);
            PRINTD(4, 3, "CONF", "Host is on port %d", host.port);
            bool dyn = false;
            if (is_ip(hostname)) {dyn = true; PRINTDI(3, "CONF", "Host is dynamic");}
            host.dynamic = dyn;
            string host_pass = get_split(STRLITFIX("loadhost"));

            host.password = host_pass;
            end_split(STRLITFIX("loadhost"));
            ho = get_split(STRLITFIX("loadhosts"));
            host_list[host_num] = host;
            host_num++;
        } 
        end_split(STRLITFIX("loadhosts"));
    } /*}}}*/

    bool validate_service_config() {/*{{{*/
        bool valid = true;
        string services = read_file(STRLITFIX("services"));
        start_split(services, "\n", STRLITFIX("valservices"));
        string serv = get_split(STRLITFIX("valservices"));
        int serv_num = 0;
        while (serv.length() > 0) {
            start_split(serv, " ", STRLITFIX("valservice"));
            string servname = get_split(STRLITFIX("valservice"));
            if (servname.length() == 0) {valid = false; PRINTD(0, 0, "CONF", "Bad formatting for service on line %d, requires name", serv_num);}                     
            string host1 = get_split(STRLITFIX("valservice"));
            if (host1.length() == 0) {valid = false; PRINTD(0, 0, "CONF", "Bad formatting for service on line %d, requires at least one host", serv_num);}
            int hosts_found = 1;
            while (split_active(STRLITFIX("valservice"))) {
                string host = get_split(STRLITFIX("valservice"));
                if (hosts_found == 1 && host.length() == 0) PRINTD(2, 3, "CONF", "Service %s only has one host! Consider adding another for redundancy.", servname.c_str());
                if (host.length() == 0) break;
                hosts_found++;
            }
            end_split(STRLITFIX("valservice"));
            serv = get_split(STRLITFIX("valservices"));
            serv_num++;
        }
        end_split(STRLITFIX("valservices"));
        return valid;
    }/*}}}*/

    void load_service_config() {/*{{{*/
        if (!validate_service_config()) {PRINTD(0, 0, "CONF", "Found invalid service configuration!"); return;}
        string services = read_file(STRLITFIX("services"));
        start_split(services, "\n", STRLITFIX("loadservices"));
        string serv = get_split(STRLITFIX("loadservices"));
        int serv_num = 0;
        map<int, vector<Service>> host_services;
        while (serv.length() > 0) {
            // Get service details
            Service service;
            vector<Host> serv_hosts;
            start_split(serv, " ", STRLITFIX("loadservice"));
            string servname = get_split(STRLITFIX("loadservice"));
            service.name = servname;
            PRINTD(3, 2, "CONF", "Parsing service %s", servname.c_str());
            string host1 = get_split(STRLITFIX("loadservice"));
            PRINTD(5, 3, "CONF", "Host %s is subscribed", host_list[stoi(host1)].address.c_str());
            serv_hosts.push_back(host_list[stoi(host1)]);
            // For each host registered with the service
            while (split_active(STRLITFIX("loadservice"))) {
                string host = get_split(STRLITFIX("loadservice"));
                if (host.length() == 0) break;
                PRINTDI(5, "CONF", "Host %s is subscribed", host_list[stoi(host)].address.c_str());
                serv_hosts.push_back(host_list[stoi(host)]);
            }
            PRINTD(4, 3, "CONF", "Found %lu hosts", serv_hosts.size());
            end_split(STRLITFIX("loadservice"));
            serv = get_split(STRLITFIX("loadservices"));
            service.hosts = serv_hosts;
            serv_list[serv_num] = service;
            // For each host this service has registered
            // Add this service to its list of services
            ITERVECTOR(serv_hosts, it)
                host_services[(*it).id].push_back(service);
            serv_num++;
        }
        end_split(STRLITFIX("loadservices"));

        // Once all services are parsed, attribute services to hosts
        PRINTD(5, 3, "CONF", "Adding services to hosts");
        ITERVECTOR(host_services, it)
            host_list[(*it).first].services = (*it).second;
    } /*}}}*/

    char* create_str(int length) {/*{{{*/
        char *s = (char*)malloc(sizeof(char) * (length + 1));
        memset(s, '\0', length + 1);
        return s;
    }/*}}}*/

    void check_services(int hostid, bool online) {/*{{{*/
        Host h = host_list[int_id];
        ITERVECTOR(h.services, it) {
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
        PRINTDI(5, "COMMON", "Getting directory list for %s", dir);
        vector<string> files;

        DIR *d;
        struct dirent *ent;
        if ((d = opendir(dir)) != NULL) {
            while ((ent = readdir(d)) != NULL) {
                if (ent->d_type == DT_DIR) {
                    if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
                    PRINTDR(5, 1, "COMMON", "Found directory %s", ent->d_name);
                    vector<string> sub_files = get_directory_files(ent->d_name);
                    files.insert(files.end(), sub_files.begin(), sub_files.end());
                    continue;
                }
                if (strcmp(ent->d_name, "start") == 0 || strcmp(ent->d_name, "stop") == 0) continue;
                PRINTDR(5, 1, "COMMON", "Found file %s", ent->d_name);
                files.push_back(string(dir).append(string(ent->d_name)));
            }
        } else {
            PRINTDI(1, "COMMON", "Failed to list directory %s", dir);
        }
        closedir(d);
        PRINTDI(5, "COMMON", "Directory %s had %lu files", dir, files.size());
        return files;
    }/*}}}*/

    // String split getters/*{{{*/
    string sp_getsrc(string key) {
        return string_split_source.at(key);
    }

    string sp_getdel(string key) {
        return string_split_delim.at(key);
    }

    int sp_getoff(string key) {
        return string_split_offset.at(key);
    }

    int sp_getlastoff(string key) {
        return last_string_split_offset.at(key);
    }/*}}}*/

    void start_split(string src, string delim, char *k) {/*{{{*/
        string key = string(k);
        PRINTDI(5, "COMMON", "Starting split level %s", (char*)key.c_str());
        string_split_source[key] = src;
        string_split_delim[key] = delim;
        string_split_offset[key] = 0;
        last_string_split_offset[key] = 0;
    }/*}}}*/

    string get_split(char *k) {/*{{{*/
        string key = string(k);
        if (sp_getsrc(key).find(sp_getdel(key), sp_getoff(key)) > sp_getsrc(key).length() && sp_getsrc(key).length() - sp_getoff(key) == 0) {
            end_split(k);
            return "";
        }
        //PRINTDI(5, 0, "COMMON", "Found token at %d for delimiter %d", sp_getsrc().find(sp_getdel(), sp_getoff()), sp_getdel().c_str()[0]);
        last_string_split_offset[key] = sp_getoff(key);
        string_split_offset[key] = sp_getsrc(key).find(sp_getdel(key), sp_getoff(key));
        if (sp_getoff(key) > sp_getsrc(key).length()) string_split_offset[key] = sp_getsrc(key).length();
        PRINTDI(5, "COMMON", "Returning substr from %d to %d", sp_getlastoff(key), sp_getoff(key));
        string string_split_ret = sp_getsrc(key).substr(sp_getlastoff(key), sp_getoff(key) - sp_getlastoff(key));
        if (sp_getoff(key) < sp_getsrc(key).length())
            string_split_offset[key] = sp_getoff(key) + sp_getdel(key).length();
        return string_split_ret;
    }/*}}}*/

    bool split_active(char *k) {/*{{{*/
        string key = string(k);
        return string_split_source.count(key) == 1;
    }/*}}}*/

    void end_split(char *k) {/*{{{*/
        string key = string(k);
        PRINTDI(5, "COMMON", "Ending split level %s", (char*)key.c_str());
        last_string_split_offset.erase(key);
        string_split_offset.erase(key);
        string_split_source.erase(key);
        string_split_delim.erase(key);
    }/*}}}*/

    time_t get_cur_time() {/*{{{*/
        struct timeval cur_time;
        gettimeofday(&cur_time, NULL);
        return cur_time.tv_sec;
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
        if (data.length() & 1) {PRINTD(1, 0, "COMMON", "Invalid length for unhexlify, got %lu", data.length()); return string("");}
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

    string filename(string fname) {/*{{{*/
        const size_t last_sep_idx = fname.rfind('/');
        if (std::string::npos != last_sep_idx) {
            return fname.substr(last_sep_idx, fname.length() - last_sep_idx);
        }
        return string();
    }/*}}}*/

    string dirname(string fname) {/*{{{*/
        const size_t last_sep_idx = fname.rfind('/');
        if (std::string::npos != last_sep_idx) {
            return fname.substr(0, last_sep_idx);
        }
        return string();
    }/*}}}*/

    string read_file(char *name) {/*{{{*/
        PRINTDI(5, "COMMON", "Attempting to read file %s", name);
        ifstream f(name);
        string str;
        f.seekg(0, std::ios::end);
        str.reserve(f.tellg());
        f.seekg(0, std::ios::beg);

        str.assign((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        PRINTDI(5, "COMMON", "Read %lu bytes", str.length());
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
