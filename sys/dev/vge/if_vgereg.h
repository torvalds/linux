/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 2004
 *	Bill Paul <wpaul@windriver.com>.  All rights reserved.
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
 *    must display the following acknowledgement:
 *	This product includes software developed by Bill Paul.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY Bill Paul AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL Bill Paul OR THE VOICES IN HIS HEAD
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/*
 * Register definitions for the VIA VT6122 gigabit ethernet controller.
 * Definitions for the built-in copper PHY can be found in vgphy.h.
 *
 * The VT612x controllers have 256 bytes of register space. The
 * manual seems to imply that the registers should all be accessed
 * using 32-bit I/O cycles, but some of them are less than 32 bits
 * wide. Go figure.
 */

#ifndef _IF_VGEREG_H_
#define _IF_VGEREG_H_

#define VIA_VENDORID		0x1106
#define VIA_DEVICEID_61XX	0x3119

#define VGE_PAR0		0x00	/* physical address register */
#define VGE_PAR1		0x02
#define VGE_PAR2		0x04
#define VGE_RXCTL		0x06	/* RX control register */
#define VGE_TXCTL		0x07	/* TX control register */
#define VGE_CRS0		0x08	/* Global cmd register 0 (w to set) */
#define VGE_CRS1		0x09	/* Global cmd register 1 (w to set) */
#define VGE_CRS2		0x0A	/* Global cmd register 2 (w to set) */
#define VGE_CRS3		0x0B	/* Global cmd register 3 (w to set) */
#define VGE_CRC0		0x0C	/* Global cmd register 0 (w to clr) */
#define VGE_CRC1		0x0D	/* Global cmd register 1 (w to clr) */
#define VGE_CRC2		0x0E	/* Global cmd register 2 (w to clr) */
#define VGE_CRC3		0x0F	/* Global cmd register 3 (w to clr) */
#define VGE_MAR0		0x10	/* Mcast hash/CAM register 0 */
#define VGE_MAR1		0x14	/* Mcast hash/CAM register 1 */
#define VGE_CAM0		0x10
#define VGE_CAM1		0x11
#define VGE_CAM2		0x12
#define VGE_CAM3		0x13
#define VGE_CAM4		0x14
#define VGE_CAM5		0x15
#define VGE_CAM6		0x16
#define VGE_CAM7		0x17
#define VGE_TXDESC_HIADDR	0x18	/* Hi part of 64bit txdesc base addr */
#define VGE_DATABUF_HIADDR	0x1D	/* Hi part of 64bit data buffer addr */
#define VGE_INTCTL0		0x20	/* interrupt control register */
#define VGE_RXSUPPTHR		0x20
#define VGE_TXSUPPTHR		0x20
#define VGE_INTHOLDOFF		0x20
#define VGE_INTCTL1		0x21	/* interrupt control register */
#define VGE_TXHOSTERR		0x22	/* TX host error status */
#define VGE_RXHOSTERR		0x23	/* RX host error status */
#define VGE_ISR			0x24	/* Interrupt status register */
#define VGE_IMR			0x28	/* Interrupt mask register */
#define VGE_TXSTS_PORT		0x2C	/* Transmit status port (???) */
#define VGE_TXQCSRS		0x30	/* TX queue ctl/status set */
#define VGE_RXQCSRS		0x32	/* RX queue ctl/status set */
#define VGE_TXQCSRC		0x34	/* TX queue ctl/status clear */
#define VGE_RXQCSRC		0x36	/* RX queue ctl/status clear */
#define VGE_RXDESC_ADDR_LO	0x38	/* RX desc base addr (lo 32 bits) */
#define VGE_RXDESC_CONSIDX	0x3C	/* Current RX descriptor index */
#define VGE_TXQTIMER		0x3E	/* TX queue timer pend register */
#define VGE_RXQTIMER		0x3F	/* RX queue timer pend register */
#define VGE_TXDESC_ADDR_LO0	0x40	/* TX desc0 base addr (lo 32 bits) */
#define VGE_TXDESC_ADDR_LO1	0x44	/* TX desc1 base addr (lo 32 bits) */
#define VGE_TXDESC_ADDR_LO2	0x48	/* TX desc2 base addr (lo 32 bits) */
#define VGE_TXDESC_ADDR_LO3	0x4C	/* TX desc3 base addr (lo 32 bits) */
#define VGE_RXDESCNUM		0x50	/* Size of RX desc ring */
#define VGE_TXDESCNUM		0x52	/* Size of TX desc ring */
#define VGE_TXDESC_CONSIDX0	0x54	/* Current TX descriptor index */
#define VGE_TXDESC_CONSIDX1	0x56	/* Current TX descriptor index */
#define VGE_TXDESC_CONSIDX2	0x58	/* Current TX descriptor index */
#define VGE_TXDESC_CONSIDX3	0x5A	/* Current TX descriptor index */
#define VGE_TX_PAUSE_TIMER	0x5C	/* TX pause frame timer */
#define VGE_RXDESC_RESIDUECNT	0x5E	/* RX descriptor residue count */
#define VGE_FIFOTEST0		0x60	/* FIFO test register */
#define VGE_FIFOTEST1		0x64	/* FIFO test register */
#define VGE_CAMADDR		0x68	/* CAM address register */
#define VGE_CAMCTL		0x69	/* CAM control register */
#define VGE_GFTEST		0x6A
#define VGE_FTSCMD		0x6B
#define VGE_MIICFG		0x6C	/* MII port config register */
#define VGE_MIISTS		0x6D	/* MII port status register */
#define VGE_PHYSTS0		0x6E	/* PHY status register */
#define VGE_PHYSTS1		0x6F	/* PHY status register */
#define VGE_MIICMD		0x70	/* MII command register */
#define VGE_MIIADDR		0x71	/* MII address register */
#define VGE_MIIDATA		0x72	/* MII data register */
#define VGE_SSTIMER		0x74	/* single-shot timer */
#define VGE_PTIMER		0x76	/* periodic timer */
#define VGE_CHIPCFG0		0x78	/* chip config A */
#define VGE_CHIPCFG1		0x79	/* chip config B */
#define VGE_CHIPCFG2		0x7A	/* chip config C */
#define VGE_CHIPCFG3		0x7B	/* chip config D */
#define VGE_DMACFG0		0x7C	/* DMA config 0 */
#define VGE_DMACFG1		0x7D	/* DMA config 1 */
#define VGE_RXCFG		0x7E	/* MAC RX config */
#define VGE_TXCFG		0x7F	/* MAC TX config */
#define VGE_PWRMGMT		0x82	/* power management shadow register */
#define VGE_PWRSTAT		0x83	/* power state shadow register */
#define VGE_MIBCSR		0x84	/* MIB control/status register */
#define VGE_SWEEDATA		0x85	/* EEPROM software loaded data */
#define VGE_MIBDATA		0x88	/* MIB data register */
#define VGE_EEWRDAT		0x8C	/* EEPROM embedded write */
#define VGE_EECSUM		0x92	/* EEPROM checksum */
#define VGE_EECSR		0x93	/* EEPROM control/status */
#define VGE_EERDDAT		0x94	/* EEPROM embedded read */
#define VGE_EEADDR		0x96	/* EEPROM address */
#define VGE_EECMD		0x97	/* EEPROM embedded command */
#define VGE_CHIPSTRAP		0x99	/* Chip jumper strapping status */
#define VGE_MEDIASTRAP		0x9B	/* Media jumper strapping */
#define VGE_DIAGSTS		0x9C	/* Chip diagnostic status */
#define VGE_DBGCTL		0x9E	/* Chip debug control */
#define VGE_DIAGCTL		0x9F	/* Chip diagnostic control */
#define VGE_WOLCR0S		0xA0	/* WOL0 event set */
#define VGE_WOLCR1S		0xA1	/* WOL1 event set */
#define VGE_PWRCFGS		0xA2	/* Power management config set */
#define VGE_WOLCFGS		0xA3	/* WOL config set */
#define VGE_WOLCR0C		0xA4	/* WOL0 event clear */
#define VGE_WOLCR1C		0xA5	/* WOL1 event clear */
#define VGE_PWRCFGC		0xA6	/* Power management config clear */
#define VGE_WOLCFGC		0xA7	/* WOL config clear */
#define VGE_WOLSR0S		0xA8	/* WOL status set */
#define VGE_WOLSR1S		0xA9	/* WOL status set */
#define VGE_WOLSR0C		0xAC	/* WOL status clear */
#define VGE_WOLSR1C		0xAD	/* WOL status clear */
#define VGE_WAKEPAT_CRC0	0xB0
#define VGE_WAKEPAT_CRC1	0xB2
#define VGE_WAKEPAT_CRC2	0xB4
#define VGE_WAKEPAT_CRC3	0xB6
#define VGE_WAKEPAT_CRC4	0xB8
#define VGE_WAKEPAT_CRC5	0xBA
#define VGE_WAKEPAT_CRC6	0xBC
#define VGE_WAKEPAT_CRC7	0xBE
#define VGE_WAKEPAT_MSK0_0	0xC0
#define VGE_WAKEPAT_MSK0_1	0xC4
#define VGE_WAKEPAT_MSK0_2	0xC8
#define VGE_WAKEPAT_MSK0_3	0xCC
#define VGE_WAKEPAT_MSK1_0	0xD0
#define VGE_WAKEPAT_MSK1_1	0xD4
#define VGE_WAKEPAT_MSK1_2	0xD8
#define VGE_WAKEPAT_MSK1_3	0xDC
#define VGE_WAKEPAT_MSK2_0	0xE0
#define VGE_WAKEPAT_MSK2_1	0xE4
#define VGE_WAKEPAT_MSK2_2	0xE8
#define VGE_WAKEPAT_MSK2_3	0xEC
#define VGE_WAKEPAT_MSK3_0	0xF0
#define VGE_WAKEPAT_MSK3_1	0xF4
#define VGE_WAKEPAT_MSK3_2	0xF8
#define VGE_WAKEPAT_MSK3_3	0xFC

