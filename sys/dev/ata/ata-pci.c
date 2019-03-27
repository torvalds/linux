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
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/ata.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/malloc.h>
#include <sys/sema.h>
#include <sys/taskqueue.h>
#include <vm/uma.h>
#include <machine/stdarg.h>
#include <machine/resource.h>
#include <machine/bus.h>
#include <sys/rman.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>
#include <dev/ata/ata-all.h>
#include <dev/ata/ata-pci.h>
#include <ata_if.h>

MALLOC_DEFINE(M_ATAPCI, "ata_pci", "ATA driver PCI");

/* misc defines */
#define IOMASK                  0xfffffffc

/*
 * generic PCI ATA device probe
 */
int
ata_pci_probe(device_t dev)
{
    struct ata_pci_controller *ctlr = device_get_softc(dev);
    char buffer[64];

    /* is this a storage class device ? */
    if (pci_get_class(dev) != PCIC_STORAGE)
	return (ENXIO);

    /* is this an IDE/ATA type device ? */
    if (pci_get_subclass(dev) != PCIS_STORAGE_IDE)
	return (ENXIO);
    
    sprintf(buffer, "%s ATA controller", ata_pcivendor2str(dev));
    device_set_desc_copy(dev, buffer);
    ctlr->chipinit = ata_generic_chipinit;

    /* we are a low priority handler */
    return (BUS_PROBE_GENERIC);
}

int
ata_pci_attach(device_t dev)
{
    struct ata_pci_controller *ctlr = device_get_softc(dev);
    device_t child;
    u_int32_t cmd;
    int unit;

    /* do chipset specific setups only needed once */
    ctlr->legacy = ata_legacy(dev);
    if (ctlr->legacy || pci_read_config(dev, PCIR_BAR(2), 4) & IOMASK)
	ctlr->channels = 2;
    else
	ctlr->channels = 1;
    ctlr->ichannels = -1;
    ctlr->ch_attach = ata_pci_ch_attach;
    ctlr->ch_detach = ata_pci_ch_detach;
    ctlr->dev = dev;

    /* if needed try to enable busmastering */
    pci_enable_busmaster(dev);
    cmd = pci_read_config(dev, PCIR_COMMAND, 2);

    /* if busmastering mode "stuck" use it */
    if ((cmd & PCIM_CMD_BUSMASTEREN) == PCIM_CMD_BUSMASTEREN) {
	ctlr->r_type1 = SYS_RES_IOPORT;
	ctlr->r_rid1 = ATA_BMADDR_RID;
	ctlr->r_res1 = bus_alloc_resource_any(dev, ctlr->r_type1, &ctlr->r_rid1,
					      RF_ACTIVE);
    }

    if (ctlr->chipinit(dev))
	return ENXIO;

    /* attach all channels on this controller */
    for (unit = 0; unit < ctlr->channels; unit++) {
	if ((ctlr->ichannels & (1 << unit)) == 0)
	    continue;
	child = device_add_child(dev, "ata",
	    ((unit == 0 || unit == 1) && ctlr->legacy) ?
	    unit : devclass_find_free_unit(ata_devclass, 2));
	if (child == NULL)
	    device_printf(dev, "failed to add ata child device\n");
	else
	    device_set_ivars(child, (void *)(intptr_t)unit);
    }
    bus_generic_attach(dev);
    return 0;
}

