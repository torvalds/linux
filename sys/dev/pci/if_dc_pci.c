/*	$OpenBSD: if_dc_pci.c,v 1.79 2024/05/24 06:02:53 jsg Exp $	*/

/*
 * Copyright (c) 1997, 1998, 1999
 *	Bill Paul <wpaul@ee.columbia.edu>.  All rights reserved.
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
 *	This product includes software developed by Bill Paul.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY Bill Paul AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL Bill Paul OR THE VOICES IN HIS HEAD
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/pci/if_dc.c,v 1.5 2000/01/12 22:24:05 wpaul Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/timeout.h>
#include <sys/malloc.h>
#include <sys/device.h>

#include <net/if.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <net/if_media.h>

#include <dev/mii/miivar.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#ifdef __sparc64__
#include <dev/ofw/openfirm.h>
#endif

#ifndef __hppa__
#define DC_USEIOSPACE
#endif

#include <dev/ic/dcreg.h>

/*
 * Various supported device vendors/types and their names.
 */
struct dc_type dc_devs[] = {
	{ PCI_VENDOR_DEC, PCI_PRODUCT_DEC_21140 },
	{ PCI_VENDOR_DEC, PCI_PRODUCT_DEC_21142 },
	{ PCI_VENDOR_DAVICOM, PCI_PRODUCT_DAVICOM_DM9009 },
	{ PCI_VENDOR_DAVICOM, PCI_PRODUCT_DAVICOM_DM9100 },
	{ PCI_VENDOR_DAVICOM, PCI_PRODUCT_DAVICOM_DM9102 },
	{ PCI_VENDOR_ADMTEK, PCI_PRODUCT_ADMTEK_ADM9511 },
	{ PCI_VENDOR_ADMTEK, PCI_PRODUCT_ADMTEK_ADM9513 },
	{ PCI_VENDOR_ADMTEK, PCI_PRODUCT_ADMTEK_AL981 },
	{ PCI_VENDOR_ADMTEK, PCI_PRODUCT_ADMTEK_AN983 },
	{ PCI_VENDOR_ASIX, PCI_PRODUCT_ASIX_AX88140A },
	{ PCI_VENDOR_MACRONIX, PCI_PRODUCT_MACRONIX_MX98713 },
	{ PCI_VENDOR_MACRONIX, PCI_PRODUCT_MACRONIX_MX98715 },
	{ PCI_VENDOR_MACRONIX, PCI_PRODUCT_MACRONIX_MX98727 },
	{ PCI_VENDOR_COMPEX, PCI_PRODUCT_COMPEX_98713 },
	{ PCI_VENDOR_LITEON, PCI_PRODUCT_LITEON_PNIC },
	{ PCI_VENDOR_LITEON, PCI_PRODUCT_LITEON_PNICII },
	{ PCI_VENDOR_ACCTON, PCI_PRODUCT_ACCTON_EN1217 },
	{ PCI_VENDOR_ACCTON, PCI_PRODUCT_ACCTON_EN2242 },
	{ PCI_VENDOR_CONEXANT, PCI_PRODUCT_CONEXANT_RS7112 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_21145 },
	{ PCI_VENDOR_3COM, PCI_PRODUCT_3COM_3CSHO100BTX },
	{ PCI_VENDOR_MICROSOFT, PCI_PRODUCT_MICROSOFT_MN130 },
	{ PCI_VENDOR_XIRCOM, PCI_PRODUCT_XIRCOM_X3201_3_21143 },
	{ PCI_VENDOR_ADMTEK, PCI_PRODUCT_ADMTEK_AN985 },
	{ PCI_VENDOR_ABOCOM, PCI_PRODUCT_ABOCOM_FE2500 },
	{ PCI_VENDOR_ABOCOM, PCI_PRODUCT_ABOCOM_FE2500MX },
	{ PCI_VENDOR_ABOCOM, PCI_PRODUCT_ABOCOM_PCM200 },
	{ PCI_VENDOR_DLINK, PCI_PRODUCT_DLINK_DRP32TXD },
	{ PCI_VENDOR_LINKSYS, PCI_PRODUCT_LINKSYS_PCMPC200 },
	{ PCI_VENDOR_LINKSYS, PCI_PRODUCT_LINKSYS_PCM200 },
	{ PCI_VENDOR_HAWKING, PCI_PRODUCT_HAWKING_PN672TX },
	{ PCI_VENDOR_MICROSOFT, PCI_PRODUCT_MICROSOFT_MN120 },
	{ 0, 0 }
};

