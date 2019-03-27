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
#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>
#include <dev/ata/ata-all.h>
#include <dev/ata/ata-pci.h>
#include <ata_if.h>

/* local prototypes */
static int ata_marvell_chipinit(device_t dev);
static int ata_marvell_ch_attach(device_t dev);
static int ata_marvell_setmode(device_t dev, int target, int mode);
static int ata_marvell_dummy_chipinit(device_t dev);

/* misc defines */
#define MV_61XX		61
#define MV_91XX		91

/*
 * Marvell chipset support functions
 */
#define ATA_MV_HOST_BASE(ch) \
	((ch->unit & 3) * 0x0100) + (ch->unit > 3 ? 0x30000 : 0x20000)
#define ATA_MV_EDMA_BASE(ch) \
	((ch->unit & 3) * 0x2000) + (ch->unit > 3 ? 0x30000 : 0x20000)

struct ata_marvell_response {
    u_int16_t   tag;
    u_int8_t    edma_status;
    u_int8_t    dev_status;
    u_int32_t   timestamp;
};

struct ata_marvell_dma_prdentry {
    u_int32_t addrlo;
    u_int32_t count;
    u_int32_t addrhi;
    u_int32_t reserved;
};  

static int
ata_marvell_probe(device_t dev)
{
    struct ata_pci_controller *ctlr = device_get_softc(dev);
    static const struct ata_chip_id ids[] =
    {{ ATA_M88SE6101, 0, 0, MV_61XX, ATA_UDMA6, "88SE6101" },
     { ATA_M88SE6102, 0, 0, MV_61XX, ATA_UDMA6, "88SE6102" },
     { ATA_M88SE6111, 0, 1, MV_61XX, ATA_UDMA6, "88SE6111" },
     { ATA_M88SE6121, 0, 2, MV_61XX, ATA_UDMA6, "88SE6121" },
     { ATA_M88SE6141, 0, 4, MV_61XX, ATA_UDMA6, "88SE6141" },
     { ATA_M88SE6145, 0, 4, MV_61XX, ATA_UDMA6, "88SE6145" },
     { 0x91a41b4b,    0, 0, MV_91XX, ATA_UDMA6, "88SE912x" },
     { 0, 0, 0, 0, 0, 0}};

    if (pci_get_vendor(dev) != ATA_MARVELL_ID &&
	pci_get_vendor(dev) != ATA_MARVELL2_ID)
	return ENXIO;

    if (!(ctlr->chip = ata_match_chip(dev, ids)))
	return ENXIO;

    ata_set_desc(dev);

    switch (ctlr->chip->cfg2) {
    case MV_61XX:
	ctlr->chipinit = ata_marvell_chipinit;
	break;
    case MV_91XX:
	ctlr->chipinit = ata_marvell_dummy_chipinit;
	break;
    }
    return (BUS_PROBE_LOW_PRIORITY);
}

static int
ata_marvell_chipinit(device_t dev)
{
	struct ata_pci_controller *ctlr = device_get_softc(dev);
	device_t child;

	if (ata_setup_interrupt(dev, ata_generic_intr))
		return ENXIO;
	/* Create AHCI subdevice if AHCI part present. */
	if (ctlr->chip->cfg1) {
	    	child = device_add_child(dev, NULL, -1);
		if (child != NULL) {
		    device_set_ivars(child, (void *)(intptr_t)-1);
		    bus_generic_attach(dev);
		}
	}
        ctlr->ch_attach = ata_marvell_ch_attach;
	ctlr->ch_detach = ata_pci_ch_detach;
	ctlr->reset = ata_generic_reset;
        ctlr->setmode = ata_marvell_setmode;
        ctlr->channels = 1;
        return (0);
}

static int
ata_marvell_ch_attach(device_t dev)
{
	struct ata_channel *ch = device_get_softc(dev);
	int error;
 
	error = ata_pci_ch_attach(dev);
    	/* dont use 32 bit PIO transfers */
	ch->flags |= ATA_USE_16BIT;
	ch->flags |= ATA_CHECKS_CABLE;
	return (error);
}

static int
ata_marvell_setmode(device_t dev, int target, int mode)
{
	struct ata_pci_controller *ctlr = device_get_softc(device_get_parent(dev));
	struct ata_channel *ch = device_get_softc(dev);

	mode = min(mode, ctlr->chip->max_dma);
	/* Check for 80pin cable present. */
	if (ata_dma_check_80pin && mode > ATA_UDMA2 &&
	    ATA_IDX_INB(ch, ATA_BMDEVSPEC_0) & 0x01) {
		ata_print_cable(dev, "controller");
		mode = ATA_UDMA2;
	}
	/* Nothing to do to setup mode, the controller snoop SET_FEATURE cmd. */
	return (mode);
}

static int
ata_marvell_dummy_chipinit(device_t dev)
{
	struct ata_pci_controller *ctlr = device_get_softc(dev);

        ctlr->channels = 0;
        return (0);
}

ATA_DECLARE_DRIVER(ata_marvell);
