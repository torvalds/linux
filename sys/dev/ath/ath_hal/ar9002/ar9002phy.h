/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010-2011 Adrian Chadd, Xenion Pty Ltd.
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
#ifndef	__ATH_AR9002PHY_H__
#define	__ATH_AR9002PHY_H__

#define	AR_PHY_TX_PWRCTRL4			0xa264
#define	AR_PHY_TX_PWRCTRL_PD_AVG_VALID		0x00000001
#define	AR_PHY_TX_PWRCTRL_PD_AVG_VALID_S	0
#define	AR_PHY_TX_PWRCTRL_PD_AVG_OUT		0x000001FE
#define	AR_PHY_TX_PWRCTRL_PD_AVG_OUT_S		1

#define	AR_PHY_TX_PWRCTRL6_0			0xa270
#define	AR_PHY_TX_PWRCTRL6_1			0xb270
#define	AR_PHY_TX_PWRCTRL_ERR_EST_MODE		0x03000000
#define	AR_PHY_TX_PWRCTRL_ERR_EST_MODE_S	24

#define	AR_PHY_TX_PWRCTRL7			0xa274
#define	AR_PHY_TX_PWRCTRL_INIT_TX_GAIN		0x01F80000
#define	AR_PHY_TX_PWRCTRL_INIT_TX_GAIN_S	19

#define	AR_PHY_TX_PWRCTRL8			0xa278
#define	AR_PHY_TX_PWRCTRL10			0xa394

#define	AR_PHY_TX_GAIN_TBL1			0xa300
#define	AR_PHY_TX_GAIN				0x0007F000
#define	AR_PHY_TX_GAIN_S			12

#define	AR_PHY_CH0_TX_PWRCTRL11			0xa398
#define	AR_PHY_CH1_TX_PWRCTRL11			0xb398
#define	AR_PHY_CH0_TX_PWRCTRL12			0xa3dc
#define	AR_PHY_CH0_TX_PWRCTRL13			0xa3e0

#endif
