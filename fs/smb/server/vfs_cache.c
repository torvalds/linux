// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2016 Namjae Jeon <linkinjeon@kernel.org>
 * Copyright (C) 2019 Samsung Electronics Co., Ltd.
 */

#include <linux/fs.h>
#include <linux/filelock.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/kthread.h>
#include <linux/freezer.h>
#include <linux/dcache.h>

#include "glob.h"
#include "vfs_cache.h"
#include "oplock.h"
#include "vfs.h"
#include "connection.h"
#include "misc.h"
#include "mgmt/tree_connect.h"
#include "mgmt/user_session.h"
#include "mgmt/user_config.h"
#include "smb_common.h"
#include "server.h"
#include "smb2pdu.h"

#define S_DEL_PENDING			1
#define S_DEL_ON_CLS			2
#define S_DEL_ON_CLS_STREAM		8

static unsigned int inode_hash_mask __read_mostly;
static unsigned int inode_hash_shift __read_mostly;
static struct hlist_head *inode_hashtable __read_mostly;
static DEFINE_RWLOCK(inode_hash_lock);

static struct ksmbd_file_table global_ft;
static atomic_long_t fd_limit;
static struct kmem_cache *filp_cache;

static int ksmbd_mark_fp_closed(struct ksmbd_file *fp);

#define OPLOCK_NONE      0
#define OPLOCK_EXCLUSIVE 1
#define OPLOCK_BATCH     2
#define OPLOCK_READ      3  /* level 2 oplock */

#ifdef CONFIG_PROC_FS

static const struct ksmbd_const_name ksmbd_lease_const_names[] = {
	{le32_to_cpu(SMB2_LEASE_NONE_LE), "LEASE_NONE"},
	{le32_to_cpu(SMB2_LEASE_READ_CACHING_LE), "LEASE_R"},
	{le32_to_cpu(SMB2_LEASE_HANDLE_CACHING_LE), "LEASE_H"},
	{le32_to_cpu(SMB2_LEASE_WRITE_CACHING_LE), "LEASE_W"},
	{le32_to_cpu(SMB2_LEASE_READ_CACHING_LE |
		     SMB2_LEASE_HANDLE_CACHING_LE), "LEASE_RH"},
	{le32_to_cpu(SMB2_LEASE_READ_CACHING_LE |
		     SMB2_LEASE_WRITE_CACHING_LE), "LEASE_RW"},
	{le32_to_cpu(SMB2_LEASE_HANDLE_CACHING_LE |
		     SMB2_LEASE_WRITE_CACHING_LE), "LEASE_WH"},
	{le32_to_cpu(SMB2_LEASE_READ_CACHING_LE |
		     SMB2_LEASE_HANDLE_CACHING_LE |
		     SMB2_LEASE_WRITE_CACHING_LE), "LEASE_RWH"},
};

static const struct ksmbd_const_name ksmbd_oplock_const_names[] = {
	{SMB2_OPLOCK_LEVEL_NONE, "OPLOCK_NONE"},
	{SMB2_OPLOCK_LEVEL_II, "OPLOCK_II"},
	{SMB2_OPLOCK_LEVEL_EXCLUSIVE, "OPLOCK_EXECL"},
	{SMB2_OPLOCK_LEVEL_BATCH, "OPLOCK_BATCH"},
};

static int proc_show_files(struct seq_file *m, void *v)
{
	struct ksmbd_file *fp = NULL;
	unsigned int id;
	struct oplock_info *opinfo;

	seq_printf(m, "#%-10s %-10s %-10s %-10s %-15s %-10s %-10s %s\n",
		   "<tree id>", "<pid>", "<vid>", "<refcnt>",
		   "<oplock>", "<daccess>", "<saccess>",
		   "<name>");

	read_lock(&global_ft.lock);
	idr_for_each_entry(global_ft.idr, fp, id) {
		seq_printf(m, "%#-10x %#-10llx %#-10llx %#-10x",
			   fp->tcon ? fp->tcon->id : 0,
			   fp->persistent_id,
			   fp->volatile_id,
			   atomic_read(&fp->refcount));

		rcu_read_lock();
		opinfo = rcu_dereference(fp->f_opinfo);
		if (opinfo) {
			const struct ksmbd_const_name *const_names;
			int count;
			unsigned int level;

			if (opinfo->is_lease) {
				const_names = ksmbd_lease_const_names;
				count = ARRAY_SIZE(ksmbd_lease_const_names);
				level = le32_to_cpu(opinfo->o_lease->state);
			} else {
				const_names = ksmbd_oplock_const_names;
				count = ARRAY_SIZE(ksmbd_oplock_const_names);
				level = opinfo->level;
			}
			rcu_read_unlock();
			ksmbd_proc_show_const_name(m, " %-15s",
						   const_names, count, level);
		} else {
			rcu_read_unlock();
			seq_printf(m, " %-15s", " ");
		}

		seq_printf(m, " %#010x %#010x %s\n",
			   le32_to_cpu(fp->daccess),
			   le32_to_cpu(fp->saccess),
			   fp->filp->f_path.dentry->d_name.name);
	}
	read_unlock(&global_ft.lock);
	return 0;
}

static int create_proc_files(void)
{
	ksmbd_proc_create("files", proc_show_files, NULL);
	return 0;
}
#else
static int create_proc_files(void) { return 0; }
#endif

static bool durable_scavenger_running;
static DEFINE_MUTEX(durable_scavenger_lock);
static wait_queue_head_t dh_wq;

void ksmbd_set_fd_limit(unsigned long limit)
{
	limit = min(limit, get_max_files());
	atomic_long_set(&fd_limit, limit);
}

static bool fd_limit_depleted(void)
{
	long v = atomic_long_dec_return(&fd_limit);

	if (v >= 0)
		return false;
	atomic_long_inc(&fd_limit);
	return true;
}

static void fd_limit_close(void)
{
	atomic_long_inc(&fd_limit);
}

/*
 * INODE hash
 */

static unsigned long inode_hash(struct super_block *sb, unsigned long hashval)
{
	unsigned long tmp;

	tmp = (hashval * (unsigned long)sb) ^ (GOLDEN_RATIO_PRIME + hashval) /
		L1_CACHE_BYTES;
	tmp = tmp ^ ((tmp ^ GOLDEN_RATIO_PRIME) >> inode_hash_shift);
	return tmp & inode_hash_mask;
}

static struct ksmbd_inode *__ksmbd_inode_lookup(struct dentry *de)
{
	struct hlist_head *head = inode_hashtable +
		inode_hash(d_inode(de)->i_sb, (unsigned long)de);
	struct ksmbd_inode *ci = NULL, *ret_ci = NULL;

	hlist_for_each_entry(ci, head, m_hash) {
		if (ci->m_de == de) {
			if (atomic_inc_not_zero(&ci->m_count))
				ret_ci = ci;
			break;
		}
	}
	return ret_ci;
}

static struct ksmbd_inode *ksmbd_inode_lookup(struct ksmbd_file *fp)
{
	return __ksmbd_inode_lookup(fp->filp->f_path.dentry);
}

struct ksmbd_inode *ksmbd_inode_lookup_lock(struct dentry *d)
{
	struct ksmbd_inode *ci;

