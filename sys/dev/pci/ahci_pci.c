/*	$OpenBSD: ahci_pci.c,v 1.18 2024/06/16 18:00:08 kn Exp $ */

/*
 * Copyright (c) 2006 David Gwynne <dlg@openbsd.org>
 * Copyright (c) 2010 Conformal Systems LLC <info@conformal.com>
 * Copyright (c) 2010 Jonathan Matthew <jonathan@d14n.org>
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
#include <sys/queue.h>
#include <sys/mutex.h>

#include <machine/bus.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#include <dev/ic/ahcireg.h>
#include <dev/ic/ahcivar.h>

#define AHCI_PCI_BAR		0x24
#define AHCI_PCI_ATI_SB600_MAGIC	0x40
#define AHCI_PCI_ATI_SB600_LOCKED	0x01

struct ahci_pci_softc {
	struct ahci_softc	psc_ahci;

	pci_chipset_tag_t	psc_pc;
	pcitag_t		psc_tag;

	int			psc_flags;
};

struct ahci_device {
	pci_vendor_id_t		ad_vendor;
	pci_product_id_t	ad_product;
	int			(*ad_match)(struct pci_attach_args *);
	int			(*ad_attach)(struct ahci_softc *,
				    struct pci_attach_args *);
};

const struct ahci_device *ahci_lookup_device(struct pci_attach_args *);

int			ahci_no_match(struct pci_attach_args *);
int			ahci_vt8251_attach(struct ahci_softc *,
			    struct pci_attach_args *);
void			ahci_ati_sb_idetoahci(struct ahci_softc *,
			    struct pci_attach_args *pa);
int			ahci_ati_sb600_attach(struct ahci_softc *,
			    struct pci_attach_args *);
int			ahci_ati_sb700_attach(struct ahci_softc *,
			    struct pci_attach_args *);
int			ahci_amd_hudson2_attach(struct ahci_softc *,
			    struct pci_attach_args *);
int			ahci_intel_attach(struct ahci_softc *,
			    struct pci_attach_args *);
int			ahci_samsung_attach(struct ahci_softc *,
			    struct pci_attach_args *);
int			ahci_storx_attach(struct ahci_softc *,
			    struct pci_attach_args *);

static const struct ahci_device ahci_devices[] = {
	{ PCI_VENDOR_AMD,	PCI_PRODUCT_AMD_HUDSON2_SATA_1,
	    NULL,		ahci_amd_hudson2_attach },
	{ PCI_VENDOR_AMD,	PCI_PRODUCT_AMD_HUDSON2_SATA_2,
	    NULL,		ahci_amd_hudson2_attach },
	{ PCI_VENDOR_AMD,	PCI_PRODUCT_AMD_HUDSON2_SATA_3,
	    NULL,		ahci_amd_hudson2_attach },
	{ PCI_VENDOR_AMD,	PCI_PRODUCT_AMD_HUDSON2_SATA_4,
	    NULL,		ahci_amd_hudson2_attach },
	{ PCI_VENDOR_AMD,	PCI_PRODUCT_AMD_HUDSON2_SATA_5,
	    NULL,		ahci_amd_hudson2_attach },
	{ PCI_VENDOR_AMD,	PCI_PRODUCT_AMD_HUDSON2_SATA_6,
	    NULL,		ahci_amd_hudson2_attach },

	{ PCI_VENDOR_ATI,	PCI_PRODUCT_ATI_SB600_SATA,
	    NULL,		ahci_ati_sb600_attach },
	{ PCI_VENDOR_ATI,	PCI_PRODUCT_ATI_SBX00_SATA_1,
	    NULL,		ahci_ati_sb700_attach },
	{ PCI_VENDOR_ATI,	PCI_PRODUCT_ATI_SBX00_SATA_2,
	    NULL,		ahci_ati_sb700_attach },
	{ PCI_VENDOR_ATI,	PCI_PRODUCT_ATI_SBX00_SATA_3,
	    NULL,		ahci_ati_sb700_attach },
	{ PCI_VENDOR_ATI,	PCI_PRODUCT_ATI_SBX00_SATA_4,
	    NULL,		ahci_ati_sb700_attach },
	{ PCI_VENDOR_ATI,	PCI_PRODUCT_ATI_SBX00_SATA_5,
	    NULL,		ahci_ati_sb700_attach },
	{ PCI_VENDOR_ATI,	PCI_PRODUCT_ATI_SBX00_SATA_6,
	    NULL,		ahci_ati_sb700_attach },

	{ PCI_VENDOR_ASMEDIA,	PCI_PRODUCT_ASMEDIA_ASM1061_SATA },

	{ PCI_VENDOR_INTEL,	PCI_PRODUCT_INTEL_6SERIES_AHCI_1,
	    NULL,		ahci_intel_attach },
	{ PCI_VENDOR_INTEL,	PCI_PRODUCT_INTEL_6SERIES_AHCI_2,
	    NULL,		ahci_intel_attach },
	{ PCI_VENDOR_INTEL,	PCI_PRODUCT_INTEL_6321ESB_AHCI,
	    NULL,		ahci_intel_attach },
	{ PCI_VENDOR_INTEL,	PCI_PRODUCT_INTEL_82801GR_AHCI,
	    NULL,		ahci_intel_attach },
	{ PCI_VENDOR_INTEL,	PCI_PRODUCT_INTEL_82801GBM_AHCI,
	    NULL,		ahci_intel_attach },
	{ PCI_VENDOR_INTEL,	PCI_PRODUCT_INTEL_82801H_AHCI_6P,
	    NULL,		ahci_intel_attach },
	{ PCI_VENDOR_INTEL,	PCI_PRODUCT_INTEL_82801H_AHCI_4P,
	    NULL,		ahci_intel_attach },
	{ PCI_VENDOR_INTEL,	PCI_PRODUCT_INTEL_82801HBM_AHCI,
	    NULL,		ahci_intel_attach },
	{ PCI_VENDOR_INTEL,	PCI_PRODUCT_INTEL_82801I_AHCI_1,
	    NULL,		ahci_intel_attach },
	{ PCI_VENDOR_INTEL,	PCI_PRODUCT_INTEL_82801I_AHCI_2,
	    NULL,		ahci_intel_attach },
	{ PCI_VENDOR_INTEL,	PCI_PRODUCT_INTEL_82801I_AHCI_3,
	    NULL,		ahci_intel_attach },
	{ PCI_VENDOR_INTEL,	PCI_PRODUCT_INTEL_82801JD_AHCI,
	    NULL,		ahci_intel_attach },
	{ PCI_VENDOR_INTEL,	PCI_PRODUCT_INTEL_82801JI_AHCI,
	    NULL,		ahci_intel_attach },
	{ PCI_VENDOR_INTEL,	PCI_PRODUCT_INTEL_3400_AHCI_1,
	    NULL,		ahci_intel_attach },
	{ PCI_VENDOR_INTEL,	PCI_PRODUCT_INTEL_3400_AHCI_2,
	    NULL,		ahci_intel_attach },
	{ PCI_VENDOR_INTEL,	PCI_PRODUCT_INTEL_3400_AHCI_3,
	    NULL,		ahci_intel_attach },
	{ PCI_VENDOR_INTEL,	PCI_PRODUCT_INTEL_3400_AHCI_4,
	    NULL,		ahci_intel_attach },
	{ PCI_VENDOR_INTEL,	PCI_PRODUCT_INTEL_EP80579_AHCI,
	    NULL,		ahci_intel_attach },

	{ PCI_VENDOR_SAMSUNG2,	PCI_PRODUCT_SAMSUNG2_S4LN053X01,
	    NULL,		ahci_samsung_attach },
	{ PCI_VENDOR_SAMSUNG2,	PCI_PRODUCT_SAMSUNG2_XP941,
	    NULL,		ahci_samsung_attach },
	{ PCI_VENDOR_SAMSUNG2,	PCI_PRODUCT_SAMSUNG2_SM951_AHCI,
	    NULL,		ahci_samsung_attach },

	{ PCI_VENDOR_VIATECH,	PCI_PRODUCT_VIATECH_VT8251_SATA,
	  ahci_no_match,	ahci_vt8251_attach },

	{ PCI_VENDOR_ZHAOXIN,	PCI_PRODUCT_ZHAOXIN_STORX_AHCI,
	  NULL,			ahci_storx_attach },
};

int			ahci_pci_match(struct device *, void *, void *);
void			ahci_pci_attach(struct device *, struct device *,
			    void *);
int			ahci_pci_detach(struct device *, int);
int			ahci_pci_activate(struct device *, int);

const struct cfattach ahci_pci_ca = {
	sizeof(struct ahci_pci_softc),
	ahci_pci_match,
	ahci_pci_attach,
	ahci_pci_detach,
	ahci_pci_activate
};

const struct cfattach ahci_jmb_ca = {
	sizeof(struct ahci_pci_softc),
	ahci_pci_match,
	ahci_pci_attach,
	ahci_pci_detach
};

int			ahci_map_regs(struct ahci_pci_softc *,
			    struct pci_attach_args *);
void			ahci_unmap_regs(struct ahci_pci_softc *);
int			ahci_map_intr(struct ahci_pci_softc *,
			    struct pci_attach_args *, pci_intr_handle_t);
void			ahci_unmap_intr(struct ahci_pci_softc *);

const struct ahci_device *
ahci_lookup_device(struct pci_attach_args *pa)
{
	int				i;
	const struct ahci_device	*ad;

	for (i = 0; i < (sizeof(ahci_devices) / sizeof(ahci_devices[0])); i++) {
		ad = &ahci_devices[i];
		if (ad->ad_vendor == PCI_VENDOR(pa->pa_id) &&
		    ad->ad_product == PCI_PRODUCT(pa->pa_id))
			return (ad);
	}

	return (NULL);
}

int
ahci_no_match(struct pci_attach_args *pa)
{
	return (0);
}

int
ahci_vt8251_attach(struct ahci_softc *sc, struct pci_attach_args *pa)
{
	sc->sc_flags |= AHCI_F_NO_NCQ;

	return (0);
}

void
ahci_ati_sb_idetoahci(struct ahci_softc *sc, struct pci_attach_args *pa)
{
	pcireg_t			magic;

	if (PCI_SUBCLASS(pa->pa_class) == PCI_SUBCLASS_MASS_STORAGE_IDE) {
		magic = pci_conf_read(pa->pa_pc, pa->pa_tag,
		    AHCI_PCI_ATI_SB600_MAGIC);
		pci_conf_write(pa->pa_pc, pa->pa_tag,
		    AHCI_PCI_ATI_SB600_MAGIC,
		    magic | AHCI_PCI_ATI_SB600_LOCKED);

		pci_conf_write(pa->pa_pc, pa->pa_tag, PCI_CLASS_REG,
		    PCI_CLASS_MASS_STORAGE << PCI_CLASS_SHIFT |
		    PCI_SUBCLASS_MASS_STORAGE_SATA << PCI_SUBCLASS_SHIFT |
		    PCI_INTERFACE_SATA_AHCI10 << PCI_INTERFACE_SHIFT |
		    PCI_REVISION(pa->pa_class) << PCI_REVISION_SHIFT);

		pci_conf_write(pa->pa_pc, pa->pa_tag,
		    AHCI_PCI_ATI_SB600_MAGIC, magic);
	}
}

int
ahci_ati_sb600_attach(struct ahci_softc *sc, struct pci_attach_args *pa)
{
	ahci_ati_sb_idetoahci(sc, pa);

	sc->sc_flags |= AHCI_F_IPMS_PROBE;

	return (0);
}

int
ahci_ati_sb700_attach(struct ahci_softc *sc, struct pci_attach_args *pa)
{
	ahci_ati_sb_idetoahci(sc, pa);

	sc->sc_flags |= AHCI_F_IPMS_PROBE;

	return (0);
}

int
ahci_amd_hudson2_attach(struct ahci_softc *sc, struct pci_attach_args *pa)
{
	ahci_ati_sb_idetoahci(sc, pa);

	sc->sc_flags |= AHCI_F_IPMS_PROBE;

	return (0);
}

int
ahci_intel_attach(struct ahci_softc *sc, struct pci_attach_args *pa)
{
	sc->sc_flags |= AHCI_F_NO_PMP;

	return (0);
}

int
ahci_samsung_attach(struct ahci_softc *sc, struct pci_attach_args *pa)
{
	/*
	 * Disable MSI with the Samsung S4LN053X01 SSD controller as found
	 * in some Apple MacBook Air models such as the 6,1 and 6,2, as well
	 * as the XP941 SSD controller.
	 * https://bugzilla.kernel.org/show_bug.cgi?id=60731
	 * https://bugzilla.kernel.org/show_bug.cgi?id=89171
	 */
	sc->sc_flags |= AHCI_F_NO_MSI;

	return (0);
}

