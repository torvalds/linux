/*-
 * Copyright (c) 2015 Marcel Moolenaar
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
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <machine/bus.h>
#include <machine/bus_dma.h>
#include <machine/resource.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/proc.h>
#include <sys/queue.h>
#include <sys/rman.h>
#include <sys/sbuf.h>
#include <sys/uio.h>
#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>

#include <dev/proto/proto.h>
#include <dev/proto/proto_dev.h>
#include <dev/proto/proto_busdma.h>

MALLOC_DEFINE(M_PROTO_BUSDMA, "proto_busdma", "DMA management data");

#define	BNDRY_MIN(a, b)		\
	(((a) == 0) ? (b) : (((b) == 0) ? (a) : MIN((a), (b))))

struct proto_callback_bundle {
	struct proto_busdma *busdma;
	struct proto_md *md;
	struct proto_ioc_busdma *ioc;
};

static int
proto_busdma_tag_create(struct proto_busdma *busdma, struct proto_tag *parent,
    struct proto_ioc_busdma *ioc)
{
	struct proto_tag *tag;

	/* Make sure that when a boundary is specified, it's a power of 2 */
	if (ioc->u.tag.bndry != 0 &&
	    (ioc->u.tag.bndry & (ioc->u.tag.bndry - 1)) != 0)
		return (EINVAL);

	/*
	 * If nsegs is 1, ignore maxsegsz. What this means is that if we have
	 * just 1 segment, then maxsz should be equal to maxsegsz. To keep it
	 * simple for us, limit maxsegsz to maxsz in any case.
	 */
	if (ioc->u.tag.maxsegsz > ioc->u.tag.maxsz || ioc->u.tag.nsegs == 1)
		ioc->u.tag.maxsegsz = ioc->u.tag.maxsz;

	tag = malloc(sizeof(*tag), M_PROTO_BUSDMA, M_WAITOK | M_ZERO);
	if (parent != NULL) {
		tag->parent = parent;
		LIST_INSERT_HEAD(&parent->children, tag, peers);
		tag->align = MAX(ioc->u.tag.align, parent->align);
		tag->bndry = BNDRY_MIN(ioc->u.tag.bndry, parent->bndry);
		tag->maxaddr = MIN(ioc->u.tag.maxaddr, parent->maxaddr);
		tag->maxsz = MIN(ioc->u.tag.maxsz, parent->maxsz);
		tag->maxsegsz = MIN(ioc->u.tag.maxsegsz, parent->maxsegsz);
		tag->nsegs = MIN(ioc->u.tag.nsegs, parent->nsegs);
		tag->datarate = MIN(ioc->u.tag.datarate, parent->datarate);
		/* Write constraints back */
		ioc->u.tag.align = tag->align;
		ioc->u.tag.bndry = tag->bndry;
		ioc->u.tag.maxaddr = tag->maxaddr;
		ioc->u.tag.maxsz = tag->maxsz;
		ioc->u.tag.maxsegsz = tag->maxsegsz;
		ioc->u.tag.nsegs = tag->nsegs;
		ioc->u.tag.datarate = tag->datarate;
	} else {
		tag->align = ioc->u.tag.align;
		tag->bndry = ioc->u.tag.bndry;
		tag->maxaddr = ioc->u.tag.maxaddr;
		tag->maxsz = ioc->u.tag.maxsz;
		tag->maxsegsz = ioc->u.tag.maxsegsz;
		tag->nsegs = ioc->u.tag.nsegs;
		tag->datarate = ioc->u.tag.datarate;
	}
	LIST_INSERT_HEAD(&busdma->tags, tag, tags);
	ioc->result = (uintptr_t)(void *)tag;
	return (0);
}

static int
proto_busdma_tag_destroy(struct proto_busdma *busdma, struct proto_tag *tag)
{

	if (!LIST_EMPTY(&tag->mds))
		return (EBUSY);
	if (!LIST_EMPTY(&tag->children))
		return (EBUSY);

	if (tag->parent != NULL) {
		LIST_REMOVE(tag, peers);
		tag->parent = NULL;
	}
	LIST_REMOVE(tag, tags);
	free(tag, M_PROTO_BUSDMA);
	return (0);
}

