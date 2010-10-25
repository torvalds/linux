/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * dlmdebug.c
 *
 * debug functionality for the dlm
 *
 * Copyright (C) 2004, 2008 Oracle.  All rights reserved.
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

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/highmem.h>
#include <linux/sysctl.h>
#include <linux/spinlock.h>
#include <linux/debugfs.h>

#include "cluster/heartbeat.h"
#include "cluster/nodemanager.h"
#include "cluster/tcp.h"

#include "dlmapi.h"
#include "dlmcommon.h"
#include "dlmdomain.h"
#include "dlmdebug.h"

#define MLOG_MASK_PREFIX ML_DLM
#include "cluster/masklog.h"

static int stringify_lockname(const char *lockname, int locklen, char *buf,
			      int len);

void dlm_print_one_lock_resource(struct dlm_lock_resource *res)
{
	spin_lock(&res->spinlock);
	__dlm_print_one_lock_resource(res);
	spin_unlock(&res->spinlock);
}

static void dlm_print_lockres_refmap(struct dlm_lock_resource *res)
{
	int bit;
	assert_spin_locked(&res->spinlock);

	printk("  refmap nodes: [ ");
	bit = 0;
	while (1) {
		bit = find_next_bit(res->refmap, O2NM_MAX_NODES, bit);
		if (bit >= O2NM_MAX_NODES)
			break;
		printk("%u ", bit);
		bit++;
	}
	printk("], inflight=%u\n", res->inflight_locks);
}

static void __dlm_print_lock(struct dlm_lock *lock)
{
	spin_lock(&lock->spinlock);

	printk("    type=%d, conv=%d, node=%u, cookie=%u:%llu, "
	       "ref=%u, ast=(empty=%c,pend=%c), bast=(empty=%c,pend=%c), "
	       "pending=(conv=%c,lock=%c,cancel=%c,unlock=%c)\n",
	       lock->ml.type, lock->ml.convert_type, lock->ml.node,
	       dlm_get_lock_cookie_node(be64_to_cpu(lock->ml.cookie)),
	       dlm_get_lock_cookie_seq(be64_to_cpu(lock->ml.cookie)),
	       atomic_read(&lock->lock_refs.refcount),
	       (list_empty(&lock->ast_list) ? 'y' : 'n'),
	       (lock->ast_pending ? 'y' : 'n'),
	       (list_empty(&lock->bast_list) ? 'y' : 'n'),
	       (lock->bast_pending ? 'y' : 'n'),
	       (lock->convert_pending ? 'y' : 'n'),
	       (lock->lock_pending ? 'y' : 'n'),
	       (lock->cancel_pending ? 'y' : 'n'),
	       (lock->unlock_pending ? 'y' : 'n'));

	spin_unlock(&lock->spinlock);
}

void __dlm_print_one_lock_resource(struct dlm_lock_resource *res)
{
	struct list_head *iter2;
	struct dlm_lock *lock;
	char buf[DLM_LOCKID_NAME_MAX];

	assert_spin_locked(&res->spinlock);

	stringify_lockname(res->lockname.name, res->lockname.len,
			   buf, sizeof(buf));
	printk("lockres: %s, owner=%u, state=%u\n",
	       buf, res->owner, res->state);
	printk("  last used: %lu, refcnt: %u, on purge list: %s\n",
	       res->last_used, atomic_read(&res->refs.refcount),
	       list_empty(&res->purge) ? "no" : "yes");
	printk("  on dirty list: %s, on reco list: %s, "
	       "migrating pending: %s\n",
	       list_empty(&res->dirty) ? "no" : "yes",
	       list_empty(&res->recovering) ? "no" : "yes",
	       res->migration_pending ? "yes" : "no");
	printk("  inflight locks: %d, asts reserved: %d\n",
	       res->inflight_locks, atomic_read(&res->asts_reserved));
	dlm_print_lockres_refmap(res);
	printk("  granted queue:\n");
	list_for_each(iter2, &res->granted) {
		lock = list_entry(iter2, struct dlm_lock, list);
		__dlm_print_lock(lock);
	}
	printk("  converting queue:\n");
	list_for_each(iter2, &res->converting) {
		lock = list_entry(iter2, struct dlm_lock, list);
		__dlm_print_lock(lock);
	}
	printk("  blocked queue:\n");
	list_for_each(iter2, &res->blocked) {
		lock = list_entry(iter2, struct dlm_lock, list);
		__dlm_print_lock(lock);
	}
}

void dlm_print_one_lock(struct dlm_lock *lockid)
{
	dlm_print_one_lock_resource(lockid->lockres);
}
EXPORT_SYMBOL_GPL(dlm_print_one_lock);

