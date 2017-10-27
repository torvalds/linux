TEST_GEN_PROGS := dscr_default_test dscr_explicit_test dscr_user_test	\
	      dscr_inherit_test dscr_inherit_exec_test dscr_sysfs_test	\
	      dscr_sysfs_thread_test

include ../../lib.mk

$(OUTPUT)/dscr_default_test: LDLIBS += -lpthread

$(TEST_GEN_PROGS): ../harness.c
