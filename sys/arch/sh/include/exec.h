/*	$OpenBSD: exec.h,v 1.5 2017/02/08 05:09:26 guenther Exp $	*/
/*	$NetBSD: elf_machdep.h,v 1.8 2002/04/28 17:10:34 uch Exp $	*/

#define __LDPGSZ	4096

#define	ARCH_ELFSIZE		32	/* MD native binary size */
#define	ELF_TARG_CLASS		ELFCLASS32
#ifdef __LITTLE_ENDIAN__
#define	ELF_TARG_DATA		ELFDATA2LSB
#else
#define	ELF_TARG_DATA		ELFDATA2MSB
#endif
#define	ELF_TARG_MACH		EM_SH

/*
 * SuperH ELF header flags.
 */
#define	EF_SH_MACH_MASK		0x1f

#define	EF_SH_UNKNOWN		0x00
#define	EF_SH_SH1		0x01
#define	EF_SH_SH2		0x02
#define	EF_SH_SH3		0x03
#define	EF_SH_DSP		0x04
#define	EF_SH_SH3_DSP		0x05
#define	EF_SH_SH3E		0x08
#define	EF_SH_SH4		0x09

#define	EF_SH_HAS_DSP(x)	((x) & EF_SH_DSP)
#define	EF_SH_HAS_FP(x)		((x) & EF_SH_SH3E)
