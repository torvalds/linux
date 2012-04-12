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

#define pr_fmt(fmt) KBUILD_MODNAME ": %s: " fmt, __func__

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/nfc.h>

#include "nfc.h"

#define VERSION "0.1"

#define NFC_CHECK_PRES_FREQ_MS	2000

int nfc_devlist_generation;
DEFINE_MUTEX(nfc_devlist_mutex);

/**
 * nfc_dev_up - turn on the NFC device
 *
 * @dev: The nfc device to be turned on
 *
 * The device remains up until the nfc_dev_down function is called.
 */
int nfc_dev_up(struct nfc_dev *dev)
{
	int rc = 0;

	pr_debug("dev_name=%s\n", dev_name(&dev->dev));

	device_lock(&dev->dev);

	if (!device_is_registered(&dev->dev)) {
		rc = -ENODEV;
		goto error;
	}

	if (dev->dev_up) {
		rc = -EALREADY;
		goto error;
	}

	if (dev->ops->dev_up)
		rc = dev->ops->dev_up(dev);

	if (!rc)
		dev->dev_up = true;

error:
	device_unlock(&dev->dev);
	return rc;
}

/**
 * nfc_dev_down - turn off the NFC device
 *
 * @dev: The nfc device to be turned off
 */
int nfc_dev_down(struct nfc_dev *dev)
{
	int rc = 0;

	pr_debug("dev_name=%s\n", dev_name(&dev->dev));

	device_lock(&dev->dev);

	if (!device_is_registered(&dev->dev)) {
		rc = -ENODEV;
		goto error;
	}

	if (!dev->dev_up) {
		rc = -EALREADY;
		goto error;
	}

	if (dev->polling || dev->activated_target_idx != NFC_TARGET_IDX_NONE) {
		rc = -EBUSY;
		goto error;
	}

	if (dev->ops->dev_down)
		dev->ops->dev_down(dev);

	dev->dev_up = false;

error:
	device_unlock(&dev->dev);
	return rc;
}

/**
 * nfc_start_poll - start polling for nfc targets
 *
 * @dev: The nfc device that must start polling
 * @protocols: bitset of nfc protocols that must be used for polling
 *
 * The device remains polling for targets until a target is found or
 * the nfc_stop_poll function is called.
 */
int nfc_start_poll(struct nfc_dev *dev, u32 protocols)
{
	int rc;

	pr_debug("dev_name=%s protocols=0x%x\n",
		 dev_name(&dev->dev), protocols);

	if (!protocols)
		return -EINVAL;

	device_lock(&dev->dev);

	if (!device_is_registered(&dev->dev)) {
		rc = -ENODEV;
		goto error;
	}

	if (dev->polling) {
		rc = -EBUSY;
		goto error;
	}

	rc = dev->ops->start_poll(dev, protocols);
	if (!rc)
		dev->polling = true;

error:
	device_unlock(&dev->dev);
	return rc;
}

/**
 * nfc_stop_poll - stop polling for nfc targets
 *
 * @dev: The nfc device that must stop polling
 */
int nfc_stop_poll(struct nfc_dev *dev)
{
	int rc = 0;

	pr_debug("dev_name=%s\n", dev_name(&dev->dev));

	device_lock(&dev->dev);

	if (!device_is_registered(&dev->dev)) {
		rc = -ENODEV;
		goto error;
	}

	if (!dev->polling) {
		rc = -EINVAL;
		goto error;
	}

	dev->ops->stop_poll(dev);
	dev->polling = false;

error:
	device_unlock(&dev->dev);
	return rc;
}

int nfc_dep_link_up(struct nfc_dev *dev, int target_index, u8 comm_mode)
{
	int rc = 0;
	u8 *gb;
	size_t gb_len;

	pr_debug("dev_name=%s comm %d\n", dev_name(&dev->dev), comm_mode);

	if (!dev->ops->dep_link_up)
		return -EOPNOTSUPP;

	device_lock(&dev->dev);

	if (!device_is_registered(&dev->dev)) {
		rc = -ENODEV;
		goto error;
	}

	if (dev->dep_link_up == true) {
		rc = -EALREADY;
		goto error;
	}

	gb = nfc_llcp_general_bytes(dev, &gb_len);
	if (gb_len > NFC_MAX_GT_LEN) {
		rc = -EINVAL;
		goto error;
	}

	rc = dev->ops->dep_link_up(dev, target_index, comm_mode, gb, gb_len);
	if (!rc)
		dev->activated_target_idx = target_index;

error:
	device_unlock(&dev->dev);
	return rc;
}

