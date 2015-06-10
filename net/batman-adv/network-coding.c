/* Copyright (C) 2012-2015 B.A.T.M.A.N. contributors:
 *
 * Martin Hundebøll, Jeppe Ledet-Pedersen
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

#include "network-coding.h"
#include "main.h"

#include <linux/atomic.h>
#include <linux/byteorder/generic.h>
#include <linux/compiler.h>
#include <linux/debugfs.h>
#include <linux/errno.h>
#include <linux/etherdevice.h>
#include <linux/fs.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <linux/init.h>
#include <linux/jhash.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/lockdep.h>
#include <linux/netdevice.h>
#include <linux/printk.h>
#include <linux/random.h>
#include <linux/rculist.h>
#include <linux/rcupdate.h>
#include <linux/seq_file.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/stat.h>
#include <linux/stddef.h>
#include <linux/string.h>
#include <linux/workqueue.h>

#include "hard-interface.h"
#include "hash.h"
#include "originator.h"
#include "packet.h"
#include "routing.h"
#include "send.h"

static struct lock_class_key batadv_nc_coding_hash_lock_class_key;
static struct lock_class_key batadv_nc_decoding_hash_lock_class_key;

static void batadv_nc_worker(struct work_struct *work);
static int batadv_nc_recv_coded_packet(struct sk_buff *skb,
				       struct batadv_hard_iface *recv_if);

/**
 * batadv_nc_init - one-time initialization for network coding
 */
int __init batadv_nc_init(void)
{
	int ret;

	/* Register our packet type */
	ret = batadv_recv_handler_register(BATADV_CODED,
					   batadv_nc_recv_coded_packet);

	return ret;
}

/**
 * batadv_nc_start_timer - initialise the nc periodic worker
 * @bat_priv: the bat priv with all the soft interface information
 */
static void batadv_nc_start_timer(struct batadv_priv *bat_priv)
{
	queue_delayed_work(batadv_event_workqueue, &bat_priv->nc.work,
			   msecs_to_jiffies(10));
}

/**
 * batadv_nc_tvlv_container_update - update the network coding tvlv container
 *  after network coding setting change
 * @bat_priv: the bat priv with all the soft interface information
 */
static void batadv_nc_tvlv_container_update(struct batadv_priv *bat_priv)
{
	char nc_mode;

	nc_mode = atomic_read(&bat_priv->network_coding);

	switch (nc_mode) {
	case 0:
		batadv_tvlv_container_unregister(bat_priv, BATADV_TVLV_NC, 1);
		break;
	case 1:
		batadv_tvlv_container_register(bat_priv, BATADV_TVLV_NC, 1,
					       NULL, 0);
		break;
	}
}

/**
 * batadv_nc_status_update - update the network coding tvlv container after
 *  network coding setting change
 * @net_dev: the soft interface net device
 */
void batadv_nc_status_update(struct net_device *net_dev)
{
	struct batadv_priv *bat_priv = netdev_priv(net_dev);

	batadv_nc_tvlv_container_update(bat_priv);
}

/**
 * batadv_nc_tvlv_ogm_handler_v1 - process incoming nc tvlv container
 * @bat_priv: the bat priv with all the soft interface information
 * @orig: the orig_node of the ogm
 * @flags: flags indicating the tvlv state (see batadv_tvlv_handler_flags)
 * @tvlv_value: tvlv buffer containing the gateway data
 * @tvlv_value_len: tvlv buffer length
 */
static void batadv_nc_tvlv_ogm_handler_v1(struct batadv_priv *bat_priv,
					  struct batadv_orig_node *orig,
					  uint8_t flags,
					  void *tvlv_value,
					  uint16_t tvlv_value_len)
{
	if (flags & BATADV_TVLV_HANDLER_OGM_CIFNOTFND)
		orig->capabilities &= ~BATADV_ORIG_CAPA_HAS_NC;
	else
		orig->capabilities |= BATADV_ORIG_CAPA_HAS_NC;
}

/**
 * batadv_nc_mesh_init - initialise coding hash table and start house keeping
 * @bat_priv: the bat priv with all the soft interface information
 */
int batadv_nc_mesh_init(struct batadv_priv *bat_priv)
{
	bat_priv->nc.timestamp_fwd_flush = jiffies;
	bat_priv->nc.timestamp_sniffed_purge = jiffies;

	if (bat_priv->nc.coding_hash || bat_priv->nc.decoding_hash)
		return 0;

	bat_priv->nc.coding_hash = batadv_hash_new(128);
	if (!bat_priv->nc.coding_hash)
		goto err;

	batadv_hash_set_lock_class(bat_priv->nc.coding_hash,
				   &batadv_nc_coding_hash_lock_class_key);

	bat_priv->nc.decoding_hash = batadv_hash_new(128);
	if (!bat_priv->nc.decoding_hash)
		goto err;

	batadv_hash_set_lock_class(bat_priv->nc.decoding_hash,
				   &batadv_nc_decoding_hash_lock_class_key);

	INIT_DELAYED_WORK(&bat_priv->nc.work, batadv_nc_worker);
	batadv_nc_start_timer(bat_priv);

	batadv_tvlv_handler_register(bat_priv, batadv_nc_tvlv_ogm_handler_v1,
				     NULL, BATADV_TVLV_NC, 1,
				     BATADV_TVLV_HANDLER_OGM_CIFNOTFND);
	batadv_nc_tvlv_container_update(bat_priv);
	return 0;

err:
	return -ENOMEM;
}

/**
 * batadv_nc_init_bat_priv - initialise the nc specific bat_priv variables
 * @bat_priv: the bat priv with all the soft interface information
 */
void batadv_nc_init_bat_priv(struct batadv_priv *bat_priv)
{
	atomic_set(&bat_priv->network_coding, 0);
	bat_priv->nc.min_tq = 200;
	bat_priv->nc.max_fwd_delay = 10;
	bat_priv->nc.max_buffer_time = 200;
}

/**
 * batadv_nc_init_orig - initialise the nc fields of an orig_node
 * @orig_node: the orig_node which is going to be initialised
 */
void batadv_nc_init_orig(struct batadv_orig_node *orig_node)
{
	INIT_LIST_HEAD(&orig_node->in_coding_list);
	INIT_LIST_HEAD(&orig_node->out_coding_list);
	spin_lock_init(&orig_node->in_coding_list_lock);
	spin_lock_init(&orig_node->out_coding_list_lock);
}

/**
 * batadv_nc_node_free_rcu - rcu callback to free an nc node and remove
 *  its refcount on the orig_node
 * @rcu: rcu pointer of the nc node
 */
static void batadv_nc_node_free_rcu(struct rcu_head *rcu)
{
	struct batadv_nc_node *nc_node;

	nc_node = container_of(rcu, struct batadv_nc_node, rcu);
	batadv_orig_node_free_ref(nc_node->orig_node);
	kfree(nc_node);
}

/**
 * batadv_nc_node_free_ref - decrements the nc node refcounter and possibly
 * frees it
 * @nc_node: the nc node to free
 */
static void batadv_nc_node_free_ref(struct batadv_nc_node *nc_node)
{
	if (atomic_dec_and_test(&nc_node->refcount))
		call_rcu(&nc_node->rcu, batadv_nc_node_free_rcu);
}

/**
 * batadv_nc_path_free_ref - decrements the nc path refcounter and possibly
 * frees it
 * @nc_path: the nc node to free
 */
static void batadv_nc_path_free_ref(struct batadv_nc_path *nc_path)
{
	if (atomic_dec_and_test(&nc_path->refcount))
		kfree_rcu(nc_path, rcu);
}

/**
 * batadv_nc_packet_free - frees nc packet
 * @nc_packet: the nc packet to free
 */
static void batadv_nc_packet_free(struct batadv_nc_packet *nc_packet)
{
	if (nc_packet->skb)
		kfree_skb(nc_packet->skb);

	batadv_nc_path_free_ref(nc_packet->nc_path);
	kfree(nc_packet);
}

/**
 * batadv_nc_to_purge_nc_node - checks whether an nc node has to be purged
 * @bat_priv: the bat priv with all the soft interface information
 * @nc_node: the nc node to check
 *
 * Returns true if the entry has to be purged now, false otherwise
 */
static bool batadv_nc_to_purge_nc_node(struct batadv_priv *bat_priv,
				       struct batadv_nc_node *nc_node)
{
	if (atomic_read(&bat_priv->mesh_state) != BATADV_MESH_ACTIVE)
		return true;

	return batadv_has_timed_out(nc_node->last_seen, BATADV_NC_NODE_TIMEOUT);
}

/**
 * batadv_nc_to_purge_nc_path_coding - checks whether an nc path has timed out
 * @bat_priv: the bat priv with all the soft interface information
 * @nc_path: the nc path to check
 *
 * Returns true if the entry has to be purged now, false otherwise
 */
