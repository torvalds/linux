/*
 * AMD 10Gb Ethernet driver
 *
 * This file is available to you under your choice of the following two
 * licenses:
 *
 * License 1: GPLv2
 *
 * Copyright (c) 2014-2016 Advanced Micro Devices, Inc.
 *
 * This file is free software; you may copy, redistribute and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or (at
 * your option) any later version.
 *
 * This file is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *     The Synopsys DWC ETHER XGMAC Software Driver and documentation
 *     (hereinafter "Software") is an unsupported proprietary work of Synopsys,
 *     Inc. unless otherwise expressly agreed to in writing between Synopsys
 *     and you.
 *
 *     The Software IS NOT an item of Licensed Software or Licensed Product
 *     under any End User Software License Agreement or Agreement for Licensed
 *     Product with Synopsys or any supplement thereto.  Permission is hereby
 *     granted, free of charge, to any person obtaining a copy of this software
 *     annotated with this license and the Software, to deal in the Software
 *     without restriction, including without limitation the rights to use,
 *     copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 *     of the Software, and to permit persons to whom the Software is furnished
 *     to do so, subject to the following conditions:
 *
 *     The above copyright notice and this permission notice shall be included
 *     in all copies or substantial portions of the Software.
 *
 *     THIS SOFTWARE IS BEING DISTRIBUTED BY SYNOPSYS SOLELY ON AN "AS IS"
 *     BASIS AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 *     TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 *     PARTICULAR PURPOSE ARE HEREBY DISCLAIMED. IN NO EVENT SHALL SYNOPSYS
 *     BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *     CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *     SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *     INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *     CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *     ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 *     THE POSSIBILITY OF SUCH DAMAGE.
 *
 *
 * License 2: Modified BSD
 *
 * Copyright (c) 2014-2016 Advanced Micro Devices, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Advanced Micro Devices, Inc. nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *     The Synopsys DWC ETHER XGMAC Software Driver and documentation
 *     (hereinafter "Software") is an unsupported proprietary work of Synopsys,
 *     Inc. unless otherwise expressly agreed to in writing between Synopsys
 *     and you.
 *
 *     The Software IS NOT an item of Licensed Software or Licensed Product
 *     under any End User Software License Agreement or Agreement for Licensed
 *     Product with Synopsys or any supplement thereto.  Permission is hereby
 *     granted, free of charge, to any person obtaining a copy of this software
 *     annotated with this license and the Software, to deal in the Software
 *     without restriction, including without limitation the rights to use,
 *     copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 *     of the Software, and to permit persons to whom the Software is furnished
 *     to do so, subject to the following conditions:
 *
 *     The above copyright notice and this permission notice shall be included
 *     in all copies or substantial portions of the Software.
 *
 *     THIS SOFTWARE IS BEING DISTRIBUTED BY SYNOPSYS SOLELY ON AN "AS IS"
 *     BASIS AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 *     TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 *     PARTICULAR PURPOSE ARE HEREBY DISCLAIMED. IN NO EVENT SHALL SYNOPSYS
 *     BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *     CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *     SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *     INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *     CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *     ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 *     THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef __XGBE_H__
#define __XGBE_H__

#include "xgbe_osdep.h"

/* From linux/dcbnl.h */
#define IEEE_8021QAZ_MAX_TCS	8

#define XGBE_DRV_NAME		"amd-xgbe"
#define XGBE_DRV_VERSION	"1.0.2"
#define XGBE_DRV_DESC		"AMD 10 Gigabit Ethernet Driver"

/* Descriptor related defines */
#define XGBE_TX_DESC_CNT	512
#define XGBE_TX_DESC_MIN_FREE	(XGBE_TX_DESC_CNT >> 3)
#define XGBE_TX_DESC_MAX_PROC	(XGBE_TX_DESC_CNT >> 1)
#define XGBE_RX_DESC_CNT	512

#define XGBE_TX_MAX_BUF_SIZE	(0x3fff & ~(64 - 1))

/* Descriptors required for maximum contiguous TSO/GSO packet */
#define XGBE_TX_MAX_SPLIT	((GSO_MAX_SIZE / XGBE_TX_MAX_BUF_SIZE) + 1)

