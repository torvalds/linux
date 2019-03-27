/*-
 * Copyright (c) 2015,2016 Annapurna Labs Ltd. and affiliates
 * All rights reserved.
 *
 * Developed by Semihalf.
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
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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

#ifndef __AL_ETH_H__
#define	__AL_ETH_H__

#include "al_init_eth_lm.h"
#include "al_hal_eth.h"
#include "al_hal_udma_iofic.h"
#include "al_hal_udma_debug.h"
#include "al_serdes.h"

enum board_t {
	ALPINE_INTEGRATED = 0,
	ALPINE_NIC = 1,
	ALPINE_FPGA_NIC = 2,
};

#define	AL_ETH_MAX_HW_QUEUES	4
#define	AL_ETH_NUM_QUEUES	4
#define	AL_ETH_MAX_MSIX_VEC	(1 + 2 * AL_ETH_MAX_HW_QUEUES)

#define AL_ETH_DEFAULT_TX_SW_DESCS	(512)
#define AL_ETH_DEFAULT_TX_HW_DESCS	(512)
#define AL_ETH_DEFAULT_RX_DESCS		(512)

#if ((AL_ETH_DEFAULT_TX_SW_DESCS / 4) < (AL_ETH_PKT_MAX_BUFS + 2))
#define	AL_ETH_TX_WAKEUP_THRESH		(AL_ETH_DEFAULT_TX_SW_DESCS / 4)
#else
#define	AL_ETH_TX_WAKEUP_THRESH		(AL_ETH_PKT_MAX_BUFS + 2)
#endif

#define	NET_IP_ALIGN				2
#define	AL_ETH_DEFAULT_SMALL_PACKET_LEN		(128 - NET_IP_ALIGN)
#define	AL_ETH_HEADER_COPY_SIZE			(128 - NET_IP_ALIGN)

#define	AL_ETH_DEFAULT_MAX_RX_BUFF_ALLOC_SIZE	9216
/*
 * Minimum the buffer size to 600 to avoid situation the mtu will be changed
 * from too little buffer to very big one and then the number of buffer per
 * packet could reach the maximum AL_ETH_PKT_MAX_BUFS
 */
#define	AL_ETH_DEFAULT_MIN_RX_BUFF_ALLOC_SIZE	600
#define	AL_ETH_DEFAULT_FORCE_1000_BASEX FALSE

#define	AL_ETH_DEFAULT_LINK_POLL_INTERVAL	100
#define	AL_ETH_FIRST_LINK_POLL_INTERVAL		1

#define	AL_ETH_NAME_MAX_LEN	20
#define	AL_ETH_IRQNAME_SIZE	40

#define	AL_ETH_DEFAULT_MDIO_FREQ_KHZ	2500
#define	AL_ETH_MDIO_FREQ_1000_KHZ	1000

struct al_eth_irq {
	driver_filter_t *handler;
	void		*data;
	unsigned int	vector;
	uint8_t		requested;
	char		name[AL_ETH_IRQNAME_SIZE];
	struct resource *res;
	void		*cookie;
};

struct al_eth_tx_buffer {
	struct mbuf *m;
	struct al_eth_pkt hal_pkt;
	bus_dmamap_t	dma_map;
	unsigned int	tx_descs;
};

struct al_eth_rx_buffer {
	struct mbuf	*m;
	unsigned int	data_size;
	bus_dmamap_t	dma_map;
	struct al_buf	al_buf;
};

struct al_eth_ring {
	device_t dev;
	struct al_eth_adapter *adapter;
	/* Used to get rx packets from hal */
	struct al_eth_pkt hal_pkt;
	/* Udma queue handler */
	struct al_udma_q *dma_q;
	uint32_t ring_id;
	uint16_t next_to_use;
	uint16_t next_to_clean;
	/* The offset of the interrupt unmask register */
	uint32_t *unmask_reg_offset;
	/* 
	 * The value to write to the above register to
	 * unmask the interrupt of this ring
	 */
	uint32_t unmask_val;
	struct al_eth_meta_data hal_meta;
	/* Contex of tx packet */
	struct al_eth_tx_buffer *tx_buffer_info;
	/* Contex of rx packet */
	struct al_eth_rx_buffer *rx_buffer_info;
	/* Number of tx/rx_buffer_info's entries */
	int sw_count;
	/* Number of hw descriptors */
	int hw_count;
	/* Size (in bytes) of hw descriptors */
	size_t descs_size;
	/* Size (in bytes) of hw completion descriptors, used for rx */
	size_t cdescs_size;
	struct ifnet *netdev;
	struct al_udma_q_params	q_params;
	struct buf_ring *br;
	struct mtx br_mtx;
	struct task enqueue_task;
	struct taskqueue *enqueue_tq;
	volatile uint32_t enqueue_is_running;
	struct task cmpl_task;
	struct taskqueue *cmpl_tq;
	volatile uint32_t cmpl_is_running;
	uint32_t lro_enabled;
	struct lro_ctrl lro;
	bus_dma_tag_t dma_buf_tag;
	volatile uint32_t stall;
};