static bool batadv_nc_to_purge_nc_path_coding(struct batadv_priv *bat_priv,
					      struct batadv_nc_path *nc_path)
{
	if (atomic_read(&bat_priv->mesh_state) != BATADV_MESH_ACTIVE)
		return true;

	/* purge the path when no packets has been added for 10 times the
	 * max_fwd_delay time
	 */
	return batadv_has_timed_out(nc_path->last_valid,
				    bat_priv->nc.max_fwd_delay * 10);
}

/**
 * batadv_nc_to_purge_nc_path_decoding - checks whether an nc path has timed out
 * @bat_priv: the bat priv with all the soft interface information
 * @nc_path: the nc path to check
 *
 * Returns true if the entry has to be purged now, false otherwise
 */
static bool batadv_nc_to_purge_nc_path_decoding(struct batadv_priv *bat_priv,
						struct batadv_nc_path *nc_path)
{
	if (atomic_read(&bat_priv->mesh_state) != BATADV_MESH_ACTIVE)
		return true;

	/* purge the path when no packets has been added for 10 times the
	 * max_buffer time
	 */
	return batadv_has_timed_out(nc_path->last_valid,
				    bat_priv->nc.max_buffer_time * 10);
}

/**
 * batadv_nc_purge_orig_nc_nodes - go through list of nc nodes and purge stale
 *  entries
 * @bat_priv: the bat priv with all the soft interface information
 * @list: list of nc nodes
 * @lock: nc node list lock
 * @to_purge: function in charge to decide whether an entry has to be purged or
 *	      not. This function takes the nc node as argument and has to return
 *	      a boolean value: true if the entry has to be deleted, false
 *	      otherwise
 */
static void
batadv_nc_purge_orig_nc_nodes(struct batadv_priv *bat_priv,
			      struct list_head *list,
			      spinlock_t *lock,
			      bool (*to_purge)(struct batadv_priv *,
					       struct batadv_nc_node *))
{
	struct batadv_nc_node *nc_node, *nc_node_tmp;

	/* For each nc_node in list */
	spin_lock_bh(lock);
	list_for_each_entry_safe(nc_node, nc_node_tmp, list, list) {
		/* if an helper function has been passed as parameter,
		 * ask it if the entry has to be purged or not
		 */
		if (to_purge && !to_purge(bat_priv, nc_node))
			continue;

		batadv_dbg(BATADV_DBG_NC, bat_priv,
			   "Removing nc_node %pM -> %pM\n",
			   nc_node->addr, nc_node->orig_node->orig);
		list_del_rcu(&nc_node->list);
		batadv_nc_node_free_ref(nc_node);
	}
	spin_unlock_bh(lock);
}

/**
 * batadv_nc_purge_orig - purges all nc node data attached of the given
 *  originator
 * @bat_priv: the bat priv with all the soft interface information
 * @orig_node: orig_node with the nc node entries to be purged
 * @to_purge: function in charge to decide whether an entry has to be purged or
 *	      not. This function takes the nc node as argument and has to return
 *	      a boolean value: true is the entry has to be deleted, false
 *	      otherwise
 */
void batadv_nc_purge_orig(struct batadv_priv *bat_priv,
			  struct batadv_orig_node *orig_node,
			  bool (*to_purge)(struct batadv_priv *,
					   struct batadv_nc_node *))
{
	/* Check ingoing nc_node's of this orig_node */
	batadv_nc_purge_orig_nc_nodes(bat_priv, &orig_node->in_coding_list,
				      &orig_node->in_coding_list_lock,
				      to_purge);

	/* Check outgoing nc_node's of this orig_node */
	batadv_nc_purge_orig_nc_nodes(bat_priv, &orig_node->out_coding_list,
				      &orig_node->out_coding_list_lock,
				      to_purge);
}

/**
 * batadv_nc_purge_orig_hash - traverse entire originator hash to check if they
 *  have timed out nc nodes
 * @bat_priv: the bat priv with all the soft interface information
 */
static void batadv_nc_purge_orig_hash(struct batadv_priv *bat_priv)
{
	struct batadv_hashtable *hash = bat_priv->orig_hash;
	struct hlist_head *head;
	struct batadv_orig_node *orig_node;
	uint32_t i;

	if (!hash)
		return;

	/* For each orig_node */
	for (i = 0; i < hash->size; i++) {
		head = &hash->table[i];

		rcu_read_lock();
		hlist_for_each_entry_rcu(orig_node, head, hash_entry)
			batadv_nc_purge_orig(bat_priv, orig_node,
					     batadv_nc_to_purge_nc_node);
		rcu_read_unlock();
	}
}

/**
 * batadv_nc_purge_paths - traverse all nc paths part of the hash and remove
 *  unused ones
 * @bat_priv: the bat priv with all the soft interface information
 * @hash: hash table containing the nc paths to check
 * @to_purge: function in charge to decide whether an entry has to be purged or
 *	      not. This function takes the nc node as argument and has to return
 *	      a boolean value: true is the entry has to be deleted, false
 *	      otherwise
 */
static void batadv_nc_purge_paths(struct batadv_priv *bat_priv,
				  struct batadv_hashtable *hash,
				  bool (*to_purge)(struct batadv_priv *,
						   struct batadv_nc_path *))
{
	struct hlist_head *head;
	struct hlist_node *node_tmp;
	struct batadv_nc_path *nc_path;
	spinlock_t *lock; /* Protects lists in hash */
	uint32_t i;

	for (i = 0; i < hash->size; i++) {
		head = &hash->table[i];
		lock = &hash->list_locks[i];

		/* For each nc_path in this bin */
		spin_lock_bh(lock);
		hlist_for_each_entry_safe(nc_path, node_tmp, head, hash_entry) {
			/* if an helper function has been passed as parameter,
			 * ask it if the entry has to be purged or not
			 */
			if (to_purge && !to_purge(bat_priv, nc_path))
				continue;

			/* purging an non-empty nc_path should never happen, but
			 * is observed under high CPU load. Delay the purging
			 * until next iteration to allow the packet_list to be
			 * emptied first.
			 */
			if (!unlikely(list_empty(&nc_path->packet_list))) {
				net_ratelimited_function(printk,
							 KERN_WARNING
							 "Skipping free of non-empty nc_path (%pM -> %pM)!\n",
							 nc_path->prev_hop,
							 nc_path->next_hop);
				continue;
			}

			/* nc_path is unused, so remove it */
			batadv_dbg(BATADV_DBG_NC, bat_priv,
				   "Remove nc_path %pM -> %pM\n",
				   nc_path->prev_hop, nc_path->next_hop);
			hlist_del_rcu(&nc_path->hash_entry);
			batadv_nc_path_free_ref(nc_path);
		}
		spin_unlock_bh(lock);
	}
}

/**
 * batadv_nc_hash_key_gen - computes the nc_path hash key
 * @key: buffer to hold the final hash key
 * @src: source ethernet mac address going into the hash key
 * @dst: destination ethernet mac address going into the hash key
 */
static void batadv_nc_hash_key_gen(struct batadv_nc_path *key, const char *src,
				   const char *dst)
{
	memcpy(key->prev_hop, src, sizeof(key->prev_hop));
	memcpy(key->next_hop, dst, sizeof(key->next_hop));
}

/**
 * batadv_nc_hash_choose - compute the hash value for an nc path
 * @data: data to hash
 * @size: size of the hash table
 *
 * Returns the selected index in the hash table for the given data.
 */
static uint32_t batadv_nc_hash_choose(const void *data, uint32_t size)
{
	const struct batadv_nc_path *nc_path = data;
	uint32_t hash = 0;

	hash = jhash(&nc_path->prev_hop, sizeof(nc_path->prev_hop), hash);
	hash = jhash(&nc_path->next_hop, sizeof(nc_path->next_hop), hash);

	return hash % size;
}

/**
 * batadv_nc_hash_compare - comparing function used in the network coding hash
 *  tables
 * @node: node in the local table
 * @data2: second object to compare the node to
 *
 * Returns 1 if the two entry are the same, 0 otherwise
 */
static int batadv_nc_hash_compare(const struct hlist_node *node,
				  const void *data2)
{
	const struct batadv_nc_path *nc_path1, *nc_path2;

	nc_path1 = container_of(node, struct batadv_nc_path, hash_entry);
	nc_path2 = data2;

	/* Return 1 if the two keys are identical */
	if (memcmp(nc_path1->prev_hop, nc_path2->prev_hop,
		   sizeof(nc_path1->prev_hop)) != 0)
		return 0;

	if (memcmp(nc_path1->next_hop, nc_path2->next_hop,
		   sizeof(nc_path1->next_hop)) != 0)
		return 0;

	return 1;
}

/**
 * batadv_nc_hash_find - search for an existing nc path and return it
 * @hash: hash table containing the nc path
 * @data: search key
 *
 * Returns the nc_path if found, NULL otherwise.
 */
