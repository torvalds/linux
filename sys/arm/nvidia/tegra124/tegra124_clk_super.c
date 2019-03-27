/*-
 * Copyright (c) 2016 Michal Meloun <mmel@FreeBSD.org>
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/rman.h>

#include <machine/bus.h>

#include <dev/extres/clk/clk.h>

#include <gnu/dts/include/dt-bindings/clock/tegra124-car.h>
#include "tegra124_car.h"


/* Flags */
#define	SMF_HAVE_DIVIDER_2	1

struct super_mux_def {
	struct clknode_init_def	clkdef;
	uint32_t		base_reg;
	uint32_t		flags;
	int			src_pllx;
	int			src_div2;
};

#define	PLIST(x) static const char *x[]
#define	SM(_id, cn, pl, r, x, d, f)				\
{									\
	.clkdef.id = _id,						\
	.clkdef.name = cn,						\
	.clkdef.parent_names = pl,					\
	.clkdef.parent_cnt = nitems(pl),				\
	.clkdef.flags = CLK_NODE_STATIC_STRINGS,				\
	.base_reg = r,							\
	.src_pllx = x,							\
	.src_div2 = d,							\
	.flags = f,							\
}

PLIST(cclk_g_parents) = {
	"clk_m", "pllC_out0", "clk_s", "pllM_out0",
	"pllP_out0", "pllP_out4", "pllC2_out0", "pllC3_out0",
	"pllX_out", NULL, NULL, NULL,
	NULL, NULL, NULL,NULL, // "dfllCPU_out0"
};

PLIST(cclk_lp_parents) = {
	"clk_m", "pllC_out0", "clk_s", "pllM_out0",
	"pllP_out0", "pllP_out4", "pllC2_out0", "pllC3_out0",
	"pllX_out", NULL, NULL, NULL,
	NULL, NULL, NULL, NULL,
	"pllX_out0"
};

PLIST(sclk_parents) = {
	"clk_m", "pllC_out1", "pllP_out4", "pllP_out0",
	"pllP_out2", "pllC_out0", "clk_s", "pllM_out1",
};

static struct super_mux_def super_mux_def[] = {
 SM(TEGRA124_CLK_CCLK_G, "cclk_g", cclk_g_parents, CCLKG_BURST_POLICY, 0, 0, 0),
 SM(TEGRA124_CLK_CCLK_LP, "cclk_lp", cclk_lp_parents, CCLKLP_BURST_POLICY, 8, 16, SMF_HAVE_DIVIDER_2),
 SM(TEGRA124_CLK_SCLK, "sclk", sclk_parents, SCLK_BURST_POLICY, 0, 0, 0),
};

static int super_mux_init(struct clknode *clk, device_t dev);
static int super_mux_set_mux(struct clknode *clk, int idx);

struct super_mux_sc {
	device_t		clkdev;
	uint32_t		base_reg;
	int			src_pllx;
	int			src_div2;
	uint32_t		flags;

	int 			mux;
};

static clknode_method_t super_mux_methods[] = {
	/* Device interface */
	CLKNODEMETHOD(clknode_init,		super_mux_init),
	CLKNODEMETHOD(clknode_set_mux, 		super_mux_set_mux),
	CLKNODEMETHOD_END
};
DEFINE_CLASS_1(tegra124_super_mux, tegra124_super_mux_class, super_mux_methods,
   sizeof(struct super_mux_sc), clknode_class);

/* Mux status. */
#define	SUPER_MUX_STATE_STDBY		0
#define	SUPER_MUX_STATE_IDLE		1
#define	SUPER_MUX_STATE_RUN		2
#define	SUPER_MUX_STATE_IRQ		3
#define	SUPER_MUX_STATE_FIQ		4

/* Mux register bits. */
#define	SUPER_MUX_STATE_BIT_SHIFT	28
#define	SUPER_MUX_STATE_BIT_MASK	0xF
/* State is Priority encoded */
#define	SUPER_MUX_STATE_BIT_STDBY	0x00
#define	SUPER_MUX_STATE_BIT_IDLE	0x01
#define	SUPER_MUX_STATE_BIT_RUN		0x02
#define	SUPER_MUX_STATE_BIT_IRQ		0x04
#define	SUPER_MUX_STATE_BIT_FIQ		0x08

#define	SUPER_MUX_MUX_WIDTH		4
#define	SUPER_MUX_LP_DIV2_BYPASS	(1 << 16)

