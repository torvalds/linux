# SPDX-License-Identifier: GPL-2.0
# Makefile for LKDTM regression tests

include ../lib.mk

# NOTE: $(OUTPUT) won't get default value if used before lib.mk
TEST_FILES := tests.txt
TEST_GEN_PROGS = $(patsubst %,$(OUTPUT)/%.sh,$(shell awk '{print $$1}' tests.txt | sed -e 's/\#//'))
all: $(TEST_GEN_PROGS)

$(OUTPUT)/%: run.sh tests.txt
	install -m 0744 run.sh $@
