/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2018 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Konstantin Belousov <kib@FreeBSD.org>
 * under sponsorship from the FreeBSD Foundation.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _MACHINE_PMAP_BASE_H_
#define	_MACHINE_PMAP_BASE_H_

struct pmap_methods {
	void (*pm_ksetrw)(vm_offset_t);
	void (*pm_remap_lower)(bool);
	void (*pm_remap_lowptdi)(bool);
	void (*pm_align_superpage)(vm_object_t object, vm_ooffset_t offset,
	    vm_offset_t *addr, vm_size_t size);
	vm_offset_t (*pm_quick_enter_page)(vm_page_t m);
	void (*pm_quick_remove_page)(vm_offset_t addr);
	void *(*pm_trm_alloc)(size_t size, int flags);
	void (*pm_trm_free)(void *addr, size_t size);
	vm_offset_t (*pm_get_map_low)(void);
	vm_offset_t (*pm_get_vm_maxuser_address)(void);
	vm_paddr_t (*pm_kextract)(vm_offset_t va);
	vm_paddr_t (*pm_pg_frame)(vm_paddr_t pa);
	void (*pm_sf_buf_map)(struct sf_buf *sf);
	void (*pm_cp_slow0_map)(vm_offset_t kaddr, int plen, vm_page_t *ma);
	u_int (*pm_get_kcr3)(void);
	u_int (*pm_get_cr3)(pmap_t);
	caddr_t (*pm_cmap3)(vm_paddr_t pa, u_int pte_flags);
	void (*pm_basemem_setup)(u_int basemem);
	void (*pm_set_nx)(void);
	void *(*pm_bios16_enter)(void);
	void (*pm_bios16_leave)(void *handle);
	void (*pm_bootstrap)(vm_paddr_t firstaddr);
	boolean_t (*pm_is_valid_memattr)(pmap_t, vm_memattr_t);
	int (*pm_cache_bits)(pmap_t, int, boolean_t);
	bool (*pm_ps_enabled)(pmap_t);
	void (*pm_pinit0)(pmap_t);
	int (*pm_pinit)(pmap_t);
	void (*pm_activate)(struct thread *);
	void (*pm_activate_boot)(pmap_t);
	void (*pm_advise)(pmap_t, vm_offset_t, vm_offset_t, int);
	void (*pm_clear_modify)(vm_page_t);
	int (*pm_change_attr)(vm_offset_t, vm_size_t, int);
	int (*pm_mincore)(pmap_t, vm_offset_t, vm_paddr_t *);
	void (*pm_copy)(pmap_t, pmap_t, vm_offset_t, vm_size_t, vm_offset_t);
	void (*pm_copy_page)(vm_page_t, vm_page_t);
	void (*pm_copy_pages)(vm_page_t [], vm_offset_t, vm_page_t [],
	    vm_offset_t, int);
	void (*pm_zero_page)(vm_page_t);
	void (*pm_zero_page_area)(vm_page_t, int, int);
	int (*pm_enter)(pmap_t, vm_offset_t, vm_page_t, vm_prot_t, u_int,
	    int8_t);
	void (*pm_enter_object)(pmap_t, vm_offset_t, vm_offset_t,
	    vm_page_t, vm_prot_t);
	void (*pm_enter_quick)(pmap_t, vm_offset_t, vm_page_t, vm_prot_t);
	void *(*pm_kenter_temporary)(vm_paddr_t pa, int);
	void (*pm_object_init_pt)(pmap_t, vm_offset_t, vm_object_t,
	    vm_pindex_t, vm_size_t);
	void (*pm_unwire)(pmap_t, vm_offset_t, vm_offset_t);
	boolean_t (*pm_page_exists_quick)(pmap_t, vm_page_t);
	int (*pm_page_wired_mappings)(vm_page_t);
	boolean_t (*pm_page_is_mapped)(vm_page_t);
	void (*pm_remove_pages)(pmap_t);
	boolean_t (*pm_is_modified)(vm_page_t);
	boolean_t (*pm_is_prefaultable)(pmap_t, vm_offset_t);
	boolean_t (*pm_is_referenced)(vm_page_t);
	void (*pm_remove_write)(vm_page_t);
	int (*pm_ts_referenced)(vm_page_t);
	void *(*pm_mapdev_attr)(vm_paddr_t, vm_size_t, int);
	void (*pm_unmapdev)(vm_offset_t, vm_size_t);
	void (*pm_page_set_memattr)(vm_page_t, vm_memattr_t);
	vm_paddr_t (*pm_extract)(pmap_t, vm_offset_t);
	vm_page_t (*pm_extract_and_hold)(pmap_t, vm_offset_t, vm_prot_t);
	vm_offset_t (*pm_map)(vm_offset_t *, vm_paddr_t, vm_paddr_t, int);
	void (*pm_qenter)(vm_offset_t sva, vm_page_t *, int);
	void (*pm_qremove)(vm_offset_t, int);
	void (*pm_release)(pmap_t);
	void (*pm_protect)(pmap_t, vm_offset_t, vm_offset_t, vm_prot_t);
	void (*pm_remove)(pmap_t, vm_offset_t, vm_offset_t);
	void (*pm_remove_all)(vm_page_t);
	void (*pm_init)(void);
	void (*pm_init_pat)(void);
	void (*pm_growkernel)(vm_offset_t);
	void (*pm_invalidate_page)(pmap_t, vm_offset_t);
	void (*pm_invalidate_range)(pmap_t, vm_offset_t, vm_offset_t);
	void (*pm_invalidate_all)(pmap_t);
	void (*pm_invalidate_cache)(void);
	void (*pm_flush_page)(vm_page_t);
	void (*pm_kenter)(vm_offset_t, vm_paddr_t);
	void (*pm_kremove)(vm_offset_t);
};

void	pmap_cold(void);
void	pmap_pae_cold(void);
void	pmap_nopae_cold(void);

#endif
