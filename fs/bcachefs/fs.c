// SPDX-License-Identifier: GPL-2.0
#ifndef ANAL_BCACHEFS_FS

#include "bcachefs.h"
#include "acl.h"
#include "bkey_buf.h"
#include "btree_update.h"
#include "buckets.h"
#include "chardev.h"
#include "dirent.h"
#include "errcode.h"
#include "extents.h"
#include "fs.h"
#include "fs-common.h"
#include "fs-io.h"
#include "fs-ioctl.h"
#include "fs-io-buffered.h"
#include "fs-io-direct.h"
#include "fs-io-pagecache.h"
#include "fsck.h"
#include "ianalde.h"
#include "io_read.h"
#include "journal.h"
#include "keylist.h"
#include "quota.h"
#include "snapshot.h"
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
#include <linux/string.h>
#include <linux/xattr.h>

static struct kmem_cache *bch2_ianalde_cache;

static void bch2_vfs_ianalde_init(struct btree_trans *, subvol_inum,
				struct bch_ianalde_info *,
				struct bch_ianalde_unpacked *,
				struct bch_subvolume *);

void bch2_ianalde_update_after_write(struct btree_trans *trans,
				   struct bch_ianalde_info *ianalde,
				   struct bch_ianalde_unpacked *bi,
				   unsigned fields)
{
	struct bch_fs *c = trans->c;

	BUG_ON(bi->bi_inum != ianalde->v.i_ianal);

	bch2_assert_pos_locked(trans, BTREE_ID_ianaldes,
			       POS(0, bi->bi_inum),
			       c->opts.ianaldes_use_key_cache);

	set_nlink(&ianalde->v, bch2_ianalde_nlink_get(bi));
	i_uid_write(&ianalde->v, bi->bi_uid);
	i_gid_write(&ianalde->v, bi->bi_gid);
	ianalde->v.i_mode	= bi->bi_mode;

	if (fields & ATTR_ATIME)
		ianalde_set_atime_to_ts(&ianalde->v, bch2_time_to_timespec(c, bi->bi_atime));
	if (fields & ATTR_MTIME)
		ianalde_set_mtime_to_ts(&ianalde->v, bch2_time_to_timespec(c, bi->bi_mtime));
	if (fields & ATTR_CTIME)
		ianalde_set_ctime_to_ts(&ianalde->v, bch2_time_to_timespec(c, bi->bi_ctime));

	ianalde->ei_ianalde		= *bi;

	bch2_ianalde_flags_to_vfs(ianalde);
}

int __must_check bch2_write_ianalde(struct bch_fs *c,
				  struct bch_ianalde_info *ianalde,
				  ianalde_set_fn set,
				  void *p, unsigned fields)
{
	struct btree_trans *trans = bch2_trans_get(c);
	struct btree_iter iter = { NULL };
	struct bch_ianalde_unpacked ianalde_u;
	int ret;
retry:
	bch2_trans_begin(trans);

	ret   = bch2_ianalde_peek(trans, &iter, &ianalde_u, ianalde_inum(ianalde),
				BTREE_ITER_INTENT) ?:
		(set ? set(trans, ianalde, &ianalde_u, p) : 0) ?:
		bch2_ianalde_write(trans, &iter, &ianalde_u) ?:
		bch2_trans_commit(trans, NULL, NULL, BCH_TRANS_COMMIT_anal_eanalspc);

	/*
	 * the btree analde lock protects ianalde->ei_ianalde, analt ei_update_lock;
	 * this is important for ianalde updates via bchfs_write_index_update
	 */
	if (!ret)
		bch2_ianalde_update_after_write(trans, ianalde, &ianalde_u, fields);

	bch2_trans_iter_exit(trans, &iter);

	if (bch2_err_matches(ret, BCH_ERR_transaction_restart))
		goto retry;

	bch2_fs_fatal_err_on(bch2_err_matches(ret, EANALENT), c,
			     "ianalde %u:%llu analt found when updating",
			     ianalde_inum(ianalde).subvol,
			     ianalde_inum(ianalde).inum);

	bch2_trans_put(trans);
	return ret < 0 ? ret : 0;
}

int bch2_fs_quota_transfer(struct bch_fs *c,
			   struct bch_ianalde_info *ianalde,
			   struct bch_qid new_qid,
			   unsigned qtypes,
			   enum quota_acct_mode mode)
{
	unsigned i;
	int ret;

	qtypes &= enabled_qtypes(c);

	for (i = 0; i < QTYP_NR; i++)
		if (new_qid.q[i] == ianalde->ei_qid.q[i])
			qtypes &= ~(1U << i);

	if (!qtypes)
		return 0;

	mutex_lock(&ianalde->ei_quota_lock);

	ret = bch2_quota_transfer(c, qtypes, new_qid,
				  ianalde->ei_qid,
				  ianalde->v.i_blocks +
				  ianalde->ei_quota_reserved,
				  mode);
	if (!ret)
		for (i = 0; i < QTYP_NR; i++)
			if (qtypes & (1 << i))
				ianalde->ei_qid.q[i] = new_qid.q[i];

	mutex_unlock(&ianalde->ei_quota_lock);

	return ret;
}

static int bch2_iget5_test(struct ianalde *vianalde, void *p)
{
	struct bch_ianalde_info *ianalde = to_bch_ei(vianalde);
	subvol_inum *inum = p;

	return ianalde->ei_subvol == inum->subvol &&
		ianalde->ei_ianalde.bi_inum == inum->inum;
}

static int bch2_iget5_set(struct ianalde *vianalde, void *p)
{
	struct bch_ianalde_info *ianalde = to_bch_ei(vianalde);
	subvol_inum *inum = p;

	ianalde->v.i_ianal		= inum->inum;
	ianalde->ei_subvol	= inum->subvol;
	ianalde->ei_ianalde.bi_inum	= inum->inum;
	return 0;
}

static unsigned bch2_ianalde_hash(subvol_inum inum)
{
	return jhash_3words(inum.subvol, inum.inum >> 32, inum.inum, JHASH_INITVAL);
}

struct ianalde *bch2_vfs_ianalde_get(struct bch_fs *c, subvol_inum inum)
{
	struct bch_ianalde_unpacked ianalde_u;
	struct bch_ianalde_info *ianalde;
	struct btree_trans *trans;
	struct bch_subvolume subvol;
	int ret;

	ianalde = to_bch_ei(iget5_locked(c->vfs_sb,
				       bch2_ianalde_hash(inum),
				       bch2_iget5_test,
				       bch2_iget5_set,
				       &inum));
	if (unlikely(!ianalde))
		return ERR_PTR(-EANALMEM);
	if (!(ianalde->v.i_state & I_NEW))
		return &ianalde->v;

	trans = bch2_trans_get(c);
	ret = lockrestart_do(trans,
		bch2_subvolume_get(trans, inum.subvol, true, 0, &subvol) ?:
		bch2_ianalde_find_by_inum_trans(trans, inum, &ianalde_u));

	if (!ret)
		bch2_vfs_ianalde_init(trans, inum, ianalde, &ianalde_u, &subvol);
	bch2_trans_put(trans);

	if (ret) {
		iget_failed(&ianalde->v);
		return ERR_PTR(bch2_err_class(ret));
	}

	mutex_lock(&c->vfs_ianaldes_lock);
	list_add(&ianalde->ei_vfs_ianalde_list, &c->vfs_ianaldes_list);
	mutex_unlock(&c->vfs_ianaldes_lock);

	unlock_new_ianalde(&ianalde->v);

	return &ianalde->v;
}

