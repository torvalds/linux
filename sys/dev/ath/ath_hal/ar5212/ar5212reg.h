/*-
 * SPDX-License-Identifier: ISC
 *
 * Copyright (c) 2002-2008 Sam Leffler, Errno Consulting
 * Copyright (c) 2002-2008 Atheros Communications, Inc.
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
#ifndef _DEV_ATH_AR5212REG_H_
#define _DEV_ATH_AR5212REG_H_

/*
 * Definitions for the Atheros 5212 chipset.
 */

/* DMA Control and Interrupt Registers */
#define	AR_CR		0x0008	/* MAC control register */
#define	AR_RXDP		0x000C	/* MAC receive queue descriptor pointer */
#define	AR_CFG		0x0014	/* MAC configuration and status register */
#define	AR_IER		0x0024	/* MAC Interrupt enable register */
/* 0x28 is RTSD0 on the 5211 */
/* 0x2c is RTSD1 on the 5211 */
#define	AR_TXCFG	0x0030	/* MAC tx DMA size config register */
#define	AR_RXCFG	0x0034	/* MAC rx DMA size config register */
/* 0x38 is the jumbo descriptor address on the 5211 */
#define	AR_MIBC		0x0040	/* MAC MIB control register */
#define	AR_TOPS		0x0044	/* MAC timeout prescale count */
#define	AR_RXNPTO	0x0048	/* MAC no frame received timeout */
#define	AR_TXNPTO	0x004C	/* MAC no frame trasmitted timeout */
#define	AR_RPGTO	0x0050	/* MAC receive frame gap timeout */
#define	AR_RPCNT	0x0054	/* MAC receive frame count limit */
#define	AR_MACMISC	0x0058	/* MAC miscellaneous control/status register */
#define	AR_SPC_0	0x005c	/* MAC sleep performance (awake cycles) */
#define	AR_SPC_1	0x0060	/* MAC sleep performance (asleep cycles) */
/* 0x5c is for QCU/DCU clock gating control on 5311 */
#define	AR_ISR		0x0080	/* MAC Primary interrupt status register */
#define	AR_ISR_S0	0x0084	/* MAC Secondary interrupt status register 0 */
#define	AR_ISR_S1	0x0088	/* MAC Secondary interrupt status register 1 */
#define	AR_ISR_S2	0x008c	/* MAC Secondary interrupt status register 2 */
#define	AR_ISR_S3	0x0090	/* MAC Secondary interrupt status register 3 */
#define	AR_ISR_S4	0x0094	/* MAC Secondary interrupt status register 4 */
#define	AR_IMR		0x00a0	/* MAC Primary interrupt mask register */
#define	AR_IMR_S0	0x00a4	/* MAC Secondary interrupt mask register 0 */
#define	AR_IMR_S1	0x00a8	/* MAC Secondary interrupt mask register 1 */
#define	AR_IMR_S2	0x00ac	/* MAC Secondary interrupt mask register 2 */
#define	AR_IMR_S3	0x00b0	/* MAC Secondary interrupt mask register 3 */
#define	AR_IMR_S4	0x00b4	/* MAC Secondary interrupt mask register 4 */
#define	AR_ISR_RAC	0x00c0	/* ISR read-and-clear access */
/* Shadow copies with read-and-clear access */
#define	AR_ISR_S0_S	0x00c4	/* ISR_S0 shadow copy */
#define	AR_ISR_S1_S	0x00c8	/* ISR_S1 shadow copy */
#define	AR_ISR_S2_S	0x00cc	/* ISR_S2 shadow copy */
#define	AR_ISR_S3_S	0x00d0	/* ISR_S3 shadow copy */
#define	AR_ISR_S4_S	0x00d4	/* ISR_S4 shadow copy */
#define	AR_DMADBG_0	0x00e0	/* DMA debug 0 */
#define	AR_DMADBG_1	0x00e4	/* DMA debug 1 */
#define	AR_DMADBG_2	0x00e8	/* DMA debug 2 */
#define	AR_DMADBG_3	0x00ec	/* DMA debug 3 */
#define	AR_DMADBG_4	0x00f0	/* DMA debug 4 */
#define	AR_DMADBG_5	0x00f4	/* DMA debug 5 */
#define	AR_DMADBG_6	0x00f8	/* DMA debug 6 */
#define	AR_DMADBG_7	0x00fc	/* DMA debug 7 */
#define	AR_DCM_A	0x0400	/* Decompression mask address */
#define	AR_DCM_D	0x0404	/* Decompression mask data */
#define	AR_DCCFG	0x0420	/* Decompression configuration */
#define	AR_CCFG		0x0600	/* Compression configuration */
#define	AR_CCUCFG	0x0604	/* Compression catchup configuration */
#define	AR_CPC_0	0x0610	/* Compression performance counter 0 */
#define	AR_CPC_1	0x0614	/* Compression performance counter 1 */
#define	AR_CPC_2	0x0618	/* Compression performance counter 2 */
#define	AR_CPC_3	0x061c	/* Compression performance counter 3 */
#define	AR_CPCOVF	0x0620	/* Compression performance overflow status */

#define	AR_Q0_TXDP	0x0800	/* MAC Transmit Queue descriptor pointer */
#define	AR_Q1_TXDP	0x0804	/* MAC Transmit Queue descriptor pointer */
#define	AR_Q2_TXDP	0x0808	/* MAC Transmit Queue descriptor pointer */
#define	AR_Q3_TXDP	0x080c	/* MAC Transmit Queue descriptor pointer */
#define	AR_Q4_TXDP	0x0810	/* MAC Transmit Queue descriptor pointer */
#define	AR_Q5_TXDP	0x0814	/* MAC Transmit Queue descriptor pointer */
#define	AR_Q6_TXDP	0x0818	/* MAC Transmit Queue descriptor pointer */
#define	AR_Q7_TXDP	0x081c	/* MAC Transmit Queue descriptor pointer */
#define	AR_Q8_TXDP	0x0820	/* MAC Transmit Queue descriptor pointer */
#define	AR_Q9_TXDP	0x0824	/* MAC Transmit Queue descriptor pointer */
#define	AR_QTXDP(_i)	(AR_Q0_TXDP + ((_i)<<2))

#define	AR_Q_TXE	0x0840	/* MAC Transmit Queue enable */
#define	AR_Q_TXE_M	0x000003FF	/* Mask for TXE (QCU 0-9) */
#define	AR_Q_TXD	0x0880	/* MAC Transmit Queue disable */
#define	AR_Q_TXD_M	0x000003FF	/* Mask for TXD (QCU 0-9) */

#define	AR_Q0_CBRCFG	0x08c0	/* MAC CBR configuration */
#define	AR_Q1_CBRCFG	0x08c4	/* MAC CBR configuration */
#define	AR_Q2_CBRCFG	0x08c8	/* MAC CBR configuration */
#define	AR_Q3_CBRCFG	0x08cc	/* MAC CBR configuration */
#define	AR_Q4_CBRCFG	0x08d0	/* MAC CBR configuration */
#define	AR_Q5_CBRCFG	0x08d4	/* MAC CBR configuration */
#define	AR_Q6_CBRCFG	0x08d8	/* MAC CBR configuration */
#define	AR_Q7_CBRCFG	0x08dc	/* MAC CBR configuration */
#define	AR_Q8_CBRCFG	0x08e0	/* MAC CBR configuration */
#define	AR_Q9_CBRCFG	0x08e4	/* MAC CBR configuration */
#define	AR_QCBRCFG(_i)	(AR_Q0_CBRCFG + ((_i)<<2))

#define	AR_Q0_RDYTIMECFG	0x0900	/* MAC ReadyTime configuration */
#define	AR_Q1_RDYTIMECFG	0x0904	/* MAC ReadyTime configuration */
#define	AR_Q2_RDYTIMECFG	0x0908	/* MAC ReadyTime configuration */
#define	AR_Q3_RDYTIMECFG	0x090c	/* MAC ReadyTime configuration */
#define	AR_Q4_RDYTIMECFG	0x0910	/* MAC ReadyTime configuration */
#define	AR_Q5_RDYTIMECFG	0x0914	/* MAC ReadyTime configuration */
#define	AR_Q6_RDYTIMECFG	0x0918	/* MAC ReadyTime configuration */
#define	AR_Q7_RDYTIMECFG	0x091c	/* MAC ReadyTime configuration */
#define	AR_Q8_RDYTIMECFG	0x0920	/* MAC ReadyTime configuration */
#define	AR_Q9_RDYTIMECFG	0x0924	/* MAC ReadyTime configuration */
#define	AR_QRDYTIMECFG(_i)	(AR_Q0_RDYTIMECFG + ((_i)<<2))

#define	AR_Q_ONESHOTARM_SC	0x0940	/* MAC OneShotArm set control */
#define	AR_Q_ONESHOTARM_CC	0x0980	/* MAC OneShotArm clear control */

