/*	$OpenBSD: sxipwm.c,v 1.2 2021/10/24 17:52:27 mpi Exp $	*/
/*
 * Copyright (c) 2019 Krystian Lewandowski
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

#define PWM_CTRL_REG		0x0
#define  PWM0_RDY			(1 << 28)
#define  SCLK_CH0_GATING		(1 << 6)
#define  PWM_CH0_ACT_STA		(1 << 5)
#define  PWM_CH0_EN			(1 << 4)
#define  PWM_CH0_PRESCAL		0xf
#define PWM_CH0_PERIOD		0x4
#define  PWM_CH0_CYCLES_SHIFT		16
#define  PWM_CH0_ACT_CYCLES_SHIFT	0
#define  PWM_CH0_CYCLES_MAX		0xffff

#define NS_PER_S		1000000000

#define HREAD4(sc, reg)							\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))

struct sxipwm_prescaler {
	uint32_t	divider;
	uint8_t		value;
};

const struct sxipwm_prescaler sxipwm_prescalers[] = {
	{ 1, 0xf },
	{ 120, 0x0 },
	{ 180, 0x1 },
	{ 240, 0x2 },
	{ 360, 0x3 },
	{ 480, 0x4 },
	{ 12000, 0x8 },
	{ 24000, 0x9 },
	{ 36000, 0xa },
	{ 48000, 0xb },
	{ 72000, 0xc },
	{ 0 }
};

struct sxipwm_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;

	uint32_t		sc_clkin;
	struct pwm_device	sc_pd;
};

int	sxipwm_match(struct device *, void *, void *);
void	sxipwm_attach(struct device *, struct device *, void *);

const struct cfattach sxipwm_ca = {
	sizeof(struct sxipwm_softc), sxipwm_match, sxipwm_attach
};

struct cfdriver sxipwm_cd = {
	NULL, "sxipwm", DV_DULL
};

int	sxipwm_get_state(void *, uint32_t *, struct pwm_state *);
int	sxipwm_set_state(void *, uint32_t *, struct pwm_state *);

int
sxipwm_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "allwinner,sun5i-a13-pwm");
}

void
sxipwm_attach(struct device *parent, struct device *self, void *aux)
{
	struct sxipwm_softc *sc = (struct sxipwm_softc *)self;
	struct fdt_attach_args *faa = aux;

	if (faa->fa_nreg < 1) {
		printf(": no registers\n");
		return;
	}

	sc->sc_clkin = clock_get_frequency_idx(faa->fa_node, 0);
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
	sc->sc_pd.pd_get_state = sxipwm_get_state;
	sc->sc_pd.pd_set_state = sxipwm_set_state;

	pwm_register(&sc->sc_pd);
}

int
sxipwm_get_state(void *cookie, uint32_t *cells, struct pwm_state *ps)
{
	struct sxipwm_softc *sc = cookie;
	uint32_t idx = cells[0];
	uint32_t ctrl, ch_period;
	uint64_t rate, cycles, act_cycles;
	int i, prescaler;

	if (idx != 0)
		return EINVAL;

	ctrl = HREAD4(sc, PWM_CTRL_REG);
	ch_period = HREAD4(sc, PWM_CH0_PERIOD);

	prescaler = -1;
	for (i = 0; sxipwm_prescalers[i].divider; i++) {
		if ((ctrl & PWM_CH0_PRESCAL) == sxipwm_prescalers[i].value) {
			prescaler = i;
			break;
		}
	}
	if (prescaler < 0)
		return EINVAL;

	rate = sc->sc_clkin / sxipwm_prescalers[prescaler].divider;
	cycles = ((ch_period >> PWM_CH0_CYCLES_SHIFT) &
	    PWM_CH0_CYCLES_MAX) + 1;
	act_cycles = (ch_period >> PWM_CH0_ACT_CYCLES_SHIFT) &
	    PWM_CH0_CYCLES_MAX;

	memset(ps, 0, sizeof(struct pwm_state));
	ps->ps_period = (NS_PER_S * cycles) / rate;
	ps->ps_pulse_width = (NS_PER_S * act_cycles) / rate;
	if ((ctrl & PWM_CH0_EN)  && (ctrl & SCLK_CH0_GATING))
		ps->ps_enabled = 1;

	return 0;
}

int
sxipwm_set_state(void *cookie, uint32_t *cells, struct pwm_state *ps)
{
	struct sxipwm_softc *sc = cookie;
	uint32_t idx = cells[0];
	uint64_t rate, cycles, act_cycles;
	uint32_t reg;
	int i, prescaler;

	if (idx != 0)
		return EINVAL;

	prescaler = -1;
	for (i = 0; sxipwm_prescalers[i].divider; i++) {
		rate = sc->sc_clkin / sxipwm_prescalers[i].divider;
		cycles = (rate * ps->ps_period) / NS_PER_S;
		if ((cycles - 1) < PWM_CH0_CYCLES_MAX) {
			prescaler = i;
			break;
		}
	}
	if (prescaler < 0)
		return EINVAL;

	rate = sc->sc_clkin / sxipwm_prescalers[prescaler].divider;
	cycles = (rate * ps->ps_period) / NS_PER_S;
	act_cycles = (rate * ps->ps_pulse_width) / NS_PER_S;
	if (cycles < 1 || act_cycles > cycles)
		return EINVAL;

	KASSERT(cycles - 1 <= PWM_CH0_CYCLES_MAX);
	KASSERT(act_cycles <= PWM_CH0_CYCLES_MAX);

	reg = HREAD4(sc, PWM_CTRL_REG);
	if (reg & PWM0_RDY)
		return EBUSY;
	if (ps->ps_enabled)
		reg |= (PWM_CH0_EN | SCLK_CH0_GATING);
	else
		reg &= ~(PWM_CH0_EN | SCLK_CH0_GATING);
	if (ps->ps_flags & PWM_POLARITY_INVERTED)
		reg &= ~PWM_CH0_ACT_STA;
	else
		reg |= PWM_CH0_ACT_STA;
	reg &= ~PWM_CH0_PRESCAL;
	reg |= sxipwm_prescalers[prescaler].value;
	HWRITE4(sc, PWM_CTRL_REG, reg);

	reg = ((cycles - 1) << PWM_CH0_CYCLES_SHIFT) |
	    (act_cycles << PWM_CH0_ACT_CYCLES_SHIFT);
	HWRITE4(sc, PWM_CH0_PERIOD, reg);

	return 0;
}
