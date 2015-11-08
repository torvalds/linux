/* -*- linux-c -*- ------------------------------------------------------- *
 *
 *   Copyright 2002 H. Peter Anvin - All Rights Reserved
 *   Copyright 2012 Tilera Corporation - All Rights Reserved
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, Inc., 53 Temple Place Ste 330,
 *   Boston MA 02111-1307, USA; either version 2 of the License, or
 *   (at your option) any later version; incorporated herein by reference.
 *
 * ----------------------------------------------------------------------- */

/*
 * tilegx$#.c
 *
 * $#-way unrolled TILE-Gx SIMD for RAID-6 math.
 *
 * This file is postprocessed using unroll.awk.
 *
 */

#include <linux/raid/pq.h>

/* Create 8 byte copies of constant byte */
# define NBYTES(x) (__insn_v1addi(0, x))
# define NSIZE  8

/*
 * The SHLBYTE() operation shifts each byte left by 1, *not*
 * rolling over into the next byte
 */
static inline __attribute_const__ u64 SHLBYTE(u64 v)
{
	/* Vector One Byte Shift Left Immediate. */
	return __insn_v1shli(v, 1);
}

/*
 * The MASK() operation returns 0xFF in any byte for which the high
 * bit is 1, 0x00 for any byte for which the high bit is 0.
 */
static inline __attribute_const__ u64 MASK(u64 v)
{
	/* Vector One Byte Shift Right Signed Immediate. */
	return __insn_v1shrsi(v, 7);
}


void raid6_tilegx$#_gen_syndrome(int disks, size_t bytes, void **ptrs)
{
	u8 **dptr = (u8 **)ptrs;
	u64 *p, *q;
	int d, z, z0;

	u64 wd$$, wq$$, wp$$, w1$$, w2$$;
	u64 x1d = NBYTES(0x1d);
	u64 * z0ptr;

	z0 = disks - 3;			/* Highest data disk */
	p = (u64 *)dptr[z0+1];	/* XOR parity */
	q = (u64 *)dptr[z0+2];	/* RS syndrome */

	z0ptr = (u64 *)&dptr[z0][0];
	for ( d = 0 ; d < bytes ; d += NSIZE*$# ) {
		wq$$ = wp$$ = *z0ptr++;
		for ( z = z0-1 ; z >= 0 ; z-- ) {
			wd$$ = *(u64 *)&dptr[z][d+$$*NSIZE];
			wp$$ = wp$$ ^ wd$$;
			w2$$ = MASK(wq$$);
			w1$$ = SHLBYTE(wq$$);
			w2$$ = w2$$ & x1d;
			w1$$ = w1$$ ^ w2$$;
			wq$$ = w1$$ ^ wd$$;
		}
		*p++ = wp$$;
		*q++ = wq$$;
	}
}

const struct raid6_calls raid6_tilegx$# = {
	raid6_tilegx$#_gen_syndrome,
	NULL,			/* XOR not yet implemented */
	NULL,
	"tilegx$#",
	0
};
