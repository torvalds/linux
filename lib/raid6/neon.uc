/* -----------------------------------------------------------------------
 *
 *   neon.uc - RAID-6 syndrome calculation using ARM NEON instructions
 *
 *   Copyright (C) 2012 Rob Herring
 *   Copyright (C) 2015 Linaro Ltd. <ard.biesheuvel@linaro.org>
 *
 *   Based on altivec.uc:
 *     Copyright 2002-2004 H. Peter Anvin - All Rights Reserved
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, Inc., 53 Temple Place Ste 330,
 *   Boston MA 02111-1307, USA; either version 2 of the License, or
 *   (at your option) any later version; incorporated herein by reference.
 *
 * ----------------------------------------------------------------------- */

/*
 * neon$#.c
 *
 * $#-way unrolled NEON intrinsics math RAID-6 instruction set
 *
 * This file is postprocessed using unroll.awk
 */

#include <arm_neon.h>

typedef uint8x16_t unative_t;

#define NBYTES(x) ((unative_t){x,x,x,x, x,x,x,x, x,x,x,x, x,x,x,x})
#define NSIZE	sizeof(unative_t)

/*
 * The SHLBYTE() operation shifts each byte left by 1, *not*
 * rolling over into the next byte
 */
static inline unative_t SHLBYTE(unative_t v)
{
	return vshlq_n_u8(v, 1);
}

/*
 * The MASK() operation returns 0xFF in any byte for which the high
 * bit is 1, 0x00 for any byte for which the high bit is 0.
 */
static inline unative_t MASK(unative_t v)
{
	return (unative_t)vshrq_n_s8((int8x16_t)v, 7);
}

static inline unative_t PMUL(unative_t v, unative_t u)
{
	return (unative_t)vmulq_p8((poly8x16_t)v, (poly8x16_t)u);
}

void raid6_neon$#_gen_syndrome_real(int disks, unsigned long bytes, void **ptrs)
{
	uint8_t **dptr = (uint8_t **)ptrs;
	uint8_t *p, *q;
	int d, z, z0;

	register unative_t wd$$, wq$$, wp$$, w1$$, w2$$;
	const unative_t x1d = NBYTES(0x1d);

	z0 = disks - 3;		/* Highest data disk */
	p = dptr[z0+1];		/* XOR parity */
	q = dptr[z0+2];		/* RS syndrome */

	for ( d = 0 ; d < bytes ; d += NSIZE*$# ) {
		wq$$ = wp$$ = vld1q_u8(&dptr[z0][d+$$*NSIZE]);
		for ( z = z0-1 ; z >= 0 ; z-- ) {
			wd$$ = vld1q_u8(&dptr[z][d+$$*NSIZE]);
			wp$$ = veorq_u8(wp$$, wd$$);
			w2$$ = MASK(wq$$);
			w1$$ = SHLBYTE(wq$$);

			w2$$ = vandq_u8(w2$$, x1d);
			w1$$ = veorq_u8(w1$$, w2$$);
			wq$$ = veorq_u8(w1$$, wd$$);
		}
		vst1q_u8(&p[d+NSIZE*$$], wp$$);
		vst1q_u8(&q[d+NSIZE*$$], wq$$);
	}
}

void raid6_neon$#_xor_syndrome_real(int disks, int start, int stop,
				    unsigned long bytes, void **ptrs)
{
	uint8_t **dptr = (uint8_t **)ptrs;
	uint8_t *p, *q;
	int d, z, z0;

	register unative_t wd$$, wq$$, wp$$, w1$$, w2$$;
	const unative_t x1d = NBYTES(0x1d);

	z0 = stop;		/* P/Q right side optimization */
	p = dptr[disks-2];	/* XOR parity */
	q = dptr[disks-1];	/* RS syndrome */

	for ( d = 0 ; d < bytes ; d += NSIZE*$# ) {
		wq$$ = vld1q_u8(&dptr[z0][d+$$*NSIZE]);
		wp$$ = veorq_u8(vld1q_u8(&p[d+$$*NSIZE]), wq$$);

		/* P/Q data pages */
		for ( z = z0-1 ; z >= start ; z-- ) {
			wd$$ = vld1q_u8(&dptr[z][d+$$*NSIZE]);
			wp$$ = veorq_u8(wp$$, wd$$);
			w2$$ = MASK(wq$$);
			w1$$ = SHLBYTE(wq$$);

			w2$$ = vandq_u8(w2$$, x1d);
			w1$$ = veorq_u8(w1$$, w2$$);
			wq$$ = veorq_u8(w1$$, wd$$);
		}
		/* P/Q left side optimization */
		for ( z = start-1 ; z >= 3 ; z -= 4 ) {
			w2$$ = vshrq_n_u8(wq$$, 4);
			w1$$ = vshlq_n_u8(wq$$, 4);

			w2$$ = PMUL(w2$$, x1d);
			wq$$ = veorq_u8(w1$$, w2$$);
		}

		switch (z) {
		case 2:
			w2$$ = vshrq_n_u8(wq$$, 5);
			w1$$ = vshlq_n_u8(wq$$, 3);

			w2$$ = PMUL(w2$$, x1d);
			wq$$ = veorq_u8(w1$$, w2$$);
			break;
		case 1:
			w2$$ = vshrq_n_u8(wq$$, 6);
			w1$$ = vshlq_n_u8(wq$$, 2);

			w2$$ = PMUL(w2$$, x1d);
			wq$$ = veorq_u8(w1$$, w2$$);
			break;
		case 0:
			w2$$ = MASK(wq$$);
			w1$$ = SHLBYTE(wq$$);

			w2$$ = vandq_u8(w2$$, x1d);
			wq$$ = veorq_u8(w1$$, w2$$);
		}
		w1$$ = vld1q_u8(&q[d+NSIZE*$$]);
		wq$$ = veorq_u8(wq$$, w1$$);

		vst1q_u8(&p[d+NSIZE*$$], wp$$);
		vst1q_u8(&q[d+NSIZE*$$], wq$$);
	}
}
