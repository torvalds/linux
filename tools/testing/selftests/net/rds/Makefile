# SPDX-License-Identifier: GPL-2.0

all:
	@echo mk_build_dir="$(shell pwd)" > include.sh

TEST_PROGS := run.sh

TEST_FILES := \
	include.sh \
	test.py \
# end of TEST_FILES

EXTRA_CLEAN := \
	include.sh \
	/tmp/rds_logs \
# end of EXTRA_CLEAN

include ../../lib.mk
