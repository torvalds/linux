// SPDX-License-Identifier: GPL-2.0

#include "bcachefs.h"
#include "acl.h"
#include "btree_update.h"
#include "dirent.h"
#include "inode.h"
#include "namei.h"
#include "subvolume.h"
#include "xattr.h"

#include <linux/posix_acl.h>

static inline subvol_inum parent_inum(subvol_inum inum, struct bch_inode_unpacked *inode)
{
	return (subvol_inum) {
		.subvol	= inode->bi_parent_subvol ?: inum.subvol,
		.inum	= inode->bi_dir,
	};
}

static inline int is_subdir_for_nlink(struct bch_inode_unpacked *inode)
{
	return S_ISDIR(inode->bi_mode) && !inode->bi_subvol;
}

int bch2_create_trans(struct btree_trans *trans,
		      subvol_inum dir,
		      struct bch_inode_unpacked *dir_u,
		      struct bch_inode_unpacked *new_inode,
		      const struct qstr *name,
		      uid_t uid, gid_t gid, umode_t mode, dev_t rdev,
		      struct posix_acl *default_acl,
		      struct posix_acl *acl,
		      subvol_inum snapshot_src,
		      unsigned flags)
{
	struct bch_fs *c = trans->c;
	struct btree_iter dir_iter = {};
	struct btree_iter inode_iter = {};
	subvol_inum new_inum = dir;
	u64 now = bch2_current_time(c);
	u64 cpu = raw_smp_processor_id();
	u64 dir_target;
	u32 snapshot;
	unsigned dir_type = mode_to_type(mode);
	int ret;

	ret = bch2_subvolume_get_snapshot(trans, dir.subvol, &snapshot);
	if (ret)
		goto err;

	ret = bch2_inode_peek(trans, &dir_iter, dir_u, dir,
			      BTREE_ITER_intent|BTREE_ITER_with_updates);
	if (ret)
		goto err;

	if (!(flags & BCH_CREATE_SNAPSHOT)) {
		/* Normal create path - allocate a new inode: */
		bch2_inode_init_late(c, new_inode, now, uid, gid, mode, rdev, dir_u);

		if (flags & BCH_CREATE_TMPFILE)
			new_inode->bi_flags |= BCH_INODE_unlinked;

		ret = bch2_inode_create(trans, &inode_iter, new_inode, snapshot, cpu);
		if (ret)
			goto err;

		snapshot_src = (subvol_inum) { 0 };
	} else {
		/*
		 * Creating a snapshot - we're not allocating a new inode, but
		 * we do have to lookup the root inode of the subvolume we're
		 * snapshotting and update it (in the new snapshot):
		 */

		if (!snapshot_src.inum) {
			/* Inode wasn't specified, just snapshot: */
			struct bch_subvolume s;
			ret = bch2_subvolume_get(trans, snapshot_src.subvol, true, &s);
			if (ret)
				goto err;

			snapshot_src.inum = le64_to_cpu(s.inode);
		}

		ret = bch2_inode_peek(trans, &inode_iter, new_inode, snapshot_src,
				      BTREE_ITER_intent);
		if (ret)
			goto err;

		if (new_inode->bi_subvol != snapshot_src.subvol) {
			/* Not a subvolume root: */
			ret = -EINVAL;
			goto err;
		}

		/*
		 * If we're not root, we have to own the subvolume being
		 * snapshotted:
		 */
		if (uid && new_inode->bi_uid != uid) {
			ret = -EPERM;
			goto err;
		}

		flags |= BCH_CREATE_SUBVOL;
	}

	new_inum.inum	= new_inode->bi_inum;
	dir_target	= new_inode->bi_inum;

	if (flags & BCH_CREATE_SUBVOL) {
		u32 new_subvol, dir_snapshot;

		ret = bch2_subvolume_create(trans, new_inode->bi_inum,
					    dir.subvol,
					    snapshot_src.subvol,
					    &new_subvol, &snapshot,
					    (flags & BCH_CREATE_SNAPSHOT_RO) != 0);
		if (ret)
			goto err;

		new_inode->bi_parent_subvol	= dir.subvol;
		new_inode->bi_subvol		= new_subvol;
		new_inum.subvol			= new_subvol;
		dir_target			= new_subvol;
		dir_type			= DT_SUBVOL;

		ret = bch2_subvolume_get_snapshot(trans, dir.subvol, &dir_snapshot);
		if (ret)
			goto err;

		bch2_btree_iter_set_snapshot(trans, &dir_iter, dir_snapshot);
		ret = bch2_btree_iter_traverse(trans, &dir_iter);
		if (ret)
			goto err;
	}

	if (!(flags & BCH_CREATE_SNAPSHOT)) {
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
	}

	if (!(flags & BCH_CREATE_TMPFILE)) {
		struct bch_hash_info dir_hash = bch2_hash_info_init(c, dir_u);
		u64 dir_offset;

		if (is_subdir_for_nlink(new_inode))
			dir_u->bi_nlink++;
		dir_u->bi_mtime = dir_u->bi_ctime = now;

		ret =   bch2_dirent_create(trans, dir, &dir_hash,
					   dir_type,
					   name,
					   dir_target,
					   &dir_offset,
					   STR_HASH_must_create|BTREE_ITER_with_updates) ?:
			bch2_inode_write(trans, &dir_iter, dir_u);
		if (ret)
			goto err;

		new_inode->bi_dir		= dir_u->bi_inum;
		new_inode->bi_dir_offset	= dir_offset;
	}

	if (S_ISDIR(mode)) {
		ret = bch2_maybe_propagate_has_case_insensitive(trans,
				(subvol_inum) {
					new_inode->bi_subvol ?: dir.subvol,
					new_inode->bi_inum },
				new_inode);
		if (ret)
			goto err;
	}

	if (S_ISDIR(mode) &&
	    !new_inode->bi_subvol)
		new_inode->bi_depth = dir_u->bi_depth + 1;

	inode_iter.flags &= ~BTREE_ITER_all_snapshots;
	bch2_btree_iter_set_snapshot(trans, &inode_iter, snapshot);

	ret   = bch2_btree_iter_traverse(trans, &inode_iter) ?:
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
	struct btree_iter dir_iter = {};
	struct btree_iter inode_iter = {};
	struct bch_hash_info dir_hash;
	u64 now = bch2_current_time(c);
	u64 dir_offset = 0;
	int ret;

	if (dir.subvol != inum.subvol)
		return -EXDEV;

	ret = bch2_inode_peek(trans, &inode_iter, inode_u, inum, BTREE_ITER_intent);
	if (ret)
		return ret;

	inode_u->bi_ctime = now;
	ret = bch2_inode_nlink_inc(inode_u);
	if (ret)
		goto err;

	ret = bch2_inode_peek(trans, &dir_iter, dir_u, dir, BTREE_ITER_intent);
	if (ret)
		goto err;

	if (bch2_reinherit_attrs(inode_u, dir_u)) {
		ret = -EXDEV;
		goto err;
	}

	dir_u->bi_mtime = dir_u->bi_ctime = now;

	dir_hash = bch2_hash_info_init(c, dir_u);

	ret = bch2_dirent_create(trans, dir, &dir_hash,
				 mode_to_type(inode_u->bi_mode),
				 name, inum.inum,
				 &dir_offset,
				 STR_HASH_must_create);
	if (ret)
		goto err;

	inode_u->bi_dir		= dir.inum;
	inode_u->bi_dir_offset	= dir_offset;

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
		      const struct qstr *name,
		      bool deleting_subvol)
{
	struct bch_fs *c = trans->c;
	struct btree_iter dir_iter = {};
	struct btree_iter dirent_iter = {};
	struct btree_iter inode_iter = {};
	struct bch_hash_info dir_hash;
	subvol_inum inum;
	u64 now = bch2_current_time(c);
	struct bkey_s_c k;
	int ret;

	ret = bch2_inode_peek(trans, &dir_iter, dir_u, dir, BTREE_ITER_intent);
	if (ret)
		goto err;

	dir_hash = bch2_hash_info_init(c, dir_u);

	ret = bch2_dirent_lookup_trans(trans, &dirent_iter, dir, &dir_hash,
				       name, &inum, BTREE_ITER_intent);
	if (ret)
		goto err;

	ret = bch2_inode_peek(trans, &inode_iter, inode_u, inum,
			      BTREE_ITER_intent);
	if (ret)
		goto err;

	if (!deleting_subvol && S_ISDIR(inode_u->bi_mode)) {
		ret = bch2_empty_dir_trans(trans, inum);
		if (ret)
			goto err;
	}

	if (deleting_subvol && !inode_u->bi_subvol) {
		ret = bch_err_throw(c, ENOENT_not_subvol);
		goto err;
	}

	if (inode_u->bi_subvol) {
		/* Recursive subvolume destroy not allowed (yet?) */
		ret = bch2_subvol_has_children(trans, inode_u->bi_subvol);
		if (ret)
			goto err;
	}

	if (deleting_subvol || inode_u->bi_subvol) {
		ret = bch2_subvolume_unlink(trans, inode_u->bi_subvol);
		if (ret)
			goto err;

		k = bch2_btree_iter_peek_slot(trans, &dirent_iter);
		ret = bkey_err(k);
		if (ret)
			goto err;

		/*
		 * If we're deleting a subvolume, we need to really delete the
		 * dirent, not just emit a whiteout in the current snapshot:
		 */
		bch2_btree_iter_set_snapshot(trans, &dirent_iter, k.k->p.snapshot);
		ret = bch2_btree_iter_traverse(trans, &dirent_iter);
		if (ret)
			goto err;
	} else {
		bch2_inode_nlink_dec(trans, inode_u);
	}

	if (inode_u->bi_dir		== dirent_iter.pos.inode &&
	    inode_u->bi_dir_offset	== dirent_iter.pos.offset) {
		inode_u->bi_dir		= 0;
		inode_u->bi_dir_offset	= 0;
	}

	dir_u->bi_mtime = dir_u->bi_ctime = inode_u->bi_ctime = now;
	dir_u->bi_nlink -= is_subdir_for_nlink(inode_u);

	ret =   bch2_hash_delete_at(trans, bch2_dirent_hash_desc,
				    &dir_hash, &dirent_iter,
				    BTREE_UPDATE_internal_snapshot_node) ?:
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
		if (!S_ISDIR(dst_u->bi_mode) && id == Inode_opt_casefold)
			continue;

		/* Skip attributes that were explicitly set on this inode */
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

static int subvol_update_parent(struct btree_trans *trans, u32 subvol, u32 new_parent)
{
	struct btree_iter iter;
	struct bkey_i_subvolume *s =
		bch2_bkey_get_mut_typed(trans, &iter,
			BTREE_ID_subvolumes, POS(0, subvol),
			BTREE_ITER_cached, subvolume);
	int ret = PTR_ERR_OR_ZERO(s);
	if (ret)
		return ret;

	s->v.fs_path_parent = cpu_to_le32(new_parent);
	bch2_trans_iter_exit(trans, &iter);
	return 0;
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
	struct btree_iter src_dir_iter = {};
	struct btree_iter dst_dir_iter = {};
	struct btree_iter src_inode_iter = {};
	struct btree_iter dst_inode_iter = {};
	struct bch_hash_info src_hash, dst_hash;
	subvol_inum src_inum, dst_inum;
	u64 src_offset, dst_offset;
	u64 now = bch2_current_time(c);
	int ret;

	ret = bch2_inode_peek(trans, &src_dir_iter, src_dir_u, src_dir,
			      BTREE_ITER_intent);
	if (ret)
		goto err;

	src_hash = bch2_hash_info_init(c, src_dir_u);

	if (!subvol_inum_eq(dst_dir, src_dir)) {
		ret = bch2_inode_peek(trans, &dst_dir_iter, dst_dir_u, dst_dir,
				      BTREE_ITER_intent);
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
			      BTREE_ITER_intent);
	if (ret)
		goto err;

	if (dst_inum.inum) {
		ret = bch2_inode_peek(trans, &dst_inode_iter, dst_inode_u, dst_inum,
				      BTREE_ITER_intent);
		if (ret)
			goto err;
	}

	if (src_inode_u->bi_subvol &&
	    dst_dir.subvol != src_inode_u->bi_parent_subvol) {
		ret = subvol_update_parent(trans, src_inode_u->bi_subvol, dst_dir.subvol);
		if (ret)
			goto err;
	}

	if (mode == BCH_RENAME_EXCHANGE &&
	    dst_inode_u->bi_subvol &&
	    src_dir.subvol != dst_inode_u->bi_parent_subvol) {
		ret = subvol_update_parent(trans, dst_inode_u->bi_subvol, src_dir.subvol);
		if (ret)
			goto err;
	}

	/* Can't move across subvolumes, unless it's a subvolume root: */
	if (src_dir.subvol != dst_dir.subvol &&
	    (!src_inode_u->bi_subvol ||
	     (dst_inum.inum && !dst_inode_u->bi_subvol))) {
		ret = -EXDEV;
		goto err;
	}

	if (src_inode_u->bi_parent_subvol)
		src_inode_u->bi_parent_subvol = dst_dir.subvol;

	if ((mode == BCH_RENAME_EXCHANGE) &&
	    dst_inode_u->bi_parent_subvol)
		dst_inode_u->bi_parent_subvol = src_dir.subvol;

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

	if (mode == BCH_RENAME_OVERWRITE) {
		if (S_ISDIR(src_inode_u->bi_mode) !=
		    S_ISDIR(dst_inode_u->bi_mode)) {
			ret = -ENOTDIR;
			goto err;
		}

		if (S_ISDIR(dst_inode_u->bi_mode)) {
			ret = bch2_empty_dir_trans(trans, dst_inum);
			if (ret)
				goto err;
		}
	}

	if (!subvol_inum_eq(dst_dir, src_dir)) {
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

		ret =   bch2_maybe_propagate_has_case_insensitive(trans, src_inum, src_inode_u) ?:
			(mode == BCH_RENAME_EXCHANGE
			 ? bch2_maybe_propagate_has_case_insensitive(trans, dst_inum, dst_inode_u)
			 : 0);
		if (ret)
			goto err;

		if (is_subdir_for_nlink(src_inode_u)) {
			src_dir_u->bi_nlink--;
			dst_dir_u->bi_nlink++;
		}

		if (S_ISDIR(src_inode_u->bi_mode) &&
		    !src_inode_u->bi_subvol)
			src_inode_u->bi_depth = dst_dir_u->bi_depth + 1;

		if (mode == BCH_RENAME_EXCHANGE &&
		    S_ISDIR(dst_inode_u->bi_mode) &&
		    !dst_inode_u->bi_subvol)
			dst_inode_u->bi_depth = src_dir_u->bi_depth + 1;
	}

	if (dst_inum.inum && is_subdir_for_nlink(dst_inode_u)) {
		dst_dir_u->bi_nlink--;
		src_dir_u->bi_nlink += mode == BCH_RENAME_EXCHANGE;
	}

	if (mode == BCH_RENAME_OVERWRITE)
		bch2_inode_nlink_dec(trans, dst_inode_u);

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
		 : 0) ?:
		bch2_inode_write(trans, &src_inode_iter, src_inode_u) ?:
		(dst_inum.inum
		 ? bch2_inode_write(trans, &dst_inode_iter, dst_inode_u)
		 : 0);
err:
	bch2_trans_iter_exit(trans, &dst_inode_iter);
	bch2_trans_iter_exit(trans, &src_inode_iter);
	bch2_trans_iter_exit(trans, &dst_dir_iter);
	bch2_trans_iter_exit(trans, &src_dir_iter);
	return ret;
}

