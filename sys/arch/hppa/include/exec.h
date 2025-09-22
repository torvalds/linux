/*	$OpenBSD: exec.h,v 1.15 2022/02/21 12:22:21 jsg Exp $	*/

/* 
 * Copyright (c) 1994, The University of Utah and
 * the Computer Systems Laboratory at the University of Utah (CSL).
 * All rights reserved.
 *
 * Permission to use, copy, modify and distribute this software is hereby
 * granted provided that (1) source code retains these copyright, permission,
 * and disclaimer notices, and (2) redistributions including binaries
 * reproduce the notices in supporting documentation, and (3) all advertising
 * materials mentioning features or use of this software display the following
 * acknowledgement: ``This product includes software developed by the
 * Computer Systems Laboratory at the University of Utah.''
 *
 * THE UNIVERSITY OF UTAH AND CSL ALLOW FREE USE OF THIS SOFTWARE IN ITS "AS
 * IS" CONDITION.  THE UNIVERSITY OF UTAH AND CSL DISCLAIM ANY LIABILITY OF
 * ANY KIND FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * CSL requests users of this software to return to csl-dist@cs.utah.edu any
 * improvements that they make and grant CSL redistribution rights.
 *
 * 	Utah $Hdr: exec.h 1.3 94/12/16$
 */

#ifndef	_MACHINE_EXEC_H_
#define	_MACHINE_EXEC_H_

/* Size of a page in an object file. */
#define	__LDPGSZ	4096

#define ARCH_ELFSIZE		32

#define ELF_TARG_CLASS          ELFCLASS32
#define ELF_TARG_DATA           ELFDATA2MSB
#define ELF_TARG_MACH           EM_PARISC   

/*
 * the following MD ELF values definitions are from the:
 * "Processor-Specific ELF Supplement for PA-RISC.
 *  Including HP and HP-UX Extensions. Version 1.43. October 6, 1997"
 *	http://devrsrc1.external.hp.com/STK/partner/elf-pa.pdf
 *
 */

/* parisc-specific elf flags */
#define	EF_PARISC_TRAPNIL	0x00010000	/* trap on NULL derefs */
#define	EF_PARISC_EXT		0x00020000	/* program uses arch exts */
#define	EF_PARISC_LSB		0x00040000	/* program expects LSB mode */
#define	EF_PARISC_WIDE		0x00080000	/* program expects wide mode */
#define	EF_PARISC_NO_KABP	0x00100000	/* don't allow kernel assisted
						   branch prediction */
#define	EF_PARISC_LAZYSWAP	0x00200000	/* allow lazy swap allocation
						   for dynamically allocated
						   program segments */
#define	EF_PARISC_ARCH		0x0000ffff	/* architecture version */
#define		EFA_PARISC_1_0	0x020B
#define		EFA_PARISC_1_1	0x0210
#define		EFA_PARISC_2_0	0x0214

/* legend: 0 - pa7000, 1 - pa7100, 2 - pa7200, 3 - pa7100LC, 4 - pa8000 */
#define	PARISC_AE_QWSI	0x00000001	/* 0  : enable quadword stores */
#define	PARISC_AE_FPLSU	0x00000002	/*   1: fp load/store to I/O space */
#define	PARISC_AE_RSQRT	0x00000004	/* 0  : reciprocal sqrt */
#define	PARISC_AE_FDCG	0x00000008	/* 0,1: fdc includes graph flushes */
#define	PARISC_AE_HPAR	0x00000010	/* 3,4: half-word add/sub/av */
#define	PARISC_AE_BSW	0x00000020	/* 3,4: half-word shift-add */
#define	PARISC_AE_HPSA	0x00000040	/* 3  : byte-swapping stores */
#define	PARISC_AE_DPR0	0x00000080	/* 2,4: data prefetch via ld to r0 */

#define	SHN_PARISC_ANSI_COMMON	0xff00
#define	SHN_PARISC_HUGE_COMMON	0xff01

/* sh_type */
#define	SHT_PARISC_EXT		0x70000000	/* contains product-specific
						   extension bits */
#define	SHT_PARISC_UNWIND	0x70000001	/* contains unwind table entries
						   sh_info contains index of
						   the code section to which
						   unwind entries apply */
#define	SHT_PARISC_DOC		0x70000002	/* contains debug info for -O */
#define	SHT_PARISC_ANNOT	0x70000003	/* contains code annotations */

/* sh_flags */
#define	SHF_PARISC_SBP	0x80000000	/* contains code compiled for
					   static branch prediction */
#define	SHF_PARISC_HUGE	0x40000000	/* should be allocated far from gp */
#define	SHF_PARISC_SHORT 0x20000000	/* should be allocated near from gp */

#define	ELF_PARISC_ARCHEXT	".PARISC.archext"
#define	ELF_PARISC_MILLI	".PARISC.milli"
#define	ELF_PARISC_UNWIND	".PARISC.unwind"
#define	ELF_PARISC_UNWIND_INFO	".PARISC.unwind_info"
#define	ELF_PARISC_SDATA	".sdata"
#define	ELF_PARISC_NOBITS	".sbss"

#define	STT_PARISC_MILLI	13	/* entry point of a millicode routine */

#define	PT_PARISC_ARCHEXT	0x70000000	/* segment contains
						   .PARISC.archext section */
#define	PT_PARISC_UNWIND	0x70000001	/* segment contains
						   .unwind section */

#define	PF_PARISC_SBP		0x08000000	/* segment contains code
					compiled for static branch prediction */

#endif	/* _MACHINE_EXEC_H_ */
