/* Copyright (C) 2009-2016  B.A.T.M.A.N. contributors:
 *
 * Marek Lindner, Simon Wunderlich
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

#include "originator.h"
#include "main.h"

#include <linux/atomic.h>
#include <linux/errno.h>
#include <linux/etherdevice.h>
#include <linux/fs.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/kref.h>
#include <linux/list.h>
#include <linux/lockdep.h>
#include <linux/netdevice.h>
#include <linux/rculist.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>

#include "distributed-arp-table.h"
#include "fragmentation.h"
#include "gateway_client.h"
#include "hard-interface.h"
#include "hash.h"
#include "multicast.h"
#include "network-coding.h"
#include "routing.h"
#include "translation-table.h"

/* hash class keys */
static struct lock_class_key batadv_orig_hash_lock_class_key;

static void batadv_purge_orig(struct work_struct *work);

/**
 * batadv_compare_orig - comparing function used in the originator hash table
 * @node: node in the local table
 * @data2: second object to compare the node to
 *
 * Return: true if they are the same originator
 */
bool batadv_compare_orig(const struct hlist_node *node, const void *data2)
{
	const void *data1 = container_of(node, struct batadv_orig_node,
					 hash_entry);

	return batadv_compare_eth(data1, data2);
}

/**
 * batadv_orig_node_vlan_get - get an orig_node_vlan object
 * @orig_node: the originator serving the VLAN
 * @vid: the VLAN identifier
 *
 * Return: the vlan object identified by vid and belonging to orig_node or NULL
 * if it does not exist.
 */
struct batadv_orig_node_vlan *
batadv_orig_node_vlan_get(struct batadv_orig_node *orig_node,
			  unsigned short vid)
{
	struct batadv_orig_node_vlan *vlan = NULL, *tmp;

	rcu_read_lock();
	hlist_for_each_entry_rcu(tmp, &orig_node->vlan_list, list) {
		if (tmp->vid != vid)
			continue;

		if (!kref_get_unless_zero(&tmp->refcount))
			continue;

		vlan = tmp;

		break;
	}
	rcu_read_unlock();

	return vlan;
}

/**
 * batadv_orig_node_vlan_new - search and possibly create an orig_node_vlan
 *  object
 * @orig_node: the originator serving the VLAN
 * @vid: the VLAN identifier
 *
 * Return: NULL in case of failure or the vlan object identified by vid and
 * belonging to orig_node otherwise. The object is created and added to the list
 * if it does not exist.
 *
 * The object is returned with refcounter increased by 1.
 */
struct batadv_orig_node_vlan *
batadv_orig_node_vlan_new(struct batadv_orig_node *orig_node,
			  unsigned short vid)
{
	struct batadv_orig_node_vlan *vlan;

	spin_lock_bh(&orig_node->vlan_list_lock);

	/* first look if an object for this vid already exists */
	vlan = batadv_orig_node_vlan_get(orig_node, vid);
	if (vlan)
		goto out;

	vlan = kzalloc(sizeof(*vlan), GFP_ATOMIC);
	if (!vlan)
		goto out;

	kref_init(&vlan->refcount);
	kref_get(&vlan->refcount);
	vlan->vid = vid;

	hlist_add_head_rcu(&vlan->list, &orig_node->vlan_list);

out:
	spin_unlock_bh(&orig_node->vlan_list_lock);

	return vlan;
}

/**
 * batadv_orig_node_vlan_release - release originator-vlan object from lists
 *  and queue for free after rcu grace period
 * @ref: kref pointer of the originator-vlan object
 */
static void batadv_orig_node_vlan_release(struct kref *ref)
{
	struct batadv_orig_node_vlan *orig_vlan;

	orig_vlan = container_of(ref, struct batadv_orig_node_vlan, refcount);

	kfree_rcu(orig_vlan, rcu);
}

/**
 * batadv_orig_node_vlan_put - decrement the refcounter and possibly release
 *  the originator-vlan object
 * @orig_vlan: the originator-vlan object to release
 */
void batadv_orig_node_vlan_put(struct batadv_orig_node_vlan *orig_vlan)
{
	kref_put(&orig_vlan->refcount, batadv_orig_node_vlan_release);
}

int batadv_originator_init(struct batadv_priv *bat_priv)
{
	if (bat_priv->orig_hash)
		return 0;

	bat_priv->orig_hash = batadv_hash_new(1024);

	if (!bat_priv->orig_hash)
		goto err;

	batadv_hash_set_lock_class(bat_priv->orig_hash,
				   &batadv_orig_hash_lock_class_key);

	INIT_DELAYED_WORK(&bat_priv->orig_work, batadv_purge_orig);
	queue_delayed_work(batadv_event_workqueue,
			   &bat_priv->orig_work,
			   msecs_to_jiffies(BATADV_ORIG_WORK_PERIOD));

	return 0;

err:
	return -ENOMEM;
}

/**
 * batadv_neigh_ifinfo_release - release neigh_ifinfo from lists and queue for
 *  free after rcu grace period
 * @ref: kref pointer of the neigh_ifinfo
 */
static void batadv_neigh_ifinfo_release(struct kref *ref)
{
	struct batadv_neigh_ifinfo *neigh_ifinfo;

	neigh_ifinfo = container_of(ref, struct batadv_neigh_ifinfo, refcount);

	if (neigh_ifinfo->if_outgoing != BATADV_IF_DEFAULT)
		batadv_hardif_put(neigh_ifinfo->if_outgoing);

	kfree_rcu(neigh_ifinfo, rcu);
}

/**
 * batadv_neigh_ifinfo_put - decrement the refcounter and possibly release
 *  the neigh_ifinfo
 * @neigh_ifinfo: the neigh_ifinfo object to release
 */
void batadv_neigh_ifinfo_put(struct batadv_neigh_ifinfo *neigh_ifinfo)
{
	kref_put(&neigh_ifinfo->refcount, batadv_neigh_ifinfo_release);
}

/**
 * batadv_hardif_neigh_release - release hardif neigh node from lists and
 *  queue for free after rcu grace period
 * @ref: kref pointer of the neigh_node
 */
