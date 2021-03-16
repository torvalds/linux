// SPDX-License-Identifier: GPL-2.0

#include "bcachefs.h"
#include "acl.h"
#include "btree_update.h"
#include "dirent.h"
#include "fs-common.h"
#include "inode.h"
#include "subvolume.h"
#include "xattr.h"

#include <linux/posix_acl.h>

int bch2_create_trans(struct btree_trans *trans,
		      subvol_inum dir,
		      struct bch_inode_unpacked *dir_u,
		      struct bch_inode_unpacked *new_inode,
		      const struct qstr *name,
		      uid_t uid, gid_t gid, umode_t mode, dev_t rdev,
		      struct posix_acl *default_acl,
		      struct posix_acl *acl,
		      unsigned flags)
{
	struct bch_fs *c = trans->c;
	struct btree_iter dir_iter = { NULL };
	struct btree_iter inode_iter = { NULL };
	subvol_inum new_inum = dir;
	u64 now = bch2_current_time(c);
	u64 cpu = raw_smp_processor_id();
	u64 dir_offset = 0;
	u64 dir_target;
	u32 snapshot;
	unsigned dir_type;
	int ret;

	ret = bch2_subvolume_get_snapshot(trans, dir.subvol, &snapshot);
	if (ret)
		goto err;

	ret = bch2_inode_peek(trans, &dir_iter, dir_u, dir, BTREE_ITER_INTENT);
	if (ret)
		goto err;

	bch2_inode_init_late(new_inode, now, uid, gid, mode, rdev, dir_u);

	if (!name)
		new_inode->bi_flags |= BCH_INODE_UNLINKED;

	ret = bch2_inode_create(trans, &inode_iter, new_inode, snapshot, cpu);
	if (ret)
		goto err;

	new_inum.inum	= new_inode->bi_inum;
	dir_target	= new_inode->bi_inum;
	dir_type	= mode_to_type(new_inode->bi_mode);

	if (default_acl) {
		ret = bch2_set_acl_trans(trans, new_inum, new_inode,
					 default_acl, ACL_TYPE_DEFAULT);
		if (ret)
			goto err;
	}

	if (acl) {
		ret = bch2_set_acl_trans(trans, new_inum, new_inode,
					 acl, ACL_TYPE_ACCESS);
		if (ret)
			goto err;
	}

	if (name) {
		struct bch_hash_info dir_hash = bch2_hash_info_init(c, dir_u);

		if (S_ISDIR(new_inode->bi_mode))
			dir_u->bi_nlink++;
		dir_u->bi_mtime = dir_u->bi_ctime = now;

		ret = bch2_inode_write(trans, &dir_iter, dir_u);
		if (ret)
			goto err;

		ret = bch2_dirent_create(trans, dir, &dir_hash,
					 dir_type,
					 name,
					 dir_target,
					 &dir_offset,
					 BCH_HASH_SET_MUST_CREATE);
		if (ret)
			goto err;
	}

	if (c->sb.version >= bcachefs_metadata_version_inode_backpointers) {
		new_inode->bi_dir		= dir_u->bi_inum;
		new_inode->bi_dir_offset	= dir_offset;
	}

	inode_iter.flags &= ~BTREE_ITER_ALL_SNAPSHOTS;
	bch2_btree_iter_set_snapshot(&inode_iter, snapshot);

	ret   = bch2_btree_iter_traverse(&inode_iter) ?:
		bch2_inode_write(trans, &inode_iter, new_inode);
err:
	bch2_trans_iter_exit(trans, &inode_iter);
	bch2_trans_iter_exit(trans, &dir_iter);
	return ret;
}

