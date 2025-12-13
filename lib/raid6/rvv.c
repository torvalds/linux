// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * RAID-6 syndrome calculation using RISC-V vector instructions
 *
 * Copyright 2024 Institute of Software, CAS.
 * Author: Chunyan Zhang <zhangchunyan@iscas.ac.cn>
 *
 * Based on neon.uc:
 *	Copyright 2002-2004 H. Peter Anvin
 */

#include <asm/vector.h>
#include <linux/raid/pq.h>
#include "rvv.h"

#define NSIZE	(riscv_v_vsize / 32) /* NSIZE = vlenb */

static int rvv_has_vector(void)
{
	return has_vector();
}

static void raid6_rvv1_gen_syndrome_real(int disks, unsigned long bytes, void **ptrs)
{
	u8 **dptr = (u8 **)ptrs;
	u8 *p, *q;
	unsigned long vl, d;
	int z, z0;

	z0 = disks - 3;		/* Highest data disk */
	p = dptr[z0 + 1];		/* XOR parity */
	q = dptr[z0 + 2];		/* RS syndrome */

	asm volatile (".option	push\n"
		      ".option	arch,+v\n"
		      "vsetvli	%0, x0, e8, m1, ta, ma\n"
		      ".option	pop\n"
		      : "=&r" (vl)
	);

	 /* v0:wp0, v1:wq0, v2:wd0/w20, v3:w10 */
	for (d = 0; d < bytes; d += NSIZE * 1) {
		/* wq$$ = wp$$ = *(unative_t *)&dptr[z0][d+$$*NSIZE]; */
		asm volatile (".option	push\n"
			      ".option	arch,+v\n"
			      "vle8.v	v0, (%[wp0])\n"
			      "vmv.v.v	v1, v0\n"
			      ".option	pop\n"
			      : :
			      [wp0]"r"(&dptr[z0][d + 0 * NSIZE])
		);

		for (z = z0 - 1 ; z >= 0 ; z--) {
			/*
			 * w2$$ = MASK(wq$$);
			 * w1$$ = SHLBYTE(wq$$);
			 * w2$$ &= NBYTES(0x1d);
			 * w1$$ ^= w2$$;
			 * wd$$ = *(unative_t *)&dptr[z][d+$$*NSIZE];
			 * wq$$ = w1$$ ^ wd$$;
			 * wp$$ ^= wd$$;
			 */
			asm volatile (".option	push\n"
				      ".option	arch,+v\n"
				      "vsra.vi	v2, v1, 7\n"
				      "vsll.vi	v3, v1, 1\n"
				      "vand.vx	v2, v2, %[x1d]\n"
				      "vxor.vv	v3, v3, v2\n"
				      "vle8.v	v2, (%[wd0])\n"
				      "vxor.vv	v1, v3, v2\n"
				      "vxor.vv	v0, v0, v2\n"
				      ".option	pop\n"
				      : :
				      [wd0]"r"(&dptr[z][d + 0 * NSIZE]),
				      [x1d]"r"(0x1d)
			);
		}

		/*
		 * *(unative_t *)&p[d+NSIZE*$$] = wp$$;
		 * *(unative_t *)&q[d+NSIZE*$$] = wq$$;
		 */
		asm volatile (".option	push\n"
			      ".option	arch,+v\n"
			      "vse8.v	v0, (%[wp0])\n"
			      "vse8.v	v1, (%[wq0])\n"
			      ".option	pop\n"
			      : :
			      [wp0]"r"(&p[d + NSIZE * 0]),
			      [wq0]"r"(&q[d + NSIZE * 0])
		);
	}
}

static void raid6_rvv1_xor_syndrome_real(int disks, int start, int stop,
					 unsigned long bytes, void **ptrs)
{
	u8 **dptr = (u8 **)ptrs;
	u8 *p, *q;
	unsigned long vl, d;
	int z, z0;

	z0 = stop;		/* P/Q right side optimization */
	p = dptr[disks - 2];	/* XOR parity */
	q = dptr[disks - 1];	/* RS syndrome */

	asm volatile (".option	push\n"
		      ".option	arch,+v\n"
		      "vsetvli	%0, x0, e8, m1, ta, ma\n"
		      ".option	pop\n"
		      : "=&r" (vl)
	);

	/* v0:wp0, v1:wq0, v2:wd0/w20, v3:w10 */
	for (d = 0 ; d < bytes ; d += NSIZE * 1) {
		/* wq$$ = wp$$ = *(unative_t *)&dptr[z0][d+$$*NSIZE]; */
		asm volatile (".option	push\n"
			      ".option	arch,+v\n"
			      "vle8.v	v0, (%[wp0])\n"
			      "vmv.v.v	v1, v0\n"
			      ".option	pop\n"
			      : :
			      [wp0]"r"(&dptr[z0][d + 0 * NSIZE])
		);

		/* P/Q data pages */
		for (z = z0 - 1; z >= start; z--) {
			/*
			 * w2$$ = MASK(wq$$);
			 * w1$$ = SHLBYTE(wq$$);
			 * w2$$ &= NBYTES(0x1d);
			 * w1$$ ^= w2$$;
			 * wd$$ = *(unative_t *)&dptr[z][d+$$*NSIZE];
			 * wq$$ = w1$$ ^ wd$$;
			 * wp$$ ^= wd$$;
			 */
			asm volatile (".option	push\n"
				      ".option	arch,+v\n"
				      "vsra.vi	v2, v1, 7\n"
				      "vsll.vi	v3, v1, 1\n"
				      "vand.vx	v2, v2, %[x1d]\n"
				      "vxor.vv	v3, v3, v2\n"
				      "vle8.v	v2, (%[wd0])\n"
				      "vxor.vv	v1, v3, v2\n"
				      "vxor.vv	v0, v0, v2\n"
				      ".option	pop\n"
				      : :
				      [wd0]"r"(&dptr[z][d + 0 * NSIZE]),
				      [x1d]"r"(0x1d)
			);
		}

		/* P/Q left side optimization */
		for (z = start - 1; z >= 0; z--) {
			/*
			 * w2$$ = MASK(wq$$);
			 * w1$$ = SHLBYTE(wq$$);
			 * w2$$ &= NBYTES(0x1d);
			 * wq$$ = w1$$ ^ w2$$;
			 */
			asm volatile (".option	push\n"
				      ".option	arch,+v\n"
				      "vsra.vi	v2, v1, 7\n"
				      "vsll.vi	v3, v1, 1\n"
				      "vand.vx	v2, v2, %[x1d]\n"
				      "vxor.vv	v1, v3, v2\n"
				      ".option	pop\n"
				      : :
				      [x1d]"r"(0x1d)
			);
		}

		/*
		 * *(unative_t *)&p[d+NSIZE*$$] ^= wp$$;
		 * *(unative_t *)&q[d+NSIZE*$$] ^= wq$$;
		 * v0:wp0, v1:wq0, v2:p0, v3:q0
		 */
		asm volatile (".option	push\n"
			      ".option	arch,+v\n"
			      "vle8.v	v2, (%[wp0])\n"
			      "vle8.v	v3, (%[wq0])\n"
			      "vxor.vv	v2, v2, v0\n"
			      "vxor.vv	v3, v3, v1\n"
			      "vse8.v	v2, (%[wp0])\n"
			      "vse8.v	v3, (%[wq0])\n"
			      ".option	pop\n"
			      : :
			      [wp0]"r"(&p[d + NSIZE * 0]),
			      [wq0]"r"(&q[d + NSIZE * 0])
		);
	}
}

