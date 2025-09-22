/*
 * Copyright (c) 2022 Claudio Jeker <claudio@openbsd.org>
 * Copyright (C) 2021 MediaTek Inc.
 * Copyright (C) 2016 Felix Fietkau <nbd@nbd.name>
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
 */

/* MCU WFDMA1 */
#define	MT_MCU_WFDMA1_BASE		0x3000

#define	MT_MDP_BASE			0x820cd000
#define	MT_MDP_DCR0			0x820cd000
#define	MT_MDP_DCR0_DAMSDU_EN		(1U << 15)
#define	MT_MDP_DCR0_RX_HDR_TRANS_EN	(1U << 19)

#define	MT_MDP_DCR1			0x820cd004
#define	MT_MDP_DCR1_MAX_RX_LEN_MASK	0x0000fff8
#define	MT_MDP_DCR1_MAX_RX_LEN_SHIFT	3

/* TMAC: band 0 (0x21000), band 1 (0xa1000) */
#define	MT_BAND_BASE0		0x820f0000
#define	MT_BAND_BASE1		0x820e0000
#define	MT_BAND_OFF		(MT_BAND_BASE0 - MT_BAND_BASE1)
#define	MT_BAND_ADDR(_band, ofs)		\
	(MT_BAND_BASE0 - (_band) * MT_BAND_OFF + (ofs))

#define	MT_TMAC_TCR0(_band)		MT_BAND_ADDR(_band, 0x4000)
#define	MT_TMAC_TCR0_TBTT_STOP_CTRL	(1U << 25)

#define	MT_TMAC_CDTR(_band)		MT_BAND_ADDR(_band, 0x4090)
#define	MT_TMAC_ODTR(_band)		MT_BAND_ADDR(_band, 0x4094)
#define	MT_TIMEOUT_VAL_PLCP		0x0000ffff
#define	MT_TIMEOUT_VAL_CCA		0xffff0000
#define	MT_TIMEOUT_CCK_DEF_VAL		(231 | (41 << 16))
#define	MT_TIMEOUT_OFDM_DEF_VAL		(60 | (28 << 16))

#define	MT_TMAC_ICR0(_band)		MT_BAND_ADDR(_band, 0x40a4)
#define	MT_IFS_EIFS_MASK		0x0000001f
#define	MT_IFS_EIFS_DEF			360
#define	MT_IFS_RIFS_MASK		0x00007c00
#define	MT_IFS_RIFS_DEF			(2 << 10)
#define	MT_IFS_SIFS_MASK		0x007f0000
#define	MT_IFS_SIFS_SHIFT		16
#define	MT_IFS_SLOT_MASK		0x7f000000
#define	MT_IFS_SLOT_SHIFT		24

#define	MT_TMAC_CTCR0(_band)			MT_BAND_ADDR(_band, 0x40f4)
#define	MT_TMAC_CTCR0_INS_DDLMT_REFTIME		0x0000003f
#define	MT_TMAC_CTCR0_INS_DDLMT_EN		(1U << 17)
#define	MT_TMAC_CTCR0_INS_DDLMT_VHT_SMPDU_EN	(1U << 18)

#define	MT_TMAC_TRCR0(_band)		MT_BAND_ADDR(_band, 0x409c)
#define	MT_TMAC_TFCR0(_band)		MT_BAND_ADDR(_band, 0x41e0)

#define	MT_DMA_DCR0(_band)		MT_BAND_ADDR(_band, 0x7000)
#define	MT_DMA_DCR0_MAX_RX_LEN_MASK	0x0000fff8
#define	MT_DMA_DCR0_MAX_RX_LEN_SHIFT	3
#define	MT_DMA_DCR0_RXD_G5_EN		(1U << 23)

/* MIB: band 0(0x24800), band 1(0xa4800) */
#define	MT_MIB_SCR1(_band)		MT_BAND_ADDR(_band, 0xd004)
#define	MT_MIB_TXDUR_EN			0x0100
#define	MT_MIB_RXDUR_EN			0x0200

#define	MT_MIB_SDR9(_band)		MT_BAND_ADDR(_band, 0xd02c)
#define	MT_MIB_SDR9_BUSY_MASK		0x00ffffff

#define	MT_MIB_SDR36(_band)		MT_BAND_ADDR(_band, 0xd054)
#define	MT_MIB_SDR36_TXTIME_MASK	0x00ffffff
#define	MT_MIB_SDR37(_band)		MT_BAND_ADDR(_band, 0xd058)
#define	MT_MIB_SDR37_RXTIME_MASK	0x00ffffff

#define	MT_TX_AGG_CNT(_band, n)		MT_BAND_ADDR(_band, 0xd7dc + ((n) << 2))
#define	MT_TX_AGG_CNT2(_band, n)	MT_BAND_ADDR(_band, 0xd7ec + ((n) << 2))

#define	MT_WTBLON_TOP_BASE		0x820d4000
#define	MT_WTBLON_TOP_WDUCR		0x820d4200
#define	MT_WTBLON_TOP_WDUCR_GROUP	0x0007

#define	MT_WTBL_UPDATE			0x820d4230
#define	MT_WTBL_UPDATE_WLAN_IDX		0x000003ff
#define	MT_WTBL_UPDATE_ADM_COUNT_CLEAR  (1U << 12)
#define	MT_WTBL_UPDATE_BUSY		(1U << 31)

#define	MT_WTBL_BASE			0x820d8000
#define	MT_WTBL_LMAC_ID			GENMASK(14, 8)
#define	MT_WTBL_LMAC_DW			GENMASK(7, 2)
#define	MT_WTBL_LMAC_OFFS(_id, _dw)	(MT_WTBL_BASE | \
					FIELD_PREP(MT_WTBL_LMAC_ID, _id) | \
					FIELD_PREP(MT_WTBL_LMAC_DW, _dw))

/* AGG: band 0(0x20800), band 1(0xa0800) */
#define	MT_AGG_ACR0(_band)		MT_BAND_ADDR(_band, 0x2084)
#define	MT_AGG_ACR_CFEND_RATE_MASK	0x00001fff
#define	MT7921_CFEND_RATE_DEFAULT       0x49    /* OFDM 24M */
#define	MT7921_CFEND_RATE_11B		0x03    /* 11B LP, 11M */
#define	MT_AGG_ACR_BAR_RATE		GENMASK(29, 16)

/* RMAC: band 0 (0x21400), band 1 (0xa1400) */
#define	MT_WF_RFCR(_base)		MT_BAND_ADDR(_base, 0x5000)
#define	MT_WF_RFCR_DROP_STBC_MULTI	0x00000001
#define	MT_WF_RFCR_DROP_FCSFAIL		0x00000002
#define	MT_WF_RFCR_DROP_VERSION		0x00000008
#define	MT_WF_RFCR_DROP_PROBEREQ	0x00000010
#define	MT_WF_RFCR_DROP_MCAST		0x00000020
#define	MT_WF_RFCR_DROP_BCAST		0x00000040
#define	MT_WF_RFCR_DROP_MCAST_FILTERED	0x00000080
#define	MT_WF_RFCR_DROP_A3_MAC		0x00000100
#define	MT_WF_RFCR_DROP_A3_BSSID	0x00000200
#define	MT_WF_RFCR_DROP_A2_BSSID	0x00000400
#define	MT_WF_RFCR_DROP_OTHER_BEACON	0x00000800
#define	MT_WF_RFCR_DROP_FRAME_REPORT	0x00001000
#define	MT_WF_RFCR_DROP_CTL_RSV		0x00002000
#define	MT_WF_RFCR_DROP_CTS		0x00004000
#define	MT_WF_RFCR_DROP_RTS		0x00008000
#define	MT_WF_RFCR_DROP_DUPLICATE	0x00010000
#define	MT_WF_RFCR_DROP_OTHER_BSS	0x00020000
#define	MT_WF_RFCR_DROP_OTHER_UC	0x00040000
#define	MT_WF_RFCR_DROP_OTHER_TIM	0x00080000
#define	MT_WF_RFCR_DROP_NDPA		0x00100000
#define	MT_WF_RFCR_DROP_UNWANTED_CTL	0x00200000

#define	MT_WF_RFCR1(_band)		MT_BAND_ADDR(_band, 0x5004)
#define	MT_WF_RFCR1_DROP_ACK		(1U << 4)
#define	MT_WF_RFCR1_DROP_BF_POLL	(1U << 5)
#define	MT_WF_RFCR1_DROP_BA		(1U << 6)
#define	MT_WF_RFCR1_DROP_CFEND		(1U << 7)
#define	MT_WF_RFCR1_DROP_CFACK		(1U << 8)

#define	MT_WF_RMAC_MIB_TIME0(_band)	MT_BAND_ADDR(_band, 0x53c4)
#define	MT_WF_RMAC_MIB_RXTIME_CLR	(1U << 31)
#define	MT_WF_RMAC_MIB_RXTIME_EN	(1U << 30)

#define	MT_WF_RMAC_MIB_AIRTIME14(_band)	MT_BAND_ADDR(_band, 0x53b8)
#define	MT_MIB_OBSSTIME_MASK		0x00ffffff
#define	MT_WF_RMAC_MIB_AIRTIME0(_band)	MT_BAND_ADDR(_band, 0x5380)