static struct batadv_nc_path *
batadv_nc_hash_find(struct batadv_hashtable *hash,
		    void *data)
{
	struct hlist_head *head;
	struct batadv_nc_path *nc_path, *nc_path_tmp = NULL;
	int index;

	if (!hash)
		return NULL;

	index = batadv_nc_hash_choose(data, hash->size);
	head = &hash->table[index];

	rcu_read_lock();
	hlist_for_each_entry_rcu(nc_path, head, hash_entry) {
		if (!batadv_nc_hash_compare(&nc_path->hash_entry, data))
			continue;

		if (!atomic_inc_not_zero(&nc_path->refcount))
			continue;

		nc_path_tmp = nc_path;
		break;
	}
	rcu_read_unlock();

	return nc_path_tmp;
}

/**
 * batadv_nc_send_packet - send non-coded packet and free nc_packet struct
 * @nc_packet: the nc packet to send
 */
static void batadv_nc_send_packet(struct batadv_nc_packet *nc_packet)
{
	batadv_send_skb_packet(nc_packet->skb,
			       nc_packet->neigh_node->if_incoming,
			       nc_packet->nc_path->next_hop);
	nc_packet->skb = NULL;
	batadv_nc_packet_free(nc_packet);
}

/**
 * batadv_nc_sniffed_purge - Checks timestamp of given sniffed nc_packet.
 * @bat_priv: the bat priv with all the soft interface information
 * @nc_path: the nc path the packet belongs to
 * @nc_packet: the nc packet to be checked
 *
 * Checks whether the given sniffed (overheard) nc_packet has hit its buffering
 * timeout. If so, the packet is no longer kept and the entry deleted from the
 * queue. Has to be called with the appropriate locks.
 *
 * Returns false as soon as the entry in the fifo queue has not been timed out
 * yet and true otherwise.
 */
static bool batadv_nc_sniffed_purge(struct batadv_priv *bat_priv,
				    struct batadv_nc_path *nc_path,
				    struct batadv_nc_packet *nc_packet)
{
	unsigned long timeout = bat_priv->nc.max_buffer_time;
	bool res = false;

	/* Packets are added to tail, so the remaining packets did not time
	 * out and we can stop processing the current queue
	 */
	if (atomic_read(&bat_priv->mesh_state) == BATADV_MESH_ACTIVE &&
	    !batadv_has_timed_out(nc_packet->timestamp, timeout))
		goto out;

	/* purge nc packet */
	list_del(&nc_packet->list);
	batadv_nc_packet_free(nc_packet);

	res = true;

out:
	return res;
}

/**
 * batadv_nc_fwd_flush - Checks the timestamp of the given nc packet.
 * @bat_priv: the bat priv with all the soft interface information
 * @nc_path: the nc path the packet belongs to
 * @nc_packet: the nc packet to be checked
 *
 * Checks whether the given nc packet has hit its forward timeout. If so, the
 * packet is no longer delayed, immediately sent and the entry deleted from the
 * queue. Has to be called with the appropriate locks.
 *
 * Returns false as soon as the entry in the fifo queue has not been timed out
 * yet and true otherwise.
 */
static bool batadv_nc_fwd_flush(struct batadv_priv *bat_priv,
				struct batadv_nc_path *nc_path,
				struct batadv_nc_packet *nc_packet)
{
	unsigned long timeout = bat_priv->nc.max_fwd_delay;

	/* Packets are added to tail, so the remaining packets did not time
	 * out and we can stop processing the current queue
	 */
	if (atomic_read(&bat_priv->mesh_state) == BATADV_MESH_ACTIVE &&
	    !batadv_has_timed_out(nc_packet->timestamp, timeout))
		return false;

	/* Send packet */
	batadv_inc_counter(bat_priv, BATADV_CNT_FORWARD);
	batadv_add_counter(bat_priv, BATADV_CNT_FORWARD_BYTES,
			   nc_packet->skb->len + ETH_HLEN);
	list_del(&nc_packet->list);
	batadv_nc_send_packet(nc_packet);

	return true;
}

/**
 * batadv_nc_process_nc_paths - traverse given nc packet pool and free timed out
 *  nc packets
 * @bat_priv: the bat priv with all the soft interface information
 * @hash: to be processed hash table
 * @process_fn: Function called to process given nc packet. Should return true
 *	        to encourage this function to proceed with the next packet.
 *	        Otherwise the rest of the current queue is skipped.
 */
static void
batadv_nc_process_nc_paths(struct batadv_priv *bat_priv,
			   struct batadv_hashtable *hash,
			   bool (*process_fn)(struct batadv_priv *,
					      struct batadv_nc_path *,
					      struct batadv_nc_packet *))
{
	struct hlist_head *head;
	struct batadv_nc_packet *nc_packet, *nc_packet_tmp;
	struct batadv_nc_path *nc_path;
	bool ret;
	int i;

	if (!hash)
		return;

	/* Loop hash table bins */
	for (i = 0; i < hash->size; i++) {
		head = &hash->table[i];

		/* Loop coding paths */
		rcu_read_lock();
		hlist_for_each_entry_rcu(nc_path, head, hash_entry) {
			/* Loop packets */
			spin_lock_bh(&nc_path->packet_list_lock);
			list_for_each_entry_safe(nc_packet, nc_packet_tmp,
						 &nc_path->packet_list, list) {
				ret = process_fn(bat_priv, nc_path, nc_packet);
				if (!ret)
					break;
			}
			spin_unlock_bh(&nc_path->packet_list_lock);
		}
		rcu_read_unlock();
	}
}

/**
 * batadv_nc_worker - periodic task for house keeping related to network coding
 * @work: kernel work struct
 */
static void batadv_nc_worker(struct work_struct *work)
{
	struct delayed_work *delayed_work;
	struct batadv_priv_nc *priv_nc;
	struct batadv_priv *bat_priv;
	unsigned long timeout;

	delayed_work = container_of(work, struct delayed_work, work);
	priv_nc = container_of(delayed_work, struct batadv_priv_nc, work);
	bat_priv = container_of(priv_nc, struct batadv_priv, nc);

	batadv_nc_purge_orig_hash(bat_priv);
	batadv_nc_purge_paths(bat_priv, bat_priv->nc.coding_hash,
			      batadv_nc_to_purge_nc_path_coding);
	batadv_nc_purge_paths(bat_priv, bat_priv->nc.decoding_hash,
			      batadv_nc_to_purge_nc_path_decoding);

	timeout = bat_priv->nc.max_fwd_delay;

	if (batadv_has_timed_out(bat_priv->nc.timestamp_fwd_flush, timeout)) {
		batadv_nc_process_nc_paths(bat_priv, bat_priv->nc.coding_hash,
					   batadv_nc_fwd_flush);
		bat_priv->nc.timestamp_fwd_flush = jiffies;
	}

	if (batadv_has_timed_out(bat_priv->nc.timestamp_sniffed_purge,
				 bat_priv->nc.max_buffer_time)) {
		batadv_nc_process_nc_paths(bat_priv, bat_priv->nc.decoding_hash,
					   batadv_nc_sniffed_purge);
		bat_priv->nc.timestamp_sniffed_purge = jiffies;
	}

	/* Schedule a new check */
	batadv_nc_start_timer(bat_priv);
}

/**
 * batadv_can_nc_with_orig - checks whether the given orig node is suitable for
 *  coding or not
 * @bat_priv: the bat priv with all the soft interface information
 * @orig_node: neighboring orig node which may be used as nc candidate
 * @ogm_packet: incoming ogm packet also used for the checks
 *
 * Returns true if:
 *  1) The OGM must have the most recent sequence number.
 *  2) The TTL must be decremented by one and only one.
 *  3) The OGM must be received from the first hop from orig_node.
 *  4) The TQ value of the OGM must be above bat_priv->nc.min_tq.
 */
static bool batadv_can_nc_with_orig(struct batadv_priv *bat_priv,
				    struct batadv_orig_node *orig_node,
				    struct batadv_ogm_packet *ogm_packet)
{
	struct batadv_orig_ifinfo *orig_ifinfo;
	uint32_t last_real_seqno;
	uint8_t last_ttl;

	orig_ifinfo = batadv_orig_ifinfo_get(orig_node, BATADV_IF_DEFAULT);
	if (!orig_ifinfo)
		return false;

	last_ttl = orig_ifinfo->last_ttl;
	last_real_seqno = orig_ifinfo->last_real_seqno;
	batadv_orig_ifinfo_free_ref(orig_ifinfo);

	if (last_real_seqno != ntohl(ogm_packet->seqno))
		return false;
	if (last_ttl != ogm_packet->ttl + 1)
		return false;
	if (!batadv_compare_eth(ogm_packet->orig, ogm_packet->prev_sender))
		return false;
	if (ogm_packet->tq < bat_priv->nc.min_tq)
		return false;

	return true;
}