/* Maximum possible descriptors needed for an SKB:
 * - Maximum number of SKB frags
 * - Maximum descriptors for contiguous TSO/GSO packet
 * - Possible context descriptor
 * - Possible TSO header descriptor
 */
#define XGBE_TX_MAX_DESCS	(MAX_SKB_FRAGS + XGBE_TX_MAX_SPLIT + 2)

#define XGBE_RX_MIN_BUF_SIZE	1522
#define XGBE_RX_BUF_ALIGN	64
#define XGBE_SKB_ALLOC_SIZE	256
#define XGBE_SPH_HDSMS_SIZE	2	/* Keep in sync with SKB_ALLOC_SIZE */

#define XGBE_MAX_DMA_CHANNELS	16
#define XGBE_MAX_QUEUES		16
#define XGBE_DMA_STOP_TIMEOUT	5

/* DMA cache settings - Outer sharable, write-back, write-allocate */
#define XGBE_DMA_OS_AXDOMAIN	0x2
#define XGBE_DMA_OS_ARCACHE	0xb
#define XGBE_DMA_OS_AWCACHE	0xf

/* DMA cache settings - System, no caches used */
#define XGBE_DMA_SYS_AXDOMAIN	0x3
#define XGBE_DMA_SYS_ARCACHE	0x0
#define XGBE_DMA_SYS_AWCACHE	0x0

#define XGBE_DMA_INTERRUPT_MASK	0x31c7

#define XGMAC_MIN_PACKET	60
#define XGMAC_STD_PACKET_MTU	1500
#define XGMAC_MAX_STD_PACKET	1518
#define XGMAC_JUMBO_PACKET_MTU	9000
#define XGMAC_MAX_JUMBO_PACKET	9018

/* Common property names */
#define XGBE_MAC_ADDR_PROPERTY	"mac-address"
#define XGBE_PHY_MODE_PROPERTY	"phy-mode"
#define XGBE_DMA_IRQS_PROPERTY	"amd,per-channel-interrupt"
#define XGBE_SPEEDSET_PROPERTY	"amd,speed-set"
#define XGBE_BLWC_PROPERTY	"amd,serdes-blwc"
#define XGBE_CDR_RATE_PROPERTY	"amd,serdes-cdr-rate"
#define XGBE_PQ_SKEW_PROPERTY	"amd,serdes-pq-skew"
#define XGBE_TX_AMP_PROPERTY	"amd,serdes-tx-amp"
#define XGBE_DFE_CFG_PROPERTY	"amd,serdes-dfe-tap-config"
#define XGBE_DFE_ENA_PROPERTY	"amd,serdes-dfe-tap-enable"

/* Device-tree clock names */
#define XGBE_DMA_CLOCK		"dma_clk"
#define XGBE_PTP_CLOCK		"ptp_clk"

/* ACPI property names */
#define XGBE_ACPI_DMA_FREQ	"amd,dma-freq"
#define XGBE_ACPI_PTP_FREQ	"amd,ptp-freq"

/* Timestamp support - values based on 50MHz PTP clock
 *   50MHz => 20 nsec
 */
#define XGBE_TSTAMP_SSINC	20
#define XGBE_TSTAMP_SNSINC	0

/* Driver PMT macros */
#define XGMAC_DRIVER_CONTEXT	1
#define XGMAC_IOCTL_CONTEXT	2

#define XGBE_FIFO_MAX		81920

#define XGBE_TC_MIN_QUANTUM	10

/* Helper macro for descriptor handling
 *  Always use XGBE_GET_DESC_DATA to access the descriptor data
 *  since the index is free-running and needs to be and-ed
 *  with the descriptor count value of the ring to index to
 *  the proper descriptor data.
 */
#define XGBE_GET_DESC_DATA(_ring, _idx)				\
	((_ring)->rdata +					\
	 ((_idx) & ((_ring)->rdesc_count - 1)))

/* Default coalescing parameters */
#define XGMAC_INIT_DMA_TX_USECS		1000
#define XGMAC_INIT_DMA_TX_FRAMES	25

