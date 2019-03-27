/**
 * Copyright (c) 2010-2012 Broadcom. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The names of the above-listed copyright holders may not be used
 *    to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2, as published by the Free
 * Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
 * IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <interface/compat/vchi_bsd.h>

#include <sys/malloc.h>
#include <sys/rwlock.h>

#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/vm_extern.h>
#include <vm/vm_kern.h>
#include <vm/vm_map.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/vm_pager.h>
#include <vm/vm_param.h>

#include <machine/bus.h>
#include <machine/cpu.h>
#include <arm/broadcom/bcm2835/bcm2835_mbox.h>
#include <arm/broadcom/bcm2835/bcm2835_vcbus.h>

MALLOC_DEFINE(M_VCPAGELIST, "vcpagelist", "VideoCore pagelist memory");

#define TOTAL_SLOTS (VCHIQ_SLOT_ZERO_SLOTS + 2 * 32)

#define VCHIQ_DOORBELL_IRQ IRQ_ARM_DOORBELL_0
#define VCHIQ_ARM_ADDRESS(x) ((void *)PHYS_TO_VCBUS(pmap_kextract((vm_offset_t)(x))))

#include "vchiq_arm.h"
#include "vchiq_2835.h"
#include "vchiq_connected.h"
#include "vchiq_killable.h"

#define MAX_FRAGMENTS (VCHIQ_NUM_CURRENT_BULKS * 2)

int g_cache_line_size = 32;
static int g_fragment_size;

typedef struct vchiq_2835_state_struct {
   int inited;
   VCHIQ_ARM_STATE_T arm_state;
} VCHIQ_2835_ARM_STATE_T;

static char *g_slot_mem;
static int g_slot_mem_size;
vm_paddr_t g_slot_phys;
/* BSD DMA */
bus_dma_tag_t bcm_slots_dma_tag;
bus_dmamap_t bcm_slots_dma_map;

static char *g_fragments_base;
static char *g_free_fragments;
struct semaphore g_free_fragments_sema;

static DEFINE_SEMAPHORE(g_free_fragments_mutex);

typedef struct bulkinfo_struct {
	PAGELIST_T	*pagelist;
	bus_dma_tag_t	pagelist_dma_tag;
	bus_dmamap_t	pagelist_dma_map;
	void		*buf;
	size_t		size;
} BULKINFO_T;

static int
create_pagelist(char __user *buf, size_t count, unsigned short type,
                struct proc *p, BULKINFO_T *bi);

static void
free_pagelist(BULKINFO_T *bi, int actual);

static void
vchiq_dmamap_cb(void *arg, bus_dma_segment_t *segs, int nseg, int err)
{
	bus_addr_t *addr;

	if (err)
		return;

	addr = (bus_addr_t*)arg;
	*addr = PHYS_TO_VCBUS(segs[0].ds_addr);
}

static int
copyout_page(vm_page_t p, size_t offset, void *kaddr, size_t size)
{
        uint8_t *dst;

        dst = (uint8_t*)pmap_quick_enter_page(p);
        if (!dst)
                return ENOMEM;

        memcpy(dst + offset, kaddr, size);

        pmap_quick_remove_page((vm_offset_t)dst);

        return 0;
}

