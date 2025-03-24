// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2021-2 ARM Limited.
// Original author: Mark Brown <broonie@kernel.org>

#ifndef SME_INST_H
#define SME_INST_H

#define REG_FPMR                                        S3_3_C4_C4_2

/*
 * RDSVL X\nx, #\imm
 */
.macro rdsvl nx, imm
	.inst	0x4bf5800			\
		| (\imm << 5)			\
		| (\nx)
.endm

.macro smstop
	msr	S0_3_C4_C6_3, xzr
.endm

.macro smstart_za
	msr	S0_3_C4_C5_3, xzr
.endm

.macro smstart_sm
	msr	S0_3_C4_C3_3, xzr
.endm

/*
 * LDR (vector to ZA array):
 *	LDR ZA[\nw, #\offset], [X\nxbase, #\offset, MUL VL]
 */
.macro _ldr_za nw, nxbase, offset=0
	.inst	0xe1000000			\
		| (((\nw) & 3) << 13)		\
		| ((\nxbase) << 5)		\
		| ((\offset) & 7)
.endm

/*
 * STR (vector from ZA array):
 *	STR ZA[\nw, #\offset], [X\nxbase, #\offset, MUL VL]
 */
.macro _str_za nw, nxbase, offset=0
	.inst	0xe1200000			\
		| (((\nw) & 3) << 13)		\
		| ((\nxbase) << 5)		\
		| ((\offset) & 7)
.endm

/*
 * LDR (ZT0)
 *
 *	LDR ZT0, nx
 */
.macro _ldr_zt nx
	.inst	0xe11f8000			\
		| (((\nx) & 0x1f) << 5)
.endm

/*
 * STR (ZT0)
 *
 *	STR ZT0, nx
 */
.macro _str_zt nx
	.inst	0xe13f8000			\
		| (((\nx) & 0x1f) << 5)
.endm

#endif
