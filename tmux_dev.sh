#!/bin/bash
tmux -S /tmp/cluster_dev new-session -d -s cluster_dev "vi cluster.cpp" \; rename-window cpp
tmux -S /tmp/cluster_dev new-window -t cluster_dev -n common "vi common.cpp" \; split-window -h \; select-layout main-vertical \; send-keys -t 1 "vi common.h" Enter
tmux -S /tmp/cluster_dev new-window -t cluster_dev -n net "vi network.cpp" \; split-window -h \; select-layout main-vertical \; send-keys -t 1 "vi network.h" Enter
tmux -S /tmp/cluster_dev new-window -t cluster_dev -n host "vi host.cpp" \; split-window -h \; select-layout main-vertical \; send-keys -t 1 "vi host.h" Enter
tmux -S /tmp/cluster_dev new-window -t cluster_dev -n service "vi service.cpp" \; split-window -h \; select-layout main-vertical \; send-keys -t 1 "vi service.h" Enter
tmux -S /tmp/cluster_dev new-window -t cluster_dev -n conf "vi cluster.conf" \; split-window -h \; send-keys -t 1 "vi hosts" Enter \; split-window -v \; send-keys -t 2 "vi services" Enter
tmux -S /tmp/cluster_dev new-window -t cluster_dev -n make \; split-window -h \; select-layout main-vertical \; send-keys -t 1 "vi Makefile" Enter
tmux -S /tmp/cluster_dev new-window -t cluster_dev -n todo \; send-keys "grep -iHn todo *" Enter
#tmux split-window
