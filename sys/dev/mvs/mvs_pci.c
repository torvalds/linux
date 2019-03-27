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
#include <sys/bus.h>
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
#include <dev/pci/pcireg.h>
#include "mvs.h"

/* local prototypes */
static int mvs_setup_interrupt(device_t dev);
static void mvs_intr(void *data);
static int mvs_suspend(device_t dev);
static int mvs_resume(device_t dev);
static int mvs_ctlr_setup(device_t dev);

static struct {
	uint32_t	id;
	uint8_t		rev;
	const char	*name;
	int		ports;
	int		quirks;
} mvs_ids[] = {
	{0x504011ab, 0x00, "Marvell 88SX5040",	4,	MVS_Q_GENI},
	{0x504111ab, 0x00, "Marvell 88SX5041",	4,	MVS_Q_GENI},
	{0x508011ab, 0x00, "Marvell 88SX5080",	8,	MVS_Q_GENI},
	{0x508111ab, 0x00, "Marvell 88SX5081",	8,	MVS_Q_GENI},
	{0x604011ab, 0x00, "Marvell 88SX6040",	4,	MVS_Q_GENII},
	{0x604111ab, 0x00, "Marvell 88SX6041",	4,	MVS_Q_GENII},
	{0x604211ab, 0x00, "Marvell 88SX6042",	4,	MVS_Q_GENIIE},
	{0x608011ab, 0x00, "Marvell 88SX6080",	8,	MVS_Q_GENII},
	{0x608111ab, 0x00, "Marvell 88SX6081",	8,	MVS_Q_GENII},
	{0x704211ab, 0x00, "Marvell 88SX7042",	4,	MVS_Q_GENIIE|MVS_Q_CT},
	{0x02419005, 0x00, "Adaptec 1420SA",	4,	MVS_Q_GENII},
	{0x02439005, 0x00, "Adaptec 1430SA",	4,	MVS_Q_GENIIE|MVS_Q_CT},
	{0x00000000, 0x00, NULL,	0,	0}
};

static int
mvs_probe(device_t dev)
{
	char buf[64];
	int i;
	uint32_t devid = pci_get_devid(dev);
	uint8_t revid = pci_get_revid(dev);

	for (i = 0; mvs_ids[i].id != 0; i++) {
		if (mvs_ids[i].id == devid &&
		    mvs_ids[i].rev <= revid) {
			snprintf(buf, sizeof(buf), "%s SATA controller",
			    mvs_ids[i].name);
			device_set_desc_copy(dev, buf);
			return (BUS_PROBE_DEFAULT);
		}
	}
	return (ENXIO);
}

