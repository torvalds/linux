/* Copyright (C) 2009-2012 B.A.T.M.A.N. contributors:
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
#include "originator.h"
#include "hash.h"
#include "translation-table.h"
#include "routing.h"
#include "gateway_client.h"
#include "hard-interface.h"
#include "unicast.h"
#include "soft-interface.h"
#include "bridge_loop_avoidance.h"

static void batadv_purge_orig(struct work_struct *work);

static void batadv_start_purge_timer(struct bat_priv *bat_priv)
{
	INIT_DELAYED_WORK(&bat_priv->orig_work, batadv_purge_orig);
	queue_delayed_work(batadv_event_workqueue,
			   &bat_priv->orig_work, msecs_to_jiffies(1000));
}

/* returns 1 if they are the same originator */
static int batadv_compare_orig(const struct hlist_node *node, const void *data2)
{
	const void *data1 = container_of(node, struct orig_node, hash_entry);

	return (memcmp(data1, data2, ETH_ALEN) == 0 ? 1 : 0);
}

int batadv_originator_init(struct bat_priv *bat_priv)
{
	if (bat_priv->orig_hash)
		return 0;

	bat_priv->orig_hash = batadv_hash_new(1024);

	if (!bat_priv->orig_hash)
		goto err;

	batadv_start_purge_timer(bat_priv);
	return 0;

err:
	return -ENOMEM;
}

void batadv_neigh_node_free_ref(struct neigh_node *neigh_node)
{
	if (atomic_dec_and_test(&neigh_node->refcount))
		kfree_rcu(neigh_node, rcu);
}

/* increases the refcounter of a found router */
struct neigh_node *batadv_orig_node_get_router(struct orig_node *orig_node)
{
	struct neigh_node *router;

	rcu_read_lock();
	router = rcu_dereference(orig_node->router);

	if (router && !atomic_inc_not_zero(&router->refcount))
		router = NULL;

	rcu_read_unlock();
	return router;
}

struct neigh_node *batadv_neigh_node_new(struct hard_iface *hard_iface,
					 const uint8_t *neigh_addr,
					 uint32_t seqno)
{
	struct bat_priv *bat_priv = netdev_priv(hard_iface->soft_iface);
	struct neigh_node *neigh_node;

	neigh_node = kzalloc(sizeof(*neigh_node), GFP_ATOMIC);
	if (!neigh_node)
		goto out;

	INIT_HLIST_NODE(&neigh_node->list);

	memcpy(neigh_node->addr, neigh_addr, ETH_ALEN);
	spin_lock_init(&neigh_node->lq_update_lock);

	/* extra reference for return */
	atomic_set(&neigh_node->refcount, 2);

	batadv_dbg(DBG_BATMAN, bat_priv,
		   "Creating new neighbor %pM, initial seqno %d\n",
		   neigh_addr, seqno);

out:
	return neigh_node;
}

static void batadv_orig_node_free_rcu(struct rcu_head *rcu)
{
	struct hlist_node *node, *node_tmp;
	struct neigh_node *neigh_node, *tmp_neigh_node;
	struct orig_node *orig_node;

	orig_node = container_of(rcu, struct orig_node, rcu);

	spin_lock_bh(&orig_node->neigh_list_lock);

	/* for all bonding members ... */
	list_for_each_entry_safe(neigh_node, tmp_neigh_node,
				 &orig_node->bond_list, bonding_list) {
		list_del_rcu(&neigh_node->bonding_list);
		batadv_neigh_node_free_ref(neigh_node);
	}

	/* for all neighbors towards this originator ... */
	hlist_for_each_entry_safe(neigh_node, node, node_tmp,
				  &orig_node->neigh_list, list) {
		hlist_del_rcu(&neigh_node->list);
		batadv_neigh_node_free_ref(neigh_node);
	}

	spin_unlock_bh(&orig_node->neigh_list_lock);

	batadv_frag_list_free(&orig_node->frag_list);
	batadv_tt_global_del_orig(orig_node->bat_priv, orig_node,
				  "originator timed out");

	kfree(orig_node->tt_buff);
	kfree(orig_node->bcast_own);
	kfree(orig_node->bcast_own_sum);
	kfree(orig_node);
}