/* ARB: band 0(0x20c00), band 1(0xa0c00) */
#define	MT_ARB_SCR(_band)		MT_BAND_ADDR(_band, 0x3080)
#define	MT_ARB_SCR_TX_DISABLE		(1U << 8)
#define	MT_ARB_SCR_RX_DISABLE		(1U << 9)

/* WFDMA0 */
#define	MT_WFDMA0_BASE			0xd4000

#define	MT_WFDMA0_RST			0xd4100
#define	MT_WFDMA0_RST_LOGIC_RST		(1U << 4)
#define	MT_WFDMA0_RST_DMASHDL_ALL_RST	(1U << 5)

#define	MT_MCU_CMD			0xd41f0
#define	MT_MCU_CMD_WAKE_RX_PCIE		(1U << 0)
#define	MT_MCU_CMD_STOP_DMA_FW_RELOAD	(1U << 1)
#define	MT_MCU_CMD_STOP_DMA		(1U << 2)
#define	MT_MCU_CMD_RESET_DONE		(1U << 3)
#define	MT_MCU_CMD_RECOVERY_DONE	(1U << 4)
#define	MT_MCU_CMD_NORMAL_STATE		(1U << 5)
#define	MT_MCU_CMD_ERROR_MASK		0x003e

#define	MT_MCU2HOST_SW_INT_ENA		0xd41f4

#define	MT_WFDMA0_HOST_INT_STA		0xd4200
#define	HOST_RX_DONE_INT_STS0		(1U << 0)	/* Rx mcu */
#define	HOST_RX_DONE_INT_STS2		(1U << 2)	/* Rx data */
#define	HOST_RX_DONE_INT_STS4		(1U << 22)	/* Rx mcu after fw downloaded */
#define	HOST_TX_DONE_INT_STS16		(1U << 26)
#define	HOST_TX_DONE_INT_STS17		(1U << 27)	/* MCU tx done*/

#define	MT_WFDMA0_HOST_INT_ENA		0xd4204
#define	HOST_RX_DONE_INT_ENA0		(1U << 0)
#define	HOST_RX_DONE_INT_ENA1		(1U << 1)
#define	HOST_RX_DONE_INT_ENA2		(1U << 2)
#define	HOST_RX_DONE_INT_ENA3		(1U << 3)
#define	HOST_TX_DONE_INT_ENA0		(1U << 4)
#define	HOST_TX_DONE_INT_ENA1		(1U << 5)
#define	HOST_TX_DONE_INT_ENA2		(1U << 6)
#define	HOST_TX_DONE_INT_ENA3		(1U << 7)
#define	HOST_TX_DONE_INT_ENA4		(1U << 8)
#define	HOST_TX_DONE_INT_ENA5		(1U << 9)
#define	HOST_TX_DONE_INT_ENA6		(1U << 10)
#define	HOST_TX_DONE_INT_ENA7		(1U << 11)
#define	HOST_TX_DONE_INT_ENA8		(1U << 12)
#define	HOST_TX_DONE_INT_ENA9		(1U << 13)
#define	HOST_TX_DONE_INT_ENA10		(1U << 14)
#define	HOST_TX_DONE_INT_ENA11		(1U << 15)
#define	HOST_TX_DONE_INT_ENA12		(1U << 16)
#define	HOST_TX_DONE_INT_ENA13		(1U << 17)
#define	HOST_TX_DONE_INT_ENA14		(1U << 18)
#define	HOST_RX_COHERENT_EN		(1U << 20)
#define	HOST_TX_COHERENT_EN		(1U << 21)
#define	HOST_RX_DONE_INT_ENA4		(1U << 22)
#define	HOST_RX_DONE_INT_ENA5		(1U << 23)
#define	HOST_TX_DONE_INT_ENA16		(1U << 26)
#define	HOST_TX_DONE_INT_ENA17		(1U << 27)
#define	MCU2HOST_SW_INT_ENA		(1U << 29)
#define	HOST_TX_DONE_INT_ENA18		(1U << 30)

#define	MT_PCIE_MAC_BASE		0x10000
#define	MT_PCIE_MAC_INT_ENABLE		0x10188
#define	MT_PCIE_MAC_PM			0x10194
#define	MT_PCIE_MAC_PM_L0S_DIS		(1U << 8)

/* WFDMA interrupt */
#define	MT_INT_RX_DONE_DATA		HOST_RX_DONE_INT_ENA2
#define	MT_INT_RX_DONE_WM		HOST_RX_DONE_INT_ENA0
#define	MT_INT_RX_DONE_WM2		HOST_RX_DONE_INT_ENA4
#define	MT_INT_RX_DONE_ALL		(MT_INT_RX_DONE_DATA |	\
					MT_INT_RX_DONE_WM |	\
					MT_INT_RX_DONE_WM2)

#define	MT_INT_TX_DONE_MCU_WM		HOST_TX_DONE_INT_ENA17
#define	MT_INT_TX_DONE_FWDL		HOST_TX_DONE_INT_ENA16
#define	MT_INT_TX_DONE_BAND0		HOST_TX_DONE_INT_ENA0
#define	MT_INT_MCU_CMD			MCU2HOST_SW_INT_ENA
#define	MT_INT_TX0_TO_TX14		0x7fff0

#define	MT_INT_TX_DONE_MCU		(MT_INT_TX_DONE_MCU_WM |	\
					MT_INT_TX_DONE_FWDL)

#define	MT_INT_TX_DONE_ALL		(MT_INT_TX_DONE_MCU |		\
					MT_INT_TX0_TO_TX14)

#define	MT_WFDMA0_GLO_CFG		0xd4208
#define	MT_WFDMA0_GLO_CFG_TX_DMA_EN	(1U << 0)
#define	MT_WFDMA0_GLO_CFG_TX_DMA_BUSY	(1U << 1)
#define	MT_WFDMA0_GLO_CFG_RX_DMA_EN	(1U << 2)
#define	MT_WFDMA0_GLO_CFG_RX_DMA_BUSY	(1U << 3)
#define	MT_WFDMA0_GLO_CFG_TX_WB_DDONE	(1U << 6)
#define	MT_WFDMA0_GLO_CFG_FIFO_LITTLE_ENDIAN		(1U << 12)
#define	MT_WFDMA0_GLO_CFG_CSR_DISP_BASE_PTR_CHAIN_EN	(1U << 15)
#define	MT_WFDMA0_GLO_CFG_OMIT_RX_INFO_PFET2		(1U << 21)
#define	MT_WFDMA0_GLO_CFG_OMIT_RX_INFO	(1U << 27)
#define	MT_WFDMA0_GLO_CFG_OMIT_TX_INFO	(1U << 28)
#define	MT_WFDMA0_GLO_CFG_CLK_GAT_DIS	(1U << 30)

#define	MT_WFDMA0_RST_DTX_PTR		0xd420c
#define	MT_WFDMA0_GLO_CFG_EXT0		0xd42b0
#define	MT_WFDMA0_CSR_TX_DMASHDL_ENABLE	(1U << 6)
#define	MT_WFDMA0_PRI_DLY_INT_CFG0	0xd42f0

#define	MT_WFDMA0_TX_RING0_EXT_CTRL	0xd4600
#define	MT_WFDMA0_TX_RING1_EXT_CTRL	0xd4604
#define	MT_WFDMA0_TX_RING2_EXT_CTRL	0xd4608
#define	MT_WFDMA0_TX_RING3_EXT_CTRL	0xd460c
#define	MT_WFDMA0_TX_RING4_EXT_CTRL	0xd4610
#define	MT_WFDMA0_TX_RING5_EXT_CTRL	0xd4614
#define	MT_WFDMA0_TX_RING6_EXT_CTRL	0xd4618
#define	MT_WFDMA0_TX_RING16_EXT_CTRL	0xd4640
#define	MT_WFDMA0_TX_RING17_EXT_CTRL	0xd4644

#define	MT_WFDMA0_RX_RING0_EXT_CTRL	0xd4680
#define	MT_WFDMA0_RX_RING1_EXT_CTRL	0xd4684
#define	MT_WFDMA0_RX_RING2_EXT_CTRL	0xd4688
#define	MT_WFDMA0_RX_RING3_EXT_CTRL	0xd468c
#define	MT_WFDMA0_RX_RING4_EXT_CTRL	0xd4690
#define	MT_WFDMA0_RX_RING5_EXT_CTRL	0xd4694

#define	MT_TX_DATA_RING_BASE		0xd4300
#define	MT_TX_FWDL_RING_BASE		0xd4400
#define	MT_TX_MCU_RING_BASE		0xd4410
#define	MT_RX_DATA_RING_BASE		0xd4520
#define	MT_RX_MCU_RING_BASE		0xd4540
#define	MT_RX_FWDL_RING_BASE		0xd4500

#define	MT_PCIE_MAC_BASE		0x10000
#define	MT_PCIE_MAC_INT_ENABLE		0x10188

