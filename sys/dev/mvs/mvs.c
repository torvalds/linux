/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010 Alexander Motin <mav@FreeBSD.org>
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
#include <sys/endian.h>
#include <sys/malloc.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <vm/uma.h>
#include <machine/stdarg.h>
#include <machine/resource.h>
#include <machine/bus.h>
#include <sys/rman.h>
#include <dev/pci/pcivar.h>
#include "mvs.h"

#include <cam/cam.h>
#include <cam/cam_ccb.h>
#include <cam/cam_sim.h>
#include <cam/cam_xpt_sim.h>
#include <cam/cam_debug.h>

/* local prototypes */
static int mvs_ch_init(device_t dev);
static int mvs_ch_deinit(device_t dev);
static int mvs_ch_suspend(device_t dev);
static int mvs_ch_resume(device_t dev);
static void mvs_dmainit(device_t dev);
static void mvs_dmasetupc_cb(void *xsc,
	bus_dma_segment_t *segs, int nsegs, int error);
static void mvs_dmafini(device_t dev);
static void mvs_slotsalloc(device_t dev);
static void mvs_slotsfree(device_t dev);
static void mvs_setup_edma_queues(device_t dev);
static void mvs_set_edma_mode(device_t dev, enum mvs_edma_mode mode);
static void mvs_ch_pm(void *arg);
static void mvs_ch_intr_locked(void *data);
static void mvs_ch_intr(void *data);
static void mvs_reset(device_t dev);
static void mvs_softreset(device_t dev, union ccb *ccb);

static int mvs_sata_connect(struct mvs_channel *ch);
static int mvs_sata_phy_reset(device_t dev);
static int mvs_wait(device_t dev, u_int s, u_int c, int t);
static void mvs_tfd_read(device_t dev, union ccb *ccb);
static void mvs_tfd_write(device_t dev, union ccb *ccb);
static void mvs_legacy_intr(device_t dev, int poll);
static void mvs_crbq_intr(device_t dev);
static void mvs_begin_transaction(device_t dev, union ccb *ccb);
static void mvs_legacy_execute_transaction(struct mvs_slot *slot);
static void mvs_timeout(struct mvs_slot *slot);
static void mvs_dmasetprd(void *arg,
	bus_dma_segment_t *segs, int nsegs, int error);
static void mvs_requeue_frozen(device_t dev);
static void mvs_execute_transaction(struct mvs_slot *slot);
static void mvs_end_transaction(struct mvs_slot *slot, enum mvs_err_type et);

static void mvs_issue_recovery(device_t dev);
static void mvs_process_read_log(device_t dev, union ccb *ccb);
static void mvs_process_request_sense(device_t dev, union ccb *ccb);

static void mvsaction(struct cam_sim *sim, union ccb *ccb);
static void mvspoll(struct cam_sim *sim);

static MALLOC_DEFINE(M_MVS, "MVS driver", "MVS driver data buffers");

#define recovery_type		spriv_field0
#define RECOVERY_NONE		0
#define RECOVERY_READ_LOG	1
#define RECOVERY_REQUEST_SENSE	2
#define recovery_slot		spriv_field1

static int
mvs_ch_probe(device_t dev)
{

	device_set_desc_copy(dev, "Marvell SATA channel");
	return (BUS_PROBE_DEFAULT);
}

static int
mvs_ch_attach(device_t dev)
{
	struct mvs_controller *ctlr = device_get_softc(device_get_parent(dev));
	struct mvs_channel *ch = device_get_softc(dev);
	struct cam_devq *devq;
	int rid, error, i, sata_rev = 0;

	ch->dev = dev;
	ch->unit = (intptr_t)device_get_ivars(dev);
	ch->quirks = ctlr->quirks;
	mtx_init(&ch->mtx, "MVS channel lock", NULL, MTX_DEF);
	ch->pm_level = 0;
	resource_int_value(device_get_name(dev),
	    device_get_unit(dev), "pm_level", &ch->pm_level);
	if (ch->pm_level > 3)
		callout_init_mtx(&ch->pm_timer, &ch->mtx, 0);
	callout_init_mtx(&ch->reset_timer, &ch->mtx, 0);
	resource_int_value(device_get_name(dev),
	    device_get_unit(dev), "sata_rev", &sata_rev);
	for (i = 0; i < 16; i++) {
		ch->user[i].revision = sata_rev;
		ch->user[i].mode = 0;
		ch->user[i].bytecount = (ch->quirks & MVS_Q_GENIIE) ? 8192 : 2048;
		ch->user[i].tags = MVS_MAX_SLOTS;
		ch->curr[i] = ch->user[i];
		if (ch->pm_level) {
			ch->user[i].caps = CTS_SATA_CAPS_H_PMREQ |
			    CTS_SATA_CAPS_H_APST |
			    CTS_SATA_CAPS_D_PMREQ | CTS_SATA_CAPS_D_APST;
		}
		ch->user[i].caps |= CTS_SATA_CAPS_H_AN;
	}
	rid = ch->unit;
	if (!(ch->r_mem = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &rid, RF_ACTIVE)))
		return (ENXIO);
	mvs_dmainit(dev);
	mvs_slotsalloc(dev);
	mvs_ch_init(dev);
	mtx_lock(&ch->mtx);
	rid = ATA_IRQ_RID;
	if (!(ch->r_irq = bus_alloc_resource_any(dev, SYS_RES_IRQ,
	    &rid, RF_SHAREABLE | RF_ACTIVE))) {
		device_printf(dev, "Unable to map interrupt\n");
		error = ENXIO;
		goto err0;
	}
	if ((bus_setup_intr(dev, ch->r_irq, ATA_INTR_FLAGS, NULL,
	    mvs_ch_intr_locked, dev, &ch->ih))) {
		device_printf(dev, "Unable to setup interrupt\n");
		error = ENXIO;
		goto err1;
	}
	/* Create the device queue for our SIM. */
	devq = cam_simq_alloc(MVS_MAX_SLOTS - 1);
	if (devq == NULL) {
		device_printf(dev, "Unable to allocate simq\n");
		error = ENOMEM;
		goto err1;
	}
	/* Construct SIM entry */
	ch->sim = cam_sim_alloc(mvsaction, mvspoll, "mvsch", ch,
	    device_get_unit(dev), &ch->mtx,
	    2, (ch->quirks & MVS_Q_GENI) ? 0 : MVS_MAX_SLOTS - 1,
	    devq);
	if (ch->sim == NULL) {
		cam_simq_free(devq);
		device_printf(dev, "unable to allocate sim\n");
		error = ENOMEM;
		goto err1;
	}
	if (xpt_bus_register(ch->sim, dev, 0) != CAM_SUCCESS) {
		device_printf(dev, "unable to register xpt bus\n");
		error = ENXIO;
		goto err2;
	}
	if (xpt_create_path(&ch->path, /*periph*/NULL, cam_sim_path(ch->sim),
	    CAM_TARGET_WILDCARD, CAM_LUN_WILDCARD) != CAM_REQ_CMP) {
		device_printf(dev, "unable to create path\n");
		error = ENXIO;
		goto err3;
	}
	if (ch->pm_level > 3) {
		callout_reset(&ch->pm_timer,
		    (ch->pm_level == 4) ? hz / 1000 : hz / 8,
		    mvs_ch_pm, dev);
	}
	mtx_unlock(&ch->mtx);
	return (0);

err3:
	xpt_bus_deregister(cam_sim_path(ch->sim));
err2:
	cam_sim_free(ch->sim, /*free_devq*/TRUE);
err1:
	bus_release_resource(dev, SYS_RES_IRQ, ATA_IRQ_RID, ch->r_irq);
err0:
	bus_release_resource(dev, SYS_RES_MEMORY, ch->unit, ch->r_mem);
	mtx_unlock(&ch->mtx);
	mtx_destroy(&ch->mtx);
	return (error);
}

static int
mvs_ch_detach(device_t dev)
{
	struct mvs_channel *ch = device_get_softc(dev);

	mtx_lock(&ch->mtx);
	xpt_async(AC_LOST_DEVICE, ch->path, NULL);
	/* Forget about reset. */
	if (ch->resetting) {
		ch->resetting = 0;
		xpt_release_simq(ch->sim, TRUE);
	}
	xpt_free_path(ch->path);
	xpt_bus_deregister(cam_sim_path(ch->sim));
	cam_sim_free(ch->sim, /*free_devq*/TRUE);
	mtx_unlock(&ch->mtx);

	if (ch->pm_level > 3)
		callout_drain(&ch->pm_timer);
	callout_drain(&ch->reset_timer);
	bus_teardown_intr(dev, ch->r_irq, ch->ih);
	bus_release_resource(dev, SYS_RES_IRQ, ATA_IRQ_RID, ch->r_irq);

	mvs_ch_deinit(dev);
	mvs_slotsfree(dev);
	mvs_dmafini(dev);

	bus_release_resource(dev, SYS_RES_MEMORY, ch->unit, ch->r_mem);
	mtx_destroy(&ch->mtx);
	return (0);
}

static int
mvs_ch_init(device_t dev)
{
	struct mvs_channel *ch = device_get_softc(dev);
	uint32_t reg;

	/* Disable port interrupts */
	ATA_OUTL(ch->r_mem, EDMA_IEM, 0);
	/* Stop EDMA */
	ch->curr_mode = MVS_EDMA_UNKNOWN;
	mvs_set_edma_mode(dev, MVS_EDMA_OFF);
	/* Clear and configure FIS interrupts. */
	ATA_OUTL(ch->r_mem, SATA_FISIC, 0);
	reg = ATA_INL(ch->r_mem, SATA_FISC);
	reg |= SATA_FISC_FISWAIT4HOSTRDYEN_B1;
	ATA_OUTL(ch->r_mem, SATA_FISC, reg);
	reg = ATA_INL(ch->r_mem, SATA_FISIM);
	reg |= SATA_FISC_FISWAIT4HOSTRDYEN_B1;
	ATA_OUTL(ch->r_mem, SATA_FISC, reg);
	/* Clear SATA error register. */
	ATA_OUTL(ch->r_mem, SATA_SE, 0xffffffff);
	/* Clear any outstanding error interrupts. */
	ATA_OUTL(ch->r_mem, EDMA_IEC, 0);
	/* Unmask all error interrupts */
	ATA_OUTL(ch->r_mem, EDMA_IEM, ~EDMA_IE_TRANSIENT);
	return (0);
}

static int
mvs_ch_deinit(device_t dev)
{
	struct mvs_channel *ch = device_get_softc(dev);

	/* Stop EDMA */
	mvs_set_edma_mode(dev, MVS_EDMA_OFF);
	/* Disable port interrupts. */
	ATA_OUTL(ch->r_mem, EDMA_IEM, 0);
	return (0);
}

static int
mvs_ch_suspend(device_t dev)
{
	struct mvs_channel *ch = device_get_softc(dev);

	mtx_lock(&ch->mtx);
	xpt_freeze_simq(ch->sim, 1);
	while (ch->oslots)
		msleep(ch, &ch->mtx, PRIBIO, "mvssusp", hz/100);
	/* Forget about reset. */
	if (ch->resetting) {
		ch->resetting = 0;
		callout_stop(&ch->reset_timer);
		xpt_release_simq(ch->sim, TRUE);
	}
	mvs_ch_deinit(dev);
	mtx_unlock(&ch->mtx);
	return (0);
}

static int
mvs_ch_resume(device_t dev)
{
	struct mvs_channel *ch = device_get_softc(dev);

	mtx_lock(&ch->mtx);
	mvs_ch_init(dev);
	mvs_reset(dev);
	xpt_release_simq(ch->sim, TRUE);
	mtx_unlock(&ch->mtx);
	return (0);
}

struct mvs_dc_cb_args {
	bus_addr_t maddr;
	int error;
};

static void
mvs_dmainit(device_t dev)
{
	struct mvs_channel *ch = device_get_softc(dev);
	struct mvs_dc_cb_args dcba;

	/* EDMA command request area. */
	if (bus_dma_tag_create(bus_get_dma_tag(dev), 1024, 0,
	    BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR,
	    NULL, NULL, MVS_WORKRQ_SIZE, 1, MVS_WORKRQ_SIZE,
	    0, NULL, NULL, &ch->dma.workrq_tag))
		goto error;
	if (bus_dmamem_alloc(ch->dma.workrq_tag, (void **)&ch->dma.workrq, 0,
	    &ch->dma.workrq_map))
		goto error;
	if (bus_dmamap_load(ch->dma.workrq_tag, ch->dma.workrq_map,
	    ch->dma.workrq, MVS_WORKRQ_SIZE, mvs_dmasetupc_cb, &dcba, 0) ||
	    dcba.error) {
		bus_dmamem_free(ch->dma.workrq_tag,
		    ch->dma.workrq, ch->dma.workrq_map);
		goto error;
	}
	ch->dma.workrq_bus = dcba.maddr;
	/* EDMA command response area. */
	if (bus_dma_tag_create(bus_get_dma_tag(dev), 256, 0,
	    BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR,
	    NULL, NULL, MVS_WORKRP_SIZE, 1, MVS_WORKRP_SIZE,
	    0, NULL, NULL, &ch->dma.workrp_tag))
		goto error;
	if (bus_dmamem_alloc(ch->dma.workrp_tag, (void **)&ch->dma.workrp, 0,
	    &ch->dma.workrp_map))
		goto error;
	if (bus_dmamap_load(ch->dma.workrp_tag, ch->dma.workrp_map,
	    ch->dma.workrp, MVS_WORKRP_SIZE, mvs_dmasetupc_cb, &dcba, 0) ||
	    dcba.error) {
		bus_dmamem_free(ch->dma.workrp_tag,
		    ch->dma.workrp, ch->dma.workrp_map);
		goto error;
	}
	ch->dma.workrp_bus = dcba.maddr;
	/* Data area. */
	if (bus_dma_tag_create(bus_get_dma_tag(dev), 2, MVS_EPRD_MAX,
	    BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR,
	    NULL, NULL,
	    MVS_SG_ENTRIES * PAGE_SIZE * MVS_MAX_SLOTS,
	    MVS_SG_ENTRIES, MVS_EPRD_MAX,
	    0, busdma_lock_mutex, &ch->mtx, &ch->dma.data_tag)) {
		goto error;
	}
	return;

error:
	device_printf(dev, "WARNING - DMA initialization failed\n");
	mvs_dmafini(dev);
}

