// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *	X.25 Packet Layer release 002
 *
 *	This is ALPHA test software. This code may break your machine,
 *	randomly fail to work with new releases, misbehave and/or generally
 *	screw up. It might even work.
 *
 *	This code REQUIRES 2.4 with seq_file support
 *
 *	History
 *	2002/10/06	Arnaldo Carvalho de Melo  seq_file support
 */

#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/export.h>
#include <net/net_namespace.h>
#include <net/sock.h>
#include <net/x25.h>

#ifdef CONFIG_PROC_FS

static void *x25_seq_route_start(struct seq_file *seq, loff_t *pos)
	__acquires(x25_route_list_lock)
{
	read_lock_bh(&x25_route_list_lock);
	return seq_list_start_head(&x25_route_list, *pos);
}

static void *x25_seq_route_next(struct seq_file *seq, void *v, loff_t *pos)
{
	return seq_list_next(v, &x25_route_list, pos);
}

static void x25_seq_route_stop(struct seq_file *seq, void *v)
	__releases(x25_route_list_lock)
{
	read_unlock_bh(&x25_route_list_lock);
}

static int x25_seq_route_show(struct seq_file *seq, void *v)
{
	struct x25_route *rt = list_entry(v, struct x25_route, node);

	if (v == &x25_route_list) {
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

static void *x25_seq_socket_start(struct seq_file *seq, loff_t *pos)
	__acquires(x25_list_lock)
{
	read_lock_bh(&x25_list_lock);
	return seq_hlist_start_head(&x25_list, *pos);
}

static void *x25_seq_socket_next(struct seq_file *seq, void *v, loff_t *pos)
{
	return seq_hlist_next(v, &x25_list, pos);
}

static void x25_seq_socket_stop(struct seq_file *seq, void *v)
	__releases(x25_list_lock)
{
	read_unlock_bh(&x25_list_lock);
}

static int x25_seq_socket_show(struct seq_file *seq, void *v)
{
	struct sock *s;
	struct x25_sock *x25;
	const char *devname;

	if (v == SEQ_START_TOKEN) {
		seq_printf(seq, "dest_addr  src_addr   dev   lci st vs vr "
				"va   t  t2 t21 t22 t23 Snd-Q Rcv-Q inode\n");
		goto out;
	}

	s = sk_entry(v);
	x25 = x25_sk(s);

	if (!x25->neighbour || !x25->neighbour->dev)
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
		   sk_wmem_alloc_get(s),
		   sk_rmem_alloc_get(s),
		   s->sk_socket ? SOCK_INODE(s->sk_socket)->i_ino : 0L);
out:
	return 0;
}

static void *x25_seq_forward_start(struct seq_file *seq, loff_t *pos)
	__acquires(x25_forward_list_lock)
{
	read_lock_bh(&x25_forward_list_lock);
	return seq_list_start_head(&x25_forward_list, *pos);
}

static void *x25_seq_forward_next(struct seq_file *seq, void *v, loff_t *pos)
{
	return seq_list_next(v, &x25_forward_list, pos);
}

static void x25_seq_forward_stop(struct seq_file *seq, void *v)
	__releases(x25_forward_list_lock)
{
	read_unlock_bh(&x25_forward_list_lock);
}

static int x25_seq_forward_show(struct seq_file *seq, void *v)
{
	struct x25_forward *f = list_entry(v, struct x25_forward, node);

	if (v == &x25_forward_list) {
		seq_printf(seq, "lci dev1       dev2\n");
		goto out;
	}

	f = v;

	seq_printf(seq, "%d %-10s %-10s\n",
			f->lci, f->dev1->name, f->dev2->name);
out:
	return 0;
}

static const struct seq_operations x25_seq_route_ops = {
	.start  = x25_seq_route_start,
	.next   = x25_seq_route_next,
	.stop   = x25_seq_route_stop,
	.show   = x25_seq_route_show,
};

static const struct seq_operations x25_seq_socket_ops = {
	.start  = x25_seq_socket_start,
	.next   = x25_seq_socket_next,
	.stop   = x25_seq_socket_stop,
	.show   = x25_seq_socket_show,
};

static const struct seq_operations x25_seq_forward_ops = {
	.start  = x25_seq_forward_start,
	.next   = x25_seq_forward_next,
	.stop   = x25_seq_forward_stop,
	.show   = x25_seq_forward_show,
};

int __init x25_proc_init(void)
{
	if (!proc_mkdir("x25", init_net.proc_net))
		return -ENOMEM;

	if (!proc_create_seq("x25/route", 0444, init_net.proc_net,
			 &x25_seq_route_ops))
		goto out;

	if (!proc_create_seq("x25/socket", 0444, init_net.proc_net,
			 &x25_seq_socket_ops))
		goto out;

	if (!proc_create_seq("x25/forward", 0444, init_net.proc_net,
			 &x25_seq_forward_ops))
		goto out;
	return 0;

out:
	remove_proc_subtree("x25", init_net.proc_net);
	return -ENOMEM;
}

void __exit x25_proc_exit(void)
{
	remove_proc_subtree("x25", init_net.proc_net);
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
