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
	int (*start_poll)(struct nfc_dev *dev, u32 protocols);
	void (*stop_poll)(struct nfc_dev *dev);
	int (*activate_target)(struct nfc_dev *dev, u32 target_idx,
							u32 protocol);
	void (*deactivate_target)(struct nfc_dev *dev, u32 target_idx);
	int (*data_exchange)(struct nfc_dev *dev, u32 target_idx,
				struct sk_buff *skb, data_exchange_cb_t cb,
							void *cb_context);
};

struct nfc_target {
	u32 idx;
	u32 supported_protocols;
	u16 sens_res;
	u8 sel_res;
};

struct nfc_genl_data {
	u32 poll_req_pid;
	struct mutex genl_data_mutex;
};

struct nfc_dev {
	unsigned idx;
	unsigned target_idx;
	struct nfc_target *targets;
	int n_targets;
	int targets_generation;
	spinlock_t targets_lock;
	struct device dev;
	bool polling;
	struct nfc_genl_data genl_data;
	u32 supported_protocols;

	struct nfc_ops *ops;
};
#define to_nfc_dev(_dev) container_of(_dev, struct nfc_dev, dev)

extern struct class nfc_class;

struct nfc_dev *nfc_allocate_device(struct nfc_ops *ops,
					u32 supported_protocols);

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

struct sk_buff *nfc_alloc_skb(unsigned int size, gfp_t gfp);

int nfc_targets_found(struct nfc_dev *dev, struct nfc_target *targets,
							int ntargets);

#endif /* __NET_NFC_H */
