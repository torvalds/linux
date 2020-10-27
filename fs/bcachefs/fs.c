// SPDX-License-Identifier: GPL-2.0
#ifndef NO_BCACHEFS_FS

#include "bcachefs.h"
#include "acl.h"
#include "bkey_on_stack.h"
#include "btree_update.h"
#include "buckets.h"
#include "chardev.h"
#include "dirent.h"
#include "extents.h"
#include "fs.h"
#include "fs-common.h"
#include "fs-io.h"
#include "fs-ioctl.h"
#include "fsck.h"
#include "inode.h"
#include "io.h"
#include "journal.h"
#include "keylist.h"
#include "quota.h"
#include "super.h"
#include "xattr.h"

#include <linux/aio.h>
#include <linux/backing-dev.h>
#include <linux/exportfs.h>
#include <linux/fiemap.h>
#include <linux/module.h>
#include <linux/pagemap.h>
#include <linux/posix_acl.h>
#include <linux/random.h>
#include <linux/seq_file.h>
#include <linux/statfs.h>
#include <linux/xattr.h>

static struct kmem_cache *bch2_inode_cache;

static void bch2_vfs_inode_init(struct bch_fs *,
				struct bch_inode_info *,
				struct bch_inode_unpacked *);

static void journal_seq_copy(struct bch_fs *c,
			     struct bch_inode_info *dst,
			     u64 journal_seq)
{
	u64 old, v = READ_ONCE(dst->ei_journal_seq);

	do {
		old = v;

		if (old >= journal_seq)
			break;
	} while ((v = cmpxchg(&dst->ei_journal_seq, old, journal_seq)) != old);

	bch2_journal_set_has_inum(&c->journal, dst->v.i_ino, journal_seq);
}

static void __pagecache_lock_put(struct pagecache_lock *lock, long i)
{
	BUG_ON(atomic_long_read(&lock->v) == 0);

	if (atomic_long_sub_return_release(i, &lock->v) == 0)
		wake_up_all(&lock->wait);
}

static bool __pagecache_lock_tryget(struct pagecache_lock *lock, long i)
{
	long v = atomic_long_read(&lock->v), old;

	do {
		old = v;

		if (i > 0 ? v < 0 : v > 0)
			return false;
	} while ((v = atomic_long_cmpxchg_acquire(&lock->v,
					old, old + i)) != old);
	return true;
}

static void __pagecache_lock_get(struct pagecache_lock *lock, long i)
{
	wait_event(lock->wait, __pagecache_lock_tryget(lock, i));
}

void bch2_pagecache_add_put(struct pagecache_lock *lock)
{
	__pagecache_lock_put(lock, 1);
}

void bch2_pagecache_add_get(struct pagecache_lock *lock)
{
	__pagecache_lock_get(lock, 1);
}

void bch2_pagecache_block_put(struct pagecache_lock *lock)
{
	__pagecache_lock_put(lock, -1);
}

void bch2_pagecache_block_get(struct pagecache_lock *lock)
{
	__pagecache_lock_get(lock, -1);
}

void bch2_inode_update_after_write(struct bch_fs *c,
				   struct bch_inode_info *inode,
				   struct bch_inode_unpacked *bi,
				   unsigned fields)
{
	set_nlink(&inode->v, bch2_inode_nlink_get(bi));
	i_uid_write(&inode->v, bi->bi_uid);
	i_gid_write(&inode->v, bi->bi_gid);
	inode->v.i_mode	= bi->bi_mode;

	if (fields & ATTR_ATIME)
		inode->v.i_atime = bch2_time_to_timespec(c, bi->bi_atime);
	if (fields & ATTR_MTIME)
		inode->v.i_mtime = bch2_time_to_timespec(c, bi->bi_mtime);
	if (fields & ATTR_CTIME)
		inode_set_ctime_to_ts(&inode->v, bch2_time_to_timespec(c, bi->bi_ctime));

	inode->ei_inode		= *bi;

	bch2_inode_flags_to_vfs(inode);
}

int __must_check bch2_write_inode(struct bch_fs *c,
				  struct bch_inode_info *inode,
				  inode_set_fn set,
				  void *p, unsigned fields)
{
	struct btree_trans trans;
	struct btree_iter *iter;
	struct bch_inode_unpacked inode_u;
	int ret;

	bch2_trans_init(&trans, c, 0, 0);
retry:
	bch2_trans_begin(&trans);

	iter = bch2_inode_peek(&trans, &inode_u, inode->v.i_ino,
			       BTREE_ITER_INTENT);
	ret   = PTR_ERR_OR_ZERO(iter) ?:
		(set ? set(inode, &inode_u, p) : 0) ?:
		bch2_inode_write(&trans, iter, &inode_u) ?:
		bch2_trans_commit(&trans, NULL,
				  &inode->ei_journal_seq,
				  BTREE_INSERT_NOUNLOCK|
				  BTREE_INSERT_NOFAIL);

	/*
	 * the btree node lock protects inode->ei_inode, not ei_update_lock;
	 * this is important for inode updates via bchfs_write_index_update
	 */
	if (!ret)
		bch2_inode_update_after_write(c, inode, &inode_u, fields);

	bch2_trans_iter_put(&trans, iter);

	if (ret == -EINTR)
		goto retry;

	bch2_trans_exit(&trans);
	return ret < 0 ? ret : 0;
}

int bch2_fs_quota_transfer(struct bch_fs *c,
			   struct bch_inode_info *inode,
			   struct bch_qid new_qid,
			   unsigned qtypes,
			   enum quota_acct_mode mode)
{
	unsigned i;
	int ret;

	qtypes &= enabled_qtypes(c);

	for (i = 0; i < QTYP_NR; i++)
		if (new_qid.q[i] == inode->ei_qid.q[i])
			qtypes &= ~(1U << i);

	if (!qtypes)
		return 0;

	mutex_lock(&inode->ei_quota_lock);

	ret = bch2_quota_transfer(c, qtypes, new_qid,
				  inode->ei_qid,
				  inode->v.i_blocks +
				  inode->ei_quota_reserved,
				  mode);
	if (!ret)
		for (i = 0; i < QTYP_NR; i++)
			if (qtypes & (1 << i))
				inode->ei_qid.q[i] = new_qid.q[i];

	mutex_unlock(&inode->ei_quota_lock);

	return ret;
}

struct inode *bch2_vfs_inode_get(struct bch_fs *c, u64 inum)
{
	struct bch_inode_unpacked inode_u;
	struct bch_inode_info *inode;
	int ret;

	inode = to_bch_ei(iget_locked(c->vfs_sb, inum));
	if (unlikely(!inode))
		return ERR_PTR(-ENOMEM);
	if (!(inode->v.i_state & I_NEW))
		return &inode->v;

	ret = bch2_inode_find_by_inum(c, inum, &inode_u);
	if (ret) {
		iget_failed(&inode->v);
		return ERR_PTR(ret);
	}

	bch2_vfs_inode_init(c, inode, &inode_u);

