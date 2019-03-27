/*-
 * Copyright 2015 Alexander Kabaev <kan@FreeBSD.org>
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

#ifndef	_MIPS_INGENIC_JZ4780_CLK_H
#define	_MIPS_INGENIC_JZ4780_CLK_H

#include <dev/extres/clk/clk.h>
#include <dev/extres/clk/clk_gate.h>

/* Convenience bitfiled manipulation macros */
#define REG_MSK(field)			(((1u << field ## _WIDTH) - 1) << field ##_SHIFT)
#define REG_VAL(field, val)		((val) << field ##_SHIFT)
#define REG_CLR(reg, field)		((reg) & ~REG_MSK(field))
#define REG_GET(reg, field)		(((reg) & REG_MSK(field)) >> field ##_SHIFT)
#define REG_SET(reg, field, val)	(REG_CLR(reg, field) | REG_VAL(field, val))

/* Common clock macros */
#define	CLK_LOCK(_sc)	mtx_lock((_sc)->clk_mtx)
#define	CLK_UNLOCK(_sc)	mtx_unlock((_sc)->clk_mtx)

#define CLK_WR_4(_sc, off, val)	bus_write_4((_sc)->clk_res, (off), (val))
#define CLK_RD_4(_sc, off)	bus_read_4((_sc)->clk_res, (off))

struct jz4780_clk_mux_descr {
	uint16_t mux_reg;
	uint16_t mux_shift: 5;
	uint16_t mux_bits:  5;
	uint16_t mux_map:   4; /* Map into mux space */
};

struct jz4780_clk_div_descr {
	uint16_t div_reg;
	uint16_t div_shift:	5;
	uint16_t div_bits:	5;
	uint16_t div_lg:	5;
	int      div_ce_bit:	6; /* -1, if CE bit is not present */
	int      div_st_bit:	6; /* Can be negative */
	int      div_busy_bit:	6; /* Can be negative */
};

struct jz4780_clk_descr {
	uint16_t clk_id:   6;
	uint16_t clk_type: 3;
	int clk_gate_bit:  7;      /* Can be negative */
	struct jz4780_clk_mux_descr  clk_mux;
	struct jz4780_clk_div_descr  clk_div;
	const char  *clk_name;
	const char  *clk_pnames[4];
};

/* clk_type bits */
#define CLK_MASK_GATE	0x01
#define CLK_MASK_DIV	0x02
#define CLK_MASK_MUX	0x04

extern int jz4780_clk_gen_register(struct clkdom *clkdom,
    const struct jz4780_clk_descr *descr, struct mtx *dev_mtx,
    struct resource *mem_res);

extern int jz4780_clk_pll_register(struct clkdom *clkdom,
    struct clknode_init_def *clkdef, struct mtx *dev_mtx,
    struct resource *mem_res, uint32_t mem_reg);

extern int jz4780_clk_otg_register(struct clkdom *clkdom,
    struct clknode_init_def *clkdef, struct mtx *dev_mtx,
    struct resource *mem_res);

#endif /* _MIPS_INGENIC_JZ4780_CLK_PLL_H */