/* Receive control register */

#define VGE_RXCTL_RX_BADFRAMES		0x01 /* accept CRC error frames */
#define VGE_RXCTL_RX_RUNT		0x02 /* accept runts */
#define VGE_RXCTL_RX_MCAST		0x04 /* accept multicasts */
#define VGE_RXCTL_RX_BCAST		0x08 /* accept broadcasts */
#define VGE_RXCTL_RX_PROMISC		0x10 /* promisc mode */
#define VGE_RXCTL_RX_GIANT		0x20 /* accept VLAN tagged frames */
#define VGE_RXCTL_RX_UCAST		0x40 /* use perfect filtering */
#define VGE_RXCTL_RX_SYMERR		0x80 /* accept symbol err packet */

/* Transmit control register */

#define VGE_TXCTL_LOOPCTL		0x03 /* loopback control */
#define VGE_TXCTL_COLLCTL		0x0C /* collision retry control */

#define VGE_TXLOOPCTL_OFF		0x00
#define VGE_TXLOOPCTL_MAC_INTERNAL	0x01
#define VGE_TXLOOPCTL_EXTERNAL		0x02

#define VGE_TXCOLLS_NORMAL		0x00 /* one set of 16 retries */
#define VGE_TXCOLLS_32			0x04 /* two sets of 16 retries */
#define VGE_TXCOLLS_48			0x08 /* three sets of 16 retries */
#define VGE_TXCOLLS_INFINITE		0x0C /* retry forever */

