/*	$OpenBSD: usb_mem.c,v 1.36 2024/10/08 19:42:31 kettenis Exp $ */
/*	$NetBSD: usb_mem.c,v 1.26 2003/02/01 06:23:40 thorpej Exp $	*/

/*
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Lennart Augustsson (lennart@augustsson.net) at
 * Carlstedt Research & Technology.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * USB DMA memory allocation.
 * We need to allocate a lot of small (many 8 byte, some larger)
 * memory blocks that can be used for DMA.  Using the bus_dma
 * routines directly would incur large overheads in space and time.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/queue.h>
#include <sys/device.h>		/* for usbdivar.h */
#include <machine/bus.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdivar.h>	/* just for struct usb_dma */
#include <dev/usb/usb_mem.h>

#ifdef USB_DEBUG
#define DPRINTF(x)	do { if (usbdebug) printf x; } while (0)
#define DPRINTFN(n,x)	do { if (usbdebug>(n)) printf x; } while (0)
extern int usbdebug;
#else
#define DPRINTF(x)
#define DPRINTFN(n,x)
#endif

#define USB_MEM_SMALL 64
#define USB_MEM_CHUNKS 64
#define USB_MEM_BLOCK (USB_MEM_SMALL * USB_MEM_CHUNKS)

struct usb_frag_dma {
	struct usb_dma_block *block;
	u_int offs;
	LIST_ENTRY(usb_frag_dma) next;
};

usbd_status	usb_block_allocmem(bus_dma_tag_t, size_t, size_t,
		    struct usb_dma_block **, int);
void		usb_block_freemem(struct usb_dma_block *);

LIST_HEAD(, usb_dma_block) usb_blk_freelist =
	LIST_HEAD_INITIALIZER(usb_blk_freelist);
int usb_blk_nfree = 0;
/* XXX should have different free list for different tags (for speed) */
LIST_HEAD(, usb_frag_dma) usb_frag_freelist =
	LIST_HEAD_INITIALIZER(usb_frag_freelist);

usbd_status
usb_block_allocmem(bus_dma_tag_t tag, size_t size, size_t align,
    struct usb_dma_block **dmap, int flags)
{
	int error;
        struct usb_dma_block *p;
	int s;

	DPRINTFN(5, ("%s: size=%lu align=%lu\n", __func__,
		     (u_long)size, (u_long)align));

	s = splusb();
	/* First check the free list. */
	for (p = LIST_FIRST(&usb_blk_freelist); p; p = LIST_NEXT(p, next)) {
		if (p->tag == tag && p->size >= size && p->align >= align &&
		    p->flags == flags) {
			LIST_REMOVE(p, next);
			usb_blk_nfree--;
			splx(s);
			*dmap = p;
			DPRINTFN(6,("%s: free list size=%lu\n", __func__,
				    (u_long)p->size));
			return (USBD_NORMAL_COMPLETION);
		}
	}
	splx(s);

	DPRINTFN(6, ("usb_block_allocmem: no free\n"));
	p = malloc(sizeof *p, M_USB, M_NOWAIT);
	if (p == NULL)
		return (USBD_NOMEM);

	p->tag = tag;
	p->size = size;
	p->align = align;
	p->flags = flags;
	error = bus_dmamem_alloc(tag, p->size, align, 0,
	    p->segs, nitems(p->segs), &p->nsegs,
	    BUS_DMA_NOWAIT | (flags & BUS_DMA_64BIT));
	if (error)
		goto free0;

	error = bus_dmamem_map(tag, p->segs, p->nsegs, p->size,
	    &p->kaddr, BUS_DMA_NOWAIT | (flags & BUS_DMA_COHERENT));
	if (error)
		goto free1;

	error = bus_dmamap_create(tag, p->size, 1, p->size,
	    0, BUS_DMA_NOWAIT | (flags & BUS_DMA_64BIT), &p->map);
	if (error)
		goto unmap;

	error = bus_dmamap_load(tag, p->map, p->kaddr, p->size, NULL,
	    BUS_DMA_NOWAIT);
	if (error)
		goto destroy;