int nfc_dep_link_down(struct nfc_dev *dev)
{
	int rc = 0;

	pr_debug("dev_name=%s\n", dev_name(&dev->dev));

	if (!dev->ops->dep_link_down)
		return -EOPNOTSUPP;

	device_lock(&dev->dev);

	if (!device_is_registered(&dev->dev)) {
		rc = -ENODEV;
		goto error;
	}

	if (dev->dep_link_up == false) {
		rc = -EALREADY;
		goto error;
	}

	if (dev->dep_rf_mode == NFC_RF_TARGET) {
		rc = -EOPNOTSUPP;
		goto error;
	}

	rc = dev->ops->dep_link_down(dev);
	if (!rc) {
		dev->dep_link_up = false;
		dev->activated_target_idx = NFC_TARGET_IDX_NONE;
		nfc_llcp_mac_is_down(dev);
		nfc_genl_dep_link_down_event(dev);
	}

error:
	device_unlock(&dev->dev);
	return rc;
}

int nfc_dep_link_is_up(struct nfc_dev *dev, u32 target_idx,
		       u8 comm_mode, u8 rf_mode)
{
	dev->dep_link_up = true;
	dev->dep_rf_mode = rf_mode;

	nfc_llcp_mac_is_up(dev, target_idx, comm_mode, rf_mode);

	return nfc_genl_dep_link_up_event(dev, target_idx, comm_mode, rf_mode);
}
EXPORT_SYMBOL(nfc_dep_link_is_up);

/**
 * nfc_activate_target - prepare the target for data exchange
 *
 * @dev: The nfc device that found the target
 * @target_idx: index of the target that must be activated
 * @protocol: nfc protocol that will be used for data exchange
 */
int nfc_activate_target(struct nfc_dev *dev, u32 target_idx, u32 protocol)
{
	int rc;

	pr_debug("dev_name=%s target_idx=%u protocol=%u\n",
		 dev_name(&dev->dev), target_idx, protocol);

	device_lock(&dev->dev);

	if (!device_is_registered(&dev->dev)) {
		rc = -ENODEV;
		goto error;
	}

	rc = dev->ops->activate_target(dev, target_idx, protocol);
	if (!rc) {
		dev->activated_target_idx = target_idx;

		if (dev->ops->check_presence)
			mod_timer(&dev->check_pres_timer, jiffies +
				  msecs_to_jiffies(NFC_CHECK_PRES_FREQ_MS));
	}

error:
	device_unlock(&dev->dev);
	return rc;
}

/**
 * nfc_deactivate_target - deactivate a nfc target
 *
 * @dev: The nfc device that found the target
 * @target_idx: index of the target that must be deactivated
 */
int nfc_deactivate_target(struct nfc_dev *dev, u32 target_idx)
{
	int rc = 0;

	pr_debug("dev_name=%s target_idx=%u\n",
		 dev_name(&dev->dev), target_idx);

	device_lock(&dev->dev);

	if (!device_is_registered(&dev->dev)) {
		rc = -ENODEV;
		goto error;
	}

	if (dev->ops->check_presence)
		del_timer_sync(&dev->check_pres_timer);

	dev->ops->deactivate_target(dev, target_idx);
	dev->activated_target_idx = NFC_TARGET_IDX_NONE;

error:
	device_unlock(&dev->dev);
	return rc;
}

/**
 * nfc_data_exchange - transceive data
 *
 * @dev: The nfc device that found the target
 * @target_idx: index of the target
 * @skb: data to be sent
 * @cb: callback called when the response is received
 * @cb_context: parameter for the callback function
 *
 * The user must wait for the callback before calling this function again.
 */