/* Global command register 0 */

#define VGE_CR0_START			0x01 /* start NIC */
#define VGE_CR0_STOP			0x02 /* stop NIC */
#define VGE_CR0_RX_ENABLE		0x04 /* turn on RX engine */
#define VGE_CR0_TX_ENABLE		0x08 /* turn on TX engine */

/* Global command register 1 */

#define VGE_CR1_NOUCAST			0x01 /* disable unicast reception */
#define VGE_CR1_NOPOLL			0x08 /* disable RX/TX desc polling */
#define VGE_CR1_TIMER0_ENABLE		0x20 /* enable single shot timer */
#define VGE_CR1_TIMER1_ENABLE		0x40 /* enable periodic timer */
#define VGE_CR1_SOFTRESET		0x80 /* software reset */

/* Global command register 2 */

#define VGE_CR2_TXPAUSE_THRESH_LO	0x03 /* TX pause frame lo threshold */
#define VGE_CR2_TXPAUSE_THRESH_HI	0x0C /* TX pause frame hi threshold */
#define VGE_CR2_HDX_FLOWCTL_ENABLE	0x10 /* half duplex flow control */
#define VGE_CR2_FDX_RXFLOWCTL_ENABLE	0x20 /* full duplex RX flow control */
#define VGE_CR2_FDX_TXFLOWCTL_ENABLE	0x40 /* full duplex TX flow control */
#define VGE_CR2_XON_ENABLE		0x80 /* 802.3x XON/XOFF flow control */

/* Global command register 3 */

#define VGE_CR3_INT_SWPEND		0x01 /* disable multi-level int bits */
#define VGE_CR3_INT_GMSK		0x02 /* mask off all interrupts */
#define VGE_CR3_INT_HOLDOFF		0x04 /* enable int hold off timer */
#define VGE_CR3_DIAG			0x10 /* diagnostic enabled */
#define VGE_CR3_PHYRST			0x20 /* assert PHYRSTZ */
#define VGE_CR3_STOP_FORCE		0x40 /* force NIC to stopped state */

/* Interrupt control register */

#define VGE_INTCTL_SC_RELOAD		0x01 /* reload hold timer */
#define VGE_INTCTL_HC_RELOAD		0x02 /* enable hold timer reload */
#define VGE_INTCTL_STATUS		0x04 /* interrupt pending status */
#define VGE_INTCTL_MASK			0x18 /* multilayer int mask */
#define VGE_INTCTL_RXINTSUP_DISABLE	0x20 /* disable RX int supression */
#define VGE_INTCTL_TXINTSUP_DISABLE	0x40 /* disable TX int supression */
#define VGE_INTCTL_SOFTINT		0x80 /* request soft interrupt */

#define VGE_INTMASK_LAYER0		0x00
#define VGE_INTMASK_LAYER1		0x08
#define VGE_INTMASK_ALL			0x10
#define VGE_INTMASK_ALL2		0x18

/* Transmit host error status register */

#define VGE_TXHOSTERR_TDSTRUCT		0x01 /* bad TX desc structure */
#define VGE_TXHOSTERR_TDFETCH_BUSERR	0x02 /* bus error on desc fetch */
#define VGE_TXHOSTERR_TDWBACK_BUSERR	0x04 /* bus error on desc writeback */
#define VGE_TXHOSTERR_FIFOERR		0x08 /* TX FIFO DMA bus error */

/* Receive host error status register */

#define VGE_RXHOSTERR_RDSTRUCT		0x01 /* bad RX desc structure */
#define VGE_RXHOSTERR_RDFETCH_BUSERR	0x02 /* bus error on desc fetch */
#define VGE_RXHOSTERR_RDWBACK_BUSERR	0x04 /* bus error on desc writeback */
#define VGE_RXHOSTERR_FIFOERR		0x08 /* RX FIFO DMA bus error */

/* Interrupt status register */

#define VGE_ISR_RXOK_HIPRIO	0x00000001 /* hi prio RX int */
#define VGE_ISR_TXOK_HIPRIO	0x00000002 /* hi prio TX int */
#define VGE_ISR_RXOK		0x00000004 /* normal RX done */
#define VGE_ISR_TXOK		0x00000008 /* combo results for next 4 bits */
#define VGE_ISR_TXOK0		0x00000010 /* TX complete on queue 0 */
#define VGE_ISR_TXOK1		0x00000020 /* TX complete on queue 1 */
#define VGE_ISR_TXOK2		0x00000040 /* TX complete on queue 2 */
#define VGE_ISR_TXOK3		0x00000080 /* TX complete on queue 3 */
#define VGE_ISR_RXCNTOFLOW	0x00000400 /* RX packet count overflow */
#define VGE_ISR_RXPAUSE		0x00000800 /* pause frame RX'ed */
#define VGE_ISR_RXOFLOW		0x00001000 /* RX FIFO overflow */
#define VGE_ISR_RXNODESC	0x00002000 /* ran out of RX descriptors */
#define VGE_ISR_RXNODESC_WARN	0x00004000 /* running out of RX descs */
#define VGE_ISR_LINKSTS		0x00008000 /* link status change */
#define VGE_ISR_TIMER0		0x00010000 /* one shot timer expired */
#define VGE_ISR_TIMER1		0x00020000 /* periodic timer expired */
#define VGE_ISR_PWR		0x00040000 /* wake up power event */
#define VGE_ISR_PHYINT		0x00080000 /* PHY interrupt */
#define VGE_ISR_STOPPED		0x00100000 /* software shutdown complete */
#define VGE_ISR_MIBOFLOW	0x00200000 /* MIB counter overflow warning */
#define VGE_ISR_SOFTINT		0x00400000 /* software interrupt */
#define VGE_ISR_HOLDOFF_RELOAD	0x00800000 /* reload hold timer */
#define VGE_ISR_RXDMA_STALL	0x01000000 /* RX DMA stall */
#define VGE_ISR_TXDMA_STALL	0x02000000 /* TX DMA STALL */
#define VGE_ISR_ISRC0		0x10000000 /* interrupt source indication */
#define VGE_ISR_ISRC1		0x20000000 /* interrupt source indication */
#define VGE_ISR_ISRC2		0x40000000 /* interrupt source indication */
#define VGE_ISR_ISRC3		0x80000000 /* interrupt source indication */