	inode->ei_journal_seq = bch2_inode_journal_seq(&c->journal, inum);

	unlock_new_inode(&inode->v);

	return &inode->v;
}

static int inum_test(struct inode *inode, void *p)
{
	unsigned long *ino = p;

	return *ino == inode->i_ino;
}

static struct bch_inode_info *
__bch2_create(struct mnt_idmap *idmap,
	      struct bch_inode_info *dir, struct dentry *dentry,
	      umode_t mode, dev_t rdev, bool tmpfile)
{
	struct bch_fs *c = dir->v.i_sb->s_fs_info;
	struct btree_trans trans;
	struct bch_inode_unpacked dir_u;
	struct bch_inode_info *inode, *old;
	struct bch_inode_unpacked inode_u;
	struct posix_acl *default_acl = NULL, *acl = NULL;
	u64 journal_seq = 0;
	int ret;

	/*
	 * preallocate acls + vfs inode before btree transaction, so that
	 * nothing can fail after the transaction succeeds:
	 */
#ifdef CONFIG_BCACHEFS_POSIX_ACL
	ret = posix_acl_create(&dir->v, &mode, &default_acl, &acl);
	if (ret)
		return ERR_PTR(ret);
#endif
	inode = to_bch_ei(new_inode(c->vfs_sb));
	if (unlikely(!inode)) {
		inode = ERR_PTR(-ENOMEM);
		goto err;
	}

	bch2_inode_init_early(c, &inode_u);

	if (!tmpfile)
		mutex_lock(&dir->ei_update_lock);

	bch2_trans_init(&trans, c, 8, 1024);
retry:
	bch2_trans_begin(&trans);

	ret   = bch2_create_trans(&trans, dir->v.i_ino, &dir_u, &inode_u,
				  !tmpfile ? &dentry->d_name : NULL,
				  from_kuid(i_user_ns(&dir->v), current_fsuid()),
				  from_kgid(i_user_ns(&dir->v), current_fsgid()),
				  mode, rdev,
				  default_acl, acl) ?:
		bch2_quota_acct(c, bch_qid(&inode_u), Q_INO, 1,
				KEY_TYPE_QUOTA_PREALLOC);
	if (unlikely(ret))
		goto err_before_quota;

	ret   = bch2_trans_commit(&trans, NULL, &journal_seq,
				  BTREE_INSERT_NOUNLOCK);
	if (unlikely(ret)) {
		bch2_quota_acct(c, bch_qid(&inode_u), Q_INO, -1,
				KEY_TYPE_QUOTA_WARN);
err_before_quota:
		if (ret == -EINTR)
			goto retry;
		goto err_trans;
	}

	if (!tmpfile) {
		bch2_inode_update_after_write(c, dir, &dir_u,
					      ATTR_MTIME|ATTR_CTIME);
		journal_seq_copy(c, dir, journal_seq);
		mutex_unlock(&dir->ei_update_lock);
	}

	bch2_vfs_inode_init(c, inode, &inode_u);
	journal_seq_copy(c, inode, journal_seq);

	set_cached_acl(&inode->v, ACL_TYPE_ACCESS, acl);
	set_cached_acl(&inode->v, ACL_TYPE_DEFAULT, default_acl);

	/*
	 * we must insert the new inode into the inode cache before calling
	 * bch2_trans_exit() and dropping locks, else we could race with another
	 * thread pulling the inode in and modifying it:
	 */

	inode->v.i_state |= I_CREATING;
	old = to_bch_ei(inode_insert5(&inode->v, inode->v.i_ino,
				      inum_test, NULL, &inode->v.i_ino));
	BUG_ON(!old);

	if (unlikely(old != inode)) {
		/*
		 * We raced, another process pulled the new inode into cache
		 * before us:
		 */
		journal_seq_copy(c, old, journal_seq);
		make_bad_inode(&inode->v);
		iput(&inode->v);

		inode = old;
	} else {
		/*
		 * we really don't want insert_inode_locked2() to be setting
		 * I_NEW...
		 */
		unlock_new_inode(&inode->v);
	}

	bch2_trans_exit(&trans);
err:
	posix_acl_release(default_acl);
	posix_acl_release(acl);
	return inode;
err_trans:
	if (!tmpfile)
		mutex_unlock(&dir->ei_update_lock);

	bch2_trans_exit(&trans);
	make_bad_inode(&inode->v);
	iput(&inode->v);
	inode = ERR_PTR(ret);
	goto err;
}

/* methods */

static struct dentry *bch2_lookup(struct inode *vdir, struct dentry *dentry,
				  unsigned int flags)
{
	struct bch_fs *c = vdir->i_sb->s_fs_info;
	struct bch_inode_info *dir = to_bch_ei(vdir);
	struct inode *vinode = NULL;
	u64 inum;

	inum = bch2_dirent_lookup(c, dir->v.i_ino,
				  &dir->ei_str_hash,
				  &dentry->d_name);

	if (inum)
		vinode = bch2_vfs_inode_get(c, inum);

	return d_splice_alias(vinode, dentry);
}

static int bch2_mknod(struct mnt_idmap *idmap,
		      struct inode *vdir, struct dentry *dentry,
		      umode_t mode, dev_t rdev)
{
	struct bch_inode_info *inode =
		__bch2_create(idmap, to_bch_ei(vdir), dentry, mode, rdev, false);

	if (IS_ERR(inode))
		return PTR_ERR(inode);

	d_instantiate(dentry, &inode->v);
	return 0;
}

static int bch2_create(struct mnt_idmap *idmap,
		       struct inode *vdir, struct dentry *dentry,
		       umode_t mode, bool excl)
{
	return bch2_mknod(idmap, vdir, dentry, mode|S_IFREG, 0);
}

static int __bch2_link(struct bch_fs *c,
		       struct bch_inode_info *inode,
		       struct bch_inode_info *dir,
		       struct dentry *dentry)
{
	struct btree_trans trans;
	struct bch_inode_unpacked dir_u, inode_u;
	int ret;

	mutex_lock(&inode->ei_update_lock);
	bch2_trans_init(&trans, c, 4, 1024);

	do {
		bch2_trans_begin(&trans);
		ret   = bch2_link_trans(&trans,
					dir->v.i_ino,
					inode->v.i_ino, &dir_u, &inode_u,
					&dentry->d_name) ?:
			bch2_trans_commit(&trans, NULL,
					&inode->ei_journal_seq,
					BTREE_INSERT_NOUNLOCK);
	} while (ret == -EINTR);

	if (likely(!ret)) {
		BUG_ON(inode_u.bi_inum != inode->v.i_ino);

		journal_seq_copy(c, inode, dir->ei_journal_seq);
		bch2_inode_update_after_write(c, dir, &dir_u,
					      ATTR_MTIME|ATTR_CTIME);
		bch2_inode_update_after_write(c, inode, &inode_u, ATTR_CTIME);
	}

	bch2_trans_exit(&trans);
	mutex_unlock(&inode->ei_update_lock);
	return ret;
}

