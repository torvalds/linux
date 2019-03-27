/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009-2012 Alexander Motin <mav@FreeBSD.org>
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
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/endian.h>
#include <sys/malloc.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/sysctl.h>
#include <machine/stdarg.h>
#include <machine/resource.h>
#include <machine/bus.h>
#include <sys/rman.h>
#include "ahci.h"

#include <cam/cam.h>
#include <cam/cam_ccb.h>
#include <cam/cam_sim.h>
#include <cam/cam_xpt_sim.h>
#include <cam/cam_debug.h>

/* local prototypes */
static void ahci_intr(void *data);
static void ahci_intr_one(void *data);
static void ahci_intr_one_edge(void *data);
static int ahci_ch_init(device_t dev);
static int ahci_ch_deinit(device_t dev);
static int ahci_ch_suspend(device_t dev);
static int ahci_ch_resume(device_t dev);
static void ahci_ch_pm(void *arg);
static void ahci_ch_intr(void *arg);
static void ahci_ch_intr_direct(void *arg);
static void ahci_ch_intr_main(struct ahci_channel *ch, uint32_t istatus);
static void ahci_begin_transaction(struct ahci_channel *ch, union ccb *ccb);
static void ahci_dmasetprd(void *arg, bus_dma_segment_t *segs, int nsegs, int error);
static void ahci_execute_transaction(struct ahci_slot *slot);
static void ahci_timeout(struct ahci_slot *slot);
static void ahci_end_transaction(struct ahci_slot *slot, enum ahci_err_type et);
static int ahci_setup_fis(struct ahci_channel *ch, struct ahci_cmd_tab *ctp, union ccb *ccb, int tag);
static void ahci_dmainit(device_t dev);
static void ahci_dmasetupc_cb(void *xsc, bus_dma_segment_t *segs, int nsegs, int error);
static void ahci_dmafini(device_t dev);
static void ahci_slotsalloc(device_t dev);
static void ahci_slotsfree(device_t dev);
static void ahci_reset(struct ahci_channel *ch);
static void ahci_start(struct ahci_channel *ch, int fbs);
static void ahci_stop(struct ahci_channel *ch);
static void ahci_clo(struct ahci_channel *ch);
static void ahci_start_fr(struct ahci_channel *ch);
static void ahci_stop_fr(struct ahci_channel *ch);
static int ahci_phy_check_events(struct ahci_channel *ch, u_int32_t serr);
static uint32_t ahci_ch_detval(struct ahci_channel *ch, uint32_t val);

static int ahci_sata_connect(struct ahci_channel *ch);
static int ahci_sata_phy_reset(struct ahci_channel *ch);
static int ahci_wait_ready(struct ahci_channel *ch, int t, int t0);

static void ahci_issue_recovery(struct ahci_channel *ch);
static void ahci_process_read_log(struct ahci_channel *ch, union ccb *ccb);
static void ahci_process_request_sense(struct ahci_channel *ch, union ccb *ccb);

static void ahciaction(struct cam_sim *sim, union ccb *ccb);
static void ahcipoll(struct cam_sim *sim);

static MALLOC_DEFINE(M_AHCI, "AHCI driver", "AHCI driver data buffers");

#define recovery_type		spriv_field0
#define RECOVERY_NONE		0
#define RECOVERY_READ_LOG	1
#define RECOVERY_REQUEST_SENSE	2
#define recovery_slot		spriv_field1

static uint32_t
ahci_ch_detval(struct ahci_channel *ch, uint32_t val)
{

	return ch->disablephy ? ATA_SC_DET_DISABLE : val;
}

int
ahci_ctlr_setup(device_t dev)
{
	struct ahci_controller *ctlr = device_get_softc(dev);
	/* Clear interrupts */
	ATA_OUTL(ctlr->r_mem, AHCI_IS, ATA_INL(ctlr->r_mem, AHCI_IS));
	/* Configure CCC */
	if (ctlr->ccc) {
		ATA_OUTL(ctlr->r_mem, AHCI_CCCP, ATA_INL(ctlr->r_mem, AHCI_PI));
		ATA_OUTL(ctlr->r_mem, AHCI_CCCC,
		    (ctlr->ccc << AHCI_CCCC_TV_SHIFT) |
		    (4 << AHCI_CCCC_CC_SHIFT) |
		    AHCI_CCCC_EN);
		ctlr->cccv = (ATA_INL(ctlr->r_mem, AHCI_CCCC) &
		    AHCI_CCCC_INT_MASK) >> AHCI_CCCC_INT_SHIFT;
		if (bootverbose) {
			device_printf(dev,
			    "CCC with %dms/4cmd enabled on vector %d\n",
			    ctlr->ccc, ctlr->cccv);
		}
	}
	/* Enable AHCI interrupts */
	ATA_OUTL(ctlr->r_mem, AHCI_GHC,
	    ATA_INL(ctlr->r_mem, AHCI_GHC) | AHCI_GHC_IE);
	return (0);
}

int
ahci_ctlr_reset(device_t dev)
{
	struct ahci_controller *ctlr = device_get_softc(dev);
	int timeout;

	/* Enable AHCI mode */
	ATA_OUTL(ctlr->r_mem, AHCI_GHC, AHCI_GHC_AE);
	/* Reset AHCI controller */
	ATA_OUTL(ctlr->r_mem, AHCI_GHC, AHCI_GHC_AE|AHCI_GHC_HR);
	for (timeout = 1000; timeout > 0; timeout--) {
		DELAY(1000);
		if ((ATA_INL(ctlr->r_mem, AHCI_GHC) & AHCI_GHC_HR) == 0)
			break;
	}
	if (timeout == 0) {
		device_printf(dev, "AHCI controller reset failure\n");
		return (ENXIO);
	}
	/* Reenable AHCI mode */
	ATA_OUTL(ctlr->r_mem, AHCI_GHC, AHCI_GHC_AE);

	if (ctlr->quirks & AHCI_Q_RESTORE_CAP) {
		/*
		 * Restore capability field.
		 * This is write to a read-only register to restore its state.
		 * On fully standard-compliant hardware this is not needed and
		 * this operation shall not take place. See ahci_pci.c for
		 * platforms using this quirk.
		 */
		ATA_OUTL(ctlr->r_mem, AHCI_CAP, ctlr->caps);
	}

	return (0);
}


int
ahci_attach(device_t dev)
{
	struct ahci_controller *ctlr = device_get_softc(dev);
	int error, i, speed, unit;
	uint32_t u, version;
	device_t child;

	ctlr->dev = dev;
	ctlr->ccc = 0;
	resource_int_value(device_get_name(dev),
	    device_get_unit(dev), "ccc", &ctlr->ccc);

	/* Setup our own memory management for channels. */
	ctlr->sc_iomem.rm_start = rman_get_start(ctlr->r_mem);
	ctlr->sc_iomem.rm_end = rman_get_end(ctlr->r_mem);
	ctlr->sc_iomem.rm_type = RMAN_ARRAY;
	ctlr->sc_iomem.rm_descr = "I/O memory addresses";
	if ((error = rman_init(&ctlr->sc_iomem)) != 0) {
		ahci_free_mem(dev);
		return (error);
	}
	if ((error = rman_manage_region(&ctlr->sc_iomem,
	    rman_get_start(ctlr->r_mem), rman_get_end(ctlr->r_mem))) != 0) {
		ahci_free_mem(dev);
		rman_fini(&ctlr->sc_iomem);
		return (error);
	}
	/* Get the HW capabilities */
	version = ATA_INL(ctlr->r_mem, AHCI_VS);
	ctlr->caps = ATA_INL(ctlr->r_mem, AHCI_CAP);
	if (version >= 0x00010200)
		ctlr->caps2 = ATA_INL(ctlr->r_mem, AHCI_CAP2);
	if (ctlr->caps & AHCI_CAP_EMS)
		ctlr->capsem = ATA_INL(ctlr->r_mem, AHCI_EM_CTL);

	if (ctlr->quirks & AHCI_Q_FORCE_PI) {
		/*
		 * Enable ports. 
		 * The spec says that BIOS sets up bits corresponding to
		 * available ports. On platforms where this information
		 * is missing, the driver can define available ports on its own.
		 */
		int nports = (ctlr->caps & AHCI_CAP_NPMASK) + 1;
		int nmask = (1 << nports) - 1;

		ATA_OUTL(ctlr->r_mem, AHCI_PI, nmask);
		device_printf(dev, "Forcing PI to %d ports (mask = %x)\n",
		    nports, nmask);
	}

	ctlr->ichannels = ATA_INL(ctlr->r_mem, AHCI_PI);

	/* Identify and set separate quirks for HBA and RAID f/w Marvells. */
	if ((ctlr->quirks & AHCI_Q_ALTSIG) &&
	    (ctlr->caps & AHCI_CAP_SPM) == 0)
		ctlr->quirks |= AHCI_Q_NOBSYRES;

	if (ctlr->quirks & AHCI_Q_1CH) {
		ctlr->caps &= ~AHCI_CAP_NPMASK;
		ctlr->ichannels &= 0x01;
	}
	if (ctlr->quirks & AHCI_Q_2CH) {
		ctlr->caps &= ~AHCI_CAP_NPMASK;
		ctlr->caps |= 1;
		ctlr->ichannels &= 0x03;
	}
	if (ctlr->quirks & AHCI_Q_4CH) {
		ctlr->caps &= ~AHCI_CAP_NPMASK;
		ctlr->caps |= 3;
		ctlr->ichannels &= 0x0f;
	}
	ctlr->channels = MAX(flsl(ctlr->ichannels),
	    (ctlr->caps & AHCI_CAP_NPMASK) + 1);
	if (ctlr->quirks & AHCI_Q_NOPMP)
		ctlr->caps &= ~AHCI_CAP_SPM;
	if (ctlr->quirks & AHCI_Q_NONCQ)
		ctlr->caps &= ~AHCI_CAP_SNCQ;
	if ((ctlr->caps & AHCI_CAP_CCCS) == 0)
		ctlr->ccc = 0;
	ctlr->emloc = ATA_INL(ctlr->r_mem, AHCI_EM_LOC);

	/* Create controller-wide DMA tag. */
	if (bus_dma_tag_create(bus_get_dma_tag(dev), 1, 0,
	    (ctlr->caps & AHCI_CAP_64BIT) ? BUS_SPACE_MAXADDR :
	    BUS_SPACE_MAXADDR_32BIT, BUS_SPACE_MAXADDR, NULL, NULL,
	    BUS_SPACE_MAXSIZE, BUS_SPACE_UNRESTRICTED, BUS_SPACE_MAXSIZE,
	    ctlr->dma_coherent ? BUS_DMA_COHERENT : 0, NULL, NULL, 
	    &ctlr->dma_tag)) {
		ahci_free_mem(dev);
		rman_fini(&ctlr->sc_iomem);
		return (ENXIO);
	}

	ahci_ctlr_setup(dev);

	/* Setup interrupts. */
	if ((error = ahci_setup_interrupt(dev)) != 0) {
		bus_dma_tag_destroy(ctlr->dma_tag);
		ahci_free_mem(dev);
		rman_fini(&ctlr->sc_iomem);
		return (error);
	}

	i = 0;
	for (u = ctlr->ichannels; u != 0; u >>= 1)
		i += (u & 1);
	ctlr->direct = (ctlr->msi && (ctlr->numirqs > 1 || i <= 3));
	resource_int_value(device_get_name(dev), device_get_unit(dev),
	    "direct", &ctlr->direct);
	/* Announce HW capabilities. */
	speed = (ctlr->caps & AHCI_CAP_ISS) >> AHCI_CAP_ISS_SHIFT;
	device_printf(dev,
		    "AHCI v%x.%02x with %d %sGbps ports, Port Multiplier %s%s\n",
		    ((version >> 20) & 0xf0) + ((version >> 16) & 0x0f),
		    ((version >> 4) & 0xf0) + (version & 0x0f),
		    (ctlr->caps & AHCI_CAP_NPMASK) + 1,
		    ((speed == 1) ? "1.5":((speed == 2) ? "3":
		    ((speed == 3) ? "6":"?"))),
		    (ctlr->caps & AHCI_CAP_SPM) ?
		    "supported" : "not supported",
		    (ctlr->caps & AHCI_CAP_FBSS) ?
		    " with FBS" : "");
	if (ctlr->quirks != 0) {
		device_printf(dev, "quirks=0x%b\n", ctlr->quirks,
		    AHCI_Q_BIT_STRING);
	}
	if (bootverbose) {
		device_printf(dev, "Caps:%s%s%s%s%s%s%s%s %sGbps",
		    (ctlr->caps & AHCI_CAP_64BIT) ? " 64bit":"",
		    (ctlr->caps & AHCI_CAP_SNCQ) ? " NCQ":"",
		    (ctlr->caps & AHCI_CAP_SSNTF) ? " SNTF":"",
		    (ctlr->caps & AHCI_CAP_SMPS) ? " MPS":"",
		    (ctlr->caps & AHCI_CAP_SSS) ? " SS":"",
		    (ctlr->caps & AHCI_CAP_SALP) ? " ALP":"",
		    (ctlr->caps & AHCI_CAP_SAL) ? " AL":"",
		    (ctlr->caps & AHCI_CAP_SCLO) ? " CLO":"",
		    ((speed == 1) ? "1.5":((speed == 2) ? "3":
		    ((speed == 3) ? "6":"?"))));
		printf("%s%s%s%s%s%s %dcmd%s%s%s %dports\n",
		    (ctlr->caps & AHCI_CAP_SAM) ? " AM":"",
		    (ctlr->caps & AHCI_CAP_SPM) ? " PM":"",
		    (ctlr->caps & AHCI_CAP_FBSS) ? " FBS":"",
		    (ctlr->caps & AHCI_CAP_PMD) ? " PMD":"",
		    (ctlr->caps & AHCI_CAP_SSC) ? " SSC":"",
		    (ctlr->caps & AHCI_CAP_PSC) ? " PSC":"",
		    ((ctlr->caps & AHCI_CAP_NCS) >> AHCI_CAP_NCS_SHIFT) + 1,
		    (ctlr->caps & AHCI_CAP_CCCS) ? " CCC":"",
		    (ctlr->caps & AHCI_CAP_EMS) ? " EM":"",
		    (ctlr->caps & AHCI_CAP_SXS) ? " eSATA":"",
		    (ctlr->caps & AHCI_CAP_NPMASK) + 1);
	}
	if (bootverbose && version >= 0x00010200) {
		device_printf(dev, "Caps2:%s%s%s%s%s%s\n",
		    (ctlr->caps2 & AHCI_CAP2_DESO) ? " DESO":"",
		    (ctlr->caps2 & AHCI_CAP2_SADM) ? " SADM":"",
		    (ctlr->caps2 & AHCI_CAP2_SDS) ? " SDS":"",
		    (ctlr->caps2 & AHCI_CAP2_APST) ? " APST":"",
		    (ctlr->caps2 & AHCI_CAP2_NVMP) ? " NVMP":"",
		    (ctlr->caps2 & AHCI_CAP2_BOH) ? " BOH":"");
	}
	/* Attach all channels on this controller */
	for (unit = 0; unit < ctlr->channels; unit++) {
		child = device_add_child(dev, "ahcich", -1);
		if (child == NULL) {
			device_printf(dev, "failed to add channel device\n");
			continue;
		}
		device_set_ivars(child, (void *)(intptr_t)unit);
		if ((ctlr->ichannels & (1 << unit)) == 0)
			device_disable(child);
	}
	if (ctlr->caps & AHCI_CAP_EMS) {
		child = device_add_child(dev, "ahciem", -1);
		if (child == NULL)
			device_printf(dev, "failed to add enclosure device\n");
		else
			device_set_ivars(child, (void *)(intptr_t)-1);
	}
	bus_generic_attach(dev);
	return (0);
}

