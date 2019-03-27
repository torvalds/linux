/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright 2008 by Nathan Whitehorn. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("* $FreeBSD$");

/*
 * Common routines for the DMA engine on both the Apple Kauai and MacIO
 * ATA controllers.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/malloc.h>
#include <sys/sema.h>
#include <sys/taskqueue.h>
#include <vm/uma.h>
#include <machine/stdarg.h>
#include <machine/resource.h>
#include <machine/bus.h>
#include <sys/rman.h>
#include <sys/ata.h>
#include <dev/ata/ata-all.h>
#include <dev/ata/ata-pci.h>
#include <ata_if.h>

#include "ata_dbdma.h"

struct ata_dbdma_dmaload_args {
	struct ata_dbdma_channel *sc;

	int write;
	int nsegs;
};

static void
ata_dbdma_setprd(void *xarg, bus_dma_segment_t *segs, int nsegs, int error)
{
	struct ata_dbdma_dmaload_args *arg = xarg;
	struct ata_dbdma_channel *sc = arg->sc;
	int branch_type, command;
	int prev_stop;
	int i;

	mtx_lock(&sc->dbdma_mtx);

	prev_stop = sc->next_dma_slot-1;
	if (prev_stop < 0)
		prev_stop = 0xff;

	for (i = 0; i < nsegs; i++) {
		/* Loop back to the beginning if this is our last slot */
		if (sc->next_dma_slot == 0xff)
			branch_type = DBDMA_ALWAYS;
		else
			branch_type = DBDMA_NEVER;

		if (arg->write) {
			command = (i + 1 < nsegs) ? DBDMA_OUTPUT_MORE : 
			    DBDMA_OUTPUT_LAST;
		} else {
			command = (i + 1 < nsegs) ? DBDMA_INPUT_MORE : 
			    DBDMA_INPUT_LAST;
		}

		dbdma_insert_command(sc->dbdma, sc->next_dma_slot++,
		    command, 0, segs[i].ds_addr, segs[i].ds_len,
		    DBDMA_NEVER, branch_type, DBDMA_NEVER, 0);

		if (branch_type == DBDMA_ALWAYS)
			sc->next_dma_slot = 0;
	}

	/* We have a corner case where the STOP command is the last slot,
	 * but you can't branch in STOP commands. So add a NOP branch here
	 * and the STOP in slot 0. */

	if (sc->next_dma_slot == 0xff) {
		dbdma_insert_branch(sc->dbdma, sc->next_dma_slot, 0);
		sc->next_dma_slot = 0;
	}

#if 0
	dbdma_insert_command(sc->dbdma, sc->next_dma_slot++,
	    DBDMA_NOP, 0, 0, 0, DBDMA_ALWAYS, DBDMA_NEVER, DBDMA_NEVER, 0);
#endif
	dbdma_insert_stop(sc->dbdma, sc->next_dma_slot++);
	dbdma_insert_nop(sc->dbdma, prev_stop);

	dbdma_sync_commands(sc->dbdma, BUS_DMASYNC_PREWRITE);

	mtx_unlock(&sc->dbdma_mtx);

	arg->nsegs = nsegs;
}

static int
ata_dbdma_status(device_t dev)
{
	struct ata_dbdma_channel *sc = device_get_softc(dev);
	struct ata_channel *ch = device_get_softc(dev);

	if (sc->sc_ch.dma.flags & ATA_DMA_ACTIVE) {
		return (!(dbdma_get_chan_status(sc->dbdma) & 
		    DBDMA_STATUS_ACTIVE));
	}

	if (ATA_IDX_INB(ch, ATA_ALTSTAT) & ATA_S_BUSY) {
		DELAY(100);
		if (ATA_IDX_INB(ch, ATA_ALTSTAT) & ATA_S_BUSY)
			return 0;
	}
	return 1;
}

static int
ata_dbdma_start(struct ata_request *request)
{
	struct ata_dbdma_channel *sc = device_get_softc(request->parent);

	sc->sc_ch.dma.flags |= ATA_DMA_ACTIVE;
	dbdma_wake(sc->dbdma);
	return 0;
}