static int bch2_link(struct dentry *old_dentry, struct inode *vdir,
		     struct dentry *dentry)
{
	struct bch_fs *c = vdir->i_sb->s_fs_info;
	struct bch_inode_info *dir = to_bch_ei(vdir);
	struct bch_inode_info *inode = to_bch_ei(old_dentry->d_inode);
	int ret;

	lockdep_assert_held(&inode->v.i_rwsem);

	ret = __bch2_link(c, inode, dir, dentry);
	if (unlikely(ret))
		return ret;

	ihold(&inode->v);
	d_instantiate(dentry, &inode->v);
	return 0;
}

static int bch2_unlink(struct inode *vdir, struct dentry *dentry)
{
	struct bch_fs *c = vdir->i_sb->s_fs_info;
	struct bch_inode_info *dir = to_bch_ei(vdir);
	struct bch_inode_info *inode = to_bch_ei(dentry->d_inode);
	struct bch_inode_unpacked dir_u, inode_u;
	struct btree_trans trans;
	int ret;

	bch2_lock_inodes(INODE_UPDATE_LOCK, dir, inode);
	bch2_trans_init(&trans, c, 4, 1024);

	do {
		bch2_trans_begin(&trans);

		ret   = bch2_unlink_trans(&trans,
					  dir->v.i_ino, &dir_u,
					  &inode_u, &dentry->d_name) ?:
			bch2_trans_commit(&trans, NULL,
					  &dir->ei_journal_seq,
					  BTREE_INSERT_NOUNLOCK|
					  BTREE_INSERT_NOFAIL);
	} while (ret == -EINTR);

	if (likely(!ret)) {
		BUG_ON(inode_u.bi_inum != inode->v.i_ino);

		journal_seq_copy(c, inode, dir->ei_journal_seq);
		bch2_inode_update_after_write(c, dir, &dir_u,
					      ATTR_MTIME|ATTR_CTIME);
		bch2_inode_update_after_write(c, inode, &inode_u,
					      ATTR_MTIME);
	}

	bch2_trans_exit(&trans);
	bch2_unlock_inodes(INODE_UPDATE_LOCK, dir, inode);

	return ret;
}

static int bch2_symlink(struct mnt_idmap *idmap,
			struct inode *vdir, struct dentry *dentry,
			const char *symname)
{
	struct bch_fs *c = vdir->i_sb->s_fs_info;
	struct bch_inode_info *dir = to_bch_ei(vdir), *inode;
	int ret;

	inode = __bch2_create(idmap, dir, dentry, S_IFLNK|S_IRWXUGO, 0, true);
	if (unlikely(IS_ERR(inode)))
		return PTR_ERR(inode);

	inode_lock(&inode->v);
	ret = page_symlink(&inode->v, symname, strlen(symname) + 1);
	inode_unlock(&inode->v);

	if (unlikely(ret))
		goto err;

	ret = filemap_write_and_wait_range(inode->v.i_mapping, 0, LLONG_MAX);
	if (unlikely(ret))
		goto err;

	journal_seq_copy(c, dir, inode->ei_journal_seq);

	ret = __bch2_link(c, inode, dir, dentry);
	if (unlikely(ret))
		goto err;

	d_instantiate(dentry, &inode->v);
	return 0;
err:
	iput(&inode->v);
	return ret;
}

static int bch2_mkdir(struct mnt_idmap *idmap,
		      struct inode *vdir, struct dentry *dentry, umode_t mode)
{
	return bch2_mknod(idmap, vdir, dentry, mode|S_IFDIR, 0);
}

static int bch2_rename2(struct mnt_idmap *idmap,
			struct inode *src_vdir, struct dentry *src_dentry,
			struct inode *dst_vdir, struct dentry *dst_dentry,
			unsigned flags)
{
	struct bch_fs *c = src_vdir->i_sb->s_fs_info;
	struct bch_inode_info *src_dir = to_bch_ei(src_vdir);
	struct bch_inode_info *dst_dir = to_bch_ei(dst_vdir);
	struct bch_inode_info *src_inode = to_bch_ei(src_dentry->d_inode);
	struct bch_inode_info *dst_inode = to_bch_ei(dst_dentry->d_inode);
	struct bch_inode_unpacked dst_dir_u, src_dir_u;
	struct bch_inode_unpacked src_inode_u, dst_inode_u;
	struct btree_trans trans;
	enum bch_rename_mode mode = flags & RENAME_EXCHANGE
		? BCH_RENAME_EXCHANGE
		: dst_dentry->d_inode
		? BCH_RENAME_OVERWRITE : BCH_RENAME;
	u64 journal_seq = 0;
	int ret;

	if (flags & ~(RENAME_NOREPLACE|RENAME_EXCHANGE))
		return -EINVAL;

	if (mode == BCH_RENAME_OVERWRITE) {
		ret = filemap_write_and_wait_range(src_inode->v.i_mapping,
						   0, LLONG_MAX);
		if (ret)
			return ret;
	}

	bch2_trans_init(&trans, c, 8, 2048);

	bch2_lock_inodes(INODE_UPDATE_LOCK,
			 src_dir,
			 dst_dir,
			 src_inode,
			 dst_inode);

	if (inode_attr_changing(dst_dir, src_inode, Inode_opt_project)) {
		ret = bch2_fs_quota_transfer(c, src_inode,
					     dst_dir->ei_qid,
					     1 << QTYP_PRJ,
					     KEY_TYPE_QUOTA_PREALLOC);
		if (ret)
			goto err;
	}

	if (mode == BCH_RENAME_EXCHANGE &&
	    inode_attr_changing(src_dir, dst_inode, Inode_opt_project)) {
		ret = bch2_fs_quota_transfer(c, dst_inode,
					     src_dir->ei_qid,
					     1 << QTYP_PRJ,
					     KEY_TYPE_QUOTA_PREALLOC);
		if (ret)
			goto err;
	}

retry:
	bch2_trans_begin(&trans);
	ret   = bch2_rename_trans(&trans,
				  src_dir->v.i_ino, &src_dir_u,
				  dst_dir->v.i_ino, &dst_dir_u,
				  &src_inode_u,
				  &dst_inode_u,
				  &src_dentry->d_name,
				  &dst_dentry->d_name,
				  mode) ?:
		bch2_trans_commit(&trans, NULL,
				  &journal_seq,
				  BTREE_INSERT_NOUNLOCK);
	if (ret == -EINTR)
		goto retry;
	if (unlikely(ret))
		goto err;

	BUG_ON(src_inode->v.i_ino != src_inode_u.bi_inum);
	BUG_ON(dst_inode &&
	       dst_inode->v.i_ino != dst_inode_u.bi_inum);

	bch2_inode_update_after_write(c, src_dir, &src_dir_u,
				      ATTR_MTIME|ATTR_CTIME);
	journal_seq_copy(c, src_dir, journal_seq);

	if (src_dir != dst_dir) {
		bch2_inode_update_after_write(c, dst_dir, &dst_dir_u,
					      ATTR_MTIME|ATTR_CTIME);
		journal_seq_copy(c, dst_dir, journal_seq);
	}

	bch2_inode_update_after_write(c, src_inode, &src_inode_u,
				      ATTR_CTIME);
	journal_seq_copy(c, src_inode, journal_seq);

	if (dst_inode) {
		bch2_inode_update_after_write(c, dst_inode, &dst_inode_u,
					      ATTR_CTIME);
		journal_seq_copy(c, dst_inode, journal_seq);
	}
err:
	bch2_trans_exit(&trans);

	bch2_fs_quota_transfer(c, src_inode,
			       bch_qid(&src_inode->ei_inode),
			       1 << QTYP_PRJ,
			       KEY_TYPE_QUOTA_NOCHECK);
	if (dst_inode)
		bch2_fs_quota_transfer(c, dst_inode,
				       bch_qid(&dst_inode->ei_inode),
				       1 << QTYP_PRJ,
				       KEY_TYPE_QUOTA_NOCHECK);

	bch2_unlock_inodes(INODE_UPDATE_LOCK,
			   src_dir,
			   dst_dir,
			   src_inode,
			   dst_inode);

	return ret;
}

