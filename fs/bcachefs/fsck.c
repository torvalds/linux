// SPDX-License-Identifier: GPL-2.0

#include "bcachefs.h"
#include "bkey_buf.h"
#include "btree_update.h"
#include "darray.h"
#include "dirent.h"
#include "error.h"
#include "fs-common.h"
#include "fsck.h"
#include "inode.h"
#include "keylist.h"
#include "subvolume.h"
#include "super.h"
#include "xattr.h"

#include <linux/bsearch.h>
#include <linux/dcache.h> /* struct qstr */

#define QSTR(n) { { { .len = strlen(n) } }, .name = n }

/*
 * XXX: this is handling transaction restarts without returning
 * -BCH_ERR_transaction_restart_nested, this is not how we do things anymore:
 */
static s64 bch2_count_inode_sectors(struct btree_trans *trans, u64 inum,
				    u32 snapshot)
{
	struct btree_iter iter;
	struct bkey_s_c k;
	u64 sectors = 0;
	int ret;

	for_each_btree_key(trans, iter, BTREE_ID_extents,
			   SPOS(inum, 0, snapshot), 0, k, ret) {
		if (k.k->p.inode != inum)
			break;

		if (bkey_extent_is_allocation(k.k))
			sectors += k.k->size;
	}

	bch2_trans_iter_exit(trans, &iter);

	return ret ?: sectors;
}

static s64 bch2_count_subdirs(struct btree_trans *trans, u64 inum,
				    u32 snapshot)
{
	struct btree_iter iter;
	struct bkey_s_c k;
	struct bkey_s_c_dirent d;
	u64 subdirs = 0;
	int ret;

	for_each_btree_key(trans, iter, BTREE_ID_dirents,
			   SPOS(inum, 0, snapshot), 0, k, ret) {
		if (k.k->p.inode != inum)
			break;

		if (k.k->type != KEY_TYPE_dirent)
			continue;

		d = bkey_s_c_to_dirent(k);
		if (d.v->d_type == DT_DIR)
			subdirs++;
	}

	bch2_trans_iter_exit(trans, &iter);

	return ret ?: subdirs;
}

static int __snapshot_lookup_subvol(struct btree_trans *trans, u32 snapshot,
				    u32 *subvol)
{
	struct btree_iter iter;
	struct bkey_s_c k;
	int ret;

	bch2_trans_iter_init(trans, &iter, BTREE_ID_snapshots,
			     POS(0, snapshot), 0);
	k = bch2_btree_iter_peek_slot(&iter);
	ret = bkey_err(k);
	if (ret)
		goto err;

	if (k.k->type != KEY_TYPE_snapshot) {
		bch_err(trans->c, "snapshot %u not fonud", snapshot);
		ret = -ENOENT;
		goto err;
	}

	*subvol = le32_to_cpu(bkey_s_c_to_snapshot(k).v->subvol);
err:
	bch2_trans_iter_exit(trans, &iter);
	return ret;

}

static int __subvol_lookup(struct btree_trans *trans, u32 subvol,
			   u32 *snapshot, u64 *inum)
{
	struct bch_subvolume s;
	int ret;

	ret = bch2_subvolume_get(trans, subvol, false, 0, &s);

	*snapshot = le32_to_cpu(s.snapshot);
	*inum = le64_to_cpu(s.inode);
	return ret;
}

static int subvol_lookup(struct btree_trans *trans, u32 subvol,
			 u32 *snapshot, u64 *inum)
{
	return lockrestart_do(trans, __subvol_lookup(trans, subvol, snapshot, inum));
}

static int lookup_first_inode(struct btree_trans *trans, u64 inode_nr,
			      struct bch_inode_unpacked *inode)
{
	struct btree_iter iter;
	struct bkey_s_c k;
	int ret;

	bch2_trans_iter_init(trans, &iter, BTREE_ID_inodes,
			     POS(0, inode_nr),
			     BTREE_ITER_ALL_SNAPSHOTS);
	k = bch2_btree_iter_peek(&iter);
	ret = bkey_err(k);
	if (ret)
		goto err;

	if (!k.k || !bkey_eq(k.k->p, POS(0, inode_nr))) {
		ret = -ENOENT;
		goto err;
	}

	ret = bch2_inode_unpack(k, inode);
err:
	if (ret && !bch2_err_matches(ret, BCH_ERR_transaction_restart))
		bch_err(trans->c, "error fetching inode %llu: %s",
			inode_nr, bch2_err_str(ret));
	bch2_trans_iter_exit(trans, &iter);
	return ret;
}

static int __lookup_inode(struct btree_trans *trans, u64 inode_nr,
			  struct bch_inode_unpacked *inode,
			  u32 *snapshot)
{
	struct btree_iter iter;
	struct bkey_s_c k;
	int ret;

	bch2_trans_iter_init(trans, &iter, BTREE_ID_inodes,
			     SPOS(0, inode_nr, *snapshot), 0);
	k = bch2_btree_iter_peek_slot(&iter);
	ret = bkey_err(k);
	if (ret)
		goto err;

	ret = bkey_is_inode(k.k)
		? bch2_inode_unpack(k, inode)
		: -ENOENT;
	if (!ret)
		*snapshot = iter.pos.snapshot;
err:
	if (ret && !bch2_err_matches(ret, BCH_ERR_transaction_restart))
		bch_err(trans->c, "error fetching inode %llu:%u: %s",
			inode_nr, *snapshot, bch2_err_str(ret));
	bch2_trans_iter_exit(trans, &iter);
	return ret;
}

static int lookup_inode(struct btree_trans *trans, u64 inode_nr,
			struct bch_inode_unpacked *inode,
			u32 *snapshot)
{
	return lockrestart_do(trans, __lookup_inode(trans, inode_nr, inode, snapshot));
}

static int __lookup_dirent(struct btree_trans *trans,
			   struct bch_hash_info hash_info,
			   subvol_inum dir, struct qstr *name,
			   u64 *target, unsigned *type)
{
	struct btree_iter iter;
	struct bkey_s_c_dirent d;
	int ret;

	ret = bch2_hash_lookup(trans, &iter, bch2_dirent_hash_desc,
			       &hash_info, dir, name, 0);
	if (ret)
		return ret;

	d = bkey_s_c_to_dirent(bch2_btree_iter_peek_slot(&iter));
	*target = le64_to_cpu(d.v->d_inum);
	*type = d.v->d_type;
	bch2_trans_iter_exit(trans, &iter);
	return 0;
}

static int __write_inode(struct btree_trans *trans,
			 struct bch_inode_unpacked *inode,
			 u32 snapshot)
{
	struct btree_iter iter;
	int ret;

	bch2_trans_iter_init(trans, &iter, BTREE_ID_inodes,
			    SPOS(0, inode->bi_inum, snapshot),
			    BTREE_ITER_INTENT);

	ret   = bch2_btree_iter_traverse(&iter) ?:
		bch2_inode_write(trans, &iter, inode);
	bch2_trans_iter_exit(trans, &iter);
	return ret;
}

static int write_inode(struct btree_trans *trans,
		       struct bch_inode_unpacked *inode,
		       u32 snapshot)
{
	int ret = commit_do(trans, NULL, NULL,
				  BTREE_INSERT_NOFAIL|
				  BTREE_INSERT_LAZY_RW,
				  __write_inode(trans, inode, snapshot));
	if (ret)
		bch_err(trans->c, "error in fsck: error updating inode: %s",
			bch2_err_str(ret));
	return ret;
}

static int fsck_inode_rm(struct btree_trans *trans, u64 inum, u32 snapshot)
{
	struct bch_fs *c = trans->c;
	struct btree_iter iter = { NULL };
	struct bkey_i_inode_generation delete;
	struct bch_inode_unpacked inode_u;
	struct bkey_s_c k;
	int ret;

	do {
		ret   = bch2_btree_delete_range_trans(trans, BTREE_ID_extents,
						      SPOS(inum, 0, snapshot),
						      SPOS(inum, U64_MAX, snapshot),
						      0, NULL) ?:
			bch2_btree_delete_range_trans(trans, BTREE_ID_dirents,
						      SPOS(inum, 0, snapshot),
						      SPOS(inum, U64_MAX, snapshot),
						      0, NULL) ?:
			bch2_btree_delete_range_trans(trans, BTREE_ID_xattrs,
						      SPOS(inum, 0, snapshot),
						      SPOS(inum, U64_MAX, snapshot),
						      0, NULL);
	} while (ret == -BCH_ERR_transaction_restart_nested);
	if (ret)
		goto err;
retry:
	bch2_trans_begin(trans);

	bch2_trans_iter_init(trans, &iter, BTREE_ID_inodes,
			     SPOS(0, inum, snapshot), BTREE_ITER_INTENT);
	k = bch2_btree_iter_peek_slot(&iter);

	ret = bkey_err(k);
	if (ret)
		goto err;

	if (!bkey_is_inode(k.k)) {
		bch2_fs_inconsistent(c,
				     "inode %llu:%u not found when deleting",
				     inum, snapshot);
		ret = -EIO;
		goto err;
	}

	bch2_inode_unpack(k, &inode_u);

	/* Subvolume root? */
	if (inode_u.bi_subvol)
		bch_warn(c, "deleting inode %llu marked as unlinked, but also a subvolume root!?", inode_u.bi_inum);

	bkey_inode_generation_init(&delete.k_i);
	delete.k.p = iter.pos;
	delete.v.bi_generation = cpu_to_le32(inode_u.bi_generation + 1);

	ret   = bch2_trans_update(trans, &iter, &delete.k_i, 0) ?:
		bch2_trans_commit(trans, NULL, NULL,
				BTREE_INSERT_NOFAIL);
err:
	bch2_trans_iter_exit(trans, &iter);
	if (bch2_err_matches(ret, BCH_ERR_transaction_restart))
		goto retry;

	return ret ?: -BCH_ERR_transaction_restart_nested;
}