/**
 * batadv_nc_find_nc_node - search for an existing nc node and return it
 * @orig_node: orig node originating the ogm packet
 * @orig_neigh_node: neighboring orig node from which we received the ogm packet
 *  (can be equal to orig_node)
 * @in_coding: traverse incoming or outgoing network coding list
 *
 * Returns the nc_node if found, NULL otherwise.
 */
static struct batadv_nc_node
*batadv_nc_find_nc_node(struct batadv_orig_node *orig_node,
			struct batadv_orig_node *orig_neigh_node,
			bool in_coding)
{
	struct batadv_nc_node *nc_node, *nc_node_out = NULL;
	struct list_head *list;

	if (in_coding)
		list = &orig_neigh_node->in_coding_list;
	else
		list = &orig_neigh_node->out_coding_list;

	/* Traverse list of nc_nodes to orig_node */
	rcu_read_lock();
	list_for_each_entry_rcu(nc_node, list, list) {
		if (!batadv_compare_eth(nc_node->addr, orig_node->orig))
			continue;

		if (!atomic_inc_not_zero(&nc_node->refcount))
			continue;

		/* Found a match */
		nc_node_out = nc_node;
		break;
	}
	rcu_read_unlock();

	return nc_node_out;
}

/**
 * batadv_nc_get_nc_node - retrieves an nc node or creates the entry if it was
 *  not found
 * @bat_priv: the bat priv with all the soft interface information
 * @orig_node: orig node originating the ogm packet
 * @orig_neigh_node: neighboring orig node from which we received the ogm packet
 *  (can be equal to orig_node)
 * @in_coding: traverse incoming or outgoing network coding list
 *
 * Returns the nc_node if found or created, NULL in case of an error.
 */
static struct batadv_nc_node
*batadv_nc_get_nc_node(struct batadv_priv *bat_priv,
		       struct batadv_orig_node *orig_node,
		       struct batadv_orig_node *orig_neigh_node,
		       bool in_coding)
{
	struct batadv_nc_node *nc_node;
	spinlock_t *lock; /* Used to lock list selected by "int in_coding" */
	struct list_head *list;

	/* Check if nc_node is already added */
	nc_node = batadv_nc_find_nc_node(orig_node, orig_neigh_node, in_coding);

	/* Node found */
	if (nc_node)
		return nc_node;

	nc_node = kzalloc(sizeof(*nc_node), GFP_ATOMIC);
	if (!nc_node)
		return NULL;

	if (!atomic_inc_not_zero(&orig_neigh_node->refcount))
		goto free;

	/* Initialize nc_node */
	INIT_LIST_HEAD(&nc_node->list);
	ether_addr_copy(nc_node->addr, orig_node->orig);
	nc_node->orig_node = orig_neigh_node;
	atomic_set(&nc_node->refcount, 2);

	/* Select ingoing or outgoing coding node */
	if (in_coding) {
		lock = &orig_neigh_node->in_coding_list_lock;
		list = &orig_neigh_node->in_coding_list;
	} else {
		lock = &orig_neigh_node->out_coding_list_lock;
		list = &orig_neigh_node->out_coding_list;
	}

	batadv_dbg(BATADV_DBG_NC, bat_priv, "Adding nc_node %pM -> %pM\n",
		   nc_node->addr, nc_node->orig_node->orig);

	/* Add nc_node to orig_node */
	spin_lock_bh(lock);
	list_add_tail_rcu(&nc_node->list, list);
	spin_unlock_bh(lock);

	return nc_node;

free:
	kfree(nc_node);
	return NULL;
}

/**
 * batadv_nc_update_nc_node - updates stored incoming and outgoing nc node structs
 *  (best called on incoming OGMs)
 * @bat_priv: the bat priv with all the soft interface information
 * @orig_node: orig node originating the ogm packet
 * @orig_neigh_node: neighboring orig node from which we received the ogm packet
 *  (can be equal to orig_node)
 * @ogm_packet: incoming ogm packet
 * @is_single_hop_neigh: orig_node is a single hop neighbor
 */
void batadv_nc_update_nc_node(struct batadv_priv *bat_priv,
			      struct batadv_orig_node *orig_node,
			      struct batadv_orig_node *orig_neigh_node,
			      struct batadv_ogm_packet *ogm_packet,
			      int is_single_hop_neigh)
{
	struct batadv_nc_node *in_nc_node = NULL, *out_nc_node = NULL;

	/* Check if network coding is enabled */
	if (!atomic_read(&bat_priv->network_coding))
		goto out;

	/* check if orig node is network coding enabled */
	if (!(orig_node->capabilities & BATADV_ORIG_CAPA_HAS_NC))
		goto out;

	/* accept ogms from 'good' neighbors and single hop neighbors */
	if (!batadv_can_nc_with_orig(bat_priv, orig_node, ogm_packet) &&
	    !is_single_hop_neigh)
		goto out;

	/* Add orig_node as in_nc_node on hop */
	in_nc_node = batadv_nc_get_nc_node(bat_priv, orig_node,
					   orig_neigh_node, true);
	if (!in_nc_node)
		goto out;

	in_nc_node->last_seen = jiffies;

	/* Add hop as out_nc_node on orig_node */
	out_nc_node = batadv_nc_get_nc_node(bat_priv, orig_neigh_node,
					    orig_node, false);
	if (!out_nc_node)
		goto out;

	out_nc_node->last_seen = jiffies;

out:
	if (in_nc_node)
		batadv_nc_node_free_ref(in_nc_node);
	if (out_nc_node)
		batadv_nc_node_free_ref(out_nc_node);
}

/**
 * batadv_nc_get_path - get existing nc_path or allocate a new one
 * @bat_priv: the bat priv with all the soft interface information
 * @hash: hash table containing the nc path
 * @src: ethernet source address - first half of the nc path search key
 * @dst: ethernet destination address - second half of the nc path search key
 *
 * Returns pointer to nc_path if the path was found or created, returns NULL
 * on error.
 */
static struct batadv_nc_path *batadv_nc_get_path(struct batadv_priv *bat_priv,
						 struct batadv_hashtable *hash,
						 uint8_t *src,
						 uint8_t *dst)
{
	int hash_added;
	struct batadv_nc_path *nc_path, nc_path_key;

	batadv_nc_hash_key_gen(&nc_path_key, src, dst);

	/* Search for existing nc_path */
	nc_path = batadv_nc_hash_find(hash, (void *)&nc_path_key);

	if (nc_path) {
		/* Set timestamp to delay removal of nc_path */
		nc_path->last_valid = jiffies;
		return nc_path;
	}

	/* No existing nc_path was found; create a new */
	nc_path = kzalloc(sizeof(*nc_path), GFP_ATOMIC);

	if (!nc_path)
		return NULL;

	/* Initialize nc_path */
	INIT_LIST_HEAD(&nc_path->packet_list);
	spin_lock_init(&nc_path->packet_list_lock);
	atomic_set(&nc_path->refcount, 2);
	nc_path->last_valid = jiffies;
	ether_addr_copy(nc_path->next_hop, dst);
	ether_addr_copy(nc_path->prev_hop, src);

	batadv_dbg(BATADV_DBG_NC, bat_priv, "Adding nc_path %pM -> %pM\n",
		   nc_path->prev_hop,
		   nc_path->next_hop);

	/* Add nc_path to hash table */
	hash_added = batadv_hash_add(hash, batadv_nc_hash_compare,
				     batadv_nc_hash_choose, &nc_path_key,
				     &nc_path->hash_entry);

	if (hash_added < 0) {
		kfree(nc_path);
		return NULL;
	}

	return nc_path;
}

/**
 * batadv_nc_random_weight_tq - scale the receivers TQ-value to avoid unfair
 *  selection of a receiver with slightly lower TQ than the other
 * @tq: to be weighted tq value
 */
static uint8_t batadv_nc_random_weight_tq(uint8_t tq)
{
	uint8_t rand_val, rand_tq;

	get_random_bytes(&rand_val, sizeof(rand_val));

	/* randomize the estimated packet loss (max TQ - estimated TQ) */
	rand_tq = rand_val * (BATADV_TQ_MAX_VALUE - tq);

	/* normalize the randomized packet loss */
	rand_tq /= BATADV_TQ_MAX_VALUE;

	/* convert to (randomized) estimated tq again */
	return BATADV_TQ_MAX_VALUE - rand_tq;
}

/**
 * batadv_nc_memxor - XOR destination with source
 * @dst: byte array to XOR into
 * @src: byte array to XOR from
 * @len: length of destination array
 */
static void batadv_nc_memxor(char *dst, const char *src, unsigned int len)
{
	unsigned int i;

	for (i = 0; i < len; ++i)
		dst[i] ^= src[i];
}

/**
 * batadv_nc_code_packets - code a received unicast_packet with an nc packet
 *  into a coded_packet and send it
 * @bat_priv: the bat priv with all the soft interface information
 * @skb: data skb to forward
 * @ethhdr: pointer to the ethernet header inside the skb
 * @nc_packet: structure containing the packet to the skb can be coded with
 * @neigh_node: next hop to forward packet to
 *
 * Returns true if both packets are consumed, false otherwise.
 */
