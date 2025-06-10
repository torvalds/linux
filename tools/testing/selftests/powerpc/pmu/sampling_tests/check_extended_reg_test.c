// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2024, Kajol Jain, IBM Corp.
 */

#include <stdio.h>
#include <stdlib.h>

#include "../event.h"
#include "misc.h"
#include "utils.h"

/*
 * A perf sampling test to check extended
 * reg support.
 */
static int check_extended_reg_test(void)
{
	/* Check for platform support for the test */
	SKIP_IF(!have_hwcap2(PPC_FEATURE2_ARCH_3_00));

	 /* Skip for Generic compat PMU */
	SKIP_IF(check_for_generic_compat_pmu());

	/* Check if platform supports extended regs */
	platform_extended_mask = perf_get_platform_reg_mask();
	FAIL_IF(check_extended_regs_support());

	return 0;
}

int main(void)
{
	return test_harness(check_extended_reg_test, "check_extended_reg_test");
}
