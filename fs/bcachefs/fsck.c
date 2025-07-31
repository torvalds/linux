// SPDX-License-Identifier: GPL-2.0

#include "bcachefs.h"
#include "bcachefs_ioctl.h"
#include "bkey_buf.h"
#include "btree_cache.h"
#include "btree_update.h"
#include "buckets.h"
#include "darray.h"
#include "dirent.h"
#include "error.h"
#include "fs.h"
#include "fsck.h"
#include "inode.h"
#include "keylist.h"
#include "namei.h"
#include "recovery_passes.h"
#include "snapshot.h"
#include "super.h"
#include "thread_with_file.h"
#include "xattr.h"

#include <linux/bsearch.h>
#include <linux/dcache.h> /* struct qstr */

static int dirent_points_to_inode_nowarn(struct bch_fs *c,
					 struct bkey_s_c_dirent d,
					 struct bch_inode_unpacked *inode)
{
	if (d.v->d_type == DT_SUBVOL
	    ? le32_to_cpu(d.v->d_child_subvol)	== inode->bi_subvol
	    : le64_to_cpu(d.v->d_inum)		== inode->bi_inum)
		return 0;
	return bch_err_throw(c, ENOENT_dirent_doesnt_match_inode);
}

static void dirent_inode_mismatch_msg(struct printbuf *out,
				      struct bch_fs *c,
				      struct bkey_s_c_dirent dirent,
				      struct bch_inode_unpacked *inode)
{
	prt_str(out, "inode points to dirent that does not point back:");
	prt_newline(out);
	bch2_bkey_val_to_text(out, c, dirent.s_c);
	prt_newline(out);
	bch2_inode_unpacked_to_text(out, inode);
}

static int dirent_points_to_inode(struct bch_fs *c,
				  struct bkey_s_c_dirent dirent,
				  struct bch_inode_unpacked *inode)
{
	int ret = dirent_points_to_inode_nowarn(c, dirent, inode);
	if (ret) {
		struct printbuf buf = PRINTBUF;
		dirent_inode_mismatch_msg(&buf, c, dirent, inode);
		bch_warn(c, "%s", buf.buf);
		printbuf_exit(&buf);
	}
	return ret;
}

/*
 * XXX: this is handling transaction restarts without returning
 * -BCH_ERR_transaction_restart_nested, this is not how we do things anymore:
 */
static s64 bch2_count_inode_sectors(struct btree_trans *trans, u64 inum,
				    u32 snapshot)
{
	u64 sectors = 0;

	int ret = for_each_btree_key_max(trans, iter, BTREE_ID_extents,
				SPOS(inum, 0, snapshot),
				POS(inum, U64_MAX),
				0, k, ({
		if (bkey_extent_is_allocation(k.k))
			sectors += k.k->size;
		0;
	}));

	return ret ?: sectors;
}

static s64 bch2_count_subdirs(struct btree_trans *trans, u64 inum,
				    u32 snapshot)
{
	u64 subdirs = 0;

	int ret = for_each_btree_key_max(trans, iter, BTREE_ID_dirents,
				    SPOS(inum, 0, snapshot),
				    POS(inum, U64_MAX),
				    0, k, ({
		if (k.k->type == KEY_TYPE_dirent &&
		    bkey_s_c_to_dirent(k).v->d_type == DT_DIR)
			subdirs++;
		0;
	}));

	return ret ?: subdirs;
}

static int subvol_lookup(struct btree_trans *trans, u32 subvol,
			 u32 *snapshot, u64 *inum)
{
	struct bch_subvolume s;
	int ret = bch2_subvolume_get(trans, subvol, false, &s);

	*snapshot = le32_to_cpu(s.snapshot);
	*inum = le64_to_cpu(s.inode);
	return ret;
}

static int lookup_dirent_in_snapshot(struct btree_trans *trans,
			   struct bch_hash_info hash_info,
			   subvol_inum dir, struct qstr *name,
			   u64 *target, unsigned *type, u32 snapshot)
{
	struct btree_iter iter;
	struct bkey_s_c k = bch2_hash_lookup_in_snapshot(trans, &iter, bch2_dirent_hash_desc,
							 &hash_info, dir, name, 0, snapshot);
	int ret = bkey_err(k);
	if (ret)
		return ret;

	struct bkey_s_c_dirent d = bkey_s_c_to_dirent(k);
	*target = le64_to_cpu(d.v->d_inum);
	*type = d.v->d_type;
	bch2_trans_iter_exit(trans, &iter);
	return 0;
}

/*
 * Find any subvolume associated with a tree of snapshots
 * We can't rely on master_subvol - it might have been deleted.
 */
static int find_snapshot_tree_subvol(struct btree_trans *trans,
				     u32 tree_id, u32 *subvol)
{
	struct btree_iter iter;
	struct bkey_s_c k;
	int ret;

	for_each_btree_key_norestart(trans, iter, BTREE_ID_snapshots, POS_MIN, 0, k, ret) {
		if (k.k->type != KEY_TYPE_snapshot)
			continue;

		struct bkey_s_c_snapshot s = bkey_s_c_to_snapshot(k);
		if (le32_to_cpu(s.v->tree) != tree_id)
			continue;

		if (s.v->subvol) {
			*subvol = le32_to_cpu(s.v->subvol);
			goto found;
		}
	}
	ret = bch_err_throw(trans->c, ENOENT_no_snapshot_tree_subvol);
found:
	bch2_trans_iter_exit(trans, &iter);
	return ret;
}

/* Get lost+found, create if it doesn't exist: */
static int lookup_lostfound(struct btree_trans *trans, u32 snapshot,
			    struct bch_inode_unpacked *lostfound,
			    u64 reattaching_inum)
{
	struct bch_fs *c = trans->c;
	struct qstr lostfound_str = QSTR("lost+found");
	struct btree_iter lostfound_iter = {};
	u64 inum = 0;
	unsigned d_type = 0;
	int ret;

	struct bch_snapshot_tree st;
	ret = bch2_snapshot_tree_lookup(trans,
			bch2_snapshot_tree(c, snapshot), &st);
	if (ret)
		return ret;

	u32 subvolid;
	ret = find_snapshot_tree_subvol(trans,
				bch2_snapshot_tree(c, snapshot), &subvolid);
	bch_err_msg(c, ret, "finding subvol associated with snapshot tree %u",
		    bch2_snapshot_tree(c, snapshot));
	if (ret)
		return ret;

	struct bch_subvolume subvol;
	ret = bch2_subvolume_get(trans, subvolid, false, &subvol);
	bch_err_msg(c, ret, "looking up subvol %u for snapshot %u", subvolid, snapshot);
	if (ret)
		return ret;

	if (!subvol.inode) {
		struct btree_iter iter;
		struct bkey_i_subvolume *subvol = bch2_bkey_get_mut_typed(trans, &iter,
				BTREE_ID_subvolumes, POS(0, subvolid),
				0, subvolume);
		ret = PTR_ERR_OR_ZERO(subvol);
		if (ret)
			return ret;

		subvol->v.inode = cpu_to_le64(reattaching_inum);
		bch2_trans_iter_exit(trans, &iter);
	}

	subvol_inum root_inum = {
		.subvol = subvolid,
		.inum = le64_to_cpu(subvol.inode)
	};

	struct bch_inode_unpacked root_inode;
	struct bch_hash_info root_hash_info;
	ret = bch2_inode_find_by_inum_snapshot(trans, root_inum.inum, snapshot, &root_inode, 0);
	bch_err_msg(c, ret, "looking up root inode %llu for subvol %u",
		    root_inum.inum, subvolid);
	if (ret)
		return ret;

	root_hash_info = bch2_hash_info_init(c, &root_inode);

	ret = lookup_dirent_in_snapshot(trans, root_hash_info, root_inum,
			      &lostfound_str, &inum, &d_type, snapshot);
	if (bch2_err_matches(ret, ENOENT))
		goto create_lostfound;

	bch_err_fn(c, ret);
	if (ret)
		return ret;

	if (d_type != DT_DIR) {
		bch_err(c, "error looking up lost+found: not a directory");
		return bch_err_throw(c, ENOENT_not_directory);
	}

	/*
	 * The bch2_check_dirents pass has already run, dangling dirents
	 * shouldn't exist here:
	 */
	ret = bch2_inode_find_by_inum_snapshot(trans, inum, snapshot, lostfound, 0);
	bch_err_msg(c, ret, "looking up lost+found %llu:%u in (root inode %llu, snapshot root %u)",
		    inum, snapshot, root_inum.inum, bch2_snapshot_root(c, snapshot));
	return ret;

create_lostfound:
	/*
	 * we always create lost+found in the root snapshot; we don't want
	 * different branches of the snapshot tree to have different lost+found
	 */
	snapshot = le32_to_cpu(st.root_snapshot);
	/*
	 * XXX: we could have a nicer log message here  if we had a nice way to
	 * walk backpointers to print a path
	 */
	struct printbuf path = PRINTBUF;
	ret = bch2_inum_to_path(trans, root_inum, &path);
	if (ret)
		goto err;

	bch_notice(c, "creating %s/lost+found in subvol %llu snapshot %u",
		   path.buf, root_inum.subvol, snapshot);
	printbuf_exit(&path);

	u64 now = bch2_current_time(c);
	u64 cpu = raw_smp_processor_id();

	bch2_inode_init_early(c, lostfound);
	bch2_inode_init_late(c, lostfound, now, 0, 0, S_IFDIR|0700, 0, &root_inode);
	lostfound->bi_dir = root_inode.bi_inum;
	lostfound->bi_snapshot = le32_to_cpu(st.root_snapshot);

	root_inode.bi_nlink++;

	ret = bch2_inode_create(trans, &lostfound_iter, lostfound, snapshot, cpu);
	if (ret)
		goto err;

	bch2_btree_iter_set_snapshot(trans, &lostfound_iter, snapshot);
	ret = bch2_btree_iter_traverse(trans, &lostfound_iter);
	if (ret)
		goto err;

	ret =   bch2_dirent_create_snapshot(trans,
				0, root_inode.bi_inum, snapshot, &root_hash_info,
				mode_to_type(lostfound->bi_mode),
				&lostfound_str,
				lostfound->bi_inum,
				&lostfound->bi_dir_offset,
				BTREE_UPDATE_internal_snapshot_node|
				STR_HASH_must_create) ?:
		bch2_inode_write_flags(trans, &lostfound_iter, lostfound,
				       BTREE_UPDATE_internal_snapshot_node);
err:
	bch_err_msg(c, ret, "creating lost+found");
	bch2_trans_iter_exit(trans, &lostfound_iter);
	return ret;
}

static inline bool inode_should_reattach(struct bch_inode_unpacked *inode)
{
	if (inode->bi_inum == BCACHEFS_ROOT_INO &&
	    inode->bi_subvol == BCACHEFS_ROOT_SUBVOL)
		return false;

	/*
	 * Subvolume roots are special: older versions of subvolume roots may be
	 * disconnected, it's only the newest version that matters.
	 *
	 * We only keep a single dirent pointing to a subvolume root, i.e.
	 * older versions of snapshots will not have a different dirent pointing
	 * to the same subvolume root.
	 *
	 * This is because dirents that point to subvolumes are only visible in
	 * the parent subvolume - versioning is not needed - and keeping them
	 * around would break fsck, because when we're crossing subvolumes we
	 * don't have a consistent snapshot ID to do check the inode <-> dirent
	 * relationships.
	 *
	 * Thus, a subvolume root that's been renamed after a snapshot will have
	 * a disconnected older version - that's expected.
	 *
	 * Note that taking a snapshot always updates the root inode (to update
	 * the dirent backpointer), so a subvolume root inode with
	 * BCH_INODE_has_child_snapshot is never visible.
	 */
	if (inode->bi_subvol &&
	    (inode->bi_flags & BCH_INODE_has_child_snapshot))
		return false;

	return !bch2_inode_has_backpointer(inode) &&
		!(inode->bi_flags & BCH_INODE_unlinked);
}

static int maybe_delete_dirent(struct btree_trans *trans, struct bpos d_pos, u32 snapshot)
{
	struct btree_iter iter;
	struct bkey_s_c k = bch2_bkey_get_iter(trans, &iter, BTREE_ID_dirents,
					SPOS(d_pos.inode, d_pos.offset, snapshot),
					BTREE_ITER_intent|
					BTREE_ITER_with_updates);
	int ret = bkey_err(k);
	if (ret)
		return ret;

	if (bpos_eq(k.k->p, d_pos)) {
		/*
		 * delet_at() doesn't work because the update path doesn't
		 * internally use BTREE_ITER_with_updates yet
		 */
		struct bkey_i *k = bch2_trans_kmalloc(trans, sizeof(*k));
		ret = PTR_ERR_OR_ZERO(k);
		if (ret)
			goto err;

		bkey_init(&k->k);
		k->k.type = KEY_TYPE_whiteout;
		k->k.p = iter.pos;
		ret = bch2_trans_update(trans, &iter, k, BTREE_UPDATE_internal_snapshot_node);
	}
err:
	bch2_trans_iter_exit(trans, &iter);
	return ret;
}

