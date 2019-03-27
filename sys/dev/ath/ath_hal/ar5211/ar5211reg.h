/*-
 * SPDX-License-Identifier: ISC
 *
 * Copyright (c) 2002-2008 Sam Leffler, Errno Consulting
 * Copyright (c) 2002-2006 Atheros Communications, Inc.
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
#ifndef _DEV_ATH_AR5211REG_H
#define _DEV_ATH_AR5211REG_H

/*
 * Definitions for the Atheros AR5211/5311 chipset.
 */

/*
 * Maui2/Spirit specific registers/fields are indicated by AR5311.
 * Oahu specific registers/fields are indicated by AR5211.
 */

/* DMA Control and Interrupt Registers */
#define	AR_CR		0x0008		/* control register */
#define	AR_RXDP		0x000C		/* receive queue descriptor pointer */
#define	AR_CFG		0x0014		/* configuration and status register */
#define	AR_IER		0x0024		/* Interrupt enable register */
#define	AR_RTSD0	0x0028		/* RTS Duration Parameters 0 */
#define	AR_RTSD1	0x002c		/* RTS Duration Parameters 1 */
#define	AR_TXCFG	0x0030		/* tx DMA size config register */
#define	AR_RXCFG	0x0034		/* rx DMA size config register */
#define	AR5211_JUMBO_LAST	0x0038	/* Jumbo descriptor last address */
#define	AR_MIBC		0x0040		/* MIB control register */
#define	AR_TOPS		0x0044		/* timeout prescale count */
#define	AR_RXNPTO	0x0048		/* no frame received timeout */
#define	AR_TXNPTO	0x004C		/* no frame trasmitted timeout */
#define	AR_RFGTO	0x0050		/* receive frame gap timeout */
#define	AR_RFCNT	0x0054		/* receive frame count limit */
#define	AR_MACMISC	0x0058		/* miscellaneous control/status */
#define	AR5311_QDCLKGATE	0x005c	/* QCU/DCU clock gating control */
#define	AR_ISR		0x0080		/* Primary interrupt status register */
#define	AR_ISR_S0	0x0084		/* Secondary interrupt status reg 0 */
#define	AR_ISR_S1	0x0088		/* Secondary interrupt status reg 1 */
#define	AR_ISR_S2	0x008c		/* Secondary interrupt status reg 2 */
#define	AR_ISR_S3	0x0090		/* Secondary interrupt status reg 3 */
#define	AR_ISR_S4	0x0094		/* Secondary interrupt status reg 4 */
#define	AR_IMR		0x00a0		/* Primary interrupt mask register */
#define	AR_IMR_S0	0x00a4		/* Secondary interrupt mask reg 0 */
#define	AR_IMR_S1	0x00a8		/* Secondary interrupt mask reg 1 */
#define	AR_IMR_S2	0x00ac		/* Secondary interrupt mask reg 2 */
#define	AR_IMR_S3	0x00b0		/* Secondary interrupt mask reg 3 */
#define	AR_IMR_S4	0x00b4		/* Secondary interrupt mask reg 4 */
#define	AR_ISR_RAC	0x00c0		/* Primary interrupt status reg, */
/* Shadow copies with read-and-clear access */
#define	AR_ISR_S0_S	0x00c4		/* Secondary interrupt status reg 0 */
#define	AR_ISR_S1_S	0x00c8		/* Secondary interrupt status reg 1 */
#define	AR_ISR_S2_S	0x00cc		/* Secondary interrupt status reg 2 */
#define	AR_ISR_S3_S	0x00d0		/* Secondary interrupt status reg 3 */
#define	AR_ISR_S4_S	0x00d4		/* Secondary interrupt status reg 4 */

#define	AR_Q0_TXDP	0x0800		/* Transmit Queue descriptor pointer */
#define	AR_Q1_TXDP	0x0804		/* Transmit Queue descriptor pointer */
#define	AR_Q2_TXDP	0x0808		/* Transmit Queue descriptor pointer */
#define	AR_Q3_TXDP	0x080c		/* Transmit Queue descriptor pointer */
#define	AR_Q4_TXDP	0x0810		/* Transmit Queue descriptor pointer */
#define	AR_Q5_TXDP	0x0814		/* Transmit Queue descriptor pointer */
#define	AR_Q6_TXDP	0x0818		/* Transmit Queue descriptor pointer */
#define	AR_Q7_TXDP	0x081c		/* Transmit Queue descriptor pointer */
#define	AR_Q8_TXDP	0x0820		/* Transmit Queue descriptor pointer */
#define	AR_Q9_TXDP	0x0824		/* Transmit Queue descriptor pointer */
#define	AR_QTXDP(i)	(AR_Q0_TXDP + ((i)<<2))

#define	AR_Q_TXE	0x0840		/* Transmit Queue enable */
#define	AR_Q_TXD	0x0880		/* Transmit Queue disable */

#define	AR_Q0_CBRCFG	0x08c0		/* CBR configuration */
#define	AR_Q1_CBRCFG	0x08c4		/* CBR configuration */
#define	AR_Q2_CBRCFG	0x08c8		/* CBR configuration */
#define	AR_Q3_CBRCFG	0x08cc		/* CBR configuration */
#define	AR_Q4_CBRCFG	0x08d0		/* CBR configuration */
#define	AR_Q5_CBRCFG	0x08d4		/* CBR configuration */
#define	AR_Q6_CBRCFG	0x08d8		/* CBR configuration */
#define	AR_Q7_CBRCFG	0x08dc		/* CBR configuration */
#define	AR_Q8_CBRCFG	0x08e0		/* CBR configuration */
#define	AR_Q9_CBRCFG	0x08e4		/* CBR configuration */
#define	AR_QCBRCFG(i)	(AR_Q0_CBRCFG + ((i)<<2))

#define	AR_Q0_RDYTIMECFG	0x0900	/* ReadyTime configuration */
#define	AR_Q1_RDYTIMECFG	0x0904	/* ReadyTime configuration */
#define	AR_Q2_RDYTIMECFG	0x0908	/* ReadyTime configuration */
#define	AR_Q3_RDYTIMECFG	0x090c	/* ReadyTime configuration */
#define	AR_Q4_RDYTIMECFG	0x0910	/* ReadyTime configuration */
#define	AR_Q5_RDYTIMECFG	0x0914	/* ReadyTime configuration */
#define	AR_Q6_RDYTIMECFG	0x0918	/* ReadyTime configuration */
#define	AR_Q7_RDYTIMECFG	0x091c	/* ReadyTime configuration */
#define	AR_Q8_RDYTIMECFG	0x0920	/* ReadyTime configuration */
#define	AR_Q9_RDYTIMECFG	0x0924	/* ReadyTime configuration */
#define	AR_QRDYTIMECFG(i)	(AR_Q0_RDYTIMECFG + ((i)<<2))

#define	AR_Q_ONESHOTARM_SC	0x0940	/* OneShotArm set control */
#define	AR_Q_ONESHOTARM_CC	0x0980	/* OneShotArm clear control */

#define	AR_Q0_MISC	0x09c0		/* Miscellaneous QCU settings */
#define	AR_Q1_MISC	0x09c4		/* Miscellaneous QCU settings */
#define	AR_Q2_MISC	0x09c8		/* Miscellaneous QCU settings */
#define	AR_Q3_MISC	0x09cc		/* Miscellaneous QCU settings */
#define	AR_Q4_MISC	0x09d0		/* Miscellaneous QCU settings */
#define	AR_Q5_MISC	0x09d4		/* Miscellaneous QCU settings */
#define	AR_Q6_MISC	0x09d8		/* Miscellaneous QCU settings */
#define	AR_Q7_MISC	0x09dc		/* Miscellaneous QCU settings */
#define	AR_Q8_MISC	0x09e0		/* Miscellaneous QCU settings */
#define	AR_Q9_MISC	0x09e4		/* Miscellaneous QCU settings */
#define	AR_QMISC(i)	(AR_Q0_MISC + ((i)<<2))

#define	AR_Q0_STS	0x0a00		/* Miscellaneous QCU status */
#define	AR_Q1_STS	0x0a04		/* Miscellaneous QCU status */
#define	AR_Q2_STS	0x0a08		/* Miscellaneous QCU status */
#define	AR_Q3_STS	0x0a0c		/* Miscellaneous QCU status */
#define	AR_Q4_STS	0x0a10		/* Miscellaneous QCU status */
#define	AR_Q5_STS	0x0a14		/* Miscellaneous QCU status */
#define	AR_Q6_STS	0x0a18		/* Miscellaneous QCU status */
#define	AR_Q7_STS	0x0a1c		/* Miscellaneous QCU status */
#define	AR_Q8_STS	0x0a20		/* Miscellaneous QCU status */
#define	AR_Q9_STS	0x0a24		/* Miscellaneous QCU status */
#define	AR_QSTS(i)	(AR_Q0_STS + ((i)<<2))

