# SPDX-License-Identifier: GPL-2.0
# Makefile for cgroup tools

CFLAGS = -Wall -Wextra

all: cgroup_event_listener
%: %.c
	$(CC) $(CFLAGS) -o $@ $^

clean:
	$(RM) cgroup_event_listener
