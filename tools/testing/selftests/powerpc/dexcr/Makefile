TEST_GEN_PROGS := hashchk_test
TEST_GEN_FILES := lsdexcr

include ../../lib.mk
include ../flags.mk

$(OUTPUT)/hashchk_test: CFLAGS += -fno-pie -no-pie $(call cc-option,-mno-rop-protect)

$(TEST_GEN_PROGS): ../harness.c ../utils.c ./dexcr.c
$(TEST_GEN_FILES): ../utils.c ./dexcr.c