int
ata_pci_detach(device_t dev)
{
    struct ata_pci_controller *ctlr = device_get_softc(dev);

    /* detach & delete all children */
    device_delete_children(dev);

    if (ctlr->r_irq) {
	bus_teardown_intr(dev, ctlr->r_irq, ctlr->handle);
	bus_release_resource(dev, SYS_RES_IRQ, ctlr->r_irq_rid, ctlr->r_irq);
	if (ctlr->r_irq_rid != ATA_IRQ_RID)
	    pci_release_msi(dev);
    }
    if (ctlr->chipdeinit != NULL)
	ctlr->chipdeinit(dev);
    if (ctlr->r_res2) {
#ifdef __sparc64__
	bus_space_unmap(rman_get_bustag(ctlr->r_res2),
	    rman_get_bushandle(ctlr->r_res2), rman_get_size(ctlr->r_res2));
#endif
	bus_release_resource(dev, ctlr->r_type2, ctlr->r_rid2, ctlr->r_res2);
    }
    if (ctlr->r_res1) {
#ifdef __sparc64__
	bus_space_unmap(rman_get_bustag(ctlr->r_res1),
	    rman_get_bushandle(ctlr->r_res1), rman_get_size(ctlr->r_res1));
#endif
	bus_release_resource(dev, ctlr->r_type1, ctlr->r_rid1, ctlr->r_res1);
    }

    return 0;
}

int
ata_pci_suspend(device_t dev)
{
    struct ata_pci_controller *ctlr = device_get_softc(dev);
    int error = 0;
 
    bus_generic_suspend(dev);
    if (ctlr->suspend)
	error = ctlr->suspend(dev);
    return error;
}
  
int
ata_pci_resume(device_t dev)
{
    struct ata_pci_controller *ctlr = device_get_softc(dev);
    int error = 0;
 
    if (ctlr->resume)
	error = ctlr->resume(dev);
    bus_generic_resume(dev);
    return error;
}

int
ata_pci_read_ivar(device_t dev, device_t child, int which, uintptr_t *result)
{

	return (BUS_READ_IVAR(device_get_parent(dev), dev, which, result));
}

int
ata_pci_write_ivar(device_t dev, device_t child, int which, uintptr_t value)
{

	return (BUS_WRITE_IVAR(device_get_parent(dev), dev, which, value));
}

uint32_t
ata_pci_read_config(device_t dev, device_t child, int reg, int width)
{

	return (pci_read_config(dev, reg, width));
}

void
ata_pci_write_config(device_t dev, device_t child, int reg, 
    uint32_t val, int width)
{

	pci_write_config(dev, reg, val, width);
}

struct resource *
ata_pci_alloc_resource(device_t dev, device_t child, int type, int *rid,
		       rman_res_t start, rman_res_t end, rman_res_t count,
		       u_int flags)
{
	struct ata_pci_controller *controller = device_get_softc(dev);
	struct resource *res = NULL;

	if (device_get_devclass(child) == ata_devclass) {
		int unit = ((struct ata_channel *)device_get_softc(child))->unit;
		int myrid;

		if (type == SYS_RES_IOPORT) {
			switch (*rid) {
			case ATA_IOADDR_RID:
			    if (controller->legacy) {
				start = (unit ? ATA_SECONDARY : ATA_PRIMARY);
				count = ATA_IOSIZE;
				end = start + count - 1;
			    }
			    myrid = PCIR_BAR(0) + (unit << 3);
			    res = BUS_ALLOC_RESOURCE(device_get_parent(dev), dev,
				SYS_RES_IOPORT, &myrid,
				start, end, count, flags);
			    break;
			case ATA_CTLADDR_RID:
			    if (controller->legacy) {
				start = (unit ? ATA_SECONDARY : ATA_PRIMARY) +
				    ATA_CTLOFFSET;
				count = ATA_CTLIOSIZE;
				end = start + count - 1;
			    }
			    myrid = PCIR_BAR(1) + (unit << 3);
			    res = BUS_ALLOC_RESOURCE(device_get_parent(dev), dev,
				SYS_RES_IOPORT, &myrid,
				start, end, count, flags);
			    break;
			}
		}
		if (type == SYS_RES_IRQ && *rid == ATA_IRQ_RID) {
			if (controller->legacy) {
			    int irq = (unit == 0 ? 14 : 15);
	    
			    res = BUS_ALLOC_RESOURCE(device_get_parent(dev), child,
				SYS_RES_IRQ, rid, irq, irq, 1, flags);
			} else
			    res = controller->r_irq;
		}
	} else {
		if (type == SYS_RES_IRQ) {
			if (*rid != ATA_IRQ_RID)
				return (NULL);
			res = controller->r_irq;
		} else {
			res = BUS_ALLOC_RESOURCE(device_get_parent(dev), dev,
			     type, rid, start, end, count, flags);
		}
	}
	return (res);
}

