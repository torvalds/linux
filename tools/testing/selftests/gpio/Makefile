# SPDX-License-Identifier: GPL-2.0

TEST_PROGS := gpio-mockup.sh gpio-sim.sh gpio-aggregator.sh gpio-cdev-uaf.sh
TEST_FILES := gpio-mockup-sysfs.sh
TEST_GEN_PROGS_EXTENDED := gpio-mockup-cdev gpio-chip-info gpio-line-name \
			   gpio-cdev-uaf
CFLAGS += -O2 -g -Wall $(KHDR_INCLUDES)

include ../lib.mk
