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

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>

#include "nfc.h"

#define VERSION "0.1"

int nfc_devlist_generation;
DEFINE_MUTEX(nfc_devlist_mutex);

int nfc_printk(const char *level, const char *format, ...)
{
	struct va_format vaf;
	va_list args;
	int r;

	va_start(args, format);

	vaf.fmt = format;
	vaf.va = &args;

	r = printk("%sNFC: %pV\n", level, &vaf);

	va_end(args);

	return r;
}
EXPORT_SYMBOL(nfc_printk);

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

	nfc_dbg("dev_name=%s protocols=0x%x", dev_name(&dev->dev), protocols);

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

	nfc_dbg("dev_name=%s", dev_name(&dev->dev));

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

	nfc_dbg("dev_name=%s target_idx=%u protocol=%u", dev_name(&dev->dev),
							target_idx, protocol);

	device_lock(&dev->dev);

	if (!device_is_registered(&dev->dev)) {
		rc = -ENODEV;
		goto error;
	}

	rc = dev->ops->activate_target(dev, target_idx, protocol);

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

	nfc_dbg("dev_name=%s target_idx=%u", dev_name(&dev->dev), target_idx);

	device_lock(&dev->dev);

	if (!device_is_registered(&dev->dev)) {
		rc = -ENODEV;
		goto error;
	}

	dev->ops->deactivate_target(dev, target_idx);

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
int nfc_data_exchange(struct nfc_dev *dev, u32 target_idx,
					struct sk_buff *skb,
					data_exchange_cb_t cb,
					void *cb_context)
{
	int rc;

	nfc_dbg("dev_name=%s target_idx=%u skb->len=%u", dev_name(&dev->dev),
							target_idx, skb->len);

	device_lock(&dev->dev);

	if (!device_is_registered(&dev->dev)) {
		rc = -ENODEV;
		kfree_skb(skb);
		goto error;
	}

	rc = dev->ops->data_exchange(dev, target_idx, skb, cb, cb_context);

error:
	device_unlock(&dev->dev);
	return rc;
}

/**
 * nfc_alloc_skb - allocate a skb for data exchange responses
 *
 * @size: size to allocate
 * @gfp: gfp flags
 */
struct sk_buff *nfc_alloc_skb(unsigned int size, gfp_t gfp)
{
	struct sk_buff *skb;
	unsigned int total_size;

	total_size = size + 1;
	skb = alloc_skb(total_size, gfp);

	if (skb)
		skb_reserve(skb, 1);

	return skb;
}
EXPORT_SYMBOL(nfc_alloc_skb);

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
int nfc_targets_found(struct nfc_dev *dev, struct nfc_target *targets,
							int n_targets)
{
	int i;

	nfc_dbg("dev_name=%s n_targets=%d", dev_name(&dev->dev), n_targets);

	dev->polling = false;

	for (i = 0; i < n_targets; i++)
		targets[i].idx = dev->target_idx++;

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

static void nfc_release(struct device *d)
{
	struct nfc_dev *dev = to_nfc_dev(d);

	nfc_dbg("dev_name=%s", dev_name(&dev->dev));

	nfc_genl_data_exit(&dev->genl_data);
	kfree(dev->targets);
	kfree(dev);
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
					int tx_headroom,
					int tx_tailroom)
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

	nfc_dbg("dev_name=%s", dev_name(&dev->dev));

	mutex_lock(&nfc_devlist_mutex);
	nfc_devlist_generation++;
	rc = device_add(&dev->dev);
	mutex_unlock(&nfc_devlist_mutex);

	if (rc < 0)
		return rc;

	rc = nfc_genl_device_added(dev);
	if (rc)
		nfc_dbg("The userspace won't be notified that the device %s was"
						" added", dev_name(&dev->dev));


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

	nfc_dbg("dev_name=%s", dev_name(&dev->dev));

	mutex_lock(&nfc_devlist_mutex);
	nfc_devlist_generation++;

	/* lock to avoid unregistering a device while an operation
	   is in progress */
	device_lock(&dev->dev);
	device_del(&dev->dev);
	device_unlock(&dev->dev);

	mutex_unlock(&nfc_devlist_mutex);

	rc = nfc_genl_device_removed(dev);
	if (rc)
		nfc_dbg("The userspace won't be notified that the device %s"
					" was removed", dev_name(&dev->dev));

}
EXPORT_SYMBOL(nfc_unregister_device);

static int __init nfc_init(void)
{
	int rc;

	nfc_info("NFC Core ver %s", VERSION);

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

	rc = af_nfc_init();
	if (rc)
		goto err_af_nfc;

	return 0;

err_af_nfc:
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