int
ata_pci_release_resource(device_t dev, device_t child, int type, int rid,
			 struct resource *r)
{

	if (device_get_devclass(child) == ata_devclass) {
		struct ata_pci_controller *controller = device_get_softc(dev);
		int unit = ((struct ata_channel *)device_get_softc(child))->unit;

	        if (type == SYS_RES_IOPORT) {
	    		switch (rid) {
			case ATA_IOADDR_RID:
		    	    return BUS_RELEASE_RESOURCE(device_get_parent(dev), dev,
				SYS_RES_IOPORT,
				PCIR_BAR(0) + (unit << 3), r);
			case ATA_CTLADDR_RID:
			    return BUS_RELEASE_RESOURCE(device_get_parent(dev), dev,
				SYS_RES_IOPORT,
				PCIR_BAR(1) + (unit << 3), r);
			default:
			    return ENOENT;
			}
		}
		if (type == SYS_RES_IRQ) {
			if (rid != ATA_IRQ_RID)
				return ENOENT;
			if (controller->legacy) {
				return BUS_RELEASE_RESOURCE(device_get_parent(dev), child,
				    SYS_RES_IRQ, rid, r);
			} else  
				return 0;
		}
	} else {
		if (type == SYS_RES_IRQ) {
			if (rid != ATA_IRQ_RID)
				return (ENOENT);
			return (0);
		} else {
			return (BUS_RELEASE_RESOURCE(device_get_parent(dev), child,
			    type, rid, r));
		}
	}
	return (EINVAL);
}

int
ata_pci_setup_intr(device_t dev, device_t child, struct resource *irq, 
		   int flags, driver_filter_t *filter, driver_intr_t *function, 
		   void *argument, void **cookiep)
{
	struct ata_pci_controller *controller = device_get_softc(dev);

	if (controller->legacy) {
		return BUS_SETUP_INTR(device_get_parent(dev), child, irq,
			      flags, filter, function, argument, cookiep);
	} else {
		struct ata_pci_controller *controller = device_get_softc(dev);
		int unit;

	    	if (filter != NULL) {
			printf("ata-pci.c: we cannot use a filter here\n");
			return (EINVAL);
		}
		if (device_get_devclass(child) == ata_devclass)
			unit = ((struct ata_channel *)device_get_softc(child))->unit;
		else
			unit = ATA_PCI_MAX_CH - 1;
		controller->interrupt[unit].function = function;
		controller->interrupt[unit].argument = argument;
		*cookiep = controller;
		return 0;
	}
}

int
ata_pci_teardown_intr(device_t dev, device_t child, struct resource *irq,
		      void *cookie)
{
	struct ata_pci_controller *controller = device_get_softc(dev);

        if (controller->legacy) {
		return BUS_TEARDOWN_INTR(device_get_parent(dev), child, irq, cookie);
	} else {
		struct ata_pci_controller *controller = device_get_softc(dev);
		int unit;

		if (device_get_devclass(child) == ata_devclass)
			unit = ((struct ata_channel *)device_get_softc(child))->unit;
		else
			unit = ATA_PCI_MAX_CH - 1;
		controller->interrupt[unit].function = NULL;
		controller->interrupt[unit].argument = NULL;
		return 0;
	}
}
    
int
ata_generic_setmode(device_t dev, int target, int mode)
{

	return (min(mode, ATA_UDMA2));
}

int
ata_generic_chipinit(device_t dev)
{
    struct ata_pci_controller *ctlr = device_get_softc(dev);

    if (ata_setup_interrupt(dev, ata_generic_intr))
	return ENXIO;
    ctlr->setmode = ata_generic_setmode;
    return 0;
}

