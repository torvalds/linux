CC = $(CROSS_COMPILE)gcc
CFLAGS = -g -Wall
SRCS=$(wildcard *.c)
OBJS=$(SRCS:.c=.o)

all: resctrl_tests

$(OBJS): $(SRCS)
	$(CC) $(CFLAGS) -c $(SRCS)

resctrl_tests: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

.PHONY: clean

clean:
	$(RM) $(OBJS) resctrl_tests