static void batadv_hardif_neigh_release(struct kref *ref)
{
	struct batadv_hardif_neigh_node *hardif_neigh;

	hardif_neigh = container_of(ref, struct batadv_hardif_neigh_node,
				    refcount);

	spin_lock_bh(&hardif_neigh->if_incoming->neigh_list_lock);
	hlist_del_init_rcu(&hardif_neigh->list);
	spin_unlock_bh(&hardif_neigh->if_incoming->neigh_list_lock);

	batadv_hardif_put(hardif_neigh->if_incoming);
	kfree_rcu(hardif_neigh, rcu);
}

/**
 * batadv_hardif_neigh_put - decrement the hardif neighbors refcounter
 *  and possibly release it
 * @hardif_neigh: hardif neigh neighbor to free
 */
void batadv_hardif_neigh_put(struct batadv_hardif_neigh_node *hardif_neigh)
{
	kref_put(&hardif_neigh->refcount, batadv_hardif_neigh_release);
}

/**
 * batadv_neigh_node_release - release neigh_node from lists and queue for
 *  free after rcu grace period
 * @ref: kref pointer of the neigh_node
 */
static void batadv_neigh_node_release(struct kref *ref)
{
	struct hlist_node *node_tmp;
	struct batadv_neigh_node *neigh_node;
	struct batadv_neigh_ifinfo *neigh_ifinfo;
	struct batadv_algo_ops *bao;

	neigh_node = container_of(ref, struct batadv_neigh_node, refcount);
	bao = neigh_node->orig_node->bat_priv->bat_algo_ops;

	hlist_for_each_entry_safe(neigh_ifinfo, node_tmp,
				  &neigh_node->ifinfo_list, list) {
		batadv_neigh_ifinfo_put(neigh_ifinfo);
	}

	batadv_hardif_neigh_put(neigh_node->hardif_neigh);

	if (bao->bat_neigh_free)
		bao->bat_neigh_free(neigh_node);

	batadv_hardif_put(neigh_node->if_incoming);

	kfree_rcu(neigh_node, rcu);
}

/**
 * batadv_neigh_node_put - decrement the neighbors refcounter and possibly
 *  release it
 * @neigh_node: neigh neighbor to free
 */
void batadv_neigh_node_put(struct batadv_neigh_node *neigh_node)
{
	kref_put(&neigh_node->refcount, batadv_neigh_node_release);
}

/**
 * batadv_orig_router_get - router to the originator depending on iface
 * @orig_node: the orig node for the router
 * @if_outgoing: the interface where the payload packet has been received or
 *  the OGM should be sent to
 *
 * Return: the neighbor which should be router for this orig_node/iface.
 *
 * The object is returned with refcounter increased by 1.
 */
struct batadv_neigh_node *
batadv_orig_router_get(struct batadv_orig_node *orig_node,
		       const struct batadv_hard_iface *if_outgoing)
{
	struct batadv_orig_ifinfo *orig_ifinfo;
	struct batadv_neigh_node *router = NULL;

	rcu_read_lock();
	hlist_for_each_entry_rcu(orig_ifinfo, &orig_node->ifinfo_list, list) {
		if (orig_ifinfo->if_outgoing != if_outgoing)
			continue;

		router = rcu_dereference(orig_ifinfo->router);
		break;
	}

	if (router && !kref_get_unless_zero(&router->refcount))
		router = NULL;

	rcu_read_unlock();
	return router;
}

/**
 * batadv_orig_ifinfo_get - find the ifinfo from an orig_node
 * @orig_node: the orig node to be queried
 * @if_outgoing: the interface for which the ifinfo should be acquired
 *
 * Return: the requested orig_ifinfo or NULL if not found.
 *
 * The object is returned with refcounter increased by 1.
 */
struct batadv_orig_ifinfo *
batadv_orig_ifinfo_get(struct batadv_orig_node *orig_node,
		       struct batadv_hard_iface *if_outgoing)
{
	struct batadv_orig_ifinfo *tmp, *orig_ifinfo = NULL;

	rcu_read_lock();
	hlist_for_each_entry_rcu(tmp, &orig_node->ifinfo_list,
				 list) {
		if (tmp->if_outgoing != if_outgoing)
			continue;

		if (!kref_get_unless_zero(&tmp->refcount))
			continue;

		orig_ifinfo = tmp;
		break;
	}
	rcu_read_unlock();

	return orig_ifinfo;
}

/**
 * batadv_orig_ifinfo_new - search and possibly create an orig_ifinfo object
 * @orig_node: the orig node to be queried
 * @if_outgoing: the interface for which the ifinfo should be acquired
 *
 * Return: NULL in case of failure or the orig_ifinfo object for the if_outgoing
 * interface otherwise. The object is created and added to the list
 * if it does not exist.
 *
 * The object is returned with refcounter increased by 1.
 */
struct batadv_orig_ifinfo *
batadv_orig_ifinfo_new(struct batadv_orig_node *orig_node,
		       struct batadv_hard_iface *if_outgoing)
{
	struct batadv_orig_ifinfo *orig_ifinfo = NULL;
	unsigned long reset_time;

	spin_lock_bh(&orig_node->neigh_list_lock);

	orig_ifinfo = batadv_orig_ifinfo_get(orig_node, if_outgoing);
	if (orig_ifinfo)
		goto out;

	orig_ifinfo = kzalloc(sizeof(*orig_ifinfo), GFP_ATOMIC);
	if (!orig_ifinfo)
		goto out;

	if (if_outgoing != BATADV_IF_DEFAULT)
		kref_get(&if_outgoing->refcount);

	reset_time = jiffies - 1;
	reset_time -= msecs_to_jiffies(BATADV_RESET_PROTECTION_MS);
	orig_ifinfo->batman_seqno_reset = reset_time;
	orig_ifinfo->if_outgoing = if_outgoing;
	INIT_HLIST_NODE(&orig_ifinfo->list);
	kref_init(&orig_ifinfo->refcount);
	kref_get(&orig_ifinfo->refcount);
	hlist_add_head_rcu(&orig_ifinfo->list,
			   &orig_node->ifinfo_list);
out:
	spin_unlock_bh(&orig_node->neigh_list_lock);
	return orig_ifinfo;
}

