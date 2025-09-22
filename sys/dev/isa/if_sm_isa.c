/*	$OpenBSD: if_sm_isa.c,v 1.17 2023/09/11 08:41:26 mvs Exp $	*/
/*	$NetBSD: if_sm_isa.c,v 1.4 1998/07/05 06:49:14 jonathan Exp $	*/

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
#include <sys/timeout.h>
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

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>

#include <dev/ic/smc91cxxreg.h>
#include <dev/ic/smc91cxxvar.h>

#include <dev/isa/isavar.h>

int	sm_isa_match(struct device *, void *, void *);
void	sm_isa_attach(struct device *, struct device *, void *);

struct sm_isa_softc {
	struct	smc91cxx_softc sc_smc;		/* real "smc" softc */

	/* ISA-specific goo. */
	void	*sc_ih;				/* interrupt cookie */
};

const struct cfattach sm_isa_ca = {
	sizeof(struct sm_isa_softc), sm_isa_match, sm_isa_attach
};

int
sm_isa_match(struct device *parent, void *match, void *aux)
{
	struct isa_attach_args *ia = aux;
	bus_space_tag_t iot = ia->ia_iot;
	bus_space_handle_t ioh;
	u_int16_t tmp;
	int rv = 0;
	extern const char *smc91cxx_idstrs[];

	/* Disallow wildcarded values. */
	if (ia->ia_irq == -1)
		return (0);
	if (ia->ia_iobase == -1)
		return (0);

	/* Map i/o space. */
	if (bus_space_map(iot, ia->ia_iobase, SMC_IOSIZE, 0, &ioh))
		return (0);

	/* Check that high byte of BANK_SELECT is what we expect. */
	tmp = bus_space_read_2(iot, ioh, BANK_SELECT_REG_W);
	if ((tmp & BSR_DETECT_MASK) != BSR_DETECT_VALUE)
		goto out;

	/*
	 * Switch to bank 0 and perform the test again.
	 * XXX INVASIVE!
	 */
	bus_space_write_2(iot, ioh, BANK_SELECT_REG_W, 0);
	tmp = bus_space_read_2(iot, ioh, BANK_SELECT_REG_W);
	if ((tmp & BSR_DETECT_MASK) != BSR_DETECT_VALUE)
		goto out;

	/*
	 * Switch to bank 1 and check the base address register.
	 * XXX INVASIVE!
	 */
	bus_space_write_2(iot, ioh, BANK_SELECT_REG_W, 1);
	tmp = bus_space_read_2(iot, ioh, BASE_ADDR_REG_W);
	if (ia->ia_iobase != ((tmp >> 3) & 0x3e0))
		goto out;

	/*
	 * Check for a recognized chip id.
	 * XXX INVASIVE!
	 */
	bus_space_write_2(iot, ioh, BANK_SELECT_REG_W, 3);
	tmp = bus_space_read_2(iot, ioh, REVISION_REG_W);
	if (smc91cxx_idstrs[RR_ID(tmp)] == NULL)
		goto out;

	/*
	 * Assume we have an SMC91Cxx.
	 */
	ia->ia_iosize = SMC_IOSIZE;
	rv = 1;

 out:
	bus_space_unmap(iot, ioh, SMC_IOSIZE);
	return (rv);
}

void
sm_isa_attach(struct device *parent, struct device *self, void *aux)
{
	struct sm_isa_softc *isc = (struct sm_isa_softc *)self;
	struct smc91cxx_softc *sc = &isc->sc_smc;
	struct isa_attach_args *ia = aux;
	bus_space_tag_t iot = ia->ia_iot;
	bus_space_handle_t ioh;

	printf("\n");

	/* Map i/o space. */
	if (bus_space_map(iot, ia->ia_iobase, ia->ia_iosize, 0, &ioh))
		panic("sm_isa_attach: can't map i/o space");

	sc->sc_bst = iot;
	sc->sc_bsh = ioh;

	/* should always be enabled */
	sc->sc_enabled = 1;

	/* XXX Should get Ethernet address from EEPROM!! */

	/* Perform generic initialization. */
	smc91cxx_attach(sc, NULL);

	/* Establish the interrupt handler. */
	isc->sc_ih = isa_intr_establish(ia->ia_ic, ia->ia_irq, IST_EDGE,
	    IPL_NET, smc91cxx_intr, sc, sc->sc_dev.dv_xname);
	if (isc->sc_ih == NULL)
		printf("%s: couldn't establish interrupt handler\n",
		    sc->sc_dev.dv_xname);
}