#define XGMAC_MAX_DMA_RIWT		0xff
#define XGMAC_INIT_DMA_RX_USECS		30
#define XGMAC_INIT_DMA_RX_FRAMES	25

/* Flow control queue count */
#define XGMAC_MAX_FLOW_CONTROL_QUEUES	8

/* Maximum MAC address hash table size (256 bits = 8 bytes) */
#define XGBE_MAC_HASH_TABLE_SIZE	8

/* Receive Side Scaling */
#define XGBE_RSS_HASH_KEY_SIZE		40
#define XGBE_RSS_MAX_TABLE_SIZE		256
#define XGBE_RSS_LOOKUP_TABLE_TYPE	0
#define XGBE_RSS_HASH_KEY_TYPE		1

/* Auto-negotiation */
#define XGBE_AN_MS_TIMEOUT		500
#define XGBE_LINK_TIMEOUT		10

#define XGBE_AN_INT_CMPLT		0x01
#define XGBE_AN_INC_LINK		0x02
#define XGBE_AN_PG_RCV			0x04
#define XGBE_AN_INT_MASK		0x07

/* Rate-change complete wait/retry count */
#define XGBE_RATECHANGE_COUNT		500

/* Default SerDes settings */
#define XGBE_SPEED_10000_BLWC		0
#define XGBE_SPEED_10000_CDR		0x7
#define XGBE_SPEED_10000_PLL		0x1
#define XGBE_SPEED_10000_PQ		0x12
#define XGBE_SPEED_10000_RATE		0x0
#define XGBE_SPEED_10000_TXAMP		0xa
#define XGBE_SPEED_10000_WORD		0x7
#define XGBE_SPEED_10000_DFE_TAP_CONFIG	0x1
#define XGBE_SPEED_10000_DFE_TAP_ENABLE	0x7f

#define XGBE_SPEED_2500_BLWC		1
#define XGBE_SPEED_2500_CDR		0x2
#define XGBE_SPEED_2500_PLL		0x0
#define XGBE_SPEED_2500_PQ		0xa
#define XGBE_SPEED_2500_RATE		0x1
#define XGBE_SPEED_2500_TXAMP		0xf
#define XGBE_SPEED_2500_WORD		0x1
#define XGBE_SPEED_2500_DFE_TAP_CONFIG	0x3
#define XGBE_SPEED_2500_DFE_TAP_ENABLE	0x0

#define XGBE_SPEED_1000_BLWC		1
#define XGBE_SPEED_1000_CDR		0x2
#define XGBE_SPEED_1000_PLL		0x0
#define XGBE_SPEED_1000_PQ		0xa
#define XGBE_SPEED_1000_RATE		0x3
#define XGBE_SPEED_1000_TXAMP		0xf
#define XGBE_SPEED_1000_WORD		0x1
#define XGBE_SPEED_1000_DFE_TAP_CONFIG	0x3
#define XGBE_SPEED_1000_DFE_TAP_ENABLE	0x0

struct xgbe_prv_data;

struct xgbe_packet_data {
	struct mbuf *m;

	unsigned int attributes;

	unsigned int errors;

	unsigned int rdesc_count;
	unsigned int length;

	u64 rx_tstamp;

	unsigned int tx_packets;
	unsigned int tx_bytes;
};

/* Common Rx and Tx descriptor mapping */
struct xgbe_ring_desc {
	__le32 desc0;
	__le32 desc1;
	__le32 desc2;
	__le32 desc3;
};

/* Tx-related ring data */
struct xgbe_tx_ring_data {
	unsigned int packets;		/* BQL packet count */
	unsigned int bytes;		/* BQL byte count */
};

/* Rx-related ring data */
struct xgbe_rx_ring_data {
	unsigned short hdr_len;		/* Length of received header */
	unsigned short len;		/* Length of received packet */
};

/* Structure used to hold information related to the descriptor
 * and the packet associated with the descriptor (always use
 * use the XGBE_GET_DESC_DATA macro to access this data from the ring)
 */
struct xgbe_ring_data {
	struct xgbe_ring_desc *rdesc;	/* Virtual address of descriptor */
	bus_addr_t rdata_paddr;

