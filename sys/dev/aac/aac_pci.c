/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2000 Michael Smith
 * Copyright (c) 2001 Scott Long
 * Copyright (c) 2000 BSDi
 * Copyright (c) 2001 Adaptec, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * PCI bus interface and resource allocation.
 */

#include "opt_aac.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>

#include <sys/bio.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/disk.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/rman.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <dev/aac/aacreg.h>
#include <sys/aac_ioctl.h>
#include <dev/aac/aacvar.h>

static int	aac_pci_probe(device_t dev);
static int	aac_pci_attach(device_t dev);

static int aac_enable_msi = 1;
SYSCTL_INT(_hw_aac, OID_AUTO, enable_msi, CTLFLAG_RDTUN, &aac_enable_msi, 0,
    "Enable MSI interrupts");

static device_method_t aac_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		aac_pci_probe),
	DEVMETHOD(device_attach,	aac_pci_attach),
	DEVMETHOD(device_detach,	aac_detach),
	DEVMETHOD(device_suspend,	aac_suspend),
	DEVMETHOD(device_resume,	aac_resume),

	DEVMETHOD_END
};

static driver_t aac_pci_driver = {
	"aac",
	aac_methods,
	sizeof(struct aac_softc)
};

static devclass_t	aac_devclass;

DRIVER_MODULE(aac, pci, aac_pci_driver, aac_devclass, NULL, NULL);
MODULE_DEPEND(aac, pci, 1, 1, 1);

