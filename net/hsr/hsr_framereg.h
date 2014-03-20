/* Copyright 2011-2013 Autronica Fire and Security AS
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * Author(s):
 *	2011-2013 Arvid Brodin, arvid.brodin@xdin.com
 */

#ifndef _HSR_FRAMEREG_H
#define _HSR_FRAMEREG_H

#include "hsr_main.h"

struct node_entry;

struct node_entry *hsr_find_node(struct list_head *node_db, struct sk_buff *skb);

struct node_entry *hsr_merge_node(struct hsr_priv *hsr_priv,
				  struct node_entry *node,
				  struct sk_buff *skb,
				  enum hsr_dev_idx dev_idx);

void hsr_addr_subst_source(struct hsr_priv *hsr_priv, struct sk_buff *skb);
void hsr_addr_subst_dest(struct hsr_priv *hsr_priv, struct ethhdr *ethhdr,
			 enum hsr_dev_idx dev_idx);

void hsr_register_frame_in(struct node_entry *node, enum hsr_dev_idx dev_idx);

int hsr_register_frame_out(struct node_entry *node, enum hsr_dev_idx dev_idx,
			   struct sk_buff *skb);

void hsr_prune_nodes(struct hsr_priv *hsr_priv);

int hsr_create_self_node(struct list_head *self_node_db,
			 unsigned char addr_a[ETH_ALEN],
			 unsigned char addr_b[ETH_ALEN]);

void *hsr_get_next_node(struct hsr_priv *hsr_priv, void *_pos,
			unsigned char addr[ETH_ALEN]);

int hsr_get_node_data(struct hsr_priv *hsr_priv,
		      const unsigned char *addr,
		      unsigned char addr_b[ETH_ALEN],
		      unsigned int *addr_b_ifindex,
		      int *if1_age,
		      u16 *if1_seq,
		      int *if2_age,
		      u16 *if2_seq);

#endif /* _HSR_FRAMEREG_H */