int
ahci_storx_attach(struct ahci_softc *sc, struct pci_attach_args *pa)
{
	/*
	 * Disable MSI with the ZX-100/ZX-200/ZX-E StorX AHCI Controller
	 * in the Unchartevice 6640MA notebook, otherwise ahci(4) hangs
	 * with SATA speed set to "Gen3" in BIOS.
	 */
	sc->sc_flags |= AHCI_F_NO_MSI;

	return (0);
}

int
ahci_pci_match(struct device *parent, void *match, void *aux)
{
	struct pci_attach_args		*pa = aux;
	const struct ahci_device	*ad;

	ad = ahci_lookup_device(pa);
	if (ad != NULL) {
		/* the device may need special checks to see if it matches */
		if (ad->ad_match != NULL)
			return (ad->ad_match(pa));

		return (2); /* match higher than pciide */
	}

	if (PCI_CLASS(pa->pa_class) == PCI_CLASS_MASS_STORAGE &&
	    PCI_SUBCLASS(pa->pa_class) == PCI_SUBCLASS_MASS_STORAGE_SATA &&
	    PCI_INTERFACE(pa->pa_class) == PCI_INTERFACE_SATA_AHCI10)
		return (2);

	return (0);
}

void
ahci_pci_attach(struct device *parent, struct device *self, void *aux)
{
	struct ahci_pci_softc		*psc = (struct ahci_pci_softc *)self;
	struct ahci_softc		*sc = &psc->psc_ahci;
	struct pci_attach_args		*pa = aux;
	const struct ahci_device	*ad;
	pci_intr_handle_t		ih;

	psc->psc_pc = pa->pa_pc;
	psc->psc_tag = pa->pa_tag;
	sc->sc_dmat = pa->pa_dmat;

	ad = ahci_lookup_device(pa);
	if (ad != NULL && ad->ad_attach != NULL) {
		if (ad->ad_attach(sc, pa) != 0) {
			/* error should be printed by ad_attach */
			return;
		}
	}

	if (sc->sc_flags & AHCI_F_NO_MSI)
		pa->pa_flags &= ~PCI_FLAGS_MSI_ENABLED;

	if (pci_intr_map_msi(pa, &ih) != 0 && pci_intr_map(pa, &ih) != 0) {
		printf(": unable to map interrupt\n");
		return;
	}
	printf(": %s,", pci_intr_string(pa->pa_pc, ih));

	if (ahci_map_regs(psc, pa) != 0) {
		/* error already printed by ahci_map_regs */
		return;
	}

	if (ahci_map_intr(psc, pa, ih) != 0) {
		/* error already printed by ahci_map_intr */
		goto unmap;
	}

	if (ahci_attach(sc) != 0) {
		/* error printed by ahci_attach */
		goto unmap;
	}

	return;

unmap:
	ahci_unmap_regs(psc);
	return;
}

