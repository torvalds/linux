/*-
 * Copyright (c) 2016 Stanislav Galabov.
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
#ifndef _MTK_USB_PHY_H_
#define _MTK_USB_PHY_H_

#define MT7621_FM_FEG_BASE	0x0100
#define MT7621_U2_BASE		0x0800
#define MT7621_U2_BASE_P1	0x1000
#define MT7621_SR_COEF		28

#define MT7628_FM_FEG_BASE	0x0f00
#define MT7628_U2_BASE		0x0800
#define MT7628_SR_COEF		32

#define U2_PHY_AC0		0x00
#define U2_PHY_AC1		0x04
#define U2_PHY_AC2		0x08
#define U2_PHY_ACR0		0x10
#define   SRCAL_EN			(1<<23)
#define   SRCTRL_MSK			0x7
#define   SRCTRL_OFF			16
#define   SRCTRL			(SRCTRL_MSK<<SRCTRL_OFF)
#define U2_PHY_ACR1		0x14
#define U2_PHY_ACR2		0x18
#define U2_PHY_ACR3		0x1C

#define U2_PHY_DCR0		0x60
#define U2_PHY_DCR1		0x64
#define U2_PHY_DTM0		0x68
#define U2_PHY_DTM1		0x6C

#define U2_PHY_FMCR0		0x00
#define   CYCLECNT			(0xffffff)
#define   FDET_EN			(1<<24)
#define U2_PHY_FMCR1		0x04
#define   FRCK_EN			(1<<8)
#define U2_PHY_FMCR2		0x08
#define U2_PHY_FMMONR0		0x0C
#define U2_PHY_FMMONR1		0x10

#endif /* _MTK_USB_PHY_H_ */
