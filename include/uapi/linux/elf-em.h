#ifndef _LINUX_ELF_EM_H
#define _LINUX_ELF_EM_H

/* These constants define the various ELF target machines */
#define EM_NONE		0
#define EM_M32		1
#define EM_SPARC	2
#define EM_386		3
#define EM_68K		4
#define EM_88K		5
#define EM_486		6	/* Perhaps disused */
#define EM_860		7
#define EM_MIPS		8	/* MIPS R3000 (officially, big-endian only) */
				/* Next two are historical and binaries and
				   modules of these types will be rejected by
				   Linux.  */
#define EM_MIPS_RS3_LE	10	/* MIPS R3000 little-endian */
#define EM_MIPS_RS4_BE	10	/* MIPS R4000 big-endian */

#define EM_PARISC	15	/* HPPA */
#define EM_SPARC32PLUS	18	/* Sun's "v8plus" */
#define EM_PPC		20	/* PowerPC */
#define EM_PPC64	21	 /* PowerPC64 */
#define EM_SPU		23	/* Cell BE SPU */
#define EM_ARM		40	/* ARM 32 bit */
#define EM_SH		42	/* SuperH */
#define EM_SPARCV9	43	/* SPARC v9 64-bit */
#define EM_H8_300	46	/* Renesas H8/300 */
#define EM_IA_64	50	/* HP/Intel IA-64 */
#define EM_X86_64	62	/* AMD x86-64 */
#define EM_S390		22	/* IBM S/390 */
#define EM_CRIS		76	/* Axis Communications 32-bit embedded processor */
#define EM_V850		87	/* NEC v850 */
#define EM_M32R		88	/* Renesas M32R */
#define EM_MN10300	89	/* Panasonic/MEI MN10300, AM33 */
#define EM_OPENRISC     92     /* OpenRISC 32-bit embedded processor */
#define EM_BLACKFIN     106     /* ADI Blackfin Processor */
#define EM_ALTERA_NIOS2	113	/* Altera Nios II soft-core processor */
#define EM_TI_C6000	140	/* TI C6X DSPs */
#define EM_AARCH64	183	/* ARM 64 bit */
#define EM_TILEPRO	188	/* Tilera TILEPro */
#define EM_MICROBLAZE	189	/* Xilinx MicroBlaze */
#define EM_TILEGX	191	/* Tilera TILE-Gx */
#define EM_FRV		0x5441	/* Fujitsu FR-V */
#define EM_AVR32	0x18ad	/* Atmel AVR32 */

/*
 * This is an interim value that we will use until the committee comes
 * up with a final number.
 */
#define EM_ALPHA	0x9026

/* Bogus old v850 magic number, used by old tools. */
#define EM_CYGNUS_V850	0x9080
/* Bogus old m32r magic number, used by old tools. */
#define EM_CYGNUS_M32R	0x9041
/* This is the old interim value for S/390 architecture */
#define EM_S390_OLD	0xA390
/* Also Panasonic/MEI MN10300, AM33 */
#define EM_CYGNUS_MN10300 0xbeef


#endif /* _LINUX_ELF_EM_H */
