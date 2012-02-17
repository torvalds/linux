/*
 *  The NFC Controller Interface is the communication protocol between an
 *  NFC Controller (NFCC) and a Device Host (DH).
 *
 *  Copyright (C) 2011 Texas Instruments, Inc.
 *
 *  Written by Ilan Elias <ilane@ti.com>
 *
 *  Acknowledgements:
 *  This file is based on hci_core.h, which was written
 *  by Maxim Krasnyansky.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2
 *  as published by the Free Software Foundation
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#ifndef __NCI_CORE_H
#define __NCI_CORE_H

#include <linux/interrupt.h>
#include <linux/skbuff.h>

#include <net/nfc/nfc.h>
#include <net/nfc/nci.h>

/* NCI device state */
enum {
	NCI_INIT,
	NCI_UP,
	NCI_DISCOVERY,
	NCI_POLL_ACTIVE,
	NCI_DATA_EXCHANGE,
};

/* NCI timeouts */
#define NCI_RESET_TIMEOUT			5000
#define NCI_INIT_TIMEOUT			5000
#define NCI_RF_DISC_TIMEOUT			5000
#define NCI_RF_DEACTIVATE_TIMEOUT		5000
#define NCI_CMD_TIMEOUT				5000

struct nci_dev;

struct nci_ops {
	int (*open)(struct nci_dev *ndev);
	int (*close)(struct nci_dev *ndev);
	int (*send)(struct sk_buff *skb);
};

#define NCI_MAX_SUPPORTED_RF_INTERFACES		4

/* NCI Core structures */
struct nci_dev {
	struct nfc_dev		*nfc_dev;
	struct nci_ops		*ops;

	int			tx_headroom;
	int			tx_tailroom;

	unsigned long		flags;

	atomic_t		cmd_cnt;
	atomic_t		credits_cnt;

	struct timer_list	cmd_timer;

	struct workqueue_struct	*cmd_wq;
	struct work_struct	cmd_work;

	struct workqueue_struct	*rx_wq;
	struct work_struct	rx_work;

	struct workqueue_struct	*tx_wq;
	struct work_struct	tx_work;

	struct sk_buff_head	cmd_q;
	struct sk_buff_head	rx_q;
	struct sk_buff_head	tx_q;

	struct mutex		req_lock;
	struct completion	req_completion;
	__u32			req_status;
	__u32			req_result;

	void			*driver_data;

	__u32			poll_prots;
	__u32			target_available_prots;
	__u32			target_active_prot;

	/* received during NCI_OP_CORE_RESET_RSP */
	__u8			nci_ver;

	/* received during NCI_OP_CORE_INIT_RSP */
	__u32			nfcc_features;
	__u8			num_supported_rf_interfaces;
	__u8			supported_rf_interfaces
				[NCI_MAX_SUPPORTED_RF_INTERFACES];
	__u8			max_logical_connections;
	__u16			max_routing_table_size;
	__u8			max_ctrl_pkt_payload_len;
	__u16			max_size_for_large_params;
	__u8			manufact_id;
	__u32			manufact_specific_info;

	/* received during NCI_OP_RF_INTF_ACTIVATED_NTF */
	__u8			max_data_pkt_payload_size;
	__u8			initial_num_credits;

	/* stored during nci_data_exchange */
	data_exchange_cb_t	data_exchange_cb;
	void			*data_exchange_cb_context;
	struct sk_buff		*rx_data_reassembly;
};

/* ----- NCI Devices ----- */
struct nci_dev *nci_allocate_device(struct nci_ops *ops,
				__u32 supported_protocols,
				int tx_headroom,
				int tx_tailroom);
void nci_free_device(struct nci_dev *ndev);
int nci_register_device(struct nci_dev *ndev);
void nci_unregister_device(struct nci_dev *ndev);
int nci_recv_frame(struct sk_buff *skb);

static inline struct sk_buff *nci_skb_alloc(struct nci_dev *ndev,
						unsigned int len,
						gfp_t how)
{
	struct sk_buff *skb;

	skb = alloc_skb(len + ndev->tx_headroom + ndev->tx_tailroom, how);
	if (skb)
		skb_reserve(skb, ndev->tx_headroom);

	return skb;
}

static inline void nci_set_parent_dev(struct nci_dev *ndev, struct device *dev)
{
	nfc_set_parent_dev(ndev->nfc_dev, dev);
}

static inline void nci_set_drvdata(struct nci_dev *ndev, void *data)
{
	ndev->driver_data = data;
}

static inline void *nci_get_drvdata(struct nci_dev *ndev)
{
	return ndev->driver_data;
}

void nci_rsp_packet(struct nci_dev *ndev, struct sk_buff *skb);
void nci_ntf_packet(struct nci_dev *ndev, struct sk_buff *skb);
void nci_rx_data_packet(struct nci_dev *ndev, struct sk_buff *skb);
int nci_send_cmd(struct nci_dev *ndev, __u16 opcode, __u8 plen, void *payload);
int nci_send_data(struct nci_dev *ndev, __u8 conn_id, struct sk_buff *skb);
void nci_data_exchange_complete(struct nci_dev *ndev, struct sk_buff *skb,
				int err);

/* ----- NCI requests ----- */
#define NCI_REQ_DONE		0
#define NCI_REQ_PEND		1
#define NCI_REQ_CANCELED	2

void nci_req_complete(struct nci_dev *ndev, int result);

/* ----- NCI status code ----- */
int nci_to_errno(__u8 code);

#endif /* __NCI_CORE_H */
