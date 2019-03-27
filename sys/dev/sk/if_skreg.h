/*	$OpenBSD: if_skreg.h,v 1.10 2003/08/12 05:23:06 nate Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1997, 1998, 1999, 2000
 *	Bill Paul <wpaul@ctr.columbia.edu>.  All rights reserved.
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

/*-
 * Copyright (c) 2003 Nathan L. Binkert <binkertn@umich.edu>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/* Values to keep the different chip revisions apart (SK_CHIPVER). */
#define SK_GENESIS		0x0A
#define SK_YUKON		0xB0
#define SK_YUKON_LITE		0xB1
#define SK_YUKON_LP		0xB2
#define SK_YUKON_FAMILY(x) ((x) & 0xB0)

/* Known revisions in SK_CONFIG. */
#define SK_YUKON_LITE_REV_A0	0x0 /* invented, see test in skc_attach. */
#define SK_YUKON_LITE_REV_A1	0x3
#define SK_YUKON_LITE_REV_A3	0x7

/*
 * SysKonnect PCI vendor ID
 */
#define VENDORID_SK		0x1148

/*
 * Marvell PCI vendor ID
 */
#define VENDORID_MARVELL	0x11AB

/*
 * SK-NET gigabit ethernet device IDs
 */
#define DEVICEID_SK_V1		0x4300
#define DEVICEID_SK_V2		0x4320

/*
 * Belkin F5D5005
 */
#define DEVICEID_BELKIN_5005	0x5005

/*
 * 3Com PCI vendor ID
 */
#define VENDORID_3COM		0x10b7

/*
 * 3Com gigabit ethernet device ID
 */
#define DEVICEID_3COM_3C940	0x1700

/*
 * Linksys PCI vendor ID
 */
#define VENDORID_LINKSYS	0x1737

/*
 * Linksys gigabit ethernet device ID
 */
#define DEVICEID_LINKSYS_EG1032	0x1032

/*
 * Linksys gigabit ethernet rev 2 sub-device ID
 */
#define SUBDEVICEID_LINKSYS_EG1032_REV2	0x0015

/*
 * D-Link PCI vendor ID
 */
#define	VENDORID_DLINK		0x1186

/*
 * D-Link gigabit ethernet device ID
 */
#define DEVICEID_DLINK_DGE530T_A1	0x4c00
#define DEVICEID_DLINK_DGE530T_B1	0x4b01

/*
 * GEnesis registers. The GEnesis chip has a 256-byte I/O window
 * but internally it has a 16K register space. This 16K space is
 * divided into 128-byte blocks. The first 128 bytes of the I/O
 * window represent the first block, which is permanently mapped
 * at the start of the window. The other 127 blocks can be mapped
 * to the second 128 bytes of the I/O window by setting the desired
 * block value in the RAP register in block 0. Not all of the 127
 * blocks are actually used. Most registers are 32 bits wide, but
 * there are a few 16-bit and 8-bit ones as well.
 */


/* Start of remappable register window. */
#define SK_WIN_BASE		0x0080

/* Size of a window */
#define SK_WIN_LEN		0x80

#define SK_WIN_MASK		0x3F80
#define SK_REG_MASK		0x7F

/* Compute the window of a given register (for the RAP register) */
#define SK_WIN(reg)		(((reg) & SK_WIN_MASK) / SK_WIN_LEN)

/* Compute the relative offset of a register within the window */
#define SK_REG(reg)		((reg) & SK_REG_MASK)

#define SK_PORT_A	0
#define SK_PORT_B	1

/*
 * Compute offset of port-specific register. Since there are two
 * ports, there are two of some GEnesis modules (e.g. two sets of
 * DMA queues, two sets of FIFO control registers, etc...). Normally,
 * the block for port 0 is at offset 0x0 and the block for port 1 is
 * at offset 0x80 (i.e. the next page over). However for the transmit
 * BMUs and RAMbuffers, there are two blocks for each port: one for
 * the sync transmit queue and one for the async queue (which we don't
 * use). However instead of ordering them like this:
 * TX sync 1 / TX sync 2 / TX async 1 / TX async 2
 * SysKonnect has instead ordered them like this:
 * TX sync 1 / TX async 1 / TX sync 2 / TX async 2
 * This means that when referencing the TX BMU and RAMbuffer registers,
 * we have to double the block offset (0x80 * 2) in order to reach the
 * second queue. This prevents us from using the same formula
 * (sk_port * 0x80) to compute the offsets for all of the port-specific
 * blocks: we need an extra offset for the BMU and RAMbuffer registers.
 * The simplest thing is to provide an extra argument to these macros:
 * the 'skip' parameter. The 'skip' value is the number of extra pages
 * for skip when computing the port0/port1 offsets. For most registers,
 * the skip value is 0; for the BMU and RAMbuffer registers, it's 1.
 */
#define SK_IF_READ_4(sc_if, skip, reg)		\
	sk_win_read_4(sc_if->sk_softc, reg +	\
	((sc_if->sk_port * (skip + 1)) * SK_WIN_LEN))
#define SK_IF_READ_2(sc_if, skip, reg)		\
	sk_win_read_2(sc_if->sk_softc, reg + 	\
	((sc_if->sk_port * (skip + 1)) * SK_WIN_LEN))
#define SK_IF_READ_1(sc_if, skip, reg)		\
	sk_win_read_1(sc_if->sk_softc, reg +	\
	((sc_if->sk_port * (skip + 1)) * SK_WIN_LEN))

#define SK_IF_WRITE_4(sc_if, skip, reg, val)	\
	sk_win_write_4(sc_if->sk_softc,		\
	reg + ((sc_if->sk_port * (skip + 1)) * SK_WIN_LEN), val)
#define SK_IF_WRITE_2(sc_if, skip, reg, val)	\
	sk_win_write_2(sc_if->sk_softc,		\
	reg + ((sc_if->sk_port * (skip + 1)) * SK_WIN_LEN), val)
#define SK_IF_WRITE_1(sc_if, skip, reg, val)	\
	sk_win_write_1(sc_if->sk_softc,		\
	reg + ((sc_if->sk_port * (skip + 1)) * SK_WIN_LEN), val)

/* Block 0 registers, permanently mapped at iobase. */
#define SK_RAP		0x0000
#define SK_CSR		0x0004
#define SK_LED		0x0006
#define SK_ISR		0x0008	/* interrupt source */
#define SK_IMR		0x000C	/* interrupt mask */
#define SK_IESR		0x0010	/* interrupt hardware error source */
#define SK_IEMR		0x0014  /* interrupt hardware error mask */
#define SK_ISSR		0x0018	/* special interrupt source */
#define SK_XM_IMR0	0x0020
#define SK_XM_ISR0	0x0028
#define SK_XM_PHYADDR0	0x0030
#define SK_XM_PHYDATA0	0x0034
#define SK_XM_IMR1	0x0040
#define SK_XM_ISR1	0x0048
#define SK_XM_PHYADDR1	0x0050
#define SK_XM_PHYDATA1	0x0054
#define SK_BMU_RX_CSR0	0x0060
#define SK_BMU_RX_CSR1	0x0064
#define SK_BMU_TXS_CSR0	0x0068
#define SK_BMU_TXA_CSR0	0x006C
#define SK_BMU_TXS_CSR1	0x0070
#define SK_BMU_TXA_CSR1	0x0074

/* SK_CSR register */
#define SK_CSR_SW_RESET			0x0001
#define SK_CSR_SW_UNRESET		0x0002
#define SK_CSR_MASTER_RESET		0x0004
#define SK_CSR_MASTER_UNRESET		0x0008
#define SK_CSR_MASTER_STOP		0x0010
#define SK_CSR_MASTER_DONE		0x0020
#define SK_CSR_SW_IRQ_CLEAR		0x0040
#define SK_CSR_SW_IRQ_SET		0x0080
#define SK_CSR_SLOTSIZE			0x0100 /* 1 == 64 bits, 0 == 32 */
#define SK_CSR_BUSCLOCK			0x0200 /* 1 == 33/66 Mhz, = 33 */

/* SK_LED register */
#define SK_LED_GREEN_OFF		0x01
#define SK_LED_GREEN_ON			0x02

/* SK_ISR register */
#define SK_ISR_TX2_AS_CHECK		0x00000001
#define SK_ISR_TX2_AS_EOF		0x00000002
#define SK_ISR_TX2_AS_EOB		0x00000004
#define SK_ISR_TX2_S_CHECK		0x00000008
#define SK_ISR_TX2_S_EOF		0x00000010
#define SK_ISR_TX2_S_EOB		0x00000020
#define SK_ISR_TX1_AS_CHECK		0x00000040
#define SK_ISR_TX1_AS_EOF		0x00000080
#define SK_ISR_TX1_AS_EOB		0x00000100
#define SK_ISR_TX1_S_CHECK		0x00000200
#define SK_ISR_TX1_S_EOF		0x00000400
#define SK_ISR_TX1_S_EOB		0x00000800
#define SK_ISR_RX2_CHECK		0x00001000
#define SK_ISR_RX2_EOF			0x00002000
#define SK_ISR_RX2_EOB			0x00004000
#define SK_ISR_RX1_CHECK		0x00008000
#define SK_ISR_RX1_EOF			0x00010000
#define SK_ISR_RX1_EOB			0x00020000
#define SK_ISR_LINK2_OFLOW		0x00040000
#define SK_ISR_MAC2			0x00080000
#define SK_ISR_LINK1_OFLOW		0x00100000
#define SK_ISR_MAC1			0x00200000
#define SK_ISR_TIMER			0x00400000
#define SK_ISR_EXTERNAL_REG		0x00800000
#define SK_ISR_SW			0x01000000
#define SK_ISR_I2C_RDY			0x02000000
#define SK_ISR_TX2_TIMEO		0x04000000
#define SK_ISR_TX1_TIMEO		0x08000000
#define SK_ISR_RX2_TIMEO		0x10000000
#define SK_ISR_RX1_TIMEO		0x20000000
#define SK_ISR_RSVD			0x40000000
#define SK_ISR_HWERR			0x80000000

