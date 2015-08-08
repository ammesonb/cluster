#include "common.h"
#include <string>
#include <fstream>
#include <streambuf>

using std::string;
using std::ifstream;

namespace Cluster {
    const char *DBUS_PATH = "/com/bammeson/cluster";
    const char *DBUS_NAME = "com.bammeson.cluster";
    const char *DBUS_HANDLER_PATH = "/com/bammeson/clusterhandler";
    const char *DBUS_HANDLER_NAME = "com.bammeson.clusterhandler";

    char* create_str(int length) {/*{{{*/
        char *s = (char*)malloc(sizeof(char) * (length + 1));
        memset(s, '\0', length + 1);
        return s;
    }/*}}}*/

    unsigned long long get_cur_time() {/*{{{*/
        struct timeval cur_time;
        gettimeofday(&cur_time, NULL);
        unsigned long long cur = cur_time.tv_sec * 1000;
        cur += cur_time.tv_usec;
        return cur;
    }/*}}}*/

    string read_file(char *name) {/*{{{*/
        PRINTD(3, "Attempting to read file %s", name);
        ifstream f(name);
        string str;
        f.seekg(0, std::ios::end);
        str.reserve(f.tellg());
        f.seekg(0, std::ios::beg);

        str.assign((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        return str;
    }/*}}}*/
}
