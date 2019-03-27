/*-
 * SPDX-License-Identifier: ISC
 *
 * Copyright (c) 2002-2008 Sam Leffler, Errno Consulting
 * Copyright (c) 2002-2004 Atheros Communications, Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
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
 *
 * $FreeBSD$
 */
#ifndef _DEV_ATH_AR5210REG_H
#define _DEV_ATH_AR5210REG_H

/*
 * Register defintions for the Atheros AR5210/5110 MAC/Basedband
 * Processor for IEEE 802.11a 5-GHz Wireless LANs.
 */

#ifndef PCI_VENDOR_ATHEROS
#define	PCI_VENDOR_ATHEROS		0x168c
#endif
#define	PCI_PRODUCT_ATHEROS_AR5210	0x0007
#define	PCI_PRODUCT_ATHEROS_AR5210_OLD	0x0004

/* DMA Registers */
#define	AR_TXDP0	0x0000		/* TX queue pointer 0 register */
#define	AR_TXDP1	0x0004		/* TX queue pointer 1 register */
#define	AR_CR		0x0008		/* Command register */
#define	AR_RXDP		0x000c		/* RX queue descriptor ptr register */
#define	AR_CFG		0x0014		/* Configuration and status register */
#define	AR_ISR		0x001c		/* Interrupt status register */
#define	AR_IMR		0x0020		/* Interrupt mask register */
#define	AR_IER		0x0024		/* Interrupt global enable register */
#define	AR_BCR		0x0028		/* Beacon control register */
#define	AR_BSR		0x002c		/* Beacon status register */
#define	AR_TXCFG	0x0030		/* TX configuration register */
#define	AR_RXCFG	0x0034		/* RX configuration register */
#define	AR_MIBC		0x0040		/* MIB control register */
#define	AR_TOPS		0x0044		/* Timeout prescale register */
#define	AR_RXNOFRM	0x0048		/* RX no frame timeout register */
#define	AR_TXNOFRM	0x004c		/* TX no frame timeout register */
#define	AR_RPGTO	0x0050		/* RX frame gap timeout register */
#define	AR_RFCNT	0x0054		/* RX frame count limit register */
#define	AR_MISC		0x0058		/* Misc control and status register */
#define	AR_RC		0x4000		/* Reset control */
#define	AR_SCR		0x4004		/* Sleep control */
#define	AR_INTPEND	0x4008		/* Interrupt pending */
#define	AR_SFR		0x400c		/* Force sleep */
#define	AR_PCICFG	0x4010		/* PCI configuration */
#define	AR_GPIOCR	0x4014		/* GPIO configuration */
#define	AR_GPIODO	0x4018		/* GPIO data output */
#define	AR_GPIODI	0x401c		/* GPIO data input */
#define	AR_SREV		0x4020		/* Silicon revision */
/* EEPROM Access Registers */
#define	AR_EP_AIR_BASE	0x6000		/* EEPROM access initiation regs base */
#define	AR_EP_AIR(n)	(AR_EP_AIR_BASE + (n)*4)
#define	AR_EP_RDATA	0x6800		/* EEPROM read data register */
#define	AR_EP_STA	0x6c00		/* EEPROM access status register */
/* PCU Registers */
#define	AR_STA_ID0	0x8000		/* Lower 32bits of MAC address */
#define	AR_STA_ID1	0x8004		/* Upper 16bits of MAC address */
#define	AR_BSS_ID0	0x8008		/* Lower 32bits of BSSID */
#define	AR_BSS_ID1	0x800c		/* Upper 16bits of BSSID */
#define	AR_SLOT_TIME	0x8010		/* Length of a back-off */
#define	AR_TIME_OUT	0x8014		/* Timeout to wait for ACK and CTS */
#define	AR_RSSI_THR	0x8018		/* Beacon RSSI warning threshold */
#define	AR_RETRY_LMT	0x801c		/* Short and long frame retry limit */
#define	AR_USEC		0x8020		/* Transmit latency */
#define	AR_BEACON	0x8024		/* Beacon control */
#define	AR_CFP_PERIOD	0x8028		/* CFP period */
#define	AR_TIMER0	0x802c		/* Next beacon time */
#define	AR_TIMER1	0x8030		/* Next DMA beacon alert time */
#define	AR_TIMER2	0x8034		/* Next software beacon alert time */
#define	AR_TIMER3	0x8038		/* Next ATIM window time */
#define	AR_IFS0		0x8040		/* Protocol timers */
#define	AR_IFS1		0x8044		/* Protocol time and control */
#define	AR_CFP_DUR	0x8048		/* Maximum CFP duration */
#define	AR_RX_FILTER	0x804c		/* Receive filter */
#define	AR_MCAST_FIL0	0x8050		/* Lower 32bits of mcast filter mask */
#define	AR_MCAST_FIL1	0x8054		/* Upper 16bits of mcast filter mask */
#define	AR_TX_MASK0	0x8058		/* Lower 32bits of TX mask */
#define	AR_TX_MASK1	0x805c		/* Upper 16bits of TX mask */
#define	AR_CLR_TMASK	0x8060		/* Clear TX mask */
#define	AR_TRIG_LEV	0x8064		/* Minimum FIFO fill level before TX */
#define	AR_DIAG_SW	0x8068		/* PCU control */
#define	AR_TSF_L32	0x806c		/* Lower 32bits of local clock */
#define	AR_TSF_U32	0x8070		/* Upper 32bits of local clock */
#define	AR_LAST_TSTP	0x8080		/* Lower 32bits of last beacon tstamp */
#define	AR_RETRY_CNT	0x8084		/* Current short or long retry cnt */
#define	AR_BACKOFF	0x8088		/* Back-off status */
#define	AR_NAV		0x808c		/* Current NAV value */
#define	AR_RTS_OK	0x8090		/* RTS success counter */
#define	AR_RTS_FAIL	0x8094		/* RTS failure counter */
#define	AR_ACK_FAIL	0x8098		/* ACK failure counter */
#define	AR_FCS_FAIL	0x809c		/* FCS failure counter */
#define	AR_BEACON_CNT	0x80a0		/* Valid beacon counter */
#define	AR_KEYTABLE_0	0x9000		/* Encryption key table */
#define	AR_KEYTABLE(n)	(AR_KEYTABLE_0 + ((n)*32))

