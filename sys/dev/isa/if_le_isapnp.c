/*	$OpenBSD: if_le_isapnp.c,v 1.16 2024/08/19 03:08:27 jsg Exp $	*/
/*	$NetBSD: if_le_isa.c,v 1.2 1996/05/12 23:52:56 mycroft Exp $	*/

/*-
 * Copyright (c) 1995 Charles M. Hannum.  All rights reserved.
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Ralph Campbell and Rick Macklem.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)if_le.c	8.2 (Berkeley) 11/16/93
 */

#include "bpfilter.h"
#include "isadma.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/syslog.h>
#include <sys/socket.h>
#include <sys/device.h>

#include <net/if.h>
#include <net/if_media.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <machine/cpu.h>
#include <machine/intr.h>

#include <dev/isa/isareg.h>
#include <dev/isa/isavar.h>
#include <dev/isa/isadmavar.h>

#include <dev/ic/lancereg.h>
#include <dev/ic/lancevar.h>
#include <dev/ic/am7990reg.h>
#include <dev/ic/am7990var.h>

#include <dev/isa/if_levar.h>

int	le_isapnp_match(struct device *, void *, void *);
void	le_isapnp_attach(struct device *, struct device *, void *);

const struct cfattach le_isapnp_ca = {
	sizeof(struct le_softc), le_isapnp_match, le_isapnp_attach
};

int
le_isapnp_match(struct device *parent, void *match, void *aux)
{
	return 1;
}

void
le_isapnp_attach(struct device *parent, struct device *self, void *aux)
{
	struct le_softc *lesc = (void *)self;
	struct isa_attach_args *ia = aux;
	struct lance_softc *sc = &lesc->sc_am7990.lsc;
	bus_space_tag_t iot = lesc->sc_iot;
	bus_space_handle_t ioh = lesc->sc_ioh;
	int i;

	lesc->sc_iot = iot = ia->ia_iot;
	lesc->sc_ioh = ioh = ia->ipa_io[0].h;
	lesc->sc_rap = NE2100_RAP;
	lesc->sc_rdp = NE2100_RDP;
	lesc->sc_card = NE2100;

	/*
	 * Extract the physical MAC address from the ROM.
	 */
	for (i = 0; i < sizeof(sc->sc_arpcom.ac_enaddr); i++)
		sc->sc_arpcom.ac_enaddr[i] = bus_space_read_1(iot, ioh, i);

	sc->sc_mem = malloc(16384, M_DEVBUF, M_NOWAIT);
	if (sc->sc_mem == NULL) {
		printf(": couldn't allocate memory for card\n");
		return;
	}

	sc->sc_conf3 = 0;
	sc->sc_addr = kvtop(sc->sc_mem);
	sc->sc_memsize = 16384;

	sc->sc_copytodesc = lance_copytobuf_contig;
	sc->sc_copyfromdesc = lance_copyfrombuf_contig;
	sc->sc_copytobuf = lance_copytobuf_contig;
	sc->sc_copyfrombuf = lance_copyfrombuf_contig;
	sc->sc_zerobuf = lance_zerobuf_contig;

	sc->sc_rdcsr = le_isa_rdcsr;
	sc->sc_wrcsr = le_isa_wrcsr;
	sc->sc_hwreset = NULL;
	sc->sc_hwinit = NULL;

	am7990_config(&lesc->sc_am7990);

#if NISADMA > 0
	if (ia->ia_drq != DRQUNK)
		isadma_cascade(ia->ia_drq);
#endif

	lesc->sc_ih = isa_intr_establish(ia->ia_ic, ia->ia_irq, IST_EDGE,
	    IPL_NET, le_isa_intredge, sc, sc->sc_dev.dv_xname);
}
