/*	$OpenBSD: if_wi_pci.c,v 1.57 2024/05/24 06:02:57 jsg Exp $	*/

/*
 * Copyright (c) 2001-2003 Todd C. Miller <millert@openbsd.org>
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
 *
 * Sponsored in part by the Defense Advanced Research Projects
 * Agency (DARPA) and Air Force Research Laboratory, Air Force
 * Materiel Command, USAF, under agreement number F39502-99-1-0512.
 */

/*
 * PCI attachment for the Wavelan driver.  There are two basic types
 * of PCI card supported:
 *
 * 1) Cards based on the Prism2.5 Mini-PCI chipset
 * 2) Cards that use a dumb PCMCIA->PCI bridge
 *
 * Only the first type are "true" PCI cards.
 *
 * The latter are simply PCMCIA cards (or the guts of same) with some
 * type of dumb PCMCIA->PCI bridge.  They are "dumb" in that they
 * are not true PCMCIA bridges and really just serve to deal with
 * the different interrupt types and timings of the ISA vs. PCI bus.
 *
 * The following bridge types are supported:
 *  o PLX 9052 (the most common)
 *  o TMD 7160 (found in some NDC/Sohoware NCP130 cards)
 *  o ACEX EP1K30 (really a PLD, found in Symbol cards and their OEMs)
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/timeout.h>

#include <net/if.h>
#include <net/if_media.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_ioctl.h>

#include <machine/bus.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#include <dev/ic/if_wireg.h>
#include <dev/ic/if_wi_ieee.h>
#include <dev/ic/if_wivar.h>

/* For printing CIS of the actual PCMCIA card */
#define CIS_MFG_NAME_OFFSET	0x16
#define CIS_INFO_SIZE		256

const struct wi_pci_product *wi_pci_lookup(struct pci_attach_args *pa);
int	wi_pci_match(struct device *, void *, void *);
void	wi_pci_attach(struct device *, struct device *, void *);
int	wi_pci_activate(struct device *, int);
void	wi_pci_wakeup(struct wi_softc *);
int	wi_pci_acex_attach(struct pci_attach_args *pa, struct wi_softc *sc);
int	wi_pci_plx_attach(struct pci_attach_args *pa, struct wi_softc *sc);
int	wi_pci_tmd_attach(struct pci_attach_args *pa, struct wi_softc *sc);
int	wi_pci_native_attach(struct pci_attach_args *pa, struct wi_softc *sc);
int	wi_pci_common_attach(struct pci_attach_args *pa, struct wi_softc *sc);
void	wi_pci_plx_print_cis(struct wi_softc *);

struct wi_pci_softc {
	struct wi_softc		 sc_wi;		/* real softc */
};

const struct cfattach wi_pci_ca = {
	sizeof (struct wi_pci_softc), wi_pci_match, wi_pci_attach, NULL,
	wi_pci_activate
};

