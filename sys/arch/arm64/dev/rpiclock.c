/* $OpenBSD: rpiclock.c,v 1.1 2025/09/17 09:23:43 kettenis Exp $ */

/*
 * Copyright (c) 2025 Marcus Glocker <mglocker@openbsd.org>
 * Copyright (c) 2025 Mark Kettenis <kettenis@openbsd.org>
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

#include <machine/fdt.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_clock.h>
#include <dev/ofw/fdt.h>

/* Clock numbers. */
#define RP1_PLL_SYS_CORE	0
#define RP1_PLL_AUDIO_CORE	1
#define RP1_PLL_SYS		3
#define RP1_PLL_AUDIO		4
#define RP1_PLL_SYS_PRI_PH	6
#define RP1_PLL_AUDIO_PRI_PH	8
#define RP1_PLL_SYS_SEC		9
#define RP1_PLL_AUDIO_SEC	10
#define RP1_CLK_SYS		12
#define RP1_CLK_SLOW_SYS	13
#define RP1_CLK_ETH		16
#define RP1_CLK_PWM0		17
#define RP1_CLK_PWM1		18
#define RP1_CLK_ETH_TSU		29
#define RP1_CLK_SDIO_TIMER	31
#define RP1_CLK_SDIO_ALT_SRC	32
#define RP1_CLK_GP0		33
#define RP1_CLK_GP1		34
#define RP1_CLK_GP2		35
#define RP1_CLK_GP3		36
#define RP1_CLK_GP4		37
#define RP1_CLK_GP5		38
#define RP1_CLK_XOSC		1023

/* Bus helper macros. */
#define HREAD4(sc, reg)							\
	    (bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
	    bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))
#define HSET4(sc, reg, bits)						\
	    HWRITE4((sc), (reg), HREAD4((sc), (reg)) | (bits))
#define HCLR4(sc, reg, bits)						\
	    HWRITE4((sc), (reg), HREAD4((sc), (reg)) & ~(bits))

/* Registers. */
#define GPCLK_OE_CTRL			0x0000

#define CLK_SYS_CTRL			0x0014
#define CLK_SYS_DIV_INT			0x0018
#define CLK_SYS_SEL			0x0020
#define CLK_SLOW_SYS_CTRL		0x0024
#define CLK_SLOW_SYS_DIV_INT		0x0028
#define CLK_SLOW_SYS_SEL		0x0030
#define CLK_ETH_CTRL			0x0064
#define CLK_ETH_DIV_INT			0x0068
#define CLK_ETH_SEL			0x0070
#define CLK_PWM0_CTRL			0x0074
#define CLK_PWM0_DIV_INT		0x0078
#define CLK_PWM0_DIV_FRAC		0x007c
#define CLK_PWM0_SEL			0x0080
#define CLK_PWM1_CTRL			0x0084
#define CLK_PWM1_DIV_INT		0x0088
#define CLK_PWM1_DIV_FRAC		0x008c
#define CLK_PWM1_SEL			0x0090
#define CLK_ETH_TSU_CTRL		0x0134
#define CLK_ETH_TSU_DIV_INT		0x0138
#define CLK_ETH_TSU_SEL			0x0140
#define CLK_SDIO_TIMER_CTRL		0x0154
#define CLK_SDIO_TIMER_DIV_INT		0x0158
#define CLK_SDIO_TIMER_SEL		0x0160
#define CLK_SDIO_ALT_SRC_CTRL		0x0164
#define CLK_SDIO_ALT_SRC_DIV_INT	0x0168
#define CLK_SDIO_ALT_SRC_SEL		0x0170

#define CLK_CTRL_ENABLE			(1 << 11)
#define CLK_CTRL_AUXSRC_MASK		(0x1f << 5)
#define CLK_CTRL_AUXSRC_SHIFT		5
#define CLK_CTRL_SRC_MASK		(0x1f << 0)
#define CLK_CTRL_SRC_SHIFT		0
#define CLK_CTRL_SRC_AUX		(0x01 << 0)

#define PLL_SYS_CS			0x8000
#define PLL_SYS_PWR			0x8004
#define PLL_SYS_FBDIV_INT		0x8008
#define PLL_SYS_FBDIV_FRAC		0x800c
#define PLL_SYS_PRIM			0x8010
#define PLL_SYS_SEC			0x8014
#define PLL_AUDIO_CS			0xc000
#define PLL_AUDIO_PWR			0xc004
#define PLL_AUDIO_FBDIV_INT		0xc008
#define PLL_AUDIO_FBDIV_FRAC		0xc00c
#define PLL_AUDIO_PRIM			0xc010
#define PLL_AUDIO_SEC			0xc014

