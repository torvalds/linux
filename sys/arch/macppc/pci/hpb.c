/*	$OpenBSD: hpb.c,v 1.1 2015/06/02 13:53:43 mpi Exp $	*/

/*
 * Copyright (c) 2015 Martin Pieuchot
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

#include <machine/intr.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>
#include <dev/pci/ppbreg.h>

/*
 * Section 7.6.2  Interrupt Message Definition Register
 */
#define PCI_HT_LAST_ISMG		(0x01 << 16)
#define PCI_HT_IMSG_LO(idx)		(((2 * (idx)) + 0x10) << 16)
#define  HT_MASK	(1 << 0)
#define  HT_ACTIVELOW	(1 << 1)
#define  HT_EOI		(1 << 5)

#define PCI_HT_IMSG_HI(idx)		(PCI_HT_IMSG_LO(idx) + 1)
#define  HT_PASSPW	(1U << 30)
#define  HT_WAITEOI	(1U << 31)	/* Waiting for EOI */


/* Apple hardware is special... */
#define HT_APPL_EOIOFF(idx)		(0x60 + (((idx) >> 3) & ~3))
#define	HT_APPL_WEOI(idx)		(1 << ((idx) & 0x1f))

struct ht_intr_msg {
	unsigned int	him_idx;	/* Index */
	int		him_ist;	/* Share type */
	pcireg_t	him_weoi;	/* Cached wait for interrupt data */
};

#define	hpb_MAX_IMSG	128

struct hpb_softc {
	struct device		sc_dev;
	pci_chipset_tag_t	sc_pc;
	pcitag_t		sc_tag;
	pcireg_t		sc_id;		/* Needed for Apple hardware */
	unsigned int		sc_off;		/* Interrupt cap. offset */
	unsigned int		sc_nirq;
	struct ht_intr_msg	sc_imap[hpb_MAX_IMSG];
};

int	hpb_match(struct device *, void *, void *);
void	hpb_attach(struct device *, struct device *, void *);

int	hpb_print(void *, const char *);

void	hpb_eoi(int);
int	hpb_enable_irq(int, int);
int	hpb_disable_irq(int, int);

const struct cfattach hpb_ca = {
	sizeof(struct hpb_softc), hpb_match, hpb_attach
};

struct cfdriver hpb_cd = {
	NULL, "hpb", DV_DULL,
};

int
hpb_match(struct device *parent, void *match, void *aux)
{
	struct pci_attach_args *pa = aux;
	pci_chipset_tag_t pc = pa->pa_pc;
	pcitag_t tag = pa->pa_tag;

	if (PCI_CLASS(pa->pa_class) != PCI_CLASS_BRIDGE ||
	    PCI_SUBCLASS(pa->pa_class) != PCI_SUBCLASS_BRIDGE_PCI)
		return (0);

	if (!pci_get_ht_capability(pc, tag, PCI_HT_CAP_INTR, NULL, NULL))
		return (0);

	return (10);
}

void
hpb_attach(struct device *parent, struct device *self, void *aux)
{
	struct hpb_softc *sc = (struct hpb_softc *)self;
	struct pci_attach_args *pa = aux;
	struct pcibus_attach_args pba;
	pci_chipset_tag_t pc = pa->pa_pc;
	pcitag_t tag = pa->pa_tag;
	int idx, irq, off;
	pcireg_t busdata, reg;

	if (!pci_get_ht_capability(pc, tag, PCI_HT_CAP_INTR, &off, NULL))
		panic("A clown ate your HT capability");

	/* Interrupt definitions are numbered beginning with 0. */
	pci_conf_write(pc, tag, off, PCI_HT_LAST_ISMG);
	reg = pci_conf_read(pc, tag, off + PCI_HT_INTR_DATA);

	sc->sc_pc = pc;
	sc->sc_tag = tag;
	sc->sc_id = pa->pa_id;
	sc->sc_off = off;
	sc->sc_nirq = ((reg >> 16) & 0xff);

	if (sc->sc_nirq == 0 || sc->sc_nirq > hpb_MAX_IMSG)
		return;

	printf(": %u sources\n", sc->sc_nirq);

	for (idx = 0; idx < sc->sc_nirq; idx++) {
		pci_conf_write(pc, tag, off, PCI_HT_IMSG_LO(idx));
		reg = pci_conf_read(pc, tag, off + PCI_HT_INTR_DATA);

		pci_conf_write(pc, tag, off + PCI_HT_INTR_DATA, reg | HT_MASK);
		irq = (reg >> 16) & 0xff;

#ifdef DIAGNOSTIC
		if (sc->sc_imap[irq].him_idx != 0) {
			printf("%s: multiple definition for irq %d\n",
			    sc->sc_dev.dv_xname, irq);
			continue;
		}
#endif
		pci_conf_write(pc, tag, off, PCI_HT_IMSG_HI(idx));
		reg = pci_conf_read(pc, tag, off + PCI_HT_INTR_DATA);

		sc->sc_imap[irq].him_idx = idx;
		sc->sc_imap[irq].him_weoi = reg | HT_WAITEOI;
	}

	busdata = pci_conf_read(pc, pa->pa_tag, PPB_REG_BUSINFO);

	memset(&pba, 0, sizeof(pba));
	pba.pba_busname = "pci";
	pba.pba_iot = pa->pa_iot;
	pba.pba_memt = pa->pa_memt;
	pba.pba_dmat = pa->pa_dmat;
	pba.pba_pc = pc;
	pba.pba_domain = pa->pa_domain;
	pba.pba_bus = PPB_BUSINFO_SECONDARY(busdata);
	pba.pba_bridgetag = &sc->sc_tag;

	config_found(self, &pba, hpb_print);
}

