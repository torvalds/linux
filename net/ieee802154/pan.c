// SPDX-License-Identifier: GPL-2.0
/*
 * IEEE 802.15.4 PAN management
 *
 * Copyright (C) 2023 Qorvo US, Inc
 * Authors:
 *   - David Girault <david.girault@qorvo.com>
 *   - Miquel Raynal <miquel.raynal@bootlin.com>
 */

#include <linux/kernel.h>
#include <net/cfg802154.h>
#include <net/af_ieee802154.h>

/* Checks whether a device address matches one from the PAN list.
 * This helper is meant to be used only during PAN management, when we expect
 * extended addresses to be used.
 */
static bool cfg802154_pan_device_is_matching(struct ieee802154_pan_device *pan_dev,
					     struct ieee802154_addr *ext_dev)
{
	if (!pan_dev || !ext_dev)
		return false;

	if (ext_dev->mode == IEEE802154_ADDR_SHORT)
		return false;

	return pan_dev->extended_addr == ext_dev->extended_addr;
}

bool cfg802154_device_is_associated(struct wpan_dev *wpan_dev)
{
	bool is_assoc;

	mutex_lock(&wpan_dev->association_lock);
	is_assoc = !list_empty(&wpan_dev->children) || wpan_dev->parent;
	mutex_unlock(&wpan_dev->association_lock);

	return is_assoc;
}

bool cfg802154_device_is_parent(struct wpan_dev *wpan_dev,
				struct ieee802154_addr *target)
{
	lockdep_assert_held(&wpan_dev->association_lock);

	return cfg802154_pan_device_is_matching(wpan_dev->parent, target);
}
EXPORT_SYMBOL_GPL(cfg802154_device_is_parent);

struct ieee802154_pan_device *
cfg802154_device_is_child(struct wpan_dev *wpan_dev,
			  struct ieee802154_addr *target)
{
	struct ieee802154_pan_device *child;

	lockdep_assert_held(&wpan_dev->association_lock);

	list_for_each_entry(child, &wpan_dev->children, node)
		if (cfg802154_pan_device_is_matching(child, target))
			return child;

	return NULL;
}
EXPORT_SYMBOL_GPL(cfg802154_device_is_child);

__le16 cfg802154_get_free_short_addr(struct wpan_dev *wpan_dev)
{
	struct ieee802154_pan_device *child;
	__le16 addr;

	lockdep_assert_held(&wpan_dev->association_lock);

	do {
		get_random_bytes(&addr, 2);
		if (addr == cpu_to_le16(IEEE802154_ADDR_SHORT_BROADCAST) ||
		    addr == cpu_to_le16(IEEE802154_ADDR_SHORT_UNSPEC))
			continue;

		if (wpan_dev->short_addr == addr)
			continue;

		if (wpan_dev->parent && wpan_dev->parent->short_addr == addr)
			continue;

		list_for_each_entry(child, &wpan_dev->children, node)
			if (child->short_addr == addr)
				continue;

		break;
	} while (1);

	return addr;
}
EXPORT_SYMBOL_GPL(cfg802154_get_free_short_addr);

unsigned int cfg802154_set_max_associations(struct wpan_dev *wpan_dev,
					    unsigned int max)
{
	unsigned int old_max;

	lockdep_assert_held(&wpan_dev->association_lock);

	old_max = wpan_dev->max_associations;
	wpan_dev->max_associations = max;

	return old_max;
}
EXPORT_SYMBOL_GPL(cfg802154_set_max_associations);
