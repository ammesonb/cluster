LINK_FLAGS = $(shell pkg-config --cflags --libs dbus-1 libevent libconfuse)

all:
	clang -Wall -g cluster.c -o cluster.out $(LINK_FLAGS) -pthread
