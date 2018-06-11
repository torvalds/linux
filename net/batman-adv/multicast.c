/* Copyright (C) 2014-2015 B.A.T.M.A.N. contributors:
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

#include "multicast.h"
#include "main.h"

#include <linux/atomic.h>
#include <linux/bitops.h>
#include <linux/bug.h>
#include <linux/byteorder/generic.h>
#include <linux/errno.h>
#include <linux/etherdevice.h>
#include <linux/fs.h>
#include <linux/if_ether.h>
#include <linux/in6.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/list.h>
#include <linux/lockdep.h>
#include <linux/netdevice.h>
#include <linux/rculist.h>
#include <linux/rcupdate.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/stddef.h>
#include <linux/string.h>
#include <linux/types.h>
#include <net/addrconf.h>
#include <net/ipv6.h>

#include "packet.h"
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
static bool batadv_mcast_mla_is_duplicate(u8 *mcast_addr,
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
 * @bat_priv: the bat priv with all the soft interface information
 * @mcast_list: the list to free
 *
 * Removes and frees all items in the given mcast_list.
 */
static void batadv_mcast_mla_list_free(struct batadv_priv *bat_priv,
				       struct hlist_head *mcast_list)
{
	struct batadv_hw_addr *mcast_entry;
	struct hlist_node *tmp;

	lockdep_assert_held(&bat_priv->tt.commit_lock);

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

	lockdep_assert_held(&bat_priv->tt.commit_lock);

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

