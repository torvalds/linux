/*	$OpenBSD: aplpwm.c,v 1.1 2022/11/21 21:48:06 kettenis Exp $	*/
/*
 * Copyright (c) 2022 Mark Kettenis <kettenis@openbsd.org>
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

#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_clock.h>
#include <dev/ofw/ofw_misc.h>
#include <dev/ofw/ofw_power.h>
#include <dev/ofw/fdt.h>

#define PWM_CTRL		0x0000
#define  PWM_CTRL_EN		(1 << 0)
#define  PWM_CTRL_UPDATE	(1 << 5)
#define  PWM_CTRL_OUTPUT_EN	(1 << 14)
#define PWM_OFF_CYCLES		0x0018
#define PWM_ON_CYCLES		0x001c

#define NS_PER_S		1000000000

#define HREAD4(sc, reg)							\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))

struct aplpwm_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;

	uint64_t		sc_clkin;
	struct pwm_device	sc_pd;
};

int	aplpwm_match(struct device *, void *, void *);
void	aplpwm_attach(struct device *, struct device *, void *);

const struct cfattach aplpwm_ca = {
	sizeof (struct aplpwm_softc), aplpwm_match, aplpwm_attach
};

struct cfdriver aplpwm_cd = {
	NULL, "aplpwm", DV_DULL
};

int	aplpwm_get_state(void *, uint32_t *, struct pwm_state *);
int	aplpwm_set_state(void *, uint32_t *, struct pwm_state *);

int
aplpwm_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "apple,s5l-fpwm");
}

void
aplpwm_attach(struct device *parent, struct device *self, void *aux)
{
	struct aplpwm_softc *sc = (struct aplpwm_softc *)self;
	struct fdt_attach_args *faa = aux;

	if (faa->fa_nreg < 1) {
		printf(": no registers\n");
		return;
	}

	sc->sc_iot = faa->fa_iot;
	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_ioh)) {
		printf(": can't map registers\n");
		return;
	}

	sc->sc_clkin = clock_get_frequency(faa->fa_node, NULL);
	if (sc->sc_clkin == 0) {
		printf(": no clock\n");
		return;
	}

	printf("\n");

	power_domain_enable(faa->fa_node);

	sc->sc_pd.pd_node = faa->fa_node;
	sc->sc_pd.pd_cookie = sc;
	sc->sc_pd.pd_get_state = aplpwm_get_state;
	sc->sc_pd.pd_set_state = aplpwm_set_state;
	pwm_register(&sc->sc_pd);
}

int
aplpwm_get_state(void *cookie, uint32_t *cells, struct pwm_state *ps)
{
	struct aplpwm_softc *sc = cookie;
	uint64_t on_cycles, off_cycles;
	uint32_t ctrl;

	ctrl = HREAD4(sc, PWM_CTRL);
	on_cycles = HREAD4(sc, PWM_ON_CYCLES);
	off_cycles = HREAD4(sc, PWM_OFF_CYCLES);

	memset(ps, 0, sizeof(struct pwm_state));
	ps->ps_period = ((on_cycles + off_cycles) * NS_PER_S) / sc->sc_clkin;
	ps->ps_pulse_width = (on_cycles * NS_PER_S) / sc->sc_clkin;
	if ((ctrl & PWM_CTRL_EN) && (ctrl & PWM_CTRL_OUTPUT_EN))
		ps->ps_enabled = 1;

	return 0;
}

int
aplpwm_set_state(void *cookie, uint32_t *cells, struct pwm_state *ps)
{
	struct aplpwm_softc *sc = cookie;
	uint64_t cycles, on_cycles, off_cycles;
	uint32_t ctrl;

	if (ps->ps_pulse_width > ps->ps_period)
		return EINVAL;

	cycles = (ps->ps_period * sc->sc_clkin) / NS_PER_S;
	on_cycles = (ps->ps_pulse_width * sc->sc_clkin) / NS_PER_S;
	off_cycles = cycles - on_cycles;
	if (on_cycles > UINT32_MAX || off_cycles > UINT32_MAX)
		return EINVAL;

	if (ps->ps_enabled)
		ctrl = PWM_CTRL_EN | PWM_CTRL_OUTPUT_EN | PWM_CTRL_UPDATE;
	else
		ctrl = 0;

	HWRITE4(sc, PWM_ON_CYCLES, on_cycles);
	HWRITE4(sc, PWM_OFF_CYCLES, off_cycles);
	HWRITE4(sc, PWM_CTRL, ctrl);

	return 0;
}
