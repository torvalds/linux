/*
 * Pretty printing Support for iptables xt_qtaguid module.
 *
 * (C) 2011 Google, Inc
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/*
 * There are run-time debug flags enabled via the debug_mask module param, or
 * via the DEFAULT_DEBUG_MASK. See xt_qtaguid_internal.h.
 */
#define DEBUG

#include <linux/fs.h>
#include <linux/gfp.h>
#include <linux/net.h>
#include <linux/rbtree.h>
#include <linux/slab.h>
#include <linux/spinlock_types.h>


#include "xt_qtaguid_internal.h"
#include "xt_qtaguid_print.h"

char *pp_tag_t(tag_t *tag)
{
	if (!tag)
		return kasprintf(GFP_ATOMIC, "tag_t@null{}");
	return kasprintf(GFP_ATOMIC,
			 "tag_t@%p{tag=0x%llx, uid=%u}",
			 tag, *tag, get_uid_from_tag(*tag));
}

char *pp_data_counters(struct data_counters *dc, bool showValues)
{
	if (!dc)
		return kasprintf(GFP_ATOMIC, "data_counters@null{}");
	if (showValues)
		return kasprintf(
			GFP_ATOMIC, "data_counters@%p{"
			"set0{"
			"rx{"
			"tcp{b=%llu, p=%llu}, "
			"udp{b=%llu, p=%llu},"
			"other{b=%llu, p=%llu}}, "
			"tx{"
			"tcp{b=%llu, p=%llu}, "
			"udp{b=%llu, p=%llu},"
			"other{b=%llu, p=%llu}}}, "
			"set1{"
			"rx{"
			"tcp{b=%llu, p=%llu}, "
			"udp{b=%llu, p=%llu},"
			"other{b=%llu, p=%llu}}, "
			"tx{"
			"tcp{b=%llu, p=%llu}, "
			"udp{b=%llu, p=%llu},"
			"other{b=%llu, p=%llu}}}}",
			dc,
			dc->bpc[0][IFS_RX][IFS_TCP].bytes,
			dc->bpc[0][IFS_RX][IFS_TCP].packets,
			dc->bpc[0][IFS_RX][IFS_UDP].bytes,
			dc->bpc[0][IFS_RX][IFS_UDP].packets,
			dc->bpc[0][IFS_RX][IFS_PROTO_OTHER].bytes,
			dc->bpc[0][IFS_RX][IFS_PROTO_OTHER].packets,
			dc->bpc[0][IFS_TX][IFS_TCP].bytes,
			dc->bpc[0][IFS_TX][IFS_TCP].packets,
			dc->bpc[0][IFS_TX][IFS_UDP].bytes,
			dc->bpc[0][IFS_TX][IFS_UDP].packets,
			dc->bpc[0][IFS_TX][IFS_PROTO_OTHER].bytes,
			dc->bpc[0][IFS_TX][IFS_PROTO_OTHER].packets,
			dc->bpc[1][IFS_RX][IFS_TCP].bytes,
			dc->bpc[1][IFS_RX][IFS_TCP].packets,
			dc->bpc[1][IFS_RX][IFS_UDP].bytes,
			dc->bpc[1][IFS_RX][IFS_UDP].packets,
			dc->bpc[1][IFS_RX][IFS_PROTO_OTHER].bytes,
			dc->bpc[1][IFS_RX][IFS_PROTO_OTHER].packets,
			dc->bpc[1][IFS_TX][IFS_TCP].bytes,
			dc->bpc[1][IFS_TX][IFS_TCP].packets,
			dc->bpc[1][IFS_TX][IFS_UDP].bytes,
			dc->bpc[1][IFS_TX][IFS_UDP].packets,
			dc->bpc[1][IFS_TX][IFS_PROTO_OTHER].bytes,
			dc->bpc[1][IFS_TX][IFS_PROTO_OTHER].packets);
	else
		return kasprintf(GFP_ATOMIC, "data_counters@%p{...}", dc);
}

char *pp_tag_node(struct tag_node *tn)
{
	char *tag_str;
	char *res;

	if (!tn)
		return kasprintf(GFP_ATOMIC, "tag_node@null{}");
	tag_str = pp_tag_t(&tn->tag);
	res = kasprintf(GFP_ATOMIC,
			"tag_node@%p{tag=%s}",
			tn, tag_str);
	kfree(tag_str);
	return res;
}

char *pp_tag_ref(struct tag_ref *tr)
{
	char *tn_str;
	char *res;

	if (!tr)
		return kasprintf(GFP_ATOMIC, "tag_ref@null{}");
	tn_str = pp_tag_node(&tr->tn);
	res = kasprintf(GFP_ATOMIC,
			"tag_ref@%p{%s, num_sock_tags=%d}",
			tr, tn_str, tr->num_sock_tags);
	kfree(tn_str);
	return res;
}

