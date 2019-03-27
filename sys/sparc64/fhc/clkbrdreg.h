/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2004 Jason L. Wright (jason@thought.net)
 * Copyright (c) 2006 Marius Strobl <marius@FreeBSD.org>
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
 *	from: OpenBSD: clkbrdreg.h,v 1.2 2004/10/01 15:36:30 jason Exp
 *
 * $FreeBSD$
 */

#ifndef _SPARC64_FHC_CLKBRDREG_H_
#define	_SPARC64_FHC_CLKBRDREG_H_

/* register bank 0 */
#define	CLK_CF_REG2		0x20	/* clock frequency register 2 */
#define	 CLK_CF_REG2_REN_RCONS	0x80	/* reset enable: remote console */
#define	 CLK_CF_REG2_REN_GEN	0x40	/* reset enable: frequency change */
#define	 CLK_CF_REG2_REN_WDOG	0x20	/* reset enable: watchdog */
#define	 CLK_CF_REG2_DIV1	0x10	/* CPU module divisor bit 1 */
#define	 CLK_CF_REG2_RANGE	0x0c	/* clock range */
#define	 CLK_CF_REG2_DIV0	0x02	/* CPU module divisor bit 0 */
#define	 CLK_CF_REG2_FREQ8	0x01	/* frequency bit 8 */

/* register bank 1 */
#define	CLK_CTRL		0x00	/* system control register */
#define	 CLK_CTRL_IEN_FAN	0x80	/* intr enable: fan failure */
#define	 CLK_CTRL_IEN_DC	0x40	/* intr enable: power supply DC */
#define	 CLK_CTRL_IEN_AC	0x20	/* intr enable: AC power */
#define	 CLK_CTRL_IEN_BRD	0x10	/* intr enable: board insert */
#define	 CLK_CTRL_POFF		0x08	/* turn off system power */
#define	 CLK_CTRL_LLED		0x04	/* left led (reversed) */
#define	 CLK_CTRL_MLED		0x02	/* middle led */
#define	 CLK_CTRL_RLED		0x01	/* right led */
#define	CLK_STS1		0x10	/* system status register 1 */
#define	 CLK_STS1_SLOTS_MASK	0xc0	/* system status 1 slots mask */
#define	 CLK_STS1_SLOTS_16	0x40	/* 16 slots */
#define	 CLK_STS1_SLOTS_8	0xc0	/* 8 slots */
#define	 CLK_STS1_SLOTS_4	0x80	/* 4 slots */
#define	 CLK_STS1_SLOTS_TESTBED	0x00	/* test machine */
#define	 CLK_STS1_SECURE	0x20	/* key in position secure (reversed) */
#define	 CLK_STS1_FAN		0x10	/* fan tray present (reversed) */
#define	 CLK_STS1_BRD		0x08	/* board inserted (reversed) */
#define	 CLK_STS1_PS0		0x04	/* power supply 0 present (reversed) */
#define	 CLK_STS1_RST_WDOG	0x02	/* rst by: watchdog (reversed) */
#define	 CLK_STS1_RST_GEN	0x01	/* rst by: freq change (reversed) */
#define	CLK_STS2		0x20	/* system status register 2 */
#define	 CLK_STS2_RST_RCONS	0x80	/* rst by: remote console (reversed) */
#define	 CLK_STS2_OK_PS0	0x40	/* ok: power supply 0 */
#define	 CLK_STS2_OK_33V	0x20	/* ok: 3.3V on clock board */
#define	 CLK_STS2_OK_50V	0x10	/* ok: 5.0V on clock board */
#define	 CLK_STS2_FAIL_AC	0x08	/* failed: AC power */
#define	 CLK_STS2_FAIL_FAN	0x04	/* failed: rack fans */
#define	 CLK_STS2_OK_ACFAN	0x02	/* ok: 4 AC box fans */
#define	 CLK_STS2_OK_KEYFAN	0x01	/* ok: keyswitch fans */
#define	CLK_PSTS1		0x30	/* power supply 1 status register */
#define	 CLK_PSTS1_PS		0x80	/* power supply 1 present (reversed) */
#define	CLK_PPRES		0x40	/* power supply presence register */
#define	 CLK_PPRES_CSHARE	0x80	/* current share backplane */
#define	 CLK_PPRES_OK_MASK	0x7f	/* precharge and peripheral pwr mask */
#define	 CLK_PPRES_OK_P_5V	0x40	/* ok: peripheral 5V */
#define	 CLK_PPRES_OK_P_12V	0x20	/* ok: peripheral 12V */
#define	 CLK_PPRES_OK_AUX_5V	0x10	/* ok: auxiliary 5V */
#define	 CLK_PPRES_OK_PP_5V	0x08	/* ok: peripheral 5V precharge */
#define	 CLK_PPRES_OK_PP_12V	0x04	/* ok: peripheral 12V precharge */
#define	 CLK_PPRES_OK_SP_3V	0x02	/* ok: system 3.3V precharge */
#define	 CLK_PPRES_OK_SP_5V	0x01	/* ok: system 5V precharge */
#define	CLK_TEMP		0x50	/* temperature register */
#define	CLK_IDIAG		0x60	/* interrupt diagnostic register */
#define	CLK_PSTS2		0x70	/* power supply 2 status register */

/* register bank 2 */
#define CLKVER_SLOTS		0x00	/* clock version slots register */
#define	 CLKVER_SLOTS_MASK	0x80	/* clock version slots mask */
#define	 CLKVER_SLOTS_PLUS	0x00	/* plus system (reversed) */

#endif /* !_SPARC64_FHC_CLKBRDREG_H_ */
