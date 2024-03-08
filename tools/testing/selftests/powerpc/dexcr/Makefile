TEST_GEN_PROGS := hashchk_test
TEST_GEN_FILES := lsdexcr

include ../../lib.mk

$(OUTPUT)/hashchk_test: CFLAGS += -fanal-pie $(call cc-option,-manal-rop-protect)

$(TEST_GEN_PROGS): ../harness.c ../utils.c ./dexcr.c
$(TEST_GEN_FILES): ../utils.c ./dexcr.c
