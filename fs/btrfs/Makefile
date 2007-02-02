
CFLAGS= -g -Wall

.c.o:
	$(CC) $(CFLAGS) -c $<

ctree: ctree.o disk-io.h ctree.h disk-io.o radix-tree.o radix-tree.h
	gcc $(CFLAGS) -o ctree ctree.o disk-io.o radix-tree.o

clean:
	rm ctree *.o

