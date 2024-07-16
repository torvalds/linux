/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
/* QLogic qed NIC Driver
 * Copyright (c) 2015-2017  QLogic Corporation
 * Copyright (c) 2019-2020 Marvell International Ltd.
 */

#ifndef _QED_LL2_IF_H
#define _QED_LL2_IF_H

#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/netdevice.h>
#include <linux/pci.h>
#include <linux/skbuff.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/qed/qed_if.h>

enum qed_ll2_conn_type {
	QED_LL2_TYPE_FCOE,
	QED_LL2_TYPE_TCP_ULP,
	QED_LL2_TYPE_TEST,
	QED_LL2_TYPE_OOO,
	QED_LL2_TYPE_RESERVED2,
	QED_LL2_TYPE_ROCE,
	QED_LL2_TYPE_IWARP,
	QED_LL2_TYPE_RESERVED3,
	MAX_QED_LL2_CONN_TYPE
};

enum qed_ll2_rx_conn_type {
	QED_LL2_RX_TYPE_LEGACY,
	QED_LL2_RX_TYPE_CTX,
	MAX_QED_LL2_RX_CONN_TYPE
};

enum qed_ll2_roce_flavor_type {
	QED_LL2_ROCE,
	QED_LL2_RROCE,
	MAX_QED_LL2_ROCE_FLAVOR_TYPE
};

enum qed_ll2_tx_dest {
	QED_LL2_TX_DEST_NW, /* Light L2 TX Destination to the Network */
	QED_LL2_TX_DEST_LB, /* Light L2 TX Destination to the Loopback */
	QED_LL2_TX_DEST_DROP, /* Light L2 Drop the TX packet */
	QED_LL2_TX_DEST_MAX
};

enum qed_ll2_error_handle {
	QED_LL2_DROP_PACKET,
	QED_LL2_DO_NOTHING,
	QED_LL2_ASSERT,
};

struct qed_ll2_stats {
	u64 gsi_invalid_hdr;
	u64 gsi_invalid_pkt_length;
	u64 gsi_unsupported_pkt_typ;
	u64 gsi_crcchksm_error;

	u64 packet_too_big_discard;
	u64 no_buff_discard;

	u64 rcv_ucast_bytes;
	u64 rcv_mcast_bytes;
	u64 rcv_bcast_bytes;
	u64 rcv_ucast_pkts;
	u64 rcv_mcast_pkts;
	u64 rcv_bcast_pkts;

	u64 sent_ucast_bytes;
	u64 sent_mcast_bytes;
	u64 sent_bcast_bytes;
	u64 sent_ucast_pkts;
	u64 sent_mcast_pkts;
	u64 sent_bcast_pkts;
};

struct qed_ll2_comp_rx_data {
	void *cookie;
	dma_addr_t rx_buf_addr;
	u16 parse_flags;
	u16 err_flags;
	u16 vlan;
	bool b_last_packet;
	u8 connection_handle;

	union {
		u16 packet_length;
		u16 data_length;
	} length;

	u32 opaque_data_0;
	u32 opaque_data_1;

	/* GSI only */
	u32 src_qp;
	u16 qp_id;

	union {
		u8 placement_offset;
		u8 data_length_error;
	} u;
};

typedef
void (*qed_ll2_complete_rx_packet_cb)(void *cxt,
				      struct qed_ll2_comp_rx_data *data);

typedef
void (*qed_ll2_release_rx_packet_cb)(void *cxt,
				     u8 connection_handle,
				     void *cookie,
				     dma_addr_t rx_buf_addr,
				     bool b_last_packet);

typedef
void (*qed_ll2_complete_tx_packet_cb)(void *cxt,
				      u8 connection_handle,
				      void *cookie,
				      dma_addr_t first_frag_addr,
				      bool b_last_fragment,
				      bool b_last_packet);

typedef
void (*qed_ll2_release_tx_packet_cb)(void *cxt,
				     u8 connection_handle,
				     void *cookie,
				     dma_addr_t first_frag_addr,
				     bool b_last_fragment, bool b_last_packet);

typedef
void (*qed_ll2_slowpath_cb)(void *cxt, u8 connection_handle,
			    u32 opaque_data_0, u32 opaque_data_1);

struct qed_ll2_cbs {
	qed_ll2_complete_rx_packet_cb rx_comp_cb;
	qed_ll2_release_rx_packet_cb rx_release_cb;
	qed_ll2_complete_tx_packet_cb tx_comp_cb;
	qed_ll2_release_tx_packet_cb tx_release_cb;
	qed_ll2_slowpath_cb slowpath_cb;
	void *cookie;
};

