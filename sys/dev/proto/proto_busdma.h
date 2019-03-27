/*-
 * Copyright (c) 2015 Marcel Moolenaar
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _DEV_PROTO_BUSDMA_H_
#define _DEV_PROTO_BUSDMA_H_

struct proto_md;

struct proto_tag {
	LIST_ENTRY(proto_tag)	tags;
	struct proto_tag	*parent;
	LIST_ENTRY(proto_tag)	peers;
	LIST_HEAD(,proto_tag)	children;
	LIST_HEAD(,proto_md)	mds;
	bus_addr_t		align;
	bus_addr_t		bndry;
	bus_addr_t		maxaddr;
	bus_size_t		maxsz;
	bus_size_t		maxsegsz;
	u_int			nsegs;
	u_int			datarate;
};

struct proto_md {
	LIST_ENTRY(proto_md)	mds;
	LIST_ENTRY(proto_md)	peers;
	struct proto_tag	*tag;
	void			*virtaddr;
	vm_paddr_t		physaddr;
	bus_dma_tag_t		bd_tag;
	bus_dmamap_t		bd_map;
};

struct proto_busdma {
	LIST_HEAD(,proto_tag)	tags;
	LIST_HEAD(,proto_md)	mds;
	bus_dma_tag_t		bd_roottag;
};

struct proto_busdma *proto_busdma_attach(struct proto_softc *);
int proto_busdma_detach(struct proto_softc *, struct proto_busdma *);

int proto_busdma_cleanup(struct proto_softc *, struct proto_busdma *);

int proto_busdma_ioctl(struct proto_softc *, struct proto_busdma *,
    struct proto_ioc_busdma *, struct thread *);

int proto_busdma_mmap_allowed(struct proto_busdma *, vm_paddr_t);

#endif /* _DEV_PROTO_BUSDMA_H_ */