/* inum_to_path */

static inline void prt_bytes_reversed(struct printbuf *out, const void *b, unsigned n)
{
	bch2_printbuf_make_room(out, n);

	unsigned can_print = min(n, printbuf_remaining(out));

	b += n;

	for (unsigned i = 0; i < can_print; i++)
		out->buf[out->pos++] = *((char *) --b);

	printbuf_nul_terminate(out);
}

static inline void prt_str_reversed(struct printbuf *out, const char *s)
{
	prt_bytes_reversed(out, s, strlen(s));
}

static inline void reverse_bytes(void *b, size_t n)
{
	char *e = b + n, *s = b;

	while (s < e) {
		--e;
		swap(*s, *e);
		s++;
	}
}

static int __bch2_inum_to_path(struct btree_trans *trans,
			       u32 subvol, u64 inum, u32 snapshot,
			       struct printbuf *path)
{
	unsigned orig_pos = path->pos;
	int ret = 0;

	while (true) {
		if (!snapshot) {
			ret = bch2_subvolume_get_snapshot(trans, subvol, &snapshot);
			if (ret)
				goto disconnected;
		}

		struct bch_inode_unpacked inode;
		ret = bch2_inode_find_by_inum_snapshot(trans, inum, snapshot, &inode, 0);
		if (ret)
			goto disconnected;

		if (inode.bi_subvol == BCACHEFS_ROOT_SUBVOL &&
		    inode.bi_inum == BCACHEFS_ROOT_INO)
			break;

		if (!inode.bi_dir && !inode.bi_dir_offset) {
			ret = bch_err_throw(trans->c, ENOENT_inode_no_backpointer);
			goto disconnected;
		}

		inum = inode.bi_dir;
		if (inode.bi_parent_subvol) {
			subvol = inode.bi_parent_subvol;
			snapshot = 0;
		}

		struct btree_iter d_iter;
		struct bkey_s_c_dirent d = bch2_bkey_get_iter_typed(trans, &d_iter,
				BTREE_ID_dirents, SPOS(inode.bi_dir, inode.bi_dir_offset, snapshot),
				0, dirent);
		ret = bkey_err(d.s_c);
		if (ret)
			goto disconnected;

		struct qstr dirent_name = bch2_dirent_get_name(d);
		prt_bytes_reversed(path, dirent_name.name, dirent_name.len);

		prt_char(path, '/');

		bch2_trans_iter_exit(trans, &d_iter);
	}