static const char *dlm_errnames[] = {
	[DLM_NORMAL] =			"DLM_NORMAL",
	[DLM_GRANTED] =			"DLM_GRANTED",
	[DLM_DENIED] =			"DLM_DENIED",
	[DLM_DENIED_NOLOCKS] =		"DLM_DENIED_NOLOCKS",
	[DLM_WORKING] =			"DLM_WORKING",
	[DLM_BLOCKED] =			"DLM_BLOCKED",
	[DLM_BLOCKED_ORPHAN] =		"DLM_BLOCKED_ORPHAN",
	[DLM_DENIED_GRACE_PERIOD] =	"DLM_DENIED_GRACE_PERIOD",
	[DLM_SYSERR] =			"DLM_SYSERR",
	[DLM_NOSUPPORT] =		"DLM_NOSUPPORT",
	[DLM_CANCELGRANT] =		"DLM_CANCELGRANT",
	[DLM_IVLOCKID] =		"DLM_IVLOCKID",
	[DLM_SYNC] =			"DLM_SYNC",
	[DLM_BADTYPE] =			"DLM_BADTYPE",
	[DLM_BADRESOURCE] =		"DLM_BADRESOURCE",
	[DLM_MAXHANDLES] =		"DLM_MAXHANDLES",
	[DLM_NOCLINFO] =		"DLM_NOCLINFO",
	[DLM_NOLOCKMGR] =		"DLM_NOLOCKMGR",
	[DLM_NOPURGED] =		"DLM_NOPURGED",
	[DLM_BADARGS] =			"DLM_BADARGS",
	[DLM_VOID] =			"DLM_VOID",
	[DLM_NOTQUEUED] =		"DLM_NOTQUEUED",
	[DLM_IVBUFLEN] =		"DLM_IVBUFLEN",
	[DLM_CVTUNGRANT] =		"DLM_CVTUNGRANT",
	[DLM_BADPARAM] =		"DLM_BADPARAM",
	[DLM_VALNOTVALID] =		"DLM_VALNOTVALID",
	[DLM_REJECTED] =		"DLM_REJECTED",
	[DLM_ABORT] =			"DLM_ABORT",
	[DLM_CANCEL] =			"DLM_CANCEL",
	[DLM_IVRESHANDLE] =		"DLM_IVRESHANDLE",
	[DLM_DEADLOCK] =		"DLM_DEADLOCK",
	[DLM_DENIED_NOASTS] =		"DLM_DENIED_NOASTS",
	[DLM_FORWARD] =			"DLM_FORWARD",
	[DLM_TIMEOUT] =			"DLM_TIMEOUT",
	[DLM_IVGROUPID] =		"DLM_IVGROUPID",
	[DLM_VERS_CONFLICT] =		"DLM_VERS_CONFLICT",
	[DLM_BAD_DEVICE_PATH] =		"DLM_BAD_DEVICE_PATH",
	[DLM_NO_DEVICE_PERMISSION] =	"DLM_NO_DEVICE_PERMISSION",
	[DLM_NO_CONTROL_DEVICE ] =	"DLM_NO_CONTROL_DEVICE ",
	[DLM_RECOVERING] =		"DLM_RECOVERING",
	[DLM_MIGRATING] =		"DLM_MIGRATING",
	[DLM_MAXSTATS] =		"DLM_MAXSTATS",
};

static const char *dlm_errmsgs[] = {
	[DLM_NORMAL] = 			"request in progress",
	[DLM_GRANTED] = 		"request granted",
	[DLM_DENIED] = 			"request denied",
	[DLM_DENIED_NOLOCKS] = 		"request denied, out of system resources",
	[DLM_WORKING] = 		"async request in progress",
	[DLM_BLOCKED] = 		"lock request blocked",
	[DLM_BLOCKED_ORPHAN] = 		"lock request blocked by a orphan lock",
	[DLM_DENIED_GRACE_PERIOD] = 	"topological change in progress",
	[DLM_SYSERR] = 			"system error",
	[DLM_NOSUPPORT] = 		"unsupported",
	[DLM_CANCELGRANT] = 		"can't cancel convert: already granted",
	[DLM_IVLOCKID] = 		"bad lockid",
	[DLM_SYNC] = 			"synchronous request granted",
	[DLM_BADTYPE] = 		"bad resource type",
	[DLM_BADRESOURCE] = 		"bad resource handle",
	[DLM_MAXHANDLES] = 		"no more resource handles",
	[DLM_NOCLINFO] = 		"can't contact cluster manager",
	[DLM_NOLOCKMGR] = 		"can't contact lock manager",
	[DLM_NOPURGED] = 		"can't contact purge daemon",
	[DLM_BADARGS] = 		"bad api args",
	[DLM_VOID] = 			"no status",
	[DLM_NOTQUEUED] = 		"NOQUEUE was specified and request failed",
	[DLM_IVBUFLEN] = 		"invalid resource name length",
	[DLM_CVTUNGRANT] = 		"attempted to convert ungranted lock",
	[DLM_BADPARAM] = 		"invalid lock mode specified",
	[DLM_VALNOTVALID] = 		"value block has been invalidated",
	[DLM_REJECTED] = 		"request rejected, unrecognized client",
	[DLM_ABORT] = 			"blocked lock request cancelled",
	[DLM_CANCEL] = 			"conversion request cancelled",
	[DLM_IVRESHANDLE] = 		"invalid resource handle",
	[DLM_DEADLOCK] = 		"deadlock recovery refused this request",
	[DLM_DENIED_NOASTS] = 		"failed to allocate AST",
	[DLM_FORWARD] = 		"request must wait for primary's response",
	[DLM_TIMEOUT] = 		"timeout value for lock has expired",
	[DLM_IVGROUPID] = 		"invalid group specification",
	[DLM_VERS_CONFLICT] = 		"version conflicts prevent request handling",
	[DLM_BAD_DEVICE_PATH] = 	"Locks device does not exist or path wrong",
	[DLM_NO_DEVICE_PERMISSION] = 	"Client has insufficient perms for device",
	[DLM_NO_CONTROL_DEVICE] = 	"Cannot set options on opened device ",
	[DLM_RECOVERING] = 		"lock resource being recovered",
	[DLM_MIGRATING] = 		"lock resource being migrated",
	[DLM_MAXSTATS] = 		"invalid error number",
};

