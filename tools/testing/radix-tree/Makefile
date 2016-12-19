
CFLAGS += -I. -I../../include -g -O2 -Wall -D_LGPL_SOURCE
LDFLAGS += -lpthread -lurcu
TARGETS = main idr-test multiorder
CORE_OFILES := radix-tree.o idr.o linux.o test.o find_bit.o
OFILES = main.o $(CORE_OFILES) regression1.o regression2.o regression3.o \
	 tag_check.o multiorder.o idr-test.o iteration_check.o benchmark.o

ifdef BENCHMARK
	CFLAGS += -DBENCHMARK=1
endif

targets: $(TARGETS)

main:	$(OFILES)
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o main

idr-test: idr-test.o $(CORE_OFILES)
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o idr-test

multiorder: multiorder.o $(CORE_OFILES)
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o multiorder

clean:
	$(RM) $(TARGETS) *.o radix-tree.c idr.c

vpath %.c ../../lib

$(OFILES): *.h */*.h \
	../../include/linux/*.h \
	../../include/asm/*.h \
	../../../include/linux/radix-tree.h \
	../../../include/linux/idr.h

radix-tree.c: ../../../lib/radix-tree.c
	sed -e 's/^static //' -e 's/__always_inline //' -e 's/inline //' < $< > $@

idr.c: ../../../lib/idr.c
	sed -e 's/^static //' -e 's/__always_inline //' -e 's/inline //' < $< > $@
