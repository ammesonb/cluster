#include <time.h>
#include <sstream>
#include <string>
#include "totp.h"
#include "common.h"
#include "hmac.h"

using std::stringstream;
using std::string;

string calculate_totp(string pw, string key) {
    string data;
    data.append(key);
    string timestr;
    stringstream strstr;
    strstr << 1L;
    strstr << time(NULL)/30;;
    data.append(strstr.str());
    unsigned char *hash = hmac_sha1((unsigned char*)pw.c_str(), pw.length(), (unsigned char*)data.c_str(), data.length());
    int offset = (hash[20] << 4);
    int extract = 0;
    for (int i = offset; i < (offset + 4); i++) {
        extract += (hash[i] >> (i - offset));
    }

    return Cluster::hexlify(std::to_string(extract));
}
