// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * RAID6 syndrome calculations in LoongArch SIMD (LSX & LASX)
 *
 * Copyright 2023 WANG Xuerui <git@xen0n.name>
 *
 * Based on the generic RAID-6 code (int.uc):
 *
 * Copyright 2002-2004 H. Peter Anvin
 */

#include <linux/raid/pq.h>
#include "loongarch.h"

/*
 * The vector algorithms are currently priority 0, which means the generic
 * scalar algorithms are not being disabled if vector support is present.
 * This is like the similar LoongArch RAID5 XOR code, with the main reason
 * repeated here: it cannot be ruled out at this point of time, that some
 * future (maybe reduced) models could run the vector algorithms slower than
 * the scalar ones, maybe for errata or micro-op reasons. It may be
 * appropriate to revisit this after one or two more uarch generations.
 */

#ifdef CONFIG_CPU_HAS_LSX
#define NSIZE 16

static int raid6_has_lsx(void)
{
	return cpu_has_lsx;
}

static void raid6_lsx_gen_syndrome(int disks, size_t bytes, void **ptrs)
{
	u8 **dptr = (u8 **)ptrs;
	u8 *p, *q;
	int d, z, z0;

	z0 = disks - 3;		/* Highest data disk */
	p = dptr[z0+1];		/* XOR parity */
	q = dptr[z0+2];		/* RS syndrome */

	kernel_fpu_begin();

	/*
	 * $vr0, $vr1, $vr2, $vr3: wp
	 * $vr4, $vr5, $vr6, $vr7: wq
	 * $vr8, $vr9, $vr10, $vr11: wd
	 * $vr12, $vr13, $vr14, $vr15: w2
	 * $vr16, $vr17, $vr18, $vr19: w1
	 */
	for (d = 0; d < bytes; d += NSIZE*4) {
		/* wq$$ = wp$$ = *(unative_t *)&dptr[z0][d+$$*NSIZE]; */
		asm volatile("vld $vr0, %0" : : "m"(dptr[z0][d+0*NSIZE]));
		asm volatile("vld $vr1, %0" : : "m"(dptr[z0][d+1*NSIZE]));
		asm volatile("vld $vr2, %0" : : "m"(dptr[z0][d+2*NSIZE]));
		asm volatile("vld $vr3, %0" : : "m"(dptr[z0][d+3*NSIZE]));
		asm volatile("vori.b $vr4, $vr0, 0");
		asm volatile("vori.b $vr5, $vr1, 0");
		asm volatile("vori.b $vr6, $vr2, 0");
		asm volatile("vori.b $vr7, $vr3, 0");
		for (z = z0-1; z >= 0; z--) {
			/* wd$$ = *(unative_t *)&dptr[z][d+$$*NSIZE]; */
			asm volatile("vld $vr8, %0" : : "m"(dptr[z][d+0*NSIZE]));
			asm volatile("vld $vr9, %0" : : "m"(dptr[z][d+1*NSIZE]));
			asm volatile("vld $vr10, %0" : : "m"(dptr[z][d+2*NSIZE]));
			asm volatile("vld $vr11, %0" : : "m"(dptr[z][d+3*NSIZE]));
			/* wp$$ ^= wd$$; */
			asm volatile("vxor.v $vr0, $vr0, $vr8");
			asm volatile("vxor.v $vr1, $vr1, $vr9");
			asm volatile("vxor.v $vr2, $vr2, $vr10");
			asm volatile("vxor.v $vr3, $vr3, $vr11");
			/* w2$$ = MASK(wq$$); */
			asm volatile("vslti.b $vr12, $vr4, 0");
			asm volatile("vslti.b $vr13, $vr5, 0");
			asm volatile("vslti.b $vr14, $vr6, 0");
			asm volatile("vslti.b $vr15, $vr7, 0");
			/* w1$$ = SHLBYTE(wq$$); */
			asm volatile("vslli.b $vr16, $vr4, 1");
			asm volatile("vslli.b $vr17, $vr5, 1");
			asm volatile("vslli.b $vr18, $vr6, 1");
			asm volatile("vslli.b $vr19, $vr7, 1");
			/* w2$$ &= NBYTES(0x1d); */
			asm volatile("vandi.b $vr12, $vr12, 0x1d");
			asm volatile("vandi.b $vr13, $vr13, 0x1d");
			asm volatile("vandi.b $vr14, $vr14, 0x1d");
			asm volatile("vandi.b $vr15, $vr15, 0x1d");
			/* w1$$ ^= w2$$; */
			asm volatile("vxor.v $vr16, $vr16, $vr12");
			asm volatile("vxor.v $vr17, $vr17, $vr13");
			asm volatile("vxor.v $vr18, $vr18, $vr14");
			asm volatile("vxor.v $vr19, $vr19, $vr15");
			/* wq$$ = w1$$ ^ wd$$; */
			asm volatile("vxor.v $vr4, $vr16, $vr8");
			asm volatile("vxor.v $vr5, $vr17, $vr9");
			asm volatile("vxor.v $vr6, $vr18, $vr10");
			asm volatile("vxor.v $vr7, $vr19, $vr11");
		}
		/* *(unative_t *)&p[d+NSIZE*$$] = wp$$; */
		asm volatile("vst $vr0, %0" : "=m"(p[d+NSIZE*0]));
		asm volatile("vst $vr1, %0" : "=m"(p[d+NSIZE*1]));
		asm volatile("vst $vr2, %0" : "=m"(p[d+NSIZE*2]));
		asm volatile("vst $vr3, %0" : "=m"(p[d+NSIZE*3]));
		/* *(unative_t *)&q[d+NSIZE*$$] = wq$$; */
		asm volatile("vst $vr4, %0" : "=m"(q[d+NSIZE*0]));
		asm volatile("vst $vr5, %0" : "=m"(q[d+NSIZE*1]));
		asm volatile("vst $vr6, %0" : "=m"(q[d+NSIZE*2]));
		asm volatile("vst $vr7, %0" : "=m"(q[d+NSIZE*3]));
	}

	kernel_fpu_end();
}

