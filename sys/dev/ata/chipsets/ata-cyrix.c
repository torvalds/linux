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
static int ata_cyrix_chipinit(device_t dev);
static int ata_cyrix_ch_attach(device_t dev);
static int ata_cyrix_setmode(device_t dev, int target, int mode);

/*
 * Cyrix chipset support functions
 */
static int
ata_cyrix_probe(device_t dev)
{
    struct ata_pci_controller *ctlr = device_get_softc(dev);

    if (pci_get_devid(dev) == ATA_CYRIX_5530) {
	device_set_desc(dev, "Cyrix 5530 ATA33 controller");
	ctlr->chipinit = ata_cyrix_chipinit;
	return (BUS_PROBE_LOW_PRIORITY);
    }
    return ENXIO;
}

static int
ata_cyrix_chipinit(device_t dev)
{
    struct ata_pci_controller *ctlr = device_get_softc(dev);

    if (ata_setup_interrupt(dev, ata_generic_intr))
	return ENXIO;
    ctlr->ch_attach = ata_cyrix_ch_attach;
    ctlr->setmode = ata_cyrix_setmode;
    return 0;
}

static int
ata_cyrix_ch_attach(device_t dev)
{
	struct ata_channel *ch = device_get_softc(dev);
 
	ch->dma.alignment = 16;
	ch->dma.max_iosize = 64 * DEV_BSIZE;
	return (ata_pci_ch_attach(dev));
}

static int
ata_cyrix_setmode(device_t dev, int target, int mode)
{
	struct ata_pci_controller *ctlr = device_get_softc(device_get_parent(dev));
	struct ata_channel *ch = device_get_softc(dev);
	int devno = (ch->unit << 1) + target;
	int piomode;
	static const uint32_t piotiming[] = 
	    { 0x00009172, 0x00012171, 0x00020080, 0x00032010, 0x00040010 };
	static const uint32_t dmatiming[] =
	    { 0x00077771, 0x00012121, 0x00002020 };
	static const uint32_t udmatiming[] =
	    { 0x00921250, 0x00911140, 0x00911030 };

	mode = min(mode, ATA_UDMA2);
	/* dont try to set the mode if we dont have the resource */
	if (ctlr->r_res1) {
		if (mode >= ATA_UDMA0) {
			/* Set UDMA timings, and PIO4. */
			ATA_OUTL(ch->r_io[ATA_BMCMD_PORT].res,
			    0x24 + (devno << 3), udmatiming[mode & ATA_MODE_MASK]);
			piomode = ATA_PIO4;
		} else if (mode >= ATA_WDMA0) {
			/* Set WDMA timings, and respective PIO mode. */
			ATA_OUTL(ch->r_io[ATA_BMCMD_PORT].res,
			    0x24 + (devno << 3), dmatiming[mode & ATA_MODE_MASK]);
		        piomode = (mode == ATA_WDMA0) ? ATA_PIO0 :
			    (mode == ATA_WDMA1) ? ATA_PIO3 : ATA_PIO4;
		} else
			piomode = mode;
		/* Set PIO mode calculated above. */
		ATA_OUTL(ch->r_io[ATA_BMCMD_PORT].res,
		    0x20 + (devno << 3), piotiming[ata_mode2idx(piomode)]);
	}
	return (mode);
}

ATA_DECLARE_DRIVER(ata_cyrix);
