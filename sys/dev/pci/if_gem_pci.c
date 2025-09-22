/*	$OpenBSD: if_gem_pci.c,v 1.41 2024/05/24 06:02:53 jsg Exp $	*/
/*	$NetBSD: if_gem_pci.c,v 1.1 2001/09/16 00:11:42 eeh Exp $ */

/*
 *
 * Copyright (C) 2001 Eduardo Horvath.
 * All rights reserved.
 *
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR  ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR  BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

/*
 * PCI bindings for Sun GEM ethernet controllers.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>

#include <net/if.h>
#include <net/if_media.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <machine/bus.h>
#include <machine/intr.h>

#ifdef __sparc64__
#include <dev/ofw/openfirm.h>
#endif

#include <dev/mii/miivar.h>

#include <dev/ic/gemreg.h>
#include <dev/ic/gemvar.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcidevs.h>

struct gem_pci_softc {
	struct	gem_softc	gsc_gem;	/* GEM device */
	bus_space_tag_t		gsc_memt;
	bus_space_handle_t	gsc_memh;
	bus_size_t		gsc_memsize;
	pci_chipset_tag_t	gsc_pc;
};

int	gem_match_pci(struct device *, void *, void *);
void	gem_attach_pci(struct device *, struct device *, void *);
int	gem_detach_pci(struct device *, int);
int	gem_pci_enaddr(struct gem_softc *, struct pci_attach_args *);

const struct cfattach gem_pci_ca = {
	sizeof(struct gem_pci_softc), gem_match_pci, gem_attach_pci,
	gem_detach_pci
};

/*
 * Attach routines need to be split out to different bus-specific files.
 */

const struct pci_matchid gem_pci_devices[] = {
	{ PCI_VENDOR_SUN, PCI_PRODUCT_SUN_ERINETWORK },
	{ PCI_VENDOR_SUN, PCI_PRODUCT_SUN_GEMNETWORK },
	{ PCI_VENDOR_APPLE, PCI_PRODUCT_APPLE_INTREPID2_GMAC },
	{ PCI_VENDOR_APPLE, PCI_PRODUCT_APPLE_K2_GMAC },
	{ PCI_VENDOR_APPLE, PCI_PRODUCT_APPLE_PANGEA_GMAC },
	{ PCI_VENDOR_APPLE, PCI_PRODUCT_APPLE_SHASTA_GMAC },
	{ PCI_VENDOR_APPLE, PCI_PRODUCT_APPLE_UNINORTHGMAC },
	{ PCI_VENDOR_APPLE, PCI_PRODUCT_APPLE_UNINORTH2GMAC },
};

int
gem_match_pci(struct device *parent, void *cf, void *aux)
{
	return (pci_matchbyid((struct pci_attach_args *)aux, gem_pci_devices,
	    nitems(gem_pci_devices)));
}

#define	PROMHDR_PTR_DATA	0x18
#define	PROMDATA_PTR_VPD	0x08
#define	PROMDATA_DATA2		0x0a

static const u_int8_t gem_promhdr[] = { 0x55, 0xaa };
static const u_int8_t gem_promdat[] = {
	'P', 'C', 'I', 'R',
	PCI_VENDOR_SUN & 0xff, PCI_VENDOR_SUN >> 8,
	PCI_PRODUCT_SUN_GEMNETWORK & 0xff, PCI_PRODUCT_SUN_GEMNETWORK >> 8
};

static const u_int8_t gem_promdat2[] = {
	0x18, 0x00,			/* structure length */
	0x00,				/* structure revision */
	0x00,				/* interface revision */
	PCI_SUBCLASS_NETWORK_ETHERNET,	/* subclass code */
	PCI_CLASS_NETWORK		/* class code */
};

int
gem_pci_enaddr(struct gem_softc *sc, struct pci_attach_args *pa)
{
	struct pci_vpd *vpd;
	bus_space_handle_t romh;
	bus_space_tag_t romt;
	bus_size_t romsize = 0;
	u_int8_t buf[32];
	pcireg_t address;
	int dataoff, vpdoff;
	int rv = -1;

	if (pci_mapreg_map(pa, PCI_ROM_REG, PCI_MAPREG_TYPE_MEM, 0,
	    &romt, &romh, 0, &romsize, 0))
		return (-1);

	address = pci_conf_read(pa->pa_pc, pa->pa_tag, PCI_ROM_REG);
	address |= PCI_ROM_ENABLE;
	pci_conf_write(pa->pa_pc, pa->pa_tag, PCI_ROM_REG, address);

	bus_space_read_region_1(romt, romh, 0, buf, sizeof(buf));
	if (bcmp(buf, gem_promhdr, sizeof(gem_promhdr)))
		goto fail;

	dataoff = buf[PROMHDR_PTR_DATA] | (buf[PROMHDR_PTR_DATA + 1] << 8);
	if (dataoff < 0x1c)
		goto fail;

	bus_space_read_region_1(romt, romh, dataoff, buf, sizeof(buf));
	if (bcmp(buf, gem_promdat, sizeof(gem_promdat)) ||
	    bcmp(buf + PROMDATA_DATA2, gem_promdat2, sizeof(gem_promdat2)))
		goto fail;

	vpdoff = buf[PROMDATA_PTR_VPD] | (buf[PROMDATA_PTR_VPD + 1] << 8);
	if (vpdoff < 0x1c)
		goto fail;

	bus_space_read_region_1(romt, romh, vpdoff, buf, sizeof(buf));

	/*
	 * The VPD of gem is not in PCI 2.2 standard format.  The length
	 * in the resource header is in big endian.
	 */
	vpd = (struct pci_vpd *)(buf + 3);
	if (!PCI_VPDRES_ISLARGE(buf[0]) ||
	    PCI_VPDRES_LARGE_NAME(buf[0]) != PCI_VPDRES_TYPE_VPD)
		goto fail;
	if (vpd->vpd_key0 != 'N' || vpd->vpd_key1 != 'A')
		goto fail;

	bcopy(buf + 6, sc->sc_arpcom.ac_enaddr, ETHER_ADDR_LEN);
	rv = 0;

 fail:
	if (romsize != 0)
		bus_space_unmap(romt, romh, romsize);

	address = pci_conf_read(pa->pa_pc, pa->pa_tag, PCI_ROM_REG);
	address &= ~PCI_ROM_ENABLE;
	pci_conf_write(pa->pa_pc, pa->pa_tag, PCI_ROM_REG, address);

	return (rv);
}

