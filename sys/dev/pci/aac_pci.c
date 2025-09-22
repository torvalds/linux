/*	$OpenBSD: aac_pci.c,v 1.29 2024/05/24 06:02:53 jsg Exp $	*/

/*-
 * Copyright (c) 2000 Michael Smith
 * Copyright (c) 2000 BSDi
 * Copyright (c) 2000 Niklas Hallqvist
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
 *
 *	$FreeBSD: /c/ncvs/src/sys/dev/aac/aac_pci.c,v 1.1 2000/09/13 03:20:34 msmith Exp $
 */

/*
 * This driver would not have rewritten for OpenBSD if it was not for the
 * hardware donation from Nocom.  I want to thank them for their support.
 * Of course, credit should go to Mike Smith for the original work he did
 * in the FreeBSD driver where I found lots of inspiration.
 * - Niklas Hallqvist
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/queue.h>
#include <sys/rwlock.h>

#include <machine/bus.h>
#include <machine/intr.h>

#include <scsi/scsi_all.h>
#include <scsi/scsiconf.h>

#include <dev/pci/pcidevs.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <dev/ic/aacreg.h>
#include <dev/ic/aacvar.h>

int	aac_pci_probe(struct device *, void *, void *);
void	aac_pci_attach(struct device *, struct device *, void *);

/* Adaptec */
#define PCI_PRODUCT_ADP2_AACASR2200S   0x0285
#define PCI_PRODUCT_ADP2_AACASR2120S   0x0286
#define PCI_PRODUCT_ADP2_AACADPSATA2C  0x0289
#define PCI_PRODUCT_ADP2_AACASR2230S   0x028c
#define PCI_PRODUCT_ADP2_AACASR2130S   0x028d
#define PCI_PRODUCT_ADP2_AACADPSATA4C  0x0290
#define PCI_PRODUCT_ADP2_AACADPSATA6C  0x0291
#define PCI_PRODUCT_ADP2_AACADPSATA8C  0x0292
#define PCI_PRODUCT_ADP2_AACADPSATA16C 0x0293

/* Dell */
#define PCI_PRODUCT_ADP2_AACCERCSATA6C 0x0291
#define PCI_PRODUCT_ADP2_AACPERC320DC  0x0287

/* IBM */
#define PCI_PRODUCT_ADP2_AACSERVERAID8I 0x02f2
#define PCI_PRODUCT_ADP2_AACSERVERAID8I_2 0x0312
#define PCI_PRODUCT_ADP2_AACSERVERAID8K 0x9580
#define PCI_PRODUCT_ADP2_AACSERVERAID8S 0x034d

struct aac_sub_ident {
	u_int16_t subvendor;
	u_int16_t subdevice;
	char *desc;
} aac_sub_identifiers[] = {
	{ PCI_VENDOR_ADP2, PCI_PRODUCT_ADP2_AACADPSATA2C, "Adaptec 1210SA" }, /* guess */
	{ PCI_VENDOR_ADP2, PCI_PRODUCT_ADP2_AACASR2130S, "Adaptec 2130S" },
	{ PCI_VENDOR_ADP2, PCI_PRODUCT_ADP2_AACASR2230S, "Adaptec 2230S" },
	{ PCI_VENDOR_ADP2, PCI_PRODUCT_ADP2_AACADPSATA4C, "Adaptec 2410SA" },
	{ PCI_VENDOR_ADP2, PCI_PRODUCT_ADP2_AACADPSATA6C, "Adaptec 2610SA" },
	{ PCI_VENDOR_ADP2, PCI_PRODUCT_ADP2_AACADPSATA8C, "Adaptec 2810SA" }, /* guess */
	{ PCI_VENDOR_ADP2, PCI_PRODUCT_ADP2_AACADPSATA16C, "Adaptec 21610SA" }, /* guess */
	{ PCI_VENDOR_ADP2, PCI_PRODUCT_ADP2_AACASR2120S, "Adaptec 2120S" },
	{ PCI_VENDOR_ADP2, PCI_PRODUCT_ADP2_AACASR2200S, "Adaptec 2200S" },
	{ PCI_VENDOR_DELL, PCI_PRODUCT_ADP2_AACCERCSATA6C, "Dell CERC-SATA" },
	{ PCI_VENDOR_DELL, PCI_PRODUCT_ADP2_AACPERC320DC, "Dell PERC 320/DC" },
	{ PCI_VENDOR_IBM, PCI_PRODUCT_ADP2_AACSERVERAID8I, "IBM ServeRAID-8i" },
	{ PCI_VENDOR_IBM, PCI_PRODUCT_ADP2_AACSERVERAID8I_2, "IBM ServeRAID-8i" },
	{ PCI_VENDOR_IBM, PCI_PRODUCT_ADP2_AACSERVERAID8K, "IBM ServeRAID-8k" },
	{ PCI_VENDOR_IBM, PCI_PRODUCT_ADP2_AACSERVERAID8S, "IBM ServeRAID-8s" },
	{ 0, 0, "" }
};

