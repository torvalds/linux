/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2018 Emmanuel Vadot <manu@freebsd.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>

#include <dev/extres/clk/clk.h>

#include <arm64/rockchip/clk/rk_clk_armclk.h>

#include "clkdev_if.h"

struct rk_clk_armclk_sc {
	uint32_t	muxdiv_offset;
	uint32_t	mux_shift;
	uint32_t	mux_width;
	uint32_t	mux_mask;

	uint32_t	div_shift;
	uint32_t	div_width;
	uint32_t	div_mask;

	uint32_t	gate_offset;
	uint32_t	gate_shift;

	uint32_t	flags;

	uint32_t	main_parent;
	uint32_t	alt_parent;

	struct rk_clk_armclk_rates	*rates;
	int		nrates;
};

#define	WRITE4(_clk, off, val)						\
	CLKDEV_WRITE_4(clknode_get_device(_clk), off, val)
#define	READ4(_clk, off, val)						\
	CLKDEV_READ_4(clknode_get_device(_clk), off, val)
#define	DEVICE_LOCK(_clk)							\
	CLKDEV_DEVICE_LOCK(clknode_get_device(_clk))
#define	DEVICE_UNLOCK(_clk)						\
	CLKDEV_DEVICE_UNLOCK(clknode_get_device(_clk))

#define	RK_ARMCLK_WRITE_MASK_SHIFT	16

/* #define	dprintf(format, arg...)	printf("%s:(%s)" format, __func__, clknode_get_name(clk), arg) */
#define	dprintf(format, arg...)

static int
rk_clk_armclk_init(struct clknode *clk, device_t dev)
{
	struct rk_clk_armclk_sc *sc;
	uint32_t val, idx;

	sc = clknode_get_softc(clk);

	idx = 0;
	DEVICE_LOCK(clk);
	READ4(clk, sc->muxdiv_offset, &val);
	DEVICE_UNLOCK(clk);

	idx = (val & sc->mux_mask) >> sc->mux_shift;

	clknode_init_parent_idx(clk, idx);

	return (0);
}

static int
rk_clk_armclk_set_mux(struct clknode *clk, int index)
{
	struct rk_clk_armclk_sc *sc;
	uint32_t val = 0;

	sc = clknode_get_softc(clk);

	dprintf("Set mux to %d\n", index);
	DEVICE_LOCK(clk);
	val |= index << sc->mux_shift;
	val |= sc->mux_mask << RK_ARMCLK_WRITE_MASK_SHIFT;
	dprintf("Write: muxdiv_offset=%x, val=%x\n", sc->muxdiv_offset, val);
	WRITE4(clk, sc->muxdiv_offset, val);
	DEVICE_UNLOCK(clk);

	return (0);
}

static int
rk_clk_armclk_recalc(struct clknode *clk, uint64_t *freq)
{
	struct rk_clk_armclk_sc *sc;
	uint32_t reg, div;

	sc = clknode_get_softc(clk);

	DEVICE_LOCK(clk);

	READ4(clk, sc->muxdiv_offset, &reg);
	dprintf("Read: muxdiv_offset=%x, val=%x\n", sc->muxdiv_offset, reg);

	DEVICE_UNLOCK(clk);

	div = ((reg & sc->div_mask) >> sc->div_shift) + 1;
	dprintf("parent_freq=%lu, div=%u\n", *freq, div);

	*freq = *freq / div;

	return (0);
}

static int
rk_clk_armclk_set_freq(struct clknode *clk, uint64_t fparent, uint64_t *fout,
    int flags, int *stop)
{
	struct rk_clk_armclk_sc *sc;
	struct clknode *p_main;
	const char **p_names;
	uint64_t best = 0, best_p = 0;
	uint32_t div = 0, val = 0;
	int err, i, rate = 0;

	sc = clknode_get_softc(clk);

	dprintf("Finding best parent/div for target freq of %lu\n", *fout);
	p_names = clknode_get_parent_names(clk);
	p_main = clknode_find_by_name(p_names[sc->main_parent]);

	for (i = 0; i < sc->nrates; i++) {
		if (sc->rates[i].freq == *fout) {
			best = sc->rates[i].freq;
			div = sc->rates[i].div;
			best_p = best * div;
			rate = i;
			dprintf("Best parent %s (%d) with best freq at %lu\n",
			    clknode_get_name(p_main),
			    sc->main_parent,
			    best);
			break;
		}
	}

	if (rate == sc->nrates)
		return (0);

	if ((flags & CLK_SET_DRYRUN) != 0) {
		*fout = best;
		*stop = 1;
		return (0);
	}

	dprintf("Changing parent (%s) freq to %lu\n", clknode_get_name(p_main), best_p);
	err = clknode_set_freq(p_main, best_p, 0, 1);
	if (err != 0)
		printf("Cannot set %s to %lu\n",
		    clknode_get_name(p_main),
		    best_p);

	clknode_set_parent_by_idx(clk, sc->main_parent);

	clknode_get_freq(p_main, &best_p);
	dprintf("main parent freq at %lu\n", best_p);
	DEVICE_LOCK(clk);
	val |= (div - 1) << sc->div_shift;
	val |= sc->div_mask << RK_ARMCLK_WRITE_MASK_SHIFT;
	dprintf("Write: muxdiv_offset=%x, val=%x\n", sc->muxdiv_offset, val);
	WRITE4(clk, sc->muxdiv_offset, val);
	DEVICE_UNLOCK(clk);

	*fout = best;
	*stop = 1;

	return (0);
}

static clknode_method_t rk_clk_armclk_clknode_methods[] = {
	/* Device interface */
	CLKNODEMETHOD(clknode_init,		rk_clk_armclk_init),
	CLKNODEMETHOD(clknode_set_mux,		rk_clk_armclk_set_mux),
	CLKNODEMETHOD(clknode_recalc_freq,	rk_clk_armclk_recalc),
	CLKNODEMETHOD(clknode_set_freq,		rk_clk_armclk_set_freq),
	CLKNODEMETHOD_END
};

DEFINE_CLASS_1(rk_clk_armclk_clknode, rk_clk_armclk_clknode_class,
    rk_clk_armclk_clknode_methods, sizeof(struct rk_clk_armclk_sc),
    clknode_class);

int
rk_clk_armclk_register(struct clkdom *clkdom, struct rk_clk_armclk_def *clkdef)
{
	struct clknode *clk;
	struct rk_clk_armclk_sc *sc;

	clk = clknode_create(clkdom, &rk_clk_armclk_clknode_class,
	    &clkdef->clkdef);
	if (clk == NULL)
		return (1);

	sc = clknode_get_softc(clk);

	sc->muxdiv_offset = clkdef->muxdiv_offset;

	sc->mux_shift = clkdef->mux_shift;
	sc->mux_width = clkdef->mux_width;
	sc->mux_mask = ((1 << clkdef->mux_width) - 1) << sc->mux_shift;

	sc->div_shift = clkdef->div_shift;
	sc->div_width = clkdef->div_width;
	sc->div_mask = ((1 << clkdef->div_width) - 1) << sc->div_shift;

	sc->flags = clkdef->flags;

	sc->main_parent = clkdef->main_parent;
	sc->alt_parent = clkdef->alt_parent;

	sc->rates = clkdef->rates;
	sc->nrates = clkdef->nrates;

	clknode_register(clkdom, clk);

	return (0);
}
