TEST_PROGS := dscr_default_test dscr_explicit_test dscr_user_test	\
	      dscr_inherit_test dscr_inherit_exec_test dscr_sysfs_test	\
	      dscr_sysfs_thread_test

dscr_default_test: LDLIBS += -lpthread

all: $(TEST_PROGS)

$(TEST_PROGS): ../harness.c

include ../../lib.mk

clean:
	rm -f $(TEST_PROGS) *.o