static void raid6_rvv2_gen_syndrome_real(int disks, unsigned long bytes, void **ptrs)
{
	u8 **dptr = (u8 **)ptrs;
	u8 *p, *q;
	unsigned long vl, d;
	int z, z0;

	z0 = disks - 3;		/* Highest data disk */
	p = dptr[z0 + 1];		/* XOR parity */
	q = dptr[z0 + 2];		/* RS syndrome */

	asm volatile (".option	push\n"
		      ".option	arch,+v\n"
		      "vsetvli	%0, x0, e8, m1, ta, ma\n"
		      ".option	pop\n"
		      : "=&r" (vl)
	);

	/*
	 * v0:wp0, v1:wq0, v2:wd0/w20, v3:w10
	 * v4:wp1, v5:wq1, v6:wd1/w21, v7:w11
	 */
	for (d = 0; d < bytes; d += NSIZE * 2) {
		/* wq$$ = wp$$ = *(unative_t *)&dptr[z0][d+$$*NSIZE]; */
		asm volatile (".option	push\n"
			      ".option	arch,+v\n"
			      "vle8.v	v0, (%[wp0])\n"
			      "vmv.v.v	v1, v0\n"
			      "vle8.v	v4, (%[wp1])\n"
			      "vmv.v.v	v5, v4\n"
			      ".option	pop\n"
			      : :
			      [wp0]"r"(&dptr[z0][d + 0 * NSIZE]),
			      [wp1]"r"(&dptr[z0][d + 1 * NSIZE])
		);

		for (z = z0 - 1; z >= 0; z--) {
			/*
			 * w2$$ = MASK(wq$$);
			 * w1$$ = SHLBYTE(wq$$);
			 * w2$$ &= NBYTES(0x1d);
			 * w1$$ ^= w2$$;
			 * wd$$ = *(unative_t *)&dptr[z][d+$$*NSIZE];
			 * wq$$ = w1$$ ^ wd$$;
			 * wp$$ ^= wd$$;
			 */
			asm volatile (".option	push\n"
				      ".option	arch,+v\n"
				      "vsra.vi	v2, v1, 7\n"
				      "vsll.vi	v3, v1, 1\n"
				      "vand.vx	v2, v2, %[x1d]\n"
				      "vxor.vv	v3, v3, v2\n"
				      "vle8.v	v2, (%[wd0])\n"
				      "vxor.vv	v1, v3, v2\n"
				      "vxor.vv	v0, v0, v2\n"

				      "vsra.vi	v6, v5, 7\n"
				      "vsll.vi	v7, v5, 1\n"
				      "vand.vx	v6, v6, %[x1d]\n"
				      "vxor.vv	v7, v7, v6\n"
				      "vle8.v	v6, (%[wd1])\n"
				      "vxor.vv	v5, v7, v6\n"
				      "vxor.vv	v4, v4, v6\n"
				      ".option	pop\n"
				      : :
				      [wd0]"r"(&dptr[z][d + 0 * NSIZE]),
				      [wd1]"r"(&dptr[z][d + 1 * NSIZE]),
				      [x1d]"r"(0x1d)
			);
		}

		/*
		 * *(unative_t *)&p[d+NSIZE*$$] = wp$$;
		 * *(unative_t *)&q[d+NSIZE*$$] = wq$$;
		 */
		asm volatile (".option	push\n"
			      ".option	arch,+v\n"
			      "vse8.v	v0, (%[wp0])\n"
			      "vse8.v	v1, (%[wq0])\n"
			      "vse8.v	v4, (%[wp1])\n"
			      "vse8.v	v5, (%[wq1])\n"
			      ".option	pop\n"
			      : :
			      [wp0]"r"(&p[d + NSIZE * 0]),
			      [wq0]"r"(&q[d + NSIZE * 0]),
			      [wp1]"r"(&p[d + NSIZE * 1]),
			      [wq1]"r"(&q[d + NSIZE * 1])
		);
	}
}

