/*	$OpenBSD: if_ne_cbus.c,v 1.5 2024/06/01 00:48:16 aoyama Exp $	*/
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

/*
 * Driver for C-bus NE2000 based Ethernet board
 *  based on: OpenBSD:src/sys/dev/isa/if_ne_isa.c
 */

/*
 * Supported boards:
 * - Allied Telesis  CentreCOM LA-98 series
 *   Available configuration:
 *   INT(IRQ): 0(3), 1(5), 2(6), 3(9), 4(10), 5(12), 6(13) (default: INT0)
 *   I/O address: 0xc8d0, 0xc2d0, 0xc4d0, 0xc460, 0xc9d0, 0xcad0, 0xcbd0
 *              (default 0xc8d0)
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

#include <machine/board.h>		/* PC_BASE */
#include <machine/bus.h>
#include <machine/intr.h>

#include <dev/ic/dp8390reg.h>
#include <dev/ic/dp8390var.h>

#include <dev/ic/ne2000reg.h>
#include <dev/ic/ne2000var.h>

#include <dev/ic/rtl80x9reg.h>
#include <dev/ic/rtl80x9var.h>

#include <arch/luna88k/cbus/cbusvar.h>

/* bus space tag for if_ne_cbus */
struct luna88k_bus_space_tag ne_cbus_io_bst = {
	.bs_stride_1 = 0,
	.bs_stride_2 = 0,
	.bs_stride_4 = 0,
	.bs_stride_8 = 0,	/* not used */
	.bs_offset = PCEXIO_BASE,
	.bs_flags = TAG_LITTLE_ENDIAN
};

int	ne_cbus_match(struct device *, void *, void *);
void	ne_cbus_attach(struct device *, struct device *, void *);

struct ne_cbus_softc {
	struct	ne2000_softc sc_ne2000;		/* real "ne2000" softc */

	/* C-bus specific goo. */
	void	*sc_ih;				/* interrupt cookie */
};

const struct cfattach ne_cbus_ca = {
	sizeof(struct ne_cbus_softc), ne_cbus_match, ne_cbus_attach
};

int
ne_cbus_match(struct device *parent, void *match, void *aux)
{
	struct ne_cbus_softc *csc = match;
	struct ne2000_softc *nsc = &csc->sc_ne2000;
	struct dp8390_softc *dsc = &nsc->sc_dp8390;
	struct cbus_attach_args *caa = aux;
	bus_space_tag_t nict = &ne_cbus_io_bst;
	bus_space_handle_t nich;
	bus_space_tag_t asict;
	bus_space_handle_t asich;
	struct cfdata *cf = match;

	if (strcmp(caa->ca_name, cf->cf_driver->cd_name) != 0)
		return (0);

	SET_TAG_LITTLE_ENDIAN(nict);

	caa->ca_iobase = cf->cf_iobase;
	caa->ca_int    = cf->cf_int;

	/* Disallow wildcarded values. */
	if (caa->ca_int ==  -1)
		return (0);
	if (caa->ca_iobase == -1)
		return (0);

#if 0	/* XXX: Is this not true on PC-9801? */
	/* Make sure this is a valid NE[12]000 i/o address. */
	if ((caa->ca_iobase & 0x1f) != 0)
		return (0);
#endif
	/* Map i/o space. */
	if (bus_space_map(nict, caa->ca_iobase, NE2000_NPORTS, 0, &nich))
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
		caa->ca_iosize = NE2000_NPORTS;

 out:
	bus_space_unmap(nict, nich, NE2000_NPORTS);
	return (nsc->sc_type);
}

void
ne_cbus_attach(struct device *parent, struct device *self, void *aux)
{
	struct ne_cbus_softc *csc = (void *)self;
	struct ne2000_softc *nsc = &csc->sc_ne2000;
	struct dp8390_softc *dsc = &nsc->sc_dp8390;
	struct cbus_attach_args *caa = aux;
	bus_space_tag_t nict = &ne_cbus_io_bst;
	bus_space_handle_t nich;
	bus_space_tag_t asict = nict;
	bus_space_handle_t asich;
	const char *typestr;

	/* Map i/o space. */
	if (bus_space_map(nict, caa->ca_iobase, NE2000_NPORTS, 0, &nich)) {
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

	/* Look for an NE2000-compatible card. */
	nsc->sc_type = ne2000_detect(nsc);

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
	if (cbus_isrlink(dp8390_intr, dsc, caa->ca_int, IPL_NET,
	    dsc->sc_dev.dv_xname) != 0)
		printf(": couldn't establish interrupt handler\n");
}
