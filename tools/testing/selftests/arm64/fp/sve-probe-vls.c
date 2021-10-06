// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2015-2020 ARM Limited.
 * Original author: Dave Martin <Dave.Martin@arm.com>
 */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/auxv.h>
#include <sys/prctl.h>
#include <asm/sigcontext.h>

#include "../../kselftest.h"
#include "rdvl.h"

int main(int argc, char **argv)
{
	unsigned int vq;
	int vl;
	static unsigned int vqs[SVE_VQ_MAX];
	unsigned int nvqs = 0;

	ksft_print_header();
	ksft_set_plan(2);

	if (!(getauxval(AT_HWCAP) & HWCAP_SVE))
		ksft_exit_skip("SVE not available\n");

	/*
	 * Enumerate up to SVE_VQ_MAX vector lengths
	 */
	for (vq = SVE_VQ_MAX; vq > 0; --vq) {
		vl = prctl(PR_SVE_SET_VL, vq * 16);
		if (vl == -1)
			ksft_exit_fail_msg("PR_SVE_SET_VL failed: %s (%d)\n",
					   strerror(errno), errno);

		vl &= PR_SVE_VL_LEN_MASK;

		if (rdvl_sve() != vl)
			ksft_exit_fail_msg("PR_SVE_SET_VL reports %d, RDVL %d\n",
					   vl, rdvl_sve());

		if (!sve_vl_valid(vl))
			ksft_exit_fail_msg("VL %d invalid\n", vl);
		vq = sve_vq_from_vl(vl);

		if (!(nvqs < SVE_VQ_MAX))
			ksft_exit_fail_msg("Too many VLs %u >= SVE_VQ_MAX\n",
					   nvqs);
		vqs[nvqs++] = vq;
	}
	ksft_test_result_pass("Enumerated %d vector lengths\n", nvqs);
	ksft_test_result_pass("All vector lengths valid\n");

	/* Print out the vector lengths in ascending order: */
	while (nvqs--)
		ksft_print_msg("%u\n", 16 * vqs[nvqs]);

	ksft_exit_pass();
}