static const struct wi_pci_product {
	pci_vendor_id_t pp_vendor;
	pci_product_id_t pp_product;
	int (*pp_attach)(struct pci_attach_args *pa, struct wi_softc *sc);
} wi_pci_products[] = {
	{ PCI_VENDOR_GLOBALSUN, PCI_PRODUCT_GLOBALSUN_GL24110P, wi_pci_plx_attach },
	{ PCI_VENDOR_GLOBALSUN, PCI_PRODUCT_GLOBALSUN_GL24110P02, wi_pci_plx_attach },
	{ PCI_VENDOR_GLOBALSUN, PCI_PRODUCT_GLOBALSUN_GL24110P03, wi_pci_plx_attach },
	{ PCI_VENDOR_GLOBALSUN, PCI_PRODUCT_GLOBALSUN_8031, wi_pci_plx_attach },
	{ PCI_VENDOR_EUMITCOM, PCI_PRODUCT_EUMITCOM_WL11000P, wi_pci_plx_attach },
	{ PCI_VENDOR_USR2, PCI_PRODUCT_USR2_WL11000P, wi_pci_plx_attach },
	{ PCI_VENDOR_3COM, PCI_PRODUCT_3COM_3CRWE777A, wi_pci_plx_attach },
	{ PCI_VENDOR_NETGEAR, PCI_PRODUCT_NETGEAR_MA301, wi_pci_plx_attach },
	{ PCI_VENDOR_EFFICIENTNETS, PCI_PRODUCT_EFFICIENTNETS_SS1023, wi_pci_plx_attach },
	{ PCI_VENDOR_ADDTRON, PCI_PRODUCT_ADDTRON_AWA100, wi_pci_plx_attach },
	{ PCI_VENDOR_BELKIN, PCI_PRODUCT_BELKIN_F5D6000, wi_pci_plx_attach },
	{ PCI_VENDOR_NDC, PCI_PRODUCT_NDC_NCP130, wi_pci_plx_attach },
	{ PCI_VENDOR_NDC, PCI_PRODUCT_NDC_NCP130A2, wi_pci_tmd_attach },
	{ PCI_VENDOR_INTERSIL, PCI_PRODUCT_INTERSIL_MINI_PCI_WLAN, wi_pci_native_attach },
	{ PCI_VENDOR_INTERSIL, PCI_PRODUCT_INTERSIL_ISL3872, wi_pci_native_attach },
	{ PCI_VENDOR_SAMSUNG, PCI_PRODUCT_SAMSUNG_SWL2210P, wi_pci_native_attach },
	{ PCI_VENDOR_NORTEL, PCI_PRODUCT_NORTEL_211818A, wi_pci_acex_attach },
	{ PCI_VENDOR_SYMBOL, PCI_PRODUCT_SYMBOL_LA41X3, wi_pci_acex_attach },
	{ 0, 0, 0 }
};

const struct wi_pci_product *
wi_pci_lookup(struct pci_attach_args *pa)
{
	const struct wi_pci_product *pp;

	for (pp = wi_pci_products; pp->pp_product != 0; pp++) {
		if (PCI_VENDOR(pa->pa_id) == pp->pp_vendor && 
		    PCI_PRODUCT(pa->pa_id) == pp->pp_product)
			return (pp);
	}

	return (NULL);
}

int
wi_pci_match(struct device *parent, void *match, void *aux)
{
	return (wi_pci_lookup(aux) != NULL);
}

void
wi_pci_attach(struct device *parent, struct device *self, void *aux)
{
	struct wi_softc *sc = (struct wi_softc *)self;
	struct pci_attach_args *pa = aux;
	const struct wi_pci_product *pp;

	pp = wi_pci_lookup(pa);
	if (pp->pp_attach(pa, sc) != 0)
		return;
	printf("\n");
	wi_attach(sc, &wi_func_io);
}

int
wi_pci_activate(struct device *self, int act)
{
	struct wi_softc *sc = (struct wi_softc *)self;
	struct ifnet *ifp = &sc->sc_ic.ic_if;

	switch (act) {
	case DVACT_SUSPEND:
		if (ifp->if_flags & IFF_RUNNING)
			wi_stop(sc);
		break;
	case DVACT_WAKEUP:
		if (ifp->if_flags & IFF_UP)
			wi_pci_wakeup(sc);
		break;
	}
	return (0);
}

void
wi_pci_wakeup(struct wi_softc *sc)
{
	int s;

	s = splnet();
	while (sc->wi_flags & WI_FLAGS_BUSY)
		tsleep_nsec(&sc->wi_flags, 0, "wipwr", INFSLP);
	sc->wi_flags |= WI_FLAGS_BUSY;

	wi_init(sc);

	sc->wi_flags &= ~WI_FLAGS_BUSY;
	wakeup(&sc->wi_flags);
	splx(s);
}

