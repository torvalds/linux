/* Copyright (C) 2007-2012 B.A.T.M.A.N. contributors:
 *
 * Marek Lindner, Simon Wunderlich, Antonio Quartulli
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
#include "translation-table.h"
#include "soft-interface.h"
#include "hard-interface.h"
#include "send.h"
#include "hash.h"
#include "originator.h"
#include "routing.h"
#include "bridge_loop_avoidance.h"

#include <linux/crc16.h>

static void batadv_send_roam_adv(struct batadv_priv *bat_priv, uint8_t *client,
				 struct batadv_orig_node *orig_node);
static void batadv_tt_purge(struct work_struct *work);
static void
batadv_tt_global_del_orig_list(struct batadv_tt_global_entry *tt_global_entry);

/* returns 1 if they are the same mac addr */
static int batadv_compare_tt(const struct hlist_node *node, const void *data2)
{
	const void *data1 = container_of(node, struct batadv_tt_common_entry,
					 hash_entry);

	return (memcmp(data1, data2, ETH_ALEN) == 0 ? 1 : 0);
}

static void batadv_tt_start_timer(struct batadv_priv *bat_priv)
{
	INIT_DELAYED_WORK(&bat_priv->tt_work, batadv_tt_purge);
	queue_delayed_work(batadv_event_workqueue, &bat_priv->tt_work,
			   msecs_to_jiffies(5000));
}

static struct batadv_tt_common_entry *
batadv_tt_hash_find(struct batadv_hashtable *hash, const void *data)
{
	struct hlist_head *head;
	struct hlist_node *node;
	struct batadv_tt_common_entry *tt_common_entry;
	struct batadv_tt_common_entry *tt_common_entry_tmp = NULL;
	uint32_t index;

	if (!hash)
		return NULL;

	index = batadv_choose_orig(data, hash->size);
	head = &hash->table[index];

	rcu_read_lock();
	hlist_for_each_entry_rcu(tt_common_entry, node, head, hash_entry) {
		if (!batadv_compare_eth(tt_common_entry, data))
			continue;

		if (!atomic_inc_not_zero(&tt_common_entry->refcount))
			continue;

		tt_common_entry_tmp = tt_common_entry;
		break;
	}
	rcu_read_unlock();

	return tt_common_entry_tmp;
}

static struct batadv_tt_local_entry *
batadv_tt_local_hash_find(struct batadv_priv *bat_priv, const void *data)
{
	struct batadv_tt_common_entry *tt_common_entry;
	struct batadv_tt_local_entry *tt_local_entry = NULL;

	tt_common_entry = batadv_tt_hash_find(bat_priv->tt_local_hash, data);
	if (tt_common_entry)
		tt_local_entry = container_of(tt_common_entry,
					      struct batadv_tt_local_entry,
					      common);
	return tt_local_entry;
}

static struct batadv_tt_global_entry *
batadv_tt_global_hash_find(struct batadv_priv *bat_priv, const void *data)
{
	struct batadv_tt_common_entry *tt_common_entry;
	struct batadv_tt_global_entry *tt_global_entry = NULL;

	tt_common_entry = batadv_tt_hash_find(bat_priv->tt_global_hash, data);
	if (tt_common_entry)
		tt_global_entry = container_of(tt_common_entry,
					       struct batadv_tt_global_entry,
					       common);
	return tt_global_entry;

}

static void
batadv_tt_local_entry_free_ref(struct batadv_tt_local_entry *tt_local_entry)
{
	if (atomic_dec_and_test(&tt_local_entry->common.refcount))
		kfree_rcu(tt_local_entry, common.rcu);
}

static void batadv_tt_global_entry_free_rcu(struct rcu_head *rcu)
{
	struct batadv_tt_common_entry *tt_common_entry;
	struct batadv_tt_global_entry *tt_global_entry;

	tt_common_entry = container_of(rcu, struct batadv_tt_common_entry, rcu);
	tt_global_entry = container_of(tt_common_entry,
				       struct batadv_tt_global_entry, common);

	kfree(tt_global_entry);
}

static void
batadv_tt_global_entry_free_ref(struct batadv_tt_global_entry *tt_global_entry)
{
	if (atomic_dec_and_test(&tt_global_entry->common.refcount)) {
		batadv_tt_global_del_orig_list(tt_global_entry);
		call_rcu(&tt_global_entry->common.rcu,
			 batadv_tt_global_entry_free_rcu);
	}
}

static void batadv_tt_orig_list_entry_free_rcu(struct rcu_head *rcu)
{
	struct batadv_tt_orig_list_entry *orig_entry;

	orig_entry = container_of(rcu, struct batadv_tt_orig_list_entry, rcu);
	batadv_orig_node_free_ref(orig_entry->orig_node);
	kfree(orig_entry);
}

static void
batadv_tt_orig_list_entry_free_ref(struct batadv_tt_orig_list_entry *orig_entry)
{
	/* to avoid race conditions, immediately decrease the tt counter */
	atomic_dec(&orig_entry->orig_node->tt_size);
	call_rcu(&orig_entry->rcu, batadv_tt_orig_list_entry_free_rcu);
}

static void batadv_tt_local_event(struct batadv_priv *bat_priv,
				  const uint8_t *addr, uint8_t flags)
{
	struct batadv_tt_change_node *tt_change_node, *entry, *safe;
	bool event_removed = false;
	bool del_op_requested, del_op_entry;

	tt_change_node = kmalloc(sizeof(*tt_change_node), GFP_ATOMIC);

	if (!tt_change_node)
		return;

	tt_change_node->change.flags = flags;
	memcpy(tt_change_node->change.addr, addr, ETH_ALEN);

	del_op_requested = flags & BATADV_TT_CLIENT_DEL;

	/* check for ADD+DEL or DEL+ADD events */
	spin_lock_bh(&bat_priv->tt_changes_list_lock);
	list_for_each_entry_safe(entry, safe, &bat_priv->tt_changes_list,
				 list) {
		if (!batadv_compare_eth(entry->change.addr, addr))
			continue;

		/* DEL+ADD in the same orig interval have no effect and can be
		 * removed to avoid silly behaviour on the receiver side. The
		 * other way around (ADD+DEL) can happen in case of roaming of
		 * a client still in the NEW state. Roaming of NEW clients is
		 * now possible due to automatically recognition of "temporary"
		 * clients
		 */
		del_op_entry = entry->change.flags & BATADV_TT_CLIENT_DEL;
		if (!del_op_requested && del_op_entry)
			goto del;
		if (del_op_requested && !del_op_entry)
			goto del;
		continue;
del:
		list_del(&entry->list);
		kfree(entry);
		event_removed = true;
		goto unlock;
	}

	/* track the change in the OGMinterval list */
	list_add_tail(&tt_change_node->list, &bat_priv->tt_changes_list);

unlock:
	spin_unlock_bh(&bat_priv->tt_changes_list_lock);

	if (event_removed)
		atomic_dec(&bat_priv->tt_local_changes);
	else
		atomic_inc(&bat_priv->tt_local_changes);
}

int batadv_tt_len(int changes_num)
{
	return changes_num * sizeof(struct batadv_tt_change);
}

static int batadv_tt_local_init(struct batadv_priv *bat_priv)
{
	if (bat_priv->tt_local_hash)
		return 0;

	bat_priv->tt_local_hash = batadv_hash_new(1024);

	if (!bat_priv->tt_local_hash)
		return -ENOMEM;

	return 0;
}

void batadv_tt_local_add(struct net_device *soft_iface, const uint8_t *addr,
			 int ifindex)
{
	struct batadv_priv *bat_priv = netdev_priv(soft_iface);
	struct batadv_tt_local_entry *tt_local_entry = NULL;
	struct batadv_tt_global_entry *tt_global_entry = NULL;
	struct hlist_head *head;
	struct hlist_node *node;
	struct batadv_tt_orig_list_entry *orig_entry;
	int hash_added;

	tt_local_entry = batadv_tt_local_hash_find(bat_priv, addr);

	if (tt_local_entry) {
		tt_local_entry->last_seen = jiffies;
		/* possibly unset the BATADV_TT_CLIENT_PENDING flag */
		tt_local_entry->common.flags &= ~BATADV_TT_CLIENT_PENDING;
		goto out;
	}

	tt_local_entry = kmalloc(sizeof(*tt_local_entry), GFP_ATOMIC);
	if (!tt_local_entry)
		goto out;

	batadv_dbg(BATADV_DBG_TT, bat_priv,
		   "Creating new local tt entry: %pM (ttvn: %d)\n", addr,
		   (uint8_t)atomic_read(&bat_priv->ttvn));

	memcpy(tt_local_entry->common.addr, addr, ETH_ALEN);
	tt_local_entry->common.flags = BATADV_NO_FLAGS;
	if (batadv_is_wifi_iface(ifindex))
		tt_local_entry->common.flags |= BATADV_TT_CLIENT_WIFI;
	atomic_set(&tt_local_entry->common.refcount, 2);
	tt_local_entry->last_seen = jiffies;

	/* the batman interface mac address should never be purged */
	if (batadv_compare_eth(addr, soft_iface->dev_addr))
		tt_local_entry->common.flags |= BATADV_TT_CLIENT_NOPURGE;

	/* The local entry has to be marked as NEW to avoid to send it in
	 * a full table response going out before the next ttvn increment
	 * (consistency check)
	 */
	tt_local_entry->common.flags |= BATADV_TT_CLIENT_NEW;

	hash_added = batadv_hash_add(bat_priv->tt_local_hash, batadv_compare_tt,
				     batadv_choose_orig,
				     &tt_local_entry->common,
				     &tt_local_entry->common.hash_entry);

	if (unlikely(hash_added != 0)) {
		/* remove the reference for the hash */
		batadv_tt_local_entry_free_ref(tt_local_entry);
		goto out;
	}

	batadv_tt_local_event(bat_priv, addr, tt_local_entry->common.flags);

	/* remove address from global hash if present */
	tt_global_entry = batadv_tt_global_hash_find(bat_priv, addr);

	/* Check whether it is a roaming! */
	if (tt_global_entry) {
		/* These node are probably going to update their tt table */
		head = &tt_global_entry->orig_list;
		rcu_read_lock();
		hlist_for_each_entry_rcu(orig_entry, node, head, list) {
			orig_entry->orig_node->tt_poss_change = true;

			batadv_send_roam_adv(bat_priv,
					     tt_global_entry->common.addr,
					     orig_entry->orig_node);
		}
		rcu_read_unlock();
		/* The global entry has to be marked as ROAMING and
		 * has to be kept for consistency purpose
		 */
		tt_global_entry->common.flags |= BATADV_TT_CLIENT_ROAM;
		tt_global_entry->roam_at = jiffies;
	}
out:
	if (tt_local_entry)
		batadv_tt_local_entry_free_ref(tt_local_entry);
	if (tt_global_entry)
		batadv_tt_global_entry_free_ref(tt_global_entry);
}

