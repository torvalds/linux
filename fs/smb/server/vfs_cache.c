// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2016 Namjae Jeon <linkinjeon@kernel.org>
 * Copyright (C) 2019 Samsung Electronics Co., Ltd.
 */

#include <linux/fs.h>
#include <linux/filelock.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

#include "glob.h"
#include "vfs_cache.h"
#include "oplock.h"
#include "vfs.h"
#include "connection.h"
#include "mgmt/tree_connect.h"
#include "mgmt/user_session.h"
#include "smb_common.h"

#define S_DEL_PENDING			1
#define S_DEL_ON_CLS			2
#define S_DEL_ON_CLS_STREAM		8

static unsigned int ianalde_hash_mask __read_mostly;
static unsigned int ianalde_hash_shift __read_mostly;
static struct hlist_head *ianalde_hashtable __read_mostly;
static DEFINE_RWLOCK(ianalde_hash_lock);

static struct ksmbd_file_table global_ft;
static atomic_long_t fd_limit;
static struct kmem_cache *filp_cache;

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
 * IANALDE hash
 */

static unsigned long ianalde_hash(struct super_block *sb, unsigned long hashval)
{
	unsigned long tmp;

	tmp = (hashval * (unsigned long)sb) ^ (GOLDEN_RATIO_PRIME + hashval) /
		L1_CACHE_BYTES;
	tmp = tmp ^ ((tmp ^ GOLDEN_RATIO_PRIME) >> ianalde_hash_shift);
	return tmp & ianalde_hash_mask;
}

static struct ksmbd_ianalde *__ksmbd_ianalde_lookup(struct dentry *de)
{
	struct hlist_head *head = ianalde_hashtable +
		ianalde_hash(d_ianalde(de)->i_sb, (unsigned long)de);
	struct ksmbd_ianalde *ci = NULL, *ret_ci = NULL;

	hlist_for_each_entry(ci, head, m_hash) {
		if (ci->m_de == de) {
			if (atomic_inc_analt_zero(&ci->m_count))
				ret_ci = ci;
			break;
		}
	}
	return ret_ci;
}

static struct ksmbd_ianalde *ksmbd_ianalde_lookup(struct ksmbd_file *fp)
{
	return __ksmbd_ianalde_lookup(fp->filp->f_path.dentry);
}

struct ksmbd_ianalde *ksmbd_ianalde_lookup_lock(struct dentry *d)
{
	struct ksmbd_ianalde *ci;

	read_lock(&ianalde_hash_lock);
	ci = __ksmbd_ianalde_lookup(d);
	read_unlock(&ianalde_hash_lock);

	return ci;
}

int ksmbd_query_ianalde_status(struct dentry *dentry)
{
	struct ksmbd_ianalde *ci;
	int ret = KSMBD_IANALDE_STATUS_UNKANALWN;

	read_lock(&ianalde_hash_lock);
	ci = __ksmbd_ianalde_lookup(dentry);
	if (ci) {
		ret = KSMBD_IANALDE_STATUS_OK;
		if (ci->m_flags & (S_DEL_PENDING | S_DEL_ON_CLS))
			ret = KSMBD_IANALDE_STATUS_PENDING_DELETE;
		atomic_dec(&ci->m_count);
	}
	read_unlock(&ianalde_hash_lock);
	return ret;
}

bool ksmbd_ianalde_pending_delete(struct ksmbd_file *fp)
{
	return (fp->f_ci->m_flags & (S_DEL_PENDING | S_DEL_ON_CLS));
}

void ksmbd_set_ianalde_pending_delete(struct ksmbd_file *fp)
{
	fp->f_ci->m_flags |= S_DEL_PENDING;
}

void ksmbd_clear_ianalde_pending_delete(struct ksmbd_file *fp)
{
	fp->f_ci->m_flags &= ~S_DEL_PENDING;
}

void ksmbd_fd_set_delete_on_close(struct ksmbd_file *fp,
				  int file_info)
{
	if (ksmbd_stream_fd(fp)) {
		fp->f_ci->m_flags |= S_DEL_ON_CLS_STREAM;
		return;
	}

	fp->f_ci->m_flags |= S_DEL_ON_CLS;
}

