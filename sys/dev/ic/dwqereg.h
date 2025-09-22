/*	$OpenBSD: dwqereg.h,v 1.10 2024/06/05 10:19:55 stsp Exp $	*/
/*
 * Copyright (c) 2008, 2019 Mark Kettenis <kettenis@openbsd.org>
 * Copyright (c) 2017, 2022 Patrick Wildt <patrick@blueri.se>
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

#define GMAC_MAC_CONF		0x0000
#define  GMAC_MAC_CONF_IPC		(1 << 27)
#define  GMAC_MAC_CONF_CST		(1 << 21)
#define  GMAC_MAC_CONF_ACS		(1 << 20)
#define  GMAC_MAC_CONF_BE		(1 << 18)
#define  GMAC_MAC_CONF_JD		(1 << 17)
#define  GMAC_MAC_CONF_JE		(1 << 16)
#define  GMAC_MAC_CONF_PS		(1 << 15)
#define  GMAC_MAC_CONF_FES		(1 << 14)
#define  GMAC_MAC_CONF_DM		(1 << 13)
#define  GMAC_MAC_CONF_DCRS		(1 << 9)
#define  GMAC_MAC_CONF_TE		(1 << 1)
#define  GMAC_MAC_CONF_RE		(1 << 0)
#define GMAC_MAC_PACKET_FILTER	0x0008
#define  GMAC_MAC_PACKET_FILTER_HPF	(1 << 10)
#define  GMAC_MAC_PACKET_FILTER_PCF_MASK (3 << 6)
#define  GMAC_MAC_PACKET_FILTER_PCF_ALL	(2 << 6)
#define  GMAC_MAC_PACKET_FILTER_DBF	(1 << 5)
#define  GMAC_MAC_PACKET_FILTER_PM	(1 << 4)
#define  GMAC_MAC_PACKET_FILTER_HMC	(1 << 2)
#define  GMAC_MAC_PACKET_FILTER_HUC	(1 << 1)
#define  GMAC_MAC_PACKET_FILTER_PR	(1 << 0)
#define GMAC_MAC_HASH_TAB_REG0	0x0010
#define GMAC_MAC_HASH_TAB_REG1	0x0014
#define GMAC_INT_MASK		0x003c
#define  GMAC_INT_MASK_LPIIM		(1 << 10)
#define  GMAC_INT_MASK_PIM		(1 << 3)
#define  GMAC_INT_MASK_RIM		(1 << 0)
#define GMAC_VLAN_TAG_CTRL	0x0050
#define  GMAC_VLAN_TAG_CTRL_EVLRXS		(1 << 24)
#define  GMAC_VLAN_TAG_CTRL_STRIP_ALWAYS	((1 << 21) | (1 << 22))	
#define GMAC_VLAN_TAG_DATA	0x0054
#define GMAC_VLAN_TAG_INCL	0x0060
#define  GMAC_VLAN_TAG_INCL_VLTI	(1 << 20)
#define  GMAC_VLAN_TAG_INCL_CSVL	(1 << 19)
#define  GMAC_VLAN_TAG_INCL_DELETE	0x10000
#define  GMAC_VLAN_TAG_INCL_INSERT	0x20000
#define  GMAC_VLAN_TAG_INCL_REPLACE	0x30000
#define  GMAC_VLAN_TAG_INCL_VLT		0x0ffff
#define  GMAC_VLAN_TAG_INCL_RDWR	(1U << 30)
#define  GMAC_VLAN_TAG_INCL_BUSY	(1U << 31)
#define GMAC_QX_TX_FLOW_CTRL(x)	(0x0070 + (x) * 4)
#define  GMAC_QX_TX_FLOW_CTRL_PT_SHIFT	16
#define  GMAC_QX_TX_FLOW_CTRL_TFE	(1 << 0)
#define GMAC_RX_FLOW_CTRL	0x0090
#define  GMAC_RX_FLOW_CTRL_RFE		(1 << 0)
#define GMAC_RXQ_CTRL0		0x00a0
#define  GMAC_RXQ_CTRL0_QUEUE_CLR(x)	(0x3 << ((x) * 2)
#define  GMAC_RXQ_CTRL0_AVB_QUEUE_EN(x)	(1 << ((x) * 2))
#define  GMAC_RXQ_CTRL0_DCB_QUEUE_EN(x)	(2 << ((x) * 2))
#define GMAC_RXQ_CTRL1		0x00a4
#define GMAC_RXQ_CTRL2		0x00a8
#define GMAC_RXQ_CTRL3		0x00ac
#define GMAC_INT_STATUS		0x00b0
#define GMAC_INT_EN		0x00b4
#define GMAC_MAC_1US_TIC_CTR	0x00dc
#define GMAC_VERSION		0x0110
#define  GMAC_VERSION_SNPS_MASK		0xff
#define GMAC_MAC_HW_FEATURE(x)	(0x011c + (x) * 0x4)
#define  GMAC_MAC_HW_FEATURE0_TXCOESEL	(1 << 14)
#define  GMAC_MAC_HW_FEATURE0_RXCOESEL	(1 << 16)
#define  GMAC_MAC_HW_FEATURE0_SAVLANINS	(1 << 27)
#define  GMAC_MAC_HW_FEATURE1_TXFIFOSIZE(x) (((x) >> 6) & 0x1f)
#define  GMAC_MAC_HW_FEATURE1_RXFIFOSIZE(x) (((x) >> 0) & 0x1f)
#define GMAC_MAC_MDIO_ADDR	0x0200
#define  GMAC_MAC_MDIO_ADDR_PA_SHIFT	21
#define  GMAC_MAC_MDIO_ADDR_RDA_SHIFT	16
#define  GMAC_MAC_MDIO_ADDR_CR_SHIFT	8
#define  GMAC_MAC_MDIO_ADDR_CR_60_100	0
#define  GMAC_MAC_MDIO_ADDR_CR_100_150	1
#define  GMAC_MAC_MDIO_ADDR_CR_20_35	2
#define  GMAC_MAC_MDIO_ADDR_CR_35_60	3
#define  GMAC_MAC_MDIO_ADDR_CR_150_250	4
#define  GMAC_MAC_MDIO_ADDR_CR_250_300	5
#define  GMAC_MAC_MDIO_ADDR_CR_300_500	6
#define  GMAC_MAC_MDIO_ADDR_CR_500_800	7
#define  GMAC_MAC_MDIO_ADDR_SKAP	(1 << 4)
#define  GMAC_MAC_MDIO_ADDR_GOC_READ	(3 << 2)
#define  GMAC_MAC_MDIO_ADDR_GOC_WRITE	(1 << 2)
#define  GMAC_MAC_MDIO_ADDR_C45E	(1 << 1)
#define  GMAC_MAC_MDIO_ADDR_GB		(1 << 0)
#define GMAC_MAC_MDIO_DATA	0x0204
#define GMAC_MAC_ADDR0_HI	0x0300
#define GMAC_MAC_ADDR0_LO	0x0304
#define GMAC_MMC_RX_INT_MASK	0x070c
#define GMAC_MMC_TX_INT_MASK	0x0710

#define GMAC_MTL_OPERATION_MODE	0x0c00
#define  GMAC_MTL_FRPE			(1 << 15)
#define  GMAC_MTL_OPERATION_SCHALG_MASK	(0x3 << 5)
#define  GMAC_MTL_OPERATION_SCHALG_WRR	(0x0 << 5)
#define  GMAC_MTL_OPERATION_SCHALG_WFQ	(0x1 << 5)
#define  GMAC_MTL_OPERATION_SCHALG_DWRR	(0x2 << 5)
#define  GMAC_MTL_OPERATION_SCHALG_SP	(0x3 << 5)
#define  GMAC_MTL_OPERATION_RAA_MASK	(0x1 << 2)
#define  GMAC_MTL_OPERATION_RAA_SP	(0x0 << 2)
#define  GMAC_MTL_OPERATION_RAA_WSP	(0x1 << 2)

#define GMAC_MTL_CHAN_BASE_ADDR(x)	(0x0d00 + (x) * 0x40)
#define GMAC_MTL_CHAN_TX_OP_MODE(x)	(GMAC_MTL_CHAN_BASE_ADDR(x) + 0x0)
#define  GMAC_MTL_CHAN_TX_OP_MODE_TQS_MASK	(0x1ffU << 16)
#define  GMAC_MTL_CHAN_TX_OP_MODE_TQS_SHIFT	16
#define  GMAC_MTL_CHAN_TX_OP_MODE_TTC_MASK	(0x7 << 4)
#define  GMAC_MTL_CHAN_TX_OP_MODE_TTC_SHIFT	4
#define  GMAC_MTL_CHAN_TX_OP_MODE_TTC_32	0
#define  GMAC_MTL_CHAN_TX_OP_MODE_TTC_64	(1 << 4)
#define  GMAC_MTL_CHAN_TX_OP_MODE_TTC_96	(2 << 4)
#define  GMAC_MTL_CHAN_TX_OP_MODE_TTC_128	(3 << 4)
#define  GMAC_MTL_CHAN_TX_OP_MODE_TTC_192	(4 << 4)
#define  GMAC_MTL_CHAN_TX_OP_MODE_TTC_256	(5 << 4)
#define  GMAC_MTL_CHAN_TX_OP_MODE_TTC_384	(6 << 4)
#define  GMAC_MTL_CHAN_TX_OP_MODE_TTC_512	(7 << 4)
#define  GMAC_MTL_CHAN_TX_OP_MODE_TXQEN_MASK	(0x3 << 2)
#define  GMAC_MTL_CHAN_TX_OP_MODE_TXQEN_AV	(1 << 2)
#define  GMAC_MTL_CHAN_TX_OP_MODE_TXQEN		(2 << 2)
#define  GMAC_MTL_CHAN_TX_OP_MODE_TSF		(1 << 1)
#define  GMAC_MTL_CHAN_TX_OP_MODE_FTQ		(1 << 0)
#define GMAC_MTL_CHAN_TX_DEBUG(x)	(GMAC_MTL_CHAN_BASE_ADDR(x) + 0x8)
#define GMAC_MTL_CHAN_INT_CTRL(x)	(GMAC_MTL_CHAN_BASE_ADDR(x) + 0x2c)
#define GMAC_MTL_CHAN_RX_OP_MODE(x)	(GMAC_MTL_CHAN_BASE_ADDR(x) + 0x30)
#define  GMAC_MTL_CHAN_RX_OP_MODE_RQS_MASK	(0x3ffU << 20)
#define  GMAC_MTL_CHAN_RX_OP_MODE_RQS_SHIFT	20
#define  GMAC_MTL_CHAN_RX_OP_MODE_RFD_MASK	(0x3fU << 14)
#define  GMAC_MTL_CHAN_RX_OP_MODE_RFD_SHIFT	14
#define  GMAC_MTL_CHAN_RX_OP_MODE_RFA_MASK	(0x3fU << 8)
#define  GMAC_MTL_CHAN_RX_OP_MODE_RFA_SHIFT	8
#define  GMAC_MTL_CHAN_RX_OP_MODE_EHFC		(1 << 7)
#define  GMAC_MTL_CHAN_RX_OP_MODE_RSF		(1 << 5)
#define  GMAC_MTL_CHAN_RX_OP_MODE_RTC_MASK	(0x3 << 3)
#define  GMAC_MTL_CHAN_RX_OP_MODE_RTC_SHIFT	3
#define  GMAC_MTL_CHAN_RX_OP_MODE_RTC_32	(1 << 3)
#define  GMAC_MTL_CHAN_RX_OP_MODE_RTC_64	(0 << 3)
#define  GMAC_MTL_CHAN_RX_OP_MODE_RTC_96	(2 << 3)
#define  GMAC_MTL_CHAN_RX_OP_MODE_RTC_128	(3 << 3)
#define GMAC_MTL_CHAN_RX_DEBUG(x)	(GMAC_MTL_CHAN_BASE_ADDR(x) + 0x38)

#define GMAC_BUS_MODE		0x1000
#define  GMAC_BUS_MODE_DCHE		(1 << 19)
#define  GMAC_BUS_MODE_SWR		(1 << 0)
#define GMAC_SYS_BUS_MODE	0x1004
#define  GMAC_SYS_BUS_MODE_EN_LPI		(1U << 31)
#define  GMAC_SYS_BUS_MODE_LPI_XIT_FRM		(1 << 30)
#define  GMAC_SYS_BUS_MODE_WR_OSR_LMT_MASK	(0xf << 24)
#define  GMAC_SYS_BUS_MODE_WR_OSR_LMT_SHIFT	24
#define  GMAC_SYS_BUS_MODE_RD_OSR_LMT_MASK	(0xf << 16)
#define  GMAC_SYS_BUS_MODE_RD_OSR_LMT_SHIFT	16
#define  GMAC_SYS_BUS_MODE_MB			(1 << 14)
#define  GMAC_SYS_BUS_MODE_AAL			(1 << 12)
#define  GMAC_SYS_BUS_MODE_EAME			(1 << 11)
#define  GMAC_SYS_BUS_MODE_BLEN_256		(1 << 7)
#define  GMAC_SYS_BUS_MODE_BLEN_128		(1 << 6)
#define  GMAC_SYS_BUS_MODE_BLEN_64		(1 << 5)
#define  GMAC_SYS_BUS_MODE_BLEN_32		(1 << 4)
#define  GMAC_SYS_BUS_MODE_BLEN_16		(1 << 3)
#define  GMAC_SYS_BUS_MODE_BLEN_8		(1 << 2)
#define  GMAC_SYS_BUS_MODE_BLEN_4		(1 << 1)
#define  GMAC_SYS_BUS_MODE_FB			(1 << 0)

#define GMAC_CHAN_BASE_ADDR(x)		(0x1100 + (x) * 0x80)
#define GMAC_CHAN_CONTROL(x)		(GMAC_CHAN_BASE_ADDR(x) + 0x0)
#define  GMAC_CHAN_CONTROL_8XPBL		(1 << 16)
#define GMAC_CHAN_TX_CONTROL(x)		(GMAC_CHAN_BASE_ADDR(x) + 0x4)
#define  GMAC_CHAN_TX_CONTROL_PBL_MASK		(0x3f << 16)
#define  GMAC_CHAN_TX_CONTROL_PBL_SHIFT		16
#define  GMAC_CHAN_TX_CONTROL_OSP		(1 << 4)
#define  GMAC_CHAN_TX_CONTROL_ST		(1 << 0)
#define GMAC_CHAN_RX_CONTROL(x)		(GMAC_CHAN_BASE_ADDR(x) + 0x8)
#define  GMAC_CHAN_RX_CONTROL_RPBL_MASK		(0x3f << 16)
#define  GMAC_CHAN_RX_CONTROL_RPBL_SHIFT	16
#define  GMAC_CHAN_RX_CONTROL_SR		(1 << 0)
#define GMAC_CHAN_TX_BASE_ADDR_HI(x)	(GMAC_CHAN_BASE_ADDR(x) + 0x10)
#define GMAC_CHAN_TX_BASE_ADDR(x)	(GMAC_CHAN_BASE_ADDR(x) + 0x14)
#define GMAC_CHAN_RX_BASE_ADDR_HI(x)	(GMAC_CHAN_BASE_ADDR(x) + 0x18)
#define GMAC_CHAN_RX_BASE_ADDR(x)	(GMAC_CHAN_BASE_ADDR(x) + 0x1c)
#define GMAC_CHAN_TX_END_ADDR(x)	(GMAC_CHAN_BASE_ADDR(x) + 0x20)
#define GMAC_CHAN_RX_END_ADDR(x)	(GMAC_CHAN_BASE_ADDR(x) + 0x28)
#define GMAC_CHAN_TX_RING_LEN(x)	(GMAC_CHAN_BASE_ADDR(x) + 0x2c)
#define GMAC_CHAN_RX_RING_LEN(x)	(GMAC_CHAN_BASE_ADDR(x) + 0x30)
#define GMAC_CHAN_INTR_ENA(x)		(GMAC_CHAN_BASE_ADDR(x) + 0x34)
#define  GMAC_CHAN_INTR_ENA_NIE			(1 << 15)
#define  GMAC_CHAN_INTR_ENA_AIE			(1 << 14)
#define  GMAC_CHAN_INTR_ENA_CDE			(1 << 13)
#define  GMAC_CHAN_INTR_ENA_FBE			(1 << 12)
#define  GMAC_CHAN_INTR_ENA_ERE			(1 << 11)
#define  GMAC_CHAN_INTR_ENA_ETE			(1 << 10)
#define  GMAC_CHAN_INTR_ENA_RWE			(1 << 9)
#define  GMAC_CHAN_INTR_ENA_RSE			(1 << 8)
#define  GMAC_CHAN_INTR_ENA_RBUE		(1 << 7)
#define  GMAC_CHAN_INTR_ENA_RIE			(1 << 6)
#define  GMAC_CHAN_INTR_ENA_TBUE		(1 << 2)
#define  GMAC_CHAN_INTR_ENA_TSE			(1 << 1)
#define  GMAC_CHAN_INTR_ENA_TIE			(1 << 0)
#define GMAC_CHAN_RX_WATCHDOG(x)	(GMAC_CHAN_CONTROL(x) + 0x38)
#define GMAC_CHAN_SLOT_CTRL_STATUS(x)	(GMAC_CHAN_CONTROL(x) + 0x3c)
#define GMAC_CHAN_CUR_TX_DESC(x)	(GMAC_CHAN_CONTROL(x) + 0x44)
#define GMAC_CHAN_CUR_RX_DESC(x)	(GMAC_CHAN_CONTROL(x) + 0x4c)
#define GMAC_CHAN_CUR_TX_BUF_ADDR(x)	(GMAC_CHAN_CONTROL(x) + 0x54)
#define GMAC_CHAN_CUR_RX_BUF_ADDR(x)	(GMAC_CHAN_CONTROL(x) + 0x5c)
#define GMAC_CHAN_STATUS(x)		(GMAC_CHAN_CONTROL(x) + 0x60)
#define  GMAC_CHAN_STATUS_REB_MASK		0x7
#define  GMAC_CHAN_STATUS_REB_SHIFT		19
#define  GMAC_CHAN_STATUS_TEB_MASK		0x7
#define  GMAC_CHAN_STATUS_TEB_SHIFT		16
#define  GMAC_CHAN_STATUS_NIS			(1 << 15)
#define  GMAC_CHAN_STATUS_AIS			(1 << 14)
#define  GMAC_CHAN_STATUS_CDE			(1 << 13)
#define  GMAC_CHAN_STATUS_FBE			(1 << 12)
#define  GMAC_CHAN_STATUS_ERI			(1 << 11)
#define  GMAC_CHAN_STATUS_ETI			(1 << 10)
#define  GMAC_CHAN_STATUS_RWT			(1 << 9)
#define  GMAC_CHAN_STATUS_RPS			(1 << 8)
#define  GMAC_CHAN_STATUS_RBU			(1 << 7)
#define  GMAC_CHAN_STATUS_RI			(1 << 6)
#define  GMAC_CHAN_STATUS_TBU			(1 << 2)
#define  GMAC_CHAN_STATUS_TPS			(1 << 1)
#define  GMAC_CHAN_STATUS_TI			(1 << 0)

/*
 * DWQE descriptors.
 */

