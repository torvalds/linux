/* Copyright (C) 2009-2013 B.A.T.M.A.N. contributors:
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA
 */

#include "main.h"
#include "distributed-arp-table.h"
#include "originator.h"
#include "hash.h"
#include "translation-table.h"
#include "routing.h"
#include "gateway_client.h"
#include "hard-interface.h"
#include "soft-interface.h"
#include "bridge_loop_avoidance.h"
#include "network-coding.h"
#include "fragmentation.h"

/* hash class keys */
static struct lock_class_key batadv_orig_hash_lock_class_key;

static void batadv_purge_orig(struct work_struct *work);

/* returns 1 if they are the same originator */
int batadv_compare_orig(const struct hlist_node *node, const void *data2)
{
	const void *data1 = container_of(node, struct batadv_orig_node,
					 hash_entry);

	return (memcmp(data1, data2, ETH_ALEN) == 0 ? 1 : 0);
}

/**
 * batadv_orig_node_vlan_get - get an orig_node_vlan object
 * @orig_node: the originator serving the VLAN
 * @vid: the VLAN identifier
 *
 * Returns the vlan object identified by vid and belonging to orig_node or NULL
 * if it does not exist.
 */
struct batadv_orig_node_vlan *
batadv_orig_node_vlan_get(struct batadv_orig_node *orig_node,
			  unsigned short vid)
{
	struct batadv_orig_node_vlan *vlan = NULL, *tmp;

	rcu_read_lock();
	list_for_each_entry_rcu(tmp, &orig_node->vlan_list, list) {
		if (tmp->vid != vid)
			continue;

		if (!atomic_inc_not_zero(&tmp->refcount))
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
 * Returns NULL in case of failure or the vlan object identified by vid and
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

	atomic_set(&vlan->refcount, 2);
	vlan->vid = vid;

	list_add_rcu(&vlan->list, &orig_node->vlan_list);

out:
	spin_unlock_bh(&orig_node->vlan_list_lock);

	return vlan;
}

/**
 * batadv_orig_node_vlan_free_ref - decrement the refcounter and possibly free
 *  the originator-vlan object
 * @orig_vlan: the originator-vlan object to release
 */
void batadv_orig_node_vlan_free_ref(struct batadv_orig_node_vlan *orig_vlan)
{
	if (atomic_dec_and_test(&orig_vlan->refcount))
		kfree_rcu(orig_vlan, rcu);
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

void batadv_neigh_node_free_ref(struct batadv_neigh_node *neigh_node)
{
	if (atomic_dec_and_test(&neigh_node->refcount))
		kfree_rcu(neigh_node, rcu);
}

/* increases the refcounter of a found router */
struct batadv_neigh_node *
batadv_orig_node_get_router(struct batadv_orig_node *orig_node)
{
	struct batadv_neigh_node *router;

	rcu_read_lock();
	router = rcu_dereference(orig_node->router);

	if (router && !atomic_inc_not_zero(&router->refcount))
		router = NULL;

	rcu_read_unlock();
	return router;
}

/**
 * batadv_neigh_node_new - create and init a new neigh_node object
 * @hard_iface: the interface where the neighbour is connected to
 * @neigh_addr: the mac address of the neighbour interface
 * @orig_node: originator object representing the neighbour
 *
 * Allocates a new neigh_node object and initialises all the generic fields.
 * Returns the new object or NULL on failure.
 */
struct batadv_neigh_node *
batadv_neigh_node_new(struct batadv_hard_iface *hard_iface,
		      const uint8_t *neigh_addr,
		      struct batadv_orig_node *orig_node)
{
	struct batadv_neigh_node *neigh_node;

	neigh_node = kzalloc(sizeof(*neigh_node), GFP_ATOMIC);
	if (!neigh_node)
		goto out;

	INIT_HLIST_NODE(&neigh_node->list);

	memcpy(neigh_node->addr, neigh_addr, ETH_ALEN);
	neigh_node->if_incoming = hard_iface;
	neigh_node->orig_node = orig_node;

	INIT_LIST_HEAD(&neigh_node->bonding_list);

	/* extra reference for return */
	atomic_set(&neigh_node->refcount, 2);

out:
	return neigh_node;
}

static void batadv_orig_node_free_rcu(struct rcu_head *rcu)
{
	struct hlist_node *node_tmp;
	struct batadv_neigh_node *neigh_node, *tmp_neigh_node;
	struct batadv_orig_node *orig_node;

	orig_node = container_of(rcu, struct batadv_orig_node, rcu);

	spin_lock_bh(&orig_node->neigh_list_lock);

	/* for all bonding members ... */
	list_for_each_entry_safe(neigh_node, tmp_neigh_node,
				 &orig_node->bond_list, bonding_list) {
		list_del_rcu(&neigh_node->bonding_list);
		batadv_neigh_node_free_ref(neigh_node);
	}

	/* for all neighbors towards this originator ... */
	hlist_for_each_entry_safe(neigh_node, node_tmp,
				  &orig_node->neigh_list, list) {
		hlist_del_rcu(&neigh_node->list);
		batadv_neigh_node_free_ref(neigh_node);
	}

	spin_unlock_bh(&orig_node->neigh_list_lock);

	/* Free nc_nodes */
	batadv_nc_purge_orig(orig_node->bat_priv, orig_node, NULL);

	batadv_frag_purge_orig(orig_node, NULL);

	batadv_tt_global_del_orig(orig_node->bat_priv, orig_node, -1,
				  "originator timed out");

	if (orig_node->bat_priv->bat_algo_ops->bat_orig_free)
		orig_node->bat_priv->bat_algo_ops->bat_orig_free(orig_node);

	kfree(orig_node->tt_buff);
	kfree(orig_node);
}

/**
 * batadv_orig_node_free_ref - decrement the orig node refcounter and possibly
 * schedule an rcu callback for freeing it
 * @orig_node: the orig node to free
 */
void batadv_orig_node_free_ref(struct batadv_orig_node *orig_node)
{
	if (atomic_dec_and_test(&orig_node->refcount))
		call_rcu(&orig_node->rcu, batadv_orig_node_free_rcu);
}

/**
 * batadv_orig_node_free_ref_now - decrement the orig node refcounter and
 * possibly free it (without rcu callback)
 * @orig_node: the orig node to free
 */
void batadv_orig_node_free_ref_now(struct batadv_orig_node *orig_node)
{
	if (atomic_dec_and_test(&orig_node->refcount))
		batadv_orig_node_free_rcu(&orig_node->rcu);
}

void batadv_originator_free(struct batadv_priv *bat_priv)
{
	struct batadv_hashtable *hash = bat_priv->orig_hash;
	struct hlist_node *node_tmp;
	struct hlist_head *head;
	spinlock_t *list_lock; /* spinlock to protect write access */
	struct batadv_orig_node *orig_node;
	uint32_t i;

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
			batadv_orig_node_free_ref(orig_node);
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
 * Returns the newly created object or NULL on failure.
 */
struct batadv_orig_node *batadv_orig_node_new(struct batadv_priv *bat_priv,
					      const uint8_t *addr)
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
	INIT_LIST_HEAD(&orig_node->bond_list);
	INIT_LIST_HEAD(&orig_node->vlan_list);
	spin_lock_init(&orig_node->bcast_seqno_lock);
	spin_lock_init(&orig_node->neigh_list_lock);
	spin_lock_init(&orig_node->tt_buff_lock);
	spin_lock_init(&orig_node->tt_lock);
	spin_lock_init(&orig_node->vlan_list_lock);

	batadv_nc_init_orig(orig_node);

	/* extra reference for return */
	atomic_set(&orig_node->refcount, 2);

	orig_node->tt_initialised = false;
	orig_node->bat_priv = bat_priv;
	memcpy(orig_node->orig, addr, ETH_ALEN);
	batadv_dat_init_orig_node_addr(orig_node);
	orig_node->router = NULL;
	atomic_set(&orig_node->last_ttvn, 0);
	orig_node->tt_buff = NULL;
	orig_node->tt_buff_len = 0;
	reset_time = jiffies - 1 - msecs_to_jiffies(BATADV_RESET_PROTECTION_MS);
	orig_node->bcast_seqno_reset = reset_time;
	orig_node->batman_seqno_reset = reset_time;

	atomic_set(&orig_node->bond_candidates, 0);

	/* create a vlan object for the "untagged" LAN */
	vlan = batadv_orig_node_vlan_new(orig_node, BATADV_NO_FLAGS);
	if (!vlan)
		goto free_orig_node;
	/* batadv_orig_node_vlan_new() increases the refcounter.
	 * Immediately release vlan since it is not needed anymore in this
	 * context
	 */
	batadv_orig_node_vlan_free_ref(vlan);

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

static bool
batadv_purge_orig_neighbors(struct batadv_priv *bat_priv,
			    struct batadv_orig_node *orig_node,
			    struct batadv_neigh_node **best_neigh)
{
	struct batadv_algo_ops *bao = bat_priv->bat_algo_ops;
	struct hlist_node *node_tmp;
	struct batadv_neigh_node *neigh_node;
	bool neigh_purged = false;
	unsigned long last_seen;
	struct batadv_hard_iface *if_incoming;

	*best_neigh = NULL;

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
			batadv_bonding_candidate_del(orig_node, neigh_node);
			batadv_neigh_node_free_ref(neigh_node);
		} else {
			/* store the best_neighbour if this is the first
			 * iteration or if a better neighbor has been found
			 */
			if (!*best_neigh ||
			    bao->bat_neigh_cmp(neigh_node, *best_neigh) > 0)
				*best_neigh = neigh_node;
		}
	}

	spin_unlock_bh(&orig_node->neigh_list_lock);
	return neigh_purged;
}

static bool batadv_purge_orig_node(struct batadv_priv *bat_priv,
				   struct batadv_orig_node *orig_node)
{
	struct batadv_neigh_node *best_neigh_node;

	if (batadv_has_timed_out(orig_node->last_seen,
				 2 * BATADV_PURGE_TIMEOUT)) {
		batadv_dbg(BATADV_DBG_BATMAN, bat_priv,
			   "Originator timeout: originator %pM, last_seen %u\n",
			   orig_node->orig,
			   jiffies_to_msecs(orig_node->last_seen));
		return true;
	} else {
		if (batadv_purge_orig_neighbors(bat_priv, orig_node,
						&best_neigh_node))
			batadv_update_route(bat_priv, orig_node,
					    best_neigh_node);
	}

	return false;
}

static void _batadv_purge_orig(struct batadv_priv *bat_priv)
{
	struct batadv_hashtable *hash = bat_priv->orig_hash;
	struct hlist_node *node_tmp;
	struct hlist_head *head;
	spinlock_t *list_lock; /* spinlock to protect write access */
	struct batadv_orig_node *orig_node;
	uint32_t i;

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
				batadv_orig_node_free_ref(orig_node);
				continue;
			}

			batadv_frag_purge_orig(orig_node,
					       batadv_frag_check_entry);
		}
		spin_unlock_bh(list_lock);
	}

	batadv_gw_node_purge(bat_priv);
	batadv_gw_election(bat_priv);
}

static void batadv_purge_orig(struct work_struct *work)
{
	struct delayed_work *delayed_work;
	struct batadv_priv *bat_priv;

	delayed_work = container_of(work, struct delayed_work, work);
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

	batadv_hardif_free_ref(primary_if);

	if (!bat_priv->bat_algo_ops->bat_orig_print) {
		seq_puts(seq,
			 "No printing function for this routing protocol\n");
		return 0;
	}

	bat_priv->bat_algo_ops->bat_orig_print(bat_priv, seq);

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
	uint32_t i;
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
	uint32_t i;
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
