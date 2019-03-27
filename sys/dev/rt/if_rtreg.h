/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009, Aleksandr Rybalko
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

#ifndef _IF_RTREG_H_
#define	_IF_RTREG_H_

#define	RT_READ(sc, reg)				\
	bus_space_read_4((sc)->bst, (sc)->bsh, reg)

#define	RT_WRITE(sc, reg, val)				\
	bus_space_write_4((sc)->bst, (sc)->bsh, reg, val)

#define	GE_PORT_BASE 0x0000

#define	MDIO_ACCESS	0x00
#define	    MDIO_CMD_ONGO	(1<<31)
#define	    MDIO_CMD_WR		(1<<30)
#define	    MDIO_PHY_ADDR_MASK	0x1f000000
#define	    MDIO_PHY_ADDR_SHIFT	24
#define	    MDIO_PHYREG_ADDR_MASK 0x001f0000
#define	    MDIO_PHYREG_ADDR_SHIFT 16
#define	    MDIO_PHY_DATA_MASK	0x0000ffff
#define	    MDIO_PHY_DATA_SHIFT	0

#define	MDIO_CFG	0x04
#define	    MDIO_2880_100T_INIT	0x1001BC01
#define	    MDIO_2880_GIGA_INIT	0x1F01DC01

#define	FE_GLO_CFG	0x08 /*Frame Engine Global Configuration */
#define	    EXT_VLAN_TYPE_MASK	0xffff0000
#define	    EXT_VLAN_TYPE_SHIFT	16
#define	    EXT_VLAN_TYPE_DFLT	0x81000000
#define	    US_CYC_CNT_MASK	0x0000ff00
#define	    US_CYC_CNT_SHIFT	8
#define	    US_CYC_CNT_DFLT	(132<<8) /* sys clocks per 1uS */
#define	    L2_SPACE		(8<<4) /* L2 space. Unit is 8 bytes */

#define	FE_RST_GLO	0x0C /*Frame Engine Global Reset*/
#define	    FC_DROP_CNT_MASK	0xffff0000 /*Flow cntrl drop count */
#define	    FC_DROP_CNT_SHIFT	16
#define	    PSE_RESET		(1<<0)

/* RT305x interrupt registers */
#define	FE_INT_STATUS	0x10
#define	    CNT_PPE_AF		(1<<31)
#define	    CNT_GDM_AF		(1<<29)
#define	    PSE_P2_FC		(1<<26)
#define	    GDM_CRC_DROP	(1<<25)
#define	    PSE_BUF_DROP	(1<<24)
#define	    GDM_OTHER_DROP	(1<<23)
#define	    PSE_P1_FC		(1<<22)
#define	    PSE_P0_FC		(1<<21)
#define	    PSE_FQ_EMPTY	(1<<20)
#define	    INT_TX_COHERENT	(1<<17)
#define	    INT_RX_COHERENT	(1<<16)
#define	    INT_TXQ3_DONE	(1<<11)
#define	    INT_TXQ2_DONE	(1<<10)
#define	    INT_TXQ1_DONE	(1<<9)
#define	    INT_TXQ0_DONE	(1<<8)
#define	    INT_RX_DONE		(1<<2)
#define	    TX_DLY_INT		(1<<1) /* TXQ[0|1]_DONE with delay */
#define	    RX_DLY_INT		(1<<0) /* RX_DONE with delay */
#define	FE_INT_ENABLE	0x14

/* RT5350 interrupt registers */
#define RT5350_FE_INT_STATUS    (RT5350_PDMA_BASE + 0x220)
#define            RT5350_INT_RX_COHERENT      (1<<31)
#define            RT5350_RX_DLY_INT           (1<<30)
#define            RT5350_INT_TX_COHERENT      (1<<29)
#define            RT5350_TX_DLY_INT           (1<<28)
#define            RT5350_INT_RXQ1_DONE	       (1<<17)
#define            RT5350_INT_RXQ0_DONE        (1<<16)
#define            RT5350_INT_TXQ3_DONE        (1<<3)
#define            RT5350_INT_TXQ2_DONE        (1<<2)
#define            RT5350_INT_TXQ1_DONE        (1<<1)
#define            RT5350_INT_TXQ0_DONE        (1<<0)
#define RT5350_FE_INT_ENABLE    (RT5350_PDMA_BASE + 0x228)