static int
mvs_attach(device_t dev)
{
	struct mvs_controller *ctlr = device_get_softc(dev);
	device_t child;
	int	error, unit, i;
	uint32_t devid = pci_get_devid(dev);
	uint8_t revid = pci_get_revid(dev);

	ctlr->dev = dev;
	i = 0;
	while (mvs_ids[i].id != 0 &&
	    (mvs_ids[i].id != devid ||
	     mvs_ids[i].rev > revid))
		i++;
	ctlr->channels = mvs_ids[i].ports;
	ctlr->quirks = mvs_ids[i].quirks;
	ctlr->ccc = 0;
	resource_int_value(device_get_name(dev),
	    device_get_unit(dev), "ccc", &ctlr->ccc);
	ctlr->cccc = 8;
	resource_int_value(device_get_name(dev),
	    device_get_unit(dev), "cccc", &ctlr->cccc);
	if (ctlr->ccc == 0 || ctlr->cccc == 0) {
		ctlr->ccc = 0;
		ctlr->cccc = 0;
	}
	if (ctlr->ccc > 100000)
		ctlr->ccc = 100000;
	device_printf(dev,
	    "Gen-%s, %d %sGbps ports, Port Multiplier %s%s\n",
	    ((ctlr->quirks & MVS_Q_GENI) ? "I" :
	     ((ctlr->quirks & MVS_Q_GENII) ? "II" : "IIe")),
	    ctlr->channels,
	    ((ctlr->quirks & MVS_Q_GENI) ? "1.5" : "3"),
	    ((ctlr->quirks & MVS_Q_GENI) ?
	    "not supported" : "supported"),
	    ((ctlr->quirks & MVS_Q_GENIIE) ?
	    " with FBS" : ""));
	mtx_init(&ctlr->mtx, "MVS controller lock", NULL, MTX_DEF);
	/* We should have a memory BAR(0). */
	ctlr->r_rid = PCIR_BAR(0);
	if (!(ctlr->r_mem = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &ctlr->r_rid, RF_ACTIVE)))
		return ENXIO;
	/* Setup our own memory management for channels. */
	ctlr->sc_iomem.rm_start = rman_get_start(ctlr->r_mem);
	ctlr->sc_iomem.rm_end = rman_get_end(ctlr->r_mem);
	ctlr->sc_iomem.rm_type = RMAN_ARRAY;
	ctlr->sc_iomem.rm_descr = "I/O memory addresses";
	if ((error = rman_init(&ctlr->sc_iomem)) != 0) {
		bus_release_resource(dev, SYS_RES_MEMORY, ctlr->r_rid, ctlr->r_mem);
		return (error);
	}
	if ((error = rman_manage_region(&ctlr->sc_iomem,
	    rman_get_start(ctlr->r_mem), rman_get_end(ctlr->r_mem))) != 0) {
		bus_release_resource(dev, SYS_RES_MEMORY, ctlr->r_rid, ctlr->r_mem);
		rman_fini(&ctlr->sc_iomem);
		return (error);
	}
	pci_enable_busmaster(dev);
	mvs_ctlr_setup(dev);
	/* Setup interrupts. */
	if (mvs_setup_interrupt(dev)) {
		bus_release_resource(dev, SYS_RES_MEMORY, ctlr->r_rid, ctlr->r_mem);
		rman_fini(&ctlr->sc_iomem);
		return ENXIO;
	}
	/* Attach all channels on this controller */
	for (unit = 0; unit < ctlr->channels; unit++) {
		child = device_add_child(dev, "mvsch", -1);
		if (child == NULL)
			device_printf(dev, "failed to add channel device\n");
		else
			device_set_ivars(child, (void *)(intptr_t)unit);
	}
	bus_generic_attach(dev);
	return 0;
}

static int
mvs_detach(device_t dev)
{
	struct mvs_controller *ctlr = device_get_softc(dev);

	/* Detach & delete all children */
	device_delete_children(dev);

	/* Free interrupt. */
	if (ctlr->irq.r_irq) {
		bus_teardown_intr(dev, ctlr->irq.r_irq,
		    ctlr->irq.handle);
		bus_release_resource(dev, SYS_RES_IRQ,
		    ctlr->irq.r_irq_rid, ctlr->irq.r_irq);
	}
	pci_release_msi(dev);
	/* Free memory. */
	rman_fini(&ctlr->sc_iomem);
	if (ctlr->r_mem)
		bus_release_resource(dev, SYS_RES_MEMORY, ctlr->r_rid, ctlr->r_mem);
	mtx_destroy(&ctlr->mtx);
	return (0);
}