static const struct aac_ident
{
	u_int16_t		vendor;
	u_int16_t		device;
	u_int16_t		subvendor;
	u_int16_t		subdevice;
	int			hwif;
	int			quirks;
	const char		*desc;
} aac_identifiers[] = {
	{0x1028, 0x0001, 0x1028, 0x0001, AAC_HWIF_I960RX, 0,
	"Dell PERC 2/Si"},
	{0x1028, 0x0002, 0x1028, 0x0002, AAC_HWIF_I960RX, 0,
	"Dell PERC 3/Di"},
	{0x1028, 0x0003, 0x1028, 0x0003, AAC_HWIF_I960RX, 0,
	"Dell PERC 3/Si"},
	{0x1028, 0x0004, 0x1028, 0x00d0, AAC_HWIF_I960RX, 0,
	"Dell PERC 3/Si"},
	{0x1028, 0x0002, 0x1028, 0x00d1, AAC_HWIF_I960RX, 0,
	"Dell PERC 3/Di"},
	{0x1028, 0x0002, 0x1028, 0x00d9, AAC_HWIF_I960RX, 0,
	"Dell PERC 3/Di"},
	{0x1028, 0x000a, 0x1028, 0x0106, AAC_HWIF_I960RX, 0,
	"Dell PERC 3/Di"},
	{0x1028, 0x000a, 0x1028, 0x011b, AAC_HWIF_I960RX, 0,
	"Dell PERC 3/Di"},
	{0x1028, 0x000a, 0x1028, 0x0121, AAC_HWIF_I960RX, 0,
	"Dell PERC 3/Di"},
	{0x1011, 0x0046, 0x9005, 0x0364, AAC_HWIF_STRONGARM, 0,
	"Adaptec AAC-364"},
	{0x1011, 0x0046, 0x9005, 0x0365, AAC_HWIF_STRONGARM,
	 AAC_FLAGS_BROKEN_MEMMAP, "Adaptec SCSI RAID 5400S"},
	{0x1011, 0x0046, 0x9005, 0x1364, AAC_HWIF_STRONGARM, AAC_FLAGS_PERC2QC,
	 "Dell PERC 2/QC"},
	{0x1011, 0x0046, 0x103c, 0x10c2, AAC_HWIF_STRONGARM, 0,
	 "HP NetRaid-4M"},
	{0x9005, 0x0285, 0x9005, 0x0285, AAC_HWIF_I960RX, AAC_FLAGS_NO4GB |
	 AAC_FLAGS_256FIBS, "Adaptec SCSI RAID 2200S"},
	{0x9005, 0x0285, 0x1028, 0x0287, AAC_HWIF_I960RX, AAC_FLAGS_NO4GB |
	 AAC_FLAGS_256FIBS, "Dell PERC 320/DC"},
	{0x9005, 0x0285, 0x9005, 0x0286, AAC_HWIF_I960RX, AAC_FLAGS_NO4GB |
	 AAC_FLAGS_256FIBS, "Adaptec SCSI RAID 2120S"},
	{0x9005, 0x0285, 0x9005, 0x0290, AAC_HWIF_I960RX, AAC_FLAGS_NO4GB,
	 "Adaptec SATA RAID 2410SA"},
	{0x9005, 0x0285, 0x1028, 0x0291, AAC_HWIF_I960RX, AAC_FLAGS_NO4GB,
	 "Dell CERC SATA RAID 2"},
	{0x9005, 0x0285, 0x9005, 0x0292, AAC_HWIF_I960RX, AAC_FLAGS_NO4GB,
	 "Adaptec SATA RAID 2810SA"},
	{0x9005, 0x0285, 0x9005, 0x0293, AAC_HWIF_I960RX, AAC_FLAGS_NO4GB,
	 "Adaptec SATA RAID 21610SA"},
	{0x9005, 0x0285, 0x103c, 0x3227, AAC_HWIF_I960RX, AAC_FLAGS_NO4GB,
	 "HP ML110 G2 (Adaptec 2610SA)"},
	{0x9005, 0x0286, 0x9005, 0x028c, AAC_HWIF_RKT, AAC_FLAGS_NOMSI,
	 "Adaptec SCSI RAID 2230S"},
	{0x9005, 0x0286, 0x9005, 0x028d, AAC_HWIF_RKT, 0,
	 "Adaptec SCSI RAID 2130S"},
	{0x9005, 0x0285, 0x9005, 0x0287, AAC_HWIF_I960RX, AAC_FLAGS_NO4GB |
	 AAC_FLAGS_256FIBS, "Adaptec SCSI RAID 2200S"},
	{0x9005, 0x0285, 0x17aa, 0x0286, AAC_HWIF_I960RX, AAC_FLAGS_NO4GB |
	 AAC_FLAGS_256FIBS, "Legend S220"},
	{0x9005, 0x0285, 0x17aa, 0x0287, AAC_HWIF_I960RX, AAC_FLAGS_NO4GB |
	 AAC_FLAGS_256FIBS, "Legend S230"},
	{0x9005, 0x0285, 0x9005, 0x0288, AAC_HWIF_I960RX, 0,
	 "Adaptec SCSI RAID 3230S"},
	{0x9005, 0x0285, 0x9005, 0x0289, AAC_HWIF_I960RX, 0,
	 "Adaptec SCSI RAID 3240S"},
	{0x9005, 0x0285, 0x9005, 0x028a, AAC_HWIF_I960RX, 0,
	 "Adaptec SCSI RAID 2020ZCR"},
	{0x9005, 0x0285, 0x9005, 0x028b, AAC_HWIF_I960RX, 0,
	 "Adaptec SCSI RAID 2025ZCR"},
	{0x9005, 0x0286, 0x9005, 0x029b, AAC_HWIF_RKT, AAC_FLAGS_NOMSI,
	 "Adaptec SATA RAID 2820SA"},
	{0x9005, 0x0286, 0x9005, 0x029c, AAC_HWIF_RKT, 0,
	 "Adaptec SATA RAID 2620SA"},
	{0x9005, 0x0286, 0x9005, 0x029d, AAC_HWIF_RKT, 0,
	 "Adaptec SATA RAID 2420SA"},
	{0x9005, 0x0286, 0x9005, 0x029e, AAC_HWIF_RKT, 0,
	 "ICP ICP9024RO SCSI RAID"},
	{0x9005, 0x0286, 0x9005, 0x029f, AAC_HWIF_RKT, 0,
	 "ICP ICP9014RO SCSI RAID"},
	{0x9005, 0x0285, 0x9005, 0x0294, AAC_HWIF_I960RX, 0,
	 "Adaptec SATA RAID 2026ZCR"},
	{0x9005, 0x0285, 0x9005, 0x0296, AAC_HWIF_I960RX, 0,
	 "Adaptec SCSI RAID 2240S"},
	{0x9005, 0x0285, 0x9005, 0x0297, AAC_HWIF_I960RX, 0,
	 "Adaptec SAS RAID 4005SAS"},
	{0x9005, 0x0285, 0x1014, 0x02f2, AAC_HWIF_I960RX, 0,
	 "IBM ServeRAID 8i"},
	{0x9005, 0x0285, 0x1014, 0x0312, AAC_HWIF_I960RX, 0,
	 "IBM ServeRAID 8i"},
	{0x9005, 0x0285, 0x9005, 0x0298, AAC_HWIF_I960RX, 0,
	 "Adaptec RAID 4000"},
	{0x9005, 0x0285, 0x9005, 0x0299, AAC_HWIF_I960RX, 0,
	 "Adaptec SAS RAID 4800SAS"},
	{0x9005, 0x0285, 0x9005, 0x029a, AAC_HWIF_I960RX, 0,
	 "Adaptec SAS RAID 4805SAS"},
	{0x9005, 0x0285, 0x9005, 0x028e, AAC_HWIF_I960RX, 0,
	 "Adaptec SATA RAID 2020SA ZCR"},
	{0x9005, 0x0285, 0x9005, 0x028f, AAC_HWIF_I960RX, 0,
	 "Adaptec SATA RAID 2025SA ZCR"},
	{0x9005, 0x0285, 0x9005, 0x02a4, AAC_HWIF_I960RX, 0,
	 "ICP ICP9085LI SAS RAID"},
	{0x9005, 0x0285, 0x9005, 0x02a5, AAC_HWIF_I960RX, 0,
	 "ICP ICP5085BR SAS RAID"},
	{0x9005, 0x0286, 0x9005, 0x02a0, AAC_HWIF_RKT, 0,
	 "ICP ICP9047MA SATA RAID"},
	{0x9005, 0x0286, 0x9005, 0x02a1, AAC_HWIF_RKT, 0,
	 "ICP ICP9087MA SATA RAID"},
	{0x9005, 0x0286, 0x9005, 0x02a6, AAC_HWIF_RKT, 0,
	 "ICP9067MA SATA RAID"},
	{0x9005, 0x0285, 0x9005, 0x02b5, AAC_HWIF_I960RX, 0,
	 "Adaptec RAID 5445"},
	{0x9005, 0x0285, 0x9005, 0x02b6, AAC_HWIF_I960RX, 0,
	 "Adaptec RAID 5805"},
	{0x9005, 0x0285, 0x9005, 0x02b7, AAC_HWIF_I960RX, 0,
	 "Adaptec RAID 5085"},
	{0x9005, 0x0285, 0x9005, 0x02b8, AAC_HWIF_I960RX, 0,
	 "ICP RAID ICP5445SL"},
	{0x9005, 0x0285, 0x9005, 0x02b9, AAC_HWIF_I960RX, 0,
	 "ICP RAID ICP5085SL"},
	{0x9005, 0x0285, 0x9005, 0x02ba, AAC_HWIF_I960RX, 0,
	 "ICP RAID ICP5805SL"},
	{0x9005, 0x0285, 0x9005, 0x02bb, AAC_HWIF_I960RX, 0,
	 "Adaptec RAID 3405"},
	{0x9005, 0x0285, 0x9005, 0x02bc, AAC_HWIF_I960RX, 0,
	 "Adaptec RAID 3805"},
	{0x9005, 0x0285, 0x9005, 0x02bd, AAC_HWIF_I960RX, 0,
	 "Adaptec RAID 31205"},
	{0x9005, 0x0285, 0x9005, 0x02be, AAC_HWIF_I960RX, 0,
	 "Adaptec RAID 31605"},
	{0x9005, 0x0285, 0x9005, 0x02bf, AAC_HWIF_I960RX, 0,
	 "ICP RAID ICP5045BL"},
	{0x9005, 0x0285, 0x9005, 0x02c0, AAC_HWIF_I960RX, 0,
	 "ICP RAID ICP5085BL"},
	{0x9005, 0x0285, 0x9005, 0x02c1, AAC_HWIF_I960RX, 0,
	 "ICP RAID ICP5125BR"},
	{0x9005, 0x0285, 0x9005, 0x02c2, AAC_HWIF_I960RX, 0,
	 "ICP RAID ICP5165BR"},
	{0x9005, 0x0285, 0x9005, 0x02c3, AAC_HWIF_I960RX, 0,
	 "Adaptec RAID 51205"},
	{0x9005, 0x0285, 0x9005, 0x02c4, AAC_HWIF_I960RX, 0,
	 "Adaptec RAID 51605"},
	{0x9005, 0x0285, 0x9005, 0x02c5, AAC_HWIF_I960RX, 0,
	 "ICP RAID ICP5125SL"},
	{0x9005, 0x0285, 0x9005, 0x02c6, AAC_HWIF_I960RX, 0,
	 "ICP RAID ICP5165SL"},
	{0x9005, 0x0285, 0x9005, 0x02c7, AAC_HWIF_I960RX, 0,
	 "Adaptec RAID 3085"},
	{0x9005, 0x0285, 0x9005, 0x02c8, AAC_HWIF_I960RX, 0,
	 "ICP RAID ICP5805BL"},
	{0x9005, 0x0285, 0x9005, 0x02ce, AAC_HWIF_I960RX, 0,
	 "Adaptec RAID 51245"},
	{0x9005, 0x0285, 0x9005, 0x02cf, AAC_HWIF_I960RX, 0,
	 "Adaptec RAID 51645"},
	{0x9005, 0x0285, 0x9005, 0x02d0, AAC_HWIF_I960RX, 0,
	 "Adaptec RAID 52445"},
	{0x9005, 0x0285, 0x9005, 0x02d1, AAC_HWIF_I960RX, 0,
	 "Adaptec RAID 5405"},
	{0x9005, 0x0285, 0x9005, 0x02d4, AAC_HWIF_I960RX, 0,
	 "Adaptec RAID 2045"},
	{0x9005, 0x0285, 0x9005, 0x02d5, AAC_HWIF_I960RX, 0,
	 "Adaptec RAID 2405"},
	{0x9005, 0x0285, 0x9005, 0x02d6, AAC_HWIF_I960RX, 0,
	 "Adaptec RAID 2445"},
	{0x9005, 0x0285, 0x9005, 0x02d7, AAC_HWIF_I960RX, 0,
	 "Adaptec RAID 2805"},
	{0x9005, 0x0286, 0x1014, 0x9580, AAC_HWIF_RKT, 0,
	 "IBM ServeRAID-8k"},
	{0x9005, 0x0285, 0x1014, 0x034d, AAC_HWIF_I960RX, 0,
	 "IBM ServeRAID 8s"},
	{0x9005, 0x0285, 0x108e, 0x7aac, AAC_HWIF_I960RX, 0,
	 "Sun STK RAID REM"},
	{0x9005, 0x0285, 0x108e, 0x7aae, AAC_HWIF_I960RX, 0,
	 "Sun STK RAID EM"},
	{0x9005, 0x0285, 0x108e, 0x286, AAC_HWIF_I960RX, 0,
	 "SG-XPCIESAS-R-IN"},
	{0x9005, 0x0285, 0x108e, 0x287, AAC_HWIF_I960RX, 0,
	 "SG-XPCIESAS-R-EX"},
	{0x9005, 0x0285, 0x15d9, 0x2b5, AAC_HWIF_I960RX, 0,
	 "AOC-USAS-S4i"},
	{0x9005, 0x0285, 0x15d9, 0x2b6, AAC_HWIF_I960RX, 0,
	 "AOC-USAS-S8i"},
	{0x9005, 0x0285, 0x15d9, 0x2c9, AAC_HWIF_I960RX, 0,
	 "AOC-USAS-S4iR"},
	{0x9005, 0x0285, 0x15d9, 0x2ca, AAC_HWIF_I960RX, 0,
	 "AOC-USAS-S8iR"},
	{0x9005, 0x0285, 0x15d9, 0x2d2, AAC_HWIF_I960RX, 0,
	 "AOC-USAS-S8i-LP"},
	{0x9005, 0x0285, 0x15d9, 0x2d3, AAC_HWIF_I960RX, 0,
	 "AOC-USAS-S8iR-LP"},
	{0, 0, 0, 0, 0, 0, 0}
};