int
ata_pci_ch_attach(device_t dev)
{
    struct ata_pci_controller *ctlr = device_get_softc(device_get_parent(dev));
    struct ata_channel *ch = device_get_softc(dev);
    struct resource *io = NULL, *ctlio = NULL;
    int i, rid;

    rid = ATA_IOADDR_RID;
    if (!(io = bus_alloc_resource_any(dev, SYS_RES_IOPORT, &rid, RF_ACTIVE)))
	return ENXIO;

    rid = ATA_CTLADDR_RID;
    if (!(ctlio = bus_alloc_resource_any(dev, SYS_RES_IOPORT, &rid,RF_ACTIVE))){
	bus_release_resource(dev, SYS_RES_IOPORT, ATA_IOADDR_RID, io);
	return ENXIO;
    }

    ata_pci_dmainit(dev);

    for (i = ATA_DATA; i <= ATA_COMMAND; i ++) {
	ch->r_io[i].res = io;
	ch->r_io[i].offset = i;
    }
    ch->r_io[ATA_CONTROL].res = ctlio;
    ch->r_io[ATA_CONTROL].offset = ctlr->legacy ? 0 : 2;
    ch->r_io[ATA_IDX_ADDR].res = io;
    ata_default_registers(dev);
    if (ctlr->r_res1) {
	for (i = ATA_BMCMD_PORT; i <= ATA_BMDTP_PORT; i++) {
	    ch->r_io[i].res = ctlr->r_res1;
	    ch->r_io[i].offset = (i - ATA_BMCMD_PORT) + (ch->unit*ATA_BMIOSIZE);
	}
    }

    ata_pci_hw(dev);
    return 0;
}

int
ata_pci_ch_detach(device_t dev)
{
    struct ata_channel *ch = device_get_softc(dev);

    ata_pci_dmafini(dev);

    bus_release_resource(dev, SYS_RES_IOPORT, ATA_CTLADDR_RID,
	ch->r_io[ATA_CONTROL].res);
    bus_release_resource(dev, SYS_RES_IOPORT, ATA_IOADDR_RID,
	ch->r_io[ATA_IDX_ADDR].res);

    return (0);
}

int
ata_pci_status(device_t dev)
{
    struct ata_pci_controller *controller =
	device_get_softc(device_get_parent(dev));
    struct ata_channel *ch = device_get_softc(dev);

    if ((dumping || !controller->legacy) &&
	((ch->flags & ATA_ALWAYS_DMASTAT) ||
	 (ch->dma.flags & ATA_DMA_ACTIVE))) {
	int bmstat = ATA_IDX_INB(ch, ATA_BMSTAT_PORT) & ATA_BMSTAT_MASK;

	if ((bmstat & ATA_BMSTAT_INTERRUPT) == 0)
	    return 0;
	ATA_IDX_OUTB(ch, ATA_BMSTAT_PORT, bmstat & ~ATA_BMSTAT_ERROR);
	DELAY(1);
    }
    if (ATA_IDX_INB(ch, ATA_ALTSTAT) & ATA_S_BUSY) {
	DELAY(100);
	if (ATA_IDX_INB(ch, ATA_ALTSTAT) & ATA_S_BUSY)
	    return 0;
    }
    return 1;
}

void
ata_pci_hw(device_t dev)
{
    struct ata_channel *ch = device_get_softc(dev);

    ata_generic_hw(dev);
    ch->hw.status = ata_pci_status;
}

static int
ata_pci_dmastart(struct ata_request *request)
{
    struct ata_channel *ch = device_get_softc(request->parent);

    ATA_DEBUG_RQ(request, "dmastart");

    ATA_IDX_OUTB(ch, ATA_BMSTAT_PORT, (ATA_IDX_INB(ch, ATA_BMSTAT_PORT) | 
		 (ATA_BMSTAT_INTERRUPT | ATA_BMSTAT_ERROR)));
    ATA_IDX_OUTL(ch, ATA_BMDTP_PORT, request->dma->sg_bus);
    ch->dma.flags |= ATA_DMA_ACTIVE;
    ATA_IDX_OUTB(ch, ATA_BMCMD_PORT,
		 (ATA_IDX_INB(ch, ATA_BMCMD_PORT) & ~ATA_BMCMD_WRITE_READ) |
		 ((request->flags & ATA_R_READ) ? ATA_BMCMD_WRITE_READ : 0)|
		 ATA_BMCMD_START_STOP);
    return 0;
}