static void
mvs_dmasetupc_cb(void *xsc, bus_dma_segment_t *segs, int nsegs, int error)
{
	struct mvs_dc_cb_args *dcba = (struct mvs_dc_cb_args *)xsc;

	if (!(dcba->error = error))
		dcba->maddr = segs[0].ds_addr;
}

static void
mvs_dmafini(device_t dev)
{
	struct mvs_channel *ch = device_get_softc(dev);

	if (ch->dma.data_tag) {
		bus_dma_tag_destroy(ch->dma.data_tag);
		ch->dma.data_tag = NULL;
	}
	if (ch->dma.workrp_bus) {
		bus_dmamap_unload(ch->dma.workrp_tag, ch->dma.workrp_map);
		bus_dmamem_free(ch->dma.workrp_tag,
		    ch->dma.workrp, ch->dma.workrp_map);
		ch->dma.workrp_bus = 0;
		ch->dma.workrp = NULL;
	}
	if (ch->dma.workrp_tag) {
		bus_dma_tag_destroy(ch->dma.workrp_tag);
		ch->dma.workrp_tag = NULL;
	}
	if (ch->dma.workrq_bus) {
		bus_dmamap_unload(ch->dma.workrq_tag, ch->dma.workrq_map);
		bus_dmamem_free(ch->dma.workrq_tag,
		    ch->dma.workrq, ch->dma.workrq_map);
		ch->dma.workrq_bus = 0;
		ch->dma.workrq = NULL;
	}
	if (ch->dma.workrq_tag) {
		bus_dma_tag_destroy(ch->dma.workrq_tag);
		ch->dma.workrq_tag = NULL;
	}
}

static void
mvs_slotsalloc(device_t dev)
{
	struct mvs_channel *ch = device_get_softc(dev);
	int i;

	/* Alloc and setup command/dma slots */
	bzero(ch->slot, sizeof(ch->slot));
	for (i = 0; i < MVS_MAX_SLOTS; i++) {
		struct mvs_slot *slot = &ch->slot[i];

		slot->dev = dev;
		slot->slot = i;
		slot->state = MVS_SLOT_EMPTY;
		slot->ccb = NULL;
		callout_init_mtx(&slot->timeout, &ch->mtx, 0);

		if (bus_dmamap_create(ch->dma.data_tag, 0, &slot->dma.data_map))
			device_printf(ch->dev, "FAILURE - create data_map\n");
	}
}

static void
mvs_slotsfree(device_t dev)
{
	struct mvs_channel *ch = device_get_softc(dev);
	int i;

	/* Free all dma slots */
	for (i = 0; i < MVS_MAX_SLOTS; i++) {
		struct mvs_slot *slot = &ch->slot[i];

		callout_drain(&slot->timeout);
		if (slot->dma.data_map) {
			bus_dmamap_destroy(ch->dma.data_tag, slot->dma.data_map);
			slot->dma.data_map = NULL;
		}
	}
}

static void
mvs_setup_edma_queues(device_t dev)
{
	struct mvs_channel *ch = device_get_softc(dev);
	uint64_t work;

	/* Requests queue. */
	work = ch->dma.workrq_bus;
	ATA_OUTL(ch->r_mem, EDMA_REQQBAH, work >> 32);
	ATA_OUTL(ch->r_mem, EDMA_REQQIP, work & 0xffffffff);
	ATA_OUTL(ch->r_mem, EDMA_REQQOP, work & 0xffffffff);
	bus_dmamap_sync(ch->dma.workrq_tag, ch->dma.workrq_map,
	    BUS_DMASYNC_PREWRITE);
	/* Responses queue. */
	memset(ch->dma.workrp, 0xff, MVS_WORKRP_SIZE);
	work = ch->dma.workrp_bus;
	ATA_OUTL(ch->r_mem, EDMA_RESQBAH, work >> 32);
	ATA_OUTL(ch->r_mem, EDMA_RESQIP, work & 0xffffffff);
	ATA_OUTL(ch->r_mem, EDMA_RESQOP, work & 0xffffffff);
	bus_dmamap_sync(ch->dma.workrp_tag, ch->dma.workrp_map,
	    BUS_DMASYNC_PREREAD);
	ch->out_idx = 0;
	ch->in_idx = 0;
}

static void
mvs_set_edma_mode(device_t dev, enum mvs_edma_mode mode)
{
	struct mvs_channel *ch = device_get_softc(dev);
	int timeout;
	uint32_t ecfg, fcfg, hc, ltm, unkn;

	if (mode == ch->curr_mode)
		return;
	/* If we are running, we should stop first. */
	if (ch->curr_mode != MVS_EDMA_OFF) {
		ATA_OUTL(ch->r_mem, EDMA_CMD, EDMA_CMD_EDSEDMA);
		timeout = 0;
		while (ATA_INL(ch->r_mem, EDMA_CMD) & EDMA_CMD_EENEDMA) {
			DELAY(1000);
			if (timeout++ > 1000) {
				device_printf(dev, "stopping EDMA engine failed\n");
				break;
			}
		}
	}
	ch->curr_mode = mode;
	ch->fbs_enabled = 0;
	ch->fake_busy = 0;
	/* Report mode to controller. Needed for correct CCC operation. */
	MVS_EDMA(device_get_parent(dev), dev, mode);
	/* Configure new mode. */
	ecfg = EDMA_CFG_RESERVED | EDMA_CFG_RESERVED2 | EDMA_CFG_EHOSTQUEUECACHEEN;
	if (ch->pm_present) {
		ecfg |= EDMA_CFG_EMASKRXPM;
		if (ch->quirks & MVS_Q_GENIIE) {
			ecfg |= EDMA_CFG_EEDMAFBS;
			ch->fbs_enabled = 1;
		}
	}
	if (ch->quirks & MVS_Q_GENI)
		ecfg |= EDMA_CFG_ERDBSZ;
	else if (ch->quirks & MVS_Q_GENII)
		ecfg |= EDMA_CFG_ERDBSZEXT | EDMA_CFG_EWRBUFFERLEN;
	if (ch->quirks & MVS_Q_CT)
		ecfg |= EDMA_CFG_ECUTTHROUGHEN;
	if (mode != MVS_EDMA_OFF)
		ecfg |= EDMA_CFG_EEARLYCOMPLETIONEN;
	if (mode == MVS_EDMA_QUEUED)
		ecfg |= EDMA_CFG_EQUE;
	else if (mode == MVS_EDMA_NCQ)
		ecfg |= EDMA_CFG_ESATANATVCMDQUE;
	ATA_OUTL(ch->r_mem, EDMA_CFG, ecfg);
	mvs_setup_edma_queues(dev);
	if (ch->quirks & MVS_Q_GENIIE) {
		/* Configure FBS-related registers */
		fcfg = ATA_INL(ch->r_mem, SATA_FISC);
		ltm = ATA_INL(ch->r_mem, SATA_LTM);
		hc = ATA_INL(ch->r_mem, EDMA_HC);
		if (ch->fbs_enabled) {
			fcfg |= SATA_FISC_FISDMAACTIVATESYNCRESP;
			if (mode == MVS_EDMA_NCQ) {
				fcfg &= ~SATA_FISC_FISWAIT4HOSTRDYEN_B0;
				hc &= ~EDMA_IE_EDEVERR;
			} else {
				fcfg |= SATA_FISC_FISWAIT4HOSTRDYEN_B0;
				hc |= EDMA_IE_EDEVERR;
			}
			ltm |= (1 << 8);
		} else {
			fcfg &= ~SATA_FISC_FISDMAACTIVATESYNCRESP;
			fcfg &= ~SATA_FISC_FISWAIT4HOSTRDYEN_B0;
			hc |= EDMA_IE_EDEVERR;
			ltm &= ~(1 << 8);
		}
		ATA_OUTL(ch->r_mem, SATA_FISC, fcfg);
		ATA_OUTL(ch->r_mem, SATA_LTM, ltm);
		ATA_OUTL(ch->r_mem, EDMA_HC, hc);
		/* This is some magic, required to handle several DRQs
		 * with basic DMA. */
		unkn = ATA_INL(ch->r_mem, EDMA_UNKN_RESD);
		if (mode == MVS_EDMA_OFF)
			unkn |= 1;
		else
			unkn &= ~1;
		ATA_OUTL(ch->r_mem, EDMA_UNKN_RESD, unkn);
	}
	/* Run EDMA. */
	if (mode != MVS_EDMA_OFF)
		ATA_OUTL(ch->r_mem, EDMA_CMD, EDMA_CMD_EENEDMA);
}

devclass_t mvs_devclass;
devclass_t mvsch_devclass;
static device_method_t mvsch_methods[] = {
	DEVMETHOD(device_probe,     mvs_ch_probe),
	DEVMETHOD(device_attach,    mvs_ch_attach),
	DEVMETHOD(device_detach,    mvs_ch_detach),
	DEVMETHOD(device_suspend,   mvs_ch_suspend),
	DEVMETHOD(device_resume,    mvs_ch_resume),
	{ 0, 0 }
};
static driver_t mvsch_driver = {
        "mvsch",
        mvsch_methods,
        sizeof(struct mvs_channel)
};
DRIVER_MODULE(mvsch, mvs, mvsch_driver, mvsch_devclass, 0, 0);
DRIVER_MODULE(mvsch, sata, mvsch_driver, mvsch_devclass, 0, 0);

static void
mvs_phy_check_events(device_t dev, u_int32_t serr)
{
	struct mvs_channel *ch = device_get_softc(dev);

	if (ch->pm_level == 0) {
		u_int32_t status = ATA_INL(ch->r_mem, SATA_SS);
		union ccb *ccb;

		if (bootverbose) {
			if (((status & SATA_SS_DET_MASK) == SATA_SS_DET_PHY_ONLINE) &&
			    ((status & SATA_SS_SPD_MASK) != SATA_SS_SPD_NO_SPEED) &&
			    ((status & SATA_SS_IPM_MASK) == SATA_SS_IPM_ACTIVE)) {
				device_printf(dev, "CONNECT requested\n");
			} else
				device_printf(dev, "DISCONNECT requested\n");
		}
		mvs_reset(dev);
		if ((ccb = xpt_alloc_ccb_nowait()) == NULL)
			return;
		if (xpt_create_path(&ccb->ccb_h.path, NULL,
		    cam_sim_path(ch->sim),
		    CAM_TARGET_WILDCARD, CAM_LUN_WILDCARD) != CAM_REQ_CMP) {
			xpt_free_ccb(ccb);
			return;
		}
		xpt_rescan(ccb);
	}
}

static void
mvs_notify_events(device_t dev)
{
	struct mvs_channel *ch = device_get_softc(dev);
	struct cam_path *dpath;
	uint32_t fis;
	int d;

	/* Try to read PMP field from SDB FIS. Present only for Gen-IIe. */
	fis = ATA_INL(ch->r_mem, SATA_FISDW0);
	if ((fis & 0x80ff) == 0x80a1)
		d = (fis & 0x0f00) >> 8;
	else
		d = ch->pm_present ? 15 : 0;
	if (bootverbose)
		device_printf(dev, "SNTF %d\n", d);
	if (xpt_create_path(&dpath, NULL,
	    xpt_path_path_id(ch->path), d, 0) == CAM_REQ_CMP) {
		xpt_async(AC_SCSI_AEN, dpath, NULL);
		xpt_free_path(dpath);
	}
}

static void
mvs_ch_intr_locked(void *data)
{
	struct mvs_intr_arg *arg = (struct mvs_intr_arg *)data;
	device_t dev = (device_t)arg->arg;
	struct mvs_channel *ch = device_get_softc(dev);

	mtx_lock(&ch->mtx);
	mvs_ch_intr(data);
	mtx_unlock(&ch->mtx);
}

static void
mvs_ch_pm(void *arg)
{
	device_t dev = (device_t)arg;
	struct mvs_channel *ch = device_get_softc(dev);
	uint32_t work;

	if (ch->numrslots != 0)
		return;
	/* If we are idle - request power state transition. */
	work = ATA_INL(ch->r_mem, SATA_SC);
	work &= ~SATA_SC_SPM_MASK;
	if (ch->pm_level == 4)
		work |= SATA_SC_SPM_PARTIAL;
	else
		work |= SATA_SC_SPM_SLUMBER;
	ATA_OUTL(ch->r_mem, SATA_SC, work);
}

