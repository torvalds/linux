/*
 * Copyright (C) 2008-2012 B.A.T.M.A.N. contributors:
 *
 * Simon Wunderlich
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
#include "send.h"
#include "translation-table.h"
#include "vis.h"
#include "soft-interface.h"
#include "hard-interface.h"
#include "hash.h"
#include "originator.h"

#define MAX_VIS_PACKET_SIZE 1000

static void start_vis_timer(struct bat_priv *bat_priv);

/* free the info */
static void free_info(struct kref *ref)
{
	struct vis_info *info = container_of(ref, struct vis_info, refcount);
	struct bat_priv *bat_priv = info->bat_priv;
	struct recvlist_node *entry, *tmp;

	list_del_init(&info->send_list);
	spin_lock_bh(&bat_priv->vis_list_lock);
	list_for_each_entry_safe(entry, tmp, &info->recv_list, list) {
		list_del(&entry->list);
		kfree(entry);
	}

	spin_unlock_bh(&bat_priv->vis_list_lock);
	kfree_skb(info->skb_packet);
	kfree(info);
}

/* Compare two vis packets, used by the hashing algorithm */
static int vis_info_cmp(const struct hlist_node *node, const void *data2)
{
	const struct vis_info *d1, *d2;
	const struct vis_packet *p1, *p2;

	d1 = container_of(node, struct vis_info, hash_entry);
	d2 = data2;
	p1 = (struct vis_packet *)d1->skb_packet->data;
	p2 = (struct vis_packet *)d2->skb_packet->data;
	return compare_eth(p1->vis_orig, p2->vis_orig);
}

/* hash function to choose an entry in a hash table of given size */
/* hash algorithm from http://en.wikipedia.org/wiki/Hash_table */
static uint32_t vis_info_choose(const void *data, uint32_t size)
{
	const struct vis_info *vis_info = data;
	const struct vis_packet *packet;
	const unsigned char *key;
	uint32_t hash = 0;
	size_t i;

	packet = (struct vis_packet *)vis_info->skb_packet->data;
	key = packet->vis_orig;
	for (i = 0; i < ETH_ALEN; i++) {
		hash += key[i];
		hash += (hash << 10);
		hash ^= (hash >> 6);
	}

	hash += (hash << 3);
	hash ^= (hash >> 11);
	hash += (hash << 15);

	return hash % size;
}

static struct vis_info *vis_hash_find(struct bat_priv *bat_priv,
				      const void *data)
{
	struct hashtable_t *hash = bat_priv->vis_hash;
	struct hlist_head *head;
	struct hlist_node *node;
	struct vis_info *vis_info, *vis_info_tmp = NULL;
	uint32_t index;

	if (!hash)
		return NULL;

	index = vis_info_choose(data, hash->size);
	head = &hash->table[index];

	rcu_read_lock();
	hlist_for_each_entry_rcu(vis_info, node, head, hash_entry) {
		if (!vis_info_cmp(node, data))
			continue;

		vis_info_tmp = vis_info;
		break;
	}
	rcu_read_unlock();

	return vis_info_tmp;
}

/* insert interface to the list of interfaces of one originator, if it
 * does not already exist in the list */
static void vis_data_insert_interface(const uint8_t *interface,
				      struct hlist_head *if_list,
				      bool primary)
{
	struct if_list_entry *entry;
	struct hlist_node *pos;

	hlist_for_each_entry(entry, pos, if_list, list) {
		if (compare_eth(entry->addr, interface))
			return;
	}

	/* it's a new address, add it to the list */
	entry = kmalloc(sizeof(*entry), GFP_ATOMIC);
	if (!entry)
		return;
	memcpy(entry->addr, interface, ETH_ALEN);
	entry->primary = primary;
	hlist_add_head(&entry->list, if_list);
}

static ssize_t vis_data_read_prim_sec(char *buff,
				      const struct hlist_head *if_list)
{
	struct if_list_entry *entry;
	struct hlist_node *pos;
	size_t len = 0;

	hlist_for_each_entry(entry, pos, if_list, list) {
		if (entry->primary)
			len += sprintf(buff + len, "PRIMARY, ");
		else
			len += sprintf(buff + len,  "SEC %pM, ", entry->addr);
	}

	return len;
}