static int
ata_pci_dmastop(struct ata_request *request)
{
    struct ata_channel *ch = device_get_softc(request->parent);
    int error;

    ATA_DEBUG_RQ(request, "dmastop");

    ATA_IDX_OUTB(ch, ATA_BMCMD_PORT, 
		 ATA_IDX_INB(ch, ATA_BMCMD_PORT) & ~ATA_BMCMD_START_STOP);
    ch->dma.flags &= ~ATA_DMA_ACTIVE;
    error = ATA_IDX_INB(ch, ATA_BMSTAT_PORT) & ATA_BMSTAT_MASK;
    ATA_IDX_OUTB(ch, ATA_BMSTAT_PORT, ATA_BMSTAT_INTERRUPT | ATA_BMSTAT_ERROR);
    return error;
}

static void
ata_pci_dmareset(device_t dev)
{
    struct ata_channel *ch = device_get_softc(dev);
    struct ata_request *request;

    ATA_IDX_OUTB(ch, ATA_BMCMD_PORT, 
		 ATA_IDX_INB(ch, ATA_BMCMD_PORT) & ~ATA_BMCMD_START_STOP);
    ch->dma.flags &= ~ATA_DMA_ACTIVE;
    ATA_IDX_OUTB(ch, ATA_BMSTAT_PORT, ATA_BMSTAT_INTERRUPT | ATA_BMSTAT_ERROR);
    if ((request = ch->running)) {
	device_printf(dev, "DMA reset calling unload\n");
	ch->dma.unload(request);
    }
}

void
ata_pci_dmainit(device_t dev)
{
    struct ata_channel *ch = device_get_softc(dev);

    ata_dmainit(dev);
    ch->dma.start = ata_pci_dmastart;
    ch->dma.stop = ata_pci_dmastop;
    ch->dma.reset = ata_pci_dmareset;
}

void
ata_pci_dmafini(device_t dev)
{

    ata_dmafini(dev);
}

int
ata_pci_print_child(device_t dev, device_t child)
{
	int retval;

	retval = bus_print_child_header(dev, child);
	retval += printf(" at channel %d",
	    (int)(intptr_t)device_get_ivars(child));
	retval += bus_print_child_footer(dev, child);

	return (retval);
}

int
ata_pci_child_location_str(device_t dev, device_t child, char *buf,
    size_t buflen)
{

	snprintf(buf, buflen, "channel=%d",
	    (int)(intptr_t)device_get_ivars(child));
	return (0);
}

static bus_dma_tag_t
ata_pci_get_dma_tag(device_t bus, device_t child)
{

	return (bus_get_dma_tag(bus));
}

static device_method_t ata_pci_methods[] = {
    /* device interface */
    DEVMETHOD(device_probe,             ata_pci_probe),
    DEVMETHOD(device_attach,            ata_pci_attach),
    DEVMETHOD(device_detach,            ata_pci_detach),
    DEVMETHOD(device_suspend,           ata_pci_suspend),
    DEVMETHOD(device_resume,            ata_pci_resume),
    DEVMETHOD(device_shutdown,          bus_generic_shutdown),

    /* bus methods */
    DEVMETHOD(bus_read_ivar,		ata_pci_read_ivar),
    DEVMETHOD(bus_write_ivar,		ata_pci_write_ivar),
    DEVMETHOD(bus_alloc_resource,       ata_pci_alloc_resource),
    DEVMETHOD(bus_release_resource,     ata_pci_release_resource),
    DEVMETHOD(bus_activate_resource,    bus_generic_activate_resource),
    DEVMETHOD(bus_deactivate_resource,  bus_generic_deactivate_resource),
    DEVMETHOD(bus_setup_intr,           ata_pci_setup_intr),
    DEVMETHOD(bus_teardown_intr,        ata_pci_teardown_intr),
    DEVMETHOD(pci_read_config,		ata_pci_read_config),
    DEVMETHOD(pci_write_config,		ata_pci_write_config),
    DEVMETHOD(bus_print_child,		ata_pci_print_child),
    DEVMETHOD(bus_child_location_str,	ata_pci_child_location_str),
    DEVMETHOD(bus_get_dma_tag,		ata_pci_get_dma_tag),

    DEVMETHOD_END
};