char *pp_tag_stat(struct tag_stat *ts)
{
	char *tn_str;
	char *counters_str;
	char *parent_counters_str;
	char *res;

	if (!ts)
		return kasprintf(GFP_ATOMIC, "tag_stat@null{}");
	tn_str = pp_tag_node(&ts->tn);
	counters_str = pp_data_counters(&ts->counters, true);
	parent_counters_str = pp_data_counters(ts->parent_counters, false);
	res = kasprintf(GFP_ATOMIC,
			"tag_stat@%p{%s, counters=%s, parent_counters=%s}",
			ts, tn_str, counters_str, parent_counters_str);
	kfree(tn_str);
	kfree(counters_str);
	kfree(parent_counters_str);
	return res;
}

char *pp_iface_stat(struct iface_stat *is)
{
	if (!is)
		return kasprintf(GFP_ATOMIC, "iface_stat@null{}");
	return kasprintf(GFP_ATOMIC, "iface_stat@%p{"
			 "list=list_head{...}, "
			 "ifname=%s, "
			 "rx_bytes=%llu, "
			 "rx_packets=%llu, "
			 "tx_bytes=%llu, "
			 "tx_packets=%llu, "
			 "active=%d, "
			 "proc_ptr=%p, "
			 "tag_stat_tree=rb_root{...}}",
			 is,
			 is->ifname,
			 is->rx_bytes,
			 is->rx_packets,
			 is->tx_bytes,
			 is->tx_packets,
			 is->active,
			 is->proc_ptr);
}

char *pp_sock_tag(struct sock_tag *st)
{
	char *tag_str;
	char *res;

	if (!st)
		return kasprintf(GFP_ATOMIC, "sock_tag@null{}");
	tag_str = pp_tag_t(&st->tag);
	res = kasprintf(GFP_ATOMIC, "sock_tag@%p{"
			"sock_node=rb_node{...}, "
			"sk=%p socket=%p (f_count=%lu), list=list_head{...}, "
			"pid=%u, tag=%s}",
			st, st->sk, st->socket, atomic_long_read(
				&st->socket->file->f_count),
			st->pid, tag_str);
	kfree(tag_str);
	return res;
}

char *pp_uid_tag_data(struct uid_tag_data *utd)
{
	char *res;

	if (!utd)
		return kasprintf(GFP_ATOMIC, "uid_tag_data@null{}");
	res = kasprintf(GFP_ATOMIC, "uid_tag_data@%p{"
			"uid=%u, num_active_acct_tags=%d, "
			"tag_node_tree=rb_root{...}, "
			"proc_qtu_data_tree=rb_root{...}}",
			utd, utd->uid,
			utd->num_active_tags);
	return res;
}

char *pp_proc_qtu_data(struct proc_qtu_data *pqd)
{
	char *parent_tag_data_str;
	char *res;

	if (!pqd)
		return kasprintf(GFP_ATOMIC, "proc_qtu_data@null{}");
	parent_tag_data_str = pp_uid_tag_data(pqd->parent_tag_data);
	res = kasprintf(GFP_ATOMIC, "proc_qtu_data@%p{"
			"node=rb_node{...}, pid=%u, "
			"parent_tag_data=%s, "
			"sock_tag_list=list_head{...}}",
			pqd, pqd->pid, parent_tag_data_str
		);
	kfree(parent_tag_data_str);
	return res;
}

/*------------------------------------------*/
void prdebug_sock_tag_tree(int indent_level,
			   struct rb_root *sock_tag_tree)
{
	struct rb_node *node;
	struct sock_tag *sock_tag_entry;
	char *str;

	str = "sock_tag_tree=rb_root{";
	CT_DEBUG("%*d: %s\n", indent_level*2, indent_level, str);
	indent_level++;
	for (node = rb_first(sock_tag_tree);
	     node;
	     node = rb_next(node)) {
		sock_tag_entry = rb_entry(node, struct sock_tag, sock_node);
		str = pp_sock_tag(sock_tag_entry);
		CT_DEBUG("%*d: %s,\n", indent_level*2, indent_level, str);
		kfree(str);
	}
	indent_level--;
	str = "}";
	CT_DEBUG("%*d: %s\n", indent_level*2, indent_level, str);
}

void prdebug_sock_tag_list(int indent_level,
			   struct list_head *sock_tag_list)
{
	struct sock_tag *sock_tag_entry;
	char *str;

	str = "sock_tag_list=list_head{";
	CT_DEBUG("%*d: %s\n", indent_level*2, indent_level, str);
	indent_level++;
	list_for_each_entry(sock_tag_entry, sock_tag_list, list) {
		str = pp_sock_tag(sock_tag_entry);
		CT_DEBUG("%*d: %s,\n", indent_level*2, indent_level, str);
		kfree(str);
	}
	indent_level--;
	str = "}";
	CT_DEBUG("%*d: %s\n", indent_level*2, indent_level, str);
}