#define	MT_INFRA_CFG_BASE		0xfe000
#define	MT_HIF_REMAP_L1			0xfe24c
#define	MT_HIF_REMAP_L1_MASK		0x0000ffff
#define	MT_HIF_REMAP_L1_GET_OFFSET(x)	((x) & 0xffff)
#define	MT_HIF_REMAP_L1_GET_BASE(x)	((x >> 16) & 0xffff)
#define	MT_HIF_REMAP_BASE_L1		0x40000

#define	MT_SWDEF_BASE			0x41f200
#define	MT_SWDEF_MODE			0x41f23c
#define	MT_SWDEF_NORMAL_MODE		0
#define	MT_SWDEF_ICAP_MODE		1
#define	MT_SWDEF_SPECTRUM_MODE		2

#define	MT_DMASHDL_SW_CONTROL		0xd6004
#define	MT_DMASHDL_DMASHDL_BYPASS	(1U << 28)
#define	MT_DMASHDL_OPTIONAL		0xd6008
#define	MT_DMASHDL_PAGE			0xd600c
#define	MT_DMASHDL_REFILL		0xd6010
#define	MT_DMASHDL_PKT_MAX_SIZE		0xd601c
#define	MT_DMASHDL_PKT_MAX_SIZE_PLE	0x00000fff
#define	MT_DMASHDL_PKT_MAX_SIZE_PSE	0x0fff0000

#define	MT_CONN_ON_MISC			0x7c0600f0
#define	MT_TOP_MISC2_FW_N9_RDY		0x3

#define	MT_CONN_ON_LPCTL		0x7c060010
#define	PCIE_LPCR_HOST_SET_OWN		(1U << 0)
#define	PCIE_LPCR_HOST_CLR_OWN		(1U << 1)
#define	PCIE_LPCR_HOST_OWN_SYNC		(1U << 2)

#define	MT_WFSYS_SW_RST_B		0x18000140
#define	WFSYS_SW_RST_B			(1U << 0)
#define	WFSYS_SW_INIT_DONE		(1U << 4)

#define	MT_TOP_BASE			0x18060000
#define	MT_TOP_LPCR_HOST_BAND0		0x18060010
#define	MT_TOP_LPCR_HOST_FW_OWN		0x0001
#define	MT_TOP_LPCR_HOST_DRV_OWN	0x0002

#define	MT_MCU_WPDMA0_BASE		0x54000000
#define	MT_WFDMA_DUMMY_CR		0x54000120
#define	MT_WFDMA_NEED_REINIT		(1U << 1)

#define	MT_HW_CHIPID			0x70010200
#define	MT_HW_REV			0x70010204

#define	MT_DMA_DESC_BASE		0
#define	MT_DMA_RING_SIZE		4
#define	MT_DMA_CPU_IDX			8
#define	MT_DMA_DMA_IDX			12

#define	MT_DMA_CTL_SD_LEN_MASK		0x00003fff
#define	MT_DMA_CTL_SD_LEN0_SHIFT	16
#define	MT_DMA_CTL_LAST_SEC1		(1U << 14)
#define	MT_DMA_CTL_BURST		(1U << 15)
#define	MT_DMA_CTL_LAST_SEC0		(1U << 30)
#define	MT_DMA_CTL_DMA_DONE		(1U << 31)
#define	MT_DMA_CTL_SD_LEN1(x)		((x) & MT_DMA_CTL_SD_LEN_MASK)
#define	MT_DMA_CTL_SD_LEN0(x)		\
	    (((x) & MT_DMA_CTL_SD_LEN_MASK) << MT_DMA_CTL_SD_LEN0_SHIFT)
#define	MT_DMA_CTL_SD_GET_LEN1(c)	((c) & MT_DMA_CTL_SD_LEN_MASK)
#define	MT_DNA_CTL_SD_GET_LEN0(c)	\
	    (((c) >> MT_DMA_CTL_SD_LEN0_SHIFT) & MT_DMA_CTL_SD_LEN_MASK)

#define	MT7921_MCU_INIT_RETRY_COUNT	10

enum mt76_txq_id {
	MT_TXQ_VO,
	MT_TXQ_VI,
	MT_TXQ_BE,
	MT_TXQ_BK,
	MT_TXQ_PSD,
	MT_TXQ_BEACON,
	MT_TXQ_CAB,
	__MT_TXQ_MAX
};

enum mt76_mcuq_id {
	MT_MCUQ_WM,
	MT_MCUQ_WA,
	MT_MCUQ_FWDL,
	__MT_MCUQ_MAX
};

enum mt76_rxq_id {
	MT_RXQ_MAIN,
	MT_RXQ_MCU,
	MT_RXQ_MCU_WA,
	MT_RXQ_BAND1,
	MT_RXQ_BAND1_WA,
	__MT_RXQ_MAX
};

#define	MT7921_MAX_INTERFACES	4
#define	MT7921_MAX_WMM_SETS	4
#define	MT7921_WTBL_SIZE	20
#define	MT7921_WTBL_RESERVED	(MT7921_WTBL_SIZE - 1)
#define	MT7921_WTBL_STA		(MT7921_WTBL_RESERVED - MT7921_MAX_INTERFACES)

#define	MT_RX_BUF_SIZE		2048
#define	MT_MAX_SCATTER		4	/* limit of MT_HW_TXP_MAX_BUF_NUM */
#define	MT_MAX_SIZE		MT_DMA_CTL_SD_LEN_MASK

#define	MWX_WCID_MAX		288
#define	MWX_TXWI_MAX		512	/* HW limit is 8192 */

#define	MT_PACKET_ID_MASK		0x7f
#define	MT_PACKET_ID_NO_ACK		0
#define	MT_PACKET_ID_NO_SKB		1
#define	MT_PACKET_ID_WED		2
#define	MT_PACKET_ID_FIRST		3
#define	MT_PACKET_ID_HAS_RATE		(1U << 7)

struct mt76_desc {
	volatile uint32_t	buf0;
	volatile uint32_t	ctrl;
	volatile uint32_t	buf1;
	volatile uint32_t	info;
} __packed __aligned(4);

struct mt76_connac_txp_ptr {
	uint32_t	buf0;
	uint16_t	len0;
	uint16_t	len1;
	uint32_t	buf1;
} __packed __aligned(4);

#define	MT_HW_TXP_MAX_MSDU_NUM		4
#define	MT_HW_TXP_MAX_BUF_NUM		4
#define	MT_TXD_SIZE			(8 * sizeof(uint32_t))

#define	MT_TXD_LEN_LAST			(1U << 15)
#define	MT_TXD_LEN_MASK			0x00000fff

#define	MT_MSDU_ID_VALID		(1U << 15)

struct mt76_txwi {
	uint32_t			txwi[8];

	uint16_t			msdu_id[MT_HW_TXP_MAX_MSDU_NUM];
	struct mt76_connac_txp_ptr	ptr[MT_HW_TXP_MAX_BUF_NUM / 2];
} __packed __aligned(4);

#define	MCU_Q_QUERY				0
#define	MCU_Q_SET				1
#define	MCU_Q_RESERVED				2
#define	MCU_Q_NA				3

#define	CMD_S2D_IDX_H2N				0
#define	CMD_S2D_IDX_C2N				1
#define	CMD_S2D_IDX_H2C				2
#define	CMD_S2D_IDX_H2N_AND_H2C			3

#define	MCU_CMD_ACK				0x01
#define	MCU_CMD_UNI				0x02
#define	MCU_CMD_QUERY				0x04
#define	MCU_CMD_UNI_EXT_ACK	(MCU_CMD_ACK | MCU_CMD_UNI | MCU_CMD_QUERY)

#define	MCU_CMD_FIELD_ID_MASK			0x000000ff
#define	MCU_CMD_FIELD_EXT_ID_MASK		0x0000ff00

#define	MCU_CMD_FIELD_QUERY			(1U << 16)
#define	MCU_CMD_FIELD_UNI			(1U << 17)
#define	MCU_CMD_FIELD_CE			(1U << 18)
#define	MCU_CMD_FIELD_WA			(1U << 19)

#define	MCU_CMD_TARGET_ADDRESS_LEN_REQ		0x00000001
#define	MCU_CMD_FW_START_REQ			0x00000002
#define	MCU_CMD_INIT_ACCESS_REG			0x00000003
#define	MCU_CMD_NIC_POWER_CTRL			0x00000004
#define	MCU_CMD_PATCH_START_REQ			0x00000005
#define	MCU_CMD_PATCH_FINISH_REQ		0x00000007
#define	MCU_CMD_PATCH_SEM_CONTROL		0x00000010
#define	MCU_CMD_WA_PARAM			0x000000c4
#define	MCU_CMD_EXT_CID				0x000000ed
#define	MCU_CMD_FW_SCATTER			0x000000ee
#define	MCU_CMD_RESTART_DL_REQ			0x000000ef