	bus_dma_tag_t mbuf_dmat;
	bus_dmamap_t mbuf_map;
	bus_addr_t mbuf_hdr_paddr;
	bus_addr_t mbuf_data_paddr;
	bus_size_t mbuf_len;

	int mbuf_free;
	struct mbuf *mb;

	struct xgbe_tx_ring_data tx;	/* Tx-related data */
	struct xgbe_rx_ring_data rx;	/* Rx-related data */
};

struct xgbe_ring {
	/* Ring lock - used just for TX rings at the moment */
	spinlock_t lock;

	/* Per packet related information */
	struct xgbe_packet_data packet_data;

	/* Virtual/DMA addresses and count of allocated descriptor memory */
	struct xgbe_ring_desc *rdesc;
	bus_dmamap_t rdesc_map;
	bus_dma_tag_t rdesc_dmat;
	bus_addr_t rdesc_paddr;
	unsigned int rdesc_count;

	bus_dma_tag_t mbuf_dmat;
	bus_dmamap_t mbuf_map;

	/* Array of descriptor data corresponding the descriptor memory
	 * (always use the XGBE_GET_DESC_DATA macro to access this data)
	 */
	struct xgbe_ring_data *rdata;

	/* Ring index values
	 *  cur   - Tx: index of descriptor to be used for current transfer
	 *          Rx: index of descriptor to check for packet availability
	 *  dirty - Tx: index of descriptor to check for transfer complete
	 *          Rx: index of descriptor to check for buffer reallocation
	 */
	unsigned int cur;
	unsigned int dirty;

	/* Coalesce frame count used for interrupt bit setting */
	unsigned int coalesce_count;

	union {
		struct {
			unsigned int queue_stopped;
			unsigned int xmit_more;
			unsigned short cur_mss;
			unsigned short cur_vlan_ctag;
		} tx;
	};
} __aligned(CACHE_LINE_SIZE);

/* Structure used to describe the descriptor rings associated with
 * a DMA channel.
 */
struct xgbe_channel {
	char name[16];

	/* Address of private data area for device */
	struct xgbe_prv_data *pdata;

	/* Queue index and base address of queue's DMA registers */
	unsigned int queue_index;
	bus_space_tag_t dma_tag;
	bus_space_handle_t dma_handle;

	/* Per channel interrupt irq number */
	struct resource *dma_irq_res;
	void *dma_irq_tag;

	unsigned int saved_ier;

	struct xgbe_ring *tx_ring;
	struct xgbe_ring *rx_ring;
} __aligned(CACHE_LINE_SIZE);

enum xgbe_state {
	XGBE_DOWN,
	XGBE_LINK_INIT,
	XGBE_LINK_ERR,
};

enum xgbe_int {
	XGMAC_INT_DMA_CH_SR_TI,
	XGMAC_INT_DMA_CH_SR_TPS,
	XGMAC_INT_DMA_CH_SR_TBU,
	XGMAC_INT_DMA_CH_SR_RI,
	XGMAC_INT_DMA_CH_SR_RBU,
	XGMAC_INT_DMA_CH_SR_RPS,
	XGMAC_INT_DMA_CH_SR_TI_RI,
	XGMAC_INT_DMA_CH_SR_FBE,
	XGMAC_INT_DMA_ALL,
};

enum xgbe_int_state {
	XGMAC_INT_STATE_SAVE,
	XGMAC_INT_STATE_RESTORE,
};

enum xgbe_speed {
	XGBE_SPEED_1000 = 0,
	XGBE_SPEED_2500,
	XGBE_SPEED_10000,
	XGBE_SPEEDS,
};

enum xgbe_an {
	XGBE_AN_READY = 0,
	XGBE_AN_PAGE_RECEIVED,
	XGBE_AN_INCOMPAT_LINK,
	XGBE_AN_COMPLETE,
	XGBE_AN_NO_LINK,
	XGBE_AN_ERROR,
};

enum xgbe_rx {
	XGBE_RX_BPA = 0,
	XGBE_RX_XNP,
	XGBE_RX_COMPLETE,
	XGBE_RX_ERROR,
};

enum xgbe_mode {
	XGBE_MODE_KR = 0,
	XGBE_MODE_KX,
};