static void bch2_setattr_copy(struct mnt_idmap *idmap,
			      struct bch_inode_info *inode,
			      struct bch_inode_unpacked *bi,
			      struct iattr *attr)
{
	struct bch_fs *c = inode->v.i_sb->s_fs_info;
	unsigned int ia_valid = attr->ia_valid;

	if (ia_valid & ATTR_UID)
		bi->bi_uid = from_kuid(i_user_ns(&inode->v), attr->ia_uid);
	if (ia_valid & ATTR_GID)
		bi->bi_gid = from_kgid(i_user_ns(&inode->v), attr->ia_gid);

	if (ia_valid & ATTR_ATIME)
		bi->bi_atime = timespec_to_bch2_time(c, attr->ia_atime);
	if (ia_valid & ATTR_MTIME)
		bi->bi_mtime = timespec_to_bch2_time(c, attr->ia_mtime);
	if (ia_valid & ATTR_CTIME)
		bi->bi_ctime = timespec_to_bch2_time(c, attr->ia_ctime);

	if (ia_valid & ATTR_MODE) {
		umode_t mode = attr->ia_mode;
		kgid_t gid = ia_valid & ATTR_GID
			? attr->ia_gid
			: inode->v.i_gid;

		if (!in_group_p(gid) &&
		    !capable_wrt_inode_uidgid(idmap, &inode->v, CAP_FSETID))
			mode &= ~S_ISGID;
		bi->bi_mode = mode;
	}
}

static int bch2_setattr_nonsize(struct mnt_idmap *idmap,
				struct bch_inode_info *inode,
				struct iattr *attr)
{
	struct bch_fs *c = inode->v.i_sb->s_fs_info;
	struct bch_qid qid;
	struct btree_trans trans;
	struct btree_iter *inode_iter;
	struct bch_inode_unpacked inode_u;
	struct posix_acl *acl = NULL;
	int ret;

	mutex_lock(&inode->ei_update_lock);

	qid = inode->ei_qid;

	if (attr->ia_valid & ATTR_UID)
		qid.q[QTYP_USR] = from_kuid(i_user_ns(&inode->v), attr->ia_uid);

	if (attr->ia_valid & ATTR_GID)
		qid.q[QTYP_GRP] = from_kgid(i_user_ns(&inode->v), attr->ia_gid);

	ret = bch2_fs_quota_transfer(c, inode, qid, ~0,
				     KEY_TYPE_QUOTA_PREALLOC);
	if (ret)
		goto err;

	bch2_trans_init(&trans, c, 0, 0);
retry:
	bch2_trans_begin(&trans);
	kfree(acl);
	acl = NULL;

	inode_iter = bch2_inode_peek(&trans, &inode_u, inode->v.i_ino,
				     BTREE_ITER_INTENT);
	ret = PTR_ERR_OR_ZERO(inode_iter);
	if (ret)
		goto btree_err;

	bch2_setattr_copy(idmap, inode, &inode_u, attr);

	if (attr->ia_valid & ATTR_MODE) {
		ret = bch2_acl_chmod(&trans, inode, inode_u.bi_mode, &acl);
		if (ret)
			goto btree_err;
	}

	ret =   bch2_inode_write(&trans, inode_iter, &inode_u) ?:
		bch2_trans_commit(&trans, NULL,
				  &inode->ei_journal_seq,
				  BTREE_INSERT_NOUNLOCK|
				  BTREE_INSERT_NOFAIL);
btree_err:
	if (ret == -EINTR)
		goto retry;
	if (unlikely(ret))
		goto err_trans;

	bch2_inode_update_after_write(c, inode, &inode_u, attr->ia_valid);

	if (acl)
		set_cached_acl(&inode->v, ACL_TYPE_ACCESS, acl);
err_trans:
	bch2_trans_exit(&trans);
err:
	mutex_unlock(&inode->ei_update_lock);

	return ret;
}

static int bch2_getattr(struct mnt_idmap *idmap,
			const struct path *path, struct kstat *stat,
			u32 request_mask, unsigned query_flags)
{
	struct bch_inode_info *inode = to_bch_ei(d_inode(path->dentry));
	struct bch_fs *c = inode->v.i_sb->s_fs_info;

	stat->dev	= inode->v.i_sb->s_dev;
	stat->ino	= inode->v.i_ino;
	stat->mode	= inode->v.i_mode;
	stat->nlink	= inode->v.i_nlink;
	stat->uid	= inode->v.i_uid;
	stat->gid	= inode->v.i_gid;
	stat->rdev	= inode->v.i_rdev;
	stat->size	= i_size_read(&inode->v);
	stat->atime	= inode->v.i_atime;
	stat->mtime	= inode->v.i_mtime;
	stat->ctime	= inode_get_ctime(&inode->v);
	stat->blksize	= block_bytes(c);
	stat->blocks	= inode->v.i_blocks;

	if (request_mask & STATX_BTIME) {
		stat->result_mask |= STATX_BTIME;
		stat->btime = bch2_time_to_timespec(c, inode->ei_inode.bi_otime);
	}

	if (inode->ei_inode.bi_flags & BCH_INODE_IMMUTABLE)
		stat->attributes |= STATX_ATTR_IMMUTABLE;
	stat->attributes_mask	 |= STATX_ATTR_IMMUTABLE;

	if (inode->ei_inode.bi_flags & BCH_INODE_APPEND)
		stat->attributes |= STATX_ATTR_APPEND;
	stat->attributes_mask	 |= STATX_ATTR_APPEND;

	if (inode->ei_inode.bi_flags & BCH_INODE_NODUMP)
		stat->attributes |= STATX_ATTR_NODUMP;
	stat->attributes_mask	 |= STATX_ATTR_NODUMP;

	return 0;
}