static struct proto_tag *
proto_busdma_tag_lookup(struct proto_busdma *busdma, u_long key)
{
	struct proto_tag *tag;

	LIST_FOREACH(tag, &busdma->tags, tags) {
		if ((void *)tag == (void *)key)
			return (tag);
	}
	return (NULL);
}

static int
proto_busdma_md_destroy_internal(struct proto_busdma *busdma,
    struct proto_md *md)
{

	LIST_REMOVE(md, mds);
	LIST_REMOVE(md, peers);
	if (md->physaddr)
		bus_dmamap_unload(md->bd_tag, md->bd_map);
	if (md->virtaddr != NULL)
		bus_dmamem_free(md->bd_tag, md->virtaddr, md->bd_map);
	else
		bus_dmamap_destroy(md->bd_tag, md->bd_map);
	bus_dma_tag_destroy(md->bd_tag);
	free(md, M_PROTO_BUSDMA);
	return (0);
}

static void
proto_busdma_mem_alloc_callback(void *arg, bus_dma_segment_t *segs, int	nseg,
    int error)
{
	struct proto_callback_bundle *pcb = arg;

	pcb->ioc->u.md.bus_nsegs = nseg;
	pcb->ioc->u.md.bus_addr = segs[0].ds_addr;
}

static int
proto_busdma_mem_alloc(struct proto_busdma *busdma, struct proto_tag *tag,
    struct proto_ioc_busdma *ioc)
{
	struct proto_callback_bundle pcb;
	struct proto_md *md;
	int error;

	md = malloc(sizeof(*md), M_PROTO_BUSDMA, M_WAITOK | M_ZERO);
	md->tag = tag;

	error = bus_dma_tag_create(busdma->bd_roottag, tag->align, tag->bndry,
	    tag->maxaddr, BUS_SPACE_MAXADDR, NULL, NULL, tag->maxsz,
	    tag->nsegs, tag->maxsegsz, 0, NULL, NULL, &md->bd_tag);
	if (error) {
		free(md, M_PROTO_BUSDMA);
		return (error);
	}
	error = bus_dmamem_alloc(md->bd_tag, &md->virtaddr, 0, &md->bd_map);
	if (error) {
		bus_dma_tag_destroy(md->bd_tag);
		free(md, M_PROTO_BUSDMA);
		return (error);
	}
	md->physaddr = pmap_kextract((uintptr_t)(md->virtaddr));
	pcb.busdma = busdma;
	pcb.md = md;
	pcb.ioc = ioc;
	error = bus_dmamap_load(md->bd_tag, md->bd_map, md->virtaddr,
	    tag->maxsz, proto_busdma_mem_alloc_callback, &pcb, BUS_DMA_NOWAIT);
	if (error) {
		bus_dmamem_free(md->bd_tag, md->virtaddr, md->bd_map);
		bus_dma_tag_destroy(md->bd_tag);
		free(md, M_PROTO_BUSDMA);
		return (error);
	}
	LIST_INSERT_HEAD(&tag->mds, md, peers);
	LIST_INSERT_HEAD(&busdma->mds, md, mds);
	ioc->u.md.virt_addr = (uintptr_t)md->virtaddr;
	ioc->u.md.virt_size = tag->maxsz;
	ioc->u.md.phys_nsegs = 1;
	ioc->u.md.phys_addr = md->physaddr;
	ioc->result = (uintptr_t)(void *)md;
	return (0);
}

static int
proto_busdma_mem_free(struct proto_busdma *busdma, struct proto_md *md)
{

	if (md->virtaddr == NULL)
		return (ENXIO);
	return (proto_busdma_md_destroy_internal(busdma, md));
}

static int
proto_busdma_md_create(struct proto_busdma *busdma, struct proto_tag *tag,
    struct proto_ioc_busdma *ioc)
{
	struct proto_md *md;
	int error;

	md = malloc(sizeof(*md), M_PROTO_BUSDMA, M_WAITOK | M_ZERO);
	md->tag = tag;