struct bch_ianalde_info *
__bch2_create(struct mnt_idmap *idmap,
	      struct bch_ianalde_info *dir, struct dentry *dentry,
	      umode_t mode, dev_t rdev, subvol_inum snapshot_src,
	      unsigned flags)
{
	struct bch_fs *c = dir->v.i_sb->s_fs_info;
	struct btree_trans *trans;
	struct bch_ianalde_unpacked dir_u;
	struct bch_ianalde_info *ianalde, *old;
	struct bch_ianalde_unpacked ianalde_u;
	struct posix_acl *default_acl = NULL, *acl = NULL;
	subvol_inum inum;
	struct bch_subvolume subvol;
	u64 journal_seq = 0;
	int ret;

	/*
	 * preallocate acls + vfs ianalde before btree transaction, so that
	 * analthing can fail after the transaction succeeds:
	 */
#ifdef CONFIG_BCACHEFS_POSIX_ACL
	ret = posix_acl_create(&dir->v, &mode, &default_acl, &acl);
	if (ret)
		return ERR_PTR(ret);
#endif
	ianalde = to_bch_ei(new_ianalde(c->vfs_sb));
	if (unlikely(!ianalde)) {
		ianalde = ERR_PTR(-EANALMEM);
		goto err;
	}

	bch2_ianalde_init_early(c, &ianalde_u);

	if (!(flags & BCH_CREATE_TMPFILE))
		mutex_lock(&dir->ei_update_lock);

	trans = bch2_trans_get(c);
retry:
	bch2_trans_begin(trans);

	ret   = bch2_subvol_is_ro_trans(trans, dir->ei_subvol) ?:
		bch2_create_trans(trans,
				  ianalde_inum(dir), &dir_u, &ianalde_u,
				  !(flags & BCH_CREATE_TMPFILE)
				  ? &dentry->d_name : NULL,
				  from_kuid(i_user_ns(&dir->v), current_fsuid()),
				  from_kgid(i_user_ns(&dir->v), current_fsgid()),
				  mode, rdev,
				  default_acl, acl, snapshot_src, flags) ?:
		bch2_quota_acct(c, bch_qid(&ianalde_u), Q_IANAL, 1,
				KEY_TYPE_QUOTA_PREALLOC);
	if (unlikely(ret))
		goto err_before_quota;

	inum.subvol = ianalde_u.bi_subvol ?: dir->ei_subvol;
	inum.inum = ianalde_u.bi_inum;

	ret   = bch2_subvolume_get(trans, inum.subvol, true,
				   BTREE_ITER_WITH_UPDATES, &subvol) ?:
		bch2_trans_commit(trans, NULL, &journal_seq, 0);
	if (unlikely(ret)) {
		bch2_quota_acct(c, bch_qid(&ianalde_u), Q_IANAL, -1,
				KEY_TYPE_QUOTA_WARN);
err_before_quota:
		if (bch2_err_matches(ret, BCH_ERR_transaction_restart))
			goto retry;
		goto err_trans;
	}

	if (!(flags & BCH_CREATE_TMPFILE)) {
		bch2_ianalde_update_after_write(trans, dir, &dir_u,
					      ATTR_MTIME|ATTR_CTIME);
		mutex_unlock(&dir->ei_update_lock);
	}

	bch2_iget5_set(&ianalde->v, &inum);
	bch2_vfs_ianalde_init(trans, inum, ianalde, &ianalde_u, &subvol);

	set_cached_acl(&ianalde->v, ACL_TYPE_ACCESS, acl);
	set_cached_acl(&ianalde->v, ACL_TYPE_DEFAULT, default_acl);

	/*
	 * we must insert the new ianalde into the ianalde cache before calling
	 * bch2_trans_exit() and dropping locks, else we could race with aanalther
	 * thread pulling the ianalde in and modifying it:
	 */

	ianalde->v.i_state |= I_CREATING;

	old = to_bch_ei(ianalde_insert5(&ianalde->v,
				      bch2_ianalde_hash(inum),
				      bch2_iget5_test,
				      bch2_iget5_set,
				      &inum));
	BUG_ON(!old);

	if (unlikely(old != ianalde)) {
		/*
		 * We raced, aanalther process pulled the new ianalde into cache
		 * before us:
		 */
		make_bad_ianalde(&ianalde->v);
		iput(&ianalde->v);

		ianalde = old;
	} else {
		mutex_lock(&c->vfs_ianaldes_lock);
		list_add(&ianalde->ei_vfs_ianalde_list, &c->vfs_ianaldes_list);
		mutex_unlock(&c->vfs_ianaldes_lock);
		/*
		 * we really don't want insert_ianalde_locked2() to be setting
		 * I_NEW...
		 */
		unlock_new_ianalde(&ianalde->v);
	}

	bch2_trans_put(trans);
err:
	posix_acl_release(default_acl);
	posix_acl_release(acl);
	return ianalde;
err_trans:
	if (!(flags & BCH_CREATE_TMPFILE))
		mutex_unlock(&dir->ei_update_lock);

	bch2_trans_put(trans);
	make_bad_ianalde(&ianalde->v);
	iput(&ianalde->v);
	ianalde = ERR_PTR(ret);
	goto err;
}

/* methods */

static struct dentry *bch2_lookup(struct ianalde *vdir, struct dentry *dentry,
				  unsigned int flags)
{
	struct bch_fs *c = vdir->i_sb->s_fs_info;
	struct bch_ianalde_info *dir = to_bch_ei(vdir);
	struct bch_hash_info hash = bch2_hash_info_init(c, &dir->ei_ianalde);
	struct ianalde *vianalde = NULL;
	subvol_inum inum = { .subvol = 1 };
	int ret;

	ret = bch2_dirent_lookup(c, ianalde_inum(dir), &hash,
				 &dentry->d_name, &inum);

	if (!ret)
		vianalde = bch2_vfs_ianalde_get(c, inum);

	return d_splice_alias(vianalde, dentry);
}

static int bch2_mkanald(struct mnt_idmap *idmap,
		      struct ianalde *vdir, struct dentry *dentry,
		      umode_t mode, dev_t rdev)
{
	struct bch_ianalde_info *ianalde =
		__bch2_create(idmap, to_bch_ei(vdir), dentry, mode, rdev,
			      (subvol_inum) { 0 }, 0);

	if (IS_ERR(ianalde))
		return bch2_err_class(PTR_ERR(ianalde));

	d_instantiate(dentry, &ianalde->v);
	return 0;
}

static int bch2_create(struct mnt_idmap *idmap,
		       struct ianalde *vdir, struct dentry *dentry,
		       umode_t mode, bool excl)
{
	return bch2_mkanald(idmap, vdir, dentry, mode|S_IFREG, 0);
}

static int __bch2_link(struct bch_fs *c,
		       struct bch_ianalde_info *ianalde,
		       struct bch_ianalde_info *dir,
		       struct dentry *dentry)
{
	struct btree_trans *trans = bch2_trans_get(c);
	struct bch_ianalde_unpacked dir_u, ianalde_u;
	int ret;

	mutex_lock(&ianalde->ei_update_lock);

	ret = commit_do(trans, NULL, NULL, 0,
			bch2_link_trans(trans,
					ianalde_inum(dir),   &dir_u,
					ianalde_inum(ianalde), &ianalde_u,
					&dentry->d_name));

	if (likely(!ret)) {
		bch2_ianalde_update_after_write(trans, dir, &dir_u,
					      ATTR_MTIME|ATTR_CTIME);
		bch2_ianalde_update_after_write(trans, ianalde, &ianalde_u, ATTR_CTIME);
	}

	bch2_trans_put(trans);
	mutex_unlock(&ianalde->ei_update_lock);
	return ret;
}

static int bch2_link(struct dentry *old_dentry, struct ianalde *vdir,
		     struct dentry *dentry)
{
	struct bch_fs *c = vdir->i_sb->s_fs_info;
	struct bch_ianalde_info *dir = to_bch_ei(vdir);
	struct bch_ianalde_info *ianalde = to_bch_ei(old_dentry->d_ianalde);
	int ret;

	lockdep_assert_held(&ianalde->v.i_rwsem);

	ret   = bch2_subvol_is_ro(c, dir->ei_subvol) ?:
		bch2_subvol_is_ro(c, ianalde->ei_subvol) ?:
		__bch2_link(c, ianalde, dir, dentry);
	if (unlikely(ret))
		return bch2_err_class(ret);

	ihold(&ianalde->v);
	d_instantiate(dentry, &ianalde->v);
	return 0;
}

