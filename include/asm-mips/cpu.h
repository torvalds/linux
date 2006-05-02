/*
 * cpu.h: Values of the PRId register used to match up
 *        various MIPS cpu types.
 *
 * Copyright (C) 1996 David S. Miller (dm@engr.sgi.com)
 * Copyright (C) 2004  Maciej W. Rozycki
 */
#ifndef _ASM_CPU_H
#define _ASM_CPU_H

/* Assigned Company values for bits 23:16 of the PRId Register
   (CP0 register 15, select 0).  As of the MIPS32 and MIPS64 specs from
   MTI, the PRId register is defined in this (backwards compatible)
   way:

  +----------------+----------------+----------------+----------------+
  | Company Options| Company ID     | Processor ID   | Revision       |
  +----------------+----------------+----------------+----------------+
   31            24 23            16 15             8 7

   I don't have docs for all the previous processors, but my impression is
   that bits 16-23 have been 0 for all MIPS processors before the MIPS32/64
   spec.
*/

#define PRID_COMP_LEGACY	0x000000
#define PRID_COMP_MIPS		0x010000
#define PRID_COMP_BROADCOM	0x020000
#define PRID_COMP_ALCHEMY	0x030000
#define PRID_COMP_SIBYTE	0x040000
#define PRID_COMP_SANDCRAFT	0x050000
#define PRID_COMP_PHILIPS	0x060000
#define PRID_COMP_TOSHIBA	0x070000
#define PRID_COMP_LSI		0x080000
#define PRID_COMP_LEXRA		0x0b0000


/*
 * Assigned values for the product ID register.  In order to detect a
 * certain CPU type exactly eventually additional registers may need to
 * be examined.  These are valid when 23:16 == PRID_COMP_LEGACY
 */
#define PRID_IMP_R2000		0x0100
#define PRID_IMP_AU1_REV1	0x0100
#define PRID_IMP_AU1_REV2	0x0200
#define PRID_IMP_R3000		0x0200		/* Same as R2000A  */
#define PRID_IMP_R6000		0x0300		/* Same as R3000A  */
#define PRID_IMP_R4000		0x0400
#define PRID_IMP_R6000A		0x0600
#define PRID_IMP_R10000		0x0900
#define PRID_IMP_R4300		0x0b00
#define PRID_IMP_VR41XX		0x0c00
#define PRID_IMP_R12000		0x0e00
#define PRID_IMP_R8000		0x1000
#define PRID_IMP_PR4450		0x1200
#define PRID_IMP_R4600		0x2000
#define PRID_IMP_R4700		0x2100
#define PRID_IMP_TX39		0x2200
#define PRID_IMP_R4640		0x2200
#define PRID_IMP_R4650		0x2200		/* Same as R4640 */
#define PRID_IMP_R5000		0x2300
#define PRID_IMP_TX49		0x2d00
#define PRID_IMP_SONIC		0x2400
#define PRID_IMP_MAGIC		0x2500
#define PRID_IMP_RM7000		0x2700
#define PRID_IMP_NEVADA		0x2800		/* RM5260 ??? */
#define PRID_IMP_RM9000		0x3400
#define PRID_IMP_R5432		0x5400
#define PRID_IMP_R5500		0x5500

#define PRID_IMP_UNKNOWN	0xff00

/*
 * These are the PRID's for when 23:16 == PRID_COMP_MIPS
 */

#define PRID_IMP_4KC		0x8000
#define PRID_IMP_5KC		0x8100
#define PRID_IMP_20KC		0x8200
#define PRID_IMP_4KEC		0x8400
#define PRID_IMP_4KSC		0x8600
#define PRID_IMP_25KF		0x8800
#define PRID_IMP_5KE		0x8900
#define PRID_IMP_4KECR2		0x9000
#define PRID_IMP_4KEMPR2	0x9100
#define PRID_IMP_4KSD		0x9200
#define PRID_IMP_24K		0x9300
#define PRID_IMP_34K		0x9500
#define PRID_IMP_24KE		0x9600
#define PRID_IMP_74K		0x9700

/*
 * These are the PRID's for when 23:16 == PRID_COMP_SIBYTE
 */

#define PRID_IMP_SB1            0x0100
#define PRID_IMP_SB1A           0x1100

/*
 * These are the PRID's for when 23:16 == PRID_COMP_SANDCRAFT
 */

#define PRID_IMP_SR71000        0x0400

/*
 * Definitions for 7:0 on legacy processors
 */


#define PRID_REV_TX4927		0x0022
#define PRID_REV_TX4937		0x0030
#define PRID_REV_R4400		0x0040
#define PRID_REV_R3000A		0x0030
#define PRID_REV_R3000		0x0020
#define PRID_REV_R2000A		0x0010
#define PRID_REV_TX3912 	0x0010
#define PRID_REV_TX3922 	0x0030
#define PRID_REV_TX3927 	0x0040
#define PRID_REV_VR4111		0x0050
#define PRID_REV_VR4181		0x0050	/* Same as VR4111 */
#define PRID_REV_VR4121		0x0060
#define PRID_REV_VR4122		0x0070
#define PRID_REV_VR4181A	0x0070	/* Same as VR4122 */
#define PRID_REV_VR4130		0x0080

/*
 * FPU implementation/revision register (CP1 control register 0).
 *
 * +---------------------------------+----------------+----------------+
 * | 0                               | Implementation | Revision       |
 * +---------------------------------+----------------+----------------+
 *  31                             16 15             8 7              0
 */

#define FPIR_IMP_NONE		0x0000

