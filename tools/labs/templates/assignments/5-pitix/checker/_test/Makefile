CFLAGS = -Wall -Wextra -g -m32
LDFLAGS = -static -m32

.PHONY: all clean

all: mkfs.pitix

mkfs.pitix: mkfs.pitix.o

mkfs.pitix.o: mkfs.pitix.c pitix.h

clean:
	-rm -f *~ mkfs.pitix.o mkfs.pitix