static void raid6_rvv2_xor_syndrome_real(int disks, int start, int stop,
					 unsigned long bytes, void **ptrs)
{
	u8 **dptr = (u8 **)ptrs;
	u8 *p, *q;
	unsigned long vl, d;
	int z, z0;

	z0 = stop;		/* P/Q right side optimization */
	p = dptr[disks - 2];	/* XOR parity */
	q = dptr[disks - 1];	/* RS syndrome */

	asm volatile (".option	push\n"
		      ".option	arch,+v\n"
		      "vsetvli	%0, x0, e8, m1, ta, ma\n"
		      ".option	pop\n"
		      : "=&r" (vl)
	);

	/*
	 * v0:wp0, v1:wq0, v2:wd0/w20, v3:w10
	 * v4:wp1, v5:wq1, v6:wd1/w21, v7:w11
	 */
	for (d = 0; d < bytes; d += NSIZE * 2) {
		 /* wq$$ = wp$$ = *(unative_t *)&dptr[z0][d+$$*NSIZE]; */
		asm volatile (".option	push\n"
			      ".option	arch,+v\n"
			      "vle8.v	v0, (%[wp0])\n"
			      "vmv.v.v	v1, v0\n"
			      "vle8.v	v4, (%[wp1])\n"
			      "vmv.v.v	v5, v4\n"
			      ".option	pop\n"
			      : :
			      [wp0]"r"(&dptr[z0][d + 0 * NSIZE]),
			      [wp1]"r"(&dptr[z0][d + 1 * NSIZE])
		);

		/* P/Q data pages */
		for (z = z0 - 1; z >= start; z--) {
			/*
			 * w2$$ = MASK(wq$$);
			 * w1$$ = SHLBYTE(wq$$);
			 * w2$$ &= NBYTES(0x1d);
			 * w1$$ ^= w2$$;
			 * wd$$ = *(unative_t *)&dptr[z][d+$$*NSIZE];
			 * wq$$ = w1$$ ^ wd$$;
			 * wp$$ ^= wd$$;
			 */
			asm volatile (".option	push\n"
				      ".option	arch,+v\n"
				      "vsra.vi	v2, v1, 7\n"
				      "vsll.vi	v3, v1, 1\n"
				      "vand.vx	v2, v2, %[x1d]\n"
				      "vxor.vv	v3, v3, v2\n"
				      "vle8.v	v2, (%[wd0])\n"
				      "vxor.vv	v1, v3, v2\n"
				      "vxor.vv	v0, v0, v2\n"

				      "vsra.vi	v6, v5, 7\n"
				      "vsll.vi	v7, v5, 1\n"
				      "vand.vx	v6, v6, %[x1d]\n"
				      "vxor.vv	v7, v7, v6\n"
				      "vle8.v	v6, (%[wd1])\n"
				      "vxor.vv	v5, v7, v6\n"
				      "vxor.vv	v4, v4, v6\n"
				      ".option	pop\n"
				      : :
				      [wd0]"r"(&dptr[z][d + 0 * NSIZE]),
				      [wd1]"r"(&dptr[z][d + 1 * NSIZE]),
				      [x1d]"r"(0x1d)
			);
		}

		/* P/Q left side optimization */
		for (z = start - 1; z >= 0; z--) {
			/*
			 * w2$$ = MASK(wq$$);
			 * w1$$ = SHLBYTE(wq$$);
			 * w2$$ &= NBYTES(0x1d);
			 * wq$$ = w1$$ ^ w2$$;
			 */
			asm volatile (".option	push\n"
				      ".option	arch,+v\n"
				      "vsra.vi	v2, v1, 7\n"
				      "vsll.vi	v3, v1, 1\n"
				      "vand.vx	v2, v2, %[x1d]\n"
				      "vxor.vv	v1, v3, v2\n"

				      "vsra.vi	v6, v5, 7\n"
				      "vsll.vi	v7, v5, 1\n"
				      "vand.vx	v6, v6, %[x1d]\n"
				      "vxor.vv	v5, v7, v6\n"
				      ".option	pop\n"
				      : :
				      [x1d]"r"(0x1d)
			);
		}

		/*
		 * *(unative_t *)&p[d+NSIZE*$$] ^= wp$$;
		 * *(unative_t *)&q[d+NSIZE*$$] ^= wq$$;
		 * v0:wp0, v1:wq0, v2:p0, v3:q0
		 * v4:wp1, v5:wq1, v6:p1, v7:q1
		 */
		asm volatile (".option	push\n"
			      ".option	arch,+v\n"
			      "vle8.v	v2, (%[wp0])\n"
			      "vle8.v	v3, (%[wq0])\n"
			      "vxor.vv	v2, v2, v0\n"
			      "vxor.vv	v3, v3, v1\n"
			      "vse8.v	v2, (%[wp0])\n"
			      "vse8.v	v3, (%[wq0])\n"

			      "vle8.v	v6, (%[wp1])\n"
			      "vle8.v	v7, (%[wq1])\n"
			      "vxor.vv	v6, v6, v4\n"
			      "vxor.vv	v7, v7, v5\n"
			      "vse8.v	v6, (%[wp1])\n"
			      "vse8.v	v7, (%[wq1])\n"
			      ".option	pop\n"
			      : :
			      [wp0]"r"(&p[d + NSIZE * 0]),
			      [wq0]"r"(&q[d + NSIZE * 0]),
			      [wp1]"r"(&p[d + NSIZE * 1]),
			      [wq1]"r"(&q[d + NSIZE * 1])
		);
	}
}

static void raid6_rvv4_gen_syndrome_real(int disks, unsigned long bytes, void **ptrs)
{
	u8 **dptr = (u8 **)ptrs;
	u8 *p, *q;
	unsigned long vl, d;
	int z, z0;

	z0 = disks - 3;	/* Highest data disk */
	p = dptr[z0 + 1];	/* XOR parity */
	q = dptr[z0 + 2];	/* RS syndrome */

	asm volatile (".option	push\n"
		      ".option	arch,+v\n"
		      "vsetvli	%0, x0, e8, m1, ta, ma\n"
		      ".option	pop\n"
		      : "=&r" (vl)
	);

	/*
	 * v0:wp0, v1:wq0, v2:wd0/w20, v3:w10
	 * v4:wp1, v5:wq1, v6:wd1/w21, v7:w11
	 * v8:wp2, v9:wq2, v10:wd2/w22, v11:w12
	 * v12:wp3, v13:wq3, v14:wd3/w23, v15:w13
	 */
	for (d = 0; d < bytes; d += NSIZE * 4) {
		/* wq$$ = wp$$ = *(unative_t *)&dptr[z0][d+$$*NSIZE]; */
		asm volatile (".option	push\n"
			      ".option	arch,+v\n"
			      "vle8.v	v0, (%[wp0])\n"
			      "vmv.v.v	v1, v0\n"
			      "vle8.v	v4, (%[wp1])\n"
			      "vmv.v.v	v5, v4\n"
			      "vle8.v	v8, (%[wp2])\n"
			      "vmv.v.v	v9, v8\n"
			      "vle8.v	v12, (%[wp3])\n"
			      "vmv.v.v	v13, v12\n"
			      ".option	pop\n"
			      : :
			      [wp0]"r"(&dptr[z0][d + 0 * NSIZE]),
			      [wp1]"r"(&dptr[z0][d + 1 * NSIZE]),
			      [wp2]"r"(&dptr[z0][d + 2 * NSIZE]),
			      [wp3]"r"(&dptr[z0][d + 3 * NSIZE])
		);

		for (z = z0 - 1; z >= 0; z--) {
			/*
			 * w2$$ = MASK(wq$$);
			 * w1$$ = SHLBYTE(wq$$);
			 * w2$$ &= NBYTES(0x1d);
			 * w1$$ ^= w2$$;
			 * wd$$ = *(unative_t *)&dptr[z][d+$$*NSIZE];
			 * wq$$ = w1$$ ^ wd$$;
			 * wp$$ ^= wd$$;
			 */
			asm volatile (".option	push\n"
				      ".option	arch,+v\n"
				      "vsra.vi	v2, v1, 7\n"
				      "vsll.vi	v3, v1, 1\n"
				      "vand.vx	v2, v2, %[x1d]\n"
				      "vxor.vv	v3, v3, v2\n"
				      "vle8.v	v2, (%[wd0])\n"
				      "vxor.vv	v1, v3, v2\n"
				      "vxor.vv	v0, v0, v2\n"

				      "vsra.vi	v6, v5, 7\n"
				      "vsll.vi	v7, v5, 1\n"
				      "vand.vx	v6, v6, %[x1d]\n"
				      "vxor.vv	v7, v7, v6\n"
				      "vle8.v	v6, (%[wd1])\n"
				      "vxor.vv	v5, v7, v6\n"
				      "vxor.vv	v4, v4, v6\n"

				      "vsra.vi	v10, v9, 7\n"
				      "vsll.vi	v11, v9, 1\n"
				      "vand.vx	v10, v10, %[x1d]\n"
				      "vxor.vv	v11, v11, v10\n"
				      "vle8.v	v10, (%[wd2])\n"
				      "vxor.vv	v9, v11, v10\n"
				      "vxor.vv	v8, v8, v10\n"

				      "vsra.vi	v14, v13, 7\n"
				      "vsll.vi	v15, v13, 1\n"
				      "vand.vx	v14, v14, %[x1d]\n"
				      "vxor.vv	v15, v15, v14\n"
				      "vle8.v	v14, (%[wd3])\n"
				      "vxor.vv	v13, v15, v14\n"
				      "vxor.vv	v12, v12, v14\n"
				      ".option	pop\n"
				      : :
				      [wd0]"r"(&dptr[z][d + 0 * NSIZE]),
				      [wd1]"r"(&dptr[z][d + 1 * NSIZE]),
				      [wd2]"r"(&dptr[z][d + 2 * NSIZE]),
				      [wd3]"r"(&dptr[z][d + 3 * NSIZE]),
				      [x1d]"r"(0x1d)
			);
		}

		/*
		 * *(unative_t *)&p[d+NSIZE*$$] = wp$$;
		 * *(unative_t *)&q[d+NSIZE*$$] = wq$$;
		 */
		asm volatile (".option	push\n"
			      ".option	arch,+v\n"
			      "vse8.v	v0, (%[wp0])\n"
			      "vse8.v	v1, (%[wq0])\n"
			      "vse8.v	v4, (%[wp1])\n"
			      "vse8.v	v5, (%[wq1])\n"
			      "vse8.v	v8, (%[wp2])\n"
			      "vse8.v	v9, (%[wq2])\n"
			      "vse8.v	v12, (%[wp3])\n"
			      "vse8.v	v13, (%[wq3])\n"
			      ".option	pop\n"
			      : :
			      [wp0]"r"(&p[d + NSIZE * 0]),
			      [wq0]"r"(&q[d + NSIZE * 0]),
			      [wp1]"r"(&p[d + NSIZE * 1]),
			      [wq1]"r"(&q[d + NSIZE * 1]),
			      [wp2]"r"(&p[d + NSIZE * 2]),
			      [wq2]"r"(&q[d + NSIZE * 2]),
			      [wp3]"r"(&p[d + NSIZE * 3]),
			      [wq3]"r"(&q[d + NSIZE * 3])
		);
	}
}

