/*-
 * Copyright 2016 Michal Meloun <mmel@FreeBSD.org>
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
#include <sys/conf.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/systm.h>

#include <machine/bus.h>

#include <dev/extres/clk/clk_div.h>

#include "clkdev_if.h"

#define	WR4(_clk, off, val)						\
	CLKDEV_WRITE_4(clknode_get_device(_clk), off, val)
#define	RD4(_clk, off, val)						\
	CLKDEV_READ_4(clknode_get_device(_clk), off, val)
#define	MD4(_clk, off, clr, set )					\
	CLKDEV_MODIFY_4(clknode_get_device(_clk), off, clr, set)
#define	DEVICE_LOCK(_clk)							\
	CLKDEV_DEVICE_LOCK(clknode_get_device(_clk))
#define	DEVICE_UNLOCK(_clk)						\
	CLKDEV_DEVICE_UNLOCK(clknode_get_device(_clk))

static int clknode_div_init(struct clknode *clk, device_t dev);
static int clknode_div_recalc(struct clknode *clk, uint64_t *req);
static int clknode_div_set_freq(struct clknode *clknode, uint64_t fin,
    uint64_t *fout, int flag, int *stop);

struct clknode_div_sc {
	struct mtx	*mtx;
	struct resource *mem_res;
	uint32_t	offset;
	uint32_t	i_shift;
	uint32_t	i_mask;
	uint32_t	i_width;
	uint32_t	f_shift;
	uint32_t	f_mask;
	uint32_t	f_width;
	int		div_flags;
	uint32_t	divider;	/* in natural form */

	struct clk_div_table	*div_table;
};

static clknode_method_t clknode_div_methods[] = {
	/* Device interface */
	CLKNODEMETHOD(clknode_init,		clknode_div_init),
	CLKNODEMETHOD(clknode_recalc_freq,	clknode_div_recalc),
	CLKNODEMETHOD(clknode_set_freq,		clknode_div_set_freq),
	CLKNODEMETHOD_END
};
DEFINE_CLASS_1(clknode_div, clknode_div_class, clknode_div_methods,
   sizeof(struct clknode_div_sc), clknode_class);

static uint32_t
clknode_div_table_get_divider(struct clknode_div_sc *sc, uint32_t divider)
{
	struct clk_div_table *table;

	if (!(sc->div_flags & CLK_DIV_WITH_TABLE))
		return (divider);

	for (table = sc->div_table; table->divider != 0; table++)
		if (table->value == sc->divider)
			return (table->divider);

	return (0);
}

static int
clknode_div_table_get_value(struct clknode_div_sc *sc, uint32_t *divider)
{
	struct clk_div_table *table;

	if (!(sc->div_flags & CLK_DIV_WITH_TABLE))
		return (0);

	for (table = sc->div_table; table->divider != 0; table++)
		if (table->divider == *divider) {
			*divider = table->value;
			return (0);
		}

	return (ENOENT);
}

static int
clknode_div_init(struct clknode *clk, device_t dev)
{
	uint32_t reg;
	struct clknode_div_sc *sc;
	uint32_t i_div, f_div;
	int rv;

	sc = clknode_get_softc(clk);

	DEVICE_LOCK(clk);
	rv = RD4(clk, sc->offset, &reg);
	DEVICE_UNLOCK(clk);
	if (rv != 0)
		return (rv);

	i_div = (reg >> sc->i_shift) & sc->i_mask;
	if (!(sc->div_flags & CLK_DIV_ZERO_BASED))
		i_div++;
	f_div = (reg >> sc->f_shift) & sc->f_mask;
	sc->divider = i_div << sc->f_width | f_div;

	sc->divider = clknode_div_table_get_divider(sc, sc->divider);
	if (sc->divider == 0)
		panic("%s: divider is zero!\n", clknode_get_name(clk));

	clknode_init_parent_idx(clk, 0);
	return(0);
}

