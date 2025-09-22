/*	$OpenBSD: power.c,v 1.10 2022/10/16 01:22:39 jsg Exp $	*/

/*
 * Copyright (c) 2006 Jason L. Wright (jason@thought.net)
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
 * Driver for power-button device on U5, U10, etc.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/device.h>
#include <sys/conf.h>
#include <sys/timeout.h>
#include <sys/proc.h>
#include <sys/signalvar.h>

#include <machine/bus.h>
#include <machine/autoconf.h>
#include <machine/openfirm.h>

#include <sparc64/dev/ebusreg.h>
#include <sparc64/dev/ebusvar.h>

#define	POWER_REG		0

#define	POWER_REG_CPWR_OFF	0x00000002	/* courtesy power off */
#define	POWER_REG_SPWR_OFF	0x00000001	/* system power off */

struct power_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
	struct intrhand		*sc_ih;
};

int	power_match(struct device *, void *, void *);
void	power_attach(struct device *, struct device *, void *);
int	power_intr(void *);

const struct cfattach power_ca = {
	sizeof(struct power_softc), power_match, power_attach
};

struct cfdriver power_cd = {
	NULL, "power", DV_DULL
};

int
power_match(struct device *parent, void *match, void *aux)
{
	struct ebus_attach_args *ea = aux;

	if (strcmp(ea->ea_name, "power") == 0)
		return (1);
	return (0);
}

void
power_attach(struct device *parent, struct device *self, void *aux)
{
	struct power_softc *sc = (void *)self;
	struct ebus_attach_args *ea = aux;

	if (ea->ea_nregs < 1) {
		printf(": no registers\n");
		return;
	}

	/* Use prom address if available, otherwise map it. */
	if (ea->ea_nvaddrs) {
		if (bus_space_map(ea->ea_memtag, ea->ea_vaddrs[0], 0,
		    BUS_SPACE_MAP_PROMADDRESS, &sc->sc_ioh)) {
			printf(": can't map PROM register space\n");
			return;
		}
		sc->sc_iot = ea->ea_memtag;
	} else if (ebus_bus_map(ea->ea_iotag, 0,
	    EBUS_PADDR_FROM_REG(&ea->ea_regs[0]),
	    ea->ea_regs[0].size, 0, 0, &sc->sc_ioh) == 0) {
		sc->sc_iot = ea->ea_iotag;
	} else if (ebus_bus_map(ea->ea_memtag, 0,
	    EBUS_PADDR_FROM_REG(&ea->ea_regs[0]),
	    ea->ea_regs[0].size, 0, 0, &sc->sc_ioh) == 0) {
		sc->sc_iot = ea->ea_memtag;
	} else {
		printf("%s: can't map register space\n", self->dv_xname);
		return;
	}

	if (ea->ea_nintrs > 0 && OF_getproplen(ea->ea_node, "button") >= 0) {
	        sc->sc_ih = bus_intr_establish(sc->sc_iot, ea->ea_intrs[0],
		    IPL_BIO, 0, power_intr, sc, self->dv_xname);
		if (sc->sc_ih == NULL) {
			printf(": can't establish interrupt\n");
			return;
		}
	}
	printf("\n");
}

int
power_intr(void *vsc)
{
	extern int allowpowerdown;

	if (allowpowerdown == 1) {
		allowpowerdown = 0;
		prsignal(initprocess, SIGUSR2);
	}
	return (1);
}