static void batadv_tt_realloc_packet_buff(unsigned char **packet_buff,
					  int *packet_buff_len,
					  int min_packet_len,
					  int new_packet_len)
{
	unsigned char *new_buff;

	new_buff = kmalloc(new_packet_len, GFP_ATOMIC);

	/* keep old buffer if kmalloc should fail */
	if (new_buff) {
		memcpy(new_buff, *packet_buff, min_packet_len);
		kfree(*packet_buff);
		*packet_buff = new_buff;
		*packet_buff_len = new_packet_len;
	}
}

static void batadv_tt_prepare_packet_buff(struct batadv_priv *bat_priv,
					  unsigned char **packet_buff,
					  int *packet_buff_len,
					  int min_packet_len)
{
	struct batadv_hard_iface *primary_if;
	int req_len;

	primary_if = batadv_primary_if_get_selected(bat_priv);

	req_len = min_packet_len;
	req_len += batadv_tt_len(atomic_read(&bat_priv->tt_local_changes));

	/* if we have too many changes for one packet don't send any
	 * and wait for the tt table request which will be fragmented
	 */
	if ((!primary_if) || (req_len > primary_if->soft_iface->mtu))
		req_len = min_packet_len;

	batadv_tt_realloc_packet_buff(packet_buff, packet_buff_len,
				      min_packet_len, req_len);

	if (primary_if)
		batadv_hardif_free_ref(primary_if);
}

static int batadv_tt_changes_fill_buff(struct batadv_priv *bat_priv,
				       unsigned char **packet_buff,
				       int *packet_buff_len,
				       int min_packet_len)
{
	struct batadv_tt_change_node *entry, *safe;
	int count = 0, tot_changes = 0, new_len;
	unsigned char *tt_buff;

	batadv_tt_prepare_packet_buff(bat_priv, packet_buff,
				      packet_buff_len, min_packet_len);

	new_len = *packet_buff_len - min_packet_len;
	tt_buff = *packet_buff + min_packet_len;

	if (new_len > 0)
		tot_changes = new_len / batadv_tt_len(1);

	spin_lock_bh(&bat_priv->tt_changes_list_lock);
	atomic_set(&bat_priv->tt_local_changes, 0);

	list_for_each_entry_safe(entry, safe, &bat_priv->tt_changes_list,
				 list) {
		if (count < tot_changes) {
			memcpy(tt_buff + batadv_tt_len(count),
			       &entry->change, sizeof(struct batadv_tt_change));
			count++;
		}
		list_del(&entry->list);
		kfree(entry);
	}
	spin_unlock_bh(&bat_priv->tt_changes_list_lock);

	/* Keep the buffer for possible tt_request */
	spin_lock_bh(&bat_priv->tt_buff_lock);
	kfree(bat_priv->tt_buff);
	bat_priv->tt_buff_len = 0;
	bat_priv->tt_buff = NULL;
	/* check whether this new OGM has no changes due to size problems */
	if (new_len > 0) {
		/* if kmalloc() fails we will reply with the full table
		 * instead of providing the diff
		 */
		bat_priv->tt_buff = kmalloc(new_len, GFP_ATOMIC);
		if (bat_priv->tt_buff) {
			memcpy(bat_priv->tt_buff, tt_buff, new_len);
			bat_priv->tt_buff_len = new_len;
		}
	}
	spin_unlock_bh(&bat_priv->tt_buff_lock);

	return count;
}

int batadv_tt_local_seq_print_text(struct seq_file *seq, void *offset)
{
	struct net_device *net_dev = (struct net_device *)seq->private;
	struct batadv_priv *bat_priv = netdev_priv(net_dev);
	struct batadv_hashtable *hash = bat_priv->tt_local_hash;
	struct batadv_tt_common_entry *tt_common_entry;
	struct batadv_hard_iface *primary_if;
	struct hlist_node *node;
	struct hlist_head *head;
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

	seq_printf(seq,
		   "Locally retrieved addresses (from %s) announced via TT (TTVN: %u):\n",
		   net_dev->name, (uint8_t)atomic_read(&bat_priv->ttvn));

	for (i = 0; i < hash->size; i++) {
		head = &hash->table[i];

		rcu_read_lock();
		hlist_for_each_entry_rcu(tt_common_entry, node,
					 head, hash_entry) {
			seq_printf(seq, " * %pM [%c%c%c%c%c]\n",
				   tt_common_entry->addr,
				   (tt_common_entry->flags &
				    BATADV_TT_CLIENT_ROAM ? 'R' : '.'),
				   (tt_common_entry->flags &
				    BATADV_TT_CLIENT_NOPURGE ? 'P' : '.'),
				   (tt_common_entry->flags &
				    BATADV_TT_CLIENT_NEW ? 'N' : '.'),
				   (tt_common_entry->flags &
				    BATADV_TT_CLIENT_PENDING ? 'X' : '.'),
				   (tt_common_entry->flags &
				    BATADV_TT_CLIENT_WIFI ? 'W' : '.'));
		}
		rcu_read_unlock();
	}
out:
	if (primary_if)
		batadv_hardif_free_ref(primary_if);
	return ret;
}

static void
batadv_tt_local_set_pending(struct batadv_priv *bat_priv,
			    struct batadv_tt_local_entry *tt_local_entry,
			    uint16_t flags, const char *message)
{
	batadv_tt_local_event(bat_priv, tt_local_entry->common.addr,
			      tt_local_entry->common.flags | flags);

	/* The local client has to be marked as "pending to be removed" but has
	 * to be kept in the table in order to send it in a full table
	 * response issued before the net ttvn increment (consistency check)
	 */
	tt_local_entry->common.flags |= BATADV_TT_CLIENT_PENDING;

	batadv_dbg(BATADV_DBG_TT, bat_priv,
		   "Local tt entry (%pM) pending to be removed: %s\n",
		   tt_local_entry->common.addr, message);
}

void batadv_tt_local_remove(struct batadv_priv *bat_priv, const uint8_t *addr,
			    const char *message, bool roaming)
{
	struct batadv_tt_local_entry *tt_local_entry = NULL;
	uint16_t flags;

	tt_local_entry = batadv_tt_local_hash_find(bat_priv, addr);
	if (!tt_local_entry)
		goto out;

	flags = BATADV_TT_CLIENT_DEL;
	if (roaming)
		flags |= BATADV_TT_CLIENT_ROAM;

	batadv_tt_local_set_pending(bat_priv, tt_local_entry, flags, message);
out:
	if (tt_local_entry)
		batadv_tt_local_entry_free_ref(tt_local_entry);
}

static void batadv_tt_local_purge_list(struct batadv_priv *bat_priv,
				       struct hlist_head *head)
{
	struct batadv_tt_local_entry *tt_local_entry;
	struct batadv_tt_common_entry *tt_common_entry;
	struct hlist_node *node, *node_tmp;

	hlist_for_each_entry_safe(tt_common_entry, node, node_tmp, head,
				  hash_entry) {
		tt_local_entry = container_of(tt_common_entry,
					      struct batadv_tt_local_entry,
					      common);
		if (tt_local_entry->common.flags & BATADV_TT_CLIENT_NOPURGE)
			continue;

		/* entry already marked for deletion */
		if (tt_local_entry->common.flags & BATADV_TT_CLIENT_PENDING)
			continue;

		if (!batadv_has_timed_out(tt_local_entry->last_seen,
					  BATADV_TT_LOCAL_TIMEOUT))
			continue;

		batadv_tt_local_set_pending(bat_priv, tt_local_entry,
					    BATADV_TT_CLIENT_DEL, "timed out");
	}
}

static void batadv_tt_local_purge(struct batadv_priv *bat_priv)
{
	struct batadv_hashtable *hash = bat_priv->tt_local_hash;
	struct hlist_head *head;
	spinlock_t *list_lock; /* protects write access to the hash lists */
	uint32_t i;

	for (i = 0; i < hash->size; i++) {
		head = &hash->table[i];
		list_lock = &hash->list_locks[i];

		spin_lock_bh(list_lock);
		batadv_tt_local_purge_list(bat_priv, head);
		spin_unlock_bh(list_lock);
	}

}

static void batadv_tt_local_table_free(struct batadv_priv *bat_priv)
{
	struct batadv_hashtable *hash;
	spinlock_t *list_lock; /* protects write access to the hash lists */
	struct batadv_tt_common_entry *tt_common_entry;
	struct batadv_tt_local_entry *tt_local;
	struct hlist_node *node, *node_tmp;
	struct hlist_head *head;
	uint32_t i;

	if (!bat_priv->tt_local_hash)
		return;

	hash = bat_priv->tt_local_hash;

	for (i = 0; i < hash->size; i++) {
		head = &hash->table[i];
		list_lock = &hash->list_locks[i];

		spin_lock_bh(list_lock);
		hlist_for_each_entry_safe(tt_common_entry, node, node_tmp,
					  head, hash_entry) {
			hlist_del_rcu(node);
			tt_local = container_of(tt_common_entry,
						struct batadv_tt_local_entry,
						common);
			batadv_tt_local_entry_free_ref(tt_local);
		}
		spin_unlock_bh(list_lock);
	}

	batadv_hash_destroy(hash);

	bat_priv->tt_local_hash = NULL;
}

static int batadv_tt_global_init(struct batadv_priv *bat_priv)
{
	if (bat_priv->tt_global_hash)
		return 0;

	bat_priv->tt_global_hash = batadv_hash_new(1024);

	if (!bat_priv->tt_global_hash)
		return -ENOMEM;

	return 0;
}