static int reattach_inode(struct btree_trans *trans, struct bch_inode_unpacked *inode)
{
	struct bch_fs *c = trans->c;
	struct bch_inode_unpacked lostfound;
	char name_buf[20];
	int ret;

	u32 dirent_snapshot = inode->bi_snapshot;
	if (inode->bi_subvol) {
		inode->bi_parent_subvol = BCACHEFS_ROOT_SUBVOL;

		struct btree_iter subvol_iter;
		struct bkey_i_subvolume *subvol =
			bch2_bkey_get_mut_typed(trans, &subvol_iter,
						BTREE_ID_subvolumes, POS(0, inode->bi_subvol),
						0, subvolume);
		ret = PTR_ERR_OR_ZERO(subvol);
		if (ret)
			return ret;

		subvol->v.fs_path_parent = BCACHEFS_ROOT_SUBVOL;
		bch2_trans_iter_exit(trans, &subvol_iter);

		u64 root_inum;
		ret = subvol_lookup(trans, inode->bi_parent_subvol,
				    &dirent_snapshot, &root_inum);
		if (ret)
			return ret;

		snprintf(name_buf, sizeof(name_buf), "subvol-%u", inode->bi_subvol);
	} else {
		snprintf(name_buf, sizeof(name_buf), "%llu", inode->bi_inum);
	}

	ret = lookup_lostfound(trans, dirent_snapshot, &lostfound, inode->bi_inum);
	if (ret)
		return ret;

	bch_verbose(c, "got lostfound inum %llu", lostfound.bi_inum);

	lostfound.bi_nlink += S_ISDIR(inode->bi_mode);

	/* ensure lost+found inode is also present in inode snapshot */
	if (!inode->bi_subvol) {
		BUG_ON(!bch2_snapshot_is_ancestor(c, inode->bi_snapshot, lostfound.bi_snapshot));
		lostfound.bi_snapshot = inode->bi_snapshot;
	}

	ret = __bch2_fsck_write_inode(trans, &lostfound);
	if (ret)
		return ret;

	struct bch_hash_info dir_hash = bch2_hash_info_init(c, &lostfound);
	struct qstr name = QSTR(name_buf);

	inode->bi_dir = lostfound.bi_inum;

	ret = bch2_dirent_create_snapshot(trans,
				inode->bi_parent_subvol, lostfound.bi_inum,
				dirent_snapshot,
				&dir_hash,
				inode_d_type(inode),
				&name,
				inode->bi_subvol ?: inode->bi_inum,
				&inode->bi_dir_offset,
				BTREE_UPDATE_internal_snapshot_node|
				STR_HASH_must_create);
	if (ret) {
		bch_err_msg(c, ret, "error creating dirent");
		return ret;
	}

	ret = __bch2_fsck_write_inode(trans, inode);
	if (ret)
		return ret;

	{
		CLASS(printbuf, buf)();
		ret = bch2_inum_snapshot_to_path(trans, inode->bi_inum,
						 inode->bi_snapshot, NULL, &buf);
		if (ret)
			return ret;

		bch_info(c, "reattached at %s", buf.buf);
	}

	/*
	 * Fix up inodes in child snapshots: if they should also be reattached
	 * update the backpointer field, if they should not be we need to emit
	 * whiteouts for the dirent we just created.
	 */
	if (!inode->bi_subvol && bch2_snapshot_is_leaf(c, inode->bi_snapshot) <= 0) {
		snapshot_id_list whiteouts_done;
		struct btree_iter iter;
		struct bkey_s_c k;

		darray_init(&whiteouts_done);

		for_each_btree_key_reverse_norestart(trans, iter,
				BTREE_ID_inodes, SPOS(0, inode->bi_inum, inode->bi_snapshot - 1),
				BTREE_ITER_all_snapshots|BTREE_ITER_intent, k, ret) {
			if (k.k->p.offset != inode->bi_inum)
				break;

			if (!bkey_is_inode(k.k) ||
			    !bch2_snapshot_is_ancestor(c, k.k->p.snapshot, inode->bi_snapshot) ||
			    snapshot_list_has_ancestor(c, &whiteouts_done, k.k->p.snapshot))
				continue;

			struct bch_inode_unpacked child_inode;
			ret = bch2_inode_unpack(k, &child_inode);
			if (ret)
				break;

			if (!inode_should_reattach(&child_inode)) {
				ret = maybe_delete_dirent(trans,
							  SPOS(lostfound.bi_inum, inode->bi_dir_offset,
							       dirent_snapshot),
							  k.k->p.snapshot);
				if (ret)
					break;

				ret = snapshot_list_add(c, &whiteouts_done, k.k->p.snapshot);
				if (ret)
					break;
			} else {
				iter.snapshot = k.k->p.snapshot;
				child_inode.bi_dir = inode->bi_dir;
				child_inode.bi_dir_offset = inode->bi_dir_offset;

				ret = bch2_inode_write_flags(trans, &iter, &child_inode,
							     BTREE_UPDATE_internal_snapshot_node);
				if (ret)
					break;
			}
		}
		darray_exit(&whiteouts_done);
		bch2_trans_iter_exit(trans, &iter);
	}

	return ret;
}

static struct bkey_s_c_dirent dirent_get_by_pos(struct btree_trans *trans,
						struct btree_iter *iter,
						struct bpos pos)
{
	return bch2_bkey_get_iter_typed(trans, iter, BTREE_ID_dirents, pos, 0, dirent);
}

static int remove_backpointer(struct btree_trans *trans,
			      struct bch_inode_unpacked *inode)
{
	if (!bch2_inode_has_backpointer(inode))
		return 0;

	u32 snapshot = inode->bi_snapshot;

	if (inode->bi_parent_subvol) {
		int ret = bch2_subvolume_get_snapshot(trans, inode->bi_parent_subvol, &snapshot);
		if (ret)
			return ret;
	}

	struct bch_fs *c = trans->c;
	struct btree_iter iter;
	struct bkey_s_c_dirent d = dirent_get_by_pos(trans, &iter,
				     SPOS(inode->bi_dir, inode->bi_dir_offset, snapshot));
	int ret = bkey_err(d) ?:
		  dirent_points_to_inode(c, d, inode) ?:
		  bch2_fsck_remove_dirent(trans, d.k->p);
	bch2_trans_iter_exit(trans, &iter);
	return ret;
}

static int reattach_subvol(struct btree_trans *trans, struct bkey_s_c_subvolume s)
{
	struct bch_fs *c = trans->c;

	struct bch_inode_unpacked inode;
	int ret = bch2_inode_find_by_inum_trans(trans,
				(subvol_inum) { s.k->p.offset, le64_to_cpu(s.v->inode) },
				&inode);
	if (ret)
		return ret;

	ret = remove_backpointer(trans, &inode);
	if (!bch2_err_matches(ret, ENOENT))
		bch_err_msg(c, ret, "removing dirent");
	if (ret)
		return ret;

	ret = reattach_inode(trans, &inode);
	bch_err_msg(c, ret, "reattaching inode %llu", inode.bi_inum);
	return ret;
}

static int reconstruct_subvol(struct btree_trans *trans, u32 snapshotid, u32 subvolid, u64 inum)
{
	struct bch_fs *c = trans->c;

	if (!bch2_snapshot_is_leaf(c, snapshotid)) {
		bch_err(c, "need to reconstruct subvol, but have interior node snapshot");
		return bch_err_throw(c, fsck_repair_unimplemented);
	}

	/*
	 * If inum isn't set, that means we're being called from check_dirents,
	 * not check_inodes - the root of this subvolume doesn't exist or we
	 * would have found it there:
	 */
	if (!inum) {
		struct btree_iter inode_iter = {};
		struct bch_inode_unpacked new_inode;
		u64 cpu = raw_smp_processor_id();

		bch2_inode_init_early(c, &new_inode);
		bch2_inode_init_late(c, &new_inode, bch2_current_time(c), 0, 0, S_IFDIR|0755, 0, NULL);

		new_inode.bi_subvol = subvolid;

		int ret = bch2_inode_create(trans, &inode_iter, &new_inode, snapshotid, cpu) ?:
			  bch2_btree_iter_traverse(trans, &inode_iter) ?:
			  bch2_inode_write(trans, &inode_iter, &new_inode);
		bch2_trans_iter_exit(trans, &inode_iter);
		if (ret)
			return ret;

		inum = new_inode.bi_inum;
	}

	bch_info(c, "reconstructing subvol %u with root inode %llu", subvolid, inum);

	struct bkey_i_subvolume *new_subvol = bch2_trans_kmalloc(trans, sizeof(*new_subvol));
	int ret = PTR_ERR_OR_ZERO(new_subvol);
	if (ret)
		return ret;

	bkey_subvolume_init(&new_subvol->k_i);
	new_subvol->k.p.offset	= subvolid;
	new_subvol->v.snapshot	= cpu_to_le32(snapshotid);
	new_subvol->v.inode	= cpu_to_le64(inum);
	ret = bch2_btree_insert_trans(trans, BTREE_ID_subvolumes, &new_subvol->k_i, 0);
	if (ret)
		return ret;

	struct btree_iter iter;
	struct bkey_i_snapshot *s = bch2_bkey_get_mut_typed(trans, &iter,
			BTREE_ID_snapshots, POS(0, snapshotid),
			0, snapshot);
	ret = PTR_ERR_OR_ZERO(s);
	bch_err_msg(c, ret, "getting snapshot %u", snapshotid);
	if (ret)
		return ret;

	u32 snapshot_tree = le32_to_cpu(s->v.tree);

	s->v.subvol = cpu_to_le32(subvolid);
	SET_BCH_SNAPSHOT_SUBVOL(&s->v, true);
	bch2_trans_iter_exit(trans, &iter);

	struct bkey_i_snapshot_tree *st = bch2_bkey_get_mut_typed(trans, &iter,
			BTREE_ID_snapshot_trees, POS(0, snapshot_tree),
			0, snapshot_tree);
	ret = PTR_ERR_OR_ZERO(st);
	bch_err_msg(c, ret, "getting snapshot tree %u", snapshot_tree);
	if (ret)
		return ret;

	if (!st->v.master_subvol)
		st->v.master_subvol = cpu_to_le32(subvolid);

	bch2_trans_iter_exit(trans, &iter);
	return 0;
}

static int reconstruct_inode(struct btree_trans *trans, enum btree_id btree, u32 snapshot, u64 inum)
{
	struct bch_fs *c = trans->c;
	unsigned i_mode = S_IFREG;
	u64 i_size = 0;

	switch (btree) {
	case BTREE_ID_extents: {
		struct btree_iter iter = {};

		bch2_trans_iter_init(trans, &iter, BTREE_ID_extents, SPOS(inum, U64_MAX, snapshot), 0);
		struct bkey_s_c k = bch2_btree_iter_peek_prev_min(trans, &iter, POS(inum, 0));
		bch2_trans_iter_exit(trans, &iter);
		int ret = bkey_err(k);
		if (ret)
			return ret;

		i_size = k.k->p.offset << 9;
		break;
	}
	case BTREE_ID_dirents:
		i_mode = S_IFDIR;
		break;
	case BTREE_ID_xattrs:
		break;
	default:
		BUG();
	}

	struct bch_inode_unpacked new_inode;
	bch2_inode_init_early(c, &new_inode);
	bch2_inode_init_late(c, &new_inode, bch2_current_time(c), 0, 0, i_mode|0600, 0, NULL);
	new_inode.bi_size = i_size;
	new_inode.bi_inum = inum;
	new_inode.bi_snapshot = snapshot;

	return __bch2_fsck_write_inode(trans, &new_inode);
}

static inline void snapshots_seen_exit(struct snapshots_seen *s)
{
	darray_exit(&s->ids);
}

static inline void snapshots_seen_init(struct snapshots_seen *s)
{
	memset(s, 0, sizeof(*s));
}

static int snapshots_seen_add_inorder(struct bch_fs *c, struct snapshots_seen *s, u32 id)
{
	u32 *i;
	__darray_for_each(s->ids, i) {
		if (*i == id)
			return 0;
		if (*i > id)
			break;
	}

	int ret = darray_insert_item(&s->ids, i - s->ids.data, id);
	if (ret)
		bch_err(c, "error reallocating snapshots_seen table (size %zu)",
			s->ids.size);
	return ret;
}

static int snapshots_seen_update(struct bch_fs *c, struct snapshots_seen *s,
				 enum btree_id btree_id, struct bpos pos)
{
	if (!bkey_eq(s->pos, pos))
		s->ids.nr = 0;
	s->pos = pos;

	return snapshot_list_add_nodup(c, &s->ids, pos.snapshot);
}

/**
 * key_visible_in_snapshot - returns true if @id is a descendent of @ancestor,
 * and @ancestor hasn't been overwritten in @seen
 *
 * @c:		filesystem handle
 * @seen:	list of snapshot ids already seen at current position
 * @id:		descendent snapshot id
 * @ancestor:	ancestor snapshot id
 *
 * Returns:	whether key in @ancestor snapshot is visible in @id snapshot
 */
static bool key_visible_in_snapshot(struct bch_fs *c, struct snapshots_seen *seen,
				    u32 id, u32 ancestor)
{
	EBUG_ON(id > ancestor);

	if (id == ancestor)
		return true;

	if (!bch2_snapshot_is_ancestor(c, id, ancestor))
		return false;

	/*
	 * We know that @id is a descendant of @ancestor, we're checking if
	 * we've seen a key that overwrote @ancestor - i.e. also a descendent of
	 * @ascestor and with @id as a descendent.
	 *
	 * But we already know that we're scanning IDs between @id and @ancestor
	 * numerically, since snapshot ID lists are kept sorted, so if we find
	 * an id that's an ancestor of @id we're done:
	 */
	darray_for_each_reverse(seen->ids, i)
		if (*i != ancestor && bch2_snapshot_is_ancestor(c, id, *i))
			return false;

	return true;
}

/**
 * ref_visible - given a key with snapshot id @src that points to a key with
 * snapshot id @dst, test whether there is some snapshot in which @dst is
 * visible.
 *
 * @c:		filesystem handle
 * @s:		list of snapshot IDs already seen at @src
 * @src:	snapshot ID of src key
 * @dst:	snapshot ID of dst key
 * Returns:	true if there is some snapshot in which @dst is visible
 *
 * Assumes we're visiting @src keys in natural key order
 */
static bool ref_visible(struct bch_fs *c, struct snapshots_seen *s,
			u32 src, u32 dst)
{
	return dst <= src
		? key_visible_in_snapshot(c, s, dst, src)
		: bch2_snapshot_is_ancestor(c, src, dst);
}

static int ref_visible2(struct bch_fs *c,
			u32 src, struct snapshots_seen *src_seen,
			u32 dst, struct snapshots_seen *dst_seen)
{
	if (dst > src) {
		swap(dst, src);
		swap(dst_seen, src_seen);
	}
	return key_visible_in_snapshot(c, src_seen, dst, src);
}

#define for_each_visible_inode(_c, _s, _w, _snapshot, _i)				\
	for (_i = (_w)->inodes.data; _i < (_w)->inodes.data + (_w)->inodes.nr &&	\
	     (_i)->inode.bi_snapshot <= (_snapshot); _i++)				\
		if (key_visible_in_snapshot(_c, _s, _i->inode.bi_snapshot, _snapshot))

struct inode_walker_entry {
	struct bch_inode_unpacked inode;
	bool			whiteout;
	u64			count;
	u64			i_size;
};

struct inode_walker {
	bool				first_this_inode;
	bool				have_inodes;
	bool				recalculate_sums;
	struct bpos			last_pos;

