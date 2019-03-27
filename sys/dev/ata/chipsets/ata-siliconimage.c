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
static int ata_cmd_ch_attach(device_t dev);
static int ata_cmd_status(device_t dev);
static int ata_cmd_setmode(device_t dev, int target, int mode);
static int ata_sii_ch_attach(device_t dev);
static int ata_sii_ch_detach(device_t dev);
static int ata_sii_status(device_t dev);
static void ata_sii_reset(device_t dev);
static int ata_sii_setmode(device_t dev, int target, int mode);

/* misc defines */
#define SII_MEMIO	1
#define SII_INTR	0x01
#define SII_SETCLK	0x02
#define SII_BUG		0x04
#define SII_4CH		0x08

/*
 * Silicon Image Inc. (SiI) (former CMD) chipset support functions
 */
static int
ata_sii_probe(device_t dev)
{
    struct ata_pci_controller *ctlr = device_get_softc(dev);
    static const struct ata_chip_id ids[] =
    {{ ATA_SII3114,   0x00, SII_MEMIO, SII_4CH,    ATA_SA150, "3114" },
     { ATA_SII3512,   0x02, SII_MEMIO, 0,          ATA_SA150, "3512" },
     { ATA_SII3112,   0x02, SII_MEMIO, 0,          ATA_SA150, "3112" },
     { ATA_SII3112_1, 0x02, SII_MEMIO, 0,          ATA_SA150, "3112" },
     { ATA_SII3512,   0x00, SII_MEMIO, SII_BUG,    ATA_SA150, "3512" },
     { ATA_SII3112,   0x00, SII_MEMIO, SII_BUG,    ATA_SA150, "3112" },
     { ATA_SII3112_1, 0x00, SII_MEMIO, SII_BUG,    ATA_SA150, "3112" },
     { ATA_SII0680,   0x00, SII_MEMIO, SII_SETCLK, ATA_UDMA6, "680" },
     { ATA_CMD649,    0x00, 0,         SII_INTR,   ATA_UDMA5, "(CMD) 649" },
     { ATA_CMD648,    0x00, 0,         SII_INTR,   ATA_UDMA4, "(CMD) 648" },
     { ATA_CMD646,    0x07, 0,         0,          ATA_UDMA2, "(CMD) 646U2" },
     { ATA_CMD646,    0x00, 0,         0,          ATA_WDMA2, "(CMD) 646" },
     { 0, 0, 0, 0, 0, 0}};

    if (pci_get_vendor(dev) != ATA_SILICON_IMAGE_ID)
	return ENXIO;

    if (!(ctlr->chip = ata_match_chip(dev, ids)))
	return ENXIO;

    ata_set_desc(dev);
    ctlr->chipinit = ata_sii_chipinit;
    return (BUS_PROBE_LOW_PRIORITY);
}

