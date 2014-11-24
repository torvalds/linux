/*
 * Linux WiMAX
 * Mappping of generic netlink family IDs to net devices
 *
 *
 * Copyright (C) 2005-2006 Intel Corporation <linux-wimax@intel.com>
 * Inaky Perez-Gonzalez <inaky.perez-gonzalez@intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 *
 * We assign a single generic netlink family ID to each device (to
 * simplify lookup).
 *
 * We need a way to map family ID to a wimax_dev pointer.
 *
 * The idea is to use a very simple lookup. Using a netlink attribute
 * with (for example) the interface name implies a heavier search over
 * all the network devices; seemed kind of a waste given that we know
 * we are looking for a WiMAX device and that most systems will have
 * just a single WiMAX adapter.
 *
 * We put all the WiMAX devices in the system in a linked list and
 * match the generic link family ID against the list.
 *
 * By using a linked list, the case of a single adapter in the system
 * becomes (almost) no overhead, while still working for many more. If
 * it ever goes beyond two, I'll be surprised.
 */
#include <linux/device.h>
#include <net/genetlink.h>
#include <linux/netdevice.h>
#include <linux/list.h>
#include <linux/wimax.h>
#include "wimax-internal.h"


#define D_SUBMODULE id_table
#include "debug-levels.h"


static DEFINE_SPINLOCK(wimax_id_table_lock);
static struct list_head wimax_id_table = LIST_HEAD_INIT(wimax_id_table);


/*
 * wimax_id_table_add - add a gennetlink familiy ID / wimax_dev mapping
 *
 * @wimax_dev: WiMAX device descriptor to associate to the Generic
 *     Netlink family ID.
 *
 * Look for an empty spot in the ID table; if none found, double the
 * table's size and get the first spot.
 */
void wimax_id_table_add(struct wimax_dev *wimax_dev)
{
	d_fnstart(3, NULL, "(wimax_dev %p)\n", wimax_dev);
	spin_lock(&wimax_id_table_lock);
	list_add(&wimax_dev->id_table_node, &wimax_id_table);
	spin_unlock(&wimax_id_table_lock);
	d_fnend(3, NULL, "(wimax_dev %p)\n", wimax_dev);
}


/*
 * wimax_get_netdev_by_info - lookup a wimax_dev from the gennetlink info
 *
 * The generic netlink family ID has been filled out in the
 * nlmsghdr->nlmsg_type field, so we pull it from there, look it up in
 * the mapping table and reference the wimax_dev.
 *
 * When done, the reference should be dropped with
 * 'dev_put(wimax_dev->net_dev)'.
 */
struct wimax_dev *wimax_dev_get_by_genl_info(
	struct genl_info *info, int ifindex)
{
	struct wimax_dev *wimax_dev = NULL;

	d_fnstart(3, NULL, "(info %p ifindex %d)\n", info, ifindex);
	spin_lock(&wimax_id_table_lock);
	list_for_each_entry(wimax_dev, &wimax_id_table, id_table_node) {
		if (wimax_dev->net_dev->ifindex == ifindex) {
			dev_hold(wimax_dev->net_dev);
			goto found;
		}
	}
	wimax_dev = NULL;
	d_printf(1, NULL, "wimax: no devices found with ifindex %d\n",
		 ifindex);
found:
	spin_unlock(&wimax_id_table_lock);
	d_fnend(3, NULL, "(info %p ifindex %d) = %p\n",
		info, ifindex, wimax_dev);
	return wimax_dev;
}


/*
 * wimax_id_table_rm - Remove a gennetlink familiy ID / wimax_dev mapping
 *
 * @id: family ID to remove from the table
 */
void wimax_id_table_rm(struct wimax_dev *wimax_dev)
{
	spin_lock(&wimax_id_table_lock);
	list_del_init(&wimax_dev->id_table_node);
	spin_unlock(&wimax_id_table_lock);
}


/*
 * Release the gennetlink family id / mapping table
 *
 * On debug, verify that the table is empty upon removal. We want the
 * code always compiled, to ensure it doesn't bit rot. It will be
 * compiled out if CONFIG_BUG is disabled.
 */
void wimax_id_table_release(void)
{
	struct wimax_dev *wimax_dev;

#ifndef CONFIG_BUG
	return;
#endif
	spin_lock(&wimax_id_table_lock);
	list_for_each_entry(wimax_dev, &wimax_id_table, id_table_node) {
		pr_err("BUG: %s wimax_dev %p ifindex %d not cleared\n",
		       __func__, wimax_dev, wimax_dev->net_dev->ifindex);
		WARN_ON(1);
	}
	spin_unlock(&wimax_id_table_lock);
}