	DARRAY(struct inode_walker_entry) inodes;
	snapshot_id_list		deletes;
};

static void inode_walker_exit(struct inode_walker *w)
{
	darray_exit(&w->inodes);
	darray_exit(&w->deletes);
}

static struct inode_walker inode_walker_init(void)
{
	return (struct inode_walker) { 0, };
}

static int add_inode(struct bch_fs *c, struct inode_walker *w,
		     struct bkey_s_c inode)
{
	int ret = darray_push(&w->inodes, ((struct inode_walker_entry) {
		.whiteout	= !bkey_is_inode(inode.k),
	}));
	if (ret)
		return ret;

	struct inode_walker_entry *n = &darray_last(w->inodes);
	if (!n->whiteout) {
		return bch2_inode_unpack(inode, &n->inode);
	} else {
		n->inode.bi_inum	= inode.k->p.offset;
		n->inode.bi_snapshot	= inode.k->p.snapshot;
		return 0;
	}
}

static int get_inodes_all_snapshots(struct btree_trans *trans,
				    struct inode_walker *w, u64 inum)
{
	struct bch_fs *c = trans->c;
	struct btree_iter iter;
	struct bkey_s_c k;
	int ret;

	/*
	 * We no longer have inodes for w->last_pos; clear this to avoid
	 * screwing up check_i_sectors/check_subdir_count if we take a
	 * transaction restart here:
	 */
	w->have_inodes = false;
	w->recalculate_sums = false;
	w->inodes.nr = 0;

	for_each_btree_key_max_norestart(trans, iter,
			BTREE_ID_inodes, POS(0, inum), SPOS(0, inum, U32_MAX),
			BTREE_ITER_all_snapshots, k, ret) {
		ret = add_inode(c, w, k);
		if (ret)
			break;
	}
	bch2_trans_iter_exit(trans, &iter);

	if (ret)
		return ret;

	w->first_this_inode = true;
	w->have_inodes = true;
	return 0;
}

static int get_visible_inodes(struct btree_trans *trans,
			      struct inode_walker *w,
			      struct snapshots_seen *s,
			      u64 inum)
{
	struct bch_fs *c = trans->c;
	struct btree_iter iter;
	struct bkey_s_c k;
	int ret;

	w->inodes.nr = 0;
	w->deletes.nr = 0;

	for_each_btree_key_reverse_norestart(trans, iter, BTREE_ID_inodes, SPOS(0, inum, s->pos.snapshot),
			   BTREE_ITER_all_snapshots, k, ret) {
		if (k.k->p.offset != inum)
			break;

		if (!ref_visible(c, s, s->pos.snapshot, k.k->p.snapshot))
			continue;

		if (snapshot_list_has_ancestor(c, &w->deletes, k.k->p.snapshot))
			continue;

		ret = bkey_is_inode(k.k)
			? add_inode(c, w, k)
			: snapshot_list_add(c, &w->deletes, k.k->p.snapshot);
		if (ret)
			break;
	}
	bch2_trans_iter_exit(trans, &iter);

	return ret;
}

static struct inode_walker_entry *
lookup_inode_for_snapshot(struct btree_trans *trans, struct inode_walker *w, struct bkey_s_c k)
{
	struct bch_fs *c = trans->c;

	struct inode_walker_entry *i = darray_find_p(w->inodes, i,
		    bch2_snapshot_is_ancestor(c, k.k->p.snapshot, i->inode.bi_snapshot));

	if (!i)
		return NULL;

	struct printbuf buf = PRINTBUF;
	int ret = 0;

	if (fsck_err_on(k.k->p.snapshot != i->inode.bi_snapshot,
			trans, snapshot_key_missing_inode_snapshot,
			 "have key for inode %llu:%u but have inode in ancestor snapshot %u\n"
			 "unexpected because we should always update the inode when we update a key in that inode\n"
			 "%s",
			 w->last_pos.inode, k.k->p.snapshot, i->inode.bi_snapshot,
			 (bch2_bkey_val_to_text(&buf, c, k),
			  buf.buf))) {
		if (!i->whiteout) {
			struct bch_inode_unpacked new = i->inode;
			new.bi_snapshot = k.k->p.snapshot;
			ret = __bch2_fsck_write_inode(trans, &new);
		} else {
			struct bkey_i whiteout;
			bkey_init(&whiteout.k);
			whiteout.k.type = KEY_TYPE_whiteout;
			whiteout.k.p = SPOS(0, i->inode.bi_inum, k.k->p.snapshot);
			ret = bch2_btree_insert_nonextent(trans, BTREE_ID_inodes,
							  &whiteout,
							  BTREE_UPDATE_internal_snapshot_node);
		}

		if (ret)
			goto fsck_err;

		ret = bch2_trans_commit(trans, NULL, NULL, 0);
		if (ret)
			goto fsck_err;

		struct inode_walker_entry new_entry = *i;

		new_entry.inode.bi_snapshot	= k.k->p.snapshot;
		new_entry.count			= 0;
		new_entry.i_size		= 0;

		while (i > w->inodes.data && i[-1].inode.bi_snapshot > k.k->p.snapshot)
			--i;

		size_t pos = i - w->inodes.data;
		ret = darray_insert_item(&w->inodes, pos, new_entry);
		if (ret)
			goto fsck_err;

		ret = bch_err_throw(c, transaction_restart_nested);
		goto fsck_err;
	}

	printbuf_exit(&buf);
	return i;
fsck_err:
	printbuf_exit(&buf);
	return ERR_PTR(ret);
}

static struct inode_walker_entry *walk_inode(struct btree_trans *trans,
					     struct inode_walker *w,
					     struct bkey_s_c k)
{
	if (w->last_pos.inode != k.k->p.inode) {
		int ret = get_inodes_all_snapshots(trans, w, k.k->p.inode);
		if (ret)
			return ERR_PTR(ret);
	}

	w->last_pos = k.k->p;

	return lookup_inode_for_snapshot(trans, w, k);
}

/*
 * Prefer to delete the first one, since that will be the one at the wrong
 * offset:
 * return value: 0 -> delete k1, 1 -> delete k2
 */
int bch2_fsck_update_backpointers(struct btree_trans *trans,
				  struct snapshots_seen *s,
				  const struct bch_hash_desc desc,
				  struct bch_hash_info *hash_info,
				  struct bkey_i *new)
{
	if (new->k.type != KEY_TYPE_dirent)
		return 0;

	struct bkey_i_dirent *d = bkey_i_to_dirent(new);
	struct inode_walker target = inode_walker_init();
	int ret = 0;

	if (d->v.d_type == DT_SUBVOL) {
		bch_err(trans->c, "%s does not support DT_SUBVOL", __func__);
		ret = -BCH_ERR_fsck_repair_unimplemented;
	} else {
		ret = get_visible_inodes(trans, &target, s, le64_to_cpu(d->v.d_inum));
		if (ret)
			goto err;

		darray_for_each(target.inodes, i) {
			i->inode.bi_dir_offset = d->k.p.offset;
			ret = __bch2_fsck_write_inode(trans, &i->inode);
			if (ret)
				goto err;
		}
	}
err:
	inode_walker_exit(&target);
	return ret;
}

static struct bkey_s_c_dirent inode_get_dirent(struct btree_trans *trans,
					       struct btree_iter *iter,
					       struct bch_inode_unpacked *inode,
					       u32 *snapshot)
{
	if (inode->bi_subvol) {
		u64 inum;
		int ret = subvol_lookup(trans, inode->bi_parent_subvol, snapshot, &inum);
		if (ret)
			return ((struct bkey_s_c_dirent) { .k = ERR_PTR(ret) });
	}

	return dirent_get_by_pos(trans, iter, SPOS(inode->bi_dir, inode->bi_dir_offset, *snapshot));
}

static int check_inode_deleted_list(struct btree_trans *trans, struct bpos p)
{
	struct btree_iter iter;
	struct bkey_s_c k = bch2_bkey_get_iter(trans, &iter, BTREE_ID_deleted_inodes, p, 0);
	int ret = bkey_err(k) ?: k.k->type == KEY_TYPE_set;
	bch2_trans_iter_exit(trans, &iter);
	return ret;
}

static int check_inode_dirent_inode(struct btree_trans *trans,
				    struct bch_inode_unpacked *inode,
				    bool *write_inode)
{
	struct bch_fs *c = trans->c;
	struct printbuf buf = PRINTBUF;

	u32 inode_snapshot = inode->bi_snapshot;
	struct btree_iter dirent_iter = {};
	struct bkey_s_c_dirent d = inode_get_dirent(trans, &dirent_iter, inode, &inode_snapshot);
	int ret = bkey_err(d);
	if (ret && !bch2_err_matches(ret, ENOENT))
		return ret;

	if ((ret || dirent_points_to_inode_nowarn(c, d, inode)) &&
	    inode->bi_subvol &&
	    (inode->bi_flags & BCH_INODE_has_child_snapshot)) {
		/* Older version of a renamed subvolume root: we won't have a
		 * correct dirent for it. That's expected, see
		 * inode_should_reattach().
		 *
		 * We don't clear the backpointer field when doing the rename
		 * because there might be arbitrarily many versions in older
		 * snapshots.
		 */
		inode->bi_dir = 0;
		inode->bi_dir_offset = 0;
		*write_inode = true;
		goto out;
	}

	if (fsck_err_on(ret,
			trans, inode_points_to_missing_dirent,
			"inode points to missing dirent\n%s",
			(bch2_inode_unpacked_to_text(&buf, inode), buf.buf)) ||
	    fsck_err_on(!ret && dirent_points_to_inode_nowarn(c, d, inode),
			trans, inode_points_to_wrong_dirent,
			"%s",
			(printbuf_reset(&buf),
			 dirent_inode_mismatch_msg(&buf, c, d, inode),
			 buf.buf))) {
		/*
		 * We just clear the backpointer fields for now. If we find a
		 * dirent that points to this inode in check_dirents(), we'll
		 * update it then; then when we get to check_path() if the
		 * backpointer is still 0 we'll reattach it.
		 */
		inode->bi_dir = 0;
		inode->bi_dir_offset = 0;
		*write_inode = true;
	}
out:
	ret = 0;
fsck_err:
	bch2_trans_iter_exit(trans, &dirent_iter);
	printbuf_exit(&buf);
	bch_err_fn(c, ret);
	return ret;
}

static int check_inode(struct btree_trans *trans,
		       struct btree_iter *iter,
		       struct bkey_s_c k,
		       struct bch_inode_unpacked *snapshot_root,
		       struct snapshots_seen *s)
{
	struct bch_fs *c = trans->c;
	struct printbuf buf = PRINTBUF;
	struct bch_inode_unpacked u;
	bool do_update = false;
	int ret;

	ret = bch2_check_key_has_snapshot(trans, iter, k);
	if (ret < 0)
		goto err;
	if (ret)
		return 0;

	ret = snapshots_seen_update(c, s, iter->btree_id, k.k->p);
	if (ret)
		goto err;

	if (!bkey_is_inode(k.k))
		return 0;

	ret = bch2_inode_unpack(k, &u);
	if (ret)
		goto err;

	if (snapshot_root->bi_inum != u.bi_inum) {
		ret = bch2_inode_find_snapshot_root(trans, u.bi_inum, snapshot_root);
		if (ret)
			goto err;
	}

	if (u.bi_hash_seed	!= snapshot_root->bi_hash_seed ||
	    INODE_STR_HASH(&u)	!= INODE_STR_HASH(snapshot_root)) {
		ret = bch2_repair_inode_hash_info(trans, snapshot_root);
		BUG_ON(ret == -BCH_ERR_fsck_repair_unimplemented);
		if (ret)
			goto err;
	}

	ret = bch2_check_inode_has_case_insensitive(trans, &u, &s->ids, &do_update);
	if (ret)
		goto err;

	if (bch2_inode_has_backpointer(&u)) {
		ret = check_inode_dirent_inode(trans, &u, &do_update);
		if (ret)
			goto err;
	}

	if (fsck_err_on(bch2_inode_has_backpointer(&u) &&
			(u.bi_flags & BCH_INODE_unlinked),
			trans, inode_unlinked_but_has_dirent,
			"inode unlinked but has dirent\n%s",
			(printbuf_reset(&buf),
			 bch2_inode_unpacked_to_text(&buf, &u),
			 buf.buf))) {
		u.bi_flags &= ~BCH_INODE_unlinked;
		do_update = true;
	}

	if (S_ISDIR(u.bi_mode) && (u.bi_flags & BCH_INODE_unlinked)) {
		/* Check for this early so that check_unreachable_inode() will reattach it */

		ret = bch2_empty_dir_snapshot(trans, k.k->p.offset, 0, k.k->p.snapshot);
		if (ret && ret != -BCH_ERR_ENOTEMPTY_dir_not_empty)
			goto err;

		fsck_err_on(ret, trans, inode_dir_unlinked_but_not_empty,
			    "dir unlinked but not empty\n%s",
			    (printbuf_reset(&buf),
			     bch2_inode_unpacked_to_text(&buf, &u),
			     buf.buf));
		u.bi_flags &= ~BCH_INODE_unlinked;
		do_update = true;
		ret = 0;
	}

	if (fsck_err_on(S_ISDIR(u.bi_mode) && u.bi_size,
			trans, inode_dir_has_nonzero_i_size,
			"directory %llu:%u with nonzero i_size %lli",
			u.bi_inum, u.bi_snapshot, u.bi_size)) {
		u.bi_size = 0;
		do_update = true;
	}

	ret = bch2_inode_has_child_snapshots(trans, k.k->p);
	if (ret < 0)
		goto err;

	if (fsck_err_on(ret != !!(u.bi_flags & BCH_INODE_has_child_snapshot),
			trans, inode_has_child_snapshots_wrong,
			"inode has_child_snapshots flag wrong (should be %u)\n%s",
			ret,
			(printbuf_reset(&buf),
			 bch2_inode_unpacked_to_text(&buf, &u),
			 buf.buf))) {
		if (ret)
			u.bi_flags |= BCH_INODE_has_child_snapshot;
		else
			u.bi_flags &= ~BCH_INODE_has_child_snapshot;
		do_update = true;
	}
	ret = 0;

