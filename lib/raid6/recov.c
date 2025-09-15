// SPDX-License-Identifier: GPL-2.0-or-later
/* -*- linux-c -*- ------------------------------------------------------- *
 *
 *   Copyright 2002 H. Peter Anvin - All Rights Reserved
 *
 * ----------------------------------------------------------------------- */

/*
 * raid6/recov.c
 *
 * RAID-6 data recovery in dual failure mode.  In single failure mode,
 * use the RAID-5 algorithm (or, in the case of Q failure, just reconstruct
 * the syndrome.)
 */

#include <linux/raid/pq.h>

/* Recover two failed data blocks. */
static void raid6_2data_recov_intx1(int disks, size_t bytes, int faila,
		int failb, void **ptrs)
{
	u8 *p, *q, *dp, *dq;
	u8 px, qx, db;
	const u8 *pbmul;	/* P multiplier table for B data */
	const u8 *qmul;		/* Q multiplier table (for both) */

	p = (u8 *)ptrs[disks-2];
	q = (u8 *)ptrs[disks-1];

	/* Compute syndrome with zero for the missing data pages
	   Use the dead data pages as temporary storage for
	   delta p and delta q */
	dp = (u8 *)ptrs[faila];
	ptrs[faila] = raid6_get_zero_page();
	ptrs[disks-2] = dp;
	dq = (u8 *)ptrs[failb];
	ptrs[failb] = raid6_get_zero_page();
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
	while ( bytes-- ) {
		px    = *p ^ *dp;
		qx    = qmul[*q ^ *dq];
		*dq++ = db = pbmul[px] ^ qx; /* Reconstructed B */
		*dp++ = db ^ px; /* Reconstructed A */
		p++; q++;
	}
}

/* Recover failure of one data block plus the P block */
static void raid6_datap_recov_intx1(int disks, size_t bytes, int faila,
		void **ptrs)
{
	u8 *p, *q, *dq;
	const u8 *qmul;		/* Q multiplier table */

	p = (u8 *)ptrs[disks-2];
	q = (u8 *)ptrs[disks-1];

	/* Compute syndrome with zero for the missing data page
	   Use the dead data page as temporary storage for delta q */
	dq = (u8 *)ptrs[faila];
	ptrs[faila] = raid6_get_zero_page();
	ptrs[disks-1] = dq;

	raid6_call.gen_syndrome(disks, bytes, ptrs);

	/* Restore pointer table */
	ptrs[faila]   = dq;
	ptrs[disks-1] = q;

	/* Now, pick the proper data tables */
	qmul  = raid6_gfmul[raid6_gfinv[raid6_gfexp[faila]]];

	/* Now do it... */
	while ( bytes-- ) {
		*p++ ^= *dq = qmul[*q ^ *dq];
		q++; dq++;
	}
}


const struct raid6_recov_calls raid6_recov_intx1 = {
	.data2 = raid6_2data_recov_intx1,
	.datap = raid6_datap_recov_intx1,
	.valid = NULL,
	.name = "intx1",
	.priority = 0,
};

#ifndef __KERNEL__
/* Testing only */

/* Recover two failed blocks. */
void raid6_dual_recov(int disks, size_t bytes, int faila, int failb, void **ptrs)
{
	if ( faila > failb ) {
		int tmp = faila;
		faila = failb;
		failb = tmp;
	}

	if ( failb == disks-1 ) {
		if ( faila == disks-2 ) {
			/* P+Q failure.  Just rebuild the syndrome. */
			raid6_call.gen_syndrome(disks, bytes, ptrs);
		} else {
			/* data+Q failure.  Reconstruct data from P,
			   then rebuild syndrome. */
			/* NOT IMPLEMENTED - equivalent to RAID-5 */
		}
	} else {
		if ( failb == disks-2 ) {
			/* data+P failure. */
			raid6_datap_recov(disks, bytes, faila, ptrs);
		} else {
			/* data+data failure. */
			raid6_2data_recov(disks, bytes, faila, failb, ptrs);
		}
	}
}

#endif
