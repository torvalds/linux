// SPDX-License-Identifier: GPL-2.0

#include "bcachefs.h"
#include "acl.h"
#include "btree_update.h"
#include "dirent.h"
#include "fs-common.h"
#include "inode.h"
#include "xattr.h"

#include <linux/posix_acl.h>

int bch2_create_trans(struct btree_trans *trans, u64 dir_inum,
		      struct bch_inode_unpacked *dir_u,
		      struct bch_inode_unpacked *new_inode,
		      const struct qstr *name,
		      uid_t uid, gid_t gid, umode_t mode, dev_t rdev,
		      struct posix_acl *default_acl,
		      struct posix_acl *acl)
{
	struct bch_fs *c = trans->c;
	struct btree_iter *dir_iter;
	struct bch_hash_info hash = bch2_hash_info_init(c, new_inode);
	u64 now = bch2_current_time(trans->c);
	int ret;

	dir_iter = bch2_inode_peek(trans, dir_u, dir_inum,
				   name ? BTREE_ITER_INTENT : 0);
	if (IS_ERR(dir_iter))
		return PTR_ERR(dir_iter);

	bch2_inode_init_late(new_inode, now, uid, gid, mode, rdev, dir_u);

	if (!name)
		new_inode->bi_flags |= BCH_INODE_UNLINKED;

	ret = bch2_inode_create(trans, new_inode,
				BLOCKDEV_INODE_MAX, 0,
				&c->unused_inode_hint);
	if (ret)
		return ret;

	if (default_acl) {
		ret = bch2_set_acl_trans(trans, new_inode, &hash,
					 default_acl, ACL_TYPE_DEFAULT);
		if (ret)
			return ret;
	}

	if (acl) {
		ret = bch2_set_acl_trans(trans, new_inode, &hash,
					 acl, ACL_TYPE_ACCESS);
		if (ret)
			return ret;
	}

	if (name) {
		struct bch_hash_info dir_hash = bch2_hash_info_init(c, dir_u);
		dir_u->bi_mtime = dir_u->bi_ctime = now;

		if (S_ISDIR(new_inode->bi_mode))
			dir_u->bi_nlink++;

		ret = bch2_inode_write(trans, dir_iter, dir_u);
		if (ret)
			return ret;

		ret = bch2_dirent_create(trans, dir_inum, &dir_hash,
					 mode_to_type(new_inode->bi_mode),
					 name, new_inode->bi_inum,
					 BCH_HASH_SET_MUST_CREATE);
		if (ret)
			return ret;
	}

	return 0;
}

int bch2_link_trans(struct btree_trans *trans,
		    u64 dir_inum,
		    u64 inum, struct bch_inode_unpacked *inode_u,
		    const struct qstr *name)
{
	struct btree_iter *dir_iter, *inode_iter;
	struct bch_inode_unpacked dir_u;
	struct bch_hash_info dir_hash;
	u64 now = bch2_current_time(trans->c);

	dir_iter = bch2_inode_peek(trans, &dir_u, dir_inum, 0);
	if (IS_ERR(dir_iter))
		return PTR_ERR(dir_iter);

	inode_iter = bch2_inode_peek(trans, inode_u, inum, BTREE_ITER_INTENT);
	if (IS_ERR(inode_iter))
		return PTR_ERR(inode_iter);

	dir_hash = bch2_hash_info_init(trans->c, &dir_u);

	inode_u->bi_ctime = now;
	bch2_inode_nlink_inc(inode_u);

	return bch2_dirent_create(trans, dir_inum, &dir_hash,
				  mode_to_type(inode_u->bi_mode),
				  name, inum, BCH_HASH_SET_MUST_CREATE) ?:
		bch2_inode_write(trans, inode_iter, inode_u);
}

int bch2_unlink_trans(struct btree_trans *trans,
		      u64 dir_inum, struct bch_inode_unpacked *dir_u,
		      struct bch_inode_unpacked *inode_u,
		      const struct qstr *name)
{
	struct btree_iter *dir_iter, *dirent_iter, *inode_iter;
	struct bch_hash_info dir_hash;
	u64 inum, now = bch2_current_time(trans->c);
	struct bkey_s_c k;

	dir_iter = bch2_inode_peek(trans, dir_u, dir_inum, BTREE_ITER_INTENT);
	if (IS_ERR(dir_iter))
		return PTR_ERR(dir_iter);

	dir_hash = bch2_hash_info_init(trans->c, dir_u);

	dirent_iter = __bch2_dirent_lookup_trans(trans, dir_inum,
						 &dir_hash, name);
	if (IS_ERR(dirent_iter))
		return PTR_ERR(dirent_iter);

	k = bch2_btree_iter_peek_slot(dirent_iter);
	inum = le64_to_cpu(bkey_s_c_to_dirent(k).v->d_inum);

	inode_iter = bch2_inode_peek(trans, inode_u, inum, BTREE_ITER_INTENT);
	if (IS_ERR(inode_iter))
		return PTR_ERR(inode_iter);

	dir_u->bi_mtime = dir_u->bi_ctime = inode_u->bi_ctime = now;
	dir_u->bi_nlink -= S_ISDIR(inode_u->bi_mode);
	bch2_inode_nlink_dec(inode_u);

	return  (S_ISDIR(inode_u->bi_mode)
		 ? bch2_empty_dir_trans(trans, inum)
		 : 0) ?:
		bch2_dirent_delete_at(trans, &dir_hash, dirent_iter) ?:
		bch2_inode_write(trans, dir_iter, dir_u) ?:
		bch2_inode_write(trans, inode_iter, inode_u);
}