#define VGE_INTRS	(VGE_ISR_TXOK0|VGE_ISR_RXOK|VGE_ISR_STOPPED|	\
			 VGE_ISR_RXOFLOW|VGE_ISR_PHYINT|		\
			 VGE_ISR_LINKSTS|VGE_ISR_RXNODESC|		\
			 VGE_ISR_RXDMA_STALL|VGE_ISR_TXDMA_STALL)

#define VGE_INTRS_POLLING	(VGE_ISR_PHYINT|VGE_ISR_LINKSTS)

/* Interrupt mask register */

#define VGE_IMR_RXOK_HIPRIO	0x00000001 /* hi prio RX int */
#define VGE_IMR_TXOK_HIPRIO	0x00000002 /* hi prio TX int */
#define VGE_IMR_RXOK		0x00000004 /* normal RX done */
#define VGE_IMR_TXOK		0x00000008 /* combo results for next 4 bits */
#define VGE_IMR_TXOK0		0x00000010 /* TX complete on queue 0 */
#define VGE_IMR_TXOK1		0x00000020 /* TX complete on queue 1 */
#define VGE_IMR_TXOK2		0x00000040 /* TX complete on queue 2 */
#define VGE_IMR_TXOK3		0x00000080 /* TX complete on queue 3 */
#define VGE_IMR_RXCNTOFLOW	0x00000400 /* RX packet count overflow */
#define VGE_IMR_RXPAUSE		0x00000800 /* pause frame RX'ed */
#define VGE_IMR_RXOFLOW		0x00001000 /* RX FIFO overflow */
#define VGE_IMR_RXNODESC	0x00002000 /* ran out of RX descriptors */
#define VGE_IMR_RXNODESC_WARN	0x00004000 /* running out of RX descs */
#define VGE_IMR_LINKSTS		0x00008000 /* link status change */
#define VGE_IMR_TIMER0		0x00010000 /* one shot timer expired */
#define VGE_IMR_TIMER1		0x00020000 /* periodic timer expired */
#define VGE_IMR_PWR		0x00040000 /* wake up power event */
#define VGE_IMR_PHYINT		0x00080000 /* PHY interrupt */
#define VGE_IMR_STOPPED		0x00100000 /* software shutdown complete */
#define VGE_IMR_MIBOFLOW	0x00200000 /* MIB counter overflow warning */
#define VGE_IMR_SOFTINT		0x00400000 /* software interrupt */
#define VGE_IMR_HOLDOFF_RELOAD	0x00800000 /* reload hold timer */
#define VGE_IMR_RXDMA_STALL	0x01000000 /* RX DMA stall */
#define VGE_IMR_TXDMA_STALL	0x02000000 /* TX DMA STALL */
#define VGE_IMR_ISRC0		0x10000000 /* interrupt source indication */
#define VGE_IMR_ISRC1		0x20000000 /* interrupt source indication */
#define VGE_IMR_ISRC2		0x40000000 /* interrupt source indication */
#define VGE_IMR_ISRC3		0x80000000 /* interrupt source indication */

/* TX descriptor queue control/status register */

#define VGE_TXQCSR_RUN0		0x0001	/* Enable TX queue 0 */
#define VGE_TXQCSR_ACT0		0x0002	/* queue 0 active indicator */
#define VGE_TXQCSR_WAK0		0x0004	/* Wake up (poll) queue 0 */
#define VGE_TXQCSR_DEAD0	0x0008	/* queue 0 dead indicator */
#define VGE_TXQCSR_RUN1		0x0010	/* Enable TX queue 1 */
#define VGE_TXQCSR_ACT1		0x0020	/* queue 1 active indicator */
#define VGE_TXQCSR_WAK1		0x0040	/* Wake up (poll) queue 1 */
#define VGE_TXQCSR_DEAD1	0x0080	/* queue 1 dead indicator */
#define VGE_TXQCSR_RUN2		0x0100	/* Enable TX queue 2 */
#define VGE_TXQCSR_ACT2		0x0200	/* queue 2 active indicator */
#define VGE_TXQCSR_WAK2		0x0400	/* Wake up (poll) queue 2 */
#define VGE_TXQCSR_DEAD2	0x0800	/* queue 2 dead indicator */
#define VGE_TXQCSR_RUN3		0x1000	/* Enable TX queue 3 */
#define VGE_TXQCSR_ACT3		0x2000	/* queue 3 active indicator */
#define VGE_TXQCSR_WAK3		0x4000	/* Wake up (poll) queue 3 */
#define VGE_TXQCSR_DEAD3	0x8000	/* queue 3 dead indicator */

/* RX descriptor queue control/status register */