#define	AR_CR_TXE0		0x00000001	/* TX queue 0 enable */
#define	AR_CR_TXE1		0x00000002	/* TX queue 1 enable */
#define	AR_CR_RXE		0x00000004	/* RX enable */
#define	AR_CR_TXD0		0x00000008	/* TX queue 0 disable */
#define	AR_CR_TXD1		0x00000010	/* TX queue 1 disable */
#define	AR_CR_RXD		0x00000020	/* RX disable */
#define	AR_CR_SWI		0x00000040	/* software interrupt */
#define	AR_CR_BITS \
	"\20\1TXE0\2TXE1\3RXE\4TXD0\5TXD1\6RXD\7SWI"

#define	AR_CFG_SWTD		0x00000001	/* BE for TX desc */
#define	AR_CFG_SWTB		0x00000002	/* BE for TX data */
#define	AR_CFG_SWRD		0x00000004	/* BE for RX desc */
#define	AR_CFG_SWRB		0x00000008	/* BE for RX data */
#define	AR_CFG_SWRG		0x00000010	/* BE for registers */
#define	AR_CFG_EEBS		0x00000200	/* EEPROM busy */
#define	AR_CFG_TXCNT		0x00007800	/* number of TX desc in Q */
#define	AR_CFG_TXCNT_S		11
#define	AR_CFG_TXFSTAT		0x00008000	/* TX DMA status */
#define	AR_CFG_TXFSTRT		0x00010000	/* re-enable TX DMA */
#define	AR_CFG_BITS \
	"\20\1SWTD\2SWTB\3SWRD\4SWRB\5SWRG\14EEBS\17TXFSTAT\20TXFSTRT"