	if (orig_pos == path->pos)
		prt_char(path, '/');
out:
	ret = path->allocation_failure ? -ENOMEM : 0;
	if (ret)
		goto err;

	reverse_bytes(path->buf + orig_pos, path->pos - orig_pos);
	return 0;
err:
	return ret;
disconnected:
	if (bch2_err_matches(ret, BCH_ERR_transaction_restart))
		goto err;

	prt_str_reversed(path, "(disconnected)");
	goto out;
}

int bch2_inum_to_path(struct btree_trans *trans,
		      subvol_inum inum,
		      struct printbuf *path)
{
	return __bch2_inum_to_path(trans, inum.subvol, inum.inum, 0, path);
}

int bch2_inum_snapshot_to_path(struct btree_trans *trans, u64 inum, u32 snapshot,
			       snapshot_id_list *snapshot_overwrites,
			       struct printbuf *path)
{
	return __bch2_inum_to_path(trans, 0, inum, snapshot, path);
}

/* fsck */

static int bch2_check_dirent_inode_dirent(struct btree_trans *trans,
					  struct bkey_s_c_dirent d,
					  struct bch_inode_unpacked *target,
					  bool in_fsck)
{
	struct bch_fs *c = trans->c;
	struct printbuf buf = PRINTBUF;
	struct btree_iter bp_iter = {};
	int ret = 0;

	if (inode_points_to_dirent(target, d))
		return 0;

	if (!target->bi_dir &&
	    !target->bi_dir_offset) {
		fsck_err_on(S_ISDIR(target->bi_mode),
			    trans, inode_dir_missing_backpointer,
			    "directory with missing backpointer\n%s",
			    (printbuf_reset(&buf),
			     bch2_bkey_val_to_text(&buf, c, d.s_c),
			     prt_printf(&buf, "\n"),
			     bch2_inode_unpacked_to_text(&buf, target),
			     buf.buf));

		fsck_err_on(target->bi_flags & BCH_INODE_unlinked,
			    trans, inode_unlinked_but_has_dirent,
			    "inode unlinked but has dirent\n%s",
			    (printbuf_reset(&buf),
			     bch2_bkey_val_to_text(&buf, c, d.s_c),
			     prt_printf(&buf, "\n"),
			     bch2_inode_unpacked_to_text(&buf, target),
			     buf.buf));

		target->bi_flags &= ~BCH_INODE_unlinked;
		target->bi_dir		= d.k->p.inode;
		target->bi_dir_offset	= d.k->p.offset;
		return __bch2_fsck_write_inode(trans, target);
	}

	struct bkey_s_c_dirent bp_dirent =
		bch2_bkey_get_iter_typed(trans, &bp_iter, BTREE_ID_dirents,
			      SPOS(target->bi_dir, target->bi_dir_offset, target->bi_snapshot),
			      0, dirent);
	ret = bkey_err(bp_dirent);
	if (ret && !bch2_err_matches(ret, ENOENT))
		goto err;

	bool backpointer_exists = !ret;
	ret = 0;

	if (!backpointer_exists) {
		if (fsck_err(trans, inode_wrong_backpointer,
			     "inode %llu:%u has wrong backpointer:\n"
			     "got       %llu:%llu\n"
			     "should be %llu:%llu",
			     target->bi_inum, target->bi_snapshot,
			     target->bi_dir,
			     target->bi_dir_offset,
			     d.k->p.inode,
			     d.k->p.offset)) {
			target->bi_dir		= d.k->p.inode;
			target->bi_dir_offset	= d.k->p.offset;
			ret = __bch2_fsck_write_inode(trans, target);
		}
	} else {
		printbuf_reset(&buf);
		bch2_bkey_val_to_text(&buf, c, d.s_c);
		prt_newline(&buf);
		bch2_bkey_val_to_text(&buf, c, bp_dirent.s_c);

		if (S_ISDIR(target->bi_mode) || target->bi_subvol) {
			/*
			 * XXX: verify connectivity of the other dirent
			 * up to the root before removing this one
			 *
			 * Additionally, bch2_lookup would need to cope with the
			 * dirent it found being removed - or should we remove
			 * the other one, even though the inode points to it?
			 */
			if (in_fsck) {
				if (fsck_err(trans, inode_dir_multiple_links,
					     "%s %llu:%u with multiple links\n%s",
					     S_ISDIR(target->bi_mode) ? "directory" : "subvolume",
					     target->bi_inum, target->bi_snapshot, buf.buf))
					ret = bch2_fsck_remove_dirent(trans, d.k->p);
			} else {
				bch2_fs_inconsistent(c,
						"%s %llu:%u with multiple links\n%s",
						S_ISDIR(target->bi_mode) ? "directory" : "subvolume",
						target->bi_inum, target->bi_snapshot, buf.buf);
			}

			goto out;
		} else {
			/*
			 * hardlinked file with nlink 0:
			 * We're just adjusting nlink here so check_nlinks() will pick
			 * it up, it ignores inodes with nlink 0
			 */
			if (fsck_err_on(!target->bi_nlink,
					trans, inode_multiple_links_but_nlink_0,
					"inode %llu:%u type %s has multiple links but i_nlink 0\n%s",
					target->bi_inum, target->bi_snapshot, bch2_d_types[d.v->d_type], buf.buf)) {
				target->bi_nlink++;
				target->bi_flags &= ~BCH_INODE_unlinked;
				ret = __bch2_fsck_write_inode(trans, target);
				if (ret)
					goto err;
			}
		}
	}
out:
err:
fsck_err:
	bch2_trans_iter_exit(trans, &bp_iter);
	printbuf_exit(&buf);
	bch_err_fn(c, ret);
	return ret;
}