#define	AR_Q_RDYTIMESHDN	0x0a40	/* ReadyTimeShutdown status */
#define	AR_D0_QCUMASK	0x1000		/* QCU Mask */
#define	AR_D1_QCUMASK	0x1004		/* QCU Mask */
#define	AR_D2_QCUMASK	0x1008		/* QCU Mask */
#define	AR_D3_QCUMASK	0x100c		/* QCU Mask */
#define	AR_D4_QCUMASK	0x1010		/* QCU Mask */
#define	AR_D5_QCUMASK	0x1014		/* QCU Mask */
#define	AR_D6_QCUMASK	0x1018		/* QCU Mask */
#define	AR_D7_QCUMASK	0x101c		/* QCU Mask */
#define	AR_D8_QCUMASK	0x1020		/* QCU Mask */
#define	AR_D9_QCUMASK	0x1024		/* QCU Mask */
#define	AR_DQCUMASK(i)	(AR_D0_QCUMASK + ((i)<<2))

#define	AR_D0_LCL_IFS	0x1040		/* DCU-specific IFS settings */
#define	AR_D1_LCL_IFS	0x1044		/* DCU-specific IFS settings */
#define	AR_D2_LCL_IFS	0x1048		/* DCU-specific IFS settings */
#define	AR_D3_LCL_IFS	0x104c		/* DCU-specific IFS settings */
#define	AR_D4_LCL_IFS	0x1050		/* DCU-specific IFS settings */
#define	AR_D5_LCL_IFS	0x1054		/* DCU-specific IFS settings */
#define	AR_D6_LCL_IFS	0x1058		/* DCU-specific IFS settings */
#define	AR_D7_LCL_IFS	0x105c		/* DCU-specific IFS settings */
#define	AR_D8_LCL_IFS	0x1060		/* DCU-specific IFS settings */
#define	AR_D9_LCL_IFS	0x1064		/* DCU-specific IFS settings */
#define	AR_DLCL_IFS(i)	(AR_D0_LCL_IFS + ((i)<<2))

#define	AR_D0_RETRY_LIMIT	0x1080	/* Retry limits */
#define	AR_D1_RETRY_LIMIT	0x1084	/* Retry limits */
#define	AR_D2_RETRY_LIMIT	0x1088	/* Retry limits */
#define	AR_D3_RETRY_LIMIT	0x108c	/* Retry limits */
#define	AR_D4_RETRY_LIMIT	0x1090	/* Retry limits */
#define	AR_D5_RETRY_LIMIT	0x1094	/* Retry limits */
#define	AR_D6_RETRY_LIMIT	0x1098	/* Retry limits */
#define	AR_D7_RETRY_LIMIT	0x109c	/* Retry limits */
#define	AR_D8_RETRY_LIMIT	0x10a0	/* Retry limits */
#define	AR_D9_RETRY_LIMIT	0x10a4	/* Retry limits */
#define	AR_DRETRY_LIMIT(i)	(AR_D0_RETRY_LIMIT + ((i)<<2))

#define	AR_D0_CHNTIME	0x10c0		/* ChannelTime settings */
#define	AR_D1_CHNTIME	0x10c4		/* ChannelTime settings */
#define	AR_D2_CHNTIME	0x10c8		/* ChannelTime settings */
#define	AR_D3_CHNTIME	0x10cc		/* ChannelTime settings */
#define	AR_D4_CHNTIME	0x10d0		/* ChannelTime settings */
#define	AR_D5_CHNTIME	0x10d4		/* ChannelTime settings */
#define	AR_D6_CHNTIME	0x10d8		/* ChannelTime settings */
#define	AR_D7_CHNTIME	0x10dc		/* ChannelTime settings */
#define	AR_D8_CHNTIME	0x10e0		/* ChannelTime settings */
#define	AR_D9_CHNTIME	0x10e4		/* ChannelTime settings */
#define	AR_DCHNTIME(i)	(AR_D0_CHNTIME + ((i)<<2))

#define	AR_D0_MISC	0x1100		/* Misc DCU-specific settings */
#define	AR_D1_MISC	0x1104		/* Misc DCU-specific settings */
#define	AR_D2_MISC	0x1108		/* Misc DCU-specific settings */
#define	AR_D3_MISC	0x110c		/* Misc DCU-specific settings */
#define	AR_D4_MISC	0x1110		/* Misc DCU-specific settings */
#define	AR_D5_MISC	0x1114		/* Misc DCU-specific settings */
#define	AR_D6_MISC	0x1118		/* Misc DCU-specific settings */
#define	AR_D7_MISC	0x111c		/* Misc DCU-specific settings */
#define	AR_D8_MISC	0x1120		/* Misc DCU-specific settings */
#define	AR_D9_MISC	0x1124		/* Misc DCU-specific settings */
#define	AR_DMISC(i)	(AR_D0_MISC + ((i)<<2))

#define	AR_D0_SEQNUM	0x1140		/* Frame seqnum control/status */
#define	AR_D1_SEQNUM	0x1144		/* Frame seqnum control/status */
#define	AR_D2_SEQNUM	0x1148		/* Frame seqnum control/status */
#define	AR_D3_SEQNUM	0x114c		/* Frame seqnum control/status */
#define	AR_D4_SEQNUM	0x1150		/* Frame seqnum control/status */
#define	AR_D5_SEQNUM	0x1154		/* Frame seqnum control/status */
#define	AR_D6_SEQNUM	0x1158		/* Frame seqnum control/status */
#define	AR_D7_SEQNUM	0x115c		/* Frame seqnum control/status */
#define	AR_D8_SEQNUM	0x1160		/* Frame seqnum control/status */
#define	AR_D9_SEQNUM	0x1164		/* Frame seqnum control/status */
#define	AR_DSEQNUM(i)	(AR_D0_SEQNUM + ((i<<2)))

/* MAC DCU-global IFS settings */
#define	AR_D_GBL_IFS_SIFS	0x1030	/* DCU global SIFS settings */
#define	AR_D_GBL_IFS_SLOT	0x1070	/* DC global slot interval */
#define	AR_D_GBL_IFS_EIFS	0x10b0	/* DCU global EIFS setting */
#define	AR_D_GBL_IFS_MISC	0x10f0	/* DCU global misc. IFS settings */
#define	AR_D_FPCTL	0x1230		/* DCU frame prefetch settings */
#define	AR_D_TXPSE	0x1270		/* DCU transmit pause control/status */
#define	AR_D_TXBLK_CMD	0x1038		/* DCU transmit filter cmd (w/only) */
#define	AR_D_TXBLK_DATA(i) (AR_D_TXBLK_CMD+(i))	/* DCU transmit filter data */
#define	AR_D_TXBLK_CLR	0x143c		/* DCU clear tx filter (w/only) */
#define	AR_D_TXBLK_SET	0x147c		/* DCU set tx filter (w/only) */

#define	AR_D_TXPSE	0x1270		/* DCU transmit pause control/status */

#define	AR_RC		0x4000		/* Warm reset control register */
#define	AR_SCR		0x4004		/* Sleep control register */
#define	AR_INTPEND	0x4008		/* Interrupt Pending register */
#define	AR_SFR		0x400C		/* Sleep force register */
#define	AR_PCICFG	0x4010		/* PCI configuration register */
#define	AR_GPIOCR	0x4014		/* GPIO control register */
#define	AR_GPIODO	0x4018		/* GPIO data output access register */
#define	AR_GPIODI	0x401C		/* GPIO data input access register */
#define	AR_SREV		0x4020		/* Silicon Revision register */

#define	AR_EEPROM_ADDR	0x6000		/* EEPROM address register (10 bit) */
#define	AR_EEPROM_DATA	0x6004		/* EEPROM data register (16 bit) */
#define	AR_EEPROM_CMD	0x6008		/* EEPROM command register */
#define	AR_EEPROM_STS	0x600c		/* EEPROM status register */
#define	AR_EEPROM_CFG	0x6010		/* EEPROM configuration register */