	if ((u.bi_flags & BCH_INODE_unlinked) &&
	    !(u.bi_flags & BCH_INODE_has_child_snapshot)) {
		if (!test_bit(BCH_FS_started, &c->flags)) {
			/*
			 * If we're not in online fsck, don't delete unlinked
			 * inodes, just make sure they're on the deleted list.
			 *
			 * They might be referred to by a logged operation -
			 * i.e. we might have crashed in the middle of a
			 * truncate on an unlinked but open file - so we want to
			 * let the delete_dead_inodes kill it after resuming
			 * logged ops.
			 */
			ret = check_inode_deleted_list(trans, k.k->p);
			if (ret < 0)
				goto err_noprint;

			fsck_err_on(!ret,
				    trans, unlinked_inode_not_on_deleted_list,
				    "inode %llu:%u unlinked, but not on deleted list",
				    u.bi_inum, k.k->p.snapshot);

			ret = bch2_btree_bit_mod_buffered(trans, BTREE_ID_deleted_inodes, k.k->p, 1);
			if (ret)
				goto err;
		} else {
			ret = bch2_inode_or_descendents_is_open(trans, k.k->p);
			if (ret < 0)
				goto err;

			if (fsck_err_on(!ret,
					trans, inode_unlinked_and_not_open,
				      "inode %llu:%u unlinked and not open",
				      u.bi_inum, u.bi_snapshot)) {
				ret = bch2_inode_rm_snapshot(trans, u.bi_inum, iter->pos.snapshot);
				bch_err_msg(c, ret, "in fsck deleting inode");
				goto err_noprint;
			}
			ret = 0;
		}
	}

	if (fsck_err_on(u.bi_parent_subvol &&
			(u.bi_subvol == 0 ||
			 u.bi_subvol == BCACHEFS_ROOT_SUBVOL),
			trans, inode_bi_parent_nonzero,
			"inode %llu:%u has subvol %u but nonzero parent subvol %u",
			u.bi_inum, k.k->p.snapshot, u.bi_subvol, u.bi_parent_subvol)) {
		u.bi_parent_subvol = 0;
		do_update = true;
	}

	if (u.bi_subvol) {
		struct bch_subvolume s;

		ret = bch2_subvolume_get(trans, u.bi_subvol, false, &s);
		if (ret && !bch2_err_matches(ret, ENOENT))
			goto err;

		if (ret && (c->sb.btrees_lost_data & BIT_ULL(BTREE_ID_subvolumes))) {
			ret = reconstruct_subvol(trans, k.k->p.snapshot, u.bi_subvol, u.bi_inum);
			goto do_update;
		}

		if (fsck_err_on(ret,
				trans, inode_bi_subvol_missing,
				"inode %llu:%u bi_subvol points to missing subvolume %u",
				u.bi_inum, k.k->p.snapshot, u.bi_subvol) ||
		    fsck_err_on(le64_to_cpu(s.inode) != u.bi_inum ||
				!bch2_snapshot_is_ancestor(c, le32_to_cpu(s.snapshot),
							   k.k->p.snapshot),
				trans, inode_bi_subvol_wrong,
				"inode %llu:%u points to subvol %u, but subvol points to %llu:%u",
				u.bi_inum, k.k->p.snapshot, u.bi_subvol,
				le64_to_cpu(s.inode),
				le32_to_cpu(s.snapshot))) {
			u.bi_subvol = 0;
			u.bi_parent_subvol = 0;
			do_update = true;
		}
	}

	if (fsck_err_on(u.bi_journal_seq > journal_cur_seq(&c->journal),
			trans, inode_journal_seq_in_future,
			"inode journal seq in future (currently at %llu)\n%s",
			journal_cur_seq(&c->journal),
			(printbuf_reset(&buf),
			 bch2_inode_unpacked_to_text(&buf, &u),
			buf.buf))) {
		u.bi_journal_seq = journal_cur_seq(&c->journal);
		do_update = true;
	}
do_update:
	if (do_update) {
		ret = __bch2_fsck_write_inode(trans, &u);
		bch_err_msg(c, ret, "in fsck updating inode");
		if (ret)
			goto err_noprint;
	}
err:
fsck_err:
	bch_err_fn(c, ret);
err_noprint:
	printbuf_exit(&buf);
	return ret;
}

int bch2_check_inodes(struct bch_fs *c)
{
	struct bch_inode_unpacked snapshot_root = {};
	struct snapshots_seen s;

	snapshots_seen_init(&s);

	int ret = bch2_trans_run(c,
		for_each_btree_key_commit(trans, iter, BTREE_ID_inodes,
				POS_MIN,
				BTREE_ITER_prefetch|BTREE_ITER_all_snapshots, k,
				NULL, NULL, BCH_TRANS_COMMIT_no_enospc,
			check_inode(trans, &iter, k, &snapshot_root, &s)));

	snapshots_seen_exit(&s);
	bch_err_fn(c, ret);
	return ret;
}

static int find_oldest_inode_needs_reattach(struct btree_trans *trans,
					    struct bch_inode_unpacked *inode)
{
	struct bch_fs *c = trans->c;
	struct btree_iter iter;
	struct bkey_s_c k;
	int ret = 0;

	/*
	 * We look for inodes to reattach in natural key order, leaves first,
	 * but we should do the reattach at the oldest version that needs to be
	 * reattached:
	 */
	for_each_btree_key_norestart(trans, iter,
				     BTREE_ID_inodes,
				     SPOS(0, inode->bi_inum, inode->bi_snapshot + 1),
				     BTREE_ITER_all_snapshots, k, ret) {
		if (k.k->p.offset != inode->bi_inum)
			break;

		if (!bch2_snapshot_is_ancestor(c, inode->bi_snapshot, k.k->p.snapshot))
			continue;

		if (!bkey_is_inode(k.k))
			break;

		struct bch_inode_unpacked parent_inode;
		ret = bch2_inode_unpack(k, &parent_inode);
		if (ret)
			break;

		if (!inode_should_reattach(&parent_inode))
			break;

		*inode = parent_inode;
	}
	bch2_trans_iter_exit(trans, &iter);

	return ret;
}

static int check_unreachable_inode(struct btree_trans *trans,
				   struct btree_iter *iter,
				   struct bkey_s_c k)
{
	struct printbuf buf = PRINTBUF;
	int ret = 0;

	if (!bkey_is_inode(k.k))
		return 0;

	struct bch_inode_unpacked inode;
	ret = bch2_inode_unpack(k, &inode);
	if (ret)
		return ret;

	if (!inode_should_reattach(&inode))
		return 0;

	ret = find_oldest_inode_needs_reattach(trans, &inode);
	if (ret)
		return ret;

	if (fsck_err(trans, inode_unreachable,
		     "unreachable inode:\n%s",
		     (bch2_inode_unpacked_to_text(&buf, &inode),
		      buf.buf)))
		ret = reattach_inode(trans, &inode);
fsck_err:
	printbuf_exit(&buf);
	return ret;
}

/*
 * Reattach unreachable (but not unlinked) inodes
 *
 * Run after check_inodes() and check_dirents(), so we node that inode
 * backpointer fields point to valid dirents, and every inode that has a dirent
 * that points to it has its backpointer field set - so we're just looking for
 * non-unlinked inodes without backpointers:
 *
 * XXX: this is racy w.r.t. hardlink removal in online fsck
 */
int bch2_check_unreachable_inodes(struct bch_fs *c)
{
	int ret = bch2_trans_run(c,
		for_each_btree_key_commit(trans, iter, BTREE_ID_inodes,
				POS_MIN,
				BTREE_ITER_prefetch|BTREE_ITER_all_snapshots, k,
				NULL, NULL, BCH_TRANS_COMMIT_no_enospc,
			check_unreachable_inode(trans, &iter, k)));
	bch_err_fn(c, ret);
	return ret;
}

static inline bool btree_matches_i_mode(enum btree_id btree, unsigned mode)
{
	switch (btree) {
	case BTREE_ID_extents:
		return S_ISREG(mode) || S_ISLNK(mode);
	case BTREE_ID_dirents:
		return S_ISDIR(mode);
	case BTREE_ID_xattrs:
		return true;
	default:
		BUG();
	}
}

static int check_key_has_inode(struct btree_trans *trans,
			       struct btree_iter *iter,
			       struct inode_walker *inode,
			       struct inode_walker_entry *i,
			       struct bkey_s_c k)
{
	struct bch_fs *c = trans->c;
	struct printbuf buf = PRINTBUF;
	struct btree_iter iter2 = {};
	int ret = PTR_ERR_OR_ZERO(i);
	if (ret)
		return ret;

	if (k.k->type == KEY_TYPE_whiteout)
		goto out;

	bool have_inode = i && !i->whiteout;

	if (!have_inode && (c->sb.btrees_lost_data & BIT_ULL(BTREE_ID_inodes)))
		goto reconstruct;

	if (have_inode && btree_matches_i_mode(iter->btree_id, i->inode.bi_mode))
		goto out;

	prt_printf(&buf, ", ");

	bool have_old_inode = false;
	darray_for_each(inode->inodes, i2)
		if (!i2->whiteout &&
		    bch2_snapshot_is_ancestor(c, k.k->p.snapshot, i2->inode.bi_snapshot) &&
		    btree_matches_i_mode(iter->btree_id, i2->inode.bi_mode)) {
			prt_printf(&buf, "but found good inode in older snapshot\n");
			bch2_inode_unpacked_to_text(&buf, &i2->inode);
			prt_newline(&buf);
			have_old_inode = true;
			break;
		}

	struct bkey_s_c k2;
	unsigned nr_keys = 0;

	prt_printf(&buf, "found keys:\n");

	for_each_btree_key_max_norestart(trans, iter2, iter->btree_id,
					 SPOS(k.k->p.inode, 0, k.k->p.snapshot),
					 POS(k.k->p.inode, U64_MAX),
					 0, k2, ret) {
		nr_keys++;
		if (nr_keys <= 10) {
			bch2_bkey_val_to_text(&buf, c, k2);
			prt_newline(&buf);
		}
		if (nr_keys >= 100)
			break;
	}

	if (ret)
		goto err;

	if (nr_keys > 100)
		prt_printf(&buf, "found > %u keys for this missing inode\n", nr_keys);
	else if (nr_keys > 10)
		prt_printf(&buf, "found %u keys for this missing inode\n", nr_keys);

	if (!have_inode) {
		if (fsck_err_on(!have_inode,
				trans, key_in_missing_inode,
				"key in missing inode%s", buf.buf)) {
			/*
			 * Maybe a deletion that raced with data move, or something
			 * weird like that? But if we know the inode was deleted, or
			 * it's just a few keys, we can safely delete them.
			 *
			 * If it's many keys, we should probably recreate the inode
			 */
			if (have_old_inode || nr_keys <= 2)
				goto delete;
			else
				goto reconstruct;
		}
	} else {
		/*
		 * not autofix, this one would be a giant wtf - bit error in the
		 * inode corrupting i_mode?
		 *
		 * may want to try repairing inode instead of deleting
		 */
		if (fsck_err_on(!btree_matches_i_mode(iter->btree_id, i->inode.bi_mode),
				trans, key_in_wrong_inode_type,
				"key for wrong inode mode %o%s",
				i->inode.bi_mode, buf.buf))
			goto delete;
	}
out:
err:
fsck_err:
	bch2_trans_iter_exit(trans, &iter2);
	printbuf_exit(&buf);
	bch_err_fn(c, ret);
	return ret;
delete:
	/*
	 * XXX: print out more info
	 * count up extents for this inode, check if we have different inode in
	 * an older snapshot version, perhaps decide if we want to reconstitute
	 */
	ret = bch2_btree_delete_at(trans, iter, BTREE_UPDATE_internal_snapshot_node);
	goto out;
reconstruct:
	ret =   reconstruct_inode(trans, iter->btree_id, k.k->p.snapshot, k.k->p.inode) ?:
		bch2_trans_commit(trans, NULL, NULL, BCH_TRANS_COMMIT_no_enospc);
	if (ret)
		goto err;

	inode->last_pos.inode--;
	ret = bch_err_throw(c, transaction_restart_nested);
	goto out;
}

static int check_i_sectors_notnested(struct btree_trans *trans, struct inode_walker *w)
{
	struct bch_fs *c = trans->c;
	int ret = 0;
	s64 count2;

	darray_for_each(w->inodes, i) {
		if (i->inode.bi_sectors == i->count)
			continue;

		count2 = bch2_count_inode_sectors(trans, w->last_pos.inode, i->inode.bi_snapshot);

		if (w->recalculate_sums)
			i->count = count2;

		if (i->count != count2) {
			bch_err_ratelimited(c, "fsck counted i_sectors wrong for inode %llu:%u: got %llu should be %llu",
					    w->last_pos.inode, i->inode.bi_snapshot, i->count, count2);
			i->count = count2;
		}

		if (fsck_err_on(!(i->inode.bi_flags & BCH_INODE_i_sectors_dirty),
				trans, inode_i_sectors_wrong,
				"inode %llu:%u has incorrect i_sectors: got %llu, should be %llu",
				w->last_pos.inode, i->inode.bi_snapshot,
				i->inode.bi_sectors, i->count)) {
			i->inode.bi_sectors = i->count;
			ret = bch2_fsck_write_inode(trans, &i->inode);
			if (ret)
				break;
		}
	}
fsck_err:
	bch_err_fn(c, ret);
	return ret;
}

static int check_i_sectors(struct btree_trans *trans, struct inode_walker *w)
{
	u32 restart_count = trans->restart_count;
	return check_i_sectors_notnested(trans, w) ?:
		trans_was_restarted(trans, restart_count);
}

struct extent_end {
	u32			snapshot;
	u64			offset;
	struct snapshots_seen	seen;
};

struct extent_ends {
	struct bpos			last_pos;
	DARRAY(struct extent_end)	e;
};

static void extent_ends_reset(struct extent_ends *extent_ends)
{
	darray_for_each(extent_ends->e, i)
		snapshots_seen_exit(&i->seen);
	extent_ends->e.nr = 0;
}

static void extent_ends_exit(struct extent_ends *extent_ends)
{
	extent_ends_reset(extent_ends);
	darray_exit(&extent_ends->e);
}

static void extent_ends_init(struct extent_ends *extent_ends)
{
	memset(extent_ends, 0, sizeof(*extent_ends));
}

static int extent_ends_at(struct bch_fs *c,
			  struct extent_ends *extent_ends,
			  struct snapshots_seen *seen,
			  struct bkey_s_c k)
{
	struct extent_end *i, n = (struct extent_end) {
		.offset		= k.k->p.offset,
		.snapshot	= k.k->p.snapshot,
		.seen		= *seen,
	};

