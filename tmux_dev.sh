#!/bin/bash
tmux -S /tmp/cluster_dev new-session -d -s cluster_dev \; send-keys "vi cluster.cpp" Enter \; rename-window cpp
tmux -S /tmp/cluster_dev new-window -t cluster_dev -n common \; send-keys "vi common.cpp" Enter \; split-window -h \; select-layout main-vertical \; send-keys -t 1 "vi common.h" Enter
tmux -S /tmp/cluster_dev new-window -t cluster_dev -n net \; send-keys "vi network.cpp" Enter \; split-window -h \; select-layout main-vertical \; send-keys -t 1 "vi network.h" Enter
tmux -S /tmp/cluster_dev new-window -t cluster_dev -n host \; send-keys "vi host.cpp" Enter \; split-window -h \; select-layout main-vertical \; send-keys -t 1 "vi host.h" Enter
tmux -S /tmp/cluster_dev new-window -t cluster_dev -n service \; send-keys "vi service.cpp" Enter \; split-window -h \; select-layout main-vertical \; send-keys -t 1 "vi service.h" Enter
tmux -S /tmp/cluster_dev new-window -t cluster_dev -n conf \; send-keys "vi cluster.conf" Enter \; split-window -h \; send-keys -t 1 "vi hosts" Enter \; split-window -v \; send-keys -t 2 "vi services" Enter
tmux -S /tmp/cluster_dev new-window -t cluster_dev -n make \; split-window -h \; select-layout main-vertical \; send-keys -t 1 "vi Makefile" Enter
tmux -S /tmp/cluster_dev new-window -t cluster_dev -n todo \; send-keys "reset; grep -iHn todo * 2>&1 | grep -i todo" Enter
tmux -S /tmp/cluster_dev new-window -t cluster_dev -n git \; send-keys "git pull" Enter