#define	AR_Q0_MISC	0x09c0	/* MAC Miscellaneous QCU settings */
#define	AR_Q1_MISC	0x09c4	/* MAC Miscellaneous QCU settings */
#define	AR_Q2_MISC	0x09c8	/* MAC Miscellaneous QCU settings */
#define	AR_Q3_MISC	0x09cc	/* MAC Miscellaneous QCU settings */
#define	AR_Q4_MISC	0x09d0	/* MAC Miscellaneous QCU settings */
#define	AR_Q5_MISC	0x09d4	/* MAC Miscellaneous QCU settings */
#define	AR_Q6_MISC	0x09d8	/* MAC Miscellaneous QCU settings */
#define	AR_Q7_MISC	0x09dc	/* MAC Miscellaneous QCU settings */
#define	AR_Q8_MISC	0x09e0	/* MAC Miscellaneous QCU settings */
#define	AR_Q9_MISC	0x09e4	/* MAC Miscellaneous QCU settings */
#define	AR_QMISC(_i)	(AR_Q0_MISC + ((_i)<<2))

#define	AR_Q0_STS	0x0a00	/* MAC Miscellaneous QCU status */
#define	AR_Q1_STS	0x0a04	/* MAC Miscellaneous QCU status */
#define	AR_Q2_STS	0x0a08	/* MAC Miscellaneous QCU status */
#define	AR_Q3_STS	0x0a0c	/* MAC Miscellaneous QCU status */
#define	AR_Q4_STS	0x0a10	/* MAC Miscellaneous QCU status */
#define	AR_Q5_STS	0x0a14	/* MAC Miscellaneous QCU status */
#define	AR_Q6_STS	0x0a18	/* MAC Miscellaneous QCU status */
#define	AR_Q7_STS	0x0a1c	/* MAC Miscellaneous QCU status */
#define	AR_Q8_STS	0x0a20	/* MAC Miscellaneous QCU status */
#define	AR_Q9_STS	0x0a24	/* MAC Miscellaneous QCU status */
#define	AR_QSTS(_i)	(AR_Q0_STS + ((_i)<<2))

#define	AR_Q_RDYTIMESHDN	0x0a40	/* MAC ReadyTimeShutdown status */
#define	AR_Q_CBBS	0xb00	/* Compression buffer base select */
#define	AR_Q_CBBA	0xb04	/* Compression buffer base access */
#define	AR_Q_CBC	0xb08	/* Compression buffer configuration */

#define	AR_D0_QCUMASK	0x1000	/* MAC QCU Mask */
#define	AR_D1_QCUMASK	0x1004	/* MAC QCU Mask */
#define	AR_D2_QCUMASK	0x1008	/* MAC QCU Mask */
#define	AR_D3_QCUMASK	0x100c	/* MAC QCU Mask */
#define	AR_D4_QCUMASK	0x1010	/* MAC QCU Mask */
#define	AR_D5_QCUMASK	0x1014	/* MAC QCU Mask */
#define	AR_D6_QCUMASK	0x1018	/* MAC QCU Mask */
#define	AR_D7_QCUMASK	0x101c	/* MAC QCU Mask */
#define	AR_D8_QCUMASK	0x1020	/* MAC QCU Mask */
#define	AR_D9_QCUMASK	0x1024	/* MAC QCU Mask */
#define	AR_DQCUMASK(_i)	(AR_D0_QCUMASK + ((_i)<<2))

#define	AR_D0_LCL_IFS	0x1040	/* MAC DCU-specific IFS settings */
#define	AR_D1_LCL_IFS	0x1044	/* MAC DCU-specific IFS settings */
#define	AR_D2_LCL_IFS	0x1048	/* MAC DCU-specific IFS settings */
#define	AR_D3_LCL_IFS	0x104c	/* MAC DCU-specific IFS settings */
#define	AR_D4_LCL_IFS	0x1050	/* MAC DCU-specific IFS settings */
#define	AR_D5_LCL_IFS	0x1054	/* MAC DCU-specific IFS settings */
#define	AR_D6_LCL_IFS	0x1058	/* MAC DCU-specific IFS settings */
#define	AR_D7_LCL_IFS	0x105c	/* MAC DCU-specific IFS settings */
#define	AR_D8_LCL_IFS	0x1060	/* MAC DCU-specific IFS settings */
#define	AR_D9_LCL_IFS	0x1064	/* MAC DCU-specific IFS settings */
#define	AR_DLCL_IFS(_i)	(AR_D0_LCL_IFS + ((_i)<<2))

#define	AR_D0_RETRY_LIMIT	0x1080	/* MAC Retry limits */
#define	AR_D1_RETRY_LIMIT	0x1084	/* MAC Retry limits */
#define	AR_D2_RETRY_LIMIT	0x1088	/* MAC Retry limits */
#define	AR_D3_RETRY_LIMIT	0x108c	/* MAC Retry limits */
#define	AR_D4_RETRY_LIMIT	0x1090	/* MAC Retry limits */
#define	AR_D5_RETRY_LIMIT	0x1094	/* MAC Retry limits */
#define	AR_D6_RETRY_LIMIT	0x1098	/* MAC Retry limits */
#define	AR_D7_RETRY_LIMIT	0x109c	/* MAC Retry limits */
#define	AR_D8_RETRY_LIMIT	0x10a0	/* MAC Retry limits */
#define	AR_D9_RETRY_LIMIT	0x10a4	/* MAC Retry limits */
#define	AR_DRETRY_LIMIT(_i)	(AR_D0_RETRY_LIMIT + ((_i)<<2))

#define	AR_D0_CHNTIME	0x10c0	/* MAC ChannelTime settings */
#define	AR_D1_CHNTIME	0x10c4	/* MAC ChannelTime settings */
#define	AR_D2_CHNTIME	0x10c8	/* MAC ChannelTime settings */
#define	AR_D3_CHNTIME	0x10cc	/* MAC ChannelTime settings */
#define	AR_D4_CHNTIME	0x10d0	/* MAC ChannelTime settings */
#define	AR_D5_CHNTIME	0x10d4	/* MAC ChannelTime settings */
#define	AR_D6_CHNTIME	0x10d8	/* MAC ChannelTime settings */
#define	AR_D7_CHNTIME	0x10dc	/* MAC ChannelTime settings */
#define	AR_D8_CHNTIME	0x10e0	/* MAC ChannelTime settings */
#define	AR_D9_CHNTIME	0x10e4	/* MAC ChannelTime settings */
#define	AR_DCHNTIME(_i)	(AR_D0_CHNTIME + ((_i)<<2))

#define	AR_D0_MISC	0x1100	/* MAC Miscellaneous DCU-specific settings */
#define	AR_D1_MISC	0x1104	/* MAC Miscellaneous DCU-specific settings */
#define	AR_D2_MISC	0x1108	/* MAC Miscellaneous DCU-specific settings */
#define	AR_D3_MISC	0x110c	/* MAC Miscellaneous DCU-specific settings */
#define	AR_D4_MISC	0x1110	/* MAC Miscellaneous DCU-specific settings */
#define	AR_D5_MISC	0x1114	/* MAC Miscellaneous DCU-specific settings */
#define	AR_D6_MISC	0x1118	/* MAC Miscellaneous DCU-specific settings */
#define	AR_D7_MISC	0x111c	/* MAC Miscellaneous DCU-specific settings */
#define	AR_D8_MISC	0x1120	/* MAC Miscellaneous DCU-specific settings */
#define	AR_D9_MISC	0x1124	/* MAC Miscellaneous DCU-specific settings */
#define	AR_DMISC(_i)	(AR_D0_MISC + ((_i)<<2))

#define	AR_D_SEQNUM	0x1140	/* MAC Frame sequence number */

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

#define	AR_RC		0x4000	/* Warm reset control register */
#define	AR_SCR		0x4004	/* Sleep control register */
#define	AR_INTPEND	0x4008	/* Interrupt Pending register */
#define	AR_SFR		0x400C	/* Sleep force register */
#define	AR_PCICFG	0x4010	/* PCI configuration register */
#define	AR_GPIOCR	0x4014	/* GPIO control register */
#define	AR_GPIODO	0x4018	/* GPIO data output access register */
#define	AR_GPIODI	0x401C	/* GPIO data input access register */
#define	AR_SREV		0x4020	/* Silicon Revision register */
#define	AR_TXEPOST	0x4028	/* TXE write posting resgister */
#define	AR_QSM		0x402C	/* QCU sleep mask */

#define	AR_PCIE_PMC	0x4068	/* PCIe power mgt config and status register */
#define AR_PCIE_SERDES	0x4080  /* PCIe Serdes register */
#define AR_PCIE_SERDES2	0x4084  /* PCIe Serdes register */

#define	AR_EEPROM_ADDR	0x6000	/* EEPROM address register (10 bit) */
#define	AR_EEPROM_DATA	0x6004	/* EEPROM data register (16 bit) */
#define	AR_EEPROM_CMD	0x6008	/* EEPROM command register */
#define	AR_EEPROM_STS	0x600c	/* EEPROM status register */
#define	AR_EEPROM_CFG	0x6010	/* EEPROM configuration register */

