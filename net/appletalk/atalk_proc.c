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

static __inline__ struct sock *atalk_get_socket_idx(loff_t pos)
{
	struct sock *s;
	struct hlist_node *node;

	sk_for_each(s, node, &atalk_sockets)
		if (!pos--)
			goto found;
	s = NULL;
found:
	return s;
}

static void *atalk_seq_socket_start(struct seq_file *seq, loff_t *pos)
	__acquires(atalk_sockets_lock)
{
	loff_t l = *pos;

	read_lock_bh(&atalk_sockets_lock);
	return l ? atalk_get_socket_idx(--l) : SEQ_START_TOKEN;
}

static void *atalk_seq_socket_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct sock *i;

	++*pos;
	if (v == SEQ_START_TOKEN) {
		i = sk_head(&atalk_sockets);
		goto out;
	}
	i = sk_next(v);
out:
	return i;
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

	s = v;
	at = at_sk(s);

	seq_printf(seq, "%02X   %04X:%02X:%02X  %04X:%02X:%02X  %08X:%08X "
			"%02X %d\n",
		   s->sk_type, ntohs(at->src_net), at->src_node, at->src_port,
		   ntohs(at->dest_net), at->dest_node, at->dest_port,
		   atomic_read(&s->sk_wmem_alloc),
		   atomic_read(&s->sk_rmem_alloc),
		   s->sk_state, SOCK_INODE(s->sk_socket)->i_uid);
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

static int atalk_seq_interface_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &atalk_seq_interface_ops);
}

static int atalk_seq_route_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &atalk_seq_route_ops);
}

static int atalk_seq_socket_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &atalk_seq_socket_ops);
}

static const struct file_operations atalk_seq_interface_fops = {
	.owner		= THIS_MODULE,
	.open		= atalk_seq_interface_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

static const struct file_operations atalk_seq_route_fops = {
	.owner		= THIS_MODULE,
	.open		= atalk_seq_route_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

static const struct file_operations atalk_seq_socket_fops = {
	.owner		= THIS_MODULE,
	.open		= atalk_seq_socket_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

static struct proc_dir_entry *atalk_proc_dir;

int __init atalk_proc_init(void)
{
	struct proc_dir_entry *p;
	int rc = -ENOMEM;

	atalk_proc_dir = proc_mkdir("atalk", init_net.proc_net);
	if (!atalk_proc_dir)
		goto out;

	p = proc_create("interface", S_IRUGO, atalk_proc_dir,
			&atalk_seq_interface_fops);
	if (!p)
		goto out_interface;

	p = proc_create("route", S_IRUGO, atalk_proc_dir,
			&atalk_seq_route_fops);
	if (!p)
		goto out_route;

	p = proc_create("socket", S_IRUGO, atalk_proc_dir,
			&atalk_seq_socket_fops);
	if (!p)
		goto out_socket;

	p = proc_create("arp", S_IRUGO, atalk_proc_dir, &atalk_seq_arp_fops);
	if (!p)
		goto out_arp;

	rc = 0;
out:
	return rc;
out_arp:
	remove_proc_entry("socket", atalk_proc_dir);
out_socket:
	remove_proc_entry("route", atalk_proc_dir);
out_route:
	remove_proc_entry("interface", atalk_proc_dir);
out_interface:
	remove_proc_entry("atalk", init_net.proc_net);
	goto out;
}

void __exit atalk_proc_exit(void)
{
	remove_proc_entry("interface", atalk_proc_dir);
	remove_proc_entry("route", atalk_proc_dir);
	remove_proc_entry("socket", atalk_proc_dir);
	remove_proc_entry("arp", atalk_proc_dir);
	remove_proc_entry("atalk", init_net.proc_net);
}
