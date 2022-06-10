# SPDX-License-Identifier: GPL-2.0
CFLAGS += -m64

TEST_GEN_PROGS := group_constraint_pmc56_test group_pmc56_exclude_constraints_test group_constraint_pmc_count_test \
	group_constraint_repeat_test group_constraint_radix_scope_qual_test reserved_bits_mmcra_sample_elig_mode_test \
	group_constraint_mmcra_sample_test invalid_event_code_test reserved_bits_mmcra_thresh_ctl_test \
	blacklisted_events_test event_alternatives_tests_p9 event_alternatives_tests_p10 generic_events_valid_test \
	group_constraint_l2l3_sel_test group_constraint_cache_test group_constraint_thresh_cmp_test \
	group_constraint_unit_test group_constraint_thresh_ctl_test group_constraint_thresh_sel_test \
	hw_cache_event_type_test

top_srcdir = ../../../../../..
include ../../../lib.mk

$(TEST_GEN_PROGS): ../../harness.c ../../utils.c ../event.c ../lib.c ../sampling_tests/misc.h ../sampling_tests/misc.c
