/*	$OpenBSD: rtl80x9reg.h,v 1.4 2014/11/24 02:03:37 brad Exp $	*/
/*	$NetBSD: rtl80x9reg.h,v 1.2 1998/10/31 00:31:43 thorpej Exp $	*/

/*-
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe of the Numerical Aerospace Simulation Facility,
 * NASA Ames Research Center.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Registers on Realtek 8019 and 8029 NE2000-compatible network interfaces.
 *
 * Data sheets for these chips can be found at:
 *
 *	http://www.realtek.com.tw
 */

#ifndef _DEV_IC_RTL80x9_REG_H_
#define	_DEV_IC_RTL80x9_REG_H_

/*
 * Page 0 register offsets.
 */
#define	NERTL_RTL0_8019ID0	0x0a	/* 8019 ID Register 0 */
#define	RTL0_8019ID0		'P'

#define	NERTL_RTL0_8019ID1	0x0b	/* 8019 ID Register 1 */
#define	RTL0_8019ID1		'p'

/*
 * Page 3 register offsets.
 */
#define	NERTL_RTL3_EECR		0x01	/* EEPROM Command Register */
#define	RTL3_EECR_EEM1		0x80	/* EEPROM Operating Mode */
#define	RTL3_EECR_EEM0		0x40	
					/* 0 0 Normal operation */
					/* 0 1 Auto-load */
					/* 1 0 9346 programming */
					/* 1 1 Config register write enab */
#define	RTL3_EECR_EECS		0x08	/* EEPROM Chip Select */
#define	RTL3_EECR_EESK		0x04	/* EEPROM Clock */
#define	RTL3_EECR_EEDI		0x02	/* EEPROM Data In */
#define	RTL3_EECR_EEDO		0x01	/* EEPROM Data Out */

#define	NERTL_RTL3_BPAGE	0x02	/* BROM Page Register (8019) */

#define	NERTL_RTL3_CONFIG0	0x03	/* Configuration 0 (ro) */
#define	RTL3_CONFIG0_JP		0x08	/* jumper mode (8019) */
#define	RTL3_CONFIG0_BNC	0x04	/* BNC is active */

#define	NERTL_RTL3_CONFIG1	0x04	/* Configuration 1 (8019) */
#define	RTL3_CONFIG1_IRQEN	0x80	/* IRQ Enable */
#define	RTL3_CONFIG1_IRQS2	0x40	/* IRQ Select */
#define	RTL3_CONFIG1_IRQS1	0x20
#define	RTL3_CONFIG1_IRQS0	0x10
					/* 0 0 0 int 0 irq 2/9 */
					/* 0 0 1 int 1 irq 3 */
					/* 0 1 0 int 2 irq 4 */
					/* 0 1 1 int 3 irq 5 */
					/* 1 0 0 int 4 irq 10 */
					/* 1 0 1 int 5 irq 11 */
					/* 1 1 0 int 6 irq 12 */
					/* 1 1 1 int 7 irq 15 */
#define	RTL_CONFIG1_IOS3	0x08	/* I/O base Select */
#define	RTL_CONFIG1_IOS2	0x04
#define	RTL_CONFIG1_IOS1	0x02
#define	RTL_CONFIG1_IOS0	0x01
					/* 0 0 0 0 0x300 */
					/* 0 0 0 1 0x320 */
					/* 0 0 1 0 0x340 */
					/* 0 0 1 1 0x360 */
					/* 0 1 0 0 0x380 */
					/* 0 1 0 1 0x3a0 */
					/* 0 1 1 0 0x3c0 */
					/* 0 1 1 1 0x3e0 */
					/* 1 0 0 0 0x200 */
					/* 1 0 0 1 0x220 */
					/* 1 0 1 0 0x240 */
					/* 1 0 1 1 0x260 */
					/* 1 1 0 0 0x280 */
					/* 1 1 0 1 0x2a0 */
					/* 1 1 1 0 0x2c0 */
					/* 1 1 1 1 0x2e0 */

#define	NERTL_RTL3_CONFIG2	0x05	/* Configuration 2 */
#define	RTL3_CONFIG2_PL1	0x80	/* Network media type */
#define	RTL3_CONFIG2_PL0	0x40
					/* 0 0 TP/CX auto-detect */
					/* 0 1 10baseT */
					/* 1 0 10base5 */
					/* 1 1 10base2 */
#define	RTL3_CONFIG2_8029FCE	0x20	/* Flow Control Enable */
#define	RTL3_CONFIG2_8029PF	0x10	/* Pause Flag */
#define	RTL3_CONFIG2_8029BS1	0x02	/* Boot Rom Size */
#define	RTL3_CONFIG2_8029BS0	0x01
					/* 0 0 No Boot Rom */
					/* 0 1 8k */
					/* 1 0 16k */
					/* 1 1 32k */
#define	RTL3_CONFIG2_8019BSELB	0x20	/* BROM disable */
#define	RTL3_CONFIG2_8019BS4	0x10	/* BROM size/base */
#define	RTL3_CONFIG2_8019BS3	0x08
#define	RTL3_CONFIG2_8019BS2	0x04
#define	RTL3_CONFIG2_8019BS1	0x02
#define	RTL3_CONFIG2_8019BS0	0x01

#define	NERTL_RTL3_CONFIG3	0x06	/* Configuration 3 */
#define	RTL3_CONFIG3_8019PNP	0x80	/* PnP Mode */
#define	RTL3_CONFIG3_FUDUP	0x40	/* Full Duplex */
#define	RTL3_CONFIG3_LEDS1	0x20	/* LED1/2 pin configuration */
					/* 0 LED1 == LED_RX, LED2 == LED_TX */
					/* 1 LED1 == LED_CRS, LED2 == MCSB */
#define	RTL3_CONFIG3_LEDS0	0x10	/* LED0 pin configuration */
					/* 0 LED0 pin == LED_COL */
					/* 1 LED0 pin == LED_LINK */
#define	RTL3_CONFIG3_SLEEP	0x04	/* Sleep mode */
#define	RTL3_CONFIG3_PWRDN	0x02	/* Power Down */
#define	RTL3_CONFIG3_8019ACTIVEB 0x01	/* inverse of bit 0 in PnP Act Reg */

#define	NERTL_RTL3_CSNSAV	0x08	/* CSN Save Register (8019) */

#define	NERTL_RTL3_HLTCLK	0x09	/* Halt Clock */
#define	RTL3_HLTCLK_RUNNING	'R'	/* clock runs in power down */
#define	RTL3_HLTCLK_HALTED	'H'	/* clock halted in power down */

#define	NERTL_RTL3_INTR		0x0b	/* ISA bus states of INT7-0 (8019) */

#define	NERTL_RTL3_8029ID0	0x0e	/* ID register 0 */

#define	NERTL_RTL3_8029ID1	0x0f	/* ID register 1 */

#endif /* _DEV_IC_RTL80x9_REG_H_ */
