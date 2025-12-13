// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2024 Institute of Software, CAS.
 * Author: Chunyan Zhang <zhangchunyan@iscas.ac.cn>
 */

#include <asm/vector.h>
#include <linux/raid/pq.h>

static int rvv_has_vector(void)
{
	return has_vector();
}

static void __raid6_2data_recov_rvv(int bytes, u8 *p, u8 *q, u8 *dp,
				    u8 *dq, const u8 *pbmul,
				    const u8 *qmul)
{
	asm volatile (".option	push\n"
		      ".option	arch,+v\n"
		      "vsetvli	x0, %[avl], e8, m1, ta, ma\n"
		      ".option	pop\n"
		      : :
		      [avl]"r"(16)
	);

	/*
	 * while ( bytes-- ) {
	 *	uint8_t px, qx, db;
	 *
	 *	px	  = *p ^ *dp;
	 *	qx	  = qmul[*q ^ *dq];
	 *	*dq++ = db = pbmul[px] ^ qx;
	 *	*dp++ = db ^ px;
	 *	p++; q++;
	 * }
	 */
	while (bytes) {
		/*
		 * v0:px, v1:dp,
		 * v2:qx, v3:dq,
		 * v4:vx, v5:vy,
		 * v6:qm0, v7:qm1,
		 * v8:pm0, v9:pm1,
		 * v14:p/qm[vx], v15:p/qm[vy]
		 */
		asm volatile (".option		push\n"
			      ".option		arch,+v\n"
			      "vle8.v		v0, (%[px])\n"
			      "vle8.v		v1, (%[dp])\n"
			      "vxor.vv		v0, v0, v1\n"
			      "vle8.v		v2, (%[qx])\n"
			      "vle8.v		v3, (%[dq])\n"
			      "vxor.vv		v4, v2, v3\n"
			      "vsrl.vi		v5, v4, 4\n"
			      "vand.vi		v4, v4, 0xf\n"
			      "vle8.v		v6, (%[qm0])\n"
			      "vle8.v		v7, (%[qm1])\n"
			      "vrgather.vv	v14, v6, v4\n" /* v14 = qm[vx] */
			      "vrgather.vv	v15, v7, v5\n" /* v15 = qm[vy] */
			      "vxor.vv		v2, v14, v15\n" /* v2 = qmul[*q ^ *dq] */

			      "vsrl.vi		v5, v0, 4\n"
			      "vand.vi		v4, v0, 0xf\n"
			      "vle8.v		v8, (%[pm0])\n"
			      "vle8.v		v9, (%[pm1])\n"
			      "vrgather.vv	v14, v8, v4\n" /* v14 = pm[vx] */
			      "vrgather.vv	v15, v9, v5\n" /* v15 = pm[vy] */
			      "vxor.vv		v4, v14, v15\n" /* v4 = pbmul[px] */
			      "vxor.vv		v3, v4, v2\n" /* v3 = db = pbmul[px] ^ qx */
			      "vxor.vv		v1, v3, v0\n" /* v1 = db ^ px; */
			      "vse8.v		v3, (%[dq])\n"
			      "vse8.v		v1, (%[dp])\n"
			      ".option		pop\n"
			      : :
			      [px]"r"(p),
			      [dp]"r"(dp),
			      [qx]"r"(q),
			      [dq]"r"(dq),
			      [qm0]"r"(qmul),
			      [qm1]"r"(qmul + 16),
			      [pm0]"r"(pbmul),
			      [pm1]"r"(pbmul + 16)
			      :);

		bytes -= 16;
		p += 16;
		q += 16;
		dp += 16;
		dq += 16;
	}
}