/* SK_IMR register */
#define SK_IMR_TX2_AS_CHECK		0x00000001
#define SK_IMR_TX2_AS_EOF		0x00000002
#define SK_IMR_TX2_AS_EOB		0x00000004
#define SK_IMR_TX2_S_CHECK		0x00000008
#define SK_IMR_TX2_S_EOF		0x00000010
#define SK_IMR_TX2_S_EOB		0x00000020
#define SK_IMR_TX1_AS_CHECK		0x00000040
#define SK_IMR_TX1_AS_EOF		0x00000080
#define SK_IMR_TX1_AS_EOB		0x00000100
#define SK_IMR_TX1_S_CHECK		0x00000200
#define SK_IMR_TX1_S_EOF		0x00000400
#define SK_IMR_TX1_S_EOB		0x00000800
#define SK_IMR_RX2_CHECK		0x00001000
#define SK_IMR_RX2_EOF			0x00002000
#define SK_IMR_RX2_EOB			0x00004000
#define SK_IMR_RX1_CHECK		0x00008000
#define SK_IMR_RX1_EOF			0x00010000
#define SK_IMR_RX1_EOB			0x00020000
#define SK_IMR_LINK2_OFLOW		0x00040000
#define SK_IMR_MAC2			0x00080000
#define SK_IMR_LINK1_OFLOW		0x00100000
#define SK_IMR_MAC1			0x00200000
#define SK_IMR_TIMER			0x00400000
#define SK_IMR_EXTERNAL_REG		0x00800000
#define SK_IMR_SW			0x01000000
#define SK_IMR_I2C_RDY			0x02000000
#define SK_IMR_TX2_TIMEO		0x04000000
#define SK_IMR_TX1_TIMEO		0x08000000
#define SK_IMR_RX2_TIMEO		0x10000000
#define SK_IMR_RX1_TIMEO		0x20000000
#define SK_IMR_RSVD			0x40000000
#define SK_IMR_HWERR			0x80000000

#define SK_INTRS1	\
	(SK_IMR_RX1_EOF|SK_IMR_TX1_S_EOF|SK_IMR_MAC1)

#define SK_INTRS2	\
	(SK_IMR_RX2_EOF|SK_IMR_TX2_S_EOF|SK_IMR_MAC2)

/* SK_IESR register */
#define SK_IESR_PAR_RX2			0x00000001
#define SK_IESR_PAR_RX1			0x00000002
#define SK_IESR_PAR_MAC2		0x00000004
#define SK_IESR_PAR_MAC1		0x00000008
#define SK_IESR_PAR_WR_RAM		0x00000010
#define SK_IESR_PAR_RD_RAM		0x00000020
#define SK_IESR_NO_TSTAMP_MAC2		0x00000040
#define SK_IESR_NO_TSTAMO_MAC1		0x00000080
#define SK_IESR_NO_STS_MAC2		0x00000100
#define SK_IESR_NO_STS_MAC1		0x00000200
#define SK_IESR_IRQ_STS			0x00000400
#define SK_IESR_MASTERERR		0x00000800

/* SK_IEMR register */
#define SK_IEMR_PAR_RX2			0x00000001
#define SK_IEMR_PAR_RX1			0x00000002
#define SK_IEMR_PAR_MAC2		0x00000004
#define SK_IEMR_PAR_MAC1		0x00000008
#define SK_IEMR_PAR_WR_RAM		0x00000010
#define SK_IEMR_PAR_RD_RAM		0x00000020
#define SK_IEMR_NO_TSTAMP_MAC2		0x00000040
#define SK_IEMR_NO_TSTAMO_MAC1		0x00000080
#define SK_IEMR_NO_STS_MAC2		0x00000100
#define SK_IEMR_NO_STS_MAC1		0x00000200
#define SK_IEMR_IRQ_STS			0x00000400
#define SK_IEMR_MASTERERR		0x00000800

/* Block 2 */
#define SK_MAC0_0	0x0100
#define SK_MAC0_1	0x0104
#define SK_MAC1_0	0x0108
#define SK_MAC1_1	0x010C
#define SK_MAC2_0	0x0110
#define SK_MAC2_1	0x0114
#define SK_CONNTYPE	0x0118
#define SK_PMDTYPE	0x0119
#define SK_CONFIG	0x011A
#define SK_CHIPVER	0x011B
#define SK_EPROM0	0x011C
#define SK_EPROM1	0x011D		/* yukon/genesis */
#define SK_EPROM2	0x011E		/* yukon/genesis */
#define SK_EPROM3	0x011F
#define SK_EP_ADDR	0x0120
#define SK_EP_DATA	0x0124
#define SK_EP_LOADCTL	0x0128
#define SK_EP_LOADTST	0x0129
#define SK_TIMERINIT	0x0130
#define SK_TIMER	0x0134
#define SK_TIMERCTL	0x0138
#define SK_TIMERTST	0x0139
#define SK_IMTIMERINIT	0x0140
#define SK_IMTIMER	0x0144
#define SK_IMTIMERCTL	0x0148
#define SK_IMTIMERTST	0x0149
#define SK_IMMR		0x014C
#define SK_IHWEMR	0x0150
#define SK_TESTCTL1	0x0158
#define SK_TESTCTL2	0x0159
#define SK_GPIO		0x015C
#define SK_I2CHWCTL	0x0160
#define SK_I2CHWDATA	0x0164
#define SK_I2CHWIRQ	0x0168
#define SK_I2CSW	0x016C
#define SK_BLNKINIT	0x0170
#define SK_BLNKCOUNT	0x0174
#define SK_BLNKCTL	0x0178
#define SK_BLNKSTS	0x0179
#define SK_BLNKTST	0x017A

#define SK_IMCTL_STOP	0x02
#define SK_IMCTL_START	0x04

#define SK_IMTIMER_TICKS_GENESIS	53
#define SK_IMTIMER_TICKS_YUKON		78
#define SK_IM_USECS(x, t)		((x) * (t))

#define	SK_IM_MIN	10
#define	SK_IM_DEFAULT	100
#define	SK_IM_MAX	10000

/*
 * The SK_EPROM0 register contains a byte that describes the
 * amount of SRAM mounted on the NIC. The value also tells if
 * the chips are 64K or 128K. This affects the RAMbuffer address
 * offset that we need to use.
 */
#define SK_RAMSIZE_512K_64	0x1
#define SK_RAMSIZE_1024K_128	0x2
#define SK_RAMSIZE_1024K_64	0x3
#define SK_RAMSIZE_2048K_128	0x4

#define SK_RBOFF_0		0x0
#define SK_RBOFF_80000		0x80000

/*
 * SK_EEPROM1 contains the PHY type, which may be XMAC for
 * fiber-based cards or BCOM for 1000baseT cards with a Broadcom
 * PHY.
 */
#define SK_PHYTYPE_XMAC		0	/* integeated XMAC II PHY */
#define SK_PHYTYPE_BCOM		1	/* Broadcom BCM5400 */
#define SK_PHYTYPE_LONE		2	/* Level One LXT1000 */
#define SK_PHYTYPE_NAT		3	/* National DP83891 */
#define SK_PHYTYPE_MARV_COPPER	4       /* Marvell 88E1011S */
#define SK_PHYTYPE_MARV_FIBER	5       /* Marvell 88E1011S (fiber) */

/*
 * PHY addresses.
 */
#define SK_PHYADDR_XMAC		0x0
#define SK_PHYADDR_BCOM		0x1
#define SK_PHYADDR_LONE		0x3
#define SK_PHYADDR_NAT		0x0
#define SK_PHYADDR_MARV		0x0

#define SK_CONFIG_SINGLEMAC	0x01
#define SK_CONFIG_DIS_DSL_CLK	0x02

#define SK_PMD_1000BASELX	0x4C
#define SK_PMD_1000BASESX	0x53
#define SK_PMD_1000BASECX	0x43
#define SK_PMD_1000BASETX	0x54

/* GPIO bits */
#define SK_GPIO_DAT0		0x00000001
#define SK_GPIO_DAT1		0x00000002
#define SK_GPIO_DAT2		0x00000004
#define SK_GPIO_DAT3		0x00000008
#define SK_GPIO_DAT4		0x00000010
#define SK_GPIO_DAT5		0x00000020
#define SK_GPIO_DAT6		0x00000040
#define SK_GPIO_DAT7		0x00000080
#define SK_GPIO_DAT8		0x00000100
#define SK_GPIO_DAT9		0x00000200
#define SK_GPIO_DIR0		0x00010000
#define SK_GPIO_DIR1		0x00020000
#define SK_GPIO_DIR2		0x00040000
#define SK_GPIO_DIR3		0x00080000
#define SK_GPIO_DIR4		0x00100000
#define SK_GPIO_DIR5		0x00200000
#define SK_GPIO_DIR6		0x00400000
#define SK_GPIO_DIR7		0x00800000
#define SK_GPIO_DIR8		0x01000000
#define SK_GPIO_DIR9		0x02000000

/* Block 3 Ram interface and MAC arbiter registers */
#define SK_RAMADDR	0x0180
#define SK_RAMDATA0	0x0184
#define SK_RAMDATA1	0x0188
#define SK_TO0		0x0190
#define SK_TO1		0x0191
#define SK_TO2		0x0192
#define SK_TO3		0x0193
#define SK_TO4		0x0194
#define SK_TO5		0x0195
#define SK_TO6		0x0196
#define SK_TO7		0x0197
#define SK_TO8		0x0198
#define SK_TO9		0x0199
#define SK_TO10		0x019A
#define SK_TO11		0x019B
#define SK_RITIMEO_TMR	0x019C
#define SK_RAMCTL	0x01A0
#define SK_RITIMER_TST	0x01A2

