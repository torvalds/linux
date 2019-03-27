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
 * Ingenic JZ4780 generic CGU clock driver.
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

/* JZ4780 generic mux and div clocks implementation */
static int jz4780_clk_gen_init(struct clknode *clk, device_t dev);
static int jz4780_clk_gen_recalc_freq(struct clknode *clk, uint64_t *freq);
static int jz4780_clk_gen_set_freq(struct clknode *clk, uint64_t fin,
    uint64_t *fout, int flags, int *stop);
static int jz4780_clk_gen_set_gate(struct clknode *clk, bool enable);
static int jz4780_clk_gen_set_mux(struct clknode *clk, int src);

struct jz4780_clk_gen_sc {
	struct mtx	*clk_mtx;
	struct resource *clk_res;
	int clk_reg;
	const struct jz4780_clk_descr *clk_descr;
};

/*
 * JZ4780 clock PLL clock methods
 */
static clknode_method_t jz4780_clk_gen_methods[] = {
	CLKNODEMETHOD(clknode_init,		jz4780_clk_gen_init),
	CLKNODEMETHOD(clknode_set_gate,		jz4780_clk_gen_set_gate),
	CLKNODEMETHOD(clknode_recalc_freq,	jz4780_clk_gen_recalc_freq),
	CLKNODEMETHOD(clknode_set_freq,		jz4780_clk_gen_set_freq),
	CLKNODEMETHOD(clknode_set_mux,		jz4780_clk_gen_set_mux),

	CLKNODEMETHOD_END
};
DEFINE_CLASS_1(jz4780_clk_pll, jz4780_clk_gen_class, jz4780_clk_gen_methods,
       sizeof(struct jz4780_clk_gen_sc), clknode_class);

static inline unsigned
mux_to_reg(unsigned src, unsigned map)
{
	unsigned ret, bit;

	bit = (1u << 3);
	for (ret = 0; bit; ret++, bit >>= 1) {
		if (map & bit) {
			if (src-- == 0)
				return (ret);
		}
	}
	panic("mux_to_reg");
}

static inline unsigned
reg_to_mux(unsigned reg, unsigned map)
{
	unsigned ret, bit;

	bit = (1u << 3);
	for (ret = 0; reg; reg--, bit >>= 1)
		if (map & bit)
			ret++;
	return (ret);
}

static int
jz4780_clk_gen_init(struct clknode *clk, device_t dev)
{
	struct jz4780_clk_gen_sc *sc;
	uint32_t reg, msk, parent_idx;

	sc = clknode_get_softc(clk);
	CLK_LOCK(sc);
	/* Figure our parent out */
	if (sc->clk_descr->clk_type & CLK_MASK_MUX) {
		msk = (1u << sc->clk_descr->clk_mux.mux_bits) - 1;
		reg = CLK_RD_4(sc, sc->clk_descr->clk_mux.mux_reg);
		reg = (reg >> sc->clk_descr->clk_mux.mux_shift) & msk;
		parent_idx = reg_to_mux(reg, sc->clk_descr->clk_mux.mux_map);
	} else
		parent_idx = 0;
	CLK_UNLOCK(sc);

	clknode_init_parent_idx(clk, parent_idx);
	return (0);
}

static int
jz4780_clk_gen_recalc_freq(struct clknode *clk, uint64_t *freq)
{
	struct jz4780_clk_gen_sc *sc;
	uint32_t reg;

	sc = clknode_get_softc(clk);

	/* Calculate divisor frequency */
	if (sc->clk_descr->clk_type & CLK_MASK_DIV) {
		uint32_t msk;

		msk = (1u << sc->clk_descr->clk_div.div_bits) - 1;
		reg = CLK_RD_4(sc, sc->clk_descr->clk_div.div_reg);
		reg = (reg >> sc->clk_descr->clk_div.div_shift) & msk;
		reg = (reg + 1) << sc->clk_descr->clk_div.div_lg;
		*freq /= reg;
	}
	return (0);
}

#define DIV_TIMEOUT	100