#define	AR_ISR_RXOK_INT		0x00000001	/* RX frame OK */
#define	AR_ISR_RXDESC_INT	0x00000002	/* RX intr request */
#define	AR_ISR_RXERR_INT	0x00000004	/* RX error */
#define	AR_ISR_RXNOFRM_INT	0x00000008	/* no frame received */
#define	AR_ISR_RXEOL_INT	0x00000010	/* RX desc empty */
#define	AR_ISR_RXORN_INT	0x00000020	/* RX fifo overrun */
#define	AR_ISR_TXOK_INT		0x00000040	/* TX frame OK */
#define	AR_ISR_TXDESC_INT	0x00000080	/* TX intr request */
#define	AR_ISR_TXERR_INT	0x00000100	/* TX error */
#define	AR_ISR_TXNOFRM_INT	0x00000200	/* no frame transmitted */
#define	AR_ISR_TXEOL_INT	0x00000400	/* TX desc empty */
#define	AR_ISR_TXURN_INT	0x00000800	/* TX fifo underrun */
#define	AR_ISR_MIB_INT		0x00001000	/* MIB interrupt */
#define	AR_ISR_SWI_INT		0x00002000	/* software interrupt */
#define	AR_ISR_RXPHY_INT	0x00004000	/* PHY RX error */
#define	AR_ISR_RXKCM_INT	0x00008000	/* Key cache miss */
#define	AR_ISR_SWBA_INT		0x00010000	/* software beacon alert */
#define	AR_ISR_BRSSI_INT	0x00020000	/* beacon threshold */
#define	AR_ISR_BMISS_INT	0x00040000	/* beacon missed */
#define	AR_ISR_MCABT_INT	0x00100000	/* master cycle abort */
#define	AR_ISR_SSERR_INT	0x00200000	/* SERR on PCI */
#define	AR_ISR_DPERR_INT	0x00400000	/* Parity error on PCI */
#define	AR_ISR_GPIO_INT		0x01000000	/* GPIO interrupt */
#define	AR_ISR_BITS \
	"\20\1RXOK\2RXDESC\3RXERR\4RXNOFM\5RXEOL\6RXORN\7TXOK\10TXDESC"\
	"\11TXERR\12TXNOFRM\13TXEOL\14TXURN\15MIB\16SWI\17RXPHY\20RXKCM"\
	"\21SWBA\22BRSSI\23BMISS\24MCABT\25SSERR\26DPERR\27GPIO"

#define	AR_IMR_RXOK_INT		0x00000001	/* RX frame OK */
#define	AR_IMR_RXDESC_INT	0x00000002	/* RX intr request */
#define	AR_IMR_RXERR_INT	0x00000004	/* RX error */
#define	AR_IMR_RXNOFRM_INT	0x00000008	/* no frame received */
#define	AR_IMR_RXEOL_INT	0x00000010	/* RX desc empty */
#define	AR_IMR_RXORN_INT	0x00000020	/* RX fifo overrun */
#define	AR_IMR_TXOK_INT		0x00000040	/* TX frame OK */
#define	AR_IMR_TXDESC_INT	0x00000080	/* TX intr request */
#define	AR_IMR_TXERR_INT	0x00000100	/* TX error */
#define	AR_IMR_TXNOFRM_INT	0x00000200	/* no frame transmitted */
#define	AR_IMR_TXEOL_INT	0x00000400	/* TX desc empty */
#define	AR_IMR_TXURN_INT	0x00000800	/* TX fifo underrun */
#define	AR_IMR_MIB_INT		0x00001000	/* MIB interrupt */
#define	AR_IMR_SWI_INT		0x00002000	/* software interrupt */
#define	AR_IMR_RXPHY_INT	0x00004000	/* PHY RX error */
#define	AR_IMR_RXKCM_INT	0x00008000	/* Key cache miss */
#define	AR_IMR_SWBA_INT		0x00010000	/* software beacon alert */
#define	AR_IMR_BRSSI_INT	0x00020000	/* beacon threshold */
#define	AR_IMR_BMISS_INT	0x00040000	/* beacon missed */
#define	AR_IMR_MCABT_INT	0x00100000	/* master cycle abort */
#define	AR_IMR_SSERR_INT	0x00200000	/* SERR on PCI */
#define	AR_IMR_DPERR_INT	0x00400000	/* Parity error on PCI */
#define	AR_IMR_GPIO_INT		0x01000000	/* GPIO interrupt */
#define	AR_IMR_BITS	AR_ISR_BITS

#define	AR_IER_DISABLE		0x00000000	/* pseudo-flag */
#define	AR_IER_ENABLE		0x00000001	/* global interrupt enable */
#define	AR_IER_BITS	"\20\1ENABLE"

