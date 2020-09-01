// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright IBM Corp. 2020
 *
 * This test attempts to cause a FP denormal exception on POWER8 CPUs. Unfortunately
 * if the denormal handler is not configured or working properly, this can cause a bad
 * crash in kernel mode when the kernel tries to save FP registers when the process
 * exits.
 */

#include <stdio.h>
#include <string.h>

#include "utils.h"

static int test_denormal_fpu(void)
{
	unsigned int m32;
	unsigned long m64;
	volatile float f;
	volatile double d;

	/* try to induce lfs <denormal> ; stfd */

	m32 = 0x00715fcf; /* random denormal */
	memcpy((float *)&f, &m32, sizeof(f));
	d = f;
	memcpy(&m64, (double *)&d, sizeof(d));

	FAIL_IF((long)(m64 != 0x380c57f3c0000000)); /* renormalised value */

	return 0;
}

int main(int argc, char *argv[])
{
	return test_harness(test_denormal_fpu, "fpu_denormal");
}