/*
 * ACEX EP1K30-based PCMCIA->PCI bridge attachment.
 *
 * The ACEX EP1K30 is a programmable logic device (PLD) used as a
 * PCMCIA->PCI bridge on the Symbol LA4123 and its OEM equivalents
 * (such as the Nortel E-mobility 211818-A).  There are 3 I/O ports:
 * BAR0 at 0x10 appears to be a command port.
 * BAR1 at 0x14 contains COR at offset 0xe0.
 * BAR2 at 0x18 maps the actual PCMCIA card.
 *
 * The datasheet for the ACEX EP1K30 is available from Altera but that
 * doesn't really help much since we don't know how it is programmed.
 * Details for this attachment were gleaned from a version of the
 * Linux orinoco driver modified by Tobias Hoffmann based on
 * what he discovered from the Windows driver.
 */
int
wi_pci_acex_attach(struct pci_attach_args *pa, struct wi_softc *sc)
{
	bus_space_handle_t commandh, localh, ioh;
	bus_space_tag_t commandt, localt;
	bus_space_tag_t iot = pa->pa_iot;
	bus_size_t commandsize, localsize, iosize;
	int i;

	if (pci_mapreg_map(pa, WI_ACEX_CMDRES, PCI_MAPREG_TYPE_IO,
	    0, &commandt, &commandh, NULL, &commandsize, 0) != 0) {
		printf(": can't map command i/o space\n");
		return (ENXIO);
	}

	if (pci_mapreg_map(pa, WI_ACEX_LOCALRES, PCI_MAPREG_TYPE_IO,
	    0, &localt, &localh, NULL, &localsize, 0) != 0) {
		printf(": can't map local i/o space\n");
		bus_space_unmap(commandt, commandh, commandsize);
		return (ENXIO);
	}
	sc->wi_ltag = localt;
	sc->wi_lhandle = localh;

	if (pci_mapreg_map(pa, WI_TMD_IORES, PCI_MAPREG_TYPE_IO,
	    0, &iot, &ioh, NULL, &iosize, 0) != 0) {
		printf(": can't map i/o space\n");
		bus_space_unmap(localt, localh, localsize);
		bus_space_unmap(commandt, commandh, commandsize);
		return (ENXIO);
	}
	sc->wi_btag = iot;
	sc->wi_bhandle = ioh;

	/*
	 * Setup bridge chip.
	 */
	if (bus_space_read_4(commandt, commandh, 0) & 1) {
		printf(": bridge not ready\n");
		bus_space_unmap(iot, ioh, iosize);
		bus_space_unmap(localt, localh, localsize);
		bus_space_unmap(commandt, commandh, commandsize);
		return (ENXIO);
	}
	bus_space_write_4(commandt, commandh, 2, 0x118);
	bus_space_write_4(commandt, commandh, 2, 0x108);
	DELAY(30 * 1000);
	bus_space_write_4(commandt, commandh, 2, 0x8);
	for (i = 0; i < 30; i++) {
		DELAY(30 * 1000);
		if (bus_space_read_4(commandt, commandh, 0) & 0x10)
			break;
	}
	if (i == 30) {
		printf(": bridge timeout\n");
		bus_space_unmap(iot, ioh, iosize);
		bus_space_unmap(localt, localh, localsize);
		bus_space_unmap(commandt, commandh, commandsize);
		return (ENXIO);
	}
	if ((bus_space_read_4(localt, localh, 0xe0) & 1) ||
	    (bus_space_read_4(localt, localh, 0xe2) & 1) ||
	    (bus_space_read_4(localt, localh, 0xe4) & 1)) {
		printf(": failed bridge setup\n");
		bus_space_unmap(iot, ioh, iosize);
		bus_space_unmap(localt, localh, localsize);
		bus_space_unmap(commandt, commandh, commandsize);
		return (ENXIO);
	}

	if (wi_pci_common_attach(pa, sc) != 0) {
		bus_space_unmap(iot, ioh, iosize);
		bus_space_unmap(localt, localh, localsize);
		bus_space_unmap(commandt, commandh, commandsize);
		return (ENXIO);
	}

	/* 
	 * Enable I/O mode and level interrupts on the embedded PCMCIA
	 * card.
	 */
	bus_space_write_1(localt, localh, WI_ACEX_COR_OFFSET, WI_COR_IOMODE);
	sc->wi_cor_offset = WI_ACEX_COR_OFFSET;

	/* Unmap registers we no longer need access to. */
	bus_space_unmap(commandt, commandh, commandsize);

	return (0);
}

