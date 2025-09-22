/*	$OpenBSD: lcd.c,v 1.6 2022/03/13 08:04:38 mpi Exp $	*/

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
#include <sys/device.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/timeout.h>

#include <machine/autoconf.h>
#include <machine/bus.h>
#include <machine/cpu.h>
#include <machine/pdc.h>

#define LCD_CLS		0x01
#define LCD_HOME	0x02
#define LCD_LOCATE(X, Y)	(((Y) & 1 ? 0xc0 : 0x80) | ((X) & 0x0f))

struct lcd_softc {
	struct device		sc_dv;

	bus_space_tag_t		sc_iot;
	bus_space_handle_t 	sc_cmdh, sc_datah;

	u_int			sc_delay;
	u_int8_t		sc_heartbeat[3];

	struct timeout		sc_to;
	int			sc_on;
	struct blink_led	sc_blink;
};

int	lcd_match(struct device *, void *, void *);
void	lcd_attach(struct device *, struct device *, void *);

const struct cfattach lcd_ca = {
	sizeof(struct lcd_softc), lcd_match, lcd_attach
};

struct cfdriver lcd_cd = {
	NULL, "lcd", DV_DULL
};

void	lcd_mountroot(struct device *);
void	lcd_write(struct lcd_softc *, const char *);
void	lcd_blink(void *, int);
void	lcd_blink_finish(void *);

int
lcd_match(struct device *parent, void *match, void *aux)
{
	struct confargs *ca = aux;

	if (strcmp(ca->ca_name, "lcd") == 0)
		return (1);

	return (0);
}

void
lcd_attach(struct device *parent, struct device *self, void *aux)
{
	struct lcd_softc *sc = (struct lcd_softc *)self;
	struct confargs *ca = aux;
	struct pdc_chassis_lcd *pdc_lcd = (void *)ca->ca_pdc_iodc_read;
	int i;

	sc->sc_iot = ca->ca_iot;
	if (bus_space_map(sc->sc_iot, pdc_lcd->cmd_addr,
		1, 0, &sc->sc_cmdh)) {
		printf(": cannot map cmd register\n");
		return;
	}

	if (bus_space_map(sc->sc_iot, pdc_lcd->data_addr,
		1, 0, &sc->sc_datah)) {
		printf(": cannot map data register\n");
		bus_space_unmap(sc->sc_iot, sc->sc_cmdh, 1);
		return;
	}

	printf(": model %d\n", pdc_lcd->model);

	sc->sc_delay = pdc_lcd->delay;
	for (i = 0; i < 3; i++)
		sc->sc_heartbeat[i] = pdc_lcd->heartbeat[i];

	timeout_set(&sc->sc_to, lcd_blink_finish, sc);

	sc->sc_blink.bl_func = lcd_blink;
	sc->sc_blink.bl_arg = sc;
	blink_led_register(&sc->sc_blink);

	config_mountroot(self, lcd_mountroot);
}

void
lcd_mountroot(struct device *self)
{
	struct lcd_softc *sc = (struct lcd_softc *)self;

	bus_space_write_1(sc->sc_iot, sc->sc_cmdh, 0, LCD_CLS);
	delay(100 * sc->sc_delay);

	bus_space_write_1(sc->sc_iot, sc->sc_cmdh, 0, LCD_LOCATE(0, 0));
	delay(sc->sc_delay);
	lcd_write(sc, "OpenBSD/" MACHINE);
}

void
lcd_write(struct lcd_softc *sc, const char *str)
{
	while (*str) {
		bus_space_write_1(sc->sc_iot, sc->sc_datah, 0, *str++);
		delay(sc->sc_delay);
	}
}

void
lcd_blink(void *v, int on)
{
	struct lcd_softc *sc = v;

	sc->sc_on = on;
	bus_space_write_1(sc->sc_iot, sc->sc_cmdh, 0, sc->sc_heartbeat[0]);
	timeout_add_usec(&sc->sc_to, sc->sc_delay);
}

void
lcd_blink_finish(void *v)
{
	struct lcd_softc *sc = v;
	u_int8_t data;

	if (sc->sc_on)
		data = sc->sc_heartbeat[1];
	else
		data = sc->sc_heartbeat[2];

	bus_space_write_1(sc->sc_iot, sc->sc_datah, 0, data);
}