/* MCU_EXT_CMD use MCU_CMD_EXT_CID as FIELD_ID plus FIELD_EXT_ID */
#define	MCU_EXT_CMD_EFUSE_ACCESS		0x000001ed
#define	MCU_EXT_CMD_RF_REG_ACCESS		0x000002ed
#define	MCU_EXT_CMD_RF_TEST			0x000004ed
#define	MCU_EXT_CMD_PM_STATE_CTRL		0x000007ed
#define	MCU_EXT_CMD_CHANNEL_SWITCH		0x000008ed
#define	MCU_EXT_CMD_SET_TX_POWER_CTRL		0x000011ed
#define	MCU_EXT_CMD_FW_LOG_2_HOST		0x000013ed
#define	MCU_EXT_CMD_TXBF_ACTION			0x00001eed
#define	MCU_EXT_CMD_EFUSE_BUFFER_MODE		0x000021ed
#define	MCU_EXT_CMD_THERMAL_PROT		0x000023ed
#define	MCU_EXT_CMD_STA_REC_UPDATE		0x000025ed
#define	MCU_EXT_CMD_BSS_INFO_UPDATE		0x000026ed
#define	MCU_EXT_CMD_EDCA_UPDATE			0x000027ed
#define	MCU_EXT_CMD_DEV_INFO_UPDATE		0x00002Aed
#define	MCU_EXT_CMD_THERMAL_CTRL		0x00002ced
#define	MCU_EXT_CMD_WTBL_UPDATE			0x000032ed
#define	MCU_EXT_CMD_SET_DRR_CTRL		0x000036ed
#define	MCU_EXT_CMD_SET_RDD_CTRL		0x00003aed
#define	MCU_EXT_CMD_ATE_CTRL			0x00003ded
#define	MCU_EXT_CMD_PROTECT_CTRL		0x00003eed
#define	MCU_EXT_CMD_DBDC_CTRL			0x000045ed
#define	MCU_EXT_CMD_MAC_INIT_CTRL		0x000046ed
#define	MCU_EXT_CMD_RX_HDR_TRANS		0x000047ed
#define	MCU_EXT_CMD_MUAR_UPDATE			0x000048ed
#define	MCU_EXT_CMD_BCN_OFFLOAD			0x000049ed
#define	MCU_EXT_CMD_RX_AIRTIME_CTRL		0x00004aed
#define	MCU_EXT_CMD_SET_RX_PATH			0x00004eed
#define	MCU_EXT_CMD_EFUSE_FREE_BLOCK		0x00004fed
#define	MCU_EXT_CMD_TX_POWER_FEATURE_CTRL	0x000058ed
#define	MCU_EXT_CMD_RXDCOC_CAL			0x000059ed
#define	MCU_EXT_CMD_GET_MIB_INFO		0x00005aed
#define	MCU_EXT_CMD_TXDPD_CAL			0x000060ed
#define	MCU_EXT_CMD_CAL_CACHE			0x000067ed
#define	MCU_EXT_CMD_SET_RADAR_TH		0x00007ced
#define	MCU_EXT_CMD_SET_RDD_PATTERN		0x00007ded
#define	MCU_EXT_CMD_MWDS_SUPPORT		0x000080ed
#define	MCU_EXT_CMD_SET_SER_TRIGGER		0x000081ed
#define	MCU_EXT_CMD_SCS_CTRL			0x000082ed
#define	MCU_EXT_CMD_TWT_AGRT_UPDATE		0x000094ed
#define	MCU_EXT_CMD_FW_DBG_CTRL			0x000095ed
#define	MCU_EXT_CMD_SET_RDD_TH			0x00009ded
#define	MCU_EXT_CMD_MURU_CTRL			0x00009fed
#define	MCU_EXT_CMD_SET_SPR			0x0000a8ed
#define	MCU_EXT_CMD_GROUP_PRE_CAL_INFO		0x0000abed
#define	MCU_EXT_CMD_DPD_PRE_CAL_INFO		0x0000aced
#define	MCU_EXT_CMD_PHY_STAT_INFO		0x0000aded

#define	MCU_GET_EXT_CMD(x)		(((x) & MCU_CMD_FIELD_EXT_ID_MASK) >> 8)

#define	MCU_UNI_CMD_DEV_INFO_UPDATE		0x00020001
#define	MCU_UNI_CMD_BSS_INFO_UPDATE		0x00020002
#define	MCU_UNI_CMD_STA_REC_UPDATE		0x00020003
#define	MCU_UNI_CMD_SUSPEND			0x00020005
#define	MCU_UNI_CMD_OFFLOAD			0x00020006
#define	MCU_UNI_CMD_HIF_CTRL			0x00020007
#define	MCU_UNI_CMD_SNIFFER			0x00020024

#define	UNI_BSS_INFO_BASIC			0
#define	UNI_BSS_INFO_RLM			2
#define	UNI_BSS_INFO_BSS_COLOR			4
#define	UNI_BSS_INFO_HE_BASIC			5
#define	UNI_BSS_INFO_BCN_CONTENT		7
#define	UNI_BSS_INFO_QBSS			15
#define	UNI_BSS_INFO_UAPSD			19
#define	UNI_BSS_INFO_PS				21
#define	UNI_BSS_INFO_BCNFT			22

/* offload mcu commands */
#define	MCU_CE_CMD_TEST_CTRL			0x00040001
#define	MCU_CE_CMD_START_HW_SCAN		0x00040003
#define	MCU_CE_CMD_SET_PS_PROFILE		0x00040005
#define	MCU_CE_CMD_SET_CHAN_DOMAIN		0x0004000f
#define	MCU_CE_CMD_SET_BSS_CONNECTED		0x00040016
#define	MCU_CE_CMD_SET_BSS_ABORT		0x00040017
#define	MCU_CE_CMD_CANCEL_HW_SCAN		0x0004001b
#define	MCU_CE_CMD_SET_ROC			0x0004001c
#define	MCU_CE_CMD_SET_EDCA_PARMS		0x0004001d
#define	MCU_CE_CMD_SET_P2P_OPPPS		0x00040033
#define	MCU_CE_CMD_SET_CLC			0x0004005c
#define	MCU_CE_CMD_SET_RATE_TX_POWER		0x0004005d
#define	MCU_CE_CMD_SCHED_SCAN_ENABLE		0x00040061
#define	MCU_CE_CMD_SCHED_SCAN_REQ		0x00040062
#define	MCU_CE_CMD_GET_NIC_CAPAB		0x0004008a
#define	MCU_CE_CMD_SET_MU_EDCA_PARMS		0x000400b0
#define	MCU_CE_CMD_REG_WRITE			0x000400c0
#define	MCU_CE_CMD_REG_READ			0x000400c0
#define	MCU_CE_CMD_CHIP_CONFIG			0x000400ca
#define	MCU_CE_CMD_FWLOG_2_HOST			0x000400c5
#define	MCU_CE_CMD_GET_WTBL			0x000400cd
#define	MCU_CE_CMD_GET_TXPWR			0x000400d0
#define	MCU_CE_QUERY_REG_READ	(MCU_CE_CMD_REG_READ | MCU_CMD_FIELD_QUERY)

/* event commands */
#define	MCU_EVENT_TARGET_ADDRESS_LEN		0x01
#define	MCU_EVENT_FW_START			0x01
#define	MCU_EVENT_GENERIC			0x01
#define	MCU_EVENT_ACCESS_REG			0x02
#define	MCU_EVENT_MT_PATCH_SEM			0x04
#define	MCU_EVENT_REG_ACCESS			0x05
#define	MCU_EVENT_LP_INFO			0x07
#define	MCU_EVENT_SCAN_DONE			0x0d
#define	MCU_EVENT_TX_DONE			0x0f
#define	MCU_EVENT_ROC				0x10
#define	MCU_EVENT_BSS_ABSENCE			0x11
#define	MCU_EVENT_BSS_BEACON_LOSS		0x13
#define	MCU_EVENT_CH_PRIVILEGE			0x18
#define	MCU_EVENT_SCHED_SCAN_DONE		0x23
#define	MCU_EVENT_DBG_MSG			0x27
#define	MCU_EVENT_TXPWR				0xd0
#define	MCU_EVENT_EXT				0xed
#define	MCU_EVENT_RESTART_DL			0xef
#define	MCU_EVENT_COREDUMP			0xf0

/* extended event commands */
#define	MCU_EXT_EVENT_PS_SYNC			0x5
#define	MCU_EXT_EVENT_FW_LOG_2_HOST		0x13
#define	MCU_EXT_EVENT_THERMAL_PROTECT		0x22
#define	MCU_EXT_EVENT_ASSERT_DUMP		0x23
#define	MCU_EXT_EVENT_RDD_REPORT		0x3a
#define	MCU_EXT_EVENT_CSA_NOTIFY		0x4f
#define	MCU_EXT_EVENT_BCC_NOTIFY		0x75
#define	MCU_EXT_EVENT_RATE_REPORT		0x87
#define	MCU_EXT_EVENT_MURU_CTRL			0x9f

#define	MCU_PQ_ID(p, q)				(((p) << 15) | ((q) << 10))
#define	MCU_PKT_ID				0xa0

/* values for MT_TXD0_PKT_FMT */
#define	MT_TX_TYPE_CT				(0 << 23)
#define	MT_TX_TYPE_SF				(1 << 23)
#define	MT_TX_TYPE_CMD				(2 << 23)
#define	MT_TX_TYPE_FW				(3 << 23)

/* values for port idx */
#define	MT_TX_PORT_IDX_LMAC			0
#define	MT_TX_PORT_IDX_MCU			1

