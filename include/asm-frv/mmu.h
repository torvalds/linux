/* mmu.h: memory management context for FR-V with or without MMU support
 *
 * Copyright (C) 2004 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#ifndef _ASM_MMU_H
#define _ASM_MMU_H

typedef struct {
#ifdef CONFIG_MMU
	struct list_head id_link;		/* link in list of context ID owners */
	unsigned short	id;			/* MMU context ID */
	unsigned short	id_busy;		/* true if ID is in CXNR */
	unsigned long	itlb_cached_pge;	/* [SCR0] PGE cached for insn TLB handler */
	unsigned long	itlb_ptd_mapping;	/* [DAMR4] PTD mapping for itlb cached PGE */
	unsigned long	dtlb_cached_pge;	/* [SCR1] PGE cached for data TLB handler */
	unsigned long	dtlb_ptd_mapping;	/* [DAMR5] PTD mapping for dtlb cached PGE */

#else
	struct vm_list_struct	*vmlist;
	unsigned long		end_brk;

#endif

#ifdef CONFIG_BINFMT_ELF_FDPIC
	unsigned long	exec_fdpic_loadmap;
	unsigned long	interp_fdpic_loadmap;
#endif

} mm_context_t;

#ifdef CONFIG_MMU
extern int __nongpreldata cxn_pinned;
extern int cxn_pin_by_pid(pid_t pid);
#endif

#endif /* _ASM_MMU_H */