static int bch2_setattr(struct mnt_idmap *idmap,
			struct dentry *dentry, struct iattr *iattr)
{
	struct bch_inode_info *inode = to_bch_ei(dentry->d_inode);
	int ret;

	lockdep_assert_held(&inode->v.i_rwsem);

	ret = setattr_prepare(idmap, dentry, iattr);
	if (ret)
		return ret;

	return iattr->ia_valid & ATTR_SIZE
		? bch2_truncate(inode, iattr)
		: bch2_setattr_nonsize(idmap, inode, iattr);
}

static int bch2_tmpfile(struct mnt_idmap *idmap,
			struct inode *vdir, struct file *file, umode_t mode)
{
	struct bch_inode_info *inode =
		__bch2_create(idmap, to_bch_ei(vdir),
			      file->f_path.dentry, mode, 0, true);

	if (IS_ERR(inode))
		return PTR_ERR(inode);

	d_mark_tmpfile(file, &inode->v);
	d_instantiate(file->f_path.dentry, &inode->v);
	return finish_open_simple(file, 0);
}

static int bch2_fill_extent(struct bch_fs *c,
			    struct fiemap_extent_info *info,
			    struct bkey_s_c k, unsigned flags)
{
	if (bkey_extent_is_direct_data(k.k)) {
		struct bkey_ptrs_c ptrs = bch2_bkey_ptrs_c(k);
		const union bch_extent_entry *entry;
		struct extent_ptr_decoded p;
		int ret;

		if (k.k->type == KEY_TYPE_reflink_v)
			flags |= FIEMAP_EXTENT_SHARED;

		bkey_for_each_ptr_decode(k.k, ptrs, p, entry) {
			int flags2 = 0;
			u64 offset = p.ptr.offset;

			if (p.crc.compression_type)
				flags2 |= FIEMAP_EXTENT_ENCODED;
			else
				offset += p.crc.offset;

			if ((offset & (c->opts.block_size - 1)) ||
			    (k.k->size & (c->opts.block_size - 1)))
				flags2 |= FIEMAP_EXTENT_NOT_ALIGNED;

			ret = fiemap_fill_next_extent(info,
						bkey_start_offset(k.k) << 9,
						offset << 9,
						k.k->size << 9, flags|flags2);
			if (ret)
				return ret;
		}

		return 0;
	} else if (bkey_extent_is_inline_data(k.k)) {
		return fiemap_fill_next_extent(info,
					       bkey_start_offset(k.k) << 9,
					       0, k.k->size << 9,
					       flags|
					       FIEMAP_EXTENT_DATA_INLINE);
	} else if (k.k->type == KEY_TYPE_reservation) {
		return fiemap_fill_next_extent(info,
					       bkey_start_offset(k.k) << 9,
					       0, k.k->size << 9,
					       flags|
					       FIEMAP_EXTENT_DELALLOC|
					       FIEMAP_EXTENT_UNWRITTEN);
	} else {
		BUG();
	}
}

static int bch2_fiemap(struct inode *vinode, struct fiemap_extent_info *info,
		       u64 start, u64 len)
{
	struct bch_fs *c = vinode->i_sb->s_fs_info;
	struct bch_inode_info *ei = to_bch_ei(vinode);
	struct btree_trans trans;
	struct btree_iter *iter;
	struct bkey_s_c k;
	struct bkey_on_stack cur, prev;
	struct bpos end = POS(ei->v.i_ino, (start + len) >> 9);
	unsigned offset_into_extent, sectors;
	bool have_extent = false;
	int ret = 0;

	ret = fiemap_prep(&ei->v, info, start, &len, FIEMAP_FLAG_SYNC);
	if (ret)
		return ret;

	if (start + len < start)
		return -EINVAL;

	bkey_on_stack_init(&cur);
	bkey_on_stack_init(&prev);
	bch2_trans_init(&trans, c, 0, 0);

	iter = bch2_trans_get_iter(&trans, BTREE_ID_EXTENTS,
				   POS(ei->v.i_ino, start >> 9), 0);
retry:
	while ((k = bch2_btree_iter_peek(iter)).k &&
	       !(ret = bkey_err(k)) &&
	       bkey_cmp(iter->pos, end) < 0) {
		if (!bkey_extent_is_data(k.k) &&
		    k.k->type != KEY_TYPE_reservation) {
			bch2_btree_iter_next(iter);
			continue;
		}

		offset_into_extent	= iter->pos.offset -
			bkey_start_offset(k.k);
		sectors			= k.k->size - offset_into_extent;

		bkey_on_stack_reassemble(&cur, c, k);

		ret = bch2_read_indirect_extent(&trans,
					&offset_into_extent, &cur);
		if (ret)
			break;

		k = bkey_i_to_s_c(cur.k);
		bkey_on_stack_realloc(&prev, c, k.k->u64s);

		sectors = min(sectors, k.k->size - offset_into_extent);

		bch2_cut_front(POS(k.k->p.inode,
				   bkey_start_offset(k.k) +
				   offset_into_extent),
			       cur.k);
		bch2_key_resize(&cur.k->k, sectors);
		cur.k->k.p = iter->pos;
		cur.k->k.p.offset += cur.k->k.size;

		if (have_extent) {
			ret = bch2_fill_extent(c, info,
					bkey_i_to_s_c(prev.k), 0);
			if (ret)
				break;
		}

		bkey_copy(prev.k, cur.k);
		have_extent = true;

		bch2_btree_iter_set_pos(iter,
			POS(iter->pos.inode, iter->pos.offset + sectors));
	}

	if (ret == -EINTR)
		goto retry;

	if (!ret && have_extent)
		ret = bch2_fill_extent(c, info, bkey_i_to_s_c(prev.k),
				       FIEMAP_EXTENT_LAST);

	ret = bch2_trans_exit(&trans) ?: ret;
	bkey_on_stack_exit(&cur, c);
	bkey_on_stack_exit(&prev, c);
	return ret < 0 ? ret : 0;
}

static const struct vm_operations_struct bch_vm_ops = {
	.fault		= bch2_page_fault,
	.map_pages	= filemap_map_pages,
	.page_mkwrite   = bch2_page_mkwrite,
};

static int bch2_mmap(struct file *file, struct vm_area_struct *vma)
{
	file_accessed(file);

	vma->vm_ops = &bch_vm_ops;
	return 0;
}

/* Directories: */

static loff_t bch2_dir_llseek(struct file *file, loff_t offset, int whence)
{
	return generic_file_llseek_size(file, offset, whence,
					S64_MAX, S64_MAX);
}

static int bch2_vfs_readdir(struct file *file, struct dir_context *ctx)
{
	struct bch_inode_info *inode = file_bch_inode(file);
	struct bch_fs *c = inode->v.i_sb->s_fs_info;

	if (!dir_emit_dots(file, ctx))
		return 0;

	return bch2_readdir(c, inode->v.i_ino, ctx);
}