#define PLL_CS_REFDIV_SHIFT		0
#define PLL_PWR_DSMPD			(1 << 2)
#define PLL_PRIM_DIV1_MASK		(0x7 << 16)
#define PLL_PRIM_DIV1_SHIFT		16
#define PLL_PRIM_DIV2_MASK		(0x7 << 12)
#define PLL_PRIM_DIV2_SHIFT		12
#define PLL_SEC_RST			(1 << 16)
#define PLL_SEC_DIV_MASK		(0x1f << 8)
#define PLL_SEC_DIV_SHIFT		8

struct rpiclock {
	uint16_t idx;
	uint16_t ctrl_reg;
	uint16_t div_int_reg;
	uint16_t sel_reg;
	uint16_t parents[3];
};

const struct rpiclock rpiclocks[] = {
	{
		RP1_CLK_SYS, CLK_SYS_CTRL, CLK_SYS_DIV_INT, CLK_SYS_SEL,
		{ RP1_CLK_XOSC, 0, RP1_PLL_SYS }
	},
	{
		RP1_CLK_SLOW_SYS, CLK_SLOW_SYS_CTRL, CLK_SLOW_SYS_DIV_INT,
		CLK_SLOW_SYS_SEL,
		{ RP1_CLK_XOSC }
	},
	{
		RP1_CLK_ETH, CLK_ETH_CTRL, CLK_ETH_DIV_INT, 0,
		{ RP1_PLL_SYS_SEC, RP1_PLL_SYS }
	},
	{
		RP1_CLK_PWM0, CLK_PWM0_CTRL, CLK_PWM0_DIV_INT, 0,
		{ 0, 0, RP1_CLK_XOSC }
	},
	{
		RP1_CLK_PWM1, CLK_PWM1_CTRL, CLK_PWM1_DIV_INT, 0,
		{ 0, 0, RP1_CLK_XOSC }
	},
	{
		RP1_CLK_ETH_TSU, CLK_ETH_TSU_CTRL, CLK_ETH_TSU_DIV_INT, 0,
		{ RP1_CLK_XOSC }
	},
	{
		RP1_CLK_SDIO_TIMER, CLK_SDIO_TIMER_CTRL,
		CLK_SDIO_TIMER_DIV_INT, 0,
		{ RP1_CLK_XOSC }
	},
	{
		RP1_CLK_SDIO_ALT_SRC, CLK_SDIO_ALT_SRC_CTRL,
		CLK_SDIO_ALT_SRC_DIV_INT, 0,
		{ RP1_PLL_SYS }
	},
};

struct rpiclock_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
	uint32_t		sc_xosc_freq;

	struct clock_device	sc_cd;
};

int	rpiclock_match(struct device *, void *, void *);
void	rpiclock_attach(struct device *, struct device *, void *);

const struct cfattach rpiclock_ca = {
	sizeof(struct rpiclock_softc), rpiclock_match, rpiclock_attach
};

struct cfdriver rpiclock_cd = {
	NULL, "rpiclock", DV_DULL
};

uint32_t	rpiclock_get_frequency(void *, uint32_t *);
int		rpiclock_set_frequency(void *, uint32_t *, uint32_t);
void		rpiclock_enable(void *, uint32_t *, int);

int
rpiclock_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "raspberrypi,rp1-clocks");
}

void
rpiclock_attach(struct device *parent, struct device *self, void *aux)
{
	struct rpiclock_softc *sc = (struct rpiclock_softc *)self;
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

	sc->sc_xosc_freq = clock_get_frequency(faa->fa_node, NULL);
	if (sc->sc_xosc_freq == 0) {
		printf(": no clock\n");
		return;
	}

	printf("\n");

	sc->sc_cd.cd_node = faa->fa_node;
	sc->sc_cd.cd_cookie = sc;
	sc->sc_cd.cd_get_frequency = rpiclock_get_frequency;
	sc->sc_cd.cd_set_frequency = rpiclock_set_frequency;
	sc->sc_cd.cd_enable = rpiclock_enable;
	clock_register(&sc->sc_cd);

	clock_set_assigned(faa->fa_node);
}

const struct rpiclock *
rpiclock_lookup(uint32_t idx)
{
	int i;

	for (i = 0; i < nitems(rpiclocks); i++) {
		if (rpiclocks[i].idx == idx)
			return &rpiclocks[i];
	}

	return NULL;
}