static int __remove_dirent(struct btree_trans *trans, struct bpos pos)
{
	struct bch_fs *c = trans->c;
	struct btree_iter iter;
	struct bch_inode_unpacked dir_inode;
	struct bch_hash_info dir_hash_info;
	int ret;

	ret = lookup_first_inode(trans, pos.inode, &dir_inode);
	if (ret)
		goto err;

	dir_hash_info = bch2_hash_info_init(c, &dir_inode);

	bch2_trans_iter_init(trans, &iter, BTREE_ID_dirents, pos, BTREE_ITER_INTENT);

	ret = bch2_hash_delete_at(trans, bch2_dirent_hash_desc,
				  &dir_hash_info, &iter,
				  BTREE_UPDATE_INTERNAL_SNAPSHOT_NODE);
	bch2_trans_iter_exit(trans, &iter);
err:
	if (ret && !bch2_err_matches(ret, BCH_ERR_transaction_restart))
		bch_err(c, "%s(): error %s", __func__, bch2_err_str(ret));
	return ret;
}

/* Get lost+found, create if it doesn't exist: */
static int lookup_lostfound(struct btree_trans *trans, u32 subvol,
			    struct bch_inode_unpacked *lostfound)
{
	struct bch_fs *c = trans->c;
	struct bch_inode_unpacked root;
	struct bch_hash_info root_hash_info;
	struct qstr lostfound_str = QSTR("lost+found");
	subvol_inum root_inum = { .subvol = subvol };
	u64 inum = 0;
	unsigned d_type = 0;
	u32 snapshot;
	int ret;

	ret = __subvol_lookup(trans, subvol, &snapshot, &root_inum.inum);
	if (ret)
		return ret;

	ret = __lookup_inode(trans, root_inum.inum, &root, &snapshot);
	if (ret)
		return ret;

	root_hash_info = bch2_hash_info_init(c, &root);

	ret = __lookup_dirent(trans, root_hash_info, root_inum,
			    &lostfound_str, &inum, &d_type);
	if (ret == -ENOENT) {
		bch_notice(c, "creating lost+found");
		goto create_lostfound;
	}

	if (ret && !bch2_err_matches(ret, BCH_ERR_transaction_restart))
		bch_err(c, "error looking up lost+found: %s", bch2_err_str(ret));
	if (ret)
		return ret;

	if (d_type != DT_DIR) {
		bch_err(c, "error looking up lost+found: not a directory");
		return ret;
	}

	/*
	 * The check_dirents pass has already run, dangling dirents
	 * shouldn't exist here:
	 */
	return __lookup_inode(trans, inum, lostfound, &snapshot);

create_lostfound:
	bch2_inode_init_early(c, lostfound);

	ret = bch2_create_trans(trans, root_inum, &root,
				lostfound, &lostfound_str,
				0, 0, S_IFDIR|0700, 0, NULL, NULL,
				(subvol_inum) { }, 0);
	if (ret && !bch2_err_matches(ret, BCH_ERR_transaction_restart))
		bch_err(c, "error creating lost+found: %s", bch2_err_str(ret));
	return ret;
}

static int __reattach_inode(struct btree_trans *trans,
			  struct bch_inode_unpacked *inode,
			  u32 inode_snapshot)
{
	struct bch_hash_info dir_hash;
	struct bch_inode_unpacked lostfound;
	char name_buf[20];
	struct qstr name;
	u64 dir_offset = 0;
	u32 subvol;
	int ret;

	ret = __snapshot_lookup_subvol(trans, inode_snapshot, &subvol);
	if (ret)
		return ret;

	ret = lookup_lostfound(trans, subvol, &lostfound);
	if (ret)
		return ret;

	if (S_ISDIR(inode->bi_mode)) {
		lostfound.bi_nlink++;

		ret = __write_inode(trans, &lostfound, U32_MAX);
		if (ret)
			return ret;
	}

	dir_hash = bch2_hash_info_init(trans->c, &lostfound);

	snprintf(name_buf, sizeof(name_buf), "%llu", inode->bi_inum);
	name = (struct qstr) QSTR(name_buf);

	ret = bch2_dirent_create(trans,
				 (subvol_inum) {
					.subvol = subvol,
					.inum = lostfound.bi_inum,
				 },
				 &dir_hash,
				 inode_d_type(inode),
				 &name, inode->bi_inum, &dir_offset,
				 BCH_HASH_SET_MUST_CREATE);
	if (ret)
		return ret;

	inode->bi_dir		= lostfound.bi_inum;
	inode->bi_dir_offset	= dir_offset;

	return __write_inode(trans, inode, inode_snapshot);
}

static int reattach_inode(struct btree_trans *trans,
			  struct bch_inode_unpacked *inode,
			  u32 inode_snapshot)
{
	int ret = commit_do(trans, NULL, NULL,
				  BTREE_INSERT_LAZY_RW|
				  BTREE_INSERT_NOFAIL,
			__reattach_inode(trans, inode, inode_snapshot));
	if (ret) {
		bch_err(trans->c, "error reattaching inode %llu: %s",
			inode->bi_inum, bch2_err_str(ret));
		return ret;
	}

	return ret;
}

static int remove_backpointer(struct btree_trans *trans,
			      struct bch_inode_unpacked *inode)
{
	struct btree_iter iter;
	struct bkey_s_c k;
	int ret;

	bch2_trans_iter_init(trans, &iter, BTREE_ID_dirents,
			     POS(inode->bi_dir, inode->bi_dir_offset), 0);
	k = bch2_btree_iter_peek_slot(&iter);
	ret = bkey_err(k);
	if (ret)
		goto out;
	if (k.k->type != KEY_TYPE_dirent) {
		ret = -ENOENT;
		goto out;
	}

	ret = __remove_dirent(trans, k.k->p);
out:
	bch2_trans_iter_exit(trans, &iter);
	return ret;
}

struct snapshots_seen_entry {
	u32				id;
	u32				equiv;
};

struct snapshots_seen {
	struct bpos			pos;
	DARRAY(struct snapshots_seen_entry) ids;
};

static inline void snapshots_seen_exit(struct snapshots_seen *s)
{
	darray_exit(&s->ids);
}

static inline void snapshots_seen_init(struct snapshots_seen *s)
{
	memset(s, 0, sizeof(*s));
}

static int snapshots_seen_add(struct bch_fs *c, struct snapshots_seen *s, u32 id)
{
	struct snapshots_seen_entry *i, n = { id, id };
	int ret;

	darray_for_each(s->ids, i) {
		if (n.equiv < i->equiv)
			break;

		if (i->equiv == n.equiv) {
			bch_err(c, "%s(): adding duplicate snapshot", __func__);
			return -EINVAL;
		}
	}

	ret = darray_insert_item(&s->ids, i - s->ids.data, n);
	if (ret)
		bch_err(c, "error reallocating snapshots_seen table (size %zu)",
			s->ids.size);
	return ret;
}

static int snapshots_seen_update(struct bch_fs *c, struct snapshots_seen *s,
				 enum btree_id btree_id, struct bpos pos)
{
	struct snapshots_seen_entry *i, n = {
		.id	= pos.snapshot,
		.equiv	= bch2_snapshot_equiv(c, pos.snapshot),
	};
	int ret = 0;

	if (!bkey_eq(s->pos, pos))
		s->ids.nr = 0;

	pos.snapshot = n.equiv;
	s->pos = pos;

	darray_for_each(s->ids, i)
		if (i->equiv == n.equiv) {
			if (fsck_err_on(i->id != n.id, c,
					"snapshot deletion did not run correctly:\n"
					"  duplicate keys in btree %s at %llu:%llu snapshots %u, %u (equiv %u)\n",
					bch2_btree_ids[btree_id],
					pos.inode, pos.offset,
					i->id, n.id, n.equiv))
				return -BCH_ERR_need_snapshot_cleanup;

			return 0;
		}

	ret = darray_push(&s->ids, n);
	if (ret)
		bch_err(c, "error reallocating snapshots_seen table (size %zu)",
			s->ids.size);
fsck_err:
	return ret;
}

/**
 * key_visible_in_snapshot - returns true if @id is a descendent of @ancestor,
 * and @ancestor hasn't been overwritten in @seen
 *
 * That is, returns whether key in @ancestor snapshot is visible in @id snapshot
 */
static bool key_visible_in_snapshot(struct bch_fs *c, struct snapshots_seen *seen,
				    u32 id, u32 ancestor)
{
	ssize_t i;
	u32 top = seen->ids.nr ? seen->ids.data[seen->ids.nr - 1].equiv : 0;

	BUG_ON(id > ancestor);
	BUG_ON(!bch2_snapshot_is_equiv(c, id));
	BUG_ON(!bch2_snapshot_is_equiv(c, ancestor));

	/* @ancestor should be the snapshot most recently added to @seen */
	BUG_ON(ancestor != seen->pos.snapshot);
	BUG_ON(ancestor != top);

	if (id == ancestor)
		return true;

	if (!bch2_snapshot_is_ancestor(c, id, ancestor))
		return false;

	for (i = seen->ids.nr - 2;
	     i >= 0 && seen->ids.data[i].equiv >= id;
	     --i)
		if (bch2_snapshot_is_ancestor(c, id, seen->ids.data[i].equiv) &&
		    bch2_snapshot_is_ancestor(c, seen->ids.data[i].equiv, ancestor))
			return false;

	return true;
}

