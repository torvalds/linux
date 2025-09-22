/*	$OpenBSD: iosf_pci.c,v 1.2 2024/05/24 06:02:57 jsg Exp $ */

/*
 * Copyright (c) 2023 David Gwynne <dlg@openbsd.org>
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

#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#include <dev/ic/iosfvar.h>

/*
 * Intel OnChip System Fabric driver
 */

struct iosf_pci_softc {
	struct device		sc_dev;

	pci_chipset_tag_t	sc_pc;
	pcitag_t		sc_pcitag;

	int			sc_semaddr;

	struct iosf_mbi		sc_mbi;
};

static int	iosf_pci_match(struct device *, void *, void *);
static void	iosf_pci_attach(struct device *, struct device *, void *);

static uint32_t	iosf_pci_mbi_mdr_rd(struct iosf_mbi *, uint32_t, uint32_t);
static void	iosf_pci_mbi_mdr_wr(struct iosf_mbi *, uint32_t, uint32_t,
		    uint32_t);

const struct cfattach iosf_pci_ca = {
	sizeof(struct iosf_pci_softc), iosf_pci_match, iosf_pci_attach
};

struct iosf_pci_device {
	struct pci_matchid		id_pm;
	int				id_semaddr;
};

static const struct iosf_pci_device iosf_pci_devices[] = {
	{ { PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_BAYTRAIL_HB },	0x7 },
	{ { PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_BSW_HB },	0x10e },
	/* Quark X1000, -1 */
	/* Tangier, -1 */
};

static const struct iosf_pci_device *
iosf_pci_device_match(struct pci_attach_args *pa)
{
	pci_vendor_id_t vid = PCI_VENDOR(pa->pa_id);
	pci_product_id_t pid = PCI_PRODUCT(pa->pa_id);
	const struct iosf_pci_device *id;
	size_t i;

	for (i = 0; i < nitems(iosf_pci_devices); i++) {
		id = &iosf_pci_devices[i];
		if (id->id_pm.pm_vid == vid && id->id_pm.pm_pid == pid)
			return (id);
	}

	return (NULL);
}

static int
iosf_pci_match(struct device *parent, void *match, void *aux)
{
	struct pci_attach_args *pa = aux;

	if (iosf_pci_device_match(pa) != NULL) {
		/* match higher than pchb(4) */
		return (2);
	}

	return (0);
}

static void
iosf_pci_attach(struct device *parent, struct device *self, void *aux)
{
	struct iosf_pci_softc *sc = (struct iosf_pci_softc *)self;
	struct pci_attach_args *pa = aux;
	const struct iosf_pci_device *id = iosf_pci_device_match(pa);

	sc->sc_pc = pa->pa_pc;
	sc->sc_pcitag = pa->pa_tag;

	printf(": mbi\n");

	sc->sc_mbi.mbi_dev = self;
	sc->sc_mbi.mbi_prio = 2; /* prefer pci over acpi ops */
	sc->sc_mbi.mbi_semaddr = id->id_semaddr;
	sc->sc_mbi.mbi_mdr_rd = iosf_pci_mbi_mdr_rd;
	sc->sc_mbi.mbi_mdr_wr = iosf_pci_mbi_mdr_wr;

	iosf_mbi_attach(&sc->sc_mbi);
}

/*
 * mbi mdr pciconf operations
 */

#define IOSF_PCI_MBI_MCR		0xd0
#define IOSF_PCI_MBI_MDR		0xd4
#define IOSF_PCI_MBI_MCRX		0xd8

static uint32_t
iosf_pci_mbi_mdr_rd(struct iosf_mbi *mbi, uint32_t mcr, uint32_t mcrx)
{
	struct iosf_pci_softc *sc = (struct iosf_pci_softc *)mbi->mbi_dev;

	if (mcrx != 0) {
		pci_conf_write(sc->sc_pc, sc->sc_pcitag,
		    IOSF_PCI_MBI_MCRX, mcrx);
	}
	pci_conf_write(sc->sc_pc, sc->sc_pcitag, IOSF_PCI_MBI_MCR, mcr);

	return (pci_conf_read(sc->sc_pc, sc->sc_pcitag, IOSF_PCI_MBI_MDR));
}

static void
iosf_pci_mbi_mdr_wr(struct iosf_mbi *mbi, uint32_t mcr, uint32_t mcrx,
    uint32_t mdr)
{
	struct iosf_pci_softc *sc = (struct iosf_pci_softc *)mbi->mbi_dev;

	pci_conf_write(sc->sc_pc, sc->sc_pcitag, IOSF_PCI_MBI_MDR, mdr);

	if (mcrx != 0) {
		pci_conf_write(sc->sc_pc, sc->sc_pcitag,
		    IOSF_PCI_MBI_MCRX, mcrx);
	}

	pci_conf_write(sc->sc_pc, sc->sc_pcitag, IOSF_PCI_MBI_MCR, mcr);
}
