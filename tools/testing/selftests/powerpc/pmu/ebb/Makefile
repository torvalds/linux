# SPDX-License-Identifier: GPL-2.0
noarg:
	$(MAKE) -C ../../

# The EBB handler is 64-bit code and everything links against it
CFLAGS += -m64

TEST_GEN_PROGS := reg_access_test event_attributes_test cycles_test	\
	 cycles_with_freeze_test pmc56_overflow_test		\
	 ebb_vs_cpu_event_test cpu_event_vs_ebb_test		\
	 cpu_event_pinned_vs_ebb_test task_event_vs_ebb_test	\
	 task_event_pinned_vs_ebb_test multi_ebb_procs_test	\
	 multi_counter_test pmae_handling_test			\
	 close_clears_pmcc_test instruction_count_test		\
	 fork_cleanup_test ebb_on_child_test			\
	 ebb_on_willing_child_test back_to_back_ebbs_test	\
	 lost_exception_test no_handler_test			\
	 cycles_with_mmcr2_test

top_srcdir = ../../../../../..
include ../../../lib.mk

$(TEST_GEN_PROGS): ../../harness.c ../../utils.c ../event.c ../lib.c \
	       ebb.c ebb_handler.S trace.c busy_loop.S

$(OUTPUT)/instruction_count_test: ../loop.S

$(OUTPUT)/lost_exception_test: ../lib.c
