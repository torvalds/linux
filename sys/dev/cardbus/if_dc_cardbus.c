/*	$OpenBSD: if_dc_cardbus.c,v 1.42 2024/05/24 06:26:47 jsg Exp $	*/

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
 *      This product includes software developed by Bill Paul.
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
#include <sys/device.h>

#include <net/if.h>
#include <net/if_media.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <dev/mii/miivar.h>

#include <machine/bus.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcidevs.h>

#include <dev/cardbus/cardbusvar.h>

#include <dev/ic/dcreg.h>

/* PCI configuration regs */
#define	PCI_CBIO	0x10
#define	PCI_CBMEM	0x14
#define	PCI_CFDA	0x40

#define	DC_CFDA_SUSPEND	0x80000000
#define	DC_CFDA_STANDBY	0x40000000

struct dc_cardbus_softc {
	struct dc_softc		sc_dc;
	int			sc_intrline;

	cardbus_devfunc_t	sc_ct;
	pci_chipset_tag_t	sc_pc;
	pcitag_t		sc_tag;
	bus_size_t		sc_mapsize;
	int			sc_actype;
};

int dc_cardbus_match(struct device *, void *, void *);
void dc_cardbus_attach(struct device *, struct device *,void *);
int dc_cardbus_detach(struct device *, int);

void dc_cardbus_setup(struct dc_cardbus_softc *csc);

const struct cfattach dc_cardbus_ca = {
	sizeof(struct dc_cardbus_softc), dc_cardbus_match, dc_cardbus_attach,
	dc_cardbus_detach, dc_activate
};

const struct pci_matchid dc_cardbus_devices[] = {
	{ PCI_VENDOR_DEC, PCI_PRODUCT_DEC_21142 },
	{ PCI_VENDOR_XIRCOM, PCI_PRODUCT_XIRCOM_X3201_3_21143 },
	{ PCI_VENDOR_ADMTEK, PCI_PRODUCT_ADMTEK_AN985 },
	{ PCI_VENDOR_ACCTON, PCI_PRODUCT_ACCTON_EN2242 },
	{ PCI_VENDOR_ABOCOM, PCI_PRODUCT_ABOCOM_FE2500 },
	{ PCI_VENDOR_ABOCOM, PCI_PRODUCT_ABOCOM_FE2500MX },
	{ PCI_VENDOR_ABOCOM, PCI_PRODUCT_ABOCOM_PCM200 },
	{ PCI_VENDOR_DLINK, PCI_PRODUCT_DLINK_DRP32TXD },
	{ PCI_VENDOR_LINKSYS, PCI_PRODUCT_LINKSYS_PCMPC200 },
	{ PCI_VENDOR_LINKSYS, PCI_PRODUCT_LINKSYS_PCM200 },
	{ PCI_VENDOR_HAWKING, PCI_PRODUCT_HAWKING_PN672TX },
	{ PCI_VENDOR_MICROSOFT, PCI_PRODUCT_MICROSOFT_MN120 },
};

int
dc_cardbus_match(struct device *parent, void *match, void *aux)
{
	return (cardbus_matchbyid((struct cardbus_attach_args *)aux,
	    dc_cardbus_devices, nitems(dc_cardbus_devices)));
}

