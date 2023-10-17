/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright (c) 2021, Microsoft Corporation. */

#ifndef _MANA_H
#define _MANA_H

#include <net/xdp.h>

#include "gdma.h"
#include "hw_channel.h"

/* Microsoft Azure Network Adapter (MANA)'s definitions
 *
 * Structures labeled with "HW DATA" are exchanged with the hardware. All of
 * them are naturally aligned and hence don't need __packed.
 */

/* MANA protocol version */
#define MANA_MAJOR_VERSION	0
#define MANA_MINOR_VERSION	1
#define MANA_MICRO_VERSION	1

typedef u64 mana_handle_t;
#define INVALID_MANA_HANDLE ((mana_handle_t)-1)

enum TRI_STATE {
	TRI_STATE_UNKNOWN = -1,
	TRI_STATE_FALSE = 0,
	TRI_STATE_TRUE = 1
};

/* Number of entries for hardware indirection table must be in power of 2 */
#define MANA_INDIRECT_TABLE_SIZE 64
#define MANA_INDIRECT_TABLE_MASK (MANA_INDIRECT_TABLE_SIZE - 1)

/* The Toeplitz hash key's length in bytes: should be multiple of 8 */
#define MANA_HASH_KEY_SIZE 40

#define COMP_ENTRY_SIZE 64

#define RX_BUFFERS_PER_QUEUE 512
#define MANA_RX_DATA_ALIGN 64

#define MAX_SEND_BUFFERS_PER_QUEUE 256

#define EQ_SIZE (8 * PAGE_SIZE)
#define LOG2_EQ_THROTTLE 3

#define MAX_PORTS_IN_MANA_DEV 256

/* Update this count whenever the respective structures are changed */
#define MANA_STATS_RX_COUNT 5
#define MANA_STATS_TX_COUNT 11

struct mana_stats_rx {
	u64 packets;
	u64 bytes;
	u64 xdp_drop;
	u64 xdp_tx;
	u64 xdp_redirect;
	struct u64_stats_sync syncp;
};

struct mana_stats_tx {
	u64 packets;
	u64 bytes;
	u64 xdp_xmit;
	u64 tso_packets;
	u64 tso_bytes;
	u64 tso_inner_packets;
	u64 tso_inner_bytes;
	u64 short_pkt_fmt;
	u64 long_pkt_fmt;
	u64 csum_partial;
	u64 mana_map_err;
	struct u64_stats_sync syncp;
};

struct mana_txq {
	struct gdma_queue *gdma_sq;

	union {
		u32 gdma_txq_id;
		struct {
			u32 reserved1	: 10;
			u32 vsq_frame	: 14;
			u32 reserved2	: 8;
		};
	};

	u16 vp_offset;

	struct net_device *ndev;

	/* The SKBs are sent to the HW and we are waiting for the CQEs. */
	struct sk_buff_head pending_skbs;
	struct netdev_queue *net_txq;

	atomic_t pending_sends;

	struct mana_stats_tx stats;
};

/* skb data and frags dma mappings */
struct mana_skb_head {
	/* GSO pkts may have 2 SGEs for the linear part*/
	dma_addr_t dma_handle[MAX_SKB_FRAGS + 2];

	u32 size[MAX_SKB_FRAGS + 2];
};

#define MANA_HEADROOM sizeof(struct mana_skb_head)

enum mana_tx_pkt_format {
	MANA_SHORT_PKT_FMT	= 0,
	MANA_LONG_PKT_FMT	= 1,
};

struct mana_tx_short_oob {
	u32 pkt_fmt		: 2;
	u32 is_outer_ipv4	: 1;
	u32 is_outer_ipv6	: 1;
	u32 comp_iphdr_csum	: 1;
	u32 comp_tcp_csum	: 1;
	u32 comp_udp_csum	: 1;
	u32 supress_txcqe_gen	: 1;
	u32 vcq_num		: 24;

	u32 trans_off		: 10; /* Transport header offset */
	u32 vsq_frame		: 14;
	u32 short_vp_offset	: 8;
}; /* HW DATA */