const char *dlm_errmsg(enum dlm_status err)
{
	if (err >= DLM_MAXSTATS || err < 0)
		return dlm_errmsgs[DLM_MAXSTATS];
	return dlm_errmsgs[err];
}
EXPORT_SYMBOL_GPL(dlm_errmsg);

const char *dlm_errname(enum dlm_status err)
{
	if (err >= DLM_MAXSTATS || err < 0)
		return dlm_errnames[DLM_MAXSTATS];
	return dlm_errnames[err];
}
EXPORT_SYMBOL_GPL(dlm_errname);

/* NOTE: This function converts a lockname into a string. It uses knowledge
 * of the format of the lockname that should be outside the purview of the dlm.
 * We are adding only to make dlm debugging slightly easier.
 *
 * For more on lockname formats, please refer to dlmglue.c and ocfs2_lockid.h.
 */
static int stringify_lockname(const char *lockname, int locklen, char *buf,
			      int len)
{
	int out = 0;
	__be64 inode_blkno_be;

#define OCFS2_DENTRY_LOCK_INO_START	18
	if (*lockname == 'N') {
		memcpy((__be64 *)&inode_blkno_be,
		       (char *)&lockname[OCFS2_DENTRY_LOCK_INO_START],
		       sizeof(__be64));
		out += snprintf(buf + out, len - out, "%.*s%08x",
				OCFS2_DENTRY_LOCK_INO_START - 1, lockname,
				(unsigned int)be64_to_cpu(inode_blkno_be));
	} else
		out += snprintf(buf + out, len - out, "%.*s",
				locklen, lockname);
	return out;
}

static int stringify_nodemap(unsigned long *nodemap, int maxnodes,
			     char *buf, int len)
{
	int out = 0;
	int i = -1;

	while ((i = find_next_bit(nodemap, maxnodes, i + 1)) < maxnodes)
		out += snprintf(buf + out, len - out, "%d ", i);

	return out;
}

static int dump_mle(struct dlm_master_list_entry *mle, char *buf, int len)
{
	int out = 0;
	char *mle_type;

	if (mle->type == DLM_MLE_BLOCK)
		mle_type = "BLK";
	else if (mle->type == DLM_MLE_MASTER)
		mle_type = "MAS";
	else
		mle_type = "MIG";

	out += stringify_lockname(mle->mname, mle->mnamelen, buf + out, len - out);
	out += snprintf(buf + out, len - out,
			"\t%3s\tmas=%3u\tnew=%3u\tevt=%1d\tuse=%1d\tref=%3d\n",
			mle_type, mle->master, mle->new_master,
			!list_empty(&mle->hb_events),
			!!mle->inuse,
			atomic_read(&mle->mle_refs.refcount));

	out += snprintf(buf + out, len - out, "Maybe=");
	out += stringify_nodemap(mle->maybe_map, O2NM_MAX_NODES,
				 buf + out, len - out);
	out += snprintf(buf + out, len - out, "\n");

	out += snprintf(buf + out, len - out, "Vote=");
	out += stringify_nodemap(mle->vote_map, O2NM_MAX_NODES,
				 buf + out, len - out);
	out += snprintf(buf + out, len - out, "\n");

	out += snprintf(buf + out, len - out, "Response=");
	out += stringify_nodemap(mle->response_map, O2NM_MAX_NODES,
				 buf + out, len - out);
	out += snprintf(buf + out, len - out, "\n");

	out += snprintf(buf + out, len - out, "Node=");
	out += stringify_nodemap(mle->node_map, O2NM_MAX_NODES,
				 buf + out, len - out);
	out += snprintf(buf + out, len - out, "\n");

	out += snprintf(buf + out, len - out, "\n");

	return out;
}

