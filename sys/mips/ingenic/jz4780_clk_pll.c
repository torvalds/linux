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
 * Ingenic JZ4780 CGU driver.
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

/**********************************************************************
 *  JZ4780 PLL control register bit fields
 **********************************************************************/
#define CGU_PLL_M_SHIFT		19
#define CGU_PLL_M_WIDTH		13

#define CGU_PLL_N_SHIFT		13
#define CGU_PLL_N_WIDTH		6

#define CGU_PLL_OD_SHIFT	9
#define CGU_PLL_OD_WIDTH	4

#define CGU_PLL_LOCK_SHIFT	6
#define CGU_PLL_LOCK_WIDTH	1

#define CGU_PLL_ON_SHIFT	4
#define CGU_PLL_ON_WIDTH	1

#define CGU_PLL_MODE_SHIFT	3
#define CGU_PLL_MODE_WIDTH	1

#define CGU_PLL_BP_SHIFT	1
#define CGU_PLL_BP_WIDTH	1

#define CGU_PLL_EN_SHIFT	0
#define CGU_PLL_EN_WIDTH	1

/* JZ4780 PLL clock */
static int jz4780_clk_pll_init(struct clknode *clk, device_t dev);
static int jz4780_clk_pll_recalc_freq(struct clknode *clk, uint64_t *freq);
static int jz4780_clk_pll_set_freq(struct clknode *clk, uint64_t fin,
    uint64_t *fout, int flags, int *stop);

struct jz4780_clk_pll_sc {
	struct mtx	*clk_mtx;
	struct resource *clk_res;
	uint32_t	 clk_reg;
};

/*
 * JZ4780 PLL clock methods
 */
static clknode_method_t jz4780_clk_pll_methods[] = {
	CLKNODEMETHOD(clknode_init,		jz4780_clk_pll_init),
	CLKNODEMETHOD(clknode_recalc_freq,	jz4780_clk_pll_recalc_freq),
	CLKNODEMETHOD(clknode_set_freq,		jz4780_clk_pll_set_freq),

	CLKNODEMETHOD_END
};
DEFINE_CLASS_1(jz4780_clk_pll, jz4780_clk_pll_class, jz4780_clk_pll_methods,
       sizeof(struct jz4780_clk_pll_sc), clknode_class);

static int
jz4780_clk_pll_init(struct clknode *clk, device_t dev)
{
	struct jz4780_clk_pll_sc *sc;
	uint32_t reg;

	sc = clknode_get_softc(clk);
	CLK_LOCK(sc);
	reg = CLK_RD_4(sc, sc->clk_reg);
	CLK_WR_4(sc, sc->clk_reg, reg);
	CLK_UNLOCK(sc);

	clknode_init_parent_idx(clk, 0);
	return (0);
}

static int
jz4780_clk_pll_recalc_freq(struct clknode *clk, uint64_t *freq)
{
	struct jz4780_clk_pll_sc *sc;
	uint32_t reg, m, n, od;

	sc = clknode_get_softc(clk);
	reg = CLK_RD_4(sc, sc->clk_reg);

	/* Check for PLL enabled status */
	if (REG_GET(reg, CGU_PLL_EN) == 0) {
		*freq = 0;
		return 0;
	}

	/* Return parent frequency if PPL is being bypassed */
	if (REG_GET(reg, CGU_PLL_BP) != 0)
		return 0;

	m = REG_GET(reg, CGU_PLL_M) + 1;
	n = REG_GET(reg, CGU_PLL_N) + 1;
	od = REG_GET(reg, CGU_PLL_OD) + 1;

	/* Sanity check values */
	if (m == 0 || n == 0 || od == 0) {
		*freq = 0;
		return (EINVAL);
	}

	*freq = ((*freq / n) * m) / od;
	return (0);
}

#define MHZ		(1000 * 1000)
#define PLL_TIMEOUT	100

static int
jz4780_clk_pll_wait_lock(struct jz4780_clk_pll_sc *sc)
{
	int i;

	for (i = 0;  i < PLL_TIMEOUT; i++) {
		if (CLK_RD_4(sc, sc->clk_reg) & REG_VAL(CGU_PLL_LOCK, 1))
			return (0);
		DELAY(1000);
	}
	return (ETIMEDOUT);
}

static int
jz4780_clk_pll_set_freq(struct clknode *clk, uint64_t fin,
    uint64_t *fout, int flags, int *stop)
{
	struct jz4780_clk_pll_sc *sc;
	uint32_t reg, m, n, od;
	int rv;

	sc = clknode_get_softc(clk);

	/* Should be able to figure all clocks with m & n only */
	od = 1;

	m = MIN((uint32_t)(*fout / MHZ), (1u << CGU_PLL_M_WIDTH));
	m = MIN(m, 1);

	n = MIN((uint32_t)(fin / MHZ), (1u << CGU_PLL_N_WIDTH));
	n = MIN(n, 1);

	if (flags & CLK_SET_DRYRUN) {
		if (((flags & (CLK_SET_ROUND_UP | CLK_SET_ROUND_DOWN)) == 0) &&
		    (*fout != (((fin / n) * m) / od)))
		return (ERANGE);

		*fout = ((fin / n) * m) / od;
		return (0);
	}

	CLK_LOCK(sc);
	reg = CLK_RD_4(sc, sc->clk_reg);

	/* Set the calculated values */
	reg = REG_SET(reg, CGU_PLL_M, m - 1);
	reg = REG_SET(reg, CGU_PLL_N, n - 1);
	reg = REG_SET(reg, CGU_PLL_OD, od - 1);

	/* Enable the PLL */
	reg = REG_SET(reg, CGU_PLL_EN, 1);
	reg = REG_SET(reg, CGU_PLL_BP, 0);

	/* Initiate the change */
	CLK_WR_4(sc, sc->clk_reg, reg);

	/* Wait for PLL to lock */
	rv = jz4780_clk_pll_wait_lock(sc);
	CLK_UNLOCK(sc);
	if (rv != 0)
		return (rv);

	*fout = ((fin / n) * m) / od;
	return (0);
}

int jz4780_clk_pll_register(struct clkdom *clkdom,
    struct clknode_init_def *clkdef, struct mtx *dev_mtx,
    struct resource *mem_res, uint32_t mem_reg)
{
	struct clknode *clk;
	struct jz4780_clk_pll_sc *sc;

	clk = clknode_create(clkdom, &jz4780_clk_pll_class, clkdef);
	if (clk == NULL)
		return (1);

	sc = clknode_get_softc(clk);
	sc->clk_mtx = dev_mtx;
	sc->clk_res = mem_res;
	sc->clk_reg = mem_reg;
	clknode_register(clkdom, clk);
	return (0);
}
