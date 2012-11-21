/* FDPIC ELF load map
 *
 * Copyright (C) 2003 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _LINUX_ELF_FDPIC_H
#define _LINUX_ELF_FDPIC_H

#include <uapi/linux/elf-fdpic.h>

/*
 * binfmt binary parameters structure
 */
struct elf_fdpic_params {
	struct elfhdr			hdr;		/* ref copy of ELF header */
	struct elf_phdr			*phdrs;		/* ref copy of PT_PHDR table */
	struct elf32_fdpic_loadmap	*loadmap;	/* loadmap to be passed to userspace */
	unsigned long			elfhdr_addr;	/* mapped ELF header user address */
	unsigned long			ph_addr;	/* mapped PT_PHDR user address */
	unsigned long			map_addr;	/* mapped loadmap user address */
	unsigned long			entry_addr;	/* mapped entry user address */
	unsigned long			stack_size;	/* stack size requested (PT_GNU_STACK) */
	unsigned long			dynamic_addr;	/* mapped PT_DYNAMIC user address */
	unsigned long			load_addr;	/* user address at which to map binary */
	unsigned long			flags;
#define ELF_FDPIC_FLAG_ARRANGEMENT	0x0000000f	/* PT_LOAD arrangement flags */
#define ELF_FDPIC_FLAG_INDEPENDENT	0x00000000	/* PT_LOADs can be put anywhere */
#define ELF_FDPIC_FLAG_HONOURVADDR	0x00000001	/* PT_LOAD.vaddr must be honoured */
#define ELF_FDPIC_FLAG_CONSTDISP	0x00000002	/* PT_LOADs require constant
							 * displacement */
#define ELF_FDPIC_FLAG_CONTIGUOUS	0x00000003	/* PT_LOADs should be contiguous */
#define ELF_FDPIC_FLAG_EXEC_STACK	0x00000010	/* T if stack to be executable */
#define ELF_FDPIC_FLAG_NOEXEC_STACK	0x00000020	/* T if stack not to be executable */
#define ELF_FDPIC_FLAG_EXECUTABLE	0x00000040	/* T if this object is the executable */
#define ELF_FDPIC_FLAG_PRESENT		0x80000000	/* T if this object is present */
};

#ifdef CONFIG_MMU
extern void elf_fdpic_arch_lay_out_mm(struct elf_fdpic_params *exec_params,
				      struct elf_fdpic_params *interp_params,
				      unsigned long *start_stack,
				      unsigned long *start_brk);
#endif

#endif /* _LINUX_ELF_FDPIC_H */