#define VGE_RXQCSR_RUN		0x0001	/* Enable RX queue */
#define VGE_RXQCSR_ACT		0x0002	/* queue active indicator */
#define VGE_RXQCSR_WAK		0x0004	/* Wake up (poll) queue */
#define VGE_RXQCSR_DEAD		0x0008	/* queue dead indicator */

/* RX/TX queue empty interrupt delay timer register */

#define VGE_QTIMER_PENDCNT	0x3F
#define VGE_QTIMER_RESOLUTION	0xC0

#define VGE_QTIMER_RES_1US	0x00
#define VGE_QTIMER_RES_4US	0x40
#define VGE_QTIMER_RES_16US	0x80
#define VGE_QTIMER_RES_64US	0xC0

/* CAM address register */

#define VGE_CAMADDR_ADDR	0x3F	/* CAM address to program */
#define VGE_CAMADDR_AVSEL	0x40	/* 0 = address cam, 1 = VLAN cam */
#define VGE_CAMADDR_ENABLE	0x80	/* enable CAM read/write */

#define VGE_CAM_MAXADDRS	64

/*
 * CAM command register
 * Note that the page select bits in this register affect three
 * different things:
 * - The behavior of the MAR0/MAR1 registers at offset 0x10 (the
 *   page select bits control whether the MAR0/MAR1 registers affect
 *   the multicast hash filter or the CAM table)
 * - The behavior of the interrupt holdoff timer register at offset
 *   0x20 (the page select bits allow you to set the interrupt
 *   holdoff timer, the TX interrupt supression count or the
 *   RX interrupt supression count)
 * - The behavior the WOL pattern programming registers at offset
 *   0xC0 (controls which pattern is set)
 */


#define VGE_CAMCTL_WRITE	0x04	/* CAM write command */
#define VGE_CAMCTL_READ		0x08	/* CAM read command */
#define VGE_CAMCTL_INTPKT_SIZ	0x10	/* select interesting pkt CAM size */
#define VGE_CAMCTL_INTPKT_ENB	0x20	/* enable interesting packet mode */
#define VGE_CAMCTL_PAGESEL	0xC0	/* page select */

#define VGE_PAGESEL_MAR		0x00
#define VGE_PAGESEL_CAMMASK	0x40
#define VGE_PAGESEL_CAMDATA	0x80

#define VGE_PAGESEL_INTHLDOFF	0x00
#define VGE_PAGESEL_TXSUPPTHR	0x40
#define VGE_PAGESEL_RXSUPPTHR	0x80

#define VGE_PAGESEL_WOLPAT0	0x00
#define VGE_PAGESEL_WOLPAT1	0x40

/* MII port config register */

#define VGE_MIICFG_PHYADDR	0x1F	/* PHY address (internal PHY is 1) */
#define VGE_MIICFG_MDCSPEED	0x20	/* MDC accelerate x 4 */
#define VGE_MIICFG_POLLINT	0xC0	/* polling interval */

#define VGE_MIIPOLLINT_1024	0x00
#define VGE_MIIPOLLINT_512	0x40
#define VGE_MIIPOLLINT_128	0x80
#define VGE_MIIPOLLINT_64	0xC0

/* MII port status register */

#define VGE_MIISTS_IIDL		0x80	/* not at sofrware/timer poll cycle */

/* PHY status register */

#define VGE_PHYSTS_TXFLOWCAP	0x01	/* resolved TX flow control cap */
#define VGE_PHYSTS_RXFLOWCAP	0x02	/* resolved RX flow control cap */
#define VGE_PHYSTS_SPEED10	0x04	/* PHY in 10Mbps mode */
#define VGE_PHYSTS_SPEED1000	0x08	/* PHY in giga mode */
#define VGE_PHYSTS_FDX		0x10	/* PHY in full duplex mode */
#define VGE_PHYSTS_LINK		0x40	/* link status */
#define VGE_PHYSTS_RESETSTS	0x80	/* reset status */

/* MII management command register */

#define VGE_MIICMD_MDC		0x01	/* clock pin */
#define VGE_MIICMD_MDI		0x02	/* data in pin */
#define VGE_MIICMD_MDO		0x04	/* data out pin */
#define VGE_MIICMD_MOUT		0x08	/* data out pin enable */
#define VGE_MIICMD_MDP		0x10	/* enable direct programming mode */
#define VGE_MIICMD_WCMD		0x20	/* embedded mode write */
#define VGE_MIICMD_RCMD		0x40	/* embadded mode read */
#define VGE_MIICMD_MAUTO	0x80	/* enable autopolling */

/* MII address register */

#define VGE_MIIADDR_SWMPL	0x80	/* initiate priority resolution */

/* Chip config register A */

#define VGE_CHIPCFG0_PACPI	0x01	/* pre-ACPI wakeup function */
#define VGE_CHIPCFG0_ABSHDN	0x02	/* abnormal shutdown function */
#define VGE_CHIPCFG0_GPIO1PD	0x04	/* GPIO pin enable */
#define VGE_CHIPCFG0_SKIPTAG	0x08	/* omit 802.1p tag from CRC calc */
#define VGE_CHIPCFG0_PHLED	0x30	/* phy LED select */

/* Chip config register B */
/* Note: some of these bits are not documented in the manual! */

#define VGE_CHIPCFG1_BAKOPT	0x01
#define VGE_CHIPCFG1_MBA	0x02
#define VGE_CHIPCFG1_CAP	0x04
#define VGE_CHIPCFG1_CRANDOM	0x08
#define VGE_CHIPCFG1_OFSET	0x10
#define VGE_CHIPCFG1_SLOTTIME	0x20	/* slot time 512/500 in giga mode */
#define VGE_CHIPCFG1_MIIOPT	0x40
#define VGE_CHIPCFG1_GTCKOPT	0x80