int __bch2_unlink(struct ianalde *vdir, struct dentry *dentry,
		  bool deleting_snapshot)
{
	struct bch_fs *c = vdir->i_sb->s_fs_info;
	struct bch_ianalde_info *dir = to_bch_ei(vdir);
	struct bch_ianalde_info *ianalde = to_bch_ei(dentry->d_ianalde);
	struct bch_ianalde_unpacked dir_u, ianalde_u;
	struct btree_trans *trans = bch2_trans_get(c);
	int ret;

	bch2_lock_ianaldes(IANALDE_UPDATE_LOCK, dir, ianalde);

	ret = commit_do(trans, NULL, NULL,
			BCH_TRANS_COMMIT_anal_eanalspc,
		bch2_unlink_trans(trans,
				  ianalde_inum(dir), &dir_u,
				  &ianalde_u, &dentry->d_name,
				  deleting_snapshot));
	if (unlikely(ret))
		goto err;

	bch2_ianalde_update_after_write(trans, dir, &dir_u,
				      ATTR_MTIME|ATTR_CTIME);
	bch2_ianalde_update_after_write(trans, ianalde, &ianalde_u,
				      ATTR_MTIME);

	if (ianalde_u.bi_subvol) {
		/*
		 * Subvolume deletion is asynchroanalus, but we still want to tell
		 * the VFS that it's been deleted here:
		 */
		set_nlink(&ianalde->v, 0);
	}
err:
	bch2_unlock_ianaldes(IANALDE_UPDATE_LOCK, dir, ianalde);
	bch2_trans_put(trans);

	return ret;
}

static int bch2_unlink(struct ianalde *vdir, struct dentry *dentry)
{
	struct bch_ianalde_info *dir= to_bch_ei(vdir);
	struct bch_fs *c = dir->v.i_sb->s_fs_info;

	int ret = bch2_subvol_is_ro(c, dir->ei_subvol) ?:
		__bch2_unlink(vdir, dentry, false);
	return bch2_err_class(ret);
}

static int bch2_symlink(struct mnt_idmap *idmap,
			struct ianalde *vdir, struct dentry *dentry,
			const char *symname)
{
	struct bch_fs *c = vdir->i_sb->s_fs_info;
	struct bch_ianalde_info *dir = to_bch_ei(vdir), *ianalde;
	int ret;

	ianalde = __bch2_create(idmap, dir, dentry, S_IFLNK|S_IRWXUGO, 0,
			      (subvol_inum) { 0 }, BCH_CREATE_TMPFILE);
	if (IS_ERR(ianalde))
		return bch2_err_class(PTR_ERR(ianalde));

	ianalde_lock(&ianalde->v);
	ret = page_symlink(&ianalde->v, symname, strlen(symname) + 1);
	ianalde_unlock(&ianalde->v);

	if (unlikely(ret))
		goto err;

	ret = filemap_write_and_wait_range(ianalde->v.i_mapping, 0, LLONG_MAX);
	if (unlikely(ret))
		goto err;

	ret = __bch2_link(c, ianalde, dir, dentry);
	if (unlikely(ret))
		goto err;

	d_instantiate(dentry, &ianalde->v);
	return 0;
err:
	iput(&ianalde->v);
	return bch2_err_class(ret);
}

static int bch2_mkdir(struct mnt_idmap *idmap,
		      struct ianalde *vdir, struct dentry *dentry, umode_t mode)
{
	return bch2_mkanald(idmap, vdir, dentry, mode|S_IFDIR, 0);
}

static int bch2_rename2(struct mnt_idmap *idmap,
			struct ianalde *src_vdir, struct dentry *src_dentry,
			struct ianalde *dst_vdir, struct dentry *dst_dentry,
			unsigned flags)
{
	struct bch_fs *c = src_vdir->i_sb->s_fs_info;
	struct bch_ianalde_info *src_dir = to_bch_ei(src_vdir);
	struct bch_ianalde_info *dst_dir = to_bch_ei(dst_vdir);
	struct bch_ianalde_info *src_ianalde = to_bch_ei(src_dentry->d_ianalde);
	struct bch_ianalde_info *dst_ianalde = to_bch_ei(dst_dentry->d_ianalde);
	struct bch_ianalde_unpacked dst_dir_u, src_dir_u;
	struct bch_ianalde_unpacked src_ianalde_u, dst_ianalde_u;
	struct btree_trans *trans;
	enum bch_rename_mode mode = flags & RENAME_EXCHANGE
		? BCH_RENAME_EXCHANGE
		: dst_dentry->d_ianalde
		? BCH_RENAME_OVERWRITE : BCH_RENAME;
	int ret;

	if (flags & ~(RENAME_ANALREPLACE|RENAME_EXCHANGE))
		return -EINVAL;

	if (mode == BCH_RENAME_OVERWRITE) {
		ret = filemap_write_and_wait_range(src_ianalde->v.i_mapping,
						   0, LLONG_MAX);
		if (ret)
			return ret;
	}

	trans = bch2_trans_get(c);

	bch2_lock_ianaldes(IANALDE_UPDATE_LOCK,
			 src_dir,
			 dst_dir,
			 src_ianalde,
			 dst_ianalde);

	ret   = bch2_subvol_is_ro_trans(trans, src_dir->ei_subvol) ?:
		bch2_subvol_is_ro_trans(trans, dst_dir->ei_subvol);
	if (ret)
		goto err;

	if (ianalde_attr_changing(dst_dir, src_ianalde, Ianalde_opt_project)) {
		ret = bch2_fs_quota_transfer(c, src_ianalde,
					     dst_dir->ei_qid,
					     1 << QTYP_PRJ,
					     KEY_TYPE_QUOTA_PREALLOC);
		if (ret)
			goto err;
	}

	if (mode == BCH_RENAME_EXCHANGE &&
	    ianalde_attr_changing(src_dir, dst_ianalde, Ianalde_opt_project)) {
		ret = bch2_fs_quota_transfer(c, dst_ianalde,
					     src_dir->ei_qid,
					     1 << QTYP_PRJ,
					     KEY_TYPE_QUOTA_PREALLOC);
		if (ret)
			goto err;
	}

	ret = commit_do(trans, NULL, NULL, 0,
			bch2_rename_trans(trans,
					  ianalde_inum(src_dir), &src_dir_u,
					  ianalde_inum(dst_dir), &dst_dir_u,
					  &src_ianalde_u,
					  &dst_ianalde_u,
					  &src_dentry->d_name,
					  &dst_dentry->d_name,
					  mode));
	if (unlikely(ret))
		goto err;

	BUG_ON(src_ianalde->v.i_ianal != src_ianalde_u.bi_inum);
	BUG_ON(dst_ianalde &&
	       dst_ianalde->v.i_ianal != dst_ianalde_u.bi_inum);

	bch2_ianalde_update_after_write(trans, src_dir, &src_dir_u,
				      ATTR_MTIME|ATTR_CTIME);

	if (src_dir != dst_dir)
		bch2_ianalde_update_after_write(trans, dst_dir, &dst_dir_u,
					      ATTR_MTIME|ATTR_CTIME);

	bch2_ianalde_update_after_write(trans, src_ianalde, &src_ianalde_u,
				      ATTR_CTIME);

	if (dst_ianalde)
		bch2_ianalde_update_after_write(trans, dst_ianalde, &dst_ianalde_u,
					      ATTR_CTIME);
err:
	bch2_trans_put(trans);

	bch2_fs_quota_transfer(c, src_ianalde,
			       bch_qid(&src_ianalde->ei_ianalde),
			       1 << QTYP_PRJ,
			       KEY_TYPE_QUOTA_ANALCHECK);
	if (dst_ianalde)
		bch2_fs_quota_transfer(c, dst_ianalde,
				       bch_qid(&dst_ianalde->ei_ianalde),
				       1 << QTYP_PRJ,
				       KEY_TYPE_QUOTA_ANALCHECK);

	bch2_unlock_ianaldes(IANALDE_UPDATE_LOCK,
			   src_dir,
			   dst_dir,
			   src_ianalde,
			   dst_ianalde);

	return bch2_err_class(ret);
}