int
ata_sii_chipinit(device_t dev)
{
    struct ata_pci_controller *ctlr = device_get_softc(dev);

    if (ata_setup_interrupt(dev, ata_generic_intr))
	return ENXIO;

    switch (ctlr->chip->cfg1) {
    case SII_MEMIO:
	ctlr->r_type2 = SYS_RES_MEMORY;
	ctlr->r_rid2 = PCIR_BAR(5);
	if (!(ctlr->r_res2 = bus_alloc_resource_any(dev, ctlr->r_type2,
						    &ctlr->r_rid2, RF_ACTIVE))){
	    if (ctlr->chip->chipid != ATA_SII0680 ||
			    (pci_read_config(dev, 0x8a, 1) & 1))
		return ENXIO;
	}

	if (ctlr->chip->cfg2 & SII_SETCLK) {
	    if ((pci_read_config(dev, 0x8a, 1) & 0x30) != 0x10)
		pci_write_config(dev, 0x8a,
				 (pci_read_config(dev, 0x8a, 1) & 0xcf)|0x10,1);
	    if ((pci_read_config(dev, 0x8a, 1) & 0x30) != 0x10)
		device_printf(dev, "%s could not set ATA133 clock\n",
			      ctlr->chip->text);
	}

	/* if we have 4 channels enable the second set */
	if (ctlr->chip->cfg2 & SII_4CH) {
	    ATA_OUTL(ctlr->r_res2, 0x0200, 0x00000002);
	    ctlr->channels = 4;
	}

	/* dont block interrupts from any channel */
	pci_write_config(dev, 0x48,
			 (pci_read_config(dev, 0x48, 4) & ~0x03c00000), 4);

	/* enable PCI interrupt as BIOS might not */
	pci_write_config(dev, 0x8a, (pci_read_config(dev, 0x8a, 1) & 0x3f), 1);

	if (ctlr->r_res2) {
	    ctlr->ch_attach = ata_sii_ch_attach;
	    ctlr->ch_detach = ata_sii_ch_detach;
	}

	if (ctlr->chip->max_dma >= ATA_SA150) {
	    ctlr->reset = ata_sii_reset;
	    ctlr->setmode = ata_sata_setmode;
	    ctlr->getrev = ata_sata_getrev;
	}
	else
	    ctlr->setmode = ata_sii_setmode;
	break;
    
    default:
	if ((pci_read_config(dev, 0x51, 1) & 0x08) != 0x08) {
	    device_printf(dev, "HW has secondary channel disabled\n");
	    ctlr->channels = 1;
	}    

	/* enable interrupt as BIOS might not */
	pci_write_config(dev, 0x71, 0x01, 1);

	ctlr->ch_attach = ata_cmd_ch_attach;
	ctlr->ch_detach = ata_pci_ch_detach;
	ctlr->setmode = ata_cmd_setmode;
	break;
    }
    return 0;
}

static int
ata_cmd_ch_attach(device_t dev)
{
    struct ata_pci_controller *ctlr = device_get_softc(device_get_parent(dev));
    struct ata_channel *ch = device_get_softc(dev);

    /* setup the usual register normal pci style */
    if (ata_pci_ch_attach(dev))
	return ENXIO;

    if (ctlr->chip->cfg2 & SII_INTR)
	ch->hw.status = ata_cmd_status;

    ch->flags |= ATA_NO_ATAPI_DMA;

    return 0;
}

static int
ata_cmd_status(device_t dev)
{
    struct ata_channel *ch = device_get_softc(dev);
    u_int8_t reg71;

    if (((reg71 = pci_read_config(device_get_parent(dev), 0x71, 1)) &
	 (ch->unit ? 0x08 : 0x04))) {
	pci_write_config(device_get_parent(dev), 0x71,
			 reg71 & ~(ch->unit ? 0x04 : 0x08), 1);
	return ata_pci_status(dev);
    }
    return 0;
}

static int
ata_cmd_setmode(device_t dev, int target, int mode)
{
	device_t parent = device_get_parent(dev);
	struct ata_pci_controller *ctlr = device_get_softc(parent);
	struct ata_channel *ch = device_get_softc(dev);
	int devno = (ch->unit << 1) + target;
	int treg = 0x54 + ((devno < 3) ? (devno << 1) : 7);
	int ureg = ch->unit ? 0x7b : 0x73;
	int piomode;
	static const uint8_t piotimings[] =
	    { 0xa9, 0x57, 0x44, 0x32, 0x3f, 0x87, 0x32, 0x3f };
	static const uint8_t udmatimings[][2] =
	    { { 0x31,  0xc2 }, { 0x21,  0x82 }, { 0x11,  0x42 },
	      { 0x25,  0x8a }, { 0x15,  0x4a }, { 0x05,  0x0a } };

	mode = min(mode, ctlr->chip->max_dma);
	if (mode >= ATA_UDMA0) {        
		u_int8_t umode = pci_read_config(parent, ureg, 1);

	        umode &= ~(target == 0 ? 0x35 : 0xca);
		umode |= udmatimings[mode & ATA_MODE_MASK][target];
		pci_write_config(parent, ureg, umode, 1);
		piomode = ATA_PIO4;
	} else { 
		pci_write_config(parent, ureg, 
			     pci_read_config(parent, ureg, 1) &
			     ~(target == 0 ? 0x35 : 0xca), 1);
		piomode = mode;
	}
	pci_write_config(parent, treg, piotimings[ata_mode2idx(piomode)], 1);
	return (mode);
}

