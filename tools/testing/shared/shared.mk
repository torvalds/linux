# SPDX-License-Identifier: GPL-2.0

CFLAGS += -I../shared -I. -I../../include -I../../../lib -g -Og -Wall \
	  -D_LGPL_SOURCE -fsanitize=address -fsanitize=undefined
LDFLAGS += -fsanitize=address -fsanitize=undefined
LDLIBS += -lpthread -lurcu
LIBS := slab.o find_bit.o bitmap.o hweight.o vsprintf.o
SHARED_OFILES = xarray-shared.o radix-tree.o idr.o linux.o $(LIBS)

SHARED_DEPS = Makefile ../shared/shared.mk ../shared/*.h generated/map-shift.h \
	generated/bit-length.h generated/autoconf.h \
	../../include/linux/*.h \
	../../include/asm/*.h \
	../../../include/linux/xarray.h \
	../../../include/linux/maple_tree.h \
	../../../include/linux/radix-tree.h \
	../../../lib/radix-tree.h \
	../../../include/linux/idr.h

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

%.o: ../shared/%.c
	$(CC) -c $(CFLAGS) $< -o $@

vpath %.c ../../lib

$(SHARED_OFILES): $(SHARED_DEPS)

radix-tree.c: ../../../lib/radix-tree.c
	sed -e 's/^static //' -e 's/__always_inline //' -e 's/inline //' < $< > $@

idr.c: ../../../lib/idr.c
	sed -e 's/^static //' -e 's/__always_inline //' -e 's/inline //' < $< > $@

xarray-shared.o: ../shared/xarray-shared.c ../../../lib/xarray.c \
	../../../lib/test_xarray.c

maple-shared.o: ../shared/maple-shared.c ../../../lib/maple_tree.c \
	../../../lib/test_maple_tree.c

generated/autoconf.h:
	@mkdir -p generated
	cp ../shared/autoconf.h generated/autoconf.h

generated/map-shift.h:
	@mkdir -p generated
	@if ! grep -qws $(SHIFT) generated/map-shift.h; then            \
		echo "Generating $@";                                   \
		echo "#define XA_CHUNK_SHIFT $(SHIFT)" >                \
				generated/map-shift.h;                  \
	fi

generated/bit-length.h: FORCE
	@mkdir -p generated
	@if ! grep -qws CONFIG_$(LONG_BIT)BIT generated/bit-length.h; then   \
		echo "Generating $@";                                        \
		echo "#define CONFIG_$(LONG_BIT)BIT 1" > $@;                 \
	fi

FORCE: ;