static int
jz4780_clk_gen_set_freq(struct clknode *clk, uint64_t fin,
    uint64_t *fout, int flags, int *stop)
{
	struct jz4780_clk_gen_sc *sc;
	uint64_t _fout;
	uint32_t divider, div_reg, div_msk, reg, div_l, div_h;
	int rv;

	sc = clknode_get_softc(clk);

	/* Find closest divider */
	div_l = howmany(fin, *fout);
	div_h = fin / *fout;
	divider = abs((int64_t)*fout - (fin / div_l)) <
	    abs((int64_t)*fout - (fin / div_h)) ? div_l : div_h;

	/* Adjust for divider multiplier */
	div_reg = divider >> sc->clk_descr->clk_div.div_lg;
	divider = div_reg << sc->clk_descr->clk_div.div_lg;
	if (divider == 0)
		divider = 1;

	_fout = fin / divider;

	/* Rounding */
	if ((flags & CLK_SET_ROUND_UP) && (*fout > _fout))
		div_reg--;
	else if ((flags & CLK_SET_ROUND_DOWN) && (*fout < _fout))
		div_reg++;
	if (div_reg == 0)
		div_reg = 1;

	div_msk = (1u << sc->clk_descr->clk_div.div_bits) - 1;

	*stop = 1;
	if (div_reg > div_msk + 1) {
		*stop = 0;
		div_reg = div_msk;
	}

	divider = (div_reg << sc->clk_descr->clk_div.div_lg);
	div_reg--;

	if ((flags & CLK_SET_DRYRUN) != 0) {
		if (*stop != 0 && *fout != fin / divider &&
		    (flags & (CLK_SET_ROUND_UP | CLK_SET_ROUND_DOWN)) == 0)
			return (ERANGE);
		*fout = fin / divider;
		return (0);
	}

	CLK_LOCK(sc);
	/* Apply the new divider value */
	reg = CLK_RD_4(sc, sc->clk_descr->clk_div.div_reg);
	reg &= ~(div_msk << sc->clk_descr->clk_div.div_shift);
	reg |= (div_reg << sc->clk_descr->clk_div.div_shift);
	/* Set the change enable bit, it present */
	if (sc->clk_descr->clk_div.div_ce_bit >= 0)
		reg |= (1u << sc->clk_descr->clk_div.div_ce_bit);
	/* Clear stop bit, it present */
	if (sc->clk_descr->clk_div.div_st_bit >= 0)
		reg &= ~(1u << sc->clk_descr->clk_div.div_st_bit);
	/* Initiate the change */
	CLK_WR_4(sc, sc->clk_descr->clk_div.div_reg, reg);

	/* Wait for busy bit to clear indicating the change is complete */
	rv = 0;
	if (sc->clk_descr->clk_div.div_busy_bit >= 0) {
		int i;

		for (i = 0;  i < DIV_TIMEOUT; i++) {
			reg = CLK_RD_4(sc, sc->clk_descr->clk_div.div_reg);
			if (!(reg & (1u << sc->clk_descr->clk_div.div_busy_bit)))
				break;
			DELAY(1000);
		}
		if (i == DIV_TIMEOUT)
			rv = ETIMEDOUT;
	}
	CLK_UNLOCK(sc);

	*fout = fin / divider;
	return (rv);
}

static int
jz4780_clk_gen_set_mux(struct clknode *clk, int src)
{
	struct jz4780_clk_gen_sc *sc;
	uint32_t reg, msk;

	sc = clknode_get_softc(clk);

	/* Only mux nodes are capable of being reparented */
	if (!(sc->clk_descr->clk_type & CLK_MASK_MUX))
		return (src ? EINVAL : 0);

	msk = (1u << sc->clk_descr->clk_mux.mux_bits) - 1;
	src = mux_to_reg(src & msk, sc->clk_descr->clk_mux.mux_map);

	CLK_LOCK(sc);
	reg = CLK_RD_4(sc, sc->clk_descr->clk_mux.mux_reg);
	reg &= ~(msk << sc->clk_descr->clk_mux.mux_shift);
	reg |=  (src << sc->clk_descr->clk_mux.mux_shift);
	CLK_WR_4(sc, sc->clk_descr->clk_mux.mux_reg, reg);
	CLK_UNLOCK(sc);

	return (0);
}

static int
jz4780_clk_gen_set_gate(struct clknode *clk, bool enable)
{
	struct jz4780_clk_gen_sc *sc;
	uint32_t off, reg, bit;

	sc = clknode_get_softc(clk);

	/* Check is clock can be gated */
	if (sc->clk_descr->clk_gate_bit < 0)
		return 0;

	bit = sc->clk_descr->clk_gate_bit;
	if (bit < 32) {
		off = JZ_CLKGR0;
	} else {
		off = JZ_CLKGR1;
		bit -= 32;
	}

	CLK_LOCK(sc);
	reg = CLK_RD_4(sc, off);
	if (enable)
		reg &= ~(1u << bit);
	else
		reg |= (1u << bit);
	CLK_WR_4(sc, off, reg);
	CLK_UNLOCK(sc);

	return (0);
}


int jz4780_clk_gen_register(struct clkdom *clkdom,
    const struct jz4780_clk_descr *descr, struct mtx *dev_mtx,
    struct resource *mem_res)
{
	struct clknode_init_def clkdef;
	struct clknode *clk;
	struct jz4780_clk_gen_sc *sc;

	clkdef.id = descr->clk_id;
	clkdef.name = __DECONST(char *, descr->clk_name);
	/* Silly const games to work around API deficiency */
	clkdef.parent_names = (const char **)(uintptr_t)&descr->clk_pnames[0];
	clkdef.flags = CLK_NODE_STATIC_STRINGS;
	if (descr->clk_type & CLK_MASK_MUX)
		clkdef.parent_cnt = __bitcount16(descr->clk_mux.mux_map);
	else
		clkdef.parent_cnt = 1;

	clk = clknode_create(clkdom, &jz4780_clk_gen_class, &clkdef);
	if (clk == NULL)
		return (1);

	sc = clknode_get_softc(clk);
	sc->clk_mtx = dev_mtx;
	sc->clk_res = mem_res;
	sc->clk_descr = descr;
	clknode_register(clkdom, clk);

	return (0);
}
