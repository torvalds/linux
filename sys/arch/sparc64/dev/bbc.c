/*	$OpenBSD: bbc.c,v 1.4 2021/10/24 17:05:03 mpi Exp $	*/

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

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/device.h>
#include <sys/malloc.h>
#include <sys/systm.h>

#include <machine/bus.h>
#include <machine/autoconf.h>

#ifdef DDB
#include <machine/db_machdep.h>
#endif

#include <sparc64/dev/ebusreg.h>
#include <sparc64/dev/ebusvar.h>

/* Agent ID */
#define BBC_AID			0x00000

/* Watchdog Action */
#define BBC_WATCHDOG_ACTION	0x00004

/* Perform system reset when watchdog timer expires. */
#define	BBC_WATCHDOG_RESET	0x01

/* Soft_XIR_GEN */
#define BBC_SOFT_XIR_GEN	0x00007

struct bbc_softc {
	struct device		sc_dv;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;

	int			sc_aid;
};

int	bbc_match(struct device *, void *, void *);
void	bbc_attach(struct device *, struct device *, void *);

const struct cfattach bbc_ca = {
	sizeof(struct bbc_softc), bbc_match, bbc_attach
};

struct cfdriver bbc_cd = {
	NULL, "bbc", DV_DULL
};

#ifdef DDB
void	bbc_xir(void *, int);
#endif

int
bbc_match(struct device *parent, void *cf, void *aux)
{
	struct ebus_attach_args *ea = aux;

	if (strcmp("bbc", ea->ea_name) == 0)
		return (1);
	return (0);
}

void
bbc_attach(struct device *parent, struct device *self, void *aux)
{
	struct bbc_softc *sc = (void *)self;
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

	sc->sc_aid = bus_space_read_1(sc->sc_iot, sc->sc_ioh, BBC_AID);
	printf(": AID 0x%02x\n", sc->sc_aid);

	/*
	 * Make sure we actually reset the system when the watchdog
	 * timer expires.
	 */
	bus_space_write_1(sc->sc_iot, sc->sc_ioh,
	    BBC_WATCHDOG_ACTION, BBC_WATCHDOG_RESET);

#ifdef DDB
	db_register_xir(bbc_xir, sc);
#endif
}

#ifdef DDB
void
bbc_xir(void *arg, int cpu)
{
	struct bbc_softc *sc = arg;

	/* Redirect a request to reset all processors to Processor 0. */
	if (cpu == -1)
		cpu = 0;

	/* Check whether we're handling the requested processor. */
	if ((cpu & ~0x7) != sc->sc_aid)
		return;

	bus_space_write_1(sc->sc_iot, sc->sc_ioh,
	    BBC_SOFT_XIR_GEN, 1 << (cpu & 0x7));
}
#endif