static const struct file_operations bch_file_operations = {
	.llseek		= bch2_llseek,
	.read_iter	= bch2_read_iter,
	.write_iter	= bch2_write_iter,
	.mmap		= bch2_mmap,
	.open		= generic_file_open,
	.fsync		= bch2_fsync,
	.splice_read	= filemap_splice_read,
	.splice_write	= iter_file_splice_write,
	.fallocate	= bch2_fallocate_dispatch,
	.unlocked_ioctl = bch2_fs_file_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= bch2_compat_fs_ioctl,
#endif
	.remap_file_range = bch2_remap_file_range,
};

static const struct inode_operations bch_file_inode_operations = {
	.getattr	= bch2_getattr,
	.setattr	= bch2_setattr,
	.fiemap		= bch2_fiemap,
	.listxattr	= bch2_xattr_list,
#ifdef CONFIG_BCACHEFS_POSIX_ACL
	.get_acl	= bch2_get_acl,
	.set_acl	= bch2_set_acl,
#endif
};

static const struct inode_operations bch_dir_inode_operations = {
	.lookup		= bch2_lookup,
	.create		= bch2_create,
	.link		= bch2_link,
	.unlink		= bch2_unlink,
	.symlink	= bch2_symlink,
	.mkdir		= bch2_mkdir,
	.rmdir		= bch2_unlink,
	.mknod		= bch2_mknod,
	.rename		= bch2_rename2,
	.getattr	= bch2_getattr,
	.setattr	= bch2_setattr,
	.tmpfile	= bch2_tmpfile,
	.listxattr	= bch2_xattr_list,
#ifdef CONFIG_BCACHEFS_POSIX_ACL
	.get_acl	= bch2_get_acl,
	.set_acl	= bch2_set_acl,
#endif
};

static const struct file_operations bch_dir_file_operations = {
	.llseek		= bch2_dir_llseek,
	.read		= generic_read_dir,
	.iterate_shared	= bch2_vfs_readdir,
	.fsync		= bch2_fsync,
	.unlocked_ioctl = bch2_fs_file_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= bch2_compat_fs_ioctl,
#endif
};

static const struct inode_operations bch_symlink_inode_operations = {
	.get_link	= page_get_link,
	.getattr	= bch2_getattr,
	.setattr	= bch2_setattr,
	.listxattr	= bch2_xattr_list,
#ifdef CONFIG_BCACHEFS_POSIX_ACL
	.get_acl	= bch2_get_acl,
	.set_acl	= bch2_set_acl,
#endif
};

static const struct inode_operations bch_special_inode_operations = {
	.getattr	= bch2_getattr,
	.setattr	= bch2_setattr,
	.listxattr	= bch2_xattr_list,
#ifdef CONFIG_BCACHEFS_POSIX_ACL
	.get_acl	= bch2_get_acl,
	.set_acl	= bch2_set_acl,
#endif
};

static const struct address_space_operations bch_address_space_operations = {
	.writepage	= bch2_writepage,
	.read_folio	= bch2_read_folio,
	.writepages	= bch2_writepages,
	.readahead	= bch2_readahead,
	.dirty_folio	= filemap_dirty_folio,
	.write_begin	= bch2_write_begin,
	.write_end	= bch2_write_end,
	.invalidate_folio = bch2_invalidate_folio,
	.release_folio	= bch2_release_folio,
	.direct_IO	= noop_direct_IO,
#ifdef CONFIG_MIGRATION
	.migrate_folio	= filemap_migrate_folio,
#endif
	.error_remove_page = generic_error_remove_page,
};

static struct inode *bch2_nfs_get_inode(struct super_block *sb,
		u64 ino, u32 generation)
{
	struct bch_fs *c = sb->s_fs_info;
	struct inode *vinode;

	if (ino < BCACHEFS_ROOT_INO)
		return ERR_PTR(-ESTALE);

	vinode = bch2_vfs_inode_get(c, ino);
	if (IS_ERR(vinode))
		return ERR_CAST(vinode);
	if (generation && vinode->i_generation != generation) {
		/* we didn't find the right inode.. */
		iput(vinode);
		return ERR_PTR(-ESTALE);
	}
	return vinode;
}

static struct dentry *bch2_fh_to_dentry(struct super_block *sb, struct fid *fid,
		int fh_len, int fh_type)
{
	return generic_fh_to_dentry(sb, fid, fh_len, fh_type,
				    bch2_nfs_get_inode);
}

static struct dentry *bch2_fh_to_parent(struct super_block *sb, struct fid *fid,
		int fh_len, int fh_type)
{
	return generic_fh_to_parent(sb, fid, fh_len, fh_type,
				    bch2_nfs_get_inode);
}

static const struct export_operations bch_export_ops = {
	.fh_to_dentry	= bch2_fh_to_dentry,
	.fh_to_parent	= bch2_fh_to_parent,
	//.get_parent	= bch2_get_parent,
};

static void bch2_vfs_inode_init(struct bch_fs *c,
				struct bch_inode_info *inode,
				struct bch_inode_unpacked *bi)
{
	bch2_inode_update_after_write(c, inode, bi, ~0);

	inode->v.i_blocks	= bi->bi_sectors;
	inode->v.i_ino		= bi->bi_inum;
	inode->v.i_rdev		= bi->bi_dev;
	inode->v.i_generation	= bi->bi_generation;
	inode->v.i_size		= bi->bi_size;

	inode->ei_journal_seq	= 0;
	inode->ei_quota_reserved = 0;
	inode->ei_str_hash	= bch2_hash_info_init(c, bi);
	inode->ei_qid		= bch_qid(bi);

	inode->v.i_mapping->a_ops = &bch_address_space_operations;

	switch (inode->v.i_mode & S_IFMT) {
	case S_IFREG:
		inode->v.i_op	= &bch_file_inode_operations;
		inode->v.i_fop	= &bch_file_operations;
		break;
	case S_IFDIR:
		inode->v.i_op	= &bch_dir_inode_operations;
		inode->v.i_fop	= &bch_dir_file_operations;
		break;
	case S_IFLNK:
		inode_nohighmem(&inode->v);
		inode->v.i_op	= &bch_symlink_inode_operations;
		break;
	default:
		init_special_inode(&inode->v, inode->v.i_mode, inode->v.i_rdev);
		inode->v.i_op	= &bch_special_inode_operations;
		break;
	}
}

static struct inode *bch2_alloc_inode(struct super_block *sb)
{
	struct bch_inode_info *inode;

	inode = kmem_cache_alloc(bch2_inode_cache, GFP_NOFS);
	if (!inode)
		return NULL;

	inode_init_once(&inode->v);
	mutex_init(&inode->ei_update_lock);
	pagecache_lock_init(&inode->ei_pagecache_lock);
	mutex_init(&inode->ei_quota_lock);
	inode->ei_journal_seq = 0;

	return &inode->v;
}

