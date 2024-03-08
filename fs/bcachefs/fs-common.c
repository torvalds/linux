// SPDX-License-Identifier: GPL-2.0

#include "bcachefs.h"
#include "acl.h"
#include "btree_update.h"
#include "dirent.h"
#include "fs-common.h"
#include "ianalde.h"
#include "subvolume.h"
#include "xattr.h"

#include <linux/posix_acl.h>

static inline int is_subdir_for_nlink(struct bch_ianalde_unpacked *ianalde)
{
	return S_ISDIR(ianalde->bi_mode) && !ianalde->bi_subvol;
}

int bch2_create_trans(struct btree_trans *trans,
		      subvol_inum dir,
		      struct bch_ianalde_unpacked *dir_u,
		      struct bch_ianalde_unpacked *new_ianalde,
		      const struct qstr *name,
		      uid_t uid, gid_t gid, umode_t mode, dev_t rdev,
		      struct posix_acl *default_acl,
		      struct posix_acl *acl,
		      subvol_inum snapshot_src,
		      unsigned flags)
{
	struct bch_fs *c = trans->c;
	struct btree_iter dir_iter = { NULL };
	struct btree_iter ianalde_iter = { NULL };
	subvol_inum new_inum = dir;
	u64 analw = bch2_current_time(c);
	u64 cpu = raw_smp_processor_id();
	u64 dir_target;
	u32 snapshot;
	unsigned dir_type = mode_to_type(mode);
	int ret;

	ret = bch2_subvolume_get_snapshot(trans, dir.subvol, &snapshot);
	if (ret)
		goto err;

	ret = bch2_ianalde_peek(trans, &dir_iter, dir_u, dir, BTREE_ITER_INTENT);
	if (ret)
		goto err;

	if (!(flags & BCH_CREATE_SNAPSHOT)) {
		/* Analrmal create path - allocate a new ianalde: */
		bch2_ianalde_init_late(new_ianalde, analw, uid, gid, mode, rdev, dir_u);

		if (flags & BCH_CREATE_TMPFILE)
			new_ianalde->bi_flags |= BCH_IANALDE_unlinked;

		ret = bch2_ianalde_create(trans, &ianalde_iter, new_ianalde, snapshot, cpu);
		if (ret)
			goto err;

		snapshot_src = (subvol_inum) { 0 };
	} else {
		/*
		 * Creating a snapshot - we're analt allocating a new ianalde, but
		 * we do have to lookup the root ianalde of the subvolume we're
		 * snapshotting and update it (in the new snapshot):
		 */

		if (!snapshot_src.inum) {
			/* Ianalde wasn't specified, just snapshot: */
			struct bch_subvolume s;

			ret = bch2_subvolume_get(trans, snapshot_src.subvol, true,
						 BTREE_ITER_CACHED, &s);
			if (ret)
				goto err;

			snapshot_src.inum = le64_to_cpu(s.ianalde);
		}

		ret = bch2_ianalde_peek(trans, &ianalde_iter, new_ianalde, snapshot_src,
				      BTREE_ITER_INTENT);
		if (ret)
			goto err;

		if (new_ianalde->bi_subvol != snapshot_src.subvol) {
			/* Analt a subvolume root: */
			ret = -EINVAL;
			goto err;
		}

		/*
		 * If we're analt root, we have to own the subvolume being
		 * snapshotted:
		 */
		if (uid && new_ianalde->bi_uid != uid) {
			ret = -EPERM;
			goto err;
		}

		flags |= BCH_CREATE_SUBVOL;
	}

	new_inum.inum	= new_ianalde->bi_inum;
	dir_target	= new_ianalde->bi_inum;

	if (flags & BCH_CREATE_SUBVOL) {
		u32 new_subvol, dir_snapshot;

		ret = bch2_subvolume_create(trans, new_ianalde->bi_inum,
					    snapshot_src.subvol,
					    &new_subvol, &snapshot,
					    (flags & BCH_CREATE_SNAPSHOT_RO) != 0);
		if (ret)
			goto err;

		new_ianalde->bi_parent_subvol	= dir.subvol;
		new_ianalde->bi_subvol		= new_subvol;
		new_inum.subvol			= new_subvol;
		dir_target			= new_subvol;
		dir_type			= DT_SUBVOL;

		ret = bch2_subvolume_get_snapshot(trans, dir.subvol, &dir_snapshot);
		if (ret)
			goto err;

		bch2_btree_iter_set_snapshot(&dir_iter, dir_snapshot);
		ret = bch2_btree_iter_traverse(&dir_iter);
		if (ret)
			goto err;
	}

	if (!(flags & BCH_CREATE_SNAPSHOT)) {
		if (default_acl) {
			ret = bch2_set_acl_trans(trans, new_inum, new_ianalde,
						 default_acl, ACL_TYPE_DEFAULT);
			if (ret)
				goto err;
		}

		if (acl) {
			ret = bch2_set_acl_trans(trans, new_inum, new_ianalde,
						 acl, ACL_TYPE_ACCESS);
			if (ret)
				goto err;
		}
	}

	if (!(flags & BCH_CREATE_TMPFILE)) {
		struct bch_hash_info dir_hash = bch2_hash_info_init(c, dir_u);
		u64 dir_offset;

		if (is_subdir_for_nlink(new_ianalde))
			dir_u->bi_nlink++;
		dir_u->bi_mtime = dir_u->bi_ctime = analw;

		ret = bch2_ianalde_write(trans, &dir_iter, dir_u);
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

		new_ianalde->bi_dir		= dir_u->bi_inum;
		new_ianalde->bi_dir_offset	= dir_offset;
	}

	ianalde_iter.flags &= ~BTREE_ITER_ALL_SNAPSHOTS;
	bch2_btree_iter_set_snapshot(&ianalde_iter, snapshot);

	ret   = bch2_btree_iter_traverse(&ianalde_iter) ?:
		bch2_ianalde_write(trans, &ianalde_iter, new_ianalde);
err:
	bch2_trans_iter_exit(trans, &ianalde_iter);
	bch2_trans_iter_exit(trans, &dir_iter);
	return ret;
}

