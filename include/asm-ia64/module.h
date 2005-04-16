#ifndef _ASM_IA64_MODULE_H
#define _ASM_IA64_MODULE_H

/*
 * IA-64-specific support for kernel module loader.
 *
 * Copyright (C) 2003 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 */

struct elf64_shdr;			/* forward declration */

struct mod_arch_specific {
	struct elf64_shdr *core_plt;	/* core PLT section */
	struct elf64_shdr *init_plt;	/* init PLT section */
	struct elf64_shdr *got;		/* global offset table */
	struct elf64_shdr *opd;		/* official procedure descriptors */
	struct elf64_shdr *unwind;	/* unwind-table section */
	unsigned long gp;		/* global-pointer for module */

	void *core_unw_table;		/* core unwind-table cookie returned by unwinder */
	void *init_unw_table;		/* init unwind-table cookie returned by unwinder */
	unsigned int next_got_entry;	/* index of next available got entry */
};

#define Elf_Shdr	Elf64_Shdr
#define Elf_Sym		Elf64_Sym
#define Elf_Ehdr	Elf64_Ehdr

#define MODULE_PROC_FAMILY	"ia64"
#define MODULE_ARCH_VERMAGIC	MODULE_PROC_FAMILY

#define ARCH_SHF_SMALL	SHF_IA_64_SHORT

#endif /* _ASM_IA64_MODULE_H */
