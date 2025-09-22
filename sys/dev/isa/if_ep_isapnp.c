/*	$OpenBSD: if_ep_isapnp.c,v 1.18 2023/09/11 08:41:26 mvs Exp $	*/
/*	$NetBSD: if_ep_isapnp.c,v 1.5 1996/05/12 23:52:36 mycroft Exp $	*/

/*
 * Copyright (c) 1996 Jason R. Thorpe <thorpej@beer.org>
 * Copyright (c) 1994 Herb Peyerl <hpeyerl@beer.org>
 * All rights reserved.
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
 *      This product includes software developed by Herb Peyerl.
 * 4. The name of Herb Peyerl may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Note: Most of the code here was written by Theo de Raadt originally,
 * ie. all the mechanics of probing for all cards on first call and then
 * searching for matching devices on subsequent calls.
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
#include <sys/timeout.h>
#include <sys/queue.h>

#include <net/if.h>
#include <net/if_media.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif

#include <machine/cpu.h>
#include <machine/bus.h>
#include <machine/intr.h>

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>

#include <dev/ic/elink3var.h>
#include <dev/ic/elink3reg.h>

#include <dev/isa/isavar.h>
#include <dev/isa/elink.h>

int ep_isapnp_match(struct device *, void *, void *);
void ep_isapnp_attach(struct device *, struct device *, void *);

const struct cfattach ep_isapnp_ca = {
	sizeof(struct ep_softc), ep_isapnp_match, ep_isapnp_attach
};

/*
 * 3c509 cards on the ISA bus are probed in ethernet address order.
 * The probe sequence requires careful orchestration, and we'd like
 * to allow the irq and base address to be wildcarded. So, we
 * probe all the cards the first time epprobe() is called. On subsequent
 * calls we look for matching cards.
 */
int
ep_isapnp_match(struct device *parent, void *match, void *aux)
{
	/* XXX This should be more intelligent */
	return 1;
}

void
ep_isapnp_attach(struct device *parent, struct device *self, void *aux)
{
	struct ep_softc *sc = (void *)self;
	struct isa_attach_args *ia = aux;
	bus_space_tag_t iot = ia->ia_iot;
	bus_space_handle_t ioh;

	sc->sc_iot = iot = ia->ia_iot;
	sc->sc_ioh = ioh = ia->ipa_io[0].h;
	sc->bustype = EP_BUS_ISA;

	printf(":");

	/* Should look at ia->ia_devident... */
	epconfig(sc, EP_CHIPSET_3C509, NULL);

	sc->sc_ih = isa_intr_establish(ia->ia_ic, ia->ia_irq, IST_EDGE,
	    IPL_NET, epintr, sc, sc->sc_dev.dv_xname);
}