int bch2_link_trans(struct btree_trans *trans,
		    subvol_inum dir,  struct bch_inode_unpacked *dir_u,
		    subvol_inum inum, struct bch_inode_unpacked *inode_u,
		    const struct qstr *name)
{
	struct bch_fs *c = trans->c;
	struct btree_iter dir_iter = { NULL };
	struct btree_iter inode_iter = { NULL };
	struct bch_hash_info dir_hash;
	u64 now = bch2_current_time(c);
	u64 dir_offset = 0;
	int ret;

	if (dir.subvol != inum.subvol)
		return -EXDEV;

	ret = bch2_inode_peek(trans, &inode_iter, inode_u, inum, BTREE_ITER_INTENT);
	if (ret)
		goto err;

	inode_u->bi_ctime = now;
	bch2_inode_nlink_inc(inode_u);

	ret = bch2_inode_peek(trans, &dir_iter, dir_u, dir, BTREE_ITER_INTENT);
	if (ret)
		goto err;

	dir_u->bi_mtime = dir_u->bi_ctime = now;

	dir_hash = bch2_hash_info_init(c, dir_u);

	ret = bch2_dirent_create(trans, dir, &dir_hash,
				 mode_to_type(inode_u->bi_mode),
				 name, inum.inum, &dir_offset,
				 BCH_HASH_SET_MUST_CREATE);
	if (ret)
		goto err;

	if (c->sb.version >= bcachefs_metadata_version_inode_backpointers) {
		inode_u->bi_dir		= dir.inum;
		inode_u->bi_dir_offset	= dir_offset;
	}

	ret =   bch2_inode_write(trans, &dir_iter, dir_u) ?:
		bch2_inode_write(trans, &inode_iter, inode_u);
err:
	bch2_trans_iter_exit(trans, &dir_iter);
	bch2_trans_iter_exit(trans, &inode_iter);
	return ret;
}

int bch2_unlink_trans(struct btree_trans *trans,
		      subvol_inum dir,
		      struct bch_inode_unpacked *dir_u,
		      struct bch_inode_unpacked *inode_u,
		      const struct qstr *name)
{
	struct bch_fs *c = trans->c;
	struct btree_iter dir_iter = { NULL };
	struct btree_iter dirent_iter = { NULL };
	struct btree_iter inode_iter = { NULL };
	struct bch_hash_info dir_hash;
	subvol_inum inum;
	u64 now = bch2_current_time(c);
	int ret;

	ret = bch2_inode_peek(trans, &dir_iter, dir_u, dir, BTREE_ITER_INTENT);
	if (ret)
		goto err;

	dir_hash = bch2_hash_info_init(c, dir_u);

	ret = __bch2_dirent_lookup_trans(trans, &dirent_iter, dir, &dir_hash,
					 name, &inum, BTREE_ITER_INTENT);
	if (ret)
		goto err;

	ret = bch2_inode_peek(trans, &inode_iter, inode_u, inum,
			      BTREE_ITER_INTENT);
	if (ret)
		goto err;

	if (inode_u->bi_dir		== dirent_iter.pos.inode &&
	    inode_u->bi_dir_offset	== dirent_iter.pos.offset) {
		inode_u->bi_dir		= 0;
		inode_u->bi_dir_offset	= 0;
	}

	if (S_ISDIR(inode_u->bi_mode)) {
		ret = bch2_empty_dir_trans(trans, inum);
		if (ret)
			goto err;
	}

	if (dir.subvol != inum.subvol) {
		ret = bch2_subvolume_delete(trans, inum.subvol, false);
		if (ret)
			goto err;
	}

	dir_u->bi_mtime = dir_u->bi_ctime = inode_u->bi_ctime = now;
	dir_u->bi_nlink -= S_ISDIR(inode_u->bi_mode);
	bch2_inode_nlink_dec(inode_u);

	ret =   bch2_dirent_delete_at(trans, &dir_hash, &dirent_iter) ?:
		bch2_inode_write(trans, &dir_iter, dir_u) ?:
		bch2_inode_write(trans, &inode_iter, inode_u);
err:
	bch2_trans_iter_exit(trans, &inode_iter);
	bch2_trans_iter_exit(trans, &dirent_iter);
	bch2_trans_iter_exit(trans, &dir_iter);
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
		      subvol_inum src_dir, struct bch_inode_unpacked *src_dir_u,
		      subvol_inum dst_dir, struct bch_inode_unpacked *dst_dir_u,
		      struct bch_inode_unpacked *src_inode_u,
		      struct bch_inode_unpacked *dst_inode_u,
		      const struct qstr *src_name,
		      const struct qstr *dst_name,
		      enum bch_rename_mode mode)
{
	struct bch_fs *c = trans->c;
	struct btree_iter src_dir_iter = { NULL };
	struct btree_iter dst_dir_iter = { NULL };
	struct btree_iter src_inode_iter = { NULL };
	struct btree_iter dst_inode_iter = { NULL };
	struct bch_hash_info src_hash, dst_hash;
	subvol_inum src_inum, dst_inum;
	u64 src_offset, dst_offset;
	u64 now = bch2_current_time(c);
	int ret;