static void raid6_rvv4_xor_syndrome_real(int disks, int start, int stop,
					 unsigned long bytes, void **ptrs)
{
	u8 **dptr = (u8 **)ptrs;
	u8 *p, *q;
	unsigned long vl, d;
	int z, z0;

	z0 = stop;		/* P/Q right side optimization */
	p = dptr[disks - 2];	/* XOR parity */
	q = dptr[disks - 1];	/* RS syndrome */

	asm volatile (".option	push\n"
		      ".option	arch,+v\n"
		      "vsetvli	%0, x0, e8, m1, ta, ma\n"
		      ".option	pop\n"
		      : "=&r" (vl)
	);

	/*
	 * v0:wp0, v1:wq0, v2:wd0/w20, v3:w10
	 * v4:wp1, v5:wq1, v6:wd1/w21, v7:w11
	 * v8:wp2, v9:wq2, v10:wd2/w22, v11:w12
	 * v12:wp3, v13:wq3, v14:wd3/w23, v15:w13
	 */
	for (d = 0; d < bytes; d += NSIZE * 4) {
		 /* wq$$ = wp$$ = *(unative_t *)&dptr[z0][d+$$*NSIZE]; */
		asm volatile (".option	push\n"
			      ".option	arch,+v\n"
			      "vle8.v	v0, (%[wp0])\n"
			      "vmv.v.v	v1, v0\n"
			      "vle8.v	v4, (%[wp1])\n"
			      "vmv.v.v	v5, v4\n"
			      "vle8.v	v8, (%[wp2])\n"
			      "vmv.v.v	v9, v8\n"
			      "vle8.v	v12, (%[wp3])\n"
			      "vmv.v.v	v13, v12\n"
			      ".option	pop\n"
			      : :
			      [wp0]"r"(&dptr[z0][d + 0 * NSIZE]),
			      [wp1]"r"(&dptr[z0][d + 1 * NSIZE]),
			      [wp2]"r"(&dptr[z0][d + 2 * NSIZE]),
			      [wp3]"r"(&dptr[z0][d + 3 * NSIZE])
		);

		/* P/Q data pages */
		for (z = z0 - 1; z >= start; z--) {
			/*
			 * w2$$ = MASK(wq$$);
			 * w1$$ = SHLBYTE(wq$$);
			 * w2$$ &= NBYTES(0x1d);
			 * w1$$ ^= w2$$;
			 * wd$$ = *(unative_t *)&dptr[z][d+$$*NSIZE];
			 * wq$$ = w1$$ ^ wd$$;
			 * wp$$ ^= wd$$;
			 */
			asm volatile (".option	push\n"
				      ".option	arch,+v\n"
				      "vsra.vi	v2, v1, 7\n"
				      "vsll.vi	v3, v1, 1\n"
				      "vand.vx	v2, v2, %[x1d]\n"
				      "vxor.vv	v3, v3, v2\n"
				      "vle8.v	v2, (%[wd0])\n"
				      "vxor.vv	v1, v3, v2\n"
				      "vxor.vv	v0, v0, v2\n"

				      "vsra.vi	v6, v5, 7\n"
				      "vsll.vi	v7, v5, 1\n"
				      "vand.vx	v6, v6, %[x1d]\n"
				      "vxor.vv	v7, v7, v6\n"
				      "vle8.v	v6, (%[wd1])\n"
				      "vxor.vv	v5, v7, v6\n"
				      "vxor.vv	v4, v4, v6\n"

				      "vsra.vi	v10, v9, 7\n"
				      "vsll.vi	v11, v9, 1\n"
				      "vand.vx	v10, v10, %[x1d]\n"
				      "vxor.vv	v11, v11, v10\n"
				      "vle8.v	v10, (%[wd2])\n"
				      "vxor.vv	v9, v11, v10\n"
				      "vxor.vv	v8, v8, v10\n"

				      "vsra.vi	v14, v13, 7\n"
				      "vsll.vi	v15, v13, 1\n"
				      "vand.vx	v14, v14, %[x1d]\n"
				      "vxor.vv	v15, v15, v14\n"
				      "vle8.v	v14, (%[wd3])\n"
				      "vxor.vv	v13, v15, v14\n"
				      "vxor.vv	v12, v12, v14\n"
				      ".option	pop\n"
				      : :
				      [wd0]"r"(&dptr[z][d + 0 * NSIZE]),
				      [wd1]"r"(&dptr[z][d + 1 * NSIZE]),
				      [wd2]"r"(&dptr[z][d + 2 * NSIZE]),
				      [wd3]"r"(&dptr[z][d + 3 * NSIZE]),
				      [x1d]"r"(0x1d)
			);
		}

		/* P/Q left side optimization */
		for (z = start - 1; z >= 0; z--) {
			/*
			 * w2$$ = MASK(wq$$);
			 * w1$$ = SHLBYTE(wq$$);
			 * w2$$ &= NBYTES(0x1d);
			 * wq$$ = w1$$ ^ w2$$;
			 */
			asm volatile (".option	push\n"
				      ".option	arch,+v\n"
				      "vsra.vi	v2, v1, 7\n"
				      "vsll.vi	v3, v1, 1\n"
				      "vand.vx	v2, v2, %[x1d]\n"
				      "vxor.vv	v1, v3, v2\n"

				      "vsra.vi	v6, v5, 7\n"
				      "vsll.vi	v7, v5, 1\n"
				      "vand.vx	v6, v6, %[x1d]\n"
				      "vxor.vv	v5, v7, v6\n"

				      "vsra.vi	v10, v9, 7\n"
				      "vsll.vi	v11, v9, 1\n"
				      "vand.vx	v10, v10, %[x1d]\n"
				      "vxor.vv	v9, v11, v10\n"

				      "vsra.vi	v14, v13, 7\n"
				      "vsll.vi	v15, v13, 1\n"
				      "vand.vx	v14, v14, %[x1d]\n"
				      "vxor.vv	v13, v15, v14\n"
				      ".option	pop\n"
				      : :
				      [x1d]"r"(0x1d)
			);
		}

		/*
		 * *(unative_t *)&p[d+NSIZE*$$] ^= wp$$;
		 * *(unative_t *)&q[d+NSIZE*$$] ^= wq$$;
		 * v0:wp0, v1:wq0, v2:p0, v3:q0
		 * v4:wp1, v5:wq1, v6:p1, v7:q1
		 * v8:wp2, v9:wq2, v10:p2, v11:q2
		 * v12:wp3, v13:wq3, v14:p3, v15:q3
		 */
		asm volatile (".option	push\n"
			      ".option	arch,+v\n"
			      "vle8.v	v2, (%[wp0])\n"
			      "vle8.v	v3, (%[wq0])\n"
			      "vxor.vv	v2, v2, v0\n"
			      "vxor.vv	v3, v3, v1\n"
			      "vse8.v	v2, (%[wp0])\n"
			      "vse8.v	v3, (%[wq0])\n"

			      "vle8.v	v6, (%[wp1])\n"
			      "vle8.v	v7, (%[wq1])\n"
			      "vxor.vv	v6, v6, v4\n"
			      "vxor.vv	v7, v7, v5\n"
			      "vse8.v	v6, (%[wp1])\n"
			      "vse8.v	v7, (%[wq1])\n"

			      "vle8.v	v10, (%[wp2])\n"
			      "vle8.v	v11, (%[wq2])\n"
			      "vxor.vv	v10, v10, v8\n"
			      "vxor.vv	v11, v11, v9\n"
			      "vse8.v	v10, (%[wp2])\n"
			      "vse8.v	v11, (%[wq2])\n"

			      "vle8.v	v14, (%[wp3])\n"
			      "vle8.v	v15, (%[wq3])\n"
			      "vxor.vv	v14, v14, v12\n"
			      "vxor.vv	v15, v15, v13\n"
			      "vse8.v	v14, (%[wp3])\n"
			      "vse8.v	v15, (%[wq3])\n"
			      ".option	pop\n"
			      : :
			      [wp0]"r"(&p[d + NSIZE * 0]),
			      [wq0]"r"(&q[d + NSIZE * 0]),
			      [wp1]"r"(&p[d + NSIZE * 1]),
			      [wq1]"r"(&q[d + NSIZE * 1]),
			      [wp2]"r"(&p[d + NSIZE * 2]),
			      [wq2]"r"(&q[d + NSIZE * 2]),
			      [wp3]"r"(&p[d + NSIZE * 3]),
			      [wq3]"r"(&q[d + NSIZE * 3])
		);
	}
}