devclass_t ata_pci_devclass;

static driver_t ata_pci_driver = {
    "atapci",
    ata_pci_methods,
    sizeof(struct ata_pci_controller),
};

DRIVER_MODULE(atapci, pci, ata_pci_driver, ata_pci_devclass, NULL, NULL);
MODULE_VERSION(atapci, 1);
MODULE_DEPEND(atapci, ata, 1, 1, 1);

static int
ata_pcichannel_probe(device_t dev)
{

    if ((intptr_t)device_get_ivars(dev) < 0)
	    return (ENXIO);
    device_set_desc(dev, "ATA channel");

    return ata_probe(dev);
}

static int
ata_pcichannel_attach(device_t dev)
{
    struct ata_pci_controller *ctlr = device_get_softc(device_get_parent(dev));
    struct ata_channel *ch = device_get_softc(dev);
    int error;

    if (ch->attached)
	return (0);
    ch->attached = 1;

    ch->dev = dev;
    ch->unit = (intptr_t)device_get_ivars(dev);

    resource_int_value(device_get_name(dev),
	device_get_unit(dev), "pm_level", &ch->pm_level);

    if ((error = ctlr->ch_attach(dev)))
	return error;

    return ata_attach(dev);
}

static int
ata_pcichannel_detach(device_t dev)
{
    struct ata_pci_controller *ctlr = device_get_softc(device_get_parent(dev));
    struct ata_channel *ch = device_get_softc(dev);
    int error;

    if (!ch->attached)
	return (0);
    ch->attached = 0;

    if ((error = ata_detach(dev)))
	return error;

    if (ctlr->ch_detach)
	return (ctlr->ch_detach(dev));

    return (0);
}
static int
ata_pcichannel_suspend(device_t dev)
{
    struct ata_pci_controller *ctlr = device_get_softc(device_get_parent(dev));
    struct ata_channel *ch = device_get_softc(dev);
    int error;

    if (!ch->attached)
	return (0);

    if ((error = ata_suspend(dev)))
	return (error);

    if (ctlr->ch_suspend != NULL && (error = ctlr->ch_suspend(dev)))
	return (error);

    return (0);
}

static int
ata_pcichannel_resume(device_t dev)
{
    struct ata_pci_controller *ctlr = device_get_softc(device_get_parent(dev));
    struct ata_channel *ch = device_get_softc(dev);
    int error;

    if (!ch->attached)
	return (0);

    if (ctlr->ch_resume != NULL && (error = ctlr->ch_resume(dev)))
	return (error);

    return ata_resume(dev);
}

static void
ata_pcichannel_reset(device_t dev)
{
    struct ata_pci_controller *ctlr = device_get_softc(device_get_parent(dev));
    struct ata_channel *ch = device_get_softc(dev);

    /* if DMA engine present reset it  */
    if (ch->dma.reset)
	ch->dma.reset(dev);

    /* reset the controller HW */
    if (ctlr->reset)
	ctlr->reset(dev);
    else
	ata_generic_reset(dev);
}

static int
ata_pcichannel_setmode(device_t dev, int target, int mode)
{
	struct ata_pci_controller *ctlr = device_get_softc(device_get_parent(dev));

	if (ctlr->setmode)
		return (ctlr->setmode(dev, target, mode));
	else
		return (ata_generic_setmode(dev, target, mode));
}