#define	AR_STA_ID0	0x8000	/* MAC station ID0 register - low 32 bits */
#define	AR_STA_ID1	0x8004	/* MAC station ID1 register - upper 16 bits */
#define	AR_BSS_ID0	0x8008	/* MAC BSSID low 32 bits */
#define	AR_BSS_ID1	0x800C	/* MAC BSSID upper 16 bits / AID */
#define	AR_SLOT_TIME	0x8010	/* MAC Time-out after a collision */
#define	AR_TIME_OUT	0x8014	/* MAC ACK & CTS time-out */
#define	AR_RSSI_THR	0x8018	/* MAC RSSI warning & missed beacon threshold */
#define	AR_USEC		0x801c	/* MAC transmit latency register */
#define	AR_BEACON	0x8020	/* MAC beacon control value/mode bits */
#define	AR_CFP_PERIOD	0x8024	/* MAC CFP Interval (TU/msec) */
#define	AR_TIMER0	0x8028	/* MAC Next beacon time (TU/msec) */
#define	AR_TIMER1	0x802c	/* MAC DMA beacon alert time (1/8 TU) */
#define	AR_TIMER2	0x8030	/* MAC Software beacon alert (1/8 TU) */
#define	AR_TIMER3	0x8034	/* MAC ATIM window time */
#define	AR_CFP_DUR	0x8038	/* MAC maximum CFP duration in TU */
#define	AR_RX_FILTER	0x803C	/* MAC receive filter register */
#define	AR_MCAST_FIL0	0x8040	/* MAC multicast filter lower 32 bits */
#define	AR_MCAST_FIL1	0x8044	/* MAC multicast filter upper 32 bits */
#define	AR_DIAG_SW	0x8048	/* MAC PCU control register */
#define	AR_TSF_L32	0x804c	/* MAC local clock lower 32 bits */
#define	AR_TSF_U32	0x8050	/* MAC local clock upper 32 bits */
#define	AR_TST_ADDAC	0x8054	/* ADDAC test register */
#define	AR_DEF_ANTENNA	0x8058	/* default antenna register */
#define	AR_QOS_MASK	0x805c	/* MAC AES mute mask: QoS field */
#define	AR_SEQ_MASK	0x8060	/* MAC AES mute mask: seqnum field */
#define	AR_OBSERV_2	0x8068	/* Observation bus 2 */
#define	AR_OBSERV_1	0x806c	/* Observation bus 1 */

#define	AR_LAST_TSTP	0x8080	/* MAC Time stamp of the last beacon received */
#define	AR_NAV		0x8084	/* MAC current NAV value */
#define	AR_RTS_OK	0x8088	/* MAC RTS exchange success counter */
#define	AR_RTS_FAIL	0x808c	/* MAC RTS exchange failure counter */
#define	AR_ACK_FAIL	0x8090	/* MAC ACK failure counter */
#define	AR_FCS_FAIL	0x8094	/* FCS check failure counter */
#define	AR_BEACON_CNT	0x8098	/* Valid beacon counter */

#define	AR_SLEEP1	0x80d4	/* Enhanced sleep control 1 */
#define	AR_SLEEP2	0x80d8	/* Enhanced sleep control 2 */
#define	AR_SLEEP3	0x80dc	/* Enhanced sleep control 3 */
#define	AR_BSSMSKL	0x80e0	/* BSSID mask lower 32 bits */
#define	AR_BSSMSKU	0x80e4	/* BSSID mask upper 16 bits */
#define	AR_TPC		0x80e8	/* Transmit power control for self gen frames */
#define	AR_TFCNT	0x80ec	/* Profile count, transmit frames */
#define	AR_RFCNT	0x80f0	/* Profile count, receive frames */
#define	AR_RCCNT	0x80f4	/* Profile count, receive clear */
#define	AR_CCCNT	0x80f8	/* Profile count, cycle counter */

#define AR_QUIET1   0x80fc  /* Quiet time programming for TGh */
#define AR_QUIET1_NEXT_QUIET_S  0   /* TSF of next quiet period (TU) */
#define AR_QUIET1_NEXT_QUIET    0xffff
#define AR_QUIET1_QUIET_ENABLE  0x10000 /* Enable Quiet time operation */
#define AR_QUIET1_QUIET_ACK_CTS_ENABLE  0x20000 /* Do we ack/cts during quiet period */
#define	AR_QUIET1_QUIET_ACK_CTS_ENABLE_S 17

#define AR_QUIET2   0x8100  /* More Quiet time programming */
#define AR_QUIET2_QUIET_PER_S   0   /* Periodicity of quiet period (TU) */
#define AR_QUIET2_QUIET_PER 0xffff
#define AR_QUIET2_QUIET_DUR_S   16  /* Duration of quiet period (TU) */
#define AR_QUIET2_QUIET_DUR 0xffff0000

#define	AR_TSF_PARM	0x8104	/* TSF parameters */
#define AR_NOACK        0x8108  /* No ack policy in QoS Control Field */ 
#define	AR_PHY_ERR	0x810c	/* Phy error filter */

#define	AR_QOS_CONTROL	0x8118	/* Control TKIP MIC for QoS */
#define	AR_QOS_SELECT	0x811c	/* MIC QoS select */
#define	AR_MISC_MODE	0x8120	/* PCU Misc. mode control */

/* Hainan MIB counter registers */
#define	AR_FILTOFDM	0x8124	/* Count of filtered OFDM frames */
#define	AR_FILTCCK	0x8128	/* Count of filtered CCK frames */
#define	AR_PHYCNT1	0x812c	/* Phy Error 1 counter */
#define	AR_PHYCNTMASK1	0x8130	/* Phy Error 1 counter mask */
#define	AR_PHYCNT2	0x8134	/* Phy Error 2 counter */
#define	AR_PHYCNTMASK2	0x8138	/* Phy Error 2 counter mask */
#define	AR_PHY_COUNTMAX	(3 << 22)	/* Max value in counter before intr */
#define	AR_MIBCNT_INTRMASK (3<<22)	/* Mask for top two bits of counters */

#define	AR_RATE_DURATION_0	0x8700		/* base of multi-rate retry */
#define	AR_RATE_DURATION(_n)	(AR_RATE_DURATION_0 + ((_n)<<2))

#define	AR_KEYTABLE_0	0x8800	/* MAC Key Cache */
#define	AR_KEYTABLE(_n)	(AR_KEYTABLE_0 + ((_n)*32))

#define	AR_CFP_MASK	0x0000ffff /* Mask for next beacon time */

#define	AR_CR_RXE	0x00000004 /* Receive enable */
#define	AR_CR_RXD	0x00000020 /* Receive disable */
#define	AR_CR_SWI	0x00000040 /* One-shot software interrupt */

#define	AR_CFG_SWTD	0x00000001 /* byteswap tx descriptor words */
#define	AR_CFG_SWTB	0x00000002 /* byteswap tx data buffer words */
#define	AR_CFG_SWRD	0x00000004 /* byteswap rx descriptor words */
#define	AR_CFG_SWRB	0x00000008 /* byteswap rx data buffer words */
#define	AR_CFG_SWRG	0x00000010 /* byteswap register access data words */
#define	AR_CFG_AP_ADHOC_INDICATION	0x00000020 /* AP/adhoc indication (0-AP, 1-Adhoc) */
#define	AR_CFG_PHOK	0x00000100 /* PHY OK status */
#define	AR_CFG_EEBS	0x00000200 /* EEPROM busy */
#define	AR_5211_CFG_CLK_GATE_DIS	0x00000400 /* Clock gating disable (Oahu only) */
#define	AR_CFG_PCI_MASTER_REQ_Q_THRESH	0x00060000 /* Mask of PCI core master request queue full threshold */
#define	AR_CFG_PCI_MASTER_REQ_Q_THRESH_S	17         /* Shift for PCI core master request queue full threshold */

#define	AR_IER_ENABLE	0x00000001 /* Global interrupt enable */
#define	AR_IER_DISABLE	0x00000000 /* Global interrupt disable */

#define	AR_DMASIZE_4B	0x00000000 /* DMA size 4 bytes (TXCFG + RXCFG) */
#define	AR_DMASIZE_8B	0x00000001 /* DMA size 8 bytes */
#define	AR_DMASIZE_16B	0x00000002 /* DMA size 16 bytes */
#define	AR_DMASIZE_32B	0x00000003 /* DMA size 32 bytes */
#define	AR_DMASIZE_64B	0x00000004 /* DMA size 64 bytes */
#define	AR_DMASIZE_128B	0x00000005 /* DMA size 128 bytes */
#define	AR_DMASIZE_256B	0x00000006 /* DMA size 256 bytes */
#define	AR_DMASIZE_512B	0x00000007 /* DMA size 512 bytes */

#define	AR_FTRIG	0x000003F0 /* Mask for Frame trigger level */
#define	AR_FTRIG_S	4          /* Shift for Frame trigger level */
#define	AR_FTRIG_IMMED	0x00000000 /* bytes in PCU TX FIFO before air */
#define	AR_FTRIG_64B	0x00000010 /* default */
#define	AR_FTRIG_128B	0x00000020
#define	AR_FTRIG_192B	0x00000030
#define	AR_FTRIG_256B	0x00000040 /* 5 bits total */

#define	AR_RXCFG_ZLFDMA	0x00000010 /* Enable DMA of zero-length frame */

#define	AR_MIBC_COW	0x00000001 /* counter overflow warning */
#define	AR_MIBC_FMC	0x00000002 /* freeze MIB counters */
#define	AR_MIBC_CMC	0x00000004 /* clear MIB counters */
#define	AR_MIBC_MCS	0x00000008 /* MIB counter strobe, increment all */

#define	AR_TOPS_MASK	0x0000FFFF /* Mask for timeout prescale */

