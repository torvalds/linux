/*	$OpenBSD: amlpwm.c,v 1.3 2021/10/24 17:52:26 mpi Exp $	*/
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

#include <machine/intr.h>
#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_clock.h>
#include <dev/ofw/ofw_misc.h>
#include <dev/ofw/ofw_pinctrl.h>
#include <dev/ofw/fdt.h>

#define PWM_PWM_A		0x00
#define PWM_PWM_B		0x01
#define  PWM_PWM_HIGH(x)	((x) >> 16)
#define  PWM_PWM_HIGH_SHIFT	16
#define  PWM_PWM_LOW(x)		((x) & 0xffff)
#define  PWM_PWM_LOW_SHIFT	0
#define PWM_MISC_REG_AB		0x02
#define  PWM_B_CLK_EN		(1 << 23)
#define  PWM_B_CLK_DIV_MASK	(0x7f << 16)
#define  PWM_B_CLK_DIV_SHIFT	16
#define  PWM_B_CLK_DIV(x)	((((x) >> 16) & 0x7f) + 1)
#define  PWM_A_CLK_EN		(1 << 15)
#define  PWM_A_CLK_DIV_MASK	(0x7f << 8)
#define  PWM_A_CLK_DIV_SHIFT	8
#define  PWM_A_CLK_DIV(x)	((((x) >> 8) & 0x7f) + 1)
#define  PWM_B_EN		(1 << 1)
#define  PWM_A_EN		(1 << 0)

#define HREAD4(sc, reg)							\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg) << 2))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg) << 2, (val))
#define HSET4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) | (bits))
#define HCLR4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) & ~(bits))

struct amlpwm_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;

	uint32_t		sc_clkin[2];

	struct pwm_device	sc_pd;
};

int amlpwm_match(struct device *, void *, void *);
void amlpwm_attach(struct device *, struct device *, void *);

const struct cfattach	amlpwm_ca = {
	sizeof (struct amlpwm_softc), amlpwm_match, amlpwm_attach
};

struct cfdriver amlpwm_cd = {
	NULL, "amlpwm", DV_DULL
};

int	amlpwm_get_state(void *, uint32_t *, struct pwm_state *);
int	amlpwm_set_state(void *, uint32_t *, struct pwm_state *);

int
amlpwm_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;
	int node = faa->fa_node;

	return (OF_is_compatible(node, "amlogic,meson-g12a-ao-pwm-cd") ||
	    OF_is_compatible(node, "amlogic,meson-g12a-ee-pwm"));
}

void
amlpwm_attach(struct device *parent, struct device *self, void *aux)
{
	struct amlpwm_softc *sc = (struct amlpwm_softc *)self;
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

	sc->sc_clkin[0] = clock_get_frequency(faa->fa_node, "clkin0");
	sc->sc_clkin[1] = clock_get_frequency(faa->fa_node, "clkin1");

	printf("\n");

	pinctrl_byname(faa->fa_node, "default");

	sc->sc_pd.pd_node = faa->fa_node;
	sc->sc_pd.pd_cookie = sc;
	sc->sc_pd.pd_get_state = amlpwm_get_state;
	sc->sc_pd.pd_set_state = amlpwm_set_state;
	pwm_register(&sc->sc_pd);
}

static inline uint32_t
cycles_to_ns(uint64_t clk_freq, uint32_t clk_div, uint32_t cycles)
{
	return cycles * clk_div * 1000000000ULL / clk_freq;
}

static inline uint32_t
ns_to_cycles(uint64_t clk_freq, uint32_t clk_div, uint32_t ns)
{
	return ns * clk_freq / (clk_div * 1000000000ULL);
}

int
amlpwm_get_state(void *cookie, uint32_t *cells, struct pwm_state *ps)
{
	struct amlpwm_softc *sc = cookie;
	uint32_t idx = cells[0];
	uint32_t pwm, misc;
	uint32_t total, high;
	uint32_t clk_div;
	int enabled = 0;

	if (idx > 1 || sc->sc_clkin[idx] == 0)
		return EINVAL;

	pwm = HREAD4(sc, idx == 0 ? PWM_PWM_A : PWM_PWM_B);
	misc = HREAD4(sc, PWM_MISC_REG_AB);

	if (idx == 0) {
		if ((misc & PWM_A_CLK_EN) && (misc & PWM_A_EN))
			enabled = 1;
		clk_div = PWM_A_CLK_DIV(misc);
	} else {
		if ((misc & PWM_B_CLK_EN) && (misc & PWM_B_EN))
			enabled = 1;
		clk_div = PWM_B_CLK_DIV(misc);
	}

	total = PWM_PWM_LOW(pwm) + PWM_PWM_HIGH(pwm);
	high = PWM_PWM_HIGH(pwm);

	memset(ps, 0, sizeof(struct pwm_state));
	ps->ps_period = cycles_to_ns(sc->sc_clkin[idx], clk_div, total);
	ps->ps_pulse_width = cycles_to_ns(sc->sc_clkin[idx], clk_div, high);
	ps->ps_enabled = enabled;

	return 0;
}

int
amlpwm_set_state(void *cookie, uint32_t *cells, struct pwm_state *ps)
{
	struct amlpwm_softc *sc = cookie;
	uint32_t idx = cells[0];
	uint32_t pwm, misc;
	uint32_t total, high, low;
	uint32_t clk_div = 1;

	if (idx > 1 || sc->sc_clkin[idx] == 0)
		return EINVAL;

	/* Hardware doesn't support polarity inversion. */
	if (ps->ps_flags & PWM_POLARITY_INVERTED)
		return EINVAL;

	if (!ps->ps_enabled) {
		HCLR4(sc, PWM_MISC_REG_AB, (idx == 0) ? PWM_A_EN : PWM_B_EN);
		return 0;
	}

	total = ns_to_cycles(sc->sc_clkin[idx], clk_div, ps->ps_period);
	while ((total / clk_div) > 0xffff)
		clk_div++;
	if (clk_div > 128)
		return EINVAL;

	total = ns_to_cycles(sc->sc_clkin[idx], clk_div, ps->ps_period);
	high = ns_to_cycles(sc->sc_clkin[idx], clk_div, ps->ps_pulse_width);
	low = total - high;

	pwm = (high << PWM_PWM_HIGH_SHIFT) | (low << PWM_PWM_LOW_SHIFT);
	misc = HREAD4(sc, PWM_MISC_REG_AB);

	if (idx == 0) {
		misc &= ~PWM_A_CLK_DIV_MASK;
		misc |= (clk_div - 1) << PWM_A_CLK_DIV_SHIFT;
		misc |= PWM_A_CLK_EN;
	} else {
		misc &= ~PWM_B_CLK_DIV_MASK;
		misc |= (clk_div - 1) << PWM_B_CLK_DIV_SHIFT;
		misc |= PWM_B_CLK_EN;
	}

	HWRITE4(sc, PWM_MISC_REG_AB, misc);
	HWRITE4(sc, (idx == 0) ? PWM_PWM_A : PWM_PWM_B, pwm);
	HSET4(sc, PWM_MISC_REG_AB, (idx == 0) ? PWM_A_EN : PWM_B_EN);

	return 0;
}
