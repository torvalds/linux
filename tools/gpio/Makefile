CC = $(CROSS_COMPILE)gcc
CFLAGS += -O2 -Wall -g -D_GNU_SOURCE

all: lsgpio gpio-hammer gpio-event-mon

lsgpio: lsgpio.o gpio-utils.o
gpio-hammer: gpio-hammer.o gpio-utils.o
gpio-event-mon: gpio-event-mon.o gpio-utils.o

%.o: %.c gpio-utils.h

.PHONY: clean
clean:
	rm -f *.o lsgpio gpio-hammer gpio-event-mon
