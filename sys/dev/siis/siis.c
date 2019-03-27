/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009 Alexander Motin <mav@FreeBSD.org>
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
#include <sys/endian.h>
#include <sys/malloc.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/sema.h>
#include <sys/taskqueue.h>
#include <vm/uma.h>
#include <machine/stdarg.h>
#include <machine/resource.h>
#include <machine/bus.h>
#include <sys/rman.h>
#include <dev/led/led.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>
#include "siis.h"

#include <cam/cam.h>
#include <cam/cam_ccb.h>
#include <cam/cam_sim.h>
#include <cam/cam_xpt_sim.h>
#include <cam/cam_debug.h>

/* local prototypes */
static int siis_setup_interrupt(device_t dev);
static void siis_intr(void *data);
static int siis_suspend(device_t dev);
static int siis_resume(device_t dev);
static int siis_ch_init(device_t dev);
static int siis_ch_deinit(device_t dev);
static int siis_ch_suspend(device_t dev);
static int siis_ch_resume(device_t dev);
static void siis_ch_intr_locked(void *data);
static void siis_ch_intr(void *data);
static void siis_ch_led(void *priv, int onoff);
static void siis_begin_transaction(device_t dev, union ccb *ccb);
static void siis_dmasetprd(void *arg, bus_dma_segment_t *segs, int nsegs, int error);
static void siis_execute_transaction(struct siis_slot *slot);
static void siis_timeout(struct siis_slot *slot);
static void siis_end_transaction(struct siis_slot *slot, enum siis_err_type et);
static int siis_setup_fis(device_t dev, struct siis_cmd *ctp, union ccb *ccb, int tag);
static void siis_dmainit(device_t dev);
static void siis_dmasetupc_cb(void *xsc, bus_dma_segment_t *segs, int nsegs, int error);
static void siis_dmafini(device_t dev);
static void siis_slotsalloc(device_t dev);
static void siis_slotsfree(device_t dev);
static void siis_reset(device_t dev);
static void siis_portinit(device_t dev);
static int siis_wait_ready(device_t dev, int t);

static int siis_sata_connect(struct siis_channel *ch);

static void siis_issue_recovery(device_t dev);
static void siis_process_read_log(device_t dev, union ccb *ccb);
static void siis_process_request_sense(device_t dev, union ccb *ccb);

static void siisaction(struct cam_sim *sim, union ccb *ccb);
static void siispoll(struct cam_sim *sim);

static MALLOC_DEFINE(M_SIIS, "SIIS driver", "SIIS driver data buffers");

static struct {
	uint32_t	id;
	const char	*name;
	int		ports;
	int		quirks;
#define SIIS_Q_SNTF	1
#define SIIS_Q_NOMSI	2
} siis_ids[] = {
	{0x31241095,	"SiI3124",	4,	0},
	{0x31248086,	"SiI3124",	4,	0},
	{0x31321095,	"SiI3132",	2,	SIIS_Q_SNTF|SIIS_Q_NOMSI},
	{0x02421095,	"SiI3132",	2,	SIIS_Q_SNTF|SIIS_Q_NOMSI},
	{0x02441095,	"SiI3132",	2,	SIIS_Q_SNTF|SIIS_Q_NOMSI},
	{0x31311095,	"SiI3131",	1,	SIIS_Q_SNTF|SIIS_Q_NOMSI},
	{0x35311095,	"SiI3531",	1,	SIIS_Q_SNTF|SIIS_Q_NOMSI},
	{0,		NULL,		0,	0}
};

#define recovery_type		spriv_field0
#define RECOVERY_NONE		0
#define RECOVERY_READ_LOG	1
#define RECOVERY_REQUEST_SENSE	2
#define recovery_slot		spriv_field1

static int
siis_probe(device_t dev)
{
	char buf[64];
	int i;
	uint32_t devid = pci_get_devid(dev);

	for (i = 0; siis_ids[i].id != 0; i++) {
		if (siis_ids[i].id == devid) {
			snprintf(buf, sizeof(buf), "%s SATA controller",
			    siis_ids[i].name);
			device_set_desc_copy(dev, buf);
			return (BUS_PROBE_DEFAULT);
		}
	}
	return (ENXIO);
}

static int
siis_attach(device_t dev)
{
	struct siis_controller *ctlr = device_get_softc(dev);
	uint32_t devid = pci_get_devid(dev);
	device_t child;
	int	error, i, unit;

	ctlr->dev = dev;
	for (i = 0; siis_ids[i].id != 0; i++) {
		if (siis_ids[i].id == devid)
			break;
	}
	ctlr->quirks = siis_ids[i].quirks;
	/* Global memory */
	ctlr->r_grid = PCIR_BAR(0);
	if (!(ctlr->r_gmem = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &ctlr->r_grid, RF_ACTIVE)))
		return (ENXIO);
	ctlr->gctl = ATA_INL(ctlr->r_gmem, SIIS_GCTL);
	/* Channels memory */
	ctlr->r_rid = PCIR_BAR(2);
	if (!(ctlr->r_mem = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &ctlr->r_rid, RF_ACTIVE)))
		return (ENXIO);
	/* Setup our own memory management for channels. */
	ctlr->sc_iomem.rm_start = rman_get_start(ctlr->r_mem);
	ctlr->sc_iomem.rm_end = rman_get_end(ctlr->r_mem);
	ctlr->sc_iomem.rm_type = RMAN_ARRAY;
	ctlr->sc_iomem.rm_descr = "I/O memory addresses";
	if ((error = rman_init(&ctlr->sc_iomem)) != 0) {
		bus_release_resource(dev, SYS_RES_MEMORY, ctlr->r_rid, ctlr->r_mem);
		bus_release_resource(dev, SYS_RES_MEMORY, ctlr->r_grid, ctlr->r_gmem);
		return (error);
	}
	if ((error = rman_manage_region(&ctlr->sc_iomem,
	    rman_get_start(ctlr->r_mem), rman_get_end(ctlr->r_mem))) != 0) {
		bus_release_resource(dev, SYS_RES_MEMORY, ctlr->r_rid, ctlr->r_mem);
		bus_release_resource(dev, SYS_RES_MEMORY, ctlr->r_grid, ctlr->r_gmem);
		rman_fini(&ctlr->sc_iomem);
		return (error);
	}
	pci_enable_busmaster(dev);
	/* Reset controller */
	siis_resume(dev);
	/* Number of HW channels */
	ctlr->channels = siis_ids[i].ports;
	/* Setup interrupts. */
	if (siis_setup_interrupt(dev)) {
		bus_release_resource(dev, SYS_RES_MEMORY, ctlr->r_rid, ctlr->r_mem);
		bus_release_resource(dev, SYS_RES_MEMORY, ctlr->r_grid, ctlr->r_gmem);
		rman_fini(&ctlr->sc_iomem);
		return ENXIO;
	}
	/* Attach all channels on this controller */
	for (unit = 0; unit < ctlr->channels; unit++) {
		child = device_add_child(dev, "siisch", -1);
		if (child == NULL)
			device_printf(dev, "failed to add channel device\n");
		else
			device_set_ivars(child, (void *)(intptr_t)unit);
	}
	bus_generic_attach(dev);
	return 0;
}

static int
siis_detach(device_t dev)
{
	struct siis_controller *ctlr = device_get_softc(dev);

	/* Detach & delete all children */
	device_delete_children(dev);

	/* Free interrupts. */
	if (ctlr->irq.r_irq) {
		bus_teardown_intr(dev, ctlr->irq.r_irq,
		    ctlr->irq.handle);
		bus_release_resource(dev, SYS_RES_IRQ,
		    ctlr->irq.r_irq_rid, ctlr->irq.r_irq);
	}
	pci_release_msi(dev);
	/* Free memory. */
	rman_fini(&ctlr->sc_iomem);
	bus_release_resource(dev, SYS_RES_MEMORY, ctlr->r_rid, ctlr->r_mem);
	bus_release_resource(dev, SYS_RES_MEMORY, ctlr->r_grid, ctlr->r_gmem);
	return (0);
}

static int
siis_suspend(device_t dev)
{
	struct siis_controller *ctlr = device_get_softc(dev);

	bus_generic_suspend(dev);
	/* Put controller into reset state. */
	ctlr->gctl |= SIIS_GCTL_GRESET;
	ATA_OUTL(ctlr->r_gmem, SIIS_GCTL, ctlr->gctl);
	return 0;
}