#define DIV_ROUND(_n, _d)	(((_n) + ((_d) / 2)) / (_d))

uint32_t
rpiclock_get_pll_core_frequency(struct rpiclock_softc *sc, bus_addr_t base)
{
	uint64_t fbdiv_int, fbdiv_frac;
	uint64_t freq = sc->sc_xosc_freq;

	fbdiv_int = HREAD4(sc, base + PLL_SYS_FBDIV_INT - PLL_SYS_CS);
	fbdiv_frac = HREAD4(sc,base + PLL_SYS_FBDIV_FRAC - PLL_SYS_CS);
	freq = (freq * ((fbdiv_int << 24) + fbdiv_frac) + (1 << 23)) >> 24;

	return freq;
}

int
rpiclock_set_pll_core_frequency(struct rpiclock_softc *sc, bus_addr_t base,
    uint32_t freq)
{
	uint32_t parent_freq = sc->sc_xosc_freq;
	uint32_t fbdiv_int, fbdiv_frac;
	uint64_t div;

	if (parent_freq > (freq / 16))
		return -1;

	HWRITE4(sc, base + PLL_SYS_FBDIV_INT - PLL_SYS_CS, 0);
	HWRITE4(sc, base + PLL_SYS_FBDIV_FRAC - PLL_SYS_CS, 0);

	/*
	 * This code uses 32.32 fixed-point representation to be able
	 * to calculate the fractional divider.  The actual hardware
	 * only uses 24 bits for the fraction.  The extra precision
	 * allows us to easily round to the nearest supported
	 * frequency.
	 */
	div = DIV_ROUND((uint64_t)freq << 32, parent_freq);

	/* Round to nearest. */
	div += (1 << (32 - 24 - 1));

	fbdiv_int = div >> 32;
	fbdiv_frac = (div >> (32 - 24)) & 0xffffff;

	if (fbdiv_frac != 0) {
		HCLR4(sc, base + PLL_SYS_FBDIV_INT - PLL_SYS_PWR,
		   PLL_PWR_DSMPD);
	} else {
		HSET4(sc, base + PLL_SYS_FBDIV_INT - PLL_SYS_PWR,
		   PLL_PWR_DSMPD);
	}

	HWRITE4(sc, base + PLL_SYS_FBDIV_INT - PLL_SYS_CS, fbdiv_int);
	HWRITE4(sc, base + PLL_SYS_FBDIV_FRAC - PLL_SYS_CS, fbdiv_frac);
	HSET4(sc, base, 1 << PLL_CS_REFDIV_SHIFT);

	return 0;
}

uint32_t
rpiclock_get_pll_frequency(struct rpiclock_softc *sc, bus_addr_t base)
{
	uint64_t freq = rpiclock_get_pll_core_frequency(sc, base);
	uint32_t prim, div1, div2;

	prim = HREAD4(sc, base + PLL_SYS_PRIM - PLL_SYS_CS);
	div1 = (prim & PLL_PRIM_DIV1_MASK) >> PLL_PRIM_DIV1_SHIFT;
	div2 = (prim & PLL_PRIM_DIV2_MASK) >> PLL_PRIM_DIV2_SHIFT;
	if (div1 == 0 || div2 == 0)
		return 0;

	return DIV_ROUND(freq, div1 * div2);
}

int
rpiclock_set_pll_frequency(struct rpiclock_softc *sc, bus_addr_t base,
    uint32_t freq)
{
	uint64_t parent_freq = rpiclock_get_pll_core_frequency(sc, base);
	uint32_t best_div1, best_div2, best_freq;
	uint32_t f, div1, div2;
	uint32_t prim;

	/*
	 * Find the best set of dividers to get a frequency closest to
	 * the target frequency.
	 */
	best_div1 = best_div2 = 7;
	best_freq = DIV_ROUND(parent_freq, best_div1 * best_div2);
	for (div1 = 1; div1 <= 7; div1++) {
		for (div2 = 1; div2 <= div1; div2++) {
			f = DIV_ROUND(parent_freq, div1 * div2);
			if (f == freq) {
				best_div1 = div1;
				best_div2 = div2;
				goto found;
			}
			if ((best_freq > freq && f < best_freq) ||
			    (f > best_freq && f <= freq)) {
				best_div1 = div1;
				best_div2 = div2;
				best_freq = f;
			}
		}
	}

found:
	prim = HREAD4(sc, base + PLL_SYS_PRIM - PLL_SYS_CS);
	prim &= ~(PLL_PRIM_DIV1_MASK | PLL_PRIM_DIV2_MASK);
	prim |= (best_div1 << PLL_PRIM_DIV1_SHIFT);
	prim |= (best_div2 << PLL_PRIM_DIV2_SHIFT);
	HWRITE4(sc, base + PLL_SYS_PRIM - PLL_SYS_CS, prim);

	return 0;
}

