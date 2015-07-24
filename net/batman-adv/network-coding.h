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

#ifndef _NET_BATMAN_ADV_NETWORK_CODING_H_
#define _NET_BATMAN_ADV_NETWORK_CODING_H_

#include "main.h"

#include <linux/types.h>

struct batadv_nc_node;
struct batadv_neigh_node;
struct batadv_ogm_packet;
struct batadv_orig_node;
struct batadv_priv;
struct net_device;
struct seq_file;
struct sk_buff;

#ifdef CONFIG_BATMAN_ADV_NC

void batadv_nc_status_update(struct net_device *net_dev);
int batadv_nc_init(void);
int batadv_nc_mesh_init(struct batadv_priv *bat_priv);
void batadv_nc_mesh_free(struct batadv_priv *bat_priv);
void batadv_nc_update_nc_node(struct batadv_priv *bat_priv,
			      struct batadv_orig_node *orig_node,
			      struct batadv_orig_node *orig_neigh_node,
			      struct batadv_ogm_packet *ogm_packet,
			      int is_single_hop_neigh);
void batadv_nc_purge_orig(struct batadv_priv *bat_priv,
			  struct batadv_orig_node *orig_node,
			  bool (*to_purge)(struct batadv_priv *,
					   struct batadv_nc_node *));
void batadv_nc_init_bat_priv(struct batadv_priv *bat_priv);
void batadv_nc_init_orig(struct batadv_orig_node *orig_node);
bool batadv_nc_skb_forward(struct sk_buff *skb,
			   struct batadv_neigh_node *neigh_node);
void batadv_nc_skb_store_for_decoding(struct batadv_priv *bat_priv,
				      struct sk_buff *skb);
void batadv_nc_skb_store_sniffed_unicast(struct batadv_priv *bat_priv,
					 struct sk_buff *skb);
int batadv_nc_nodes_seq_print_text(struct seq_file *seq, void *offset);
int batadv_nc_init_debugfs(struct batadv_priv *bat_priv);

#else /* ifdef CONFIG_BATMAN_ADV_NC */

static inline void batadv_nc_status_update(struct net_device *net_dev)
{
}

static inline int batadv_nc_init(void)
{
	return 0;
}

static inline int batadv_nc_mesh_init(struct batadv_priv *bat_priv)
{
	return 0;
}

static inline void batadv_nc_mesh_free(struct batadv_priv *bat_priv)
{
}

static inline void
batadv_nc_update_nc_node(struct batadv_priv *bat_priv,
			 struct batadv_orig_node *orig_node,
			 struct batadv_orig_node *orig_neigh_node,
			 struct batadv_ogm_packet *ogm_packet,
			 int is_single_hop_neigh)
{
}

static inline void
batadv_nc_purge_orig(struct batadv_priv *bat_priv,
		     struct batadv_orig_node *orig_node,
		     bool (*to_purge)(struct batadv_priv *,
				      struct batadv_nc_node *))
{
}

static inline void batadv_nc_init_bat_priv(struct batadv_priv *bat_priv)
{
}

static inline void batadv_nc_init_orig(struct batadv_orig_node *orig_node)
{
}

static inline bool batadv_nc_skb_forward(struct sk_buff *skb,
					 struct batadv_neigh_node *neigh_node)
{
	return false;
}

static inline void
batadv_nc_skb_store_for_decoding(struct batadv_priv *bat_priv,
				 struct sk_buff *skb)
{
}

static inline void
batadv_nc_skb_store_sniffed_unicast(struct batadv_priv *bat_priv,
				    struct sk_buff *skb)
{
}

static inline int batadv_nc_nodes_seq_print_text(struct seq_file *seq,
						 void *offset)
{
	return 0;
}

static inline int batadv_nc_init_debugfs(struct batadv_priv *bat_priv)
{
	return 0;
}

#endif /* ifdef CONFIG_BATMAN_ADV_NC */

#endif /* _NET_BATMAN_ADV_NETWORK_CODING_H_ */