int __bch2_check_dirent_target(struct btree_trans *trans,
			       struct btree_iter *dirent_iter,
			       struct bkey_s_c_dirent d,
			       struct bch_inode_unpacked *target,
			       bool in_fsck)
{
	struct bch_fs *c = trans->c;
	struct printbuf buf = PRINTBUF;
	int ret = 0;

	ret = bch2_check_dirent_inode_dirent(trans, d, target, in_fsck);
	if (ret)
		goto err;

	if (fsck_err_on(d.v->d_type != inode_d_type(target),
			trans, dirent_d_type_wrong,
			"incorrect d_type: got %s, should be %s:\n%s",
			bch2_d_type_str(d.v->d_type),
			bch2_d_type_str(inode_d_type(target)),
			(printbuf_reset(&buf),
			 bch2_bkey_val_to_text(&buf, c, d.s_c), buf.buf))) {
		struct bkey_i_dirent *n = bch2_trans_kmalloc(trans, bkey_bytes(d.k));
		ret = PTR_ERR_OR_ZERO(n);
		if (ret)
			goto err;

		bkey_reassemble(&n->k_i, d.s_c);
		n->v.d_type = inode_d_type(target);
		if (n->v.d_type == DT_SUBVOL) {
			n->v.d_parent_subvol = cpu_to_le32(target->bi_parent_subvol);
			n->v.d_child_subvol = cpu_to_le32(target->bi_subvol);
		} else {
			n->v.d_inum = cpu_to_le64(target->bi_inum);
		}

		ret = bch2_trans_update(trans, dirent_iter, &n->k_i,
					BTREE_UPDATE_internal_snapshot_node);
		if (ret)
			goto err;
	}
err:
fsck_err:
	printbuf_exit(&buf);
	bch_err_fn(c, ret);
	return ret;
}