	read_lock(&inode_hash_lock);
	ci = __ksmbd_inode_lookup(d);
	read_unlock(&inode_hash_lock);

	return ci;
}

int ksmbd_query_inode_status(struct dentry *dentry)
{
	struct ksmbd_inode *ci;
	int ret = KSMBD_INODE_STATUS_UNKNOWN;

	read_lock(&inode_hash_lock);
	ci = __ksmbd_inode_lookup(dentry);
	read_unlock(&inode_hash_lock);
	if (!ci)
		return ret;

	down_read(&ci->m_lock);
	if (ci->m_flags & S_DEL_PENDING)
		ret = KSMBD_INODE_STATUS_PENDING_DELETE;
	else
		ret = KSMBD_INODE_STATUS_OK;
	up_read(&ci->m_lock);

	ksmbd_inode_put(ci);
	return ret;
}

bool ksmbd_inode_pending_delete(struct ksmbd_file *fp)
{
	struct ksmbd_inode *ci = fp->f_ci;
	int ret;

	down_read(&ci->m_lock);
	ret = (ci->m_flags & S_DEL_PENDING);
	up_read(&ci->m_lock);

	return ret;
}

void ksmbd_set_inode_pending_delete(struct ksmbd_file *fp)
{
	struct ksmbd_inode *ci = fp->f_ci;

	down_write(&ci->m_lock);
	ci->m_flags |= S_DEL_PENDING;
	up_write(&ci->m_lock);
}

void ksmbd_clear_inode_pending_delete(struct ksmbd_file *fp)
{
	struct ksmbd_inode *ci = fp->f_ci;

	down_write(&ci->m_lock);
	ci->m_flags &= ~S_DEL_PENDING;
	up_write(&ci->m_lock);
}

bool ksmbd_has_stream_without_delete_share(struct ksmbd_file *fp)
{
	struct ksmbd_file *prev_fp;
	struct ksmbd_inode *ci = fp->f_ci;
	bool ret = false;

	if (ksmbd_stream_fd(fp))
		return false;

	down_read(&ci->m_lock);
	list_for_each_entry(prev_fp, &ci->m_fp_list, node) {
		if (prev_fp == fp || !ksmbd_stream_fd(prev_fp))
			continue;

		if (file_inode(fp->filp) != file_inode(prev_fp->filp))
			continue;

		if (!(prev_fp->saccess & FILE_SHARE_DELETE_LE)) {
			ret = true;
			break;
		}
	}
	up_read(&ci->m_lock);

	return ret;
}

void ksmbd_fd_set_delete_on_close(struct ksmbd_file *fp,
				  int file_info)
{
	struct ksmbd_inode *ci = fp->f_ci;

	down_write(&ci->m_lock);
	if (ksmbd_stream_fd(fp))
		ci->m_flags |= S_DEL_ON_CLS_STREAM;
	else
		ci->m_flags |= S_DEL_ON_CLS;
	up_write(&ci->m_lock);
}

static void ksmbd_inode_hash(struct ksmbd_inode *ci)
{
	struct hlist_head *b = inode_hashtable +
		inode_hash(d_inode(ci->m_de)->i_sb, (unsigned long)ci->m_de);

	hlist_add_head(&ci->m_hash, b);
}

static void ksmbd_inode_unhash(struct ksmbd_inode *ci)
{
	write_lock(&inode_hash_lock);
	hlist_del_init(&ci->m_hash);
	write_unlock(&inode_hash_lock);
}

static int ksmbd_inode_init(struct ksmbd_inode *ci, struct ksmbd_file *fp)
{
	atomic_set(&ci->m_count, 1);
	atomic_set(&ci->op_count, 0);
	atomic_set(&ci->sop_count, 0);
	ci->m_flags = 0;
	ci->m_fattr = 0;
	INIT_LIST_HEAD(&ci->m_fp_list);
	INIT_LIST_HEAD(&ci->m_op_list);
	init_rwsem(&ci->m_lock);
	ci->m_de = fp->filp->f_path.dentry;
	return 0;
}

static struct ksmbd_inode *ksmbd_inode_get(struct ksmbd_file *fp)
{
	struct ksmbd_inode *ci, *tmpci;
	int rc;

	read_lock(&inode_hash_lock);
	ci = ksmbd_inode_lookup(fp);
	read_unlock(&inode_hash_lock);
	if (ci)
		return ci;

	ci = kmalloc_obj(struct ksmbd_inode, KSMBD_DEFAULT_GFP);
	if (!ci)
		return NULL;

	rc = ksmbd_inode_init(ci, fp);
	if (rc) {
		pr_err("inode initialized failed\n");
		kfree(ci);
		return NULL;
	}

	write_lock(&inode_hash_lock);
	tmpci = ksmbd_inode_lookup(fp);
	if (!tmpci) {
		ksmbd_inode_hash(ci);
	} else {
		kfree(ci);
		ci = tmpci;
	}
	write_unlock(&inode_hash_lock);
	return ci;
}

static void ksmbd_inode_free(struct ksmbd_inode *ci)
{
	ksmbd_inode_unhash(ci);
	kfree(ci);
}

void ksmbd_inode_put(struct ksmbd_inode *ci)
{
	if (atomic_dec_and_test(&ci->m_count))
		ksmbd_inode_free(ci);
}

int __init ksmbd_inode_hash_init(void)
{
	unsigned int loop;
	unsigned long numentries = 16384;
	unsigned long bucketsize = sizeof(struct hlist_head);
	unsigned long size;

	inode_hash_shift = ilog2(numentries);
	inode_hash_mask = (1 << inode_hash_shift) - 1;

	size = bucketsize << inode_hash_shift;

	/* init master fp hash table */
	inode_hashtable = vmalloc(size);
	if (!inode_hashtable)
		return -ENOMEM;

	for (loop = 0; loop < (1U << inode_hash_shift); loop++)
		INIT_HLIST_HEAD(&inode_hashtable[loop]);
	return 0;
}

void ksmbd_release_inode_hash(void)
{
	vfree(inode_hashtable);
}

static void __ksmbd_inode_close(struct ksmbd_file *fp)
{
	struct ksmbd_inode *ci = fp->f_ci;
	int err;
	struct file *filp;

	filp = fp->filp;

	if (ksmbd_stream_fd(fp)) {
		bool remove_stream_xattr = false;

		down_write(&ci->m_lock);
		if (ci->m_flags & S_DEL_ON_CLS_STREAM) {
			ci->m_flags &= ~S_DEL_ON_CLS_STREAM;
			remove_stream_xattr = true;
		}
		up_write(&ci->m_lock);

		if (remove_stream_xattr) {
			const struct cred *saved_cred;

			saved_cred = override_creds(filp->f_cred);
			err = ksmbd_vfs_remove_xattr(file_mnt_idmap(filp),
						     &filp->f_path,
						     fp->stream.name,
						     true);
			revert_creds(saved_cred);
			if (err)
				pr_err("remove xattr failed : %s\n",
				       fp->stream.name);
		}
	}

	down_write(&ci->m_lock);
	/* Promote S_DEL_ON_CLS to S_DEL_PENDING when close */
	if (ci->m_flags & S_DEL_ON_CLS) {
		ci->m_flags &= ~S_DEL_ON_CLS;
		ci->m_flags |= S_DEL_PENDING;
	}
	up_write(&ci->m_lock);

	if (atomic_dec_and_test(&ci->m_count)) {
		bool do_unlink = false;

		down_write(&ci->m_lock);
		if (ci->m_flags & S_DEL_PENDING) {
			ci->m_flags &= ~S_DEL_PENDING;
			do_unlink = true;
		}
		up_write(&ci->m_lock);

		if (do_unlink)
			ksmbd_vfs_unlink(filp);

		ksmbd_inode_free(ci);
	}
}