int
ahci_detach(device_t dev)
{
	struct ahci_controller *ctlr = device_get_softc(dev);
	int i;

	/* Detach & delete all children */
	device_delete_children(dev);

	/* Free interrupts. */
	for (i = 0; i < ctlr->numirqs; i++) {
		if (ctlr->irqs[i].r_irq) {
			bus_teardown_intr(dev, ctlr->irqs[i].r_irq,
			    ctlr->irqs[i].handle);
			bus_release_resource(dev, SYS_RES_IRQ,
			    ctlr->irqs[i].r_irq_rid, ctlr->irqs[i].r_irq);
		}
	}
	bus_dma_tag_destroy(ctlr->dma_tag);
	/* Free memory. */
	rman_fini(&ctlr->sc_iomem);
	ahci_free_mem(dev);
	return (0);
}

void
ahci_free_mem(device_t dev)
{
	struct ahci_controller *ctlr = device_get_softc(dev);

	/* Release memory resources */
	if (ctlr->r_mem)
		bus_release_resource(dev, SYS_RES_MEMORY, ctlr->r_rid, ctlr->r_mem);
	if (ctlr->r_msix_table)
		bus_release_resource(dev, SYS_RES_MEMORY,
		    ctlr->r_msix_tab_rid, ctlr->r_msix_table);
	if (ctlr->r_msix_pba)
		bus_release_resource(dev, SYS_RES_MEMORY,
		    ctlr->r_msix_pba_rid, ctlr->r_msix_pba);

	ctlr->r_msix_pba = ctlr->r_mem = ctlr->r_msix_table = NULL;
}

int
ahci_setup_interrupt(device_t dev)
{
	struct ahci_controller *ctlr = device_get_softc(dev);
	int i;

	/* Check for single MSI vector fallback. */
	if (ctlr->numirqs > 1 &&
	    (ATA_INL(ctlr->r_mem, AHCI_GHC) & AHCI_GHC_MRSM) != 0) {
		device_printf(dev, "Falling back to one MSI\n");
		ctlr->numirqs = 1;
	}

	/* Ensure we don't overrun irqs. */
	if (ctlr->numirqs > AHCI_MAX_IRQS) {
		device_printf(dev, "Too many irqs %d > %d (clamping)\n",
		    ctlr->numirqs, AHCI_MAX_IRQS);
		ctlr->numirqs = AHCI_MAX_IRQS;
	}

	/* Allocate all IRQs. */
	for (i = 0; i < ctlr->numirqs; i++) {
		ctlr->irqs[i].ctlr = ctlr;
		ctlr->irqs[i].r_irq_rid = i + (ctlr->msi ? 1 : 0);
		if (ctlr->channels == 1 && !ctlr->ccc && ctlr->msi)
			ctlr->irqs[i].mode = AHCI_IRQ_MODE_ONE;
		else if (ctlr->numirqs == 1 || i >= ctlr->channels ||
		    (ctlr->ccc && i == ctlr->cccv))
			ctlr->irqs[i].mode = AHCI_IRQ_MODE_ALL;
		else if (ctlr->channels > ctlr->numirqs &&
		    i == ctlr->numirqs - 1)
			ctlr->irqs[i].mode = AHCI_IRQ_MODE_AFTER;
		else
			ctlr->irqs[i].mode = AHCI_IRQ_MODE_ONE;
		if (!(ctlr->irqs[i].r_irq = bus_alloc_resource_any(dev, SYS_RES_IRQ,
		    &ctlr->irqs[i].r_irq_rid, RF_SHAREABLE | RF_ACTIVE))) {
			device_printf(dev, "unable to map interrupt\n");
			return (ENXIO);
		}
		if ((bus_setup_intr(dev, ctlr->irqs[i].r_irq, ATA_INTR_FLAGS, NULL,
		    (ctlr->irqs[i].mode != AHCI_IRQ_MODE_ONE) ? ahci_intr :
		     ((ctlr->quirks & AHCI_Q_EDGEIS) ? ahci_intr_one_edge :
		      ahci_intr_one),
		    &ctlr->irqs[i], &ctlr->irqs[i].handle))) {
			/* SOS XXX release r_irq */
			device_printf(dev, "unable to setup interrupt\n");
			return (ENXIO);
		}
		if (ctlr->numirqs > 1) {
			bus_describe_intr(dev, ctlr->irqs[i].r_irq,
			    ctlr->irqs[i].handle,
			    ctlr->irqs[i].mode == AHCI_IRQ_MODE_ONE ?
			    "ch%d" : "%d", i);
		}
	}
	return (0);
}

/*
 * Common case interrupt handler.
 */
static void
ahci_intr(void *data)
{
	struct ahci_controller_irq *irq = data;
	struct ahci_controller *ctlr = irq->ctlr;
	u_int32_t is, ise = 0;
	void *arg;
	int unit;

	if (irq->mode == AHCI_IRQ_MODE_ALL) {
		unit = 0;
		if (ctlr->ccc)
			is = ctlr->ichannels;
		else
			is = ATA_INL(ctlr->r_mem, AHCI_IS);
	} else {	/* AHCI_IRQ_MODE_AFTER */
		unit = irq->r_irq_rid - 1;
		is = ATA_INL(ctlr->r_mem, AHCI_IS);
		is &= (0xffffffff << unit);
	}
	/* CCC interrupt is edge triggered. */
	if (ctlr->ccc)
		ise = 1 << ctlr->cccv;
	/* Some controllers have edge triggered IS. */
	if (ctlr->quirks & AHCI_Q_EDGEIS)
		ise |= is;
	if (ise != 0)
		ATA_OUTL(ctlr->r_mem, AHCI_IS, ise);
	for (; unit < ctlr->channels; unit++) {
		if ((is & (1 << unit)) != 0 &&
		    (arg = ctlr->interrupt[unit].argument)) {
				ctlr->interrupt[unit].function(arg);
		}
	}
	/* AHCI declares level triggered IS. */
	if (!(ctlr->quirks & AHCI_Q_EDGEIS))
		ATA_OUTL(ctlr->r_mem, AHCI_IS, is);
	ATA_RBL(ctlr->r_mem, AHCI_IS);
}

/*
 * Simplified interrupt handler for multivector MSI mode.
 */
static void
ahci_intr_one(void *data)
{
	struct ahci_controller_irq *irq = data;
	struct ahci_controller *ctlr = irq->ctlr;
	void *arg;
	int unit;

	unit = irq->r_irq_rid - 1;
	if ((arg = ctlr->interrupt[unit].argument))
	    ctlr->interrupt[unit].function(arg);
	/* AHCI declares level triggered IS. */
	ATA_OUTL(ctlr->r_mem, AHCI_IS, 1 << unit);
	ATA_RBL(ctlr->r_mem, AHCI_IS);
}

static void
ahci_intr_one_edge(void *data)
{
	struct ahci_controller_irq *irq = data;
	struct ahci_controller *ctlr = irq->ctlr;
	void *arg;
	int unit;

	unit = irq->r_irq_rid - 1;
	/* Some controllers have edge triggered IS. */
	ATA_OUTL(ctlr->r_mem, AHCI_IS, 1 << unit);
	if ((arg = ctlr->interrupt[unit].argument))
		ctlr->interrupt[unit].function(arg);
	ATA_RBL(ctlr->r_mem, AHCI_IS);
}

struct resource *
ahci_alloc_resource(device_t dev, device_t child, int type, int *rid,
    rman_res_t start, rman_res_t end, rman_res_t count, u_int flags)
{
	struct ahci_controller *ctlr = device_get_softc(dev);
	struct resource *res;
	rman_res_t st;
	int offset, size, unit;

	unit = (intptr_t)device_get_ivars(child);
	res = NULL;
	switch (type) {
	case SYS_RES_MEMORY:
		if (unit >= 0) {
			offset = AHCI_OFFSET + (unit << 7);
			size = 128;
		} else if (*rid == 0) {
			offset = AHCI_EM_CTL;
			size = 4;
		} else {
			offset = (ctlr->emloc & 0xffff0000) >> 14;
			size = (ctlr->emloc & 0x0000ffff) << 2;
			if (*rid != 1) {
				if (*rid == 2 && (ctlr->capsem &
				    (AHCI_EM_XMT | AHCI_EM_SMB)) == 0)
					offset += size;
				else
					break;
			}
		}
		st = rman_get_start(ctlr->r_mem);
		res = rman_reserve_resource(&ctlr->sc_iomem, st + offset,
		    st + offset + size - 1, size, RF_ACTIVE, child);
		if (res) {
			bus_space_handle_t bsh;
			bus_space_tag_t bst;
			bsh = rman_get_bushandle(ctlr->r_mem);
			bst = rman_get_bustag(ctlr->r_mem);
			bus_space_subregion(bst, bsh, offset, 128, &bsh);
			rman_set_bushandle(res, bsh);
			rman_set_bustag(res, bst);
		}
		break;
	case SYS_RES_IRQ:
		if (*rid == ATA_IRQ_RID)
			res = ctlr->irqs[0].r_irq;
		break;
	}
	return (res);
}

int
ahci_release_resource(device_t dev, device_t child, int type, int rid,
    struct resource *r)
{

	switch (type) {
	case SYS_RES_MEMORY:
		rman_release_resource(r);
		return (0);
	case SYS_RES_IRQ:
		if (rid != ATA_IRQ_RID)
			return (ENOENT);
		return (0);
	}
	return (EINVAL);
}

int
ahci_setup_intr(device_t dev, device_t child, struct resource *irq, 
    int flags, driver_filter_t *filter, driver_intr_t *function, 
    void *argument, void **cookiep)
{
	struct ahci_controller *ctlr = device_get_softc(dev);
	int unit = (intptr_t)device_get_ivars(child);

	if (filter != NULL) {
		printf("ahci.c: we cannot use a filter here\n");
		return (EINVAL);
	}
	ctlr->interrupt[unit].function = function;
	ctlr->interrupt[unit].argument = argument;
	return (0);
}

int
ahci_teardown_intr(device_t dev, device_t child, struct resource *irq,
    void *cookie)
{
	struct ahci_controller *ctlr = device_get_softc(dev);
	int unit = (intptr_t)device_get_ivars(child);

	ctlr->interrupt[unit].function = NULL;
	ctlr->interrupt[unit].argument = NULL;
	return (0);
}

int
ahci_print_child(device_t dev, device_t child)
{
	int retval, channel;

	retval = bus_print_child_header(dev, child);
	channel = (int)(intptr_t)device_get_ivars(child);
	if (channel >= 0)
		retval += printf(" at channel %d", channel);
	retval += bus_print_child_footer(dev, child);
	return (retval);
}

int
ahci_child_location_str(device_t dev, device_t child, char *buf,
    size_t buflen)
{
	int channel;

	channel = (int)(intptr_t)device_get_ivars(child);
	if (channel >= 0)
		snprintf(buf, buflen, "channel=%d", channel);
	return (0);
}

bus_dma_tag_t
ahci_get_dma_tag(device_t dev, device_t child)
{
	struct ahci_controller *ctlr = device_get_softc(dev);

	return (ctlr->dma_tag);
}

