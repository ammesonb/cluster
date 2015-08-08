LINK_FLAGS = $(shell pkg-config --cflags --libs dbus-1 libevent libconfuse)
CFLAGS = $(shell pkg-config --cflags dbus-1)

all:
	clang++ -Wall -g $(CFLAGS) cluster.cpp common.cpp -o cluster.out -pthread $(LINK_FLAGS)
