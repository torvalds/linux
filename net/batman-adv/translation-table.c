/*
 * Copyright (C) 2007-2011 B.A.T.M.A.N. contributors:
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
 *
 */

#include "main.h"
#include "translation-table.h"
#include "soft-interface.h"
#include "hard-interface.h"
#include "hash.h"
#include "originator.h"

static void tt_local_purge(struct work_struct *work);
static void _tt_global_del_orig(struct bat_priv *bat_priv,
				 struct tt_global_entry *tt_global_entry,
				 char *message);

/* returns 1 if they are the same mac addr */
static int compare_ltt(struct hlist_node *node, void *data2)
{
	void *data1 = container_of(node, struct tt_local_entry, hash_entry);

	return (memcmp(data1, data2, ETH_ALEN) == 0 ? 1 : 0);
}

/* returns 1 if they are the same mac addr */
static int compare_gtt(struct hlist_node *node, void *data2)
{
	void *data1 = container_of(node, struct tt_global_entry, hash_entry);

	return (memcmp(data1, data2, ETH_ALEN) == 0 ? 1 : 0);
}

static void tt_local_start_timer(struct bat_priv *bat_priv)
{
	INIT_DELAYED_WORK(&bat_priv->tt_work, tt_local_purge);
	queue_delayed_work(bat_event_workqueue, &bat_priv->tt_work, 10 * HZ);
}

static struct tt_local_entry *tt_local_hash_find(struct bat_priv *bat_priv,
						   void *data)
{
	struct hashtable_t *hash = bat_priv->tt_local_hash;
	struct hlist_head *head;
	struct hlist_node *node;
	struct tt_local_entry *tt_local_entry, *tt_local_entry_tmp = NULL;
	int index;

	if (!hash)
		return NULL;

	index = choose_orig(data, hash->size);
	head = &hash->table[index];

	rcu_read_lock();
	hlist_for_each_entry_rcu(tt_local_entry, node, head, hash_entry) {
		if (!compare_eth(tt_local_entry, data))
			continue;

		tt_local_entry_tmp = tt_local_entry;
		break;
	}
	rcu_read_unlock();

	return tt_local_entry_tmp;
}

static struct tt_global_entry *tt_global_hash_find(struct bat_priv *bat_priv,
						     void *data)
{
	struct hashtable_t *hash = bat_priv->tt_global_hash;
	struct hlist_head *head;
	struct hlist_node *node;
	struct tt_global_entry *tt_global_entry;
	struct tt_global_entry *tt_global_entry_tmp = NULL;
	int index;

	if (!hash)
		return NULL;

	index = choose_orig(data, hash->size);
	head = &hash->table[index];

	rcu_read_lock();
	hlist_for_each_entry_rcu(tt_global_entry, node, head, hash_entry) {
		if (!compare_eth(tt_global_entry, data))
			continue;

		tt_global_entry_tmp = tt_global_entry;
		break;
	}
	rcu_read_unlock();

	return tt_global_entry_tmp;
}

int tt_local_init(struct bat_priv *bat_priv)
{
	if (bat_priv->tt_local_hash)
		return 1;

	bat_priv->tt_local_hash = hash_new(1024);

	if (!bat_priv->tt_local_hash)
		return 0;

	atomic_set(&bat_priv->tt_local_changed, 0);
	tt_local_start_timer(bat_priv);

	return 1;
}