#define	MDIO_CFG2	0x18
#define	FOE_TS_T	0x1c
#define	    PSE_FQ_PCNT_MASK	0xff000000
#define	    PSE_FQ_PCNT_SHIFT	24
#define	    FOE_TS_TIMESTAMP_MASK 0x0000ffff
#define	    FOE_TS_TIMESTAMP_SHIFT 0

#define	GDMA1_BASE		0x0020
#define	GDMA2_BASE		0x0060
#define	CDMA_BASE		0x0080
#define	MT7620_GDMA1_BASE	0x600

#define	GDMA_FWD_CFG	0x00	/* Only GDMA */
#define	    GDM_DROP_256B	(1<<23)
#define	    GDM_ICS_EN		(1<<22)
#define	    GDM_TCS_EN		(1<<21)
#define	    GDM_UCS_EN		(1<<20)
#define	    GDM_DISPAD		(1<<18)
#define	    GDM_DISCRC		(1<<17)
#define	    GDM_STRPCRC		(1<<16)
#define	    GDM_UFRC_P_SHIFT	12
#define	    GDM_BFRC_P_SHIFT	8
#define	    GDM_MFRC_P_SHIFT	4
#define	    GDM_OFRC_P_SHIFT	0
#define	    GDM_XFRC_P_MASK	0x07
#define	    GDM_DST_PORT_CPU	0
#define	    GDM_DST_PORT_GDMA1	1
#define	    GDM_DST_PORT_GDMA2	2
#define	    GDM_DST_PORT_PPE	6
#define	    GDM_DST_PORT_DISCARD 7

#define	CDMA_CSG_CFG	0x00	/* Only CDMA */
#define	    INS_VLAN_TAG	(0x8100<<16)
#define	    ICS_GEN_EN		(1<<2)
#define	    TCS_GEN_EN		(1<<1)
#define	    UCS_GEN_EN		(1<<0)

#define	GDMA_SCH_CFG	0x04
#define	    GDM1_SCH_MOD_MASK	0x03000000
#define	    GDM1_SCH_MOD_SHIFT	24
#define	    GDM1_SCH_MOD_WRR	0
#define	    GDM1_SCH_MOD_STRICT	1
#define	    GDM1_SCH_MOD_MIXED	2
#define	    GDM1_WT_1		0
#define	    GDM1_WT_2		1
#define	    GDM1_WT_4		2
#define	    GDM1_WT_8		3
#define	    GDM1_WT_16		4
#define	    GDM1_WT_Q3_SHIFT	12
#define	    GDM1_WT_Q2_SHIFT	8
#define	    GDM1_WT_Q1_SHIFT	4
#define	    GDM1_WT_Q0_SHIFT	0

#define	GDMA_SHPR_CFG	0x08
#define	    GDM1_SHPR_EN	(1<<24)
#define	    GDM1_BK_SIZE_MASK	0x00ff0000 /* Bucket size 1kB units */
#define	    GDM1_BK_SIZE_SHIFT	16
#define	    GDM1_TK_RATE_MASK	0x00003fff /* Shaper token rate 8B/ms units */
#define	    GDM1_TK_RATE_SHIFT	0

#define	GDMA_MAC_ADRL	 0x0C
#define	GDMA_MAC_ADRH	 0x10

#define	PPPOE_SID_0001		0x08 /* 0..15 SID0, 15..31 SID1 */
#define	PPPOE_SID_0203		0x0c
#define	PPPOE_SID_0405		0x10
#define	PPPOE_SID_0607		0x14
#define	PPPOE_SID_0809		0x18
#define	PPPOE_SID_1011		0x1c
#define	PPPOE_SID_1213		0x20
#define	PPPOE_SID_1415		0x24
#define	VLAN_ID_0001		0x28 /* 0..11 VID0, 15..26 VID1 */
#define	VLAN_ID_0203		0x2c
#define	VLAN_ID_0405		0x30
#define	VLAN_ID_0607		0x34
#define	VLAN_ID_0809		0x38
#define	VLAN_ID_1011		0x3c
#define	VLAN_ID_1213		0x40
#define	VLAN_ID_1415		0x44