	n.seen.ids.data = kmemdup(seen->ids.data,
			      sizeof(seen->ids.data[0]) * seen->ids.size,
			      GFP_KERNEL);
	if (!n.seen.ids.data)
		return bch_err_throw(c, ENOMEM_fsck_extent_ends_at);

	__darray_for_each(extent_ends->e, i) {
		if (i->snapshot == k.k->p.snapshot) {
			snapshots_seen_exit(&i->seen);
			*i = n;
			return 0;
		}

		if (i->snapshot >= k.k->p.snapshot)
			break;
	}

	return darray_insert_item(&extent_ends->e, i - extent_ends->e.data, n);
}

static int overlapping_extents_found(struct btree_trans *trans,
				     enum btree_id btree,
				     struct bpos pos1, struct snapshots_seen *pos1_seen,
				     struct bkey pos2,
				     bool *fixed,
				     struct extent_end *extent_end)
{
	struct bch_fs *c = trans->c;
	struct printbuf buf = PRINTBUF;
	struct btree_iter iter1, iter2 = {};
	struct bkey_s_c k1, k2;
	int ret;

	BUG_ON(bkey_le(pos1, bkey_start_pos(&pos2)));

	bch2_trans_iter_init(trans, &iter1, btree, pos1,
			     BTREE_ITER_all_snapshots|
			     BTREE_ITER_not_extents);
	k1 = bch2_btree_iter_peek_max(trans, &iter1, POS(pos1.inode, U64_MAX));
	ret = bkey_err(k1);
	if (ret)
		goto err;

	prt_newline(&buf);
	bch2_bkey_val_to_text(&buf, c, k1);

	if (!bpos_eq(pos1, k1.k->p)) {
		prt_str(&buf, "\nwanted\n  ");
		bch2_bpos_to_text(&buf, pos1);
		prt_str(&buf, "\n");
		bch2_bkey_to_text(&buf, &pos2);

		bch_err(c, "%s: error finding first overlapping extent when repairing, got%s",
			__func__, buf.buf);
		ret = bch_err_throw(c, internal_fsck_err);
		goto err;
	}

	bch2_trans_copy_iter(trans, &iter2, &iter1);

	while (1) {
		bch2_btree_iter_advance(trans, &iter2);

		k2 = bch2_btree_iter_peek_max(trans, &iter2, POS(pos1.inode, U64_MAX));
		ret = bkey_err(k2);
		if (ret)
			goto err;

		if (bpos_ge(k2.k->p, pos2.p))
			break;
	}

	prt_newline(&buf);
	bch2_bkey_val_to_text(&buf, c, k2);

	if (bpos_gt(k2.k->p, pos2.p) ||
	    pos2.size != k2.k->size) {
		bch_err(c, "%s: error finding seconding overlapping extent when repairing%s",
			__func__, buf.buf);
		ret = bch_err_throw(c, internal_fsck_err);
		goto err;
	}

	prt_printf(&buf, "\noverwriting %s extent",
		   pos1.snapshot >= pos2.p.snapshot ? "first" : "second");

	if (fsck_err(trans, extent_overlapping,
		     "overlapping extents%s", buf.buf)) {
		struct btree_iter *old_iter = &iter1;
		struct disk_reservation res = { 0 };

		if (pos1.snapshot < pos2.p.snapshot) {
			old_iter = &iter2;
			swap(k1, k2);
		}

		trans->extra_disk_res += bch2_bkey_sectors_compressed(k2);

		ret =   bch2_trans_update_extent_overwrite(trans, old_iter,
				BTREE_UPDATE_internal_snapshot_node,
				k1, k2) ?:
			bch2_trans_commit(trans, &res, NULL, BCH_TRANS_COMMIT_no_enospc);
		bch2_disk_reservation_put(c, &res);

		bch_info(c, "repair ret %s", bch2_err_str(ret));

		if (ret)
			goto err;

		*fixed = true;

		if (pos1.snapshot == pos2.p.snapshot) {
			/*
			 * We overwrote the first extent, and did the overwrite
			 * in the same snapshot:
			 */
			extent_end->offset = bkey_start_offset(&pos2);
		} else if (pos1.snapshot > pos2.p.snapshot) {
			/*
			 * We overwrote the first extent in pos2's snapshot:
			 */
			ret = snapshots_seen_add_inorder(c, pos1_seen, pos2.p.snapshot);
		} else {
			/*
			 * We overwrote the second extent - restart
			 * check_extent() from the top:
			 */
			ret = bch_err_throw(c, transaction_restart_nested);
		}
	}
fsck_err:
err:
	bch2_trans_iter_exit(trans, &iter2);
	bch2_trans_iter_exit(trans, &iter1);
	printbuf_exit(&buf);
	return ret;
}

static int check_overlapping_extents(struct btree_trans *trans,
			      struct snapshots_seen *seen,
			      struct extent_ends *extent_ends,
			      struct bkey_s_c k,
			      struct btree_iter *iter,
			      bool *fixed)
{
	struct bch_fs *c = trans->c;
	int ret = 0;

	/* transaction restart, running again */
	if (bpos_eq(extent_ends->last_pos, k.k->p))
		return 0;

	if (extent_ends->last_pos.inode != k.k->p.inode)
		extent_ends_reset(extent_ends);

	darray_for_each(extent_ends->e, i) {
		if (i->offset <= bkey_start_offset(k.k))
			continue;

		if (!ref_visible2(c,
				  k.k->p.snapshot, seen,
				  i->snapshot, &i->seen))
			continue;

		ret = overlapping_extents_found(trans, iter->btree_id,
						SPOS(iter->pos.inode,
						     i->offset,
						     i->snapshot),
						&i->seen,
						*k.k, fixed, i);
		if (ret)
			goto err;
	}

	extent_ends->last_pos = k.k->p;
err:
	return ret;
}

static int check_extent_overbig(struct btree_trans *trans, struct btree_iter *iter,
				struct bkey_s_c k)
{
	struct bch_fs *c = trans->c;
	struct bkey_ptrs_c ptrs = bch2_bkey_ptrs_c(k);
	struct bch_extent_crc_unpacked crc;
	const union bch_extent_entry *i;
	unsigned encoded_extent_max_sectors = c->opts.encoded_extent_max >> 9;

	bkey_for_each_crc(k.k, ptrs, crc, i)
		if (crc_is_encoded(crc) &&
		    crc.uncompressed_size > encoded_extent_max_sectors) {
			struct printbuf buf = PRINTBUF;

			bch2_bkey_val_to_text(&buf, c, k);
			bch_err(c, "overbig encoded extent, please report this:\n  %s", buf.buf);
			printbuf_exit(&buf);
		}

	return 0;
}

static int check_extent(struct btree_trans *trans, struct btree_iter *iter,
			struct bkey_s_c k,
			struct inode_walker *inode,
			struct snapshots_seen *s,
			struct extent_ends *extent_ends,
			struct disk_reservation *res)
{
	struct bch_fs *c = trans->c;
	struct printbuf buf = PRINTBUF;
	int ret = 0;

	ret = bch2_check_key_has_snapshot(trans, iter, k);
	if (ret) {
		ret = ret < 0 ? ret : 0;
		goto out;
	}

	if (inode->last_pos.inode != k.k->p.inode && inode->have_inodes) {
		ret = check_i_sectors(trans, inode);
		if (ret)
			goto err;
	}

	ret = snapshots_seen_update(c, s, iter->btree_id, k.k->p);
	if (ret)
		goto err;

	struct inode_walker_entry *extent_i = walk_inode(trans, inode, k);
	ret = PTR_ERR_OR_ZERO(extent_i);
	if (ret)
		goto err;

	ret = check_key_has_inode(trans, iter, inode, extent_i, k);
	if (ret)
		goto err;

	if (k.k->type != KEY_TYPE_whiteout) {
		ret = check_overlapping_extents(trans, s, extent_ends, k, iter,
						&inode->recalculate_sums);
		if (ret)
			goto err;

		/*
		 * Check inodes in reverse order, from oldest snapshots to
		 * newest, starting from the inode that matches this extent's
		 * snapshot. If we didn't have one, iterate over all inodes:
		 */
		for (struct inode_walker_entry *i = extent_i ?: &darray_last(inode->inodes);
		     inode->inodes.data && i >= inode->inodes.data;
		     --i) {
			if (i->inode.bi_snapshot > k.k->p.snapshot ||
			    !key_visible_in_snapshot(c, s, i->inode.bi_snapshot, k.k->p.snapshot))
				continue;

			u64 last_block = round_up(i->inode.bi_size, block_bytes(c)) >> 9;

			if (fsck_err_on(k.k->p.offset > last_block &&
					!bkey_extent_is_reservation(k),
					trans, extent_past_end_of_inode,
					"extent type past end of inode %llu:%u, i_size %llu\n%s",
					i->inode.bi_inum, i->inode.bi_snapshot, i->inode.bi_size,
					(bch2_bkey_val_to_text(&buf, c, k), buf.buf))) {
				struct bkey_i *whiteout = bch2_trans_kmalloc(trans, sizeof(*whiteout));
				ret = PTR_ERR_OR_ZERO(whiteout);
				if (ret)
					goto err;

				bkey_init(&whiteout->k);
				whiteout->k.p = SPOS(k.k->p.inode,
						     last_block,
						     i->inode.bi_snapshot);
				bch2_key_resize(&whiteout->k,
						min(KEY_SIZE_MAX & (~0 << c->block_bits),
						    U64_MAX - whiteout->k.p.offset));


				/*
				 * Need a normal (not BTREE_ITER_all_snapshots)
				 * iterator, if we're deleting in a different
				 * snapshot and need to emit a whiteout
				 */
				struct btree_iter iter2;
				bch2_trans_iter_init(trans, &iter2, BTREE_ID_extents,
						     bkey_start_pos(&whiteout->k),
						     BTREE_ITER_intent);
				ret =   bch2_btree_iter_traverse(trans, &iter2) ?:
					bch2_trans_update(trans, &iter2, whiteout,
						BTREE_UPDATE_internal_snapshot_node);
				bch2_trans_iter_exit(trans, &iter2);
				if (ret)
					goto err;

				iter->k.type = KEY_TYPE_whiteout;
				break;
			}
		}
	}

	ret = bch2_trans_commit(trans, res, NULL, BCH_TRANS_COMMIT_no_enospc);
	if (ret)
		goto err;

	if (bkey_extent_is_allocation(k.k)) {
		for (struct inode_walker_entry *i = extent_i ?: &darray_last(inode->inodes);
		     inode->inodes.data && i >= inode->inodes.data;
		     --i) {
			if (i->whiteout ||
			    i->inode.bi_snapshot > k.k->p.snapshot ||
			    !key_visible_in_snapshot(c, s, i->inode.bi_snapshot, k.k->p.snapshot))
				continue;

			i->count += k.k->size;
		}
	}

	if (k.k->type != KEY_TYPE_whiteout) {
		ret = extent_ends_at(c, extent_ends, s, k);
		if (ret)
			goto err;
	}
out:
err:
fsck_err:
	printbuf_exit(&buf);
	bch_err_fn(c, ret);
	return ret;
}

/*
 * Walk extents: verify that extents have a corresponding S_ISREG inode, and
 * that i_size an i_sectors are consistent
 */
int bch2_check_extents(struct bch_fs *c)
{
	struct inode_walker w = inode_walker_init();
	struct snapshots_seen s;
	struct extent_ends extent_ends;
	struct disk_reservation res = { 0 };

	snapshots_seen_init(&s);
	extent_ends_init(&extent_ends);

	int ret = bch2_trans_run(c,
		for_each_btree_key(trans, iter, BTREE_ID_extents,
				POS(BCACHEFS_ROOT_INO, 0),
				BTREE_ITER_prefetch|BTREE_ITER_all_snapshots, k, ({
			bch2_disk_reservation_put(c, &res);
			check_extent(trans, &iter, k, &w, &s, &extent_ends, &res) ?:
			check_extent_overbig(trans, &iter, k);
		})) ?:
		check_i_sectors_notnested(trans, &w));

	bch2_disk_reservation_put(c, &res);
	extent_ends_exit(&extent_ends);
	inode_walker_exit(&w);
	snapshots_seen_exit(&s);

	bch_err_fn(c, ret);
	return ret;
}

int bch2_check_indirect_extents(struct bch_fs *c)
{
	struct disk_reservation res = { 0 };

	int ret = bch2_trans_run(c,
		for_each_btree_key_commit(trans, iter, BTREE_ID_reflink,
				POS_MIN,
				BTREE_ITER_prefetch, k,
				&res, NULL,
				BCH_TRANS_COMMIT_no_enospc, ({
			bch2_disk_reservation_put(c, &res);
			check_extent_overbig(trans, &iter, k);
		})));

	bch2_disk_reservation_put(c, &res);
	bch_err_fn(c, ret);
	return ret;
}

static int check_subdir_count_notnested(struct btree_trans *trans, struct inode_walker *w)
{
	struct bch_fs *c = trans->c;
	int ret = 0;
	s64 count2;

	darray_for_each(w->inodes, i) {
		if (i->inode.bi_nlink == i->count)
			continue;

		count2 = bch2_count_subdirs(trans, w->last_pos.inode, i->inode.bi_snapshot);
		if (count2 < 0)
			return count2;

		if (i->count != count2) {
			bch_err_ratelimited(c, "fsck counted subdirectories wrong for inum %llu:%u: got %llu should be %llu",
					    w->last_pos.inode, i->inode.bi_snapshot, i->count, count2);
			i->count = count2;
			if (i->inode.bi_nlink == i->count)
				continue;
		}

		if (i->inode.bi_nlink != i->count) {
			CLASS(printbuf, buf)();

			lockrestart_do(trans,
				       bch2_inum_snapshot_to_path(trans, w->last_pos.inode,
								  i->inode.bi_snapshot, NULL, &buf));

			if (fsck_err_on(i->inode.bi_nlink != i->count,
					trans, inode_dir_wrong_nlink,
					"directory with wrong i_nlink: got %u, should be %llu\n%s",
					i->inode.bi_nlink, i->count, buf.buf)) {
				i->inode.bi_nlink = i->count;
				ret = bch2_fsck_write_inode(trans, &i->inode);
				if (ret)
					break;
			}
		}
	}
fsck_err:
	bch_err_fn(c, ret);
	return ret;
}