void
gem_attach_pci(struct device *parent, struct device *self, void *aux)
{
	struct pci_attach_args *pa = aux;
	struct gem_pci_softc *gsc = (void *)self;
	struct gem_softc *sc = &gsc->gsc_gem;
	pci_intr_handle_t ih;
#ifdef __sparc64__
	/* XXX the following declarations should be elsewhere */
	extern void myetheraddr(u_char *);
#endif
	const char *intrstr = NULL;
	int type, gotenaddr = 0;

	gsc->gsc_pc = pa->pa_pc;

	if (pa->pa_memt) {
		type = PCI_MAPREG_TYPE_MEM;
		sc->sc_bustag = pa->pa_memt;
	} else {
		type = PCI_MAPREG_TYPE_IO;
		sc->sc_bustag = pa->pa_iot;
	}

	sc->sc_dmatag = pa->pa_dmat;

	sc->sc_pci = 1; /* XXXXX should all be done in bus_dma. */

	switch (PCI_PRODUCT(pa->pa_id)) {
	case PCI_PRODUCT_SUN_GEMNETWORK:
		sc->sc_variant = GEM_SUN_GEM;
		break;
	case PCI_PRODUCT_SUN_ERINETWORK:
		sc->sc_variant = GEM_SUN_ERI;
		break;
	case PCI_PRODUCT_APPLE_K2_GMAC:
		sc->sc_variant = GEM_APPLE_K2_GMAC;
		break;
	default:
		sc->sc_variant = GEM_APPLE_GMAC;
	}

#define PCI_GEM_BASEADDR	0x10
	if (pci_mapreg_map(pa, PCI_GEM_BASEADDR, type, 0,
	    &gsc->gsc_memt, &gsc->gsc_memh, NULL, &gsc->gsc_memsize, 0) != 0) {
		printf(": can't map registers\n");
		return;
	}

	sc->sc_bustag = gsc->gsc_memt;
	sc->sc_h1 = gsc->gsc_memh;

	if (bus_space_subregion(sc->sc_bustag, sc->sc_h1,
	    GEM_PCI_BANK2_OFFSET, GEM_PCI_BANK2_SIZE, &sc->sc_h2)) {
		printf(": unable to create bank 2 subregion\n");
		bus_space_unmap(gsc->gsc_memt, gsc->gsc_memh, gsc->gsc_memsize);
		return;
	}

	if (gem_pci_enaddr(sc, pa) == 0)
		gotenaddr = 1;

#ifdef __sparc64__
	if (!gotenaddr) {
		if (OF_getprop(PCITAG_NODE(pa->pa_tag), "local-mac-address",
		    sc->sc_arpcom.ac_enaddr, ETHER_ADDR_LEN) <= 0)
			myetheraddr(sc->sc_arpcom.ac_enaddr);
		gotenaddr = 1;
	}
#endif
#ifdef __powerpc__
	if (!gotenaddr) {
		pci_ether_hw_addr(pa->pa_pc, sc->sc_arpcom.ac_enaddr);
		gotenaddr = 1;
	}
#endif

	sc->sc_burst = 16;	/* XXX */

	if (pci_intr_map(pa, &ih) != 0) {
		printf(": couldn't map interrupt\n");
		bus_space_unmap(gsc->gsc_memt, gsc->gsc_memh, gsc->gsc_memsize);
		return;
	}
	intrstr = pci_intr_string(pa->pa_pc, ih);
	sc->sc_ih = pci_intr_establish(pa->pa_pc,
	    ih, IPL_NET | IPL_MPSAFE, gem_intr, sc, self->dv_xname);
	if (sc->sc_ih == NULL) {
		printf(": couldn't establish interrupt");
		if (intrstr != NULL)
			printf(" at %s", intrstr);
		printf("\n");
		bus_space_unmap(gsc->gsc_memt, gsc->gsc_memh, gsc->gsc_memsize);
		return;
	}

	printf(": %s", intrstr);

	/*
	 * call the main configure
	 */
	gem_config(sc);
}

int
gem_detach_pci(struct device *self, int flags)
{
	struct gem_pci_softc *gsc = (void *)self;
	struct gem_softc *sc = &gsc->gsc_gem;

	timeout_del(&sc->sc_tick_ch);
	timeout_del(&sc->sc_rx_watchdog);

	gem_unconfig(sc);
	pci_intr_disestablish(gsc->gsc_pc, sc->sc_ih);
	bus_space_unmap(gsc->gsc_memt, gsc->gsc_memh, gsc->gsc_memsize);

	return (0);
}
