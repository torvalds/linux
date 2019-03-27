/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1998-2002 Katsushi Kobayashi and Hidetoshi Shimokawa
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the acknowledgement as bellow:
 *
 *    This product includes software developed by K. Kobayashi and H. Shimokawa
 *
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 *
 */

#define	FW_PHY_PHYSID_REG		0x00
#define	FW_PHY_PHYSID			(63<<2)
#define	FW_PHY_ROOT_REG			0x00
#define	FW_PHY_ROOT			(1<<1)
#define	FW_PHY_CPS_REG			0x00
#define	FW_PHY_CPS			(1<<0)

#define	FW_PHY_RHB_REG			0x01
#define	FW_PHY_RHB			(1<<7)
#define	FW_PHY_IBR_REG			0x01
#define	FW_PHY_IBR			(1<<6)
#define	FW_PHY_ISBR_REG			0x05
#define	FW_PHY_ISBR			(1<<6)
#define	FW_PHY_GC_REG			0x01

#define	FW_PHY_SPD_REG			0x02
#define	FW_PHY_SPD			(3<<6)
#define	FW_PHY_REV_REG			0x02
#define	FW_PHY_REV			(3<<4)
#define	FW_PHY_NP_REG			0x02
#define	FW_PHY_NP			(15<<0)

#define	FW_PHY_P1_REG			0x03
#define	FW_PHY_P2_REG			0x04
#define	FW_PHY_P3_REG			0x05

#define	FW_PHY_P_ASTAT			(3<<6)
#define	FW_PHY_P_BSTAT			(3<<4)
#define	FW_PHY_P_CH			(1<<3)
#define	FW_PHY_P_CON			(1<<2)

#define FW_PHY_LOOPINT_REG		0x06
#define FW_PHY_LOOPINT			(1<<7)
#define FW_PHY_CPSINT_REG		0x06
#define FW_PHY_CPSNT			(1<<6)
/*
#define FW_PHY_CPS_REG			0x06
#define FW_PHY_CPS			(1<<5)
*/
#define FW_PHY_IR_REG			0x06
#define FW_PHY_IR			(1<<4)
#define FW_PHY_C_REG			0x06
#define FW_PHY_C			(1<<0)

#define FW_PHY_ESPD_REG			0x03
#define	FW_PHY_ESPD			(7<<5)

#define FW_PHY_EDEL_REG			0x03
#define FW_PHY_EDEL			15<<0