static bool batadv_nc_code_packets(struct batadv_priv *bat_priv,
				   struct sk_buff *skb,
				   struct ethhdr *ethhdr,
				   struct batadv_nc_packet *nc_packet,
				   struct batadv_neigh_node *neigh_node)
{
	uint8_t tq_weighted_neigh, tq_weighted_coding, tq_tmp;
	struct sk_buff *skb_dest, *skb_src;
	struct batadv_unicast_packet *packet1;
	struct batadv_unicast_packet *packet2;
	struct batadv_coded_packet *coded_packet;
	struct batadv_neigh_node *neigh_tmp, *router_neigh;
	struct batadv_neigh_node *router_coding = NULL;
	struct batadv_neigh_ifinfo *router_neigh_ifinfo = NULL;
	struct batadv_neigh_ifinfo *router_coding_ifinfo = NULL;
	uint8_t *first_source, *first_dest, *second_source, *second_dest;
	__be32 packet_id1, packet_id2;
	size_t count;
	bool res = false;
	int coding_len;
	int unicast_size = sizeof(*packet1);
	int coded_size = sizeof(*coded_packet);
	int header_add = coded_size - unicast_size;

	/* TODO: do we need to consider the outgoing interface for
	 * coded packets?
	 */
	router_neigh = batadv_orig_router_get(neigh_node->orig_node,
					      BATADV_IF_DEFAULT);
	if (!router_neigh)
		goto out;

	router_neigh_ifinfo = batadv_neigh_ifinfo_get(router_neigh,
						      BATADV_IF_DEFAULT);
	if (!router_neigh_ifinfo)
		goto out;

	neigh_tmp = nc_packet->neigh_node;
	router_coding = batadv_orig_router_get(neigh_tmp->orig_node,
					       BATADV_IF_DEFAULT);
	if (!router_coding)
		goto out;

	router_coding_ifinfo = batadv_neigh_ifinfo_get(router_coding,
						       BATADV_IF_DEFAULT);
	if (!router_coding_ifinfo)
		goto out;

	tq_tmp = router_neigh_ifinfo->bat_iv.tq_avg;
	tq_weighted_neigh = batadv_nc_random_weight_tq(tq_tmp);
	tq_tmp = router_coding_ifinfo->bat_iv.tq_avg;
	tq_weighted_coding = batadv_nc_random_weight_tq(tq_tmp);

	/* Select one destination for the MAC-header dst-field based on
	 * weighted TQ-values.
	 */
	if (tq_weighted_neigh >= tq_weighted_coding) {
		/* Destination from nc_packet is selected for MAC-header */
		first_dest = nc_packet->nc_path->next_hop;
		first_source = nc_packet->nc_path->prev_hop;
		second_dest = neigh_node->addr;
		second_source = ethhdr->h_source;
		packet1 = (struct batadv_unicast_packet *)nc_packet->skb->data;
		packet2 = (struct batadv_unicast_packet *)skb->data;
		packet_id1 = nc_packet->packet_id;
		packet_id2 = batadv_skb_crc32(skb,
					      skb->data + sizeof(*packet2));
	} else {
		/* Destination for skb is selected for MAC-header */
		first_dest = neigh_node->addr;
		first_source = ethhdr->h_source;
		second_dest = nc_packet->nc_path->next_hop;
		second_source = nc_packet->nc_path->prev_hop;
		packet1 = (struct batadv_unicast_packet *)skb->data;
		packet2 = (struct batadv_unicast_packet *)nc_packet->skb->data;
		packet_id1 = batadv_skb_crc32(skb,
					      skb->data + sizeof(*packet1));
		packet_id2 = nc_packet->packet_id;
	}

	/* Instead of zero padding the smallest data buffer, we
	 * code into the largest.
	 */
	if (skb->len <= nc_packet->skb->len) {
		skb_dest = nc_packet->skb;
		skb_src = skb;
	} else {
		skb_dest = skb;
		skb_src = nc_packet->skb;
	}

	/* coding_len is used when decoding the packet shorter packet */
	coding_len = skb_src->len - unicast_size;

	if (skb_linearize(skb_dest) < 0 || skb_linearize(skb_src) < 0)
		goto out;

	skb_push(skb_dest, header_add);

	coded_packet = (struct batadv_coded_packet *)skb_dest->data;
	skb_reset_mac_header(skb_dest);

	coded_packet->packet_type = BATADV_CODED;
	coded_packet->version = BATADV_COMPAT_VERSION;
	coded_packet->ttl = packet1->ttl;

	/* Info about first unicast packet */
	ether_addr_copy(coded_packet->first_source, first_source);
	ether_addr_copy(coded_packet->first_orig_dest, packet1->dest);
	coded_packet->first_crc = packet_id1;
	coded_packet->first_ttvn = packet1->ttvn;

	/* Info about second unicast packet */
	ether_addr_copy(coded_packet->second_dest, second_dest);
	ether_addr_copy(coded_packet->second_source, second_source);
	ether_addr_copy(coded_packet->second_orig_dest, packet2->dest);
	coded_packet->second_crc = packet_id2;
	coded_packet->second_ttl = packet2->ttl;
	coded_packet->second_ttvn = packet2->ttvn;
	coded_packet->coded_len = htons(coding_len);

	/* This is where the magic happens: Code skb_src into skb_dest */
	batadv_nc_memxor(skb_dest->data + coded_size,
			 skb_src->data + unicast_size, coding_len);

	/* Update counters accordingly */
	if (BATADV_SKB_CB(skb_src)->decoded &&
	    BATADV_SKB_CB(skb_dest)->decoded) {
		/* Both packets are recoded */
		count = skb_src->len + ETH_HLEN;
		count += skb_dest->len + ETH_HLEN;
		batadv_add_counter(bat_priv, BATADV_CNT_NC_RECODE, 2);
		batadv_add_counter(bat_priv, BATADV_CNT_NC_RECODE_BYTES, count);
	} else if (!BATADV_SKB_CB(skb_src)->decoded &&
		   !BATADV_SKB_CB(skb_dest)->decoded) {
		/* Both packets are newly coded */
		count = skb_src->len + ETH_HLEN;
		count += skb_dest->len + ETH_HLEN;
		batadv_add_counter(bat_priv, BATADV_CNT_NC_CODE, 2);
		batadv_add_counter(bat_priv, BATADV_CNT_NC_CODE_BYTES, count);
	} else if (BATADV_SKB_CB(skb_src)->decoded &&
		   !BATADV_SKB_CB(skb_dest)->decoded) {
		/* skb_src recoded and skb_dest is newly coded */
		batadv_inc_counter(bat_priv, BATADV_CNT_NC_RECODE);
		batadv_add_counter(bat_priv, BATADV_CNT_NC_RECODE_BYTES,
				   skb_src->len + ETH_HLEN);
		batadv_inc_counter(bat_priv, BATADV_CNT_NC_CODE);
		batadv_add_counter(bat_priv, BATADV_CNT_NC_CODE_BYTES,
				   skb_dest->len + ETH_HLEN);
	} else if (!BATADV_SKB_CB(skb_src)->decoded &&
		   BATADV_SKB_CB(skb_dest)->decoded) {
		/* skb_src is newly coded and skb_dest is recoded */
		batadv_inc_counter(bat_priv, BATADV_CNT_NC_CODE);
		batadv_add_counter(bat_priv, BATADV_CNT_NC_CODE_BYTES,
				   skb_src->len + ETH_HLEN);
		batadv_inc_counter(bat_priv, BATADV_CNT_NC_RECODE);
		batadv_add_counter(bat_priv, BATADV_CNT_NC_RECODE_BYTES,
				   skb_dest->len + ETH_HLEN);
	}

	/* skb_src is now coded into skb_dest, so free it */
	kfree_skb(skb_src);

	/* avoid duplicate free of skb from nc_packet */
	nc_packet->skb = NULL;
	batadv_nc_packet_free(nc_packet);

	/* Send the coded packet and return true */
	batadv_send_skb_packet(skb_dest, neigh_node->if_incoming, first_dest);
	res = true;
out:
	if (router_neigh)
		batadv_neigh_node_free_ref(router_neigh);
	if (router_coding)
		batadv_neigh_node_free_ref(router_coding);
	if (router_neigh_ifinfo)
		batadv_neigh_ifinfo_free_ref(router_neigh_ifinfo);
	if (router_coding_ifinfo)
		batadv_neigh_ifinfo_free_ref(router_coding_ifinfo);
	return res;
}

