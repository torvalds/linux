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
static int ata_intel_chipinit(device_t dev);
static int ata_intel_chipdeinit(device_t dev);
static int ata_intel_ch_attach(device_t dev);
static void ata_intel_reset(device_t dev);
static int ata_intel_old_setmode(device_t dev, int target, int mode);
static int ata_intel_new_setmode(device_t dev, int target, int mode);
static int ata_intel_sch_setmode(device_t dev, int target, int mode);
static int ata_intel_sata_getrev(device_t dev, int target);
static int ata_intel_sata_status(device_t dev);
static int ata_intel_sata_ahci_read(device_t dev, int port,
    int reg, u_int32_t *result);
static int ata_intel_sata_cscr_read(device_t dev, int port,
    int reg, u_int32_t *result);
static int ata_intel_sata_sidpr_read(device_t dev, int port,
    int reg, u_int32_t *result);
static int ata_intel_sata_ahci_write(device_t dev, int port,
    int reg, u_int32_t result);
static int ata_intel_sata_cscr_write(device_t dev, int port,
    int reg, u_int32_t result);
static int ata_intel_sata_sidpr_write(device_t dev, int port,
    int reg, u_int32_t result);
static int ata_intel_sata_sidpr_test(device_t dev);
static int ata_intel_31244_ch_attach(device_t dev);
static int ata_intel_31244_ch_detach(device_t dev);
static int ata_intel_31244_status(device_t dev);
static void ata_intel_31244_tf_write(struct ata_request *request);
static void ata_intel_31244_reset(device_t dev);

/* misc defines */
#define INTEL_ICH5	2
#define INTEL_6CH	4
#define INTEL_6CH2	8
#define INTEL_ICH7	16

struct ata_intel_data {
	struct mtx	lock;
	u_char		smap[4];
};

#define ATA_INTEL_SMAP(ctlr, ch) \
    &((struct ata_intel_data *)((ctlr)->chipset_data))->smap[(ch)->unit * 2]
#define ATA_INTEL_LOCK(ctlr) \
    mtx_lock(&((struct ata_intel_data *)((ctlr)->chipset_data))->lock)
#define ATA_INTEL_UNLOCK(ctlr) \
    mtx_unlock(&((struct ata_intel_data *)((ctlr)->chipset_data))->lock)

/*
 * Intel chipset support functions
 */
