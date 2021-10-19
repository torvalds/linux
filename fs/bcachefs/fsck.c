// SPDX-License-Identifier: GPL-2.0

#include "bcachefs.h"
#include "bkey_buf.h"
#include "btree_update.h"
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

static int snapshot_lookup_subvol(struct btree_trans *trans, u32 snapshot,
				  u32 *subvol)
{
	return lockrestart_do(trans, __snapshot_lookup_subvol(trans, snapshot, subvol));
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

	*snapshot = iter.pos.snapshot;
	ret = k.k->type == KEY_TYPE_inode
		? bch2_inode_unpack(bkey_s_c_to_inode(k), inode)
		: -ENOENT;
err:
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

static int lookup_dirent(struct btree_trans *trans,
			 struct bch_hash_info hash_info,
			 subvol_inum dir, struct qstr *name,
			 u64 *target, unsigned *type)
{
	return lockrestart_do(trans,
		__lookup_dirent(trans, hash_info, dir, name, target, type));
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
	int ret = __bch2_trans_do(trans, NULL, NULL,
				  BTREE_INSERT_NOFAIL|
				  BTREE_INSERT_LAZY_RW,
				  __write_inode(trans, inode, snapshot));
	if (ret)
		bch_err(trans->c, "error in fsck: error %i updating inode", ret);
	return ret;
}

static int fsck_inode_rm(struct btree_trans *trans, u64 inum, u32 snapshot)
{
	struct btree_iter iter = { NULL };
	struct bkey_i_inode_generation delete;
	struct bch_inode_unpacked inode_u;
	struct bkey_s_c k;
	int ret;

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

	if (k.k->type != KEY_TYPE_inode) {
		bch2_fs_inconsistent(trans->c,
				     "inode %llu:%u not found when deleting",
				     inum, snapshot);
		ret = -EIO;
		goto err;
	}

	bch2_inode_unpack(bkey_s_c_to_inode(k), &inode_u);

	/* Subvolume root? */
	if (inode_u.bi_subvol) {
		ret = bch2_subvolume_delete(trans, inode_u.bi_subvol, -1);
		if (ret)
			goto err;
	}

	bkey_inode_generation_init(&delete.k_i);
	delete.k.p = iter.pos;
	delete.v.bi_generation = cpu_to_le32(inode_u.bi_generation + 1);

	ret   = bch2_trans_update(trans, &iter, &delete.k_i, 0) ?:
		bch2_trans_commit(trans, NULL, NULL,
				BTREE_INSERT_NOFAIL);
err:
	bch2_trans_iter_exit(trans, &iter);
	if (ret == -EINTR)
		goto retry;

	return ret;
}

static int __remove_dirent(struct btree_trans *trans, struct bpos pos)
{
	struct bch_fs *c = trans->c;
	struct btree_iter iter;
	struct bch_inode_unpacked dir_inode;
	struct bch_hash_info dir_hash_info;
	int ret;

	ret = lookup_inode(trans, pos.inode, &dir_inode, NULL);
	if (ret)
		return ret;

	dir_hash_info = bch2_hash_info_init(c, &dir_inode);

	bch2_trans_iter_init(trans, &iter, BTREE_ID_dirents, pos, BTREE_ITER_INTENT);

	ret = bch2_hash_delete_at(trans, bch2_dirent_hash_desc,
				  &dir_hash_info, &iter, 0);
	bch2_trans_iter_exit(trans, &iter);
	return ret;
}

static int remove_dirent(struct btree_trans *trans, struct bpos pos)
{
	int ret = __bch2_trans_do(trans, NULL, NULL,
				  BTREE_INSERT_NOFAIL|
				  BTREE_INSERT_LAZY_RW,
				  __remove_dirent(trans, pos));
	if (ret)
		bch_err(trans->c, "remove_dirent: err %i deleting dirent", ret);
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

	ret = subvol_lookup(trans, subvol, &snapshot, &root_inum.inum);
	if (ret)
		return ret;

	ret = lookup_inode(trans, root_inum.inum, &root, &snapshot);
	if (ret) {
		bch_err(c, "error fetching subvol root: %i", ret);
		return ret;
	}

	root_hash_info = bch2_hash_info_init(c, &root);

	ret = lookup_dirent(trans, root_hash_info, root_inum,
			    &lostfound_str, &inum, &d_type);
	if (ret == -ENOENT) {
		bch_notice(c, "creating lost+found");
		goto create_lostfound;
	}

	if (ret) {
		bch_err(c, "error looking up lost+found: %i", ret);
		return ret;
	}

	if (d_type != DT_DIR) {
		bch_err(c, "error looking up lost+found: not a directory");
		return ret;

	}

	ret = lookup_inode(trans, inum, lostfound, &snapshot);
	if (ret && ret != -ENOENT) {
		/*
		 * The check_dirents pass has already run, dangling dirents
		 * shouldn't exist here:
		 */
		bch_err(c, "error looking up lost+found: %i", ret);
		return ret;
	}

	if (ret == -ENOENT) {
create_lostfound:
		bch2_inode_init_early(c, lostfound);

		ret = __bch2_trans_do(trans, NULL, NULL,
				      BTREE_INSERT_NOFAIL|
				      BTREE_INSERT_LAZY_RW,
			bch2_create_trans(trans, root_inum, &root,
					  lostfound, &lostfound_str,
					  0, 0, S_IFDIR|0700, 0, NULL, NULL,
					  (subvol_inum) { }, 0));
		if (ret)
			bch_err(c, "error creating lost+found: %i", ret);
	}

	return 0;
}

static int reattach_inode(struct btree_trans *trans,
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

	ret = snapshot_lookup_subvol(trans, inode_snapshot, &subvol);
	if (ret)
		return ret;

	ret = lookup_lostfound(trans, subvol, &lostfound);
	if (ret)
		return ret;

	if (S_ISDIR(inode->bi_mode)) {
		lostfound.bi_nlink++;

		ret = write_inode(trans, &lostfound, U32_MAX);
		if (ret)
			return ret;
	}

	dir_hash = bch2_hash_info_init(trans->c, &lostfound);

	snprintf(name_buf, sizeof(name_buf), "%llu", inode->bi_inum);
	name = (struct qstr) QSTR(name_buf);

	ret = __bch2_trans_do(trans, NULL, NULL, BTREE_INSERT_LAZY_RW,
			bch2_dirent_create(trans,
					   (subvol_inum) {
						.subvol = subvol,
						.inum = lostfound.bi_inum,
					   },
					   &dir_hash,
					   mode_to_type(inode->bi_mode),
					   &name, inode->bi_inum, &dir_offset,
					   BCH_HASH_SET_MUST_CREATE));
	if (ret) {
		bch_err(trans->c, "error %i reattaching inode %llu",
			ret, inode->bi_inum);
		return ret;
	}

	inode->bi_dir		= lostfound.bi_inum;
	inode->bi_dir_offset	= dir_offset;

	return write_inode(trans, inode, inode_snapshot);
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

	ret = remove_dirent(trans, k.k->p);
out:
	bch2_trans_iter_exit(trans, &iter);
	return ret;
}

static int snapshots_seen_update(struct bch_fs *c, struct snapshots_seen *s, struct bpos pos)
{
	pos.snapshot = snapshot_t(c, pos.snapshot)->equiv;

	if (bkey_cmp(s->pos, pos))
		s->nr = 0;
	s->pos = pos;

	/* Might get called multiple times due to lock restarts */
	if (s->nr && s->d[s->nr - 1] == pos.snapshot)
		return 0;

	return snapshots_seen_add(c, s, pos.snapshot);
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

	BUG_ON(id > ancestor);

	id		= snapshot_t(c, id)->equiv;
	ancestor	= snapshot_t(c, ancestor)->equiv;

	/* @ancestor should be the snapshot most recently added to @seen */
	BUG_ON(!seen->nr || seen->d[seen->nr - 1] != ancestor);
	BUG_ON(seen->pos.snapshot != ancestor);

	if (id == ancestor)
		return true;

	if (!bch2_snapshot_is_ancestor(c, id, ancestor))
		return false;

	for (i = seen->nr - 2;
	     i >= 0 && seen->d[i] >= id;
	     --i)
		if (bch2_snapshot_is_ancestor(c, id, seen->d[i]) &&
		    bch2_snapshot_is_ancestor(c, seen->d[i], ancestor))
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

#define for_each_visible_inode(_c, _s, _w, _snapshot, _i)	\
	for (_i = (_w)->d; _i < (_w)->d + (_w)->nr && (_i)->snapshot <= (_snapshot); _i++)\
		if (key_visible_in_snapshot(_c, _s, _i->snapshot, _snapshot))

struct inode_walker {
	bool				first_this_inode;
	u64				cur_inum;

	size_t				nr;
	size_t				size;
	struct inode_walker_entry {
		struct bch_inode_unpacked inode;
		u32			snapshot;
		u64			count;
	} *d;
};

static void inode_walker_exit(struct inode_walker *w)
{
	kfree(w->d);
	w->d = NULL;
}

static struct inode_walker inode_walker_init(void)
{
	return (struct inode_walker) { 0, };
}

static int inode_walker_realloc(struct inode_walker *w)
{
	if (w->nr == w->size) {
		size_t new_size = max_t(size_t, 8UL, w->size * 2);
		void *d = krealloc(w->d, new_size * sizeof(w->d[0]),
				   GFP_KERNEL);
		if (!d)
			return -ENOMEM;

		w->d = d;
		w->size = new_size;
	}

	return 0;
}

static int add_inode(struct bch_fs *c, struct inode_walker *w,
		     struct bkey_s_c_inode inode)
{
	struct bch_inode_unpacked u;
	int ret;

	ret = inode_walker_realloc(w);
	if (ret)
		return ret;

	BUG_ON(bch2_inode_unpack(inode, &u));

	w->d[w->nr++] = (struct inode_walker_entry) {
		.inode		= u,
		.snapshot	= snapshot_t(c, inode.k->p.snapshot)->equiv,
	};

	return 0;
}

static int __walk_inode(struct btree_trans *trans,
			struct inode_walker *w, struct bpos pos)
{
	struct bch_fs *c = trans->c;
	struct btree_iter iter;
	struct bkey_s_c k;
	unsigned i, ancestor_pos;
	int ret;

	pos.snapshot = snapshot_t(c, pos.snapshot)->equiv;

	if (pos.inode == w->cur_inum) {
		w->first_this_inode = false;
		goto lookup_snapshot;
	}

	w->nr = 0;

	for_each_btree_key(trans, iter, BTREE_ID_inodes, POS(0, pos.inode),
			   BTREE_ITER_ALL_SNAPSHOTS, k, ret) {
		if (k.k->p.offset != pos.inode)
			break;

		if (k.k->type == KEY_TYPE_inode)
			add_inode(c, w, bkey_s_c_to_inode(k));
	}
	bch2_trans_iter_exit(trans, &iter);

	if (ret)
		return ret;

	w->cur_inum		= pos.inode;
	w->first_this_inode	= true;
lookup_snapshot:
	for (i = 0; i < w->nr; i++)
		if (bch2_snapshot_is_ancestor(c, pos.snapshot, w->d[i].snapshot))
			goto found;
	return INT_MAX;
found:
	BUG_ON(pos.snapshot > w->d[i].snapshot);

	if (pos.snapshot != w->d[i].snapshot) {
		ancestor_pos = i;

		while (i && w->d[i - 1].snapshot > pos.snapshot)
			--i;

		ret = inode_walker_realloc(w);
		if (ret)
			return ret;

		array_insert_item(w->d, w->nr, i, w->d[ancestor_pos]);
		w->d[i].snapshot = pos.snapshot;
		w->d[i].count	= 0;
	}

	return i;
}

static int walk_inode(struct btree_trans *trans,
		      struct inode_walker *w, struct bpos pos)
{
	return lockrestart_do(trans, __walk_inode(trans, w, pos));
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

	w->nr = 0;

	for_each_btree_key(trans, iter, BTREE_ID_inodes, POS(0, inum),
			   BTREE_ITER_ALL_SNAPSHOTS, k, ret) {
		if (k.k->p.offset != inum)
			break;

		if (k.k->type != KEY_TYPE_inode)
			continue;

		if (ref_visible(c, s, s->pos.snapshot, k.k->p.snapshot)) {
			add_inode(c, w, bkey_s_c_to_inode(k));
			if (k.k->p.snapshot >= s->pos.snapshot)
				break;
		}
	}
	bch2_trans_iter_exit(trans, &iter);

	return ret;
}

static int check_key_has_snapshot(struct btree_trans *trans,
				  struct btree_iter *iter,
				  struct bkey_s_c k)
{
	struct bch_fs *c = trans->c;
	char buf[200];
	int ret = 0;

	if (fsck_err_on(!snapshot_t(c, k.k->p.snapshot)->equiv, c,
			"key in missing snapshot: %s",
			(bch2_bkey_val_to_text(&PBUF(buf), c, k), buf))) {
		ret = __bch2_trans_do(trans, NULL, NULL, BTREE_INSERT_LAZY_RW,
			bch2_btree_delete_at(trans, iter,
					     BTREE_UPDATE_INTERNAL_SNAPSHOT_NODE));
		return ret ?: -EINTR;
	}
fsck_err:
	return ret;
}

static int hash_redo_key(struct btree_trans *trans,
			 const struct bch_hash_desc desc,
			 struct bch_hash_info *hash_info,
			 struct btree_iter *k_iter, struct bkey_s_c k)
{
	bch_err(trans->c, "hash_redo_key() not implemented yet");
	return -EINVAL;
#if 0
	struct bkey_i *delete;
	struct bkey_i *tmp;

	delete = bch2_trans_kmalloc(trans, sizeof(*delete));
	if (IS_ERR(delete))
		return PTR_ERR(delete);

	tmp = bch2_trans_kmalloc(trans, bkey_bytes(k.k));
	if (IS_ERR(tmp))
		return PTR_ERR(tmp);

	bkey_reassemble(tmp, k);

	bkey_init(&delete->k);
	delete->k.p = k_iter->pos;
	return  bch2_btree_iter_traverse(k_iter) ?:
		bch2_trans_update(trans, k_iter, delete, 0) ?:
		bch2_hash_set(trans, desc, hash_info, k_iter->pos.inode, tmp, 0);
#endif
}

static int fsck_hash_delete_at(struct btree_trans *trans,
			       const struct bch_hash_desc desc,
			       struct bch_hash_info *info,
			       struct btree_iter *iter)
{
	int ret;
retry:
	ret   = bch2_hash_delete_at(trans, desc, info, iter, 0) ?:
		bch2_trans_commit(trans, NULL, NULL,
				  BTREE_INSERT_NOFAIL|
				  BTREE_INSERT_LAZY_RW);
	if (ret == -EINTR) {
		ret = bch2_btree_iter_traverse(iter);
		if (!ret)
			goto retry;
	}

	return ret;
}

static int hash_check_key(struct btree_trans *trans,
			  const struct bch_hash_desc desc,
			  struct bch_hash_info *hash_info,
			  struct btree_iter *k_iter, struct bkey_s_c hash_k)
{
	struct bch_fs *c = trans->c;
	struct btree_iter iter = { NULL };
	char buf[200];
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

	for_each_btree_key(trans, iter, desc.btree_id, POS(hash_k.k->p.inode, hash),
			   BTREE_ITER_SLOTS, k, ret) {
		if (!bkey_cmp(k.k->p, hash_k.k->p))
			break;

		if (fsck_err_on(k.k->type == desc.key_type &&
				!desc.cmp_bkey(k, hash_k), c,
				"duplicate hash table keys:\n%s",
				(bch2_bkey_val_to_text(&PBUF(buf), c,
						       hash_k), buf))) {
			ret = fsck_hash_delete_at(trans, desc, hash_info, k_iter);
			if (ret)
				return ret;
			ret = 1;
			break;
		}

		if (bkey_deleted(k.k)) {
			bch2_trans_iter_exit(trans, &iter);
			goto bad_hash;
		}

	}
	bch2_trans_iter_exit(trans, &iter);
	return ret;
bad_hash:
	if (fsck_err(c, "hash table key at wrong offset: btree %u inode %llu offset %llu, "
		     "hashed to %llu\n%s",
		     desc.btree_id, hash_k.k->p.inode, hash_k.k->p.offset, hash,
		     (bch2_bkey_val_to_text(&PBUF(buf), c, hash_k), buf)) == FSCK_ERR_IGNORE)
		return 0;

	ret = __bch2_trans_do(trans, NULL, NULL,
			      BTREE_INSERT_NOFAIL|BTREE_INSERT_LAZY_RW,
		hash_redo_key(trans, desc, hash_info, k_iter, hash_k));
	if (ret) {
		bch_err(c, "hash_redo_key err %i", ret);
		return ret;
	}
	return -EINTR;
fsck_err:
	return ret;
}

static int check_inode(struct btree_trans *trans,
		       struct btree_iter *iter,
		       struct bch_inode_unpacked *prev,
		       struct bch_inode_unpacked u)
{
	struct bch_fs *c = trans->c;
	bool do_update = false;
	int ret = 0;

	if (fsck_err_on(prev &&
			(prev->bi_hash_seed		!= u.bi_hash_seed ||
			 mode_to_type(prev->bi_mode) != mode_to_type(u.bi_mode)), c,
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
		if (ret)
			bch_err(c, "error in fsck: error %i while deleting inode", ret);
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
			bch_err(c, "error in fsck: error %i truncating inode", ret);
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
			bch_err(c, "error in fsck: error %i recounting inode sectors",
				(int) sectors);
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
		ret = write_inode(trans, &u, iter->pos.snapshot);
		if (ret)
			bch_err(c, "error in fsck: error %i "
				"updating inode", ret);
	}
fsck_err:
	return ret;
}

noinline_for_stack
static int check_inodes(struct bch_fs *c, bool full)
{
	struct btree_trans trans;
	struct btree_iter iter;
	struct bkey_s_c k;
	struct bkey_s_c_inode inode;
	struct bch_inode_unpacked prev, u;
	int ret;

	memset(&prev, 0, sizeof(prev));

	bch2_trans_init(&trans, c, BTREE_ITER_MAX, 0);

	for_each_btree_key(&trans, iter, BTREE_ID_inodes, POS_MIN,
			   BTREE_ITER_INTENT|
			   BTREE_ITER_PREFETCH|
			   BTREE_ITER_ALL_SNAPSHOTS, k, ret) {
		ret = check_key_has_snapshot(&trans, &iter, k);
		if (ret)
			break;

		/*
		 * if snapshot id isn't a leaf node, skip it - deletion in
		 * particular is not atomic, so on the internal snapshot nodes
		 * we can see inodes marked for deletion after a clean shutdown
		 */
		if (bch2_snapshot_internal_node(c, k.k->p.snapshot))
			continue;

		if (k.k->type != KEY_TYPE_inode)
			continue;

		inode = bkey_s_c_to_inode(k);

		if (!full &&
		    !(inode.v->bi_flags & (BCH_INODE_I_SIZE_DIRTY|
					   BCH_INODE_I_SECTORS_DIRTY|
					   BCH_INODE_UNLINKED)))
			continue;

		BUG_ON(bch2_inode_unpack(inode, &u));

		ret = check_inode(&trans, &iter,
				  full && prev.bi_inum == u.bi_inum
				  ? &prev : NULL, u);
		if (ret)
			break;

		prev = u;
	}
	bch2_trans_iter_exit(&trans, &iter);

	BUG_ON(ret == -EINTR);

	bch2_trans_exit(&trans);
	return ret;
}

noinline_for_stack
static int check_subvols(struct bch_fs *c)
{
	struct btree_trans trans;
	struct btree_iter iter;
	struct bkey_s_c k;
	int ret;

	bch2_trans_init(&trans, c, BTREE_ITER_MAX, 0);

	for_each_btree_key(&trans, iter, BTREE_ID_subvolumes, POS_MIN,
			   0, k, ret) {
	}
	bch2_trans_iter_exit(&trans, &iter);

	bch2_trans_exit(&trans);
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

static int inode_backpointer_exists(struct btree_trans *trans,
				    struct bch_inode_unpacked *inode,
				    u32 snapshot)
{
	struct btree_iter iter;
	struct bkey_s_c k;
	u32 target_subvol, target_snapshot;
	u64 target_inum;
	int ret;

	bch2_trans_iter_init(trans, &iter, BTREE_ID_dirents,
			SPOS(inode->bi_dir, inode->bi_dir_offset, snapshot), 0);
	k = bch2_btree_iter_peek_slot(&iter);
	ret = bkey_err(k);
	if (ret)
		goto out;
	if (k.k->type != KEY_TYPE_dirent)
		goto out;

	ret = __bch2_dirent_read_target(trans, bkey_s_c_to_dirent(k),
					&target_subvol,
					&target_snapshot,
					&target_inum,
					true);
	if (ret)
		goto out;

	ret = target_inum == inode->bi_inum;
out:
	bch2_trans_iter_exit(trans, &iter);
	return ret;
}

static bool inode_backpointer_matches(struct bkey_s_c_dirent d,
				      struct bch_inode_unpacked *inode)
{
	return d.k->p.inode == inode->bi_dir &&
		d.k->p.offset == inode->bi_dir_offset;
}

static int check_i_sectors(struct btree_trans *trans, struct inode_walker *w)
{
	struct bch_fs *c = trans->c;
	struct inode_walker_entry *i;
	int ret = 0, ret2 = 0;
	s64 count2;

	for (i = w->d; i < w->d + w->nr; i++) {
		if (i->inode.bi_sectors == i->count)
			continue;

		count2 = lockrestart_do(trans,
			bch2_count_inode_sectors(trans, w->cur_inum, i->snapshot));

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
			    i->inode.bi_sectors, i->count) == FSCK_ERR_IGNORE)
			continue;

		i->inode.bi_sectors = i->count;
		ret = write_inode(trans, &i->inode, i->snapshot);
		if (ret)
			break;
		ret2 = -EINTR;
	}
fsck_err:
	return ret ?: ret2;
}

static int check_extent(struct btree_trans *trans, struct btree_iter *iter,
			struct inode_walker *inode,
			struct snapshots_seen *s)
{
	struct bch_fs *c = trans->c;
	struct bkey_s_c k;
	struct inode_walker_entry *i;
	char buf[200];
	int ret = 0;

	k = bch2_btree_iter_peek(iter);
	if (!k.k)
		return 0;

	ret = bkey_err(k);
	if (ret)
		return ret;

	ret = check_key_has_snapshot(trans, iter, k);
	if (ret)
		return ret;

	ret = snapshots_seen_update(c, s, k.k->p);
	if (ret)
		return ret;

	if (k.k->type == KEY_TYPE_whiteout)
		return 0;

	if (inode->cur_inum != k.k->p.inode) {
		ret = check_i_sectors(trans, inode);
		if (ret)
			return ret;
	}
#if 0
	if (bkey_cmp(prev.k->k.p, bkey_start_pos(k.k)) > 0) {
		char buf1[200];
		char buf2[200];

		bch2_bkey_val_to_text(&PBUF(buf1), c, bkey_i_to_s_c(prev.k));
		bch2_bkey_val_to_text(&PBUF(buf2), c, k);

		if (fsck_err(c, "overlapping extents:\n%s\n%s", buf1, buf2))
			return fix_overlapping_extent(trans, k, prev.k->k.p) ?: -EINTR;
	}
#endif
	ret = __walk_inode(trans, inode, k.k->p);
	if (ret < 0)
		return ret;

	if (fsck_err_on(ret == INT_MAX, c,
			"extent in missing inode:\n  %s",
			(bch2_bkey_val_to_text(&PBUF(buf), c, k), buf)))
		return __bch2_trans_do(trans, NULL, NULL, BTREE_INSERT_LAZY_RW,
			bch2_btree_delete_at(trans, iter,
					     BTREE_UPDATE_INTERNAL_SNAPSHOT_NODE));

	if (ret == INT_MAX)
		return 0;

	i = inode->d + ret;
	ret = 0;

	if (fsck_err_on(!S_ISREG(i->inode.bi_mode) &&
			!S_ISLNK(i->inode.bi_mode), c,
			"extent in non regular inode mode %o:\n  %s",
			i->inode.bi_mode,
			(bch2_bkey_val_to_text(&PBUF(buf), c, k), buf)))
		return __bch2_trans_do(trans, NULL, NULL, BTREE_INSERT_LAZY_RW,
			 bch2_btree_delete_at(trans, iter,
					      BTREE_UPDATE_INTERNAL_SNAPSHOT_NODE));

	if (!bch2_snapshot_internal_node(c, k.k->p.snapshot)) {
		for_each_visible_inode(c, s, inode, k.k->p.snapshot, i) {
			if (fsck_err_on(!(i->inode.bi_flags & BCH_INODE_I_SIZE_DIRTY) &&
					k.k->type != KEY_TYPE_reservation &&
					k.k->p.offset > round_up(i->inode.bi_size, block_bytes(c)) >> 9, c,
					"extent type %u offset %llu past end of inode %llu, i_size %llu",
					k.k->type, k.k->p.offset, k.k->p.inode, i->inode.bi_size)) {
				bch2_fs_lazy_rw(c);
				return bch2_btree_delete_range_trans(trans, BTREE_ID_extents,
						SPOS(k.k->p.inode, round_up(i->inode.bi_size, block_bytes(c)) >> 9,
						     k.k->p.snapshot),
						POS(k.k->p.inode, U64_MAX),
						0, NULL) ?: -EINTR;
			}
		}
	}

	if (bkey_extent_is_allocation(k.k))
		for_each_visible_inode(c, s, inode, k.k->p.snapshot, i)
			i->count += k.k->size;
#if 0
	bch2_bkey_buf_reassemble(&prev, c, k);
#endif

fsck_err:
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
	int ret = 0;

#if 0
	struct bkey_buf prev;
	bch2_bkey_buf_init(&prev);
	prev.k->k = KEY(0, 0, 0);
#endif
	snapshots_seen_init(&s);
	bch2_trans_init(&trans, c, BTREE_ITER_MAX, 0);

	bch_verbose(c, "checking extents");

	bch2_trans_iter_init(&trans, &iter, BTREE_ID_extents,
			     POS(BCACHEFS_ROOT_INO, 0),
			     BTREE_ITER_INTENT|
			     BTREE_ITER_PREFETCH|
			     BTREE_ITER_ALL_SNAPSHOTS);

	do {
		ret = lockrestart_do(&trans,
			check_extent(&trans, &iter, &w, &s));
		if (ret)
			break;
	} while (bch2_btree_iter_advance(&iter));
	bch2_trans_iter_exit(&trans, &iter);
#if 0
	bch2_bkey_buf_exit(&prev, c);
#endif
	inode_walker_exit(&w);
	bch2_trans_exit(&trans);
	snapshots_seen_exit(&s);

	return ret;
}

static int check_subdir_count(struct btree_trans *trans, struct inode_walker *w)
{
	struct bch_fs *c = trans->c;
	struct inode_walker_entry *i;
	int ret = 0, ret2 = 0;
	s64 count2;

	for (i = w->d; i < w->d + w->nr; i++) {
		if (i->inode.bi_nlink == i->count)
			continue;

		count2 = lockrestart_do(trans,
				bch2_count_subdirs(trans, w->cur_inum, i->snapshot));

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
			ret2 = -EINTR;
		}
	}
fsck_err:
	return ret ?: ret2;
}

static int check_dirent_target(struct btree_trans *trans,
			       struct btree_iter *iter,
			       struct bkey_s_c_dirent d,
			       struct bch_inode_unpacked *target,
			       u32 target_snapshot)
{
	struct bch_fs *c = trans->c;
	bool backpointer_exists = true;
	char buf[200];
	int ret = 0;

	if (!target->bi_dir &&
	    !target->bi_dir_offset) {
		target->bi_dir		= d.k->p.inode;
		target->bi_dir_offset	= d.k->p.offset;

		ret = write_inode(trans, target, target_snapshot);
		if (ret)
			goto err;
	}

	if (!inode_backpointer_matches(d, target)) {
		ret = inode_backpointer_exists(trans, target, d.k->p.snapshot);
		if (ret < 0)
			goto err;

		backpointer_exists = ret;
		ret = 0;

		if (fsck_err_on(S_ISDIR(target->bi_mode) &&
				backpointer_exists, c,
				"directory %llu with multiple links",
				target->bi_inum)) {
			ret = remove_dirent(trans, d.k->p);
			if (ret)
				goto err;
			return 0;
		}

		if (fsck_err_on(backpointer_exists &&
				!target->bi_nlink, c,
				"inode %llu has multiple links but i_nlink 0",
				target->bi_inum)) {
			target->bi_nlink++;
			target->bi_flags &= ~BCH_INODE_UNLINKED;

			ret = write_inode(trans, target, target_snapshot);
			if (ret)
				goto err;
		}

		if (fsck_err_on(!backpointer_exists, c,
				"inode %llu has wrong backpointer:\n"
				"got       %llu:%llu\n"
				"should be %llu:%llu",
				target->bi_inum,
				target->bi_dir,
				target->bi_dir_offset,
				d.k->p.inode,
				d.k->p.offset)) {
			target->bi_dir		= d.k->p.inode;
			target->bi_dir_offset	= d.k->p.offset;

			ret = write_inode(trans, target, target_snapshot);
			if (ret)
				goto err;
		}
	}

	if (fsck_err_on(vfs_d_type(d.v->d_type) != mode_to_type(target->bi_mode), c,
			"incorrect d_type: should be %u:\n%s",
			mode_to_type(target->bi_mode),
			(bch2_bkey_val_to_text(&PBUF(buf), c, d.s_c), buf))) {
		struct bkey_i_dirent *n;

		n = kmalloc(bkey_bytes(d.k), GFP_KERNEL);
		if (!n) {
			ret = -ENOMEM;
			goto err;
		}

		bkey_reassemble(&n->k_i, d.s_c);
		n->v.d_type = mode_to_type(target->bi_mode);

		ret = __bch2_trans_do(trans, NULL, NULL,
				      BTREE_INSERT_NOFAIL|
				      BTREE_INSERT_LAZY_RW,
			bch2_trans_update(trans, iter, &n->k_i, 0));
		kfree(n);
		if (ret)
			goto err;
	}
err:
fsck_err:
	return ret;
}

static int check_dirent(struct btree_trans *trans, struct btree_iter *iter,
			struct bch_hash_info *hash_info,
			struct inode_walker *dir,
			struct inode_walker *target,
			struct snapshots_seen *s)
{
	struct bch_fs *c = trans->c;
	struct bkey_s_c k;
	struct bkey_s_c_dirent d;
	struct inode_walker_entry *i;
	u32 target_snapshot;
	u32 target_subvol;
	u64 target_inum;
	char buf[200];
	int ret;

	k = bch2_btree_iter_peek(iter);
	if (!k.k)
		return 0;

	ret = bkey_err(k);
	if (ret)
		return ret;

	ret = check_key_has_snapshot(trans, iter, k);
	if (ret)
		return ret;

	ret = snapshots_seen_update(c, s, k.k->p);
	if (ret)
		return ret;

	if (k.k->type == KEY_TYPE_whiteout)
		return 0;

	if (dir->cur_inum != k.k->p.inode) {
		ret = check_subdir_count(trans, dir);
		if (ret)
			return ret;
	}

	ret = __walk_inode(trans, dir, k.k->p);
	if (ret < 0)
		return ret;

	if (fsck_err_on(ret == INT_MAX, c,
			"dirent in nonexisting directory:\n%s",
			(bch2_bkey_val_to_text(&PBUF(buf), c, k), buf)))
		return __bch2_trans_do(trans, NULL, NULL, BTREE_INSERT_LAZY_RW,
				bch2_btree_delete_at(trans, iter,
						     BTREE_UPDATE_INTERNAL_SNAPSHOT_NODE));

	if (ret == INT_MAX)
		return 0;

	i = dir->d + ret;
	ret = 0;

	if (fsck_err_on(!S_ISDIR(i->inode.bi_mode), c,
			"dirent in non directory inode type %u:\n%s",
			mode_to_type(i->inode.bi_mode),
			(bch2_bkey_val_to_text(&PBUF(buf), c, k), buf)))
		return __bch2_trans_do(trans, NULL, NULL, 0,
				bch2_btree_delete_at(trans, iter, 0));

	if (dir->first_this_inode)
		*hash_info = bch2_hash_info_init(c, &dir->d[0].inode);

	ret = hash_check_key(trans, bch2_dirent_hash_desc,
			     hash_info, iter, k);
	if (ret < 0)
		return ret;
	if (ret) /* dirent has been deleted */
		return 0;

	if (k.k->type != KEY_TYPE_dirent)
		return 0;

	d = bkey_s_c_to_dirent(k);

	ret = __bch2_dirent_read_target(trans, d,
					&target_subvol,
					&target_snapshot,
					&target_inum,
					true);
	if (ret && ret != -ENOENT)
		return ret;

	if (fsck_err_on(ret, c,
			"dirent points to missing subvolume %llu",
			le64_to_cpu(d.v->d_inum)))
		return remove_dirent(trans, d.k->p);

	if (target_subvol) {
		struct bch_inode_unpacked subvol_root;

		ret = __lookup_inode(trans, target_inum,
				   &subvol_root, &target_snapshot);
		if (ret && ret != -ENOENT)
			return ret;

		if (fsck_err_on(ret, c,
				"subvolume %u points to missing subvolume root %llu",
				target_subvol,
				target_inum)) {
			bch_err(c, "repair not implemented yet");
			return -EINVAL;
		}

		if (fsck_err_on(subvol_root.bi_subvol != target_subvol, c,
				"subvol root %llu has wrong bi_subvol field: got %u, should be %u",
				target_inum,
				subvol_root.bi_subvol, target_subvol)) {
			subvol_root.bi_subvol = target_subvol;
			ret = write_inode(trans, &subvol_root, target_snapshot);
			if (ret)
				return ret;
		}

		ret = check_dirent_target(trans, iter, d, &subvol_root,
					  target_snapshot);
		if (ret)
			return ret;
	} else {
		ret = __get_visible_inodes(trans, target, s, target_inum);
		if (ret)
			return ret;

		if (fsck_err_on(!target->nr, c,
				"dirent points to missing inode:\n%s",
				(bch2_bkey_val_to_text(&PBUF(buf), c,
						       k), buf))) {
			ret = remove_dirent(trans, d.k->p);
			if (ret)
				return ret;
		}

		for (i = target->d; i < target->d + target->nr; i++) {
			ret = check_dirent_target(trans, iter, d,
						  &i->inode, i->snapshot);
			if (ret)
				return ret;
		}
	}

	if (d.v->d_type == DT_DIR)
		for_each_visible_inode(c, s, dir, d.k->p.snapshot, i)
			i->count++;

fsck_err:
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
	int ret = 0;

	bch_verbose(c, "checking dirents");

	snapshots_seen_init(&s);
	bch2_trans_init(&trans, c, BTREE_ITER_MAX, 0);

	bch2_trans_iter_init(&trans, &iter, BTREE_ID_dirents,
			     POS(BCACHEFS_ROOT_INO, 0),
			     BTREE_ITER_INTENT|
			     BTREE_ITER_PREFETCH|
			     BTREE_ITER_ALL_SNAPSHOTS);

	do {
		ret = lockrestart_do(&trans,
			check_dirent(&trans, &iter, &hash_info,
				     &dir, &target, &s));
		if (ret)
			break;
	} while (bch2_btree_iter_advance(&iter));
	bch2_trans_iter_exit(&trans, &iter);

	bch2_trans_exit(&trans);
	snapshots_seen_exit(&s);
	inode_walker_exit(&dir);
	inode_walker_exit(&target);
	return ret;
}

/*
 * Walk xattrs: verify that they all have a corresponding inode
 */
noinline_for_stack
static int check_xattrs(struct bch_fs *c)
{
	struct inode_walker w = inode_walker_init();
	struct bch_hash_info hash_info;
	struct btree_trans trans;
	struct btree_iter iter;
	struct bkey_s_c k;
	int ret = 0;

	bch_verbose(c, "checking xattrs");

	bch2_trans_init(&trans, c, BTREE_ITER_MAX, 0);

	bch2_trans_iter_init(&trans, &iter, BTREE_ID_xattrs,
			     POS(BCACHEFS_ROOT_INO, 0),
			     BTREE_ITER_INTENT|
			     BTREE_ITER_PREFETCH|
			     BTREE_ITER_ALL_SNAPSHOTS);
retry:
	bch2_trans_begin(&trans);

	while ((k = bch2_btree_iter_peek(&iter)).k &&
	       !(ret = bkey_err(k))) {
		ret = check_key_has_snapshot(&trans, &iter, k);
		if (ret)
			break;

		ret = walk_inode(&trans, &w, k.k->p);
		if (ret < 0)
			break;

		if (fsck_err_on(ret == INT_MAX, c,
				"xattr for missing inode %llu",
				k.k->p.inode)) {
			ret = bch2_btree_delete_at(&trans, &iter, 0);
			if (ret)
				break;
			continue;
		}

		if (ret == INT_MAX)
			goto next;
		ret = 0;

		if (w.first_this_inode)
			hash_info = bch2_hash_info_init(c, &w.d[0].inode);

		ret = hash_check_key(&trans, bch2_xattr_hash_desc,
				     &hash_info, &iter, k);
		if (ret)
			break;
next:
		bch2_btree_iter_advance(&iter);
	}
fsck_err:
	if (ret == -EINTR)
		goto retry;

	bch2_trans_iter_exit(&trans, &iter);
	bch2_trans_exit(&trans);
	return ret;
}

/* Get root directory, create if it doesn't exist: */
static int check_root(struct bch_fs *c)
{
	struct btree_trans trans;
	struct bch_inode_unpacked root_inode;
	u32 snapshot;
	u64 inum;
	int ret;

	bch2_trans_init(&trans, c, 0, 0);

	bch_verbose(c, "checking root directory");

	ret = subvol_lookup(&trans, BCACHEFS_ROOT_SUBVOL, &snapshot, &inum);
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
		ret = __bch2_trans_do(&trans, NULL, NULL,
				      BTREE_INSERT_NOFAIL|
				      BTREE_INSERT_LAZY_RW,
			__bch2_btree_insert(&trans, BTREE_ID_subvolumes, &root_subvol.k_i));
		if (ret) {
			bch_err(c, "error writing root subvol: %i", ret);
			goto err;
		}

	}

	ret = lookup_inode(&trans, BCACHEFS_ROOT_INO, &root_inode, &snapshot);
	if (ret && ret != -ENOENT)
		return ret;

	if (mustfix_fsck_err_on(ret, c, "root directory missing") ||
	    mustfix_fsck_err_on(!S_ISDIR(root_inode.bi_mode), c,
				"root inode not a directory")) {
		bch2_inode_init(c, &root_inode, 0, 0, S_IFDIR|0755,
				0, NULL);
		root_inode.bi_inum = inum;

		ret = write_inode(&trans, &root_inode, snapshot);
		if (ret)
			bch_err(c, "error writing root inode: %i", ret);
	}
err:
fsck_err:
	bch2_trans_exit(&trans);
	return ret;
}