static size_t vis_data_count_prim_sec(struct hlist_head *if_list)
{
	struct if_list_entry *entry;
	struct hlist_node *pos;
	size_t count = 0;

	hlist_for_each_entry(entry, pos, if_list, list) {
		if (entry->primary)
			count += 9;
		else
			count += 23;
	}

	return count;
}

/* read an entry  */
static ssize_t vis_data_read_entry(char *buff,
				   const struct vis_info_entry *entry,
				   const uint8_t *src, bool primary)
{
	/* maximal length: max(4+17+2, 3+17+1+3+2) == 26 */
	if (primary && entry->quality == 0)
		return sprintf(buff, "TT %pM, ", entry->dest);
	else if (compare_eth(entry->src, src))
		return sprintf(buff, "TQ %pM %d, ", entry->dest,
			       entry->quality);

	return 0;
}

int vis_seq_print_text(struct seq_file *seq, void *offset)
{
	struct hard_iface *primary_if;
	struct hlist_node *node;
	struct hlist_head *head;
	struct vis_info *info;
	struct vis_packet *packet;
	struct vis_info_entry *entries;
	struct net_device *net_dev = (struct net_device *)seq->private;
	struct bat_priv *bat_priv = netdev_priv(net_dev);
	struct hashtable_t *hash = bat_priv->vis_hash;
	HLIST_HEAD(vis_if_list);
	struct if_list_entry *entry;
	struct hlist_node *pos, *n;
	uint32_t i;
	int j, ret = 0;
	int vis_server = atomic_read(&bat_priv->vis_mode);
	size_t buff_pos, buf_size;
	char *buff;
	int compare;

	primary_if = primary_if_get_selected(bat_priv);
	if (!primary_if)
		goto out;

	if (vis_server == VIS_TYPE_CLIENT_UPDATE)
		goto out;

	buf_size = 1;
	/* Estimate length */
	spin_lock_bh(&bat_priv->vis_hash_lock);
	for (i = 0; i < hash->size; i++) {
		head = &hash->table[i];

		rcu_read_lock();
		hlist_for_each_entry_rcu(info, node, head, hash_entry) {
			packet = (struct vis_packet *)info->skb_packet->data;
			entries = (struct vis_info_entry *)
				((char *)packet + sizeof(*packet));

			for (j = 0; j < packet->entries; j++) {
				if (entries[j].quality == 0)
					continue;
				compare =
				 compare_eth(entries[j].src, packet->vis_orig);
				vis_data_insert_interface(entries[j].src,
							  &vis_if_list,
							  compare);
			}

			hlist_for_each_entry(entry, pos, &vis_if_list, list) {
				buf_size += 18 + 26 * packet->entries;

				/* add primary/secondary records */
				if (compare_eth(entry->addr, packet->vis_orig))
					buf_size +=
					  vis_data_count_prim_sec(&vis_if_list);

				buf_size += 1;
			}

			hlist_for_each_entry_safe(entry, pos, n, &vis_if_list,
						  list) {
				hlist_del(&entry->list);
				kfree(entry);
			}
		}
		rcu_read_unlock();
	}

	buff = kmalloc(buf_size, GFP_ATOMIC);
	if (!buff) {
		spin_unlock_bh(&bat_priv->vis_hash_lock);
		ret = -ENOMEM;
		goto out;
	}
	buff[0] = '\0';
	buff_pos = 0;

	for (i = 0; i < hash->size; i++) {
		head = &hash->table[i];

		rcu_read_lock();
		hlist_for_each_entry_rcu(info, node, head, hash_entry) {
			packet = (struct vis_packet *)info->skb_packet->data;
			entries = (struct vis_info_entry *)
				((char *)packet + sizeof(*packet));

			for (j = 0; j < packet->entries; j++) {
				if (entries[j].quality == 0)
					continue;
				compare =
				 compare_eth(entries[j].src, packet->vis_orig);
				vis_data_insert_interface(entries[j].src,
							  &vis_if_list,
							  compare);
			}

			hlist_for_each_entry(entry, pos, &vis_if_list, list) {
				buff_pos += sprintf(buff + buff_pos, "%pM,",
						entry->addr);

				for (j = 0; j < packet->entries; j++)
					buff_pos += vis_data_read_entry(
							buff + buff_pos,
							&entries[j],
							entry->addr,
							entry->primary);

				/* add primary/secondary records */
				if (compare_eth(entry->addr, packet->vis_orig))
					buff_pos +=
					 vis_data_read_prim_sec(buff + buff_pos,
								&vis_if_list);

				buff_pos += sprintf(buff + buff_pos, "\n");
			}

			hlist_for_each_entry_safe(entry, pos, n, &vis_if_list,
						  list) {
				hlist_del(&entry->list);
				kfree(entry);
			}
		}
		rcu_read_unlock();
	}

	spin_unlock_bh(&bat_priv->vis_hash_lock);

	seq_printf(seq, "%s", buff);
	kfree(buff);

out:
	if (primary_if)
		hardif_free_ref(primary_if);
	return ret;
}

