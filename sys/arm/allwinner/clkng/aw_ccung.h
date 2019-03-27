/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2017,2018 Emmanuel Vadot <manu@freebsd.org>
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

#ifndef __CCU_NG_H__
#define __CCU_NG_H__

#include <arm/allwinner/clkng/aw_clk.h>
#include <arm/allwinner/clkng/aw_clk_nkmp.h>
#include <arm/allwinner/clkng/aw_clk_nm.h>
#include <arm/allwinner/clkng/aw_clk_prediv_mux.h>
#include <dev/extres/clk/clk_mux.h>
#include <dev/extres/clk/clk_div.h>
#include <dev/extres/clk/clk_fixed.h>

enum aw_ccung_clk_type {
	AW_CLK_UNDEFINED = 0,
	AW_CLK_MUX,
	AW_CLK_DIV,
	AW_CLK_FIXED,
	AW_CLK_NKMP,
	AW_CLK_NM,
	AW_CLK_PREDIV_MUX,
};

struct aw_ccung_clk {
	enum aw_ccung_clk_type	type;
	union {
		struct clk_mux_def		*mux;
		struct clk_div_def		*div;
		struct clk_fixed_def		*fixed;
		struct aw_clk_nkmp_def		*nkmp;
		struct aw_clk_nm_def		*nm;
		struct aw_clk_prediv_mux_def	*prediv_mux;
	} clk;
};

struct aw_ccung_softc {
	device_t		dev;
	struct resource		*res;
	struct clkdom		*clkdom;
	struct mtx		mtx;
	struct aw_ccung_reset	*resets;
	int			nresets;
	struct aw_ccung_gate	*gates;
	int			ngates;
	struct aw_ccung_clk	*clks;
	int			nclks;
	struct aw_clk_init	*clk_init;
	int			n_clk_init;
};

struct aw_ccung_reset {
	uint32_t	offset;
	uint32_t	shift;
};

struct aw_ccung_gate {
	const char	*name;
	const char	*parent_name;
	uint32_t	id;
	uint32_t	offset;
	uint32_t	shift;
};

DECLARE_CLASS(aw_ccung_driver);

int aw_ccung_attach(device_t dev);

#endif /* __CCU_NG_H__ */