/* values for EEPROM command */
#define	EE_MODE_EFUSE		0
#define	EE_MODE_BUFFER		1

#define	EE_FORMAT_BIN		0
#define	EE_FORMAT_WHOLE		1
#define	EE_FORMAT_MULTIPLE	2

#define	MT_CTX0			0x0
#define	MT_HIF0			0x0
#define	MT_LMAC_AC00		0x0
#define	MT_LMAC_AC01		0x1
#define	MT_LMAC_AC02		0x2
#define	MT_LMAC_AC03		0x3
#define	MT_LMAC_ALTX0		0x10
#define	MT_LMAC_BMC0		0x11
#define	MT_LMAC_BCN0		0x12
#define	MT_LMAC_PSMP0		0x13

/* values for MT_TXD0_Q_IDX */
#define	MT_TX_MCU_PORT_RX_Q0			0x20
#define	MT_TX_MCU_PORT_RX_Q1			0x21
#define	MT_TX_MCU_PORT_RX_Q2			0x22
#define	MT_TX_MCU_PORT_RX_Q3			0x23
#define	MT_TX_MCU_PORT_RX_FWDL			0x3e

#define	MT_TXD0_Q_IDX_MASK			0xfe000000
#define	MT_TXD0_Q_IDX(x)		(((x) << 25) & MT_TXD0_Q_IDX_MASK)
#define	MT_TXD0_PKT_FMT				0x01800000
#define	MT_TXD0_ETH_TYPE_OFFSET			0x007f0000
#define	MT_TXD0_TX_BYTES_MASK			0x0000ffff

/* values for MT_TXD1_HDR_FORMAT */
#define	MT_HDR_FORMAT_802_3			(0 << 16)
#define	MT_HDR_FORMAT_CMD			(1 << 16)
#define	MT_HDR_FORMAT_802_11			(2 << 16)
#define	MT_HDR_FORMAT_802_11_EXT		(3 << 16)

#define	MT_TXD1_LONG_FORMAT			(1U << 31)
#define	MT_TXD1_TGID				(1U << 30)
#define	MT_TXD1_OWN_MAC_MASK			0x3f000000
#define	MT_TXD1_OWN_MAC_SHIFT			24
#define	MT_TXD1_AMSDU				(1U << 23)
#define	MT_TXD1_TID_MASK			0x00700000
#define	MT_TXD1_TID(x)			(((x) << 20) & MT_TXD1_TID_MASK)
#define	MT_TXD1_HDR_PAD_MASK			0x000c0000
#define	MT_TXD1_HDR_PAD_SHIFT			18
#define	MT_TXD1_HDR_FORMAT_MASK			0x00030000
#define	MT_TXD1_HDR_FORMAT_SHIFT		16
#define	MT_TXD1_HDR_INFO_MASK			0x0000f800
#define MT_TXD1_HDR_INFO(x)		(((x) << 11) & MT_TXD1_HDR_INFO_MASK)
#define	MT_TXD1_ETH_802_3			(1U << 15)
#define	MT_TXD1_VTA				(1U << 10)
#define	MT_TXD1_WLAN_IDX_MASK			0x000003ff

#define	MT_TXD2_FIX_RATE			(1U << 31)
#define	MT_TXD2_FIXED_RATE			(1U << 30)
#define	MT_TXD2_POWER_OFFSET_MASK		0x3f000000
#define	MT_TXD2_POWER_OFFSET_SHIFT		24
#define	MT_TXD2_MAX_TX_TIME_MASK		0x00ff0000
#define	MT_TXD2_MAX_TX_TIME_SHIFT		16
#define	MT_TXD2_FRAG				0x0000c000
#define	MT_TXD2_HTC_VLD				(1U << 13)
#define	MT_TXD2_DURATION			(1U << 12)
#define	MT_TXD2_BIP				(1U << 11)
#define	MT_TXD2_MULTICAST			(1U << 10)
#define	MT_TXD2_RTS				(1U << 9)
#define	MT_TXD2_SOUNDING			(1U << 8)
#define	MT_TXD2_NDPA				(1U << 7)
#define	MT_TXD2_NDP				(1U << 6)
#define	MT_TXD2_FRAME_TYPE_MASK			0x00000030
#define	MT_TXD2_SUB_TYPE_MASK			0x0000000f
#define	MT_TXD2_FRAME_TYPE(x)		(((x) << 4) & MT_TXD2_FRAME_TYPE_MASK)
#define	MT_TXD2_SUB_TYPE(x)		((x) & MT_TXD2_SUB_TYPE_MASK)

#define	MT_TXD3_SN_VALID			(1U << 31)
#define	MT_TXD3_PN_VALID			(1U << 30)
#define	MT_TXD3_SW_POWER_MGMT			(1U << 29)
#define	MT_TXD3_BA_DISABLE			(1U << 28)
#define	MT_TXD3_SEQ				GENMASK(27, 16)
#define	MT_TXD3_REM_TX_COUNT_MASK		0x0000f800
#define	MT_TXD3_REM_TX_COUNT_SHIFT		11
#define	MT_TXD3_TX_COUNT			GENMASK(10, 6)
#define	MT_TXD3_TIMING_MEASURE			(1U << 5)
#define	MT_TXD3_DAS				(1U << 4)
#define	MT_TXD3_EEOSP				(1U << 3)
#define	MT_TXD3_EMRD				(1U << 2)
#define	MT_TXD3_PROTECT_FRAME			(1U << 1)
#define	MT_TXD3_NO_ACK				(1U << 0)

#define	MT_TXD4_PN_LOW_MASK			0xffffffff

#define	MT_TXD5_PN_HIGH				0xffff0000
#define	MT_TXD5_MD				(1U << 15)
#define	MT_TXD5_ADD_BA				(1U << 14)
#define	MT_TXD5_TX_STATUS_HOST			(1U << 10)
#define	MT_TXD5_TX_STATUS_MCU			(1U << 9)
#define	MT_TXD5_TX_STATUS_FMT			(1U << 8)
#define	MT_TXD5_PID				0x000000ff

#define	MT_TXD6_TX_IBF				(1U << 31)
#define	MT_TXD6_TX_EBF				(1U << 30)
#define	MT_TXD6_TX_RATE_MASK			0x3fff0000
#define	MT_TXD6_TX_RATE_SHIFT			16
#define	MT_TXD6_SGI				0x0000c000
#define	MT_TXD6_HELTF				0x00003000
#define	MT_TXD6_LDPC				(1U << 11)
#define	MT_TXD6_SPE_ID_IDX			(1U << 10)
#define	MT_TXD6_ANT_ID				0x000000f0
#define	MT_TXD6_DYN_BW				(1U << 3)
#define	MT_TXD6_FIXED_BW			(1U << 2)
#define	MT_TXD6_BW				0x00000003

#define	MT_TXD7_TXD_LEN_MASK			0xc0000000
#define	MT_TXD7_UDP_TCP_SUM			(1U << 29)
#define	MT_TXD7_IP_SUM				(1U << 28)
#define	MT_TXD7_TYPE_MASK			0x00300000
#define	MT_TXD7_SUB_TYPE_MASK			0x000f0000
#define	MT_TXD7_TYPE(x)			(((x) << 20) & MT_TXD7_TYPE_MASK)
#define	MT_TXD7_SUB_TYPE(x)		(((x) << 16) & MT_TXD7_SUB_TYPE_MASK)

#define	MT_TXD7_PSE_FID				GENMASK(27, 16)
#define	MT_TXD7_SPE_IDX				GENMASK(15, 11)
#define	MT_TXD7_HW_AMSDU			(1U << 10)
#define	MT_TXD7_TX_TIME				0x000003ff

#define	MT_TX_RATE_STBC				(1U << 13)
#define	MT_TX_RATE_NSS_MASK			0x00001c00
#define	MT_TX_RATE_NSS_SHIFT			10
#define	MT_TX_RATE_MODE_MASK			0x000003c0
#define	MT_TX_RATE_MODE_SHIFT			6
#define	MT_TX_RATE_SU_EXT_TONE			(1U << 5)
#define	MT_TX_RATE_DCM				(1U << 4)
/* VHT/HE only use bits 0-3 */
#define	MT_TX_RATE_IDX_MASK			0x0000003f


#define	MT_RXD0_LENGTH_MASK			0x0000ffff
#define	MT_RXD0_PKT_FLAG_MASK			0x000f0000
#define	MT_RXD0_PKT_FLAG_SHIFT			16
#define	MT_RXD0_NORMAL_ETH_TYPE_OFS		0x007f0000
#define	MT_RXD0_NORMAL_IP_SUM			(1U << 23)
#define	MT_RXD0_NORMAL_UDP_TCP_SUM		(1U << 24)
#define	MT_RXD0_PKT_TYPE_MASK			0xf8000000
#define	MT_RXD0_PKT_TYPE_SHIFT			27
#define	MT_RXD0_PKT_TYPE_GET(x)		\
	    (((x) & MT_RXD0_PKT_TYPE_MASK) >> MT_RXD0_PKT_TYPE_SHIFT)