struct mana_tx_long_oob {
	u32 is_encap		: 1;
	u32 inner_is_ipv6	: 1;
	u32 inner_tcp_opt	: 1;
	u32 inject_vlan_pri_tag : 1;
	u32 reserved1		: 12;
	u32 pcp			: 3;  /* 802.1Q */
	u32 dei			: 1;  /* 802.1Q */
	u32 vlan_id		: 12; /* 802.1Q */

	u32 inner_frame_offset	: 10;
	u32 inner_ip_rel_offset : 6;
	u32 long_vp_offset	: 12;
	u32 reserved2		: 4;

	u32 reserved3;
	u32 reserved4;
}; /* HW DATA */

struct mana_tx_oob {
	struct mana_tx_short_oob s_oob;
	struct mana_tx_long_oob l_oob;
}; /* HW DATA */

enum mana_cq_type {
	MANA_CQ_TYPE_RX,
	MANA_CQ_TYPE_TX,
};

enum mana_cqe_type {
	CQE_INVALID			= 0,
	CQE_RX_OKAY			= 1,
	CQE_RX_COALESCED_4		= 2,
	CQE_RX_OBJECT_FENCE		= 3,
	CQE_RX_TRUNCATED		= 4,

	CQE_TX_OKAY			= 32,
	CQE_TX_SA_DROP			= 33,
	CQE_TX_MTU_DROP			= 34,
	CQE_TX_INVALID_OOB		= 35,
	CQE_TX_INVALID_ETH_TYPE		= 36,
	CQE_TX_HDR_PROCESSING_ERROR	= 37,
	CQE_TX_VF_DISABLED		= 38,
	CQE_TX_VPORT_IDX_OUT_OF_RANGE	= 39,
	CQE_TX_VPORT_DISABLED		= 40,
	CQE_TX_VLAN_TAGGING_VIOLATION	= 41,
};

#define MANA_CQE_COMPLETION 1

struct mana_cqe_header {
	u32 cqe_type	: 6;
	u32 client_type	: 2;
	u32 vendor_err	: 24;
}; /* HW DATA */

/* NDIS HASH Types */
#define NDIS_HASH_IPV4		BIT(0)
#define NDIS_HASH_TCP_IPV4	BIT(1)
#define NDIS_HASH_UDP_IPV4	BIT(2)
#define NDIS_HASH_IPV6		BIT(3)
#define NDIS_HASH_TCP_IPV6	BIT(4)
#define NDIS_HASH_UDP_IPV6	BIT(5)
#define NDIS_HASH_IPV6_EX	BIT(6)
#define NDIS_HASH_TCP_IPV6_EX	BIT(7)
#define NDIS_HASH_UDP_IPV6_EX	BIT(8)

#define MANA_HASH_L3 (NDIS_HASH_IPV4 | NDIS_HASH_IPV6 | NDIS_HASH_IPV6_EX)
#define MANA_HASH_L4                                                         \
	(NDIS_HASH_TCP_IPV4 | NDIS_HASH_UDP_IPV4 | NDIS_HASH_TCP_IPV6 |      \
	 NDIS_HASH_UDP_IPV6 | NDIS_HASH_TCP_IPV6_EX | NDIS_HASH_UDP_IPV6_EX)

struct mana_rxcomp_perpkt_info {
	u32 pkt_len	: 16;
	u32 reserved1	: 16;
	u32 reserved2;
	u32 pkt_hash;
}; /* HW DATA */

#define MANA_RXCOMP_OOB_NUM_PPI 4

/* Receive completion OOB */
struct mana_rxcomp_oob {
	struct mana_cqe_header cqe_hdr;

	u32 rx_vlan_id			: 12;
	u32 rx_vlantag_present		: 1;
	u32 rx_outer_iphdr_csum_succeed	: 1;
	u32 rx_outer_iphdr_csum_fail	: 1;
	u32 reserved1			: 1;
	u32 rx_hashtype			: 9;
	u32 rx_iphdr_csum_succeed	: 1;
	u32 rx_iphdr_csum_fail		: 1;
	u32 rx_tcp_csum_succeed		: 1;
	u32 rx_tcp_csum_fail		: 1;
	u32 rx_udp_csum_succeed		: 1;
	u32 rx_udp_csum_fail		: 1;
	u32 reserved2			: 1;