/**
 * ref_visible - given a key with snapshot id @src that points to a key with
 * snapshot id @dst, test whether there is some snapshot in which @dst is
 * visible.
 *
 * This assumes we're visiting @src keys in natural key order.
 *
 * @s	- list of snapshot IDs already seen at @src
 * @src	- snapshot ID of src key
 * @dst	- snapshot ID of dst key
 */
static int ref_visible(struct bch_fs *c, struct snapshots_seen *s,
		       u32 src, u32 dst)
{
	return dst <= src
		? key_visible_in_snapshot(c, s, dst, src)
		: bch2_snapshot_is_ancestor(c, src, dst);
}

#define for_each_visible_inode(_c, _s, _w, _snapshot, _i)				\
	for (_i = (_w)->inodes.data; _i < (_w)->inodes.data + (_w)->inodes.nr &&	\
	     (_i)->snapshot <= (_snapshot); _i++)					\
		if (key_visible_in_snapshot(_c, _s, _i->snapshot, _snapshot))

struct inode_walker_entry {
	struct bch_inode_unpacked inode;
	u32			snapshot;
	u64			count;
};

struct inode_walker {
	bool				first_this_inode;
	u64				cur_inum;

	DARRAY(struct inode_walker_entry) inodes;
};

static void inode_walker_exit(struct inode_walker *w)
{
	darray_exit(&w->inodes);
}

static struct inode_walker inode_walker_init(void)
{
	return (struct inode_walker) { 0, };
}

static int add_inode(struct bch_fs *c, struct inode_walker *w,
		     struct bkey_s_c inode)
{
	struct bch_inode_unpacked u;

	BUG_ON(bch2_inode_unpack(inode, &u));

	return darray_push(&w->inodes, ((struct inode_walker_entry) {
		.inode		= u,
		.snapshot	= bch2_snapshot_equiv(c, inode.k->p.snapshot),
	}));
}

static int __walk_inode(struct btree_trans *trans,
			struct inode_walker *w, struct bpos pos)
{
	struct bch_fs *c = trans->c;
	struct btree_iter iter;
	struct bkey_s_c k;
	u32 restart_count = trans->restart_count;
	unsigned i;
	int ret;

	pos.snapshot = bch2_snapshot_equiv(c, pos.snapshot);

	if (pos.inode == w->cur_inum) {
		w->first_this_inode = false;
		goto lookup_snapshot;
	}

	w->inodes.nr = 0;

	for_each_btree_key(trans, iter, BTREE_ID_inodes, POS(0, pos.inode),
			   BTREE_ITER_ALL_SNAPSHOTS, k, ret) {
		if (k.k->p.offset != pos.inode)
			break;

		if (bkey_is_inode(k.k))
			add_inode(c, w, k);
	}
	bch2_trans_iter_exit(trans, &iter);

	if (ret)
		return ret;

	w->cur_inum		= pos.inode;
	w->first_this_inode	= true;

	if (trans_was_restarted(trans, restart_count))
		return -BCH_ERR_transaction_restart_nested;

lookup_snapshot:
	for (i = 0; i < w->inodes.nr; i++)
		if (bch2_snapshot_is_ancestor(c, pos.snapshot, w->inodes.data[i].snapshot))
			goto found;
	return INT_MAX;
found:
	BUG_ON(pos.snapshot > w->inodes.data[i].snapshot);

	if (pos.snapshot != w->inodes.data[i].snapshot) {
		struct inode_walker_entry e = w->inodes.data[i];

		e.snapshot = pos.snapshot;
		e.count = 0;

		bch_info(c, "have key for inode %llu:%u but have inode in ancestor snapshot %u",
			 pos.inode, pos.snapshot, w->inodes.data[i].snapshot);

		while (i && w->inodes.data[i - 1].snapshot > pos.snapshot)
			--i;

		ret = darray_insert_item(&w->inodes, i, e);
		if (ret)
			return ret;
	}

	return i;
}

static int __get_visible_inodes(struct btree_trans *trans,
				struct inode_walker *w,
				struct snapshots_seen *s,
				u64 inum)
{
	struct bch_fs *c = trans->c;
	struct btree_iter iter;
	struct bkey_s_c k;
	int ret;

	w->inodes.nr = 0;

	for_each_btree_key_norestart(trans, iter, BTREE_ID_inodes, POS(0, inum),
			   BTREE_ITER_ALL_SNAPSHOTS, k, ret) {
		u32 equiv = bch2_snapshot_equiv(c, k.k->p.snapshot);

		if (k.k->p.offset != inum)
			break;

		if (!ref_visible(c, s, s->pos.snapshot, equiv))
			continue;

		if (bkey_is_inode(k.k))
			add_inode(c, w, k);

		if (equiv >= s->pos.snapshot)
			break;
	}
	bch2_trans_iter_exit(trans, &iter);

	return ret;
}

static int check_key_has_snapshot(struct btree_trans *trans,
				  struct btree_iter *iter,
				  struct bkey_s_c k)
{
	struct bch_fs *c = trans->c;
	struct printbuf buf = PRINTBUF;
	int ret = 0;

	if (mustfix_fsck_err_on(!bch2_snapshot_equiv(c, k.k->p.snapshot), c,
			"key in missing snapshot: %s",
			(bch2_bkey_val_to_text(&buf, c, k), buf.buf)))
		ret = bch2_btree_delete_at(trans, iter,
					    BTREE_UPDATE_INTERNAL_SNAPSHOT_NODE) ?: 1;
fsck_err:
	printbuf_exit(&buf);
	return ret;
}

static int hash_redo_key(struct btree_trans *trans,
			 const struct bch_hash_desc desc,
			 struct bch_hash_info *hash_info,
			 struct btree_iter *k_iter, struct bkey_s_c k)
{
	struct bkey_i *delete;
	struct bkey_i *tmp;

	delete = bch2_trans_kmalloc(trans, sizeof(*delete));
	if (IS_ERR(delete))
		return PTR_ERR(delete);

	tmp = bch2_bkey_make_mut(trans, k);
	if (IS_ERR(tmp))
		return PTR_ERR(tmp);

	bkey_init(&delete->k);
	delete->k.p = k_iter->pos;
	return  bch2_btree_iter_traverse(k_iter) ?:
		bch2_trans_update(trans, k_iter, delete, 0) ?:
		bch2_hash_set_snapshot(trans, desc, hash_info,
				       (subvol_inum) { 0, k.k->p.inode },
				       k.k->p.snapshot, tmp,
				       BCH_HASH_SET_MUST_CREATE,
				       BTREE_UPDATE_INTERNAL_SNAPSHOT_NODE) ?:
		bch2_trans_commit(trans, NULL, NULL,
				  BTREE_INSERT_NOFAIL|
				  BTREE_INSERT_LAZY_RW);
}

static int hash_check_key(struct btree_trans *trans,
			  const struct bch_hash_desc desc,
			  struct bch_hash_info *hash_info,
			  struct btree_iter *k_iter, struct bkey_s_c hash_k)
{
	struct bch_fs *c = trans->c;
	struct btree_iter iter = { NULL };
	struct printbuf buf = PRINTBUF;
	struct bkey_s_c k;
	u64 hash;
	int ret = 0;

	if (hash_k.k->type != desc.key_type)
		return 0;

	hash = desc.hash_bkey(hash_info, hash_k);

	if (likely(hash == hash_k.k->p.offset))
		return 0;

	if (hash_k.k->p.offset < hash)
		goto bad_hash;

	for_each_btree_key_norestart(trans, iter, desc.btree_id,
				     POS(hash_k.k->p.inode, hash),
				     BTREE_ITER_SLOTS, k, ret) {
		if (bkey_eq(k.k->p, hash_k.k->p))
			break;

		if (fsck_err_on(k.k->type == desc.key_type &&
				!desc.cmp_bkey(k, hash_k), c,
				"duplicate hash table keys:\n%s",
				(printbuf_reset(&buf),
				 bch2_bkey_val_to_text(&buf, c, hash_k),
				 buf.buf))) {
			ret = bch2_hash_delete_at(trans, desc, hash_info, k_iter, 0) ?: 1;
			break;
		}

		if (bkey_deleted(k.k)) {
			bch2_trans_iter_exit(trans, &iter);
			goto bad_hash;
		}
	}
out:
	bch2_trans_iter_exit(trans, &iter);
	printbuf_exit(&buf);
	return ret;
bad_hash:
	if (fsck_err(c, "hash table key at wrong offset: btree %s inode %llu offset %llu, hashed to %llu\n%s",
		     bch2_btree_ids[desc.btree_id], hash_k.k->p.inode, hash_k.k->p.offset, hash,
		     (printbuf_reset(&buf),
		      bch2_bkey_val_to_text(&buf, c, hash_k), buf.buf))) {
		ret = hash_redo_key(trans, desc, hash_info, k_iter, hash_k);
		if (ret) {
			bch_err(c, "hash_redo_key err %s", bch2_err_str(ret));
			return ret;
		}
		ret = -BCH_ERR_transaction_restart_nested;
	}
fsck_err:
	goto out;
}