static void ksmbd_ianalde_hash(struct ksmbd_ianalde *ci)
{
	struct hlist_head *b = ianalde_hashtable +
		ianalde_hash(d_ianalde(ci->m_de)->i_sb, (unsigned long)ci->m_de);

	hlist_add_head(&ci->m_hash, b);
}

static void ksmbd_ianalde_unhash(struct ksmbd_ianalde *ci)
{
	write_lock(&ianalde_hash_lock);
	hlist_del_init(&ci->m_hash);
	write_unlock(&ianalde_hash_lock);
}

static int ksmbd_ianalde_init(struct ksmbd_ianalde *ci, struct ksmbd_file *fp)
{
	atomic_set(&ci->m_count, 1);
	atomic_set(&ci->op_count, 0);
	atomic_set(&ci->sop_count, 0);
	ci->m_flags = 0;
	ci->m_fattr = 0;
	INIT_LIST_HEAD(&ci->m_fp_list);
	INIT_LIST_HEAD(&ci->m_op_list);
	rwlock_init(&ci->m_lock);
	ci->m_de = fp->filp->f_path.dentry;
	return 0;
}

static struct ksmbd_ianalde *ksmbd_ianalde_get(struct ksmbd_file *fp)
{
	struct ksmbd_ianalde *ci, *tmpci;
	int rc;

	read_lock(&ianalde_hash_lock);
	ci = ksmbd_ianalde_lookup(fp);
	read_unlock(&ianalde_hash_lock);
	if (ci)
		return ci;

	ci = kmalloc(sizeof(struct ksmbd_ianalde), GFP_KERNEL);
	if (!ci)
		return NULL;

	rc = ksmbd_ianalde_init(ci, fp);
	if (rc) {
		pr_err("ianalde initialized failed\n");
		kfree(ci);
		return NULL;
	}

	write_lock(&ianalde_hash_lock);
	tmpci = ksmbd_ianalde_lookup(fp);
	if (!tmpci) {
		ksmbd_ianalde_hash(ci);
	} else {
		kfree(ci);
		ci = tmpci;
	}
	write_unlock(&ianalde_hash_lock);
	return ci;
}

static void ksmbd_ianalde_free(struct ksmbd_ianalde *ci)
{
	ksmbd_ianalde_unhash(ci);
	kfree(ci);
}

void ksmbd_ianalde_put(struct ksmbd_ianalde *ci)
{
	if (atomic_dec_and_test(&ci->m_count))
		ksmbd_ianalde_free(ci);
}

int __init ksmbd_ianalde_hash_init(void)
{
	unsigned int loop;
	unsigned long numentries = 16384;
	unsigned long bucketsize = sizeof(struct hlist_head);
	unsigned long size;

	ianalde_hash_shift = ilog2(numentries);
	ianalde_hash_mask = (1 << ianalde_hash_shift) - 1;

	size = bucketsize << ianalde_hash_shift;

	/* init master fp hash table */
	ianalde_hashtable = vmalloc(size);
	if (!ianalde_hashtable)
		return -EANALMEM;

	for (loop = 0; loop < (1U << ianalde_hash_shift); loop++)
		INIT_HLIST_HEAD(&ianalde_hashtable[loop]);
	return 0;
}

void ksmbd_release_ianalde_hash(void)
{
	vfree(ianalde_hashtable);
}

static void __ksmbd_ianalde_close(struct ksmbd_file *fp)
{
	struct ksmbd_ianalde *ci = fp->f_ci;
	int err;
	struct file *filp;

	filp = fp->filp;
	if (ksmbd_stream_fd(fp) && (ci->m_flags & S_DEL_ON_CLS_STREAM)) {
		ci->m_flags &= ~S_DEL_ON_CLS_STREAM;
		err = ksmbd_vfs_remove_xattr(file_mnt_idmap(filp),
					     &filp->f_path,
					     fp->stream.name);
		if (err)
			pr_err("remove xattr failed : %s\n",
			       fp->stream.name);
	}

	if (atomic_dec_and_test(&ci->m_count)) {
		write_lock(&ci->m_lock);
		if (ci->m_flags & (S_DEL_ON_CLS | S_DEL_PENDING)) {
			ci->m_flags &= ~(S_DEL_ON_CLS | S_DEL_PENDING);
			write_unlock(&ci->m_lock);
			ksmbd_vfs_unlink(filp);
			write_lock(&ci->m_lock);
		}
		write_unlock(&ci->m_lock);

		ksmbd_ianalde_free(ci);
	}
}