/* add the info packet to the send list, if it was not
 * already linked in. */
static void send_list_add(struct bat_priv *bat_priv, struct vis_info *info)
{
	if (list_empty(&info->send_list)) {
		kref_get(&info->refcount);
		list_add_tail(&info->send_list, &bat_priv->vis_send_list);
	}
}

/* delete the info packet from the send list, if it was
 * linked in. */
static void send_list_del(struct vis_info *info)
{
	if (!list_empty(&info->send_list)) {
		list_del_init(&info->send_list);
		kref_put(&info->refcount, free_info);
	}
}

/* tries to add one entry to the receive list. */
static void recv_list_add(struct bat_priv *bat_priv,
			  struct list_head *recv_list, const char *mac)
{
	struct recvlist_node *entry;

	entry = kmalloc(sizeof(*entry), GFP_ATOMIC);
	if (!entry)
		return;

	memcpy(entry->mac, mac, ETH_ALEN);
	spin_lock_bh(&bat_priv->vis_list_lock);
	list_add_tail(&entry->list, recv_list);
	spin_unlock_bh(&bat_priv->vis_list_lock);
}

/* returns 1 if this mac is in the recv_list */
static int recv_list_is_in(struct bat_priv *bat_priv,
			   const struct list_head *recv_list, const char *mac)
{
	const struct recvlist_node *entry;

	spin_lock_bh(&bat_priv->vis_list_lock);
	list_for_each_entry(entry, recv_list, list) {
		if (compare_eth(entry->mac, mac)) {
			spin_unlock_bh(&bat_priv->vis_list_lock);
			return 1;
		}
	}
	spin_unlock_bh(&bat_priv->vis_list_lock);
	return 0;
}

/* try to add the packet to the vis_hash. return NULL if invalid (e.g. too old,
 * broken.. ).	vis hash must be locked outside.  is_new is set when the packet
 * is newer than old entries in the hash. */
static struct vis_info *add_packet(struct bat_priv *bat_priv,
				   struct vis_packet *vis_packet,
				   int vis_info_len, int *is_new,
				   int make_broadcast)
{
	struct vis_info *info, *old_info;
	struct vis_packet *search_packet, *old_packet;
	struct vis_info search_elem;
	struct vis_packet *packet;
	int hash_added;

	*is_new = 0;
	/* sanity check */
	if (!bat_priv->vis_hash)
		return NULL;

	/* see if the packet is already in vis_hash */
	search_elem.skb_packet = dev_alloc_skb(sizeof(*search_packet));
	if (!search_elem.skb_packet)
		return NULL;
	search_packet = (struct vis_packet *)skb_put(search_elem.skb_packet,
						     sizeof(*search_packet));

	memcpy(search_packet->vis_orig, vis_packet->vis_orig, ETH_ALEN);
	old_info = vis_hash_find(bat_priv, &search_elem);
	kfree_skb(search_elem.skb_packet);

	if (old_info) {
		old_packet = (struct vis_packet *)old_info->skb_packet->data;
		if (!seq_after(ntohl(vis_packet->seqno),
			       ntohl(old_packet->seqno))) {
			if (old_packet->seqno == vis_packet->seqno) {
				recv_list_add(bat_priv, &old_info->recv_list,
					      vis_packet->sender_orig);
				return old_info;
			} else {
				/* newer packet is already in hash. */
				return NULL;
			}
		}
		/* remove old entry */
		hash_remove(bat_priv->vis_hash, vis_info_cmp, vis_info_choose,
			    old_info);
		send_list_del(old_info);
		kref_put(&old_info->refcount, free_info);
	}

	info = kmalloc(sizeof(*info), GFP_ATOMIC);
	if (!info)
		return NULL;

	info->skb_packet = dev_alloc_skb(sizeof(*packet) + vis_info_len +
					 ETH_HLEN);
	if (!info->skb_packet) {
		kfree(info);
		return NULL;
	}
	skb_reserve(info->skb_packet, ETH_HLEN);
	packet = (struct vis_packet *)skb_put(info->skb_packet, sizeof(*packet)
					      + vis_info_len);

	kref_init(&info->refcount);
	INIT_LIST_HEAD(&info->send_list);
	INIT_LIST_HEAD(&info->recv_list);
	info->first_seen = jiffies;
	info->bat_priv = bat_priv;
	memcpy(packet, vis_packet, sizeof(*packet) + vis_info_len);

	/* initialize and add new packet. */
	*is_new = 1;

	/* Make it a broadcast packet, if required */
	if (make_broadcast)
		memcpy(packet->target_orig, broadcast_addr, ETH_ALEN);

	/* repair if entries is longer than packet. */
	if (packet->entries * sizeof(struct vis_info_entry) > vis_info_len)
		packet->entries = vis_info_len / sizeof(struct vis_info_entry);

	recv_list_add(bat_priv, &info->recv_list, packet->sender_orig);

	/* try to add it */
	hash_added = hash_add(bat_priv->vis_hash, vis_info_cmp, vis_info_choose,
			      info, &info->hash_entry);
	if (hash_added != 0) {
		/* did not work (for some reason) */
		kref_put(&info->refcount, free_info);
		info = NULL;
	}

	return info;
}

