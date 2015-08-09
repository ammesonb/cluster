#include "common.h"

#include <string>
#include <fstream>
#include <streambuf>
#include <vector>

using std::vector;
using std::string;
using std::ifstream;

namespace Cluster {
    const char *DBUS_PATH = "/com/bammeson/cluster";
    const char *DBUS_NAME = "com.bammeson.cluster";
    const char *DBUS_HANDLER_PATH = "/com/bammeson/clusterhandler";
    const char *DBUS_HANDLER_NAME = "com.bammeson.clusterhandler";

    int string_split_level = -1;
    vector<int> last_string_split_offset;
    vector<int> string_split_offset;
    vector<string> string_split_source;
    vector<string> string_split_delim;

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
        PRINTD(5, "Starting split level %d", string_split_level);
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
        //PRINTD(5, "Found token at %d for delimiter %d", sp_getsrc().find(sp_getdel(), sp_getoff()), sp_getdel().c_str()[0]);
        last_string_split_offset[string_split_level] = sp_getoff();
        string_split_offset[string_split_level] = sp_getsrc().find(sp_getdel(), sp_getoff());
        if (sp_getoff() > sp_getsrc().length()) string_split_offset[string_split_level] = sp_getsrc().length();
        PRINTD(5, "Returning substr from %d to %d", sp_getlastoff(), sp_getoff());
        string string_split_ret = sp_getsrc().substr(sp_getlastoff(), sp_getoff() - sp_getlastoff());
        if (sp_getoff() < sp_getsrc().length())
            string_split_offset[string_split_level] = sp_getoff() + 1;
        return string_split_ret;
    }/*}}}*/

    void end_split(int level) {/*{{{*/
        if (string_split_level != level) return;
        PRINTD(5, "Ending split level %d", string_split_level);
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
        PRINTD(5, "5ttempting to read file %s", name);
        ifstream f(name);
        string str;
        f.seekg(0, std::ios::end);
        str.reserve(f.tellg());
        f.seekg(0, std::ios::beg);

        str.assign((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        return str;
    }/*}}}*/
}