static int check_subdir_dirents_count(struct btree_trans *trans, struct inode_walker *w)
{
	u32 restart_count = trans->restart_count;
	return check_subdir_count_notnested(trans, w) ?:
		trans_was_restarted(trans, restart_count);
}

/* find a subvolume that's a descendent of @snapshot: */
static int find_snapshot_subvol(struct btree_trans *trans, u32 snapshot, u32 *subvolid)
{
	struct btree_iter iter;
	struct bkey_s_c k;
	int ret;

	for_each_btree_key_norestart(trans, iter, BTREE_ID_subvolumes, POS_MIN, 0, k, ret) {
		if (k.k->type != KEY_TYPE_subvolume)
			continue;

		struct bkey_s_c_subvolume s = bkey_s_c_to_subvolume(k);
		if (bch2_snapshot_is_ancestor(trans->c, le32_to_cpu(s.v->snapshot), snapshot)) {
			bch2_trans_iter_exit(trans, &iter);
			*subvolid = k.k->p.offset;
			goto found;
		}
	}
	if (!ret)
		ret = -ENOENT;
found:
	bch2_trans_iter_exit(trans, &iter);
	return ret;
}

noinline_for_stack
static int check_dirent_to_subvol(struct btree_trans *trans, struct btree_iter *iter,
				  struct bkey_s_c_dirent d)
{
	struct bch_fs *c = trans->c;
	struct btree_iter subvol_iter = {};
	struct bch_inode_unpacked subvol_root;
	u32 parent_subvol = le32_to_cpu(d.v->d_parent_subvol);
	u32 target_subvol = le32_to_cpu(d.v->d_child_subvol);
	u32 parent_snapshot;
	u32 new_parent_subvol = 0;
	u64 parent_inum;
	struct printbuf buf = PRINTBUF;
	int ret = 0;

	ret = subvol_lookup(trans, parent_subvol, &parent_snapshot, &parent_inum);
	if (ret && !bch2_err_matches(ret, ENOENT))
		return ret;

	if (ret ||
	    (!ret && !bch2_snapshot_is_ancestor(c, parent_snapshot, d.k->p.snapshot))) {
		int ret2 = find_snapshot_subvol(trans, d.k->p.snapshot, &new_parent_subvol);
		if (ret2 && !bch2_err_matches(ret, ENOENT))
			return ret2;
	}

	if (ret &&
	    !new_parent_subvol &&
	    (c->sb.btrees_lost_data & BIT_ULL(BTREE_ID_subvolumes))) {
		/*
		 * Couldn't find a subvol for dirent's snapshot - but we lost
		 * subvols, so we need to reconstruct:
		 */
		ret = reconstruct_subvol(trans, d.k->p.snapshot, parent_subvol, 0);
		if (ret)
			return ret;

		parent_snapshot = d.k->p.snapshot;
	}

	if (fsck_err_on(ret,
			trans, dirent_to_missing_parent_subvol,
			"dirent parent_subvol points to missing subvolume\n%s",
			(bch2_bkey_val_to_text(&buf, c, d.s_c), buf.buf)) ||
	    fsck_err_on(!ret && !bch2_snapshot_is_ancestor(c, parent_snapshot, d.k->p.snapshot),
			trans, dirent_not_visible_in_parent_subvol,
			"dirent not visible in parent_subvol (not an ancestor of subvol snap %u)\n%s",
			parent_snapshot,
			(bch2_bkey_val_to_text(&buf, c, d.s_c), buf.buf))) {
		if (!new_parent_subvol) {
			bch_err(c, "could not find a subvol for snapshot %u", d.k->p.snapshot);
			return bch_err_throw(c, fsck_repair_unimplemented);
		}

		struct bkey_i_dirent *new_dirent = bch2_bkey_make_mut_typed(trans, iter, &d.s_c, 0, dirent);
		ret = PTR_ERR_OR_ZERO(new_dirent);
		if (ret)
			goto err;

		new_dirent->v.d_parent_subvol = cpu_to_le32(new_parent_subvol);
	}

	struct bkey_s_c_subvolume s =
		bch2_bkey_get_iter_typed(trans, &subvol_iter,
					 BTREE_ID_subvolumes, POS(0, target_subvol),
					 0, subvolume);
	ret = bkey_err(s.s_c);
	if (ret && !bch2_err_matches(ret, ENOENT))
		goto err;

	if (ret) {
		if (fsck_err(trans, dirent_to_missing_subvol,
			     "dirent points to missing subvolume\n%s",
			     (bch2_bkey_val_to_text(&buf, c, d.s_c), buf.buf)))
			return bch2_fsck_remove_dirent(trans, d.k->p);
		ret = 0;
		goto out;
	}

	if (le32_to_cpu(s.v->fs_path_parent) != parent_subvol) {
		printbuf_reset(&buf);

		prt_printf(&buf, "subvol with wrong fs_path_parent, should be be %u\n",
			   parent_subvol);

		ret = bch2_inum_to_path(trans, (subvol_inum) { s.k->p.offset,
					le64_to_cpu(s.v->inode) }, &buf);
		if (ret)
			goto err;
		prt_newline(&buf);
		bch2_bkey_val_to_text(&buf, c, s.s_c);

		if (fsck_err(trans, subvol_fs_path_parent_wrong, "%s", buf.buf)) {
			struct bkey_i_subvolume *n =
				bch2_bkey_make_mut_typed(trans, &subvol_iter, &s.s_c, 0, subvolume);
			ret = PTR_ERR_OR_ZERO(n);
			if (ret)
				goto err;

			n->v.fs_path_parent = cpu_to_le32(parent_subvol);
		}
	}

	u64 target_inum = le64_to_cpu(s.v->inode);
	u32 target_snapshot = le32_to_cpu(s.v->snapshot);

	ret = bch2_inode_find_by_inum_snapshot(trans, target_inum, target_snapshot,
					       &subvol_root, 0);
	if (ret && !bch2_err_matches(ret, ENOENT))
		goto err;

	if (ret) {
		bch_err(c, "subvol %u points to missing inode root %llu", target_subvol, target_inum);
		ret = bch_err_throw(c, fsck_repair_unimplemented);
		goto err;
	}

	if (fsck_err_on(!ret && parent_subvol != subvol_root.bi_parent_subvol,
			trans, inode_bi_parent_wrong,
			"subvol root %llu has wrong bi_parent_subvol: got %u, should be %u",
			target_inum,
			subvol_root.bi_parent_subvol, parent_subvol)) {
		subvol_root.bi_parent_subvol = parent_subvol;
		subvol_root.bi_snapshot = le32_to_cpu(s.v->snapshot);
		ret = __bch2_fsck_write_inode(trans, &subvol_root);
		if (ret)
			goto err;
	}

	ret = bch2_check_dirent_target(trans, iter, d, &subvol_root, true);
	if (ret)
		goto err;
out:
err:
fsck_err:
	bch2_trans_iter_exit(trans, &subvol_iter);
	printbuf_exit(&buf);
	return ret;
}

static int check_dirent(struct btree_trans *trans, struct btree_iter *iter,
			struct bkey_s_c k,
			struct bch_hash_info *hash_info,
			struct inode_walker *dir,
			struct inode_walker *target,
			struct snapshots_seen *s,
			bool *need_second_pass)
{
	struct bch_fs *c = trans->c;
	struct inode_walker_entry *i;
	struct printbuf buf = PRINTBUF;
	int ret = 0;

	ret = bch2_check_key_has_snapshot(trans, iter, k);
	if (ret) {
		ret = ret < 0 ? ret : 0;
		goto out;
	}

	ret = snapshots_seen_update(c, s, iter->btree_id, k.k->p);
	if (ret)
		goto err;

	if (k.k->type == KEY_TYPE_whiteout)
		goto out;

	if (dir->last_pos.inode != k.k->p.inode && dir->have_inodes) {
		ret = check_subdir_dirents_count(trans, dir);
		if (ret)
			goto err;
	}

	i = walk_inode(trans, dir, k);
	ret = PTR_ERR_OR_ZERO(i);
	if (ret < 0)
		goto err;

	ret = check_key_has_inode(trans, iter, dir, i, k);
	if (ret)
		goto err;

	if (!i || i->whiteout)
		goto out;

	if (dir->first_this_inode)
		*hash_info = bch2_hash_info_init(c, &i->inode);
	dir->first_this_inode = false;

#ifdef CONFIG_UNICODE
	hash_info->cf_encoding = bch2_inode_casefold(c, &i->inode) ? c->cf_encoding : NULL;
#endif

	ret = bch2_str_hash_check_key(trans, s, &bch2_dirent_hash_desc, hash_info,
				      iter, k, need_second_pass);
	if (ret < 0)
		goto err;
	if (ret) {
		/* dirent has been deleted */
		ret = 0;
		goto out;
	}

	if (k.k->type != KEY_TYPE_dirent)
		goto out;

	struct bkey_s_c_dirent d = bkey_s_c_to_dirent(k);

	/* check casefold */
	if (fsck_err_on(d.v->d_casefold != !!hash_info->cf_encoding,
			trans, dirent_casefold_mismatch,
			"dirent casefold does not match dir casefold\n%s",
			(printbuf_reset(&buf),
			 bch2_bkey_val_to_text(&buf, c, k),
			 buf.buf))) {
		subvol_inum dir_inum = { .subvol = d.v->d_type == DT_SUBVOL
				? le32_to_cpu(d.v->d_parent_subvol)
				: 0,
		};
		u64 target = d.v->d_type == DT_SUBVOL
			? le32_to_cpu(d.v->d_child_subvol)
			: le64_to_cpu(d.v->d_inum);
		struct qstr name = bch2_dirent_get_name(d);

		struct bkey_i_dirent *new_d =
			bch2_dirent_create_key(trans, hash_info, dir_inum,
					       d.v->d_type, &name, NULL, target);
		ret = PTR_ERR_OR_ZERO(new_d);
		if (ret)
			goto out;

		new_d->k.p.inode	= d.k->p.inode;
		new_d->k.p.snapshot	= d.k->p.snapshot;

		struct btree_iter dup_iter = {};
		ret =	bch2_hash_delete_at(trans,
					    bch2_dirent_hash_desc, hash_info, iter,
					    BTREE_UPDATE_internal_snapshot_node) ?:
			bch2_str_hash_repair_key(trans, s,
						 &bch2_dirent_hash_desc, hash_info,
						 iter, bkey_i_to_s_c(&new_d->k_i),
						 &dup_iter, bkey_s_c_null,
						 need_second_pass);
		goto out;
	}

	if (d.v->d_type == DT_SUBVOL) {
		ret = check_dirent_to_subvol(trans, iter, d);
		if (ret)
			goto err;
	} else {
		ret = get_visible_inodes(trans, target, s, le64_to_cpu(d.v->d_inum));
		if (ret)
			goto err;

		if (fsck_err_on(!target->inodes.nr,
				trans, dirent_to_missing_inode,
				"dirent points to missing inode:\n%s",
				(printbuf_reset(&buf),
				 bch2_bkey_val_to_text(&buf, c, k),
				 buf.buf))) {
			ret = bch2_fsck_remove_dirent(trans, d.k->p);
			if (ret)
				goto err;
		}

		darray_for_each(target->inodes, i) {
			ret = bch2_check_dirent_target(trans, iter, d, &i->inode, true);
			if (ret)
				goto err;
		}

		darray_for_each(target->deletes, i)
			if (fsck_err_on(!snapshot_list_has_id(&s->ids, *i),
					trans, dirent_to_overwritten_inode,
					"dirent points to inode overwritten in snapshot %u:\n%s",
					*i,
					(printbuf_reset(&buf),
					 bch2_bkey_val_to_text(&buf, c, k),
					 buf.buf))) {
				struct btree_iter delete_iter;
				bch2_trans_iter_init(trans, &delete_iter,
						     BTREE_ID_dirents,
						     SPOS(k.k->p.inode, k.k->p.offset, *i),
						     BTREE_ITER_intent);
				ret =   bch2_btree_iter_traverse(trans, &delete_iter) ?:
					bch2_hash_delete_at(trans, bch2_dirent_hash_desc,
							  hash_info,
							  &delete_iter,
							  BTREE_UPDATE_internal_snapshot_node);
				bch2_trans_iter_exit(trans, &delete_iter);
				if (ret)
					goto err;

			}
	}

	ret = bch2_trans_commit(trans, NULL, NULL, BCH_TRANS_COMMIT_no_enospc);
	if (ret)
		goto err;

	for_each_visible_inode(c, s, dir, d.k->p.snapshot, i) {
		if (d.v->d_type == DT_DIR)
			i->count++;
		i->i_size += bkey_bytes(d.k);
	}
out:
err:
fsck_err:
	printbuf_exit(&buf);
	return ret;
}

/*
 * Walk dirents: verify that they all have a corresponding S_ISDIR inode,
 * validate d_type
 */
int bch2_check_dirents(struct bch_fs *c)
{
	struct inode_walker dir = inode_walker_init();
	struct inode_walker target = inode_walker_init();
	struct snapshots_seen s;
	struct bch_hash_info hash_info;
	bool need_second_pass = false, did_second_pass = false;
	int ret;

	snapshots_seen_init(&s);
again:
	ret = bch2_trans_run(c,
		for_each_btree_key_commit(trans, iter, BTREE_ID_dirents,
				POS(BCACHEFS_ROOT_INO, 0),
				BTREE_ITER_prefetch|BTREE_ITER_all_snapshots, k,
				NULL, NULL, BCH_TRANS_COMMIT_no_enospc,
			check_dirent(trans, &iter, k, &hash_info, &dir, &target, &s,
				     &need_second_pass)) ?:
		check_subdir_count_notnested(trans, &dir));

	if (!ret && need_second_pass && !did_second_pass) {
		bch_info(c, "check_dirents requires second pass");
		swap(did_second_pass, need_second_pass);
		goto again;
	}

	if (!ret && need_second_pass) {
		bch_err(c, "dirents not repairing");
		ret = -EINVAL;
	}

	snapshots_seen_exit(&s);
	inode_walker_exit(&dir);
	inode_walker_exit(&target);
	bch_err_fn(c, ret);
	return ret;
}

static int check_xattr(struct btree_trans *trans, struct btree_iter *iter,
		       struct bkey_s_c k,
		       struct bch_hash_info *hash_info,
		       struct inode_walker *inode)
{
	struct bch_fs *c = trans->c;