static void raid6_lsx_xor_syndrome(int disks, int start, int stop,
				   size_t bytes, void **ptrs)
{
	u8 **dptr = (u8 **)ptrs;
	u8 *p, *q;
	int d, z, z0;

	z0 = stop;		/* P/Q right side optimization */
	p = dptr[disks-2];	/* XOR parity */
	q = dptr[disks-1];	/* RS syndrome */

	kernel_fpu_begin();

	/*
	 * $vr0, $vr1, $vr2, $vr3: wp
	 * $vr4, $vr5, $vr6, $vr7: wq
	 * $vr8, $vr9, $vr10, $vr11: wd
	 * $vr12, $vr13, $vr14, $vr15: w2
	 * $vr16, $vr17, $vr18, $vr19: w1
	 */
	for (d = 0; d < bytes; d += NSIZE*4) {
		/* P/Q data pages */
		/* wq$$ = wp$$ = *(unative_t *)&dptr[z0][d+$$*NSIZE]; */
		asm volatile("vld $vr0, %0" : : "m"(dptr[z0][d+0*NSIZE]));
		asm volatile("vld $vr1, %0" : : "m"(dptr[z0][d+1*NSIZE]));
		asm volatile("vld $vr2, %0" : : "m"(dptr[z0][d+2*NSIZE]));
		asm volatile("vld $vr3, %0" : : "m"(dptr[z0][d+3*NSIZE]));
		asm volatile("vori.b $vr4, $vr0, 0");
		asm volatile("vori.b $vr5, $vr1, 0");
		asm volatile("vori.b $vr6, $vr2, 0");
		asm volatile("vori.b $vr7, $vr3, 0");
		for (z = z0-1; z >= start; z--) {
			/* wd$$ = *(unative_t *)&dptr[z][d+$$*NSIZE]; */
			asm volatile("vld $vr8, %0" : : "m"(dptr[z][d+0*NSIZE]));
			asm volatile("vld $vr9, %0" : : "m"(dptr[z][d+1*NSIZE]));
			asm volatile("vld $vr10, %0" : : "m"(dptr[z][d+2*NSIZE]));
			asm volatile("vld $vr11, %0" : : "m"(dptr[z][d+3*NSIZE]));
			/* wp$$ ^= wd$$; */
			asm volatile("vxor.v $vr0, $vr0, $vr8");
			asm volatile("vxor.v $vr1, $vr1, $vr9");
			asm volatile("vxor.v $vr2, $vr2, $vr10");
			asm volatile("vxor.v $vr3, $vr3, $vr11");
			/* w2$$ = MASK(wq$$); */
			asm volatile("vslti.b $vr12, $vr4, 0");
			asm volatile("vslti.b $vr13, $vr5, 0");
			asm volatile("vslti.b $vr14, $vr6, 0");
			asm volatile("vslti.b $vr15, $vr7, 0");
			/* w1$$ = SHLBYTE(wq$$); */
			asm volatile("vslli.b $vr16, $vr4, 1");
			asm volatile("vslli.b $vr17, $vr5, 1");
			asm volatile("vslli.b $vr18, $vr6, 1");
			asm volatile("vslli.b $vr19, $vr7, 1");
			/* w2$$ &= NBYTES(0x1d); */
			asm volatile("vandi.b $vr12, $vr12, 0x1d");
			asm volatile("vandi.b $vr13, $vr13, 0x1d");
			asm volatile("vandi.b $vr14, $vr14, 0x1d");
			asm volatile("vandi.b $vr15, $vr15, 0x1d");
			/* w1$$ ^= w2$$; */
			asm volatile("vxor.v $vr16, $vr16, $vr12");
			asm volatile("vxor.v $vr17, $vr17, $vr13");
			asm volatile("vxor.v $vr18, $vr18, $vr14");
			asm volatile("vxor.v $vr19, $vr19, $vr15");
			/* wq$$ = w1$$ ^ wd$$; */
			asm volatile("vxor.v $vr4, $vr16, $vr8");
			asm volatile("vxor.v $vr5, $vr17, $vr9");
			asm volatile("vxor.v $vr6, $vr18, $vr10");
			asm volatile("vxor.v $vr7, $vr19, $vr11");
		}

		/* P/Q left side optimization */
		for (z = start-1; z >= 0; z--) {
			/* w2$$ = MASK(wq$$); */
			asm volatile("vslti.b $vr12, $vr4, 0");
			asm volatile("vslti.b $vr13, $vr5, 0");
			asm volatile("vslti.b $vr14, $vr6, 0");
			asm volatile("vslti.b $vr15, $vr7, 0");
			/* w1$$ = SHLBYTE(wq$$); */
			asm volatile("vslli.b $vr16, $vr4, 1");
			asm volatile("vslli.b $vr17, $vr5, 1");
			asm volatile("vslli.b $vr18, $vr6, 1");
			asm volatile("vslli.b $vr19, $vr7, 1");
			/* w2$$ &= NBYTES(0x1d); */
			asm volatile("vandi.b $vr12, $vr12, 0x1d");
			asm volatile("vandi.b $vr13, $vr13, 0x1d");
			asm volatile("vandi.b $vr14, $vr14, 0x1d");
			asm volatile("vandi.b $vr15, $vr15, 0x1d");
			/* wq$$ = w1$$ ^ w2$$; */
			asm volatile("vxor.v $vr4, $vr16, $vr12");
			asm volatile("vxor.v $vr5, $vr17, $vr13");
			asm volatile("vxor.v $vr6, $vr18, $vr14");
			asm volatile("vxor.v $vr7, $vr19, $vr15");
		}
		/*
		 * *(unative_t *)&p[d+NSIZE*$$] ^= wp$$;
		 * *(unative_t *)&q[d+NSIZE*$$] ^= wq$$;
		 */
		asm volatile(
			"vld $vr20, %0\n\t"
			"vld $vr21, %1\n\t"
			"vld $vr22, %2\n\t"
			"vld $vr23, %3\n\t"
			"vld $vr24, %4\n\t"
			"vld $vr25, %5\n\t"
			"vld $vr26, %6\n\t"
			"vld $vr27, %7\n\t"
			"vxor.v $vr20, $vr20, $vr0\n\t"
			"vxor.v $vr21, $vr21, $vr1\n\t"
			"vxor.v $vr22, $vr22, $vr2\n\t"
			"vxor.v $vr23, $vr23, $vr3\n\t"
			"vxor.v $vr24, $vr24, $vr4\n\t"
			"vxor.v $vr25, $vr25, $vr5\n\t"
			"vxor.v $vr26, $vr26, $vr6\n\t"
			"vxor.v $vr27, $vr27, $vr7\n\t"
			"vst $vr20, %0\n\t"
			"vst $vr21, %1\n\t"
			"vst $vr22, %2\n\t"
			"vst $vr23, %3\n\t"
			"vst $vr24, %4\n\t"
			"vst $vr25, %5\n\t"
			"vst $vr26, %6\n\t"
			"vst $vr27, %7\n\t"
			: "+m"(p[d+NSIZE*0]), "+m"(p[d+NSIZE*1]),
			  "+m"(p[d+NSIZE*2]), "+m"(p[d+NSIZE*3]),
			  "+m"(q[d+NSIZE*0]), "+m"(q[d+NSIZE*1]),
			  "+m"(q[d+NSIZE*2]), "+m"(q[d+NSIZE*3])
		);
	}

	kernel_fpu_end();
}