	error = bus_dma_tag_create(busdma->bd_roottag, tag->align, tag->bndry,
	    tag->maxaddr, BUS_SPACE_MAXADDR, NULL, NULL, tag->maxsz,
	    tag->nsegs, tag->maxsegsz, 0, NULL, NULL, &md->bd_tag);
	if (error) {
		free(md, M_PROTO_BUSDMA);
		return (error);
	}
	error = bus_dmamap_create(md->bd_tag, 0, &md->bd_map);
	if (error) {
		bus_dma_tag_destroy(md->bd_tag);
		free(md, M_PROTO_BUSDMA);
		return (error);
	}

	LIST_INSERT_HEAD(&tag->mds, md, peers);
	LIST_INSERT_HEAD(&busdma->mds, md, mds);
	ioc->result = (uintptr_t)(void *)md;
	return (0);
}

static int
proto_busdma_md_destroy(struct proto_busdma *busdma, struct proto_md *md)
{

	if (md->virtaddr != NULL)
		return (ENXIO);
	return (proto_busdma_md_destroy_internal(busdma, md));
}

static void
proto_busdma_md_load_callback(void *arg, bus_dma_segment_t *segs, int nseg,
    bus_size_t sz, int error)
{
	struct proto_callback_bundle *pcb = arg;
 
	pcb->ioc->u.md.bus_nsegs = nseg;
	pcb->ioc->u.md.bus_addr = segs[0].ds_addr;
}

static int
proto_busdma_md_load(struct proto_busdma *busdma, struct proto_md *md,
    struct proto_ioc_busdma *ioc, struct thread *td)
{
	struct proto_callback_bundle pcb;
	struct iovec iov;
	struct uio uio;
	pmap_t pmap;
	int error;

	iov.iov_base = (void *)(uintptr_t)ioc->u.md.virt_addr;
	iov.iov_len = ioc->u.md.virt_size;
	uio.uio_iov = &iov;
	uio.uio_iovcnt = 1;
	uio.uio_offset = 0;
	uio.uio_resid = iov.iov_len;
	uio.uio_segflg = UIO_USERSPACE;
	uio.uio_rw = UIO_READ;
	uio.uio_td = td;

	pcb.busdma = busdma;
	pcb.md = md;
	pcb.ioc = ioc;
	error = bus_dmamap_load_uio(md->bd_tag, md->bd_map, &uio,
	    proto_busdma_md_load_callback, &pcb, BUS_DMA_NOWAIT);
	if (error)
		return (error);

	/* XXX determine *all* physical memory segments */
	pmap = vmspace_pmap(td->td_proc->p_vmspace);
	md->physaddr = pmap_extract(pmap, ioc->u.md.virt_addr);
	ioc->u.md.phys_nsegs = 1;	/* XXX */
	ioc->u.md.phys_addr = md->physaddr;
	return (0);
}

static int
proto_busdma_md_unload(struct proto_busdma *busdma, struct proto_md *md)
{

	if (!md->physaddr)
		return (ENXIO);
	bus_dmamap_unload(md->bd_tag, md->bd_map);
	md->physaddr = 0;
	return (0);
}

static int
proto_busdma_sync(struct proto_busdma *busdma, struct proto_md *md,
    struct proto_ioc_busdma *ioc)
{
	u_int ops;

	ops = BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE |
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE;
	if (ioc->u.sync.op & ~ops)
		return (EINVAL);
	if (!md->physaddr)
		return (ENXIO);
	bus_dmamap_sync(md->bd_tag, md->bd_map, ioc->u.sync.op);
	return (0);
}

static struct proto_md *
proto_busdma_md_lookup(struct proto_busdma *busdma, u_long key)
{
	struct proto_md *md;

	LIST_FOREACH(md, &busdma->mds, mds) {
		if ((void *)md == (void *)key)
			return (md);
	}
	return (NULL);
}

struct proto_busdma *
proto_busdma_attach(struct proto_softc *sc)
{
	struct proto_busdma *busdma;

	busdma = malloc(sizeof(*busdma), M_PROTO_BUSDMA, M_WAITOK | M_ZERO);
	return (busdma);
}

int
proto_busdma_detach(struct proto_softc *sc, struct proto_busdma *busdma)
{

	proto_busdma_cleanup(sc, busdma);
	free(busdma, M_PROTO_BUSDMA);
	return (0);
}