/* Chip config register C */

#define VGE_CHIPCFG2_EELOAD	0x80	/* enable EEPROM programming */

/* Chip config register D */

#define VGE_CHIPCFG3_64BIT_DAC	0x20	/* enable 64bit via DAC */
#define VGE_CHIPCFG3_IODISABLE	0x80	/* disable I/O access mode */

/* DMA config register 0 */

#define VGE_DMACFG0_BURSTLEN	0x07	/* RX/TX DMA burst (in dwords) */

#define VGE_DMABURST_8		0x00
#define VGE_DMABURST_16		0x01
#define VGE_DMABURST_32		0x02
#define VGE_DMABURST_64		0x03
#define VGE_DMABURST_128	0x04
#define VGE_DMABURST_256	0x05
#define VGE_DMABURST_STRFWD	0x07

/* DMA config register 1 */

#define VGE_DMACFG1_LATENB	0x01	/* Latency timer enable */
#define VGE_DMACFG1_MWWAIT	0x02	/* insert wait on master write */
#define VGE_DMACFG1_MRWAIT	0x04	/* insert wait on master read */
#define VGE_DMACFG1_MRM		0x08	/* use memory read multiple */
#define VGE_DMACFG1_PERR_DIS	0x10	/* disable parity error checking */
#define VGE_DMACFG1_XMRL	0x20	/* disable memory read line support */

/* RX MAC config register */

#define VGE_RXCFG_VLANFILT	0x01	/* filter VLAN ID mismatches */
#define VGE_RXCFG_VTAGOPT	0x06	/* VLAN tag handling */
#define VGE_RXCFG_FIFO_LOWAT	0x08	/* RX FIFO low watermark (7QW/15QW) */
#define VGE_RXCFG_FIFO_THR	0x30	/* RX FIFO threshold */
#define VGE_RXCFG_ARB_PRIO	0x80	/* arbitration priority */

#define VGE_VTAG_OPT0		0x00	/* TX: no tag insertion
					   RX: rx all, no tag extraction */

#define VGE_VTAG_OPT1		0x02	/* TX: no tag insertion
					   RX: rx only tagged pkts, no
					       extraction */

#define VGE_VTAG_OPT2		0x04	/* TX: perform tag insertion,
					   RX: rx all, extract tags */

#define VGE_VTAG_OPT3		0x06	/* TX: perform tag insertion,
					   RX: rx only tagged pkts,
					       with extraction */

#define VGE_RXFIFOTHR_128BYTES	0x00
#define VGE_RXFIFOTHR_512BYTES	0x10
#define VGE_RXFIFOTHR_1024BYTES	0x20
#define VGE_RXFIFOTHR_STRNFWD	0x30

/* TX MAC config register */

#define VGE_TXCFG_SNAPOPT	0x01	/* 1 == insert VLAN tag at
					   13th byte
					   0 == insert VLANM tag after
					   SNAP header (21st byte) */
#define VGE_TXCFG_NONBLK	0x02	/* priority TX/non-blocking mode */
#define VGE_TXCFG_NONBLK_THR	0x0C	/* non-blocking threshold */
#define VGE_TXCFG_ARB_PRIO	0x80	/* arbitration priority */

#define VGE_TXBLOCK_64PKTS	0x00
#define VGE_TXBLOCK_32PKTS	0x04
#define VGE_TXBLOCK_128PKTS	0x08
#define VGE_TXBLOCK_8PKTS	0x0C

/* MIB control/status register */
#define	VGE_MIBCSR_CLR		0x01
#define	VGE_MIBCSR_RINI		0x02
#define	VGE_MIBCSR_FLUSH	0x04
#define	VGE_MIBCSR_FREEZE	0x08
#define	VGE_MIBCSR_HI_80	0x00
#define	VGE_MIBCSR_HI_C0	0x10
#define	VGE_MIBCSR_BISTGO	0x40
#define	VGE_MIBCSR_BISTOK	0x80

/* MIB data index. */
#define	VGE_MIB_RX_FRAMES		0
#define	VGE_MIB_RX_GOOD_FRAMES		1
#define	VGE_MIB_TX_GOOD_FRAMES		2
#define	VGE_MIB_RX_FIFO_OVERRUNS	3
#define	VGE_MIB_RX_RUNTS		4
#define	VGE_MIB_RX_RUNTS_ERRS		5
#define	VGE_MIB_RX_PKTS_64		6
#define	VGE_MIB_TX_PKTS_64		7
#define	VGE_MIB_RX_PKTS_65_127		8
#define	VGE_MIB_TX_PKTS_65_127		9
#define	VGE_MIB_RX_PKTS_128_255		10
#define	VGE_MIB_TX_PKTS_128_255		11
#define	VGE_MIB_RX_PKTS_256_511		12
#define	VGE_MIB_TX_PKTS_256_511		13
#define	VGE_MIB_RX_PKTS_512_1023	14
#define	VGE_MIB_TX_PKTS_512_1023	15
#define	VGE_MIB_RX_PKTS_1024_1518	16
#define	VGE_MIB_TX_PKTS_1024_1518	17
#define	VGE_MIB_TX_COLLS		18
#define	VGE_MIB_RX_CRCERRS		19
#define	VGE_MIB_RX_JUMBOS		20
#define	VGE_MIB_TX_JUMBOS		21
#define	VGE_MIB_RX_PAUSE		22
#define	VGE_MIB_TX_PAUSE		23
#define	VGE_MIB_RX_ALIGNERRS		24
#define	VGE_MIB_RX_PKTS_1519_MAX	25
#define	VGE_MIB_RX_PKTS_1519_MAX_ERRS	26
#define	VGE_MIB_TX_SQEERRS		27
#define	VGE_MIB_RX_NOBUFS		28
#define	VGE_MIB_RX_SYMERRS		29
#define	VGE_MIB_RX_LENERRS		30
#define	VGE_MIB_TX_LATECOLLS		31

