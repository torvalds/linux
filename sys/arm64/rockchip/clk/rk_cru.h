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

#ifndef __RK_CRU_H__
#define __RK_CRU_H__

#include <arm64/rockchip/clk/rk_clk_armclk.h>
#include <arm64/rockchip/clk/rk_clk_composite.h>
#include <arm64/rockchip/clk/rk_clk_gate.h>
#include <arm64/rockchip/clk/rk_clk_mux.h>
#include <arm64/rockchip/clk/rk_clk_pll.h>

struct rk_cru_reset {
	uint32_t	offset;
	uint32_t	shift;
};

struct rk_cru_gate {
	const char	*name;
	const char	*parent_name;
	uint32_t	id;
	uint32_t	offset;
	uint32_t	shift;
};

#define	CRU_GATE(idx, clkname, pname, o, s)	\
	{				\
		.id = idx,			\
		.name = clkname,		\
		.parent_name = pname,		\
		.offset = o,			\
		.shift = s,			\
	},

enum rk_clk_type {
	RK_CLK_UNDEFINED = 0,
	RK3328_CLK_PLL,
	RK3399_CLK_PLL,
	RK_CLK_COMPOSITE,
	RK_CLK_MUX,
	RK_CLK_ARMCLK,
};

struct rk_clk {
	enum rk_clk_type	type;
	union {
		struct rk_clk_pll_def		*pll;
		struct rk_clk_composite_def	*composite;
		struct rk_clk_mux_def		*mux;
		struct rk_clk_armclk_def	*armclk;
	} clk;
};

struct rk_cru_softc {
	device_t		dev;
	struct resource		*res;
	struct clkdom		*clkdom;
	struct mtx		mtx;
	int			type;
	struct rk_cru_reset	*resets;
	int			nresets;
	struct rk_cru_gate	*gates;
	int			ngates;
	struct rk_clk		*clks;
	int			nclks;
	struct rk_clk_armclk_def	*armclk;
	struct rk_clk_armclk_rates	*armclk_rates;
	int			narmclk_rates;
};

DECLARE_CLASS(rk_cru_driver);

int	rk_cru_attach(device_t dev);

#endif /* __RK_CRU_H__ */