static void __ksmbd_remove_durable_fd(struct ksmbd_file *fp)
{
	if (!has_file_id(fp->persistent_id))
		return;

	idr_remove(global_ft.idr, fp->persistent_id);
	/*
	 * Clear persistent_id so a later __ksmbd_close_fd() that runs from a
	 * delayed putter (e.g. when a concurrent ksmbd_lookup_fd_inode()
	 * walker held the final reference) does not re-issue idr_remove() on
	 * an id that idr_alloc_cyclic() may have already handed out to a new
	 * durable handle.
	 */
	fp->persistent_id = KSMBD_NO_FID;
}

static void ksmbd_remove_durable_fd(struct ksmbd_file *fp)
{
	write_lock(&global_ft.lock);
	__ksmbd_remove_durable_fd(fp);
	write_unlock(&global_ft.lock);
	if (waitqueue_active(&dh_wq))
		wake_up(&dh_wq);
}

static void __ksmbd_remove_fd(struct ksmbd_file_table *ft, struct ksmbd_file *fp)
{
	down_write(&fp->f_ci->m_lock);
	list_del_init(&fp->node);
	up_write(&fp->f_ci->m_lock);

	if (!has_file_id(fp->volatile_id))
		return;

	write_lock(&ft->lock);
	idr_remove(ft->idr, fp->volatile_id);
	write_unlock(&ft->lock);
}

static void __ksmbd_close_fd(struct ksmbd_file_table *ft, struct ksmbd_file *fp)
{
	struct file *filp;
	struct ksmbd_lock *smb_lock, *tmp_lock;

	fd_limit_close();
	ksmbd_remove_durable_fd(fp);
	if (ft)
		__ksmbd_remove_fd(ft, fp);

	close_id_del_oplock(fp);
	filp = fp->filp;

	__ksmbd_inode_close(fp);
	if (!IS_ERR_OR_NULL(filp))
		fput(filp);

	/* because the reference count of fp is 0, it is guaranteed that
	 * there are not accesses to fp->lock_list.
	 */
	list_for_each_entry_safe(smb_lock, tmp_lock, &fp->lock_list, flist) {
		struct ksmbd_conn *conn = smb_lock->conn;

		if (conn) {
			spin_lock(&conn->llist_lock);
			list_del_init(&smb_lock->clist);
			smb_lock->conn = NULL;
			spin_unlock(&conn->llist_lock);
			ksmbd_conn_put(conn);
		}

		list_del(&smb_lock->flist);
		locks_free_lock(smb_lock->fl);
		kfree(smb_lock);
	}

	/*
	 * Drop fp's strong reference on conn (taken in ksmbd_open_fd() /
	 * ksmbd_reopen_durable_fd()).  Durable fps that reached the
	 * scavenger have already had fp->conn cleared by session_fd_check(),
	 * in which case there is nothing to drop here.
	 */
	if (fp->conn) {
		ksmbd_conn_put(fp->conn);
		fp->conn = NULL;
	}

	if (ksmbd_stream_fd(fp))
		kfree(fp->stream.name);
	kfree(fp->owner.name);

	kmem_cache_free(filp_cache, fp);
}

/**
 * ksmbd_close_disconnected_durable_delete_on_close() - drop a delete-on-close
 *	file kept present only by disconnected durable handles
 * @dentry:	dentry of the file being opened
 *
 * A durable handle opened with delete-on-close is preserved across a
 * disconnect so it can be reclaimed by a durable reconnect.  When a new
 * (non-reconnect) open arrives for the same name instead, the disconnected
 * handle has to give way.  Close such handles so their delete-on-close is
 * applied and the file is removed once the last handle is gone, letting the
 * new open create a fresh file.
 *
 * The caller's inode reference is dropped before closing so that the final
 * close can promote S_DEL_ON_CLS to S_DEL_PENDING and unlink the file.
 *
 * Return:	true if a disconnected durable handle was closed.
 */
bool ksmbd_close_disconnected_durable_delete_on_close(struct dentry *dentry)
{
	struct ksmbd_inode *ci;
	struct ksmbd_file *fp, *tmp;
	LIST_HEAD(dispose);
	bool closed = false;

	ci = ksmbd_inode_lookup_lock(dentry);
	if (!ci)
		return false;

	down_write(&ci->m_lock);
	if (ci->m_flags & (S_DEL_ON_CLS | S_DEL_ON_CLS_STREAM | S_DEL_PENDING)) {
		list_for_each_entry_safe(fp, tmp, &ci->m_fp_list, node) {
			if (fp->conn || !fp->is_durable ||
			    fp->f_state != FP_INITED)
				continue;

			/*
			 * Claim the close before unlinking fp from m_fp_list.
			 * refcount == 1 means only the durable lifetime ref is
			 * left. Add a transient ref so final close can drop both.
			 */
			write_lock(&global_ft.lock);
			if (atomic_read(&fp->refcount) == 1) {
				atomic_inc(&fp->refcount);
				__ksmbd_remove_durable_fd(fp);
				ksmbd_mark_fp_closed(fp);
				list_move_tail(&fp->node, &dispose);
			}
			write_unlock(&global_ft.lock);
		}
	}
	up_write(&ci->m_lock);

	/*
	 * Drop our lookup reference before closing so the last __ksmbd_close_fd()
	 * can drop m_count to zero and unlink the delete-on-close file.  The
	 * collected handles still hold the transient reference taken above, so
	 * ci stays valid until they are closed below.
	 */
	ksmbd_inode_put(ci);

	while (!list_empty(&dispose)) {
		fp = list_first_entry(&dispose, struct ksmbd_file, node);
		list_del_init(&fp->node);
		if (atomic_sub_and_test(2, &fp->refcount)) {
			__ksmbd_close_fd(NULL, fp);
			closed = true;
		}
	}

	return closed;
}

static struct ksmbd_file *ksmbd_fp_get(struct ksmbd_file *fp)
{
	if (fp->f_state != FP_INITED)
		return NULL;

	if (!atomic_inc_not_zero(&fp->refcount))
		return NULL;
	return fp;
}

static struct ksmbd_file *__ksmbd_lookup_fd(struct ksmbd_file_table *ft,
					    u64 id)
{
	struct ksmbd_file *fp;

	if (!has_file_id(id))
		return NULL;

	read_lock(&ft->lock);
	fp = idr_find(ft->idr, id);
	if (fp)
		fp = ksmbd_fp_get(fp);
	read_unlock(&ft->lock);
	return fp;
}

