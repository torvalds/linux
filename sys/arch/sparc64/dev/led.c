/*	$OpenBSD: led.c,v 1.3 2021/10/24 17:05:03 mpi Exp $	*/

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
 * Driver for leds on Fire V215/V245.
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/device.h>
#include <sys/malloc.h>
#include <sys/systm.h>
#include <sys/timeout.h>

#include <machine/autoconf.h>
#include <machine/bus.h>
#include <machine/cpu.h>

#include <sparc64/dev/ebusreg.h>
#include <sparc64/dev/ebusvar.h>

/*
 * Register access is indirect, through an address and data port.
 */

#define	EPIC_DATA			0x40
#define	EPIC_ADDR			0x41
#define	EPIC_WRITE_MASK			0x80

#define	EPIC_FW_VERSION			0x05
#define	EPIC_LED_STATE0			0x06

#define	EPIC_ALERT_LED_MASK		0x0C
#define	EPIC_ALERT_LED_OFF		0x00
#define	EPIC_ALERT_LED_ON		0x04

#define	EPIC_POWER_LED_MASK		0x30
#define	EPIC_POWER_LED_OFF		0x00
#define	EPIC_POWER_LED_ON		0x10
#define	EPIC_POWER_LED_SB_BLINK		0x20
#define EPIC_POWER_LED_FAST_BLINK	0x30

struct led_softc {
	struct device		sc_dv;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;

	struct timeout		sc_to;
	int			sc_on;
	struct blink_led	sc_blink;
};

int	led_match(struct device *, void *, void *);
void	led_attach(struct device *, struct device *, void *);

const struct cfattach led_ca = {
	sizeof(struct led_softc), led_match, led_attach
};

struct cfdriver led_cd = {
	NULL, "led", DV_DULL
};

void led_blink(void *, int);
void led_blink_finish(void *);

int
led_match(struct device *parent, void *cf, void *aux)
{
	struct ebus_attach_args *ea = aux;
	char *str;

	if (strcmp("env-monitor", ea->ea_name) != 0)
		return (0);

	str = getpropstring(ea->ea_node, "compatible");
	if (strcmp(str, "epic") == 0)
		return (1);

	return (0);
}

void
led_attach(struct device *parent, struct device *self, void *aux)
{
	struct led_softc *sc = (void *)self;
	struct ebus_attach_args *ea = aux;
	int rev;

	if (ebus_bus_map(ea->ea_iotag, 0,
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

	bus_space_write_1(sc->sc_iot, sc->sc_ioh, EPIC_ADDR, EPIC_FW_VERSION);
	delay(10000);
	rev = bus_space_read_1(sc->sc_iot, sc->sc_ioh, EPIC_DATA);
	printf(": rev 0x%02x\n", rev);

	/* Turn off the alert LED. */
	bus_space_write_1(sc->sc_iot, sc->sc_ioh,
	    EPIC_WRITE_MASK, EPIC_ALERT_LED_MASK);
	bus_space_write_1(sc->sc_iot, sc->sc_ioh,
	    EPIC_ADDR, EPIC_LED_STATE0);
	delay(10000);
	bus_space_write_1(sc->sc_iot, sc->sc_ioh,
	    EPIC_DATA, EPIC_ALERT_LED_OFF);

	timeout_set(&sc->sc_to, led_blink_finish, sc);

	sc->sc_blink.bl_func = led_blink;
	sc->sc_blink.bl_arg = sc;
	blink_led_register(&sc->sc_blink);
}

void
led_blink(void *v, int on)
{
	struct led_softc *sc = v;

	sc->sc_on = on;
	bus_space_write_1(sc->sc_iot, sc->sc_ioh, EPIC_ADDR, EPIC_LED_STATE0);
	timeout_add_msec(&sc->sc_to, 10);
}

void
led_blink_finish(void *v)
{
	struct led_softc *sc = v;
	u_int8_t reg;

	if (sc->sc_on)
		reg = EPIC_ALERT_LED_ON;
	else
		reg = EPIC_ALERT_LED_OFF;

	bus_space_write_1(sc->sc_iot, sc->sc_ioh, EPIC_DATA, reg);
}
