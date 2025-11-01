// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2020 ARM Limited

#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ucontext.h>
#include <sys/mman.h>

#include "kselftest.h"
#include "mte_common_util.h"
#include "mte_def.h"

#define TEST_UNIT	10
#define PATH_KSM	"/sys/kernel/mm/ksm/"
#define MAX_LOOP	4

static size_t page_sz;
static unsigned long ksm_sysfs[5];

static unsigned long read_sysfs(char *str)
{
	FILE *f;
	unsigned long val = 0;

	f = fopen(str, "r");
	if (!f) {
		ksft_print_msg("ERR: missing %s\n", str);
		return 0;
	}
	if (fscanf(f, "%lu", &val) != 1) {
		ksft_print_msg("ERR: parsing %s\n", str);
		val = 0;
	}
	fclose(f);
	return val;
}

static void write_sysfs(char *str, unsigned long val)
{
	FILE *f;

	f = fopen(str, "w");
	if (!f) {
		ksft_print_msg("ERR: missing %s\n", str);
		return;
	}
	fprintf(f, "%lu", val);
	fclose(f);
}

static void mte_ksm_setup(void)
{
	ksm_sysfs[0] = read_sysfs(PATH_KSM "merge_across_nodes");
	write_sysfs(PATH_KSM "merge_across_nodes", 1);
	ksm_sysfs[1] = read_sysfs(PATH_KSM "sleep_millisecs");
	write_sysfs(PATH_KSM "sleep_millisecs", 0);
	ksm_sysfs[2] = read_sysfs(PATH_KSM "run");
	write_sysfs(PATH_KSM "run", 1);
	ksm_sysfs[3] = read_sysfs(PATH_KSM "max_page_sharing");
	write_sysfs(PATH_KSM "max_page_sharing", ksm_sysfs[3] + TEST_UNIT);
	ksm_sysfs[4] = read_sysfs(PATH_KSM "pages_to_scan");
	write_sysfs(PATH_KSM "pages_to_scan", ksm_sysfs[4] + TEST_UNIT);
}

static void mte_ksm_restore(void)
{
	write_sysfs(PATH_KSM "merge_across_nodes", ksm_sysfs[0]);
	write_sysfs(PATH_KSM "sleep_millisecs", ksm_sysfs[1]);
	write_sysfs(PATH_KSM "run", ksm_sysfs[2]);
	write_sysfs(PATH_KSM "max_page_sharing", ksm_sysfs[3]);
	write_sysfs(PATH_KSM "pages_to_scan", ksm_sysfs[4]);
}

static void mte_ksm_scan(void)
{
	int cur_count = read_sysfs(PATH_KSM "full_scans");
	int scan_count = cur_count + 1;
	int max_loop_count = MAX_LOOP;

	while ((cur_count < scan_count) && max_loop_count) {
		sleep(1);
		cur_count = read_sysfs(PATH_KSM "full_scans");
		max_loop_count--;
	}
#ifdef DEBUG
	ksft_print_msg("INFO: pages_shared=%lu pages_sharing=%lu\n",
			read_sysfs(PATH_KSM "pages_shared"),
			read_sysfs(PATH_KSM "pages_sharing"));
#endif
}

static int check_madvise_options(int mem_type, int mode, int mapping)
{
	char *ptr;
	int err, ret;

	err = KSFT_FAIL;
	if (access(PATH_KSM, F_OK) == -1) {
		ksft_print_msg("ERR: Kernel KSM config not enabled\n");
		return err;
	}

	mte_switch_mode(mode, MTE_ALLOW_NON_ZERO_TAG, false);
	ptr = mte_allocate_memory(TEST_UNIT * page_sz, mem_type, mapping, true);
	if (check_allocated_memory(ptr, TEST_UNIT * page_sz, mem_type, false) != KSFT_PASS)
		return KSFT_FAIL;

	/* Insert same data in all the pages */
	memset(ptr, 'A', TEST_UNIT * page_sz);
	ret = madvise(ptr, TEST_UNIT * page_sz, MADV_MERGEABLE);
	if (ret) {
		ksft_print_msg("ERR: madvise failed to set MADV_UNMERGEABLE\n");
		goto madvise_err;
	}
	mte_ksm_scan();
	/* Tagged pages should not merge */
	if ((read_sysfs(PATH_KSM "pages_shared") < 1) ||
	    (read_sysfs(PATH_KSM "pages_sharing") < (TEST_UNIT - 1)))
		err = KSFT_PASS;
madvise_err:
	mte_free_memory(ptr, TEST_UNIT * page_sz, mem_type, true);
	return err;
}

int main(int argc, char *argv[])
{
	int err;

	err = mte_default_setup();
	if (err)
		return err;
	page_sz = getpagesize();
	if (!page_sz) {
		ksft_print_msg("ERR: Unable to get page size\n");
		return KSFT_FAIL;
	}
	/* Register signal handlers */
	mte_register_signal(SIGBUS, mte_default_handler, false);
	mte_register_signal(SIGSEGV, mte_default_handler, false);

	/* Set test plan */
	ksft_set_plan(4);

	/* Enable KSM */
	mte_ksm_setup();

	evaluate_test(check_madvise_options(USE_MMAP, MTE_SYNC_ERR, MAP_PRIVATE),
		"Check KSM mte page merge for private mapping, sync mode and mmap memory\n");
	evaluate_test(check_madvise_options(USE_MMAP, MTE_ASYNC_ERR, MAP_PRIVATE),
		"Check KSM mte page merge for private mapping, async mode and mmap memory\n");
	evaluate_test(check_madvise_options(USE_MMAP, MTE_SYNC_ERR, MAP_SHARED),
		"Check KSM mte page merge for shared mapping, sync mode and mmap memory\n");
	evaluate_test(check_madvise_options(USE_MMAP, MTE_ASYNC_ERR, MAP_SHARED),
		"Check KSM mte page merge for shared mapping, async mode and mmap memory\n");

	mte_ksm_restore();
	mte_restore_setup();
	ksft_print_cnts();
	return ksft_get_fail_cnt() == 0 ? KSFT_PASS : KSFT_FAIL;
}