enum xgbe_speedset {
	XGBE_SPEEDSET_1000_10000 = 0,
	XGBE_SPEEDSET_2500_10000,
};

struct xgbe_phy {
	u32 supported;
	u32 advertising;
	u32 lp_advertising;

	int address;

	int autoneg;
	int speed;
	int duplex;

	int link;

	int pause_autoneg;
	int tx_pause;
	int rx_pause;
};

struct xgbe_mmc_stats {
	/* Tx Stats */
	u64 txoctetcount_gb;
	u64 txframecount_gb;
	u64 txbroadcastframes_g;
	u64 txmulticastframes_g;
	u64 tx64octets_gb;
	u64 tx65to127octets_gb;
	u64 tx128to255octets_gb;
	u64 tx256to511octets_gb;
	u64 tx512to1023octets_gb;
	u64 tx1024tomaxoctets_gb;
	u64 txunicastframes_gb;
	u64 txmulticastframes_gb;
	u64 txbroadcastframes_gb;
	u64 txunderflowerror;
	u64 txoctetcount_g;
	u64 txframecount_g;
	u64 txpauseframes;
	u64 txvlanframes_g;

	/* Rx Stats */
	u64 rxframecount_gb;
	u64 rxoctetcount_gb;
	u64 rxoctetcount_g;
	u64 rxbroadcastframes_g;
	u64 rxmulticastframes_g;
	u64 rxcrcerror;
	u64 rxrunterror;
	u64 rxjabbererror;
	u64 rxundersize_g;
	u64 rxoversize_g;
	u64 rx64octets_gb;
	u64 rx65to127octets_gb;
	u64 rx128to255octets_gb;
	u64 rx256to511octets_gb;
	u64 rx512to1023octets_gb;
	u64 rx1024tomaxoctets_gb;
	u64 rxunicastframes_g;
	u64 rxlengtherror;
	u64 rxoutofrangetype;
	u64 rxpauseframes;
	u64 rxfifooverflow;
	u64 rxvlanframes_gb;
	u64 rxwatchdogerror;
};

struct xgbe_ext_stats {
	u64 tx_tso_packets;
	u64 rx_split_header_packets;
	u64 rx_buffer_unavailable;
};

struct xgbe_hw_if {
	int (*tx_complete)(struct xgbe_ring_desc *);

	int (*set_mac_address)(struct xgbe_prv_data *, u8 *addr);
	int (*config_rx_mode)(struct xgbe_prv_data *);

	int (*enable_rx_csum)(struct xgbe_prv_data *);
	int (*disable_rx_csum)(struct xgbe_prv_data *);

	int (*enable_rx_vlan_stripping)(struct xgbe_prv_data *);
	int (*disable_rx_vlan_stripping)(struct xgbe_prv_data *);
	int (*enable_rx_vlan_filtering)(struct xgbe_prv_data *);
	int (*disable_rx_vlan_filtering)(struct xgbe_prv_data *);
	int (*update_vlan_hash_table)(struct xgbe_prv_data *);

	int (*read_mmd_regs)(struct xgbe_prv_data *, int, int);
	void (*write_mmd_regs)(struct xgbe_prv_data *, int, int, int);
	int (*set_gmii_speed)(struct xgbe_prv_data *);
	int (*set_gmii_2500_speed)(struct xgbe_prv_data *);
	int (*set_xgmii_speed)(struct xgbe_prv_data *);

	void (*enable_tx)(struct xgbe_prv_data *);
	void (*disable_tx)(struct xgbe_prv_data *);
	void (*enable_rx)(struct xgbe_prv_data *);
	void (*disable_rx)(struct xgbe_prv_data *);

	void (*powerup_tx)(struct xgbe_prv_data *);
	void (*powerdown_tx)(struct xgbe_prv_data *);
	void (*powerup_rx)(struct xgbe_prv_data *);
	void (*powerdown_rx)(struct xgbe_prv_data *);

	int (*init)(struct xgbe_prv_data *);
	int (*exit)(struct xgbe_prv_data *);

