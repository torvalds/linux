/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * netdebug.c
 *
 * debug functionality for o2net
 *
 * Copyright (C) 2005, 2008 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 *
 */

#ifdef CONFIG_DEBUG_FS

#include <linux/module.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/idr.h>
#include <linux/kref.h>
#include <linux/seq_file.h>
#include <linux/debugfs.h>

#include <linux/uaccess.h>

#include "tcp.h"
#include "nodemanager.h"
#define MLOG_MASK_PREFIX ML_TCP
#include "masklog.h"

#include "tcp_internal.h"

#define O2NET_DEBUG_DIR		"o2net"
#define SC_DEBUG_NAME		"sock_containers"
#define NST_DEBUG_NAME		"send_tracking"
#define STATS_DEBUG_NAME	"stats"
#define NODES_DEBUG_NAME	"connected_nodes"

#define SHOW_SOCK_CONTAINERS	0
#define SHOW_SOCK_STATS		1

static struct dentry *o2net_dentry;
static struct dentry *sc_dentry;
static struct dentry *nst_dentry;
static struct dentry *stats_dentry;
static struct dentry *nodes_dentry;

static DEFINE_SPINLOCK(o2net_debug_lock);

static LIST_HEAD(sock_containers);
static LIST_HEAD(send_tracking);

void o2net_debug_add_nst(struct o2net_send_tracking *nst)
{
	spin_lock(&o2net_debug_lock);
	list_add(&nst->st_net_debug_item, &send_tracking);
	spin_unlock(&o2net_debug_lock);
}

void o2net_debug_del_nst(struct o2net_send_tracking *nst)
{
	spin_lock(&o2net_debug_lock);
	if (!list_empty(&nst->st_net_debug_item))
		list_del_init(&nst->st_net_debug_item);
	spin_unlock(&o2net_debug_lock);
}

static struct o2net_send_tracking
			*next_nst(struct o2net_send_tracking *nst_start)
{
	struct o2net_send_tracking *nst, *ret = NULL;

	assert_spin_locked(&o2net_debug_lock);

	list_for_each_entry(nst, &nst_start->st_net_debug_item,
			    st_net_debug_item) {
		/* discover the head of the list */
		if (&nst->st_net_debug_item == &send_tracking)
			break;

		/* use st_task to detect real nsts in the list */
		if (nst->st_task != NULL) {
			ret = nst;
			break;
		}
	}

	return ret;
}

static void *nst_seq_start(struct seq_file *seq, loff_t *pos)
{
	struct o2net_send_tracking *nst, *dummy_nst = seq->private;

	spin_lock(&o2net_debug_lock);
	nst = next_nst(dummy_nst);
	spin_unlock(&o2net_debug_lock);

	return nst;
}

static void *nst_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct o2net_send_tracking *nst, *dummy_nst = seq->private;

	spin_lock(&o2net_debug_lock);
	nst = next_nst(dummy_nst);
	list_del_init(&dummy_nst->st_net_debug_item);
	if (nst)
		list_add(&dummy_nst->st_net_debug_item,
			 &nst->st_net_debug_item);
	spin_unlock(&o2net_debug_lock);

	return nst; /* unused, just needs to be null when done */
}

static int nst_seq_show(struct seq_file *seq, void *v)
{
	struct o2net_send_tracking *nst, *dummy_nst = seq->private;
	ktime_t now;
	s64 sock, send, status;

	spin_lock(&o2net_debug_lock);
	nst = next_nst(dummy_nst);
	if (!nst)
		goto out;

	now = ktime_get();
	sock = ktime_to_us(ktime_sub(now, nst->st_sock_time));
	send = ktime_to_us(ktime_sub(now, nst->st_send_time));
	status = ktime_to_us(ktime_sub(now, nst->st_status_time));

	/* get_task_comm isn't exported.  oh well. */
	seq_printf(seq, "%p:\n"
		   "  pid:          %lu\n"
		   "  tgid:         %lu\n"
		   "  process name: %s\n"
		   "  node:         %u\n"
		   "  sc:           %p\n"
		   "  message id:   %d\n"
		   "  message type: %u\n"
		   "  message key:  0x%08x\n"
		   "  sock acquiry: %lld usecs ago\n"
		   "  send start:   %lld usecs ago\n"
		   "  wait start:   %lld usecs ago\n",
		   nst, (unsigned long)task_pid_nr(nst->st_task),
		   (unsigned long)nst->st_task->tgid,
		   nst->st_task->comm, nst->st_node,
		   nst->st_sc, nst->st_id, nst->st_msg_type,
		   nst->st_msg_key,
		   (long long)sock,
		   (long long)send,
		   (long long)status);

out:
	spin_unlock(&o2net_debug_lock);

	return 0;
}