int nfc_data_exchange(struct nfc_dev *dev, u32 target_idx, struct sk_buff *skb,
		      data_exchange_cb_t cb, void *cb_context)
{
	int rc;

	pr_debug("dev_name=%s target_idx=%u skb->len=%u\n",
		 dev_name(&dev->dev), target_idx, skb->len);

	device_lock(&dev->dev);

	if (!device_is_registered(&dev->dev)) {
		rc = -ENODEV;
		kfree_skb(skb);
		goto error;
	}

	if (dev->activated_target_idx == NFC_TARGET_IDX_NONE) {
		rc = -ENOTCONN;
		kfree_skb(skb);
		goto error;
	}

	if (target_idx != dev->activated_target_idx) {
		rc = -EADDRNOTAVAIL;
		kfree_skb(skb);
		goto error;
	}

	if (dev->ops->check_presence)
		del_timer_sync(&dev->check_pres_timer);

	rc = dev->ops->data_exchange(dev, target_idx, skb, cb, cb_context);

	if (!rc && dev->ops->check_presence)
		mod_timer(&dev->check_pres_timer, jiffies +
			  msecs_to_jiffies(NFC_CHECK_PRES_FREQ_MS));

error:
	device_unlock(&dev->dev);
	return rc;
}

int nfc_set_remote_general_bytes(struct nfc_dev *dev, u8 *gb, u8 gb_len)
{
	pr_debug("dev_name=%s gb_len=%d\n", dev_name(&dev->dev), gb_len);

	if (gb_len > NFC_MAX_GT_LEN)
		return -EINVAL;

	return nfc_llcp_set_remote_gb(dev, gb, gb_len);
}
EXPORT_SYMBOL(nfc_set_remote_general_bytes);

/**
 * nfc_alloc_send_skb - allocate a skb for data exchange responses
 *
 * @size: size to allocate
 * @gfp: gfp flags
 */
struct sk_buff *nfc_alloc_send_skb(struct nfc_dev *dev, struct sock *sk,
				   unsigned int flags, unsigned int size,
				   unsigned int *err)
{
	struct sk_buff *skb;
	unsigned int total_size;

	total_size = size +
		dev->tx_headroom + dev->tx_tailroom + NFC_HEADER_SIZE;

	skb = sock_alloc_send_skb(sk, total_size, flags & MSG_DONTWAIT, err);
	if (skb)
		skb_reserve(skb, dev->tx_headroom + NFC_HEADER_SIZE);

	return skb;
}

/**
 * nfc_alloc_recv_skb - allocate a skb for data exchange responses
 *
 * @size: size to allocate
 * @gfp: gfp flags
 */
struct sk_buff *nfc_alloc_recv_skb(unsigned int size, gfp_t gfp)
{
	struct sk_buff *skb;
	unsigned int total_size;

	total_size = size + 1;
	skb = alloc_skb(total_size, gfp);

	if (skb)
		skb_reserve(skb, 1);

	return skb;
}
EXPORT_SYMBOL(nfc_alloc_recv_skb);

/**
 * nfc_targets_found - inform that targets were found
 *
 * @dev: The nfc device that found the targets
 * @targets: array of nfc targets found
 * @ntargets: targets array size
 *
 * The device driver must call this function when one or many nfc targets
 * are found. After calling this function, the device driver must stop
 * polling for targets.
 */
int nfc_targets_found(struct nfc_dev *dev,
		      struct nfc_target *targets, int n_targets)
{
	int i;

	pr_debug("dev_name=%s n_targets=%d\n", dev_name(&dev->dev), n_targets);

	dev->polling = false;

	for (i = 0; i < n_targets; i++)
		targets[i].idx = dev->target_next_idx++;

	spin_lock_bh(&dev->targets_lock);

	dev->targets_generation++;

	kfree(dev->targets);
	dev->targets = kmemdup(targets, n_targets * sizeof(struct nfc_target),
			       GFP_ATOMIC);

	if (!dev->targets) {
		dev->n_targets = 0;
		spin_unlock_bh(&dev->targets_lock);
		return -ENOMEM;
	}

	dev->n_targets = n_targets;
	spin_unlock_bh(&dev->targets_lock);

	nfc_genl_targets_found(dev);

	return 0;
}
EXPORT_SYMBOL(nfc_targets_found);