/**
 * batadv_neigh_ifinfo_get - find the ifinfo from an neigh_node
 * @neigh: the neigh node to be queried
 * @if_outgoing: the interface for which the ifinfo should be acquired
 *
 * The object is returned with refcounter increased by 1.
 *
 * Return: the requested neigh_ifinfo or NULL if not found
 */
struct batadv_neigh_ifinfo *
batadv_neigh_ifinfo_get(struct batadv_neigh_node *neigh,
			struct batadv_hard_iface *if_outgoing)
{
	struct batadv_neigh_ifinfo *neigh_ifinfo = NULL,
				   *tmp_neigh_ifinfo;

	rcu_read_lock();
	hlist_for_each_entry_rcu(tmp_neigh_ifinfo, &neigh->ifinfo_list,
				 list) {
		if (tmp_neigh_ifinfo->if_outgoing != if_outgoing)
			continue;

		if (!kref_get_unless_zero(&tmp_neigh_ifinfo->refcount))
			continue;

		neigh_ifinfo = tmp_neigh_ifinfo;
		break;
	}
	rcu_read_unlock();

	return neigh_ifinfo;
}

/**
 * batadv_neigh_ifinfo_new - search and possibly create an neigh_ifinfo object
 * @neigh: the neigh node to be queried
 * @if_outgoing: the interface for which the ifinfo should be acquired
 *
 * Return: NULL in case of failure or the neigh_ifinfo object for the
 * if_outgoing interface otherwise. The object is created and added to the list
 * if it does not exist.
 *
 * The object is returned with refcounter increased by 1.
 */
struct batadv_neigh_ifinfo *
batadv_neigh_ifinfo_new(struct batadv_neigh_node *neigh,
			struct batadv_hard_iface *if_outgoing)
{
	struct batadv_neigh_ifinfo *neigh_ifinfo;

	spin_lock_bh(&neigh->ifinfo_lock);

	neigh_ifinfo = batadv_neigh_ifinfo_get(neigh, if_outgoing);
	if (neigh_ifinfo)
		goto out;

	neigh_ifinfo = kzalloc(sizeof(*neigh_ifinfo), GFP_ATOMIC);
	if (!neigh_ifinfo)
		goto out;

	if (if_outgoing)
		kref_get(&if_outgoing->refcount);

	INIT_HLIST_NODE(&neigh_ifinfo->list);
	kref_init(&neigh_ifinfo->refcount);
	kref_get(&neigh_ifinfo->refcount);
	neigh_ifinfo->if_outgoing = if_outgoing;

	hlist_add_head_rcu(&neigh_ifinfo->list, &neigh->ifinfo_list);

out:
	spin_unlock_bh(&neigh->ifinfo_lock);

	return neigh_ifinfo;
}

/**
 * batadv_neigh_node_get - retrieve a neighbour from the list
 * @orig_node: originator which the neighbour belongs to
 * @hard_iface: the interface where this neighbour is connected to
 * @addr: the address of the neighbour
 *
 * Looks for and possibly returns a neighbour belonging to this originator list
 * which is connected through the provided hard interface.
 *
 * Return: neighbor when found. Othwerwise NULL
 */
static struct batadv_neigh_node *
batadv_neigh_node_get(const struct batadv_orig_node *orig_node,
		      const struct batadv_hard_iface *hard_iface,
		      const u8 *addr)
{
	struct batadv_neigh_node *tmp_neigh_node, *res = NULL;

	rcu_read_lock();
	hlist_for_each_entry_rcu(tmp_neigh_node, &orig_node->neigh_list, list) {
		if (!batadv_compare_eth(tmp_neigh_node->addr, addr))
			continue;

		if (tmp_neigh_node->if_incoming != hard_iface)
			continue;

		if (!kref_get_unless_zero(&tmp_neigh_node->refcount))
			continue;

		res = tmp_neigh_node;
		break;
	}
	rcu_read_unlock();

	return res;
}

/**
 * batadv_hardif_neigh_create - create a hardif neighbour node
 * @hard_iface: the interface this neighbour is connected to
 * @neigh_addr: the interface address of the neighbour to retrieve
 *
 * Return: the hardif neighbour node if found or created or NULL otherwise.
 */
static struct batadv_hardif_neigh_node *
batadv_hardif_neigh_create(struct batadv_hard_iface *hard_iface,
			   const u8 *neigh_addr)
{
	struct batadv_priv *bat_priv = netdev_priv(hard_iface->soft_iface);
	struct batadv_hardif_neigh_node *hardif_neigh = NULL;

	spin_lock_bh(&hard_iface->neigh_list_lock);

	/* check if neighbor hasn't been added in the meantime */
	hardif_neigh = batadv_hardif_neigh_get(hard_iface, neigh_addr);
	if (hardif_neigh)
		goto out;

	hardif_neigh = kzalloc(sizeof(*hardif_neigh), GFP_ATOMIC);
	if (!hardif_neigh)
		goto out;

	kref_get(&hard_iface->refcount);
	INIT_HLIST_NODE(&hardif_neigh->list);
	ether_addr_copy(hardif_neigh->addr, neigh_addr);
	hardif_neigh->if_incoming = hard_iface;
	hardif_neigh->last_seen = jiffies;

	kref_init(&hardif_neigh->refcount);

	if (bat_priv->bat_algo_ops->bat_hardif_neigh_init)
		bat_priv->bat_algo_ops->bat_hardif_neigh_init(hardif_neigh);

	hlist_add_head(&hardif_neigh->list, &hard_iface->neigh_list);

out:
	spin_unlock_bh(&hard_iface->neigh_list_lock);
	return hardif_neigh;
}

/**
 * batadv_hardif_neigh_get_or_create - retrieve or create a hardif neighbour
 *  node
 * @hard_iface: the interface this neighbour is connected to
 * @neigh_addr: the interface address of the neighbour to retrieve
 *
 * Return: the hardif neighbour node if found or created or NULL otherwise.
 */
