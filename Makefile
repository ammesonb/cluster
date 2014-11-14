DBUS_FLAGS = $(shell pkg-config dbus-1 --cflags)

all:
	clang -Wall cluster.c -o cluster.out $(DBUS_FLAGS) -levent -ldbus-1 -lconfuse
