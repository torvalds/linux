CC=gcc
CFLAGS = -Wall
CPPFLAGS = -I$(topsrcdir)/nanospi/kernel

all: hic-console

hic-console: hic-console.o
	$(CC) $(CFLAGS) -o hic-console hic-console.o

clean:
	rm -f *.o hic-console

topsrcdir = ../../..