struct pathbuf {
	size_t		nr;
	size_t		size;

	struct pathbuf_entry {
		u64	inum;
	}		*entries;
};

static int path_down(struct pathbuf *p, u64 inum)
{
	if (p->nr == p->size) {
		size_t new_size = max_t(size_t, 256UL, p->size * 2);
		void *n = krealloc(p->entries,
				   new_size * sizeof(p->entries[0]),
				   GFP_KERNEL);
		if (!n) {
			return -ENOMEM;
		}

		p->entries = n;
		p->size = new_size;
	};

	p->entries[p->nr++] = (struct pathbuf_entry) {
		.inum = inum,
	};
	return 0;
}

static int check_path(struct btree_trans *trans,
		      struct pathbuf *p,
		      struct bch_inode_unpacked *inode,
		      u32 snapshot)
{
	struct bch_fs *c = trans->c;
	size_t i;
	int ret = 0;

	snapshot = snapshot_t(c, snapshot)->equiv;
	p->nr = 0;

	while (!(inode->bi_inum == BCACHEFS_ROOT_INO &&
		 inode->bi_subvol == BCACHEFS_ROOT_SUBVOL)) {
		if (inode->bi_parent_subvol) {
			u64 inum;

			ret = subvol_lookup(trans, inode->bi_parent_subvol,
					    &snapshot, &inum);
			if (ret)
				break;
		}

		ret = lockrestart_do(trans,
			inode_backpointer_exists(trans, inode, snapshot));
		if (ret < 0)
			break;

		if (!ret) {
			if (fsck_err(c,  "unreachable inode %llu:%u, type %u nlink %u backptr %llu:%llu",
				     inode->bi_inum, snapshot,
				     mode_to_type(inode->bi_mode),
				     inode->bi_nlink,
				     inode->bi_dir,
				     inode->bi_dir_offset))
				ret = reattach_inode(trans, inode, snapshot);
			break;
		}
		ret = 0;

		if (!S_ISDIR(inode->bi_mode))
			break;

		ret = path_down(p, inode->bi_inum);
		if (ret) {
			bch_err(c, "memory allocation failure");
			return ret;
		}

		for (i = 0; i < p->nr; i++) {
			if (inode->bi_dir != p->entries[i].inum)
				continue;

			/* XXX print path */
			if (!fsck_err(c, "directory structure loop"))
				return 0;

			ret = lockrestart_do(trans,
					remove_backpointer(trans, inode));
			if (ret) {
				bch_err(c, "error removing dirent: %i", ret);
				break;
			}

			ret = reattach_inode(trans, inode, snapshot);
			break;
		}

		ret = lookup_inode(trans, inode->bi_dir, inode, &snapshot);
		if (ret) {
			/* Should have been caught in dirents pass */
			bch_err(c, "error looking up parent directory: %i", ret);
			break;
		}
	}
fsck_err:
	if (ret)
		bch_err(c, "%s: err %i", __func__, ret);
	return ret;
}