struct dwqe_desc {
	uint32_t sd_tdes0;
	uint32_t sd_tdes1;
	uint32_t sd_tdes2;
	uint32_t sd_tdes3;
};

/* Tx context descriptor bits (host to device); precedes regular descriptor */
#define TDES3_CTXT		(1 << 30)
#define TDES3_VLAN_TAG_VALID	(1 << 16)
#define TDES3_VLAN_TAG		0xffff	
/* Bit 31 is the OWN bit, as in regular Tx descriptor. */

/* Tx bits (read format; host to device) */
#define TDES2_HDR_LEN		0x000003ff	/* if TSO is enabled */
#define TDES2_BUF1_LEN		0x00003fff	/* if TSO is disabled */
#define TDES2_VLAN_TIR		0x0000c000
#define   TDES2_NO_VLAN_TAGGING		(0x0 << 14)
#define   TDES2_VLAN_TAG_STRIP		(0x1 << 14)
#define   TDES2_VLAN_TAG_INSERT		(0x2 << 14)
#define   TDES2_VLAN_TAG_REPLACE	(0x3 << 14)
#define TDES2_BUF2_LEN		0x3fff0000
#define TDES2_TX_TIMESTAMP_EN	(1 << 30)	/* if TSO is disabled */
#define TDES2_TSO_EXTMEM_DIS	(1 << 30)	/* if TSO is enabled */
#define TDES2_IC		(1U << 31)
#define TDES3_TCP_PAYLOAD_LEN	0x0003ffff	/* if TSO is enabled */
#define TDES3_FRAME_LEN		0x00007fff	/* if TSO is disabled */
#define TDES3_CIC		0x00030000	/* if TSO is disabled */
#define   TDES3_CSUM_DISABLE			(0x0 << 16)
#define   TDES3_CSUM_IPHDR			(0x1 << 16)
#define   TDES3_CSUM_IPHDR_PAYLOAD		(0x2 << 16)
#define   TDES3_CSUM_IPHDR_PAYLOAD_PSEUDOHDR	(0x3 << 16)
#define TDES3_TSO_EN		(1 << 18)
#define TDES3_CPC		((1 << 26) | (1 << 27)) /* if TSO is disabled */
#define   TDES3_CPC_CRC_AND_PAD		(0x0 << 26)
#define   TDES3_CPC_CRC_NO_PAD		(0x1 << 26)
#define   TDES3_CPC_DISABLE		(0x2 << 26)
#define   TDES3_CPC_CRC_REPLACE		(0x3 << 26)
#define TDES3_LS		(1 << 28)
#define TDES3_FS		(1 << 29)
#define TDES3_OWN		(1U << 31)