void batadv_orig_node_free_ref(struct orig_node *orig_node)
{
	if (atomic_dec_and_test(&orig_node->refcount))
		call_rcu(&orig_node->rcu, batadv_orig_node_free_rcu);
}

void batadv_originator_free(struct bat_priv *bat_priv)
{
	struct hashtable_t *hash = bat_priv->orig_hash;
	struct hlist_node *node, *node_tmp;
	struct hlist_head *head;
	spinlock_t *list_lock; /* spinlock to protect write access */
	struct orig_node *orig_node;
	uint32_t i;

	if (!hash)
		return;

	cancel_delayed_work_sync(&bat_priv->orig_work);

	bat_priv->orig_hash = NULL;

	for (i = 0; i < hash->size; i++) {
		head = &hash->table[i];
		list_lock = &hash->list_locks[i];

		spin_lock_bh(list_lock);
		hlist_for_each_entry_safe(orig_node, node, node_tmp,
					  head, hash_entry) {

			hlist_del_rcu(node);
			batadv_orig_node_free_ref(orig_node);
		}
		spin_unlock_bh(list_lock);
	}

	batadv_hash_destroy(hash);
}

/* this function finds or creates an originator entry for the given
 * address if it does not exits
 */
struct orig_node *batadv_get_orig_node(struct bat_priv *bat_priv,
				       const uint8_t *addr)
{
	struct orig_node *orig_node;
	int size;
	int hash_added;
	unsigned long reset_time;

	orig_node = batadv_orig_hash_find(bat_priv, addr);
	if (orig_node)
		return orig_node;

	batadv_dbg(DBG_BATMAN, bat_priv, "Creating new originator: %pM\n",
		   addr);

	orig_node = kzalloc(sizeof(*orig_node), GFP_ATOMIC);
	if (!orig_node)
		return NULL;

	INIT_HLIST_HEAD(&orig_node->neigh_list);
	INIT_LIST_HEAD(&orig_node->bond_list);
	spin_lock_init(&orig_node->ogm_cnt_lock);
	spin_lock_init(&orig_node->bcast_seqno_lock);
	spin_lock_init(&orig_node->neigh_list_lock);
	spin_lock_init(&orig_node->tt_buff_lock);

	/* extra reference for return */
	atomic_set(&orig_node->refcount, 2);

	orig_node->tt_initialised = false;
	orig_node->tt_poss_change = false;
	orig_node->bat_priv = bat_priv;
	memcpy(orig_node->orig, addr, ETH_ALEN);
	orig_node->router = NULL;
	orig_node->tt_crc = 0;
	atomic_set(&orig_node->last_ttvn, 0);
	orig_node->tt_buff = NULL;
	orig_node->tt_buff_len = 0;
	atomic_set(&orig_node->tt_size, 0);
	reset_time = jiffies - 1 - msecs_to_jiffies(BATADV_RESET_PROTECTION_MS);
	orig_node->bcast_seqno_reset = reset_time;
	orig_node->batman_seqno_reset = reset_time;

	atomic_set(&orig_node->bond_candidates, 0);

	size = bat_priv->num_ifaces * sizeof(unsigned long) * BATADV_NUM_WORDS;

	orig_node->bcast_own = kzalloc(size, GFP_ATOMIC);
	if (!orig_node->bcast_own)
		goto free_orig_node;

	size = bat_priv->num_ifaces * sizeof(uint8_t);
	orig_node->bcast_own_sum = kzalloc(size, GFP_ATOMIC);

	INIT_LIST_HEAD(&orig_node->frag_list);
	orig_node->last_frag_packet = 0;

	if (!orig_node->bcast_own_sum)
		goto free_bcast_own;

	hash_added = batadv_hash_add(bat_priv->orig_hash, batadv_compare_orig,
				     batadv_choose_orig, orig_node,
				     &orig_node->hash_entry);
	if (hash_added != 0)
		goto free_bcast_own_sum;

	return orig_node;
free_bcast_own_sum:
	kfree(orig_node->bcast_own_sum);
free_bcast_own:
	kfree(orig_node->bcast_own);
free_orig_node:
	kfree(orig_node);
	return NULL;
}