/* handle the server sync packet, forward if needed. */
void receive_server_sync_packet(struct bat_priv *bat_priv,
				struct vis_packet *vis_packet,
				int vis_info_len)
{
	struct vis_info *info;
	int is_new, make_broadcast;
	int vis_server = atomic_read(&bat_priv->vis_mode);

	make_broadcast = (vis_server == VIS_TYPE_SERVER_SYNC);

	spin_lock_bh(&bat_priv->vis_hash_lock);
	info = add_packet(bat_priv, vis_packet, vis_info_len,
			  &is_new, make_broadcast);
	if (!info)
		goto end;

	/* only if we are server ourselves and packet is newer than the one in
	 * hash.*/
	if (vis_server == VIS_TYPE_SERVER_SYNC && is_new)
		send_list_add(bat_priv, info);
end:
	spin_unlock_bh(&bat_priv->vis_hash_lock);
}

/* handle an incoming client update packet and schedule forward if needed. */
void receive_client_update_packet(struct bat_priv *bat_priv,
				  struct vis_packet *vis_packet,
				  int vis_info_len)
{
	struct vis_info *info;
	struct vis_packet *packet;
	int is_new;
	int vis_server = atomic_read(&bat_priv->vis_mode);
	int are_target = 0;

	/* clients shall not broadcast. */
	if (is_broadcast_ether_addr(vis_packet->target_orig))
		return;

	/* Are we the target for this VIS packet? */
	if (vis_server == VIS_TYPE_SERVER_SYNC	&&
	    is_my_mac(vis_packet->target_orig))
		are_target = 1;

	spin_lock_bh(&bat_priv->vis_hash_lock);
	info = add_packet(bat_priv, vis_packet, vis_info_len,
			  &is_new, are_target);

	if (!info)
		goto end;
	/* note that outdated packets will be dropped at this point. */

	packet = (struct vis_packet *)info->skb_packet->data;

	/* send only if we're the target server or ... */
	if (are_target && is_new) {
		packet->vis_type = VIS_TYPE_SERVER_SYNC;	/* upgrade! */
		send_list_add(bat_priv, info);

		/* ... we're not the recipient (and thus need to forward). */
	} else if (!is_my_mac(packet->target_orig)) {
		send_list_add(bat_priv, info);
	}

end:
	spin_unlock_bh(&bat_priv->vis_hash_lock);
}

/* Walk the originators and find the VIS server with the best tq. Set the packet
 * address to its address and return the best_tq.
 *
 * Must be called with the originator hash locked */
static int find_best_vis_server(struct bat_priv *bat_priv,
				struct vis_info *info)
{
	struct hashtable_t *hash = bat_priv->orig_hash;
	struct neigh_node *router;
	struct hlist_node *node;
	struct hlist_head *head;
	struct orig_node *orig_node;
	struct vis_packet *packet;
	int best_tq = -1;
	uint32_t i;

	packet = (struct vis_packet *)info->skb_packet->data;

