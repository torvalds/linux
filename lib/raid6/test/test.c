/* -*- linux-c -*- ------------------------------------------------------- *
 *
 *   Copyright 2002-2007 H. Peter Anvin - All Rights Reserved
 *
 *   This file is part of the Linux kernel, and is made available under
 *   the terms of the GNU General Public License version 2 or (at your
 *   option) any later version; incorporated herein by reference.
 *
 * ----------------------------------------------------------------------- */

/*
 * raid6test.c
 *
 * Test RAID-6 recovery with various algorithms
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <linux/raid/pq.h>

#define NDISKS		16	/* Including P and Q */

const char raid6_empty_zero_page[PAGE_SIZE] __attribute__((aligned(256)));
struct raid6_calls raid6_call;

char *dataptrs[NDISKS];
char data[NDISKS][PAGE_SIZE];
char recovi[PAGE_SIZE], recovj[PAGE_SIZE];

static void makedata(int start, int stop)
{
	int i, j;

	for (i = start; i <= stop; i++) {
		for (j = 0; j < PAGE_SIZE; j++)
			data[i][j] = rand();

		dataptrs[i] = data[i];
	}
}

static char disk_type(int d)
{
	switch (d) {
	case NDISKS-2:
		return 'P';
	case NDISKS-1:
		return 'Q';
	default:
		return 'D';
	}
}

static int test_disks(int i, int j)
{
	int erra, errb;

	memset(recovi, 0xf0, PAGE_SIZE);
	memset(recovj, 0xba, PAGE_SIZE);

	dataptrs[i] = recovi;
	dataptrs[j] = recovj;

	raid6_dual_recov(NDISKS, PAGE_SIZE, i, j, (void **)&dataptrs);

	erra = memcmp(data[i], recovi, PAGE_SIZE);
	errb = memcmp(data[j], recovj, PAGE_SIZE);

	if (i < NDISKS-2 && j == NDISKS-1) {
		/* We don't implement the DQ failure scenario, since it's
		   equivalent to a RAID-5 failure (XOR, then recompute Q) */
		erra = errb = 0;
	} else {
		printf("algo=%-8s  faila=%3d(%c)  failb=%3d(%c)  %s\n",
		       raid6_call.name,
		       i, disk_type(i),
		       j, disk_type(j),
		       (!erra && !errb) ? "OK" :
		       !erra ? "ERRB" :
		       !errb ? "ERRA" : "ERRAB");
	}

	dataptrs[i] = data[i];
	dataptrs[j] = data[j];

	return erra || errb;
}

int main(int argc, char *argv[])
{
	const struct raid6_calls *const *algo;
	const struct raid6_recov_calls *const *ra;
	int i, j, p1, p2;
	int err = 0;

	makedata(0, NDISKS-1);

	for (ra = raid6_recov_algos; *ra; ra++) {
		if ((*ra)->valid  && !(*ra)->valid())
			continue;

		raid6_2data_recov = (*ra)->data2;
		raid6_datap_recov = (*ra)->datap;

		printf("using recovery %s\n", (*ra)->name);

		for (algo = raid6_algos; *algo; algo++) {
			if ((*algo)->valid && !(*algo)->valid())
				continue;

			raid6_call = **algo;

			/* Nuke syndromes */
			memset(data[NDISKS-2], 0xee, 2*PAGE_SIZE);

			/* Generate assumed good syndrome */
			raid6_call.gen_syndrome(NDISKS, PAGE_SIZE,
						(void **)&dataptrs);

			for (i = 0; i < NDISKS-1; i++)
				for (j = i+1; j < NDISKS; j++)
					err += test_disks(i, j);

			if (!raid6_call.xor_syndrome)
				continue;

			for (p1 = 0; p1 < NDISKS-2; p1++)
				for (p2 = p1; p2 < NDISKS-2; p2++) {

					/* Simulate rmw run */
					raid6_call.xor_syndrome(NDISKS, p1, p2, PAGE_SIZE,
								(void **)&dataptrs);
					makedata(p1, p2);
					raid6_call.xor_syndrome(NDISKS, p1, p2, PAGE_SIZE,
                                                                (void **)&dataptrs);

					for (i = 0; i < NDISKS-1; i++)
						for (j = i+1; j < NDISKS; j++)
							err += test_disks(i, j);
				}

		}
		printf("\n");
	}

	printf("\n");
	/* Pick the best algorithm test */
	raid6_select_algo();

	if (err)
		printf("\n*** ERRORS FOUND ***\n");

	return err;
}
