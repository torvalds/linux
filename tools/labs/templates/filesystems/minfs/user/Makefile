CFLAGS = -Wall -g -m32
LDFLAGS = -static -m32

.PHONY: all clean

all: mkfs.minfs

mkfs.minfs: mkfs.minfs.o

mkfs.minfs.o: mkfs.minfs.c ../kernel/minfs.h

clean:
	-rm -f *~ *.o mkfs.minfs