#define	AR_BCR_BCMD		0x00000001	/* ad hoc beacon mode */
#define	AR_BCR_BDMAE		0x00000002	/* beacon DMA enable */
#define	AR_BCR_TQ1FV		0x00000004	/* use TXQ1 for non-beacon */
#define	AR_BCR_TQ1V		0x00000008	/* TXQ1 valid for beacon */
#define	AR_BCR_BCGET		0x00000010	/* force a beacon fetch */
#define	AR_BCR_BITS	"\20\1BCMD\2BDMAE\3TQ1FV\4TQ1V\5BCGET"

#define	AR_BSR_BDLYSW		0x00000001	/* software beacon delay */
#define	AR_BSR_BDLYDMA		0x00000002	/* DMA beacon delay */
#define	AR_BSR_TXQ1F		0x00000004	/* TXQ1 fetch */
#define	AR_BSR_ATIMDLY		0x00000008	/* ATIM delay */
#define	AR_BSR_SNPBCMD		0x00000100	/* snapshot of BCMD */
#define	AR_BSR_SNPBDMAE		0x00000200	/* snapshot of BDMAE */
#define	AR_BSR_SNPTQ1FV		0x00000400	/* snapshot of TQ1FV */
#define	AR_BSR_SNPTQ1V		0x00000800	/* snapshot of TQ1V */
#define	AR_BSR_SNAPPEDBCRVALID	0x00001000	/* snapshot of BCR are valid */
#define	AR_BSR_SWBA_CNT		0x00ff0000	/* software beacon alert cnt */
#define	AR_BSR_BITS \
	"\20\1BDLYSW\2BDLYDMA\3TXQ1F\4ATIMDLY\11SNPBCMD\12SNPBDMAE"\
	"\13SNPTQ1FV\14SNPTQ1V\15SNAPPEDBCRVALID"

#define	AR_TXCFG_SDMAMR		0x00000007	/* DMA burst size 2^(2+x) */
#define	AR_TXCFG_TXFSTP		0x00000008	/* Stop TX DMA on filtered */
#define	AR_TXCFG_TXFULL		0x00000070	/* TX DMA desc Q full thresh */
#define	AR_TXCFG_TXCONT_EN	0x00000080	/* Enable continuous TX mode */
#define	AR_TXCFG_BITS	"\20\3TXFSTP\7TXCONT_EN"

#define	AR_RXCFG_SDMAMW		0x00000007	/* DMA burst size 2^(2+x) */
#define	AR_RXCFG_ZLFDMA		0x00000010	/* enable zero length DMA */

/* DMA sizes used for both AR_TXCFG_SDMAMR and AR_RXCFG_SDMAMW */
#define	AR_DMASIZE_4B		0		/* DMA size 4 bytes */
#define	AR_DMASIZE_8B		1		/* DMA size 8 bytes */
#define	AR_DMASIZE_16B		2		/* DMA size 16 bytes */
#define	AR_DMASIZE_32B		3		/* DMA size 32 bytes */
#define	AR_DMASIZE_64B		4		/* DMA size 64 bytes */
#define	AR_DMASIZE_128B		5		/* DMA size 128 bytes */
#define	AR_DMASIZE_256B		6		/* DMA size 256 bytes */
#define	AR_DMASIZE_512B		7		/* DMA size 512 bytes */

#define	AR_MIBC_COW		0x00000001	/* counter overflow warning */
#define	AR_MIBC_FMC		0x00000002	/* freeze MIB counters */
#define	AR_MIBC_CMC		0x00000004	/* clear MIB counters */
#define	AR_MIBC_MCS		0x00000008	/* MIB counter strobe */

#define	AR_RFCNT_RFCL		0x0000000f	/* RX frame count limit */

#define	AR_MISC_LED_DECAY	0x001c0000	/* LED decay rate */
#define	AR_MISC_LED_BLINK	0x00e00000	/* LED blink rate */

#define	AR_RC_RPCU		0x00000001	/* PCU Warm Reset */
#define	AR_RC_RDMA		0x00000002	/* DMA Warm Reset */
#define	AR_RC_RMAC		0x00000004	/* MAC Warm Reset */
#define	AR_RC_RPHY		0x00000008	/* PHY Warm Reset */
#define	AR_RC_RPCI		0x00000010	/* PCI Core Warm Reset */
#define	AR_RC_BITS	"\20\1RPCU\2RDMA\3RMAC\4RPHY\5RPCI"