/**
 * batadv_nc_skb_coding_possible - true if a decoded skb is available at dst.
 * @skb: data skb to forward
 * @dst: destination mac address of the other skb to code with
 * @src: source mac address of skb
 *
 * Whenever we network code a packet we have to check whether we received it in
 * a network coded form. If so, we may not be able to use it for coding because
 * some neighbors may also have received (overheard) the packet in the network
 * coded form without being able to decode it. It is hard to know which of the
 * neighboring nodes was able to decode the packet, therefore we can only
 * re-code the packet if the source of the previous encoded packet is involved.
 * Since the source encoded the packet we can be certain it has all necessary
 * decode information.
 *
 * Returns true if coding of a decoded packet is allowed.
 */
static bool batadv_nc_skb_coding_possible(struct sk_buff *skb,
					  uint8_t *dst, uint8_t *src)
{
	if (BATADV_SKB_CB(skb)->decoded && !batadv_compare_eth(dst, src))
		return false;
	return true;
}

/**
 * batadv_nc_path_search - Find the coding path matching in_nc_node and
 *  out_nc_node to retrieve a buffered packet that can be used for coding.
 * @bat_priv: the bat priv with all the soft interface information
 * @in_nc_node: pointer to skb next hop's neighbor nc node
 * @out_nc_node: pointer to skb source's neighbor nc node
 * @skb: data skb to forward
 * @eth_dst: next hop mac address of skb
 *
 * Returns true if coding of a decoded skb is allowed.
 */
static struct batadv_nc_packet *
batadv_nc_path_search(struct batadv_priv *bat_priv,
		      struct batadv_nc_node *in_nc_node,
		      struct batadv_nc_node *out_nc_node,
		      struct sk_buff *skb,
		      uint8_t *eth_dst)
{
	struct batadv_nc_path *nc_path, nc_path_key;
	struct batadv_nc_packet *nc_packet_out = NULL;
	struct batadv_nc_packet *nc_packet, *nc_packet_tmp;
	struct batadv_hashtable *hash = bat_priv->nc.coding_hash;
	int idx;

	if (!hash)
		return NULL;

	/* Create almost path key */
	batadv_nc_hash_key_gen(&nc_path_key, in_nc_node->addr,
			       out_nc_node->addr);
	idx = batadv_nc_hash_choose(&nc_path_key, hash->size);

	/* Check for coding opportunities in this nc_path */
	rcu_read_lock();
	hlist_for_each_entry_rcu(nc_path, &hash->table[idx], hash_entry) {
		if (!batadv_compare_eth(nc_path->prev_hop, in_nc_node->addr))
			continue;

		if (!batadv_compare_eth(nc_path->next_hop, out_nc_node->addr))
			continue;

		spin_lock_bh(&nc_path->packet_list_lock);
		if (list_empty(&nc_path->packet_list)) {
			spin_unlock_bh(&nc_path->packet_list_lock);
			continue;
		}

		list_for_each_entry_safe(nc_packet, nc_packet_tmp,
					 &nc_path->packet_list, list) {
			if (!batadv_nc_skb_coding_possible(nc_packet->skb,
							   eth_dst,
							   in_nc_node->addr))
				continue;

			/* Coding opportunity is found! */
			list_del(&nc_packet->list);
			nc_packet_out = nc_packet;
			break;
		}

		spin_unlock_bh(&nc_path->packet_list_lock);
		break;
	}
	rcu_read_unlock();

	return nc_packet_out;
}

/**
 * batadv_nc_skb_src_search - Loops through the list of neighoring nodes of the
 *  skb's sender (may be equal to the originator).
 * @bat_priv: the bat priv with all the soft interface information
 * @skb: data skb to forward
 * @eth_dst: next hop mac address of skb
 * @eth_src: source mac address of skb
 * @in_nc_node: pointer to skb next hop's neighbor nc node
 *
 * Returns an nc packet if a suitable coding packet was found, NULL otherwise.
 */
static struct batadv_nc_packet *
batadv_nc_skb_src_search(struct batadv_priv *bat_priv,
			 struct sk_buff *skb,
			 uint8_t *eth_dst,
			 uint8_t *eth_src,
			 struct batadv_nc_node *in_nc_node)
{
	struct batadv_orig_node *orig_node;
	struct batadv_nc_node *out_nc_node;
	struct batadv_nc_packet *nc_packet = NULL;

	orig_node = batadv_orig_hash_find(bat_priv, eth_src);
	if (!orig_node)
		return NULL;

	rcu_read_lock();
	list_for_each_entry_rcu(out_nc_node,
				&orig_node->out_coding_list, list) {
		/* Check if the skb is decoded and if recoding is possible */
		if (!batadv_nc_skb_coding_possible(skb,
						   out_nc_node->addr, eth_src))
			continue;

		/* Search for an opportunity in this nc_path */
		nc_packet = batadv_nc_path_search(bat_priv, in_nc_node,
						  out_nc_node, skb, eth_dst);
		if (nc_packet)
			break;
	}
	rcu_read_unlock();

	batadv_orig_node_free_ref(orig_node);
	return nc_packet;
}

/**
 * batadv_nc_skb_store_before_coding - set the ethernet src and dst of the
 *  unicast skb before it is stored for use in later decoding
 * @bat_priv: the bat priv with all the soft interface information
 * @skb: data skb to store
 * @eth_dst_new: new destination mac address of skb
 */
static void batadv_nc_skb_store_before_coding(struct batadv_priv *bat_priv,
					      struct sk_buff *skb,
					      uint8_t *eth_dst_new)
{
	struct ethhdr *ethhdr;

	/* Copy skb header to change the mac header */
	skb = pskb_copy_for_clone(skb, GFP_ATOMIC);
	if (!skb)
		return;

	/* Set the mac header as if we actually sent the packet uncoded */
	ethhdr = eth_hdr(skb);
	ether_addr_copy(ethhdr->h_source, ethhdr->h_dest);
	ether_addr_copy(ethhdr->h_dest, eth_dst_new);

	/* Set data pointer to MAC header to mimic packets from our tx path */
	skb_push(skb, ETH_HLEN);

	/* Add the packet to the decoding packet pool */
	batadv_nc_skb_store_for_decoding(bat_priv, skb);

	/* batadv_nc_skb_store_for_decoding() clones the skb, so we must free
	 * our ref
	 */
	kfree_skb(skb);
}

/**
 * batadv_nc_skb_dst_search - Loops through list of neighboring nodes to dst.
 * @skb: data skb to forward
 * @neigh_node: next hop to forward packet to
 * @ethhdr: pointer to the ethernet header inside the skb
 *
 * Loops through list of neighboring nodes the next hop has a good connection to
 * (receives OGMs with a sufficient quality). We need to find a neighbor of our
 * next hop that potentially sent a packet which our next hop also received
 * (overheard) and has stored for later decoding.
 *
 * Returns true if the skb was consumed (encoded packet sent) or false otherwise
 */
static bool batadv_nc_skb_dst_search(struct sk_buff *skb,
				     struct batadv_neigh_node *neigh_node,
				     struct ethhdr *ethhdr)
{
	struct net_device *netdev = neigh_node->if_incoming->soft_iface;
	struct batadv_priv *bat_priv = netdev_priv(netdev);
	struct batadv_orig_node *orig_node = neigh_node->orig_node;
	struct batadv_nc_node *nc_node;
	struct batadv_nc_packet *nc_packet = NULL;

	rcu_read_lock();
	list_for_each_entry_rcu(nc_node, &orig_node->in_coding_list, list) {
		/* Search for coding opportunity with this in_nc_node */
		nc_packet = batadv_nc_skb_src_search(bat_priv, skb,
						     neigh_node->addr,
						     ethhdr->h_source, nc_node);

		/* Opportunity was found, so stop searching */
		if (nc_packet)
			break;
	}
	rcu_read_unlock();

	if (!nc_packet)
		return false;

	/* Save packets for later decoding */
	batadv_nc_skb_store_before_coding(bat_priv, skb,
					  neigh_node->addr);
	batadv_nc_skb_store_before_coding(bat_priv, nc_packet->skb,
					  nc_packet->neigh_node->addr);

	/* Code and send packets */
	if (batadv_nc_code_packets(bat_priv, skb, ethhdr, nc_packet,
				   neigh_node))
		return true;

	/* out of mem ? Coding failed - we have to free the buffered packet
	 * to avoid memleaks. The skb passed as argument will be dealt with
	 * by the calling function.
	 */
	batadv_nc_send_packet(nc_packet);
	return false;
}

/**
 * batadv_nc_skb_add_to_path - buffer skb for later encoding / decoding
 * @skb: skb to add to path
 * @nc_path: path to add skb to
 * @neigh_node: next hop to forward packet to
 * @packet_id: checksum to identify packet
 *
 * Returns true if the packet was buffered or false in case of an error.
 */