static int
mvs_ctlr_setup(device_t dev)
{
	struct mvs_controller *ctlr = device_get_softc(dev);
	int i, ccc = ctlr->ccc, cccc = ctlr->cccc, ccim = 0;

	/* Mask chip interrupts */
	ATA_OUTL(ctlr->r_mem, CHIP_MIM, 0x00000000);
	/* Mask PCI interrupts */
	ATA_OUTL(ctlr->r_mem, CHIP_PCIIM, 0x00000000);
	/* Clear PCI interrupts */
	ATA_OUTL(ctlr->r_mem, CHIP_PCIIC, 0x00000000);
	if (ccc && bootverbose) {
		device_printf(dev,
		    "CCC with %dus/%dcmd enabled\n",
		    ctlr->ccc, ctlr->cccc);
	}
	ccc *= 150;
	/* Configure chip-global CCC */
	if (ctlr->channels > 4 && (ctlr->quirks & MVS_Q_GENI) == 0) {
		ATA_OUTL(ctlr->r_mem, CHIP_ICT, cccc);
		ATA_OUTL(ctlr->r_mem, CHIP_ITT, ccc);
		ATA_OUTL(ctlr->r_mem, CHIP_ICC, ~CHIP_ICC_ALL_PORTS);
		if (ccc)
			ccim |= IC_ALL_PORTS_COAL_DONE;
		ccc = 0;
		cccc = 0;
	}
	for (i = 0; i < ctlr->channels / 4; i++) {
		/* Configure per-HC CCC */
		ATA_OUTL(ctlr->r_mem, HC_BASE(i) + HC_ICT, cccc);
		ATA_OUTL(ctlr->r_mem, HC_BASE(i) + HC_ITT, ccc);
		if (ccc)
			ccim |= (IC_HC0_COAL_DONE << (i * IC_HC_SHIFT));
		/* Clear HC interrupts */
		ATA_OUTL(ctlr->r_mem, HC_BASE(i) + HC_IC, 0x00000000);
	}
	/* Enable chip interrupts */
	ctlr->gmim = (ccim ? ccim : (IC_DONE_HC0 | IC_DONE_HC1)) |
	     IC_ERR_HC0 | IC_ERR_HC1;
	ctlr->mim = ctlr->gmim | ctlr->pmim;
	ATA_OUTL(ctlr->r_mem, CHIP_MIM, ctlr->mim);
	/* Enable PCI interrupts */
	ATA_OUTL(ctlr->r_mem, CHIP_PCIIM, 0x007fffff);
	return (0);
}

static void
mvs_edma(device_t dev, device_t child, int mode)
{
	struct mvs_controller *ctlr = device_get_softc(dev);
	int unit = ((struct mvs_channel *)device_get_softc(child))->unit;
	int bit = IC_DONE_IRQ << (unit * 2 + unit / 4) ;

	if (ctlr->ccc == 0)
		return;
	/* CCC is not working for non-EDMA mode. Unmask device interrupts. */
	mtx_lock(&ctlr->mtx);
	if (mode == MVS_EDMA_OFF)
		ctlr->pmim |= bit;
	else
		ctlr->pmim &= ~bit;
	ctlr->mim = ctlr->gmim | ctlr->pmim;
	if (!ctlr->msia)
		ATA_OUTL(ctlr->r_mem, CHIP_MIM, ctlr->mim);
	mtx_unlock(&ctlr->mtx);
}

static int
mvs_suspend(device_t dev)
{
	struct mvs_controller *ctlr = device_get_softc(dev);

	bus_generic_suspend(dev);
	/* Mask chip interrupts */
	ATA_OUTL(ctlr->r_mem, CHIP_MIM, 0x00000000);
	/* Mask PCI interrupts */
	ATA_OUTL(ctlr->r_mem, CHIP_PCIIM, 0x00000000);
	return 0;
}

static int
mvs_resume(device_t dev)
{

	mvs_ctlr_setup(dev);
	return (bus_generic_resume(dev));
}

static int
mvs_setup_interrupt(device_t dev)
{
	struct mvs_controller *ctlr = device_get_softc(dev);
	int msi = 0;

	/* Process hints. */
	resource_int_value(device_get_name(dev),
	    device_get_unit(dev), "msi", &msi);
	if (msi < 0)
		msi = 0;
	else if (msi > 0)
		msi = min(1, pci_msi_count(dev));
	/* Allocate MSI if needed/present. */
	if (msi && pci_alloc_msi(dev, &msi) != 0)
		msi = 0;
	ctlr->msi = msi;
	/* Allocate all IRQs. */
	ctlr->irq.r_irq_rid = msi ? 1 : 0;
	if (!(ctlr->irq.r_irq = bus_alloc_resource_any(dev, SYS_RES_IRQ,
	    &ctlr->irq.r_irq_rid, RF_SHAREABLE | RF_ACTIVE))) {
		device_printf(dev, "unable to map interrupt\n");
		return (ENXIO);
	}
	if ((bus_setup_intr(dev, ctlr->irq.r_irq, ATA_INTR_FLAGS, NULL,
	    mvs_intr, ctlr, &ctlr->irq.handle))) {
		device_printf(dev, "unable to setup interrupt\n");
		bus_release_resource(dev, SYS_RES_IRQ,
		    ctlr->irq.r_irq_rid, ctlr->irq.r_irq);
		ctlr->irq.r_irq = NULL;
		return (ENXIO);
	}
	return (0);
}

