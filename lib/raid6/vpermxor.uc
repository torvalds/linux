/*
 * Copyright 2017, Matt Brown, IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * vpermxor$#.c
 *
 * Based on H. Peter Anvin's paper - The mathematics of RAID-6
 *
 * $#-way unrolled portable integer math RAID-6 instruction set
 * This file is postprocessed using unroll.awk
 *
 * vpermxor$#.c makes use of the vpermxor instruction to optimise the RAID6 Q
 * syndrome calculations.
 * This can be run on systems which have both Altivec and vpermxor instruction.
 *
 * This instruction was introduced in POWER8 - ISA v2.07.
 */

#include <linux/raid/pq.h>
#ifdef CONFIG_ALTIVEC

#include <altivec.h>
#include <asm/ppc-opcode.h>
#ifdef __KERNEL__
#include <asm/cputable.h>
#include <asm/switch_to.h>
#endif

typedef vector unsigned char unative_t;
#define NSIZE sizeof(unative_t)

static const vector unsigned char gf_low = {0x1e, 0x1c, 0x1a, 0x18, 0x16, 0x14,
					    0x12, 0x10, 0x0e, 0x0c, 0x0a, 0x08,
					    0x06, 0x04, 0x02,0x00};
static const vector unsigned char gf_high = {0xfd, 0xdd, 0xbd, 0x9d, 0x7d, 0x5d,
					     0x3d, 0x1d, 0xe0, 0xc0, 0xa0, 0x80,
					     0x60, 0x40, 0x20, 0x00};

static void noinline raid6_vpermxor$#_gen_syndrome_real(int disks, size_t bytes,
							void **ptrs)
{
	u8 **dptr = (u8 **)ptrs;
	u8 *p, *q;
	int d, z, z0;
	unative_t wp$$, wq$$, wd$$;

	z0 = disks - 3;		/* Highest data disk */
	p = dptr[z0+1];		/* XOR parity */
	q = dptr[z0+2];		/* RS syndrome */

	for (d = 0; d < bytes; d += NSIZE*$#) {
		wp$$ = wq$$ = *(unative_t *)&dptr[z0][d+$$*NSIZE];

		for (z = z0-1; z>=0; z--) {
			wd$$ = *(unative_t *)&dptr[z][d+$$*NSIZE];
			/* P syndrome */
			wp$$ = vec_xor(wp$$, wd$$);

			/* Q syndrome */
			asm(VPERMXOR(%0,%1,%2,%3):"=v"(wq$$):"v"(gf_high), "v"(gf_low), "v"(wq$$));
			wq$$ = vec_xor(wq$$, wd$$);
		}
		*(unative_t *)&p[d+NSIZE*$$] = wp$$;
		*(unative_t *)&q[d+NSIZE*$$] = wq$$;
	}
}

static void raid6_vpermxor$#_gen_syndrome(int disks, size_t bytes, void **ptrs)
{
	preempt_disable();
	enable_kernel_altivec();

	raid6_vpermxor$#_gen_syndrome_real(disks, bytes, ptrs);

	disable_kernel_altivec();
	preempt_enable();
}

int raid6_have_altivec_vpermxor(void);
#if $# == 1
int raid6_have_altivec_vpermxor(void)
{
	/* Check if arch has both altivec and the vpermxor instructions */
# ifdef __KERNEL__
	return (cpu_has_feature(CPU_FTR_ALTIVEC_COMP) &&
		cpu_has_feature(CPU_FTR_ARCH_207S));
# else
	return 1;
#endif

}
#endif

const struct raid6_calls raid6_vpermxor$# = {
	raid6_vpermxor$#_gen_syndrome,
	NULL,
	raid6_have_altivec_vpermxor,
	"vpermxor$#",
	0
};
#endif
