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
static int ata_amd_ch_attach(device_t dev);
static int ata_amd_chipinit(device_t dev);
static int ata_amd_setmode(device_t dev, int target, int mode);

/* misc defines */
#define AMD_BUG		0x01
#define AMD_CABLE	0x02

/*
 * Advanced Micro Devices (AMD) chipset support functions
 */
static int
ata_amd_probe(device_t dev)
{
    struct ata_pci_controller *ctlr = device_get_softc(dev);
    static const struct ata_chip_id ids[] =
    {{ ATA_AMD756,  0x00, 0x00,              0, ATA_UDMA4, "756" },
     { ATA_AMD766,  0x00, AMD_CABLE|AMD_BUG, 0, ATA_UDMA5, "766" },
     { ATA_AMD768,  0x00, AMD_CABLE,         0, ATA_UDMA5, "768" },
     { ATA_AMD8111, 0x00, AMD_CABLE,         0, ATA_UDMA6, "8111" },
     { ATA_AMD5536, 0x00, 0x00,              0, ATA_UDMA5, "CS5536" },
     { 0, 0, 0, 0, 0, 0}};

    if (pci_get_vendor(dev) != ATA_AMD_ID)
	return ENXIO;

    if (!(ctlr->chip = ata_match_chip(dev, ids)))
	return ENXIO;

    ata_set_desc(dev);
    ctlr->chipinit = ata_amd_chipinit;
    return (BUS_PROBE_LOW_PRIORITY);
}

static int
ata_amd_chipinit(device_t dev)
{
    struct ata_pci_controller *ctlr = device_get_softc(dev);

    if (ata_setup_interrupt(dev, ata_generic_intr))
	return ENXIO;

    /* disable/set prefetch, postwrite */
    if (ctlr->chip->cfg1 & AMD_BUG)
	pci_write_config(dev, 0x41, pci_read_config(dev, 0x41, 1) & 0x0f, 1);
    else
	pci_write_config(dev, 0x41, pci_read_config(dev, 0x41, 1) | 0xf0, 1);

    ctlr->ch_attach = ata_amd_ch_attach;
    ctlr->setmode = ata_amd_setmode;
    return 0;
}

static int
ata_amd_setmode(device_t dev, int target, int mode)
{
	device_t parent = device_get_parent(dev);
	struct ata_pci_controller *ctlr = device_get_softc(parent);
	struct ata_channel *ch = device_get_softc(dev);
        int devno = (ch->unit << 1) + target;
	int piomode;
	static const uint8_t timings[] =
	    { 0xa8, 0x65, 0x42, 0x22, 0x20, 0xa8, 0x22, 0x20 };
	static const uint8_t modes[] =
	    { 0xc2, 0xc1, 0xc0, 0xc4, 0xc5, 0xc6, 0xc7 };
	int reg = 0x53 - devno;

	mode = min(mode, ctlr->chip->max_dma);
	if (ctlr->chip->cfg1 & AMD_CABLE) {
		if (ata_dma_check_80pin && mode > ATA_UDMA2 &&
		    !(pci_read_config(parent, 0x42, 1) & (1 << devno))) {
			ata_print_cable(dev, "controller");
			mode = ATA_UDMA2;
		}
	}
	/* Set UDMA timings. */
	if (mode >= ATA_UDMA0) {
	    pci_write_config(parent, reg, modes[mode & ATA_MODE_MASK], 1);
	    piomode = ATA_PIO4;
	} else {
	    pci_write_config(parent, reg, 0x8b, 1);
	    piomode = mode;
	}
	/* Set WDMA/PIO timings. */
	pci_write_config(parent, reg - 0x08, timings[ata_mode2idx(piomode)], 1);
	return (mode);
}

static int
ata_amd_ch_attach(device_t dev)
{
	struct ata_pci_controller *ctlr;
	struct ata_channel *ch;
	int error;

	ctlr = device_get_softc(device_get_parent(dev));
	ch = device_get_softc(dev);
	error = ata_pci_ch_attach(dev);
	if (ctlr->chip->cfg1 & AMD_CABLE)
		ch->flags |= ATA_CHECKS_CABLE;
	return (error);
}

ATA_DECLARE_DRIVER(ata_amd);