static void nst_seq_stop(struct seq_file *seq, void *v)
{
}

static const struct seq_operations nst_seq_ops = {
	.start = nst_seq_start,
	.next = nst_seq_next,
	.stop = nst_seq_stop,
	.show = nst_seq_show,
};

static int nst_fop_open(struct inode *inode, struct file *file)
{
	struct o2net_send_tracking *dummy_nst;

	dummy_nst = __seq_open_private(file, &nst_seq_ops, sizeof(*dummy_nst));
	if (!dummy_nst)
		return -ENOMEM;
	o2net_debug_add_nst(dummy_nst);

	return 0;
}

static int nst_fop_release(struct inode *inode, struct file *file)
{
	struct seq_file *seq = file->private_data;
	struct o2net_send_tracking *dummy_nst = seq->private;

	o2net_debug_del_nst(dummy_nst);
	return seq_release_private(inode, file);
}

static const struct file_operations nst_seq_fops = {
	.open = nst_fop_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = nst_fop_release,
};

void o2net_debug_add_sc(struct o2net_sock_container *sc)
{
	spin_lock(&o2net_debug_lock);
	list_add(&sc->sc_net_debug_item, &sock_containers);
	spin_unlock(&o2net_debug_lock);
}

void o2net_debug_del_sc(struct o2net_sock_container *sc)
{
	spin_lock(&o2net_debug_lock);
	list_del_init(&sc->sc_net_debug_item);
	spin_unlock(&o2net_debug_lock);
}

struct o2net_sock_debug {
	int dbg_ctxt;
	struct o2net_sock_container *dbg_sock;
};

static struct o2net_sock_container
			*next_sc(struct o2net_sock_container *sc_start)
{
	struct o2net_sock_container *sc, *ret = NULL;

	assert_spin_locked(&o2net_debug_lock);

	list_for_each_entry(sc, &sc_start->sc_net_debug_item,
			    sc_net_debug_item) {
		/* discover the head of the list miscast as a sc */
		if (&sc->sc_net_debug_item == &sock_containers)
			break;

		/* use sc_page to detect real scs in the list */
		if (sc->sc_page != NULL) {
			ret = sc;
			break;
		}
	}

	return ret;
}

static void *sc_seq_start(struct seq_file *seq, loff_t *pos)
{
	struct o2net_sock_debug *sd = seq->private;
	struct o2net_sock_container *sc, *dummy_sc = sd->dbg_sock;

	spin_lock(&o2net_debug_lock);
	sc = next_sc(dummy_sc);
	spin_unlock(&o2net_debug_lock);

	return sc;
}

static void *sc_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct o2net_sock_debug *sd = seq->private;
	struct o2net_sock_container *sc, *dummy_sc = sd->dbg_sock;

	spin_lock(&o2net_debug_lock);
	sc = next_sc(dummy_sc);
	list_del_init(&dummy_sc->sc_net_debug_item);
	if (sc)
		list_add(&dummy_sc->sc_net_debug_item, &sc->sc_net_debug_item);
	spin_unlock(&o2net_debug_lock);

	return sc; /* unused, just needs to be null when done */
}

#ifdef CONFIG_OCFS2_FS_STATS
# define sc_send_count(_s)		((_s)->sc_send_count)
# define sc_recv_count(_s)		((_s)->sc_recv_count)
# define sc_tv_acquiry_total_ns(_s)	(ktime_to_ns((_s)->sc_tv_acquiry_total))
# define sc_tv_send_total_ns(_s)	(ktime_to_ns((_s)->sc_tv_send_total))
# define sc_tv_status_total_ns(_s)	(ktime_to_ns((_s)->sc_tv_status_total))
# define sc_tv_process_total_ns(_s)	(ktime_to_ns((_s)->sc_tv_process_total))
#else
# define sc_send_count(_s)		(0U)
# define sc_recv_count(_s)		(0U)
# define sc_tv_acquiry_total_ns(_s)	(0LL)
# define sc_tv_send_total_ns(_s)	(0LL)
# define sc_tv_status_total_ns(_s)	(0LL)
# define sc_tv_process_total_ns(_s)	(0LL)
#endif