void dlm_print_one_mle(struct dlm_master_list_entry *mle)
{
	char *buf;

	buf = (char *) get_zeroed_page(GFP_NOFS);
	if (buf) {
		dump_mle(mle, buf, PAGE_SIZE - 1);
		free_page((unsigned long)buf);
	}
}

#ifdef CONFIG_DEBUG_FS

static struct dentry *dlm_debugfs_root = NULL;

#define DLM_DEBUGFS_DIR				"o2dlm"
#define DLM_DEBUGFS_DLM_STATE			"dlm_state"
#define DLM_DEBUGFS_LOCKING_STATE		"locking_state"
#define DLM_DEBUGFS_MLE_STATE			"mle_state"
#define DLM_DEBUGFS_PURGE_LIST			"purge_list"

/* begin - utils funcs */
static void dlm_debug_free(struct kref *kref)
{
	struct dlm_debug_ctxt *dc;

	dc = container_of(kref, struct dlm_debug_ctxt, debug_refcnt);

	kfree(dc);
}

static void dlm_debug_put(struct dlm_debug_ctxt *dc)
{
	if (dc)
		kref_put(&dc->debug_refcnt, dlm_debug_free);
}

static void dlm_debug_get(struct dlm_debug_ctxt *dc)
{
	kref_get(&dc->debug_refcnt);
}

static struct debug_buffer *debug_buffer_allocate(void)
{
	struct debug_buffer *db = NULL;

	db = kzalloc(sizeof(struct debug_buffer), GFP_KERNEL);
	if (!db)
		goto bail;

	db->len = PAGE_SIZE;
	db->buf = kmalloc(db->len, GFP_KERNEL);
	if (!db->buf)
		goto bail;

	return db;
bail:
	kfree(db);
	return NULL;
}

static ssize_t debug_buffer_read(struct file *file, char __user *buf,
				 size_t nbytes, loff_t *ppos)
{
	struct debug_buffer *db = file->private_data;

	return simple_read_from_buffer(buf, nbytes, ppos, db->buf, db->len);
}

static loff_t debug_buffer_llseek(struct file *file, loff_t off, int whence)
{
	struct debug_buffer *db = file->private_data;
	loff_t new = -1;

	switch (whence) {
	case 0:
		new = off;
		break;
	case 1:
		new = file->f_pos + off;
		break;
	}

	if (new < 0 || new > db->len)
		return -EINVAL;

	return (file->f_pos = new);
}

static int debug_buffer_release(struct inode *inode, struct file *file)
{
	struct debug_buffer *db = file->private_data;

	if (db)
		kfree(db->buf);
	kfree(db);

	return 0;
}
/* end - util funcs */

/* begin - purge list funcs */
static int debug_purgelist_print(struct dlm_ctxt *dlm, struct debug_buffer *db)
{
	struct dlm_lock_resource *res;
	int out = 0;
	unsigned long total = 0;

	out += snprintf(db->buf + out, db->len - out,
			"Dumping Purgelist for Domain: %s\n", dlm->name);

	spin_lock(&dlm->spinlock);
	list_for_each_entry(res, &dlm->purge_list, purge) {
		++total;
		if (db->len - out < 100)
			continue;
		spin_lock(&res->spinlock);
		out += stringify_lockname(res->lockname.name,
					  res->lockname.len,
					  db->buf + out, db->len - out);
		out += snprintf(db->buf + out, db->len - out, "\t%ld\n",
				(jiffies - res->last_used)/HZ);
		spin_unlock(&res->spinlock);
	}
	spin_unlock(&dlm->spinlock);

	out += snprintf(db->buf + out, db->len - out,
			"Total on list: %ld\n", total);

	return out;
}

static int debug_purgelist_open(struct inode *inode, struct file *file)
{
	struct dlm_ctxt *dlm = inode->i_private;
	struct debug_buffer *db;

	db = debug_buffer_allocate();
	if (!db)
		goto bail;

	db->len = debug_purgelist_print(dlm, db);

	file->private_data = db;

	return 0;
bail:
	return -ENOMEM;
}

static const struct file_operations debug_purgelist_fops = {
	.open =		debug_purgelist_open,
	.release =	debug_buffer_release,
	.read =		debug_buffer_read,
	.llseek =	debug_buffer_llseek,
};
/* end - purge list funcs */