#define	AR_RXNPTO_MASK	0x000003FF /* Mask for no frame received timeout */

#define	AR_TXNPTO_MASK	0x000003FF /* Mask for no frame transmitted timeout */
#define	AR_TXNPTO_QCU_MASK	0x000FFC00 /* Mask indicating the set of QCUs */
				 /* for which frame completions will cause */
				 /* a reset of the no frame xmit'd timeout */

#define	AR_RPGTO_MASK	0x000003FF /* Mask for receive frame gap timeout */

#define	AR_RPCNT_MASK	0x0000001F /* Mask for receive frame count limit */

#define	AR_MACMISC_DMA_OBS	0x000001E0 /* Mask for DMA observation bus mux select */
#define	AR_MACMISC_DMA_OBS_S	5          /* Shift for DMA observation bus mux select */
#define	AR_MACMISC_MISC_OBS	0x00000E00 /* Mask for MISC observation bus mux select */
#define	AR_MACMISC_MISC_OBS_S	9          /* Shift for MISC observation bus mux select */
#define	AR_MACMISC_MAC_OBS_BUS_LSB	0x00007000 /* Mask for MAC observation bus mux select (lsb) */
#define	AR_MACMISC_MAC_OBS_BUS_LSB_S	12         /* Shift for MAC observation bus mux select (lsb) */
#define	AR_MACMISC_MAC_OBS_BUS_MSB	0x00038000 /* Mask for MAC observation bus mux select (msb) */
#define	AR_MACMISC_MAC_OBS_BUS_MSB_S	15         /* Shift for MAC observation bus mux select (msb) */

/*
 * Interrupt Status Registers
 *
 * Only the bits in the ISR_P register and the IMR_P registers
 * control whether the MAC's INTA# output is asserted.  The bits in
 * the secondary interrupt status/mask registers control what bits
 * are set in the primary interrupt status register; however the
 * IMR_S* registers DO NOT determine whether INTA# is asserted.
 * That is INTA# is asserted only when the logical AND of ISR_P
 * and IMR_P is non-zero.  The secondary interrupt mask/status
 * registers affect what bits are set in ISR_P but they do not
 * directly affect whether INTA# is asserted.
 */
#define	AR_ISR_RXOK	0x00000001 /* At least one frame received sans errors */
#define	AR_ISR_RXDESC	0x00000002 /* Receive interrupt request */
#define	AR_ISR_RXERR	0x00000004 /* Receive error interrupt */
#define	AR_ISR_RXNOPKT	0x00000008 /* No frame received within timeout clock */
#define	AR_ISR_RXEOL	0x00000010 /* Received descriptor empty interrupt */
#define	AR_ISR_RXORN	0x00000020 /* Receive FIFO overrun interrupt */
#define	AR_ISR_TXOK	0x00000040 /* Transmit okay interrupt */
#define	AR_ISR_TXDESC	0x00000080 /* Transmit interrupt request */
#define	AR_ISR_TXERR	0x00000100 /* Transmit error interrupt */
#define	AR_ISR_TXNOPKT	0x00000200 /* No frame transmitted interrupt */
#define	AR_ISR_TXEOL	0x00000400 /* Transmit descriptor empty interrupt */
#define	AR_ISR_TXURN	0x00000800 /* Transmit FIFO underrun interrupt */
#define	AR_ISR_MIB	0x00001000 /* MIB interrupt - see MIBC */
#define	AR_ISR_SWI	0x00002000 /* Software interrupt */
#define	AR_ISR_RXPHY	0x00004000 /* PHY receive error interrupt */
#define	AR_ISR_RXKCM	0x00008000 /* Key-cache miss interrupt */
#define	AR_ISR_SWBA	0x00010000 /* Software beacon alert interrupt */
#define	AR_ISR_BRSSI	0x00020000 /* Beacon threshold interrupt */
#define	AR_ISR_BMISS	0x00040000 /* Beacon missed interrupt */
#define	AR_ISR_HIUERR	0x00080000 /* An unexpected bus error has occurred */
#define	AR_ISR_BNR	0x00100000 /* Beacon not ready interrupt */
#define	AR_ISR_RXCHIRP	0x00200000 /* Phy received a 'chirp' */
#define	AR_ISR_RXDOPPL	0x00400000 /* Phy received a 'doppler chirp' */
#define	AR_ISR_BCNMISC	0x00800000 /* 'or' of TIM, CABEND, DTIMSYNC, BCNTO,
				      CABTO, DTIM bits from ISR_S2 */
#define	AR_ISR_TIM	0x00800000 /* TIM interrupt */
#define	AR_ISR_GPIO	0x01000000 /* GPIO Interrupt */
#define	AR_ISR_QCBROVF	0x02000000 /* QCU CBR overflow interrupt */
#define	AR_ISR_QCBRURN	0x04000000 /* QCU CBR underrun interrupt */
#define	AR_ISR_QTRIG	0x08000000 /* QCU scheduling trigger interrupt */
#define	AR_ISR_RESV0	0xF0000000 /* Reserved */

#define	AR_ISR_S0_QCU_TXOK	0x000003FF /* Mask for TXOK (QCU 0-9) */
#define AR_ISR_S0_QCU_TXOK_S	0
#define	AR_ISR_S0_QCU_TXDESC	0x03FF0000 /* Mask for TXDESC (QCU 0-9) */
#define AR_ISR_S0_QCU_TXDESC_S	16

#define	AR_ISR_S1_QCU_TXERR	0x000003FF /* Mask for TXERR (QCU 0-9) */
#define AR_ISR_S1_QCU_TXERR_S	0
#define	AR_ISR_S1_QCU_TXEOL	0x03FF0000 /* Mask for TXEOL (QCU 0-9) */
#define AR_ISR_S1_QCU_TXEOL_S	16

#define	AR_ISR_S2_QCU_TXURN	0x000003FF /* Mask for TXURN (QCU 0-9) */
#define	AR_ISR_S2_MCABT		0x00010000 /* Master cycle abort interrupt */
#define	AR_ISR_S2_SSERR		0x00020000 /* SERR interrupt */
#define	AR_ISR_S2_DPERR		0x00040000 /* PCI bus parity error */
#define	AR_ISR_S2_TIM		0x01000000 /* TIM */
#define	AR_ISR_S2_CABEND	0x02000000 /* CABEND */
#define	AR_ISR_S2_DTIMSYNC	0x04000000 /* DTIMSYNC */
#define	AR_ISR_S2_BCNTO		0x08000000 /* BCNTO */
#define	AR_ISR_S2_CABTO		0x10000000 /* CABTO */
#define	AR_ISR_S2_DTIM		0x20000000 /* DTIM */
#define	AR_ISR_S2_TSFOOR	0x40000000 /* TSF OOR */
#define	AR_ISR_S2_TBTT		0x80000000 /* TBTT timer */

#define	AR_ISR_S3_QCU_QCBROVF	0x000003FF /* Mask for QCBROVF (QCU 0-9) */
#define	AR_ISR_S3_QCU_QCBRURN	0x03FF0000 /* Mask for QCBRURN (QCU 0-9) */

#define	AR_ISR_S4_QCU_QTRIG	0x000003FF /* Mask for QTRIG (QCU 0-9) */
#define	AR_ISR_S4_RESV0		0xFFFFFC00 /* Reserved */

/*
 * Interrupt Mask Registers
 *
 * Only the bits in the IMR control whether the MAC's INTA#
 * output will be asserted.  The bits in the secondary interrupt
 * mask registers control what bits get set in the primary
 * interrupt status register; however the IMR_S* registers
 * DO NOT determine whether INTA# is asserted.
 */
#define	AR_IMR_RXOK	0x00000001 /* At least one frame received sans errors */
#define	AR_IMR_RXDESC	0x00000002 /* Receive interrupt request */
#define	AR_IMR_RXERR	0x00000004 /* Receive error interrupt */
#define	AR_IMR_RXNOPKT	0x00000008 /* No frame received within timeout clock */
#define	AR_IMR_RXEOL	0x00000010 /* Received descriptor empty interrupt */
#define	AR_IMR_RXORN	0x00000020 /* Receive FIFO overrun interrupt */
#define	AR_IMR_TXOK	0x00000040 /* Transmit okay interrupt */
#define	AR_IMR_TXDESC	0x00000080 /* Transmit interrupt request */
#define	AR_IMR_TXERR	0x00000100 /* Transmit error interrupt */
#define	AR_IMR_TXNOPKT	0x00000200 /* No frame transmitted interrupt */
#define	AR_IMR_TXEOL	0x00000400 /* Transmit descriptor empty interrupt */
#define	AR_IMR_TXURN	0x00000800 /* Transmit FIFO underrun interrupt */
#define	AR_IMR_MIB	0x00001000 /* MIB interrupt - see MIBC */
#define	AR_IMR_SWI	0x00002000 /* Software interrupt */
#define	AR_IMR_RXPHY	0x00004000 /* PHY receive error interrupt */
#define	AR_IMR_RXKCM	0x00008000 /* Key-cache miss interrupt */
#define	AR_IMR_SWBA	0x00010000 /* Software beacon alert interrupt */
#define	AR_IMR_BRSSI	0x00020000 /* Beacon threshold interrupt */
#define	AR_IMR_BMISS	0x00040000 /* Beacon missed interrupt */
#define	AR_IMR_HIUERR	0x00080000 /* An unexpected bus error has occurred */
#define	AR_IMR_BNR	0x00100000 /* BNR interrupt */
#define	AR_IMR_RXCHIRP	0x00200000 /* RXCHIRP interrupt */
#define	AR_IMR_BCNMISC	0x00800000 /* Venice: BCNMISC */
#define	AR_IMR_TIM	0x00800000 /* TIM interrupt */
#define	AR_IMR_GPIO	0x01000000 /* GPIO Interrupt */
#define	AR_IMR_QCBROVF	0x02000000 /* QCU CBR overflow interrupt */
#define	AR_IMR_QCBRURN	0x04000000 /* QCU CBR underrun interrupt */
#define	AR_IMR_QTRIG	0x08000000 /* QCU scheduling trigger interrupt */
#define	AR_IMR_RESV0	0xF0000000 /* Reserved */

