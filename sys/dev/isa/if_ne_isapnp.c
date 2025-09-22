/*	$OpenBSD: if_ne_isapnp.c,v 1.18 2023/09/11 08:41:26 mvs Exp $	*/
/*	$NetBSD: if_ne_isapnp.c,v 1.7 1998/07/23 19:30:45 christos Exp $	*/

/*-
 * Copyright (c) 1997 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe of the Numerical Aerospace Simulation Facility,
 * NASA Ames Research Center and Matt Thomas of the 3am Software Foundry.
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

#include <dev/isa/isapnpreg.h>

static int ne_isapnp_match(struct device *, void *, void *);
static void ne_isapnp_attach(struct device *, struct device *, void *);

struct ne_isapnp_softc {
	struct	ne2000_softc sc_ne2000;		/* real "ne2000" softc */

	/* ISA-specific goo. */
	void	*sc_ih;				/* interrupt cookie */
};

const struct cfattach ne_isapnp_ca = {
	sizeof(struct ne_isapnp_softc), ne_isapnp_match, ne_isapnp_attach
};

static int
ne_isapnp_match(struct device *parent, void *match, void *aux)
{
	return (1);
}

static void
ne_isapnp_attach(struct device *parent, struct device *self, void *aux)
{
	struct ne_isapnp_softc * const isc = (struct ne_isapnp_softc *)self;
	struct ne2000_softc * const nsc = &isc->sc_ne2000;
	struct dp8390_softc * const dsc = &nsc->sc_dp8390;
	struct isa_attach_args * const ipa = aux;
	bus_space_tag_t nict;
	bus_space_handle_t nich;
	bus_space_tag_t asict;
	bus_space_handle_t asich;
	const char *typestr;
	int netype;

	nict = ipa->ia_iot;
	nich = ipa->ipa_io[0].h;

	asict = nict;

	if (bus_space_subregion(nict, nich, NE2000_ASIC_OFFSET,
	    NE2000_ASIC_NPORTS, &asich)) {
		printf("%s: can't subregion i/o space\n", dsc->sc_dev.dv_xname);
		return;
	}

	dsc->sc_regt = nict;
	dsc->sc_regh = nich;

	nsc->sc_asict = asict;
	nsc->sc_asich = asich;

	/*
	 * Detect it again, so we can print some information about the
	 * interface.
	 */
	netype = ne2000_detect(nsc);
	switch (netype) {
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

	printf(": %s", typestr);

	/* This interface is always enabled. */
	dsc->sc_enabled = 1;

	/*
	 * Do generic NE2000 attach.  This will read the station address
	 * from the EEPROM.
	 */
	ne2000_attach(nsc, NULL);

	/* Establish the interrupt handler. */
	isc->sc_ih = isa_intr_establish(ipa->ia_ic, ipa->ipa_irq[0].num,
	    IST_EDGE, IPL_NET, dp8390_intr, dsc,
	    dsc->sc_dev.dv_xname);
	if (isc->sc_ih == NULL)
		printf(": couldn't establish interrupt handler\n");
}