static void
mvs_ch_pm_wake(device_t dev)
{
	struct mvs_channel *ch = device_get_softc(dev);
	uint32_t work;
	int timeout = 0;

	work = ATA_INL(ch->r_mem, SATA_SS);
	if (work & SATA_SS_IPM_ACTIVE)
		return;
	/* If we are not in active state - request power state transition. */
	work = ATA_INL(ch->r_mem, SATA_SC);
	work &= ~SATA_SC_SPM_MASK;
	work |= SATA_SC_SPM_ACTIVE;
	ATA_OUTL(ch->r_mem, SATA_SC, work);
	/* Wait for transition to happen. */
	while ((ATA_INL(ch->r_mem, SATA_SS) & SATA_SS_IPM_ACTIVE) == 0 &&
	    timeout++ < 100) {
		DELAY(100);
	}
}

static void
mvs_ch_intr(void *data)
{
	struct mvs_intr_arg *arg = (struct mvs_intr_arg *)data;
	device_t dev = (device_t)arg->arg;
	struct mvs_channel *ch = device_get_softc(dev);
	uint32_t iec, serr = 0, fisic = 0;
	enum mvs_err_type et;
	int i, ccs, port = -1, selfdis = 0;
	int edma = (ch->numtslots != 0 || ch->numdslots != 0);

	/* New item in response queue. */
	if ((arg->cause & 2) && edma)
		mvs_crbq_intr(dev);
	/* Some error or special event. */
	if (arg->cause & 1) {
		iec = ATA_INL(ch->r_mem, EDMA_IEC);
		if (iec & EDMA_IE_SERRINT) {
			serr = ATA_INL(ch->r_mem, SATA_SE);
			ATA_OUTL(ch->r_mem, SATA_SE, serr);
		}
		/* EDMA self-disabled due to error. */
		if (iec & EDMA_IE_ESELFDIS)
			selfdis = 1;
		/* Transport interrupt. */
		if (iec & EDMA_IE_ETRANSINT) {
			/* For Gen-I this bit means self-disable. */
			if (ch->quirks & MVS_Q_GENI)
				selfdis = 1;
			/* For Gen-II this bit means SDB-N. */
			else if (ch->quirks & MVS_Q_GENII)
				fisic = SATA_FISC_FISWAIT4HOSTRDYEN_B1;
			else	/* For Gen-IIe - read FIS interrupt cause. */
				fisic = ATA_INL(ch->r_mem, SATA_FISIC);
		}
		if (selfdis)
			ch->curr_mode = MVS_EDMA_UNKNOWN;
		ATA_OUTL(ch->r_mem, EDMA_IEC, ~iec);
		/* Interface errors or Device error. */
		if (iec & (0xfc1e9000 | EDMA_IE_EDEVERR)) {
			port = -1;
			if (ch->numpslots != 0) {
				ccs = 0;
			} else {
				if (ch->quirks & MVS_Q_GENIIE)
					ccs = EDMA_S_EIOID(ATA_INL(ch->r_mem, EDMA_S));
				else
					ccs = EDMA_S_EDEVQUETAG(ATA_INL(ch->r_mem, EDMA_S));
				/* Check if error is one-PMP-port-specific, */
				if (ch->fbs_enabled) {
					/* Which ports were active. */
					for (i = 0; i < 16; i++) {
						if (ch->numrslotspd[i] == 0)
							continue;
						if (port == -1)
							port = i;
						else if (port != i) {
							port = -2;
							break;
						}
					}
					/* If several ports were active and EDMA still enabled - 
					 * other ports are probably unaffected and may continue.
					 */
					if (port == -2 && !selfdis) {
						uint16_t p = ATA_INL(ch->r_mem, SATA_SATAITC) >> 16;
						port = ffs(p) - 1;
						if (port != (fls(p) - 1))
							port = -2;
					}
				}
			}
			mvs_requeue_frozen(dev);
			for (i = 0; i < MVS_MAX_SLOTS; i++) {
				/* XXX: reqests in loading state. */
				if (((ch->rslots >> i) & 1) == 0)
					continue;
				if (port >= 0 &&
				    ch->slot[i].ccb->ccb_h.target_id != port)
					continue;
				if (iec & EDMA_IE_EDEVERR) { /* Device error. */
				    if (port != -2) {
					if (ch->numtslots == 0) {
						/* Untagged operation. */
						if (i == ccs)
							et = MVS_ERR_TFE;
						else
							et = MVS_ERR_INNOCENT;
					} else {
						/* Tagged operation. */
						et = MVS_ERR_NCQ;
					}
				    } else {
					et = MVS_ERR_TFE;
					ch->fatalerr = 1;
				    }
				} else if (iec & 0xfc1e9000) {
					if (ch->numtslots == 0 &&
					    i != ccs && port != -2)
						et = MVS_ERR_INNOCENT;
					else
						et = MVS_ERR_SATA;
				} else
					et = MVS_ERR_INVALID;
				mvs_end_transaction(&ch->slot[i], et);
			}
		}
		/* Process SDB-N. */
		if (fisic & SATA_FISC_FISWAIT4HOSTRDYEN_B1)
			mvs_notify_events(dev);
		if (fisic)
			ATA_OUTL(ch->r_mem, SATA_FISIC, ~fisic);
		/* Process hot-plug. */
		if ((iec & (EDMA_IE_EDEVDIS | EDMA_IE_EDEVCON)) ||
		    (serr & SATA_SE_PHY_CHANGED))
			mvs_phy_check_events(dev, serr);
	}
	/* Legacy mode device interrupt. */
	if ((arg->cause & 2) && !edma)
		mvs_legacy_intr(dev, arg->cause & 4);
}

static uint8_t
mvs_getstatus(device_t dev, int clear)
{
	struct mvs_channel *ch = device_get_softc(dev);
	uint8_t status = ATA_INB(ch->r_mem, clear ? ATA_STATUS : ATA_ALTSTAT);

	if (ch->fake_busy) {
		if (status & (ATA_S_BUSY | ATA_S_DRQ | ATA_S_ERROR))
			ch->fake_busy = 0;
		else
			status |= ATA_S_BUSY;
	}
	return (status);
}

static void
mvs_legacy_intr(device_t dev, int poll)
{
	struct mvs_channel *ch = device_get_softc(dev);
	struct mvs_slot *slot = &ch->slot[0]; /* PIO is always in slot 0. */
	union ccb *ccb = slot->ccb;
	enum mvs_err_type et = MVS_ERR_NONE;
	int port;
	u_int length, resid, size;
	uint8_t buf[2];
	uint8_t status, ireason;

	/* Clear interrupt and get status. */
	status = mvs_getstatus(dev, 1);
	if (slot->state < MVS_SLOT_RUNNING)
	    return;
	port = ccb->ccb_h.target_id & 0x0f;
	/* Wait a bit for late !BUSY status update. */
	if (status & ATA_S_BUSY) {
		if (poll)
			return;
		DELAY(100);
		if ((status = mvs_getstatus(dev, 1)) & ATA_S_BUSY) {
			DELAY(1000);
			if ((status = mvs_getstatus(dev, 1)) & ATA_S_BUSY)
				return;
		}
	}
	/* If we got an error, we are done. */
	if (status & ATA_S_ERROR) {
		et = MVS_ERR_TFE;
		goto end_finished;
	}
	if (ccb->ccb_h.func_code == XPT_ATA_IO) { /* ATA PIO */
		ccb->ataio.res.status = status;
		/* Are we moving data? */
		if ((ccb->ccb_h.flags & CAM_DIR_MASK) != CAM_DIR_NONE) {
		    /* If data read command - get them. */
		    if ((ccb->ccb_h.flags & CAM_DIR_MASK) == CAM_DIR_IN) {
			if (mvs_wait(dev, ATA_S_DRQ, ATA_S_BUSY, 1000) < 0) {
			    device_printf(dev, "timeout waiting for read DRQ\n");
			    et = MVS_ERR_TIMEOUT;
			    xpt_freeze_simq(ch->sim, 1);
			    ch->toslots |= (1 << slot->slot);
			    goto end_finished;
			}
			ATA_INSW_STRM(ch->r_mem, ATA_DATA,
			   (uint16_t *)(ccb->ataio.data_ptr + ch->donecount),
			   ch->transfersize / 2);
		    }
		    /* Update how far we've gotten. */
		    ch->donecount += ch->transfersize;
		    /* Do we need more? */
		    if (ccb->ataio.dxfer_len > ch->donecount) {
			/* Set this transfer size according to HW capabilities */
			ch->transfersize = min(ccb->ataio.dxfer_len - ch->donecount,
			    ch->transfersize);
			/* If data write command - put them */
			if ((ccb->ccb_h.flags & CAM_DIR_MASK) == CAM_DIR_OUT) {
				if (mvs_wait(dev, ATA_S_DRQ, ATA_S_BUSY, 1000) < 0) {
				    device_printf(dev,
					"timeout waiting for write DRQ\n");
				    et = MVS_ERR_TIMEOUT;
				    xpt_freeze_simq(ch->sim, 1);
				    ch->toslots |= (1 << slot->slot);
				    goto end_finished;
				}
				ATA_OUTSW_STRM(ch->r_mem, ATA_DATA,
				   (uint16_t *)(ccb->ataio.data_ptr + ch->donecount),
				   ch->transfersize / 2);
				return;
			}
			/* If data read command, return & wait for interrupt */
			if ((ccb->ccb_h.flags & CAM_DIR_MASK) == CAM_DIR_IN)
				return;
		    }
		}
	} else if (ch->basic_dma) {	/* ATAPI DMA */
		if (status & ATA_S_DWF)
			et = MVS_ERR_TFE;
		else if (ATA_INL(ch->r_mem, DMA_S) & DMA_S_ERR)
			et = MVS_ERR_TFE;
		/* Stop basic DMA. */
		ATA_OUTL(ch->r_mem, DMA_C, 0);
		goto end_finished;
	} else {			/* ATAPI PIO */
		length = ATA_INB(ch->r_mem,ATA_CYL_LSB) |
		    (ATA_INB(ch->r_mem,ATA_CYL_MSB) << 8);
		size = min(ch->transfersize, length);
		ireason = ATA_INB(ch->r_mem,ATA_IREASON);
		switch ((ireason & (ATA_I_CMD | ATA_I_IN)) |
			(status & ATA_S_DRQ)) {

		case ATAPI_P_CMDOUT:
		    device_printf(dev, "ATAPI CMDOUT\n");
		    /* Return wait for interrupt */
		    return;

		case ATAPI_P_WRITE:
		    if ((ccb->ccb_h.flags & CAM_DIR_MASK) == CAM_DIR_IN) {
			device_printf(dev, "trying to write on read buffer\n");
			et = MVS_ERR_TFE;
			goto end_finished;
			break;
		    }
		    ATA_OUTSW_STRM(ch->r_mem, ATA_DATA,
			(uint16_t *)(ccb->csio.data_ptr + ch->donecount),
			(size + 1) / 2);
		    for (resid = ch->transfersize + (size & 1);
			resid < length; resid += sizeof(int16_t))
			    ATA_OUTW(ch->r_mem, ATA_DATA, 0);
		    ch->donecount += length;
		    /* Set next transfer size according to HW capabilities */
		    ch->transfersize = min(ccb->csio.dxfer_len - ch->donecount,
			    ch->curr[ccb->ccb_h.target_id].bytecount);
		    /* Return wait for interrupt */
		    return;

		case ATAPI_P_READ:
		    if ((ccb->ccb_h.flags & CAM_DIR_MASK) == CAM_DIR_OUT) {
			device_printf(dev, "trying to read on write buffer\n");
			et = MVS_ERR_TFE;
			goto end_finished;
		    }
		    if (size >= 2) {
			ATA_INSW_STRM(ch->r_mem, ATA_DATA,
			    (uint16_t *)(ccb->csio.data_ptr + ch->donecount),
			    size / 2);
		    }
		    if (size & 1) {
			ATA_INSW_STRM(ch->r_mem, ATA_DATA, (void*)buf, 1);
			((uint8_t *)ccb->csio.data_ptr + ch->donecount +
			    (size & ~1))[0] = buf[0];
		    }
		    for (resid = ch->transfersize + (size & 1);
			resid < length; resid += sizeof(int16_t))
			    ATA_INW(ch->r_mem, ATA_DATA);
		    ch->donecount += length;
		    /* Set next transfer size according to HW capabilities */
		    ch->transfersize = min(ccb->csio.dxfer_len - ch->donecount,
			    ch->curr[ccb->ccb_h.target_id].bytecount);
		    /* Return wait for interrupt */
		    return;

		case ATAPI_P_DONEDRQ:
		    device_printf(dev,
			  "WARNING - DONEDRQ non conformant device\n");
		    if ((ccb->ccb_h.flags & CAM_DIR_MASK) == CAM_DIR_IN) {
			ATA_INSW_STRM(ch->r_mem, ATA_DATA,
			    (uint16_t *)(ccb->csio.data_ptr + ch->donecount),
			    length / 2);
			ch->donecount += length;
		    }
		    else if ((ccb->ccb_h.flags & CAM_DIR_MASK) == CAM_DIR_OUT) {
			ATA_OUTSW_STRM(ch->r_mem, ATA_DATA,
			    (uint16_t *)(ccb->csio.data_ptr + ch->donecount),
			    length / 2);
			ch->donecount += length;
		    }
		    else
			et = MVS_ERR_TFE;
		    /* FALLTHROUGH */

		case ATAPI_P_ABORT:
		case ATAPI_P_DONE:
		    if (status & (ATA_S_ERROR | ATA_S_DWF))
			et = MVS_ERR_TFE;
		    goto end_finished;

		default:
		    device_printf(dev, "unknown transfer phase"
			" (status %02x, ireason %02x)\n",
			status, ireason);
		    et = MVS_ERR_TFE;
		}
	}

end_finished:
	mvs_end_transaction(slot, et);
}