static int
ata_intel_probe(device_t dev)
{
    struct ata_pci_controller *ctlr = device_get_softc(dev);
    static const struct ata_chip_id ids[] =
    {{ ATA_I82371FB,     0,          0, 2, ATA_WDMA2, "PIIX" },
     { ATA_I82371SB,     0,          0, 2, ATA_WDMA2, "PIIX3" },
     { ATA_I82371AB,     0,          0, 2, ATA_UDMA2, "PIIX4" },
     { ATA_I82443MX,     0,          0, 2, ATA_UDMA2, "PIIX4" },
     { ATA_I82451NX,     0,          0, 2, ATA_UDMA2, "PIIX4" },
     { ATA_I82801AB,     0,          0, 2, ATA_UDMA2, "ICH0" },
     { ATA_I82801AA,     0,          0, 2, ATA_UDMA4, "ICH" },
     { ATA_I82372FB,     0,          0, 2, ATA_UDMA4, "ICH" },
     { ATA_I82801BA,     0,          0, 2, ATA_UDMA5, "ICH2" },
     { ATA_I82801BA_1,   0,          0, 2, ATA_UDMA5, "ICH2" },
     { ATA_I82801CA,     0,          0, 2, ATA_UDMA5, "ICH3" },
     { ATA_I82801CA_1,   0,          0, 2, ATA_UDMA5, "ICH3" },
     { ATA_I82801DB,     0,          0, 2, ATA_UDMA5, "ICH4" },
     { ATA_I82801DB_1,   0,          0, 2, ATA_UDMA5, "ICH4" },
     { ATA_I82801EB,     0,          0, 2, ATA_UDMA5, "ICH5" },
     { ATA_I82801EB_S1,  0, INTEL_ICH5, 2, ATA_SA150, "ICH5" },
     { ATA_I82801EB_R1,  0, INTEL_ICH5, 2, ATA_SA150, "ICH5" },
     { ATA_I6300ESB,     0,          0, 2, ATA_UDMA5, "6300ESB" },
     { ATA_I6300ESB_S1,  0, INTEL_ICH5, 2, ATA_SA150, "6300ESB" },
     { ATA_I6300ESB_R1,  0, INTEL_ICH5, 2, ATA_SA150, "6300ESB" },
     { ATA_I82801FB,     0,          0, 2, ATA_UDMA5, "ICH6" },
     { ATA_I82801FB_S1,  0,          0, 0, ATA_SA150, "ICH6" },
     { ATA_I82801FB_R1,  0,          0, 0, ATA_SA150, "ICH6" },
     { ATA_I82801FBM,    0,          0, 0, ATA_SA150, "ICH6M" },
     { ATA_I82801GB,     0,          0, 1, ATA_UDMA5, "ICH7" },
     { ATA_I82801GB_S1,  0, INTEL_ICH7, 0, ATA_SA300, "ICH7" },
     { ATA_I82801GBM_S1, 0, INTEL_ICH7, 0, ATA_SA150, "ICH7M" },
     { ATA_I63XXESB2,    0,          0, 1, ATA_UDMA5, "63XXESB2" },
     { ATA_I63XXESB2_S1, 0,          0, 0, ATA_SA300, "63XXESB2" },
     { ATA_I82801HB_S1,  0, INTEL_6CH,  0, ATA_SA300, "ICH8" },
     { ATA_I82801HB_S2,  0, INTEL_6CH2, 0, ATA_SA300, "ICH8" },
     { ATA_I82801HBM,    0,          0, 1, ATA_UDMA5, "ICH8M" },
     { ATA_I82801HBM_S1, 0, INTEL_6CH,  0, ATA_SA300, "ICH8M" },
     { ATA_I82801IB_S1,  0, INTEL_6CH,  0, ATA_SA300, "ICH9" },
     { ATA_I82801IB_S2,  0, INTEL_6CH2, 0, ATA_SA300, "ICH9" },
     { ATA_I82801IB_S3,  0, INTEL_6CH2, 0, ATA_SA300, "ICH9" },
     { ATA_I82801IBM_S1, 0, INTEL_6CH2, 0, ATA_SA300, "ICH9M" },
     { ATA_I82801IBM_S2, 0, INTEL_6CH2, 0, ATA_SA300, "ICH9M" },
     { ATA_I82801JIB_S1, 0, INTEL_6CH,  0, ATA_SA300, "ICH10" },
     { ATA_I82801JIB_S2, 0, INTEL_6CH2, 0, ATA_SA300, "ICH10" },
     { ATA_I82801JD_S1,  0, INTEL_6CH,  0, ATA_SA300, "ICH10" },
     { ATA_I82801JD_S2,  0, INTEL_6CH2, 0, ATA_SA300, "ICH10" },
     { ATA_I82801JI_S1,  0, INTEL_6CH,  0, ATA_SA300, "ICH10" },
     { ATA_I82801JI_S2,  0, INTEL_6CH2, 0, ATA_SA300, "ICH10" },
     { ATA_IBP_S1,       0, INTEL_6CH,  0, ATA_SA300, "Ibex Peak" },
     { ATA_IBP_S2,       0, INTEL_6CH2, 0, ATA_SA300, "Ibex Peak" },
     { ATA_IBP_S3,       0, INTEL_6CH2, 0, ATA_SA300, "Ibex Peak" },
     { ATA_IBP_S4,       0, INTEL_6CH,  0, ATA_SA300, "Ibex Peak-M" },
     { ATA_IBP_S5,       0, INTEL_6CH2, 0, ATA_SA300, "Ibex Peak-M" },
     { ATA_IBP_S6,       0, INTEL_6CH,  0, ATA_SA300, "Ibex Peak-M" },
     { ATA_CPT_S1,       0, INTEL_6CH,  0, ATA_SA600, "Cougar Point" },
     { ATA_CPT_S2,       0, INTEL_6CH,  0, ATA_SA600, "Cougar Point" },
     { ATA_CPT_S3,       0, INTEL_6CH2, 0, ATA_SA300, "Cougar Point" },
     { ATA_CPT_S4,       0, INTEL_6CH2, 0, ATA_SA300, "Cougar Point" },
     { ATA_PBG_S1,       0, INTEL_6CH,  0, ATA_SA600, "Patsburg" },
     { ATA_PBG_S2,       0, INTEL_6CH2, 0, ATA_SA300, "Patsburg" },
     { ATA_PPT_S1,       0, INTEL_6CH,  0, ATA_SA600, "Panther Point" },
     { ATA_PPT_S2,       0, INTEL_6CH,  0, ATA_SA600, "Panther Point" },
     { ATA_PPT_S3,       0, INTEL_6CH2, 0, ATA_SA300, "Panther Point" },
     { ATA_PPT_S4,       0, INTEL_6CH2, 0, ATA_SA300, "Panther Point" },
     { ATA_AVOTON_S1,    0, INTEL_6CH,  0, ATA_SA600, "Avoton" },
     { ATA_AVOTON_S2,    0, INTEL_6CH,  0, ATA_SA600, "Avoton" },
     { ATA_AVOTON_S3,    0, INTEL_6CH2, 0, ATA_SA300, "Avoton" },
     { ATA_AVOTON_S4,    0, INTEL_6CH2, 0, ATA_SA300, "Avoton" },
     { ATA_LPT_S1,       0, INTEL_6CH,  0, ATA_SA600, "Lynx Point" },
     { ATA_LPT_S2,       0, INTEL_6CH,  0, ATA_SA600, "Lynx Point" },
     { ATA_LPT_S3,       0, INTEL_6CH2, 0, ATA_SA600, "Lynx Point" },
     { ATA_LPT_S4,       0, INTEL_6CH2, 0, ATA_SA600, "Lynx Point" },
     { ATA_WCPT_S1,      0, INTEL_6CH,  0, ATA_SA600, "Wildcat Point" },
     { ATA_WCPT_S2,      0, INTEL_6CH,  0, ATA_SA600, "Wildcat Point" },
     { ATA_WCPT_S3,      0, INTEL_6CH2, 0, ATA_SA600, "Wildcat Point" },
     { ATA_WCPT_S4,      0, INTEL_6CH2, 0, ATA_SA600, "Wildcat Point" },
     { ATA_WELLS_S1,     0, INTEL_6CH,  0, ATA_SA600, "Wellsburg" },
     { ATA_WELLS_S2,     0, INTEL_6CH2, 0, ATA_SA600, "Wellsburg" },
     { ATA_WELLS_S3,     0, INTEL_6CH,  0, ATA_SA600, "Wellsburg" },
     { ATA_WELLS_S4,     0, INTEL_6CH2, 0, ATA_SA600, "Wellsburg" },
     { ATA_LPTLP_S1,     0, INTEL_6CH,  0, ATA_SA600, "Lynx Point-LP" },
     { ATA_LPTLP_S2,     0, INTEL_6CH,  0, ATA_SA600, "Lynx Point-LP" },
     { ATA_LPTLP_S3,     0, INTEL_6CH2, 0, ATA_SA300, "Lynx Point-LP" },
     { ATA_LPTLP_S4,     0, INTEL_6CH2, 0, ATA_SA300, "Lynx Point-LP" },
     { ATA_I31244,       0,          0, 2, ATA_SA150, "31244" },
     { ATA_ISCH,         0,          0, 1, ATA_UDMA5, "SCH" },
     { ATA_COLETOCRK_S1, 0, INTEL_6CH2, 0, ATA_SA300, "COLETOCRK" },
     { ATA_COLETOCRK_S2, 0, INTEL_6CH2, 0, ATA_SA300, "COLETOCRK" },
     { 0, 0, 0, 0, 0, 0}};

    if (pci_get_vendor(dev) != ATA_INTEL_ID)
	return ENXIO;

    if (!(ctlr->chip = ata_match_chip(dev, ids)))
	return ENXIO;

    ata_set_desc(dev);
    ctlr->chipinit = ata_intel_chipinit;
    ctlr->chipdeinit = ata_intel_chipdeinit;
    return (BUS_PROBE_LOW_PRIORITY);
}

