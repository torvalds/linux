// SPDX-License-Identifier: GPL-2.0
/*
 * RAID-6 data recovery in dual failure mode based on the XC instruction.
 *
 * Copyright IBM Corp. 2016
 * Author(s): Martin Schwidefsky <schwidefsky@de.ibm.com>
 */

#include <linux/export.h>
#include <linux/raid/pq.h>

static inline void xor_block(u8 *p1, u8 *p2)
{
	typedef struct { u8 _[256]; } addrtype;

	asm volatile(
		"	xc	0(256,%[p1]),0(%[p2])\n"
		: "+m" (*(addrtype *) p1) : "m" (*(addrtype *) p2),
		  [p1] "a" (p1), [p2] "a" (p2) : "cc");
}

/* Recover two failed data blocks. */
static void raid6_2data_recov_s390xc(int disks, size_t bytes, int faila,
		int failb, void **ptrs)
{
	u8 *p, *q, *dp, *dq;
	const u8 *pbmul;	/* P multiplier table for B data */
	const u8 *qmul;		/* Q multiplier table (for both) */
	int i;

	p = (u8 *)ptrs[disks-2];
	q = (u8 *)ptrs[disks-1];

	/* Compute syndrome with zero for the missing data pages
	   Use the dead data pages as temporary storage for
	   delta p and delta q */
	dp = (u8 *)ptrs[faila];
	ptrs[faila] = (void *)raid6_empty_zero_page;
	ptrs[disks-2] = dp;
	dq = (u8 *)ptrs[failb];
	ptrs[failb] = (void *)raid6_empty_zero_page;
	ptrs[disks-1] = dq;

	raid6_call.gen_syndrome(disks, bytes, ptrs);

	/* Restore pointer table */
	ptrs[faila]   = dp;
	ptrs[failb]   = dq;
	ptrs[disks-2] = p;
	ptrs[disks-1] = q;

	/* Now, pick the proper data tables */
	pbmul = raid6_gfmul[raid6_gfexi[failb-faila]];
	qmul  = raid6_gfmul[raid6_gfinv[raid6_gfexp[faila]^raid6_gfexp[failb]]];

	/* Now do it... */
	while (bytes) {
		xor_block(dp, p);
		xor_block(dq, q);
		for (i = 0; i < 256; i++)
			dq[i] = pbmul[dp[i]] ^ qmul[dq[i]];
		xor_block(dp, dq);
		p += 256;
		q += 256;
		dp += 256;
		dq += 256;
		bytes -= 256;
	}
}

/* Recover failure of one data block plus the P block */
static void raid6_datap_recov_s390xc(int disks, size_t bytes, int faila,
		void **ptrs)
{
	u8 *p, *q, *dq;
	const u8 *qmul;		/* Q multiplier table */
	int i;

	p = (u8 *)ptrs[disks-2];
	q = (u8 *)ptrs[disks-1];

	/* Compute syndrome with zero for the missing data page
	   Use the dead data page as temporary storage for delta q */
	dq = (u8 *)ptrs[faila];
	ptrs[faila] = (void *)raid6_empty_zero_page;
	ptrs[disks-1] = dq;

	raid6_call.gen_syndrome(disks, bytes, ptrs);

	/* Restore pointer table */
	ptrs[faila]   = dq;
	ptrs[disks-1] = q;

	/* Now, pick the proper data tables */
	qmul  = raid6_gfmul[raid6_gfinv[raid6_gfexp[faila]]];

	/* Now do it... */
	while (bytes) {
		xor_block(dq, q);
		for (i = 0; i < 256; i++)
			dq[i] = qmul[dq[i]];
		xor_block(p, dq);
		p += 256;
		q += 256;
		dq += 256;
		bytes -= 256;
	}
}


const struct raid6_recov_calls raid6_recov_s390xc = {
	.data2 = raid6_2data_recov_s390xc,
	.datap = raid6_datap_recov_s390xc,
	.valid = NULL,
	.name = "s390xc",
	.priority = 1,
};