static const struct aac_ident
aac_family_identifiers[] = {
	{0x9005, 0x0285, 0, 0, AAC_HWIF_I960RX, 0,
	 "Adaptec RAID Controller"},
	{0x9005, 0x0286, 0, 0, AAC_HWIF_RKT, 0,
	 "Adaptec RAID Controller"},
	{0, 0, 0, 0, 0, 0, 0}
};

static const struct aac_ident *
aac_find_ident(device_t dev)
{
	const struct aac_ident *m;
	u_int16_t vendid, devid, sub_vendid, sub_devid;

	vendid = pci_get_vendor(dev);
	devid = pci_get_device(dev);
	sub_vendid = pci_get_subvendor(dev);
	sub_devid = pci_get_subdevice(dev);

	for (m = aac_identifiers; m->vendor != 0; m++) {
		if ((m->vendor == vendid) && (m->device == devid) &&
		    (m->subvendor == sub_vendid) &&
		    (m->subdevice == sub_devid))
			return (m);
	}

	for (m = aac_family_identifiers; m->vendor != 0; m++) {
		if ((m->vendor == vendid) && (m->device == devid))
			return (m);
	}
	return (NULL);
}

/*
 * Determine whether this is one of our supported adapters.
 */
static int
aac_pci_probe(device_t dev)
{
	const struct aac_ident *id;

	fwprintf(NULL, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");

	if ((id = aac_find_ident(dev)) != NULL) {
		device_set_desc(dev, id->desc);
		return(BUS_PROBE_DEFAULT);
	}
	return(ENXIO);
}

/*
 * Allocate resources for our device, set up the bus interface.
 */
static int
aac_pci_attach(device_t dev)
{
	struct aac_softc *sc;
	const struct aac_ident *id;
	int count, error, rid;

	fwprintf(NULL, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");

	/*
	 * Initialise softc.
	 */
	sc = device_get_softc(dev);
	sc->aac_dev = dev;

	/* assume failure is 'not configured' */
	error = ENXIO;

	/*
	 * Verify that the adapter is correctly set up in PCI space.
	 */
	pci_enable_busmaster(dev);
	if (!(pci_read_config(dev, PCIR_COMMAND, 2) & PCIM_CMD_BUSMASTEREN)) {
		device_printf(dev, "can't enable bus-master feature\n");
		goto out;
	}

	/*
	 * Detect the hardware interface version, set up the bus interface
	 * indirection.
	 */
	id = aac_find_ident(dev);
	sc->aac_hwif = id->hwif;
	switch(sc->aac_hwif) {
	case AAC_HWIF_I960RX:
	case AAC_HWIF_NARK:
		fwprintf(sc, HBA_FLAGS_DBG_INIT_B,
		    "set hardware up for i960Rx/NARK");
		sc->aac_if = &aac_rx_interface;
		break;
	case AAC_HWIF_STRONGARM:
		fwprintf(sc, HBA_FLAGS_DBG_INIT_B,
		    "set hardware up for StrongARM");
		sc->aac_if = &aac_sa_interface;
		break;
	case AAC_HWIF_RKT:
		fwprintf(sc, HBA_FLAGS_DBG_INIT_B,
		    "set hardware up for Rocket/MIPS");
		sc->aac_if = &aac_rkt_interface;
		break;
	default:
		sc->aac_hwif = AAC_HWIF_UNKNOWN;
		device_printf(dev, "unknown hardware type\n");
		goto out;
	}

	/* Set up quirks */
	sc->flags = id->quirks;

	/*
	 * Allocate the PCI register window(s).
	 */
	rid = PCIR_BAR(0);
	if ((sc->aac_regs_res0 = bus_alloc_resource_any(dev,
	    SYS_RES_MEMORY, &rid, RF_ACTIVE)) == NULL) {
		device_printf(dev, "can't allocate register window 0\n");
		goto out;
	}
	sc->aac_btag0 = rman_get_bustag(sc->aac_regs_res0);
	sc->aac_bhandle0 = rman_get_bushandle(sc->aac_regs_res0);

	if (sc->aac_hwif == AAC_HWIF_NARK) {
		rid = PCIR_BAR(1);
		if ((sc->aac_regs_res1 = bus_alloc_resource_any(dev,
		    SYS_RES_MEMORY, &rid, RF_ACTIVE)) == NULL) {
			device_printf(dev,
			    "can't allocate register window 1\n");
			goto out;
		}
		sc->aac_btag1 = rman_get_bustag(sc->aac_regs_res1);
		sc->aac_bhandle1 = rman_get_bushandle(sc->aac_regs_res1);
	} else {
		sc->aac_regs_res1 = sc->aac_regs_res0;
		sc->aac_btag1 = sc->aac_btag0;
		sc->aac_bhandle1 = sc->aac_bhandle0;
	}

	/*
	 * Allocate the interrupt.
	 */
	rid = 0;
	if (aac_enable_msi != 0 && (sc->flags & AAC_FLAGS_NOMSI) == 0) {
		count = 1;
		if (pci_alloc_msi(dev, &count) == 0)
			rid = 1;
	}
	if ((sc->aac_irq = bus_alloc_resource_any(sc->aac_dev, SYS_RES_IRQ,
	    &rid, RF_ACTIVE | (rid != 0 ? 0 : RF_SHAREABLE))) == NULL) {
		device_printf(dev, "can't allocate interrupt\n");
		goto out;
	}

	/*
	 * Allocate the parent bus DMA tag appropriate for our PCI interface.
	 *
	 * Note that some of these controllers are 64-bit capable.
	 */
	if (bus_dma_tag_create(bus_get_dma_tag(dev),	/* parent */
			       PAGE_SIZE, 0,		/* algnmnt, boundary */
			       BUS_SPACE_MAXADDR,	/* lowaddr */
			       BUS_SPACE_MAXADDR, 	/* highaddr */
			       NULL, NULL, 		/* filter, filterarg */
			       BUS_SPACE_MAXSIZE_32BIT,	/* maxsize */
			       BUS_SPACE_UNRESTRICTED,	/* nsegments */
			       BUS_SPACE_MAXSIZE_32BIT,	/* maxsegsize */
			       0,			/* flags */
			       NULL, NULL,		/* No locking needed */
			       &sc->aac_parent_dmat)) {
		device_printf(dev, "can't allocate parent DMA tag\n");
		goto out;
	}

	/*
	 * Do bus-independent initialisation.
	 */
	error = aac_attach(sc);

out:
	if (error)
		aac_free(sc);
	return(error);
}

/*
 * Do nothing driver that will attach to the SCSI channels of a Dell PERC
 * controller.  This is needed to keep the power management subsystem from
 * trying to power down these devices.
 */
static int aacch_probe(device_t dev);
static int aacch_attach(device_t dev);
static int aacch_detach(device_t dev);

static device_method_t aacch_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		aacch_probe),
	DEVMETHOD(device_attach,	aacch_attach),
	DEVMETHOD(device_detach,	aacch_detach),
	DEVMETHOD_END
};

static driver_t aacch_driver = {
	"aacch",
	aacch_methods,
	1	/* no softc */
};

static devclass_t	aacch_devclass;
DRIVER_MODULE(aacch, pci, aacch_driver, aacch_devclass, NULL, NULL);
MODULE_PNP_INFO("U16:vendor;U16:device;", pci, aac,
    aac_identifiers, nitems(aac_identifiers) - 1);

static int
aacch_probe(device_t dev)
{

	if ((pci_get_vendor(dev) != 0x9005) ||
	    (pci_get_device(dev) != 0x00c5))
		return (ENXIO);

	device_set_desc(dev, "AAC RAID Channel");
	return (-10);
}

static int
aacch_attach(device_t dev __unused)
{

	return (0);
}

static int
aacch_detach(device_t dev __unused)
{

	return (0);
}
