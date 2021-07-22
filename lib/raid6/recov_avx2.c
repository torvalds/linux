// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2012 Intel Corporation
 * Author: Jim Kukunas <james.t.kukunas@linux.intel.com>
 */

#include <linux/raid/pq.h>
#include "x86.h"

static int raid6_has_avx2(void)
{
	return boot_cpu_has(X86_FEATURE_AVX2) &&
		boot_cpu_has(X86_FEATURE_AVX);
}

static void raid6_2data_recov_avx2(int disks, size_t bytes, int faila,
		int failb, void **ptrs)
{
	u8 *p, *q, *dp, *dq;
	const u8 *pbmul;	/* P multiplier table for B data */
	const u8 *qmul;		/* Q multiplier table (for both) */
	const u8 x0f = 0x0f;

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
	pbmul = raid6_vgfmul[raid6_gfexi[failb-faila]];
	qmul  = raid6_vgfmul[raid6_gfinv[raid6_gfexp[faila] ^
		raid6_gfexp[failb]]];

	kernel_fpu_begin();

	/* ymm0 = x0f[16] */
	asm volatile("vpbroadcastb %0, %%ymm7" : : "m" (x0f));

	while (bytes) {
#ifdef CONFIG_X86_64
		asm volatile("vmovdqa %0, %%ymm1" : : "m" (q[0]));
		asm volatile("vmovdqa %0, %%ymm9" : : "m" (q[32]));
		asm volatile("vmovdqa %0, %%ymm0" : : "m" (p[0]));
		asm volatile("vmovdqa %0, %%ymm8" : : "m" (p[32]));
		asm volatile("vpxor %0, %%ymm1, %%ymm1" : : "m" (dq[0]));
		asm volatile("vpxor %0, %%ymm9, %%ymm9" : : "m" (dq[32]));
		asm volatile("vpxor %0, %%ymm0, %%ymm0" : : "m" (dp[0]));
		asm volatile("vpxor %0, %%ymm8, %%ymm8" : : "m" (dp[32]));

		/*
		 * 1 = dq[0]  ^ q[0]
		 * 9 = dq[32] ^ q[32]
		 * 0 = dp[0]  ^ p[0]
		 * 8 = dp[32] ^ p[32]
		 */

		asm volatile("vbroadcasti128 %0, %%ymm4" : : "m" (qmul[0]));
		asm volatile("vbroadcasti128 %0, %%ymm5" : : "m" (qmul[16]));

		asm volatile("vpsraw $4, %ymm1, %ymm3");
		asm volatile("vpsraw $4, %ymm9, %ymm12");
		asm volatile("vpand %ymm7, %ymm1, %ymm1");
		asm volatile("vpand %ymm7, %ymm9, %ymm9");
		asm volatile("vpand %ymm7, %ymm3, %ymm3");
		asm volatile("vpand %ymm7, %ymm12, %ymm12");
		asm volatile("vpshufb %ymm9, %ymm4, %ymm14");
		asm volatile("vpshufb %ymm1, %ymm4, %ymm4");
		asm volatile("vpshufb %ymm12, %ymm5, %ymm15");
		asm volatile("vpshufb %ymm3, %ymm5, %ymm5");
		asm volatile("vpxor %ymm14, %ymm15, %ymm15");
		asm volatile("vpxor %ymm4, %ymm5, %ymm5");

		/*
		 * 5 = qx[0]
		 * 15 = qx[32]
		 */

		asm volatile("vbroadcasti128 %0, %%ymm4" : : "m" (pbmul[0]));
		asm volatile("vbroadcasti128 %0, %%ymm1" : : "m" (pbmul[16]));
		asm volatile("vpsraw $4, %ymm0, %ymm2");
		asm volatile("vpsraw $4, %ymm8, %ymm6");
		asm volatile("vpand %ymm7, %ymm0, %ymm3");
		asm volatile("vpand %ymm7, %ymm8, %ymm14");
		asm volatile("vpand %ymm7, %ymm2, %ymm2");
		asm volatile("vpand %ymm7, %ymm6, %ymm6");
		asm volatile("vpshufb %ymm14, %ymm4, %ymm12");
		asm volatile("vpshufb %ymm3, %ymm4, %ymm4");
		asm volatile("vpshufb %ymm6, %ymm1, %ymm13");
		asm volatile("vpshufb %ymm2, %ymm1, %ymm1");
		asm volatile("vpxor %ymm4, %ymm1, %ymm1");
		asm volatile("vpxor %ymm12, %ymm13, %ymm13");

		/*
		 * 1  = pbmul[px[0]]
		 * 13 = pbmul[px[32]]
		 */
		asm volatile("vpxor %ymm5, %ymm1, %ymm1");
		asm volatile("vpxor %ymm15, %ymm13, %ymm13");

		/*
		 * 1 = db = DQ
		 * 13 = db[32] = DQ[32]
		 */
		asm volatile("vmovdqa %%ymm1, %0" : "=m" (dq[0]));
		asm volatile("vmovdqa %%ymm13,%0" : "=m" (dq[32]));
		asm volatile("vpxor %ymm1, %ymm0, %ymm0");
		asm volatile("vpxor %ymm13, %ymm8, %ymm8");

		asm volatile("vmovdqa %%ymm0, %0" : "=m" (dp[0]));
		asm volatile("vmovdqa %%ymm8, %0" : "=m" (dp[32]));

		bytes -= 64;
		p += 64;
		q += 64;
		dp += 64;
		dq += 64;
#else
		asm volatile("vmovdqa %0, %%ymm1" : : "m" (*q));
		asm volatile("vmovdqa %0, %%ymm0" : : "m" (*p));
		asm volatile("vpxor %0, %%ymm1, %%ymm1" : : "m" (*dq));
		asm volatile("vpxor %0, %%ymm0, %%ymm0" : : "m" (*dp));

		/* 1 = dq ^ q;  0 = dp ^ p */

		asm volatile("vbroadcasti128 %0, %%ymm4" : : "m" (qmul[0]));
		asm volatile("vbroadcasti128 %0, %%ymm5" : : "m" (qmul[16]));

		/*
		 * 1 = dq ^ q
		 * 3 = dq ^ p >> 4
		 */
		asm volatile("vpsraw $4, %ymm1, %ymm3");
		asm volatile("vpand %ymm7, %ymm1, %ymm1");
		asm volatile("vpand %ymm7, %ymm3, %ymm3");
		asm volatile("vpshufb %ymm1, %ymm4, %ymm4");
		asm volatile("vpshufb %ymm3, %ymm5, %ymm5");
		asm volatile("vpxor %ymm4, %ymm5, %ymm5");

		/* 5 = qx */

		asm volatile("vbroadcasti128 %0, %%ymm4" : : "m" (pbmul[0]));
		asm volatile("vbroadcasti128 %0, %%ymm1" : : "m" (pbmul[16]));

		asm volatile("vpsraw $4, %ymm0, %ymm2");
		asm volatile("vpand %ymm7, %ymm0, %ymm3");
		asm volatile("vpand %ymm7, %ymm2, %ymm2");
		asm volatile("vpshufb %ymm3, %ymm4, %ymm4");
		asm volatile("vpshufb %ymm2, %ymm1, %ymm1");
		asm volatile("vpxor %ymm4, %ymm1, %ymm1");

		/* 1 = pbmul[px] */
		asm volatile("vpxor %ymm5, %ymm1, %ymm1");
		/* 1 = db = DQ */
		asm volatile("vmovdqa %%ymm1, %0" : "=m" (dq[0]));

		asm volatile("vpxor %ymm1, %ymm0, %ymm0");
		asm volatile("vmovdqa %%ymm0, %0" : "=m" (dp[0]));

		bytes -= 32;
		p += 32;
		q += 32;
		dp += 32;
		dq += 32;
#endif
	}

	kernel_fpu_end();
}