static bool batadv_purge_orig_neighbors(struct bat_priv *bat_priv,
					struct orig_node *orig_node,
					struct neigh_node **best_neigh_node)
{
	struct hlist_node *node, *node_tmp;
	struct neigh_node *neigh_node;
	bool neigh_purged = false;
	unsigned long last_seen;
	struct hard_iface *if_incoming;

	*best_neigh_node = NULL;

	spin_lock_bh(&orig_node->neigh_list_lock);

	/* for all neighbors towards this originator ... */
	hlist_for_each_entry_safe(neigh_node, node, node_tmp,
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
				batadv_dbg(DBG_BATMAN, bat_priv,
					   "neighbor purge: originator %pM, neighbor: %pM, iface: %s\n",
					   orig_node->orig, neigh_node->addr,
					   if_incoming->net_dev->name);
			else
				batadv_dbg(DBG_BATMAN, bat_priv,
					   "neighbor timeout: originator %pM, neighbor: %pM, last_seen: %u\n",
					   orig_node->orig, neigh_node->addr,
					   jiffies_to_msecs(last_seen));

			neigh_purged = true;

			hlist_del_rcu(&neigh_node->list);
			batadv_bonding_candidate_del(orig_node, neigh_node);
			batadv_neigh_node_free_ref(neigh_node);
		} else {
			if ((!*best_neigh_node) ||
			    (neigh_node->tq_avg > (*best_neigh_node)->tq_avg))
				*best_neigh_node = neigh_node;
		}
	}

	spin_unlock_bh(&orig_node->neigh_list_lock);
	return neigh_purged;
}

static bool batadv_purge_orig_node(struct bat_priv *bat_priv,
				   struct orig_node *orig_node)
{
	struct neigh_node *best_neigh_node;

