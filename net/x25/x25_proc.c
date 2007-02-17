/*
 *	X.25 Packet Layer release 002
 *
 *	This is ALPHA test software. This code may break your machine,
 *	randomly fail to work with new releases, misbehave and/or generally
 *	screw up. It might even work.
 *
 *	This code REQUIRES 2.4 with seq_file support
 *
 *	This module:
 *		This module is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 *	History
 *	2002/10/06	Arnaldo Carvalho de Melo  seq_file support
 */

#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <net/sock.h>
#include <net/x25.h>

#ifdef CONFIG_PROC_FS
static __inline__ struct x25_route *x25_get_route_idx(loff_t pos)
{
	struct list_head *route_entry;
	struct x25_route *rt = NULL;

	list_for_each(route_entry, &x25_route_list) {
		rt = list_entry(route_entry, struct x25_route, node);
		if (!pos--)
			goto found;
	}
	rt = NULL;
found:
	return rt;
}

static void *x25_seq_route_start(struct seq_file *seq, loff_t *pos)
{
	loff_t l = *pos;

	read_lock_bh(&x25_route_list_lock);
	return l ? x25_get_route_idx(--l) : SEQ_START_TOKEN;
}

static void *x25_seq_route_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct x25_route *rt;

	++*pos;
	if (v == SEQ_START_TOKEN) {
		rt = NULL;
		if (!list_empty(&x25_route_list))
			rt = list_entry(x25_route_list.next,
					struct x25_route, node);
		goto out;
	}
	rt = v;
	if (rt->node.next != &x25_route_list)
		rt = list_entry(rt->node.next, struct x25_route, node);
	else
		rt = NULL;
out:
	return rt;
}

static void x25_seq_route_stop(struct seq_file *seq, void *v)
{
	read_unlock_bh(&x25_route_list_lock);
}

static int x25_seq_route_show(struct seq_file *seq, void *v)
{
	struct x25_route *rt;

	if (v == SEQ_START_TOKEN) {
		seq_puts(seq, "Address          Digits  Device\n");
		goto out;
	}

	rt = v;
	seq_printf(seq, "%-15s  %-6d  %-5s\n",
		   rt->address.x25_addr, rt->sigdigits,
		   rt->dev ? rt->dev->name : "???");
out:
	return 0;
}

static __inline__ struct sock *x25_get_socket_idx(loff_t pos)
{
	struct sock *s;
	struct hlist_node *node;

	sk_for_each(s, node, &x25_list)
		if (!pos--)
			goto found;
	s = NULL;
found:
	return s;
}

static void *x25_seq_socket_start(struct seq_file *seq, loff_t *pos)
{
	loff_t l = *pos;

	read_lock_bh(&x25_list_lock);
	return l ? x25_get_socket_idx(--l) : SEQ_START_TOKEN;
}

static void *x25_seq_socket_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct sock *s;

	++*pos;
	if (v == SEQ_START_TOKEN) {
		s = sk_head(&x25_list);
		goto out;
	}
	s = sk_next(v);
out:
	return s;
}

static void x25_seq_socket_stop(struct seq_file *seq, void *v)
{
	read_unlock_bh(&x25_list_lock);
}

static int x25_seq_socket_show(struct seq_file *seq, void *v)
{
	struct sock *s;
	struct x25_sock *x25;
	struct net_device *dev;
	const char *devname;

	if (v == SEQ_START_TOKEN) {
		seq_printf(seq, "dest_addr  src_addr   dev   lci st vs vr "
				"va   t  t2 t21 t22 t23 Snd-Q Rcv-Q inode\n");
		goto out;
	}

	s = v;
	x25 = x25_sk(s);

	if (!x25->neighbour || (dev = x25->neighbour->dev) == NULL)
		devname = "???";
	else
		devname = x25->neighbour->dev->name;

	seq_printf(seq, "%-10s %-10s %-5s %3.3X  %d  %d  %d  %d %3lu %3lu "
			"%3lu %3lu %3lu %5d %5d %ld\n",
		   !x25->dest_addr.x25_addr[0] ? "*" : x25->dest_addr.x25_addr,
		   !x25->source_addr.x25_addr[0] ? "*" : x25->source_addr.x25_addr,
		   devname, x25->lci & 0x0FFF, x25->state, x25->vs, x25->vr,
		   x25->va, x25_display_timer(s) / HZ, x25->t2  / HZ,
		   x25->t21 / HZ, x25->t22 / HZ, x25->t23 / HZ,
		   atomic_read(&s->sk_wmem_alloc),
		   atomic_read(&s->sk_rmem_alloc),
		   s->sk_socket ? SOCK_INODE(s->sk_socket)->i_ino : 0L);
out:
	return 0;
}