/*
 * Common case interrupt handler.
 */
static void
mvs_intr(void *data)
{
	struct mvs_controller *ctlr = data;
	struct mvs_intr_arg arg;
	void (*function)(void *);
	int p;
	u_int32_t ic, aic;

	ic = ATA_INL(ctlr->r_mem, CHIP_MIC);
	if (ctlr->msi) {
		/* We have to mask MSI during processing. */
		mtx_lock(&ctlr->mtx);
		ATA_OUTL(ctlr->r_mem, CHIP_MIM, 0);
		ctlr->msia = 1; /* Deny MIM update during processing. */
		mtx_unlock(&ctlr->mtx);
	} else if (ic == 0)
		return;
	/* Acknowledge all-ports CCC interrupt. */
	if (ic & IC_ALL_PORTS_COAL_DONE)
		ATA_OUTL(ctlr->r_mem, CHIP_ICC, ~CHIP_ICC_ALL_PORTS);
	for (p = 0; p < ctlr->channels; p++) {
		if ((p & 3) == 0) {
			if (p != 0)
				ic >>= 1;
			if ((ic & IC_HC0) == 0) {
				p += 3;
				ic >>= 8;
				continue;
			}
			/* Acknowledge interrupts of this HC. */
			aic = 0;
			if (ic & (IC_DONE_IRQ << 0))
				aic |= HC_IC_DONE(0) | HC_IC_DEV(0);
			if (ic & (IC_DONE_IRQ << 2))
				aic |= HC_IC_DONE(1) | HC_IC_DEV(1);
			if (ic & (IC_DONE_IRQ << 4))
				aic |= HC_IC_DONE(2) | HC_IC_DEV(2);
			if (ic & (IC_DONE_IRQ << 6))
				aic |= HC_IC_DONE(3) | HC_IC_DEV(3);
			if (ic & IC_HC0_COAL_DONE)
				aic |= HC_IC_COAL;
			ATA_OUTL(ctlr->r_mem, HC_BASE(p == 4) + HC_IC, ~aic);
		}
		/* Call per-port interrupt handler. */
		arg.cause = ic & (IC_ERR_IRQ|IC_DONE_IRQ);
		if ((arg.cause != 0) &&
		    (function = ctlr->interrupt[p].function)) {
			arg.arg = ctlr->interrupt[p].argument;
			function(&arg);
		}
		ic >>= 2;
	}
	if (ctlr->msi) {
		/* Unmasking MSI triggers next interrupt, if needed. */
		mtx_lock(&ctlr->mtx);
		ctlr->msia = 0;	/* Allow MIM update. */
		ATA_OUTL(ctlr->r_mem, CHIP_MIM, ctlr->mim);
		mtx_unlock(&ctlr->mtx);
	}
}

static struct resource *
mvs_alloc_resource(device_t dev, device_t child, int type, int *rid,
		       rman_res_t start, rman_res_t end, rman_res_t count,
		       u_int flags)
{
	struct mvs_controller *ctlr = device_get_softc(dev);
	int unit = ((struct mvs_channel *)device_get_softc(child))->unit;
	struct resource *res = NULL;
	int offset = HC_BASE(unit >> 2) + PORT_BASE(unit & 0x03);
	rman_res_t st;

	switch (type) {
	case SYS_RES_MEMORY:
		st = rman_get_start(ctlr->r_mem);
		res = rman_reserve_resource(&ctlr->sc_iomem, st + offset,
		    st + offset + PORT_SIZE - 1, PORT_SIZE, RF_ACTIVE, child);
		if (res) {
			bus_space_handle_t bsh;
			bus_space_tag_t bst;
			bsh = rman_get_bushandle(ctlr->r_mem);
			bst = rman_get_bustag(ctlr->r_mem);
			bus_space_subregion(bst, bsh, offset, PORT_SIZE, &bsh);
			rman_set_bushandle(res, bsh);
			rman_set_bustag(res, bst);
		}
		break;
	case SYS_RES_IRQ:
		if (*rid == ATA_IRQ_RID)
			res = ctlr->irq.r_irq;
		break;
	}
	return (res);
}