/*
 * PLX 9052-based PCMCIA->PCI bridge attachment.
 *
 * These are often sold as "PCI wireless card adapters" and are
 * sold by several vendors.  Most are simply rebadged versions of the
 * Eumitcom WL11000P or Global Sun Technology GL24110P02.
 * These cards use the PLX 9052 dumb bridge chip to connect a PCMCIA
 * wireless card to the PCI bus.  Because it is a dumb bridge and
 * not a true PCMCIA bridge, the PCMCIA subsystem is not involved
 * (or even required).  The PLX 9052 provides multiple PCI address
 * space mappings.  The primary mappings at PCI registers 0x10 (mem)
 * and 0x14 (I/O) are for the PLX chip itself, *NOT* the PCMCIA card.
 * The mem and I/O spaces for the PCMCIA card are mapped to 0x18 and
 * 0x1C respectively.
 * The PLX 9050/9052 datasheet may be downloaded from PLX at
 *	http://www.plxtech.com/products/toolbox/9050.htm
 */
int
wi_pci_plx_attach(struct pci_attach_args *pa, struct wi_softc *sc)
{
	bus_space_handle_t localh, ioh, memh;
	bus_space_tag_t localt;
	bus_space_tag_t iot = pa->pa_iot;
	bus_space_tag_t memt = pa->pa_memt;
	bus_size_t localsize, memsize, iosize;
	u_int32_t intcsr;

	if (pci_mapreg_map(pa, WI_PLX_MEMRES, PCI_MAPREG_TYPE_MEM, 0,
	    &memt, &memh, NULL, &memsize, 0) != 0) {
		printf(": can't map mem space\n");
		return (ENXIO);
	}
	sc->wi_ltag = memt;
	sc->wi_lhandle = memh;

	if (pci_mapreg_map(pa, WI_PLX_IORES,
	    PCI_MAPREG_TYPE_IO, 0, &iot, &ioh, NULL, &iosize, 0) != 0) {
		printf(": can't map i/o space\n");
		bus_space_unmap(memt, memh, memsize);
		return (ENXIO);
	}
	sc->wi_btag = iot;
	sc->wi_bhandle = ioh;

	/*
	 * Some cards, such as the PLX version of the NDC NCP130,
	 * don't have the PLX local registers mapped.  In general
	 * this is OK since on those cards the serial EEPROM has
	 * already set things up for us.
	 * As such, we don't consider an error here to be fatal.
	 */
	localsize = 0;
	if (pci_mapreg_type(pa->pa_pc, pa->pa_tag, WI_PLX_LOCALRES)
	    == PCI_MAPREG_TYPE_IO) {
		if (pci_mapreg_map(pa, WI_PLX_LOCALRES, PCI_MAPREG_TYPE_IO,
		    0, &localt, &localh, NULL, &localsize, 0) != 0)
			printf(": can't map PLX I/O space\n");
	}

	if (wi_pci_common_attach(pa, sc) != 0) {
		if (localsize)
			bus_space_unmap(localt, localh, localsize);
		bus_space_unmap(iot, ioh, iosize);
		bus_space_unmap(memt, memh, memsize);
		return (ENXIO);
	}

	if (localsize != 0) {
		intcsr = bus_space_read_4(localt, localh,
		    WI_PLX_INTCSR);

		/*
		 * The Netgear MA301 has local interrupt 1 active
		 * when there is no card in the adapter.  We bail
		 * early in this case since our attempt to check
		 * for the presence of a card later will hang the
		 * MA301.
		 */
		if (intcsr & WI_PLX_LINT1STAT) {
			printf("\n%s: no PCMCIA card detected in bridge card\n",
			    WI_PRT_ARG(sc));
			pci_intr_disestablish(pa->pa_pc, sc->sc_ih);
			if (localsize)
				bus_space_unmap(localt, localh, localsize);
			bus_space_unmap(iot, ioh, iosize);
			bus_space_unmap(memt, memh, memsize);
			return (ENXIO);
		}

		/*
		 * Enable PCI interrupts on the PLX chip if they are
		 * not already enabled. On most adapters the serial
		 * EEPROM has done this for us but some (such as
		 * the Netgear MA301) do not.
		 */
		if (!(intcsr & WI_PLX_INTEN)) {
			intcsr |= WI_PLX_INTEN;
			bus_space_write_4(localt, localh, WI_PLX_INTCSR,
			    intcsr);
		}
	}

	/*
	 * Enable I/O mode and level interrupts on the PCMCIA card.
	 * The PCMCIA card's COR is the first byte after the CIS.
	 */
	bus_space_write_1(memt, memh, WI_PLX_COR_OFFSET, WI_COR_IOMODE);
	sc->wi_cor_offset = WI_PLX_COR_OFFSET;

	if (localsize != 0) {
		/*
		 * Test the presence of a wi(4) card by writing
		 * a magic number to the first software support
		 * register and then reading it back.
		 */
		CSR_WRITE_2(sc, WI_SW0, WI_DRVR_MAGIC);
		DELAY(1000);
		if (CSR_READ_2(sc, WI_SW0) != WI_DRVR_MAGIC) {
			printf("\n%s: no PCMCIA card detected in bridge card\n",
			    WI_PRT_ARG(sc));
			pci_intr_disestablish(pa->pa_pc, sc->sc_ih);
			if (localsize)
				bus_space_unmap(localt, localh, localsize);
			bus_space_unmap(iot, ioh, iosize);
			bus_space_unmap(memt, memh, memsize);
			return (ENXIO);
		}

		/* Unmap registers we no longer need access to. */
		bus_space_unmap(localt, localh, localsize);

		/* Print PCMCIA card's CIS strings. */
		wi_pci_plx_print_cis(sc);
	}

	return (0);
}