static int check_inode(struct btree_trans *trans,
		       struct btree_iter *iter,
		       struct bkey_s_c k,
		       struct bch_inode_unpacked *prev,
		       struct snapshots_seen *s,
		       bool full)
{
	struct bch_fs *c = trans->c;
	struct bch_inode_unpacked u;
	bool do_update = false;
	int ret;

	ret = check_key_has_snapshot(trans, iter, k);
	if (ret < 0)
		goto err;
	if (ret)
		return 0;

	ret = snapshots_seen_update(c, s, iter->btree_id, k.k->p);
	if (ret)
		goto err;

	/*
	 * if snapshot id isn't a leaf node, skip it - deletion in
	 * particular is not atomic, so on the internal snapshot nodes
	 * we can see inodes marked for deletion after a clean shutdown
	 */
	if (bch2_snapshot_internal_node(c, k.k->p.snapshot))
		return 0;

	if (!bkey_is_inode(k.k))
		return 0;

	BUG_ON(bch2_inode_unpack(k, &u));

	if (!full &&
	    !(u.bi_flags & (BCH_INODE_I_SIZE_DIRTY|
			    BCH_INODE_I_SECTORS_DIRTY|
			    BCH_INODE_UNLINKED)))
		return 0;

	if (prev->bi_inum != u.bi_inum)
		*prev = u;

	if (fsck_err_on(prev->bi_hash_seed	!= u.bi_hash_seed ||
			inode_d_type(prev)	!= inode_d_type(&u), c,
			"inodes in different snapshots don't match")) {
		bch_err(c, "repair not implemented yet");
		return -EINVAL;
	}

	if (u.bi_flags & BCH_INODE_UNLINKED &&
	    (!c->sb.clean ||
	     fsck_err(c, "filesystem marked clean, but inode %llu unlinked",
		      u.bi_inum))) {
		bch2_trans_unlock(trans);
		bch2_fs_lazy_rw(c);

		ret = fsck_inode_rm(trans, u.bi_inum, iter->pos.snapshot);
		if (ret && !bch2_err_matches(ret, BCH_ERR_transaction_restart))
			bch_err(c, "error in fsck: error while deleting inode: %s",
				bch2_err_str(ret));
		return ret;
	}

	if (u.bi_flags & BCH_INODE_I_SIZE_DIRTY &&
	    (!c->sb.clean ||
	     fsck_err(c, "filesystem marked clean, but inode %llu has i_size dirty",
		      u.bi_inum))) {
		bch_verbose(c, "truncating inode %llu", u.bi_inum);

		bch2_trans_unlock(trans);
		bch2_fs_lazy_rw(c);

		/*
		 * XXX: need to truncate partial blocks too here - or ideally
		 * just switch units to bytes and that issue goes away
		 */
		ret = bch2_btree_delete_range_trans(trans, BTREE_ID_extents,
				SPOS(u.bi_inum, round_up(u.bi_size, block_bytes(c)) >> 9,
				     iter->pos.snapshot),
				POS(u.bi_inum, U64_MAX),
				0, NULL);
		if (ret) {
			bch_err(c, "error in fsck: error truncating inode: %s",
				bch2_err_str(ret));
			return ret;
		}

		/*
		 * We truncated without our normal sector accounting hook, just
		 * make sure we recalculate it:
		 */
		u.bi_flags |= BCH_INODE_I_SECTORS_DIRTY;

		u.bi_flags &= ~BCH_INODE_I_SIZE_DIRTY;
		do_update = true;
	}

	if (u.bi_flags & BCH_INODE_I_SECTORS_DIRTY &&
	    (!c->sb.clean ||
	     fsck_err(c, "filesystem marked clean, but inode %llu has i_sectors dirty",
		      u.bi_inum))) {
		s64 sectors;

		bch_verbose(c, "recounting sectors for inode %llu",
			    u.bi_inum);

		sectors = bch2_count_inode_sectors(trans, u.bi_inum, iter->pos.snapshot);
		if (sectors < 0) {
			bch_err(c, "error in fsck: error recounting inode sectors: %s",
				bch2_err_str(sectors));
			return sectors;
		}

		u.bi_sectors = sectors;
		u.bi_flags &= ~BCH_INODE_I_SECTORS_DIRTY;
		do_update = true;
	}

	if (u.bi_flags & BCH_INODE_BACKPTR_UNTRUSTED) {
		u.bi_dir = 0;
		u.bi_dir_offset = 0;
		u.bi_flags &= ~BCH_INODE_BACKPTR_UNTRUSTED;
		do_update = true;
	}

	if (do_update) {
		ret = __write_inode(trans, &u, iter->pos.snapshot);
		if (ret)
			bch_err(c, "error in fsck: error updating inode: %s",
				bch2_err_str(ret));
	}
err:
fsck_err:
	if (ret)
		bch_err(c, "%s(): error %s", __func__, bch2_err_str(ret));
	return ret;
}

noinline_for_stack
static int check_inodes(struct bch_fs *c, bool full)
{
	struct btree_trans trans;
	struct btree_iter iter;
	struct bch_inode_unpacked prev = { 0 };
	struct snapshots_seen s;
	struct bkey_s_c k;
	int ret;

	snapshots_seen_init(&s);
	bch2_trans_init(&trans, c, BTREE_ITER_MAX, 0);

	ret = for_each_btree_key_commit(&trans, iter, BTREE_ID_inodes,
			POS_MIN,
			BTREE_ITER_PREFETCH|BTREE_ITER_ALL_SNAPSHOTS, k,
			NULL, NULL, BTREE_INSERT_LAZY_RW|BTREE_INSERT_NOFAIL,
		check_inode(&trans, &iter, k, &prev, &s, full));

	bch2_trans_exit(&trans);
	snapshots_seen_exit(&s);
	if (ret)
		bch_err(c, "%s(): error %s", __func__, bch2_err_str(ret));
	return ret;
}

/*
 * Checking for overlapping extents needs to be reimplemented
 */
#if 0
static int fix_overlapping_extent(struct btree_trans *trans,
				       struct bkey_s_c k, struct bpos cut_at)
{
	struct btree_iter iter;
	struct bkey_i *u;
	int ret;

	u = bch2_trans_kmalloc(trans, bkey_bytes(k.k));
	ret = PTR_ERR_OR_ZERO(u);
	if (ret)
		return ret;

	bkey_reassemble(u, k);
	bch2_cut_front(cut_at, u);


	/*
	 * We don't want to go through the extent_handle_overwrites path:
	 *
	 * XXX: this is going to screw up disk accounting, extent triggers
	 * assume things about extent overwrites - we should be running the
	 * triggers manually here
	 */
	bch2_trans_iter_init(trans, &iter, BTREE_ID_extents, u->k.p,
			     BTREE_ITER_INTENT|BTREE_ITER_NOT_EXTENTS);

	BUG_ON(iter.flags & BTREE_ITER_IS_EXTENTS);
	ret   = bch2_btree_iter_traverse(&iter) ?:
		bch2_trans_update(trans, &iter, u, BTREE_TRIGGER_NORUN) ?:
		bch2_trans_commit(trans, NULL, NULL,
				  BTREE_INSERT_NOFAIL|
				  BTREE_INSERT_LAZY_RW);
	bch2_trans_iter_exit(trans, &iter);
	return ret;
}
#endif

static struct bkey_s_c_dirent dirent_get_by_pos(struct btree_trans *trans,
						struct btree_iter *iter,
						struct bpos pos)
{
	struct bkey_s_c k;
	int ret;

	bch2_trans_iter_init(trans, iter, BTREE_ID_dirents, pos, 0);
	k = bch2_btree_iter_peek_slot(iter);
	ret = bkey_err(k);
	if (!ret && k.k->type != KEY_TYPE_dirent)
		ret = -ENOENT;
	if (ret) {
		bch2_trans_iter_exit(trans, iter);
		return (struct bkey_s_c_dirent) { .k = ERR_PTR(ret) };
	}

	return bkey_s_c_to_dirent(k);
}

static bool inode_points_to_dirent(struct bch_inode_unpacked *inode,
				   struct bkey_s_c_dirent d)
{
	return  inode->bi_dir		== d.k->p.inode &&
		inode->bi_dir_offset	== d.k->p.offset;
}

static bool dirent_points_to_inode(struct bkey_s_c_dirent d,
				   struct bch_inode_unpacked *inode)
{
	return d.v->d_type == DT_SUBVOL
		? le32_to_cpu(d.v->d_child_subvol)	== inode->bi_subvol
		: le64_to_cpu(d.v->d_inum)		== inode->bi_inum;
}

static int inode_backpointer_exists(struct btree_trans *trans,
				    struct bch_inode_unpacked *inode,
				    u32 snapshot)
{
	struct btree_iter iter;
	struct bkey_s_c_dirent d;
	int ret;

	d = dirent_get_by_pos(trans, &iter,
			SPOS(inode->bi_dir, inode->bi_dir_offset, snapshot));
	ret = bkey_err(d.s_c);
	if (ret)
		return ret == -ENOENT ? 0 : ret;

	ret = dirent_points_to_inode(d, inode);
	bch2_trans_iter_exit(trans, &iter);
	return ret;
}