/*
 * BCH_INODE_has_case_insensitive:
 * We have to track whether directories have any descendent directory that is
 * casefolded - for overlayfs:
 */

static int bch2_propagate_has_case_insensitive(struct btree_trans *trans, subvol_inum inum)
{
	struct btree_iter iter = {};
	int ret = 0;

	while (true) {
		struct bch_inode_unpacked inode;
		ret = bch2_inode_peek(trans, &iter, &inode, inum,
				      BTREE_ITER_intent|BTREE_ITER_with_updates);
		if (ret)
			break;

		if (inode.bi_flags & BCH_INODE_has_case_insensitive)
			break;

		inode.bi_flags |= BCH_INODE_has_case_insensitive;
		ret = bch2_inode_write(trans, &iter, &inode);
		if (ret)
			break;

		bch2_trans_iter_exit(trans, &iter);
		if (subvol_inum_eq(inum, BCACHEFS_ROOT_SUBVOL_INUM))
			break;

		inum = parent_inum(inum, &inode);
	}

	bch2_trans_iter_exit(trans, &iter);
	return ret;
}

int bch2_maybe_propagate_has_case_insensitive(struct btree_trans *trans, subvol_inum inum,
					      struct bch_inode_unpacked *inode)
{
	if (!bch2_inode_casefold(trans->c, inode))
		return 0;