static struct batadv_hardif_neigh_node *
batadv_hardif_neigh_get_or_create(struct batadv_hard_iface *hard_iface,
				  const u8 *neigh_addr)
{
	struct batadv_hardif_neigh_node *hardif_neigh = NULL;

	/* first check without locking to avoid the overhead */
	hardif_neigh = batadv_hardif_neigh_get(hard_iface, neigh_addr);
	if (hardif_neigh)
		return hardif_neigh;

	return batadv_hardif_neigh_create(hard_iface, neigh_addr);
}

/**
 * batadv_hardif_neigh_get - retrieve a hardif neighbour from the list
 * @hard_iface: the interface where this neighbour is connected to
 * @neigh_addr: the address of the neighbour
 *
 * Looks for and possibly returns a neighbour belonging to this hard interface.
 *
 * Return: neighbor when found. Othwerwise NULL
 */
struct batadv_hardif_neigh_node *
batadv_hardif_neigh_get(const struct batadv_hard_iface *hard_iface,
			const u8 *neigh_addr)
{
	struct batadv_hardif_neigh_node *tmp_hardif_neigh, *hardif_neigh = NULL;

	rcu_read_lock();
	hlist_for_each_entry_rcu(tmp_hardif_neigh,
				 &hard_iface->neigh_list, list) {
		if (!batadv_compare_eth(tmp_hardif_neigh->addr, neigh_addr))
			continue;

		if (!kref_get_unless_zero(&tmp_hardif_neigh->refcount))
			continue;

		hardif_neigh = tmp_hardif_neigh;
		break;
	}
	rcu_read_unlock();

	return hardif_neigh;
}

/**
 * batadv_neigh_node_new - create and init a new neigh_node object
 * @orig_node: originator object representing the neighbour
 * @hard_iface: the interface where the neighbour is connected to
 * @neigh_addr: the mac address of the neighbour interface
 *
 * Allocates a new neigh_node object and initialises all the generic fields.
 *
 * Return: neighbor when found. Othwerwise NULL
 */
struct batadv_neigh_node *
batadv_neigh_node_new(struct batadv_orig_node *orig_node,
		      struct batadv_hard_iface *hard_iface,
		      const u8 *neigh_addr)
{
	struct batadv_neigh_node *neigh_node;
	struct batadv_hardif_neigh_node *hardif_neigh = NULL;

	spin_lock_bh(&orig_node->neigh_list_lock);

	neigh_node = batadv_neigh_node_get(orig_node, hard_iface, neigh_addr);
	if (neigh_node)
		goto out;

	hardif_neigh = batadv_hardif_neigh_get_or_create(hard_iface,
							 neigh_addr);
	if (!hardif_neigh)
		goto out;

	neigh_node = kzalloc(sizeof(*neigh_node), GFP_ATOMIC);
	if (!neigh_node)
		goto out;

	INIT_HLIST_NODE(&neigh_node->list);
	INIT_HLIST_HEAD(&neigh_node->ifinfo_list);
	spin_lock_init(&neigh_node->ifinfo_lock);

	kref_get(&hard_iface->refcount);
	ether_addr_copy(neigh_node->addr, neigh_addr);
	neigh_node->if_incoming = hard_iface;
	neigh_node->orig_node = orig_node;
	neigh_node->last_seen = jiffies;

	/* increment unique neighbor refcount */
	kref_get(&hardif_neigh->refcount);
	neigh_node->hardif_neigh = hardif_neigh;

	/* extra reference for return */
	kref_init(&neigh_node->refcount);
	kref_get(&neigh_node->refcount);

	hlist_add_head_rcu(&neigh_node->list, &orig_node->neigh_list);

	batadv_dbg(BATADV_DBG_BATMAN, orig_node->bat_priv,
		   "Creating new neighbor %pM for orig_node %pM on interface %s\n",
		   neigh_addr, orig_node->orig, hard_iface->net_dev->name);

out:
	spin_unlock_bh(&orig_node->neigh_list_lock);

	if (hardif_neigh)
		batadv_hardif_neigh_put(hardif_neigh);
	return neigh_node;
}

/**
 * batadv_hardif_neigh_seq_print_text - print the single hop neighbour list
 * @seq: neighbour table seq_file struct
 * @offset: not used
 *
 * Return: always 0
 */
int batadv_hardif_neigh_seq_print_text(struct seq_file *seq, void *offset)
{
	struct net_device *net_dev = (struct net_device *)seq->private;
	struct batadv_priv *bat_priv = netdev_priv(net_dev);
	struct batadv_hard_iface *primary_if;

	primary_if = batadv_seq_print_text_primary_if_get(seq);
	if (!primary_if)
		return 0;

	seq_printf(seq, "[B.A.T.M.A.N. adv %s, MainIF/MAC: %s/%pM (%s %s)]\n",
		   BATADV_SOURCE_VERSION, primary_if->net_dev->name,
		   primary_if->net_dev->dev_addr, net_dev->name,
		   bat_priv->bat_algo_ops->name);

	batadv_hardif_put(primary_if);

	if (!bat_priv->bat_algo_ops->bat_neigh_print) {
		seq_puts(seq,
			 "No printing function for this routing protocol\n");
		return 0;
	}

	bat_priv->bat_algo_ops->bat_neigh_print(bat_priv, seq);
	return 0;
}

/**
 * batadv_orig_ifinfo_release - release orig_ifinfo from lists and queue for
 *  free after rcu grace period
 * @ref: kref pointer of the orig_ifinfo
 */
static void batadv_orig_ifinfo_release(struct kref *ref)
{
	struct batadv_orig_ifinfo *orig_ifinfo;
	struct batadv_neigh_node *router;

	orig_ifinfo = container_of(ref, struct batadv_orig_ifinfo, refcount);

	if (orig_ifinfo->if_outgoing != BATADV_IF_DEFAULT)
		batadv_hardif_put(orig_ifinfo->if_outgoing);

	/* this is the last reference to this object */
	router = rcu_dereference_protected(orig_ifinfo->router, true);
	if (router)
		batadv_neigh_node_put(router);

	kfree_rcu(orig_ifinfo, rcu);
}

