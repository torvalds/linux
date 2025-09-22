/*	$OpenBSD: qla_pci.c,v 1.11 2024/09/04 07:54:52 mglocker Exp $ */

/*
 * Copyright (c) 2011 David Gwynne <dlg@openbsd.org>
 * Copyright (c) 2013, 2014 Jonathan Matthew <jmatthew@openbsd.org>
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

#include <dev/ic/qlareg.h>
#include <dev/ic/qlavar.h>

#define QLA_PCI_MEM_BAR		0x14
#define QLA_PCI_IO_BAR		0x10

int	qla_pci_match(struct device *, void *, void *);
void	qla_pci_attach(struct device *, struct device *, void *);
int	qla_pci_detach(struct device *, int);

struct qla_pci_softc {
	struct qla_softc	psc_qla;

	pci_chipset_tag_t	psc_pc;
	pcitag_t		psc_tag;

	void			*psc_ih;
};

const struct cfattach qla_pci_ca = {
	sizeof(struct qla_pci_softc),
	qla_pci_match,
	qla_pci_attach
};

#define PREAD(s, r)	pci_conf_read((s)->psc_pc, (s)->psc_tag, (r))
#define PWRITE(s, r, v)	pci_conf_write((s)->psc_pc, (s)->psc_tag, (r), (v))

static const struct pci_matchid qla_devices[] = {
	{ PCI_VENDOR_QLOGIC,	PCI_PRODUCT_QLOGIC_ISP2100 },
	{ PCI_VENDOR_QLOGIC,	PCI_PRODUCT_QLOGIC_ISP2200 },
	{ PCI_VENDOR_QLOGIC,	PCI_PRODUCT_QLOGIC_ISP2300 },
	{ PCI_VENDOR_QLOGIC,	PCI_PRODUCT_QLOGIC_ISP2312 },
	{ PCI_VENDOR_QLOGIC,	PCI_PRODUCT_QLOGIC_ISP2322 },
	{ PCI_VENDOR_QLOGIC,	PCI_PRODUCT_QLOGIC_ISP6312 },
	{ PCI_VENDOR_QLOGIC,	PCI_PRODUCT_QLOGIC_ISP6322 },
};

int
qla_pci_match(struct device *parent, void *match, void *aux)
{
	return (pci_matchbyid(aux, qla_devices, nitems(qla_devices)) * 2);
}

void
qla_pci_attach(struct device *parent, struct device *self, void *aux)
{
	struct qla_pci_softc *psc = (void *)self;
	struct qla_softc *sc = &psc->psc_qla;
	struct pci_attach_args *pa = aux;
	pci_intr_handle_t ih;
	const char *intrstr;
	u_int32_t pcictl;
#ifdef __sparc64__
	u_int64_t wwn;
	int node;
#endif

	pcireg_t bars[] = { QLA_PCI_MEM_BAR, QLA_PCI_IO_BAR };
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
	    qla_intr, sc, DEVNAME(sc));
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
	/* fw manual says to enable bus master here, then disable it while
	 * resetting.. hm.
	 */
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
	case PCI_PRODUCT_QLOGIC_ISP2100:
		sc->sc_isp_gen = QLA_GEN_ISP2100;
		sc->sc_isp_type = QLA_ISP2100;
		break;

	case PCI_PRODUCT_QLOGIC_ISP2200:
		sc->sc_isp_gen = QLA_GEN_ISP2200;
		sc->sc_isp_type = QLA_ISP2200;
		break;
		
	case PCI_PRODUCT_QLOGIC_ISP2300:
		sc->sc_isp_type = QLA_ISP2300;
		sc->sc_isp_gen = QLA_GEN_ISP23XX;
		break;

	case PCI_PRODUCT_QLOGIC_ISP2312:
	case PCI_PRODUCT_QLOGIC_ISP6312:
		sc->sc_isp_type = QLA_ISP2312;
		sc->sc_isp_gen = QLA_GEN_ISP23XX;
		break;

	case PCI_PRODUCT_QLOGIC_ISP2322:
	case PCI_PRODUCT_QLOGIC_ISP6322:
		sc->sc_isp_type = QLA_ISP2322;
		sc->sc_isp_gen = QLA_GEN_ISP23XX;
		break;

	default:
		printf("unknown pci id %x", pa->pa_id);
		return;
	}

#ifdef __sparc64__
	node = PCITAG_NODE(pa->pa_tag);
	if (OF_getprop(node, "port-wwn", &wwn, sizeof(wwn)) == sizeof(wwn))
		sc->sc_port_name = wwn;
	if (OF_getprop(node, "node-wwn", &wwn, sizeof(wwn)) == sizeof(wwn))
		sc->sc_node_name = wwn;
#endif

	sc->sc_port = pa->pa_function;

	if (qla_attach(sc) != 0) {
		/* error printed by qla_attach */
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
qla_pci_detach(struct device *self, int flags)
{
	struct qla_pci_softc *psc = (struct qla_pci_softc *)self;
	struct qla_softc *sc = &psc->psc_qla;
	int rv;

	if (psc->psc_ih == NULL) {
		/* we didn't attach properly, so nothing to detach */
		return (0);
	}

	rv = qla_detach(sc, flags);
	if (rv != 0)
		return (rv);

	pci_intr_disestablish(psc->psc_pc, psc->psc_ih);
	psc->psc_ih = NULL;

	bus_space_unmap(sc->sc_iot, sc->sc_ioh, sc->sc_ios);
	sc->sc_ios = 0;

	return (0);
}
