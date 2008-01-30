/*
 * 32-bit compatibility support for ELF format executables and core dumps.
 *
 * Copyright (C) 2007 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License v.2.
 *
 * Red Hat Author: Roland McGrath.
 *
 * This file is used in a 64-bit kernel that wants to support 32-bit ELF.
 * asm/elf.h is responsible for defining the compat_* and COMPAT_* macros
 * used below, with definitions appropriate for 32-bit ABI compatibility.
 *
 * We use macros to rename the ABI types and machine-dependent
 * functions used in binfmt_elf.c to compat versions.
 */

#include <linux/elfcore-compat.h>
#include <linux/time.h>

/*
 * Rename the basic ELF layout types to refer to the 32-bit class of files.
 */
#undef	ELF_CLASS
#define ELF_CLASS	ELFCLASS32

#undef	elfhdr
#undef	elf_phdr
#undef	elf_note
#undef	elf_addr_t
#define elfhdr		elf32_hdr
#define elf_phdr	elf32_phdr
#define elf_note	elf32_note
#define elf_addr_t	Elf32_Addr

/*
 * The machine-dependent core note format types are defined in elfcore-compat.h,
 * which requires asm/elf.h to define compat_elf_gregset_t et al.
 */
#define elf_prstatus	compat_elf_prstatus
#define elf_prpsinfo	compat_elf_prpsinfo

/*
 * Compat version of cputime_to_compat_timeval, perhaps this
 * should be an inline in <linux/compat.h>.
 */
static void cputime_to_compat_timeval(const cputime_t cputime,
				      struct compat_timeval *value)
{
	struct timeval tv;
	cputime_to_timeval(cputime, &tv);
	value->tv_sec = tv.tv_sec;
	value->tv_usec = tv.tv_usec;
}

#undef cputime_to_timeval
#define cputime_to_timeval cputime_to_compat_timeval


/*
 * To use this file, asm/elf.h must define compat_elf_check_arch.
 * The other following macros can be defined if the compat versions
 * differ from the native ones, or omitted when they match.
 */

#undef	ELF_ARCH
#undef	elf_check_arch
#define	elf_check_arch	compat_elf_check_arch

#ifdef	COMPAT_ELF_PLATFORM
#undef	ELF_PLATFORM
#define	ELF_PLATFORM		COMPAT_ELF_PLATFORM
#endif

#ifdef	COMPAT_ELF_HWCAP
#undef	ELF_HWCAP
#define	ELF_HWCAP		COMPAT_ELF_HWCAP
#endif

#ifdef	COMPAT_ARCH_DLINFO
#undef	ARCH_DLINFO
#define	ARCH_DLINFO		COMPAT_ARCH_DLINFO
#endif

#ifdef	COMPAT_ELF_ET_DYN_BASE
#undef	ELF_ET_DYN_BASE
#define	ELF_ET_DYN_BASE		COMPAT_ELF_ET_DYN_BASE
#endif

#ifdef COMPAT_ELF_EXEC_PAGESIZE
#undef	ELF_EXEC_PAGESIZE
#define	ELF_EXEC_PAGESIZE	COMPAT_ELF_EXEC_PAGESIZE
#endif

#ifdef	COMPAT_ELF_PLAT_INIT
#undef	ELF_PLAT_INIT
#define	ELF_PLAT_INIT		COMPAT_ELF_PLAT_INIT
#endif

#ifdef	COMPAT_SET_PERSONALITY
#undef	SET_PERSONALITY
#define	SET_PERSONALITY		COMPAT_SET_PERSONALITY
#endif

#ifdef	compat_start_thread
#undef	start_thread
#define	start_thread		compat_start_thread
#endif

#ifdef	compat_arch_setup_additional_pages
#undef	ARCH_HAS_SETUP_ADDITIONAL_PAGES
#define ARCH_HAS_SETUP_ADDITIONAL_PAGES 1
#undef	arch_setup_additional_pages
#define	arch_setup_additional_pages compat_arch_setup_additional_pages
#endif

/*
 * Rename a few of the symbols that binfmt_elf.c will define.
 * These are all local so the names don't really matter, but it
 * might make some debugging less confusing not to duplicate them.
 */
#define elf_format		compat_elf_format
#define init_elf_binfmt		init_compat_elf_binfmt
#define exit_elf_binfmt		exit_compat_elf_binfmt

/*
 * We share all the actual code with the native (64-bit) version.
 */
#include "binfmt_elf.c"
