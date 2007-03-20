CC=gcc
CFLAGS = -g -Wall -Werror
headers = radix-tree.h ctree.h disk-io.h kerncompat.h print-tree.h list.h \
	  transaction.h
objects = ctree.o disk-io.o radix-tree.o mkfs.o extent-tree.o print-tree.o \
	  root-tree.o dir-item.o hash.o file-item.o inode-item.o \
	  inode-map.o \

# if you don't have sparse installed, use ls instead
CHECKFLAGS=-D__linux__ -Dlinux -D__STDC__ -Dunix -D__unix__ -Wbitwise \
		-Wcontext -Wcast-truncate -Wuninitialized -Wshadow -Wundef
check=sparse $(CHECKFLAGS)
#check=ls

.c.o:
	$(check) $<
	$(CC) $(CFLAGS) -c $<

all: tester debug-tree quick-test dir-test tags

debug-tree: $(objects) debug-tree.o
	gcc $(CFLAGS) -o debug-tree $(objects) debug-tree.o

tester: $(objects) random-test.o
	gcc $(CFLAGS) -o tester $(objects) random-test.o

dir-test: $(objects) dir-test.o
	gcc $(CFLAGS) -o dir-test $(objects) dir-test.o
quick-test: $(objects) quick-test.o
	gcc $(CFLAGS) -o quick-test $(objects) quick-test.o

$(objects): $(headers)

clean :
	rm debug-tree tester *.o