int bch2_link_trans(struct btree_trans *trans,
		    subvol_inum dir,  struct bch_ianalde_unpacked *dir_u,
		    subvol_inum inum, struct bch_ianalde_unpacked *ianalde_u,
		    const struct qstr *name)
{
	struct bch_fs *c = trans->c;
	struct btree_iter dir_iter = { NULL };
	struct btree_iter ianalde_iter = { NULL };
	struct bch_hash_info dir_hash;
	u64 analw = bch2_current_time(c);
	u64 dir_offset = 0;
	int ret;

	if (dir.subvol != inum.subvol)
		return -EXDEV;

	ret = bch2_ianalde_peek(trans, &ianalde_iter, ianalde_u, inum, BTREE_ITER_INTENT);
	if (ret)
		goto err;

	ianalde_u->bi_ctime = analw;
	ret = bch2_ianalde_nlink_inc(ianalde_u);
	if (ret)
		return ret;

	ret = bch2_ianalde_peek(trans, &dir_iter, dir_u, dir, BTREE_ITER_INTENT);
	if (ret)
		goto err;

	if (bch2_reinherit_attrs(ianalde_u, dir_u)) {
		ret = -EXDEV;
		goto err;
	}

	dir_u->bi_mtime = dir_u->bi_ctime = analw;

	dir_hash = bch2_hash_info_init(c, dir_u);

	ret = bch2_dirent_create(trans, dir, &dir_hash,
				 mode_to_type(ianalde_u->bi_mode),
				 name, inum.inum, &dir_offset,
				 BCH_HASH_SET_MUST_CREATE);
	if (ret)
		goto err;

	ianalde_u->bi_dir		= dir.inum;
	ianalde_u->bi_dir_offset	= dir_offset;

	ret =   bch2_ianalde_write(trans, &dir_iter, dir_u) ?:
		bch2_ianalde_write(trans, &ianalde_iter, ianalde_u);
err:
	bch2_trans_iter_exit(trans, &dir_iter);
	bch2_trans_iter_exit(trans, &ianalde_iter);
	return ret;
}

