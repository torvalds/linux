// SPDX-License-Identifier: GPL-2.0
/*
 * raid6_vx$#.c
 *
 * $#-way unrolled RAID6 gen/xor functions for s390
 * based on the vector facility
 *
 * Copyright IBM Corp. 2016
 * Author(s): Martin Schwidefsky <schwidefsky@de.ibm.com>
 *
 * This file is postprocessed using unroll.awk.
 */

#include <linux/cpufeature.h>
#include <linux/raid/pq.h>
#include <asm/fpu.h>

#define NSIZE 16

static __always_inline void LOAD_CONST(void)
{
	fpu_vrepib(24, 0x07);
	fpu_vrepib(25, 0x1d);
}

/*
 * The SHLBYTE() operation shifts each of the 16 bytes in
 * vector register y left by 1 bit and stores the result in
 * vector register x.
 */
#define SHLBYTE(x, y)		fpu_vab(x, y, y)

/*
 * For each of the 16 bytes in the vector register y the MASK()
 * operation returns 0xFF if the high bit of the byte is 1,
 * or 0x00 if the high bit is 0. The result is stored in vector
 * register x.
 */
#define MASK(x, y)		fpu_vesravb(x, y, 24)

#define AND(x, y, z)		fpu_vn(x, y, z)
#define XOR(x, y, z)		fpu_vx(x, y, z)
#define LOAD_DATA(x, ptr)	fpu_vlm(x, x + $# - 1, ptr)
#define STORE_DATA(x, ptr)	fpu_vstm(x, x + $# - 1, ptr)
#define COPY_VEC(x, y)		fpu_vlr(x, y)

static void raid6_s390vx$#_gen_syndrome(int disks, size_t bytes, void **ptrs)
{
	DECLARE_KERNEL_FPU_ONSTACK32(vxstate);
	u8 **dptr, *p, *q;
	int d, z, z0;

	kernel_fpu_begin(&vxstate, KERNEL_VXR);
	LOAD_CONST();

	dptr = (u8 **) ptrs;
	z0 = disks - 3;		/* Highest data disk */
	p = dptr[z0 + 1];	/* XOR parity */
	q = dptr[z0 + 2];	/* RS syndrome */

	for (d = 0; d < bytes; d += $#*NSIZE) {
		LOAD_DATA(0,&dptr[z0][d]);
		COPY_VEC(8+$$,0+$$);
		for (z = z0 - 1; z >= 0; z--) {
			MASK(16+$$,8+$$);
			AND(16+$$,16+$$,25);
			SHLBYTE(8+$$,8+$$);
			XOR(8+$$,8+$$,16+$$);
			LOAD_DATA(16,&dptr[z][d]);
			XOR(0+$$,0+$$,16+$$);
			XOR(8+$$,8+$$,16+$$);
		}
		STORE_DATA(0,&p[d]);
		STORE_DATA(8,&q[d]);
	}
	kernel_fpu_end(&vxstate, KERNEL_VXR);
}

static void raid6_s390vx$#_xor_syndrome(int disks, int start, int stop,
					size_t bytes, void **ptrs)
{
	DECLARE_KERNEL_FPU_ONSTACK32(vxstate);
	u8 **dptr, *p, *q;
	int d, z, z0;

	dptr = (u8 **) ptrs;
	z0 = stop;		/* P/Q right side optimization */
	p = dptr[disks - 2];	/* XOR parity */
	q = dptr[disks - 1];	/* RS syndrome */

	kernel_fpu_begin(&vxstate, KERNEL_VXR);
	LOAD_CONST();

	for (d = 0; d < bytes; d += $#*NSIZE) {
		/* P/Q data pages */
		LOAD_DATA(0,&dptr[z0][d]);
		COPY_VEC(8+$$,0+$$);
		for (z = z0 - 1; z >= start; z--) {
			MASK(16+$$,8+$$);
			AND(16+$$,16+$$,25);
			SHLBYTE(8+$$,8+$$);
			XOR(8+$$,8+$$,16+$$);
			LOAD_DATA(16,&dptr[z][d]);
			XOR(0+$$,0+$$,16+$$);
			XOR(8+$$,8+$$,16+$$);
		}
		/* P/Q left side optimization */
		for (z = start - 1; z >= 0; z--) {
			MASK(16+$$,8+$$);
			AND(16+$$,16+$$,25);
			SHLBYTE(8+$$,8+$$);
			XOR(8+$$,8+$$,16+$$);
		}
		LOAD_DATA(16,&p[d]);
		XOR(16+$$,16+$$,0+$$);
		STORE_DATA(16,&p[d]);
		LOAD_DATA(16,&q[d]);
		XOR(16+$$,16+$$,8+$$);
		STORE_DATA(16,&q[d]);
	}
	kernel_fpu_end(&vxstate, KERNEL_VXR);
}

static int raid6_s390vx$#_valid(void)
{
	return cpu_has_vx();
}

const struct raid6_calls raid6_s390vx$# = {
	raid6_s390vx$#_gen_syndrome,
	raid6_s390vx$#_xor_syndrome,
	raid6_s390vx$#_valid,
	"vx128x$#",
	1
};
