TEST_GEN_PROGS := dexcr_test hashchk_test
TEST_GEN_FILES := lsdexcr chdexcr

include ../../lib.mk
include ../flags.mk

CFLAGS += $(KHDR_INCLUDES)

$(OUTPUT)/hashchk_test: CFLAGS += -fno-pie -no-pie $(call cc-option,-mno-rop-protect)

$(TEST_GEN_PROGS): ../harness.c ../utils.c ./dexcr.c
$(TEST_GEN_FILES): ../utils.c ./dexcr.c
