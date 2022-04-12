# SPDX-License-Identifier: GPL-2.0

CFLAGS += -I. -I../../include -g -Og -Wall -D_LGPL_SOURCE -fsanitize=address \
	  -fsanitize=undefined
LDFLAGS += -fsanitize=address -fsanitize=undefined
LDLIBS+= -lpthread -lurcu
TARGETS = main idr-test multiorder xarray
CORE_OFILES := xarray.o radix-tree.o idr.o linux.o test.o find_bit.o bitmap.o \
			 slab.o
OFILES = main.o $(CORE_OFILES) regression1.o regression2.o regression3.o \
	 regression4.o tag_check.o multiorder.o idr-test.o iteration_check.o \
	 iteration_check_2.o benchmark.o

ifndef SHIFT
	SHIFT=3
endif

ifeq ($(BUILD), 32)
	CFLAGS += -m32
	LDFLAGS += -m32
endif

targets: generated/map-shift.h $(TARGETS)

main:	$(OFILES)

idr-test.o: ../../../lib/test_ida.c
idr-test: idr-test.o $(CORE_OFILES)

xarray: $(CORE_OFILES)

multiorder: multiorder.o $(CORE_OFILES)

clean:
	$(RM) $(TARGETS) *.o radix-tree.c idr.c generated/map-shift.h

vpath %.c ../../lib

$(OFILES): Makefile *.h */*.h generated/map-shift.h \
	../../include/linux/*.h \
	../../include/asm/*.h \
	../../../include/linux/xarray.h \
	../../../include/linux/radix-tree.h \
	../../../include/linux/idr.h

radix-tree.c: ../../../lib/radix-tree.c
	sed -e 's/^static //' -e 's/__always_inline //' -e 's/inline //' < $< > $@

idr.c: ../../../lib/idr.c
	sed -e 's/^static //' -e 's/__always_inline //' -e 's/inline //' < $< > $@

xarray.o: ../../../lib/xarray.c ../../../lib/test_xarray.c

generated/map-shift.h:
	@if ! grep -qws $(SHIFT) generated/map-shift.h; then		\
		echo "#define XA_CHUNK_SHIFT $(SHIFT)" >		\
				generated/map-shift.h;			\
	fi