void tt_local_add(struct net_device *soft_iface, uint8_t *addr)
{
	struct bat_priv *bat_priv = netdev_priv(soft_iface);
	struct tt_local_entry *tt_local_entry;
	struct tt_global_entry *tt_global_entry;
	int required_bytes;

	spin_lock_bh(&bat_priv->tt_lhash_lock);
	tt_local_entry = tt_local_hash_find(bat_priv, addr);
	spin_unlock_bh(&bat_priv->tt_lhash_lock);

	if (tt_local_entry) {
		tt_local_entry->last_seen = jiffies;
		return;
	}

	/* only announce as many hosts as possible in the batman-packet and
	   space in batman_packet->num_tt That also should give a limit to
	   MAC-flooding. */
	required_bytes = (bat_priv->num_local_tt + 1) * ETH_ALEN;
	required_bytes += BAT_PACKET_LEN;

	if ((required_bytes > ETH_DATA_LEN) ||
	    (atomic_read(&bat_priv->aggregated_ogms) &&
	     required_bytes > MAX_AGGREGATION_BYTES) ||
	    (bat_priv->num_local_tt + 1 > 255)) {
		bat_dbg(DBG_ROUTES, bat_priv,
			"Can't add new local tt entry (%pM): "
			"number of local tt entries exceeds packet size\n",
			addr);
		return;
	}

	bat_dbg(DBG_ROUTES, bat_priv,
		"Creating new local tt entry: %pM\n", addr);

	tt_local_entry = kmalloc(sizeof(struct tt_local_entry), GFP_ATOMIC);
	if (!tt_local_entry)
		return;

	memcpy(tt_local_entry->addr, addr, ETH_ALEN);
	tt_local_entry->last_seen = jiffies;

	/* the batman interface mac address should never be purged */
	if (compare_eth(addr, soft_iface->dev_addr))
		tt_local_entry->never_purge = 1;
	else
		tt_local_entry->never_purge = 0;

	spin_lock_bh(&bat_priv->tt_lhash_lock);

	hash_add(bat_priv->tt_local_hash, compare_ltt, choose_orig,
		 tt_local_entry, &tt_local_entry->hash_entry);
	bat_priv->num_local_tt++;
	atomic_set(&bat_priv->tt_local_changed, 1);

	spin_unlock_bh(&bat_priv->tt_lhash_lock);

	/* remove address from global hash if present */
	spin_lock_bh(&bat_priv->tt_ghash_lock);

	tt_global_entry = tt_global_hash_find(bat_priv, addr);

	if (tt_global_entry)
		_tt_global_del_orig(bat_priv, tt_global_entry,
				     "local tt received");

	spin_unlock_bh(&bat_priv->tt_ghash_lock);
}

int tt_local_fill_buffer(struct bat_priv *bat_priv,
			  unsigned char *buff, int buff_len)
{
	struct hashtable_t *hash = bat_priv->tt_local_hash;
	struct tt_local_entry *tt_local_entry;
	struct hlist_node *node;
	struct hlist_head *head;
	int i, count = 0;

	spin_lock_bh(&bat_priv->tt_lhash_lock);

	for (i = 0; i < hash->size; i++) {
		head = &hash->table[i];

		rcu_read_lock();
		hlist_for_each_entry_rcu(tt_local_entry, node,
					 head, hash_entry) {
			if (buff_len < (count + 1) * ETH_ALEN)
				break;

			memcpy(buff + (count * ETH_ALEN), tt_local_entry->addr,
			       ETH_ALEN);

			count++;
		}
		rcu_read_unlock();
	}

	/* if we did not get all new local tts see you next time  ;-) */
	if (count == bat_priv->num_local_tt)
		atomic_set(&bat_priv->tt_local_changed, 0);

	spin_unlock_bh(&bat_priv->tt_lhash_lock);
	return count;
}

