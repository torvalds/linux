TEST_PROGS := copy_unaligned copy_first_unaligned paste_unaligned paste_last_unaligned

all: $(TEST_PROGS)

$(TEST_PROGS): ../harness.c ../utils.c copy_paste_unaligned_common.c

include ../../lib.mk

clean:
	rm -f $(TEST_PROGS)
