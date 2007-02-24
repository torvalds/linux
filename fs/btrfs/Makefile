
CFLAGS = -g -Wall
headers = radix-tree.h ctree.h disk-io.h kerncompat.h print-tree.h
objects = ctree.o disk-io.o radix-tree.o mkfs.o extent-tree.o print-tree.o

#.c.o:
#	$(CC) $(CFLAGS) -c $<

ctree : $(objects)
	gcc $(CFLAGS) -o ctree $(objects)

$(objects) : $(headers)

clean :
	rm ctree *.o