static int
ata_pcichannel_getrev(device_t dev, int target)
{
	struct ata_pci_controller *ctlr = device_get_softc(device_get_parent(dev));
	struct ata_channel *ch = device_get_softc(dev);

	if (ch->flags & ATA_SATA) {
		if (ctlr->getrev)
			return (ctlr->getrev(dev, target));
		else 
			return (0xff);
	} else
		return (0);
}

static device_method_t ata_pcichannel_methods[] = {
    /* device interface */
    DEVMETHOD(device_probe,     ata_pcichannel_probe),
    DEVMETHOD(device_attach,    ata_pcichannel_attach),
    DEVMETHOD(device_detach,    ata_pcichannel_detach),
    DEVMETHOD(device_shutdown,  bus_generic_shutdown),
    DEVMETHOD(device_suspend,   ata_pcichannel_suspend),
    DEVMETHOD(device_resume,    ata_pcichannel_resume),

    /* ATA methods */
    DEVMETHOD(ata_setmode,      ata_pcichannel_setmode),
    DEVMETHOD(ata_getrev,       ata_pcichannel_getrev),
    DEVMETHOD(ata_reset,        ata_pcichannel_reset),

    DEVMETHOD_END
};

driver_t ata_pcichannel_driver = {
    "ata",
    ata_pcichannel_methods,
    sizeof(struct ata_channel),
};

DRIVER_MODULE(ata, atapci, ata_pcichannel_driver, ata_devclass, NULL, NULL);

/*
 * misc support fucntions
 */
int
ata_legacy(device_t dev)
{
    return (((pci_read_config(dev, PCIR_SUBCLASS, 1) == PCIS_STORAGE_IDE) &&
	     (pci_read_config(dev, PCIR_PROGIF, 1)&PCIP_STORAGE_IDE_MASTERDEV)&&
	     ((pci_read_config(dev, PCIR_PROGIF, 1) &
	       (PCIP_STORAGE_IDE_MODEPRIM | PCIP_STORAGE_IDE_MODESEC)) !=
	      (PCIP_STORAGE_IDE_MODEPRIM | PCIP_STORAGE_IDE_MODESEC))) ||
	    (!pci_read_config(dev, PCIR_BAR(0), 4) &&
	     !pci_read_config(dev, PCIR_BAR(1), 4) &&
	     !pci_read_config(dev, PCIR_BAR(2), 4) &&
	     !pci_read_config(dev, PCIR_BAR(3), 4) &&
	     !pci_read_config(dev, PCIR_BAR(5), 4)));
}

void
ata_generic_intr(void *data)
{
    struct ata_pci_controller *ctlr = data;
    struct ata_channel *ch;
    int unit;

    for (unit = 0; unit < ATA_PCI_MAX_CH; unit++) {
	if ((ch = ctlr->interrupt[unit].argument))
	    ctlr->interrupt[unit].function(ch);
    }
}

int
ata_setup_interrupt(device_t dev, void *intr_func)
{
    struct ata_pci_controller *ctlr = device_get_softc(dev);
    int i, msi = 0;

    if (!ctlr->legacy) {
	if (resource_int_value(device_get_name(dev),
		device_get_unit(dev), "msi", &i) == 0 && i != 0)
	    msi = 1;
	if (msi && pci_msi_count(dev) > 0 && pci_alloc_msi(dev, &msi) == 0) {
	    ctlr->r_irq_rid = 0x1;
	} else {
	    msi = 0;
	    ctlr->r_irq_rid = ATA_IRQ_RID;
	}
	if (!(ctlr->r_irq = bus_alloc_resource_any(dev, SYS_RES_IRQ,
		&ctlr->r_irq_rid, RF_SHAREABLE | RF_ACTIVE))) {
	    device_printf(dev, "unable to map interrupt\n");
	    if (msi)
		    pci_release_msi(dev);
	    return ENXIO;
	}
	if ((bus_setup_intr(dev, ctlr->r_irq, ATA_INTR_FLAGS, NULL,
			    intr_func, ctlr, &ctlr->handle))) {
	    device_printf(dev, "unable to setup interrupt\n");
	    bus_release_resource(dev,
		SYS_RES_IRQ, ctlr->r_irq_rid, ctlr->r_irq);
	    if (msi)
		    pci_release_msi(dev);
	    return ENXIO;
	}
    }
    return 0;
}

