/*	$OpenBSD: if_gem_sbus.c,v 1.11 2022/03/13 13:34:54 mpi Exp $	*/
/*	$NetBSD: if_gem_sbus.c,v 1.1 2006/11/24 13:23:32 martin Exp $	*/

/*-
 * Copyright (c) 2006 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Martin Husemann.
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
 * SBus front-end for the GEM network driver
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

#include <dev/ic/gemreg.h>
#include <dev/ic/gemvar.h>

#include <dev/ofw/openfirm.h>

struct gem_sbus_softc {
	struct	gem_softc	gsc_gem;	/* GEM device */
};

int	gemmatch_sbus(struct device *, void *, void *);
void	gemattach_sbus(struct device *, struct device *, void *);

const struct cfattach gem_sbus_ca = {
	sizeof(struct gem_sbus_softc), gemmatch_sbus, gemattach_sbus
};

int
gemmatch_sbus(struct device *parent, void *vcf, void *aux)
{
	struct sbus_attach_args *sa = aux;

	return (strcmp("network", sa->sa_name) == 0);
}

void
gemattach_sbus(struct device *parent, struct device *self, void *aux)
{
	struct sbus_attach_args *sa = aux;
	struct gem_sbus_softc *gsc = (void *)self;
	struct gem_softc *sc = &gsc->gsc_gem;
	/* XXX the following declaration should be elsewhere */
	extern void myetheraddr(u_char *);

	/* Pass on the bus tags */
	sc->sc_bustag = sa->sa_bustag;
	sc->sc_dmatag = sa->sa_dmatag;

	if (sa->sa_nintr < 1) {
		printf(": no interrupt\n");
		return;
	}

	if (sa->sa_nreg < 2) {
		printf(": only %d register sets\n", sa->sa_nreg);
		return;
	}

	sc->sc_variant = GEM_SUN_GEM;

	/*
	 * Map two register banks:
	 *
	 *	bank 0: status, config, reset
	 *	bank 1: various gem parts
	 *
	 */
	if (sbus_bus_map(sa->sa_bustag, sa->sa_reg[0].sbr_slot,
			 (bus_addr_t)sa->sa_reg[0].sbr_offset,
			 (bus_size_t)sa->sa_reg[0].sbr_size, 0, 0,
			 &sc->sc_h2) != 0) {
		printf(": can't map registers\n");
		return;
	}
	if (sbus_bus_map(sa->sa_bustag, sa->sa_reg[0].sbr_slot,
			 (bus_addr_t)sa->sa_reg[1].sbr_offset,
			 (bus_size_t)sa->sa_reg[1].sbr_size, 0, 0,
			 &sc->sc_h1) != 0) {
		printf(": can't map registers\n");
		return;
	}

	if (OF_getprop(sa->sa_node, "local-mac-address",
	    sc->sc_arpcom.ac_enaddr, ETHER_ADDR_LEN) <= 0)
		myetheraddr(sc->sc_arpcom.ac_enaddr);

	/*
	 * SBUS config
	 */
	(void) bus_space_read_4(sa->sa_bustag, sc->sc_h2, GEM_SBUS_RESET);
	delay(100);
	bus_space_write_4(sa->sa_bustag, sc->sc_h2, GEM_SBUS_CONFIG,
	    GEM_SBUS_CFG_BSIZE128|GEM_SBUS_CFG_PARITY|GEM_SBUS_CFG_BMODE64);

	/* Establish interrupt handler */
	sc->sc_ih = bus_intr_establish(sa->sa_bustag, sa->sa_pri, IPL_NET, 0,
	    gem_intr, sc, self->dv_xname);

	gem_config(sc);
}
