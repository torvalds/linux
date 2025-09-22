/*	$OpenBSD: pciide.c,v 1.366 2024/05/15 07:46:25 jsg Exp $	*/
/*	$NetBSD: pciide.c,v 1.127 2001/08/03 01:31:08 tsutsui Exp $	*/

/*
 * Copyright (c) 1999, 2000, 2001 Manuel Bouyer.
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
 *
 */

/*
 * Copyright (c) 1996, 1998 Christopher G. Demetriou.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Christopher G. Demetriou
 *	for the NetBSD Project.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
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

/*
 * PCI IDE controller driver.
 *
 * Author: Christopher G. Demetriou, March 2, 1998 (derived from NetBSD
 * sys/dev/pci/ppb.c, revision 1.16).
 *
 * See "PCI IDE Controller Specification, Revision 1.0 3/4/94" and
 * "Programming Interface for Bus Master IDE Controller, Revision 1.0
 * 5/16/94" from the PCI SIG.
 *
 */

#define DEBUG_DMA	0x01
#define DEBUG_XFERS	0x02
#define DEBUG_FUNCS	0x08
#define DEBUG_PROBE	0x10

#ifdef WDCDEBUG
#ifndef WDCDEBUG_PCIIDE_MASK
#define WDCDEBUG_PCIIDE_MASK 0x00
#endif
int wdcdebug_pciide_mask = WDCDEBUG_PCIIDE_MASK;
#define WDCDEBUG_PRINT(args, level) do {		\
	if ((wdcdebug_pciide_mask & (level)) != 0)	\
		printf args;				\
} while (0)
#else
#define WDCDEBUG_PRINT(args, level)
#endif
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/malloc.h>
#include <sys/endian.h>

#include <machine/bus.h>

#include <dev/ata/atavar.h>
#include <dev/ata/satareg.h>
#include <dev/ic/wdcreg.h>
#include <dev/ic/wdcvar.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#include <dev/pci/pciidereg.h>
#include <dev/pci/pciidevar.h>
#include <dev/pci/pciide_piix_reg.h>
#include <dev/pci/pciide_amd_reg.h>
#include <dev/pci/pciide_apollo_reg.h>
#include <dev/pci/pciide_cmd_reg.h>
#include <dev/pci/pciide_sii3112_reg.h>
#include <dev/pci/pciide_cy693_reg.h>
#include <dev/pci/pciide_sis_reg.h>
#include <dev/pci/pciide_acer_reg.h>
#include <dev/pci/pciide_pdc202xx_reg.h>
#include <dev/pci/pciide_hpt_reg.h>
#include <dev/pci/pciide_acard_reg.h>
#include <dev/pci/pciide_natsemi_reg.h>
#include <dev/pci/pciide_nforce_reg.h>
#include <dev/pci/pciide_ite_reg.h>
#include <dev/pci/pciide_ixp_reg.h>
#include <dev/pci/pciide_svwsata_reg.h>
#include <dev/pci/pciide_jmicron_reg.h>
#include <dev/pci/pciide_rdc_reg.h>
#include <dev/pci/cy82c693var.h>

int pciide_skip_ata;
int pciide_skip_atapi;

/* functions for reading/writing 8-bit PCI registers */

u_int8_t pciide_pci_read(pci_chipset_tag_t, pcitag_t,
					int);
void pciide_pci_write(pci_chipset_tag_t, pcitag_t,
					int, u_int8_t);

u_int8_t
pciide_pci_read(pci_chipset_tag_t pc, pcitag_t pa, int reg)
{
	return (pci_conf_read(pc, pa, (reg & ~0x03)) >>
	    ((reg & 0x03) * 8) & 0xff);
}

void
pciide_pci_write(pci_chipset_tag_t pc, pcitag_t pa, int reg, u_int8_t val)
{
	pcireg_t pcival;

	pcival = pci_conf_read(pc, pa, (reg & ~0x03));
	pcival &= ~(0xff << ((reg & 0x03) * 8));
	pcival |= (val << ((reg & 0x03) * 8));
	pci_conf_write(pc, pa, (reg & ~0x03), pcival);
}

void default_chip_map(struct pciide_softc *, struct pci_attach_args *);

void sata_chip_map(struct pciide_softc *, struct pci_attach_args *);
void sata_setup_channel(struct channel_softc *);

void piix_chip_map(struct pciide_softc *, struct pci_attach_args *);
void piixsata_chip_map(struct pciide_softc *, struct pci_attach_args *);
void piix_setup_channel(struct channel_softc *);
void piix3_4_setup_channel(struct channel_softc *);
void piix_timing_debug(struct pciide_softc *);

u_int32_t piix_setup_idetim_timings(u_int8_t, u_int8_t, u_int8_t);
u_int32_t piix_setup_idetim_drvs(struct ata_drive_datas *);
u_int32_t piix_setup_sidetim_timings(u_int8_t, u_int8_t, u_int8_t);

void amd756_chip_map(struct pciide_softc *, struct pci_attach_args *);
void amd756_setup_channel(struct channel_softc *);

void apollo_chip_map(struct pciide_softc *, struct pci_attach_args *);
void apollo_setup_channel(struct channel_softc *);

void cmd_chip_map(struct pciide_softc *, struct pci_attach_args *);
void cmd0643_9_chip_map(struct pciide_softc *, struct pci_attach_args *);
void cmd0643_9_setup_channel(struct channel_softc *);
void cmd680_chip_map(struct pciide_softc *, struct pci_attach_args *);
void cmd680_setup_channel(struct channel_softc *);
void cmd680_channel_map(struct pci_attach_args *, struct pciide_softc *, int);
void cmd_channel_map(struct pci_attach_args *,
			struct pciide_softc *, int);
int  cmd_pci_intr(void *);
void cmd646_9_irqack(struct channel_softc *);

void sii_fixup_cacheline(struct pciide_softc *, struct pci_attach_args *);
void sii3112_chip_map(struct pciide_softc *, struct pci_attach_args *);
void sii3112_setup_channel(struct channel_softc *);
void sii3112_drv_probe(struct channel_softc *);
void sii3114_chip_map(struct pciide_softc *, struct pci_attach_args *);
void sii3114_mapreg_dma(struct pciide_softc *, struct pci_attach_args *);
int  sii3114_chansetup(struct pciide_softc *, int);
void sii3114_mapchan(struct pciide_channel *);
u_int8_t sii3114_dmacmd_read(struct pciide_softc *, int);
void sii3114_dmacmd_write(struct pciide_softc *, int, u_int8_t);
u_int8_t sii3114_dmactl_read(struct pciide_softc *, int);
void sii3114_dmactl_write(struct pciide_softc *, int, u_int8_t);
void sii3114_dmatbl_write(struct pciide_softc *, int, u_int32_t);

void cy693_chip_map(struct pciide_softc *, struct pci_attach_args *);
void cy693_setup_channel(struct channel_softc *);

void sis_chip_map(struct pciide_softc *, struct pci_attach_args *);
void sis_setup_channel(struct channel_softc *);
void sis96x_setup_channel(struct channel_softc *);
int  sis_hostbr_match(struct pci_attach_args *);
int  sis_south_match(struct pci_attach_args *);

void natsemi_chip_map(struct pciide_softc *, struct pci_attach_args *);
void natsemi_setup_channel(struct channel_softc *);
int  natsemi_pci_intr(void *);
void natsemi_irqack(struct channel_softc *);
void ns_scx200_chip_map(struct pciide_softc *, struct pci_attach_args *);
void ns_scx200_setup_channel(struct channel_softc *);

void acer_chip_map(struct pciide_softc *, struct pci_attach_args *);
void acer_setup_channel(struct channel_softc *);
int  acer_pci_intr(void *);
int  acer_dma_init(void *, int, int, void *, size_t, int);

void pdc202xx_chip_map(struct pciide_softc *, struct pci_attach_args *);
void pdc202xx_setup_channel(struct channel_softc *);
void pdc20268_setup_channel(struct channel_softc *);
int  pdc202xx_pci_intr(void *);
int  pdc20265_pci_intr(void *);
void pdc20262_dma_start(void *, int, int);
int  pdc20262_dma_finish(void *, int, int, int);

u_int8_t pdc268_config_read(struct channel_softc *, int);

void pdcsata_chip_map(struct pciide_softc *, struct pci_attach_args *);
void pdc203xx_setup_channel(struct channel_softc *);
int  pdc203xx_pci_intr(void *);
void pdc203xx_irqack(struct channel_softc *);
void pdc203xx_dma_start(void *,int ,int);
int  pdc203xx_dma_finish(void *, int, int, int);
int  pdc205xx_pci_intr(void *);
void pdc205xx_do_reset(struct channel_softc *);
void pdc205xx_drv_probe(struct channel_softc *);

void hpt_chip_map(struct pciide_softc *, struct pci_attach_args *);
void hpt_setup_channel(struct channel_softc *);
int  hpt_pci_intr(void *);

void acard_chip_map(struct pciide_softc *, struct pci_attach_args *);
void acard_setup_channel(struct channel_softc *);

void serverworks_chip_map(struct pciide_softc *, struct pci_attach_args *);
void serverworks_setup_channel(struct channel_softc *);
int  serverworks_pci_intr(void *);

void svwsata_chip_map(struct pciide_softc *, struct pci_attach_args *);
void svwsata_mapreg_dma(struct pciide_softc *, struct pci_attach_args *);
void svwsata_mapchan(struct pciide_channel *);
u_int8_t svwsata_dmacmd_read(struct pciide_softc *, int);
void svwsata_dmacmd_write(struct pciide_softc *, int, u_int8_t);
u_int8_t svwsata_dmactl_read(struct pciide_softc *, int);
void svwsata_dmactl_write(struct pciide_softc *, int, u_int8_t);
void svwsata_dmatbl_write(struct pciide_softc *, int, u_int32_t);
void svwsata_drv_probe(struct channel_softc *);

void nforce_chip_map(struct pciide_softc *, struct pci_attach_args *);
void nforce_setup_channel(struct channel_softc *);
int  nforce_pci_intr(void *);

void artisea_chip_map(struct pciide_softc *, struct pci_attach_args *);

void ite_chip_map(struct pciide_softc *, struct pci_attach_args *);
void ite_setup_channel(struct channel_softc *);

void ixp_chip_map(struct pciide_softc *, struct pci_attach_args *);
void ixp_setup_channel(struct channel_softc *);

void jmicron_chip_map(struct pciide_softc *, struct pci_attach_args *);
void jmicron_setup_channel(struct channel_softc *);

void phison_chip_map(struct pciide_softc *, struct pci_attach_args *);
void phison_setup_channel(struct channel_softc *);

void sch_chip_map(struct pciide_softc *, struct pci_attach_args *);
void sch_setup_channel(struct channel_softc *);

void rdc_chip_map(struct pciide_softc *, struct pci_attach_args *);
void rdc_setup_channel(struct channel_softc *);

struct pciide_product_desc {
	u_int32_t ide_product;
	u_short ide_flags;
	/* map and setup chip, probe drives */
	void (*chip_map)(struct pciide_softc *, struct pci_attach_args *);
};

/* Flags for ide_flags */
#define IDE_PCI_CLASS_OVERRIDE	0x0001	/* accept even if class != pciide */
#define IDE_16BIT_IOSPACE	0x0002	/* I/O space BARS ignore upper word */

/* Default product description for devices not known from this controller */
const struct pciide_product_desc default_product_desc = {
	0,				/* Generic PCI IDE controller */
	0,
	default_chip_map
};

const struct pciide_product_desc pciide_intel_products[] =  {
	{ PCI_PRODUCT_INTEL_31244,	/* Intel 31244 SATA */
	  0,
	  artisea_chip_map
	},
	{ PCI_PRODUCT_INTEL_82092AA,	/* Intel 82092AA IDE */
	  0,
	  default_chip_map
	},
	{ PCI_PRODUCT_INTEL_82371FB_IDE, /* Intel 82371FB IDE (PIIX) */
	  0,
	  piix_chip_map
	},
	{ PCI_PRODUCT_INTEL_82371FB_ISA, /* Intel 82371FB IDE (PIIX) */
	  0,
	  piix_chip_map
	},
	{ PCI_PRODUCT_INTEL_82372FB_IDE, /* Intel 82372FB IDE (PIIX4) */
	  0,
	  piix_chip_map
	},
	{ PCI_PRODUCT_INTEL_82371SB_IDE, /* Intel 82371SB IDE (PIIX3) */
	  0,
	  piix_chip_map
	},
	{ PCI_PRODUCT_INTEL_82371AB_IDE, /* Intel 82371AB IDE (PIIX4) */
	  0,
	  piix_chip_map
	},
	{ PCI_PRODUCT_INTEL_82371MX, /* Intel 82371MX IDE */
	  0,
	  piix_chip_map
	},
	{ PCI_PRODUCT_INTEL_82440MX_IDE, /* Intel 82440MX IDE */
	  0,
	  piix_chip_map
	},
	{ PCI_PRODUCT_INTEL_82451NX, /* Intel 82451NX (PIIX4) IDE */
	  0,
	  piix_chip_map
	},
	{ PCI_PRODUCT_INTEL_82801AA_IDE, /* Intel 82801AA IDE (ICH) */
	  0,
	  piix_chip_map
	},
	{ PCI_PRODUCT_INTEL_82801AB_IDE, /* Intel 82801AB IDE (ICH0) */
	  0,
	  piix_chip_map
	},
	{ PCI_PRODUCT_INTEL_82801BAM_IDE, /* Intel 82801BAM IDE (ICH2) */
	  0,
	  piix_chip_map
	},
	{ PCI_PRODUCT_INTEL_82801BA_IDE, /* Intel 82801BA IDE (ICH2) */
	  0,
	  piix_chip_map
	},
	{ PCI_PRODUCT_INTEL_82801CAM_IDE, /* Intel 82801CAM IDE (ICH3) */
	  0,
	  piix_chip_map
	},
	{ PCI_PRODUCT_INTEL_82801CA_IDE, /* Intel 82801CA IDE (ICH3) */
	  0,
	  piix_chip_map
	},
	{ PCI_PRODUCT_INTEL_82801DB_IDE, /* Intel 82801DB IDE (ICH4) */
	  0,
	  piix_chip_map
	},
	{ PCI_PRODUCT_INTEL_82801DBL_IDE, /* Intel 82801DBL IDE (ICH4-L) */
	  0,
	  piix_chip_map
	},
	{ PCI_PRODUCT_INTEL_82801DBM_IDE, /* Intel 82801DBM IDE (ICH4-M) */
	  0,
	  piix_chip_map
	},
	{ PCI_PRODUCT_INTEL_82801EB_IDE, /* Intel 82801EB/ER (ICH5/5R) IDE */
	  0,
	  piix_chip_map
	},
	{ PCI_PRODUCT_INTEL_82801EB_SATA, /* Intel 82801EB (ICH5) SATA */
	  0,
	  piixsata_chip_map
	},
	{ PCI_PRODUCT_INTEL_82801ER_SATA, /* Intel 82801ER (ICH5R) SATA */
	  0,
	  piixsata_chip_map
	},
	{ PCI_PRODUCT_INTEL_6300ESB_IDE, /* Intel 6300ESB IDE */
	  0,
	  piix_chip_map
	},
	{ PCI_PRODUCT_INTEL_6300ESB_SATA, /* Intel 6300ESB SATA */
	  0,
	  piixsata_chip_map
	},
	{ PCI_PRODUCT_INTEL_6300ESB_SATA2, /* Intel 6300ESB SATA */
	  0,
	  piixsata_chip_map
	},
	{ PCI_PRODUCT_INTEL_6321ESB_IDE, /* Intel 6321ESB IDE */
	  0,
	  piix_chip_map
	},
	{ PCI_PRODUCT_INTEL_82801FB_IDE,  /* Intel 82801FB (ICH6) IDE */
	  0,
	  piix_chip_map
	},
	{ PCI_PRODUCT_INTEL_82801FBM_SATA,  /* Intel 82801FBM (ICH6M) SATA */
	  0,
	  piixsata_chip_map
	},
	{ PCI_PRODUCT_INTEL_82801FB_SATA, /* Intel 82801FB (ICH6) SATA */
	  0,
	  piixsata_chip_map
	},
	{ PCI_PRODUCT_INTEL_82801FR_SATA, /* Intel 82801FR (ICH6R) SATA */
	  0,
	  piixsata_chip_map
	},
	{ PCI_PRODUCT_INTEL_82801GB_IDE,  /* Intel 82801GB (ICH7) IDE */
	  0,
	  piix_chip_map
	},
	{ PCI_PRODUCT_INTEL_82801GB_SATA, /* Intel 82801GB (ICH7) SATA */
	  0,
	  piixsata_chip_map
	},
	{ PCI_PRODUCT_INTEL_82801GR_AHCI, /* Intel 82801GR (ICH7R) AHCI */
	  0,
	  piixsata_chip_map
	},
	{ PCI_PRODUCT_INTEL_82801GR_RAID, /* Intel 82801GR (ICH7R) RAID */
	  0,
	  piixsata_chip_map
	},
	{ PCI_PRODUCT_INTEL_82801GBM_SATA, /* Intel 82801GBM (ICH7M) SATA */
	  0,
	  piixsata_chip_map
	},
	{ PCI_PRODUCT_INTEL_82801GBM_AHCI, /* Intel 82801GBM (ICH7M) AHCI */
	  0,
	  piixsata_chip_map
	},
	{ PCI_PRODUCT_INTEL_82801GHM_RAID, /* Intel 82801GHM (ICH7M DH) RAID */
	  0,
	  piixsata_chip_map
	},
	{ PCI_PRODUCT_INTEL_82801H_SATA_1, /* Intel 82801H (ICH8) SATA */
	  0,
	  piixsata_chip_map
	},
	{ PCI_PRODUCT_INTEL_82801H_AHCI_6P, /* Intel 82801H (ICH8) AHCI */
	  0,
	  piixsata_chip_map
	},
	{ PCI_PRODUCT_INTEL_82801H_RAID, /* Intel 82801H (ICH8) RAID */
	  0,
	  piixsata_chip_map
	},
	{ PCI_PRODUCT_INTEL_82801H_AHCI_4P, /* Intel 82801H (ICH8) AHCI */
	  0,
	  piixsata_chip_map
	},
	{ PCI_PRODUCT_INTEL_82801H_SATA_2, /* Intel 82801H (ICH8) SATA */
	  0,
	  piixsata_chip_map
	},
	{ PCI_PRODUCT_INTEL_82801HBM_SATA, /* Intel 82801HBM (ICH8M) SATA */
	  0,
	  piixsata_chip_map
	},
	{ PCI_PRODUCT_INTEL_82801HBM_AHCI, /* Intel 82801HBM (ICH8M) AHCI */
	  0,
	  piixsata_chip_map
	},
	{ PCI_PRODUCT_INTEL_82801HBM_RAID, /* Intel 82801HBM (ICH8M) RAID */
	  0,
	  piixsata_chip_map
	},
	{ PCI_PRODUCT_INTEL_82801HBM_IDE, /* Intel 82801HBM (ICH8M) IDE */
	  0,
	  piix_chip_map
	},
	{ PCI_PRODUCT_INTEL_82801I_SATA_1, /* Intel 82801I (ICH9) SATA */
	  0,
	  piixsata_chip_map
	},
	{ PCI_PRODUCT_INTEL_82801I_SATA_2, /* Intel 82801I (ICH9) SATA */
	  0,
	  piixsata_chip_map
	},
	{ PCI_PRODUCT_INTEL_82801I_SATA_3, /* Intel 82801I (ICH9) SATA */
	  0,
	  piixsata_chip_map
	},
	{ PCI_PRODUCT_INTEL_82801I_SATA_4, /* Intel 82801I (ICH9) SATA */
	  0,
	  piixsata_chip_map
	},
	{ PCI_PRODUCT_INTEL_82801I_SATA_5, /* Intel 82801I (ICH9M) SATA */
	  0,
	  piixsata_chip_map
	},
	{ PCI_PRODUCT_INTEL_82801I_SATA_6, /* Intel 82801I (ICH9M) SATA */
	  0,
	  piixsata_chip_map
	},
	{ PCI_PRODUCT_INTEL_82801JD_SATA_1, /* Intel 82801JD (ICH10) SATA */
	  0,
	  piixsata_chip_map
	},
	{ PCI_PRODUCT_INTEL_82801JD_SATA_2, /* Intel 82801JD (ICH10) SATA */
	  0,
	  piixsata_chip_map
	},
	{ PCI_PRODUCT_INTEL_82801JI_SATA_1, /* Intel 82801JI (ICH10) SATA */
	  0,
	  piixsata_chip_map
	},
	{ PCI_PRODUCT_INTEL_82801JI_SATA_2, /* Intel 82801JI (ICH10) SATA */
	  0,
	  piixsata_chip_map
	},
	{ PCI_PRODUCT_INTEL_6321ESB_SATA, /* Intel 6321ESB SATA */
	  0,
	  piixsata_chip_map
	},
	{ PCI_PRODUCT_INTEL_3400_SATA_1, /* Intel 3400 SATA */
	  0,
	  piixsata_chip_map
	},
	{ PCI_PRODUCT_INTEL_3400_SATA_2, /* Intel 3400 SATA */
	  0,
	  piixsata_chip_map
	},
	{ PCI_PRODUCT_INTEL_3400_SATA_3, /* Intel 3400 SATA */
	  0,
	  piixsata_chip_map
	},
	{ PCI_PRODUCT_INTEL_3400_SATA_4, /* Intel 3400 SATA */
	  0,
	  piixsata_chip_map
	},
	{ PCI_PRODUCT_INTEL_3400_SATA_5, /* Intel 3400 SATA */
	  0,
	  piixsata_chip_map
	},
	{ PCI_PRODUCT_INTEL_3400_SATA_6, /* Intel 3400 SATA */
	  0,
	  piixsata_chip_map
	},
	{ PCI_PRODUCT_INTEL_C600_SATA, /* Intel C600 SATA */
	  0,
	  piixsata_chip_map
	},
	{ PCI_PRODUCT_INTEL_C610_SATA_1, /* Intel C610 SATA */
	  0,
	  piixsata_chip_map
	},
	{ PCI_PRODUCT_INTEL_C610_SATA_2, /* Intel C610 SATA */
	  0,
	  piixsata_chip_map
	},
	{ PCI_PRODUCT_INTEL_C610_SATA_3, /* Intel C610 SATA */
	  0,
	  piixsata_chip_map
	},
	{ PCI_PRODUCT_INTEL_6SERIES_SATA_1, /* Intel 6 Series SATA */
	  0,
	  piixsata_chip_map
	},
	{ PCI_PRODUCT_INTEL_6SERIES_SATA_2, /* Intel 6 Series SATA */
	  0,
	  piixsata_chip_map
	},
	{ PCI_PRODUCT_INTEL_6SERIES_SATA_3, /* Intel 6 Series SATA */
	  0,
	  piixsata_chip_map
	},
	{ PCI_PRODUCT_INTEL_6SERIES_SATA_4, /* Intel 6 Series SATA */
	  0,
	  piixsata_chip_map
	},
	{ PCI_PRODUCT_INTEL_7SERIES_SATA_1, /* Intel 7 Series SATA */
	  0,
	  piixsata_chip_map
	},
	{ PCI_PRODUCT_INTEL_7SERIES_SATA_2, /* Intel 7 Series SATA */
	  0,
	  piixsata_chip_map
	},
	{ PCI_PRODUCT_INTEL_7SERIES_SATA_3, /* Intel 7 Series SATA */
	  0,
	  piixsata_chip_map
	},
	{ PCI_PRODUCT_INTEL_7SERIES_SATA_4, /* Intel 7 Series SATA */
	  0,
	  piixsata_chip_map
	},
	{ PCI_PRODUCT_INTEL_8SERIES_SATA_1, /* Intel 8 Series SATA */
	  0,
	  piixsata_chip_map
	},
	{ PCI_PRODUCT_INTEL_8SERIES_SATA_2, /* Intel 8 Series SATA */
	  0,
	  piixsata_chip_map
	},
	{ PCI_PRODUCT_INTEL_8SERIES_SATA_3, /* Intel 8 Series SATA */
	  0,
	  piixsata_chip_map
	},
	{ PCI_PRODUCT_INTEL_8SERIES_SATA_4, /* Intel 8 Series SATA */
	  0,
	  piixsata_chip_map
	},
	{ PCI_PRODUCT_INTEL_8SERIES_LP_SATA_1, /* Intel 8 Series SATA */
	  0,
	  piixsata_chip_map
	},
	{ PCI_PRODUCT_INTEL_8SERIES_LP_SATA_2, /* Intel 8 Series SATA */
	  0,
	  piixsata_chip_map
	},
	{ PCI_PRODUCT_INTEL_8SERIES_LP_SATA_3, /* Intel 8 Series SATA */
	  0,
	  piixsata_chip_map
	},
	{ PCI_PRODUCT_INTEL_8SERIES_LP_SATA_4, /* Intel 8 Series SATA */
	  0,
	  piixsata_chip_map
	},
	{ PCI_PRODUCT_INTEL_9SERIES_SATA_1, /* Intel 9 Series SATA */
	  0,
	  piixsata_chip_map
	},
	{ PCI_PRODUCT_INTEL_9SERIES_SATA_2, /* Intel 9 Series SATA */
	  0,
	  piixsata_chip_map
	},
	{ PCI_PRODUCT_INTEL_ATOMC2000_SATA_1, /* Intel Atom C2000 SATA */
	  0,
	  piixsata_chip_map
	},
	{ PCI_PRODUCT_INTEL_ATOMC2000_SATA_2, /* Intel Atom C2000 SATA */
	  0,
	  piixsata_chip_map
	},
	{ PCI_PRODUCT_INTEL_ATOMC2000_SATA_3, /* Intel Atom C2000 SATA */
	  0,
	  piixsata_chip_map
	},
	{ PCI_PRODUCT_INTEL_ATOMC2000_SATA_4, /* Intel Atom C2000 SATA */
	  0,
	  piixsata_chip_map
	},
	{ PCI_PRODUCT_INTEL_BAYTRAIL_SATA_1, /* Intel Baytrail SATA */
	  0,
	  piixsata_chip_map
	},
	{ PCI_PRODUCT_INTEL_BAYTRAIL_SATA_2, /* Intel Baytrail SATA */
	  0,
	  piixsata_chip_map
	},
	{ PCI_PRODUCT_INTEL_EP80579_SATA, /* Intel EP80579 SATA */
	  0,
	  piixsata_chip_map
	},
	{ PCI_PRODUCT_INTEL_DH8900_SATA_1, /* Intel DH8900 SATA */
	  0,
	  piixsata_chip_map
	},
	{ PCI_PRODUCT_INTEL_DH8900_SATA_2, /* Intel DH8900 SATA */
	  0,
	  piixsata_chip_map
	},
	{ PCI_PRODUCT_INTEL_SCH_IDE, /* Intel SCH IDE */
	  0,
	  sch_chip_map
	}
};

const struct pciide_product_desc pciide_amd_products[] =  {
	{ PCI_PRODUCT_AMD_PBC756_IDE,	/* AMD 756 */
	  0,
	  amd756_chip_map
	},
	{ PCI_PRODUCT_AMD_766_IDE, /* AMD 766 */
	  0,
	  amd756_chip_map
	},
	{ PCI_PRODUCT_AMD_PBC768_IDE,
	  0,
	  amd756_chip_map
	},
	{ PCI_PRODUCT_AMD_8111_IDE,
	  0,
	  amd756_chip_map
	},
	{ PCI_PRODUCT_AMD_CS5536_IDE,
	  0,
	  amd756_chip_map
	},
	{ PCI_PRODUCT_AMD_HUDSON2_IDE,
	  0,
	  ixp_chip_map
	}
};

const struct pciide_product_desc pciide_cmd_products[] =  {
	{ PCI_PRODUCT_CMDTECH_640,	/* CMD Technology PCI0640 */
	  0,
	  cmd_chip_map
	},
	{ PCI_PRODUCT_CMDTECH_643,	/* CMD Technology PCI0643 */
	  0,
	  cmd0643_9_chip_map
	},
	{ PCI_PRODUCT_CMDTECH_646,	/* CMD Technology PCI0646 */
	  0,
	  cmd0643_9_chip_map
	},
	{ PCI_PRODUCT_CMDTECH_648,	/* CMD Technology PCI0648 */
	  0,
	  cmd0643_9_chip_map
	},
	{ PCI_PRODUCT_CMDTECH_649,	/* CMD Technology PCI0649 */
	  0,
	  cmd0643_9_chip_map
	},
	{ PCI_PRODUCT_CMDTECH_680,	/* CMD Technology PCI0680 */
	  IDE_PCI_CLASS_OVERRIDE,
	  cmd680_chip_map
	},
	{ PCI_PRODUCT_CMDTECH_3112,	/* SiI3112 SATA */
	  0,
	  sii3112_chip_map
	},
	{ PCI_PRODUCT_CMDTECH_3512,	/* SiI3512 SATA */
	  0,
	  sii3112_chip_map
	},
	{ PCI_PRODUCT_CMDTECH_AAR_1210SA, /* Adaptec AAR-1210SA */
	  0,
	  sii3112_chip_map
	},
	{ PCI_PRODUCT_CMDTECH_3114,	/* SiI3114 SATA */
	  0,
	  sii3114_chip_map
	}
};

const struct pciide_product_desc pciide_via_products[] =  {
	{ PCI_PRODUCT_VIATECH_VT82C416, /* VIA VT82C416 IDE */
	  0,
	  apollo_chip_map
	},
	{ PCI_PRODUCT_VIATECH_VT82C571, /* VIA VT82C571 IDE */
	  0,
	  apollo_chip_map
	},
	{ PCI_PRODUCT_VIATECH_VT6410, /* VIA VT6410 IDE */
	  IDE_PCI_CLASS_OVERRIDE,
	  apollo_chip_map
	},
	{ PCI_PRODUCT_VIATECH_VT6415, /* VIA VT6415 IDE */
	  IDE_PCI_CLASS_OVERRIDE,
	  apollo_chip_map
	},
	{ PCI_PRODUCT_VIATECH_CX700_IDE, /* VIA CX700 IDE */
	  0,
	  apollo_chip_map
	},
	{ PCI_PRODUCT_VIATECH_VX700_IDE, /* VIA VX700 IDE */
	  0,
	  apollo_chip_map
	},
	{ PCI_PRODUCT_VIATECH_VX855_IDE, /* VIA VX855 IDE */
	  0,
	  apollo_chip_map
	},
	{ PCI_PRODUCT_VIATECH_VX900_IDE, /* VIA VX900 IDE */
	  0,
	  apollo_chip_map
	},
	{ PCI_PRODUCT_VIATECH_VT6420_SATA, /* VIA VT6420 SATA */
	  0,
	  sata_chip_map
	},
	{ PCI_PRODUCT_VIATECH_VT6421_SATA, /* VIA VT6421 SATA */
	  0,
	  sata_chip_map
	},
	{ PCI_PRODUCT_VIATECH_VT8237A_SATA, /* VIA VT8237A SATA */
	  0,
	  sata_chip_map
	},
	{ PCI_PRODUCT_VIATECH_VT8237A_SATA_2, /* VIA VT8237A SATA */
	  0,
	  sata_chip_map
	},
	{ PCI_PRODUCT_VIATECH_VT8237S_SATA, /* VIA VT8237S SATA */
	  0,
	  sata_chip_map
	},
	{ PCI_PRODUCT_VIATECH_VT8251_SATA, /* VIA VT8251 SATA */
	  0,
	  sata_chip_map
	}
};

const struct pciide_product_desc pciide_cypress_products[] =  {
	{ PCI_PRODUCT_CONTAQ_82C693,	/* Contaq CY82C693 IDE */
	  IDE_16BIT_IOSPACE,
	  cy693_chip_map
	}
};

const struct pciide_product_desc pciide_sis_products[] =  {
	{ PCI_PRODUCT_SIS_5513,		/* SIS 5513 EIDE */
	  0,
	  sis_chip_map
	},
	{ PCI_PRODUCT_SIS_180,		/* SIS 180 SATA */
	  0,
	  sata_chip_map
	},
	{ PCI_PRODUCT_SIS_181,		/* SIS 181 SATA */
	  0,
	  sata_chip_map
	},
	{ PCI_PRODUCT_SIS_182,		/* SIS 182 SATA */
	  0,
	  sata_chip_map
	},
	{ PCI_PRODUCT_SIS_1183,		/* SIS 1183 SATA */
	  0,
	  sata_chip_map
	}
};

/*
 * The National/AMD CS5535 requires MSRs to set DMA/PIO modes so it
 * has been banished to the MD i386 pciide_machdep
 */
const struct pciide_product_desc pciide_natsemi_products[] =  {
#ifdef __i386__
	{ PCI_PRODUCT_NS_CS5535_IDE,	/* National/AMD CS5535 IDE */
	  0,
	  gcsc_chip_map
	},
#endif
	{ PCI_PRODUCT_NS_PC87415,	/* National Semi PC87415 IDE */
	  0,
	  natsemi_chip_map
	},
	{ PCI_PRODUCT_NS_SCX200_IDE,	/* National Semi SCx200 IDE */
	  0,
	  ns_scx200_chip_map
	}
};

const struct pciide_product_desc pciide_acer_products[] =  {
	{ PCI_PRODUCT_ALI_M5229,	/* Acer Labs M5229 UDMA IDE */
	  0,
	  acer_chip_map
	}
};

const struct pciide_product_desc pciide_triones_products[] =  {
	{ PCI_PRODUCT_TRIONES_HPT366,	/* Highpoint HPT36x/37x IDE */
	  IDE_PCI_CLASS_OVERRIDE,
	  hpt_chip_map,
	},
	{ PCI_PRODUCT_TRIONES_HPT372A,	/* Highpoint HPT372A IDE */
	  IDE_PCI_CLASS_OVERRIDE,
	  hpt_chip_map
	},
	{ PCI_PRODUCT_TRIONES_HPT302,	/* Highpoint HPT302 IDE */
	  IDE_PCI_CLASS_OVERRIDE,
	  hpt_chip_map
	},
	{ PCI_PRODUCT_TRIONES_HPT371,	/* Highpoint HPT371 IDE */
	  IDE_PCI_CLASS_OVERRIDE,
	  hpt_chip_map
	},
	{ PCI_PRODUCT_TRIONES_HPT374,	/* Highpoint HPT374 IDE */
	  IDE_PCI_CLASS_OVERRIDE,
	  hpt_chip_map
	}
};

const struct pciide_product_desc pciide_promise_products[] =  {
	{ PCI_PRODUCT_PROMISE_PDC20246,
	  IDE_PCI_CLASS_OVERRIDE,
	  pdc202xx_chip_map,
	},
	{ PCI_PRODUCT_PROMISE_PDC20262,
	  IDE_PCI_CLASS_OVERRIDE,
	  pdc202xx_chip_map,
	},
	{ PCI_PRODUCT_PROMISE_PDC20265,
	  IDE_PCI_CLASS_OVERRIDE,
	  pdc202xx_chip_map,
	},
	{ PCI_PRODUCT_PROMISE_PDC20267,
	  IDE_PCI_CLASS_OVERRIDE,
	  pdc202xx_chip_map,
	},
	{ PCI_PRODUCT_PROMISE_PDC20268,
	  IDE_PCI_CLASS_OVERRIDE,
	  pdc202xx_chip_map,
	},
	{ PCI_PRODUCT_PROMISE_PDC20268R,
	  IDE_PCI_CLASS_OVERRIDE,
	  pdc202xx_chip_map,
	},
	{ PCI_PRODUCT_PROMISE_PDC20269,
	  IDE_PCI_CLASS_OVERRIDE,
	  pdc202xx_chip_map,
	},
	{ PCI_PRODUCT_PROMISE_PDC20271,
	  IDE_PCI_CLASS_OVERRIDE,
	  pdc202xx_chip_map,
	},
	{ PCI_PRODUCT_PROMISE_PDC20275,
	  IDE_PCI_CLASS_OVERRIDE,
	  pdc202xx_chip_map,
	},
	{ PCI_PRODUCT_PROMISE_PDC20276,
	  IDE_PCI_CLASS_OVERRIDE,
	  pdc202xx_chip_map,
	},
	{ PCI_PRODUCT_PROMISE_PDC20277,
	  IDE_PCI_CLASS_OVERRIDE,
	  pdc202xx_chip_map,
	},
	{ PCI_PRODUCT_PROMISE_PDC20318,
	  IDE_PCI_CLASS_OVERRIDE,
	  pdcsata_chip_map,
	},
	{ PCI_PRODUCT_PROMISE_PDC20319,
	  IDE_PCI_CLASS_OVERRIDE,
	  pdcsata_chip_map,
	},
	{ PCI_PRODUCT_PROMISE_PDC20371,
	  IDE_PCI_CLASS_OVERRIDE,
	  pdcsata_chip_map,
	},
	{ PCI_PRODUCT_PROMISE_PDC20375,
	  IDE_PCI_CLASS_OVERRIDE,
	  pdcsata_chip_map,
	},
	{ PCI_PRODUCT_PROMISE_PDC20376,
	  IDE_PCI_CLASS_OVERRIDE,
	  pdcsata_chip_map,
	},
	{ PCI_PRODUCT_PROMISE_PDC20377,
	  IDE_PCI_CLASS_OVERRIDE,
	  pdcsata_chip_map,
	},
	{ PCI_PRODUCT_PROMISE_PDC20378,
	  IDE_PCI_CLASS_OVERRIDE,
	  pdcsata_chip_map,
	},
	{ PCI_PRODUCT_PROMISE_PDC20379,
	  IDE_PCI_CLASS_OVERRIDE,
	  pdcsata_chip_map,
	},
	{ PCI_PRODUCT_PROMISE_PDC40518,
	  IDE_PCI_CLASS_OVERRIDE,
	  pdcsata_chip_map,
	},
	{ PCI_PRODUCT_PROMISE_PDC40519,
	  IDE_PCI_CLASS_OVERRIDE,
	  pdcsata_chip_map,
	},
	{ PCI_PRODUCT_PROMISE_PDC40718,
	  IDE_PCI_CLASS_OVERRIDE,
	  pdcsata_chip_map,
	},
	{ PCI_PRODUCT_PROMISE_PDC40719,
	  IDE_PCI_CLASS_OVERRIDE,
	  pdcsata_chip_map,
	},
	{ PCI_PRODUCT_PROMISE_PDC40779,
	  IDE_PCI_CLASS_OVERRIDE,
	  pdcsata_chip_map,
	},
	{ PCI_PRODUCT_PROMISE_PDC20571,
	  IDE_PCI_CLASS_OVERRIDE,
	  pdcsata_chip_map,
	},
	{ PCI_PRODUCT_PROMISE_PDC20575,
	  IDE_PCI_CLASS_OVERRIDE,
	  pdcsata_chip_map,
	},
	{ PCI_PRODUCT_PROMISE_PDC20579,
	  IDE_PCI_CLASS_OVERRIDE,
	  pdcsata_chip_map,
	},
	{ PCI_PRODUCT_PROMISE_PDC20771,
	  IDE_PCI_CLASS_OVERRIDE,
	  pdcsata_chip_map,
	},
	{ PCI_PRODUCT_PROMISE_PDC20775,
	  IDE_PCI_CLASS_OVERRIDE,
	  pdcsata_chip_map,
	}
};

const struct pciide_product_desc pciide_acard_products[] =  {
	{ PCI_PRODUCT_ACARD_ATP850U,	/* Acard ATP850U Ultra33 Controller */
	  IDE_PCI_CLASS_OVERRIDE,
	  acard_chip_map,
	},
	{ PCI_PRODUCT_ACARD_ATP860,	/* Acard ATP860 Ultra66 Controller */
	  IDE_PCI_CLASS_OVERRIDE,
	  acard_chip_map,
	},
	{ PCI_PRODUCT_ACARD_ATP860A,	/* Acard ATP860-A Ultra66 Controller */
	  IDE_PCI_CLASS_OVERRIDE,
	  acard_chip_map,
	},
	{ PCI_PRODUCT_ACARD_ATP865A,	/* Acard ATP865-A Ultra133 Controller */
	  IDE_PCI_CLASS_OVERRIDE,
	  acard_chip_map,
	},
	{ PCI_PRODUCT_ACARD_ATP865R,	/* Acard ATP865-R Ultra133 Controller */
	  IDE_PCI_CLASS_OVERRIDE,
	  acard_chip_map,
	}
};

const struct pciide_product_desc pciide_serverworks_products[] =  {
	{ PCI_PRODUCT_RCC_OSB4_IDE,
	  0,
	  serverworks_chip_map,
	},
	{ PCI_PRODUCT_RCC_CSB5_IDE,
	  0,
	  serverworks_chip_map,
	},
	{ PCI_PRODUCT_RCC_CSB6_IDE,
	  0,
	  serverworks_chip_map,
	},
	{ PCI_PRODUCT_RCC_CSB6_RAID_IDE,
	  0,
	  serverworks_chip_map,
	},
	{ PCI_PRODUCT_RCC_HT_1000_IDE,
	  0,
	  serverworks_chip_map,
	},
	{ PCI_PRODUCT_RCC_K2_SATA,
	  0,
	  svwsata_chip_map,
	},
	{ PCI_PRODUCT_RCC_FRODO4_SATA,
	  0,
	  svwsata_chip_map,
	},
	{ PCI_PRODUCT_RCC_FRODO8_SATA,
	  0,
	  svwsata_chip_map,
	},
	{ PCI_PRODUCT_RCC_HT_1000_SATA_1,
	  0,
	  svwsata_chip_map,
	},
	{ PCI_PRODUCT_RCC_HT_1000_SATA_2,
	  0,
	  svwsata_chip_map,
	}
};

const struct pciide_product_desc pciide_nvidia_products[] = {
	{ PCI_PRODUCT_NVIDIA_NFORCE_IDE,
	  0,
	  nforce_chip_map
	},
	{ PCI_PRODUCT_NVIDIA_NFORCE2_IDE,
	  0,
	  nforce_chip_map
	},
	{ PCI_PRODUCT_NVIDIA_NFORCE2_400_IDE,
	  0,
	  nforce_chip_map
	},
	{ PCI_PRODUCT_NVIDIA_NFORCE3_IDE,
	  0,
	  nforce_chip_map
	},
	{ PCI_PRODUCT_NVIDIA_NFORCE3_250_IDE,
	  0,
	  nforce_chip_map
	},
	{ PCI_PRODUCT_NVIDIA_NFORCE4_ATA133,
	  0,
	  nforce_chip_map
	},
	{ PCI_PRODUCT_NVIDIA_MCP04_IDE,
	  0,
	  nforce_chip_map
	},
	{ PCI_PRODUCT_NVIDIA_MCP51_IDE,
	  0,
	  nforce_chip_map
	},
	{ PCI_PRODUCT_NVIDIA_MCP55_IDE,
	  0,
	  nforce_chip_map
	},
	{ PCI_PRODUCT_NVIDIA_MCP61_IDE,
	  0,
	  nforce_chip_map
	},
	{ PCI_PRODUCT_NVIDIA_MCP65_IDE,
	  0,
	  nforce_chip_map
	},
	{ PCI_PRODUCT_NVIDIA_MCP67_IDE,
	  0,
	  nforce_chip_map
	},
	{ PCI_PRODUCT_NVIDIA_MCP73_IDE,
	  0,
	  nforce_chip_map
	},
	{ PCI_PRODUCT_NVIDIA_MCP77_IDE,
	  0,
	  nforce_chip_map
	},
	{ PCI_PRODUCT_NVIDIA_NFORCE2_400_SATA,
	  0,
	  sata_chip_map
	},
	{ PCI_PRODUCT_NVIDIA_NFORCE3_250_SATA,
	  0,
	  sata_chip_map
	},
	{ PCI_PRODUCT_NVIDIA_NFORCE3_250_SATA2,
	  0,
	  sata_chip_map
	},
	{ PCI_PRODUCT_NVIDIA_NFORCE4_SATA1,
	  0,
	  sata_chip_map
	},
	{ PCI_PRODUCT_NVIDIA_NFORCE4_SATA2,
	  0,
	  sata_chip_map
	},
	{ PCI_PRODUCT_NVIDIA_MCP04_SATA,
	  0,
	  sata_chip_map
	},
	{ PCI_PRODUCT_NVIDIA_MCP04_SATA2,
	  0,
	  sata_chip_map
	},
	{ PCI_PRODUCT_NVIDIA_MCP51_SATA,
	  0,
	  sata_chip_map
	},
	{ PCI_PRODUCT_NVIDIA_MCP51_SATA2,
	  0,
	  sata_chip_map
	},
	{ PCI_PRODUCT_NVIDIA_MCP55_SATA,
	  0,
	  sata_chip_map
	},
	{ PCI_PRODUCT_NVIDIA_MCP55_SATA2,
	  0,
	  sata_chip_map
	},
	{ PCI_PRODUCT_NVIDIA_MCP61_SATA,
	  0,
	  sata_chip_map
	},
	{ PCI_PRODUCT_NVIDIA_MCP61_SATA2,
	  0,
	  sata_chip_map
	},
	{ PCI_PRODUCT_NVIDIA_MCP61_SATA3,
	  0,
	  sata_chip_map
	},
	{ PCI_PRODUCT_NVIDIA_MCP65_SATA_1,
	  0,
	  sata_chip_map
	},
	{ PCI_PRODUCT_NVIDIA_MCP65_SATA_2,
	  0,
	  sata_chip_map
	},
	{ PCI_PRODUCT_NVIDIA_MCP65_SATA_3,
	  0,
	  sata_chip_map
	},
	{ PCI_PRODUCT_NVIDIA_MCP65_SATA_4,
	  0,
	  sata_chip_map
	},
	{ PCI_PRODUCT_NVIDIA_MCP67_SATA_1,
	  0,
	  sata_chip_map
	},
	{ PCI_PRODUCT_NVIDIA_MCP67_SATA_2,
	  0,
	  sata_chip_map
	},
	{ PCI_PRODUCT_NVIDIA_MCP67_SATA_3,
	  0,
	  sata_chip_map
	},
	{ PCI_PRODUCT_NVIDIA_MCP67_SATA_4,
	  0,
	  sata_chip_map
	},
	{ PCI_PRODUCT_NVIDIA_MCP77_SATA_1,
	  0,
	  sata_chip_map
	},
	{ PCI_PRODUCT_NVIDIA_MCP79_SATA_1,
	  0,
	  sata_chip_map
	},
	{ PCI_PRODUCT_NVIDIA_MCP79_SATA_2,
	  0,
	  sata_chip_map
	},
	{ PCI_PRODUCT_NVIDIA_MCP79_SATA_3,
	  0,
	  sata_chip_map
	},
	{ PCI_PRODUCT_NVIDIA_MCP79_SATA_4,
	  0,
	  sata_chip_map
	},
	{ PCI_PRODUCT_NVIDIA_MCP89_SATA_1,
	  0,
	  sata_chip_map
	},
	{ PCI_PRODUCT_NVIDIA_MCP89_SATA_2,
	  0,
	  sata_chip_map
	},
	{ PCI_PRODUCT_NVIDIA_MCP89_SATA_3,
	  0,
	  sata_chip_map
	},
	{ PCI_PRODUCT_NVIDIA_MCP89_SATA_4,
	  0,
	  sata_chip_map
	}
};

const struct pciide_product_desc pciide_ite_products[] = {
	{ PCI_PRODUCT_ITEXPRESS_IT8211F,
	  IDE_PCI_CLASS_OVERRIDE,
	  ite_chip_map
	},
	{ PCI_PRODUCT_ITEXPRESS_IT8212F,
	  IDE_PCI_CLASS_OVERRIDE,
	  ite_chip_map
	}
};

const struct pciide_product_desc pciide_ati_products[] = {
	{ PCI_PRODUCT_ATI_SB200_IDE,
	  0,
	  ixp_chip_map
	},
	{ PCI_PRODUCT_ATI_SB300_IDE,
	  0,
	  ixp_chip_map
	},
	{ PCI_PRODUCT_ATI_SB400_IDE,
	  0,
	  ixp_chip_map
	},
	{ PCI_PRODUCT_ATI_SB600_IDE,
	  0,
	  ixp_chip_map
	},
	{ PCI_PRODUCT_ATI_SB700_IDE,
	  0,
	  ixp_chip_map
	},
	{ PCI_PRODUCT_ATI_SB300_SATA,
	  0,
	  sii3112_chip_map
	},
	{ PCI_PRODUCT_ATI_SB400_SATA_1,
	  0,
	  sii3112_chip_map
	},
	{ PCI_PRODUCT_ATI_SB400_SATA_2,
	  0,
	  sii3112_chip_map
	}
};

const struct pciide_product_desc pciide_jmicron_products[] = {
	{ PCI_PRODUCT_JMICRON_JMB361,
	  0,
	  jmicron_chip_map
	},
	{ PCI_PRODUCT_JMICRON_JMB363,
	  0,
	  jmicron_chip_map
	},
	{ PCI_PRODUCT_JMICRON_JMB365,
	  0,
	  jmicron_chip_map
	},
	{ PCI_PRODUCT_JMICRON_JMB366,
	  0,
	  jmicron_chip_map
	},
	{ PCI_PRODUCT_JMICRON_JMB368,
	  0,
	  jmicron_chip_map
	}
};

const struct pciide_product_desc pciide_phison_products[] = {
	{ PCI_PRODUCT_PHISON_PS5000,
	  0,
	  phison_chip_map
	},
};

const struct pciide_product_desc pciide_rdc_products[] = {
	{ PCI_PRODUCT_RDC_R1012_IDE,
	  0,
	  rdc_chip_map
	},
};

struct pciide_vendor_desc {
	u_int32_t ide_vendor;
	const struct pciide_product_desc *ide_products;
	int ide_nproducts;
};

const struct pciide_vendor_desc pciide_vendors[] = {
	{ PCI_VENDOR_INTEL, pciide_intel_products,
	  nitems(pciide_intel_products) },
	{ PCI_VENDOR_AMD, pciide_amd_products,
	  nitems(pciide_amd_products) },
	{ PCI_VENDOR_CMDTECH, pciide_cmd_products,
	  nitems(pciide_cmd_products) },
	{ PCI_VENDOR_VIATECH, pciide_via_products,
	  nitems(pciide_via_products) },
	{ PCI_VENDOR_CONTAQ, pciide_cypress_products,
	  nitems(pciide_cypress_products) },
	{ PCI_VENDOR_SIS, pciide_sis_products,
	  nitems(pciide_sis_products) },
	{ PCI_VENDOR_NS, pciide_natsemi_products,
	  nitems(pciide_natsemi_products) },
	{ PCI_VENDOR_ALI, pciide_acer_products,
	  nitems(pciide_acer_products) },
	{ PCI_VENDOR_TRIONES, pciide_triones_products,
	  nitems(pciide_triones_products) },
	{ PCI_VENDOR_ACARD, pciide_acard_products,
	  nitems(pciide_acard_products) },
	{ PCI_VENDOR_RCC, pciide_serverworks_products,
	  nitems(pciide_serverworks_products) },
	{ PCI_VENDOR_PROMISE, pciide_promise_products,
	  nitems(pciide_promise_products) },
	{ PCI_VENDOR_NVIDIA, pciide_nvidia_products,
	  nitems(pciide_nvidia_products) },
	{ PCI_VENDOR_ITEXPRESS, pciide_ite_products,
	  nitems(pciide_ite_products) },
	{ PCI_VENDOR_ATI, pciide_ati_products,
	  nitems(pciide_ati_products) },
	{ PCI_VENDOR_JMICRON, pciide_jmicron_products,
	  nitems(pciide_jmicron_products) },
	{ PCI_VENDOR_PHISON, pciide_phison_products,
	  nitems(pciide_phison_products) },
	{ PCI_VENDOR_RDC, pciide_rdc_products,
	  nitems(pciide_rdc_products) }
};

/* options passed via the 'flags' config keyword */
#define PCIIDE_OPTIONS_DMA	0x01

int	pciide_match(struct device *, void *, void *);
void	pciide_attach(struct device *, struct device *, void *);
int	pciide_detach(struct device *, int);
int	pciide_activate(struct device *, int);

const struct cfattach pciide_pci_ca = {
	sizeof(struct pciide_softc), pciide_match, pciide_attach,
	pciide_detach, pciide_activate
};

const struct cfattach pciide_jmb_ca = {
	sizeof(struct pciide_softc), pciide_match, pciide_attach,
	pciide_detach, pciide_activate
};

struct cfdriver pciide_cd = {
	NULL, "pciide", DV_DULL
};

const struct pciide_product_desc *pciide_lookup_product(u_int32_t);

const struct pciide_product_desc *
pciide_lookup_product(u_int32_t id)
{
	const struct pciide_product_desc *pp;
	const struct pciide_vendor_desc *vp;
	int i;

	for (i = 0, vp = pciide_vendors; i < nitems(pciide_vendors); vp++, i++)
		if (PCI_VENDOR(id) == vp->ide_vendor)
			break;

	if (i == nitems(pciide_vendors))
		return (NULL);

	for (pp = vp->ide_products, i = 0; i < vp->ide_nproducts; pp++, i++)
		if (PCI_PRODUCT(id) == pp->ide_product)
			break;

	if (i == vp->ide_nproducts)
		return (NULL);
	return (pp);
}

int
pciide_match(struct device *parent, void *match, void *aux)
{
	struct pci_attach_args *pa = aux;
	const struct pciide_product_desc *pp;

	/*
 	 * Some IDE controllers have severe bugs when used in PCI mode.
	 * We punt and attach them to the ISA bus instead.
	 */
	if (PCI_VENDOR(pa->pa_id) == PCI_VENDOR_PCTECH &&
	    PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_PCTECH_RZ1000)
		return (0);

	/*
 	 * Some controllers (e.g. promise Ultra-33) don't claim to be PCI IDE
	 * controllers. Let see if we can deal with it anyway.
	 */
	pp = pciide_lookup_product(pa->pa_id);
	if (pp  && (pp->ide_flags & IDE_PCI_CLASS_OVERRIDE))
		return (1);

	/*
	 * Check the ID register to see that it's a PCI IDE controller.
	 * If it is, we assume that we can deal with it; it _should_
	 * work in a standardized way...
	 */
	if (PCI_CLASS(pa->pa_class) == PCI_CLASS_MASS_STORAGE) {
		switch (PCI_SUBCLASS(pa->pa_class)) {
		case PCI_SUBCLASS_MASS_STORAGE_IDE:
			return (1);

		/*
		 * We only match these if we know they have
		 * a match, as we may not support native interfaces
		 * on them.
		 */
		case PCI_SUBCLASS_MASS_STORAGE_SATA:
		case PCI_SUBCLASS_MASS_STORAGE_RAID:
		case PCI_SUBCLASS_MASS_STORAGE_MISC:
			if (pp)
				return (1);
			else
				return (0);
			break;
		}
	}

	return (0);
}

void
pciide_attach(struct device *parent, struct device *self, void *aux)
{
	struct pciide_softc *sc = (struct pciide_softc *)self;
	struct pci_attach_args *pa = aux;

	sc->sc_pp = pciide_lookup_product(pa->pa_id);
	if (sc->sc_pp == NULL)
		sc->sc_pp = &default_product_desc;
	sc->sc_rev = PCI_REVISION(pa->pa_class);

	sc->sc_pc = pa->pa_pc;
	sc->sc_tag = pa->pa_tag;

	/* Set up DMA defaults; these might be adjusted by chip_map. */
	sc->sc_dma_maxsegsz = IDEDMA_BYTE_COUNT_MAX;
	sc->sc_dma_boundary = IDEDMA_BYTE_COUNT_ALIGN;

	sc->sc_dmacmd_read = pciide_dmacmd_read;
	sc->sc_dmacmd_write = pciide_dmacmd_write;
	sc->sc_dmactl_read = pciide_dmactl_read;
	sc->sc_dmactl_write = pciide_dmactl_write;
	sc->sc_dmatbl_write = pciide_dmatbl_write;

	WDCDEBUG_PRINT((" sc_pc=%p, sc_tag=0x%x, pa_class=0x%x\n", sc->sc_pc,
	    (u_int32_t)sc->sc_tag, pa->pa_class), DEBUG_PROBE);

	if (pciide_skip_ata)
		sc->sc_wdcdev.quirks |= WDC_QUIRK_NOATA;
	if (pciide_skip_atapi)
		sc->sc_wdcdev.quirks |= WDC_QUIRK_NOATAPI;

	sc->sc_pp->chip_map(sc, pa);

	WDCDEBUG_PRINT(("pciide: command/status register=0x%x\n",
	    pci_conf_read(sc->sc_pc, sc->sc_tag, PCI_COMMAND_STATUS_REG)),
	    DEBUG_PROBE);
}

int
pciide_detach(struct device *self, int flags)
{
	struct pciide_softc *sc = (struct pciide_softc *)self;
	if (sc->chip_unmap == NULL)
		panic("unmap not yet implemented for this chipset");
	else
		sc->chip_unmap(sc, flags);

	return 0;
}

int
pciide_activate(struct device *self, int act)
{
	int rv = 0;
	struct pciide_softc *sc = (struct pciide_softc *)self;
	int i;

	switch (act) {
	case DVACT_SUSPEND:
		rv = config_activate_children(self, act);

		for (i = 0; i < nitems(sc->sc_save); i++)
			sc->sc_save[i] = pci_conf_read(sc->sc_pc,
			    sc->sc_tag, PCI_MAPREG_END + 0x18 + (i * 4));

		if (sc->sc_pp->chip_map == sch_chip_map) {
			sc->sc_save2[0] = pci_conf_read(sc->sc_pc,
			    sc->sc_tag, SCH_D0TIM);
			sc->sc_save2[1] = pci_conf_read(sc->sc_pc,
			    sc->sc_tag, SCH_D1TIM);
		} else if (sc->sc_pp->chip_map == piixsata_chip_map) {
			sc->sc_save2[0] = pciide_pci_read(sc->sc_pc,
			    sc->sc_tag, ICH5_SATA_MAP);
			sc->sc_save2[1] = pciide_pci_read(sc->sc_pc,
			    sc->sc_tag, ICH5_SATA_PI);
			sc->sc_save2[2] = pciide_pci_read(sc->sc_pc,
			    sc->sc_tag, ICH_SATA_PCS);
		} else if (sc->sc_pp->chip_map == sii3112_chip_map) {
			sc->sc_save2[0] = pci_conf_read(sc->sc_pc,
			    sc->sc_tag, SII3112_SCS_CMD);
			sc->sc_save2[1] = pci_conf_read(sc->sc_pc,
			    sc->sc_tag, SII3112_PCI_CFGCTL);
		} else if (sc->sc_pp->chip_map == ite_chip_map) {
			sc->sc_save2[0] = pci_conf_read(sc->sc_pc,
			    sc->sc_tag, IT_TIM(0));
		} else if (sc->sc_pp->chip_map == nforce_chip_map) {
			sc->sc_save2[0] = pci_conf_read(sc->sc_pc,
			    sc->sc_tag, NFORCE_PIODMATIM);
			sc->sc_save2[1] = pci_conf_read(sc->sc_pc,
			    sc->sc_tag, NFORCE_PIOTIM);
			sc->sc_save2[2] = pci_conf_read(sc->sc_pc,
			    sc->sc_tag, NFORCE_UDMATIM);
		}
		break;
	case DVACT_RESUME:
		for (i = 0; i < nitems(sc->sc_save); i++)
			pci_conf_write(sc->sc_pc, sc->sc_tag,
			    PCI_MAPREG_END + 0x18 + (i * 4),
			    sc->sc_save[i]);

		if (sc->sc_pp->chip_map == default_chip_map ||
		    sc->sc_pp->chip_map == sata_chip_map ||
		    sc->sc_pp->chip_map == piix_chip_map ||
		    sc->sc_pp->chip_map == amd756_chip_map ||
		    sc->sc_pp->chip_map == phison_chip_map ||
		    sc->sc_pp->chip_map == rdc_chip_map ||
		    sc->sc_pp->chip_map == ixp_chip_map ||
		    sc->sc_pp->chip_map == acard_chip_map ||
		    sc->sc_pp->chip_map == apollo_chip_map ||
		    sc->sc_pp->chip_map == sis_chip_map) {
			/* nothing to restore -- uses only 0x40 - 0x56 */
		} else if (sc->sc_pp->chip_map == sch_chip_map) {
			pci_conf_write(sc->sc_pc, sc->sc_tag,
			    SCH_D0TIM, sc->sc_save2[0]);
			pci_conf_write(sc->sc_pc, sc->sc_tag,
			    SCH_D1TIM, sc->sc_save2[1]);
		} else if (sc->sc_pp->chip_map == piixsata_chip_map) {
			pciide_pci_write(sc->sc_pc, sc->sc_tag,
			    ICH5_SATA_MAP, sc->sc_save2[0]);
			pciide_pci_write(sc->sc_pc, sc->sc_tag,
			    ICH5_SATA_PI, sc->sc_save2[1]);
			pciide_pci_write(sc->sc_pc, sc->sc_tag,
			    ICH_SATA_PCS, sc->sc_save2[2]);
		} else if (sc->sc_pp->chip_map == sii3112_chip_map) {
			pci_conf_write(sc->sc_pc, sc->sc_tag,
			    SII3112_SCS_CMD, sc->sc_save2[0]);
			delay(50 * 1000);
			pci_conf_write(sc->sc_pc, sc->sc_tag,
			    SII3112_PCI_CFGCTL, sc->sc_save2[1]);
			delay(50 * 1000);
		} else if (sc->sc_pp->chip_map == ite_chip_map) {
			pci_conf_write(sc->sc_pc, sc->sc_tag,
			    IT_TIM(0), sc->sc_save2[0]);
		} else if (sc->sc_pp->chip_map == nforce_chip_map) {
			pci_conf_write(sc->sc_pc, sc->sc_tag,
			    NFORCE_PIODMATIM, sc->sc_save2[0]);
			pci_conf_write(sc->sc_pc, sc->sc_tag,
			    NFORCE_PIOTIM, sc->sc_save2[1]);
			pci_conf_write(sc->sc_pc, sc->sc_tag,
			    NFORCE_UDMATIM, sc->sc_save2[2]);
		} else {
			printf("%s: restore for unknown chip map %x\n",
			    sc->sc_wdcdev.sc_dev.dv_xname,
			    sc->sc_pp->ide_product);
		}

		rv = config_activate_children(self, act);
		break;
	default:
		rv = config_activate_children(self, act);
		break;
	}
	return (rv);
}

int
pciide_mapregs_compat(struct pci_attach_args *pa, struct pciide_channel *cp,
    int compatchan, bus_size_t *cmdsizep, bus_size_t *ctlsizep)
{
	struct pciide_softc *sc = (struct pciide_softc *)cp->wdc_channel.wdc;
	struct channel_softc *wdc_cp = &cp->wdc_channel;
	pcireg_t csr;

	cp->compat = 1;
	*cmdsizep = PCIIDE_COMPAT_CMD_SIZE;
	*ctlsizep = PCIIDE_COMPAT_CTL_SIZE;

	csr = pci_conf_read(sc->sc_pc, sc->sc_tag, PCI_COMMAND_STATUS_REG);
	pci_conf_write(sc->sc_pc, sc->sc_tag, PCI_COMMAND_STATUS_REG,
	    csr | PCI_COMMAND_IO_ENABLE | PCI_COMMAND_MASTER_ENABLE);
	
	wdc_cp->cmd_iot = pa->pa_iot;

	if (bus_space_map(wdc_cp->cmd_iot, PCIIDE_COMPAT_CMD_BASE(compatchan),
	    PCIIDE_COMPAT_CMD_SIZE, 0, &wdc_cp->cmd_ioh) != 0) {
		printf("%s: couldn't map %s cmd regs\n",
		    sc->sc_wdcdev.sc_dev.dv_xname, cp->name);
		return (0);
	}

	wdc_cp->ctl_iot = pa->pa_iot;

	if (bus_space_map(wdc_cp->ctl_iot, PCIIDE_COMPAT_CTL_BASE(compatchan),
	    PCIIDE_COMPAT_CTL_SIZE, 0, &wdc_cp->ctl_ioh) != 0) {
		printf("%s: couldn't map %s ctl regs\n",
		    sc->sc_wdcdev.sc_dev.dv_xname, cp->name);
		bus_space_unmap(wdc_cp->cmd_iot, wdc_cp->cmd_ioh,
		    PCIIDE_COMPAT_CMD_SIZE);
		return (0);
	}
	wdc_cp->cmd_iosz = *cmdsizep;
	wdc_cp->ctl_iosz = *ctlsizep;

	return (1);
}

int
pciide_unmapregs_compat(struct pciide_softc *sc, struct pciide_channel *cp)
{
	struct channel_softc *wdc_cp = &cp->wdc_channel;

	bus_space_unmap(wdc_cp->cmd_iot, wdc_cp->cmd_ioh, wdc_cp->cmd_iosz);
	bus_space_unmap(wdc_cp->ctl_iot, wdc_cp->cmd_ioh, wdc_cp->ctl_iosz);

	if (sc->sc_pci_ih != NULL) {
		pciide_machdep_compat_intr_disestablish(sc->sc_pc, sc->sc_pci_ih);
		sc->sc_pci_ih = NULL;
	}

	return (0);
}

int
pciide_mapregs_native(struct pci_attach_args *pa, struct pciide_channel *cp,
    bus_size_t *cmdsizep, bus_size_t *ctlsizep, int (*pci_intr)(void *))
{
	struct pciide_softc *sc = (struct pciide_softc *)cp->wdc_channel.wdc;
	struct channel_softc *wdc_cp = &cp->wdc_channel;
	const char *intrstr;
	pci_intr_handle_t intrhandle;
	pcireg_t maptype;

	cp->compat = 0;

	if (sc->sc_pci_ih == NULL) {
		if (pci_intr_map(pa, &intrhandle) != 0) {
			printf("%s: couldn't map native-PCI interrupt\n",
			    sc->sc_wdcdev.sc_dev.dv_xname);
			return (0);
		}
		intrstr = pci_intr_string(pa->pa_pc, intrhandle);
		sc->sc_pci_ih = pci_intr_establish(pa->pa_pc,
		    intrhandle, IPL_BIO, pci_intr, sc,
		    sc->sc_wdcdev.sc_dev.dv_xname);
		if (sc->sc_pci_ih != NULL) {
			printf("%s: using %s for native-PCI interrupt\n",
			    sc->sc_wdcdev.sc_dev.dv_xname,
			    intrstr ? intrstr : "unknown interrupt");
		} else {
			printf("%s: couldn't establish native-PCI interrupt",
			    sc->sc_wdcdev.sc_dev.dv_xname);
			if (intrstr != NULL)
				printf(" at %s", intrstr);
			printf("\n");
			return (0);
		}
	}
	cp->ih = sc->sc_pci_ih;
	sc->sc_pc = pa->pa_pc;

	maptype = pci_mapreg_type(pa->pa_pc, pa->pa_tag,
	    PCIIDE_REG_CMD_BASE(wdc_cp->channel));
	WDCDEBUG_PRINT(("%s: %s cmd regs mapping: %s\n",
	    sc->sc_wdcdev.sc_dev.dv_xname, cp->name,
	    (maptype == PCI_MAPREG_TYPE_IO ? "I/O" : "memory")), DEBUG_PROBE);
	if (pci_mapreg_map(pa, PCIIDE_REG_CMD_BASE(wdc_cp->channel),
	    maptype, 0,
	    &wdc_cp->cmd_iot, &wdc_cp->cmd_ioh, NULL, cmdsizep, 0) != 0) {
		printf("%s: couldn't map %s cmd regs\n",
		    sc->sc_wdcdev.sc_dev.dv_xname, cp->name);
		return (0);
	}

	maptype = pci_mapreg_type(pa->pa_pc, pa->pa_tag,
	    PCIIDE_REG_CTL_BASE(wdc_cp->channel));
	WDCDEBUG_PRINT(("%s: %s ctl regs mapping: %s\n",
	    sc->sc_wdcdev.sc_dev.dv_xname, cp->name,
	    (maptype == PCI_MAPREG_TYPE_IO ? "I/O": "memory")), DEBUG_PROBE);
	if (pci_mapreg_map(pa, PCIIDE_REG_CTL_BASE(wdc_cp->channel),
	    maptype, 0,
	    &wdc_cp->ctl_iot, &cp->ctl_baseioh, NULL, ctlsizep, 0) != 0) {
		printf("%s: couldn't map %s ctl regs\n",
		    sc->sc_wdcdev.sc_dev.dv_xname, cp->name);
		bus_space_unmap(wdc_cp->cmd_iot, wdc_cp->cmd_ioh, *cmdsizep);
		return (0);
	}
	/*
	 * In native mode, 4 bytes of I/O space are mapped for the control
	 * register, the control register is at offset 2. Pass the generic
	 * code a handle for only one byte at the right offset.
	 */
	if (bus_space_subregion(wdc_cp->ctl_iot, cp->ctl_baseioh, 2, 1,
	    &wdc_cp->ctl_ioh) != 0) {
		printf("%s: unable to subregion %s ctl regs\n",
		    sc->sc_wdcdev.sc_dev.dv_xname, cp->name);
		bus_space_unmap(wdc_cp->cmd_iot, wdc_cp->cmd_ioh, *cmdsizep);
		bus_space_unmap(wdc_cp->cmd_iot, cp->ctl_baseioh, *ctlsizep);
		return (0);
	}
	wdc_cp->cmd_iosz = *cmdsizep;
	wdc_cp->ctl_iosz = *ctlsizep;

	return (1);
}

int
pciide_unmapregs_native(struct pciide_softc *sc, struct pciide_channel *cp)
{
	struct channel_softc *wdc_cp = &cp->wdc_channel;

	bus_space_unmap(wdc_cp->cmd_iot, wdc_cp->cmd_ioh, wdc_cp->cmd_iosz);

	/* Unmap the whole control space, not just the sub-region */
	bus_space_unmap(wdc_cp->ctl_iot, cp->ctl_baseioh, wdc_cp->ctl_iosz);

	if (sc->sc_pci_ih != NULL) {
		pci_intr_disestablish(sc->sc_pc, sc->sc_pci_ih);
		sc->sc_pci_ih = NULL;
	}

	return (0);
}

void
pciide_mapreg_dma(struct pciide_softc *sc, struct pci_attach_args *pa)
{
	pcireg_t maptype;
	bus_addr_t addr;

	/*
	 * Map DMA registers
	 *
	 * Note that sc_dma_ok is the right variable to test to see if
	 * DMA can be done.  If the interface doesn't support DMA,
	 * sc_dma_ok will never be non-zero.  If the DMA regs couldn't
	 * be mapped, it'll be zero.  I.e., sc_dma_ok will only be
	 * non-zero if the interface supports DMA and the registers
	 * could be mapped.
	 *
	 * XXX Note that despite the fact that the Bus Master IDE specs
	 * XXX say that "The bus master IDE function uses 16 bytes of IO
	 * XXX space", some controllers (at least the United
	 * XXX Microelectronics UM8886BF) place it in memory space.
	 */

	maptype = pci_mapreg_type(pa->pa_pc, pa->pa_tag,
	    PCIIDE_REG_BUS_MASTER_DMA);

	switch (maptype) {
	case PCI_MAPREG_TYPE_IO:
		sc->sc_dma_ok = (pci_mapreg_info(pa->pa_pc, pa->pa_tag,
		    PCIIDE_REG_BUS_MASTER_DMA, PCI_MAPREG_TYPE_IO,
		    &addr, NULL, NULL) == 0);
		if (sc->sc_dma_ok == 0) {
			printf(", unused (couldn't query registers)");
			break;
		}
		if ((sc->sc_pp->ide_flags & IDE_16BIT_IOSPACE)
		    && addr >= 0x10000) {
			sc->sc_dma_ok = 0;
			printf(", unused (registers at unsafe address %#lx)", addr);
			break;
		}
		/* FALLTHROUGH */

	case PCI_MAPREG_MEM_TYPE_32BIT:
		sc->sc_dma_ok = (pci_mapreg_map(pa,
		    PCIIDE_REG_BUS_MASTER_DMA, maptype, 0,
		    &sc->sc_dma_iot, &sc->sc_dma_ioh, NULL, &sc->sc_dma_iosz,
		    0) == 0);
		sc->sc_dmat = pa->pa_dmat;
		if (sc->sc_dma_ok == 0) {
			printf(", unused (couldn't map registers)");
		} else {
			sc->sc_wdcdev.dma_arg = sc;
			sc->sc_wdcdev.dma_init = pciide_dma_init;
			sc->sc_wdcdev.dma_start = pciide_dma_start;
			sc->sc_wdcdev.dma_finish = pciide_dma_finish;
		}
		break;

	default:
		sc->sc_dma_ok = 0;
		printf(", (unsupported maptype 0x%x)", maptype);
		break;
	}
}

void
pciide_unmapreg_dma(struct pciide_softc *sc)
{
	bus_space_unmap(sc->sc_dma_iot, sc->sc_dma_ioh, sc->sc_dma_iosz);
}

int
pciide_intr_flag(struct pciide_channel *cp)
{
	struct pciide_softc *sc = (struct pciide_softc *)cp->wdc_channel.wdc;
	int chan = cp->wdc_channel.channel;

	if (cp->dma_in_progress) {
		int retry = 10;
		int status;

		/* Check the status register */
		for (retry = 10; retry > 0; retry--) {
			status = PCIIDE_DMACTL_READ(sc, chan);
			if (status & IDEDMA_CTL_INTR) {
				break;
			}
			DELAY(5);
		}

		/* Not for us.  */
		if (retry == 0)
			return (0);

		return (1);
	}

	return (-1);
}

int
pciide_compat_intr(void *arg)
{
	struct pciide_channel *cp = arg;

	if (pciide_intr_flag(cp) == 0)
		return (0);

#ifdef DIAGNOSTIC
	/* should only be called for a compat channel */
	if (cp->compat == 0)
		panic("pciide compat intr called for non-compat chan %p", cp);
#endif
	return (wdcintr(&cp->wdc_channel));
}

int
pciide_pci_intr(void *arg)
{
	struct pciide_softc *sc = arg;
	struct pciide_channel *cp;
	struct channel_softc *wdc_cp;
	int i, rv, crv;

	rv = 0;
	for (i = 0; i < sc->sc_wdcdev.nchannels; i++) {
		cp = &sc->pciide_channels[i];
		wdc_cp = &cp->wdc_channel;

		/* If a compat channel skip. */
		if (cp->compat)
			continue;

		if (cp->hw_ok == 0)
			continue;

		if (pciide_intr_flag(cp) == 0)
			continue;

		crv = wdcintr(wdc_cp);
		if (crv == 0)
			;		/* leave rv alone */
		else if (crv == 1)
			rv = 1;		/* claim the intr */
		else if (rv == 0)	/* crv should be -1 in this case */
			rv = crv;	/* if we've done no better, take it */
	}
	return (rv);
}

u_int8_t
pciide_dmacmd_read(struct pciide_softc *sc, int chan)
{
	return (bus_space_read_1(sc->sc_dma_iot, sc->sc_dma_ioh,
	    IDEDMA_CMD(chan)));
}

void
pciide_dmacmd_write(struct pciide_softc *sc, int chan, u_int8_t val)
{
	bus_space_write_1(sc->sc_dma_iot, sc->sc_dma_ioh,
	    IDEDMA_CMD(chan), val);
}

u_int8_t
pciide_dmactl_read(struct pciide_softc *sc, int chan)
{
	return (bus_space_read_1(sc->sc_dma_iot, sc->sc_dma_ioh,
	    IDEDMA_CTL(chan)));
}

void
pciide_dmactl_write(struct pciide_softc *sc, int chan, u_int8_t val)
{
	bus_space_write_1(sc->sc_dma_iot, sc->sc_dma_ioh,
	    IDEDMA_CTL(chan), val);
}

void
pciide_dmatbl_write(struct pciide_softc *sc, int chan, u_int32_t val)
{
	bus_space_write_4(sc->sc_dma_iot, sc->sc_dma_ioh,
	    IDEDMA_TBL(chan), val);
}

void
pciide_channel_dma_setup(struct pciide_channel *cp)
{
	int drive;
	struct pciide_softc *sc = (struct pciide_softc *)cp->wdc_channel.wdc;
	struct ata_drive_datas *drvp;

	for (drive = 0; drive < 2; drive++) {
		drvp = &cp->wdc_channel.ch_drive[drive];
		/* If no drive, skip */
		if ((drvp->drive_flags & DRIVE) == 0)
			continue;
		/* setup DMA if needed */
		if (((drvp->drive_flags & DRIVE_DMA) == 0 &&
		    (drvp->drive_flags & DRIVE_UDMA) == 0) ||
		    sc->sc_dma_ok == 0) {
			drvp->drive_flags &= ~(DRIVE_DMA | DRIVE_UDMA);
			continue;
		}
		if (pciide_dma_table_setup(sc, cp->wdc_channel.channel, drive)
		    != 0) {
			/* Abort DMA setup */
			drvp->drive_flags &= ~(DRIVE_DMA | DRIVE_UDMA);
			continue;
		}
	}
}

int
pciide_dma_table_setup(struct pciide_softc *sc, int channel, int drive)
{
	bus_dma_segment_t seg;
	int error, rseg;
	const bus_size_t dma_table_size =
	    sizeof(struct idedma_table) * NIDEDMA_TABLES;
	struct pciide_dma_maps *dma_maps =
	    &sc->pciide_channels[channel].dma_maps[drive];

	/* If table was already allocated, just return */
	if (dma_maps->dma_table)
		return (0);

	/* Allocate memory for the DMA tables and map it */
	if ((error = bus_dmamem_alloc(sc->sc_dmat, dma_table_size,
	    IDEDMA_TBL_ALIGN, IDEDMA_TBL_ALIGN, &seg, 1, &rseg,
	    BUS_DMA_NOWAIT)) != 0) {
		printf("%s:%d: unable to allocate table DMA for "
		    "drive %d, error=%d\n", sc->sc_wdcdev.sc_dev.dv_xname,
		    channel, drive, error);
		return (error);
	}

	if ((error = bus_dmamem_map(sc->sc_dmat, &seg, rseg,
	    dma_table_size,
	    (caddr_t *)&dma_maps->dma_table,
	    BUS_DMA_NOWAIT|BUS_DMA_COHERENT)) != 0) {
		printf("%s:%d: unable to map table DMA for"
		    "drive %d, error=%d\n", sc->sc_wdcdev.sc_dev.dv_xname,
		    channel, drive, error);
		return (error);
	}

	WDCDEBUG_PRINT(("pciide_dma_table_setup: table at %p len %ld, "
	    "phy 0x%lx\n", dma_maps->dma_table, dma_table_size,
	    seg.ds_addr), DEBUG_PROBE);

	/* Create and load table DMA map for this disk */
	if ((error = bus_dmamap_create(sc->sc_dmat, dma_table_size,
	    1, dma_table_size, IDEDMA_TBL_ALIGN, BUS_DMA_NOWAIT,
	    &dma_maps->dmamap_table)) != 0) {
		printf("%s:%d: unable to create table DMA map for "
		    "drive %d, error=%d\n", sc->sc_wdcdev.sc_dev.dv_xname,
		    channel, drive, error);
		return (error);
	}
	if ((error = bus_dmamap_load(sc->sc_dmat,
	    dma_maps->dmamap_table,
	    dma_maps->dma_table,
	    dma_table_size, NULL, BUS_DMA_NOWAIT)) != 0) {
		printf("%s:%d: unable to load table DMA map for "
		    "drive %d, error=%d\n", sc->sc_wdcdev.sc_dev.dv_xname,
		    channel, drive, error);
		return (error);
	}
	WDCDEBUG_PRINT(("pciide_dma_table_setup: phy addr of table 0x%lx\n",
	    dma_maps->dmamap_table->dm_segs[0].ds_addr), DEBUG_PROBE);
	/* Create a xfer DMA map for this drive */
	if ((error = bus_dmamap_create(sc->sc_dmat, IDEDMA_BYTE_COUNT_MAX,
	    NIDEDMA_TABLES, sc->sc_dma_maxsegsz, sc->sc_dma_boundary,
	    BUS_DMA_NOWAIT | BUS_DMA_ALLOCNOW,
	    &dma_maps->dmamap_xfer)) != 0) {
		printf("%s:%d: unable to create xfer DMA map for "
		    "drive %d, error=%d\n", sc->sc_wdcdev.sc_dev.dv_xname,
		    channel, drive, error);
		return (error);
	}
	return (0);
}

int
pciide_dma_init(void *v, int channel, int drive, void *databuf,
    size_t datalen, int flags)
{
	struct pciide_softc *sc = v;
	int error, seg;
	struct pciide_channel *cp = &sc->pciide_channels[channel];
	struct pciide_dma_maps *dma_maps =
	    &sc->pciide_channels[channel].dma_maps[drive];
#ifndef BUS_DMA_RAW
#define BUS_DMA_RAW 0
#endif

	error = bus_dmamap_load(sc->sc_dmat,
	    dma_maps->dmamap_xfer,
	    databuf, datalen, NULL, BUS_DMA_NOWAIT|BUS_DMA_RAW);
	if (error) {
		printf("%s:%d: unable to load xfer DMA map for "
		    "drive %d, error=%d\n", sc->sc_wdcdev.sc_dev.dv_xname,
		    channel, drive, error);
		return (error);
	}

	bus_dmamap_sync(sc->sc_dmat, dma_maps->dmamap_xfer, 0,
	    dma_maps->dmamap_xfer->dm_mapsize,
	    (flags & WDC_DMA_READ) ?
	    BUS_DMASYNC_PREREAD : BUS_DMASYNC_PREWRITE);

	for (seg = 0; seg < dma_maps->dmamap_xfer->dm_nsegs; seg++) {
#ifdef DIAGNOSTIC
		/* A segment must not cross a 64k boundary */
		{
		u_long phys = dma_maps->dmamap_xfer->dm_segs[seg].ds_addr;
		u_long len = dma_maps->dmamap_xfer->dm_segs[seg].ds_len;
		if ((phys & ~IDEDMA_BYTE_COUNT_MASK) !=
		    ((phys + len - 1) & ~IDEDMA_BYTE_COUNT_MASK)) {
			printf("pciide_dma: segment %d physical addr 0x%lx"
			    " len 0x%lx not properly aligned\n",
			    seg, phys, len);
			panic("pciide_dma: buf align");
		}
		}
#endif
		dma_maps->dma_table[seg].base_addr =
		    htole32(dma_maps->dmamap_xfer->dm_segs[seg].ds_addr);
		dma_maps->dma_table[seg].byte_count =
		    htole32(dma_maps->dmamap_xfer->dm_segs[seg].ds_len &
		    IDEDMA_BYTE_COUNT_MASK);
		WDCDEBUG_PRINT(("\t seg %d len %d addr 0x%x\n",
		   seg, letoh32(dma_maps->dma_table[seg].byte_count),
		   letoh32(dma_maps->dma_table[seg].base_addr)), DEBUG_DMA);

	}
	dma_maps->dma_table[dma_maps->dmamap_xfer->dm_nsegs -1].byte_count |=
	    htole32(IDEDMA_BYTE_COUNT_EOT);

	bus_dmamap_sync(sc->sc_dmat, dma_maps->dmamap_table, 0,
	    dma_maps->dmamap_table->dm_mapsize,
	    BUS_DMASYNC_PREWRITE);

	/* Maps are ready. Start DMA function */
#ifdef DIAGNOSTIC
	if (dma_maps->dmamap_table->dm_segs[0].ds_addr & ~IDEDMA_TBL_MASK) {
		printf("pciide_dma_init: addr 0x%lx not properly aligned\n",
		    dma_maps->dmamap_table->dm_segs[0].ds_addr);
		panic("pciide_dma_init: table align");
	}
#endif

	/* Clear status bits */
	PCIIDE_DMACTL_WRITE(sc, channel, PCIIDE_DMACTL_READ(sc, channel));
	/* Write table addr */
	PCIIDE_DMATBL_WRITE(sc, channel,
	    dma_maps->dmamap_table->dm_segs[0].ds_addr);
	/* set read/write */
	PCIIDE_DMACMD_WRITE(sc, channel,
	    ((flags & WDC_DMA_READ) ? IDEDMA_CMD_WRITE : 0) | cp->idedma_cmd);
	/* remember flags */
	dma_maps->dma_flags = flags;
	return (0);
}

void
pciide_dma_start(void *v, int channel, int drive)
{
	struct pciide_softc *sc = v;

	WDCDEBUG_PRINT(("pciide_dma_start\n"), DEBUG_XFERS);
	PCIIDE_DMACMD_WRITE(sc, channel, PCIIDE_DMACMD_READ(sc, channel) |
	    IDEDMA_CMD_START);

	sc->pciide_channels[channel].dma_in_progress = 1;
}

int
pciide_dma_finish(void *v, int channel, int drive, int force)
{
	struct pciide_softc *sc = v;
	struct pciide_channel *cp = &sc->pciide_channels[channel];
	u_int8_t status;
	int error = 0;
	struct pciide_dma_maps *dma_maps =
	    &sc->pciide_channels[channel].dma_maps[drive];

	status = PCIIDE_DMACTL_READ(sc, channel);
	WDCDEBUG_PRINT(("pciide_dma_finish: status 0x%x\n", status),
	    DEBUG_XFERS);
	if (status == 0xff)
		return (status);

	if (force == 0 && (status & IDEDMA_CTL_INTR) == 0) {
		error = WDC_DMAST_NOIRQ;
		goto done;
	}

	/* stop DMA channel */
	PCIIDE_DMACMD_WRITE(sc, channel,
	    ((dma_maps->dma_flags & WDC_DMA_READ) ?
	    0x00 : IDEDMA_CMD_WRITE) | cp->idedma_cmd);

	/* Unload the map of the data buffer */
	bus_dmamap_sync(sc->sc_dmat, dma_maps->dmamap_xfer, 0,
	    dma_maps->dmamap_xfer->dm_mapsize,
	    (dma_maps->dma_flags & WDC_DMA_READ) ?
	    BUS_DMASYNC_POSTREAD : BUS_DMASYNC_POSTWRITE);
	bus_dmamap_unload(sc->sc_dmat, dma_maps->dmamap_xfer);

	/* Clear status bits */
	PCIIDE_DMACTL_WRITE(sc, channel, status);

	if ((status & IDEDMA_CTL_ERR) != 0) {
		printf("%s:%d:%d: bus-master DMA error: status=0x%x\n",
		    sc->sc_wdcdev.sc_dev.dv_xname, channel, drive, status);
		error |= WDC_DMAST_ERR;
	}

	if ((status & IDEDMA_CTL_INTR) == 0) {
		printf("%s:%d:%d: bus-master DMA error: missing interrupt, "
		    "status=0x%x\n", sc->sc_wdcdev.sc_dev.dv_xname, channel,
		    drive, status);
		error |= WDC_DMAST_NOIRQ;
	}

	if ((status & IDEDMA_CTL_ACT) != 0) {
		/* data underrun, may be a valid condition for ATAPI */
		error |= WDC_DMAST_UNDER;
	}

done:
	sc->pciide_channels[channel].dma_in_progress = 0;
	return (error);
}

void
pciide_irqack(struct channel_softc *chp)
{
	struct pciide_channel *cp = (struct pciide_channel *)chp;
	struct pciide_softc *sc = (struct pciide_softc *)cp->wdc_channel.wdc;
	int chan = chp->channel;

	/* clear status bits in IDE DMA registers */
	PCIIDE_DMACTL_WRITE(sc, chan, PCIIDE_DMACTL_READ(sc, chan));
}

/* some common code used by several chip_map */
int
pciide_chansetup(struct pciide_softc *sc, int channel, pcireg_t interface)
{
	struct pciide_channel *cp = &sc->pciide_channels[channel];
	sc->wdc_chanarray[channel] = &cp->wdc_channel;
	cp->name = PCIIDE_CHANNEL_NAME(channel);
	cp->wdc_channel.channel = channel;
	cp->wdc_channel.wdc = &sc->sc_wdcdev;
	cp->wdc_channel.ch_queue = wdc_alloc_queue();
	if (cp->wdc_channel.ch_queue == NULL) {
		printf("%s: %s "
		    "cannot allocate channel queue",
		    sc->sc_wdcdev.sc_dev.dv_xname, cp->name);
		return (0);
	}
	cp->hw_ok = 1;

	return (1);
}

void
pciide_chanfree(struct pciide_softc *sc, int channel)
{
	struct pciide_channel *cp = &sc->pciide_channels[channel];
	if (cp->wdc_channel.ch_queue)
		wdc_free_queue(cp->wdc_channel.ch_queue);
}

/* some common code used by several chip channel_map */
void
pciide_mapchan(struct pci_attach_args *pa, struct pciide_channel *cp,
    pcireg_t interface, bus_size_t *cmdsizep, bus_size_t *ctlsizep,
    int (*pci_intr)(void *))
{
	struct channel_softc *wdc_cp = &cp->wdc_channel;

	if (interface & PCIIDE_INTERFACE_PCI(wdc_cp->channel))
		cp->hw_ok = pciide_mapregs_native(pa, cp, cmdsizep, ctlsizep,
		    pci_intr);
	else
		cp->hw_ok = pciide_mapregs_compat(pa, cp,
		    wdc_cp->channel, cmdsizep, ctlsizep);
	if (cp->hw_ok == 0)
		return;
	wdc_cp->data32iot = wdc_cp->cmd_iot;
	wdc_cp->data32ioh = wdc_cp->cmd_ioh;
	wdcattach(wdc_cp);
}

void
pciide_unmap_chan(struct pciide_softc *sc, struct pciide_channel *cp, int flags)
{
	struct channel_softc *wdc_cp = &cp->wdc_channel;

	wdcdetach(wdc_cp, flags);

	if (cp->compat != 0)
		pciide_unmapregs_compat(sc, cp);
	else
		pciide_unmapregs_native(sc, cp);
}

/*
 * Generic code to call to know if a channel can be disabled. Return 1
 * if channel can be disabled, 0 if not
 */
int
pciide_chan_candisable(struct pciide_channel *cp)
{
	struct pciide_softc *sc = (struct pciide_softc *)cp->wdc_channel.wdc;
	struct channel_softc *wdc_cp = &cp->wdc_channel;

	if ((wdc_cp->ch_drive[0].drive_flags & DRIVE) == 0 &&
	    (wdc_cp->ch_drive[1].drive_flags & DRIVE) == 0) {
		printf("%s: %s disabled (no drives)\n",
		    sc->sc_wdcdev.sc_dev.dv_xname, cp->name);
		cp->hw_ok = 0;
		return (1);
	}
	return (0);
}

/*
 * generic code to map the compat intr if hw_ok=1 and it is a compat channel.
 * Set hw_ok=0 on failure
 */
void
pciide_map_compat_intr(struct pci_attach_args *pa, struct pciide_channel *cp,
    int compatchan, int interface)
{
	struct pciide_softc *sc = (struct pciide_softc *)cp->wdc_channel.wdc;
	struct channel_softc *wdc_cp = &cp->wdc_channel;

	if ((interface & PCIIDE_INTERFACE_PCI(wdc_cp->channel)) != 0)
		return;

	cp->compat = 1;
	cp->ih = pciide_machdep_compat_intr_establish(&sc->sc_wdcdev.sc_dev,
	    pa, compatchan, pciide_compat_intr, cp);
	if (cp->ih == NULL) {
		printf("%s: no compatibility interrupt for use by %s\n",
		    sc->sc_wdcdev.sc_dev.dv_xname, cp->name);
		cp->hw_ok = 0;
	}
}

/*
 * generic code to unmap the compat intr if hw_ok=1 and it is a compat channel.
 * Set hw_ok=0 on failure
 */
void
pciide_unmap_compat_intr(struct pci_attach_args *pa, struct pciide_channel *cp,
    int compatchan, int interface)
{
	struct channel_softc *wdc_cp = &cp->wdc_channel;

	if ((interface & PCIIDE_INTERFACE_PCI(wdc_cp->channel)) != 0)
		return;

	pciide_machdep_compat_intr_disestablish(pa->pa_pc, cp->ih);
}

void
pciide_print_channels(int nchannels, pcireg_t interface)
{
	int i;

	for (i = 0; i < nchannels; i++) {
		printf(", %s %s to %s", PCIIDE_CHANNEL_NAME(i),
		    (interface & PCIIDE_INTERFACE_SETTABLE(i)) ?
		    "configured" : "wired",
		    (interface & PCIIDE_INTERFACE_PCI(i)) ? "native-PCI" :
		    "compatibility");
	}

	printf("\n");
}

void
pciide_print_modes(struct pciide_channel *cp)
{
	wdc_print_current_modes(&cp->wdc_channel);
}

void
default_chip_map(struct pciide_softc *sc, struct pci_attach_args *pa)
{
	struct pciide_channel *cp;
	pcireg_t interface = PCI_INTERFACE(pa->pa_class);
	pcireg_t csr;
	int channel, drive;
	struct ata_drive_datas *drvp;
	u_int8_t idedma_ctl;
	bus_size_t cmdsize, ctlsize;
	char *failreason;

	if (interface & PCIIDE_INTERFACE_BUS_MASTER_DMA) {
		printf(": DMA");
		if (sc->sc_pp == &default_product_desc &&
		    (sc->sc_wdcdev.sc_dev.dv_cfdata->cf_flags &
		    PCIIDE_OPTIONS_DMA) == 0) {
			printf(" (unsupported)");
			sc->sc_dma_ok = 0;
		} else {
			pciide_mapreg_dma(sc, pa);
			if (sc->sc_dma_ok != 0)
				printf(", (partial support)");
		}
	} else {
		printf(": no DMA");
		sc->sc_dma_ok = 0;
	}
	if (sc->sc_dma_ok) {
		sc->sc_wdcdev.cap |= WDC_CAPABILITY_DMA | WDC_CAPABILITY_IRQACK;
		sc->sc_wdcdev.irqack = pciide_irqack;
	}
	sc->sc_wdcdev.PIO_cap = 0;
	sc->sc_wdcdev.DMA_cap = 0;
	sc->sc_wdcdev.channels = sc->wdc_chanarray;
	sc->sc_wdcdev.nchannels = PCIIDE_NUM_CHANNELS;
	sc->sc_wdcdev.cap |= WDC_CAPABILITY_DATA16;

	pciide_print_channels(sc->sc_wdcdev.nchannels, interface);

	for (channel = 0; channel < sc->sc_wdcdev.nchannels; channel++) {
		cp = &sc->pciide_channels[channel];
		if (pciide_chansetup(sc, channel, interface) == 0)
			continue;
		if (interface & PCIIDE_INTERFACE_PCI(channel)) {
			cp->hw_ok = pciide_mapregs_native(pa, cp, &cmdsize,
			    &ctlsize, pciide_pci_intr);
		} else {
			cp->hw_ok = pciide_mapregs_compat(pa, cp,
			    channel, &cmdsize, &ctlsize);
		}
		if (cp->hw_ok == 0)
			continue;
		/*
		 * Check to see if something appears to be there.
		 */
		failreason = NULL;
		pciide_map_compat_intr(pa, cp, channel, interface);
		if (cp->hw_ok == 0)
			continue;
		if (!wdcprobe(&cp->wdc_channel)) {
			failreason = "not responding; disabled or no drives?";
			goto next;
		}
		/*
		 * Now, make sure it's actually attributable to this PCI IDE
		 * channel by trying to access the channel again while the
		 * PCI IDE controller's I/O space is disabled.  (If the
		 * channel no longer appears to be there, it belongs to
		 * this controller.)  YUCK!
		 */
		csr = pci_conf_read(sc->sc_pc, sc->sc_tag,
	  	    PCI_COMMAND_STATUS_REG);
		pci_conf_write(sc->sc_pc, sc->sc_tag, PCI_COMMAND_STATUS_REG,
		    csr & ~PCI_COMMAND_IO_ENABLE);
		if (wdcprobe(&cp->wdc_channel))
			failreason = "other hardware responding at addresses";
		pci_conf_write(sc->sc_pc, sc->sc_tag,
		    PCI_COMMAND_STATUS_REG, csr);
next:
		if (failreason) {
			printf("%s: %s ignored (%s)\n",
			    sc->sc_wdcdev.sc_dev.dv_xname, cp->name,
			    failreason);
			cp->hw_ok = 0;
			pciide_unmap_compat_intr(pa, cp, channel, interface);
			bus_space_unmap(cp->wdc_channel.cmd_iot,
			    cp->wdc_channel.cmd_ioh, cmdsize);
			if (interface & PCIIDE_INTERFACE_PCI(channel))
				bus_space_unmap(cp->wdc_channel.ctl_iot,
				    cp->ctl_baseioh, ctlsize);
			else
				bus_space_unmap(cp->wdc_channel.ctl_iot,
				    cp->wdc_channel.ctl_ioh, ctlsize);
		}
		if (cp->hw_ok) {
			cp->wdc_channel.data32iot = cp->wdc_channel.cmd_iot;
			cp->wdc_channel.data32ioh = cp->wdc_channel.cmd_ioh;
			wdcattach(&cp->wdc_channel);
		}
	}

	if (sc->sc_dma_ok == 0)
		return;

	/* Allocate DMA maps */
	for (channel = 0; channel < sc->sc_wdcdev.nchannels; channel++) {
		idedma_ctl = 0;
		cp = &sc->pciide_channels[channel];
		for (drive = 0; drive < 2; drive++) {
			drvp = &cp->wdc_channel.ch_drive[drive];
			/* If no drive, skip */
			if ((drvp->drive_flags & DRIVE) == 0)
				continue;
			if ((drvp->drive_flags & DRIVE_DMA) == 0)
				continue;
			if (pciide_dma_table_setup(sc, channel, drive) != 0) {
				/* Abort DMA setup */
				printf("%s:%d:%d: cannot allocate DMA maps, "
				    "using PIO transfers\n",
				    sc->sc_wdcdev.sc_dev.dv_xname,
				    channel, drive);
				drvp->drive_flags &= ~DRIVE_DMA;
			}
			printf("%s:%d:%d: using DMA data transfers\n",
			    sc->sc_wdcdev.sc_dev.dv_xname,
			    channel, drive);
			idedma_ctl |= IDEDMA_CTL_DRV_DMA(drive);
		}
		if (idedma_ctl != 0) {
			/* Add software bits in status register */
			PCIIDE_DMACTL_WRITE(sc, channel, idedma_ctl);
		}
	}
}

void
default_chip_unmap(struct pciide_softc *sc, int flags)
{
	struct pciide_channel *cp;
	int channel;

	for (channel = 0; channel < sc->sc_wdcdev.nchannels; channel++) {
		cp = &sc->pciide_channels[channel];
		pciide_unmap_chan(sc, cp, flags);
		pciide_chanfree(sc, channel);
	}

	pciide_unmapreg_dma(sc);

	if (sc->sc_cookie)
		free(sc->sc_cookie, M_DEVBUF, sc->sc_cookielen);
}

void
sata_chip_map(struct pciide_softc *sc, struct pci_attach_args *pa)
{
	struct pciide_channel *cp;
	pcireg_t interface = PCI_INTERFACE(pa->pa_class);
	int channel;
	bus_size_t cmdsize, ctlsize;

	if (interface == 0) {
		WDCDEBUG_PRINT(("sata_chip_map interface == 0\n"),
		    DEBUG_PROBE);
		interface = PCIIDE_INTERFACE_BUS_MASTER_DMA |
		    PCIIDE_INTERFACE_PCI(0) | PCIIDE_INTERFACE_PCI(1);
	}

	printf(": DMA");
	pciide_mapreg_dma(sc, pa);
	printf("\n");

	if (sc->sc_dma_ok) {
		sc->sc_wdcdev.cap |= WDC_CAPABILITY_UDMA |
		    WDC_CAPABILITY_DMA | WDC_CAPABILITY_IRQACK;
		sc->sc_wdcdev.irqack = pciide_irqack;
	}
	sc->sc_wdcdev.PIO_cap = 4;
	sc->sc_wdcdev.DMA_cap = 2;
	sc->sc_wdcdev.UDMA_cap = 6;

	sc->sc_wdcdev.channels = sc->wdc_chanarray;
	sc->sc_wdcdev.nchannels = PCIIDE_NUM_CHANNELS;
	sc->sc_wdcdev.cap |= WDC_CAPABILITY_DATA16 | WDC_CAPABILITY_DATA32 |
	    WDC_CAPABILITY_MODE | WDC_CAPABILITY_SATA;
	sc->sc_wdcdev.set_modes = sata_setup_channel;
	sc->chip_unmap = default_chip_unmap;

	for (channel = 0; channel < sc->sc_wdcdev.nchannels; channel++) {
		cp = &sc->pciide_channels[channel];
		if (pciide_chansetup(sc, channel, interface) == 0)
			continue;
		pciide_mapchan(pa, cp, interface, &cmdsize, &ctlsize,
		    pciide_pci_intr);
		sata_setup_channel(&cp->wdc_channel);
	}
}

void
sata_setup_channel(struct channel_softc *chp)
{
	struct ata_drive_datas *drvp;
	int drive;
	u_int32_t idedma_ctl;
	struct pciide_channel *cp = (struct pciide_channel *)chp;
	struct pciide_softc *sc = (struct pciide_softc *)cp->wdc_channel.wdc;

	/* setup DMA if needed */
	pciide_channel_dma_setup(cp);

	idedma_ctl = 0;

	for (drive = 0; drive < 2; drive++) {
		drvp = &chp->ch_drive[drive];
		/* If no drive, skip */
		if ((drvp->drive_flags & DRIVE) == 0)
			continue;
		if (drvp->drive_flags & DRIVE_UDMA) {
			/* use Ultra/DMA */
			drvp->drive_flags &= ~DRIVE_DMA;
			idedma_ctl |= IDEDMA_CTL_DRV_DMA(drive);
		} else if (drvp->drive_flags & DRIVE_DMA) {
			idedma_ctl |= IDEDMA_CTL_DRV_DMA(drive);
		}
	}

	/*
	 * Nothing to do to setup modes; it is meaningless in S-ATA
	 * (but many S-ATA drives still want to get the SET_FEATURE
	 * command).
	 */
	if (idedma_ctl != 0) {
		/* Add software bits in status register */
		PCIIDE_DMACTL_WRITE(sc, chp->channel, idedma_ctl);
	}
	pciide_print_modes(cp);
}

void
piix_timing_debug(struct pciide_softc *sc)
{
	WDCDEBUG_PRINT(("piix_setup_chip: idetim=0x%x",
	    pci_conf_read(sc->sc_pc, sc->sc_tag, PIIX_IDETIM)),
	    DEBUG_PROBE);
	if (sc->sc_pp->ide_product != PCI_PRODUCT_INTEL_82371FB_IDE &&
	    sc->sc_pp->ide_product != PCI_PRODUCT_INTEL_82371FB_ISA) {
		WDCDEBUG_PRINT((", sidetim=0x%x",
		    pci_conf_read(sc->sc_pc, sc->sc_tag, PIIX_SIDETIM)),
		    DEBUG_PROBE);
		if (sc->sc_wdcdev.cap & WDC_CAPABILITY_UDMA) {
			WDCDEBUG_PRINT((", udmareg 0x%x",
			    pci_conf_read(sc->sc_pc, sc->sc_tag, PIIX_UDMAREG)),
			    DEBUG_PROBE);
		}
		if (sc->sc_pp->ide_product == PCI_PRODUCT_INTEL_6300ESB_IDE ||
		    sc->sc_pp->ide_product == PCI_PRODUCT_INTEL_6321ESB_IDE ||
		    sc->sc_pp->ide_product == PCI_PRODUCT_INTEL_82801AA_IDE ||
		    sc->sc_pp->ide_product == PCI_PRODUCT_INTEL_82801AB_IDE ||
		    sc->sc_pp->ide_product == PCI_PRODUCT_INTEL_82801BAM_IDE ||
		    sc->sc_pp->ide_product == PCI_PRODUCT_INTEL_82801BA_IDE ||
		    sc->sc_pp->ide_product == PCI_PRODUCT_INTEL_82801CAM_IDE ||
		    sc->sc_pp->ide_product == PCI_PRODUCT_INTEL_82801CA_IDE ||
		    sc->sc_pp->ide_product == PCI_PRODUCT_INTEL_82801DB_IDE ||
		    sc->sc_pp->ide_product == PCI_PRODUCT_INTEL_82801DBL_IDE ||
		    sc->sc_pp->ide_product == PCI_PRODUCT_INTEL_82801DBM_IDE ||
		    sc->sc_pp->ide_product == PCI_PRODUCT_INTEL_82801EB_IDE ||
		    sc->sc_pp->ide_product == PCI_PRODUCT_INTEL_82801FB_IDE ||
		    sc->sc_pp->ide_product == PCI_PRODUCT_INTEL_82801GB_IDE ||
		    sc->sc_pp->ide_product == PCI_PRODUCT_INTEL_82801HBM_IDE ||
		    sc->sc_pp->ide_product == PCI_PRODUCT_INTEL_82372FB_IDE) {
			WDCDEBUG_PRINT((", IDE_CONTROL 0x%x",
			    pci_conf_read(sc->sc_pc, sc->sc_tag, PIIX_CONFIG)),
			    DEBUG_PROBE);
		}
	}
	WDCDEBUG_PRINT(("\n"), DEBUG_PROBE);
}

void
piix_chip_map(struct pciide_softc *sc, struct pci_attach_args *pa)
{
	struct pciide_channel *cp;
	int channel;
	u_int32_t idetim;
	bus_size_t cmdsize, ctlsize;

	pcireg_t interface = PCI_INTERFACE(pa->pa_class);

	printf(": DMA");
	pciide_mapreg_dma(sc, pa);
	sc->sc_wdcdev.cap |= WDC_CAPABILITY_DATA16 | WDC_CAPABILITY_DATA32 |
	    WDC_CAPABILITY_MODE;
	if (sc->sc_dma_ok) {
		sc->sc_wdcdev.cap |= WDC_CAPABILITY_DMA | WDC_CAPABILITY_IRQACK;
		sc->sc_wdcdev.irqack = pciide_irqack;
		switch (sc->sc_pp->ide_product) {
		case PCI_PRODUCT_INTEL_6300ESB_IDE:
		case PCI_PRODUCT_INTEL_6321ESB_IDE:
		case PCI_PRODUCT_INTEL_82371AB_IDE:
		case PCI_PRODUCT_INTEL_82372FB_IDE:
		case PCI_PRODUCT_INTEL_82440MX_IDE:
		case PCI_PRODUCT_INTEL_82451NX:
		case PCI_PRODUCT_INTEL_82801AA_IDE:
		case PCI_PRODUCT_INTEL_82801AB_IDE:
		case PCI_PRODUCT_INTEL_82801BAM_IDE:
		case PCI_PRODUCT_INTEL_82801BA_IDE:
		case PCI_PRODUCT_INTEL_82801CAM_IDE:
		case PCI_PRODUCT_INTEL_82801CA_IDE:
		case PCI_PRODUCT_INTEL_82801DB_IDE:
		case PCI_PRODUCT_INTEL_82801DBL_IDE:
		case PCI_PRODUCT_INTEL_82801DBM_IDE:
		case PCI_PRODUCT_INTEL_82801EB_IDE:
		case PCI_PRODUCT_INTEL_82801FB_IDE:
		case PCI_PRODUCT_INTEL_82801GB_IDE:
		case PCI_PRODUCT_INTEL_82801HBM_IDE:
			sc->sc_wdcdev.cap |= WDC_CAPABILITY_UDMA;
			break;
		}
	}
	sc->sc_wdcdev.PIO_cap = 4;
	sc->sc_wdcdev.DMA_cap = 2;
	switch (sc->sc_pp->ide_product) {
	case PCI_PRODUCT_INTEL_82801AA_IDE:
	case PCI_PRODUCT_INTEL_82372FB_IDE:
		sc->sc_wdcdev.UDMA_cap = 4;
		break;
	case PCI_PRODUCT_INTEL_6300ESB_IDE:
	case PCI_PRODUCT_INTEL_6321ESB_IDE:
	case PCI_PRODUCT_INTEL_82801BAM_IDE:
	case PCI_PRODUCT_INTEL_82801BA_IDE:
	case PCI_PRODUCT_INTEL_82801CAM_IDE:
	case PCI_PRODUCT_INTEL_82801CA_IDE:
	case PCI_PRODUCT_INTEL_82801DB_IDE:
	case PCI_PRODUCT_INTEL_82801DBL_IDE:
	case PCI_PRODUCT_INTEL_82801DBM_IDE:
	case PCI_PRODUCT_INTEL_82801EB_IDE:
	case PCI_PRODUCT_INTEL_82801FB_IDE:
	case PCI_PRODUCT_INTEL_82801GB_IDE:
	case PCI_PRODUCT_INTEL_82801HBM_IDE:
		sc->sc_wdcdev.UDMA_cap = 5;
		break;
	default:
		sc->sc_wdcdev.UDMA_cap = 2;
		break;
	}

	if (sc->sc_pp->ide_product == PCI_PRODUCT_INTEL_82371FB_IDE ||
		   sc->sc_pp->ide_product == PCI_PRODUCT_INTEL_82371FB_ISA) {
		sc->sc_wdcdev.set_modes = piix_setup_channel;
	} else {
		sc->sc_wdcdev.set_modes = piix3_4_setup_channel;
	}
	sc->sc_wdcdev.channels = sc->wdc_chanarray;
	sc->sc_wdcdev.nchannels = PCIIDE_NUM_CHANNELS;

	pciide_print_channels(sc->sc_wdcdev.nchannels, interface);

	piix_timing_debug(sc);

	for (channel = 0; channel < sc->sc_wdcdev.nchannels; channel++) {
		cp = &sc->pciide_channels[channel];

		if (pciide_chansetup(sc, channel, interface) == 0)
			continue;
		idetim = pci_conf_read(sc->sc_pc, sc->sc_tag, PIIX_IDETIM);
		if ((PIIX_IDETIM_READ(idetim, channel) &
		    PIIX_IDETIM_IDE) == 0) {
			printf("%s: %s ignored (disabled)\n",
			    sc->sc_wdcdev.sc_dev.dv_xname, cp->name);
			cp->hw_ok = 0;
			continue;
		}
		pciide_map_compat_intr(pa, cp, channel, interface);
		if (cp->hw_ok == 0)
			continue;
		pciide_mapchan(pa, cp, interface, &cmdsize, &ctlsize,
		    pciide_pci_intr);
		if (cp->hw_ok == 0)
			goto next;
		if (pciide_chan_candisable(cp)) {
			idetim = PIIX_IDETIM_CLEAR(idetim, PIIX_IDETIM_IDE,
			    channel);
			pci_conf_write(sc->sc_pc, sc->sc_tag, PIIX_IDETIM,
			    idetim);
		}
		if (cp->hw_ok == 0)
			goto next;
		sc->sc_wdcdev.set_modes(&cp->wdc_channel);
next:
		if (cp->hw_ok == 0)
			pciide_unmap_compat_intr(pa, cp, channel, interface);
	}

	piix_timing_debug(sc);
}

void
piixsata_chip_map(struct pciide_softc *sc, struct pci_attach_args *pa)
{
	struct pciide_channel *cp;
	pcireg_t interface = PCI_INTERFACE(pa->pa_class);
	int channel;
	bus_size_t cmdsize, ctlsize;
	u_int8_t reg, ich = 0;

	printf(": DMA");
	pciide_mapreg_dma(sc, pa);

	if (sc->sc_dma_ok) {
		sc->sc_wdcdev.cap |= WDC_CAPABILITY_UDMA |
		    WDC_CAPABILITY_DMA | WDC_CAPABILITY_IRQACK;
		sc->sc_wdcdev.irqack = pciide_irqack;
		sc->sc_wdcdev.DMA_cap = 2;
		sc->sc_wdcdev.UDMA_cap = 6;
	}
	sc->sc_wdcdev.PIO_cap = 4;

	sc->sc_wdcdev.channels = sc->wdc_chanarray;
	sc->sc_wdcdev.nchannels = PCIIDE_NUM_CHANNELS;
	sc->sc_wdcdev.cap |= WDC_CAPABILITY_DATA16 | WDC_CAPABILITY_DATA32 |
	    WDC_CAPABILITY_MODE | WDC_CAPABILITY_SATA;
	sc->sc_wdcdev.set_modes = sata_setup_channel;

	switch(sc->sc_pp->ide_product) {
	case PCI_PRODUCT_INTEL_6300ESB_SATA:
	case PCI_PRODUCT_INTEL_6300ESB_SATA2:
	case PCI_PRODUCT_INTEL_82801EB_SATA:
	case PCI_PRODUCT_INTEL_82801ER_SATA:
		ich = 5;
		break;
	case PCI_PRODUCT_INTEL_82801FB_SATA:
	case PCI_PRODUCT_INTEL_82801FR_SATA:
	case PCI_PRODUCT_INTEL_82801FBM_SATA:
		ich = 6;
		break;
	default:
		ich = 7;
		break;
	}

	/*
	 * Put the SATA portion of controllers that don't operate in combined
	 * mode into native PCI modes so the maximum number of devices can be
	 * used.  Intel calls this "enhanced mode"
	 */
	if (ich == 5) {
		reg = pciide_pci_read(sc->sc_pc, sc->sc_tag, ICH5_SATA_MAP);
		if ((reg & ICH5_SATA_MAP_COMBINED) == 0) {
			reg = pciide_pci_read(pa->pa_pc, pa->pa_tag,
			    ICH5_SATA_PI);
			reg |= ICH5_SATA_PI_PRI_NATIVE |
			    ICH5_SATA_PI_SEC_NATIVE;
			pciide_pci_write(pa->pa_pc, pa->pa_tag,
			    ICH5_SATA_PI, reg);
			interface |= PCIIDE_INTERFACE_PCI(0) |
			    PCIIDE_INTERFACE_PCI(1);
		}
	} else {
		reg = pciide_pci_read(sc->sc_pc, sc->sc_tag, ICH5_SATA_MAP) &
		    ICH6_SATA_MAP_CMB_MASK;
		if (reg != ICH6_SATA_MAP_CMB_PRI &&
		    reg != ICH6_SATA_MAP_CMB_SEC) {
			reg = pciide_pci_read(pa->pa_pc, pa->pa_tag,
			    ICH5_SATA_PI);
			reg |= ICH5_SATA_PI_PRI_NATIVE |
			    ICH5_SATA_PI_SEC_NATIVE;

			pciide_pci_write(pa->pa_pc, pa->pa_tag,
			    ICH5_SATA_PI, reg);
			interface |= PCIIDE_INTERFACE_PCI(0) |
			    PCIIDE_INTERFACE_PCI(1);

			/*
			 * Ask for SATA IDE Mode, we don't need to do this
			 * for the combined mode case as combined mode is
			 * only allowed in IDE Mode
			 */
			if (ich >= 7) {
				reg = pciide_pci_read(sc->sc_pc, sc->sc_tag,
				    ICH5_SATA_MAP) & ~ICH7_SATA_MAP_SMS_MASK;
				pciide_pci_write(pa->pa_pc, pa->pa_tag,
				    ICH5_SATA_MAP, reg);
			}
		}
	}

	pciide_print_channels(sc->sc_wdcdev.nchannels, interface);

	for (channel = 0; channel < sc->sc_wdcdev.nchannels; channel++) {
		cp = &sc->pciide_channels[channel];
		if (pciide_chansetup(sc, channel, interface) == 0)
			continue;

		pciide_map_compat_intr(pa, cp, channel, interface);
		if (cp->hw_ok == 0)
			continue;

		pciide_mapchan(pa, cp, interface, &cmdsize, &ctlsize,
		    pciide_pci_intr);
		if (cp->hw_ok != 0)
			sc->sc_wdcdev.set_modes(&cp->wdc_channel);

		if (cp->hw_ok == 0)
			pciide_unmap_compat_intr(pa, cp, channel, interface);
	}
}

void
piix_setup_channel(struct channel_softc *chp)
{
	u_int8_t mode[2], drive;
	u_int32_t oidetim, idetim, idedma_ctl;
	struct pciide_channel *cp = (struct pciide_channel *)chp;
	struct pciide_softc *sc = (struct pciide_softc *)cp->wdc_channel.wdc;
	struct ata_drive_datas *drvp = cp->wdc_channel.ch_drive;

	oidetim = pci_conf_read(sc->sc_pc, sc->sc_tag, PIIX_IDETIM);
	idetim = PIIX_IDETIM_CLEAR(oidetim, 0xffff, chp->channel);
	idedma_ctl = 0;

	/* set up new idetim: Enable IDE registers decode */
	idetim = PIIX_IDETIM_SET(idetim, PIIX_IDETIM_IDE,
	    chp->channel);

	/* setup DMA */
	pciide_channel_dma_setup(cp);

	/*
	 * Here we have to mess up with drives mode: PIIX can't have
	 * different timings for master and slave drives.
	 * We need to find the best combination.
	 */

	/* If both drives supports DMA, take the lower mode */
	if ((drvp[0].drive_flags & DRIVE_DMA) &&
	    (drvp[1].drive_flags & DRIVE_DMA)) {
		mode[0] = mode[1] =
		    min(drvp[0].DMA_mode, drvp[1].DMA_mode);
		drvp[0].DMA_mode = mode[0];
		drvp[1].DMA_mode = mode[1];
		goto ok;
	}
	/*
	 * If only one drive supports DMA, use its mode, and
	 * put the other one in PIO mode 0 if mode not compatible
	 */
	if (drvp[0].drive_flags & DRIVE_DMA) {
		mode[0] = drvp[0].DMA_mode;
		mode[1] = drvp[1].PIO_mode;
		if (piix_isp_pio[mode[1]] != piix_isp_dma[mode[0]] ||
		    piix_rtc_pio[mode[1]] != piix_rtc_dma[mode[0]])
			mode[1] = drvp[1].PIO_mode = 0;
		goto ok;
	}
	if (drvp[1].drive_flags & DRIVE_DMA) {
		mode[1] = drvp[1].DMA_mode;
		mode[0] = drvp[0].PIO_mode;
		if (piix_isp_pio[mode[0]] != piix_isp_dma[mode[1]] ||
		    piix_rtc_pio[mode[0]] != piix_rtc_dma[mode[1]])
			mode[0] = drvp[0].PIO_mode = 0;
		goto ok;
	}
	/*
	 * If both drives are not DMA, takes the lower mode, unless
	 * one of them is PIO mode < 2
	 */
	if (drvp[0].PIO_mode < 2) {
		mode[0] = drvp[0].PIO_mode = 0;
		mode[1] = drvp[1].PIO_mode;
	} else if (drvp[1].PIO_mode < 2) {
		mode[1] = drvp[1].PIO_mode = 0;
		mode[0] = drvp[0].PIO_mode;
	} else {
		mode[0] = mode[1] =
		    min(drvp[1].PIO_mode, drvp[0].PIO_mode);
		drvp[0].PIO_mode = mode[0];
		drvp[1].PIO_mode = mode[1];
	}
ok:	/* The modes are setup */
	for (drive = 0; drive < 2; drive++) {
		if (drvp[drive].drive_flags & DRIVE_DMA) {
			idetim |= piix_setup_idetim_timings(
			    mode[drive], 1, chp->channel);
			goto end;
		}
	}
	/* If we are there, none of the drives are DMA */
	if (mode[0] >= 2)
		idetim |= piix_setup_idetim_timings(
		    mode[0], 0, chp->channel);
	else
		idetim |= piix_setup_idetim_timings(
		    mode[1], 0, chp->channel);
end:	/*
	 * timing mode is now set up in the controller. Enable
	 * it per-drive
	 */
	for (drive = 0; drive < 2; drive++) {
		/* If no drive, skip */
		if ((drvp[drive].drive_flags & DRIVE) == 0)
			continue;
		idetim |= piix_setup_idetim_drvs(&drvp[drive]);
		if (drvp[drive].drive_flags & DRIVE_DMA)
			idedma_ctl |= IDEDMA_CTL_DRV_DMA(drive);
	}
	if (idedma_ctl != 0) {
		/* Add software bits in status register */
		bus_space_write_1(sc->sc_dma_iot, sc->sc_dma_ioh,
		    IDEDMA_CTL(chp->channel),
		    idedma_ctl);
	}
	pci_conf_write(sc->sc_pc, sc->sc_tag, PIIX_IDETIM, idetim);
	pciide_print_modes(cp);
}

void
piix3_4_setup_channel(struct channel_softc *chp)
{
	struct ata_drive_datas *drvp;
	u_int32_t oidetim, idetim, sidetim, udmareg, ideconf, idedma_ctl;
	struct pciide_channel *cp = (struct pciide_channel *)chp;
	struct pciide_softc *sc = (struct pciide_softc *)cp->wdc_channel.wdc;
	int drive;
	int channel = chp->channel;

	oidetim = pci_conf_read(sc->sc_pc, sc->sc_tag, PIIX_IDETIM);
	sidetim = pci_conf_read(sc->sc_pc, sc->sc_tag, PIIX_SIDETIM);
	udmareg = pci_conf_read(sc->sc_pc, sc->sc_tag, PIIX_UDMAREG);
	ideconf = pci_conf_read(sc->sc_pc, sc->sc_tag, PIIX_CONFIG);
	idetim = PIIX_IDETIM_CLEAR(oidetim, 0xffff, channel);
	sidetim &= ~(PIIX_SIDETIM_ISP_MASK(channel) |
	    PIIX_SIDETIM_RTC_MASK(channel));

	idedma_ctl = 0;
	/* If channel disabled, no need to go further */
	if ((PIIX_IDETIM_READ(oidetim, channel) & PIIX_IDETIM_IDE) == 0)
		return;
	/* set up new idetim: Enable IDE registers decode */
	idetim = PIIX_IDETIM_SET(idetim, PIIX_IDETIM_IDE, channel);

	/* setup DMA if needed */
	pciide_channel_dma_setup(cp);

	for (drive = 0; drive < 2; drive++) {
		udmareg &= ~(PIIX_UDMACTL_DRV_EN(channel, drive) |
		    PIIX_UDMATIM_SET(0x3, channel, drive));
		drvp = &chp->ch_drive[drive];
		/* If no drive, skip */
		if ((drvp->drive_flags & DRIVE) == 0)
			continue;
		if (((drvp->drive_flags & DRIVE_DMA) == 0 &&
		    (drvp->drive_flags & DRIVE_UDMA) == 0))
			goto pio;

		if (sc->sc_pp->ide_product == PCI_PRODUCT_INTEL_6300ESB_IDE ||
		    sc->sc_pp->ide_product == PCI_PRODUCT_INTEL_6321ESB_IDE ||
		    sc->sc_pp->ide_product == PCI_PRODUCT_INTEL_82801AA_IDE ||
		    sc->sc_pp->ide_product == PCI_PRODUCT_INTEL_82801AB_IDE ||
		    sc->sc_pp->ide_product == PCI_PRODUCT_INTEL_82801BAM_IDE ||
		    sc->sc_pp->ide_product == PCI_PRODUCT_INTEL_82801BA_IDE ||
		    sc->sc_pp->ide_product == PCI_PRODUCT_INTEL_82801CAM_IDE ||
		    sc->sc_pp->ide_product == PCI_PRODUCT_INTEL_82801CA_IDE ||
		    sc->sc_pp->ide_product == PCI_PRODUCT_INTEL_82801DB_IDE ||
		    sc->sc_pp->ide_product == PCI_PRODUCT_INTEL_82801DBL_IDE ||
		    sc->sc_pp->ide_product == PCI_PRODUCT_INTEL_82801DBM_IDE ||
		    sc->sc_pp->ide_product == PCI_PRODUCT_INTEL_82801EB_IDE ||
		    sc->sc_pp->ide_product == PCI_PRODUCT_INTEL_82801FB_IDE ||
		    sc->sc_pp->ide_product == PCI_PRODUCT_INTEL_82801GB_IDE ||
		    sc->sc_pp->ide_product == PCI_PRODUCT_INTEL_82801HBM_IDE ||
		    sc->sc_pp->ide_product == PCI_PRODUCT_INTEL_82372FB_IDE) {
			ideconf |= PIIX_CONFIG_PINGPONG;
		}
		if (sc->sc_pp->ide_product == PCI_PRODUCT_INTEL_6300ESB_IDE ||
		    sc->sc_pp->ide_product == PCI_PRODUCT_INTEL_6321ESB_IDE ||
		    sc->sc_pp->ide_product == PCI_PRODUCT_INTEL_82801BAM_IDE ||
		    sc->sc_pp->ide_product == PCI_PRODUCT_INTEL_82801BA_IDE||
		    sc->sc_pp->ide_product == PCI_PRODUCT_INTEL_82801CAM_IDE||
		    sc->sc_pp->ide_product == PCI_PRODUCT_INTEL_82801CA_IDE ||
		    sc->sc_pp->ide_product == PCI_PRODUCT_INTEL_82801DB_IDE ||
		    sc->sc_pp->ide_product == PCI_PRODUCT_INTEL_82801DBL_IDE ||
		    sc->sc_pp->ide_product == PCI_PRODUCT_INTEL_82801DBM_IDE ||
		    sc->sc_pp->ide_product == PCI_PRODUCT_INTEL_82801EB_IDE ||
		    sc->sc_pp->ide_product == PCI_PRODUCT_INTEL_82801FB_IDE ||
		    sc->sc_pp->ide_product == PCI_PRODUCT_INTEL_82801GB_IDE ||
		    sc->sc_pp->ide_product == PCI_PRODUCT_INTEL_82801HBM_IDE) {
			/* setup Ultra/100 */
			if (drvp->UDMA_mode > 2 &&
			    (ideconf & PIIX_CONFIG_CR(channel, drive)) == 0)
				drvp->UDMA_mode = 2;
			if (drvp->UDMA_mode > 4) {
				ideconf |= PIIX_CONFIG_UDMA100(channel, drive);
			} else {
				ideconf &= ~PIIX_CONFIG_UDMA100(channel, drive);
				if (drvp->UDMA_mode > 2) {
					ideconf |= PIIX_CONFIG_UDMA66(channel,
					    drive);
				} else {
					ideconf &= ~PIIX_CONFIG_UDMA66(channel,
					    drive);
				}
			}
		}
		if (sc->sc_pp->ide_product == PCI_PRODUCT_INTEL_82801AA_IDE ||
		    sc->sc_pp->ide_product == PCI_PRODUCT_INTEL_82372FB_IDE) {
			/* setup Ultra/66 */
			if (drvp->UDMA_mode > 2 &&
			    (ideconf & PIIX_CONFIG_CR(channel, drive)) == 0)
				drvp->UDMA_mode = 2;
			if (drvp->UDMA_mode > 2)
				ideconf |= PIIX_CONFIG_UDMA66(channel, drive);
			else
				ideconf &= ~PIIX_CONFIG_UDMA66(channel, drive);
		}

		if ((chp->wdc->cap & WDC_CAPABILITY_UDMA) &&
		    (drvp->drive_flags & DRIVE_UDMA)) {
			/* use Ultra/DMA */
			drvp->drive_flags &= ~DRIVE_DMA;
			udmareg |= PIIX_UDMACTL_DRV_EN( channel, drive);
			udmareg |= PIIX_UDMATIM_SET(
			    piix4_sct_udma[drvp->UDMA_mode], channel, drive);
		} else {
			/* use Multiword DMA */
			drvp->drive_flags &= ~DRIVE_UDMA;
			if (drive == 0) {
				idetim |= piix_setup_idetim_timings(
				    drvp->DMA_mode, 1, channel);
			} else {
				sidetim |= piix_setup_sidetim_timings(
					drvp->DMA_mode, 1, channel);
				idetim = PIIX_IDETIM_SET(idetim,
				    PIIX_IDETIM_SITRE, channel);
			}
		}
		idedma_ctl |= IDEDMA_CTL_DRV_DMA(drive);

pio:		/* use PIO mode */
		idetim |= piix_setup_idetim_drvs(drvp);
		if (drive == 0) {
			idetim |= piix_setup_idetim_timings(
			    drvp->PIO_mode, 0, channel);
		} else {
			sidetim |= piix_setup_sidetim_timings(
				drvp->PIO_mode, 0, channel);
			idetim = PIIX_IDETIM_SET(idetim,
			    PIIX_IDETIM_SITRE, channel);
		}
	}
	if (idedma_ctl != 0) {
		/* Add software bits in status register */
		bus_space_write_1(sc->sc_dma_iot, sc->sc_dma_ioh,
		    IDEDMA_CTL(channel),
		    idedma_ctl);
	}
	pci_conf_write(sc->sc_pc, sc->sc_tag, PIIX_IDETIM, idetim);
	pci_conf_write(sc->sc_pc, sc->sc_tag, PIIX_SIDETIM, sidetim);
	pci_conf_write(sc->sc_pc, sc->sc_tag, PIIX_UDMAREG, udmareg);
	pci_conf_write(sc->sc_pc, sc->sc_tag, PIIX_CONFIG, ideconf);
	pciide_print_modes(cp);
}


/* setup ISP and RTC fields, based on mode */
u_int32_t
piix_setup_idetim_timings(u_int8_t mode, u_int8_t dma, u_int8_t channel)
{

	if (dma)
		return (PIIX_IDETIM_SET(0,
		    PIIX_IDETIM_ISP_SET(piix_isp_dma[mode]) |
		    PIIX_IDETIM_RTC_SET(piix_rtc_dma[mode]),
		    channel));
	else
		return (PIIX_IDETIM_SET(0,
		    PIIX_IDETIM_ISP_SET(piix_isp_pio[mode]) |
		    PIIX_IDETIM_RTC_SET(piix_rtc_pio[mode]),
		    channel));
}

/* setup DTE, PPE, IE and TIME field based on PIO mode */
u_int32_t
piix_setup_idetim_drvs(struct ata_drive_datas *drvp)
{
	u_int32_t ret = 0;
	struct channel_softc *chp = drvp->chnl_softc;
	u_int8_t channel = chp->channel;
	u_int8_t drive = drvp->drive;

	/*
	 * If drive is using UDMA, timing setup is independent
	 * so just check DMA and PIO here.
	 */
	if (drvp->drive_flags & DRIVE_DMA) {
		/* if mode = DMA mode 0, use compatible timings */
		if ((drvp->drive_flags & DRIVE_DMA) &&
		    drvp->DMA_mode == 0) {
			drvp->PIO_mode = 0;
			return (ret);
		}
		ret = PIIX_IDETIM_SET(ret, PIIX_IDETIM_TIME(drive), channel);
		/*
		 * PIO and DMA timings are the same, use fast timings for PIO
		 * too, else use compat timings.
		 */
		if ((piix_isp_pio[drvp->PIO_mode] !=
		    piix_isp_dma[drvp->DMA_mode]) ||
		    (piix_rtc_pio[drvp->PIO_mode] !=
		    piix_rtc_dma[drvp->DMA_mode]))
			drvp->PIO_mode = 0;
		/* if PIO mode <= 2, use compat timings for PIO */
		if (drvp->PIO_mode <= 2) {
			ret = PIIX_IDETIM_SET(ret, PIIX_IDETIM_DTE(drive),
			    channel);
			return (ret);
		}
	}

	/*
	 * Now setup PIO modes. If mode < 2, use compat timings.
	 * Else enable fast timings. Enable IORDY and prefetch/post
	 * if PIO mode >= 3.
	 */

	if (drvp->PIO_mode < 2)
		return (ret);

	ret = PIIX_IDETIM_SET(ret, PIIX_IDETIM_TIME(drive), channel);
	if (drvp->PIO_mode >= 3) {
		ret = PIIX_IDETIM_SET(ret, PIIX_IDETIM_IE(drive), channel);
		ret = PIIX_IDETIM_SET(ret, PIIX_IDETIM_PPE(drive), channel);
	}
	return (ret);
}

/* setup values in SIDETIM registers, based on mode */
u_int32_t
piix_setup_sidetim_timings(u_int8_t mode, u_int8_t dma, u_int8_t channel)
{
	if (dma)
		return (PIIX_SIDETIM_ISP_SET(piix_isp_dma[mode], channel) |
		    PIIX_SIDETIM_RTC_SET(piix_rtc_dma[mode], channel));
	else
		return (PIIX_SIDETIM_ISP_SET(piix_isp_pio[mode], channel) |
		    PIIX_SIDETIM_RTC_SET(piix_rtc_pio[mode], channel));
}

void
amd756_chip_map(struct pciide_softc *sc, struct pci_attach_args *pa)
{
	struct pciide_channel *cp;
	pcireg_t interface = PCI_INTERFACE(pa->pa_class);
	int channel;
	pcireg_t chanenable;
	bus_size_t cmdsize, ctlsize;

	printf(": DMA");
	pciide_mapreg_dma(sc, pa);
	sc->sc_wdcdev.cap = WDC_CAPABILITY_DATA16 | WDC_CAPABILITY_DATA32 |
	    WDC_CAPABILITY_MODE;
	if (sc->sc_dma_ok) {
		sc->sc_wdcdev.cap |= WDC_CAPABILITY_DMA | WDC_CAPABILITY_UDMA;
		sc->sc_wdcdev.cap |= WDC_CAPABILITY_IRQACK;
		sc->sc_wdcdev.irqack = pciide_irqack;
	}
	sc->sc_wdcdev.PIO_cap = 4;
	sc->sc_wdcdev.DMA_cap = 2;
	switch (sc->sc_pp->ide_product) {
	case PCI_PRODUCT_AMD_8111_IDE:
		sc->sc_wdcdev.UDMA_cap = 6;
		break;
	case PCI_PRODUCT_AMD_766_IDE:
	case PCI_PRODUCT_AMD_PBC768_IDE:
		sc->sc_wdcdev.UDMA_cap = 5;
		break;
	default:
		sc->sc_wdcdev.UDMA_cap = 4;
		break;
	}
	sc->sc_wdcdev.set_modes = amd756_setup_channel;
	sc->sc_wdcdev.channels = sc->wdc_chanarray;
	sc->sc_wdcdev.nchannels = PCIIDE_NUM_CHANNELS;
	chanenable = pci_conf_read(sc->sc_pc, sc->sc_tag, AMD756_CHANSTATUS_EN);

	pciide_print_channels(sc->sc_wdcdev.nchannels, interface);

	for (channel = 0; channel < sc->sc_wdcdev.nchannels; channel++) {
		cp = &sc->pciide_channels[channel];
		if (pciide_chansetup(sc, channel, interface) == 0)
			continue;

		if ((chanenable & AMD756_CHAN_EN(channel)) == 0) {
			printf("%s: %s ignored (disabled)\n",
			    sc->sc_wdcdev.sc_dev.dv_xname, cp->name);
			cp->hw_ok = 0;
			continue;
		}
		pciide_map_compat_intr(pa, cp, channel, interface);
		if (cp->hw_ok == 0)
			continue;

		pciide_mapchan(pa, cp, interface, &cmdsize, &ctlsize,
		    pciide_pci_intr);

		if (pciide_chan_candisable(cp)) {
			chanenable &= ~AMD756_CHAN_EN(channel);
		}
		if (cp->hw_ok == 0) {
			pciide_unmap_compat_intr(pa, cp, channel, interface);
			continue;
		}

		amd756_setup_channel(&cp->wdc_channel);
	}
	pci_conf_write(sc->sc_pc, sc->sc_tag, AMD756_CHANSTATUS_EN,
	    chanenable);
	return;
}

void
amd756_setup_channel(struct channel_softc *chp)
{
	u_int32_t udmatim_reg, datatim_reg;
	u_int8_t idedma_ctl;
	int mode, drive;
	struct ata_drive_datas *drvp;
	struct pciide_channel *cp = (struct pciide_channel *)chp;
	struct pciide_softc *sc = (struct pciide_softc *)cp->wdc_channel.wdc;
	pcireg_t chanenable;
#ifndef	PCIIDE_AMD756_ENABLEDMA
	int product = sc->sc_pp->ide_product;
	int rev = sc->sc_rev;
#endif

	idedma_ctl = 0;
	datatim_reg = pci_conf_read(sc->sc_pc, sc->sc_tag, AMD756_DATATIM);
	udmatim_reg = pci_conf_read(sc->sc_pc, sc->sc_tag, AMD756_UDMA);
	datatim_reg &= ~AMD756_DATATIM_MASK(chp->channel);
	udmatim_reg &= ~AMD756_UDMA_MASK(chp->channel);
	chanenable = pci_conf_read(sc->sc_pc, sc->sc_tag,
	    AMD756_CHANSTATUS_EN);

	/* setup DMA if needed */
	pciide_channel_dma_setup(cp);

	for (drive = 0; drive < 2; drive++) {
		drvp = &chp->ch_drive[drive];
		/* If no drive, skip */
		if ((drvp->drive_flags & DRIVE) == 0)
			continue;
		/* add timing values, setup DMA if needed */
		if (((drvp->drive_flags & DRIVE_DMA) == 0 &&
		    (drvp->drive_flags & DRIVE_UDMA) == 0)) {
			mode = drvp->PIO_mode;
			goto pio;
		}
		if ((chp->wdc->cap & WDC_CAPABILITY_UDMA) &&
		    (drvp->drive_flags & DRIVE_UDMA)) {
			/* use Ultra/DMA */
			drvp->drive_flags &= ~DRIVE_DMA;

			/* Check cable */
			if ((chanenable & AMD756_CABLE(chp->channel,
			    drive)) == 0 && drvp->UDMA_mode > 2) {
				WDCDEBUG_PRINT(("%s(%s:%d:%d): 80-wire "
				    "cable not detected\n", drvp->drive_name,
				    sc->sc_wdcdev.sc_dev.dv_xname,
				    chp->channel, drive), DEBUG_PROBE);
				drvp->UDMA_mode = 2;
			}

			udmatim_reg |= AMD756_UDMA_EN(chp->channel, drive) |
			    AMD756_UDMA_EN_MTH(chp->channel, drive) |
			    AMD756_UDMA_TIME(chp->channel, drive,
				amd756_udma_tim[drvp->UDMA_mode]);
			/* can use PIO timings, MW DMA unused */
			mode = drvp->PIO_mode;
		} else {
			/* use Multiword DMA, but only if revision is OK */
			drvp->drive_flags &= ~DRIVE_UDMA;
#ifndef PCIIDE_AMD756_ENABLEDMA
			/*
			 * The workaround doesn't seem to be necessary
			 * with all drives, so it can be disabled by
			 * PCIIDE_AMD756_ENABLEDMA. It causes a hard hang if
			 * triggered.
			 */
			if (AMD756_CHIPREV_DISABLEDMA(product, rev)) {
				printf("%s:%d:%d: multi-word DMA disabled due "
				    "to chip revision\n",
				    sc->sc_wdcdev.sc_dev.dv_xname,
				    chp->channel, drive);
				mode = drvp->PIO_mode;
				drvp->drive_flags &= ~DRIVE_DMA;
				goto pio;
			}
#endif
			/* mode = min(pio, dma+2) */
			if (drvp->PIO_mode <= (drvp->DMA_mode +2))
				mode = drvp->PIO_mode;
			else
				mode = drvp->DMA_mode + 2;
		}
		idedma_ctl |= IDEDMA_CTL_DRV_DMA(drive);

pio:		/* setup PIO mode */
		if (mode <= 2) {
			drvp->DMA_mode = 0;
			drvp->PIO_mode = 0;
			mode = 0;
		} else {
			drvp->PIO_mode = mode;
			drvp->DMA_mode = mode - 2;
		}
		datatim_reg |=
		    AMD756_DATATIM_PULSE(chp->channel, drive,
			amd756_pio_set[mode]) |
		    AMD756_DATATIM_RECOV(chp->channel, drive,
			amd756_pio_rec[mode]);
	}
	if (idedma_ctl != 0) {
		/* Add software bits in status register */
		bus_space_write_1(sc->sc_dma_iot, sc->sc_dma_ioh,
		    IDEDMA_CTL(chp->channel),
		    idedma_ctl);
	}
	pciide_print_modes(cp);
	pci_conf_write(sc->sc_pc, sc->sc_tag, AMD756_DATATIM, datatim_reg);
	pci_conf_write(sc->sc_pc, sc->sc_tag, AMD756_UDMA, udmatim_reg);
}

void
apollo_chip_map(struct pciide_softc *sc, struct pci_attach_args *pa)
{
	struct pciide_channel *cp;
	pcireg_t interface;
	int no_ideconf = 0, channel;
	u_int32_t ideconf;
	bus_size_t cmdsize, ctlsize;
	pcitag_t tag;
	pcireg_t id, class;

	/*
	 * Fake interface since VT6410 is claimed to be a ``RAID'' device.
	 */
	if (PCI_SUBCLASS(pa->pa_class) == PCI_SUBCLASS_MASS_STORAGE_IDE) {
		interface = PCI_INTERFACE(pa->pa_class);
	} else {
		interface = PCIIDE_INTERFACE_BUS_MASTER_DMA |
		    PCIIDE_INTERFACE_PCI(0) | PCIIDE_INTERFACE_PCI(1);
	}

	switch (PCI_PRODUCT(pa->pa_id)) {
	case PCI_PRODUCT_VIATECH_VT6410:
	case PCI_PRODUCT_VIATECH_VT6415:
		no_ideconf = 1;
		/* FALLTHROUGH */
	case PCI_PRODUCT_VIATECH_CX700_IDE:
	case PCI_PRODUCT_VIATECH_VX700_IDE:
	case PCI_PRODUCT_VIATECH_VX855_IDE:
	case PCI_PRODUCT_VIATECH_VX900_IDE:
		printf(": ATA133");
		sc->sc_wdcdev.UDMA_cap = 6;
		break;
	default:
		/* 
		 * Determine the DMA capabilities by looking at the
		 * ISA bridge.
		 */
		tag = pci_make_tag(pa->pa_pc, pa->pa_bus, pa->pa_device, 0);
		id = pci_conf_read(sc->sc_pc, tag, PCI_ID_REG);
		class = pci_conf_read(sc->sc_pc, tag, PCI_CLASS_REG);

		/*
		 * XXX On the VT8237, the ISA bridge is on a different
		 * device.
		 */
		if (PCI_CLASS(class) != PCI_CLASS_BRIDGE &&
		    pa->pa_device == 15) {
			tag = pci_make_tag(pa->pa_pc, pa->pa_bus, 17, 0);
			id = pci_conf_read(sc->sc_pc, tag, PCI_ID_REG);
			class = pci_conf_read(sc->sc_pc, tag, PCI_CLASS_REG);
		}

		switch (PCI_PRODUCT(id)) {
		case PCI_PRODUCT_VIATECH_VT82C586_ISA:
			if (PCI_REVISION(class) >= 0x02) {
				printf(": ATA33");
				sc->sc_wdcdev.UDMA_cap = 2;
			} else {
				printf(": DMA");
				sc->sc_wdcdev.UDMA_cap = 0;
			}
			break;
		case PCI_PRODUCT_VIATECH_VT82C596A:
			if (PCI_REVISION(class) >= 0x12) {
				printf(": ATA66");
				sc->sc_wdcdev.UDMA_cap = 4;
			} else {
				printf(": ATA33");
				sc->sc_wdcdev.UDMA_cap = 2;
			}
			break;

		case PCI_PRODUCT_VIATECH_VT82C686A_ISA:
			if (PCI_REVISION(class) >= 0x40) {
				printf(": ATA100");
				sc->sc_wdcdev.UDMA_cap = 5;
			} else {
				printf(": ATA66");
				sc->sc_wdcdev.UDMA_cap = 4;
			}
			break;
		case PCI_PRODUCT_VIATECH_VT8231_ISA:
		case PCI_PRODUCT_VIATECH_VT8233_ISA:
			printf(": ATA100");
			sc->sc_wdcdev.UDMA_cap = 5;
			break;
		case PCI_PRODUCT_VIATECH_VT8233A_ISA:
		case PCI_PRODUCT_VIATECH_VT8235_ISA:
		case PCI_PRODUCT_VIATECH_VT8237_ISA:
			printf(": ATA133");
			sc->sc_wdcdev.UDMA_cap = 6;
			break;
		default:
			printf(": DMA");
			sc->sc_wdcdev.UDMA_cap = 0;
			break;
		}
		break;
	}

	pciide_mapreg_dma(sc, pa);
	sc->sc_wdcdev.cap |= WDC_CAPABILITY_DATA16 | WDC_CAPABILITY_DATA32 |
	    WDC_CAPABILITY_MODE;
	if (sc->sc_dma_ok) {
		sc->sc_wdcdev.cap |= WDC_CAPABILITY_DMA | WDC_CAPABILITY_IRQACK;
		sc->sc_wdcdev.irqack = pciide_irqack;
		if (sc->sc_wdcdev.UDMA_cap > 0)
			sc->sc_wdcdev.cap |= WDC_CAPABILITY_UDMA;
	}
	sc->sc_wdcdev.PIO_cap = 4;
	sc->sc_wdcdev.DMA_cap = 2;
	sc->sc_wdcdev.set_modes = apollo_setup_channel;
	sc->sc_wdcdev.channels = sc->wdc_chanarray;
	sc->sc_wdcdev.nchannels = PCIIDE_NUM_CHANNELS;

	pciide_print_channels(sc->sc_wdcdev.nchannels, interface);

	WDCDEBUG_PRINT(("apollo_chip_map: old APO_IDECONF=0x%x, "
	    "APO_CTLMISC=0x%x, APO_DATATIM=0x%x, APO_UDMA=0x%x\n",
	    pci_conf_read(sc->sc_pc, sc->sc_tag, APO_IDECONF),
	    pci_conf_read(sc->sc_pc, sc->sc_tag, APO_CTLMISC),
	    pci_conf_read(sc->sc_pc, sc->sc_tag, APO_DATATIM),
	    pci_conf_read(sc->sc_pc, sc->sc_tag, APO_UDMA)),
	    DEBUG_PROBE);

	for (channel = 0; channel < sc->sc_wdcdev.nchannels; channel++) {
		cp = &sc->pciide_channels[channel];
		if (pciide_chansetup(sc, channel, interface) == 0)
			continue;

		if (no_ideconf == 0) {
			ideconf = pci_conf_read(sc->sc_pc, sc->sc_tag,
			    APO_IDECONF);
			if ((ideconf & APO_IDECONF_EN(channel)) == 0) {
				printf("%s: %s ignored (disabled)\n",
				    sc->sc_wdcdev.sc_dev.dv_xname, cp->name);
				cp->hw_ok = 0;
				continue;
			}
		}
		pciide_map_compat_intr(pa, cp, channel, interface);
		if (cp->hw_ok == 0)
			continue;

		pciide_mapchan(pa, cp, interface, &cmdsize, &ctlsize,
		    pciide_pci_intr);
		if (cp->hw_ok == 0) {
			goto next;
		}
		if (pciide_chan_candisable(cp)) {
			if (no_ideconf == 0) {
				ideconf &= ~APO_IDECONF_EN(channel);
				pci_conf_write(sc->sc_pc, sc->sc_tag,
				    APO_IDECONF, ideconf);
			}
		}

		if (cp->hw_ok == 0)
			goto next;
		apollo_setup_channel(&sc->pciide_channels[channel].wdc_channel);
next:
		if (cp->hw_ok == 0)
			pciide_unmap_compat_intr(pa, cp, channel, interface);
	}
	WDCDEBUG_PRINT(("apollo_chip_map: APO_DATATIM=0x%x, APO_UDMA=0x%x\n",
	    pci_conf_read(sc->sc_pc, sc->sc_tag, APO_DATATIM),
	    pci_conf_read(sc->sc_pc, sc->sc_tag, APO_UDMA)), DEBUG_PROBE);
}

void
apollo_setup_channel(struct channel_softc *chp)
{
	u_int32_t udmatim_reg, datatim_reg;
	u_int8_t idedma_ctl;
	int mode, drive;
	struct ata_drive_datas *drvp;
	struct pciide_channel *cp = (struct pciide_channel *)chp;
	struct pciide_softc *sc = (struct pciide_softc *)cp->wdc_channel.wdc;

	idedma_ctl = 0;
	datatim_reg = pci_conf_read(sc->sc_pc, sc->sc_tag, APO_DATATIM);
	udmatim_reg = pci_conf_read(sc->sc_pc, sc->sc_tag, APO_UDMA);
	datatim_reg &= ~APO_DATATIM_MASK(chp->channel);
	udmatim_reg &= ~APO_UDMA_MASK(chp->channel);

	/* setup DMA if needed */
	pciide_channel_dma_setup(cp);

	/*
	 * We can't mix Ultra/33 and Ultra/66 on the same channel, so
	 * downgrade to Ultra/33 if needed
	 */
	if ((chp->ch_drive[0].drive_flags & DRIVE_UDMA) &&
	    (chp->ch_drive[1].drive_flags & DRIVE_UDMA)) {
		/* both drives UDMA */
		if (chp->ch_drive[0].UDMA_mode > 2 &&
		    chp->ch_drive[1].UDMA_mode <= 2) {
			/* drive 0 Ultra/66, drive 1 Ultra/33 */
			chp->ch_drive[0].UDMA_mode = 2;
		} else if (chp->ch_drive[1].UDMA_mode > 2 &&
		    chp->ch_drive[0].UDMA_mode <= 2) {
			/* drive 1 Ultra/66, drive 0 Ultra/33 */
			chp->ch_drive[1].UDMA_mode = 2;
		}
	}

	for (drive = 0; drive < 2; drive++) {
		drvp = &chp->ch_drive[drive];
		/* If no drive, skip */
		if ((drvp->drive_flags & DRIVE) == 0)
			continue;
		/* add timing values, setup DMA if needed */
		if (((drvp->drive_flags & DRIVE_DMA) == 0 &&
		    (drvp->drive_flags & DRIVE_UDMA) == 0)) {
			mode = drvp->PIO_mode;
			goto pio;
		}
		if ((chp->wdc->cap & WDC_CAPABILITY_UDMA) &&
		    (drvp->drive_flags & DRIVE_UDMA)) {
			/* use Ultra/DMA */
			drvp->drive_flags &= ~DRIVE_DMA;
			udmatim_reg |= APO_UDMA_EN(chp->channel, drive) |
			    APO_UDMA_EN_MTH(chp->channel, drive);
			if (sc->sc_wdcdev.UDMA_cap == 6) {
				udmatim_reg |= APO_UDMA_TIME(chp->channel,
				    drive, apollo_udma133_tim[drvp->UDMA_mode]);
			} else if (sc->sc_wdcdev.UDMA_cap == 5) {
				/* 686b */
				udmatim_reg |= APO_UDMA_TIME(chp->channel,
				    drive, apollo_udma100_tim[drvp->UDMA_mode]);
			} else if (sc->sc_wdcdev.UDMA_cap == 4) {
				/* 596b or 686a */
				udmatim_reg |= APO_UDMA_CLK66(chp->channel);
				udmatim_reg |= APO_UDMA_TIME(chp->channel,
				    drive, apollo_udma66_tim[drvp->UDMA_mode]);
			} else {
				/* 596a or 586b */
				udmatim_reg |= APO_UDMA_TIME(chp->channel,
				    drive, apollo_udma33_tim[drvp->UDMA_mode]);
			}
			/* can use PIO timings, MW DMA unused */
			mode = drvp->PIO_mode;
		} else {
			/* use Multiword DMA */
			drvp->drive_flags &= ~DRIVE_UDMA;
			/* mode = min(pio, dma+2) */
			if (drvp->PIO_mode <= (drvp->DMA_mode +2))
				mode = drvp->PIO_mode;
			else
				mode = drvp->DMA_mode + 2;
		}
		idedma_ctl |= IDEDMA_CTL_DRV_DMA(drive);

pio:		/* setup PIO mode */
		if (mode <= 2) {
			drvp->DMA_mode = 0;
			drvp->PIO_mode = 0;
			mode = 0;
		} else {
			drvp->PIO_mode = mode;
			drvp->DMA_mode = mode - 2;
		}
		datatim_reg |=
		    APO_DATATIM_PULSE(chp->channel, drive,
			apollo_pio_set[mode]) |
		    APO_DATATIM_RECOV(chp->channel, drive,
			apollo_pio_rec[mode]);
	}
	if (idedma_ctl != 0) {
		/* Add software bits in status register */
		bus_space_write_1(sc->sc_dma_iot, sc->sc_dma_ioh,
		    IDEDMA_CTL(chp->channel),
		    idedma_ctl);
	}
	pciide_print_modes(cp);
	pci_conf_write(sc->sc_pc, sc->sc_tag, APO_DATATIM, datatim_reg);
	pci_conf_write(sc->sc_pc, sc->sc_tag, APO_UDMA, udmatim_reg);
}

void
cmd_channel_map(struct pci_attach_args *pa, struct pciide_softc *sc,
    int channel)
{
	struct pciide_channel *cp = &sc->pciide_channels[channel];
	bus_size_t cmdsize, ctlsize;
	u_int8_t ctrl = pciide_pci_read(sc->sc_pc, sc->sc_tag, CMD_CTRL);
	pcireg_t interface;
	int one_channel;

	/*
	 * The 0648/0649 can be told to identify as a RAID controller.
	 * In this case, we have to fake interface
	 */
	if (PCI_SUBCLASS(pa->pa_class) != PCI_SUBCLASS_MASS_STORAGE_IDE) {
		interface = PCIIDE_INTERFACE_SETTABLE(0) |
		    PCIIDE_INTERFACE_SETTABLE(1);
		if (pciide_pci_read(pa->pa_pc, pa->pa_tag, CMD_CONF) &
		    CMD_CONF_DSA1)
			interface |= PCIIDE_INTERFACE_PCI(0) |
			    PCIIDE_INTERFACE_PCI(1);
	} else {
		interface = PCI_INTERFACE(pa->pa_class);
	}

	sc->wdc_chanarray[channel] = &cp->wdc_channel;
	cp->name = PCIIDE_CHANNEL_NAME(channel);
	cp->wdc_channel.channel = channel;
	cp->wdc_channel.wdc = &sc->sc_wdcdev;

	/*
	 * Older CMD64X doesn't have independent channels
	 */
	switch (sc->sc_pp->ide_product) {
	case PCI_PRODUCT_CMDTECH_649:
		one_channel = 0;
		break;
	default:
		one_channel = 1;
		break;
	}

	if (channel > 0 && one_channel) {
		cp->wdc_channel.ch_queue =
		    sc->pciide_channels[0].wdc_channel.ch_queue;
	} else {
		cp->wdc_channel.ch_queue = wdc_alloc_queue();
	}
	if (cp->wdc_channel.ch_queue == NULL) {
		printf(
		    "%s: %s cannot allocate channel queue",
		    sc->sc_wdcdev.sc_dev.dv_xname, cp->name);
		return;
	}

	/*
	 * with a CMD PCI64x, if we get here, the first channel is enabled:
	 * there's no way to disable the first channel without disabling
	 * the whole device
	 */
	if (channel != 0 && (ctrl & CMD_CTRL_2PORT) == 0) {
		printf("%s: %s ignored (disabled)\n",
		    sc->sc_wdcdev.sc_dev.dv_xname, cp->name);
		cp->hw_ok = 0;
		return;
	}
	cp->hw_ok = 1;
	pciide_map_compat_intr(pa, cp, channel, interface);
	if (cp->hw_ok == 0)
		return;
	pciide_mapchan(pa, cp, interface, &cmdsize, &ctlsize, cmd_pci_intr);
	if (cp->hw_ok == 0) {
		pciide_unmap_compat_intr(pa, cp, channel, interface);
		return;
	}
	if (pciide_chan_candisable(cp)) {
		if (channel == 1) {
			ctrl &= ~CMD_CTRL_2PORT;
			pciide_pci_write(pa->pa_pc, pa->pa_tag,
			    CMD_CTRL, ctrl);
			pciide_unmap_compat_intr(pa, cp, channel, interface);
		}
	}
}

int
cmd_pci_intr(void *arg)
{
	struct pciide_softc *sc = arg;
	struct pciide_channel *cp;
	struct channel_softc *wdc_cp;
	int i, rv, crv;
	u_int32_t priirq, secirq;

	rv = 0;
	priirq = pciide_pci_read(sc->sc_pc, sc->sc_tag, CMD_CONF);
	secirq = pciide_pci_read(sc->sc_pc, sc->sc_tag, CMD_ARTTIM23);
	for (i = 0; i < sc->sc_wdcdev.nchannels; i++) {
		cp = &sc->pciide_channels[i];
		wdc_cp = &cp->wdc_channel;
		/* If a compat channel skip. */
		if (cp->compat)
			continue;
		if ((i == 0 && (priirq & CMD_CONF_DRV0_INTR)) ||
		    (i == 1 && (secirq & CMD_ARTTIM23_IRQ))) {
			crv = wdcintr(wdc_cp);
			if (crv == 0) {
#if 0
				printf("%s:%d: bogus intr\n",
				    sc->sc_wdcdev.sc_dev.dv_xname, i);
#endif
			} else
				rv = 1;
		}
	}
	return (rv);
}

void
cmd_chip_map(struct pciide_softc *sc, struct pci_attach_args *pa)
{
	int channel;
	pcireg_t interface = PCI_INTERFACE(pa->pa_class);

	printf(": no DMA");
	sc->sc_dma_ok = 0;

	sc->sc_wdcdev.channels = sc->wdc_chanarray;
	sc->sc_wdcdev.nchannels = PCIIDE_NUM_CHANNELS;
	sc->sc_wdcdev.cap = WDC_CAPABILITY_DATA16;

	pciide_print_channels(sc->sc_wdcdev.nchannels, interface);

	for (channel = 0; channel < sc->sc_wdcdev.nchannels; channel++) {
		cmd_channel_map(pa, sc, channel);
	}
}

void
cmd0643_9_chip_map(struct pciide_softc *sc, struct pci_attach_args *pa)
{
	struct pciide_channel *cp;
	int channel;
	int rev = sc->sc_rev;
	pcireg_t interface;

	/*
	 * The 0648/0649 can be told to identify as a RAID controller.
	 * In this case, we have to fake interface
	 */
	if (PCI_SUBCLASS(pa->pa_class) != PCI_SUBCLASS_MASS_STORAGE_IDE) {
		interface = PCIIDE_INTERFACE_SETTABLE(0) |
		    PCIIDE_INTERFACE_SETTABLE(1);
		if (pciide_pci_read(pa->pa_pc, pa->pa_tag, CMD_CONF) &
		    CMD_CONF_DSA1)
			interface |= PCIIDE_INTERFACE_PCI(0) |
			    PCIIDE_INTERFACE_PCI(1);
	} else {
		interface = PCI_INTERFACE(pa->pa_class);
	}

	printf(": DMA");
	pciide_mapreg_dma(sc, pa);
	sc->sc_wdcdev.cap = WDC_CAPABILITY_DATA16 | WDC_CAPABILITY_DATA32 |
	    WDC_CAPABILITY_MODE;
	if (sc->sc_dma_ok) {
		sc->sc_wdcdev.cap |= WDC_CAPABILITY_DMA | WDC_CAPABILITY_IRQACK;
		switch (sc->sc_pp->ide_product) {
		case PCI_PRODUCT_CMDTECH_649:
			sc->sc_wdcdev.cap |= WDC_CAPABILITY_UDMA;
			sc->sc_wdcdev.UDMA_cap = 5;
			sc->sc_wdcdev.irqack = cmd646_9_irqack;
			break;
		case PCI_PRODUCT_CMDTECH_648:
			sc->sc_wdcdev.cap |= WDC_CAPABILITY_UDMA;
			sc->sc_wdcdev.UDMA_cap = 4;
			sc->sc_wdcdev.irqack = cmd646_9_irqack;
			break;
		case PCI_PRODUCT_CMDTECH_646:
			if (rev >= CMD0646U2_REV) {
				sc->sc_wdcdev.cap |= WDC_CAPABILITY_UDMA;
				sc->sc_wdcdev.UDMA_cap = 2;
			} else if (rev >= CMD0646U_REV) {
			/*
			 * Linux's driver claims that the 646U is broken
			 * with UDMA. Only enable it if we know what we're
			 * doing
			 */
#ifdef PCIIDE_CMD0646U_ENABLEUDMA
				sc->sc_wdcdev.cap |= WDC_CAPABILITY_UDMA;
				sc->sc_wdcdev.UDMA_cap = 2;
#endif
				/* explicitly disable UDMA */
				pciide_pci_write(sc->sc_pc, sc->sc_tag,
				    CMD_UDMATIM(0), 0);
				pciide_pci_write(sc->sc_pc, sc->sc_tag,
				    CMD_UDMATIM(1), 0);
			}
			sc->sc_wdcdev.irqack = cmd646_9_irqack;
			break;
		default:
			sc->sc_wdcdev.irqack = pciide_irqack;
		}
	}

	sc->sc_wdcdev.channels = sc->wdc_chanarray;
	sc->sc_wdcdev.nchannels = PCIIDE_NUM_CHANNELS;
	sc->sc_wdcdev.PIO_cap = 4;
	sc->sc_wdcdev.DMA_cap = 2;
	sc->sc_wdcdev.set_modes = cmd0643_9_setup_channel;

	pciide_print_channels(sc->sc_wdcdev.nchannels, interface);

	WDCDEBUG_PRINT(("cmd0643_9_chip_map: old timings reg 0x%x 0x%x\n",
		pci_conf_read(sc->sc_pc, sc->sc_tag, 0x54),
		pci_conf_read(sc->sc_pc, sc->sc_tag, 0x58)),
		DEBUG_PROBE);
	for (channel = 0; channel < sc->sc_wdcdev.nchannels; channel++) {
		cp = &sc->pciide_channels[channel];
		cmd_channel_map(pa, sc, channel);
		if (cp->hw_ok == 0)
			continue;
		cmd0643_9_setup_channel(&cp->wdc_channel);
	}
	/*
	 * note - this also makes sure we clear the irq disable and reset
	 * bits
	 */
	pciide_pci_write(sc->sc_pc, sc->sc_tag, CMD_DMA_MODE, CMD_DMA_MULTIPLE);
	WDCDEBUG_PRINT(("cmd0643_9_chip_map: timings reg now 0x%x 0x%x\n",
	    pci_conf_read(sc->sc_pc, sc->sc_tag, 0x54),
	    pci_conf_read(sc->sc_pc, sc->sc_tag, 0x58)),
	    DEBUG_PROBE);
}

void
cmd0643_9_setup_channel(struct channel_softc *chp)
{
	struct ata_drive_datas *drvp;
	u_int8_t tim;
	u_int32_t idedma_ctl, udma_reg;
	int drive;
	struct pciide_channel *cp = (struct pciide_channel *)chp;
	struct pciide_softc *sc = (struct pciide_softc *)cp->wdc_channel.wdc;

	idedma_ctl = 0;
	/* setup DMA if needed */
	pciide_channel_dma_setup(cp);

	for (drive = 0; drive < 2; drive++) {
		drvp = &chp->ch_drive[drive];
		/* If no drive, skip */
		if ((drvp->drive_flags & DRIVE) == 0)
			continue;
		/* add timing values, setup DMA if needed */
		tim = cmd0643_9_data_tim_pio[drvp->PIO_mode];
		if (drvp->drive_flags & (DRIVE_DMA | DRIVE_UDMA)) {
			if (drvp->drive_flags & DRIVE_UDMA) {
				/* UltraDMA on a 646U2, 0648 or 0649 */
				drvp->drive_flags &= ~DRIVE_DMA;
				udma_reg = pciide_pci_read(sc->sc_pc,
				    sc->sc_tag, CMD_UDMATIM(chp->channel));
				if (drvp->UDMA_mode > 2 &&
				    (pciide_pci_read(sc->sc_pc, sc->sc_tag,
				    CMD_BICSR) &
				    CMD_BICSR_80(chp->channel)) == 0) {
					WDCDEBUG_PRINT(("%s(%s:%d:%d): "
					    "80-wire cable not detected\n",
					    drvp->drive_name,
					    sc->sc_wdcdev.sc_dev.dv_xname,
					    chp->channel, drive), DEBUG_PROBE);
					drvp->UDMA_mode = 2;
				}
				if (drvp->UDMA_mode > 2)
					udma_reg &= ~CMD_UDMATIM_UDMA33(drive);
				else if (sc->sc_wdcdev.UDMA_cap > 2)
					udma_reg |= CMD_UDMATIM_UDMA33(drive);
				udma_reg |= CMD_UDMATIM_UDMA(drive);
				udma_reg &= ~(CMD_UDMATIM_TIM_MASK <<
				    CMD_UDMATIM_TIM_OFF(drive));
				udma_reg |=
				    (cmd0646_9_tim_udma[drvp->UDMA_mode] <<
				    CMD_UDMATIM_TIM_OFF(drive));
				pciide_pci_write(sc->sc_pc, sc->sc_tag,
				    CMD_UDMATIM(chp->channel), udma_reg);
			} else {
				/*
				 * use Multiword DMA.
				 * Timings will be used for both PIO and DMA,
				 * so adjust DMA mode if needed
				 * if we have a 0646U2/8/9, turn off UDMA
				 */
				if (sc->sc_wdcdev.cap & WDC_CAPABILITY_UDMA) {
					udma_reg = pciide_pci_read(sc->sc_pc,
					    sc->sc_tag,
					    CMD_UDMATIM(chp->channel));
					udma_reg &= ~CMD_UDMATIM_UDMA(drive);
					pciide_pci_write(sc->sc_pc, sc->sc_tag,
					    CMD_UDMATIM(chp->channel),
					    udma_reg);
				}
				if (drvp->PIO_mode >= 3 &&
				    (drvp->DMA_mode + 2) > drvp->PIO_mode) {
					drvp->DMA_mode = drvp->PIO_mode - 2;
				}
				tim = cmd0643_9_data_tim_dma[drvp->DMA_mode];
			}
			idedma_ctl |= IDEDMA_CTL_DRV_DMA(drive);
		}
		pciide_pci_write(sc->sc_pc, sc->sc_tag,
		    CMD_DATA_TIM(chp->channel, drive), tim);
	}
	if (idedma_ctl != 0) {
		/* Add software bits in status register */
		bus_space_write_1(sc->sc_dma_iot, sc->sc_dma_ioh,
		    IDEDMA_CTL(chp->channel),
		    idedma_ctl);
	}
	pciide_print_modes(cp);
#ifdef __sparc64__
	/*
	 * The Ultra 5 has a tendency to hang during reboot.  This is due
	 * to the PCI0646U asserting a PCI interrupt line when the chip
	 * registers claim that it is not.  Performing a reset at this
	 * point appears to eliminate the symptoms.  It is likely the
	 * real cause is still lurking somewhere in the code.
	 */
	wdcreset(chp, SILENT);
#endif /* __sparc64__ */
}

void
cmd646_9_irqack(struct channel_softc *chp)
{
	u_int32_t priirq, secirq;
	struct pciide_channel *cp = (struct pciide_channel *)chp;
	struct pciide_softc *sc = (struct pciide_softc *)cp->wdc_channel.wdc;

	if (chp->channel == 0) {
		priirq = pciide_pci_read(sc->sc_pc, sc->sc_tag, CMD_CONF);
		pciide_pci_write(sc->sc_pc, sc->sc_tag, CMD_CONF, priirq);
	} else {
		secirq = pciide_pci_read(sc->sc_pc, sc->sc_tag, CMD_ARTTIM23);
		pciide_pci_write(sc->sc_pc, sc->sc_tag, CMD_ARTTIM23, secirq);
	}
	pciide_irqack(chp);
}

void
cmd680_chip_map(struct pciide_softc *sc, struct pci_attach_args *pa)
{
	struct pciide_channel *cp;
	int channel;

	printf("\n%s: bus-master DMA support present",
	    sc->sc_wdcdev.sc_dev.dv_xname);
	pciide_mapreg_dma(sc, pa);
	printf("\n");
	sc->sc_wdcdev.cap = WDC_CAPABILITY_DATA16 | WDC_CAPABILITY_DATA32 |
	    WDC_CAPABILITY_MODE;
	if (sc->sc_dma_ok) {
		sc->sc_wdcdev.cap |= WDC_CAPABILITY_DMA | WDC_CAPABILITY_IRQACK;
		sc->sc_wdcdev.cap |= WDC_CAPABILITY_UDMA;
		sc->sc_wdcdev.UDMA_cap = 6;
		sc->sc_wdcdev.irqack = pciide_irqack;
	}

	sc->sc_wdcdev.channels = sc->wdc_chanarray;
	sc->sc_wdcdev.nchannels = PCIIDE_NUM_CHANNELS;
	sc->sc_wdcdev.PIO_cap = 4;
	sc->sc_wdcdev.DMA_cap = 2;
	sc->sc_wdcdev.set_modes = cmd680_setup_channel;

	pciide_pci_write(sc->sc_pc, sc->sc_tag, 0x80, 0x00);
	pciide_pci_write(sc->sc_pc, sc->sc_tag, 0x84, 0x00);
	pciide_pci_write(sc->sc_pc, sc->sc_tag, 0x8a,
	    pciide_pci_read(sc->sc_pc, sc->sc_tag, 0x8a) | 0x01);
	for (channel = 0; channel < sc->sc_wdcdev.nchannels; channel++) {
		cp = &sc->pciide_channels[channel];
		cmd680_channel_map(pa, sc, channel);
		if (cp->hw_ok == 0)
			continue;
		cmd680_setup_channel(&cp->wdc_channel);
	}
}

void
cmd680_channel_map(struct pci_attach_args *pa, struct pciide_softc *sc,
    int channel)
{
	struct pciide_channel *cp = &sc->pciide_channels[channel];
	bus_size_t cmdsize, ctlsize;
	int interface, i, reg;
	static const u_int8_t init_val[] =
	    {             0x8a, 0x32, 0x8a, 0x32, 0x8a, 0x32,
	      0x92, 0x43, 0x92, 0x43, 0x09, 0x40, 0x09, 0x40 };

	if (PCI_SUBCLASS(pa->pa_class) != PCI_SUBCLASS_MASS_STORAGE_IDE) {
		interface = PCIIDE_INTERFACE_SETTABLE(0) |
		    PCIIDE_INTERFACE_SETTABLE(1);
		interface |= PCIIDE_INTERFACE_PCI(0) |
		    PCIIDE_INTERFACE_PCI(1);
	} else {
		interface = PCI_INTERFACE(pa->pa_class);
	}

	sc->wdc_chanarray[channel] = &cp->wdc_channel;
	cp->name = PCIIDE_CHANNEL_NAME(channel);
	cp->wdc_channel.channel = channel;
	cp->wdc_channel.wdc = &sc->sc_wdcdev;

	cp->wdc_channel.ch_queue = wdc_alloc_queue();
	if (cp->wdc_channel.ch_queue == NULL) {
		printf("%s %s: "
		    "cannot allocate channel queue",
		    sc->sc_wdcdev.sc_dev.dv_xname, cp->name);
		return;
	}

	/* XXX */
	reg = 0xa2 + channel * 16;
	for (i = 0; i < sizeof(init_val); i++)
		pciide_pci_write(sc->sc_pc, sc->sc_tag, reg + i, init_val[i]);

	printf("%s: %s %s to %s mode\n",
	    sc->sc_wdcdev.sc_dev.dv_xname, cp->name,
	    (interface & PCIIDE_INTERFACE_SETTABLE(channel)) ?
	    "configured" : "wired",
	    (interface & PCIIDE_INTERFACE_PCI(channel)) ?
	    "native-PCI" : "compatibility");

	pciide_mapchan(pa, cp, interface, &cmdsize, &ctlsize, pciide_pci_intr);
	if (cp->hw_ok == 0)
		return;
	pciide_map_compat_intr(pa, cp, channel, interface);
}

void
cmd680_setup_channel(struct channel_softc *chp)
{
	struct ata_drive_datas *drvp;
	u_int8_t mode, off, scsc;
	u_int16_t val;
	u_int32_t idedma_ctl;
	int drive;
	struct pciide_channel *cp = (struct pciide_channel *)chp;
	struct pciide_softc *sc = (struct pciide_softc *)cp->wdc_channel.wdc;
	pci_chipset_tag_t pc = sc->sc_pc;
	pcitag_t pa = sc->sc_tag;
	static const u_int8_t udma2_tbl[] =
	    { 0x0f, 0x0b, 0x07, 0x06, 0x03, 0x02, 0x01 };
	static const u_int8_t udma_tbl[] =
	    { 0x0c, 0x07, 0x05, 0x04, 0x02, 0x01, 0x00 };
	static const u_int16_t dma_tbl[] =
	    { 0x2208, 0x10c2, 0x10c1 };
	static const u_int16_t pio_tbl[] =
	    { 0x328a, 0x2283, 0x1104, 0x10c3, 0x10c1 };

	idedma_ctl = 0;
	pciide_channel_dma_setup(cp);
	mode = pciide_pci_read(pc, pa, 0x80 + chp->channel * 4);

	for (drive = 0; drive < 2; drive++) {
		drvp = &chp->ch_drive[drive];
		/* If no drive, skip */
		if ((drvp->drive_flags & DRIVE) == 0)
			continue;
		mode &= ~(0x03 << (drive * 4));
		if (drvp->drive_flags & DRIVE_UDMA) {
			drvp->drive_flags &= ~DRIVE_DMA;
			off = 0xa0 + chp->channel * 16;
			if (drvp->UDMA_mode > 2 &&
			    (pciide_pci_read(pc, pa, off) & 0x01) == 0)
				drvp->UDMA_mode = 2;
			scsc = pciide_pci_read(pc, pa, 0x8a);
			if (drvp->UDMA_mode == 6 && (scsc & 0x30) == 0) {
				pciide_pci_write(pc, pa, 0x8a, scsc | 0x01);
				scsc = pciide_pci_read(pc, pa, 0x8a);
				if ((scsc & 0x30) == 0)
					drvp->UDMA_mode = 5;
			}
			mode |= 0x03 << (drive * 4);
			off = 0xac + chp->channel * 16 + drive * 2;
			val = pciide_pci_read(pc, pa, off) & ~0x3f;
			if (scsc & 0x30)
				val |= udma2_tbl[drvp->UDMA_mode];
			else
				val |= udma_tbl[drvp->UDMA_mode];
			pciide_pci_write(pc, pa, off, val);
			idedma_ctl |= IDEDMA_CTL_DRV_DMA(drive);
		} else if (drvp->drive_flags & DRIVE_DMA) {
			mode |= 0x02 << (drive * 4);
			off = 0xa8 + chp->channel * 16 + drive * 2;
			val = dma_tbl[drvp->DMA_mode];
			pciide_pci_write(pc, pa, off, val & 0xff);
			pciide_pci_write(pc, pa, off, val >> 8);
			idedma_ctl |= IDEDMA_CTL_DRV_DMA(drive);
		} else {
			mode |= 0x01 << (drive * 4);
			off = 0xa4 + chp->channel * 16 + drive * 2;
			val = pio_tbl[drvp->PIO_mode];
			pciide_pci_write(pc, pa, off, val & 0xff);
			pciide_pci_write(pc, pa, off, val >> 8);
		}
	}

	pciide_pci_write(pc, pa, 0x80 + chp->channel * 4, mode);
	if (idedma_ctl != 0) {
		/* Add software bits in status register */
		bus_space_write_1(sc->sc_dma_iot, sc->sc_dma_ioh,
		    IDEDMA_CTL(chp->channel),
		    idedma_ctl);
	}
	pciide_print_modes(cp);
}

/*
 * When the Silicon Image 3112 retries a PCI memory read command,
 * it may retry it as a memory read multiple command under some
 * circumstances.  This can totally confuse some PCI controllers,
 * so ensure that it will never do this by making sure that the
 * Read Threshold (FIFO Read Request Control) field of the FIFO
 * Valid Byte Count and Control registers for both channels (BA5
 * offset 0x40 and 0x44) are set to be at least as large as the
 * cacheline size register.
 */
void
sii_fixup_cacheline(struct pciide_softc *sc, struct pci_attach_args *pa)
{
	pcireg_t cls, reg40, reg44;

	cls = pci_conf_read(pa->pa_pc, pa->pa_tag, PCI_BHLC_REG);
	cls = (cls >> PCI_CACHELINE_SHIFT) & PCI_CACHELINE_MASK;
	cls *= 4;
	if (cls > 224) {
		cls = pci_conf_read(pa->pa_pc, pa->pa_tag, PCI_BHLC_REG);
		cls &= ~(PCI_CACHELINE_MASK << PCI_CACHELINE_SHIFT);
		cls |= ((224/4) << PCI_CACHELINE_SHIFT);
		pci_conf_write(pa->pa_pc, pa->pa_tag, PCI_BHLC_REG, cls);
		cls = 224;
	}
	if (cls < 32)
		cls = 32;
	cls = (cls + 31) / 32;
	reg40 = ba5_read_4(sc, 0x40);
	reg44 = ba5_read_4(sc, 0x44);
	if ((reg40 & 0x7) < cls)
		ba5_write_4(sc, 0x40, (reg40 & ~0x07) | cls);
	if ((reg44 & 0x7) < cls)
		ba5_write_4(sc, 0x44, (reg44 & ~0x07) | cls);
}

void
sii3112_chip_map(struct pciide_softc *sc, struct pci_attach_args *pa)
{
	struct pciide_channel *cp;
	bus_size_t cmdsize, ctlsize;
	pcireg_t interface, scs_cmd, cfgctl;
	int channel;
	struct pciide_satalink *sl;

	/* Allocate memory for private data */
	sc->sc_cookielen = sizeof(*sl);
	sc->sc_cookie = malloc(sc->sc_cookielen, M_DEVBUF, M_NOWAIT | M_ZERO);
	sl = sc->sc_cookie;

	sc->chip_unmap = default_chip_unmap;

#define	SII3112_RESET_BITS						\
	(SCS_CMD_PBM_RESET | SCS_CMD_ARB_RESET |			\
	 SCS_CMD_FF1_RESET | SCS_CMD_FF0_RESET |			\
	 SCS_CMD_IDE1_RESET | SCS_CMD_IDE0_RESET)

	/*
	 * Reset everything and then unblock all of the interrupts.
	 */
	scs_cmd = pci_conf_read(pa->pa_pc, pa->pa_tag, SII3112_SCS_CMD);
	pci_conf_write(pa->pa_pc, pa->pa_tag, SII3112_SCS_CMD,
		       scs_cmd | SII3112_RESET_BITS);
	delay(50 * 1000);
	pci_conf_write(pa->pa_pc, pa->pa_tag, SII3112_SCS_CMD,
		       scs_cmd & SCS_CMD_BA5_EN);
	delay(50 * 1000);

	if (scs_cmd & SCS_CMD_BA5_EN) {
		if (pci_mapreg_map(pa, PCI_MAPREG_START + 0x14,
				   PCI_MAPREG_TYPE_MEM |
				   PCI_MAPREG_MEM_TYPE_32BIT, 0,
				   &sl->ba5_st, &sl->ba5_sh,
				   NULL, NULL, 0) != 0)
			printf(": unable to map BA5 register space\n");
		else
			sl->ba5_en = 1;
	} else {
		cfgctl = pci_conf_read(pa->pa_pc, pa->pa_tag,
				       SII3112_PCI_CFGCTL);
		pci_conf_write(pa->pa_pc, pa->pa_tag, SII3112_PCI_CFGCTL,
			       cfgctl | CFGCTL_BA5INDEN);
	}

	printf(": DMA");
	pciide_mapreg_dma(sc, pa);
	printf("\n");

	/*
	 * Rev. <= 0x01 of the 3112 have a bug that can cause data
	 * corruption if DMA transfers cross an 8K boundary.  This is
	 * apparently hard to tickle, but we'll go ahead and play it
	 * safe.
	 */
	if (sc->sc_rev <= 0x01) {
		sc->sc_dma_maxsegsz = 8192;
		sc->sc_dma_boundary = 8192;
	}

	sii_fixup_cacheline(sc, pa);

	sc->sc_wdcdev.cap |= WDC_CAPABILITY_DATA16 | WDC_CAPABILITY_DATA32;
	sc->sc_wdcdev.PIO_cap = 4;
	if (sc->sc_dma_ok) {
		sc->sc_wdcdev.cap |= WDC_CAPABILITY_DMA | WDC_CAPABILITY_UDMA;
		sc->sc_wdcdev.cap |= WDC_CAPABILITY_IRQACK;
		sc->sc_wdcdev.irqack = pciide_irqack;
		sc->sc_wdcdev.DMA_cap = 2;
		sc->sc_wdcdev.UDMA_cap = 6;
	}
	sc->sc_wdcdev.set_modes = sii3112_setup_channel;

	/* We can use SControl and SStatus to probe for drives. */
	sc->sc_wdcdev.drv_probe = sii3112_drv_probe;

	sc->sc_wdcdev.channels = sc->wdc_chanarray;
	sc->sc_wdcdev.nchannels = PCIIDE_NUM_CHANNELS;

	/*
	 * The 3112 either identifies itself as a RAID storage device
	 * or a Misc storage device.  Fake up the interface bits for
	 * what our driver expects.
	 */
	if (PCI_SUBCLASS(pa->pa_class) == PCI_SUBCLASS_MASS_STORAGE_IDE) {
		interface = PCI_INTERFACE(pa->pa_class);
	} else {
		interface = PCIIDE_INTERFACE_BUS_MASTER_DMA |
		    PCIIDE_INTERFACE_PCI(0) | PCIIDE_INTERFACE_PCI(1);
	}

	for (channel = 0; channel < sc->sc_wdcdev.nchannels; channel++) {
		cp = &sc->pciide_channels[channel];
		if (pciide_chansetup(sc, channel, interface) == 0)
			continue;
		pciide_mapchan(pa, cp, interface, &cmdsize, &ctlsize,
		    pciide_pci_intr);
		if (cp->hw_ok == 0)
			continue;
		sc->sc_wdcdev.set_modes(&cp->wdc_channel);
	}
}

void
sii3112_setup_channel(struct channel_softc *chp)
{
	struct ata_drive_datas *drvp;
	int drive;
	u_int32_t idedma_ctl, dtm;
	struct pciide_channel *cp = (struct pciide_channel *)chp;
	struct pciide_softc *sc = (struct pciide_softc *)cp->wdc_channel.wdc;

	/* setup DMA if needed */
	pciide_channel_dma_setup(cp);

	idedma_ctl = 0;
	dtm = 0;

	for (drive = 0; drive < 2; drive++) {
		drvp = &chp->ch_drive[drive];
		/* If no drive, skip */
		if ((drvp->drive_flags & DRIVE) == 0)
			continue;
		if (drvp->drive_flags & DRIVE_UDMA) {
			/* use Ultra/DMA */
			drvp->drive_flags &= ~DRIVE_DMA;
			idedma_ctl |= IDEDMA_CTL_DRV_DMA(drive);
			dtm |= DTM_IDEx_DMA;
		} else if (drvp->drive_flags & DRIVE_DMA) {
			idedma_ctl |= IDEDMA_CTL_DRV_DMA(drive);
			dtm |= DTM_IDEx_DMA;
		} else {
			dtm |= DTM_IDEx_PIO;
		}
	}

	/*
	 * Nothing to do to setup modes; it is meaningless in S-ATA
	 * (but many S-ATA drives still want to get the SET_FEATURE
	 * command).
	 */
	if (idedma_ctl != 0) {
		/* Add software bits in status register */
		PCIIDE_DMACTL_WRITE(sc, chp->channel, idedma_ctl);
	}
	BA5_WRITE_4(sc, chp->channel, ba5_IDE_DTM, dtm);
	pciide_print_modes(cp);
}

void
sii3112_drv_probe(struct channel_softc *chp)
{
	struct pciide_channel *cp = (struct pciide_channel *)chp;
	struct pciide_softc *sc = (struct pciide_softc *)cp->wdc_channel.wdc;
	uint32_t scontrol, sstatus;
	uint8_t scnt, sn, cl, ch;
	int s;

	/*
	 * The 3112 is a 2-port part, and only has one drive per channel
	 * (each port emulates a master drive).
	 *
	 * The 3114 is similar, but has 4 channels.
	 */

	/*
	 * Request communication initialization sequence, any speed.
	 * Performing this is the equivalent of an ATA Reset.
	 */
	scontrol = SControl_DET_INIT | SControl_SPD_ANY;

	/*
	 * XXX We don't yet support SATA power management; disable all
	 * power management state transitions.
	 */
	scontrol |= SControl_IPM_NONE;

	BA5_WRITE_4(sc, chp->channel, ba5_SControl, scontrol);
	delay(50 * 1000);
	scontrol &= ~SControl_DET_INIT;
	BA5_WRITE_4(sc, chp->channel, ba5_SControl, scontrol);
	delay(50 * 1000);

	sstatus = BA5_READ_4(sc, chp->channel, ba5_SStatus);
#if 0
	printf("%s: port %d: SStatus=0x%08x, SControl=0x%08x\n",
	    sc->sc_wdcdev.sc_dev.dv_xname, chp->channel, sstatus,
	    BA5_READ_4(sc, chp->channel, ba5_SControl));
#endif
	switch (sstatus & SStatus_DET_mask) {
	case SStatus_DET_NODEV:
		/* No device; be silent. */
		break;

	case SStatus_DET_DEV_NE:
		printf("%s: port %d: device connected, but "
		    "communication not established\n",
		    sc->sc_wdcdev.sc_dev.dv_xname, chp->channel);
		break;

	case SStatus_DET_OFFLINE:
		printf("%s: port %d: PHY offline\n",
		    sc->sc_wdcdev.sc_dev.dv_xname, chp->channel);
		break;

	case SStatus_DET_DEV:
		/*
		 * XXX ATAPI detection doesn't currently work.  Don't
		 * XXX know why.  But, it's not like the standard method
		 * XXX can detect an ATAPI device connected via a SATA/PATA
		 * XXX bridge, so at least this is no worse.  --thorpej
		 */
		if (chp->_vtbl != NULL)
			CHP_WRITE_REG(chp, wdr_sdh, WDSD_IBM | (0 << 4));
		else
			bus_space_write_1(chp->cmd_iot, chp->cmd_ioh,
			    wdr_sdh & _WDC_REGMASK, WDSD_IBM | (0 << 4));
		delay(10);	/* 400ns delay */
		/* Save register contents. */
		if (chp->_vtbl != NULL) {
			scnt = CHP_READ_REG(chp, wdr_seccnt);
			sn = CHP_READ_REG(chp, wdr_sector);
			cl = CHP_READ_REG(chp, wdr_cyl_lo);
			ch = CHP_READ_REG(chp, wdr_cyl_hi);
		} else {
			scnt = bus_space_read_1(chp->cmd_iot,
			    chp->cmd_ioh, wdr_seccnt & _WDC_REGMASK);
			sn = bus_space_read_1(chp->cmd_iot,
			    chp->cmd_ioh, wdr_sector & _WDC_REGMASK);
			cl = bus_space_read_1(chp->cmd_iot,
			    chp->cmd_ioh, wdr_cyl_lo & _WDC_REGMASK);
			ch = bus_space_read_1(chp->cmd_iot,
			    chp->cmd_ioh, wdr_cyl_hi & _WDC_REGMASK);
		}
#if 0
		printf("%s: port %d: scnt=0x%x sn=0x%x cl=0x%x ch=0x%x\n",
		    sc->sc_wdcdev.sc_dev.dv_xname, chp->channel,
		    scnt, sn, cl, ch);
#endif
		/*
		 * scnt and sn are supposed to be 0x1 for ATAPI, but in some
		 * cases we get wrong values here, so ignore it.
		 */
		s = splbio();
		if (cl == 0x14 && ch == 0xeb)
			chp->ch_drive[0].drive_flags |= DRIVE_ATAPI;
		else
			chp->ch_drive[0].drive_flags |= DRIVE_ATA;
		splx(s);

		printf("%s: port %d",
		    sc->sc_wdcdev.sc_dev.dv_xname, chp->channel);
		switch ((sstatus & SStatus_SPD_mask) >> SStatus_SPD_shift) {
		case 1:
			printf(": 1.5Gb/s");
			break;
		case 2:
			printf(": 3.0Gb/s");
			break;
		}
		printf("\n");
		break;

	default:
		printf("%s: port %d: unknown SStatus: 0x%08x\n",
		    sc->sc_wdcdev.sc_dev.dv_xname, chp->channel, sstatus);
	}
}

void
sii3114_chip_map(struct pciide_softc *sc, struct pci_attach_args *pa)
{
	struct pciide_channel *cp;
	pcireg_t scs_cmd;
	pci_intr_handle_t intrhandle;
	const char *intrstr;
	int channel;
	struct pciide_satalink *sl;

	/* Allocate memory for private data */
	sc->sc_cookielen = sizeof(*sl);
	sc->sc_cookie = malloc(sc->sc_cookielen, M_DEVBUF, M_NOWAIT | M_ZERO);
	sl = sc->sc_cookie;

#define	SII3114_RESET_BITS						\
	(SCS_CMD_PBM_RESET | SCS_CMD_ARB_RESET |			\
	 SCS_CMD_FF1_RESET | SCS_CMD_FF0_RESET |			\
	 SCS_CMD_FF3_RESET | SCS_CMD_FF2_RESET |			\
	 SCS_CMD_IDE1_RESET | SCS_CMD_IDE0_RESET |			\
	 SCS_CMD_IDE3_RESET | SCS_CMD_IDE2_RESET)

	/*
	 * Reset everything and then unblock all of the interrupts.
	 */
	scs_cmd = pci_conf_read(pa->pa_pc, pa->pa_tag, SII3112_SCS_CMD);
	pci_conf_write(pa->pa_pc, pa->pa_tag, SII3112_SCS_CMD,
		       scs_cmd | SII3114_RESET_BITS);
	delay(50 * 1000);
	pci_conf_write(pa->pa_pc, pa->pa_tag, SII3112_SCS_CMD,
		       scs_cmd & SCS_CMD_M66EN);
	delay(50 * 1000);

	/*
	 * On the 3114, the BA5 register space is always enabled.  In
	 * order to use the 3114 in any sane way, we must use this BA5
	 * register space, and so we consider it an error if we cannot
	 * map it.
	 *
	 * As a consequence of using BA5, our register mapping is different
	 * from a normal PCI IDE controller's, and so we are unable to use
	 * most of the common PCI IDE register mapping functions.
	 */
	if (pci_mapreg_map(pa, PCI_MAPREG_START + 0x14,
			   PCI_MAPREG_TYPE_MEM |
			   PCI_MAPREG_MEM_TYPE_32BIT, 0,
			   &sl->ba5_st, &sl->ba5_sh,
			   NULL, NULL, 0) != 0) {
		printf(": unable to map BA5 register space\n");
		return;
	}
	sl->ba5_en = 1;

	/*
	 * Set the Interrupt Steering bit in the IDEDMA_CMD register of
	 * channel 2.  This is required at all times for proper operation
	 * when using the BA5 register space (otherwise interrupts from
	 * all 4 channels won't work).
	 */
	BA5_WRITE_4(sc, 2, ba5_IDEDMA_CMD, IDEDMA_CMD_INT_STEER);

	printf(": DMA");
	sii3114_mapreg_dma(sc, pa);
	printf("\n");

	sii_fixup_cacheline(sc, pa);

	sc->sc_wdcdev.cap |= WDC_CAPABILITY_DATA16 | WDC_CAPABILITY_DATA32;
	sc->sc_wdcdev.PIO_cap = 4;
	if (sc->sc_dma_ok) {
		sc->sc_wdcdev.cap |= WDC_CAPABILITY_DMA | WDC_CAPABILITY_UDMA;
		sc->sc_wdcdev.cap |= WDC_CAPABILITY_IRQACK;
		sc->sc_wdcdev.irqack = pciide_irqack;
		sc->sc_wdcdev.DMA_cap = 2;
		sc->sc_wdcdev.UDMA_cap = 6;
	}
	sc->sc_wdcdev.set_modes = sii3112_setup_channel;

	/* We can use SControl and SStatus to probe for drives. */
	sc->sc_wdcdev.drv_probe = sii3112_drv_probe;

	sc->sc_wdcdev.channels = sc->wdc_chanarray;
	sc->sc_wdcdev.nchannels = 4;

	/* Map and establish the interrupt handler. */
	if (pci_intr_map(pa, &intrhandle) != 0) {
		printf("%s: couldn't map native-PCI interrupt\n",
		    sc->sc_wdcdev.sc_dev.dv_xname);
		return;
	}
	intrstr = pci_intr_string(pa->pa_pc, intrhandle);
	sc->sc_pci_ih = pci_intr_establish(pa->pa_pc, intrhandle, IPL_BIO,
					   /* XXX */
					   pciide_pci_intr, sc,
					   sc->sc_wdcdev.sc_dev.dv_xname);
	if (sc->sc_pci_ih != NULL) {
		printf("%s: using %s for native-PCI interrupt\n",
		    sc->sc_wdcdev.sc_dev.dv_xname,
		    intrstr ? intrstr : "unknown interrupt");
	} else {
		printf("%s: couldn't establish native-PCI interrupt",
		    sc->sc_wdcdev.sc_dev.dv_xname);
		if (intrstr != NULL)
			printf(" at %s", intrstr);
		printf("\n");
		return;
	}

	for (channel = 0; channel < sc->sc_wdcdev.nchannels; channel++) {
		cp = &sc->pciide_channels[channel];
		if (sii3114_chansetup(sc, channel) == 0)
			continue;
		sii3114_mapchan(cp);
		if (cp->hw_ok == 0)
			continue;
		sc->sc_wdcdev.set_modes(&cp->wdc_channel);
	}
}

void
sii3114_mapreg_dma(struct pciide_softc *sc, struct pci_attach_args *pa)
{
	int chan, reg;
	bus_size_t size;
	struct pciide_satalink *sl = sc->sc_cookie;

	sc->sc_wdcdev.dma_arg = sc;
	sc->sc_wdcdev.dma_init = pciide_dma_init;
	sc->sc_wdcdev.dma_start = pciide_dma_start;
	sc->sc_wdcdev.dma_finish = pciide_dma_finish;

	/*
	 * Slice off a subregion of BA5 for each of the channel's DMA
	 * registers.
	 */

	sc->sc_dma_iot = sl->ba5_st;
	for (chan = 0; chan < 4; chan++) {
		for (reg = 0; reg < IDEDMA_NREGS; reg++) {
			size = 4;
			if (size > (IDEDMA_SCH_OFFSET - reg))
				size = IDEDMA_SCH_OFFSET - reg;
			if (bus_space_subregion(sl->ba5_st,
			    sl->ba5_sh,
			    satalink_ba5_regmap[chan].ba5_IDEDMA_CMD + reg,
			    size, &sl->regs[chan].dma_iohs[reg]) != 0) {
				sc->sc_dma_ok = 0;
				printf(": can't subregion offset "
				    "%lu size %lu",
				    (u_long) satalink_ba5_regmap[
						chan].ba5_IDEDMA_CMD + reg,
				    (u_long) size);
				return;
			}
		}
	}

	sc->sc_dmacmd_read = sii3114_dmacmd_read;
	sc->sc_dmacmd_write = sii3114_dmacmd_write;
	sc->sc_dmactl_read = sii3114_dmactl_read;
	sc->sc_dmactl_write = sii3114_dmactl_write;
	sc->sc_dmatbl_write = sii3114_dmatbl_write;

	/* DMA registers all set up! */
	sc->sc_dmat = pa->pa_dmat;
	sc->sc_dma_ok = 1;
}

int
sii3114_chansetup(struct pciide_softc *sc, int channel)
{
	static const char *channel_names[] = {
		"port 0",
		"port 1",
		"port 2",
		"port 3",
	};
	struct pciide_channel *cp = &sc->pciide_channels[channel];

	sc->wdc_chanarray[channel] = &cp->wdc_channel;

	/*
	 * We must always keep the Interrupt Steering bit set in channel 2's
	 * IDEDMA_CMD register.
	 */
	if (channel == 2)
		cp->idedma_cmd = IDEDMA_CMD_INT_STEER;

	cp->name = channel_names[channel];
	cp->wdc_channel.channel = channel;
	cp->wdc_channel.wdc = &sc->sc_wdcdev;
	cp->wdc_channel.ch_queue = wdc_alloc_queue();
	if (cp->wdc_channel.ch_queue == NULL) {
		printf("%s %s channel: "
		    "cannot allocate channel queue",
		    sc->sc_wdcdev.sc_dev.dv_xname, cp->name);
		return (0);
	}
	return (1);
}

void
sii3114_mapchan(struct pciide_channel *cp)
{
	struct channel_softc *wdc_cp = &cp->wdc_channel;
	struct pciide_softc *sc = (struct pciide_softc *)cp->wdc_channel.wdc;
	struct pciide_satalink *sl = sc->sc_cookie;
	int chan = wdc_cp->channel;
	int i;

	cp->hw_ok = 0;
	cp->compat = 0;
	cp->ih = sc->sc_pci_ih;

	sl->regs[chan].cmd_iot = sl->ba5_st;
	if (bus_space_subregion(sl->ba5_st, sl->ba5_sh,
			satalink_ba5_regmap[chan].ba5_IDE_TF0,
			9, &sl->regs[chan].cmd_baseioh) != 0) {
		printf("%s: couldn't subregion %s cmd base\n",
		    sc->sc_wdcdev.sc_dev.dv_xname, cp->name);
		return;
	}

	sl->regs[chan].ctl_iot = sl->ba5_st;
	if (bus_space_subregion(sl->ba5_st, sl->ba5_sh,
			satalink_ba5_regmap[chan].ba5_IDE_TF8,
			1, &cp->ctl_baseioh) != 0) {
		printf("%s: couldn't subregion %s ctl base\n",
		    sc->sc_wdcdev.sc_dev.dv_xname, cp->name);
		return;
	}
	sl->regs[chan].ctl_ioh = cp->ctl_baseioh;

	for (i = 0; i < WDC_NREG; i++) {
		if (bus_space_subregion(sl->regs[chan].cmd_iot,
		    sl->regs[chan].cmd_baseioh,
		    i, i == 0 ? 4 : 1,
		    &sl->regs[chan].cmd_iohs[i]) != 0) {
			printf("%s: couldn't subregion %s channel "
			    "cmd regs\n",
			    sc->sc_wdcdev.sc_dev.dv_xname, cp->name);
			return;
		}
	}
	sl->regs[chan].cmd_iohs[wdr_status & _WDC_REGMASK] =
	    sl->regs[chan].cmd_iohs[wdr_command & _WDC_REGMASK];
	sl->regs[chan].cmd_iohs[wdr_features & _WDC_REGMASK] =
	    sl->regs[chan].cmd_iohs[wdr_error & _WDC_REGMASK];
	wdc_cp->data32iot = wdc_cp->cmd_iot = sl->regs[chan].cmd_iot;
	wdc_cp->data32ioh = wdc_cp->cmd_ioh = sl->regs[chan].cmd_iohs[0];
	wdc_cp->_vtbl = &wdc_sii3114_vtbl;
	wdcattach(wdc_cp);
	cp->hw_ok = 1;
}

u_int8_t
sii3114_read_reg(struct channel_softc *chp, enum wdc_regs reg)
{
	struct pciide_channel *cp = (struct pciide_channel *)chp;
	struct pciide_softc *sc = (struct pciide_softc *)cp->wdc_channel.wdc;
	struct pciide_satalink *sl = sc->sc_cookie;

	if (reg & _WDC_AUX)
		return (bus_space_read_1(sl->regs[chp->channel].ctl_iot,
		    sl->regs[chp->channel].ctl_ioh, reg & _WDC_REGMASK));
	else
		return (bus_space_read_1(sl->regs[chp->channel].cmd_iot,
		    sl->regs[chp->channel].cmd_iohs[reg & _WDC_REGMASK], 0));
}

void
sii3114_write_reg(struct channel_softc *chp, enum wdc_regs reg, u_int8_t val)
{
	struct pciide_channel *cp = (struct pciide_channel *)chp;
	struct pciide_softc *sc = (struct pciide_softc *)cp->wdc_channel.wdc;
	struct pciide_satalink *sl = sc->sc_cookie;

	if (reg & _WDC_AUX)
		bus_space_write_1(sl->regs[chp->channel].ctl_iot,
		    sl->regs[chp->channel].ctl_ioh, reg & _WDC_REGMASK, val);
	else
		bus_space_write_1(sl->regs[chp->channel].cmd_iot,
		    sl->regs[chp->channel].cmd_iohs[reg & _WDC_REGMASK],
		    0, val);
}

u_int8_t
sii3114_dmacmd_read(struct pciide_softc *sc, int chan)
{
	struct pciide_satalink *sl = sc->sc_cookie;

	return (bus_space_read_1(sc->sc_dma_iot,
	    sl->regs[chan].dma_iohs[IDEDMA_CMD(0)], 0));
}

void
sii3114_dmacmd_write(struct pciide_softc *sc, int chan, u_int8_t val)
{
	struct pciide_satalink *sl = sc->sc_cookie;

	bus_space_write_1(sc->sc_dma_iot,
	    sl->regs[chan].dma_iohs[IDEDMA_CMD(0)], 0, val);
}

u_int8_t
sii3114_dmactl_read(struct pciide_softc *sc, int chan)
{
	struct pciide_satalink *sl = sc->sc_cookie;

	return (bus_space_read_1(sc->sc_dma_iot,
	    sl->regs[chan].dma_iohs[IDEDMA_CTL(0)], 0));
}

void
sii3114_dmactl_write(struct pciide_softc *sc, int chan, u_int8_t val)
{
	struct pciide_satalink *sl = sc->sc_cookie;

	bus_space_write_1(sc->sc_dma_iot,
	    sl->regs[chan].dma_iohs[IDEDMA_CTL(0)], 0, val);
}

void
sii3114_dmatbl_write(struct pciide_softc *sc, int chan, u_int32_t val)
{
	struct pciide_satalink *sl = sc->sc_cookie;

	bus_space_write_4(sc->sc_dma_iot,
	    sl->regs[chan].dma_iohs[IDEDMA_TBL(0)], 0, val);
}

void
cy693_chip_map(struct pciide_softc *sc, struct pci_attach_args *pa)
{
	struct pciide_channel *cp;
	pcireg_t interface = PCI_INTERFACE(pa->pa_class);
	bus_size_t cmdsize, ctlsize;
	struct pciide_cy *cy;

	/* Allocate memory for private data */
	sc->sc_cookielen = sizeof(*cy);
	sc->sc_cookie = malloc(sc->sc_cookielen, M_DEVBUF, M_NOWAIT | M_ZERO);
	cy = sc->sc_cookie;

	/*
	 * this chip has 2 PCI IDE functions, one for primary and one for
	 * secondary. So we need to call pciide_mapregs_compat() with
	 * the real channel
	 */
	if (pa->pa_function == 1) {
		cy->cy_compatchan = 0;
	} else if (pa->pa_function == 2) {
		cy->cy_compatchan = 1;
	} else {
		printf(": unexpected PCI function %d\n", pa->pa_function);
		return;
	}

	if (interface & PCIIDE_INTERFACE_BUS_MASTER_DMA) {
		printf(": DMA");
		pciide_mapreg_dma(sc, pa);
	} else {
		printf(": no DMA");
		sc->sc_dma_ok = 0;
	}

	cy->cy_handle = cy82c693_init(pa->pa_iot);
	if (cy->cy_handle == NULL) {
		printf(", (unable to map ctl registers)");
		sc->sc_dma_ok = 0;
	}

	sc->sc_wdcdev.cap = WDC_CAPABILITY_DATA16 | WDC_CAPABILITY_DATA32 |
	    WDC_CAPABILITY_MODE;
	if (sc->sc_dma_ok) {
		sc->sc_wdcdev.cap |= WDC_CAPABILITY_DMA | WDC_CAPABILITY_IRQACK;
		sc->sc_wdcdev.irqack = pciide_irqack;
	}
	sc->sc_wdcdev.PIO_cap = 4;
	sc->sc_wdcdev.DMA_cap = 2;
	sc->sc_wdcdev.set_modes = cy693_setup_channel;

	sc->sc_wdcdev.channels = sc->wdc_chanarray;
	sc->sc_wdcdev.nchannels = 1;

	/* Only one channel for this chip; if we are here it's enabled */
	cp = &sc->pciide_channels[0];
	sc->wdc_chanarray[0] = &cp->wdc_channel;
	cp->name = PCIIDE_CHANNEL_NAME(0);
	cp->wdc_channel.channel = 0;
	cp->wdc_channel.wdc = &sc->sc_wdcdev;
	cp->wdc_channel.ch_queue = wdc_alloc_queue();
	if (cp->wdc_channel.ch_queue == NULL) {
		printf(": cannot allocate channel queue\n");
		return;
	}
	printf(", %s %s to ", PCIIDE_CHANNEL_NAME(0),
	    (interface & PCIIDE_INTERFACE_SETTABLE(0)) ?
	    "configured" : "wired");
	if (interface & PCIIDE_INTERFACE_PCI(0)) {
		printf("native-PCI\n");
		cp->hw_ok = pciide_mapregs_native(pa, cp, &cmdsize, &ctlsize,
		    pciide_pci_intr);
	} else {
		printf("compatibility\n");
		cp->hw_ok = pciide_mapregs_compat(pa, cp, cy->cy_compatchan,
		    &cmdsize, &ctlsize);
	}

	cp->wdc_channel.data32iot = cp->wdc_channel.cmd_iot;
	cp->wdc_channel.data32ioh = cp->wdc_channel.cmd_ioh;
	pciide_map_compat_intr(pa, cp, cy->cy_compatchan, interface);
	if (cp->hw_ok == 0)
		return;
	wdcattach(&cp->wdc_channel);
	if (pciide_chan_candisable(cp)) {
		pci_conf_write(sc->sc_pc, sc->sc_tag,
		    PCI_COMMAND_STATUS_REG, 0);
	}
	if (cp->hw_ok == 0) {
		pciide_unmap_compat_intr(pa, cp, cy->cy_compatchan,
		    interface);
		return;
	}

	WDCDEBUG_PRINT(("cy693_chip_map: old timings reg 0x%x\n",
	    pci_conf_read(sc->sc_pc, sc->sc_tag, CY_CMD_CTRL)), DEBUG_PROBE);
	cy693_setup_channel(&cp->wdc_channel);
	WDCDEBUG_PRINT(("cy693_chip_map: new timings reg 0x%x\n",
	    pci_conf_read(sc->sc_pc, sc->sc_tag, CY_CMD_CTRL)), DEBUG_PROBE);
}

void
cy693_setup_channel(struct channel_softc *chp)
{
	struct ata_drive_datas *drvp;
	int drive;
	u_int32_t cy_cmd_ctrl;
	u_int32_t idedma_ctl;
	struct pciide_channel *cp = (struct pciide_channel *)chp;
	struct pciide_softc *sc = (struct pciide_softc *)cp->wdc_channel.wdc;
	int dma_mode = -1;
	struct pciide_cy *cy = sc->sc_cookie;

	cy_cmd_ctrl = idedma_ctl = 0;

	/* setup DMA if needed */
	pciide_channel_dma_setup(cp);

	for (drive = 0; drive < 2; drive++) {
		drvp = &chp->ch_drive[drive];
		/* If no drive, skip */
		if ((drvp->drive_flags & DRIVE) == 0)
			continue;
		/* add timing values, setup DMA if needed */
		if (drvp->drive_flags & DRIVE_DMA) {
			idedma_ctl |= IDEDMA_CTL_DRV_DMA(drive);
			/* use Multiword DMA */
			if (dma_mode == -1 || dma_mode > drvp->DMA_mode)
				dma_mode = drvp->DMA_mode;
		}
		cy_cmd_ctrl |= (cy_pio_pulse[drvp->PIO_mode] <<
		    CY_CMD_CTRL_IOW_PULSE_OFF(drive));
		cy_cmd_ctrl |= (cy_pio_rec[drvp->PIO_mode] <<
		    CY_CMD_CTRL_IOW_REC_OFF(drive));
		cy_cmd_ctrl |= (cy_pio_pulse[drvp->PIO_mode] <<
		    CY_CMD_CTRL_IOR_PULSE_OFF(drive));
		cy_cmd_ctrl |= (cy_pio_rec[drvp->PIO_mode] <<
		    CY_CMD_CTRL_IOR_REC_OFF(drive));
	}
	pci_conf_write(sc->sc_pc, sc->sc_tag, CY_CMD_CTRL, cy_cmd_ctrl);
	chp->ch_drive[0].DMA_mode = dma_mode;
	chp->ch_drive[1].DMA_mode = dma_mode;

	if (dma_mode == -1)
		dma_mode = 0;

	if (cy->cy_handle != NULL) {
		/* Note: `multiple' is implied. */
		cy82c693_write(cy->cy_handle,
		    (cy->cy_compatchan == 0) ?
		    CY_DMA_IDX_PRIMARY : CY_DMA_IDX_SECONDARY, dma_mode);
	}

	pciide_print_modes(cp);

	if (idedma_ctl != 0) {
		/* Add software bits in status register */
		bus_space_write_1(sc->sc_dma_iot, sc->sc_dma_ioh,
		    IDEDMA_CTL(chp->channel), idedma_ctl);
	}
}

static struct sis_hostbr_type {
	u_int16_t id;
	u_int8_t rev;
	u_int8_t udma_mode;
	char *name;
	u_int8_t type;
#define SIS_TYPE_NOUDMA	0
#define SIS_TYPE_66	1
#define SIS_TYPE_100OLD	2
#define SIS_TYPE_100NEW 3
#define SIS_TYPE_133OLD 4
#define SIS_TYPE_133NEW 5
#define SIS_TYPE_SOUTH	6
} sis_hostbr_type[] = {
	/* Most infos here are from sos@freebsd.org */
	{PCI_PRODUCT_SIS_530, 0x00, 4, "530", SIS_TYPE_66},
#if 0
	/*
	 * controllers associated to a rev 0x2 530 Host to PCI Bridge
	 * have problems with UDMA (info provided by Christos)
	 */
	{PCI_PRODUCT_SIS_530, 0x02, 0, "530 (buggy)", SIS_TYPE_NOUDMA},
#endif
	{PCI_PRODUCT_SIS_540, 0x00, 4, "540", SIS_TYPE_66},
	{PCI_PRODUCT_SIS_550, 0x00, 4, "550", SIS_TYPE_66},
	{PCI_PRODUCT_SIS_620, 0x00, 4, "620", SIS_TYPE_66},
	{PCI_PRODUCT_SIS_630, 0x00, 4, "630", SIS_TYPE_66},
	{PCI_PRODUCT_SIS_630, 0x30, 5, "630S", SIS_TYPE_100NEW},
	{PCI_PRODUCT_SIS_633, 0x00, 5, "633", SIS_TYPE_100NEW},
	{PCI_PRODUCT_SIS_635, 0x00, 5, "635", SIS_TYPE_100NEW},
	{PCI_PRODUCT_SIS_640, 0x00, 4, "640", SIS_TYPE_SOUTH},
	{PCI_PRODUCT_SIS_645, 0x00, 6, "645", SIS_TYPE_SOUTH},
	{PCI_PRODUCT_SIS_646, 0x00, 6, "645DX", SIS_TYPE_SOUTH},
	{PCI_PRODUCT_SIS_648, 0x00, 6, "648", SIS_TYPE_SOUTH},
	{PCI_PRODUCT_SIS_650, 0x00, 6, "650", SIS_TYPE_SOUTH},
	{PCI_PRODUCT_SIS_651, 0x00, 6, "651", SIS_TYPE_SOUTH},
	{PCI_PRODUCT_SIS_652, 0x00, 6, "652", SIS_TYPE_SOUTH},
	{PCI_PRODUCT_SIS_655, 0x00, 6, "655", SIS_TYPE_SOUTH},
	{PCI_PRODUCT_SIS_658, 0x00, 6, "658", SIS_TYPE_SOUTH},
	{PCI_PRODUCT_SIS_661, 0x00, 6, "661", SIS_TYPE_SOUTH},
	{PCI_PRODUCT_SIS_730, 0x00, 5, "730", SIS_TYPE_100OLD},
	{PCI_PRODUCT_SIS_733, 0x00, 5, "733", SIS_TYPE_100NEW},
	{PCI_PRODUCT_SIS_735, 0x00, 5, "735", SIS_TYPE_100NEW},
	{PCI_PRODUCT_SIS_740, 0x00, 5, "740", SIS_TYPE_SOUTH},
	{PCI_PRODUCT_SIS_741, 0x00, 6, "741", SIS_TYPE_SOUTH},
	{PCI_PRODUCT_SIS_745, 0x00, 5, "745", SIS_TYPE_100NEW},
	{PCI_PRODUCT_SIS_746, 0x00, 6, "746", SIS_TYPE_SOUTH},
	{PCI_PRODUCT_SIS_748, 0x00, 6, "748", SIS_TYPE_SOUTH},
	{PCI_PRODUCT_SIS_750, 0x00, 6, "750", SIS_TYPE_SOUTH},
	{PCI_PRODUCT_SIS_751, 0x00, 6, "751", SIS_TYPE_SOUTH},
	{PCI_PRODUCT_SIS_752, 0x00, 6, "752", SIS_TYPE_SOUTH},
	{PCI_PRODUCT_SIS_755, 0x00, 6, "755", SIS_TYPE_SOUTH},
	{PCI_PRODUCT_SIS_760, 0x00, 6, "760", SIS_TYPE_SOUTH},
	/*
	 * From sos@freebsd.org: the 0x961 ID will never be found in real world
	 * {PCI_PRODUCT_SIS_961, 0x00, 6, "961", SIS_TYPE_133NEW},
	 */
	{PCI_PRODUCT_SIS_962, 0x00, 6, "962", SIS_TYPE_133NEW},
	{PCI_PRODUCT_SIS_963, 0x00, 6, "963", SIS_TYPE_133NEW},
	{PCI_PRODUCT_SIS_964, 0x00, 6, "964", SIS_TYPE_133NEW},
	{PCI_PRODUCT_SIS_965, 0x00, 6, "965", SIS_TYPE_133NEW},
	{PCI_PRODUCT_SIS_966, 0x00, 6, "966", SIS_TYPE_133NEW},
	{PCI_PRODUCT_SIS_968, 0x00, 6, "968", SIS_TYPE_133NEW}
};

static struct sis_hostbr_type *sis_hostbr_type_match;

int
sis_hostbr_match(struct pci_attach_args *pa)
{
	int i;

	if (PCI_VENDOR(pa->pa_id) != PCI_VENDOR_SIS)
		return (0);
	sis_hostbr_type_match = NULL;
	for (i = 0;
	    i < sizeof(sis_hostbr_type) / sizeof(sis_hostbr_type[0]);
	    i++) {
		if (PCI_PRODUCT(pa->pa_id) == sis_hostbr_type[i].id &&
		    PCI_REVISION(pa->pa_class) >= sis_hostbr_type[i].rev)
			sis_hostbr_type_match = &sis_hostbr_type[i];
	}
	return (sis_hostbr_type_match != NULL);
}

int
sis_south_match(struct pci_attach_args *pa)
{
	return(PCI_VENDOR(pa->pa_id) == PCI_VENDOR_SIS &&
	    PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_SIS_85C503 &&
	    PCI_REVISION(pa->pa_class) >= 0x10);
}

void
sis_chip_map(struct pciide_softc *sc, struct pci_attach_args *pa)
{
	struct pciide_channel *cp;
	int channel;
	u_int8_t sis_ctr0 = pciide_pci_read(sc->sc_pc, sc->sc_tag, SIS_CTRL0);
	pcireg_t interface = PCI_INTERFACE(pa->pa_class);
	int rev = sc->sc_rev;
	bus_size_t cmdsize, ctlsize;
	struct pciide_sis *sis;

	/* Allocate memory for private data */
	sc->sc_cookielen = sizeof(*sis);
	sc->sc_cookie = malloc(sc->sc_cookielen, M_DEVBUF, M_NOWAIT | M_ZERO);
	sis = sc->sc_cookie;

	pci_find_device(NULL, sis_hostbr_match);

	if (sis_hostbr_type_match) {
		if (sis_hostbr_type_match->type == SIS_TYPE_SOUTH) {
			pciide_pci_write(sc->sc_pc, sc->sc_tag, SIS_REG_57,
			    pciide_pci_read(sc->sc_pc, sc->sc_tag,
			    SIS_REG_57) & 0x7f);
			if (sc->sc_pp->ide_product == SIS_PRODUCT_5518) {
				sis->sis_type = SIS_TYPE_133NEW;
				sc->sc_wdcdev.UDMA_cap =
				    sis_hostbr_type_match->udma_mode;
			} else {
				if (pci_find_device(NULL, sis_south_match)) {
					sis->sis_type = SIS_TYPE_133OLD;
					sc->sc_wdcdev.UDMA_cap =
					    sis_hostbr_type_match->udma_mode;
				} else {
					sis->sis_type = SIS_TYPE_100NEW;
					sc->sc_wdcdev.UDMA_cap =
					    sis_hostbr_type_match->udma_mode;
				}
			}
		} else {
			sis->sis_type = sis_hostbr_type_match->type;
			sc->sc_wdcdev.UDMA_cap =
			    sis_hostbr_type_match->udma_mode;
		}
		printf(": %s", sis_hostbr_type_match->name);
	} else {
		printf(": 5597/5598");
		if (rev >= 0xd0) {
			sc->sc_wdcdev.UDMA_cap = 2;
			sis->sis_type = SIS_TYPE_66;
		} else {
			sc->sc_wdcdev.UDMA_cap = 0;
			sis->sis_type = SIS_TYPE_NOUDMA;
		}
	}

	printf(": DMA");
	pciide_mapreg_dma(sc, pa);

	sc->sc_wdcdev.cap = WDC_CAPABILITY_DATA16 | WDC_CAPABILITY_DATA32 |
	    WDC_CAPABILITY_MODE;
	if (sc->sc_dma_ok) {
		sc->sc_wdcdev.cap |= WDC_CAPABILITY_DMA | WDC_CAPABILITY_IRQACK;
		sc->sc_wdcdev.irqack = pciide_irqack;
		if (sis->sis_type >= SIS_TYPE_66)
			sc->sc_wdcdev.cap |= WDC_CAPABILITY_UDMA;
	}

	sc->sc_wdcdev.PIO_cap = 4;
	sc->sc_wdcdev.DMA_cap = 2;

	sc->sc_wdcdev.channels = sc->wdc_chanarray;
	sc->sc_wdcdev.nchannels = PCIIDE_NUM_CHANNELS;
	switch (sis->sis_type) {
	case SIS_TYPE_NOUDMA:
	case SIS_TYPE_66:
	case SIS_TYPE_100OLD:
		sc->sc_wdcdev.set_modes = sis_setup_channel;
		pciide_pci_write(sc->sc_pc, sc->sc_tag, SIS_MISC,
		    pciide_pci_read(sc->sc_pc, sc->sc_tag, SIS_MISC) |
		    SIS_MISC_TIM_SEL | SIS_MISC_FIFO_SIZE | SIS_MISC_GTC);
		break;
	case SIS_TYPE_100NEW:
	case SIS_TYPE_133OLD:
		sc->sc_wdcdev.set_modes = sis_setup_channel;
		pciide_pci_write(sc->sc_pc, sc->sc_tag, SIS_REG_49,
		    pciide_pci_read(sc->sc_pc, sc->sc_tag, SIS_REG_49) | 0x01);
		break;
	case SIS_TYPE_133NEW:
		sc->sc_wdcdev.set_modes = sis96x_setup_channel;
		pciide_pci_write(sc->sc_pc, sc->sc_tag, SIS_REG_50,
		    pciide_pci_read(sc->sc_pc, sc->sc_tag, SIS_REG_50) & 0xf7);
		pciide_pci_write(sc->sc_pc, sc->sc_tag, SIS_REG_52,
		    pciide_pci_read(sc->sc_pc, sc->sc_tag, SIS_REG_52) & 0xf7);
		break;
	}

	pciide_print_channels(sc->sc_wdcdev.nchannels, interface);

	for (channel = 0; channel < sc->sc_wdcdev.nchannels; channel++) {
		cp = &sc->pciide_channels[channel];
		if (pciide_chansetup(sc, channel, interface) == 0)
			continue;
		if ((channel == 0 && (sis_ctr0 & SIS_CTRL0_CHAN0_EN) == 0) ||
		    (channel == 1 && (sis_ctr0 & SIS_CTRL0_CHAN1_EN) == 0)) {
			printf("%s: %s ignored (disabled)\n",
			    sc->sc_wdcdev.sc_dev.dv_xname, cp->name);
			cp->hw_ok = 0;
			continue;
		}
		pciide_map_compat_intr(pa, cp, channel, interface);
		if (cp->hw_ok == 0)
			continue;
		pciide_mapchan(pa, cp, interface, &cmdsize, &ctlsize,
		    pciide_pci_intr);
		if (cp->hw_ok == 0) {
			pciide_unmap_compat_intr(pa, cp, channel, interface);
			continue;
		}
		if (pciide_chan_candisable(cp)) {
			if (channel == 0)
				sis_ctr0 &= ~SIS_CTRL0_CHAN0_EN;
			else
				sis_ctr0 &= ~SIS_CTRL0_CHAN1_EN;
			pciide_pci_write(sc->sc_pc, sc->sc_tag, SIS_CTRL0,
			    sis_ctr0);
		}
		if (cp->hw_ok == 0) {
			pciide_unmap_compat_intr(pa, cp, channel, interface);
			continue;
		}
		sc->sc_wdcdev.set_modes(&cp->wdc_channel);
	}
}

void
sis96x_setup_channel(struct channel_softc *chp)
{
	struct ata_drive_datas *drvp;
	int drive;
	u_int32_t sis_tim;
	u_int32_t idedma_ctl;
	int regtim;
	struct pciide_channel *cp = (struct pciide_channel *)chp;
	struct pciide_softc *sc = (struct pciide_softc *)cp->wdc_channel.wdc;

	sis_tim = 0;
	idedma_ctl = 0;
	/* setup DMA if needed */
	pciide_channel_dma_setup(cp);

	for (drive = 0; drive < 2; drive++) {
		regtim = SIS_TIM133(
		    pciide_pci_read(sc->sc_pc, sc->sc_tag, SIS_REG_57),
		    chp->channel, drive);
		drvp = &chp->ch_drive[drive];
		/* If no drive, skip */
		if ((drvp->drive_flags & DRIVE) == 0)
			continue;
		/* add timing values, setup DMA if needed */
		if (drvp->drive_flags & DRIVE_UDMA) {
			/* use Ultra/DMA */
			drvp->drive_flags &= ~DRIVE_DMA;
			if (pciide_pci_read(sc->sc_pc, sc->sc_tag,
			    SIS96x_REG_CBL(chp->channel)) & SIS96x_REG_CBL_33) {
				if (drvp->UDMA_mode > 2)
					drvp->UDMA_mode = 2;
			}
			sis_tim |= sis_udma133new_tim[drvp->UDMA_mode];
			sis_tim |= sis_pio133new_tim[drvp->PIO_mode];
			idedma_ctl |= IDEDMA_CTL_DRV_DMA(drive);
		} else if (drvp->drive_flags & DRIVE_DMA) {
			/*
			 * use Multiword DMA
			 * Timings will be used for both PIO and DMA,
			 * so adjust DMA mode if needed
			 */
			if (drvp->PIO_mode > (drvp->DMA_mode + 2))
				drvp->PIO_mode = drvp->DMA_mode + 2;
			if (drvp->DMA_mode + 2 > (drvp->PIO_mode))
				drvp->DMA_mode = (drvp->PIO_mode > 2) ?
				    drvp->PIO_mode - 2 : 0;
			sis_tim |= sis_dma133new_tim[drvp->DMA_mode];
			idedma_ctl |= IDEDMA_CTL_DRV_DMA(drive);
		} else {
			sis_tim |= sis_pio133new_tim[drvp->PIO_mode];
		}
		WDCDEBUG_PRINT(("sis96x_setup_channel: new timings reg for "
		    "channel %d drive %d: 0x%x (reg 0x%x)\n",
		    chp->channel, drive, sis_tim, regtim), DEBUG_PROBE);
		pci_conf_write(sc->sc_pc, sc->sc_tag, regtim, sis_tim);
	}
	if (idedma_ctl != 0) {
		/* Add software bits in status register */
		bus_space_write_1(sc->sc_dma_iot, sc->sc_dma_ioh,
		    IDEDMA_CTL(chp->channel), idedma_ctl);
	}
	pciide_print_modes(cp);
}

void
sis_setup_channel(struct channel_softc *chp)
{
	struct ata_drive_datas *drvp;
	int drive;
	u_int32_t sis_tim;
	u_int32_t idedma_ctl;
	struct pciide_channel *cp = (struct pciide_channel *)chp;
	struct pciide_softc *sc = (struct pciide_softc *)cp->wdc_channel.wdc;
	struct pciide_sis *sis = sc->sc_cookie;

	WDCDEBUG_PRINT(("sis_setup_channel: old timings reg for "
	    "channel %d 0x%x\n", chp->channel,
	    pci_conf_read(sc->sc_pc, sc->sc_tag, SIS_TIM(chp->channel))),
	    DEBUG_PROBE);
	sis_tim = 0;
	idedma_ctl = 0;
	/* setup DMA if needed */
	pciide_channel_dma_setup(cp);

	for (drive = 0; drive < 2; drive++) {
		drvp = &chp->ch_drive[drive];
		/* If no drive, skip */
		if ((drvp->drive_flags & DRIVE) == 0)
			continue;
		/* add timing values, setup DMA if needed */
		if ((drvp->drive_flags & DRIVE_DMA) == 0 &&
		    (drvp->drive_flags & DRIVE_UDMA) == 0)
			goto pio;

		if (drvp->drive_flags & DRIVE_UDMA) {
			/* use Ultra/DMA */
			drvp->drive_flags &= ~DRIVE_DMA;
			if (pciide_pci_read(sc->sc_pc, sc->sc_tag,
			    SIS_REG_CBL) & SIS_REG_CBL_33(chp->channel)) {
				if (drvp->UDMA_mode > 2)
					drvp->UDMA_mode = 2;
			}
			switch (sis->sis_type) {
			case SIS_TYPE_66:
			case SIS_TYPE_100OLD:
				sis_tim |= sis_udma66_tim[drvp->UDMA_mode] <<
				    SIS_TIM66_UDMA_TIME_OFF(drive);
				break;
			case SIS_TYPE_100NEW:
				sis_tim |=
				    sis_udma100new_tim[drvp->UDMA_mode] <<
				    SIS_TIM100_UDMA_TIME_OFF(drive);
				break;
			case SIS_TYPE_133OLD:
				sis_tim |=
				    sis_udma133old_tim[drvp->UDMA_mode] <<
				    SIS_TIM100_UDMA_TIME_OFF(drive);
				break;
			default:
				printf("unknown SiS IDE type %d\n",
				    sis->sis_type);
			}
		} else {
			/*
			 * use Multiword DMA
			 * Timings will be used for both PIO and DMA,
			 * so adjust DMA mode if needed
			 */
			if (drvp->PIO_mode > (drvp->DMA_mode + 2))
				drvp->PIO_mode = drvp->DMA_mode + 2;
			if (drvp->DMA_mode + 2 > (drvp->PIO_mode))
				drvp->DMA_mode = (drvp->PIO_mode > 2) ?
				    drvp->PIO_mode - 2 : 0;
			if (drvp->DMA_mode == 0)
				drvp->PIO_mode = 0;
		}
		idedma_ctl |= IDEDMA_CTL_DRV_DMA(drive);
pio:		switch (sis->sis_type) {
		case SIS_TYPE_NOUDMA:
		case SIS_TYPE_66:
		case SIS_TYPE_100OLD:
			sis_tim |= sis_pio_act[drvp->PIO_mode] <<
			    SIS_TIM66_ACT_OFF(drive);
			sis_tim |= sis_pio_rec[drvp->PIO_mode] <<
			    SIS_TIM66_REC_OFF(drive);
			break;
		case SIS_TYPE_100NEW:
		case SIS_TYPE_133OLD:
			sis_tim |= sis_pio_act[drvp->PIO_mode] <<
			    SIS_TIM100_ACT_OFF(drive);
			sis_tim |= sis_pio_rec[drvp->PIO_mode] <<
			    SIS_TIM100_REC_OFF(drive);
			break;
		default:
			printf("unknown SiS IDE type %d\n",
			    sis->sis_type);
		}
	}
	WDCDEBUG_PRINT(("sis_setup_channel: new timings reg for "
	    "channel %d 0x%x\n", chp->channel, sis_tim), DEBUG_PROBE);
	pci_conf_write(sc->sc_pc, sc->sc_tag, SIS_TIM(chp->channel), sis_tim);
	if (idedma_ctl != 0) {
		/* Add software bits in status register */
		bus_space_write_1(sc->sc_dma_iot, sc->sc_dma_ioh,
		    IDEDMA_CTL(chp->channel), idedma_ctl);
	}
	pciide_print_modes(cp);
}

void
natsemi_chip_map(struct pciide_softc *sc, struct pci_attach_args *pa)
{
	struct pciide_channel *cp;
	int channel;
	pcireg_t interface, ctl;
	bus_size_t cmdsize, ctlsize;

	printf(": DMA");
	pciide_mapreg_dma(sc, pa);
	sc->sc_wdcdev.cap = WDC_CAPABILITY_DATA16;

	if (sc->sc_dma_ok) {
		sc->sc_wdcdev.cap |= WDC_CAPABILITY_DMA | WDC_CAPABILITY_IRQACK;
		sc->sc_wdcdev.irqack = natsemi_irqack;
	}

	pciide_pci_write(sc->sc_pc, sc->sc_tag, NATSEMI_CCBT, 0xb7);

	/*
	 * Mask off interrupts from both channels, appropriate channel(s)
	 * will be unmasked later.
	 */
	pciide_pci_write(sc->sc_pc, sc->sc_tag, NATSEMI_CTRL2,
	    pciide_pci_read(sc->sc_pc, sc->sc_tag, NATSEMI_CTRL2) |
	    NATSEMI_CHMASK(0) | NATSEMI_CHMASK(1));

	sc->sc_wdcdev.PIO_cap = 4;
	sc->sc_wdcdev.DMA_cap = 2;
	sc->sc_wdcdev.set_modes = natsemi_setup_channel;
	sc->sc_wdcdev.channels = sc->wdc_chanarray;
	sc->sc_wdcdev.nchannels = PCIIDE_NUM_CHANNELS;

	interface = PCI_INTERFACE(pci_conf_read(sc->sc_pc, sc->sc_tag,
	    PCI_CLASS_REG));
	interface &= ~PCIIDE_CHANSTATUS_EN;	/* Reserved on PC87415 */
	pciide_print_channels(sc->sc_wdcdev.nchannels, interface);

	/* If we're in PCIIDE mode, unmask INTA, otherwise mask it. */
	ctl = pciide_pci_read(sc->sc_pc, sc->sc_tag, NATSEMI_CTRL1);
	if (interface & (PCIIDE_INTERFACE_PCI(0) | PCIIDE_INTERFACE_PCI(1)))
		ctl &= ~NATSEMI_CTRL1_INTAMASK;
	else
		ctl |= NATSEMI_CTRL1_INTAMASK;
	pciide_pci_write(sc->sc_pc, sc->sc_tag, NATSEMI_CTRL1, ctl);

	for (channel = 0; channel < sc->sc_wdcdev.nchannels; channel++) {
		cp = &sc->pciide_channels[channel];
		if (pciide_chansetup(sc, channel, interface) == 0)
			continue;

		pciide_map_compat_intr(pa, cp, channel, interface);
		if (cp->hw_ok == 0)
			continue;

		pciide_mapchan(pa, cp, interface, &cmdsize, &ctlsize,
		    natsemi_pci_intr);
		if (cp->hw_ok == 0) {
			pciide_unmap_compat_intr(pa, cp, channel, interface);
			continue;
		}
		natsemi_setup_channel(&cp->wdc_channel);
	}
}

void
natsemi_setup_channel(struct channel_softc *chp)
{
	struct ata_drive_datas *drvp;
	int drive, ndrives = 0;
	u_int32_t idedma_ctl = 0;
	struct pciide_channel *cp = (struct pciide_channel *)chp;
	struct pciide_softc *sc = (struct pciide_softc *)cp->wdc_channel.wdc;
	u_int8_t tim;

	/* setup DMA if needed */
	pciide_channel_dma_setup(cp);

	for (drive = 0; drive < 2; drive++) {
		drvp = &chp->ch_drive[drive];
		/* If no drive, skip */
		if ((drvp->drive_flags & DRIVE) == 0)
			continue;

		ndrives++;
		/* add timing values, setup DMA if needed */
		if ((drvp->drive_flags & DRIVE_DMA) == 0) {
			tim = natsemi_pio_pulse[drvp->PIO_mode] |
			    (natsemi_pio_recover[drvp->PIO_mode] << 4);
		} else {
			/*
			 * use Multiword DMA
			 * Timings will be used for both PIO and DMA,
			 * so adjust DMA mode if needed
			 */
			if (drvp->PIO_mode >= 3 &&
			    (drvp->DMA_mode + 2) > drvp->PIO_mode) {
				drvp->DMA_mode = drvp->PIO_mode - 2;
			}
			idedma_ctl |= IDEDMA_CTL_DRV_DMA(drive);
			tim = natsemi_dma_pulse[drvp->DMA_mode] |
			    (natsemi_dma_recover[drvp->DMA_mode] << 4);
		}

		pciide_pci_write(sc->sc_pc, sc->sc_tag,
		    NATSEMI_RTREG(chp->channel, drive), tim);
		pciide_pci_write(sc->sc_pc, sc->sc_tag,
		    NATSEMI_WTREG(chp->channel, drive), tim);
	}
	if (idedma_ctl != 0) {
		/* Add software bits in status register */
		bus_space_write_1(sc->sc_dma_iot, sc->sc_dma_ioh,
		    IDEDMA_CTL(chp->channel), idedma_ctl);
	}
	if (ndrives > 0) {
		/* Unmask the channel if at least one drive is found */
		pciide_pci_write(sc->sc_pc, sc->sc_tag, NATSEMI_CTRL2,
		    pciide_pci_read(sc->sc_pc, sc->sc_tag, NATSEMI_CTRL2) &
		    ~(NATSEMI_CHMASK(chp->channel)));
	}

	pciide_print_modes(cp);

	/* Go ahead and ack interrupts generated during probe. */
	bus_space_write_1(sc->sc_dma_iot, sc->sc_dma_ioh,
	    IDEDMA_CTL(chp->channel),
	    bus_space_read_1(sc->sc_dma_iot, sc->sc_dma_ioh,
		IDEDMA_CTL(chp->channel)));
}

void
natsemi_irqack(struct channel_softc *chp)
{
	struct pciide_channel *cp = (struct pciide_channel *)chp;
	struct pciide_softc *sc = (struct pciide_softc *)cp->wdc_channel.wdc;
	u_int8_t clr;

	/* The "clear" bits are in the wrong register *sigh* */
	clr = bus_space_read_1(sc->sc_dma_iot, sc->sc_dma_ioh,
	    IDEDMA_CMD(chp->channel));
	clr |= bus_space_read_1(sc->sc_dma_iot, sc->sc_dma_ioh,
	    IDEDMA_CTL(chp->channel)) &
	    (IDEDMA_CTL_ERR | IDEDMA_CTL_INTR);
	bus_space_write_1(sc->sc_dma_iot, sc->sc_dma_ioh,
	    IDEDMA_CMD(chp->channel), clr);
}

int
natsemi_pci_intr(void *arg)
{
	struct pciide_softc *sc = arg;
	struct pciide_channel *cp;
	struct channel_softc *wdc_cp;
	int i, rv, crv;
	u_int8_t msk;

	rv = 0;
	msk = pciide_pci_read(sc->sc_pc, sc->sc_tag, NATSEMI_CTRL2);
	for (i = 0; i < sc->sc_wdcdev.nchannels; i++) {
		cp = &sc->pciide_channels[i];
		wdc_cp = &cp->wdc_channel;

		/* If a compat channel skip. */
		if (cp->compat)
			continue;

		/* If this channel is masked, skip it. */
		if (msk & NATSEMI_CHMASK(i))
			continue;

		if (pciide_intr_flag(cp) == 0)
			continue;

		crv = wdcintr(wdc_cp);
		if (crv == 0)
			;		/* leave rv alone */
		else if (crv == 1)
			rv = 1;		/* claim the intr */
		else if (rv == 0)	/* crv should be -1 in this case */
			rv = crv;	/* if we've done no better, take it */
	}
	return (rv);
}

void
ns_scx200_chip_map(struct pciide_softc *sc, struct pci_attach_args *pa)
{
	struct pciide_channel *cp;
	int channel;
	pcireg_t interface = PCI_INTERFACE(pa->pa_class);
	bus_size_t cmdsize, ctlsize;

	printf(": DMA");
	pciide_mapreg_dma(sc, pa);

	sc->sc_wdcdev.cap = WDC_CAPABILITY_DATA16 | WDC_CAPABILITY_DATA32 |
	    WDC_CAPABILITY_MODE;
	if (sc->sc_dma_ok) {
		sc->sc_wdcdev.cap |= WDC_CAPABILITY_DMA | WDC_CAPABILITY_UDMA;
		sc->sc_wdcdev.cap |= WDC_CAPABILITY_IRQACK;
		sc->sc_wdcdev.irqack = pciide_irqack;
	}
	sc->sc_wdcdev.PIO_cap = 4;
	sc->sc_wdcdev.DMA_cap = 2;
	sc->sc_wdcdev.UDMA_cap = 2;

	sc->sc_wdcdev.set_modes = ns_scx200_setup_channel;
	sc->sc_wdcdev.channels = sc->wdc_chanarray;
	sc->sc_wdcdev.nchannels = PCIIDE_NUM_CHANNELS;

	/*
	 * Soekris net4801 errata 0003:
	 *
	 * The SC1100 built in busmaster IDE controller is pretty standard,
	 * but have two bugs: data transfers need to be dword aligned and
	 * it cannot do an exact 64Kbyte data transfer.
	 *
	 * Assume that reducing maximum segment size by one page
	 * will be enough, and restrict boundary too for extra certainty.
	 */
	if (sc->sc_pp->ide_product == PCI_PRODUCT_NS_SCX200_IDE) {
		sc->sc_dma_maxsegsz = IDEDMA_BYTE_COUNT_MAX - PAGE_SIZE;
		sc->sc_dma_boundary = IDEDMA_BYTE_COUNT_MAX - PAGE_SIZE;
	}

	/*
	 * This chip seems to be unable to do one-sector transfers
	 * using DMA.
	 */
	sc->sc_wdcdev.quirks = WDC_QUIRK_NOSHORTDMA;

	pciide_print_channels(sc->sc_wdcdev.nchannels, interface);

	for (channel = 0; channel < sc->sc_wdcdev.nchannels; channel++) {
		cp = &sc->pciide_channels[channel];
		if (pciide_chansetup(sc, channel, interface) == 0)
			continue;
		pciide_map_compat_intr(pa, cp, channel, interface);
		if (cp->hw_ok == 0)
			continue;
		pciide_mapchan(pa, cp, interface, &cmdsize, &ctlsize,
		    pciide_pci_intr);
		if (cp->hw_ok == 0) {
			pciide_unmap_compat_intr(pa, cp, channel, interface);
			continue;
		}
		sc->sc_wdcdev.set_modes(&cp->wdc_channel);
	}
}

void
ns_scx200_setup_channel(struct channel_softc *chp)
{
	struct ata_drive_datas *drvp;
	int drive, mode;
	u_int32_t idedma_ctl;
	struct pciide_channel *cp = (struct pciide_channel*)chp;
	struct pciide_softc *sc = (struct pciide_softc *)cp->wdc_channel.wdc;
	int channel = chp->channel;
	int pioformat;
	pcireg_t piotim, dmatim;

	/* Setup DMA if needed */
	pciide_channel_dma_setup(cp);

	idedma_ctl = 0;

	pioformat = (pci_conf_read(sc->sc_pc, sc->sc_tag,
	    SCx200_TIM_DMA(0, 0)) >> SCx200_PIOFORMAT_SHIFT) & 0x01;
	WDCDEBUG_PRINT(("%s: pio format %d\n", __func__, pioformat),
	    DEBUG_PROBE);

	/* Per channel settings */
	for (drive = 0; drive < 2; drive++) {
		drvp = &chp->ch_drive[drive];

		/* If no drive, skip */
		if ((drvp->drive_flags & DRIVE) == 0)
			continue;

		piotim = pci_conf_read(sc->sc_pc, sc->sc_tag,
		    SCx200_TIM_PIO(channel, drive));
		dmatim = pci_conf_read(sc->sc_pc, sc->sc_tag,
		    SCx200_TIM_DMA(channel, drive));
		WDCDEBUG_PRINT(("%s:%d:%d: piotim=0x%x, dmatim=0x%x\n",
		    sc->sc_wdcdev.sc_dev.dv_xname, channel, drive,
		    piotim, dmatim), DEBUG_PROBE);

		if ((chp->wdc->cap & WDC_CAPABILITY_UDMA) != 0 &&
		    (drvp->drive_flags & DRIVE_UDMA) != 0) {
			/* Setup UltraDMA mode */
			drvp->drive_flags &= ~DRIVE_DMA;
			idedma_ctl |= IDEDMA_CTL_DRV_DMA(drive);
			dmatim = scx200_udma33[drvp->UDMA_mode];
			mode = drvp->PIO_mode;
		} else if ((chp->wdc->cap & WDC_CAPABILITY_DMA) != 0 &&
		    (drvp->drive_flags & DRIVE_DMA) != 0) {
			/* Setup multiword DMA mode */
			drvp->drive_flags &= ~DRIVE_UDMA;
			idedma_ctl |= IDEDMA_CTL_DRV_DMA(drive);
			dmatim = scx200_dma33[drvp->DMA_mode];

			/* mode = min(pio, dma + 2) */
			if (drvp->PIO_mode <= (drvp->DMA_mode + 2))
				mode = drvp->PIO_mode;
			else
				mode = drvp->DMA_mode + 2;
		} else {
			mode = drvp->PIO_mode;
		}

		/* Setup PIO mode */
		drvp->PIO_mode = mode;
		if (mode < 2)
			drvp->DMA_mode = 0;
		else
			drvp->DMA_mode = mode - 2;

		piotim = scx200_pio33[pioformat][drvp->PIO_mode];

		WDCDEBUG_PRINT(("%s:%d:%d: new piotim=0x%x, dmatim=0x%x\n",
		    sc->sc_wdcdev.sc_dev.dv_xname, channel, drive,
		    piotim, dmatim), DEBUG_PROBE);

		pci_conf_write(sc->sc_pc, sc->sc_tag,
		    SCx200_TIM_PIO(channel, drive), piotim);
		pci_conf_write(sc->sc_pc, sc->sc_tag,
		    SCx200_TIM_DMA(channel, drive), dmatim);
	}

	if (idedma_ctl != 0) {
		/* Add software bits in status register */
		bus_space_write_1(sc->sc_dma_iot, sc->sc_dma_ioh,
		    IDEDMA_CTL(channel), idedma_ctl);
	}

	pciide_print_modes(cp);
}

void
acer_chip_map(struct pciide_softc *sc, struct pci_attach_args *pa)
{
	struct pciide_channel *cp;
	int channel;
	pcireg_t cr, interface;
	bus_size_t cmdsize, ctlsize;
	int rev = sc->sc_rev;

	printf(": DMA");
	pciide_mapreg_dma(sc, pa);
	sc->sc_wdcdev.cap = WDC_CAPABILITY_DATA16 | WDC_CAPABILITY_DATA32 |
	    WDC_CAPABILITY_MODE;

	if (sc->sc_dma_ok) {
		sc->sc_wdcdev.cap |= WDC_CAPABILITY_DMA;
		if (rev >= 0x20) {
			sc->sc_wdcdev.cap |= WDC_CAPABILITY_UDMA;
			if (rev >= 0xC4)
				sc->sc_wdcdev.UDMA_cap = 5;
			else if (rev >= 0xC2)
				sc->sc_wdcdev.UDMA_cap = 4;
			else
				sc->sc_wdcdev.UDMA_cap = 2;
		}
		sc->sc_wdcdev.cap |= WDC_CAPABILITY_IRQACK;
		sc->sc_wdcdev.irqack = pciide_irqack;
		if (rev <= 0xC4)
			sc->sc_wdcdev.dma_init = acer_dma_init;
	}

	sc->sc_wdcdev.PIO_cap = 4;
	sc->sc_wdcdev.DMA_cap = 2;
	sc->sc_wdcdev.set_modes = acer_setup_channel;
	sc->sc_wdcdev.channels = sc->wdc_chanarray;
	sc->sc_wdcdev.nchannels = PCIIDE_NUM_CHANNELS;

	pciide_pci_write(sc->sc_pc, sc->sc_tag, ACER_CDRC,
	    (pciide_pci_read(sc->sc_pc, sc->sc_tag, ACER_CDRC) |
		ACER_CDRC_DMA_EN) & ~ACER_CDRC_FIFO_DISABLE);

	/* Enable "microsoft register bits" R/W. */
	pciide_pci_write(sc->sc_pc, sc->sc_tag, ACER_CCAR3,
	    pciide_pci_read(sc->sc_pc, sc->sc_tag, ACER_CCAR3) | ACER_CCAR3_PI);
	pciide_pci_write(sc->sc_pc, sc->sc_tag, ACER_CCAR1,
	    pciide_pci_read(sc->sc_pc, sc->sc_tag, ACER_CCAR1) &
	    ~(ACER_CHANSTATUS_RO|PCIIDE_CHAN_RO(0)|PCIIDE_CHAN_RO(1)));
	pciide_pci_write(sc->sc_pc, sc->sc_tag, ACER_CCAR2,
	    pciide_pci_read(sc->sc_pc, sc->sc_tag, ACER_CCAR2) &
	    ~ACER_CHANSTATUSREGS_RO);
	cr = pci_conf_read(sc->sc_pc, sc->sc_tag, PCI_CLASS_REG);
	cr |= (PCIIDE_CHANSTATUS_EN << PCI_INTERFACE_SHIFT);
	pci_conf_write(sc->sc_pc, sc->sc_tag, PCI_CLASS_REG, cr);
	/* Don't use cr, re-read the real register content instead */
	interface = PCI_INTERFACE(pci_conf_read(sc->sc_pc, sc->sc_tag,
	    PCI_CLASS_REG));

	pciide_print_channels(sc->sc_wdcdev.nchannels, interface);

	/* From linux: enable "Cable Detection" */
	if (rev >= 0xC2)
		pciide_pci_write(sc->sc_pc, sc->sc_tag, ACER_0x4B,
		    pciide_pci_read(sc->sc_pc, sc->sc_tag, ACER_0x4B)
		    | ACER_0x4B_CDETECT);

	for (channel = 0; channel < sc->sc_wdcdev.nchannels; channel++) {
		cp = &sc->pciide_channels[channel];
		if (pciide_chansetup(sc, channel, interface) == 0)
			continue;
		if ((interface & PCIIDE_CHAN_EN(channel)) == 0) {
			printf("%s: %s ignored (disabled)\n",
			    sc->sc_wdcdev.sc_dev.dv_xname, cp->name);
			cp->hw_ok = 0;
			continue;
		}
		pciide_map_compat_intr(pa, cp, channel, interface);
		if (cp->hw_ok == 0)
			continue;
		pciide_mapchan(pa, cp, interface, &cmdsize, &ctlsize,
		    (rev >= 0xC2) ? pciide_pci_intr : acer_pci_intr);
		if (cp->hw_ok == 0) {
			pciide_unmap_compat_intr(pa, cp, channel, interface);
			continue;
		}
		if (pciide_chan_candisable(cp)) {
			cr &= ~(PCIIDE_CHAN_EN(channel) << PCI_INTERFACE_SHIFT);
			pci_conf_write(sc->sc_pc, sc->sc_tag,
			    PCI_CLASS_REG, cr);
		}
		if (cp->hw_ok == 0) {
			pciide_unmap_compat_intr(pa, cp, channel, interface);
			continue;
		}
		acer_setup_channel(&cp->wdc_channel);
	}
}

void
acer_setup_channel(struct channel_softc *chp)
{
	struct ata_drive_datas *drvp;
	int drive;
	u_int32_t acer_fifo_udma;
	u_int32_t idedma_ctl;
	struct pciide_channel *cp = (struct pciide_channel *)chp;
	struct pciide_softc *sc = (struct pciide_softc *)cp->wdc_channel.wdc;

	idedma_ctl = 0;
	acer_fifo_udma = pci_conf_read(sc->sc_pc, sc->sc_tag, ACER_FTH_UDMA);
	WDCDEBUG_PRINT(("acer_setup_channel: old fifo/udma reg 0x%x\n",
	    acer_fifo_udma), DEBUG_PROBE);
	/* setup DMA if needed */
	pciide_channel_dma_setup(cp);

	if ((chp->ch_drive[0].drive_flags | chp->ch_drive[1].drive_flags) &
	    DRIVE_UDMA)	{	/* check 80 pins cable */
		if (pciide_pci_read(sc->sc_pc, sc->sc_tag, ACER_0x4A) &
		    ACER_0x4A_80PIN(chp->channel)) {
			WDCDEBUG_PRINT(("%s:%d: 80-wire cable not detected\n",
			    sc->sc_wdcdev.sc_dev.dv_xname, chp->channel),
			    DEBUG_PROBE);
			if (chp->ch_drive[0].UDMA_mode > 2)
				chp->ch_drive[0].UDMA_mode = 2;
			if (chp->ch_drive[1].UDMA_mode > 2)
				chp->ch_drive[1].UDMA_mode = 2;
		}
	}

	for (drive = 0; drive < 2; drive++) {
		drvp = &chp->ch_drive[drive];
		/* If no drive, skip */
		if ((drvp->drive_flags & DRIVE) == 0)
			continue;
		WDCDEBUG_PRINT(("acer_setup_channel: old timings reg for "
		    "channel %d drive %d 0x%x\n", chp->channel, drive,
		    pciide_pci_read(sc->sc_pc, sc->sc_tag,
		    ACER_IDETIM(chp->channel, drive))), DEBUG_PROBE);
		/* clear FIFO/DMA mode */
		acer_fifo_udma &= ~(ACER_FTH_OPL(chp->channel, drive, 0x3) |
		    ACER_UDMA_EN(chp->channel, drive) |
		    ACER_UDMA_TIM(chp->channel, drive, 0x7));

		/* add timing values, setup DMA if needed */
		if ((drvp->drive_flags & DRIVE_DMA) == 0 &&
		    (drvp->drive_flags & DRIVE_UDMA) == 0) {
			acer_fifo_udma |=
			    ACER_FTH_OPL(chp->channel, drive, 0x1);
			goto pio;
		}

		acer_fifo_udma |= ACER_FTH_OPL(chp->channel, drive, 0x2);
		if (drvp->drive_flags & DRIVE_UDMA) {
			/* use Ultra/DMA */
			drvp->drive_flags &= ~DRIVE_DMA;
			acer_fifo_udma |= ACER_UDMA_EN(chp->channel, drive);
			acer_fifo_udma |=
			    ACER_UDMA_TIM(chp->channel, drive,
				acer_udma[drvp->UDMA_mode]);
			/* XXX disable if one drive < UDMA3 ? */
			if (drvp->UDMA_mode >= 3) {
				pciide_pci_write(sc->sc_pc, sc->sc_tag,
				    ACER_0x4B,
				    pciide_pci_read(sc->sc_pc, sc->sc_tag,
				    ACER_0x4B) | ACER_0x4B_UDMA66);
			}
		} else {
			/*
			 * use Multiword DMA
			 * Timings will be used for both PIO and DMA,
			 * so adjust DMA mode if needed
			 */
			if (drvp->PIO_mode > (drvp->DMA_mode + 2))
				drvp->PIO_mode = drvp->DMA_mode + 2;
			if (drvp->DMA_mode + 2 > (drvp->PIO_mode))
				drvp->DMA_mode = (drvp->PIO_mode > 2) ?
				    drvp->PIO_mode - 2 : 0;
			if (drvp->DMA_mode == 0)
				drvp->PIO_mode = 0;
		}
		idedma_ctl |= IDEDMA_CTL_DRV_DMA(drive);
pio:		pciide_pci_write(sc->sc_pc, sc->sc_tag,
		    ACER_IDETIM(chp->channel, drive),
		    acer_pio[drvp->PIO_mode]);
	}
	WDCDEBUG_PRINT(("acer_setup_channel: new fifo/udma reg 0x%x\n",
	    acer_fifo_udma), DEBUG_PROBE);
	pci_conf_write(sc->sc_pc, sc->sc_tag, ACER_FTH_UDMA, acer_fifo_udma);
	if (idedma_ctl != 0) {
		/* Add software bits in status register */
		bus_space_write_1(sc->sc_dma_iot, sc->sc_dma_ioh,
		    IDEDMA_CTL(chp->channel), idedma_ctl);
	}
	pciide_print_modes(cp);
}

int
acer_pci_intr(void *arg)
{
	struct pciide_softc *sc = arg;
	struct pciide_channel *cp;
	struct channel_softc *wdc_cp;
	int i, rv, crv;
	u_int32_t chids;

	rv = 0;
	chids = pciide_pci_read(sc->sc_pc, sc->sc_tag, ACER_CHIDS);
	for (i = 0; i < sc->sc_wdcdev.nchannels; i++) {
		cp = &sc->pciide_channels[i];
		wdc_cp = &cp->wdc_channel;
		/* If a compat channel skip. */
		if (cp->compat)
			continue;
		if (chids & ACER_CHIDS_INT(i)) {
			crv = wdcintr(wdc_cp);
			if (crv == 0)
				printf("%s:%d: bogus intr\n",
				    sc->sc_wdcdev.sc_dev.dv_xname, i);
			else
				rv = 1;
		}
	}
	return (rv);
}

int
acer_dma_init(void *v, int channel, int drive, void *databuf,
    size_t datalen, int flags)
{
	/* Use PIO for LBA48 transfers. */
	if (flags & WDC_DMA_LBA48)
		return (EINVAL);

	return (pciide_dma_init(v, channel, drive, databuf, datalen, flags));
}

void
hpt_chip_map(struct pciide_softc *sc, struct pci_attach_args *pa)
{
	struct pciide_channel *cp;
	int i, compatchan, revision;
	pcireg_t interface;
	bus_size_t cmdsize, ctlsize;

	revision = sc->sc_rev;

	/*
	 * when the chip is in native mode it identifies itself as a
	 * 'misc mass storage'. Fake interface in this case.
	 */
	if (PCI_SUBCLASS(pa->pa_class) == PCI_SUBCLASS_MASS_STORAGE_IDE) {
		interface = PCI_INTERFACE(pa->pa_class);
	} else {
		interface = PCIIDE_INTERFACE_BUS_MASTER_DMA |
		    PCIIDE_INTERFACE_PCI(0);
		if ((sc->sc_pp->ide_product == PCI_PRODUCT_TRIONES_HPT366 &&
		   (revision == HPT370_REV || revision == HPT370A_REV ||
		    revision == HPT372_REV)) ||
		    sc->sc_pp->ide_product == PCI_PRODUCT_TRIONES_HPT372A ||
		    sc->sc_pp->ide_product == PCI_PRODUCT_TRIONES_HPT302 ||
		    sc->sc_pp->ide_product == PCI_PRODUCT_TRIONES_HPT371 ||
		    sc->sc_pp->ide_product == PCI_PRODUCT_TRIONES_HPT374)
			interface |= PCIIDE_INTERFACE_PCI(1);
	}

	printf(": DMA");
	pciide_mapreg_dma(sc, pa);
	printf("\n");
	sc->sc_wdcdev.cap = WDC_CAPABILITY_DATA16 | WDC_CAPABILITY_DATA32 |
	    WDC_CAPABILITY_MODE;
	if (sc->sc_dma_ok) {
		sc->sc_wdcdev.cap |= WDC_CAPABILITY_DMA | WDC_CAPABILITY_UDMA;
		sc->sc_wdcdev.cap |= WDC_CAPABILITY_IRQACK;
		sc->sc_wdcdev.irqack = pciide_irqack;
	}
	sc->sc_wdcdev.PIO_cap = 4;
	sc->sc_wdcdev.DMA_cap = 2;

	sc->sc_wdcdev.set_modes = hpt_setup_channel;
	sc->sc_wdcdev.channels = sc->wdc_chanarray;
	if (sc->sc_pp->ide_product == PCI_PRODUCT_TRIONES_HPT366 &&
	    revision == HPT366_REV) {
		sc->sc_wdcdev.UDMA_cap = 4;
		/*
		 * The 366 has 2 PCI IDE functions, one for primary and one
		 * for secondary. So we need to call pciide_mapregs_compat()
		 * with the real channel
		 */
		if (pa->pa_function == 0) {
			compatchan = 0;
		} else if (pa->pa_function == 1) {
			compatchan = 1;
		} else {
			printf("%s: unexpected PCI function %d\n",
			    sc->sc_wdcdev.sc_dev.dv_xname, pa->pa_function);
			return;
		}
		sc->sc_wdcdev.nchannels = 1;
	} else {
		sc->sc_wdcdev.nchannels = 2;
		if (sc->sc_pp->ide_product == PCI_PRODUCT_TRIONES_HPT372A ||
		    sc->sc_pp->ide_product == PCI_PRODUCT_TRIONES_HPT302 ||
		    sc->sc_pp->ide_product == PCI_PRODUCT_TRIONES_HPT371 ||
		    sc->sc_pp->ide_product == PCI_PRODUCT_TRIONES_HPT374)
			sc->sc_wdcdev.UDMA_cap = 6;
		else if (sc->sc_pp->ide_product == PCI_PRODUCT_TRIONES_HPT366) {
			if (revision == HPT372_REV)
				sc->sc_wdcdev.UDMA_cap = 6;
			else
				sc->sc_wdcdev.UDMA_cap = 5;
		}
	}
	for (i = 0; i < sc->sc_wdcdev.nchannels; i++) {
		cp = &sc->pciide_channels[i];
		compatchan = 0;
		if (sc->sc_wdcdev.nchannels > 1) {
			compatchan = i;
			if((pciide_pci_read(sc->sc_pc, sc->sc_tag,
			    HPT370_CTRL1(i)) & HPT370_CTRL1_EN) == 0) {
				printf("%s: %s ignored (disabled)\n",
				    sc->sc_wdcdev.sc_dev.dv_xname, cp->name);
				cp->hw_ok = 0;
				continue;
			}
		}
		if (pciide_chansetup(sc, i, interface) == 0)
			continue;
		if (interface & PCIIDE_INTERFACE_PCI(i)) {
			cp->hw_ok = pciide_mapregs_native(pa, cp, &cmdsize,
			    &ctlsize, hpt_pci_intr);
		} else {
			cp->hw_ok = pciide_mapregs_compat(pa, cp, compatchan,
			    &cmdsize, &ctlsize);
		}
		if (cp->hw_ok == 0)
			return;
		cp->wdc_channel.data32iot = cp->wdc_channel.cmd_iot;
		cp->wdc_channel.data32ioh = cp->wdc_channel.cmd_ioh;
		wdcattach(&cp->wdc_channel);
		hpt_setup_channel(&cp->wdc_channel);
	}
	if ((sc->sc_pp->ide_product == PCI_PRODUCT_TRIONES_HPT366 &&
	    (revision == HPT370_REV || revision == HPT370A_REV ||
	    revision == HPT372_REV)) ||
	    sc->sc_pp->ide_product == PCI_PRODUCT_TRIONES_HPT372A ||
	    sc->sc_pp->ide_product == PCI_PRODUCT_TRIONES_HPT302 ||
	    sc->sc_pp->ide_product == PCI_PRODUCT_TRIONES_HPT371 ||
	    sc->sc_pp->ide_product == PCI_PRODUCT_TRIONES_HPT374) {
		/*
		 * Turn off fast interrupts
		 */
		pciide_pci_write(sc->sc_pc, sc->sc_tag, HPT370_CTRL2(0),
		    pciide_pci_read(sc->sc_pc, sc->sc_tag, HPT370_CTRL2(0)) &
		    ~(HPT370_CTRL2_FASTIRQ | HPT370_CTRL2_HIRQ));
		pciide_pci_write(sc->sc_pc, sc->sc_tag, HPT370_CTRL2(1),
		pciide_pci_read(sc->sc_pc, sc->sc_tag, HPT370_CTRL2(1)) &
		~(HPT370_CTRL2_FASTIRQ | HPT370_CTRL2_HIRQ));

		/*
		 * HPT370 and higher has a bit to disable interrupts,
		 * make sure to clear it
		 */
		pciide_pci_write(sc->sc_pc, sc->sc_tag, HPT_CSEL,
		    pciide_pci_read(sc->sc_pc, sc->sc_tag, HPT_CSEL) &
		    ~HPT_CSEL_IRQDIS);
	}
	/* set clocks, etc (mandatory on 372/4, optional otherwise) */
	if (sc->sc_pp->ide_product == PCI_PRODUCT_TRIONES_HPT372A ||
	    sc->sc_pp->ide_product == PCI_PRODUCT_TRIONES_HPT302 ||
	    sc->sc_pp->ide_product == PCI_PRODUCT_TRIONES_HPT371 ||
	    sc->sc_pp->ide_product == PCI_PRODUCT_TRIONES_HPT374 ||
	    (sc->sc_pp->ide_product == PCI_PRODUCT_TRIONES_HPT366 &&
	    revision == HPT372_REV))
		pciide_pci_write(sc->sc_pc, sc->sc_tag, HPT_SC2,
		    (pciide_pci_read(sc->sc_pc, sc->sc_tag, HPT_SC2) &
		     HPT_SC2_MAEN) | HPT_SC2_OSC_EN);

	return;
}

void
hpt_setup_channel(struct channel_softc *chp)
{
	struct ata_drive_datas *drvp;
	int drive;
	int cable;
	u_int32_t before, after;
	u_int32_t idedma_ctl;
	struct pciide_channel *cp = (struct pciide_channel *)chp;
	struct pciide_softc *sc = (struct pciide_softc *)cp->wdc_channel.wdc;
	int revision = sc->sc_rev;
	u_int32_t *tim_pio, *tim_dma, *tim_udma;

	cable = pciide_pci_read(sc->sc_pc, sc->sc_tag, HPT_CSEL);

	/* setup DMA if needed */
	pciide_channel_dma_setup(cp);

	idedma_ctl = 0;

	switch (sc->sc_pp->ide_product) {
	case PCI_PRODUCT_TRIONES_HPT366:
		if (revision == HPT370_REV ||
		    revision == HPT370A_REV) {
			tim_pio = hpt370_pio;
			tim_dma = hpt370_dma;
			tim_udma = hpt370_udma;
		} else if (revision == HPT372_REV) {
			tim_pio = hpt372_pio;
			tim_dma = hpt372_dma;
			tim_udma = hpt372_udma;
		} else {
			tim_pio = hpt366_pio;
			tim_dma = hpt366_dma;
			tim_udma = hpt366_udma;
		}
		break;
	case PCI_PRODUCT_TRIONES_HPT372A:
	case PCI_PRODUCT_TRIONES_HPT302:
	case PCI_PRODUCT_TRIONES_HPT371:
		tim_pio = hpt372_pio;
		tim_dma = hpt372_dma;
		tim_udma = hpt372_udma;
		break;
	case PCI_PRODUCT_TRIONES_HPT374:
		tim_pio = hpt374_pio;
		tim_dma = hpt374_dma;
		tim_udma = hpt374_udma;
		break;
	default:
		printf("%s: no known timing values\n",
		    sc->sc_wdcdev.sc_dev.dv_xname);
		goto end;
	}

	/* Per drive settings */
	for (drive = 0; drive < 2; drive++) {
		drvp = &chp->ch_drive[drive];
		/* If no drive, skip */
		if ((drvp->drive_flags & DRIVE) == 0)
			continue;
		before = pci_conf_read(sc->sc_pc, sc->sc_tag,
				       HPT_IDETIM(chp->channel, drive));

		/* add timing values, setup DMA if needed */
		if (drvp->drive_flags & DRIVE_UDMA) {
			/* use Ultra/DMA */
			drvp->drive_flags &= ~DRIVE_DMA;
			if ((cable & HPT_CSEL_CBLID(chp->channel)) != 0 &&
			    drvp->UDMA_mode > 2) {
				WDCDEBUG_PRINT(("%s(%s:%d:%d): 80-wire "
				    "cable not detected\n", drvp->drive_name,
				    sc->sc_wdcdev.sc_dev.dv_xname,
				    chp->channel, drive), DEBUG_PROBE);
				drvp->UDMA_mode = 2;
			}
			after = tim_udma[drvp->UDMA_mode];
			idedma_ctl |= IDEDMA_CTL_DRV_DMA(drive);
		} else if (drvp->drive_flags & DRIVE_DMA) {
			/*
			 * use Multiword DMA.
			 * Timings will be used for both PIO and DMA, so adjust
			 * DMA mode if needed
			 */
			if (drvp->PIO_mode >= 3 &&
			    (drvp->DMA_mode + 2) > drvp->PIO_mode) {
				drvp->DMA_mode = drvp->PIO_mode - 2;
			}
			after = tim_dma[drvp->DMA_mode];
			idedma_ctl |= IDEDMA_CTL_DRV_DMA(drive);
		} else {
			/* PIO only */
			after = tim_pio[drvp->PIO_mode];
		}
		pci_conf_write(sc->sc_pc, sc->sc_tag,
		    HPT_IDETIM(chp->channel, drive), after);
		WDCDEBUG_PRINT(("%s: bus speed register set to 0x%08x "
		    "(BIOS 0x%08x)\n", sc->sc_wdcdev.sc_dev.dv_xname,
		    after, before), DEBUG_PROBE);
	}
end:
	if (idedma_ctl != 0) {
		/* Add software bits in status register */
		bus_space_write_1(sc->sc_dma_iot, sc->sc_dma_ioh,
		    IDEDMA_CTL(chp->channel), idedma_ctl);
	}
	pciide_print_modes(cp);
}

int
hpt_pci_intr(void *arg)
{
	struct pciide_softc *sc = arg;
	struct pciide_channel *cp;
	struct channel_softc *wdc_cp;
	int rv = 0;
	int dmastat, i, crv;

	for (i = 0; i < sc->sc_wdcdev.nchannels; i++) {
		dmastat = bus_space_read_1(sc->sc_dma_iot, sc->sc_dma_ioh,
		    IDEDMA_CTL(i));
		if((dmastat & (IDEDMA_CTL_ACT | IDEDMA_CTL_INTR)) !=
		    IDEDMA_CTL_INTR)
		    continue;
		cp = &sc->pciide_channels[i];
		wdc_cp = &cp->wdc_channel;
		crv = wdcintr(wdc_cp);
		if (crv == 0) {
			printf("%s:%d: bogus intr\n",
			    sc->sc_wdcdev.sc_dev.dv_xname, i);
			bus_space_write_1(sc->sc_dma_iot, sc->sc_dma_ioh,
			    IDEDMA_CTL(i), dmastat);
		} else
			rv = 1;
	}
	return (rv);
}

/* Macros to test product */
#define PDC_IS_262(sc)							\
	((sc)->sc_pp->ide_product == PCI_PRODUCT_PROMISE_PDC20262 ||	\
	(sc)->sc_pp->ide_product == PCI_PRODUCT_PROMISE_PDC20265  ||	\
	(sc)->sc_pp->ide_product == PCI_PRODUCT_PROMISE_PDC20267)
#define PDC_IS_265(sc)							\
	((sc)->sc_pp->ide_product == PCI_PRODUCT_PROMISE_PDC20265 ||	\
	(sc)->sc_pp->ide_product == PCI_PRODUCT_PROMISE_PDC20267  ||	\
	(sc)->sc_pp->ide_product == PCI_PRODUCT_PROMISE_PDC20268  ||	\
	(sc)->sc_pp->ide_product == PCI_PRODUCT_PROMISE_PDC20268R ||	\
	(sc)->sc_pp->ide_product == PCI_PRODUCT_PROMISE_PDC20269  ||	\
	(sc)->sc_pp->ide_product == PCI_PRODUCT_PROMISE_PDC20271  ||	\
	(sc)->sc_pp->ide_product == PCI_PRODUCT_PROMISE_PDC20275  ||	\
	(sc)->sc_pp->ide_product == PCI_PRODUCT_PROMISE_PDC20276  ||	\
	(sc)->sc_pp->ide_product == PCI_PRODUCT_PROMISE_PDC20277)
#define PDC_IS_268(sc)							\
	((sc)->sc_pp->ide_product == PCI_PRODUCT_PROMISE_PDC20268 ||	\
	(sc)->sc_pp->ide_product == PCI_PRODUCT_PROMISE_PDC20268R ||	\
	(sc)->sc_pp->ide_product == PCI_PRODUCT_PROMISE_PDC20269  ||	\
	(sc)->sc_pp->ide_product == PCI_PRODUCT_PROMISE_PDC20271  ||	\
	(sc)->sc_pp->ide_product == PCI_PRODUCT_PROMISE_PDC20275  ||	\
	(sc)->sc_pp->ide_product == PCI_PRODUCT_PROMISE_PDC20276  ||	\
	(sc)->sc_pp->ide_product == PCI_PRODUCT_PROMISE_PDC20277)
#define PDC_IS_269(sc)							\
	((sc)->sc_pp->ide_product == PCI_PRODUCT_PROMISE_PDC20269 ||	\
	(sc)->sc_pp->ide_product == PCI_PRODUCT_PROMISE_PDC20271  ||	\
	(sc)->sc_pp->ide_product == PCI_PRODUCT_PROMISE_PDC20275  ||	\
	(sc)->sc_pp->ide_product == PCI_PRODUCT_PROMISE_PDC20276  ||	\
	(sc)->sc_pp->ide_product == PCI_PRODUCT_PROMISE_PDC20277)

u_int8_t
pdc268_config_read(struct channel_softc *chp, int index)
{
	struct pciide_channel *cp = (struct pciide_channel *)chp;
	struct pciide_softc *sc = (struct pciide_softc *)cp->wdc_channel.wdc;
	int channel = chp->channel;

	bus_space_write_1(sc->sc_dma_iot, sc->sc_dma_ioh,
	    PDC268_INDEX(channel), index);
	return (bus_space_read_1(sc->sc_dma_iot, sc->sc_dma_ioh,
	    PDC268_DATA(channel)));
}

void
pdc202xx_chip_map(struct pciide_softc *sc, struct pci_attach_args *pa)
{
	struct pciide_channel *cp;
	int channel;
	pcireg_t interface, st, mode;
	bus_size_t cmdsize, ctlsize;

	if (!PDC_IS_268(sc)) {
		st = pci_conf_read(sc->sc_pc, sc->sc_tag, PDC2xx_STATE);
		WDCDEBUG_PRINT(("pdc202xx_setup_chip: controller state 0x%x\n",
		    st), DEBUG_PROBE);
	}

	/* turn off  RAID mode */
	if (!PDC_IS_268(sc))
		st &= ~PDC2xx_STATE_IDERAID;

	/*
 	 * can't rely on the PCI_CLASS_REG content if the chip was in raid
	 * mode. We have to fake interface
	 */
	interface = PCIIDE_INTERFACE_SETTABLE(0) | PCIIDE_INTERFACE_SETTABLE(1);
	if (PDC_IS_268(sc) || (st & PDC2xx_STATE_NATIVE))
		interface |= PCIIDE_INTERFACE_PCI(0) | PCIIDE_INTERFACE_PCI(1);

	printf(": DMA");
	pciide_mapreg_dma(sc, pa);

	sc->sc_wdcdev.cap = WDC_CAPABILITY_DATA16 | WDC_CAPABILITY_DATA32 |
	    WDC_CAPABILITY_MODE;
	if (sc->sc_pp->ide_product == PCI_PRODUCT_PROMISE_PDC20246 ||
	    PDC_IS_262(sc))
		sc->sc_wdcdev.cap |= WDC_CAPABILITY_NO_ATAPI_DMA;
	if (sc->sc_dma_ok) {
		sc->sc_wdcdev.cap |= WDC_CAPABILITY_DMA | WDC_CAPABILITY_UDMA;
		sc->sc_wdcdev.cap |= WDC_CAPABILITY_IRQACK;
		sc->sc_wdcdev.irqack = pciide_irqack;
	}
	sc->sc_wdcdev.PIO_cap = 4;
	sc->sc_wdcdev.DMA_cap = 2;
	if (PDC_IS_269(sc))
		sc->sc_wdcdev.UDMA_cap = 6;
	else if (PDC_IS_265(sc))
		sc->sc_wdcdev.UDMA_cap = 5;
	else if (PDC_IS_262(sc))
		sc->sc_wdcdev.UDMA_cap = 4;
	else
		sc->sc_wdcdev.UDMA_cap = 2;
	sc->sc_wdcdev.set_modes = PDC_IS_268(sc) ?
	    pdc20268_setup_channel : pdc202xx_setup_channel;
	sc->sc_wdcdev.channels = sc->wdc_chanarray;
	sc->sc_wdcdev.nchannels = PCIIDE_NUM_CHANNELS;

	if (PDC_IS_262(sc)) {
		sc->sc_wdcdev.dma_start = pdc20262_dma_start;
		sc->sc_wdcdev.dma_finish = pdc20262_dma_finish;
	}

	pciide_print_channels(sc->sc_wdcdev.nchannels, interface);
	if (!PDC_IS_268(sc)) {
		/* setup failsafe defaults */
		mode = 0;
		mode = PDC2xx_TIM_SET_PA(mode, pdc2xx_pa[0]);
		mode = PDC2xx_TIM_SET_PB(mode, pdc2xx_pb[0]);
		mode = PDC2xx_TIM_SET_MB(mode, pdc2xx_dma_mb[0]);
		mode = PDC2xx_TIM_SET_MC(mode, pdc2xx_dma_mc[0]);
		for (channel = 0;
		     channel < sc->sc_wdcdev.nchannels;
		     channel++) {
			WDCDEBUG_PRINT(("pdc202xx_setup_chip: channel %d "
			    "drive 0 initial timings  0x%x, now 0x%x\n",
			    channel, pci_conf_read(sc->sc_pc, sc->sc_tag,
			    PDC2xx_TIM(channel, 0)), mode | PDC2xx_TIM_IORDYp),
			    DEBUG_PROBE);
			pci_conf_write(sc->sc_pc, sc->sc_tag,
			    PDC2xx_TIM(channel, 0), mode | PDC2xx_TIM_IORDYp);
			WDCDEBUG_PRINT(("pdc202xx_setup_chip: channel %d "
			    "drive 1 initial timings  0x%x, now 0x%x\n",
			    channel, pci_conf_read(sc->sc_pc, sc->sc_tag,
	 		    PDC2xx_TIM(channel, 1)), mode), DEBUG_PROBE);
			pci_conf_write(sc->sc_pc, sc->sc_tag,
			    PDC2xx_TIM(channel, 1), mode);
		}

		mode = PDC2xx_SCR_DMA;
		if (PDC_IS_262(sc)) {
			mode = PDC2xx_SCR_SET_GEN(mode, PDC262_SCR_GEN_LAT);
		} else {
			/* the BIOS set it up this way */
			mode = PDC2xx_SCR_SET_GEN(mode, 0x1);
		}
		mode = PDC2xx_SCR_SET_I2C(mode, 0x3); /* ditto */
		mode = PDC2xx_SCR_SET_POLL(mode, 0x1); /* ditto */
		WDCDEBUG_PRINT(("pdc202xx_setup_chip: initial SCR  0x%x, "
		    "now 0x%x\n",
		    bus_space_read_4(sc->sc_dma_iot, sc->sc_dma_ioh,
			PDC2xx_SCR),
		    mode), DEBUG_PROBE);
		bus_space_write_4(sc->sc_dma_iot, sc->sc_dma_ioh,
		    PDC2xx_SCR, mode);

		/* controller initial state register is OK even without BIOS */
		/* Set DMA mode to IDE DMA compatibility */
		mode =
		    bus_space_read_1(sc->sc_dma_iot, sc->sc_dma_ioh, PDC2xx_PM);
		WDCDEBUG_PRINT(("pdc202xx_setup_chip: primary mode 0x%x", mode),
		    DEBUG_PROBE);
		bus_space_write_1(sc->sc_dma_iot, sc->sc_dma_ioh, PDC2xx_PM,
		    mode | 0x1);
		mode =
		    bus_space_read_1(sc->sc_dma_iot, sc->sc_dma_ioh, PDC2xx_SM);
		WDCDEBUG_PRINT((", secondary mode 0x%x\n", mode ), DEBUG_PROBE);
		bus_space_write_1(sc->sc_dma_iot, sc->sc_dma_ioh, PDC2xx_SM,
		    mode | 0x1);
	}

	for (channel = 0; channel < sc->sc_wdcdev.nchannels; channel++) {
		cp = &sc->pciide_channels[channel];
		if (pciide_chansetup(sc, channel, interface) == 0)
			continue;
		if (!PDC_IS_268(sc) && (st & (PDC_IS_262(sc) ?
		    PDC262_STATE_EN(channel):PDC246_STATE_EN(channel))) == 0) {
			printf("%s: %s ignored (disabled)\n",
			    sc->sc_wdcdev.sc_dev.dv_xname, cp->name);
			cp->hw_ok = 0;
			continue;
		}
		pciide_map_compat_intr(pa, cp, channel, interface);
		if (cp->hw_ok == 0)
			continue;
		if (PDC_IS_265(sc))
			pciide_mapchan(pa, cp, interface, &cmdsize, &ctlsize,
			    pdc20265_pci_intr);
		else
			pciide_mapchan(pa, cp, interface, &cmdsize, &ctlsize,
			    pdc202xx_pci_intr);
		if (cp->hw_ok == 0) {
			pciide_unmap_compat_intr(pa, cp, channel, interface);
			continue;
		}
		if (!PDC_IS_268(sc) && pciide_chan_candisable(cp)) {
			st &= ~(PDC_IS_262(sc) ?
			    PDC262_STATE_EN(channel):PDC246_STATE_EN(channel));
			pciide_unmap_compat_intr(pa, cp, channel, interface);
		}
		if (PDC_IS_268(sc))
			pdc20268_setup_channel(&cp->wdc_channel);
		else
			pdc202xx_setup_channel(&cp->wdc_channel);
	}
	if (!PDC_IS_268(sc)) {
		WDCDEBUG_PRINT(("pdc202xx_setup_chip: new controller state "
		    "0x%x\n", st), DEBUG_PROBE);
		pci_conf_write(sc->sc_pc, sc->sc_tag, PDC2xx_STATE, st);
	}
	return;
}

void
pdc202xx_setup_channel(struct channel_softc *chp)
{
	struct ata_drive_datas *drvp;
	int drive;
	pcireg_t mode, st;
	u_int32_t idedma_ctl, scr, atapi;
	struct pciide_channel *cp = (struct pciide_channel *)chp;
	struct pciide_softc *sc = (struct pciide_softc *)cp->wdc_channel.wdc;
	int channel = chp->channel;

	/* setup DMA if needed */
	pciide_channel_dma_setup(cp);

	idedma_ctl = 0;
	WDCDEBUG_PRINT(("pdc202xx_setup_channel %s: scr 0x%x\n",
	    sc->sc_wdcdev.sc_dev.dv_xname,
	    bus_space_read_1(sc->sc_dma_iot, sc->sc_dma_ioh, PDC262_U66)),
	    DEBUG_PROBE);

	/* Per channel settings */
	if (PDC_IS_262(sc)) {
		scr = bus_space_read_1(sc->sc_dma_iot, sc->sc_dma_ioh,
		    PDC262_U66);
		st = pci_conf_read(sc->sc_pc, sc->sc_tag, PDC2xx_STATE);
		/* Check cable */
		if ((st & PDC262_STATE_80P(channel)) != 0 &&
		    ((chp->ch_drive[0].drive_flags & DRIVE_UDMA &&
		    chp->ch_drive[0].UDMA_mode > 2) ||
		    (chp->ch_drive[1].drive_flags & DRIVE_UDMA &&
		    chp->ch_drive[1].UDMA_mode > 2))) {
			WDCDEBUG_PRINT(("%s:%d: 80-wire cable not detected\n",
			    sc->sc_wdcdev.sc_dev.dv_xname, channel),
			    DEBUG_PROBE);
			if (chp->ch_drive[0].UDMA_mode > 2)
				chp->ch_drive[0].UDMA_mode = 2;
			if (chp->ch_drive[1].UDMA_mode > 2)
				chp->ch_drive[1].UDMA_mode = 2;
		}
		/* Trim UDMA mode */
		if ((chp->ch_drive[0].drive_flags & DRIVE_UDMA &&
		    chp->ch_drive[0].UDMA_mode <= 2) ||
		    (chp->ch_drive[1].drive_flags & DRIVE_UDMA &&
		    chp->ch_drive[1].UDMA_mode <= 2)) {
			if (chp->ch_drive[0].UDMA_mode > 2)
				chp->ch_drive[0].UDMA_mode = 2;
			if (chp->ch_drive[1].UDMA_mode > 2)
				chp->ch_drive[1].UDMA_mode = 2;
		}
		/* Set U66 if needed */
		if ((chp->ch_drive[0].drive_flags & DRIVE_UDMA &&
		    chp->ch_drive[0].UDMA_mode > 2) ||
		    (chp->ch_drive[1].drive_flags & DRIVE_UDMA &&
		    chp->ch_drive[1].UDMA_mode > 2))
			scr |= PDC262_U66_EN(channel);
		else
			scr &= ~PDC262_U66_EN(channel);
		bus_space_write_1(sc->sc_dma_iot, sc->sc_dma_ioh,
		    PDC262_U66, scr);
		WDCDEBUG_PRINT(("pdc202xx_setup_channel %s:%d: ATAPI 0x%x\n",
		    sc->sc_wdcdev.sc_dev.dv_xname, channel,
		    bus_space_read_4(sc->sc_dma_iot, sc->sc_dma_ioh,
		    PDC262_ATAPI(channel))), DEBUG_PROBE);
		if (chp->ch_drive[0].drive_flags & DRIVE_ATAPI ||
		    chp->ch_drive[1].drive_flags & DRIVE_ATAPI) {
			if (((chp->ch_drive[0].drive_flags & DRIVE_UDMA) &&
			    !(chp->ch_drive[1].drive_flags & DRIVE_UDMA) &&
			    (chp->ch_drive[1].drive_flags & DRIVE_DMA)) ||
			    ((chp->ch_drive[1].drive_flags & DRIVE_UDMA) &&
			    !(chp->ch_drive[0].drive_flags & DRIVE_UDMA) &&
			    (chp->ch_drive[0].drive_flags & DRIVE_DMA)))
				atapi = 0;
			else
				atapi = PDC262_ATAPI_UDMA;
			bus_space_write_4(sc->sc_dma_iot, sc->sc_dma_ioh,
			    PDC262_ATAPI(channel), atapi);
		}
	}
	for (drive = 0; drive < 2; drive++) {
		drvp = &chp->ch_drive[drive];
		/* If no drive, skip */
		if ((drvp->drive_flags & DRIVE) == 0)
			continue;
		mode = 0;
		if (drvp->drive_flags & DRIVE_UDMA) {
			/* use Ultra/DMA */
			drvp->drive_flags &= ~DRIVE_DMA;
			mode = PDC2xx_TIM_SET_MB(mode,
			   pdc2xx_udma_mb[drvp->UDMA_mode]);
			mode = PDC2xx_TIM_SET_MC(mode,
			   pdc2xx_udma_mc[drvp->UDMA_mode]);
			idedma_ctl |= IDEDMA_CTL_DRV_DMA(drive);
		} else if (drvp->drive_flags & DRIVE_DMA) {
			mode = PDC2xx_TIM_SET_MB(mode,
			    pdc2xx_dma_mb[drvp->DMA_mode]);
			mode = PDC2xx_TIM_SET_MC(mode,
			   pdc2xx_dma_mc[drvp->DMA_mode]);
			idedma_ctl |= IDEDMA_CTL_DRV_DMA(drive);
		} else {
			mode = PDC2xx_TIM_SET_MB(mode,
			    pdc2xx_dma_mb[0]);
			mode = PDC2xx_TIM_SET_MC(mode,
			    pdc2xx_dma_mc[0]);
		}
		mode = PDC2xx_TIM_SET_PA(mode, pdc2xx_pa[drvp->PIO_mode]);
		mode = PDC2xx_TIM_SET_PB(mode, pdc2xx_pb[drvp->PIO_mode]);
		if (drvp->drive_flags & DRIVE_ATA)
			mode |= PDC2xx_TIM_PRE;
		mode |= PDC2xx_TIM_SYNC | PDC2xx_TIM_ERRDY;
		if (drvp->PIO_mode >= 3) {
			mode |= PDC2xx_TIM_IORDY;
			if (drive == 0)
				mode |= PDC2xx_TIM_IORDYp;
		}
		WDCDEBUG_PRINT(("pdc202xx_setup_channel: %s:%d:%d "
		    "timings 0x%x\n",
		    sc->sc_wdcdev.sc_dev.dv_xname,
		    chp->channel, drive, mode), DEBUG_PROBE);
		    pci_conf_write(sc->sc_pc, sc->sc_tag,
		    PDC2xx_TIM(chp->channel, drive), mode);
	}
	if (idedma_ctl != 0) {
		/* Add software bits in status register */
		bus_space_write_1(sc->sc_dma_iot, sc->sc_dma_ioh,
		    IDEDMA_CTL(channel), idedma_ctl);
	}
	pciide_print_modes(cp);
}

void
pdc20268_setup_channel(struct channel_softc *chp)
{
	struct ata_drive_datas *drvp;
	int drive, cable;
	u_int32_t idedma_ctl;
	struct pciide_channel *cp = (struct pciide_channel *)chp;
	struct pciide_softc *sc = (struct pciide_softc *)cp->wdc_channel.wdc;
	int channel = chp->channel;

	/* check 80 pins cable */
	cable = pdc268_config_read(chp, 0x0b) & PDC268_CABLE;

	/* setup DMA if needed */
	pciide_channel_dma_setup(cp);

	idedma_ctl = 0;

	for (drive = 0; drive < 2; drive++) {
		drvp = &chp->ch_drive[drive];
		/* If no drive, skip */
		if ((drvp->drive_flags & DRIVE) == 0)
			continue;
		if (drvp->drive_flags & DRIVE_UDMA) {
			/* use Ultra/DMA */
			drvp->drive_flags &= ~DRIVE_DMA;
			idedma_ctl |= IDEDMA_CTL_DRV_DMA(drive);
			if (cable && drvp->UDMA_mode > 2) {
				WDCDEBUG_PRINT(("%s(%s:%d:%d): 80-wire "
				    "cable not detected\n", drvp->drive_name,
				    sc->sc_wdcdev.sc_dev.dv_xname,
				    channel, drive), DEBUG_PROBE);
				drvp->UDMA_mode = 2;
			}
		} else if (drvp->drive_flags & DRIVE_DMA) {
			idedma_ctl |= IDEDMA_CTL_DRV_DMA(drive);
		}
	}
	/* nothing to do to setup modes, the controller snoop SET_FEATURE cmd */
	if (idedma_ctl != 0) {
		/* Add software bits in status register */
		bus_space_write_1(sc->sc_dma_iot, sc->sc_dma_ioh,
		    IDEDMA_CTL(channel), idedma_ctl);
	}
	pciide_print_modes(cp);
}

int
pdc202xx_pci_intr(void *arg)
{
	struct pciide_softc *sc = arg;
	struct pciide_channel *cp;
	struct channel_softc *wdc_cp;
	int i, rv, crv;
	u_int32_t scr;

	rv = 0;
	scr = bus_space_read_4(sc->sc_dma_iot, sc->sc_dma_ioh, PDC2xx_SCR);
	for (i = 0; i < sc->sc_wdcdev.nchannels; i++) {
		cp = &sc->pciide_channels[i];
		wdc_cp = &cp->wdc_channel;
		/* If a compat channel skip. */
		if (cp->compat)
			continue;
		if (scr & PDC2xx_SCR_INT(i)) {
			crv = wdcintr(wdc_cp);
			if (crv == 0)
				printf("%s:%d: bogus intr (reg 0x%x)\n",
				    sc->sc_wdcdev.sc_dev.dv_xname, i, scr);
			else
				rv = 1;
		}
	}
	return (rv);
}

int
pdc20265_pci_intr(void *arg)
{
	struct pciide_softc *sc = arg;
	struct pciide_channel *cp;
	struct channel_softc *wdc_cp;
	int i, rv, crv;
	u_int32_t dmastat;

	rv = 0;
	for (i = 0; i < sc->sc_wdcdev.nchannels; i++) {
		cp = &sc->pciide_channels[i];
		wdc_cp = &cp->wdc_channel;
		/* If a compat channel skip. */
		if (cp->compat)
			continue;

		/*
		 * In case of shared IRQ check that the interrupt
		 * was actually generated by this channel.
		 * Only check the channel that is enabled.
		 */
		if (cp->hw_ok && PDC_IS_268(sc)) {
			if ((pdc268_config_read(wdc_cp,
			    0x0b) & PDC268_INTR) == 0)
				continue;
		}

		/*
		 * The Ultra/100 seems to assert PDC2xx_SCR_INT * spuriously,
		 * however it asserts INT in IDEDMA_CTL even for non-DMA ops.
		 * So use it instead (requires 2 reg reads instead of 1,
		 * but we can't do it another way).
		 */
		dmastat = bus_space_read_1(sc->sc_dma_iot,
		    sc->sc_dma_ioh, IDEDMA_CTL(i));
		if ((dmastat & IDEDMA_CTL_INTR) == 0)
			continue;

		crv = wdcintr(wdc_cp);
		if (crv == 0)
			printf("%s:%d: bogus intr\n",
			    sc->sc_wdcdev.sc_dev.dv_xname, i);
		else
			rv = 1;
	}
	return (rv);
}

void
pdc20262_dma_start(void *v, int channel, int drive)
{
	struct pciide_softc *sc = v;
	struct pciide_dma_maps *dma_maps =
	    &sc->pciide_channels[channel].dma_maps[drive];
	u_int8_t clock;
	u_int32_t count;

	if (dma_maps->dma_flags & WDC_DMA_LBA48) {
		clock = bus_space_read_1(sc->sc_dma_iot, sc->sc_dma_ioh,
		    PDC262_U66);
		bus_space_write_1(sc->sc_dma_iot, sc->sc_dma_ioh,
		    PDC262_U66, clock | PDC262_U66_EN(channel));
		count = dma_maps->dmamap_xfer->dm_mapsize >> 1;
		count |= dma_maps->dma_flags & WDC_DMA_READ ?
		    PDC262_ATAPI_LBA48_READ : PDC262_ATAPI_LBA48_WRITE;
		bus_space_write_4(sc->sc_dma_iot, sc->sc_dma_ioh,
		    PDC262_ATAPI(channel), count);
	}

	pciide_dma_start(v, channel, drive);
}

int
pdc20262_dma_finish(void *v, int channel, int drive, int force)
{
	struct pciide_softc *sc = v;
	struct pciide_dma_maps *dma_maps =
	    &sc->pciide_channels[channel].dma_maps[drive];
 	u_int8_t clock;

	if (dma_maps->dma_flags & WDC_DMA_LBA48) {
		clock = bus_space_read_1(sc->sc_dma_iot, sc->sc_dma_ioh,
		    PDC262_U66);
		bus_space_write_1(sc->sc_dma_iot, sc->sc_dma_ioh,
		    PDC262_U66, clock & ~PDC262_U66_EN(channel));
		bus_space_write_4(sc->sc_dma_iot, sc->sc_dma_ioh,
		    PDC262_ATAPI(channel), 0);
	}

	return (pciide_dma_finish(v, channel, drive, force));
}

void
pdcsata_chip_map(struct pciide_softc *sc, struct pci_attach_args *pa)
{
	struct pciide_channel *cp;
	struct channel_softc *wdc_cp;
	struct pciide_pdcsata *ps;
	int channel, i;
	bus_size_t dmasize;
	pci_intr_handle_t intrhandle;
	const char *intrstr;

	/* Allocate memory for private data */
	sc->sc_cookielen = sizeof(*ps);
	sc->sc_cookie = malloc(sc->sc_cookielen, M_DEVBUF, M_NOWAIT | M_ZERO);
	ps = sc->sc_cookie;

	/*
	 * Promise SATA controllers have 3 or 4 channels,
	 * the usual IDE registers are mapped in I/O space, with offsets.
	 */
	if (pci_intr_map(pa, &intrhandle) != 0) {
		printf(": couldn't map interrupt\n");
		return;
	}
	intrstr = pci_intr_string(pa->pa_pc, intrhandle);

	switch (sc->sc_pp->ide_product) {
	case PCI_PRODUCT_PROMISE_PDC20318:
	case PCI_PRODUCT_PROMISE_PDC20319:
	case PCI_PRODUCT_PROMISE_PDC20371:
	case PCI_PRODUCT_PROMISE_PDC20375:
	case PCI_PRODUCT_PROMISE_PDC20376:
	case PCI_PRODUCT_PROMISE_PDC20377:
	case PCI_PRODUCT_PROMISE_PDC20378:
	case PCI_PRODUCT_PROMISE_PDC20379:
	default:
		sc->sc_pci_ih = pci_intr_establish(pa->pa_pc,
	    	    intrhandle, IPL_BIO, pdc203xx_pci_intr, sc,
	    	    sc->sc_wdcdev.sc_dev.dv_xname);
		break;

	case PCI_PRODUCT_PROMISE_PDC40518:
	case PCI_PRODUCT_PROMISE_PDC40519:
	case PCI_PRODUCT_PROMISE_PDC40718:
	case PCI_PRODUCT_PROMISE_PDC40719:
	case PCI_PRODUCT_PROMISE_PDC40779:
	case PCI_PRODUCT_PROMISE_PDC20571:
	case PCI_PRODUCT_PROMISE_PDC20575:
	case PCI_PRODUCT_PROMISE_PDC20579:
	case PCI_PRODUCT_PROMISE_PDC20771:
	case PCI_PRODUCT_PROMISE_PDC20775:
		sc->sc_pci_ih = pci_intr_establish(pa->pa_pc,
	    	    intrhandle, IPL_BIO, pdc205xx_pci_intr, sc,
	    	    sc->sc_wdcdev.sc_dev.dv_xname);
		break;
	}
		
	if (sc->sc_pci_ih == NULL) {
		printf(": couldn't establish native-PCI interrupt");
		if (intrstr != NULL)
			printf(" at %s", intrstr);
		printf("\n");
		return;
	}

	sc->sc_dma_ok = (pci_mapreg_map(pa, PCIIDE_REG_BUS_MASTER_DMA,
	    PCI_MAPREG_MEM_TYPE_32BIT, 0, &sc->sc_dma_iot,
	    &sc->sc_dma_ioh, NULL, &dmasize, 0) == 0);
	if (!sc->sc_dma_ok) {
		printf(": couldn't map bus-master DMA registers\n");
		pci_intr_disestablish(pa->pa_pc, sc->sc_pci_ih);
		return;
	}

	sc->sc_dmat = pa->pa_dmat;

	if (pci_mapreg_map(pa, PDC203xx_BAR_IDEREGS,
	    PCI_MAPREG_MEM_TYPE_32BIT, 0, &ps->ba5_st,
	    &ps->ba5_sh, NULL, NULL, 0) != 0) {
		printf(": couldn't map IDE registers\n");
		bus_space_unmap(sc->sc_dma_iot, sc->sc_dma_ioh, dmasize);
		pci_intr_disestablish(pa->pa_pc, sc->sc_pci_ih);
		return;
	}

	printf(": DMA\n");

	sc->sc_wdcdev.cap = WDC_CAPABILITY_DATA16;
	sc->sc_wdcdev.cap |= WDC_CAPABILITY_DMA | WDC_CAPABILITY_UDMA;
	sc->sc_wdcdev.cap |= WDC_CAPABILITY_IRQACK;
	sc->sc_wdcdev.irqack = pdc203xx_irqack;
	sc->sc_wdcdev.PIO_cap = 4;
	sc->sc_wdcdev.DMA_cap = 2;
	sc->sc_wdcdev.UDMA_cap = 6;
	sc->sc_wdcdev.set_modes = pdc203xx_setup_channel;
	sc->sc_wdcdev.channels = sc->wdc_chanarray;

	switch (sc->sc_pp->ide_product) {
	case PCI_PRODUCT_PROMISE_PDC20318:
	case PCI_PRODUCT_PROMISE_PDC20319:
	case PCI_PRODUCT_PROMISE_PDC20371:
	case PCI_PRODUCT_PROMISE_PDC20375:
	case PCI_PRODUCT_PROMISE_PDC20376:
	case PCI_PRODUCT_PROMISE_PDC20377:
	case PCI_PRODUCT_PROMISE_PDC20378:
	case PCI_PRODUCT_PROMISE_PDC20379:
	default:
		bus_space_write_4(ps->ba5_st, ps->ba5_sh, 0x06c, 0x00ff0033);
		sc->sc_wdcdev.nchannels =
		    (bus_space_read_4(ps->ba5_st, ps->ba5_sh, 0x48) & 0x02) ?
		    PDC203xx_NCHANNELS : 3;
		break;

	case PCI_PRODUCT_PROMISE_PDC40518:
	case PCI_PRODUCT_PROMISE_PDC40519:
	case PCI_PRODUCT_PROMISE_PDC40718:
	case PCI_PRODUCT_PROMISE_PDC40719:
	case PCI_PRODUCT_PROMISE_PDC40779:
	case PCI_PRODUCT_PROMISE_PDC20571:
		bus_space_write_4(ps->ba5_st, ps->ba5_sh, 0x60, 0x00ff00ff);
		sc->sc_wdcdev.nchannels = PDC40718_NCHANNELS;
  	 
		sc->sc_wdcdev.reset = pdc205xx_do_reset;
		sc->sc_wdcdev.drv_probe = pdc205xx_drv_probe;
		
		break;
	case PCI_PRODUCT_PROMISE_PDC20575:
	case PCI_PRODUCT_PROMISE_PDC20579:
	case PCI_PRODUCT_PROMISE_PDC20771:
	case PCI_PRODUCT_PROMISE_PDC20775:
		bus_space_write_4(ps->ba5_st, ps->ba5_sh, 0x60, 0x00ff00ff);
		sc->sc_wdcdev.nchannels = PDC20575_NCHANNELS;

		sc->sc_wdcdev.reset = pdc205xx_do_reset;
		sc->sc_wdcdev.drv_probe = pdc205xx_drv_probe;
		
		break;
	}

	sc->sc_wdcdev.dma_arg = sc;
	sc->sc_wdcdev.dma_init = pciide_dma_init;
	sc->sc_wdcdev.dma_start = pdc203xx_dma_start;
	sc->sc_wdcdev.dma_finish = pdc203xx_dma_finish;

	for (channel = 0; channel < sc->sc_wdcdev.nchannels;
	     channel++) {
		cp = &sc->pciide_channels[channel];
		sc->wdc_chanarray[channel] = &cp->wdc_channel;

		cp->ih = sc->sc_pci_ih;
		cp->name = NULL;
		cp->wdc_channel.channel = channel;
		cp->wdc_channel.wdc = &sc->sc_wdcdev;
		cp->wdc_channel.ch_queue = wdc_alloc_queue();
		if (cp->wdc_channel.ch_queue == NULL) {
			printf("%s: channel %d: "
			    "cannot allocate channel queue\n",
			sc->sc_wdcdev.sc_dev.dv_xname, channel);
			continue;
		}
		wdc_cp = &cp->wdc_channel;

		ps->regs[channel].ctl_iot = ps->ba5_st;
		ps->regs[channel].cmd_iot = ps->ba5_st;

		if (bus_space_subregion(ps->ba5_st, ps->ba5_sh,
		    0x0238 + (channel << 7), 1,
		    &ps->regs[channel].ctl_ioh) != 0) {
			printf("%s: couldn't map channel %d ctl regs\n",
			    sc->sc_wdcdev.sc_dev.dv_xname,
			    channel);
			continue;
		}
		for (i = 0; i < WDC_NREG; i++) {
			if (bus_space_subregion(ps->ba5_st, ps->ba5_sh,
			    0x0200 + (i << 2) + (channel << 7), i == 0 ? 4 : 1,
			    &ps->regs[channel].cmd_iohs[i]) != 0) {
				printf("%s: couldn't map channel %d cmd "
				    "regs\n",
				    sc->sc_wdcdev.sc_dev.dv_xname,
				    channel);
				goto loop_end;
			}
		}
		ps->regs[channel].cmd_iohs[wdr_status & _WDC_REGMASK] =
		    ps->regs[channel].cmd_iohs[wdr_command & _WDC_REGMASK];
		ps->regs[channel].cmd_iohs[wdr_features & _WDC_REGMASK] =
		    ps->regs[channel].cmd_iohs[wdr_error & _WDC_REGMASK];
		wdc_cp->data32iot = wdc_cp->cmd_iot =
		    ps->regs[channel].cmd_iot;
		wdc_cp->data32ioh = wdc_cp->cmd_ioh =
		    ps->regs[channel].cmd_iohs[0];
		wdc_cp->_vtbl = &wdc_pdc203xx_vtbl;

		/*
		 * Subregion de busmaster registers. They're spread all over
		 * the controller's register space :(. They are also 4 bytes
		 * sized, with some specific extensions in the extra bits.
		 * It also seems that the IDEDMA_CTL register isn't available.
		 */
		if (bus_space_subregion(ps->ba5_st, ps->ba5_sh,
		    0x260 + (channel << 7), 1,
		    &ps->regs[channel].dma_iohs[IDEDMA_CMD(0)]) != 0) {
			printf("%s channel %d: can't subregion DMA "
			    "registers\n",
			    sc->sc_wdcdev.sc_dev.dv_xname, channel);
			continue;
		}
		if (bus_space_subregion(ps->ba5_st, ps->ba5_sh,
		    0x244 + (channel << 7), 4,
		    &ps->regs[channel].dma_iohs[IDEDMA_TBL(0)]) != 0) {
			printf("%s channel %d: can't subregion DMA "
			    "registers\n",
			    sc->sc_wdcdev.sc_dev.dv_xname, channel);
			continue;
		}

		wdcattach(wdc_cp);
		bus_space_write_4(sc->sc_dma_iot,
		    ps->regs[channel].dma_iohs[IDEDMA_CMD(0)], 0,
		    (bus_space_read_4(sc->sc_dma_iot,
			ps->regs[channel].dma_iohs[IDEDMA_CMD(0)],
			0) & ~0x00003f9f) | (channel + 1));
		bus_space_write_4(ps->ba5_st, ps->ba5_sh,
		    (channel + 1) << 2, 0x00000001);

		pdc203xx_setup_channel(&cp->wdc_channel);

loop_end: ;
	}

	printf("%s: using %s for native-PCI interrupt\n",
	    sc->sc_wdcdev.sc_dev.dv_xname,
	    intrstr ? intrstr : "unknown interrupt");
}

void
pdc203xx_setup_channel(struct channel_softc *chp)
{
	struct ata_drive_datas *drvp;
	struct pciide_channel *cp = (struct pciide_channel *)chp;
	int drive, s;

	pciide_channel_dma_setup(cp);

	for (drive = 0; drive < 2; drive++) {
		drvp = &chp->ch_drive[drive];
		if ((drvp->drive_flags & DRIVE) == 0)
			continue;
		if (drvp->drive_flags & DRIVE_UDMA) {
			s = splbio();
			drvp->drive_flags &= ~DRIVE_DMA;
			splx(s);
		}
	}
	pciide_print_modes(cp);
}

int
pdc203xx_pci_intr(void *arg)
{
	struct pciide_softc *sc = arg;
	struct pciide_channel *cp;
	struct channel_softc *wdc_cp;
	struct pciide_pdcsata *ps = sc->sc_cookie;
	int i, rv, crv;
	u_int32_t scr;

	rv = 0;
	scr = bus_space_read_4(ps->ba5_st, ps->ba5_sh, 0x00040);

	for (i = 0; i < sc->sc_wdcdev.nchannels; i++) {
		cp = &sc->pciide_channels[i];
		wdc_cp = &cp->wdc_channel;
		if (scr & (1 << (i + 1))) {
			crv = wdcintr(wdc_cp);
			if (crv == 0) {
				printf("%s:%d: bogus intr (reg 0x%x)\n",
				    sc->sc_wdcdev.sc_dev.dv_xname,
				    i, scr);
			} else
				rv = 1;
		}
	}

	return (rv);
}

int
pdc205xx_pci_intr(void *arg)
{
	struct pciide_softc *sc = arg;
	struct pciide_channel *cp;
	struct channel_softc *wdc_cp;
	struct pciide_pdcsata *ps = sc->sc_cookie;
	int i, rv, crv;
	u_int32_t scr, status;

	rv = 0;
	scr = bus_space_read_4(ps->ba5_st, ps->ba5_sh, 0x40);
	bus_space_write_4(ps->ba5_st, ps->ba5_sh, 0x40, scr & 0x0000ffff);

	status = bus_space_read_4(ps->ba5_st, ps->ba5_sh, 0x60);
	bus_space_write_4(ps->ba5_st, ps->ba5_sh, 0x60, status & 0x000000ff);

	for (i = 0; i < sc->sc_wdcdev.nchannels; i++) {
		cp = &sc->pciide_channels[i];
		wdc_cp = &cp->wdc_channel;
		if (scr & (1 << (i + 1))) {
			crv = wdcintr(wdc_cp);
			if (crv == 0) {
				printf("%s:%d: bogus intr (reg 0x%x)\n",
				    sc->sc_wdcdev.sc_dev.dv_xname,
				    i, scr);
			} else
				rv = 1;
		}
	}
	return rv;
}

void
pdc203xx_irqack(struct channel_softc *chp)
{
	struct pciide_channel *cp = (struct pciide_channel *)chp;
	struct pciide_softc *sc = (struct pciide_softc *)cp->wdc_channel.wdc;
	struct pciide_pdcsata *ps = sc->sc_cookie;
	int chan = chp->channel;

	bus_space_write_4(sc->sc_dma_iot,
	    ps->regs[chan].dma_iohs[IDEDMA_CMD(0)], 0,
	    (bus_space_read_4(sc->sc_dma_iot,
		ps->regs[chan].dma_iohs[IDEDMA_CMD(0)],
		0) & ~0x00003f9f) | (chan + 1));
	bus_space_write_4(ps->ba5_st, ps->ba5_sh,
	    (chan + 1) << 2, 0x00000001);
}

void
pdc203xx_dma_start(void *v, int channel, int drive)
{
	struct pciide_softc *sc = v;
	struct pciide_channel *cp = &sc->pciide_channels[channel];
	struct pciide_dma_maps *dma_maps = &cp->dma_maps[drive];
	struct pciide_pdcsata *ps = sc->sc_cookie;

	/* Write table address */
	bus_space_write_4(sc->sc_dma_iot,
	    ps->regs[channel].dma_iohs[IDEDMA_TBL(0)], 0,
	    dma_maps->dmamap_table->dm_segs[0].ds_addr);

	/* Start DMA engine */
	bus_space_write_4(sc->sc_dma_iot,
	    ps->regs[channel].dma_iohs[IDEDMA_CMD(0)], 0,
	    (bus_space_read_4(sc->sc_dma_iot,
	    ps->regs[channel].dma_iohs[IDEDMA_CMD(0)],
	    0) & ~0xc0) | ((dma_maps->dma_flags & WDC_DMA_READ) ? 0x80 : 0xc0));
}

int
pdc203xx_dma_finish(void *v, int channel, int drive, int force)
{
	struct pciide_softc *sc = v;
	struct pciide_channel *cp = &sc->pciide_channels[channel];
	struct pciide_dma_maps *dma_maps = &cp->dma_maps[drive];
	struct pciide_pdcsata *ps = sc->sc_cookie;

	/* Stop DMA channel */
	bus_space_write_4(sc->sc_dma_iot,
	    ps->regs[channel].dma_iohs[IDEDMA_CMD(0)], 0,
	    (bus_space_read_4(sc->sc_dma_iot,
	    ps->regs[channel].dma_iohs[IDEDMA_CMD(0)],
	    0) & ~0x80));

	/* Unload the map of the data buffer */
	bus_dmamap_sync(sc->sc_dmat, dma_maps->dmamap_xfer, 0,
	    dma_maps->dmamap_xfer->dm_mapsize,
	    (dma_maps->dma_flags & WDC_DMA_READ) ?
	    BUS_DMASYNC_POSTREAD : BUS_DMASYNC_POSTWRITE);
	bus_dmamap_unload(sc->sc_dmat, dma_maps->dmamap_xfer);

	return (0);
}

u_int8_t
pdc203xx_read_reg(struct channel_softc *chp, enum wdc_regs reg)
{
	struct pciide_channel *cp = (struct pciide_channel *)chp;
	struct pciide_softc *sc = (struct pciide_softc *)cp->wdc_channel.wdc;
	struct pciide_pdcsata *ps = sc->sc_cookie;
	u_int8_t val;

	if (reg & _WDC_AUX) {
		return (bus_space_read_1(ps->regs[chp->channel].ctl_iot,
		    ps->regs[chp->channel].ctl_ioh, reg & _WDC_REGMASK));
	} else {
		val = bus_space_read_1(ps->regs[chp->channel].cmd_iot,
		    ps->regs[chp->channel].cmd_iohs[reg & _WDC_REGMASK], 0);
		return (val);
	}
}

void
pdc203xx_write_reg(struct channel_softc *chp, enum wdc_regs reg, u_int8_t val)
{
	struct pciide_channel *cp = (struct pciide_channel *)chp;
	struct pciide_softc *sc = (struct pciide_softc *)cp->wdc_channel.wdc;
	struct pciide_pdcsata *ps = sc->sc_cookie;

	if (reg & _WDC_AUX)
		bus_space_write_1(ps->regs[chp->channel].ctl_iot,
		    ps->regs[chp->channel].ctl_ioh, reg & _WDC_REGMASK, val);
	else
		bus_space_write_1(ps->regs[chp->channel].cmd_iot,
		    ps->regs[chp->channel].cmd_iohs[reg & _WDC_REGMASK],
		    0, val);
}

void
pdc205xx_do_reset(struct channel_softc *chp)
{
	struct pciide_channel *cp = (struct pciide_channel *)chp;
	struct pciide_softc *sc = (struct pciide_softc *)cp->wdc_channel.wdc;
	struct pciide_pdcsata *ps = sc->sc_cookie;
	u_int32_t scontrol;

	wdc_do_reset(chp);

	/* reset SATA */
	scontrol = SControl_DET_INIT | SControl_SPD_ANY | SControl_IPM_NONE;
	SCONTROL_WRITE(ps, chp->channel, scontrol);
	delay(50*1000);

	scontrol &= ~SControl_DET_INIT;
	SCONTROL_WRITE(ps, chp->channel, scontrol);
	delay(50*1000);
}

void
pdc205xx_drv_probe(struct channel_softc *chp)
{
	struct pciide_channel *cp = (struct pciide_channel *)chp;
	struct pciide_softc *sc = (struct pciide_softc *)cp->wdc_channel.wdc;
	struct pciide_pdcsata *ps = sc->sc_cookie;
	bus_space_handle_t *iohs;
	u_int32_t scontrol, sstatus;
	u_int16_t scnt, sn, cl, ch;
	int s;

	SCONTROL_WRITE(ps, chp->channel, 0);
	delay(50*1000);

	scontrol = SControl_DET_INIT | SControl_SPD_ANY | SControl_IPM_NONE;
	SCONTROL_WRITE(ps,chp->channel,scontrol);
	delay(50*1000);

	scontrol &= ~SControl_DET_INIT;
	SCONTROL_WRITE(ps,chp->channel,scontrol);
	delay(50*1000);

	sstatus = SSTATUS_READ(ps,chp->channel);

	switch (sstatus & SStatus_DET_mask) {
	case SStatus_DET_NODEV:
		/* No Device; be silent.  */
		break;

	case SStatus_DET_DEV_NE:
		printf("%s: port %d: device connected, but "
		    "communication not established\n",
		    sc->sc_wdcdev.sc_dev.dv_xname, chp->channel);
		break;

	case SStatus_DET_OFFLINE:
		printf("%s: port %d: PHY offline\n",
		    sc->sc_wdcdev.sc_dev.dv_xname, chp->channel);
		break;

	case SStatus_DET_DEV:
		iohs = ps->regs[chp->channel].cmd_iohs;
		bus_space_write_1(chp->cmd_iot, iohs[wdr_sdh], 0,
		    WDSD_IBM);
		delay(10);	/* 400ns delay */
		scnt = bus_space_read_2(chp->cmd_iot, iohs[wdr_seccnt], 0);
		sn = bus_space_read_2(chp->cmd_iot, iohs[wdr_sector], 0);
		cl = bus_space_read_2(chp->cmd_iot, iohs[wdr_cyl_lo], 0);
		ch = bus_space_read_2(chp->cmd_iot, iohs[wdr_cyl_hi], 0);
#if 0
		printf("%s: port %d: scnt=0x%x sn=0x%x cl=0x%x ch=0x%x\n",
		    sc->sc_wdcdev.sc_dev.dv_xname, chp->channel,
		    scnt, sn, cl, ch);
#endif
		/*
		 * scnt and sn are supposed to be 0x1 for ATAPI, but in some
		 * cases we get wrong values here, so ignore it.
		 */
		s = splbio();
		if (cl == 0x14 && ch == 0xeb)
			chp->ch_drive[0].drive_flags |= DRIVE_ATAPI;
		else
			chp->ch_drive[0].drive_flags |= DRIVE_ATA;
		splx(s);
#if 0
		printf("%s: port %d",
		    sc->sc_wdcdev.sc_dev.dv_xname, chp->channel);
		switch ((sstatus & SStatus_SPD_mask) >> SStatus_SPD_shift) {
		case 1:
			printf(": 1.5Gb/s");
			break;
		case 2:
			printf(": 3.0Gb/s");
			break;
		}
		printf("\n");
#endif
		break;

	default:
		printf("%s: port %d: unknown SStatus: 0x%08x\n",
		    sc->sc_wdcdev.sc_dev.dv_xname, chp->channel, sstatus);
	}
}

void
serverworks_chip_map(struct pciide_softc *sc, struct pci_attach_args *pa)
{
	struct pciide_channel *cp;
	pcireg_t interface = PCI_INTERFACE(pa->pa_class);
	pcitag_t pcib_tag;
	int channel;
	bus_size_t cmdsize, ctlsize;

	printf(": DMA");
	pciide_mapreg_dma(sc, pa);
	printf("\n");
	sc->sc_wdcdev.cap = WDC_CAPABILITY_DATA16 | WDC_CAPABILITY_DATA32 |
	    WDC_CAPABILITY_MODE;

	if (sc->sc_dma_ok) {
		sc->sc_wdcdev.cap |= WDC_CAPABILITY_DMA | WDC_CAPABILITY_UDMA;
		sc->sc_wdcdev.cap |= WDC_CAPABILITY_IRQACK;
		sc->sc_wdcdev.irqack = pciide_irqack;
	}
	sc->sc_wdcdev.PIO_cap = 4;
	sc->sc_wdcdev.DMA_cap = 2;
	switch (sc->sc_pp->ide_product) {
	case PCI_PRODUCT_RCC_OSB4_IDE:
		sc->sc_wdcdev.UDMA_cap = 2;
		break;
	case PCI_PRODUCT_RCC_CSB5_IDE:
		if (sc->sc_rev < 0x92)
			sc->sc_wdcdev.UDMA_cap = 4;
		else
			sc->sc_wdcdev.UDMA_cap = 5;
		break;
	case PCI_PRODUCT_RCC_CSB6_IDE:
		sc->sc_wdcdev.UDMA_cap = 4;
		break;
	case PCI_PRODUCT_RCC_CSB6_RAID_IDE:
	case PCI_PRODUCT_RCC_HT_1000_IDE:
		sc->sc_wdcdev.UDMA_cap = 5;
		break;
	}

	sc->sc_wdcdev.set_modes = serverworks_setup_channel;
	sc->sc_wdcdev.channels = sc->wdc_chanarray;
	sc->sc_wdcdev.nchannels =
	    (sc->sc_pp->ide_product == PCI_PRODUCT_RCC_CSB6_IDE ? 1 : 2);

	for (channel = 0; channel < sc->sc_wdcdev.nchannels; channel++) {
		cp = &sc->pciide_channels[channel];
		if (pciide_chansetup(sc, channel, interface) == 0)
			continue;
		pciide_mapchan(pa, cp, interface, &cmdsize, &ctlsize,
		    serverworks_pci_intr);
		if (cp->hw_ok == 0)
			return;
		pciide_map_compat_intr(pa, cp, channel, interface);
		if (cp->hw_ok == 0)
			return;
		serverworks_setup_channel(&cp->wdc_channel);
	}

	pcib_tag = pci_make_tag(pa->pa_pc, pa->pa_bus, pa->pa_device, 0);
	pci_conf_write(pa->pa_pc, pcib_tag, 0x64,
	    (pci_conf_read(pa->pa_pc, pcib_tag, 0x64) & ~0x2000) | 0x4000);
}

void
serverworks_setup_channel(struct channel_softc *chp)
{
	struct ata_drive_datas *drvp;
	struct pciide_channel *cp = (struct pciide_channel *)chp;
	struct pciide_softc *sc = (struct pciide_softc *)cp->wdc_channel.wdc;
	int channel = chp->channel;
	int drive, unit;
	u_int32_t pio_time, dma_time, pio_mode, udma_mode;
	u_int32_t idedma_ctl;
	static const u_int8_t pio_modes[5] = {0x5d, 0x47, 0x34, 0x22, 0x20};
	static const u_int8_t dma_modes[3] = {0x77, 0x21, 0x20};

	/* setup DMA if needed */
	pciide_channel_dma_setup(cp);

	pio_time = pci_conf_read(sc->sc_pc, sc->sc_tag, 0x40);
	dma_time = pci_conf_read(sc->sc_pc, sc->sc_tag, 0x44);
	pio_mode = pci_conf_read(sc->sc_pc, sc->sc_tag, 0x48);
	udma_mode = pci_conf_read(sc->sc_pc, sc->sc_tag, 0x54);

	pio_time &= ~(0xffff << (16 * channel));
	dma_time &= ~(0xffff << (16 * channel));
	pio_mode &= ~(0xff << (8 * channel + 16));
	udma_mode &= ~(0xff << (8 * channel + 16));
	udma_mode &= ~(3 << (2 * channel));

	idedma_ctl = 0;

	/* Per drive settings */
	for (drive = 0; drive < 2; drive++) {
		drvp = &chp->ch_drive[drive];
		/* If no drive, skip */
		if ((drvp->drive_flags & DRIVE) == 0)
			continue;
		unit = drive + 2 * channel;
		/* add timing values, setup DMA if needed */
		pio_time |= pio_modes[drvp->PIO_mode] << (8 * (unit^1));
		pio_mode |= drvp->PIO_mode << (4 * unit + 16);
		if ((chp->wdc->cap & WDC_CAPABILITY_UDMA) &&
		    (drvp->drive_flags & DRIVE_UDMA)) {
			/* use Ultra/DMA, check for 80-pin cable */
			if (sc->sc_rev <= 0x92 && drvp->UDMA_mode > 2 &&
			    (PCI_PRODUCT(pci_conf_read(sc->sc_pc, sc->sc_tag,
			    PCI_SUBSYS_ID_REG)) &
			    (1 << (14 + channel))) == 0) {
				WDCDEBUG_PRINT(("%s(%s:%d:%d): 80-wire "
				    "cable not detected\n", drvp->drive_name,
				    sc->sc_wdcdev.sc_dev.dv_xname,
				    channel, drive), DEBUG_PROBE);
				drvp->UDMA_mode = 2;
			}
			dma_time |= dma_modes[drvp->DMA_mode] << (8 * (unit^1));
			udma_mode |= drvp->UDMA_mode << (4 * unit + 16);
			udma_mode |= 1 << unit;
			idedma_ctl |= IDEDMA_CTL_DRV_DMA(drive);
		} else if ((chp->wdc->cap & WDC_CAPABILITY_DMA) &&
		    (drvp->drive_flags & DRIVE_DMA)) {
			/* use Multiword DMA */
			drvp->drive_flags &= ~DRIVE_UDMA;
			dma_time |= dma_modes[drvp->DMA_mode] << (8 * (unit^1));
			idedma_ctl |= IDEDMA_CTL_DRV_DMA(drive);
		} else {
			/* PIO only */
			drvp->drive_flags &= ~(DRIVE_UDMA | DRIVE_DMA);
		}
	}

	pci_conf_write(sc->sc_pc, sc->sc_tag, 0x40, pio_time);
	pci_conf_write(sc->sc_pc, sc->sc_tag, 0x44, dma_time);
	if (sc->sc_pp->ide_product != PCI_PRODUCT_RCC_OSB4_IDE)
		pci_conf_write(sc->sc_pc, sc->sc_tag, 0x48, pio_mode);
	pci_conf_write(sc->sc_pc, sc->sc_tag, 0x54, udma_mode);

	if (idedma_ctl != 0) {
		/* Add software bits in status register */
		bus_space_write_1(sc->sc_dma_iot, sc->sc_dma_ioh,
		    IDEDMA_CTL(channel), idedma_ctl);
	}
	pciide_print_modes(cp);
}

int
serverworks_pci_intr(void *arg)
{
	struct pciide_softc *sc = arg;
	struct pciide_channel *cp;
	struct channel_softc *wdc_cp;
	int rv = 0;
	int dmastat, i, crv;

	for (i = 0; i < sc->sc_wdcdev.nchannels; i++) {
		dmastat = bus_space_read_1(sc->sc_dma_iot, sc->sc_dma_ioh,
		    IDEDMA_CTL(i));
		if ((dmastat & (IDEDMA_CTL_ACT | IDEDMA_CTL_INTR)) !=
		    IDEDMA_CTL_INTR)
			continue;
		cp = &sc->pciide_channels[i];
		wdc_cp = &cp->wdc_channel;
		crv = wdcintr(wdc_cp);
		if (crv == 0) {
			printf("%s:%d: bogus intr\n",
			    sc->sc_wdcdev.sc_dev.dv_xname, i);
			bus_space_write_1(sc->sc_dma_iot, sc->sc_dma_ioh,
			    IDEDMA_CTL(i), dmastat);
		} else
			rv = 1;
	}
	return (rv);
}

void
svwsata_chip_map(struct pciide_softc *sc, struct pci_attach_args *pa)
{
	struct pciide_channel *cp;
	pci_intr_handle_t intrhandle;
	const char *intrstr;
	int channel;
	struct pciide_svwsata *ss;

	/* Allocate memory for private data */
	sc->sc_cookielen = sizeof(*ss);
	sc->sc_cookie = malloc(sc->sc_cookielen, M_DEVBUF, M_NOWAIT | M_ZERO);
	ss = sc->sc_cookie;

	/* The 4-port version has a dummy second function. */
	if (pci_conf_read(sc->sc_pc, sc->sc_tag,
	    PCI_MAPREG_START + 0x14) == 0) {
		printf("\n");
		return;
	}

	if (pci_mapreg_map(pa, PCI_MAPREG_START + 0x14,
	    PCI_MAPREG_TYPE_MEM | PCI_MAPREG_MEM_TYPE_32BIT, 0,
	    &ss->ba5_st, &ss->ba5_sh, NULL, NULL, 0) != 0) {
		printf(": unable to map BA5 register space\n");
		return;
	}

	printf(": DMA");
	svwsata_mapreg_dma(sc, pa);
	printf("\n");

	if (sc->sc_dma_ok) {
		sc->sc_wdcdev.cap |= WDC_CAPABILITY_UDMA |
		    WDC_CAPABILITY_DMA | WDC_CAPABILITY_IRQACK;
		sc->sc_wdcdev.irqack = pciide_irqack;
	}
	sc->sc_wdcdev.PIO_cap = 4;
	sc->sc_wdcdev.DMA_cap = 2;
	sc->sc_wdcdev.UDMA_cap = 6;

	sc->sc_wdcdev.channels = sc->wdc_chanarray;
	sc->sc_wdcdev.nchannels = 4;
	sc->sc_wdcdev.cap |= WDC_CAPABILITY_DATA16 | WDC_CAPABILITY_DATA32 |
	    WDC_CAPABILITY_MODE | WDC_CAPABILITY_SATA;
	sc->sc_wdcdev.set_modes = sata_setup_channel;

	/* We can use SControl and SStatus to probe for drives. */
	sc->sc_wdcdev.drv_probe = svwsata_drv_probe;

	/* Map and establish the interrupt handler. */
	if(pci_intr_map(pa, &intrhandle) != 0) {
		printf("%s: couldn't map native-PCI interrupt\n",
		    sc->sc_wdcdev.sc_dev.dv_xname);
		return;
	}
	intrstr = pci_intr_string(pa->pa_pc, intrhandle);
	sc->sc_pci_ih = pci_intr_establish(pa->pa_pc, intrhandle, IPL_BIO,
	    pciide_pci_intr, sc, sc->sc_wdcdev.sc_dev.dv_xname);
	if (sc->sc_pci_ih != NULL) {
		printf("%s: using %s for native-PCI interrupt\n",
		    sc->sc_wdcdev.sc_dev.dv_xname,
		    intrstr ? intrstr : "unknown interrupt");
	} else {
		printf("%s: couldn't establish native-PCI interrupt",
		    sc->sc_wdcdev.sc_dev.dv_xname);
		if (intrstr != NULL)
			printf(" at %s", intrstr);
		printf("\n");
		return;
	}

	switch (sc->sc_pp->ide_product) {
	case PCI_PRODUCT_RCC_K2_SATA:
		bus_space_write_4(ss->ba5_st, ss->ba5_sh, SVWSATA_SICR1,
		    bus_space_read_4(ss->ba5_st, ss->ba5_sh, SVWSATA_SICR1)
		    & ~0x00040000);
		bus_space_write_4(ss->ba5_st, ss->ba5_sh,
		    SVWSATA_SIM, 0);
		break;
	}

	for (channel = 0; channel < sc->sc_wdcdev.nchannels; channel++) {
		cp = &sc->pciide_channels[channel];
		if (pciide_chansetup(sc, channel, 0) == 0)
			continue;
		svwsata_mapchan(cp);
		sata_setup_channel(&cp->wdc_channel);
	}
}

void
svwsata_mapreg_dma(struct pciide_softc *sc, struct pci_attach_args *pa)
{
	struct pciide_svwsata *ss = sc->sc_cookie;

	sc->sc_wdcdev.dma_arg = sc;
	sc->sc_wdcdev.dma_init = pciide_dma_init;
	sc->sc_wdcdev.dma_start = pciide_dma_start;
	sc->sc_wdcdev.dma_finish = pciide_dma_finish;

	/* XXX */
	sc->sc_dma_iot = ss->ba5_st;
	sc->sc_dma_ioh = ss->ba5_sh;

	sc->sc_dmacmd_read = svwsata_dmacmd_read;
	sc->sc_dmacmd_write = svwsata_dmacmd_write;
	sc->sc_dmactl_read = svwsata_dmactl_read;
	sc->sc_dmactl_write = svwsata_dmactl_write;
	sc->sc_dmatbl_write = svwsata_dmatbl_write;

	/* DMA registers all set up! */
	sc->sc_dmat = pa->pa_dmat;
	sc->sc_dma_ok = 1;
}

u_int8_t
svwsata_dmacmd_read(struct pciide_softc *sc, int chan)
{
	return (bus_space_read_1(sc->sc_dma_iot, sc->sc_dma_ioh,
	    (chan << 8) + SVWSATA_DMA + IDEDMA_CMD(0)));
}

void
svwsata_dmacmd_write(struct pciide_softc *sc, int chan, u_int8_t val)
{
	bus_space_write_1(sc->sc_dma_iot, sc->sc_dma_ioh,
	    (chan << 8) + SVWSATA_DMA + IDEDMA_CMD(0), val);
}

u_int8_t
svwsata_dmactl_read(struct pciide_softc *sc, int chan)
{
	return (bus_space_read_1(sc->sc_dma_iot, sc->sc_dma_ioh,
	    (chan << 8) + SVWSATA_DMA + IDEDMA_CTL(0)));
}

void
svwsata_dmactl_write(struct pciide_softc *sc, int chan, u_int8_t val)
{
	bus_space_write_1(sc->sc_dma_iot, sc->sc_dma_ioh,
	    (chan << 8) + SVWSATA_DMA + IDEDMA_CTL(0), val);
}

void
svwsata_dmatbl_write(struct pciide_softc *sc, int chan, u_int32_t val)
{
	bus_space_write_4(sc->sc_dma_iot, sc->sc_dma_ioh,
	    (chan << 8) + SVWSATA_DMA + IDEDMA_TBL(0), val);
}

void
svwsata_mapchan(struct pciide_channel *cp)
{
	struct pciide_softc *sc = (struct pciide_softc *)cp->wdc_channel.wdc;
	struct channel_softc *wdc_cp = &cp->wdc_channel;
	struct pciide_svwsata *ss = sc->sc_cookie;

	cp->compat = 0;
	cp->ih = sc->sc_pci_ih;

	if (bus_space_subregion(ss->ba5_st, ss->ba5_sh,
		(wdc_cp->channel << 8) + SVWSATA_TF0,
		SVWSATA_TF8 - SVWSATA_TF0, &wdc_cp->cmd_ioh) != 0) {
		printf("%s: couldn't map %s cmd regs\n",
		       sc->sc_wdcdev.sc_dev.dv_xname, cp->name);
		return;
	}
	if (bus_space_subregion(ss->ba5_st, ss->ba5_sh,
		(wdc_cp->channel << 8) + SVWSATA_TF8, 4,
		&wdc_cp->ctl_ioh) != 0) {
		printf("%s: couldn't map %s ctl regs\n",
		       sc->sc_wdcdev.sc_dev.dv_xname, cp->name);
		return;
	}
	wdc_cp->cmd_iot = wdc_cp->ctl_iot = ss->ba5_st;
	wdc_cp->_vtbl = &wdc_svwsata_vtbl;
	wdc_cp->ch_flags |= WDCF_DMA_BEFORE_CMD;
	wdcattach(wdc_cp);
}

void
svwsata_drv_probe(struct channel_softc *chp)
{
	struct pciide_channel *cp = (struct pciide_channel *)chp;
	struct pciide_softc *sc = (struct pciide_softc *)cp->wdc_channel.wdc;
	struct pciide_svwsata *ss = sc->sc_cookie;
	int channel = chp->channel;
	uint32_t scontrol, sstatus;
	uint8_t scnt, sn, cl, ch;
	int s;

	/*
	 * Request communication initialization sequence, any speed.
	 * Performing this is the equivalent of an ATA Reset.
	 */
	scontrol = SControl_DET_INIT | SControl_SPD_ANY;

	/*
	 * XXX We don't yet support SATA power management; disable all
	 * power management state transitions.
	 */
	scontrol |= SControl_IPM_NONE;

	bus_space_write_4(ss->ba5_st, ss->ba5_sh,
	    (channel << 8) + SVWSATA_SCONTROL, scontrol);
	delay(50 * 1000);
	scontrol &= ~SControl_DET_INIT;
	bus_space_write_4(ss->ba5_st, ss->ba5_sh,
	    (channel << 8) + SVWSATA_SCONTROL, scontrol);
	delay(100 * 1000);

	sstatus = bus_space_read_4(ss->ba5_st, ss->ba5_sh,
	    (channel << 8) + SVWSATA_SSTATUS);
#if 0
	printf("%s: port %d: SStatus=0x%08x, SControl=0x%08x\n",
	    sc->sc_wdcdev.sc_dev.dv_xname, chp->channel, sstatus,
	    bus_space_read_4(ss->ba5_st, ss->ba5_sh,
	        (channel << 8) + SVWSATA_SSTATUS));
#endif
	switch (sstatus & SStatus_DET_mask) {
	case SStatus_DET_NODEV:
		/* No device; be silent. */
		break;

	case SStatus_DET_DEV_NE:
		printf("%s: port %d: device connected, but "
		    "communication not established\n",
		    sc->sc_wdcdev.sc_dev.dv_xname, chp->channel);
		break;

	case SStatus_DET_OFFLINE:
		printf("%s: port %d: PHY offline\n",
		    sc->sc_wdcdev.sc_dev.dv_xname, chp->channel);
		break;

	case SStatus_DET_DEV:
		/*
		 * XXX ATAPI detection doesn't currently work.  Don't
		 * XXX know why.  But, it's not like the standard method
		 * XXX can detect an ATAPI device connected via a SATA/PATA
		 * XXX bridge, so at least this is no worse.  --thorpej
		 */
		if (chp->_vtbl != NULL)
			CHP_WRITE_REG(chp, wdr_sdh, WDSD_IBM | (0 << 4));
		else
			bus_space_write_1(chp->cmd_iot, chp->cmd_ioh,
			    wdr_sdh & _WDC_REGMASK, WDSD_IBM | (0 << 4));
		delay(10);	/* 400ns delay */
		/* Save register contents. */
		if (chp->_vtbl != NULL) {
			scnt = CHP_READ_REG(chp, wdr_seccnt);
			sn = CHP_READ_REG(chp, wdr_sector);
			cl = CHP_READ_REG(chp, wdr_cyl_lo);
			ch = CHP_READ_REG(chp, wdr_cyl_hi);
		} else {
			scnt = bus_space_read_1(chp->cmd_iot,
			    chp->cmd_ioh, wdr_seccnt & _WDC_REGMASK);
			sn = bus_space_read_1(chp->cmd_iot,
			    chp->cmd_ioh, wdr_sector & _WDC_REGMASK);
			cl = bus_space_read_1(chp->cmd_iot,
			    chp->cmd_ioh, wdr_cyl_lo & _WDC_REGMASK);
			ch = bus_space_read_1(chp->cmd_iot,
			    chp->cmd_ioh, wdr_cyl_hi & _WDC_REGMASK);
		}
#if 0
		printf("%s: port %d: scnt=0x%x sn=0x%x cl=0x%x ch=0x%x\n",
		    sc->sc_wdcdev.sc_dev.dv_xname, chp->channel,
		    scnt, sn, cl, ch);
#endif
		/*
		 * scnt and sn are supposed to be 0x1 for ATAPI, but in some
		 * cases we get wrong values here, so ignore it.
		 */
		s = splbio();
		if (cl == 0x14 && ch == 0xeb)
			chp->ch_drive[0].drive_flags |= DRIVE_ATAPI;
		else
			chp->ch_drive[0].drive_flags |= DRIVE_ATA;
		splx(s);

		printf("%s: port %d",
		    sc->sc_wdcdev.sc_dev.dv_xname, chp->channel);
		switch ((sstatus & SStatus_SPD_mask) >> SStatus_SPD_shift) {
		case 1:
			printf(": 1.5Gb/s");
			break;
		case 2:
			printf(": 3.0Gb/s");
			break;
		}
		printf("\n");
		break;

	default:
		printf("%s: port %d: unknown SStatus: 0x%08x\n",
		    sc->sc_wdcdev.sc_dev.dv_xname, chp->channel, sstatus);
	}
}

u_int8_t
svwsata_read_reg(struct channel_softc *chp, enum wdc_regs reg)
{
	if (reg & _WDC_AUX) {
		return (bus_space_read_4(chp->ctl_iot, chp->ctl_ioh,
		    (reg & _WDC_REGMASK) << 2));
	} else {
		return (bus_space_read_4(chp->cmd_iot, chp->cmd_ioh,
		    (reg & _WDC_REGMASK) << 2));
	}
}

void
svwsata_write_reg(struct channel_softc *chp, enum wdc_regs reg, u_int8_t val)
{
	if (reg & _WDC_AUX) {
		bus_space_write_4(chp->ctl_iot, chp->ctl_ioh,
		    (reg & _WDC_REGMASK) << 2, val);
	} else {
		bus_space_write_4(chp->cmd_iot, chp->cmd_ioh,
		    (reg & _WDC_REGMASK) << 2, val);
	}
}
 
void
svwsata_lba48_write_reg(struct channel_softc *chp, enum wdc_regs reg, u_int16_t val)
{
	if (reg & _WDC_AUX) {
		bus_space_write_4(chp->ctl_iot, chp->ctl_ioh,
		    (reg & _WDC_REGMASK) << 2, val);
	} else {
		bus_space_write_4(chp->cmd_iot, chp->cmd_ioh,
		    (reg & _WDC_REGMASK) << 2, val);
	}
}
 
#define	ACARD_IS_850(sc) \
	((sc)->sc_pp->ide_product == PCI_PRODUCT_ACARD_ATP850U)

void
acard_chip_map(struct pciide_softc *sc, struct pci_attach_args *pa)
{
	struct pciide_channel *cp;
	int i;
	pcireg_t interface;
	bus_size_t cmdsize, ctlsize;

	/*
	 * when the chip is in native mode it identifies itself as a
	 * 'misc mass storage'. Fake interface in this case.
	 */
	if (PCI_SUBCLASS(pa->pa_class) == PCI_SUBCLASS_MASS_STORAGE_IDE) {
		interface = PCI_INTERFACE(pa->pa_class);
	} else {
		interface = PCIIDE_INTERFACE_BUS_MASTER_DMA |
		    PCIIDE_INTERFACE_PCI(0) | PCIIDE_INTERFACE_PCI(1);
	}

	printf(": DMA");
	pciide_mapreg_dma(sc, pa);
	printf("\n");
	sc->sc_wdcdev.cap = WDC_CAPABILITY_DATA16 | WDC_CAPABILITY_DATA32 |
	    WDC_CAPABILITY_MODE;

	if (sc->sc_dma_ok) {
		sc->sc_wdcdev.cap |= WDC_CAPABILITY_DMA | WDC_CAPABILITY_UDMA;
		sc->sc_wdcdev.cap |= WDC_CAPABILITY_IRQACK;
		sc->sc_wdcdev.irqack = pciide_irqack;
	}
	sc->sc_wdcdev.PIO_cap = 4;
	sc->sc_wdcdev.DMA_cap = 2;
	switch (sc->sc_pp->ide_product) {
	case PCI_PRODUCT_ACARD_ATP850U:
		sc->sc_wdcdev.UDMA_cap = 2;
		break;
	case PCI_PRODUCT_ACARD_ATP860:
	case PCI_PRODUCT_ACARD_ATP860A:
		sc->sc_wdcdev.UDMA_cap = 4;
		break;
	case PCI_PRODUCT_ACARD_ATP865A:
	case PCI_PRODUCT_ACARD_ATP865R:
		sc->sc_wdcdev.UDMA_cap = 6;
		break;
	}

	sc->sc_wdcdev.set_modes = acard_setup_channel;
	sc->sc_wdcdev.channels = sc->wdc_chanarray;
	sc->sc_wdcdev.nchannels = 2;

	for (i = 0; i < sc->sc_wdcdev.nchannels; i++) {
		cp = &sc->pciide_channels[i];
		if (pciide_chansetup(sc, i, interface) == 0)
			continue;
		if (interface & PCIIDE_INTERFACE_PCI(i)) {
			cp->hw_ok = pciide_mapregs_native(pa, cp, &cmdsize,
			    &ctlsize, pciide_pci_intr);
		} else {
			cp->hw_ok = pciide_mapregs_compat(pa, cp, i,
			    &cmdsize, &ctlsize);
		}
		if (cp->hw_ok == 0)
			return;
		cp->wdc_channel.data32iot = cp->wdc_channel.cmd_iot;
		cp->wdc_channel.data32ioh = cp->wdc_channel.cmd_ioh;
		wdcattach(&cp->wdc_channel);
		acard_setup_channel(&cp->wdc_channel);
	}
	if (!ACARD_IS_850(sc)) {
		u_int32_t reg;
		reg = pci_conf_read(sc->sc_pc, sc->sc_tag, ATP8x0_CTRL);
		reg &= ~ATP860_CTRL_INT;
		pci_conf_write(sc->sc_pc, sc->sc_tag, ATP8x0_CTRL, reg);
	}
}

void
acard_setup_channel(struct channel_softc *chp)
{
	struct ata_drive_datas *drvp;
	struct pciide_channel *cp = (struct pciide_channel *)chp;
	struct pciide_softc *sc = (struct pciide_softc *)cp->wdc_channel.wdc;
	int channel = chp->channel;
	int drive;
	u_int32_t idetime, udma_mode;
	u_int32_t idedma_ctl;

	/* setup DMA if needed */
	pciide_channel_dma_setup(cp);

	if (ACARD_IS_850(sc)) {
		idetime = 0;
		udma_mode = pci_conf_read(sc->sc_pc, sc->sc_tag, ATP850_UDMA);
		udma_mode &= ~ATP850_UDMA_MASK(channel);
	} else {
		idetime = pci_conf_read(sc->sc_pc, sc->sc_tag, ATP860_IDETIME);
		idetime &= ~ATP860_SETTIME_MASK(channel);
		udma_mode = pci_conf_read(sc->sc_pc, sc->sc_tag, ATP860_UDMA);
		udma_mode &= ~ATP860_UDMA_MASK(channel);
	}

	idedma_ctl = 0;

	/* Per drive settings */
	for (drive = 0; drive < 2; drive++) {
		drvp = &chp->ch_drive[drive];
		/* If no drive, skip */
		if ((drvp->drive_flags & DRIVE) == 0)
			continue;
		/* add timing values, setup DMA if needed */
		if ((chp->wdc->cap & WDC_CAPABILITY_UDMA) &&
		    (drvp->drive_flags & DRIVE_UDMA)) {
			/* use Ultra/DMA */
			if (ACARD_IS_850(sc)) {
				idetime |= ATP850_SETTIME(drive,
				    acard_act_udma[drvp->UDMA_mode],
				    acard_rec_udma[drvp->UDMA_mode]);
				udma_mode |= ATP850_UDMA_MODE(channel, drive,
				    acard_udma_conf[drvp->UDMA_mode]);
			} else {
				idetime |= ATP860_SETTIME(channel, drive,
				    acard_act_udma[drvp->UDMA_mode],
				    acard_rec_udma[drvp->UDMA_mode]);
				udma_mode |= ATP860_UDMA_MODE(channel, drive,
				    acard_udma_conf[drvp->UDMA_mode]);
			}
			idedma_ctl |= IDEDMA_CTL_DRV_DMA(drive);
		} else if ((chp->wdc->cap & WDC_CAPABILITY_DMA) &&
		    (drvp->drive_flags & DRIVE_DMA)) {
			/* use Multiword DMA */
			drvp->drive_flags &= ~DRIVE_UDMA;
			if (ACARD_IS_850(sc)) {
				idetime |= ATP850_SETTIME(drive,
				    acard_act_dma[drvp->DMA_mode],
				    acard_rec_dma[drvp->DMA_mode]);
			} else {
				idetime |= ATP860_SETTIME(channel, drive,
				    acard_act_dma[drvp->DMA_mode],
				    acard_rec_dma[drvp->DMA_mode]);
			}
			idedma_ctl |= IDEDMA_CTL_DRV_DMA(drive);
		} else {
			/* PIO only */
			drvp->drive_flags &= ~(DRIVE_UDMA | DRIVE_DMA);
			if (ACARD_IS_850(sc)) {
				idetime |= ATP850_SETTIME(drive,
				    acard_act_pio[drvp->PIO_mode],
				    acard_rec_pio[drvp->PIO_mode]);
			} else {
				idetime |= ATP860_SETTIME(channel, drive,
				    acard_act_pio[drvp->PIO_mode],
				    acard_rec_pio[drvp->PIO_mode]);
			}
			pci_conf_write(sc->sc_pc, sc->sc_tag, ATP8x0_CTRL,
			    pci_conf_read(sc->sc_pc, sc->sc_tag, ATP8x0_CTRL) |
			    ATP8x0_CTRL_EN(channel));
		}
	}

	if (idedma_ctl != 0) {
		/* Add software bits in status register */
		bus_space_write_1(sc->sc_dma_iot, sc->sc_dma_ioh,
		    IDEDMA_CTL(channel), idedma_ctl);
	}
	pciide_print_modes(cp);

	if (ACARD_IS_850(sc)) {
		pci_conf_write(sc->sc_pc, sc->sc_tag,
		    ATP850_IDETIME(channel), idetime);
		pci_conf_write(sc->sc_pc, sc->sc_tag, ATP850_UDMA, udma_mode);
	} else {
		pci_conf_write(sc->sc_pc, sc->sc_tag, ATP860_IDETIME, idetime);
		pci_conf_write(sc->sc_pc, sc->sc_tag, ATP860_UDMA, udma_mode);
	}
}

void
nforce_chip_map(struct pciide_softc *sc, struct pci_attach_args *pa)
{
	struct pciide_channel *cp;
	int channel;
	pcireg_t interface = PCI_INTERFACE(pa->pa_class);
	bus_size_t cmdsize, ctlsize;
	u_int32_t conf;

	conf = pci_conf_read(sc->sc_pc, sc->sc_tag, NFORCE_CONF);
	WDCDEBUG_PRINT(("%s: conf register 0x%x\n",
	    sc->sc_wdcdev.sc_dev.dv_xname, conf), DEBUG_PROBE);

	printf(": DMA");
	pciide_mapreg_dma(sc, pa);

	sc->sc_wdcdev.cap = WDC_CAPABILITY_DATA16 | WDC_CAPABILITY_DATA32 |
	    WDC_CAPABILITY_MODE;
	if (sc->sc_dma_ok) {
		sc->sc_wdcdev.cap |= WDC_CAPABILITY_DMA | WDC_CAPABILITY_UDMA;
		sc->sc_wdcdev.cap |= WDC_CAPABILITY_IRQACK;
		sc->sc_wdcdev.irqack = pciide_irqack;
	}
	sc->sc_wdcdev.PIO_cap = 4;
	sc->sc_wdcdev.DMA_cap = 2;
	switch (sc->sc_pp->ide_product) {
	case PCI_PRODUCT_NVIDIA_NFORCE_IDE:
		sc->sc_wdcdev.UDMA_cap = 5;
		break;
	default:
		sc->sc_wdcdev.UDMA_cap = 6;
	}
	sc->sc_wdcdev.set_modes = nforce_setup_channel;
	sc->sc_wdcdev.channels = sc->wdc_chanarray;
	sc->sc_wdcdev.nchannels = PCIIDE_NUM_CHANNELS;

	pciide_print_channels(sc->sc_wdcdev.nchannels, interface);

	for (channel = 0; channel < sc->sc_wdcdev.nchannels; channel++) {
		cp = &sc->pciide_channels[channel];

		if (pciide_chansetup(sc, channel, interface) == 0)
			continue;

		if ((conf & NFORCE_CHAN_EN(channel)) == 0) {
			printf("%s: %s ignored (disabled)\n",
			    sc->sc_wdcdev.sc_dev.dv_xname, cp->name);
			cp->hw_ok = 0;
			continue;
		}

		pciide_map_compat_intr(pa, cp, channel, interface);
		if (cp->hw_ok == 0)
			continue;
		pciide_mapchan(pa, cp, interface, &cmdsize, &ctlsize,
		    nforce_pci_intr);
		if (cp->hw_ok == 0) {
			pciide_unmap_compat_intr(pa, cp, channel, interface);
			continue;
		}

		if (pciide_chan_candisable(cp)) {
			conf &= ~NFORCE_CHAN_EN(channel);
			pciide_unmap_compat_intr(pa, cp, channel, interface);
			continue;
		}

		sc->sc_wdcdev.set_modes(&cp->wdc_channel);
	}
	WDCDEBUG_PRINT(("%s: new conf register 0x%x\n",
	    sc->sc_wdcdev.sc_dev.dv_xname, conf), DEBUG_PROBE);
	pci_conf_write(sc->sc_pc, sc->sc_tag, NFORCE_CONF, conf);
}

void
nforce_setup_channel(struct channel_softc *chp)
{
	struct ata_drive_datas *drvp;
	int drive, mode;
	u_int32_t idedma_ctl;
	struct pciide_channel *cp = (struct pciide_channel *)chp;
	struct pciide_softc *sc = (struct pciide_softc *)cp->wdc_channel.wdc;
	int channel = chp->channel;
	u_int32_t conf, piodmatim, piotim, udmatim;

	conf = pci_conf_read(sc->sc_pc, sc->sc_tag, NFORCE_CONF);
	piodmatim = pci_conf_read(sc->sc_pc, sc->sc_tag, NFORCE_PIODMATIM);
	piotim = pci_conf_read(sc->sc_pc, sc->sc_tag, NFORCE_PIOTIM);
	udmatim = pci_conf_read(sc->sc_pc, sc->sc_tag, NFORCE_UDMATIM);
	WDCDEBUG_PRINT(("%s: %s old timing values: piodmatim=0x%x, "
	    "piotim=0x%x, udmatim=0x%x\n", sc->sc_wdcdev.sc_dev.dv_xname,
	    cp->name, piodmatim, piotim, udmatim), DEBUG_PROBE);

	/* Setup DMA if needed */
	pciide_channel_dma_setup(cp);

	/* Clear all bits for this channel */
	idedma_ctl = 0;
	piodmatim &= ~NFORCE_PIODMATIM_MASK(channel);
	udmatim &= ~NFORCE_UDMATIM_MASK(channel);

	/* Per channel settings */
	for (drive = 0; drive < 2; drive++) {
		drvp = &chp->ch_drive[drive];

		/* If no drive, skip */
		if ((drvp->drive_flags & DRIVE) == 0)
			continue;

		if ((chp->wdc->cap & WDC_CAPABILITY_UDMA) != 0 &&
		    (drvp->drive_flags & DRIVE_UDMA) != 0) {
			/* Setup UltraDMA mode */
			drvp->drive_flags &= ~DRIVE_DMA;

			udmatim |= NFORCE_UDMATIM_SET(channel, drive,
			    nforce_udma[drvp->UDMA_mode]) |
			    NFORCE_UDMA_EN(channel, drive) |
			    NFORCE_UDMA_ENM(channel, drive);

			mode = drvp->PIO_mode;
		} else if ((chp->wdc->cap & WDC_CAPABILITY_DMA) != 0 &&
		    (drvp->drive_flags & DRIVE_DMA) != 0) {
			/* Setup multiword DMA mode */
			drvp->drive_flags &= ~DRIVE_UDMA;

			/* mode = min(pio, dma + 2) */
			if (drvp->PIO_mode <= (drvp->DMA_mode + 2))
				mode = drvp->PIO_mode;
			else
				mode = drvp->DMA_mode + 2;
		} else {
			mode = drvp->PIO_mode;
			goto pio;
		}
		idedma_ctl |= IDEDMA_CTL_DRV_DMA(drive);

pio:
		/* Setup PIO mode */
		if (mode <= 2) {
			drvp->DMA_mode = 0;
			drvp->PIO_mode = 0;
			mode = 0;
		} else {
			drvp->PIO_mode = mode;
			drvp->DMA_mode = mode - 2;
		}
		piodmatim |= NFORCE_PIODMATIM_SET(channel, drive,
		    nforce_pio[mode]);
	}

	if (idedma_ctl != 0) {
		/* Add software bits in status register */
		bus_space_write_1(sc->sc_dma_iot, sc->sc_dma_ioh,
		    IDEDMA_CTL(channel), idedma_ctl);
	}

	WDCDEBUG_PRINT(("%s: %s new timing values: piodmatim=0x%x, "
	    "piotim=0x%x, udmatim=0x%x\n", sc->sc_wdcdev.sc_dev.dv_xname,
	    cp->name, piodmatim, piotim, udmatim), DEBUG_PROBE);
	pci_conf_write(sc->sc_pc, sc->sc_tag, NFORCE_PIODMATIM, piodmatim);
	pci_conf_write(sc->sc_pc, sc->sc_tag, NFORCE_UDMATIM, udmatim);

	pciide_print_modes(cp);
}

int
nforce_pci_intr(void *arg)
{
	struct pciide_softc *sc = arg;
	struct pciide_channel *cp;
	struct channel_softc *wdc_cp;
	int i, rv, crv;
	u_int32_t dmastat;

	rv = 0;
	for (i = 0; i < sc->sc_wdcdev.nchannels; i++) {
		cp = &sc->pciide_channels[i];
		wdc_cp = &cp->wdc_channel;

		/* Skip compat channel */
		if (cp->compat)
			continue;

		dmastat = bus_space_read_1(sc->sc_dma_iot, sc->sc_dma_ioh,
		    IDEDMA_CTL(i));
		if ((dmastat & IDEDMA_CTL_INTR) == 0)
			continue;

		crv = wdcintr(wdc_cp);
		if (crv == 0)
			printf("%s:%d: bogus intr\n",
			    sc->sc_wdcdev.sc_dev.dv_xname, i);
		else
			rv = 1;
	}
	return (rv);
}

void
artisea_chip_map(struct pciide_softc *sc, struct pci_attach_args *pa)
{
	struct pciide_channel *cp;
	bus_size_t cmdsize, ctlsize;
	pcireg_t interface;
	int channel;

	printf(": DMA");
#ifdef PCIIDE_I31244_DISABLEDMA
	if (sc->sc_rev == 0) {
		printf(" disabled due to rev. 0");
		sc->sc_dma_ok = 0;
	} else
#endif
		pciide_mapreg_dma(sc, pa);
	printf("\n");

	/*
	 * XXX Configure LEDs to show activity.
	 */

	sc->sc_wdcdev.cap |= WDC_CAPABILITY_DATA16 | WDC_CAPABILITY_DATA32 |
	    WDC_CAPABILITY_MODE | WDC_CAPABILITY_SATA;
	sc->sc_wdcdev.PIO_cap = 4;
	if (sc->sc_dma_ok) {
		sc->sc_wdcdev.cap |= WDC_CAPABILITY_DMA | WDC_CAPABILITY_UDMA;
		sc->sc_wdcdev.cap |= WDC_CAPABILITY_IRQACK;
		sc->sc_wdcdev.irqack = pciide_irqack;
		sc->sc_wdcdev.DMA_cap = 2;
		sc->sc_wdcdev.UDMA_cap = 6;
	}
	sc->sc_wdcdev.set_modes = sata_setup_channel;

	sc->sc_wdcdev.channels = sc->wdc_chanarray;
	sc->sc_wdcdev.nchannels = PCIIDE_NUM_CHANNELS;

	interface = PCI_INTERFACE(pa->pa_class);

	for (channel = 0; channel < sc->sc_wdcdev.nchannels; channel++) {
		cp = &sc->pciide_channels[channel];
		if (pciide_chansetup(sc, channel, interface) == 0)
			continue;
		pciide_mapchan(pa, cp, interface, &cmdsize, &ctlsize,
		    pciide_pci_intr);
		if (cp->hw_ok == 0)
			continue;
		pciide_map_compat_intr(pa, cp, channel, interface);
		sata_setup_channel(&cp->wdc_channel);
	}
}

void
ite_chip_map(struct pciide_softc *sc, struct pci_attach_args *pa)
{
	struct pciide_channel *cp;
	int channel;
	pcireg_t interface;
	bus_size_t cmdsize, ctlsize;
	pcireg_t cfg, modectl;

	/*
	 * Fake interface since IT8212F is claimed to be a ``RAID'' device.
	 */
	interface = PCIIDE_INTERFACE_BUS_MASTER_DMA |
	    PCIIDE_INTERFACE_PCI(0) | PCIIDE_INTERFACE_PCI(1);

	cfg = pci_conf_read(sc->sc_pc, sc->sc_tag, IT_CFG);
	modectl = pci_conf_read(sc->sc_pc, sc->sc_tag, IT_MODE);
	WDCDEBUG_PRINT(("%s: cfg=0x%x, modectl=0x%x\n",
	    sc->sc_wdcdev.sc_dev.dv_xname, cfg & IT_CFG_MASK,
	    modectl & IT_MODE_MASK), DEBUG_PROBE);

	printf(": DMA");
	pciide_mapreg_dma(sc, pa);

	sc->sc_wdcdev.cap = WDC_CAPABILITY_DATA16 | WDC_CAPABILITY_DATA32 |
	    WDC_CAPABILITY_MODE;
	if (sc->sc_dma_ok) {
		sc->sc_wdcdev.cap |= WDC_CAPABILITY_DMA | WDC_CAPABILITY_UDMA;
		sc->sc_wdcdev.cap |= WDC_CAPABILITY_IRQACK;
		sc->sc_wdcdev.irqack = pciide_irqack;
	}
	sc->sc_wdcdev.PIO_cap = 4;
	sc->sc_wdcdev.DMA_cap = 2;
	sc->sc_wdcdev.UDMA_cap = 6;

	sc->sc_wdcdev.set_modes = ite_setup_channel;
	sc->sc_wdcdev.channels = sc->wdc_chanarray;
	sc->sc_wdcdev.nchannels = PCIIDE_NUM_CHANNELS;

	pciide_print_channels(sc->sc_wdcdev.nchannels, interface);

	/* Disable RAID */
	modectl &= ~IT_MODE_RAID1;
	/* Disable CPU firmware mode */
	modectl &= ~IT_MODE_CPU;

	pci_conf_write(sc->sc_pc, sc->sc_tag, IT_MODE, modectl);

	for (channel = 0; channel < sc->sc_wdcdev.nchannels; channel++) {
		cp = &sc->pciide_channels[channel];

		if (pciide_chansetup(sc, channel, interface) == 0)
			continue;
		pciide_mapchan(pa, cp, interface, &cmdsize, &ctlsize,
		    pciide_pci_intr);
		sc->sc_wdcdev.set_modes(&cp->wdc_channel);
	}

	/* Re-read configuration registers after channels setup */
	cfg = pci_conf_read(sc->sc_pc, sc->sc_tag, IT_CFG);
	modectl = pci_conf_read(sc->sc_pc, sc->sc_tag, IT_MODE);
	WDCDEBUG_PRINT(("%s: cfg=0x%x, modectl=0x%x\n",
	    sc->sc_wdcdev.sc_dev.dv_xname, cfg & IT_CFG_MASK,
	    modectl & IT_MODE_MASK), DEBUG_PROBE);
}

void
ite_setup_channel(struct channel_softc *chp)
{
	struct ata_drive_datas *drvp;
	int drive, mode;
	u_int32_t idedma_ctl;
	struct pciide_channel *cp = (struct pciide_channel *)chp;
	struct pciide_softc *sc = (struct pciide_softc *)cp->wdc_channel.wdc;
	int channel = chp->channel;
	pcireg_t cfg, modectl;
	pcireg_t tim;

	cfg = pci_conf_read(sc->sc_pc, sc->sc_tag, IT_CFG);
	modectl = pci_conf_read(sc->sc_pc, sc->sc_tag, IT_MODE);
	tim = pci_conf_read(sc->sc_pc, sc->sc_tag, IT_TIM(channel));
	WDCDEBUG_PRINT(("%s:%d: tim=0x%x\n", sc->sc_wdcdev.sc_dev.dv_xname,
	    channel, tim), DEBUG_PROBE);

	/* Setup DMA if needed */
	pciide_channel_dma_setup(cp);

	/* Clear all bits for this channel */
	idedma_ctl = 0;

	/* Per channel settings */
	for (drive = 0; drive < 2; drive++) {
		drvp = &chp->ch_drive[drive];

		/* If no drive, skip */
		if ((drvp->drive_flags & DRIVE) == 0)
			continue;

		if ((chp->wdc->cap & WDC_CAPABILITY_UDMA) != 0 &&
		    (drvp->drive_flags & DRIVE_UDMA) != 0) {
			/* Setup UltraDMA mode */
			drvp->drive_flags &= ~DRIVE_DMA;
			modectl &= ~IT_MODE_DMA(channel, drive);

#if 0
			/* Check cable, works only in CPU firmware mode */
			if (drvp->UDMA_mode > 2 &&
			    (cfg & IT_CFG_CABLE(channel, drive)) == 0) {
				WDCDEBUG_PRINT(("%s(%s:%d:%d): "
				    "80-wire cable not detected\n",
				    drvp->drive_name,
				    sc->sc_wdcdev.sc_dev.dv_xname,
				    channel, drive), DEBUG_PROBE);
				drvp->UDMA_mode = 2;
			}
#endif

			if (drvp->UDMA_mode >= 5)
				tim |= IT_TIM_UDMA5(drive);
			else
				tim &= ~IT_TIM_UDMA5(drive);

			mode = drvp->PIO_mode;
		} else if ((chp->wdc->cap & WDC_CAPABILITY_DMA) != 0 &&
		    (drvp->drive_flags & DRIVE_DMA) != 0) {
			/* Setup multiword DMA mode */
			drvp->drive_flags &= ~DRIVE_UDMA;
			modectl |= IT_MODE_DMA(channel, drive);

			/* mode = min(pio, dma + 2) */
			if (drvp->PIO_mode <= (drvp->DMA_mode + 2))
				mode = drvp->PIO_mode;
			else
				mode = drvp->DMA_mode + 2;
		} else {
			mode = drvp->PIO_mode;
			goto pio;
		}
		idedma_ctl |= IDEDMA_CTL_DRV_DMA(drive);

pio:
		/* Setup PIO mode */
		if (mode <= 2) {
			drvp->DMA_mode = 0;
			drvp->PIO_mode = 0;
			mode = 0;
		} else {
			drvp->PIO_mode = mode;
			drvp->DMA_mode = mode - 2;
		}

		/* Enable IORDY if PIO mode >= 3 */
		if (drvp->PIO_mode >= 3)
			cfg |= IT_CFG_IORDY(channel);
	}

	WDCDEBUG_PRINT(("%s: tim=0x%x\n", sc->sc_wdcdev.sc_dev.dv_xname,
	    tim), DEBUG_PROBE);

	pci_conf_write(sc->sc_pc, sc->sc_tag, IT_CFG, cfg);
	pci_conf_write(sc->sc_pc, sc->sc_tag, IT_MODE, modectl);
	pci_conf_write(sc->sc_pc, sc->sc_tag, IT_TIM(channel), tim);

	if (idedma_ctl != 0) {
		/* Add software bits in status register */
		bus_space_write_1(sc->sc_dma_iot, sc->sc_dma_ioh,
		    IDEDMA_CTL(channel), idedma_ctl);
	}

	pciide_print_modes(cp);
}

void
ixp_chip_map(struct pciide_softc *sc, struct pci_attach_args *pa)
{
	struct pciide_channel *cp;
	int channel;
	pcireg_t interface = PCI_INTERFACE(pa->pa_class);
	bus_size_t cmdsize, ctlsize;

	printf(": DMA");
	pciide_mapreg_dma(sc, pa);

	sc->sc_wdcdev.cap = WDC_CAPABILITY_DATA16 | WDC_CAPABILITY_DATA32 |
	    WDC_CAPABILITY_MODE;
	if (sc->sc_dma_ok) {
		sc->sc_wdcdev.cap |= WDC_CAPABILITY_DMA | WDC_CAPABILITY_UDMA;
		sc->sc_wdcdev.cap |= WDC_CAPABILITY_IRQACK;
		sc->sc_wdcdev.irqack = pciide_irqack;
	}
	sc->sc_wdcdev.PIO_cap = 4;
	sc->sc_wdcdev.DMA_cap = 2;
	sc->sc_wdcdev.UDMA_cap = 6;

	sc->sc_wdcdev.set_modes = ixp_setup_channel;
	sc->sc_wdcdev.channels = sc->wdc_chanarray;
	sc->sc_wdcdev.nchannels = PCIIDE_NUM_CHANNELS;

	pciide_print_channels(sc->sc_wdcdev.nchannels, interface);

	for (channel = 0; channel < sc->sc_wdcdev.nchannels; channel++) {
		cp = &sc->pciide_channels[channel];
		if (pciide_chansetup(sc, channel, interface) == 0)
			continue;
		pciide_map_compat_intr(pa, cp, channel, interface);
		if (cp->hw_ok == 0)
			continue;
		pciide_mapchan(pa, cp, interface, &cmdsize, &ctlsize,
		    pciide_pci_intr);
		if (cp->hw_ok == 0) {
			pciide_unmap_compat_intr(pa, cp, channel, interface);
			continue;
		}
		sc->sc_wdcdev.set_modes(&cp->wdc_channel);
	}
}

void
ixp_setup_channel(struct channel_softc *chp)
{
	struct ata_drive_datas *drvp;
	int drive, mode;
	u_int32_t idedma_ctl;
	struct pciide_channel *cp = (struct pciide_channel*)chp;
	struct pciide_softc *sc = (struct pciide_softc *)cp->wdc_channel.wdc;
	int channel = chp->channel;
	pcireg_t udma, mdma_timing, pio, pio_timing;

	pio_timing = pci_conf_read(sc->sc_pc, sc->sc_tag, IXP_PIO_TIMING);
	pio = pci_conf_read(sc->sc_pc, sc->sc_tag, IXP_PIO_CTL);
	mdma_timing = pci_conf_read(sc->sc_pc, sc->sc_tag, IXP_MDMA_TIMING);
	udma = pci_conf_read(sc->sc_pc, sc->sc_tag, IXP_UDMA_CTL);

	/* Setup DMA if needed */
	pciide_channel_dma_setup(cp);

	idedma_ctl = 0;

	/* Per channel settings */
	for (drive = 0; drive < 2; drive++) {
		drvp = &chp->ch_drive[drive];

		/* If no drive, skip */
		if ((drvp->drive_flags & DRIVE) == 0)
			continue;
		if ((chp->wdc->cap & WDC_CAPABILITY_UDMA) != 0 &&
		    (drvp->drive_flags & DRIVE_UDMA) != 0) {
			/* Setup UltraDMA mode */
			idedma_ctl |= IDEDMA_CTL_DRV_DMA(drive);
			IXP_UDMA_ENABLE(udma, chp->channel, drive);
			IXP_SET_MODE(udma, chp->channel, drive,
			    drvp->UDMA_mode);
			mode = drvp->PIO_mode;
		} else if ((chp->wdc->cap & WDC_CAPABILITY_DMA) != 0 &&
		    (drvp->drive_flags & DRIVE_DMA) != 0) {
			/* Setup multiword DMA mode */
			drvp->drive_flags &= ~DRIVE_UDMA;
			idedma_ctl |= IDEDMA_CTL_DRV_DMA(drive);
			IXP_UDMA_DISABLE(udma, chp->channel, drive);
			IXP_SET_TIMING(mdma_timing, chp->channel, drive,
			    ixp_mdma_timings[drvp->DMA_mode]);

			/* mode = min(pio, dma + 2) */
			if (drvp->PIO_mode <= (drvp->DMA_mode + 2))
				mode = drvp->PIO_mode;
			else
				mode = drvp->DMA_mode + 2;
		} else {
			mode = drvp->PIO_mode;
		}

		/* Setup PIO mode */
		drvp->PIO_mode = mode;
		if (mode < 2)
			drvp->DMA_mode = 0;
		else
			drvp->DMA_mode = mode - 2;
		/*
		 * Set PIO mode and timings
		 * Linux driver avoids PIO mode 1, let's do it too.
		 */
		if (drvp->PIO_mode == 1)
			drvp->PIO_mode = 0;

		IXP_SET_MODE(pio, chp->channel, drive, drvp->PIO_mode);
		IXP_SET_TIMING(pio_timing, chp->channel, drive,
		    ixp_pio_timings[drvp->PIO_mode]);
	}

	pci_conf_write(sc->sc_pc, sc->sc_tag, IXP_UDMA_CTL, udma);
	pci_conf_write(sc->sc_pc, sc->sc_tag, IXP_MDMA_TIMING, mdma_timing);
	pci_conf_write(sc->sc_pc, sc->sc_tag, IXP_PIO_CTL, pio);
	pci_conf_write(sc->sc_pc, sc->sc_tag, IXP_PIO_TIMING, pio_timing);

	if (idedma_ctl != 0) {
		/* Add software bits in status register */
		bus_space_write_1(sc->sc_dma_iot, sc->sc_dma_ioh,
		    IDEDMA_CTL(channel), idedma_ctl);
	}

	pciide_print_modes(cp);
}

void
jmicron_chip_map(struct pciide_softc *sc, struct pci_attach_args *pa)
{
	struct pciide_channel *cp;
	int channel;
	pcireg_t interface = PCI_INTERFACE(pa->pa_class);
	bus_size_t cmdsize, ctlsize;
	u_int32_t conf;

	conf = pci_conf_read(sc->sc_pc, sc->sc_tag, JMICRON_CONF);
	WDCDEBUG_PRINT(("%s: conf register 0x%x\n",
	    sc->sc_wdcdev.sc_dev.dv_xname, conf), DEBUG_PROBE);

	printf(": DMA");
	pciide_mapreg_dma(sc, pa);

	sc->sc_wdcdev.cap = WDC_CAPABILITY_DATA16 | WDC_CAPABILITY_DATA32 |
	    WDC_CAPABILITY_MODE;
	if (sc->sc_dma_ok) {
		sc->sc_wdcdev.cap |= WDC_CAPABILITY_DMA | WDC_CAPABILITY_UDMA;
		sc->sc_wdcdev.cap |= WDC_CAPABILITY_IRQACK;
		sc->sc_wdcdev.irqack = pciide_irqack;
	}
	sc->sc_wdcdev.PIO_cap = 4;
	sc->sc_wdcdev.DMA_cap = 2;
	sc->sc_wdcdev.UDMA_cap = 6;
	sc->sc_wdcdev.set_modes = jmicron_setup_channel;
	sc->sc_wdcdev.channels = sc->wdc_chanarray;
	sc->sc_wdcdev.nchannels = PCIIDE_NUM_CHANNELS;

	pciide_print_channels(sc->sc_wdcdev.nchannels, interface);

	for (channel = 0; channel < sc->sc_wdcdev.nchannels; channel++) {
		cp = &sc->pciide_channels[channel];

		if (pciide_chansetup(sc, channel, interface) == 0)
			continue;

#if 0
		if ((conf & JMICRON_CHAN_EN(channel)) == 0) {
			printf("%s: %s ignored (disabled)\n",
			    sc->sc_wdcdev.sc_dev.dv_xname, cp->name);
			cp->hw_ok = 0;
			continue;
		}
#endif

		pciide_map_compat_intr(pa, cp, channel, interface);
		if (cp->hw_ok == 0)
			continue;
		pciide_mapchan(pa, cp, interface, &cmdsize, &ctlsize,
		    pciide_pci_intr);
		if (cp->hw_ok == 0) {
			pciide_unmap_compat_intr(pa, cp, channel, interface);
			continue;
		}

		if (pciide_chan_candisable(cp)) {
			conf &= ~JMICRON_CHAN_EN(channel);
			pciide_unmap_compat_intr(pa, cp, channel, interface);
			continue;
		}

		sc->sc_wdcdev.set_modes(&cp->wdc_channel);
	}
	WDCDEBUG_PRINT(("%s: new conf register 0x%x\n",
	    sc->sc_wdcdev.sc_dev.dv_xname, conf), DEBUG_PROBE);
	pci_conf_write(sc->sc_pc, sc->sc_tag, JMICRON_CONF, conf);
}

void
jmicron_setup_channel(struct channel_softc *chp)
{
	struct ata_drive_datas *drvp;
	int drive, mode;
	u_int32_t idedma_ctl;
	struct pciide_channel *cp = (struct pciide_channel *)chp;
	struct pciide_softc *sc = (struct pciide_softc *)cp->wdc_channel.wdc;
	int channel = chp->channel;
	u_int32_t conf;

	conf = pci_conf_read(sc->sc_pc, sc->sc_tag, JMICRON_CONF);

	/* Setup DMA if needed */
	pciide_channel_dma_setup(cp);

	/* Clear all bits for this channel */
	idedma_ctl = 0;

	/* Per channel settings */
	for (drive = 0; drive < 2; drive++) {
		drvp = &chp->ch_drive[drive];

		/* If no drive, skip */
		if ((drvp->drive_flags & DRIVE) == 0)
			continue;

		if ((chp->wdc->cap & WDC_CAPABILITY_UDMA) != 0 &&
		    (drvp->drive_flags & DRIVE_UDMA) != 0) {
			/* Setup UltraDMA mode */
			drvp->drive_flags &= ~DRIVE_DMA;

			/* see if cable is up to scratch */
			if ((conf & JMICRON_CONF_40PIN) &&
			    (drvp->UDMA_mode > 2))
				drvp->UDMA_mode = 2;

			mode = drvp->PIO_mode;
		} else if ((chp->wdc->cap & WDC_CAPABILITY_DMA) != 0 &&
		    (drvp->drive_flags & DRIVE_DMA) != 0) {
			/* Setup multiword DMA mode */
			drvp->drive_flags &= ~DRIVE_UDMA;

			/* mode = min(pio, dma + 2) */
			if (drvp->PIO_mode <= (drvp->DMA_mode + 2))
				mode = drvp->PIO_mode;
			else
				mode = drvp->DMA_mode + 2;
		} else {
			mode = drvp->PIO_mode;
			goto pio;
		}
		idedma_ctl |= IDEDMA_CTL_DRV_DMA(drive);

pio:
		/* Setup PIO mode */
		if (mode <= 2) {
			drvp->DMA_mode = 0;
			drvp->PIO_mode = 0;
		} else {
			drvp->PIO_mode = mode;
			drvp->DMA_mode = mode - 2;
		}
	}

	if (idedma_ctl != 0) {
		/* Add software bits in status register */
		bus_space_write_1(sc->sc_dma_iot, sc->sc_dma_ioh,
		    IDEDMA_CTL(channel), idedma_ctl);
	}

	pciide_print_modes(cp);
}

void
phison_chip_map(struct pciide_softc *sc, struct pci_attach_args *pa)
{
	struct pciide_channel *cp;
	int channel;
	pcireg_t interface = PCI_INTERFACE(pa->pa_class);
	bus_size_t cmdsize, ctlsize;

	sc->chip_unmap = default_chip_unmap;

	printf(": DMA");
	pciide_mapreg_dma(sc, pa);

	sc->sc_wdcdev.cap = WDC_CAPABILITY_DATA16 | WDC_CAPABILITY_DATA32 |
	    WDC_CAPABILITY_MODE;
	if (sc->sc_dma_ok) {
		sc->sc_wdcdev.cap |= WDC_CAPABILITY_DMA | WDC_CAPABILITY_UDMA;
		sc->sc_wdcdev.cap |= WDC_CAPABILITY_IRQACK;
		sc->sc_wdcdev.irqack = pciide_irqack;
	}
	sc->sc_wdcdev.PIO_cap = 4;
	sc->sc_wdcdev.DMA_cap = 2;
	sc->sc_wdcdev.UDMA_cap = 5;
	sc->sc_wdcdev.set_modes = phison_setup_channel;
	sc->sc_wdcdev.channels = sc->wdc_chanarray;
	sc->sc_wdcdev.nchannels = 1;

	pciide_print_channels(sc->sc_wdcdev.nchannels, interface);

	for (channel = 0; channel < sc->sc_wdcdev.nchannels; channel++) {
		cp = &sc->pciide_channels[channel];

		if (pciide_chansetup(sc, channel, interface) == 0)
			continue;

		pciide_map_compat_intr(pa, cp, channel, interface);
		if (cp->hw_ok == 0)
			continue;
		pciide_mapchan(pa, cp, interface, &cmdsize, &ctlsize,
		    pciide_pci_intr);
		if (cp->hw_ok == 0) {
			pciide_unmap_compat_intr(pa, cp, channel, interface);
			continue;
		}

		sc->sc_wdcdev.set_modes(&cp->wdc_channel);
	}
}

void
phison_setup_channel(struct channel_softc *chp)
{
	struct ata_drive_datas *drvp;
	int drive, mode;
	u_int32_t idedma_ctl;
	struct pciide_channel *cp = (struct pciide_channel *)chp;
	struct pciide_softc *sc = (struct pciide_softc *)cp->wdc_channel.wdc;
	int channel = chp->channel;

	/* Setup DMA if needed */
	pciide_channel_dma_setup(cp);

	/* Clear all bits for this channel */
	idedma_ctl = 0;

	/* Per channel settings */
	for (drive = 0; drive < 2; drive++) {
		drvp = &chp->ch_drive[drive];

		/* If no drive, skip */
		if ((drvp->drive_flags & DRIVE) == 0)
			continue;

		if ((chp->wdc->cap & WDC_CAPABILITY_UDMA) != 0 &&
		    (drvp->drive_flags & DRIVE_UDMA) != 0) {
			/* Setup UltraDMA mode */
			drvp->drive_flags &= ~DRIVE_DMA;
			mode = drvp->PIO_mode;
		} else if ((chp->wdc->cap & WDC_CAPABILITY_DMA) != 0 &&
		    (drvp->drive_flags & DRIVE_DMA) != 0) {
			/* Setup multiword DMA mode */
			drvp->drive_flags &= ~DRIVE_UDMA;

			/* mode = min(pio, dma + 2) */
			if (drvp->PIO_mode <= (drvp->DMA_mode + 2))
				mode = drvp->PIO_mode;
			else
				mode = drvp->DMA_mode + 2;
		} else {
			mode = drvp->PIO_mode;
			goto pio;
		}
		idedma_ctl |= IDEDMA_CTL_DRV_DMA(drive);

pio:
		/* Setup PIO mode */
		if (mode <= 2) {
			drvp->DMA_mode = 0;
			drvp->PIO_mode = 0;
		} else {
			drvp->PIO_mode = mode;
			drvp->DMA_mode = mode - 2;
		}
	}

	if (idedma_ctl != 0) {
		/* Add software bits in status register */
		bus_space_write_1(sc->sc_dma_iot, sc->sc_dma_ioh,
		    IDEDMA_CTL(channel), idedma_ctl);
	}

	pciide_print_modes(cp);
}

void
sch_chip_map(struct pciide_softc *sc, struct pci_attach_args *pa)
{
	struct pciide_channel *cp;
	int channel;
	pcireg_t interface = PCI_INTERFACE(pa->pa_class);
	bus_size_t cmdsize, ctlsize;

	printf(": DMA");
	pciide_mapreg_dma(sc, pa);

	sc->sc_wdcdev.cap = WDC_CAPABILITY_DATA16 | WDC_CAPABILITY_DATA32 |
	    WDC_CAPABILITY_MODE;
	if (sc->sc_dma_ok) {
		sc->sc_wdcdev.cap |= WDC_CAPABILITY_DMA | WDC_CAPABILITY_UDMA;
		sc->sc_wdcdev.cap |= WDC_CAPABILITY_IRQACK;
		sc->sc_wdcdev.irqack = pciide_irqack;
	}
	sc->sc_wdcdev.PIO_cap = 4;
	sc->sc_wdcdev.DMA_cap = 2;
	sc->sc_wdcdev.UDMA_cap = 5;
	sc->sc_wdcdev.set_modes = sch_setup_channel;
	sc->sc_wdcdev.channels = sc->wdc_chanarray;
	sc->sc_wdcdev.nchannels = 1;

	pciide_print_channels(sc->sc_wdcdev.nchannels, interface);

	for (channel = 0; channel < sc->sc_wdcdev.nchannels; channel++) {
		cp = &sc->pciide_channels[channel];

		if (pciide_chansetup(sc, channel, interface) == 0)
			continue;

		pciide_map_compat_intr(pa, cp, channel, interface);
		if (cp->hw_ok == 0)
			continue;
		pciide_mapchan(pa, cp, interface, &cmdsize, &ctlsize,
		    pciide_pci_intr);
		if (cp->hw_ok == 0) {
			pciide_unmap_compat_intr(pa, cp, channel, interface);
			continue;
		}

		sc->sc_wdcdev.set_modes(&cp->wdc_channel);
	}
}

void
sch_setup_channel(struct channel_softc *chp)
{
	struct ata_drive_datas *drvp;
	int drive, mode;
	u_int32_t tim, timaddr;
	struct pciide_channel *cp = (struct pciide_channel *)chp;
	struct pciide_softc *sc = (struct pciide_softc *)cp->wdc_channel.wdc;

	/* Setup DMA if needed */
	pciide_channel_dma_setup(cp);

	/* Per channel settings */
	for (drive = 0; drive < 2; drive++) {
		drvp = &chp->ch_drive[drive];

		/* If no drive, skip */
		if ((drvp->drive_flags & DRIVE) == 0)
			continue;

		timaddr = (drive == 0) ? SCH_D0TIM : SCH_D1TIM;
		tim = pci_conf_read(sc->sc_pc, sc->sc_tag, timaddr);
		tim &= ~SCH_TIM_MASK;

		if ((chp->wdc->cap & WDC_CAPABILITY_UDMA) != 0 &&
		    (drvp->drive_flags & DRIVE_UDMA) != 0) {
			/* Setup UltraDMA mode */
			drvp->drive_flags &= ~DRIVE_DMA;

			mode = drvp->PIO_mode;
			tim |= (drvp->UDMA_mode << 16) | SCH_TIM_SYNCDMA;
		} else if ((chp->wdc->cap & WDC_CAPABILITY_DMA) != 0 &&
		    (drvp->drive_flags & DRIVE_DMA) != 0) {
			/* Setup multiword DMA mode */
			drvp->drive_flags &= ~DRIVE_UDMA;

			tim &= ~SCH_TIM_SYNCDMA;

			/* mode = min(pio, dma + 2) */
			if (drvp->PIO_mode <= (drvp->DMA_mode + 2))
				mode = drvp->PIO_mode;
			else
				mode = drvp->DMA_mode + 2;
		} else {
			mode = drvp->PIO_mode;
			goto pio;
		}

pio:
		/* Setup PIO mode */
		if (mode <= 2) {
			drvp->DMA_mode = 0;
			drvp->PIO_mode = 0;
		} else {
			drvp->PIO_mode = mode;
			drvp->DMA_mode = mode - 2;
		}
		tim |= (drvp->DMA_mode << 8) | (drvp->PIO_mode);
		pci_conf_write(sc->sc_pc, sc->sc_tag, timaddr, tim);
	}

	pciide_print_modes(cp);
}

void
rdc_chip_map(struct pciide_softc *sc, struct pci_attach_args *pa)
{
	struct pciide_channel *cp;
	int channel;
	u_int32_t patr;
	pcireg_t interface = PCI_INTERFACE(pa->pa_class);
	bus_size_t cmdsize, ctlsize;

	printf(": DMA");
	pciide_mapreg_dma(sc, pa);
	sc->sc_wdcdev.cap |= WDC_CAPABILITY_DATA16 | WDC_CAPABILITY_DATA32;
	if (sc->sc_dma_ok) {
		sc->sc_wdcdev.cap |= WDC_CAPABILITY_UDMA |
			WDC_CAPABILITY_DMA | WDC_CAPABILITY_IRQACK;
		sc->sc_wdcdev.irqack = pciide_irqack;
		sc->sc_wdcdev.dma_init = pciide_dma_init;
	}
	sc->sc_wdcdev.PIO_cap = 4;
	sc->sc_wdcdev.DMA_cap = 2;
	sc->sc_wdcdev.UDMA_cap = 5;
	sc->sc_wdcdev.set_modes = rdc_setup_channel;
	sc->sc_wdcdev.channels = sc->wdc_chanarray;
	sc->sc_wdcdev.nchannels = PCIIDE_NUM_CHANNELS;

	pciide_print_channels(sc->sc_wdcdev.nchannels, interface);

	WDCDEBUG_PRINT(("rdc_chip_map: old PATR=0x%x, "
			"PSD1ATR=0x%x, UDCCR=0x%x, IIOCR=0x%x\n",
			pci_conf_read(sc->sc_pc, sc->sc_tag, RDCIDE_PATR),
			pci_conf_read(sc->sc_pc, sc->sc_tag, RDCIDE_PSD1ATR),
			pci_conf_read(sc->sc_pc, sc->sc_tag, RDCIDE_UDCCR),
			pci_conf_read(sc->sc_pc, sc->sc_tag, RDCIDE_IIOCR)),
		       DEBUG_PROBE);

	for (channel = 0; channel < sc->sc_wdcdev.nchannels; channel++) {
		cp = &sc->pciide_channels[channel];

		if (pciide_chansetup(sc, channel, interface) == 0)
			continue;
		patr = pci_conf_read(sc->sc_pc, sc->sc_tag, RDCIDE_PATR);
		if ((patr & RDCIDE_PATR_EN(channel)) == 0) {
			printf("%s: %s ignored (disabled)\n",
			       sc->sc_wdcdev.sc_dev.dv_xname, cp->name);
			cp->hw_ok = 0;
			continue;
		}
		pciide_map_compat_intr(pa, cp, channel, interface);
		if (cp->hw_ok == 0)
			continue;
		pciide_mapchan(pa, cp, interface, &cmdsize, &ctlsize,
			       pciide_pci_intr);
		if (cp->hw_ok == 0)
			goto next;
		if (pciide_chan_candisable(cp)) {
			patr &= ~RDCIDE_PATR_EN(channel);
			pci_conf_write(sc->sc_pc, sc->sc_tag, RDCIDE_PATR,
				       patr);
		}
		if (cp->hw_ok == 0)
			goto next;
		sc->sc_wdcdev.set_modes(&cp->wdc_channel);
next:
		if (cp->hw_ok == 0)
			pciide_unmap_compat_intr(pa, cp, channel, interface);
	}

	WDCDEBUG_PRINT(("rdc_chip_map: PATR=0x%x, "
			"PSD1ATR=0x%x, UDCCR=0x%x, IIOCR=0x%x\n",
			pci_conf_read(sc->sc_pc, sc->sc_tag, RDCIDE_PATR),
			pci_conf_read(sc->sc_pc, sc->sc_tag, RDCIDE_PSD1ATR),
			pci_conf_read(sc->sc_pc, sc->sc_tag, RDCIDE_UDCCR),
			pci_conf_read(sc->sc_pc, sc->sc_tag, RDCIDE_IIOCR)),
		       DEBUG_PROBE);
}

void
rdc_setup_channel(struct channel_softc *chp)
{
	u_int8_t drive;
	u_int32_t patr, psd1atr, udccr, iiocr;
	struct pciide_channel *cp = (struct pciide_channel *)chp;
	struct pciide_softc *sc = (struct pciide_softc *)cp->wdc_channel.wdc;
	struct ata_drive_datas *drvp;

	patr = pci_conf_read(sc->sc_pc, sc->sc_tag, RDCIDE_PATR);
	psd1atr = pci_conf_read(sc->sc_pc, sc->sc_tag, RDCIDE_PSD1ATR);
	udccr = pci_conf_read(sc->sc_pc, sc->sc_tag, RDCIDE_UDCCR);
	iiocr = pci_conf_read(sc->sc_pc, sc->sc_tag, RDCIDE_IIOCR);

	/* setup DMA */
	pciide_channel_dma_setup(cp);

	/* clear modes */
	patr = patr & (RDCIDE_PATR_EN(0) | RDCIDE_PATR_EN(1));
	psd1atr &= ~RDCIDE_PSD1ATR_SETUP_MASK(chp->channel);
	psd1atr &= ~RDCIDE_PSD1ATR_HOLD_MASK(chp->channel);
	for (drive = 0; drive < 2; drive++) {
		udccr &= ~RDCIDE_UDCCR_EN(chp->channel, drive);
		udccr &= ~RDCIDE_UDCCR_TIM_MASK(chp->channel, drive);
		iiocr &= ~RDCIDE_IIOCR_CLK_MASK(chp->channel, drive);
	}
	/* now setup modes */
	for (drive = 0; drive < 2; drive++) {
		drvp = &cp->wdc_channel.ch_drive[drive];
		if ((drvp->drive_flags & DRIVE) == 0)
			continue;
		if (drvp->drive_flags & DRIVE_ATAPI)
			patr |= RDCIDE_PATR_ATA(chp->channel, drive);
		if (drive == 0) {
			patr |= RDCIDE_PATR_SETUP(rdcide_setup[drvp->PIO_mode],
						  chp->channel);
			patr |= RDCIDE_PATR_HOLD(rdcide_hold[drvp->PIO_mode],
						 chp->channel);
		} else {
			patr |= RDCIDE_PATR_DEV1_TEN(chp->channel);
			psd1atr |= RDCIDE_PSD1ATR_SETUP(
				rdcide_setup[drvp->PIO_mode],
				chp->channel);
			psd1atr |= RDCIDE_PSD1ATR_HOLD(
				rdcide_hold[drvp->PIO_mode],
				chp->channel);
		}
		if (drvp->PIO_mode > 0) {
			patr |= RDCIDE_PATR_FTIM(chp->channel, drive);
			patr |= RDCIDE_PATR_IORDY(chp->channel, drive);
		}
		if (drvp->drive_flags & DRIVE_DMA)
			patr |= RDCIDE_PATR_DMAEN(chp->channel, drive);
		if ((drvp->drive_flags & DRIVE_UDMA) == 0)
			continue;

		if ((iiocr & RDCIDE_IIOCR_CABLE(chp->channel, drive)) == 0
		    && drvp->UDMA_mode > 2)
			drvp->UDMA_mode = 2;
		udccr |= RDCIDE_UDCCR_EN(chp->channel, drive);
		udccr |= RDCIDE_UDCCR_TIM(rdcide_udmatim[drvp->UDMA_mode],
			chp->channel, drive);
		iiocr |= RDCIDE_IIOCR_CLK(rdcide_udmaclk[drvp->UDMA_mode],
			chp->channel, drive);
	}

	pci_conf_write(sc->sc_pc, sc->sc_tag, RDCIDE_PATR, patr);
	pci_conf_write(sc->sc_pc, sc->sc_tag, RDCIDE_PSD1ATR, psd1atr);
	pci_conf_write(sc->sc_pc, sc->sc_tag, RDCIDE_UDCCR, udccr);
	pci_conf_write(sc->sc_pc, sc->sc_tag, RDCIDE_IIOCR, iiocr);
}
