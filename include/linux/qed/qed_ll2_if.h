/* QLogic qed NIC Driver
 *
 * Copyright (c) 2015 QLogic Corporation
 *
 * This software is available under the terms of the GNU General Public License
 * (GPL) Version 2, available from the file COPYING in the main directory of
 * this source tree.
 */

#ifndef _QED_LL2_IF_H
#define _QED_LL2_IF_H

#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/netdevice.h>
#include <linux/pci.h>
#include <linux/skbuff.h>
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/qed/qed_if.h>

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

struct qed_ll2_ops {
/**
 * @brief start - initializes ll2
 *
 * @param cdev
 * @param params - protocol driver configuration for the ll2.
 *
 * @return 0 on success, otherwise error value.
 */
	int (*start)(struct qed_dev *cdev, struct qed_ll2_params *params);

/**
 * @brief stop - stops the ll2
 *
 * @param cdev
 *
 * @return 0 on success, otherwise error value.
 */
	int (*stop)(struct qed_dev *cdev);

/**
 * @brief start_xmit - transmits an skb over the ll2 interface
 *
 * @param cdev
 * @param skb
 *
 * @return 0 on success, otherwise error value.
 */
	int (*start_xmit)(struct qed_dev *cdev, struct sk_buff *skb);

/**
 * @brief register_cb_ops - protocol driver register the callback for Rx/Tx
 * packets. Should be called before `start'.
 *
 * @param cdev
 * @param cookie - to be passed to the callback functions.
 * @param ops - the callback functions to register for Rx / Tx.
 *
 * @return 0 on success, otherwise error value.
 */
	void (*register_cb_ops)(struct qed_dev *cdev,
				const struct qed_ll2_cb_ops *ops,
				void *cookie);

/**
 * @brief get LL2 related statistics
 *
 * @param cdev
 * @param stats - pointer to struct that would be filled with stats
 *
 * @return 0 on success, error otherwise.
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