static bool batadv_nc_skb_add_to_path(struct sk_buff *skb,
				      struct batadv_nc_path *nc_path,
				      struct batadv_neigh_node *neigh_node,
				      __be32 packet_id)
{
	struct batadv_nc_packet *nc_packet;

	nc_packet = kzalloc(sizeof(*nc_packet), GFP_ATOMIC);
	if (!nc_packet)
		return false;

	/* Initialize nc_packet */
	nc_packet->timestamp = jiffies;
	nc_packet->packet_id = packet_id;
	nc_packet->skb = skb;
	nc_packet->neigh_node = neigh_node;
	nc_packet->nc_path = nc_path;

	/* Add coding packet to list */
	spin_lock_bh(&nc_path->packet_list_lock);
	list_add_tail(&nc_packet->list, &nc_path->packet_list);
	spin_unlock_bh(&nc_path->packet_list_lock);

	return true;
}

/**
 * batadv_nc_skb_forward - try to code a packet or add it to the coding packet
 *  buffer
 * @skb: data skb to forward
 * @neigh_node: next hop to forward packet to
 *
 * Returns true if the skb was consumed (encoded packet sent) or false otherwise
 */
bool batadv_nc_skb_forward(struct sk_buff *skb,
			   struct batadv_neigh_node *neigh_node)
{
	const struct net_device *netdev = neigh_node->if_incoming->soft_iface;
	struct batadv_priv *bat_priv = netdev_priv(netdev);
	struct batadv_unicast_packet *packet;
	struct batadv_nc_path *nc_path;
	struct ethhdr *ethhdr = eth_hdr(skb);
	__be32 packet_id;
	u8 *payload;

	/* Check if network coding is enabled */
	if (!atomic_read(&bat_priv->network_coding))
		goto out;

	/* We only handle unicast packets */
	payload = skb_network_header(skb);
	packet = (struct batadv_unicast_packet *)payload;
	if (packet->packet_type != BATADV_UNICAST)
		goto out;

	/* Try to find a coding opportunity and send the skb if one is found */
	if (batadv_nc_skb_dst_search(skb, neigh_node, ethhdr))
		return true;

	/* Find or create a nc_path for this src-dst pair */
	nc_path = batadv_nc_get_path(bat_priv,
				     bat_priv->nc.coding_hash,
				     ethhdr->h_source,
				     neigh_node->addr);

	if (!nc_path)
		goto out;

	/* Add skb to nc_path */
	packet_id = batadv_skb_crc32(skb, payload + sizeof(*packet));
	if (!batadv_nc_skb_add_to_path(skb, nc_path, neigh_node, packet_id))
		goto free_nc_path;

	/* Packet is consumed */
	return true;

free_nc_path:
	batadv_nc_path_free_ref(nc_path);
out:
	/* Packet is not consumed */
	return false;
}

/**
 * batadv_nc_skb_store_for_decoding - save a clone of the skb which can be used
 *  when decoding coded packets
 * @bat_priv: the bat priv with all the soft interface information
 * @skb: data skb to store
 */
void batadv_nc_skb_store_for_decoding(struct batadv_priv *bat_priv,
				      struct sk_buff *skb)
{
	struct batadv_unicast_packet *packet;
	struct batadv_nc_path *nc_path;
	struct ethhdr *ethhdr = eth_hdr(skb);
	__be32 packet_id;
	u8 *payload;

	/* Check if network coding is enabled */
	if (!atomic_read(&bat_priv->network_coding))
		goto out;

	/* Check for supported packet type */
	payload = skb_network_header(skb);
	packet = (struct batadv_unicast_packet *)payload;
	if (packet->packet_type != BATADV_UNICAST)
		goto out;

	/* Find existing nc_path or create a new */
	nc_path = batadv_nc_get_path(bat_priv,
				     bat_priv->nc.decoding_hash,
				     ethhdr->h_source,
				     ethhdr->h_dest);

	if (!nc_path)
		goto out;

	/* Clone skb and adjust skb->data to point at batman header */
	skb = skb_clone(skb, GFP_ATOMIC);
	if (unlikely(!skb))
		goto free_nc_path;

	if (unlikely(!pskb_may_pull(skb, ETH_HLEN)))
		goto free_skb;

	if (unlikely(!skb_pull_rcsum(skb, ETH_HLEN)))
		goto free_skb;

	/* Add skb to nc_path */
	packet_id = batadv_skb_crc32(skb, payload + sizeof(*packet));
	if (!batadv_nc_skb_add_to_path(skb, nc_path, NULL, packet_id))
		goto free_skb;

	batadv_inc_counter(bat_priv, BATADV_CNT_NC_BUFFER);
	return;

free_skb:
	kfree_skb(skb);
free_nc_path:
	batadv_nc_path_free_ref(nc_path);
out:
	return;
}

/**
 * batadv_nc_skb_store_sniffed_unicast - check if a received unicast packet
 *  should be saved in the decoding buffer and, if so, store it there
 * @bat_priv: the bat priv with all the soft interface information
 * @skb: unicast skb to store
 */
void batadv_nc_skb_store_sniffed_unicast(struct batadv_priv *bat_priv,
					 struct sk_buff *skb)
{
	struct ethhdr *ethhdr = eth_hdr(skb);

	if (batadv_is_my_mac(bat_priv, ethhdr->h_dest))
		return;

	/* Set data pointer to MAC header to mimic packets from our tx path */
	skb_push(skb, ETH_HLEN);

	batadv_nc_skb_store_for_decoding(bat_priv, skb);
}

/**
 * batadv_nc_skb_decode_packet - decode given skb using the decode data stored
 *  in nc_packet
 * @bat_priv: the bat priv with all the soft interface information
 * @skb: unicast skb to decode
 * @nc_packet: decode data needed to decode the skb
 *
 * Returns pointer to decoded unicast packet if the packet was decoded or NULL
 * in case of an error.
 */
static struct batadv_unicast_packet *
batadv_nc_skb_decode_packet(struct batadv_priv *bat_priv, struct sk_buff *skb,
			    struct batadv_nc_packet *nc_packet)
{
	const int h_size = sizeof(struct batadv_unicast_packet);
	const int h_diff = sizeof(struct batadv_coded_packet) - h_size;
	struct batadv_unicast_packet *unicast_packet;
	struct batadv_coded_packet coded_packet_tmp;
	struct ethhdr *ethhdr, ethhdr_tmp;
	uint8_t *orig_dest, ttl, ttvn;
	unsigned int coding_len;
	int err;

	/* Save headers temporarily */
	memcpy(&coded_packet_tmp, skb->data, sizeof(coded_packet_tmp));
	memcpy(&ethhdr_tmp, skb_mac_header(skb), sizeof(ethhdr_tmp));

	if (skb_cow(skb, 0) < 0)
		return NULL;

	if (unlikely(!skb_pull_rcsum(skb, h_diff)))
		return NULL;

	/* Data points to batman header, so set mac header 14 bytes before
	 * and network to data
	 */
	skb_set_mac_header(skb, -ETH_HLEN);
	skb_reset_network_header(skb);

	/* Reconstruct original mac header */
	ethhdr = eth_hdr(skb);
	*ethhdr = ethhdr_tmp;

	/* Select the correct unicast header information based on the location
	 * of our mac address in the coded_packet header
	 */
	if (batadv_is_my_mac(bat_priv, coded_packet_tmp.second_dest)) {
		/* If we are the second destination the packet was overheard,
		 * so the Ethernet address must be copied to h_dest and
		 * pkt_type changed from PACKET_OTHERHOST to PACKET_HOST
		 */
		ether_addr_copy(ethhdr->h_dest, coded_packet_tmp.second_dest);
		skb->pkt_type = PACKET_HOST;

		orig_dest = coded_packet_tmp.second_orig_dest;
		ttl = coded_packet_tmp.second_ttl;
		ttvn = coded_packet_tmp.second_ttvn;
	} else {
		orig_dest = coded_packet_tmp.first_orig_dest;
		ttl = coded_packet_tmp.ttl;
		ttvn = coded_packet_tmp.first_ttvn;
	}

	coding_len = ntohs(coded_packet_tmp.coded_len);

	if (coding_len > skb->len)
		return NULL;

	/* Here the magic is reversed:
	 *   extract the missing packet from the received coded packet
	 */
	batadv_nc_memxor(skb->data + h_size,
			 nc_packet->skb->data + h_size,
			 coding_len);

	/* Resize decoded skb if decoded with larger packet */
	if (nc_packet->skb->len > coding_len + h_size) {
		err = pskb_trim_rcsum(skb, coding_len + h_size);
		if (err)
			return NULL;
	}

	/* Create decoded unicast packet */
	unicast_packet = (struct batadv_unicast_packet *)skb->data;
	unicast_packet->packet_type = BATADV_UNICAST;
	unicast_packet->version = BATADV_COMPAT_VERSION;
	unicast_packet->ttl = ttl;
	ether_addr_copy(unicast_packet->dest, orig_dest);
	unicast_packet->ttvn = ttvn;

	batadv_nc_packet_free(nc_packet);
	return unicast_packet;
}