static int
ata_sii_ch_attach(device_t dev)
{
    struct ata_pci_controller *ctlr = device_get_softc(device_get_parent(dev));
    struct ata_channel *ch = device_get_softc(dev);
    int unit01 = (ch->unit & 1), unit10 = (ch->unit & 2);
    int i;

    for (i = ATA_DATA; i <= ATA_COMMAND; i++) {
	ch->r_io[i].res = ctlr->r_res2;
	ch->r_io[i].offset = 0x80 + i + (unit01 << 6) + (unit10 << 8);
    }
    ch->r_io[ATA_CONTROL].res = ctlr->r_res2;
    ch->r_io[ATA_CONTROL].offset = 0x8a + (unit01 << 6) + (unit10 << 8);
    ch->r_io[ATA_IDX_ADDR].res = ctlr->r_res2;
    ata_default_registers(dev);

    ch->r_io[ATA_BMCMD_PORT].res = ctlr->r_res2;
    ch->r_io[ATA_BMCMD_PORT].offset = 0x00 + (unit01 << 3) + (unit10 << 8);
    ch->r_io[ATA_BMSTAT_PORT].res = ctlr->r_res2;
    ch->r_io[ATA_BMSTAT_PORT].offset = 0x02 + (unit01 << 3) + (unit10 << 8);
    ch->r_io[ATA_BMDTP_PORT].res = ctlr->r_res2;
    ch->r_io[ATA_BMDTP_PORT].offset = 0x04 + (unit01 << 3) + (unit10 << 8);

    if (ctlr->chip->max_dma >= ATA_SA150) {
	ch->r_io[ATA_SSTATUS].res = ctlr->r_res2;
	ch->r_io[ATA_SSTATUS].offset = 0x104 + (unit01 << 7) + (unit10 << 8);
	ch->r_io[ATA_SERROR].res = ctlr->r_res2;
	ch->r_io[ATA_SERROR].offset = 0x108 + (unit01 << 7) + (unit10 << 8);
	ch->r_io[ATA_SCONTROL].res = ctlr->r_res2;
	ch->r_io[ATA_SCONTROL].offset = 0x100 + (unit01 << 7) + (unit10 << 8);
	ch->flags |= ATA_NO_SLAVE;
	ch->flags |= ATA_SATA;
	ch->flags |= ATA_KNOWN_PRESENCE;

	/* enable PHY state change interrupt */
	ATA_OUTL(ctlr->r_res2, 0x148 + (unit01 << 7) + (unit10 << 8),(1 << 16));
    }

    if (ctlr->chip->cfg2 & SII_BUG) {
	/* work around errata in early chips */
	ch->dma.boundary = 8192;
	ch->dma.segsize = 15 * DEV_BSIZE;
    }

    ata_pci_hw(dev);
    ch->hw.status = ata_sii_status;
    if (ctlr->chip->cfg2 & SII_SETCLK)
	ch->flags |= ATA_CHECKS_CABLE;

    ata_pci_dmainit(dev);

    return 0;
}

static int
ata_sii_ch_detach(device_t dev)
{

    ata_pci_dmafini(dev);
    return (0);
}

