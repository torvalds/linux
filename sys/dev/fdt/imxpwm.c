/*	$OpenBSD: imxpwm.c,v 1.2 2022/04/06 18:59:28 naddy Exp $	*/
/*
 * Copyright (c) 2018-2020 Patrick Wildt <patrick@blueri.se>
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
#include <sys/malloc.h>
#include <sys/device.h>
#include <sys/sysctl.h>

#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/fdt.h>
#include <dev/ofw/ofw_clock.h>
#include <dev/ofw/ofw_pinctrl.h>
#include <dev/ofw/ofw_misc.h>

#define PWM_CR				0x00
#define  PWM_CR_EN				(1 << 0)
#define  PWM_CR_SWR				(1 << 3)
#define  PWM_CR_CLKSRC_IPG			(1 << 16)
#define  PWM_CR_CLKSRC_IPG_HIGH			(2 << 16)
#define  PWM_CR_DBGEN				(1 << 22)
#define  PWM_CR_WAITEN				(1 << 23)
#define  PWM_CR_DOZEEN				(1 << 24)
#define  PWM_CR_PRESCALER(x)			((((x) - 1) & 0xfff) << 4)
#define  PWM_CR_PRESCALER_SHIFT			4
#define  PWM_CR_PRESCALER_MASK			0xfff
#define PWM_SR				0x04
#define  PWM_SR_FIFOAV_4WORDS			0x4
#define  PWM_SR_FIFOAV_MASK			0x7
#define PWM_SAR				0x0c
#define PWM_PR				0x10
#define  PWM_PR_MAX				0xfffe

#define NS_PER_S			1000000000

#define HREAD4(sc, reg)							\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))
#define HSET4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) | (bits))
#define HCLR4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) & ~(bits))

struct imxpwm_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;

	uint32_t		sc_dcycles;
	uint32_t		sc_clkin;
	struct pwm_device	sc_pd;
};

int	imxpwm_match(struct device *, void *, void *);
void	imxpwm_attach(struct device *, struct device *, void *);

const struct cfattach imxpwm_ca = {
	sizeof(struct imxpwm_softc), imxpwm_match, imxpwm_attach
};

struct cfdriver imxpwm_cd = {
	NULL, "imxpwm", DV_DULL
};

int	imxpwm_get_state(void *, uint32_t *, struct pwm_state *);
int	imxpwm_set_state(void *, uint32_t *, struct pwm_state *);

int
imxpwm_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "fsl,imx27-pwm");
}

void
imxpwm_attach(struct device *parent, struct device *self, void *aux)
{
	struct imxpwm_softc *sc = (struct imxpwm_softc *)self;
	struct fdt_attach_args *faa = aux;

	if (faa->fa_nreg < 1)
		return;

	sc->sc_clkin = clock_get_frequency(faa->fa_node, "per");
	if (sc->sc_clkin == 0) {
		printf(": no clock\n");
		return;
	}

	sc->sc_iot = faa->fa_iot;
	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_ioh)) {
		printf(": can't map registers");
		return;
	}

	printf("\n");

	pinctrl_byname(faa->fa_node, "default");

	clock_enable_all(faa->fa_node);
	reset_deassert_all(faa->fa_node);

	sc->sc_pd.pd_node = faa->fa_node;
	sc->sc_pd.pd_cookie = sc;
	sc->sc_pd.pd_get_state = imxpwm_get_state;
	sc->sc_pd.pd_set_state = imxpwm_set_state;

	pwm_register(&sc->sc_pd);
}

int
imxpwm_get_state(void *cookie, uint32_t *cells, struct pwm_state *ps)
{
	struct imxpwm_softc *sc = cookie;
	uint64_t dcycles, pcycles, prescale, pwmclk;
	int enabled = 0;

	prescale = ((HREAD4(sc, PWM_CR) >> PWM_CR_PRESCALER_SHIFT) &
	    PWM_CR_PRESCALER_MASK) + 1;
	pwmclk = (sc->sc_clkin + (prescale / 2)) / prescale;
	if (pwmclk == 0)
		return EINVAL;

	if (HREAD4(sc, PWM_CR) & PWM_CR_EN)
		enabled = 1;

	pcycles = HREAD4(sc, PWM_PR);
	if (pcycles >= PWM_PR_MAX)
		pcycles = PWM_PR_MAX;
	pcycles = (pcycles + 2) * NS_PER_S;
	pcycles = (pcycles + (pwmclk / 2)) / pwmclk;

	dcycles = sc->sc_dcycles;
	if (enabled)
		dcycles = HREAD4(sc, PWM_SAR);
	dcycles = dcycles * NS_PER_S;
	dcycles = (dcycles + (pwmclk / 2)) / pwmclk;

	memset(ps, 0, sizeof(struct pwm_state));
	ps->ps_period = pcycles;
	ps->ps_pulse_width = dcycles;
	ps->ps_enabled = enabled;
	return 0;
}

int
imxpwm_set_state(void *cookie, uint32_t *cells, struct pwm_state *ps)
{
	struct imxpwm_softc *sc = cookie;
	uint64_t dcycles, pcycles, prescale;
	int i;

	if (ps->ps_enabled) {
		pcycles = sc->sc_clkin;
		pcycles = (pcycles * ps->ps_period) / NS_PER_S;
		prescale = pcycles / 0x10000 + 1;

		if (ps->ps_period == 0 || prescale == 0)
			return EINVAL;

		pcycles = pcycles / prescale;
		dcycles = (pcycles * ps->ps_pulse_width) / ps->ps_period;

		if (pcycles > 2)
			pcycles -= 2;
		else
			pcycles = 0;
	}

	/* disable and flush fifo */
	HCLR4(sc, PWM_CR, PWM_CR_EN);
	HWRITE4(sc, PWM_CR, PWM_CR_SWR);
	for (i = 0; i < 5; i++) {
		delay(1000);
		if ((HREAD4(sc, PWM_CR) & PWM_CR_SWR) == 0)
			break;
	}
	if (i == 5) {
		printf("%s: reset timeout\n", sc->sc_dev.dv_xname);
		return ETIMEDOUT;
	}

	if (ps->ps_enabled) {
		HWRITE4(sc, PWM_SAR, dcycles);
		HWRITE4(sc, PWM_PR, pcycles);

		sc->sc_dcycles = dcycles;

		HWRITE4(sc, PWM_CR, PWM_CR_PRESCALER(prescale) |
		    PWM_CR_DOZEEN | PWM_CR_WAITEN |
		    PWM_CR_DBGEN | PWM_CR_CLKSRC_IPG_HIGH |
		    PWM_CR_EN);
	}

	return 0;
}
