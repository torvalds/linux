/*	$OpenBSD: qcpwm.c,v 1.3 2025/06/12 12:25:29 kettenis Exp $	*/
/*
 * Copyright (c) 2022 Patrick Wildt <patrick@blueri.se>
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

#include <dev/fdt/spmivar.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/fdt.h>
#include <dev/ofw/ofw_clock.h>
#include <dev/ofw/ofw_pinctrl.h>
#include <dev/ofw/ofw_misc.h>

#define PWM_CHAN_OFF(x)			(0x100 * (x))
#define PWM_SUBTYPE			0x05
#define  PWM_SUBTYPE_LPG			0x2
#define  PWM_SUBTYPE_PWM			0xb
#define  PWM_SUBTYPE_HI_RES_PWM			0xc
#define  PWM_SUBTYPE_LPG_LITE			0x11
#define PWM_SIZE_CLK			0x41
#define  PWM_SIZE_CLK_SELECT_SHIFT		0
#define  PWM_SIZE_CLK_SELECT_MASK		0x3
#define  PWM_SIZE_CLK_SIZE_SHIFT		2
#define  PWM_SIZE_CLK_SIZE_MASK			0x1
#define  PWM_SIZE_CLK_HI_RES_SELECT_SHIFT	0
#define  PWM_SIZE_CLK_HI_RES_SELECT_MASK	0x7
#define  PWM_SIZE_CLK_HI_RES_SIZE_SHIFT		4
#define  PWM_SIZE_CLK_HI_RES_SIZE_MASK		0x7
#define  PWM_SIZE_CLK_LPG_9BIT			(3 << 4)
#define  PWM_SIZE_CLK_LPG_LITE_9BIT		(1 << 4)
#define PWM_PREDIV_CLK			0x42
#define  PWM_PREDIV_CLK_EXP_SHIFT		0
#define  PWM_PREDIV_CLK_EXP_MASK		0x7
#define  PWM_PREDIV_CLK_PREDIV_SHIFT		5
#define  PWM_PREDIV_CLK_PREDIV_MASK		0x3
#define PWM_TYPE_CONFIG			0x43
#define  PWM_TYPE_CONFIG_GLITCH_REMOVAL		(1 << 5)
#define PWM_VALUE			0x44
#define PWM_ENABLE_CONTROL		0x46
#define  PWM_ENABLE_CONTROL_OUTPUT		(1U << 7)
#define  PWM_ENABLE_CONTROL_BUFFER_TRISTATE	(1 << 5)
#define  PWM_ENABLE_CONTROL_SRC_PWM		(1 << 2)
#define  PWM_ENABLE_CONTROL_RAMP_GEN		(1 << 1)
#define PWM_SYNC			0x47
#define  PWM_SYNC_PWM				(1 << 0)

#define NS_PER_S			1000000000LLU

uint64_t qcpwm_clk_rates[8] = { 0, 1024, 32768, 19200000, 76800000 };
uint64_t qcpwm_pre_divs[4] = { 1, 3, 5, 6 };
uint64_t qcpwm_res[2] = { 64, 512 };
uint64_t qcpwm_hi_res[8] = { 256, 512, 1024, 2048, 4096, 8192, 16384, 32768 };

struct qcpwm_softc {
	struct device		sc_dev;
	int			sc_node;

	spmi_tag_t		sc_tag;
	int8_t			sc_sid;
	uint16_t		sc_addr;

	int			sc_nchannel;

	struct pwm_device	sc_pd;
};

int	qcpwm_match(struct device *, void *, void *);
void	qcpwm_attach(struct device *, struct device *, void *);

const struct cfattach qcpwm_ca = {
	sizeof(struct qcpwm_softc), qcpwm_match, qcpwm_attach
};

struct cfdriver qcpwm_cd = {
	NULL, "qcpwm", DV_DULL
};

uint8_t	qcpwm_read(struct qcpwm_softc *, uint16_t);
void	qcpwm_write(struct qcpwm_softc *, uint16_t, uint8_t);

int	qcpwm_get_state(void *, uint32_t *, struct pwm_state *);
int	qcpwm_set_state(void *, uint32_t *, struct pwm_state *);

int
qcpwm_match(struct device *parent, void *match, void *aux)
{
	struct spmi_attach_args *saa = aux;

	return (OF_is_compatible(saa->sa_node, "qcom,pm8350c-pwm") ||
	    OF_is_compatible(saa->sa_node, "qcom,pmk8550-pwm"));
}

void
qcpwm_attach(struct device *parent, struct device *self, void *aux)
{
	struct qcpwm_softc *sc = (struct qcpwm_softc *)self;
	struct spmi_attach_args *saa = aux;

	sc->sc_addr = OF_getpropint(saa->sa_node, "reg", 0xe800);
	sc->sc_node = saa->sa_node;
	sc->sc_tag = saa->sa_tag;
	sc->sc_sid = saa->sa_sid;

	if (OF_is_compatible(saa->sa_node, "qcom,pm8350c-pwm"))
		sc->sc_nchannel = 4;
	else
		sc->sc_nchannel = 2;

	printf("\n");

	pinctrl_byname(saa->sa_node, "default");

	clock_enable_all(saa->sa_node);
	reset_deassert_all(saa->sa_node);

	sc->sc_pd.pd_node = saa->sa_node;
	sc->sc_pd.pd_cookie = sc;
	sc->sc_pd.pd_get_state = qcpwm_get_state;
	sc->sc_pd.pd_set_state = qcpwm_set_state;

	pwm_register(&sc->sc_pd);
}

int
qcpwm_get_state(void *cookie, uint32_t *cells, struct pwm_state *ps)
{
	struct qcpwm_softc *sc = cookie;
	int chan = cells[0];
	uint64_t refclk, prediv, exp, res;
	uint64_t pcycles, dcycles;
	uint16_t pwmval;
	uint8_t reg;

	if (chan >= sc->sc_nchannel)
		return ENXIO;

	memset(ps, 0, sizeof(struct pwm_state));

	ps->ps_enabled = !!(qcpwm_read(sc, PWM_CHAN_OFF(chan) +
	    PWM_ENABLE_CONTROL) & PWM_ENABLE_CONTROL_OUTPUT);

	reg = qcpwm_read(sc, PWM_CHAN_OFF(chan) + PWM_SIZE_CLK);
	switch (qcpwm_read(sc, PWM_CHAN_OFF(chan) + PWM_SUBTYPE)) {
	case PWM_SUBTYPE_HI_RES_PWM:
		refclk = (reg >> PWM_SIZE_CLK_HI_RES_SELECT_SHIFT) &
		    PWM_SIZE_CLK_HI_RES_SELECT_MASK;
		refclk = qcpwm_clk_rates[refclk];
		res = (reg >> PWM_SIZE_CLK_HI_RES_SIZE_SHIFT) &
		    PWM_SIZE_CLK_HI_RES_SIZE_MASK;
		res = qcpwm_hi_res[res];
		break;
	default:
		refclk = (reg >> PWM_SIZE_CLK_SELECT_SHIFT) &
		    PWM_SIZE_CLK_SELECT_MASK;
		refclk = qcpwm_clk_rates[refclk];
		res = (reg >> PWM_SIZE_CLK_SIZE_SHIFT) &
		    PWM_SIZE_CLK_SIZE_MASK;
		res = qcpwm_res[res];
		break;
	}

	if (refclk == 0)
		return 0;

	reg = qcpwm_read(sc, PWM_CHAN_OFF(chan) + PWM_PREDIV_CLK);
	exp = (reg >> PWM_PREDIV_CLK_EXP_SHIFT) &
	    PWM_PREDIV_CLK_EXP_MASK;
	prediv = (reg >> PWM_PREDIV_CLK_PREDIV_SHIFT) &
	    PWM_PREDIV_CLK_PREDIV_MASK;
	prediv = qcpwm_pre_divs[prediv];

	spmi_cmd_read(sc->sc_tag, sc->sc_sid, SPMI_CMD_EXT_READL,
	    sc->sc_addr + PWM_CHAN_OFF(chan) + PWM_VALUE,
	    &pwmval, sizeof(pwmval));

	pcycles = (NS_PER_S * res * prediv * (1 << exp));
	pcycles = (pcycles + refclk - 1) / refclk;
	dcycles = (NS_PER_S * pwmval * prediv * (1 << exp));
	dcycles = (dcycles + refclk - 1) / refclk;

	ps->ps_period = pcycles;
	ps->ps_pulse_width = dcycles;
	return 0;
}

int
qcpwm_set_state(void *cookie, uint32_t *cells, struct pwm_state *ps)
{
	struct qcpwm_softc *sc = cookie;
	uint64_t dcycles, pcycles, acycles, diff = UINT64_MAX;
	uint64_t *res;
	int chan = cells[0];
	int clksel, divsel, ressel, exp;
	int nclk, nres;
	uint16_t pwmval;
	uint8_t reg;
	int i, j, k, m;

	if (chan >= sc->sc_nchannel)
		return ENXIO;

	pcycles = ps->ps_period;
	dcycles = ps->ps_pulse_width;

	reg = qcpwm_read(sc, PWM_CHAN_OFF(chan) + PWM_SIZE_CLK);
	switch (qcpwm_read(sc, PWM_CHAN_OFF(chan) + PWM_SUBTYPE)) {
	case PWM_SUBTYPE_HI_RES_PWM:
		res = qcpwm_hi_res;
		nres = nitems(qcpwm_hi_res);
		nclk = 5;
		break;
	default:
		res = qcpwm_res;
		nres = nitems(qcpwm_res);
		nclk = 4;
		break;
	}

	for (i = 0; i < nres; i++) {
		for (j = 1; j < nclk; j++) {
			for (k = 0; k < nitems(qcpwm_pre_divs); k++) {
				if (pcycles * qcpwm_clk_rates[j] <
				    NS_PER_S * qcpwm_pre_divs[k] * res[i])
					continue;

				m = flsl((pcycles * qcpwm_clk_rates[j]) /
				    (NS_PER_S * qcpwm_pre_divs[k] * res[i]))
				    - 1;
				if (m > PWM_PREDIV_CLK_EXP_MASK)
					m = PWM_PREDIV_CLK_EXP_MASK;

				acycles = (NS_PER_S * qcpwm_pre_divs[k] *
				    res[i] * (1 << m)) / qcpwm_clk_rates[j];
				if (pcycles - acycles < diff) {
					diff = pcycles - acycles;
					ps->ps_period = acycles;
					ressel = i;
					clksel = j;
					divsel = k;
					exp = m;
				}
			}
		}
	}

	ps->ps_pulse_width = (dcycles * ps->ps_period) / pcycles;
	if (ps->ps_pulse_width > ps->ps_period)
		ps->ps_pulse_width = ps->ps_period;

	pcycles = ps->ps_period;
	dcycles = ps->ps_pulse_width;
	pwmval = ((uint64_t)ps->ps_pulse_width * qcpwm_clk_rates[clksel]) /
	    (NS_PER_S * qcpwm_pre_divs[divsel] * (1 << exp));

	reg = qcpwm_read(sc, PWM_CHAN_OFF(chan) + PWM_TYPE_CONFIG);
	reg |= PWM_TYPE_CONFIG_GLITCH_REMOVAL;
	qcpwm_write(sc, PWM_CHAN_OFF(chan) + PWM_TYPE_CONFIG, reg);

	reg = clksel << PWM_SIZE_CLK_SELECT_SHIFT;
	switch (qcpwm_read(sc, PWM_CHAN_OFF(chan) + PWM_SUBTYPE)) {
	case PWM_SUBTYPE_LPG:
		reg |= PWM_SIZE_CLK_LPG_9BIT;
		break;
	case PWM_SUBTYPE_PWM:
		reg |= (ressel << PWM_SIZE_CLK_SIZE_SHIFT);
		break;
	case PWM_SUBTYPE_HI_RES_PWM:
		reg |= (ressel << PWM_SIZE_CLK_HI_RES_SIZE_SHIFT);
		break;
	case PWM_SUBTYPE_LPG_LITE:
	default:
		reg |= PWM_SIZE_CLK_LPG_LITE_9BIT;
		break;
	}
	qcpwm_write(sc, PWM_CHAN_OFF(chan) + PWM_SIZE_CLK, reg);
	qcpwm_write(sc, PWM_CHAN_OFF(chan) + PWM_PREDIV_CLK,
	    divsel << PWM_PREDIV_CLK_PREDIV_SHIFT |
	    exp << PWM_PREDIV_CLK_EXP_SHIFT);

	if (ps->ps_enabled) {
		spmi_cmd_write(sc->sc_tag, sc->sc_sid, SPMI_CMD_EXT_WRITEL,
		    sc->sc_addr + PWM_CHAN_OFF(chan) + PWM_VALUE,
		    &pwmval, sizeof(pwmval));
	}

	reg = PWM_ENABLE_CONTROL_BUFFER_TRISTATE;
	if (ps->ps_enabled)
		reg |= PWM_ENABLE_CONTROL_OUTPUT;
	reg |= PWM_ENABLE_CONTROL_SRC_PWM;
	qcpwm_write(sc, PWM_CHAN_OFF(chan) + PWM_ENABLE_CONTROL, reg);

	/* HW quirk: rewrite after enable */
	if (ps->ps_enabled) {
		spmi_cmd_write(sc->sc_tag, sc->sc_sid, SPMI_CMD_EXT_WRITEL,
		    sc->sc_addr + PWM_CHAN_OFF(chan) + PWM_VALUE,
		    &pwmval, sizeof(pwmval));
	}

	qcpwm_write(sc, PWM_CHAN_OFF(chan) + PWM_SYNC, PWM_SYNC_PWM);

	reg = qcpwm_read(sc, PWM_CHAN_OFF(chan) + PWM_TYPE_CONFIG);
	reg &= ~PWM_TYPE_CONFIG_GLITCH_REMOVAL;
	qcpwm_write(sc, PWM_CHAN_OFF(chan) + PWM_TYPE_CONFIG, reg);

	return 0;
}

uint8_t
qcpwm_read(struct qcpwm_softc *sc, uint16_t reg)
{
	uint8_t val = 0;
	int error;

	error = spmi_cmd_read(sc->sc_tag, sc->sc_sid, SPMI_CMD_EXT_READL,
	    sc->sc_addr + reg, &val, sizeof(val));
	if (error)
		printf("%s: error reading\n", sc->sc_dev.dv_xname);

	return val;
}

void
qcpwm_write(struct qcpwm_softc *sc, uint16_t reg, uint8_t val)
{
	int error;

	error = spmi_cmd_write(sc->sc_tag, sc->sc_sid, SPMI_CMD_EXT_WRITEL,
	    sc->sc_addr + reg, &val, sizeof(val));
	if (error)
		printf("%s: error writing\n", sc->sc_dev.dv_xname);
}