static int check_i_sectors(struct btree_trans *trans, struct inode_walker *w)
{
	struct bch_fs *c = trans->c;
	struct inode_walker_entry *i;
	u32 restart_count = trans->restart_count;
	int ret = 0;
	s64 count2;

	darray_for_each(w->inodes, i) {
		if (i->inode.bi_sectors == i->count)
			continue;

		count2 = bch2_count_inode_sectors(trans, w->cur_inum, i->snapshot);

		if (i->count != count2) {
			bch_err(c, "fsck counted i_sectors wrong: got %llu should be %llu",
				i->count, count2);
			i->count = count2;
			if (i->inode.bi_sectors == i->count)
				continue;
		}

		if (fsck_err_on(!(i->inode.bi_flags & BCH_INODE_I_SECTORS_DIRTY), c,
			    "inode %llu:%u has incorrect i_sectors: got %llu, should be %llu",
			    w->cur_inum, i->snapshot,
			    i->inode.bi_sectors, i->count)) {
			i->inode.bi_sectors = i->count;
			ret = write_inode(trans, &i->inode, i->snapshot);
			if (ret)
				break;
		}
	}
fsck_err:
	if (ret)
		bch_err(c, "%s(): error %s", __func__, bch2_err_str(ret));
	if (!ret && trans_was_restarted(trans, restart_count))
		ret = -BCH_ERR_transaction_restart_nested;
	return ret;
}

static int check_extent(struct btree_trans *trans, struct btree_iter *iter,
			struct bkey_s_c k,
			struct inode_walker *inode,
			struct snapshots_seen *s)
{
	struct bch_fs *c = trans->c;
	struct inode_walker_entry *i;
	struct printbuf buf = PRINTBUF;
	struct bpos equiv;
	int ret = 0;

	ret = check_key_has_snapshot(trans, iter, k);
	if (ret) {
		ret = ret < 0 ? ret : 0;
		goto out;
	}

	equiv = k.k->p;
	equiv.snapshot = bch2_snapshot_equiv(c, k.k->p.snapshot);

	ret = snapshots_seen_update(c, s, iter->btree_id, k.k->p);
	if (ret)
		goto err;

	if (k.k->type == KEY_TYPE_whiteout)
		goto out;

	if (inode->cur_inum != k.k->p.inode) {
		ret = check_i_sectors(trans, inode);
		if (ret)
			goto err;
	}

	BUG_ON(!iter->path->should_be_locked);
#if 0
	if (bkey_gt(prev.k->k.p, bkey_start_pos(k.k))) {
		char buf1[200];
		char buf2[200];

		bch2_bkey_val_to_text(&PBUF(buf1), c, bkey_i_to_s_c(prev.k));
		bch2_bkey_val_to_text(&PBUF(buf2), c, k);

		if (fsck_err(c, "overlapping extents:\n%s\n%s", buf1, buf2)) {
			ret = fix_overlapping_extent(trans, k, prev.k->k.p)
				?: -BCH_ERR_transaction_restart_nested;
			goto out;
		}
	}
#endif
	ret = __walk_inode(trans, inode, equiv);
	if (ret < 0)
		goto err;

	if (fsck_err_on(ret == INT_MAX, c,
			"extent in missing inode:\n  %s",
			(printbuf_reset(&buf),
			 bch2_bkey_val_to_text(&buf, c, k), buf.buf))) {
		ret = bch2_btree_delete_at(trans, iter,
					    BTREE_UPDATE_INTERNAL_SNAPSHOT_NODE);
		goto out;
	}

	if (ret == INT_MAX) {
		ret = 0;
		goto out;
	}

	i = inode->inodes.data + ret;
	ret = 0;

	if (fsck_err_on(!S_ISREG(i->inode.bi_mode) &&
			!S_ISLNK(i->inode.bi_mode), c,
			"extent in non regular inode mode %o:\n  %s",
			i->inode.bi_mode,
			(printbuf_reset(&buf),
			 bch2_bkey_val_to_text(&buf, c, k), buf.buf))) {
		ret = bch2_btree_delete_at(trans, iter,
					    BTREE_UPDATE_INTERNAL_SNAPSHOT_NODE);
		goto out;
	}

	/*
	 * Check inodes in reverse order, from oldest snapshots to newest, so
	 * that we emit the fewest number of whiteouts necessary:
	 */
	for (i = inode->inodes.data + inode->inodes.nr - 1;
	     i >= inode->inodes.data;
	     --i) {
		if (i->snapshot > equiv.snapshot ||
		    !key_visible_in_snapshot(c, s, i->snapshot, equiv.snapshot))
			continue;

		if (fsck_err_on(!(i->inode.bi_flags & BCH_INODE_I_SIZE_DIRTY) &&
				k.k->type != KEY_TYPE_reservation &&
				k.k->p.offset > round_up(i->inode.bi_size, block_bytes(c)) >> 9, c,
				"extent type past end of inode %llu:%u, i_size %llu\n  %s",
				i->inode.bi_inum, i->snapshot, i->inode.bi_size,
				(bch2_bkey_val_to_text(&buf, c, k), buf.buf))) {
			struct btree_iter iter2;

			bch2_trans_copy_iter(&iter2, iter);
			bch2_btree_iter_set_snapshot(&iter2, i->snapshot);
			ret =   bch2_btree_iter_traverse(&iter2) ?:
				bch2_btree_delete_at(trans, &iter2,
					BTREE_UPDATE_INTERNAL_SNAPSHOT_NODE);
			bch2_trans_iter_exit(trans, &iter2);
			if (ret)
				goto err;

			if (i->snapshot != equiv.snapshot) {
				ret = snapshots_seen_add(c, s, i->snapshot);
				if (ret)
					goto err;
			}
		}
	}

	if (bkey_extent_is_allocation(k.k))
		for_each_visible_inode(c, s, inode, equiv.snapshot, i)
			i->count += k.k->size;
#if 0
	bch2_bkey_buf_reassemble(&prev, c, k);
#endif

out:
err:
fsck_err:
	printbuf_exit(&buf);

	if (ret && !bch2_err_matches(ret, BCH_ERR_transaction_restart))
		bch_err(c, "%s(): error %s", __func__, bch2_err_str(ret));
	return ret;
}

/*
 * Walk extents: verify that extents have a corresponding S_ISREG inode, and
 * that i_size an i_sectors are consistent
 */
noinline_for_stack
static int check_extents(struct bch_fs *c)
{
	struct inode_walker w = inode_walker_init();
	struct snapshots_seen s;
	struct btree_trans trans;
	struct btree_iter iter;
	struct bkey_s_c k;
	int ret = 0;

#if 0
	struct bkey_buf prev;
	bch2_bkey_buf_init(&prev);
	prev.k->k = KEY(0, 0, 0);
#endif
	snapshots_seen_init(&s);
	bch2_trans_init(&trans, c, BTREE_ITER_MAX, 0);

	bch_verbose(c, "checking extents");

	ret = for_each_btree_key_commit(&trans, iter, BTREE_ID_extents,
			POS(BCACHEFS_ROOT_INO, 0),
			BTREE_ITER_PREFETCH|BTREE_ITER_ALL_SNAPSHOTS, k,
			NULL, NULL,
			BTREE_INSERT_LAZY_RW|BTREE_INSERT_NOFAIL,
		check_extent(&trans, &iter, k, &w, &s));
#if 0
	bch2_bkey_buf_exit(&prev, c);
#endif
	inode_walker_exit(&w);
	bch2_trans_exit(&trans);
	snapshots_seen_exit(&s);

	if (ret)
		bch_err(c, "%s(): error %s", __func__, bch2_err_str(ret));
	return ret;
}

static int check_subdir_count(struct btree_trans *trans, struct inode_walker *w)
{
	struct bch_fs *c = trans->c;
	struct inode_walker_entry *i;
	u32 restart_count = trans->restart_count;
	int ret = 0;
	s64 count2;

	darray_for_each(w->inodes, i) {
		if (i->inode.bi_nlink == i->count)
			continue;

		count2 = bch2_count_subdirs(trans, w->cur_inum, i->snapshot);
		if (count2 < 0)
			return count2;

		if (i->count != count2) {
			bch_err(c, "fsck counted subdirectories wrong: got %llu should be %llu",
				i->count, count2);
			i->count = count2;
			if (i->inode.bi_nlink == i->count)
				continue;
		}

		if (fsck_err_on(i->inode.bi_nlink != i->count, c,
				"directory %llu:%u with wrong i_nlink: got %u, should be %llu",
				w->cur_inum, i->snapshot, i->inode.bi_nlink, i->count)) {
			i->inode.bi_nlink = i->count;
			ret = write_inode(trans, &i->inode, i->snapshot);
			if (ret)
				break;
		}
	}
fsck_err:
	if (ret)
		bch_err(c, "%s(): error %s", __func__, bch2_err_str(ret));
	if (!ret && trans_was_restarted(trans, restart_count))
		ret = -BCH_ERR_transaction_restart_nested;
	return ret;
}