static int
ata_intel_chipinit(device_t dev)
{
    struct ata_pci_controller *ctlr = device_get_softc(dev);
    struct ata_intel_data *data;

    if (ata_setup_interrupt(dev, ata_generic_intr))
	return ENXIO;

    data = malloc(sizeof(struct ata_intel_data), M_ATAPCI, M_WAITOK | M_ZERO);
    mtx_init(&data->lock, "Intel SATA lock", NULL, MTX_DEF);
    ctlr->chipset_data = (void *)data;

    /* good old PIIX needs special treatment (not implemented) */
    if (ctlr->chip->chipid == ATA_I82371FB) {
	ctlr->setmode = ata_intel_old_setmode;
    }

    /* the intel 31244 needs special care if in DPA mode */
    else if (ctlr->chip->chipid == ATA_I31244) {
	if (pci_get_subclass(dev) != PCIS_STORAGE_IDE) {
	    ctlr->r_type2 = SYS_RES_MEMORY;
	    ctlr->r_rid2 = PCIR_BAR(0);
	    if (!(ctlr->r_res2 = bus_alloc_resource_any(dev, ctlr->r_type2,
							&ctlr->r_rid2,
							RF_ACTIVE)))
		return ENXIO;
	    ctlr->channels = 4;
	    ctlr->ch_attach = ata_intel_31244_ch_attach;
	    ctlr->ch_detach = ata_intel_31244_ch_detach;
	    ctlr->reset = ata_intel_31244_reset;
	}
	ctlr->setmode = ata_sata_setmode;
	ctlr->getrev = ata_sata_getrev;
    }
    /* SCH */
    else if (ctlr->chip->chipid == ATA_ISCH) {
	ctlr->channels = 1;
	ctlr->ch_attach = ata_intel_ch_attach;
	ctlr->ch_detach = ata_pci_ch_detach;
	ctlr->setmode = ata_intel_sch_setmode;
    }
    /* non SATA intel chips goes here */
    else if (ctlr->chip->max_dma < ATA_SA150) {
	ctlr->channels = ctlr->chip->cfg2;
	ctlr->ch_attach = ata_intel_ch_attach;
	ctlr->ch_detach = ata_pci_ch_detach;
	ctlr->setmode = ata_intel_new_setmode;
    }

    /* SATA parts can be either compat or AHCI */
    else {
	/* force all ports active "the legacy way" */
	pci_write_config(dev, 0x92, pci_read_config(dev, 0x92, 2) | 0x0f, 2);

	ctlr->ch_attach = ata_intel_ch_attach;
	ctlr->ch_detach = ata_pci_ch_detach;
	ctlr->reset = ata_intel_reset;

	/* BAR(5) may point to SATA interface registers */
	if ((ctlr->chip->cfg1 & INTEL_ICH7)) {
		ctlr->r_type2 = SYS_RES_MEMORY;
		ctlr->r_rid2 = PCIR_BAR(5);
		ctlr->r_res2 = bus_alloc_resource_any(dev, ctlr->r_type2,
		    &ctlr->r_rid2, RF_ACTIVE);
		if (ctlr->r_res2 != NULL) {
			/* Set SCRAE bit to enable registers access. */
			pci_write_config(dev, 0x94,
			    pci_read_config(dev, 0x94, 4) | (1 << 9), 4);
			/* Set Ports Implemented register bits. */
			ATA_OUTL(ctlr->r_res2, 0x0C,
			    ATA_INL(ctlr->r_res2, 0x0C) | 0xf);
		}
	/* Skip BAR(5) on ICH8M Apples, system locks up on access. */
	} else if (ctlr->chip->chipid != ATA_I82801HBM_S1 ||
	    pci_get_subvendor(dev) != 0x106b) {
		ctlr->r_type2 = SYS_RES_IOPORT;
		ctlr->r_rid2 = PCIR_BAR(5);
		ctlr->r_res2 = bus_alloc_resource_any(dev, ctlr->r_type2,
		    &ctlr->r_rid2, RF_ACTIVE);
	}
	if (ctlr->r_res2 != NULL ||
	    (ctlr->chip->cfg1 & INTEL_ICH5))
		ctlr->getrev = ata_intel_sata_getrev;
	ctlr->setmode = ata_sata_setmode;
    }
    return 0;
}