static int
siis_resume(device_t dev)
{
	struct siis_controller *ctlr = device_get_softc(dev);

	/* Set PCIe max read request size to at least 1024 bytes */
	if (pci_get_max_read_req(dev) < 1024)
		pci_set_max_read_req(dev, 1024);
	/* Put controller into reset state. */
	ctlr->gctl |= SIIS_GCTL_GRESET;
	ATA_OUTL(ctlr->r_gmem, SIIS_GCTL, ctlr->gctl);
	DELAY(10000);
	/* Get controller out of reset state and enable port interrupts. */
	ctlr->gctl &= ~(SIIS_GCTL_GRESET | SIIS_GCTL_I2C_IE);
	ctlr->gctl |= 0x0000000f;
	ATA_OUTL(ctlr->r_gmem, SIIS_GCTL, ctlr->gctl);
	return (bus_generic_resume(dev));
}

static int
siis_setup_interrupt(device_t dev)
{
	struct siis_controller *ctlr = device_get_softc(dev);
	int msi = ctlr->quirks & SIIS_Q_NOMSI ? 0 : 1;

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
	/* Allocate all IRQs. */
	ctlr->irq.r_irq_rid = msi ? 1 : 0;
	if (!(ctlr->irq.r_irq = bus_alloc_resource_any(dev, SYS_RES_IRQ,
	    &ctlr->irq.r_irq_rid, RF_SHAREABLE | RF_ACTIVE))) {
		device_printf(dev, "unable to map interrupt\n");
		return ENXIO;
	}
	if ((bus_setup_intr(dev, ctlr->irq.r_irq, ATA_INTR_FLAGS, NULL,
	    siis_intr, ctlr, &ctlr->irq.handle))) {
		/* SOS XXX release r_irq */
		device_printf(dev, "unable to setup interrupt\n");
		return ENXIO;
	}
	return (0);
}

/*
 * Common case interrupt handler.
 */
static void
siis_intr(void *data)
{
	struct siis_controller *ctlr = (struct siis_controller *)data;
	u_int32_t is;
	void *arg;
	int unit;

	is = ATA_INL(ctlr->r_gmem, SIIS_IS);
	for (unit = 0; unit < ctlr->channels; unit++) {
		if ((is & SIIS_IS_PORT(unit)) != 0 &&
		    (arg = ctlr->interrupt[unit].argument)) {
			ctlr->interrupt[unit].function(arg);
		}
	}
	/* Acknowledge interrupt, if MSI enabled. */
	if (ctlr->irq.r_irq_rid) {
		ATA_OUTL(ctlr->r_gmem, SIIS_GCTL,
		    ctlr->gctl | SIIS_GCTL_MSIACK);
	}
}