#define	AR_STA_ID0	0x8000		/* station ID0 - low 32 bits */
#define	AR_STA_ID1	0x8004		/* station ID1 - upper 16 bits */
#define	AR_BSS_ID0	0x8008		/* BSSID low 32 bits */
#define	AR_BSS_ID1	0x800C		/* BSSID upper 16 bits / AID */
#define	AR_SLOT_TIME	0x8010		/* Time-out after a collision */
#define	AR_TIME_OUT	0x8014		/* ACK & CTS time-out */
#define	AR_RSSI_THR	0x8018		/* RSSI warning & missed beacon threshold */
#define	AR_USEC		0x801c		/* transmit latency register */
#define	AR_BEACON	0x8020		/* beacon control value/mode bits */
#define	AR_CFP_PERIOD	0x8024		/* CFP Interval (TU/msec) */
#define	AR_TIMER0	0x8028		/* Next beacon time (TU/msec) */
#define	AR_TIMER1	0x802c		/* DMA beacon alert time (1/8 TU) */
#define	AR_TIMER2	0x8030		/* Software beacon alert (1/8 TU) */
#define	AR_TIMER3	0x8034		/* ATIM window time */
#define	AR_CFP_DUR	0x8038		/* maximum CFP duration in TU */
#define	AR_RX_FILTER	0x803C		/* receive filter register */
#define	AR_MCAST_FIL0	0x8040		/* multicast filter lower 32 bits */
#define	AR_MCAST_FIL1	0x8044		/* multicast filter upper 32 bits */
#define	AR_DIAG_SW	0x8048		/* PCU control register */
#define	AR_TSF_L32	0x804c		/* local clock lower 32 bits */
#define	AR_TSF_U32	0x8050		/* local clock upper 32 bits */
#define	AR_TST_ADDAC	0x8054		/* ADDAC test register */
#define	AR_DEF_ANTENNA	0x8058		/* default antenna register */

#define	AR_LAST_TSTP	0x8080		/* Time stamp of the last beacon rcvd */
#define	AR_NAV		0x8084		/* current NAV value */
#define	AR_RTS_OK	0x8088		/* RTS exchange success counter */
#define	AR_RTS_FAIL	0x808c		/* RTS exchange failure counter */
#define	AR_ACK_FAIL	0x8090		/* ACK failure counter */
#define	AR_FCS_FAIL	0x8094		/* FCS check failure counter */
#define	AR_BEACON_CNT	0x8098		/* Valid beacon counter */

#define	AR_KEYTABLE_0	0x8800		/* Encryption key table */
#define	AR_KEYTABLE(n)	(AR_KEYTABLE_0 + ((n)*32))

#define	AR_CR_RXE	0x00000004	/* Receive enable */
#define	AR_CR_RXD	0x00000020	/* Receive disable */
#define	AR_CR_SWI	0x00000040	/* One-shot software interrupt */
#define	AR_CR_BITS	"\20\3RXE\6RXD\7SWI"

#define	AR_CFG_SWTD	0x00000001	/* byteswap tx descriptor words */
#define	AR_CFG_SWTB	0x00000002	/* byteswap tx data buffer words */
#define	AR_CFG_SWRD	0x00000004	/* byteswap rx descriptor words */
#define	AR_CFG_SWRB	0x00000008	/* byteswap rx data buffer words */
#define	AR_CFG_SWRG	0x00000010	/* byteswap register access data words */
#define	AR_CFG_AP_ADHOC_INDICATION	0x00000020	/* AP/adhoc indication (0-AP, 1-Adhoc) */
#define	AR_CFG_PHOK	0x00000100	/* PHY OK status */
#define	AR_CFG_EEBS	0x00000200	/* EEPROM busy */
#define	AR_CFG_CLK_GATE_DIS	0x00000400	/* Clock gating disable (Oahu only) */
#define	AR_CFG_PCI_MASTER_REQ_Q_THRESH_M	0x00060000	/* Mask of PCI core master request queue full threshold */
#define	AR_CFG_PCI_MASTER_REQ_Q_THRESH_S	17        	/* Shift for PCI core master request queue full threshold */
#define	AR_CFG_BITS \
	"\20\1SWTD\2SWTB\3SWRD\4SWRB\5SWRG\10PHYOK11EEBS"

#define	AR_IER_ENABLE	0x00000001	/* Global interrupt enable */
#define	AR_IER_DISABLE	0x00000000	/* Global interrupt disable */
#define	AR_IER_BITS	"\20\1ENABLE"

#define	AR_RTSD0_RTS_DURATION_6_M	0x000000FF
#define	AR_RTSD0_RTS_DURATION_6_S	0
#define	AR_RTSD0_RTS_DURATION_9_M	0x0000FF00
#define	AR_RTSD0_RTS_DURATION_9_S	8
#define	AR_RTSD0_RTS_DURATION_12_M	0x00FF0000
#define	AR_RTSD0_RTS_DURATION_12_S	16
#define	AR_RTSD0_RTS_DURATION_18_M	0xFF000000
#define	AR_RTSD0_RTS_DURATION_18_S	24

#define	AR_RTSD0_RTS_DURATION_24_M	0x000000FF
#define	AR_RTSD0_RTS_DURATION_24_S	0
#define	AR_RTSD0_RTS_DURATION_36_M	0x0000FF00
#define	AR_RTSD0_RTS_DURATION_36_S	8
#define	AR_RTSD0_RTS_DURATION_48_M	0x00FF0000
#define	AR_RTSD0_RTS_DURATION_48_S	16
#define	AR_RTSD0_RTS_DURATION_54_M	0xFF000000
#define	AR_RTSD0_RTS_DURATION_54_S	24

#define	AR_DMASIZE_4B	0x00000000	/* DMA size 4 bytes (TXCFG + RXCFG) */
#define	AR_DMASIZE_8B	0x00000001	/* DMA size 8 bytes */
#define	AR_DMASIZE_16B	0x00000002	/* DMA size 16 bytes */
#define	AR_DMASIZE_32B	0x00000003	/* DMA size 32 bytes */
#define	AR_DMASIZE_64B	0x00000004	/* DMA size 64 bytes */
#define	AR_DMASIZE_128B	0x00000005	/* DMA size 128 bytes */
#define	AR_DMASIZE_256B	0x00000006	/* DMA size 256 bytes */
#define	AR_DMASIZE_512B	0x00000007	/* DMA size 512 bytes */

#define	AR_TXCFG_FTRIG_M	0x000003F0	/* Mask for Frame trigger level */
#define	AR_TXCFG_FTRIG_S	4         	/* Shift for Frame trigger level */
#define	AR_TXCFG_FTRIG_IMMED	0x00000000	/* bytes in PCU TX FIFO before air */
#define	AR_TXCFG_FTRIG_64B	0x00000010	/* default */
#define	AR_TXCFG_FTRIG_128B	0x00000020
#define	AR_TXCFG_FTRIG_192B	0x00000030
#define	AR_TXCFG_FTRIG_256B	0x00000040	/* 5 bits total */
#define	AR_TXCFG_BITS	"\20"

#define	AR5311_RXCFG_DEF_RX_ANTENNA	0x00000008	/* Default Receive Antenna */
						/* Maui2/Spirit only - reserved on Oahu */
#define	AR_RXCFG_ZLFDMA	0x00000010	/* Enable DMA of zero-length frame */
#define	AR_RXCFG_EN_JUM	0x00000020	/* Enable jumbo rx descriptors */
#define	AR_RXCFG_WR_JUM	0x00000040	/* Wrap jumbo rx descriptors */

#define	AR_MIBC_COW	0x00000001	/* counter overflow warning */
#define	AR_MIBC_FMC	0x00000002	/* freeze MIB counters */
#define	AR_MIBC_CMC	0x00000004	/* clear MIB counters */
#define	AR_MIBC_MCS	0x00000008	/* MIB counter strobe, increment all */

#define	AR_TOPS_MASK	0x0000FFFF	/* Mask for timeout prescale */

#define	AR_RXNPTO_MASK	0x000003FF	/* Mask for no frame received timeout */

#define	AR_TXNPTO_MASK	0x000003FF	/* Mask for no frame transmitted timeout */
#define	AR_TXNPTO_QCU_MASK	0x03FFFC00	/* Mask indicating the set of QCUs */
					/* for which frame completions will cause */
					/* a reset of the no frame transmitted timeout */

#define	AR_RPGTO_MASK	0x000003FF	/* Mask for receive frame gap timeout */

#define	AR_RPCNT_MASK	0x0000001F	/* Mask for receive frame count limit */