const struct raid6_calls raid6_lsx = {
	raid6_lsx_gen_syndrome,
	raid6_lsx_xor_syndrome,
	raid6_has_lsx,
	"lsx",
	.priority = 0 /* see the comment near the top of the file for reason */
};

#undef NSIZE
#endif /* CONFIG_CPU_HAS_LSX */

#ifdef CONFIG_CPU_HAS_LASX
#define NSIZE 32

static int raid6_has_lasx(void)
{
	return cpu_has_lasx;
}

static void raid6_lasx_gen_syndrome(int disks, size_t bytes, void **ptrs)
{
	u8 **dptr = (u8 **)ptrs;
	u8 *p, *q;
	int d, z, z0;

	z0 = disks - 3;		/* Highest data disk */
	p = dptr[z0+1];		/* XOR parity */
	q = dptr[z0+2];		/* RS syndrome */

	kernel_fpu_begin();

	/*
	 * $xr0, $xr1: wp
	 * $xr2, $xr3: wq
	 * $xr4, $xr5: wd
	 * $xr6, $xr7: w2
	 * $xr8, $xr9: w1
	 */
	for (d = 0; d < bytes; d += NSIZE*2) {
		/* wq$$ = wp$$ = *(unative_t *)&dptr[z0][d+$$*NSIZE]; */
		asm volatile("xvld $xr0, %0" : : "m"(dptr[z0][d+0*NSIZE]));
		asm volatile("xvld $xr1, %0" : : "m"(dptr[z0][d+1*NSIZE]));
		asm volatile("xvori.b $xr2, $xr0, 0");
		asm volatile("xvori.b $xr3, $xr1, 0");
		for (z = z0-1; z >= 0; z--) {
			/* wd$$ = *(unative_t *)&dptr[z][d+$$*NSIZE]; */
			asm volatile("xvld $xr4, %0" : : "m"(dptr[z][d+0*NSIZE]));
			asm volatile("xvld $xr5, %0" : : "m"(dptr[z][d+1*NSIZE]));
			/* wp$$ ^= wd$$; */
			asm volatile("xvxor.v $xr0, $xr0, $xr4");
			asm volatile("xvxor.v $xr1, $xr1, $xr5");
			/* w2$$ = MASK(wq$$); */
			asm volatile("xvslti.b $xr6, $xr2, 0");
			asm volatile("xvslti.b $xr7, $xr3, 0");
			/* w1$$ = SHLBYTE(wq$$); */
			asm volatile("xvslli.b $xr8, $xr2, 1");
			asm volatile("xvslli.b $xr9, $xr3, 1");
			/* w2$$ &= NBYTES(0x1d); */
			asm volatile("xvandi.b $xr6, $xr6, 0x1d");
			asm volatile("xvandi.b $xr7, $xr7, 0x1d");
			/* w1$$ ^= w2$$; */
			asm volatile("xvxor.v $xr8, $xr8, $xr6");
			asm volatile("xvxor.v $xr9, $xr9, $xr7");
			/* wq$$ = w1$$ ^ wd$$; */
			asm volatile("xvxor.v $xr2, $xr8, $xr4");
			asm volatile("xvxor.v $xr3, $xr9, $xr5");
		}
		/* *(unative_t *)&p[d+NSIZE*$$] = wp$$; */
		asm volatile("xvst $xr0, %0" : "=m"(p[d+NSIZE*0]));
		asm volatile("xvst $xr1, %0" : "=m"(p[d+NSIZE*1]));
		/* *(unative_t *)&q[d+NSIZE*$$] = wq$$; */
		asm volatile("xvst $xr2, %0" : "=m"(q[d+NSIZE*0]));
		asm volatile("xvst $xr3, %0" : "=m"(q[d+NSIZE*1]));
	}

	kernel_fpu_end();
}

