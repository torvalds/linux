/* MN10300 ELF constant and register definitions
 *
 * Copyright (C) 2007 Matsushita Electric Industrial Co., Ltd.
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
#ifndef _ASM_ELF_H
#define _ASM_ELF_H

#include <linux/utsname.h>
#include <asm/ptrace.h>
#include <asm/user.h>

/*
 * AM33 relocations
 */
#define R_MN10300_NONE		0	/* No reloc.  */
#define R_MN10300_32		1	/* Direct 32 bit.  */
#define R_MN10300_16		2	/* Direct 16 bit.  */
#define R_MN10300_8		3	/* Direct 8 bit.  */
#define R_MN10300_PCREL32	4	/* PC-relative 32-bit.  */
#define R_MN10300_PCREL16	5	/* PC-relative 16-bit signed.  */
#define R_MN10300_PCREL8	6	/* PC-relative 8-bit signed.  */
#define R_MN10300_24		9	/* Direct 24 bit.  */
#define R_MN10300_RELATIVE	23	/* Adjust by program base.  */

/*
 * ELF register definitions..
 */
typedef unsigned long elf_greg_t;

#define ELF_NGREG (sizeof (struct pt_regs) / sizeof(elf_greg_t))
typedef elf_greg_t elf_gregset_t[ELF_NGREG];

#define ELF_NFPREG 32
typedef float elf_fpreg_t;

typedef struct {
	elf_fpreg_t	fpregs[ELF_NFPREG];
	u_int32_t	fpcr;
} elf_fpregset_t;

extern int dump_fpu(struct pt_regs *, elf_fpregset_t *);

/*
 * This is used to ensure we don't load something for the wrong architecture
 */
#define elf_check_arch(x) \
	(((x)->e_machine == EM_CYGNUS_MN10300) ||	\
	 ((x)->e_machine == EM_MN10300))

/*
 * These are used to set parameters in the core dumps.
 */
#define ELF_CLASS	ELFCLASS32
#define ELF_DATA	ELFDATA2LSB
#define ELF_ARCH	EM_MN10300

/*
 * ELF process initialiser
 */
#define ELF_PLAT_INIT(_r, load_addr)					\
do {									\
	struct pt_regs *_ur = current->thread.uregs;			\
	_ur->a3   = 0;	_ur->a2   = 0;	_ur->d3   = 0;	_ur->d2   = 0;	\
	_ur->mcvf = 0;	_ur->mcrl = 0;	_ur->mcrh = 0;	_ur->mdrq = 0;	\
	_ur->e1   = 0;	_ur->e0   = 0;	_ur->e7   = 0;	_ur->e6   = 0;	\
	_ur->e5   = 0;	_ur->e4   = 0;	_ur->e3   = 0;	_ur->e2   = 0;	\
	_ur->lar  = 0;	_ur->lir  = 0;	_ur->mdr  = 0;			\
	_ur->a1   = 0;	_ur->a0   = 0;	_ur->d1   = 0;	_ur->d0   = 0;	\
} while (0)

#define USE_ELF_CORE_DUMP
#define ELF_EXEC_PAGESIZE	4096

/*
 * This is the location that an ET_DYN program is loaded if exec'ed.  Typical
 * use of this is to invoke "./ld.so someprog" to test out a new version of
 * the loader.  We need to make sure that it is out of the way of the program
 * that it will "exec", and that there is sufficient room for the brk.
 * - must clear the VMALLOC area
 */
#define ELF_ET_DYN_BASE         0x04000000

/*
 * regs is struct pt_regs, pr_reg is elf_gregset_t (which is
 * now struct user_regs, they are different)
 * - ELF_CORE_COPY_REGS has been guessed, and may be wrong
 */
#define ELF_CORE_COPY_REGS(pr_reg, regs)	\
do {						\
	pr_reg[0]	= regs->a3;		\
	pr_reg[1]	= regs->a2;		\
	pr_reg[2]	= regs->d3;		\
	pr_reg[3]	= regs->d2;		\
	pr_reg[4]	= regs->mcvf;		\
	pr_reg[5]	= regs->mcrl;		\
	pr_reg[6]	= regs->mcrh;		\
	pr_reg[7]	= regs->mdrq;		\
	pr_reg[8]	= regs->e1;		\
	pr_reg[9]	= regs->e0;		\
	pr_reg[10]	= regs->e7;		\
	pr_reg[11]	= regs->e6;		\
	pr_reg[12]	= regs->e5;		\
	pr_reg[13]	= regs->e4;		\
	pr_reg[14]	= regs->e3;		\
	pr_reg[15]	= regs->e2;		\
	pr_reg[16]	= regs->sp;		\
	pr_reg[17]	= regs->lar;		\
	pr_reg[18]	= regs->lir;		\
	pr_reg[19]	= regs->mdr;		\
	pr_reg[20]	= regs->a1;		\
	pr_reg[21]	= regs->a0;		\
	pr_reg[22]	= regs->d1;		\
	pr_reg[23]	= regs->d0;		\
	pr_reg[24]	= regs->orig_d0;	\
	pr_reg[25]	= regs->epsw;		\
	pr_reg[26]	= regs->pc;		\
} while (0);

/*
 * This yields a mask that user programs can use to figure out what
 * instruction set this CPU supports.  This could be done in user space,
 * but it's not easy, and we've already done it here.
 */
#define ELF_HWCAP	(0)

/*
 * This yields a string that ld.so will use to load implementation
 * specific libraries for optimization.  This is more specific in
 * intent than poking at uname or /proc/cpuinfo.
 *
 * For the moment, we have only optimizations for the Intel generations,
 * but that could change...
 */
#define ELF_PLATFORM  (NULL)

#ifdef __KERNEL__
#define SET_PERSONALITY(ex, ibcs2) set_personality(PER_LINUX)
#endif

#endif /* _ASM_ELF_H */
