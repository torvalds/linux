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
static int ata_ali_chipinit(device_t dev);
static int ata_ali_chipdeinit(device_t dev);
static int ata_ali_ch_attach(device_t dev);
static int ata_ali_sata_ch_attach(device_t dev);
static void ata_ali_reset(device_t dev);
static int ata_ali_setmode(device_t dev, int target, int mode);

/* misc defines */
#define ALI_OLD		0x01
#define ALI_NEW		0x02
#define ALI_SATA	0x04

struct ali_sata_resources {
	struct resource *bars[4];
};

/*
 * Acer Labs Inc (ALI) chipset support functions
 */
static int
ata_ali_probe(device_t dev)
{
    struct ata_pci_controller *ctlr = device_get_softc(dev);
    static const struct ata_chip_id ids[] =
    {{ ATA_ALI_5289, 0x00, 2, ALI_SATA, ATA_SA150, "M5289" },
     { ATA_ALI_5288, 0x00, 4, ALI_SATA, ATA_SA300, "M5288" },
     { ATA_ALI_5287, 0x00, 4, ALI_SATA, ATA_SA150, "M5287" },
     { ATA_ALI_5281, 0x00, 2, ALI_SATA, ATA_SA150, "M5281" },
     { ATA_ALI_5228, 0xc5, 0, ALI_NEW,  ATA_UDMA6, "M5228" },
     { ATA_ALI_5229, 0xc5, 0, ALI_NEW,  ATA_UDMA6, "M5229" },
     { ATA_ALI_5229, 0xc4, 0, ALI_NEW,  ATA_UDMA5, "M5229" },
     { ATA_ALI_5229, 0xc2, 0, ALI_NEW,  ATA_UDMA4, "M5229" },
     { ATA_ALI_5229, 0x20, 0, ALI_OLD,  ATA_UDMA2, "M5229" },
     { ATA_ALI_5229, 0x00, 0, ALI_OLD,  ATA_WDMA2, "M5229" },
     { 0, 0, 0, 0, 0, 0}};

    if (pci_get_vendor(dev) != ATA_ACER_LABS_ID)
	return ENXIO;

    if (!(ctlr->chip = ata_match_chip(dev, ids)))
	return ENXIO;

    ata_set_desc(dev);
    ctlr->chipinit = ata_ali_chipinit;
    ctlr->chipdeinit = ata_ali_chipdeinit;
    return (BUS_PROBE_LOW_PRIORITY);
}

static int
ata_ali_chipinit(device_t dev)
{
    struct ata_pci_controller *ctlr = device_get_softc(dev);
    struct ali_sata_resources *res;
    int i, rid;

    if (ata_setup_interrupt(dev, ata_generic_intr))
	return ENXIO;

    switch (ctlr->chip->cfg2) {
    case ALI_SATA:
	ctlr->channels = ctlr->chip->cfg1;
	ctlr->ch_attach = ata_ali_sata_ch_attach;
	ctlr->ch_detach = ata_pci_ch_detach;
	ctlr->setmode = ata_sata_setmode;
	ctlr->getrev = ata_sata_getrev;

	/* Allocate resources for later use by channel attach routines. */
	res = malloc(sizeof(struct ali_sata_resources), M_ATAPCI, M_WAITOK);
	for (i = 0; i < 4; i++) {
		rid = PCIR_BAR(i);
		res->bars[i] = bus_alloc_resource_any(dev, SYS_RES_IOPORT, &rid,
		    RF_ACTIVE);
		if (res->bars[i] == NULL) {
			device_printf(dev, "Failed to allocate BAR %d\n", i);
			for (i--; i >=0; i--)
				bus_release_resource(dev, SYS_RES_IOPORT,
				    PCIR_BAR(i), res->bars[i]);
			free(res, M_ATAPCI);
			return ENXIO;
		}
	}
	ctlr->chipset_data = res;
	break;

    case ALI_NEW:
	/* use device interrupt as byte count end */
	pci_write_config(dev, 0x4a, pci_read_config(dev, 0x4a, 1) | 0x20, 1);

	/* enable cable detection and UDMA support on revisions < 0xc7 */
	if (ctlr->chip->chiprev < 0xc7)
	    pci_write_config(dev, 0x4b, pci_read_config(dev, 0x4b, 1) |
		0x09, 1);

	/* enable ATAPI UDMA mode (even if we are going to do PIO) */
	pci_write_config(dev, 0x53, pci_read_config(dev, 0x53, 1) |
	    (ctlr->chip->chiprev >= 0xc7 ? 0x03 : 0x01), 1);

	/* only chips with revision > 0xc4 can do 48bit DMA */
	if (ctlr->chip->chiprev <= 0xc4)
	    device_printf(dev,
			  "using PIO transfers above 137GB as workaround for "
			  "48bit DMA access bug, expect reduced performance\n");
	ctlr->ch_attach = ata_ali_ch_attach;
	ctlr->ch_detach = ata_pci_ch_detach;
	ctlr->reset = ata_ali_reset;
	ctlr->setmode = ata_ali_setmode;
	break;

    case ALI_OLD:
	/* deactivate the ATAPI FIFO and enable ATAPI UDMA */
	pci_write_config(dev, 0x53, pci_read_config(dev, 0x53, 1) | 0x03, 1);
	ctlr->setmode = ata_ali_setmode;
	break;
    }
    return 0;
}