static int
clknode_div_recalc(struct clknode *clk, uint64_t *freq)
{
	struct clknode_div_sc *sc;

	sc = clknode_get_softc(clk);
	if (sc->divider == 0) {
		printf("%s: %s divider is zero!\n", clknode_get_name(clk),
		__func__);
		*freq = 0;
		return(EINVAL);
	}
	*freq = (*freq << sc->f_width) / sc->divider;
	return (0);
}

static int
clknode_div_set_freq(struct clknode *clk, uint64_t fin, uint64_t *fout,
  int flags, int *stop)
{
	struct clknode_div_sc *sc;
	uint64_t divider, _fin, _fout;
	uint32_t div_value, reg, i_div, f_div, hw_i_div;
	int rv;

	sc = clknode_get_softc(clk);

	/* For fractional divider. */
	_fin = fin << sc->f_width;
	divider = (_fin + *fout / 2) / *fout;
	_fout = _fin / divider;

	/* Rounding. */
	if ((flags & CLK_SET_ROUND_UP) && (*fout < _fout))
		divider--;
	else if ((flags & CLK_SET_ROUND_DOWN) && (*fout > _fout))
		divider++;

	/* Break divider into integer and fractional parts. */
	i_div = divider >> sc->f_width;
	f_div = divider  & sc->f_mask;

	if (i_div == 0) {
		printf("%s: %s integer divider is zero!\n",
		     clknode_get_name(clk), __func__);
		return(EINVAL);
	}

	hw_i_div = i_div;
	if (!(sc->div_flags & CLK_DIV_ZERO_BASED))
		hw_i_div--;

	*stop = 1;
	if (hw_i_div > sc->i_mask &&
	    ((sc->div_flags & CLK_DIV_WITH_TABLE) == 0)) {
		/* XXX Or only return error? */
		printf("%s: %s integer divider is too big: %u\n",
		    clknode_get_name(clk), __func__, hw_i_div);
		hw_i_div = sc->i_mask;
		*stop = 0;
	}

	i_div = hw_i_div;
	if (!(sc->div_flags & CLK_DIV_ZERO_BASED))
		i_div++;
	divider = i_div << sc->f_width | f_div;

	if ((flags & CLK_SET_DRYRUN) == 0) {
		if ((*stop != 0) &&
		    ((flags & (CLK_SET_ROUND_UP | CLK_SET_ROUND_DOWN)) == 0) &&
		    (*fout != (_fin / divider)))
			return (ERANGE);

		div_value = divider;
		if (clknode_div_table_get_value(sc, &div_value) != 0)
			return (ERANGE);
		if (div_value != divider)
			i_div = div_value;

		DEVICE_LOCK(clk);
		rv = MD4(clk, sc->offset,
		    (sc->i_mask << sc->i_shift) | (sc->f_mask << sc->f_shift),
		    (i_div << sc->i_shift) | (f_div << sc->f_shift));
		if (rv != 0) {
			DEVICE_UNLOCK(clk);
			return (rv);
		}
		RD4(clk, sc->offset, &reg);
		DEVICE_UNLOCK(clk);

		sc->divider = divider;
	}

	*fout = _fin / divider;
	return (0);
}

int
clknode_div_register(struct clkdom *clkdom, struct clk_div_def *clkdef)
{
	struct clknode *clk;
	struct clknode_div_sc *sc;

	clk = clknode_create(clkdom, &clknode_div_class, &clkdef->clkdef);
	if (clk == NULL)
		return (1);

	sc = clknode_get_softc(clk);
	sc->offset = clkdef->offset;
	sc->i_shift = clkdef->i_shift;
	sc->i_width = clkdef->i_width;
	sc->i_mask = (1 << clkdef->i_width) - 1;
	sc->f_shift = clkdef->f_shift;
	sc->f_width = clkdef->f_width;
	sc->f_mask = (1 << clkdef->f_width) - 1;
	sc->div_flags = clkdef->div_flags;
	sc->div_table = clkdef->div_table;

	clknode_register(clkdom, clk);
	return (0);
}