uint32_t
rpiclock_get_pll_sec_frequency(struct rpiclock_softc *sc, bus_addr_t base)
{
	uint64_t freq = rpiclock_get_pll_core_frequency(sc, base);
	uint32_t sec, div;

	sec = HREAD4(sc, base + PLL_SYS_SEC - PLL_SYS_CS);
	div = (sec & PLL_SEC_DIV_MASK) >> PLL_SEC_DIV_SHIFT;
	if (div == 0)
		return 0;

	return freq / div;
}

int
rpiclock_set_pll_sec_frequency(struct rpiclock_softc *sc, bus_addr_t base,
    uint32_t freq)
{
	uint64_t parent_freq = rpiclock_get_pll_core_frequency(sc, base);
	uint32_t sec, div;
	int s;

	/*
	 * Linux rounds up here (instead of nearest), but that results
	 * in the wrong clock for the RP1_PLL_AUDIO_SEC.
	 */
	div = DIV_ROUND(parent_freq, freq);
	div = MAX(div, 8);
	div = MIN(div, 19);

	sec = HREAD4(sc, base + PLL_SYS_SEC - PLL_SYS_CS);
	sec &= ~PLL_SEC_DIV_MASK;
	sec |= (div << PLL_SEC_DIV_SHIFT);
	s = splhigh();
	HWRITE4(sc, base + PLL_SYS_SEC - PLL_SYS_CS, sec | PLL_SEC_RST);
	delay((10 * div * 1000000) / parent_freq);
	HWRITE4(sc, base + PLL_SYS_SEC - PLL_SYS_CS, sec);
	splx(s);

	return 0;
}

uint32_t
rpiclock_freq(struct rpiclock_softc *sc, const struct rpiclock *clk,
    uint32_t mux, uint32_t freq)
{
	uint32_t parent_freq, div;
	uint32_t idx = clk->parents[mux];

	parent_freq = rpiclock_get_frequency(sc, &idx);
	div = (parent_freq + freq - 1) / freq;
	if (div == 0)
		return 0;
	return parent_freq / div;
}

uint32_t
rpiclock_get_frequency(void *cookie, uint32_t *cells)
{
	struct rpiclock_softc *sc = cookie;
	const struct rpiclock *clk;
	uint32_t idx = cells[0];
	uint32_t ctrl, sel, div;
	uint32_t parent, freq;

	switch (idx) {
	case RP1_PLL_SYS_CORE:
		return rpiclock_get_pll_core_frequency(sc, PLL_SYS_CS);
	case RP1_PLL_AUDIO_CORE:
		return rpiclock_get_pll_core_frequency(sc, PLL_AUDIO_CS);
	case RP1_PLL_SYS:
		return rpiclock_get_pll_frequency(sc, PLL_SYS_CS);
	case RP1_PLL_AUDIO:
		return rpiclock_get_pll_frequency(sc, PLL_AUDIO_CS);
	case RP1_PLL_SYS_SEC:
		return rpiclock_get_pll_sec_frequency(sc, PLL_SYS_CS);
	case RP1_PLL_AUDIO_SEC:
		return rpiclock_get_pll_sec_frequency(sc, PLL_AUDIO_CS);
	case RP1_PLL_SYS_PRI_PH:
		return rpiclock_get_pll_frequency(sc, PLL_SYS_CS) / 2;
	case RP1_CLK_XOSC:
		return sc->sc_xosc_freq;
	}

	clk = rpiclock_lookup(idx);
	if (clk == NULL) {
		printf("%s(%s, %u)\n", __func__, sc->sc_dev.dv_xname, idx);
		return 0;
	}

	if (clk->sel_reg) {
		sel = HREAD4(sc, clk->sel_reg);
		parent = ffs(sel) - 1;
	} else {
		ctrl = HREAD4(sc, clk->ctrl_reg);
		parent = (ctrl & CLK_CTRL_AUXSRC_MASK) >>
		     CLK_CTRL_AUXSRC_SHIFT;
	}
	if (parent >= nitems(clk->parents))
		return 0;
	parent = clk->parents[parent];
	if (parent == 0)
		return 0;

	freq = rpiclock_get_frequency(sc, &parent);
	div = HREAD4(sc, clk->div_int_reg);
	if (div == 0)
		return 0;

	return freq / div;
}

