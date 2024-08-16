# SPDX-License-Identifier: GPL-2.0

CFLAGS += -I. -I../../include -I../../../lib -g -Og -Wall \
	  -D_LGPL_SOURCE -fsanitize=address -fsanitize=undefined
LDFLAGS += -fsanitize=address -fsanitize=undefined
LDLIBS+= -lpthread -lurcu
TARGETS = main idr-test multiorder xarray maple
LIBS := slab.o find_bit.o bitmap.o hweight.o vsprintf.o
CORE_OFILES := xarray.o radix-tree.o idr.o linux.o test.o maple.o $(LIBS)
OFILES = main.o $(CORE_OFILES) regression1.o regression2.o regression3.o \
	 regression4.o tag_check.o multiorder.o idr-test.o iteration_check.o \
	 iteration_check_2.o benchmark.o

ifndef SHIFT
	SHIFT=3
endif

ifeq ($(BUILD), 32)
	CFLAGS += -m32
	LDFLAGS += -m32
LONG_BIT := 32
endif

ifndef LONG_BIT
LONG_BIT := $(shell getconf LONG_BIT)
endif

targets: generated/map-shift.h generated/bit-length.h $(TARGETS)

main:	$(OFILES)

idr-test.o: ../../../lib/test_ida.c
idr-test: idr-test.o $(CORE_OFILES)

xarray: $(CORE_OFILES)

maple: $(CORE_OFILES)

multiorder: multiorder.o $(CORE_OFILES)

clean:
	$(RM) $(TARGETS) *.o radix-tree.c idr.c generated/map-shift.h generated/bit-length.h

vpath %.c ../../lib

$(OFILES): Makefile *.h */*.h generated/map-shift.h generated/bit-length.h \
	../../include/linux/*.h \
	../../include/asm/*.h \
	../../../include/linux/xarray.h \
	../../../include/linux/maple_tree.h \
	../../../include/linux/radix-tree.h \
	../../../lib/radix-tree.h \
	../../../include/linux/idr.h

radix-tree.c: ../../../lib/radix-tree.c
	sed -e 's/^static //' -e 's/__always_inline //' -e 's/inline //' < $< > $@

idr.c: ../../../lib/idr.c
	sed -e 's/^static //' -e 's/__always_inline //' -e 's/inline //' < $< > $@

xarray.o: ../../../lib/xarray.c ../../../lib/test_xarray.c

maple.o: ../../../lib/maple_tree.c ../../../lib/test_maple_tree.c

generated/map-shift.h:
	@if ! grep -qws $(SHIFT) generated/map-shift.h; then		\
		echo "#define XA_CHUNK_SHIFT $(SHIFT)" >		\
				generated/map-shift.h;			\
	fi

generated/bit-length.h: FORCE
	@if ! grep -qws CONFIG_$(LONG_BIT)BIT generated/bit-length.h; then   \
		echo "Generating $@";                                        \
		echo "#define CONFIG_$(LONG_BIT)BIT 1" > $@;                 \
	fi

FORCE: ;