int __init
vchiq_platform_init(VCHIQ_STATE_T *state)
{
	VCHIQ_SLOT_ZERO_T *vchiq_slot_zero;
	int frag_mem_size;
	int err;
	int i;

	/* Allocate space for the channels in coherent memory */
	g_slot_mem_size = PAGE_ALIGN(TOTAL_SLOTS * VCHIQ_SLOT_SIZE);
	g_fragment_size = 2*g_cache_line_size;
	frag_mem_size = PAGE_ALIGN(g_fragment_size * MAX_FRAGMENTS);

	err = bus_dma_tag_create(
	    NULL,
	    PAGE_SIZE, 0,	       /* alignment, boundary */
	    BUS_SPACE_MAXADDR_32BIT,    /* lowaddr */
	    BUS_SPACE_MAXADDR,	  /* highaddr */
	    NULL, NULL,		 /* filter, filterarg */
	    g_slot_mem_size + frag_mem_size, 1,		/* maxsize, nsegments */
	    g_slot_mem_size + frag_mem_size, 0,		/* maxsegsize, flags */
	    NULL, NULL,		 /* lockfunc, lockarg */
	    &bcm_slots_dma_tag);

	err = bus_dmamem_alloc(bcm_slots_dma_tag, (void **)&g_slot_mem,
	    BUS_DMA_COHERENT | BUS_DMA_WAITOK, &bcm_slots_dma_map);
	if (err) {
		vchiq_log_error(vchiq_core_log_level, "Unable to allocate channel memory");
		err = -ENOMEM;
		goto failed_alloc;
	}

	err = bus_dmamap_load(bcm_slots_dma_tag, bcm_slots_dma_map, g_slot_mem,
	    g_slot_mem_size + frag_mem_size, vchiq_dmamap_cb,
	    &g_slot_phys, 0);

	if (err) {
		vchiq_log_error(vchiq_core_log_level, "cannot load DMA map");
		err = -ENOMEM;
		goto failed_load;
	}

	WARN_ON(((int)g_slot_mem & (PAGE_SIZE - 1)) != 0);

	vchiq_slot_zero = vchiq_init_slots(g_slot_mem, g_slot_mem_size);
	if (!vchiq_slot_zero) {
		err = -EINVAL;
		goto failed_init_slots;
	}

	vchiq_slot_zero->platform_data[VCHIQ_PLATFORM_FRAGMENTS_OFFSET_IDX] =
		(int)g_slot_phys + g_slot_mem_size;
	vchiq_slot_zero->platform_data[VCHIQ_PLATFORM_FRAGMENTS_COUNT_IDX] =
		MAX_FRAGMENTS;

	g_fragments_base = (char *)(g_slot_mem + g_slot_mem_size);
	g_slot_mem_size += frag_mem_size;

	g_free_fragments = g_fragments_base;
	for (i = 0; i < (MAX_FRAGMENTS - 1); i++) {
		*(char **)&g_fragments_base[i*g_fragment_size] =
			&g_fragments_base[(i + 1)*g_fragment_size];
	}
	*(char **)&g_fragments_base[i*g_fragment_size] = NULL;
	_sema_init(&g_free_fragments_sema, MAX_FRAGMENTS);

	if (vchiq_init_state(state, vchiq_slot_zero, 0/*slave*/) !=
		VCHIQ_SUCCESS) {
		err = -EINVAL;
		goto failed_vchiq_init;
	}

	bcm_mbox_write(BCM2835_MBOX_CHAN_VCHIQ, (unsigned int)g_slot_phys);

	vchiq_log_info(vchiq_arm_log_level,
		"vchiq_init - done (slots %x, phys %x)",
		(unsigned int)vchiq_slot_zero, g_slot_phys);

   vchiq_call_connected_callbacks();

   return 0;

failed_vchiq_init:
failed_init_slots:
	bus_dmamap_unload(bcm_slots_dma_tag, bcm_slots_dma_map);
failed_load:
	bus_dmamem_free(bcm_slots_dma_tag, g_slot_mem, bcm_slots_dma_map);
failed_alloc:
	bus_dma_tag_destroy(bcm_slots_dma_tag);

   return err;
}

void __exit
vchiq_platform_exit(VCHIQ_STATE_T *state)
{

	bus_dmamap_unload(bcm_slots_dma_tag, bcm_slots_dma_map);
	bus_dmamem_free(bcm_slots_dma_tag, g_slot_mem, bcm_slots_dma_map);
	bus_dma_tag_destroy(bcm_slots_dma_tag);
}

VCHIQ_STATUS_T
vchiq_platform_init_state(VCHIQ_STATE_T *state)
{
   VCHIQ_STATUS_T status = VCHIQ_SUCCESS;
   state->platform_state = kzalloc(sizeof(VCHIQ_2835_ARM_STATE_T), GFP_KERNEL);
   ((VCHIQ_2835_ARM_STATE_T*)state->platform_state)->inited = 1;
   status = vchiq_arm_init_state(state, &((VCHIQ_2835_ARM_STATE_T*)state->platform_state)->arm_state);
   if(status != VCHIQ_SUCCESS)
   {
      ((VCHIQ_2835_ARM_STATE_T*)state->platform_state)->inited = 0;
   }
   return status;
}

