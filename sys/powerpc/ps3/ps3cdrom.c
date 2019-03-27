/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2010 Nathan Whitehorn
 * Copyright (C) 2011 glevand <geoffrey.levand@mail.ru>
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
#include <sys/module.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/ata.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/kthread.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <machine/pio.h>
#include <machine/bus.h>
#include <machine/platform.h>
#include <machine/resource.h>
#include <sys/bus.h>
#include <sys/rman.h>

#include <cam/cam.h>
#include <cam/cam_ccb.h>
#include <cam/cam_sim.h>
#include <cam/cam_xpt_sim.h>
#include <cam/cam_debug.h>
#include <cam/scsi/scsi_all.h>

#include "ps3bus.h"
#include "ps3-hvcall.h"

#define PS3CDROM_LOCK_INIT(_sc)		\
	mtx_init(&_sc->sc_mtx, device_get_nameunit(_sc->sc_dev), "ps3cdrom", \
	    MTX_DEF)
#define PS3CDROM_LOCK_DESTROY(_sc)	mtx_destroy(&_sc->sc_mtx);
#define PS3CDROM_LOCK(_sc)		mtx_lock(&(_sc)->sc_mtx)
#define	PS3CDROM_UNLOCK(_sc)		mtx_unlock(&(_sc)->sc_mtx)
#define PS3CDROM_ASSERT_LOCKED(_sc)	mtx_assert(&_sc->sc_mtx, MA_OWNED);
#define PS3CDROM_ASSERT_UNLOCKED(_sc)	mtx_assert(&_sc->sc_mtx, MA_NOTOWNED);

#define PS3CDROM_MAX_XFERS		3

#define	LV1_STORAGE_SEND_ATAPI_COMMAND	0x01

struct ps3cdrom_softc;

struct ps3cdrom_xfer {
	TAILQ_ENTRY(ps3cdrom_xfer) x_queue;
	struct ps3cdrom_softc *x_sc;
	union ccb *x_ccb;
	bus_dmamap_t x_dmamap;
	uint64_t x_tag;
};

TAILQ_HEAD(ps3cdrom_xferq, ps3cdrom_xfer);

struct ps3cdrom_softc {
	device_t sc_dev;

	struct mtx sc_mtx;

	uint64_t sc_blksize;
	uint64_t sc_nblocks;

	int sc_irqid;
	struct resource	*sc_irq;
	void *sc_irqctx;

	bus_dma_tag_t sc_dmatag;

	struct cam_sim *sc_sim;
	struct cam_path *sc_path;

	struct ps3cdrom_xfer sc_xfer[PS3CDROM_MAX_XFERS];
	struct ps3cdrom_xferq sc_active_xferq;
	struct ps3cdrom_xferq sc_free_xferq;
};

enum lv1_ata_proto {
	NON_DATA_PROTO		= 0x00,
	PIO_DATA_IN_PROTO	= 0x01,
	PIO_DATA_OUT_PROTO	= 0x02,
	DMA_PROTO		= 0x03
};

enum lv1_ata_in_out {
	DIR_WRITE		= 0x00,
	DIR_READ		= 0x01
};

struct lv1_atapi_cmd {
	uint8_t pkt[32];
	uint32_t pktlen;
	uint32_t nblocks;
	uint32_t blksize;
	uint32_t proto;		/* enum lv1_ata_proto */
	uint32_t in_out;	/* enum lv1_ata_in_out */
	uint64_t buf;
	uint32_t arglen;
};

static void ps3cdrom_action(struct cam_sim *sim, union ccb *ccb);
static void ps3cdrom_poll(struct cam_sim *sim);
static void ps3cdrom_async(void *callback_arg, u_int32_t code,
    struct cam_path* path, void *arg);

static void ps3cdrom_intr(void *arg);

static void ps3cdrom_transfer(void *arg, bus_dma_segment_t *segs, int nsegs,
    int error);

static int ps3cdrom_decode_lv1_status(uint64_t status,
	u_int8_t *sense_key, u_int8_t *asc, u_int8_t *ascq);

static int
ps3cdrom_probe(device_t dev)
{
	if (ps3bus_get_bustype(dev) != PS3_BUSTYPE_STORAGE ||
	    ps3bus_get_devtype(dev) != PS3_DEVTYPE_CDROM)
		return (ENXIO);

	device_set_desc(dev, "Playstation 3 CDROM");

	return (BUS_PROBE_SPECIFIC);
}