/* RXD DW1 */
#define	MT_RXD1_NORMAL_WLAN_IDX_MASK		0x000003ff
#define	MT_RXD1_NORMAL_GROUP_1			(1U << 11)
#define	MT_RXD1_NORMAL_GROUP_2			(1U << 12)
#define	MT_RXD1_NORMAL_GROUP_3			(1U << 13)
#define	MT_RXD1_NORMAL_GROUP_4			(1U << 14)
#define	MT_RXD1_NORMAL_GROUP_5			(1U << 15)
#define	MT_RXD1_NORMAL_SEC_MODE_MASK		0x001f0000
#define	MT_RXD1_NORMAL_SEC_MODE_SHIFT		16
#define	MT_RXD1_NORMAL_KEY_ID_MASK		0x00600000
#define	MT_RXD1_NORMAL_CM			(1U << 23)
#define	MT_RXD1_NORMAL_CLM			(1U << 24)
#define	MT_RXD1_NORMAL_ICV_ERR			(1U << 25)
#define	MT_RXD1_NORMAL_TKIP_MIC_ERR		(1U << 26)
#define	MT_RXD1_NORMAL_FCS_ERR			(1U << 27)
#define	MT_RXD1_NORMAL_BAND_IDX			(1U << 28)
#define	MT_RXD1_NORMAL_SPP_EN			(1U << 29)
#define	MT_RXD1_NORMAL_ADD_OM			(1U << 30)
#define	MT_RXD1_NORMAL_SEC_DONE			(1U << 31)

/* RXD DW2 */
#define	MT_RXD2_NORMAL_BSSID			0x0000003f
#define	MT_RXD2_NORMAL_CO_ANT			(1U << 6)
#define	MT_RXD2_NORMAL_BF_CQI			(1U << 7)
#define	MT_RXD2_NORMAL_MAC_HDR_LEN		0x00001f00
#define	MT_RXD2_NORMAL_HDR_TRANS		(1U << 13)
#define	MT_RXD2_NORMAL_HDR_OFFSET_MASK		0x0000c000
#define	MT_RXD2_NORMAL_HDR_OFFSET_SHIFT		14
#define	MT_RXD2_NORMAL_TID			0x000f0000
#define	MT_RXD2_NORMAL_MU_BAR			(1U << 21)
#define	MT_RXD2_NORMAL_SW_BIT			(1U << 22)
#define	MT_RXD2_NORMAL_AMSDU_ERR		(1U << 23)
#define	MT_RXD2_NORMAL_MAX_LEN_ERROR		(1U << 24)
#define	MT_RXD2_NORMAL_HDR_TRANS_ERROR		(1U << 25)
#define	MT_RXD2_NORMAL_INT_FRAME		(1U << 26)
#define	MT_RXD2_NORMAL_FRAG			(1U << 27)
#define	MT_RXD2_NORMAL_NULL_FRAME		(1U << 28)
#define	MT_RXD2_NORMAL_NDATA			(1U << 29)
#define	MT_RXD2_NORMAL_NON_AMPDU		(1U << 30)
#define	MT_RXD2_NORMAL_BF_REPORT		(1U << 31)

/* RXD DW3 */
#define	MT_RXD3_NORMAL_RXV_SEQ_MASK		0x000000ff
#define	MT_RXD3_NORMAL_CH_NUM_MASK		0x0000ff00
#define	MT_RXD3_NORMAL_CH_NUM_SHIFT		8
#define	MT_RXD3_NORMAL_ADDR_TYPE_MASK		0x00030000
#define	MT_RXD3_NORMAL_U2M			(1U << 16)
#define	MT_RXD3_NORMAL_HTC_VLD			(1U << 0)
#define	MT_RXD3_NORMAL_TSF_COMPARE_LOSS		(1U << 19)
#define	MT_RXD3_NORMAL_BEACON_MC		(1U << 20)
#define	MT_RXD3_NORMAL_BEACON_UC		(1U << 21)
#define	MT_RXD3_NORMAL_AMSDU			(1U << 22)
#define	MT_RXD3_NORMAL_MESH			(1U << 23)
#define	MT_RXD3_NORMAL_MHCP			(1U << 24)
#define	MT_RXD3_NORMAL_NO_INFO_WB		(1U << 25)
#define	MT_RXD3_NORMAL_DISABLE_RX_HDR_TRANS	(1U << 26)
#define	MT_RXD3_NORMAL_POWER_SAVE_STAT		(1U << 27)
#define	MT_RXD3_NORMAL_MORE			(1U << 28)
#define	MT_RXD3_NORMAL_UNWANT			(1U << 29)
#define	MT_RXD3_NORMAL_RX_DROP			(1U << 30)
#define	MT_RXD3_NORMAL_VLAN2ETH			(1U << 31)

/* RXD DW4 */
#define	MT_RXD4_NORMAL_PAYLOAD_FORMAT		0x00000003
#define	MT_RXD4_FIRST_AMSDU_FRAME		0x3
#define	MT_RXD4_MID_AMSDU_FRAME			0x2
#define	MT_RXD4_LAST_AMSDU_FRAME		0x1
#define	MT_RXD4_NORMAL_PATTERN_DROP		(1U << 9)
#define	MT_RXD4_NORMAL_CLS			(1U << 10)
#define	MT_RXD4_NORMAL_OFLD			GENMASK(12, 11)
#define	MT_RXD4_NORMAL_MAGIC_PKT		(1U << 13)
#define	MT_RXD4_NORMAL_WOL			GENMASK(18, 14)
#define	MT_RXD4_NORMAL_CLS_BITMAP		GENMASK(28, 19)
#define	MT_RXD3_NORMAL_PF_MODE			(1U << 99)
#define	MT_RXD3_NORMAL_PF_STS			GENMASK(31, 30)

#define	PKT_TYPE_TXS				0
#define	PKT_TYPE_TXRXV				1
#define	PKT_TYPE_NORMAL				2
#define	PKT_TYPE_RX_DUP_RFB			3
#define	PKT_TYPE_RX_TMR				4
#define	PKT_TYPE_RETRIEVE			5
#define	PKT_TYPE_TXRX_NOTIFY			6
#define	PKT_TYPE_RX_EVENT			7
#define	PKT_TYPE_NORMAL_MCU			8

struct mt7921_mcu_txd {
	uint32_t	txd[8];

	uint16_t	len;
	uint16_t	pq_id;

	uint8_t		cid;
	uint8_t		pkt_type;
	uint8_t		set_query; /* FW don't care */
	uint8_t		seq;

	uint8_t		uc_d2b0_rev;
	uint8_t		ext_cid;
	uint8_t		s2d_index;
	uint8_t		ext_cid_ack;

	uint32_t	reserved[5];
} __packed __aligned(4);

/**
 * struct mt7921_uni_txd - mcu command descriptor for firmware v3
 * @txd: hardware descriptor
 * @len: total length not including txd
 * @cid: command identifier
 * @pkt_type: must be 0xa0 (cmd packet by long format)
 * @frag_n: fragment number
 * @seq: sequence number
 * @checksum: 0 mean there is no checksum
 * @s2d_index: index for command source and destination
 *  Definition			| value | note
 *  CMD_S2D_IDX_H2N		| 0x00  | command from HOST to WM
 *  CMD_S2D_IDX_C2N		| 0x01  | command from WA to WM
 *  CMD_S2D_IDX_H2C		| 0x02  | command from HOST to WA
 *  CMD_S2D_IDX_H2N_AND_H2C	| 0x03  | command from HOST to WA and WM
 *
 * @option: command option
 *  BIT[0]:	UNI_CMD_OPT_BIT_ACK
 *		set to 1 to request a fw reply
 *		if UNI_CMD_OPT_BIT_0_ACK is set and UNI_CMD_OPT_BIT_2_SET_QUERY
 *		is set, mcu firmware will send response event EID = 0x01
 *		(UNI_EVENT_ID_CMD_RESULT) to the host.
 *  BIT[1]:	UNI_CMD_OPT_BIT_UNI_CMD
 *		0: original command
 *		1: unified command
 *  BIT[2]:	UNI_CMD_OPT_BIT_SET_QUERY
 *		0: QUERY command
 *		1: SET command
 */
struct mt7921_uni_txd {
	uint32_t	txd[8];

	/* DW1 */
	uint16_t	len;
	uint16_t	cid;

	/* DW2 */
	uint8_t		reserved;
	uint8_t		pkt_type;
	uint8_t		frag_n;
	uint8_t		seq;

	/* DW3 */
	uint16_t	checksum;
	uint8_t		s2d_index;
	uint8_t		option;

	/* DW4 */
	uint8_t		reserved2[4];
} __packed __aligned(4);


struct mt7921_mcu_rxd {
	uint32_t	rxd[6];
	uint16_t	len;		/* includes hdr but without rxd[6] */
	uint16_t	pkt_type_id;
	uint8_t		eid;
	uint8_t		seq;
	uint16_t	pad0;
	uint8_t		ext_eid;
	uint8_t		pad1[2];
	uint8_t		s2d_index;
} __packed;

struct mt7921_mcu_uni_event {
	uint8_t		cid;
	uint8_t		pad[3];
	uint32_t	status; /* 0: success, others: fail */
} __packed;