	struct mana_rxcomp_perpkt_info ppi[MANA_RXCOMP_OOB_NUM_PPI];

	u32 rx_wqe_offset;
}; /* HW DATA */

struct mana_tx_comp_oob {
	struct mana_cqe_header cqe_hdr;

	u32 tx_data_offset;

	u32 tx_sgl_offset	: 5;
	u32 tx_wqe_offset	: 27;

	u32 reserved[12];
}; /* HW DATA */

struct mana_rxq;

#define CQE_POLLING_BUFFER 512

struct mana_cq {
	struct gdma_queue *gdma_cq;

	/* Cache the CQ id (used to verify if each CQE comes to the right CQ. */
	u32 gdma_id;

	/* Type of the CQ: TX or RX */
	enum mana_cq_type type;

	/* Pointer to the mana_rxq that is pushing RX CQEs to the queue.
	 * Only and must be non-NULL if type is MANA_CQ_TYPE_RX.
	 */
	struct mana_rxq *rxq;

	/* Pointer to the mana_txq that is pushing TX CQEs to the queue.
	 * Only and must be non-NULL if type is MANA_CQ_TYPE_TX.
	 */
	struct mana_txq *txq;

	/* Buffer which the CQ handler can copy the CQE's into. */
	struct gdma_comp gdma_comp_buf[CQE_POLLING_BUFFER];

	/* NAPI data */
	struct napi_struct napi;
	int work_done;
	int budget;
};

struct mana_recv_buf_oob {
	/* A valid GDMA work request representing the data buffer. */
	struct gdma_wqe_request wqe_req;

	void *buf_va;
	bool from_pool; /* allocated from a page pool */

	/* SGL of the buffer going to be sent has part of the work request. */
	u32 num_sge;
	struct gdma_sge sgl[MAX_RX_WQE_SGL_ENTRIES];

	/* Required to store the result of mana_gd_post_work_request.
	 * gdma_posted_wqe_info.wqe_size_in_bu is required for progressing the
	 * work queue when the WQE is consumed.
	 */
	struct gdma_posted_wqe_info wqe_inf;
};

#define MANA_RXBUF_PAD (SKB_DATA_ALIGN(sizeof(struct skb_shared_info)) \
			+ ETH_HLEN)

#define MANA_XDP_MTU_MAX (PAGE_SIZE - MANA_RXBUF_PAD - XDP_PACKET_HEADROOM)

struct mana_rxq {
	struct gdma_queue *gdma_rq;
	/* Cache the gdma receive queue id */
	u32 gdma_id;

	/* Index of RQ in the vPort, not gdma receive queue id */
	u32 rxq_idx;

	u32 datasize;
	u32 alloc_size;
	u32 headroom;

	mana_handle_t rxobj;

	struct mana_cq rx_cq;

	struct completion fence_event;

	struct net_device *ndev;

	/* Total number of receive buffers to be allocated */
	u32 num_rx_buf;

	u32 buf_index;

	struct mana_stats_rx stats;

	struct bpf_prog __rcu *bpf_prog;
	struct xdp_rxq_info xdp_rxq;
	void *xdp_save_va; /* for reusing */
	bool xdp_flush;
	int xdp_rc; /* XDP redirect return code */

	struct page_pool *page_pool;

	/* MUST BE THE LAST MEMBER:
	 * Each receive buffer has an associated mana_recv_buf_oob.
	 */
	struct mana_recv_buf_oob rx_oobs[];
};

struct mana_tx_qp {
	struct mana_txq txq;

	struct mana_cq tx_cq;

	mana_handle_t tx_object;
};