/**
 * batadv_nc_find_decoding_packet - search through buffered decoding data to
 *  find the data needed to decode the coded packet
 * @bat_priv: the bat priv with all the soft interface information
 * @ethhdr: pointer to the ethernet header inside the coded packet
 * @coded: coded packet we try to find decode data for
 *
 * Returns pointer to nc packet if the needed data was found or NULL otherwise.
 */
static struct batadv_nc_packet *
batadv_nc_find_decoding_packet(struct batadv_priv *bat_priv,
			       struct ethhdr *ethhdr,
			       struct batadv_coded_packet *coded)
{
	struct batadv_hashtable *hash = bat_priv->nc.decoding_hash;
	struct batadv_nc_packet *tmp_nc_packet, *nc_packet = NULL;
	struct batadv_nc_path *nc_path, nc_path_key;
	uint8_t *dest, *source;
	__be32 packet_id;
	int index;

	if (!hash)
		return NULL;

	/* Select the correct packet id based on the location of our mac-addr */
	dest = ethhdr->h_source;
	if (!batadv_is_my_mac(bat_priv, coded->second_dest)) {
		source = coded->second_source;
		packet_id = coded->second_crc;
	} else {
		source = coded->first_source;
		packet_id = coded->first_crc;
	}

	batadv_nc_hash_key_gen(&nc_path_key, source, dest);
	index = batadv_nc_hash_choose(&nc_path_key, hash->size);

	/* Search for matching coding path */
	rcu_read_lock();
	hlist_for_each_entry_rcu(nc_path, &hash->table[index], hash_entry) {
		/* Find matching nc_packet */
		spin_lock_bh(&nc_path->packet_list_lock);
		list_for_each_entry(tmp_nc_packet,
				    &nc_path->packet_list, list) {
			if (packet_id == tmp_nc_packet->packet_id) {
				list_del(&tmp_nc_packet->list);

				nc_packet = tmp_nc_packet;
				break;
			}
		}
		spin_unlock_bh(&nc_path->packet_list_lock);

		if (nc_packet)
			break;
	}
	rcu_read_unlock();

	if (!nc_packet)
		batadv_dbg(BATADV_DBG_NC, bat_priv,
			   "No decoding packet found for %u\n", packet_id);

	return nc_packet;
}

/**
 * batadv_nc_recv_coded_packet - try to decode coded packet and enqueue the
 *  resulting unicast packet
 * @skb: incoming coded packet
 * @recv_if: pointer to interface this packet was received on
 */
static int batadv_nc_recv_coded_packet(struct sk_buff *skb,
				       struct batadv_hard_iface *recv_if)
{
	struct batadv_priv *bat_priv = netdev_priv(recv_if->soft_iface);
	struct batadv_unicast_packet *unicast_packet;
	struct batadv_coded_packet *coded_packet;
	struct batadv_nc_packet *nc_packet;
	struct ethhdr *ethhdr;
	int hdr_size = sizeof(*coded_packet);

	/* Check if network coding is enabled */
	if (!atomic_read(&bat_priv->network_coding))
		return NET_RX_DROP;

	/* Make sure we can access (and remove) header */
	if (unlikely(!pskb_may_pull(skb, hdr_size)))
		return NET_RX_DROP;

	coded_packet = (struct batadv_coded_packet *)skb->data;
	ethhdr = eth_hdr(skb);

	/* Verify frame is destined for us */
	if (!batadv_is_my_mac(bat_priv, ethhdr->h_dest) &&
	    !batadv_is_my_mac(bat_priv, coded_packet->second_dest))
		return NET_RX_DROP;

	/* Update stat counter */
	if (batadv_is_my_mac(bat_priv, coded_packet->second_dest))
		batadv_inc_counter(bat_priv, BATADV_CNT_NC_SNIFFED);

	nc_packet = batadv_nc_find_decoding_packet(bat_priv, ethhdr,
						   coded_packet);
	if (!nc_packet) {
		batadv_inc_counter(bat_priv, BATADV_CNT_NC_DECODE_FAILED);
		return NET_RX_DROP;
	}

	/* Make skb's linear, because decoding accesses the entire buffer */
	if (skb_linearize(skb) < 0)
		goto free_nc_packet;

	if (skb_linearize(nc_packet->skb) < 0)
		goto free_nc_packet;

	/* Decode the packet */
	unicast_packet = batadv_nc_skb_decode_packet(bat_priv, skb, nc_packet);
	if (!unicast_packet) {
		batadv_inc_counter(bat_priv, BATADV_CNT_NC_DECODE_FAILED);
		goto free_nc_packet;
	}

	/* Mark packet as decoded to do correct recoding when forwarding */
	BATADV_SKB_CB(skb)->decoded = true;
	batadv_inc_counter(bat_priv, BATADV_CNT_NC_DECODE);
	batadv_add_counter(bat_priv, BATADV_CNT_NC_DECODE_BYTES,
			   skb->len + ETH_HLEN);
	return batadv_recv_unicast_packet(skb, recv_if);

free_nc_packet:
	batadv_nc_packet_free(nc_packet);
	return NET_RX_DROP;
}

/**
 * batadv_nc_mesh_free - clean up network coding memory
 * @bat_priv: the bat priv with all the soft interface information
 */
void batadv_nc_mesh_free(struct batadv_priv *bat_priv)
{
	batadv_tvlv_container_unregister(bat_priv, BATADV_TVLV_NC, 1);
	batadv_tvlv_handler_unregister(bat_priv, BATADV_TVLV_NC, 1);
	cancel_delayed_work_sync(&bat_priv->nc.work);

	batadv_nc_purge_paths(bat_priv, bat_priv->nc.coding_hash, NULL);
	batadv_hash_destroy(bat_priv->nc.coding_hash);
	batadv_nc_purge_paths(bat_priv, bat_priv->nc.decoding_hash, NULL);
	batadv_hash_destroy(bat_priv->nc.decoding_hash);
}

/**
 * batadv_nc_nodes_seq_print_text - print the nc node information
 * @seq: seq file to print on
 * @offset: not used
 */
int batadv_nc_nodes_seq_print_text(struct seq_file *seq, void *offset)
{
	struct net_device *net_dev = (struct net_device *)seq->private;
	struct batadv_priv *bat_priv = netdev_priv(net_dev);
	struct batadv_hashtable *hash = bat_priv->orig_hash;
	struct batadv_hard_iface *primary_if;
	struct hlist_head *head;
	struct batadv_orig_node *orig_node;
	struct batadv_nc_node *nc_node;
	int i;

	primary_if = batadv_seq_print_text_primary_if_get(seq);
	if (!primary_if)
		goto out;

	/* Traverse list of originators */
	for (i = 0; i < hash->size; i++) {
		head = &hash->table[i];

		/* For each orig_node in this bin */
		rcu_read_lock();
		hlist_for_each_entry_rcu(orig_node, head, hash_entry) {
			/* no need to print the orig node if it does not have
			 * network coding neighbors
			 */
			if (list_empty(&orig_node->in_coding_list) &&
			    list_empty(&orig_node->out_coding_list))
				continue;

			seq_printf(seq, "Node:      %pM\n", orig_node->orig);

			seq_puts(seq, " Ingoing:  ");
			/* For each in_nc_node to this orig_node */
			list_for_each_entry_rcu(nc_node,
						&orig_node->in_coding_list,
						list)
				seq_printf(seq, "%pM ",
					   nc_node->addr);
			seq_puts(seq, "\n");

			seq_puts(seq, " Outgoing: ");
			/* For out_nc_node to this orig_node */
			list_for_each_entry_rcu(nc_node,
						&orig_node->out_coding_list,
						list)
				seq_printf(seq, "%pM ",
					   nc_node->addr);
			seq_puts(seq, "\n\n");
		}
		rcu_read_unlock();
	}

out:
	if (primary_if)
		batadv_hardif_free_ref(primary_if);
	return 0;
}

/**
 * batadv_nc_init_debugfs - create nc folder and related files in debugfs
 * @bat_priv: the bat priv with all the soft interface information
 */
int batadv_nc_init_debugfs(struct batadv_priv *bat_priv)
{
	struct dentry *nc_dir, *file;

	nc_dir = debugfs_create_dir("nc", bat_priv->debug_dir);
	if (!nc_dir)
		goto out;

	file = debugfs_create_u8("min_tq", S_IRUGO | S_IWUSR, nc_dir,
				 &bat_priv->nc.min_tq);
	if (!file)
		goto out;

	file = debugfs_create_u32("max_fwd_delay", S_IRUGO | S_IWUSR, nc_dir,
				  &bat_priv->nc.max_fwd_delay);
	if (!file)
		goto out;

	file = debugfs_create_u32("max_buffer_time", S_IRUGO | S_IWUSR, nc_dir,
				  &bat_priv->nc.max_buffer_time);
	if (!file)
		goto out;

	return 0;

out:
	return -ENOMEM;
}
