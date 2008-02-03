/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994 Waldorf GMBH
 * Copyright (C) 1995, 1996, 1997, 1998, 1999, 2001, 2002, 2003 Ralf Baechle
 * Copyright (C) 1996 Paul M. Antoine
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 * Copyright (C) 2004  Maciej W. Rozycki
 */
#ifndef __ASM_CPU_INFO_H
#define __ASM_CPU_INFO_H

#include <asm/cache.h>

/*
 * Descriptor for a cache
 */
struct cache_desc {
	unsigned int waysize;	/* Bytes per way */
	unsigned short sets;	/* Number of lines per set */
	unsigned char ways;	/* Number of ways */
	unsigned char linesz;	/* Size of line in bytes */
	unsigned char waybit;	/* Bits to select in a cache set */
	unsigned char flags;	/* Flags describing cache properties */
};

/*
 * Flag definitions
 */
#define MIPS_CACHE_NOT_PRESENT	0x00000001
#define MIPS_CACHE_VTAG		0x00000002	/* Virtually tagged cache */
#define MIPS_CACHE_ALIASES	0x00000004	/* Cache could have aliases */
#define MIPS_CACHE_IC_F_DC	0x00000008	/* Ic can refill from D-cache */
#define MIPS_IC_SNOOPS_REMOTE	0x00000010	/* Ic snoops remote stores */
#define MIPS_CACHE_PINDEX	0x00000020	/* Physically indexed cache */

struct cpuinfo_mips {
	unsigned long		udelay_val;
	unsigned long		asid_cache;

	/*
	 * Capability and feature descriptor structure for MIPS CPU
	 */
	unsigned long		options;
	unsigned long		ases;
	unsigned int		processor_id;
	unsigned int		fpu_id;
	unsigned int		cputype;
	int			isa_level;
	int			tlbsize;
	struct cache_desc	icache;	/* Primary I-cache */
	struct cache_desc	dcache;	/* Primary D or combined I/D cache */
	struct cache_desc	scache;	/* Secondary cache */
	struct cache_desc	tcache;	/* Tertiary/split secondary cache */
	int			srsets;	/* Shadow register sets */
	int			core;	/* physical core number */
#if defined(CONFIG_MIPS_MT_SMTC)
	/*
	 * In the MIPS MT "SMTC" model, each TC is considered
	 * to be a "CPU" for the purposes of scheduling, but
	 * exception resources, ASID spaces, etc, are common
	 * to all TCs within the same VPE.
	 */
	int			vpe_id;  /* Virtual Processor number */
#endif /* CONFIG_MIPS_MT */
#ifdef CONFIG_MIPS_MT_SMTC
	int			tc_id;   /* Thread Context number */
#endif
	void 			*data;	/* Additional data */
} __attribute__((aligned(SMP_CACHE_BYTES)));

extern struct cpuinfo_mips cpu_data[];
#define current_cpu_data cpu_data[smp_processor_id()]
#define raw_current_cpu_data cpu_data[raw_smp_processor_id()]

extern void cpu_probe(void);
extern void cpu_report(void);

extern const char *__cpu_name[];
#define cpu_name_string()	__cpu_name[smp_processor_id()]

#endif /* __ASM_CPU_INFO_H */