void
dc_cardbus_attach(struct device *parent, struct device *self, void *aux)
{
	struct dc_cardbus_softc *csc = (struct dc_cardbus_softc *)self;
	struct dc_softc *sc = &csc->sc_dc;
	struct cardbus_attach_args *ca = aux;
	struct cardbus_devfunc *ct = ca->ca_ct;
	cardbus_chipset_tag_t cc = ct->ct_cc;
	pci_chipset_tag_t pc = ca->ca_pc;
	cardbus_function_tag_t cf = ct->ct_cf;
	pcireg_t reg;
	bus_addr_t addr;

	sc->sc_dmat = ca->ca_dmat;
	csc->sc_ct = ct;
	csc->sc_tag = ca->ca_tag;
	csc->sc_pc = ca->ca_pc;

	Cardbus_function_enable(ct);

	if (Cardbus_mapreg_map(ct, PCI_CBIO,
	    PCI_MAPREG_TYPE_IO, 0, &sc->dc_btag, &sc->dc_bhandle, &addr,
	    &csc->sc_mapsize) == 0) {

		csc->sc_actype = CARDBUS_IO_ENABLE;
	} else if (Cardbus_mapreg_map(ct, PCI_CBMEM,
	    PCI_MAPREG_TYPE_MEM|PCI_MAPREG_MEM_TYPE_32BIT, 0,
	    &sc->dc_btag, &sc->dc_bhandle, &addr, &csc->sc_mapsize) == 0) {
		csc->sc_actype = CARDBUS_MEM_ENABLE;
	} else {
		printf(": can't map device registers\n");
		return;
	}

	csc->sc_intrline = ca->ca_intrline;

	sc->dc_cachesize = pci_conf_read(csc->sc_pc, ca->ca_tag, DC_PCI_CFLT)
	    & 0xFF;

	dc_cardbus_setup(csc);

	/* Get the eeprom width */
	if ((PCI_VENDOR(ca->ca_id) == PCI_VENDOR_XIRCOM &&
	      PCI_PRODUCT(ca->ca_id) == PCI_PRODUCT_XIRCOM_X3201_3_21143))
		;	/* XIRCOM has non-standard eeprom */
	else
		dc_eeprom_width(sc);

	switch (PCI_VENDOR(ca->ca_id)) {
	case PCI_VENDOR_DEC:
		if (PCI_PRODUCT(ca->ca_id) == PCI_PRODUCT_DEC_21142) {
			sc->dc_type = DC_TYPE_21143;
			sc->dc_flags |= DC_TX_POLL|DC_TX_USE_TX_INTR;
			sc->dc_flags |= DC_REDUCED_MII_POLL;
			dc_read_srom(sc, sc->dc_romwidth);
			dc_parse_21143_srom(sc);
		}
		break;
	case PCI_VENDOR_XIRCOM:
		if (PCI_PRODUCT(ca->ca_id) ==
		    PCI_PRODUCT_XIRCOM_X3201_3_21143) {
			sc->dc_type = DC_TYPE_XIRCOM;
			sc->dc_flags |= DC_TX_INTR_ALWAYS|DC_TX_COALESCE |
					DC_TX_ALIGN;
			sc->dc_pmode = DC_PMODE_MII;
		}
		break;
	case PCI_VENDOR_ADMTEK:
	case PCI_VENDOR_ACCTON:
	case PCI_VENDOR_ABOCOM:
	case PCI_VENDOR_DLINK:
	case PCI_VENDOR_LINKSYS:
	case PCI_VENDOR_HAWKING:
	case PCI_VENDOR_MICROSOFT:
		if (PCI_PRODUCT(ca->ca_id) == PCI_PRODUCT_ADMTEK_AN985 ||
		    PCI_PRODUCT(ca->ca_id) == PCI_PRODUCT_ACCTON_EN2242 ||
		    PCI_PRODUCT(ca->ca_id) == PCI_PRODUCT_ABOCOM_FE2500 ||
		    PCI_PRODUCT(ca->ca_id) == PCI_PRODUCT_ABOCOM_FE2500MX ||
		    PCI_PRODUCT(ca->ca_id) == PCI_PRODUCT_ABOCOM_PCM200 ||
		    PCI_PRODUCT(ca->ca_id) == PCI_PRODUCT_DLINK_DRP32TXD ||
		    PCI_PRODUCT(ca->ca_id) == PCI_PRODUCT_LINKSYS_PCMPC200 ||
		    PCI_PRODUCT(ca->ca_id) == PCI_PRODUCT_LINKSYS_PCM200 ||
		    PCI_PRODUCT(ca->ca_id) == PCI_PRODUCT_HAWKING_PN672TX ||
		    PCI_PRODUCT(ca->ca_id) == PCI_PRODUCT_MICROSOFT_MN120) {
			sc->dc_type = DC_TYPE_AN983;
			sc->dc_flags |= DC_TX_USE_TX_INTR|DC_TX_ADMTEK_WAR |
					DC_64BIT_HASH;
			sc->dc_pmode = DC_PMODE_MII;
			/* Don't read SROM for - auto-loaded on reset */
		}
		break;
	default:
		printf(": unknown device\n");
		return;
	}

 	/*
	 * set latency timer, do we really need this?
	 */
	reg = pci_conf_read(pc, ca->ca_tag, PCI_BHLC_REG);
	if (PCI_LATTIMER(reg) < 0x20) {
		reg &= ~(PCI_LATTIMER_MASK << PCI_LATTIMER_SHIFT);
		reg |= (0x20 << PCI_LATTIMER_SHIFT);
		pci_conf_write(pc, ca->ca_tag, PCI_BHLC_REG, reg);
	}

	sc->sc_ih = cardbus_intr_establish(cc, cf, ca->ca_intrline, IPL_NET,
	    dc_intr, csc, sc->sc_dev.dv_xname);
	if (sc->sc_ih == NULL) {
		printf(": can't establish interrupt at %d\n",
		    ca->ca_intrline);
		return;
	}
	printf(": irq %d", ca->ca_intrline);

	dc_reset(sc);

	sc->dc_revision = PCI_REVISION(ca->ca_class);
	dc_attach(sc);
}

