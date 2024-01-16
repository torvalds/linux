/* -*- linux-c -*- ------------------------------------------------------- *
 *
 *   Copyright 2002-2004 H. Peter Anvin - All Rights Reserved
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, Inc., 53 Temple Place Ste 330,
 *   Boston MA 02111-1307, USA; either version 2 of the License, or
 *   (at your option) any later version; incorporated herein by reference.
 *
 * ----------------------------------------------------------------------- */

/*
 * int$#.c
 *
 * $#-way unrolled portable integer math RAID-6 instruction set
 *
 * This file is postprocessed using unroll.awk
 */

#include <linux/raid/pq.h>

/*
 * This is the C data type to use
 */

/* Change this from BITS_PER_LONG if there is something better... */
#if BITS_PER_LONG == 64
# define NBYTES(x) ((x) * 0x0101010101010101UL)
# define NSIZE  8
# define NSHIFT 3
# define NSTRING "64"
typedef u64 unative_t;
#else
# define NBYTES(x) ((x) * 0x01010101U)
# define NSIZE  4
# define NSHIFT 2
# define NSTRING "32"
typedef u32 unative_t;
#endif



/*
 * IA-64 wants insane amounts of unrolling.  On other architectures that
 * is just a waste of space.
 */
#if ($# <= 8) || defined(__ia64__)


/*
 * These sub-operations are separate inlines since they can sometimes be
 * specially optimized using architecture-specific hacks.
 */

/*
 * The SHLBYTE() operation shifts each byte left by 1, *not*
 * rolling over into the next byte
 */
static inline __attribute_const__ unative_t SHLBYTE(unative_t v)
{
	unative_t vv;

	vv = (v << 1) & NBYTES(0xfe);
	return vv;
}

/*
 * The MASK() operation returns 0xFF in any byte for which the high
 * bit is 1, 0x00 for any byte for which the high bit is 0.
 */
static inline __attribute_const__ unative_t MASK(unative_t v)
{
	unative_t vv;

	vv = v & NBYTES(0x80);
	vv = (vv << 1) - (vv >> 7); /* Overflow on the top bit is OK */
	return vv;
}


static void raid6_int$#_gen_syndrome(int disks, size_t bytes, void **ptrs)
{
	u8 **dptr = (u8 **)ptrs;
	u8 *p, *q;
	int d, z, z0;

	unative_t wd$$, wq$$, wp$$, w1$$, w2$$;

	z0 = disks - 3;		/* Highest data disk */
	p = dptr[z0+1];		/* XOR parity */
	q = dptr[z0+2];		/* RS syndrome */

	for ( d = 0 ; d < bytes ; d += NSIZE*$# ) {
		wq$$ = wp$$ = *(unative_t *)&dptr[z0][d+$$*NSIZE];
		for ( z = z0-1 ; z >= 0 ; z-- ) {
			wd$$ = *(unative_t *)&dptr[z][d+$$*NSIZE];
			wp$$ ^= wd$$;
			w2$$ = MASK(wq$$);
			w1$$ = SHLBYTE(wq$$);
			w2$$ &= NBYTES(0x1d);
			w1$$ ^= w2$$;
			wq$$ = w1$$ ^ wd$$;
		}
		*(unative_t *)&p[d+NSIZE*$$] = wp$$;
		*(unative_t *)&q[d+NSIZE*$$] = wq$$;
	}
}

static void raid6_int$#_xor_syndrome(int disks, int start, int stop,
				     size_t bytes, void **ptrs)
{
	u8 **dptr = (u8 **)ptrs;
	u8 *p, *q;
	int d, z, z0;

	unative_t wd$$, wq$$, wp$$, w1$$, w2$$;

	z0 = stop;		/* P/Q right side optimization */
	p = dptr[disks-2];	/* XOR parity */
	q = dptr[disks-1];	/* RS syndrome */

	for ( d = 0 ; d < bytes ; d += NSIZE*$# ) {
		/* P/Q data pages */
		wq$$ = wp$$ = *(unative_t *)&dptr[z0][d+$$*NSIZE];
		for ( z = z0-1 ; z >= start ; z-- ) {
			wd$$ = *(unative_t *)&dptr[z][d+$$*NSIZE];
			wp$$ ^= wd$$;
			w2$$ = MASK(wq$$);
			w1$$ = SHLBYTE(wq$$);
			w2$$ &= NBYTES(0x1d);
			w1$$ ^= w2$$;
			wq$$ = w1$$ ^ wd$$;
		}
		/* P/Q left side optimization */
		for ( z = start-1 ; z >= 0 ; z-- ) {
			w2$$ = MASK(wq$$);
			w1$$ = SHLBYTE(wq$$);
			w2$$ &= NBYTES(0x1d);
			wq$$ = w1$$ ^ w2$$;
		}
		*(unative_t *)&p[d+NSIZE*$$] ^= wp$$;
		*(unative_t *)&q[d+NSIZE*$$] ^= wq$$;
	}

}

const struct raid6_calls raid6_intx$# = {
	raid6_int$#_gen_syndrome,
	raid6_int$#_xor_syndrome,
	NULL,			/* always valid */
	"int" NSTRING "x$#",
	0
};

#endif