/**
 * batadv_orig_ifinfo_put - decrement the refcounter and possibly release
 *  the orig_ifinfo
 * @orig_ifinfo: the orig_ifinfo object to release
 */
void batadv_orig_ifinfo_put(struct batadv_orig_ifinfo *orig_ifinfo)
{
	kref_put(&orig_ifinfo->refcount, batadv_orig_ifinfo_release);
}

/**
 * batadv_orig_node_free_rcu - free the orig_node
 * @rcu: rcu pointer of the orig_node
 */
static void batadv_orig_node_free_rcu(struct rcu_head *rcu)
{
	struct batadv_orig_node *orig_node;

	orig_node = container_of(rcu, struct batadv_orig_node, rcu);

	batadv_mcast_purge_orig(orig_node);

	batadv_frag_purge_orig(orig_node, NULL);

	if (orig_node->bat_priv->bat_algo_ops->bat_orig_free)
		orig_node->bat_priv->bat_algo_ops->bat_orig_free(orig_node);

	kfree(orig_node->tt_buff);
	kfree(orig_node);
}

/**
 * batadv_orig_node_release - release orig_node from lists and queue for
 *  free after rcu grace period
 * @ref: kref pointer of the orig_node
 */
static void batadv_orig_node_release(struct kref *ref)
{
	struct hlist_node *node_tmp;
	struct batadv_neigh_node *neigh_node;
	struct batadv_orig_node *orig_node;
	struct batadv_orig_ifinfo *orig_ifinfo;

	orig_node = container_of(ref, struct batadv_orig_node, refcount);

	spin_lock_bh(&orig_node->neigh_list_lock);

	/* for all neighbors towards this originator ... */
	hlist_for_each_entry_safe(neigh_node, node_tmp,
				  &orig_node->neigh_list, list) {
		hlist_del_rcu(&neigh_node->list);
		batadv_neigh_node_put(neigh_node);
	}

	hlist_for_each_entry_safe(orig_ifinfo, node_tmp,
				  &orig_node->ifinfo_list, list) {
		hlist_del_rcu(&orig_ifinfo->list);
		batadv_orig_ifinfo_put(orig_ifinfo);
	}
	spin_unlock_bh(&orig_node->neigh_list_lock);

	/* Free nc_nodes */
	batadv_nc_purge_orig(orig_node->bat_priv, orig_node, NULL);

	call_rcu(&orig_node->rcu, batadv_orig_node_free_rcu);
}

/**
 * batadv_orig_node_put - decrement the orig node refcounter and possibly
 *  release it
 * @orig_node: the orig node to free
 */
void batadv_orig_node_put(struct batadv_orig_node *orig_node)
{
	kref_put(&orig_node->refcount, batadv_orig_node_release);
}

void batadv_originator_free(struct batadv_priv *bat_priv)
{
	struct batadv_hashtable *hash = bat_priv->orig_hash;
	struct hlist_node *node_tmp;
	struct hlist_head *head;
	spinlock_t *list_lock; /* spinlock to protect write access */
	struct batadv_orig_node *orig_node;
	u32 i;

	if (!hash)
		return;

	cancel_delayed_work_sync(&bat_priv->orig_work);

	bat_priv->orig_hash = NULL;

	for (i = 0; i < hash->size; i++) {
		head = &hash->table[i];
		list_lock = &hash->list_locks[i];

		spin_lock_bh(list_lock);
		hlist_for_each_entry_safe(orig_node, node_tmp,
					  head, hash_entry) {
			hlist_del_rcu(&orig_node->hash_entry);
			batadv_orig_node_put(orig_node);
		}
		spin_unlock_bh(list_lock);
	}

	batadv_hash_destroy(hash);
}

/**
 * batadv_orig_node_new - creates a new orig_node
 * @bat_priv: the bat priv with all the soft interface information
 * @addr: the mac address of the originator
 *
 * Creates a new originator object and initialise all the generic fields.
 * The new object is not added to the originator list.
 *
 * Return: the newly created object or NULL on failure.
 */
struct batadv_orig_node *batadv_orig_node_new(struct batadv_priv *bat_priv,
					      const u8 *addr)
{
	struct batadv_orig_node *orig_node;
	struct batadv_orig_node_vlan *vlan;
	unsigned long reset_time;
	int i;

	batadv_dbg(BATADV_DBG_BATMAN, bat_priv,
		   "Creating new originator: %pM\n", addr);

	orig_node = kzalloc(sizeof(*orig_node), GFP_ATOMIC);
	if (!orig_node)
		return NULL;

	INIT_HLIST_HEAD(&orig_node->neigh_list);
	INIT_HLIST_HEAD(&orig_node->vlan_list);
	INIT_HLIST_HEAD(&orig_node->ifinfo_list);
	spin_lock_init(&orig_node->bcast_seqno_lock);
	spin_lock_init(&orig_node->neigh_list_lock);
	spin_lock_init(&orig_node->tt_buff_lock);
	spin_lock_init(&orig_node->tt_lock);
	spin_lock_init(&orig_node->vlan_list_lock);

	batadv_nc_init_orig(orig_node);

	/* extra reference for return */
	kref_init(&orig_node->refcount);
	kref_get(&orig_node->refcount);

	orig_node->bat_priv = bat_priv;
	ether_addr_copy(orig_node->orig, addr);
	batadv_dat_init_orig_node_addr(orig_node);
	atomic_set(&orig_node->last_ttvn, 0);
	orig_node->tt_buff = NULL;
	orig_node->tt_buff_len = 0;
	orig_node->last_seen = jiffies;
	reset_time = jiffies - 1 - msecs_to_jiffies(BATADV_RESET_PROTECTION_MS);
	orig_node->bcast_seqno_reset = reset_time;

#ifdef CONFIG_BATMAN_ADV_MCAST
	orig_node->mcast_flags = BATADV_NO_FLAGS;
	INIT_HLIST_NODE(&orig_node->mcast_want_all_unsnoopables_node);
	INIT_HLIST_NODE(&orig_node->mcast_want_all_ipv4_node);
	INIT_HLIST_NODE(&orig_node->mcast_want_all_ipv6_node);
	spin_lock_init(&orig_node->mcast_handler_lock);
#endif

	/* create a vlan object for the "untagged" LAN */
	vlan = batadv_orig_node_vlan_new(orig_node, BATADV_NO_FLAGS);
	if (!vlan)
		goto free_orig_node;
	/* batadv_orig_node_vlan_new() increases the refcounter.
	 * Immediately release vlan since it is not needed anymore in this
	 * context
	 */
	batadv_orig_node_vlan_put(vlan);

	for (i = 0; i < BATADV_FRAG_BUFFER_COUNT; i++) {
		INIT_HLIST_HEAD(&orig_node->fragments[i].head);
		spin_lock_init(&orig_node->fragments[i].lock);
		orig_node->fragments[i].size = 0;
	}

	return orig_node;
free_orig_node:
	kfree(orig_node);
	return NULL;
}