#define	AR_MACMISC_DMA_OBS_M	0x000001E0	/* Mask for DMA observation bus mux select */
#define	AR_MACMISC_DMA_OBS_S	5         	/* Shift for DMA observation bus mux select */
#define	AR_MACMISC_MISC_OBS_M	0x00000E00	/* Mask for MISC observation bus mux select */
#define	AR_MACMISC_MISC_OBS_S	9         	/* Shift for MISC observation bus mux select */
#define	AR_MACMISC_MAC_OBS_BUS_LSB_M	0x00007000	/* Mask for MAC observation bus mux select (lsb) */
#define	AR_MACMISC_MAC_OBS_BUS_LSB_S	12        	/* Shift for MAC observation bus mux select (lsb) */
#define	AR_MACMISC_MAC_OBS_BUS_MSB_M	0x00038000	/* Mask for MAC observation bus mux select (msb) */
#define	AR_MACMISC_MAC_OBS_BUS_MSB_S	15        	/* Shift for MAC observation bus mux select (msb) */

				/* Maui2/Spirit only. */
#define	AR5311_QDCLKGATE_QCU_M	0x0000FFFF	/* Mask for QCU clock disable */
#define	AR5311_QDCLKGATE_DCU_M	0x07FF0000	/* Mask for DCU clock disable */

	/* Interrupt Status Registers */
#define	AR_ISR_RXOK	0x00000001	/* At least one frame received sans errors */
#define	AR_ISR_RXDESC	0x00000002	/* Receive interrupt request */
#define	AR_ISR_RXERR	0x00000004	/* Receive error interrupt */
#define	AR_ISR_RXNOPKT	0x00000008	/* No frame received within timeout clock */
#define	AR_ISR_RXEOL	0x00000010	/* Received descriptor empty interrupt */
#define	AR_ISR_RXORN	0x00000020	/* Receive FIFO overrun interrupt */
#define	AR_ISR_TXOK	0x00000040	/* Transmit okay interrupt */
#define	AR_ISR_TXDESC	0x00000080	/* Transmit interrupt request */
#define	AR_ISR_TXERR	0x00000100	/* Transmit error interrupt */
#define	AR_ISR_TXNOPKT	0x00000200	/* No frame transmitted interrupt */
#define	AR_ISR_TXEOL	0x00000400	/* Transmit descriptor empty interrupt */
#define	AR_ISR_TXURN	0x00000800	/* Transmit FIFO underrun interrupt */
#define	AR_ISR_MIB	0x00001000	/* MIB interrupt - see MIBC */
#define	AR_ISR_SWI	0x00002000	/* Software interrupt */
#define	AR_ISR_RXPHY	0x00004000	/* PHY receive error interrupt */
#define	AR_ISR_RXKCM	0x00008000	/* Key-cache miss interrupt */
#define	AR_ISR_SWBA	0x00010000	/* Software beacon alert interrupt */
#define	AR_ISR_BRSSI	0x00020000	/* Beacon threshold interrupt */
#define	AR_ISR_BMISS	0x00040000	/* Beacon missed interrupt */
#define	AR_ISR_HIUERR	0x00080000	/* An unexpected bus error has occurred */
#define	AR_ISR_BNR	0x00100000	/* Beacon not ready interrupt */
#define	AR_ISR_TIM	0x00800000	/* TIM interrupt */
#define	AR_ISR_GPIO	0x01000000	/* GPIO Interrupt */
#define	AR_ISR_QCBROVF	0x02000000	/* QCU CBR overflow interrupt */
#define	AR_ISR_QCBRURN	0x04000000	/* QCU CBR underrun interrupt */
#define	AR_ISR_QTRIG	0x08000000	/* QCU scheduling trigger interrupt */
#define	AR_ISR_RESV0	0xF0000000	/* Reserved */

#define	AR_ISR_S0_QCU_TXOK_M	0x000003FF	/* Mask for TXOK (QCU 0-9) */
#define	AR_ISR_S0_QCU_TXDESC_M	0x03FF0000	/* Mask for TXDESC (QCU 0-9) */

#define	AR_ISR_S1_QCU_TXERR_M	0x000003FF	/* Mask for TXERR (QCU 0-9) */
#define	AR_ISR_S1_QCU_TXEOL_M	0x03FF0000	/* Mask for TXEOL (QCU 0-9) */

#define	AR_ISR_S2_QCU_TXURN_M	0x000003FF	/* Mask for TXURN (QCU 0-9) */
#define	AR_ISR_S2_MCABT	0x00010000	/* Master cycle abort interrupt */
#define	AR_ISR_S2_SSERR	0x00020000	/* SERR interrupt */
#define	AR_ISR_S2_DPERR	0x00040000	/* PCI bus parity error */
#define	AR_ISR_S2_RESV0	0xFFF80000	/* Reserved */

#define	AR_ISR_S3_QCU_QCBROVF_M	0x000003FF	/* Mask for QCBROVF (QCU 0-9) */
#define	AR_ISR_S3_QCU_QCBRURN_M	0x03FF0000	/* Mask for QCBRURN (QCU 0-9) */

#define	AR_ISR_S4_QCU_QTRIG_M	0x000003FF	/* Mask for QTRIG (QCU 0-9) */
#define	AR_ISR_S4_RESV0	0xFFFFFC00	/* Reserved */

	/* Interrupt Mask Registers */
#define	AR_IMR_RXOK	0x00000001	/* At least one frame received sans errors */
#define	AR_IMR_RXDESC	0x00000002	/* Receive interrupt request */
#define	AR_IMR_RXERR	0x00000004	/* Receive error interrupt */
#define	AR_IMR_RXNOPKT	0x00000008	/* No frame received within timeout clock */
#define	AR_IMR_RXEOL	0x00000010	/* Received descriptor empty interrupt */
#define	AR_IMR_RXORN	0x00000020	/* Receive FIFO overrun interrupt */
#define	AR_IMR_TXOK	0x00000040	/* Transmit okay interrupt */
#define	AR_IMR_TXDESC	0x00000080	/* Transmit interrupt request */
#define	AR_IMR_TXERR	0x00000100	/* Transmit error interrupt */
#define	AR_IMR_TXNOPKT	0x00000200	/* No frame transmitted interrupt */
#define	AR_IMR_TXEOL	0x00000400	/* Transmit descriptor empty interrupt */
#define	AR_IMR_TXURN	0x00000800	/* Transmit FIFO underrun interrupt */
#define	AR_IMR_MIB	0x00001000	/* MIB interrupt - see MIBC */
#define	AR_IMR_SWI	0x00002000	/* Software interrupt */
#define	AR_IMR_RXPHY	0x00004000	/* PHY receive error interrupt */
#define	AR_IMR_RXKCM	0x00008000	/* Key-cache miss interrupt */
#define	AR_IMR_SWBA	0x00010000	/* Software beacon alert interrupt */
#define	AR_IMR_BRSSI	0x00020000	/* Beacon threshold interrupt */
#define	AR_IMR_BMISS	0x00040000	/* Beacon missed interrupt */
#define	AR_IMR_HIUERR	0x00080000	/* An unexpected bus error has occurred */
#define	AR_IMR_BNR	0x00100000	/* BNR interrupt */
#define	AR_IMR_TIM	0x00800000	/* TIM interrupt */
#define	AR_IMR_GPIO	0x01000000	/* GPIO Interrupt */
#define	AR_IMR_QCBROVF	0x02000000	/* QCU CBR overflow interrupt */
#define	AR_IMR_QCBRURN	0x04000000	/* QCU CBR underrun interrupt */
#define	AR_IMR_QTRIG	0x08000000	/* QCU scheduling trigger interrupt */
#define	AR_IMR_RESV0	0xF0000000	/* Reserved */

#define	AR_IMR_S0_QCU_TXOK	0x000003FF	/* Mask for TXOK (QCU 0-9) */
#define	AR_IMR_S0_QCU_TXOK_S	0
#define	AR_IMR_S0_QCU_TXDESC	0x03FF0000	/* Mask for TXDESC (QCU 0-9) */
#define	AR_IMR_S0_QCU_TXDESC_S	16        	/* Shift for TXDESC (QCU 0-9) */

#define	AR_IMR_S1_QCU_TXERR	0x000003FF	/* Mask for TXERR (QCU 0-9) */
#define	AR_IMR_S1_QCU_TXERR_S	0
#define	AR_IMR_S1_QCU_TXEOL	0x03FF0000	/* Mask for TXEOL (QCU 0-9) */
#define	AR_IMR_S1_QCU_TXEOL_S	16        	/* Shift for TXEOL (QCU 0-9) */

