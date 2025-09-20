// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2022 ARM Limited

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <sys/auxv.h>
#include <sys/prctl.h>

#include <asm/hwcap.h>

#include "kselftest.h"

#ifndef AT_HWCAP3
#define AT_HWCAP3 29
#endif

static int set_tagged_addr_ctrl(int val)
{
	int ret;

	ret = prctl(PR_SET_TAGGED_ADDR_CTRL, val, 0, 0, 0);
	if (ret < 0)
		ksft_print_msg("PR_SET_TAGGED_ADDR_CTRL: failed %d %d (%s)\n",
			       ret, errno, strerror(errno));
	return ret;
}

static int get_tagged_addr_ctrl(void)
{
	int ret;

	ret = prctl(PR_GET_TAGGED_ADDR_CTRL, 0, 0, 0, 0);
	if (ret < 0)
		ksft_print_msg("PR_GET_TAGGED_ADDR_CTRL failed: %d %d (%s)\n",
			       ret, errno, strerror(errno));
	return ret;
}

/*
 * Read the current mode without having done any configuration, should
 * run first.
 */
void check_basic_read(void)
{
	int ret;

	ret = get_tagged_addr_ctrl();
	if (ret < 0) {
		ksft_test_result_fail("check_basic_read\n");
		return;
	}

	if (ret & PR_MTE_TCF_SYNC)
		ksft_print_msg("SYNC enabled\n");
	if (ret & PR_MTE_TCF_ASYNC)
		ksft_print_msg("ASYNC enabled\n");

	/* Any configuration is valid */
	ksft_test_result_pass("check_basic_read\n");
}

/*
 * Attempt to set a specified combination of modes.
 */
void set_mode_test(const char *name, int hwcap2, int hwcap3, int mask)
{
	int ret;

	if ((getauxval(AT_HWCAP2) & hwcap2) != hwcap2) {
		ksft_test_result_skip("%s\n", name);
		return;
	}

	if ((getauxval(AT_HWCAP3) & hwcap3) != hwcap3) {
		ksft_test_result_skip("%s\n", name);
		return;
	}

	ret = set_tagged_addr_ctrl(mask);
	if (ret < 0) {
		ksft_test_result_fail("%s\n", name);
		return;
	}

	ret = get_tagged_addr_ctrl();
	if (ret < 0) {
		ksft_test_result_fail("%s\n", name);
		return;
	}

	if ((ret & (PR_MTE_TCF_MASK | PR_MTE_STORE_ONLY)) == mask) {
		ksft_test_result_pass("%s\n", name);
	} else {
		ksft_print_msg("Got %x, expected %x\n",
			       (ret & (int)PR_MTE_TCF_MASK), mask);
		ksft_test_result_fail("%s\n", name);
	}
}

struct mte_mode {
	int mask;
	int hwcap2;
	int hwcap3;
	const char *name;
} mte_modes[] = {
	{ PR_MTE_TCF_NONE,                                        0,          0,                     "NONE"  },
	{ PR_MTE_TCF_SYNC,                                        HWCAP2_MTE, 0,                     "SYNC"  },
	{ PR_MTE_TCF_ASYNC,                                       HWCAP2_MTE, 0,                     "ASYNC" },
	{ PR_MTE_TCF_SYNC | PR_MTE_TCF_ASYNC,                     HWCAP2_MTE, 0,                     "SYNC+ASYNC"  },
	{ PR_MTE_TCF_SYNC | PR_MTE_STORE_ONLY,                    HWCAP2_MTE, HWCAP3_MTE_STORE_ONLY, "SYNC+STONLY" },
	{ PR_MTE_TCF_ASYNC | PR_MTE_STORE_ONLY,                   HWCAP2_MTE, HWCAP3_MTE_STORE_ONLY, "ASYNC+STONLY" },
	{ PR_MTE_TCF_SYNC | PR_MTE_TCF_ASYNC | PR_MTE_STORE_ONLY, HWCAP2_MTE, HWCAP3_MTE_STORE_ONLY, "SYNC+ASYNC+STONLY" },
};

int main(void)
{
	int i;

	ksft_print_header();
	ksft_set_plan(ARRAY_SIZE(mte_modes));

	check_basic_read();
	for (i = 0; i < ARRAY_SIZE(mte_modes); i++)
		set_mode_test(mte_modes[i].name, mte_modes[i].hwcap2, mte_modes[i].hwcap3,
			      mte_modes[i].mask);

	ksft_print_cnts();

	return 0;
}
