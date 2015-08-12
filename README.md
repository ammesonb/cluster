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
libconfuse, libcrypto, and pthread.

## Configuration files
There are several configuration files which <b>MUST</b> be modified.

### hosts
This is a list of hosts. The order is arbitrary, but important. The
line number the host is on uniquely identifies that host. Each line
should have exactly one of two format entries. The line will contain
either an IP address/URL a host will be found at OR a name for the
host, indicating the host's address is unknown and it will connect
to the known hosts. In this case, a passphrase should be included,
separated by a comma. A sample configuration file may look like:
```
server1.example.com password1
server2.example.com password2
laptop1,open password3
```
Note the initial passphrase must contain at least 8 characters.
This will be updated automatically every hour.

### services
The services file should be a newline-separated file of services
that should be distributed. Each line will be in the format:
```
<service name> <primary host> <secondary host> ....
```
where each host is the numerical ID given in the hosts file.
Assuming the primary host is up, that is where the service will
be started, otherwise it will fall through secondary all the way
to the end of the line. If no hosts are up, the service will not
run on any machine. A directory should be created for each service
with a bash 'start' and 'stop' script, similar to the SystemV functions.
Any other files may be specified there as well.

### cluster.conf
This is the main configuration file. The options here include
elapsed time before considering a host down, frequency of
keepalive packets, log verbosity, etc. Of note is critical files and
directories. Any files you wish to synchronize between hosts may be
added here. Any files in the service directories will also by shared,
though the start/stop scripts will *NOT* be included by default
since different hosts may require different actions to start a service.

## DBus Handlers
Cluster will register several DBus handlers. These will perform actions
such as sending configuration and program file updates, reloading config
files, and notifying other hosts of status changes.

## Example connection behavior
```
There are two laptops and a server. The server and second laptop
are connected, the first laptop is not. Services are stable.

        S
         \
      L   P

L connects to the internet. The following connection is established,
where L sends a username and passphrase to validate itself with S.
S will stop any services on which L has a higher priority, L will
stop any services for which S has a higher priority.

        S
       / \
      L   P

Since L and P cannot know the other's address via the configuration file,
S will now send P the location from which L connected. L will not attempt
to connect to P. Authentication is still necessary. P will stop any services
for which L has higher priority, and L will do the same. All nodes are
connected, all services are stable.

        S
       / \
      L - P
```

