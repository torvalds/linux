
CFLAGS += -I. -g -Wall -D_LGPL_SOURCE
LDFLAGS += -lpthread -lurcu
TARGETS = main
OFILES = main.o radix-tree.o linux.o test.o tag_check.o find_next_bit.o \
	 regression1.o regression2.o regression3.o multiorder.o

targets: $(TARGETS)

main:	$(OFILES)
	$(CC) $(CFLAGS) $(LDFLAGS) $(OFILES) -o main

clean:
	$(RM) -f $(TARGETS) *.o radix-tree.c

$(OFILES): *.h */*.h ../../../include/linux/radix-tree.h ../../include/linux/*.h

radix-tree.c: ../../../lib/radix-tree.c
	sed -e 's/^static //' -e 's/__always_inline //' -e 's/inline //' < $< > $@
