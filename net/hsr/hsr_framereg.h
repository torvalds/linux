/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright 2011-2014 Autronica Fire and Security AS
 *
 * Author(s):
 *	2011-2014 Arvid Brodin, arvid.brodin@alten.se
 *
 * include file for HSR and PRP.
 */

#ifndef __HSR_FRAMEREG_H
#define __HSR_FRAMEREG_H

#include "hsr_main.h"

struct hsr_node;

struct hsr_frame_info {
	struct sk_buff *skb_std;
	struct sk_buff *skb_hsr;
	struct sk_buff *skb_prp;
	struct hsr_port *port_rcv;
	struct hsr_node *node_src;
	u16 sequence_nr;
	bool is_supervision;
	bool is_vlan;
	bool is_local_dest;
	bool is_local_exclusive;
	bool is_from_san;
};

void hsr_del_self_node(struct hsr_priv *hsr);
void hsr_del_nodes(struct list_head *node_db);
struct hsr_node *hsr_get_node(struct hsr_port *port, struct list_head *node_db,
			      struct sk_buff *skb, bool is_sup,
			      enum hsr_port_type rx_port);
void hsr_handle_sup_frame(struct hsr_frame_info *frame);
bool hsr_addr_is_self(struct hsr_priv *hsr, unsigned char *addr);

void hsr_addr_subst_source(struct hsr_node *node, struct sk_buff *skb);
void hsr_addr_subst_dest(struct hsr_node *node_src, struct sk_buff *skb,
			 struct hsr_port *port);

void hsr_register_frame_in(struct hsr_node *node, struct hsr_port *port,
			   u16 sequence_nr);
int hsr_register_frame_out(struct hsr_port *port, struct hsr_node *node,
			   u16 sequence_nr);

void hsr_prune_nodes(struct timer_list *t);

int hsr_create_self_node(struct hsr_priv *hsr,
			 unsigned char addr_a[ETH_ALEN],
			 unsigned char addr_b[ETH_ALEN]);

void *hsr_get_next_node(struct hsr_priv *hsr, void *_pos,
			unsigned char addr[ETH_ALEN]);

int hsr_get_node_data(struct hsr_priv *hsr,
		      const unsigned char *addr,
		      unsigned char addr_b[ETH_ALEN],
		      unsigned int *addr_b_ifindex,
		      int *if1_age,
		      u16 *if1_seq,
		      int *if2_age,
		      u16 *if2_seq);

void prp_handle_san_frame(bool san, enum hsr_port_type port,
			  struct hsr_node *node);
void prp_update_san_info(struct hsr_node *node, bool is_sup);

struct hsr_node {
	struct list_head	mac_list;
	unsigned char		macaddress_A[ETH_ALEN];
	unsigned char		macaddress_B[ETH_ALEN];
	/* Local slave through which AddrB frames are received from this node */
	enum hsr_port_type	addr_B_port;
	unsigned long		time_in[HSR_PT_PORTS];
	bool			time_in_stale[HSR_PT_PORTS];
	/* if the node is a SAN */
	bool			san_a;
	bool			san_b;
	u16			seq_out[HSR_PT_PORTS];
	struct rcu_head		rcu_head;
};

#endif /* __HSR_FRAMEREG_H */