static void
mvs_crbq_intr(device_t dev)
{
	struct mvs_channel *ch = device_get_softc(dev);
	struct mvs_crpb *crpb;
	union ccb *ccb;
	int in_idx, fin_idx, cin_idx, slot;
	uint32_t val;
	uint16_t flags;

	val = ATA_INL(ch->r_mem, EDMA_RESQIP);
	if (val == 0)
		val = ATA_INL(ch->r_mem, EDMA_RESQIP);
	in_idx = (val & EDMA_RESQP_ERPQP_MASK) >>
	    EDMA_RESQP_ERPQP_SHIFT;
	bus_dmamap_sync(ch->dma.workrp_tag, ch->dma.workrp_map,
	    BUS_DMASYNC_POSTREAD);
	fin_idx = cin_idx = ch->in_idx;
	ch->in_idx = in_idx;
	while (in_idx != cin_idx) {
		crpb = (struct mvs_crpb *)
		    (ch->dma.workrp + MVS_CRPB_OFFSET +
		    (MVS_CRPB_SIZE * cin_idx));
		slot = le16toh(crpb->id) & MVS_CRPB_TAG_MASK;
		flags = le16toh(crpb->rspflg);
		/*
		 * Handle only successful completions here.
		 * Errors will be handled by main intr handler.
		 */
#if defined(__i386__) || defined(__amd64__)
		if (crpb->id == 0xffff && crpb->rspflg == 0xffff) {
			device_printf(dev, "Unfilled CRPB "
			    "%d (%d->%d) tag %d flags %04x rs %08x\n",
			    cin_idx, fin_idx, in_idx, slot, flags, ch->rslots);
		} else
#endif
		if (ch->numtslots != 0 ||
		    (flags & EDMA_IE_EDEVERR) == 0) {
#if defined(__i386__) || defined(__amd64__)
			crpb->id = 0xffff;
			crpb->rspflg = 0xffff;
#endif
			if (ch->slot[slot].state >= MVS_SLOT_RUNNING) {
				ccb = ch->slot[slot].ccb;
				ccb->ataio.res.status =
				    (flags & MVS_CRPB_ATASTS_MASK) >>
				    MVS_CRPB_ATASTS_SHIFT;
				mvs_end_transaction(&ch->slot[slot], MVS_ERR_NONE);
			} else {
				device_printf(dev, "Unused tag in CRPB "
				    "%d (%d->%d) tag %d flags %04x rs %08x\n",
				    cin_idx, fin_idx, in_idx, slot, flags,
				    ch->rslots);
			}
		} else {
			device_printf(dev,
			    "CRPB with error %d tag %d flags %04x\n",
			    cin_idx, slot, flags);
		}
		cin_idx = (cin_idx + 1) & (MVS_MAX_SLOTS - 1);
	}
	bus_dmamap_sync(ch->dma.workrp_tag, ch->dma.workrp_map,
	    BUS_DMASYNC_PREREAD);
	if (cin_idx == ch->in_idx) {
		ATA_OUTL(ch->r_mem, EDMA_RESQOP,
		    ch->dma.workrp_bus | (cin_idx << EDMA_RESQP_ERPQP_SHIFT));
	}
}

/* Must be called with channel locked. */
static int
mvs_check_collision(device_t dev, union ccb *ccb)
{
	struct mvs_channel *ch = device_get_softc(dev);

	if (ccb->ccb_h.func_code == XPT_ATA_IO) {
		/* NCQ DMA */
		if (ccb->ataio.cmd.flags & CAM_ATAIO_FPDMA) {
			/* Can't mix NCQ and non-NCQ DMA commands. */
			if (ch->numdslots != 0)
				return (1);
			/* Can't mix NCQ and PIO commands. */
			if (ch->numpslots != 0)
				return (1);
			/* If we have no FBS */
			if (!ch->fbs_enabled) {
				/* Tagged command while tagged to other target is active. */
				if (ch->numtslots != 0 &&
				    ch->taggedtarget != ccb->ccb_h.target_id)
					return (1);
			}
		/* Non-NCQ DMA */
		} else if (ccb->ataio.cmd.flags & CAM_ATAIO_DMA) {
			/* Can't mix non-NCQ DMA and NCQ commands. */
			if (ch->numtslots != 0)
				return (1);
			/* Can't mix non-NCQ DMA and PIO commands. */
			if (ch->numpslots != 0)
				return (1);
		/* PIO */
		} else {
			/* Can't mix PIO with anything. */
			if (ch->numrslots != 0)
				return (1);
		}
		if (ccb->ataio.cmd.flags & (CAM_ATAIO_CONTROL | CAM_ATAIO_NEEDRESULT)) {
			/* Atomic command while anything active. */
			if (ch->numrslots != 0)
				return (1);
		}
	} else { /* ATAPI */
		/* ATAPI goes without EDMA, so can't mix it with anything. */
		if (ch->numrslots != 0)
			return (1);
	}
	/* We have some atomic command running. */
	if (ch->aslots != 0)
		return (1);
	return (0);
}

static void
mvs_tfd_read(device_t dev, union ccb *ccb)
{
	struct mvs_channel *ch = device_get_softc(dev);
	struct ata_res *res = &ccb->ataio.res;

	res->status = ATA_INB(ch->r_mem, ATA_ALTSTAT);
	res->error =  ATA_INB(ch->r_mem, ATA_ERROR);
	res->device = ATA_INB(ch->r_mem, ATA_DRIVE);
	ATA_OUTB(ch->r_mem, ATA_CONTROL, ATA_A_HOB);
	res->sector_count_exp = ATA_INB(ch->r_mem, ATA_COUNT);
	res->lba_low_exp = ATA_INB(ch->r_mem, ATA_SECTOR);
	res->lba_mid_exp = ATA_INB(ch->r_mem, ATA_CYL_LSB);
	res->lba_high_exp = ATA_INB(ch->r_mem, ATA_CYL_MSB);
	ATA_OUTB(ch->r_mem, ATA_CONTROL, 0);
	res->sector_count = ATA_INB(ch->r_mem, ATA_COUNT);
	res->lba_low = ATA_INB(ch->r_mem, ATA_SECTOR);
	res->lba_mid = ATA_INB(ch->r_mem, ATA_CYL_LSB);
	res->lba_high = ATA_INB(ch->r_mem, ATA_CYL_MSB);
}

static void
mvs_tfd_write(device_t dev, union ccb *ccb)
{
	struct mvs_channel *ch = device_get_softc(dev);
	struct ata_cmd *cmd = &ccb->ataio.cmd;

	ATA_OUTB(ch->r_mem, ATA_DRIVE, cmd->device);
	ATA_OUTB(ch->r_mem, ATA_CONTROL, cmd->control);
	ATA_OUTB(ch->r_mem, ATA_FEATURE, cmd->features_exp);
	ATA_OUTB(ch->r_mem, ATA_FEATURE, cmd->features);
	ATA_OUTB(ch->r_mem, ATA_COUNT, cmd->sector_count_exp);
	ATA_OUTB(ch->r_mem, ATA_COUNT, cmd->sector_count);
	ATA_OUTB(ch->r_mem, ATA_SECTOR, cmd->lba_low_exp);
	ATA_OUTB(ch->r_mem, ATA_SECTOR, cmd->lba_low);
	ATA_OUTB(ch->r_mem, ATA_CYL_LSB, cmd->lba_mid_exp);
	ATA_OUTB(ch->r_mem, ATA_CYL_LSB, cmd->lba_mid);
	ATA_OUTB(ch->r_mem, ATA_CYL_MSB, cmd->lba_high_exp);
	ATA_OUTB(ch->r_mem, ATA_CYL_MSB, cmd->lba_high);
	ATA_OUTB(ch->r_mem, ATA_COMMAND, cmd->command);
}


/* Must be called with channel locked. */
static void
mvs_begin_transaction(device_t dev, union ccb *ccb)
{
	struct mvs_channel *ch = device_get_softc(dev);
	struct mvs_slot *slot;
	int slotn, tag;

	if (ch->pm_level > 0)
		mvs_ch_pm_wake(dev);
	/* Softreset is a special case. */
	if (ccb->ccb_h.func_code == XPT_ATA_IO &&
	    (ccb->ataio.cmd.flags & CAM_ATAIO_CONTROL)) {
		mvs_softreset(dev, ccb);
		return;
	}
	/* Choose empty slot. */
	slotn = ffs(~ch->oslots) - 1;
	if ((ccb->ccb_h.func_code == XPT_ATA_IO) &&
	    (ccb->ataio.cmd.flags & CAM_ATAIO_FPDMA)) {
		if (ch->quirks & MVS_Q_GENIIE)
			tag = ffs(~ch->otagspd[ccb->ccb_h.target_id]) - 1;
		else
			tag = slotn;
	} else
		tag = 0;
	/* Occupy chosen slot. */
	slot = &ch->slot[slotn];
	slot->ccb = ccb;
	slot->tag = tag;
	/* Stop PM timer. */
	if (ch->numrslots == 0 && ch->pm_level > 3)
		callout_stop(&ch->pm_timer);
	/* Update channel stats. */
	ch->oslots |= (1 << slot->slot);
	ch->numrslots++;
	ch->numrslotspd[ccb->ccb_h.target_id]++;
	if (ccb->ccb_h.func_code == XPT_ATA_IO) {
		if (ccb->ataio.cmd.flags & CAM_ATAIO_FPDMA) {
			ch->otagspd[ccb->ccb_h.target_id] |= (1 << slot->tag);
			ch->numtslots++;
			ch->numtslotspd[ccb->ccb_h.target_id]++;
			ch->taggedtarget = ccb->ccb_h.target_id;
			mvs_set_edma_mode(dev, MVS_EDMA_NCQ);
		} else if (ccb->ataio.cmd.flags & CAM_ATAIO_DMA) {
			ch->numdslots++;
			mvs_set_edma_mode(dev, MVS_EDMA_ON);
		} else {
			ch->numpslots++;
			mvs_set_edma_mode(dev, MVS_EDMA_OFF);
		}
		if (ccb->ataio.cmd.flags &
		    (CAM_ATAIO_CONTROL | CAM_ATAIO_NEEDRESULT)) {
			ch->aslots |= (1 << slot->slot);
		}
	} else {
		uint8_t *cdb = (ccb->ccb_h.flags & CAM_CDB_POINTER) ?
		    ccb->csio.cdb_io.cdb_ptr : ccb->csio.cdb_io.cdb_bytes;
		ch->numpslots++;
		/* Use ATAPI DMA only for commands without under-/overruns. */
		if ((ccb->ccb_h.flags & CAM_DIR_MASK) != CAM_DIR_NONE &&
		    ch->curr[ccb->ccb_h.target_id].mode >= ATA_DMA &&
		    (ch->quirks & MVS_Q_SOC) == 0 &&
		    (cdb[0] == 0x08 ||
		     cdb[0] == 0x0a ||
		     cdb[0] == 0x28 ||
		     cdb[0] == 0x2a ||
		     cdb[0] == 0x88 ||
		     cdb[0] == 0x8a ||
		     cdb[0] == 0xa8 ||
		     cdb[0] == 0xaa ||
		     cdb[0] == 0xbe)) {
			ch->basic_dma = 1;
		}
		mvs_set_edma_mode(dev, MVS_EDMA_OFF);
	}
	if (ch->numpslots == 0 || ch->basic_dma) {
		slot->state = MVS_SLOT_LOADING;
		bus_dmamap_load_ccb(ch->dma.data_tag, slot->dma.data_map,
		    ccb, mvs_dmasetprd, slot, 0);
	} else
		mvs_legacy_execute_transaction(slot);
}

