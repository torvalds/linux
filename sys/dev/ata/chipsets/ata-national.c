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
static int ata_national_chipinit(device_t dev);
static int ata_national_ch_attach(device_t dev);
static int ata_national_setmode(device_t dev, int target, int mode);

/*
 * National chipset support functions
 */
static int
ata_national_probe(device_t dev)
{
    struct ata_pci_controller *ctlr = device_get_softc(dev);

    /* this chip is a clone of the Cyrix chip, bugs and all */
    if (pci_get_devid(dev) == ATA_SC1100) {
	device_set_desc(dev, "National Geode SC1100 ATA33 controller");
	ctlr->chipinit = ata_national_chipinit;
	return (BUS_PROBE_LOW_PRIORITY);
    }
    return ENXIO;
}
    
static int
ata_national_chipinit(device_t dev)
{
    struct ata_pci_controller *ctlr = device_get_softc(dev);
    
    if (ata_setup_interrupt(dev, ata_generic_intr))
	return ENXIO;
		    
    ctlr->ch_attach = ata_national_ch_attach;
    ctlr->setmode = ata_national_setmode;
    return 0;
}

static int
ata_national_ch_attach(device_t dev)
{
	struct ata_channel *ch = device_get_softc(dev);
 
	ch->dma.alignment = 16;
	ch->dma.max_iosize = 64 * DEV_BSIZE;
	return (ata_pci_ch_attach(dev));
}

static int
ata_national_setmode(device_t dev, int target, int mode)
{
	device_t parent = device_get_parent(dev);
	struct ata_channel *ch = device_get_softc(dev);
	int devno = (ch->unit << 1) + target;
	int piomode;
	static const uint32_t piotiming[] =
	    { 0x9172d132, 0x21717121, 0x00803020, 0x20102010, 0x00100010,
	      0x9172d132, 0x20102010, 0x00100010 };
	static const uint32_t dmatiming[] =
	    { 0x80077771, 0x80012121, 0x80002020 };
	static const uint32_t udmatiming[] =
	    { 0x80921250, 0x80911140, 0x80911030 };

	mode = min(mode, ATA_UDMA2);

	if (mode >= ATA_UDMA0) {
	    pci_write_config(parent, 0x44 + (devno << 3),
			     udmatiming[mode & ATA_MODE_MASK], 4);
	    piomode = ATA_PIO4;
	} else if (mode >= ATA_WDMA0) {
	    pci_write_config(parent, 0x44 + (devno << 3),
			     dmatiming[mode & ATA_MODE_MASK], 4);
	    piomode = mode;
	} else {
	    pci_write_config(parent, 0x44 + (devno << 3),
			     pci_read_config(parent, 0x44 + (devno << 3), 4) |
			     0x80000000, 4);
	    piomode = mode;
	}
	pci_write_config(parent, 0x40 + (devno << 3),
			 piotiming[ata_mode2idx(piomode)], 4);
	return (mode);
}

ATA_DECLARE_DRIVER(ata_national);