int
rpiclock_set_frequency(void *cookie, uint32_t *cells, uint32_t freq)
{
	struct rpiclock_softc *sc = cookie;
	const struct rpiclock *clk;
	uint32_t idx = cells[0];
	uint32_t parent, parent_freq;
	uint32_t best_freq, best_mux, f;
	uint32_t ctrl, div, sel;
	int i;

	/*
	 * Don't bother changing the clock if it is already set
	 * to the right frequency.
	 */
	best_freq = rpiclock_get_frequency(sc, cells);
	if (best_freq == freq)
		return 0;

	switch (idx) {
	case RP1_PLL_SYS_CORE:
		return rpiclock_set_pll_core_frequency(sc, PLL_SYS_CS, freq);
	case RP1_PLL_AUDIO_CORE:
		return rpiclock_set_pll_core_frequency(sc, PLL_AUDIO_CS, freq);
	case RP1_PLL_SYS:
		return rpiclock_set_pll_frequency(sc, PLL_SYS_CS, freq);
	case RP1_PLL_AUDIO:
		return rpiclock_set_pll_frequency(sc, PLL_AUDIO_CS, freq);
	case RP1_PLL_SYS_SEC:
		return rpiclock_set_pll_sec_frequency(sc, PLL_SYS_CS, freq);
	case RP1_PLL_AUDIO_SEC:
		return rpiclock_set_pll_sec_frequency(sc, PLL_AUDIO_CS, freq);
	}

	clk = rpiclock_lookup(idx);
	if (clk == NULL) {
		printf("%s(%s, %u, %u)\n", __func__, sc->sc_dev.dv_xname,
		    idx, freq);
		return -1;
	}

	/*
	 * Start out with the current parent.  This prevents
	 * unnecessary switching to a different parent.
	 */
	if (clk->sel_reg) {
		sel = HREAD4(sc, clk->sel_reg);
		best_mux = ffs(sel) - 1;
	} else {
		ctrl = HREAD4(sc, clk->ctrl_reg);
		best_mux = (ctrl & CLK_CTRL_AUXSRC_MASK) >>
		     CLK_CTRL_AUXSRC_SHIFT;
	}

	/*
	 * Find the parent that allows configuration of a frequency
	 * closest to the target frequency.
	 */
	for (i = 0; i < nitems(clk->parents); i++) {
		if (clk->parents[i] == 0)
			continue;
		f = rpiclock_freq(sc, clk, i, freq);
		if ((best_freq > freq && f < best_freq) ||
		    (f > best_freq && f <= freq)) {
			best_freq = f;
			best_mux = i;
		}
	}

	ctrl = HREAD4(sc, clk->ctrl_reg);
	if (clk->sel_reg) {
		ctrl &= ~CLK_CTRL_SRC_MASK;
		ctrl |= (best_mux << CLK_CTRL_SRC_SHIFT);
	} else {
		ctrl &= ~CLK_CTRL_AUXSRC_MASK;
		ctrl |= (best_mux << CLK_CTRL_AUXSRC_SHIFT);
		ctrl &= ~CLK_CTRL_SRC_MASK;
		ctrl |= CLK_CTRL_SRC_AUX;
	}
	HWRITE4(sc, clk->ctrl_reg, ctrl);

	parent = clk->parents[best_mux];
	parent_freq = rpiclock_get_frequency(sc, &parent);
	div = (parent_freq + freq - 1) / freq;
	HWRITE4(sc, clk->div_int_reg, div);

	return 0;
}

void
rpiclock_enable(void *cookie, uint32_t *cells, int on)
{
	struct rpiclock_softc *sc = cookie;
	const struct rpiclock *clk;
	uint32_t idx = cells[0];

	clk = rpiclock_lookup(idx);
	if (clk == NULL) {
		printf("%s(%s, %u)\n", __func__, sc->sc_dev.dv_xname, idx);
		return;
	}

	if (on)
		HSET4(sc, clk->ctrl_reg, CLK_CTRL_ENABLE);
	else
		HCLR4(sc, clk->ctrl_reg, CLK_CTRL_ENABLE);
}
