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
static int ata_ite_chipinit(device_t dev);
static int ata_ite_ch_attach(device_t dev);
static int ata_ite_821x_setmode(device_t dev, int target, int mode);
static int ata_ite_8213_setmode(device_t dev, int target, int mode);

/*
 * Integrated Technology Express Inc. (ITE) chipset support functions
 */
static int
ata_ite_probe(device_t dev)
{
    struct ata_pci_controller *ctlr = device_get_softc(dev);
    static const struct ata_chip_id ids[] =
    {{ ATA_IT8213F, 0x00, 0x00, 0x00, ATA_UDMA6, "IT8213F" },
     { ATA_IT8212F, 0x00, 0x00, 0x00, ATA_UDMA6, "IT8212F" },
     { ATA_IT8211F, 0x00, 0x00, 0x00, ATA_UDMA6, "IT8211F" },
     { 0, 0, 0, 0, 0, 0}};

    if (pci_get_vendor(dev) != ATA_ITE_ID)
	return ENXIO;

    if (!(ctlr->chip = ata_match_chip(dev, ids)))
	return ENXIO;

    ata_set_desc(dev);
    ctlr->chipinit = ata_ite_chipinit;
    return (BUS_PROBE_LOW_PRIORITY);
}

static int
ata_ite_chipinit(device_t dev)
{
    struct ata_pci_controller *ctlr = device_get_softc(dev);

    if (ata_setup_interrupt(dev, ata_generic_intr))
	return ENXIO;

    if (ctlr->chip->chipid == ATA_IT8213F) {
	/* the ITE 8213F only has one channel */
	ctlr->channels = 1;

	ctlr->setmode = ata_ite_8213_setmode;
    }
    else {
	/* set PCI mode and 66Mhz reference clock */
	pci_write_config(dev, 0x50, pci_read_config(dev, 0x50, 1) & ~0x83, 1);

	/* set default active & recover timings */
	pci_write_config(dev, 0x54, 0x31, 1);
	pci_write_config(dev, 0x56, 0x31, 1);

	ctlr->setmode = ata_ite_821x_setmode;
	/* No timing restrictions initially. */
	ctlr->chipset_data = NULL;
    }
    ctlr->ch_attach = ata_ite_ch_attach;
    return (0);
}

static int
ata_ite_ch_attach(device_t dev)
{
	struct ata_channel *ch = device_get_softc(dev);
	int error;
 
	error = ata_pci_ch_attach(dev);
	ch->flags |= ATA_CHECKS_CABLE;
	ch->flags |= ATA_NO_ATAPI_DMA;
	return (error);
}

static int
ata_ite_821x_setmode(device_t dev, int target, int mode)
{
	device_t parent = device_get_parent(dev);
	struct ata_pci_controller *ctlr = device_get_softc(parent);
	struct ata_channel *ch = device_get_softc(dev);
	int devno = (ch->unit << 1) + target;
	int piomode;
	uint8_t *timings = (uint8_t*)(&ctlr->chipset_data);
	static const uint8_t udmatiming[] =
		{ 0x44, 0x42, 0x31, 0x21, 0x11, 0xa2, 0x91 };
	static const uint8_t chtiming[] =
		{ 0xaa, 0xa3, 0xa1, 0x33, 0x31, 0x88, 0x32, 0x31 };

	mode = min(mode, ctlr->chip->max_dma);
	/* check the CBLID bits for 80 conductor cable detection */
	if (ata_dma_check_80pin && mode > ATA_UDMA2 &&
	    (pci_read_config(parent, 0x40, 2) &
			     (ch->unit ? (1<<3) : (1<<2)))) {
		ata_print_cable(dev, "controller");
		mode = ATA_UDMA2;
	}
	if (mode >= ATA_UDMA0) {
		/* enable UDMA mode */
		pci_write_config(parent, 0x50,
			     pci_read_config(parent, 0x50, 1) &
			     ~(1 << (devno + 3)), 1);
		/* set UDMA timing */
		pci_write_config(parent,
			     0x56 + (ch->unit << 2) + target,
			     udmatiming[mode & ATA_MODE_MASK], 1);
		piomode = ATA_PIO4;
	} else {
		/* disable UDMA mode */
		pci_write_config(parent, 0x50,
			     pci_read_config(parent, 0x50, 1) |
			     (1 << (devno + 3)), 1);
		piomode = mode;
	}
	timings[devno] = chtiming[ata_mode2idx(piomode)];
	/* set active and recover timing (shared between master & slave) */
	pci_write_config(parent, 0x54 + (ch->unit << 2),
	    max(timings[ch->unit << 1], timings[(ch->unit << 1) + 1]), 1);
	return (mode);
}

