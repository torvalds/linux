/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008, Pyun YongHyeon
 * All rights reserved.
 *              
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:             
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.  
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

#ifndef	_DEV_MII_JMPHYREG_H_
#define	_DEV_MII_JMPHYREG_H_

/*
 * Registers for the JMicron JMC250 Gigabit PHY.
 */

/* PHY specific status register. */
#define JMPHY_SSR			0x11
#define JMPHY_SSR_SPEED_1000		0x8000
#define JMPHY_SSR_SPEED_100		0x4000
#define JMPHY_SSR_SPEED_10		0x0000
#define JMPHY_SSR_SPEED_MASK		0xC000
#define JMPHY_SSR_DUPLEX		0x2000
#define JMPHY_SSR_SPD_DPLX_RESOLVED	0x0800
#define JMPHY_SSR_LINK_UP		0x0400
#define JMPHY_SSR_MDI_XOVER		0x0040
#define	JMPHY_SSR_INV_POLARITY		0x0002

/* PHY specific cable length status register. */
#define	JMPHY_SCL			0x17
#define	JMPHY_SCL_CHAN_D_MASK		0xF000
#define	JMPHY_SCL_CHAN_C_MASK		0x0F00
#define	JMPHY_SCL_CHAN_B_MASK		0x00F0
#define	JMPHY_SCL_CHAN_A_MASK		0x000F
#define	JMPHY_SCL_LEN_35		0
#define	JMPHY_SCL_LEN_40		1
#define	JMPHY_SCL_LEN_50		2
#define	JMPHY_SCL_LEN_60		3
#define	JMPHY_SCL_LEN_70		4
#define	JMPHY_SCL_LEN_80		5
#define	JMPHY_SCL_LEN_90		6
#define	JMPHY_SCL_LEN_100		7
#define	JMPHY_SCL_LEN_110		8
#define	JMPHY_SCL_LEN_120		9
#define	JMPHY_SCL_LEN_130		10
#define	JMPHY_SCL_LEN_140		11
#define	JMPHY_SCL_LEN_150		12
#define	JMPHY_SCL_LEN_160		13
#define	JMPHY_SCL_LEN_170		14
#define	JMPHY_SCL_RSVD			15

/* PHY specific LED control register 1. */
#define	JMPHY_LED_CTL1			0x18
#define	JMPHY_LED_BLINK_42MS		0x0000
#define	JMPHY_LED_BLINK_84MS		0x2000
#define	JMPHY_LED_BLINK_170MS		0x4000
#define	JMPHY_LED_BLINK_340MS		0x6000
#define	JMPHY_LED_BLINK_670MS		0x8000
#define	JMPHY_LED_BLINK_MASK		0xE000
#define	JMPHY_LED_FLP_GAP_MASK		0x1F00
#define	JMPHY_LED_FLP_GAP_DEFULT	0x1000
#define	JMPHY_LED2_POLARITY_MASK	0x0030
#define	JMPHY_LED1_POLARITY_MASK	0x000C
#define	JMPHY_LED0_POLARITY_MASK	0x0003
#define	JMPHY_LED_ON_LO_OFF_HI		0
#define	JMPHY_LED_ON_HI_OFF_HI		1
#define	JMPHY_LED_ON_LO_OFF_TS		2
#define	JMPHY_LED_ON_HI_OFF_TS		3

/* PHY specific LED control register 2. */
#define	JMPHY_LED_CTL2			0x19
#define	JMPHY_LED_NO_STRETCH		0x0000
#define	JMPHY_LED_STRETCH_42MS		0x2000
#define	JMPHY_LED_STRETCH_84MS		0x4000
#define	JMPHY_LED_STRETCH_170MS		0x6000
#define	JMPHY_LED_STRETCH_340MS		0x8000
#define	JMPHY_LED_STRETCH_670MS		0xB000
#define	JMPHY_LED_STRETCH_1300MS	0xC000
#define	JMPHY_LED_STRETCH_2700MS	0xE000
#define	JMPHY_LED2_MODE_MASK		0x0F00
#define	JMPHY_LED1_MODE_MASK		0x00F0
#define	JMPHY_LED0_MODE_MASK		0x000F

/* PHY specific test mode control register. */
#define	JMPHY_TMCTL			0x1A
#define	JMPHY_TMCTL_SLEEP_ENB		0x1000

/* PHY specific configuration register. */
#define	JMPHY_SPEC_ADDR			0x1E
#define	JMPHY_SPEC_ADDR_READ		0x4000
#define	JMPHY_SPEC_ADDR_WRITE		0x8000

#define	JMPHY_SPEC_DATA			0x1F

#define	JMPHY_EXT_COMM_2		0x32

#endif	/* _DEV_MII_JMPHYREG_H_ */