static int
ata_intel_chipdeinit(device_t dev)
{
	struct ata_pci_controller *ctlr = device_get_softc(dev);
	struct ata_intel_data *data;

	data = ctlr->chipset_data;
	mtx_destroy(&data->lock);
	free(data, M_ATAPCI);
	ctlr->chipset_data = NULL;
	return (0);
}

static int
ata_intel_ch_attach(device_t dev)
{
	struct ata_pci_controller *ctlr;
	struct ata_channel *ch;
	u_char *smap;
	u_int map;

	/* setup the usual register normal pci style */
	if (ata_pci_ch_attach(dev))
		return (ENXIO);

	ctlr = device_get_softc(device_get_parent(dev));
	ch = device_get_softc(dev);

	/* if r_res2 is valid it points to SATA interface registers */
	if (ctlr->r_res2) {
		ch->r_io[ATA_IDX_ADDR].res = ctlr->r_res2;
		ch->r_io[ATA_IDX_ADDR].offset = 0x00;
		ch->r_io[ATA_IDX_DATA].res = ctlr->r_res2;
		ch->r_io[ATA_IDX_DATA].offset = 0x04;
	}

	ch->flags |= ATA_ALWAYS_DMASTAT;
	if (ctlr->chip->max_dma >= ATA_SA150) {
		smap = ATA_INTEL_SMAP(ctlr, ch);
		map = pci_read_config(device_get_parent(dev), 0x90, 1);
		if (ctlr->chip->cfg1 & INTEL_ICH5) {
			map &= 0x07;
			if ((map & 0x04) == 0) {
				ch->flags |= ATA_SATA;
				ch->flags |= ATA_NO_SLAVE;
				smap[0] = (map & 0x01) ^ ch->unit;
				smap[1] = 0;
			} else if ((map & 0x02) == 0 && ch->unit == 0) {
				ch->flags |= ATA_SATA;
				smap[0] = (map & 0x01) ? 1 : 0;
				smap[1] = (map & 0x01) ? 0 : 1;
			} else if ((map & 0x02) != 0 && ch->unit == 1) {
				ch->flags |= ATA_SATA;
				smap[0] = (map & 0x01) ? 1 : 0;
				smap[1] = (map & 0x01) ? 0 : 1;
			}
		} else if (ctlr->chip->cfg1 & INTEL_6CH2) {
			ch->flags |= ATA_SATA;
			ch->flags |= ATA_NO_SLAVE;
			smap[0] = (ch->unit == 0) ? 0 : 1;
			smap[1] = 0;
		} else {
			map &= 0x03;
			if (map == 0x00) {
				ch->flags |= ATA_SATA;
				smap[0] = (ch->unit == 0) ? 0 : 1;
				smap[1] = (ch->unit == 0) ? 2 : 3;
			} else if (map == 0x02 && ch->unit == 0) {
				ch->flags |= ATA_SATA;
				smap[0] = 0;
				smap[1] = 2;
			} else if (map == 0x01 && ch->unit == 1) {
				ch->flags |= ATA_SATA;
				smap[0] = 1;
				smap[1] = 3;
			}
		}
		if (ch->flags & ATA_SATA) {
			if ((ctlr->chip->cfg1 & INTEL_ICH5)) {
				ch->hw.pm_read = ata_intel_sata_cscr_read;
				ch->hw.pm_write = ata_intel_sata_cscr_write;
			} else if (ctlr->r_res2) {
				if ((ctlr->chip->cfg1 & INTEL_ICH7)) {
					ch->hw.pm_read = ata_intel_sata_ahci_read;
					ch->hw.pm_write = ata_intel_sata_ahci_write;
				} else if (ata_intel_sata_sidpr_test(dev)) {
					ch->hw.pm_read = ata_intel_sata_sidpr_read;
					ch->hw.pm_write = ata_intel_sata_sidpr_write;
				}
			}
			if (ch->hw.pm_write != NULL) {
				ch->flags |= ATA_PERIODIC_POLL;
				ch->hw.status = ata_intel_sata_status;
				ata_sata_scr_write(ch, 0,
				    ATA_SERROR, 0xffffffff);
				if ((ch->flags & ATA_NO_SLAVE) == 0) {
					ata_sata_scr_write(ch, 1,
					    ATA_SERROR, 0xffffffff);
				}
			}
		} else
			ctlr->setmode = ata_intel_new_setmode;
		if (ctlr->chip->max_dma >= ATA_SA600)
			ch->flags |= ATA_USE_16BIT;
	} else if (ctlr->chip->chipid != ATA_ISCH)
		ch->flags |= ATA_CHECKS_CABLE;
	return (0);
}

