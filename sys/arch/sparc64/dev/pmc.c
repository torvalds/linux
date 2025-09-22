/*	$OpenBSD: pmc.c,v 1.5 2021/10/24 17:05:04 mpi Exp $	*/

/*
 * Copyright (c) 2007 Mark Kettenis
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Driver for watchdog device on Blade 1000, Fire 280R, Fire V480 etc.
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/device.h>
#include <sys/malloc.h>
#include <sys/systm.h>

#include <machine/bus.h>
#include <machine/autoconf.h>

#include <sparc64/dev/ebusreg.h>
#include <sparc64/dev/ebusvar.h>

/*
 * Register access is indirect, through an address and data port.
 */

#define	PMC_ADDR	0
#define	PMC_DATA	1

/* Watchdog time-out register. */
#define PMC_WDTO	0x05

struct pmc_softc {
	struct device		sc_dv;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
};

int	pmc_match(struct device *, void *, void *);
void	pmc_attach(struct device *, struct device *, void *);
int	pmc_activate(struct device *, int);
int	pmc_wdog_cb(void *, int);

const struct cfattach pmc_ca = {
	sizeof(struct pmc_softc), pmc_match, pmc_attach, NULL, pmc_activate
};

struct cfdriver pmc_cd = {
	NULL, "pmc", DV_DULL
};

int
pmc_match(struct device *parent, void *cf, void *aux)
{
	struct ebus_attach_args *ea = aux;

	if (strcmp("pmc", ea->ea_name) == 0)
		return (1);
	return (0);
}

void
pmc_attach(struct device *parent, struct device *self, void *aux)
{
	struct pmc_softc *sc = (void *)self;
	struct ebus_attach_args *ea = aux;

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
		printf(": can't map register space\n");
		return;
	}

	printf("\n");

	wdog_register(pmc_wdog_cb, sc);
}

int
pmc_activate(struct device *self, int act)
{
	switch (act) {
	case DVACT_POWERDOWN:
		wdog_shutdown(self);
		break;
	}

	return (0);
}

int
pmc_wdog_cb(void *arg, int period)
{
	struct pmc_softc *sc = arg;
	int mins;

	mins = (period + 59) / 60;
	if (mins > 255)
		mins = 255;

	bus_space_write_1(sc->sc_iot, sc->sc_ioh, PMC_ADDR, PMC_WDTO);
	bus_space_write_1(sc->sc_iot, sc->sc_ioh, PMC_DATA, mins);

	return (mins * 60);
}
