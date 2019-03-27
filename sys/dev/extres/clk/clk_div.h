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
 *
 * $FreeBSD$
 */

#ifndef _DEV_EXTRES_CLK_DIV_H_
#define _DEV_EXTRES_CLK_DIV_H_

#include <dev/extres/clk/clk.h>

#define	CLK_DIV_ZERO_BASED	0x0001 /* Zero based divider. */
#define	CLK_DIV_WITH_TABLE	0x0002 /* Table to lookup the real value */

struct clk_div_table {
	uint32_t	value;
	uint32_t	divider;
};

struct clk_div_def {
	struct clknode_init_def clkdef;
	uint32_t		offset;		/* Divider register offset */
	uint32_t		i_shift;	/* Pos of div bits in reg */
	uint32_t		i_width;	/* Width of div bit field */
	uint32_t		f_shift;	/* Fractional divide bits, */
	uint32_t		f_width;	/* set to 0 for int divider */
	int			div_flags;	/* Divider-specific flags */
	struct clk_div_table	*div_table;	/* Divider table */
};

int clknode_div_register(struct clkdom *clkdom, struct clk_div_def *clkdef);

#endif /*_DEV_EXTRES_CLK_DIV_H_*/