	int (*enable_int)(struct xgbe_channel *, enum xgbe_int);
	int (*disable_int)(struct xgbe_channel *, enum xgbe_int);
	void (*dev_xmit)(struct xgbe_channel *);
	int (*dev_read)(struct xgbe_channel *);
	void (*tx_desc_init)(struct xgbe_channel *);
	void (*rx_desc_init)(struct xgbe_channel *);
	void (*tx_desc_reset)(struct xgbe_ring_data *);
	void (*rx_desc_reset)(struct xgbe_prv_data *, struct xgbe_ring_data *,
			      unsigned int);
	int (*is_last_desc)(struct xgbe_ring_desc *);
	int (*is_context_desc)(struct xgbe_ring_desc *);
	void (*tx_start_xmit)(struct xgbe_channel *, struct xgbe_ring *);

	/* For FLOW ctrl */
	int (*config_tx_flow_control)(struct xgbe_prv_data *);
	int (*config_rx_flow_control)(struct xgbe_prv_data *);

	/* For RX coalescing */
	int (*config_rx_coalesce)(struct xgbe_prv_data *);
	int (*config_tx_coalesce)(struct xgbe_prv_data *);
	unsigned int (*usec_to_riwt)(struct xgbe_prv_data *, unsigned int);
	unsigned int (*riwt_to_usec)(struct xgbe_prv_data *, unsigned int);

	/* For RX and TX threshold config */
	int (*config_rx_threshold)(struct xgbe_prv_data *, unsigned int);
	int (*config_tx_threshold)(struct xgbe_prv_data *, unsigned int);

	/* For RX and TX Store and Forward Mode config */
	int (*config_rsf_mode)(struct xgbe_prv_data *, unsigned int);
	int (*config_tsf_mode)(struct xgbe_prv_data *, unsigned int);

	/* For TX DMA Operate on Second Frame config */
	int (*config_osp_mode)(struct xgbe_prv_data *);

	/* For RX and TX PBL config */
	int (*config_rx_pbl_val)(struct xgbe_prv_data *);
	int (*get_rx_pbl_val)(struct xgbe_prv_data *);
	int (*config_tx_pbl_val)(struct xgbe_prv_data *);
	int (*get_tx_pbl_val)(struct xgbe_prv_data *);
	int (*config_pblx8)(struct xgbe_prv_data *);

	/* For MMC statistics */
	void (*rx_mmc_int)(struct xgbe_prv_data *);
	void (*tx_mmc_int)(struct xgbe_prv_data *);
	void (*read_mmc_stats)(struct xgbe_prv_data *);

	/* For Receive Side Scaling */
	int (*disable_rss)(struct xgbe_prv_data *);
};

struct xgbe_phy_if {
	/* For initial PHY setup */
	void (*phy_init)(struct xgbe_prv_data *);

	/* For PHY support when setting device up/down */
	int (*phy_reset)(struct xgbe_prv_data *);
	int (*phy_start)(struct xgbe_prv_data *);
	void (*phy_stop)(struct xgbe_prv_data *);

	/* For PHY support while device is up */
	void (*phy_status)(struct xgbe_prv_data *);
	int (*phy_config_aneg)(struct xgbe_prv_data *);
};

struct xgbe_desc_if {
	int (*alloc_ring_resources)(struct xgbe_prv_data *);
	void (*free_ring_resources)(struct xgbe_prv_data *);
	int (*map_tx_skb)(struct xgbe_channel *, struct mbuf *);
	int (*map_rx_buffer)(struct xgbe_prv_data *, struct xgbe_ring *,
			     struct xgbe_ring_data *);
	void (*unmap_rdata)(struct xgbe_prv_data *, struct xgbe_ring_data *);
	void (*wrapper_tx_desc_init)(struct xgbe_prv_data *);
	void (*wrapper_rx_desc_init)(struct xgbe_prv_data *);
};

/* This structure contains flags that indicate what hardware features
 * or configurations are present in the device.
 */
struct xgbe_hw_features {
	/* HW Version */
	unsigned int version;