static void
ata_dbdma_reset(device_t dev)
{
	struct ata_dbdma_channel *sc = device_get_softc(dev);

	mtx_lock(&sc->dbdma_mtx);

	dbdma_stop(sc->dbdma);
	dbdma_insert_stop(sc->dbdma, 0);
	sc->next_dma_slot=1;
	dbdma_set_current_cmd(sc->dbdma, 0);

	sc->sc_ch.dma.flags &= ~ATA_DMA_ACTIVE;

	mtx_unlock(&sc->dbdma_mtx);
}

static int
ata_dbdma_stop(struct ata_request *request)
{
	struct ata_dbdma_channel *sc = device_get_softc(request->parent);

	uint16_t status;
	
	status = dbdma_get_chan_status(sc->dbdma);

	dbdma_pause(sc->dbdma);
	sc->sc_ch.dma.flags &= ~ATA_DMA_ACTIVE;

	if (status & DBDMA_STATUS_DEAD) {
		device_printf(request->parent,"DBDMA dead, resetting "
		    "channel...\n");
		ata_dbdma_reset(request->parent);
		return ATA_S_ERROR;
	}

	if (!(status & DBDMA_STATUS_RUN)) {
		device_printf(request->parent,"DBDMA confused, stop called "
		    "when channel is not running!\n");
		return ATA_S_ERROR;
	}

	if (status & DBDMA_STATUS_ACTIVE) {
		device_printf(request->parent,"DBDMA channel stopped "
		    "prematurely\n");
		return ATA_S_ERROR;
	}
	return 0;
}

static int
ata_dbdma_load(struct ata_request *request, void *addr, int *entries)
{
	struct ata_channel *ch = device_get_softc(request->parent);
	struct ata_dbdma_dmaload_args args;

	int error;

	args.sc = device_get_softc(request->parent);
	args.write = !(request->flags & ATA_R_READ);

	if (!request->bytecount) {
		device_printf(request->dev,
		    "FAILURE - zero length DMA transfer attempted\n");
		return EIO;
	}
	if (((uintptr_t)(request->data) & (ch->dma.alignment - 1)) ||
	    (request->bytecount & (ch->dma.alignment - 1))) {
		device_printf(request->dev,
		    "FAILURE - non aligned DMA transfer attempted\n");
		return EIO;
	}
	if (request->bytecount > ch->dma.max_iosize) {
		device_printf(request->dev,
		    "FAILURE - oversized DMA transfer attempt %d > %d\n",
		    request->bytecount, ch->dma.max_iosize);
		return EIO;
	}

	request->dma = &ch->dma.slot[0];

	if ((error = bus_dmamap_load(request->dma->data_tag, 
	    request->dma->data_map, request->data, request->bytecount,
	    &ata_dbdma_setprd, &args, BUS_DMA_NOWAIT))) {
		device_printf(request->dev, "FAILURE - load data\n");
		goto error;
	}

	if (entries)
		*entries = args.nsegs;

	bus_dmamap_sync(request->dma->sg_tag, request->dma->sg_map,
	    BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(request->dma->data_tag, request->dma->data_map,
	    (request->flags & ATA_R_READ) ?
	    BUS_DMASYNC_PREREAD : BUS_DMASYNC_PREWRITE);

	return 0;

error:
	ch->dma.unload(request);
	return EIO;
}

void
ata_dbdma_dmainit(device_t dev)
{
	struct ata_dbdma_channel *sc = device_get_softc(dev);
	int error;

	error = dbdma_allocate_channel(sc->dbdma_regs, sc->dbdma_offset,
	    bus_get_dma_tag(dev), 256, &sc->dbdma);

	dbdma_set_wait_selector(sc->dbdma,1 << 7, 1 << 7);

	dbdma_insert_stop(sc->dbdma,0);
	sc->next_dma_slot=1;

	sc->sc_ch.dma.start = ata_dbdma_start;
	sc->sc_ch.dma.stop = ata_dbdma_stop;
	sc->sc_ch.dma.load = ata_dbdma_load;
	sc->sc_ch.dma.reset = ata_dbdma_reset;

	/*
	 * DBDMA's field for transfer size is 16 bits. This will overflow
	 * if we try to do a 64K transfer, so stop short of 64K.
	 */
	sc->sc_ch.dma.segsize = 126 * DEV_BSIZE;
	ata_dmainit(dev);

	sc->sc_ch.hw.status = ata_dbdma_status;

	mtx_init(&sc->dbdma_mtx, "ATA DBDMA", NULL, MTX_DEF);
}
