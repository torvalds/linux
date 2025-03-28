# SPDX-License-Identifier: GPL-2.0-only
# Carsten Haitzler <carsten.haitzler@arm.com>, 2021
include ../../../../../tools/scripts/Makefile.include
include ../../../../../tools/scripts/Makefile.arch
include ../../../../../tools/scripts/utilities.mak

SUBDIRS = \
	asm_pure_loop \
	memcpy_thread \
	thread_loop \
	unroll_loop_thread

all: $(SUBDIRS)
$(SUBDIRS):
	@$(MAKE) -C $@ >/dev/null

INSTALLDIRS = $(SUBDIRS:%=install-%)

install-tests: $(INSTALLDIRS)
$(INSTALLDIRS):
	@$(MAKE) -C $(@:install-%=%) install-tests >/dev/null

CLEANDIRS = $(SUBDIRS:%=clean-%)

clean: $(CLEANDIRS)
$(CLEANDIRS):
	$(call QUIET_CLEAN, test-$(@:clean-%=%)) $(MAKE) -C $(@:clean-%=%) clean >/dev/null

.PHONY: all clean $(SUBDIRS) $(CLEANDIRS) $(INSTALLDIRS)