void prdebug_proc_qtu_data_tree(int indent_level,
				struct rb_root *proc_qtu_data_tree)
{
	char *str;
	struct rb_node *node;
	struct proc_qtu_data *proc_qtu_data_entry;

	str = "proc_qtu_data_tree=rb_root{";
	CT_DEBUG("%*d: %s\n", indent_level*2, indent_level, str);
	indent_level++;
	for (node = rb_first(proc_qtu_data_tree);
	     node;
	     node = rb_next(node)) {
		proc_qtu_data_entry = rb_entry(node,
					       struct proc_qtu_data,
					       node);
		str = pp_proc_qtu_data(proc_qtu_data_entry);
		CT_DEBUG("%*d: %s,\n", indent_level*2, indent_level,
			 str);
		kfree(str);
		indent_level++;
		prdebug_sock_tag_list(indent_level,
				      &proc_qtu_data_entry->sock_tag_list);
		indent_level--;

	}
	indent_level--;
	str = "}";
	CT_DEBUG("%*d: %s\n", indent_level*2, indent_level, str);
}

void prdebug_tag_ref_tree(int indent_level, struct rb_root *tag_ref_tree)
{
	char *str;
	struct rb_node *node;
	struct tag_ref *tag_ref_entry;

	str = "tag_ref_tree{";
	CT_DEBUG("%*d: %s\n", indent_level*2, indent_level, str);
	indent_level++;
	for (node = rb_first(tag_ref_tree);
	     node;
	     node = rb_next(node)) {
		tag_ref_entry = rb_entry(node,
					 struct tag_ref,
					 tn.node);
		str = pp_tag_ref(tag_ref_entry);
		CT_DEBUG("%*d: %s,\n", indent_level*2, indent_level,
			 str);
		kfree(str);
	}
	indent_level--;
	str = "}";
	CT_DEBUG("%*d: %s\n", indent_level*2, indent_level, str);
}

void prdebug_uid_tag_data_tree(int indent_level,
			       struct rb_root *uid_tag_data_tree)
{
	char *str;
	struct rb_node *node;
	struct uid_tag_data *uid_tag_data_entry;

	str = "uid_tag_data_tree=rb_root{";
	CT_DEBUG("%*d: %s\n", indent_level*2, indent_level, str);
	indent_level++;
	for (node = rb_first(uid_tag_data_tree);
	     node;
	     node = rb_next(node)) {
		uid_tag_data_entry = rb_entry(node, struct uid_tag_data,
					      node);
		str = pp_uid_tag_data(uid_tag_data_entry);
		CT_DEBUG("%*d: %s,\n", indent_level*2, indent_level, str);
		kfree(str);
		if (!RB_EMPTY_ROOT(&uid_tag_data_entry->tag_ref_tree)) {
			indent_level++;
			prdebug_tag_ref_tree(indent_level,
					     &uid_tag_data_entry->tag_ref_tree);
			indent_level--;
		}
	}
	indent_level--;
	str = "}";
	CT_DEBUG("%*d: %s\n", indent_level*2, indent_level, str);
}

void prdebug_tag_stat_tree(int indent_level,
				  struct rb_root *tag_stat_tree)
{
	char *str;
	struct rb_node *node;
	struct tag_stat *ts_entry;

	str = "tag_stat_tree{";
	CT_DEBUG("%*d: %s\n", indent_level*2, indent_level, str);
	indent_level++;
	for (node = rb_first(tag_stat_tree);
	     node;
	     node = rb_next(node)) {
		ts_entry = rb_entry(node, struct tag_stat, tn.node);
		str = pp_tag_stat(ts_entry);
		CT_DEBUG("%*d: %s\n", indent_level*2, indent_level,
			 str);
		kfree(str);
	}
	indent_level--;
	str = "}";
	CT_DEBUG("%*d: %s\n", indent_level*2, indent_level, str);
}

void prdebug_iface_stat_list(int indent_level,
			     struct list_head *iface_stat_list)
{
	char *str;
	struct iface_stat *iface_entry;

	str = "iface_stat_list=list_head{";
	CT_DEBUG("%*d: %s\n", indent_level*2, indent_level, str);
	indent_level++;
	list_for_each_entry(iface_entry, iface_stat_list, list) {
		str = pp_iface_stat(iface_entry);
		CT_DEBUG("%*d: %s\n", indent_level*2, indent_level, str);
		kfree(str);

		spin_lock_bh(&iface_entry->tag_stat_list_lock);
		if (!RB_EMPTY_ROOT(&iface_entry->tag_stat_tree)) {
			indent_level++;
			prdebug_tag_stat_tree(indent_level,
					      &iface_entry->tag_stat_tree);
			indent_level--;
		}
		spin_unlock_bh(&iface_entry->tag_stat_list_lock);
	}
	indent_level--;
	str = "}";
	CT_DEBUG("%*d: %s\n", indent_level*2, indent_level, str);
}