static int
ata_sii_status(device_t dev)
{
    struct ata_pci_controller *ctlr = device_get_softc(device_get_parent(dev));
    struct ata_channel *ch = device_get_softc(dev);
    int offset0 = ((ch->unit & 1) << 3) + ((ch->unit & 2) << 8);
    int offset1 = ((ch->unit & 1) << 6) + ((ch->unit & 2) << 8);

    /* do we have any PHY events ? */
    if (ctlr->chip->max_dma >= ATA_SA150 &&
	(ATA_INL(ctlr->r_res2, 0x10 + offset0) & 0x00000010))
	ata_sata_phy_check_events(dev, -1);

    if (ATA_INL(ctlr->r_res2, 0xa0 + offset1) & 0x00000800)
	return ata_pci_status(dev);
    else
	return 0;
}

static void
ata_sii_reset(device_t dev)
{
    struct ata_pci_controller *ctlr = device_get_softc(device_get_parent(dev));
    struct ata_channel *ch = device_get_softc(dev);
    int offset = ((ch->unit & 1) << 7) + ((ch->unit & 2) << 8);
    uint32_t val;

    /* Apply R_ERR on DMA activate FIS errata workaround. */
    val = ATA_INL(ctlr->r_res2, 0x14c + offset);
    if ((val & 0x3) == 0x1)
	ATA_OUTL(ctlr->r_res2, 0x14c + offset, val & ~0x3);

    if (ata_sata_phy_reset(dev, -1, 1))
	ata_generic_reset(dev);
    else
	ch->devices = 0;
}

static int
ata_sii_setmode(device_t dev, int target, int mode)
{
	device_t parent = device_get_parent(dev);
	struct ata_pci_controller *ctlr = device_get_softc(parent);
	struct ata_channel *ch = device_get_softc(dev);
	int rego = (ch->unit << 4) + (target << 1);
	int mreg = ch->unit ? 0x84 : 0x80;
	int mask = 0x03 << (target << 2);
	int mval = pci_read_config(parent, mreg, 1) & ~mask;
	int piomode;
	u_int8_t preg = 0xa4 + rego;
	u_int8_t dreg = 0xa8 + rego;
	u_int8_t ureg = 0xac + rego;
	static const uint16_t piotimings[] =
	    { 0x328a, 0x2283, 0x1104, 0x10c3, 0x10c1 };
	static const uint16_t dmatimings[] = { 0x2208, 0x10c2, 0x10c1 };
	static const uint8_t udmatimings[] =
	    { 0xf, 0xb, 0x7, 0x5, 0x3, 0x2, 0x1 };

	mode = min(mode, ctlr->chip->max_dma);

	if (ctlr->chip->cfg2 & SII_SETCLK) {
	    if (ata_dma_check_80pin && mode > ATA_UDMA2 &&
		(pci_read_config(parent, 0x79, 1) &
				 (ch->unit ? 0x02 : 0x01))) {
		ata_print_cable(dev, "controller");
		mode = ATA_UDMA2;
	    }
	}
	if (mode >= ATA_UDMA0) {
		pci_write_config(parent, mreg,
			 mval | (0x03 << (target << 2)), 1);
		pci_write_config(parent, ureg, 
			 (pci_read_config(parent, ureg, 1) & ~0x3f) |
			 udmatimings[mode & ATA_MODE_MASK], 1);
		piomode = ATA_PIO4;
	} else if (mode >= ATA_WDMA0) {
		pci_write_config(parent, mreg,
			 mval | (0x02 << (target << 2)), 1);
		pci_write_config(parent, dreg, dmatimings[mode & ATA_MODE_MASK], 2);
		piomode = (mode == ATA_WDMA0) ? ATA_PIO0 :
		    (mode == ATA_WDMA1) ? ATA_PIO3 : ATA_PIO4;
	} else {
		pci_write_config(parent, mreg,
			 mval | (0x01 << (target << 2)), 1);
		piomode = mode;
	}
	pci_write_config(parent, preg, piotimings[ata_mode2idx(piomode)], 2);
	return (mode);
}

ATA_DECLARE_DRIVER(ata_sii);
