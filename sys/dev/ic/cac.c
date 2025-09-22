/*	$OpenBSD: cac.c,v 1.77 2023/11/28 09:29:20 jsg Exp $	*/
/*	$NetBSD: cac.c,v 1.15 2000/11/08 19:20:35 ad Exp $	*/

/*
 * Copyright (c) 2001,2003 Michael Shalayeff
 * All rights reserved.
 *
 * The SCSI emulation layer is derived from gdt(4) driver,
 * Copyright (c) 1999, 2000 Niklas Hallqvist. All rights reserved.
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
 * IN NO EVENT SHALL THE AUTHOR OR HIS RELATIVES BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF MIND, USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */
/*-
 * Copyright (c) 2000 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Andrew Doran.
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
 * Driver for Compaq array controllers.
 */

#include "bio.h"

/* #define	CAC_DEBUG */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/ioctl.h>
#include <sys/device.h>
#include <sys/queue.h>
#include <sys/buf.h>
#include <sys/endian.h>
#include <sys/malloc.h>
#include <sys/pool.h>

#include <machine/bus.h>

#include <scsi/scsi_all.h>
#include <scsi/scsi_disk.h>
#include <scsi/scsiconf.h>

#include <dev/ic/cacreg.h>
#include <dev/ic/cacvar.h>

#if NBIO > 0
#include <dev/biovar.h>
#endif
#include <sys/sensors.h>

struct cfdriver cac_cd = {
	NULL, "cac", DV_DULL
};

void    cac_scsi_cmd(struct scsi_xfer *);

const struct scsi_adapter cac_switch = {
	cac_scsi_cmd, NULL, NULL, NULL, NULL
};

void	*cac_ccb_alloc(void *);
void	cac_ccb_done(struct cac_softc *, struct cac_ccb *);
void	cac_ccb_free(void *, void *);
int	cac_ccb_poll(struct cac_softc *, struct cac_ccb *, int);
int	cac_ccb_start(struct cac_softc *, struct cac_ccb *);
int	cac_cmd(struct cac_softc *sc, int command, void *data, int datasize,
	int drive, int blkno, int flags, struct scsi_xfer *xs);
int	cac_get_dinfo(struct cac_softc *sc, int target);

struct	cac_ccb *cac_l0_completed(struct cac_softc *);
int	cac_l0_fifo_full(struct cac_softc *);
void	cac_l0_intr_enable(struct cac_softc *, int);
int	cac_l0_intr_pending(struct cac_softc *);
void	cac_l0_submit(struct cac_softc *, struct cac_ccb *);

#if NBIO > 0
int	cac_ioctl(struct device *, u_long, caddr_t);
int	cac_ioctl_vol(struct cac_softc *, struct bioc_vol *);

#ifndef SMALL_KERNEL
int	cac_create_sensors(struct cac_softc *);
void	cac_sensor_refresh(void *);
#endif
#endif /* NBIO > 0 */

const
struct cac_linkage cac_l0 = {
	cac_l0_completed,
	cac_l0_fifo_full,
	cac_l0_intr_enable,
	cac_l0_intr_pending,
	cac_l0_submit
};

/*
 * Initialise our interface to the controller.
 */
