#ifndef __TOTP_H
#define __TOTP_H
#include <string>
using std::string;

string calculate_totp(string pw, string key);
#endif