int
ahci_pci_detach(struct device *self, int flags)
{
	struct ahci_pci_softc		*psc = (struct ahci_pci_softc *)self;
	struct ahci_softc		*sc = &psc->psc_ahci;

	ahci_detach(sc, flags);

	ahci_unmap_intr(psc);
	ahci_unmap_regs(psc);

	return (0);
}

int
ahci_map_regs(struct ahci_pci_softc *psc, struct pci_attach_args *pa)
{
	pcireg_t			maptype;
	struct ahci_softc		*sc = &psc->psc_ahci;

	maptype = pci_mapreg_type(pa->pa_pc, pa->pa_tag, AHCI_PCI_BAR);
	if (pci_mapreg_map(pa, AHCI_PCI_BAR, maptype, 0, &sc->sc_iot,
	    &sc->sc_ioh, NULL, &sc->sc_ios, 0) != 0) {
		printf(" unable to map registers\n");
		return (1);
	}

	return (0);
}

void
ahci_unmap_regs(struct ahci_pci_softc *psc)
{
	struct ahci_softc		*sc = &psc->psc_ahci;

	bus_space_unmap(sc->sc_iot, sc->sc_ioh, sc->sc_ios);
	sc->sc_ios = 0;
}

int
ahci_map_intr(struct ahci_pci_softc *psc, struct pci_attach_args *pa,
    pci_intr_handle_t ih)
{
	struct ahci_softc		*sc = &psc->psc_ahci;
	sc->sc_ih = pci_intr_establish(psc->psc_pc, ih, IPL_BIO,
	    ahci_intr, sc, DEVNAME(sc));
	if (sc->sc_ih == NULL) {
		printf("%s: unable to map interrupt\n", DEVNAME(sc));
		return (1);
	}

	return (0);
}

void
ahci_unmap_intr(struct ahci_pci_softc *psc)
{
	struct ahci_softc		*sc = &psc->psc_ahci;
	pci_intr_disestablish(psc->psc_pc, sc->sc_ih);
}

int
ahci_pci_activate(struct device *self, int act)
{
	struct ahci_pci_softc		*psc = (struct ahci_pci_softc *)self;
	struct ahci_softc		*sc = &psc->psc_ahci;
	return ahci_activate((struct device *)sc, act);
}
