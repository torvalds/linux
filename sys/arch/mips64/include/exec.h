/*	$OpenBSD: exec.h,v 1.10 2022/10/28 15:07:25 kettenis Exp $	*/

/*
 * Copyright (c) 1996-2004 Per Fogelstrom, Opsycon AB
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#ifndef _MIPS64_EXEC_H_
#define _MIPS64_EXEC_H_

#define	__LDPGSZ	16384

#define	ARCH_ELFSIZE 64
#define ELF_TARG_CLASS  ELFCLASS64

#if defined(__MIPSEB__)
#define ELF_TARG_DATA		ELFDATA2MSB
#else
#define ELF_TARG_DATA		ELFDATA2LSB
#endif
#define ELF_TARG_MACH		EM_MIPS

/* Information taken from MIPS ABI supplement */

/* Architecture dependent Segment types - p_type */
#define PT_MIPS_REGINFO 0x70000000      /* Register usage information */

/* Architecture dependent d_tag field for Elf32_Dyn.  */
#define DT_MIPS_RLD_VERSION  0x70000001 /* Runtime Linker Interface ID */
#define DT_MIPS_TIME_STAMP   0x70000002 /* Timestamp */
#define DT_MIPS_ICHECKSUM    0x70000003 /* Cksum of ext. str. and com. sizes */
#define DT_MIPS_IVERSION     0x70000004 /* Version string (string tbl index) */
#define DT_MIPS_FLAGS        0x70000005 /* Flags */
#define DT_MIPS_BASE_ADDRESS 0x70000006 /* Segment base address */
#define DT_MIPS_CONFLICT     0x70000008 /* Adr of .conflict section */
#define DT_MIPS_LIBLIST      0x70000009 /* Address of .liblist section */
#define DT_MIPS_LOCAL_GOTNO  0x7000000a /* Number of local .GOT entries */
#define DT_MIPS_CONFLICTNO   0x7000000b /* Number of .conflict entries */
#define DT_MIPS_LIBLISTNO    0x70000010 /* Number of .liblist entries */
#define DT_MIPS_SYMTABNO     0x70000011 /* Number of .dynsym entries */
#define DT_MIPS_UNREFEXTNO   0x70000012 /* First external DYNSYM */
#define DT_MIPS_GOTSYM       0x70000013 /* First GOT entry in .dynsym */
#define DT_MIPS_HIPAGENO     0x70000014 /* Number of GOT page table entries */
#define DT_MIPS_RLD_MAP      0x70000016 /* Address of debug map pointer */
#define DT_MIPS_RLD_MAP_REL  0x70000035 /* Relative address of debug map ptr */

#define DT_PROCNUM (DT_MIPS_RLD_MAP_REL - DT_LOPROC + 1)

/*
 * Legal values for e_flags field of Elf32_Ehdr.
 */
#define EF_MIPS_NOREORDER	0x00000001	/* .noreorder was used */
#define EF_MIPS_PIC		0x00000002	/* Contains PIC code */
#define EF_MIPS_CPIC		0x00000004	/* Uses PIC calling sequence */
#define	EF_MIPS_ABI2		0x00000020	/* -n32 on Irix 6 */
#define	EF_MIPS_32BITMODE	0x00000100	/* 64 bit in 32 bit mode... */
#define EF_MIPS_ARCH		0xf0000000	/* MIPS architecture level */
#define	E_MIPS_ARCH_1		0x00000000
#define	E_MIPS_ARCH_2		0x10000000
#define	E_MIPS_ARCH_3		0x20000000
#define	E_MIPS_ARCH_4		0x30000000
#define	EF_MIPS_ABI		0x0000f000	/* ABI level */
#define	E_MIPS_ABI_NONE		0x00000000	/* ABI level not set */
#define	E_MIPS_ABI_O32		0x00001000
#define	E_MIPS_ABI_O64		0x00002000
#define	E_MIPS_ABI_EABI32	0x00004000
#define	E_MIPS_ABI_EABI64	0x00004000

/*
 * Mips special sections.
 */
#define	SHN_MIPS_ACOMMON	0xff00		/* Allocated common symbols */
#define	SHN_MIPS_SCOMMON	0xff03		/* Small common symbols */
#define	SHN_MIPS_SUNDEFINED	0xff04		/* Small undefined symbols */

/*
 * Legal values for sh_type field of Elf32_Shdr.
 */
#define	SHT_MIPS_LIBLIST  0x70000000	/* Shared objects used in link */
#define	SHT_MIPS_CONFLICT 0x70000002	/* Conflicting symbols */
#define	SHT_MIPS_GPTAB    0x70000003	/* Global data area sizes */
#define	SHT_MIPS_UCODE    0x70000004	/* Reserved for SGI/MIPS compilers */
#define	SHT_MIPS_DEBUG    0x70000005	/* MIPS ECOFF debugging information */
#define	SHT_MIPS_REGINFO  0x70000006	/* Register usage information */

/*
 * Legal values for sh_flags field of Elf32_Shdr.
 */
#define	SHF_MIPS_GPREL	0x10000000	/* Must be part of global data area */

#if 0
/*
 * Entries found in sections of type SHT_MIPS_GPTAB.
 */
typedef union {
	struct {
		Elf32_Word gt_current_g_value;	/* -G val used in compilation */
		Elf32_Word gt_unused;	/* Not used */
	} gt_header;			/* First entry in section */
	struct {
		Elf32_Word gt_g_value;	/* If this val were used for -G */
		Elf32_Word gt_bytes;	/* This many bytes would be used */
	} gt_entry;			/* Subsequent entries in section */
} Elf32_gptab;

/*
 * Entry found in sections of type SHT_MIPS_REGINFO.
 */
typedef struct {
	Elf32_Word	ri_gprmask;	/* General registers used */
	Elf32_Word	ri_cprmask[4];	/* Coprocessor registers used */
	Elf32_Sword	ri_gp_value;	/* $gp register value */
} Elf32_RegInfo;
#endif

#endif	/* !_MIPS64_EXEC_H_ */