static void batadv_tt_changes_list_free(struct batadv_priv *bat_priv)
{
	struct batadv_tt_change_node *entry, *safe;

	spin_lock_bh(&bat_priv->tt_changes_list_lock);

	list_for_each_entry_safe(entry, safe, &bat_priv->tt_changes_list,
				 list) {
		list_del(&entry->list);
		kfree(entry);
	}

	atomic_set(&bat_priv->tt_local_changes, 0);
	spin_unlock_bh(&bat_priv->tt_changes_list_lock);
}

/* find out if an orig_node is already in the list of a tt_global_entry.
 * returns 1 if found, 0 otherwise
 */
static bool
batadv_tt_global_entry_has_orig(const struct batadv_tt_global_entry *entry,
				const struct batadv_orig_node *orig_node)
{
	struct batadv_tt_orig_list_entry *tmp_orig_entry;
	const struct hlist_head *head;
	struct hlist_node *node;
	bool found = false;

	rcu_read_lock();
	head = &entry->orig_list;
	hlist_for_each_entry_rcu(tmp_orig_entry, node, head, list) {
		if (tmp_orig_entry->orig_node == orig_node) {
			found = true;
			break;
		}
	}
	rcu_read_unlock();
	return found;
}

static void
batadv_tt_global_add_orig_entry(struct batadv_tt_global_entry *tt_global_entry,
				struct batadv_orig_node *orig_node, int ttvn)
{
	struct batadv_tt_orig_list_entry *orig_entry;

	orig_entry = kzalloc(sizeof(*orig_entry), GFP_ATOMIC);
	if (!orig_entry)
		return;

	INIT_HLIST_NODE(&orig_entry->list);
	atomic_inc(&orig_node->refcount);
	atomic_inc(&orig_node->tt_size);
	orig_entry->orig_node = orig_node;
	orig_entry->ttvn = ttvn;

	spin_lock_bh(&tt_global_entry->list_lock);
	hlist_add_head_rcu(&orig_entry->list,
			   &tt_global_entry->orig_list);
	spin_unlock_bh(&tt_global_entry->list_lock);
}

/* caller must hold orig_node refcount */
int batadv_tt_global_add(struct batadv_priv *bat_priv,
			 struct batadv_orig_node *orig_node,
			 const unsigned char *tt_addr, uint8_t flags,
			 uint8_t ttvn)
{
	struct batadv_tt_global_entry *tt_global_entry = NULL;
	int ret = 0;
	int hash_added;
	struct batadv_tt_common_entry *common;

	tt_global_entry = batadv_tt_global_hash_find(bat_priv, tt_addr);

	if (!tt_global_entry) {
		tt_global_entry = kzalloc(sizeof(*tt_global_entry), GFP_ATOMIC);
		if (!tt_global_entry)
			goto out;

		common = &tt_global_entry->common;
		memcpy(common->addr, tt_addr, ETH_ALEN);

		common->flags = flags;
		tt_global_entry->roam_at = 0;
		atomic_set(&common->refcount, 2);

		INIT_HLIST_HEAD(&tt_global_entry->orig_list);
		spin_lock_init(&tt_global_entry->list_lock);

		hash_added = batadv_hash_add(bat_priv->tt_global_hash,
					     batadv_compare_tt,
					     batadv_choose_orig, common,
					     &common->hash_entry);

		if (unlikely(hash_added != 0)) {
			/* remove the reference for the hash */
			batadv_tt_global_entry_free_ref(tt_global_entry);
			goto out_remove;
		}

		batadv_tt_global_add_orig_entry(tt_global_entry, orig_node,
						ttvn);
	} else {
		/* there is already a global entry, use this one. */

		/* If there is the BATADV_TT_CLIENT_ROAM flag set, there is only
		 * one originator left in the list and we previously received a
		 * delete + roaming change for this originator.
		 *
		 * We should first delete the old originator before adding the
		 * new one.
		 */
		if (tt_global_entry->common.flags & BATADV_TT_CLIENT_ROAM) {
			batadv_tt_global_del_orig_list(tt_global_entry);
			tt_global_entry->common.flags &= ~BATADV_TT_CLIENT_ROAM;
			tt_global_entry->roam_at = 0;
		}

		if (!batadv_tt_global_entry_has_orig(tt_global_entry,
						     orig_node))
			batadv_tt_global_add_orig_entry(tt_global_entry,
							orig_node, ttvn);
	}

	batadv_dbg(BATADV_DBG_TT, bat_priv,
		   "Creating new global tt entry: %pM (via %pM)\n",
		   tt_global_entry->common.addr, orig_node->orig);

out_remove:
	/* remove address from local hash if present */
	batadv_tt_local_remove(bat_priv, tt_global_entry->common.addr,
			       "global tt received",
			       flags & BATADV_TT_CLIENT_ROAM);
	ret = 1;
out:
	if (tt_global_entry)
		batadv_tt_global_entry_free_ref(tt_global_entry);
	return ret;
}

/* print all orig nodes who announce the address for this global entry.
 * it is assumed that the caller holds rcu_read_lock();
 */
static void
batadv_tt_global_print_entry(struct batadv_tt_global_entry *tt_global_entry,
			     struct seq_file *seq)
{
	struct hlist_head *head;
	struct hlist_node *node;
	struct batadv_tt_orig_list_entry *orig_entry;
	struct batadv_tt_common_entry *tt_common_entry;
	uint16_t flags;
	uint8_t last_ttvn;

	tt_common_entry = &tt_global_entry->common;

	head = &tt_global_entry->orig_list;

	hlist_for_each_entry_rcu(orig_entry, node, head, list) {
		flags = tt_common_entry->flags;
		last_ttvn = atomic_read(&orig_entry->orig_node->last_ttvn);
		seq_printf(seq, " * %pM  (%3u) via %pM     (%3u)   [%c%c]\n",
			   tt_global_entry->common.addr, orig_entry->ttvn,
			   orig_entry->orig_node->orig, last_ttvn,
			   (flags & BATADV_TT_CLIENT_ROAM ? 'R' : '.'),
			   (flags & BATADV_TT_CLIENT_WIFI ? 'W' : '.'));
	}
}

int batadv_tt_global_seq_print_text(struct seq_file *seq, void *offset)
{
	struct net_device *net_dev = (struct net_device *)seq->private;
	struct batadv_priv *bat_priv = netdev_priv(net_dev);
	struct batadv_hashtable *hash = bat_priv->tt_global_hash;
	struct batadv_tt_common_entry *tt_common_entry;
	struct batadv_tt_global_entry *tt_global;
	struct batadv_hard_iface *primary_if;
	struct hlist_node *node;
	struct hlist_head *head;
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

	seq_printf(seq,
		   "Globally announced TT entries received via the mesh %s\n",
		   net_dev->name);
	seq_printf(seq, "       %-13s %s       %-15s %s %s\n",
		   "Client", "(TTVN)", "Originator", "(Curr TTVN)", "Flags");

	for (i = 0; i < hash->size; i++) {
		head = &hash->table[i];

		rcu_read_lock();
		hlist_for_each_entry_rcu(tt_common_entry, node,
					 head, hash_entry) {
			tt_global = container_of(tt_common_entry,
						 struct batadv_tt_global_entry,
						 common);
			batadv_tt_global_print_entry(tt_global, seq);
		}
		rcu_read_unlock();
	}
out:
	if (primary_if)
		batadv_hardif_free_ref(primary_if);
	return ret;
}

/* deletes the orig list of a tt_global_entry */
static void
batadv_tt_global_del_orig_list(struct batadv_tt_global_entry *tt_global_entry)
{
	struct hlist_head *head;
	struct hlist_node *node, *safe;
	struct batadv_tt_orig_list_entry *orig_entry;

	spin_lock_bh(&tt_global_entry->list_lock);
	head = &tt_global_entry->orig_list;
	hlist_for_each_entry_safe(orig_entry, node, safe, head, list) {
		hlist_del_rcu(node);
		batadv_tt_orig_list_entry_free_ref(orig_entry);
	}
	spin_unlock_bh(&tt_global_entry->list_lock);

}

static void
batadv_tt_global_del_orig_entry(struct batadv_priv *bat_priv,
				struct batadv_tt_global_entry *tt_global_entry,
				struct batadv_orig_node *orig_node,
				const char *message)
{
	struct hlist_head *head;
	struct hlist_node *node, *safe;
	struct batadv_tt_orig_list_entry *orig_entry;

	spin_lock_bh(&tt_global_entry->list_lock);
	head = &tt_global_entry->orig_list;
	hlist_for_each_entry_safe(orig_entry, node, safe, head, list) {
		if (orig_entry->orig_node == orig_node) {
			batadv_dbg(BATADV_DBG_TT, bat_priv,
				   "Deleting %pM from global tt entry %pM: %s\n",
				   orig_node->orig,
				   tt_global_entry->common.addr, message);
			hlist_del_rcu(node);
			batadv_tt_orig_list_entry_free_ref(orig_entry);
		}
	}
	spin_unlock_bh(&tt_global_entry->list_lock);
}

static void
batadv_tt_global_del_struct(struct batadv_priv *bat_priv,
			    struct batadv_tt_global_entry *tt_global_entry,
			    const char *message)
{
	batadv_dbg(BATADV_DBG_TT, bat_priv,
		   "Deleting global tt entry %pM: %s\n",
		   tt_global_entry->common.addr, message);

	batadv_hash_remove(bat_priv->tt_global_hash, batadv_compare_tt,
			   batadv_choose_orig, tt_global_entry->common.addr);
	batadv_tt_global_entry_free_ref(tt_global_entry);

}

/* If the client is to be deleted, we check if it is the last origantor entry
 * within tt_global entry. If yes, we set the BATADV_TT_CLIENT_ROAM flag and the
 * timer, otherwise we simply remove the originator scheduled for deletion.
 */
static void
batadv_tt_global_del_roaming(struct batadv_priv *bat_priv,
			     struct batadv_tt_global_entry *tt_global_entry,
			     struct batadv_orig_node *orig_node,
			     const char *message)
{
	bool last_entry = true;
	struct hlist_head *head;
	struct hlist_node *node;
	struct batadv_tt_orig_list_entry *orig_entry;

	/* no local entry exists, case 1:
	 * Check if this is the last one or if other entries exist.
	 */

	rcu_read_lock();
	head = &tt_global_entry->orig_list;
	hlist_for_each_entry_rcu(orig_entry, node, head, list) {
		if (orig_entry->orig_node != orig_node) {
			last_entry = false;
			break;
		}
	}
	rcu_read_unlock();

	if (last_entry) {
		/* its the last one, mark for roaming. */
		tt_global_entry->common.flags |= BATADV_TT_CLIENT_ROAM;
		tt_global_entry->roam_at = jiffies;
	} else
		/* there is another entry, we can simply delete this
		 * one and can still use the other one.
		 */
		batadv_tt_global_del_orig_entry(bat_priv, tt_global_entry,
						orig_node, message);
}