#define	AR_SCR_SLDUR		0x0000ffff	/* sleep duration */
#define	AR_SCR_SLE		0x00030000	/* sleep enable */
#define	AR_SCR_SLE_S		16
/*
 * The previous values for the following three defines were:
 *
 *	AR_SCR_SLE_WAKE		0x00000000
 *	AR_SCR_SLE_SLP		0x00010000
 *	AR_SCR_SLE_ALLOW	0x00020000
 *
 * However, these have been pre-shifted with AR_SCR_SLE_S.  The
 * OS_REG_READ() macro would attempt to shift them again, effectively
 * shifting out any of the set bits completely.
 */
#define	AR_SCR_SLE_WAKE		0		/* force wake */
#define	AR_SCR_SLE_SLP		1		/* force sleep */
#define	AR_SCR_SLE_ALLOW	2		/* allow to control sleep */
#define	AR_SCR_BITS	"\20\20SLE_SLP\21SLE_ALLOW"

#define	AR_INTPEND_IP		0x00000001	/* interrupt pending */
#define	AR_INTPEND_BITS	"\20\1IP"

#define	AR_SFR_SF		0x00000001	/* force sleep immediately */

#define	AR_PCICFG_EEPROMSEL	0x00000001	/* EEPROM access enable */
#define	AR_PCICFG_CLKRUNEN	0x00000004	/* CLKRUN enable */
#define	AR_PCICFG_LED_PEND	0x00000020	/* LED for assoc pending */
#define	AR_PCICFG_LED_ACT	0x00000040	/* LED for assoc active */
#define	AR_PCICFG_SL_INTEN	0x00000800	/* Enable sleep intr */
#define	AR_PCICFG_LED_BCTL	0x00001000	/* LED blink for local act */
#define	AR_PCICFG_SL_INPEN	0x00002800	/* sleep even intr pending */
#define	AR_PCICFG_SPWR_DN	0x00010000	/* sleep indication */
#define	AR_PCICFG_BITS \
	"\20\1EEPROMSEL\3CLKRUNEN\5LED_PEND\6LED_ACT\13SL_INTEN"\
	"\14LED_BCTL\20SPWR_DN"

#define	AR_GPIOCR_IN(n)		(0<<((n)*2))	/* input-only */
#define	AR_GPIOCR_OUT0(n)	(1<<((n)*2))	/* output-only if GPIODO = 0 */
#define	AR_GPIOCR_OUT1(n)	(2<<((n)*2))	/* output-only if GPIODO = 1 */
#define	AR_GPIOCR_OUT(n)	(3<<((n)*2))	/* always output */
#define	AR_GPIOCR_ALL(n)	(3<<((n)*2))	/* all bits for pin */
#define	AR_GPIOCR_INT_SEL(n)	((n)<<12)	/* GPIO interrupt pin select */
#define	AR_GPIOCR_INT_ENA	0x00008000	/* Enable GPIO interrupt */
#define	AR_GPIOCR_INT_SELL	0x00000000	/* Interrupt if pin is low */
#define	AR_GPIOCR_INT_SELH	0x00010000	/* Interrupt if pin is high */

#define	AR_SREV_CRETE		4		/* Crete 1st version */
#define	AR_SREV_CRETE_MS	5		/* Crete FCS version */
#define	AR_SREV_CRETE_23	8		/* Crete version 2.3 */

#define	AR_EP_STA_RDERR		0x00000001	/* read error */
#define	AR_EP_STA_RDCMPLT	0x00000002	/* read complete */
#define	AR_EP_STA_WRERR		0x00000004	/* write error */
#define	AR_EP_STA_WRCMPLT	0x00000008	/* write complete */
#define	AR_EP_STA_BITS \
	"\20\1RDERR\2RDCMPLT\3WRERR\4WRCMPLT"

