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
static int ata_jmicron_chipinit(device_t dev);
static int ata_jmicron_ch_attach(device_t dev);
static int ata_jmicron_setmode(device_t dev, int target, int mode);

/*
 * JMicron chipset support functions
 */
static int
ata_jmicron_probe(device_t dev)
{
    struct ata_pci_controller *ctlr = device_get_softc(dev);
    const struct ata_chip_id *idx;
    static const struct ata_chip_id ids[] =
    {{ ATA_JMB360, 0, 1, 0, ATA_SA300, "JMB360" },
     { ATA_JMB361, 0, 1, 1, ATA_UDMA6, "JMB361" },
     { ATA_JMB362, 0, 2, 0, ATA_SA300, "JMB362" },
     { ATA_JMB363, 0, 2, 1, ATA_UDMA6, "JMB363" },
     { ATA_JMB365, 0, 1, 2, ATA_UDMA6, "JMB365" },
     { ATA_JMB366, 0, 2, 2, ATA_UDMA6, "JMB366" },
     { ATA_JMB368, 0, 0, 1, ATA_UDMA6, "JMB368" },
     { ATA_JMB368_2, 0, 0, 1, ATA_UDMA6, "JMB368" },
     { 0, 0, 0, 0, 0, 0}};
    char buffer[64];

    if (pci_get_vendor(dev) != ATA_JMICRON_ID)
	return ENXIO;

    if (!(idx = ata_match_chip(dev, ids)))
        return ENXIO;

    sprintf(buffer, "JMicron %s %s controller",
	idx->text, ata_mode2str(idx->max_dma));
    device_set_desc_copy(dev, buffer);
    ctlr->chip = idx;
    ctlr->chipinit = ata_jmicron_chipinit;
    return (BUS_PROBE_LOW_PRIORITY);
}

static int
ata_jmicron_chipinit(device_t dev)
{
    struct ata_pci_controller *ctlr = device_get_softc(dev);
    device_t child;

    if (ata_setup_interrupt(dev, ata_generic_intr))
	return ENXIO;

    /* do we have multiple PCI functions ? */
    if (pci_read_config(dev, 0xdf, 1) & 0x40) {
	/* If this was not claimed by AHCI, then we are on the PATA part */
	ctlr->ch_attach = ata_jmicron_ch_attach;
	ctlr->ch_detach = ata_pci_ch_detach;
	ctlr->reset = ata_generic_reset;
	ctlr->setmode = ata_jmicron_setmode;
	ctlr->channels = ctlr->chip->cfg2;
    }
    else {
	/* set controller configuration to a combined setup we support */
	pci_write_config(dev, 0x40, 0x80c0a131, 4);
	pci_write_config(dev, 0x80, 0x01200000, 4);
	/* Create AHCI subdevice if AHCI part present. */
	if (ctlr->chip->cfg1) {
	    	child = device_add_child(dev, NULL, -1);
		if (child != NULL) {
		    device_set_ivars(child, (void *)(intptr_t)-1);
		    bus_generic_attach(dev);
		}
	}
	ctlr->ch_attach = ata_jmicron_ch_attach;
	ctlr->ch_detach = ata_pci_ch_detach;
	ctlr->reset = ata_generic_reset;
	ctlr->setmode = ata_jmicron_setmode;
	ctlr->channels = ctlr->chip->cfg2;
    }
    return 0;
}

static int
ata_jmicron_ch_attach(device_t dev)
{
	struct ata_channel *ch = device_get_softc(dev);
	int error;
 
	error = ata_pci_ch_attach(dev);
	ch->flags |= ATA_CHECKS_CABLE;
	return (error);
}

static int
ata_jmicron_setmode(device_t dev, int target, int mode)
{
	device_t parent = device_get_parent(dev);
	struct ata_pci_controller *ctlr = device_get_softc(parent);

	mode = min(mode, ctlr->chip->max_dma);
	/* check for 80pin cable present */
	if (ata_dma_check_80pin && mode > ATA_UDMA2 &&
	    pci_read_config(parent, 0x40, 1) & 0x08) {
		ata_print_cable(dev, "controller");
		mode = ATA_UDMA2;
	}
	/* Nothing to do to setup mode, the controller snoop SET_FEATURE cmd. */
	return (mode);
}

ATA_DECLARE_DRIVER(ata_jmicron);
