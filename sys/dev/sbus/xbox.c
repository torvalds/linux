/*	$OpenBSD: xbox.c,v 1.4 2022/03/13 13:34:54 mpi Exp $	*/

/*
 * Copyright (c) 1999 Jason L. Wright (jason@thought.net)
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Driver for the Sun SBus Expansion Subsystem
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/errno.h>
#include <sys/ioctl.h>
#include <sys/syslog.h>
#include <sys/device.h>
#include <sys/malloc.h>

#include <machine/autoconf.h>
#include <machine/bus.h>
#include <machine/cpu.h>
#include <dev/sbus/sbusvar.h>

#include <dev/sbus/xboxreg.h>
#include <dev/sbus/xboxvar.h>

int	xboxmatch(struct device *, void *, void *);
void	xboxattach(struct device *, struct device *, void *);
int	xboxprint(void *, const char *);
int	xbox_fix_range(struct xbox_softc *sc, struct sbus_softc *sbp);

const struct cfattach xbox_ca = {
	sizeof (struct xbox_softc), xboxmatch, xboxattach
};

struct cfdriver xbox_cd = {
	NULL, "xbox", DV_DULL
};

int
xboxmatch(struct device *parent, void *cf, void *aux)
{
	struct sbus_attach_args *sa = aux;

	if (strcmp("SUNW,xbox", sa->sa_name))
		return (0);

	return (1);
}

void    
xboxattach(struct device *parent, struct device *self, void *aux)
{
	struct xbox_softc *sc = (struct xbox_softc *)self;
	struct sbus_attach_args *sa = aux;
	int node = sa->sa_node;
	struct xbox_attach_args xa;
	bus_space_handle_t write0;
	char *s;

	s = getpropstring(node, "model");
	printf(": model %s", s);

	s = getpropstring(node, "child-present");
	if (strcmp(s, "false") == 0) {
		printf(": no devices\n");
		return;
	}

	sc->sc_key = getpropint(node, "write0-key", -1);
	sc->sc_node = node;

	/*
	 * Setup transparent access
	 */

	if (sbus_bus_map(sa->sa_bustag, sa->sa_reg[0].sbr_slot,
	    sa->sa_reg[0].sbr_offset, sa->sa_reg[0].sbr_size, 0, 0,
	    &write0) != 0) {
		printf(": couldn't map write 0 register\n");
		return;
	}

	bus_space_write_4(sa->sa_bustag, write0, 0,
	    (sc->sc_key << 24) | XAC_CTL1_OFFSET |
	    XBOX_CTL1_CSIE | XBOX_CTL1_TRANSPARENT);
	bus_space_write_4(sa->sa_bustag, write0, 0,
	    (sc->sc_key << 24) | XBC_CTL1_OFFSET |
	    XBOX_CTL1_XSIE | XBOX_CTL1_XSBRE | XBOX_CTL1_XSSE);
	DELAY(100);

	bus_space_unmap(sa->sa_bustag, write0, sa->sa_reg[0].sbr_size);

	printf("\n");

	if (xbox_fix_range(sc, (struct sbus_softc *)parent) != 0)
		return;

	bzero(&xa, sizeof xa);
	xa.xa_name = "sbus";
	xa.xa_node = node;
	xa.xa_bustag = sa->sa_bustag;
	xa.xa_dmatag = sa->sa_dmatag;

	(void)config_found(&sc->sc_dev, (void *)&xa, xboxprint);
}

/*
 * Fix up our address ranges based on parent address spaces.
 */
int
xbox_fix_range(struct xbox_softc *sc, struct sbus_softc *sbp)
{
	int error, i, j;

	error = getprop(sc->sc_node, "ranges", sizeof(struct sbus_range),
	    &sc->sc_nrange, (void **)&sc->sc_range);
	if (error != 0) {
		printf("%s: PROM ranges too large\n", sc->sc_dev.dv_xname);
		return (error);
	}

	for (i = 0; i < sc->sc_nrange; i++) {
		for (j = 0; j < sbp->sc_nrange; j++) {
			if (sc->sc_range[i].pspace == sbp->sc_range[j].cspace) {
				sc->sc_range[i].poffset +=
				    sbp->sc_range[j].poffset;
				sc->sc_range[i].pspace =
				    sbp->sc_range[j].pspace;
				break;
			}
		}
	}

	return (0);
}

int
xboxprint(void *args, const char *bus)
{
	struct xbox_attach_args *xa = args;

	if (bus != NULL)
		printf("%s at %s", xa->xa_name, bus);
	return (UNCONF);
}
