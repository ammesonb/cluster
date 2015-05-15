#ifndef CLUSTER_COMMON_H
#define CLUSTER_COMMON_H

#define   PRINTD(level, d_str, args...) if (debug >= level) printf("DEBUG%d: ", level); printf(d_str, ##args); printf("\n");

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <string.h>
#include <sys/time.h>
#include <dbus/dbus.h>

extern DBusConnection *conn;
extern char *DBUS_PATH;
extern char *DBUS_NAME;
extern char *DBUS_HANDLER_PATH;
extern char *DBUS_HANDLER_NAME;

extern int debug;
char* create_str(int length);
unsigned long long get_cur_time();
char* read_file(char *name);
#endif