#define	AR_STA_ID1_AP		0x00010000	/* Access Point Operation */
#define	AR_STA_ID1_ADHOC	0x00020000	/* ad hoc Operation */
#define	AR_STA_ID1_PWR_SV	0x00040000	/* power save report enable */
#define	AR_STA_ID1_NO_KEYSRCH	0x00080000	/* key table search disable */
#define	AR_STA_ID1_NO_PSPOLL	0x00100000	/* auto PS-POLL disable */
#define	AR_STA_ID1_PCF		0x00200000	/* PCF observation enable */
#define	AR_STA_ID1_DESC_ANTENNA 0x00400000	/* use antenna in TX desc */
#define	AR_STA_ID1_DEFAULT_ANTENNA 0x00800000	/* toggle default antenna */
#define	AR_STA_ID1_ACKCTS_6MB	0x01000000	/* use 6Mbps for ACK/CTS */
#define	AR_STA_ID1_BITS \
	"\20\20AP\21ADHOC\22PWR_SV\23NO_KEYSRCH\24NO_PSPOLL\25PCF"\
	"\26DESC_ANTENNA\27DEFAULT_ANTENNA\30ACKCTS_6MB"

#define	AR_BSS_ID1_AID		0xffff0000	/* association ID */
#define	AR_BSS_ID1_AID_S	16

#define	AR_TIME_OUT_ACK		0x00001fff	/* ACK timeout */
#define	AR_TIME_OUT_ACK_S	0
#define	AR_TIME_OUT_CTS		0x1fff0000	/* CTS timeout */
#define	AR_TIME_OUT_CTS_S	16

#define	AR_RSSI_THR_BM_THR	0x00000700	/* missed beacon threshold */
#define	AR_RSSI_THR_BM_THR_S	8

#define	AR_RETRY_LMT_SH_RETRY	0x0000000f	/* short frame retry limit */
#define	AR_RETRY_LMT_SH_RETRY_S	0
#define	AR_RETRY_LMT_LG_RETRY	0x000000f0	/* long frame retry limit */
#define	AR_RETRY_LMT_LG_RETRY_S	4
#define	AR_RETRY_LMT_SSH_RETRY	0x00003f00	/* short station retry limit */
#define	AR_RETRY_LMT_SSH_RETRY_S	8
#define	AR_RETRY_LMT_SLG_RETRY	0x000fc000	/* long station retry limit */
#define	AR_RETRY_LMT_SLG_RETRY_S	14
#define	AR_RETRY_LMT_CW_MIN	0x3ff00000	/* minimum contention window */
#define	AR_RETRY_LMT_CW_MIN_S		20

#define	AR_USEC_1		0x0000007f	/* number of clk in 1us */
#define	AR_USEC_1_S		0
#define	AR_USEC_32		0x00003f80	/* number of 32MHz clk in 1us */
#define	AR_USEC_32_S		7
#define	AR_USEC_TX_LATENCY	0x000fc000	/* transmit latency in us */
#define	AR_USEC_TX_LATENCY_S	14
#define	AR_USEC_RX_LATENCY	0x03f00000	/* receive latency in us */
#define	AR_USEC_RX_LATENCY_S	20

#define	AR_BEACON_PERIOD	0x0000ffff	/* beacon period in TU/ms */
#define	AR_BEACON_PERIOD_S	0
#define	AR_BEACON_TIM 		0x007f0000	/* byte offset */
#define	AR_BEACON_TIM_S	16
#define	AR_BEACON_EN		0x00800000	/* beacon transmission enable */
#define	AR_BEACON_RESET_TSF 	0x01000000	/* TSF reset oneshot */
#define	AR_BEACON_BITS	"\20\27ENABLE\30RESET_TSF"

#define	AR_IFS0_SIFS		0x000007ff	/* SIFS in core clock cycles */
#define	AR_IFS0_SIFS_S		0
#define	AR_IFS0_DIFS		0x007ff800	/* DIFS in core clock cycles */
#define	AR_IFS0_DIFS_S		11

#define	AR_IFS1_PIFS		0x00000fff	/* Programmable IFS */
#define	AR_IFS1_PIFS_S		0
#define	AR_IFS1_EIFS		0x03fff000	/* EIFS in core clock cycles */
#define	AR_IFS1_EIFS_S		12
#define	AR_IFS1_CS_EN		0x04000000	/* carrier sense enable */

