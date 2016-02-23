/*****************************************************************************
 *
 * Copyright (C) 2008 Cedric Bregardis <cedric.bregardis@free.fr> and
 * Jean-Christian Hassler <jhassler@free.fr>
 *
 * This file is part of the Audiowerk2 ALSA driver
 *
 * The Audiowerk2 ALSA driver is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2.
 *
 * The Audiowerk2 ALSA driver is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with the Audiowerk2 ALSA driver; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301,
 * USA.
 *
 *****************************************************************************/

/* SAA7146 registers */
#define PCI_BT_A	0x4C
#define IICTFR		0x8C
#define IICSTA		0x90
#define BaseA1_in	0x94
#define ProtA1_in	0x98
#define PageA1_in	0x9C
#define BaseA1_out	0xA0
#define ProtA1_out	0xA4
#define PageA1_out	0xA8
#define BaseA2_in	0xAC
#define ProtA2_in	0xB0
#define PageA2_in	0xB4
#define BaseA2_out	0xB8
#define ProtA2_out	0xBC
#define PageA2_out	0xC0
#define IER		0xDC
#define GPIO_CTRL	0xE0
#define ACON1		0xF4
#define ACON2		0xF8
#define MC1		0xFC
#define MC2		0x100
#define ISR		0x10C
#define PSR		0x110
#define SSR		0x114
#define PCI_ADP1	0x12C
#define PCI_ADP2	0x130
#define PCI_ADP3	0x134
#define PCI_ADP4	0x138
#define LEVEL_REP	0x140
#define FB_BUFFER1	0x144
#define FB_BUFFER2	0x148
#define TSL1		0x180
#define TSL2		0x1C0

#define ME	(1UL << 11)
#define LIMIT	(1UL << 4)
#define PV	(1UL << 3)

/* PSR/ISR/IER */
#define PPEF		(1UL << 31)
#define PABO		(1UL << 30)
#define IIC_S		(1UL << 17)
#define IIC_E		(1UL << 16)
#define A2_in		(1UL << 15)
#define A2_out		(1UL << 14)
#define A1_in		(1UL << 13)
#define A1_out		(1UL << 12)
#define AFOU		(1UL << 11)
#define PIN3		(1UL << 6)
#define PIN2		(1UL << 5)
#define PIN1		(1UL << 4)
#define PIN0		(1UL << 3)
#define ECS		(1UL << 2)
#define EC3S		(1UL << 1)
#define EC0S		(1UL << 0)

/* SSR */
#define PRQ		(1UL << 31)
#define PMA		(1UL << 30)
#define IIC_EA		(1UL << 21)
#define IIC_EW		(1UL << 20)
#define IIC_ER		(1UL << 19)
#define IIC_EL		(1UL << 18)
#define IIC_EF		(1UL << 17)
#define AF2_in		(1UL << 10)
#define AF2_out		(1UL << 9)
#define AF1_in		(1UL << 8)
#define AF1_out		(1UL << 7)
#define EC5S		(1UL << 3)
#define EC4S		(1UL << 2)
#define EC2S		(1UL << 1)
#define EC1S		(1UL << 0)

/* PCI_BT_A */
#define BurstA1_in	(1UL << 26)
#define ThreshA1_in	(1UL << 24)
#define BurstA1_out	(1UL << 18)
#define ThreshA1_out	(1UL << 16)
#define BurstA2_in	(1UL << 10)
#define ThreshA2_in	(1UL << 8)
#define BurstA2_out	(1UL << 2)
#define ThreshA2_out	(1UL << 0)

/* MC1 */
#define MRST_N		(1UL << 15)
#define EAP		(1UL << 9)
#define EI2C		(1UL << 8)
#define TR_E_A2_OUT	(1UL << 3)
#define TR_E_A2_IN	(1UL << 2)
#define TR_E_A1_OUT	(1UL << 1)
#define TR_E_A1_IN	(1UL << 0)

/* MC2 */
#define UPLD_IIC	(1UL << 0)

/* ACON1 */
#define AUDIO_MODE	(1UL << 29)
#define MAXLEVEL	(1UL << 22)
#define A1_SWAP		(1UL << 21)
#define A2_SWAP		(1UL << 20)
#define WS0_CTRL	(1UL << 18)
#define WS0_SYNC	(1UL << 16)
#define WS1_CTRL	(1UL << 14)
#define WS1_SYNC	(1UL << 12)
#define WS2_CTRL	(1UL << 10)
#define WS2_SYNC	(1UL << 8)
#define WS3_CTRL	(1UL << 6)
#define WS3_SYNC	(1UL << 4)
#define WS4_CTRL	(1UL << 2)
#define WS4_SYNC	(1UL << 0)

/* ACON2 */
#define A1_CLKSRC	(1UL << 27)
#define A2_CLKSRC	(1UL << 22)
#define INVERT_BCLK1	(1UL << 21)
#define INVERT_BCLK2	(1UL << 20)
#define BCLK1_OEN	(1UL << 19)
#define BCLK2_OEN	(1UL << 18)

/* IICSTA */
#define IICCC		(1UL << 8)
#define ABORT		(1UL << 7)
#define SPERR		(1UL << 6)
#define APERR		(1UL << 5)
#define DTERR		(1UL << 4)
#define DRERR		(1UL << 3)
#define AL		(1UL << 2)
#define ERR		(1UL << 1)
#define BUSY		(1UL << 0)

/* IICTFR */
#define BYTE2		(1UL << 24)
#define BYTE1		(1UL << 16)
#define BYTE0		(1UL << 8)
#define ATRR2		(1UL << 6)
#define ATRR1		(1UL << 4)
#define ATRR0		(1UL << 2)
#define ERR		(1UL << 1)
#define BUSY		(1UL << 0)

#define START	3
#define CONT	2
#define STOP	1
#define NOP	0