#define	PSE_BASE	    0x0040
#define	PSE_FQFC_CFG        0x00
#define	    FQ_MAX_PCNT_MASK	0xff000000
#define	    FQ_MAX_PCNT_SHIFT	24
#define	    FQ_FC_RLS_MASK	0x00ff0000
#define	    FQ_FC_RLS_SHIFT	16
#define	    FQ_FC_ASRT_MASK	0x0000ff00
#define	    FQ_FC_ASRT_SHIFT	8
#define	    FQ_FC_DROP_MASK	0x000000ff
#define	    FQ_FC_DROP_SHIFT	0

#define	CDMA_FC_CFG         0x04
#define	GDMA1_FC_CFG        0x08
#define	GDMA2_FC_CFG        0x0C
#define	    P_SHARING		(1<<28)
#define	    P_HQ_DEF_MASK	0x0f000000
#define	    P_HQ_DEF_SHIFT	24
#define	    P_HQ_RESV_MASK	0x00ff0000
#define	    P_HQ_RESV_SHIFT	16
#define	    P_LQ_RESV_MASK	0x0000ff00
#define	    P_LQ_RESV_SHIFT	8
#define	    P_IQ_ASRT_MASK	0x000000ff
#define	    P_IQ_ASRT_SHIFT	0

#define	CDMA_OQ_STA         0x10
#define	GDMA1_OQ_STA        0x14
#define	GDMA2_OQ_STA        0x18
#define	    P_OQ3_PCNT_MASK	0xff000000
#define	    P_OQ3_PCNT_SHIFT	24
#define	    P_OQ2_PCNT_MASK	0x00ff0000
#define	    P_OQ2_PCNT_SHIFT	16
#define	    P_OQ1_PCNT_MASK	0x0000ff00
#define	    P_OQ1_PCNT_SHIFT	8
#define	    P_OQ0_PCNT_MASK	0x000000ff
#define	    P_OQ0_PCNT_SHIFT	0

#define	PSE_IQ_STA          0x1C
#define	    P6_OQ0_PCNT_MASK	0xff000000
#define	    P6_OQ0_PCNT_SHIFT	24
#define	    P2_IQ_PCNT_MASK	0x00ff0000
#define	    P2_IQ_PCNT_SHIFT	16
#define	    P1_IQ_PCNT_MASK	0x0000ff00
#define	    P1_IQ_PCNT_SHIFT	8
#define	    P0_IQ_PCNT_MASK	0x000000ff
#define	    P0_IQ_PCNT_SHIFT	0

#define	PDMA_BASE 0x0100
#define RT5350_PDMA_BASE 0x0800
#define	PDMA_GLO_CFG	    0x00
#define RT5350_PDMA_GLO_CFG 0x204
#define	    FE_TX_WB_DDONE	(1<<6)
#define	    FE_DMA_BT_SIZE4	(0<<4)
#define	    FE_DMA_BT_SIZE8	(1<<4)
#define	    FE_DMA_BT_SIZE16	(2<<4)
#define	    FE_RX_DMA_BUSY	(1<<3)
#define	    FE_RX_DMA_EN	(1<<2)
#define	    FE_TX_DMA_BUSY	(1<<1)
#define	    FE_TX_DMA_EN	(1<<0)
#define	PDMA_RST_IDX        0x04
#define RT5350_PDMA_RST_IDX 0x208
#define	    FE_RST_DRX_IDX0	(1<<16)
#define	    FE_RST_DTX_IDX3	(1<<3)
#define	    FE_RST_DTX_IDX2	(1<<2)
#define	    FE_RST_DTX_IDX1	(1<<1)
#define	    FE_RST_DTX_IDX0	(1<<0)

#define	PDMA_SCH_CFG        0x08
#define RT5350_PDMA_SCH_CFG 0x280
#define	DELAY_INT_CFG       0x0C
#define RT5350_DELAY_INT_CFG 0x20C
#define	    TXDLY_INT_EN 	(1<<31)
#define	    TXMAX_PINT_SHIFT	24
#define	    TXMAX_PTIME_SHIFT	16
#define	    RXDLY_INT_EN	(1<<15)
#define	    RXMAX_PINT_SHIFT	8
#define	    RXMAX_PTIME_SHIFT	0

#define	TX_BASE_PTR0        0x10
#define	TX_MAX_CNT0         0x14
#define	TX_CTX_IDX0         0x18
#define	TX_DTX_IDX0         0x1C

