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

#ifndef	__AW_CLK_NKMP_H__
#define __AW_CLK_NKMP_H__

#include <arm/allwinner/clkng/aw_clk.h>

struct aw_clk_nkmp_def {
	struct clknode_init_def clkdef;

	uint32_t		offset;

	struct aw_clk_factor	m;
	struct aw_clk_factor	k;
	struct aw_clk_factor	n;
	struct aw_clk_factor	p;

	uint32_t		mux_shift;
	uint32_t		mux_width;
	uint32_t		gate_shift;
	uint32_t		lock_shift;
	uint32_t		lock_retries;
	uint32_t		update_shift;

	uint32_t		flags;
};

int	aw_clk_nkmp_register(struct clkdom *clkdom, struct aw_clk_nkmp_def *clkdef);

#endif /* __AW_CLK_NKMP_H__ */