#define	AR_RX_FILTER_UNICAST	0x00000001	/* unicast frame enable */
#define	AR_RX_FILTER_MULTICAST	0x00000002	/* multicast frame enable */
#define	AR_RX_FILTER_BROADCAST	0x00000004	/* broadcast frame enable */
#define	AR_RX_FILTER_CONTROL	0x00000008	/* control frame enable */
#define	AR_RX_FILTER_BEACON	0x00000010	/* beacon frame enable */
#define	AR_RX_FILTER_PROMISCUOUS 0x00000020	/* promiscuous receive enable */
#define	AR_RX_FILTER_BITS \
	"\20\1UCAST\2MCAST\3BCAST\4CONTROL\5BEACON\6PROMISC"

#define	AR_DIAG_SW_DIS_WEP_ACK	0x00000001	/* disable ACK if no key found*/
#define	AR_DIAG_SW_DIS_ACK	0x00000002	/* disable ACK generation */
#define	AR_DIAG_SW_DIS_CTS	0x00000004	/* disable CTS generation */
#define	AR_DIAG_SW_DIS_ENC	0x00000008	/* encryption disable */
#define	AR_DIAG_SW_DIS_DEC	0x00000010	/* decryption disable */
#define	AR_DIAG_SW_DIS_TX	0x00000020	/* TX disable */
#define	AR_DIAG_SW_DIS_RX	0x00000040	/* RX disable */
#define	AR_DIAG_SW_LOOP_BACK	0x00000080	/* TX data loopback enable */
#define	AR_DIAG_SW_CORR_FCS	0x00000100	/* corrupt FCS enable */
#define	AR_DIAG_SW_CHAN_INFO	0x00000200	/* channel information enable */
#define	AR_DIAG_SW_EN_SCRAM_SEED 0x00000400	/* use fixed scrambler seed */
#define	AR_DIAG_SW_SCVRAM_SEED	0x0003f800	/* fixed scrambler seed */
#define	AR_DIAG_SW_DIS_SEQ_INC	0x00040000	/* seq increment disable */
#define	AR_DIAG_SW_FRAME_NV0	0x00080000	/* accept frame vers != 0 */
#define	AR_DIAG_SW_DIS_CRYPTO	(AR_DIAG_SW_DIS_ENC | AR_DIAG_SW_DIS_DEC)
#define	AR_DIAG_SW_BITS \
	"\20\1DIS_WEP_ACK\2DIS_ACK\3DIS_CTS\4DIS_ENC\5DIS_DEC\6DIS_TX"\
	"\7DIS_RX\10LOOP_BACK\11CORR_FCS\12CHAN_INFO\13EN_SCRAM_SEED"\
	"\22DIS_SEQ_INC\24FRAME_NV0"

#define	AR_RETRY_CNT_SSH	0x0000003f	/* current short retry count */
#define	AR_RETRY_CNT_SLG	0x00000fc0	/* current long retry count */

#define	AR_BACKOFF_CW		0x000003ff	/* current contention window */
#define	AR_BACKOFF_CNT		0x03ff0000	/* backoff count */

#define	AR_KEYTABLE_KEY0(n)	(AR_KEYTABLE(n) + 0)	/* key bit 0-31 */
#define	AR_KEYTABLE_KEY1(n)	(AR_KEYTABLE(n) + 4)	/* key bit 32-47 */
#define	AR_KEYTABLE_KEY2(n)	(AR_KEYTABLE(n) + 8)	/* key bit 48-79 */
#define	AR_KEYTABLE_KEY3(n)	(AR_KEYTABLE(n) + 12)	/* key bit 80-95 */
#define	AR_KEYTABLE_KEY4(n)	(AR_KEYTABLE(n) + 16)	/* key bit 96-127 */
#define	AR_KEYTABLE_TYPE(n)	(AR_KEYTABLE(n) + 20)	/* key type */
#define	AR_KEYTABLE_TYPE_40	0x00000000	/* 40 bit key */
#define	AR_KEYTABLE_TYPE_104	0x00000001	/* 104 bit key */
#define	AR_KEYTABLE_TYPE_128	0x00000003	/* 128 bit key */
#define	AR_KEYTABLE_MAC0(n)	(AR_KEYTABLE(n) + 24)	/* MAC address 1-32 */
#define	AR_KEYTABLE_MAC1(n)	(AR_KEYTABLE(n) + 28)	/* MAC address 33-47 */
#define	AR_KEYTABLE_VALID	0x00008000	/* key and MAC address valid */

#endif /* _DEV_ATH_AR5210REG_H */