/* Locked by busdma engine. */
static void
mvs_dmasetprd(void *arg, bus_dma_segment_t *segs, int nsegs, int error)
{    
	struct mvs_slot *slot = arg;
	struct mvs_channel *ch = device_get_softc(slot->dev);
	struct mvs_eprd *eprd;
	int i;

	if (error) {
		device_printf(slot->dev, "DMA load error\n");
		mvs_end_transaction(slot, MVS_ERR_INVALID);
		return;
	}
	KASSERT(nsegs <= MVS_SG_ENTRIES, ("too many DMA segment entries\n"));
	/* If there is only one segment - no need to use S/G table on Gen-IIe. */
	if (nsegs == 1 && ch->basic_dma == 0 && (ch->quirks & MVS_Q_GENIIE)) {
		slot->dma.addr = segs[0].ds_addr;
		slot->dma.len = segs[0].ds_len;
	} else {
		slot->dma.addr = 0;
		/* Get a piece of the workspace for this EPRD */
		eprd = (struct mvs_eprd *)
		    (ch->dma.workrq + MVS_EPRD_OFFSET + (MVS_EPRD_SIZE * slot->slot));
		/* Fill S/G table */
		for (i = 0; i < nsegs; i++) {
			eprd[i].prdbal = htole32(segs[i].ds_addr);
			eprd[i].bytecount = htole32(segs[i].ds_len & MVS_EPRD_MASK);
			eprd[i].prdbah = htole32((segs[i].ds_addr >> 16) >> 16);
		}
		eprd[i - 1].bytecount |= htole32(MVS_EPRD_EOF);
	}
	bus_dmamap_sync(ch->dma.data_tag, slot->dma.data_map,
	    ((slot->ccb->ccb_h.flags & CAM_DIR_IN) ?
	    BUS_DMASYNC_PREREAD : BUS_DMASYNC_PREWRITE));
	if (ch->basic_dma)
		mvs_legacy_execute_transaction(slot);
	else
		mvs_execute_transaction(slot);
}

static void
mvs_legacy_execute_transaction(struct mvs_slot *slot)
{
	device_t dev = slot->dev;
	struct mvs_channel *ch = device_get_softc(dev);
	bus_addr_t eprd;
	union ccb *ccb = slot->ccb;
	int port = ccb->ccb_h.target_id & 0x0f;
	int timeout;

	slot->state = MVS_SLOT_RUNNING;
	ch->rslots |= (1 << slot->slot);
	ATA_OUTB(ch->r_mem, SATA_SATAICTL, port << SATA_SATAICTL_PMPTX_SHIFT);
	if (ccb->ccb_h.func_code == XPT_ATA_IO) {
		mvs_tfd_write(dev, ccb);
		/* Device reset doesn't interrupt. */
		if (ccb->ataio.cmd.command == ATA_DEVICE_RESET) {
			int timeout = 1000000;
			do {
			    DELAY(10);
			    ccb->ataio.res.status = ATA_INB(ch->r_mem, ATA_STATUS);
			} while (ccb->ataio.res.status & ATA_S_BUSY && timeout--);
			mvs_legacy_intr(dev, 1);
			return;
		}
		ch->donecount = 0;
		if (ccb->ataio.cmd.command == ATA_READ_MUL ||
		    ccb->ataio.cmd.command == ATA_READ_MUL48 ||
		    ccb->ataio.cmd.command == ATA_WRITE_MUL ||
		    ccb->ataio.cmd.command == ATA_WRITE_MUL48) {
			ch->transfersize = min(ccb->ataio.dxfer_len,
			    ch->curr[port].bytecount);
		} else
			ch->transfersize = min(ccb->ataio.dxfer_len, 512);
		if ((ccb->ccb_h.flags & CAM_DIR_MASK) != CAM_DIR_NONE)
			ch->fake_busy = 1;
		/* If data write command - output the data */
		if ((ccb->ccb_h.flags & CAM_DIR_MASK) == CAM_DIR_OUT) {
			if (mvs_wait(dev, ATA_S_DRQ, ATA_S_BUSY, 1000) < 0) {
				device_printf(dev,
				    "timeout waiting for write DRQ\n");
				xpt_freeze_simq(ch->sim, 1);
				ch->toslots |= (1 << slot->slot);
				mvs_end_transaction(slot, MVS_ERR_TIMEOUT);
				return;
			}
			ATA_OUTSW_STRM(ch->r_mem, ATA_DATA,
			   (uint16_t *)(ccb->ataio.data_ptr + ch->donecount),
			   ch->transfersize / 2);
		}
	} else {
		ch->donecount = 0;
		ch->transfersize = min(ccb->csio.dxfer_len,
		    ch->curr[port].bytecount);
		/* Write ATA PACKET command. */
		if (ch->basic_dma) {
			ATA_OUTB(ch->r_mem, ATA_FEATURE, ATA_F_DMA);
			ATA_OUTB(ch->r_mem, ATA_CYL_LSB, 0);
		    	ATA_OUTB(ch->r_mem, ATA_CYL_MSB, 0);
		} else {
			ATA_OUTB(ch->r_mem, ATA_FEATURE, 0);
			ATA_OUTB(ch->r_mem, ATA_CYL_LSB, ch->transfersize);
		    	ATA_OUTB(ch->r_mem, ATA_CYL_MSB, ch->transfersize >> 8);
		}
		ATA_OUTB(ch->r_mem, ATA_COMMAND, ATA_PACKET_CMD);
		ch->fake_busy = 1;
		/* Wait for ready to write ATAPI command block */
		if (mvs_wait(dev, 0, ATA_S_BUSY, 1000) < 0) {
			device_printf(dev, "timeout waiting for ATAPI !BUSY\n");
			xpt_freeze_simq(ch->sim, 1);
			ch->toslots |= (1 << slot->slot);
			mvs_end_transaction(slot, MVS_ERR_TIMEOUT);
			return;
		}
		timeout = 5000;
		while (timeout--) {
		    int reason = ATA_INB(ch->r_mem, ATA_IREASON);
		    int status = ATA_INB(ch->r_mem, ATA_STATUS);

		    if (((reason & (ATA_I_CMD | ATA_I_IN)) |
			 (status & (ATA_S_DRQ | ATA_S_BUSY))) == ATAPI_P_CMDOUT)
			break;
		    DELAY(20);
		}
		if (timeout <= 0) {
			device_printf(dev,
			    "timeout waiting for ATAPI command ready\n");
			xpt_freeze_simq(ch->sim, 1);
			ch->toslots |= (1 << slot->slot);
			mvs_end_transaction(slot, MVS_ERR_TIMEOUT);
			return;
		}
		/* Write ATAPI command. */
		ATA_OUTSW_STRM(ch->r_mem, ATA_DATA,
		   (uint16_t *)((ccb->ccb_h.flags & CAM_CDB_POINTER) ?
		    ccb->csio.cdb_io.cdb_ptr : ccb->csio.cdb_io.cdb_bytes),
		   ch->curr[port].atapi / 2);
		DELAY(10);
		if (ch->basic_dma) {
			/* Start basic DMA. */
			eprd = ch->dma.workrq_bus + MVS_EPRD_OFFSET +
			    (MVS_EPRD_SIZE * slot->slot);
			ATA_OUTL(ch->r_mem, DMA_DTLBA, eprd);
			ATA_OUTL(ch->r_mem, DMA_DTHBA, (eprd >> 16) >> 16);
			ATA_OUTL(ch->r_mem, DMA_C, DMA_C_START |
			    (((ccb->ccb_h.flags & CAM_DIR_MASK) == CAM_DIR_IN) ?
			    DMA_C_READ : 0));
		}
	}
	/* Start command execution timeout */
	callout_reset_sbt(&slot->timeout, SBT_1MS * ccb->ccb_h.timeout, 0,
	    (timeout_t*)mvs_timeout, slot, 0);
}