static int
ahci_ch_probe(device_t dev)
{

	device_set_desc_copy(dev, "AHCI channel");
	return (BUS_PROBE_DEFAULT);
}

static int
ahci_ch_disablephy_proc(SYSCTL_HANDLER_ARGS)
{
	struct ahci_channel *ch;
	int error, value;

	ch = arg1;
	value = ch->disablephy;
	error = sysctl_handle_int(oidp, &value, 0, req);
	if (error != 0 || req->newptr == NULL || (value != 0 && value != 1))
		return (error);

	mtx_lock(&ch->mtx);
	ch->disablephy = value;
	if (value) {
		ahci_ch_deinit(ch->dev);
	} else {
		ahci_ch_init(ch->dev);
		ahci_phy_check_events(ch, ATA_SE_PHY_CHANGED | ATA_SE_EXCHANGED);
	}
	mtx_unlock(&ch->mtx);

	return (0);
}

static int
ahci_ch_attach(device_t dev)
{
	struct ahci_controller *ctlr = device_get_softc(device_get_parent(dev));
	struct ahci_channel *ch = device_get_softc(dev);
	struct cam_devq *devq;
	struct sysctl_ctx_list *ctx;
	struct sysctl_oid *tree;
	int rid, error, i, sata_rev = 0;
	u_int32_t version;

	ch->dev = dev;
	ch->unit = (intptr_t)device_get_ivars(dev);
	ch->caps = ctlr->caps;
	ch->caps2 = ctlr->caps2;
	ch->start = ctlr->ch_start;
	ch->quirks = ctlr->quirks;
	ch->vendorid = ctlr->vendorid;
	ch->deviceid = ctlr->deviceid;
	ch->subvendorid = ctlr->subvendorid;
	ch->subdeviceid = ctlr->subdeviceid;
	ch->numslots = ((ch->caps & AHCI_CAP_NCS) >> AHCI_CAP_NCS_SHIFT) + 1;
	mtx_init(&ch->mtx, "AHCI channel lock", NULL, MTX_DEF);
	ch->pm_level = 0;
	resource_int_value(device_get_name(dev),
	    device_get_unit(dev), "pm_level", &ch->pm_level);
	STAILQ_INIT(&ch->doneq);
	if (ch->pm_level > 3)
		callout_init_mtx(&ch->pm_timer, &ch->mtx, 0);
	callout_init_mtx(&ch->reset_timer, &ch->mtx, 0);
	/* JMicron external ports (0) sometimes limited */
	if ((ctlr->quirks & AHCI_Q_SATA1_UNIT0) && ch->unit == 0)
		sata_rev = 1;
	if (ch->quirks & AHCI_Q_SATA2)
		sata_rev = 2;
	resource_int_value(device_get_name(dev),
	    device_get_unit(dev), "sata_rev", &sata_rev);
	for (i = 0; i < 16; i++) {
		ch->user[i].revision = sata_rev;
		ch->user[i].mode = 0;
		ch->user[i].bytecount = 8192;
		ch->user[i].tags = ch->numslots;
		ch->user[i].caps = 0;
		ch->curr[i] = ch->user[i];
		if (ch->pm_level) {
			ch->user[i].caps = CTS_SATA_CAPS_H_PMREQ |
			    CTS_SATA_CAPS_H_APST |
			    CTS_SATA_CAPS_D_PMREQ | CTS_SATA_CAPS_D_APST;
		}
		ch->user[i].caps |= CTS_SATA_CAPS_H_DMAAA |
		    CTS_SATA_CAPS_H_AN;
	}
	rid = 0;
	if (!(ch->r_mem = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &rid, RF_ACTIVE)))
		return (ENXIO);
	ch->chcaps = ATA_INL(ch->r_mem, AHCI_P_CMD);
	version = ATA_INL(ctlr->r_mem, AHCI_VS);
	if (version < 0x00010200 && (ctlr->caps & AHCI_CAP_FBSS))
		ch->chcaps |= AHCI_P_CMD_FBSCP;
	if (ch->caps2 & AHCI_CAP2_SDS)
		ch->chscaps = ATA_INL(ch->r_mem, AHCI_P_DEVSLP);
	if (bootverbose) {
		device_printf(dev, "Caps:%s%s%s%s%s%s\n",
		    (ch->chcaps & AHCI_P_CMD_HPCP) ? " HPCP":"",
		    (ch->chcaps & AHCI_P_CMD_MPSP) ? " MPSP":"",
		    (ch->chcaps & AHCI_P_CMD_CPD) ? " CPD":"",
		    (ch->chcaps & AHCI_P_CMD_ESP) ? " ESP":"",
		    (ch->chcaps & AHCI_P_CMD_FBSCP) ? " FBSCP":"",
		    (ch->chscaps & AHCI_P_DEVSLP_DSP) ? " DSP":"");
	}
	ahci_dmainit(dev);
	ahci_slotsalloc(dev);
	mtx_lock(&ch->mtx);
	ahci_ch_init(dev);
	rid = ATA_IRQ_RID;
	if (!(ch->r_irq = bus_alloc_resource_any(dev, SYS_RES_IRQ,
	    &rid, RF_SHAREABLE | RF_ACTIVE))) {
		device_printf(dev, "Unable to map interrupt\n");
		error = ENXIO;
		goto err0;
	}
	if ((bus_setup_intr(dev, ch->r_irq, ATA_INTR_FLAGS, NULL,
	    ctlr->direct ? ahci_ch_intr_direct : ahci_ch_intr,
	    ch, &ch->ih))) {
		device_printf(dev, "Unable to setup interrupt\n");
		error = ENXIO;
		goto err1;
	}
	/* Create the device queue for our SIM. */
	devq = cam_simq_alloc(ch->numslots);
	if (devq == NULL) {
		device_printf(dev, "Unable to allocate simq\n");
		error = ENOMEM;
		goto err1;
	}
	/* Construct SIM entry */
	ch->sim = cam_sim_alloc(ahciaction, ahcipoll, "ahcich", ch,
	    device_get_unit(dev), (struct mtx *)&ch->mtx,
	    (ch->quirks & AHCI_Q_NOCCS) ? 1 : min(2, ch->numslots),
	    (ch->caps & AHCI_CAP_SNCQ) ? ch->numslots : 0,
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
		    ahci_ch_pm, ch);
	}
	mtx_unlock(&ch->mtx);
	ctx = device_get_sysctl_ctx(dev);
	tree = device_get_sysctl_tree(dev);
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO, "disable_phy",
	    CTLFLAG_RW | CTLTYPE_UINT, ch, 0, ahci_ch_disablephy_proc, "IU",
	    "Disable PHY");
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
ahci_ch_detach(device_t dev)
{
	struct ahci_channel *ch = device_get_softc(dev);

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

	ahci_ch_deinit(dev);
	ahci_slotsfree(dev);
	ahci_dmafini(dev);

	bus_release_resource(dev, SYS_RES_MEMORY, ch->unit, ch->r_mem);
	mtx_destroy(&ch->mtx);
	return (0);
}

static int
ahci_ch_init(device_t dev)
{
	struct ahci_channel *ch = device_get_softc(dev);
	uint64_t work;

	/* Disable port interrupts */
	ATA_OUTL(ch->r_mem, AHCI_P_IE, 0);
	/* Setup work areas */
	work = ch->dma.work_bus + AHCI_CL_OFFSET;
	ATA_OUTL(ch->r_mem, AHCI_P_CLB, work & 0xffffffff);
	ATA_OUTL(ch->r_mem, AHCI_P_CLBU, work >> 32);
	work = ch->dma.rfis_bus;
	ATA_OUTL(ch->r_mem, AHCI_P_FB, work & 0xffffffff); 
	ATA_OUTL(ch->r_mem, AHCI_P_FBU, work >> 32);
	/* Activate the channel and power/spin up device */
	ATA_OUTL(ch->r_mem, AHCI_P_CMD,
	     (AHCI_P_CMD_ACTIVE | AHCI_P_CMD_POD | AHCI_P_CMD_SUD |
	     ((ch->pm_level == 2 || ch->pm_level == 3) ? AHCI_P_CMD_ALPE : 0) |
	     ((ch->pm_level > 2) ? AHCI_P_CMD_ASP : 0 )));
	ahci_start_fr(ch);
	ahci_start(ch, 1);
	return (0);
}

static int
ahci_ch_deinit(device_t dev)
{
	struct ahci_channel *ch = device_get_softc(dev);

	/* Disable port interrupts. */
	ATA_OUTL(ch->r_mem, AHCI_P_IE, 0);
	/* Reset command register. */
	ahci_stop(ch);
	ahci_stop_fr(ch);
	ATA_OUTL(ch->r_mem, AHCI_P_CMD, 0);
	/* Allow everything, including partial and slumber modes. */
	ATA_OUTL(ch->r_mem, AHCI_P_SCTL, 0);
	/* Request slumber mode transition and give some time to get there. */
	ATA_OUTL(ch->r_mem, AHCI_P_CMD, AHCI_P_CMD_SLUMBER);
	DELAY(100);
	/* Disable PHY. */
	ATA_OUTL(ch->r_mem, AHCI_P_SCTL, ATA_SC_DET_DISABLE);
	return (0);
}

static int
ahci_ch_suspend(device_t dev)
{
	struct ahci_channel *ch = device_get_softc(dev);

	mtx_lock(&ch->mtx);
	xpt_freeze_simq(ch->sim, 1);
	/* Forget about reset. */
	if (ch->resetting) {
		ch->resetting = 0;
		callout_stop(&ch->reset_timer);
		xpt_release_simq(ch->sim, TRUE);
	}
	while (ch->oslots)
		msleep(ch, &ch->mtx, PRIBIO, "ahcisusp", hz/100);
	ahci_ch_deinit(dev);
	mtx_unlock(&ch->mtx);
	return (0);
}

static int
ahci_ch_resume(device_t dev)
{
	struct ahci_channel *ch = device_get_softc(dev);

	mtx_lock(&ch->mtx);
	ahci_ch_init(dev);
	ahci_reset(ch);
	xpt_release_simq(ch->sim, TRUE);
	mtx_unlock(&ch->mtx);
	return (0);
}

devclass_t ahcich_devclass;
static device_method_t ahcich_methods[] = {
	DEVMETHOD(device_probe,     ahci_ch_probe),
	DEVMETHOD(device_attach,    ahci_ch_attach),
	DEVMETHOD(device_detach,    ahci_ch_detach),
	DEVMETHOD(device_suspend,   ahci_ch_suspend),
	DEVMETHOD(device_resume,    ahci_ch_resume),
	DEVMETHOD_END
};
static driver_t ahcich_driver = {
        "ahcich",
        ahcich_methods,
        sizeof(struct ahci_channel)
};
DRIVER_MODULE(ahcich, ahci, ahcich_driver, ahcich_devclass, NULL, NULL);

struct ahci_dc_cb_args {
	bus_addr_t maddr;
	int error;
};

static void
ahci_dmainit(device_t dev)
{
	struct ahci_channel *ch = device_get_softc(dev);
	struct ahci_dc_cb_args dcba;
	size_t rfsize;
	int error;

	/* Command area. */
	error = bus_dma_tag_create(bus_get_dma_tag(dev), 1024, 0,
	    BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR,
	    NULL, NULL, AHCI_WORK_SIZE, 1, AHCI_WORK_SIZE,
	    0, NULL, NULL, &ch->dma.work_tag);
	if (error != 0)
		goto error;
	error = bus_dmamem_alloc(ch->dma.work_tag, (void **)&ch->dma.work,
	    BUS_DMA_ZERO, &ch->dma.work_map);
	if (error != 0)
		goto error;
	error = bus_dmamap_load(ch->dma.work_tag, ch->dma.work_map, ch->dma.work,
	    AHCI_WORK_SIZE, ahci_dmasetupc_cb, &dcba, BUS_DMA_NOWAIT);
	if (error != 0 || (error = dcba.error) != 0) {
		bus_dmamem_free(ch->dma.work_tag, ch->dma.work, ch->dma.work_map);
		goto error;
	}
	ch->dma.work_bus = dcba.maddr;
	/* FIS receive area. */
	if (ch->chcaps & AHCI_P_CMD_FBSCP)
	    rfsize = 4096;
	else
	    rfsize = 256;
	error = bus_dma_tag_create(bus_get_dma_tag(dev), rfsize, 0,
	    BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR,
	    NULL, NULL, rfsize, 1, rfsize,
	    0, NULL, NULL, &ch->dma.rfis_tag);
	if (error != 0)
		goto error;
	error = bus_dmamem_alloc(ch->dma.rfis_tag, (void **)&ch->dma.rfis, 0,
	    &ch->dma.rfis_map);
	if (error != 0)
		goto error;
	error = bus_dmamap_load(ch->dma.rfis_tag, ch->dma.rfis_map, ch->dma.rfis,
	    rfsize, ahci_dmasetupc_cb, &dcba, BUS_DMA_NOWAIT);
	if (error != 0 || (error = dcba.error) != 0) {
		bus_dmamem_free(ch->dma.rfis_tag, ch->dma.rfis, ch->dma.rfis_map);
		goto error;
	}
	ch->dma.rfis_bus = dcba.maddr;
	/* Data area. */
	error = bus_dma_tag_create(bus_get_dma_tag(dev), 2, 0,
	    BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR,
	    NULL, NULL,
	    AHCI_SG_ENTRIES * PAGE_SIZE * ch->numslots,
	    AHCI_SG_ENTRIES, AHCI_PRD_MAX,
	    0, busdma_lock_mutex, &ch->mtx, &ch->dma.data_tag);
	if (error != 0)
		goto error;
	return;

error:
	device_printf(dev, "WARNING - DMA initialization failed, error %d\n",
	    error);
	ahci_dmafini(dev);
}

