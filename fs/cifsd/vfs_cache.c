// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2016 Namjae Jeon <linkinjeon@kernel.org>
 * Copyright (C) 2019 Samsung Electronics Co., Ltd.
 */

#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

#include "glob.h"
#include "vfs_cache.h"
#include "buffer_pool.h"
#include "oplock.h"
#include "vfs.h"
#include "connection.h"
#include "mgmt/tree_connect.h"
#include "mgmt/user_session.h"
#include "smb_common.h"

#define S_DEL_PENDING			1
#define S_DEL_ON_CLS			2
#define S_DEL_ON_CLS_STREAM		8

static unsigned int inode_hash_mask __read_mostly;
static unsigned int inode_hash_shift __read_mostly;
static struct hlist_head *inode_hashtable __read_mostly;
static DEFINE_RWLOCK(inode_hash_lock);

static struct ksmbd_file_table global_ft;
static atomic_long_t fd_limit;

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

static struct ksmbd_inode *__ksmbd_inode_lookup(struct inode *inode)
{
	struct hlist_head *head = inode_hashtable +
		inode_hash(inode->i_sb, inode->i_ino);
	struct ksmbd_inode *ci = NULL, *ret_ci = NULL;

	hlist_for_each_entry(ci, head, m_hash) {
		if (ci->m_inode == inode) {
			if (atomic_inc_not_zero(&ci->m_count))
				ret_ci = ci;
			break;
		}
	}
	return ret_ci;
}

static struct ksmbd_inode *ksmbd_inode_lookup(struct ksmbd_file *fp)
{
	return __ksmbd_inode_lookup(FP_INODE(fp));
}

static struct ksmbd_inode *ksmbd_inode_lookup_by_vfsinode(struct inode *inode)
{
	struct ksmbd_inode *ci;

	read_lock(&inode_hash_lock);
	ci = __ksmbd_inode_lookup(inode);
	read_unlock(&inode_hash_lock);
	return ci;
}

int ksmbd_query_inode_status(struct inode *inode)
{
	struct ksmbd_inode *ci;
	int ret = KSMBD_INODE_STATUS_UNKNOWN;

	read_lock(&inode_hash_lock);
	ci = __ksmbd_inode_lookup(inode);
	if (ci) {
		ret = KSMBD_INODE_STATUS_OK;
		if (ci->m_flags & S_DEL_PENDING)
			ret = KSMBD_INODE_STATUS_PENDING_DELETE;
		atomic_dec(&ci->m_count);
	}
	read_unlock(&inode_hash_lock);
	return ret;
}

bool ksmbd_inode_pending_delete(struct ksmbd_file *fp)
{
	return (fp->f_ci->m_flags & S_DEL_PENDING);
}

void ksmbd_set_inode_pending_delete(struct ksmbd_file *fp)
{
	fp->f_ci->m_flags |= S_DEL_PENDING;
}

void ksmbd_clear_inode_pending_delete(struct ksmbd_file *fp)
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