struct mana_ethtool_stats {
	u64 stop_queue;
	u64 wake_queue;
	u64 hc_tx_bytes;
	u64 hc_tx_ucast_pkts;
	u64 hc_tx_ucast_bytes;
	u64 hc_tx_bcast_pkts;
	u64 hc_tx_bcast_bytes;
	u64 hc_tx_mcast_pkts;
	u64 hc_tx_mcast_bytes;
	u64 tx_cqe_err;
	u64 tx_cqe_unknown_type;
	u64 rx_coalesced_err;
	u64 rx_cqe_unknown_type;
};

struct mana_context {
	struct gdma_dev *gdma_dev;

	u16 num_ports;

	struct mana_eq *eqs;

	struct net_device *ports[MAX_PORTS_IN_MANA_DEV];
};

struct mana_port_context {
	struct mana_context *ac;
	struct net_device *ndev;

	u8 mac_addr[ETH_ALEN];

	enum TRI_STATE rss_state;

	mana_handle_t default_rxobj;
	bool tx_shortform_allowed;
	u16 tx_vp_offset;

	struct mana_tx_qp *tx_qp;

	/* Indirection Table for RX & TX. The values are queue indexes */
	u32 indir_table[MANA_INDIRECT_TABLE_SIZE];

	/* Indirection table containing RxObject Handles */
	mana_handle_t rxobj_table[MANA_INDIRECT_TABLE_SIZE];

	/*  Hash key used by the NIC */
	u8 hashkey[MANA_HASH_KEY_SIZE];

	/* This points to an array of num_queues of RQ pointers. */
	struct mana_rxq **rxqs;

	/* pre-allocated rx buffer array */
	void **rxbufs_pre;
	dma_addr_t *das_pre;
	int rxbpre_total;
	u32 rxbpre_datasize;
	u32 rxbpre_alloc_size;
	u32 rxbpre_headroom;

	struct bpf_prog *bpf_prog;

	/* Create num_queues EQs, SQs, SQ-CQs, RQs and RQ-CQs, respectively. */
	unsigned int max_queues;
	unsigned int num_queues;

	mana_handle_t port_handle;
	mana_handle_t pf_filter_handle;

	/* Mutex for sharing access to vport_use_count */
	struct mutex vport_mutex;
	int vport_use_count;

	u16 port_idx;

	bool port_is_up;
	bool port_st_save; /* Saved port state */

	struct mana_ethtool_stats eth_stats;
};

netdev_tx_t mana_start_xmit(struct sk_buff *skb, struct net_device *ndev);
int mana_config_rss(struct mana_port_context *ac, enum TRI_STATE rx,
		    bool update_hash, bool update_tab);

int mana_alloc_queues(struct net_device *ndev);
int mana_attach(struct net_device *ndev);
int mana_detach(struct net_device *ndev, bool from_close);

int mana_probe(struct gdma_dev *gd, bool resuming);
void mana_remove(struct gdma_dev *gd, bool suspending);

void mana_xdp_tx(struct sk_buff *skb, struct net_device *ndev);
int mana_xdp_xmit(struct net_device *ndev, int n, struct xdp_frame **frames,
		  u32 flags);
u32 mana_run_xdp(struct net_device *ndev, struct mana_rxq *rxq,
		 struct xdp_buff *xdp, void *buf_va, uint pkt_len);
struct bpf_prog *mana_xdp_get(struct mana_port_context *apc);
void mana_chn_setxdp(struct mana_port_context *apc, struct bpf_prog *prog);
int mana_bpf(struct net_device *ndev, struct netdev_bpf *bpf);
void mana_query_gf_stats(struct mana_port_context *apc);

extern const struct ethtool_ops mana_ethtool_ops;

/* A CQ can be created not associated with any EQ */
#define GDMA_CQ_NO_EQ  0xffff

struct mana_obj_spec {
	u32 queue_index;
	u64 gdma_region;
	u32 queue_size;
	u32 attached_eq;
	u32 modr_ctx_id;
};