#define SK_RAMCTL_RESET		0x0001
#define SK_RAMCTL_UNRESET	0x0002
#define SK_RAMCTL_CLR_IRQ_WPAR	0x0100
#define SK_RAMCTL_CLR_IRQ_RPAR	0x0200

/* Mac arbiter registers */
#define SK_MINIT_RX1	0x01B0
#define SK_MINIT_RX2	0x01B1
#define SK_MINIT_TX1	0x01B2
#define SK_MINIT_TX2	0x01B3
#define SK_MTIMEO_RX1	0x01B4
#define SK_MTIMEO_RX2	0x01B5
#define SK_MTIMEO_TX1	0x01B6
#define SK_MTIEMO_TX2	0x01B7
#define SK_MACARB_CTL	0x01B8
#define SK_MTIMER_TST	0x01BA
#define SK_RCINIT_RX1	0x01C0
#define SK_RCINIT_RX2	0x01C1
#define SK_RCINIT_TX1	0x01C2
#define SK_RCINIT_TX2	0x01C3
#define SK_RCTIMEO_RX1	0x01C4
#define SK_RCTIMEO_RX2	0x01C5
#define SK_RCTIMEO_TX1	0x01C6
#define SK_RCTIMEO_TX2	0x01C7
#define SK_RECOVERY_CTL	0x01C8
#define SK_RCTIMER_TST	0x01CA

/* Packet arbiter registers */
#define SK_RXPA1_TINIT	0x01D0
#define SK_RXPA2_TINIT	0x01D4
#define SK_TXPA1_TINIT	0x01D8
#define SK_TXPA2_TINIT	0x01DC
#define SK_RXPA1_TIMEO	0x01E0
#define SK_RXPA2_TIMEO	0x01E4
#define SK_TXPA1_TIMEO	0x01E8
#define SK_TXPA2_TIMEO	0x01EC
#define SK_PKTARB_CTL	0x01F0
#define SK_PKTATB_TST	0x01F2

#define SK_PKTARB_TIMEOUT	0x2000

#define SK_PKTARBCTL_RESET		0x0001
#define SK_PKTARBCTL_UNRESET		0x0002
#define SK_PKTARBCTL_RXTO1_OFF		0x0004
#define SK_PKTARBCTL_RXTO1_ON		0x0008
#define SK_PKTARBCTL_RXTO2_OFF		0x0010
#define SK_PKTARBCTL_RXTO2_ON		0x0020
#define SK_PKTARBCTL_TXTO1_OFF		0x0040
#define SK_PKTARBCTL_TXTO1_ON		0x0080
#define SK_PKTARBCTL_TXTO2_OFF		0x0100
#define SK_PKTARBCTL_TXTO2_ON		0x0200
#define SK_PKTARBCTL_CLR_IRQ_RXTO1	0x0400
#define SK_PKTARBCTL_CLR_IRQ_RXTO2	0x0800
#define SK_PKTARBCTL_CLR_IRQ_TXTO1	0x1000
#define SK_PKTARBCTL_CLR_IRQ_TXTO2	0x2000

#define SK_MINIT_XMAC_B2	54
#define SK_MINIT_XMAC_C1	63

#define SK_MACARBCTL_RESET	0x0001
#define SK_MACARBCTL_UNRESET	0x0002
#define SK_MACARBCTL_FASTOE_OFF	0x0004
#define SK_MACARBCRL_FASTOE_ON	0x0008

#define SK_RCINIT_XMAC_B2	54
#define SK_RCINIT_XMAC_C1	0

#define SK_RECOVERYCTL_RX1_OFF	0x0001
#define SK_RECOVERYCTL_RX1_ON	0x0002
#define SK_RECOVERYCTL_RX2_OFF	0x0004
#define SK_RECOVERYCTL_RX2_ON	0x0008
#define SK_RECOVERYCTL_TX1_OFF	0x0010
#define SK_RECOVERYCTL_TX1_ON	0x0020
#define SK_RECOVERYCTL_TX2_OFF	0x0040
#define SK_RECOVERYCTL_TX2_ON	0x0080

#define SK_RECOVERY_XMAC_B2				\
	(SK_RECOVERYCTL_RX1_ON|SK_RECOVERYCTL_RX2_ON|	\
	SK_RECOVERYCTL_TX1_ON|SK_RECOVERYCTL_TX2_ON)

#define SK_RECOVERY_XMAC_C1				\
	(SK_RECOVERYCTL_RX1_OFF|SK_RECOVERYCTL_RX2_OFF|	\
	SK_RECOVERYCTL_TX1_OFF|SK_RECOVERYCTL_TX2_OFF)

/* Block 4 -- TX Arbiter MAC 1 */
#define SK_TXAR1_TIMERINIT	0x0200
#define SK_TXAR1_TIMERVAL	0x0204
#define SK_TXAR1_LIMITINIT	0x0208
#define SK_TXAR1_LIMITCNT	0x020C
#define SK_TXAR1_COUNTERCTL	0x0210
#define SK_TXAR1_COUNTERTST	0x0212
#define SK_TXAR1_COUNTERSTS	0x0212

/* Block 5 -- TX Arbiter MAC 2 */
#define SK_TXAR2_TIMERINIT	0x0280
#define SK_TXAR2_TIMERVAL	0x0284
#define SK_TXAR2_LIMITINIT	0x0288
#define SK_TXAR2_LIMITCNT	0x028C
#define SK_TXAR2_COUNTERCTL	0x0290
#define SK_TXAR2_COUNTERTST	0x0291
#define SK_TXAR2_COUNTERSTS	0x0292

#define SK_TXARCTL_OFF		0x01
#define SK_TXARCTL_ON		0x02
#define SK_TXARCTL_RATECTL_OFF	0x04
#define SK_TXARCTL_RATECTL_ON	0x08
#define SK_TXARCTL_ALLOC_OFF	0x10
#define SK_TXARCTL_ALLOC_ON	0x20
#define SK_TXARCTL_FSYNC_OFF	0x40
#define SK_TXARCTL_FSYNC_ON	0x80

/* Block 6 -- External registers */
#define SK_EXTREG_BASE	0x300
#define SK_EXTREG_END	0x37C

/* Block 7 -- PCI config registers */
#define SK_PCI_BASE	0x0380
#define SK_PCI_END	0x03FC

/* Compute offset of mirrored PCI register */
#define SK_PCI_REG(reg)		((reg) + SK_PCI_BASE)

/* Block 8 -- RX queue 1 */
#define SK_RXQ1_BUFCNT		0x0400
#define SK_RXQ1_BUFCTL		0x0402
#define SK_RXQ1_NEXTDESC	0x0404
#define SK_RXQ1_RXBUF_LO	0x0408
#define SK_RXQ1_RXBUF_HI	0x040C
#define SK_RXQ1_RXSTAT		0x0410
#define SK_RXQ1_TIMESTAMP	0x0414
#define SK_RXQ1_CSUM1		0x0418
#define SK_RXQ1_CSUM2		0x041A
#define SK_RXQ1_CSUM1_START	0x041C
#define SK_RXQ1_CSUM2_START	0x041E
#define SK_RXQ1_CURADDR_LO	0x0420
#define SK_RXQ1_CURADDR_HI	0x0424
#define SK_RXQ1_CURCNT_LO	0x0428
#define SK_RXQ1_CURCNT_HI	0x042C
#define SK_RXQ1_CURBYTES	0x0430
#define SK_RXQ1_BMU_CSR		0x0434
#define SK_RXQ1_WATERMARK	0x0438
#define SK_RXQ1_FLAG		0x043A
#define SK_RXQ1_TEST1		0x043C
#define SK_RXQ1_TEST2		0x0440
#define SK_RXQ1_TEST3		0x0444

/* Block 9 -- RX queue 2 */
#define SK_RXQ2_BUFCNT		0x0480
#define SK_RXQ2_BUFCTL		0x0482
#define SK_RXQ2_NEXTDESC	0x0484
#define SK_RXQ2_RXBUF_LO	0x0488
#define SK_RXQ2_RXBUF_HI	0x048C
#define SK_RXQ2_RXSTAT		0x0490
#define SK_RXQ2_TIMESTAMP	0x0494
#define SK_RXQ2_CSUM1		0x0498
#define SK_RXQ2_CSUM2		0x049A
#define SK_RXQ2_CSUM1_START	0x049C
#define SK_RXQ2_CSUM2_START	0x049E
#define SK_RXQ2_CURADDR_LO	0x04A0
#define SK_RXQ2_CURADDR_HI	0x04A4
#define SK_RXQ2_CURCNT_LO	0x04A8
#define SK_RXQ2_CURCNT_HI	0x04AC
#define SK_RXQ2_CURBYTES	0x04B0
#define SK_RXQ2_BMU_CSR		0x04B4
#define SK_RXQ2_WATERMARK	0x04B8
#define SK_RXQ2_FLAG		0x04BA
#define SK_RXQ2_TEST1		0x04BC
#define SK_RXQ2_TEST2		0x04C0
#define SK_RXQ2_TEST3		0x04C4

