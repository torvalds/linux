/*
 * Copyright (C) 2012 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 */

#include <linux/raid/pq.h>
#include "x86.h"

static int raid6_has_ssse3(void)
{
	return boot_cpu_has(X86_FEATURE_XMM) &&
		boot_cpu_has(X86_FEATURE_XMM2) &&
		boot_cpu_has(X86_FEATURE_SSSE3);
}

static void raid6_2data_recov_ssse3(int disks, size_t bytes, int faila,
		int failb, void **ptrs)
{
	u8 *p, *q, *dp, *dq;
	const u8 *pbmul;	/* P multiplier table for B data */
	const u8 *qmul;		/* Q multiplier table (for both) */
	static const u8 __aligned(16) x0f[16] = {
		 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f,
		 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f};

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

	asm volatile("movdqa %0,%%xmm7" : : "m" (x0f[0]));

#ifdef CONFIG_X86_64
	asm volatile("movdqa %0,%%xmm6" : : "m" (qmul[0]));
	asm volatile("movdqa %0,%%xmm14" : : "m" (pbmul[0]));
	asm volatile("movdqa %0,%%xmm15" : : "m" (pbmul[16]));
#endif

	/* Now do it... */
	while (bytes) {
#ifdef CONFIG_X86_64
		/* xmm6, xmm14, xmm15 */

		asm volatile("movdqa %0,%%xmm1" : : "m" (q[0]));
		asm volatile("movdqa %0,%%xmm9" : : "m" (q[16]));
		asm volatile("movdqa %0,%%xmm0" : : "m" (p[0]));
		asm volatile("movdqa %0,%%xmm8" : : "m" (p[16]));
		asm volatile("pxor   %0,%%xmm1" : : "m" (dq[0]));
		asm volatile("pxor   %0,%%xmm9" : : "m" (dq[16]));
		asm volatile("pxor   %0,%%xmm0" : : "m" (dp[0]));
		asm volatile("pxor   %0,%%xmm8" : : "m" (dp[16]));

		/* xmm0/8 = px */

		asm volatile("movdqa %xmm6,%xmm4");
		asm volatile("movdqa %0,%%xmm5" : : "m" (qmul[16]));
		asm volatile("movdqa %xmm6,%xmm12");
		asm volatile("movdqa %xmm5,%xmm13");
		asm volatile("movdqa %xmm1,%xmm3");
		asm volatile("movdqa %xmm9,%xmm11");
		asm volatile("movdqa %xmm0,%xmm2"); /* xmm2/10 = px */
		asm volatile("movdqa %xmm8,%xmm10");
		asm volatile("psraw  $4,%xmm1");
		asm volatile("psraw  $4,%xmm9");
		asm volatile("pand   %xmm7,%xmm3");
		asm volatile("pand   %xmm7,%xmm11");
		asm volatile("pand   %xmm7,%xmm1");
		asm volatile("pand   %xmm7,%xmm9");
		asm volatile("pshufb %xmm3,%xmm4");
		asm volatile("pshufb %xmm11,%xmm12");
		asm volatile("pshufb %xmm1,%xmm5");
		asm volatile("pshufb %xmm9,%xmm13");
		asm volatile("pxor   %xmm4,%xmm5");
		asm volatile("pxor   %xmm12,%xmm13");

		/* xmm5/13 = qx */

		asm volatile("movdqa %xmm14,%xmm4");
		asm volatile("movdqa %xmm15,%xmm1");
		asm volatile("movdqa %xmm14,%xmm12");
		asm volatile("movdqa %xmm15,%xmm9");
		asm volatile("movdqa %xmm2,%xmm3");
		asm volatile("movdqa %xmm10,%xmm11");
		asm volatile("psraw  $4,%xmm2");
		asm volatile("psraw  $4,%xmm10");
		asm volatile("pand   %xmm7,%xmm3");
		asm volatile("pand   %xmm7,%xmm11");
		asm volatile("pand   %xmm7,%xmm2");
		asm volatile("pand   %xmm7,%xmm10");
		asm volatile("pshufb %xmm3,%xmm4");
		asm volatile("pshufb %xmm11,%xmm12");
		asm volatile("pshufb %xmm2,%xmm1");
		asm volatile("pshufb %xmm10,%xmm9");
		asm volatile("pxor   %xmm4,%xmm1");
		asm volatile("pxor   %xmm12,%xmm9");

		/* xmm1/9 = pbmul[px] */
		asm volatile("pxor   %xmm5,%xmm1");
		asm volatile("pxor   %xmm13,%xmm9");
		/* xmm1/9 = db = DQ */
		asm volatile("movdqa %%xmm1,%0" : "=m" (dq[0]));
		asm volatile("movdqa %%xmm9,%0" : "=m" (dq[16]));

		asm volatile("pxor   %xmm1,%xmm0");
		asm volatile("pxor   %xmm9,%xmm8");
		asm volatile("movdqa %%xmm0,%0" : "=m" (dp[0]));
		asm volatile("movdqa %%xmm8,%0" : "=m" (dp[16]));

		bytes -= 32;
		p += 32;
		q += 32;
		dp += 32;
		dq += 32;
#else
		asm volatile("movdqa %0,%%xmm1" : : "m" (*q));
		asm volatile("movdqa %0,%%xmm0" : : "m" (*p));
		asm volatile("pxor   %0,%%xmm1" : : "m" (*dq));
		asm volatile("pxor   %0,%%xmm0" : : "m" (*dp));

		/* 1 = dq ^ q
		 * 0 = dp ^ p
		 */
		asm volatile("movdqa %0,%%xmm4" : : "m" (qmul[0]));
		asm volatile("movdqa %0,%%xmm5" : : "m" (qmul[16]));

		asm volatile("movdqa %xmm1,%xmm3");
		asm volatile("psraw  $4,%xmm1");
		asm volatile("pand   %xmm7,%xmm3");
		asm volatile("pand   %xmm7,%xmm1");
		asm volatile("pshufb %xmm3,%xmm4");
		asm volatile("pshufb %xmm1,%xmm5");
		asm volatile("pxor   %xmm4,%xmm5");

		asm volatile("movdqa %xmm0,%xmm2"); /* xmm2 = px */

		/* xmm5 = qx */

		asm volatile("movdqa %0,%%xmm4" : : "m" (pbmul[0]));
		asm volatile("movdqa %0,%%xmm1" : : "m" (pbmul[16]));
		asm volatile("movdqa %xmm2,%xmm3");
		asm volatile("psraw  $4,%xmm2");
		asm volatile("pand   %xmm7,%xmm3");
		asm volatile("pand   %xmm7,%xmm2");
		asm volatile("pshufb %xmm3,%xmm4");
		asm volatile("pshufb %xmm2,%xmm1");
		asm volatile("pxor   %xmm4,%xmm1");

		/* xmm1 = pbmul[px] */
		asm volatile("pxor   %xmm5,%xmm1");
		/* xmm1 = db = DQ */
		asm volatile("movdqa %%xmm1,%0" : "=m" (*dq));

		asm volatile("pxor   %xmm1,%xmm0");
		asm volatile("movdqa %%xmm0,%0" : "=m" (*dp));

		bytes -= 16;
		p += 16;
		q += 16;
		dp += 16;
		dq += 16;
#endif
	}

	kernel_fpu_end();
}