struct mt7921_mcu_reg_event {
	uint32_t	reg;
	uint32_t	val;
} __packed;

struct mt76_connac_config {
	uint16_t	id;
	uint8_t		type;
	uint8_t		resp_type;
	uint16_t	data_size;
	uint16_t	resv;
	uint8_t		data[320];
};

#define	MT_SKU_POWER_LIMIT      161

struct mt76_connac_sku_tlv {
	uint8_t		channel;
	int8_t		pwr_limit[MT_SKU_POWER_LIMIT];
} __packed;

struct mt76_power_limits {
	int8_t		cck[4];
	int8_t		ofdm[8];
	int8_t		mcs[4][10];
	int8_t		ru[7][12];
};

#define	MT_TX_PWR_BAND_2GHZ	1
#define	MT_TX_PWR_BAND_5GHZ	2
#define	MT_TX_PWR_BAND_6GHZ	3

struct mt76_connac_tx_power_limit_tlv {
	/* DW0 - common info*/
	uint8_t		ver;
	uint8_t		pad0;
	uint16_t	len;
	/* DW1 - cmd hint */
	uint8_t		n_chan; /* # channel */
	uint8_t		band; /* 2.4GHz - 5GHz - 6GHz */
	uint8_t		last_msg;
	uint8_t		pad1;
	/* DW3 */
	uint8_t		alpha2[4]; /* regulatory_request.alpha2 */
	uint8_t		pad2[32];
} __packed;

struct mt76_connac_bss_basic_tlv {
	uint16_t	tag;
	uint16_t	len;
	uint8_t		active;
	uint8_t		omac_idx;
	uint8_t		hw_bss_idx;
	uint8_t		band_idx;
	uint32_t	conn_type;
	uint8_t		conn_state;
	uint8_t		wmm_idx;
	uint8_t		bssid[IEEE80211_ADDR_LEN];
	uint16_t	bmc_tx_wlan_idx;
	uint16_t	bcn_interval;
	uint8_t		dtim_period;
	uint8_t		phymode;	/* bit(0): A
					 * bit(1): B
					 * bit(2): G
					 * bit(3): GN
					 * bit(4): AN
					 * bit(5): AC
					 * bit(6): AX2
					 * bit(7): AX5
					 * bit(8): AX6
					 */
	uint16_t	sta_idx;
	uint16_t	nonht_basic_phy;
	uint8_t		phymode_ext; /* bit(0) AX_6G */
	uint8_t		pad[1];
} __packed;

struct mt76_connac_mcu_scan_ssid {
	uint32_t	ssid_len;
	uint8_t		ssid[IEEE80211_NWID_LEN];
} __packed;

struct mt76_connac_mcu_scan_channel {
	uint8_t		 band;	/* 1: 2.4GHz
				 * 2: 5.0GHz
				 * Others: Reserved
				 */
	uint8_t		 channel_num;
} __packed;

struct mt76_connac_mcu_scan_match {
	uint32_t	rssi_th;
	uint8_t		ssid[IEEE80211_NWID_LEN];
	uint8_t		ssid_len;
	uint8_t		rsv[3];
} __packed;

#define	SCAN_FUNC_RANDOM_MAC		0x1
#define	SCAN_FUNC_SPLIT_SCAN		0x20
#define	MT76_HW_SCAN_IE_LEN		600

struct mt76_connac_hw_scan_req {
	uint8_t		seq_num;
	uint8_t		bss_idx;
	uint8_t		scan_type;	/* 0: PASSIVE SCAN
					 * 1: ACTIVE SCAN
					 */
	uint8_t		ssid_type;	/* 0x1 wildcard SSID
					 * 0x2 P2P wildcard SSID
					 * 0x4 specified SSID + wildcard SSID
					 * 0x4 + ssid_type_ext 0x1
					 *     specified SSID only
					 */
	uint8_t		ssids_num;
	uint8_t		probe_req_num;	/* Number of probe request per SSID */
	uint8_t		scan_func;	/* 0x1 Enable random MAC scan
					 * 0x2 Disable DBDC scan type 1~3.
					 * 0x4 Use DBDC scan type 3
					 *    (dedicated one RF to scan).
					 */
	uint8_t		version;	/* 0: Not support fields after ies.
					 * 1: Support fields after ies.
					 */
	struct mt76_connac_mcu_scan_ssid ssids[4];
	uint16_t	probe_delay_time;
	uint16_t	channel_dwell_time;	/* channel Dwell interval */
	uint16_t	timeout_value;
	uint8_t		channel_type;	/* 0: Full channels
					 * 1: Only 2.4GHz channels
					 * 2: Only 5GHz channels
					 * 3: P2P social channels only
					 *     (channel #1, #6 and #11)
					 * 4: Specified channels
					 * Others: Reserved
					 */
	uint8_t		channels_num;	/* valid when channel_type is 4 */
	/* valid when channels_num is set */
	struct mt76_connac_mcu_scan_channel channels[32];
	uint16_t	ies_len;
	uint8_t		ies[MT76_HW_SCAN_IE_LEN];
	/* following fields are valid if version > 0 */
	uint8_t		ext_channels_num;
	uint8_t		ext_ssids_num;
	uint16_t	channel_min_dwell_time;
	struct mt76_connac_mcu_scan_channel ext_channels[32];
	struct mt76_connac_mcu_scan_ssid ext_ssids[6];
	uint8_t		bssid[IEEE80211_ADDR_LEN];
	/* valid when BIT(1) in scan_func is set. */
	uint8_t		random_mac[IEEE80211_ADDR_LEN];
	uint8_t		pad[63];
	uint8_t		ssid_type_ext;
} __packed;

#define	MT76_HW_SCAN_DONE_MAX_CHANNEL_NUM		64

struct mt76_connac_hw_scan_done {
	uint8_t		seq_num;
	uint8_t		sparse_channel_num;
	struct mt76_connac_mcu_scan_channel sparse_channel;
	uint8_t		complete_channel_num;
	uint8_t		current_state;
	uint8_t		version;
	uint8_t		pad;
	uint32_t	beacon_scan_num;
	uint8_t		pno_enabled;
	uint8_t		pad2[3];
	uint8_t		sparse_channel_valid_num;
	uint8_t		pad3[3];
	uint8_t		channel_num[MT76_HW_SCAN_DONE_MAX_CHANNEL_NUM];
	/* idle format for channel_idle_time
	 * 0: first bytes: idle time(ms) 2nd byte: dwell time(ms)
	 * 1: first bytes: idle time(8ms) 2nd byte: dwell time(8ms)
	 * 2: dwell time (16us)
	 */
	uint16_t	channel_idle_time[MT76_HW_SCAN_DONE_MAX_CHANNEL_NUM];
	/* beacon and probe response count */
	uint8_t		beacon_probe_num[MT76_HW_SCAN_DONE_MAX_CHANNEL_NUM];
	uint8_t		mdrdy_count[MT76_HW_SCAN_DONE_MAX_CHANNEL_NUM];
	uint32_t	beacon_2g_num;
	uint32_t	beacon_5g_num;
} __packed;

struct mt7921_patch_hdr {
	char		build_date[16];
	char		platform[4];
	uint32_t	hw_sw_ver;
	uint32_t	patch_ver;
	uint16_t	checksum;
	uint16_t	reserved;
	struct {
		uint32_t	patch_ver;
		uint32_t	subsys;
		uint32_t	feature;
		uint32_t	n_region;
		uint32_t	crc;
		uint32_t	reserved[11];
	} desc;
} __packed;

struct mt7921_patch_sec {
	uint32_t	type;
	uint32_t	offs;
	uint32_t	size;
	union {
		uint32_t		spec[13];
		struct {
			uint32_t	addr;
			uint32_t	len;
			uint32_t	sec_key_idx;
			uint32_t	align_len;
			uint32_t	reserved[9];
		} info;
	};
} __packed;

struct mt7921_fw_trailer {
	uint8_t		chip_id;
	uint8_t		eco_code;
	uint8_t		n_region;
	uint8_t		format_ver;
	uint8_t		format_flag;
	uint8_t		reserved[2];
	char		fw_ver[10];
	char		build_date[15];
	uint32_t	crc;
} __packed;

struct mt7921_fw_region {
	uint32_t	decomp_crc;
	uint32_t	decomp_len;
	uint32_t	decomp_blk_sz;
	uint8_t		reserved[4];
	uint32_t	addr;
	uint32_t	len;
	uint8_t		feature_set;
	uint8_t		reserved1[15];
} __packed;

/* STA_REC HEADER */
struct sta_req_hdr {
	uint8_t		bss_idx;
	uint8_t		wlan_idx_lo;
	uint16_t	tlv_num;
	uint8_t		is_tlv_append;
	uint8_t		muar_idx;
	uint8_t		wlan_idx_hi;
	uint8_t		rsv;
} __packed;

#define	STA_REC_BASIC			0x00
struct sta_rec_basic {
	uint16_t	tag;
	uint16_t	len;
	uint32_t	conn_type;
	uint8_t		conn_state;
	uint8_t		qos;
	uint16_t	aid;
	uint8_t		peer_addr[IEEE80211_ADDR_LEN];
#define	EXTRA_INFO_VER	(1U << 0)
#define	EXTRA_INFO_NEW	(1U << 1)
	uint16_t	extra_info;
} __packed;

