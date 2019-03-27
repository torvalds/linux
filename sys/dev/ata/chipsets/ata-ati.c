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
static int ata_ati_chipinit(device_t dev);
static int ata_ati_dumb_ch_attach(device_t dev);
static int ata_ati_ixp700_ch_attach(device_t dev);
static int ata_ati_setmode(device_t dev, int target, int mode);

/* misc defines */
#define SII_MEMIO       1	/* must match ata_siliconimage.c's definition */
#define SII_BUG         0x04	/* must match ata_siliconimage.c's definition */

#define ATI_SATA	SII_MEMIO
#define ATI_PATA	0x02
#define ATI_AHCI	0x04

/*
 * ATI chipset support functions
 */
static int
ata_ati_probe(device_t dev)
{
    struct ata_pci_controller *ctlr = device_get_softc(dev);
    static const struct ata_chip_id ids[] =
    {{ ATA_ATI_IXP200,    0x00, ATI_PATA, 0, ATA_UDMA5, "IXP200" },
     { ATA_ATI_IXP300,    0x00, ATI_PATA, 0, ATA_UDMA6, "IXP300" },
     { ATA_ATI_IXP300_S1, 0x00, ATI_SATA, SII_BUG, ATA_SA150, "IXP300" },
     { ATA_ATI_IXP400,    0x00, ATI_PATA, 0, ATA_UDMA6, "IXP400" },
     { ATA_ATI_IXP400_S1, 0x00, ATI_SATA, SII_BUG, ATA_SA150, "IXP400" },
     { ATA_ATI_IXP400_S2, 0x00, ATI_SATA, SII_BUG, ATA_SA150, "IXP400" },
     { ATA_ATI_IXP600,    0x00, ATI_PATA, 0, ATA_UDMA6, "IXP600" },
     { ATA_ATI_IXP600_S1, 0x00, ATI_AHCI, 0, ATA_SA300, "IXP600" },
     { ATA_ATI_IXP700,    0x00, ATI_PATA, 0, ATA_UDMA6, "IXP700/800" },
     { ATA_ATI_IXP700_S1, 0x00, ATI_AHCI, 0, ATA_SA300, "IXP700/800" },
     { ATA_ATI_IXP700_S2, 0x00, ATI_AHCI, 0, ATA_SA300, "IXP700/800" },
     { ATA_ATI_IXP700_S3, 0x00, ATI_AHCI, 0, ATA_SA300, "IXP700/800" },
     { ATA_ATI_IXP700_S4, 0x00, ATI_AHCI, 0, ATA_SA300, "IXP700/800" },
     { ATA_ATI_IXP800_S1, 0x00, ATI_AHCI, 0, ATA_SA300, "IXP800" },
     { ATA_ATI_IXP800_S2, 0x00, ATI_AHCI, 0, ATA_SA300, "IXP800" },
     { ATA_AMD_HUDSON2,     0x00, ATI_PATA, 0, ATA_UDMA6, "Hudson-2" },
     { ATA_AMD_HUDSON2_S1,  0x00, ATI_AHCI, 0, ATA_SA300, "Hudson-2" },
     { ATA_AMD_HUDSON2_S2,  0x00, ATI_AHCI, 0, ATA_SA300, "Hudson-2" },
     { ATA_AMD_HUDSON2_S3,  0x00, ATI_AHCI, 0, ATA_SA300, "Hudson-2" },
     { ATA_AMD_HUDSON2_S4,  0x00, ATI_AHCI, 0, ATA_SA300, "Hudson-2" },
     { ATA_AMD_HUDSON2_S5,  0x00, ATI_AHCI, 0, ATA_SA300, "Hudson-2" },
     { 0, 0, 0, 0, 0, 0}};

    if (pci_get_vendor(dev) != ATA_AMD_ID && pci_get_vendor(dev) != ATA_ATI_ID)
	return ENXIO;

    if (!(ctlr->chip = ata_match_chip(dev, ids)))
	return ENXIO;

    switch (ctlr->chip->cfg1) {
    case ATI_PATA:
	ctlr->chipinit = ata_ati_chipinit;
	break;
    case ATI_SATA:
	/*
	 * the ATI SATA controller is actually a SiI 3112 controller
	 */
	ctlr->chipinit = ata_sii_chipinit;
	break;
    case ATI_AHCI:
	if (pci_get_subclass(dev) != PCIS_STORAGE_IDE)
		return (ENXIO);
	ctlr->chipinit = ata_ati_chipinit;
	break;
    }

    ata_set_desc(dev);
    return (BUS_PROBE_LOW_PRIORITY);
}

