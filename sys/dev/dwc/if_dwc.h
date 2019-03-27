/*-
 * Copyright (c) 2014 Ruslan Bukin <br@bsdpad.com>
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract (FA8750-10-C-0237)
 * ("CTSRD"), as part of the DARPA CRASH research programme.
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

/*
 * Register names were taken almost as is from the documentation.
 */

#ifndef __IF_DWC_H__
#define __IF_DWC_H__

#define	MAC_CONFIGURATION	0x0
#define	 CONF_JD		(1 << 22)	/* jabber timer disable */
#define	 CONF_BE		(1 << 21)	/* Frame Burst Enable */
#define	 CONF_PS		(1 << 15)	/* GMII/MII */
#define	 CONF_FES		(1 << 14)	/* MII speed select */
#define	 CONF_DM		(1 << 11)	/* Full Duplex Enable */
#define	 CONF_ACS		(1 << 7)
#define	 CONF_TE		(1 << 3)
#define	 CONF_RE		(1 << 2)
#define	MAC_FRAME_FILTER	0x4
#define	 FRAME_FILTER_RA	(1U << 31)	/* Receive All */
#define	 FRAME_FILTER_HPF	(1 << 10)	/* Hash or Perfect Filter */
#define	 FRAME_FILTER_PM	(1 << 4)	/* Pass multicast */
#define	 FRAME_FILTER_HMC	(1 << 2)
#define	 FRAME_FILTER_HUC	(1 << 1)
#define	 FRAME_FILTER_PR	(1 << 0)	/* All Incoming Frames */
#define	GMAC_MAC_HTHIGH		0x08
#define	GMAC_MAC_HTLOW		0x0c
#define	GMII_ADDRESS		0x10
#define	 GMII_ADDRESS_PA_MASK	0x1f		/* Phy device */
#define	 GMII_ADDRESS_PA_SHIFT	11
#define	 GMII_ADDRESS_GR_MASK	0x1f		/* Phy register */
#define	 GMII_ADDRESS_GR_SHIFT	6
#define	 GMII_ADDRESS_CR_MASK	0xf
#define	 GMII_ADDRESS_CR_SHIFT	2		/* Clock */
#define	 GMII_ADDRESS_GW	(1 << 1)	/* Write operation */
#define	 GMII_ADDRESS_GB	(1 << 0)	/* Busy */
#define	GMII_DATA		0x14
#define	FLOW_CONTROL		0x18
#define	GMAC_VLAN_TAG		0x1C
#define	VERSION			0x20
#define	DEBUG			0x24
#define	LPI_CONTROL_STATUS	0x30
#define	LPI_TIMERS_CONTROL	0x34
#define	INTERRUPT_STATUS	0x38
#define	INTERRUPT_MASK		0x3C
#define	MAC_ADDRESS_HIGH(n)	((n > 15 ? 0x800 : 0x40) + 0x8 * n)
#define	MAC_ADDRESS_LOW(n)	((n > 15 ? 0x804 : 0x44) + 0x8 * n)