#define	STA_REC_RA			0x01
#define	HT_MCS_MASK_NUM 10
struct sta_rec_ra_info {
	uint16_t	tag;
	uint16_t	len;
	uint16_t	legacy;
	uint8_t		rx_mcs_bitmask[HT_MCS_MASK_NUM];
} __packed;
#define	RA_LEGACY_OFDM	0x3fc0
#define	RA_LEGACY_CCK	0x000f

#define	STA_REC_STATE			0x07
struct sta_rec_state {
	uint16_t	tag;
	uint16_t	len;
	uint32_t	flags;
	uint8_t		state;
	uint8_t		vht_opmode;
	uint8_t		action;
	uint8_t		rsv[1];
} __packed;

#define	STA_REC_WTBL			0x0d
struct sta_rec_wtbl {
	uint16_t	tag;
	uint16_t	len;
	uint8_t		wlan_idx_lo;
	uint8_t		operation;
	uint16_t	tlv_num;
	uint8_t		wlan_idx_hi;
	uint8_t		rsv[3];
} __packed;

#define	STA_REC_PHY			0x15
struct sta_rec_phy {
	uint16_t	tag;
	uint16_t	len;
	uint16_t	basic_rate;
	uint8_t		phy_type;
	uint8_t		ampdu;
	uint8_t		rts_policy;
	uint8_t		rcpi;
	uint8_t		rsv[2];
} __packed;

/* WTBL REC */
#define	WTBL_RESET_AND_SET	1
#define	WTBL_SET		2
#define	WTBL_QUERY		3
#define	WTBL_RESET_ALL		4


#define	WTBL_GENERIC			0x00
struct wtbl_generic {
	uint16_t	tag;
	uint16_t	len;
	uint8_t		peer_addr[IEEE80211_ADDR_LEN];
	uint8_t		muar_idx;
	uint8_t		skip_tx;
	uint8_t		cf_ack;
	uint8_t		qos;
	uint8_t		mesh;
	uint8_t		adm;
	uint16_t	partial_aid;
	uint8_t		baf_en;
	uint8_t		aad_om;
} __packed;

#define	WTBL_RX				0x01
struct wtbl_rx {
	uint16_t	tag;
	uint16_t	len;
	uint8_t		rcid;
	uint8_t		rca1;
	uint8_t		rca2;
	uint8_t		rv;
	uint8_t		rsv[4];
} __packed;

#define	WTBL_HDR_TRANS			0x06
struct wtbl_hdr_trans {
	uint16_t	tag;
	uint16_t	len;
	uint8_t		to_ds;
	uint8_t		from_ds;
	uint8_t		no_rx_trans;
	uint8_t		rsv;
} __packed;

#define	WTBL_SMPS			0x0d
struct wtbl_smps {
	uint16_t	tag;
	uint16_t	len;
	uint8_t		smps;
	uint8_t		rsv[3];
} __packed;



#define	FW_START_OVERRIDE		0x01
#define	FW_START_WORKING_PDA_CR4	0x02

#define	FW_FEATURE_SET_ENCRYPT		0x1
#define	FW_FEATURE_SET_KEY_IDX_MASK	0x06
#define	FW_FEATURE_ENCRY_MODE		0x10
#define	FW_FEATURE_OVERRIDE_ADDR	0x20

#define	PATCH_SEC_NOT_SUPPORT		0xffffffff
#define	PATCH_SEC_TYPE_MASK		0x0000ffff
#define	PATCH_SEC_TYPE_INFO		0x2

#define	PATCH_SEC_ENC_TYPE_MASK		0xff000000
#define	PATCH_SEC_ENC_TYPE_SHIFT	24
#define	PATCH_SEC_ENC_TYPE_PLAIN	(0x00 << PATCH_SEC_ENC_TYPE_SHIFT)
#define	PATCH_SEC_ENC_TYPE_AES		(0x01 << PATCH_SEC_ENC_TYPE_SHIFT)
#define	PATCH_SEC_ENC_TYPE_SCRAMBLE	(0x02 << PATCH_SEC_ENC_TYPE_SHIFT)
#define	PATCH_SEC_ENC_SCRAMBLE_INFO_MASK	0xffff
#define	PATCH_SEC_ENC_AES_KEY_MASK		0xff

#define	DL_MODE_ENCRYPT			0x01
#define	DL_MODE_KEY_IDX_MASK		0x06
#define	DL_MODE_KEY_IDX_SHIFT		1
#define	DL_MODE_RESET_SEC_IV		0x08
#define	DL_MODE_WORKING_PDA_CR4		0x10
#define	DL_CONFIG_ENCRY_MODE_SEL	0x40
#define	DL_MODE_NEED_RSP		0x80000000

/* defines for mt7921_mcu_get_nic_capability */
enum {
	MT_NIC_CAP_TX_RESOURCE,
	MT_NIC_CAP_TX_EFUSE_ADDR,
	MT_NIC_CAP_COEX,
	MT_NIC_CAP_SINGLE_SKU,
	MT_NIC_CAP_CSUM_OFFLOAD,
	MT_NIC_CAP_HW_VER,
	MT_NIC_CAP_SW_VER,
	MT_NIC_CAP_MAC_ADDR,
	MT_NIC_CAP_PHY,
	MT_NIC_CAP_MAC,
	MT_NIC_CAP_FRAME_BUF,
	MT_NIC_CAP_BEAM_FORM,
	MT_NIC_CAP_LOCATION,
	MT_NIC_CAP_MUMIMO,
	MT_NIC_CAP_BUFFER_MODE_INFO,
	MT_NIC_CAP_HW_ADIE_VERSION = 0x14,
	MT_NIC_CAP_ANTSWP = 0x16,
	MT_NIC_CAP_WFDMA_REALLOC,
	MT_NIC_CAP_6G,
};

/* defines for channel bandwidth */
enum {
	CMD_CBW_20MHZ,
	CMD_CBW_40MHZ,
	CMD_CBW_80MHZ,
	CMD_CBW_160MHZ,
	CMD_CBW_10MHZ,
	CMD_CBW_5MHZ,
	CMD_CBW_8080MHZ,
};

/* defines for channel switch reason */
enum {
	CH_SWITCH_NORMAL = 0,
	CH_SWITCH_SCAN = 3,
	CH_SWITCH_MCC = 4,
	CH_SWITCH_DFS = 5,
	CH_SWITCH_BACKGROUND_SCAN_START = 6,
	CH_SWITCH_BACKGROUND_SCAN_RUNNING = 7,
	CH_SWITCH_BACKGROUND_SCAN_STOP = 8,
	CH_SWITCH_SCAN_BYPASS_DPD = 9
};

enum {
	HW_BSSID_0 = 0x0,
	HW_BSSID_1,
	HW_BSSID_2,
	HW_BSSID_3,
	HW_BSSID_MAX = HW_BSSID_3,
	EXT_BSSID_START = 0x10,
	EXT_BSSID_1,
	EXT_BSSID_15 = 0x1f,
	EXT_BSSID_MAX = EXT_BSSID_15,
	REPEATER_BSSID_START = 0x20,
	REPEATER_BSSID_MAX = 0x3f,
};

enum mt76_phy_type {
	MT_PHY_TYPE_CCK,
	MT_PHY_TYPE_OFDM,
	MT_PHY_TYPE_HT,
	MT_PHY_TYPE_HT_GF,
	MT_PHY_TYPE_VHT,
	MT_PHY_TYPE_HE_SU = 8,
	MT_PHY_TYPE_HE_EXT_SU,
	MT_PHY_TYPE_HE_TB,
	MT_PHY_TYPE_HE_MU,
	__MT_PHY_TYPE_HE_MAX,
};


#define	STA_TYPE_STA			(1U << 0)
#define	STA_TYPE_AP			(1U << 1)
#define	STA_TYPE_ADHOC			(1U << 2)
#define	STA_TYPE_WDS			(1U << 4)
#define	STA_TYPE_BC			(1U << 5)

#define	NETWORK_INFRA			(1U << 16)
#define	NETWORK_P2P			(1U << 17)
#define	NETWORK_IBSS			(1U << 18)
#define	NETWORK_WDS			(1U << 21)

#define	CONN_STATE_DISCONNECT		0
#define	CONN_STATE_CONNECT		1
#define	CONN_STATE_PORT_SECURE		2

#define	DEV_INFO_ACTIVE			0

#define	PHY_TYPE_BIT_HR_DSSS		(1U << 0)
#define	PHY_TYPE_BIT_ERP		(1U << 1)
#define	PHY_TYPE_BIT_OFDM		(1U << 3)
#define	PHY_TYPE_BIT_HT			(1U << 4)
#define	PHY_TYPE_BIT_VHT		(1U << 5)
#define	PHY_TYPE_BIT_HE			(1U << 6)

#define	rssi_to_rcpi(rssi)		(2 * (rssi) + 220)
#define	rcpi_to_rssi(field, rxv)	((FIELD_GET(field, rxv) - 220) / 2)