/* begin - debug mle funcs */
static int debug_mle_print(struct dlm_ctxt *dlm, struct debug_buffer *db)
{
	struct dlm_master_list_entry *mle;
	struct hlist_head *bucket;
	struct hlist_node *list;
	int i, out = 0;
	unsigned long total = 0, longest = 0, bucket_count = 0;

	out += snprintf(db->buf + out, db->len - out,
			"Dumping MLEs for Domain: %s\n", dlm->name);

	spin_lock(&dlm->master_lock);
	for (i = 0; i < DLM_HASH_BUCKETS; i++) {
		bucket = dlm_master_hash(dlm, i);
		hlist_for_each(list, bucket) {
			mle = hlist_entry(list, struct dlm_master_list_entry,
					  master_hash_node);
			++total;
			++bucket_count;
			if (db->len - out < 200)
				continue;
			out += dump_mle(mle, db->buf + out, db->len - out);
		}
		longest = max(longest, bucket_count);
		bucket_count = 0;
	}
	spin_unlock(&dlm->master_lock);

	out += snprintf(db->buf + out, db->len - out,
			"Total: %ld, Longest: %ld\n", total, longest);
	return out;
}

static int debug_mle_open(struct inode *inode, struct file *file)
{
	struct dlm_ctxt *dlm = inode->i_private;
	struct debug_buffer *db;

	db = debug_buffer_allocate();
	if (!db)
		goto bail;

	db->len = debug_mle_print(dlm, db);

	file->private_data = db;

	return 0;
bail:
	return -ENOMEM;
}

static const struct file_operations debug_mle_fops = {
	.open =		debug_mle_open,
	.release =	debug_buffer_release,
	.read =		debug_buffer_read,
	.llseek =	debug_buffer_llseek,
};

/* end - debug mle funcs */

/* begin - debug lockres funcs */
static int dump_lock(struct dlm_lock *lock, int list_type, char *buf, int len)
{
	int out;

#define DEBUG_LOCK_VERSION	1
	spin_lock(&lock->spinlock);
	out = snprintf(buf, len, "LOCK:%d,%d,%d,%d,%d,%d:%lld,%d,%d,%d,%d,%d,"
		       "%d,%d,%d,%d\n",
		       DEBUG_LOCK_VERSION,
		       list_type, lock->ml.type, lock->ml.convert_type,
		       lock->ml.node,
		       dlm_get_lock_cookie_node(be64_to_cpu(lock->ml.cookie)),
		       dlm_get_lock_cookie_seq(be64_to_cpu(lock->ml.cookie)),
		       !list_empty(&lock->ast_list),
		       !list_empty(&lock->bast_list),
		       lock->ast_pending, lock->bast_pending,
		       lock->convert_pending, lock->lock_pending,
		       lock->cancel_pending, lock->unlock_pending,
		       atomic_read(&lock->lock_refs.refcount));
	spin_unlock(&lock->spinlock);

	return out;
}

static int dump_lockres(struct dlm_lock_resource *res, char *buf, int len)
{
	struct dlm_lock *lock;
	int i;
	int out = 0;

	out += snprintf(buf + out, len - out, "NAME:");
	out += stringify_lockname(res->lockname.name, res->lockname.len,
				  buf + out, len - out);
	out += snprintf(buf + out, len - out, "\n");

#define DEBUG_LRES_VERSION	1
	out += snprintf(buf + out, len - out,
			"LRES:%d,%d,%d,%ld,%d,%d,%d,%d,%d,%d,%d\n",
			DEBUG_LRES_VERSION,
			res->owner, res->state, res->last_used,
			!list_empty(&res->purge),
			!list_empty(&res->dirty),
			!list_empty(&res->recovering),
			res->inflight_locks, res->migration_pending,
			atomic_read(&res->asts_reserved),
			atomic_read(&res->refs.refcount));

	/* refmap */
	out += snprintf(buf + out, len - out, "RMAP:");
	out += stringify_nodemap(res->refmap, O2NM_MAX_NODES,
				 buf + out, len - out);
	out += snprintf(buf + out, len - out, "\n");

	/* lvb */
	out += snprintf(buf + out, len - out, "LVBX:");
	for (i = 0; i < DLM_LVB_LEN; i++)
		out += snprintf(buf + out, len - out,
					"%02x", (unsigned char)res->lvb[i]);
	out += snprintf(buf + out, len - out, "\n");

	/* granted */
	list_for_each_entry(lock, &res->granted, list)
		out += dump_lock(lock, 0, buf + out, len - out);

	/* converting */
	list_for_each_entry(lock, &res->converting, list)
		out += dump_lock(lock, 1, buf + out, len - out);

	/* blocked */
	list_for_each_entry(lock, &res->blocked, list)
		out += dump_lock(lock, 2, buf + out, len - out);

	out += snprintf(buf + out, len - out, "\n");

	return out;
}