int dc_pci_match(struct device *, void *, void *);
void dc_pci_attach(struct device *, struct device *, void *);
int dc_pci_detach(struct device *, int);

struct dc_pci_softc {
	struct dc_softc		psc_softc;
	pci_chipset_tag_t	psc_pc;
	bus_size_t		psc_mapsize;
};

/*
 * Probe for a 21143 or clone chip. Check the PCI vendor and device
 * IDs against our list and return a device name if we find a match.
 */
int
dc_pci_match(struct device *parent, void *match, void *aux)
{
	struct pci_attach_args *pa = (struct pci_attach_args *)aux;
	struct dc_type *t;

	/*
	 * Support for the 21140 chip is experimental.  If it works for you,
	 * that's great.  By default, this chip will use de.
	 */
        if (PCI_VENDOR(pa->pa_id) == PCI_VENDOR_DEC &&
	    PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_DEC_21140)
		return (1);

	/*
	 * The following chip revision doesn't seem to work so well with dc,
	 * so let's have de handle it.  (de will return a match of 2)
	 */
        if (PCI_VENDOR(pa->pa_id) == PCI_VENDOR_DEC &&
	    PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_DEC_21142 &&
	    PCI_REVISION(pa->pa_class) == 0x21)
		return (1);

	for (t = dc_devs; t->dc_vid != 0; t++) {
		if ((PCI_VENDOR(pa->pa_id) == t->dc_vid) &&
		    (PCI_PRODUCT(pa->pa_id) == t->dc_did)) {
			return (3);
		}
	}

	return (0);
}

/*
 * Attach the interface. Allocate softc structures, do ifmedia
 * setup and ethernet/BPF attach.
 */