#define	SGMII_RGMII_SMII_CTRL_STATUS	0xD8
#define	MMC_CONTROL			0x100
#define	 MMC_CONTROL_CNTRST		(1 << 0)
#define	MMC_RECEIVE_INTERRUPT		0x104
#define	MMC_TRANSMIT_INTERRUPT		0x108
#define	MMC_RECEIVE_INTERRUPT_MASK	0x10C
#define	MMC_TRANSMIT_INTERRUPT_MASK	0x110
#define	TXOCTETCOUNT_GB			0x114
#define	TXFRAMECOUNT_GB			0x118
#define	TXBROADCASTFRAMES_G		0x11C
#define	TXMULTICASTFRAMES_G		0x120
#define	TX64OCTETS_GB			0x124
#define	TX65TO127OCTETS_GB		0x128
#define	TX128TO255OCTETS_GB		0x12C
#define	TX256TO511OCTETS_GB		0x130
#define	TX512TO1023OCTETS_GB		0x134
#define	TX1024TOMAXOCTETS_GB		0x138
#define	TXUNICASTFRAMES_GB		0x13C
#define	TXMULTICASTFRAMES_GB		0x140
#define	TXBROADCASTFRAMES_GB		0x144
#define	TXUNDERFLOWERROR		0x148
#define	TXSINGLECOL_G			0x14C
#define	TXMULTICOL_G			0x150
#define	TXDEFERRED			0x154
#define	TXLATECOL			0x158
#define	TXEXESSCOL			0x15C
#define	TXCARRIERERR			0x160
#define	TXOCTETCNT			0x164
#define	TXFRAMECOUNT_G			0x168
#define	TXEXCESSDEF			0x16C
#define	TXPAUSEFRAMES			0x170
#define	TXVLANFRAMES_G			0x174
#define	TXOVERSIZE_G			0x178
#define	RXFRAMECOUNT_GB			0x180
#define	RXOCTETCOUNT_GB			0x184
#define	RXOCTETCOUNT_G			0x188
#define	RXBROADCASTFRAMES_G		0x18C
#define	RXMULTICASTFRAMES_G		0x190
#define	RXCRCERROR			0x194
#define	RXALIGNMENTERROR		0x198
#define	RXRUNTERROR			0x19C
#define	RXJABBERERROR			0x1A0
#define	RXUNDERSIZE_G			0x1A4
#define	RXOVERSIZE_G			0x1A8
#define	RX64OCTETS_GB			0x1AC
#define	RX65TO127OCTETS_GB		0x1B0
#define	RX128TO255OCTETS_GB		0x1B4
#define	RX256TO511OCTETS_GB		0x1B8
#define	RX512TO1023OCTETS_GB		0x1BC
#define	RX1024TOMAXOCTETS_GB		0x1C0
#define	RXUNICASTFRAMES_G		0x1C4
#define	RXLENGTHERROR			0x1C8
#define	RXOUTOFRANGETYPE		0x1CC
#define	RXPAUSEFRAMES			0x1D0
#define	RXFIFOOVERFLOW			0x1D4
#define	RXVLANFRAMES_GB			0x1D8
#define	RXWATCHDOGERROR			0x1DC
#define	RXRCVERROR			0x1E0
#define	RXCTRLFRAMES_G			0x1E4
#define	MMC_IPC_RECEIVE_INT_MASK	0x200
#define	MMC_IPC_RECEIVE_INT		0x208
#define	RXIPV4_GD_FRMS			0x210
#define	RXIPV4_HDRERR_FRMS		0x214
#define	RXIPV4_NOPAY_FRMS		0x218
#define	RXIPV4_FRAG_FRMS		0x21C
#define	RXIPV4_UDSBL_FRMS		0x220
#define	RXIPV6_GD_FRMS			0x224
#define	RXIPV6_HDRERR_FRMS		0x228
#define	RXIPV6_NOPAY_FRMS		0x22C
#define	RXUDP_GD_FRMS			0x230
#define	RXUDP_ERR_FRMS			0x234
#define	RXTCP_GD_FRMS			0x238
#define	RXTCP_ERR_FRMS			0x23C
#define	RXICMP_GD_FRMS			0x240
#define	RXICMP_ERR_FRMS			0x244
#define	RXIPV4_GD_OCTETS		0x250
#define	RXIPV4_HDRERR_OCTETS		0x254
#define	RXIPV4_NOPAY_OCTETS		0x258
#define	RXIPV4_FRAG_OCTETS		0x25C
#define	RXIPV4_UDSBL_OCTETS		0x260
#define	RXIPV6_GD_OCTETS		0x264
#define	RXIPV6_HDRERR_OCTETS		0x268
#define	RXIPV6_NOPAY_OCTETS		0x26C
#define	RXUDP_GD_OCTETS			0x270
#define	RXUDP_ERR_OCTETS		0x274
#define	RXTCP_GD_OCTETS			0x278
#define	RXTCPERROCTETS			0x27C
#define	RXICMP_GD_OCTETS		0x280
#define	RXICMP_ERR_OCTETS		0x284
#define	L3_L4_CONTROL0			0x400
#define	LAYER4_ADDRESS0			0x404
#define	LAYER3_ADDR0_REG0		0x410
#define	LAYER3_ADDR1_REG0		0x414
#define	LAYER3_ADDR2_REG0		0x418
#define	LAYER3_ADDR3_REG0		0x41C
#define	L3_L4_CONTROL1			0x430
#define	LAYER4_ADDRESS1			0x434
#define	LAYER3_ADDR0_REG1		0x440
#define	LAYER3_ADDR1_REG1		0x444
#define	LAYER3_ADDR2_REG1		0x448
#define	LAYER3_ADDR3_REG1		0x44C
#define	L3_L4_CONTROL2			0x460
#define	LAYER4_ADDRESS2			0x464
#define	LAYER3_ADDR0_REG2		0x470
#define	LAYER3_ADDR1_REG2		0x474
#define	LAYER3_ADDR2_REG2		0x478
#define	LAYER3_ADDR3_REG2		0x47C
#define	L3_L4_CONTROL3			0x490
#define	LAYER4_ADDRESS3			0x494
#define	LAYER3_ADDR0_REG3		0x4A0
#define	LAYER3_ADDR1_REG3		0x4A4
#define	LAYER3_ADDR2_REG3		0x4A8
#define	LAYER3_ADDR3_REG3		0x4AC
#define	HASH_TABLE_REG(n)		0x500 + (0x4 * n)
#define	VLAN_INCL_REG			0x584
#define	VLAN_HASH_TABLE_REG		0x588
#define	TIMESTAMP_CONTROL		0x700
#define	SUB_SECOND_INCREMENT		0x704
#define	SYSTEM_TIME_SECONDS		0x708
#define	SYSTEM_TIME_NANOSECONDS		0x70C
#define	SYSTEM_TIME_SECONDS_UPDATE	0x710
#define	SYSTEM_TIME_NANOSECONDS_UPDATE	0x714
#define	TIMESTAMP_ADDEND		0x718
#define	TARGET_TIME_SECONDS		0x71C
#define	TARGET_TIME_NANOSECONDS		0x720
#define	SYSTEM_TIME_HIGHER_WORD_SECONDS	0x724
#define	TIMESTAMP_STATUS		0x728
#define	PPS_CONTROL			0x72C
#define	AUXILIARY_TIMESTAMP_NANOSECONDS	0x730
#define	AUXILIARY_TIMESTAMP_SECONDS	0x734
#define	PPS0_INTERVAL			0x760
#define	PPS0_WIDTH			0x764