	/* HW Feature Register0 */
	unsigned int gmii;		/* 1000 Mbps support */
	unsigned int vlhash;		/* VLAN Hash Filter */
	unsigned int sma;		/* SMA(MDIO) Interface */
	unsigned int rwk;		/* PMT remote wake-up packet */
	unsigned int mgk;		/* PMT magic packet */
	unsigned int mmc;		/* RMON module */
	unsigned int aoe;		/* ARP Offload */
	unsigned int ts;		/* IEEE 1588-2008 Advanced Timestamp */
	unsigned int eee;		/* Energy Efficient Ethernet */
	unsigned int tx_coe;		/* Tx Checksum Offload */
	unsigned int rx_coe;		/* Rx Checksum Offload */
	unsigned int addn_mac;		/* Additional MAC Addresses */
	unsigned int ts_src;		/* Timestamp Source */
	unsigned int sa_vlan_ins;	/* Source Address or VLAN Insertion */

	/* HW Feature Register1 */
	unsigned int rx_fifo_size;	/* MTL Receive FIFO Size */
	unsigned int tx_fifo_size;	/* MTL Transmit FIFO Size */
	unsigned int adv_ts_hi;		/* Advance Timestamping High Word */
	unsigned int dma_width;		/* DMA width */
	unsigned int dcb;		/* DCB Feature */
	unsigned int sph;		/* Split Header Feature */
	unsigned int tso;		/* TCP Segmentation Offload */
	unsigned int dma_debug;		/* DMA Debug Registers */
	unsigned int rss;		/* Receive Side Scaling */
	unsigned int tc_cnt;		/* Number of Traffic Classes */
	unsigned int hash_table_size;	/* Hash Table Size */
	unsigned int l3l4_filter_num;	/* Number of L3-L4 Filters */

	/* HW Feature Register2 */
	unsigned int rx_q_cnt;		/* Number of MTL Receive Queues */
	unsigned int tx_q_cnt;		/* Number of MTL Transmit Queues */
	unsigned int rx_ch_cnt;		/* Number of DMA Receive Channels */
	unsigned int tx_ch_cnt;		/* Number of DMA Transmit Channels */
	unsigned int pps_out_num;	/* Number of PPS outputs */
	unsigned int aux_snap_num;	/* Number of Aux snapshot inputs */
};

struct xgbe_prv_data {
	struct ifnet *netdev;
	struct platform_device *pdev;
	struct acpi_device *adev;
	device_t dev;

	/* ACPI or DT flag */
	unsigned int use_acpi;

	/* XGMAC/XPCS related mmio registers */
	struct resource *xgmac_res;	/* XGMAC CSRs */
	struct resource *xpcs_res;	/* XPCS MMD registers */
	struct resource *rxtx_res;	/* SerDes Rx/Tx CSRs */
	struct resource *sir0_res;	/* SerDes integration registers (1/2) */
	struct resource *sir1_res;	/* SerDes integration registers (2/2) */

	/* DMA tag */
	bus_dma_tag_t dmat;

	/* XPCS indirect addressing lock */
	spinlock_t xpcs_lock;

	/* Flags representing xgbe_state */
	unsigned long dev_state;

	struct resource *dev_irq_res;
	struct resource *chan_irq_res[4];
	void *dev_irq_tag;
	unsigned int per_channel_irq;

	struct xgbe_hw_if hw_if;
	struct xgbe_phy_if phy_if;
	struct xgbe_desc_if desc_if;

	/* AXI DMA settings */
	unsigned int coherent;
	unsigned int axdomain;
	unsigned int arcache;
	unsigned int awcache;

	/* Service routine support */
	struct taskqueue *dev_workqueue;
	struct task service_work;
	struct callout service_timer;

	/* Rings for Tx/Rx on a DMA channel */
	struct xgbe_channel *channel;
	unsigned int channel_count;
	unsigned int tx_ring_count;
	unsigned int tx_desc_count;
	unsigned int rx_ring_count;
	unsigned int rx_desc_count;

	unsigned int tx_q_count;
	unsigned int rx_q_count;

	/* Tx/Rx common settings */
	unsigned int pblx8;

	/* Tx settings */
	unsigned int tx_sf_mode;
	unsigned int tx_threshold;
	unsigned int tx_pbl;
	unsigned int tx_osp_mode;

	/* Rx settings */
	unsigned int rx_sf_mode;
	unsigned int rx_threshold;
	unsigned int rx_pbl;

	/* Tx coalescing settings */
	unsigned int tx_usecs;
	unsigned int tx_frames;