/* So that debugfs.ocfs2 can determine which format is being used */
#define O2NET_STATS_STR_VERSION		1
static void sc_show_sock_stats(struct seq_file *seq,
			       struct o2net_sock_container *sc)
{
	if (!sc)
		return;

	seq_printf(seq, "%d,%u,%lu,%lld,%lld,%lld,%lu,%lld\n", O2NET_STATS_STR_VERSION,
		   sc->sc_node->nd_num, (unsigned long)sc_send_count(sc),
		   (long long)sc_tv_acquiry_total_ns(sc),
		   (long long)sc_tv_send_total_ns(sc),
		   (long long)sc_tv_status_total_ns(sc),
		   (unsigned long)sc_recv_count(sc),
		   (long long)sc_tv_process_total_ns(sc));
}

static void sc_show_sock_container(struct seq_file *seq,
				   struct o2net_sock_container *sc)
{
	struct inet_sock *inet = NULL;
	__be32 saddr = 0, daddr = 0;
	__be16 sport = 0, dport = 0;

	if (!sc)
		return;

	if (sc->sc_sock) {
		inet = inet_sk(sc->sc_sock->sk);
		/* the stack's structs aren't sparse endian clean */
		saddr = (__force __be32)inet->inet_saddr;
		daddr = (__force __be32)inet->inet_daddr;
		sport = (__force __be16)inet->inet_sport;
		dport = (__force __be16)inet->inet_dport;
	}

	/* XXX sigh, inet-> doesn't have sparse annotation so any
	 * use of it here generates a warning with -Wbitwise */
	seq_printf(seq, "%p:\n"
		   "  krefs:           %d\n"
		   "  sock:            %pI4:%u -> "
				      "%pI4:%u\n"
		   "  remote node:     %s\n"
		   "  page off:        %zu\n"
		   "  handshake ok:    %u\n"
		   "  timer:           %lld usecs\n"
		   "  data ready:      %lld usecs\n"
		   "  advance start:   %lld usecs\n"
		   "  advance stop:    %lld usecs\n"
		   "  func start:      %lld usecs\n"
		   "  func stop:       %lld usecs\n"
		   "  func key:        0x%08x\n"
		   "  func type:       %u\n",
		   sc,
		   kref_read(&sc->sc_kref),
		   &saddr, inet ? ntohs(sport) : 0,
		   &daddr, inet ? ntohs(dport) : 0,
		   sc->sc_node->nd_name,
		   sc->sc_page_off,
		   sc->sc_handshake_ok,
		   (long long)ktime_to_us(sc->sc_tv_timer),
		   (long long)ktime_to_us(sc->sc_tv_data_ready),
		   (long long)ktime_to_us(sc->sc_tv_advance_start),
		   (long long)ktime_to_us(sc->sc_tv_advance_stop),
		   (long long)ktime_to_us(sc->sc_tv_func_start),
		   (long long)ktime_to_us(sc->sc_tv_func_stop),
		   sc->sc_msg_key,
		   sc->sc_msg_type);
}

static int sc_seq_show(struct seq_file *seq, void *v)
{
	struct o2net_sock_debug *sd = seq->private;
	struct o2net_sock_container *sc, *dummy_sc = sd->dbg_sock;

	spin_lock(&o2net_debug_lock);
	sc = next_sc(dummy_sc);

	if (sc) {
		if (sd->dbg_ctxt == SHOW_SOCK_CONTAINERS)
			sc_show_sock_container(seq, sc);
		else
			sc_show_sock_stats(seq, sc);
	}

	spin_unlock(&o2net_debug_lock);

	return 0;
}

static void sc_seq_stop(struct seq_file *seq, void *v)
{
}

static const struct seq_operations sc_seq_ops = {
	.start = sc_seq_start,
	.next = sc_seq_next,
	.stop = sc_seq_stop,
	.show = sc_seq_show,
};

