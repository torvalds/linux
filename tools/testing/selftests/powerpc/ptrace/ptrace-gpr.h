/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2015 Anshuman Khandual, IBM Corporation.
 */
#define GPR_1	1
#define GPR_2	2
#define GPR_3	3
#define GPR_4	4

#define FPR_1	0.001
#define FPR_2	0.002
#define FPR_3	0.003
#define FPR_4	0.004

#define FPR_1_REP 0x3f50624de0000000
#define FPR_2_REP 0x3f60624de0000000
#define FPR_3_REP 0x3f689374c0000000
#define FPR_4_REP 0x3f70624de0000000

/* Buffer must have 18 elements */
int validate_gpr(unsigned long *gpr, unsigned long val)
{
	int i, found = 1;

	for (i = 0; i < 18; i++) {
		if (gpr[i] != val) {
			printf("GPR[%d]: %lx Expected: %lx\n",
				i+14, gpr[i], val);
			found = 0;
		}
	}

	if (!found)
		return TEST_FAIL;
	return TEST_PASS;
}

/* Buffer must have 32 elements */
int validate_fpr(unsigned long *fpr, unsigned long val)
{
	int i, found = 1;

	for (i = 0; i < 32; i++) {
		if (fpr[i] != val) {
			printf("FPR[%d]: %lx Expected: %lx\n", i, fpr[i], val);
			found = 0;
		}
	}

	if (!found)
		return TEST_FAIL;
	return TEST_PASS;
}

/* Buffer must have 32 elements */
int validate_fpr_float(float *fpr, float val)
{
	int i, found = 1;

	for (i = 0; i < 32; i++) {
		if (fpr[i] != val) {
			printf("FPR[%d]: %f Expected: %f\n", i, fpr[i], val);
			found = 0;
		}
	}

	if (!found)
		return TEST_FAIL;
	return TEST_PASS;
}