enum mana_command_code {
	MANA_QUERY_DEV_CONFIG	= 0x20001,
	MANA_QUERY_GF_STAT	= 0x20002,
	MANA_CONFIG_VPORT_TX	= 0x20003,
	MANA_CREATE_WQ_OBJ	= 0x20004,
	MANA_DESTROY_WQ_OBJ	= 0x20005,
	MANA_FENCE_RQ		= 0x20006,
	MANA_CONFIG_VPORT_RX	= 0x20007,
	MANA_QUERY_VPORT_CONFIG	= 0x20008,

	/* Privileged commands for the PF mode */
	MANA_REGISTER_FILTER	= 0x28000,
	MANA_DEREGISTER_FILTER	= 0x28001,
	MANA_REGISTER_HW_PORT	= 0x28003,
	MANA_DEREGISTER_HW_PORT	= 0x28004,
};

/* Query Device Configuration */
struct mana_query_device_cfg_req {
	struct gdma_req_hdr hdr;

	/* MANA Nic Driver Capability flags */
	u64 mn_drv_cap_flags1;
	u64 mn_drv_cap_flags2;
	u64 mn_drv_cap_flags3;
	u64 mn_drv_cap_flags4;

	u32 proto_major_ver;
	u32 proto_minor_ver;
	u32 proto_micro_ver;

	u32 reserved;
}; /* HW DATA */

struct mana_query_device_cfg_resp {
	struct gdma_resp_hdr hdr;

	u64 pf_cap_flags1;
	u64 pf_cap_flags2;
	u64 pf_cap_flags3;
	u64 pf_cap_flags4;

	u16 max_num_vports;
	u16 reserved;
	u32 max_num_eqs;

	/* response v2: */
	u16 adapter_mtu;
	u16 reserved2;
	u32 reserved3;
}; /* HW DATA */

/* Query vPort Configuration */
struct mana_query_vport_cfg_req {
	struct gdma_req_hdr hdr;
	u32 vport_index;
}; /* HW DATA */

struct mana_query_vport_cfg_resp {
	struct gdma_resp_hdr hdr;
	u32 max_num_sq;
	u32 max_num_rq;
	u32 num_indirection_ent;
	u32 reserved1;
	u8 mac_addr[6];
	u8 reserved2[2];
	mana_handle_t vport;
}; /* HW DATA */

/* Configure vPort */
struct mana_config_vport_req {
	struct gdma_req_hdr hdr;
	mana_handle_t vport;
	u32 pdid;
	u32 doorbell_pageid;
}; /* HW DATA */

struct mana_config_vport_resp {
	struct gdma_resp_hdr hdr;
	u16 tx_vport_offset;
	u8 short_form_allowed;
	u8 reserved;
}; /* HW DATA */

/* Create WQ Object */
struct mana_create_wqobj_req {
	struct gdma_req_hdr hdr;
	mana_handle_t vport;
	u32 wq_type;
	u32 reserved;
	u64 wq_gdma_region;
	u64 cq_gdma_region;
	u32 wq_size;
	u32 cq_size;
	u32 cq_moderation_ctx_id;
	u32 cq_parent_qid;
}; /* HW DATA */

struct mana_create_wqobj_resp {
	struct gdma_resp_hdr hdr;
	u32 wq_id;
	u32 cq_id;
	mana_handle_t wq_obj;
}; /* HW DATA */

/* Destroy WQ Object */
struct mana_destroy_wqobj_req {
	struct gdma_req_hdr hdr;
	u32 wq_type;
	u32 reserved;
	mana_handle_t wq_obj_handle;
}; /* HW DATA */

struct mana_destroy_wqobj_resp {
	struct gdma_resp_hdr hdr;
}; /* HW DATA */

/* Fence RQ */
struct mana_fence_rq_req {
	struct gdma_req_hdr hdr;
	mana_handle_t wq_obj_handle;
}; /* HW DATA */

struct mana_fence_rq_resp {
	struct gdma_resp_hdr hdr;
}; /* HW DATA */

/* Query stats RQ */
struct mana_query_gf_stat_req {
	struct gdma_req_hdr hdr;
	u64 req_stats;
}; /* HW DATA */