static void raid6_lasx_xor_syndrome(int disks, int start, int stop,
				    size_t bytes, void **ptrs)
{
	u8 **dptr = (u8 **)ptrs;
	u8 *p, *q;
	int d, z, z0;

	z0 = stop;		/* P/Q right side optimization */
	p = dptr[disks-2];	/* XOR parity */
	q = dptr[disks-1];	/* RS syndrome */

	kernel_fpu_begin();

	/*
	 * $xr0, $xr1: wp
	 * $xr2, $xr3: wq
	 * $xr4, $xr5: wd
	 * $xr6, $xr7: w2
	 * $xr8, $xr9: w1
	 */
	for (d = 0; d < bytes; d += NSIZE*2) {
		/* P/Q data pages */
		/* wq$$ = wp$$ = *(unative_t *)&dptr[z0][d+$$*NSIZE]; */
		asm volatile("xvld $xr0, %0" : : "m"(dptr[z0][d+0*NSIZE]));
		asm volatile("xvld $xr1, %0" : : "m"(dptr[z0][d+1*NSIZE]));
		asm volatile("xvori.b $xr2, $xr0, 0");
		asm volatile("xvori.b $xr3, $xr1, 0");
		for (z = z0-1; z >= start; z--) {
			/* wd$$ = *(unative_t *)&dptr[z][d+$$*NSIZE]; */
			asm volatile("xvld $xr4, %0" : : "m"(dptr[z][d+0*NSIZE]));
			asm volatile("xvld $xr5, %0" : : "m"(dptr[z][d+1*NSIZE]));
			/* wp$$ ^= wd$$; */
			asm volatile("xvxor.v $xr0, $xr0, $xr4");
			asm volatile("xvxor.v $xr1, $xr1, $xr5");
			/* w2$$ = MASK(wq$$); */
			asm volatile("xvslti.b $xr6, $xr2, 0");
			asm volatile("xvslti.b $xr7, $xr3, 0");
			/* w1$$ = SHLBYTE(wq$$); */
			asm volatile("xvslli.b $xr8, $xr2, 1");
			asm volatile("xvslli.b $xr9, $xr3, 1");
			/* w2$$ &= NBYTES(0x1d); */
			asm volatile("xvandi.b $xr6, $xr6, 0x1d");
			asm volatile("xvandi.b $xr7, $xr7, 0x1d");
			/* w1$$ ^= w2$$; */
			asm volatile("xvxor.v $xr8, $xr8, $xr6");
			asm volatile("xvxor.v $xr9, $xr9, $xr7");
			/* wq$$ = w1$$ ^ wd$$; */
			asm volatile("xvxor.v $xr2, $xr8, $xr4");
			asm volatile("xvxor.v $xr3, $xr9, $xr5");
		}

		/* P/Q left side optimization */
		for (z = start-1; z >= 0; z--) {
			/* w2$$ = MASK(wq$$); */
			asm volatile("xvslti.b $xr6, $xr2, 0");
			asm volatile("xvslti.b $xr7, $xr3, 0");
			/* w1$$ = SHLBYTE(wq$$); */
			asm volatile("xvslli.b $xr8, $xr2, 1");
			asm volatile("xvslli.b $xr9, $xr3, 1");
			/* w2$$ &= NBYTES(0x1d); */
			asm volatile("xvandi.b $xr6, $xr6, 0x1d");
			asm volatile("xvandi.b $xr7, $xr7, 0x1d");
			/* wq$$ = w1$$ ^ w2$$; */
			asm volatile("xvxor.v $xr2, $xr8, $xr6");
			asm volatile("xvxor.v $xr3, $xr9, $xr7");
		}
		/*
		 * *(unative_t *)&p[d+NSIZE*$$] ^= wp$$;
		 * *(unative_t *)&q[d+NSIZE*$$] ^= wq$$;
		 */
		asm volatile(
			"xvld $xr10, %0\n\t"
			"xvld $xr11, %1\n\t"
			"xvld $xr12, %2\n\t"
			"xvld $xr13, %3\n\t"
			"xvxor.v $xr10, $xr10, $xr0\n\t"
			"xvxor.v $xr11, $xr11, $xr1\n\t"
			"xvxor.v $xr12, $xr12, $xr2\n\t"
			"xvxor.v $xr13, $xr13, $xr3\n\t"
			"xvst $xr10, %0\n\t"
			"xvst $xr11, %1\n\t"
			"xvst $xr12, %2\n\t"
			"xvst $xr13, %3\n\t"
			: "+m"(p[d+NSIZE*0]), "+m"(p[d+NSIZE*1]),
			  "+m"(q[d+NSIZE*0]), "+m"(q[d+NSIZE*1])
		);
	}

	kernel_fpu_end();
}

const struct raid6_calls raid6_lasx = {
	raid6_lasx_gen_syndrome,
	raid6_lasx_xor_syndrome,
	raid6_has_lasx,
	"lasx",
	.priority = 0 /* see the comment near the top of the file for reason */
};
#undef NSIZE
#endif /* CONFIG_CPU_HAS_LASX */