static void batadv_tt_global_del(struct batadv_priv *bat_priv,
				 struct batadv_orig_node *orig_node,
				 const unsigned char *addr,
				 const char *message, bool roaming)
{
	struct batadv_tt_global_entry *tt_global_entry = NULL;
	struct batadv_tt_local_entry *local_entry = NULL;

	tt_global_entry = batadv_tt_global_hash_find(bat_priv, addr);
	if (!tt_global_entry)
		goto out;

	if (!roaming) {
		batadv_tt_global_del_orig_entry(bat_priv, tt_global_entry,
						orig_node, message);

		if (hlist_empty(&tt_global_entry->orig_list))
			batadv_tt_global_del_struct(bat_priv, tt_global_entry,
						    message);

		goto out;
	}

	/* if we are deleting a global entry due to a roam
	 * event, there are two possibilities:
	 * 1) the client roamed from node A to node B => if there
	 *    is only one originator left for this client, we mark
	 *    it with BATADV_TT_CLIENT_ROAM, we start a timer and we
	 *    wait for node B to claim it. In case of timeout
	 *    the entry is purged.
	 *
	 *    If there are other originators left, we directly delete
	 *    the originator.
	 * 2) the client roamed to us => we can directly delete
	 *    the global entry, since it is useless now.
	 */
	local_entry = batadv_tt_local_hash_find(bat_priv,
						tt_global_entry->common.addr);
	if (local_entry) {
		/* local entry exists, case 2: client roamed to us. */
		batadv_tt_global_del_orig_list(tt_global_entry);
		batadv_tt_global_del_struct(bat_priv, tt_global_entry, message);
	} else
		/* no local entry exists, case 1: check for roaming */
		batadv_tt_global_del_roaming(bat_priv, tt_global_entry,
					     orig_node, message);


out:
	if (tt_global_entry)
		batadv_tt_global_entry_free_ref(tt_global_entry);
	if (local_entry)
		batadv_tt_local_entry_free_ref(local_entry);
}

void batadv_tt_global_del_orig(struct batadv_priv *bat_priv,
			       struct batadv_orig_node *orig_node,
			       const char *message)
{
	struct batadv_tt_global_entry *tt_global;
	struct batadv_tt_common_entry *tt_common_entry;
	uint32_t i;
	struct batadv_hashtable *hash = bat_priv->tt_global_hash;
	struct hlist_node *node, *safe;
	struct hlist_head *head;
	spinlock_t *list_lock; /* protects write access to the hash lists */

	if (!hash)
		return;

	for (i = 0; i < hash->size; i++) {
		head = &hash->table[i];
		list_lock = &hash->list_locks[i];

		spin_lock_bh(list_lock);
		hlist_for_each_entry_safe(tt_common_entry, node, safe,
					  head, hash_entry) {
			tt_global = container_of(tt_common_entry,
						 struct batadv_tt_global_entry,
						 common);

			batadv_tt_global_del_orig_entry(bat_priv, tt_global,
							orig_node, message);

			if (hlist_empty(&tt_global->orig_list)) {
				batadv_dbg(BATADV_DBG_TT, bat_priv,
					   "Deleting global tt entry %pM: %s\n",
					   tt_global->common.addr, message);
				hlist_del_rcu(node);
				batadv_tt_global_entry_free_ref(tt_global);
			}
		}
		spin_unlock_bh(list_lock);
	}
	orig_node->tt_initialised = false;
}

static void batadv_tt_global_roam_purge_list(struct batadv_priv *bat_priv,
					     struct hlist_head *head)
{
	struct batadv_tt_common_entry *tt_common_entry;
	struct batadv_tt_global_entry *tt_global_entry;
	struct hlist_node *node, *node_tmp;

	hlist_for_each_entry_safe(tt_common_entry, node, node_tmp, head,
				  hash_entry) {
		tt_global_entry = container_of(tt_common_entry,
					       struct batadv_tt_global_entry,
					       common);
		if (!(tt_global_entry->common.flags & BATADV_TT_CLIENT_ROAM))
			continue;
		if (!batadv_has_timed_out(tt_global_entry->roam_at,
					  BATADV_TT_CLIENT_ROAM_TIMEOUT))
			continue;

		batadv_dbg(BATADV_DBG_TT, bat_priv,
			   "Deleting global tt entry (%pM): Roaming timeout\n",
			   tt_global_entry->common.addr);

		hlist_del_rcu(node);
		batadv_tt_global_entry_free_ref(tt_global_entry);
	}
}

static void batadv_tt_global_roam_purge(struct batadv_priv *bat_priv)
{
	struct batadv_hashtable *hash = bat_priv->tt_global_hash;
	struct hlist_head *head;
	spinlock_t *list_lock; /* protects write access to the hash lists */
	uint32_t i;

	for (i = 0; i < hash->size; i++) {
		head = &hash->table[i];
		list_lock = &hash->list_locks[i];

		spin_lock_bh(list_lock);
		batadv_tt_global_roam_purge_list(bat_priv, head);
		spin_unlock_bh(list_lock);
	}

}

static void batadv_tt_global_table_free(struct batadv_priv *bat_priv)
{
	struct batadv_hashtable *hash;
	spinlock_t *list_lock; /* protects write access to the hash lists */
	struct batadv_tt_common_entry *tt_common_entry;
	struct batadv_tt_global_entry *tt_global;
	struct hlist_node *node, *node_tmp;
	struct hlist_head *head;
	uint32_t i;

	if (!bat_priv->tt_global_hash)
		return;

	hash = bat_priv->tt_global_hash;

	for (i = 0; i < hash->size; i++) {
		head = &hash->table[i];
		list_lock = &hash->list_locks[i];

		spin_lock_bh(list_lock);
		hlist_for_each_entry_safe(tt_common_entry, node, node_tmp,
					  head, hash_entry) {
			hlist_del_rcu(node);
			tt_global = container_of(tt_common_entry,
						 struct batadv_tt_global_entry,
						 common);
			batadv_tt_global_entry_free_ref(tt_global);
		}
		spin_unlock_bh(list_lock);
	}

	batadv_hash_destroy(hash);

	bat_priv->tt_global_hash = NULL;
}

static bool
_batadv_is_ap_isolated(struct batadv_tt_local_entry *tt_local_entry,
		       struct batadv_tt_global_entry *tt_global_entry)
{
	bool ret = false;

	if (tt_local_entry->common.flags & BATADV_TT_CLIENT_WIFI &&
	    tt_global_entry->common.flags & BATADV_TT_CLIENT_WIFI)
		ret = true;

	return ret;
}

struct batadv_orig_node *batadv_transtable_search(struct batadv_priv *bat_priv,
						  const uint8_t *src,
						  const uint8_t *addr)
{
	struct batadv_tt_local_entry *tt_local_entry = NULL;
	struct batadv_tt_global_entry *tt_global_entry = NULL;
	struct batadv_orig_node *orig_node = NULL;
	struct batadv_neigh_node *router = NULL;
	struct hlist_head *head;
	struct hlist_node *node;
	struct batadv_tt_orig_list_entry *orig_entry;
	int best_tq;

	if (src && atomic_read(&bat_priv->ap_isolation)) {
		tt_local_entry = batadv_tt_local_hash_find(bat_priv, src);
		if (!tt_local_entry)
			goto out;
	}

	tt_global_entry = batadv_tt_global_hash_find(bat_priv, addr);
	if (!tt_global_entry)
		goto out;

	/* check whether the clients should not communicate due to AP
	 * isolation
	 */
	if (tt_local_entry &&
	    _batadv_is_ap_isolated(tt_local_entry, tt_global_entry))
		goto out;

	best_tq = 0;

	rcu_read_lock();
	head = &tt_global_entry->orig_list;
	hlist_for_each_entry_rcu(orig_entry, node, head, list) {
		router = batadv_orig_node_get_router(orig_entry->orig_node);
		if (!router)
			continue;

		if (router->tq_avg > best_tq) {
			orig_node = orig_entry->orig_node;
			best_tq = router->tq_avg;
		}
		batadv_neigh_node_free_ref(router);
	}
	/* found anything? */
	if (orig_node && !atomic_inc_not_zero(&orig_node->refcount))
		orig_node = NULL;
	rcu_read_unlock();
out:
	if (tt_global_entry)
		batadv_tt_global_entry_free_ref(tt_global_entry);
	if (tt_local_entry)
		batadv_tt_local_entry_free_ref(tt_local_entry);

	return orig_node;
}

/* Calculates the checksum of the local table of a given orig_node */
static uint16_t batadv_tt_global_crc(struct batadv_priv *bat_priv,
				     struct batadv_orig_node *orig_node)
{
	uint16_t total = 0, total_one;
	struct batadv_hashtable *hash = bat_priv->tt_global_hash;
	struct batadv_tt_common_entry *tt_common;
	struct batadv_tt_global_entry *tt_global;
	struct hlist_node *node;
	struct hlist_head *head;
	uint32_t i;
	int j;

	for (i = 0; i < hash->size; i++) {
		head = &hash->table[i];

		rcu_read_lock();
		hlist_for_each_entry_rcu(tt_common, node, head, hash_entry) {
			tt_global = container_of(tt_common,
						 struct batadv_tt_global_entry,
						 common);
			/* Roaming clients are in the global table for
			 * consistency only. They don't have to be
			 * taken into account while computing the
			 * global crc
			 */
			if (tt_common->flags & BATADV_TT_CLIENT_ROAM)
				continue;

			/* find out if this global entry is announced by this
			 * originator
			 */
			if (!batadv_tt_global_entry_has_orig(tt_global,
							     orig_node))
				continue;

			total_one = 0;
			for (j = 0; j < ETH_ALEN; j++)
				total_one = crc16_byte(total_one,
						       tt_common->addr[j]);
			total ^= total_one;
		}
		rcu_read_unlock();
	}

