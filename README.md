# Cluster

The goal of this program is to provide a high availability service across 
hosts that are not necessarily in the same subnet.

Features:
 *  Service failover
 *  Dynamic host detection
 *  File synchronization for given services
 *  Alerts on loss of connection on monitored host

## Status
Cluster is currently in the beginning phases of development. There will
be many bugs and missing features.

## Installation
Use the included Makefile. The following should suffice.

```
make
make install
```

## General information
Cluster works by creating connections between various machines with
static or dynamic addresses. It listens for connection on port 35790
and handles system commands with DBus. It relies on libdbus2, libevent2,
and libconfuse.

## Configuration files
There are several configuration files which <b>NUST</b> be modified.

### hosts
This is a list of hosts. The order is arbitrary, but important. The
line number the host is on uniquely identifies that host. Each line
should have exactly one of two format entries. The line will contain
either an IP address/URL a host will be found at OR a name for the
host with a trailing comma, indicating the host's address is unknown
and it will connect to the known hosts. Consider this case:
```
There are two laptops and a server. The server and second laptop
are connected, the first is not.

        S
         \
      L   P

L connects to the internet. The following connection is established:

        S
       / \
      L   P

Since L and P cannot know eachother's address, S will now send P the
location from which L connected, so the final graph is:

        S
       / \
      L - P
```

### services
The services file should be a newline-separated file of SystemV
init scripts. Each line will be in the format:
```
<service name> <primary host> <secondary host> ....
```
where each host is the numerical ID given in the hosts file.
Assuming the primary host is up, that is where the service will
be started, otherwise it will fall through secondary all the way
to the end of the line. If no hosts are up, the service will not
run on any machine.

### cluster.conf
This is the main configuration file. The options here include
elapsed time before considering a host down, frequency of
keepalive packets, log verbosity, etc.

## DBus Handlers
Cluster will register several DBus handlers. These will do things
such as send configuration and program file updates, reload config
files, and notify other hosts of status changes.