	if (batadv_has_timed_out(orig_node->last_seen,
				 2 * BATADV_PURGE_TIMEOUT)) {
		batadv_dbg(DBG_BATMAN, bat_priv,
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

static void _batadv_purge_orig(struct bat_priv *bat_priv)
{
	struct hashtable_t *hash = bat_priv->orig_hash;
	struct hlist_node *node, *node_tmp;
	struct hlist_head *head;
	spinlock_t *list_lock; /* spinlock to protect write access */
	struct orig_node *orig_node;
	uint32_t i;

	if (!hash)
		return;

	/* for all origins... */
	for (i = 0; i < hash->size; i++) {
		head = &hash->table[i];
		list_lock = &hash->list_locks[i];

		spin_lock_bh(list_lock);
		hlist_for_each_entry_safe(orig_node, node, node_tmp,
					  head, hash_entry) {
			if (batadv_purge_orig_node(bat_priv, orig_node)) {
				if (orig_node->gw_flags)
					batadv_gw_node_delete(bat_priv,
							      orig_node);
				hlist_del_rcu(node);
				batadv_orig_node_free_ref(orig_node);
				continue;
			}

			if (batadv_has_timed_out(orig_node->last_frag_packet,
						 BATADV_FRAG_TIMEOUT))
				batadv_frag_list_free(&orig_node->frag_list);
		}
		spin_unlock_bh(list_lock);
	}

	batadv_gw_node_purge(bat_priv);
	batadv_gw_election(bat_priv);
}

static void batadv_purge_orig(struct work_struct *work)
{
	struct delayed_work *delayed_work =
		container_of(work, struct delayed_work, work);
	struct bat_priv *bat_priv =
		container_of(delayed_work, struct bat_priv, orig_work);

	_batadv_purge_orig(bat_priv);
	batadv_start_purge_timer(bat_priv);
}

void batadv_purge_orig_ref(struct bat_priv *bat_priv)
{
	_batadv_purge_orig(bat_priv);
}

int batadv_orig_seq_print_text(struct seq_file *seq, void *offset)
{
	struct net_device *net_dev = (struct net_device *)seq->private;
	struct bat_priv *bat_priv = netdev_priv(net_dev);
	struct hashtable_t *hash = bat_priv->orig_hash;
	struct hlist_node *node, *node_tmp;
	struct hlist_head *head;
	struct hard_iface *primary_if;
	struct orig_node *orig_node;
	struct neigh_node *neigh_node, *neigh_node_tmp;
	int batman_count = 0;
	int last_seen_secs;
	int last_seen_msecs;
	uint32_t i;
	int ret = 0;

	primary_if = batadv_primary_if_get_selected(bat_priv);

	if (!primary_if) {
		ret = seq_printf(seq,
				 "BATMAN mesh %s disabled - please specify interfaces to enable it\n",
				 net_dev->name);
		goto out;
	}

	if (primary_if->if_status != BATADV_IF_ACTIVE) {
		ret = seq_printf(seq,
				 "BATMAN mesh %s disabled - primary interface not active\n",
				 net_dev->name);
		goto out;
	}

	seq_printf(seq, "[B.A.T.M.A.N. adv %s, MainIF/MAC: %s/%pM (%s)]\n",
		   BATADV_SOURCE_VERSION, primary_if->net_dev->name,
		   primary_if->net_dev->dev_addr, net_dev->name);
	seq_printf(seq, "  %-15s %s (%s/%i) %17s [%10s]: %20s ...\n",
		   "Originator", "last-seen", "#", BATADV_TQ_MAX_VALUE,
		   "Nexthop", "outgoingIF", "Potential nexthops");

	for (i = 0; i < hash->size; i++) {
		head = &hash->table[i];

		rcu_read_lock();
		hlist_for_each_entry_rcu(orig_node, node, head, hash_entry) {
			neigh_node = batadv_orig_node_get_router(orig_node);
			if (!neigh_node)
				continue;

			if (neigh_node->tq_avg == 0)
				goto next;

			last_seen_secs = jiffies_to_msecs(jiffies -
						orig_node->last_seen) / 1000;
			last_seen_msecs = jiffies_to_msecs(jiffies -
						orig_node->last_seen) % 1000;

			seq_printf(seq, "%pM %4i.%03is   (%3i) %pM [%10s]:",
				   orig_node->orig, last_seen_secs,
				   last_seen_msecs, neigh_node->tq_avg,
				   neigh_node->addr,
				   neigh_node->if_incoming->net_dev->name);

			hlist_for_each_entry_rcu(neigh_node_tmp, node_tmp,
						 &orig_node->neigh_list, list) {
				seq_printf(seq, " %pM (%3i)",
					   neigh_node_tmp->addr,
					   neigh_node_tmp->tq_avg);
			}

			seq_printf(seq, "\n");
			batman_count++;

next:
			batadv_neigh_node_free_ref(neigh_node);
		}
		rcu_read_unlock();
	}

	if (batman_count == 0)
		seq_printf(seq, "No batman nodes in range ...\n");

out:
	if (primary_if)
		batadv_hardif_free_ref(primary_if);
	return ret;
}

static int batadv_orig_node_add_if(struct orig_node *orig_node, int max_if_num)
{
	void *data_ptr;
	size_t data_size, old_size;

	data_size = max_if_num * sizeof(unsigned long) * BATADV_NUM_WORDS;
	old_size = (max_if_num - 1) * sizeof(unsigned long) * BATADV_NUM_WORDS;
	data_ptr = kmalloc(data_size, GFP_ATOMIC);
	if (!data_ptr)
		return -ENOMEM;

	memcpy(data_ptr, orig_node->bcast_own, old_size);
	kfree(orig_node->bcast_own);
	orig_node->bcast_own = data_ptr;

	data_ptr = kmalloc(max_if_num * sizeof(uint8_t), GFP_ATOMIC);
	if (!data_ptr)
		return -ENOMEM;

	memcpy(data_ptr, orig_node->bcast_own_sum,
	       (max_if_num - 1) * sizeof(uint8_t));
	kfree(orig_node->bcast_own_sum);
	orig_node->bcast_own_sum = data_ptr;

	return 0;
}

int batadv_orig_hash_add_if(struct hard_iface *hard_iface, int max_if_num)
{
	struct bat_priv *bat_priv = netdev_priv(hard_iface->soft_iface);
	struct hashtable_t *hash = bat_priv->orig_hash;
	struct hlist_node *node;
	struct hlist_head *head;
	struct orig_node *orig_node;
	uint32_t i;
	int ret;

	/* resize all orig nodes because orig_node->bcast_own(_sum) depend on
	 * if_num
	 */
	for (i = 0; i < hash->size; i++) {
		head = &hash->table[i];

		rcu_read_lock();
		hlist_for_each_entry_rcu(orig_node, node, head, hash_entry) {
			spin_lock_bh(&orig_node->ogm_cnt_lock);
			ret = batadv_orig_node_add_if(orig_node, max_if_num);
			spin_unlock_bh(&orig_node->ogm_cnt_lock);

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

static int batadv_orig_node_del_if(struct orig_node *orig_node,
				   int max_if_num, int del_if_num)
{
	void *data_ptr = NULL;
	int chunk_size;

	/* last interface was removed */
	if (max_if_num == 0)
		goto free_bcast_own;

	chunk_size = sizeof(unsigned long) * BATADV_NUM_WORDS;
	data_ptr = kmalloc(max_if_num * chunk_size, GFP_ATOMIC);
	if (!data_ptr)
		return -ENOMEM;

	/* copy first part */
	memcpy(data_ptr, orig_node->bcast_own, del_if_num * chunk_size);

	/* copy second part */
	memcpy((char *)data_ptr + del_if_num * chunk_size,
	       orig_node->bcast_own + ((del_if_num + 1) * chunk_size),
	       (max_if_num - del_if_num) * chunk_size);

free_bcast_own:
	kfree(orig_node->bcast_own);
	orig_node->bcast_own = data_ptr;

	if (max_if_num == 0)
		goto free_own_sum;

	data_ptr = kmalloc(max_if_num * sizeof(uint8_t), GFP_ATOMIC);
	if (!data_ptr)
		return -ENOMEM;

	memcpy(data_ptr, orig_node->bcast_own_sum,
	       del_if_num * sizeof(uint8_t));

	memcpy((char *)data_ptr + del_if_num * sizeof(uint8_t),
	       orig_node->bcast_own_sum + ((del_if_num + 1) * sizeof(uint8_t)),
	       (max_if_num - del_if_num) * sizeof(uint8_t));

free_own_sum:
	kfree(orig_node->bcast_own_sum);
	orig_node->bcast_own_sum = data_ptr;

	return 0;
}

int batadv_orig_hash_del_if(struct hard_iface *hard_iface, int max_if_num)
{
	struct bat_priv *bat_priv = netdev_priv(hard_iface->soft_iface);
	struct hashtable_t *hash = bat_priv->orig_hash;
	struct hlist_node *node;
	struct hlist_head *head;
	struct hard_iface *hard_iface_tmp;
	struct orig_node *orig_node;
	uint32_t i;
	int ret;

	/* resize all orig nodes because orig_node->bcast_own(_sum) depend on
	 * if_num
	 */
	for (i = 0; i < hash->size; i++) {
		head = &hash->table[i];

		rcu_read_lock();
		hlist_for_each_entry_rcu(orig_node, node, head, hash_entry) {
			spin_lock_bh(&orig_node->ogm_cnt_lock);
			ret = batadv_orig_node_del_if(orig_node, max_if_num,
						      hard_iface->if_num);
			spin_unlock_bh(&orig_node->ogm_cnt_lock);

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
