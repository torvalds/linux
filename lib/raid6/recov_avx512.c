/*
 * Copyright (C) 2016 Intel Corporation
 *
 * Author: Gayatri Kammela <gayatri.kammela@intel.com>
 * Author: Megha Dey <megha.dey@linux.intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 *
 */

#ifdef CONFIG_AS_AVX512

#include <linux/raid/pq.h>
#include "x86.h"

static int raid6_has_avx512(void)
{
	return boot_cpu_has(X86_FEATURE_AVX2) &&
		boot_cpu_has(X86_FEATURE_AVX) &&
		boot_cpu_has(X86_FEATURE_AVX512F) &&
		boot_cpu_has(X86_FEATURE_AVX512BW) &&
		boot_cpu_has(X86_FEATURE_AVX512VL) &&
		boot_cpu_has(X86_FEATURE_AVX512DQ);
}

static void raid6_2data_recov_avx512(int disks, size_t bytes, int faila,
				     int failb, void **ptrs)
{
	u8 *p, *q, *dp, *dq;
	const u8 *pbmul;	/* P multiplier table for B data */
	const u8 *qmul;		/* Q multiplier table (for both) */
	const u8 x0f = 0x0f;

	p = (u8 *)ptrs[disks-2];
	q = (u8 *)ptrs[disks-1];

	/*
	 * Compute syndrome with zero for the missing data pages
	 * Use the dead data pages as temporary storage for
	 * delta p and delta q
	 */

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

	/* zmm0 = x0f[16] */
	asm volatile("vpbroadcastb %0, %%zmm7" : : "m" (x0f));

	while (bytes) {
#ifdef CONFIG_X86_64
		asm volatile("vmovdqa64 %0, %%zmm1\n\t"
			     "vmovdqa64 %1, %%zmm9\n\t"
			     "vmovdqa64 %2, %%zmm0\n\t"
			     "vmovdqa64 %3, %%zmm8\n\t"
			     "vpxorq %4, %%zmm1, %%zmm1\n\t"
			     "vpxorq %5, %%zmm9, %%zmm9\n\t"
			     "vpxorq %6, %%zmm0, %%zmm0\n\t"
			     "vpxorq %7, %%zmm8, %%zmm8"
			     :
			     : "m" (q[0]), "m" (q[64]), "m" (p[0]),
			       "m" (p[64]), "m" (dq[0]), "m" (dq[64]),
			       "m" (dp[0]), "m" (dp[64]));

		/*
		 * 1 = dq[0]  ^ q[0]
		 * 9 = dq[64] ^ q[64]
		 * 0 = dp[0]  ^ p[0]
		 * 8 = dp[64] ^ p[64]
		 */

		asm volatile("vbroadcasti64x2 %0, %%zmm4\n\t"
			     "vbroadcasti64x2 %1, %%zmm5"
			     :
			     : "m" (qmul[0]), "m" (qmul[16]));

		asm volatile("vpsraw $4, %%zmm1, %%zmm3\n\t"
			     "vpsraw $4, %%zmm9, %%zmm12\n\t"
			     "vpandq %%zmm7, %%zmm1, %%zmm1\n\t"
			     "vpandq %%zmm7, %%zmm9, %%zmm9\n\t"
			     "vpandq %%zmm7, %%zmm3, %%zmm3\n\t"
			     "vpandq %%zmm7, %%zmm12, %%zmm12\n\t"
			     "vpshufb %%zmm9, %%zmm4, %%zmm14\n\t"
			     "vpshufb %%zmm1, %%zmm4, %%zmm4\n\t"
			     "vpshufb %%zmm12, %%zmm5, %%zmm15\n\t"
			     "vpshufb %%zmm3, %%zmm5, %%zmm5\n\t"
			     "vpxorq %%zmm14, %%zmm15, %%zmm15\n\t"
			     "vpxorq %%zmm4, %%zmm5, %%zmm5"
			     :
			     : );

		/*
		 * 5 = qx[0]
		 * 15 = qx[64]
		 */

		asm volatile("vbroadcasti64x2 %0, %%zmm4\n\t"
			     "vbroadcasti64x2 %1, %%zmm1\n\t"
			     "vpsraw $4, %%zmm0, %%zmm2\n\t"
			     "vpsraw $4, %%zmm8, %%zmm6\n\t"
			     "vpandq %%zmm7, %%zmm0, %%zmm3\n\t"
			     "vpandq %%zmm7, %%zmm8, %%zmm14\n\t"
			     "vpandq %%zmm7, %%zmm2, %%zmm2\n\t"
			     "vpandq %%zmm7, %%zmm6, %%zmm6\n\t"
			     "vpshufb %%zmm14, %%zmm4, %%zmm12\n\t"
			     "vpshufb %%zmm3, %%zmm4, %%zmm4\n\t"
			     "vpshufb %%zmm6, %%zmm1, %%zmm13\n\t"
			     "vpshufb %%zmm2, %%zmm1, %%zmm1\n\t"
			     "vpxorq %%zmm4, %%zmm1, %%zmm1\n\t"
			     "vpxorq %%zmm12, %%zmm13, %%zmm13"
			     :
			     : "m" (pbmul[0]), "m" (pbmul[16]));

		/*
		 * 1  = pbmul[px[0]]
		 * 13 = pbmul[px[64]]
		 */
		asm volatile("vpxorq %%zmm5, %%zmm1, %%zmm1\n\t"
			     "vpxorq %%zmm15, %%zmm13, %%zmm13"
			     :
			     : );

		/*
		 * 1 = db = DQ
		 * 13 = db[64] = DQ[64]
		 */
		asm volatile("vmovdqa64 %%zmm1, %0\n\t"
			     "vmovdqa64 %%zmm13,%1\n\t"
			     "vpxorq %%zmm1, %%zmm0, %%zmm0\n\t"
			     "vpxorq %%zmm13, %%zmm8, %%zmm8"
			     :
			     : "m" (dq[0]), "m" (dq[64]));

		asm volatile("vmovdqa64 %%zmm0, %0\n\t"
			     "vmovdqa64 %%zmm8, %1"
			     :
			     : "m" (dp[0]), "m" (dp[64]));

		bytes -= 128;
		p += 128;
		q += 128;
		dp += 128;
		dq += 128;
#else
		asm volatile("vmovdqa64 %0, %%zmm1\n\t"
			     "vmovdqa64 %1, %%zmm0\n\t"
			     "vpxorq %2, %%zmm1, %%zmm1\n\t"
			     "vpxorq %3, %%zmm0, %%zmm0"
			     :
			     : "m" (*q), "m" (*p), "m"(*dq), "m" (*dp));

		/* 1 = dq ^ q;  0 = dp ^ p */

		asm volatile("vbroadcasti64x2 %0, %%zmm4\n\t"
			     "vbroadcasti64x2 %1, %%zmm5"
			     :
			     : "m" (qmul[0]), "m" (qmul[16]));

		/*
		 * 1 = dq ^ q
		 * 3 = dq ^ p >> 4
		 */
		asm volatile("vpsraw $4, %%zmm1, %%zmm3\n\t"
			     "vpandq %%zmm7, %%zmm1, %%zmm1\n\t"
			     "vpandq %%zmm7, %%zmm3, %%zmm3\n\t"
			     "vpshufb %%zmm1, %%zmm4, %%zmm4\n\t"
			     "vpshufb %%zmm3, %%zmm5, %%zmm5\n\t"
			     "vpxorq %%zmm4, %%zmm5, %%zmm5"
			     :
			     : );

		/* 5 = qx */

		asm volatile("vbroadcasti64x2 %0, %%zmm4\n\t"
			     "vbroadcasti64x2 %1, %%zmm1"
			     :
			     : "m" (pbmul[0]), "m" (pbmul[16]));

		asm volatile("vpsraw $4, %%zmm0, %%zmm2\n\t"
			     "vpandq %%zmm7, %%zmm0, %%zmm3\n\t"
			     "vpandq %%zmm7, %%zmm2, %%zmm2\n\t"
			     "vpshufb %%zmm3, %%zmm4, %%zmm4\n\t"
			     "vpshufb %%zmm2, %%zmm1, %%zmm1\n\t"
			     "vpxorq %%zmm4, %%zmm1, %%zmm1"
			     :
			     : );

		/* 1 = pbmul[px] */
		asm volatile("vpxorq %%zmm5, %%zmm1, %%zmm1\n\t"
			     /* 1 = db = DQ */
			     "vmovdqa64 %%zmm1, %0\n\t"
			     :
			     : "m" (dq[0]));

		asm volatile("vpxorq %%zmm1, %%zmm0, %%zmm0\n\t"
			     "vmovdqa64 %%zmm0, %0"
			     :
			     : "m" (dp[0]));

		bytes -= 64;
		p += 64;
		q += 64;
		dp += 64;
		dq += 64;
#endif
	}

	kernel_fpu_end();
}