static int sc_common_open(struct file *file, int ctxt)
{
	struct o2net_sock_debug *sd;
	struct o2net_sock_container *dummy_sc;

	dummy_sc = kzalloc(sizeof(*dummy_sc), GFP_KERNEL);
	if (!dummy_sc)
		return -ENOMEM;

	sd = __seq_open_private(file, &sc_seq_ops, sizeof(*sd));
	if (!sd) {
		kfree(dummy_sc);
		return -ENOMEM;
	}

	sd->dbg_ctxt = ctxt;
	sd->dbg_sock = dummy_sc;

	o2net_debug_add_sc(dummy_sc);

	return 0;
}

static int sc_fop_release(struct inode *inode, struct file *file)
{
	struct seq_file *seq = file->private_data;
	struct o2net_sock_debug *sd = seq->private;
	struct o2net_sock_container *dummy_sc = sd->dbg_sock;

	o2net_debug_del_sc(dummy_sc);
	return seq_release_private(inode, file);
}

static int stats_fop_open(struct inode *inode, struct file *file)
{
	return sc_common_open(file, SHOW_SOCK_STATS);
}

static const struct file_operations stats_seq_fops = {
	.open = stats_fop_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = sc_fop_release,
};

static int sc_fop_open(struct inode *inode, struct file *file)
{
	return sc_common_open(file, SHOW_SOCK_CONTAINERS);
}

static const struct file_operations sc_seq_fops = {
	.open = sc_fop_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = sc_fop_release,
};

static int o2net_fill_bitmap(char *buf, int len)
{
	unsigned long map[BITS_TO_LONGS(O2NM_MAX_NODES)];
	int i = -1, out = 0;

	o2net_fill_node_map(map, sizeof(map));

	while ((i = find_next_bit(map, O2NM_MAX_NODES, i + 1)) < O2NM_MAX_NODES)
		out += snprintf(buf + out, PAGE_SIZE - out, "%d ", i);
	out += snprintf(buf + out, PAGE_SIZE - out, "\n");

	return out;
}

static int nodes_fop_open(struct inode *inode, struct file *file)
{
	char *buf;

	buf = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	i_size_write(inode, o2net_fill_bitmap(buf, PAGE_SIZE));

	file->private_data = buf;

	return 0;
}

static int o2net_debug_release(struct inode *inode, struct file *file)
{
	kfree(file->private_data);
	return 0;
}

static ssize_t o2net_debug_read(struct file *file, char __user *buf,
				size_t nbytes, loff_t *ppos)
{
	return simple_read_from_buffer(buf, nbytes, ppos, file->private_data,
				       i_size_read(file->f_mapping->host));
}

static const struct file_operations nodes_fops = {
	.open		= nodes_fop_open,
	.release	= o2net_debug_release,
	.read		= o2net_debug_read,
	.llseek		= generic_file_llseek,
};

void o2net_debugfs_exit(void)
{
	debugfs_remove(nodes_dentry);
	debugfs_remove(stats_dentry);
	debugfs_remove(sc_dentry);
	debugfs_remove(nst_dentry);
	debugfs_remove(o2net_dentry);
}

int o2net_debugfs_init(void)
{
	umode_t mode = S_IFREG|S_IRUSR;

	o2net_dentry = debugfs_create_dir(O2NET_DEBUG_DIR, NULL);
	if (o2net_dentry)
		nst_dentry = debugfs_create_file(NST_DEBUG_NAME, mode,
					o2net_dentry, NULL, &nst_seq_fops);
	if (nst_dentry)
		sc_dentry = debugfs_create_file(SC_DEBUG_NAME, mode,
					o2net_dentry, NULL, &sc_seq_fops);
	if (sc_dentry)
		stats_dentry = debugfs_create_file(STATS_DEBUG_NAME, mode,
					o2net_dentry, NULL, &stats_seq_fops);
	if (stats_dentry)
		nodes_dentry = debugfs_create_file(NODES_DEBUG_NAME, mode,
					o2net_dentry, NULL, &nodes_fops);
	if (nodes_dentry)
		return 0;

	o2net_debugfs_exit();
	mlog_errno(-ENOMEM);
	return -ENOMEM;
}

#endif	/* CONFIG_DEBUG_FS */