static void raid6_rvv8_gen_syndrome_real(int disks, unsigned long bytes, void **ptrs)
{
	u8 **dptr = (u8 **)ptrs;
	u8 *p, *q;
	unsigned long vl, d;
	int z, z0;

	z0 = disks - 3;	/* Highest data disk */
	p = dptr[z0 + 1];	/* XOR parity */
	q = dptr[z0 + 2];	/* RS syndrome */

	asm volatile (".option	push\n"
		      ".option	arch,+v\n"
		      "vsetvli	%0, x0, e8, m1, ta, ma\n"
		      ".option	pop\n"
		      : "=&r" (vl)
	);

	/*
	 * v0:wp0,   v1:wq0,  v2:wd0/w20,  v3:w10
	 * v4:wp1,   v5:wq1,  v6:wd1/w21,  v7:w11
	 * v8:wp2,   v9:wq2, v10:wd2/w22, v11:w12
	 * v12:wp3, v13:wq3, v14:wd3/w23, v15:w13
	 * v16:wp4, v17:wq4, v18:wd4/w24, v19:w14
	 * v20:wp5, v21:wq5, v22:wd5/w25, v23:w15
	 * v24:wp6, v25:wq6, v26:wd6/w26, v27:w16
	 * v28:wp7, v29:wq7, v30:wd7/w27, v31:w17
	 */
	for (d = 0; d < bytes; d += NSIZE * 8) {
		/* wq$$ = wp$$ = *(unative_t *)&dptr[z0][d+$$*NSIZE]; */
		asm volatile (".option	push\n"
			      ".option	arch,+v\n"
			      "vle8.v	v0, (%[wp0])\n"
			      "vmv.v.v	v1, v0\n"
			      "vle8.v	v4, (%[wp1])\n"
			      "vmv.v.v	v5, v4\n"
			      "vle8.v	v8, (%[wp2])\n"
			      "vmv.v.v	v9, v8\n"
			      "vle8.v	v12, (%[wp3])\n"
			      "vmv.v.v	v13, v12\n"
			      "vle8.v	v16, (%[wp4])\n"
			      "vmv.v.v	v17, v16\n"
			      "vle8.v	v20, (%[wp5])\n"
			      "vmv.v.v	v21, v20\n"
			      "vle8.v	v24, (%[wp6])\n"
			      "vmv.v.v	v25, v24\n"
			      "vle8.v	v28, (%[wp7])\n"
			      "vmv.v.v	v29, v28\n"
			      ".option	pop\n"
			      : :
			      [wp0]"r"(&dptr[z0][d + 0 * NSIZE]),
			      [wp1]"r"(&dptr[z0][d + 1 * NSIZE]),
			      [wp2]"r"(&dptr[z0][d + 2 * NSIZE]),
			      [wp3]"r"(&dptr[z0][d + 3 * NSIZE]),
			      [wp4]"r"(&dptr[z0][d + 4 * NSIZE]),
			      [wp5]"r"(&dptr[z0][d + 5 * NSIZE]),
			      [wp6]"r"(&dptr[z0][d + 6 * NSIZE]),
			      [wp7]"r"(&dptr[z0][d + 7 * NSIZE])
		);

		for (z = z0 - 1; z >= 0; z--) {
			/*
			 * w2$$ = MASK(wq$$);
			 * w1$$ = SHLBYTE(wq$$);
			 * w2$$ &= NBYTES(0x1d);
			 * w1$$ ^= w2$$;
			 * wd$$ = *(unative_t *)&dptr[z][d+$$*NSIZE];
			 * wq$$ = w1$$ ^ wd$$;
			 * wp$$ ^= wd$$;
			 */
			asm volatile (".option	push\n"
				      ".option	arch,+v\n"
				      "vsra.vi	v2, v1, 7\n"
				      "vsll.vi	v3, v1, 1\n"
				      "vand.vx	v2, v2, %[x1d]\n"
				      "vxor.vv	v3, v3, v2\n"
				      "vle8.v	v2, (%[wd0])\n"
				      "vxor.vv	v1, v3, v2\n"
				      "vxor.vv	v0, v0, v2\n"

				      "vsra.vi	v6, v5, 7\n"
				      "vsll.vi	v7, v5, 1\n"
				      "vand.vx	v6, v6, %[x1d]\n"
				      "vxor.vv	v7, v7, v6\n"
				      "vle8.v	v6, (%[wd1])\n"
				      "vxor.vv	v5, v7, v6\n"
				      "vxor.vv	v4, v4, v6\n"

				      "vsra.vi	v10, v9, 7\n"
				      "vsll.vi	v11, v9, 1\n"
				      "vand.vx	v10, v10, %[x1d]\n"
				      "vxor.vv	v11, v11, v10\n"
				      "vle8.v	v10, (%[wd2])\n"
				      "vxor.vv	v9, v11, v10\n"
				      "vxor.vv	v8, v8, v10\n"

				      "vsra.vi	v14, v13, 7\n"
				      "vsll.vi	v15, v13, 1\n"
				      "vand.vx	v14, v14, %[x1d]\n"
				      "vxor.vv	v15, v15, v14\n"
				      "vle8.v	v14, (%[wd3])\n"
				      "vxor.vv	v13, v15, v14\n"
				      "vxor.vv	v12, v12, v14\n"

				      "vsra.vi	v18, v17, 7\n"
				      "vsll.vi	v19, v17, 1\n"
				      "vand.vx	v18, v18, %[x1d]\n"
				      "vxor.vv	v19, v19, v18\n"
				      "vle8.v	v18, (%[wd4])\n"
				      "vxor.vv	v17, v19, v18\n"
				      "vxor.vv	v16, v16, v18\n"

				      "vsra.vi	v22, v21, 7\n"
				      "vsll.vi	v23, v21, 1\n"
				      "vand.vx	v22, v22, %[x1d]\n"
				      "vxor.vv	v23, v23, v22\n"
				      "vle8.v	v22, (%[wd5])\n"
				      "vxor.vv	v21, v23, v22\n"
				      "vxor.vv	v20, v20, v22\n"

				      "vsra.vi	v26, v25, 7\n"
				      "vsll.vi	v27, v25, 1\n"
				      "vand.vx	v26, v26, %[x1d]\n"
				      "vxor.vv	v27, v27, v26\n"
				      "vle8.v	v26, (%[wd6])\n"
				      "vxor.vv	v25, v27, v26\n"
				      "vxor.vv	v24, v24, v26\n"

				      "vsra.vi	v30, v29, 7\n"
				      "vsll.vi	v31, v29, 1\n"
				      "vand.vx	v30, v30, %[x1d]\n"
				      "vxor.vv	v31, v31, v30\n"
				      "vle8.v	v30, (%[wd7])\n"
				      "vxor.vv	v29, v31, v30\n"
				      "vxor.vv	v28, v28, v30\n"
				      ".option	pop\n"
				      : :
				      [wd0]"r"(&dptr[z][d + 0 * NSIZE]),
				      [wd1]"r"(&dptr[z][d + 1 * NSIZE]),
				      [wd2]"r"(&dptr[z][d + 2 * NSIZE]),
				      [wd3]"r"(&dptr[z][d + 3 * NSIZE]),
				      [wd4]"r"(&dptr[z][d + 4 * NSIZE]),
				      [wd5]"r"(&dptr[z][d + 5 * NSIZE]),
				      [wd6]"r"(&dptr[z][d + 6 * NSIZE]),
				      [wd7]"r"(&dptr[z][d + 7 * NSIZE]),
				      [x1d]"r"(0x1d)
			);
		}

		/*
		 * *(unative_t *)&p[d+NSIZE*$$] = wp$$;
		 * *(unative_t *)&q[d+NSIZE*$$] = wq$$;
		 */
		asm volatile (".option	push\n"
			      ".option	arch,+v\n"
			      "vse8.v	v0, (%[wp0])\n"
			      "vse8.v	v1, (%[wq0])\n"
			      "vse8.v	v4, (%[wp1])\n"
			      "vse8.v	v5, (%[wq1])\n"
			      "vse8.v	v8, (%[wp2])\n"
			      "vse8.v	v9, (%[wq2])\n"
			      "vse8.v	v12, (%[wp3])\n"
			      "vse8.v	v13, (%[wq3])\n"
			      "vse8.v	v16, (%[wp4])\n"
			      "vse8.v	v17, (%[wq4])\n"
			      "vse8.v	v20, (%[wp5])\n"
			      "vse8.v	v21, (%[wq5])\n"
			      "vse8.v	v24, (%[wp6])\n"
			      "vse8.v	v25, (%[wq6])\n"
			      "vse8.v	v28, (%[wp7])\n"
			      "vse8.v	v29, (%[wq7])\n"
			      ".option	pop\n"
			      : :
			      [wp0]"r"(&p[d + NSIZE * 0]),
			      [wq0]"r"(&q[d + NSIZE * 0]),
			      [wp1]"r"(&p[d + NSIZE * 1]),
			      [wq1]"r"(&q[d + NSIZE * 1]),
			      [wp2]"r"(&p[d + NSIZE * 2]),
			      [wq2]"r"(&q[d + NSIZE * 2]),
			      [wp3]"r"(&p[d + NSIZE * 3]),
			      [wq3]"r"(&q[d + NSIZE * 3]),
			      [wp4]"r"(&p[d + NSIZE * 4]),
			      [wq4]"r"(&q[d + NSIZE * 4]),
			      [wp5]"r"(&p[d + NSIZE * 5]),
			      [wq5]"r"(&q[d + NSIZE * 5]),
			      [wp6]"r"(&p[d + NSIZE * 6]),
			      [wq6]"r"(&q[d + NSIZE * 6]),
			      [wp7]"r"(&p[d + NSIZE * 7]),
			      [wq7]"r"(&q[d + NSIZE * 7])
		);
	}
}

