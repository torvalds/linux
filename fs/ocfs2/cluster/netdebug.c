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

static struct dentry *o2net_dentry;
static struct dentry *sc_dentry;
static struct dentry *nst_dentry;

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

	spin_lock(&o2net_debug_lock);
	nst = next_nst(dummy_nst);

	if (nst != NULL) {
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
			   "  sock acquiry: %lu.%lu\n"
			   "  send start:   %lu.%lu\n"
			   "  wait start:   %lu.%lu\n",
			   nst, (unsigned long)nst->st_task->pid,
			   (unsigned long)nst->st_task->tgid,
			   nst->st_task->comm, nst->st_node,
			   nst->st_sc, nst->st_id, nst->st_msg_type,
			   nst->st_msg_key,
			   nst->st_sock_time.tv_sec, nst->st_sock_time.tv_usec,
			   nst->st_send_time.tv_sec, nst->st_send_time.tv_usec,
			   nst->st_status_time.tv_sec,
			   nst->st_status_time.tv_usec);
	}

	spin_unlock(&o2net_debug_lock);

	return 0;
}

static void nst_seq_stop(struct seq_file *seq, void *v)
{
}

static struct seq_operations nst_seq_ops = {
	.start = nst_seq_start,
	.next = nst_seq_next,
	.stop = nst_seq_stop,
	.show = nst_seq_show,
};

static int nst_fop_open(struct inode *inode, struct file *file)
{
	struct o2net_send_tracking *dummy_nst;
	struct seq_file *seq;
	int ret;

	dummy_nst = kmalloc(sizeof(struct o2net_send_tracking), GFP_KERNEL);
	if (dummy_nst == NULL) {
		ret = -ENOMEM;
		goto out;
	}
	dummy_nst->st_task = NULL;

	ret = seq_open(file, &nst_seq_ops);
	if (ret)
		goto out;

	seq = file->private_data;
	seq->private = dummy_nst;
	o2net_debug_add_nst(dummy_nst);

	dummy_nst = NULL;

out:
	kfree(dummy_nst);
	return ret;
}

static int nst_fop_release(struct inode *inode, struct file *file)
{
	struct seq_file *seq = file->private_data;
	struct o2net_send_tracking *dummy_nst = seq->private;

	o2net_debug_del_nst(dummy_nst);
	return seq_release_private(inode, file);
}

static struct file_operations nst_seq_fops = {
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
	struct o2net_sock_container *sc, *dummy_sc = seq->private;

	spin_lock(&o2net_debug_lock);
	sc = next_sc(dummy_sc);
	spin_unlock(&o2net_debug_lock);

	return sc;
}

static void *sc_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct o2net_sock_container *sc, *dummy_sc = seq->private;

	spin_lock(&o2net_debug_lock);
	sc = next_sc(dummy_sc);
	list_del_init(&dummy_sc->sc_net_debug_item);
	if (sc)
		list_add(&dummy_sc->sc_net_debug_item, &sc->sc_net_debug_item);
	spin_unlock(&o2net_debug_lock);

	return sc; /* unused, just needs to be null when done */
}

#define TV_SEC_USEC(TV) TV.tv_sec, TV.tv_usec