static int
ata_ati_chipinit(device_t dev)
{
    struct ata_pci_controller *ctlr = device_get_softc(dev);
    device_t smbdev;
    uint8_t satacfg;

    if (ata_setup_interrupt(dev, ata_generic_intr))
	return ENXIO;

    if (ctlr->chip->cfg1 == ATI_AHCI) {
	ctlr->ch_attach = ata_ati_dumb_ch_attach;
	ctlr->setmode = ata_sata_setmode;
	return (0);
    }
    switch (ctlr->chip->chipid) {
    case ATA_ATI_IXP600:
	/* IXP600 only has 1 PATA channel */
	ctlr->channels = 1;
	break;
    case ATA_ATI_IXP700:
	/*
	 * When "combined mode" is enabled, an additional PATA channel is
	 * emulated with two SATA ports and appears on this device.
	 * This mode can only be detected via SMB controller.
	 */
	smbdev = pci_find_device(ATA_ATI_ID, 0x4385);
	if (smbdev != NULL) {
	    satacfg = pci_read_config(smbdev, 0xad, 1);
	    if (bootverbose)
		device_printf(dev, "SATA controller %s (%s%s channel)\n",
		    (satacfg & 0x01) == 0 ? "disabled" : "enabled",
		    (satacfg & 0x08) == 0 ? "" : "combined mode, ",
		    (satacfg & 0x10) == 0 ? "primary" : "secondary");
	    ctlr->chipset_data = (void *)(uintptr_t)satacfg;
	    /*
	     * If SATA controller is enabled but combined mode is disabled,
	     * we have only one PATA channel.  Ignore a non-existent channel.
	     */
	    if ((satacfg & 0x09) == 0x01)
		ctlr->ichannels &= ~(1 << ((satacfg & 0x10) >> 4));
	    else {
	        ctlr->ch_attach = ata_ati_ixp700_ch_attach;
	    }
	}
	break;
    }

    ctlr->setmode = ata_ati_setmode;
    return 0;
}

static int
ata_ati_dumb_ch_attach(device_t dev)
{
	struct ata_channel *ch = device_get_softc(dev);

	if (ata_pci_ch_attach(dev))
		return ENXIO;
	ch->flags |= ATA_SATA;
	return (0);
}

static int
ata_ati_ixp700_ch_attach(device_t dev)
{
	struct ata_pci_controller *ctlr = device_get_softc(device_get_parent(dev));
	struct ata_channel *ch = device_get_softc(dev);
	uint8_t satacfg = (uint8_t)(uintptr_t)ctlr->chipset_data;

	/* Setup the usual register normal pci style. */
	if (ata_pci_ch_attach(dev))
		return ENXIO;

	/* One of channels is PATA, another is SATA. */
	if (ch->unit == ((satacfg & 0x10) >> 4))
		ch->flags |= ATA_SATA;
	return (0);
}

static int
ata_ati_setmode(device_t dev, int target, int mode)
{
	device_t parent = device_get_parent(dev);
	struct ata_pci_controller *ctlr = device_get_softc(parent);
	struct ata_channel *ch = device_get_softc(dev);
	int devno = (ch->unit << 1) + target;
	int offset = (devno ^ 0x01) << 3;
	int piomode;
	static const uint8_t piotimings[] = { 0x5d, 0x47, 0x34, 0x22, 0x20 };
	static const uint8_t dmatimings[] = { 0x77, 0x21, 0x20 };

	mode = min(mode, ctlr->chip->max_dma);
	if (mode >= ATA_UDMA0) {
	    /* Set UDMA mode, enable UDMA, set WDMA2/PIO4 */
	    pci_write_config(parent, 0x56, 
			     (pci_read_config(parent, 0x56, 2) &
			      ~(0xf << (devno << 2))) |
			     ((mode & ATA_MODE_MASK) << (devno << 2)), 2);
	    pci_write_config(parent, 0x54,
			     pci_read_config(parent, 0x54, 1) |
			     (0x01 << devno), 1);
	    pci_write_config(parent, 0x44, 
			     (pci_read_config(parent, 0x44, 4) &
			      ~(0xff << offset)) |
			     (dmatimings[2] << offset), 4);
	    piomode = ATA_PIO4;
	} else if (mode >= ATA_WDMA0) {
	    /* Disable UDMA, set WDMA mode and timings, calculate PIO. */
	    pci_write_config(parent, 0x54,
			     pci_read_config(parent, 0x54, 1) &
			      ~(0x01 << devno), 1);
	    pci_write_config(parent, 0x44, 
			     (pci_read_config(parent, 0x44, 4) &
			      ~(0xff << offset)) |
			     (dmatimings[mode & ATA_MODE_MASK] << offset), 4);
	    piomode = (mode == ATA_WDMA0) ? ATA_PIO0 :
		(mode == ATA_WDMA1) ? ATA_PIO3 : ATA_PIO4;
	} else {
	    /* Disable UDMA, set requested PIO. */
	    pci_write_config(parent, 0x54,
			     pci_read_config(parent, 0x54, 1) &
			     ~(0x01 << devno), 1);
	    piomode = mode;
	}
	/* Set PIO mode and timings, calculated above. */
	pci_write_config(parent, 0x4a,
			 (pci_read_config(parent, 0x4a, 2) &
			  ~(0xf << (devno << 2))) |
			 ((piomode - ATA_PIO0) << (devno<<2)),2);
	pci_write_config(parent, 0x40, 
			 (pci_read_config(parent, 0x40, 4) &
			  ~(0xff << offset)) |
			 (piotimings[ata_mode2idx(piomode)] << offset), 4);
	return (mode);
}

ATA_DECLARE_DRIVER(ata_ati);
MODULE_DEPEND(ata_ati, ata_sii, 1, 1, 1);