static void raid6_datap_recov_ssse3(int disks, size_t bytes, int faila,
		void **ptrs)
{
	u8 *p, *q, *dq;
	const u8 *qmul;		/* Q multiplier table */
	static const u8 __aligned(16) x0f[16] = {
		 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f,
		 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f};

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

	asm volatile("movdqa %0, %%xmm7" : : "m" (x0f[0]));

	while (bytes) {
#ifdef CONFIG_X86_64
		asm volatile("movdqa %0, %%xmm3" : : "m" (dq[0]));
		asm volatile("movdqa %0, %%xmm4" : : "m" (dq[16]));
		asm volatile("pxor %0, %%xmm3" : : "m" (q[0]));
		asm volatile("movdqa %0, %%xmm0" : : "m" (qmul[0]));

		/* xmm3 = q[0] ^ dq[0] */

		asm volatile("pxor %0, %%xmm4" : : "m" (q[16]));
		asm volatile("movdqa %0, %%xmm1" : : "m" (qmul[16]));

		/* xmm4 = q[16] ^ dq[16] */

		asm volatile("movdqa %xmm3, %xmm6");
		asm volatile("movdqa %xmm4, %xmm8");

		/* xmm4 = xmm8 = q[16] ^ dq[16] */

		asm volatile("psraw $4, %xmm3");
		asm volatile("pand %xmm7, %xmm6");
		asm volatile("pand %xmm7, %xmm3");
		asm volatile("pshufb %xmm6, %xmm0");
		asm volatile("pshufb %xmm3, %xmm1");
		asm volatile("movdqa %0, %%xmm10" : : "m" (qmul[0]));
		asm volatile("pxor %xmm0, %xmm1");
		asm volatile("movdqa %0, %%xmm11" : : "m" (qmul[16]));

		/* xmm1 = qmul[q[0] ^ dq[0]] */

		asm volatile("psraw $4, %xmm4");
		asm volatile("pand %xmm7, %xmm8");
		asm volatile("pand %xmm7, %xmm4");
		asm volatile("pshufb %xmm8, %xmm10");
		asm volatile("pshufb %xmm4, %xmm11");
		asm volatile("movdqa %0, %%xmm2" : : "m" (p[0]));
		asm volatile("pxor %xmm10, %xmm11");
		asm volatile("movdqa %0, %%xmm12" : : "m" (p[16]));

		/* xmm11 = qmul[q[16] ^ dq[16]] */

		asm volatile("pxor %xmm1, %xmm2");

		/* xmm2 = p[0] ^ qmul[q[0] ^ dq[0]] */

		asm volatile("pxor %xmm11, %xmm12");

		/* xmm12 = p[16] ^ qmul[q[16] ^ dq[16]] */

		asm volatile("movdqa %%xmm1, %0" : "=m" (dq[0]));
		asm volatile("movdqa %%xmm11, %0" : "=m" (dq[16]));

		asm volatile("movdqa %%xmm2, %0" : "=m" (p[0]));
		asm volatile("movdqa %%xmm12, %0" : "=m" (p[16]));

		bytes -= 32;
		p += 32;
		q += 32;
		dq += 32;

#else
		asm volatile("movdqa %0, %%xmm3" : : "m" (dq[0]));
		asm volatile("movdqa %0, %%xmm0" : : "m" (qmul[0]));
		asm volatile("pxor %0, %%xmm3" : : "m" (q[0]));
		asm volatile("movdqa %0, %%xmm1" : : "m" (qmul[16]));

		/* xmm3 = *q ^ *dq */

		asm volatile("movdqa %xmm3, %xmm6");
		asm volatile("movdqa %0, %%xmm2" : : "m" (p[0]));
		asm volatile("psraw $4, %xmm3");
		asm volatile("pand %xmm7, %xmm6");
		asm volatile("pand %xmm7, %xmm3");
		asm volatile("pshufb %xmm6, %xmm0");
		asm volatile("pshufb %xmm3, %xmm1");
		asm volatile("pxor %xmm0, %xmm1");

		/* xmm1 = qmul[*q ^ *dq */

		asm volatile("pxor %xmm1, %xmm2");

		/* xmm2 = *p ^ qmul[*q ^ *dq] */

		asm volatile("movdqa %%xmm1, %0" : "=m" (dq[0]));
		asm volatile("movdqa %%xmm2, %0" : "=m" (p[0]));

		bytes -= 16;
		p += 16;
		q += 16;
		dq += 16;
#endif
	}

	kernel_fpu_end();
}

const struct raid6_recov_calls raid6_recov_ssse3 = {
	.data2 = raid6_2data_recov_ssse3,
	.datap = raid6_datap_recov_ssse3,
	.valid = raid6_has_ssse3,
#ifdef CONFIG_X86_64
	.name = "ssse3x2",
#else
	.name = "ssse3x1",
#endif
	.priority = 1,
};