static void raid6_rvv8_xor_syndrome_real(int disks, int start, int stop,
					 unsigned long bytes, void **ptrs)
{
	u8 **dptr = (u8 **)ptrs;
	u8 *p, *q;
	unsigned long vl, d;
	int z, z0;

	z0 = stop;		/* P/Q right side optimization */
	p = dptr[disks - 2];	/* XOR parity */
	q = dptr[disks - 1];	/* RS syndrome */

	asm volatile (".option	push\n"
		      ".option	arch,+v\n"
		      "vsetvli	%0, x0, e8, m1, ta, ma\n"
		      ".option	pop\n"
		      : "=&r" (vl)
	);

	/*
	 * v0:wp0, v1:wq0, v2:wd0/w20, v3:w10
	 * v4:wp1, v5:wq1, v6:wd1/w21, v7:w11
	 * v8:wp2, v9:wq2, v10:wd2/w22, v11:w12
	 * v12:wp3, v13:wq3, v14:wd3/w23, v15:w13
	 * v16:wp4, v17:wq4, v18:wd4/w24, v19:w14
	 * v20:wp5, v21:wq5, v22:wd5/w25, v23:w15
	 * v24:wp6, v25:wq6, v26:wd6/w26, v27:w16
	 * v28:wp7, v29:wq7, v30:wd7/w27, v31:w17
	 */
	for (d = 0; d < bytes; d += NSIZE * 8) {
		 /* wq$$ = wp$$ = *(unative_t *)&dptr[z0][d+$$*NSIZE]; */
		asm volatile (".option	push\n"
			      ".option	arch,+v\n"
			      "vle8.v	v0, (%[wp0])\n"
			      "vmv.v.v	v1, v0\n"
			      "vle8.v	v4, (%[wp1])\n"
			      "vmv.v.v	v5, v4\n"
			      "vle8.v	v8, (%[wp2])\n"
			      "vmv.v.v	v9, v8\n"
			      "vle8.v	v12, (%[wp3])\n"
			      "vmv.v.v	v13, v12\n"
			      "vle8.v	v16, (%[wp4])\n"
			      "vmv.v.v	v17, v16\n"
			      "vle8.v	v20, (%[wp5])\n"
			      "vmv.v.v	v21, v20\n"
			      "vle8.v	v24, (%[wp6])\n"
			      "vmv.v.v	v25, v24\n"
			      "vle8.v	v28, (%[wp7])\n"
			      "vmv.v.v	v29, v28\n"
			      ".option	pop\n"
			      : :
			      [wp0]"r"(&dptr[z0][d + 0 * NSIZE]),
			      [wp1]"r"(&dptr[z0][d + 1 * NSIZE]),
			      [wp2]"r"(&dptr[z0][d + 2 * NSIZE]),
			      [wp3]"r"(&dptr[z0][d + 3 * NSIZE]),
			      [wp4]"r"(&dptr[z0][d + 4 * NSIZE]),
			      [wp5]"r"(&dptr[z0][d + 5 * NSIZE]),
			      [wp6]"r"(&dptr[z0][d + 6 * NSIZE]),
			      [wp7]"r"(&dptr[z0][d + 7 * NSIZE])
		);

		/* P/Q data pages */
		for (z = z0 - 1; z >= start; z--) {
			/*
			 * w2$$ = MASK(wq$$);
			 * w1$$ = SHLBYTE(wq$$);
			 * w2$$ &= NBYTES(0x1d);
			 * w1$$ ^= w2$$;
			 * wd$$ = *(unative_t *)&dptr[z][d+$$*NSIZE];
			 * wq$$ = w1$$ ^ wd$$;
			 * wp$$ ^= wd$$;
			 */
			asm volatile (".option	push\n"
				      ".option	arch,+v\n"
				      "vsra.vi	v2, v1, 7\n"
				      "vsll.vi	v3, v1, 1\n"
				      "vand.vx	v2, v2, %[x1d]\n"
				      "vxor.vv	v3, v3, v2\n"
				      "vle8.v	v2, (%[wd0])\n"
				      "vxor.vv	v1, v3, v2\n"
				      "vxor.vv	v0, v0, v2\n"

				      "vsra.vi	v6, v5, 7\n"
				      "vsll.vi	v7, v5, 1\n"
				      "vand.vx	v6, v6, %[x1d]\n"
				      "vxor.vv	v7, v7, v6\n"
				      "vle8.v	v6, (%[wd1])\n"
				      "vxor.vv	v5, v7, v6\n"
				      "vxor.vv	v4, v4, v6\n"

				      "vsra.vi	v10, v9, 7\n"
				      "vsll.vi	v11, v9, 1\n"
				      "vand.vx	v10, v10, %[x1d]\n"
				      "vxor.vv	v11, v11, v10\n"
				      "vle8.v	v10, (%[wd2])\n"
				      "vxor.vv	v9, v11, v10\n"
				      "vxor.vv	v8, v8, v10\n"

				      "vsra.vi	v14, v13, 7\n"
				      "vsll.vi	v15, v13, 1\n"
				      "vand.vx	v14, v14, %[x1d]\n"
				      "vxor.vv	v15, v15, v14\n"
				      "vle8.v	v14, (%[wd3])\n"
				      "vxor.vv	v13, v15, v14\n"
				      "vxor.vv	v12, v12, v14\n"

				      "vsra.vi	v18, v17, 7\n"
				      "vsll.vi	v19, v17, 1\n"
				      "vand.vx	v18, v18, %[x1d]\n"
				      "vxor.vv	v19, v19, v18\n"
				      "vle8.v	v18, (%[wd4])\n"
				      "vxor.vv	v17, v19, v18\n"
				      "vxor.vv	v16, v16, v18\n"

				      "vsra.vi	v22, v21, 7\n"
				      "vsll.vi	v23, v21, 1\n"
				      "vand.vx	v22, v22, %[x1d]\n"
				      "vxor.vv	v23, v23, v22\n"
				      "vle8.v	v22, (%[wd5])\n"
				      "vxor.vv	v21, v23, v22\n"
				      "vxor.vv	v20, v20, v22\n"

				      "vsra.vi	v26, v25, 7\n"
				      "vsll.vi	v27, v25, 1\n"
				      "vand.vx	v26, v26, %[x1d]\n"
				      "vxor.vv	v27, v27, v26\n"
				      "vle8.v	v26, (%[wd6])\n"
				      "vxor.vv	v25, v27, v26\n"
				      "vxor.vv	v24, v24, v26\n"

				      "vsra.vi	v30, v29, 7\n"
				      "vsll.vi	v31, v29, 1\n"
				      "vand.vx	v30, v30, %[x1d]\n"
				      "vxor.vv	v31, v31, v30\n"
				      "vle8.v	v30, (%[wd7])\n"
				      "vxor.vv	v29, v31, v30\n"
				      "vxor.vv	v28, v28, v30\n"
				      ".option	pop\n"
				      : :
				      [wd0]"r"(&dptr[z][d + 0 * NSIZE]),
				      [wd1]"r"(&dptr[z][d + 1 * NSIZE]),
				      [wd2]"r"(&dptr[z][d + 2 * NSIZE]),
				      [wd3]"r"(&dptr[z][d + 3 * NSIZE]),
				      [wd4]"r"(&dptr[z][d + 4 * NSIZE]),
				      [wd5]"r"(&dptr[z][d + 5 * NSIZE]),
				      [wd6]"r"(&dptr[z][d + 6 * NSIZE]),
				      [wd7]"r"(&dptr[z][d + 7 * NSIZE]),
				      [x1d]"r"(0x1d)
			);
		}

		/* P/Q left side optimization */
		for (z = start - 1; z >= 0; z--) {
			/*
			 * w2$$ = MASK(wq$$);
			 * w1$$ = SHLBYTE(wq$$);
			 * w2$$ &= NBYTES(0x1d);
			 * wq$$ = w1$$ ^ w2$$;
			 */
			asm volatile (".option	push\n"
				      ".option	arch,+v\n"
				      "vsra.vi	v2, v1, 7\n"
				      "vsll.vi	v3, v1, 1\n"
				      "vand.vx	v2, v2, %[x1d]\n"
				      "vxor.vv	v1, v3, v2\n"

				      "vsra.vi	v6, v5, 7\n"
				      "vsll.vi	v7, v5, 1\n"
				      "vand.vx	v6, v6, %[x1d]\n"
				      "vxor.vv	v5, v7, v6\n"

				      "vsra.vi	v10, v9, 7\n"
				      "vsll.vi	v11, v9, 1\n"
				      "vand.vx	v10, v10, %[x1d]\n"
				      "vxor.vv	v9, v11, v10\n"

				      "vsra.vi	v14, v13, 7\n"
				      "vsll.vi	v15, v13, 1\n"
				      "vand.vx	v14, v14, %[x1d]\n"
				      "vxor.vv	v13, v15, v14\n"

				      "vsra.vi	v18, v17, 7\n"
				      "vsll.vi	v19, v17, 1\n"
				      "vand.vx	v18, v18, %[x1d]\n"
				      "vxor.vv	v17, v19, v18\n"

				      "vsra.vi	v22, v21, 7\n"
				      "vsll.vi	v23, v21, 1\n"
				      "vand.vx	v22, v22, %[x1d]\n"
				      "vxor.vv	v21, v23, v22\n"

				      "vsra.vi	v26, v25, 7\n"
				      "vsll.vi	v27, v25, 1\n"
				      "vand.vx	v26, v26, %[x1d]\n"
				      "vxor.vv	v25, v27, v26\n"

				      "vsra.vi	v30, v29, 7\n"
				      "vsll.vi	v31, v29, 1\n"
				      "vand.vx	v30, v30, %[x1d]\n"
				      "vxor.vv	v29, v31, v30\n"
				      ".option	pop\n"
				      : :
				      [x1d]"r"(0x1d)
			);
		}

		/*
		 * *(unative_t *)&p[d+NSIZE*$$] ^= wp$$;
		 * *(unative_t *)&q[d+NSIZE*$$] ^= wq$$;
		 * v0:wp0, v1:wq0, v2:p0, v3:q0
		 * v4:wp1, v5:wq1, v6:p1, v7:q1
		 * v8:wp2, v9:wq2, v10:p2, v11:q2
		 * v12:wp3, v13:wq3, v14:p3, v15:q3
		 * v16:wp4, v17:wq4, v18:p4, v19:q4
		 * v20:wp5, v21:wq5, v22:p5, v23:q5
		 * v24:wp6, v25:wq6, v26:p6, v27:q6
		 * v28:wp7, v29:wq7, v30:p7, v31:q7
		 */
		asm volatile (".option	push\n"
			      ".option	arch,+v\n"
			      "vle8.v	v2, (%[wp0])\n"
			      "vle8.v	v3, (%[wq0])\n"
			      "vxor.vv	v2, v2, v0\n"
			      "vxor.vv	v3, v3, v1\n"
			      "vse8.v	v2, (%[wp0])\n"
			      "vse8.v	v3, (%[wq0])\n"

			      "vle8.v	v6, (%[wp1])\n"
			      "vle8.v	v7, (%[wq1])\n"
			      "vxor.vv	v6, v6, v4\n"
			      "vxor.vv	v7, v7, v5\n"
			      "vse8.v	v6, (%[wp1])\n"
			      "vse8.v	v7, (%[wq1])\n"

			      "vle8.v	v10, (%[wp2])\n"
			      "vle8.v	v11, (%[wq2])\n"
			      "vxor.vv	v10, v10, v8\n"
			      "vxor.vv	v11, v11, v9\n"
			      "vse8.v	v10, (%[wp2])\n"
			      "vse8.v	v11, (%[wq2])\n"

			      "vle8.v	v14, (%[wp3])\n"
			      "vle8.v	v15, (%[wq3])\n"
			      "vxor.vv	v14, v14, v12\n"
			      "vxor.vv	v15, v15, v13\n"
			      "vse8.v	v14, (%[wp3])\n"
			      "vse8.v	v15, (%[wq3])\n"

			      "vle8.v	v18, (%[wp4])\n"
			      "vle8.v	v19, (%[wq4])\n"
			      "vxor.vv	v18, v18, v16\n"
			      "vxor.vv	v19, v19, v17\n"
			      "vse8.v	v18, (%[wp4])\n"
			      "vse8.v	v19, (%[wq4])\n"

			      "vle8.v	v22, (%[wp5])\n"
			      "vle8.v	v23, (%[wq5])\n"
			      "vxor.vv	v22, v22, v20\n"
			      "vxor.vv	v23, v23, v21\n"
			      "vse8.v	v22, (%[wp5])\n"
			      "vse8.v	v23, (%[wq5])\n"

			      "vle8.v	v26, (%[wp6])\n"
			      "vle8.v	v27, (%[wq6])\n"
			      "vxor.vv	v26, v26, v24\n"
			      "vxor.vv	v27, v27, v25\n"
			      "vse8.v	v26, (%[wp6])\n"
			      "vse8.v	v27, (%[wq6])\n"

			      "vle8.v	v30, (%[wp7])\n"
			      "vle8.v	v31, (%[wq7])\n"
			      "vxor.vv	v30, v30, v28\n"
			      "vxor.vv	v31, v31, v29\n"
			      "vse8.v	v30, (%[wp7])\n"
			      "vse8.v	v31, (%[wq7])\n"
			      ".option	pop\n"
			      : :
			      [wp0]"r"(&p[d + NSIZE * 0]),
			      [wq0]"r"(&q[d + NSIZE * 0]),
			      [wp1]"r"(&p[d + NSIZE * 1]),
			      [wq1]"r"(&q[d + NSIZE * 1]),
			      [wp2]"r"(&p[d + NSIZE * 2]),
			      [wq2]"r"(&q[d + NSIZE * 2]),
			      [wp3]"r"(&p[d + NSIZE * 3]),
			      [wq3]"r"(&q[d + NSIZE * 3]),
			      [wp4]"r"(&p[d + NSIZE * 4]),
			      [wq4]"r"(&q[d + NSIZE * 4]),
			      [wp5]"r"(&p[d + NSIZE * 5]),
			      [wq5]"r"(&q[d + NSIZE * 5]),
			      [wp6]"r"(&p[d + NSIZE * 6]),
			      [wq6]"r"(&q[d + NSIZE * 6]),
			      [wp7]"r"(&p[d + NSIZE * 7]),
			      [wq7]"r"(&q[d + NSIZE * 7])
		);
	}
}

RAID6_RVV_WRAPPER(1);
RAID6_RVV_WRAPPER(2);
RAID6_RVV_WRAPPER(4);
RAID6_RVV_WRAPPER(8);
