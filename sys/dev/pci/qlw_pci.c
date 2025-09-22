/*	$OpenBSD: qlw_pci.c,v 1.14 2024/09/04 07:54:52 mglocker Exp $ */

/*
 * Copyright (c) 2011 David Gwynne <dlg@openbsd.org>
 * Copyright (c) 2013, 2014 Jonathan Matthew <jmatthew@openbsd.org>
 * Copyright (c) 2014 Mark Kettenis <kettenis@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>

#include <machine/bus.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#ifdef __sparc64__
#include <dev/ofw/openfirm.h>
#endif

#include <scsi/scsi_all.h>
#include <scsi/scsiconf.h>

#include <dev/ic/qlwreg.h>
#include <dev/ic/qlwvar.h>

#ifndef ISP_NOFIRMWARE
#include <dev/microcode/isp/asm_1040.h>
#include <dev/microcode/isp/asm_1080.h>
#include <dev/microcode/isp/asm_12160.h>
#endif

#define QLW_PCI_MEM_BAR		0x14
#define QLW_PCI_IO_BAR		0x10

int	qlw_pci_match(struct device *, void *, void *);
void	qlw_pci_attach(struct device *, struct device *, void *);
int	qlw_pci_detach(struct device *, int);

struct qlw_pci_softc {
	struct qlw_softc	psc_qlw;

	pci_chipset_tag_t	psc_pc;
	pcitag_t		psc_tag;

	void			*psc_ih;
};

const struct cfattach qlw_pci_ca = {
	sizeof(struct qlw_pci_softc),
	qlw_pci_match,
	qlw_pci_attach
};

static const struct pci_matchid qlw_devices[] = {
	{ PCI_VENDOR_QLOGIC,	PCI_PRODUCT_QLOGIC_ISP1020 },
	{ PCI_VENDOR_QLOGIC,	PCI_PRODUCT_QLOGIC_ISP1240 },
	{ PCI_VENDOR_QLOGIC,	PCI_PRODUCT_QLOGIC_ISP1080 },
	{ PCI_VENDOR_QLOGIC,	PCI_PRODUCT_QLOGIC_ISP1280 },
	{ PCI_VENDOR_QLOGIC,	PCI_PRODUCT_QLOGIC_ISP10160 },
	{ PCI_VENDOR_QLOGIC,	PCI_PRODUCT_QLOGIC_ISP12160 },
};

int
qlw_pci_match(struct device *parent, void *match, void *aux)
{
	struct pci_attach_args *pa = aux;

	/* Silly AMI MegaRAID exposes its ISP12160 to us. */
	if (PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_QLOGIC_ISP12160) {
		pcireg_t subid;

		subid = pci_conf_read(pa->pa_pc, pa->pa_tag, PCI_SUBVEND_0);
		if (PCI_VENDOR(subid) == PCI_VENDOR_AMI)
			return (0);
	}

	return (pci_matchbyid(aux, qlw_devices, nitems(qlw_devices)) * 2);
}