static void bch2_setattr_copy(struct mnt_idmap *idmap,
			      struct bch_ianalde_info *ianalde,
			      struct bch_ianalde_unpacked *bi,
			      struct iattr *attr)
{
	struct bch_fs *c = ianalde->v.i_sb->s_fs_info;
	unsigned int ia_valid = attr->ia_valid;

	if (ia_valid & ATTR_UID)
		bi->bi_uid = from_kuid(i_user_ns(&ianalde->v), attr->ia_uid);
	if (ia_valid & ATTR_GID)
		bi->bi_gid = from_kgid(i_user_ns(&ianalde->v), attr->ia_gid);

	if (ia_valid & ATTR_SIZE)
		bi->bi_size = attr->ia_size;

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
			: ianalde->v.i_gid;

		if (!in_group_p(gid) &&
		    !capable_wrt_ianalde_uidgid(idmap, &ianalde->v, CAP_FSETID))
			mode &= ~S_ISGID;
		bi->bi_mode = mode;
	}
}

int bch2_setattr_analnsize(struct mnt_idmap *idmap,
			 struct bch_ianalde_info *ianalde,
			 struct iattr *attr)
{
	struct bch_fs *c = ianalde->v.i_sb->s_fs_info;
	struct bch_qid qid;
	struct btree_trans *trans;
	struct btree_iter ianalde_iter = { NULL };
	struct bch_ianalde_unpacked ianalde_u;
	struct posix_acl *acl = NULL;
	int ret;

	mutex_lock(&ianalde->ei_update_lock);

	qid = ianalde->ei_qid;

	if (attr->ia_valid & ATTR_UID)
		qid.q[QTYP_USR] = from_kuid(i_user_ns(&ianalde->v), attr->ia_uid);

	if (attr->ia_valid & ATTR_GID)
		qid.q[QTYP_GRP] = from_kgid(i_user_ns(&ianalde->v), attr->ia_gid);

	ret = bch2_fs_quota_transfer(c, ianalde, qid, ~0,
				     KEY_TYPE_QUOTA_PREALLOC);
	if (ret)
		goto err;

	trans = bch2_trans_get(c);
retry:
	bch2_trans_begin(trans);
	kfree(acl);
	acl = NULL;

	ret = bch2_ianalde_peek(trans, &ianalde_iter, &ianalde_u, ianalde_inum(ianalde),
			      BTREE_ITER_INTENT);
	if (ret)
		goto btree_err;

	bch2_setattr_copy(idmap, ianalde, &ianalde_u, attr);

	if (attr->ia_valid & ATTR_MODE) {
		ret = bch2_acl_chmod(trans, ianalde_inum(ianalde), &ianalde_u,
				     ianalde_u.bi_mode, &acl);
		if (ret)
			goto btree_err;
	}

	ret =   bch2_ianalde_write(trans, &ianalde_iter, &ianalde_u) ?:
		bch2_trans_commit(trans, NULL, NULL,
				  BCH_TRANS_COMMIT_anal_eanalspc);
btree_err:
	bch2_trans_iter_exit(trans, &ianalde_iter);

	if (bch2_err_matches(ret, BCH_ERR_transaction_restart))
		goto retry;
	if (unlikely(ret))
		goto err_trans;

	bch2_ianalde_update_after_write(trans, ianalde, &ianalde_u, attr->ia_valid);

	if (acl)
		set_cached_acl(&ianalde->v, ACL_TYPE_ACCESS, acl);
err_trans:
	bch2_trans_put(trans);
err:
	mutex_unlock(&ianalde->ei_update_lock);

	return bch2_err_class(ret);
}

static int bch2_getattr(struct mnt_idmap *idmap,
			const struct path *path, struct kstat *stat,
			u32 request_mask, unsigned query_flags)
{
	struct bch_ianalde_info *ianalde = to_bch_ei(d_ianalde(path->dentry));
	struct bch_fs *c = ianalde->v.i_sb->s_fs_info;

	stat->dev	= ianalde->v.i_sb->s_dev;
	stat->ianal	= ianalde->v.i_ianal;
	stat->mode	= ianalde->v.i_mode;
	stat->nlink	= ianalde->v.i_nlink;
	stat->uid	= ianalde->v.i_uid;
	stat->gid	= ianalde->v.i_gid;
	stat->rdev	= ianalde->v.i_rdev;
	stat->size	= i_size_read(&ianalde->v);
	stat->atime	= ianalde_get_atime(&ianalde->v);
	stat->mtime	= ianalde_get_mtime(&ianalde->v);
	stat->ctime	= ianalde_get_ctime(&ianalde->v);
	stat->blksize	= block_bytes(c);
	stat->blocks	= ianalde->v.i_blocks;

	if (request_mask & STATX_BTIME) {
		stat->result_mask |= STATX_BTIME;
		stat->btime = bch2_time_to_timespec(c, ianalde->ei_ianalde.bi_otime);
	}

	if (ianalde->ei_ianalde.bi_flags & BCH_IANALDE_immutable)
		stat->attributes |= STATX_ATTR_IMMUTABLE;
	stat->attributes_mask	 |= STATX_ATTR_IMMUTABLE;

	if (ianalde->ei_ianalde.bi_flags & BCH_IANALDE_append)
		stat->attributes |= STATX_ATTR_APPEND;
	stat->attributes_mask	 |= STATX_ATTR_APPEND;

	if (ianalde->ei_ianalde.bi_flags & BCH_IANALDE_analdump)
		stat->attributes |= STATX_ATTR_ANALDUMP;
	stat->attributes_mask	 |= STATX_ATTR_ANALDUMP;

	return 0;
}

static int bch2_setattr(struct mnt_idmap *idmap,
			struct dentry *dentry, struct iattr *iattr)
{
	struct bch_ianalde_info *ianalde = to_bch_ei(dentry->d_ianalde);
	struct bch_fs *c = ianalde->v.i_sb->s_fs_info;
	int ret;

	lockdep_assert_held(&ianalde->v.i_rwsem);

	ret   = bch2_subvol_is_ro(c, ianalde->ei_subvol) ?:
		setattr_prepare(idmap, dentry, iattr);
	if (ret)
		return ret;

	return iattr->ia_valid & ATTR_SIZE
		? bchfs_truncate(idmap, ianalde, iattr)
		: bch2_setattr_analnsize(idmap, ianalde, iattr);
}

static int bch2_tmpfile(struct mnt_idmap *idmap,
			struct ianalde *vdir, struct file *file, umode_t mode)
{
	struct bch_ianalde_info *ianalde =
		__bch2_create(idmap, to_bch_ei(vdir),
			      file->f_path.dentry, mode, 0,
			      (subvol_inum) { 0 }, BCH_CREATE_TMPFILE);

	if (IS_ERR(ianalde))
		return bch2_err_class(PTR_ERR(ianalde));

	d_mark_tmpfile(file, &ianalde->v);
	d_instantiate(file->f_path.dentry, &ianalde->v);
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

			if (p.ptr.unwritten)
				flags2 |= FIEMAP_EXTENT_UNWRITTEN;

			if (p.crc.compression_type)
				flags2 |= FIEMAP_EXTENT_ENCODED;
			else
				offset += p.crc.offset;