/**
 * batadv_purge_neigh_ifinfo - purge obsolete ifinfo entries from neighbor
 * @bat_priv: the bat priv with all the soft interface information
 * @neigh: orig node which is to be checked
 */
static void
batadv_purge_neigh_ifinfo(struct batadv_priv *bat_priv,
			  struct batadv_neigh_node *neigh)
{
	struct batadv_neigh_ifinfo *neigh_ifinfo;
	struct batadv_hard_iface *if_outgoing;
	struct hlist_node *node_tmp;

	spin_lock_bh(&neigh->ifinfo_lock);

	/* for all ifinfo objects for this neighinator */
	hlist_for_each_entry_safe(neigh_ifinfo, node_tmp,
				  &neigh->ifinfo_list, list) {
		if_outgoing = neigh_ifinfo->if_outgoing;

		/* always keep the default interface */
		if (if_outgoing == BATADV_IF_DEFAULT)
			continue;

		/* don't purge if the interface is not (going) down */
		if ((if_outgoing->if_status != BATADV_IF_INACTIVE) &&
		    (if_outgoing->if_status != BATADV_IF_NOT_IN_USE) &&
		    (if_outgoing->if_status != BATADV_IF_TO_BE_REMOVED))
			continue;

		batadv_dbg(BATADV_DBG_BATMAN, bat_priv,
			   "neighbor/ifinfo purge: neighbor %pM, iface: %s\n",
			   neigh->addr, if_outgoing->net_dev->name);

		hlist_del_rcu(&neigh_ifinfo->list);
		batadv_neigh_ifinfo_put(neigh_ifinfo);
	}

	spin_unlock_bh(&neigh->ifinfo_lock);
}

/**
 * batadv_purge_orig_ifinfo - purge obsolete ifinfo entries from originator
 * @bat_priv: the bat priv with all the soft interface information
 * @orig_node: orig node which is to be checked
 *
 * Return: true if any ifinfo entry was purged, false otherwise.
 */
static bool
batadv_purge_orig_ifinfo(struct batadv_priv *bat_priv,
			 struct batadv_orig_node *orig_node)
{
	struct batadv_orig_ifinfo *orig_ifinfo;
	struct batadv_hard_iface *if_outgoing;
	struct hlist_node *node_tmp;
	bool ifinfo_purged = false;

	spin_lock_bh(&orig_node->neigh_list_lock);

	/* for all ifinfo objects for this originator */
	hlist_for_each_entry_safe(orig_ifinfo, node_tmp,
				  &orig_node->ifinfo_list, list) {
		if_outgoing = orig_ifinfo->if_outgoing;

		/* always keep the default interface */
		if (if_outgoing == BATADV_IF_DEFAULT)
			continue;

		/* don't purge if the interface is not (going) down */
		if ((if_outgoing->if_status != BATADV_IF_INACTIVE) &&
		    (if_outgoing->if_status != BATADV_IF_NOT_IN_USE) &&
		    (if_outgoing->if_status != BATADV_IF_TO_BE_REMOVED))
			continue;

		batadv_dbg(BATADV_DBG_BATMAN, bat_priv,
			   "router/ifinfo purge: originator %pM, iface: %s\n",
			   orig_node->orig, if_outgoing->net_dev->name);

		ifinfo_purged = true;

		hlist_del_rcu(&orig_ifinfo->list);
		batadv_orig_ifinfo_put(orig_ifinfo);
		if (orig_node->last_bonding_candidate == orig_ifinfo) {
			orig_node->last_bonding_candidate = NULL;
			batadv_orig_ifinfo_put(orig_ifinfo);
		}
	}

	spin_unlock_bh(&orig_node->neigh_list_lock);

	return ifinfo_purged;
}

/**
 * batadv_purge_orig_neighbors - purges neighbors from originator
 * @bat_priv: the bat priv with all the soft interface information
 * @orig_node: orig node which is to be checked
 *
 * Return: true if any neighbor was purged, false otherwise
 */
static bool
batadv_purge_orig_neighbors(struct batadv_priv *bat_priv,
			    struct batadv_orig_node *orig_node)
{
	struct hlist_node *node_tmp;
	struct batadv_neigh_node *neigh_node;
	bool neigh_purged = false;
	unsigned long last_seen;
	struct batadv_hard_iface *if_incoming;

	spin_lock_bh(&orig_node->neigh_list_lock);