struct mana_query_gf_stat_resp {
	struct gdma_resp_hdr hdr;
	u64 reported_stats;
	/* rx errors/discards */
	u64 discard_rx_nowqe;
	u64 err_rx_vport_disabled;
	/* rx bytes/packets */
	u64 hc_rx_bytes;
	u64 hc_rx_ucast_pkts;
	u64 hc_rx_ucast_bytes;
	u64 hc_rx_bcast_pkts;
	u64 hc_rx_bcast_bytes;
	u64 hc_rx_mcast_pkts;
	u64 hc_rx_mcast_bytes;
	/* tx errors */
	u64 err_tx_gf_disabled;
	u64 err_tx_vport_disabled;
	u64 err_tx_inval_vport_offset_pkt;
	u64 err_tx_vlan_enforcement;
	u64 err_tx_ethtype_enforcement;
	u64 err_tx_SA_enforecement;
	u64 err_tx_SQPDID_enforcement;
	u64 err_tx_CQPDID_enforcement;
	u64 err_tx_mtu_violation;
	u64 err_tx_inval_oob;
	/* tx bytes/packets */
	u64 hc_tx_bytes;
	u64 hc_tx_ucast_pkts;
	u64 hc_tx_ucast_bytes;
	u64 hc_tx_bcast_pkts;
	u64 hc_tx_bcast_bytes;
	u64 hc_tx_mcast_pkts;
	u64 hc_tx_mcast_bytes;
	/* tx error */
	u64 err_tx_gdma;
}; /* HW DATA */

/* Configure vPort Rx Steering */
struct mana_cfg_rx_steer_req_v2 {
	struct gdma_req_hdr hdr;
	mana_handle_t vport;
	u16 num_indir_entries;
	u16 indir_tab_offset;
	u32 rx_enable;
	u32 rss_enable;
	u8 update_default_rxobj;
	u8 update_hashkey;
	u8 update_indir_tab;
	u8 reserved;
	mana_handle_t default_rxobj;
	u8 hashkey[MANA_HASH_KEY_SIZE];
	u8 cqe_coalescing_enable;
	u8 reserved2[7];
}; /* HW DATA */

struct mana_cfg_rx_steer_resp {
	struct gdma_resp_hdr hdr;
}; /* HW DATA */

/* Register HW vPort */
struct mana_register_hw_vport_req {
	struct gdma_req_hdr hdr;
	u16 attached_gfid;
	u8 is_pf_default_vport;
	u8 reserved1;
	u8 allow_all_ether_types;
	u8 reserved2;
	u8 reserved3;
	u8 reserved4;
}; /* HW DATA */

struct mana_register_hw_vport_resp {
	struct gdma_resp_hdr hdr;
	mana_handle_t hw_vport_handle;
}; /* HW DATA */

/* Deregister HW vPort */
struct mana_deregister_hw_vport_req {
	struct gdma_req_hdr hdr;
	mana_handle_t hw_vport_handle;
}; /* HW DATA */

struct mana_deregister_hw_vport_resp {
	struct gdma_resp_hdr hdr;
}; /* HW DATA */

/* Register filter */
struct mana_register_filter_req {
	struct gdma_req_hdr hdr;
	mana_handle_t vport;
	u8 mac_addr[6];
	u8 reserved1;
	u8 reserved2;
	u8 reserved3;
	u8 reserved4;
	u16 reserved5;
	u32 reserved6;
	u32 reserved7;
	u32 reserved8;
}; /* HW DATA */

struct mana_register_filter_resp {
	struct gdma_resp_hdr hdr;
	mana_handle_t filter_handle;
}; /* HW DATA */

/* Deregister filter */
struct mana_deregister_filter_req {
	struct gdma_req_hdr hdr;
	mana_handle_t filter_handle;
}; /* HW DATA */

struct mana_deregister_filter_resp {
	struct gdma_resp_hdr hdr;
}; /* HW DATA */