struct aac_ident {
	u_int16_t vendor;
	u_int16_t device;
	u_int16_t subvendor;
	u_int16_t subdevice;
	int	hwif;
} aac_identifiers[] = {
	/* Dell PERC 2/Si models */
	{ PCI_VENDOR_DELL, PCI_PRODUCT_DELL_PERC_2SI, PCI_VENDOR_DELL,
	    PCI_PRODUCT_DELL_PERC_2SI, AAC_HWIF_I960RX },
	/* Dell PERC 3/Di models */
	{ PCI_VENDOR_DELL, PCI_PRODUCT_DELL_PERC_3DI, PCI_VENDOR_DELL,
	    PCI_PRODUCT_DELL_PERC_3DI, AAC_HWIF_I960RX },
	{ PCI_VENDOR_DELL, PCI_PRODUCT_DELL_PERC_3DI, PCI_VENDOR_DELL,
	    PCI_PRODUCT_DELL_PERC_3DI_3, AAC_HWIF_I960RX },
	{ PCI_VENDOR_DELL, PCI_PRODUCT_DELL_PERC_3DI, PCI_VENDOR_DELL,
	    PCI_PRODUCT_DELL_PERC_3DI_SUB2, AAC_HWIF_I960RX },
	{ PCI_VENDOR_DELL, PCI_PRODUCT_DELL_PERC_3DI, PCI_VENDOR_DELL,
	    PCI_PRODUCT_DELL_PERC_3DI_SUB3, AAC_HWIF_I960RX },
	{ PCI_VENDOR_DELL, PCI_PRODUCT_DELL_PERC_3DI_3, PCI_VENDOR_DELL,
	    PCI_PRODUCT_DELL_PERC_3DI_3_SUB, AAC_HWIF_I960RX },
	{ PCI_VENDOR_DELL, PCI_PRODUCT_DELL_PERC_3DI_3, PCI_VENDOR_DELL,
	    PCI_PRODUCT_DELL_PERC_3DI_3_SUB2, AAC_HWIF_I960RX },
	{ PCI_VENDOR_DELL, PCI_PRODUCT_DELL_PERC_3DI_3, PCI_VENDOR_DELL,
	    PCI_PRODUCT_DELL_PERC_3DI_3_SUB3, AAC_HWIF_I960RX },
	/* Dell PERC 3/Si models */
	{ PCI_VENDOR_DELL, PCI_PRODUCT_DELL_PERC_3SI, PCI_VENDOR_DELL,
	    PCI_PRODUCT_DELL_PERC_3SI, AAC_HWIF_I960RX },
	{ PCI_VENDOR_DELL, PCI_PRODUCT_DELL_PERC_3SI_2, PCI_VENDOR_DELL,
	    PCI_PRODUCT_DELL_PERC_3SI_2_SUB, AAC_HWIF_I960RX },
	/* Adaptec SATA RAID 2 channel XXX guess */
	{ PCI_VENDOR_ADP2, PCI_PRODUCT_ADP2_ASR2200S, PCI_VENDOR_ADP2,
	    PCI_PRODUCT_ADP2_AACADPSATA2C, AAC_HWIF_I960RX },
	/* Adaptec SATA RAID 4 channel */
	{ PCI_VENDOR_ADP2, PCI_PRODUCT_ADP2_ASR2200S, PCI_VENDOR_ADP2,
	    PCI_PRODUCT_ADP2_AACADPSATA4C, AAC_HWIF_I960RX },
	/* Adaptec SATA RAID 6 channel XXX guess */
	{ PCI_VENDOR_ADP2, PCI_PRODUCT_ADP2_ASR2200S, PCI_VENDOR_ADP2,
	    PCI_PRODUCT_ADP2_AACADPSATA6C, AAC_HWIF_I960RX },
	/* Adaptec SATA RAID 8 channel XXX guess */
	{ PCI_VENDOR_ADP2, PCI_PRODUCT_ADP2_ASR2200S, PCI_VENDOR_ADP2,
	    PCI_PRODUCT_ADP2_AACADPSATA8C, AAC_HWIF_I960RX },
	/* Adaptec SATA RAID 16 channel XXX guess */
	{ PCI_VENDOR_ADP2, PCI_PRODUCT_ADP2_ASR2200S, PCI_VENDOR_ADP2,
	    PCI_PRODUCT_ADP2_AACADPSATA16C, AAC_HWIF_I960RX },
	/* Dell CERC-SATA */
	{ PCI_VENDOR_ADP2, PCI_PRODUCT_ADP2_ASR2200S, PCI_VENDOR_DELL,
	    PCI_PRODUCT_ADP2_AACCERCSATA6C, AAC_HWIF_I960RX },
	/* Dell PERC 320/DC */
	{ PCI_VENDOR_ADP2, PCI_PRODUCT_ADP2_ASR2200S, PCI_VENDOR_DELL,
	    PCI_PRODUCT_ADP2_AACPERC320DC, AAC_HWIF_I960RX },
	/* Adaptec ADP-2622 */
	{ PCI_VENDOR_ADP2, PCI_PRODUCT_ADP2_AAC2622, PCI_VENDOR_ADP2,
	    PCI_PRODUCT_ADP2_AAC2622, AAC_HWIF_I960RX },
	/* Adaptec ADP-364 */
	{ PCI_VENDOR_DEC, PCI_PRODUCT_DEC_21554, PCI_VENDOR_ADP2,
	    PCI_PRODUCT_ADP2_AAC364, AAC_HWIF_STRONGARM },
	/* Adaptec ADP-3642 */
	{ PCI_VENDOR_DEC, PCI_PRODUCT_DEC_21554, PCI_VENDOR_ADP2,
	    PCI_PRODUCT_ADP2_AAC3642, AAC_HWIF_STRONGARM },
	/* Dell PERC 2/QC */
	{ PCI_VENDOR_DEC, PCI_PRODUCT_DEC_21554, PCI_VENDOR_ADP2,
	    PCI_PRODUCT_ADP2_PERC_2QC, AAC_HWIF_STRONGARM },
	/* HP NetRAID-4M */
	{ PCI_VENDOR_DEC, PCI_PRODUCT_DEC_21554, PCI_VENDOR_HP,
	    PCI_PRODUCT_HP_NETRAID_4M, AAC_HWIF_STRONGARM },
	{ PCI_VENDOR_ADP2, PCI_PRODUCT_ADP2_ASR2200S, PCI_VENDOR_ADP2,
	    PCI_PRODUCT_ADP2_ASR2120S, AAC_HWIF_I960RX },
	{ PCI_VENDOR_ADP2, PCI_PRODUCT_ADP2_ASR2200S, PCI_VENDOR_ADP2,
	    PCI_PRODUCT_ADP2_ASR2200S, AAC_HWIF_I960RX },
	{ PCI_VENDOR_ADP2, PCI_PRODUCT_ADP2_AACASR2120S, PCI_VENDOR_ADP2,
	    PCI_PRODUCT_ADP2_AACASR2130S, AAC_HWIF_RKT },
	{ PCI_VENDOR_ADP2, PCI_PRODUCT_ADP2_AACASR2120S, PCI_VENDOR_ADP2,
	    PCI_PRODUCT_ADP2_AACASR2230S, AAC_HWIF_RKT },
	/* IBM ServeRAID-8i/8k/8s */
	{ PCI_VENDOR_ADP2, PCI_PRODUCT_ADP2_AACASR2200S, PCI_VENDOR_IBM,
	    PCI_PRODUCT_ADP2_AACSERVERAID8I, AAC_HWIF_I960RX },
	{ PCI_VENDOR_ADP2, PCI_PRODUCT_ADP2_AACASR2200S, PCI_VENDOR_IBM,
	    PCI_PRODUCT_ADP2_AACSERVERAID8I_2, AAC_HWIF_I960RX },
	{ PCI_VENDOR_ADP2, PCI_PRODUCT_ADP2_AACASR2120S, PCI_VENDOR_IBM,
	    PCI_PRODUCT_ADP2_AACSERVERAID8K, AAC_HWIF_RKT },
	{ PCI_VENDOR_ADP2, PCI_PRODUCT_ADP2_AACASR2200S, PCI_VENDOR_IBM,
	    PCI_PRODUCT_ADP2_AACSERVERAID8S, AAC_HWIF_I960RX },
	{ 0, 0, 0, 0 }
};