int tt_local_seq_print_text(struct seq_file *seq, void *offset)
{
	struct net_device *net_dev = (struct net_device *)seq->private;
	struct bat_priv *bat_priv = netdev_priv(net_dev);
	struct hashtable_t *hash = bat_priv->tt_local_hash;
	struct tt_local_entry *tt_local_entry;
	struct hard_iface *primary_if;
	struct hlist_node *node;
	struct hlist_head *head;
	size_t buf_size, pos;
	char *buff;
	int i, ret = 0;

	primary_if = primary_if_get_selected(bat_priv);
	if (!primary_if) {
		ret = seq_printf(seq, "BATMAN mesh %s disabled - "
				 "please specify interfaces to enable it\n",
				 net_dev->name);
		goto out;
	}

	if (primary_if->if_status != IF_ACTIVE) {
		ret = seq_printf(seq, "BATMAN mesh %s disabled - "
				 "primary interface not active\n",
				 net_dev->name);
		goto out;
	}

	seq_printf(seq, "Locally retrieved addresses (from %s) "
		   "announced via TT:\n",
		   net_dev->name);

	spin_lock_bh(&bat_priv->tt_lhash_lock);

	buf_size = 1;
	/* Estimate length for: " * xx:xx:xx:xx:xx:xx\n" */
	for (i = 0; i < hash->size; i++) {
		head = &hash->table[i];

		rcu_read_lock();
		__hlist_for_each_rcu(node, head)
			buf_size += 21;
		rcu_read_unlock();
	}

	buff = kmalloc(buf_size, GFP_ATOMIC);
	if (!buff) {
		spin_unlock_bh(&bat_priv->tt_lhash_lock);
		ret = -ENOMEM;
		goto out;
	}

	buff[0] = '\0';
	pos = 0;

	for (i = 0; i < hash->size; i++) {
		head = &hash->table[i];

		rcu_read_lock();
		hlist_for_each_entry_rcu(tt_local_entry, node,
					 head, hash_entry) {
			pos += snprintf(buff + pos, 22, " * %pM\n",
					tt_local_entry->addr);
		}
		rcu_read_unlock();
	}

	spin_unlock_bh(&bat_priv->tt_lhash_lock);

	seq_printf(seq, "%s", buff);
	kfree(buff);
out:
	if (primary_if)
		hardif_free_ref(primary_if);
	return ret;
}

static void _tt_local_del(struct hlist_node *node, void *arg)
{
	struct bat_priv *bat_priv = (struct bat_priv *)arg;
	void *data = container_of(node, struct tt_local_entry, hash_entry);

	kfree(data);
	bat_priv->num_local_tt--;
	atomic_set(&bat_priv->tt_local_changed, 1);
}

static void tt_local_del(struct bat_priv *bat_priv,
			  struct tt_local_entry *tt_local_entry,
			  char *message)
{
	bat_dbg(DBG_ROUTES, bat_priv, "Deleting local tt entry (%pM): %s\n",
		tt_local_entry->addr, message);

	hash_remove(bat_priv->tt_local_hash, compare_ltt, choose_orig,
		    tt_local_entry->addr);
	_tt_local_del(&tt_local_entry->hash_entry, bat_priv);
}

void tt_local_remove(struct bat_priv *bat_priv,
		      uint8_t *addr, char *message)
{
	struct tt_local_entry *tt_local_entry;

	spin_lock_bh(&bat_priv->tt_lhash_lock);

	tt_local_entry = tt_local_hash_find(bat_priv, addr);

	if (tt_local_entry)
		tt_local_del(bat_priv, tt_local_entry, message);

	spin_unlock_bh(&bat_priv->tt_lhash_lock);
}

static void tt_local_purge(struct work_struct *work)
{
	struct delayed_work *delayed_work =
		container_of(work, struct delayed_work, work);
	struct bat_priv *bat_priv =
		container_of(delayed_work, struct bat_priv, tt_work);
	struct hashtable_t *hash = bat_priv->tt_local_hash;
	struct tt_local_entry *tt_local_entry;
	struct hlist_node *node, *node_tmp;
	struct hlist_head *head;
	unsigned long timeout;
	int i;

	spin_lock_bh(&bat_priv->tt_lhash_lock);

	for (i = 0; i < hash->size; i++) {
		head = &hash->table[i];

		hlist_for_each_entry_safe(tt_local_entry, node, node_tmp,
					  head, hash_entry) {
			if (tt_local_entry->never_purge)
				continue;

			timeout = tt_local_entry->last_seen;
			timeout += TT_LOCAL_TIMEOUT * HZ;

			if (time_before(jiffies, timeout))
				continue;

			tt_local_del(bat_priv, tt_local_entry,
				      "address timed out");
		}
	}

	spin_unlock_bh(&bat_priv->tt_lhash_lock);
	tt_local_start_timer(bat_priv);
}