static void __put_fd_final(struct ksmbd_work *work, struct ksmbd_file *fp)
{
	/*
	 * Detached durable fp -- session_fd_check() cleared fp->conn at
	 * preserve, so this fp is no longer tracked by any conn's
	 * stats.open_files_count.  This happens when
	 * ksmbd_scavenger_dispose_dh() hands the final close off to an
	 * m_fp_list walker (e.g. ksmbd_lookup_fd_inode()) whose work->conn
	 * is unrelated to the conn that originally opened the handle; close
	 * via the NULL-ft path so we do not underflow that unrelated
	 * counter.
	 */
	if (!fp->conn) {
		__ksmbd_close_fd(NULL, fp);
		return;
	}
	__ksmbd_close_fd(&work->sess->file_table, fp);
	atomic_dec(&work->conn->stats.open_files_count);
}

static void set_close_state_blocked_works(struct ksmbd_file *fp)
{
	struct ksmbd_work *cancel_work;

	spin_lock(&fp->f_lock);
	list_for_each_entry(cancel_work, &fp->blocked_works,
				 fp_entry) {
		cancel_work->state = KSMBD_WORK_CLOSED;
		cancel_work->cancel_fn(cancel_work->cancel_argv);
	}
	spin_unlock(&fp->f_lock);
}

int ksmbd_close_fd(struct ksmbd_work *work, u64 id)
{
	struct ksmbd_file	*fp;
	struct ksmbd_file_table	*ft;
	bool closed = false;

	if (!has_file_id(id))
		return 0;

	ft = &work->sess->file_table;
	write_lock(&ft->lock);
	fp = idr_find(ft->idr, id);
	if (fp) {
		set_close_state_blocked_works(fp);

		if (fp->f_state != FP_INITED)
			fp = NULL;
		else {
			fp->f_state = FP_CLOSED;
			closed = true;
			if (!atomic_dec_and_test(&fp->refcount))
				fp = NULL;
		}
	}
	write_unlock(&ft->lock);

	if (!fp)
		return closed ? 0 : -EINVAL;

	__put_fd_final(work, fp);
	return 0;
}

void ksmbd_fd_put(struct ksmbd_work *work, struct ksmbd_file *fp)
{
	if (!fp)
		return;

	if (!atomic_dec_and_test(&fp->refcount))
		return;
	__put_fd_final(work, fp);
}

static bool __sanity_check(struct ksmbd_tree_connect *tcon, struct ksmbd_file *fp)
{
	if (!fp)
		return false;
	if (fp->tcon != tcon)
		return false;
	return true;
}

struct ksmbd_file *ksmbd_lookup_foreign_fd(struct ksmbd_work *work, u64 id)
{
	return __ksmbd_lookup_fd(&work->sess->file_table, id);
}

struct ksmbd_file *ksmbd_lookup_fd_fast(struct ksmbd_work *work, u64 id)
{
	struct ksmbd_file *fp = __ksmbd_lookup_fd(&work->sess->file_table, id);

	if (__sanity_check(work->tcon, fp))
		return fp;

	ksmbd_fd_put(work, fp);
	return NULL;
}

struct ksmbd_file *ksmbd_lookup_fd_slow(struct ksmbd_work *work, u64 id,
					u64 pid)
{
	struct ksmbd_file *fp;

	if (!has_file_id(id)) {
		id = work->compound_fid;
		pid = work->compound_pfid;
	}

	fp = __ksmbd_lookup_fd(&work->sess->file_table, id);
	if (!__sanity_check(work->tcon, fp)) {
		ksmbd_fd_put(work, fp);
		return NULL;
	}
	if (fp->persistent_id != pid) {
		ksmbd_fd_put(work, fp);
		return NULL;
	}
	return fp;
}

struct ksmbd_file *ksmbd_lookup_global_fd(unsigned long long id)
{
	return __ksmbd_lookup_fd(&global_ft, id);
}

struct ksmbd_file *ksmbd_lookup_durable_fd(unsigned long long id)
{
	struct ksmbd_file *fp;

	fp = __ksmbd_lookup_fd(&global_ft, id);
	if (fp && (fp->durable_reconnect_disabled ||
		   fp->conn ||
		   (fp->durable_scavenger_timeout &&
		    (fp->durable_scavenger_timeout <
		     jiffies_to_msecs(jiffies))))) {
		ksmbd_put_durable_fd(fp);
		fp = NULL;
	}

	return fp;
}

void ksmbd_put_durable_fd(struct ksmbd_file *fp)
{
	if (!atomic_dec_and_test(&fp->refcount))
		return;

	__ksmbd_close_fd(NULL, fp);
}

bool ksmbd_has_other_active_fd(struct ksmbd_file *fp)
{
	struct ksmbd_file *lfp;
	struct ksmbd_inode *ci = fp->f_ci;
	bool ret = false;

	down_read(&ci->m_lock);
	list_for_each_entry(lfp, &ci->m_fp_list, node) {
		if (lfp == fp)
			continue;

		if (lfp->f_state == FP_INITED &&
		    (READ_ONCE(lfp->conn) || READ_ONCE(lfp->tcon))) {
			ret = true;
			break;
		}
	}
	up_read(&ci->m_lock);

	return ret;
}

static struct ksmbd_file *ksmbd_lookup_fd_app_instance_id(char *app_instance_id)
{
	struct ksmbd_file *fp = NULL;
	unsigned int id;

	if (!memchr_inv(app_instance_id, 0, SMB2_CREATE_GUID_SIZE))
		return NULL;

	read_lock(&global_ft.lock);
	idr_for_each_entry(global_ft.idr, fp, id) {
		if (!memcmp(fp->app_instance_id, app_instance_id,
			    SMB2_CREATE_GUID_SIZE)) {
			fp = ksmbd_fp_get(fp);
			break;
		}
	}
	read_unlock(&global_ft.lock);

	return fp;
}

int ksmbd_close_fd_app_instance_id(char *app_instance_id)
{
	struct ksmbd_file_table *ft;
	struct ksmbd_file *fp;
	struct oplock_info *opinfo;
	int n_to_drop = 0;

	fp = ksmbd_lookup_fd_app_instance_id(app_instance_id);
	if (!fp)
		return 0;

	opinfo = opinfo_get(fp);
	if (!opinfo)
		goto out;

	down_read(&fp->f_ci->m_lock);
	if (!opinfo->conn) {
		up_read(&fp->f_ci->m_lock);
		goto out;
	}

	ft = &opinfo->sess->file_table;
	write_lock(&ft->lock);
	if (fp->f_state == FP_INITED && has_file_id(fp->volatile_id)) {
		idr_remove(ft->idr, fp->volatile_id);
		fp->volatile_id = KSMBD_NO_FID;
		n_to_drop = ksmbd_mark_fp_closed(fp);
	}
	write_unlock(&ft->lock);
	up_read(&fp->f_ci->m_lock);
	opinfo_put(opinfo);
	opinfo = NULL;

	if (!n_to_drop)
		goto out;

	down_write(&fp->f_ci->m_lock);
	list_del_init(&fp->node);
	up_write(&fp->f_ci->m_lock);

	if (atomic_sub_and_test(n_to_drop, &fp->refcount)) {
		if (fp->conn)
			atomic_dec(&fp->conn->stats.open_files_count);
		__ksmbd_close_fd(NULL, fp);
	}
	return 0;

out:
	if (opinfo)
		opinfo_put(opinfo);
	ksmbd_put_durable_fd(fp);
	return 0;
}