	int ret = bch2_check_key_has_snapshot(trans, iter, k);
	if (ret < 0)
		return ret;
	if (ret)
		return 0;

	struct inode_walker_entry *i = walk_inode(trans, inode, k);
	ret = PTR_ERR_OR_ZERO(i);
	if (ret)
		return ret;

	ret = check_key_has_inode(trans, iter, inode, i, k);
	if (ret)
		return ret;

	if (!i || i->whiteout)
		return 0;

	if (inode->first_this_inode)
		*hash_info = bch2_hash_info_init(c, &i->inode);
	inode->first_this_inode = false;

	bool need_second_pass = false;
	return bch2_str_hash_check_key(trans, NULL, &bch2_xattr_hash_desc, hash_info,
				      iter, k, &need_second_pass);
}

/*
 * Walk xattrs: verify that they all have a corresponding inode
 */
int bch2_check_xattrs(struct bch_fs *c)
{
	struct inode_walker inode = inode_walker_init();
	struct bch_hash_info hash_info;
	int ret = 0;

	ret = bch2_trans_run(c,
		for_each_btree_key_commit(trans, iter, BTREE_ID_xattrs,
			POS(BCACHEFS_ROOT_INO, 0),
			BTREE_ITER_prefetch|BTREE_ITER_all_snapshots,
			k,
			NULL, NULL,
			BCH_TRANS_COMMIT_no_enospc,
		check_xattr(trans, &iter, k, &hash_info, &inode)));

	inode_walker_exit(&inode);
	bch_err_fn(c, ret);
	return ret;
}

static int check_root_trans(struct btree_trans *trans)
{
	struct bch_fs *c = trans->c;
	struct bch_inode_unpacked root_inode;
	u32 snapshot;
	u64 inum;
	int ret;

	ret = subvol_lookup(trans, BCACHEFS_ROOT_SUBVOL, &snapshot, &inum);
	if (ret && !bch2_err_matches(ret, ENOENT))
		return ret;

	if (mustfix_fsck_err_on(ret, trans, root_subvol_missing,
				"root subvol missing")) {
		struct bkey_i_subvolume *root_subvol =
			bch2_trans_kmalloc(trans, sizeof(*root_subvol));
		ret = PTR_ERR_OR_ZERO(root_subvol);
		if (ret)
			goto err;

		snapshot	= U32_MAX;
		inum		= BCACHEFS_ROOT_INO;

		bkey_subvolume_init(&root_subvol->k_i);
		root_subvol->k.p.offset = BCACHEFS_ROOT_SUBVOL;
		root_subvol->v.flags	= 0;
		root_subvol->v.snapshot	= cpu_to_le32(snapshot);
		root_subvol->v.inode	= cpu_to_le64(inum);
		ret = bch2_btree_insert_trans(trans, BTREE_ID_subvolumes, &root_subvol->k_i, 0);
		bch_err_msg(c, ret, "writing root subvol");
		if (ret)
			goto err;
	}

	ret = bch2_inode_find_by_inum_snapshot(trans, BCACHEFS_ROOT_INO, snapshot,
					       &root_inode, 0);
	if (ret && !bch2_err_matches(ret, ENOENT))
		return ret;

	if (mustfix_fsck_err_on(ret,
				trans, root_dir_missing,
				"root directory missing") ||
	    mustfix_fsck_err_on(!S_ISDIR(root_inode.bi_mode),
				trans, root_inode_not_dir,
				"root inode not a directory")) {
		bch2_inode_init(c, &root_inode, 0, 0, S_IFDIR|0755,
				0, NULL);
		root_inode.bi_inum = inum;
		root_inode.bi_snapshot = snapshot;

		ret = __bch2_fsck_write_inode(trans, &root_inode);
		bch_err_msg(c, ret, "writing root inode");
	}
err:
fsck_err:
	return ret;
}

/* Get root directory, create if it doesn't exist: */
int bch2_check_root(struct bch_fs *c)
{
	int ret = bch2_trans_commit_do(c, NULL, NULL, BCH_TRANS_COMMIT_no_enospc,
		check_root_trans(trans));
	bch_err_fn(c, ret);
	return ret;
}

static bool darray_u32_has(darray_u32 *d, u32 v)
{
	darray_for_each(*d, i)
		if (*i == v)
			return true;
	return false;
}

static int check_subvol_path(struct btree_trans *trans, struct btree_iter *iter, struct bkey_s_c k)
{
	struct bch_fs *c = trans->c;
	struct btree_iter parent_iter = {};
	darray_u32 subvol_path = {};
	struct printbuf buf = PRINTBUF;
	int ret = 0;

	if (k.k->type != KEY_TYPE_subvolume)
		return 0;

	subvol_inum start = {
		.subvol = k.k->p.offset,
		.inum	= le64_to_cpu(bkey_s_c_to_subvolume(k).v->inode),
	};

	while (k.k->p.offset != BCACHEFS_ROOT_SUBVOL) {
		ret = darray_push(&subvol_path, k.k->p.offset);
		if (ret)
			goto err;

		struct bkey_s_c_subvolume s = bkey_s_c_to_subvolume(k);

		struct bch_inode_unpacked subvol_root;
		ret = bch2_inode_find_by_inum_trans(trans,
					(subvol_inum) { s.k->p.offset, le64_to_cpu(s.v->inode) },
					&subvol_root);
		if (ret)
			break;

		u32 parent = le32_to_cpu(s.v->fs_path_parent);

		if (darray_u32_has(&subvol_path, parent)) {
			printbuf_reset(&buf);
			prt_printf(&buf, "subvolume loop: ");

			ret = bch2_inum_to_path(trans, start, &buf);
			if (ret)
				goto err;

			if (fsck_err(trans, subvol_loop, "%s", buf.buf))
				ret = reattach_subvol(trans, s);
			break;
		}

		bch2_trans_iter_exit(trans, &parent_iter);
		bch2_trans_iter_init(trans, &parent_iter,
				     BTREE_ID_subvolumes, POS(0, parent), 0);
		k = bch2_btree_iter_peek_slot(trans, &parent_iter);
		ret = bkey_err(k);
		if (ret)
			goto err;

		if (fsck_err_on(k.k->type != KEY_TYPE_subvolume,
				trans, subvol_unreachable,
				"unreachable subvolume %s",
				(printbuf_reset(&buf),
				 bch2_bkey_val_to_text(&buf, c, s.s_c),
				 buf.buf))) {
			ret = reattach_subvol(trans, s);
			break;
		}
	}
fsck_err:
err:
	printbuf_exit(&buf);
	darray_exit(&subvol_path);
	bch2_trans_iter_exit(trans, &parent_iter);
	return ret;
}

int bch2_check_subvolume_structure(struct bch_fs *c)
{
	int ret = bch2_trans_run(c,
		for_each_btree_key_commit(trans, iter,
				BTREE_ID_subvolumes, POS_MIN, BTREE_ITER_prefetch, k,
				NULL, NULL, BCH_TRANS_COMMIT_no_enospc,
			check_subvol_path(trans, &iter, k)));
	bch_err_fn(c, ret);
	return ret;
}

static int bch2_bi_depth_renumber_one(struct btree_trans *trans,
				      u64 inum, u32 snapshot,
				      u32 new_depth)
{
	struct btree_iter iter;
	struct bkey_s_c k = bch2_bkey_get_iter(trans, &iter, BTREE_ID_inodes,
					       SPOS(0, inum, snapshot), 0);

	struct bch_inode_unpacked inode;
	int ret = bkey_err(k) ?:
		!bkey_is_inode(k.k) ? -BCH_ERR_ENOENT_inode
		: bch2_inode_unpack(k, &inode);
	if (ret)
		goto err;

	if (inode.bi_depth != new_depth) {
		inode.bi_depth = new_depth;
		ret = __bch2_fsck_write_inode(trans, &inode) ?:
			bch2_trans_commit(trans, NULL, NULL, 0);
	}
err:
	bch2_trans_iter_exit(trans, &iter);
	return ret;
}

static int bch2_bi_depth_renumber(struct btree_trans *trans, darray_u64 *path,
				  u32 snapshot, u32 new_bi_depth)
{
	u32 restart_count = trans->restart_count;
	int ret = 0;

	darray_for_each_reverse(*path, i) {
		ret = nested_lockrestart_do(trans,
				bch2_bi_depth_renumber_one(trans, *i, snapshot, new_bi_depth));
		bch_err_fn(trans->c, ret);
		if (ret)
			break;

		new_bi_depth++;
	}

	return ret ?: trans_was_restarted(trans, restart_count);
}

static int check_path_loop(struct btree_trans *trans, struct bkey_s_c inode_k)
{
	struct bch_fs *c = trans->c;
	struct btree_iter inode_iter = {};
	darray_u64 path = {};
	struct printbuf buf = PRINTBUF;
	u32 snapshot = inode_k.k->p.snapshot;
	bool redo_bi_depth = false;
	u32 min_bi_depth = U32_MAX;
	int ret = 0;

	struct bpos start = inode_k.k->p;

	struct bch_inode_unpacked inode;
	ret = bch2_inode_unpack(inode_k, &inode);
	if (ret)
		return ret;

	/*
	 * If we're running full fsck, check_dirents() will have already ran,
	 * and we shouldn't see any missing backpointers here - otherwise that's
	 * handled separately, by check_unreachable_inodes
	 */
	while (!inode.bi_subvol &&
	       bch2_inode_has_backpointer(&inode)) {
		struct btree_iter dirent_iter;
		struct bkey_s_c_dirent d;

		d = dirent_get_by_pos(trans, &dirent_iter,
				      SPOS(inode.bi_dir, inode.bi_dir_offset, snapshot));
		ret = bkey_err(d.s_c);
		if (ret && !bch2_err_matches(ret, ENOENT))
			goto out;

		if (!ret && (ret = dirent_points_to_inode(c, d, &inode)))
			bch2_trans_iter_exit(trans, &dirent_iter);

		if (bch2_err_matches(ret, ENOENT)) {
			printbuf_reset(&buf);
			bch2_bkey_val_to_text(&buf, c, inode_k);
			bch_err(c, "unreachable inode in check_directory_structure: %s\n%s",
				bch2_err_str(ret), buf.buf);
			goto out;
		}

		bch2_trans_iter_exit(trans, &dirent_iter);

		ret = darray_push(&path, inode.bi_inum);
		if (ret)
			return ret;

		bch2_trans_iter_exit(trans, &inode_iter);
		inode_k = bch2_bkey_get_iter(trans, &inode_iter, BTREE_ID_inodes,
					     SPOS(0, inode.bi_dir, snapshot), 0);

		struct bch_inode_unpacked parent_inode;
		ret = bkey_err(inode_k) ?:
			!bkey_is_inode(inode_k.k) ? -BCH_ERR_ENOENT_inode
			: bch2_inode_unpack(inode_k, &parent_inode);
		if (ret) {
			/* Should have been caught in dirents pass */
			bch_err_msg(c, ret, "error looking up parent directory");
			goto out;
		}

		min_bi_depth = parent_inode.bi_depth;

		if (parent_inode.bi_depth < inode.bi_depth &&
		    min_bi_depth < U16_MAX)
			break;

		inode = parent_inode;
		redo_bi_depth = true;

		if (darray_find(path, inode.bi_inum)) {
			printbuf_reset(&buf);
			prt_printf(&buf, "directory structure loop in snapshot %u: ",
				   snapshot);

			ret = bch2_inum_snapshot_to_path(trans, start.offset, start.snapshot, NULL, &buf);
			if (ret)
				goto out;

			if (c->opts.verbose) {
				prt_newline(&buf);
				darray_for_each(path, i)
					prt_printf(&buf, "%llu ", *i);
			}

			if (fsck_err(trans, dir_loop, "%s", buf.buf)) {
				ret = remove_backpointer(trans, &inode);
				bch_err_msg(c, ret, "removing dirent");
				if (ret)
					break;

				ret = reattach_inode(trans, &inode);
				bch_err_msg(c, ret, "reattaching inode %llu", inode.bi_inum);
			}

			goto out;
		}
	}

	if (inode.bi_subvol)
		min_bi_depth = 0;

	if (redo_bi_depth)
		ret = bch2_bi_depth_renumber(trans, &path, snapshot, min_bi_depth);
out:
fsck_err:
	bch2_trans_iter_exit(trans, &inode_iter);
	darray_exit(&path);
	printbuf_exit(&buf);
	bch_err_fn(c, ret);
	return ret;
}

/*
 * Check for loops in the directory structure: all other connectivity issues
 * have been fixed by prior passes
 */
int bch2_check_directory_structure(struct bch_fs *c)
{
	int ret = bch2_trans_run(c,
		for_each_btree_key_reverse_commit(trans, iter, BTREE_ID_inodes, POS_MIN,
					  BTREE_ITER_intent|
					  BTREE_ITER_prefetch|
					  BTREE_ITER_all_snapshots, k,
					  NULL, NULL, BCH_TRANS_COMMIT_no_enospc, ({
			if (!S_ISDIR(bkey_inode_mode(k)))
				continue;

			if (bch2_inode_flags(k) & BCH_INODE_unlinked)
				continue;

			check_path_loop(trans, k);
		})));

	bch_err_fn(c, ret);
	return ret;
}

struct nlink_table {
	size_t		nr;
	size_t		size;

	struct nlink {
		u64	inum;
		u32	snapshot;
		u32	count;
	}		*d;
};

static int add_nlink(struct bch_fs *c, struct nlink_table *t,
		     u64 inum, u32 snapshot)
{
	if (t->nr == t->size) {
		size_t new_size = max_t(size_t, 128UL, t->size * 2);
		void *d = kvmalloc_array(new_size, sizeof(t->d[0]), GFP_KERNEL);

		if (!d) {
			bch_err(c, "fsck: error allocating memory for nlink_table, size %zu",
				new_size);
			return bch_err_throw(c, ENOMEM_fsck_add_nlink);
		}

		if (t->d)
			memcpy(d, t->d, t->size * sizeof(t->d[0]));
		kvfree(t->d);

		t->d = d;
		t->size = new_size;
	}


	t->d[t->nr++] = (struct nlink) {
		.inum		= inum,
		.snapshot	= snapshot,
	};

	return 0;
}