#define	TX_BASE_PTR1        0x20
#define	TX_MAX_CNT1         0x24
#define	TX_CTX_IDX1         0x28
#define	TX_DTX_IDX1         0x2C

#define	RX_BASE_PTR0        0x30
#define	RX_MAX_CNT0         0x34
#define	RX_CALC_IDX0        0x38
#define	RX_DRX_IDX0         0x3C

#define	TX_BASE_PTR2        0x40
#define	TX_MAX_CNT2         0x44
#define	TX_CTX_IDX2         0x48
#define	TX_DTX_IDX2         0x4C

#define	TX_BASE_PTR3        0x50
#define	TX_MAX_CNT3         0x54
#define	TX_CTX_IDX3         0x58
#define	TX_DTX_IDX3         0x5C

#define	TX_BASE_PTR(qid)		(((qid>1)?(0x20):(0x10)) + (qid) * 16)
#define	TX_MAX_CNT(qid)			(((qid>1)?(0x24):(0x14)) + (qid) * 16)
#define	TX_CTX_IDX(qid)			(((qid>1)?(0x28):(0x18)) + (qid) * 16)
#define	TX_DTX_IDX(qid)			(((qid>1)?(0x2c):(0x1c)) + (qid) * 16)

#define RT5350_TX_BASE_PTR0        0x000
#define RT5350_TX_MAX_CNT0         0x004
#define RT5350_TX_CTX_IDX0         0x008
#define RT5350_TX_DTX_IDX0         0x00C

#define RT5350_TX_BASE_PTR1        0x010
#define RT5350_TX_MAX_CNT1         0x014
#define RT5350_TX_CTX_IDX1         0x018
#define RT5350_TX_DTX_IDX1         0x01C

#define        RT5350_TX_BASE_PTR2        0x020
#define        RT5350_TX_MAX_CNT2         0x024
#define        RT5350_TX_CTX_IDX2         0x028
#define        RT5350_TX_DTX_IDX2         0x02C

#define        RT5350_TX_BASE_PTR3        0x030
#define        RT5350_TX_MAX_CNT3         0x034
#define        RT5350_TX_CTX_IDX3         0x038
#define        RT5350_TX_DTX_IDX3         0x03C

#define        RT5350_RX_BASE_PTR0        0x100
#define        RT5350_RX_MAX_CNT0         0x104
#define        RT5350_RX_CALC_IDX0        0x108
#define        RT5350_RX_DRX_IDX0         0x10C

#define        RT5350_RX_BASE_PTR1        0x110
#define        RT5350_RX_MAX_CNT1         0x114
#define        RT5350_RX_CALC_IDX1        0x118
#define        RT5350_RX_DRX_IDX1         0x11C

#define        RT5350_TX_BASE_PTR(qid)         ((qid) * 0x10 + 0x000)
#define        RT5350_TX_MAX_CNT(qid)          ((qid) * 0x10 + 0x004)
#define        RT5350_TX_CTX_IDX(qid)          ((qid) * 0x10 + 0x008)
#define        RT5350_TX_DTX_IDX(qid)          ((qid) * 0x10 + 0x00C)

#define	PPE_BASE 0x0200

#define	CNTR_BASE 0x0400
#define	PPE_AC_BCNT0		0x000
#define	PPE_AC_PCNT0		0x004
#define	PPE_AC_BCNT63		0x1F8
#define	PPE_AC_PCNT63		0x1FC
#define	PPE_MTR_CNT0		0x200
#define	PPE_MTR_CNT63		0x2FC
#define	GDMA_TX_GBCNT0		0x300
#define	GDMA_TX_GPCNT0		0x304
#define	GDMA_TX_SKIPCNT0	0x308
#define	GDMA_TX_COLCNT0		0x30C
#define	GDMA_RX_GBCNT0		0x320
#define	GDMA_RX_GPCNT0		0x324
#define	GDMA_RX_OERCNT0		0x328
#define	GDMA_RX_FERCNT0		0x32C
#define	GDMA_RX_SHORT_ERCNT0	0x330
#define	GDMA_RX_LONG_ERCNT0	0x334
#define	GDMA_RX_CSUM_ERCNT0	0x338

#define	POLICYTABLE_BASE 	0x1000

#endif /* _IF_RTREG_H_ */