static int
mvs_release_resource(device_t dev, device_t child, int type, int rid,
			 struct resource *r)
{

	switch (type) {
	case SYS_RES_MEMORY:
		rman_release_resource(r);
		return (0);
	case SYS_RES_IRQ:
		if (rid != ATA_IRQ_RID)
			return ENOENT;
		return (0);
	}
	return (EINVAL);
}

static int
mvs_setup_intr(device_t dev, device_t child, struct resource *irq, 
		   int flags, driver_filter_t *filter, driver_intr_t *function, 
		   void *argument, void **cookiep)
{
	struct mvs_controller *ctlr = device_get_softc(dev);
	int unit = (intptr_t)device_get_ivars(child);

	if (filter != NULL) {
		printf("mvs.c: we cannot use a filter here\n");
		return (EINVAL);
	}
	ctlr->interrupt[unit].function = function;
	ctlr->interrupt[unit].argument = argument;
	return (0);
}

static int
mvs_teardown_intr(device_t dev, device_t child, struct resource *irq,
		      void *cookie)
{
	struct mvs_controller *ctlr = device_get_softc(dev);
	int unit = (intptr_t)device_get_ivars(child);

	ctlr->interrupt[unit].function = NULL;
	ctlr->interrupt[unit].argument = NULL;
	return (0);
}

static int
mvs_print_child(device_t dev, device_t child)
{
	int retval;

	retval = bus_print_child_header(dev, child);
	retval += printf(" at channel %d",
	    (int)(intptr_t)device_get_ivars(child));
	retval += bus_print_child_footer(dev, child);

	return (retval);
}

static int
mvs_child_location_str(device_t dev, device_t child, char *buf,
    size_t buflen)
{

	snprintf(buf, buflen, "channel=%d",
	    (int)(intptr_t)device_get_ivars(child));
	return (0);
}

static bus_dma_tag_t
mvs_get_dma_tag(device_t bus, device_t child)
{

	return (bus_get_dma_tag(bus));
}

static device_method_t mvs_methods[] = {
	DEVMETHOD(device_probe,     mvs_probe),
	DEVMETHOD(device_attach,    mvs_attach),
	DEVMETHOD(device_detach,    mvs_detach),
	DEVMETHOD(device_suspend,   mvs_suspend),
	DEVMETHOD(device_resume,    mvs_resume),
	DEVMETHOD(bus_print_child,  mvs_print_child),
	DEVMETHOD(bus_alloc_resource,       mvs_alloc_resource),
	DEVMETHOD(bus_release_resource,     mvs_release_resource),
	DEVMETHOD(bus_setup_intr,   mvs_setup_intr),
	DEVMETHOD(bus_teardown_intr,mvs_teardown_intr),
	DEVMETHOD(bus_child_location_str, mvs_child_location_str),
	DEVMETHOD(bus_get_dma_tag,  mvs_get_dma_tag),
	DEVMETHOD(mvs_edma,         mvs_edma),
	{ 0, 0 }
};
static driver_t mvs_driver = {
        "mvs",
        mvs_methods,
        sizeof(struct mvs_controller)
};
DRIVER_MODULE(mvs, pci, mvs_driver, mvs_devclass, 0, 0);
MODULE_PNP_INFO("W32:vendor/device", pci, mvs, mvs_ids,
    nitems(mvs_ids) - 1);
MODULE_VERSION(mvs, 1);
MODULE_DEPEND(mvs, cam, 1, 1, 1);

