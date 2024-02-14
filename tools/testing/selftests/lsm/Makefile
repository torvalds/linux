# SPDX-License-Identifier: GPL-2.0
#
# First run: make -C ../../../.. headers_install

CFLAGS += -Wall -O2 $(KHDR_INCLUDES)
LOCAL_HDRS += common.h

TEST_GEN_PROGS := lsm_get_self_attr_test lsm_list_modules_test \
		  lsm_set_self_attr_test

include ../lib.mk

$(OUTPUT)/lsm_get_self_attr_test: lsm_get_self_attr_test.c common.c
$(OUTPUT)/lsm_set_self_attr_test: lsm_set_self_attr_test.c common.c
$(OUTPUT)/lsm_list_modules_test: lsm_list_modules_test.c common.c

EXTRA_CLEAN = $(OUTPUT)/common.o
