/*
 * 	atalk_proc.c - proc support for Appletalk
 *
 * 	Copyright(c) Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 *
 *	This program is free software; you can redistribute it and/or modify it
 *	under the terms of the GNU General Public License as published by the
 *	Free Software Foundation, version 2.
 */

#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <net/net_namespace.h>
#include <net/sock.h>
#include <linux/atalk.h>
#include <linux/export.h>


static __inline__ struct atalk_iface *atalk_get_interface_idx(loff_t pos)
{
	struct atalk_iface *i;

	for (i = atalk_interfaces; pos && i; i = i->next)
		--pos;

	return i;
}

static void *atalk_seq_interface_start(struct seq_file *seq, loff_t *pos)
	__acquires(atalk_interfaces_lock)
{
	loff_t l = *pos;

	read_lock_bh(&atalk_interfaces_lock);
	return l ? atalk_get_interface_idx(--l) : SEQ_START_TOKEN;
}

static void *atalk_seq_interface_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct atalk_iface *i;

	++*pos;
	if (v == SEQ_START_TOKEN) {
		i = NULL;
		if (atalk_interfaces)
			i = atalk_interfaces;
		goto out;
	}
	i = v;
	i = i->next;
out:
	return i;
}

static void atalk_seq_interface_stop(struct seq_file *seq, void *v)
	__releases(atalk_interfaces_lock)
{
	read_unlock_bh(&atalk_interfaces_lock);
}

static int atalk_seq_interface_show(struct seq_file *seq, void *v)
{
	struct atalk_iface *iface;

	if (v == SEQ_START_TOKEN) {
		seq_puts(seq, "Interface        Address   Networks  "
			      "Status\n");
		goto out;
	}

	iface = v;
	seq_printf(seq, "%-16s %04X:%02X  %04X-%04X  %d\n",
		   iface->dev->name, ntohs(iface->address.s_net),
		   iface->address.s_node, ntohs(iface->nets.nr_firstnet),
		   ntohs(iface->nets.nr_lastnet), iface->status);
out:
	return 0;
}

static __inline__ struct atalk_route *atalk_get_route_idx(loff_t pos)
{
	struct atalk_route *r;

	for (r = atalk_routes; pos && r; r = r->next)
		--pos;

	return r;
}

static void *atalk_seq_route_start(struct seq_file *seq, loff_t *pos)
	__acquires(atalk_routes_lock)
{
	loff_t l = *pos;

	read_lock_bh(&atalk_routes_lock);
	return l ? atalk_get_route_idx(--l) : SEQ_START_TOKEN;
}

static void *atalk_seq_route_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct atalk_route *r;

	++*pos;
	if (v == SEQ_START_TOKEN) {
		r = NULL;
		if (atalk_routes)
			r = atalk_routes;
		goto out;
	}
	r = v;
	r = r->next;
out:
	return r;
}

static void atalk_seq_route_stop(struct seq_file *seq, void *v)
	__releases(atalk_routes_lock)
{
	read_unlock_bh(&atalk_routes_lock);
}

static int atalk_seq_route_show(struct seq_file *seq, void *v)
{
	struct atalk_route *rt;

	if (v == SEQ_START_TOKEN) {
		seq_puts(seq, "Target        Router  Flags Dev\n");
		goto out;
	}

	if (atrtr_default.dev) {
		rt = &atrtr_default;
		seq_printf(seq, "Default     %04X:%02X  %-4d  %s\n",
			       ntohs(rt->gateway.s_net), rt->gateway.s_node,
			       rt->flags, rt->dev->name);
	}

	rt = v;
	seq_printf(seq, "%04X:%02X     %04X:%02X  %-4d  %s\n",
		   ntohs(rt->target.s_net), rt->target.s_node,
		   ntohs(rt->gateway.s_net), rt->gateway.s_node,
		   rt->flags, rt->dev->name);
out:
	return 0;
}

static void *atalk_seq_socket_start(struct seq_file *seq, loff_t *pos)
	__acquires(atalk_sockets_lock)
{
	read_lock_bh(&atalk_sockets_lock);
	return seq_hlist_start_head(&atalk_sockets, *pos);
}

static void *atalk_seq_socket_next(struct seq_file *seq, void *v, loff_t *pos)
{
	return seq_hlist_next(v, &atalk_sockets, pos);
}

static void atalk_seq_socket_stop(struct seq_file *seq, void *v)
	__releases(atalk_sockets_lock)
{
	read_unlock_bh(&atalk_sockets_lock);
}

static int atalk_seq_socket_show(struct seq_file *seq, void *v)
{
	struct sock *s;
	struct atalk_sock *at;

	if (v == SEQ_START_TOKEN) {
		seq_printf(seq, "Type Local_addr  Remote_addr Tx_queue "
				"Rx_queue St UID\n");
		goto out;
	}

	s = sk_entry(v);
	at = at_sk(s);

	seq_printf(seq, "%02X   %04X:%02X:%02X  %04X:%02X:%02X  %08X:%08X "
			"%02X %u\n",
		   s->sk_type, ntohs(at->src_net), at->src_node, at->src_port,
		   ntohs(at->dest_net), at->dest_node, at->dest_port,
		   sk_wmem_alloc_get(s),
		   sk_rmem_alloc_get(s),
		   s->sk_state,
		   from_kuid_munged(seq_user_ns(seq), sock_i_uid(s)));
out:
	return 0;
}

static const struct seq_operations atalk_seq_interface_ops = {
	.start  = atalk_seq_interface_start,
	.next   = atalk_seq_interface_next,
	.stop   = atalk_seq_interface_stop,
	.show   = atalk_seq_interface_show,
};

static const struct seq_operations atalk_seq_route_ops = {
	.start  = atalk_seq_route_start,
	.next   = atalk_seq_route_next,
	.stop   = atalk_seq_route_stop,
	.show   = atalk_seq_route_show,
};

static const struct seq_operations atalk_seq_socket_ops = {
	.start  = atalk_seq_socket_start,
	.next   = atalk_seq_socket_next,
	.stop   = atalk_seq_socket_stop,
	.show   = atalk_seq_socket_show,
};

int __init atalk_proc_init(void)
{
	if (!proc_mkdir("atalk", init_net.proc_net))
		return -ENOMEM;

	if (!proc_create_seq("atalk/interface", 0444, init_net.proc_net,
			    &atalk_seq_interface_ops))
		goto out;

	if (!proc_create_seq("atalk/route", 0444, init_net.proc_net,
			    &atalk_seq_route_ops))
		goto out;

	if (!proc_create_seq("atalk/socket", 0444, init_net.proc_net,
			    &atalk_seq_socket_ops))
		goto out;

	if (!proc_create_seq_private("atalk/arp", 0444, init_net.proc_net,
				     &aarp_seq_ops,
				     sizeof(struct aarp_iter_state), NULL))
		goto out;

out:
	remove_proc_subtree("atalk", init_net.proc_net);
	return -ENOMEM;
}

void atalk_proc_exit(void)
{
	remove_proc_subtree("atalk", init_net.proc_net);
}
