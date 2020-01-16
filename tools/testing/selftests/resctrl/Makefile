CC = $(CROSS_COMPILE)gcc
CFLAGS = -g -Wall
SRCS=$(wildcard *.c)
OBJS=$(SRCS:.c=.o)

$(OBJS): $(SRCS)
	$(CC) $(CFLAGS) -c $(SRCS)

.PHONY: clean

clean:
	$(RM) $(OBJS)
