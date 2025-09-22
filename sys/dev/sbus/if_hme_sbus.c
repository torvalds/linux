/*	$OpenBSD: if_hme_sbus.c,v 1.17 2022/03/13 13:34:54 mpi Exp $	*/
/*	$NetBSD: if_hme_sbus.c,v 1.6 2001/02/28 14:52:48 mrg Exp $	*/

/*-
 * Copyright (c) 1999 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Paul Kranenburg.
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
 * SBus front-end device driver for the HME ethernet device.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/syslog.h>
#include <sys/device.h>
#include <sys/malloc.h>
#include <sys/socket.h>

#include <net/if.h>
#include <net/if_media.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>

#include <machine/bus.h>
#include <machine/intr.h>
#include <machine/autoconf.h>

#include <dev/sbus/sbusvar.h>
#include <dev/ic/hmevar.h>
#include <dev/ofw/openfirm.h>

struct hmesbus_softc {
	struct	hme_softc	hsc_hme;	/* HME device */
};

int	hmematch_sbus(struct device *, void *, void *);
void	hmeattach_sbus(struct device *, struct device *, void *);

const struct cfattach hme_sbus_ca = {
	sizeof(struct hmesbus_softc), hmematch_sbus, hmeattach_sbus
};

int
hmematch_sbus(struct device *parent, void *vcf, void *aux)
{
	struct cfdata *cf = vcf;
	struct sbus_attach_args *sa = aux;

	return (strcmp(cf->cf_driver->cd_name, sa->sa_name) == 0 ||
	    strcmp("SUNW,qfe", sa->sa_name) == 0 ||
	    strcmp("SUNW,hme", sa->sa_name) == 0);
}

void
hmeattach_sbus(struct device *parent, struct device *self, void *aux)
{
	struct sbus_attach_args *sa = aux;
	struct hmesbus_softc *hsc = (void *)self;
	struct hme_softc *sc = &hsc->hsc_hme;
	u_int32_t burst, sbusburst;
	/* XXX the following declaration should be elsewhere */
	extern void myetheraddr(u_char *);

	/* Pass on the bus tags */
	sc->sc_bustag = sa->sa_bustag;
	sc->sc_dmatag = sa->sa_dmatag;

	if (sa->sa_nintr < 1) {
		printf(": no interrupt\n");
		return;
	}

	if (sa->sa_nreg < 5) {
		printf(": only %d register sets\n", sa->sa_nreg);
		return;
	}

	/*
	 * Map five register banks:
	 *
	 *	bank 0: HME SEB registers
	 *	bank 1: HME ETX registers
	 *	bank 2: HME ERX registers
	 *	bank 3: HME MAC registers
	 *	bank 4: HME MIF registers
	 *
	 */
	if (sbus_bus_map(sa->sa_bustag, sa->sa_reg[0].sbr_slot,
	    (bus_addr_t)sa->sa_reg[0].sbr_offset,
	    (bus_size_t)sa->sa_reg[0].sbr_size, 0, 0, &sc->sc_seb) != 0) {
		printf(": can't map registers\n");
		return;
	}
	if (sbus_bus_map(sa->sa_bustag, sa->sa_reg[1].sbr_slot,
	    (bus_addr_t)sa->sa_reg[1].sbr_offset,
	    (bus_size_t)sa->sa_reg[1].sbr_size, 0, 0, &sc->sc_etx) != 0) {
		printf(": can't map registers\n");
		return;
	}
	if (sbus_bus_map(sa->sa_bustag, sa->sa_reg[2].sbr_slot,
	    (bus_addr_t)sa->sa_reg[2].sbr_offset,
	    (bus_size_t)sa->sa_reg[2].sbr_size, 0, 0, &sc->sc_erx) != 0) {
		printf(": can't map registers\n");
		return;
	}
	if (sbus_bus_map(sa->sa_bustag, sa->sa_reg[3].sbr_slot,
	    (bus_addr_t)sa->sa_reg[3].sbr_offset,
	    (bus_size_t)sa->sa_reg[3].sbr_size, 0, 0, &sc->sc_mac) != 0) {
		printf(": can't map registers\n");
		return;
	}
	if (sbus_bus_map(sa->sa_bustag, sa->sa_reg[4].sbr_slot,
	    (bus_addr_t)sa->sa_reg[4].sbr_offset,
	    (bus_size_t)sa->sa_reg[4].sbr_size, 0, 0, &sc->sc_mif) != 0) {
		printf(": can't map registers\n");
		return;
	}

	if (OF_getprop(sa->sa_node, "local-mac-address",
	    sc->sc_arpcom.ac_enaddr, ETHER_ADDR_LEN) <= 0)
		myetheraddr(sc->sc_arpcom.ac_enaddr);

	/*
	 * Get transfer burst size from PROM and pass it on
	 * to the back-end driver.
	 */
	sbusburst = ((struct sbus_softc *)parent)->sc_burst;
	if (sbusburst == 0)
		sbusburst = SBUS_BURST_32 - 1; /* 1->16 */

	burst = getpropint(sa->sa_node, "burst-sizes", -1);
	if (burst == -1)
		/* take SBus burst sizes */
		burst = sbusburst;

	/* Clamp at parent's burst sizes */
	burst &= sbusburst;

	/* Translate into plain numerical format */
	if ((burst & SBUS_BURST_64))
		sc->sc_burst = 64;
	else if ((burst & SBUS_BURST_32))
		sc->sc_burst = 32;
	else if ((burst & SBUS_BURST_16))
		sc->sc_burst = 16;
	else
		sc->sc_burst = 0;

	sc->sc_pci = 0; /* XXXXX should all be done in bus_dma. */

	/* Establish interrupt handler */
	bus_intr_establish(sa->sa_bustag, sa->sa_pri, IPL_NET, 0, hme_intr,
	    sc, self->dv_xname);

	hme_config(sc);
}