int nfc_target_lost(struct nfc_dev *dev, u32 target_idx)
{
	struct nfc_target *tg;
	int i;

	pr_debug("dev_name %s n_target %d\n", dev_name(&dev->dev), target_idx);

	spin_lock_bh(&dev->targets_lock);

	for (i = 0; i < dev->n_targets; i++) {
		tg = &dev->targets[i];
		if (tg->idx == target_idx)
			break;
	}

	if (i == dev->n_targets) {
		spin_unlock_bh(&dev->targets_lock);
		return -EINVAL;
	}

	dev->targets_generation++;
	dev->n_targets--;
	dev->activated_target_idx = NFC_TARGET_IDX_NONE;

	if (dev->n_targets) {
		memcpy(&dev->targets[i], &dev->targets[i + 1],
		       (dev->n_targets - i) * sizeof(struct nfc_target));
	} else {
		kfree(dev->targets);
		dev->targets = NULL;
	}

	spin_unlock_bh(&dev->targets_lock);

	nfc_genl_target_lost(dev, target_idx);

	return 0;
}
EXPORT_SYMBOL(nfc_target_lost);

static void nfc_release(struct device *d)
{
	struct nfc_dev *dev = to_nfc_dev(d);

	pr_debug("dev_name=%s\n", dev_name(&dev->dev));

	if (dev->ops->check_presence) {
		del_timer_sync(&dev->check_pres_timer);
		destroy_workqueue(dev->check_pres_wq);
	}

	nfc_genl_data_exit(&dev->genl_data);
	kfree(dev->targets);
	kfree(dev);
}

static void nfc_check_pres_work(struct work_struct *work)
{
	struct nfc_dev *dev = container_of(work, struct nfc_dev,
					   check_pres_work);
	int rc;

	device_lock(&dev->dev);

	if (dev->activated_target_idx != NFC_TARGET_IDX_NONE &&
	    timer_pending(&dev->check_pres_timer) == 0) {
		rc = dev->ops->check_presence(dev, dev->activated_target_idx);
		if (!rc) {
			mod_timer(&dev->check_pres_timer, jiffies +
				  msecs_to_jiffies(NFC_CHECK_PRES_FREQ_MS));
		} else {
			nfc_target_lost(dev, dev->activated_target_idx);
			dev->activated_target_idx = NFC_TARGET_IDX_NONE;
		}
	}

	device_unlock(&dev->dev);
}

static void nfc_check_pres_timeout(unsigned long data)
{
	struct nfc_dev *dev = (struct nfc_dev *)data;

	queue_work(dev->check_pres_wq, &dev->check_pres_work);
}

struct class nfc_class = {
	.name = "nfc",
	.dev_release = nfc_release,
};
EXPORT_SYMBOL(nfc_class);

static int match_idx(struct device *d, void *data)
{
	struct nfc_dev *dev = to_nfc_dev(d);
	unsigned *idx = data;

	return dev->idx == *idx;
}

struct nfc_dev *nfc_get_device(unsigned idx)
{
	struct device *d;

	d = class_find_device(&nfc_class, NULL, &idx, match_idx);
	if (!d)
		return NULL;

	return to_nfc_dev(d);
}

/**
 * nfc_allocate_device - allocate a new nfc device
 *
 * @ops: device operations
 * @supported_protocols: NFC protocols supported by the device
 */