#define CPU_UNKNOWN		 0
#define CPU_R2000		 1
#define CPU_R3000		 2
#define CPU_R3000A		 3
#define CPU_R3041		 4
#define CPU_R3051		 5
#define CPU_R3052		 6
#define CPU_R3081		 7
#define CPU_R3081E		 8
#define CPU_R4000PC		 9
#define CPU_R4000SC		10
#define CPU_R4000MC		11
#define CPU_R4200		12
#define CPU_R4400PC		13
#define CPU_R4400SC		14
#define CPU_R4400MC		15
#define CPU_R4600		16
#define CPU_R6000		17
#define CPU_R6000A		18
#define CPU_R8000		19
#define CPU_R10000		20
#define CPU_R12000		21
#define CPU_R4300		22
#define CPU_R4650		23
#define CPU_R4700		24
#define CPU_R5000		25
#define CPU_R5000A		26
#define CPU_R4640		27
#define CPU_NEVADA		28
#define CPU_RM7000		29
#define CPU_R5432		30
#define CPU_4KC			31
#define CPU_5KC			32
#define CPU_R4310		33
#define CPU_SB1			34
#define CPU_TX3912		35
#define CPU_TX3922		36
#define CPU_TX3927		37
#define CPU_AU1000		38
#define CPU_4KEC		39
#define CPU_4KSC		40
#define CPU_VR41XX		41
#define CPU_R5500		42
#define CPU_TX49XX		43
#define CPU_AU1500		44
#define CPU_20KC		45
#define CPU_VR4111		46
#define CPU_VR4121		47
#define CPU_VR4122		48
#define CPU_VR4131		49
#define CPU_VR4181		50
#define CPU_VR4181A		51
#define CPU_AU1100		52
#define CPU_SR71000		53
#define CPU_RM9000		54
#define CPU_25KF		55
#define CPU_VR4133		56
#define CPU_AU1550		57
#define CPU_24K			58
#define CPU_AU1200		59
#define CPU_34K			60
#define CPU_PR4450		61
#define CPU_SB1A		62
#define CPU_74K			63
#define CPU_LAST		63

/*
 * ISA Level encodings
 *
 */
#define MIPS_CPU_ISA_I		0x00000001
#define MIPS_CPU_ISA_II		0x00000002
#define MIPS_CPU_ISA_III	0x00000004
#define MIPS_CPU_ISA_IV		0x00000008
#define MIPS_CPU_ISA_V		0x00000010
#define MIPS_CPU_ISA_M32R1	0x00000020
#define MIPS_CPU_ISA_M32R2	0x00000040
#define MIPS_CPU_ISA_M64R1	0x00000080
#define MIPS_CPU_ISA_M64R2	0x00000100

#define MIPS_CPU_ISA_32BIT (MIPS_CPU_ISA_I | MIPS_CPU_ISA_II | \
	MIPS_CPU_ISA_M32R1 | MIPS_CPU_ISA_M32R2 )
#define MIPS_CPU_ISA_64BIT (MIPS_CPU_ISA_III | MIPS_CPU_ISA_IV | \
	MIPS_CPU_ISA_V | MIPS_CPU_ISA_M64R1 | MIPS_CPU_ISA_M64R2)

/*
 * CPU Option encodings
 */
#define MIPS_CPU_TLB		0x00000001 /* CPU has TLB */
#define MIPS_CPU_4KEX		0x00000002 /* "R4K" exception model */
#define MIPS_CPU_3K_CACHE	0x00000004 /* R3000-style caches */
#define MIPS_CPU_4K_CACHE	0x00000008 /* R4000-style caches */
#define MIPS_CPU_TX39_CACHE	0x00000010 /* TX3900-style caches */
#define MIPS_CPU_SB1_CACHE	0x00000020 /* SB1-style caches */
#define MIPS_CPU_FPU		0x00000040 /* CPU has FPU */
#define MIPS_CPU_32FPR		0x00000080 /* 32 dbl. prec. FP registers */
#define MIPS_CPU_COUNTER	0x00000100 /* Cycle count/compare */
#define MIPS_CPU_WATCH		0x00000200 /* watchpoint registers */
#define MIPS_CPU_DIVEC		0x00000400 /* dedicated interrupt vector */
#define MIPS_CPU_VCE		0x00000800 /* virt. coherence conflict possible */
#define MIPS_CPU_CACHE_CDEX_P	0x00001000 /* Create_Dirty_Exclusive CACHE op */
#define MIPS_CPU_CACHE_CDEX_S	0x00002000 /* ... same for seconary cache ... */
#define MIPS_CPU_MCHECK		0x00004000 /* Machine check exception */
#define MIPS_CPU_EJTAG		0x00008000 /* EJTAG exception */
#define MIPS_CPU_NOFPUEX	0x00010000 /* no FPU exception */
#define MIPS_CPU_LLSC		0x00020000 /* CPU has ll/sc instructions */
#define MIPS_CPU_SUBSET_CACHES	0x00040000 /* P-cache subset enforced */
#define MIPS_CPU_PREFETCH	0x00080000 /* CPU has usable prefetch */
#define MIPS_CPU_VINT		0x00100000 /* CPU supports MIPSR2 vectored interrupts */
#define MIPS_CPU_VEIC		0x00200000 /* CPU supports MIPSR2 external interrupt controller mode */

/*
 * CPU ASE encodings
 */
#define MIPS_ASE_MIPS16		0x00000001 /* code compression */
#define MIPS_ASE_MDMX		0x00000002 /* MIPS digital media extension */
#define MIPS_ASE_MIPS3D		0x00000004 /* MIPS-3D */
#define MIPS_ASE_SMARTMIPS	0x00000008 /* SmartMIPS */
#define MIPS_ASE_DSP		0x00000010 /* Signal Processing ASE */
#define MIPS_ASE_MIPSMT		0x00000020 /* CPU supports MIPS MT */


#endif /* _ASM_CPU_H */
