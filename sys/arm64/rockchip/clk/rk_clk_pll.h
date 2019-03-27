/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright 2018 Emmanuel Vadot <manu@FreeBSD.org>
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
 *
 * $FreeBSD$
 */

#ifndef _RK_CLK_PLL_H_
#define _RK_CLK_PLL_H_

#include <dev/extres/clk/clk.h>

struct rk_clk_pll_rate {
	uint32_t	freq;
	uint32_t	refdiv;
	uint32_t	fbdiv;
	uint32_t	postdiv1;
	uint32_t	postdiv2;
	uint32_t	dsmpd;
	uint32_t	frac;
};

struct rk_clk_pll_def {
	struct clknode_init_def	clkdef;
	uint32_t		base_offset;

	uint32_t		gate_offset;
	uint32_t		gate_shift;

	uint32_t		mode_reg;
	uint32_t		mode_shift;

	uint32_t		flags;

	struct rk_clk_pll_rate	*rates;
	struct rk_clk_pll_rate	*frac_rates;
};

#define	RK_CLK_PLL_HAVE_GATE	0x1

int rk3328_clk_pll_register(struct clkdom *clkdom, struct rk_clk_pll_def *clkdef);
int rk3399_clk_pll_register(struct clkdom *clkdom, struct rk_clk_pll_def *clkdef);

#endif /* _RK_CLK_PLL_H_ */