static void *lockres_seq_start(struct seq_file *m, loff_t *pos)
{
	struct debug_lockres *dl = m->private;
	struct dlm_ctxt *dlm = dl->dl_ctxt;
	struct dlm_lock_resource *oldres = dl->dl_res;
	struct dlm_lock_resource *res = NULL;
	struct list_head *track_list;

	spin_lock(&dlm->track_lock);
	if (oldres)
		track_list = &oldres->tracking;
	else {
		track_list = &dlm->tracking_list;
		if (list_empty(track_list)) {
			dl = NULL;
			spin_unlock(&dlm->track_lock);
			goto bail;
		}
	}

	list_for_each_entry(res, track_list, tracking) {
		if (&res->tracking == &dlm->tracking_list)
			res = NULL;
		else
			dlm_lockres_get(res);
		break;
	}
	spin_unlock(&dlm->track_lock);

	if (oldres)
		dlm_lockres_put(oldres);

	dl->dl_res = res;

	if (res) {
		spin_lock(&res->spinlock);
		dump_lockres(res, dl->dl_buf, dl->dl_len - 1);
		spin_unlock(&res->spinlock);
	} else
		dl = NULL;

bail:
	/* passed to seq_show */
	return dl;
}

static void lockres_seq_stop(struct seq_file *m, void *v)
{
}

static void *lockres_seq_next(struct seq_file *m, void *v, loff_t *pos)
{
	return NULL;
}

static int lockres_seq_show(struct seq_file *s, void *v)
{
	struct debug_lockres *dl = (struct debug_lockres *)v;

	seq_printf(s, "%s", dl->dl_buf);

	return 0;
}

static const struct seq_operations debug_lockres_ops = {
	.start =	lockres_seq_start,
	.stop =		lockres_seq_stop,
	.next =		lockres_seq_next,
	.show =		lockres_seq_show,
};

static int debug_lockres_open(struct inode *inode, struct file *file)
{
	struct dlm_ctxt *dlm = inode->i_private;
	int ret = -ENOMEM;
	struct seq_file *seq;
	struct debug_lockres *dl = NULL;

	dl = kzalloc(sizeof(struct debug_lockres), GFP_KERNEL);
	if (!dl) {
		mlog_errno(ret);
		goto bail;
	}

	dl->dl_len = PAGE_SIZE;
	dl->dl_buf = kmalloc(dl->dl_len, GFP_KERNEL);
	if (!dl->dl_buf) {
		mlog_errno(ret);
		goto bail;
	}

	ret = seq_open(file, &debug_lockres_ops);
	if (ret) {
		mlog_errno(ret);
		goto bail;
	}

	seq = file->private_data;
	seq->private = dl;

	dlm_grab(dlm);
	dl->dl_ctxt = dlm;

	return 0;
bail:
	if (dl)
		kfree(dl->dl_buf);
	kfree(dl);
	return ret;
}

static int debug_lockres_release(struct inode *inode, struct file *file)
{
	struct seq_file *seq = file->private_data;
	struct debug_lockres *dl = (struct debug_lockres *)seq->private;

	if (dl->dl_res)
		dlm_lockres_put(dl->dl_res);
	dlm_put(dl->dl_ctxt);
	kfree(dl->dl_buf);
	return seq_release_private(inode, file);
}

static const struct file_operations debug_lockres_fops = {
	.open =		debug_lockres_open,
	.release =	debug_lockres_release,
	.read =		seq_read,
	.llseek =	seq_lseek,
};
/* end - debug lockres funcs */

