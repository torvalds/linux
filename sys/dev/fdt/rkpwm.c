/*	$OpenBSD: rkpwm.c,v 1.4 2021/10/24 17:52:27 mpi Exp $	*/
/*
 * Copyright (c) 2019 Krystian Lewandowski
 * Copyright (c) 2019 Patrick Wildt <patrick@blueri.se>
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

#include <machine/fdt.h>
#include <machine/bus.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_clock.h>
#include <dev/ofw/ofw_misc.h>
#include <dev/ofw/ofw_pinctrl.h>
#include <dev/ofw/fdt.h>

#define PWM_V2_CNTR		0x00
#define PWM_V2_PERIOD		0x04
#define PWM_V2_DUTY		0x08
#define PWM_V2_CTRL		0x0c
#define  PWM_V2_CTRL_ENABLE		(1 << 0)
#define  PWM_V2_CTRL_CONTINUOUS		(1 << 1)
#define  PWM_V2_CTRL_DUTY_POSITIVE	(1 << 3)
#define  PWM_V2_CTRL_INACTIVE_POSITIVE	(1 << 4)

#define NS_PER_S		1000000000

#define HREAD4(sc, reg)							\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))
#define HSET4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) | (bits))
#define HCLR4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) & ~(bits))

struct rkpwm_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;

	uint32_t		sc_clkin;
	struct pwm_device	sc_pd;
};

int	rkpwm_match(struct device *, void *, void *);
void	rkpwm_attach(struct device *, struct device *, void *);

const struct cfattach rkpwm_ca = {
	sizeof(struct rkpwm_softc), rkpwm_match, rkpwm_attach
};

struct cfdriver rkpwm_cd = {
	NULL, "rkpwm", DV_DULL
};

int	rkpwm_get_state(void *, uint32_t *, struct pwm_state *);
int	rkpwm_set_state(void *, uint32_t *, struct pwm_state *);

int
rkpwm_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return (OF_is_compatible(faa->fa_node, "rockchip,rk3288-pwm") ||
	    OF_is_compatible(faa->fa_node, "rockchip,rk3328-pwm"));
}

void
rkpwm_attach(struct device *parent, struct device *self, void *aux)
{
	struct rkpwm_softc *sc = (struct rkpwm_softc *)self;
	struct fdt_attach_args *faa = aux;

	if (faa->fa_nreg < 1) {
		printf(": no registers\n");
		return;
	}

	sc->sc_clkin = clock_get_frequency(faa->fa_node, NULL);
	if (sc->sc_clkin == 0) {
		printf(": no clock\n");
		return;
	}

	sc->sc_iot = faa->fa_iot;
	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_ioh)) {
		printf(": can't map registers\n");
		return;
	}

	printf("\n");

	pinctrl_byname(faa->fa_node, "default");

	clock_enable_all(faa->fa_node);
	reset_deassert_all(faa->fa_node);

	sc->sc_pd.pd_node = faa->fa_node;
	sc->sc_pd.pd_cookie = sc;
	sc->sc_pd.pd_get_state = rkpwm_get_state;
	sc->sc_pd.pd_set_state = rkpwm_set_state;

	pwm_register(&sc->sc_pd);
}

int
rkpwm_get_state(void *cookie, uint32_t *cells, struct pwm_state *ps)
{
	struct rkpwm_softc *sc = cookie;
	uint32_t idx = cells[0];
	uint64_t rate, cycles, act_cycles;

	if (idx != 0)
		return EINVAL;

	rate = sc->sc_clkin;
	cycles = HREAD4(sc, PWM_V2_PERIOD);
	act_cycles = HREAD4(sc, PWM_V2_DUTY);

	memset(ps, 0, sizeof(struct pwm_state));
	ps->ps_period = (NS_PER_S * cycles) / rate;
	ps->ps_pulse_width = (NS_PER_S * act_cycles) / rate;
	if (HREAD4(sc, PWM_V2_CTRL) & PWM_V2_CTRL_ENABLE)
		ps->ps_enabled = 1;

	return 0;
}

int
rkpwm_set_state(void *cookie, uint32_t *cells, struct pwm_state *ps)
{
	struct rkpwm_softc *sc = cookie;
	uint32_t idx = cells[0];
	uint64_t rate, cycles, act_cycles;

	if (idx != 0)
		return EINVAL;

	HCLR4(sc, PWM_V2_CTRL, PWM_V2_CTRL_ENABLE | PWM_V2_CTRL_CONTINUOUS);

	if (!ps->ps_enabled)
		return 0;

	rate = sc->sc_clkin;
	cycles = (rate * ps->ps_period) / NS_PER_S;
	act_cycles = (rate * ps->ps_pulse_width) / NS_PER_S;
	if (cycles < 1 || act_cycles > cycles)
		return EINVAL;

	HWRITE4(sc, PWM_V2_PERIOD, cycles);
	HWRITE4(sc, PWM_V2_DUTY, act_cycles);

	HCLR4(sc, PWM_V2_CTRL, PWM_V2_CTRL_INACTIVE_POSITIVE);
	HCLR4(sc, PWM_V2_CTRL, PWM_V2_CTRL_DUTY_POSITIVE);

	if (ps->ps_flags & PWM_POLARITY_INVERTED)
		HSET4(sc, PWM_V2_CTRL, PWM_V2_CTRL_INACTIVE_POSITIVE);
	else
		HSET4(sc, PWM_V2_CTRL, PWM_V2_CTRL_DUTY_POSITIVE);

	HSET4(sc, PWM_V2_CTRL, PWM_V2_CTRL_ENABLE | PWM_V2_CTRL_CONTINUOUS);
	return 0;
}
