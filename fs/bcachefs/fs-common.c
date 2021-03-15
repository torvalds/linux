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
	struct btree_iter *dir_iter = NULL;
	struct btree_iter *inode_iter = NULL;
	struct bch_hash_info hash = bch2_hash_info_init(c, new_inode);
	u64 now = bch2_current_time(c);
	u64 dir_offset = 0;
	int ret;

	dir_iter = bch2_inode_peek(trans, dir_u, dir_inum, BTREE_ITER_INTENT);
	ret = PTR_ERR_OR_ZERO(dir_iter);
	if (ret)
		goto err;

	bch2_inode_init_late(new_inode, now, uid, gid, mode, rdev, dir_u);

	if (!name)
		new_inode->bi_flags |= BCH_INODE_UNLINKED;

	inode_iter = bch2_inode_create(trans, new_inode, U32_MAX);
	ret = PTR_ERR_OR_ZERO(inode_iter);
	if (ret)
		goto err;

	if (default_acl) {
		ret = bch2_set_acl_trans(trans, new_inode, &hash,
					 default_acl, ACL_TYPE_DEFAULT);
		if (ret)
			goto err;
	}

	if (acl) {
		ret = bch2_set_acl_trans(trans, new_inode, &hash,
					 acl, ACL_TYPE_ACCESS);
		if (ret)
			goto err;
	}

	if (name) {
		struct bch_hash_info dir_hash = bch2_hash_info_init(c, dir_u);
		dir_u->bi_mtime = dir_u->bi_ctime = now;

		if (S_ISDIR(new_inode->bi_mode))
			dir_u->bi_nlink++;

		ret = bch2_inode_write(trans, dir_iter, dir_u);
		if (ret)
			goto err;

		ret = bch2_dirent_create(trans, dir_inum, &dir_hash,
					 mode_to_type(new_inode->bi_mode),
					 name, new_inode->bi_inum,
					 &dir_offset,
					 BCH_HASH_SET_MUST_CREATE);
		if (ret)
			goto err;
	}

	if (c->sb.version >= bcachefs_metadata_version_inode_backpointers) {
		new_inode->bi_dir		= dir_u->bi_inum;
		new_inode->bi_dir_offset	= dir_offset;
	}

	/* XXX use bch2_btree_iter_set_snapshot() */
	inode_iter->snapshot = U32_MAX;
	bch2_btree_iter_set_pos(inode_iter, SPOS(0, new_inode->bi_inum, U32_MAX));

	ret = bch2_inode_write(trans, inode_iter, new_inode);
err:
	bch2_trans_iter_put(trans, inode_iter);
	bch2_trans_iter_put(trans, dir_iter);
	return ret;
}

int bch2_link_trans(struct btree_trans *trans, u64 dir_inum,
		    u64 inum, struct bch_inode_unpacked *dir_u,
		    struct bch_inode_unpacked *inode_u, const struct qstr *name)
{
	struct bch_fs *c = trans->c;
	struct btree_iter *dir_iter = NULL, *inode_iter = NULL;
	struct bch_hash_info dir_hash;
	u64 now = bch2_current_time(c);
	u64 dir_offset = 0;
	int ret;

	inode_iter = bch2_inode_peek(trans, inode_u, inum, BTREE_ITER_INTENT);
	ret = PTR_ERR_OR_ZERO(inode_iter);
	if (ret)
		goto err;

	inode_u->bi_ctime = now;
	bch2_inode_nlink_inc(inode_u);

	inode_u->bi_flags |= BCH_INODE_BACKPTR_UNTRUSTED;

	dir_iter = bch2_inode_peek(trans, dir_u, dir_inum, 0);
	ret = PTR_ERR_OR_ZERO(dir_iter);
	if (ret)
		goto err;

	dir_u->bi_mtime = dir_u->bi_ctime = now;

	dir_hash = bch2_hash_info_init(c, dir_u);

	ret = bch2_dirent_create(trans, dir_inum, &dir_hash,
				 mode_to_type(inode_u->bi_mode),
				 name, inum, &dir_offset,
				 BCH_HASH_SET_MUST_CREATE);
	if (ret)
		goto err;

	if (c->sb.version >= bcachefs_metadata_version_inode_backpointers) {
		inode_u->bi_dir		= dir_inum;
		inode_u->bi_dir_offset	= dir_offset;
	}

	ret =   bch2_inode_write(trans, dir_iter, dir_u) ?:
		bch2_inode_write(trans, inode_iter, inode_u);
err:
	bch2_trans_iter_put(trans, dir_iter);
	bch2_trans_iter_put(trans, inode_iter);
	return ret;
}

