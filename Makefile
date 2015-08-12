LINK_FLAGS = $(shell pkg-config --cflags --libs dbus-1 libevent libconfuse)
CFLAGS = $(shell pkg-config --cflags dbus-1)

all:
	clang++ -std=c++11 -Wall -g $(CFLAGS) cluster.cpp common.cpp network.cpp -o cluster.out $(LINK_FLAGS) -lcrypto -pthread