static void raid6_datap_recov_avx512(int disks, size_t bytes, int faila,
				     void **ptrs)
{
	u8 *p, *q, *dq;
	const u8 *qmul;		/* Q multiplier table */
	const u8 x0f = 0x0f;

	p = (u8 *)ptrs[disks-2];
	q = (u8 *)ptrs[disks-1];

	/*
	 * Compute syndrome with zero for the missing data page
	 * Use the dead data page as temporary storage for delta q
	 */

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

	asm volatile("vpbroadcastb %0, %%zmm7" : : "m" (x0f));

	while (bytes) {
#ifdef CONFIG_X86_64
		asm volatile("vmovdqa64 %0, %%zmm3\n\t"
			     "vmovdqa64 %1, %%zmm8\n\t"
			     "vpxorq %2, %%zmm3, %%zmm3\n\t"
			     "vpxorq %3, %%zmm8, %%zmm8"
			     :
			     : "m" (dq[0]), "m" (dq[64]), "m" (q[0]),
			       "m" (q[64]));

		/*
		 * 3 = q[0] ^ dq[0]
		 * 8 = q[64] ^ dq[64]
		 */
		asm volatile("vbroadcasti64x2 %0, %%zmm0\n\t"
			     "vmovapd %%zmm0, %%zmm13\n\t"
			     "vbroadcasti64x2 %1, %%zmm1\n\t"
			     "vmovapd %%zmm1, %%zmm14"
			     :
			     : "m" (qmul[0]), "m" (qmul[16]));

		asm volatile("vpsraw $4, %%zmm3, %%zmm6\n\t"
			     "vpsraw $4, %%zmm8, %%zmm12\n\t"
			     "vpandq %%zmm7, %%zmm3, %%zmm3\n\t"
			     "vpandq %%zmm7, %%zmm8, %%zmm8\n\t"
			     "vpandq %%zmm7, %%zmm6, %%zmm6\n\t"
			     "vpandq %%zmm7, %%zmm12, %%zmm12\n\t"
			     "vpshufb %%zmm3, %%zmm0, %%zmm0\n\t"
			     "vpshufb %%zmm8, %%zmm13, %%zmm13\n\t"
			     "vpshufb %%zmm6, %%zmm1, %%zmm1\n\t"
			     "vpshufb %%zmm12, %%zmm14, %%zmm14\n\t"
			     "vpxorq %%zmm0, %%zmm1, %%zmm1\n\t"
			     "vpxorq %%zmm13, %%zmm14, %%zmm14"
			     :
			     : );

		/*
		 * 1  = qmul[q[0]  ^ dq[0]]
		 * 14 = qmul[q[64] ^ dq[64]]
		 */
		asm volatile("vmovdqa64 %0, %%zmm2\n\t"
			     "vmovdqa64 %1, %%zmm12\n\t"
			     "vpxorq %%zmm1, %%zmm2, %%zmm2\n\t"
			     "vpxorq %%zmm14, %%zmm12, %%zmm12"
			     :
			     : "m" (p[0]), "m" (p[64]));

		/*
		 * 2  = p[0]  ^ qmul[q[0]  ^ dq[0]]
		 * 12 = p[64] ^ qmul[q[64] ^ dq[64]]
		 */

		asm volatile("vmovdqa64 %%zmm1, %0\n\t"
			     "vmovdqa64 %%zmm14, %1\n\t"
			     "vmovdqa64 %%zmm2, %2\n\t"
			     "vmovdqa64 %%zmm12,%3"
			     :
			     : "m" (dq[0]), "m" (dq[64]), "m" (p[0]),
			       "m" (p[64]));

		bytes -= 128;
		p += 128;
		q += 128;
		dq += 128;
#else
		asm volatile("vmovdqa64 %0, %%zmm3\n\t"
			     "vpxorq %1, %%zmm3, %%zmm3"
			     :
			     : "m" (dq[0]), "m" (q[0]));

		/* 3 = q ^ dq */

		asm volatile("vbroadcasti64x2 %0, %%zmm0\n\t"
			     "vbroadcasti64x2 %1, %%zmm1"
			     :
			     : "m" (qmul[0]), "m" (qmul[16]));

		asm volatile("vpsraw $4, %%zmm3, %%zmm6\n\t"
			     "vpandq %%zmm7, %%zmm3, %%zmm3\n\t"
			     "vpandq %%zmm7, %%zmm6, %%zmm6\n\t"
			     "vpshufb %%zmm3, %%zmm0, %%zmm0\n\t"
			     "vpshufb %%zmm6, %%zmm1, %%zmm1\n\t"
			     "vpxorq %%zmm0, %%zmm1, %%zmm1"
			     :
			     : );

		/* 1 = qmul[q ^ dq] */

		asm volatile("vmovdqa64 %0, %%zmm2\n\t"
			     "vpxorq %%zmm1, %%zmm2, %%zmm2"
			     :
			     : "m" (p[0]));

		/* 2 = p ^ qmul[q ^ dq] */

		asm volatile("vmovdqa64 %%zmm1, %0\n\t"
			     "vmovdqa64 %%zmm2, %1"
			     :
			     : "m" (dq[0]), "m" (p[0]));

		bytes -= 64;
		p += 64;
		q += 64;
		dq += 64;
#endif
	}

	kernel_fpu_end();
}

const struct raid6_recov_calls raid6_recov_avx512 = {
	.data2 = raid6_2data_recov_avx512,
	.datap = raid6_datap_recov_avx512,
	.valid = raid6_has_avx512,
#ifdef CONFIG_X86_64
	.name = "avx512x2",
#else
	.name = "avx512x1",
#endif
	.priority = 3,
};

#else
#warning "your version of binutils lacks AVX512 support"
#endif
