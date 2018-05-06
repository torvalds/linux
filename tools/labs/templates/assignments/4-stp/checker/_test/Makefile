#CPPFLAGS = -DDEBUG -DLOG_LEVEL=LOG_DEBUG
CFLAGS = -Wall -g -m32

.PHONY: all clean

all: stp_test.o

stp_test.o: stp_test.c stp_test.h stp.h test.h util.h debug.h

clean:
	-rm -f *~ *.o