/* DMA */
#define	BUS_MODE		0x1000
#define	 BUS_MODE_EIGHTXPBL	(1 << 24) /* Multiplies PBL by 8 */
#define	 BUS_MODE_FIXEDBURST	(1 << 16)
#define	 BUS_MODE_PRIORXTX_SHIFT	14
#define	 BUS_MODE_PRIORXTX_41	3
#define	 BUS_MODE_PRIORXTX_31	2
#define	 BUS_MODE_PRIORXTX_21	1
#define	 BUS_MODE_PRIORXTX_11	0
#define	 BUS_MODE_PBL_SHIFT	8 /* Single block transfer size */
#define	 BUS_MODE_PBL_BEATS_8	8
#define	 BUS_MODE_SWR		(1 << 0) /* Reset */
#define	TRANSMIT_POLL_DEMAND	0x1004
#define	RECEIVE_POLL_DEMAND	0x1008
#define	RX_DESCR_LIST_ADDR	0x100C
#define	TX_DESCR_LIST_ADDR	0x1010
#define	DMA_STATUS		0x1014
#define	 DMA_STATUS_NIS		(1 << 16)
#define	 DMA_STATUS_AIS		(1 << 15)
#define	 DMA_STATUS_FBI		(1 << 13)
#define	 DMA_STATUS_RI		(1 << 6)
#define	 DMA_STATUS_TI		(1 << 0)
#define	 DMA_STATUS_INTR_MASK	0x1ffff
#define	OPERATION_MODE		0x1018
#define	 MODE_RSF		(1 << 25) /* RX Full Frame */
#define	 MODE_TSF		(1 << 21) /* TX Full Frame */
#define	 MODE_FTF		(1 << 20) /* Flush TX FIFO */
#define	 MODE_ST		(1 << 13) /* Start DMA TX */
#define	 MODE_FUF		(1 << 6)  /* TX frames < 64bytes */
#define	 MODE_RTC_LEV32		0x1
#define	 MODE_RTC_SHIFT		3
#define	 MODE_OSF		(1 << 2) /* Process Second frame */
#define	 MODE_SR		(1 << 1) /* Start DMA RX */
#define	INTERRUPT_ENABLE	0x101C
#define	 INT_EN_NIE		(1 << 16) /* Normal/Summary */
#define	 INT_EN_AIE		(1 << 15) /* Abnormal/Summary */
#define	 INT_EN_ERE		(1 << 14) /* Early receive */
#define	 INT_EN_FBE		(1 << 13) /* Fatal bus error */
#define	 INT_EN_ETE		(1 << 10) /* Early transmit */
#define	 INT_EN_RWE		(1 << 9)  /* Receive watchdog */
#define	 INT_EN_RSE		(1 << 8)  /* Receive stopped */
#define	 INT_EN_RUE		(1 << 7)  /* Recv buf unavailable */
#define	 INT_EN_RIE		(1 << 6)  /* Receive interrupt */
#define	 INT_EN_UNE		(1 << 5)  /* Tx underflow */
#define	 INT_EN_OVE		(1 << 4)  /* Receive overflow */
#define	 INT_EN_TJE		(1 << 3)  /* Transmit jabber */
#define	 INT_EN_TUE		(1 << 2)  /* Tx. buf unavailable */
#define	 INT_EN_TSE		(1 << 1)  /* Transmit stopped */
#define	 INT_EN_TIE		(1 << 0)  /* Transmit interrupt */
#define	 INT_EN_DEFAULT		(INT_EN_TIE|INT_EN_RIE|	\
	    INT_EN_NIE|INT_EN_AIE|			\
	    INT_EN_FBE|INT_EN_UNE)

