/*	$OpenBSD: exec.h,v 1.11 2017/02/08 05:09:26 guenther Exp $	*/
/*	$NetBSD: elf_machdep.h,v 1.7 2001/02/11 00:18:49 eeh Exp $	*/

#define ARCH_ELFSIZE		64	/* MD native binary size */
#define ELF_TARG_CLASS		ELFCLASS64
#define ELF_TARG_MACH		EM_SPARCV9

#define ELF_TARG_DATA		ELFDATA2MSB

/* The following are what is used for AT_SUN_HWCAP: */
#define AV_SPARC_HWMUL_32x32	1	/* 32x32-bit smul/umul is efficient */
#define	AV_SPARC_HWDIV_32x32	2	/* 32x32-bit sdiv/udiv is efficient */
#define	AV_SPARC_HWFSMULD	4	/* fsmuld is efficient */

/*
 * Here are some SPARC specific flags I can't 
 * find a better home for.  They are used for AT_FLAGS
 * and in the exec header.
 */
#define	EF_SPARCV9_MM		0x3
#define	EF_SPARCV9_TSO		0x0
#define	EF_SPARCV9_PSO		0x1
#define	EF_SPARCV9_RMO		0x2

#define EF_SPARC_32PLUS_MASK    0xffff00        /* bits indicating V8+ type */
#define EF_SPARC_32PLUS         0x000100        /* generic V8+ features */
#define EF_SPARC_EXT_MASK       0xffff00        /* bits for vendor extensions */
#define	EF_SPARC_SUN_US1	0x000200	/* UltraSPARC 1 extensions */	
#define	EF_SPARC_HAL_R1		0x000400	/* HAL R1 extensions */
#define	EF_SPARC_SUN_US3	0x000800	/* UltraSPARC 3 extensions */

/* Relocation types */
#define R_SPARC_NONE		0
#define R_SPARC_8		1
#define R_SPARC_16		2
#define R_SPARC_32		3
#define R_SPARC_DISP8		4
#define R_SPARC_DISP16		5
#define R_SPARC_DISP32		6
#define R_SPARC_WDISP30		7
#define R_SPARC_WDISP22		8
#define R_SPARC_HI22		9
#define R_SPARC_22		10
#define R_SPARC_13		11
#define R_SPARC_LO10		12
#define R_SPARC_GOT10		13
#define R_SPARC_GOT13		14
#define R_SPARC_GOT22		15
#define R_SPARC_PC10		16
#define R_SPARC_PC22		17
#define R_SPARC_WPLT30		18
#define R_SPARC_COPY		19
#define R_SPARC_GLOB_DAT	20
#define R_SPARC_JMP_SLOT	21
#define R_SPARC_RELATIVE	22
#define R_SPARC_UA32		23
#define R_SPARC_PLT32		24
#define R_SPARC_HIPLT22		25
#define R_SPARC_LOPLT10		26
#define R_SPARC_PCPLT32		27
#define R_SPARC_PCPLT22		28
#define R_SPARC_PCPLT10		29
#define R_SPARC_10		30
#define R_SPARC_11		31
#define R_SPARC_64		32
#define R_SPARC_OLO10		33
#define R_SPARC_HH22		34
#define R_SPARC_HM10		35
#define R_SPARC_LM22		36
#define R_SPARC_PC_HH22		37
#define R_SPARC_PC_HM10		38
#define R_SPARC_PC_LM22		39
#define R_SPARC_WDISP16		40
#define R_SPARC_WDISP19		41
#define R_SPARC_GLOB_JMP	42
#define R_SPARC_7		43
#define R_SPARC_5		44
#define R_SPARC_6		45
#define	R_SPARC_DISP64		46
#define	R_SPARC_PLT64		47
#define	R_SPARC_HIX22		48
#define	R_SPARC_LOX10		49
#define	R_SPARC_H44		50
#define	R_SPARC_M44		51
#define	R_SPARC_L44		52
#define	R_SPARC_REGISTER	53
#define	R_SPARC_UA64		54
#define	R_SPARC_UA16		55
#define	R_SPARC_TLS_DTPMOD32	74
#define	R_SPARC_TLS_DTPMOD64	75
#define	R_SPARC_TLS_DTPOFF32	76
#define	R_SPARC_TLS_DTPOFF64	77
#define	R_SPARC_TLS_TPOFF32	78
#define	R_SPARC_TLS_TPOFF64	79


#define R_TYPE(name)		__CONCAT(R_SPARC_,name)

#define	__LDPGSZ		8192	/* linker page size */
