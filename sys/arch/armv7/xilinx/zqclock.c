/*	$OpenBSD: zqclock.c,v 1.1 2021/04/30 13:25:24 visa Exp $	*/

/*
 * Copyright (c) 2021 Visa Hankala
 *
 * Permission to use, copy, modify, and/or distribute this software for any
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
 * Driver for Xilinx Zynq-7000 clock controller.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/mutex.h>

#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/ofw/fdt.h>
#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_clock.h>
#include <dev/ofw/ofw_misc.h>

#include <armv7/xilinx/slcreg.h>

#define CLK_ARM_PLL			0
#define CLK_DDR_PLL			1
#define CLK_IO_PLL			2
#define CLK_CPU_6OR4X			3
#define CLK_CPU_3OR2X			4
#define CLK_CPU_2X			5
#define CLK_CPU_1X			6
#define CLK_DDR_2X			7
#define CLK_DDR_3X			8
#define CLK_DCI				9
#define CLK_LQSPI			10
#define CLK_SMC				11
#define CLK_PCAP			12
#define CLK_GEM0			13
#define CLK_GEM1			14
#define CLK_FCLK0			15
#define CLK_FCLK1			16
#define CLK_FCLK2			17
#define CLK_FCLK3			18
#define CLK_CAN0			19
#define CLK_CAN1			20
#define CLK_SDIO0			21
#define CLK_SDIO1			22
#define CLK_UART0			23
#define CLK_UART1			24
#define CLK_SPI0			25
#define CLK_SPI1			26
#define CLK_DMA				27

struct zqclock_softc {
	struct device		sc_dev;
	struct regmap		*sc_rm;

	struct clock_device	sc_cd;
	uint32_t		sc_psclk_freq;		/* in Hz */
};

int	zqclock_match(struct device *, void *, void *);
void	zqclock_attach(struct device *, struct device *, void *);

void	zqclock_enable(void *, uint32_t *, int);
uint32_t zqclock_get_frequency(void *, uint32_t *);
int	zqclock_set_frequency(void *, uint32_t *, uint32_t);

const struct cfattach zqclock_ca = {
	sizeof(struct zqclock_softc), zqclock_match, zqclock_attach
};

struct cfdriver zqclock_cd = {
	NULL, "zqclock", DV_DULL
};

struct zqclock_clock {
	uint16_t		clk_ctl_reg;
	uint8_t			clk_has_div1;
	uint8_t			clk_index;
};

const struct zqclock_clock zqclock_clocks[] = {
	[CLK_GEM0]		= { SLCR_GEM0_CLK_CTRL, 1, 0 },
	[CLK_GEM1]		= { SLCR_GEM1_CLK_CTRL, 1, 0 },
	[CLK_SDIO0]		= { SLCR_SDIO_CLK_CTRL, 0, 0 },
	[CLK_SDIO1]		= { SLCR_SDIO_CLK_CTRL, 0, 1 },
	[CLK_UART0]		= { SLCR_UART_CLK_CTRL, 0, 0 },
	[CLK_UART1]		= { SLCR_UART_CLK_CTRL, 0, 1 },
};

int
zqclock_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "xlnx,ps7-clkc");
}

void
zqclock_attach(struct device *parent, struct device *self, void *aux)
{
	struct fdt_attach_args *faa = aux;
	struct zqclock_softc *sc = (struct zqclock_softc *)self;

	sc->sc_rm = regmap_bynode(OF_parent(faa->fa_node));
	if (sc->sc_rm == NULL) {
		printf(": can't get regmap\n");
		return;
	}

	sc->sc_psclk_freq = OF_getpropint(faa->fa_node, "ps-clk-frequency",
	    33333333);

	printf(": %u MHz PS clock\n", (sc->sc_psclk_freq + 500000) / 1000000);

	sc->sc_cd.cd_node = faa->fa_node;
	sc->sc_cd.cd_cookie = sc;
	sc->sc_cd.cd_enable = zqclock_enable;
	sc->sc_cd.cd_get_frequency = zqclock_get_frequency;
	sc->sc_cd.cd_set_frequency = zqclock_set_frequency;
	clock_register(&sc->sc_cd);
}

const struct zqclock_clock *
zqclock_get_clock(uint32_t idx)
{
	const struct zqclock_clock *clock;

	if (idx >= nitems(zqclock_clocks))
		return NULL;

	clock = &zqclock_clocks[idx];
	if (clock->clk_ctl_reg == 0)
		return NULL;

	return clock;
}

uint32_t
zqclock_get_pll_frequency(struct zqclock_softc *sc, uint32_t clk_ctrl)
{
	uint32_t reg, val;

	switch (clk_ctrl & SLCR_CLK_CTRL_SRCSEL_MASK) {
	case SLCR_CLK_CTRL_SRCSEL_ARM:
		reg = SLCR_ARM_PLL_CTRL;
		break;
	case SLCR_CLK_CTRL_SRCSEL_DDR:
		reg = SLCR_DDR_PLL_CTRL;
		break;
	default:
		reg = SLCR_IO_PLL_CTRL;
		break;
	}

	val = zynq_slcr_read(sc->sc_rm, reg);
	return sc->sc_psclk_freq * ((val >> SLCR_PLL_CTRL_FDIV_SHIFT) &
	    SLCR_PLL_CTRL_FDIV_MASK);
}

