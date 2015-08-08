#ifndef _DBUS_LIB_H
#define _DBUS_LIB_H
#include <dbus/dbus.h>

#define   MAX_WATCHES    256
#define   MAX_EVENT_NAME 256

#define DBUS_FUNC(function) \
    static DBusHandlerResult function(DBusConnection *conn, DBusMessage *dbmsg, void *_this)


#define DBUS_CHECK_ERR(str, err) \
    if (dbus_error_is_set(&err)) { \
        fprintf(stderr, "%s%s\n", str, err.message); \
        dbus_error_free(&err); \
        return DBUS_HANDLER_RESULT_NEED_MEMORY; \
    }

#define DBUS_INIT(name, path, message_func) \
    DBusError dbus_init_error; \
    dbus_error_init(&dbus_init_error); \
    conn = dbus_bus_get(DBUS_BUS_SESSION, &dbus_init_error); \
    DBUS_CHECK_ERR("Failed to get session bus: ", dbus_init_error); \
    dbus_bus_request_name(conn, name, DBUS_NAME_FLAG_REPLACE_EXISTING, &dbus_init_error); \
    DBUS_CHECK_ERR("Failed to get requested name: ", dbus_init_error); \
    DBusObjectPathVTable vtable = {NULL, message_func, NULL, NULL, NULL, NULL}; \
    dbus_connection_register_object_path(conn, path, &vtable, &dbus_init_error); \
    DBUS_CHECK_ERR("Failed to register object path: ", dbus_init_error); \
    dbus_error_free(&dbus_init_error);

#define DBUS_SET_WATCH(add, rm, tog, arg) \
    if (!dbus_connection_set_watch_functions(conn, add, rm, tog, arg, NULL)) \
        return DBUS_HANDLER_RESULT_NEED_MEMORY;
    
#define DBUS_ADD_FILTER(func, arg) \
    if (dbus_connection_add_filter(conn, func, arg, NULL) == FALSE) \
        return DBUS_HANDLER_RESULT_NEED_MEMORY;

#define DBUS_SET_TIMEOUT(add, rm, tog, arg) \
    if (!dbus_connection_set_timeout_functions(conn, add, rm, tog, arg, NULL)) \
        return DBUS_HANDLER_RESULT_NEED_MEMORY;

#define DBUS_ASYNC_DISPATCH(func, arg) \
    dbus_connection_set_dispatch_status_function(conn, func, arg, NULL);

#define DBUS_ADD_MATCH(match) \
    DBusError err; \
    dbus_error_init(&err); \
    dbus_bus_add_match(conn, match, &err); \
    DBUS_CHECK_ERR("Failed to add match", err); \
    dbus_error_free(&err);

#define DBUS_SIG(function) \
    static DBusHandlerResult function(DBusConnection *conn, void *data)

#define DBUS_REPLY_INIT \
    DBusMessage *db_reply_msg = dbus_message_new_method_return(dbmsg); \
    if (!db_reply_msg) return DBUS_HANDLER_RESULT_NEED_MEMORY;

#define DBUS_REPLY_SEND(msg) \
    if (!dbus_connection_send(conn, msg, NULL)) \
        return DBUS_HANDLER_RESULT_NEED_MEMORY; \
    dbus_connection_flush(conn); \
    dbus_message_unref(msg);

#define DBUS_SIGNAL_INIT \
    DBusMessage *db_reply_message = dbus_message_new_signal(path, iface, signal); \
    if (!dbmsg) return DBUS_HANDLER_RESULT_NEED_MEMORY;

#define DBUS_SIGNAL_SEND \
    if (!dbus_connection_send(conn, db_reply_msg, NULL)) \
        return DBUS_HANDLER_RESULT_NEED_MEMORY; \
    dbus_message_unref(db_reply_msg); \
    dbus_connection_flush(conn); \
    return DBUS_HANDLER_RESULT_HANDLED;

#define DBUS_INIT_METHOD_CALL(obj, path, iface, method) \
    DBusMessage *db_call_msg = dbus_message_new_method_call(obj, path, iface, method); \
    if (db_call_msg == NULL) { \
        fprintf(stderr, "Failed to create method call"); \
        return DBUS_HANDLER_RESULT_NEED_MEMORY; \
    }

#define DBUS_ADD_ARGS_SIMPLE(msg, ...) \
    dbus_message_append_args(msg, ##__VA_ARGS__, DBUS_TYPE_INVALID);

#define DBUS_ADD_ARGS(msg) \
    DBusMessageIter args; \
    dbus_message_iter_init_append(msg, &args);

#define DBUS_ADD(type, value) \
    if (!dbus_message_iter_append_basic(&args, type, value)) \
        return DBUS_HANDLER_RESULT_NEED_MEMORY; \

#define DBUS_ADD_STRING(s) DBUS_ADD(DBUS_TYPE_STRING, s)
#define DBUS_ADD_DOUBLE(s) DBUS_ADD(DBUS_TYPE_DOUBLE, s)
#define DBUS_ADD_BOOL(s) DBUS_ADD(DBUS_TYPE_BOOLEAN, s)
#define DBUS_ADD_INT32(s) DBUS_ADD(DBUS_TYPE_INT32, s)
#define DBUS_ADD_INT64(s) DBUS_ADD(DBUS_TYPE_INT64, s)
#define DBUS_ADD_BYTE(s) DBUS_ADD(DBUS_TYPE_BYTE, s)

#define DBUS_GET_ARGS(...) \
    DBusError dbus_args_error; \
    dbus_error_init(&dbus_args_error); \
    dbus_message_get_args(dbmsg, &dbus_args_error, __VA_ARGS__, DBUS_TYPE_INVALID); \
    DBUS_CHECK_ERR("Failed to get method arguments", dbus_args_error) \
    dbus_error_free(&dbus_args_error);

#define DBUS_INTROSPEC \
    DBUS_REPLY_INIT \
    DBUS_ADD_ARGS(db_reply_msg) \
    DBUS_ADD_STRING(&introspec_xml) \
    DBUS_REPLY_SEND(db_reply_msg)

namespace Cluster {
    void dispatch(int, short, void*);
    void handle_dispatch_status(DBusConnection*, DBusDispatchStatus, void*);
    void handle_watch(int, short, void*);
    dbus_bool_t add_watch(DBusWatch*, void*);
    void remove_watch(DBusWatch*, void*);
    void toggle_watch(DBusWatch*, void*);
    void handle_timeout(int, short, void*);
    dbus_bool_t add_timeout(DBusTimeout*, void*);
    void remove_timeout(DBusTimeout*, void*);
    void toggle_timeout(DBusTimeout*, void*);
    DBUS_FUNC(message_handler);
    DBUS_FUNC(message_filter);

    extern int watches;
    extern int event_fds[];
    extern char **event_names;
}

#endif