static void
ata_intel_reset(device_t dev)
{
	device_t parent = device_get_parent(dev);
	struct ata_pci_controller *ctlr = device_get_softc(parent);
	struct ata_channel *ch = device_get_softc(dev);
	int mask, pshift, timeout, devs;
	u_char *smap;
	uint16_t pcs;

	/* In combined mode, skip SATA stuff for PATA channel. */
	if ((ch->flags & ATA_SATA) == 0)
		return (ata_generic_reset(dev));

	/* Do hard-reset on respective SATA ports. */
	smap = ATA_INTEL_SMAP(ctlr, ch);
	mask = 1 << smap[0];
	if ((ch->flags & ATA_NO_SLAVE) == 0)
		mask |= (1 << smap[1]);
	pci_write_config(parent, 0x92,
	    pci_read_config(parent, 0x92, 2) & ~mask, 2);
	DELAY(100);
	pci_write_config(parent, 0x92,
	    pci_read_config(parent, 0x92, 2) | mask, 2);

	/* Wait up to 1 sec for "connect well". */
	if (ctlr->chip->cfg1 & (INTEL_6CH | INTEL_6CH2))
		pshift = 8;
	else
		pshift = 4;
	for (timeout = 0; timeout < 100 ; timeout++) {
		pcs = (pci_read_config(parent, 0x92, 2) >> pshift) & mask;
		if ((pcs == mask) && (ATA_IDX_INB(ch, ATA_STATUS) != 0xff))
			break;
		ata_udelay(10000);
	}

	if (bootverbose)
		device_printf(dev, "SATA reset: ports status=0x%02x\n", pcs);
	/* If any device found, do soft-reset. */
	if (ch->hw.pm_read != NULL) {
		devs = ata_sata_phy_reset(dev, 0, 2) ? ATA_ATA_MASTER : 0;
		if ((ch->flags & ATA_NO_SLAVE) == 0)
			devs |= ata_sata_phy_reset(dev, 1, 2) ?
			    ATA_ATA_SLAVE : 0;
	} else {
		devs = (pcs & (1 << smap[0])) ? ATA_ATA_MASTER : 0;
		if ((ch->flags & ATA_NO_SLAVE) == 0)
			devs |= (pcs & (1 << smap[1])) ?
			    ATA_ATA_SLAVE : 0;
	}
	if (devs) {
		ata_generic_reset(dev);
		/* Reset may give fake slave when only ATAPI master present. */
		ch->devices &= (devs | (devs * ATA_ATAPI_MASTER));
	} else
		ch->devices = 0;
}

static int
ata_intel_old_setmode(device_t dev, int target, int mode)
{
	device_t parent = device_get_parent(dev);
	struct ata_pci_controller *ctlr = device_get_softc(parent);

	mode = min(mode, ctlr->chip->max_dma);
	return (mode);
}

static int
ata_intel_new_setmode(device_t dev, int target, int mode)
{
	device_t parent = device_get_parent(dev);
	struct ata_pci_controller *ctlr = device_get_softc(parent);
	struct ata_channel *ch = device_get_softc(dev);
	int devno = (ch->unit << 1) + target;
	int piomode;
	u_int32_t reg40 = pci_read_config(parent, 0x40, 4);
	u_int8_t reg44 = pci_read_config(parent, 0x44, 1);
	u_int8_t reg48 = pci_read_config(parent, 0x48, 1);
	u_int16_t reg4a = pci_read_config(parent, 0x4a, 2);
	u_int16_t reg54 = pci_read_config(parent, 0x54, 2);
	u_int32_t mask40 = 0, new40 = 0;
	u_int8_t mask44 = 0, new44 = 0;
	static const uint8_t timings[] =
	    { 0x00, 0x00, 0x10, 0x21, 0x23, 0x00, 0x21, 0x23 };
	static const uint8_t utimings[] =
	    { 0x00, 0x01, 0x02, 0x01, 0x02, 0x01, 0x02 };

	/* In combined mode, skip PATA stuff for SATA channel. */
	if (ch->flags & ATA_SATA)
		return (ata_sata_setmode(dev, target, mode));

	mode = min(mode, ctlr->chip->max_dma);
	if (ata_dma_check_80pin && mode > ATA_UDMA2 &&
	    !(reg54 & (0x10 << devno))) {
		ata_print_cable(dev, "controller");
		mode = ATA_UDMA2;
	}
	/* Enable/disable UDMA and set timings. */
	if (mode >= ATA_UDMA0) {
	    pci_write_config(parent, 0x48, reg48 | (0x0001 << devno), 2);
	    pci_write_config(parent, 0x4a,
		(reg4a & ~(0x3 << (devno << 2))) |
		(utimings[mode & ATA_MODE_MASK] << (devno<<2)), 2);
	    piomode = ATA_PIO4;
	} else {
	    pci_write_config(parent, 0x48, reg48 & ~(0x0001 << devno), 2);
	    pci_write_config(parent, 0x4a, (reg4a & ~(0x3 << (devno << 2))),2);
	    piomode = mode;
	}
	reg54 |= 0x0400;
	/* Set UDMA reference clock (33/66/133MHz). */
	reg54 &= ~(0x1001 << devno);
	if (mode >= ATA_UDMA5)
	    reg54 |= (0x1000 << devno);
	else if (mode >= ATA_UDMA3)
	    reg54 |= (0x1 << devno);
	pci_write_config(parent, 0x54, reg54, 2);
	/* Allow PIO/WDMA timing controls. */
	reg40 &= ~0x00ff00ff;
	reg40 |= 0x40774077;
	/* Set PIO/WDMA timings. */
	if (target == 0) {
	    mask40 = 0x3300;
	    new40 = timings[ata_mode2idx(piomode)] << 8;
	} else {
	    mask44 = 0x0f;
	    new44 = ((timings[ata_mode2idx(piomode)] & 0x30) >> 2) |
		    (timings[ata_mode2idx(piomode)] & 0x03);
	}
	if (ch->unit) {
	    mask40 <<= 16;
	    new40 <<= 16;
	    mask44 <<= 4;
	    new44 <<= 4;
	}
	pci_write_config(parent, 0x40, (reg40 & ~mask40) | new40, 4);
	pci_write_config(parent, 0x44, (reg44 & ~mask44) | new44, 1);
	return (mode);
}

