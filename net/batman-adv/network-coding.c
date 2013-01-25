/* Copyright (C) 2012-2013 B.A.T.M.A.N. contributors:
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA
 */

#include <linux/debugfs.h>

#include "main.h"
#include "network-coding.h"
#include "originator.h"
#include "hard-interface.h"

static void batadv_nc_worker(struct work_struct *work);

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
 * batadv_nc_init - initialise coding hash table and start house keeping
 * @bat_priv: the bat priv with all the soft interface information
 */
int batadv_nc_init(struct batadv_priv *bat_priv)
{
	INIT_DELAYED_WORK(&bat_priv->nc.work, batadv_nc_worker);
	batadv_nc_start_timer(bat_priv);

	return 0;
}

/**
 * batadv_nc_init_bat_priv - initialise the nc specific bat_priv variables
 * @bat_priv: the bat priv with all the soft interface information
 */
void batadv_nc_init_bat_priv(struct batadv_priv *bat_priv)
{
	atomic_set(&bat_priv->network_coding, 1);
	bat_priv->nc.min_tq = 200;
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
 * batadv_nc_worker - periodic task for house keeping related to network coding
 * @work: kernel work struct
 */
static void batadv_nc_worker(struct work_struct *work)
{
	struct delayed_work *delayed_work;
	struct batadv_priv_nc *priv_nc;
	struct batadv_priv *bat_priv;

	delayed_work = container_of(work, struct delayed_work, work);
	priv_nc = container_of(delayed_work, struct batadv_priv_nc, work);
	bat_priv = container_of(priv_nc, struct batadv_priv, nc);

	batadv_nc_purge_orig_hash(bat_priv);

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
	if (orig_node->last_real_seqno != ogm_packet->seqno)
		return false;
	if (orig_node->last_ttl != ogm_packet->header.ttl + 1)
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
	memcpy(nc_node->addr, orig_node->orig, ETH_ALEN);
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
 * batadv_nc_free - clean up network coding memory
 * @bat_priv: the bat priv with all the soft interface information
 */
void batadv_nc_free(struct batadv_priv *bat_priv)
{
	cancel_delayed_work_sync(&bat_priv->nc.work);
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
			seq_printf(seq, "Node:      %pM\n", orig_node->orig);

			seq_printf(seq, " Ingoing:  ");
			/* For each in_nc_node to this orig_node */
			list_for_each_entry_rcu(nc_node,
						&orig_node->in_coding_list,
						list)
				seq_printf(seq, "%pM ",
					   nc_node->addr);
			seq_printf(seq, "\n");

			seq_printf(seq, " Outgoing: ");
			/* For out_nc_node to this orig_node */
			list_for_each_entry_rcu(nc_node,
						&orig_node->out_coding_list,
						list)
				seq_printf(seq, "%pM ",
					   nc_node->addr);
			seq_printf(seq, "\n\n");
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

	return 0;

out:
	return -ENOMEM;
}
