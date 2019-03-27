/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002-2006 Rice University
 * Copyright (c) 2007-2008 Alan L. Cox <alc@cs.rice.edu>
 * All rights reserved.
 *
 * This software was developed for the FreeBSD Project by Alan L. Cox,
 * Olivier Crameri, Peter Druschel, Sitaram Iyer, and Juan Navarro.
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
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY
 * WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/*
 *	Superpage reservation management definitions
 */

#ifndef	_VM_RESERV_H_
#define	_VM_RESERV_H_

#ifdef _KERNEL

#if VM_NRESERVLEVEL > 0

/*
 * The following functions are only to be used by the virtual memory system.
 */
vm_page_t	vm_reserv_alloc_contig(int req, vm_object_t object, vm_pindex_t pindex,
		    int domain, u_long npages, vm_paddr_t low, vm_paddr_t high,
		    u_long alignment, vm_paddr_t boundary, vm_page_t mpred);
vm_page_t	vm_reserv_extend_contig(int req, vm_object_t object,
		    vm_pindex_t pindex, int domain, u_long npages,
		    vm_paddr_t low, vm_paddr_t high, u_long alignment,
		    vm_paddr_t boundary, vm_page_t mpred);
vm_page_t	vm_reserv_alloc_page(int req, vm_object_t object, vm_pindex_t pindex,
		    int domain, vm_page_t mpred);
vm_page_t	vm_reserv_extend(int req, vm_object_t object,
		    vm_pindex_t pindex, int domain, vm_page_t mpred);
void		vm_reserv_break_all(vm_object_t object);
boolean_t	vm_reserv_free_page(vm_page_t m);
void		vm_reserv_init(void);
bool		vm_reserv_is_page_free(vm_page_t m);
int		vm_reserv_level(vm_page_t m);
int		vm_reserv_level_iffullpop(vm_page_t m);
boolean_t	vm_reserv_reclaim_contig(int domain, u_long npages,
		    vm_paddr_t low, vm_paddr_t high, u_long alignment,
		    vm_paddr_t boundary);
boolean_t	vm_reserv_reclaim_inactive(int domain);
void		vm_reserv_rename(vm_page_t m, vm_object_t new_object,
		    vm_object_t old_object, vm_pindex_t old_object_offset);
int		vm_reserv_size(int level);
vm_paddr_t	vm_reserv_startup(vm_offset_t *vaddr, vm_paddr_t end,
		    vm_paddr_t high_water);
vm_page_t	vm_reserv_to_superpage(vm_page_t m);

#endif	/* VM_NRESERVLEVEL > 0 */
#endif	/* _KERNEL */
#endif	/* !_VM_RESERV_H_ */