/* begin - debug state funcs */
static int debug_state_print(struct dlm_ctxt *dlm, struct debug_buffer *db)
{
	int out = 0;
	struct dlm_reco_node_data *node;
	char *state;
	int cur_mles = 0, tot_mles = 0;
	int i;

	spin_lock(&dlm->spinlock);

	switch (dlm->dlm_state) {
	case DLM_CTXT_NEW:
		state = "NEW"; break;
	case DLM_CTXT_JOINED:
		state = "JOINED"; break;
	case DLM_CTXT_IN_SHUTDOWN:
		state = "SHUTDOWN"; break;
	case DLM_CTXT_LEAVING:
		state = "LEAVING"; break;
	default:
		state = "UNKNOWN"; break;
	}

	/* Domain: xxxxxxxxxx  Key: 0xdfbac769 */
	out += snprintf(db->buf + out, db->len - out,
			"Domain: %s  Key: 0x%08x  Protocol: %d.%d\n",
			dlm->name, dlm->key, dlm->dlm_locking_proto.pv_major,
			dlm->dlm_locking_proto.pv_minor);

	/* Thread Pid: xxx  Node: xxx  State: xxxxx */
	out += snprintf(db->buf + out, db->len - out,
			"Thread Pid: %d  Node: %d  State: %s\n",
			dlm->dlm_thread_task->pid, dlm->node_num, state);

	/* Number of Joins: xxx  Joining Node: xxx */
	out += snprintf(db->buf + out, db->len - out,
			"Number of Joins: %d  Joining Node: %d\n",
			dlm->num_joins, dlm->joining_node);

	/* Domain Map: xx xx xx */
	out += snprintf(db->buf + out, db->len - out, "Domain Map: ");
	out += stringify_nodemap(dlm->domain_map, O2NM_MAX_NODES,
				 db->buf + out, db->len - out);
	out += snprintf(db->buf + out, db->len - out, "\n");

	/* Live Map: xx xx xx */
	out += snprintf(db->buf + out, db->len - out, "Live Map: ");
	out += stringify_nodemap(dlm->live_nodes_map, O2NM_MAX_NODES,
				 db->buf + out, db->len - out);
	out += snprintf(db->buf + out, db->len - out, "\n");

	/* Lock Resources: xxx (xxx) */
	out += snprintf(db->buf + out, db->len - out,
			"Lock Resources: %d (%d)\n",
			atomic_read(&dlm->res_cur_count),
			atomic_read(&dlm->res_tot_count));

	for (i = 0; i < DLM_MLE_NUM_TYPES; ++i)
		tot_mles += atomic_read(&dlm->mle_tot_count[i]);

	for (i = 0; i < DLM_MLE_NUM_TYPES; ++i)
		cur_mles += atomic_read(&dlm->mle_cur_count[i]);

	/* MLEs: xxx (xxx) */
	out += snprintf(db->buf + out, db->len - out,
			"MLEs: %d (%d)\n", cur_mles, tot_mles);

	/*  Blocking: xxx (xxx) */
	out += snprintf(db->buf + out, db->len - out,
			"  Blocking: %d (%d)\n",
			atomic_read(&dlm->mle_cur_count[DLM_MLE_BLOCK]),
			atomic_read(&dlm->mle_tot_count[DLM_MLE_BLOCK]));

	/*  Mastery: xxx (xxx) */
	out += snprintf(db->buf + out, db->len - out,
			"  Mastery: %d (%d)\n",
			atomic_read(&dlm->mle_cur_count[DLM_MLE_MASTER]),
			atomic_read(&dlm->mle_tot_count[DLM_MLE_MASTER]));

	/*  Migration: xxx (xxx) */
	out += snprintf(db->buf + out, db->len - out,
			"  Migration: %d (%d)\n",
			atomic_read(&dlm->mle_cur_count[DLM_MLE_MIGRATION]),
			atomic_read(&dlm->mle_tot_count[DLM_MLE_MIGRATION]));

	/* Lists: Dirty=Empty  Purge=InUse  PendingASTs=Empty  ... */
	out += snprintf(db->buf + out, db->len - out,
			"Lists: Dirty=%s  Purge=%s  PendingASTs=%s  "
			"PendingBASTs=%s\n",
			(list_empty(&dlm->dirty_list) ? "Empty" : "InUse"),
			(list_empty(&dlm->purge_list) ? "Empty" : "InUse"),
			(list_empty(&dlm->pending_asts) ? "Empty" : "InUse"),
			(list_empty(&dlm->pending_basts) ? "Empty" : "InUse"));

	/* Purge Count: xxx  Refs: xxx */
	out += snprintf(db->buf + out, db->len - out,
			"Purge Count: %d  Refs: %d\n", dlm->purge_count,
			atomic_read(&dlm->dlm_refs.refcount));

	/* Dead Node: xxx */
	out += snprintf(db->buf + out, db->len - out,
			"Dead Node: %d\n", dlm->reco.dead_node);

	/* What about DLM_RECO_STATE_FINALIZE? */
	if (dlm->reco.state == DLM_RECO_STATE_ACTIVE)
		state = "ACTIVE";
	else
		state = "INACTIVE";

	/* Recovery Pid: xxxx  Master: xxx  State: xxxx */
	out += snprintf(db->buf + out, db->len - out,
			"Recovery Pid: %d  Master: %d  State: %s\n",
			dlm->dlm_reco_thread_task->pid,
			dlm->reco.new_master, state);

	/* Recovery Map: xx xx */
	out += snprintf(db->buf + out, db->len - out, "Recovery Map: ");
	out += stringify_nodemap(dlm->recovery_map, O2NM_MAX_NODES,
				 db->buf + out, db->len - out);
	out += snprintf(db->buf + out, db->len - out, "\n");

	/* Recovery Node State: */
	out += snprintf(db->buf + out, db->len - out, "Recovery Node State:\n");
	list_for_each_entry(node, &dlm->reco.node_data, list) {
		switch (node->state) {
		case DLM_RECO_NODE_DATA_INIT:
			state = "INIT";
			break;
		case DLM_RECO_NODE_DATA_REQUESTING:
			state = "REQUESTING";
			break;
		case DLM_RECO_NODE_DATA_DEAD:
			state = "DEAD";
			break;
		case DLM_RECO_NODE_DATA_RECEIVING:
			state = "RECEIVING";
			break;
		case DLM_RECO_NODE_DATA_REQUESTED:
			state = "REQUESTED";
			break;
		case DLM_RECO_NODE_DATA_DONE:
			state = "DONE";
			break;
		case DLM_RECO_NODE_DATA_FINALIZE_SENT:
			state = "FINALIZE-SENT";
			break;
		default:
			state = "BAD";
			break;
		}
		out += snprintf(db->buf + out, db->len - out, "\t%u - %s\n",
				node->node_num, state);
	}

	spin_unlock(&dlm->spinlock);

	return out;
}

