/*	$OpenBSD: if_ne_isa.c,v 1.19 2023/09/11 08:41:26 mvs Exp $	*/
/*	$NetBSD: if_ne_isa.c,v 1.6 1998/07/05 06:49:13 jonathan Exp $	*/

/*-
 * Copyright (c) 1997 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe of the Numerical Aerospace Simulation Facility,
 * NASA Ames Research Center.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "bpfilter.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/errno.h>
#include <sys/syslog.h>
#include <sys/device.h>

#include <net/if.h>
#include <net/if_media.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif

#include <machine/intr.h>
#include <machine/bus.h>

#include <dev/ic/dp8390reg.h>
#include <dev/ic/dp8390var.h>

#include <dev/ic/ne2000reg.h>
#include <dev/ic/ne2000var.h>

#include <dev/ic/rtl80x9reg.h>
#include <dev/ic/rtl80x9var.h>          

#include <dev/isa/isavar.h>

int	ne_isa_match(struct device *, void *, void *);
void	ne_isa_attach(struct device *, struct device *, void *);

struct ne_isa_softc {
	struct	ne2000_softc sc_ne2000;		/* real "ne2000" softc */

	/* ISA-specific goo. */
	void	*sc_ih;				/* interrupt cookie */
};

const struct cfattach ne_isa_ca = {
	sizeof(struct ne_isa_softc), ne_isa_match, ne_isa_attach
};

int
ne_isa_match(struct device *parent, void *match, void *aux)
{
	struct ne_isa_softc *isc = match;
	struct ne2000_softc *nsc = &isc->sc_ne2000;
	struct dp8390_softc *dsc = &nsc->sc_dp8390;
	struct isa_attach_args *ia = aux;
	bus_space_tag_t nict = ia->ia_iot;
	bus_space_handle_t nich;
	bus_space_tag_t asict;
	bus_space_handle_t asich;

	/* Disallow wildcarded values. */
	if (ia->ia_irq ==  -1 /* ISACF_IRQ_DEFAULT */)
		return (0);
	if (ia->ia_iobase == -1 /* ISACF_PORT_DEFAULT */)
		return (0);

	/* Make sure this is a valid NE[12]000 i/o address. */
	if ((ia->ia_iobase & 0x1f) != 0)
		return (0);

	/* Map i/o space. */
	if (bus_space_map(nict, ia->ia_iobase, NE2000_NPORTS, 0, &nich))
		return (0);

	asict = nict;
	if (bus_space_subregion(nict, nich, NE2000_ASIC_OFFSET,
	    NE2000_ASIC_NPORTS, &asich))
		goto out;

	dsc->sc_regt = nict;
	dsc->sc_regh = nich;

	nsc->sc_asict = asict;
	nsc->sc_asich = asich;

	/* Look for an NE2000-compatible card. */
	nsc->sc_type = ne2000_detect(nsc);

	if (nsc->sc_type)
		ia->ia_iosize = NE2000_NPORTS;

 out:
	bus_space_unmap(nict, nich, NE2000_NPORTS);
	return (nsc->sc_type);
}

void
ne_isa_attach(struct device *parent, struct device *self, void *aux)
{
	struct ne_isa_softc *isc = (struct ne_isa_softc *)self;
	struct ne2000_softc *nsc = &isc->sc_ne2000;
	struct dp8390_softc *dsc = &nsc->sc_dp8390;
	struct isa_attach_args *ia = aux;
	bus_space_tag_t nict = ia->ia_iot;
	bus_space_handle_t nich;
	bus_space_tag_t asict = nict;
	bus_space_handle_t asich;
	const char *typestr;

	/* Map i/o space. */
	if (bus_space_map(nict, ia->ia_iobase, NE2000_NPORTS, 0, &nich)) {
		printf("%s: can't map i/o space\n", dsc->sc_dev.dv_xname);
		return;
	}

	if (bus_space_subregion(nict, nich, NE2000_ASIC_OFFSET,
	    NE2000_ASIC_NPORTS, &asich)) {
		printf("%s: can't subregion i/o space\n", dsc->sc_dev.dv_xname);
		return;
	}

	dsc->sc_regt = nict;
	dsc->sc_regh = nich;

	nsc->sc_asict = asict;
	nsc->sc_asich = asich;

	switch (nsc->sc_type) {
	case NE2000_TYPE_NE1000:
		typestr = "NE1000";
		break;

	case NE2000_TYPE_NE2000:
		typestr = "NE2000";
		/*
		 * Check for a Realtek 8019.
		 */
		bus_space_write_1(nict, nich, ED_P0_CR,
		    ED_CR_PAGE_0 | ED_CR_STP);
		if (bus_space_read_1(nict, nich, NERTL_RTL0_8019ID0) ==
								RTL0_8019ID0 &&
		    bus_space_read_1(nict, nich, NERTL_RTL0_8019ID1) ==
								RTL0_8019ID1) {
			typestr = "NE2000 (RTL8019)";
			dsc->sc_mediachange = rtl80x9_mediachange;
			dsc->sc_mediastatus = rtl80x9_mediastatus;
			dsc->init_card = rtl80x9_init_card;
			dsc->sc_media_init = rtl80x9_media_init;
		}
		break;

	default:
		printf(": where did the card go?!\n");
		return;
	}

	printf(", %s", typestr);

	/* This interface is always enabled. */
	dsc->sc_enabled = 1;

	/*
	 * Do generic NE2000 attach.  This will read the station address
	 * from the EEPROM.
	 */
	ne2000_attach(nsc, NULL);

	/* Establish the interrupt handler. */
	isc->sc_ih = isa_intr_establish(ia->ia_ic, ia->ia_irq, IST_EDGE,
	    IPL_NET, dp8390_intr, dsc, dsc->sc_dev.dv_xname);
	if (isc->sc_ih == NULL)
		printf(": couldn't establish interrupt handler\n");
}
