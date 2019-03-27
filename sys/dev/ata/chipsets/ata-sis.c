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
static int ata_sis_chipinit(device_t dev);
static int ata_sis_ch_attach(device_t dev);
static void ata_sis_reset(device_t dev);
static int ata_sis_setmode(device_t dev, int target, int mode);

/* misc defines */
#define SIS_33		1
#define SIS_66		2
#define SIS_100NEW	3
#define SIS_100OLD	4
#define SIS_133NEW	5
#define SIS_133OLD	6
#define SIS_SATA	7

/*
 * Silicon Integrated Systems Corp. (SiS) chipset support functions
 */
static int
ata_sis_probe(device_t dev)
{
    struct ata_pci_controller *ctlr = device_get_softc(dev);
    const struct ata_chip_id *idx;
    static const struct ata_chip_id ids[] =
    {{ ATA_SIS182,  0x00, SIS_SATA,   0, ATA_SA150, "182" }, /* south */
     { ATA_SIS181,  0x00, SIS_SATA,   0, ATA_SA150, "181" }, /* south */
     { ATA_SIS180,  0x00, SIS_SATA,   0, ATA_SA150, "180" }, /* south */
     { ATA_SIS965,  0x00, SIS_133NEW, 0, ATA_UDMA6, "965" }, /* south */
     { ATA_SIS964,  0x00, SIS_133NEW, 0, ATA_UDMA6, "964" }, /* south */
     { ATA_SIS963,  0x00, SIS_133NEW, 0, ATA_UDMA6, "963" }, /* south */
     { ATA_SIS962,  0x00, SIS_133NEW, 0, ATA_UDMA6, "962" }, /* south */

     { ATA_SIS745,  0x00, SIS_100NEW, 0, ATA_UDMA5, "745" }, /* 1chip */
     { ATA_SIS735,  0x00, SIS_100NEW, 0, ATA_UDMA5, "735" }, /* 1chip */
     { ATA_SIS733,  0x00, SIS_100NEW, 0, ATA_UDMA5, "733" }, /* 1chip */
     { ATA_SIS730,  0x00, SIS_100OLD, 0, ATA_UDMA5, "730" }, /* 1chip */

     { ATA_SIS635,  0x00, SIS_100NEW, 0, ATA_UDMA5, "635" }, /* 1chip */
     { ATA_SIS633,  0x00, SIS_100NEW, 0, ATA_UDMA5, "633" }, /* unknown */
     { ATA_SIS630,  0x30, SIS_100OLD, 0, ATA_UDMA5, "630S"}, /* 1chip */
     { ATA_SIS630,  0x00, SIS_66,     0, ATA_UDMA4, "630" }, /* 1chip */
     { ATA_SIS620,  0x00, SIS_66,     0, ATA_UDMA4, "620" }, /* 1chip */

     { ATA_SIS550,  0x00, SIS_66,     0, ATA_UDMA5, "550" },
     { ATA_SIS540,  0x00, SIS_66,     0, ATA_UDMA4, "540" },
     { ATA_SIS530,  0x00, SIS_66,     0, ATA_UDMA4, "530" },

     { ATA_SIS5513, 0xc2, SIS_33,     1, ATA_UDMA2, "5513" },
     { ATA_SIS5513, 0x00, SIS_33,     1, ATA_WDMA2, "5513" },
     { 0, 0, 0, 0, 0, 0 }};
    static struct ata_chip_id id[] =
    {{ ATA_SISSOUTH, 0x10, 0, 0, 0, "" }, { 0, 0, 0, 0, 0, 0 }};
    char buffer[64];
    int found = 0;

    if (pci_get_class(dev) != PCIC_STORAGE)
	return (ENXIO);

    if (pci_get_vendor(dev) != ATA_SIS_ID)
	return ENXIO;

    if (!(idx = ata_find_chip(dev, ids, -pci_get_slot(dev)))) 
	return ENXIO;

    if (idx->cfg2) {
	u_int8_t reg57 = pci_read_config(dev, 0x57, 1);

	pci_write_config(dev, 0x57, (reg57 & 0x7f), 1);
	if (pci_read_config(dev, PCIR_DEVVENDOR, 4) == ATA_SIS5518) {
	    found = 1;
    	    memcpy(&id[0], idx, sizeof(id[0]));
	    id[0].cfg1 = SIS_133NEW;
	    id[0].max_dma = ATA_UDMA6;
	    sprintf(buffer, "SiS 962/963 %s controller",
		    ata_mode2str(idx->max_dma));
	}
	pci_write_config(dev, 0x57, reg57, 1);
    }
    if (idx->cfg2 && !found) {
	u_int8_t reg4a = pci_read_config(dev, 0x4a, 1);

	pci_write_config(dev, 0x4a, (reg4a | 0x10), 1);
	if (pci_read_config(dev, PCIR_DEVVENDOR, 4) == ATA_SIS5517) {
	    found = 1;
	    if (ata_find_chip(dev, id, pci_get_slot(dev))) {
		id[0].cfg1 = SIS_133OLD;
		id[0].max_dma = ATA_UDMA6;
	    } else {
		id[0].cfg1 = SIS_100NEW;
		id[0].max_dma = ATA_UDMA5;
	    }
	    sprintf(buffer, "SiS 961 %s controller",ata_mode2str(idx->max_dma));
	}
	pci_write_config(dev, 0x4a, reg4a, 1);
    }
    if (!found)
	sprintf(buffer,"SiS %s %s controller",
		idx->text, ata_mode2str(idx->max_dma));
    else
	idx = &id[0];

    device_set_desc_copy(dev, buffer);
    ctlr->chip = idx;
    ctlr->chipinit = ata_sis_chipinit;
    return (BUS_PROBE_LOW_PRIORITY);
}

