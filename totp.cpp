#include <time.h>
#include <sstream>
#include <string>
#include "totp.h"
#include "common.h"
#include "hmac.h"

using std::stringstream;
using std::string;

namespace Cluster {
    string calculate_totp(string pw, string key) {
        string data;
        data.append(key);
        string timestr;
        stringstream strstr;
        strstr << 1L;
        strstr << time(NULL)/30;
        data.append(strstr.str());
        unsigned char *hash = hmac_sha1((unsigned char*)pw.c_str(), pw.length(), (unsigned char*)data.c_str(), data.length());
        int offset = (hash[19] >> 4);
        printf("%d\n", offset);
        int extract = 0;
        for (int i = offset; i < (offset + 4); i++) {
            extract += (hash[i] << (8 * (i - offset)));
        }
        printf("%d\n", extract);

        string res = hexlify((unsigned char*)&extract, 32);
        PRINTD(4, 0, "Using time %s, key %s returned key %s", (char*)strstr.str().c_str(), (char*)key.c_str(), (char*)res.c_str());
        return res;
    }
}