static int
ata_intel_sch_setmode(device_t dev, int target, int mode)
{
	device_t parent = device_get_parent(dev);
	struct ata_pci_controller *ctlr = device_get_softc(parent);
	u_int8_t dtim = 0x80 + (target << 2);
	u_int32_t tim = pci_read_config(parent, dtim, 4);
	int piomode;

	mode = min(mode, ctlr->chip->max_dma);
	if (mode >= ATA_UDMA0) {
		tim |= (0x1 << 31);
		tim &= ~(0x7 << 16);
		tim |= ((mode & ATA_MODE_MASK) << 16);
		piomode = ATA_PIO4;
	} else if (mode >= ATA_WDMA0) {
		tim &= ~(0x1 << 31);
		tim &= ~(0x3 << 8);
		tim |= ((mode & ATA_MODE_MASK) << 8);
		piomode = (mode == ATA_WDMA0) ? ATA_PIO0 :
		    (mode == ATA_WDMA1) ? ATA_PIO3 : ATA_PIO4;
	} else
		piomode = mode;
	tim &= ~(0x7);
	tim |= (piomode & 0x7);
	pci_write_config(parent, dtim, tim, 4);
	return (mode);
}

static int
ata_intel_sata_getrev(device_t dev, int target)
{
	struct ata_channel *ch = device_get_softc(dev);
	uint32_t status;

	if (ata_sata_scr_read(ch, target, ATA_SSTATUS, &status) == 0)
		return ((status & 0x0f0) >> 4);
	return (0xff);
}

static int
ata_intel_sata_status(device_t dev)
{
	struct ata_channel *ch = device_get_softc(dev);

	ata_sata_phy_check_events(dev, 0);
	if ((ch->flags & ATA_NO_SLAVE) == 0)
		ata_sata_phy_check_events(dev, 1);

	return ata_pci_status(dev);
}

static int
ata_intel_sata_ahci_read(device_t dev, int port, int reg, u_int32_t *result)
{
	struct ata_pci_controller *ctlr;
	struct ata_channel *ch;
	device_t parent;
	u_char *smap;
	int offset;

	parent = device_get_parent(dev);
	ctlr = device_get_softc(parent);
	ch = device_get_softc(dev);
	port = (port == 1) ? 1 : 0;
	smap = ATA_INTEL_SMAP(ctlr, ch);
	offset = 0x100 + smap[port] * 0x80;
	switch (reg) {
	case ATA_SSTATUS:
	    reg = 0x28;
	    break;
	case ATA_SCONTROL:
	    reg = 0x2c;
	    break;
	case ATA_SERROR:
	    reg = 0x30;
	    break;
	default:
	    return (EINVAL);
	}
	*result = ATA_INL(ctlr->r_res2, offset + reg);
	return (0);
}

static int
ata_intel_sata_cscr_read(device_t dev, int port, int reg, u_int32_t *result)
{
	struct ata_pci_controller *ctlr;
	struct ata_channel *ch;
	device_t parent;
	u_char *smap;

	parent = device_get_parent(dev);
	ctlr = device_get_softc(parent);
	ch = device_get_softc(dev);
	smap = ATA_INTEL_SMAP(ctlr, ch);
	port = (port == 1) ? 1 : 0;
	switch (reg) {
	case ATA_SSTATUS:
	    reg = 0;
	    break;
	case ATA_SERROR:
	    reg = 1;
	    break;
	case ATA_SCONTROL:
	    reg = 2;
	    break;
	default:
	    return (EINVAL);
	}
	ATA_INTEL_LOCK(ctlr);
	pci_write_config(parent, 0xa0,
	    0x50 + smap[port] * 0x10 + reg * 4, 4);
	*result = pci_read_config(parent, 0xa4, 4);
	ATA_INTEL_UNLOCK(ctlr);
	return (0);
}

