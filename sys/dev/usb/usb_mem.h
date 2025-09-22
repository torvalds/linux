/*	$OpenBSD: usb_mem.h,v 1.18 2024/10/08 19:42:31 kettenis Exp $ */
/*	$NetBSD: usb_mem.h,v 1.20 2003/05/03 18:11:42 wiz Exp $	*/
/*	$FreeBSD: src/sys/dev/usb/usb_mem.h,v 1.9 1999/11/17 22:33:47 n_hibma Exp $	*/

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

struct usb_frag_dma;

struct usb_dma_block {
	bus_dma_tag_t tag;
	bus_dmamap_t map;
        caddr_t kaddr;
        bus_dma_segment_t segs[1];
        int nsegs;
        int flags;
        size_t size;
        size_t align;
	struct usb_frag_dma *frags;
	LIST_ENTRY(usb_dma_block) next;
};

#define DMAADDR(dma, o) ((dma)->block->map->dm_segs[0].ds_addr + (dma)->offs + (o))
#define KERNADDR(dma, o) \
	((void *)((char *)((dma)->block->kaddr + (dma)->offs) + (o)))

usbd_status	usb_allocmem(struct usbd_bus *, size_t, size_t, int,
		    struct usb_dma *);
void		usb_freemem(struct usbd_bus *, struct usb_dma *);
void		usb_syncmem(struct usb_dma *, bus_addr_t, bus_size_t, int);