void
qlw_pci_attach(struct device *parent, struct device *self, void *aux)
{
	struct qlw_pci_softc *psc = (void *)self;
	struct qlw_softc *sc = &psc->psc_qlw;
	struct pci_attach_args *pa = aux;
	pci_intr_handle_t ih;
	const char *intrstr;
	u_int32_t pcictl;
#ifdef __sparc64__
	int node, initiator;
#endif

	pcireg_t bars[] = { QLW_PCI_MEM_BAR, QLW_PCI_IO_BAR };
	pcireg_t memtype;
	int r;

	psc->psc_pc = pa->pa_pc;
	psc->psc_tag = pa->pa_tag;
	psc->psc_ih = NULL;
	sc->sc_dmat = pa->pa_dmat;
	sc->sc_ios = 0;

	for (r = 0; r < nitems(bars); r++) {
		memtype = pci_mapreg_type(psc->psc_pc, psc->psc_tag, bars[r]);
		if (pci_mapreg_map(pa, bars[r], memtype, 0,
		    &sc->sc_iot, &sc->sc_ioh, NULL, &sc->sc_ios, 0) == 0)
			break;

		sc->sc_ios = 0;
	}
	if (sc->sc_ios == 0) {
		printf(": unable to map registers\n");
		return;
	}

	if (pci_intr_map(pa, &ih)) {
		printf(": unable to map interrupt\n");
		goto unmap;
	}
	intrstr = pci_intr_string(psc->psc_pc, ih);
	psc->psc_ih = pci_intr_establish(psc->psc_pc, ih, IPL_BIO,
	    qlw_intr, sc, DEVNAME(sc));
	if (psc->psc_ih == NULL) {
		printf(": unable to establish interrupt");
		if (intrstr != NULL)
			printf(" at %s", intrstr);
		printf("\n");
		goto deintr;
	}

	printf(": %s\n", intrstr);

	pcictl = pci_conf_read(pa->pa_pc, pa->pa_tag, PCI_COMMAND_STATUS_REG);
	pcictl |= PCI_COMMAND_INVALIDATE_ENABLE |
	    PCI_COMMAND_PARITY_ENABLE | PCI_COMMAND_SERR_ENABLE;
	pci_conf_write(pa->pa_pc, pa->pa_tag, PCI_COMMAND_STATUS_REG, pcictl);

	pcictl = pci_conf_read(pa->pa_pc, pa->pa_tag, PCI_BHLC_REG);
	pcictl &= ~(PCI_LATTIMER_MASK << PCI_LATTIMER_SHIFT);
	pcictl &= ~(PCI_CACHELINE_MASK << PCI_CACHELINE_SHIFT);
	pcictl |= (0x80 << PCI_LATTIMER_SHIFT);
	pcictl |= (0x10 << PCI_CACHELINE_SHIFT);
	pci_conf_write(pa->pa_pc, pa->pa_tag, PCI_BHLC_REG, pcictl);

	pcictl = pci_conf_read(pa->pa_pc, pa->pa_tag, PCI_ROM_REG);
	pcictl &= ~1;
	pci_conf_write(pa->pa_pc, pa->pa_tag, PCI_ROM_REG, pcictl);

	switch (PCI_PRODUCT(pa->pa_id)) {
	case PCI_PRODUCT_QLOGIC_ISP1020:
		sc->sc_isp_gen = QLW_GEN_ISP1040;
		switch (PCI_REVISION(pa->pa_class)) {
		case 0:
			sc->sc_isp_type = QLW_ISP1020;
			break;
		case 1:
			sc->sc_isp_type = QLW_ISP1020A;
			break;
		case 2:
			sc->sc_isp_type = QLW_ISP1040;
			break;
		case 3:
			sc->sc_isp_type = QLW_ISP1040A;
			break;
		case 4:
			sc->sc_isp_type = QLW_ISP1040B;
			break;
		case 5:
		default:
			sc->sc_isp_type = QLW_ISP1040C;
			break;
		}
		sc->sc_numbusses = 1;
		if (PCI_REVISION(pa->pa_class) < 2)
			sc->sc_clock = 40; /* ISP1020/1020A */
		else
			sc->sc_clock = 60; /* ISP1040/1040A/1040B/1040C */
		break;

	case PCI_PRODUCT_QLOGIC_ISP1240:
		sc->sc_isp_gen = QLW_GEN_ISP1080;
		sc->sc_isp_type = QLW_ISP1240;
		sc->sc_numbusses = 2;
		sc->sc_clock = 60;
		break;

	case PCI_PRODUCT_QLOGIC_ISP1080:
		sc->sc_isp_gen = QLW_GEN_ISP1080;
		sc->sc_isp_type = QLW_ISP1080;
		sc->sc_numbusses = 1;
		sc->sc_clock = 100;
		break;

	case PCI_PRODUCT_QLOGIC_ISP1280:
		sc->sc_isp_gen = QLW_GEN_ISP1080;
		sc->sc_isp_type = QLW_ISP1280;
		sc->sc_numbusses = 2;
		sc->sc_clock = 100;
		break;

	case PCI_PRODUCT_QLOGIC_ISP10160:
		sc->sc_isp_gen = QLW_GEN_ISP12160;
		sc->sc_isp_type = QLW_ISP10160;
		sc->sc_numbusses = 1;
		sc->sc_clock = 100;
		break;

	case PCI_PRODUCT_QLOGIC_ISP12160:
		sc->sc_isp_gen = QLW_GEN_ISP12160;
		sc->sc_isp_type = QLW_ISP12160;
		sc->sc_numbusses = 2;
		sc->sc_clock = 100;
		break;

	default:
		printf("unknown pci id %x", pa->pa_id);
		return;
	}

#ifndef ISP_NOFIRMWARE
	switch (sc->sc_isp_gen) {
	case QLW_GEN_ISP1040:
		sc->sc_firmware = isp_1040_risc_code;
		break;
	case QLW_GEN_ISP1080:
		sc->sc_firmware = isp_1080_risc_code;
		break;
	case QLW_GEN_ISP12160:
		sc->sc_firmware = isp_12160_risc_code;
		break;
	default:
		break;
	}
#endif

	/*
	 * The standard SCSI initiator ID is 7.
	 * Add-on cards should have a valid nvram, which will override
	 * these defaults.
	 */
	sc->sc_initiator[0] = sc->sc_initiator[1] = 7;

#ifdef __sparc64__
	/*
	 * Walk up the Open Firmware device tree until we find a
	 * "scsi-initiator-id" property.
	 */
	node = PCITAG_NODE(pa->pa_tag);
	while (node) {
		if (OF_getprop(node, "scsi-initiator-id",
		    &initiator, sizeof(initiator)) == sizeof(initiator)) {
			/*
			 * Override the SCSI initiator ID provided by
			 * the nvram.
			 */
			sc->sc_flags |= QLW_FLAG_INITIATOR;
			sc->sc_initiator[0] = sc->sc_initiator[1] = initiator;
			break;
		}
		node = OF_parent(node);
	}
#endif

	sc->sc_host_cmd_ctrl = QLW_HOST_CMD_CTRL_PCI;
	sc->sc_mbox_base = QLW_MBOX_BASE_PCI;

	if (qlw_attach(sc) != 0) {
		/* error printed by qlw_attach */
		goto deintr;
	}

	return;

deintr:
	pci_intr_disestablish(psc->psc_pc, psc->psc_ih);
	psc->psc_ih = NULL;
unmap:
	bus_space_unmap(sc->sc_iot, sc->sc_ioh, sc->sc_ios);
	sc->sc_ios = 0;
}

int
qlw_pci_detach(struct device *self, int flags)
{
	struct qlw_pci_softc *psc = (struct qlw_pci_softc *)self;
	struct qlw_softc *sc = &psc->psc_qlw;
	int rv;

	if (psc->psc_ih == NULL) {
		/* we didn't attach properly, so nothing to detach */
		return (0);
	}

	rv = qlw_detach(sc, flags);
	if (rv != 0)
		return (rv);

	pci_intr_disestablish(psc->psc_pc, psc->psc_ih);
	psc->psc_ih = NULL;

	bus_space_unmap(sc->sc_iot, sc->sc_ioh, sc->sc_ios);
	sc->sc_ios = 0;

	return (0);
}