void
dc_pci_attach(struct device *parent, struct device *self, void *aux)
{
	const char		*intrstr = NULL;
	pcireg_t		command;
	struct dc_pci_softc	*psc = (struct dc_pci_softc *)self;
	struct dc_softc		*sc = &psc->psc_softc;
	struct pci_attach_args	*pa = aux;
	pci_chipset_tag_t	pc = pa->pa_pc;
	pci_intr_handle_t	ih;
	int			found = 0;

	psc->psc_pc = pa->pa_pc;
	sc->sc_dmat = pa->pa_dmat;

	pci_set_powerstate(pa->pa_pc, pa->pa_tag, PCI_PMCSR_STATE_D0);

	sc->dc_csid = pci_conf_read(pc, pa->pa_tag, PCI_SUBSYS_ID_REG);

	/*
	 * Map control/status registers.
	 */
#ifdef DC_USEIOSPACE
	if (pci_mapreg_map(pa, DC_PCI_CFBIO,
	    PCI_MAPREG_TYPE_IO, 0,
	    &sc->dc_btag, &sc->dc_bhandle, NULL, &psc->psc_mapsize, 0)) {
		printf(": can't map i/o space\n");
		return;
	}
#else
	if (pci_mapreg_map(pa, DC_PCI_CFBMA,
	    PCI_MAPREG_TYPE_MEM|PCI_MAPREG_MEM_TYPE_32BIT, 0,
	    &sc->dc_btag, &sc->dc_bhandle, NULL, &psc->psc_mapsize, 0)) {
		printf(": can't map mem space\n");
		return;
	}
#endif

	/* Allocate interrupt */
	if (pci_intr_map(pa, &ih)) {
		printf(": couldn't map interrupt\n");
		goto fail_1;
	}
	intrstr = pci_intr_string(pc, ih);
	sc->sc_ih = pci_intr_establish(pc, ih, IPL_NET, dc_intr, sc,
	    self->dv_xname);
	if (sc->sc_ih == NULL) {
		printf(": couldn't establish interrupt");
		if (intrstr != NULL)
			printf(" at %s", intrstr);
		printf("\n");
		goto fail_1;
	}
	printf(": %s", intrstr);

	/* Need this info to decide on a chip type. */
	sc->dc_revision = PCI_REVISION(pa->pa_class);

	/* Get the eeprom width, if possible */
	if ((PCI_VENDOR(pa->pa_id) == PCI_VENDOR_LITEON &&
	      PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_LITEON_PNIC))
		;	/* PNIC has non-standard eeprom */
	else if ((PCI_VENDOR(pa->pa_id) == PCI_VENDOR_XIRCOM &&
	      PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_XIRCOM_X3201_3_21143))
		;	/* XIRCOM has non-standard eeprom */
	else
		dc_eeprom_width(sc);

	switch (PCI_VENDOR(pa->pa_id)) {
	case PCI_VENDOR_DEC:
		if (PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_DEC_21140 ||
		    PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_DEC_21142) {
			found = 1;
			sc->dc_type = DC_TYPE_21143;
			sc->dc_flags |= DC_TX_POLL|DC_TX_USE_TX_INTR;
			sc->dc_flags |= DC_REDUCED_MII_POLL;
			dc_read_srom(sc, sc->dc_romwidth);
		}
		break;
	case PCI_VENDOR_INTEL:
		if (PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_INTEL_21145) {
			found = 1;
			sc->dc_type = DC_TYPE_21145;
			sc->dc_flags |= DC_TX_POLL|DC_TX_USE_TX_INTR;
			sc->dc_flags |= DC_REDUCED_MII_POLL;
			dc_read_srom(sc, sc->dc_romwidth);
		}
		break;
	case PCI_VENDOR_DAVICOM:
		if (PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_DAVICOM_DM9100 ||
		    PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_DAVICOM_DM9102 ||
		    PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_DAVICOM_DM9009) {
			found = 1;
			sc->dc_type = DC_TYPE_DM9102;
			sc->dc_flags |= DC_TX_COALESCE|DC_TX_INTR_ALWAYS;
			sc->dc_flags |= DC_REDUCED_MII_POLL|DC_TX_STORENFWD;
			sc->dc_flags |= DC_TX_ALIGN;
			sc->dc_pmode = DC_PMODE_MII;

			/* Increase the latency timer value. */
			command = pci_conf_read(pc, pa->pa_tag, DC_PCI_CFLT);
			command &= 0xFFFF00FF;
			command |= 0x00008000;
			pci_conf_write(pc, pa->pa_tag, DC_PCI_CFLT, command);
		}
		break;
	case PCI_VENDOR_ADMTEK:
	case PCI_VENDOR_3COM:
	case PCI_VENDOR_MICROSOFT:
		if (PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_ADMTEK_AL981) {
			found = 1;
			sc->dc_type = DC_TYPE_AL981;
			sc->dc_flags |= DC_TX_USE_TX_INTR;
			sc->dc_flags |= DC_TX_ADMTEK_WAR;
			sc->dc_pmode = DC_PMODE_MII;
			dc_read_srom(sc, sc->dc_romwidth);
		}
		if (PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_ADMTEK_ADM9511 ||
		    PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_ADMTEK_ADM9513 ||
		    PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_ADMTEK_AN983 ||
		    PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_3COM_3CSHO100BTX ||
		    PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_MICROSOFT_MN130) {
			found = 1;
			sc->dc_type = DC_TYPE_AN983;
			sc->dc_flags |= DC_TX_USE_TX_INTR;
			sc->dc_flags |= DC_TX_ADMTEK_WAR;
			sc->dc_flags |= DC_64BIT_HASH;
			sc->dc_pmode = DC_PMODE_MII;
			/* Don't read SROM for - auto-loaded on reset */
		}
		break;
	case PCI_VENDOR_MACRONIX:
	case PCI_VENDOR_ACCTON:
		if (PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_ACCTON_EN2242) {
			found = 1;
			sc->dc_type = DC_TYPE_AN983;
			sc->dc_flags |= DC_TX_USE_TX_INTR;
			sc->dc_flags |= DC_TX_ADMTEK_WAR;
			sc->dc_flags |= DC_64BIT_HASH;
			sc->dc_pmode = DC_PMODE_MII;
			/* Don't read SROM for - auto-loaded on reset */
		}
		if (PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_MACRONIX_MX98713) {
			found = 1;
			if (sc->dc_revision < DC_REVISION_98713A)
				sc->dc_type = DC_TYPE_98713;
			if (sc->dc_revision >= DC_REVISION_98713A) {
				sc->dc_type = DC_TYPE_98713A;
				sc->dc_flags |= DC_21143_NWAY;
			}
			sc->dc_flags |= DC_REDUCED_MII_POLL;
			sc->dc_flags |= DC_TX_POLL|DC_TX_USE_TX_INTR;
		}
		if (PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_MACRONIX_MX98715 ||
		    PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_ACCTON_EN1217) {
			found = 1;
			if (sc->dc_revision >= DC_REVISION_98715AEC_C &&
			    sc->dc_revision < DC_REVISION_98725)
				sc->dc_flags |= DC_128BIT_HASH;
			sc->dc_type = DC_TYPE_987x5;
			sc->dc_flags |= DC_TX_POLL|DC_TX_USE_TX_INTR;
			sc->dc_flags |= DC_REDUCED_MII_POLL|DC_21143_NWAY;
		}
		if (PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_MACRONIX_MX98727) {
			found = 1;
			sc->dc_type = DC_TYPE_987x5;
			sc->dc_flags |= DC_TX_POLL|DC_TX_USE_TX_INTR;
			sc->dc_flags |= DC_REDUCED_MII_POLL|DC_21143_NWAY;
		}
		break;
	case PCI_VENDOR_COMPEX:
		if (PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_COMPEX_98713) {
			found = 1;
			if (sc->dc_revision < DC_REVISION_98713A) {
				sc->dc_type = DC_TYPE_98713;
				sc->dc_flags |= DC_REDUCED_MII_POLL;
			}
			if (sc->dc_revision >= DC_REVISION_98713A)
				sc->dc_type = DC_TYPE_98713A;
			sc->dc_flags |= DC_TX_POLL|DC_TX_USE_TX_INTR;
		}
		break;
	case PCI_VENDOR_LITEON:
		if (PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_LITEON_PNICII) {
			found = 1;
			sc->dc_type = DC_TYPE_PNICII;
			sc->dc_flags |= DC_TX_POLL|DC_TX_USE_TX_INTR;
			sc->dc_flags |= DC_REDUCED_MII_POLL|DC_21143_NWAY;
			sc->dc_flags |= DC_128BIT_HASH;
		}
		if (PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_LITEON_PNIC) {
			found = 1;
			sc->dc_type = DC_TYPE_PNIC;
			sc->dc_flags |= DC_TX_STORENFWD|DC_TX_INTR_ALWAYS;
			sc->dc_flags |= DC_PNIC_RX_BUG_WAR;
			sc->dc_pnic_rx_buf = malloc(ETHER_MAX_DIX_LEN * 5,
			    M_DEVBUF, M_NOWAIT);
			if (sc->dc_pnic_rx_buf == NULL)
				panic("dc_pci_attach");
			if (sc->dc_revision < DC_REVISION_82C169)
				sc->dc_pmode = DC_PMODE_SYM;
		}
		break;
	case PCI_VENDOR_ASIX:
		if (PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_ASIX_AX88140A) {
			found = 1;
			sc->dc_type = DC_TYPE_ASIX;
			sc->dc_flags |= DC_TX_USE_TX_INTR|DC_TX_INTR_FIRSTFRAG;
			sc->dc_flags |= DC_REDUCED_MII_POLL;
			sc->dc_pmode = DC_PMODE_MII;
		}
		break;
	case PCI_VENDOR_CONEXANT:
		if (PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_CONEXANT_RS7112) {
			found = 1;
			sc->dc_type = DC_TYPE_CONEXANT;
			sc->dc_flags |= DC_TX_INTR_ALWAYS;
			sc->dc_flags |= DC_REDUCED_MII_POLL;
			sc->dc_pmode = DC_PMODE_MII;
			dc_read_srom(sc, sc->dc_romwidth);
		}
		break;
	case PCI_VENDOR_XIRCOM:
		if (PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_XIRCOM_X3201_3_21143) {
			found = 1;
			sc->dc_type = DC_TYPE_XIRCOM;
			sc->dc_flags |= DC_TX_INTR_ALWAYS;
			sc->dc_flags |= DC_TX_COALESCE;
			sc->dc_flags |= DC_TX_ALIGN;
			sc->dc_pmode = DC_PMODE_MII;
		}
		break;
	}
	if (found == 0) {
		/* This shouldn't happen if probe has done its job... */
		printf(": unknown device: %x:%x\n",
		    PCI_VENDOR(pa->pa_id), PCI_PRODUCT(pa->pa_id));
		goto fail_2;
	}

	/* Save the cache line size. */
	if (DC_IS_DAVICOM(sc))
		sc->dc_cachesize = 0;
	else
		sc->dc_cachesize = pci_conf_read(pc, pa->pa_tag,
		    DC_PCI_CFLT) & 0xFF;

	/* Reset the adapter. */
	dc_reset(sc);

	/* Take 21143 out of snooze mode */
	if (DC_IS_INTEL(sc) || DC_IS_XIRCOM(sc)) {
		command = pci_conf_read(pc, pa->pa_tag, DC_PCI_CFDD);
		command &= ~(DC_CFDD_SNOOZE_MODE|DC_CFDD_SLEEP_MODE);
		pci_conf_write(pc, pa->pa_tag, DC_PCI_CFDD, command);
	}

	/*
	 * If we discover later (in dc_attach) that we have an
	 * MII with no PHY, we need to have the 21143 drive the LEDs.
	 * Except there are some systems like the NEC VersaPro NoteBook PC
	 * which have no LEDs, and twiddling these bits has adverse effects
	 * on them. (I.e. you suddenly can't get a link.)
	 *
	 * If mii_attach() returns an error, we leave the DC_TULIP_LEDS
	 * bit set, else we clear it. Since our dc(4) driver is split into
	 * bus-dependent and bus-independent parts, we must do set this bit
	 * here while we are able to do PCI configuration reads.
	 */
	if (DC_IS_INTEL(sc)) {
		if (pci_conf_read(pc, pa->pa_tag, DC_PCI_CSID) != 0x80281033)
			sc->dc_flags |= DC_TULIP_LEDS;
	}

	/*
	 * Try to learn something about the supported media.
	 * We know that ASIX and ADMtek and Davicom devices
	 * will *always* be using MII media, so that's a no-brainer.
	 * The tricky ones are the Macronix/PNIC II and the
	 * Intel 21143.
	 */
	if (DC_IS_INTEL(sc))
		dc_parse_21143_srom(sc);
	else if (DC_IS_MACRONIX(sc) || DC_IS_PNICII(sc)) {
		if (sc->dc_type == DC_TYPE_98713)
			sc->dc_pmode = DC_PMODE_MII;
		else
			sc->dc_pmode = DC_PMODE_SYM;
	} else if (!sc->dc_pmode)
		sc->dc_pmode = DC_PMODE_MII;

#ifdef __sparc64__
	{
		extern void myetheraddr(u_char *);

		if (OF_getprop(PCITAG_NODE(pa->pa_tag), "local-mac-address",
		    sc->sc_arpcom.ac_enaddr, ETHER_ADDR_LEN) <= 0)
			myetheraddr(sc->sc_arpcom.ac_enaddr);
		if (sc->sc_arpcom.ac_enaddr[0] == 0x00 &&
		    sc->sc_arpcom.ac_enaddr[1] == 0x03 &&
		    sc->sc_arpcom.ac_enaddr[2] == 0xcc)
			sc->dc_flags |= DC_MOMENCO_BOTCH;
		sc->sc_hasmac = 1;
	}
#endif

#ifdef SRM_MEDIA
	sc->dc_srm_media = 0;

	/* Remember the SRM console media setting */
	if (DC_IS_INTEL(sc)) {
		command = pci_conf_read(pc, pa->pa_tag, DC_PCI_CFDD);
		command &= ~(DC_CFDD_SNOOZE_MODE|DC_CFDD_SLEEP_MODE);
		switch ((command >> 8) & 0xff) {
		case 3: 
			sc->dc_srm_media = IFM_10_T;
			break;
		case 4: 
			sc->dc_srm_media = IFM_10_T | IFM_FDX;
			break;
		case 5: 
			sc->dc_srm_media = IFM_100_TX;
			break;
		case 6: 
			sc->dc_srm_media = IFM_100_TX | IFM_FDX;
			break;
		}
		if (sc->dc_srm_media)
			sc->dc_srm_media |= IFM_ACTIVE | IFM_ETHER;
	}
#endif
	dc_attach(sc);

	return;

fail_2:
	pci_intr_disestablish(pc, sc->sc_ih);

fail_1:
	bus_space_unmap(sc->dc_btag, sc->dc_bhandle, psc->psc_mapsize);
}

int
dc_pci_detach(struct device *self, int flags)
{
	struct dc_pci_softc *psc = (void *)self;
	struct dc_softc *sc = &psc->psc_softc;

	if (sc->sc_ih != NULL)
		pci_intr_disestablish(psc->psc_pc, sc->sc_ih);	
	dc_detach(sc);
	bus_space_unmap(sc->dc_btag, sc->dc_bhandle, psc->psc_mapsize);

	return (0);
}

const struct cfattach dc_pci_ca = {
	sizeof(struct dc_softc), dc_pci_match, dc_pci_attach, dc_pci_detach,
	dc_activate
};