int
hpb_print(void *aux, const char *pnp)
{
	struct pcibus_attach_args *pba = aux;

	if (pnp)
		printf("%s at %s", pba->pba_busname, pnp);
	printf(" bus %d", pba->pba_bus);
	return (UNCONF);
}

void
hpb_eoi(int irq)
{
	struct hpb_softc *sc = hpb_cd.cd_devs[0];
	pci_chipset_tag_t pc = sc->sc_pc;
	pcitag_t tag = sc->sc_tag;
	int idx;

	if (irq >= sc->sc_nirq || sc->sc_imap[irq].him_weoi == 0 ||
	    sc->sc_imap[irq].him_ist != IST_LEVEL)
		return;

	idx = sc->sc_imap[irq].him_idx;

	if (PCI_VENDOR(sc->sc_id) == PCI_VENDOR_APPLE) {
		pci_conf_write(pc, tag, HT_APPL_EOIOFF(idx), HT_APPL_WEOI(idx));
	} else {
		pci_conf_write(pc, tag, sc->sc_off, PCI_HT_IMSG_HI(idx));
		pci_conf_write(pc, tag, sc->sc_off + PCI_HT_INTR_DATA,
		    sc->sc_imap[irq].him_weoi);
	}
}

int
hpb_enable_irq(int irq, int ist)
{
	struct hpb_softc *sc = hpb_cd.cd_devs[0];
	pci_chipset_tag_t pc = sc->sc_pc;
	pcitag_t tag = sc->sc_tag;
	pcireg_t reg;
	int idx;

	if (irq >= sc->sc_nirq || sc->sc_imap[irq].him_weoi == 0)
		return (0);

	idx = sc->sc_imap[irq].him_idx;
	sc->sc_imap[irq].him_ist = ist;

	pci_conf_write(pc, tag, sc->sc_off, PCI_HT_IMSG_LO(idx));
	reg = pci_conf_read(pc, tag, sc->sc_off + PCI_HT_INTR_DATA);

	pci_conf_write(pc, tag, sc->sc_off + PCI_HT_INTR_DATA, reg | HT_MASK);

	reg &= ~(HT_ACTIVELOW | HT_EOI | HT_MASK);
	if (ist == IST_LEVEL)
		reg |= HT_ACTIVELOW | HT_EOI;

	pci_conf_write(pc, tag, sc->sc_off + PCI_HT_INTR_DATA, reg);

	return (1);
}

int
hpb_disable_irq(int irq, int ist)
{
	struct hpb_softc *sc = hpb_cd.cd_devs[0];
	pci_chipset_tag_t pc = sc->sc_pc;
	pcitag_t tag = sc->sc_tag;
	pcireg_t reg;
	int idx;

	if (irq > sc->sc_nirq || sc->sc_imap[irq].him_weoi == 0)
		return (0);

	idx = sc->sc_imap[irq].him_idx;

	pci_conf_write(pc, tag, sc->sc_off, PCI_HT_IMSG_LO(idx));
	reg = pci_conf_read(pc, tag, sc->sc_off + PCI_HT_INTR_DATA);

	pci_conf_write(pc, tag, sc->sc_off + PCI_HT_INTR_DATA, reg | HT_MASK);

	return (1);
}