void tt_local_free(struct bat_priv *bat_priv)
{
	if (!bat_priv->tt_local_hash)
		return;

	cancel_delayed_work_sync(&bat_priv->tt_work);
	hash_delete(bat_priv->tt_local_hash, _tt_local_del, bat_priv);
	bat_priv->tt_local_hash = NULL;
}

int tt_global_init(struct bat_priv *bat_priv)
{
	if (bat_priv->tt_global_hash)
		return 1;

	bat_priv->tt_global_hash = hash_new(1024);

	if (!bat_priv->tt_global_hash)
		return 0;

	return 1;
}

void tt_global_add_orig(struct bat_priv *bat_priv,
			 struct orig_node *orig_node,
			 unsigned char *tt_buff, int tt_buff_len)
{
	struct tt_global_entry *tt_global_entry;
	struct tt_local_entry *tt_local_entry;
	int tt_buff_count = 0;
	unsigned char *tt_ptr;

	while ((tt_buff_count + 1) * ETH_ALEN <= tt_buff_len) {
		spin_lock_bh(&bat_priv->tt_ghash_lock);

		tt_ptr = tt_buff + (tt_buff_count * ETH_ALEN);
		tt_global_entry = tt_global_hash_find(bat_priv, tt_ptr);

		if (!tt_global_entry) {
			spin_unlock_bh(&bat_priv->tt_ghash_lock);

			tt_global_entry =
				kmalloc(sizeof(struct tt_global_entry),
					GFP_ATOMIC);

			if (!tt_global_entry)
				break;

			memcpy(tt_global_entry->addr, tt_ptr, ETH_ALEN);

			bat_dbg(DBG_ROUTES, bat_priv,
				"Creating new global tt entry: "
				"%pM (via %pM)\n",
				tt_global_entry->addr, orig_node->orig);

			spin_lock_bh(&bat_priv->tt_ghash_lock);
			hash_add(bat_priv->tt_global_hash, compare_gtt,
				 choose_orig, tt_global_entry,
				 &tt_global_entry->hash_entry);

		}

		tt_global_entry->orig_node = orig_node;
		spin_unlock_bh(&bat_priv->tt_ghash_lock);

		/* remove address from local hash if present */
		spin_lock_bh(&bat_priv->tt_lhash_lock);

		tt_ptr = tt_buff + (tt_buff_count * ETH_ALEN);
		tt_local_entry = tt_local_hash_find(bat_priv, tt_ptr);

		if (tt_local_entry)
			tt_local_del(bat_priv, tt_local_entry,
				      "global tt received");

		spin_unlock_bh(&bat_priv->tt_lhash_lock);

		tt_buff_count++;
	}

	/* initialize, and overwrite if malloc succeeds */
	orig_node->tt_buff = NULL;
	orig_node->tt_buff_len = 0;

	if (tt_buff_len > 0) {
		orig_node->tt_buff = kmalloc(tt_buff_len, GFP_ATOMIC);
		if (orig_node->tt_buff) {
			memcpy(orig_node->tt_buff, tt_buff, tt_buff_len);
			orig_node->tt_buff_len = tt_buff_len;
		}
	}
}