static void
ahci_dmasetupc_cb(void *xsc, bus_dma_segment_t *segs, int nsegs, int error)
{
	struct ahci_dc_cb_args *dcba = (struct ahci_dc_cb_args *)xsc;

	if (!(dcba->error = error))
		dcba->maddr = segs[0].ds_addr;
}

static void
ahci_dmafini(device_t dev)
{
	struct ahci_channel *ch = device_get_softc(dev);

	if (ch->dma.data_tag) {
		bus_dma_tag_destroy(ch->dma.data_tag);
		ch->dma.data_tag = NULL;
	}
	if (ch->dma.rfis_bus) {
		bus_dmamap_unload(ch->dma.rfis_tag, ch->dma.rfis_map);
		bus_dmamem_free(ch->dma.rfis_tag, ch->dma.rfis, ch->dma.rfis_map);
		ch->dma.rfis_bus = 0;
		ch->dma.rfis = NULL;
	}
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
}

static void
ahci_slotsalloc(device_t dev)
{
	struct ahci_channel *ch = device_get_softc(dev);
	int i;

	/* Alloc and setup command/dma slots */
	bzero(ch->slot, sizeof(ch->slot));
	for (i = 0; i < ch->numslots; i++) {
		struct ahci_slot *slot = &ch->slot[i];

		slot->ch = ch;
		slot->slot = i;
		slot->state = AHCI_SLOT_EMPTY;
		slot->ccb = NULL;
		callout_init_mtx(&slot->timeout, &ch->mtx, 0);

		if (bus_dmamap_create(ch->dma.data_tag, 0, &slot->dma.data_map))
			device_printf(ch->dev, "FAILURE - create data_map\n");
	}
}

static void
ahci_slotsfree(device_t dev)
{
	struct ahci_channel *ch = device_get_softc(dev);
	int i;

	/* Free all dma slots */
	for (i = 0; i < ch->numslots; i++) {
		struct ahci_slot *slot = &ch->slot[i];

		callout_drain(&slot->timeout);
		if (slot->dma.data_map) {
			bus_dmamap_destroy(ch->dma.data_tag, slot->dma.data_map);
			slot->dma.data_map = NULL;
		}
	}
}

static int
ahci_phy_check_events(struct ahci_channel *ch, u_int32_t serr)
{

	if (((ch->pm_level == 0) && (serr & ATA_SE_PHY_CHANGED)) ||
	    ((ch->pm_level != 0 || ch->listening) && (serr & ATA_SE_EXCHANGED))) {
		u_int32_t status = ATA_INL(ch->r_mem, AHCI_P_SSTS);
		union ccb *ccb;

		if (bootverbose) {
			if ((status & ATA_SS_DET_MASK) != ATA_SS_DET_NO_DEVICE)
				device_printf(ch->dev, "CONNECT requested\n");
			else
				device_printf(ch->dev, "DISCONNECT requested\n");
		}
		ahci_reset(ch);
		if ((ccb = xpt_alloc_ccb_nowait()) == NULL)
			return (0);
		if (xpt_create_path(&ccb->ccb_h.path, NULL,
		    cam_sim_path(ch->sim),
		    CAM_TARGET_WILDCARD, CAM_LUN_WILDCARD) != CAM_REQ_CMP) {
			xpt_free_ccb(ccb);
			return (0);
		}
		xpt_rescan(ccb);
		return (1);
	}
	return (0);
}

static void
ahci_cpd_check_events(struct ahci_channel *ch)
{
	u_int32_t status;
	union ccb *ccb;
	device_t dev;

	if (ch->pm_level == 0)
		return;

	status = ATA_INL(ch->r_mem, AHCI_P_CMD);
	if ((status & AHCI_P_CMD_CPD) == 0)
		return;

	if (bootverbose) {
		dev = ch->dev;
		if (status & AHCI_P_CMD_CPS) {
			device_printf(dev, "COLD CONNECT requested\n");
		} else
			device_printf(dev, "COLD DISCONNECT requested\n");
	}
	ahci_reset(ch);
	if ((ccb = xpt_alloc_ccb_nowait()) == NULL)
		return;
	if (xpt_create_path(&ccb->ccb_h.path, NULL, cam_sim_path(ch->sim),
	    CAM_TARGET_WILDCARD, CAM_LUN_WILDCARD) != CAM_REQ_CMP) {
		xpt_free_ccb(ccb);
		return;
	}
	xpt_rescan(ccb);
}

static void
ahci_notify_events(struct ahci_channel *ch, u_int32_t status)
{
	struct cam_path *dpath;
	int i;

	if (ch->caps & AHCI_CAP_SSNTF)
		ATA_OUTL(ch->r_mem, AHCI_P_SNTF, status);
	if (bootverbose)
		device_printf(ch->dev, "SNTF 0x%04x\n", status);
	for (i = 0; i < 16; i++) {
		if ((status & (1 << i)) == 0)
			continue;
		if (xpt_create_path(&dpath, NULL,
		    xpt_path_path_id(ch->path), i, 0) == CAM_REQ_CMP) {
			xpt_async(AC_SCSI_AEN, dpath, NULL);
			xpt_free_path(dpath);
		}
	}
}

static void
ahci_done(struct ahci_channel *ch, union ccb *ccb)
{

	mtx_assert(&ch->mtx, MA_OWNED);
	if ((ccb->ccb_h.func_code & XPT_FC_QUEUED) == 0 ||
	    ch->batch == 0) {
		xpt_done(ccb);
		return;
	}

	STAILQ_INSERT_TAIL(&ch->doneq, &ccb->ccb_h, sim_links.stqe);
}

static void
ahci_ch_intr(void *arg)
{
	struct ahci_channel *ch = (struct ahci_channel *)arg;
	uint32_t istatus;

	/* Read interrupt statuses. */
	istatus = ATA_INL(ch->r_mem, AHCI_P_IS);

	mtx_lock(&ch->mtx);
	ahci_ch_intr_main(ch, istatus);
	mtx_unlock(&ch->mtx);
}

static void
ahci_ch_intr_direct(void *arg)
{
	struct ahci_channel *ch = (struct ahci_channel *)arg;
	struct ccb_hdr *ccb_h;
	uint32_t istatus;
	STAILQ_HEAD(, ccb_hdr) tmp_doneq = STAILQ_HEAD_INITIALIZER(tmp_doneq);

	/* Read interrupt statuses. */
	istatus = ATA_INL(ch->r_mem, AHCI_P_IS);

	mtx_lock(&ch->mtx);
	ch->batch = 1;
	ahci_ch_intr_main(ch, istatus);
	ch->batch = 0;
	/*
	 * Prevent the possibility of issues caused by processing the queue
	 * while unlocked below by moving the contents to a local queue.
	 */
	STAILQ_CONCAT(&tmp_doneq, &ch->doneq);
	mtx_unlock(&ch->mtx);
	while ((ccb_h = STAILQ_FIRST(&tmp_doneq)) != NULL) {
		STAILQ_REMOVE_HEAD(&tmp_doneq, sim_links.stqe);
		xpt_done_direct((union ccb *)ccb_h);
	}
}

static void
ahci_ch_pm(void *arg)
{
	struct ahci_channel *ch = (struct ahci_channel *)arg;
	uint32_t work;

	if (ch->numrslots != 0)
		return;
	work = ATA_INL(ch->r_mem, AHCI_P_CMD);
	if (ch->pm_level == 4)
		work |= AHCI_P_CMD_PARTIAL;
	else
		work |= AHCI_P_CMD_SLUMBER;
	ATA_OUTL(ch->r_mem, AHCI_P_CMD, work);
}

static void
ahci_ch_intr_main(struct ahci_channel *ch, uint32_t istatus)
{
	uint32_t cstatus, serr = 0, sntf = 0, ok, err;
	enum ahci_err_type et;
	int i, ccs, port, reset = 0;

	/* Clear interrupt statuses. */
	ATA_OUTL(ch->r_mem, AHCI_P_IS, istatus);
	/* Read command statuses. */
	if (ch->numtslots != 0)
		cstatus = ATA_INL(ch->r_mem, AHCI_P_SACT);
	else
		cstatus = 0;
	if (ch->numrslots != ch->numtslots)
		cstatus |= ATA_INL(ch->r_mem, AHCI_P_CI);
	/* Read SNTF in one of possible ways. */
	if ((istatus & AHCI_P_IX_SDB) &&
	    (ch->pm_present || ch->curr[0].atapi != 0)) {
		if (ch->caps & AHCI_CAP_SSNTF)
			sntf = ATA_INL(ch->r_mem, AHCI_P_SNTF);
		else if (ch->fbs_enabled) {
			u_int8_t *fis = ch->dma.rfis + 0x58;

			for (i = 0; i < 16; i++) {
				if (fis[1] & 0x80) {
					fis[1] &= 0x7f;
	    				sntf |= 1 << i;
	    			}
	    			fis += 256;
	    		}
		} else {
			u_int8_t *fis = ch->dma.rfis + 0x58;

			if (fis[1] & 0x80)
				sntf = (1 << (fis[1] & 0x0f));
		}
	}
	/* Process PHY events */
	if (istatus & (AHCI_P_IX_PC | AHCI_P_IX_PRC | AHCI_P_IX_OF |
	    AHCI_P_IX_IF | AHCI_P_IX_HBD | AHCI_P_IX_HBF | AHCI_P_IX_TFE)) {
		serr = ATA_INL(ch->r_mem, AHCI_P_SERR);
		if (serr) {
			ATA_OUTL(ch->r_mem, AHCI_P_SERR, serr);
			reset = ahci_phy_check_events(ch, serr);
		}
	}
	/* Process cold presence detection events */
	if ((istatus & AHCI_P_IX_CPD) && !reset)
		ahci_cpd_check_events(ch);
	/* Process command errors */
	if (istatus & (AHCI_P_IX_OF | AHCI_P_IX_IF |
	    AHCI_P_IX_HBD | AHCI_P_IX_HBF | AHCI_P_IX_TFE)) {
		if (ch->quirks & AHCI_Q_NOCCS) {
			/*
			 * ASMedia chips sometimes report failed commands as
			 * completed.  Count all running commands as failed.
			 */
			cstatus |= ch->rslots;

			/* They also report wrong CCS, so try to guess one. */
			ccs = powerof2(cstatus) ? ffs(cstatus) - 1 : -1;
		} else {
			ccs = (ATA_INL(ch->r_mem, AHCI_P_CMD) &
			    AHCI_P_CMD_CCS_MASK) >> AHCI_P_CMD_CCS_SHIFT;
		}
//device_printf(dev, "%s ERROR is %08x cs %08x ss %08x rs %08x tfd %02x serr %08x fbs %08x ccs %d\n",
//    __func__, istatus, cstatus, sstatus, ch->rslots, ATA_INL(ch->r_mem, AHCI_P_TFD),
//    serr, ATA_INL(ch->r_mem, AHCI_P_FBS), ccs);
		port = -1;
		if (ch->fbs_enabled) {
			uint32_t fbs = ATA_INL(ch->r_mem, AHCI_P_FBS);
			if (fbs & AHCI_P_FBS_SDE) {
				port = (fbs & AHCI_P_FBS_DWE)
				    >> AHCI_P_FBS_DWE_SHIFT;
			} else {
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
			}
		}
		err = ch->rslots & cstatus;
	} else {
		ccs = 0;
		err = 0;
		port = -1;
	}
	/* Complete all successful commands. */
	ok = ch->rslots & ~cstatus;
	for (i = 0; i < ch->numslots; i++) {
		if ((ok >> i) & 1)
			ahci_end_transaction(&ch->slot[i], AHCI_ERR_NONE);
	}
	/* On error, complete the rest of commands with error statuses. */
	if (err) {
		if (ch->frozen) {
			union ccb *fccb = ch->frozen;
			ch->frozen = NULL;
			fccb->ccb_h.status = CAM_REQUEUE_REQ | CAM_RELEASE_SIMQ;
			if (!(fccb->ccb_h.status & CAM_DEV_QFRZN)) {
				xpt_freeze_devq(fccb->ccb_h.path, 1);
				fccb->ccb_h.status |= CAM_DEV_QFRZN;
			}
			ahci_done(ch, fccb);
		}
		for (i = 0; i < ch->numslots; i++) {
			/* XXX: reqests in loading state. */
			if (((err >> i) & 1) == 0)
				continue;
			if (port >= 0 &&
			    ch->slot[i].ccb->ccb_h.target_id != port)
				continue;
			if (istatus & AHCI_P_IX_TFE) {
			    if (port != -2) {
				/* Task File Error */
				if (ch->numtslotspd[
				    ch->slot[i].ccb->ccb_h.target_id] == 0) {
					/* Untagged operation. */
					if (i == ccs)
						et = AHCI_ERR_TFE;
					else
						et = AHCI_ERR_INNOCENT;
				} else {
					/* Tagged operation. */
					et = AHCI_ERR_NCQ;
				}
			    } else {
				et = AHCI_ERR_TFE;
				ch->fatalerr = 1;
			    }
			} else if (istatus & AHCI_P_IX_IF) {
				if (ch->numtslots == 0 && i != ccs && port != -2)
					et = AHCI_ERR_INNOCENT;
				else
					et = AHCI_ERR_SATA;
			} else
				et = AHCI_ERR_INVALID;
			ahci_end_transaction(&ch->slot[i], et);
		}
		/*
		 * We can't reinit port if there are some other
		 * commands active, use resume to complete them.
		 */
		if (ch->rslots != 0 && !ch->recoverycmd)
			ATA_OUTL(ch->r_mem, AHCI_P_FBS, AHCI_P_FBS_EN | AHCI_P_FBS_DEC);
	}
	/* Process NOTIFY events */
	if (sntf)
		ahci_notify_events(ch, sntf);
}