	/* for all neighbors towards this originator ... */
	hlist_for_each_entry_safe(neigh_node, node_tmp,
				  &orig_node->neigh_list, list) {
		last_seen = neigh_node->last_seen;
		if_incoming = neigh_node->if_incoming;

		if ((batadv_has_timed_out(last_seen, BATADV_PURGE_TIMEOUT)) ||
		    (if_incoming->if_status == BATADV_IF_INACTIVE) ||
		    (if_incoming->if_status == BATADV_IF_NOT_IN_USE) ||
		    (if_incoming->if_status == BATADV_IF_TO_BE_REMOVED)) {
			if ((if_incoming->if_status == BATADV_IF_INACTIVE) ||
			    (if_incoming->if_status == BATADV_IF_NOT_IN_USE) ||
			    (if_incoming->if_status == BATADV_IF_TO_BE_REMOVED))
				batadv_dbg(BATADV_DBG_BATMAN, bat_priv,
					   "neighbor purge: originator %pM, neighbor: %pM, iface: %s\n",
					   orig_node->orig, neigh_node->addr,
					   if_incoming->net_dev->name);
			else
				batadv_dbg(BATADV_DBG_BATMAN, bat_priv,
					   "neighbor timeout: originator %pM, neighbor: %pM, last_seen: %u\n",
					   orig_node->orig, neigh_node->addr,
					   jiffies_to_msecs(last_seen));

			neigh_purged = true;

			hlist_del_rcu(&neigh_node->list);
			batadv_neigh_node_put(neigh_node);
		} else {
			/* only necessary if not the whole neighbor is to be
			 * deleted, but some interface has been removed.
			 */
			batadv_purge_neigh_ifinfo(bat_priv, neigh_node);
		}
	}

	spin_unlock_bh(&orig_node->neigh_list_lock);
	return neigh_purged;
}

/**
 * batadv_find_best_neighbor - finds the best neighbor after purging
 * @bat_priv: the bat priv with all the soft interface information
 * @orig_node: orig node which is to be checked
 * @if_outgoing: the interface for which the metric should be compared
 *
 * Return: the current best neighbor, with refcount increased.
 */
static struct batadv_neigh_node *
batadv_find_best_neighbor(struct batadv_priv *bat_priv,
			  struct batadv_orig_node *orig_node,
			  struct batadv_hard_iface *if_outgoing)
{
	struct batadv_neigh_node *best = NULL, *neigh;
	struct batadv_algo_ops *bao = bat_priv->bat_algo_ops;

	rcu_read_lock();
	hlist_for_each_entry_rcu(neigh, &orig_node->neigh_list, list) {
		if (best && (bao->bat_neigh_cmp(neigh, if_outgoing,
						best, if_outgoing) <= 0))
			continue;

		if (!kref_get_unless_zero(&neigh->refcount))
			continue;

		if (best)
			batadv_neigh_node_put(best);

		best = neigh;
	}
	rcu_read_unlock();

	return best;
}

/**
 * batadv_purge_orig_node - purges obsolete information from an orig_node
 * @bat_priv: the bat priv with all the soft interface information
 * @orig_node: orig node which is to be checked
 *
 * This function checks if the orig_node or substructures of it have become
 * obsolete, and purges this information if that's the case.
 *
 * Return: true if the orig_node is to be removed, false otherwise.
 */
static bool batadv_purge_orig_node(struct batadv_priv *bat_priv,
				   struct batadv_orig_node *orig_node)
{
	struct batadv_neigh_node *best_neigh_node;
	struct batadv_hard_iface *hard_iface;
	bool changed_ifinfo, changed_neigh;

	if (batadv_has_timed_out(orig_node->last_seen,
				 2 * BATADV_PURGE_TIMEOUT)) {
		batadv_dbg(BATADV_DBG_BATMAN, bat_priv,
			   "Originator timeout: originator %pM, last_seen %u\n",
			   orig_node->orig,
			   jiffies_to_msecs(orig_node->last_seen));
		return true;
	}
	changed_ifinfo = batadv_purge_orig_ifinfo(bat_priv, orig_node);
	changed_neigh = batadv_purge_orig_neighbors(bat_priv, orig_node);

	if (!changed_ifinfo && !changed_neigh)
		return false;

	/* first for NULL ... */
	best_neigh_node = batadv_find_best_neighbor(bat_priv, orig_node,
						    BATADV_IF_DEFAULT);
	batadv_update_route(bat_priv, orig_node, BATADV_IF_DEFAULT,
			    best_neigh_node);
	if (best_neigh_node)
		batadv_neigh_node_put(best_neigh_node);

	/* ... then for all other interfaces. */
	rcu_read_lock();
	list_for_each_entry_rcu(hard_iface, &batadv_hardif_list, list) {
		if (hard_iface->if_status != BATADV_IF_ACTIVE)
			continue;

		if (hard_iface->soft_iface != bat_priv->soft_iface)
			continue;

		if (!kref_get_unless_zero(&hard_iface->refcount))
			continue;

		best_neigh_node = batadv_find_best_neighbor(bat_priv,
							    orig_node,
							    hard_iface);
		batadv_update_route(bat_priv, orig_node, hard_iface,
				    best_neigh_node);
		if (best_neigh_node)
			batadv_neigh_node_put(best_neigh_node);

		batadv_hardif_put(hard_iface);
	}
	rcu_read_unlock();

	return false;
}

static void _batadv_purge_orig(struct batadv_priv *bat_priv)
{
	struct batadv_hashtable *hash = bat_priv->orig_hash;
	struct hlist_node *node_tmp;
	struct hlist_head *head;
	spinlock_t *list_lock; /* spinlock to protect write access */
	struct batadv_orig_node *orig_node;
	u32 i;

	if (!hash)
		return;

	/* for all origins... */
	for (i = 0; i < hash->size; i++) {
		head = &hash->table[i];
		list_lock = &hash->list_locks[i];

		spin_lock_bh(list_lock);
		hlist_for_each_entry_safe(orig_node, node_tmp,
					  head, hash_entry) {
			if (batadv_purge_orig_node(bat_priv, orig_node)) {
				batadv_gw_node_delete(bat_priv, orig_node);
				hlist_del_rcu(&orig_node->hash_entry);
				batadv_tt_global_del_orig(orig_node->bat_priv,
							  orig_node, -1,
							  "originator timed out");
				batadv_orig_node_put(orig_node);
				continue;
			}

			batadv_frag_purge_orig(orig_node,
					       batadv_frag_check_entry);
		}
		spin_unlock_bh(list_lock);
	}

	batadv_gw_election(bat_priv);
}

