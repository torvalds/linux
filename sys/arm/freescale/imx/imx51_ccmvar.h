/*	$NetBSD: imx51_ccmvar.h,v 1.1 2012/04/17 09:33:31 bsh Exp $	*/
/*-
 * SPDX-License-Identifier: BSD-2-Clause AND BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012  Genetec Corporation.  All rights reserved.
 * Written by Hashimoto Kenichi for Genetec Corporation.
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
 * THIS SOFTWARE IS PROVIDED BY GENETEC CORPORATION ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL GENETEC CORPORATION
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*-
 * Copyright (c) 2012, 2013 The FreeBSD Foundation
 * All rights reserved.
 *
 * Portions of this software were developed by Oleksandr Rybalko
 * under sponsorship from the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1.	Redistributions of source code must retain the above copyright
 *	notice, this list of conditions and the following disclaimer.
 * 2.	Redistributions in binary form must reproduce the above copyright
 *	notice, this list of conditions and the following disclaimer in the
 *	documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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

#ifndef	_ARM_IMX_IMX51_CCMVAR_H_
#define	_ARM_IMX_IMX51_CCMVAR_H_

enum imx51_clock {
	IMX51CLK_FPM,
	IMX51CLK_PLL1,
	IMX51CLK_PLL2,
	IMX51CLK_PLL3,
	IMX51CLK_PLL1SW,
	IMX51CLK_PLL2SW,
	IMX51CLK_PLL3SW,
	IMX51CLK_PLL1STEP,
	IMX51CLK_LP_APM,
	IMX51CLK_ARM_ROOT,
	IMX51CLK_MAIN_BUS_CLK_SRC,	/* XXX */
	IMX51CLK_MAIN_BUS_CLK,
	IMX51CLK_EMI_SLOW_CLK_ROOT,
	IMX51CLK_ENFC_CLK_ROOT,
	IMX51CLK_AHB_CLK_ROOT,
	IMX51CLK_IPG_CLK_ROOT,
	IMX51CLK_PERCLK_ROOT,
	IMX51CLK_DDR_CLK_ROOT,
	IMX51CLK_ARM_AXI_CLK_ROOT,
	IMX51CLK_ARM_AXI_A_CLK,
	IMX51CLK_ARM_AXI_B_CLK,
	IMX51CLK_IPU_HSP_CLK_ROOT,
	IMX51CLK_CKIL_SYNC_CLK_ROOT,
	IMX51CLK_USBOH3_CLK_ROOT,
	IMX51CLK_ESDHC1_CLK_ROOT,
	IMX51CLK_ESDHC2_CLK_ROOT,
	IMX51CLK_ESDHC3_CLK_ROOT,
	IMX51CLK_UART_CLK_ROOT,
	IMX51CLK_SSI1_CLK_ROOT,
	IMX51CLK_SSI2_CLK_ROOT,
	IMX51CLK_SSI_EXT1_CLK_ROOT,
	IMX51CLK_SSI_EXT2_CLK_ROOT,
	IMX51CLK_USB_PHY_CLK_ROOT,
	IMX51CLK_TVE_216_54_CLK_ROOT,
	IMX51CLK_DI_CLK_ROOT,
	IMX51CLK_SPDIF0_CLK_ROOT,
	IMX51CLK_SPDIF1_CLK_ROOT,
	IMX51CLK_CSPI_CLK_ROOT,
	IMX51CLK_WRCK_CLK_ROOT,
	IMX51CLK_LPSR_CLK_ROOT,
	IMX51CLK_PGC_CLK_ROOT
};

u_int imx51_get_clock(enum imx51_clock);
void imx51_clk_gating(int, int);
int imx51_get_clk_gating(int);

#endif	/* _ARM_IMX_IMX51_CCMVAR_H_ */
