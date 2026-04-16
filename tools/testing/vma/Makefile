# SPDX-License-Identifier: GPL-2.0-or-later

.PHONY: default clean

default: vma

include ../shared/shared.mk

OFILES = $(SHARED_OFILES) main.o shared.o maple-shim.o
TARGETS = vma

# These can be varied to test different sizes.
CFLAGS += -DNUM_VMA_FLAG_BITS=128 -DNUM_MM_FLAG_BITS=128

main.o: main.c shared.c shared.h vma_internal.h tests/merge.c tests/mmap.c tests/vma.c ../../../mm/vma.c ../../../mm/vma_init.c ../../../mm/vma_exec.c ../../../mm/vma.h include/custom.h include/dup.h include/stubs.h

vma:	$(OFILES)
	$(CC) $(CFLAGS) -o $@ $(OFILES) $(LDLIBS)

clean:
	$(RM) $(TARGETS) *.o radix-tree.c idr.c generated/map-shift.h generated/bit-length.h generated/autoconf.h
