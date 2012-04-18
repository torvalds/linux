/*
 * Copyright (C) 2011 Instituto Nokia de Tecnologia
 *
 * Authors:
 *    Lauro Ramos Venancio <lauro.venancio@openbossa.org>
 *    Aloisio Almeida Jr <aloisio.almeida@openbossa.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the
 * Free Software Foundation, Inc.,
 * 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef __NET_NFC_H
#define __NET_NFC_H

#include <linux/nfc.h>
#include <linux/device.h>
#include <linux/skbuff.h>

#define nfc_dev_info(dev, fmt, arg...) dev_info((dev), "NFC: " fmt "\n", ## arg)
#define nfc_dev_err(dev, fmt, arg...) dev_err((dev), "NFC: " fmt "\n", ## arg)
#define nfc_dev_dbg(dev, fmt, arg...) dev_dbg((dev), fmt "\n", ## arg)

struct nfc_dev;

/**
 * data_exchange_cb_t - Definition of nfc_data_exchange callback
 *
 * @context: nfc_data_exchange cb_context parameter
 * @skb: response data
 * @err: If an error has occurred during data exchange, it is the
 *	error number. Zero means no error.
 *
 * When a rx or tx package is lost or corrupted or the target gets out
 * of the operating field, err is -EIO.
 */
typedef void (*data_exchange_cb_t)(void *context, struct sk_buff *skb,
								int err);

struct nfc_ops {
	int (*dev_up)(struct nfc_dev *dev);
	int (*dev_down)(struct nfc_dev *dev);
	int (*start_poll)(struct nfc_dev *dev, u32 protocols);
	void (*stop_poll)(struct nfc_dev *dev);
	int (*dep_link_up)(struct nfc_dev *dev, int target_idx, u8 comm_mode,
			   u8 *gb, size_t gb_len);
	int (*dep_link_down)(struct nfc_dev *dev);
	int (*activate_target)(struct nfc_dev *dev, u32 target_idx,
			       u32 protocol);
	void (*deactivate_target)(struct nfc_dev *dev, u32 target_idx);
	int (*data_exchange)(struct nfc_dev *dev, u32 target_idx,
			     struct sk_buff *skb, data_exchange_cb_t cb,
			     void *cb_context);
	int (*check_presence)(struct nfc_dev *dev, u32 target_idx);
};

#define NFC_TARGET_IDX_ANY -1
#define NFC_MAX_GT_LEN 48
#define NFC_TARGET_IDX_NONE 0xffffffff

struct nfc_target {
	u32 idx;
	u32 supported_protocols;
	u16 sens_res;
	u8 sel_res;
	u8 nfcid1_len;
	u8 nfcid1[NFC_NFCID1_MAXSIZE];
	u8 sensb_res_len;
	u8 sensb_res[NFC_SENSB_RES_MAXSIZE];
	u8 sensf_res_len;
	u8 sensf_res[NFC_SENSF_RES_MAXSIZE];
	u8 hci_reader_gate;
	u8 logical_idx;
};

struct nfc_genl_data {
	u32 poll_req_pid;
	struct mutex genl_data_mutex;
};

struct nfc_dev {
	unsigned int idx;
	u32 target_next_idx;
	struct nfc_target *targets;
	int n_targets;
	int targets_generation;
	spinlock_t targets_lock;
	struct device dev;
	bool dev_up;
	bool polling;
	u32 activated_target_idx;
	bool dep_link_up;
	u32 dep_rf_mode;
	struct nfc_genl_data genl_data;
	u32 supported_protocols;

	int tx_headroom;
	int tx_tailroom;

	struct timer_list check_pres_timer;
	struct workqueue_struct *check_pres_wq;
	struct work_struct check_pres_work;

	struct nfc_ops *ops;
};
#define to_nfc_dev(_dev) container_of(_dev, struct nfc_dev, dev)

extern struct class nfc_class;

struct nfc_dev *nfc_allocate_device(struct nfc_ops *ops,
				    u32 supported_protocols,
				    int tx_headroom,
				    int tx_tailroom);

/**
 * nfc_free_device - free nfc device
 *
 * @dev: The nfc device to free
 */
static inline void nfc_free_device(struct nfc_dev *dev)
{
	put_device(&dev->dev);
}

int nfc_register_device(struct nfc_dev *dev);

void nfc_unregister_device(struct nfc_dev *dev);

/**
 * nfc_set_parent_dev - set the parent device
 *
 * @nfc_dev: The nfc device whose parent is being set
 * @dev: The parent device
 */
static inline void nfc_set_parent_dev(struct nfc_dev *nfc_dev,
				      struct device *dev)
{
	nfc_dev->dev.parent = dev;
}

/**
 * nfc_set_drvdata - set driver specifc data
 *
 * @dev: The nfc device
 * @data: Pointer to driver specifc data
 */
static inline void nfc_set_drvdata(struct nfc_dev *dev, void *data)
{
	dev_set_drvdata(&dev->dev, data);
}

/**
 * nfc_get_drvdata - get driver specifc data
 *
 * @dev: The nfc device
 */
static inline void *nfc_get_drvdata(struct nfc_dev *dev)
{
	return dev_get_drvdata(&dev->dev);
}

/**
 * nfc_device_name - get the nfc device name
 *
 * @dev: The nfc device whose name to return
 */
static inline const char *nfc_device_name(struct nfc_dev *dev)
{
	return dev_name(&dev->dev);
}

struct sk_buff *nfc_alloc_send_skb(struct nfc_dev *dev, struct sock *sk,
				   unsigned int flags, unsigned int size,
				   unsigned int *err);
struct sk_buff *nfc_alloc_recv_skb(unsigned int size, gfp_t gfp);

int nfc_set_remote_general_bytes(struct nfc_dev *dev,
				 u8 *gt, u8 gt_len);

int nfc_targets_found(struct nfc_dev *dev,
		      struct nfc_target *targets, int ntargets);
int nfc_target_lost(struct nfc_dev *dev, u32 target_idx);

int nfc_dep_link_is_up(struct nfc_dev *dev, u32 target_idx,
		       u8 comm_mode, u8 rf_mode);

#endif /* __NET_NFC_H */