struct nfc_dev *nfc_allocate_device(struct nfc_ops *ops,
				    u32 supported_protocols,
				    int tx_headroom, int tx_tailroom)
{
	static atomic_t dev_no = ATOMIC_INIT(0);
	struct nfc_dev *dev;

	if (!ops->start_poll || !ops->stop_poll || !ops->activate_target ||
	    !ops->deactivate_target || !ops->data_exchange)
		return NULL;

	if (!supported_protocols)
		return NULL;

	dev = kzalloc(sizeof(struct nfc_dev), GFP_KERNEL);
	if (!dev)
		return NULL;

	dev->dev.class = &nfc_class;
	dev->idx = atomic_inc_return(&dev_no) - 1;
	dev_set_name(&dev->dev, "nfc%d", dev->idx);
	device_initialize(&dev->dev);

	dev->ops = ops;
	dev->supported_protocols = supported_protocols;
	dev->tx_headroom = tx_headroom;
	dev->tx_tailroom = tx_tailroom;

	spin_lock_init(&dev->targets_lock);
	nfc_genl_data_init(&dev->genl_data);

	/* first generation must not be 0 */
	dev->targets_generation = 1;

	dev->activated_target_idx = NFC_TARGET_IDX_NONE;

	if (ops->check_presence) {
		char name[32];
		init_timer(&dev->check_pres_timer);
		dev->check_pres_timer.data = (unsigned long)dev;
		dev->check_pres_timer.function = nfc_check_pres_timeout;

		INIT_WORK(&dev->check_pres_work, nfc_check_pres_work);
		snprintf(name, sizeof(name), "nfc%d_check_pres_wq", dev->idx);
		dev->check_pres_wq = alloc_workqueue(name, WQ_NON_REENTRANT |
						     WQ_UNBOUND |
						     WQ_MEM_RECLAIM, 1);
		if (dev->check_pres_wq == NULL) {
			kfree(dev);
			return NULL;
		}
	}


	return dev;
}
EXPORT_SYMBOL(nfc_allocate_device);

/**
 * nfc_register_device - register a nfc device in the nfc subsystem
 *
 * @dev: The nfc device to register
 */
int nfc_register_device(struct nfc_dev *dev)
{
	int rc;

	pr_debug("dev_name=%s\n", dev_name(&dev->dev));

	mutex_lock(&nfc_devlist_mutex);
	nfc_devlist_generation++;
	rc = device_add(&dev->dev);
	mutex_unlock(&nfc_devlist_mutex);

	if (rc < 0)
		return rc;

	rc = nfc_llcp_register_device(dev);
	if (rc)
		pr_err("Could not register llcp device\n");

	rc = nfc_genl_device_added(dev);
	if (rc)
		pr_debug("The userspace won't be notified that the device %s was added\n",
			 dev_name(&dev->dev));

	return 0;
}
EXPORT_SYMBOL(nfc_register_device);

/**
 * nfc_unregister_device - unregister a nfc device in the nfc subsystem
 *
 * @dev: The nfc device to unregister
 */
void nfc_unregister_device(struct nfc_dev *dev)
{
	int rc;

	pr_debug("dev_name=%s\n", dev_name(&dev->dev));

	mutex_lock(&nfc_devlist_mutex);
	nfc_devlist_generation++;

	/* lock to avoid unregistering a device while an operation
	   is in progress */
	device_lock(&dev->dev);
	device_del(&dev->dev);
	device_unlock(&dev->dev);

	mutex_unlock(&nfc_devlist_mutex);

	nfc_llcp_unregister_device(dev);

	rc = nfc_genl_device_removed(dev);
	if (rc)
		pr_debug("The userspace won't be notified that the device %s was removed\n",
			 dev_name(&dev->dev));

}
EXPORT_SYMBOL(nfc_unregister_device);

static int __init nfc_init(void)
{
	int rc;

	pr_info("NFC Core ver %s\n", VERSION);

	rc = class_register(&nfc_class);
	if (rc)
		return rc;

	rc = nfc_genl_init();
	if (rc)
		goto err_genl;

	/* the first generation must not be 0 */
	nfc_devlist_generation = 1;

	rc = rawsock_init();
	if (rc)
		goto err_rawsock;

	rc = nfc_llcp_init();
	if (rc)
		goto err_llcp_sock;

	rc = af_nfc_init();
	if (rc)
		goto err_af_nfc;

	return 0;

err_af_nfc:
	nfc_llcp_exit();
err_llcp_sock:
	rawsock_exit();
err_rawsock:
	nfc_genl_exit();
err_genl:
	class_unregister(&nfc_class);
	return rc;
}

static void __exit nfc_exit(void)
{
	af_nfc_exit();
	nfc_llcp_exit();
	rawsock_exit();
	nfc_genl_exit();
	class_unregister(&nfc_class);
}

subsys_initcall(nfc_init);
module_exit(nfc_exit);

MODULE_AUTHOR("Lauro Ramos Venancio <lauro.venancio@openbossa.org>");
MODULE_DESCRIPTION("NFC Core ver " VERSION);
MODULE_VERSION(VERSION);
MODULE_LICENSE("GPL");