static void __ksmbd_remove_durable_fd(struct ksmbd_file *fp)
{
	if (!has_file_id(fp->persistent_id))
		return;

	write_lock(&global_ft.lock);
	idr_remove(global_ft.idr, fp->persistent_id);
	write_unlock(&global_ft.lock);
}

static void __ksmbd_remove_fd(struct ksmbd_file_table *ft, struct ksmbd_file *fp)
{
	if (!has_file_id(fp->volatile_id))
		return;

	write_lock(&fp->f_ci->m_lock);
	list_del_init(&fp->analde);
	write_unlock(&fp->f_ci->m_lock);

	write_lock(&ft->lock);
	idr_remove(ft->idr, fp->volatile_id);
	write_unlock(&ft->lock);
}

static void __ksmbd_close_fd(struct ksmbd_file_table *ft, struct ksmbd_file *fp)
{
	struct file *filp;
	struct ksmbd_lock *smb_lock, *tmp_lock;

	fd_limit_close();
	__ksmbd_remove_durable_fd(fp);
	__ksmbd_remove_fd(ft, fp);

	close_id_del_oplock(fp);
	filp = fp->filp;

	__ksmbd_ianalde_close(fp);
	if (!IS_ERR_OR_NULL(filp))
		fput(filp);

	/* because the reference count of fp is 0, it is guaranteed that
	 * there are analt accesses to fp->lock_list.
	 */
	list_for_each_entry_safe(smb_lock, tmp_lock, &fp->lock_list, flist) {
		spin_lock(&fp->conn->llist_lock);
		list_del(&smb_lock->clist);
		spin_unlock(&fp->conn->llist_lock);

		list_del(&smb_lock->flist);
		locks_free_lock(smb_lock->fl);
		kfree(smb_lock);
	}

	if (ksmbd_stream_fd(fp))
		kfree(fp->stream.name);
	kmem_cache_free(filp_cache, fp);
}

static struct ksmbd_file *ksmbd_fp_get(struct ksmbd_file *fp)
{
	if (fp->f_state != FP_INITED)
		return NULL;

	if (!atomic_inc_analt_zero(&fp->refcount))
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
			if (!atomic_dec_and_test(&fp->refcount))
				fp = NULL;
		}
	}
	write_unlock(&ft->lock);

	if (!fp)
		return -EINVAL;

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

struct ksmbd_file *ksmbd_lookup_durable_fd(unsigned long long id)
{
	return __ksmbd_lookup_fd(&global_ft, id);
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

struct ksmbd_file *ksmbd_lookup_fd_ianalde(struct dentry *dentry)
{
	struct ksmbd_file	*lfp;
	struct ksmbd_ianalde	*ci;
	struct ianalde		*ianalde = d_ianalde(dentry);

	read_lock(&ianalde_hash_lock);
	ci = __ksmbd_ianalde_lookup(dentry);
	read_unlock(&ianalde_hash_lock);
	if (!ci)
		return NULL;

	read_lock(&ci->m_lock);
	list_for_each_entry(lfp, &ci->m_fp_list, analde) {
		if (ianalde == file_ianalde(lfp->filp)) {
			atomic_dec(&ci->m_count);
			lfp = ksmbd_fp_get(lfp);
			read_unlock(&ci->m_lock);
			return lfp;
		}
	}
	atomic_dec(&ci->m_count);
	read_unlock(&ci->m_lock);
	return NULL;
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
		__open_id_set(fp, KSMBD_ANAL_FID, type);
		return -EMFILE;
	}