	ret = bch2_inode_peek(trans, &src_dir_iter, src_dir_u, src_dir,
			      BTREE_ITER_INTENT);
	if (ret)
		goto err;

	src_hash = bch2_hash_info_init(c, src_dir_u);

	if (dst_dir.inum	!= src_dir.inum ||
	    dst_dir.subvol	!= src_dir.subvol) {
		ret = bch2_inode_peek(trans, &dst_dir_iter, dst_dir_u, dst_dir,
				      BTREE_ITER_INTENT);
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
				 src_name, &src_inum, &src_offset,
				 dst_name, &dst_inum, &dst_offset,
				 mode);
	if (ret)
		goto err;

	ret = bch2_inode_peek(trans, &src_inode_iter, src_inode_u, src_inum,
			      BTREE_ITER_INTENT);
	if (ret)
		goto err;

	if (dst_inum.inum) {
		ret = bch2_inode_peek(trans, &dst_inode_iter, dst_inode_u, dst_inum,
				      BTREE_ITER_INTENT);
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

		if (mode == BCH_RENAME_OVERWRITE &&
		    dst_inode_u->bi_dir		== dst_dir_u->bi_inum &&
		    dst_inode_u->bi_dir_offset	== src_offset) {
			dst_inode_u->bi_dir		= 0;
			dst_inode_u->bi_dir_offset	= 0;
		}
	}

	if (mode == BCH_RENAME_OVERWRITE) {
		if (S_ISDIR(src_inode_u->bi_mode) !=
		    S_ISDIR(dst_inode_u->bi_mode)) {
			ret = -ENOTDIR;
			goto err;
		}

		if (S_ISDIR(dst_inode_u->bi_mode) &&
		    bch2_empty_dir_trans(trans, dst_inum)) {
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

	if (dst_inum.inum && S_ISDIR(dst_inode_u->bi_mode)) {
		dst_dir_u->bi_nlink--;
		src_dir_u->bi_nlink += mode == BCH_RENAME_EXCHANGE;
	}

	if (mode == BCH_RENAME_OVERWRITE)
		bch2_inode_nlink_dec(dst_inode_u);

	src_dir_u->bi_mtime		= now;
	src_dir_u->bi_ctime		= now;

	if (src_dir.inum != dst_dir.inum) {
		dst_dir_u->bi_mtime	= now;
		dst_dir_u->bi_ctime	= now;
	}

	src_inode_u->bi_ctime		= now;

	if (dst_inum.inum)
		dst_inode_u->bi_ctime	= now;

	ret =   bch2_inode_write(trans, &src_dir_iter, src_dir_u) ?:
		(src_dir.inum != dst_dir.inum
		 ? bch2_inode_write(trans, &dst_dir_iter, dst_dir_u)
		 : 0 ) ?:
		bch2_inode_write(trans, &src_inode_iter, src_inode_u) ?:
		(dst_inum.inum
		 ? bch2_inode_write(trans, &dst_inode_iter, dst_inode_u)
		 : 0 );
err:
	bch2_trans_iter_exit(trans, &dst_inode_iter);
	bch2_trans_iter_exit(trans, &src_inode_iter);
	bch2_trans_iter_exit(trans, &dst_dir_iter);
	bch2_trans_iter_exit(trans, &src_dir_iter);
	return ret;
}