static int check_dirent_target(struct btree_trans *trans,
			       struct btree_iter *iter,
			       struct bkey_s_c_dirent d,
			       struct bch_inode_unpacked *target,
			       u32 target_snapshot)
{
	struct bch_fs *c = trans->c;
	struct bkey_i_dirent *n;
	bool backpointer_exists = true;
	struct printbuf buf = PRINTBUF;
	int ret = 0;

	if (!target->bi_dir &&
	    !target->bi_dir_offset) {
		target->bi_dir		= d.k->p.inode;
		target->bi_dir_offset	= d.k->p.offset;

		ret = __write_inode(trans, target, target_snapshot);
		if (ret)
			goto err;
	}

	if (!inode_points_to_dirent(target, d)) {
		ret = inode_backpointer_exists(trans, target, d.k->p.snapshot);
		if (ret < 0)
			goto err;

		backpointer_exists = ret;
		ret = 0;

		if (fsck_err_on(S_ISDIR(target->bi_mode) &&
				backpointer_exists, c,
				"directory %llu with multiple links",
				target->bi_inum)) {
			ret = __remove_dirent(trans, d.k->p);
			goto out;
		}

		if (fsck_err_on(backpointer_exists &&
				!target->bi_nlink, c,
				"inode %llu type %s has multiple links but i_nlink 0",
				target->bi_inum, bch2_d_types[d.v->d_type])) {
			target->bi_nlink++;
			target->bi_flags &= ~BCH_INODE_UNLINKED;

			ret = __write_inode(trans, target, target_snapshot);
			if (ret)
				goto err;
		}

		if (fsck_err_on(!backpointer_exists, c,
				"inode %llu:%u has wrong backpointer:\n"
				"got       %llu:%llu\n"
				"should be %llu:%llu",
				target->bi_inum, target_snapshot,
				target->bi_dir,
				target->bi_dir_offset,
				d.k->p.inode,
				d.k->p.offset)) {
			target->bi_dir		= d.k->p.inode;
			target->bi_dir_offset	= d.k->p.offset;

			ret = __write_inode(trans, target, target_snapshot);
			if (ret)
				goto err;
		}
	}

	if (fsck_err_on(d.v->d_type != inode_d_type(target), c,
			"incorrect d_type: got %s, should be %s:\n%s",
			bch2_d_type_str(d.v->d_type),
			bch2_d_type_str(inode_d_type(target)),
			(printbuf_reset(&buf),
			 bch2_bkey_val_to_text(&buf, c, d.s_c), buf.buf))) {
		n = bch2_trans_kmalloc(trans, bkey_bytes(d.k));
		ret = PTR_ERR_OR_ZERO(n);
		if (ret)
			goto err;

		bkey_reassemble(&n->k_i, d.s_c);
		n->v.d_type = inode_d_type(target);

		ret = bch2_trans_update(trans, iter, &n->k_i, 0);
		if (ret)
			goto err;

		d = dirent_i_to_s_c(n);
	}

	if (d.v->d_type == DT_SUBVOL &&
	    target->bi_parent_subvol != le32_to_cpu(d.v->d_parent_subvol) &&
	    (c->sb.version < bcachefs_metadata_version_subvol_dirent ||
	     fsck_err(c, "dirent has wrong d_parent_subvol field: got %u, should be %u",
		      le32_to_cpu(d.v->d_parent_subvol),
		      target->bi_parent_subvol))) {
		n = bch2_trans_kmalloc(trans, bkey_bytes(d.k));
		ret = PTR_ERR_OR_ZERO(n);
		if (ret)
			goto err;

		bkey_reassemble(&n->k_i, d.s_c);
		n->v.d_parent_subvol = cpu_to_le32(target->bi_parent_subvol);

		ret = bch2_trans_update(trans, iter, &n->k_i, 0);
		if (ret)
			goto err;

		d = dirent_i_to_s_c(n);
	}
out:
err:
fsck_err:
	printbuf_exit(&buf);

	if (ret && !bch2_err_matches(ret, BCH_ERR_transaction_restart))
		bch_err(c, "%s(): error %s", __func__, bch2_err_str(ret));
	return ret;
}

static int check_dirent(struct btree_trans *trans, struct btree_iter *iter,
			struct bkey_s_c k,
			struct bch_hash_info *hash_info,
			struct inode_walker *dir,
			struct inode_walker *target,
			struct snapshots_seen *s)
{
	struct bch_fs *c = trans->c;
	struct bkey_s_c_dirent d;
	struct inode_walker_entry *i;
	struct printbuf buf = PRINTBUF;
	struct bpos equiv;
	int ret = 0;

	ret = check_key_has_snapshot(trans, iter, k);
	if (ret) {
		ret = ret < 0 ? ret : 0;
		goto out;
	}

	equiv = k.k->p;
	equiv.snapshot = bch2_snapshot_equiv(c, k.k->p.snapshot);

	ret = snapshots_seen_update(c, s, iter->btree_id, k.k->p);
	if (ret)
		goto err;

	if (k.k->type == KEY_TYPE_whiteout)
		goto out;

	if (dir->cur_inum != k.k->p.inode) {
		ret = check_subdir_count(trans, dir);
		if (ret)
			goto err;
	}

	BUG_ON(!iter->path->should_be_locked);

	ret = __walk_inode(trans, dir, equiv);
	if (ret < 0)
		goto err;

	if (fsck_err_on(ret == INT_MAX, c,
			"dirent in nonexisting directory:\n%s",
			(printbuf_reset(&buf),
			 bch2_bkey_val_to_text(&buf, c, k), buf.buf))) {
		ret = bch2_btree_delete_at(trans, iter,
				BTREE_UPDATE_INTERNAL_SNAPSHOT_NODE);
		goto out;
	}

	if (ret == INT_MAX) {
		ret = 0;
		goto out;
	}

	i = dir->inodes.data + ret;
	ret = 0;

	if (fsck_err_on(!S_ISDIR(i->inode.bi_mode), c,
			"dirent in non directory inode type %s:\n%s",
			bch2_d_type_str(inode_d_type(&i->inode)),
			(printbuf_reset(&buf),
			 bch2_bkey_val_to_text(&buf, c, k), buf.buf))) {
		ret = bch2_btree_delete_at(trans, iter, 0);
		goto out;
	}

	if (dir->first_this_inode)
		*hash_info = bch2_hash_info_init(c, &dir->inodes.data[0].inode);

	ret = hash_check_key(trans, bch2_dirent_hash_desc,
			     hash_info, iter, k);
	if (ret < 0)
		goto err;
	if (ret) {
		/* dirent has been deleted */
		ret = 0;
		goto out;
	}

	if (k.k->type != KEY_TYPE_dirent)
		goto out;

	d = bkey_s_c_to_dirent(k);

	if (d.v->d_type == DT_SUBVOL) {
		struct bch_inode_unpacked subvol_root;
		u32 target_subvol = le32_to_cpu(d.v->d_child_subvol);
		u32 target_snapshot;
		u64 target_inum;

		ret = __subvol_lookup(trans, target_subvol,
				      &target_snapshot, &target_inum);
		if (ret && ret != -ENOENT)
			goto err;

		if (fsck_err_on(ret, c,
				"dirent points to missing subvolume %llu",
				le64_to_cpu(d.v->d_child_subvol))) {
			ret = __remove_dirent(trans, d.k->p);
			goto err;
		}

		ret = __lookup_inode(trans, target_inum,
				   &subvol_root, &target_snapshot);
		if (ret && ret != -ENOENT)
			goto err;

		if (fsck_err_on(ret, c,
				"subvolume %u points to missing subvolume root %llu",
				target_subvol,
				target_inum)) {
			bch_err(c, "repair not implemented yet");
			ret = -EINVAL;
			goto err;
		}

		if (fsck_err_on(subvol_root.bi_subvol != target_subvol, c,
				"subvol root %llu has wrong bi_subvol field: got %u, should be %u",
				target_inum,
				subvol_root.bi_subvol, target_subvol)) {
			subvol_root.bi_subvol = target_subvol;
			ret = __write_inode(trans, &subvol_root, target_snapshot);
			if (ret)
				goto err;
		}

		ret = check_dirent_target(trans, iter, d, &subvol_root,
					  target_snapshot);
		if (ret)
			goto err;
	} else {
		ret = __get_visible_inodes(trans, target, s, le64_to_cpu(d.v->d_inum));
		if (ret)
			goto err;

		if (fsck_err_on(!target->inodes.nr, c,
				"dirent points to missing inode: (equiv %u)\n%s",
				equiv.snapshot,
				(printbuf_reset(&buf),
				 bch2_bkey_val_to_text(&buf, c, k),
				 buf.buf))) {
			ret = __remove_dirent(trans, d.k->p);
			if (ret)
				goto err;
		}

		darray_for_each(target->inodes, i) {
			ret = check_dirent_target(trans, iter, d,
						  &i->inode, i->snapshot);
			if (ret)
				goto err;
		}
	}

	if (d.v->d_type == DT_DIR)
		for_each_visible_inode(c, s, dir, equiv.snapshot, i)
			i->count++;

out:
err:
fsck_err:
	printbuf_exit(&buf);

	if (ret && !bch2_err_matches(ret, BCH_ERR_transaction_restart))
		bch_err(c, "%s(): error %s", __func__, bch2_err_str(ret));
	return ret;
}

/*
 * Walk dirents: verify that they all have a corresponding S_ISDIR inode,
 * validate d_type
 */
noinline_for_stack
static int check_dirents(struct bch_fs *c)
{
	struct inode_walker dir = inode_walker_init();
	struct inode_walker target = inode_walker_init();
	struct snapshots_seen s;
	struct bch_hash_info hash_info;
	struct btree_trans trans;
	struct btree_iter iter;
	struct bkey_s_c k;
	int ret = 0;

	bch_verbose(c, "checking dirents");

	snapshots_seen_init(&s);
	bch2_trans_init(&trans, c, BTREE_ITER_MAX, 0);

	ret = for_each_btree_key_commit(&trans, iter, BTREE_ID_dirents,
			POS(BCACHEFS_ROOT_INO, 0),
			BTREE_ITER_PREFETCH|BTREE_ITER_ALL_SNAPSHOTS,
			k,
			NULL, NULL,
			BTREE_INSERT_LAZY_RW|BTREE_INSERT_NOFAIL,
		check_dirent(&trans, &iter, k, &hash_info, &dir, &target, &s));

	bch2_trans_exit(&trans);
	snapshots_seen_exit(&s);
	inode_walker_exit(&dir);
	inode_walker_exit(&target);

	if (ret)
		bch_err(c, "%s(): error %s", __func__, bch2_err_str(ret));
	return ret;
}