#define	AR_IMR_S2_QCU_TXURN	0x000003FF	/* Mask for TXURN (QCU 0-9) */
#define	AR_IMR_S2_QCU_TXURN_S	0
#define	AR_IMR_S2_MCABT	0x00010000	/* Master cycle abort interrupt */
#define	AR_IMR_S2_SSERR	0x00020000	/* SERR interrupt */
#define	AR_IMR_S2_DPERR	0x00040000	/* PCI bus parity error */
#define	AR_IMR_S2_RESV0	0xFFF80000	/* Reserved */

#define	AR_IMR_S3_QCU_QCBROVF_M	0x000003FF	/* Mask for QCBROVF (QCU 0-9) */
#define	AR_IMR_S3_QCU_QCBRURN_M	0x03FF0000	/* Mask for QCBRURN (QCU 0-9) */
#define	AR_IMR_S3_QCU_QCBRURN_S	16        	/* Shift for QCBRURN (QCU 0-9) */

#define	AR_IMR_S4_QCU_QTRIG_M	0x000003FF	/* Mask for QTRIG (QCU 0-9) */
#define	AR_IMR_S4_RESV0	0xFFFFFC00	/* Reserved */

	/* Interrupt status registers (read-and-clear access, secondary shadow copies) */

	/* QCU registers */
#define	AR_NUM_QCU	10    	/* Only use QCU 0-9 for forward QCU compatibility */
#define	AR_QCU_0	0x0001
#define	AR_QCU_1	0x0002
#define	AR_QCU_2	0x0004
#define	AR_QCU_3	0x0008
#define	AR_QCU_4	0x0010
#define	AR_QCU_5	0x0020
#define	AR_QCU_6	0x0040
#define	AR_QCU_7	0x0080
#define	AR_QCU_8	0x0100
#define	AR_QCU_9	0x0200

#define	AR_Q_TXE_M	0x000003FF	/* Mask for TXE (QCU 0-9) */

#define	AR_Q_TXD_M	0x000003FF	/* Mask for TXD (QCU 0-9) */

#define	AR_Q_CBRCFG_CBR_INTERVAL	0x00FFFFFF	/* Mask for CBR interval (us) */
#define	AR_Q_CBRCFG_CBR_INTERVAL_S		0	/* Shift for CBR interval */
#define	AR_Q_CBRCFG_CBR_OVF_THRESH	0xFF000000	/* Mask for CBR overflow threshold */
#define	AR_Q_CBRCFG_CBR_OVF_THRESH_S		24	/* Shift for " " " */

#define	AR_Q_RDYTIMECFG_INT	0x00FFFFFF 	/* CBR interval (us) */
#define	AR_Q_RDYTIMECFG_INT_S	0		/* Shift for ReadyTime Interval (us) */
#define	AR_Q_RDYTIMECFG_DURATION_M	0x00FFFFFF	/* Mask for CBR interval (us) */
#define	AR_Q_RDYTIMECFG_EN	0x01000000	/* ReadyTime enable */
#define	AR_Q_RDYTIMECFG_RESV0	0xFE000000	/* Reserved */

#define	AR_Q_ONESHOTARM_SC_M	0x0000FFFF	/* Mask for MAC_Q_ONESHOTARM_SC (QCU 0-15) */
#define	AR_Q_ONESHOTARM_SC_RESV0 0xFFFF0000	/* Reserved */

#define	AR_Q_ONESHOTARM_CC_M	0x0000FFFF	/* Mask for MAC_Q_ONESHOTARM_CC (QCU 0-15) */
#define	AR_Q_ONESHOTARM_CC_RESV0 0xFFFF0000	/* Reserved */

#define	AR_Q_MISC_FSP_M		0x0000000F	/* Mask for Frame Scheduling Policy */
#define	AR_Q_MISC_FSP_ASAP		0	/* ASAP */
#define	AR_Q_MISC_FSP_CBR		1	/* CBR */
#define	AR_Q_MISC_FSP_DBA_GATED		2	/* DMA Beacon Alert gated */
#define	AR_Q_MISC_FSP_TIM_GATED		3	/* TIM gated */
#define	AR_Q_MISC_FSP_BEACON_SENT_GATED	4	/* Beacon-sent-gated */
#define	AR_Q_MISC_ONE_SHOT_EN	0x00000010	/* OneShot enable */
#define	AR_Q_MISC_CBR_INCR_DIS1	0x00000020	/* Disable CBR expired counter
						   incr (empty q) */
#define	AR_Q_MISC_CBR_INCR_DIS0	0x00000040	/* Disable CBR expired counter
						   incr (empty beacon q) */
#define	AR_Q_MISC_BEACON_USE	0x00000080	/* Beacon use indication */
#define	AR_Q_MISC_CBR_EXP_CNTR_LIMIT	0x00000100	/* CBR expired counter limit enable */
#define	AR_Q_MISC_RDYTIME_EXP_POLICY	0x00000200	/* Enable TXE cleared on ReadyTime expired or VEOL */
#define	AR_Q_MISC_RESET_CBR_EXP_CTR	0x00000400	/* Reset CBR expired counter */
#define	AR_Q_MISC_DCU_EARLY_TERM_REQ	0x00000800	/* DCU frame early termination request control */
#define	AR_Q_MISC_RESV0	0xFFFFF000	/* Reserved */

#define	AR_Q_STS_PEND_FR_CNT_M	0x00000003	/* Mask for Pending Frame Count */
#define	AR_Q_STS_RESV0	0x000000FC	/* Reserved */
#define	AR_Q_STS_CBR_EXP_CNT_M	0x0000FF00	/* Mask for CBR expired counter */
#define	AR_Q_STS_RESV1	0xFFFF0000	/* Reserved */

#define	AR_Q_RDYTIMESHDN_M	0x000003FF	/* Mask for ReadyTimeShutdown status (QCU 0-9) */

	/* DCU registers */
#define	AR_NUM_DCU	10    	/* Only use 10 DCU's for forward QCU/DCU compatibility */
#define	AR_DCU_0	0x0001
#define	AR_DCU_1	0x0002
#define	AR_DCU_2	0x0004
#define	AR_DCU_3	0x0008
#define	AR_DCU_4	0x0010
#define	AR_DCU_5	0x0020
#define	AR_DCU_6	0x0040
#define	AR_DCU_7	0x0080
#define	AR_DCU_8	0x0100
#define	AR_DCU_9	0x0200

#define	AR_D_QCUMASK_M	0x000003FF	/* Mask for QCU Mask (QCU 0-9) */
#define	AR_D_QCUMASK_RESV0	0xFFFFFC00	/* Reserved */

#define	AR_D_LCL_IFS_CWMIN	0x000003FF	/* Mask for CW_MIN */
#define	AR_D_LCL_IFS_CWMIN_S	0		/* Shift for CW_MIN */
#define	AR_D_LCL_IFS_CWMAX	0x000FFC00	/* Mask for CW_MAX */
#define	AR_D_LCL_IFS_CWMAX_S	10        	/* Shift for CW_MAX */
#define	AR_D_LCL_IFS_AIFS	0x0FF00000	/* Mask for AIFS */
#define	AR_D_LCL_IFS_AIFS_S	20        	/* Shift for AIFS */
#define	AR_D_LCL_IFS_RESV0	0xF0000000	/* Reserved */

#define	AR_D_RETRY_LIMIT_FR_SH	0x0000000F	/* Mask for frame short retry limit */
#define	AR_D_RETRY_LIMIT_FR_SH_S	0	/* Shift for frame short retry limit */
#define	AR_D_RETRY_LIMIT_FR_LG	0x000000F0	/* Mask for frame long retry limit */
#define	AR_D_RETRY_LIMIT_FR_LG_S	4	/* Shift for frame long retry limit */
#define	AR_D_RETRY_LIMIT_STA_SH	0x00003F00	/* Mask for station short retry limit */
#define	AR_D_RETRY_LIMIT_STA_SH_S	8	/* Shift for station short retry limit */
#define	AR_D_RETRY_LIMIT_STA_LG	0x000FC000	/* Mask for station short retry limit */
#define	AR_D_RETRY_LIMIT_STA_LG_S	14	/* Shift for station short retry limit */
#define	AR_D_RETRY_LIMIT_RESV0	0xFFF00000	/* Reserved */