#define	MISSED_FRAMEBUF_OVERFLOW_CNTR	0x1020
#define	RECEIVE_INT_WATCHDOG_TMR	0x1024
#define	AXI_BUS_MODE			0x1028
#define	AHB_OR_AXI_STATUS		0x102C
#define	CURRENT_HOST_TRANSMIT_DESCR	0x1048
#define	CURRENT_HOST_RECEIVE_DESCR	0x104C
#define	CURRENT_HOST_TRANSMIT_BUF_ADDR	0x1050
#define	CURRENT_HOST_RECEIVE_BUF_ADDR	0x1054
#define	HW_FEATURE			0x1058

#define	DWC_GMAC			0x1
#define	DWC_GMAC_ALT_DESC		0x2
#define	GMAC_MII_CLK_60_100M_DIV42	0x0
#define	GMAC_MII_CLK_100_150M_DIV62	0x1
#define	GMAC_MII_CLK_25_35M_DIV16	0x2
#define	GMAC_MII_CLK_35_60M_DIV26	0x3
#define	GMAC_MII_CLK_150_250M_DIV102	0x4
#define	GMAC_MII_CLK_250_300M_DIV124	0x5
#define	GMAC_MII_CLK_DIV4		0x8
#define	GMAC_MII_CLK_DIV6		0x9
#define	GMAC_MII_CLK_DIV8		0xa
#define	GMAC_MII_CLK_DIV10		0xb
#define	GMAC_MII_CLK_DIV12		0xc
#define	GMAC_MII_CLK_DIV14		0xd
#define	GMAC_MII_CLK_DIV16		0xe
#define	GMAC_MII_CLK_DIV18		0xf

#endif	/* __IF_DWC_H__ */