uint32_t
zqclock_get_frequency(void *cookie, uint32_t *cells)
{
	const struct zqclock_clock *clock;
	struct zqclock_softc *sc = cookie;
	uint32_t idx = cells[0];
	uint32_t ctl, div, freq;

	clock = zqclock_get_clock(idx);
	if (clock == NULL)
		return 0;

	mtx_enter(&zynq_slcr_lock);

	ctl = zynq_slcr_read(sc->sc_rm, clock->clk_ctl_reg);

	div = SLCR_CLK_CTRL_DIVISOR(ctl);
	if (clock->clk_has_div1)
		div *= SLCR_CLK_CTRL_DIVISOR1(ctl);

	freq = zqclock_get_pll_frequency(sc, ctl);
	freq = (freq + div / 2) / div;

	mtx_leave(&zynq_slcr_lock);

	return freq;
}

int
zqclock_set_frequency(void *cookie, uint32_t *cells, uint32_t freq)
{
	static const uint32_t srcsels[] = {
		SLCR_CLK_CTRL_SRCSEL_IO,
		SLCR_CLK_CTRL_SRCSEL_ARM,
		SLCR_CLK_CTRL_SRCSEL_DDR,
	};
	const struct zqclock_clock *clock;
	struct zqclock_softc *sc = cookie;
	uint32_t idx = cells[0];
	uint32_t best_delta = ~0U;
	uint32_t best_div1 = 0;
	uint32_t best_si = 0;
	uint32_t best_pllf = 0;
	uint32_t ctl, div, div1, maxdiv1, si;
	int error = 0;

	clock = zqclock_get_clock(idx);
	if (clock == NULL)
		return EINVAL;

	if (freq == 0)
		return EINVAL;

	mtx_enter(&zynq_slcr_lock);

	maxdiv1 = 1;
	if (clock->clk_has_div1)
		maxdiv1 = SLCR_DIV_MASK;

	/* Find PLL and divisors that give best frequency. */
	for (si = 0; si < nitems(srcsels); si++) {
		uint32_t delta, f, pllf;

		pllf = zqclock_get_pll_frequency(sc, srcsels[si]);
		if (freq > pllf)
			continue;

		for (div1 = 1; div1 <= maxdiv1; div1++) {
			div = (pllf + (freq * div1 / 2)) / (freq * div1);
			if (div > SLCR_DIV_MASK)
				continue;
			if (div == 0)
				break;

			f = (pllf + (div * div1 / 2)) / (div * div1);
			delta = abs(f - freq);
			if (best_div1 == 0 || delta < best_delta) {
				best_delta = delta;
				best_div1 = div1;
				best_pllf = pllf;
				best_si = si;

				if (delta == 0)
					goto found;
			}
		}
	}

	if (best_div1 == 0) {
		error = EINVAL;
		goto out;
	}

found:
	div1 = best_div1;
	div = (best_pllf + (freq * div1 / 2)) / (freq * div1);

	KASSERT(div > 0 && div <= SLCR_DIV_MASK);
	KASSERT(div1 > 0 && div1 <= SLCR_DIV_MASK);

	ctl = zynq_slcr_read(sc->sc_rm, clock->clk_ctl_reg);

	ctl &= ~SLCR_CLK_CTRL_SRCSEL_MASK;
	ctl |= srcsels[best_si];
	ctl &= ~(SLCR_DIV_MASK << SLCR_CLK_CTRL_DIVISOR_SHIFT);
	ctl |= (div & SLCR_DIV_MASK) << SLCR_CLK_CTRL_DIVISOR_SHIFT;
	if (clock->clk_has_div1) {
		ctl &= ~(SLCR_DIV_MASK << SLCR_CLK_CTRL_DIVISOR1_SHIFT);
		ctl |= (div1 & SLCR_DIV_MASK) << SLCR_CLK_CTRL_DIVISOR1_SHIFT;
	}

	zynq_slcr_write(sc->sc_rm, clock->clk_ctl_reg, ctl);

out:
	mtx_leave(&zynq_slcr_lock);

	return error;
}

void
zqclock_enable(void *cookie, uint32_t *cells, int on)
{
	const struct zqclock_clock *clock;
	struct zqclock_softc *sc = cookie;
	uint32_t idx = cells[0];
	uint32_t ctl;

	clock = zqclock_get_clock(idx);
	if (clock == NULL)
		return;

	mtx_enter(&zynq_slcr_lock);

	ctl = zynq_slcr_read(sc->sc_rm, clock->clk_ctl_reg);
	if (on)
		ctl |= SLCR_CLK_CTRL_CLKACT(clock->clk_index);
	else
		ctl &= ~SLCR_CLK_CTRL_CLKACT(clock->clk_index);
	zynq_slcr_write(sc->sc_rm, clock->clk_ctl_reg, ctl);

	mtx_leave(&zynq_slcr_lock);
}