#define	AR_IMR_S0_QCU_TXOK	0x000003FF /* TXOK (QCU 0-9) */
#define	AR_IMR_S0_QCU_TXOK_S	0
#define	AR_IMR_S0_QCU_TXDESC	0x03FF0000 /* TXDESC (QCU 0-9) */
#define	AR_IMR_S0_QCU_TXDESC_S	16

#define	AR_IMR_S1_QCU_TXERR	0x000003FF /* TXERR (QCU 0-9) */
#define	AR_IMR_S1_QCU_TXERR_S	0
#define	AR_IMR_S1_QCU_TXEOL	0x03FF0000 /* TXEOL (QCU 0-9) */
#define	AR_IMR_S1_QCU_TXEOL_S	16

#define	AR_IMR_S2_QCU_TXURN	0x000003FF /* Mask for TXURN (QCU 0-9) */
#define	AR_IMR_S2_QCU_TXURN_S	0
#define	AR_IMR_S2_MCABT		0x00010000 /* Master cycle abort interrupt */
#define	AR_IMR_S2_SSERR		0x00020000 /* SERR interrupt */
#define	AR_IMR_S2_DPERR		0x00040000 /* PCI bus parity error */
#define	AR_IMR_S2_TIM		0x01000000 /* TIM */
#define	AR_IMR_S2_CABEND	0x02000000 /* CABEND */
#define	AR_IMR_S2_DTIMSYNC	0x04000000 /* DTIMSYNC */
#define	AR_IMR_S2_BCNTO		0x08000000 /* BCNTO */
#define	AR_IMR_S2_CABTO		0x10000000 /* CABTO */
#define	AR_IMR_S2_DTIM		0x20000000 /* DTIM */
#define	AR_IMR_S2_TSFOOR	0x40000000 /* TSF OOR */
#define	AR_IMR_S2_TBTT		0x80000000 /* TBTT timer */

/* AR_IMR_SR2 bits that correspond to AR_IMR_BCNMISC */
#define	AR_IMR_SR2_BCNMISC \
	(AR_IMR_S2_TIM | AR_IMR_S2_DTIM | AR_IMR_S2_DTIMSYNC | \
	 AR_IMR_S2_CABEND | AR_IMR_S2_CABTO  | AR_IMR_S2_TSFOOR | \
	 AR_IMR_S2_TBTT)

#define	AR_IMR_S3_QCU_QCBROVF	0x000003FF /* Mask for QCBROVF (QCU 0-9) */
#define	AR_IMR_S3_QCU_QCBRURN	0x03FF0000 /* Mask for QCBRURN (QCU 0-9) */
#define	AR_IMR_S3_QCU_QCBRURN_S	16         /* Shift for QCBRURN (QCU 0-9) */

#define	AR_IMR_S4_QCU_QTRIG	0x000003FF /* Mask for QTRIG (QCU 0-9) */
#define	AR_IMR_S4_RESV0		0xFFFFFC00 /* Reserved */

/* QCU registers */
#define	AR_NUM_QCU	10     /* Only use QCU 0-9 for forward QCU compatibility */
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

#define	AR_Q_CBRCFG_CBR_INTERVAL	0x00FFFFFF /* Mask for CBR interval (us) */
#define AR_Q_CBRCFG_CBR_INTERVAL_S      0   /* Shift for CBR interval */
#define	AR_Q_CBRCFG_CBR_OVF_THRESH	0xFF000000 /* Mask for CBR overflow threshold */
#define AR_Q_CBRCFG_CBR_OVF_THRESH_S    24  /* Shift for CBR overflow thresh */

#define	AR_Q_RDYTIMECFG_INT	0x00FFFFFF /* CBR interval (us) */
#define AR_Q_RDYTIMECFG_INT_S   0  // Shift for ReadyTime Interval (us) */
#define	AR_Q_RDYTIMECFG_ENA	0x01000000 /* CBR enable */
/* bits 25-31 are reserved */

#define	AR_Q_MISC_FSP		0x0000000F /* Frame Scheduling Policy mask */
#define	AR_Q_MISC_FSP_ASAP		0	/* ASAP */
#define	AR_Q_MISC_FSP_CBR		1	/* CBR */
#define	AR_Q_MISC_FSP_DBA_GATED		2	/* DMA Beacon Alert gated */
#define	AR_Q_MISC_FSP_TIM_GATED		3	/* TIM gated */
#define	AR_Q_MISC_FSP_BEACON_SENT_GATED	4	/* Beacon-sent-gated */
#define	AR_Q_MISC_FSP_S		0
#define	AR_Q_MISC_ONE_SHOT_EN	0x00000010 /* OneShot enable */
#define	AR_Q_MISC_CBR_INCR_DIS1	0x00000020 /* Disable CBR expired counter incr
					      (empty q) */
#define	AR_Q_MISC_CBR_INCR_DIS0	0x00000040 /* Disable CBR expired counter incr
					      (empty beacon q) */
#define	AR_Q_MISC_BEACON_USE	0x00000080 /* Beacon use indication */
#define	AR_Q_MISC_CBR_EXP_CNTR_LIMIT	0x00000100 /* CBR expired counter limit enable */
#define	AR_Q_MISC_RDYTIME_EXP_POLICY	0x00000200 /* Enable TXE cleared on ReadyTime expired or VEOL */
#define	AR_Q_MISC_RESET_CBR_EXP_CTR	0x00000400 /* Reset CBR expired counter */
#define	AR_Q_MISC_DCU_EARLY_TERM_REQ	0x00000800 /* DCU frame early termination request control */
#define	AR_Q_MISC_QCU_COMP_EN	0x00001000 /* QCU frame compression enable */
#define	AR_Q_MISC_RESV0		0xFFFFF000 /* Reserved */

#define	AR_Q_STS_PEND_FR_CNT	0x00000003 /* Mask for Pending Frame Count */
#define	AR_Q_STS_RESV0		0x000000FC /* Reserved */
#define	AR_Q_STS_CBR_EXP_CNT	0x0000FF00 /* Mask for CBR expired counter */
#define	AR_Q_STS_RESV1		0xFFFF0000 /* Reserved */

/* DCU registers */
#define	AR_NUM_DCU	10     /* Only use 10 DCU's for forward QCU/DCU compatibility */
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

#define	AR_D_QCUMASK		0x000003FF /* Mask for QCU Mask (QCU 0-9) */
#define	AR_D_QCUMASK_RESV0	0xFFFFFC00 /* Reserved */

#define	AR_D_LCL_IFS_CWMIN	0x000003FF /* Mask for CW_MIN */
#define	AR_D_LCL_IFS_CWMIN_S	0
#define	AR_D_LCL_IFS_CWMAX	0x000FFC00 /* Mask for CW_MAX */
#define	AR_D_LCL_IFS_CWMAX_S	10
#define	AR_D_LCL_IFS_AIFS	0x0FF00000 /* Mask for AIFS */
#define	AR_D_LCL_IFS_AIFS_S	20
/*
 *  Note:  even though this field is 8 bits wide the
 *  maximum supported AIFS value is 0xfc.  Setting the AIFS value
 *  to 0xfd 0xfe, or 0xff will not work correctly and will cause
 *  the DCU to hang.
 */
#define	AR_D_LCL_IFS_RESV0	0xF0000000 /* Reserved */

#define	AR_D_RETRY_LIMIT_FR_SH	0x0000000F /* frame short retry limit */
#define	AR_D_RETRY_LIMIT_FR_SH_S	0
#define	AR_D_RETRY_LIMIT_FR_LG	0x000000F0 /* frame long retry limit */
#define	AR_D_RETRY_LIMIT_FR_LG_S	4
#define	AR_D_RETRY_LIMIT_STA_SH	0x00003F00 /* station short retry limit */
#define	AR_D_RETRY_LIMIT_STA_SH_S	8
#define	AR_D_RETRY_LIMIT_STA_LG	0x000FC000 /* station short retry limit */
#define	AR_D_RETRY_LIMIT_STA_LG_S	14
#define	AR_D_RETRY_LIMIT_RESV0		0xFFF00000 /* Reserved */

#define	AR_D_CHNTIME_DUR		0x000FFFFF /* ChannelTime duration (us) */
#define AR_D_CHNTIME_DUR_S              0 /* Shift for ChannelTime duration */
#define	AR_D_CHNTIME_EN			0x00100000 /* ChannelTime enable */
#define	AR_D_CHNTIME_RESV0		0xFFE00000 /* Reserved */