/* Must be called with channel locked. */
static void
mvs_execute_transaction(struct mvs_slot *slot)
{
	device_t dev = slot->dev;
	struct mvs_channel *ch = device_get_softc(dev);
	bus_addr_t eprd;
	struct mvs_crqb *crqb;
	struct mvs_crqb_gen2e *crqb2e;
	union ccb *ccb = slot->ccb;
	int port = ccb->ccb_h.target_id & 0x0f;
	int i;

	/* Get address of the prepared EPRD */
	eprd = ch->dma.workrq_bus + MVS_EPRD_OFFSET + (MVS_EPRD_SIZE * slot->slot);
	/* Prepare CRQB. Gen IIe uses different CRQB format. */
	if (ch->quirks & MVS_Q_GENIIE) {
		crqb2e = (struct mvs_crqb_gen2e *)
		    (ch->dma.workrq + MVS_CRQB_OFFSET + (MVS_CRQB_SIZE * ch->out_idx));
		crqb2e->ctrlflg = htole32(
		    ((ccb->ccb_h.flags & CAM_DIR_IN) ? MVS_CRQB2E_READ : 0) |
		    (slot->tag << MVS_CRQB2E_DTAG_SHIFT) |
		    (port << MVS_CRQB2E_PMP_SHIFT) |
		    (slot->slot << MVS_CRQB2E_HTAG_SHIFT));
		/* If there is only one segment - no need to use S/G table. */
		if (slot->dma.addr != 0) {
			eprd = slot->dma.addr;
			crqb2e->ctrlflg |= htole32(MVS_CRQB2E_CPRD);
			crqb2e->drbc = slot->dma.len;
		}
		crqb2e->cprdbl = htole32(eprd);
		crqb2e->cprdbh = htole32((eprd >> 16) >> 16);
		crqb2e->cmd[0] = 0;
		crqb2e->cmd[1] = 0;
		crqb2e->cmd[2] = ccb->ataio.cmd.command;
		crqb2e->cmd[3] = ccb->ataio.cmd.features;
		crqb2e->cmd[4] = ccb->ataio.cmd.lba_low;
		crqb2e->cmd[5] = ccb->ataio.cmd.lba_mid;
		crqb2e->cmd[6] = ccb->ataio.cmd.lba_high;
		crqb2e->cmd[7] = ccb->ataio.cmd.device;
		crqb2e->cmd[8] = ccb->ataio.cmd.lba_low_exp;
		crqb2e->cmd[9] = ccb->ataio.cmd.lba_mid_exp;
		crqb2e->cmd[10] = ccb->ataio.cmd.lba_high_exp;
		crqb2e->cmd[11] = ccb->ataio.cmd.features_exp;
		if (ccb->ataio.cmd.flags & CAM_ATAIO_FPDMA) {
			crqb2e->cmd[12] = slot->tag << 3;
			crqb2e->cmd[13] = 0;
		} else {
			crqb2e->cmd[12] = ccb->ataio.cmd.sector_count;
			crqb2e->cmd[13] = ccb->ataio.cmd.sector_count_exp;
		}
		crqb2e->cmd[14] = 0;
		crqb2e->cmd[15] = 0;
	} else {
		crqb = (struct mvs_crqb *)
		    (ch->dma.workrq + MVS_CRQB_OFFSET + (MVS_CRQB_SIZE * ch->out_idx));
		crqb->cprdbl = htole32(eprd);
		crqb->cprdbh = htole32((eprd >> 16) >> 16);
		crqb->ctrlflg = htole16(
		    ((ccb->ccb_h.flags & CAM_DIR_IN) ? MVS_CRQB_READ : 0) |
		    (slot->slot << MVS_CRQB_TAG_SHIFT) |
		    (port << MVS_CRQB_PMP_SHIFT));
		i = 0;
		/*
		 * Controller can handle only 11 of 12 ATA registers,
		 * so we have to choose which one to skip.
		 */
		if (ccb->ataio.cmd.flags & CAM_ATAIO_FPDMA) {
			crqb->cmd[i++] = ccb->ataio.cmd.features_exp;
			crqb->cmd[i++] = 0x11;
		}
		crqb->cmd[i++] = ccb->ataio.cmd.features;
		crqb->cmd[i++] = 0x11;
		if (ccb->ataio.cmd.flags & CAM_ATAIO_FPDMA) {
			crqb->cmd[i++] = slot->tag << 3;
			crqb->cmd[i++] = 0x12;
		} else {
			crqb->cmd[i++] = ccb->ataio.cmd.sector_count_exp;
			crqb->cmd[i++] = 0x12;
			crqb->cmd[i++] = ccb->ataio.cmd.sector_count;
			crqb->cmd[i++] = 0x12;
		}
		crqb->cmd[i++] = ccb->ataio.cmd.lba_low_exp;
		crqb->cmd[i++] = 0x13;
		crqb->cmd[i++] = ccb->ataio.cmd.lba_low;
		crqb->cmd[i++] = 0x13;
		crqb->cmd[i++] = ccb->ataio.cmd.lba_mid_exp;
		crqb->cmd[i++] = 0x14;
		crqb->cmd[i++] = ccb->ataio.cmd.lba_mid;
		crqb->cmd[i++] = 0x14;
		crqb->cmd[i++] = ccb->ataio.cmd.lba_high_exp;
		crqb->cmd[i++] = 0x15;
		crqb->cmd[i++] = ccb->ataio.cmd.lba_high;
		crqb->cmd[i++] = 0x15;
		crqb->cmd[i++] = ccb->ataio.cmd.device;
		crqb->cmd[i++] = 0x16;
		crqb->cmd[i++] = ccb->ataio.cmd.command;
		crqb->cmd[i++] = 0x97;
	}
	bus_dmamap_sync(ch->dma.workrq_tag, ch->dma.workrq_map,
	    BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(ch->dma.workrp_tag, ch->dma.workrp_map,
	    BUS_DMASYNC_PREREAD);
	slot->state = MVS_SLOT_RUNNING;
	ch->rslots |= (1 << slot->slot);
	/* Issue command to the controller. */
	ch->out_idx = (ch->out_idx + 1) & (MVS_MAX_SLOTS - 1);
	ATA_OUTL(ch->r_mem, EDMA_REQQIP,
	    ch->dma.workrq_bus + MVS_CRQB_OFFSET + (MVS_CRQB_SIZE * ch->out_idx));
	/* Start command execution timeout */
	callout_reset_sbt(&slot->timeout, SBT_1MS * ccb->ccb_h.timeout, 0,
	    (timeout_t*)mvs_timeout, slot, 0);
	return;
}

/* Must be called with channel locked. */
static void
mvs_process_timeout(device_t dev)
{
	struct mvs_channel *ch = device_get_softc(dev);
	int i;

	mtx_assert(&ch->mtx, MA_OWNED);
	/* Handle the rest of commands. */
	for (i = 0; i < MVS_MAX_SLOTS; i++) {
		/* Do we have a running request on slot? */
		if (ch->slot[i].state < MVS_SLOT_RUNNING)
			continue;
		mvs_end_transaction(&ch->slot[i], MVS_ERR_TIMEOUT);
	}
}

/* Must be called with channel locked. */
static void
mvs_rearm_timeout(device_t dev)
{
	struct mvs_channel *ch = device_get_softc(dev);
	int i;

	mtx_assert(&ch->mtx, MA_OWNED);
	for (i = 0; i < MVS_MAX_SLOTS; i++) {
		struct mvs_slot *slot = &ch->slot[i];

		/* Do we have a running request on slot? */
		if (slot->state < MVS_SLOT_RUNNING)
			continue;
		if ((ch->toslots & (1 << i)) == 0)
			continue;
		callout_reset_sbt(&slot->timeout,
		    SBT_1MS * slot->ccb->ccb_h.timeout / 2, 0,
		    (timeout_t*)mvs_timeout, slot, 0);
	}
}

/* Locked by callout mechanism. */
static void
mvs_timeout(struct mvs_slot *slot)
{
	device_t dev = slot->dev;
	struct mvs_channel *ch = device_get_softc(dev);

	/* Check for stale timeout. */
	if (slot->state < MVS_SLOT_RUNNING)
		return;
	device_printf(dev, "Timeout on slot %d\n", slot->slot);
	device_printf(dev, "iec %08x sstat %08x serr %08x edma_s %08x "
	    "dma_c %08x dma_s %08x rs %08x status %02x\n",
	    ATA_INL(ch->r_mem, EDMA_IEC),
	    ATA_INL(ch->r_mem, SATA_SS), ATA_INL(ch->r_mem, SATA_SE),
	    ATA_INL(ch->r_mem, EDMA_S), ATA_INL(ch->r_mem, DMA_C),
	    ATA_INL(ch->r_mem, DMA_S), ch->rslots,
	    ATA_INB(ch->r_mem, ATA_ALTSTAT));
	/* Handle frozen command. */
	mvs_requeue_frozen(dev);
	/* We wait for other commands timeout and pray. */
	if (ch->toslots == 0)
		xpt_freeze_simq(ch->sim, 1);
	ch->toslots |= (1 << slot->slot);
	if ((ch->rslots & ~ch->toslots) == 0)
		mvs_process_timeout(dev);
	else
		device_printf(dev, " ... waiting for slots %08x\n",
		    ch->rslots & ~ch->toslots);
}

/* Must be called with channel locked. */
static void
mvs_end_transaction(struct mvs_slot *slot, enum mvs_err_type et)
{
	device_t dev = slot->dev;
	struct mvs_channel *ch = device_get_softc(dev);
	union ccb *ccb = slot->ccb;
	int lastto;

	bus_dmamap_sync(ch->dma.workrq_tag, ch->dma.workrq_map,
	    BUS_DMASYNC_POSTWRITE);
	/* Read result registers to the result struct
	 * May be incorrect if several commands finished same time,
	 * so read only when sure or have to.
	 */
	if (ccb->ccb_h.func_code == XPT_ATA_IO) {
		struct ata_res *res = &ccb->ataio.res;

		if ((et == MVS_ERR_TFE) ||
		    (ccb->ataio.cmd.flags & CAM_ATAIO_NEEDRESULT)) {
			mvs_tfd_read(dev, ccb);
		} else
			bzero(res, sizeof(*res));
	} else {
		if ((ccb->ccb_h.flags & CAM_DIR_MASK) != CAM_DIR_NONE &&
		    ch->basic_dma == 0)
			ccb->csio.resid = ccb->csio.dxfer_len - ch->donecount;
	}
	if (ch->numpslots == 0 || ch->basic_dma) {
		if ((ccb->ccb_h.flags & CAM_DIR_MASK) != CAM_DIR_NONE) {
			bus_dmamap_sync(ch->dma.data_tag, slot->dma.data_map,
			    (ccb->ccb_h.flags & CAM_DIR_IN) ?
			    BUS_DMASYNC_POSTREAD : BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(ch->dma.data_tag, slot->dma.data_map);
		}
	}
	if (et != MVS_ERR_NONE)
		ch->eslots |= (1 << slot->slot);
	/* In case of error, freeze device for proper recovery. */
	if ((et != MVS_ERR_NONE) && (!ch->recoverycmd) &&
	    !(ccb->ccb_h.status & CAM_DEV_QFRZN)) {
		xpt_freeze_devq(ccb->ccb_h.path, 1);
		ccb->ccb_h.status |= CAM_DEV_QFRZN;
	}
	/* Set proper result status. */
	ccb->ccb_h.status &= ~CAM_STATUS_MASK;
	switch (et) {
	case MVS_ERR_NONE:
		ccb->ccb_h.status |= CAM_REQ_CMP;
		if (ccb->ccb_h.func_code == XPT_SCSI_IO)
			ccb->csio.scsi_status = SCSI_STATUS_OK;
		break;
	case MVS_ERR_INVALID:
		ch->fatalerr = 1;
		ccb->ccb_h.status |= CAM_REQ_INVALID;
		break;
	case MVS_ERR_INNOCENT:
		ccb->ccb_h.status |= CAM_REQUEUE_REQ;
		break;
	case MVS_ERR_TFE:
	case MVS_ERR_NCQ:
		if (ccb->ccb_h.func_code == XPT_SCSI_IO) {
			ccb->ccb_h.status |= CAM_SCSI_STATUS_ERROR;
			ccb->csio.scsi_status = SCSI_STATUS_CHECK_COND;
		} else {
			ccb->ccb_h.status |= CAM_ATA_STATUS_ERROR;
		}
		break;
	case MVS_ERR_SATA:
		ch->fatalerr = 1;
		if (!ch->recoverycmd) {
			xpt_freeze_simq(ch->sim, 1);
			ccb->ccb_h.status &= ~CAM_STATUS_MASK;
			ccb->ccb_h.status |= CAM_RELEASE_SIMQ;
		}
		ccb->ccb_h.status |= CAM_UNCOR_PARITY;
		break;
	case MVS_ERR_TIMEOUT:
		if (!ch->recoverycmd) {
			xpt_freeze_simq(ch->sim, 1);
			ccb->ccb_h.status &= ~CAM_STATUS_MASK;
			ccb->ccb_h.status |= CAM_RELEASE_SIMQ;
		}
		ccb->ccb_h.status |= CAM_CMD_TIMEOUT;
		break;
	default:
		ch->fatalerr = 1;
		ccb->ccb_h.status |= CAM_REQ_CMP_ERR;
	}
	/* Free slot. */
	ch->oslots &= ~(1 << slot->slot);
	ch->rslots &= ~(1 << slot->slot);
	ch->aslots &= ~(1 << slot->slot);
	slot->state = MVS_SLOT_EMPTY;
	slot->ccb = NULL;
	/* Update channel stats. */
	ch->numrslots--;
	ch->numrslotspd[ccb->ccb_h.target_id]--;
	if (ccb->ccb_h.func_code == XPT_ATA_IO) {
		if (ccb->ataio.cmd.flags & CAM_ATAIO_FPDMA) {
			ch->otagspd[ccb->ccb_h.target_id] &= ~(1 << slot->tag);
			ch->numtslots--;
			ch->numtslotspd[ccb->ccb_h.target_id]--;
		} else if (ccb->ataio.cmd.flags & CAM_ATAIO_DMA) {
			ch->numdslots--;
		} else {
			ch->numpslots--;
		}
	} else {
		ch->numpslots--;
		ch->basic_dma = 0;
	}
	/* Cancel timeout state if request completed normally. */
	if (et != MVS_ERR_TIMEOUT) {
		lastto = (ch->toslots == (1 << slot->slot));
		ch->toslots &= ~(1 << slot->slot);
		if (lastto)
			xpt_release_simq(ch->sim, TRUE);
	}
	/* If it was our READ LOG command - process it. */
	if (ccb->ccb_h.recovery_type == RECOVERY_READ_LOG) {
		mvs_process_read_log(dev, ccb);
	/* If it was our REQUEST SENSE command - process it. */
	} else if (ccb->ccb_h.recovery_type == RECOVERY_REQUEST_SENSE) {
		mvs_process_request_sense(dev, ccb);
	/* If it was NCQ or ATAPI command error, put result on hold. */
	} else if (et == MVS_ERR_NCQ ||
	    ((ccb->ccb_h.status & CAM_STATUS_MASK) == CAM_SCSI_STATUS_ERROR &&
	     (ccb->ccb_h.flags & CAM_DIS_AUTOSENSE) == 0)) {
		ch->hold[slot->slot] = ccb;
		ch->holdtag[slot->slot] = slot->tag;
		ch->numhslots++;
	} else
		xpt_done(ccb);
	/* If we have no other active commands, ... */
	if (ch->rslots == 0) {
		/* if there was fatal error - reset port. */
		if (ch->toslots != 0 || ch->fatalerr) {
			mvs_reset(dev);
		} else {
			/* if we have slots in error, we can reinit port. */
			if (ch->eslots != 0) {
				mvs_set_edma_mode(dev, MVS_EDMA_OFF);
				ch->eslots = 0;
			}
			/* if there commands on hold, we can do READ LOG. */
			if (!ch->recoverycmd && ch->numhslots)
				mvs_issue_recovery(dev);
		}
	/* If all the rest of commands are in timeout - give them chance. */
	} else if ((ch->rslots & ~ch->toslots) == 0 &&
	    et != MVS_ERR_TIMEOUT)
		mvs_rearm_timeout(dev);
	/* Unfreeze frozen command. */
	if (ch->frozen && !mvs_check_collision(dev, ch->frozen)) {
		union ccb *fccb = ch->frozen;
		ch->frozen = NULL;
		mvs_begin_transaction(dev, fccb);
		xpt_release_simq(ch->sim, TRUE);
	}
	/* Start PM timer. */
	if (ch->numrslots == 0 && ch->pm_level > 3 &&
	    (ch->curr[ch->pm_present ? 15 : 0].caps & CTS_SATA_CAPS_D_PMREQ)) {
		callout_schedule(&ch->pm_timer,
		    (ch->pm_level == 4) ? hz / 1000 : hz / 8);
	}
}

static void
mvs_issue_recovery(device_t dev)
{
	struct mvs_channel *ch = device_get_softc(dev);
	union ccb *ccb;
	struct ccb_ataio *ataio;
	struct ccb_scsiio *csio;
	int i;

	/* Find some held command. */
	for (i = 0; i < MVS_MAX_SLOTS; i++) {
		if (ch->hold[i])
			break;
	}
	ccb = xpt_alloc_ccb_nowait();
	if (ccb == NULL) {
		device_printf(dev, "Unable to allocate recovery command\n");
completeall:
		/* We can't do anything -- complete held commands. */
		for (i = 0; i < MVS_MAX_SLOTS; i++) {
			if (ch->hold[i] == NULL)
				continue;
			ch->hold[i]->ccb_h.status &= ~CAM_STATUS_MASK;
			ch->hold[i]->ccb_h.status |= CAM_RESRC_UNAVAIL;
			xpt_done(ch->hold[i]);
			ch->hold[i] = NULL;
			ch->numhslots--;
		}
		mvs_reset(dev);
		return;
	}
	ccb->ccb_h = ch->hold[i]->ccb_h;	/* Reuse old header. */
	if (ccb->ccb_h.func_code == XPT_ATA_IO) {
		/* READ LOG */
		ccb->ccb_h.recovery_type = RECOVERY_READ_LOG;
		ccb->ccb_h.func_code = XPT_ATA_IO;
		ccb->ccb_h.flags = CAM_DIR_IN;
		ccb->ccb_h.timeout = 1000;	/* 1s should be enough. */
		ataio = &ccb->ataio;
		ataio->data_ptr = malloc(512, M_MVS, M_NOWAIT);
		if (ataio->data_ptr == NULL) {
			xpt_free_ccb(ccb);
			device_printf(dev,
			    "Unable to allocate memory for READ LOG command\n");
			goto completeall;
		}
		ataio->dxfer_len = 512;
		bzero(&ataio->cmd, sizeof(ataio->cmd));
		ataio->cmd.flags = CAM_ATAIO_48BIT;
		ataio->cmd.command = 0x2F;	/* READ LOG EXT */
		ataio->cmd.sector_count = 1;
		ataio->cmd.sector_count_exp = 0;
		ataio->cmd.lba_low = 0x10;
		ataio->cmd.lba_mid = 0;
		ataio->cmd.lba_mid_exp = 0;
	} else {
		/* REQUEST SENSE */
		ccb->ccb_h.recovery_type = RECOVERY_REQUEST_SENSE;
		ccb->ccb_h.recovery_slot = i;
		ccb->ccb_h.func_code = XPT_SCSI_IO;
		ccb->ccb_h.flags = CAM_DIR_IN;
		ccb->ccb_h.status = 0;
		ccb->ccb_h.timeout = 1000;	/* 1s should be enough. */
		csio = &ccb->csio;
		csio->data_ptr = (void *)&ch->hold[i]->csio.sense_data;
		csio->dxfer_len = ch->hold[i]->csio.sense_len;
		csio->cdb_len = 6;
		bzero(&csio->cdb_io, sizeof(csio->cdb_io));
		csio->cdb_io.cdb_bytes[0] = 0x03;
		csio->cdb_io.cdb_bytes[4] = csio->dxfer_len;
	}
	/* Freeze SIM while doing recovery. */
	ch->recoverycmd = 1;
	xpt_freeze_simq(ch->sim, 1);
	mvs_begin_transaction(dev, ccb);
}

static void
mvs_process_read_log(device_t dev, union ccb *ccb)
{
	struct mvs_channel *ch = device_get_softc(dev);
	uint8_t *data;
	struct ata_res *res;
	int i;

	ch->recoverycmd = 0;

	data = ccb->ataio.data_ptr;
	if ((ccb->ccb_h.status & CAM_STATUS_MASK) == CAM_REQ_CMP &&
	    (data[0] & 0x80) == 0) {
		for (i = 0; i < MVS_MAX_SLOTS; i++) {
			if (!ch->hold[i])
				continue;
			if (ch->hold[i]->ccb_h.target_id != ccb->ccb_h.target_id)
				continue;
			if ((data[0] & 0x1F) == ch->holdtag[i]) {
				res = &ch->hold[i]->ataio.res;
				res->status = data[2];
				res->error = data[3];
				res->lba_low = data[4];
				res->lba_mid = data[5];
				res->lba_high = data[6];
				res->device = data[7];
				res->lba_low_exp = data[8];
				res->lba_mid_exp = data[9];
				res->lba_high_exp = data[10];
				res->sector_count = data[12];
				res->sector_count_exp = data[13];
			} else {
				ch->hold[i]->ccb_h.status &= ~CAM_STATUS_MASK;
				ch->hold[i]->ccb_h.status |= CAM_REQUEUE_REQ;
			}
			xpt_done(ch->hold[i]);
			ch->hold[i] = NULL;
			ch->numhslots--;
		}
	} else {
		if ((ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP)
			device_printf(dev, "Error while READ LOG EXT\n");
		else if ((data[0] & 0x80) == 0) {
			device_printf(dev,
			    "Non-queued command error in READ LOG EXT\n");
		}
		for (i = 0; i < MVS_MAX_SLOTS; i++) {
			if (!ch->hold[i])
				continue;
			if (ch->hold[i]->ccb_h.target_id != ccb->ccb_h.target_id)
				continue;
			xpt_done(ch->hold[i]);
			ch->hold[i] = NULL;
			ch->numhslots--;
		}
	}
	free(ccb->ataio.data_ptr, M_MVS);
	xpt_free_ccb(ccb);
	xpt_release_simq(ch->sim, TRUE);
}

static void
mvs_process_request_sense(device_t dev, union ccb *ccb)
{
	struct mvs_channel *ch = device_get_softc(dev);
	int i;

	ch->recoverycmd = 0;

	i = ccb->ccb_h.recovery_slot;
	if ((ccb->ccb_h.status & CAM_STATUS_MASK) == CAM_REQ_CMP) {
		ch->hold[i]->ccb_h.status |= CAM_AUTOSNS_VALID;
	} else {
		ch->hold[i]->ccb_h.status &= ~CAM_STATUS_MASK;
		ch->hold[i]->ccb_h.status |= CAM_AUTOSENSE_FAIL;
	}
	xpt_done(ch->hold[i]);
	ch->hold[i] = NULL;
	ch->numhslots--;
	xpt_free_ccb(ccb);
	xpt_release_simq(ch->sim, TRUE);
}

static int
mvs_wait(device_t dev, u_int s, u_int c, int t)
{
	int timeout = 0;
	uint8_t st;

	while (((st =  mvs_getstatus(dev, 0)) & (s | c)) != s) {
		if (timeout >= t) {
			if (t != 0)
				device_printf(dev, "Wait status %02x\n", st);
			return (-1);
		}
		DELAY(1000);
		timeout++;
	} 
	return (timeout);
}

static void
mvs_requeue_frozen(device_t dev)
{
	struct mvs_channel *ch = device_get_softc(dev);
	union ccb *fccb = ch->frozen;

	if (fccb) {
		ch->frozen = NULL;
		fccb->ccb_h.status = CAM_REQUEUE_REQ | CAM_RELEASE_SIMQ;
		if (!(fccb->ccb_h.status & CAM_DEV_QFRZN)) {
			xpt_freeze_devq(fccb->ccb_h.path, 1);
			fccb->ccb_h.status |= CAM_DEV_QFRZN;
		}
		xpt_done(fccb);
	}
}

static void
mvs_reset_to(void *arg)
{
	device_t dev = arg;
	struct mvs_channel *ch = device_get_softc(dev);
	int t;

	if (ch->resetting == 0)
		return;
	ch->resetting--;
	if ((t = mvs_wait(dev, 0, ATA_S_BUSY | ATA_S_DRQ, 0)) >= 0) {
		if (bootverbose) {
			device_printf(dev,
			    "MVS reset: device ready after %dms\n",
			    (310 - ch->resetting) * 100);
		}
		ch->resetting = 0;
		xpt_release_simq(ch->sim, TRUE);
		return;
	}
	if (ch->resetting == 0) {
		device_printf(dev,
		    "MVS reset: device not ready after 31000ms\n");
		xpt_release_simq(ch->sim, TRUE);
		return;
	}
	callout_schedule(&ch->reset_timer, hz / 10);
}

static void
mvs_errata(device_t dev)
{
	struct mvs_channel *ch = device_get_softc(dev);
	uint32_t val;

	if (ch->quirks & MVS_Q_SOC65) {
		val = ATA_INL(ch->r_mem, SATA_PHYM3);
		val &= ~(0x3 << 27);	/* SELMUPF = 1 */
		val |= (0x1 << 27);
		val &= ~(0x3 << 29);	/* SELMUPI = 1 */
		val |= (0x1 << 29);
		ATA_OUTL(ch->r_mem, SATA_PHYM3, val);

		val = ATA_INL(ch->r_mem, SATA_PHYM4);
		val &= ~0x1;		/* SATU_OD8 = 0 */
		val |= (0x1 << 16);	/* reserved bit 16 = 1 */
		ATA_OUTL(ch->r_mem, SATA_PHYM4, val);

		val = ATA_INL(ch->r_mem, SATA_PHYM9_GEN2);
		val &= ~0xf;		/* TXAMP[3:0] = 8 */
		val |= 0x8;
		val &= ~(0x1 << 14);	/* TXAMP[4] = 0 */
		ATA_OUTL(ch->r_mem, SATA_PHYM9_GEN2, val);

		val = ATA_INL(ch->r_mem, SATA_PHYM9_GEN1);
		val &= ~0xf;		/* TXAMP[3:0] = 8 */
		val |= 0x8;
		val &= ~(0x1 << 14);	/* TXAMP[4] = 0 */
		ATA_OUTL(ch->r_mem, SATA_PHYM9_GEN1, val);
	}
}

static void
mvs_reset(device_t dev)
{
	struct mvs_channel *ch = device_get_softc(dev);
	int i;

	xpt_freeze_simq(ch->sim, 1);
	if (bootverbose)
		device_printf(dev, "MVS reset...\n");
	/* Forget about previous reset. */
	if (ch->resetting) {
		ch->resetting = 0;
		callout_stop(&ch->reset_timer);
		xpt_release_simq(ch->sim, TRUE);
	}
	/* Requeue freezed command. */
	mvs_requeue_frozen(dev);
	/* Kill the engine and requeue all running commands. */
	mvs_set_edma_mode(dev, MVS_EDMA_OFF);
	ATA_OUTL(ch->r_mem, DMA_C, 0);
	for (i = 0; i < MVS_MAX_SLOTS; i++) {
		/* Do we have a running request on slot? */
		if (ch->slot[i].state < MVS_SLOT_RUNNING)
			continue;
		/* XXX; Commands in loading state. */
		mvs_end_transaction(&ch->slot[i], MVS_ERR_INNOCENT);
	}
	for (i = 0; i < MVS_MAX_SLOTS; i++) {
		if (!ch->hold[i])
			continue;
		xpt_done(ch->hold[i]);
		ch->hold[i] = NULL;
		ch->numhslots--;
	}
	if (ch->toslots != 0)
		xpt_release_simq(ch->sim, TRUE);
	ch->eslots = 0;
	ch->toslots = 0;
	ch->fatalerr = 0;
	ch->fake_busy = 0;
	/* Tell the XPT about the event */
	xpt_async(AC_BUS_RESET, ch->path, NULL);
	ATA_OUTL(ch->r_mem, EDMA_IEM, 0);
	ATA_OUTL(ch->r_mem, EDMA_CMD, EDMA_CMD_EATARST);
	DELAY(25);
	ATA_OUTL(ch->r_mem, EDMA_CMD, 0);
	mvs_errata(dev);
	/* Reset and reconnect PHY, */
	if (!mvs_sata_phy_reset(dev)) {
		if (bootverbose)
			device_printf(dev, "MVS reset: device not found\n");
		ch->devices = 0;
		ATA_OUTL(ch->r_mem, SATA_SE, 0xffffffff);
		ATA_OUTL(ch->r_mem, EDMA_IEC, 0);
		ATA_OUTL(ch->r_mem, EDMA_IEM, ~EDMA_IE_TRANSIENT);
		xpt_release_simq(ch->sim, TRUE);
		return;
	}
	if (bootverbose)
		device_printf(dev, "MVS reset: device found\n");
	/* Wait for clearing busy status. */
	if ((i = mvs_wait(dev, 0, ATA_S_BUSY | ATA_S_DRQ,
	    dumping ? 31000 : 0)) < 0) {
		if (dumping) {
			device_printf(dev,
			    "MVS reset: device not ready after 31000ms\n");
		} else
			ch->resetting = 310;
	} else if (bootverbose)
		device_printf(dev, "MVS reset: device ready after %dms\n", i);
	ch->devices = 1;
	ATA_OUTL(ch->r_mem, SATA_SE, 0xffffffff);
	ATA_OUTL(ch->r_mem, EDMA_IEC, 0);
	ATA_OUTL(ch->r_mem, EDMA_IEM, ~EDMA_IE_TRANSIENT);
	if (ch->resetting)
		callout_reset(&ch->reset_timer, hz / 10, mvs_reset_to, dev);
	else
		xpt_release_simq(ch->sim, TRUE);
}

static void
mvs_softreset(device_t dev, union ccb *ccb)
{
	struct mvs_channel *ch = device_get_softc(dev);
	int port = ccb->ccb_h.target_id & 0x0f;
	int i, stuck;
	uint8_t status;

	mvs_set_edma_mode(dev, MVS_EDMA_OFF);
	ATA_OUTB(ch->r_mem, SATA_SATAICTL, port << SATA_SATAICTL_PMPTX_SHIFT);
	ATA_OUTB(ch->r_mem, ATA_CONTROL, ATA_A_RESET);
	DELAY(10000);
	ATA_OUTB(ch->r_mem, ATA_CONTROL, 0);
	ccb->ccb_h.status &= ~CAM_STATUS_MASK;
	/* Wait for clearing busy status. */
	if ((i = mvs_wait(dev, 0, ATA_S_BUSY, ccb->ccb_h.timeout)) < 0) {
		ccb->ccb_h.status |= CAM_CMD_TIMEOUT;
		stuck = 1;
	} else {
		status = mvs_getstatus(dev, 0);
		if (status & ATA_S_ERROR)
			ccb->ccb_h.status |= CAM_ATA_STATUS_ERROR;
		else
			ccb->ccb_h.status |= CAM_REQ_CMP;
		if (status & ATA_S_DRQ)
			stuck = 1;
		else
			stuck = 0;
	}
	mvs_tfd_read(dev, ccb);

	/*
	 * XXX: If some device on PMP failed to soft-reset,
	 * try to recover by sending dummy soft-reset to PMP.
	 */
	if (stuck && ch->pm_present && port != 15) {
		ATA_OUTB(ch->r_mem, SATA_SATAICTL,
		    15 << SATA_SATAICTL_PMPTX_SHIFT);
		ATA_OUTB(ch->r_mem, ATA_CONTROL, ATA_A_RESET);
		DELAY(10000);
		ATA_OUTB(ch->r_mem, ATA_CONTROL, 0);
		mvs_wait(dev, 0, ATA_S_BUSY | ATA_S_DRQ, ccb->ccb_h.timeout);
	}

	xpt_done(ccb);
}

static int
mvs_sata_connect(struct mvs_channel *ch)
{
	u_int32_t status;
	int timeout, found = 0;

	/* Wait up to 100ms for "connect well" */
	for (timeout = 0; timeout < 1000 ; timeout++) {
		status = ATA_INL(ch->r_mem, SATA_SS);
		if ((status & SATA_SS_DET_MASK) != SATA_SS_DET_NO_DEVICE)
			found = 1;
		if (((status & SATA_SS_DET_MASK) == SATA_SS_DET_PHY_ONLINE) &&
		    ((status & SATA_SS_SPD_MASK) != SATA_SS_SPD_NO_SPEED) &&
		    ((status & SATA_SS_IPM_MASK) == SATA_SS_IPM_ACTIVE))
			break;
		if ((status & SATA_SS_DET_MASK) == SATA_SS_DET_PHY_OFFLINE) {
			if (bootverbose) {
				device_printf(ch->dev, "SATA offline status=%08x\n",
				    status);
			}
			return (0);
		}
		if (found == 0 && timeout >= 100)
			break;
		DELAY(100);
	}
	if (timeout >= 1000 || !found) {
		if (bootverbose) {
			device_printf(ch->dev,
			    "SATA connect timeout time=%dus status=%08x\n",
			    timeout * 100, status);
		}
		return (0);
	}
	if (bootverbose) {
		device_printf(ch->dev, "SATA connect time=%dus status=%08x\n",
		    timeout * 100, status);
	}
	/* Clear SATA error register */
	ATA_OUTL(ch->r_mem, SATA_SE, 0xffffffff);
	return (1);
}

static int
mvs_sata_phy_reset(device_t dev)
{
	struct mvs_channel *ch = device_get_softc(dev);
	int sata_rev;
	uint32_t val;

	sata_rev = ch->user[ch->pm_present ? 15 : 0].revision;
	if (sata_rev == 1)
		val = SATA_SC_SPD_SPEED_GEN1;
	else if (sata_rev == 2)
		val = SATA_SC_SPD_SPEED_GEN2;
	else if (sata_rev == 3)
		val = SATA_SC_SPD_SPEED_GEN3;
	else
		val = 0;
	ATA_OUTL(ch->r_mem, SATA_SC,
	    SATA_SC_DET_RESET | val |
	    SATA_SC_IPM_DIS_PARTIAL | SATA_SC_IPM_DIS_SLUMBER);
	DELAY(1000);
	ATA_OUTL(ch->r_mem, SATA_SC,
	    SATA_SC_DET_IDLE | val | ((ch->pm_level > 0) ? 0 :
	    (SATA_SC_IPM_DIS_PARTIAL | SATA_SC_IPM_DIS_SLUMBER)));
	if (!mvs_sata_connect(ch)) {
		if (ch->pm_level > 0)
			ATA_OUTL(ch->r_mem, SATA_SC, SATA_SC_DET_DISABLE);
		return (0);
	}
	return (1);
}

static int
mvs_check_ids(device_t dev, union ccb *ccb)
{
	struct mvs_channel *ch = device_get_softc(dev);

	if (ccb->ccb_h.target_id > ((ch->quirks & MVS_Q_GENI) ? 0 : 15)) {
		ccb->ccb_h.status = CAM_TID_INVALID;
		xpt_done(ccb);
		return (-1);
	}
	if (ccb->ccb_h.target_lun != 0) {
		ccb->ccb_h.status = CAM_LUN_INVALID;
		xpt_done(ccb);
		return (-1);
	}
	/*
	 * It's a programming error to see AUXILIARY register requests.
	 */
	KASSERT(ccb->ccb_h.func_code != XPT_ATA_IO ||
	    ((ccb->ataio.ata_flags & ATA_FLAG_AUX) == 0),
	    ("AUX register unsupported"));
	return (0);
}

static void
mvsaction(struct cam_sim *sim, union ccb *ccb)
{
	device_t dev, parent;
	struct mvs_channel *ch;

	CAM_DEBUG(ccb->ccb_h.path, CAM_DEBUG_TRACE, ("mvsaction func_code=%x\n",
	    ccb->ccb_h.func_code));

	ch = (struct mvs_channel *)cam_sim_softc(sim);
	dev = ch->dev;
	switch (ccb->ccb_h.func_code) {
	/* Common cases first */
	case XPT_ATA_IO:	/* Execute the requested I/O operation */
	case XPT_SCSI_IO:
		if (mvs_check_ids(dev, ccb))
			return;
		if (ch->devices == 0 ||
		    (ch->pm_present == 0 &&
		     ccb->ccb_h.target_id > 0 && ccb->ccb_h.target_id < 15)) {
			ccb->ccb_h.status = CAM_SEL_TIMEOUT;
			break;
		}
		ccb->ccb_h.recovery_type = RECOVERY_NONE;
		/* Check for command collision. */
		if (mvs_check_collision(dev, ccb)) {
			/* Freeze command. */
			ch->frozen = ccb;
			/* We have only one frozen slot, so freeze simq also. */
			xpt_freeze_simq(ch->sim, 1);
			return;
		}
		mvs_begin_transaction(dev, ccb);
		return;
	case XPT_ABORT:			/* Abort the specified CCB */
		/* XXX Implement */
		ccb->ccb_h.status = CAM_REQ_INVALID;
		break;
	case XPT_SET_TRAN_SETTINGS:
	{
		struct	ccb_trans_settings *cts = &ccb->cts;
		struct	mvs_device *d; 

		if (mvs_check_ids(dev, ccb))
			return;
		if (cts->type == CTS_TYPE_CURRENT_SETTINGS)
			d = &ch->curr[ccb->ccb_h.target_id];
		else
			d = &ch->user[ccb->ccb_h.target_id];
		if (cts->xport_specific.sata.valid & CTS_SATA_VALID_REVISION)
			d->revision = cts->xport_specific.sata.revision;
		if (cts->xport_specific.sata.valid & CTS_SATA_VALID_MODE)
			d->mode = cts->xport_specific.sata.mode;
		if (cts->xport_specific.sata.valid & CTS_SATA_VALID_BYTECOUNT) {
			d->bytecount = min((ch->quirks & MVS_Q_GENIIE) ? 8192 : 2048,
			    cts->xport_specific.sata.bytecount);
		}
		if (cts->xport_specific.sata.valid & CTS_SATA_VALID_TAGS)
			d->tags = min(MVS_MAX_SLOTS, cts->xport_specific.sata.tags);
		if (cts->xport_specific.sata.valid & CTS_SATA_VALID_PM)
			ch->pm_present = cts->xport_specific.sata.pm_present;
		if (cts->xport_specific.sata.valid & CTS_SATA_VALID_ATAPI)
			d->atapi = cts->xport_specific.sata.atapi;
		if (cts->xport_specific.sata.valid & CTS_SATA_VALID_CAPS)
			d->caps = cts->xport_specific.sata.caps;
		ccb->ccb_h.status = CAM_REQ_CMP;
		break;
	}
	case XPT_GET_TRAN_SETTINGS:
	/* Get default/user set transfer settings for the target */
	{
		struct	ccb_trans_settings *cts = &ccb->cts;
		struct  mvs_device *d;
		uint32_t status;

		if (mvs_check_ids(dev, ccb))
			return;
		if (cts->type == CTS_TYPE_CURRENT_SETTINGS)
			d = &ch->curr[ccb->ccb_h.target_id];
		else
			d = &ch->user[ccb->ccb_h.target_id];
		cts->protocol = PROTO_UNSPECIFIED;
		cts->protocol_version = PROTO_VERSION_UNSPECIFIED;
		cts->transport = XPORT_SATA;
		cts->transport_version = XPORT_VERSION_UNSPECIFIED;
		cts->proto_specific.valid = 0;
		cts->xport_specific.sata.valid = 0;
		if (cts->type == CTS_TYPE_CURRENT_SETTINGS &&
		    (ccb->ccb_h.target_id == 15 ||
		    (ccb->ccb_h.target_id == 0 && !ch->pm_present))) {
			status = ATA_INL(ch->r_mem, SATA_SS) & SATA_SS_SPD_MASK;
			if (status & 0x0f0) {
				cts->xport_specific.sata.revision =
				    (status & 0x0f0) >> 4;
				cts->xport_specific.sata.valid |=
				    CTS_SATA_VALID_REVISION;
			}
			cts->xport_specific.sata.caps = d->caps & CTS_SATA_CAPS_D;
//			if (ch->pm_level)
//				cts->xport_specific.sata.caps |= CTS_SATA_CAPS_H_PMREQ;
			cts->xport_specific.sata.caps |= CTS_SATA_CAPS_H_AN;
			cts->xport_specific.sata.caps &=
			    ch->user[ccb->ccb_h.target_id].caps;
			cts->xport_specific.sata.valid |= CTS_SATA_VALID_CAPS;
		} else {
			cts->xport_specific.sata.revision = d->revision;
			cts->xport_specific.sata.valid |= CTS_SATA_VALID_REVISION;
			cts->xport_specific.sata.caps = d->caps;
			if (cts->type == CTS_TYPE_CURRENT_SETTINGS/* &&
			    (ch->quirks & MVS_Q_GENIIE) == 0*/)
				cts->xport_specific.sata.caps &= ~CTS_SATA_CAPS_H_AN;
			cts->xport_specific.sata.valid |= CTS_SATA_VALID_CAPS;
		}
		cts->xport_specific.sata.mode = d->mode;
		cts->xport_specific.sata.valid |= CTS_SATA_VALID_MODE;
		cts->xport_specific.sata.bytecount = d->bytecount;
		cts->xport_specific.sata.valid |= CTS_SATA_VALID_BYTECOUNT;
		cts->xport_specific.sata.pm_present = ch->pm_present;
		cts->xport_specific.sata.valid |= CTS_SATA_VALID_PM;
		cts->xport_specific.sata.tags = d->tags;
		cts->xport_specific.sata.valid |= CTS_SATA_VALID_TAGS;
		cts->xport_specific.sata.atapi = d->atapi;
		cts->xport_specific.sata.valid |= CTS_SATA_VALID_ATAPI;
		ccb->ccb_h.status = CAM_REQ_CMP;
		break;
	}
	case XPT_RESET_BUS:		/* Reset the specified SCSI bus */
	case XPT_RESET_DEV:	/* Bus Device Reset the specified SCSI device */
		mvs_reset(dev);
		ccb->ccb_h.status = CAM_REQ_CMP;
		break;
	case XPT_TERM_IO:		/* Terminate the I/O process */
		/* XXX Implement */
		ccb->ccb_h.status = CAM_REQ_INVALID;
		break;
	case XPT_PATH_INQ:		/* Path routing inquiry */
	{
		struct ccb_pathinq *cpi = &ccb->cpi;

		parent = device_get_parent(dev);
		cpi->version_num = 1; /* XXX??? */
		cpi->hba_inquiry = PI_SDTR_ABLE;
		if (!(ch->quirks & MVS_Q_GENI)) {
			cpi->hba_inquiry |= PI_SATAPM;
			/* Gen-II is extremely slow with NCQ on PMP. */
			if ((ch->quirks & MVS_Q_GENIIE) || ch->pm_present == 0)
				cpi->hba_inquiry |= PI_TAG_ABLE;
		}
		cpi->target_sprt = 0;
		cpi->hba_misc = PIM_SEQSCAN;
		cpi->hba_eng_cnt = 0;
		if (!(ch->quirks & MVS_Q_GENI))
			cpi->max_target = 15;
		else
			cpi->max_target = 0;
		cpi->max_lun = 0;
		cpi->initiator_id = 0;
		cpi->bus_id = cam_sim_bus(sim);
		cpi->base_transfer_speed = 150000;
		strlcpy(cpi->sim_vid, "FreeBSD", SIM_IDLEN);
		strlcpy(cpi->hba_vid, "Marvell", HBA_IDLEN);
		strlcpy(cpi->dev_name, cam_sim_name(sim), DEV_IDLEN);
		cpi->unit_number = cam_sim_unit(sim);
		cpi->transport = XPORT_SATA;
		cpi->transport_version = XPORT_VERSION_UNSPECIFIED;
		cpi->protocol = PROTO_ATA;
		cpi->protocol_version = PROTO_VERSION_UNSPECIFIED;
		cpi->maxio = MAXPHYS;
		if ((ch->quirks & MVS_Q_SOC) == 0) {
			cpi->hba_vendor = pci_get_vendor(parent);
			cpi->hba_device = pci_get_device(parent);
			cpi->hba_subvendor = pci_get_subvendor(parent);
			cpi->hba_subdevice = pci_get_subdevice(parent);
		}
		cpi->ccb_h.status = CAM_REQ_CMP;
		break;
	}
	default:
		ccb->ccb_h.status = CAM_REQ_INVALID;
		break;
	}
	xpt_done(ccb);
}

static void
mvspoll(struct cam_sim *sim)
{
	struct mvs_channel *ch = (struct mvs_channel *)cam_sim_softc(sim);
	struct mvs_intr_arg arg;

	arg.arg = ch->dev;
	arg.cause = 2 | 4; /* XXX */
	mvs_ch_intr(&arg);
	if (ch->resetting != 0 &&
	    (--ch->resetpolldiv <= 0 || !callout_pending(&ch->reset_timer))) {
		ch->resetpolldiv = 1000;
		mvs_reset_to(ch->dev);
	}
}