VCHIQ_ARM_STATE_T*
vchiq_platform_get_arm_state(VCHIQ_STATE_T *state)
{
   if(!((VCHIQ_2835_ARM_STATE_T*)state->platform_state)->inited)
   {
      BUG();
   }
   return &((VCHIQ_2835_ARM_STATE_T*)state->platform_state)->arm_state;
}

int
vchiq_copy_from_user(void *dst, const void *src, int size)
{

	if (((vm_offset_t)(src)) < VM_MIN_KERNEL_ADDRESS) {
		int error = copyin(src, dst, size);
		return error ? VCHIQ_ERROR : VCHIQ_SUCCESS;
	}
	else
		bcopy(src, dst, size);

	return 0;
}

VCHIQ_STATUS_T
vchiq_prepare_bulk_data(VCHIQ_BULK_T *bulk, VCHI_MEM_HANDLE_T memhandle,
	void *offset, int size, int dir)
{
	BULKINFO_T *bi;
	int ret;

	WARN_ON(memhandle != VCHI_MEM_HANDLE_INVALID);
	bi = malloc(sizeof(*bi), M_VCPAGELIST, M_WAITOK | M_ZERO);
	if (bi == NULL)
		return VCHIQ_ERROR;

	ret = create_pagelist((char __user *)offset, size,
			(dir == VCHIQ_BULK_RECEIVE)
			? PAGELIST_READ
			: PAGELIST_WRITE,
			current,
			bi);
	if (ret != 0)
		return VCHIQ_ERROR;

	bulk->handle = memhandle;
	bulk->data = VCHIQ_ARM_ADDRESS(bi->pagelist);

	/* Store the pagelist address in remote_data, which isn't used by the
	   slave. */
	bulk->remote_data = bi;

	return VCHIQ_SUCCESS;
}

void
vchiq_complete_bulk(VCHIQ_BULK_T *bulk)
{
	if (bulk && bulk->remote_data && bulk->actual)
		free_pagelist((BULKINFO_T *)bulk->remote_data, bulk->actual);
}

void
vchiq_transfer_bulk(VCHIQ_BULK_T *bulk)
{
	/*
	 * This should only be called on the master (VideoCore) side, but
	 * provide an implementation to avoid the need for ifdefery.
	 */
	BUG();
}

void
vchiq_dump_platform_state(void *dump_context)
{
	char buf[80];
	int len;
	len = snprintf(buf, sizeof(buf),
		"  Platform: 2835 (VC master)");
	vchiq_dump(dump_context, buf, len + 1);
}

VCHIQ_STATUS_T
vchiq_platform_suspend(VCHIQ_STATE_T *state)
{
   return VCHIQ_ERROR;
}

VCHIQ_STATUS_T
vchiq_platform_resume(VCHIQ_STATE_T *state)
{
   return VCHIQ_SUCCESS;
}

void
vchiq_platform_paused(VCHIQ_STATE_T *state)
{
}

void
vchiq_platform_resumed(VCHIQ_STATE_T *state)
{
}

int
vchiq_platform_videocore_wanted(VCHIQ_STATE_T* state)
{
   return 1; // autosuspend not supported - videocore always wanted
}

int
vchiq_platform_use_suspend_timer(void)
{
   return 0;
}
void
vchiq_dump_platform_use_state(VCHIQ_STATE_T *state)
{
	vchiq_log_info(vchiq_arm_log_level, "Suspend timer not in use");
}
void
vchiq_platform_handle_timeout(VCHIQ_STATE_T *state)
{
	(void)state;
}
/*
 * Local functions
 */

static void
pagelist_page_free(vm_page_t pp)
{
	vm_page_lock(pp);
	if (vm_page_unwire(pp, PQ_INACTIVE) && pp->object == NULL)
		vm_page_free(pp);
	vm_page_unlock(pp);
}

/* There is a potential problem with partial cache lines (pages?)
** at the ends of the block when reading. If the CPU accessed anything in
** the same line (page?) then it may have pulled old data into the cache,
** obscuring the new data underneath. We can solve this by transferring the
** partial cache lines separately, and allowing the ARM to copy into the
** cached area.

** N.B. This implementation plays slightly fast and loose with the Linux
** driver programming rules, e.g. its use of __virt_to_bus instead of
** dma_map_single, but it isn't a multi-platform driver and it benefits
** from increased speed as a result.
*/