static int
ata_intel_sata_sidpr_read(device_t dev, int port, int reg, u_int32_t *result)
{
	struct ata_pci_controller *ctlr;
	struct ata_channel *ch;
	device_t parent;

	parent = device_get_parent(dev);
	ctlr = device_get_softc(parent);
	ch = device_get_softc(dev);
	port = (port == 1) ? 1 : 0;
	switch (reg) {
	case ATA_SSTATUS:
	    reg = 0;
	    break;
	case ATA_SCONTROL:
	    reg = 1;
	    break;
	case ATA_SERROR:
	    reg = 2;
	    break;
	default:
	    return (EINVAL);
	}
	ATA_INTEL_LOCK(ctlr);
	ATA_IDX_OUTL(ch, ATA_IDX_ADDR, ((ch->unit * 2 + port) << 8) + reg);
	*result = ATA_IDX_INL(ch, ATA_IDX_DATA);
	ATA_INTEL_UNLOCK(ctlr);
	return (0);
}

static int
ata_intel_sata_ahci_write(device_t dev, int port, int reg, u_int32_t value)
{
	struct ata_pci_controller *ctlr;
	struct ata_channel *ch;
	device_t parent;
	u_char *smap;
	int offset;

	parent = device_get_parent(dev);
	ctlr = device_get_softc(parent);
	ch = device_get_softc(dev);
	port = (port == 1) ? 1 : 0;
	smap = ATA_INTEL_SMAP(ctlr, ch);
	offset = 0x100 + smap[port] * 0x80;
	switch (reg) {
	case ATA_SSTATUS:
	    reg = 0x28;
	    break;
	case ATA_SCONTROL:
	    reg = 0x2c;
	    break;
	case ATA_SERROR:
	    reg = 0x30;
	    break;
	default:
	    return (EINVAL);
	}
	ATA_OUTL(ctlr->r_res2, offset + reg, value);
	return (0);
}

static int
ata_intel_sata_cscr_write(device_t dev, int port, int reg, u_int32_t value)
{
	struct ata_pci_controller *ctlr;
	struct ata_channel *ch;
	device_t parent;
	u_char *smap;

	parent = device_get_parent(dev);
	ctlr = device_get_softc(parent);
	ch = device_get_softc(dev);
	smap = ATA_INTEL_SMAP(ctlr, ch);
	port = (port == 1) ? 1 : 0;
	switch (reg) {
	case ATA_SSTATUS:
	    reg = 0;
	    break;
	case ATA_SERROR:
	    reg = 1;
	    break;
	case ATA_SCONTROL:
	    reg = 2;
	    break;
	default:
	    return (EINVAL);
	}
	ATA_INTEL_LOCK(ctlr);
	pci_write_config(parent, 0xa0,
	    0x50 + smap[port] * 0x10 + reg * 4, 4);
	pci_write_config(parent, 0xa4, value, 4);
	ATA_INTEL_UNLOCK(ctlr);
	return (0);
}

static int
ata_intel_sata_sidpr_write(device_t dev, int port, int reg, u_int32_t value)
{
	struct ata_pci_controller *ctlr;
	struct ata_channel *ch;
	device_t parent;

	parent = device_get_parent(dev);
	ctlr = device_get_softc(parent);
	ch = device_get_softc(dev);
	port = (port == 1) ? 1 : 0;
	switch (reg) {
	case ATA_SSTATUS:
	    reg = 0;
	    break;
	case ATA_SCONTROL:
	    reg = 1;
	    break;
	case ATA_SERROR:
	    reg = 2;
	    break;
	default:
	    return (EINVAL);
	}
	ATA_INTEL_LOCK(ctlr);
	ATA_IDX_OUTL(ch, ATA_IDX_ADDR, ((ch->unit * 2 + port) << 8) + reg);
	ATA_IDX_OUTL(ch, ATA_IDX_DATA, value);
	ATA_INTEL_UNLOCK(ctlr);
	return (0);
}

static int
ata_intel_sata_sidpr_test(device_t dev)
{
	struct ata_channel *ch = device_get_softc(dev);
	int port;
	uint32_t val;

	port = (ch->flags & ATA_NO_SLAVE) ? 0 : 1;
	for (; port >= 0; port--) {
		ata_intel_sata_sidpr_read(dev, port, ATA_SCONTROL, &val);
		if ((val & ATA_SC_IPM_MASK) ==
		    (ATA_SC_IPM_DIS_PARTIAL | ATA_SC_IPM_DIS_SLUMBER))
			return (1);
		val |= ATA_SC_IPM_DIS_PARTIAL | ATA_SC_IPM_DIS_SLUMBER;
		ata_intel_sata_sidpr_write(dev, port, ATA_SCONTROL, val);
		ata_intel_sata_sidpr_read(dev, port, ATA_SCONTROL, &val);
		if ((val & ATA_SC_IPM_MASK) ==
		    (ATA_SC_IPM_DIS_PARTIAL | ATA_SC_IPM_DIS_SLUMBER))
			return (1);
	}
	if (bootverbose)
		device_printf(dev,
		    "SControl registers are not functional: %08x\n", val);
	return (0);
}