static int nlink_cmp(const void *_l, const void *_r)
{
	const struct nlink *l = _l;
	const struct nlink *r = _r;

	return cmp_int(l->inum, r->inum);
}

static void inc_link(struct bch_fs *c, struct snapshots_seen *s,
		     struct nlink_table *links,
		     u64 range_start, u64 range_end, u64 inum, u32 snapshot)
{
	struct nlink *link, key = {
		.inum = inum, .snapshot = U32_MAX,
	};

	if (inum < range_start || inum >= range_end)
		return;

	link = __inline_bsearch(&key, links->d, links->nr,
				sizeof(links->d[0]), nlink_cmp);
	if (!link)
		return;

	while (link > links->d && link[0].inum == link[-1].inum)
		--link;

	for (; link < links->d + links->nr && link->inum == inum; link++)
		if (ref_visible(c, s, snapshot, link->snapshot)) {
			link->count++;
			if (link->snapshot >= snapshot)
				break;
		}
}

noinline_for_stack
static int check_nlinks_find_hardlinks(struct bch_fs *c,
				       struct nlink_table *t,
				       u64 start, u64 *end)
{
	int ret = bch2_trans_run(c,
		for_each_btree_key(trans, iter, BTREE_ID_inodes,
				   POS(0, start),
				   BTREE_ITER_intent|
				   BTREE_ITER_prefetch|
				   BTREE_ITER_all_snapshots, k, ({
			if (!bkey_is_inode(k.k))
				continue;

			/* Should never fail, checked by bch2_inode_invalid: */
			struct bch_inode_unpacked u;
			_ret3 = bch2_inode_unpack(k, &u);
			if (_ret3)
				break;

			/*
			 * Backpointer and directory structure checks are sufficient for
			 * directories, since they can't have hardlinks:
			 */
			if (S_ISDIR(u.bi_mode))
				continue;

			/*
			 * Previous passes ensured that bi_nlink is nonzero if
			 * it had multiple hardlinks:
			 */
			if (!u.bi_nlink)
				continue;

			ret = add_nlink(c, t, k.k->p.offset, k.k->p.snapshot);
			if (ret) {
				*end = k.k->p.offset;
				ret = 0;
				break;
			}
			0;
		})));

	bch_err_fn(c, ret);
	return ret;
}

noinline_for_stack
static int check_nlinks_walk_dirents(struct bch_fs *c, struct nlink_table *links,
				     u64 range_start, u64 range_end)
{
	struct snapshots_seen s;

	snapshots_seen_init(&s);

	int ret = bch2_trans_run(c,
		for_each_btree_key(trans, iter, BTREE_ID_dirents, POS_MIN,
				   BTREE_ITER_intent|
				   BTREE_ITER_prefetch|
				   BTREE_ITER_all_snapshots, k, ({
			ret = snapshots_seen_update(c, &s, iter.btree_id, k.k->p);
			if (ret)
				break;

			if (k.k->type == KEY_TYPE_dirent) {
				struct bkey_s_c_dirent d = bkey_s_c_to_dirent(k);

				if (d.v->d_type != DT_DIR &&
				    d.v->d_type != DT_SUBVOL)
					inc_link(c, &s, links, range_start, range_end,
						 le64_to_cpu(d.v->d_inum), d.k->p.snapshot);
			}
			0;
		})));

	snapshots_seen_exit(&s);

	bch_err_fn(c, ret);
	return ret;
}

static int check_nlinks_update_inode(struct btree_trans *trans, struct btree_iter *iter,
				     struct bkey_s_c k,
				     struct nlink_table *links,
				     size_t *idx, u64 range_end)
{
	struct bch_inode_unpacked u;
	struct nlink *link = &links->d[*idx];
	int ret = 0;

	if (k.k->p.offset >= range_end)
		return 1;

	if (!bkey_is_inode(k.k))
		return 0;

	ret = bch2_inode_unpack(k, &u);
	if (ret)
		return ret;

	if (S_ISDIR(u.bi_mode))
		return 0;

	if (!u.bi_nlink)
		return 0;

	while ((cmp_int(link->inum, k.k->p.offset) ?:
		cmp_int(link->snapshot, k.k->p.snapshot)) < 0) {
		BUG_ON(*idx == links->nr);
		link = &links->d[++*idx];
	}

	if (fsck_err_on(bch2_inode_nlink_get(&u) != link->count,
			trans, inode_wrong_nlink,
			"inode %llu type %s has wrong i_nlink (%u, should be %u)",
			u.bi_inum, bch2_d_types[mode_to_type(u.bi_mode)],
			bch2_inode_nlink_get(&u), link->count)) {
		bch2_inode_nlink_set(&u, link->count);
		ret = __bch2_fsck_write_inode(trans, &u);
	}
fsck_err:
	return ret;
}

noinline_for_stack
static int check_nlinks_update_hardlinks(struct bch_fs *c,
			       struct nlink_table *links,
			       u64 range_start, u64 range_end)
{
	size_t idx = 0;

	int ret = bch2_trans_run(c,
		for_each_btree_key_commit(trans, iter, BTREE_ID_inodes,
				POS(0, range_start),
				BTREE_ITER_intent|BTREE_ITER_prefetch|BTREE_ITER_all_snapshots, k,
				NULL, NULL, BCH_TRANS_COMMIT_no_enospc,
			check_nlinks_update_inode(trans, &iter, k, links, &idx, range_end)));
	if (ret < 0) {
		bch_err(c, "error in fsck walking inodes: %s", bch2_err_str(ret));
		return ret;
	}

	return 0;
}

int bch2_check_nlinks(struct bch_fs *c)
{
	struct nlink_table links = { 0 };
	u64 this_iter_range_start, next_iter_range_start = 0;
	int ret = 0;

	do {
		this_iter_range_start = next_iter_range_start;
		next_iter_range_start = U64_MAX;

		ret = check_nlinks_find_hardlinks(c, &links,
						  this_iter_range_start,
						  &next_iter_range_start);

		ret = check_nlinks_walk_dirents(c, &links,
					  this_iter_range_start,
					  next_iter_range_start);
		if (ret)
			break;

		ret = check_nlinks_update_hardlinks(c, &links,
					 this_iter_range_start,
					 next_iter_range_start);
		if (ret)
			break;

		links.nr = 0;
	} while (next_iter_range_start != U64_MAX);

	kvfree(links.d);
	bch_err_fn(c, ret);
	return ret;
}

static int fix_reflink_p_key(struct btree_trans *trans, struct btree_iter *iter,
			     struct bkey_s_c k)
{
	struct bkey_s_c_reflink_p p;
	struct bkey_i_reflink_p *u;

	if (k.k->type != KEY_TYPE_reflink_p)
		return 0;

	p = bkey_s_c_to_reflink_p(k);

	if (!p.v->front_pad && !p.v->back_pad)
		return 0;

	u = bch2_trans_kmalloc(trans, sizeof(*u));
	int ret = PTR_ERR_OR_ZERO(u);
	if (ret)
		return ret;

	bkey_reassemble(&u->k_i, k);
	u->v.front_pad	= 0;
	u->v.back_pad	= 0;

	return bch2_trans_update(trans, iter, &u->k_i, BTREE_TRIGGER_norun);
}

int bch2_fix_reflink_p(struct bch_fs *c)
{
	if (c->sb.version >= bcachefs_metadata_version_reflink_p_fix)
		return 0;

	int ret = bch2_trans_run(c,
		for_each_btree_key_commit(trans, iter,
				BTREE_ID_extents, POS_MIN,
				BTREE_ITER_intent|BTREE_ITER_prefetch|
				BTREE_ITER_all_snapshots, k,
				NULL, NULL, BCH_TRANS_COMMIT_no_enospc,
			fix_reflink_p_key(trans, &iter, k)));
	bch_err_fn(c, ret);
	return ret;
}

#ifndef NO_BCACHEFS_CHARDEV

struct fsck_thread {
	struct thread_with_stdio thr;
	struct bch_fs		*c;
	struct bch_opts		opts;
};

static void bch2_fsck_thread_exit(struct thread_with_stdio *_thr)
{
	struct fsck_thread *thr = container_of(_thr, struct fsck_thread, thr);
	kfree(thr);
}

static int bch2_fsck_offline_thread_fn(struct thread_with_stdio *stdio)
{
	struct fsck_thread *thr = container_of(stdio, struct fsck_thread, thr);
	struct bch_fs *c = thr->c;

	int ret = PTR_ERR_OR_ZERO(c);
	if (ret)
		return ret;

	ret = bch2_fs_start(thr->c);
	if (ret)
		goto err;

	if (test_bit(BCH_FS_errors_fixed, &c->flags)) {
		bch2_stdio_redirect_printf(&stdio->stdio, false, "%s: errors fixed\n", c->name);
		ret |= 1;
	}
	if (test_bit(BCH_FS_error, &c->flags)) {
		bch2_stdio_redirect_printf(&stdio->stdio, false, "%s: still has errors\n", c->name);
		ret |= 4;
	}
err:
	bch2_fs_stop(c);
	return ret;
}

static const struct thread_with_stdio_ops bch2_offline_fsck_ops = {
	.exit		= bch2_fsck_thread_exit,
	.fn		= bch2_fsck_offline_thread_fn,
};

long bch2_ioctl_fsck_offline(struct bch_ioctl_fsck_offline __user *user_arg)
{
	struct bch_ioctl_fsck_offline arg;
	struct fsck_thread *thr = NULL;
	darray_const_str devs = {};
	long ret = 0;

	if (copy_from_user(&arg, user_arg, sizeof(arg)))
		return -EFAULT;

	if (arg.flags)
		return -EINVAL;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	for (size_t i = 0; i < arg.nr_devs; i++) {
		u64 dev_u64;
		ret = copy_from_user_errcode(&dev_u64, &user_arg->devs[i], sizeof(u64));
		if (ret)
			goto err;

		char *dev_str = strndup_user((char __user *)(unsigned long) dev_u64, PATH_MAX);
		ret = PTR_ERR_OR_ZERO(dev_str);
		if (ret)
			goto err;

		ret = darray_push(&devs, dev_str);
		if (ret) {
			kfree(dev_str);
			goto err;
		}
	}

	thr = kzalloc(sizeof(*thr), GFP_KERNEL);
	if (!thr) {
		ret = -ENOMEM;
		goto err;
	}

	thr->opts = bch2_opts_empty();

	if (arg.opts) {
		char *optstr = strndup_user((char __user *)(unsigned long) arg.opts, 1 << 16);
		ret =   PTR_ERR_OR_ZERO(optstr) ?:
			bch2_parse_mount_opts(NULL, &thr->opts, NULL, optstr, false);
		if (!IS_ERR(optstr))
			kfree(optstr);

		if (ret)
			goto err;
	}

	opt_set(thr->opts, stdio, (u64)(unsigned long)&thr->thr.stdio);
	opt_set(thr->opts, read_only, 1);
	opt_set(thr->opts, ratelimit_errors, 0);

	/* We need request_key() to be called before we punt to kthread: */
	opt_set(thr->opts, nostart, true);

	bch2_thread_with_stdio_init(&thr->thr, &bch2_offline_fsck_ops);

	thr->c = bch2_fs_open(&devs, &thr->opts);

	if (!IS_ERR(thr->c) &&
	    thr->c->opts.errors == BCH_ON_ERROR_panic)
		thr->c->opts.errors = BCH_ON_ERROR_ro;

	ret = __bch2_run_thread_with_stdio(&thr->thr);
out:
	darray_for_each(devs, i)
		kfree(*i);
	darray_exit(&devs);
	return ret;
err:
	if (thr)
		bch2_fsck_thread_exit(&thr->thr);
	pr_err("ret %s", bch2_err_str(ret));
	goto out;
}

static int bch2_fsck_online_thread_fn(struct thread_with_stdio *stdio)
{
	struct fsck_thread *thr = container_of(stdio, struct fsck_thread, thr);
	struct bch_fs *c = thr->c;

	c->stdio_filter = current;
	c->stdio = &thr->thr.stdio;

	/*
	 * XXX: can we figure out a way to do this without mucking with c->opts?
	 */
	unsigned old_fix_errors = c->opts.fix_errors;
	if (opt_defined(thr->opts, fix_errors))
		c->opts.fix_errors = thr->opts.fix_errors;
	else
		c->opts.fix_errors = FSCK_FIX_ask;

	c->opts.fsck = true;
	set_bit(BCH_FS_in_fsck, &c->flags);

	int ret = bch2_run_online_recovery_passes(c, ~0ULL);

	clear_bit(BCH_FS_in_fsck, &c->flags);
	bch_err_fn(c, ret);

	c->stdio = NULL;
	c->stdio_filter = NULL;
	c->opts.fix_errors = old_fix_errors;

	up(&c->recovery.run_lock);
	bch2_ro_ref_put(c);
	return ret;
}

static const struct thread_with_stdio_ops bch2_online_fsck_ops = {
	.exit		= bch2_fsck_thread_exit,
	.fn		= bch2_fsck_online_thread_fn,
};

long bch2_ioctl_fsck_online(struct bch_fs *c, struct bch_ioctl_fsck_online arg)
{
	struct fsck_thread *thr = NULL;
	long ret = 0;

	if (arg.flags)
		return -EINVAL;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (!bch2_ro_ref_tryget(c))
		return -EROFS;

	if (down_trylock(&c->recovery.run_lock)) {
		bch2_ro_ref_put(c);
		return -EAGAIN;
	}

	thr = kzalloc(sizeof(*thr), GFP_KERNEL);
	if (!thr) {
		ret = -ENOMEM;
		goto err;
	}

	thr->c = c;
	thr->opts = bch2_opts_empty();

	if (arg.opts) {
		char *optstr = strndup_user((char __user *)(unsigned long) arg.opts, 1 << 16);

		ret =   PTR_ERR_OR_ZERO(optstr) ?:
			bch2_parse_mount_opts(c, &thr->opts, NULL, optstr, false);
		if (!IS_ERR(optstr))
			kfree(optstr);

		if (ret)
			goto err;
	}

	ret = bch2_run_thread_with_stdio(&thr->thr, &bch2_online_fsck_ops);
err:
	if (ret < 0) {
		bch_err_fn(c, ret);
		if (thr)
			bch2_fsck_thread_exit(&thr->thr);
		up(&c->recovery.run_lock);
		bch2_ro_ref_put(c);
	}
	return ret;
}

#endif /* NO_BCACHEFS_CHARDEV */