/*
 * TMD 7160-based PCMCIA->PCI bridge attachment.
 *
 * The TMD7160 dumb bridge chip is used on some versions of the
 * NDC/Sohoware NCP130.  The TMD7160 provides two PCI I/O registers.
 * The first, at 0x14, maps to the Prism2 COR.
 * The second, at 0x18, is for the Prism2 chip itself.
 *
 * The datasheet for the TMD7160 does not seem to be publicly available.
 * Details for this attachment were gleaned from a version of the
 * Linux WLAN driver modified by NDC.
 */
int
wi_pci_tmd_attach(struct pci_attach_args *pa, struct wi_softc *sc)
{
	bus_space_handle_t localh, ioh;
	bus_space_tag_t localt;
	bus_space_tag_t iot = pa->pa_iot;
	bus_size_t localsize, iosize;

	if (pci_mapreg_map(pa, WI_TMD_LOCALRES, PCI_MAPREG_TYPE_IO,
	    0, &localt, &localh, NULL, &localsize, 0) != 0) {
		printf(": can't map TMD I/O space\n");
		return (ENXIO);
	}
	sc->wi_ltag = localt;
	sc->wi_lhandle = localh;

	if (pci_mapreg_map(pa, WI_TMD_IORES, PCI_MAPREG_TYPE_IO,
	    0, &iot, &ioh, NULL, &iosize, 0) != 0) {
		printf(": can't map i/o space\n");
		bus_space_unmap(localt, localh, localsize);
		return (ENXIO);
	}
	sc->wi_btag = iot;
	sc->wi_bhandle = ioh;

	if (wi_pci_common_attach(pa, sc) != 0) {
		bus_space_unmap(iot, ioh, iosize);
		bus_space_unmap(localt, localh, localsize);
		return (ENXIO);
	}

	/* 
	 * Enable I/O mode and level interrupts on the embedded PCMCIA
	 * card.  The PCMCIA card's COR is the first byte of BAR 0.
	 */
	bus_space_write_1(localt, localh, 0, WI_COR_IOMODE);
	sc->wi_cor_offset = 0;

	return (0);
}

