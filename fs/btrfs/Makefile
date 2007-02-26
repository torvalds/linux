
CFLAGS = -g -Wall
headers = radix-tree.h ctree.h disk-io.h kerncompat.h print-tree.h
objects = ctree.o disk-io.o radix-tree.o mkfs.o extent-tree.o print-tree.o

#.c.o:
#	$(CC) $(CFLAGS) -c $<

all: tester debug-tree

debug-tree: $(objects) debug-tree.o
	gcc $(CFLAGS) -o debug-tree $(objects) debug-tree.o

tester: $(objects) random-test.o
	gcc $(CFLAGS) -o tester $(objects) random-test.o

$(objects) : $(headers)

clean :
	rm debug-tree tester *.o


