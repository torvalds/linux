# ################################################################
# Copyright (c) 2017-present, Facebook, Inc.
# All rights reserved.
#
# This source code is licensed under both the BSD-style license (found in the
# LICENSE file in the root directory of this source tree) and the GPLv2 (found
# in the COPYING file in the root directory of this source tree).
# ################################################################

PRGDIR = ../../programs
VOID   = /dev/null
export PATH := .:$(PATH)

.PHONY: all
#all: test-gzip-env
all: test-helin-segv test-hufts test-keep test-list test-memcpy-abuse test-mixed
all: test-null-suffix-clobber test-stdin test-trailing-nul test-unpack-invalid
all: test-zdiff test-zgrep-context test-zgrep-f test-zgrep-signal test-znew-k test-z-suffix
	@echo Testing completed

.PHONY: zstd
zstd:
	$(MAKE) -C $(PRGDIR) zstd
	ln -sf $(PRGDIR)/zstd gzip
	@echo PATH=$(PATH)
	gzip --version

.PHONY: clean
clean:
	@$(MAKE) -C $(PRGDIR) $@ > $(VOID)
	@$(RM) *.trs *.log
	@echo Cleaning completed


#------------------------------------------------------------------------------
# validated only for Linux, macOS, Hurd and some BSD targets
#------------------------------------------------------------------------------
ifneq (,$(filter $(shell uname),Linux Darwin GNU/kFreeBSD GNU FreeBSD DragonFly NetBSD))

test-%: zstd
	@./test-driver.sh --test-name $* --log-file $*.log --trs-file $*.trs --expect-failure "no" --color-tests "yes" --enable-hard-errors "yes" ./$*.sh
	# || echo ignoring error

endif
