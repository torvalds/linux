/*
 * Copyright (C) 2014 Texas Instruments Incorporated
 * Authors:	Sandeep Nair <sandeep_n@ti.com
 *		Cyril Chemparathy <cyril@ti.com
		Santosh Shilimkar <santosh.shilimkar@ti.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __SOC_TI_KEYSTONE_NAVIGATOR_DMA_H__
#define __SOC_TI_KEYSTONE_NAVIGATOR_DMA_H__

/*
 * PKTDMA descriptor manipulation macros for host packet descriptor
 */
#define MASK(x)					(BIT(x) - 1)
#define KNAV_DMA_DESC_PKT_LEN_MASK		MASK(22)
#define KNAV_DMA_DESC_PKT_LEN_SHIFT		0
#define KNAV_DMA_DESC_PS_INFO_IN_SOP		BIT(22)
#define KNAV_DMA_DESC_PS_INFO_IN_DESC		0
#define KNAV_DMA_DESC_TAG_MASK			MASK(8)
#define KNAV_DMA_DESC_SAG_HI_SHIFT		24
#define KNAV_DMA_DESC_STAG_LO_SHIFT		16
#define KNAV_DMA_DESC_DTAG_HI_SHIFT		8
#define KNAV_DMA_DESC_DTAG_LO_SHIFT		0
#define KNAV_DMA_DESC_HAS_EPIB			BIT(31)
#define KNAV_DMA_DESC_NO_EPIB			0
#define KNAV_DMA_DESC_PSLEN_SHIFT		24
#define KNAV_DMA_DESC_PSLEN_MASK		MASK(6)
#define KNAV_DMA_DESC_ERR_FLAG_SHIFT		20
#define KNAV_DMA_DESC_ERR_FLAG_MASK		MASK(4)
#define KNAV_DMA_DESC_PSFLAG_SHIFT		16
#define KNAV_DMA_DESC_PSFLAG_MASK		MASK(4)
#define KNAV_DMA_DESC_RETQ_SHIFT		0
#define KNAV_DMA_DESC_RETQ_MASK			MASK(14)
#define KNAV_DMA_DESC_BUF_LEN_MASK		MASK(22)
#define KNAV_DMA_DESC_EFLAGS_MASK		MASK(4)
#define KNAV_DMA_DESC_EFLAGS_SHIFT		20

#define KNAV_DMA_NUM_EPIB_WORDS			4
#define KNAV_DMA_NUM_PS_WORDS			16
#define KNAV_DMA_NUM_SW_DATA_WORDS		4
#define KNAV_DMA_FDQ_PER_CHAN			4

/* Tx channel scheduling priority */
enum knav_dma_tx_priority {
	DMA_PRIO_HIGH	= 0,
	DMA_PRIO_MED_H,
	DMA_PRIO_MED_L,
	DMA_PRIO_LOW
};

/* Rx channel error handling mode during buffer starvation */
enum knav_dma_rx_err_mode {
	DMA_DROP = 0,
	DMA_RETRY
};

/* Rx flow size threshold configuration */
enum knav_dma_rx_thresholds {
	DMA_THRESH_NONE		= 0,
	DMA_THRESH_0		= 1,
	DMA_THRESH_0_1		= 3,
	DMA_THRESH_0_1_2	= 7
};

/* Descriptor type */
enum knav_dma_desc_type {
	DMA_DESC_HOST = 0,
	DMA_DESC_MONOLITHIC = 2
};

/**
 * struct knav_dma_tx_cfg:	Tx channel configuration
 * @filt_einfo:			Filter extended packet info
 * @filt_pswords:		Filter PS words present
 * @knav_dma_tx_priority:	Tx channel scheduling priority
 */
struct knav_dma_tx_cfg {
	bool				filt_einfo;
	bool				filt_pswords;
	enum knav_dma_tx_priority	priority;
};

/**
 * struct knav_dma_rx_cfg:	Rx flow configuration
 * @einfo_present:		Extended packet info present
 * @psinfo_present:		PS words present
 * @knav_dma_rx_err_mode:	Error during buffer starvation
 * @knav_dma_desc_type:	Host or Monolithic desc
 * @psinfo_at_sop:		PS word located at start of packet
 * @sop_offset:			Start of packet offset
 * @dst_q:			Destination queue for a given flow
 * @thresh:			Rx flow size threshold
 * @fdq[]:			Free desc Queue array
 * @sz_thresh0:			RX packet size threshold 0
 * @sz_thresh1:			RX packet size threshold 1
 * @sz_thresh2:			RX packet size threshold 2
 */
struct knav_dma_rx_cfg {
	bool				einfo_present;
	bool				psinfo_present;
	enum knav_dma_rx_err_mode	err_mode;
	enum knav_dma_desc_type		desc_type;
	bool				psinfo_at_sop;
	unsigned int			sop_offset;
	unsigned int			dst_q;
	enum knav_dma_rx_thresholds	thresh;
	unsigned int			fdq[KNAV_DMA_FDQ_PER_CHAN];
	unsigned int			sz_thresh0;
	unsigned int			sz_thresh1;
	unsigned int			sz_thresh2;
};

/**
 * struct knav_dma_cfg:	Pktdma channel configuration
 * @sl_cfg:			Slave configuration
 * @tx:				Tx channel configuration
 * @rx:				Rx flow configuration
 */
struct knav_dma_cfg {
	enum dma_transfer_direction direction;
	union {
		struct knav_dma_tx_cfg	tx;
		struct knav_dma_rx_cfg	rx;
	} u;
};

/**
 * struct knav_dma_desc:	Host packet descriptor layout
 * @desc_info:			Descriptor information like id, type, length
 * @tag_info:			Flow tag info written in during RX
 * @packet_info:		Queue Manager, policy, flags etc
 * @buff_len:			Buffer length in bytes
 * @buff:			Buffer pointer
 * @next_desc:			For chaining the descriptors
 * @orig_len:			length since 'buff_len' can be overwritten
 * @orig_buff:			buff pointer since 'buff' can be overwritten
 * @epib:			Extended packet info block
 * @psdata:			Protocol specific
 * @sw_data:			Software private data not touched by h/w
 */
struct knav_dma_desc {
	__le32	desc_info;
	__le32	tag_info;
	__le32	packet_info;
	__le32	buff_len;
	__le32	buff;
	__le32	next_desc;
	__le32	orig_len;
	__le32	orig_buff;
	__le32	epib[KNAV_DMA_NUM_EPIB_WORDS];
	__le32	psdata[KNAV_DMA_NUM_PS_WORDS];
	u32	sw_data[KNAV_DMA_NUM_SW_DATA_WORDS];
} ____cacheline_aligned;

#if IS_ENABLED(CONFIG_KEYSTONE_NAVIGATOR_DMA)
void *knav_dma_open_channel(struct device *dev, const char *name,
				struct knav_dma_cfg *config);
void knav_dma_close_channel(void *channel);
#else
static inline void *knav_dma_open_channel(struct device *dev, const char *name,
				struct knav_dma_cfg *config)
{
	return (void *) NULL;
}
static inline void knav_dma_close_channel(void *channel)
{}

#endif

#endif /* __SOC_TI_KEYSTONE_NAVIGATOR_DMA_H__ */