bool bch2_reinherit_attrs(struct bch_inode_unpacked *dst_u,
			  struct bch_inode_unpacked *src_u)
{
	u64 src, dst;
	unsigned id;
	bool ret = false;

	for (id = 0; id < Inode_opt_nr; id++) {
		if (dst_u->bi_fields_set & (1 << id))
			continue;

		src = bch2_inode_opt_get(src_u, id);
		dst = bch2_inode_opt_get(dst_u, id);

		if (src == dst)
			continue;

		bch2_inode_opt_set(dst_u, id, src);
		ret = true;
	}

	return ret;
}

int bch2_rename_trans(struct btree_trans *trans,
		      u64 src_dir, struct bch_inode_unpacked *src_dir_u,
		      u64 dst_dir, struct bch_inode_unpacked *dst_dir_u,
		      struct bch_inode_unpacked *src_inode_u,
		      struct bch_inode_unpacked *dst_inode_u,
		      const struct qstr *src_name,
		      const struct qstr *dst_name,
		      enum bch_rename_mode mode)
{
	struct btree_iter *src_dir_iter, *dst_dir_iter = NULL;
	struct btree_iter *src_inode_iter, *dst_inode_iter = NULL;
	struct bch_hash_info src_hash, dst_hash;
	u64 src_inode, dst_inode, now = bch2_current_time(trans->c);
	int ret;

	src_dir_iter = bch2_inode_peek(trans, src_dir_u, src_dir,
				       BTREE_ITER_INTENT);
	if (IS_ERR(src_dir_iter))
		return PTR_ERR(src_dir_iter);

	src_hash = bch2_hash_info_init(trans->c, src_dir_u);

	if (dst_dir != src_dir) {
		dst_dir_iter = bch2_inode_peek(trans, dst_dir_u, dst_dir,
					       BTREE_ITER_INTENT);
		if (IS_ERR(dst_dir_iter))
			return PTR_ERR(dst_dir_iter);

		dst_hash = bch2_hash_info_init(trans->c, dst_dir_u);
	} else {
		dst_dir_u = src_dir_u;
		dst_hash = src_hash;
	}

	ret = bch2_dirent_rename(trans,
				 src_dir, &src_hash,
				 dst_dir, &dst_hash,
				 src_name, &src_inode,
				 dst_name, &dst_inode,
				 mode);
	if (ret)
		return ret;

	src_inode_iter = bch2_inode_peek(trans, src_inode_u, src_inode,
					 BTREE_ITER_INTENT);
	if (IS_ERR(src_inode_iter))
		return PTR_ERR(src_inode_iter);

	if (dst_inode) {
		dst_inode_iter = bch2_inode_peek(trans, dst_inode_u, dst_inode,
						 BTREE_ITER_INTENT);
		if (IS_ERR(dst_inode_iter))
			return PTR_ERR(dst_inode_iter);
	}

	if (mode == BCH_RENAME_OVERWRITE) {
		if (S_ISDIR(src_inode_u->bi_mode) !=
		    S_ISDIR(dst_inode_u->bi_mode))
			return -ENOTDIR;

		if (S_ISDIR(dst_inode_u->bi_mode) &&
		    bch2_empty_dir_trans(trans, dst_inode))
			return -ENOTEMPTY;
	}

	if (bch2_reinherit_attrs(src_inode_u, dst_dir_u) &&
	    S_ISDIR(src_inode_u->bi_mode))
		return -EXDEV;

	if (mode == BCH_RENAME_EXCHANGE &&
	    bch2_reinherit_attrs(dst_inode_u, src_dir_u) &&
	    S_ISDIR(dst_inode_u->bi_mode))
		return -EXDEV;

	if (S_ISDIR(src_inode_u->bi_mode)) {
		src_dir_u->bi_nlink--;
		dst_dir_u->bi_nlink++;
	}

	if (dst_inode && S_ISDIR(dst_inode_u->bi_mode)) {
		dst_dir_u->bi_nlink--;
		src_dir_u->bi_nlink += mode == BCH_RENAME_EXCHANGE;
	}

	if (mode == BCH_RENAME_OVERWRITE)
		bch2_inode_nlink_dec(dst_inode_u);

	src_dir_u->bi_mtime		= now;
	src_dir_u->bi_ctime		= now;

	if (src_dir != dst_dir) {
		dst_dir_u->bi_mtime	= now;
		dst_dir_u->bi_ctime	= now;
	}

	src_inode_u->bi_ctime		= now;

	if (dst_inode)
		dst_inode_u->bi_ctime	= now;

	return  bch2_inode_write(trans, src_dir_iter, src_dir_u) ?:
		(src_dir != dst_dir
		 ? bch2_inode_write(trans, dst_dir_iter, dst_dir_u)
		 : 0 ) ?:
		bch2_inode_write(trans, src_inode_iter, src_inode_u) ?:
		(dst_inode
		 ? bch2_inode_write(trans, dst_inode_iter, dst_inode_u)
		 : 0 );
}
