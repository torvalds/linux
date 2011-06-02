/* Copyright (C) 2011-2012 B.A.T.M.A.N. contributors:
 *
 * Antonio Quartulli
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

#ifndef _NET_BATMAN_ADV_ARP_H_
#define _NET_BATMAN_ADV_ARP_H_

#include "types.h"
#include "originator.h"

#include <linux/if_arp.h>

#define BATADV_DAT_ADDR_MAX ((batadv_dat_addr_t)~(batadv_dat_addr_t)0)

/**
 * batadv_dat_init_orig_node_addr - assign a DAT address to the orig_node
 * @orig_node: the node to assign the DAT address to
 */
static inline void
batadv_dat_init_orig_node_addr(struct batadv_orig_node *orig_node)
{
	uint32_t addr;

	addr = batadv_choose_orig(orig_node->orig, BATADV_DAT_ADDR_MAX);
	orig_node->dat_addr = (batadv_dat_addr_t)addr;
}

/**
 * batadv_dat_init_own_addr - assign a DAT address to the node itself
 * @bat_priv: the bat priv with all the soft interface information
 * @primary_if: a pointer to the primary interface
 */
static inline void
batadv_dat_init_own_addr(struct batadv_priv *bat_priv,
			 struct batadv_hard_iface *primary_if)
{
	uint32_t addr;

	addr = batadv_choose_orig(primary_if->net_dev->dev_addr,
				  BATADV_DAT_ADDR_MAX);

	bat_priv->dat.addr = (batadv_dat_addr_t)addr;
}

int batadv_dat_init(struct batadv_priv *bat_priv);
void batadv_dat_free(struct batadv_priv *bat_priv);
int batadv_dat_cache_seq_print_text(struct seq_file *seq, void *offset);

#endif /* _NET_BATMAN_ADV_ARP_H_ */
