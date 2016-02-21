#ifndef __TOTP_H
#define __TOTP_H
#include <vector>
#include <string>
using std::vector;
using std::string;

namespace Cluster {
    vector<string> calculate_totp(string pw, string key);
    string get_totp(string pw, string key, time_t tm);
}
#endif
