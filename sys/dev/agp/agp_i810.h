/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Konstantin Belousov under sponsorship from
 * the FreeBSD Foundation.
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

#ifndef AGP_AGP_I810_H
#define	AGP_AGP_I810_H

#include <sys/param.h>
#include <sys/rman.h>
#include <sys/sglist.h>

#include <vm/vm.h>
#include <vm/vm_page.h>

/* Special gtt memory types */
#define AGP_DCACHE_MEMORY	1
#define AGP_PHYS_MEMORY		2

/* New caching attributes for gen6/sandybridge */
#define AGP_USER_CACHED_MEMORY_LLC_MLC (AGP_USER_TYPES + 2)
#define AGP_USER_UNCACHED_MEMORY (AGP_USER_TYPES + 4)

/* flag for GFDT type */
#define AGP_USER_CACHED_MEMORY_GFDT (1 << 3)

struct intel_gtt {
	/* Size of memory reserved for graphics by the BIOS */
	unsigned int stolen_size;
	/* Total number of gtt entries. */
	unsigned int gtt_total_entries;
	/* Part of the gtt that is mappable by the cpu, for those chips where
	 * this is not the full gtt. */
	unsigned int gtt_mappable_entries;
	/* Whether i915 needs to use the dmar apis or not. */
	unsigned int needs_dmar : 1;
	/* Whether we idle the gpu before mapping/unmapping */
	unsigned int do_idle_maps : 1;
	/* Share the scratch page dma with ppgtts. */
	vm_paddr_t scratch_page_dma;
	vm_page_t scratch_page;
	/* for ppgtt PDE access */
	uint32_t *gtt;
	/* needed for ioremap in drm/i915 */
	bus_addr_t gma_bus_addr;
};

struct intel_gtt agp_intel_gtt_get(device_t dev);
int agp_intel_gtt_chipset_flush(device_t dev);
void agp_intel_gtt_unmap_memory(device_t dev, struct sglist *sg_list);
void agp_intel_gtt_clear_range(device_t dev, u_int first_entry,
    u_int num_entries);
int agp_intel_gtt_map_memory(device_t dev, vm_page_t *pages, u_int num_entries,
    struct sglist **sg_list);
void agp_intel_gtt_insert_sg_entries(device_t dev, struct sglist *sg_list,
    u_int pg_start, u_int flags);
void agp_intel_gtt_insert_pages(device_t dev, u_int first_entry,
    u_int num_entries, vm_page_t *pages, u_int flags);

struct intel_gtt *intel_gtt_get(void);
int intel_gtt_chipset_flush(void);
void intel_gtt_unmap_memory(struct sglist *sg_list);
void intel_gtt_clear_range(u_int first_entry, u_int num_entries);
void intel_gtt_install_pte(u_int index, vm_paddr_t addr, u_int flags);
int intel_gtt_map_memory(vm_page_t *pages, u_int num_entries,
    struct sglist **sg_list);
void intel_gtt_insert_sg_entries(struct sglist *sg_list, u_int pg_start,
    u_int flags);
void intel_gtt_insert_pages(u_int first_entry, u_int num_entries,
    vm_page_t *pages, u_int flags);
vm_paddr_t intel_gtt_read_pte_paddr(u_int entry);
u_int32_t intel_gtt_read_pte(u_int entry);
device_t intel_gtt_get_bridge_device(void);
void intel_gtt_write(u_int entry, uint32_t val);

#endif