static void __raid6_datap_recov_rvv(int bytes, u8 *p, u8 *q,
				    u8 *dq, const u8 *qmul)
{
	asm volatile (".option	push\n"
		      ".option	arch,+v\n"
		      "vsetvli	x0, %[avl], e8, m1, ta, ma\n"
		      ".option	pop\n"
		      : :
		      [avl]"r"(16)
	);

	/*
	 * while (bytes--) {
	 *  *p++ ^= *dq = qmul[*q ^ *dq];
	 *  q++; dq++;
	 * }
	 */
	while (bytes) {
		/*
		 * v0:vx, v1:vy,
		 * v2:dq, v3:p,
		 * v4:qm0, v5:qm1,
		 * v10:m[vx], v11:m[vy]
		 */
		asm volatile (".option		push\n"
			      ".option		arch,+v\n"
			      "vle8.v		v0, (%[vx])\n"
			      "vle8.v		v2, (%[dq])\n"
			      "vxor.vv		v0, v0, v2\n"
			      "vsrl.vi		v1, v0, 4\n"
			      "vand.vi		v0, v0, 0xf\n"
			      "vle8.v		v4, (%[qm0])\n"
			      "vle8.v		v5, (%[qm1])\n"
			      "vrgather.vv	v10, v4, v0\n"
			      "vrgather.vv	v11, v5, v1\n"
			      "vxor.vv		v0, v10, v11\n"
			      "vle8.v		v1, (%[vy])\n"
			      "vxor.vv		v1, v0, v1\n"
			      "vse8.v		v0, (%[dq])\n"
			      "vse8.v		v1, (%[vy])\n"
			      ".option		pop\n"
			      : :
			      [vx]"r"(q),
			      [vy]"r"(p),
			      [dq]"r"(dq),
			      [qm0]"r"(qmul),
			      [qm1]"r"(qmul + 16)
			      :);

		bytes -= 16;
		p += 16;
		q += 16;
		dq += 16;
	}
}

static void raid6_2data_recov_rvv(int disks, size_t bytes, int faila,
				  int failb, void **ptrs)
{
	u8 *p, *q, *dp, *dq;
	const u8 *pbmul;	/* P multiplier table for B data */
	const u8 *qmul;		/* Q multiplier table (for both) */

	p = (u8 *)ptrs[disks - 2];
	q = (u8 *)ptrs[disks - 1];

	/*
	 * Compute syndrome with zero for the missing data pages
	 * Use the dead data pages as temporary storage for
	 * delta p and delta q
	 */
	dp = (u8 *)ptrs[faila];
	ptrs[faila] = raid6_get_zero_page();
	ptrs[disks - 2] = dp;
	dq = (u8 *)ptrs[failb];
	ptrs[failb] = raid6_get_zero_page();
	ptrs[disks - 1] = dq;

	raid6_call.gen_syndrome(disks, bytes, ptrs);

	/* Restore pointer table */
	ptrs[faila]     = dp;
	ptrs[failb]     = dq;
	ptrs[disks - 2] = p;
	ptrs[disks - 1] = q;

	/* Now, pick the proper data tables */
	pbmul = raid6_vgfmul[raid6_gfexi[failb - faila]];
	qmul  = raid6_vgfmul[raid6_gfinv[raid6_gfexp[faila] ^
					 raid6_gfexp[failb]]];

	kernel_vector_begin();
	__raid6_2data_recov_rvv(bytes, p, q, dp, dq, pbmul, qmul);
	kernel_vector_end();
}

static void raid6_datap_recov_rvv(int disks, size_t bytes, int faila,
				  void **ptrs)
{
	u8 *p, *q, *dq;
	const u8 *qmul;		/* Q multiplier table */

	p = (u8 *)ptrs[disks - 2];
	q = (u8 *)ptrs[disks - 1];

	/*
	 * Compute syndrome with zero for the missing data page
	 * Use the dead data page as temporary storage for delta q
	 */
	dq = (u8 *)ptrs[faila];
	ptrs[faila] = raid6_get_zero_page();
	ptrs[disks - 1] = dq;

	raid6_call.gen_syndrome(disks, bytes, ptrs);

	/* Restore pointer table */
	ptrs[faila]     = dq;
	ptrs[disks - 1] = q;

	/* Now, pick the proper data tables */
	qmul = raid6_vgfmul[raid6_gfinv[raid6_gfexp[faila]]];

	kernel_vector_begin();
	__raid6_datap_recov_rvv(bytes, p, q, dq, qmul);
	kernel_vector_end();
}

const struct raid6_recov_calls raid6_recov_rvv = {
	.data2		= raid6_2data_recov_rvv,
	.datap		= raid6_datap_recov_rvv,
	.valid		= rvv_has_vector,
	.name		= "rvv",
	.priority	= 1,
};