	return total;
}

/* Calculates the checksum of the local table */
static uint16_t batadv_tt_local_crc(struct batadv_priv *bat_priv)
{
	uint16_t total = 0, total_one;
	struct batadv_hashtable *hash = bat_priv->tt_local_hash;
	struct batadv_tt_common_entry *tt_common;
	struct hlist_node *node;
	struct hlist_head *head;
	uint32_t i;
	int j;

	for (i = 0; i < hash->size; i++) {
		head = &hash->table[i];

		rcu_read_lock();
		hlist_for_each_entry_rcu(tt_common, node, head, hash_entry) {
			/* not yet committed clients have not to be taken into
			 * account while computing the CRC
			 */
			if (tt_common->flags & BATADV_TT_CLIENT_NEW)
				continue;
			total_one = 0;
			for (j = 0; j < ETH_ALEN; j++)
				total_one = crc16_byte(total_one,
						       tt_common->addr[j]);
			total ^= total_one;
		}
		rcu_read_unlock();
	}

	return total;
}

static void batadv_tt_req_list_free(struct batadv_priv *bat_priv)
{
	struct batadv_tt_req_node *node, *safe;

	spin_lock_bh(&bat_priv->tt_req_list_lock);

	list_for_each_entry_safe(node, safe, &bat_priv->tt_req_list, list) {
		list_del(&node->list);
		kfree(node);
	}

	spin_unlock_bh(&bat_priv->tt_req_list_lock);
}

static void batadv_tt_save_orig_buffer(struct batadv_priv *bat_priv,
				       struct batadv_orig_node *orig_node,
				       const unsigned char *tt_buff,
				       uint8_t tt_num_changes)
{
	uint16_t tt_buff_len = batadv_tt_len(tt_num_changes);

	/* Replace the old buffer only if I received something in the
	 * last OGM (the OGM could carry no changes)
	 */
	spin_lock_bh(&orig_node->tt_buff_lock);
	if (tt_buff_len > 0) {
		kfree(orig_node->tt_buff);
		orig_node->tt_buff_len = 0;
		orig_node->tt_buff = kmalloc(tt_buff_len, GFP_ATOMIC);
		if (orig_node->tt_buff) {
			memcpy(orig_node->tt_buff, tt_buff, tt_buff_len);
			orig_node->tt_buff_len = tt_buff_len;
		}
	}
	spin_unlock_bh(&orig_node->tt_buff_lock);
}

static void batadv_tt_req_purge(struct batadv_priv *bat_priv)
{
	struct batadv_tt_req_node *node, *safe;

	spin_lock_bh(&bat_priv->tt_req_list_lock);
	list_for_each_entry_safe(node, safe, &bat_priv->tt_req_list, list) {
		if (batadv_has_timed_out(node->issued_at,
					 BATADV_TT_REQUEST_TIMEOUT)) {
			list_del(&node->list);
			kfree(node);
		}
	}
	spin_unlock_bh(&bat_priv->tt_req_list_lock);
}

/* returns the pointer to the new tt_req_node struct if no request
 * has already been issued for this orig_node, NULL otherwise
 */
static struct batadv_tt_req_node *
batadv_new_tt_req_node(struct batadv_priv *bat_priv,
		       struct batadv_orig_node *orig_node)
{
	struct batadv_tt_req_node *tt_req_node_tmp, *tt_req_node = NULL;

	spin_lock_bh(&bat_priv->tt_req_list_lock);
	list_for_each_entry(tt_req_node_tmp, &bat_priv->tt_req_list, list) {
		if (batadv_compare_eth(tt_req_node_tmp, orig_node) &&
		    !batadv_has_timed_out(tt_req_node_tmp->issued_at,
					  BATADV_TT_REQUEST_TIMEOUT))
			goto unlock;
	}

	tt_req_node = kmalloc(sizeof(*tt_req_node), GFP_ATOMIC);
	if (!tt_req_node)
		goto unlock;

	memcpy(tt_req_node->addr, orig_node->orig, ETH_ALEN);
	tt_req_node->issued_at = jiffies;

	list_add(&tt_req_node->list, &bat_priv->tt_req_list);
unlock:
	spin_unlock_bh(&bat_priv->tt_req_list_lock);
	return tt_req_node;
}

/* data_ptr is useless here, but has to be kept to respect the prototype */
static int batadv_tt_local_valid_entry(const void *entry_ptr,
				       const void *data_ptr)
{
	const struct batadv_tt_common_entry *tt_common_entry = entry_ptr;

	if (tt_common_entry->flags & BATADV_TT_CLIENT_NEW)
		return 0;
	return 1;
}

static int batadv_tt_global_valid(const void *entry_ptr,
				  const void *data_ptr)
{
	const struct batadv_tt_common_entry *tt_common_entry = entry_ptr;
	const struct batadv_tt_global_entry *tt_global_entry;
	const struct batadv_orig_node *orig_node = data_ptr;

	if (tt_common_entry->flags & BATADV_TT_CLIENT_ROAM)
		return 0;

	tt_global_entry = container_of(tt_common_entry,
				       struct batadv_tt_global_entry,
				       common);

	return batadv_tt_global_entry_has_orig(tt_global_entry, orig_node);
}

static struct sk_buff *
batadv_tt_response_fill_table(uint16_t tt_len, uint8_t ttvn,
			      struct batadv_hashtable *hash,
			      struct batadv_hard_iface *primary_if,
			      int (*valid_cb)(const void *, const void *),
			      void *cb_data)
{
	struct batadv_tt_common_entry *tt_common_entry;
	struct batadv_tt_query_packet *tt_response;
	struct batadv_tt_change *tt_change;
	struct hlist_node *node;
	struct hlist_head *head;
	struct sk_buff *skb = NULL;
	uint16_t tt_tot, tt_count;
	ssize_t tt_query_size = sizeof(struct batadv_tt_query_packet);
	uint32_t i;
	size_t len;

	if (tt_query_size + tt_len > primary_if->soft_iface->mtu) {
		tt_len = primary_if->soft_iface->mtu - tt_query_size;
		tt_len -= tt_len % sizeof(struct batadv_tt_change);
	}
	tt_tot = tt_len / sizeof(struct batadv_tt_change);

	len = tt_query_size + tt_len;
	skb = dev_alloc_skb(len + ETH_HLEN);
	if (!skb)
		goto out;

	skb_reserve(skb, ETH_HLEN);
	tt_response = (struct batadv_tt_query_packet *)skb_put(skb, len);
	tt_response->ttvn = ttvn;

	tt_change = (struct batadv_tt_change *)(skb->data + tt_query_size);
	tt_count = 0;

	rcu_read_lock();
	for (i = 0; i < hash->size; i++) {
		head = &hash->table[i];

		hlist_for_each_entry_rcu(tt_common_entry, node,
					 head, hash_entry) {
			if (tt_count == tt_tot)
				break;

			if ((valid_cb) && (!valid_cb(tt_common_entry, cb_data)))
				continue;

			memcpy(tt_change->addr, tt_common_entry->addr,
			       ETH_ALEN);
			tt_change->flags = BATADV_NO_FLAGS;

			tt_count++;
			tt_change++;
		}
	}
	rcu_read_unlock();

	/* store in the message the number of entries we have successfully
	 * copied
	 */
	tt_response->tt_data = htons(tt_count);

out:
	return skb;
}

static int batadv_send_tt_request(struct batadv_priv *bat_priv,
				  struct batadv_orig_node *dst_orig_node,
				  uint8_t ttvn, uint16_t tt_crc,
				  bool full_table)
{
	struct sk_buff *skb = NULL;
	struct batadv_tt_query_packet *tt_request;
	struct batadv_neigh_node *neigh_node = NULL;
	struct batadv_hard_iface *primary_if;
	struct batadv_tt_req_node *tt_req_node = NULL;
	int ret = 1;
	size_t tt_req_len;

	primary_if = batadv_primary_if_get_selected(bat_priv);
	if (!primary_if)
		goto out;

	/* The new tt_req will be issued only if I'm not waiting for a
	 * reply from the same orig_node yet
	 */
	tt_req_node = batadv_new_tt_req_node(bat_priv, dst_orig_node);
	if (!tt_req_node)
		goto out;

	skb = dev_alloc_skb(sizeof(*tt_request) + ETH_HLEN);
	if (!skb)
		goto out;

	skb_reserve(skb, ETH_HLEN);

	tt_req_len = sizeof(*tt_request);
	tt_request = (struct batadv_tt_query_packet *)skb_put(skb, tt_req_len);

	tt_request->header.packet_type = BATADV_TT_QUERY;
	tt_request->header.version = BATADV_COMPAT_VERSION;
	memcpy(tt_request->src, primary_if->net_dev->dev_addr, ETH_ALEN);
	memcpy(tt_request->dst, dst_orig_node->orig, ETH_ALEN);
	tt_request->header.ttl = BATADV_TTL;
	tt_request->ttvn = ttvn;
	tt_request->tt_data = htons(tt_crc);
	tt_request->flags = BATADV_TT_REQUEST;

	if (full_table)
		tt_request->flags |= BATADV_TT_FULL_TABLE;

	neigh_node = batadv_orig_node_get_router(dst_orig_node);
	if (!neigh_node)
		goto out;

	batadv_dbg(BATADV_DBG_TT, bat_priv,
		   "Sending TT_REQUEST to %pM via %pM [%c]\n",
		   dst_orig_node->orig, neigh_node->addr,
		   (full_table ? 'F' : '.'));

	batadv_inc_counter(bat_priv, BATADV_CNT_TT_REQUEST_TX);

	batadv_send_skb_packet(skb, neigh_node->if_incoming, neigh_node->addr);
	ret = 0;

out:
	if (neigh_node)
		batadv_neigh_node_free_ref(neigh_node);
	if (primary_if)
		batadv_hardif_free_ref(primary_if);
	if (ret)
		kfree_skb(skb);
	if (ret && tt_req_node) {
		spin_lock_bh(&bat_priv->tt_req_list_lock);
		list_del(&tt_req_node->list);
		spin_unlock_bh(&bat_priv->tt_req_list_lock);
		kfree(tt_req_node);
	}
	return ret;
}

