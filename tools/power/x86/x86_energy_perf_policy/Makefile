# SPDX-License-Identifier: GPL-2.0
CC		= $(CROSS_COMPILE)gcc
BUILD_OUTPUT	:= $(CURDIR)
PREFIX		:= /usr
DESTDIR		:=
DAY		:= $(shell date +%Y.%m.%d)
SNAPSHOT	= x86_energy_perf_policy-$(DAY)



ifeq ("$(origin O)", "command line")
	BUILD_OUTPUT := $(O)
endif

x86_energy_perf_policy : x86_energy_perf_policy.c
override CFLAGS +=	-O2 -Wall -I../../../include
override CFLAGS +=	-DMSRHEADER='"../../../../arch/x86/include/asm/msr-index.h"'
override CFLAGS +=	-D_FORTIFY_SOURCE=2

%: %.c
	@mkdir -p $(BUILD_OUTPUT)
	$(CC) $(CFLAGS) $< -o $(BUILD_OUTPUT)/$@ $(LDFLAGS)

.PHONY : clean
clean :
	@rm -f $(BUILD_OUTPUT)/x86_energy_perf_policy

install : x86_energy_perf_policy
	install -d  $(DESTDIR)$(PREFIX)/bin
	install $(BUILD_OUTPUT)/x86_energy_perf_policy $(DESTDIR)$(PREFIX)/bin/x86_energy_perf_policy
	install -d  $(DESTDIR)$(PREFIX)/share/man/man8
	install -m 644 x86_energy_perf_policy.8 $(DESTDIR)$(PREFIX)/share/man/man8

snapshot: x86_energy_perf_policy
	@rm -rf $(SNAPSHOT)
	@mkdir $(SNAPSHOT)
	@cp x86_energy_perf_policy Makefile x86_energy_perf_policy.c x86_energy_perf_policy.8 $(SNAPSHOT)

	@sed -e 's/^#include <linux\/bits.h>/#include "bits.h"/' -e 's/u64/unsigned long long/' ../../../../arch/x86/include/asm/msr-index.h > $(SNAPSHOT)/msr-index.h
	@echo '#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))' >> $(SNAPSHOT)/msr-index.h
	@echo "#define BIT(x) (1 << (x))" > $(SNAPSHOT)/bits.h
	@echo "#define BIT_ULL(nr) (1ULL << (nr))" >> $(SNAPSHOT)/bits.h
	@echo "#define GENMASK(h, l) (((~0UL) << (l)) & (~0UL >> (sizeof(long) * 8 - 1 - (h))))" >> $(SNAPSHOT)/bits.h
	@echo "#define GENMASK_ULL(h, l) (((~0ULL) << (l)) & (~0ULL >> (sizeof(long long) * 8 - 1 - (h))))" >> $(SNAPSHOT)/bits.h

	@echo '#define BUILD_BUG_ON(cond) do { enum { compile_time_check ## __COUNTER__ = 1/(!(cond)) }; } while (0)' > $(SNAPSHOT)/build_bug.h
	@echo '#define __must_be_array(arr) 0' >> $(SNAPSHOT)/build_bug.h

	@echo PWD=. > $(SNAPSHOT)/Makefile
	@echo "CFLAGS +=	-DMSRHEADER='\"msr-index.h\"'" >> $(SNAPSHOT)/Makefile
	@echo "CFLAGS +=	-DBUILD_BUG_HEADER='\"build_bug.h\"'" >> $(SNAPSHOT)/Makefile
	@sed -e's/.*MSRHEADER.*//' Makefile >> $(SNAPSHOT)/Makefile

	@rm -f $(SNAPSHOT).tar.gz
	tar cvzf $(SNAPSHOT).tar.gz $(SNAPSHOT)

