/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2018 Rubicon Communications, LLC (Netgate)
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

#ifndef _MV_CP110_SYSCON_H_
#define	_MV_CP110_SYSCON_H_

enum mv_cp110_clk_id {
	CP110_PLL_0 = 0,
	CP110_PPV2_CORE,
	CP110_X2CORE,
	CP110_CORE,
	CP110_NAND,
	CP110_SDIO,
	CP110_MAX_CLOCK
};

/* Gates */
#define	CP110_CLOCK_GATING_OFFSET	0x220

struct cp110_gate {
	const char	*name;
	uint32_t	shift;
};

#define	CCU_GATE(idx, clkname, s)		\
	[idx] = {					\
		.name = clkname,			\
		.shift = s,				\
	},

#define	CP110_GATE_AUDIO		0
#define	CP110_GATE_COMM_UNIT		1
#define	CP110_GATE_NAND			2
#define	CP110_GATE_PPV2			3
#define	CP110_GATE_SDIO			4
#define	CP110_GATE_MG			5
#define	CP110_GATE_MG_CORE		6
#define	CP110_GATE_XOR1			7
#define	CP110_GATE_XOR0			8
#define	CP110_GATE_GOP_DP		9
#define	CP110_GATE_PCIE_X1_0		11
#define	CP110_GATE_PCIE_X1_1		12
#define	CP110_GATE_PCIE_X4		13
#define	CP110_GATE_PCIE_XOR		14
#define	CP110_GATE_SATA			15
#define	CP110_GATE_SATA_USB		16
#define	CP110_GATE_MAIN			17
#define	CP110_GATE_SDMMC_GOP		18
#define	CP110_GATE_SLOW_IO		21
#define	CP110_GATE_USB3H0		22
#define	CP110_GATE_USB3H1		23
#define	CP110_GATE_USB3DEV		24
#define	CP110_GATE_EIP150		25
#define	CP110_GATE_EIP197		26

#endif