int
wi_pci_native_attach(struct pci_attach_args *pa, struct wi_softc *sc)
{
	bus_space_handle_t ioh;
	bus_space_tag_t iot = pa->pa_iot;
	bus_size_t iosize;

	if (pci_mapreg_map(pa, WI_PCI_CBMA, PCI_MAPREG_TYPE_MEM,
	    0, &iot, &ioh, NULL, &iosize, 0) != 0) {
		printf(": can't map mem space\n");
		return (ENXIO);
	}
	sc->wi_ltag = iot;
	sc->wi_lhandle = ioh;
	sc->wi_btag = iot;
	sc->wi_bhandle = ioh;
	sc->sc_pci = 1;

	if (wi_pci_common_attach(pa, sc) != 0) {
		bus_space_unmap(iot, ioh, iosize);
		return (ENXIO);
	}

	/* Do a soft reset of the HFA3842 MAC core */
	bus_space_write_2(iot, ioh, WI_PCI_COR_OFFSET, WI_COR_SOFT_RESET);
	DELAY(100*1000); /* 100 m sec */
	bus_space_write_2(iot, ioh, WI_PCI_COR_OFFSET, WI_COR_CLEAR);
	DELAY(100*1000); /* 100 m sec */
	sc->wi_cor_offset = WI_PCI_COR_OFFSET;

	return (0);
}

int
wi_pci_common_attach(struct pci_attach_args *pa, struct wi_softc *sc)
{
	pci_intr_handle_t ih;
	pci_chipset_tag_t pc = pa->pa_pc;
	const char *intrstr;

	/* Make sure interrupts are disabled. */
	CSR_WRITE_2(sc, WI_INT_EN, 0);
	CSR_WRITE_2(sc, WI_EVENT_ACK, 0xFFFF);

	/* Map and establish the interrupt. */
	if (pci_intr_map(pa, &ih)) {
		printf(": couldn't map interrupt\n");
		return (ENXIO);
	}
	intrstr = pci_intr_string(pc, ih);
	sc->sc_ih = pci_intr_establish(pc, ih, IPL_NET, wi_intr, sc,
	    sc->sc_dev.dv_xname);
	if (sc->sc_ih == NULL) {
		printf(": couldn't establish interrupt");
		if (intrstr != NULL)
			printf(" at %s", intrstr);
		printf("\n");
		return (ENXIO);
	}
	printf(": %s", intrstr);

	return (0);
}

void
wi_pci_plx_print_cis(struct wi_softc *sc)
{
	int i, stringno;
	char cisbuf[CIS_INFO_SIZE];
	char *cis_strings[3];
	u_int8_t value;
	const u_int8_t cis_magic[] = {
		0x01, 0x03, 0x00, 0x00, 0xff, 0x17, 0x04, 0x67
	};

	/* Make sure the CIS data is valid. */
	for (i = 0; i < 8; i++) {
		value = bus_space_read_1(sc->wi_ltag, sc->wi_lhandle, i * 2);
		if (value != cis_magic[i])
			return;
	}

	cis_strings[0] = cisbuf;
	stringno = 0;
	for (i = 0; i < CIS_INFO_SIZE && stringno < 3; i++) {
		cisbuf[i] = bus_space_read_1(sc->wi_ltag,
		    sc->wi_lhandle, (CIS_MFG_NAME_OFFSET + i) * 2);
		if (cisbuf[i] == '\0' && ++stringno < 3)
			cis_strings[stringno] = &cisbuf[i + 1];
	}
	cisbuf[CIS_INFO_SIZE - 1] = '\0';
	printf("\n%s: \"%s, %s, %s\"", WI_PRT_ARG(sc),
	    cis_strings[0], cis_strings[1], cis_strings[2]);
}