	/* Rx coalescing settings */
	unsigned int rx_riwt;
	unsigned int rx_usecs;
	unsigned int rx_frames;

	/* Current Rx buffer size */
	unsigned int rx_buf_size;

	/* Flow control settings */
	unsigned int pause_autoneg;
	unsigned int tx_pause;
	unsigned int rx_pause;

	/* Receive Side Scaling settings */
	u8 rss_key[XGBE_RSS_HASH_KEY_SIZE];
	u32 rss_table[XGBE_RSS_MAX_TABLE_SIZE];
	u32 rss_options;

	/* Netdev related settings */
	unsigned char mac_addr[ETH_ALEN];
	struct xgbe_mmc_stats mmc_stats;
	struct xgbe_ext_stats ext_stats;

	/* Device clocks */
	struct clk *sysclk;
	unsigned long sysclk_rate;
	struct clk *ptpclk;
	unsigned long ptpclk_rate;

	/* DCB support */
	unsigned int q2tc_map[XGBE_MAX_QUEUES];
	unsigned int prio2q_map[IEEE_8021QAZ_MAX_TCS];
	u8 num_tcs;

	/* Hardware features of the device */
	struct xgbe_hw_features hw_feat;

	/* Device restart work structure */
	struct task restart_work;

	/* Keeps track of power mode */
	unsigned int power_down;

	/* Network interface message level setting */
	u32 msg_enable;

	/* Current PHY settings */
	int phy_link;
	int phy_speed;

	/* MDIO/PHY related settings */
	struct xgbe_phy phy;
	int mdio_mmd;
	unsigned long link_check;

	char an_name[IFNAMSIZ + 32];

	struct resource *an_irq_res;
	void *an_irq_tag;

	unsigned int speed_set;

	/* SerDes UEFI configurable settings.
	 *   Switching between modes/speeds requires new values for some
	 *   SerDes settings.  The values can be supplied as device
	 *   properties in array format.  The first array entry is for
	 *   1GbE, second for 2.5GbE and third for 10GbE
	 */
	u32 serdes_blwc[XGBE_SPEEDS];
	u32 serdes_cdr_rate[XGBE_SPEEDS];
	u32 serdes_pq_skew[XGBE_SPEEDS];
	u32 serdes_tx_amp[XGBE_SPEEDS];
	u32 serdes_dfe_tap_cfg[XGBE_SPEEDS];
	u32 serdes_dfe_tap_ena[XGBE_SPEEDS];

	/* Auto-negotiation state machine support */
	unsigned int an_int;
	struct sx an_mutex;
	enum xgbe_an an_result;
	enum xgbe_an an_state;
	enum xgbe_rx kr_state;
	enum xgbe_rx kx_state;
	unsigned int an_supported;
	unsigned int parallel_detect;
	unsigned int fec_ability;
	unsigned long an_start;

	unsigned int lpm_ctrl;		/* CTRL1 for resume */
};

/* Function prototypes*/

int xgbe_open(struct ifnet *);
int xgbe_close(struct ifnet *);
int xgbe_xmit(struct ifnet *, struct mbuf *);
int xgbe_change_mtu(struct ifnet *, int);
void xgbe_init_function_ptrs_dev(struct xgbe_hw_if *);
void xgbe_init_function_ptrs_phy(struct xgbe_phy_if *);
void xgbe_init_function_ptrs_desc(struct xgbe_desc_if *);
void xgbe_get_all_hw_features(struct xgbe_prv_data *);
void xgbe_init_rx_coalesce(struct xgbe_prv_data *);
void xgbe_init_tx_coalesce(struct xgbe_prv_data *);

/* NOTE: Uncomment for function trace log messages in KERNEL LOG */
#if 0
#define YDEBUG
#define YDEBUG_MDIO
#endif

/* For debug prints */
#ifdef YDEBUG
#define DBGPR(x...) printf(x)
#else
#define DBGPR(x...) do { } while (0)
#endif

#ifdef YDEBUG_MDIO
#define DBGPR_MDIO(x...) printf(x)
#else
#define DBGPR_MDIO(x...) do { } while (0)
#endif

#endif