static __inline__ struct x25_forward *x25_get_forward_idx(loff_t pos)
{
	struct x25_forward *f;
	struct list_head *entry;

	list_for_each(entry, &x25_forward_list) {
		f = list_entry(entry, struct x25_forward, node);
		if (!pos--)
			goto found;
	}

	f = NULL;
found:
	return f;
}

static void *x25_seq_forward_start(struct seq_file *seq, loff_t *pos)
{
	loff_t l = *pos;

	read_lock_bh(&x25_forward_list_lock);
	return l ? x25_get_forward_idx(--l) : SEQ_START_TOKEN;
}

static void *x25_seq_forward_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct x25_forward *f;

	++*pos;
	if (v == SEQ_START_TOKEN) {
		f = NULL;
		if (!list_empty(&x25_forward_list))
			f = list_entry(x25_forward_list.next,
					struct x25_forward, node);
		goto out;
	}
	f = v;
	if (f->node.next != &x25_forward_list)
		f = list_entry(f->node.next, struct x25_forward, node);
	else
		f = NULL;
out:
	return f;

}

static void x25_seq_forward_stop(struct seq_file *seq, void *v)
{
	read_unlock_bh(&x25_forward_list_lock);
}

static int x25_seq_forward_show(struct seq_file *seq, void *v)
{
	struct x25_forward *f;

	if (v == SEQ_START_TOKEN) {
		seq_printf(seq, "lci dev1       dev2\n");
		goto out;
	}

	f = v;

	seq_printf(seq, "%d %-10s %-10s\n",
			f->lci, f->dev1->name, f->dev2->name);

out:
	return 0;
}

static struct seq_operations x25_seq_route_ops = {
	.start  = x25_seq_route_start,
	.next   = x25_seq_route_next,
	.stop   = x25_seq_route_stop,
	.show   = x25_seq_route_show,
};

static struct seq_operations x25_seq_socket_ops = {
	.start  = x25_seq_socket_start,
	.next   = x25_seq_socket_next,
	.stop   = x25_seq_socket_stop,
	.show   = x25_seq_socket_show,
};

static struct seq_operations x25_seq_forward_ops = {
	.start  = x25_seq_forward_start,
	.next   = x25_seq_forward_next,
	.stop   = x25_seq_forward_stop,
	.show   = x25_seq_forward_show,
};

static int x25_seq_socket_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &x25_seq_socket_ops);
}

static int x25_seq_route_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &x25_seq_route_ops);
}

static int x25_seq_forward_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &x25_seq_forward_ops);
}

static const struct file_operations x25_seq_socket_fops = {
	.owner		= THIS_MODULE,
	.open		= x25_seq_socket_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

static const struct file_operations x25_seq_route_fops = {
	.owner		= THIS_MODULE,
	.open		= x25_seq_route_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

static struct file_operations x25_seq_forward_fops = {
	.owner		= THIS_MODULE,
	.open		= x25_seq_forward_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

static struct proc_dir_entry *x25_proc_dir;

int __init x25_proc_init(void)
{
	struct proc_dir_entry *p;
	int rc = -ENOMEM;

	x25_proc_dir = proc_mkdir("x25", proc_net);
	if (!x25_proc_dir)
		goto out;

	p = create_proc_entry("route", S_IRUGO, x25_proc_dir);
	if (!p)
		goto out_route;
	p->proc_fops = &x25_seq_route_fops;

	p = create_proc_entry("socket", S_IRUGO, x25_proc_dir);
	if (!p)
		goto out_socket;
	p->proc_fops = &x25_seq_socket_fops;

	p = create_proc_entry("forward", S_IRUGO, x25_proc_dir);
	if (!p)
		goto out_forward;
	p->proc_fops = &x25_seq_forward_fops;
	rc = 0;

out:
	return rc;
out_forward:
	remove_proc_entry("socket", x25_proc_dir);
out_socket:
	remove_proc_entry("route", x25_proc_dir);
out_route:
	remove_proc_entry("x25", proc_net);
	goto out;
}

void __exit x25_proc_exit(void)
{
	remove_proc_entry("forward", x25_proc_dir);
	remove_proc_entry("route", x25_proc_dir);
	remove_proc_entry("socket", x25_proc_dir);
	remove_proc_entry("x25", proc_net);
}

#else /* CONFIG_PROC_FS */

int __init x25_proc_init(void)
{
	return 0;
}

void __exit x25_proc_exit(void)
{
}
#endif /* CONFIG_PROC_FS */