static int check_xattr(struct btree_trans *trans, struct btree_iter *iter,
		       struct bkey_s_c k,
		       struct bch_hash_info *hash_info,
		       struct inode_walker *inode)
{
	struct bch_fs *c = trans->c;
	int ret;

	ret = check_key_has_snapshot(trans, iter, k);
	if (ret)
		return ret;

	ret = __walk_inode(trans, inode, k.k->p);
	if (ret < 0)
		return ret;

	if (fsck_err_on(ret == INT_MAX, c,
			"xattr for missing inode %llu",
			k.k->p.inode))
		return bch2_btree_delete_at(trans, iter, 0);

	if (ret == INT_MAX)
		return 0;

	ret = 0;

	if (inode->first_this_inode)
		*hash_info = bch2_hash_info_init(c, &inode->inodes.data[0].inode);

	ret = hash_check_key(trans, bch2_xattr_hash_desc, hash_info, iter, k);
fsck_err:
	if (ret && !bch2_err_matches(ret, BCH_ERR_transaction_restart))
		bch_err(c, "%s(): error %s", __func__, bch2_err_str(ret));
	return ret;
}

/*
 * Walk xattrs: verify that they all have a corresponding inode
 */
noinline_for_stack
static int check_xattrs(struct bch_fs *c)
{
	struct inode_walker inode = inode_walker_init();
	struct bch_hash_info hash_info;
	struct btree_trans trans;
	struct btree_iter iter;
	struct bkey_s_c k;
	int ret = 0;

	bch_verbose(c, "checking xattrs");

	bch2_trans_init(&trans, c, BTREE_ITER_MAX, 0);

	ret = for_each_btree_key_commit(&trans, iter, BTREE_ID_xattrs,
			POS(BCACHEFS_ROOT_INO, 0),
			BTREE_ITER_PREFETCH|BTREE_ITER_ALL_SNAPSHOTS,
			k,
			NULL, NULL,
			BTREE_INSERT_LAZY_RW|BTREE_INSERT_NOFAIL,
		check_xattr(&trans, &iter, k, &hash_info, &inode));

	bch2_trans_exit(&trans);

	if (ret)
		bch_err(c, "%s(): error %s", __func__, bch2_err_str(ret));
	return ret;
}

static int check_root_trans(struct btree_trans *trans)
{
	struct bch_fs *c = trans->c;
	struct bch_inode_unpacked root_inode;
	u32 snapshot;
	u64 inum;
	int ret;

	ret = __subvol_lookup(trans, BCACHEFS_ROOT_SUBVOL, &snapshot, &inum);
	if (ret && ret != -ENOENT)
		return ret;

	if (mustfix_fsck_err_on(ret, c, "root subvol missing")) {
		struct bkey_i_subvolume root_subvol;

		snapshot	= U32_MAX;
		inum		= BCACHEFS_ROOT_INO;

		bkey_subvolume_init(&root_subvol.k_i);
		root_subvol.k.p.offset = BCACHEFS_ROOT_SUBVOL;
		root_subvol.v.flags	= 0;
		root_subvol.v.snapshot	= cpu_to_le32(snapshot);
		root_subvol.v.inode	= cpu_to_le64(inum);
		ret = commit_do(trans, NULL, NULL,
				      BTREE_INSERT_NOFAIL|
				      BTREE_INSERT_LAZY_RW,
			__bch2_btree_insert(trans, BTREE_ID_subvolumes, &root_subvol.k_i));
		if (ret) {
			bch_err(c, "error writing root subvol: %s", bch2_err_str(ret));
			goto err;
		}

	}

	ret = __lookup_inode(trans, BCACHEFS_ROOT_INO, &root_inode, &snapshot);
	if (ret && ret != -ENOENT)
		return ret;

	if (mustfix_fsck_err_on(ret, c, "root directory missing") ||
	    mustfix_fsck_err_on(!S_ISDIR(root_inode.bi_mode), c,
				"root inode not a directory")) {
		bch2_inode_init(c, &root_inode, 0, 0, S_IFDIR|0755,
				0, NULL);
		root_inode.bi_inum = inum;

		ret = __write_inode(trans, &root_inode, snapshot);
		if (ret)
			bch_err(c, "error writing root inode: %s", bch2_err_str(ret));
	}
err:
fsck_err:
	return ret;
}

/* Get root directory, create if it doesn't exist: */
noinline_for_stack
static int check_root(struct bch_fs *c)
{
	bch_verbose(c, "checking root directory");

	return bch2_trans_do(c, NULL, NULL,
			     BTREE_INSERT_NOFAIL|
			     BTREE_INSERT_LAZY_RW,
		check_root_trans(&trans));
}

struct pathbuf_entry {
	u64	inum;
	u32	snapshot;
};

typedef DARRAY(struct pathbuf_entry) pathbuf;

static bool path_is_dup(pathbuf *p, u64 inum, u32 snapshot)
{
	struct pathbuf_entry *i;

	darray_for_each(*p, i)
		if (i->inum	== inum &&
		    i->snapshot	== snapshot)
			return true;

	return false;
}

static int path_down(struct bch_fs *c, pathbuf *p,
		     u64 inum, u32 snapshot)
{
	int ret = darray_push(p, ((struct pathbuf_entry) {
		.inum		= inum,
		.snapshot	= snapshot,
	}));

	if (ret)
		bch_err(c, "fsck: error allocating memory for pathbuf, size %zu",
			p->size);
	return ret;
}

/*
 * Check that a given inode is reachable from the root:
 *
 * XXX: we should also be verifying that inodes are in the right subvolumes
 */
static int check_path(struct btree_trans *trans,
		      pathbuf *p,
		      struct bch_inode_unpacked *inode,
		      u32 snapshot)
{
	struct bch_fs *c = trans->c;
	int ret = 0;

	snapshot = bch2_snapshot_equiv(c, snapshot);
	p->nr = 0;

	while (!(inode->bi_inum == BCACHEFS_ROOT_INO &&
		 inode->bi_subvol == BCACHEFS_ROOT_SUBVOL)) {
		struct btree_iter dirent_iter;
		struct bkey_s_c_dirent d;
		u32 parent_snapshot = snapshot;

		if (inode->bi_subvol) {
			u64 inum;

			ret = subvol_lookup(trans, inode->bi_parent_subvol,
					    &parent_snapshot, &inum);
			if (ret)
				break;
		}

		ret = lockrestart_do(trans,
			PTR_ERR_OR_ZERO((d = dirent_get_by_pos(trans, &dirent_iter,
					  SPOS(inode->bi_dir, inode->bi_dir_offset,
					       parent_snapshot))).k));
		if (ret && ret != -ENOENT)
			break;

		if (!ret && !dirent_points_to_inode(d, inode)) {
			bch2_trans_iter_exit(trans, &dirent_iter);
			ret = -ENOENT;
		}

		if (ret == -ENOENT) {
			if (fsck_err(c,  "unreachable inode %llu:%u, type %s nlink %u backptr %llu:%llu",
				     inode->bi_inum, snapshot,
				     bch2_d_type_str(inode_d_type(inode)),
				     inode->bi_nlink,
				     inode->bi_dir,
				     inode->bi_dir_offset))
				ret = reattach_inode(trans, inode, snapshot);
			break;
		}

		bch2_trans_iter_exit(trans, &dirent_iter);

		if (!S_ISDIR(inode->bi_mode))
			break;

		ret = path_down(c, p, inode->bi_inum, snapshot);
		if (ret) {
			bch_err(c, "memory allocation failure");
			return ret;
		}

		snapshot = parent_snapshot;

		ret = lookup_inode(trans, inode->bi_dir, inode, &snapshot);
		if (ret) {
			/* Should have been caught in dirents pass */
			bch_err(c, "error looking up parent directory: %i", ret);
			break;
		}

		if (path_is_dup(p, inode->bi_inum, snapshot)) {
			struct pathbuf_entry *i;

			/* XXX print path */
			bch_err(c, "directory structure loop");

			darray_for_each(*p, i)
				pr_err("%llu:%u", i->inum, i->snapshot);
			pr_err("%llu:%u", inode->bi_inum, snapshot);

			if (!fsck_err(c, "directory structure loop"))
				return 0;

			ret = commit_do(trans, NULL, NULL,
					      BTREE_INSERT_NOFAIL|
					      BTREE_INSERT_LAZY_RW,
					remove_backpointer(trans, inode));
			if (ret) {
				bch_err(c, "error removing dirent: %i", ret);
				break;
			}

			ret = reattach_inode(trans, inode, snapshot);
		}
	}
fsck_err:
	if (ret)
		bch_err(c, "%s: err %s", __func__, bch2_err_str(ret));
	return ret;
}

/*
 * Check for unreachable inodes, as well as loops in the directory structure:
 * After check_dirents(), if an inode backpointer doesn't exist that means it's
 * unreachable:
 */