/* Requested GF stats Flags */
/* Rx discards/Errors */
#define STATISTICS_FLAGS_RX_DISCARDS_NO_WQE		0x0000000000000001
#define STATISTICS_FLAGS_RX_ERRORS_VPORT_DISABLED	0x0000000000000002
/* Rx bytes/pkts */
#define STATISTICS_FLAGS_HC_RX_BYTES			0x0000000000000004
#define STATISTICS_FLAGS_HC_RX_UCAST_PACKETS		0x0000000000000008
#define STATISTICS_FLAGS_HC_RX_UCAST_BYTES		0x0000000000000010
#define STATISTICS_FLAGS_HC_RX_MCAST_PACKETS		0x0000000000000020
#define STATISTICS_FLAGS_HC_RX_MCAST_BYTES		0x0000000000000040
#define STATISTICS_FLAGS_HC_RX_BCAST_PACKETS		0x0000000000000080
#define STATISTICS_FLAGS_HC_RX_BCAST_BYTES		0x0000000000000100
/* Tx errors */
#define STATISTICS_FLAGS_TX_ERRORS_GF_DISABLED		0x0000000000000200
#define STATISTICS_FLAGS_TX_ERRORS_VPORT_DISABLED	0x0000000000000400
#define STATISTICS_FLAGS_TX_ERRORS_INVAL_VPORT_OFFSET_PACKETS		\
							0x0000000000000800
#define STATISTICS_FLAGS_TX_ERRORS_VLAN_ENFORCEMENT	0x0000000000001000
#define STATISTICS_FLAGS_TX_ERRORS_ETH_TYPE_ENFORCEMENT			\
							0x0000000000002000
#define STATISTICS_FLAGS_TX_ERRORS_SA_ENFORCEMENT	0x0000000000004000
#define STATISTICS_FLAGS_TX_ERRORS_SQPDID_ENFORCEMENT	0x0000000000008000
#define STATISTICS_FLAGS_TX_ERRORS_CQPDID_ENFORCEMENT	0x0000000000010000
#define STATISTICS_FLAGS_TX_ERRORS_MTU_VIOLATION	0x0000000000020000
#define STATISTICS_FLAGS_TX_ERRORS_INVALID_OOB		0x0000000000040000
/* Tx bytes/pkts */
#define STATISTICS_FLAGS_HC_TX_BYTES			0x0000000000080000
#define STATISTICS_FLAGS_HC_TX_UCAST_PACKETS		0x0000000000100000
#define STATISTICS_FLAGS_HC_TX_UCAST_BYTES		0x0000000000200000
#define STATISTICS_FLAGS_HC_TX_MCAST_PACKETS		0x0000000000400000
#define STATISTICS_FLAGS_HC_TX_MCAST_BYTES		0x0000000000800000
#define STATISTICS_FLAGS_HC_TX_BCAST_PACKETS		0x0000000001000000
#define STATISTICS_FLAGS_HC_TX_BCAST_BYTES		0x0000000002000000
/* Tx error */
#define STATISTICS_FLAGS_TX_ERRORS_GDMA_ERROR		0x0000000004000000

#define MANA_MAX_NUM_QUEUES 64

#define MANA_SHORT_VPORT_OFFSET_MAX ((1U << 8) - 1)

struct mana_tx_package {
	struct gdma_wqe_request wqe_req;
	struct gdma_sge sgl_array[5];
	struct gdma_sge *sgl_ptr;

	struct mana_tx_oob tx_oob;

	struct gdma_posted_wqe_info wqe_info;
};

int mana_create_wq_obj(struct mana_port_context *apc,
		       mana_handle_t vport,
		       u32 wq_type, struct mana_obj_spec *wq_spec,
		       struct mana_obj_spec *cq_spec,
		       mana_handle_t *wq_obj);

void mana_destroy_wq_obj(struct mana_port_context *apc, u32 wq_type,
			 mana_handle_t wq_obj);

int mana_cfg_vport(struct mana_port_context *apc, u32 protection_dom_id,
		   u32 doorbell_pg_id);
void mana_uncfg_vport(struct mana_port_context *apc);
#endif /* _MANA_H */