static uint32_t
super_mux_get_state(uint32_t reg)
{
	reg = (reg >> SUPER_MUX_STATE_BIT_SHIFT) & SUPER_MUX_STATE_BIT_MASK;
	if (reg & SUPER_MUX_STATE_BIT_FIQ)
		 return (SUPER_MUX_STATE_FIQ);
	if (reg & SUPER_MUX_STATE_BIT_IRQ)
		 return (SUPER_MUX_STATE_IRQ);
	if (reg & SUPER_MUX_STATE_BIT_RUN)
		 return (SUPER_MUX_STATE_RUN);
	if (reg & SUPER_MUX_STATE_BIT_IDLE)
		 return (SUPER_MUX_STATE_IDLE);
	return (SUPER_MUX_STATE_STDBY);
}

static int
super_mux_init(struct clknode *clk, device_t dev)
{
	struct super_mux_sc *sc;
	uint32_t reg;
	int shift, state;

	sc = clknode_get_softc(clk);

	DEVICE_LOCK(sc);
	RD4(sc, sc->base_reg, &reg);
	DEVICE_UNLOCK(sc);
	state = super_mux_get_state(reg);

	if ((state != SUPER_MUX_STATE_RUN) &&
	    (state != SUPER_MUX_STATE_IDLE)) {
		panic("Unexpected super mux state: %u", state);
	}

	shift = state * SUPER_MUX_MUX_WIDTH;

	sc->mux = (reg >> shift) & ((1 << SUPER_MUX_MUX_WIDTH) - 1);

	/*
	 * CCLKLP uses PLLX/2 as source if LP_DIV2_BYPASS isn't set
	 * and source mux is set to PLLX.
	 */
	if (sc->flags & SMF_HAVE_DIVIDER_2) {
		if (((reg & SUPER_MUX_LP_DIV2_BYPASS) == 0) &&
		    (sc->mux == sc->src_pllx))
		sc->mux = sc->src_div2;
	}
	clknode_init_parent_idx(clk, sc->mux);

	return(0);
}

static int
super_mux_set_mux(struct clknode *clk, int idx)
{

	struct super_mux_sc *sc;
	int shift, state;
	uint32_t reg, dummy;

	sc = clknode_get_softc(clk);

	DEVICE_LOCK(sc);
	RD4(sc, sc->base_reg, &reg);
	state = super_mux_get_state(reg);

	if ((state != SUPER_MUX_STATE_RUN) &&
	    (state != SUPER_MUX_STATE_IDLE)) {
		panic("Unexpected super mux state: %u", state);
	}
	shift = (state - 1) * SUPER_MUX_MUX_WIDTH;
	sc->mux = idx;
	if (sc->flags & SMF_HAVE_DIVIDER_2) {
		if (idx == sc->src_div2) {
			idx = sc->src_pllx;
			reg &= ~SUPER_MUX_LP_DIV2_BYPASS;
			WR4(sc, sc->base_reg, reg);
			RD4(sc, sc->base_reg, &dummy);
		} else if (idx == sc->src_pllx) {
			reg = SUPER_MUX_LP_DIV2_BYPASS;
			WR4(sc, sc->base_reg, reg);
			RD4(sc, sc->base_reg, &dummy);
		}
	}
	reg &= ~(((1 << SUPER_MUX_MUX_WIDTH) - 1) << shift);
	reg |= idx << shift;

	WR4(sc, sc->base_reg, reg);
	RD4(sc, sc->base_reg, &dummy);
	DEVICE_UNLOCK(sc);

	return(0);
}

static int
super_mux_register(struct clkdom *clkdom, struct super_mux_def *clkdef)
{
	struct clknode *clk;
	struct super_mux_sc *sc;

	clk = clknode_create(clkdom, &tegra124_super_mux_class,
	    &clkdef->clkdef);
	if (clk == NULL)
		return (1);

	sc = clknode_get_softc(clk);
	sc->clkdev = clknode_get_device(clk);
	sc->base_reg = clkdef->base_reg;
	sc->src_pllx = clkdef->src_pllx;
	sc->src_div2 = clkdef->src_div2;
	sc->flags = clkdef->flags;

	clknode_register(clkdom, clk);
	return (0);
}

void
tegra124_super_mux_clock(struct tegra124_car_softc *sc)
{
	int i, rv;

	for (i = 0; i <  nitems(super_mux_def); i++) {
		rv = super_mux_register(sc->clkdom, &super_mux_def[i]);
		if (rv != 0)
			panic("super_mux_register failed");
	}

}
