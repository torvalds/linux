/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1998 - 2008 Søren Schmidt <sos@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
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
#include <sys/ata.h>
#include <sys/kernel.h>
#include <sys/endian.h>
#include <sys/malloc.h> 
#include <sys/lock.h>
#include <sys/sema.h>
#include <sys/taskqueue.h>
#include <vm/uma.h>
#include <sys/bus.h>
#include <machine/bus.h>
#include <sys/rman.h>
#include <dev/ata/ata-all.h>

/* prototypes */
static void ata_dmasetupc_cb(void *xsc, bus_dma_segment_t *segs, int nsegs, int error);
static void ata_dmaalloc(device_t dev);
static void ata_dmafree(device_t dev);
static void ata_dmasetprd(void *xsc, bus_dma_segment_t *segs, int nsegs, int error);
static int ata_dmaload(struct ata_request *request, void *addr, int *nsegs);
static int ata_dmaunload(struct ata_request *request);

/* local vars */
static MALLOC_DEFINE(M_ATADMA, "ata_dma", "ATA driver DMA");

/* misc defines */
#define MAXTABSZ        PAGE_SIZE
#define MAXWSPCSZ       PAGE_SIZE*2

struct ata_dc_cb_args {
    bus_addr_t maddr;
    int error;
};

void 
ata_dmainit(device_t dev)
{
    struct ata_channel *ch = device_get_softc(dev);
    struct ata_dc_cb_args dcba;

    if (ch->dma.alloc == NULL)
	ch->dma.alloc = ata_dmaalloc;
    if (ch->dma.free == NULL)
	ch->dma.free = ata_dmafree;
    if (ch->dma.setprd == NULL)
	ch->dma.setprd = ata_dmasetprd;
    if (ch->dma.load == NULL)
	ch->dma.load = ata_dmaload;
    if (ch->dma.unload == NULL)
	ch->dma.unload = ata_dmaunload;
    if (ch->dma.alignment == 0)
	ch->dma.alignment = 2;
    if (ch->dma.boundary == 0)
	ch->dma.boundary = 65536;
    if (ch->dma.segsize == 0)
	ch->dma.segsize = 65536;
    if (ch->dma.max_iosize == 0)
	ch->dma.max_iosize = MIN((ATA_DMA_ENTRIES - 1) * PAGE_SIZE, MAXPHYS);
    if (ch->dma.max_address == 0)
	ch->dma.max_address = BUS_SPACE_MAXADDR_32BIT;
    if (ch->dma.dma_slots == 0)
	ch->dma.dma_slots = 1;

    if (bus_dma_tag_create(bus_get_dma_tag(dev), ch->dma.alignment, 0,
			   ch->dma.max_address, BUS_SPACE_MAXADDR,
			   NULL, NULL, ch->dma.max_iosize,
			   ATA_DMA_ENTRIES, ch->dma.segsize,
			   0, NULL, NULL, &ch->dma.dmatag))
	goto error;

    if (bus_dma_tag_create(ch->dma.dmatag, PAGE_SIZE, 64 * 1024,
			   ch->dma.max_address, BUS_SPACE_MAXADDR,
			   NULL, NULL, MAXWSPCSZ, 1, MAXWSPCSZ,
			   0, NULL, NULL, &ch->dma.work_tag))
	goto error;

    if (bus_dmamem_alloc(ch->dma.work_tag, (void **)&ch->dma.work,
			 BUS_DMA_WAITOK | BUS_DMA_COHERENT,
			 &ch->dma.work_map))
	goto error;

    if (bus_dmamap_load(ch->dma.work_tag, ch->dma.work_map, ch->dma.work,
			MAXWSPCSZ, ata_dmasetupc_cb, &dcba, 0) ||
			dcba.error) {
	bus_dmamem_free(ch->dma.work_tag, ch->dma.work, ch->dma.work_map);
	goto error;
    }
    ch->dma.work_bus = dcba.maddr;
    return;

error:
    device_printf(dev, "WARNING - DMA initialization failed, disabling DMA\n");
    ata_dmafini(dev);
}