static void bch2_i_callback(struct rcu_head *head)
{
	struct inode *vinode = container_of(head, struct inode, i_rcu);
	struct bch_inode_info *inode = to_bch_ei(vinode);

	kmem_cache_free(bch2_inode_cache, inode);
}

static void bch2_destroy_inode(struct inode *vinode)
{
	call_rcu(&vinode->i_rcu, bch2_i_callback);
}

static int inode_update_times_fn(struct bch_inode_info *inode,
				 struct bch_inode_unpacked *bi,
				 void *p)
{
	struct bch_fs *c = inode->v.i_sb->s_fs_info;

	bi->bi_atime	= timespec_to_bch2_time(c, inode->v.i_atime);
	bi->bi_mtime	= timespec_to_bch2_time(c, inode->v.i_mtime);
	bi->bi_ctime	= timespec_to_bch2_time(c, inode_get_ctime(&inode->v));

	return 0;
}

static int bch2_vfs_write_inode(struct inode *vinode,
				struct writeback_control *wbc)
{
	struct bch_fs *c = vinode->i_sb->s_fs_info;
	struct bch_inode_info *inode = to_bch_ei(vinode);
	int ret;

	mutex_lock(&inode->ei_update_lock);
	ret = bch2_write_inode(c, inode, inode_update_times_fn, NULL,
			       ATTR_ATIME|ATTR_MTIME|ATTR_CTIME);
	mutex_unlock(&inode->ei_update_lock);

	return ret;
}

static void bch2_evict_inode(struct inode *vinode)
{
	struct bch_fs *c = vinode->i_sb->s_fs_info;
	struct bch_inode_info *inode = to_bch_ei(vinode);

	truncate_inode_pages_final(&inode->v.i_data);

	clear_inode(&inode->v);

	BUG_ON(!is_bad_inode(&inode->v) && inode->ei_quota_reserved);

	if (!inode->v.i_nlink && !is_bad_inode(&inode->v)) {
		bch2_quota_acct(c, inode->ei_qid, Q_SPC, -((s64) inode->v.i_blocks),
				KEY_TYPE_QUOTA_WARN);
		bch2_quota_acct(c, inode->ei_qid, Q_INO, -1,
				KEY_TYPE_QUOTA_WARN);
		bch2_inode_rm(c, inode->v.i_ino);
	}
}

static int bch2_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	struct super_block *sb = dentry->d_sb;
	struct bch_fs *c = sb->s_fs_info;
	struct bch_fs_usage_short usage = bch2_fs_usage_read_short(c);
	unsigned shift = sb->s_blocksize_bits - 9;
	u64 fsid;

	buf->f_type	= BCACHEFS_STATFS_MAGIC;
	buf->f_bsize	= sb->s_blocksize;
	buf->f_blocks	= usage.capacity >> shift;
	buf->f_bfree	= (usage.capacity - usage.used) >> shift;
	buf->f_bavail	= buf->f_bfree;
	buf->f_files	= 0;
	buf->f_ffree	= 0;

	fsid = le64_to_cpup((void *) c->sb.user_uuid.b) ^
	       le64_to_cpup((void *) c->sb.user_uuid.b + sizeof(u64));
	buf->f_fsid.val[0] = fsid & 0xFFFFFFFFUL;
	buf->f_fsid.val[1] = (fsid >> 32) & 0xFFFFFFFFUL;
	buf->f_namelen	= BCH_NAME_MAX;

	return 0;
}

static int bch2_sync_fs(struct super_block *sb, int wait)
{
	struct bch_fs *c = sb->s_fs_info;

	if (c->opts.journal_flush_disabled)
		return 0;

	if (!wait) {
		bch2_journal_flush_async(&c->journal, NULL);
		return 0;
	}

	return bch2_journal_flush(&c->journal);
}

static struct bch_fs *bch2_path_to_fs(const char *path)
{
	struct bch_fs *c;
	dev_t dev;
	int ret;

	ret = lookup_bdev(path, &dev);
	if (ret)
		return ERR_PTR(ret);

	c = bch2_dev_to_fs(dev);
	if (c)
		closure_put(&c->cl);
	return c ?: ERR_PTR(-ENOENT);
}

static char **split_devs(const char *_dev_name, unsigned *nr)
{
	char *dev_name = NULL, **devs = NULL, *s;
	size_t i, nr_devs = 0;

	dev_name = kstrdup(_dev_name, GFP_KERNEL);
	if (!dev_name)
		return NULL;

	for (s = dev_name; s; s = strchr(s + 1, ':'))
		nr_devs++;

	devs = kcalloc(nr_devs + 1, sizeof(const char *), GFP_KERNEL);
	if (!devs) {
		kfree(dev_name);
		return NULL;
	}

	for (i = 0, s = dev_name;
	     s;
	     (s = strchr(s, ':')) && (*s++ = '\0'))
		devs[i++] = s;

	*nr = nr_devs;
	return devs;
}

static int bch2_remount(struct super_block *sb, int *flags, char *data)
{
	struct bch_fs *c = sb->s_fs_info;
	struct bch_opts opts = bch2_opts_empty();
	int ret;

	opt_set(opts, read_only, (*flags & SB_RDONLY) != 0);

	ret = bch2_parse_mount_opts(c, &opts, data);
	if (ret)
		return ret;

	if (opts.read_only != c->opts.read_only) {
		down_write(&c->state_lock);

		if (opts.read_only) {
			bch2_fs_read_only(c);

			sb->s_flags |= SB_RDONLY;
		} else {
			ret = bch2_fs_read_write(c);
			if (ret) {
				bch_err(c, "error going rw: %i", ret);
				up_write(&c->state_lock);
				return -EINVAL;
			}

			sb->s_flags &= ~SB_RDONLY;
		}

		c->opts.read_only = opts.read_only;

		up_write(&c->state_lock);
	}

	if (opts.errors >= 0)
		c->opts.errors = opts.errors;

	return ret;
}

static int bch2_show_devname(struct seq_file *seq, struct dentry *root)
{
	struct bch_fs *c = root->d_sb->s_fs_info;
	struct bch_dev *ca;
	unsigned i;
	bool first = true;

	for_each_online_member(ca, c, i) {
		if (!first)
			seq_putc(seq, ':');
		first = false;
		seq_puts(seq, "/dev/");
		seq_puts(seq, ca->name);
	}

	return 0;
}

static int bch2_show_options(struct seq_file *seq, struct dentry *root)
{
	struct bch_fs *c = root->d_sb->s_fs_info;
	enum bch_opt_id i;
	char buf[512];

	for (i = 0; i < bch2_opts_nr; i++) {
		const struct bch_option *opt = &bch2_opt_table[i];
		u64 v = bch2_opt_get_by_id(&c->opts, i);

		if (!(opt->mode & OPT_MOUNT))
			continue;

		if (v == bch2_opt_get_by_id(&bch2_opts_default, i))
			continue;

		bch2_opt_to_text(&PBUF(buf), c, opt, v,
				 OPT_SHOW_MOUNT_STYLE);
		seq_putc(seq, ',');
		seq_puts(seq, buf);
	}

	return 0;
}

