/* Copyright (C) 2014 B.A.T.M.A.N. contributors:
 *
 * Linus Lüssing
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "main.h"
#include "multicast.h"
#include "originator.h"
#include "hard-interface.h"
#include "translation-table.h"

/**
 * batadv_mcast_mla_softif_get - get softif multicast listeners
 * @dev: the device to collect multicast addresses from
 * @mcast_list: a list to put found addresses into
 *
 * Collect multicast addresses of the local multicast listeners
 * on the given soft interface, dev, in the given mcast_list.
 *
 * Returns -ENOMEM on memory allocation error or the number of
 * items added to the mcast_list otherwise.
 */
static int batadv_mcast_mla_softif_get(struct net_device *dev,
				       struct hlist_head *mcast_list)
{
	struct netdev_hw_addr *mc_list_entry;
	struct batadv_hw_addr *new;
	int ret = 0;

	netif_addr_lock_bh(dev);
	netdev_for_each_mc_addr(mc_list_entry, dev) {
		new = kmalloc(sizeof(*new), GFP_ATOMIC);
		if (!new) {
			ret = -ENOMEM;
			break;
		}

		ether_addr_copy(new->addr, mc_list_entry->addr);
		hlist_add_head(&new->list, mcast_list);
		ret++;
	}
	netif_addr_unlock_bh(dev);

	return ret;
}

/**
 * batadv_mcast_mla_is_duplicate - check whether an address is in a list
 * @mcast_addr: the multicast address to check
 * @mcast_list: the list with multicast addresses to search in
 *
 * Returns true if the given address is already in the given list.
 * Otherwise returns false.
 */
static bool batadv_mcast_mla_is_duplicate(uint8_t *mcast_addr,
					  struct hlist_head *mcast_list)
{
	struct batadv_hw_addr *mcast_entry;

	hlist_for_each_entry(mcast_entry, mcast_list, list)
		if (batadv_compare_eth(mcast_entry->addr, mcast_addr))
			return true;

	return false;
}

/**
 * batadv_mcast_mla_list_free - free a list of multicast addresses
 * @mcast_list: the list to free
 *
 * Removes and frees all items in the given mcast_list.
 */
static void batadv_mcast_mla_list_free(struct hlist_head *mcast_list)
{
	struct batadv_hw_addr *mcast_entry;
	struct hlist_node *tmp;

	hlist_for_each_entry_safe(mcast_entry, tmp, mcast_list, list) {
		hlist_del(&mcast_entry->list);
		kfree(mcast_entry);
	}
}

/**
 * batadv_mcast_mla_tt_retract - clean up multicast listener announcements
 * @bat_priv: the bat priv with all the soft interface information
 * @mcast_list: a list of addresses which should _not_ be removed
 *
 * Retracts the announcement of any multicast listener from the
 * translation table except the ones listed in the given mcast_list.
 *
 * If mcast_list is NULL then all are retracted.
 */
static void batadv_mcast_mla_tt_retract(struct batadv_priv *bat_priv,
					struct hlist_head *mcast_list)
{
	struct batadv_hw_addr *mcast_entry;
	struct hlist_node *tmp;

	hlist_for_each_entry_safe(mcast_entry, tmp, &bat_priv->mcast.mla_list,
				  list) {
		if (mcast_list &&
		    batadv_mcast_mla_is_duplicate(mcast_entry->addr,
						  mcast_list))
			continue;

		batadv_tt_local_remove(bat_priv, mcast_entry->addr,
				       BATADV_NO_FLAGS,
				       "mcast TT outdated", false);

		hlist_del(&mcast_entry->list);
		kfree(mcast_entry);
	}
}

/**
 * batadv_mcast_mla_tt_add - add multicast listener announcements
 * @bat_priv: the bat priv with all the soft interface information
 * @mcast_list: a list of addresses which are going to get added
 *
 * Adds multicast listener announcements from the given mcast_list to the
 * translation table if they have not been added yet.
 */
static void batadv_mcast_mla_tt_add(struct batadv_priv *bat_priv,
				    struct hlist_head *mcast_list)
{
	struct batadv_hw_addr *mcast_entry;
	struct hlist_node *tmp;

	if (!mcast_list)
		return;

	hlist_for_each_entry_safe(mcast_entry, tmp, mcast_list, list) {
		if (batadv_mcast_mla_is_duplicate(mcast_entry->addr,
						  &bat_priv->mcast.mla_list))
			continue;

		if (!batadv_tt_local_add(bat_priv->soft_iface,
					 mcast_entry->addr, BATADV_NO_FLAGS,
					 BATADV_NULL_IFINDEX, BATADV_NO_MARK))
			continue;

		hlist_del(&mcast_entry->list);
		hlist_add_head(&mcast_entry->list, &bat_priv->mcast.mla_list);
	}
}

/**
 * batadv_mcast_has_bridge - check whether the soft-iface is bridged
 * @bat_priv: the bat priv with all the soft interface information
 *
 * Checks whether there is a bridge on top of our soft interface. Returns
 * true if so, false otherwise.
 */
static bool batadv_mcast_has_bridge(struct batadv_priv *bat_priv)
{
	struct net_device *upper = bat_priv->soft_iface;

	rcu_read_lock();
	do {
		upper = netdev_master_upper_dev_get_rcu(upper);
	} while (upper && !(upper->priv_flags & IFF_EBRIDGE));
	rcu_read_unlock();

	return upper;
}

/**
 * batadv_mcast_mla_update - update the own MLAs
 * @bat_priv: the bat priv with all the soft interface information
 *
 * Update the own multicast listener announcements in the translation
 * table.
 */
void batadv_mcast_mla_update(struct batadv_priv *bat_priv)
{
	struct net_device *soft_iface = bat_priv->soft_iface;
	struct hlist_head mcast_list = HLIST_HEAD_INIT;
	int ret;

	/* Avoid attaching MLAs, if there is a bridge on top of our soft
	 * interface, we don't support that yet (TODO)
	 */
	if (batadv_mcast_has_bridge(bat_priv))
		goto update;

	ret = batadv_mcast_mla_softif_get(soft_iface, &mcast_list);
	if (ret < 0)
		goto out;

update:
	batadv_mcast_mla_tt_retract(bat_priv, &mcast_list);
	batadv_mcast_mla_tt_add(bat_priv, &mcast_list);

out:
	batadv_mcast_mla_list_free(&mcast_list);
}

/**
 * batadv_mcast_free - free the multicast optimizations structures
 * @bat_priv: the bat priv with all the soft interface information
 */
void batadv_mcast_free(struct batadv_priv *bat_priv)
{
	batadv_mcast_mla_tt_retract(bat_priv, NULL);
}