#define	AR_D_CHNTIME_EN	0x00100000	/* ChannelTime enable */
#define	AR_D_CHNTIME_RESV0	0xFFE00000	/* Reserved */
#define	AR_D_CHNTIME_DUR	0x000FFFFF	/* Mask for ChannelTime duration (us) */
#define AR_D_CHNTIME_DUR_S              0 /* Shift for ChannelTime duration */

#define	AR_D_MISC_BKOFF_THRESH_M	0x000007FF	/* Mask for Backoff threshold setting */
#define AR_D_MISC_FRAG_BKOFF_EN         0x00000200 /* Backoff during a frag burst */
#define	AR_D_MISC_HCF_POLL_EN	0x00000800	/* HFC poll enable */
#define	AR_D_MISC_BKOFF_PERSISTENCE	0x00001000	/* Backoff persistence factor setting */
#define	AR_D_MISC_FR_PREFETCH_EN	0x00002000	/* Frame prefetch enable */
#define	AR_D_MISC_VIR_COL_HANDLING_M	0x0000C000	/* Mask for Virtual collision handling policy */
#define	AR_D_MISC_VIR_COL_HANDLING_NORMAL	0     	/* Normal */
#define	AR_D_MISC_VIR_COL_HANDLING_MODIFIED	1   	/* Modified */
#define	AR_D_MISC_VIR_COL_HANDLING_IGNORE	2     	/* Ignore */
#define	AR_D_MISC_BEACON_USE	0x00010000	/* Beacon use indication */
#define	AR_D_MISC_ARB_LOCKOUT_CNTRL	0x00060000	/* Mask for DCU arbiter lockout control */
#define	AR_D_MISC_ARB_LOCKOUT_CNTRL_S	17        	/* Shift for DCU arbiter lockout control */
#define	AR_D_MISC_ARB_LOCKOUT_CNTRL_NONE	0      	/* No lockout */
#define	AR_D_MISC_ARB_LOCKOUT_CNTRL_INTRA_FR	1  	/* Intra-frame */
#define	AR_D_MISC_ARB_LOCKOUT_CNTRL_GLOBAL	2    	/* Global */
#define	AR_D_MISC_ARB_LOCKOUT_IGNORE	0x00080000	/* DCU arbiter lockout ignore control */
#define	AR_D_MISC_SEQ_NUM_INCR_DIS	0x00100000	/* Sequence number increment disable */
#define	AR_D_MISC_POST_FR_BKOFF_DIS	0x00200000	/* Post-frame backoff disable */
#define	AR_D_MISC_VIRT_COLL_POLICY	0x00400000	/* Virtual coll. handling policy */
#define	AR_D_MISC_BLOWN_IFS_POLICY	0x00800000	/* Blown IFS handling policy */
#define	AR5311_D_MISC_SEQ_NUM_CONTROL	0x01000000	/* Sequence Number local or global */
						/* Maui2/Spirit only, reserved on Oahu */
#define	AR_D_MISC_RESV0	0xFE000000	/* Reserved */

#define	AR_D_SEQNUM_M	0x00000FFF	/* Mask for value of sequence number */
#define	AR_D_SEQNUM_RESV0	0xFFFFF000	/* Reserved */

#define	AR_D_GBL_IFS_MISC_LFSR_SLICE_SEL	0x00000007	/* Mask forLFSR slice select */
#define	AR_D_GBL_IFS_MISC_TURBO_MODE	0x00000008	/* Turbo mode indication */
#define	AR_D_GBL_IFS_MISC_SIFS_DURATION_USEC	0x000003F0	/* Mask for SIFS duration (us) */
#define	AR_D_GBL_IFS_MISC_USEC_DURATION	0x000FFC00	/* Mask for microsecond duration */
#define	AR_D_GBL_IFS_MISC_DCU_ARBITER_DLY	0x00300000	/* Mask for DCU arbiter delay */
#define	AR_D_GBL_IFS_MISC_RESV0	0xFFC00000	/* Reserved */

/* Oahu only */
#define	AR_D_TXPSE_CTRL_M	0x000003FF	/* Mask of DCUs to pause (DCUs 0-9) */
#define	AR_D_TXPSE_RESV0	0x0000FC00	/* Reserved */
#define	AR_D_TXPSE_STATUS	0x00010000	/* Transmit pause status */
#define	AR_D_TXPSE_RESV1	0xFFFE0000	/* Reserved */

	/* DMA & PCI Registers in PCI space (usable during sleep) */
#define	AR_RC_MAC	0x00000001	/* MAC reset */
#define	AR_RC_BB	0x00000002	/* Baseband reset */
#define	AR_RC_RESV0	0x00000004	/* Reserved */
#define	AR_RC_RESV1	0x00000008	/* Reserved */
#define	AR_RC_PCI	0x00000010	/* PCI-core reset */
#define	AR_RC_BITS	"\20\1MAC\2BB\3RESV0\4RESV1\5RPCI"

#define	AR_SCR_SLDUR	0x0000ffff	/* sleep duration mask, units of 128us */
#define	AR_SCR_SLDUR_S	0
#define	AR_SCR_SLE	0x00030000	/* sleep enable mask */
#define	AR_SCR_SLE_S	16		/* sleep enable bits shift */
/*
 * The previous values for the following three defines were:
 *
 *	AR_SCR_SLE_WAKE	0x00000000
 *	AR_SCR_SLE_SLP	0x00010000
 *	AR_SCR_SLE_NORM	0x00020000
 *
 * However, these have been pre-shifted with AR_SCR_SLE_S.  The
 * OS_REG_READ() macro would attempt to shift them again, effectively
 * shifting out any of the set bits completely.
 */
#define	AR_SCR_SLE_WAKE	0		/* force wake */
#define	AR_SCR_SLE_SLP	1		/* force sleep */
#define	AR_SCR_SLE_NORM	2		/* sleep logic normal operation */
#define	AR_SCR_SLE_UNITS	0x00000008	/* SCR units/TU */
#define	AR_SCR_BITS	"\20\20SLE_SLP\21SLE"

#define	AR_INTPEND_TRUE	0x00000001	/* interrupt pending */
#define	AR_INTPEND_BITS	"\20\1IP"

#define	AR_SFR_SLEEP	0x00000001	/* force sleep */

#define	AR_PCICFG_CLKRUNEN	0x00000004	/* enable PCI CLKRUN function */
#define	AR_PCICFG_EEPROM_SIZE_M	0x00000018	/* Mask for EEPROM size */
#define	AR_PCICFG_EEPROM_SIZE_S	  3	/* Mask for EEPROM size */
#define	AR_PCICFG_EEPROM_SIZE_4K	 0	/* EEPROM size 4 Kbit */
#define	AR_PCICFG_EEPROM_SIZE_8K	 1	/* EEPROM size 8 Kbit */
#define	AR_PCICFG_EEPROM_SIZE_16K	2	/* EEPROM size 16 Kbit */
#define	AR_PCICFG_EEPROM_SIZE_FAILED	3	/* Failure */
#define	AR_PCICFG_LEDCTL	0x00000060 /* LED control Status */
#define	AR_PCICFG_LEDCTL_NONE	0x00000000 /* STA is not associated or trying */
#define	AR_PCICFG_LEDCTL_PEND	0x00000020 /* STA is trying to associate */
#define	AR_PCICFG_LEDCTL_ASSOC	0x00000040 /* STA is associated */
#define	AR_PCICFG_PCI_BUS_SEL_M	0x00000380	/* Mask for PCI observation bus mux select */
#define	AR_PCICFG_DIS_CBE_FIX	0x00000400	/* Disable fix for bad PCI CBE# generation */
#define	AR_PCICFG_SL_INTEN	0x00000800	/* enable interrupt line assertion when asleep */
#define	AR_PCICFG_RESV0		0x00001000	/* Reserved */
#define	AR_PCICFG_SL_INPEN	0x00002000	/* Force asleep when an interrupt is pending */
#define	AR_PCICFG_RESV1		0x0000C000	/* Reserved */
#define	AR_PCICFG_SPWR_DN	0x00010000	/* mask for sleep/awake indication */
#define	AR_PCICFG_LEDMODE	0x000E0000 /* LED mode */
#define	AR_PCICFG_LEDMODE_PROP	0x00000000 /* Blink prop to filtered tx/rx */
#define	AR_PCICFG_LEDMODE_RPROP	0x00020000 /* Blink prop to unfiltered tx/rx */
#define	AR_PCICFG_LEDMODE_SPLIT	0x00040000 /* Blink power for tx/net for rx */
#define	AR_PCICFG_LEDMODE_RAND	0x00060000 /* Blink randomly */
#define	AR_PCICFG_LEDBLINK	0x00700000 /* LED blink threshold select */
#define	AR_PCICFG_LEDBLINK_S	20
#define	AR_PCICFG_LEDSLOW	0x00800000 /* LED slowest blink rate mode */
#define	AR_PCICFG_RESV2		0xFF000000	/* Reserved */
#define	AR_PCICFG_BITS	"\20\3CLKRUNEN\13SL_INTEN"