static int
create_pagelist(char __user *buf, size_t count, unsigned short type,
	struct proc *p, BULKINFO_T *bi)
{
	PAGELIST_T *pagelist;
	vm_page_t* pages;
	unsigned long *addrs;
	unsigned int num_pages, i;
	vm_offset_t offset;
	int pagelist_size;
	char *addr, *base_addr, *next_addr;
	int run, addridx, actual_pages;
	int err;
	vm_paddr_t pagelist_phys;
	vm_paddr_t pa;

	offset = (vm_offset_t)buf & (PAGE_SIZE - 1);
	num_pages = (count + offset + PAGE_SIZE - 1) / PAGE_SIZE;

	bi->pagelist = NULL;
	bi->buf = buf;
	bi->size = count;

	/* Allocate enough storage to hold the page pointers and the page
	** list
	*/
	pagelist_size = sizeof(PAGELIST_T) +
		(num_pages * sizeof(unsigned long)) +
		(num_pages * sizeof(pages[0]));

	err = bus_dma_tag_create(
	    NULL,
	    PAGE_SIZE, 0,	       /* alignment, boundary */
	    BUS_SPACE_MAXADDR_32BIT,    /* lowaddr */
	    BUS_SPACE_MAXADDR,	  /* highaddr */
	    NULL, NULL,		 /* filter, filterarg */
	    pagelist_size, 1,		/* maxsize, nsegments */
	    pagelist_size, 0,		/* maxsegsize, flags */
	    NULL, NULL,		 /* lockfunc, lockarg */
	    &bi->pagelist_dma_tag);

	err = bus_dmamem_alloc(bi->pagelist_dma_tag, (void **)&pagelist,
	    BUS_DMA_COHERENT | BUS_DMA_WAITOK, &bi->pagelist_dma_map);
	if (err) {
		vchiq_log_error(vchiq_core_log_level, "Unable to allocate pagelist memory");
		err = -ENOMEM;
		goto failed_alloc;
	}

	err = bus_dmamap_load(bi->pagelist_dma_tag, bi->pagelist_dma_map, pagelist,
	    pagelist_size, vchiq_dmamap_cb,
	    &pagelist_phys, 0);

	if (err) {
		vchiq_log_error(vchiq_core_log_level, "cannot load DMA map for pagelist memory");
		err = -ENOMEM;
		goto failed_load;
	}

	vchiq_log_trace(vchiq_arm_log_level,
		"create_pagelist - %x (%d bytes @%p)", (unsigned int)pagelist, count, buf);

	if (!pagelist)
		return -ENOMEM;

	addrs = pagelist->addrs;
	pages = (vm_page_t*)(addrs + num_pages);

	actual_pages = vm_fault_quick_hold_pages(&p->p_vmspace->vm_map,
	    (vm_offset_t)buf, count,
	    (type == PAGELIST_READ ? VM_PROT_WRITE : 0 ) | VM_PROT_READ, pages, num_pages);

	if (actual_pages != num_pages) {
		vm_page_unhold_pages(pages, actual_pages);
		free(pagelist, M_VCPAGELIST);
		return (-ENOMEM);
	}

	for (i = 0; i < actual_pages; i++) {
		vm_page_lock(pages[i]);
		vm_page_wire(pages[i]);
		vm_page_unhold(pages[i]);
		vm_page_unlock(pages[i]);
	}

	pagelist->length = count;
	pagelist->type = type;
	pagelist->offset = offset;

	/* Group the pages into runs of contiguous pages */

	base_addr = (void *)PHYS_TO_VCBUS(VM_PAGE_TO_PHYS(pages[0]));
	next_addr = base_addr + PAGE_SIZE;
	addridx = 0;
	run = 0;

	for (i = 1; i < num_pages; i++) {
		addr = (void *)PHYS_TO_VCBUS(VM_PAGE_TO_PHYS(pages[i]));
		if ((addr == next_addr) && (run < (PAGE_SIZE - 1))) {
			next_addr += PAGE_SIZE;
			run++;
		} else {
			addrs[addridx] = (unsigned long)base_addr + run;
			addridx++;
			base_addr = addr;
			next_addr = addr + PAGE_SIZE;
			run = 0;
		}
	}

