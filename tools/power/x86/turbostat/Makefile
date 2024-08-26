# SPDX-License-Identifier: GPL-2.0
CC		= $(CROSS_COMPILE)gcc
BUILD_OUTPUT	:= $(CURDIR)
PREFIX		?= /usr
DESTDIR		?=
DAY		:= $(shell date +%Y.%m.%d)
SNAPSHOT		= turbostat-$(DAY)

ifeq ("$(origin O)", "command line")
	BUILD_OUTPUT := $(O)
endif

turbostat : turbostat.c
override CFLAGS +=	-O2 -Wall -Wextra -I../../../include
override CFLAGS +=	-DMSRHEADER='"../../../../arch/x86/include/asm/msr-index.h"'
override CFLAGS +=	-DINTEL_FAMILY_HEADER='"../../../../arch/x86/include/asm/intel-family.h"'
override CFLAGS +=	-DBUILD_BUG_HEADER='"../../../../include/linux/build_bug.h"'
override CFLAGS +=	-D_FILE_OFFSET_BITS=64
override CFLAGS +=	-D_FORTIFY_SOURCE=2

%: %.c
	@mkdir -p $(BUILD_OUTPUT)
	$(CC) $(CFLAGS) $< -o $(BUILD_OUTPUT)/$@ $(LDFLAGS) -lcap -lrt

.PHONY : clean
clean :
	@rm -f $(BUILD_OUTPUT)/turbostat
	@rm -f $(SNAPSHOT).tar.gz

install : turbostat
	install -d $(DESTDIR)$(PREFIX)/bin
	install $(BUILD_OUTPUT)/turbostat $(DESTDIR)$(PREFIX)/bin/turbostat
	install -d $(DESTDIR)$(PREFIX)/share/man/man8
	install -m 644 turbostat.8 $(DESTDIR)$(PREFIX)/share/man/man8

snapshot: turbostat
	@rm -rf $(SNAPSHOT)
	@mkdir $(SNAPSHOT)
	@cp turbostat Makefile turbostat.c turbostat.8 ../../../../arch/x86/include/asm/intel-family.h $(SNAPSHOT)

	@sed -e 's/^#include <linux\/bits.h>/#include "bits.h"/' ../../../../arch/x86/include/asm/msr-index.h > $(SNAPSHOT)/msr-index.h
	@echo '#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))' >> $(SNAPSHOT)/msr-index.h
	@echo "#define BIT(x) (1 << (x))" > $(SNAPSHOT)/bits.h
	@echo "#define BIT_ULL(nr) (1ULL << (nr))" >> $(SNAPSHOT)/bits.h
	@echo "#define GENMASK(h, l) (((~0UL) << (l)) & (~0UL >> (sizeof(long) * 8 - 1 - (h))))" >> $(SNAPSHOT)/bits.h
	@echo "#define GENMASK_ULL(h, l) (((~0ULL) << (l)) & (~0ULL >> (sizeof(long long) * 8 - 1 - (h))))" >> $(SNAPSHOT)/bits.h

	@echo '#define BUILD_BUG_ON(cond) do { enum { compile_time_check ## __COUNTER__ = 1/(!(cond)) }; } while (0)' > $(SNAPSHOT)/build_bug.h

	@echo PWD=. > $(SNAPSHOT)/Makefile
	@echo "CFLAGS +=	-DMSRHEADER='\"msr-index.h\"'" >> $(SNAPSHOT)/Makefile
	@echo "CFLAGS +=	-DINTEL_FAMILY_HEADER='\"intel-family.h\"'" >> $(SNAPSHOT)/Makefile
	@echo "CFLAGS +=	-DBUILD_BUG_HEADER='\"build_bug.h\"'" >> $(SNAPSHOT)/Makefile
	@sed -e's/.*MSRHEADER.*//' -e's/.*INTEL_FAMILY_HEADER.*//' -e's/.*BUILD_BUG_HEADER.*//' Makefile >> $(SNAPSHOT)/Makefile

	@rm -f $(SNAPSHOT).tar.gz
	tar cvzf $(SNAPSHOT).tar.gz $(SNAPSHOT)