static struct resource *
siis_alloc_resource(device_t dev, device_t child, int type, int *rid,
		    rman_res_t start, rman_res_t end, rman_res_t count, u_int flags)
{
	struct siis_controller *ctlr = device_get_softc(dev);
	int unit = ((struct siis_channel *)device_get_softc(child))->unit;
	struct resource *res = NULL;
	int offset = unit << 13;
	rman_res_t st;

	switch (type) {
	case SYS_RES_MEMORY:
		st = rman_get_start(ctlr->r_mem);
		res = rman_reserve_resource(&ctlr->sc_iomem, st + offset,
		    st + offset + 0x2000, 0x2000, RF_ACTIVE, child);
		if (res) {
			bus_space_handle_t bsh;
			bus_space_tag_t bst;
			bsh = rman_get_bushandle(ctlr->r_mem);
			bst = rman_get_bustag(ctlr->r_mem);
			bus_space_subregion(bst, bsh, offset, 0x2000, &bsh);
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
siis_release_resource(device_t dev, device_t child, int type, int rid,
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
siis_setup_intr(device_t dev, device_t child, struct resource *irq, 
		   int flags, driver_filter_t *filter, driver_intr_t *function, 
		   void *argument, void **cookiep)
{
	struct siis_controller *ctlr = device_get_softc(dev);
	int unit = (intptr_t)device_get_ivars(child);

	if (filter != NULL) {
		printf("siis.c: we cannot use a filter here\n");
		return (EINVAL);
	}
	ctlr->interrupt[unit].function = function;
	ctlr->interrupt[unit].argument = argument;
	return (0);
}

static int
siis_teardown_intr(device_t dev, device_t child, struct resource *irq,
		      void *cookie)
{
	struct siis_controller *ctlr = device_get_softc(dev);
	int unit = (intptr_t)device_get_ivars(child);

	ctlr->interrupt[unit].function = NULL;
	ctlr->interrupt[unit].argument = NULL;
	return (0);
}

static int
siis_print_child(device_t dev, device_t child)
{
	int retval;

	retval = bus_print_child_header(dev, child);
	retval += printf(" at channel %d",
	    (int)(intptr_t)device_get_ivars(child));
	retval += bus_print_child_footer(dev, child);

	return (retval);
}

static int
siis_child_location_str(device_t dev, device_t child, char *buf,
    size_t buflen)
{

	snprintf(buf, buflen, "channel=%d",
	    (int)(intptr_t)device_get_ivars(child));
	return (0);
}

static bus_dma_tag_t
siis_get_dma_tag(device_t bus, device_t child)
{

	return (bus_get_dma_tag(bus));
}

devclass_t siis_devclass;
static device_method_t siis_methods[] = {
	DEVMETHOD(device_probe,     siis_probe),
	DEVMETHOD(device_attach,    siis_attach),
	DEVMETHOD(device_detach,    siis_detach),
	DEVMETHOD(device_suspend,   siis_suspend),
	DEVMETHOD(device_resume,    siis_resume),
	DEVMETHOD(bus_print_child,  siis_print_child),
	DEVMETHOD(bus_alloc_resource,       siis_alloc_resource),
	DEVMETHOD(bus_release_resource,     siis_release_resource),
	DEVMETHOD(bus_setup_intr,   siis_setup_intr),
	DEVMETHOD(bus_teardown_intr,siis_teardown_intr),
	DEVMETHOD(bus_child_location_str, siis_child_location_str),
	DEVMETHOD(bus_get_dma_tag,  siis_get_dma_tag),
	{ 0, 0 }
};
static driver_t siis_driver = {
        "siis",
        siis_methods,
        sizeof(struct siis_controller)
};
DRIVER_MODULE(siis, pci, siis_driver, siis_devclass, 0, 0);
MODULE_VERSION(siis, 1);
MODULE_DEPEND(siis, cam, 1, 1, 1);

static int
siis_ch_probe(device_t dev)
{

	device_set_desc_copy(dev, "SIIS channel");
	return (BUS_PROBE_DEFAULT);
}

static int
siis_ch_attach(device_t dev)
{
	struct siis_controller *ctlr = device_get_softc(device_get_parent(dev));
	struct siis_channel *ch = device_get_softc(dev);
	struct cam_devq *devq;
	int rid, error, i, sata_rev = 0;

	ch->dev = dev;
	ch->unit = (intptr_t)device_get_ivars(dev);
	ch->quirks = ctlr->quirks;
	ch->pm_level = 0;
	resource_int_value(device_get_name(dev),
	    device_get_unit(dev), "pm_level", &ch->pm_level);
	resource_int_value(device_get_name(dev),
	    device_get_unit(dev), "sata_rev", &sata_rev);
	for (i = 0; i < 16; i++) {
		ch->user[i].revision = sata_rev;
		ch->user[i].mode = 0;
		ch->user[i].bytecount = 8192;
		ch->user[i].tags = SIIS_MAX_SLOTS;
		ch->curr[i] = ch->user[i];
		if (ch->pm_level)
			ch->user[i].caps = CTS_SATA_CAPS_H_PMREQ;
		ch->user[i].caps |= CTS_SATA_CAPS_H_AN;
	}
	mtx_init(&ch->mtx, "SIIS channel lock", NULL, MTX_DEF);
	rid = ch->unit;
	if (!(ch->r_mem = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &rid, RF_ACTIVE)))
		return (ENXIO);
	siis_dmainit(dev);
	siis_slotsalloc(dev);
	siis_ch_init(dev);
	mtx_lock(&ch->mtx);
	rid = ATA_IRQ_RID;
	if (!(ch->r_irq = bus_alloc_resource_any(dev, SYS_RES_IRQ,
	    &rid, RF_SHAREABLE | RF_ACTIVE))) {
		device_printf(dev, "Unable to map interrupt\n");
		error = ENXIO;
		goto err0;
	}
	if ((bus_setup_intr(dev, ch->r_irq, ATA_INTR_FLAGS, NULL,
	    siis_ch_intr_locked, dev, &ch->ih))) {
		device_printf(dev, "Unable to setup interrupt\n");
		error = ENXIO;
		goto err1;
	}
	/* Create the device queue for our SIM. */
	devq = cam_simq_alloc(SIIS_MAX_SLOTS);
	if (devq == NULL) {
		device_printf(dev, "Unable to allocate simq\n");
		error = ENOMEM;
		goto err1;
	}
	/* Construct SIM entry */
	ch->sim = cam_sim_alloc(siisaction, siispoll, "siisch", ch,
	    device_get_unit(dev), &ch->mtx, 2, SIIS_MAX_SLOTS, devq);
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
	mtx_unlock(&ch->mtx);
	ch->led = led_create(siis_ch_led, dev, device_get_nameunit(dev));
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
siis_ch_detach(device_t dev)
{
	struct siis_channel *ch = device_get_softc(dev);

	led_destroy(ch->led);
	mtx_lock(&ch->mtx);
	xpt_async(AC_LOST_DEVICE, ch->path, NULL);
	xpt_free_path(ch->path);
	xpt_bus_deregister(cam_sim_path(ch->sim));
	cam_sim_free(ch->sim, /*free_devq*/TRUE);
	mtx_unlock(&ch->mtx);

	bus_teardown_intr(dev, ch->r_irq, ch->ih);
	bus_release_resource(dev, SYS_RES_IRQ, ATA_IRQ_RID, ch->r_irq);

	siis_ch_deinit(dev);
	siis_slotsfree(dev);
	siis_dmafini(dev);

	bus_release_resource(dev, SYS_RES_MEMORY, ch->unit, ch->r_mem);
	mtx_destroy(&ch->mtx);
	return (0);
}

static int
siis_ch_init(device_t dev)
{
	struct siis_channel *ch = device_get_softc(dev);

	/* Get port out of reset state. */
	ATA_OUTL(ch->r_mem, SIIS_P_CTLCLR, SIIS_P_CTL_PORT_RESET);
	ATA_OUTL(ch->r_mem, SIIS_P_CTLCLR, SIIS_P_CTL_32BIT);
	if (ch->pm_present)
		ATA_OUTL(ch->r_mem, SIIS_P_CTLSET, SIIS_P_CTL_PME);
	else
		ATA_OUTL(ch->r_mem, SIIS_P_CTLCLR, SIIS_P_CTL_PME);
	/* Enable port interrupts */
	ATA_OUTL(ch->r_mem, SIIS_P_IESET, SIIS_P_IX_ENABLED);
	return (0);
}

static int
siis_ch_deinit(device_t dev)
{
	struct siis_channel *ch = device_get_softc(dev);

	/* Put port into reset state. */
	ATA_OUTL(ch->r_mem, SIIS_P_CTLSET, SIIS_P_CTL_PORT_RESET);
	return (0);
}

static int
siis_ch_suspend(device_t dev)
{
	struct siis_channel *ch = device_get_softc(dev);

	mtx_lock(&ch->mtx);
	xpt_freeze_simq(ch->sim, 1);
	while (ch->oslots)
		msleep(ch, &ch->mtx, PRIBIO, "siissusp", hz/100);
	siis_ch_deinit(dev);
	mtx_unlock(&ch->mtx);
	return (0);
}

static int
siis_ch_resume(device_t dev)
{
	struct siis_channel *ch = device_get_softc(dev);

	mtx_lock(&ch->mtx);
	siis_ch_init(dev);
	siis_reset(dev);
	xpt_release_simq(ch->sim, TRUE);
	mtx_unlock(&ch->mtx);
	return (0);
}

devclass_t siisch_devclass;
static device_method_t siisch_methods[] = {
	DEVMETHOD(device_probe,     siis_ch_probe),
	DEVMETHOD(device_attach,    siis_ch_attach),
	DEVMETHOD(device_detach,    siis_ch_detach),
	DEVMETHOD(device_suspend,   siis_ch_suspend),
	DEVMETHOD(device_resume,    siis_ch_resume),
	{ 0, 0 }
};
static driver_t siisch_driver = {
        "siisch",
        siisch_methods,
        sizeof(struct siis_channel)
};
DRIVER_MODULE(siisch, siis, siisch_driver, siis_devclass, 0, 0);

static void
siis_ch_led(void *priv, int onoff)
{
	device_t dev;
	struct siis_channel *ch;

	dev = (device_t)priv;
	ch = device_get_softc(dev);

	if (onoff == 0)
		ATA_OUTL(ch->r_mem, SIIS_P_CTLCLR, SIIS_P_CTL_LED_ON);
	else
		ATA_OUTL(ch->r_mem, SIIS_P_CTLSET, SIIS_P_CTL_LED_ON);
}

struct siis_dc_cb_args {
	bus_addr_t maddr;
	int error;
};

static void
siis_dmainit(device_t dev)
{
	struct siis_channel *ch = device_get_softc(dev);
	struct siis_dc_cb_args dcba;

	/* Command area. */
	if (bus_dma_tag_create(bus_get_dma_tag(dev), 1024, 0,
	    BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR,
	    NULL, NULL, SIIS_WORK_SIZE, 1, SIIS_WORK_SIZE,
	    0, NULL, NULL, &ch->dma.work_tag))
		goto error;
	if (bus_dmamem_alloc(ch->dma.work_tag, (void **)&ch->dma.work, 0,
	    &ch->dma.work_map))
		goto error;
	if (bus_dmamap_load(ch->dma.work_tag, ch->dma.work_map, ch->dma.work,
	    SIIS_WORK_SIZE, siis_dmasetupc_cb, &dcba, 0) || dcba.error) {
		bus_dmamem_free(ch->dma.work_tag, ch->dma.work, ch->dma.work_map);
		goto error;
	}
	ch->dma.work_bus = dcba.maddr;
	/* Data area. */
	if (bus_dma_tag_create(bus_get_dma_tag(dev), 1, 0,
	    BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR,
	    NULL, NULL,
	    SIIS_SG_ENTRIES * PAGE_SIZE * SIIS_MAX_SLOTS,
	    SIIS_SG_ENTRIES, 0xFFFFFFFF,
	    0, busdma_lock_mutex, &ch->mtx, &ch->dma.data_tag)) {
		goto error;
	}
	return;

error:
	device_printf(dev, "WARNING - DMA initialization failed\n");
	siis_dmafini(dev);
}

static void
siis_dmasetupc_cb(void *xsc, bus_dma_segment_t *segs, int nsegs, int error)
{
	struct siis_dc_cb_args *dcba = (struct siis_dc_cb_args *)xsc;

	if (!(dcba->error = error))
		dcba->maddr = segs[0].ds_addr;
}

static void
siis_dmafini(device_t dev)
{
	struct siis_channel *ch = device_get_softc(dev);

	if (ch->dma.data_tag) {
		bus_dma_tag_destroy(ch->dma.data_tag);
		ch->dma.data_tag = NULL;
	}
	if (ch->dma.work_bus) {
		bus_dmamap_unload(ch->dma.work_tag, ch->dma.work_map);
		bus_dmamem_free(ch->dma.work_tag, ch->dma.work, ch->dma.work_map);
		ch->dma.work_bus = 0;
		ch->dma.work_map = NULL;
		ch->dma.work = NULL;
	}
	if (ch->dma.work_tag) {
		bus_dma_tag_destroy(ch->dma.work_tag);
		ch->dma.work_tag = NULL;
	}
}

static void
siis_slotsalloc(device_t dev)
{
	struct siis_channel *ch = device_get_softc(dev);
	int i;

	/* Alloc and setup command/dma slots */
	bzero(ch->slot, sizeof(ch->slot));
	for (i = 0; i < SIIS_MAX_SLOTS; i++) {
		struct siis_slot *slot = &ch->slot[i];

		slot->dev = dev;
		slot->slot = i;
		slot->state = SIIS_SLOT_EMPTY;
		slot->ccb = NULL;
		callout_init_mtx(&slot->timeout, &ch->mtx, 0);

		if (bus_dmamap_create(ch->dma.data_tag, 0, &slot->dma.data_map))
			device_printf(ch->dev, "FAILURE - create data_map\n");
	}
}

static void
siis_slotsfree(device_t dev)
{
	struct siis_channel *ch = device_get_softc(dev);
	int i;

	/* Free all dma slots */
	for (i = 0; i < SIIS_MAX_SLOTS; i++) {
		struct siis_slot *slot = &ch->slot[i];

		callout_drain(&slot->timeout);
		if (slot->dma.data_map) {
			bus_dmamap_destroy(ch->dma.data_tag, slot->dma.data_map);
			slot->dma.data_map = NULL;
		}
	}
}

static void
siis_notify_events(device_t dev)
{
	struct siis_channel *ch = device_get_softc(dev);
	struct cam_path *dpath;
	u_int32_t status;
	int i;

	if (ch->quirks & SIIS_Q_SNTF) {
		status = ATA_INL(ch->r_mem, SIIS_P_SNTF);
		ATA_OUTL(ch->r_mem, SIIS_P_SNTF, status);
	} else {
		/*
		 * Without SNTF we have no idea which device sent notification.
		 * If PMP is connected, assume it, else - device.
		 */
		status = (ch->pm_present) ? 0x8000 : 0x0001;
	}
	if (bootverbose)
		device_printf(dev, "SNTF 0x%04x\n", status);
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
siis_phy_check_events(device_t dev)
{
	struct siis_channel *ch = device_get_softc(dev);

	/* If we have a connection event, deal with it */
	if (ch->pm_level == 0) {
		u_int32_t status = ATA_INL(ch->r_mem, SIIS_P_SSTS);
		union ccb *ccb;

		if (bootverbose) {
			if (((status & ATA_SS_DET_MASK) == ATA_SS_DET_PHY_ONLINE) &&
			    ((status & ATA_SS_SPD_MASK) != ATA_SS_SPD_NO_SPEED) &&
			    ((status & ATA_SS_IPM_MASK) == ATA_SS_IPM_ACTIVE)) {
				device_printf(dev, "CONNECT requested\n");
			} else
				device_printf(dev, "DISCONNECT requested\n");
		}
		siis_reset(dev);
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
siis_ch_intr_locked(void *data)
{
	device_t dev = (device_t)data;
	struct siis_channel *ch = device_get_softc(dev);

	mtx_lock(&ch->mtx);
	siis_ch_intr(data);
	mtx_unlock(&ch->mtx);
}

static void
siis_ch_intr(void *data)
{
	device_t dev = (device_t)data;
	struct siis_channel *ch = device_get_softc(dev);
	uint32_t istatus, sstatus, ctx, estatus, ok, err = 0;
	enum siis_err_type et;
	int i, ccs, port, tslots;

	mtx_assert(&ch->mtx, MA_OWNED);
	/* Read command statuses. */
	sstatus = ATA_INL(ch->r_mem, SIIS_P_SS);
	ok = ch->rslots & ~sstatus;
	/* Complete all successful commands. */
	for (i = 0; i < SIIS_MAX_SLOTS; i++) {
		if ((ok >> i) & 1)
			siis_end_transaction(&ch->slot[i], SIIS_ERR_NONE);
	}
	/* Do we have any other events? */
	if ((sstatus & SIIS_P_SS_ATTN) == 0)
		return;
	/* Read and clear interrupt statuses. */
	istatus = ATA_INL(ch->r_mem, SIIS_P_IS) &
	    (0xFFFF & ~SIIS_P_IX_COMMCOMP);
	ATA_OUTL(ch->r_mem, SIIS_P_IS, istatus);
	/* Process PHY events */
	if (istatus & SIIS_P_IX_PHYRDYCHG)
		siis_phy_check_events(dev);
	/* Process NOTIFY events */
	if (istatus & SIIS_P_IX_SDBN)
		siis_notify_events(dev);
	/* Process command errors */
	if (istatus & SIIS_P_IX_COMMERR) {
		estatus = ATA_INL(ch->r_mem, SIIS_P_CMDERR);
		ctx = ATA_INL(ch->r_mem, SIIS_P_CTX);
		ccs = (ctx & SIIS_P_CTX_SLOT) >> SIIS_P_CTX_SLOT_SHIFT;
		port = (ctx & SIIS_P_CTX_PMP) >> SIIS_P_CTX_PMP_SHIFT;
		err = ch->rslots & sstatus;
//device_printf(dev, "%s ERROR ss %08x is %08x rs %08x es %d act %d port %d serr %08x\n",
//    __func__, sstatus, istatus, ch->rslots, estatus, ccs, port,
//    ATA_INL(ch->r_mem, SIIS_P_SERR));

		if (!ch->recoverycmd && !ch->recovery) {
			xpt_freeze_simq(ch->sim, ch->numrslots);
			ch->recovery = 1;
		}
		if (ch->frozen) {
			union ccb *fccb = ch->frozen;
			ch->frozen = NULL;
			fccb->ccb_h.status &= ~CAM_STATUS_MASK;
			fccb->ccb_h.status |= CAM_REQUEUE_REQ | CAM_RELEASE_SIMQ;
			if (!(fccb->ccb_h.status & CAM_DEV_QFRZN)) {
				xpt_freeze_devq(fccb->ccb_h.path, 1);
				fccb->ccb_h.status |= CAM_DEV_QFRZN;
			}
			xpt_done(fccb);
		}
		if (estatus == SIIS_P_CMDERR_DEV ||
		    estatus == SIIS_P_CMDERR_SDB ||
		    estatus == SIIS_P_CMDERR_DATAFIS) {
			tslots = ch->numtslots[port];
			for (i = 0; i < SIIS_MAX_SLOTS; i++) {
				/* XXX: requests in loading state. */
				if (((ch->rslots >> i) & 1) == 0)
					continue;
				if (ch->slot[i].ccb->ccb_h.target_id != port)
					continue;
				if (tslots == 0) {
					/* Untagged operation. */
					if (i == ccs)
						et = SIIS_ERR_TFE;
					else
						et = SIIS_ERR_INNOCENT;
				} else {
					/* Tagged operation. */
					et = SIIS_ERR_NCQ;
				}
				siis_end_transaction(&ch->slot[i], et);
			}
			/*
			 * We can't reinit port if there are some other
			 * commands active, use resume to complete them.
			 */
			if (ch->rslots != 0 && !ch->recoverycmd)
				ATA_OUTL(ch->r_mem, SIIS_P_CTLSET, SIIS_P_CTL_RESUME);
		} else {
			if (estatus == SIIS_P_CMDERR_SENDFIS ||
			    estatus == SIIS_P_CMDERR_INCSTATE ||
			    estatus == SIIS_P_CMDERR_PPE ||
			    estatus == SIIS_P_CMDERR_SERVICE) {
				et = SIIS_ERR_SATA;
			} else
				et = SIIS_ERR_INVALID;
			for (i = 0; i < SIIS_MAX_SLOTS; i++) {
				/* XXX: requests in loading state. */
				if (((ch->rslots >> i) & 1) == 0)
					continue;
				siis_end_transaction(&ch->slot[i], et);
			}
		}
	}
}

/* Must be called with channel locked. */
static int
siis_check_collision(device_t dev, union ccb *ccb)
{
	struct siis_channel *ch = device_get_softc(dev);

	mtx_assert(&ch->mtx, MA_OWNED);
	if ((ccb->ccb_h.func_code == XPT_ATA_IO) &&
	    (ccb->ataio.cmd.flags & CAM_ATAIO_FPDMA)) {
		/* Tagged command while we have no supported tag free. */
		if (((~ch->oslots) & (0x7fffffff >> (31 -
		    ch->curr[ccb->ccb_h.target_id].tags))) == 0)
			return (1);
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
siis_begin_transaction(device_t dev, union ccb *ccb)
{
	struct siis_channel *ch = device_get_softc(dev);
	struct siis_slot *slot;
	int tag, tags;

	mtx_assert(&ch->mtx, MA_OWNED);
	/* Choose empty slot. */
	tags = SIIS_MAX_SLOTS;
	if ((ccb->ccb_h.func_code == XPT_ATA_IO) &&
	    (ccb->ataio.cmd.flags & CAM_ATAIO_FPDMA))
		tags = ch->curr[ccb->ccb_h.target_id].tags;
	tag = fls((~ch->oslots) & (0x7fffffff >> (31 - tags))) - 1;
	/* Occupy chosen slot. */
	slot = &ch->slot[tag];
	slot->ccb = ccb;
	/* Update channel stats. */
	ch->oslots |= (1 << slot->slot);
	ch->numrslots++;
	if ((ccb->ccb_h.func_code == XPT_ATA_IO) &&
	    (ccb->ataio.cmd.flags & CAM_ATAIO_FPDMA)) {
		ch->numtslots[ccb->ccb_h.target_id]++;
	}
	if ((ccb->ccb_h.func_code == XPT_ATA_IO) &&
	    (ccb->ataio.cmd.flags & (CAM_ATAIO_CONTROL | CAM_ATAIO_NEEDRESULT)))
		ch->aslots |= (1 << slot->slot);
	slot->dma.nsegs = 0;
	/* If request moves data, setup and load SG list */
	if ((ccb->ccb_h.flags & CAM_DIR_MASK) != CAM_DIR_NONE) {
		slot->state = SIIS_SLOT_LOADING;
		bus_dmamap_load_ccb(ch->dma.data_tag, slot->dma.data_map,
		    ccb, siis_dmasetprd, slot, 0);
	} else
		siis_execute_transaction(slot);
}

/* Locked by busdma engine. */
static void
siis_dmasetprd(void *arg, bus_dma_segment_t *segs, int nsegs, int error)
{    
	struct siis_slot *slot = arg;
	struct siis_channel *ch = device_get_softc(slot->dev);
	struct siis_cmd *ctp;
	struct siis_dma_prd *prd;
	int i;

	mtx_assert(&ch->mtx, MA_OWNED);
	if (error) {
		device_printf(slot->dev, "DMA load error\n");
		if (!ch->recoverycmd)
			xpt_freeze_simq(ch->sim, 1);
		siis_end_transaction(slot, SIIS_ERR_INVALID);
		return;
	}
	KASSERT(nsegs <= SIIS_SG_ENTRIES, ("too many DMA segment entries\n"));
	slot->dma.nsegs = nsegs;
	if (nsegs != 0) {
		/* Get a piece of the workspace for this request */
		ctp = (struct siis_cmd *)(ch->dma.work + SIIS_CT_OFFSET +
		    (SIIS_CT_SIZE * slot->slot));
		/* Fill S/G table */
		if (slot->ccb->ccb_h.func_code == XPT_ATA_IO) 
			prd = &ctp->u.ata.prd[0];
		else
			prd = &ctp->u.atapi.prd[0];
		for (i = 0; i < nsegs; i++) {
			prd[i].dba = htole64(segs[i].ds_addr);
			prd[i].dbc = htole32(segs[i].ds_len);
			prd[i].control = 0;
		}
			prd[nsegs - 1].control = htole32(SIIS_PRD_TRM);
		bus_dmamap_sync(ch->dma.data_tag, slot->dma.data_map,
		    ((slot->ccb->ccb_h.flags & CAM_DIR_IN) ?
		    BUS_DMASYNC_PREREAD : BUS_DMASYNC_PREWRITE));
	}
	siis_execute_transaction(slot);
}

/* Must be called with channel locked. */
static void
siis_execute_transaction(struct siis_slot *slot)
{
	device_t dev = slot->dev;
	struct siis_channel *ch = device_get_softc(dev);
	struct siis_cmd *ctp;
	union ccb *ccb = slot->ccb;
	u_int64_t prb_bus;

	mtx_assert(&ch->mtx, MA_OWNED);
	/* Get a piece of the workspace for this request */
	ctp = (struct siis_cmd *)
		(ch->dma.work + SIIS_CT_OFFSET + (SIIS_CT_SIZE * slot->slot));
	ctp->control = 0;
	ctp->protocol_override = 0;
	ctp->transfer_count = 0;
	/* Special handling for Soft Reset command. */
	if (ccb->ccb_h.func_code == XPT_ATA_IO) {
		if (ccb->ataio.cmd.flags & CAM_ATAIO_CONTROL) {
			ctp->control |= htole16(SIIS_PRB_SOFT_RESET);
		} else {
			ctp->control |= htole16(SIIS_PRB_PROTOCOL_OVERRIDE);
			if (ccb->ataio.cmd.flags & CAM_ATAIO_FPDMA) {
				ctp->protocol_override |=
				    htole16(SIIS_PRB_PROTO_NCQ);
			}
			if ((ccb->ccb_h.flags & CAM_DIR_MASK) == CAM_DIR_IN) {
				ctp->protocol_override |=
				    htole16(SIIS_PRB_PROTO_READ);
			} else
			if ((ccb->ccb_h.flags & CAM_DIR_MASK) == CAM_DIR_OUT) {
				ctp->protocol_override |=
				    htole16(SIIS_PRB_PROTO_WRITE);
			}
		}
	} else if (ccb->ccb_h.func_code == XPT_SCSI_IO) {
		if ((ccb->ccb_h.flags & CAM_DIR_MASK) == CAM_DIR_IN)
			ctp->control |= htole16(SIIS_PRB_PACKET_READ);
		else
		if ((ccb->ccb_h.flags & CAM_DIR_MASK) == CAM_DIR_OUT)
			ctp->control |= htole16(SIIS_PRB_PACKET_WRITE);
	}
	/* Special handling for Soft Reset command. */
	if ((ccb->ccb_h.func_code == XPT_ATA_IO) &&
	    (ccb->ataio.cmd.flags & CAM_ATAIO_CONTROL) &&
	    (ccb->ataio.cmd.control & ATA_A_RESET)) {
		/* Kick controller into sane state */
		siis_portinit(dev);
	}
	/* Setup the FIS for this request */
	if (!siis_setup_fis(dev, ctp, ccb, slot->slot)) {
		device_printf(ch->dev, "Setting up SATA FIS failed\n");
		if (!ch->recoverycmd)
			xpt_freeze_simq(ch->sim, 1);
		siis_end_transaction(slot, SIIS_ERR_INVALID);
		return;
	}
	bus_dmamap_sync(ch->dma.work_tag, ch->dma.work_map,
	    BUS_DMASYNC_PREWRITE);
	/* Issue command to the controller. */
	slot->state = SIIS_SLOT_RUNNING;
	ch->rslots |= (1 << slot->slot);
	prb_bus = ch->dma.work_bus +
	      SIIS_CT_OFFSET + (SIIS_CT_SIZE * slot->slot);
	ATA_OUTL(ch->r_mem, SIIS_P_CACTL(slot->slot), prb_bus);
	ATA_OUTL(ch->r_mem, SIIS_P_CACTH(slot->slot), prb_bus >> 32);
	/* Start command execution timeout */
	callout_reset_sbt(&slot->timeout, SBT_1MS * ccb->ccb_h.timeout, 0,
	    (timeout_t*)siis_timeout, slot, 0);
	return;
}

/* Must be called with channel locked. */
static void
siis_process_timeout(device_t dev)
{
	struct siis_channel *ch = device_get_softc(dev);
	int i;

	mtx_assert(&ch->mtx, MA_OWNED);
	if (!ch->recoverycmd && !ch->recovery) {
		xpt_freeze_simq(ch->sim, ch->numrslots);
		ch->recovery = 1;
	}
	/* Handle the rest of commands. */
	for (i = 0; i < SIIS_MAX_SLOTS; i++) {
		/* Do we have a running request on slot? */
		if (ch->slot[i].state < SIIS_SLOT_RUNNING)
			continue;
		siis_end_transaction(&ch->slot[i], SIIS_ERR_TIMEOUT);
	}
}

/* Must be called with channel locked. */
static void
siis_rearm_timeout(device_t dev)
{
	struct siis_channel *ch = device_get_softc(dev);
	int i;

	mtx_assert(&ch->mtx, MA_OWNED);
	for (i = 0; i < SIIS_MAX_SLOTS; i++) {
		struct siis_slot *slot = &ch->slot[i];

		/* Do we have a running request on slot? */
		if (slot->state < SIIS_SLOT_RUNNING)
			continue;
		if ((ch->toslots & (1 << i)) == 0)
			continue;
		callout_reset_sbt(&slot->timeout,
		    SBT_1MS * slot->ccb->ccb_h.timeout, 0,
		    (timeout_t*)siis_timeout, slot, 0);
	}
}

/* Locked by callout mechanism. */
static void
siis_timeout(struct siis_slot *slot)
{
	device_t dev = slot->dev;
	struct siis_channel *ch = device_get_softc(dev);
	union ccb *ccb = slot->ccb;

	mtx_assert(&ch->mtx, MA_OWNED);
	/* Check for stale timeout. */
	if (slot->state < SIIS_SLOT_RUNNING)
		return;

	/* Handle soft-reset timeouts without doing hard-reset. */
	if ((ccb->ccb_h.func_code == XPT_ATA_IO) &&
	    (ccb->ataio.cmd.flags & CAM_ATAIO_CONTROL) &&
	    (ccb->ataio.cmd.control & ATA_A_RESET)) {
		xpt_freeze_simq(ch->sim, ch->numrslots);
		siis_end_transaction(slot, SIIS_ERR_TFE);
		return;
	}

	device_printf(dev, "Timeout on slot %d\n", slot->slot);
	device_printf(dev, "%s is %08x ss %08x rs %08x es %08x sts %08x serr %08x\n",
	    __func__, ATA_INL(ch->r_mem, SIIS_P_IS),
	    ATA_INL(ch->r_mem, SIIS_P_SS), ch->rslots,
	    ATA_INL(ch->r_mem, SIIS_P_CMDERR), ATA_INL(ch->r_mem, SIIS_P_STS),
	    ATA_INL(ch->r_mem, SIIS_P_SERR));

	if (ch->toslots == 0)
		xpt_freeze_simq(ch->sim, 1);
	ch->toslots |= (1 << slot->slot);
	if ((ch->rslots & ~ch->toslots) == 0)
		siis_process_timeout(dev);
	else
		device_printf(dev, " ... waiting for slots %08x\n",
		    ch->rslots & ~ch->toslots);
}

/* Must be called with channel locked. */
static void
siis_end_transaction(struct siis_slot *slot, enum siis_err_type et)
{
	device_t dev = slot->dev;
	struct siis_channel *ch = device_get_softc(dev);
	union ccb *ccb = slot->ccb;
	int lastto;

	mtx_assert(&ch->mtx, MA_OWNED);
	bus_dmamap_sync(ch->dma.work_tag, ch->dma.work_map,
	    BUS_DMASYNC_POSTWRITE);
	/* Read result registers to the result struct
	 * May be incorrect if several commands finished same time,
	 * so read only when sure or have to.
	 */
	if (ccb->ccb_h.func_code == XPT_ATA_IO) {
		struct ata_res *res = &ccb->ataio.res;
		if ((et == SIIS_ERR_TFE) ||
		    (ccb->ataio.cmd.flags & CAM_ATAIO_NEEDRESULT)) {
			int offs = SIIS_P_LRAM_SLOT(slot->slot) + 8;

			res->status = ATA_INB(ch->r_mem, offs + 2);
			res->error = ATA_INB(ch->r_mem, offs + 3);
			res->lba_low = ATA_INB(ch->r_mem, offs + 4);
			res->lba_mid = ATA_INB(ch->r_mem, offs + 5);
			res->lba_high = ATA_INB(ch->r_mem, offs + 6);
			res->device = ATA_INB(ch->r_mem, offs + 7);
			res->lba_low_exp = ATA_INB(ch->r_mem, offs + 8);
			res->lba_mid_exp = ATA_INB(ch->r_mem, offs + 9);
			res->lba_high_exp = ATA_INB(ch->r_mem, offs + 10);
			res->sector_count = ATA_INB(ch->r_mem, offs + 12);
			res->sector_count_exp = ATA_INB(ch->r_mem, offs + 13);
		} else
			bzero(res, sizeof(*res));
		if ((ccb->ccb_h.flags & CAM_DIR_MASK) == CAM_DIR_IN &&
		    ch->numrslots == 1) {
			ccb->ataio.resid = ccb->ataio.dxfer_len -
			    ATA_INL(ch->r_mem, SIIS_P_LRAM_SLOT(slot->slot) + 4);
		}
	} else {
		if ((ccb->ccb_h.flags & CAM_DIR_MASK) == CAM_DIR_IN &&
		    ch->numrslots == 1) {
			ccb->csio.resid = ccb->csio.dxfer_len -
			    ATA_INL(ch->r_mem, SIIS_P_LRAM_SLOT(slot->slot) + 4);
		}
	}
	if ((ccb->ccb_h.flags & CAM_DIR_MASK) != CAM_DIR_NONE) {
		bus_dmamap_sync(ch->dma.data_tag, slot->dma.data_map,
		    (ccb->ccb_h.flags & CAM_DIR_IN) ?
		    BUS_DMASYNC_POSTREAD : BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(ch->dma.data_tag, slot->dma.data_map);
	}
	/* Set proper result status. */
	if (et != SIIS_ERR_NONE || ch->recovery) {
		ch->eslots |= (1 << slot->slot);
		ccb->ccb_h.status |= CAM_RELEASE_SIMQ;
	}
	/* In case of error, freeze device for proper recovery. */
	if (et != SIIS_ERR_NONE && (!ch->recoverycmd) &&
	    !(ccb->ccb_h.status & CAM_DEV_QFRZN)) {
		xpt_freeze_devq(ccb->ccb_h.path, 1);
		ccb->ccb_h.status |= CAM_DEV_QFRZN;
	}
	ccb->ccb_h.status &= ~CAM_STATUS_MASK;
	switch (et) {
	case SIIS_ERR_NONE:
		ccb->ccb_h.status |= CAM_REQ_CMP;
		if (ccb->ccb_h.func_code == XPT_SCSI_IO)
			ccb->csio.scsi_status = SCSI_STATUS_OK;
		break;
	case SIIS_ERR_INVALID:
		ch->fatalerr = 1;
		ccb->ccb_h.status |= CAM_REQ_INVALID;
		break;
	case SIIS_ERR_INNOCENT:
		ccb->ccb_h.status |= CAM_REQUEUE_REQ;
		break;
	case SIIS_ERR_TFE:
	case SIIS_ERR_NCQ:
		if (ccb->ccb_h.func_code == XPT_SCSI_IO) {
			ccb->ccb_h.status |= CAM_SCSI_STATUS_ERROR;
			ccb->csio.scsi_status = SCSI_STATUS_CHECK_COND;
		} else {
			ccb->ccb_h.status |= CAM_ATA_STATUS_ERROR;
		}
		break;
	case SIIS_ERR_SATA:
		ch->fatalerr = 1;
		ccb->ccb_h.status |= CAM_UNCOR_PARITY;
		break;
	case SIIS_ERR_TIMEOUT:
		ch->fatalerr = 1;
		ccb->ccb_h.status |= CAM_CMD_TIMEOUT;
		break;
	default:
		ccb->ccb_h.status |= CAM_REQ_CMP_ERR;
	}
	/* Free slot. */
	ch->oslots &= ~(1 << slot->slot);
	ch->rslots &= ~(1 << slot->slot);
	ch->aslots &= ~(1 << slot->slot);
	slot->state = SIIS_SLOT_EMPTY;
	slot->ccb = NULL;
	/* Update channel stats. */
	ch->numrslots--;
	if ((ccb->ccb_h.func_code == XPT_ATA_IO) &&
	    (ccb->ataio.cmd.flags & CAM_ATAIO_FPDMA)) {
		ch->numtslots[ccb->ccb_h.target_id]--;
	}
	/* Cancel timeout state if request completed normally. */
	if (et != SIIS_ERR_TIMEOUT) {
		lastto = (ch->toslots == (1 << slot->slot));
		ch->toslots &= ~(1 << slot->slot);
		if (lastto)
			xpt_release_simq(ch->sim, TRUE);
	}
	/* If it was our READ LOG command - process it. */
	if (ccb->ccb_h.recovery_type == RECOVERY_READ_LOG) {
		siis_process_read_log(dev, ccb);
	/* If it was our REQUEST SENSE command - process it. */
	} else if (ccb->ccb_h.recovery_type == RECOVERY_REQUEST_SENSE) {
		siis_process_request_sense(dev, ccb);
	/* If it was NCQ or ATAPI command error, put result on hold. */
	} else if (et == SIIS_ERR_NCQ ||
	    ((ccb->ccb_h.status & CAM_STATUS_MASK) == CAM_SCSI_STATUS_ERROR &&
	     (ccb->ccb_h.flags & CAM_DIS_AUTOSENSE) == 0)) {
		ch->hold[slot->slot] = ccb;
		ch->numhslots++;
	} else
		xpt_done(ccb);
	/* If we have no other active commands, ... */
	if (ch->rslots == 0) {
		/* if there were timeouts or fatal error - reset port. */
		if (ch->toslots != 0 || ch->fatalerr) {
			siis_reset(dev);
		} else {
			/* if we have slots in error, we can reinit port. */
			if (ch->eslots != 0)
				siis_portinit(dev);
			/* if there commands on hold, we can do recovery. */
			if (!ch->recoverycmd && ch->numhslots)
				siis_issue_recovery(dev);
		}
	/* If all the reset of commands are in timeout - abort them. */
	} else if ((ch->rslots & ~ch->toslots) == 0 &&
	    et != SIIS_ERR_TIMEOUT)
		siis_rearm_timeout(dev);
	/* Unfreeze frozen command. */
	if (ch->frozen && !siis_check_collision(dev, ch->frozen)) {
		union ccb *fccb = ch->frozen;
		ch->frozen = NULL;
		siis_begin_transaction(dev, fccb);
		xpt_release_simq(ch->sim, TRUE);
	}
}

static void
siis_issue_recovery(device_t dev)
{
	struct siis_channel *ch = device_get_softc(dev);
	union ccb *ccb;
	struct ccb_ataio *ataio;
	struct ccb_scsiio *csio;
	int i;

	/* Find some held command. */
	for (i = 0; i < SIIS_MAX_SLOTS; i++) {
		if (ch->hold[i])
			break;
	}
	if (i == SIIS_MAX_SLOTS)
		return;
	ccb = xpt_alloc_ccb_nowait();
	if (ccb == NULL) {
		device_printf(dev, "Unable to allocate recovery command\n");
completeall:
		/* We can't do anything -- complete held commands. */
		for (i = 0; i < SIIS_MAX_SLOTS; i++) {
			if (ch->hold[i] == NULL)
				continue;
			ch->hold[i]->ccb_h.status &= ~CAM_STATUS_MASK;
			ch->hold[i]->ccb_h.status |= CAM_RESRC_UNAVAIL;
			xpt_done(ch->hold[i]);
			ch->hold[i] = NULL;
			ch->numhslots--;
		}
		siis_reset(dev);
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
		ataio->data_ptr = malloc(512, M_SIIS, M_NOWAIT);
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
	ch->recoverycmd = 1;
	siis_begin_transaction(dev, ccb);
}

static void
siis_process_read_log(device_t dev, union ccb *ccb)
{
	struct siis_channel *ch = device_get_softc(dev);
	uint8_t *data;
	struct ata_res *res;
	int i;

	ch->recoverycmd = 0;
	data = ccb->ataio.data_ptr;
	if ((ccb->ccb_h.status & CAM_STATUS_MASK) == CAM_REQ_CMP &&
	    (data[0] & 0x80) == 0) {
		for (i = 0; i < SIIS_MAX_SLOTS; i++) {
			if (!ch->hold[i])
				continue;
			if (ch->hold[i]->ccb_h.target_id != ccb->ccb_h.target_id)
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
			xpt_done(ch->hold[i]);
			ch->hold[i] = NULL;
			ch->numhslots--;
		}
	} else {
		if ((ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP)
			device_printf(dev, "Error while READ LOG EXT\n");
		else if ((data[0] & 0x80) == 0) {
			device_printf(dev, "Non-queued command error in READ LOG EXT\n");
		}
		for (i = 0; i < SIIS_MAX_SLOTS; i++) {
			if (!ch->hold[i])
				continue;
			if (ch->hold[i]->ccb_h.target_id != ccb->ccb_h.target_id)
				continue;
			xpt_done(ch->hold[i]);
			ch->hold[i] = NULL;
			ch->numhslots--;
		}
	}
	free(ccb->ataio.data_ptr, M_SIIS);
	xpt_free_ccb(ccb);
}

static void
siis_process_request_sense(device_t dev, union ccb *ccb)
{
	struct siis_channel *ch = device_get_softc(dev);
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
}

static void
siis_portinit(device_t dev)
{
	struct siis_channel *ch = device_get_softc(dev);
	int i;

	ch->eslots = 0;
	ch->recovery = 0;
	ATA_OUTL(ch->r_mem, SIIS_P_CTLCLR, SIIS_P_CTL_RESUME);
	for (i = 0; i < 16; i++) {
		ATA_OUTL(ch->r_mem, SIIS_P_PMPSTS(i), 0),
		ATA_OUTL(ch->r_mem, SIIS_P_PMPQACT(i), 0);
	}
	ATA_OUTL(ch->r_mem, SIIS_P_CTLSET, SIIS_P_CTL_PORT_INIT);
	siis_wait_ready(dev, 1000);
}

static int
siis_devreset(device_t dev)
{
	struct siis_channel *ch = device_get_softc(dev);
	int timeout = 0;
	uint32_t val;

	ATA_OUTL(ch->r_mem, SIIS_P_CTLSET, SIIS_P_CTL_DEV_RESET);
	while (((val = ATA_INL(ch->r_mem, SIIS_P_STS)) &
	    SIIS_P_CTL_DEV_RESET) != 0) {
		DELAY(100);
		if (timeout++ > 1000) {
			device_printf(dev, "device reset stuck "
			    "(timeout 100ms) status = %08x\n", val);
			return (EBUSY);
		}
	}
	return (0);
}

static int
siis_wait_ready(device_t dev, int t)
{
	struct siis_channel *ch = device_get_softc(dev);
	int timeout = 0;
	uint32_t val;

	while (((val = ATA_INL(ch->r_mem, SIIS_P_STS)) &
	    SIIS_P_CTL_READY) == 0) {
		DELAY(1000);
		if (timeout++ > t) {
			device_printf(dev, "port is not ready (timeout %dms) "
			    "status = %08x\n", t, val);
			return (EBUSY);
		}
	}
	return (0);
}

static void
siis_reset(device_t dev)
{
	struct siis_channel *ch = device_get_softc(dev);
	int i, retry = 0, sata_rev;
	uint32_t val;

	xpt_freeze_simq(ch->sim, 1);
	if (bootverbose)
		device_printf(dev, "SIIS reset...\n");
	if (!ch->recoverycmd && !ch->recovery)
		xpt_freeze_simq(ch->sim, ch->numrslots);
	/* Requeue frozen command. */
	if (ch->frozen) {
		union ccb *fccb = ch->frozen;
		ch->frozen = NULL;
		fccb->ccb_h.status &= ~CAM_STATUS_MASK;
		fccb->ccb_h.status |= CAM_REQUEUE_REQ | CAM_RELEASE_SIMQ;
		if (!(fccb->ccb_h.status & CAM_DEV_QFRZN)) {
			xpt_freeze_devq(fccb->ccb_h.path, 1);
			fccb->ccb_h.status |= CAM_DEV_QFRZN;
		}
		xpt_done(fccb);
	}
	/* Requeue all running commands. */
	for (i = 0; i < SIIS_MAX_SLOTS; i++) {
		/* Do we have a running request on slot? */
		if (ch->slot[i].state < SIIS_SLOT_RUNNING)
			continue;
		/* XXX; Commands in loading state. */
		siis_end_transaction(&ch->slot[i], SIIS_ERR_INNOCENT);
	}
	/* Finish all held commands as-is. */
	for (i = 0; i < SIIS_MAX_SLOTS; i++) {
		if (!ch->hold[i])
			continue;
		xpt_done(ch->hold[i]);
		ch->hold[i] = NULL;
		ch->numhslots--;
	}
	if (ch->toslots != 0)
		xpt_release_simq(ch->sim, TRUE);
	ch->eslots = 0;
	ch->recovery = 0;
	ch->toslots = 0;
	ch->fatalerr = 0;
	/* Disable port interrupts */
	ATA_OUTL(ch->r_mem, SIIS_P_IECLR, 0x0000FFFF);
	/* Set speed limit. */
	sata_rev = ch->user[ch->pm_present ? 15 : 0].revision;
	if (sata_rev == 1)
		val = ATA_SC_SPD_SPEED_GEN1;
	else if (sata_rev == 2)
		val = ATA_SC_SPD_SPEED_GEN2;
	else if (sata_rev == 3)
		val = ATA_SC_SPD_SPEED_GEN3;
	else
		val = 0;
	ATA_OUTL(ch->r_mem, SIIS_P_SCTL,
	    ATA_SC_DET_IDLE | val | ((ch->pm_level > 0) ? 0 :
	    (ATA_SC_IPM_DIS_PARTIAL | ATA_SC_IPM_DIS_SLUMBER)));
retry:
	siis_devreset(dev);
	/* Reset and reconnect PHY, */
	if (!siis_sata_connect(ch)) {
		ch->devices = 0;
		/* Enable port interrupts */
		ATA_OUTL(ch->r_mem, SIIS_P_IESET, SIIS_P_IX_ENABLED);
		if (bootverbose)
			device_printf(dev,
			    "SIIS reset done: phy reset found no device\n");
		/* Tell the XPT about the event */
		xpt_async(AC_BUS_RESET, ch->path, NULL);
		xpt_release_simq(ch->sim, TRUE);
		return;
	}
	/* Wait for port ready status. */
	if (siis_wait_ready(dev, 1000)) {
		device_printf(dev, "port ready timeout\n");
		if (!retry) {
			device_printf(dev, "trying full port reset ...\n");
			/* Get port to the reset state. */
			ATA_OUTL(ch->r_mem, SIIS_P_CTLSET, SIIS_P_CTL_PORT_RESET);
			DELAY(10000);
			/* Get port out of reset state. */
			ATA_OUTL(ch->r_mem, SIIS_P_CTLCLR, SIIS_P_CTL_PORT_RESET);
			ATA_OUTL(ch->r_mem, SIIS_P_CTLCLR, SIIS_P_CTL_32BIT);
			if (ch->pm_present)
				ATA_OUTL(ch->r_mem, SIIS_P_CTLSET, SIIS_P_CTL_PME);
			else
				ATA_OUTL(ch->r_mem, SIIS_P_CTLCLR, SIIS_P_CTL_PME);
			siis_wait_ready(dev, 5000);
			retry = 1;
			goto retry;
		}
	}
	ch->devices = 1;
	/* Enable port interrupts */
	ATA_OUTL(ch->r_mem, SIIS_P_IS, 0xFFFFFFFF);
	ATA_OUTL(ch->r_mem, SIIS_P_IESET, SIIS_P_IX_ENABLED);
	if (bootverbose)
		device_printf(dev, "SIIS reset done: devices=%08x\n", ch->devices);
	/* Tell the XPT about the event */
	xpt_async(AC_BUS_RESET, ch->path, NULL);
	xpt_release_simq(ch->sim, TRUE);
}

static int
siis_setup_fis(device_t dev, struct siis_cmd *ctp, union ccb *ccb, int tag)
{
	struct siis_channel *ch = device_get_softc(dev);
	u_int8_t *fis = &ctp->fis[0];

	bzero(fis, 24);
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
		bzero(ctp->u.atapi.ccb, 16);
		bcopy((ccb->ccb_h.flags & CAM_CDB_POINTER) ?
		    ccb->csio.cdb_io.cdb_ptr : ccb->csio.cdb_io.cdb_bytes,
		    ctp->u.atapi.ccb, ccb->csio.cdb_len);
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
			fis[13] = 0;
		} else {
			fis[12] = ccb->ataio.cmd.sector_count;
			fis[13] = ccb->ataio.cmd.sector_count_exp;
		}
		fis[15] = ATA_A_4BIT;
		if (ccb->ataio.ata_flags & ATA_FLAG_AUX) {
			fis[16] =  ccb->ataio.aux        & 0xff;
			fis[17] = (ccb->ataio.aux >>  8) & 0xff;
			fis[18] = (ccb->ataio.aux >> 16) & 0xff;
			fis[19] = (ccb->ataio.aux >> 24) & 0xff;
		}
	} else {
		/* Soft reset. */
	}
	return (20);
}

static int
siis_sata_connect(struct siis_channel *ch)
{
	u_int32_t status;
	int timeout, found = 0;

	/* Wait up to 100ms for "connect well" */
	for (timeout = 0; timeout < 1000 ; timeout++) {
		status = ATA_INL(ch->r_mem, SIIS_P_SSTS);
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
	ATA_OUTL(ch->r_mem, SIIS_P_SERR, 0xffffffff);
	return (1);
}

static int
siis_check_ids(device_t dev, union ccb *ccb)
{

	if (ccb->ccb_h.target_id > 15) {
		ccb->ccb_h.status = CAM_TID_INVALID;
		xpt_done(ccb);
		return (-1);
	}
	if (ccb->ccb_h.target_lun != 0) {
		ccb->ccb_h.status = CAM_LUN_INVALID;
		xpt_done(ccb);
		return (-1);
	}
	return (0);
}

static void
siisaction(struct cam_sim *sim, union ccb *ccb)
{
	device_t dev, parent;
	struct siis_channel *ch;

	CAM_DEBUG(ccb->ccb_h.path, CAM_DEBUG_TRACE, ("siisaction func_code=%x\n",
	    ccb->ccb_h.func_code));

	ch = (struct siis_channel *)cam_sim_softc(sim);
	dev = ch->dev;
	mtx_assert(&ch->mtx, MA_OWNED);
	switch (ccb->ccb_h.func_code) {
	/* Common cases first */
	case XPT_ATA_IO:	/* Execute the requested I/O operation */
	case XPT_SCSI_IO:
		if (siis_check_ids(dev, ccb))
			return;
		if (ch->devices == 0 ||
		    (ch->pm_present == 0 &&
		     ccb->ccb_h.target_id > 0 && ccb->ccb_h.target_id < 15)) {
			ccb->ccb_h.status = CAM_SEL_TIMEOUT;
			break;
		}
		ccb->ccb_h.recovery_type = RECOVERY_NONE;
		/* Check for command collision. */
		if (siis_check_collision(dev, ccb)) {
			/* Freeze command. */
			ch->frozen = ccb;
			/* We have only one frozen slot, so freeze simq also. */
			xpt_freeze_simq(ch->sim, 1);
			return;
		}
		siis_begin_transaction(dev, ccb);
		return;
	case XPT_ABORT:			/* Abort the specified CCB */
		/* XXX Implement */
		ccb->ccb_h.status = CAM_REQ_INVALID;
		break;
	case XPT_SET_TRAN_SETTINGS:
	{
		struct	ccb_trans_settings *cts = &ccb->cts;
		struct	siis_device *d; 

		if (siis_check_ids(dev, ccb))
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
			d->tags = min(SIIS_MAX_SLOTS, cts->xport_specific.sata.tags);
		if (cts->xport_specific.sata.valid & CTS_SATA_VALID_PM) {
			ch->pm_present = cts->xport_specific.sata.pm_present;
			if (ch->pm_present)
				ATA_OUTL(ch->r_mem, SIIS_P_CTLSET, SIIS_P_CTL_PME);
			else
				ATA_OUTL(ch->r_mem, SIIS_P_CTLCLR, SIIS_P_CTL_PME);
		}
		if (cts->xport_specific.sata.valid & CTS_SATA_VALID_TAGS)
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
		struct  siis_device *d;
		uint32_t status;

		if (siis_check_ids(dev, ccb))
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
			status = ATA_INL(ch->r_mem, SIIS_P_SSTS) & ATA_SS_SPD_MASK;
			if (status & 0x0f0) {
				cts->xport_specific.sata.revision =
				    (status & 0x0f0) >> 4;
				cts->xport_specific.sata.valid |=
				    CTS_SATA_VALID_REVISION;
			}
			cts->xport_specific.sata.caps = d->caps & CTS_SATA_CAPS_D;
			if (ch->pm_level)
				cts->xport_specific.sata.caps |= CTS_SATA_CAPS_H_PMREQ;
			cts->xport_specific.sata.caps |= CTS_SATA_CAPS_H_AN;
			cts->xport_specific.sata.caps &=
			    ch->user[ccb->ccb_h.target_id].caps;
			cts->xport_specific.sata.valid |= CTS_SATA_VALID_CAPS;
		} else {
			cts->xport_specific.sata.revision = d->revision;
			cts->xport_specific.sata.valid |= CTS_SATA_VALID_REVISION;
			cts->xport_specific.sata.caps = d->caps;
			if (cts->type == CTS_TYPE_CURRENT_SETTINGS &&
			    (ch->quirks & SIIS_Q_SNTF) == 0)
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
		siis_reset(dev);
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
		cpi->hba_inquiry = PI_SDTR_ABLE | PI_TAG_ABLE;
		cpi->hba_inquiry |= PI_SATAPM;
		cpi->target_sprt = 0;
		cpi->hba_misc = PIM_SEQSCAN | PIM_UNMAPPED | PIM_ATA_EXT;
		cpi->hba_eng_cnt = 0;
		cpi->max_target = 15;
		cpi->max_lun = 0;
		cpi->initiator_id = 0;
		cpi->bus_id = cam_sim_bus(sim);
		cpi->base_transfer_speed = 150000;
		strlcpy(cpi->sim_vid, "FreeBSD", SIM_IDLEN);
		strlcpy(cpi->hba_vid, "SIIS", HBA_IDLEN);
		strlcpy(cpi->dev_name, cam_sim_name(sim), DEV_IDLEN);
		cpi->unit_number = cam_sim_unit(sim);
		cpi->transport = XPORT_SATA;
		cpi->transport_version = XPORT_VERSION_UNSPECIFIED;
		cpi->protocol = PROTO_ATA;
		cpi->protocol_version = PROTO_VERSION_UNSPECIFIED;
		cpi->maxio = MAXPHYS;
		cpi->hba_vendor = pci_get_vendor(parent);
		cpi->hba_device = pci_get_device(parent);
		cpi->hba_subvendor = pci_get_subvendor(parent);
		cpi->hba_subdevice = pci_get_subdevice(parent);
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
siispoll(struct cam_sim *sim)
{
	struct siis_channel *ch = (struct siis_channel *)cam_sim_softc(sim);

	siis_ch_intr(ch->dev);
}