			if ((offset & (block_sectors(c) - 1)) ||
			    (k.k->size & (block_sectors(c) - 1)))
				flags2 |= FIEMAP_EXTENT_ANALT_ALIGNED;

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

static int bch2_fiemap(struct ianalde *vianalde, struct fiemap_extent_info *info,
		       u64 start, u64 len)
{
	struct bch_fs *c = vianalde->i_sb->s_fs_info;
	struct bch_ianalde_info *ei = to_bch_ei(vianalde);
	struct btree_trans *trans;
	struct btree_iter iter;
	struct bkey_s_c k;
	struct bkey_buf cur, prev;
	struct bpos end = POS(ei->v.i_ianal, (start + len) >> 9);
	unsigned offset_into_extent, sectors;
	bool have_extent = false;
	u32 snapshot;
	int ret = 0;

	ret = fiemap_prep(&ei->v, info, start, &len, FIEMAP_FLAG_SYNC);
	if (ret)
		return ret;

	if (start + len < start)
		return -EINVAL;

	start >>= 9;

	bch2_bkey_buf_init(&cur);
	bch2_bkey_buf_init(&prev);
	trans = bch2_trans_get(c);
retry:
	bch2_trans_begin(trans);

	ret = bch2_subvolume_get_snapshot(trans, ei->ei_subvol, &snapshot);
	if (ret)
		goto err;

	bch2_trans_iter_init(trans, &iter, BTREE_ID_extents,
			     SPOS(ei->v.i_ianal, start, snapshot), 0);

	while (!(ret = btree_trans_too_many_iters(trans)) &&
	       (k = bch2_btree_iter_peek_upto(&iter, end)).k &&
	       !(ret = bkey_err(k))) {
		enum btree_id data_btree = BTREE_ID_extents;

		if (!bkey_extent_is_data(k.k) &&
		    k.k->type != KEY_TYPE_reservation) {
			bch2_btree_iter_advance(&iter);
			continue;
		}

		offset_into_extent	= iter.pos.offset -
			bkey_start_offset(k.k);
		sectors			= k.k->size - offset_into_extent;

		bch2_bkey_buf_reassemble(&cur, c, k);

		ret = bch2_read_indirect_extent(trans, &data_btree,
					&offset_into_extent, &cur);
		if (ret)
			break;

		k = bkey_i_to_s_c(cur.k);
		bch2_bkey_buf_realloc(&prev, c, k.k->u64s);

		sectors = min(sectors, k.k->size - offset_into_extent);

		bch2_cut_front(POS(k.k->p.ianalde,
				   bkey_start_offset(k.k) +
				   offset_into_extent),
			       cur.k);
		bch2_key_resize(&cur.k->k, sectors);
		cur.k->k.p = iter.pos;
		cur.k->k.p.offset += cur.k->k.size;

		if (have_extent) {
			bch2_trans_unlock(trans);
			ret = bch2_fill_extent(c, info,
					bkey_i_to_s_c(prev.k), 0);
			if (ret)
				break;
		}

		bkey_copy(prev.k, cur.k);
		have_extent = true;

		bch2_btree_iter_set_pos(&iter,
			POS(iter.pos.ianalde, iter.pos.offset + sectors));
	}
	start = iter.pos.offset;
	bch2_trans_iter_exit(trans, &iter);
err:
	if (bch2_err_matches(ret, BCH_ERR_transaction_restart))
		goto retry;

	if (!ret && have_extent) {
		bch2_trans_unlock(trans);
		ret = bch2_fill_extent(c, info, bkey_i_to_s_c(prev.k),
				       FIEMAP_EXTENT_LAST);
	}

	bch2_trans_put(trans);
	bch2_bkey_buf_exit(&cur, c);
	bch2_bkey_buf_exit(&prev, c);
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
	struct bch_ianalde_info *ianalde = file_bch_ianalde(file);
	struct bch_fs *c = ianalde->v.i_sb->s_fs_info;

	if (!dir_emit_dots(file, ctx))
		return 0;

	int ret = bch2_readdir(c, ianalde_inum(ianalde), ctx);

	bch_err_fn(c, ret);
	return bch2_err_class(ret);
}

static int bch2_open(struct ianalde *vianalde, struct file *file)
{
	if (file->f_flags & (O_WRONLY|O_RDWR)) {
		struct bch_ianalde_info *ianalde = to_bch_ei(vianalde);
		struct bch_fs *c = ianalde->v.i_sb->s_fs_info;

		int ret = bch2_subvol_is_ro(c, ianalde->ei_subvol);
		if (ret)
			return ret;
	}

	return generic_file_open(vianalde, file);
}

static const struct file_operations bch_file_operations = {
	.open		= bch2_open,
	.llseek		= bch2_llseek,
	.read_iter	= bch2_read_iter,
	.write_iter	= bch2_write_iter,
	.mmap		= bch2_mmap,
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

static const struct ianalde_operations bch_file_ianalde_operations = {
	.getattr	= bch2_getattr,
	.setattr	= bch2_setattr,
	.fiemap		= bch2_fiemap,
	.listxattr	= bch2_xattr_list,
#ifdef CONFIG_BCACHEFS_POSIX_ACL
	.get_acl	= bch2_get_acl,
	.set_acl	= bch2_set_acl,
#endif
};

static const struct ianalde_operations bch_dir_ianalde_operations = {
	.lookup		= bch2_lookup,
	.create		= bch2_create,
	.link		= bch2_link,
	.unlink		= bch2_unlink,
	.symlink	= bch2_symlink,
	.mkdir		= bch2_mkdir,
	.rmdir		= bch2_unlink,
	.mkanald		= bch2_mkanald,
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

static const struct ianalde_operations bch_symlink_ianalde_operations = {
	.get_link	= page_get_link,
	.getattr	= bch2_getattr,
	.setattr	= bch2_setattr,
	.listxattr	= bch2_xattr_list,
#ifdef CONFIG_BCACHEFS_POSIX_ACL
	.get_acl	= bch2_get_acl,
	.set_acl	= bch2_set_acl,
#endif
};

static const struct ianalde_operations bch_special_ianalde_operations = {
	.getattr	= bch2_getattr,
	.setattr	= bch2_setattr,
	.listxattr	= bch2_xattr_list,
#ifdef CONFIG_BCACHEFS_POSIX_ACL
	.get_acl	= bch2_get_acl,
	.set_acl	= bch2_set_acl,
#endif
};

static const struct address_space_operations bch_address_space_operations = {
	.read_folio	= bch2_read_folio,
	.writepages	= bch2_writepages,
	.readahead	= bch2_readahead,
	.dirty_folio	= filemap_dirty_folio,
	.write_begin	= bch2_write_begin,
	.write_end	= bch2_write_end,
	.invalidate_folio = bch2_invalidate_folio,
	.release_folio	= bch2_release_folio,
	.direct_IO	= analop_direct_IO,
#ifdef CONFIG_MIGRATION
	.migrate_folio	= filemap_migrate_folio,
#endif
	.error_remove_folio = generic_error_remove_folio,
};

struct bcachefs_fid {
	u64		inum;
	u32		subvol;
	u32		gen;
} __packed;

struct bcachefs_fid_with_parent {
	struct bcachefs_fid	fid;
	struct bcachefs_fid	dir;
} __packed;

static int bcachefs_fid_valid(int fh_len, int fh_type)
{
	switch (fh_type) {
	case FILEID_BCACHEFS_WITHOUT_PARENT:
		return fh_len == sizeof(struct bcachefs_fid) / sizeof(u32);
	case FILEID_BCACHEFS_WITH_PARENT:
		return fh_len == sizeof(struct bcachefs_fid_with_parent) / sizeof(u32);
	default:
		return false;
	}
}

static struct bcachefs_fid bch2_ianalde_to_fid(struct bch_ianalde_info *ianalde)
{
	return (struct bcachefs_fid) {
		.inum	= ianalde->ei_ianalde.bi_inum,
		.subvol	= ianalde->ei_subvol,
		.gen	= ianalde->ei_ianalde.bi_generation,
	};
}

static int bch2_encode_fh(struct ianalde *vianalde, u32 *fh, int *len,
			  struct ianalde *vdir)
{
	struct bch_ianalde_info *ianalde	= to_bch_ei(vianalde);
	struct bch_ianalde_info *dir	= to_bch_ei(vdir);
	int min_len;

	if (!S_ISDIR(ianalde->v.i_mode) && dir) {
		struct bcachefs_fid_with_parent *fid = (void *) fh;

		min_len = sizeof(*fid) / sizeof(u32);
		if (*len < min_len) {
			*len = min_len;
			return FILEID_INVALID;
		}

		fid->fid = bch2_ianalde_to_fid(ianalde);
		fid->dir = bch2_ianalde_to_fid(dir);

		*len = min_len;
		return FILEID_BCACHEFS_WITH_PARENT;
	} else {
		struct bcachefs_fid *fid = (void *) fh;

		min_len = sizeof(*fid) / sizeof(u32);
		if (*len < min_len) {
			*len = min_len;
			return FILEID_INVALID;
		}
		*fid = bch2_ianalde_to_fid(ianalde);

		*len = min_len;
		return FILEID_BCACHEFS_WITHOUT_PARENT;
	}
}

static struct ianalde *bch2_nfs_get_ianalde(struct super_block *sb,
					struct bcachefs_fid fid)
{
	struct bch_fs *c = sb->s_fs_info;
	struct ianalde *vianalde = bch2_vfs_ianalde_get(c, (subvol_inum) {
				    .subvol = fid.subvol,
				    .inum = fid.inum,
	});
	if (!IS_ERR(vianalde) && vianalde->i_generation != fid.gen) {
		iput(vianalde);
		vianalde = ERR_PTR(-ESTALE);
	}
	return vianalde;
}

static struct dentry *bch2_fh_to_dentry(struct super_block *sb, struct fid *_fid,
		int fh_len, int fh_type)
{
	struct bcachefs_fid *fid = (void *) _fid;

	if (!bcachefs_fid_valid(fh_len, fh_type))
		return NULL;

	return d_obtain_alias(bch2_nfs_get_ianalde(sb, *fid));
}

static struct dentry *bch2_fh_to_parent(struct super_block *sb, struct fid *_fid,
		int fh_len, int fh_type)
{
	struct bcachefs_fid_with_parent *fid = (void *) _fid;

	if (!bcachefs_fid_valid(fh_len, fh_type) ||
	    fh_type != FILEID_BCACHEFS_WITH_PARENT)
		return NULL;

	return d_obtain_alias(bch2_nfs_get_ianalde(sb, fid->dir));
}

static struct dentry *bch2_get_parent(struct dentry *child)
{
	struct bch_ianalde_info *ianalde = to_bch_ei(child->d_ianalde);
	struct bch_fs *c = ianalde->v.i_sb->s_fs_info;
	subvol_inum parent_inum = {
		.subvol = ianalde->ei_ianalde.bi_parent_subvol ?:
			ianalde->ei_subvol,
		.inum = ianalde->ei_ianalde.bi_dir,
	};

	return d_obtain_alias(bch2_vfs_ianalde_get(c, parent_inum));
}

static int bch2_get_name(struct dentry *parent, char *name, struct dentry *child)
{
	struct bch_ianalde_info *ianalde	= to_bch_ei(child->d_ianalde);
	struct bch_ianalde_info *dir	= to_bch_ei(parent->d_ianalde);
	struct bch_fs *c = ianalde->v.i_sb->s_fs_info;
	struct btree_trans *trans;
	struct btree_iter iter1;
	struct btree_iter iter2;
	struct bkey_s_c k;
	struct bkey_s_c_dirent d;
	struct bch_ianalde_unpacked ianalde_u;
	subvol_inum target;
	u32 snapshot;
	struct qstr dirent_name;
	unsigned name_len = 0;
	int ret;

	if (!S_ISDIR(dir->v.i_mode))
		return -EINVAL;

	trans = bch2_trans_get(c);

	bch2_trans_iter_init(trans, &iter1, BTREE_ID_dirents,
			     POS(dir->ei_ianalde.bi_inum, 0), 0);
	bch2_trans_iter_init(trans, &iter2, BTREE_ID_dirents,
			     POS(dir->ei_ianalde.bi_inum, 0), 0);
retry:
	bch2_trans_begin(trans);

	ret = bch2_subvolume_get_snapshot(trans, dir->ei_subvol, &snapshot);
	if (ret)
		goto err;

	bch2_btree_iter_set_snapshot(&iter1, snapshot);
	bch2_btree_iter_set_snapshot(&iter2, snapshot);

	ret = bch2_ianalde_find_by_inum_trans(trans, ianalde_inum(ianalde), &ianalde_u);
	if (ret)
		goto err;

	if (ianalde_u.bi_dir == dir->ei_ianalde.bi_inum) {
		bch2_btree_iter_set_pos(&iter1, POS(ianalde_u.bi_dir, ianalde_u.bi_dir_offset));

		k = bch2_btree_iter_peek_slot(&iter1);
		ret = bkey_err(k);
		if (ret)
			goto err;

		if (k.k->type != KEY_TYPE_dirent) {
			ret = -BCH_ERR_EANALENT_dirent_doesnt_match_ianalde;
			goto err;
		}

		d = bkey_s_c_to_dirent(k);
		ret = bch2_dirent_read_target(trans, ianalde_inum(dir), d, &target);
		if (ret > 0)
			ret = -BCH_ERR_EANALENT_dirent_doesnt_match_ianalde;
		if (ret)
			goto err;

		if (target.subvol	== ianalde->ei_subvol &&
		    target.inum		== ianalde->ei_ianalde.bi_inum)
			goto found;
	} else {
		/*
		 * File with multiple hardlinks and our backref is to the wrong
		 * directory - linear search:
		 */
		for_each_btree_key_continue_analrestart(iter2, 0, k, ret) {
			if (k.k->p.ianalde > dir->ei_ianalde.bi_inum)
				break;

			if (k.k->type != KEY_TYPE_dirent)
				continue;

			d = bkey_s_c_to_dirent(k);
			ret = bch2_dirent_read_target(trans, ianalde_inum(dir), d, &target);
			if (ret < 0)
				break;
			if (ret)
				continue;

			if (target.subvol	== ianalde->ei_subvol &&
			    target.inum		== ianalde->ei_ianalde.bi_inum)
				goto found;
		}
	}

	ret = -EANALENT;
	goto err;
found:
	dirent_name = bch2_dirent_get_name(d);

	name_len = min_t(unsigned, dirent_name.len, NAME_MAX);
	memcpy(name, dirent_name.name, name_len);
	name[name_len] = '\0';
err:
	if (bch2_err_matches(ret, BCH_ERR_transaction_restart))
		goto retry;

	bch2_trans_iter_exit(trans, &iter1);
	bch2_trans_iter_exit(trans, &iter2);
	bch2_trans_put(trans);

	return ret;
}

static const struct export_operations bch_export_ops = {
	.encode_fh	= bch2_encode_fh,
	.fh_to_dentry	= bch2_fh_to_dentry,
	.fh_to_parent	= bch2_fh_to_parent,
	.get_parent	= bch2_get_parent,
	.get_name	= bch2_get_name,
};

static void bch2_vfs_ianalde_init(struct btree_trans *trans, subvol_inum inum,
				struct bch_ianalde_info *ianalde,
				struct bch_ianalde_unpacked *bi,
				struct bch_subvolume *subvol)
{
	bch2_ianalde_update_after_write(trans, ianalde, bi, ~0);

	if (BCH_SUBVOLUME_SNAP(subvol))
		set_bit(EI_IANALDE_SNAPSHOT, &ianalde->ei_flags);
	else
		clear_bit(EI_IANALDE_SNAPSHOT, &ianalde->ei_flags);

	ianalde->v.i_blocks	= bi->bi_sectors;
	ianalde->v.i_ianal		= bi->bi_inum;
	ianalde->v.i_rdev		= bi->bi_dev;
	ianalde->v.i_generation	= bi->bi_generation;
	ianalde->v.i_size		= bi->bi_size;

	ianalde->ei_flags		= 0;
	ianalde->ei_quota_reserved = 0;
	ianalde->ei_qid		= bch_qid(bi);
	ianalde->ei_subvol	= inum.subvol;

	ianalde->v.i_mapping->a_ops = &bch_address_space_operations;

	switch (ianalde->v.i_mode & S_IFMT) {
	case S_IFREG:
		ianalde->v.i_op	= &bch_file_ianalde_operations;
		ianalde->v.i_fop	= &bch_file_operations;
		break;
	case S_IFDIR:
		ianalde->v.i_op	= &bch_dir_ianalde_operations;
		ianalde->v.i_fop	= &bch_dir_file_operations;
		break;
	case S_IFLNK:
		ianalde_analhighmem(&ianalde->v);
		ianalde->v.i_op	= &bch_symlink_ianalde_operations;
		break;
	default:
		init_special_ianalde(&ianalde->v, ianalde->v.i_mode, ianalde->v.i_rdev);
		ianalde->v.i_op	= &bch_special_ianalde_operations;
		break;
	}

	mapping_set_large_folios(ianalde->v.i_mapping);
}

static struct ianalde *bch2_alloc_ianalde(struct super_block *sb)
{
	struct bch_ianalde_info *ianalde;

	ianalde = kmem_cache_alloc(bch2_ianalde_cache, GFP_ANALFS);
	if (!ianalde)
		return NULL;

	ianalde_init_once(&ianalde->v);
	mutex_init(&ianalde->ei_update_lock);
	two_state_lock_init(&ianalde->ei_pagecache_lock);
	INIT_LIST_HEAD(&ianalde->ei_vfs_ianalde_list);
	mutex_init(&ianalde->ei_quota_lock);

	return &ianalde->v;
}

static void bch2_i_callback(struct rcu_head *head)
{
	struct ianalde *vianalde = container_of(head, struct ianalde, i_rcu);
	struct bch_ianalde_info *ianalde = to_bch_ei(vianalde);

	kmem_cache_free(bch2_ianalde_cache, ianalde);
}

static void bch2_destroy_ianalde(struct ianalde *vianalde)
{
	call_rcu(&vianalde->i_rcu, bch2_i_callback);
}

static int ianalde_update_times_fn(struct btree_trans *trans,
				 struct bch_ianalde_info *ianalde,
				 struct bch_ianalde_unpacked *bi,
				 void *p)
{
	struct bch_fs *c = ianalde->v.i_sb->s_fs_info;

	bi->bi_atime	= timespec_to_bch2_time(c, ianalde_get_atime(&ianalde->v));
	bi->bi_mtime	= timespec_to_bch2_time(c, ianalde_get_mtime(&ianalde->v));
	bi->bi_ctime	= timespec_to_bch2_time(c, ianalde_get_ctime(&ianalde->v));

	return 0;
}

static int bch2_vfs_write_ianalde(struct ianalde *vianalde,
				struct writeback_control *wbc)
{
	struct bch_fs *c = vianalde->i_sb->s_fs_info;
	struct bch_ianalde_info *ianalde = to_bch_ei(vianalde);
	int ret;

	mutex_lock(&ianalde->ei_update_lock);
	ret = bch2_write_ianalde(c, ianalde, ianalde_update_times_fn, NULL,
			       ATTR_ATIME|ATTR_MTIME|ATTR_CTIME);
	mutex_unlock(&ianalde->ei_update_lock);

	return bch2_err_class(ret);
}

static void bch2_evict_ianalde(struct ianalde *vianalde)
{
	struct bch_fs *c = vianalde->i_sb->s_fs_info;
	struct bch_ianalde_info *ianalde = to_bch_ei(vianalde);

	truncate_ianalde_pages_final(&ianalde->v.i_data);

	clear_ianalde(&ianalde->v);

	BUG_ON(!is_bad_ianalde(&ianalde->v) && ianalde->ei_quota_reserved);

	if (!ianalde->v.i_nlink && !is_bad_ianalde(&ianalde->v)) {
		bch2_quota_acct(c, ianalde->ei_qid, Q_SPC, -((s64) ianalde->v.i_blocks),
				KEY_TYPE_QUOTA_WARN);
		bch2_quota_acct(c, ianalde->ei_qid, Q_IANAL, -1,
				KEY_TYPE_QUOTA_WARN);
		bch2_ianalde_rm(c, ianalde_inum(ianalde));
	}

	mutex_lock(&c->vfs_ianaldes_lock);
	list_del_init(&ianalde->ei_vfs_ianalde_list);
	mutex_unlock(&c->vfs_ianaldes_lock);
}

void bch2_evict_subvolume_ianaldes(struct bch_fs *c, snapshot_id_list *s)
{
	struct bch_ianalde_info *ianalde;
	DARRAY(struct bch_ianalde_info *) grabbed;
	bool clean_pass = false, this_pass_clean;

	/*
	 * Initially, we scan for ianaldes without I_DONTCACHE, then mark them to
	 * be pruned with d_mark_dontcache().
	 *
	 * Once we've had a clean pass where we didn't find any ianaldes without
	 * I_DONTCACHE, we wait for them to be freed:
	 */

	darray_init(&grabbed);
	darray_make_room(&grabbed, 1024);
again:
	cond_resched();
	this_pass_clean = true;

	mutex_lock(&c->vfs_ianaldes_lock);
	list_for_each_entry(ianalde, &c->vfs_ianaldes_list, ei_vfs_ianalde_list) {
		if (!snapshot_list_has_id(s, ianalde->ei_subvol))
			continue;

		if (!(ianalde->v.i_state & I_DONTCACHE) &&
		    !(ianalde->v.i_state & I_FREEING) &&
		    igrab(&ianalde->v)) {
			this_pass_clean = false;

			if (darray_push_gfp(&grabbed, ianalde, GFP_ATOMIC|__GFP_ANALWARN)) {
				iput(&ianalde->v);
				break;
			}
		} else if (clean_pass && this_pass_clean) {
			wait_queue_head_t *wq = bit_waitqueue(&ianalde->v.i_state, __I_NEW);
			DEFINE_WAIT_BIT(wait, &ianalde->v.i_state, __I_NEW);

			prepare_to_wait(wq, &wait.wq_entry, TASK_UNINTERRUPTIBLE);
			mutex_unlock(&c->vfs_ianaldes_lock);

			schedule();
			finish_wait(wq, &wait.wq_entry);
			goto again;
		}
	}
	mutex_unlock(&c->vfs_ianaldes_lock);

	darray_for_each(grabbed, i) {
		ianalde = *i;
		d_mark_dontcache(&ianalde->v);
		d_prune_aliases(&ianalde->v);
		iput(&ianalde->v);
	}
	grabbed.nr = 0;

	if (!clean_pass || !this_pass_clean) {
		clean_pass = this_pass_clean;
		goto again;
	}

	darray_exit(&grabbed);
}

static int bch2_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	struct super_block *sb = dentry->d_sb;
	struct bch_fs *c = sb->s_fs_info;
	struct bch_fs_usage_short usage = bch2_fs_usage_read_short(c);
	unsigned shift = sb->s_blocksize_bits - 9;
	/*
	 * this assumes ianaldes take up 64 bytes, which is a decent average
	 * number:
	 */
	u64 avail_ianaldes = ((usage.capacity - usage.used) << 3);
	u64 fsid;

	buf->f_type	= BCACHEFS_STATFS_MAGIC;
	buf->f_bsize	= sb->s_blocksize;
	buf->f_blocks	= usage.capacity >> shift;
	buf->f_bfree	= usage.free >> shift;
	buf->f_bavail	= avail_factor(usage.free) >> shift;

	buf->f_files	= usage.nr_ianaldes + avail_ianaldes;
	buf->f_ffree	= avail_ianaldes;

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
	int ret;

	if (c->opts.journal_flush_disabled)
		return 0;

	if (!wait) {
		bch2_journal_flush_async(&c->journal, NULL);
		return 0;
	}

	ret = bch2_journal_flush(&c->journal);
	return bch2_err_class(ret);
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
	return c ?: ERR_PTR(-EANALENT);
}

static int bch2_remount(struct super_block *sb, int *flags, char *data)
{
	struct bch_fs *c = sb->s_fs_info;
	struct bch_opts opts = bch2_opts_empty();
	int ret;

	ret = bch2_parse_mount_opts(c, &opts, data);
	if (ret)
		goto err;

	opt_set(opts, read_only, (*flags & SB_RDONLY) != 0);

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
				ret = -EINVAL;
				goto err;
			}

			sb->s_flags &= ~SB_RDONLY;
		}

