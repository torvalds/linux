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
#define PRID_IMP_R14000		0x0f00
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
#define PRID_IMP_LOONGSON1      0x4200
#define PRID_IMP_LOONGSON2      0x6300

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
 * These are the PRID's for when 23:16 == PRID_COMP_BROADCOM
 */

#define PRID_IMP_BCM4710	0x4000
#define PRID_IMP_BCM3302	0x9000

/*
 * Definitions for 7:0 on legacy processors
 */

#define PRID_REV_MASK		0x00ff

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
#define PRID_REV_34K_V1_0_2	0x0022

/*
 * Older processors used to encode processor version and revision in two
 * 4-bit bitfields, the 4K seems to simply count up and even newer MTI cores
 * have switched to use the 8-bits as 3:3:2 bitfield with the last field as
 * the patch number.  *ARGH*
 */
#define PRID_REV_ENCODE_44(ver, rev)					\
	((ver) << 4 | (rev))
#define PRID_REV_ENCODE_332(ver, rev, patch)				\
	((ver) << 5 | (rev) << 2 | (patch))

/*
 * FPU implementation/revision register (CP1 control register 0).
 *
 * +---------------------------------+----------------+----------------+
 * | 0                               | Implementation | Revision       |
 * +---------------------------------+----------------+----------------+
 *  31                             16 15             8 7              0
 */

#define FPIR_IMP_NONE		0x0000

enum cpu_type_enum {
	CPU_UNKNOWN,

	/*
	 * R2000 class processors
	 */
	CPU_R2000, CPU_R3000, CPU_R3000A, CPU_R3041, CPU_R3051, CPU_R3052,
	CPU_R3081, CPU_R3081E,

	/*
	 * R6000 class processors
	 */
	CPU_R6000, CPU_R6000A,

	/*
	 * R4000 class processors
	 */
	CPU_R4000PC, CPU_R4000SC, CPU_R4000MC, CPU_R4200, CPU_R4300, CPU_R4310,
	CPU_R4400PC, CPU_R4400SC, CPU_R4400MC, CPU_R4600, CPU_R4640, CPU_R4650,
	CPU_R4700, CPU_R5000, CPU_R5000A, CPU_R5500, CPU_NEVADA, CPU_R5432,
	CPU_R10000, CPU_R12000, CPU_R14000, CPU_VR41XX, CPU_VR4111, CPU_VR4121,
	CPU_VR4122, CPU_VR4131, CPU_VR4133, CPU_VR4181, CPU_VR4181A, CPU_RM7000,
	CPU_SR71000, CPU_RM9000, CPU_TX49XX,

	/*
	 * R8000 class processors
	 */
	CPU_R8000,

	/*
	 * TX3900 class processors
	 */
	CPU_TX3912, CPU_TX3922, CPU_TX3927,

	/*
	 * MIPS32 class processors
	 */
	CPU_4KC, CPU_4KEC, CPU_4KSC, CPU_24K, CPU_34K, CPU_74K, CPU_AU1000,
	CPU_AU1100, CPU_AU1200, CPU_AU1210, CPU_AU1250, CPU_AU1500, CPU_AU1550,
	CPU_PR4450, CPU_BCM3302, CPU_BCM4710,

	/*
	 * MIPS64 class processors
	 */
	CPU_5KC, CPU_20KC, CPU_25KF, CPU_SB1, CPU_SB1A, CPU_LOONGSON2,

	CPU_LAST
};


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
#define MIPS_CPU_FPU		0x00000020 /* CPU has FPU */
#define MIPS_CPU_32FPR		0x00000040 /* 32 dbl. prec. FP registers */
#define MIPS_CPU_COUNTER	0x00000080 /* Cycle count/compare */
#define MIPS_CPU_WATCH		0x00000100 /* watchpoint registers */
#define MIPS_CPU_DIVEC		0x00000200 /* dedicated interrupt vector */
#define MIPS_CPU_VCE		0x00000400 /* virt. coherence conflict possible */
#define MIPS_CPU_CACHE_CDEX_P	0x00000800 /* Create_Dirty_Exclusive CACHE op */
#define MIPS_CPU_CACHE_CDEX_S	0x00001000 /* ... same for seconary cache ... */
#define MIPS_CPU_MCHECK		0x00002000 /* Machine check exception */
#define MIPS_CPU_EJTAG		0x00004000 /* EJTAG exception */
#define MIPS_CPU_NOFPUEX	0x00008000 /* no FPU exception */
#define MIPS_CPU_LLSC		0x00010000 /* CPU has ll/sc instructions */
#define MIPS_CPU_INCLUSIVE_CACHES	0x00020000 /* P-cache subset enforced */
#define MIPS_CPU_PREFETCH	0x00040000 /* CPU has usable prefetch */
#define MIPS_CPU_VINT		0x00080000 /* CPU supports MIPSR2 vectored interrupts */
#define MIPS_CPU_VEIC		0x00100000 /* CPU supports MIPSR2 external interrupt controller mode */
#define MIPS_CPU_ULRI		0x00200000 /* CPU has ULRI feature */

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