static void batadv_purge_orig(struct work_struct *work)
{
	struct delayed_work *delayed_work;
	struct batadv_priv *bat_priv;

	delayed_work = to_delayed_work(work);
	bat_priv = container_of(delayed_work, struct batadv_priv, orig_work);
	_batadv_purge_orig(bat_priv);
	queue_delayed_work(batadv_event_workqueue,
			   &bat_priv->orig_work,
			   msecs_to_jiffies(BATADV_ORIG_WORK_PERIOD));
}

void batadv_purge_orig_ref(struct batadv_priv *bat_priv)
{
	_batadv_purge_orig(bat_priv);
}

int batadv_orig_seq_print_text(struct seq_file *seq, void *offset)
{
	struct net_device *net_dev = (struct net_device *)seq->private;
	struct batadv_priv *bat_priv = netdev_priv(net_dev);
	struct batadv_hard_iface *primary_if;

	primary_if = batadv_seq_print_text_primary_if_get(seq);
	if (!primary_if)
		return 0;

	seq_printf(seq, "[B.A.T.M.A.N. adv %s, MainIF/MAC: %s/%pM (%s %s)]\n",
		   BATADV_SOURCE_VERSION, primary_if->net_dev->name,
		   primary_if->net_dev->dev_addr, net_dev->name,
		   bat_priv->bat_algo_ops->name);

	batadv_hardif_put(primary_if);

	if (!bat_priv->bat_algo_ops->bat_orig_print) {
		seq_puts(seq,
			 "No printing function for this routing protocol\n");
		return 0;
	}

	bat_priv->bat_algo_ops->bat_orig_print(bat_priv, seq,
					       BATADV_IF_DEFAULT);

	return 0;
}

/**
 * batadv_orig_hardif_seq_print_text - writes originator infos for a specific
 *  outgoing interface
 * @seq: debugfs table seq_file struct
 * @offset: not used
 *
 * Return: 0
 */
int batadv_orig_hardif_seq_print_text(struct seq_file *seq, void *offset)
{
	struct net_device *net_dev = (struct net_device *)seq->private;
	struct batadv_hard_iface *hard_iface;
	struct batadv_priv *bat_priv;

	hard_iface = batadv_hardif_get_by_netdev(net_dev);

	if (!hard_iface || !hard_iface->soft_iface) {
		seq_puts(seq, "Interface not known to B.A.T.M.A.N.\n");
		goto out;
	}

	bat_priv = netdev_priv(hard_iface->soft_iface);
	if (!bat_priv->bat_algo_ops->bat_orig_print) {
		seq_puts(seq,
			 "No printing function for this routing protocol\n");
		goto out;
	}

	if (hard_iface->if_status != BATADV_IF_ACTIVE) {
		seq_puts(seq, "Interface not active\n");
		goto out;
	}

	seq_printf(seq, "[B.A.T.M.A.N. adv %s, IF/MAC: %s/%pM (%s %s)]\n",
		   BATADV_SOURCE_VERSION, hard_iface->net_dev->name,
		   hard_iface->net_dev->dev_addr,
		   hard_iface->soft_iface->name, bat_priv->bat_algo_ops->name);

	bat_priv->bat_algo_ops->bat_orig_print(bat_priv, seq, hard_iface);

out:
	if (hard_iface)
		batadv_hardif_put(hard_iface);
	return 0;
}

int batadv_orig_hash_add_if(struct batadv_hard_iface *hard_iface,
			    int max_if_num)
{
	struct batadv_priv *bat_priv = netdev_priv(hard_iface->soft_iface);
	struct batadv_algo_ops *bao = bat_priv->bat_algo_ops;
	struct batadv_hashtable *hash = bat_priv->orig_hash;
	struct hlist_head *head;
	struct batadv_orig_node *orig_node;
	u32 i;
	int ret;

	/* resize all orig nodes because orig_node->bcast_own(_sum) depend on
	 * if_num
	 */
	for (i = 0; i < hash->size; i++) {
		head = &hash->table[i];

		rcu_read_lock();
		hlist_for_each_entry_rcu(orig_node, head, hash_entry) {
			ret = 0;
			if (bao->bat_orig_add_if)
				ret = bao->bat_orig_add_if(orig_node,
							   max_if_num);
			if (ret == -ENOMEM)
				goto err;
		}
		rcu_read_unlock();
	}

	return 0;

err:
	rcu_read_unlock();
	return -ENOMEM;
}

int batadv_orig_hash_del_if(struct batadv_hard_iface *hard_iface,
			    int max_if_num)
{
	struct batadv_priv *bat_priv = netdev_priv(hard_iface->soft_iface);
	struct batadv_hashtable *hash = bat_priv->orig_hash;
	struct hlist_head *head;
	struct batadv_hard_iface *hard_iface_tmp;
	struct batadv_orig_node *orig_node;
	struct batadv_algo_ops *bao = bat_priv->bat_algo_ops;
	u32 i;
	int ret;

	/* resize all orig nodes because orig_node->bcast_own(_sum) depend on
	 * if_num
	 */
	for (i = 0; i < hash->size; i++) {
		head = &hash->table[i];

		rcu_read_lock();
		hlist_for_each_entry_rcu(orig_node, head, hash_entry) {
			ret = 0;
			if (bao->bat_orig_del_if)
				ret = bao->bat_orig_del_if(orig_node,
							   max_if_num,
							   hard_iface->if_num);
			if (ret == -ENOMEM)
				goto err;
		}
		rcu_read_unlock();
	}

	/* renumber remaining batman interfaces _inside_ of orig_hash_lock */
	rcu_read_lock();
	list_for_each_entry_rcu(hard_iface_tmp, &batadv_hardif_list, list) {
		if (hard_iface_tmp->if_status == BATADV_IF_NOT_IN_USE)
			continue;

		if (hard_iface == hard_iface_tmp)
			continue;

		if (hard_iface->soft_iface != hard_iface_tmp->soft_iface)
			continue;

		if (hard_iface_tmp->if_num > hard_iface->if_num)
			hard_iface_tmp->if_num--;
	}
	rcu_read_unlock();

	hard_iface->if_num = -1;
	return 0;

err:
	rcu_read_unlock();
	return -ENOMEM;
}