/*
 * Check for unreachable inodes, as well as loops in the directory structure:
 * After check_dirents(), if an inode backpointer doesn't exist that means it's
 * unreachable:
 */
static int check_directory_structure(struct bch_fs *c)
{
	struct btree_trans trans;
	struct btree_iter iter;
	struct bkey_s_c k;
	struct bch_inode_unpacked u;
	struct pathbuf path = { 0, 0, NULL };
	int ret;

	bch2_trans_init(&trans, c, BTREE_ITER_MAX, 0);

	for_each_btree_key(&trans, iter, BTREE_ID_inodes, POS_MIN,
			   BTREE_ITER_INTENT|
			   BTREE_ITER_PREFETCH|
			   BTREE_ITER_ALL_SNAPSHOTS, k, ret) {
		if (k.k->type != KEY_TYPE_inode)
			continue;

		ret = bch2_inode_unpack(bkey_s_c_to_inode(k), &u);
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

	BUG_ON(ret == -EINTR);

	kfree(path.entries);

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

static int add_nlink(struct nlink_table *t, u64 inum, u32 snapshot)
{
	if (t->nr == t->size) {
		size_t new_size = max_t(size_t, 128UL, t->size * 2);
		void *d = kvmalloc(new_size * sizeof(t->d[0]), GFP_KERNEL);
		if (!d) {
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
	struct bkey_s_c_inode inode;
	struct bch_inode_unpacked u;
	int ret = 0;

	bch2_trans_init(&trans, c, BTREE_ITER_MAX, 0);

	for_each_btree_key(&trans, iter, BTREE_ID_inodes,
			   POS(0, start),
			   BTREE_ITER_INTENT|
			   BTREE_ITER_PREFETCH|
			   BTREE_ITER_ALL_SNAPSHOTS, k, ret) {
		if (k.k->type != KEY_TYPE_inode)
			continue;

		inode = bkey_s_c_to_inode(k);

		/*
		 * Backpointer and directory structure checks are sufficient for
		 * directories, since they can't have hardlinks:
		 */
		if (S_ISDIR(le16_to_cpu(inode.v->bi_mode)))
			continue;

		/* Should never fail, checked by bch2_inode_invalid: */
		BUG_ON(bch2_inode_unpack(inode, &u));

		if (!u.bi_nlink)
			continue;

		ret = add_nlink(t, k.k->p.offset, k.k->p.snapshot);
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
		ret = snapshots_seen_update(c, &s, k.k->p);
		if (ret)
			break;

		switch (k.k->type) {
		case KEY_TYPE_dirent:
			d = bkey_s_c_to_dirent(k);

			if (d.v->d_type != DT_DIR &&
			    d.v->d_type != DT_SUBVOL)
				inc_link(c, &s, links, range_start, range_end,
					 le64_to_cpu(d.v->d_inum),
					 d.k->p.snapshot);
			break;
		}

		bch2_trans_cond_resched(&trans);
	}
	bch2_trans_iter_exit(&trans, &iter);

	if (ret)
		bch_err(c, "error in fsck: btree error %i while walking dirents", ret);

	bch2_trans_exit(&trans);
	snapshots_seen_exit(&s);
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
	struct bkey_s_c_inode inode;
	struct bch_inode_unpacked u;
	struct nlink *link = links->d;
	int ret = 0;

	bch2_trans_init(&trans, c, BTREE_ITER_MAX, 0);

	for_each_btree_key(&trans, iter, BTREE_ID_inodes,
			   POS(0, range_start),
			   BTREE_ITER_INTENT|
			   BTREE_ITER_PREFETCH|
			   BTREE_ITER_ALL_SNAPSHOTS, k, ret) {
		if (k.k->p.offset >= range_end)
			break;

		if (k.k->type != KEY_TYPE_inode)
			continue;

		inode = bkey_s_c_to_inode(k);
		if (S_ISDIR(le16_to_cpu(inode.v->bi_mode)))
			continue;

		BUG_ON(bch2_inode_unpack(inode, &u));

		if (!u.bi_nlink)
			continue;

		while ((cmp_int(link->inum, k.k->p.offset) ?:
			cmp_int(link->snapshot, k.k->p.snapshot)) < 0) {
			link++;
			BUG_ON(link >= links->d + links->nr);
		}

		if (fsck_err_on(bch2_inode_nlink_get(&u) != link->count, c,
				"inode %llu has wrong i_nlink (type %u i_nlink %u, should be %u)",
				u.bi_inum, mode_to_type(u.bi_mode),
				bch2_inode_nlink_get(&u), link->count)) {
			bch2_inode_nlink_set(&u, link->count);

			ret = write_inode(&trans, &u, k.k->p.snapshot);
			if (ret)
				bch_err(c, "error in fsck: error %i updating inode", ret);
		}
	}
fsck_err:
	bch2_trans_iter_exit(&trans, &iter);
	bch2_trans_exit(&trans);

	if (ret)
		bch_err(c, "error in fsck: btree error %i while walking inodes", ret);

	return ret;
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

static int fix_reflink_p_key(struct btree_trans *trans, struct btree_iter *iter)
{
	struct bkey_s_c k;
	struct bkey_s_c_reflink_p p;
	struct bkey_i_reflink_p *u;
	int ret;

	k = bch2_btree_iter_peek(iter);
	if (!k.k)
		return 0;

	ret = bkey_err(k);
	if (ret)
		return ret;

	if (k.k->type != KEY_TYPE_reflink_p)
		return 0;

	p = bkey_s_c_to_reflink_p(k);

	if (!p.v->v2)
		return 0;

	u = bch2_trans_kmalloc(trans, sizeof(*u));
	ret = PTR_ERR_OR_ZERO(u);
	if (ret)
		return ret;

	bkey_reassemble(&u->k_i, k);
	u->v.v2 = 0;

	return bch2_trans_update(trans, iter, &u->k_i, 0);
}

static int fix_reflink_p(struct bch_fs *c)
{
	struct btree_trans trans;
	struct btree_iter iter;
	struct bkey_s_c k;
	int ret;

	if (c->sb.version >= bcachefs_metadata_version_reflink_p_fix)
		return 0;

	bch2_trans_init(&trans, c, BTREE_ITER_MAX, 0);

	for_each_btree_key(&trans, iter, BTREE_ID_extents, POS_MIN,
			   BTREE_ITER_INTENT|
			   BTREE_ITER_PREFETCH|
			   BTREE_ITER_ALL_SNAPSHOTS, k, ret) {
		if (k.k->type == KEY_TYPE_reflink_p) {
			ret = __bch2_trans_do(&trans, NULL, NULL,
					      BTREE_INSERT_NOFAIL|
					      BTREE_INSERT_LAZY_RW,
					      fix_reflink_p_key(&trans, &iter));
			if (ret)
				break;
		}
	}
	bch2_trans_iter_exit(&trans, &iter);

	bch2_trans_exit(&trans);
	return ret;
}

/*
 * Checks for inconsistencies that shouldn't happen, unless we have a bug.
 * Doesn't fix them yet, mainly because they haven't yet been observed:
 */
int bch2_fsck_full(struct bch_fs *c)
{
	return  bch2_fs_snapshots_check(c) ?:
		check_inodes(c, true) ?:
		check_subvols(c) ?:
		check_extents(c) ?:
		check_dirents(c) ?:
		check_xattrs(c) ?:
		check_root(c) ?:
		check_directory_structure(c) ?:
		check_nlinks(c) ?:
		fix_reflink_p(c);
}

int bch2_fsck_walk_inodes_only(struct bch_fs *c)
{
	return check_inodes(c, false);
}