static int debug_state_open(struct inode *inode, struct file *file)
{
	struct dlm_ctxt *dlm = inode->i_private;
	struct debug_buffer *db = NULL;

	db = debug_buffer_allocate();
	if (!db)
		goto bail;

	db->len = debug_state_print(dlm, db);

	file->private_data = db;

	return 0;
bail:
	return -ENOMEM;
}

static const struct file_operations debug_state_fops = {
	.open =		debug_state_open,
	.release =	debug_buffer_release,
	.read =		debug_buffer_read,
	.llseek =	debug_buffer_llseek,
};
/* end  - debug state funcs */

/* files in subroot */
int dlm_debug_init(struct dlm_ctxt *dlm)
{
	struct dlm_debug_ctxt *dc = dlm->dlm_debug_ctxt;

	/* for dumping dlm_ctxt */
	dc->debug_state_dentry = debugfs_create_file(DLM_DEBUGFS_DLM_STATE,
						     S_IFREG|S_IRUSR,
						     dlm->dlm_debugfs_subroot,
						     dlm, &debug_state_fops);
	if (!dc->debug_state_dentry) {
		mlog_errno(-ENOMEM);
		goto bail;
	}

	/* for dumping lockres */
	dc->debug_lockres_dentry =
			debugfs_create_file(DLM_DEBUGFS_LOCKING_STATE,
					    S_IFREG|S_IRUSR,
					    dlm->dlm_debugfs_subroot,
					    dlm, &debug_lockres_fops);
	if (!dc->debug_lockres_dentry) {
		mlog_errno(-ENOMEM);
		goto bail;
	}

	/* for dumping mles */
	dc->debug_mle_dentry = debugfs_create_file(DLM_DEBUGFS_MLE_STATE,
						   S_IFREG|S_IRUSR,
						   dlm->dlm_debugfs_subroot,
						   dlm, &debug_mle_fops);
	if (!dc->debug_mle_dentry) {
		mlog_errno(-ENOMEM);
		goto bail;
	}

	/* for dumping lockres on the purge list */
	dc->debug_purgelist_dentry =
			debugfs_create_file(DLM_DEBUGFS_PURGE_LIST,
					    S_IFREG|S_IRUSR,
					    dlm->dlm_debugfs_subroot,
					    dlm, &debug_purgelist_fops);
	if (!dc->debug_purgelist_dentry) {
		mlog_errno(-ENOMEM);
		goto bail;
	}

	dlm_debug_get(dc);
	return 0;

bail:
	dlm_debug_shutdown(dlm);
	return -ENOMEM;
}

void dlm_debug_shutdown(struct dlm_ctxt *dlm)
{
	struct dlm_debug_ctxt *dc = dlm->dlm_debug_ctxt;

	if (dc) {
		if (dc->debug_purgelist_dentry)
			debugfs_remove(dc->debug_purgelist_dentry);
		if (dc->debug_mle_dentry)
			debugfs_remove(dc->debug_mle_dentry);
		if (dc->debug_lockres_dentry)
			debugfs_remove(dc->debug_lockres_dentry);
		if (dc->debug_state_dentry)
			debugfs_remove(dc->debug_state_dentry);
		dlm_debug_put(dc);
	}
}

/* subroot - domain dir */
int dlm_create_debugfs_subroot(struct dlm_ctxt *dlm)
{
	dlm->dlm_debugfs_subroot = debugfs_create_dir(dlm->name,
						      dlm_debugfs_root);
	if (!dlm->dlm_debugfs_subroot) {
		mlog_errno(-ENOMEM);
		goto bail;
	}

	dlm->dlm_debug_ctxt = kzalloc(sizeof(struct dlm_debug_ctxt),
				      GFP_KERNEL);
	if (!dlm->dlm_debug_ctxt) {
		mlog_errno(-ENOMEM);
		goto bail;
	}
	kref_init(&dlm->dlm_debug_ctxt->debug_refcnt);

	return 0;
bail:
	dlm_destroy_debugfs_subroot(dlm);
	return -ENOMEM;
}

void dlm_destroy_debugfs_subroot(struct dlm_ctxt *dlm)
{
	if (dlm->dlm_debugfs_subroot)
		debugfs_remove(dlm->dlm_debugfs_subroot);
}

/* debugfs root */
int dlm_create_debugfs_root(void)
{
	dlm_debugfs_root = debugfs_create_dir(DLM_DEBUGFS_DIR, NULL);
	if (!dlm_debugfs_root) {
		mlog_errno(-ENOMEM);
		return -ENOMEM;
	}
	return 0;
}

void dlm_destroy_debugfs_root(void)
{
	if (dlm_debugfs_root)
		debugfs_remove(dlm_debugfs_root);
}
#endif	/* CONFIG_DEBUG_FS */
