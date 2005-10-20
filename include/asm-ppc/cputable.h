/*
 *  include/asm-ppc/cputable.h
 *
 *  Copyright (C) 2001 Ben. Herrenschmidt (benh@kernel.crashing.org)
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 */

#ifndef __ASM_PPC_CPUTABLE_H
#define __ASM_PPC_CPUTABLE_H

/* Exposed to userland CPU features */
#define PPC_FEATURE_32			0x80000000
#define PPC_FEATURE_64			0x40000000
#define PPC_FEATURE_601_INSTR		0x20000000
#define PPC_FEATURE_HAS_ALTIVEC		0x10000000
#define PPC_FEATURE_HAS_FPU		0x08000000
#define PPC_FEATURE_HAS_MMU		0x04000000
#define PPC_FEATURE_HAS_4xxMAC		0x02000000
#define PPC_FEATURE_UNIFIED_CACHE	0x01000000
#define PPC_FEATURE_HAS_SPE		0x00800000
#define PPC_FEATURE_HAS_EFP_SINGLE	0x00400000
#define PPC_FEATURE_HAS_EFP_DOUBLE	0x00200000
#define PPC_FEATURE_NO_TB		0x00100000

#ifdef __KERNEL__

#ifndef __ASSEMBLY__

/* This structure can grow, it's real size is used by head.S code
 * via the mkdefs mecanism.
 */
struct cpu_spec;

typedef	void (*cpu_setup_t)(unsigned long offset, int cpu_nr, struct cpu_spec* spec);

struct cpu_spec {
	/* CPU is matched via (PVR & pvr_mask) == pvr_value */
	unsigned int	pvr_mask;
	unsigned int	pvr_value;

	char		*cpu_name;
	unsigned int	cpu_features;		/* Kernel features */
	unsigned int	cpu_user_features;	/* Userland features */

	/* cache line sizes */
	unsigned int	icache_bsize;
	unsigned int	dcache_bsize;

	/* number of performance monitor counters */
	unsigned int	num_pmcs;

	/* this is called to initialize various CPU bits like L1 cache,
	 * BHT, SPD, etc... from head.S before branching to identify_machine
	 */
	cpu_setup_t	cpu_setup;
};

extern struct cpu_spec		cpu_specs[];
extern struct cpu_spec		*cur_cpu_spec[];

static inline unsigned int cpu_has_feature(unsigned int feature)
{
	return cur_cpu_spec[0]->cpu_features & feature;
}

#endif /* __ASSEMBLY__ */

/* CPU kernel features */
#define CPU_FTR_SPLIT_ID_CACHE		0x00000001
#define CPU_FTR_L2CR			0x00000002
#define CPU_FTR_SPEC7450		0x00000004
#define CPU_FTR_ALTIVEC			0x00000008
#define CPU_FTR_TAU			0x00000010
#define CPU_FTR_CAN_DOZE		0x00000020
#define CPU_FTR_USE_TB			0x00000040
#define CPU_FTR_604_PERF_MON		0x00000080
#define CPU_FTR_601			0x00000100
#define CPU_FTR_HPTE_TABLE		0x00000200
#define CPU_FTR_CAN_NAP			0x00000400
#define CPU_FTR_L3CR			0x00000800
#define CPU_FTR_L3_DISABLE_NAP		0x00001000
#define CPU_FTR_NAP_DISABLE_L2_PR	0x00002000
#define CPU_FTR_DUAL_PLL_750FX		0x00004000
#define CPU_FTR_NO_DPM			0x00008000
#define CPU_FTR_HAS_HIGH_BATS		0x00010000
#define CPU_FTR_NEED_COHERENT		0x00020000
#define CPU_FTR_NO_BTIC			0x00040000
#define CPU_FTR_BIG_PHYS		0x00080000

#ifdef __ASSEMBLY__

#define BEGIN_FTR_SECTION		98:

#define END_FTR_SECTION(msk, val)		\
99:						\
	.section __ftr_fixup,"a";		\
	.align 2;				\
	.long msk;				\
	.long val;				\
	.long 98b;				\
	.long 99b;				\
	.previous

#else

#define BEGIN_FTR_SECTION		"98:\n"
#define END_FTR_SECTION(msk, val)		\
"99:\n"						\
"	.section __ftr_fixup,\"a\";\n"		\
"	.align 2;\n"				\
"	.long "#msk";\n"			\
"	.long "#val";\n"			\
"	.long 98b;\n"			        \
"	.long 99b;\n"	 		        \
"	.previous\n"


#endif /* __ASSEMBLY__ */

#define END_FTR_SECTION_IFSET(msk)	END_FTR_SECTION((msk), (msk))
#define END_FTR_SECTION_IFCLR(msk)	END_FTR_SECTION((msk), 0)

#endif /* __ASM_PPC_CPUTABLE_H */
#endif /* __KERNEL__ */