	lockdep_assert_held(&bat_priv->tt.commit_lock);

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
 * batadv_mcast_mla_tvlv_update - update multicast tvlv
 * @bat_priv: the bat priv with all the soft interface information
 *
 * Updates the own multicast tvlv with our current multicast related settings,
 * capabilities and inabilities.
 *
 * Returns true if the tvlv container is registered afterwards. Otherwise
 * returns false.
 */
static bool batadv_mcast_mla_tvlv_update(struct batadv_priv *bat_priv)
{
	struct batadv_tvlv_mcast_data mcast_data;

	mcast_data.flags = BATADV_NO_FLAGS;
	memset(mcast_data.reserved, 0, sizeof(mcast_data.reserved));

	/* Avoid attaching MLAs, if there is a bridge on top of our soft
	 * interface, we don't support that yet (TODO)
	 */
	if (batadv_mcast_has_bridge(bat_priv)) {
		if (bat_priv->mcast.enabled) {
			batadv_tvlv_container_unregister(bat_priv,
							 BATADV_TVLV_MCAST, 1);
			bat_priv->mcast.enabled = false;
		}

		return false;
	}

	if (!bat_priv->mcast.enabled ||
	    mcast_data.flags != bat_priv->mcast.flags) {
		batadv_tvlv_container_register(bat_priv, BATADV_TVLV_MCAST, 1,
					       &mcast_data, sizeof(mcast_data));
		bat_priv->mcast.flags = mcast_data.flags;
		bat_priv->mcast.enabled = true;
	}

	return true;
}

/**
 * batadv_mcast_mla_update - update the own MLAs
 * @bat_priv: the bat priv with all the soft interface information
 *
 * Updates the own multicast listener announcements in the translation
 * table as well as the own, announced multicast tvlv container.
 */
void batadv_mcast_mla_update(struct batadv_priv *bat_priv)
{
	struct net_device *soft_iface = bat_priv->soft_iface;
	struct hlist_head mcast_list = HLIST_HEAD_INIT;
	int ret;

	if (!batadv_mcast_mla_tvlv_update(bat_priv))
		goto update;

	ret = batadv_mcast_mla_softif_get(soft_iface, &mcast_list);
	if (ret < 0)
		goto out;

update:
	batadv_mcast_mla_tt_retract(bat_priv, &mcast_list);
	batadv_mcast_mla_tt_add(bat_priv, &mcast_list);

out:
	batadv_mcast_mla_list_free(bat_priv, &mcast_list);
}

/**
 * batadv_mcast_forw_mode_check_ipv4 - check for optimized forwarding potential
 * @bat_priv: the bat priv with all the soft interface information
 * @skb: the IPv4 packet to check
 * @is_unsnoopable: stores whether the destination is snoopable
 *
 * Checks whether the given IPv4 packet has the potential to be forwarded with a
 * mode more optimal than classic flooding.
 *
 * If so then returns 0. Otherwise -EINVAL is returned or -ENOMEM in case of
 * memory allocation failure.
 */
static int batadv_mcast_forw_mode_check_ipv4(struct batadv_priv *bat_priv,
					     struct sk_buff *skb,
					     bool *is_unsnoopable)
{
	struct iphdr *iphdr;

	/* We might fail due to out-of-memory -> drop it */
	if (!pskb_may_pull(skb, sizeof(struct ethhdr) + sizeof(*iphdr)))
		return -ENOMEM;

	iphdr = ip_hdr(skb);

	/* TODO: Implement Multicast Router Discovery (RFC4286),
	 * then allow scope > link local, too
	 */
	if (!ipv4_is_local_multicast(iphdr->daddr))
		return -EINVAL;

	/* link-local multicast listeners behind a bridge are
	 * not snoopable (see RFC4541, section 2.1.2.2)
	 */
	*is_unsnoopable = true;

	return 0;
}

/**
 * batadv_mcast_forw_mode_check_ipv6 - check for optimized forwarding potential
 * @bat_priv: the bat priv with all the soft interface information
 * @skb: the IPv6 packet to check
 * @is_unsnoopable: stores whether the destination is snoopable
 *
 * Checks whether the given IPv6 packet has the potential to be forwarded with a
 * mode more optimal than classic flooding.
 *
 * If so then returns 0. Otherwise -EINVAL is returned or -ENOMEM if we are out
 * of memory.
 */
static int batadv_mcast_forw_mode_check_ipv6(struct batadv_priv *bat_priv,
					     struct sk_buff *skb,
					     bool *is_unsnoopable)
{
	struct ipv6hdr *ip6hdr;

	/* We might fail due to out-of-memory -> drop it */
	if (!pskb_may_pull(skb, sizeof(struct ethhdr) + sizeof(*ip6hdr)))
		return -ENOMEM;

	ip6hdr = ipv6_hdr(skb);

	/* TODO: Implement Multicast Router Discovery (RFC4286),
	 * then allow scope > link local, too
	 */
	if (IPV6_ADDR_MC_SCOPE(&ip6hdr->daddr) != IPV6_ADDR_SCOPE_LINKLOCAL)
		return -EINVAL;

	/* link-local-all-nodes multicast listeners behind a bridge are
	 * not snoopable (see RFC4541, section 3, paragraph 3)
	 */
	if (ipv6_addr_is_ll_all_nodes(&ip6hdr->daddr))
		*is_unsnoopable = true;

	return 0;
}

/**
 * batadv_mcast_forw_mode_check - check for optimized forwarding potential
 * @bat_priv: the bat priv with all the soft interface information
 * @skb: the multicast frame to check
 * @is_unsnoopable: stores whether the destination is snoopable
 *
 * Checks whether the given multicast ethernet frame has the potential to be
 * forwarded with a mode more optimal than classic flooding.
 *
 * If so then returns 0. Otherwise -EINVAL is returned or -ENOMEM if we are out
 * of memory.
 */
static int batadv_mcast_forw_mode_check(struct batadv_priv *bat_priv,
					struct sk_buff *skb,
					bool *is_unsnoopable)
{
	struct ethhdr *ethhdr = eth_hdr(skb);

	if (!atomic_read(&bat_priv->multicast_mode))
		return -EINVAL;

	if (atomic_read(&bat_priv->mcast.num_disabled))
		return -EINVAL;

	switch (ntohs(ethhdr->h_proto)) {
	case ETH_P_IP:
		return batadv_mcast_forw_mode_check_ipv4(bat_priv, skb,
							 is_unsnoopable);
	case ETH_P_IPV6:
		return batadv_mcast_forw_mode_check_ipv6(bat_priv, skb,
							 is_unsnoopable);
	default:
		return -EINVAL;
	}
}

/**
 * batadv_mcast_want_all_ip_count - count nodes with unspecific mcast interest
 * @bat_priv: the bat priv with all the soft interface information
 * @ethhdr: ethernet header of a packet
 *
 * Returns the number of nodes which want all IPv4 multicast traffic if the
 * given ethhdr is from an IPv4 packet or the number of nodes which want all
 * IPv6 traffic if it matches an IPv6 packet.
 */
static int batadv_mcast_forw_want_all_ip_count(struct batadv_priv *bat_priv,
					       struct ethhdr *ethhdr)
{
	switch (ntohs(ethhdr->h_proto)) {
	case ETH_P_IP:
		return atomic_read(&bat_priv->mcast.num_want_all_ipv4);
	case ETH_P_IPV6:
		return atomic_read(&bat_priv->mcast.num_want_all_ipv6);
	default:
		/* we shouldn't be here... */
		return 0;
	}
}

/**
 * batadv_mcast_forw_tt_node_get - get a multicast tt node
 * @bat_priv: the bat priv with all the soft interface information
 * @ethhdr: the ether header containing the multicast destination
 *
 * Returns an orig_node matching the multicast address provided by ethhdr
 * via a translation table lookup. This increases the returned nodes refcount.
 */
static struct batadv_orig_node *
batadv_mcast_forw_tt_node_get(struct batadv_priv *bat_priv,
			      struct ethhdr *ethhdr)
{
	return batadv_transtable_search(bat_priv, NULL, ethhdr->h_dest,
					BATADV_NO_FLAGS);
}

/**
 * batadv_mcast_want_forw_ipv4_node_get - get a node with an ipv4 flag
 * @bat_priv: the bat priv with all the soft interface information
 *
 * Returns an orig_node which has the BATADV_MCAST_WANT_ALL_IPV4 flag set and
 * increases its refcount.
 */
static struct batadv_orig_node *
batadv_mcast_forw_ipv4_node_get(struct batadv_priv *bat_priv)
{
	struct batadv_orig_node *tmp_orig_node, *orig_node = NULL;

	rcu_read_lock();
	hlist_for_each_entry_rcu(tmp_orig_node,
				 &bat_priv->mcast.want_all_ipv4_list,
				 mcast_want_all_ipv4_node) {
		if (!atomic_inc_not_zero(&tmp_orig_node->refcount))
			continue;

		orig_node = tmp_orig_node;
		break;
	}
	rcu_read_unlock();

	return orig_node;
}

/**
 * batadv_mcast_want_forw_ipv6_node_get - get a node with an ipv6 flag
 * @bat_priv: the bat priv with all the soft interface information
 *
 * Returns an orig_node which has the BATADV_MCAST_WANT_ALL_IPV6 flag set
 * and increases its refcount.
 */
static struct batadv_orig_node *
batadv_mcast_forw_ipv6_node_get(struct batadv_priv *bat_priv)
{
	struct batadv_orig_node *tmp_orig_node, *orig_node = NULL;

	rcu_read_lock();
	hlist_for_each_entry_rcu(tmp_orig_node,
				 &bat_priv->mcast.want_all_ipv6_list,
				 mcast_want_all_ipv6_node) {
		if (!atomic_inc_not_zero(&tmp_orig_node->refcount))
			continue;

		orig_node = tmp_orig_node;
		break;
	}
	rcu_read_unlock();

	return orig_node;
}

/**
 * batadv_mcast_want_forw_ip_node_get - get a node with an ipv4/ipv6 flag
 * @bat_priv: the bat priv with all the soft interface information
 * @ethhdr: an ethernet header to determine the protocol family from
 *
 * Returns an orig_node which has the BATADV_MCAST_WANT_ALL_IPV4 or
 * BATADV_MCAST_WANT_ALL_IPV6 flag, depending on the provided ethhdr, set and
 * increases its refcount.
 */
static struct batadv_orig_node *
batadv_mcast_forw_ip_node_get(struct batadv_priv *bat_priv,
			      struct ethhdr *ethhdr)
{
	switch (ntohs(ethhdr->h_proto)) {
	case ETH_P_IP:
		return batadv_mcast_forw_ipv4_node_get(bat_priv);
	case ETH_P_IPV6:
		return batadv_mcast_forw_ipv6_node_get(bat_priv);
	default:
		/* we shouldn't be here... */
		return NULL;
	}
}

/**
 * batadv_mcast_want_forw_unsnoop_node_get - get a node with an unsnoopable flag
 * @bat_priv: the bat priv with all the soft interface information
 *
 * Returns an orig_node which has the BATADV_MCAST_WANT_ALL_UNSNOOPABLES flag
 * set and increases its refcount.
 */
static struct batadv_orig_node *
batadv_mcast_forw_unsnoop_node_get(struct batadv_priv *bat_priv)
{
	struct batadv_orig_node *tmp_orig_node, *orig_node = NULL;

	rcu_read_lock();
	hlist_for_each_entry_rcu(tmp_orig_node,
				 &bat_priv->mcast.want_all_unsnoopables_list,
				 mcast_want_all_unsnoopables_node) {
		if (!atomic_inc_not_zero(&tmp_orig_node->refcount))
			continue;

		orig_node = tmp_orig_node;
		break;
	}
	rcu_read_unlock();

	return orig_node;
}

/**
 * batadv_mcast_forw_mode - check on how to forward a multicast packet
 * @bat_priv: the bat priv with all the soft interface information
 * @skb: The multicast packet to check
 * @orig: an originator to be set to forward the skb to
 *
 * Returns the forwarding mode as enum batadv_forw_mode and in case of
 * BATADV_FORW_SINGLE set the orig to the single originator the skb
 * should be forwarded to.
 */
enum batadv_forw_mode
batadv_mcast_forw_mode(struct batadv_priv *bat_priv, struct sk_buff *skb,
		       struct batadv_orig_node **orig)
{
	int ret, tt_count, ip_count, unsnoop_count, total_count;
	bool is_unsnoopable = false;
	struct ethhdr *ethhdr;

