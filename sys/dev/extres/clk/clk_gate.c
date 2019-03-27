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

#include <dev/extres/clk/clk_gate.h>

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

static int clknode_gate_init(struct clknode *clk, device_t dev);
static int clknode_gate_set_gate(struct clknode *clk, bool enable);
struct clknode_gate_sc {
	uint32_t	offset;
	uint32_t	shift;
	uint32_t	mask;
	uint32_t	on_value;
	uint32_t	off_value;
	int		gate_flags;
	bool		ungated;
};

static clknode_method_t clknode_gate_methods[] = {
	/* Device interface */
	CLKNODEMETHOD(clknode_init,	clknode_gate_init),
	CLKNODEMETHOD(clknode_set_gate,	clknode_gate_set_gate),
	CLKNODEMETHOD_END
};
DEFINE_CLASS_1(clknode_gate, clknode_gate_class, clknode_gate_methods,
   sizeof(struct clknode_gate_sc), clknode_class);

static int
clknode_gate_init(struct clknode *clk, device_t dev)
{
	uint32_t reg;
	struct clknode_gate_sc *sc;
	int rv;

	sc = clknode_get_softc(clk);
	DEVICE_LOCK(clk);
	rv = RD4(clk, sc->offset, &reg);
	DEVICE_UNLOCK(clk);
	if (rv != 0)
		return (rv);
	reg = (reg >> sc->shift) & sc->mask;
	sc->ungated = reg == sc->on_value ? 1 : 0;
	clknode_init_parent_idx(clk, 0);
	return(0);
}

static int
clknode_gate_set_gate(struct clknode *clk, bool enable)
{
	uint32_t reg;
	struct clknode_gate_sc *sc;
	int rv;

	sc = clknode_get_softc(clk);
	sc->ungated = enable;
	DEVICE_LOCK(clk);
	rv = MD4(clk, sc->offset, sc->mask << sc->shift,
	    (sc->ungated ? sc->on_value : sc->off_value) << sc->shift);
	if (rv != 0) {
		DEVICE_UNLOCK(clk);
		return (rv);
	}
	RD4(clk, sc->offset, &reg);
	DEVICE_UNLOCK(clk);
	return(0);
}

int
clknode_gate_register(struct clkdom *clkdom, struct clk_gate_def *clkdef)
{
	struct clknode *clk;
	struct clknode_gate_sc *sc;

	clk = clknode_create(clkdom, &clknode_gate_class, &clkdef->clkdef);
	if (clk == NULL)
		return (1);

	sc = clknode_get_softc(clk);
	sc->offset = clkdef->offset;
	sc->shift = clkdef->shift;
	sc->mask =  clkdef->mask;
	sc->on_value = clkdef->on_value;
	sc->off_value = clkdef->off_value;
	sc->gate_flags = clkdef->gate_flags;

	clknode_register(clkdom, clk);
	return (0);
}