int
cac_init(struct cac_softc *sc, int startfw)
{
	struct scsibus_attach_args saa;
	struct cac_controller_info cinfo;
	int error, rseg, size, i;
	bus_dma_segment_t seg[1];
	struct cac_ccb *ccb;

	SIMPLEQ_INIT(&sc->sc_ccb_free);
	SIMPLEQ_INIT(&sc->sc_ccb_queue);
	mtx_init(&sc->sc_ccb_mtx, IPL_BIO);
	scsi_iopool_init(&sc->sc_iopool, sc, cac_ccb_alloc, cac_ccb_free);

        size = sizeof(struct cac_ccb) * CAC_MAX_CCBS;

	if ((error = bus_dmamem_alloc(sc->sc_dmat, size, PAGE_SIZE, 0, seg, 1,
	    &rseg, BUS_DMA_NOWAIT | BUS_DMA_ZERO)) != 0) {
		printf("%s: unable to allocate CCBs, error = %d\n",
		    sc->sc_dv.dv_xname, error);
		return (-1);
	}

	if ((error = bus_dmamem_map(sc->sc_dmat, seg, rseg, size,
	    &sc->sc_ccbs, BUS_DMA_NOWAIT | BUS_DMA_COHERENT)) != 0) {
		printf("%s: unable to map CCBs, error = %d\n",
		    sc->sc_dv.dv_xname, error);
		return (-1);
	}

	if ((error = bus_dmamap_create(sc->sc_dmat, size, 1, size, 0,
	    BUS_DMA_NOWAIT, &sc->sc_dmamap)) != 0) {
		printf("%s: unable to create CCB DMA map, error = %d\n",
		    sc->sc_dv.dv_xname, error);
		return (-1);
	}

	if ((error = bus_dmamap_load(sc->sc_dmat, sc->sc_dmamap, sc->sc_ccbs,
	    size, NULL, BUS_DMA_NOWAIT)) != 0) {
		printf("%s: unable to load CCB DMA map, error = %d\n",
		    sc->sc_dv.dv_xname, error);
		return (-1);
	}

	sc->sc_ccbs_paddr = sc->sc_dmamap->dm_segs[0].ds_addr;
	ccb = (struct cac_ccb *)sc->sc_ccbs;

	for (i = 0; i < CAC_MAX_CCBS; i++, ccb++) {
		/* Create the DMA map for this CCB's data */
		error = bus_dmamap_create(sc->sc_dmat, CAC_MAX_XFER,
		    CAC_SG_SIZE, CAC_MAX_XFER, 0,
		    BUS_DMA_NOWAIT | BUS_DMA_ALLOCNOW,
		    &ccb->ccb_dmamap_xfer);

		if (error) {
			printf("%s: can't create ccb dmamap (%d)\n",
			    sc->sc_dv.dv_xname, error);
			break;
		}

		ccb->ccb_paddr = sc->sc_ccbs_paddr + i * sizeof(struct cac_ccb);
		mtx_enter(&sc->sc_ccb_mtx);
		SIMPLEQ_INSERT_TAIL(&sc->sc_ccb_free, ccb, ccb_chain);
		mtx_leave(&sc->sc_ccb_mtx);
	}

	/* Start firmware background tasks, if needed. */
	if (startfw) {
		if (cac_cmd(sc, CAC_CMD_START_FIRMWARE, &cinfo, sizeof(cinfo),
		    0, 0, CAC_CCB_DATA_IN, NULL)) {
			printf("%s: CAC_CMD_START_FIRMWARE failed\n",
			    sc->sc_dv.dv_xname);
			return (-1);
		}
	}

	if (cac_cmd(sc, CAC_CMD_GET_CTRL_INFO, &cinfo, sizeof(cinfo), 0, 0,
	    CAC_CCB_DATA_IN, NULL)) {
		printf("%s: CAC_CMD_GET_CTRL_INFO failed\n",
		    sc->sc_dv.dv_xname);
		return (-1);
	}

	if (!cinfo.num_drvs) {
		printf("%s: no volumes defined\n", sc->sc_dv.dv_xname);
		return (-1);
	}

	sc->sc_nunits = cinfo.num_drvs;
	sc->sc_dinfos = mallocarray(cinfo.num_drvs,
	    sizeof(struct cac_drive_info), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (sc->sc_dinfos == NULL) {
		printf("%s: cannot allocate memory for drive_info\n",
		    sc->sc_dv.dv_xname);
		return (-1);
	}

	saa.saa_adapter_softc = sc;
	saa.saa_adapter = &cac_switch;
	saa.saa_adapter_target = SDEV_NO_ADAPTER_TARGET;
	saa.saa_adapter_buswidth = cinfo.num_drvs;
	saa.saa_luns = 8;
	saa.saa_openings = CAC_MAX_CCBS / sc->sc_nunits;
	if (saa.saa_openings < 4 )
		saa.saa_openings = 4;
	saa.saa_pool = &sc->sc_iopool;
	saa.saa_quirks = saa.saa_flags = 0;
	saa.saa_wwpn = saa.saa_wwnn = 0;

	sc->sc_scsibus = (struct scsibus_softc *)config_found(&sc->sc_dv, &saa,
	    scsiprint);

	(*sc->sc_cl->cl_intr_enable)(sc, 1);

#if NBIO > 0
	if (bio_register(&sc->sc_dv, cac_ioctl) != 0)
		printf("%s: controller registration failed\n",
		    sc->sc_dv.dv_xname);
	else
		sc->sc_ioctl = cac_ioctl;

#ifndef SMALL_KERNEL
	if (cac_create_sensors(sc) != 0)
		printf("%s: unable to create sensors\n", sc->sc_dv.dv_xname);
#endif
#endif


	return (0);
}

int
cac_flush(struct cac_softc *sc)
{
	u_int8_t buf[512];

	memset(buf, 0, sizeof(buf));
	buf[0] = 1;
	return cac_cmd(sc, CAC_CMD_FLUSH_CACHE, buf, sizeof(buf), 0, 0,
	    CAC_CCB_DATA_OUT, NULL);
}

/*
 * Handle an interrupt from the controller: process finished CCBs and
 * dequeue any waiting CCBs.
 */
int
cac_intr(void *v)
{
	struct cac_softc *sc = v;
	struct cac_ccb *ccb;
	int istat, ret = 0;

	if (!(istat = (*sc->sc_cl->cl_intr_pending)(sc)))
		return 0;

	if (istat & CAC_INTR_FIFO_NEMPTY)
		while ((ccb = (*sc->sc_cl->cl_completed)(sc)) != NULL) {
			ret = 1;
			cac_ccb_done(sc, ccb);
		}
	cac_ccb_start(sc, NULL);

	return (ret);
}

/*
 * Execute a [polled] command.
 */
int
cac_cmd(struct cac_softc *sc, int command, void *data, int datasize,
	int drive, int blkno, int flags, struct scsi_xfer *xs)
{
	struct cac_ccb *ccb;
	struct cac_sgb *sgb;
	int i, rv, size, nsegs;

#ifdef CAC_DEBUG
	printf("cac_cmd op=%x drv=%d blk=%d data=%p[%x] fl=%x xs=%p ",
	    command, drive, blkno, data, datasize, flags, xs);
#endif

	if (xs) {
		ccb = xs->io;
		/*
		 * The xs may have been restarted by the scsi layer, so
		 * ensure the ccb starts in the proper state.
		 */
		ccb->ccb_flags = 0;
	} else {
		/* Internal command. Need to get our own ccb. */
		ccb = scsi_io_get(&sc->sc_iopool, SCSI_POLL | SCSI_NOSLEEP);
		if (ccb == NULL)
			return (EBUSY);
	}

	if ((flags & (CAC_CCB_DATA_IN | CAC_CCB_DATA_OUT)) != 0) {
		bus_dmamap_load(sc->sc_dmat, ccb->ccb_dmamap_xfer,
		    (void *)data, datasize, NULL, BUS_DMA_NOWAIT);

		bus_dmamap_sync(sc->sc_dmat, ccb->ccb_dmamap_xfer, 0,
		    ccb->ccb_dmamap_xfer->dm_mapsize,
		    (flags & CAC_CCB_DATA_IN) != 0 ? BUS_DMASYNC_PREREAD :
		    BUS_DMASYNC_PREWRITE);

		sgb = ccb->ccb_seg;
		nsegs = ccb->ccb_dmamap_xfer->dm_nsegs;
		if (nsegs > CAC_SG_SIZE)
			panic("cac_cmd: nsegs botch");

		size = 0;
		for (i = 0; i < nsegs; i++, sgb++) {
			size += ccb->ccb_dmamap_xfer->dm_segs[i].ds_len;
			sgb->length =
			    htole32(ccb->ccb_dmamap_xfer->dm_segs[i].ds_len);
			sgb->addr =
			    htole32(ccb->ccb_dmamap_xfer->dm_segs[i].ds_addr);
		}
	} else {
		size = datasize;
		nsegs = 0;
	}

	ccb->ccb_hdr.drive = drive;
	ccb->ccb_hdr.priority = 0;
	ccb->ccb_hdr.size = htole16((sizeof(struct cac_req) +
	    sizeof(struct cac_sgb) * CAC_SG_SIZE) >> 2);

	ccb->ccb_req.next = 0;
	ccb->ccb_req.command = command;
	ccb->ccb_req.error = 0;
	ccb->ccb_req.blkno = htole32(blkno);
	ccb->ccb_req.bcount = htole16(howmany(size, DEV_BSIZE));
	ccb->ccb_req.sgcount = nsegs;
	ccb->ccb_req.reserved = 0;

	ccb->ccb_flags = flags;
	ccb->ccb_datasize = size;
	ccb->ccb_xs = xs;

	if (!xs || xs->flags & SCSI_POLL) {
		/* Synchronous commands mustn't wait. */
		mtx_enter(&sc->sc_ccb_mtx);
		if ((*sc->sc_cl->cl_fifo_full)(sc)) {
			mtx_leave(&sc->sc_ccb_mtx);
			rv = EBUSY;
		} else {
			mtx_leave(&sc->sc_ccb_mtx);
			ccb->ccb_flags |= CAC_CCB_ACTIVE;
			(*sc->sc_cl->cl_submit)(sc, ccb);
			rv = cac_ccb_poll(sc, ccb, 2000);
		}
	} else
		rv = cac_ccb_start(sc, ccb);

	if (xs == NULL)
		scsi_io_put(&sc->sc_iopool, ccb);

	return (rv);
}

/*
 * Wait for the specified CCB to complete.  Must be called at splbio.
 */
int
cac_ccb_poll(struct cac_softc *sc, struct cac_ccb *wantccb, int timo)
{
	struct cac_ccb *ccb;
	int t;

	t = timo * 100;
	do {
		for (; t--; DELAY(10))
			if ((ccb = (*sc->sc_cl->cl_completed)(sc)) != NULL)
				break;
		if (t < 0) {
			printf("%s: timeout\n", sc->sc_dv.dv_xname);
			return (EBUSY);
		}
		cac_ccb_done(sc, ccb);
	} while (ccb != wantccb);

	return (0);
}

/*
 * Enqueue the specified command (if any) and attempt to start all enqueued
 * commands.
 */
int
cac_ccb_start(struct cac_softc *sc, struct cac_ccb *ccb)
{
	if (ccb != NULL) {
		mtx_enter(&sc->sc_ccb_mtx);
		SIMPLEQ_INSERT_TAIL(&sc->sc_ccb_queue, ccb, ccb_chain);
		mtx_leave(&sc->sc_ccb_mtx);
	}

	while (1) {
		mtx_enter(&sc->sc_ccb_mtx);
		if (SIMPLEQ_EMPTY(&sc->sc_ccb_queue) ||
		    (*sc->sc_cl->cl_fifo_full)(sc)) {
			mtx_leave(&sc->sc_ccb_mtx);
			break;
		}
		ccb = SIMPLEQ_FIRST(&sc->sc_ccb_queue);
		SIMPLEQ_REMOVE_HEAD(&sc->sc_ccb_queue, ccb_chain);
		mtx_leave(&sc->sc_ccb_mtx);

		ccb->ccb_flags |= CAC_CCB_ACTIVE;
		(*sc->sc_cl->cl_submit)(sc, ccb);
	}

	return (0);
}

/*
 * Process a finished CCB.
 */
void
cac_ccb_done(struct cac_softc *sc, struct cac_ccb *ccb)
{
	struct scsi_xfer *xs = ccb->ccb_xs;
	int error = 0;

	if ((ccb->ccb_flags & CAC_CCB_ACTIVE) == 0) {
		printf("%s: CCB not active, xs=%p\n", sc->sc_dv.dv_xname, xs);
		if (xs) {
			xs->error = XS_DRIVER_STUFFUP;
			scsi_done(xs);
		}
		return;
	}

	if ((ccb->ccb_flags & (CAC_CCB_DATA_IN | CAC_CCB_DATA_OUT)) != 0) {
		bus_dmamap_sync(sc->sc_dmat, ccb->ccb_dmamap_xfer, 0,
		    ccb->ccb_dmamap_xfer->dm_mapsize,
		    ccb->ccb_flags & CAC_CCB_DATA_IN ?
		    BUS_DMASYNC_POSTREAD : BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->sc_dmat, ccb->ccb_dmamap_xfer);
	}

	if ((ccb->ccb_req.error & CAC_RET_SOFT_ERROR) != 0)
		printf("%s: soft error; corrected\n", sc->sc_dv.dv_xname);
	if ((ccb->ccb_req.error & CAC_RET_HARD_ERROR) != 0) {
		error = 1;
		printf("%s: hard error\n", sc->sc_dv.dv_xname);
	}
	if ((ccb->ccb_req.error & CAC_RET_CMD_REJECTED) != 0) {
		error = 1;
		printf("%s: invalid request\n", sc->sc_dv.dv_xname);
	}

	if (xs) {
		if (error)
			xs->error = XS_DRIVER_STUFFUP;
		else
			xs->resid = 0;

		scsi_done(xs);
	}
}

/*
 * Allocate a CCB.
 */
void *
cac_ccb_alloc(void *xsc)
{
	struct cac_softc *sc = xsc;
	struct cac_ccb *ccb = NULL;

	mtx_enter(&sc->sc_ccb_mtx);
	if (SIMPLEQ_EMPTY(&sc->sc_ccb_free)) {
#ifdef CAC_DEBUG
		printf("%s: unable to alloc CCB\n", sc->sc_dv.dv_xname);
#endif
	} else {
		ccb = SIMPLEQ_FIRST(&sc->sc_ccb_free);
		SIMPLEQ_REMOVE_HEAD(&sc->sc_ccb_free, ccb_chain);
	}
	mtx_leave(&sc->sc_ccb_mtx);

	return (ccb);
}

/*
 * Put a CCB onto the freelist.
 */
void
cac_ccb_free(void *xsc, void *xccb)
{
	struct cac_softc *sc = xsc;
	struct cac_ccb *ccb = xccb;

	ccb->ccb_flags = 0;

	mtx_enter(&sc->sc_ccb_mtx);
	SIMPLEQ_INSERT_HEAD(&sc->sc_ccb_free, ccb, ccb_chain);
	mtx_leave(&sc->sc_ccb_mtx);
}

int
cac_get_dinfo(struct cac_softc *sc, int target)
{
	if (sc->sc_dinfos[target].ncylinders)
		return (0);

	if (cac_cmd(sc, CAC_CMD_GET_LOG_DRV_INFO, &sc->sc_dinfos[target],
	    sizeof(*sc->sc_dinfos), target, 0, CAC_CCB_DATA_IN, NULL)) {
		printf("%s: CMD_GET_LOG_DRV_INFO failed\n",
		    sc->sc_dv.dv_xname);
		return (-1);
	}

	return (0);
}

void
cac_scsi_cmd(struct scsi_xfer *xs)
{
	struct scsi_link *link = xs->sc_link;
	struct cac_softc *sc = link->bus->sb_adapter_softc;
	struct cac_drive_info *dinfo;
	struct scsi_inquiry_data inq;
	struct scsi_sense_data sd;
	struct scsi_read_cap_data rcd;
	u_int8_t target = link->target;
	u_int32_t blockno, blockcnt, size;
	struct scsi_rw *rw;
	struct scsi_rw_10 *rw10;
	int op, flags, s, error;
	const char *p;

	if (target >= sc->sc_nunits || link->lun != 0) {
		xs->error = XS_DRIVER_STUFFUP;
		scsi_done(xs);
		return;
	}

	s = splbio();
	xs->error = XS_NOERROR;
	dinfo = &sc->sc_dinfos[target];

	switch (xs->cmd.opcode) {
	case TEST_UNIT_READY:
	case START_STOP:
#if 0
	case VERIFY:
#endif
		break;

	case REQUEST_SENSE:
		bzero(&sd, sizeof sd);
		sd.error_code = SSD_ERRCODE_CURRENT;
		sd.segment = 0;
		sd.flags = SKEY_NO_SENSE;
		*(u_int32_t*)sd.info = htole32(0);
		sd.extra_len = 0;
		scsi_copy_internal_data(xs, &sd, sizeof(sd));
		break;

	case INQUIRY:
		if (cac_get_dinfo(sc, target)) {
			xs->error = XS_DRIVER_STUFFUP;
			break;
		}
		bzero(&inq, sizeof inq);
		inq.device = T_DIRECT;
		inq.dev_qual2 = 0;
		inq.version = SCSI_REV_2;
		inq.response_format = SID_SCSI2_RESPONSE;
		inq.additional_length = SID_SCSI2_ALEN;
		inq.flags |= SID_CmdQue;
		strlcpy(inq.vendor, "Compaq  ", sizeof inq.vendor);
		switch (CAC_GET1(dinfo->mirror)) {
		case 0: p = "RAID0";	break;
		case 1: p = "RAID4";	break;
		case 2: p = "RAID1";	break;
		case 3: p = "RAID5";	break;
		default:p = "<UNK>";	break;
		}
		snprintf(inq.product, sizeof inq.product, "%s vol  #%02d",
		    p, target);
		strlcpy(inq.revision, "   ", sizeof inq.revision);
		scsi_copy_internal_data(xs, &inq, sizeof(inq));
		break;

	case READ_CAPACITY:
		if (cac_get_dinfo(sc, target)) {
			xs->error = XS_DRIVER_STUFFUP;
			break;
		}
		bzero(&rcd, sizeof rcd);
		_lto4b( CAC_GET2(dinfo->ncylinders) * CAC_GET1(dinfo->nheads) *
		    CAC_GET1(dinfo->nsectors) - 1, rcd.addr);
		_lto4b(CAC_SECTOR_SIZE, rcd.length);
		scsi_copy_internal_data(xs, &rcd, sizeof(rcd));
		break;

	case PREVENT_ALLOW:
		break;

	case SYNCHRONIZE_CACHE:
		if (cac_flush(sc))
			xs->error = XS_DRIVER_STUFFUP;
		break;

	case READ_COMMAND:
	case READ_10:
	case WRITE_COMMAND:
	case WRITE_10:

		flags = 0;
		/* A read or write operation. */
		if (xs->cmdlen == 6) {
			rw = (struct scsi_rw *)&xs->cmd;
			blockno = _3btol(rw->addr) &
			    (SRW_TOPADDR << 16 | 0xffff);
			blockcnt = rw->length ? rw->length : 0x100;
		} else {
			rw10 = (struct scsi_rw_10 *)&xs->cmd;
			blockno = _4btol(rw10->addr);
			blockcnt = _2btol(rw10->length);
		}
		size = CAC_GET2(dinfo->ncylinders) *
		    CAC_GET1(dinfo->nheads) * CAC_GET1(dinfo->nsectors);
		if (blockno >= size || blockno + blockcnt > size) {
			printf("%s: out of bounds %u-%u >= %u\n",
			    sc->sc_dv.dv_xname, blockno, blockcnt, size);
			xs->error = XS_DRIVER_STUFFUP;
			break;
		}

		switch (xs->cmd.opcode) {
		case READ_COMMAND:
		case READ_10:
			op = CAC_CMD_READ;
			flags = CAC_CCB_DATA_IN;
			break;
		case WRITE_COMMAND:
		case WRITE_10:
			op = CAC_CMD_WRITE;
			flags = CAC_CCB_DATA_OUT;
			break;
		}

		if ((error = cac_cmd(sc, op, xs->data, blockcnt * DEV_BSIZE,
		    target, blockno, flags, xs))) {
			splx(s);
			if (error == EBUSY)
				xs->error = XS_BUSY;
			else
				xs->error = XS_DRIVER_STUFFUP;
			scsi_done(xs);
			return;
		}

		splx(s);
		return;

	default:
#ifdef CAC_DEBUG
		printf("unsupported scsi command %#x tgt %d ", xs->cmd.opcode, target);
#endif
		xs->error = XS_DRIVER_STUFFUP;
	}

	splx(s);
	scsi_done(xs);
}

/*
 * Board specific linkage shared between multiple bus types.
 */

int
cac_l0_fifo_full(struct cac_softc *sc)
{

	return (cac_inl(sc, CAC_REG_CMD_FIFO) == 0);
}

void
cac_l0_submit(struct cac_softc *sc, struct cac_ccb *ccb)
{
#ifdef CAC_DEBUG
	printf("submit-%lx ", ccb->ccb_paddr);
#endif
	bus_dmamap_sync(sc->sc_dmat, sc->sc_dmamap, 0,
	    sc->sc_dmamap->dm_mapsize,
	    BUS_DMASYNC_PREWRITE | BUS_DMASYNC_PREREAD);
	cac_outl(sc, CAC_REG_CMD_FIFO, ccb->ccb_paddr);
}

struct cac_ccb *
cac_l0_completed(struct cac_softc *sc)
{
	struct cac_ccb *ccb;
	paddr_t off, orig_off;

	if (!(off = cac_inl(sc, CAC_REG_DONE_FIFO)))
		return NULL;
#ifdef CAC_DEBUG
	printf("compl-%lx ", off);
#endif
	orig_off = off;

	bus_dmamap_sync(sc->sc_dmat, sc->sc_dmamap, 0,
	    sc->sc_dmamap->dm_mapsize,
	    BUS_DMASYNC_POSTWRITE | BUS_DMASYNC_POSTREAD);

	off = (off & ~3) - sc->sc_ccbs_paddr;
	ccb = (struct cac_ccb *)(sc->sc_ccbs + off);

	if (orig_off & 3 && ccb->ccb_req.error == 0)
		ccb->ccb_req.error = CAC_RET_CMD_INVALID;

	return (ccb);
}

int
cac_l0_intr_pending(struct cac_softc *sc)
{

	return (cac_inl(sc, CAC_REG_INTR_PENDING));
}

void
cac_l0_intr_enable(struct cac_softc *sc, int state)
{

	cac_outl(sc, CAC_REG_INTR_MASK,
	    state ? CAC_INTR_ENABLE : CAC_INTR_DISABLE);
}

#if NBIO > 0
const int cac_level[] = { 0, 4, 1, 5, 51, 7 };
const int cac_stat[] = { BIOC_SVONLINE, BIOC_SVOFFLINE, BIOC_SVOFFLINE,
    BIOC_SVDEGRADED, BIOC_SVREBUILD, BIOC_SVREBUILD, BIOC_SVDEGRADED,
    BIOC_SVDEGRADED, BIOC_SVINVALID, BIOC_SVINVALID, BIOC_SVBUILDING,
    BIOC_SVOFFLINE, BIOC_SVBUILDING };

int
cac_ioctl(struct device *dev, u_long cmd, caddr_t addr)
{
	struct cac_softc *sc = (struct cac_softc *)dev;
	struct bioc_inq *bi;
	struct bioc_disk *bd;
	cac_lock_t lock;
	int error = 0;

	lock = CAC_LOCK(sc);
	switch (cmd) {
	case BIOCINQ:
		bi = (struct bioc_inq *)addr;
		strlcpy(bi->bi_dev, sc->sc_dv.dv_xname, sizeof(bi->bi_dev));
		bi->bi_novol = sc->sc_nunits;
		bi->bi_nodisk = 0;
		break;

	case BIOCVOL:
		error = cac_ioctl_vol(sc, (struct bioc_vol *)addr);
		break;

	case BIOCDISK:
		bd = (struct bioc_disk *)addr;
		if (bd->bd_volid > sc->sc_nunits) {
			error = EINVAL;
			break;
		}
		/* No disk information yet */
		break;

	case BIOCBLINK:
	case BIOCALARM:
	case BIOCSETSTATE:
	default:
		error = ENOTTY;
	}
	CAC_UNLOCK(sc, lock);

	return (error);
}

int
cac_ioctl_vol(struct cac_softc *sc, struct bioc_vol *bv)
{
	struct cac_drive_info dinfo;
	struct cac_drive_status dstatus;
	u_int32_t blks;

	if (bv->bv_volid > sc->sc_nunits)
		return (EINVAL);
	if (cac_cmd(sc, CAC_CMD_GET_LOG_DRV_INFO, &dinfo, sizeof(dinfo),
	    bv->bv_volid, 0, CAC_CCB_DATA_IN, NULL))
		return (EIO);
	if (cac_cmd(sc, CAC_CMD_SENSE_DRV_STATUS, &dstatus, sizeof(dstatus),
	    bv->bv_volid, 0, CAC_CCB_DATA_IN, NULL))
		return (EIO);
	bv->bv_status = BIOC_SVINVALID;
	blks = CAC_GET2(dinfo.ncylinders) * CAC_GET1(dinfo.nheads) *
	    CAC_GET1(dinfo.nsectors);
	bv->bv_size = (off_t)blks * CAC_GET2(dinfo.secsize);
	bv->bv_level = cac_level[CAC_GET1(dinfo.mirror)];	/*XXX limit check */
	bv->bv_nodisk = 0;		/* XXX */
	bv->bv_status = 0;		/* XXX */
	bv->bv_percent = -1;
	bv->bv_seconds = 0;
	if (dstatus.stat < nitems(cac_stat))
		bv->bv_status = cac_stat[dstatus.stat];
	if (bv->bv_status == BIOC_SVREBUILD ||
	    bv->bv_status == BIOC_SVBUILDING)
		bv->bv_percent = ((blks - CAC_GET4(dstatus.prog)) * 1000ULL) /
		    blks;

	return (0);
}

#ifndef SMALL_KERNEL
int
cac_create_sensors(struct cac_softc *sc)
{
	struct device *dev;
	struct scsibus_softc *ssc = NULL;
	struct scsi_link *link;
	int i;

	TAILQ_FOREACH(dev, &alldevs, dv_list) {
		if (dev->dv_parent != &sc->sc_dv)
			continue;

		/* check if this is the scsibus for the logical disks */
		ssc = (struct scsibus_softc *)dev;
		if (ssc == sc->sc_scsibus)
			break;
		ssc = NULL;
	}

	if (ssc == NULL)
		return (1);

	sc->sc_sensors = mallocarray(sc->sc_nunits,
	    sizeof(struct ksensor), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (sc->sc_sensors == NULL)
		return (1);

	strlcpy(sc->sc_sensordev.xname, sc->sc_dv.dv_xname,
	    sizeof(sc->sc_sensordev.xname));

	for (i = 0; i < sc->sc_nunits; i++) {
		link = scsi_get_link(ssc, i, 0);
		if (link == NULL)
			goto bad;

		dev = link->device_softc;

		sc->sc_sensors[i].type = SENSOR_DRIVE;
		sc->sc_sensors[i].status = SENSOR_S_UNKNOWN;

		strlcpy(sc->sc_sensors[i].desc, dev->dv_xname,
		    sizeof(sc->sc_sensors[i].desc));

		sensor_attach(&sc->sc_sensordev, &sc->sc_sensors[i]);
	}

	if (sensor_task_register(sc, cac_sensor_refresh, 10) == NULL)
		goto bad;

	sensordev_install(&sc->sc_sensordev);

	return (0);

bad:
	free(sc->sc_sensors, M_DEVBUF,
	    sc->sc_nunits * sizeof(struct ksensor));

	return (1);
}

void
cac_sensor_refresh(void *arg)
{
	struct cac_softc *sc = arg;
	struct bioc_vol bv;
	int i, s;

	for (i = 0; i < sc->sc_nunits; i++) {
		bzero(&bv, sizeof(bv));
		bv.bv_volid = i;
		s = splbio();
		if (cac_ioctl_vol(sc, &bv)) {
			splx(s);
			return;
		}
		splx(s);

		switch (bv.bv_status) {
		case BIOC_SVOFFLINE:
			sc->sc_sensors[i].value = SENSOR_DRIVE_FAIL;
			sc->sc_sensors[i].status = SENSOR_S_CRIT;
			break;

		case BIOC_SVDEGRADED:
			sc->sc_sensors[i].value = SENSOR_DRIVE_PFAIL;
			sc->sc_sensors[i].status = SENSOR_S_WARN;
			break;

		case BIOC_SVSCRUB:
		case BIOC_SVONLINE:
			sc->sc_sensors[i].value = SENSOR_DRIVE_ONLINE;
			sc->sc_sensors[i].status = SENSOR_S_OK;
			break;

		case BIOC_SVREBUILD:
		case BIOC_SVBUILDING:
			sc->sc_sensors[i].value = SENSOR_DRIVE_REBUILD;
			sc->sc_sensors[i].status = SENSOR_S_OK;
			break;

		case BIOC_SVINVALID:
			/* FALLTHROUGH */
		default:
			sc->sc_sensors[i].value = 0; /* unknown */
			sc->sc_sensors[i].status = SENSOR_S_UNKNOWN;
		}
	}
}
#endif /* SMALL_KERNEL */
#endif /* NBIO > 0 */