static int
ata_ite_8213_setmode(device_t dev, int target, int mode)
{
	device_t parent = device_get_parent(dev);
	struct ata_pci_controller *ctlr = device_get_softc(parent);
	int piomode;
	u_int16_t reg40 = pci_read_config(parent, 0x40, 2);
	u_int8_t reg44 = pci_read_config(parent, 0x44, 1);
	u_int8_t reg48 = pci_read_config(parent, 0x48, 1);
	u_int16_t reg4a = pci_read_config(parent, 0x4a, 2);
	u_int16_t reg54 = pci_read_config(parent, 0x54, 2);
	u_int16_t mask40 = 0, new40 = 0;
	u_int8_t mask44 = 0, new44 = 0;
	static const uint8_t timings[] =
	    { 0x00, 0x00, 0x10, 0x21, 0x23, 0x00, 0x21, 0x23 };
	static const uint8_t utimings[] =
	    { 0x00, 0x01, 0x02, 0x01, 0x02, 0x01, 0x02 };

	mode = min(mode, ctlr->chip->max_dma);

	if (ata_dma_check_80pin && mode > ATA_UDMA2 &&
	    !(reg54 & (0x10 << target))) {
		ata_print_cable(dev, "controller");
		mode = ATA_UDMA2;
	}
	/* Enable/disable UDMA and set timings. */
	if (mode >= ATA_UDMA0) {
	    pci_write_config(parent, 0x48, reg48 | (0x0001 << target), 2);
	    pci_write_config(parent, 0x4a,
			     (reg4a & ~(0x3 << (target << 2))) |
			     (utimings[mode & ATA_MODE_MASK] << (target<<2)), 2);
	    piomode = ATA_PIO4;
	} else {
	    pci_write_config(parent, 0x48, reg48 & ~(0x0001 << target), 2);
	    pci_write_config(parent, 0x4a, (reg4a & ~(0x3 << (target << 2))),2);
	    piomode = mode;
	}
	/* Set UDMA reference clock (33/66/133MHz). */
	reg54 &= ~(0x1001 << target);
	if (mode >= ATA_UDMA5)
	    reg54 |= (0x1000 << target);
	else if (mode >= ATA_UDMA3)
	    reg54 |= (0x1 << target);
	pci_write_config(parent, 0x54, reg54, 2);
	/* Allow PIO/WDMA timing controls. */
	reg40 &= 0xff00;
	reg40 |= 0x4033;
	/* Set PIO/WDMA timings. */
	if (target == 0) {
	    reg40 |= (ata_atapi(dev, target) ? 0x04 : 0x00);
	    mask40 = 0x3300;
	    new40 = timings[ata_mode2idx(piomode)] << 8;
	}
	else {
	    reg40 |= (ata_atapi(dev, target) ? 0x40 : 0x00);
	    mask44 = 0x0f;
	    new44 = ((timings[ata_mode2idx(piomode)] & 0x30) >> 2) |
		    (timings[ata_mode2idx(piomode)] & 0x03);
	}
	pci_write_config(parent, 0x40, (reg40 & ~mask40) | new40, 4);
	pci_write_config(parent, 0x44, (reg44 & ~mask44) | new44, 1);
	return (mode);
}

ATA_DECLARE_DRIVER(ata_ite);
