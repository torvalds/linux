/*
 * NFC Digital Protocol stack
 * Copyright (c) 2013, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */

#include <linux/module.h>

#include "digital.h"

static int digital_start_poll(struct nfc_dev *nfc_dev, __u32 im_protocols,
			      __u32 tm_protocols)
{
	return -EOPNOTSUPP;
}

static void digital_stop_poll(struct nfc_dev *nfc_dev)
{
}

static int digital_dev_up(struct nfc_dev *nfc_dev)
{
	return -EOPNOTSUPP;
}

static int digital_dev_down(struct nfc_dev *nfc_dev)
{
	return -EOPNOTSUPP;
}

static int digital_dep_link_up(struct nfc_dev *nfc_dev,
			       struct nfc_target *target,
			       __u8 comm_mode, __u8 *gb, size_t gb_len)
{
	return -EOPNOTSUPP;
}

static int digital_dep_link_down(struct nfc_dev *nfc_dev)
{
	return -EOPNOTSUPP;
}

static int digital_activate_target(struct nfc_dev *nfc_dev,
				   struct nfc_target *target, __u32 protocol)
{
	return -EOPNOTSUPP;
}

static void digital_deactivate_target(struct nfc_dev *nfc_dev,
				      struct nfc_target *target)
{
}

static int digital_tg_send(struct nfc_dev *dev, struct sk_buff *skb)
{
	return -EOPNOTSUPP;
}

static int digital_in_send(struct nfc_dev *nfc_dev, struct nfc_target *target,
			   struct sk_buff *skb, data_exchange_cb_t cb,
			   void *cb_context)
{
	return -EOPNOTSUPP;
}

static struct nfc_ops digital_nfc_ops = {
	.dev_up = digital_dev_up,
	.dev_down = digital_dev_down,
	.start_poll = digital_start_poll,
	.stop_poll = digital_stop_poll,
	.dep_link_up = digital_dep_link_up,
	.dep_link_down = digital_dep_link_down,
	.activate_target = digital_activate_target,
	.deactivate_target = digital_deactivate_target,
	.tm_send = digital_tg_send,
	.im_transceive = digital_in_send,
};

struct nfc_digital_dev *nfc_digital_allocate_device(struct nfc_digital_ops *ops,
					    __u32 supported_protocols,
					    __u32 driver_capabilities,
					    int tx_headroom, int tx_tailroom)
{
	struct nfc_digital_dev *ddev;

	if (!ops->in_configure_hw || !ops->in_send_cmd || !ops->tg_listen ||
	    !ops->tg_configure_hw || !ops->tg_send_cmd || !ops->abort_cmd ||
	    !ops->switch_rf)
		return NULL;

	ddev = kzalloc(sizeof(struct nfc_digital_dev), GFP_KERNEL);
	if (!ddev) {
		PR_ERR("kzalloc failed");
		return NULL;
	}

	ddev->driver_capabilities = driver_capabilities;
	ddev->ops = ops;

	ddev->tx_headroom = tx_headroom;
	ddev->tx_tailroom = tx_tailroom;

	ddev->nfc_dev = nfc_allocate_device(&digital_nfc_ops, ddev->protocols,
					    ddev->tx_headroom,
					    ddev->tx_tailroom);
	if (!ddev->nfc_dev) {
		PR_ERR("nfc_allocate_device failed");
		goto free_dev;
	}

	nfc_set_drvdata(ddev->nfc_dev, ddev);

	return ddev;

free_dev:
	kfree(ddev);

	return NULL;
}
EXPORT_SYMBOL(nfc_digital_allocate_device);

void nfc_digital_free_device(struct nfc_digital_dev *ddev)
{
	nfc_free_device(ddev->nfc_dev);

	kfree(ddev);
}
EXPORT_SYMBOL(nfc_digital_free_device);

int nfc_digital_register_device(struct nfc_digital_dev *ddev)
{
	return nfc_register_device(ddev->nfc_dev);
}
EXPORT_SYMBOL(nfc_digital_register_device);

void nfc_digital_unregister_device(struct nfc_digital_dev *ddev)
{
	nfc_unregister_device(ddev->nfc_dev);
}
EXPORT_SYMBOL(nfc_digital_unregister_device);

MODULE_LICENSE("GPL");