static int
ps3cdrom_attach(device_t dev)
{
	struct ps3cdrom_softc *sc = device_get_softc(dev);
	struct cam_devq *devq;
	struct ps3cdrom_xfer *xp;
	struct ccb_setasync csa;
	int i, err;

	sc->sc_dev = dev;

	PS3CDROM_LOCK_INIT(sc);

	/* Setup interrupt handler */

	sc->sc_irqid = 0;
	sc->sc_irq = bus_alloc_resource_any(dev, SYS_RES_IRQ, &sc->sc_irqid,
	    RF_ACTIVE);
	if (!sc->sc_irq) {
		device_printf(dev, "Could not allocate IRQ\n");
		err = ENXIO;
		goto fail_destroy_lock;
	}

	err = bus_setup_intr(dev, sc->sc_irq,
	    INTR_TYPE_CAM | INTR_MPSAFE | INTR_ENTROPY,
	    NULL, ps3cdrom_intr, sc, &sc->sc_irqctx);
	if (err) {
		device_printf(dev, "Could not setup IRQ\n");
		err = ENXIO;
		goto fail_release_intr;
	}

	/* Setup DMA */

	err = bus_dma_tag_create(bus_get_dma_tag(dev), 4096, 0,
	    BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR, NULL, NULL,
	    BUS_SPACE_UNRESTRICTED, 1, PAGE_SIZE, 0,
	    busdma_lock_mutex, &sc->sc_mtx, &sc->sc_dmatag);
	if (err) {
		device_printf(dev, "Could not create DMA tag\n");
		err = ENXIO;
		goto fail_teardown_intr;
	}

	/* Setup transfer queues */

	TAILQ_INIT(&sc->sc_active_xferq);
	TAILQ_INIT(&sc->sc_free_xferq);

	for (i = 0; i < PS3CDROM_MAX_XFERS; i++) {
		xp = &sc->sc_xfer[i];
		xp->x_sc = sc;

		err = bus_dmamap_create(sc->sc_dmatag, BUS_DMA_COHERENT,
		    &xp->x_dmamap);
		if (err) {
			device_printf(dev, "Could not create DMA map (%d)\n",
			    err);
			goto fail_destroy_dmamap;
		}

		TAILQ_INSERT_TAIL(&sc->sc_free_xferq, xp, x_queue);
	}

	/* Setup CAM */

	devq = cam_simq_alloc(PS3CDROM_MAX_XFERS - 1);
	if (!devq) {
		device_printf(dev, "Could not allocate SIM queue\n");
		err = ENOMEM;
		goto fail_destroy_dmatag;
	}

	sc->sc_sim = cam_sim_alloc(ps3cdrom_action, ps3cdrom_poll, "ps3cdrom",
	    sc, device_get_unit(dev), &sc->sc_mtx, PS3CDROM_MAX_XFERS - 1, 0,
	    devq);
	if (!sc->sc_sim) {
		device_printf(dev, "Could not allocate SIM\n");
		cam_simq_free(devq);
		err = ENOMEM;
		goto fail_destroy_dmatag;
	}

	/* Setup XPT */

	PS3CDROM_LOCK(sc);

	err = xpt_bus_register(sc->sc_sim, dev, 0);
	if (err != CAM_SUCCESS) {
		device_printf(dev, "Could not register XPT bus\n");
		err = ENXIO;
		PS3CDROM_UNLOCK(sc);
		goto fail_free_sim;
	}

	err = xpt_create_path(&sc->sc_path, NULL, cam_sim_path(sc->sc_sim),
	    CAM_TARGET_WILDCARD, CAM_LUN_WILDCARD);
	if (err != CAM_REQ_CMP) {
		device_printf(dev, "Could not create XPT path\n");
		err = ENOMEM;
		PS3CDROM_UNLOCK(sc);
		goto fail_unregister_xpt_bus;
	}

	xpt_setup_ccb(&csa.ccb_h, sc->sc_path, 5);
	csa.ccb_h.func_code = XPT_SASYNC_CB;
	csa.event_enable = AC_LOST_DEVICE;
	csa.callback = ps3cdrom_async;
	csa.callback_arg = sc->sc_sim;
	xpt_action((union ccb *) &csa);

	CAM_DEBUG(sc->sc_path, CAM_DEBUG_TRACE,
	    ("registered SIM for ps3cdrom%d\n", device_get_unit(dev)));

	PS3CDROM_UNLOCK(sc);

	return (BUS_PROBE_SPECIFIC);

fail_unregister_xpt_bus:

	xpt_bus_deregister(cam_sim_path(sc->sc_sim));

fail_free_sim:

	cam_sim_free(sc->sc_sim, TRUE);

fail_destroy_dmamap:

	while ((xp = TAILQ_FIRST(&sc->sc_free_xferq))) {
		TAILQ_REMOVE(&sc->sc_free_xferq, xp, x_queue);
		bus_dmamap_destroy(sc->sc_dmatag, xp->x_dmamap);
	}

fail_destroy_dmatag:

	bus_dma_tag_destroy(sc->sc_dmatag);

fail_teardown_intr:

	bus_teardown_intr(dev, sc->sc_irq, sc->sc_irqctx);

fail_release_intr:

	bus_release_resource(dev, SYS_RES_IRQ, sc->sc_irqid, sc->sc_irq);

fail_destroy_lock:

	PS3CDROM_LOCK_DESTROY(sc);

	return (err);
}