static int sc_seq_show(struct seq_file *seq, void *v)
{
	struct o2net_sock_container *sc, *dummy_sc = seq->private;

	spin_lock(&o2net_debug_lock);
	sc = next_sc(dummy_sc);

	if (sc != NULL) {
		struct inet_sock *inet = NULL;

		__be32 saddr = 0, daddr = 0;
		__be16 sport = 0, dport = 0;

		if (sc->sc_sock) {
			inet = inet_sk(sc->sc_sock->sk);
			/* the stack's structs aren't sparse endian clean */
			saddr = (__force __be32)inet->saddr;
			daddr = (__force __be32)inet->daddr;
			sport = (__force __be16)inet->sport;
			dport = (__force __be16)inet->dport;
		}

		/* XXX sigh, inet-> doesn't have sparse annotation so any
		 * use of it here generates a warning with -Wbitwise */
		seq_printf(seq, "%p:\n"
			   "  krefs:           %d\n"
			   "  sock:            %u.%u.%u.%u:%u -> "
					      "%u.%u.%u.%u:%u\n"
			   "  remote node:     %s\n"
			   "  page off:        %zu\n"
			   "  handshake ok:    %u\n"
			   "  timer:           %lu.%lu\n"
			   "  data ready:      %lu.%lu\n"
			   "  advance start:   %lu.%lu\n"
			   "  advance stop:    %lu.%lu\n"
			   "  func start:      %lu.%lu\n"
			   "  func stop:       %lu.%lu\n"
			   "  func key:        %u\n"
			   "  func type:       %u\n",
			   sc,
			   atomic_read(&sc->sc_kref.refcount),
			   NIPQUAD(saddr), inet ? ntohs(sport) : 0,
			   NIPQUAD(daddr), inet ? ntohs(dport) : 0,
			   sc->sc_node->nd_name,
			   sc->sc_page_off,
			   sc->sc_handshake_ok,
			   TV_SEC_USEC(sc->sc_tv_timer),
			   TV_SEC_USEC(sc->sc_tv_data_ready),
			   TV_SEC_USEC(sc->sc_tv_advance_start),
			   TV_SEC_USEC(sc->sc_tv_advance_stop),
			   TV_SEC_USEC(sc->sc_tv_func_start),
			   TV_SEC_USEC(sc->sc_tv_func_stop),
			   sc->sc_msg_key,
			   sc->sc_msg_type);
	}


	spin_unlock(&o2net_debug_lock);

	return 0;
}

static void sc_seq_stop(struct seq_file *seq, void *v)
{
}

static struct seq_operations sc_seq_ops = {
	.start = sc_seq_start,
	.next = sc_seq_next,
	.stop = sc_seq_stop,
	.show = sc_seq_show,
};

static int sc_fop_open(struct inode *inode, struct file *file)
{
	struct o2net_sock_container *dummy_sc;
	struct seq_file *seq;
	int ret;

	dummy_sc = kmalloc(sizeof(struct o2net_sock_container), GFP_KERNEL);
	if (dummy_sc == NULL) {
		ret = -ENOMEM;
		goto out;
	}
	dummy_sc->sc_page = NULL;

	ret = seq_open(file, &sc_seq_ops);
	if (ret)
		goto out;

	seq = file->private_data;
	seq->private = dummy_sc;
	o2net_debug_add_sc(dummy_sc);

	dummy_sc = NULL;

out:
	kfree(dummy_sc);
	return ret;
}

static int sc_fop_release(struct inode *inode, struct file *file)
{
	struct seq_file *seq = file->private_data;
	struct o2net_sock_container *dummy_sc = seq->private;

	o2net_debug_del_sc(dummy_sc);
	return seq_release_private(inode, file);
}

static struct file_operations sc_seq_fops = {
	.open = sc_fop_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = sc_fop_release,
};

int o2net_debugfs_init(void)
{
	o2net_dentry = debugfs_create_dir(O2NET_DEBUG_DIR, NULL);
	if (!o2net_dentry) {
		mlog_errno(-ENOMEM);
		goto bail;
	}

	nst_dentry = debugfs_create_file(NST_DEBUG_NAME, S_IFREG|S_IRUSR,
					 o2net_dentry, NULL,
					 &nst_seq_fops);
	if (!nst_dentry) {
		mlog_errno(-ENOMEM);
		goto bail;
	}

	sc_dentry = debugfs_create_file(SC_DEBUG_NAME, S_IFREG|S_IRUSR,
					o2net_dentry, NULL,
					&sc_seq_fops);
	if (!sc_dentry) {
		mlog_errno(-ENOMEM);
		goto bail;
	}

	return 0;
bail:
	if (sc_dentry)
		debugfs_remove(sc_dentry);
	if (nst_dentry)
		debugfs_remove(nst_dentry);
	if (o2net_dentry)
		debugfs_remove(o2net_dentry);
	return -ENOMEM;
}

void o2net_debugfs_exit(void)
{
	if (sc_dentry)
		debugfs_remove(sc_dentry);
	if (nst_dentry)
		debugfs_remove(nst_dentry);
	if (o2net_dentry)
		debugfs_remove(o2net_dentry);
}

#endif	/* CONFIG_DEBUG_FS */
