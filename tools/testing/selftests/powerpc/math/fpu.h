/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright 2023, Michael Ellerman, IBM Corporation.
 */

#ifndef _SELFTESTS_POWERPC_FPU_H
#define _SELFTESTS_POWERPC_FPU_H

static inline void randomise_darray(double *darray, int num)
{
	long val;

	for (int i = 0; i < num; i++) {
		val = random();
		if (val & 1)
			val *= -1;

		if (val & 2)
			darray[i] = 1.0 / val;
		else
			darray[i] = val * val;
	}
}

#endif /* _SELFTESTS_POWERPC_FPU_H */