#define	AR_GPIOCR_CR_SHIFT	2         	/* Each CR is 2 bits */
#define	AR_GPIOCR_0_CR_N	0x00000000	/* Input only mode for GPIODO[0] */
#define	AR_GPIOCR_0_CR_0	0x00000001	/* Output only if GPIODO[0] = 0 */
#define	AR_GPIOCR_0_CR_1	0x00000002	/* Output only if GPIODO[0] = 1 */
#define	AR_GPIOCR_0_CR_A	0x00000003	/* Always output */
#define	AR_GPIOCR_1_CR_N	0x00000000	/* Input only mode for GPIODO[1] */
#define	AR_GPIOCR_1_CR_0	0x00000004	/* Output only if GPIODO[1] = 0 */
#define	AR_GPIOCR_1_CR_1	0x00000008	/* Output only if GPIODO[1] = 1 */
#define	AR_GPIOCR_1_CR_A	0x0000000C	/* Always output */
#define	AR_GPIOCR_2_CR_N	0x00000000	/* Input only mode for GPIODO[2] */
#define	AR_GPIOCR_2_CR_0	0x00000010	/* Output only if GPIODO[2] = 0 */
#define	AR_GPIOCR_2_CR_1	0x00000020	/* Output only if GPIODO[2] = 1 */
#define	AR_GPIOCR_2_CR_A	0x00000030	/* Always output */
#define	AR_GPIOCR_3_CR_N	0x00000000	/* Input only mode for GPIODO[3] */
#define	AR_GPIOCR_3_CR_0	0x00000040	/* Output only if GPIODO[3] = 0 */
#define	AR_GPIOCR_3_CR_1	0x00000080	/* Output only if GPIODO[3] = 1 */
#define	AR_GPIOCR_3_CR_A	0x000000C0	/* Always output */
#define	AR_GPIOCR_4_CR_N	0x00000000	/* Input only mode for GPIODO[4] */
#define	AR_GPIOCR_4_CR_0	0x00000100	/* Output only if GPIODO[4] = 0 */
#define	AR_GPIOCR_4_CR_1	0x00000200	/* Output only if GPIODO[4] = 1 */
#define	AR_GPIOCR_4_CR_A	0x00000300	/* Always output */
#define	AR_GPIOCR_5_CR_N	0x00000000	/* Input only mode for GPIODO[5] */
#define	AR_GPIOCR_5_CR_0	0x00000400	/* Output only if GPIODO[5] = 0 */
#define	AR_GPIOCR_5_CR_1	0x00000800	/* Output only if GPIODO[5] = 1 */
#define	AR_GPIOCR_5_CR_A	0x00000C00	/* Always output */
#define	AR_GPIOCR_INT_SHIFT	12        	/* Interrupt select field shifter */
#define	AR_GPIOCR_INT_MASK	0x00007000	/* Interrupt select field mask */
#define	AR_GPIOCR_INT_SEL0	0x00000000	/* Select Interrupt Pin GPIO_0 */
#define	AR_GPIOCR_INT_SEL1	0x00001000	/* Select Interrupt Pin GPIO_1 */
#define	AR_GPIOCR_INT_SEL2	0x00002000	/* Select Interrupt Pin GPIO_2 */
#define	AR_GPIOCR_INT_SEL3	0x00003000	/* Select Interrupt Pin GPIO_3 */
#define	AR_GPIOCR_INT_SEL4	0x00004000	/* Select Interrupt Pin GPIO_4 */
#define	AR_GPIOCR_INT_SEL5	0x00005000	/* Select Interrupt Pin GPIO_5 */
#define	AR_GPIOCR_INT_ENA	0x00008000	/* Enable GPIO Interrupt */
#define	AR_GPIOCR_INT_SELL	0x00000000	/* Generate Interrupt if selected pin is low */
#define	AR_GPIOCR_INT_SELH	0x00010000	/* Generate Interrupt if selected pin is high */

#define	AR_SREV_ID_M	0x000000FF	/* Mask to read SREV info */
#define	AR_PCICFG_EEPROM_SIZE_16K	2	/* EEPROM size 16 Kbit */
#define	AR_SREV_ID_S		4		/* Major Rev Info */
#define	AR_SREV_REVISION_M	0x0000000F	/* Chip revision level */
#define	AR_SREV_FPGA		1
#define	AR_SREV_D2PLUS		2
#define	AR_SREV_D2PLUS_MS	3		/* metal spin */
#define	AR_SREV_CRETE		4
#define	AR_SREV_CRETE_MS	5		/* FCS metal spin */
#define	AR_SREV_CRETE_MS23	7		/* 2.3 metal spin (6 skipped) */
#define	AR_SREV_CRETE_23	8		/* 2.3 full tape out */
#define	AR_SREV_VERSION_M	0x000000F0	/* Chip version indication */
#define	AR_SREV_VERSION_CRETE	0
#define	AR_SREV_VERSION_MAUI_1	1
#define	AR_SREV_VERSION_MAUI_2	2
#define	AR_SREV_VERSION_SPIRIT	3
#define	AR_SREV_VERSION_OAHU	4
#define	AR_SREV_OAHU_ES		0	/* Engineering Sample */
#define	AR_SREV_OAHU_PROD	2	/* Production */

#define	RAD5_SREV_MAJOR	0x10	/* All current supported ar5211 5 GHz radios are rev 0x10 */
#define	RAD5_SREV_PROD	0x15	/* Current production level radios */
#define	RAD2_SREV_MAJOR	0x20	/* All current supported ar5211 2 GHz radios are rev 0x10 */

	/* EEPROM Registers in the MAC */
#define	AR_EEPROM_CMD_READ	0x00000001
#define	AR_EEPROM_CMD_WRITE	0x00000002
#define	AR_EEPROM_CMD_RESET	0x00000004

#define	AR_EEPROM_STS_READ_ERROR	0x00000001
#define	AR_EEPROM_STS_READ_COMPLETE	0x00000002
#define	AR_EEPROM_STS_WRITE_ERROR	0x00000004
#define	AR_EEPROM_STS_WRITE_COMPLETE	0x00000008

#define	AR_EEPROM_CFG_SIZE_M	0x00000003	/* Mask for EEPROM size determination override */
#define	AR_EEPROM_CFG_SIZE_AUTO	0
#define	AR_EEPROM_CFG_SIZE_4KBIT	1
#define	AR_EEPROM_CFG_SIZE_8KBIT	2
#define	AR_EEPROM_CFG_SIZE_16KBIT	3
#define	AR_EEPROM_CFG_DIS_WAIT_WRITE_COMPL	0x00000004	/* Disable wait for write completion */
#define	AR_EEPROM_CFG_CLOCK_M	0x00000018	/* Mask for EEPROM clock rate control */
#define	AR_EEPROM_CFG_CLOCK_S	3        	/* Shift for EEPROM clock rate control */
#define	AR_EEPROM_CFG_CLOCK_156KHZ	0
#define	AR_EEPROM_CFG_CLOCK_312KHZ	1
#define	AR_EEPROM_CFG_CLOCK_625KHZ	2
#define	AR_EEPROM_CFG_RESV0	0x000000E0	/* Reserved */
#define	AR_EEPROM_CFG_PROT_KEY_M	0x00FFFF00	/* Mask for EEPROM protection key */
#define	AR_EEPROM_CFG_PROT_KEY_S	8          	/* Shift for EEPROM protection key */
#define	AR_EEPROM_CFG_EN_L	0x01000000	/* EPRM_EN_L setting */

	/* MAC PCU Registers */
