/*	$OpenBSD: auxio.c,v 1.12 2025/06/26 20:43:41 miod Exp $	*/
/*	$NetBSD: auxio.c,v 1.1 2000/04/15 03:08:13 mrg Exp $	*/

/*
 * Copyright (c) 2000 Matthew R. Green
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
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * AUXIO registers support on the sbus & ebus2.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/device.h>
#include <sys/timeout.h>
#include <sys/kernel.h>

#include <machine/autoconf.h>
#include <machine/cpu.h>

#include <sparc64/dev/ebusreg.h>
#include <sparc64/dev/ebusvar.h>
#include <sparc64/dev/sbusvar.h>
#include <sparc64/dev/auxioreg.h>
#include <sparc64/dev/auxiovar.h>

#include "fdc.h"

#define	AUXIO_ROM_NAME		"auxio"

/*
 * ebus code.
 */
int	auxio_ebus_match(struct device *, void *, void *);
void	auxio_ebus_attach(struct device *, struct device *, void *);
int	auxio_sbus_match(struct device *, void *, void *);
void	auxio_sbus_attach(struct device *, struct device *, void *);
void	auxio_attach_common(struct auxio_softc *);

const struct cfattach auxio_ebus_ca = {
	sizeof(struct auxio_softc), auxio_ebus_match, auxio_ebus_attach
};

const struct cfattach auxio_sbus_ca = {
	sizeof(struct auxio_softc), auxio_sbus_match, auxio_sbus_attach
};

struct cfdriver auxio_cd = {
	NULL, "auxio", DV_DULL
};

void auxio_led_blink(void *, int);
void auxio_flip(struct auxio_softc *, uint32_t, uint32_t);

int
auxio_ebus_match(struct device *parent, void *cf, void *aux)
{
	struct ebus_attach_args *ea = aux;

	return (strcmp(AUXIO_ROM_NAME, ea->ea_name) == 0);
}

void
auxio_ebus_attach(struct device *parent, struct device *self, void *aux)
{
	struct auxio_softc *sc = (struct auxio_softc *)self;
	struct ebus_attach_args *ea = aux;

	if (ea->ea_nregs < 1 || ea->ea_nvaddrs < 1) {
		printf(": no registers??\n");
		return;
	}

	sc->sc_tag = ea->ea_memtag;
	sc->sc_flags = AUXIO_EBUS;

	if (bus_space_map(sc->sc_tag, ea->ea_vaddrs[0], sizeof(u_int32_t),
	    BUS_SPACE_MAP_PROMADDRESS, &sc->sc_led)) {
		printf(": unable to map LED\n");
		return;
	}

	auxio_attach_common(sc);
}

int
auxio_sbus_match(struct device *parent, void *cf, void *aux)
{
	struct sbus_attach_args *sa = aux;

	return (strcmp(AUXIO_ROM_NAME, sa->sa_name) == 0);
}

void
auxio_sbus_attach(struct device *parent, struct device *self, void *aux)
{
	struct auxio_softc *sc = (struct auxio_softc *)self;
	struct sbus_attach_args *sa = aux;

	sc->sc_tag = sa->sa_bustag;

	if (sa->sa_nreg < 1 || sa->sa_npromvaddrs < 1) {
		printf(": no registers??\n");
		return;
	}

	if (sa->sa_nreg != 1 || sa->sa_npromvaddrs != 1) {
		printf(": not 1 (%d/%d) registers??", sa->sa_nreg, sa->sa_npromvaddrs);
		return;
	}

	/* sbus auxio only has one set of registers */
	sc->sc_flags = AUXIO_SBUS;
	if (bus_space_map(sc->sc_tag, sa->sa_promvaddr, 1,
	    BUS_SPACE_MAP_PROMADDRESS, &sc->sc_led)) {
		printf(": couldn't map registers\n");
		return;
	}

	auxio_attach_common(sc);
}

void
auxio_attach_common(struct auxio_softc *sc)
{
	sc->sc_blink.bl_func = auxio_led_blink;
	sc->sc_blink.bl_arg = sc;
	blink_led_register(&sc->sc_blink);
	printf("\n");
}

void
auxio_flip(struct auxio_softc *sc, uint32_t mask, uint32_t set)
{
	uint32_t led;
	int s;

	s = splhigh();

	if (sc->sc_flags & AUXIO_EBUS)
		led = letoh32(bus_space_read_4(sc->sc_tag, sc->sc_led, 0));
	else
		led = bus_space_read_1(sc->sc_tag, sc->sc_led, 0);

	led = (led & ~mask) | set;

	if (sc->sc_flags & AUXIO_EBUS)
		bus_space_write_4(sc->sc_tag, sc->sc_led, 0, htole32(led));
	else
		bus_space_write_1(sc->sc_tag, sc->sc_led, 0, led);

	splx(s);
}

void
auxio_led_blink(void *vsc, int on)
{
	struct auxio_softc *sc = vsc;
	auxio_flip(sc, AUXIO_LED_LED, on ? AUXIO_LED_LED : 0);
}

#if NFDC > 0
int
auxio_fd_control(u_int32_t bits)
{
	struct auxio_softc *sc;

	if (auxio_cd.cd_ndevs == 0) {
		return ENXIO;
	}

	/*
	 * XXX This does not handle > 1 auxio correctly.
	 * We'll assume the floppy drive is tied to first auxio found.
	 */
	sc = (struct auxio_softc *)auxio_cd.cd_devs[0];
	auxio_flip(sc, AUXIO_LED_FLOPPY_MASK, bits);
	return 0;
}
#endif