int
dc_cardbus_detach(struct device *self, int flags)
{
	struct dc_cardbus_softc *csc = (struct dc_cardbus_softc *)self;
	struct dc_softc *sc = &csc->sc_dc;
	struct cardbus_devfunc *ct = csc->sc_ct;

	cardbus_intr_disestablish(ct->ct_cc, ct->ct_cf, sc->sc_ih);
	dc_detach(sc);

	/* unmap cardbus resources */
	Cardbus_mapreg_unmap(ct,
	    csc->sc_actype == CARDBUS_IO_ENABLE ? PCI_CBIO : PCI_CBMEM,
	    sc->dc_btag, sc->dc_bhandle, csc->sc_mapsize);

	return (0);
}

void
dc_cardbus_setup(struct dc_cardbus_softc *csc)
{
	cardbus_devfunc_t ct = csc->sc_ct;
	cardbus_chipset_tag_t cc = ct->ct_cc;
	pci_chipset_tag_t pc = csc->sc_pc;
	pcireg_t reg;
	int r;

	/* wakeup the card if needed */
	reg = pci_conf_read(pc, csc->sc_tag, PCI_CFDA);
	if (reg & (DC_CFDA_SUSPEND|DC_CFDA_STANDBY)) {
		pci_conf_write(pc, csc->sc_tag, PCI_CFDA,
		    reg & ~(DC_CFDA_SUSPEND|DC_CFDA_STANDBY));
	}

	if (pci_get_capability(csc->sc_pc, csc->sc_tag, PCI_CAP_PWRMGMT, &r,
	    0)) {
		r = pci_conf_read(csc->sc_pc, csc->sc_tag, r + 4) & 3;
		if (r) {
			printf("%s: awakening from state D%d\n",
			    csc->sc_dc.sc_dev.dv_xname, r);
			pci_conf_write(csc->sc_pc, csc->sc_tag, r + 4, 0);
		}
	}

	(*ct->ct_cf->cardbus_ctrl)(cc, csc->sc_actype);
	(*ct->ct_cf->cardbus_ctrl)(cc, CARDBUS_BM_ENABLE);

	reg = pci_conf_read(csc->sc_pc, csc->sc_tag, PCI_COMMAND_STATUS_REG);
	reg |= PCI_COMMAND_IO_ENABLE | PCI_COMMAND_MEM_ENABLE |
	    PCI_COMMAND_MASTER_ENABLE;
	pci_conf_write(csc->sc_pc, csc->sc_tag, PCI_COMMAND_STATUS_REG, reg);
	reg = pci_conf_read(csc->sc_pc, csc->sc_tag, PCI_COMMAND_STATUS_REG);
}