#define	AR_STA_ID1_SADH_MASK	0x0000FFFF	/* Mask for upper 16 bits of MAC addr */
#define	AR_STA_ID1_STA_AP	0x00010000	/* Device is AP */
#define	AR_STA_ID1_ADHOC	0x00020000	/* Device is ad-hoc */
#define	AR_STA_ID1_PWR_SAV	0x00040000	/* Power save reporting in self-generated frames */
#define	AR_STA_ID1_KSRCHDIS	0x00080000	/* Key search disable */
#define	AR_STA_ID1_PCF	0x00100000	/* Observe PCF */
#define	AR_STA_ID1_DEFAULT_ANTENNA	0x00200000	/* Use default antenna */
#define	AR_STA_ID1_DESC_ANTENNA	0x00400000	/* Update default antenna w/ TX antenna */
#define	AR_STA_ID1_RTS_USE_DEF	0x00800000	/* Use default antenna to send RTS */
#define	AR_STA_ID1_ACKCTS_6MB	0x01000000	/* Use 6Mb/s rate for ACK & CTS */
#define	AR_STA_ID1_BASE_RATE_11B	0x02000000	/* Use 11b base rate for ACK & CTS */
#define	AR_STA_ID1_BITS \
	"\20\20AP\21ADHOC\22PWR_SAV\23KSRCHDIS\25PCF"

#define	AR_BSS_ID1_U16_M	0x0000FFFF	/* Mask for upper 16 bits of BSSID */
#define	AR_BSS_ID1_AID_M	0xFFFF0000	/* Mask for association ID */
#define	AR_BSS_ID1_AID_S	16       	/* Shift for association ID */

#define	AR_SLOT_TIME_MASK	0x000007FF	/* Slot time mask */

#define	AR_TIME_OUT_ACK		0x00001FFF	/* Mask for ACK time-out */
#define	AR_TIME_OUT_ACK_S	0		/* Shift for ACK time-out */
#define	AR_TIME_OUT_CTS		0x1FFF0000	/* Mask for CTS time-out */
#define	AR_TIME_OUT_CTS_S	16       	/* Shift for CTS time-out */

#define	AR_RSSI_THR_MASK	0x000000FF	/* Mask for Beacon RSSI warning threshold */
#define	AR_RSSI_THR_BM_THR	0x0000FF00	/* Mask for Missed beacon threshold */
#define	AR_RSSI_THR_BM_THR_S	8        	/* Shift for Missed beacon threshold */

#define	AR_USEC_M	0x0000007F		/* Mask for clock cycles in 1 usec */
#define	AR_USEC_32_M	0x00003F80		/* Mask for number of 32MHz clock cycles in 1 usec */
#define	AR_USEC_32_S	7			/* Shift for number of 32MHz clock cycles in 1 usec */
/*
 * Tx/Rx latencies are to signal start and are in usecs.
 *
 * NOTE: AR5211/AR5311 difference: on Oahu, the TX latency field
 *       has increased from 6 bits to 9 bits.  The RX latency field
 *	 is unchanged, but is shifted over 3 bits.
 */
#define	AR5311_USEC_TX_LAT_M	0x000FC000	/* Tx latency */
#define	AR5311_USEC_TX_LAT_S	14
#define	AR5311_USEC_RX_LAT_M	0x03F00000	/* Rx latency */
#define	AR5311_USEC_RX_LAT_S	20

#define	AR5211_USEC_TX_LAT_M	0x007FC000	/* Tx latency */
#define	AR5211_USEC_TX_LAT_S	14
#define	AR5211_USEC_RX_LAT_M	0x1F800000	/* Rx latency */
#define	AR5211_USEC_RX_LAT_S	23


#define	AR_BEACON_PERIOD	0x0000FFFF	/* Beacon period in TU/msec */
#define	AR_BEACON_PERIOD_S	0		/* Byte offset of PERIOD start*/
#define	AR_BEACON_TIM		0x007F0000	/* Byte offset of TIM start */
#define	AR_BEACON_TIM_S		16        	/* Byte offset of TIM start */
#define	AR_BEACON_EN		0x00800000	/* beacon enable */
#define	AR_BEACON_RESET_TSF	0x01000000	/* Clears TSF to 0 */
#define	AR_BEACON_BITS	"\20\27ENABLE\30RESET_TSF"

#define	AR_RX_FILTER_ALL	0x00000000	/* Disallow all frames */
#define	AR_RX_UCAST		0x00000001	/* Allow unicast frames */
#define	AR_RX_MCAST		0x00000002	/* Allow multicast frames */
#define	AR_RX_BCAST		0x00000004	/* Allow broadcast frames */
#define	AR_RX_CONTROL		0x00000008	/* Allow control frames */
#define	AR_RX_BEACON		0x00000010	/* Allow beacon frames */
#define	AR_RX_PROM		0x00000020	/* Promiscuous mode */
#define	AR_RX_PHY_ERR		0x00000040	/* Allow all phy errors */
#define	AR_RX_PHY_RADAR		0x00000080	/* Allow radar phy errors */
#define	AR_RX_FILTER_BITS \
	"\20\1UCAST\2MCAST\3BCAST\4CONTROL\5BEACON\6PROMISC\7PHY_ERR\10PHY_RADAR"

#define	AR_DIAG_SW_CACHE_ACK	0x00000001	/* disable ACK if no valid key*/
#define	AR_DIAG_SW_DIS_ACK	0x00000002	/* disable ACK generation */
#define	AR_DIAG_SW_DIS_CTS	0x00000004	/* disable CTS generation */
#define	AR_DIAG_SW_DIS_ENCRYPT	0x00000008	/* disable encryption */
#define	AR_DIAG_SW_DIS_DECRYPT	0x00000010	/* disable decryption */
#define	AR_DIAG_SW_DIS_RX	0x00000020	/* disable receive */
#define	AR_DIAG_SW_CORR_FCS	0x00000080	/* corrupt FCS */
#define	AR_DIAG_SW_CHAN_INFO	0x00000100	/* dump channel info */
#define	AR_DIAG_SW_EN_SCRAMSD	0x00000200	/* enable fixed scrambler seed*/
#define	AR5311_DIAG_SW_USE_ECO	0x00000400	/* "super secret" use ECO enable bit */
#define	AR_DIAG_SW_SCRAM_SEED_M	0x0001FC00	/* Fixed scrambler seed mask */
#define	AR_DIAG_SW_SCRAM_SEED_S	10       	/* Fixed scrambler seed shfit */
#define	AR_DIAG_SW_FRAME_NV0	0x00020000	/* accept frames of non-zero protocol version */
#define	AR_DIAG_SW_OBS_PT_SEL_M	0x000C0000	/* Observation point select */
#define	AR_DIAG_SW_OBS_PT_SEL_S	18       	/* Observation point select */
#define	AR_DIAG_SW_BITS \
	"\20\1DIS_CACHE_ACK\2DIS_ACK\3DIS_CTS\4DIS_ENC\5DIS_DEC\6DIS_RX"\
	"\11CORR_FCS\12CHAN_INFO\13EN_SCRAM_SEED\14USE_ECO\24FRAME_NV0"

#define	AR_KEYTABLE_KEY0(n)	(AR_KEYTABLE(n) + 0)	/* key bit 0-31 */
#define	AR_KEYTABLE_KEY1(n)	(AR_KEYTABLE(n) + 4)	/* key bit 32-47 */
#define	AR_KEYTABLE_KEY2(n)	(AR_KEYTABLE(n) + 8)	/* key bit 48-79 */
#define	AR_KEYTABLE_KEY3(n)	(AR_KEYTABLE(n) + 12)	/* key bit 80-95 */
#define	AR_KEYTABLE_KEY4(n)	(AR_KEYTABLE(n) + 16)	/* key bit 96-127 */
#define	AR_KEYTABLE_TYPE(n)	(AR_KEYTABLE(n) + 20)	/* key type */
#define	AR_KEYTABLE_TYPE_40	0x00000000	/* WEP 40 bit key */
#define	AR_KEYTABLE_TYPE_104	0x00000001	/* WEP 104 bit key */
#define	AR_KEYTABLE_TYPE_128	0x00000003	/* WEP 128 bit key */
#define	AR_KEYTABLE_TYPE_AES	0x00000005	/* AES 128 bit key */
#define	AR_KEYTABLE_TYPE_CLR	0x00000007	/* no encryption */
#define	AR_KEYTABLE_MAC0(n)	(AR_KEYTABLE(n) + 24)	/* MAC address 1-32 */
#define	AR_KEYTABLE_MAC1(n)	(AR_KEYTABLE(n) + 28)	/* MAC address 33-47 */
#define	AR_KEYTABLE_VALID	0x00008000	/* key and MAC address valid */

#endif /* _DEV_ATH_AR5211REG_H */
