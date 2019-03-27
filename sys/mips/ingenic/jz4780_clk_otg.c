/*-
 * Copyright 2015 Alexander Kabaev <kan@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Ingenic JZ4780 OTG PHY clock driver.
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/bus.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/resource.h>

#include <machine/bus.h>

#include <mips/ingenic/jz4780_clk.h>
#include <mips/ingenic/jz4780_regs.h>

/* JZ4780 OTG PHY clock */
static int jz4780_clk_otg_init(struct clknode *clk, device_t dev);
static int jz4780_clk_otg_recalc_freq(struct clknode *clk, uint64_t *freq);
static int jz4780_clk_otg_set_freq(struct clknode *clk, uint64_t fin,
    uint64_t *fout, int flags, int *stop);

struct jz4780_clk_otg_sc {
	struct mtx	*clk_mtx;
	struct resource *clk_res;
};

/*
 * JZ4780 OTG PHY clock methods
 */
static clknode_method_t jz4780_clk_otg_methods[] = {
	CLKNODEMETHOD(clknode_init,		jz4780_clk_otg_init),
	CLKNODEMETHOD(clknode_recalc_freq,	jz4780_clk_otg_recalc_freq),
	CLKNODEMETHOD(clknode_set_freq,		jz4780_clk_otg_set_freq),

	CLKNODEMETHOD_END
};
DEFINE_CLASS_1(jz4780_clk_pll, jz4780_clk_otg_class, jz4780_clk_otg_methods,
       sizeof(struct jz4780_clk_otg_sc), clknode_class);

static int
jz4780_clk_otg_init(struct clknode *clk, device_t dev)
{
	struct jz4780_clk_otg_sc *sc;
	uint32_t reg;

	sc = clknode_get_softc(clk);
	CLK_LOCK(sc);
	/* Force the use fo the core clock */
	reg = CLK_RD_4(sc, JZ_USBPCR1);
	reg &= ~PCR_REFCLK_M;
	reg |= PCR_REFCLK_CORE;
	CLK_WR_4(sc, JZ_USBPCR1, reg);
	CLK_UNLOCK(sc);

	clknode_init_parent_idx(clk, 0);
	return (0);
}

static const struct {
	uint32_t div_val;
	uint32_t freq;
} otg_div_table[] = {
    { PCR_CLK_12,	12000000 },
    { PCR_CLK_192,	19200000 },
    { PCR_CLK_24,	24000000 },
    { PCR_CLK_48,	48000000 }
};

static int
jz4780_clk_otg_recalc_freq(struct clknode *clk, uint64_t *freq)
{
	struct jz4780_clk_otg_sc *sc;
	uint32_t reg;
	int i;

	sc = clknode_get_softc(clk);
	reg = CLK_RD_4(sc, JZ_USBPCR1);
	reg &= PCR_CLK_M;

	for (i = 0; i < nitems(otg_div_table); i++)
		if (otg_div_table[i].div_val == reg)
			*freq = otg_div_table[i].freq;
	return (0);
}

static int
jz4780_clk_otg_set_freq(struct clknode *clk, uint64_t fin,
    uint64_t *fout, int flags, int *stop)
{
	struct jz4780_clk_otg_sc *sc;
	uint32_t reg;
	int i;

	sc = clknode_get_softc(clk);

	for (i = 0; i < nitems(otg_div_table) - 1; i++) {
		if (*fout < (otg_div_table[i].freq + otg_div_table[i + 1].freq) / 2)
			break;
	}

	*fout = otg_div_table[i].freq;

	*stop = 1;
	if (flags & CLK_SET_DRYRUN)
		return (0);

	CLK_LOCK(sc);
	reg = CLK_RD_4(sc, JZ_USBPCR1);
	/* Set the calculated values */
	reg &= ~PCR_CLK_M;
	reg |= otg_div_table[i].div_val;
	/* Initiate the change */
	CLK_WR_4(sc, JZ_USBPCR1, reg);
	CLK_UNLOCK(sc);

	return (0);
}

int jz4780_clk_otg_register(struct clkdom *clkdom,
    struct clknode_init_def *clkdef, struct mtx *dev_mtx,
    struct resource *mem_res)
{
	struct clknode *clk;
	struct jz4780_clk_otg_sc *sc;

	clk = clknode_create(clkdom, &jz4780_clk_otg_class, clkdef);
	if (clk == NULL)
		return (1);

	sc = clknode_get_softc(clk);
	sc->clk_mtx = dev_mtx;
	sc->clk_res = mem_res;
	clknode_register(clkdom, clk);
	return (0);
}