static bool
batadv_send_other_tt_response(struct batadv_priv *bat_priv,
			      struct batadv_tt_query_packet *tt_request)
{
	struct batadv_orig_node *req_dst_orig_node = NULL;
	struct batadv_orig_node *res_dst_orig_node = NULL;
	struct batadv_neigh_node *neigh_node = NULL;
	struct batadv_hard_iface *primary_if = NULL;
	uint8_t orig_ttvn, req_ttvn, ttvn;
	int ret = false;
	unsigned char *tt_buff;
	bool full_table;
	uint16_t tt_len, tt_tot;
	struct sk_buff *skb = NULL;
	struct batadv_tt_query_packet *tt_response;
	size_t len;

	batadv_dbg(BATADV_DBG_TT, bat_priv,
		   "Received TT_REQUEST from %pM for ttvn: %u (%pM) [%c]\n",
		   tt_request->src, tt_request->ttvn, tt_request->dst,
		   (tt_request->flags & BATADV_TT_FULL_TABLE ? 'F' : '.'));

	/* Let's get the orig node of the REAL destination */
	req_dst_orig_node = batadv_orig_hash_find(bat_priv, tt_request->dst);
	if (!req_dst_orig_node)
		goto out;

	res_dst_orig_node = batadv_orig_hash_find(bat_priv, tt_request->src);
	if (!res_dst_orig_node)
		goto out;

	neigh_node = batadv_orig_node_get_router(res_dst_orig_node);
	if (!neigh_node)
		goto out;

	primary_if = batadv_primary_if_get_selected(bat_priv);
	if (!primary_if)
		goto out;

	orig_ttvn = (uint8_t)atomic_read(&req_dst_orig_node->last_ttvn);
	req_ttvn = tt_request->ttvn;

	/* I don't have the requested data */
	if (orig_ttvn != req_ttvn ||
	    tt_request->tt_data != htons(req_dst_orig_node->tt_crc))
		goto out;

	/* If the full table has been explicitly requested */
	if (tt_request->flags & BATADV_TT_FULL_TABLE ||
	    !req_dst_orig_node->tt_buff)
		full_table = true;
	else
		full_table = false;

	/* In this version, fragmentation is not implemented, then
	 * I'll send only one packet with as much TT entries as I can
	 */
	if (!full_table) {
		spin_lock_bh(&req_dst_orig_node->tt_buff_lock);
		tt_len = req_dst_orig_node->tt_buff_len;
		tt_tot = tt_len / sizeof(struct batadv_tt_change);

		len = sizeof(*tt_response) + tt_len;
		skb = dev_alloc_skb(len + ETH_HLEN);
		if (!skb)
			goto unlock;

		skb_reserve(skb, ETH_HLEN);
		tt_response = (struct batadv_tt_query_packet *)skb_put(skb,
								       len);
		tt_response->ttvn = req_ttvn;
		tt_response->tt_data = htons(tt_tot);

		tt_buff = skb->data + sizeof(*tt_response);
		/* Copy the last orig_node's OGM buffer */
		memcpy(tt_buff, req_dst_orig_node->tt_buff,
		       req_dst_orig_node->tt_buff_len);

		spin_unlock_bh(&req_dst_orig_node->tt_buff_lock);
	} else {
		tt_len = (uint16_t)atomic_read(&req_dst_orig_node->tt_size);
		tt_len *= sizeof(struct batadv_tt_change);
		ttvn = (uint8_t)atomic_read(&req_dst_orig_node->last_ttvn);

		skb = batadv_tt_response_fill_table(tt_len, ttvn,
						    bat_priv->tt_global_hash,
						    primary_if,
						    batadv_tt_global_valid,
						    req_dst_orig_node);
		if (!skb)
			goto out;

		tt_response = (struct batadv_tt_query_packet *)skb->data;
	}

	tt_response->header.packet_type = BATADV_TT_QUERY;
	tt_response->header.version = BATADV_COMPAT_VERSION;
	tt_response->header.ttl = BATADV_TTL;
	memcpy(tt_response->src, req_dst_orig_node->orig, ETH_ALEN);
	memcpy(tt_response->dst, tt_request->src, ETH_ALEN);
	tt_response->flags = BATADV_TT_RESPONSE;

	if (full_table)
		tt_response->flags |= BATADV_TT_FULL_TABLE;

	batadv_dbg(BATADV_DBG_TT, bat_priv,
		   "Sending TT_RESPONSE %pM via %pM for %pM (ttvn: %u)\n",
		   res_dst_orig_node->orig, neigh_node->addr,
		   req_dst_orig_node->orig, req_ttvn);

	batadv_inc_counter(bat_priv, BATADV_CNT_TT_RESPONSE_TX);

	batadv_send_skb_packet(skb, neigh_node->if_incoming, neigh_node->addr);
	ret = true;
	goto out;

unlock:
	spin_unlock_bh(&req_dst_orig_node->tt_buff_lock);

out:
	if (res_dst_orig_node)
		batadv_orig_node_free_ref(res_dst_orig_node);
	if (req_dst_orig_node)
		batadv_orig_node_free_ref(req_dst_orig_node);
	if (neigh_node)
		batadv_neigh_node_free_ref(neigh_node);
	if (primary_if)
		batadv_hardif_free_ref(primary_if);
	if (!ret)
		kfree_skb(skb);
	return ret;

}

static bool
batadv_send_my_tt_response(struct batadv_priv *bat_priv,
			   struct batadv_tt_query_packet *tt_request)
{
	struct batadv_orig_node *orig_node = NULL;
	struct batadv_neigh_node *neigh_node = NULL;
	struct batadv_hard_iface *primary_if = NULL;
	uint8_t my_ttvn, req_ttvn, ttvn;
	int ret = false;
	unsigned char *tt_buff;
	bool full_table;
	uint16_t tt_len, tt_tot;
	struct sk_buff *skb = NULL;
	struct batadv_tt_query_packet *tt_response;
	size_t len;

	batadv_dbg(BATADV_DBG_TT, bat_priv,
		   "Received TT_REQUEST from %pM for ttvn: %u (me) [%c]\n",
		   tt_request->src, tt_request->ttvn,
		   (tt_request->flags & BATADV_TT_FULL_TABLE ? 'F' : '.'));


	my_ttvn = (uint8_t)atomic_read(&bat_priv->ttvn);
	req_ttvn = tt_request->ttvn;

	orig_node = batadv_orig_hash_find(bat_priv, tt_request->src);
	if (!orig_node)
		goto out;

	neigh_node = batadv_orig_node_get_router(orig_node);
	if (!neigh_node)
		goto out;

	primary_if = batadv_primary_if_get_selected(bat_priv);
	if (!primary_if)
		goto out;

	/* If the full table has been explicitly requested or the gap
	 * is too big send the whole local translation table
	 */
	if (tt_request->flags & BATADV_TT_FULL_TABLE || my_ttvn != req_ttvn ||
	    !bat_priv->tt_buff)
		full_table = true;
	else
		full_table = false;

	/* In this version, fragmentation is not implemented, then
	 * I'll send only one packet with as much TT entries as I can
	 */
	if (!full_table) {
		spin_lock_bh(&bat_priv->tt_buff_lock);
		tt_len = bat_priv->tt_buff_len;
		tt_tot = tt_len / sizeof(struct batadv_tt_change);

		len = sizeof(*tt_response) + tt_len;
		skb = dev_alloc_skb(len + ETH_HLEN);
		if (!skb)
			goto unlock;

		skb_reserve(skb, ETH_HLEN);
		tt_response = (struct batadv_tt_query_packet *)skb_put(skb,
								       len);
		tt_response->ttvn = req_ttvn;
		tt_response->tt_data = htons(tt_tot);

		tt_buff = skb->data + sizeof(*tt_response);
		memcpy(tt_buff, bat_priv->tt_buff,
		       bat_priv->tt_buff_len);
		spin_unlock_bh(&bat_priv->tt_buff_lock);
	} else {
		tt_len = (uint16_t)atomic_read(&bat_priv->num_local_tt);
		tt_len *= sizeof(struct batadv_tt_change);
		ttvn = (uint8_t)atomic_read(&bat_priv->ttvn);

		skb = batadv_tt_response_fill_table(tt_len, ttvn,
						    bat_priv->tt_local_hash,
						    primary_if,
						    batadv_tt_local_valid_entry,
						    NULL);
		if (!skb)
			goto out;

		tt_response = (struct batadv_tt_query_packet *)skb->data;
	}

	tt_response->header.packet_type = BATADV_TT_QUERY;
	tt_response->header.version = BATADV_COMPAT_VERSION;
	tt_response->header.ttl = BATADV_TTL;
	memcpy(tt_response->src, primary_if->net_dev->dev_addr, ETH_ALEN);
	memcpy(tt_response->dst, tt_request->src, ETH_ALEN);
	tt_response->flags = BATADV_TT_RESPONSE;

	if (full_table)
		tt_response->flags |= BATADV_TT_FULL_TABLE;

	batadv_dbg(BATADV_DBG_TT, bat_priv,
		   "Sending TT_RESPONSE to %pM via %pM [%c]\n",
		   orig_node->orig, neigh_node->addr,
		   (tt_response->flags & BATADV_TT_FULL_TABLE ? 'F' : '.'));

	batadv_inc_counter(bat_priv, BATADV_CNT_TT_RESPONSE_TX);

	batadv_send_skb_packet(skb, neigh_node->if_incoming, neigh_node->addr);
	ret = true;
	goto out;

unlock:
	spin_unlock_bh(&bat_priv->tt_buff_lock);
out:
	if (orig_node)
		batadv_orig_node_free_ref(orig_node);
	if (neigh_node)
		batadv_neigh_node_free_ref(neigh_node);
	if (primary_if)
		batadv_hardif_free_ref(primary_if);
	if (!ret)
		kfree_skb(skb);
	/* This packet was for me, so it doesn't need to be re-routed */
	return true;
}

bool batadv_send_tt_response(struct batadv_priv *bat_priv,
			     struct batadv_tt_query_packet *tt_request)
{
	if (batadv_is_my_mac(tt_request->dst)) {
		/* don't answer backbone gws! */
		if (batadv_bla_is_backbone_gw_orig(bat_priv, tt_request->src))
			return true;

		return batadv_send_my_tt_response(bat_priv, tt_request);
	} else {
		return batadv_send_other_tt_response(bat_priv, tt_request);
	}
}