static int
ata_intel_31244_ch_attach(device_t dev)
{
    struct ata_pci_controller *ctlr = device_get_softc(device_get_parent(dev));
    struct ata_channel *ch = device_get_softc(dev);
    int i;
    int ch_offset;

    ata_pci_dmainit(dev);

    ch_offset = 0x200 + ch->unit * 0x200;

    for (i = ATA_DATA; i < ATA_MAX_RES; i++)
	ch->r_io[i].res = ctlr->r_res2;

    /* setup ATA registers */
    ch->r_io[ATA_DATA].offset = ch_offset + 0x00;
    ch->r_io[ATA_FEATURE].offset = ch_offset + 0x06;
    ch->r_io[ATA_COUNT].offset = ch_offset + 0x08;
    ch->r_io[ATA_SECTOR].offset = ch_offset + 0x0c;
    ch->r_io[ATA_CYL_LSB].offset = ch_offset + 0x10;
    ch->r_io[ATA_CYL_MSB].offset = ch_offset + 0x14;
    ch->r_io[ATA_DRIVE].offset = ch_offset + 0x18;
    ch->r_io[ATA_COMMAND].offset = ch_offset + 0x1d;
    ch->r_io[ATA_ERROR].offset = ch_offset + 0x04;
    ch->r_io[ATA_STATUS].offset = ch_offset + 0x1c;
    ch->r_io[ATA_ALTSTAT].offset = ch_offset + 0x28;
    ch->r_io[ATA_CONTROL].offset = ch_offset + 0x29;

    /* setup DMA registers */
    ch->r_io[ATA_SSTATUS].offset = ch_offset + 0x100;
    ch->r_io[ATA_SERROR].offset = ch_offset + 0x104;
    ch->r_io[ATA_SCONTROL].offset = ch_offset + 0x108;

    /* setup SATA registers */
    ch->r_io[ATA_BMCMD_PORT].offset = ch_offset + 0x70;
    ch->r_io[ATA_BMSTAT_PORT].offset = ch_offset + 0x72;
    ch->r_io[ATA_BMDTP_PORT].offset = ch_offset + 0x74;

    ch->flags |= ATA_NO_SLAVE;
    ch->flags |= ATA_SATA;
    ata_pci_hw(dev);
    ch->hw.status = ata_intel_31244_status;
    ch->hw.tf_write = ata_intel_31244_tf_write;

    /* enable PHY state change interrupt */
    ATA_OUTL(ctlr->r_res2, 0x4,
	     ATA_INL(ctlr->r_res2, 0x04) | (0x01 << (ch->unit << 3)));
    return 0;
}

static int
ata_intel_31244_ch_detach(device_t dev)
{

    ata_pci_dmafini(dev);
    return (0);
}

static int
ata_intel_31244_status(device_t dev)
{
    /* do we have any PHY events ? */
    ata_sata_phy_check_events(dev, -1);

    /* any drive action to take care of ? */
    return ata_pci_status(dev);
}

static void
ata_intel_31244_tf_write(struct ata_request *request)
{
    struct ata_channel *ch = device_get_softc(request->parent);

    if (request->flags & ATA_R_48BIT) {
	ATA_IDX_OUTW(ch, ATA_FEATURE, request->u.ata.feature);
	ATA_IDX_OUTW(ch, ATA_COUNT, request->u.ata.count);
	ATA_IDX_OUTW(ch, ATA_SECTOR, ((request->u.ata.lba >> 16) & 0xff00) |
				      (request->u.ata.lba & 0x00ff));
	ATA_IDX_OUTW(ch, ATA_CYL_LSB, ((request->u.ata.lba >> 24) & 0xff00) |
				       ((request->u.ata.lba >> 8) & 0x00ff));
	ATA_IDX_OUTW(ch, ATA_CYL_MSB, ((request->u.ata.lba >> 32) & 0xff00) | 
				       ((request->u.ata.lba >> 16) & 0x00ff));
	ATA_IDX_OUTW(ch, ATA_DRIVE, ATA_D_LBA | ATA_DEV(request->unit));
    }
    else {
	ATA_IDX_OUTB(ch, ATA_FEATURE, request->u.ata.feature);
	ATA_IDX_OUTB(ch, ATA_COUNT, request->u.ata.count);
	    ATA_IDX_OUTB(ch, ATA_SECTOR, request->u.ata.lba);
	    ATA_IDX_OUTB(ch, ATA_CYL_LSB, request->u.ata.lba >> 8);
	    ATA_IDX_OUTB(ch, ATA_CYL_MSB, request->u.ata.lba >> 16);
	    ATA_IDX_OUTB(ch, ATA_DRIVE,
			 ATA_D_IBM | ATA_D_LBA | ATA_DEV(request->unit) |
			 ((request->u.ata.lba >> 24) & 0x0f));
    }
}

static void
ata_intel_31244_reset(device_t dev)
{
    struct ata_channel *ch = device_get_softc(dev);

    if (ata_sata_phy_reset(dev, -1, 1))
	ata_generic_reset(dev);
    else
	ch->devices = 0;
}

ATA_DECLARE_DRIVER(ata_intel);