int bch2_unlink_trans(struct btree_trans *trans,
		      u64 dir_inum, struct bch_inode_unpacked *dir_u,
		      struct bch_inode_unpacked *inode_u,
		      const struct qstr *name)
{
	struct bch_fs *c = trans->c;
	struct btree_iter *dir_iter = NULL, *dirent_iter = NULL,
			  *inode_iter = NULL;
	struct bch_hash_info dir_hash;
	u64 inum, now = bch2_current_time(c);
	struct bkey_s_c k;
	int ret;

	dir_iter = bch2_inode_peek(trans, dir_u, dir_inum, BTREE_ITER_INTENT);
	ret = PTR_ERR_OR_ZERO(dir_iter);
	if (ret)
		goto err;

	dir_hash = bch2_hash_info_init(c, dir_u);

	dirent_iter = __bch2_dirent_lookup_trans(trans, dir_inum, &dir_hash,
						 name, BTREE_ITER_INTENT);
	ret = PTR_ERR_OR_ZERO(dirent_iter);
	if (ret)
		goto err;

	k = bch2_btree_iter_peek_slot(dirent_iter);
	inum = le64_to_cpu(bkey_s_c_to_dirent(k).v->d_inum);

	inode_iter = bch2_inode_peek(trans, inode_u, inum, BTREE_ITER_INTENT);
	ret = PTR_ERR_OR_ZERO(inode_iter);
	if (ret)
		goto err;

	dir_u->bi_mtime = dir_u->bi_ctime = inode_u->bi_ctime = now;
	dir_u->bi_nlink -= S_ISDIR(inode_u->bi_mode);
	bch2_inode_nlink_dec(inode_u);

	ret =   (S_ISDIR(inode_u->bi_mode)
		 ? bch2_empty_dir_trans(trans, inum)
		 : 0) ?:
		bch2_dirent_delete_at(trans, &dir_hash, dirent_iter) ?:
		bch2_inode_write(trans, dir_iter, dir_u) ?:
		bch2_inode_write(trans, inode_iter, inode_u);
err:
	bch2_trans_iter_put(trans, inode_iter);
	bch2_trans_iter_put(trans, dirent_iter);
	bch2_trans_iter_put(trans, dir_iter);
	return ret;
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
	struct bch_fs *c = trans->c;
	struct btree_iter *src_dir_iter = NULL, *dst_dir_iter = NULL;
	struct btree_iter *src_inode_iter = NULL, *dst_inode_iter = NULL;
	struct bch_hash_info src_hash, dst_hash;
	u64 src_inode, src_offset, dst_inode, dst_offset;
	u64 now = bch2_current_time(c);
	int ret;

	src_dir_iter = bch2_inode_peek(trans, src_dir_u, src_dir,
				       BTREE_ITER_INTENT);
	ret = PTR_ERR_OR_ZERO(src_dir_iter);
	if (ret)
		goto err;

	src_hash = bch2_hash_info_init(c, src_dir_u);

	if (dst_dir != src_dir) {
		dst_dir_iter = bch2_inode_peek(trans, dst_dir_u, dst_dir,
					       BTREE_ITER_INTENT);
		ret = PTR_ERR_OR_ZERO(dst_dir_iter);
		if (ret)
			goto err;

		dst_hash = bch2_hash_info_init(c, dst_dir_u);
	} else {
		dst_dir_u = src_dir_u;
		dst_hash = src_hash;
	}

	ret = bch2_dirent_rename(trans,
				 src_dir, &src_hash,
				 dst_dir, &dst_hash,
				 src_name, &src_inode, &src_offset,
				 dst_name, &dst_inode, &dst_offset,
				 mode);
	if (ret)
		goto err;

	src_inode_iter = bch2_inode_peek(trans, src_inode_u, src_inode,
					 BTREE_ITER_INTENT);
	ret = PTR_ERR_OR_ZERO(src_inode_iter);
	if (ret)
		goto err;

	if (dst_inode) {
		dst_inode_iter = bch2_inode_peek(trans, dst_inode_u, dst_inode,
						 BTREE_ITER_INTENT);
		ret = PTR_ERR_OR_ZERO(dst_inode_iter);
		if (ret)
			goto err;
	}

	if (c->sb.version >= bcachefs_metadata_version_inode_backpointers) {
		src_inode_u->bi_dir		= dst_dir_u->bi_inum;
		src_inode_u->bi_dir_offset	= dst_offset;

		if (mode == BCH_RENAME_EXCHANGE) {
			dst_inode_u->bi_dir		= src_dir_u->bi_inum;
			dst_inode_u->bi_dir_offset	= src_offset;
		}
	}

	if (mode == BCH_RENAME_OVERWRITE) {
		if (S_ISDIR(src_inode_u->bi_mode) !=
		    S_ISDIR(dst_inode_u->bi_mode)) {
			ret = -ENOTDIR;
			goto err;
		}

		if (S_ISDIR(dst_inode_u->bi_mode) &&
		    bch2_empty_dir_trans(trans, dst_inode)) {
			ret = -ENOTEMPTY;
			goto err;
		}
	}

	if (bch2_reinherit_attrs(src_inode_u, dst_dir_u) &&
	    S_ISDIR(src_inode_u->bi_mode)) {
		ret = -EXDEV;
		goto err;
	}

	if (mode == BCH_RENAME_EXCHANGE &&
	    bch2_reinherit_attrs(dst_inode_u, src_dir_u) &&
	    S_ISDIR(dst_inode_u->bi_mode)) {
		ret = -EXDEV;
		goto err;
	}

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

	ret =   bch2_inode_write(trans, src_dir_iter, src_dir_u) ?:
		(src_dir != dst_dir
		 ? bch2_inode_write(trans, dst_dir_iter, dst_dir_u)
		 : 0 ) ?:
		bch2_inode_write(trans, src_inode_iter, src_inode_u) ?:
		(dst_inode
		 ? bch2_inode_write(trans, dst_inode_iter, dst_inode_u)
		 : 0 );
err:
	bch2_trans_iter_put(trans, dst_inode_iter);
	bch2_trans_iter_put(trans, src_inode_iter);
	bch2_trans_iter_put(trans, dst_dir_iter);
	bch2_trans_iter_put(trans, src_dir_iter);
	return ret;
}