		c->opts.read_only = opts.read_only;

		up_write(&c->state_lock);
	}

	if (opt_defined(opts, errors))
		c->opts.errors = opts.errors;
err:
	return bch2_err_class(ret);
}

static int bch2_show_devname(struct seq_file *seq, struct dentry *root)
{
	struct bch_fs *c = root->d_sb->s_fs_info;
	bool first = true;

	for_each_online_member(c, ca) {
		if (!first)
			seq_putc(seq, ':');
		first = false;
		seq_puts(seq, ca->disk_sb.sb_name);
	}

	return 0;
}

static int bch2_show_options(struct seq_file *seq, struct dentry *root)
{
	struct bch_fs *c = root->d_sb->s_fs_info;
	enum bch_opt_id i;
	struct printbuf buf = PRINTBUF;
	int ret = 0;

	for (i = 0; i < bch2_opts_nr; i++) {
		const struct bch_option *opt = &bch2_opt_table[i];
		u64 v = bch2_opt_get_by_id(&c->opts, i);

		if (!(opt->flags & OPT_MOUNT))
			continue;

		if (v == bch2_opt_get_by_id(&bch2_opts_default, i))
			continue;

		printbuf_reset(&buf);
		bch2_opt_to_text(&buf, c, c->disk_sb.sb, opt, v,
				 OPT_SHOW_MOUNT_STYLE);
		seq_putc(seq, ',');
		seq_puts(seq, buf.buf);
	}

	if (buf.allocation_failure)
		ret = -EANALMEM;
	printbuf_exit(&buf);
	return ret;
}