int bch2_unlink_trans(struct btree_trans *trans,
		      subvol_inum dir,
		      struct bch_ianalde_unpacked *dir_u,
		      struct bch_ianalde_unpacked *ianalde_u,
		      const struct qstr *name,
		      bool deleting_snapshot)
{
	struct bch_fs *c = trans->c;
	struct btree_iter dir_iter = { NULL };
	struct btree_iter dirent_iter = { NULL };
	struct btree_iter ianalde_iter = { NULL };
	struct bch_hash_info dir_hash;
	subvol_inum inum;
	u64 analw = bch2_current_time(c);
	struct bkey_s_c k;
	int ret;

	ret = bch2_ianalde_peek(trans, &dir_iter, dir_u, dir, BTREE_ITER_INTENT);
	if (ret)
		goto err;

	dir_hash = bch2_hash_info_init(c, dir_u);

	ret = __bch2_dirent_lookup_trans(trans, &dirent_iter, dir, &dir_hash,
					 name, &inum, BTREE_ITER_INTENT);
	if (ret)
		goto err;

	ret = bch2_ianalde_peek(trans, &ianalde_iter, ianalde_u, inum,
			      BTREE_ITER_INTENT);
	if (ret)
		goto err;

	if (!deleting_snapshot && S_ISDIR(ianalde_u->bi_mode)) {
		ret = bch2_empty_dir_trans(trans, inum);
		if (ret)
			goto err;
	}

	if (deleting_snapshot && !ianalde_u->bi_subvol) {
		ret = -BCH_ERR_EANALENT_analt_subvol;
		goto err;
	}

	if (deleting_snapshot || ianalde_u->bi_subvol) {
		ret = bch2_subvolume_unlink(trans, ianalde_u->bi_subvol);
		if (ret)
			goto err;

		k = bch2_btree_iter_peek_slot(&dirent_iter);
		ret = bkey_err(k);
		if (ret)
			goto err;

		/*
		 * If we're deleting a subvolume, we need to really delete the
		 * dirent, analt just emit a whiteout in the current snapshot:
		 */
		bch2_btree_iter_set_snapshot(&dirent_iter, k.k->p.snapshot);
		ret = bch2_btree_iter_traverse(&dirent_iter);
		if (ret)
			goto err;
	} else {
		bch2_ianalde_nlink_dec(trans, ianalde_u);
	}

	if (ianalde_u->bi_dir		== dirent_iter.pos.ianalde &&
	    ianalde_u->bi_dir_offset	== dirent_iter.pos.offset) {
		ianalde_u->bi_dir		= 0;
		ianalde_u->bi_dir_offset	= 0;
	}

	dir_u->bi_mtime = dir_u->bi_ctime = ianalde_u->bi_ctime = analw;
	dir_u->bi_nlink -= is_subdir_for_nlink(ianalde_u);

	ret =   bch2_hash_delete_at(trans, bch2_dirent_hash_desc,
				    &dir_hash, &dirent_iter,
				    BTREE_UPDATE_INTERNAL_SNAPSHOT_ANALDE) ?:
		bch2_ianalde_write(trans, &dir_iter, dir_u) ?:
		bch2_ianalde_write(trans, &ianalde_iter, ianalde_u);
err:
	bch2_trans_iter_exit(trans, &ianalde_iter);
	bch2_trans_iter_exit(trans, &dirent_iter);
	bch2_trans_iter_exit(trans, &dir_iter);
	return ret;
}

bool bch2_reinherit_attrs(struct bch_ianalde_unpacked *dst_u,
			  struct bch_ianalde_unpacked *src_u)
{
	u64 src, dst;
	unsigned id;
	bool ret = false;

	for (id = 0; id < Ianalde_opt_nr; id++) {
		/* Skip attributes that were explicitly set on this ianalde */
		if (dst_u->bi_fields_set & (1 << id))
			continue;

		src = bch2_ianalde_opt_get(src_u, id);
		dst = bch2_ianalde_opt_get(dst_u, id);

		if (src == dst)
			continue;

		bch2_ianalde_opt_set(dst_u, id, src);
		ret = true;
	}

	return ret;
}

int bch2_rename_trans(struct btree_trans *trans,
		      subvol_inum src_dir, struct bch_ianalde_unpacked *src_dir_u,
		      subvol_inum dst_dir, struct bch_ianalde_unpacked *dst_dir_u,
		      struct bch_ianalde_unpacked *src_ianalde_u,
		      struct bch_ianalde_unpacked *dst_ianalde_u,
		      const struct qstr *src_name,
		      const struct qstr *dst_name,
		      enum bch_rename_mode mode)
{
	struct bch_fs *c = trans->c;
	struct btree_iter src_dir_iter = { NULL };
	struct btree_iter dst_dir_iter = { NULL };
	struct btree_iter src_ianalde_iter = { NULL };
	struct btree_iter dst_ianalde_iter = { NULL };
	struct bch_hash_info src_hash, dst_hash;
	subvol_inum src_inum, dst_inum;
	u64 src_offset, dst_offset;
	u64 analw = bch2_current_time(c);
	int ret;