void 
ata_dmafini(device_t dev)
{
    struct ata_channel *ch = device_get_softc(dev);

    if (ch->dma.work_bus) {
	bus_dmamap_unload(ch->dma.work_tag, ch->dma.work_map);
	bus_dmamem_free(ch->dma.work_tag, ch->dma.work, ch->dma.work_map);
	ch->dma.work_bus = 0;
	ch->dma.work = NULL;
    }
    if (ch->dma.work_tag) {
	bus_dma_tag_destroy(ch->dma.work_tag);
	ch->dma.work_tag = NULL;
    }
    if (ch->dma.dmatag) {
	bus_dma_tag_destroy(ch->dma.dmatag);
	ch->dma.dmatag = NULL;
    }
}

static void
ata_dmasetupc_cb(void *xsc, bus_dma_segment_t *segs, int nsegs, int error)
{
    struct ata_dc_cb_args *dcba = (struct ata_dc_cb_args *)xsc;

    if (!(dcba->error = error))
	dcba->maddr = segs[0].ds_addr;
}

static void
ata_dmaalloc(device_t dev)
{
    struct ata_channel *ch = device_get_softc(dev);
    struct ata_dc_cb_args dcba;
    int i;

    /* alloc and setup needed dma slots */
    bzero(ch->dma.slot, sizeof(struct ata_dmaslot) * ATA_DMA_SLOTS);
    for (i = 0; i < ch->dma.dma_slots; i++) {
	struct ata_dmaslot *slot = &ch->dma.slot[i];

	if (bus_dma_tag_create(ch->dma.dmatag, PAGE_SIZE, PAGE_SIZE,
			       ch->dma.max_address, BUS_SPACE_MAXADDR,
			       NULL, NULL, PAGE_SIZE, 1, PAGE_SIZE,
			       0, NULL, NULL, &slot->sg_tag)) {
            device_printf(ch->dev, "FAILURE - create sg_tag\n");
            goto error;
	}

	if (bus_dmamem_alloc(slot->sg_tag, (void **)&slot->sg, BUS_DMA_WAITOK,
			     &slot->sg_map)) {
	    device_printf(ch->dev, "FAILURE - alloc sg_map\n");
	    goto error;
        }

	if (bus_dmamap_load(slot->sg_tag, slot->sg_map, slot->sg, MAXTABSZ,
			    ata_dmasetupc_cb, &dcba, 0) || dcba.error) {
	    device_printf(ch->dev, "FAILURE - load sg\n");
	    goto error;
	}
	slot->sg_bus = dcba.maddr;

	if (bus_dma_tag_create(ch->dma.dmatag,
			       ch->dma.alignment, ch->dma.boundary,
                               ch->dma.max_address, BUS_SPACE_MAXADDR,
                               NULL, NULL, ch->dma.max_iosize,
                               ATA_DMA_ENTRIES, ch->dma.segsize,
                               BUS_DMA_ALLOCNOW, NULL, NULL, &slot->data_tag)) {
	    device_printf(ch->dev, "FAILURE - create data_tag\n");
	    goto error;
	}

	if (bus_dmamap_create(slot->data_tag, 0, &slot->data_map)) {
	    device_printf(ch->dev, "FAILURE - create data_map\n");
	    goto error;
        }
    }

    return;

error:
    device_printf(dev, "WARNING - DMA allocation failed, disabling DMA\n");
    ata_dmafree(dev);
}

static void
ata_dmafree(device_t dev)
{
    struct ata_channel *ch = device_get_softc(dev);
    int i;

    /* free all dma slots */
    for (i = 0; i < ATA_DMA_SLOTS; i++) {
	struct ata_dmaslot *slot = &ch->dma.slot[i];

	if (slot->sg_bus) {
            bus_dmamap_unload(slot->sg_tag, slot->sg_map);
            slot->sg_bus = 0;
	}
	if (slot->sg) {
            bus_dmamem_free(slot->sg_tag, slot->sg, slot->sg_map);
            slot->sg = NULL;
	}
	if (slot->data_map) {
            bus_dmamap_destroy(slot->data_tag, slot->data_map);
            slot->data_map = NULL;
	}
	if (slot->sg_tag) {
            bus_dma_tag_destroy(slot->sg_tag);
            slot->sg_tag = NULL;
	}
	if (slot->data_tag) {
            bus_dma_tag_destroy(slot->data_tag);
            slot->data_tag = NULL;
	}
    }
}