#define SK_RXBMU_CLR_IRQ_ERR		0x00000001
#define SK_RXBMU_CLR_IRQ_EOF		0x00000002
#define SK_RXBMU_CLR_IRQ_EOB		0x00000004
#define SK_RXBMU_CLR_IRQ_PAR		0x00000008
#define SK_RXBMU_RX_START		0x00000010
#define SK_RXBMU_RX_STOP		0x00000020
#define SK_RXBMU_POLL_OFF		0x00000040
#define SK_RXBMU_POLL_ON		0x00000080
#define SK_RXBMU_TRANSFER_SM_RESET	0x00000100
#define SK_RXBMU_TRANSFER_SM_UNRESET	0x00000200
#define SK_RXBMU_DESCWR_SM_RESET	0x00000400
#define SK_RXBMU_DESCWR_SM_UNRESET	0x00000800
#define SK_RXBMU_DESCRD_SM_RESET	0x00001000
#define SK_RXBMU_DESCRD_SM_UNRESET	0x00002000
#define SK_RXBMU_SUPERVISOR_SM_RESET	0x00004000
#define SK_RXBMU_SUPERVISOR_SM_UNRESET	0x00008000
#define SK_RXBMU_PFI_SM_RESET		0x00010000
#define SK_RXBMU_PFI_SM_UNRESET		0x00020000
#define SK_RXBMU_FIFO_RESET		0x00040000
#define SK_RXBMU_FIFO_UNRESET		0x00080000
#define SK_RXBMU_DESC_RESET		0x00100000
#define SK_RXBMU_DESC_UNRESET		0x00200000
#define SK_RXBMU_SUPERVISOR_IDLE	0x01000000

#define SK_RXBMU_ONLINE		\
	(SK_RXBMU_TRANSFER_SM_UNRESET|SK_RXBMU_DESCWR_SM_UNRESET|	\
	SK_RXBMU_DESCRD_SM_UNRESET|SK_RXBMU_SUPERVISOR_SM_UNRESET|	\
	SK_RXBMU_PFI_SM_UNRESET|SK_RXBMU_FIFO_UNRESET|			\
	SK_RXBMU_DESC_UNRESET)

#define SK_RXBMU_OFFLINE		\
	(SK_RXBMU_TRANSFER_SM_RESET|SK_RXBMU_DESCWR_SM_RESET|	\
	SK_RXBMU_DESCRD_SM_RESET|SK_RXBMU_SUPERVISOR_SM_RESET|	\
	SK_RXBMU_PFI_SM_RESET|SK_RXBMU_FIFO_RESET|		\
	SK_RXBMU_DESC_RESET)

/* Block 12 -- TX sync queue 1 */
#define SK_TXQS1_BUFCNT		0x0600
#define SK_TXQS1_BUFCTL		0x0602
#define SK_TXQS1_NEXTDESC	0x0604
#define SK_TXQS1_RXBUF_LO	0x0608
#define SK_TXQS1_RXBUF_HI	0x060C
#define SK_TXQS1_RXSTAT		0x0610
#define SK_TXQS1_CSUM_STARTVAL	0x0614
#define SK_TXQS1_CSUM_STARTPOS	0x0618
#define SK_TXQS1_CSUM_WRITEPOS	0x061A
#define SK_TXQS1_CURADDR_LO	0x0620
#define SK_TXQS1_CURADDR_HI	0x0624
#define SK_TXQS1_CURCNT_LO	0x0628
#define SK_TXQS1_CURCNT_HI	0x062C
#define SK_TXQS1_CURBYTES	0x0630
#define SK_TXQS1_BMU_CSR	0x0634
#define SK_TXQS1_WATERMARK	0x0638
#define SK_TXQS1_FLAG		0x063A
#define SK_TXQS1_TEST1		0x063C
#define SK_TXQS1_TEST2		0x0640
#define SK_TXQS1_TEST3		0x0644

/* Block 13 -- TX async queue 1 */
#define SK_TXQA1_BUFCNT		0x0680
#define SK_TXQA1_BUFCTL		0x0682
#define SK_TXQA1_NEXTDESC	0x0684
#define SK_TXQA1_RXBUF_LO	0x0688
#define SK_TXQA1_RXBUF_HI	0x068C
#define SK_TXQA1_RXSTAT		0x0690
#define SK_TXQA1_CSUM_STARTVAL	0x0694
#define SK_TXQA1_CSUM_STARTPOS	0x0698
#define SK_TXQA1_CSUM_WRITEPOS	0x069A
#define SK_TXQA1_CURADDR_LO	0x06A0
#define SK_TXQA1_CURADDR_HI	0x06A4
#define SK_TXQA1_CURCNT_LO	0x06A8
#define SK_TXQA1_CURCNT_HI	0x06AC
#define SK_TXQA1_CURBYTES	0x06B0
#define SK_TXQA1_BMU_CSR	0x06B4
#define SK_TXQA1_WATERMARK	0x06B8
#define SK_TXQA1_FLAG		0x06BA
#define SK_TXQA1_TEST1		0x06BC
#define SK_TXQA1_TEST2		0x06C0
#define SK_TXQA1_TEST3		0x06C4

/* Block 14 -- TX sync queue 2 */
#define SK_TXQS2_BUFCNT		0x0700
#define SK_TXQS2_BUFCTL		0x0702
#define SK_TXQS2_NEXTDESC	0x0704
#define SK_TXQS2_RXBUF_LO	0x0708
#define SK_TXQS2_RXBUF_HI	0x070C
#define SK_TXQS2_RXSTAT		0x0710
#define SK_TXQS2_CSUM_STARTVAL	0x0714
#define SK_TXQS2_CSUM_STARTPOS	0x0718
#define SK_TXQS2_CSUM_WRITEPOS	0x071A
#define SK_TXQS2_CURADDR_LO	0x0720
#define SK_TXQS2_CURADDR_HI	0x0724
#define SK_TXQS2_CURCNT_LO	0x0728
#define SK_TXQS2_CURCNT_HI	0x072C
#define SK_TXQS2_CURBYTES	0x0730
#define SK_TXQS2_BMU_CSR	0x0734
#define SK_TXQS2_WATERMARK	0x0738
#define SK_TXQS2_FLAG		0x073A
#define SK_TXQS2_TEST1		0x073C
#define SK_TXQS2_TEST2		0x0740
#define SK_TXQS2_TEST3		0x0744

/* Block 15 -- TX async queue 2 */
#define SK_TXQA2_BUFCNT		0x0780
#define SK_TXQA2_BUFCTL		0x0782
#define SK_TXQA2_NEXTDESC	0x0784
#define SK_TXQA2_RXBUF_LO	0x0788
#define SK_TXQA2_RXBUF_HI	0x078C
#define SK_TXQA2_RXSTAT		0x0790
#define SK_TXQA2_CSUM_STARTVAL	0x0794
#define SK_TXQA2_CSUM_STARTPOS	0x0798
#define SK_TXQA2_CSUM_WRITEPOS	0x079A
#define SK_TXQA2_CURADDR_LO	0x07A0
#define SK_TXQA2_CURADDR_HI	0x07A4
#define SK_TXQA2_CURCNT_LO	0x07A8
#define SK_TXQA2_CURCNT_HI	0x07AC
#define SK_TXQA2_CURBYTES	0x07B0
#define SK_TXQA2_BMU_CSR	0x07B4
#define SK_TXQA2_WATERMARK	0x07B8
#define SK_TXQA2_FLAG		0x07BA
#define SK_TXQA2_TEST1		0x07BC
#define SK_TXQA2_TEST2		0x07C0
#define SK_TXQA2_TEST3		0x07C4

#define SK_TXBMU_CLR_IRQ_ERR		0x00000001
#define SK_TXBMU_CLR_IRQ_EOF		0x00000002
#define SK_TXBMU_CLR_IRQ_EOB		0x00000004
#define SK_TXBMU_TX_START		0x00000010
#define SK_TXBMU_TX_STOP		0x00000020
#define SK_TXBMU_POLL_OFF		0x00000040
#define SK_TXBMU_POLL_ON		0x00000080
#define SK_TXBMU_TRANSFER_SM_RESET	0x00000100
#define SK_TXBMU_TRANSFER_SM_UNRESET	0x00000200
#define SK_TXBMU_DESCWR_SM_RESET	0x00000400
#define SK_TXBMU_DESCWR_SM_UNRESET	0x00000800
#define SK_TXBMU_DESCRD_SM_RESET	0x00001000
#define SK_TXBMU_DESCRD_SM_UNRESET	0x00002000
#define SK_TXBMU_SUPERVISOR_SM_RESET	0x00004000
#define SK_TXBMU_SUPERVISOR_SM_UNRESET	0x00008000
#define SK_TXBMU_PFI_SM_RESET		0x00010000
#define SK_TXBMU_PFI_SM_UNRESET		0x00020000
#define SK_TXBMU_FIFO_RESET		0x00040000
#define SK_TXBMU_FIFO_UNRESET		0x00080000
#define SK_TXBMU_DESC_RESET		0x00100000
#define SK_TXBMU_DESC_UNRESET		0x00200000
#define SK_TXBMU_SUPERVISOR_IDLE	0x01000000

#define SK_TXBMU_ONLINE		\
	(SK_TXBMU_TRANSFER_SM_UNRESET|SK_TXBMU_DESCWR_SM_UNRESET|	\
	SK_TXBMU_DESCRD_SM_UNRESET|SK_TXBMU_SUPERVISOR_SM_UNRESET|	\
	SK_TXBMU_PFI_SM_UNRESET|SK_TXBMU_FIFO_UNRESET|			\
	SK_TXBMU_DESC_UNRESET|SK_TXBMU_POLL_ON)

#define SK_TXBMU_OFFLINE		\
	(SK_TXBMU_TRANSFER_SM_RESET|SK_TXBMU_DESCWR_SM_RESET|	\
	SK_TXBMU_DESCRD_SM_RESET|SK_TXBMU_SUPERVISOR_SM_RESET|	\
	SK_TXBMU_PFI_SM_RESET|SK_TXBMU_FIFO_RESET|		\
	SK_TXBMU_DESC_RESET|SK_TXBMU_POLL_OFF)