/* Tx bits (writeback format; device to host) */
#define TDES3_ES		(1 << 15)
#define TDES3_DE		(1 << 23)
/* Bit 28 is the LS bit, as in "read" format. */
/* Bit 29 is the FS bit, as in "read" format. */
/* Bit 31 is the OWN bit, as in "read" format. */

/* Rx bits (read format; host to device) */
#define RDES3_BUF1V		(1 << 24)
#define RDES3_BUF2V		(1 << 25)
#define RDES3_IC		(1 << 30)
#define RDES3_OWN		(1U << 31)

/* Rx bits (writeback format; device to host) */
#define RDES0_IVT		0xffff0000
#define RDES0_OVT		0x0000ffff
#define RDES1_IP_PAYLOAD_TYPE	0x7
#define   RDES1_IP_PAYLOAD_UNKNOWN	0x0
#define   RDES1_IP_PAYLOAD_UDP		0x1
#define   RDES1_IP_PAYLOAD_TCP		0x2
#define   RDES1_IP_PAYLOAD_ICMP		0x3
#define RDES1_IP_HDR_ERROR	(1 << 3)
#define RDES1_IPV4_HDR		(1 << 4)
#define RDES1_IPV6_HDR		(1 << 5)
#define RDES1_IP_CSUM_BYPASS	(1 << 6)
#define RDES1_IP_PAYLOAD_ERROR	(1 << 7)
#define RDES3_LENGTH		(0x7fff << 0)
#define RDES3_ES		(1 << 15)
#define RDES3_LENTYPE		0x70000
#define   RDES3_LENTYPE_LENGTH	(0x0 << 16)
#define   RDES3_LENTYPE_TYPE	(0x1 << 16)
				/* 0x2 is reserved */
#define   RDES3_LENTYPE_ARP	(0x3 << 16)
#define   RDES3_LENTYPE_VLAN	(0x4 << 16)
#define   RDES3_LENTYPE_2VLAN	(0x5 << 16)
#define   RDES3_LENTYPE_MACCTL	(0x6 << 16)
#define   RDES3_LENTYPE_OAM	(0x7 << 16)
#define RDES3_DE		(1 << 19)
#define RDES3_RE		(1 << 20)
#define RDES3_OE		(1 << 21)
#define RDES3_RWT		(1 << 22)
#define RDES3_GP		(1 << 23)
#define RDES3_CE		(1 << 24)
#define RDES3_RDES0_VALID	(1 << 25)
#define RDES3_RDES1_VALID	(1 << 26)
#define RDES3_RDES2_VALID	(1 << 27)
#define RDES3_LD		(1 << 28)
#define RDES3_FD		(1 << 29)
#define RDES3_CTXT		(1 << 30)
/* Bit 31 is the OWN bit, as in "read" format. */
