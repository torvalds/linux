# SPDX-License-Identifier: GPL-2.0-or-later

.PHONY: default clean

default: vma

include ../shared/shared.mk

OFILES = $(SHARED_OFILES) main.o shared.o maple-shim.o
TARGETS = vma

main.o: main.c shared.c shared.h vma_internal.h tests/merge.c tests/mmap.c tests/vma.c ../../../mm/vma.c ../../../mm/vma_init.c ../../../mm/vma_exec.c ../../../mm/vma.h

vma:	$(OFILES)
	$(CC) $(CFLAGS) -o $@ $(OFILES) $(LDLIBS)

clean:
	$(RM) $(TARGETS) *.o radix-tree.c idr.c generated/map-shift.h generated/bit-length.h generated/autoconf.h