	for (i = 0; i < hash->size; i++) {
		head = &hash->table[i];

		rcu_read_lock();
		hlist_for_each_entry_rcu(orig_node, node, head, hash_entry) {
			router = orig_node_get_router(orig_node);
			if (!router)
				continue;

			if ((orig_node->flags & VIS_SERVER) &&
			    (router->tq_avg > best_tq)) {
				best_tq = router->tq_avg;
				memcpy(packet->target_orig, orig_node->orig,
				       ETH_ALEN);
			}
			neigh_node_free_ref(router);
		}
		rcu_read_unlock();
	}

	return best_tq;
}

/* Return true if the vis packet is full. */
static bool vis_packet_full(const struct vis_info *info)
{
	const struct vis_packet *packet;
	packet = (struct vis_packet *)info->skb_packet->data;

	if (MAX_VIS_PACKET_SIZE / sizeof(struct vis_info_entry)
		< packet->entries + 1)
		return true;
	return false;
}

/* generates a packet of own vis data,
 * returns 0 on success, -1 if no packet could be generated */
static int generate_vis_packet(struct bat_priv *bat_priv)
{
	struct hashtable_t *hash = bat_priv->orig_hash;
	struct hlist_node *node;
	struct hlist_head *head;
	struct orig_node *orig_node;
	struct neigh_node *router;
	struct vis_info *info = bat_priv->my_vis_info;
	struct vis_packet *packet = (struct vis_packet *)info->skb_packet->data;
	struct vis_info_entry *entry;
	struct tt_common_entry *tt_common_entry;
	int best_tq = -1;
	uint32_t i;

	info->first_seen = jiffies;
	packet->vis_type = atomic_read(&bat_priv->vis_mode);

	memcpy(packet->target_orig, broadcast_addr, ETH_ALEN);
	packet->header.ttl = TTL;
	packet->seqno = htonl(ntohl(packet->seqno) + 1);
	packet->entries = 0;
	skb_trim(info->skb_packet, sizeof(*packet));

	if (packet->vis_type == VIS_TYPE_CLIENT_UPDATE) {
		best_tq = find_best_vis_server(bat_priv, info);

		if (best_tq < 0)
			return best_tq;
	}

	for (i = 0; i < hash->size; i++) {
		head = &hash->table[i];

		rcu_read_lock();
		hlist_for_each_entry_rcu(orig_node, node, head, hash_entry) {
			router = orig_node_get_router(orig_node);
			if (!router)
				continue;

			if (!compare_eth(router->addr, orig_node->orig))
				goto next;

			if (router->if_incoming->if_status != IF_ACTIVE)
				goto next;

			if (router->tq_avg < 1)
				goto next;

			/* fill one entry into buffer. */
			entry = (struct vis_info_entry *)
				      skb_put(info->skb_packet, sizeof(*entry));
			memcpy(entry->src,
			       router->if_incoming->net_dev->dev_addr,
			       ETH_ALEN);
			memcpy(entry->dest, orig_node->orig, ETH_ALEN);
			entry->quality = router->tq_avg;
			packet->entries++;

next:
			neigh_node_free_ref(router);

			if (vis_packet_full(info))
				goto unlock;
		}
		rcu_read_unlock();
	}

	hash = bat_priv->tt_local_hash;

	for (i = 0; i < hash->size; i++) {
		head = &hash->table[i];

		rcu_read_lock();
		hlist_for_each_entry_rcu(tt_common_entry, node, head,
					 hash_entry) {
			entry = (struct vis_info_entry *)
					skb_put(info->skb_packet,
						sizeof(*entry));
			memset(entry->src, 0, ETH_ALEN);
			memcpy(entry->dest, tt_common_entry->addr, ETH_ALEN);
			entry->quality = 0; /* 0 means TT */
			packet->entries++;

			if (vis_packet_full(info))
				goto unlock;
		}
		rcu_read_unlock();
	}

	return 0;

unlock:
	rcu_read_unlock();
	return 0;
}

/* free old vis packets. Must be called with this vis_hash_lock
 * held */
static void purge_vis_packets(struct bat_priv *bat_priv)
{
	uint32_t i;
	struct hashtable_t *hash = bat_priv->vis_hash;
	struct hlist_node *node, *node_tmp;
	struct hlist_head *head;
	struct vis_info *info;

	for (i = 0; i < hash->size; i++) {
		head = &hash->table[i];

		hlist_for_each_entry_safe(info, node, node_tmp,
					  head, hash_entry) {
			/* never purge own data. */
			if (info == bat_priv->my_vis_info)
				continue;

			if (has_timed_out(info->first_seen, VIS_TIMEOUT)) {
				hlist_del(node);
				send_list_del(info);
				kref_put(&info->refcount, free_info);
			}
		}
	}
}

