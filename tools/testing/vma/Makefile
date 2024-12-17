# SPDX-License-Identifier: GPL-2.0-or-later

.PHONY: default clean

default: vma

include ../shared/shared.mk

OFILES = $(SHARED_OFILES) vma.o maple-shim.o
TARGETS = vma

vma.o: vma.c vma_internal.h ../../../mm/vma.c ../../../mm/vma.h

vma:	$(OFILES)
	$(CC) $(CFLAGS) -o $@ $(OFILES) $(LDLIBS)

clean:
	$(RM) $(TARGETS) *.o radix-tree.c idr.c generated/map-shift.h generated/bit-length.h generated/autoconf.h