/* Block 16 -- Receive RAMbuffer 1 */
#define SK_RXRB1_START		0x0800
#define SK_RXRB1_END		0x0804
#define SK_RXRB1_WR_PTR		0x0808
#define SK_RXRB1_RD_PTR		0x080C
#define SK_RXRB1_UTHR_PAUSE	0x0810
#define SK_RXRB1_LTHR_PAUSE	0x0814
#define SK_RXRB1_UTHR_HIPRIO	0x0818
#define SK_RXRB1_UTHR_LOPRIO	0x081C
#define SK_RXRB1_PKTCNT		0x0820
#define SK_RXRB1_LVL		0x0824
#define SK_RXRB1_CTLTST		0x0828

/* Block 17 -- Receive RAMbuffer 2 */
#define SK_RXRB2_START		0x0880
#define SK_RXRB2_END		0x0884
#define SK_RXRB2_WR_PTR		0x0888
#define SK_RXRB2_RD_PTR		0x088C
#define SK_RXRB2_UTHR_PAUSE	0x0890
#define SK_RXRB2_LTHR_PAUSE	0x0894
#define SK_RXRB2_UTHR_HIPRIO	0x0898
#define SK_RXRB2_UTHR_LOPRIO	0x089C
#define SK_RXRB2_PKTCNT		0x08A0
#define SK_RXRB2_LVL		0x08A4
#define SK_RXRB2_CTLTST		0x08A8

/* Block 20 -- Sync. Transmit RAMbuffer 1 */
#define SK_TXRBS1_START		0x0A00
#define SK_TXRBS1_END		0x0A04
#define SK_TXRBS1_WR_PTR	0x0A08
#define SK_TXRBS1_RD_PTR	0x0A0C
#define SK_TXRBS1_PKTCNT	0x0A20
#define SK_TXRBS1_LVL		0x0A24
#define SK_TXRBS1_CTLTST	0x0A28

/* Block 21 -- Async. Transmit RAMbuffer 1 */
#define SK_TXRBA1_START		0x0A80
#define SK_TXRBA1_END		0x0A84
#define SK_TXRBA1_WR_PTR	0x0A88
#define SK_TXRBA1_RD_PTR	0x0A8C
#define SK_TXRBA1_PKTCNT	0x0AA0
#define SK_TXRBA1_LVL		0x0AA4
#define SK_TXRBA1_CTLTST	0x0AA8

/* Block 22 -- Sync. Transmit RAMbuffer 2 */
#define SK_TXRBS2_START		0x0B00
#define SK_TXRBS2_END		0x0B04
#define SK_TXRBS2_WR_PTR	0x0B08
#define SK_TXRBS2_RD_PTR	0x0B0C
#define SK_TXRBS2_PKTCNT	0x0B20
#define SK_TXRBS2_LVL		0x0B24
#define SK_TXRBS2_CTLTST	0x0B28

/* Block 23 -- Async. Transmit RAMbuffer 2 */
#define SK_TXRBA2_START		0x0B80
#define SK_TXRBA2_END		0x0B84
#define SK_TXRBA2_WR_PTR	0x0B88
#define SK_TXRBA2_RD_PTR	0x0B8C
#define SK_TXRBA2_PKTCNT	0x0BA0
#define SK_TXRBA2_LVL		0x0BA4
#define SK_TXRBA2_CTLTST	0x0BA8

#define SK_RBCTL_RESET		0x00000001
#define SK_RBCTL_UNRESET	0x00000002
#define SK_RBCTL_OFF		0x00000004
#define SK_RBCTL_ON		0x00000008
#define SK_RBCTL_STORENFWD_OFF	0x00000010
#define SK_RBCTL_STORENFWD_ON	0x00000020

/* Block 24 -- RX MAC FIFO 1 regisrers and LINK_SYNC counter */
#define SK_RXF1_END		0x0C00
#define SK_RXF1_WPTR		0x0C04
#define SK_RXF1_RPTR		0x0C0C
#define SK_RXF1_PKTCNT		0x0C10
#define SK_RXF1_LVL		0x0C14
#define SK_RXF1_MACCTL		0x0C18
#define SK_RXF1_CTL		0x0C1C
#define SK_RXLED1_CNTINIT	0x0C20
#define SK_RXLED1_COUNTER	0x0C24
#define SK_RXLED1_CTL		0x0C28
#define SK_RXLED1_TST		0x0C29
#define SK_LINK_SYNC1_CINIT	0x0C30
#define SK_LINK_SYNC1_COUNTER	0x0C34
#define SK_LINK_SYNC1_CTL	0x0C38
#define SK_LINK_SYNC1_TST	0x0C39
#define SK_LINKLED1_CTL		0x0C3C

#define SK_FIFO_END		0x3F

/* Receive MAC FIFO 1 (Yukon Only) */
#define SK_RXMF1_END		0x0C40
#define SK_RXMF1_THRESHOLD	0x0C44
#define SK_RXMF1_CTRL_TEST	0x0C48
#define SK_RXMF1_FLUSH_MASK	0x0C4C
#define SK_RXMF1_FLUSH_THRESHOLD	0x0C50
#define SK_RXMF1_WRITE_PTR	0x0C60
#define SK_RXMF1_WRITE_LEVEL	0x0C68
#define SK_RXMF1_READ_PTR	0x0C70
#define SK_RXMF1_READ_LEVEL	0x0C78

/* Receive MAC FIFO 1 Control/Test */
#define SK_RFCTL_WR_PTR_TST_ON	0x00004000	/* Write pointer test on*/
#define SK_RFCTL_WR_PTR_TST_OFF	0x00002000	/* Write pointer test off */
#define SK_RFCTL_WR_PTR_STEP	0x00001000	/* Write pointer increment */
#define SK_RFCTL_RD_PTR_TST_ON	0x00000400	/* Read pointer test on */
#define SK_RFCTL_RD_PTR_TST_OFF	0x00000200	/* Read pointer test off */
#define SK_RFCTL_RD_PTR_STEP	0x00000100	/* Read pointer increment */
#define SK_RFCTL_FIFO_FLUSH_OFF	0x00000080	/* RX FIFO Flsuh mode off */
#define SK_RFCTL_FIFO_FLUSH_ON	0x00000040	/* RX FIFO Flush mode on */
#define SK_RFCTL_RX_FIFO_OVER	0x00000020	/* Clear IRQ RX FIFO Overrun */
#define SK_RFCTL_FRAME_RX_DONE	0x00000010	/* Clear IRQ Frame RX Done */
#define SK_RFCTL_OPERATION_ON	0x00000008	/* Operational mode on */
#define SK_RFCTL_OPERATION_OFF	0x00000004	/* Operational mode off */
#define SK_RFCTL_RESET_CLEAR	0x00000002	/* MAC FIFO Reset Clear */
#define SK_RFCTL_RESET_SET	0x00000001	/* MAC FIFO Reset Set */

#define SK_RFCTL_FIFO_THRESHOLD	0x0a	/* flush threshold (default) */

/* Block 25 -- RX MAC FIFO 2 regisrers and LINK_SYNC counter */
#define SK_RXF2_END		0x0C80
#define SK_RXF2_WPTR		0x0C84
#define SK_RXF2_RPTR		0x0C8C
#define SK_RXF2_PKTCNT		0x0C90
#define SK_RXF2_LVL		0x0C94
#define SK_RXF2_MACCTL		0x0C98
#define SK_RXF2_CTL		0x0C9C
#define SK_RXLED2_CNTINIT	0x0CA0
#define SK_RXLED2_COUNTER	0x0CA4
#define SK_RXLED2_CTL		0x0CA8
#define SK_RXLED2_TST		0x0CA9
#define SK_LINK_SYNC2_CINIT	0x0CB0
#define SK_LINK_SYNC2_COUNTER	0x0CB4
#define SK_LINK_SYNC2_CTL	0x0CB8
#define SK_LINK_SYNC2_TST	0x0CB9
#define SK_LINKLED2_CTL		0x0CBC

#define SK_RXMACCTL_CLR_IRQ_NOSTS	0x00000001
#define SK_RXMACCTL_CLR_IRQ_NOTSTAMP	0x00000002
#define SK_RXMACCTL_TSTAMP_OFF		0x00000004
#define SK_RXMACCTL_RSTAMP_ON		0x00000008
#define SK_RXMACCTL_FLUSH_OFF		0x00000010
#define SK_RXMACCTL_FLUSH_ON		0x00000020
#define SK_RXMACCTL_PAUSE_OFF		0x00000040
#define SK_RXMACCTL_PAUSE_ON		0x00000080
#define SK_RXMACCTL_AFULL_OFF		0x00000100
#define SK_RXMACCTL_AFULL_ON		0x00000200
#define SK_RXMACCTL_VALIDTIME_PATCH_OFF	0x00000400
#define SK_RXMACCTL_VALIDTIME_PATCH_ON	0x00000800
#define SK_RXMACCTL_RXRDY_PATCH_OFF	0x00001000
#define SK_RXMACCTL_RXRDY_PATCH_ON	0x00002000
#define SK_RXMACCTL_STS_TIMEO		0x00FF0000
#define SK_RXMACCTL_TSTAMP_TIMEO	0xFF000000

#define SK_RXLEDCTL_ENABLE		0x0001
#define SK_RXLEDCTL_COUNTER_STOP	0x0002
#define SK_RXLEDCTL_COUNTER_START	0x0004

#define SK_LINKLED_OFF			0x0001
#define SK_LINKLED_ON			0x0002
#define SK_LINKLED_LINKSYNC_OFF		0x0004
#define SK_LINKLED_LINKSYNC_ON		0x0008
#define SK_LINKLED_BLINK_OFF		0x0010
#define SK_LINKLED_BLINK_ON		0x0020

/* Block 26 -- TX MAC FIFO 1 regisrers  */
#define SK_TXF1_END		0x0D00
#define SK_TXF1_WPTR		0x0D04
#define SK_TXF1_RPTR		0x0D0C
#define SK_TXF1_PKTCNT		0x0D10
#define SK_TXF1_LVL		0x0D14
#define SK_TXF1_MACCTL		0x0D18
#define SK_TXF1_CTL		0x0D1C
#define SK_TXLED1_CNTINIT	0x0D20
#define SK_TXLED1_COUNTER	0x0D24
#define SK_TXLED1_CTL		0x0D28
#define SK_TXLED1_TST		0x0D29