static void broadcast_vis_packet(struct bat_priv *bat_priv,
				 struct vis_info *info)
{
	struct neigh_node *router;
	struct hashtable_t *hash = bat_priv->orig_hash;
	struct hlist_node *node;
	struct hlist_head *head;
	struct orig_node *orig_node;
	struct vis_packet *packet;
	struct sk_buff *skb;
	struct hard_iface *hard_iface;
	uint8_t dstaddr[ETH_ALEN];
	uint32_t i;


	packet = (struct vis_packet *)info->skb_packet->data;

	/* send to all routers in range. */
	for (i = 0; i < hash->size; i++) {
		head = &hash->table[i];

		rcu_read_lock();
		hlist_for_each_entry_rcu(orig_node, node, head, hash_entry) {
			/* if it's a vis server and reachable, send it. */
			if (!(orig_node->flags & VIS_SERVER))
				continue;

			router = orig_node_get_router(orig_node);
			if (!router)
				continue;

			/* don't send it if we already received the packet from
			 * this node. */
			if (recv_list_is_in(bat_priv, &info->recv_list,
					    orig_node->orig)) {
				neigh_node_free_ref(router);
				continue;
			}

			memcpy(packet->target_orig, orig_node->orig, ETH_ALEN);
			hard_iface = router->if_incoming;
			memcpy(dstaddr, router->addr, ETH_ALEN);

			neigh_node_free_ref(router);

			skb = skb_clone(info->skb_packet, GFP_ATOMIC);
			if (skb)
				send_skb_packet(skb, hard_iface, dstaddr);

		}
		rcu_read_unlock();
	}
}

static void unicast_vis_packet(struct bat_priv *bat_priv,
			       struct vis_info *info)
{
	struct orig_node *orig_node;
	struct neigh_node *router = NULL;
	struct sk_buff *skb;
	struct vis_packet *packet;

	packet = (struct vis_packet *)info->skb_packet->data;

	orig_node = orig_hash_find(bat_priv, packet->target_orig);
	if (!orig_node)
		goto out;

	router = orig_node_get_router(orig_node);
	if (!router)
		goto out;

	skb = skb_clone(info->skb_packet, GFP_ATOMIC);
	if (skb)
		send_skb_packet(skb, router->if_incoming, router->addr);

out:
	if (router)
		neigh_node_free_ref(router);
	if (orig_node)
		orig_node_free_ref(orig_node);
}

/* only send one vis packet. called from send_vis_packets() */
static void send_vis_packet(struct bat_priv *bat_priv, struct vis_info *info)
{
	struct hard_iface *primary_if;
	struct vis_packet *packet;

	primary_if = primary_if_get_selected(bat_priv);
	if (!primary_if)
		goto out;

	packet = (struct vis_packet *)info->skb_packet->data;
	if (packet->header.ttl < 2) {
		pr_debug("Error - can't send vis packet: ttl exceeded\n");
		goto out;
	}

	memcpy(packet->sender_orig, primary_if->net_dev->dev_addr, ETH_ALEN);
	packet->header.ttl--;

	if (is_broadcast_ether_addr(packet->target_orig))
		broadcast_vis_packet(bat_priv, info);
	else
		unicast_vis_packet(bat_priv, info);
	packet->header.ttl++; /* restore TTL */

out:
	if (primary_if)
		hardif_free_ref(primary_if);
}

/* called from timer; send (and maybe generate) vis packet. */
static void send_vis_packets(struct work_struct *work)
{
	struct delayed_work *delayed_work =
		container_of(work, struct delayed_work, work);
	struct bat_priv *bat_priv =
		container_of(delayed_work, struct bat_priv, vis_work);
	struct vis_info *info;

	spin_lock_bh(&bat_priv->vis_hash_lock);
	purge_vis_packets(bat_priv);

	if (generate_vis_packet(bat_priv) == 0) {
		/* schedule if generation was successful */
		send_list_add(bat_priv, bat_priv->my_vis_info);
	}

	while (!list_empty(&bat_priv->vis_send_list)) {
		info = list_first_entry(&bat_priv->vis_send_list,
					typeof(*info), send_list);

		kref_get(&info->refcount);
		spin_unlock_bh(&bat_priv->vis_hash_lock);

		send_vis_packet(bat_priv, info);

		spin_lock_bh(&bat_priv->vis_hash_lock);
		send_list_del(info);
		kref_put(&info->refcount, free_info);
	}
	spin_unlock_bh(&bat_priv->vis_hash_lock);
	start_vis_timer(bat_priv);
}