int ksmbd_invalidate_durable_fd(unsigned long long id)
{
	struct ksmbd_file *fp;

	fp = ksmbd_lookup_global_fd(id);
	if (!fp)
		return -ENOENT;

	fp->durable_reconnect_disabled = true;

	if (fp->conn) {
		ksmbd_put_durable_fd(fp);
		return -ENOENT;
	}

	fp->durable_timeout = 1;
	fp->durable_scavenger_timeout = jiffies_to_msecs(jiffies);
	ksmbd_put_durable_fd(fp);
	if (waitqueue_active(&dh_wq))
		wake_up(&dh_wq);

	return -ENOENT;
}

struct ksmbd_file *ksmbd_lookup_fd_cguid(char *cguid)
{
	struct ksmbd_file	*fp = NULL;
	unsigned int		id;

	read_lock(&global_ft.lock);
	idr_for_each_entry(global_ft.idr, fp, id) {
		if (!memcmp(fp->create_guid,
			    cguid,
			    SMB2_CREATE_GUID_SIZE)) {
			fp = ksmbd_fp_get(fp);
			break;
		}
	}
	read_unlock(&global_ft.lock);

	return fp;
}

struct ksmbd_file *ksmbd_lookup_fd_inode(struct dentry *dentry)
{
	struct ksmbd_file	*lfp;
	struct ksmbd_inode	*ci;
	struct inode		*inode = d_inode(dentry);

	read_lock(&inode_hash_lock);
	ci = __ksmbd_inode_lookup(dentry);
	read_unlock(&inode_hash_lock);
	if (!ci)
		return NULL;

	down_read(&ci->m_lock);
	list_for_each_entry(lfp, &ci->m_fp_list, node) {
		if (inode == file_inode(lfp->filp)) {
			lfp = ksmbd_fp_get(lfp);
			up_read(&ci->m_lock);
			ksmbd_inode_put(ci);
			return lfp;
		}
	}
	up_read(&ci->m_lock);
	ksmbd_inode_put(ci);
	return NULL;
}

bool ksmbd_has_open_files(struct dentry *dentry)
{
	struct ksmbd_file *fp;
	unsigned int id;
	bool ret = false;

	read_lock(&global_ft.lock);
	idr_for_each_entry(global_ft.idr, fp, id) {
		struct dentry *fp_dentry = fp->filp->f_path.dentry;

		if (fp->f_state != FP_INITED)
			continue;
		if (fp_dentry == dentry)
			continue;
		if (is_subdir(fp_dentry, dentry)) {
			ret = true;
			break;
		}
	}
	read_unlock(&global_ft.lock);

	return ret;
}

#define OPEN_ID_TYPE_VOLATILE_ID	(0)
#define OPEN_ID_TYPE_PERSISTENT_ID	(1)

static void __open_id_set(struct ksmbd_file *fp, u64 id, int type)
{
	if (type == OPEN_ID_TYPE_VOLATILE_ID)
		fp->volatile_id = id;
	if (type == OPEN_ID_TYPE_PERSISTENT_ID)
		fp->persistent_id = id;
}

static int __open_id(struct ksmbd_file_table *ft, struct ksmbd_file *fp,
		     int type)
{
	u64			id = 0;
	int			ret;

	if (type == OPEN_ID_TYPE_VOLATILE_ID && fd_limit_depleted()) {
		__open_id_set(fp, KSMBD_NO_FID, type);
		return -EMFILE;
	}

	idr_preload(KSMBD_DEFAULT_GFP);
	write_lock(&ft->lock);
	ret = idr_alloc_cyclic(ft->idr, fp, KSMBD_START_FID, INT_MAX - 1,
			       GFP_NOWAIT);
	if (ret >= 0) {
		id = ret;
		ret = 0;
	} else {
		id = KSMBD_NO_FID;
		fd_limit_close();
	}

	__open_id_set(fp, id, type);
	write_unlock(&ft->lock);
	idr_preload_end();
	return ret;
}

unsigned int ksmbd_open_durable_fd(struct ksmbd_file *fp)
{
	__open_id(&global_ft, fp, OPEN_ID_TYPE_PERSISTENT_ID);
	return fp->persistent_id;
}

struct ksmbd_file *ksmbd_open_fd(struct ksmbd_work *work, struct file *filp)
{
	struct ksmbd_file *fp;
	int ret;

	fp = kmem_cache_zalloc(filp_cache, KSMBD_DEFAULT_GFP);
	if (!fp) {
		pr_err("Failed to allocate memory\n");
		return ERR_PTR(-ENOMEM);
	}

	INIT_LIST_HEAD(&fp->blocked_works);
	INIT_LIST_HEAD(&fp->node);
	INIT_LIST_HEAD(&fp->lock_list);
	spin_lock_init(&fp->f_lock);
	mutex_init(&fp->readdir_lock);
	atomic_set(&fp->refcount, 1);

	fp->filp		= filp;
	/*
	 * fp owns a strong reference on fp->conn for as long as fp->conn is
	 * non-NULL, so session_fd_check() and __ksmbd_close_fd() never
	 * dereference a dangling pointer.  Paired with ksmbd_conn_put() in
	 * session_fd_check() (durable preserve), in __ksmbd_close_fd()
	 * (final close), and on the error paths below.
	 */
	fp->conn		= ksmbd_conn_get(work->conn);
	fp->tcon		= work->tcon;
	fp->volatile_id		= KSMBD_NO_FID;
	fp->persistent_id	= KSMBD_NO_FID;
	fp->f_state		= FP_NEW;
	fp->f_ci		= ksmbd_inode_get(fp);

	if (!fp->f_ci) {
		ret = -ENOMEM;
		goto err_out;
	}

	ret = __open_id(&work->sess->file_table, fp, OPEN_ID_TYPE_VOLATILE_ID);
	if (ret) {
		ksmbd_inode_put(fp->f_ci);
		goto err_out;
	}

	atomic_inc(&work->conn->stats.open_files_count);
	return fp;

err_out:
	/* fp->conn was set and refcounted before every branch here. */
	ksmbd_conn_put(fp->conn);
	kmem_cache_free(filp_cache, fp);
	return ERR_PTR(ret);
}

/**
 * ksmbd_update_fstate() - update an fp state under the file-table lock
 * @ft: file table that publishes @fp's volatile id
 * @fp: file pointer to update
 * @state: new state
 *
 * Return: 0 on success.  The FP_NEW -> FP_INITED transition is special:
 * -ENOENT if teardown already unpublished @fp by advancing the state or
 * clearing the volatile id.  Other state updates preserve the historical
 * fire-and-forget behavior.
 */