#define	AL_ETH_TX_RING_IDX_NEXT(tx_ring, idx) (((idx) + 1) & (AL_ETH_DEFAULT_TX_SW_DESCS - 1))

#define	AL_ETH_RX_RING_IDX_NEXT(rx_ring, idx) (((idx) + 1) & (AL_ETH_DEFAULT_RX_DESCS - 1))
#define	AL_ETH_RX_RING_IDX_ADD(rx_ring, idx, n) (((idx) + (n)) & (AL_ETH_DEFAULT_RX_DESCS - 1))

/* flow control configuration */
#define	AL_ETH_FLOW_CTRL_RX_FIFO_TH_HIGH	0x160
#define	AL_ETH_FLOW_CTRL_RX_FIFO_TH_LOW		0x90
#define	AL_ETH_FLOW_CTRL_QUANTA			0xffff
#define	AL_ETH_FLOW_CTRL_QUANTA_TH		0x8000

#define	AL_ETH_FLOW_CTRL_AUTONEG	1 
#define	AL_ETH_FLOW_CTRL_RX_PAUSE	2
#define	AL_ETH_FLOW_CTRL_TX_PAUSE	4

/* link configuration for 1G port */
struct al_eth_link_config {
	int old_link;
	/* Describes what we actually have. */
	int	active_duplex;
	int	active_speed;

	/* current flow control status */
	uint8_t flow_ctrl_active;
	/* supported configuration (can be changed from ethtool) */
	uint8_t flow_ctrl_supported;

	/* the following are not relevant to RGMII */
	boolean_t	force_1000_base_x;
	boolean_t	autoneg;
};

/* SFP detection event */
enum al_eth_sfp_detect_evt {
	/* No change (no connect, disconnect, or new SFP module */
	AL_ETH_SFP_DETECT_EVT_NO_CHANGE,
	/* SFP module connected */
	AL_ETH_SFP_DETECT_EVT_CONNECTED,
	/* SFP module disconnected */
	AL_ETH_SFP_DETECT_EVT_DISCONNECTED,
	/* SFP module replaced */
	AL_ETH_SFP_DETECT_EVT_CHANGED,
};

/* SFP detection status */
struct al_eth_sfp_detect_stat {
	/* Status is valid (i.e. rest of fields are valid) */
	boolean_t		valid;
	boolean_t		connected;
	uint8_t			sfp_10g;
	uint8_t			sfp_1g;
	uint8_t			sfp_cable_tech;
	boolean_t		lt_en;
	boolean_t		an_en;
	enum al_eth_mac_mode	mac_mode;
};

struct al_eth_retimer_params {
	boolean_t			exist;
	uint8_t				bus_id;
	uint8_t				i2c_addr;
	enum al_eth_retimer_channel	channel;
};

struct msix_entry {
	int entry;
	int vector;
};

/* board specific private data structure */
struct al_eth_adapter {
	enum board_t	board_type;
	device_t	miibus;
	struct mii_data *mii;
	uint16_t dev_id;
	uint8_t rev_id;

	device_t dev;
	struct ifnet *netdev;
	struct ifmedia media;
	struct resource	*udma_res;
	struct resource	*mac_res;
	struct resource	*ec_res;
	int if_flags;
	struct callout wd_callout;
	struct mtx     wd_mtx;
	struct callout stats_callout;
	struct mtx     stats_mtx;

	/* this is for intx mode */
	void *irq_cookie;
	struct resource *irq_res;

	/* 
	 * Some features need tri-state capability,
	 * thus the additional *_CAPABLE flags.
	 */
	uint32_t flags;
#define	AL_ETH_FLAG_MSIX_CAPABLE		(uint32_t)(1 << 1)
#define	AL_ETH_FLAG_MSIX_ENABLED		(uint32_t)(1 << 2)
#define	AL_ETH_FLAG_IN_NETPOLL			(uint32_t)(1 << 3)
#define	AL_ETH_FLAG_MQ_CAPABLE			(uint32_t)(1 << 4)
#define	AL_ETH_FLAG_SRIOV_CAPABLE		(uint32_t)(1 << 5)
#define	AL_ETH_FLAG_SRIOV_ENABLED		(uint32_t)(1 << 6)
#define	AL_ETH_FLAG_RESET_REQUESTED		(uint32_t)(1 << 7)

