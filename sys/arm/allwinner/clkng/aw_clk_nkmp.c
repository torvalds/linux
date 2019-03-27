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
#include <arm/allwinner/clkng/aw_clk_nkmp.h>

#include "clkdev_if.h"

/*
 * clknode for clocks matching the formula :
 *
 * clk = (clkin * n * k) / (m * p)
 *
 */

struct aw_clk_nkmp_sc {
	uint32_t	offset;

	struct aw_clk_factor	n;
	struct aw_clk_factor	k;
	struct aw_clk_factor	m;
	struct aw_clk_factor	p;

	uint32_t	mux_shift;
	uint32_t	mux_mask;
	uint32_t	gate_shift;
	uint32_t	lock_shift;
	uint32_t	lock_retries;
	uint32_t	update_shift;

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
aw_clk_nkmp_init(struct clknode *clk, device_t dev)
{
	struct aw_clk_nkmp_sc *sc;
	uint32_t val, idx;

	sc = clknode_get_softc(clk);

	idx = 0;
	if ((sc->flags & AW_CLK_HAS_MUX) != 0) {
		DEVICE_LOCK(clk);
		READ4(clk, sc->offset, &val);
		DEVICE_UNLOCK(clk);

		idx = (val & sc->mux_mask) >> sc->mux_shift;
	}

	clknode_init_parent_idx(clk, idx);
	return (0);
}

static int
aw_clk_nkmp_set_gate(struct clknode *clk, bool enable)
{
	struct aw_clk_nkmp_sc *sc;
	uint32_t val;

	sc = clknode_get_softc(clk);

	if ((sc->flags & AW_CLK_HAS_GATE) == 0)
		return (0);

	DEVICE_LOCK(clk);
	READ4(clk, sc->offset, &val);
	if (enable)
		val |= (1 << sc->gate_shift);
	else
		val &= ~(1 << sc->gate_shift);
	WRITE4(clk, sc->offset, val);
	DEVICE_UNLOCK(clk);

	return (0);
}

static int
aw_clk_nkmp_set_mux(struct clknode *clk, int index)
{
	struct aw_clk_nkmp_sc *sc;
	uint32_t val;

	sc = clknode_get_softc(clk);

	if ((sc->flags & AW_CLK_HAS_MUX) == 0)
		return (0);

	DEVICE_LOCK(clk);
	READ4(clk, sc->offset, &val);
	val &= ~sc->mux_mask;
	val |= index << sc->mux_shift;
	WRITE4(clk, sc->offset, val);
	DEVICE_UNLOCK(clk);

	return (0);
}

static uint64_t
aw_clk_nkmp_find_best(struct aw_clk_nkmp_sc *sc, uint64_t fparent, uint64_t *fout,
    uint32_t *factor_n, uint32_t *factor_k, uint32_t *factor_m, uint32_t *factor_p)
{
	uint64_t cur, best;
	uint32_t n, k, m, p;

	best = 0;
	*factor_n = 0;
	*factor_k = 0;
	*factor_m = 0;
	*factor_p = 0;

	for (n = aw_clk_factor_get_min(&sc->n); n <= aw_clk_factor_get_max(&sc->n); ) {
		for (k = aw_clk_factor_get_min(&sc->k); k <= aw_clk_factor_get_max(&sc->k); ) {
			for (m = aw_clk_factor_get_min(&sc->m); m <= aw_clk_factor_get_max(&sc->m); ) {
				for (p = aw_clk_factor_get_min(&sc->p); p <= aw_clk_factor_get_max(&sc->p); ) {
					cur = (fparent * n * k) / (m * p);
					if ((*fout - cur) < (*fout - best)) {
						best = cur;
						*factor_n = n;
						*factor_k = k;
						*factor_m = m;
						*factor_p = p;
					}
					if (best == *fout)
						return (best);
					if ((sc->p.flags & AW_CLK_FACTOR_POWER_OF_TWO) != 0)
						p <<= 1;
					else
						p++;
				}
				if ((sc->m.flags & AW_CLK_FACTOR_POWER_OF_TWO) != 0)
					m <<= 1;
				else
					m++;
			}
			if ((sc->k.flags & AW_CLK_FACTOR_POWER_OF_TWO) != 0)
				k <<= 1;
			else
				k++;
		}
		if ((sc->n.flags & AW_CLK_FACTOR_POWER_OF_TWO) != 0)
			n <<= 1;
		else
			n++;
	}

	return best;
}

static void
aw_clk_nkmp_set_freq_scale(struct clknode *clk, struct aw_clk_nkmp_sc *sc,
    uint32_t factor_n, uint32_t factor_k, uint32_t factor_m, uint32_t factor_p)
{
	uint32_t val, n, k, m, p;
	int retry;

	DEVICE_LOCK(clk);
	READ4(clk, sc->offset, &val);

	n = aw_clk_get_factor(val, &sc->n);
	k = aw_clk_get_factor(val, &sc->k);
	m = aw_clk_get_factor(val, &sc->m);
	p = aw_clk_get_factor(val, &sc->p);

	if (p < factor_p) {
		val &= ~sc->p.mask;
		val |= aw_clk_factor_get_value(&sc->p, factor_p) << sc->p.shift;
		WRITE4(clk, sc->offset, val);
		DELAY(2000);
	}

	if (m < factor_m) {
		val &= ~sc->m.mask;
		val |= aw_clk_factor_get_value(&sc->m, factor_m) << sc->m.shift;
		WRITE4(clk, sc->offset, val);
		DELAY(2000);
	}

	val &= ~sc->n.mask;
	val &= ~sc->k.mask;
	val |= aw_clk_factor_get_value(&sc->n, factor_n) << sc->n.shift;
	val |= aw_clk_factor_get_value(&sc->k, factor_k) << sc->k.shift;
	WRITE4(clk, sc->offset, val);
	DELAY(2000);

	if (m > factor_m) {
		val &= ~sc->m.mask;
		val |= aw_clk_factor_get_value(&sc->m, factor_m) << sc->m.shift;
		WRITE4(clk, sc->offset, val);
		DELAY(2000);
	}

	if (p > factor_p) {
		val &= ~sc->p.mask;
		val |= aw_clk_factor_get_value(&sc->p, factor_p) << sc->p.shift;
		WRITE4(clk, sc->offset, val);
		DELAY(2000);
	}

	if ((sc->flags & AW_CLK_HAS_LOCK) != 0) {
		for (retry = 0; retry < sc->lock_retries; retry++) {
			READ4(clk, sc->offset, &val);
			if ((val & (1 << sc->lock_shift)) != 0)
				break;
			DELAY(1000);
		}
	}

	DEVICE_UNLOCK(clk);
}

static int
aw_clk_nkmp_set_freq(struct clknode *clk, uint64_t fparent, uint64_t *fout,
    int flags, int *stop)
{
	struct aw_clk_nkmp_sc *sc;
	uint64_t best;
	uint32_t val, best_n, best_k, best_m, best_p;
	int retry;

	sc = clknode_get_softc(clk);

	best = aw_clk_nkmp_find_best(sc, fparent, fout,
	    &best_n, &best_k, &best_m, &best_p);
	if ((flags & CLK_SET_DRYRUN) != 0) {
		*fout = best;
		*stop = 1;
		return (0);
	}

	if ((best < *fout) &&
	  ((flags & CLK_SET_ROUND_DOWN) != 0)) {
		*stop = 1;
		return (ERANGE);
	}
	if ((best > *fout) &&
	  ((flags & CLK_SET_ROUND_UP) != 0)) {
		*stop = 1;
		return (ERANGE);
	}

	if ((sc->flags & AW_CLK_SCALE_CHANGE) != 0)
		aw_clk_nkmp_set_freq_scale(clk, sc,
		    best_n, best_k, best_m, best_p);
	else {
		DEVICE_LOCK(clk);
		READ4(clk, sc->offset, &val);
		val &= ~sc->n.mask;
		val &= ~sc->k.mask;
		val &= ~sc->m.mask;
		val &= ~sc->p.mask;
		val |= aw_clk_factor_get_value(&sc->n, best_n) << sc->n.shift;
		val |= aw_clk_factor_get_value(&sc->k, best_k) << sc->k.shift;
		val |= aw_clk_factor_get_value(&sc->m, best_m) << sc->m.shift;
		val |= aw_clk_factor_get_value(&sc->p, best_p) << sc->p.shift;
		WRITE4(clk, sc->offset, val);
		DELAY(2000);
		DEVICE_UNLOCK(clk);

		if ((sc->flags & AW_CLK_HAS_UPDATE) != 0) {
			DEVICE_LOCK(clk);
			READ4(clk, sc->offset, &val);
			val |= 1 << sc->update_shift;
			WRITE4(clk, sc->offset, val);
			DELAY(2000);
			DEVICE_UNLOCK(clk);
		}

		if ((sc->flags & AW_CLK_HAS_LOCK) != 0) {
			for (retry = 0; retry < sc->lock_retries; retry++) {
				READ4(clk, sc->offset, &val);
				if ((val & (1 << sc->lock_shift)) != 0)
					break;
				DELAY(1000);
			}
		}
	}

	*fout = best;
	*stop = 1;

	return (0);
}

static int
aw_clk_nkmp_recalc(struct clknode *clk, uint64_t *freq)
{
	struct aw_clk_nkmp_sc *sc;
	uint32_t val, m, n, k, p;

	sc = clknode_get_softc(clk);

	DEVICE_LOCK(clk);
	READ4(clk, sc->offset, &val);
	DEVICE_UNLOCK(clk);

	n = aw_clk_get_factor(val, &sc->n);
	k = aw_clk_get_factor(val, &sc->k);
	m = aw_clk_get_factor(val, &sc->m);
	p = aw_clk_get_factor(val, &sc->p);

	*freq = (*freq * n * k) / (m * p);

	return (0);
}

static clknode_method_t aw_nkmp_clknode_methods[] = {
	/* Device interface */
	CLKNODEMETHOD(clknode_init,		aw_clk_nkmp_init),
	CLKNODEMETHOD(clknode_set_gate,		aw_clk_nkmp_set_gate),
	CLKNODEMETHOD(clknode_set_mux,		aw_clk_nkmp_set_mux),
	CLKNODEMETHOD(clknode_recalc_freq,	aw_clk_nkmp_recalc),
	CLKNODEMETHOD(clknode_set_freq,		aw_clk_nkmp_set_freq),
	CLKNODEMETHOD_END
};

DEFINE_CLASS_1(aw_nkmp_clknode, aw_nkmp_clknode_class, aw_nkmp_clknode_methods,
    sizeof(struct aw_clk_nkmp_sc), clknode_class);

int
aw_clk_nkmp_register(struct clkdom *clkdom, struct aw_clk_nkmp_def *clkdef)
{
	struct clknode *clk;
	struct aw_clk_nkmp_sc *sc;

	clk = clknode_create(clkdom, &aw_nkmp_clknode_class, &clkdef->clkdef);
	if (clk == NULL)
		return (1);

	sc = clknode_get_softc(clk);

	sc->offset = clkdef->offset;

	sc->n.shift = clkdef->n.shift;
	sc->n.width = clkdef->n.width;
	sc->n.mask = ((1 << clkdef->n.width) - 1) << sc->n.shift;
	sc->n.value = clkdef->n.value;
	sc->n.flags = clkdef->n.flags;

	sc->k.shift = clkdef->k.shift;
	sc->k.width = clkdef->k.width;
	sc->k.mask = ((1 << clkdef->k.width) - 1) << sc->k.shift;
	sc->k.value = clkdef->k.value;
	sc->k.flags = clkdef->k.flags;

	sc->m.shift = clkdef->m.shift;
	sc->m.width = clkdef->m.width;
	sc->m.mask = ((1 << clkdef->m.width) - 1) << sc->m.shift;
	sc->m.value = clkdef->m.value;
	sc->m.flags = clkdef->m.flags;

	sc->p.shift = clkdef->p.shift;
	sc->p.width = clkdef->p.width;
	sc->p.mask = ((1 << clkdef->p.width) - 1) << sc->p.shift;
	sc->p.value = clkdef->p.value;
	sc->p.flags = clkdef->p.flags;

	sc->mux_shift = clkdef->mux_shift;
	sc->mux_mask = ((1 << clkdef->mux_width) - 1) << sc->mux_shift;

	sc->gate_shift = clkdef->gate_shift;
	sc->lock_shift = clkdef->lock_shift;
	sc->lock_retries = clkdef->lock_retries;
	sc->update_shift = clkdef->update_shift;
	sc->flags = clkdef->flags;

	clknode_register(clkdom, clk);

	return (0);
}