int ksmbd_update_fstate(struct ksmbd_file_table *ft, struct ksmbd_file *fp,
			unsigned int state)
{
	int ret;

	if (!fp)
		return -ENOENT;

	write_lock(&ft->lock);
	if (state == FP_INITED &&
	    (fp->f_state != FP_NEW || !has_file_id(fp->volatile_id))) {
		ret = -ENOENT;
	} else {
		fp->f_state = state;
		ret = 0;
	}
	write_unlock(&ft->lock);

	return ret;
}

/*
 * ksmbd_mark_fp_closed() - mark fp closed under ft->lock and return how many
 * refs the teardown path owns.
 *
 * FP_INITED has a normal idr-owned reference, so teardown owns both that
 * reference and the transient lookup reference.  FP_NEW is still owned by the
 * in-flight opener/reopener, which will drop the original reference after
 * ksmbd_update_fstate(..., FP_INITED) observes the cleared volatile id.
 * FP_CLOSED on entry means an earlier ksmbd_close_fd() already consumed the
 * idr-owned ref.
 */
static int ksmbd_mark_fp_closed(struct ksmbd_file *fp)
{
	if (fp->f_state == FP_INITED) {
		set_close_state_blocked_works(fp);
		fp->f_state = FP_CLOSED;
		return 2;
	}

	return 1;
}

static int
__close_file_table_ids(struct ksmbd_session *sess,
		       struct ksmbd_tree_connect *tcon,
		       bool (*skip)(struct ksmbd_tree_connect *tcon,
				    struct ksmbd_file *fp,
				    struct ksmbd_user *user),
		       bool skip_preserves_fp)
{
	struct ksmbd_file_table *ft = &sess->file_table;
	struct ksmbd_file *fp;
	unsigned int id = 0;
	int num = 0;

	while (1) {
		int n_to_drop;

		write_lock(&ft->lock);
		fp = idr_get_next(ft->idr, &id);
		if (!fp) {
			write_unlock(&ft->lock);
			break;
		}
		if (!atomic_inc_not_zero(&fp->refcount)) {
			id++;
			write_unlock(&ft->lock);
			continue;
		}

		if (skip_preserves_fp) {
			/*
			 * Session teardown: skip() is session_fd_check(),
			 * which may sleep and mutates fp->conn / fp->tcon /
			 * fp->volatile_id when it chooses to preserve fp
			 * for durable reconnect.  Unpublish fp from the
			 * session idr here, under ft->lock, so that
			 * __ksmbd_lookup_fd() through this session cannot
			 * grant a new ksmbd_fp_get() reference to an fp
			 * whose fields are about to be rewritten outside
			 * the lock.  Durable reconnect still reaches fp via
			 * global_ft.
			 */
			idr_remove(ft->idr, id);
			fp->durable_volatile_id = fp->volatile_id;
			fp->volatile_id = KSMBD_NO_FID;
			write_unlock(&ft->lock);

			if (skip(tcon, fp, sess->user)) {
				/*
				 * session_fd_check() has converted fp to
				 * durable-preserve state and cleared its
				 * per-conn fields.  fp is already unpublished
				 * above; the original idr-owned ref keeps it
				 * alive for the durable scavenger.  Drop only
				 * the transient ref.  atomic_dec() is safe --
				 * atomic_inc_not_zero() succeeded on a
				 * positive value and we added one more, so
				 * refcount cannot be zero here.
				 */
				atomic_dec(&fp->refcount);
				id++;
				continue;
			}

			/*
			 * Keep the close-state decision under the same lock
			 * observed by ksmbd_update_fstate(), which is how an
			 * in-flight FP_NEW opener learns that teardown has
			 * cleared its volatile id.
			 */
			write_lock(&ft->lock);
			n_to_drop = ksmbd_mark_fp_closed(fp);
			write_unlock(&ft->lock);
		} else {
			/*
			 * Tree teardown: skip() is tree_conn_fd_check(), a
			 * cheap pointer compare that doesn't sleep and has
			 * no side effects, so keep the skip decision plus
			 * the unpublish-and-mark-closed sequence atomic
			 * under ft->lock.  fps belonging to other tree
			 * connects (skip() == true) stay fully published in
			 * the session idr with no lock window.
			 */
			if (skip(tcon, fp, sess->user)) {
				atomic_dec(&fp->refcount);
				write_unlock(&ft->lock);
				id++;
				continue;
			}
			idr_remove(ft->idr, id);
			fp->volatile_id = KSMBD_NO_FID;
			n_to_drop = ksmbd_mark_fp_closed(fp);
			write_unlock(&ft->lock);
		}

		/*
		 * fp->volatile_id is already cleared to prevent stale idr
		 * removal from a deferred final close.  Remove fp from
		 * m_fp_list here because __ksmbd_remove_fd() will skip the
		 * list unlink when volatile_id is KSMBD_NO_FID.
		 */
		down_write(&fp->f_ci->m_lock);
		list_del_init(&fp->node);
		up_write(&fp->f_ci->m_lock);

		/*
		 * Drop the references this iteration owns:
		 *
		 *   n_to_drop == 2: we observed FP_INITED and committed
		 *     the FP_CLOSED transition ourselves, so we own the
		 *     transient (+1) and the still-intact idr-owned ref.
		 *
		 *   n_to_drop == 1: either a prior ksmbd_close_fd()
		 *     already consumed the idr-owned ref, or fp was still
		 *     FP_NEW and the in-flight opener/reopener must keep
		 *     the original reference until ksmbd_update_fstate()
		 *     observes the cleared volatile id.
		 *
		 * If we end up as the final putter, finalize fp and
		 * account the open_files_count decrement via the caller's
		 * atomic_sub(num, ...).  Otherwise the remaining user's
		 * ksmbd_fd_put() reaches __put_fd_final(), which does its
		 * own atomic_dec(&open_files_count), so we must not count
		 * this fp here -- doing so would double-decrement the
		 * connection-wide counter.
		 */
		if (atomic_sub_and_test(n_to_drop, &fp->refcount)) {
			__ksmbd_close_fd(NULL, fp);
			num++;
		}
		id++;
	}

	return num;
}

static inline bool is_reconnectable(struct ksmbd_file *fp)
{
	struct oplock_info *opinfo = opinfo_get(fp);
	bool reconn = false;

	if (!opinfo)
		return false;

	if (opinfo->op_state != OPLOCK_STATE_NONE) {
		opinfo_put(opinfo);
		return false;
	}

	if (fp->is_resilient || fp->is_persistent)
		reconn = true;
	else if (fp->is_durable && opinfo->is_lease &&
		 opinfo->o_lease->state & SMB2_LEASE_HANDLE_CACHING_LE)
		reconn = true;

	else if (fp->is_durable && opinfo->level == SMB2_OPLOCK_LEVEL_BATCH)
		reconn = true;

	opinfo_put(opinfo);
	return reconn;
}

static bool tree_conn_fd_check(struct ksmbd_tree_connect *tcon,
			       struct ksmbd_file *fp,
			       struct ksmbd_user *user)
{
	return fp->tcon != tcon;
}

static bool ksmbd_durable_scavenger_alive(void)
{
	if (!durable_scavenger_running)
		return false;

	if (kthread_should_stop())
		return false;

	if (idr_is_empty(global_ft.idr))
		return false;

	return true;
}

