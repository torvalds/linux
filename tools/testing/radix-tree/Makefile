
CFLAGS += -I. -I../../include -g -O2 -Wall -D_LGPL_SOURCE
LDFLAGS += -lpthread -lurcu
TARGETS = main
OFILES = main.o radix-tree.o linux.o test.o tag_check.o find_next_bit.o \
	 regression1.o regression2.o regression3.o multiorder.o \
	 iteration_check.o benchmark.o

ifdef BENCHMARK
	CFLAGS += -DBENCHMARK=1
endif

targets: $(TARGETS)

main:	$(OFILES)
	$(CC) $(CFLAGS) $(LDFLAGS) $(OFILES) -o main

clean:
	$(RM) -f $(TARGETS) *.o radix-tree.c

find_next_bit.o: ../../lib/find_bit.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(OFILES): *.h */*.h \
	../../include/linux/*.h \
	../../../include/linux/radix-tree.h

radix-tree.c: ../../../lib/radix-tree.c
	sed -e 's/^static //' -e 's/__always_inline //' -e 's/inline //' < $< > $@