#define	AR_D_MISC_BKOFF_THRESH	0x0000003F /* Backoff threshold */
#define	AR_D_MISC_ETS_RTS		0x00000040 /* End of transmission series
						      station RTS/data failure
						      count reset policy */
#define	AR_D_MISC_ETS_CW		0x00000080 /* End of transmission series
						      CW reset policy */
#define AR_D_MISC_FRAG_WAIT_EN          0x00000100 /* Wait for next fragment */
#define AR_D_MISC_FRAG_BKOFF_EN         0x00000200 /* Backoff during a frag burst */
#define	AR_D_MISC_HCF_POLL_EN		0x00000800 /* HFC poll enable */
#define	AR_D_MISC_BKOFF_PERSISTENCE	0x00001000 /* Backoff persistence factor
						      setting */
#define	AR_D_MISC_FR_PREFETCH_EN	0x00002000 /* Frame prefetch enable */
#define	AR_D_MISC_VIR_COL_HANDLING	0x0000C000 /* Mask for Virtual collision
						      handling policy */
#define	AR_D_MISC_VIR_COL_HANDLING_S	14
/* FOO redefined for venice CW increment policy */
#define	AR_D_MISC_VIR_COL_HANDLING_DEFAULT	0	/* Normal */
#define	AR_D_MISC_VIR_COL_HANDLING_IGNORE	1	/* Ignore */
#define	AR_D_MISC_BEACON_USE		0x00010000 /* Beacon use indication */
#define	AR_D_MISC_ARB_LOCKOUT_CNTRL	0x00060000 /* DCU arbiter lockout ctl */
#define	AR_D_MISC_ARB_LOCKOUT_CNTRL_S	17         /* DCU arbiter lockout ctl */
#define	AR_D_MISC_ARB_LOCKOUT_CNTRL_NONE	0	/* No lockout */
#define	AR_D_MISC_ARB_LOCKOUT_CNTRL_INTRA_FR	1	/* Intra-frame */
#define	AR_D_MISC_ARB_LOCKOUT_CNTRL_GLOBAL	2	/* Global */
#define	AR_D_MISC_ARB_LOCKOUT_IGNORE	0x00080000 /* DCU arbiter lockout ignore control */
#define	AR_D_MISC_SEQ_NUM_INCR_DIS	0x00100000 /* Sequence number increment disable */
#define	AR_D_MISC_POST_FR_BKOFF_DIS	0x00200000 /* Post-frame backoff disable */
#define	AR_D_MISC_VIRT_COLL_POLICY	0x00400000 /* Virtual coll. handling policy */
#define	AR_D_MISC_BLOWN_IFS_POLICY	0x00800000 /* Blown IFS handling policy */
#define	AR_D_MISC_RESV0			0xFE000000 /* Reserved */

#define	AR_D_SEQNUM_RESV0	0xFFFFF000 /* Reserved */

#define	AR_D_GBL_IFS_MISC_LFSR_SLICE_SEL	0x00000007 /* LFSR slice select */
#define	AR_D_GBL_IFS_MISC_TURBO_MODE	0x00000008 /* Turbo mode indication */
#define	AR_D_GBL_IFS_MISC_SIFS_DURATION_USEC	0x000003F0 /* SIFS duration (us) */
#define	AR_D_GBL_IFS_MISC_USEC_DURATION	0x000FFC00 /* microsecond duration */
#define	AR_D_GBL_IFS_MISC_USEC_DURATION_S 10
#define	AR_D_GBL_IFS_MISC_DCU_ARBITER_DLY	0x00300000 /* DCU arbiter delay */
#define	AR_D_GBL_IFS_MISC_RESV0	0xFFC00000 /* Reserved */

/* DMA & PCI Registers in PCI space (usable during sleep) */
#define	AR_RC_MAC		0x00000001 /* MAC reset */
#define	AR_RC_BB		0x00000002 /* Baseband reset */
#define	AR_RC_RESV0		0x00000004 /* Reserved */
#define	AR_RC_RESV1		0x00000008 /* Reserved */
#define	AR_RC_PCI		0x00000010 /* PCI-core reset */

#define	AR_SCR_SLDUR		0x0000ffff /* sleep duration, units of 128us */
#define	AR_SCR_SLDUR_S		0
#define	AR_SCR_SLE		0x00030000 /* sleep enable */
#define	AR_SCR_SLE_S		16
#define	AR_SCR_SLE_WAKE		0 	/* force wake */
#define	AR_SCR_SLE_SLP		1	/* force sleep */
#define	AR_SCR_SLE_NORM		2	/* sleep logic normal operation */
#define	AR_SCR_SLDTP		0x00040000 /* sleep duration timing policy */
#define	AR_SCR_SLDWP		0x00080000 /* sleep duration write policy */
#define	AR_SCR_SLEPOL		0x00100000 /* sleep policy mode */
#define	AR_SCR_MIBIE		0x00200000 /* sleep perf cntrs MIB intr ena */
#define	AR_SCR_UNKNOWN		0x00400000

#define	AR_INTPEND_TRUE		0x00000001 /* interrupt pending */

#define	AR_SFR_SLEEP		0x00000001 /* force sleep */

#define	AR_PCICFG_SCLK_SEL	0x00000002 /* sleep clock select */
#define	AR_PCICFG_SCLK_SEL_S	1
#define	AR_PCICFG_CLKRUNEN	0x00000004 /* enable PCI CLKRUN function */
#define	AR_PCICFG_EEPROM_SIZE	0x00000018 /* Mask for EEPROM size */
#define	AR_PCICFG_EEPROM_SIZE_4		0	/* EEPROM size 4 Kbit */
#define	AR_PCICFG_EEPROM_SIZE_8K	1	/* EEPROM size 8 Kbit */
#define	AR_PCICFG_EEPROM_SIZE_16K	2	/* EEPROM size 16 Kbit */
#define	AR_PCICFG_EEPROM_SIZE_FAILED	3	/* Failure */
#define	AR_PCICFG_EEPROM_SIZE_S	3
#define	AR_PCICFG_LEDCTL	0x00000060 /* LED control Status */
#define	AR_PCICFG_LEDCTL_NONE	0	   /* STA is not associated or trying */
#define	AR_PCICFG_LEDCTL_PEND	1	   /* STA is trying to associate */
#define	AR_PCICFG_LEDCTL_ASSOC	2	   /* STA is associated */
#define	AR_PCICFG_LEDCTL_S	5
#define	AR_PCICFG_PCI_BUS_SEL	0x00000380 /* PCI observation bus mux select */
#define	AR_PCICFG_DIS_CBE_FIX	0x00000400 /* Disable fix for bad PCI CBE# generation */
#define	AR_PCICFG_SL_INTEN	0x00000800 /* enable interrupt line assertion when asleep */
#define	AR_PCICFG_RETRYFIXEN	0x00001000 /* Enable PCI core retry fix */
#define	AR_PCICFG_SL_INPEN	0x00002000 /* Force asleep when an interrupt is pending */
#define	AR_PCICFG_RESV1		0x0000C000 /* Reserved */
#define	AR_PCICFG_SPWR_DN	0x00010000 /* mask for sleep/awake indication */
#define	AR_PCICFG_LEDMODE	0x000E0000 /* LED mode */
#define	AR_PCICFG_LEDMODE_PROP	0	   /* Blink prop to filtered tx/rx */
#define	AR_PCICFG_LEDMODE_RPROP	1	   /* Blink prop to unfiltered tx/rx */
#define	AR_PCICFG_LEDMODE_SPLIT	2	   /* Blink power for tx/net for rx */
#define	AR_PCICFG_LEDMODE_RAND	3	   /* Blink randomly */
/* NB: s/w led control present in Hainan 1.1 and above */
#define	AR_PCICFG_LEDMODE_OFF	4	   /* s/w control + both led's off */
#define	AR_PCICFG_LEDMODE_POWON	5	   /* s/w control + power led on */
#define	AR_PCICFG_LEDMODE_NETON	6	   /* s/w control + network led on */
#define	AR_PCICFG_LEDMODE_S	17
#define	AR_PCICFG_LEDBLINK	0x00700000 /* LED blink threshold select */
#define	AR_PCICFG_LEDBLINK_S	20
#define	AR_PCICFG_LEDSLOW	0x00800000 /* LED slowest blink rate mode */
#define	AR_PCICFG_LEDSLOW_S	23
#define	AR_PCICFG_SCLK_RATE_IND 0x03000000 /* Sleep clock rate */
#define	AR_PCICFG_SCLK_RATE_IND_S 24
#define	AR_PCICFG_RESV2		0xFC000000 /* Reserved */