static void _batadv_tt_update_changes(struct batadv_priv *bat_priv,
				      struct batadv_orig_node *orig_node,
				      struct batadv_tt_change *tt_change,
				      uint16_t tt_num_changes, uint8_t ttvn)
{
	int i;
	int roams;

	for (i = 0; i < tt_num_changes; i++) {
		if ((tt_change + i)->flags & BATADV_TT_CLIENT_DEL) {
			roams = (tt_change + i)->flags & BATADV_TT_CLIENT_ROAM;
			batadv_tt_global_del(bat_priv, orig_node,
					     (tt_change + i)->addr,
					     "tt removed by changes",
					     roams);
		} else {
			if (!batadv_tt_global_add(bat_priv, orig_node,
						  (tt_change + i)->addr,
						  (tt_change + i)->flags, ttvn))
				/* In case of problem while storing a
				 * global_entry, we stop the updating
				 * procedure without committing the
				 * ttvn change. This will avoid to send
				 * corrupted data on tt_request
				 */
				return;
		}
	}
	orig_node->tt_initialised = true;
}

static void batadv_tt_fill_gtable(struct batadv_priv *bat_priv,
				  struct batadv_tt_query_packet *tt_response)
{
	struct batadv_orig_node *orig_node = NULL;

	orig_node = batadv_orig_hash_find(bat_priv, tt_response->src);
	if (!orig_node)
		goto out;

	/* Purge the old table first.. */
	batadv_tt_global_del_orig(bat_priv, orig_node, "Received full table");

	_batadv_tt_update_changes(bat_priv, orig_node,
				  (struct batadv_tt_change *)(tt_response + 1),
				  ntohs(tt_response->tt_data),
				  tt_response->ttvn);

	spin_lock_bh(&orig_node->tt_buff_lock);
	kfree(orig_node->tt_buff);
	orig_node->tt_buff_len = 0;
	orig_node->tt_buff = NULL;
	spin_unlock_bh(&orig_node->tt_buff_lock);

	atomic_set(&orig_node->last_ttvn, tt_response->ttvn);

out:
	if (orig_node)
		batadv_orig_node_free_ref(orig_node);
}

static void batadv_tt_update_changes(struct batadv_priv *bat_priv,
				     struct batadv_orig_node *orig_node,
				     uint16_t tt_num_changes, uint8_t ttvn,
				     struct batadv_tt_change *tt_change)
{
	_batadv_tt_update_changes(bat_priv, orig_node, tt_change,
				  tt_num_changes, ttvn);

	batadv_tt_save_orig_buffer(bat_priv, orig_node,
				   (unsigned char *)tt_change, tt_num_changes);
	atomic_set(&orig_node->last_ttvn, ttvn);
}

bool batadv_is_my_client(struct batadv_priv *bat_priv, const uint8_t *addr)
{
	struct batadv_tt_local_entry *tt_local_entry = NULL;
	bool ret = false;

	tt_local_entry = batadv_tt_local_hash_find(bat_priv, addr);
	if (!tt_local_entry)
		goto out;
	/* Check if the client has been logically deleted (but is kept for
	 * consistency purpose)
	 */
	if (tt_local_entry->common.flags & BATADV_TT_CLIENT_PENDING)
		goto out;
	ret = true;
out:
	if (tt_local_entry)
		batadv_tt_local_entry_free_ref(tt_local_entry);
	return ret;
}

void batadv_handle_tt_response(struct batadv_priv *bat_priv,
			       struct batadv_tt_query_packet *tt_response)
{
	struct batadv_tt_req_node *node, *safe;
	struct batadv_orig_node *orig_node = NULL;
	struct batadv_tt_change *tt_change;

	batadv_dbg(BATADV_DBG_TT, bat_priv,
		   "Received TT_RESPONSE from %pM for ttvn %d t_size: %d [%c]\n",
		   tt_response->src, tt_response->ttvn,
		   ntohs(tt_response->tt_data),
		   (tt_response->flags & BATADV_TT_FULL_TABLE ? 'F' : '.'));

	/* we should have never asked a backbone gw */
	if (batadv_bla_is_backbone_gw_orig(bat_priv, tt_response->src))
		goto out;

	orig_node = batadv_orig_hash_find(bat_priv, tt_response->src);
	if (!orig_node)
		goto out;

	if (tt_response->flags & BATADV_TT_FULL_TABLE) {
		batadv_tt_fill_gtable(bat_priv, tt_response);
	} else {
		tt_change = (struct batadv_tt_change *)(tt_response + 1);
		batadv_tt_update_changes(bat_priv, orig_node,
					 ntohs(tt_response->tt_data),
					 tt_response->ttvn, tt_change);
	}

	/* Delete the tt_req_node from pending tt_requests list */
	spin_lock_bh(&bat_priv->tt_req_list_lock);
	list_for_each_entry_safe(node, safe, &bat_priv->tt_req_list, list) {
		if (!batadv_compare_eth(node->addr, tt_response->src))
			continue;
		list_del(&node->list);
		kfree(node);
	}
	spin_unlock_bh(&bat_priv->tt_req_list_lock);

	/* Recalculate the CRC for this orig_node and store it */
	orig_node->tt_crc = batadv_tt_global_crc(bat_priv, orig_node);
	/* Roaming phase is over: tables are in sync again. I can
	 * unset the flag
	 */
	orig_node->tt_poss_change = false;
out:
	if (orig_node)
		batadv_orig_node_free_ref(orig_node);
}

int batadv_tt_init(struct batadv_priv *bat_priv)
{
	int ret;

	ret = batadv_tt_local_init(bat_priv);
	if (ret < 0)
		return ret;

	ret = batadv_tt_global_init(bat_priv);
	if (ret < 0)
		return ret;

	batadv_tt_start_timer(bat_priv);

	return 1;
}

static void batadv_tt_roam_list_free(struct batadv_priv *bat_priv)
{
	struct batadv_tt_roam_node *node, *safe;

	spin_lock_bh(&bat_priv->tt_roam_list_lock);

	list_for_each_entry_safe(node, safe, &bat_priv->tt_roam_list, list) {
		list_del(&node->list);
		kfree(node);
	}

	spin_unlock_bh(&bat_priv->tt_roam_list_lock);
}

static void batadv_tt_roam_purge(struct batadv_priv *bat_priv)
{
	struct batadv_tt_roam_node *node, *safe;

	spin_lock_bh(&bat_priv->tt_roam_list_lock);
	list_for_each_entry_safe(node, safe, &bat_priv->tt_roam_list, list) {
		if (!batadv_has_timed_out(node->first_time,
					  BATADV_ROAMING_MAX_TIME))
			continue;

		list_del(&node->list);
		kfree(node);
	}
	spin_unlock_bh(&bat_priv->tt_roam_list_lock);
}

/* This function checks whether the client already reached the
 * maximum number of possible roaming phases. In this case the ROAMING_ADV
 * will not be sent.
 *
 * returns true if the ROAMING_ADV can be sent, false otherwise
 */
static bool batadv_tt_check_roam_count(struct batadv_priv *bat_priv,
				       uint8_t *client)
{
	struct batadv_tt_roam_node *tt_roam_node;
	bool ret = false;

	spin_lock_bh(&bat_priv->tt_roam_list_lock);
	/* The new tt_req will be issued only if I'm not waiting for a
	 * reply from the same orig_node yet
	 */
	list_for_each_entry(tt_roam_node, &bat_priv->tt_roam_list, list) {
		if (!batadv_compare_eth(tt_roam_node->addr, client))
			continue;

		if (batadv_has_timed_out(tt_roam_node->first_time,
					 BATADV_ROAMING_MAX_TIME))
			continue;

		if (!batadv_atomic_dec_not_zero(&tt_roam_node->counter))
			/* Sorry, you roamed too many times! */
			goto unlock;
		ret = true;
		break;
	}

	if (!ret) {
		tt_roam_node = kmalloc(sizeof(*tt_roam_node), GFP_ATOMIC);
		if (!tt_roam_node)
			goto unlock;

		tt_roam_node->first_time = jiffies;
		atomic_set(&tt_roam_node->counter,
			   BATADV_ROAMING_MAX_COUNT - 1);
		memcpy(tt_roam_node->addr, client, ETH_ALEN);

		list_add(&tt_roam_node->list, &bat_priv->tt_roam_list);
		ret = true;
	}

unlock:
	spin_unlock_bh(&bat_priv->tt_roam_list_lock);
	return ret;
}

static void batadv_send_roam_adv(struct batadv_priv *bat_priv, uint8_t *client,
				 struct batadv_orig_node *orig_node)
{
	struct batadv_neigh_node *neigh_node = NULL;
	struct sk_buff *skb = NULL;
	struct batadv_roam_adv_packet *roam_adv_packet;
	int ret = 1;
	struct batadv_hard_iface *primary_if;
	size_t len = sizeof(*roam_adv_packet);

	/* before going on we have to check whether the client has
	 * already roamed to us too many times
	 */
	if (!batadv_tt_check_roam_count(bat_priv, client))
		goto out;

	skb = dev_alloc_skb(sizeof(*roam_adv_packet) + ETH_HLEN);
	if (!skb)
		goto out;

	skb_reserve(skb, ETH_HLEN);

	roam_adv_packet = (struct batadv_roam_adv_packet *)skb_put(skb, len);

	roam_adv_packet->header.packet_type = BATADV_ROAM_ADV;
	roam_adv_packet->header.version = BATADV_COMPAT_VERSION;
	roam_adv_packet->header.ttl = BATADV_TTL;
	primary_if = batadv_primary_if_get_selected(bat_priv);
	if (!primary_if)
		goto out;
	memcpy(roam_adv_packet->src, primary_if->net_dev->dev_addr, ETH_ALEN);
	batadv_hardif_free_ref(primary_if);
	memcpy(roam_adv_packet->dst, orig_node->orig, ETH_ALEN);
	memcpy(roam_adv_packet->client, client, ETH_ALEN);

	neigh_node = batadv_orig_node_get_router(orig_node);
	if (!neigh_node)
		goto out;

	batadv_dbg(BATADV_DBG_TT, bat_priv,
		   "Sending ROAMING_ADV to %pM (client %pM) via %pM\n",
		   orig_node->orig, client, neigh_node->addr);