static void ksmbd_scavenger_dispose_dh(struct ksmbd_file *fp)
{
	/*
	 * Durable-preserved fp can remain linked on f_ci->m_fp_list for
	 * share-mode checks.  Unlink it before final close; fp->node is not
	 * available as a scavenger-private list node because re-adding it to
	 * another list corrupts m_fp_list.
	 */
	down_write(&fp->f_ci->m_lock);
	list_del_init(&fp->node);
	up_write(&fp->f_ci->m_lock);

	/*
	 * Drop both the durable lifetime reference and the transient reference
	 * taken by the scavenger under global_ft.lock.  If a concurrent
	 * ksmbd_lookup_fd_inode() (or any other m_fp_list walker) snatched fp
	 * before the unlink above, that holder owns the final close via
	 * ksmbd_fd_put() -> __ksmbd_close_fd().  Otherwise the scavenger is
	 * the last putter and finalises fp here.
	 */
	if (atomic_sub_and_test(2, &fp->refcount))
		__ksmbd_close_fd(NULL, fp);
}

static int ksmbd_durable_scavenger(void *dummy)
{
	struct ksmbd_file *fp = NULL;
	struct ksmbd_file *expired_fp;
	unsigned int id;
	unsigned int min_timeout = 1;
	bool found_fp_timeout;
	unsigned long remaining_jiffies;

	__module_get(THIS_MODULE);

	set_freezable();
	while (ksmbd_durable_scavenger_alive()) {
		if (try_to_freeze())
			continue;

		remaining_jiffies = wait_event_interruptible_timeout(dh_wq,
				   ksmbd_durable_scavenger_alive() == false,
				   __msecs_to_jiffies(min_timeout));
		if ((long)remaining_jiffies > 0)
			min_timeout = jiffies_to_msecs(remaining_jiffies);
		else
			min_timeout = DURABLE_HANDLE_MAX_TIMEOUT;

		do {
			expired_fp = NULL;
			found_fp_timeout = false;

			write_lock(&global_ft.lock);
			idr_for_each_entry(global_ft.idr, fp, id) {
				unsigned long durable_timeout;

				if (!fp->durable_timeout)
					continue;

				if (atomic_read(&fp->refcount) > 1 ||
				    fp->conn)
					continue;

				found_fp_timeout = true;
				if (fp->durable_scavenger_timeout <=
				    jiffies_to_msecs(jiffies)) {
					__ksmbd_remove_durable_fd(fp);
					/*
					 * Take a transient reference so fp
					 * cannot be freed by an in-flight
					 * ksmbd_lookup_fd_inode() that found
					 * it through f_ci->m_fp_list while we
					 * drop global_ft.lock and reach the
					 * m_fp_list unlink in
					 * ksmbd_scavenger_dispose_dh().
					 */
					atomic_inc(&fp->refcount);
					expired_fp = fp;
					break;
				}

				durable_timeout =
					fp->durable_scavenger_timeout -
						jiffies_to_msecs(jiffies);

				if (min_timeout > durable_timeout)
					min_timeout = durable_timeout;
			}
			write_unlock(&global_ft.lock);

			if (expired_fp)
				ksmbd_scavenger_dispose_dh(expired_fp);
		} while (expired_fp);

		if (found_fp_timeout == false)
			break;
	}

	durable_scavenger_running = false;

	module_put(THIS_MODULE);

	return 0;
}

void ksmbd_launch_ksmbd_durable_scavenger(void)
{
	if (!(server_conf.flags & KSMBD_GLOBAL_FLAG_DURABLE_HANDLE))
		return;

	mutex_lock(&durable_scavenger_lock);
	if (durable_scavenger_running == true) {
		mutex_unlock(&durable_scavenger_lock);
		return;
	}

	durable_scavenger_running = true;

	server_conf.dh_task = kthread_run(ksmbd_durable_scavenger,
				     (void *)NULL, "ksmbd-durable-scavenger");
	if (IS_ERR(server_conf.dh_task))
		pr_err("cannot start conn thread, err : %ld\n",
		       PTR_ERR(server_conf.dh_task));
	mutex_unlock(&durable_scavenger_lock);
}

void ksmbd_stop_durable_scavenger(void)
{
	if (!(server_conf.flags & KSMBD_GLOBAL_FLAG_DURABLE_HANDLE))
		return;

	mutex_lock(&durable_scavenger_lock);
	if (!durable_scavenger_running) {
		mutex_unlock(&durable_scavenger_lock);
		return;
	}

	durable_scavenger_running = false;
	if (waitqueue_active(&dh_wq))
		wake_up(&dh_wq);
	mutex_unlock(&durable_scavenger_lock);
	kthread_stop(server_conf.dh_task);
}

/*
 * ksmbd_vfs_copy_durable_owner - Copy owner info for durable reconnect
 * @fp: ksmbd file pointer to store owner info
 * @user: user pointer to copy from
 *
 * This function binds the current user's identity to the file handle
 * to satisfy MS-SMB2 Step 8 (SecurityContext matching) during reconnect.
 *
 * Return: 0 on success, or negative error code on failure
 */
static int ksmbd_vfs_copy_durable_owner(struct ksmbd_file *fp,
		struct ksmbd_user *user)
{
	char *name;

	if (!user)
		return -EINVAL;

	/* Duplicate the user name to ensure identity persistence */
	name = kstrdup(user->name, GFP_KERNEL);
	if (!name)
		return -ENOMEM;

	spin_lock(&fp->f_lock);
	fp->owner.uid = user->uid;
	fp->owner.gid = user->gid;
	fp->owner.name = name;
	spin_unlock(&fp->f_lock);

	return 0;
}

/**
 * ksmbd_vfs_compare_durable_owner - Verify if the requester is original owner
 * @fp: existing ksmbd file pointer
 * @user: user pointer of the reconnect requester
 *
 * Compares the UID, GID, and name of the current requester against the
 * original owner stored in the file handle.
 *
 * Return: true if the user matches, false otherwise
 */
bool ksmbd_vfs_compare_durable_owner(struct ksmbd_file *fp,
		struct ksmbd_user *user)
{
	bool ret = false;

	if (!user)
		return false;

	spin_lock(&fp->f_lock);
	if (!fp->owner.name)
		goto out;

	/* Check if the UID and GID match first (fast path) */
	if (fp->owner.uid != user->uid || fp->owner.gid != user->gid)
		goto out;

	/* Validate the account name to ensure the same SecurityContext */
	ret = (strcmp(fp->owner.name, user->name) == 0);
out:
	spin_unlock(&fp->f_lock);
	return ret;
}

static bool session_fd_check(struct ksmbd_tree_connect *tcon,
			     struct ksmbd_file *fp, struct ksmbd_user *user)
{
	struct ksmbd_inode *ci;
	struct oplock_info *op;
	struct ksmbd_conn *conn;
	struct ksmbd_lock *smb_lock, *tmp_lock;

	if (!is_reconnectable(fp))
		return false;

	if (fp->f_state != FP_INITED)
		return false;

	if (WARN_ON_ONCE(!fp->conn))
		return false;

	if (ksmbd_vfs_copy_durable_owner(fp, user))
		return false;