noinline_for_stack
static int check_directory_structure(struct bch_fs *c)
{
	struct btree_trans trans;
	struct btree_iter iter;
	struct bkey_s_c k;
	struct bch_inode_unpacked u;
	pathbuf path = { 0, };
	int ret;

	bch2_trans_init(&trans, c, BTREE_ITER_MAX, 0);

	for_each_btree_key(&trans, iter, BTREE_ID_inodes, POS_MIN,
			   BTREE_ITER_INTENT|
			   BTREE_ITER_PREFETCH|
			   BTREE_ITER_ALL_SNAPSHOTS, k, ret) {
		if (!bkey_is_inode(k.k))
			continue;

		ret = bch2_inode_unpack(k, &u);
		if (ret) {
			/* Should have been caught earlier in fsck: */
			bch_err(c, "error unpacking inode %llu: %i", k.k->p.offset, ret);
			break;
		}

		if (u.bi_flags & BCH_INODE_UNLINKED)
			continue;

		ret = check_path(&trans, &path, &u, iter.pos.snapshot);
		if (ret)
			break;
	}
	bch2_trans_iter_exit(&trans, &iter);

	darray_exit(&path);

	bch2_trans_exit(&trans);
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
			return -ENOMEM;
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

	return cmp_int(l->inum, r->inum) ?: cmp_int(l->snapshot, r->snapshot);
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
	struct btree_trans trans;
	struct btree_iter iter;
	struct bkey_s_c k;
	struct bch_inode_unpacked u;
	int ret = 0;

	bch2_trans_init(&trans, c, BTREE_ITER_MAX, 0);

	for_each_btree_key(&trans, iter, BTREE_ID_inodes,
			   POS(0, start),
			   BTREE_ITER_INTENT|
			   BTREE_ITER_PREFETCH|
			   BTREE_ITER_ALL_SNAPSHOTS, k, ret) {
		if (!bkey_is_inode(k.k))
			continue;

		/* Should never fail, checked by bch2_inode_invalid: */
		BUG_ON(bch2_inode_unpack(k, &u));

		/*
		 * Backpointer and directory structure checks are sufficient for
		 * directories, since they can't have hardlinks:
		 */
		if (S_ISDIR(le16_to_cpu(u.bi_mode)))
			continue;

		if (!u.bi_nlink)
			continue;

		ret = add_nlink(c, t, k.k->p.offset, k.k->p.snapshot);
		if (ret) {
			*end = k.k->p.offset;
			ret = 0;
			break;
		}

	}
	bch2_trans_iter_exit(&trans, &iter);
	bch2_trans_exit(&trans);

	if (ret)
		bch_err(c, "error in fsck: btree error %i while walking inodes", ret);

	return ret;
}

noinline_for_stack
static int check_nlinks_walk_dirents(struct bch_fs *c, struct nlink_table *links,
				     u64 range_start, u64 range_end)
{
	struct btree_trans trans;
	struct snapshots_seen s;
	struct btree_iter iter;
	struct bkey_s_c k;
	struct bkey_s_c_dirent d;
	int ret;

	snapshots_seen_init(&s);

	bch2_trans_init(&trans, c, BTREE_ITER_MAX, 0);

	for_each_btree_key(&trans, iter, BTREE_ID_dirents, POS_MIN,
			   BTREE_ITER_INTENT|
			   BTREE_ITER_PREFETCH|
			   BTREE_ITER_ALL_SNAPSHOTS, k, ret) {
		ret = snapshots_seen_update(c, &s, iter.btree_id, k.k->p);
		if (ret)
			break;

		switch (k.k->type) {
		case KEY_TYPE_dirent:
			d = bkey_s_c_to_dirent(k);

			if (d.v->d_type != DT_DIR &&
			    d.v->d_type != DT_SUBVOL)
				inc_link(c, &s, links, range_start, range_end,
					 le64_to_cpu(d.v->d_inum),
					 bch2_snapshot_equiv(c, d.k->p.snapshot));
			break;
		}
	}
	bch2_trans_iter_exit(&trans, &iter);

	if (ret)
		bch_err(c, "error in fsck: btree error %i while walking dirents", ret);

	bch2_trans_exit(&trans);
	snapshots_seen_exit(&s);
	return ret;
}

static int check_nlinks_update_inode(struct btree_trans *trans, struct btree_iter *iter,
				     struct bkey_s_c k,
				     struct nlink_table *links,
				     size_t *idx, u64 range_end)
{
	struct bch_fs *c = trans->c;
	struct bch_inode_unpacked u;
	struct nlink *link = &links->d[*idx];
	int ret = 0;

	if (k.k->p.offset >= range_end)
		return 1;

	if (!bkey_is_inode(k.k))
		return 0;

	BUG_ON(bch2_inode_unpack(k, &u));

	if (S_ISDIR(le16_to_cpu(u.bi_mode)))
		return 0;

	if (!u.bi_nlink)
		return 0;

	while ((cmp_int(link->inum, k.k->p.offset) ?:
		cmp_int(link->snapshot, k.k->p.snapshot)) < 0) {
		BUG_ON(*idx == links->nr);
		link = &links->d[++*idx];
	}

	if (fsck_err_on(bch2_inode_nlink_get(&u) != link->count, c,
			"inode %llu type %s has wrong i_nlink (%u, should be %u)",
			u.bi_inum, bch2_d_types[mode_to_type(u.bi_mode)],
			bch2_inode_nlink_get(&u), link->count)) {
		bch2_inode_nlink_set(&u, link->count);
		ret = __write_inode(trans, &u, k.k->p.snapshot);
	}
fsck_err:
	return ret;
}

noinline_for_stack
static int check_nlinks_update_hardlinks(struct bch_fs *c,
			       struct nlink_table *links,
			       u64 range_start, u64 range_end)
{
	struct btree_trans trans;
	struct btree_iter iter;
	struct bkey_s_c k;
	size_t idx = 0;
	int ret = 0;

	bch2_trans_init(&trans, c, BTREE_ITER_MAX, 0);

	ret = for_each_btree_key_commit(&trans, iter, BTREE_ID_inodes,
			POS(0, range_start),
			BTREE_ITER_INTENT|BTREE_ITER_PREFETCH|BTREE_ITER_ALL_SNAPSHOTS, k,
			NULL, NULL, BTREE_INSERT_LAZY_RW|BTREE_INSERT_NOFAIL,
		check_nlinks_update_inode(&trans, &iter, k, links, &idx, range_end));

	bch2_trans_exit(&trans);

	if (ret < 0) {
		bch_err(c, "error in fsck: btree error %i while walking inodes", ret);
		return ret;
	}

	return 0;
}

noinline_for_stack
static int check_nlinks(struct bch_fs *c)
{
	struct nlink_table links = { 0 };
	u64 this_iter_range_start, next_iter_range_start = 0;
	int ret = 0;

	bch_verbose(c, "checking inode nlinks");

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

	return ret;
}

static int fix_reflink_p_key(struct btree_trans *trans, struct btree_iter *iter,
			     struct bkey_s_c k)
{
	struct bkey_s_c_reflink_p p;
	struct bkey_i_reflink_p *u;
	int ret;

	if (k.k->type != KEY_TYPE_reflink_p)
		return 0;

	p = bkey_s_c_to_reflink_p(k);

	if (!p.v->front_pad && !p.v->back_pad)
		return 0;

	u = bch2_trans_kmalloc(trans, sizeof(*u));
	ret = PTR_ERR_OR_ZERO(u);
	if (ret)
		return ret;

	bkey_reassemble(&u->k_i, k);
	u->v.front_pad	= 0;
	u->v.back_pad	= 0;

	return bch2_trans_update(trans, iter, &u->k_i, BTREE_TRIGGER_NORUN);
}

noinline_for_stack
static int fix_reflink_p(struct bch_fs *c)
{
	struct btree_trans trans;
	struct btree_iter iter;
	struct bkey_s_c k;
	int ret;

	if (c->sb.version >= bcachefs_metadata_version_reflink_p_fix)
		return 0;

	bch_verbose(c, "fixing reflink_p keys");

	bch2_trans_init(&trans, c, BTREE_ITER_MAX, 0);

	ret = for_each_btree_key_commit(&trans, iter,
			BTREE_ID_extents, POS_MIN,
			BTREE_ITER_INTENT|BTREE_ITER_PREFETCH|BTREE_ITER_ALL_SNAPSHOTS, k,
			NULL, NULL, BTREE_INSERT_NOFAIL|BTREE_INSERT_LAZY_RW,
		fix_reflink_p_key(&trans, &iter, k));

	bch2_trans_exit(&trans);
	return ret;
}

/*
 * Checks for inconsistencies that shouldn't happen, unless we have a bug.
 * Doesn't fix them yet, mainly because they haven't yet been observed:
 */
int bch2_fsck_full(struct bch_fs *c)
{
	int ret;
again:
	ret =   bch2_fs_check_snapshots(c) ?:
		bch2_fs_check_subvols(c) ?:
		bch2_delete_dead_snapshots(c) ?:
		check_inodes(c, true) ?:
		check_extents(c) ?:
		check_dirents(c) ?:
		check_xattrs(c) ?:
		check_root(c) ?:
		check_directory_structure(c) ?:
		check_nlinks(c) ?:
		fix_reflink_p(c);

	if (bch2_err_matches(ret, BCH_ERR_need_snapshot_cleanup)) {
		set_bit(BCH_FS_HAVE_DELETED_SNAPSHOTS, &c->flags);
		goto again;
	}

	return ret;
}

int bch2_fsck_walk_inodes_only(struct bch_fs *c)
{
	return  bch2_fs_check_snapshots(c) ?:
		bch2_fs_check_subvols(c) ?:
		bch2_delete_dead_snapshots(c) ?:
		check_inodes(c, false);
}
