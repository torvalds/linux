/*	$OpenBSD: pwmreg.c,v 1.2 2021/10/24 17:52:26 mpi Exp $	*/
/*
 * Copyright (c) 2019 Mark Kettenis <kettenis@openbsd.org>
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
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/malloc.h>

#include <machine/intr.h>
#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_misc.h>
#include <dev/ofw/ofw_regulator.h>
#include <dev/ofw/fdt.h>

struct pwmreg_softc {
	struct device		sc_dev;
	uint32_t		*sc_pwm;
	uint32_t		sc_dutycycle_unit;
	uint32_t		sc_dutycycle_range[2];

	struct regulator_device	sc_rd;
};

int pwmreg_match(struct device *, void *, void *);
void pwmreg_attach(struct device *, struct device *, void *);

const struct cfattach	pwmreg_ca = {
	sizeof (struct pwmreg_softc), pwmreg_match, pwmreg_attach
};

struct cfdriver pwmreg_cd = {
	NULL, "pwmreg", DV_DULL
};


uint32_t pwmreg_get_voltage(void *);
int	pwmreg_set_voltage(void *, uint32_t);
int	pwmreg_enable(void *, int);

int
pwmreg_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "pwm-regulator");
}

void
pwmreg_attach(struct device *parent, struct device *self, void *aux)
{
	struct pwmreg_softc *sc = (struct pwmreg_softc *)self;
	struct fdt_attach_args *faa = aux;
	int len;

	if (OF_getproplen(faa->fa_node, "voltage-tables") > 0) {
		printf(": voltage table mode unsupported\n");
		return;
	}

	len = OF_getproplen(faa->fa_node, "pwms");
	if (len <= 4) {
		printf(": no pwm\n");
		return;
	}

	sc->sc_pwm = malloc(len, M_DEVBUF, M_WAITOK);
	OF_getpropintarray(faa->fa_node, "pwms", sc->sc_pwm, len);

	sc->sc_dutycycle_unit =
	    OF_getpropint(faa->fa_node, "pwm-dutycycle-unit", 100);
	sc->sc_dutycycle_range[0] = 0;
	sc->sc_dutycycle_range[1] = 100;
	OF_getpropintarray(faa->fa_node, "pwm-dutycycle-range",
	    sc->sc_dutycycle_range, sizeof(sc->sc_dutycycle_range));

	printf("\n");

	sc->sc_rd.rd_node = faa->fa_node;
	sc->sc_rd.rd_cookie = sc;
	sc->sc_rd.rd_get_voltage = pwmreg_get_voltage;
	sc->sc_rd.rd_set_voltage = pwmreg_set_voltage;
	sc->sc_rd.rd_enable = pwmreg_enable;
	regulator_register(&sc->sc_rd);
}

uint32_t
pwmreg_get_voltage(void *cookie)
{
	struct pwmreg_softc *sc = cookie;
	struct pwm_state ps;
	int32_t x0, x1, y0, y1;
	int32_t x, y;

	if (pwm_get_state(sc->sc_pwm, &ps))
		return 0;

	x0 = sc->sc_dutycycle_range[0];
	x1 = sc->sc_dutycycle_range[1];
	y0 = sc->sc_rd.rd_volt_min;
	y1 = sc->sc_rd.rd_volt_max;
	x = (ps.ps_pulse_width * sc->sc_dutycycle_unit) / ps.ps_period;
	y = y0 + (x - x0) * (y1 - y0) / (x1 - x0);
	return y;
}

int
pwmreg_set_voltage(void *cookie, uint32_t voltage)
{
	struct pwmreg_softc *sc = cookie;
	struct pwm_state ps;
	int32_t x0, x1, y0, y1;
	int32_t x, y;

	if (pwm_init_state(sc->sc_pwm, &ps))
		return 0;

	x0 = sc->sc_rd.rd_volt_min;
	x1 = sc->sc_rd.rd_volt_max;
	y0 = sc->sc_dutycycle_range[0];
	y1 = sc->sc_dutycycle_range[1];
	x = voltage;
	y = y0 + (x - x0) * (y1 - y0) / (x1 - x0);

	ps.ps_pulse_width = (y * ps.ps_period) / sc->sc_dutycycle_unit;
	return pwm_set_state(sc->sc_pwm, &ps);
}

int
pwmreg_enable(void *cookie, int on)
{
	struct pwmreg_softc *sc = cookie;
	struct pwm_state ps;
	int error;

	error = pwm_get_state(sc->sc_pwm, &ps);
	if (error)
		return error;

	if (ps.ps_enabled == on)
		return 0;
	
	ps.ps_enabled = on;
	return pwm_set_state(sc->sc_pwm, &ps);
}
