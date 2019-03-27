/*-
 * Copyright (c) 2017 Emmanuel Vadot <manu@freebsd.org>
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

#include <arm/allwinner/clkng/aw_clk.h>
#include <arm/allwinner/clkng/aw_clk_prediv_mux.h>

#include "clkdev_if.h"

/*
 * clknode for clocks matching the formula :
 *
 * clk = clkin / prediv / div
 *
 * and where prediv is conditional
 *
 */

struct aw_clk_prediv_mux_sc {
	uint32_t	offset;

	uint32_t		mux_shift;
	uint32_t		mux_mask;

	struct aw_clk_factor	div;
	struct aw_clk_factor	prediv;

	uint32_t	flags;
};

#define	WRITE4(_clk, off, val)						\
	CLKDEV_WRITE_4(clknode_get_device(_clk), off, val)
#define	READ4(_clk, off, val)						\
	CLKDEV_READ_4(clknode_get_device(_clk), off, val)
#define	MODIFY4(_clk, off, clr, set )					\
	CLKDEV_MODIFY_4(clknode_get_device(_clk), off, clr, set)
#define	DEVICE_LOCK(_clk)							\
	CLKDEV_DEVICE_LOCK(clknode_get_device(_clk))
#define	DEVICE_UNLOCK(_clk)						\
	CLKDEV_DEVICE_UNLOCK(clknode_get_device(_clk))

static int
aw_clk_prediv_mux_init(struct clknode *clk, device_t dev)
{
	clknode_init_parent_idx(clk, 0);
	return (0);
}

static int
aw_clk_prediv_mux_set_mux(struct clknode *clk, int index)
{
	struct aw_clk_prediv_mux_sc *sc;
	uint32_t val;

	sc = clknode_get_softc(clk);

	DEVICE_LOCK(clk);
	READ4(clk, sc->offset, &val);
	val &= ~sc->mux_mask;
	val |= index << sc->mux_shift;
	WRITE4(clk, sc->offset, val);
	DEVICE_UNLOCK(clk);

	return (0);
}

static int
aw_clk_prediv_mux_recalc(struct clknode *clk, uint64_t *freq)
{
	struct aw_clk_prediv_mux_sc *sc;
	uint32_t val, div, prediv;

	sc = clknode_get_softc(clk);

	DEVICE_LOCK(clk);
	READ4(clk, sc->offset, &val);
	DEVICE_UNLOCK(clk);

	div = aw_clk_get_factor(val, &sc->div);
	prediv = aw_clk_get_factor(val, &sc->prediv);

	*freq = *freq / prediv / div;
	return (0);
}

static clknode_method_t aw_prediv_mux_clknode_methods[] = {
	/* Device interface */
	CLKNODEMETHOD(clknode_init,		aw_clk_prediv_mux_init),
	CLKNODEMETHOD(clknode_set_mux,		aw_clk_prediv_mux_set_mux),
	CLKNODEMETHOD(clknode_recalc_freq,	aw_clk_prediv_mux_recalc),
	CLKNODEMETHOD_END
};

DEFINE_CLASS_1(aw_prediv_mux_clknode, aw_prediv_mux_clknode_class,
    aw_prediv_mux_clknode_methods, sizeof(struct aw_clk_prediv_mux_sc),
    clknode_class);

int
aw_clk_prediv_mux_register(struct clkdom *clkdom, struct aw_clk_prediv_mux_def *clkdef)
{
	struct clknode *clk;
	struct aw_clk_prediv_mux_sc *sc;

	clk = clknode_create(clkdom, &aw_prediv_mux_clknode_class, &clkdef->clkdef);
	if (clk == NULL)
		return (1);

	sc = clknode_get_softc(clk);

	sc->offset = clkdef->offset;

	sc->mux_shift = clkdef->mux_shift;
	sc->mux_mask = ((1 << clkdef->mux_width) - 1) << sc->mux_shift;

	sc->div.shift = clkdef->div.shift;
	sc->div.mask = ((1 << clkdef->div.width) - 1) << sc->div.shift;
	sc->div.value = clkdef->div.value;
	sc->div.cond_shift = clkdef->div.cond_shift;
	sc->div.cond_mask = ((1 << clkdef->div.cond_width) - 1) << sc->div.shift;
	sc->div.cond_value = clkdef->div.cond_value;
	sc->div.flags = clkdef->div.flags;

	sc->prediv.shift = clkdef->prediv.shift;
	sc->prediv.mask = ((1 << clkdef->prediv.width) - 1) << sc->prediv.shift;
	sc->prediv.value = clkdef->prediv.value;
	sc->prediv.cond_shift = clkdef->prediv.cond_shift;
	if (clkdef->prediv.cond_width != 0)
		sc->prediv.cond_mask = ((1 << clkdef->prediv.cond_width) - 1) << sc->prediv.shift;
	else
		sc->prediv.cond_mask = clkdef->prediv.cond_mask;
	sc->prediv.cond_value = clkdef->prediv.cond_value;
	sc->prediv.flags = clkdef->prediv.flags;

	sc->flags = clkdef->flags;

	clknode_register(clkdom, clk);

	return (0);
}