const struct cfattach aac_pci_ca = {
	sizeof (struct aac_softc), aac_pci_probe, aac_pci_attach
};

/*
 * Determine whether this is one of our supported adapters.
 */
int
aac_pci_probe(struct device *parent, void *match, void *aux)
{
        struct pci_attach_args *pa = aux;
	struct aac_ident *m;
	u_int32_t subsysid;

	for (m = aac_identifiers; m->vendor != 0; m++)
		if (m->vendor == PCI_VENDOR(pa->pa_id) &&
		    m->device == PCI_PRODUCT(pa->pa_id)) {
			subsysid = pci_conf_read(pa->pa_pc, pa->pa_tag,
			    PCI_SUBSYS_ID_REG);
			if (m->subvendor == PCI_VENDOR(subsysid) &&
			    m->subdevice == PCI_PRODUCT(subsysid))
				return (1);
		}
	return (0);
}

void
aac_pci_attach(struct device *parent, struct device *self, void *aux)
{
	struct pci_attach_args *pa = aux;
	pci_chipset_tag_t pc = pa->pa_pc;
	struct aac_softc *sc = (void *)self;
	bus_addr_t membase;
	bus_size_t memsize;
	pci_intr_handle_t ih;
	const char *intrstr;
	int state = 0;
	struct aac_ident *m;
	struct aac_sub_ident *subid;
	u_int32_t subsysid;
	pcireg_t memtype;

	printf(": ");
	subsysid = pci_conf_read(pa->pa_pc, pa->pa_tag, PCI_SUBSYS_ID_REG);
	if ((PCI_VENDOR(pa->pa_id) != PCI_VENDOR(subsysid)) ||
	    (PCI_PRODUCT(pa->pa_id) != PCI_PRODUCT(subsysid))) {
		for (subid = aac_sub_identifiers; subid->subvendor != 0;
		    subid++) {
			if (subid->subvendor == PCI_VENDOR(subsysid) &&
			    subid->subdevice == PCI_PRODUCT(subsysid)) {
				printf("%s ", subid->desc);
				break;
			}
		}
	}

	/*
	 * Map control/status registers.
	 */
	memtype = pci_mapreg_type(pa->pa_pc, pa->pa_tag, PCI_MAPREG_START);
	if (pci_mapreg_map(pa, PCI_MAPREG_START, memtype, 0, &sc->aac_memt,
	    &sc->aac_memh, &membase, &memsize, AAC_REGSIZE)) {
		printf("can't find mem space\n");
		goto bail_out;
	}
	state++;

	if (pci_intr_map(pa, &ih)) {
		printf("couldn't map interrupt\n");
		goto bail_out;
	}
	intrstr = pci_intr_string(pc, ih);
	sc->aac_ih = pci_intr_establish(pc, ih, IPL_BIO, aac_intr, sc,
	    sc->aac_dev.dv_xname);
	if (sc->aac_ih == NULL) {
		printf("couldn't establish interrupt");
		if (intrstr != NULL)
			printf(" at %s", intrstr);
		printf("\n");
		goto bail_out;
	}
	state++;
	if (intrstr != NULL)
		printf("%s\n", intrstr);

	sc->aac_dmat = pa->pa_dmat;
 
	for (m = aac_identifiers; m->vendor != 0; m++)
		if (m->vendor == PCI_VENDOR(pa->pa_id) &&
		    m->device == PCI_PRODUCT(pa->pa_id)) {
			if (m->subvendor == PCI_VENDOR(subsysid) &&
			    m->subdevice == PCI_PRODUCT(subsysid)) {
				sc->aac_hwif = m->hwif;
				switch(sc->aac_hwif) {
				case AAC_HWIF_I960RX:
					AAC_DPRINTF(AAC_D_MISC,
					    ("set hardware up for i960Rx"));
					sc->aac_if = aac_rx_interface;
					break;
				case AAC_HWIF_STRONGARM:
					AAC_DPRINTF(AAC_D_MISC,
					    ("set hardware up for StrongARM"));
					sc->aac_if = aac_sa_interface;
					break;
				case AAC_HWIF_FALCON:
					AAC_DPRINTF(AAC_D_MISC,
					   ("set hardware up for Falcon/PPC"));
					sc->aac_if = aac_fa_interface;
					break;
				case AAC_HWIF_RKT:
					AAC_DPRINTF(AAC_D_MISC,
					   ("set hardware up for Rocket/MIPS"));
					sc->aac_if = aac_rkt_interface;
					break;
				default:
					sc->aac_hwif = AAC_HWIF_UNKNOWN;
					break;
				}
				break;
			}
		}

	if (aac_attach(sc))
		goto bail_out;

	return;

 bail_out:
	if (state > 1)
		pci_intr_disestablish(pc, sc->aac_ih);
	if (state > 0)
		bus_space_unmap(sc->aac_memt, sc->aac_memh, memsize);
	return;
}