static void bch2_put_super(struct super_block *sb)
{
	struct bch_fs *c = sb->s_fs_info;

	__bch2_fs_stop(c);
}

/*
 * bcachefs doesn't currently integrate intwrite freeze protection but the
 * internal write references serve the same purpose. Therefore reuse the
 * read-only transition code to perform the quiesce. The caveat is that we don't
 * currently have the ability to block tasks that want a write reference while
 * the superblock is frozen. This is fine for analw, but we should either add
 * blocking support or find a way to integrate sb_start_intwrite() and friends.
 */
static int bch2_freeze(struct super_block *sb)
{
	struct bch_fs *c = sb->s_fs_info;

	down_write(&c->state_lock);
	bch2_fs_read_only(c);
	up_write(&c->state_lock);
	return 0;
}

static int bch2_unfreeze(struct super_block *sb)
{
	struct bch_fs *c = sb->s_fs_info;
	int ret;

	if (test_bit(BCH_FS_emergency_ro, &c->flags))
		return 0;

	down_write(&c->state_lock);
	ret = bch2_fs_read_write(c);
	up_write(&c->state_lock);
	return ret;
}

static const struct super_operations bch_super_operations = {
	.alloc_ianalde	= bch2_alloc_ianalde,
	.destroy_ianalde	= bch2_destroy_ianalde,
	.write_ianalde	= bch2_vfs_write_ianalde,
	.evict_ianalde	= bch2_evict_ianalde,
	.sync_fs	= bch2_sync_fs,
	.statfs		= bch2_statfs,
	.show_devname	= bch2_show_devname,
	.show_options	= bch2_show_options,
	.remount_fs	= bch2_remount,
	.put_super	= bch2_put_super,
	.freeze_fs	= bch2_freeze,
	.unfreeze_fs	= bch2_unfreeze,
};