/* Transmit MAC FIFO 1 (Yukon Only) */
#define SK_TXMF1_END		0x0D40
#define SK_TXMF1_THRESHOLD	0x0D44
#define SK_TXMF1_CTRL_TEST	0x0D48
#define SK_TXMF1_WRITE_PTR	0x0D60
#define SK_TXMF1_WRITE_SHADOW	0x0D64
#define SK_TXMF1_WRITE_LEVEL	0x0D68
#define SK_TXMF1_READ_PTR	0x0D70
#define SK_TXMF1_RESTART_PTR	0x0D74
#define SK_TXMF1_READ_LEVEL	0x0D78

/* Transmit MAC FIFO Control/Test */
#define SK_TFCTL_WR_PTR_TST_ON	0x00004000	/* Write pointer test on*/
#define SK_TFCTL_WR_PTR_TST_OFF	0x00002000	/* Write pointer test off */
#define SK_TFCTL_WR_PTR_STEP	0x00001000	/* Write pointer increment */
#define SK_TFCTL_RD_PTR_TST_ON	0x00000400	/* Read pointer test on */
#define SK_TFCTL_RD_PTR_TST_OFF	0x00000200	/* Read pointer test off */
#define SK_TFCTL_RD_PTR_STEP	0x00000100	/* Read pointer increment */
#define SK_TFCTL_TX_FIFO_UNDER	0x00000040	/* Clear IRQ TX FIFO Under */
#define SK_TFCTL_FRAME_TX_DONE	0x00000020	/* Clear IRQ Frame TX Done */
#define SK_TFCTL_IRQ_PARITY_ER	0x00000010	/* Clear IRQ Parity Error */
#define SK_TFCTL_OPERATION_ON	0x00000008	/* Operational mode on */
#define SK_TFCTL_OPERATION_OFF	0x00000004	/* Operational mode off */
#define SK_TFCTL_RESET_CLEAR	0x00000002	/* MAC FIFO Reset Clear */
#define SK_TFCTL_RESET_SET	0x00000001	/* MAC FIFO Reset Set */

/* Block 27 -- TX MAC FIFO 2 regisrers  */
#define SK_TXF2_END		0x0D80
#define SK_TXF2_WPTR		0x0D84
#define SK_TXF2_RPTR		0x0D8C
#define SK_TXF2_PKTCNT		0x0D90
#define SK_TXF2_LVL		0x0D94
#define SK_TXF2_MACCTL		0x0D98
#define SK_TXF2_CTL		0x0D9C
#define SK_TXLED2_CNTINIT	0x0DA0
#define SK_TXLED2_COUNTER	0x0DA4
#define SK_TXLED2_CTL		0x0DA8
#define SK_TXLED2_TST		0x0DA9

#define SK_TXMACCTL_XMAC_RESET		0x00000001
#define SK_TXMACCTL_XMAC_UNRESET	0x00000002
#define SK_TXMACCTL_LOOP_OFF		0x00000004
#define SK_TXMACCTL_LOOP_ON		0x00000008
#define SK_TXMACCTL_FLUSH_OFF		0x00000010
#define SK_TXMACCTL_FLUSH_ON		0x00000020
#define SK_TXMACCTL_WAITEMPTY_OFF	0x00000040
#define SK_TXMACCTL_WAITEMPTY_ON	0x00000080
#define SK_TXMACCTL_AFULL_OFF		0x00000100
#define SK_TXMACCTL_AFULL_ON		0x00000200
#define SK_TXMACCTL_TXRDY_PATCH_OFF	0x00000400
#define SK_TXMACCTL_RXRDY_PATCH_ON	0x00000800
#define SK_TXMACCTL_PKT_RECOVERY_OFF	0x00001000
#define SK_TXMACCTL_PKT_RECOVERY_ON	0x00002000
#define SK_TXMACCTL_CLR_IRQ_PERR	0x00008000
#define SK_TXMACCTL_WAITAFTERFLUSH	0x00010000

#define SK_TXLEDCTL_ENABLE		0x0001
#define SK_TXLEDCTL_COUNTER_STOP	0x0002
#define SK_TXLEDCTL_COUNTER_START	0x0004

#define SK_FIFO_RESET		0x00000001
#define SK_FIFO_UNRESET		0x00000002
#define SK_FIFO_OFF		0x00000004
#define SK_FIFO_ON		0x00000008

/* Block 28 -- Descriptor Poll Timer */
#define SK_DPT_INIT		0x0e00	/* Initial value 24 bits */
#define SK_DPT_TIMER		0x0e04	/* Mul of 78.12MHz clk (24b) */

#define SK_DPT_TIMER_MAX	0x00ffffffff	/* 214.75ms at 78.125MHz */

#define SK_DPT_TIMER_CTRL	0x0e08	/* Timer Control 16 bits */
#define SK_DPT_TCTL_STOP	0x0001	/* Stop Timer */
#define SK_DPT_TCTL_START	0x0002	/* Start Timer */

#define SK_DPT_TIMER_TEST	0x0e0a	/* Timer Test 16 bits */
#define SK_DPT_TTEST_STEP	0x0001	/* Timer Decrement */
#define SK_DPT_TTEST_OFF	0x0002	/* Test Mode Off */
#define SK_DPT_TTEST_ON		0x0004	/* Test Mode On */

/* Block 29 -- reserved */

/* Block 30 -- GMAC/GPHY Control Registers (Yukon Only)*/
#define SK_GMAC_CTRL		0x0f00	/* GMAC Control Register */
#define SK_GPHY_CTRL		0x0f04	/* GPHY Control Register */
#define SK_GMAC_ISR		0x0f08	/* GMAC Interrupt Source Register */
#define SK_GMAC_IMR		0x0f0c	/* GMAC Interrupt Mask Register */
#define SK_LINK_CTRL		0x0f10	/* Link Control Register (LCR) */
#define SK_WOL_CTRL		0x0f20	/* Wake on LAN Control Register */
#define SK_MAC_ADDR_LOW		0x0f24	/* Mack Address Registers LOW */
#define SK_MAC_ADDR_HIGH	0x0f28	/* Mack Address Registers HIGH */
#define SK_PAT_READ_PTR		0x0f2c	/* Pattern Read Pointer Register */
#define SK_PAT_LEN_REG0		0x0f30	/* Pattern Length Register 0 */
#define SK_PAT_LEN0		0x0f30	/* Pattern Length 0 */
#define SK_PAT_LEN1		0x0f31	/* Pattern Length 1 */
#define SK_PAT_LEN2		0x0f32	/* Pattern Length 2 */
#define SK_PAT_LEN3		0x0f33	/* Pattern Length 3 */
#define SK_PAT_LEN_REG1		0x0f34	/* Pattern Length Register 1 */
#define SK_PAT_LEN4		0x0f34	/* Pattern Length 4 */
#define SK_PAT_LEN5		0x0f35	/* Pattern Length 5 */
#define SK_PAT_LEN6		0x0f36	/* Pattern Length 6 */
#define SK_PAT_LEN7		0x0f37	/* Pattern Length 7 */
#define SK_PAT_CTR_REG0		0x0f38	/* Pattern Counter Register 0 */
#define SK_PAT_CTR0		0x0f38	/* Pattern Counter 0 */
#define SK_PAT_CTR1		0x0f39	/* Pattern Counter 1 */
#define SK_PAT_CTR2		0x0f3a	/* Pattern Counter 2 */
#define SK_PAT_CTR3		0x0f3b	/* Pattern Counter 3 */
#define SK_PAT_CTR_REG1		0x0f3c	/* Pattern Counter Register 1 */
#define SK_PAT_CTR4		0x0f3c	/* Pattern Counter 4 */
#define SK_PAT_CTR5		0x0f3d	/* Pattern Counter 5 */
#define SK_PAT_CTR6		0x0f3e	/* Pattern Counter 6 */
#define SK_PAT_CTR7		0x0f3f	/* Pattern Counter 7 */

#define SK_GMAC_LOOP_ON		0x00000020	/* Loopback mode for testing */
#define SK_GMAC_LOOP_OFF	0x00000010	/* purposes */
#define SK_GMAC_PAUSE_ON	0x00000008	/* enable forward of pause */
#define SK_GMAC_PAUSE_OFF	0x00000004	/* signal to GMAC */
#define SK_GMAC_RESET_CLEAR	0x00000002	/* Clear GMAC Reset */
#define SK_GMAC_RESET_SET	0x00000001	/* Set GMAC Reset */

