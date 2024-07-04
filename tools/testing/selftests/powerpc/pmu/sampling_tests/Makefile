# SPDX-License-Identifier: GPL-2.0
TEST_GEN_PROGS := mmcr0_exceptionbits_test mmcr0_cc56run_test mmcr0_pmccext_test \
		   mmcr0_pmcjce_test mmcr0_fc56_pmc1ce_test mmcr0_fc56_pmc56_test \
		   mmcr1_comb_test mmcr2_l2l3_test mmcr2_fcs_fch_test \
		   mmcr3_src_test mmcra_thresh_marked_sample_test mmcra_thresh_cmp_test \
		   mmcra_bhrb_ind_call_test mmcra_bhrb_any_test mmcra_bhrb_cond_test \
		   mmcra_bhrb_disable_test bhrb_no_crash_wo_pmu_test intr_regs_no_crash_wo_pmu_test \
		   bhrb_filter_map_test mmcr1_sel_unit_cache_test mmcra_bhrb_disable_no_branch_test

top_srcdir = ../../../../../..
include ../../../lib.mk
include ../../flags.mk

CFLAGS += -m64

$(TEST_GEN_PROGS): ../../harness.c ../../utils.c ../event.c ../lib.c misc.c misc.h ../loop.S ../branch_loops.S