/* init the vis server. this may only be called when if_list is already
 * initialized (e.g. bat0 is initialized, interfaces have been added) */
int vis_init(struct bat_priv *bat_priv)
{
	struct vis_packet *packet;
	int hash_added;

	if (bat_priv->vis_hash)
		return 0;

	spin_lock_bh(&bat_priv->vis_hash_lock);

	bat_priv->vis_hash = hash_new(256);
	if (!bat_priv->vis_hash) {
		pr_err("Can't initialize vis_hash\n");
		goto err;
	}

	bat_priv->my_vis_info = kmalloc(MAX_VIS_PACKET_SIZE, GFP_ATOMIC);
	if (!bat_priv->my_vis_info)
		goto err;

	bat_priv->my_vis_info->skb_packet = dev_alloc_skb(sizeof(*packet) +
							  MAX_VIS_PACKET_SIZE +
							  ETH_HLEN);
	if (!bat_priv->my_vis_info->skb_packet)
		goto free_info;

	skb_reserve(bat_priv->my_vis_info->skb_packet, ETH_HLEN);
	packet = (struct vis_packet *)skb_put(bat_priv->my_vis_info->skb_packet,
					      sizeof(*packet));

	/* prefill the vis info */
	bat_priv->my_vis_info->first_seen = jiffies -
						msecs_to_jiffies(VIS_INTERVAL);
	INIT_LIST_HEAD(&bat_priv->my_vis_info->recv_list);
	INIT_LIST_HEAD(&bat_priv->my_vis_info->send_list);
	kref_init(&bat_priv->my_vis_info->refcount);
	bat_priv->my_vis_info->bat_priv = bat_priv;
	packet->header.version = COMPAT_VERSION;
	packet->header.packet_type = BAT_VIS;
	packet->header.ttl = TTL;
	packet->seqno = 0;
	packet->entries = 0;

	INIT_LIST_HEAD(&bat_priv->vis_send_list);

	hash_added = hash_add(bat_priv->vis_hash, vis_info_cmp, vis_info_choose,
			      bat_priv->my_vis_info,
			      &bat_priv->my_vis_info->hash_entry);
	if (hash_added != 0) {
		pr_err("Can't add own vis packet into hash\n");
		/* not in hash, need to remove it manually. */
		kref_put(&bat_priv->my_vis_info->refcount, free_info);
		goto err;
	}

	spin_unlock_bh(&bat_priv->vis_hash_lock);
	start_vis_timer(bat_priv);
	return 0;

free_info:
	kfree(bat_priv->my_vis_info);
	bat_priv->my_vis_info = NULL;
err:
	spin_unlock_bh(&bat_priv->vis_hash_lock);
	vis_quit(bat_priv);
	return -ENOMEM;
}

/* Decrease the reference count on a hash item info */
static void free_info_ref(struct hlist_node *node, void *arg)
{
	struct vis_info *info;

	info = container_of(node, struct vis_info, hash_entry);
	send_list_del(info);
	kref_put(&info->refcount, free_info);
}

/* shutdown vis-server */
void vis_quit(struct bat_priv *bat_priv)
{
	if (!bat_priv->vis_hash)
		return;

	cancel_delayed_work_sync(&bat_priv->vis_work);

	spin_lock_bh(&bat_priv->vis_hash_lock);
	/* properly remove, kill timers ... */
	hash_delete(bat_priv->vis_hash, free_info_ref, NULL);
	bat_priv->vis_hash = NULL;
	bat_priv->my_vis_info = NULL;
	spin_unlock_bh(&bat_priv->vis_hash_lock);
}

/* schedule packets for (re)transmission */
static void start_vis_timer(struct bat_priv *bat_priv)
{
	INIT_DELAYED_WORK(&bat_priv->vis_work, send_vis_packets);
	queue_delayed_work(bat_event_workqueue, &bat_priv->vis_work,
			   msecs_to_jiffies(VIS_INTERVAL));
}