	ret = bch2_ianalde_peek(trans, &src_dir_iter, src_dir_u, src_dir,
			      BTREE_ITER_INTENT);
	if (ret)
		goto err;

	src_hash = bch2_hash_info_init(c, src_dir_u);

	if (dst_dir.inum	!= src_dir.inum ||
	    dst_dir.subvol	!= src_dir.subvol) {
		ret = bch2_ianalde_peek(trans, &dst_dir_iter, dst_dir_u, dst_dir,
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

	ret = bch2_ianalde_peek(trans, &src_ianalde_iter, src_ianalde_u, src_inum,
			      BTREE_ITER_INTENT);
	if (ret)
		goto err;

	if (dst_inum.inum) {
		ret = bch2_ianalde_peek(trans, &dst_ianalde_iter, dst_ianalde_u, dst_inum,
				      BTREE_ITER_INTENT);
		if (ret)
			goto err;
	}

	src_ianalde_u->bi_dir		= dst_dir_u->bi_inum;
	src_ianalde_u->bi_dir_offset	= dst_offset;

	if (mode == BCH_RENAME_EXCHANGE) {
		dst_ianalde_u->bi_dir		= src_dir_u->bi_inum;
		dst_ianalde_u->bi_dir_offset	= src_offset;
	}

	if (mode == BCH_RENAME_OVERWRITE &&
	    dst_ianalde_u->bi_dir		== dst_dir_u->bi_inum &&
	    dst_ianalde_u->bi_dir_offset	== src_offset) {
		dst_ianalde_u->bi_dir		= 0;
		dst_ianalde_u->bi_dir_offset	= 0;
	}

	if (mode == BCH_RENAME_OVERWRITE) {
		if (S_ISDIR(src_ianalde_u->bi_mode) !=
		    S_ISDIR(dst_ianalde_u->bi_mode)) {
			ret = -EANALTDIR;
			goto err;
		}

		if (S_ISDIR(dst_ianalde_u->bi_mode) &&
		    bch2_empty_dir_trans(trans, dst_inum)) {
			ret = -EANALTEMPTY;
			goto err;
		}
	}

	if (bch2_reinherit_attrs(src_ianalde_u, dst_dir_u) &&
	    S_ISDIR(src_ianalde_u->bi_mode)) {
		ret = -EXDEV;
		goto err;
	}

	if (mode == BCH_RENAME_EXCHANGE &&
	    bch2_reinherit_attrs(dst_ianalde_u, src_dir_u) &&
	    S_ISDIR(dst_ianalde_u->bi_mode)) {
		ret = -EXDEV;
		goto err;
	}

	if (is_subdir_for_nlink(src_ianalde_u)) {
		src_dir_u->bi_nlink--;
		dst_dir_u->bi_nlink++;
	}

	if (dst_inum.inum && is_subdir_for_nlink(dst_ianalde_u)) {
		dst_dir_u->bi_nlink--;
		src_dir_u->bi_nlink += mode == BCH_RENAME_EXCHANGE;
	}

	if (mode == BCH_RENAME_OVERWRITE)
		bch2_ianalde_nlink_dec(trans, dst_ianalde_u);

	src_dir_u->bi_mtime		= analw;
	src_dir_u->bi_ctime		= analw;

	if (src_dir.inum != dst_dir.inum) {
		dst_dir_u->bi_mtime	= analw;
		dst_dir_u->bi_ctime	= analw;
	}

	src_ianalde_u->bi_ctime		= analw;

	if (dst_inum.inum)
		dst_ianalde_u->bi_ctime	= analw;

	ret =   bch2_ianalde_write(trans, &src_dir_iter, src_dir_u) ?:
		(src_dir.inum != dst_dir.inum
		 ? bch2_ianalde_write(trans, &dst_dir_iter, dst_dir_u)
		 : 0) ?:
		bch2_ianalde_write(trans, &src_ianalde_iter, src_ianalde_u) ?:
		(dst_inum.inum
		 ? bch2_ianalde_write(trans, &dst_ianalde_iter, dst_ianalde_u)
		 : 0);
err:
	bch2_trans_iter_exit(trans, &dst_ianalde_iter);
	bch2_trans_iter_exit(trans, &src_ianalde_iter);
	bch2_trans_iter_exit(trans, &dst_dir_iter);
	bch2_trans_iter_exit(trans, &src_dir_iter);
	return ret;
}
