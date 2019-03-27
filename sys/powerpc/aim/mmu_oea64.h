/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2010 Nathan Whitehorn
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL TOOLS GMBH BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _POWERPC_AIM_MMU_OEA64_H
#define _POWERPC_AIM_MMU_OEA64_H

#include <machine/mmuvar.h>

extern mmu_def_t oea64_mmu;

/*
 * Helper routines
 */

/* Allocate physical memory for use in moea64_bootstrap. */
vm_offset_t	moea64_bootstrap_alloc(vm_size_t size, vm_size_t align);
/* Set an LPTE structure to match the contents of a PVO */
void	moea64_pte_from_pvo(const struct pvo_entry *pvo, struct lpte *lpte);

/*
 * Flags
 */

#define MOEA64_PTE_PROT_UPDATE	1
#define MOEA64_PTE_INVALIDATE	2

/*
 * Bootstrap subroutines
 *
 * An MMU_BOOTSTRAP() implementation looks like this:
 *   moea64_early_bootstrap();
 *   Allocate Page Table
 *   moea64_mid_bootstrap();
 *   Add mappings for MMU resources
 *   moea64_late_bootstrap();
 */

void		moea64_early_bootstrap(mmu_t mmup, vm_offset_t kernelstart,
		    vm_offset_t kernelend);
void		moea64_mid_bootstrap(mmu_t mmup, vm_offset_t kernelstart,
		    vm_offset_t kernelend);
void		moea64_late_bootstrap(mmu_t mmup, vm_offset_t kernelstart,
		    vm_offset_t kernelend);

/*
 * Statistics
 */

extern u_int	moea64_pte_valid;
extern u_int	moea64_pte_overflow;

/*
 * State variables
 */

extern int		moea64_large_page_shift;
extern uint64_t		moea64_large_page_size;
extern u_long		moea64_pteg_count;
extern u_long		moea64_pteg_mask;
extern int		n_slbs;

#endif /* _POWERPC_AIM_MMU_OEA64_H */

