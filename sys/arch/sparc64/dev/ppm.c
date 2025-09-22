/*	$OpenBSD: ppm.c,v 1.3 2021/10/24 17:05:04 mpi Exp $	*/

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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>

#include <machine/bus.h>
#include <machine/autoconf.h>
#include <machine/openfirm.h>

#include <sparc64/dev/ebusreg.h>
#include <sparc64/dev/ebusvar.h>

#define	BBC_GPIOBANK_INDEX	0x0
#define	BBC_GPIOBANK_DATA	0x1

#define	BBC_GPIO_PORT1_DATA	0x0
#define	BBC_GPIO_PORT2_DATA	0x4

#define	GPIO_P2D_LED		0x2

struct ppm_softc {
	struct device		sc_dv;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_estarh;
	bus_space_handle_t	sc_rioh;
	bus_space_handle_t	sc_gpiobankh;
	bus_space_handle_t	sc_gpioh;
	struct blink_led	sc_blink;
};

int	ppm_match(struct device *, void *, void *);
void	ppm_attach(struct device *, struct device *, void *);

const struct cfattach ppm_ca = {
	sizeof(struct ppm_softc), ppm_match, ppm_attach
};

struct cfdriver ppm_cd = {
	NULL, "ppm", DV_DULL
};

void ppm_led_blink(void *, int);

int
ppm_match(struct device *parent, void *match, void *aux)
{
	struct ebus_attach_args *ea = aux;

	if (strcmp(ea->ea_name, "ppm"))
		return (0);
	return (1);
}

void
ppm_attach(struct device *parent, struct device *self, void *aux)
{
	struct ppm_softc *sc = (void *)self;
	struct ebus_attach_args *ea = aux;
	u_int8_t reg;

	sc->sc_iot = ea->ea_memtag;

	if (ea->ea_nregs < 4) {
		printf(": need %d regs\n", 4);
		return;
	}

#if 0
	if (ebus_bus_map(sc->sc_iot, 0,
	    EBUS_PADDR_FROM_REG(&ea->ea_regs[0]), ea->ea_regs[0].size,
	    0, 0, &sc->sc_estarh)) {
		printf(": failed to map estar\n");
		return;
	}
#endif
#if 0
	if (ebus_bus_map(sc->sc_iot, 0,
	    EBUS_PADDR_FROM_REG(&ea->ea_regs[1]), ea->ea_regs[1].size,
	    0, 0, &sc->sc_rioh)) {
		printf(": failed to map riohr\n");
		return;
	}
#endif

	if (ebus_bus_map(sc->sc_iot, 0,
	    EBUS_PADDR_FROM_REG(&ea->ea_regs[2]), ea->ea_regs[2].size,
	    0, 0, &sc->sc_gpiobankh)) {
		printf(": failed to map gpiobank\n");
		return;
	}

	if (ebus_bus_map(sc->sc_iot, 0,
	    EBUS_PADDR_FROM_REG(&ea->ea_regs[3]), ea->ea_regs[3].size,
	    0, 0, &sc->sc_gpioh)) {
		printf(": failed to map gpio\n");
		return;
	}

	bus_space_write_1(sc->sc_iot, sc->sc_gpiobankh,
	    BBC_GPIOBANK_INDEX, 0x22);
	bus_space_read_1(sc->sc_iot, sc->sc_gpiobankh, BBC_GPIOBANK_INDEX);
	reg = bus_space_read_1(sc->sc_iot, sc->sc_gpiobankh, BBC_GPIOBANK_DATA);
	reg &= 0x7f;
	bus_space_write_1(sc->sc_iot, sc->sc_gpiobankh,BBC_GPIOBANK_INDEX, reg);
	bus_space_read_1(sc->sc_iot, sc->sc_gpiobankh, BBC_GPIOBANK_INDEX);


	sc->sc_blink.bl_func = ppm_led_blink;
	sc->sc_blink.bl_arg = sc;
	blink_led_register(&sc->sc_blink);
	printf("\n");
}

void
ppm_led_blink(void *vsc, int on)
{
	struct ppm_softc *sc = vsc;
	u_int8_t r;

	r = bus_space_read_1(sc->sc_iot, sc->sc_gpioh, BBC_GPIO_PORT1_DATA);
	if (on)
		r |= GPIO_P2D_LED;
	else
		r &= ~GPIO_P2D_LED;
	bus_space_write_1(sc->sc_iot, sc->sc_gpioh, BBC_GPIO_PORT1_DATA, r);
	bus_space_read_1(sc->sc_iot, sc->sc_gpioh, BBC_GPIO_PORT1_DATA);
}
