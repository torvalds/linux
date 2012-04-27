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
 * Most of the functions in this file just waste time if DEBUG is not defined.
 * The matching xt_qtaguid_print.h will static inline empty funcs if the needed
 * debug flags ore not defined.
 * Those funcs that fail to allocate memory will panic as there is no need to
 * hobble allong just pretending to do the requested work.
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

#ifdef DDEBUG

static void _bug_on_err_or_null(void *ptr)
{
	if (IS_ERR_OR_NULL(ptr)) {
		pr_err("qtaguid: kmalloc failed\n");
		BUG();
	}
}

char *pp_tag_t(tag_t *tag)
{
	char *res;

	if (!tag)
		res = kasprintf(GFP_ATOMIC, "tag_t@null{}");
	else
		res = kasprintf(GFP_ATOMIC,
				"tag_t@%p{tag=0x%llx, uid=%u}",
				tag, *tag, get_uid_from_tag(*tag));
	_bug_on_err_or_null(res);
	return res;
}

char *pp_data_counters(struct data_counters *dc, bool showValues)
{
	char *res;

	if (!dc)
		res = kasprintf(GFP_ATOMIC, "data_counters@null{}");
	else if (showValues)
		res = kasprintf(
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
		res = kasprintf(GFP_ATOMIC, "data_counters@%p{...}", dc);
	_bug_on_err_or_null(res);
	return res;
}

char *pp_tag_node(struct tag_node *tn)
{
	char *tag_str;
	char *res;

	if (!tn) {
		res = kasprintf(GFP_ATOMIC, "tag_node@null{}");
		_bug_on_err_or_null(res);
		return res;
	}
	tag_str = pp_tag_t(&tn->tag);
	res = kasprintf(GFP_ATOMIC,
			"tag_node@%p{tag=%s}",
			tn, tag_str);
	_bug_on_err_or_null(res);
	kfree(tag_str);
	return res;
}

char *pp_tag_ref(struct tag_ref *tr)
{
	char *tn_str;
	char *res;

	if (!tr) {
		res = kasprintf(GFP_ATOMIC, "tag_ref@null{}");
		_bug_on_err_or_null(res);
		return res;
	}
	tn_str = pp_tag_node(&tr->tn);
	res = kasprintf(GFP_ATOMIC,
			"tag_ref@%p{%s, num_sock_tags=%d}",
			tr, tn_str, tr->num_sock_tags);
	_bug_on_err_or_null(res);
	kfree(tn_str);
	return res;
}

char *pp_tag_stat(struct tag_stat *ts)
{
	char *tn_str;
	char *counters_str;
	char *parent_counters_str;
	char *res;

	if (!ts) {
		res = kasprintf(GFP_ATOMIC, "tag_stat@null{}");
		_bug_on_err_or_null(res);
		return res;
	}
	tn_str = pp_tag_node(&ts->tn);
	counters_str = pp_data_counters(&ts->counters, true);
	parent_counters_str = pp_data_counters(ts->parent_counters, false);
	res = kasprintf(GFP_ATOMIC,
			"tag_stat@%p{%s, counters=%s, parent_counters=%s}",
			ts, tn_str, counters_str, parent_counters_str);
	_bug_on_err_or_null(res);
	kfree(tn_str);
	kfree(counters_str);
	kfree(parent_counters_str);
	return res;
}

char *pp_iface_stat(struct iface_stat *is)
{
	char *res;
	if (!is)
		res = kasprintf(GFP_ATOMIC, "iface_stat@null{}");
	else
		res = kasprintf(GFP_ATOMIC, "iface_stat@%p{"
				"list=list_head{...}, "
				"ifname=%s, "
				"total_dev={rx={bytes=%llu, "
				"packets=%llu}, "
				"tx={bytes=%llu, "
				"packets=%llu}}, "
				"total_skb={rx={bytes=%llu, "
				"packets=%llu}, "
				"tx={bytes=%llu, "
				"packets=%llu}}, "
				"last_known_valid=%d, "
				"last_known={rx={bytes=%llu, "
				"packets=%llu}, "
				"tx={bytes=%llu, "
				"packets=%llu}}, "
				"active=%d, "
				"net_dev=%p, "
				"proc_ptr=%p, "
				"tag_stat_tree=rb_root{...}}",
				is,
				is->ifname,
				is->totals_via_dev[IFS_RX].bytes,
				is->totals_via_dev[IFS_RX].packets,
				is->totals_via_dev[IFS_TX].bytes,
				is->totals_via_dev[IFS_TX].packets,
				is->totals_via_skb[IFS_RX].bytes,
				is->totals_via_skb[IFS_RX].packets,
				is->totals_via_skb[IFS_TX].bytes,
				is->totals_via_skb[IFS_TX].packets,
				is->last_known_valid,
				is->last_known[IFS_RX].bytes,
				is->last_known[IFS_RX].packets,
				is->last_known[IFS_TX].bytes,
				is->last_known[IFS_TX].packets,
				is->active,
				is->net_dev,
				is->proc_ptr);
	_bug_on_err_or_null(res);
	return res;
}

char *pp_sock_tag(struct sock_tag *st)
{
	char *tag_str;
	char *res;

	if (!st) {
		res = kasprintf(GFP_ATOMIC, "sock_tag@null{}");
		_bug_on_err_or_null(res);
		return res;
	}
	tag_str = pp_tag_t(&st->tag);
	res = kasprintf(GFP_ATOMIC, "sock_tag@%p{"
			"sock_node=rb_node{...}, "
			"sk=%p socket=%p (f_count=%lu), list=list_head{...}, "
			"pid=%u, tag=%s}",
			st, st->sk, st->socket, atomic_long_read(
				&st->socket->file->f_count),
			st->pid, tag_str);
	_bug_on_err_or_null(res);
	kfree(tag_str);
	return res;
}

char *pp_uid_tag_data(struct uid_tag_data *utd)
{
	char *res;

	if (!utd)
		res = kasprintf(GFP_ATOMIC, "uid_tag_data@null{}");
	else
		res = kasprintf(GFP_ATOMIC, "uid_tag_data@%p{"
				"uid=%u, num_active_acct_tags=%d, "
				"num_pqd=%d, "
				"tag_node_tree=rb_root{...}, "
				"proc_qtu_data_tree=rb_root{...}}",
				utd, utd->uid,
				utd->num_active_tags, utd->num_pqd);
	_bug_on_err_or_null(res);
	return res;
}

char *pp_proc_qtu_data(struct proc_qtu_data *pqd)
{
	char *parent_tag_data_str;
	char *res;

	if (!pqd) {
		res = kasprintf(GFP_ATOMIC, "proc_qtu_data@null{}");
		_bug_on_err_or_null(res);
		return res;
	}
	parent_tag_data_str = pp_uid_tag_data(pqd->parent_tag_data);
	res = kasprintf(GFP_ATOMIC, "proc_qtu_data@%p{"
			"node=rb_node{...}, pid=%u, "
			"parent_tag_data=%s, "
			"sock_tag_list=list_head{...}}",
			pqd, pqd->pid, parent_tag_data_str
		);
	_bug_on_err_or_null(res);
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

	if (!unlikely(qtaguid_debug_mask & DDEBUG_MASK))
		return;

	if (RB_EMPTY_ROOT(sock_tag_tree)) {
		str = "sock_tag_tree=rb_root{}";
		pr_debug("%*d: %s\n", indent_level*2, indent_level, str);
		return;
	}

	str = "sock_tag_tree=rb_root{";
	pr_debug("%*d: %s\n", indent_level*2, indent_level, str);
	indent_level++;
	for (node = rb_first(sock_tag_tree);
	     node;
	     node = rb_next(node)) {
		sock_tag_entry = rb_entry(node, struct sock_tag, sock_node);
		str = pp_sock_tag(sock_tag_entry);
		pr_debug("%*d: %s,\n", indent_level*2, indent_level, str);
		kfree(str);
	}
	indent_level--;
	str = "}";
	pr_debug("%*d: %s\n", indent_level*2, indent_level, str);
}

void prdebug_sock_tag_list(int indent_level,
			   struct list_head *sock_tag_list)
{
	struct sock_tag *sock_tag_entry;
	char *str;

	if (!unlikely(qtaguid_debug_mask & DDEBUG_MASK))
		return;

	if (list_empty(sock_tag_list)) {
		str = "sock_tag_list=list_head{}";
		pr_debug("%*d: %s\n", indent_level*2, indent_level, str);
		return;
	}

	str = "sock_tag_list=list_head{";
	pr_debug("%*d: %s\n", indent_level*2, indent_level, str);
	indent_level++;
	list_for_each_entry(sock_tag_entry, sock_tag_list, list) {
		str = pp_sock_tag(sock_tag_entry);
		pr_debug("%*d: %s,\n", indent_level*2, indent_level, str);
		kfree(str);
	}
	indent_level--;
	str = "}";
	pr_debug("%*d: %s\n", indent_level*2, indent_level, str);
}

void prdebug_proc_qtu_data_tree(int indent_level,
				struct rb_root *proc_qtu_data_tree)
{
	char *str;
	struct rb_node *node;
	struct proc_qtu_data *proc_qtu_data_entry;

	if (!unlikely(qtaguid_debug_mask & DDEBUG_MASK))
		return;

	if (RB_EMPTY_ROOT(proc_qtu_data_tree)) {
		str = "proc_qtu_data_tree=rb_root{}";
		pr_debug("%*d: %s\n", indent_level*2, indent_level, str);
		return;
	}

	str = "proc_qtu_data_tree=rb_root{";
	pr_debug("%*d: %s\n", indent_level*2, indent_level, str);
	indent_level++;
	for (node = rb_first(proc_qtu_data_tree);
	     node;
	     node = rb_next(node)) {
		proc_qtu_data_entry = rb_entry(node,
					       struct proc_qtu_data,
					       node);
		str = pp_proc_qtu_data(proc_qtu_data_entry);
		pr_debug("%*d: %s,\n", indent_level*2, indent_level,
			 str);
		kfree(str);
		indent_level++;
		prdebug_sock_tag_list(indent_level,
				      &proc_qtu_data_entry->sock_tag_list);
		indent_level--;

	}
	indent_level--;
	str = "}";
	pr_debug("%*d: %s\n", indent_level*2, indent_level, str);
}

void prdebug_tag_ref_tree(int indent_level, struct rb_root *tag_ref_tree)
{
	char *str;
	struct rb_node *node;
	struct tag_ref *tag_ref_entry;

	if (!unlikely(qtaguid_debug_mask & DDEBUG_MASK))
		return;

	if (RB_EMPTY_ROOT(tag_ref_tree)) {
		str = "tag_ref_tree{}";
		pr_debug("%*d: %s\n", indent_level*2, indent_level, str);
		return;
	}

	str = "tag_ref_tree{";
	pr_debug("%*d: %s\n", indent_level*2, indent_level, str);
	indent_level++;
	for (node = rb_first(tag_ref_tree);
	     node;
	     node = rb_next(node)) {
		tag_ref_entry = rb_entry(node,
					 struct tag_ref,
					 tn.node);
		str = pp_tag_ref(tag_ref_entry);
		pr_debug("%*d: %s,\n", indent_level*2, indent_level,
			 str);
		kfree(str);
	}
	indent_level--;
	str = "}";
	pr_debug("%*d: %s\n", indent_level*2, indent_level, str);
}

void prdebug_uid_tag_data_tree(int indent_level,
			       struct rb_root *uid_tag_data_tree)
{
	char *str;
	struct rb_node *node;
	struct uid_tag_data *uid_tag_data_entry;

	if (!unlikely(qtaguid_debug_mask & DDEBUG_MASK))
		return;

	if (RB_EMPTY_ROOT(uid_tag_data_tree)) {
		str = "uid_tag_data_tree=rb_root{}";
		pr_debug("%*d: %s\n", indent_level*2, indent_level, str);
		return;
	}

	str = "uid_tag_data_tree=rb_root{";
	pr_debug("%*d: %s\n", indent_level*2, indent_level, str);
	indent_level++;
	for (node = rb_first(uid_tag_data_tree);
	     node;
	     node = rb_next(node)) {
		uid_tag_data_entry = rb_entry(node, struct uid_tag_data,
					      node);
		str = pp_uid_tag_data(uid_tag_data_entry);
		pr_debug("%*d: %s,\n", indent_level*2, indent_level, str);
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
	pr_debug("%*d: %s\n", indent_level*2, indent_level, str);
}

void prdebug_tag_stat_tree(int indent_level,
				  struct rb_root *tag_stat_tree)
{
	char *str;
	struct rb_node *node;
	struct tag_stat *ts_entry;

	if (!unlikely(qtaguid_debug_mask & DDEBUG_MASK))
		return;

	if (RB_EMPTY_ROOT(tag_stat_tree)) {
		str = "tag_stat_tree{}";
		pr_debug("%*d: %s\n", indent_level*2, indent_level, str);
		return;
	}

	str = "tag_stat_tree{";
	pr_debug("%*d: %s\n", indent_level*2, indent_level, str);
	indent_level++;
	for (node = rb_first(tag_stat_tree);
	     node;
	     node = rb_next(node)) {
		ts_entry = rb_entry(node, struct tag_stat, tn.node);
		str = pp_tag_stat(ts_entry);
		pr_debug("%*d: %s\n", indent_level*2, indent_level,
			 str);
		kfree(str);
	}
	indent_level--;
	str = "}";
	pr_debug("%*d: %s\n", indent_level*2, indent_level, str);
}

void prdebug_iface_stat_list(int indent_level,
			     struct list_head *iface_stat_list)
{
	char *str;
	struct iface_stat *iface_entry;

	if (!unlikely(qtaguid_debug_mask & DDEBUG_MASK))
		return;

	if (list_empty(iface_stat_list)) {
		str = "iface_stat_list=list_head{}";
		pr_debug("%*d: %s\n", indent_level*2, indent_level, str);
		return;
	}

	str = "iface_stat_list=list_head{";
	pr_debug("%*d: %s\n", indent_level*2, indent_level, str);
	indent_level++;
	list_for_each_entry(iface_entry, iface_stat_list, list) {
		str = pp_iface_stat(iface_entry);
		pr_debug("%*d: %s\n", indent_level*2, indent_level, str);
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
	pr_debug("%*d: %s\n", indent_level*2, indent_level, str);
}

#endif  /* ifdef DDEBUG */
/*------------------------------------------*/
static const char * const netdev_event_strings[] = {
	"netdev_unknown",
	"NETDEV_UP",
	"NETDEV_DOWN",
	"NETDEV_REBOOT",
	"NETDEV_CHANGE",
	"NETDEV_REGISTER",
	"NETDEV_UNREGISTER",
	"NETDEV_CHANGEMTU",
	"NETDEV_CHANGEADDR",
	"NETDEV_GOING_DOWN",
	"NETDEV_CHANGENAME",
	"NETDEV_FEAT_CHANGE",
	"NETDEV_BONDING_FAILOVER",
	"NETDEV_PRE_UP",
	"NETDEV_PRE_TYPE_CHANGE",
	"NETDEV_POST_TYPE_CHANGE",
	"NETDEV_POST_INIT",
	"NETDEV_UNREGISTER_BATCH",
	"NETDEV_RELEASE",
	"NETDEV_NOTIFY_PEERS",
	"NETDEV_JOIN",
};

const char *netdev_evt_str(int netdev_event)
{
	if (netdev_event < 0
	    || netdev_event >= ARRAY_SIZE(netdev_event_strings))
		return "bad event num";
	return netdev_event_strings[netdev_event];
}