#define	AR_GPIOCR_CR_SHIFT	2          /* Each CR is 2 bits */
#define	AR_GPIOCR_CR_N(_g)	(0 << (AR_GPIOCR_CR_SHIFT * (_g)))
#define	AR_GPIOCR_CR_0(_g)	(1 << (AR_GPIOCR_CR_SHIFT * (_g)))
#define	AR_GPIOCR_CR_1(_g)	(2 << (AR_GPIOCR_CR_SHIFT * (_g)))
#define	AR_GPIOCR_CR_A(_g)	(3 << (AR_GPIOCR_CR_SHIFT * (_g)))
#define	AR_GPIOCR_INT_SHIFT	12         /* Interrupt select field shifter */
#define	AR_GPIOCR_INT(_g)	((_g) << AR_GPIOCR_INT_SHIFT)
#define	AR_GPIOCR_INT_MASK	0x00007000 /* Interrupt select field mask */
#define	AR_GPIOCR_INT_ENA	0x00008000 /* Enable GPIO Interrupt */
#define	AR_GPIOCR_INT_SELL	0x00000000 /* Generate int if pin is low */
#define	AR_GPIOCR_INT_SELH	0x00010000 /* Generate int if pin is high */
#define	AR_GPIOCR_INT_SEL	AR_GPIOCR_INT_SELH

#define	AR_SREV_ID		0x000000FF /* Mask to read SREV info */
#define	AR_SREV_ID_S		4	   /* Mask to shift Major Rev Info */
#define	AR_SREV_REVISION	0x0000000F /* Mask for Chip revision level */
#define	AR_SREV_REVISION_MIN	0	   /* lowest revision level */
#define	AR_SREV_REVISION_MAX	0xF	   /* highest revision level */
#define	AR_SREV_FPGA		1
#define	AR_SREV_D2PLUS		2
#define	AR_SREV_D2PLUS_MS	3	/* metal spin */
#define	AR_SREV_CRETE		4
#define	AR_SREV_CRETE_MS	5	/* FCS metal spin */
#define	AR_SREV_CRETE_MS23	7	/* 2.3 metal spin (6 skipped) */
#define	AR_SREV_CRETE_23	8	/* 2.3 full tape out */
#define	AR_SREV_GRIFFIN_LITE	8
#define	AR_SREV_HAINAN		9
#define	AR_SREV_CONDOR		11
#define	AR_SREV_VERSION	0x000000F0 /* Mask for Chip version */
#define	AR_SREV_VERSION_CRETE	0
#define	AR_SREV_VERSION_MAUI_1	1
#define	AR_SREV_VERSION_MAUI_2	2
#define	AR_SREV_VERSION_SPIRIT	3
#define	AR_SREV_VERSION_OAHU	4
#define	AR_SREV_VERSION_VENICE	5
#define	AR_SREV_VERSION_GRIFFIN	7
#define	AR_SREV_VERSION_CONDOR	9
#define	AR_SREV_VERSION_EAGLE	10
#define	AR_SREV_VERSION_COBRA	11	
#define	AR_SREV_2413		AR_SREV_VERSION_GRIFFIN
#define	AR_SREV_5413	        AR_SREV_VERSION_EAGLE
#define	AR_SREV_2415		AR_SREV_VERSION_COBRA
#define	AR_SREV_5424		AR_SREV_VERSION_CONDOR
#define	AR_SREV_2425		14	/* SWAN */
#define	AR_SREV_2417		15	/* Nala */
#define	AR_SREV_OAHU_ES		0	/* Engineering Sample */
#define	AR_SREV_OAHU_PROD	2	/* Production */

#define	AR_PHYREV_HAINAN	0x43
#define	AR_ANALOG5REV_HAINAN	0x46

#define	AR_RADIO_SREV_MAJOR	0xF0
#define	AR_RADIO_SREV_MINOR	0x0F
#define	AR_RAD5111_SREV_MAJOR	0x10	/* All current supported ar5211 5 GHz
					   radios are rev 0x10 */
#define	AR_RAD5111_SREV_PROD	0x15	/* Current production level radios */
#define	AR_RAD2111_SREV_MAJOR	0x20	/* All current supported ar5211 2 GHz
					   radios are rev 0x10 */
#define	AR_RAD5112_SREV_MAJOR	0x30	/* 5112 Major Rev */
#define AR_RAD5112_SREV_2_0     0x35    /* AR5112 Revision 2.0 */
#define AR_RAD5112_SREV_2_1     0x36    /* AR5112 Revision 2.1 */
#define	AR_RAD2112_SREV_MAJOR	0x40	/* 2112 Major Rev */
#define AR_RAD2112_SREV_2_0     0x45    /* AR2112 Revision 2.0 */
#define AR_RAD2112_SREV_2_1     0x46    /* AR2112 Revision 2.1 */
#define AR_RAD2413_SREV_MAJOR	0x50	/* 2413 Major Rev */
#define AR_RAD5413_SREV_MAJOR   0x60    /* 5413 Major Rev */
#define AR_RAD2316_SREV_MAJOR	0x70	/* 2316 Major Rev */
#define AR_RAD2317_SREV_MAJOR	0x80	/* 2317 Major Rev */
#define AR_RAD5424_SREV_MAJOR   0xa0    /* Mostly same as 5413 Major Rev */

#define	AR_PCIE_PMC_ENA_L1	0x01	/* enable PCIe core enter L1 when
					   d2_sleep_en is asserted */
#define	AR_PCIE_PMC_ENA_RESET	0x08	/* enable reset on link going down */

/* EEPROM Registers in the MAC */
#define	AR_EEPROM_CMD_READ	0x00000001
#define	AR_EEPROM_CMD_WRITE	0x00000002
#define	AR_EEPROM_CMD_RESET	0x00000004

#define	AR_EEPROM_STS_READ_ERROR	0x00000001
#define	AR_EEPROM_STS_READ_COMPLETE	0x00000002
#define	AR_EEPROM_STS_WRITE_ERROR	0x00000004
#define	AR_EEPROM_STS_WRITE_COMPLETE	0x00000008

#define	AR_EEPROM_CFG_SIZE	0x00000003	/* size determination override */
#define	AR_EEPROM_CFG_SIZE_AUTO		0
#define	AR_EEPROM_CFG_SIZE_4KBIT	1
#define	AR_EEPROM_CFG_SIZE_8KBIT	2
#define	AR_EEPROM_CFG_SIZE_16KBIT	3
#define	AR_EEPROM_CFG_DIS_WWRCL	0x00000004	/* Disable wait for write completion */
#define	AR_EEPROM_CFG_CLOCK	0x00000018	/* clock rate control */
#define	AR_EEPROM_CFG_CLOCK_S		3	/* clock rate control */
#define	AR_EEPROM_CFG_CLOCK_156KHZ	0
#define	AR_EEPROM_CFG_CLOCK_312KHZ	1
#define	AR_EEPROM_CFG_CLOCK_625KHZ	2
#define	AR_EEPROM_CFG_RESV0	0x000000E0	/* Reserved */
#define	AR_EEPROM_CFG_PKEY	0x00FFFF00	/* protection key */
#define	AR_EEPROM_CFG_PKEY_S	8
#define	AR_EEPROM_CFG_EN_L	0x01000000	/* EPRM_EN_L setting */

/* MAC PCU Registers */

#define	AR_STA_ID1_SADH_MASK	0x0000FFFF /* upper 16 bits of MAC addr */
#define	AR_STA_ID1_STA_AP	0x00010000 /* Device is AP */
#define	AR_STA_ID1_ADHOC	0x00020000 /* Device is ad-hoc */
#define	AR_STA_ID1_PWR_SAV	0x00040000 /* Power save reporting in
					      self-generated frames */
#define	AR_STA_ID1_KSRCHDIS	0x00080000 /* Key search disable */
#define	AR_STA_ID1_PCF		0x00100000 /* Observe PCF */
#define	AR_STA_ID1_USE_DEFANT	0x00200000 /* Use default antenna */
#define	AR_STA_ID1_UPD_DEFANT	0x00400000 /* Update default antenna w/
					      TX antenna */
#define	AR_STA_ID1_RTS_USE_DEF	0x00800000 /* Use default antenna to send RTS */
#define	AR_STA_ID1_ACKCTS_6MB	0x01000000 /* Use 6Mb/s rate for ACK & CTS */
#define	AR_STA_ID1_BASE_RATE_11B 0x02000000/* Use 11b base rate for ACK & CTS */
#define	AR_STA_ID1_USE_DA_SG	0x04000000 /* Use default antenna for
					      self-generated frames */
#define	AR_STA_ID1_CRPT_MIC_ENABLE	0x08000000 /* Enable Michael */
#define	AR_STA_ID1_KSRCH_MODE	0x10000000 /* Look-up key when keyID != 0 */
#define	AR_STA_ID1_PRE_SEQNUM	0x20000000 /* Preserve s/w sequence number */
#define	AR_STA_ID1_CBCIV_ENDIAN	0x40000000
#define	AR_STA_ID1_MCAST_KSRCH	0x80000000 /* Do keycache search for mcast */

#define	AR_BSS_ID1_U16		0x0000FFFF /* Upper 16 bits of BSSID */
#define	AR_BSS_ID1_AID		0xFFFF0000 /* Association ID */
#define	AR_BSS_ID1_AID_S	16

#define	AR_SLOT_TIME_MASK	0x000007FF /* Slot time mask */

#define	AR_TIME_OUT_ACK		0x00003FFF /* ACK time-out */
#define	AR_TIME_OUT_ACK_S	0
#define	AR_TIME_OUT_CTS		0x3FFF0000 /* CTS time-out */
#define	AR_TIME_OUT_CTS_S	16

#define	AR_RSSI_THR_MASK	0x000000FF /* Beacon RSSI warning threshold */
#define	AR_RSSI_THR_BM_THR	0x0000FF00 /* Missed beacon threshold */
#define	AR_RSSI_THR_BM_THR_S	8