static void bch2_put_super(struct super_block *sb)
{
	struct bch_fs *c = sb->s_fs_info;

	__bch2_fs_stop(c);
}

static const struct super_operations bch_super_operations = {
	.alloc_inode	= bch2_alloc_inode,
	.destroy_inode	= bch2_destroy_inode,
	.write_inode	= bch2_vfs_write_inode,
	.evict_inode	= bch2_evict_inode,
	.sync_fs	= bch2_sync_fs,
	.statfs		= bch2_statfs,
	.show_devname	= bch2_show_devname,
	.show_options	= bch2_show_options,
	.remount_fs	= bch2_remount,
	.put_super	= bch2_put_super,
#if 0
	.freeze_fs	= bch2_freeze,
	.unfreeze_fs	= bch2_unfreeze,
#endif
};

static int bch2_set_super(struct super_block *s, void *data)
{
	s->s_fs_info = data;
	return 0;
}

static int bch2_noset_super(struct super_block *s, void *data)
{
	return -EBUSY;
}

static int bch2_test_super(struct super_block *s, void *data)
{
	struct bch_fs *c = s->s_fs_info;
	struct bch_fs **devs = data;
	unsigned i;

	if (!c)
		return false;

	for (i = 0; devs[i]; i++)
		if (c != devs[i])
			return false;
	return true;
}

static struct dentry *bch2_mount(struct file_system_type *fs_type,
				 int flags, const char *dev_name, void *data)
{
	struct bch_fs *c;
	struct bch_dev *ca;
	struct super_block *sb;
	struct inode *vinode;
	struct bch_opts opts = bch2_opts_empty();
	char **devs;
	struct bch_fs **devs_to_fs = NULL;
	unsigned i, nr_devs;
	int ret;

	opt_set(opts, read_only, (flags & SB_RDONLY) != 0);

	ret = bch2_parse_mount_opts(NULL, &opts, data);
	if (ret)
		return ERR_PTR(ret);

	devs = split_devs(dev_name, &nr_devs);
	if (!devs)
		return ERR_PTR(-ENOMEM);

	devs_to_fs = kcalloc(nr_devs + 1, sizeof(void *), GFP_KERNEL);
	if (!devs_to_fs) {
		sb = ERR_PTR(-ENOMEM);
		goto got_sb;
	}

	for (i = 0; i < nr_devs; i++)
		devs_to_fs[i] = bch2_path_to_fs(devs[i]);

	sb = sget(fs_type, bch2_test_super, bch2_noset_super,
		  flags|SB_NOSEC, devs_to_fs);
	if (!IS_ERR(sb))
		goto got_sb;

	c = bch2_fs_open(devs, nr_devs, opts);
	if (IS_ERR(c)) {
		sb = ERR_CAST(c);
		goto got_sb;
	}

	/* Some options can't be parsed until after the fs is started: */
	ret = bch2_parse_mount_opts(c, &opts, data);
	if (ret) {
		bch2_fs_stop(c);
		sb = ERR_PTR(ret);
		goto got_sb;
	}

	bch2_opts_apply(&c->opts, opts);

	sb = sget(fs_type, NULL, bch2_set_super, flags|SB_NOSEC, c);
	if (IS_ERR(sb))
		bch2_fs_stop(c);
got_sb:
	kfree(devs_to_fs);
	kfree(devs[0]);
	kfree(devs);

	if (IS_ERR(sb))
		return ERR_CAST(sb);

	c = sb->s_fs_info;

	if (sb->s_root) {
		if ((flags ^ sb->s_flags) & SB_RDONLY) {
			ret = -EBUSY;
			goto err_put_super;
		}
		goto out;
	}

	sb->s_blocksize		= block_bytes(c);
	sb->s_blocksize_bits	= ilog2(block_bytes(c));
	sb->s_maxbytes		= MAX_LFS_FILESIZE;
	sb->s_op		= &bch_super_operations;
	sb->s_export_op		= &bch_export_ops;
#ifdef CONFIG_BCACHEFS_QUOTA
	sb->s_qcop		= &bch2_quotactl_operations;
	sb->s_quota_types	= QTYPE_MASK_USR|QTYPE_MASK_GRP|QTYPE_MASK_PRJ;
#endif
	sb->s_xattr		= bch2_xattr_handlers;
	sb->s_magic		= BCACHEFS_STATFS_MAGIC;
	sb->s_time_gran		= c->sb.time_precision;
	c->vfs_sb		= sb;
	strlcpy(sb->s_id, c->name, sizeof(sb->s_id));

	ret = super_setup_bdi(sb);
	if (ret)
		goto err_put_super;

	sb->s_bdi->ra_pages		= VM_READAHEAD_PAGES;

	for_each_online_member(ca, c, i) {
		struct block_device *bdev = ca->disk_sb.bdev;

		/* XXX: create an anonymous device for multi device filesystems */
		sb->s_bdev	= bdev;
		sb->s_dev	= bdev->bd_dev;
		percpu_ref_put(&ca->io_ref);
		break;
	}

#ifdef CONFIG_BCACHEFS_POSIX_ACL
	if (c->opts.acl)
		sb->s_flags	|= SB_POSIXACL;
#endif

	vinode = bch2_vfs_inode_get(c, BCACHEFS_ROOT_INO);
	if (IS_ERR(vinode)) {
		bch_err(c, "error mounting: error getting root inode %i",
			(int) PTR_ERR(vinode));
		ret = PTR_ERR(vinode);
		goto err_put_super;
	}

	sb->s_root = d_make_root(vinode);
	if (!sb->s_root) {
		bch_err(c, "error mounting: error allocating root dentry");
		ret = -ENOMEM;
		goto err_put_super;
	}

	sb->s_flags |= SB_ACTIVE;
out:
	return dget(sb->s_root);

err_put_super:
	deactivate_locked_super(sb);
	return ERR_PTR(ret);
}

static void bch2_kill_sb(struct super_block *sb)
{
	struct bch_fs *c = sb->s_fs_info;

	generic_shutdown_super(sb);
	bch2_fs_free(c);
}

static struct file_system_type bcache_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "bcachefs",
	.mount		= bch2_mount,
	.kill_sb	= bch2_kill_sb,
	.fs_flags	= FS_REQUIRES_DEV,
};

MODULE_ALIAS_FS("bcachefs");

void bch2_vfs_exit(void)
{
	unregister_filesystem(&bcache_fs_type);
	if (bch2_inode_cache)
		kmem_cache_destroy(bch2_inode_cache);
}

int __init bch2_vfs_init(void)
{
	int ret = -ENOMEM;

	bch2_inode_cache = KMEM_CACHE(bch_inode_info, 0);
	if (!bch2_inode_cache)
		goto err;

	ret = register_filesystem(&bcache_fs_type);
	if (ret)
		goto err;

	return 0;
err:
	bch2_vfs_exit();
	return ret;
}

#endif /* NO_BCACHEFS_FS */