void
ata_set_desc(device_t dev)
{
    struct ata_pci_controller *ctlr = device_get_softc(dev);
    char buffer[128];

    sprintf(buffer, "%s %s %s controller",
            ata_pcivendor2str(dev), ctlr->chip->text, 
            ata_mode2str(ctlr->chip->max_dma));
    device_set_desc_copy(dev, buffer);
}

const struct ata_chip_id *
ata_match_chip(device_t dev, const struct ata_chip_id *index)
{
    uint32_t devid;
    uint8_t revid;

    devid = pci_get_devid(dev);
    revid = pci_get_revid(dev);
    while (index->chipid != 0) {
	if (devid == index->chipid && revid >= index->chiprev)
	    return (index);
	index++;
    }
    return (NULL);
}

const struct ata_chip_id *
ata_find_chip(device_t dev, const struct ata_chip_id *index, int slot)
{
    const struct ata_chip_id *idx;
    device_t *children;
    int nchildren, i;
    uint8_t s;

    if (device_get_children(device_get_parent(dev), &children, &nchildren))
	return (NULL);

    for (i = 0; i < nchildren; i++) {
	s = pci_get_slot(children[i]);
	if ((slot >= 0 && s == slot) || (slot < 0 && s <= -slot)) {
	    idx = ata_match_chip(children[i], index);
	    if (idx != NULL) {
		free(children, M_TEMP);
		return (idx);
	    }
	}
    }
    free(children, M_TEMP);
    return (NULL);
}

const char *
ata_pcivendor2str(device_t dev)
{
    switch (pci_get_vendor(dev)) {
    case ATA_ACARD_ID:          return "Acard";
    case ATA_ACER_LABS_ID:      return "AcerLabs";
    case ATA_AMD_ID:            return "AMD";
    case ATA_ADAPTEC_ID:        return "Adaptec";
    case ATA_ATI_ID:            return "ATI";
    case ATA_CYRIX_ID:          return "Cyrix";
    case ATA_CYPRESS_ID:        return "Cypress";
    case ATA_HIGHPOINT_ID:      return "HighPoint";
    case ATA_INTEL_ID:          return "Intel";
    case ATA_ITE_ID:            return "ITE";
    case ATA_JMICRON_ID:        return "JMicron";
    case ATA_MARVELL_ID:        return "Marvell";
    case ATA_MARVELL2_ID:       return "Marvell";
    case ATA_NATIONAL_ID:       return "National";
    case ATA_NETCELL_ID:        return "Netcell";
    case ATA_NVIDIA_ID:         return "nVidia";
    case ATA_PROMISE_ID:        return "Promise";
    case ATA_SERVERWORKS_ID:    return "ServerWorks";
    case ATA_SILICON_IMAGE_ID:  return "SiI";
    case ATA_SIS_ID:            return "SiS";
    case ATA_VIA_ID:            return "VIA";
    case ATA_CENATEK_ID:        return "Cenatek";
    case ATA_MICRON_ID:         return "Micron";
    default:                    return "Generic";
    }
}

int
ata_mode2idx(int mode)
{
    if ((mode & ATA_DMA_MASK) == ATA_UDMA0)
	return (mode & ATA_MODE_MASK) + 8;
    if ((mode & ATA_DMA_MASK) == ATA_WDMA0)
	return (mode & ATA_MODE_MASK) + 5;
    return (mode & ATA_MODE_MASK) - ATA_PIO0;
}
