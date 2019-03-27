/* $FreeBSD$ */
/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008 Hans Petter Selasky. All rights reserved.
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
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _USB_BUSDMA_H_
#define	_USB_BUSDMA_H_

#ifndef USB_GLOBAL_INCLUDE_FILE
#include <sys/uio.h>
#include <sys/mbuf.h>

#include <machine/bus.h>
#endif

/* defines */

#define	USB_PAGE_SIZE PAGE_SIZE		/* use system PAGE_SIZE */

#if (__FreeBSD_version >= 700020)
#define	USB_GET_DMA_TAG(dev) bus_get_dma_tag(dev)
#else
#define	USB_GET_DMA_TAG(dev) NULL	/* XXX */
#endif

/* structure prototypes */

struct usb_xfer_root;
struct usb_dma_parent_tag;
struct usb_dma_tag;

/*
 * The following typedef defines the USB DMA load done callback.
 */

typedef void (usb_dma_callback_t)(struct usb_dma_parent_tag *udpt);

/*
 * The following structure defines physical and non kernel virtual
 * address of a memory page having size USB_PAGE_SIZE.
 */
struct usb_page {
#if USB_HAVE_BUSDMA
	bus_addr_t physaddr;
	void   *buffer;			/* non Kernel Virtual Address */
#endif
};

/*
 * The following structure is used when needing the kernel virtual
 * pointer and the physical address belonging to an offset in an USB
 * page cache.
 */
struct usb_page_search {
	void   *buffer;
#if USB_HAVE_BUSDMA
	bus_addr_t physaddr;
#endif
	usb_size_t length;
};

/*
 * The following structure is used to keep information about a DMA
 * memory allocation.
 */
struct usb_page_cache {

#if USB_HAVE_BUSDMA
	bus_dma_tag_t tag;
	bus_dmamap_t map;
	struct usb_page *page_start;
#endif
	struct usb_dma_parent_tag *tag_parent;	/* always set */
	void   *buffer;			/* virtual buffer pointer */
#if USB_HAVE_BUSDMA
	usb_size_t page_offset_buf;
	usb_size_t page_offset_end;
	uint8_t	isread:1;		/* set if we are currently reading
					 * from the memory. Else write. */
	uint8_t	ismultiseg:1;		/* set if we can have multiple
					 * segments */
#endif
};

/*
 * The following structure describes the parent USB DMA tag.
 */
#if USB_HAVE_BUSDMA
struct usb_dma_parent_tag {
	struct cv cv[1];		/* internal condition variable */
	bus_dma_tag_t tag;		/* always set */

	struct mtx *mtx;		/* private mutex, always set */
	usb_dma_callback_t *func;	/* load complete callback function */
	struct usb_dma_tag *utag_first;/* pointer to first USB DMA tag */
	uint8_t	dma_error;		/* set if DMA load operation failed */
	uint8_t	dma_bits;		/* number of DMA address lines */
	uint8_t	utag_max;		/* number of USB DMA tags */
};
#else
struct usb_dma_parent_tag {};		/* empty struct */
#endif

/*
 * The following structure describes an USB DMA tag.
 */
#if USB_HAVE_BUSDMA
struct usb_dma_tag {
	struct usb_dma_parent_tag *tag_parent;
	bus_dma_tag_t tag;
	usb_size_t align;
	usb_size_t size;
};
#else
struct usb_dma_tag {};			/* empty struct */
#endif

/* function prototypes */

int	usb_uiomove(struct usb_page_cache *pc, struct uio *uio,
	    usb_frlength_t pc_offset, usb_frlength_t len);
struct usb_dma_tag *usb_dma_tag_find(struct usb_dma_parent_tag *udpt,
	    usb_size_t size, usb_size_t align);
uint8_t	usb_pc_alloc_mem(struct usb_page_cache *pc, struct usb_page *pg,
	    usb_size_t size, usb_size_t align);
uint8_t	usb_pc_dmamap_create(struct usb_page_cache *pc, usb_size_t size);
uint8_t	usb_pc_load_mem(struct usb_page_cache *pc, usb_size_t size,
	    uint8_t sync);
void	usb_bdma_done_event(struct usb_dma_parent_tag *udpt);
void	usb_bdma_post_sync(struct usb_xfer *xfer);
void	usb_bdma_pre_sync(struct usb_xfer *xfer);
void	usb_bdma_work_loop(struct usb_xfer_queue *pq);
void	usb_dma_tag_setup(struct usb_dma_parent_tag *udpt,
	    struct usb_dma_tag *udt, bus_dma_tag_t dmat, struct mtx *mtx,
	    usb_dma_callback_t *func, uint8_t ndmabits, uint8_t nudt);
void	usb_dma_tag_unsetup(struct usb_dma_parent_tag *udpt);
void	usb_pc_cpu_flush(struct usb_page_cache *pc);
void	usb_pc_cpu_invalidate(struct usb_page_cache *pc);
void	usb_pc_dmamap_destroy(struct usb_page_cache *pc);
void	usb_pc_free_mem(struct usb_page_cache *pc);
uint8_t	usb_pc_buffer_is_aligned(struct usb_page_cache *pc,
	    usb_frlength_t offset, usb_frlength_t len,
	    usb_frlength_t mask);

#endif					/* _USB_BUSDMA_H_ */