#define	AR_USEC_USEC		0x0000007F /* clock cycles in 1 usec */
#define	AR_USEC_USEC_S		0
#define	AR_USEC_USEC32		0x00003F80 /* 32MHz clock cycles in 1 usec */
#define	AR_USEC_USEC32_S	7

#define AR5212_USEC_TX_LAT_M    0x007FC000      /* Tx latency */
#define AR5212_USEC_TX_LAT_S    14
#define AR5212_USEC_RX_LAT_M    0x1F800000      /* Rx latency */
#define AR5212_USEC_RX_LAT_S    23

#define	AR_BEACON_PERIOD	0x0000FFFF /* Beacon period mask in TU/msec */
#define	AR_BEACON_PERIOD_S	0
#define	AR_BEACON_TIM		0x007F0000 /* byte offset of TIM start */
#define	AR_BEACON_TIM_S		16
#define	AR_BEACON_EN		0x00800000 /* Beacon enable */
#define	AR_BEACON_RESET_TSF	0x01000000 /* Clear TSF to 0 */

#define	AR_RX_NONE		0x00000000 /* Disallow all frames */
#define	AR_RX_UCAST		0x00000001 /* Allow unicast frames */
#define	AR_RX_MCAST		0x00000002 /* Allow multicast frames */
#define	AR_RX_BCAST		0x00000004 /* Allow broadcast frames */
#define	AR_RX_CONTROL		0x00000008 /* Allow control frames */
#define	AR_RX_BEACON		0x00000010 /* Allow beacon frames */
#define	AR_RX_PROM		0x00000020 /* Promiscuous mode, all packets */
#define	AR_RX_PROBE_REQ		0x00000080 /* Allow probe request frames */

#define	AR_DIAG_CACHE_ACK	0x00000001 /* No ACK if no valid key found */
#define	AR_DIAG_ACK_DIS		0x00000002 /* Disable ACK generation */
#define	AR_DIAG_CTS_DIS		0x00000004 /* Disable CTS generation */
#define	AR_DIAG_ENCRYPT_DIS	0x00000008 /* Disable encryption */
#define	AR_DIAG_DECRYPT_DIS	0x00000010 /* Disable decryption */
#define	AR_DIAG_RX_DIS		0x00000020 /* Disable receive */
#define	AR_DIAG_CORR_FCS	0x00000080 /* Corrupt FCS */
#define	AR_DIAG_CHAN_INFO	0x00000100 /* Dump channel info */
#define	AR_DIAG_EN_SCRAMSD	0x00000200 /* Enable fixed scrambler seed */
#define	AR_DIAG_SCRAM_SEED	0x0001FC00 /* Fixed scrambler seed */
#define	AR_DIAG_SCRAM_SEED_S	10
#define	AR_DIAG_FRAME_NV0	0x00020000 /* Accept frames of non-zero
					      protocol version */
#define	AR_DIAG_OBS_PT_SEL	0x000C0000 /* Observation point select */
#define	AR_DIAG_OBS_PT_SEL_S	18
#define AR_DIAG_RX_CLR_HI	0x00100000 /* Force rx_clear high */
#define AR_DIAG_IGNORE_CS	0x00200000 /* Force virtual carrier sense */
#define AR_DIAG_CHAN_IDLE	0x00400000 /* Force channel idle high */
#define AR_DIAG_PHEAR_ME	0x00800000 /* Uses framed and wait_wep in the pherr_enable_eifs if set to 0 */

#define	AR_SLEEP1_NEXT_DTIM	0x0007ffff /* Abs. time(1/8TU) for next DTIM */
#define	AR_SLEEP1_NEXT_DTIM_S	0
#define	AR_SLEEP1_ASSUME_DTIM	0x00080000 /* Assume DTIM present on missent beacon */
#define	AR_SLEEP1_ENH_SLEEP_ENA	0x00100000 /* Enable enhanced sleep logic */
#define	AR_SLEEP1_CAB_TIMEOUT	0xff000000 /* CAB timeout(TU) */
#define	AR_SLEEP1_CAB_TIMEOUT_S	24

#define	AR_SLEEP2_NEXT_TIM	0x0007ffff /* Abs. time(1/8TU) for next DTIM */
#define	AR_SLEEP2_NEXT_TIM_S	0
#define	AR_SLEEP2_BEACON_TIMEOUT	0xff000000 /* Beacon timeout(TU) */
#define	AR_SLEEP2_BEACON_TIMEOUT_S	24

#define	AR_SLEEP3_TIM_PERIOD	0x0000ffff /* Tim/Beacon period (TU) */
#define	AR_SLEEP3_TIM_PERIOD_S	0
#define	AR_SLEEP3_DTIM_PERIOD	0xffff0000 /* DTIM period (TU) */
#define	AR_SLEEP3_DTIM_PERIOD_S	16

#define	AR_TPC_ACK		0x0000003f /* ack frames */
#define	AR_TPC_ACK_S		0
#define	AR_TPC_CTS		0x00003f00 /* cts frames */
#define	AR_TPC_CTS_S		8
#define	AR_TPC_CHIRP		0x003f0000 /* chirp frames */
#define	AR_TPC_CHIRP_S		16
#define AR_TPC_DOPPLER          0x0f000000 /* doppler chirp span */
#define AR_TPC_DOPPLER_S        24

#define	AR_PHY_ERR_RADAR	0x00000020	/* Radar signal */
#define	AR_PHY_ERR_OFDM_TIMING	0x00020000	/* False detect for OFDM */
#define	AR_PHY_ERR_CCK_TIMING	0x02000000	/* False detect for CCK */

#define	AR_TSF_PARM_INCREMENT	0x000000ff
#define	AR_TSF_PARM_INCREMENT_S	0

#define AR_NOACK_2BIT_VALUE    0x0000000f
#define AR_NOACK_2BIT_VALUE_S  0
#define AR_NOACK_BIT_OFFSET     0x00000070
#define AR_NOACK_BIT_OFFSET_S   4
#define AR_NOACK_BYTE_OFFSET    0x00000180
#define AR_NOACK_BYTE_OFFSET_S  7

#define	AR_MISC_MODE_BSSID_MATCH_FORCE  0x1	/* Force BSSID match */
#define	AR_MISC_MODE_ACKSIFS_MEMORY     0x2	/* ACKSIFS use contents of Rate */
#define	AR_MISC_MODE_MIC_NEW_LOC_ENABLE 0x4	/* Xmit Michael Key same as Rcv */
#define	AR_MISC_MODE_TX_ADD_TSF         0x8	/* Beacon/Probe-Rsp timestamp add (not replace) */

#define	AR_KEYTABLE_KEY0(_n)	(AR_KEYTABLE(_n) + 0)	/* key bit 0-31 */
#define	AR_KEYTABLE_KEY1(_n)	(AR_KEYTABLE(_n) + 4)	/* key bit 32-47 */
#define	AR_KEYTABLE_KEY2(_n)	(AR_KEYTABLE(_n) + 8)	/* key bit 48-79 */
#define	AR_KEYTABLE_KEY3(_n)	(AR_KEYTABLE(_n) + 12)	/* key bit 80-95 */
#define	AR_KEYTABLE_KEY4(_n)	(AR_KEYTABLE(_n) + 16)	/* key bit 96-127 */
#define	AR_KEYTABLE_TYPE(_n)	(AR_KEYTABLE(_n) + 20)	/* key type */
#define	AR_KEYTABLE_TYPE_40	0x00000000	/* WEP 40 bit key */
#define	AR_KEYTABLE_TYPE_104	0x00000001	/* WEP 104 bit key */
#define	AR_KEYTABLE_TYPE_128	0x00000003	/* WEP 128 bit key */
#define	AR_KEYTABLE_TYPE_TKIP	0x00000004	/* TKIP and Michael */
#define	AR_KEYTABLE_TYPE_AES	0x00000005	/* AES/OCB 128 bit key */
#define	AR_KEYTABLE_TYPE_CCM	0x00000006	/* AES/CCM 128 bit key */
#define	AR_KEYTABLE_TYPE_CLR	0x00000007	/* no encryption */
#define	AR_KEYTABLE_ANT		0x00000008	/* previous transmit antenna */
#define	AR_KEYTABLE_MAC0(_n)	(AR_KEYTABLE(_n) + 24)	/* MAC address 1-32 */
#define	AR_KEYTABLE_MAC1(_n)	(AR_KEYTABLE(_n) + 28)	/* MAC address 33-47 */
#define	AR_KEYTABLE_VALID	0x00008000	/* key and MAC address valid */

/* Compress settings */
#define AR_CCFG_WIN_M           0x00000007 /* mask for AR_CCFG_WIN size */
#define AR_CCFG_MIB_INT_EN      0x00000008 /* compression performance MIB counter int enable */
#define AR_CCUCFG_RESET_VAL     0x00100200 /* the should be reset value */
#define AR_CCUCFG_CATCHUP_EN    0x00000001 /* Compression catchup enable */
#define AR_DCM_D_EN             0x00000001 /* all direct frames to be decompressed */
#define AR_COMPRESSION_WINDOW_SIZE      4096 /* default comp. window size */

#endif /* _DEV_AR5212REG_H_ */
