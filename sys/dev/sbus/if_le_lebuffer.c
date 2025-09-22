/*	$OpenBSD: if_le_lebuffer.c,v 1.14 2022/03/13 13:34:54 mpi Exp $	*/
/*	$NetBSD: if_le_lebuffer.c,v 1.10 2002/03/11 16:00:56 pk Exp $	*/

/*-
 * Copyright (c) 1997, 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Charles M. Hannum; Jason R. Thorpe of the Numerical Aerospace
 * Simulation Facility, NASA Ames Research Center; Paul Kranenburg.
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
#include <sys/syslog.h>
#include <sys/socket.h>
#include <sys/device.h>
#include <sys/malloc.h>

#include <net/if.h>
#include <net/if_media.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <machine/bus.h>
#include <machine/intr.h>
#include <machine/autoconf.h>

#include <dev/sbus/sbusvar.h>
#include <dev/sbus/lebuffervar.h>

#include <dev/ic/lancereg.h>
#include <dev/ic/lancevar.h>
#include <dev/ic/am7990reg.h>
#include <dev/ic/am7990var.h>

/*
 * LANCE registers.
 */
#define LEREG1_RDP	0	/* Register Data port */
#define LEREG1_RAP	2	/* Register Address port */

struct	le_softc {
	struct	am7990_softc	sc_am7990;	/* glue to MI code */
	bus_space_tag_t		sc_bustag;
	bus_dma_tag_t		sc_dmatag;
	bus_space_handle_t	sc_reg;		/* LANCE registers */
};


int	lematch_lebuffer(struct device *, void *, void *);
void	leattach_lebuffer(struct device *, struct device *, void *);

/*
 * Media types supported.
 */
static uint64_t lemedia[] = {
	IFM_ETHER | IFM_10_T
};

const struct cfattach le_lebuffer_ca = {
	sizeof(struct le_softc), lematch_lebuffer, leattach_lebuffer
};

extern struct cfdriver le_cd;

struct cfdriver lebuffer_cd = {
	NULL, "lebuffer", DV_DULL
};

void le_lebuffer_wrcsr(struct lance_softc *, uint16_t, uint16_t);
uint16_t le_lebuffer_rdcsr(struct lance_softc *, uint16_t);

void
le_lebuffer_wrcsr(struct lance_softc *sc, uint16_t port, uint16_t val)
{
	struct le_softc *lesc = (struct le_softc *)sc;

	bus_space_write_2(lesc->sc_bustag, lesc->sc_reg, LEREG1_RAP, port);
	bus_space_barrier(lesc->sc_bustag, lesc->sc_reg, LEREG1_RAP, 2,
	    BUS_SPACE_BARRIER_WRITE);
	bus_space_write_2(lesc->sc_bustag, lesc->sc_reg, LEREG1_RDP, val);
	bus_space_barrier(lesc->sc_bustag, lesc->sc_reg, LEREG1_RDP, 2,
	    BUS_SPACE_BARRIER_WRITE);

#if defined(SUN4M)
	/*
	 * We need to flush the SBus->MBus write buffers. This can most
	 * easily be accomplished by reading back the register that we
	 * just wrote (thanks to Chris Torek for this solution).
	 */
	if (CPU_ISSUN4M) {
		volatile uint16_t discard;
		discard = bus_space_read_2(lesc->sc_bustag, lesc->sc_reg,
		    LEREG1_RDP);
	}
#endif
}

uint16_t
le_lebuffer_rdcsr(struct lance_softc *sc, uint16_t port)
{
	struct le_softc *lesc = (struct le_softc *)sc;

	bus_space_write_2(lesc->sc_bustag, lesc->sc_reg, LEREG1_RAP, port);
	bus_space_barrier(lesc->sc_bustag, lesc->sc_reg, LEREG1_RAP, 2,
	    BUS_SPACE_BARRIER_WRITE);
	return (bus_space_read_2(lesc->sc_bustag, lesc->sc_reg, LEREG1_RDP));
}

int
lematch_lebuffer(struct device *parent, void *vcf, void *aux)
{
	struct sbus_attach_args *sa = aux;
	struct cfdata *cf = vcf;

	return (strcmp(cf->cf_driver->cd_name, sa->sa_name) == 0);
}


void
leattach_lebuffer(struct device *parent, struct device *self, void *aux)
{
	struct sbus_attach_args *sa = aux;
	struct le_softc *lesc = (struct le_softc *)self;
	struct lance_softc *sc = &lesc->sc_am7990.lsc;
	struct lebuf_softc *lebuf = (struct lebuf_softc *)parent;
	/* XXX the following declarations should be elsewhere */
	extern void myetheraddr(u_char *);

	lesc->sc_bustag = sa->sa_bustag;
	lesc->sc_dmatag = sa->sa_dmatag;

	if (sbus_bus_map(sa->sa_bustag,
	    sa->sa_slot, sa->sa_offset, sa->sa_size,
	    0, 0, &lesc->sc_reg)) {
		printf(": cannot map registers\n");
		return;
	}

	sc->sc_mem = lebuf->sc_buffer;
	sc->sc_memsize = lebuf->sc_bufsiz;
	sc->sc_addr = 0; /* Lance view is offset by buffer location */
	lebuf->attached = 1;

	/* That old black magic... */
	sc->sc_conf3 = getpropint(sa->sa_node, "busmaster-regval",
	    LE_C3_BSWP | LE_C3_ACON | LE_C3_BCON);

	myetheraddr(sc->sc_arpcom.ac_enaddr);

	sc->sc_supmedia = lemedia;
	sc->sc_nsupmedia = nitems(lemedia);
	sc->sc_defaultmedia = sc->sc_supmedia[sc->sc_nsupmedia - 1];

	sc->sc_copytodesc = lance_copytobuf_contig;
	sc->sc_copyfromdesc = lance_copyfrombuf_contig;
	sc->sc_copytobuf = lance_copytobuf_contig;
	sc->sc_copyfrombuf = lance_copyfrombuf_contig;
	sc->sc_zerobuf = lance_zerobuf_contig;

	sc->sc_rdcsr = le_lebuffer_rdcsr;
	sc->sc_wrcsr = le_lebuffer_wrcsr;

	am7990_config(&lesc->sc_am7990);

	/* Establish interrupt handler */
	if (sa->sa_nintr != 0)
		(void)bus_intr_establish(lesc->sc_bustag, sa->sa_pri,
		    IPL_NET, 0, am7990_intr, sc, self->dv_xname);
}
