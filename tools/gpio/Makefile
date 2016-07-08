CC = $(CROSS_COMPILE)gcc
CFLAGS += -O2 -Wall -g -D_GNU_SOURCE

all: lsgpio

lsgpio: lsgpio.o gpio-utils.o

%.o: %.c gpio-utils.h

.PHONY: clean
clean:
	rm -f *.o lsgpio
