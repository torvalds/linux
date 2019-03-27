/*	$NetBSD: imx51_dpllreg.h,v 1.1 2012/04/17 09:33:31 bsh Exp $	*/
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

#ifndef	_IMX51_DPLLREG_H
#define	_IMX51_DPLLREG_H

#include <sys/cdefs.h>

/* register offset address */

#define	IMX51_N_DPLLS		3		/* 1..3 */

#define	DPLL_BASE(n)	      	(0x83F80000 + (0x4000 * ((n)-1)))
#define	DPLL_SIZE		0x100

#define	DPLL_DP_CTL		0x0000		/* 0x1223 */
#define	 DP_CTL_LRF		0x00000001
#define	 DP_CTL_BRM		0x00000002
#define	 DP_CTL_PLM		0x00000004
#define	 DP_CTL_RCP		0x00000008
#define	 DP_CTL_RST		0x00000010
#define	 DP_CTL_UPEN		0x00000020
#define	 DP_CTL_PRE		0x00000040
#define	 DP_CTL_HFSM		0x00000080
#define	 DP_CTL_REF_CLK_SEL_MASK	0x00000300
#define	 DP_CTL_REF_CLK_SEL_COSC	0x00000200
#define	 DP_CTL_REF_CLK_SEL_FPM 	0x00000300
#define	 DP_CTL_REF_CLK_DIV	0x00000400
#define	 DP_CTL_DPDCK0_2_EN	0x00001000
#define	 DP_CTL_HIGHCLK_EN	DP_CTL_DPDCK0_2_EN
#define	 DP_CTL_MULCTRL		0x00002000
#define	DPLL_DP_CONFIG		0x0004		/* 2 */
#define	 DPLL_DP_CONFIG_APEN	0x00000002
#define	 DPLL_DP_CONFIG_LDREQ	0x00000001
#define	DPLL_DP_OP		0x0008		/* 0x80 */
#define	 DP_OP_PDF_SHIFT	0
#define	 DP_OP_PDF_MASK		(0xf << DP_OP_PDF_SHIFT)
#define	 DP_OP_MFI_SHIFT	4
#define	 DP_OP_MFI_MASK		(0xf << DP_OP_MFI_SHIFT)
#define	DPLL_DP_MFD		0x000C		/* 2 */
#define	DPLL_DP_MFN		0x0010		/* 1 */
#define	DPLL_DP_MFNMINUS	0x0014		/* 0 */
#define	DPLL_DP_MFNPLUS		0x0018		/* 0 */
#define	DPLL_DP_HFS_OP		0x001C		/* 0x80 */
#define	DPLL_DP_HFS_MFD		0x0020		/* 2 */
#define	DPLL_DP_HFS_MFN		0x0024		/* 1 */
#define	DPLL_DP_TOGC		0x0028		/* 0x20000 */
#define	DPLL_DP_DESTAT		0x002C		/* 1 */

#endif /* _IMX51_DPLLREG_H */