/* Must be called with channel locked. */
static int
ahci_check_collision(struct ahci_channel *ch, union ccb *ccb)
{
	int t = ccb->ccb_h.target_id;

	if ((ccb->ccb_h.func_code == XPT_ATA_IO) &&
	    (ccb->ataio.cmd.flags & CAM_ATAIO_FPDMA)) {
		/* Tagged command while we have no supported tag free. */
		if (((~ch->oslots) & (0xffffffff >> (32 -
		    ch->curr[t].tags))) == 0)
			return (1);
		/* If we have FBS */
		if (ch->fbs_enabled) {
			/* Tagged command while untagged are active. */
			if (ch->numrslotspd[t] != 0 && ch->numtslotspd[t] == 0)
				return (1);
		} else {
			/* Tagged command while untagged are active. */
			if (ch->numrslots != 0 && ch->numtslots == 0)
				return (1);
			/* Tagged command while tagged to other target is active. */
			if (ch->numtslots != 0 &&
			    ch->taggedtarget != ccb->ccb_h.target_id)
				return (1);
		}
	} else {
		/* If we have FBS */
		if (ch->fbs_enabled) {
			/* Untagged command while tagged are active. */
			if (ch->numrslotspd[t] != 0 && ch->numtslotspd[t] != 0)
				return (1);
		} else {
			/* Untagged command while tagged are active. */
			if (ch->numrslots != 0 && ch->numtslots != 0)
				return (1);
		}
	}
	if ((ccb->ccb_h.func_code == XPT_ATA_IO) &&
	    (ccb->ataio.cmd.flags & (CAM_ATAIO_CONTROL | CAM_ATAIO_NEEDRESULT))) {
		/* Atomic command while anything active. */
		if (ch->numrslots != 0)
			return (1);
	}
       /* We have some atomic command running. */
       if (ch->aslots != 0)
               return (1);
	return (0);
}

/* Must be called with channel locked. */
static void
ahci_begin_transaction(struct ahci_channel *ch, union ccb *ccb)
{
	struct ahci_slot *slot;
	int tag, tags;

	/* Choose empty slot. */
	tags = ch->numslots;
	if ((ccb->ccb_h.func_code == XPT_ATA_IO) &&
	    (ccb->ataio.cmd.flags & CAM_ATAIO_FPDMA))
		tags = ch->curr[ccb->ccb_h.target_id].tags;
	if (ch->lastslot + 1 < tags)
		tag = ffs(~(ch->oslots >> (ch->lastslot + 1)));
	else
		tag = 0;
	if (tag == 0 || tag + ch->lastslot >= tags)
		tag = ffs(~ch->oslots) - 1;
	else
		tag += ch->lastslot;
	ch->lastslot = tag;
	/* Occupy chosen slot. */
	slot = &ch->slot[tag];
	slot->ccb = ccb;
	/* Stop PM timer. */
	if (ch->numrslots == 0 && ch->pm_level > 3)
		callout_stop(&ch->pm_timer);
	/* Update channel stats. */
	ch->oslots |= (1 << tag);
	ch->numrslots++;
	ch->numrslotspd[ccb->ccb_h.target_id]++;
	if ((ccb->ccb_h.func_code == XPT_ATA_IO) &&
	    (ccb->ataio.cmd.flags & CAM_ATAIO_FPDMA)) {
		ch->numtslots++;
		ch->numtslotspd[ccb->ccb_h.target_id]++;
		ch->taggedtarget = ccb->ccb_h.target_id;
	}
	if ((ccb->ccb_h.func_code == XPT_ATA_IO) &&
	    (ccb->ataio.cmd.flags & (CAM_ATAIO_CONTROL | CAM_ATAIO_NEEDRESULT)))
		ch->aslots |= (1 << tag);
	if ((ccb->ccb_h.flags & CAM_DIR_MASK) != CAM_DIR_NONE) {
		slot->state = AHCI_SLOT_LOADING;
		bus_dmamap_load_ccb(ch->dma.data_tag, slot->dma.data_map, ccb,
		    ahci_dmasetprd, slot, 0);
	} else {
		slot->dma.nsegs = 0;
		ahci_execute_transaction(slot);
	}
}

/* Locked by busdma engine. */
static void
ahci_dmasetprd(void *arg, bus_dma_segment_t *segs, int nsegs, int error)
{    
	struct ahci_slot *slot = arg;
	struct ahci_channel *ch = slot->ch;
	struct ahci_cmd_tab *ctp;
	struct ahci_dma_prd *prd;
	int i;

	if (error) {
		device_printf(ch->dev, "DMA load error\n");
		ahci_end_transaction(slot, AHCI_ERR_INVALID);
		return;
	}
	KASSERT(nsegs <= AHCI_SG_ENTRIES, ("too many DMA segment entries\n"));
	/* Get a piece of the workspace for this request */
	ctp = (struct ahci_cmd_tab *)
		(ch->dma.work + AHCI_CT_OFFSET + (AHCI_CT_SIZE * slot->slot));
	/* Fill S/G table */
	prd = &ctp->prd_tab[0];
	for (i = 0; i < nsegs; i++) {
		prd[i].dba = htole64(segs[i].ds_addr);
		prd[i].dbc = htole32((segs[i].ds_len - 1) & AHCI_PRD_MASK);
	}
	slot->dma.nsegs = nsegs;
	bus_dmamap_sync(ch->dma.data_tag, slot->dma.data_map,
	    ((slot->ccb->ccb_h.flags & CAM_DIR_IN) ?
	    BUS_DMASYNC_PREREAD : BUS_DMASYNC_PREWRITE));
	ahci_execute_transaction(slot);
}