int
proto_busdma_cleanup(struct proto_softc *sc, struct proto_busdma *busdma)
{
	struct proto_md *md, *md1;
	struct proto_tag *tag, *tag1;

	LIST_FOREACH_SAFE(md, &busdma->mds, mds, md1)
		proto_busdma_md_destroy_internal(busdma, md);
	LIST_FOREACH_SAFE(tag, &busdma->tags, tags, tag1)
		proto_busdma_tag_destroy(busdma, tag);
	return (0);
}

int
proto_busdma_ioctl(struct proto_softc *sc, struct proto_busdma *busdma,
    struct proto_ioc_busdma *ioc, struct thread *td)
{
	struct proto_tag *tag;
	struct proto_md *md;
	int error;

	error = 0;
	switch (ioc->request) {
	case PROTO_IOC_BUSDMA_TAG_CREATE:
		busdma->bd_roottag = bus_get_dma_tag(sc->sc_dev);
		error = proto_busdma_tag_create(busdma, NULL, ioc);
		break;
	case PROTO_IOC_BUSDMA_TAG_DERIVE:
		tag = proto_busdma_tag_lookup(busdma, ioc->key);
		if (tag == NULL) {
			error = EINVAL;
			break;
		}
		error = proto_busdma_tag_create(busdma, tag, ioc);
		break;
	case PROTO_IOC_BUSDMA_TAG_DESTROY:
		tag = proto_busdma_tag_lookup(busdma, ioc->key);
		if (tag == NULL) {
			error = EINVAL;
			break;
		}
		error = proto_busdma_tag_destroy(busdma, tag);
		break;
	case PROTO_IOC_BUSDMA_MEM_ALLOC:
		tag = proto_busdma_tag_lookup(busdma, ioc->u.md.tag);
		if (tag == NULL) {
			error = EINVAL;
			break;
		}
		error = proto_busdma_mem_alloc(busdma, tag, ioc);
		break;
	case PROTO_IOC_BUSDMA_MEM_FREE:
		md = proto_busdma_md_lookup(busdma, ioc->key);
		if (md == NULL) {
			error = EINVAL;
			break;
		}
		error = proto_busdma_mem_free(busdma, md);
		break;
	case PROTO_IOC_BUSDMA_MD_CREATE:
		tag = proto_busdma_tag_lookup(busdma, ioc->u.md.tag);
		if (tag == NULL) {
			error = EINVAL;
			break;
		}
		error = proto_busdma_md_create(busdma, tag, ioc);
		break;
	case PROTO_IOC_BUSDMA_MD_DESTROY:
		md = proto_busdma_md_lookup(busdma, ioc->key);
		if (md == NULL) {
			error = EINVAL;
			break;
		}
		error = proto_busdma_md_destroy(busdma, md);
		break;
	case PROTO_IOC_BUSDMA_MD_LOAD:
		md = proto_busdma_md_lookup(busdma, ioc->key);
		if (md == NULL) {
			error = EINVAL;
			break;
		}
		error = proto_busdma_md_load(busdma, md, ioc, td);
		break;
	case PROTO_IOC_BUSDMA_MD_UNLOAD:
		md = proto_busdma_md_lookup(busdma, ioc->key);
		if (md == NULL) {
			error = EINVAL;
			break;
		}
		error = proto_busdma_md_unload(busdma, md);
		break;
	case PROTO_IOC_BUSDMA_SYNC:
		md = proto_busdma_md_lookup(busdma, ioc->key);
		if (md == NULL) {
			error = EINVAL;
			break;
		}
		error = proto_busdma_sync(busdma, md, ioc);
		break;
	default:
		error = EINVAL;
		break;
	}
	return (error);
}

int
proto_busdma_mmap_allowed(struct proto_busdma *busdma, vm_paddr_t physaddr)
{
	struct proto_md *md;

	LIST_FOREACH(md, &busdma->mds, mds) {
		if (physaddr >= trunc_page(md->physaddr) &&
		    physaddr <= trunc_page(md->physaddr + md->tag->maxsz))
			return (1);
	}
	return (0);
}