static void raid6_datap_recov_avx2(int disks, size_t bytes, int faila,
		void **ptrs)
{
	u8 *p, *q, *dq;
	const u8 *qmul;		/* Q multiplier table */
	const u8 x0f = 0x0f;

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
	qmul  = raid6_vgfmul[raid6_gfinv[raid6_gfexp[faila]]];

	kernel_fpu_begin();

	asm volatile("vpbroadcastb %0, %%ymm7" : : "m" (x0f));

	while (bytes) {
#ifdef CONFIG_X86_64
		asm volatile("vmovdqa %0, %%ymm3" : : "m" (dq[0]));
		asm volatile("vmovdqa %0, %%ymm8" : : "m" (dq[32]));
		asm volatile("vpxor %0, %%ymm3, %%ymm3" : : "m" (q[0]));
		asm volatile("vpxor %0, %%ymm8, %%ymm8" : : "m" (q[32]));

		/*
		 * 3 = q[0] ^ dq[0]
		 * 8 = q[32] ^ dq[32]
		 */
		asm volatile("vbroadcasti128 %0, %%ymm0" : : "m" (qmul[0]));
		asm volatile("vmovapd %ymm0, %ymm13");
		asm volatile("vbroadcasti128 %0, %%ymm1" : : "m" (qmul[16]));
		asm volatile("vmovapd %ymm1, %ymm14");

		asm volatile("vpsraw $4, %ymm3, %ymm6");
		asm volatile("vpsraw $4, %ymm8, %ymm12");
		asm volatile("vpand %ymm7, %ymm3, %ymm3");
		asm volatile("vpand %ymm7, %ymm8, %ymm8");
		asm volatile("vpand %ymm7, %ymm6, %ymm6");
		asm volatile("vpand %ymm7, %ymm12, %ymm12");
		asm volatile("vpshufb %ymm3, %ymm0, %ymm0");
		asm volatile("vpshufb %ymm8, %ymm13, %ymm13");
		asm volatile("vpshufb %ymm6, %ymm1, %ymm1");
		asm volatile("vpshufb %ymm12, %ymm14, %ymm14");
		asm volatile("vpxor %ymm0, %ymm1, %ymm1");
		asm volatile("vpxor %ymm13, %ymm14, %ymm14");

		/*
		 * 1  = qmul[q[0]  ^ dq[0]]
		 * 14 = qmul[q[32] ^ dq[32]]
		 */
		asm volatile("vmovdqa %0, %%ymm2" : : "m" (p[0]));
		asm volatile("vmovdqa %0, %%ymm12" : : "m" (p[32]));
		asm volatile("vpxor %ymm1, %ymm2, %ymm2");
		asm volatile("vpxor %ymm14, %ymm12, %ymm12");

		/*
		 * 2  = p[0]  ^ qmul[q[0]  ^ dq[0]]
		 * 12 = p[32] ^ qmul[q[32] ^ dq[32]]
		 */

		asm volatile("vmovdqa %%ymm1, %0" : "=m" (dq[0]));
		asm volatile("vmovdqa %%ymm14, %0" : "=m" (dq[32]));
		asm volatile("vmovdqa %%ymm2, %0" : "=m" (p[0]));
		asm volatile("vmovdqa %%ymm12,%0" : "=m" (p[32]));

		bytes -= 64;
		p += 64;
		q += 64;
		dq += 64;
#else
		asm volatile("vmovdqa %0, %%ymm3" : : "m" (dq[0]));
		asm volatile("vpxor %0, %%ymm3, %%ymm3" : : "m" (q[0]));

		/* 3 = q ^ dq */

		asm volatile("vbroadcasti128 %0, %%ymm0" : : "m" (qmul[0]));
		asm volatile("vbroadcasti128 %0, %%ymm1" : : "m" (qmul[16]));

		asm volatile("vpsraw $4, %ymm3, %ymm6");
		asm volatile("vpand %ymm7, %ymm3, %ymm3");
		asm volatile("vpand %ymm7, %ymm6, %ymm6");
		asm volatile("vpshufb %ymm3, %ymm0, %ymm0");
		asm volatile("vpshufb %ymm6, %ymm1, %ymm1");
		asm volatile("vpxor %ymm0, %ymm1, %ymm1");

		/* 1 = qmul[q ^ dq] */

		asm volatile("vmovdqa %0, %%ymm2" : : "m" (p[0]));
		asm volatile("vpxor %ymm1, %ymm2, %ymm2");

		/* 2 = p ^ qmul[q ^ dq] */

		asm volatile("vmovdqa %%ymm1, %0" : "=m" (dq[0]));
		asm volatile("vmovdqa %%ymm2, %0" : "=m" (p[0]));

		bytes -= 32;
		p += 32;
		q += 32;
		dq += 32;
#endif
	}

	kernel_fpu_end();
}

const struct raid6_recov_calls raid6_recov_avx2 = {
	.data2 = raid6_2data_recov_avx2,
	.datap = raid6_datap_recov_avx2,
	.valid = raid6_has_avx2,
#ifdef CONFIG_X86_64
	.name = "avx2x2",
#else
	.name = "avx2x1",
#endif
	.priority = 2,
};