struct qed_ll2_acquire_data_inputs {
	enum qed_ll2_rx_conn_type rx_conn_type;
	enum qed_ll2_conn_type conn_type;
	u16 mtu;
	u16 rx_num_desc;
	u16 rx_num_ooo_buffers;
	u8 rx_drop_ttl0_flg;
	u8 rx_vlan_removal_en;
	u16 tx_num_desc;
	u8 tx_max_bds_per_packet;
	u8 tx_tc;
	enum qed_ll2_tx_dest tx_dest;
	enum qed_ll2_error_handle ai_err_packet_too_big;
	enum qed_ll2_error_handle ai_err_no_buf;
	bool secondary_queue;
	u8 gsi_enable;
};

struct qed_ll2_acquire_data {
	struct qed_ll2_acquire_data_inputs input;
	const struct qed_ll2_cbs *cbs;

	/* Output container for LL2 connection's handle */
	u8 *p_connection_handle;
};

struct qed_ll2_tx_pkt_info {
	void *cookie;
	dma_addr_t first_frag;
	enum qed_ll2_tx_dest tx_dest;
	enum qed_ll2_roce_flavor_type qed_roce_flavor;
	u16 vlan;
	u16 l4_hdr_offset_w;	/* from start of packet */
	u16 first_frag_len;
	u8 num_of_bds;
	u8 bd_flags;
	bool enable_ip_cksum;
	bool enable_l4_cksum;
	bool calc_ip_len;
	bool remove_stag;
};

#define QED_LL2_UNUSED_HANDLE   (0xff)

struct qed_ll2_cb_ops {
	int (*rx_cb)(void *, struct sk_buff *, u32, u32);
	int (*tx_cb)(void *, struct sk_buff *, bool);
};

struct qed_ll2_params {
	u16 mtu;
	bool drop_ttl0_packets;
	bool rx_vlan_stripping;
	u8 tx_tc;
	bool frags_mapped;
	u8 ll2_mac_address[ETH_ALEN];
};

enum qed_ll2_xmit_flags {
	/* FIP discovery packet */
	QED_LL2_XMIT_FLAGS_FIP_DISCOVERY
};

struct qed_ll2_ops {
/**
 * start(): Initializes ll2.
 *
 * @cdev: Qed dev pointer.
 * @params: Protocol driver configuration for the ll2.
 *
 * Return: 0 on success, otherwise error value.
 */
	int (*start)(struct qed_dev *cdev, struct qed_ll2_params *params);

/**
 * stop(): Stops the ll2
 *
 * @cdev: Qed dev pointer.
 *
 * Return: 0 on success, otherwise error value.
 */
	int (*stop)(struct qed_dev *cdev);

/**
 * start_xmit(): Transmits an skb over the ll2 interface
 *
 * @cdev: Qed dev pointer.
 * @skb: SKB.
 * @xmit_flags: Transmit options defined by the enum qed_ll2_xmit_flags.
 *
 * Return: 0 on success, otherwise error value.
 */
	int (*start_xmit)(struct qed_dev *cdev, struct sk_buff *skb,
			  unsigned long xmit_flags);

/**
 * register_cb_ops(): Protocol driver register the callback for Rx/Tx
 * packets. Should be called before `start'.
 *
 * @cdev: Qed dev pointer.
 * @cookie: to be passed to the callback functions.
 * @ops: the callback functions to register for Rx / Tx.
 *
 * Return: 0 on success, otherwise error value.
 */
	void (*register_cb_ops)(struct qed_dev *cdev,
				const struct qed_ll2_cb_ops *ops,
				void *cookie);

/**
 * get_stats(): Get LL2 related statistics.
 *
 * @cdev: Qed dev pointer.
 * @stats: Pointer to struct that would be filled with stats.
 *
 * Return: 0 on success, error otherwise.
 */
	int (*get_stats)(struct qed_dev *cdev, struct qed_ll2_stats *stats);
};

#ifdef CONFIG_QED_LL2
int qed_ll2_alloc_if(struct qed_dev *);
void qed_ll2_dealloc_if(struct qed_dev *);
#else
static const struct qed_ll2_ops qed_ll2_ops_pass = {
	.start = NULL,
	.stop = NULL,
	.start_xmit = NULL,
	.register_cb_ops = NULL,
	.get_stats = NULL,
};

static inline int qed_ll2_alloc_if(struct qed_dev *cdev)
{
	return 0;
}

static inline void qed_ll2_dealloc_if(struct qed_dev *cdev)
{
}
#endif
#endif
