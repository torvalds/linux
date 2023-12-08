# SPDX-License-Identifier: GPL-2.0
CC = gcc
CFLAGS = -O2 -Wall -pthread

all: trace-agent

.c.o:
	$(CC) $(CFLAGS) -c $^ -o $@

trace-agent: trace-agent.o trace-agent-ctl.o trace-agent-rw.o
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f *.o trace-agent