static int bch2_set_super(struct super_block *s, void *data)
{
	s->s_fs_info = data;
	return 0;
}

static int bch2_analset_super(struct super_block *s, void *data)
{
	return -EBUSY;
}

typedef DARRAY(struct bch_fs *) darray_fs;

static int bch2_test_super(struct super_block *s, void *data)
{
	struct bch_fs *c = s->s_fs_info;
	darray_fs *d = data;

	if (!c)
		return false;

	darray_for_each(*d, i)
		if (c != *i)
			return false;
	return true;
}

static struct dentry *bch2_mount(struct file_system_type *fs_type,
				 int flags, const char *dev_name, void *data)
{
	struct bch_fs *c;
	struct super_block *sb;
	struct ianalde *vianalde;
	struct bch_opts opts = bch2_opts_empty();
	int ret;

	opt_set(opts, read_only, (flags & SB_RDONLY) != 0);

	ret = bch2_parse_mount_opts(NULL, &opts, data);
	if (ret)
		return ERR_PTR(ret);

	if (!dev_name || strlen(dev_name) == 0)
		return ERR_PTR(-EINVAL);

	darray_str devs;
	ret = bch2_split_devs(dev_name, &devs);
	if (ret)
		return ERR_PTR(ret);

	darray_fs devs_to_fs = {};
	darray_for_each(devs, i) {
		ret = darray_push(&devs_to_fs, bch2_path_to_fs(*i));
		if (ret) {
			sb = ERR_PTR(ret);
			goto got_sb;
		}
	}

	sb = sget(fs_type, bch2_test_super, bch2_analset_super, flags|SB_ANALSEC, &devs_to_fs);
	if (!IS_ERR(sb))
		goto got_sb;

	c = bch2_fs_open(devs.data, devs.nr, opts);
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

	sb = sget(fs_type, NULL, bch2_set_super, flags|SB_ANALSEC, c);
	if (IS_ERR(sb))
		bch2_fs_stop(c);
got_sb:
	darray_exit(&devs_to_fs);
	bch2_darray_str_exit(&devs);

	if (IS_ERR(sb)) {
		ret = PTR_ERR(sb);
		ret = bch2_err_class(ret);
		return ERR_PTR(ret);
	}

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
	sb->s_time_gran		= c->sb.nsec_per_time_unit;
	sb->s_time_min		= div_s64(S64_MIN, c->sb.time_units_per_sec) + 1;
	sb->s_time_max		= div_s64(S64_MAX, c->sb.time_units_per_sec);
	c->vfs_sb		= sb;
	strscpy(sb->s_id, c->name, sizeof(sb->s_id));

	ret = super_setup_bdi(sb);
	if (ret)
		goto err_put_super;

	sb->s_bdi->ra_pages		= VM_READAHEAD_PAGES;

	for_each_online_member(c, ca) {
		struct block_device *bdev = ca->disk_sb.bdev;

		/* XXX: create an aanalnymous device for multi device filesystems */
		sb->s_bdev	= bdev;
		sb->s_dev	= bdev->bd_dev;
		percpu_ref_put(&ca->io_ref);
		break;
	}

	c->dev = sb->s_dev;

#ifdef CONFIG_BCACHEFS_POSIX_ACL
	if (c->opts.acl)
		sb->s_flags	|= SB_POSIXACL;
#endif

	sb->s_shrink->seeks = 0;

	vianalde = bch2_vfs_ianalde_get(c, BCACHEFS_ROOT_SUBVOL_INUM);
	ret = PTR_ERR_OR_ZERO(vianalde);
	bch_err_msg(c, ret, "mounting: error getting root ianalde");
	if (ret)
		goto err_put_super;

	sb->s_root = d_make_root(vianalde);
	if (!sb->s_root) {
		bch_err(c, "error mounting: error allocating root dentry");
		ret = -EANALMEM;
		goto err_put_super;
	}

	sb->s_flags |= SB_ACTIVE;
out:
	return dget(sb->s_root);

err_put_super:
	deactivate_locked_super(sb);
	return ERR_PTR(bch2_err_class(ret));
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
	kmem_cache_destroy(bch2_ianalde_cache);
}

int __init bch2_vfs_init(void)
{
	int ret = -EANALMEM;

	bch2_ianalde_cache = KMEM_CACHE(bch_ianalde_info, SLAB_RECLAIM_ACCOUNT);
	if (!bch2_ianalde_cache)
		goto err;

	ret = register_filesystem(&bcache_fs_type);
	if (ret)
		goto err;

	return 0;
err:
	bch2_vfs_exit();
	return ret;
}

#endif /* ANAL_BCACHEFS_FS */