static void ksmbd_inode_hash(struct ksmbd_inode *ci)
{
	struct hlist_head *b = inode_hashtable +
		inode_hash(ci->m_inode->i_sb, ci->m_inode->i_ino);

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
	ci->m_inode = FP_INODE(fp);
	atomic_set(&ci->m_count, 1);
	atomic_set(&ci->op_count, 0);
	atomic_set(&ci->sop_count, 0);
	ci->m_flags = 0;
	ci->m_fattr = 0;
	INIT_LIST_HEAD(&ci->m_fp_list);
	INIT_LIST_HEAD(&ci->m_op_list);
	rwlock_init(&ci->m_lock);
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

	ci = kmalloc(sizeof(struct ksmbd_inode), GFP_KERNEL);
	if (!ci)
		return NULL;

	rc = ksmbd_inode_init(ci, fp);
	if (rc) {
		ksmbd_err("inode initialized failed\n");
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

static void ksmbd_inode_put(struct ksmbd_inode *ci)
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

void __exit ksmbd_release_inode_hash(void)
{
	vfree(inode_hashtable);
}

static void __ksmbd_inode_close(struct ksmbd_file *fp)
{
	struct dentry *dir, *dentry;
	struct ksmbd_inode *ci = fp->f_ci;
	int err;
	struct file *filp;

	filp = fp->filp;
	if (ksmbd_stream_fd(fp) && (ci->m_flags & S_DEL_ON_CLS_STREAM)) {
		ci->m_flags &= ~S_DEL_ON_CLS_STREAM;
		err = ksmbd_vfs_remove_xattr(filp->f_path.dentry,
					     fp->stream.name);
		if (err)
			ksmbd_err("remove xattr failed : %s\n",
				fp->stream.name);
	}

	if (atomic_dec_and_test(&ci->m_count)) {
		write_lock(&ci->m_lock);
		if (ci->m_flags & (S_DEL_ON_CLS | S_DEL_PENDING)) {
			dentry = filp->f_path.dentry;
			dir = dentry->d_parent;
			ci->m_flags &= ~(S_DEL_ON_CLS | S_DEL_PENDING);
			write_unlock(&ci->m_lock);
			ksmbd_vfs_unlink(dir, dentry);
			write_lock(&ci->m_lock);
		}
		write_unlock(&ci->m_lock);

		ksmbd_inode_free(ci);
	}
}

static void __ksmbd_remove_durable_fd(struct ksmbd_file *fp)
{
	if (!HAS_FILE_ID(fp->persistent_id))
		return;

	write_lock(&global_ft.lock);
	idr_remove(global_ft.idr, fp->persistent_id);
	write_unlock(&global_ft.lock);
}

static void __ksmbd_remove_fd(struct ksmbd_file_table *ft,
			      struct ksmbd_file *fp)
{
	if (!HAS_FILE_ID(fp->volatile_id))
		return;

	write_lock(&fp->f_ci->m_lock);
	list_del_init(&fp->node);
	write_unlock(&fp->f_ci->m_lock);

	write_lock(&ft->lock);
	idr_remove(ft->idr, fp->volatile_id);
	write_unlock(&ft->lock);
}

static void __ksmbd_close_fd(struct ksmbd_file_table *ft,
			     struct ksmbd_file *fp)
{
	struct file *filp;

	fd_limit_close();
	__ksmbd_remove_durable_fd(fp);
	__ksmbd_remove_fd(ft, fp);

	close_id_del_oplock(fp);
	filp = fp->filp;

	__ksmbd_inode_close(fp);
	if (!IS_ERR_OR_NULL(filp))
		fput(filp);
	kfree(fp->filename);
	if (ksmbd_stream_fd(fp))
		kfree(fp->stream.name);
	ksmbd_free_file_struct(fp);
}

static struct ksmbd_file *ksmbd_fp_get(struct ksmbd_file *fp)
{
	if (!atomic_inc_not_zero(&fp->refcount))
		return NULL;
	return fp;
}

static struct ksmbd_file *__ksmbd_lookup_fd(struct ksmbd_file_table *ft,
					    unsigned int id)
{
	bool unclaimed = true;
	struct ksmbd_file *fp;

	read_lock(&ft->lock);
	fp = idr_find(ft->idr, id);
	if (fp)
		fp = ksmbd_fp_get(fp);

	if (fp && fp->f_ci) {
		read_lock(&fp->f_ci->m_lock);
		unclaimed = list_empty(&fp->node);
		read_unlock(&fp->f_ci->m_lock);
	}
	read_unlock(&ft->lock);

	if (fp && unclaimed) {
		atomic_dec(&fp->refcount);
		return NULL;
	}
	return fp;
}

static void __put_fd_final(struct ksmbd_work *work,
			   struct ksmbd_file *fp)
{
	__ksmbd_close_fd(&work->sess->file_table, fp);
	atomic_dec(&work->conn->stats.open_files_count);
}

static void set_close_state_blocked_works(struct ksmbd_file *fp)
{
	struct ksmbd_work *cancel_work, *ctmp;

	spin_lock(&fp->f_lock);
	list_for_each_entry_safe(cancel_work, ctmp, &fp->blocked_works,
			fp_entry) {
		list_del(&cancel_work->fp_entry);
		cancel_work->state = KSMBD_WORK_CLOSED;
		cancel_work->cancel_fn(cancel_work->cancel_argv);
	}
	spin_unlock(&fp->f_lock);
}

int ksmbd_close_fd(struct ksmbd_work *work, unsigned int id)
{
	struct ksmbd_file	*fp;
	struct ksmbd_file_table	*ft;

	if (!HAS_FILE_ID(id))
		return 0;

	ft = &work->sess->file_table;
	read_lock(&ft->lock);
	fp = idr_find(ft->idr, id);
	if (fp) {
		set_close_state_blocked_works(fp);

		if (!atomic_dec_and_test(&fp->refcount))
			fp = NULL;
	}
	read_unlock(&ft->lock);

	if (!fp)
		return -EINVAL;

	__put_fd_final(work, fp);
	return 0;
}

void ksmbd_fd_put(struct ksmbd_work *work,
		  struct ksmbd_file *fp)
{
	if (!fp)
		return;

	if (!atomic_dec_and_test(&fp->refcount))
		return;
	__put_fd_final(work, fp);
}

static bool __sanity_check(struct ksmbd_tree_connect *tcon,
			   struct ksmbd_file *fp)
{
	if (!fp)
		return false;
	if (fp->tcon != tcon)
		return false;
	return true;
}

struct ksmbd_file *ksmbd_lookup_foreign_fd(struct ksmbd_work *work,
					   unsigned int id)
{
	return __ksmbd_lookup_fd(&work->sess->file_table, id);
}

struct ksmbd_file *ksmbd_lookup_fd_fast(struct ksmbd_work *work,
					unsigned int id)
{
	struct ksmbd_file *fp = __ksmbd_lookup_fd(&work->sess->file_table, id);

	if (__sanity_check(work->tcon, fp))
		return fp;

	ksmbd_fd_put(work, fp);
	return NULL;
}

struct ksmbd_file *ksmbd_lookup_fd_slow(struct ksmbd_work *work,
					unsigned int id,
					unsigned int pid)
{
	struct ksmbd_file *fp;

	if (!HAS_FILE_ID(id)) {
		id = work->compound_fid;
		pid = work->compound_pfid;
	}

	if (!HAS_FILE_ID(id))
		return NULL;

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

int ksmbd_close_fd_app_id(struct ksmbd_work *work,
			  char *app_id)
{
	struct ksmbd_file	*fp = NULL;
	unsigned int		id;

	read_lock(&global_ft.lock);
	idr_for_each_entry(global_ft.idr, fp, id) {
		if (!memcmp(fp->app_instance_id,
			    app_id,
			    SMB2_CREATE_GUID_SIZE)) {
			if (!atomic_dec_and_test(&fp->refcount))
				fp = NULL;
			break;
		}
	}
	read_unlock(&global_ft.lock);

	if (!fp)
		return -EINVAL;

	__put_fd_final(work, fp);
	return 0;
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

struct ksmbd_file *ksmbd_lookup_fd_filename(struct ksmbd_work *work,
					    char *filename)
{
	struct ksmbd_file	*fp = NULL;
	unsigned int		id;

	read_lock(&work->sess->file_table.lock);
	idr_for_each_entry(work->sess->file_table.idr, fp, id) {
		if (!strcmp(fp->filename, filename)) {
			fp = ksmbd_fp_get(fp);
			break;
		}
	}
	read_unlock(&work->sess->file_table.lock);

	return fp;
}

struct ksmbd_file *ksmbd_lookup_fd_inode(struct inode *inode)
{
	struct ksmbd_file	*lfp;
	struct ksmbd_inode	*ci;
	struct list_head	*cur;

	ci = ksmbd_inode_lookup_by_vfsinode(inode);
	if (!ci)
		return NULL;

	read_lock(&ci->m_lock);
	list_for_each(cur, &ci->m_fp_list) {
		lfp = list_entry(cur, struct ksmbd_file, node);
		if (inode == FP_INODE(lfp)) {
			atomic_dec(&ci->m_count);
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

static void __open_id_set(struct ksmbd_file *fp, unsigned int id, int type)
{
	if (type == OPEN_ID_TYPE_VOLATILE_ID)
		fp->volatile_id = id;
	if (type == OPEN_ID_TYPE_PERSISTENT_ID)
		fp->persistent_id = id;
}

static int __open_id(struct ksmbd_file_table *ft,
		     struct ksmbd_file *fp,
		     int type)
{
	unsigned int		id = 0;
	int			ret;

	if (type == OPEN_ID_TYPE_VOLATILE_ID && fd_limit_depleted()) {
		__open_id_set(fp, KSMBD_NO_FID, type);
		return -EMFILE;
	}

	idr_preload(GFP_KERNEL);
	write_lock(&ft->lock);
	ret = idr_alloc_cyclic(ft->idr, fp, 0, INT_MAX, GFP_NOWAIT);
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

struct ksmbd_file *ksmbd_open_fd(struct ksmbd_work *work,
				 struct file *filp)
{
	struct ksmbd_file	*fp;
	int ret;

	fp = ksmbd_alloc_file_struct();
	if (!fp) {
		ksmbd_err("Failed to allocate memory\n");
		return ERR_PTR(-ENOMEM);
	}

	INIT_LIST_HEAD(&fp->blocked_works);
	INIT_LIST_HEAD(&fp->node);
	spin_lock_init(&fp->f_lock);
	atomic_set(&fp->refcount, 1);

	fp->filp		= filp;
	fp->conn		= work->sess->conn;
	fp->tcon		= work->tcon;
	fp->volatile_id		= KSMBD_NO_FID;
	fp->persistent_id	= KSMBD_NO_FID;
	fp->f_ci		= ksmbd_inode_get(fp);

	if (!fp->f_ci) {
		ksmbd_free_file_struct(fp);
		return ERR_PTR(-ENOMEM);
	}

	ret = __open_id(&work->sess->file_table, fp, OPEN_ID_TYPE_VOLATILE_ID);
	if (ret) {
		ksmbd_inode_put(fp->f_ci);
		ksmbd_free_file_struct(fp);
		return ERR_PTR(ret);
	}

	atomic_inc(&work->conn->stats.open_files_count);
	return fp;
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
	if (!is_reconnectable(fp))
		return false;

	fp->conn = NULL;
	fp->tcon = NULL;
	fp->volatile_id = KSMBD_NO_FID;
	return true;
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
		ksmbd_free_file_struct(fp);
	}

	ksmbd_destroy_file_table(&global_ft);
}

int ksmbd_reopen_durable_fd(struct ksmbd_work *work,
			    struct ksmbd_file *fp)
{
	if (!fp->is_durable || fp->conn || fp->tcon) {
		ksmbd_err("Invalid durable fd [%p:%p]\n",
				fp->conn, fp->tcon);
		return -EBADF;
	}

	if (HAS_FILE_ID(fp->volatile_id)) {
		ksmbd_err("Still in use durable fd: %u\n", fp->volatile_id);
		return -EBADF;
	}

	fp->conn = work->sess->conn;
	fp->tcon = work->tcon;

	__open_id(&work->sess->file_table, fp, OPEN_ID_TYPE_VOLATILE_ID);
	if (!HAS_FILE_ID(fp->volatile_id)) {
		fp->conn = NULL;
		fp->tcon = NULL;
		return -EBADF;
	}
	return 0;
}

static void close_fd_list(struct ksmbd_work *work, struct list_head *head)
{
	while (!list_empty(head)) {
		struct ksmbd_file *fp;

		fp = list_first_entry(head, struct ksmbd_file, node);
		list_del_init(&fp->node);

		__ksmbd_close_fd(&work->sess->file_table, fp);
	}
}

int ksmbd_close_inode_fds(struct ksmbd_work *work, struct inode *inode)
{
	struct ksmbd_inode *ci;
	bool unlinked = true;
	struct ksmbd_file *fp, *fptmp;
	LIST_HEAD(dispose);

	ci = ksmbd_inode_lookup_by_vfsinode(inode);
	if (!ci)
		return true;

	if (ci->m_flags & (S_DEL_ON_CLS | S_DEL_PENDING))
		unlinked = false;

	write_lock(&ci->m_lock);
	list_for_each_entry_safe(fp, fptmp, &ci->m_fp_list, node) {
		if (fp->conn)
			continue;

		list_del(&fp->node);
		list_add(&fp->node, &dispose);
	}
	atomic_dec(&ci->m_count);
	write_unlock(&ci->m_lock);

	close_fd_list(work, &dispose);
	return unlinked;
}

int ksmbd_file_table_flush(struct ksmbd_work *work)
{
	struct ksmbd_file	*fp = NULL;
	unsigned int		id;
	int			ret;

	read_lock(&work->sess->file_table.lock);
	idr_for_each_entry(work->sess->file_table.idr, fp, id) {
		ret = ksmbd_vfs_fsync(work, fp->volatile_id, KSMBD_NO_FID);
		if (ret)
			break;
	}
	read_unlock(&work->sess->file_table.lock);
	return ret;
}

int ksmbd_init_file_table(struct ksmbd_file_table *ft)
{
	ft->idr = ksmbd_alloc(sizeof(struct idr));
	if (!ft->idr)
		return -ENOMEM;

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
	ksmbd_free(ft->idr);
	ft->idr = NULL;
}
