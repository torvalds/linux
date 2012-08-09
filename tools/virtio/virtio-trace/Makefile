CC = gcc
CFLAGS = -O2 -Wall
LFLAG = -lpthread

all: trace-agent

.c.o:
	$(CC) $(CFLAGS) $(LFLAG) -c $^ -o $@

trace-agent: trace-agent.o trace-agent-ctl.o trace-agent-rw.o
	$(CC) $(CFLAGS) $(LFLAG) -o $@ $^

clean:
	rm -f *.o trace-agent
