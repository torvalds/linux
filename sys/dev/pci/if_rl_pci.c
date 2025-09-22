/*	$OpenBSD: if_rl_pci.c,v 1.35 2024/05/24 06:02:56 jsg Exp $ */

/*
 * Copyright (c) 1997, 1998
 *	Bill Paul <wpaul@ctr.columbia.edu>.  All rights reserved.
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
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/timeout.h>

#include <net/if.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <net/if_media.h>

#include <machine/bus.h>

#include <dev/mii/miivar.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

/*
 * Default to using PIO access for this driver. On SMP systems,
 * there appear to be problems with memory mapped mode: it looks like
 * doing too many memory mapped access back to back in rapid succession
 * can hang the bus. I'm inclined to blame this on crummy design/construction
 * on the part of Realtek. Memory mapped mode does appear to work on
 * uniprocessor systems though.
 */
#define RL_USEIOSPACE

#include <dev/ic/rtl81x9reg.h>

int rl_pci_match(struct device *, void *, void *);
void rl_pci_attach(struct device *, struct device *, void *);
int rl_pci_detach(struct device *, int);

struct rl_pci_softc {
	struct rl_softc		psc_softc;
	pci_chipset_tag_t	psc_pc;
	bus_size_t		psc_mapsize;
};

const struct cfattach rl_pci_ca = {
	sizeof(struct rl_pci_softc), rl_pci_match, rl_pci_attach, rl_pci_detach,
	rl_activate
};

const struct pci_matchid rl_pci_devices[] = {
	{ PCI_VENDOR_ABOCOM, PCI_PRODUCT_ABOCOM_FE2000VX },
	{ PCI_VENDOR_ACCTON, PCI_PRODUCT_ACCTON_5030 },
	{ PCI_VENDOR_ADDTRON, PCI_PRODUCT_ADDTRON_8139 },
	{ PCI_VENDOR_COREGA, PCI_PRODUCT_COREGA_2CB_TXD },
	{ PCI_VENDOR_COREGA, PCI_PRODUCT_COREGA_CB_TXD },
	{ PCI_VENDOR_DELTA, PCI_PRODUCT_DELTA_8139 },
	{ PCI_VENDOR_DLINK, PCI_PRODUCT_DLINK_DFE520TX_C1 },
	{ PCI_VENDOR_DLINK, PCI_PRODUCT_DLINK_DFE530TXPLUS },
	{ PCI_VENDOR_DLINK, PCI_PRODUCT_DLINK_DFE690TXD },
	{ PCI_VENDOR_DLINK2, PCI_PRODUCT_DLINK2_DFE530TXPLUS2 },
	{ PCI_VENDOR_NORTEL, PCI_PRODUCT_NORTEL_BS21 },
	{ PCI_VENDOR_PLANEX, PCI_PRODUCT_PLANEX_FNW_3603_TX },
	{ PCI_VENDOR_PLANEX, PCI_PRODUCT_PLANEX_FNW_3800_TX },
	{ PCI_VENDOR_REALTEK, PCI_PRODUCT_REALTEK_RT8129 },
	{ PCI_VENDOR_REALTEK, PCI_PRODUCT_REALTEK_RT8138 },
	{ PCI_VENDOR_REALTEK, PCI_PRODUCT_REALTEK_RT8139D }
};

int
rl_pci_match(struct device *parent, void *match, void *aux)
{
	struct pci_attach_args *pa = aux;

	if (PCI_VENDOR(pa->pa_id) == PCI_VENDOR_REALTEK &&
	    PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_REALTEK_RT8139 &&
	    PCI_REVISION(pa->pa_class) == 0x10)
		return (1);

	return (pci_matchbyid((struct pci_attach_args *)aux, rl_pci_devices,
	    nitems(rl_pci_devices)));
}

void
rl_pci_attach(struct device *parent, struct device *self, void *aux)
{
	struct rl_pci_softc	*psc = (void *)self;
	struct rl_softc		*sc = &psc->psc_softc;
	struct pci_attach_args	*pa = aux;
	pci_chipset_tag_t	pc = pa->pa_pc;
	pci_intr_handle_t	ih;
	const char		*intrstr = NULL;

	/*
	 * Map control/status registers.
	 */

#ifdef RL_USEIOSPACE
	if (pci_mapreg_map(pa, RL_PCI_LOIO, PCI_MAPREG_TYPE_IO, 0,
	    &sc->rl_btag, &sc->rl_bhandle, NULL, &psc->psc_mapsize, 0)) {
		printf(": can't map i/o space\n");
		return;
	}
#else
	if (pci_mapreg_map(pa, RL_PCI_LOMEM, PCI_MAPREG_TYPE_MEM, 0,
	    &sc->rl_btag, &sc->rl_bhandle, NULL, &psc->psc_mapsize, 0)){
		printf(": can't map mem space\n");
		return;
	}
#endif

	/*
	 * Allocate our interrupt.
	 */
	if (pci_intr_map(pa, &ih)) {
		printf(": couldn't map interrupt\n");
		bus_space_unmap(sc->rl_btag, sc->rl_bhandle, psc->psc_mapsize);
		return;
	}

	psc->psc_pc = pa->pa_pc;
	intrstr = pci_intr_string(pc, ih);
	sc->sc_ih = pci_intr_establish(pc, ih, IPL_NET, rl_intr, sc,
	    self->dv_xname);
	if (sc->sc_ih == NULL) {
		printf(": couldn't establish interrupt");
		if (intrstr != NULL)
			printf(" at %s", intrstr);
		printf("\n");
		bus_space_unmap(sc->rl_btag, sc->rl_bhandle, psc->psc_mapsize);
		return;
	}
	printf(": %s", intrstr);

	sc->sc_dmat = pa->pa_dmat;

	if (PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_REALTEK_RT8129)
		sc->rl_type = RL_8129;
	else
		sc->rl_type = RL_8139;

	rl_attach(sc);
}

int
rl_pci_detach(struct device *self, int flags)
{
	struct rl_pci_softc	*psc = (void *)self;
	struct rl_softc		*sc = &psc->psc_softc;
	int			rv;

	rv = rl_detach(sc);
	if (rv)
		return (rv);

	if (sc->sc_ih != NULL)
		pci_intr_disestablish(psc->psc_pc, sc->sc_ih);

	bus_space_unmap(sc->rl_btag, sc->rl_bhandle, psc->psc_mapsize);

	return (0);
}
