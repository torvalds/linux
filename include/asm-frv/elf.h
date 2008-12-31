/* elf.h: FR-V ELF definitions
 *
 * Copyright (C) 2003 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 * - Derived from include/asm-m68knommu/elf.h
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#ifndef __ASM_ELF_H
#define __ASM_ELF_H

#include <asm/ptrace.h>
#include <asm/user.h>

struct elf32_hdr;

/*
 * ELF header e_flags defines.
 */
#define EF_FRV_GPR_MASK         0x00000003 /* mask for # of gprs */
#define EF_FRV_GPR32		0x00000001 /* Only uses GR on 32-register */
#define EF_FRV_GPR64		0x00000002 /* Only uses GR on 64-register */
#define EF_FRV_FPR_MASK         0x0000000c /* mask for # of fprs */
#define EF_FRV_FPR32		0x00000004 /* Only uses FR on 32-register */
#define EF_FRV_FPR64		0x00000008 /* Only uses FR on 64-register */
#define EF_FRV_FPR_NONE		0x0000000C /* Uses software floating-point */
#define EF_FRV_DWORD_MASK       0x00000030 /* mask for dword support */
#define EF_FRV_DWORD_YES	0x00000010 /* Assumes stack aligned to 8-byte boundaries. */
#define EF_FRV_DWORD_NO		0x00000020 /* Assumes stack aligned to 4-byte boundaries. */
#define EF_FRV_DOUBLE		0x00000040 /* Uses double instructions. */
#define EF_FRV_MEDIA		0x00000080 /* Uses media instructions. */
#define EF_FRV_PIC		0x00000100 /* Uses position independent code. */
#define EF_FRV_NON_PIC_RELOCS	0x00000200 /* Does not use position Independent code. */
#define EF_FRV_MULADD           0x00000400 /* -mmuladd */
#define EF_FRV_BIGPIC           0x00000800 /* -fPIC */
#define EF_FRV_LIBPIC           0x00001000 /* -mlibrary-pic */
#define EF_FRV_G0               0x00002000 /* -G 0, no small data ptr */
#define EF_FRV_NOPACK           0x00004000 /* -mnopack */
#define EF_FRV_FDPIC            0x00008000 /* -mfdpic */
#define EF_FRV_CPU_MASK         0xff000000 /* specific cpu bits */
#define EF_FRV_CPU_GENERIC	0x00000000 /* Set CPU type is FR-V */
#define EF_FRV_CPU_FR500	0x01000000 /* Set CPU type is FR500 */
#define EF_FRV_CPU_FR300	0x02000000 /* Set CPU type is FR300 */
#define EF_FRV_CPU_SIMPLE       0x03000000 /* SIMPLE */
#define EF_FRV_CPU_TOMCAT       0x04000000 /* Tomcat, FR500 prototype */
#define EF_FRV_CPU_FR400	0x05000000 /* Set CPU type is FR400 */
#define EF_FRV_CPU_FR550        0x06000000 /* Set CPU type is FR550 */
#define EF_FRV_CPU_FR405	0x07000000 /* Set CPU type is FR405 */
#define EF_FRV_CPU_FR450	0x08000000 /* Set CPU type is FR450 */

/*
 * FR-V ELF relocation types
 */


/*
 * ELF register definitions..
 */
typedef unsigned long elf_greg_t;

#define ELF_NGREG (sizeof(struct pt_regs) / sizeof(elf_greg_t))
typedef elf_greg_t elf_gregset_t[ELF_NGREG];

typedef struct user_fpmedia_regs elf_fpregset_t;

/*
 * This is used to ensure we don't load something for the wrong architecture.
 */
extern int elf_check_arch(const struct elf32_hdr *hdr);

#define elf_check_fdpic(x) ((x)->e_flags & EF_FRV_FDPIC && !((x)->e_flags & EF_FRV_NON_PIC_RELOCS))
#define elf_check_const_displacement(x) ((x)->e_flags & EF_FRV_PIC)

/*
 * These are used to set parameters in the core dumps.
 */
#define ELF_CLASS	ELFCLASS32
#define ELF_DATA	ELFDATA2MSB
#define ELF_ARCH	EM_FRV

#define ELF_PLAT_INIT(_r)			\
do {						\
	__kernel_frame0_ptr->gr16	= 0;	\
	__kernel_frame0_ptr->gr17	= 0;	\
	__kernel_frame0_ptr->gr18	= 0;	\
	__kernel_frame0_ptr->gr19	= 0;	\
	__kernel_frame0_ptr->gr20	= 0;	\
	__kernel_frame0_ptr->gr21	= 0;	\
	__kernel_frame0_ptr->gr22	= 0;	\
	__kernel_frame0_ptr->gr23	= 0;	\
	__kernel_frame0_ptr->gr24	= 0;	\
	__kernel_frame0_ptr->gr25	= 0;	\
	__kernel_frame0_ptr->gr26	= 0;	\
	__kernel_frame0_ptr->gr27	= 0;	\
	__kernel_frame0_ptr->gr29	= 0;	\
} while(0)

#define ELF_FDPIC_PLAT_INIT(_regs, _exec_map_addr, _interp_map_addr, _dynamic_addr)	\
do {											\
	__kernel_frame0_ptr->gr16	= _exec_map_addr;				\
	__kernel_frame0_ptr->gr17	= _interp_map_addr;				\
	__kernel_frame0_ptr->gr18	= _dynamic_addr;				\
	__kernel_frame0_ptr->gr19	= 0;						\
	__kernel_frame0_ptr->gr20	= 0;						\
	__kernel_frame0_ptr->gr21	= 0;						\
	__kernel_frame0_ptr->gr22	= 0;						\
	__kernel_frame0_ptr->gr23	= 0;						\
	__kernel_frame0_ptr->gr24	= 0;						\
	__kernel_frame0_ptr->gr25	= 0;						\
	__kernel_frame0_ptr->gr26	= 0;						\
	__kernel_frame0_ptr->gr27	= 0;						\
	__kernel_frame0_ptr->gr29	= 0;						\
} while(0)

#define USE_ELF_CORE_DUMP
#define ELF_FDPIC_CORE_EFLAGS	EF_FRV_FDPIC
#define ELF_EXEC_PAGESIZE	16384

/* This is the location that an ET_DYN program is loaded if exec'ed.  Typical
   use of this is to invoke "./ld.so someprog" to test out a new version of
   the loader.  We need to make sure that it is out of the way of the program
   that it will "exec", and that there is sufficient room for the brk.  */

#define ELF_ET_DYN_BASE         0x08000000UL

/* This yields a mask that user programs can use to figure out what
   instruction set this cpu supports.  */

#define ELF_HWCAP	(0)

/* This yields a string that ld.so will use to load implementation
   specific libraries for optimization.  This is more specific in
   intent than poking at uname or /proc/cpuinfo.  */

#define ELF_PLATFORM  (NULL)

#define SET_PERSONALITY(ex) set_personality(PER_LINUX)

#endif