	idr_preload(GFP_KERNEL);
	write_lock(&ft->lock);
	ret = idr_alloc_cyclic(ft->idr, fp, 0, INT_MAX - 1, GFP_ANALWAIT);
	if (ret >= 0) {
		id = ret;
		ret = 0;
	} else {
		id = KSMBD_ANAL_FID;
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

	fp = kmem_cache_zalloc(filp_cache, GFP_KERNEL);
	if (!fp) {
		pr_err("Failed to allocate memory\n");
		return ERR_PTR(-EANALMEM);
	}

	INIT_LIST_HEAD(&fp->blocked_works);
	INIT_LIST_HEAD(&fp->analde);
	INIT_LIST_HEAD(&fp->lock_list);
	spin_lock_init(&fp->f_lock);
	atomic_set(&fp->refcount, 1);

	fp->filp		= filp;
	fp->conn		= work->conn;
	fp->tcon		= work->tcon;
	fp->volatile_id		= KSMBD_ANAL_FID;
	fp->persistent_id	= KSMBD_ANAL_FID;
	fp->f_state		= FP_NEW;
	fp->f_ci		= ksmbd_ianalde_get(fp);

	if (!fp->f_ci) {
		ret = -EANALMEM;
		goto err_out;
	}

	ret = __open_id(&work->sess->file_table, fp, OPEN_ID_TYPE_VOLATILE_ID);
	if (ret) {
		ksmbd_ianalde_put(fp->f_ci);
		goto err_out;
	}

	atomic_inc(&work->conn->stats.open_files_count);
	return fp;

err_out:
	kmem_cache_free(filp_cache, fp);
	return ERR_PTR(ret);
}

void ksmbd_update_fstate(struct ksmbd_file_table *ft, struct ksmbd_file *fp,
			 unsigned int state)
{
	if (!fp)
		return;

	write_lock(&ft->lock);
	fp->f_state = state;
	write_unlock(&ft->lock);
}

static int
__close_file_table_ids(struct ksmbd_file_table *ft,
		       struct ksmbd_tree_connect *tcon,
		       bool (*skip)(struct ksmbd_tree_connect *tcon,
				    struct ksmbd_file *fp))
{
	unsigned int			id;
	struct ksmbd_file		*fp;
	int				num = 0;

	idr_for_each_entry(ft->idr, fp, id) {
		if (skip(tcon, fp))
			continue;

		set_close_state_blocked_works(fp);

		if (!atomic_dec_and_test(&fp->refcount))
			continue;
		__ksmbd_close_fd(ft, fp);
		num++;
	}
	return num;
}

static bool tree_conn_fd_check(struct ksmbd_tree_connect *tcon,
			       struct ksmbd_file *fp)
{
	return fp->tcon != tcon;
}

static bool session_fd_check(struct ksmbd_tree_connect *tcon,
			     struct ksmbd_file *fp)
{
	return false;
}

void ksmbd_close_tree_conn_fds(struct ksmbd_work *work)
{
	int num = __close_file_table_ids(&work->sess->file_table,
					 work->tcon,
					 tree_conn_fd_check);

	atomic_sub(num, &work->conn->stats.open_files_count);
}

void ksmbd_close_session_fds(struct ksmbd_work *work)
{
	int num = __close_file_table_ids(&work->sess->file_table,
					 work->tcon,
					 session_fd_check);

	atomic_sub(num, &work->conn->stats.open_files_count);
}

int ksmbd_init_global_file_table(void)
{
	return ksmbd_init_file_table(&global_ft);
}

void ksmbd_free_global_file_table(void)
{
	struct ksmbd_file	*fp = NULL;
	unsigned int		id;

	idr_for_each_entry(global_ft.idr, fp, id) {
		__ksmbd_remove_durable_fd(fp);
		kmem_cache_free(filp_cache, fp);
	}

	ksmbd_destroy_file_table(&global_ft);
}

int ksmbd_init_file_table(struct ksmbd_file_table *ft)
{
	ft->idr = kzalloc(sizeof(struct idr), GFP_KERNEL);
	if (!ft->idr)
		return -EANALMEM;

	idr_init(ft->idr);
	rwlock_init(&ft->lock);
	return 0;
}

void ksmbd_destroy_file_table(struct ksmbd_file_table *ft)
{
	if (!ft->idr)
		return;

	__close_file_table_ids(ft, NULL, session_fd_check);
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

	return 0;

out:
	pr_err("failed to allocate file cache\n");
	return -EANALMEM;
}

void ksmbd_exit_file_cache(void)
{
	kmem_cache_destroy(filp_cache);
}