	/*
	 * fp owns a strong reference on fp->conn (taken in ksmbd_open_fd()
	 * / ksmbd_reopen_durable_fd()), so conn stays valid for the whole
	 * body of this function regardless of any op->conn puts below.
	 */
	conn = fp->conn;
	ci = fp->f_ci;
	down_write(&ci->m_lock);
	list_for_each_entry_rcu(op, &ci->m_op_list, op_entry,
				lockdep_is_held(&ci->m_lock)) {
		if (op->conn != conn)
			continue;
		ksmbd_conn_put(op->conn);
		op->conn = NULL;
		op->sess = NULL;
	}
	up_write(&ci->m_lock);

	list_for_each_entry_safe(smb_lock, tmp_lock, &fp->lock_list, flist) {
		struct ksmbd_conn *lock_conn = smb_lock->conn;

		if (!lock_conn)
			continue;
		spin_lock(&lock_conn->llist_lock);
		list_del_init(&smb_lock->clist);
		smb_lock->conn = NULL;
		spin_unlock(&lock_conn->llist_lock);
		ksmbd_conn_put(lock_conn);
	}

	fp->conn = NULL;
	fp->tcon = NULL;
	fp->volatile_id = KSMBD_NO_FID;

	if (fp->durable_timeout)
		fp->durable_scavenger_timeout =
			jiffies_to_msecs(jiffies) + fp->durable_timeout;

	/* Drop fp's own reference on conn. */
	ksmbd_conn_put(conn);
	return true;
}

void ksmbd_close_tree_conn_fds(struct ksmbd_work *work)
{
	int num = __close_file_table_ids(work->sess,
					 work->tcon,
					 tree_conn_fd_check,
					 false);

	atomic_sub(num, &work->conn->stats.open_files_count);
}

void ksmbd_close_session_fds(struct ksmbd_work *work)
{
	int num = __close_file_table_ids(work->sess,
					 work->tcon,
					 session_fd_check,
					 true);

	atomic_sub(num, &work->conn->stats.open_files_count);
}

int ksmbd_init_global_file_table(void)
{
	create_proc_files();
	return ksmbd_init_file_table(&global_ft);
}

void ksmbd_free_global_file_table(void)
{
	struct ksmbd_file	*fp = NULL;
	unsigned int		id;

	idr_for_each_entry(global_ft.idr, fp, id) {
		ksmbd_remove_durable_fd(fp);
		__ksmbd_close_fd(NULL, fp);
	}

	idr_destroy(global_ft.idr);
	kfree(global_ft.idr);
}

int ksmbd_validate_name_reconnect(struct ksmbd_share_config *share,
				  struct ksmbd_file *fp, char *name)
{
	char *pathname, *ab_pathname;
	int ret = 0;

	pathname = kmalloc(PATH_MAX, KSMBD_DEFAULT_GFP);
	if (!pathname)
		return -EACCES;

	ab_pathname = d_path(&fp->filp->f_path, pathname, PATH_MAX);
	if (IS_ERR(ab_pathname)) {
		kfree(pathname);
		return -EACCES;
	}

	if (name && strcmp(&ab_pathname[share->path_sz + 1], name)) {
		ksmbd_debug(SMB, "invalid name reconnect %s\n", name);
		ret = -EINVAL;
	}

	kfree(pathname);

	return ret;
}

int ksmbd_reopen_durable_fd(struct ksmbd_work *work, struct ksmbd_file *fp)
{
	struct ksmbd_inode *ci;
	struct oplock_info *op;
	struct ksmbd_conn *conn = work->conn;
	struct ksmbd_lock *smb_lock;
	unsigned int old_f_state;

	write_lock(&global_ft.lock);
	if (!fp->is_durable || fp->conn || fp->tcon) {
		write_unlock(&global_ft.lock);
		pr_err("Invalid durable fd [%p:%p]\n", fp->conn, fp->tcon);
		return -EBADF;
	}

	if (has_file_id(fp->volatile_id)) {
		write_unlock(&global_ft.lock);
		pr_err("Still in use durable fd: %llu\n", fp->volatile_id);
		return -EBADF;
	}

	/*
	 * Initialize fp's connection binding before publishing fp into the
	 * session's file table.  If __open_id() is ordered first, a
	 * concurrent teardown that iterates the table can observe a valid
	 * volatile_id with fp->conn == NULL and preserve a
	 * partially-initialized fp.  fp owns a strong reference on the new
	 * conn (see ksmbd_open_fd()); undo it on __open_id() failure.
	 */
	fp->conn = ksmbd_conn_get(conn);
	fp->tcon = work->tcon;
	write_unlock(&global_ft.lock);

	old_f_state = fp->f_state;
	fp->f_state = FP_NEW;

	__open_id(&work->sess->file_table, fp, OPEN_ID_TYPE_VOLATILE_ID);
	if (!has_file_id(fp->volatile_id)) {
		write_lock(&global_ft.lock);
		fp->conn = NULL;
		fp->tcon = NULL;
		write_unlock(&global_ft.lock);
		ksmbd_conn_put(conn);
		fp->f_state = old_f_state;
		return -EBADF;
	}

	list_for_each_entry(smb_lock, &fp->lock_list, flist) {
		smb_lock->conn = ksmbd_conn_get(conn);
		spin_lock(&conn->llist_lock);
		list_add_tail(&smb_lock->clist, &conn->lock_list);
		spin_unlock(&conn->llist_lock);
	}

	ci = fp->f_ci;
	down_write(&ci->m_lock);
	list_for_each_entry_rcu(op, &ci->m_op_list, op_entry,
				lockdep_is_held(&ci->m_lock)) {
		if (op->conn)
			continue;
		op->conn = ksmbd_conn_get(fp->conn);
		op->sess = work->sess;
	}
	up_write(&ci->m_lock);

	spin_lock(&fp->f_lock);
	fp->owner.uid = fp->owner.gid = 0;
	kfree(fp->owner.name);
	fp->owner.name = NULL;
	spin_unlock(&fp->f_lock);

	return 0;
}

int ksmbd_init_file_table(struct ksmbd_file_table *ft)
{
	ft->idr = kzalloc_obj(struct idr, KSMBD_DEFAULT_GFP);
	if (!ft->idr)
		return -ENOMEM;

	idr_init(ft->idr);
	rwlock_init(&ft->lock);
	return 0;
}

void ksmbd_destroy_file_table(struct ksmbd_session *sess)
{
	struct ksmbd_file_table *ft = &sess->file_table;

	if (!ft->idr)
		return;

	__close_file_table_ids(sess, NULL, session_fd_check, true);
	idr_destroy(ft->idr);
	kfree(ft->idr);
	ft->idr = NULL;
}

int ksmbd_init_file_cache(void)
{
	filp_cache = kmem_cache_create("ksmbd_file_cache",
				       sizeof(struct ksmbd_file), 0,
				       SLAB_HWCACHE_ALIGN, NULL);
	if (!filp_cache)
		goto out;

	init_waitqueue_head(&dh_wq);

	return 0;

out:
	pr_err("failed to allocate file cache\n");
	return -ENOMEM;
}

void ksmbd_exit_file_cache(void)
{
	kmem_cache_destroy(filp_cache);
}
