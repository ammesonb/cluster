#include "common.h"

char *DBUS_PATH = "/com/bammeson/cluster";
char *DBUS_NAME = "com.bammeson.cluster";
char *DBUS_HANDLER_PATH = "/com/bammeson/clusterhandler";
char *DBUS_HANDLER_NAME = "com.bammeson.clusterhandler";

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

char* read_file(char *name) {/*{{{*/
    PRINTD(3, "Attempting to read file %s", name);
    struct stat *s = NULL;
    s = (struct stat*)malloc(sizeof(struct stat));
    stat(name, s);
    int file = open(name, O_RDONLY);
    char *data = create_str(s->st_size);
    read(file, data, s->st_size);
    close(file);
    free(s);
    return data;
}/*}}}*/
