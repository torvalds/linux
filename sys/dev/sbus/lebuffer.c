/*	$OpenBSD: lebuffer.c,v 1.11 2022/03/13 13:34:54 mpi Exp $	*/
/*	$NetBSD: lebuffer.c,v 1.12 2002/03/11 16:00:57 pk Exp $ */

/*-
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/errno.h>
#include <sys/device.h>
#include <sys/malloc.h>

#include <machine/bus.h>
#include <machine/autoconf.h>
#include <machine/cpu.h>

#include <dev/sbus/sbusvar.h>
#include <dev/sbus/lebuffervar.h>

int	lebufprint(void *, const char *);
int	lebufmatch(struct device *, void *, void *);
void	lebufattach(struct device *, struct device *, void *);

const struct cfattach lebuffer_ca = {
	sizeof(struct lebuf_softc), lebufmatch, lebufattach
};

int
lebufprint(void *aux, const char *busname)
{
	struct sbus_attach_args *sa = aux;
	bus_space_tag_t t = sa->sa_bustag;
	struct lebuf_softc *sc = t->cookie;

	sa->sa_bustag = sc->sc_bustag;	/* XXX */
	sbus_print(aux, busname);	/* XXX */
	sa->sa_bustag = t;		/* XXX */
	return (UNCONF);
}

int
lebufmatch(struct device *parent, void *vcf, void *aux)
{
	struct sbus_attach_args *sa = aux;
	struct cfdata *cf = vcf;

	return (strcmp(cf->cf_driver->cd_name, sa->sa_name) == 0);
}

/*
 * Attach all the sub-devices we can find
 */
void
lebufattach(struct device *parent, struct device *self, void *aux)
{
	struct sbus_attach_args *sa = aux;
	struct lebuf_softc *sc = (void *)self;
	int node;
	int sbusburst;
	struct sparc_bus_space_tag *sbt;
	bus_space_handle_t bh;

	sc->sc_bustag = sa->sa_bustag;
	sc->sc_dmatag = sa->sa_dmatag;

	if (sbus_bus_map(sa->sa_bustag,
	    sa->sa_slot, sa->sa_offset, sa->sa_size,
	    BUS_SPACE_MAP_LINEAR, 0, &bh) != 0) {
		printf("%s: attach: cannot map registers\n", self->dv_xname);
		return;
	}

	/*
	 * This device's "register space" is just a buffer where the
	 * Lance ring-buffers can be stored. Note the buffer's location
	 * and size, so the `le' driver can pick them up.
	 */
	sc->sc_buffer = (void *)bus_space_vaddr(sa->sa_bustag, bh);
	sc->sc_bufsiz = sa->sa_size;

	node = sc->sc_node = sa->sa_node;

	/*
	 * Get transfer burst size from PROM
	 */
	sbusburst = ((struct sbus_softc *)parent)->sc_burst;
	if (sbusburst == 0)
		sbusburst = SBUS_BURST_32 - 1; /* 1->16 */

	sc->sc_burst = getpropint(node, "burst-sizes", -1);
	if (sc->sc_burst == -1)
		/* take SBus burst sizes */
		sc->sc_burst = sbusburst;

	/* Clamp at parent's burst sizes */
	sc->sc_burst &= sbusburst;

	/* Allocate a bus tag */
	sbt = malloc(sizeof(*sbt), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (sbt == NULL) {
		printf("%s: attach: out of memory\n", self->dv_xname);
		return;
	}

	printf(": %dK memory\n", sc->sc_bufsiz / 1024);

	sbt->cookie = sc;
	sbt->parent = sc->sc_bustag;
	sbt->asi = sbt->parent->asi;
	sbt->sasi = sbt->parent->sasi;

	/* search through children */
	for (node = firstchild(node); node; node = nextsibling(node)) {
		struct sbus_attach_args sa;
		sbus_setup_attach_args((struct sbus_softc *)parent,
		    sbt, sc->sc_dmatag, node, &sa);
		(void)config_found(&sc->sc_dev, (void *)&sa, lebufprint);
		sbus_destroy_attach_args(&sa);
	}
}