	ret = batadv_mcast_forw_mode_check(bat_priv, skb, &is_unsnoopable);
	if (ret == -ENOMEM)
		return BATADV_FORW_NONE;
	else if (ret < 0)
		return BATADV_FORW_ALL;

	ethhdr = eth_hdr(skb);

	tt_count = batadv_tt_global_hash_count(bat_priv, ethhdr->h_dest,
					       BATADV_NO_FLAGS);
	ip_count = batadv_mcast_forw_want_all_ip_count(bat_priv, ethhdr);
	unsnoop_count = !is_unsnoopable ? 0 :
			atomic_read(&bat_priv->mcast.num_want_all_unsnoopables);

	total_count = tt_count + ip_count + unsnoop_count;

	switch (total_count) {
	case 1:
		if (tt_count)
			*orig = batadv_mcast_forw_tt_node_get(bat_priv, ethhdr);
		else if (ip_count)
			*orig = batadv_mcast_forw_ip_node_get(bat_priv, ethhdr);
		else if (unsnoop_count)
			*orig = batadv_mcast_forw_unsnoop_node_get(bat_priv);

		if (*orig)
			return BATADV_FORW_SINGLE;

		/* fall through */
	case 0:
		return BATADV_FORW_NONE;
	default:
		return BATADV_FORW_ALL;
	}
}

/**
 * batadv_mcast_want_unsnoop_update - update unsnoop counter and list
 * @bat_priv: the bat priv with all the soft interface information
 * @orig: the orig_node which multicast state might have changed of
 * @mcast_flags: flags indicating the new multicast state
 *
 * If the BATADV_MCAST_WANT_ALL_UNSNOOPABLES flag of this originator,
 * orig, has toggled then this method updates counter and list accordingly.
 *
 * Caller needs to hold orig->mcast_handler_lock.
 */
static void batadv_mcast_want_unsnoop_update(struct batadv_priv *bat_priv,
					     struct batadv_orig_node *orig,
					     u8 mcast_flags)
{
	struct hlist_node *node = &orig->mcast_want_all_unsnoopables_node;
	struct hlist_head *head = &bat_priv->mcast.want_all_unsnoopables_list;

	lockdep_assert_held(&orig->mcast_handler_lock);

	/* switched from flag unset to set */
	if (mcast_flags & BATADV_MCAST_WANT_ALL_UNSNOOPABLES &&
	    !(orig->mcast_flags & BATADV_MCAST_WANT_ALL_UNSNOOPABLES)) {
		atomic_inc(&bat_priv->mcast.num_want_all_unsnoopables);

		spin_lock_bh(&bat_priv->mcast.want_lists_lock);
		/* flag checks above + mcast_handler_lock prevents this */
		WARN_ON(!hlist_unhashed(node));

		hlist_add_head_rcu(node, head);
		spin_unlock_bh(&bat_priv->mcast.want_lists_lock);
	/* switched from flag set to unset */
	} else if (!(mcast_flags & BATADV_MCAST_WANT_ALL_UNSNOOPABLES) &&
		   orig->mcast_flags & BATADV_MCAST_WANT_ALL_UNSNOOPABLES) {
		atomic_dec(&bat_priv->mcast.num_want_all_unsnoopables);

		spin_lock_bh(&bat_priv->mcast.want_lists_lock);
		/* flag checks above + mcast_handler_lock prevents this */
		WARN_ON(hlist_unhashed(node));

		hlist_del_init_rcu(node);
		spin_unlock_bh(&bat_priv->mcast.want_lists_lock);
	}
}

/**
 * batadv_mcast_want_ipv4_update - update want-all-ipv4 counter and list
 * @bat_priv: the bat priv with all the soft interface information
 * @orig: the orig_node which multicast state might have changed of
 * @mcast_flags: flags indicating the new multicast state
 *
 * If the BATADV_MCAST_WANT_ALL_IPV4 flag of this originator, orig, has
 * toggled then this method updates counter and list accordingly.
 *
 * Caller needs to hold orig->mcast_handler_lock.
 */
static void batadv_mcast_want_ipv4_update(struct batadv_priv *bat_priv,
					  struct batadv_orig_node *orig,
					  u8 mcast_flags)
{
	struct hlist_node *node = &orig->mcast_want_all_ipv4_node;
	struct hlist_head *head = &bat_priv->mcast.want_all_ipv4_list;

	lockdep_assert_held(&orig->mcast_handler_lock);

	/* switched from flag unset to set */
	if (mcast_flags & BATADV_MCAST_WANT_ALL_IPV4 &&
	    !(orig->mcast_flags & BATADV_MCAST_WANT_ALL_IPV4)) {
		atomic_inc(&bat_priv->mcast.num_want_all_ipv4);

		spin_lock_bh(&bat_priv->mcast.want_lists_lock);
		/* flag checks above + mcast_handler_lock prevents this */
		WARN_ON(!hlist_unhashed(node));

		hlist_add_head_rcu(node, head);
		spin_unlock_bh(&bat_priv->mcast.want_lists_lock);
	/* switched from flag set to unset */
	} else if (!(mcast_flags & BATADV_MCAST_WANT_ALL_IPV4) &&
		   orig->mcast_flags & BATADV_MCAST_WANT_ALL_IPV4) {
		atomic_dec(&bat_priv->mcast.num_want_all_ipv4);

		spin_lock_bh(&bat_priv->mcast.want_lists_lock);
		/* flag checks above + mcast_handler_lock prevents this */
		WARN_ON(hlist_unhashed(node));

		hlist_del_init_rcu(node);
		spin_unlock_bh(&bat_priv->mcast.want_lists_lock);
	}
}

/**
 * batadv_mcast_want_ipv6_update - update want-all-ipv6 counter and list
 * @bat_priv: the bat priv with all the soft interface information
 * @orig: the orig_node which multicast state might have changed of
 * @mcast_flags: flags indicating the new multicast state
 *
 * If the BATADV_MCAST_WANT_ALL_IPV6 flag of this originator, orig, has
 * toggled then this method updates counter and list accordingly.
 *
 * Caller needs to hold orig->mcast_handler_lock.
 */
static void batadv_mcast_want_ipv6_update(struct batadv_priv *bat_priv,
					  struct batadv_orig_node *orig,
					  u8 mcast_flags)
{
	struct hlist_node *node = &orig->mcast_want_all_ipv6_node;
	struct hlist_head *head = &bat_priv->mcast.want_all_ipv6_list;

	lockdep_assert_held(&orig->mcast_handler_lock);

	/* switched from flag unset to set */
	if (mcast_flags & BATADV_MCAST_WANT_ALL_IPV6 &&
	    !(orig->mcast_flags & BATADV_MCAST_WANT_ALL_IPV6)) {
		atomic_inc(&bat_priv->mcast.num_want_all_ipv6);

		spin_lock_bh(&bat_priv->mcast.want_lists_lock);
		/* flag checks above + mcast_handler_lock prevents this */
		WARN_ON(!hlist_unhashed(node));

		hlist_add_head_rcu(node, head);
		spin_unlock_bh(&bat_priv->mcast.want_lists_lock);
	/* switched from flag set to unset */
	} else if (!(mcast_flags & BATADV_MCAST_WANT_ALL_IPV6) &&
		   orig->mcast_flags & BATADV_MCAST_WANT_ALL_IPV6) {
		atomic_dec(&bat_priv->mcast.num_want_all_ipv6);

		spin_lock_bh(&bat_priv->mcast.want_lists_lock);
		/* flag checks above + mcast_handler_lock prevents this */
		WARN_ON(hlist_unhashed(node));

		hlist_del_init_rcu(node);
		spin_unlock_bh(&bat_priv->mcast.want_lists_lock);
	}
}

/**
 * batadv_mcast_tvlv_ogm_handler_v1 - process incoming multicast tvlv container
 * @bat_priv: the bat priv with all the soft interface information
 * @orig: the orig_node of the ogm
 * @flags: flags indicating the tvlv state (see batadv_tvlv_handler_flags)
 * @tvlv_value: tvlv buffer containing the multicast data
 * @tvlv_value_len: tvlv buffer length
 */
static void batadv_mcast_tvlv_ogm_handler_v1(struct batadv_priv *bat_priv,
					     struct batadv_orig_node *orig,
					     u8 flags,
					     void *tvlv_value,
					     u16 tvlv_value_len)
{
	bool orig_mcast_enabled = !(flags & BATADV_TVLV_HANDLER_OGM_CIFNOTFND);
	u8 mcast_flags = BATADV_NO_FLAGS;
	bool orig_initialized;

	if (orig_mcast_enabled && tvlv_value &&
	    (tvlv_value_len >= sizeof(mcast_flags)))
		mcast_flags = *(u8 *)tvlv_value;

	spin_lock_bh(&orig->mcast_handler_lock);
	orig_initialized = test_bit(BATADV_ORIG_CAPA_HAS_MCAST,
				    &orig->capa_initialized);

	/* If mcast support is turned on decrease the disabled mcast node
	 * counter only if we had increased it for this node before. If this
	 * is a completely new orig_node no need to decrease the counter.
	 */
	if (orig_mcast_enabled &&
	    !test_bit(BATADV_ORIG_CAPA_HAS_MCAST, &orig->capabilities)) {
		if (orig_initialized)
			atomic_dec(&bat_priv->mcast.num_disabled);
		set_bit(BATADV_ORIG_CAPA_HAS_MCAST, &orig->capabilities);
	/* If mcast support is being switched off or if this is an initial
	 * OGM without mcast support then increase the disabled mcast
	 * node counter.
	 */
	} else if (!orig_mcast_enabled &&
		   (test_bit(BATADV_ORIG_CAPA_HAS_MCAST, &orig->capabilities) ||
		    !orig_initialized)) {
		atomic_inc(&bat_priv->mcast.num_disabled);
		clear_bit(BATADV_ORIG_CAPA_HAS_MCAST, &orig->capabilities);
	}

	set_bit(BATADV_ORIG_CAPA_HAS_MCAST, &orig->capa_initialized);

	batadv_mcast_want_unsnoop_update(bat_priv, orig, mcast_flags);
	batadv_mcast_want_ipv4_update(bat_priv, orig, mcast_flags);
	batadv_mcast_want_ipv6_update(bat_priv, orig, mcast_flags);

	orig->mcast_flags = mcast_flags;
	spin_unlock_bh(&orig->mcast_handler_lock);
}

/**
 * batadv_mcast_init - initialize the multicast optimizations structures
 * @bat_priv: the bat priv with all the soft interface information
 */
void batadv_mcast_init(struct batadv_priv *bat_priv)
{
	batadv_tvlv_handler_register(bat_priv, batadv_mcast_tvlv_ogm_handler_v1,
				     NULL, BATADV_TVLV_MCAST, 1,
				     BATADV_TVLV_HANDLER_OGM_CIFNOTFND);
}

/**
 * batadv_mcast_free - free the multicast optimizations structures
 * @bat_priv: the bat priv with all the soft interface information
 */
void batadv_mcast_free(struct batadv_priv *bat_priv)
{
	batadv_tvlv_container_unregister(bat_priv, BATADV_TVLV_MCAST, 1);
	batadv_tvlv_handler_unregister(bat_priv, BATADV_TVLV_MCAST, 1);

	batadv_mcast_mla_tt_retract(bat_priv, NULL);
}

/**
 * batadv_mcast_purge_orig - reset originator global mcast state modifications
 * @orig: the originator which is going to get purged
 */
void batadv_mcast_purge_orig(struct batadv_orig_node *orig)
{
	struct batadv_priv *bat_priv = orig->bat_priv;

	spin_lock_bh(&orig->mcast_handler_lock);

	if (!test_bit(BATADV_ORIG_CAPA_HAS_MCAST, &orig->capabilities) &&
	    test_bit(BATADV_ORIG_CAPA_HAS_MCAST, &orig->capa_initialized))
		atomic_dec(&bat_priv->mcast.num_disabled);

	batadv_mcast_want_unsnoop_update(bat_priv, orig, BATADV_NO_FLAGS);
	batadv_mcast_want_ipv4_update(bat_priv, orig, BATADV_NO_FLAGS);
	batadv_mcast_want_ipv6_update(bat_priv, orig, BATADV_NO_FLAGS);

	spin_unlock_bh(&orig->mcast_handler_lock);
}
