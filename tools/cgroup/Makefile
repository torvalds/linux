# SPDX-License-Identifier: GPL-2.0
# Makefile for cgroup tools

CC = $(CROSS_COMPILE)gcc
CFLAGS = -Wall -Wextra

all: cgroup_event_listener
%: %.c
	$(CC) $(CFLAGS) -o $@ $^

clean:
	$(RM) cgroup_event_listener