	inode->bi_flags |= BCH_INODE_has_case_insensitive;

	return bch2_propagate_has_case_insensitive(trans, parent_inum(inum, inode));
}

int bch2_check_inode_has_case_insensitive(struct btree_trans *trans,
					  struct bch_inode_unpacked *inode,
					  snapshot_id_list *snapshot_overwrites,
					  bool *do_update)
{
	struct printbuf buf = PRINTBUF;
	bool repairing_parents = false;
	int ret = 0;

	if (!S_ISDIR(inode->bi_mode)) {
		/*
		 * Old versions set bi_casefold for non dirs, but that's
		 * unnecessary and wasteful
		 */
		if (inode->bi_casefold) {
			inode->bi_casefold = 0;
			*do_update = true;
		}
		return 0;
	}

	if (trans->c->sb.version < bcachefs_metadata_version_inode_has_case_insensitive)
		return 0;

	if (bch2_inode_casefold(trans->c, inode) &&
	    !(inode->bi_flags & BCH_INODE_has_case_insensitive)) {
		prt_printf(&buf, "casefolded dir with has_case_insensitive not set\ninum %llu:%u ",
			   inode->bi_inum, inode->bi_snapshot);

		ret = bch2_inum_snapshot_to_path(trans, inode->bi_inum, inode->bi_snapshot,
						 snapshot_overwrites, &buf);
		if (ret)
			goto err;

		if (fsck_err(trans, inode_has_case_insensitive_not_set, "%s", buf.buf)) {
			inode->bi_flags |= BCH_INODE_has_case_insensitive;
			*do_update = true;
		}
	}

	if (!(inode->bi_flags & BCH_INODE_has_case_insensitive))
		goto out;

	struct bch_inode_unpacked dir = *inode;
	u32 snapshot = dir.bi_snapshot;

	while (!(dir.bi_inum	== BCACHEFS_ROOT_INO &&
		 dir.bi_subvol	== BCACHEFS_ROOT_SUBVOL)) {
		if (dir.bi_parent_subvol) {
			ret = bch2_subvolume_get_snapshot(trans, dir.bi_parent_subvol, &snapshot);
			if (ret)
				goto err;

			snapshot_overwrites = NULL;
		}

		ret = bch2_inode_find_by_inum_snapshot(trans, dir.bi_dir, snapshot, &dir, 0);
		if (ret)
			goto err;

		if (!(dir.bi_flags & BCH_INODE_has_case_insensitive)) {
			prt_printf(&buf, "parent of casefolded dir with has_case_insensitive not set\n");

			ret = bch2_inum_snapshot_to_path(trans, dir.bi_inum, dir.bi_snapshot,
							 snapshot_overwrites, &buf);
			if (ret)
				goto err;

			if (fsck_err(trans, inode_parent_has_case_insensitive_not_set, "%s", buf.buf)) {
				dir.bi_flags |= BCH_INODE_has_case_insensitive;
				ret = __bch2_fsck_write_inode(trans, &dir);
				if (ret)
					goto err;
			}
		}

		/*
		 * We only need to check the first parent, unless we find an
		 * inconsistency
		 */
		if (!repairing_parents)
			break;
	}
out:
err:
fsck_err:
	printbuf_exit(&buf);
	if (ret)
		return ret;

	if (repairing_parents) {
		return bch2_trans_commit(trans, NULL, NULL, BCH_TRANS_COMMIT_no_enospc) ?:
			-BCH_ERR_transaction_restart_nested;
	}

	return 0;
}