#define	VGE_MIB_CNT		(VGE_MIB_TX_LATECOLLS - VGE_MIB_RX_FRAMES + 1)
#define	VGE_MIB_DATA_MASK	0x00FFFFFF
#define	VGE_MIB_DATA_IDX(x)	((x) >> 24)

/* Sticky bit shadow register */

#define	VGE_STICKHW_DS0		0x01
#define	VGE_STICKHW_DS1		0x02
#define	VGE_STICKHW_WOL_ENB	0x04
#define	VGE_STICKHW_WOL_STS	0x08
#define	VGE_STICKHW_SWPTAG	0x10

/* WOL pattern control */
#define	VGE_WOLCR0_PATTERN0	0x01
#define	VGE_WOLCR0_PATTERN1	0x02
#define	VGE_WOLCR0_PATTERN2	0x04
#define	VGE_WOLCR0_PATTERN3	0x08
#define	VGE_WOLCR0_PATTERN4	0x10
#define	VGE_WOLCR0_PATTERN5	0x20
#define	VGE_WOLCR0_PATTERN6	0x40
#define	VGE_WOLCR0_PATTERN7	0x80
#define	VGE_WOLCR0_PATTERN_ALL	0xFF

/* WOL event control */
#define	VGE_WOLCR1_UCAST	0x01
#define	VGE_WOLCR1_MAGIC	0x02
#define	VGE_WOLCR1_LINKON	0x04
#define	VGE_WOLCR1_LINKOFF	0x08

/* Poweer management config */
#define VGE_PWRCFG_LEGACY_WOLEN	0x01
#define VGE_PWRCFG_WOL_PULSE	0x20
#define VGE_PWRCFG_WOL_BUTTON	0x00

/* WOL config register */
#define	VGE_WOLCFG_PHYINT_ENB	0x01
#define	VGE_WOLCFG_SAB		0x10
#define	VGE_WOLCFG_SAM		0x20
#define	VGE_WOLCFG_PMEOVR	0x80

/* EEPROM control/status register */

#define VGE_EECSR_EDO		0x01	/* data out pin */
#define VGE_EECSR_EDI		0x02	/* data in pin */
#define VGE_EECSR_ECK		0x04	/* clock pin */
#define VGE_EECSR_ECS		0x08	/* chip select pin */
#define VGE_EECSR_DPM		0x10	/* direct program mode enable */
#define VGE_EECSR_RELOAD	0x20	/* trigger reload from EEPROM */
#define VGE_EECSR_EMBP		0x40	/* embedded program mode enable */

/* EEPROM embedded command register */

#define VGE_EECMD_ERD		0x01	/* EEPROM read command */
#define VGE_EECMD_EWR		0x02	/* EEPROM write command */
#define VGE_EECMD_EWEN		0x04	/* EEPROM write enable */
#define VGE_EECMD_EWDIS		0x08	/* EEPROM write disable */
#define VGE_EECMD_EDONE		0x80	/* read/write done */

/* Chip operation and diagnostic control register */

#define VGE_DIAGCTL_PHYINT_ENB	0x01	/* Enable PHY interrupts */
#define VGE_DIAGCTL_TIMER0_RES	0x02	/* timer0 uSec resolution */
#define VGE_DIAGCTL_TIMER1_RES	0x04	/* timer1 uSec resolution */
#define VGE_DIAGCTL_LPSEL_DIS	0x08	/* disable LPSEL field */
#define VGE_DIAGCTL_MACFORCE	0x10	/* MAC side force mode */
#define VGE_DIAGCTL_FCRSVD	0x20	/* reserved for future fiber use */
#define VGE_DIAGCTL_FDXFORCE	0x40	/* force full duplex mode */
#define VGE_DIAGCTL_GMII	0x80	/* force GMII mode, otherwise MII */

/* Location of station address in EEPROM */
#define VGE_EE_EADDR		0

/* DMA descriptor structures */

/*
 * Each TX DMA descriptor has a control and status word, and 7
 * fragment address/length words. If a transmitted packet spans
 * more than 7 fragments, it has to be coalesced.
 */

#define VGE_TX_FRAGS	7

struct vge_tx_frag {
	uint32_t		vge_addrlo;
	uint32_t		vge_addrhi;
};

/*
 * The high bit in the buflen field of fragment #0 has special meaning.
 * Normally, the chip requires the driver to issue a TX poll command
 * for every packet that gets put in the TX DMA queue. Sometimes though,
 * the driver might want to queue up several packets at once and just
 * issue one transmit command to have all of them processed. In order
 * to obtain this behavior, the special 'queue' bit must be set.
 */

#define VGE_TXDESC_Q		0x80000000

struct vge_tx_desc {
	uint32_t		vge_sts;
	uint32_t		vge_ctl;
	struct vge_tx_frag	vge_frag[VGE_TX_FRAGS];
};