static void
ata_dmasetprd(void *xsc, bus_dma_segment_t *segs, int nsegs, int error)
{
    struct ata_dmasetprd_args *args = xsc;
    struct ata_dma_prdentry *prd = args->dmatab;
    int i;

    if ((args->error = error))
	return;

    for (i = 0; i < nsegs; i++) {
	prd[i].addr = htole32(segs[i].ds_addr);
	prd[i].count = htole32(segs[i].ds_len);
    }
    prd[i - 1].count |= htole32(ATA_DMA_EOT);
    KASSERT(nsegs <= ATA_DMA_ENTRIES, ("too many DMA segment entries\n"));
    args->nsegs = nsegs;
}

static int
ata_dmaload(struct ata_request *request, void *addr, int *entries)
{
    struct ata_channel *ch = device_get_softc(request->parent);
    struct ata_dmasetprd_args dspa;
    int error;

    ATA_DEBUG_RQ(request, "dmaload");

    if (request->dma) {
	device_printf(request->parent,
		      "FAILURE - already active DMA on this device\n");
	return EIO;
    }
    if (!request->bytecount) {
	device_printf(request->parent,
		      "FAILURE - zero length DMA transfer attempted\n");
	return EIO;
    }
    if (request->bytecount & (ch->dma.alignment - 1)) {
	device_printf(request->parent,
		      "FAILURE - odd-sized DMA transfer attempt %d %% %d\n",
		      request->bytecount, ch->dma.alignment);
	return EIO;
    }
    if (request->bytecount > ch->dma.max_iosize) {
	device_printf(request->parent,
		      "FAILURE - oversized DMA transfer attempt %d > %d\n",
		      request->bytecount, ch->dma.max_iosize);
	return EIO;
    }

    /* set our slot. XXX SOS NCQ will change that */
    request->dma = &ch->dma.slot[0];

    if (addr)
	dspa.dmatab = addr;
    else
	dspa.dmatab = request->dma->sg;

    if (request->flags & ATA_R_DATA_IN_CCB)
        error = bus_dmamap_load_ccb(request->dma->data_tag,
				request->dma->data_map, request->ccb,
				ch->dma.setprd, &dspa, BUS_DMA_NOWAIT);
    else
        error = bus_dmamap_load(request->dma->data_tag, request->dma->data_map,
				request->data, request->bytecount,
				ch->dma.setprd, &dspa, BUS_DMA_NOWAIT);
    if (error || (error = dspa.error)) {
	device_printf(request->parent, "FAILURE - load data\n");
	goto error;
    }

    if (entries)
	*entries = dspa.nsegs;

    bus_dmamap_sync(request->dma->sg_tag, request->dma->sg_map,
		    BUS_DMASYNC_PREWRITE);
    bus_dmamap_sync(request->dma->data_tag, request->dma->data_map,
		    (request->flags & ATA_R_READ) ?
		    BUS_DMASYNC_PREREAD : BUS_DMASYNC_PREWRITE);
    return 0;

error:
    ata_dmaunload(request);
    return EIO;
}

int
ata_dmaunload(struct ata_request *request)
{
    ATA_DEBUG_RQ(request, "dmaunload");

    if (request->dma) {
	bus_dmamap_sync(request->dma->sg_tag, request->dma->sg_map,
			BUS_DMASYNC_POSTWRITE);
	bus_dmamap_sync(request->dma->data_tag, request->dma->data_map,
			(request->flags & ATA_R_READ) ?
			BUS_DMASYNC_POSTREAD : BUS_DMASYNC_POSTWRITE);

	bus_dmamap_unload(request->dma->data_tag, request->dma->data_map);
        request->dma = NULL;
    }
    return 0;
}