static int
ata_ali_chipdeinit(device_t dev)
{
	struct ata_pci_controller *ctlr = device_get_softc(dev);
	struct ali_sata_resources *res;
	int i;

	if (ctlr->chip->cfg2 == ALI_SATA) {
		res = ctlr->chipset_data;
		for (i = 0; i < 4; i++) {
			if (res->bars[i] != NULL) {
				bus_release_resource(dev, SYS_RES_IOPORT,
				    PCIR_BAR(i), res->bars[i]);
			}
		}
		free(res, M_ATAPCI);
		ctlr->chipset_data = NULL;
	}
	return (0);
}

static int
ata_ali_ch_attach(device_t dev)
{
    struct ata_pci_controller *ctlr = device_get_softc(device_get_parent(dev));
    struct ata_channel *ch = device_get_softc(dev);

    /* setup the usual register normal pci style */
    if (ata_pci_ch_attach(dev))
	return ENXIO;

    if (ctlr->chip->cfg2 & ALI_NEW && ctlr->chip->chiprev < 0xc7)
	ch->flags |= ATA_CHECKS_CABLE;
    /* older chips can't do 48bit DMA transfers */
    if (ctlr->chip->chiprev <= 0xc4) {
	ch->flags |= ATA_NO_48BIT_DMA;
	if (ch->dma.max_iosize > 256 * 512)
		ch->dma.max_iosize = 256 * 512;
    }
	if (ctlr->chip->cfg2 & ALI_NEW)
		ch->flags |= ATA_NO_ATAPI_DMA;

    return 0;
}

static int
ata_ali_sata_ch_attach(device_t dev)
{
    device_t parent = device_get_parent(dev);
    struct ata_pci_controller *ctlr = device_get_softc(parent);
    struct ata_channel *ch = device_get_softc(dev);
    struct ali_sata_resources *res;
    struct resource *io = NULL, *ctlio = NULL;
    int unit01 = (ch->unit & 1), unit10 = (ch->unit & 2);
    int i;

    res = ctlr->chipset_data;
    if (unit01) {
	    io = res->bars[2];
	    ctlio = res->bars[3];
    } else {
	    io = res->bars[0];
	    ctlio = res->bars[1];
    }
    ata_pci_dmainit(dev);
    for (i = ATA_DATA; i <= ATA_COMMAND; i ++) {
	ch->r_io[i].res = io;
	ch->r_io[i].offset = i + (unit10 ? 8 : 0);
    }
    ch->r_io[ATA_CONTROL].res = ctlio;
    ch->r_io[ATA_CONTROL].offset = 2 + (unit10 ? 4 : 0);
    ch->r_io[ATA_IDX_ADDR].res = io;
    ata_default_registers(dev);
    if (ctlr->r_res1) {
	for (i = ATA_BMCMD_PORT; i <= ATA_BMDTP_PORT; i++) {
	    ch->r_io[i].res = ctlr->r_res1;
	    ch->r_io[i].offset = (i - ATA_BMCMD_PORT)+(ch->unit * ATA_BMIOSIZE);
	}
    }
    ch->flags |= ATA_NO_SLAVE;
    ch->flags |= ATA_SATA;

    /* XXX SOS PHY handling awkward in ALI chip not supported yet */
    ata_pci_hw(dev);
    return 0;
}