static int
ata_sis_chipinit(device_t dev)
{
    struct ata_pci_controller *ctlr = device_get_softc(dev);

    if (ata_setup_interrupt(dev, ata_generic_intr))
	return ENXIO;
    
    switch (ctlr->chip->cfg1) {
    case SIS_33:
	break;
    case SIS_66:
    case SIS_100OLD:
	pci_write_config(dev, 0x52, pci_read_config(dev, 0x52, 1) & ~0x04, 1);
	break;
    case SIS_100NEW:
    case SIS_133OLD:
	pci_write_config(dev, 0x49, pci_read_config(dev, 0x49, 1) & ~0x01, 1);
	break;
    case SIS_133NEW:
	pci_write_config(dev, 0x50, pci_read_config(dev, 0x50, 2) | 0x0008, 2);
	pci_write_config(dev, 0x52, pci_read_config(dev, 0x52, 2) | 0x0008, 2);
	break;
    case SIS_SATA:
	ctlr->r_type2 = SYS_RES_IOPORT;
	ctlr->r_rid2 = PCIR_BAR(5);
	if ((ctlr->r_res2 = bus_alloc_resource_any(dev, ctlr->r_type2,
						   &ctlr->r_rid2, RF_ACTIVE))) {
	    ctlr->ch_attach = ata_sis_ch_attach;
	    ctlr->ch_detach = ata_pci_ch_detach;
	    ctlr->reset = ata_sis_reset;
	}
	ctlr->setmode = ata_sata_setmode;
	ctlr->getrev = ata_sata_getrev;
	return 0;
    default:
	return ENXIO;
    }
    ctlr->setmode = ata_sis_setmode;
    return 0;
}

static int
ata_sis_ch_attach(device_t dev)
{
    struct ata_pci_controller *ctlr = device_get_softc(device_get_parent(dev));
    struct ata_channel *ch = device_get_softc(dev);
    int offset = ch->unit << ((ctlr->chip->chipid == ATA_SIS182) ? 5 : 6);

    /* setup the usual register normal pci style */
    if (ata_pci_ch_attach(dev))
	return ENXIO;

    ch->r_io[ATA_SSTATUS].res = ctlr->r_res2;
    ch->r_io[ATA_SSTATUS].offset = 0x00 + offset;
    ch->r_io[ATA_SERROR].res = ctlr->r_res2;
    ch->r_io[ATA_SERROR].offset = 0x04 + offset;
    ch->r_io[ATA_SCONTROL].res = ctlr->r_res2;
    ch->r_io[ATA_SCONTROL].offset = 0x08 + offset;
    ch->flags |= ATA_NO_SLAVE;
    ch->flags |= ATA_SATA;

    /* XXX SOS PHY hotplug handling missing in SiS chip ?? */
    /* XXX SOS unknown how to enable PHY state change interrupt */
    return 0;
}