	struct al_hal_eth_adapter hal_adapter;

	/*
	 * Rx packets that shorter that this len will be copied to the mbuf 
	 */
	unsigned int small_copy_len;

	/* Maximum size for rx buffer */
	unsigned int max_rx_buff_alloc_size;
	uint32_t rx_mbuf_sz;

	/* Tx fast path data */
	int num_tx_queues;

	/* Rx fast path data */
	int num_rx_queues;

	/* TX */
	struct al_eth_ring tx_ring[AL_ETH_NUM_QUEUES];

	/* RX */
	struct al_eth_ring rx_ring[AL_ETH_NUM_QUEUES];

	enum al_iofic_mode int_mode;

#define	AL_ETH_MGMT_IRQ_IDX		0
#define	AL_ETH_RXQ_IRQ_IDX(adapter, q)	(1 + (q))
#define	AL_ETH_TXQ_IRQ_IDX(adapter, q)	(1 + (adapter)->num_rx_queues + (q))
	struct al_eth_irq irq_tbl[AL_ETH_MAX_MSIX_VEC];
	struct msix_entry *msix_entries;
	int	msix_vecs;
	int	irq_vecs;

	unsigned int tx_usecs, rx_usecs; /* interrupt coalescing */

	unsigned int tx_ring_count;
	unsigned int tx_descs_count;
	unsigned int rx_ring_count;
	unsigned int rx_descs_count;

	/* RSS */
	uint32_t toeplitz_hash_key[AL_ETH_RX_HASH_KEY_NUM];
#define	AL_ETH_RX_RSS_TABLE_SIZE	AL_ETH_RX_THASH_TABLE_SIZE
	uint8_t	 rss_ind_tbl[AL_ETH_RX_RSS_TABLE_SIZE];

	uint32_t msg_enable;
	struct al_eth_mac_stats mac_stats;

	enum al_eth_mac_mode	mac_mode;
	boolean_t		mac_mode_set; /* Relevant only when 'auto_speed' is set */
	uint8_t mac_addr[ETHER_ADDR_LEN];
	/* mdio and phy*/
	boolean_t		phy_exist;
	struct mii_bus		*mdio_bus;
	struct phy_device	*phydev;
	uint8_t			phy_addr;
	struct al_eth_link_config	link_config;

	/* HAL layer data */
	int			id_number;
	char			name[AL_ETH_NAME_MAX_LEN];
	void			*internal_pcie_base; /* use for ALPINE_NIC devices */
	void			*udma_base;
	void			*ec_base;
	void			*mac_base;

	struct al_eth_flow_control_params flow_ctrl_params;

	struct al_eth_adapter_params eth_hal_params;

	struct task			link_status_task;
	uint32_t			link_poll_interval; /* task interval in mSec */

	boolean_t			serdes_init;
	struct al_serdes_grp_obj	serdes_obj;
	uint8_t				serdes_grp;
	uint8_t				serdes_lane;

	boolean_t			an_en;	/* run kr auto-negotiation */
	boolean_t			lt_en;	/* run kr link-training */

	boolean_t			sfp_detection_needed; /* true if need to run sfp detection */
	boolean_t			auto_speed; /* true if allowed to change SerDes speed configuration */
	uint8_t				i2c_adapter_id; /* identifier for the i2c adapter to use to access SFP+ module */
	enum al_eth_ref_clk_freq	ref_clk_freq; /* reference clock frequency */
	unsigned int			mdio_freq; /* MDIO frequency [Khz] */

	boolean_t up;

	boolean_t			last_link;
	boolean_t			last_establish_failed;
	struct al_eth_lm_context	lm_context;
	boolean_t			use_lm;

	boolean_t			dont_override_serdes; /* avoid overriding serdes parameters
								   to preset static values */
	struct mtx			serdes_config_lock;
	struct mtx			if_rx_lock;

	uint32_t wol;

	struct al_eth_retimer_params	retimer;

	bool				phy_fixup_needed;

	enum al_eth_lm_max_speed	max_speed;
};

#endif /* !(AL_ETH_H) */