	addrs[addridx] = (unsigned long)base_addr + run;
	addridx++;

	/* Partial cache lines (fragments) require special measures */
	if ((type == PAGELIST_READ) &&
		((pagelist->offset & (g_cache_line_size - 1)) ||
		((pagelist->offset + pagelist->length) &
		(g_cache_line_size - 1)))) {
		char *fragments;

		if (down_interruptible(&g_free_fragments_sema) != 0) {
      			free(pagelist, M_VCPAGELIST);
			return -EINTR;
		}

		WARN_ON(g_free_fragments == NULL);

		down(&g_free_fragments_mutex);
		fragments = g_free_fragments;
		WARN_ON(fragments == NULL);
		g_free_fragments = *(char **) g_free_fragments;
		up(&g_free_fragments_mutex);
		pagelist->type =
			 PAGELIST_READ_WITH_FRAGMENTS + 
			 (fragments - g_fragments_base)/g_fragment_size;
	}

	pa = pmap_extract(PCPU_GET(curpmap), (vm_offset_t)buf);
	dcache_wbinv_poc((vm_offset_t)buf, pa, count);

	bus_dmamap_sync(bi->pagelist_dma_tag, bi->pagelist_dma_map, BUS_DMASYNC_PREWRITE);

	bi->pagelist = pagelist;

	return 0;

failed_load:
	bus_dmamem_free(bi->pagelist_dma_tag, bi->pagelist, bi->pagelist_dma_map);
failed_alloc:
	bus_dma_tag_destroy(bi->pagelist_dma_tag);

	return err;
}

static void
free_pagelist(BULKINFO_T *bi, int actual)
{
	vm_page_t*pages;
	unsigned int num_pages, i;
	PAGELIST_T *pagelist;

	pagelist = bi->pagelist;

	vchiq_log_trace(vchiq_arm_log_level,
		"free_pagelist - %x, %d (%lu bytes @%p)", (unsigned int)pagelist, actual, pagelist->length, bi->buf);

	num_pages =
		(pagelist->length + pagelist->offset + PAGE_SIZE - 1) /
		PAGE_SIZE;

	pages = (vm_page_t*)(pagelist->addrs + num_pages);

	/* Deal with any partial cache lines (fragments) */
	if (pagelist->type >= PAGELIST_READ_WITH_FRAGMENTS) {
		char *fragments = g_fragments_base +
			(pagelist->type - PAGELIST_READ_WITH_FRAGMENTS)*g_fragment_size;
		int head_bytes, tail_bytes;
		head_bytes = (g_cache_line_size - pagelist->offset) &
			(g_cache_line_size - 1);
		tail_bytes = (pagelist->offset + actual) &
			(g_cache_line_size - 1);

		if ((actual >= 0) && (head_bytes != 0)) {
			if (head_bytes > actual)
				head_bytes = actual;

			copyout_page(pages[0],
				pagelist->offset,
				fragments,
				head_bytes);
		}

		if ((actual >= 0) && (head_bytes < actual) &&
			(tail_bytes != 0)) {

			copyout_page(pages[num_pages-1],
				(((vm_offset_t)bi->buf + actual) % PAGE_SIZE) - tail_bytes,
				fragments + g_cache_line_size,
				tail_bytes);
		}

		down(&g_free_fragments_mutex);
		*(char **) fragments = g_free_fragments;
		g_free_fragments = fragments;
		up(&g_free_fragments_mutex);
		up(&g_free_fragments_sema);
	}

	for (i = 0; i < num_pages; i++) {
		if (pagelist->type != PAGELIST_WRITE) {
			vm_page_dirty(pages[i]);
			pagelist_page_free(pages[i]);
		}
	}

	bus_dmamap_unload(bi->pagelist_dma_tag, bi->pagelist_dma_map);
	bus_dmamem_free(bi->pagelist_dma_tag, bi->pagelist, bi->pagelist_dma_map);
	bus_dma_tag_destroy(bi->pagelist_dma_tag);

	free(bi, M_VCPAGELIST);
}