static int
ps3cdrom_detach(device_t dev)
{
	struct ps3cdrom_softc *sc = device_get_softc(dev);
	int i;

	xpt_async(AC_LOST_DEVICE, sc->sc_path, NULL);
	xpt_free_path(sc->sc_path);
	xpt_bus_deregister(cam_sim_path(sc->sc_sim));
	cam_sim_free(sc->sc_sim, TRUE);

	for (i = 0; i < PS3CDROM_MAX_XFERS; i++)
		bus_dmamap_destroy(sc->sc_dmatag, sc->sc_xfer[i].x_dmamap);

	bus_dma_tag_destroy(sc->sc_dmatag);

	bus_teardown_intr(dev, sc->sc_irq, sc->sc_irqctx);
	bus_release_resource(dev, SYS_RES_IRQ, sc->sc_irqid, sc->sc_irq);

	PS3CDROM_LOCK_DESTROY(sc);

	return (0);
}

static void
ps3cdrom_action(struct cam_sim *sim, union ccb *ccb)
{
	struct ps3cdrom_softc *sc = (struct ps3cdrom_softc *)cam_sim_softc(sim);
	device_t dev = sc->sc_dev;
	struct ps3cdrom_xfer *xp;
	int err;

	PS3CDROM_ASSERT_LOCKED(sc);

	CAM_DEBUG(ccb->ccb_h.path, CAM_DEBUG_TRACE,
	   ("function code 0x%02x\n", ccb->ccb_h.func_code));

	switch (ccb->ccb_h.func_code) {
	case XPT_SCSI_IO:
		if ((ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_INPROG)
			break;

		if(ccb->ccb_h.target_id > 0) {
			ccb->ccb_h.status = CAM_TID_INVALID;
			break;
		}

		if(ccb->ccb_h.target_lun > 0) {
			ccb->ccb_h.status = CAM_LUN_INVALID;
			break;
		}

		xp = TAILQ_FIRST(&sc->sc_free_xferq);
		
		KASSERT(xp != NULL, ("no free transfers"));

		xp->x_ccb = ccb;

		TAILQ_REMOVE(&sc->sc_free_xferq, xp, x_queue);

		err = bus_dmamap_load_ccb(sc->sc_dmatag, xp->x_dmamap,
		    ccb, ps3cdrom_transfer, xp, 0);
		if (err && err != EINPROGRESS) {
			device_printf(dev, "Could not load DMA map (%d)\n",
			    err);

			xp->x_ccb = NULL;
			TAILQ_INSERT_TAIL(&sc->sc_free_xferq, xp, x_queue);
			ccb->ccb_h.status = CAM_SCSI_STATUS_ERROR;
			break;
		}
		return;
	case XPT_SET_TRAN_SETTINGS:
		ccb->ccb_h.status = CAM_FUNC_NOTAVAIL;
		break;
	case XPT_GET_TRAN_SETTINGS:
	{
		struct ccb_trans_settings *cts = &ccb->cts;

		cts->protocol = PROTO_SCSI;
		cts->protocol_version = SCSI_REV_2;
		cts->transport = XPORT_SPI;
		cts->transport_version = 2;
		cts->proto_specific.valid = 0;
		cts->xport_specific.valid = 0;
		ccb->ccb_h.status = CAM_REQ_CMP;
		break;
	}
	case XPT_RESET_BUS:
	case XPT_RESET_DEV:
		ccb->ccb_h.status = CAM_REQ_CMP;
		break;
	case XPT_CALC_GEOMETRY:
		cam_calc_geometry(&ccb->ccg, 1);
		break;
	case XPT_PATH_INQ:
	{
		struct ccb_pathinq *cpi = &ccb->cpi;

		cpi->version_num = 1;
		cpi->hba_inquiry = 0;
		cpi->target_sprt = 0;
		cpi->hba_inquiry = PI_SDTR_ABLE;
		cpi->hba_misc = PIM_NOBUSRESET | PIM_SEQSCAN | PIM_NO_6_BYTE;
		cpi->hba_eng_cnt = 0;
		bzero(cpi->vuhba_flags, sizeof(cpi->vuhba_flags));
		cpi->max_target = 0;
		cpi->max_lun = 0;
		cpi->initiator_id = 7;
		cpi->bus_id = cam_sim_bus(sim);
		cpi->unit_number = cam_sim_unit(sim);
		cpi->base_transfer_speed = 150000;
		strlcpy(cpi->sim_vid, "FreeBSD", SIM_IDLEN);
		strlcpy(cpi->hba_vid, "Sony", HBA_IDLEN);
		strlcpy(cpi->dev_name, cam_sim_name(sim), DEV_IDLEN);
		cpi->transport = XPORT_SPI;
		cpi->transport_version = 2;
		cpi->protocol = PROTO_SCSI;
		cpi->protocol_version = SCSI_REV_2;
		cpi->maxio = PAGE_SIZE;
		cpi->ccb_h.status = CAM_REQ_CMP;
		break;
	}
	default:
		CAM_DEBUG(ccb->ccb_h.path, CAM_DEBUG_TRACE,
		    ("unsupported function code 0x%02x\n",
		    ccb->ccb_h.func_code));
		ccb->ccb_h.status = CAM_REQ_INVALID;
		break;
	}

	xpt_done(ccb);
}

static void
ps3cdrom_poll(struct cam_sim *sim)
{
	ps3cdrom_intr(cam_sim_softc(sim));
}

static void
ps3cdrom_async(void *callback_arg, u_int32_t code,
	struct cam_path* path, void *arg)
{
	switch (code) {
	case AC_LOST_DEVICE:
		xpt_print_path(path);
		break;
	default:
		break;
	}
}

static void
ps3cdrom_intr(void *arg)
{
	struct ps3cdrom_softc *sc = (struct ps3cdrom_softc *) arg;
	device_t dev = sc->sc_dev;
	uint64_t devid = ps3bus_get_device(dev);
	struct ps3cdrom_xfer *xp;
	union ccb *ccb;
	u_int8_t *cdb, sense_key, asc, ascq;
	uint64_t tag, status;

	if (lv1_storage_get_async_status(devid, &tag, &status) != 0)
		return;

	PS3CDROM_LOCK(sc);

	/* Find transfer with the returned tag */

	TAILQ_FOREACH(xp, &sc->sc_active_xferq, x_queue) {
		if (xp->x_tag == tag)
			break;
	}

	if (xp) {
		ccb = xp->x_ccb;
		cdb = (ccb->ccb_h.flags & CAM_CDB_POINTER) ?
			    ccb->csio.cdb_io.cdb_ptr :
			    ccb->csio.cdb_io.cdb_bytes;

		CAM_DEBUG(ccb->ccb_h.path, CAM_DEBUG_TRACE,
		   ("ATAPI command 0x%02x tag 0x%016lx completed (0x%016lx)\n",
		    cdb[0], tag, status));

		if (!status) {
			ccb->csio.scsi_status = SCSI_STATUS_OK;
			ccb->csio.resid = 0;
			ccb->ccb_h.status = CAM_REQ_CMP;
		} else {
			ccb->csio.scsi_status = SCSI_STATUS_CHECK_COND;
			ccb->ccb_h.status = CAM_SCSI_STATUS_ERROR;

			if (!ps3cdrom_decode_lv1_status(status, &sense_key,
			    &asc, &ascq)) {

				CAM_DEBUG(ccb->ccb_h.path, CAM_DEBUG_TRACE,
				   ("sense key 0x%02x asc 0x%02x ascq 0x%02x\n",
				    sense_key, asc, ascq));

				scsi_set_sense_data(&ccb->csio.sense_data,
				    /*sense_format*/ SSD_TYPE_NONE,
				    /*current_error*/ 1,
				    sense_key,
				    asc,
				    ascq,
				    SSD_ELEM_NONE);
				ccb->csio.sense_len = SSD_FULL_SIZE;
				ccb->ccb_h.status = CAM_SCSI_STATUS_ERROR |
				    CAM_AUTOSNS_VALID;
			}

			if ((ccb->ccb_h.flags & CAM_DIR_MASK) != CAM_DIR_NONE)
				ccb->csio.resid = ccb->csio.dxfer_len;
		}

		if (ccb->ccb_h.flags & CAM_DIR_IN)
			bus_dmamap_sync(sc->sc_dmatag, xp->x_dmamap,
			    BUS_DMASYNC_POSTREAD);

		bus_dmamap_unload(sc->sc_dmatag, xp->x_dmamap);

		xp->x_ccb = NULL;
		TAILQ_REMOVE(&sc->sc_active_xferq, xp, x_queue);
		TAILQ_INSERT_TAIL(&sc->sc_free_xferq, xp, x_queue);

		xpt_done(ccb);
	} else {
		device_printf(dev,
		    "Could not find transfer with tag 0x%016lx\n",  tag);
	}

	PS3CDROM_UNLOCK(sc);
}

static void
ps3cdrom_transfer(void *arg, bus_dma_segment_t *segs, int nsegs, int error)
{
	struct ps3cdrom_xfer *xp = (struct ps3cdrom_xfer *) arg;
	struct ps3cdrom_softc *sc = xp->x_sc;
	device_t dev = sc->sc_dev;
	uint64_t devid = ps3bus_get_device(dev);
	union ccb *ccb = xp->x_ccb;
	u_int8_t *cdb;
	uint64_t start_sector, block_count;
	int err;

	KASSERT(nsegs == 1 || nsegs == 0,
	    ("ps3cdrom_transfer: invalid number of DMA segments %d", nsegs));
	KASSERT(error == 0, ("ps3cdrom_transfer: DMA error %d", error));

	PS3CDROM_ASSERT_LOCKED(sc);

	if (error) {
		device_printf(dev, "Could not load DMA map (%d)\n",  error);

		xp->x_ccb = NULL;
		TAILQ_INSERT_TAIL(&sc->sc_free_xferq, xp, x_queue);
		ccb->ccb_h.status = CAM_SCSI_STATUS_ERROR;
		xpt_done(ccb);
		return;
	}

	cdb = (ccb->ccb_h.flags & CAM_CDB_POINTER) ?
		    ccb->csio.cdb_io.cdb_ptr :
		    ccb->csio.cdb_io.cdb_bytes;

	CAM_DEBUG(ccb->ccb_h.path, CAM_DEBUG_TRACE,
	   ("ATAPI command 0x%02x cdb_len %d dxfer_len %d\n ", cdb[0],
	    ccb->csio.cdb_len, ccb->csio.dxfer_len));

	switch (cdb[0]) {
	case READ_10:
		KASSERT(nsegs == 1, ("ps3cdrom_transfer: no data to read"));
		start_sector = (cdb[2] << 24) | (cdb[3] << 16) |
		    (cdb[4] << 8) | cdb[5];
		block_count = (cdb[7] << 8) | cdb[8];

		err = lv1_storage_read(devid, 0 /* region id */,
		    start_sector, block_count, 0 /* flags */, segs[0].ds_addr,
		    &xp->x_tag);
		bus_dmamap_sync(sc->sc_dmatag, xp->x_dmamap,
		    BUS_DMASYNC_POSTREAD);
		break;
	case WRITE_10:
		KASSERT(nsegs == 1, ("ps3cdrom_transfer: no data to write"));
		start_sector = (cdb[2] << 24) | (cdb[3] << 16) |
		    (cdb[4] << 8) | cdb[5];
		block_count = (cdb[7] << 8) | cdb[8];

		bus_dmamap_sync(sc->sc_dmatag, xp->x_dmamap,
		    BUS_DMASYNC_PREWRITE);
		err = lv1_storage_write(devid, 0 /* region id */,
		    start_sector, block_count, 0 /* flags */,
		    segs[0].ds_addr, &xp->x_tag);
		break;
	default:
		{
		struct lv1_atapi_cmd atapi_cmd;

		bzero(&atapi_cmd, sizeof(atapi_cmd));
		atapi_cmd.pktlen = 12;
		bcopy(cdb, atapi_cmd.pkt, ccb->csio.cdb_len);

		if (ccb->ccb_h.flags & CAM_DIR_IN) {
			atapi_cmd.in_out = DIR_READ;
			atapi_cmd.proto = (ccb->csio.dxfer_len >= 2048) ?
			    DMA_PROTO : PIO_DATA_IN_PROTO;
		} else if (ccb->ccb_h.flags & CAM_DIR_OUT) {
			atapi_cmd.in_out = DIR_WRITE;
			atapi_cmd.proto = (ccb->csio.dxfer_len >= 2048) ?
			    DMA_PROTO : PIO_DATA_OUT_PROTO;
		} else {
			atapi_cmd.proto = NON_DATA_PROTO;
		}

		atapi_cmd.nblocks = atapi_cmd.arglen =
		    (nsegs == 0) ? 0 : segs[0].ds_len;
		atapi_cmd.blksize = 1;
		atapi_cmd.buf = (nsegs == 0) ? 0 : segs[0].ds_addr;

		if (ccb->ccb_h.flags & CAM_DIR_OUT)
			bus_dmamap_sync(sc->sc_dmatag, xp->x_dmamap,
			    BUS_DMASYNC_PREWRITE);

		err = lv1_storage_send_device_command(devid,
		    LV1_STORAGE_SEND_ATAPI_COMMAND, vtophys(&atapi_cmd),
		    sizeof(atapi_cmd), atapi_cmd.buf, atapi_cmd.arglen,
		    &xp->x_tag);
	
		break;
		}
	}

	if (err) {
		device_printf(dev, "ATAPI command 0x%02x failed (%d)\n",
		    cdb[0], err);

		bus_dmamap_unload(sc->sc_dmatag, xp->x_dmamap);

		xp->x_ccb = NULL;
		TAILQ_INSERT_TAIL(&sc->sc_free_xferq, xp, x_queue);

		bzero(&ccb->csio.sense_data, sizeof(ccb->csio.sense_data));
		/* Invalid field in parameter list */
		scsi_set_sense_data(&ccb->csio.sense_data,
				    /*sense_format*/ SSD_TYPE_NONE,
				    /*current_error*/ 1,
				    /*sense_key*/ SSD_KEY_ILLEGAL_REQUEST,
				    /*asc*/ 0x26,
				    /*ascq*/ 0x00,
				    SSD_ELEM_NONE);

		ccb->csio.sense_len = SSD_FULL_SIZE;
		ccb->csio.scsi_status = SCSI_STATUS_CHECK_COND;
		ccb->ccb_h.status = CAM_SCSI_STATUS_ERROR | CAM_AUTOSNS_VALID;
		xpt_done(ccb);
	} else {
		CAM_DEBUG(ccb->ccb_h.path, CAM_DEBUG_TRACE,
		   ("ATAPI command 0x%02x tag 0x%016lx submitted\n ", cdb[0],
		   xp->x_tag));

		TAILQ_INSERT_TAIL(&sc->sc_active_xferq, xp, x_queue);
		ccb->ccb_h.status |= CAM_SIM_QUEUED;
	}
}

static int
ps3cdrom_decode_lv1_status(uint64_t status, u_int8_t *sense_key, u_int8_t *asc,
    u_int8_t *ascq)
{
	if (((status >> 24) & 0xff) != SCSI_STATUS_CHECK_COND)
		return -1;

	*sense_key = (status >> 16) & 0xff;
	*asc = (status >> 8) & 0xff;
	*ascq = status & 0xff;

	return (0);
}

static device_method_t ps3cdrom_methods[] = {
	DEVMETHOD(device_probe,		ps3cdrom_probe),
	DEVMETHOD(device_attach,	ps3cdrom_attach),
	DEVMETHOD(device_detach,	ps3cdrom_detach),
	{0, 0},
};

static driver_t ps3cdrom_driver = {
	"ps3cdrom",
	ps3cdrom_methods,
	sizeof(struct ps3cdrom_softc),
};

static devclass_t ps3cdrom_devclass;

DRIVER_MODULE(ps3cdrom, ps3bus, ps3cdrom_driver, ps3cdrom_devclass, 0, 0);
MODULE_DEPEND(ps3cdrom, cam, 1, 1, 1);