/* Must be called with channel locked. */
static void
ahci_execute_transaction(struct ahci_slot *slot)
{
	struct ahci_channel *ch = slot->ch;
	struct ahci_cmd_tab *ctp;
	struct ahci_cmd_list *clp;
	union ccb *ccb = slot->ccb;
	int port = ccb->ccb_h.target_id & 0x0f;
	int fis_size, i, softreset;
	uint8_t *fis = ch->dma.rfis + 0x40;
	uint8_t val;
	uint16_t cmd_flags;

	/* Get a piece of the workspace for this request */
	ctp = (struct ahci_cmd_tab *)
		(ch->dma.work + AHCI_CT_OFFSET + (AHCI_CT_SIZE * slot->slot));
	/* Setup the FIS for this request */
	if (!(fis_size = ahci_setup_fis(ch, ctp, ccb, slot->slot))) {
		device_printf(ch->dev, "Setting up SATA FIS failed\n");
		ahci_end_transaction(slot, AHCI_ERR_INVALID);
		return;
	}
	/* Setup the command list entry */
	clp = (struct ahci_cmd_list *)
	    (ch->dma.work + AHCI_CL_OFFSET + (AHCI_CL_SIZE * slot->slot));
	cmd_flags =
		    (ccb->ccb_h.flags & CAM_DIR_OUT ? AHCI_CMD_WRITE : 0) |
		    (ccb->ccb_h.func_code == XPT_SCSI_IO ?
		     (AHCI_CMD_ATAPI | AHCI_CMD_PREFETCH) : 0) |
		    (fis_size / sizeof(u_int32_t)) |
		    (port << 12);
	clp->prd_length = htole16(slot->dma.nsegs);
	/* Special handling for Soft Reset command. */
	if ((ccb->ccb_h.func_code == XPT_ATA_IO) &&
	    (ccb->ataio.cmd.flags & CAM_ATAIO_CONTROL)) {
		if (ccb->ataio.cmd.control & ATA_A_RESET) {
			softreset = 1;
			/* Kick controller into sane state */
			ahci_stop(ch);
			ahci_clo(ch);
			ahci_start(ch, 0);
			cmd_flags |= AHCI_CMD_RESET | AHCI_CMD_CLR_BUSY;
		} else {
			softreset = 2;
			/* Prepare FIS receive area for check. */
			for (i = 0; i < 20; i++)
				fis[i] = 0xff;
		}
	} else
		softreset = 0;
	clp->bytecount = 0;
	clp->cmd_flags = htole16(cmd_flags);
	clp->cmd_table_phys = htole64(ch->dma.work_bus + AHCI_CT_OFFSET +
				  (AHCI_CT_SIZE * slot->slot));
	bus_dmamap_sync(ch->dma.work_tag, ch->dma.work_map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(ch->dma.rfis_tag, ch->dma.rfis_map,
	    BUS_DMASYNC_PREREAD);
	/* Set ACTIVE bit for NCQ commands. */
	if ((ccb->ccb_h.func_code == XPT_ATA_IO) &&
	    (ccb->ataio.cmd.flags & CAM_ATAIO_FPDMA)) {
		ATA_OUTL(ch->r_mem, AHCI_P_SACT, 1 << slot->slot);
	}
	/* If FBS is enabled, set PMP port. */
	if (ch->fbs_enabled) {
		ATA_OUTL(ch->r_mem, AHCI_P_FBS, AHCI_P_FBS_EN |
		    (port << AHCI_P_FBS_DEV_SHIFT));
	}
	/* Issue command to the controller. */
	slot->state = AHCI_SLOT_RUNNING;
	ch->rslots |= (1 << slot->slot);
	ATA_OUTL(ch->r_mem, AHCI_P_CI, (1 << slot->slot));
	/* Device reset commands doesn't interrupt. Poll them. */
	if (ccb->ccb_h.func_code == XPT_ATA_IO &&
	    (ccb->ataio.cmd.command == ATA_DEVICE_RESET || softreset)) {
		int count, timeout = ccb->ccb_h.timeout * 100;
		enum ahci_err_type et = AHCI_ERR_NONE;

		for (count = 0; count < timeout; count++) {
			DELAY(10);
			if (!(ATA_INL(ch->r_mem, AHCI_P_CI) & (1 << slot->slot)))
				break;
			if ((ATA_INL(ch->r_mem, AHCI_P_TFD) & ATA_S_ERROR) &&
			    softreset != 1) {
#if 0
				device_printf(ch->dev,
				    "Poll error on slot %d, TFD: %04x\n",
				    slot->slot, ATA_INL(ch->r_mem, AHCI_P_TFD));
#endif
				et = AHCI_ERR_TFE;
				break;
			}
			/* Workaround for ATI SB600/SB700 chipsets. */
			if (ccb->ccb_h.target_id == 15 &&
			    (ch->quirks & AHCI_Q_ATI_PMP_BUG) &&
			    (ATA_INL(ch->r_mem, AHCI_P_IS) & AHCI_P_IX_IPM)) {
				et = AHCI_ERR_TIMEOUT;
				break;
			}
		}

		/*
		 * Some Marvell controllers require additional time
		 * after soft reset to work properly. Setup delay
		 * to 50ms after soft reset.
		 */
		if (ch->quirks & AHCI_Q_MRVL_SR_DEL)
			DELAY(50000);

		/*
		 * Marvell HBAs with non-RAID firmware do not wait for
		 * readiness after soft reset, so we have to wait here.
		 * Marvell RAIDs do not have this problem, but instead
		 * sometimes forget to update FIS receive area, breaking
		 * this wait.
		 */
		if ((ch->quirks & AHCI_Q_NOBSYRES) == 0 &&
		    (ch->quirks & AHCI_Q_ATI_PMP_BUG) == 0 &&
		    softreset == 2 && et == AHCI_ERR_NONE) {
			for ( ; count < timeout; count++) {
				bus_dmamap_sync(ch->dma.rfis_tag,
				    ch->dma.rfis_map, BUS_DMASYNC_POSTREAD);
				val = fis[2];
				bus_dmamap_sync(ch->dma.rfis_tag,
				    ch->dma.rfis_map, BUS_DMASYNC_PREREAD);
				if ((val & ATA_S_BUSY) == 0)
					break;
				DELAY(10);
			}
		}

		if (timeout && (count >= timeout)) {
			device_printf(ch->dev, "Poll timeout on slot %d port %d\n",
			    slot->slot, port);
			device_printf(ch->dev, "is %08x cs %08x ss %08x "
			    "rs %08x tfd %02x serr %08x cmd %08x\n",
			    ATA_INL(ch->r_mem, AHCI_P_IS),
			    ATA_INL(ch->r_mem, AHCI_P_CI),
			    ATA_INL(ch->r_mem, AHCI_P_SACT), ch->rslots,
			    ATA_INL(ch->r_mem, AHCI_P_TFD),
			    ATA_INL(ch->r_mem, AHCI_P_SERR),
			    ATA_INL(ch->r_mem, AHCI_P_CMD));
			et = AHCI_ERR_TIMEOUT;
		}

		/* Kick controller into sane state and enable FBS. */
		if (softreset == 2)
			ch->eslots |= (1 << slot->slot);
		ahci_end_transaction(slot, et);
		return;
	}
	/* Start command execution timeout */
	callout_reset_sbt(&slot->timeout, SBT_1MS * ccb->ccb_h.timeout / 2,
	    0, (timeout_t*)ahci_timeout, slot, 0);
	return;
}

/* Must be called with channel locked. */
static void
ahci_process_timeout(struct ahci_channel *ch)
{
	int i;

	mtx_assert(&ch->mtx, MA_OWNED);
	/* Handle the rest of commands. */
	for (i = 0; i < ch->numslots; i++) {
		/* Do we have a running request on slot? */
		if (ch->slot[i].state < AHCI_SLOT_RUNNING)
			continue;
		ahci_end_transaction(&ch->slot[i], AHCI_ERR_TIMEOUT);
	}
}

/* Must be called with channel locked. */
static void
ahci_rearm_timeout(struct ahci_channel *ch)
{
	int i;

	mtx_assert(&ch->mtx, MA_OWNED);
	for (i = 0; i < ch->numslots; i++) {
		struct ahci_slot *slot = &ch->slot[i];

		/* Do we have a running request on slot? */
		if (slot->state < AHCI_SLOT_RUNNING)
			continue;
		if ((ch->toslots & (1 << i)) == 0)
			continue;
		callout_reset_sbt(&slot->timeout,
    	    	    SBT_1MS * slot->ccb->ccb_h.timeout / 2, 0,
		    (timeout_t*)ahci_timeout, slot, 0);
	}
}

/* Locked by callout mechanism. */
static void
ahci_timeout(struct ahci_slot *slot)
{
	struct ahci_channel *ch = slot->ch;
	device_t dev = ch->dev;
	uint32_t sstatus;
	int ccs;
	int i;

	/* Check for stale timeout. */
	if (slot->state < AHCI_SLOT_RUNNING)
		return;

	/* Check if slot was not being executed last time we checked. */
	if (slot->state < AHCI_SLOT_EXECUTING) {
		/* Check if slot started executing. */
		sstatus = ATA_INL(ch->r_mem, AHCI_P_SACT);
		ccs = (ATA_INL(ch->r_mem, AHCI_P_CMD) & AHCI_P_CMD_CCS_MASK)
		    >> AHCI_P_CMD_CCS_SHIFT;
		if ((sstatus & (1 << slot->slot)) != 0 || ccs == slot->slot ||
		    ch->fbs_enabled || ch->wrongccs)
			slot->state = AHCI_SLOT_EXECUTING;
		else if ((ch->rslots & (1 << ccs)) == 0) {
			ch->wrongccs = 1;
			slot->state = AHCI_SLOT_EXECUTING;
		}

		callout_reset_sbt(&slot->timeout,
	    	    SBT_1MS * slot->ccb->ccb_h.timeout / 2, 0,
		    (timeout_t*)ahci_timeout, slot, 0);
		return;
	}

	device_printf(dev, "Timeout on slot %d port %d\n",
	    slot->slot, slot->ccb->ccb_h.target_id & 0x0f);
	device_printf(dev, "is %08x cs %08x ss %08x rs %08x tfd %02x "
	    "serr %08x cmd %08x\n",
	    ATA_INL(ch->r_mem, AHCI_P_IS), ATA_INL(ch->r_mem, AHCI_P_CI),
	    ATA_INL(ch->r_mem, AHCI_P_SACT), ch->rslots,
	    ATA_INL(ch->r_mem, AHCI_P_TFD), ATA_INL(ch->r_mem, AHCI_P_SERR),
	    ATA_INL(ch->r_mem, AHCI_P_CMD));

	/* Handle frozen command. */
	if (ch->frozen) {
		union ccb *fccb = ch->frozen;
		ch->frozen = NULL;
		fccb->ccb_h.status = CAM_REQUEUE_REQ | CAM_RELEASE_SIMQ;
		if (!(fccb->ccb_h.status & CAM_DEV_QFRZN)) {
			xpt_freeze_devq(fccb->ccb_h.path, 1);
			fccb->ccb_h.status |= CAM_DEV_QFRZN;
		}
		ahci_done(ch, fccb);
	}
	if (!ch->fbs_enabled && !ch->wrongccs) {
		/* Without FBS we know real timeout source. */
		ch->fatalerr = 1;
		/* Handle command with timeout. */
		ahci_end_transaction(&ch->slot[slot->slot], AHCI_ERR_TIMEOUT);
		/* Handle the rest of commands. */
		for (i = 0; i < ch->numslots; i++) {
			/* Do we have a running request on slot? */
			if (ch->slot[i].state < AHCI_SLOT_RUNNING)
				continue;
			ahci_end_transaction(&ch->slot[i], AHCI_ERR_INNOCENT);
		}
	} else {
		/* With FBS we wait for other commands timeout and pray. */
		if (ch->toslots == 0)
			xpt_freeze_simq(ch->sim, 1);
		ch->toslots |= (1 << slot->slot);
		if ((ch->rslots & ~ch->toslots) == 0)
			ahci_process_timeout(ch);
		else
			device_printf(dev, " ... waiting for slots %08x\n",
			    ch->rslots & ~ch->toslots);
	}
}

/* Must be called with channel locked. */
static void
ahci_end_transaction(struct ahci_slot *slot, enum ahci_err_type et)
{
	struct ahci_channel *ch = slot->ch;
	union ccb *ccb = slot->ccb;
	struct ahci_cmd_list *clp;
	int lastto;
	uint32_t sig;

	bus_dmamap_sync(ch->dma.work_tag, ch->dma.work_map,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
	clp = (struct ahci_cmd_list *)
	    (ch->dma.work + AHCI_CL_OFFSET + (AHCI_CL_SIZE * slot->slot));
	/* Read result registers to the result struct
	 * May be incorrect if several commands finished same time,
	 * so read only when sure or have to.
	 */
	if (ccb->ccb_h.func_code == XPT_ATA_IO) {
		struct ata_res *res = &ccb->ataio.res;

		if ((et == AHCI_ERR_TFE) ||
		    (ccb->ataio.cmd.flags & CAM_ATAIO_NEEDRESULT)) {
			u_int8_t *fis = ch->dma.rfis + 0x40;

			bus_dmamap_sync(ch->dma.rfis_tag, ch->dma.rfis_map,
			    BUS_DMASYNC_POSTREAD);
			if (ch->fbs_enabled) {
				fis += ccb->ccb_h.target_id * 256;
				res->status = fis[2];
				res->error = fis[3];
			} else {
				uint16_t tfd = ATA_INL(ch->r_mem, AHCI_P_TFD);

				res->status = tfd;
				res->error = tfd >> 8;
			}
			res->lba_low = fis[4];
			res->lba_mid = fis[5];
			res->lba_high = fis[6];
			res->device = fis[7];
			res->lba_low_exp = fis[8];
			res->lba_mid_exp = fis[9];
			res->lba_high_exp = fis[10];
			res->sector_count = fis[12];
			res->sector_count_exp = fis[13];

			/*
			 * Some weird controllers do not return signature in
			 * FIS receive area. Read it from PxSIG register.
			 */
			if ((ch->quirks & AHCI_Q_ALTSIG) &&
			    (ccb->ataio.cmd.flags & CAM_ATAIO_CONTROL) &&
			    (ccb->ataio.cmd.control & ATA_A_RESET) == 0) {
				sig = ATA_INL(ch->r_mem,  AHCI_P_SIG);
				res->lba_high = sig >> 24;
				res->lba_mid = sig >> 16;
				res->lba_low = sig >> 8;
				res->sector_count = sig;
			}
		} else
			bzero(res, sizeof(*res));
		if ((ccb->ataio.cmd.flags & CAM_ATAIO_FPDMA) == 0 &&
		    (ccb->ccb_h.flags & CAM_DIR_MASK) != CAM_DIR_NONE &&
		    (ch->quirks & AHCI_Q_NOCOUNT) == 0) {
			ccb->ataio.resid =
			    ccb->ataio.dxfer_len - le32toh(clp->bytecount);
		}
	} else {
		if ((ccb->ccb_h.flags & CAM_DIR_MASK) != CAM_DIR_NONE &&
		    (ch->quirks & AHCI_Q_NOCOUNT) == 0) {
			ccb->csio.resid =
			    ccb->csio.dxfer_len - le32toh(clp->bytecount);
		}
	}
	if ((ccb->ccb_h.flags & CAM_DIR_MASK) != CAM_DIR_NONE) {
		bus_dmamap_sync(ch->dma.data_tag, slot->dma.data_map,
		    (ccb->ccb_h.flags & CAM_DIR_IN) ?
		    BUS_DMASYNC_POSTREAD : BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(ch->dma.data_tag, slot->dma.data_map);
	}
	if (et != AHCI_ERR_NONE)
		ch->eslots |= (1 << slot->slot);
	/* In case of error, freeze device for proper recovery. */
	if ((et != AHCI_ERR_NONE) && (!ch->recoverycmd) &&
	    !(ccb->ccb_h.status & CAM_DEV_QFRZN)) {
		xpt_freeze_devq(ccb->ccb_h.path, 1);
		ccb->ccb_h.status |= CAM_DEV_QFRZN;
	}
	/* Set proper result status. */
	ccb->ccb_h.status &= ~CAM_STATUS_MASK;
	switch (et) {
	case AHCI_ERR_NONE:
		ccb->ccb_h.status |= CAM_REQ_CMP;
		if (ccb->ccb_h.func_code == XPT_SCSI_IO)
			ccb->csio.scsi_status = SCSI_STATUS_OK;
		break;
	case AHCI_ERR_INVALID:
		ch->fatalerr = 1;
		ccb->ccb_h.status |= CAM_REQ_INVALID;
		break;
	case AHCI_ERR_INNOCENT:
		ccb->ccb_h.status |= CAM_REQUEUE_REQ;
		break;
	case AHCI_ERR_TFE:
	case AHCI_ERR_NCQ:
		if (ccb->ccb_h.func_code == XPT_SCSI_IO) {
			ccb->ccb_h.status |= CAM_SCSI_STATUS_ERROR;
			ccb->csio.scsi_status = SCSI_STATUS_CHECK_COND;
		} else {
			ccb->ccb_h.status |= CAM_ATA_STATUS_ERROR;
		}
		break;
	case AHCI_ERR_SATA:
		ch->fatalerr = 1;
		if (!ch->recoverycmd) {
			xpt_freeze_simq(ch->sim, 1);
			ccb->ccb_h.status &= ~CAM_STATUS_MASK;
			ccb->ccb_h.status |= CAM_RELEASE_SIMQ;
		}
		ccb->ccb_h.status |= CAM_UNCOR_PARITY;
		break;
	case AHCI_ERR_TIMEOUT:
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
	slot->state = AHCI_SLOT_EMPTY;
	slot->ccb = NULL;
	/* Update channel stats. */
	ch->numrslots--;
	ch->numrslotspd[ccb->ccb_h.target_id]--;
	if ((ccb->ccb_h.func_code == XPT_ATA_IO) &&
	    (ccb->ataio.cmd.flags & CAM_ATAIO_FPDMA)) {
		ch->numtslots--;
		ch->numtslotspd[ccb->ccb_h.target_id]--;
	}
	/* Cancel timeout state if request completed normally. */
	if (et != AHCI_ERR_TIMEOUT) {
		lastto = (ch->toslots == (1 << slot->slot));
		ch->toslots &= ~(1 << slot->slot);
		if (lastto)
			xpt_release_simq(ch->sim, TRUE);
	}
	/* If it was first request of reset sequence and there is no error,
	 * proceed to second request. */
	if ((ccb->ccb_h.func_code == XPT_ATA_IO) &&
	    (ccb->ataio.cmd.flags & CAM_ATAIO_CONTROL) &&
	    (ccb->ataio.cmd.control & ATA_A_RESET) &&
	    et == AHCI_ERR_NONE) {
		ccb->ataio.cmd.control &= ~ATA_A_RESET;
		ahci_begin_transaction(ch, ccb);
		return;
	}
	/* If it was our READ LOG command - process it. */
	if (ccb->ccb_h.recovery_type == RECOVERY_READ_LOG) {
		ahci_process_read_log(ch, ccb);
	/* If it was our REQUEST SENSE command - process it. */
	} else if (ccb->ccb_h.recovery_type == RECOVERY_REQUEST_SENSE) {
		ahci_process_request_sense(ch, ccb);
	/* If it was NCQ or ATAPI command error, put result on hold. */
	} else if (et == AHCI_ERR_NCQ ||
	    ((ccb->ccb_h.status & CAM_STATUS_MASK) == CAM_SCSI_STATUS_ERROR &&
	     (ccb->ccb_h.flags & CAM_DIS_AUTOSENSE) == 0)) {
		ch->hold[slot->slot] = ccb;
		ch->numhslots++;
	} else
		ahci_done(ch, ccb);
	/* If we have no other active commands, ... */
	if (ch->rslots == 0) {
		/* if there was fatal error - reset port. */
		if (ch->toslots != 0 || ch->fatalerr) {
			ahci_reset(ch);
		} else {
			/* if we have slots in error, we can reinit port. */
			if (ch->eslots != 0) {
				ahci_stop(ch);
				ahci_clo(ch);
				ahci_start(ch, 1);
			}
			/* if there commands on hold, we can do READ LOG. */
			if (!ch->recoverycmd && ch->numhslots)
				ahci_issue_recovery(ch);
		}
	/* If all the rest of commands are in timeout - give them chance. */
	} else if ((ch->rslots & ~ch->toslots) == 0 &&
	    et != AHCI_ERR_TIMEOUT)
		ahci_rearm_timeout(ch);
	/* Unfreeze frozen command. */
	if (ch->frozen && !ahci_check_collision(ch, ch->frozen)) {
		union ccb *fccb = ch->frozen;
		ch->frozen = NULL;
		ahci_begin_transaction(ch, fccb);
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
ahci_issue_recovery(struct ahci_channel *ch)
{
	union ccb *ccb;
	struct ccb_ataio *ataio;
	struct ccb_scsiio *csio;
	int i;

	/* Find some held command. */
	for (i = 0; i < ch->numslots; i++) {
		if (ch->hold[i])
			break;
	}
	ccb = xpt_alloc_ccb_nowait();
	if (ccb == NULL) {
		device_printf(ch->dev, "Unable to allocate recovery command\n");
completeall:
		/* We can't do anything -- complete held commands. */
		for (i = 0; i < ch->numslots; i++) {
			if (ch->hold[i] == NULL)
				continue;
			ch->hold[i]->ccb_h.status &= ~CAM_STATUS_MASK;
			ch->hold[i]->ccb_h.status |= CAM_RESRC_UNAVAIL;
			ahci_done(ch, ch->hold[i]);
			ch->hold[i] = NULL;
			ch->numhslots--;
		}
		ahci_reset(ch);
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
		ataio->data_ptr = malloc(512, M_AHCI, M_NOWAIT);
		if (ataio->data_ptr == NULL) {
			xpt_free_ccb(ccb);
			device_printf(ch->dev,
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
	ahci_begin_transaction(ch, ccb);
}

static void
ahci_process_read_log(struct ahci_channel *ch, union ccb *ccb)
{
	uint8_t *data;
	struct ata_res *res;
	int i;

	ch->recoverycmd = 0;

	data = ccb->ataio.data_ptr;
	if ((ccb->ccb_h.status & CAM_STATUS_MASK) == CAM_REQ_CMP &&
	    (data[0] & 0x80) == 0) {
		for (i = 0; i < ch->numslots; i++) {
			if (!ch->hold[i])
				continue;
			if (ch->hold[i]->ccb_h.func_code != XPT_ATA_IO)
				continue;
			if ((data[0] & 0x1F) == i) {
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
			ahci_done(ch, ch->hold[i]);
			ch->hold[i] = NULL;
			ch->numhslots--;
		}
	} else {
		if ((ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP)
			device_printf(ch->dev, "Error while READ LOG EXT\n");
		else if ((data[0] & 0x80) == 0) {
			device_printf(ch->dev, "Non-queued command error in READ LOG EXT\n");
		}
		for (i = 0; i < ch->numslots; i++) {
			if (!ch->hold[i])
				continue;
			if (ch->hold[i]->ccb_h.func_code != XPT_ATA_IO)
				continue;
			ahci_done(ch, ch->hold[i]);
			ch->hold[i] = NULL;
			ch->numhslots--;
		}
	}
	free(ccb->ataio.data_ptr, M_AHCI);
	xpt_free_ccb(ccb);
	xpt_release_simq(ch->sim, TRUE);
}

static void
ahci_process_request_sense(struct ahci_channel *ch, union ccb *ccb)
{
	int i;

	ch->recoverycmd = 0;

	i = ccb->ccb_h.recovery_slot;
	if ((ccb->ccb_h.status & CAM_STATUS_MASK) == CAM_REQ_CMP) {
		ch->hold[i]->ccb_h.status |= CAM_AUTOSNS_VALID;
	} else {
		ch->hold[i]->ccb_h.status &= ~CAM_STATUS_MASK;
		ch->hold[i]->ccb_h.status |= CAM_AUTOSENSE_FAIL;
	}
	ahci_done(ch, ch->hold[i]);
	ch->hold[i] = NULL;
	ch->numhslots--;
	xpt_free_ccb(ccb);
	xpt_release_simq(ch->sim, TRUE);
}

static void
ahci_start(struct ahci_channel *ch, int fbs)
{
	u_int32_t cmd;

	/* Run the channel start callback, if any. */
	if (ch->start)
		ch->start(ch);

	/* Clear SATA error register */
	ATA_OUTL(ch->r_mem, AHCI_P_SERR, 0xFFFFFFFF);
	/* Clear any interrupts pending on this channel */
	ATA_OUTL(ch->r_mem, AHCI_P_IS, 0xFFFFFFFF);
	/* Configure FIS-based switching if supported. */
	if (ch->chcaps & AHCI_P_CMD_FBSCP) {
		ch->fbs_enabled = (fbs && ch->pm_present) ? 1 : 0;
		ATA_OUTL(ch->r_mem, AHCI_P_FBS,
		    ch->fbs_enabled ? AHCI_P_FBS_EN : 0);
	}
	/* Start operations on this channel */
	cmd = ATA_INL(ch->r_mem, AHCI_P_CMD);
	cmd &= ~AHCI_P_CMD_PMA;
	ATA_OUTL(ch->r_mem, AHCI_P_CMD, cmd | AHCI_P_CMD_ST |
	    (ch->pm_present ? AHCI_P_CMD_PMA : 0));
}

static void
ahci_stop(struct ahci_channel *ch)
{
	u_int32_t cmd;
	int timeout;

	/* Kill all activity on this channel */
	cmd = ATA_INL(ch->r_mem, AHCI_P_CMD);
	ATA_OUTL(ch->r_mem, AHCI_P_CMD, cmd & ~AHCI_P_CMD_ST);
	/* Wait for activity stop. */
	timeout = 0;
	do {
		DELAY(10);
		if (timeout++ > 50000) {
			device_printf(ch->dev, "stopping AHCI engine failed\n");
			break;
		}
	} while (ATA_INL(ch->r_mem, AHCI_P_CMD) & AHCI_P_CMD_CR);
	ch->eslots = 0;
}

static void
ahci_clo(struct ahci_channel *ch)
{
	u_int32_t cmd;
	int timeout;

	/* Issue Command List Override if supported */ 
	if (ch->caps & AHCI_CAP_SCLO) {
		cmd = ATA_INL(ch->r_mem, AHCI_P_CMD);
		cmd |= AHCI_P_CMD_CLO;
		ATA_OUTL(ch->r_mem, AHCI_P_CMD, cmd);
		timeout = 0;
		do {
			DELAY(10);
			if (timeout++ > 50000) {
			    device_printf(ch->dev, "executing CLO failed\n");
			    break;
			}
		} while (ATA_INL(ch->r_mem, AHCI_P_CMD) & AHCI_P_CMD_CLO);
	}
}

static void
ahci_stop_fr(struct ahci_channel *ch)
{
	u_int32_t cmd;
	int timeout;

	/* Kill all FIS reception on this channel */
	cmd = ATA_INL(ch->r_mem, AHCI_P_CMD);
	ATA_OUTL(ch->r_mem, AHCI_P_CMD, cmd & ~AHCI_P_CMD_FRE);
	/* Wait for FIS reception stop. */
	timeout = 0;
	do {
		DELAY(10);
		if (timeout++ > 50000) {
			device_printf(ch->dev, "stopping AHCI FR engine failed\n");
			break;
		}
	} while (ATA_INL(ch->r_mem, AHCI_P_CMD) & AHCI_P_CMD_FR);
}

static void
ahci_start_fr(struct ahci_channel *ch)
{
	u_int32_t cmd;

	/* Start FIS reception on this channel */
	cmd = ATA_INL(ch->r_mem, AHCI_P_CMD);
	ATA_OUTL(ch->r_mem, AHCI_P_CMD, cmd | AHCI_P_CMD_FRE);
}

static int
ahci_wait_ready(struct ahci_channel *ch, int t, int t0)
{
	int timeout = 0;
	uint32_t val;

	while ((val = ATA_INL(ch->r_mem, AHCI_P_TFD)) &
	    (ATA_S_BUSY | ATA_S_DRQ)) {
		if (timeout > t) {
			if (t != 0) {
				device_printf(ch->dev,
				    "AHCI reset: device not ready after %dms "
				    "(tfd = %08x)\n",
				    MAX(t, 0) + t0, val);
			}
			return (EBUSY);
		}
		DELAY(1000);
		timeout++;
	}
	if (bootverbose)
		device_printf(ch->dev, "AHCI reset: device ready after %dms\n",
		    timeout + t0);
	return (0);
}

static void
ahci_reset_to(void *arg)
{
	struct ahci_channel *ch = arg;

	if (ch->resetting == 0)
		return;
	ch->resetting--;
	if (ahci_wait_ready(ch, ch->resetting == 0 ? -1 : 0,
	    (310 - ch->resetting) * 100) == 0) {
		ch->resetting = 0;
		ahci_start(ch, 1);
		xpt_release_simq(ch->sim, TRUE);
		return;
	}
	if (ch->resetting == 0) {
		ahci_clo(ch);
		ahci_start(ch, 1);
		xpt_release_simq(ch->sim, TRUE);
		return;
	}
	callout_schedule(&ch->reset_timer, hz / 10);
}

static void
ahci_reset(struct ahci_channel *ch)
{
	struct ahci_controller *ctlr = device_get_softc(device_get_parent(ch->dev));
	int i;

	xpt_freeze_simq(ch->sim, 1);
	if (bootverbose)
		device_printf(ch->dev, "AHCI reset...\n");
	/* Forget about previous reset. */
	if (ch->resetting) {
		ch->resetting = 0;
		callout_stop(&ch->reset_timer);
		xpt_release_simq(ch->sim, TRUE);
	}
	/* Requeue freezed command. */
	if (ch->frozen) {
		union ccb *fccb = ch->frozen;
		ch->frozen = NULL;
		fccb->ccb_h.status = CAM_REQUEUE_REQ | CAM_RELEASE_SIMQ;
		if (!(fccb->ccb_h.status & CAM_DEV_QFRZN)) {
			xpt_freeze_devq(fccb->ccb_h.path, 1);
			fccb->ccb_h.status |= CAM_DEV_QFRZN;
		}
		ahci_done(ch, fccb);
	}
	/* Kill the engine and requeue all running commands. */
	ahci_stop(ch);
	for (i = 0; i < ch->numslots; i++) {
		/* Do we have a running request on slot? */
		if (ch->slot[i].state < AHCI_SLOT_RUNNING)
			continue;
		/* XXX; Commands in loading state. */
		ahci_end_transaction(&ch->slot[i], AHCI_ERR_INNOCENT);
	}
	for (i = 0; i < ch->numslots; i++) {
		if (!ch->hold[i])
			continue;
		ahci_done(ch, ch->hold[i]);
		ch->hold[i] = NULL;
		ch->numhslots--;
	}
	if (ch->toslots != 0)
		xpt_release_simq(ch->sim, TRUE);
	ch->eslots = 0;
	ch->toslots = 0;
	ch->wrongccs = 0;
	ch->fatalerr = 0;
	/* Tell the XPT about the event */
	xpt_async(AC_BUS_RESET, ch->path, NULL);
	/* Disable port interrupts */
	ATA_OUTL(ch->r_mem, AHCI_P_IE, 0);
	/* Reset and reconnect PHY, */
	if (!ahci_sata_phy_reset(ch)) {
		if (bootverbose)
			device_printf(ch->dev,
			    "AHCI reset: device not found\n");
		ch->devices = 0;
		/* Enable wanted port interrupts */
		ATA_OUTL(ch->r_mem, AHCI_P_IE,
		    (((ch->pm_level != 0) ? AHCI_P_IX_CPD | AHCI_P_IX_MP : 0) |
		     AHCI_P_IX_PRC | AHCI_P_IX_PC));
		xpt_release_simq(ch->sim, TRUE);
		return;
	}
	if (bootverbose)
		device_printf(ch->dev, "AHCI reset: device found\n");
	/* Wait for clearing busy status. */
	if (ahci_wait_ready(ch, dumping ? 31000 : 0, 0)) {
		if (dumping)
			ahci_clo(ch);
		else
			ch->resetting = 310;
	}
	ch->devices = 1;
	/* Enable wanted port interrupts */
	ATA_OUTL(ch->r_mem, AHCI_P_IE,
	     (((ch->pm_level != 0) ? AHCI_P_IX_CPD | AHCI_P_IX_MP : 0) |
	      AHCI_P_IX_TFE | AHCI_P_IX_HBF |
	      AHCI_P_IX_HBD | AHCI_P_IX_IF | AHCI_P_IX_OF |
	      ((ch->pm_level == 0) ? AHCI_P_IX_PRC : 0) | AHCI_P_IX_PC |
	      AHCI_P_IX_DP | AHCI_P_IX_UF | (ctlr->ccc ? 0 : AHCI_P_IX_SDB) |
	      AHCI_P_IX_DS | AHCI_P_IX_PS | (ctlr->ccc ? 0 : AHCI_P_IX_DHR)));
	if (ch->resetting)
		callout_reset(&ch->reset_timer, hz / 10, ahci_reset_to, ch);
	else {
		ahci_start(ch, 1);
		xpt_release_simq(ch->sim, TRUE);
	}
}

static int
ahci_setup_fis(struct ahci_channel *ch, struct ahci_cmd_tab *ctp, union ccb *ccb, int tag)
{
	u_int8_t *fis = &ctp->cfis[0];

	bzero(fis, 20);
	fis[0] = 0x27;  		/* host to device */
	fis[1] = (ccb->ccb_h.target_id & 0x0f);
	if (ccb->ccb_h.func_code == XPT_SCSI_IO) {
		fis[1] |= 0x80;
		fis[2] = ATA_PACKET_CMD;
		if ((ccb->ccb_h.flags & CAM_DIR_MASK) != CAM_DIR_NONE &&
		    ch->curr[ccb->ccb_h.target_id].mode >= ATA_DMA)
			fis[3] = ATA_F_DMA;
		else {
			fis[5] = ccb->csio.dxfer_len;
		        fis[6] = ccb->csio.dxfer_len >> 8;
		}
		fis[7] = ATA_D_LBA;
		fis[15] = ATA_A_4BIT;
		bcopy((ccb->ccb_h.flags & CAM_CDB_POINTER) ?
		    ccb->csio.cdb_io.cdb_ptr : ccb->csio.cdb_io.cdb_bytes,
		    ctp->acmd, ccb->csio.cdb_len);
		bzero(ctp->acmd + ccb->csio.cdb_len, 32 - ccb->csio.cdb_len);
	} else if ((ccb->ataio.cmd.flags & CAM_ATAIO_CONTROL) == 0) {
		fis[1] |= 0x80;
		fis[2] = ccb->ataio.cmd.command;
		fis[3] = ccb->ataio.cmd.features;
		fis[4] = ccb->ataio.cmd.lba_low;
		fis[5] = ccb->ataio.cmd.lba_mid;
		fis[6] = ccb->ataio.cmd.lba_high;
		fis[7] = ccb->ataio.cmd.device;
		fis[8] = ccb->ataio.cmd.lba_low_exp;
		fis[9] = ccb->ataio.cmd.lba_mid_exp;
		fis[10] = ccb->ataio.cmd.lba_high_exp;
		fis[11] = ccb->ataio.cmd.features_exp;
		if (ccb->ataio.cmd.flags & CAM_ATAIO_FPDMA) {
			fis[12] = tag << 3;
		} else {
			fis[12] = ccb->ataio.cmd.sector_count;
		}
		fis[13] = ccb->ataio.cmd.sector_count_exp;
		fis[15] = ATA_A_4BIT;
	} else {
		fis[15] = ccb->ataio.cmd.control;
	}
	if (ccb->ataio.ata_flags & ATA_FLAG_AUX) {
		fis[16] =  ccb->ataio.aux        & 0xff;
		fis[17] = (ccb->ataio.aux >>  8) & 0xff;
		fis[18] = (ccb->ataio.aux >> 16) & 0xff;
		fis[19] = (ccb->ataio.aux >> 24) & 0xff;
	}
	return (20);
}

static int
ahci_sata_connect(struct ahci_channel *ch)
{
	u_int32_t status;
	int timeout, found = 0;

	/* Wait up to 100ms for "connect well" */
	for (timeout = 0; timeout < 1000 ; timeout++) {
		status = ATA_INL(ch->r_mem, AHCI_P_SSTS);
		if ((status & ATA_SS_DET_MASK) != ATA_SS_DET_NO_DEVICE)
			found = 1;
		if (((status & ATA_SS_DET_MASK) == ATA_SS_DET_PHY_ONLINE) &&
		    ((status & ATA_SS_SPD_MASK) != ATA_SS_SPD_NO_SPEED) &&
		    ((status & ATA_SS_IPM_MASK) == ATA_SS_IPM_ACTIVE))
			break;
		if ((status & ATA_SS_DET_MASK) == ATA_SS_DET_PHY_OFFLINE) {
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
	ATA_OUTL(ch->r_mem, AHCI_P_SERR, 0xffffffff);
	return (1);
}

static int
ahci_sata_phy_reset(struct ahci_channel *ch)
{
	int sata_rev;
	uint32_t val, detval;

	if (ch->listening) {
		val = ATA_INL(ch->r_mem, AHCI_P_CMD);
		val |= AHCI_P_CMD_SUD;
		ATA_OUTL(ch->r_mem, AHCI_P_CMD, val);
		ch->listening = 0;
	}
	sata_rev = ch->user[ch->pm_present ? 15 : 0].revision;
	if (sata_rev == 1)
		val = ATA_SC_SPD_SPEED_GEN1;
	else if (sata_rev == 2)
		val = ATA_SC_SPD_SPEED_GEN2;
	else if (sata_rev == 3)
		val = ATA_SC_SPD_SPEED_GEN3;
	else
		val = 0;
	detval = ahci_ch_detval(ch, ATA_SC_DET_RESET);
	ATA_OUTL(ch->r_mem, AHCI_P_SCTL,
	    detval | val |
	    ATA_SC_IPM_DIS_PARTIAL | ATA_SC_IPM_DIS_SLUMBER);
	DELAY(1000);
	detval = ahci_ch_detval(ch, ATA_SC_DET_IDLE);
	ATA_OUTL(ch->r_mem, AHCI_P_SCTL,
	    detval | val | ((ch->pm_level > 0) ? 0 :
	    (ATA_SC_IPM_DIS_PARTIAL | ATA_SC_IPM_DIS_SLUMBER)));
	if (!ahci_sata_connect(ch)) {
		if (ch->caps & AHCI_CAP_SSS) {
			val = ATA_INL(ch->r_mem, AHCI_P_CMD);
			val &= ~AHCI_P_CMD_SUD;
			ATA_OUTL(ch->r_mem, AHCI_P_CMD, val);
			ch->listening = 1;
		} else if (ch->pm_level > 0)
			ATA_OUTL(ch->r_mem, AHCI_P_SCTL, ATA_SC_DET_DISABLE);
		return (0);
	}
	return (1);
}

static int
ahci_check_ids(struct ahci_channel *ch, union ccb *ccb)
{

	if (ccb->ccb_h.target_id > ((ch->caps & AHCI_CAP_SPM) ? 15 : 0)) {
		ccb->ccb_h.status = CAM_TID_INVALID;
		ahci_done(ch, ccb);
		return (-1);
	}
	if (ccb->ccb_h.target_lun != 0) {
		ccb->ccb_h.status = CAM_LUN_INVALID;
		ahci_done(ch, ccb);
		return (-1);
	}
	return (0);
}

static void
ahciaction(struct cam_sim *sim, union ccb *ccb)
{
	struct ahci_channel *ch;

	CAM_DEBUG(ccb->ccb_h.path, CAM_DEBUG_TRACE, ("ahciaction func_code=%x\n",
	    ccb->ccb_h.func_code));

	ch = (struct ahci_channel *)cam_sim_softc(sim);
	switch (ccb->ccb_h.func_code) {
	/* Common cases first */
	case XPT_ATA_IO:	/* Execute the requested I/O operation */
	case XPT_SCSI_IO:
		if (ahci_check_ids(ch, ccb))
			return;
		if (ch->devices == 0 ||
		    (ch->pm_present == 0 &&
		     ccb->ccb_h.target_id > 0 && ccb->ccb_h.target_id < 15)) {
			ccb->ccb_h.status = CAM_SEL_TIMEOUT;
			break;
		}
		ccb->ccb_h.recovery_type = RECOVERY_NONE;
		/* Check for command collision. */
		if (ahci_check_collision(ch, ccb)) {
			/* Freeze command. */
			ch->frozen = ccb;
			/* We have only one frozen slot, so freeze simq also. */
			xpt_freeze_simq(ch->sim, 1);
			return;
		}
		ahci_begin_transaction(ch, ccb);
		return;
	case XPT_ABORT:			/* Abort the specified CCB */
		/* XXX Implement */
		ccb->ccb_h.status = CAM_REQ_INVALID;
		break;
	case XPT_SET_TRAN_SETTINGS:
	{
		struct	ccb_trans_settings *cts = &ccb->cts;
		struct	ahci_device *d; 

		if (ahci_check_ids(ch, ccb))
			return;
		if (cts->type == CTS_TYPE_CURRENT_SETTINGS)
			d = &ch->curr[ccb->ccb_h.target_id];
		else
			d = &ch->user[ccb->ccb_h.target_id];
		if (cts->xport_specific.sata.valid & CTS_SATA_VALID_REVISION)
			d->revision = cts->xport_specific.sata.revision;
		if (cts->xport_specific.sata.valid & CTS_SATA_VALID_MODE)
			d->mode = cts->xport_specific.sata.mode;
		if (cts->xport_specific.sata.valid & CTS_SATA_VALID_BYTECOUNT)
			d->bytecount = min(8192, cts->xport_specific.sata.bytecount);
		if (cts->xport_specific.sata.valid & CTS_SATA_VALID_TAGS)
			d->tags = min(ch->numslots, cts->xport_specific.sata.tags);
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
		struct  ahci_device *d;
		uint32_t status;

		if (ahci_check_ids(ch, ccb))
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
			status = ATA_INL(ch->r_mem, AHCI_P_SSTS) & ATA_SS_SPD_MASK;
			if (status & 0x0f0) {
				cts->xport_specific.sata.revision =
				    (status & 0x0f0) >> 4;
				cts->xport_specific.sata.valid |=
				    CTS_SATA_VALID_REVISION;
			}
			cts->xport_specific.sata.caps = d->caps & CTS_SATA_CAPS_D;
			if (ch->pm_level) {
				if (ch->caps & (AHCI_CAP_PSC | AHCI_CAP_SSC))
					cts->xport_specific.sata.caps |= CTS_SATA_CAPS_H_PMREQ;
				if (ch->caps2 & AHCI_CAP2_APST)
					cts->xport_specific.sata.caps |= CTS_SATA_CAPS_H_APST;
			}
			if ((ch->caps & AHCI_CAP_SNCQ) &&
			    (ch->quirks & AHCI_Q_NOAA) == 0)
				cts->xport_specific.sata.caps |= CTS_SATA_CAPS_H_DMAAA;
			cts->xport_specific.sata.caps |= CTS_SATA_CAPS_H_AN;
			cts->xport_specific.sata.caps &=
			    ch->user[ccb->ccb_h.target_id].caps;
			cts->xport_specific.sata.valid |= CTS_SATA_VALID_CAPS;
		} else {
			cts->xport_specific.sata.revision = d->revision;
			cts->xport_specific.sata.valid |= CTS_SATA_VALID_REVISION;
			cts->xport_specific.sata.caps = d->caps;
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
		ahci_reset(ch);
		ccb->ccb_h.status = CAM_REQ_CMP;
		break;
	case XPT_TERM_IO:		/* Terminate the I/O process */
		/* XXX Implement */
		ccb->ccb_h.status = CAM_REQ_INVALID;
		break;
	case XPT_PATH_INQ:		/* Path routing inquiry */
	{
		struct ccb_pathinq *cpi = &ccb->cpi;

		cpi->version_num = 1; /* XXX??? */
		cpi->hba_inquiry = PI_SDTR_ABLE;
		if (ch->caps & AHCI_CAP_SNCQ)
			cpi->hba_inquiry |= PI_TAG_ABLE;
		if (ch->caps & AHCI_CAP_SPM)
			cpi->hba_inquiry |= PI_SATAPM;
		cpi->target_sprt = 0;
		cpi->hba_misc = PIM_SEQSCAN | PIM_UNMAPPED;
		if ((ch->quirks & AHCI_Q_NOAUX) == 0)
			cpi->hba_misc |= PIM_ATA_EXT;
		cpi->hba_eng_cnt = 0;
		if (ch->caps & AHCI_CAP_SPM)
			cpi->max_target = 15;
		else
			cpi->max_target = 0;
		cpi->max_lun = 0;
		cpi->initiator_id = 0;
		cpi->bus_id = cam_sim_bus(sim);
		cpi->base_transfer_speed = 150000;
		strlcpy(cpi->sim_vid, "FreeBSD", SIM_IDLEN);
		strlcpy(cpi->hba_vid, "AHCI", HBA_IDLEN);
		strlcpy(cpi->dev_name, cam_sim_name(sim), DEV_IDLEN);
		cpi->unit_number = cam_sim_unit(sim);
		cpi->transport = XPORT_SATA;
		cpi->transport_version = XPORT_VERSION_UNSPECIFIED;
		cpi->protocol = PROTO_ATA;
		cpi->protocol_version = PROTO_VERSION_UNSPECIFIED;
		cpi->maxio = MAXPHYS;
		/* ATI SB600 can't handle 256 sectors with FPDMA (NCQ). */
		if (ch->quirks & AHCI_Q_MAXIO_64K)
			cpi->maxio = min(cpi->maxio, 128 * 512);
		cpi->hba_vendor = ch->vendorid;
		cpi->hba_device = ch->deviceid;
		cpi->hba_subvendor = ch->subvendorid;
		cpi->hba_subdevice = ch->subdeviceid;
		cpi->ccb_h.status = CAM_REQ_CMP;
		break;
	}
	default:
		ccb->ccb_h.status = CAM_REQ_INVALID;
		break;
	}
	ahci_done(ch, ccb);
}

static void
ahcipoll(struct cam_sim *sim)
{
	struct ahci_channel *ch = (struct ahci_channel *)cam_sim_softc(sim);
	uint32_t istatus;

	/* Read interrupt statuses and process if any. */
	istatus = ATA_INL(ch->r_mem, AHCI_P_IS);
	if (istatus != 0)
		ahci_ch_intr_main(ch, istatus);
	if (ch->resetting != 0 &&
	    (--ch->resetpolldiv <= 0 || !callout_pending(&ch->reset_timer))) {
		ch->resetpolldiv = 1000;
		ahci_reset_to(ch);
	}
}

devclass_t ahci_devclass;

MODULE_VERSION(ahci, 1);
MODULE_DEPEND(ahci, cam, 1, 1, 1);