#define VGE_TDSTS_COLLCNT	0x0000000F	/* TX collision count */
#define VGE_TDSTS_COLL		0x00000010	/* collision seen */
#define VGE_TDSTS_OWINCOLL	0x00000020	/* out of window collision */
#define VGE_TDSTS_OWT		0x00000040	/* jumbo frame tx abort */
#define VGE_TDSTS_EXCESSCOLL	0x00000080	/* TX aborted, excess colls */
#define VGE_TDSTS_HBEATFAIL	0x00000100	/* heartbeat detect failed */
#define VGE_TDSTS_CARRLOSS	0x00000200	/* carrier sense lost */
#define VGE_TDSTS_SHUTDOWN	0x00000400	/* shutdown during TX */
#define VGE_TDSTS_LINKFAIL	0x00001000	/* link fail during TX */
#define VGE_TDSTS_GMII		0x00002000	/* GMII transmission */
#define VGE_TDSTS_FDX		0x00004000	/* full duplex transmit */
#define VGE_TDSTS_TXERR		0x00008000	/* error occurred */
#define VGE_TDSTS_SEGSIZE	0x3FFF0000	/* TCP large send size */
#define VGE_TDSTS_OWN		0x80000000	/* own bit */

#define VGE_TDCTL_VLANID	0x00000FFF	/* VLAN ID */
#define VGE_TDCTL_CFI		0x00001000	/* VLAN CFI bit */
#define VGE_TDCTL_PRIO		0x0000E000	/* VLAN prio bits */
#define VGE_TDCTL_NOCRC		0x00010000	/* disable CRC generation */
#define VGE_TDCTL_JUMBO		0x00020000	/* jumbo frame */
#define VGE_TDCTL_TCPCSUM	0x00040000	/* do TCP hw checksum */
#define VGE_TDCTL_UDPCSUM	0x00080000	/* do UDP hw checksum */
#define VGE_TDCTL_IPCSUM	0x00100000	/* do IP hw checksum */
#define VGE_TDCTL_VTAG		0x00200000	/* insert VLAN tag */
#define VGE_TDCTL_PRIO_INT	0x00400000	/* priority int request */
#define VGE_TDCTL_TIC		0x00800000	/* transfer int request */
#define VGE_TDCTL_TCPLSCTL	0x03000000	/* TCP large send ctl */
#define VGE_TDCTL_FRAGCNT	0xF0000000	/* number of frags used */

#define VGE_TD_LS_MOF		0x00000000	/* middle of large send */
#define VGE_TD_LS_SOF		0x01000000	/* start of large send */
#define VGE_TD_LS_EOF		0x02000000	/* end of large send */
#define VGE_TD_LS_NORM		0x03000000	/* normal frame */

/* Receive DMA descriptors have a single fragment pointer. */

struct vge_rx_desc {
	uint32_t	vge_sts;
	uint32_t	vge_ctl;
	uint32_t	vge_addrlo;
	uint32_t	vge_addrhi;
};

/*
 * Like the TX descriptor, the high bit in the buflen field in the
 * RX descriptor has special meaning. This bit controls whether or
 * not interrupts are generated for this descriptor.
 */

#define VGE_RXDESC_I		0x80000000

#define VGE_RDSTS_VIDM		0x00000001	/* VLAN tag filter miss */
#define VGE_RDSTS_CRCERR	0x00000002	/* bad CRC error */
#define VGE_RDSTS_FAERR		0x00000004	/* frame alignment error */
#define VGE_RDSTS_CSUMERR	0x00000008	/* bad TCP/IP checksum */
#define VGE_RDSTS_RLERR		0x00000010	/* RX length error */
#define VGE_RDSTS_SYMERR	0x00000020	/* PCS symbol error */
#define VGE_RDSTS_SNTAG		0x00000040	/* RX'ed tagged SNAP pkt */
#define VGE_RDSTS_DETAG		0x00000080	/* VLAN tag extracted */
#define VGE_RDSTS_BOUNDARY	0x00000300	/* frame boundary bits */
#define VGE_RDSTS_VTAG		0x00000400	/* VLAN tag indicator */
#define VGE_RDSTS_UCAST		0x00000800	/* unicast frame */
#define VGE_RDSTS_BCAST		0x00001000	/* broadcast frame */
#define VGE_RDSTS_MCAST		0x00002000	/* multicast frame */
#define VGE_RDSTS_PFT		0x00004000	/* perfect filter hit */
#define VGE_RDSTS_RXOK		0x00008000	/* frame is good. */
#define VGE_RDSTS_BUFSIZ	0x3FFF0000	/* received frame len */
#define VGE_RDSTS_SHUTDOWN	0x40000000	/* shutdown during RX */
#define VGE_RDSTS_OWN		0x80000000	/* own bit. */

#define VGE_RXPKT_ONEFRAG	0x00000000	/* only one fragment */
#define VGE_RXPKT_EOF		0x00000100	/* last frag in frame */
#define VGE_RXPKT_SOF		0x00000200	/* first frag in frame */
#define VGE_RXPKT_MOF		0x00000300	/* intermediate frag */

#define VGE_RDCTL_VLANID	0x0000FFFF	/* VLAN ID info */
#define VGE_RDCTL_UDPPKT	0x00010000	/* UDP packet received */
#define VGE_RDCTL_TCPPKT	0x00020000	/* TCP packet received */
#define VGE_RDCTL_IPPKT		0x00040000	/* IP packet received */
#define VGE_RDCTL_UDPZERO	0x00080000	/* pkt with UDP CSUM of 0 */
#define VGE_RDCTL_FRAG		0x00100000	/* received IP frag */
#define VGE_RDCTL_PROTOCSUMOK	0x00200000	/* TCP/UDP checksum ok */
#define VGE_RDCTL_IPCSUMOK	0x00400000	/* IP checksum ok */
#define VGE_RDCTL_FILTIDX	0x3C000000	/* interesting filter idx */

#endif /* _IF_VGEREG_H_ */