static void
ata_sis_reset(device_t dev)
{
    struct ata_channel *ch = device_get_softc(dev);

    if (ata_sata_phy_reset(dev, -1, 1))
	ata_generic_reset(dev);
    else
	ch->devices = 0;
}

static int
ata_sis_setmode(device_t dev, int target, int mode)
{
	device_t parent = device_get_parent(dev);
	struct ata_pci_controller *ctlr = device_get_softc(parent);
	struct ata_channel *ch = device_get_softc(dev);
	int devno = (ch->unit << 1) + target;

	mode = min(mode, ctlr->chip->max_dma);

	if (ctlr->chip->cfg1 == SIS_133NEW) {
		if (ata_dma_check_80pin && mode > ATA_UDMA2 &&
		        pci_read_config(parent, ch->unit ? 0x52 : 0x50,2) & 0x8000) {
		        ata_print_cable(dev, "controller");
		        mode = ATA_UDMA2;
		}
	} else {
		if (ata_dma_check_80pin && mode > ATA_UDMA2 &&
		    pci_read_config(parent, 0x48, 1)&(ch->unit ? 0x20 : 0x10)) {
		    ata_print_cable(dev, "controller");
		    mode = ATA_UDMA2;
		}
        }

	switch (ctlr->chip->cfg1) {
	case SIS_133NEW: {
	    static const uint32_t timings[] = 
		{ 0x28269008, 0x0c266008, 0x04263008, 0x0c0a3008, 0x05093008,
		  0x22196008, 0x0c0a3008, 0x05093008, 0x050939fc, 0x050936ac,
		  0x0509347c, 0x0509325c, 0x0509323c, 0x0509322c, 0x0509321c};
	    u_int32_t reg;

	    reg = (pci_read_config(parent, 0x57, 1)&0x40?0x70:0x40)+(devno<<2);
	    pci_write_config(parent, reg, timings[ata_mode2idx(mode)], 4);
	    break;
	    }
	case SIS_133OLD: {
	    static const uint16_t timings[] =
	     { 0x00cb, 0x0067, 0x0044, 0x0033, 0x0031, 0x0044, 0x0033, 0x0031,
	       0x8f31, 0x8a31, 0x8731, 0x8531, 0x8331, 0x8231, 0x8131 };
		  
	    u_int16_t reg = 0x40 + (devno << 1);

	    pci_write_config(parent, reg, timings[ata_mode2idx(mode)], 2);
	    break;
	    }
	case SIS_100NEW: {
	    static const uint16_t timings[] =
		{ 0x00cb, 0x0067, 0x0044, 0x0033, 0x0031, 0x0044, 0x0033,
		  0x0031, 0x8b31, 0x8731, 0x8531, 0x8431, 0x8231, 0x8131 };
	    u_int16_t reg = 0x40 + (devno << 1);

	    pci_write_config(parent, reg, timings[ata_mode2idx(mode)], 2);
	    break;
	    }
	case SIS_100OLD:
	case SIS_66:
	case SIS_33: {
	    static const uint16_t timings[] =
		{ 0x0c0b, 0x0607, 0x0404, 0x0303, 0x0301, 0x0404, 0x0303,
		  0x0301, 0xf301, 0xd301, 0xb301, 0xa301, 0x9301, 0x8301 };
	    u_int16_t reg = 0x40 + (devno << 1);

	    pci_write_config(parent, reg, timings[ata_mode2idx(mode)], 2);
	    break;
	    }
	}
	return (mode);
}

ATA_DECLARE_DRIVER(ata_sis);