int tt_global_seq_print_text(struct seq_file *seq, void *offset)
{
	struct net_device *net_dev = (struct net_device *)seq->private;
	struct bat_priv *bat_priv = netdev_priv(net_dev);
	struct hashtable_t *hash = bat_priv->tt_global_hash;
	struct tt_global_entry *tt_global_entry;
	struct hard_iface *primary_if;
	struct hlist_node *node;
	struct hlist_head *head;
	size_t buf_size, pos;
	char *buff;
	int i, ret = 0;

	primary_if = primary_if_get_selected(bat_priv);
	if (!primary_if) {
		ret = seq_printf(seq, "BATMAN mesh %s disabled - please "
				 "specify interfaces to enable it\n",
				 net_dev->name);
		goto out;
	}

	if (primary_if->if_status != IF_ACTIVE) {
		ret = seq_printf(seq, "BATMAN mesh %s disabled - "
				 "primary interface not active\n",
				 net_dev->name);
		goto out;
	}

	seq_printf(seq,
		   "Globally announced TT entries received via the mesh %s\n",
		   net_dev->name);

	spin_lock_bh(&bat_priv->tt_ghash_lock);

	buf_size = 1;
	/* Estimate length for: " * xx:xx:xx:xx:xx:xx via xx:xx:xx:xx:xx:xx\n"*/
	for (i = 0; i < hash->size; i++) {
		head = &hash->table[i];

		rcu_read_lock();
		__hlist_for_each_rcu(node, head)
			buf_size += 43;
		rcu_read_unlock();
	}

	buff = kmalloc(buf_size, GFP_ATOMIC);
	if (!buff) {
		spin_unlock_bh(&bat_priv->tt_ghash_lock);
		ret = -ENOMEM;
		goto out;
	}
	buff[0] = '\0';
	pos = 0;

	for (i = 0; i < hash->size; i++) {
		head = &hash->table[i];

		rcu_read_lock();
		hlist_for_each_entry_rcu(tt_global_entry, node,
					 head, hash_entry) {
			pos += snprintf(buff + pos, 44,
					" * %pM via %pM\n",
					tt_global_entry->addr,
					tt_global_entry->orig_node->orig);
		}
		rcu_read_unlock();
	}

	spin_unlock_bh(&bat_priv->tt_ghash_lock);

	seq_printf(seq, "%s", buff);
	kfree(buff);
out:
	if (primary_if)
		hardif_free_ref(primary_if);
	return ret;
}

static void _tt_global_del_orig(struct bat_priv *bat_priv,
				 struct tt_global_entry *tt_global_entry,
				 char *message)
{
	bat_dbg(DBG_ROUTES, bat_priv,
		"Deleting global tt entry %pM (via %pM): %s\n",
		tt_global_entry->addr, tt_global_entry->orig_node->orig,
		message);

	hash_remove(bat_priv->tt_global_hash, compare_gtt, choose_orig,
		    tt_global_entry->addr);
	kfree(tt_global_entry);
}

void tt_global_del_orig(struct bat_priv *bat_priv,
			 struct orig_node *orig_node, char *message)
{
	struct tt_global_entry *tt_global_entry;
	int tt_buff_count = 0;
	unsigned char *tt_ptr;

	if (orig_node->tt_buff_len == 0)
		return;

	spin_lock_bh(&bat_priv->tt_ghash_lock);

	while ((tt_buff_count + 1) * ETH_ALEN <= orig_node->tt_buff_len) {
		tt_ptr = orig_node->tt_buff + (tt_buff_count * ETH_ALEN);
		tt_global_entry = tt_global_hash_find(bat_priv, tt_ptr);

		if ((tt_global_entry) &&
		    (tt_global_entry->orig_node == orig_node))
			_tt_global_del_orig(bat_priv, tt_global_entry,
					     message);

		tt_buff_count++;
	}

	spin_unlock_bh(&bat_priv->tt_ghash_lock);

	orig_node->tt_buff_len = 0;
	kfree(orig_node->tt_buff);
	orig_node->tt_buff = NULL;
}

static void tt_global_del(struct hlist_node *node, void *arg)
{
	void *data = container_of(node, struct tt_global_entry, hash_entry);

	kfree(data);
}

void tt_global_free(struct bat_priv *bat_priv)
{
	if (!bat_priv->tt_global_hash)
		return;

	hash_delete(bat_priv->tt_global_hash, tt_global_del, NULL);
	bat_priv->tt_global_hash = NULL;
}

struct orig_node *transtable_search(struct bat_priv *bat_priv, uint8_t *addr)
{
	struct tt_global_entry *tt_global_entry;
	struct orig_node *orig_node = NULL;

	spin_lock_bh(&bat_priv->tt_ghash_lock);
	tt_global_entry = tt_global_hash_find(bat_priv, addr);

	if (!tt_global_entry)
		goto out;

	if (!atomic_inc_not_zero(&tt_global_entry->orig_node->refcount))
		goto out;

	orig_node = tt_global_entry->orig_node;

out:
	spin_unlock_bh(&bat_priv->tt_ghash_lock);
	return orig_node;
}