static void
ata_ali_reset(device_t dev)
{
    struct ata_pci_controller *ctlr = device_get_softc(device_get_parent(dev));
    struct ata_channel *ch = device_get_softc(dev);
    device_t *children;
    int nchildren, i;

    ata_generic_reset(dev);

    /*
     * workaround for datacorruption bug found on at least SUN Blade-100
     * find the ISA function on the southbridge and disable then enable
     * the ATA channel tristate buffer
     */
    if (ctlr->chip->chiprev == 0xc3 || ctlr->chip->chiprev == 0xc2) {
	if (!device_get_children(GRANDPARENT(dev), &children, &nchildren)) {
	    for (i = 0; i < nchildren; i++) {
		if (pci_get_devid(children[i]) == ATA_ALI_1533) {
		    pci_write_config(children[i], 0x58, 
				     pci_read_config(children[i], 0x58, 1) &
				     ~(0x04 << ch->unit), 1);
		    pci_write_config(children[i], 0x58, 
				     pci_read_config(children[i], 0x58, 1) |
				     (0x04 << ch->unit), 1);
		    break;
		}
	    }
	    free(children, M_TEMP);
	}
    }
}

static int
ata_ali_setmode(device_t dev, int target, int mode)
{
	device_t parent = device_get_parent(dev);
	struct ata_pci_controller *ctlr = device_get_softc(parent);
	struct ata_channel *ch = device_get_softc(dev);
	int devno = (ch->unit << 1) + target;
	int piomode;
	static const uint32_t piotimings[] =
		{ 0x006d0003, 0x00580002, 0x00440001, 0x00330001,
		  0x00310001, 0x006d0003, 0x00330001, 0x00310001 };
	static const uint8_t udma[] = {0x0c, 0x0b, 0x0a, 0x09, 0x08, 0x0f,
	    0x0d};
	uint32_t word54;

        mode = min(mode, ctlr->chip->max_dma);

	if (ctlr->chip->cfg2 & ALI_NEW && ctlr->chip->chiprev < 0xc7) {
		if (ata_dma_check_80pin && mode > ATA_UDMA2 &&
		    pci_read_config(parent, 0x4a, 1) & (1 << ch->unit)) {
			ata_print_cable(dev, "controller");
			mode = ATA_UDMA2;
		}
	}
	if (ctlr->chip->cfg2 & ALI_OLD) {
		/* doesn't support ATAPI DMA on write */
		ch->flags |= ATA_ATAPI_DMA_RO;
		if (ch->devices & ATA_ATAPI_MASTER &&
		    ch->devices & ATA_ATAPI_SLAVE) {
		        /* doesn't support ATAPI DMA on two ATAPI devices */
		        device_printf(dev, "two atapi devices on this channel,"
			    " no DMA\n");
		        mode = min(mode, ATA_PIO_MAX);
		}
	}
	/* Set UDMA mode */
	word54 = pci_read_config(parent, 0x54, 4);
	if (mode >= ATA_UDMA0) {
	    word54 &= ~(0x000f000f << (devno << 2));
	    word54 |= (((udma[mode&ATA_MODE_MASK]<<16)|0x05)<<(devno<<2));
	    piomode = ATA_PIO4;
	}
	else {
	    word54 &= ~(0x0008000f << (devno << 2));
	    piomode = mode;
	}
	pci_write_config(parent, 0x54, word54, 4);
	/* Set PIO/WDMA mode */
	pci_write_config(parent, 0x58 + (ch->unit << 2),
	    piotimings[ata_mode2idx(piomode)], 4);
	return (mode);
}

ATA_DECLARE_DRIVER(ata_ali);