#define SK_GPHY_SEL_BDT		0x10000000	/* Select Bidirectional xfer */
#define SK_GPHY_INT_POL_HI	0x08000000	/* IRQ Polarity Active */
#define SK_GPHY_75_OHM		0x04000000	/* Use 75 Ohm Termination */
#define SK_GPHY_DIS_FC		0x02000000	/* Disable Auto Fiber/Copper */
#define SK_GPHY_DIS_SLEEP	0x01000000	/* Disable Energy Detect */
#define SK_GPHY_HWCFG_M_3	0x00800000	/* HWCFG_MODE[3] */
#define SK_GPHY_HWCFG_M_2	0x00400000	/* HWCFG_MODE[2] */
#define SK_GPHY_HWCFG_M_1	0x00200000	/* HWCFG_MODE[1] */
#define SK_GPHY_HWCFG_M_0	0x00100000	/* HWCFG_MODE[0] */
#define SK_GPHY_ANEG_0		0x00080000	/* ANEG[0] */
#define SK_GPHY_ENA_XC		0x00040000	/* Enable MDI Crossover */
#define SK_GPHY_DIS_125		0x00020000	/* Disable 125MHz Clock */
#define SK_GPHY_ANEG_3		0x00010000	/* ANEG[3] */
#define SK_GPHY_ANEG_2		0x00008000	/* ANEG[2] */
#define SK_GPHY_ANEG_1		0x00004000	/* ANEG[1] */
#define SK_GPHY_ENA_PAUSE	0x00002000	/* Enable Pause */
#define SK_GPHY_PHYADDR_4	0x00001000	/* Bit 4 of Phy Addr */
#define SK_GPHY_PHYADDR_3	0x00000800	/* Bit 3 of Phy Addr */
#define SK_GPHY_PHYADDR_2	0x00000400	/* Bit 2 of Phy Addr */
#define SK_GPHY_PHYADDR_1	0x00000200	/* Bit 1 of Phy Addr */
#define SK_GPHY_PHYADDR_0	0x00000100	/* Bit 0 of Phy Addr */
#define SK_GPHY_RESET_CLEAR	0x00000002	/* Clear GPHY Reset */
#define SK_GPHY_RESET_SET	0x00000001	/* Set GPHY Reset */

#define SK_GPHY_COPPER		(SK_GPHY_HWCFG_M_0 | SK_GPHY_HWCFG_M_1 | \
				 SK_GPHY_HWCFG_M_2 | SK_GPHY_HWCFG_M_3 )
#define SK_GPHY_FIBER		(SK_GPHY_HWCFG_M_0 | SK_GPHY_HWCFG_M_1 | \
				 SK_GPHY_HWCFG_M_2 )
#define SK_GPHY_ANEG_ALL	(SK_GPHY_ANEG_0 | SK_GPHY_ANEG_1 | \
				 SK_GPHY_ANEG_2 | SK_GPHY_ANEG_3 )

#define SK_GMAC_INT_TX_OFLOW	0x20	/* Transmit Counter Overflow */
#define SK_GMAC_INT_RX_OFLOW	0x10	/* Receiver Overflow */
#define SK_GMAC_INT_TX_UNDER	0x08	/* Transmit FIFO Underrun */
#define SK_GMAC_INT_TX_DONE	0x04	/* Transmit Complete */
#define SK_GMAC_INT_RX_OVER	0x02	/* Receive FIFO Overrun */
#define SK_GMAC_INT_RX_DONE	0x01	/* Receive Complete */

#define SK_LINK_RESET_CLEAR	0x0002	/* Link Reset Clear */
#define SK_LINK_RESET_SET	0x0001	/* Link Reset Set */

/* Block 31 -- reserved */

/* Block 32-33 -- Pattern Ram */
#define SK_WOL_PRAM		0x1000

/* Block 0x22 - 0x3f -- reserved */

/* Block 0x40 to 0x4F -- XMAC 1 registers */
#define SK_XMAC1_BASE	0x2000

/* Block 0x50 to 0x5F -- MARV 1 registers */
#define SK_MARV1_BASE	0x2800

/* Block 0x60 to 0x6F -- XMAC 2 registers */
#define SK_XMAC2_BASE	0x3000

/* Block 0x70 to 0x7F -- MARV 2 registers */
#define SK_MARV2_BASE	0x3800

/* Compute relative offset of an XMAC register in the XMAC window(s). */
#define SK_XMAC_REG(sc, reg)	(((reg) * 2) + SK_XMAC1_BASE +		\
	(((sc)->sk_port) * (SK_XMAC2_BASE - SK_XMAC1_BASE)))

#if 0
#define SK_XM_READ_4(sc, reg)						\
	((sk_win_read_2(sc->sk_softc,					\
	SK_XMAC_REG(sc, reg)) & 0xFFFF) |				\
	((sk_win_read_2(sc->sk_softc,					\
	SK_XMAC_REG(sc, reg + 2)) & 0xFFFF) << 16))

#define SK_XM_WRITE_4(sc, reg, val)					\
	sk_win_write_2(sc->sk_softc, SK_XMAC_REG(sc, reg),		\
	((val) & 0xFFFF));						\
	sk_win_write_2(sc->sk_softc, SK_XMAC_REG(sc, reg + 2),		\
	((val) >> 16) & 0xFFFF)
#else
#define SK_XM_READ_4(sc, reg)		\
	sk_win_read_4(sc->sk_softc, SK_XMAC_REG(sc, reg))

#define SK_XM_WRITE_4(sc, reg, val)	\
	sk_win_write_4(sc->sk_softc, SK_XMAC_REG(sc, reg), (val))
#endif

#define SK_XM_READ_2(sc, reg)		\
	sk_win_read_2(sc->sk_softc, SK_XMAC_REG(sc, reg))

#define SK_XM_WRITE_2(sc, reg, val)	\
	sk_win_write_2(sc->sk_softc, SK_XMAC_REG(sc, reg), val)

#define SK_XM_SETBIT_4(sc, reg, x)	\
	SK_XM_WRITE_4(sc, reg, (SK_XM_READ_4(sc, reg)) | (x))

#define SK_XM_CLRBIT_4(sc, reg, x)	\
	SK_XM_WRITE_4(sc, reg, (SK_XM_READ_4(sc, reg)) & ~(x))

#define SK_XM_SETBIT_2(sc, reg, x)	\
	SK_XM_WRITE_2(sc, reg, (SK_XM_READ_2(sc, reg)) | (x))

#define SK_XM_CLRBIT_2(sc, reg, x)	\
	SK_XM_WRITE_2(sc, reg, (SK_XM_READ_2(sc, reg)) & ~(x))

/* Compute relative offset of an MARV register in the MARV window(s). */
#define SK_YU_REG(sc, reg) \
	((reg) + SK_MARV1_BASE + \
	(((sc)->sk_port) * (SK_MARV2_BASE - SK_MARV1_BASE)))

#define SK_YU_READ_4(sc, reg)		\
	sk_win_read_4((sc)->sk_softc, SK_YU_REG((sc), (reg)))

#define SK_YU_READ_2(sc, reg)		\
	sk_win_read_2((sc)->sk_softc, SK_YU_REG((sc), (reg)))

#define SK_YU_WRITE_4(sc, reg, val)	\
	sk_win_write_4((sc)->sk_softc, SK_YU_REG((sc), (reg)), (val))

#define SK_YU_WRITE_2(sc, reg, val)	\
	sk_win_write_2((sc)->sk_softc, SK_YU_REG((sc), (reg)), (val))

#define SK_YU_SETBIT_4(sc, reg, x)	\
	SK_YU_WRITE_4(sc, reg, (SK_YU_READ_4(sc, reg)) | (x))

#define SK_YU_CLRBIT_4(sc, reg, x)	\
	SK_YU_WRITE_4(sc, reg, (SK_YU_READ_4(sc, reg)) & ~(x))

#define SK_YU_SETBIT_2(sc, reg, x)	\
	SK_YU_WRITE_2(sc, reg, (SK_YU_READ_2(sc, reg)) | (x))

#define SK_YU_CLRBIT_2(sc, reg, x)	\
	SK_YU_WRITE_2(sc, reg, (SK_YU_READ_2(sc, reg)) & ~(x))

/*
 * The default FIFO threshold on the XMAC II is 4 bytes. On
 * dual port NICs, this often leads to transmit underruns, so we
 * bump the threshold a little.
 */
#define SK_XM_TX_FIFOTHRESH	512

#define SK_PCI_VENDOR_ID	0x0000
#define SK_PCI_DEVICE_ID	0x0002
#define SK_PCI_COMMAND		0x0004
#define SK_PCI_STATUS		0x0006
#define SK_PCI_REVID		0x0008
#define SK_PCI_CLASSCODE	0x0009
#define SK_PCI_CACHELEN		0x000C
#define SK_PCI_LATENCY_TIMER	0x000D
#define SK_PCI_HEADER_TYPE	0x000E
#define SK_PCI_LOMEM		0x0010
#define SK_PCI_LOIO		0x0014
#define SK_PCI_SUBVEN_ID	0x002C
#define SK_PCI_SYBSYS_ID	0x002E
#define SK_PCI_BIOSROM		0x0030
#define SK_PCI_INTLINE		0x003C
#define SK_PCI_INTPIN		0x003D
#define SK_PCI_MINGNT		0x003E
#define SK_PCI_MINLAT		0x003F

/* device specific PCI registers */
#define SK_PCI_OURREG1		0x0040
#define SK_PCI_OURREG2		0x0044
#define SK_PCI_CAPID		0x0048 /* 8 bits */
#define SK_PCI_NEXTPTR		0x0049 /* 8 bits */
#define SK_PCI_PWRMGMTCAP	0x004A /* 16 bits */
#define SK_PCI_PWRMGMTCTRL	0x004C /* 16 bits */
#define SK_PCI_PME_EVENT	0x004F

#define SK_PSTATE_MASK		0x0003
#define SK_PSTATE_D0		0x0000
#define SK_PSTATE_D1		0x0001
#define SK_PSTATE_D2		0x0002
#define SK_PSTATE_D3		0x0003
#define SK_PME_EN		0x0010
#define SK_PME_STATUS		0x8000

#define CSR_WRITE_4(sc, reg, val)	\
	bus_write_4((sc)->sk_res[0], (reg), (val))
#define CSR_WRITE_2(sc, reg, val)	\
	bus_write_2((sc)->sk_res[0], (reg), (val))
#define CSR_WRITE_1(sc, reg, val)	\
	bus_write_1((sc)->sk_res[0], (reg), (val))

#define CSR_READ_4(sc, reg)		\
	bus_read_4((sc)->sk_res[0], (reg))
#define CSR_READ_2(sc, reg)		\
	bus_read_2((sc)->sk_res[0], (reg))
#define CSR_READ_1(sc, reg)		\
	bus_read_1((sc)->sk_res[0], (reg))

