/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) B.A.T.M.A.N. contributors:
 *
 * Marek Lindner, Simon Wunderlich
 */

#ifndef _NET_BATMAN_ADV_ORIGINATOR_H_
#define _NET_BATMAN_ADV_ORIGINATOR_H_

#include "main.h"

#include <linux/compiler.h>
#include <linux/if_ether.h>
#include <linux/jhash.h>
#include <linux/netlink.h>
#include <linux/skbuff.h>
#include <linux/types.h>

bool batadv_compare_orig(const struct hlist_node *node, const void *data2);
int batadv_originator_init(struct batadv_priv *bat_priv);
void batadv_originator_free(struct batadv_priv *bat_priv);
void batadv_purge_orig_ref(struct batadv_priv *bat_priv);
void batadv_orig_node_put(struct batadv_orig_node *orig_node);
struct batadv_orig_node *batadv_orig_node_new(struct batadv_priv *bat_priv,
					      const u8 *addr);
struct batadv_hardif_neigh_node *
batadv_hardif_neigh_get(const struct batadv_hard_iface *hard_iface,
			const u8 *neigh_addr);
void
batadv_hardif_neigh_put(struct batadv_hardif_neigh_node *hardif_neigh);
struct batadv_neigh_node *
batadv_neigh_node_get_or_create(struct batadv_orig_node *orig_node,
				struct batadv_hard_iface *hard_iface,
				const u8 *neigh_addr);
void batadv_neigh_node_put(struct batadv_neigh_node *neigh_node);
struct batadv_neigh_node *
batadv_orig_router_get(struct batadv_orig_node *orig_node,
		       const struct batadv_hard_iface *if_outgoing);
struct batadv_neigh_ifinfo *
batadv_neigh_ifinfo_new(struct batadv_neigh_node *neigh,
			struct batadv_hard_iface *if_outgoing);
struct batadv_neigh_ifinfo *
batadv_neigh_ifinfo_get(struct batadv_neigh_node *neigh,
			struct batadv_hard_iface *if_outgoing);
void batadv_neigh_ifinfo_put(struct batadv_neigh_ifinfo *neigh_ifinfo);

int batadv_hardif_neigh_dump(struct sk_buff *msg, struct netlink_callback *cb);

struct batadv_orig_ifinfo *
batadv_orig_ifinfo_get(struct batadv_orig_node *orig_node,
		       struct batadv_hard_iface *if_outgoing);
struct batadv_orig_ifinfo *
batadv_orig_ifinfo_new(struct batadv_orig_node *orig_node,
		       struct batadv_hard_iface *if_outgoing);
void batadv_orig_ifinfo_put(struct batadv_orig_ifinfo *orig_ifinfo);

int batadv_orig_dump(struct sk_buff *msg, struct netlink_callback *cb);
struct batadv_orig_node_vlan *
batadv_orig_node_vlan_new(struct batadv_orig_node *orig_node,
			  unsigned short vid);
struct batadv_orig_node_vlan *
batadv_orig_node_vlan_get(struct batadv_orig_node *orig_node,
			  unsigned short vid);
void batadv_orig_node_vlan_put(struct batadv_orig_node_vlan *orig_vlan);

/**
 * batadv_choose_orig() - Return the index of the orig entry in the hash table
 * @data: mac address of the originator node
 * @size: the size of the hash table
 *
 * Return: the hash index where the object represented by @data should be
 * stored at.
 */
static inline u32 batadv_choose_orig(const void *data, u32 size)
{
	u32 hash = 0;

	hash = jhash(data, ETH_ALEN, hash);
	return hash % size;
}

struct batadv_orig_node *
batadv_orig_hash_find(struct batadv_priv *bat_priv, const void *data);

#endif /* _NET_BATMAN_ADV_ORIGINATOR_H_ */