	*dmap = p;
	return (USBD_NORMAL_COMPLETION);

destroy:
	bus_dmamap_destroy(tag, p->map);
unmap:
	bus_dmamem_unmap(tag, p->kaddr, p->size);
free1:
	bus_dmamem_free(tag, p->segs, p->nsegs);
free0:
	free(p, M_USB, sizeof *p);
	return (USBD_NOMEM);
}

#if 0
void
usb_block_real_freemem(struct usb_dma_block *p)
{
	bus_dmamap_unload(p->tag, p->map);
	bus_dmamap_destroy(p->tag, p->map);
	bus_dmamem_unmap(p->tag, p->kaddr, p->size);
	bus_dmamem_free(p->tag, p->segs, p->nsegs);
	free(p, M_USB, sizeof *p);
}
#endif

/*
 * Do not free the memory unconditionally since we might be called
 * from an interrupt context and that is BAD.
 * XXX when should we really free?
 */
void
usb_block_freemem(struct usb_dma_block *p)
{
	int s;

	DPRINTFN(6, ("%s: size=%lu\n", __func__, (u_long)p->size));
	s = splusb();
	LIST_INSERT_HEAD(&usb_blk_freelist, p, next);
	usb_blk_nfree++;
	splx(s);
}

usbd_status
usb_allocmem(struct usbd_bus *bus, size_t size, size_t align, int flags,
    struct usb_dma *p)
{
	bus_dma_tag_t tag = bus->dmatag;
	usbd_status err;
	struct usb_frag_dma *f;
	struct usb_dma_block *b;
	int i;
	int s;

	flags = (flags & USB_DMA_COHERENT) ? BUS_DMA_COHERENT : 0;
	flags |= bus->dmaflags;

	/* If the request is large then just use a full block. */
	if (size > USB_MEM_SMALL || align > USB_MEM_SMALL) {
		DPRINTFN(1, ("%s: large alloc %d\n", __func__, (int)size));
		size = (size + USB_MEM_BLOCK - 1) & ~(USB_MEM_BLOCK - 1);
		err = usb_block_allocmem(tag, size, align, &p->block, flags);
		if (!err) {
			p->block->frags = NULL;
			p->offs = 0;
		}
		return (err);
	}

	s = splusb();
	/* Check for free fragments. */
	for (f = LIST_FIRST(&usb_frag_freelist); f; f = LIST_NEXT(f, next))
		if (f->block->tag == tag && f->block->flags == flags)
			break;
	if (f == NULL) {
		DPRINTFN(1, ("usb_allocmem: adding fragments\n"));
		err = usb_block_allocmem(tag, USB_MEM_BLOCK, USB_MEM_SMALL, &b,
		    flags);
		if (err) {
			splx(s);
			return (err);
		}
		b->frags = mallocarray(USB_MEM_CHUNKS, sizeof(*f), M_USB,
		    M_NOWAIT);
		if (b->frags == NULL) {
			splx(s);
			usb_block_freemem(b);
			return (USBD_NOMEM);
		}
		for (i = 0; i < USB_MEM_CHUNKS; i++) {
			f = &b->frags[i];
			f->block = b;
			f->offs = USB_MEM_SMALL * i;
			LIST_INSERT_HEAD(&usb_frag_freelist, f, next);
		}
		f = LIST_FIRST(&usb_frag_freelist);
	}
	p->block = f->block;
	p->offs = f->offs;
	LIST_REMOVE(f, next);
	splx(s);
	DPRINTFN(5, ("%s: use frag=%p size=%d\n", __func__, f, (int)size));
	return (USBD_NORMAL_COMPLETION);
}

void
usb_freemem(struct usbd_bus *bus, struct usb_dma *p)
{
	struct usb_frag_dma *f;
	int s;

	if (p->block->frags == NULL) {
		DPRINTFN(1, ("usb_freemem: large free\n"));
		usb_block_freemem(p->block);
		return;
	}
	s = splusb();
	f = &p->block->frags[p->offs / USB_MEM_SMALL];
	LIST_INSERT_HEAD(&usb_frag_freelist, f, next);
	splx(s);
	DPRINTFN(5, ("%s: frag=%p block=%p\n", __func__, f, f->block));
}

void
usb_syncmem(struct usb_dma *p, bus_addr_t offset, bus_size_t len, int ops)
{
	bus_dmamap_sync(p->block->tag, p->block->map, p->offs + offset,
	    len, ops);
}