	batadv_inc_counter(bat_priv, BATADV_CNT_TT_ROAM_ADV_TX);

	batadv_send_skb_packet(skb, neigh_node->if_incoming, neigh_node->addr);
	ret = 0;

out:
	if (neigh_node)
		batadv_neigh_node_free_ref(neigh_node);
	if (ret)
		kfree_skb(skb);
	return;
}

static void batadv_tt_purge(struct work_struct *work)
{
	struct delayed_work *delayed_work;
	struct batadv_priv *bat_priv;

	delayed_work = container_of(work, struct delayed_work, work);
	bat_priv = container_of(delayed_work, struct batadv_priv, tt_work);

	batadv_tt_local_purge(bat_priv);
	batadv_tt_global_roam_purge(bat_priv);
	batadv_tt_req_purge(bat_priv);
	batadv_tt_roam_purge(bat_priv);

	batadv_tt_start_timer(bat_priv);
}

void batadv_tt_free(struct batadv_priv *bat_priv)
{
	cancel_delayed_work_sync(&bat_priv->tt_work);

	batadv_tt_local_table_free(bat_priv);
	batadv_tt_global_table_free(bat_priv);
	batadv_tt_req_list_free(bat_priv);
	batadv_tt_changes_list_free(bat_priv);
	batadv_tt_roam_list_free(bat_priv);

	kfree(bat_priv->tt_buff);
}

/* This function will enable or disable the specified flags for all the entries
 * in the given hash table and returns the number of modified entries
 */
static uint16_t batadv_tt_set_flags(struct batadv_hashtable *hash,
				    uint16_t flags, bool enable)
{
	uint32_t i;
	uint16_t changed_num = 0;
	struct hlist_head *head;
	struct hlist_node *node;
	struct batadv_tt_common_entry *tt_common_entry;

	if (!hash)
		goto out;

	for (i = 0; i < hash->size; i++) {
		head = &hash->table[i];

		rcu_read_lock();
		hlist_for_each_entry_rcu(tt_common_entry, node,
					 head, hash_entry) {
			if (enable) {
				if ((tt_common_entry->flags & flags) == flags)
					continue;
				tt_common_entry->flags |= flags;
			} else {
				if (!(tt_common_entry->flags & flags))
					continue;
				tt_common_entry->flags &= ~flags;
			}
			changed_num++;
		}
		rcu_read_unlock();
	}
out:
	return changed_num;
}

/* Purge out all the tt local entries marked with BATADV_TT_CLIENT_PENDING */
static void batadv_tt_local_purge_pending_clients(struct batadv_priv *bat_priv)
{
	struct batadv_hashtable *hash = bat_priv->tt_local_hash;
	struct batadv_tt_common_entry *tt_common;
	struct batadv_tt_local_entry *tt_local;
	struct hlist_node *node, *node_tmp;
	struct hlist_head *head;
	spinlock_t *list_lock; /* protects write access to the hash lists */
	uint32_t i;

	if (!hash)
		return;

	for (i = 0; i < hash->size; i++) {
		head = &hash->table[i];
		list_lock = &hash->list_locks[i];

		spin_lock_bh(list_lock);
		hlist_for_each_entry_safe(tt_common, node, node_tmp, head,
					  hash_entry) {
			if (!(tt_common->flags & BATADV_TT_CLIENT_PENDING))
				continue;

			batadv_dbg(BATADV_DBG_TT, bat_priv,
				   "Deleting local tt entry (%pM): pending\n",
				   tt_common->addr);

			atomic_dec(&bat_priv->num_local_tt);
			hlist_del_rcu(node);
			tt_local = container_of(tt_common,
						struct batadv_tt_local_entry,
						common);
			batadv_tt_local_entry_free_ref(tt_local);
		}
		spin_unlock_bh(list_lock);
	}

}

static int batadv_tt_commit_changes(struct batadv_priv *bat_priv,
				    unsigned char **packet_buff,
				    int *packet_buff_len, int packet_min_len)
{
	uint16_t changed_num = 0;

	if (atomic_read(&bat_priv->tt_local_changes) < 1)
		return -ENOENT;

	changed_num = batadv_tt_set_flags(bat_priv->tt_local_hash,
					  BATADV_TT_CLIENT_NEW, false);

	/* all reset entries have to be counted as local entries */
	atomic_add(changed_num, &bat_priv->num_local_tt);
	batadv_tt_local_purge_pending_clients(bat_priv);
	bat_priv->tt_crc = batadv_tt_local_crc(bat_priv);

	/* Increment the TTVN only once per OGM interval */
	atomic_inc(&bat_priv->ttvn);
	batadv_dbg(BATADV_DBG_TT, bat_priv,
		   "Local changes committed, updating to ttvn %u\n",
		   (uint8_t)atomic_read(&bat_priv->ttvn));
	bat_priv->tt_poss_change = false;

	/* reset the sending counter */
	atomic_set(&bat_priv->tt_ogm_append_cnt, BATADV_TT_OGM_APPEND_MAX);

	return batadv_tt_changes_fill_buff(bat_priv, packet_buff,
					   packet_buff_len, packet_min_len);
}

/* when calling this function (hard_iface == primary_if) has to be true */
int batadv_tt_append_diff(struct batadv_priv *bat_priv,
			  unsigned char **packet_buff, int *packet_buff_len,
			  int packet_min_len)
{
	int tt_num_changes;

	/* if at least one change happened */
	tt_num_changes = batadv_tt_commit_changes(bat_priv, packet_buff,
						  packet_buff_len,
						  packet_min_len);

	/* if the changes have been sent often enough */
	if ((tt_num_changes < 0) &&
	    (!batadv_atomic_dec_not_zero(&bat_priv->tt_ogm_append_cnt))) {
		batadv_tt_realloc_packet_buff(packet_buff, packet_buff_len,
					      packet_min_len, packet_min_len);
		tt_num_changes = 0;
	}

	return tt_num_changes;
}

bool batadv_is_ap_isolated(struct batadv_priv *bat_priv, uint8_t *src,
			   uint8_t *dst)
{
	struct batadv_tt_local_entry *tt_local_entry = NULL;
	struct batadv_tt_global_entry *tt_global_entry = NULL;
	bool ret = false;

	if (!atomic_read(&bat_priv->ap_isolation))
		goto out;

	tt_local_entry = batadv_tt_local_hash_find(bat_priv, dst);
	if (!tt_local_entry)
		goto out;

	tt_global_entry = batadv_tt_global_hash_find(bat_priv, src);
	if (!tt_global_entry)
		goto out;

	if (!_batadv_is_ap_isolated(tt_local_entry, tt_global_entry))
		goto out;

	ret = true;

out:
	if (tt_global_entry)
		batadv_tt_global_entry_free_ref(tt_global_entry);
	if (tt_local_entry)
		batadv_tt_local_entry_free_ref(tt_local_entry);
	return ret;
}

void batadv_tt_update_orig(struct batadv_priv *bat_priv,
			   struct batadv_orig_node *orig_node,
			   const unsigned char *tt_buff, uint8_t tt_num_changes,
			   uint8_t ttvn, uint16_t tt_crc)
{
	uint8_t orig_ttvn = (uint8_t)atomic_read(&orig_node->last_ttvn);
	bool full_table = true;
	struct batadv_tt_change *tt_change;

	/* don't care about a backbone gateways updates. */
	if (batadv_bla_is_backbone_gw_orig(bat_priv, orig_node->orig))
		return;

	/* orig table not initialised AND first diff is in the OGM OR the ttvn
	 * increased by one -> we can apply the attached changes
	 */
	if ((!orig_node->tt_initialised && ttvn == 1) ||
	    ttvn - orig_ttvn == 1) {
		/* the OGM could not contain the changes due to their size or
		 * because they have already been sent BATADV_TT_OGM_APPEND_MAX
		 * times.
		 * In this case send a tt request
		 */
		if (!tt_num_changes) {
			full_table = false;
			goto request_table;
		}

		tt_change = (struct batadv_tt_change *)tt_buff;
		batadv_tt_update_changes(bat_priv, orig_node, tt_num_changes,
					 ttvn, tt_change);

		/* Even if we received the precomputed crc with the OGM, we
		 * prefer to recompute it to spot any possible inconsistency
		 * in the global table
		 */
		orig_node->tt_crc = batadv_tt_global_crc(bat_priv, orig_node);

		/* The ttvn alone is not enough to guarantee consistency
		 * because a single value could represent different states
		 * (due to the wrap around). Thus a node has to check whether
		 * the resulting table (after applying the changes) is still
		 * consistent or not. E.g. a node could disconnect while its
		 * ttvn is X and reconnect on ttvn = X + TTVN_MAX: in this case
		 * checking the CRC value is mandatory to detect the
		 * inconsistency
		 */
		if (orig_node->tt_crc != tt_crc)
			goto request_table;

		/* Roaming phase is over: tables are in sync again. I can
		 * unset the flag
		 */
		orig_node->tt_poss_change = false;
	} else {
		/* if we missed more than one change or our tables are not
		 * in sync anymore -> request fresh tt data
		 */
		if (!orig_node->tt_initialised || ttvn != orig_ttvn ||
		    orig_node->tt_crc != tt_crc) {
request_table:
			batadv_dbg(BATADV_DBG_TT, bat_priv,
				   "TT inconsistency for %pM. Need to retrieve the correct information (ttvn: %u last_ttvn: %u crc: %u last_crc: %u num_changes: %u)\n",
				   orig_node->orig, ttvn, orig_ttvn, tt_crc,
				   orig_node->tt_crc, tt_num_changes);
			batadv_send_tt_request(bat_priv, orig_node, ttvn,
					       tt_crc, full_table);
			return;
		}
	}
}

/* returns true whether we know that the client has moved from its old
 * originator to another one. This entry is kept is still kept for consistency
 * purposes
 */
bool batadv_tt_global_client_is_roaming(struct batadv_priv *bat_priv,
					uint8_t *addr)
{
	struct batadv_tt_global_entry *tt_global_entry;
	bool ret = false;

	tt_global_entry = batadv_tt_global_hash_find(bat_priv, addr);
	if (!tt_global_entry)
		goto out;

	ret = tt_global_entry->common.flags & BATADV_TT_CLIENT_ROAM;
	batadv_tt_global_entry_free_ref(tt_global_entry);
out:
	return ret;
}