struct sk_type {
	u_int16_t		sk_vid;
	u_int16_t		sk_did;
	const char		*sk_name;
};

#define SK_ADDR_LO(x)	((u_int64_t) (x) & 0xffffffff)
#define SK_ADDR_HI(x)	((u_int64_t) (x) >> 32)

#define SK_RING_ALIGN	64

/* RX queue descriptor data structure */
struct sk_rx_desc {
	u_int32_t		sk_ctl;
	u_int32_t		sk_next;
	u_int32_t		sk_data_lo;
	u_int32_t		sk_data_hi;
	u_int32_t		sk_xmac_rxstat;
	u_int32_t		sk_timestamp;
	u_int32_t		sk_csum;
	u_int32_t		sk_csum_start;
};

#define SK_OPCODE_DEFAULT	0x00550000
#define SK_OPCODE_CSUM		0x00560000

#define SK_RXCTL_LEN		0x0000FFFF
#define SK_RXCTL_OPCODE		0x00FF0000
#define SK_RXCTL_TSTAMP_VALID	0x01000000
#define SK_RXCTL_STATUS_VALID	0x02000000
#define SK_RXCTL_DEV0		0x04000000
#define SK_RXCTL_EOF_INTR	0x08000000
#define SK_RXCTL_EOB_INTR	0x10000000
#define SK_RXCTL_LASTFRAG	0x20000000
#define SK_RXCTL_FIRSTFRAG	0x40000000
#define SK_RXCTL_OWN		0x80000000

#define SK_RXSTAT	\
	(SK_RXCTL_EOF_INTR|SK_RXCTL_LASTFRAG|SK_RXCTL_FIRSTFRAG|SK_RXCTL_OWN)

struct sk_tx_desc {
	u_int32_t		sk_ctl;
	u_int32_t		sk_next;
	u_int32_t		sk_data_lo;
	u_int32_t		sk_data_hi;
	u_int32_t		sk_xmac_txstat;
	u_int32_t		sk_csum_startval;
	u_int32_t		sk_csum_start;
	u_int32_t		sk_rsvd1;
};

#define SK_TXCTL_LEN		0x0000FFFF
#define SK_TXCTL_OPCODE		0x00FF0000
#define SK_TXCTL_SW		0x01000000
#define SK_TXCTL_NOCRC		0x02000000
#define SK_TXCTL_STORENFWD	0x04000000
#define SK_TXCTL_EOF_INTR	0x08000000
#define SK_TXCTL_EOB_INTR	0x10000000
#define SK_TXCTL_LASTFRAG	0x20000000
#define SK_TXCTL_FIRSTFRAG	0x40000000
#define SK_TXCTL_OWN		0x80000000

#define SK_TXSTAT	\
	(SK_OPCODE_DEFAULT|SK_TXCTL_EOF_INTR|SK_TXCTL_LASTFRAG|SK_TXCTL_OWN)

#define SK_RXBYTES(x)		((x) & 0x0000FFFF)
#define SK_TXBYTES		SK_RXBYTES

#define SK_TX_RING_CNT		512
#define SK_RX_RING_CNT		256
#define SK_JUMBO_RX_RING_CNT	256
#define SK_MAXTXSEGS		32

#define SK_JUMBO_FRAMELEN	9018
#define SK_JUMBO_MTU		(SK_JUMBO_FRAMELEN-ETHER_HDR_LEN-ETHER_CRC_LEN)
#define SK_MAX_FRAMELEN		\
	(ETHER_MAX_LEN + ETHER_VLAN_ENCAP_LEN - ETHER_CRC_LEN)
#define SK_MIN_FRAMELEN		(ETHER_MIN_LEN - ETHER_CRC_LEN)

struct sk_txdesc {
	struct mbuf		*tx_m;
	bus_dmamap_t		tx_dmamap;
	STAILQ_ENTRY(sk_txdesc)	tx_q;
};

STAILQ_HEAD(sk_txdq, sk_txdesc);

struct sk_rxdesc {
	struct mbuf		*rx_m;
	bus_dmamap_t		rx_dmamap;
};

struct sk_chain_data {
	bus_dma_tag_t		sk_parent_tag;
	bus_dma_tag_t		sk_tx_tag;
	struct sk_txdesc	sk_txdesc[SK_TX_RING_CNT];
	struct sk_txdq		sk_txfreeq;
	struct sk_txdq		sk_txbusyq;
	bus_dma_tag_t		sk_rx_tag;
	struct sk_rxdesc	sk_rxdesc[SK_RX_RING_CNT];
	bus_dma_tag_t		sk_tx_ring_tag;
	bus_dma_tag_t		sk_rx_ring_tag;
	bus_dmamap_t		sk_tx_ring_map;
	bus_dmamap_t		sk_rx_ring_map;
	bus_dmamap_t		sk_rx_sparemap;
	bus_dma_tag_t		sk_jumbo_rx_tag;
	struct sk_rxdesc	sk_jumbo_rxdesc[SK_JUMBO_RX_RING_CNT];
	bus_dma_tag_t		sk_jumbo_rx_ring_tag;
	bus_dmamap_t		sk_jumbo_rx_ring_map;
	bus_dmamap_t		sk_jumbo_rx_sparemap;
	int			sk_tx_prod;
	int			sk_tx_cons;
	int			sk_tx_cnt;
	int			sk_rx_cons;
	int			sk_jumbo_rx_cons;
};

struct sk_ring_data {
	struct sk_tx_desc	*sk_tx_ring;
	bus_addr_t		sk_tx_ring_paddr;
	struct sk_rx_desc	*sk_rx_ring;
	bus_addr_t		sk_rx_ring_paddr;
	struct sk_rx_desc	*sk_jumbo_rx_ring;
	bus_addr_t		sk_jumbo_rx_ring_paddr;
};

#define SK_TX_RING_ADDR(sc, i)	\
    ((sc)->sk_rdata.sk_tx_ring_paddr + sizeof(struct sk_tx_desc) * (i))
#define SK_RX_RING_ADDR(sc, i) \
    ((sc)->sk_rdata.sk_rx_ring_paddr + sizeof(struct sk_rx_desc) * (i))
#define SK_JUMBO_RX_RING_ADDR(sc, i) \
    ((sc)->sk_rdata.sk_jumbo_rx_ring_paddr + sizeof(struct sk_rx_desc) * (i))

#define SK_TX_RING_SZ		\
    (sizeof(struct sk_tx_desc) * SK_TX_RING_CNT)
#define SK_RX_RING_SZ		\
    (sizeof(struct sk_rx_desc) * SK_RX_RING_CNT)
#define SK_JUMBO_RX_RING_SZ		\
    (sizeof(struct sk_rx_desc) * SK_JUMBO_RX_RING_CNT)

struct sk_bcom_hack {
	int			reg;
	int			val;
};

#define SK_INC(x, y)	(x) = (x + 1) % y

/* Forward decl. */
struct sk_if_softc;

/* Softc for the GEnesis controller. */
struct sk_softc {
	struct resource		*sk_res[2];	/* I/O and IRQ resources */
	struct resource_spec	*sk_res_spec;
	void			*sk_intrhand;	/* irq handler handle */
	device_t		sk_dev;
	u_int8_t		sk_type;
	u_int8_t		sk_rev;
	u_int8_t		spare;
	u_int32_t		sk_rboff;	/* RAMbuffer offset */
	u_int32_t		sk_ramsize;	/* amount of RAM on NIC */
	u_int32_t		sk_pmd;		/* physical media type */
	u_int32_t		sk_coppertype;
	u_int32_t		sk_intrmask;
	int			sk_int_mod;
	int			sk_int_ticks;
	int			sk_suspended;
	struct sk_if_softc	*sk_if[2];
	device_t		sk_devs[2];
	struct mtx		sk_mii_mtx;
	struct mtx		sk_mtx;
};

#define	SK_LOCK(_sc)		mtx_lock(&(_sc)->sk_mtx)
#define	SK_UNLOCK(_sc)		mtx_unlock(&(_sc)->sk_mtx)
#define	SK_LOCK_ASSERT(_sc)	mtx_assert(&(_sc)->sk_mtx, MA_OWNED)
#define	SK_IF_LOCK(_sc)		SK_LOCK((_sc)->sk_softc)
#define	SK_IF_UNLOCK(_sc)	SK_UNLOCK((_sc)->sk_softc)
#define	SK_IF_LOCK_ASSERT(_sc)	SK_LOCK_ASSERT((_sc)->sk_softc)
#define	SK_IF_MII_LOCK(_sc)	mtx_lock(&(_sc)->sk_softc->sk_mii_mtx)
#define	SK_IF_MII_UNLOCK(_sc)	mtx_unlock(&(_sc)->sk_softc->sk_mii_mtx)

/* Softc for each logical interface */
struct sk_if_softc {
	struct ifnet		*sk_ifp;	/* interface info */
	device_t		sk_miibus;
	device_t		sk_if_dev;
	u_int8_t		sk_port;	/* port # on controller */
	u_int8_t		sk_xmac_rev;	/* XMAC chip rev (B2 or C1) */
	u_int32_t		sk_rx_ramstart;
	u_int32_t		sk_rx_ramend;
	u_int32_t		sk_tx_ramstart;
	u_int32_t		sk_tx_ramend;
	int			sk_phytype;
	int			sk_phyaddr;
	int			sk_link;
	struct callout		sk_tick_ch;
	struct callout		sk_watchdog_ch;
	int			sk_watchdog_timer;
	struct sk_chain_data	sk_cdata;
	struct sk_ring_data	sk_rdata;
	struct sk_softc		*sk_softc;	/* parent controller */
	int			sk_tx_bmu;	/* TX BMU register */
	int			sk_if_flags;
	int			sk_jumbo_disable;
};

#define SK_TIMEOUT	1000
