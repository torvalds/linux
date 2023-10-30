// SPDX-License-Identifier: GPL-2.0

#include "bcachefs.h"
#include "btree_key_cache.h"
#include "btree_update.h"
#include "errcode.h"
#include "error.h"
#include "fs.h"
#include "snapshot.h"
#include "subvolume.h"

#include <linux/random.h>

static int bch2_subvolume_delete(struct btree_trans *, u32);

static int check_subvol(struct btree_trans *trans,
			struct btree_iter *iter,
			struct bkey_s_c k)
{
	struct bch_fs *c = trans->c;
	struct bkey_s_c_subvolume subvol;
	struct bch_snapshot snapshot;
	unsigned snapid;
	int ret = 0;

	if (k.k->type != KEY_TYPE_subvolume)
		return 0;

	subvol = bkey_s_c_to_subvolume(k);
	snapid = le32_to_cpu(subvol.v->snapshot);
	ret = bch2_snapshot_lookup(trans, snapid, &snapshot);

	if (bch2_err_matches(ret, ENOENT))
		bch_err(c, "subvolume %llu points to nonexistent snapshot %u",
			k.k->p.offset, snapid);
	if (ret)
		return ret;

	if (BCH_SUBVOLUME_UNLINKED(subvol.v)) {
		bch2_fs_lazy_rw(c);

		ret = bch2_subvolume_delete(trans, iter->pos.offset);
		if (ret)
			bch_err_msg(c, ret, "deleting subvolume %llu", iter->pos.offset);
		return ret ?: -BCH_ERR_transaction_restart_nested;
	}

	if (!BCH_SUBVOLUME_SNAP(subvol.v)) {
		u32 snapshot_root = bch2_snapshot_root(c, le32_to_cpu(subvol.v->snapshot));
		u32 snapshot_tree;
		struct bch_snapshot_tree st;

		rcu_read_lock();
		snapshot_tree = snapshot_t(c, snapshot_root)->tree;
		rcu_read_unlock();

		ret = bch2_snapshot_tree_lookup(trans, snapshot_tree, &st);

		bch2_fs_inconsistent_on(bch2_err_matches(ret, ENOENT), c,
				"%s: snapshot tree %u not found", __func__, snapshot_tree);

		if (ret)
			return ret;

		if (fsck_err_on(le32_to_cpu(st.master_subvol) != subvol.k->p.offset, c,
				"subvolume %llu is not set as snapshot but is not master subvolume",
				k.k->p.offset)) {
			struct bkey_i_subvolume *s =
				bch2_bkey_make_mut_typed(trans, iter, &subvol.s_c, 0, subvolume);
			ret = PTR_ERR_OR_ZERO(s);
			if (ret)
				return ret;

			SET_BCH_SUBVOLUME_SNAP(&s->v, true);
		}
	}

fsck_err:
	return ret;
}

int bch2_check_subvols(struct bch_fs *c)
{
	struct btree_iter iter;
	struct bkey_s_c k;
	int ret;

	ret = bch2_trans_run(c,
		for_each_btree_key_commit(trans, iter,
			BTREE_ID_subvolumes, POS_MIN, BTREE_ITER_PREFETCH, k,
			NULL, NULL, BTREE_INSERT_LAZY_RW|BTREE_INSERT_NOFAIL,
		check_subvol(trans, &iter, k)));
	if (ret)
		bch_err_fn(c, ret);
	return ret;
}

/* Subvolumes: */

int bch2_subvolume_invalid(const struct bch_fs *c, struct bkey_s_c k,
			   enum bkey_invalid_flags flags, struct printbuf *err)
{
	if (bkey_lt(k.k->p, SUBVOL_POS_MIN) ||
	    bkey_gt(k.k->p, SUBVOL_POS_MAX)) {
		prt_printf(err, "invalid pos");
		return -BCH_ERR_invalid_bkey;
	}

	return 0;
}

void bch2_subvolume_to_text(struct printbuf *out, struct bch_fs *c,
			    struct bkey_s_c k)
{
	struct bkey_s_c_subvolume s = bkey_s_c_to_subvolume(k);

	prt_printf(out, "root %llu snapshot id %u",
		   le64_to_cpu(s.v->inode),
		   le32_to_cpu(s.v->snapshot));

	if (bkey_val_bytes(s.k) > offsetof(struct bch_subvolume, parent))
		prt_printf(out, " parent %u", le32_to_cpu(s.v->parent));
}

static __always_inline int
bch2_subvolume_get_inlined(struct btree_trans *trans, unsigned subvol,
			   bool inconsistent_if_not_found,
			   int iter_flags,
			   struct bch_subvolume *s)
{
	int ret = bch2_bkey_get_val_typed(trans, BTREE_ID_subvolumes, POS(0, subvol),
					  iter_flags, subvolume, s);
	bch2_fs_inconsistent_on(bch2_err_matches(ret, ENOENT) &&
				inconsistent_if_not_found,
				trans->c, "missing subvolume %u", subvol);
	return ret;
}

int bch2_subvolume_get(struct btree_trans *trans, unsigned subvol,
		       bool inconsistent_if_not_found,
		       int iter_flags,
		       struct bch_subvolume *s)
{
	return bch2_subvolume_get_inlined(trans, subvol, inconsistent_if_not_found, iter_flags, s);
}

int bch2_snapshot_get_subvol(struct btree_trans *trans, u32 snapshot,
			     struct bch_subvolume *subvol)
{
	struct bch_snapshot snap;

	return  bch2_snapshot_lookup(trans, snapshot, &snap) ?:
		bch2_subvolume_get(trans, le32_to_cpu(snap.subvol), true, 0, subvol);
}

int bch2_subvolume_get_snapshot(struct btree_trans *trans, u32 subvolid,
				u32 *snapid)
{
	struct btree_iter iter;
	struct bkey_s_c_subvolume subvol;
	int ret;

	subvol = bch2_bkey_get_iter_typed(trans, &iter,
					  BTREE_ID_subvolumes, POS(0, subvolid),
					  BTREE_ITER_CACHED|BTREE_ITER_WITH_UPDATES,
					  subvolume);
	ret = bkey_err(subvol);
	bch2_fs_inconsistent_on(bch2_err_matches(ret, ENOENT), trans->c,
				"missing subvolume %u", subvolid);

	if (likely(!ret))
		*snapid = le32_to_cpu(subvol.v->snapshot);
	bch2_trans_iter_exit(trans, &iter);
	return ret;
}

static int bch2_subvolume_reparent(struct btree_trans *trans,
				   struct btree_iter *iter,
				   struct bkey_s_c k,
				   u32 old_parent, u32 new_parent)
{
	struct bkey_i_subvolume *s;
	int ret;

	if (k.k->type != KEY_TYPE_subvolume)
		return 0;

	if (bkey_val_bytes(k.k) > offsetof(struct bch_subvolume, parent) &&
	    le32_to_cpu(bkey_s_c_to_subvolume(k).v->parent) != old_parent)
		return 0;

	s = bch2_bkey_make_mut_typed(trans, iter, &k, 0, subvolume);
	ret = PTR_ERR_OR_ZERO(s);
	if (ret)
		return ret;

	s->v.parent = cpu_to_le32(new_parent);
	return 0;
}

/*
 * Separate from the snapshot tree in the snapshots btree, we record the tree
 * structure of how snapshot subvolumes were created - the parent subvolume of
 * each snapshot subvolume.
 *
 * When a subvolume is deleted, we scan for child subvolumes and reparant them,
 * to avoid dangling references:
 */
static int bch2_subvolumes_reparent(struct btree_trans *trans, u32 subvolid_to_delete)
{
	struct btree_iter iter;
	struct bkey_s_c k;
	struct bch_subvolume s;

	return lockrestart_do(trans,
			bch2_subvolume_get(trans, subvolid_to_delete, true,
				   BTREE_ITER_CACHED, &s)) ?:
		for_each_btree_key_commit(trans, iter,
				BTREE_ID_subvolumes, POS_MIN, BTREE_ITER_PREFETCH, k,
				NULL, NULL, BTREE_INSERT_NOFAIL,
			bch2_subvolume_reparent(trans, &iter, k,
					subvolid_to_delete, le32_to_cpu(s.parent)));
}

/*
 * Delete subvolume, mark snapshot ID as deleted, queue up snapshot
 * deletion/cleanup:
 */
static int __bch2_subvolume_delete(struct btree_trans *trans, u32 subvolid)
{
	struct btree_iter iter;
	struct bkey_s_c_subvolume subvol;
	struct btree_trans_commit_hook *h;
	u32 snapid;
	int ret = 0;

	subvol = bch2_bkey_get_iter_typed(trans, &iter,
				BTREE_ID_subvolumes, POS(0, subvolid),
				BTREE_ITER_CACHED|BTREE_ITER_INTENT,
				subvolume);
	ret = bkey_err(subvol);
	bch2_fs_inconsistent_on(bch2_err_matches(ret, ENOENT), trans->c,
				"missing subvolume %u", subvolid);
	if (ret)
		return ret;

	snapid = le32_to_cpu(subvol.v->snapshot);

	ret = bch2_btree_delete_at(trans, &iter, 0);
	if (ret)
		goto err;

	ret = bch2_snapshot_node_set_deleted(trans, snapid);
	if (ret)
		goto err;

	h = bch2_trans_kmalloc(trans, sizeof(*h));
	ret = PTR_ERR_OR_ZERO(h);
	if (ret)
		goto err;

	h->fn = bch2_delete_dead_snapshots_hook;
	bch2_trans_commit_hook(trans, h);
err:
	bch2_trans_iter_exit(trans, &iter);
	return ret;
}

static int bch2_subvolume_delete(struct btree_trans *trans, u32 subvolid)
{
	return bch2_subvolumes_reparent(trans, subvolid) ?:
		commit_do(trans, NULL, NULL, BTREE_INSERT_NOFAIL,
			  __bch2_subvolume_delete(trans, subvolid));
}

static void bch2_subvolume_wait_for_pagecache_and_delete(struct work_struct *work)
{
	struct bch_fs *c = container_of(work, struct bch_fs,
				snapshot_wait_for_pagecache_and_delete_work);
	snapshot_id_list s;
	u32 *id;
	int ret = 0;

	while (!ret) {
		mutex_lock(&c->snapshots_unlinked_lock);
		s = c->snapshots_unlinked;
		darray_init(&c->snapshots_unlinked);
		mutex_unlock(&c->snapshots_unlinked_lock);

		if (!s.nr)
			break;

		bch2_evict_subvolume_inodes(c, &s);

		for (id = s.data; id < s.data + s.nr; id++) {
			ret = bch2_trans_run(c, bch2_subvolume_delete(trans, *id));
			if (ret) {
				bch_err_msg(c, ret, "deleting subvolume %u", *id);
				break;
			}
		}

		darray_exit(&s);
	}

	bch2_write_ref_put(c, BCH_WRITE_REF_snapshot_delete_pagecache);
}

struct subvolume_unlink_hook {
	struct btree_trans_commit_hook	h;
	u32				subvol;
};

static int bch2_subvolume_wait_for_pagecache_and_delete_hook(struct btree_trans *trans,
						      struct btree_trans_commit_hook *_h)
{
	struct subvolume_unlink_hook *h = container_of(_h, struct subvolume_unlink_hook, h);
	struct bch_fs *c = trans->c;
	int ret = 0;

	mutex_lock(&c->snapshots_unlinked_lock);
	if (!snapshot_list_has_id(&c->snapshots_unlinked, h->subvol))
		ret = snapshot_list_add(c, &c->snapshots_unlinked, h->subvol);
	mutex_unlock(&c->snapshots_unlinked_lock);

	if (ret)
		return ret;

	if (!bch2_write_ref_tryget(c, BCH_WRITE_REF_snapshot_delete_pagecache))
		return -EROFS;

	if (!queue_work(c->write_ref_wq, &c->snapshot_wait_for_pagecache_and_delete_work))
		bch2_write_ref_put(c, BCH_WRITE_REF_snapshot_delete_pagecache);
	return 0;
}

int bch2_subvolume_unlink(struct btree_trans *trans, u32 subvolid)
{
	struct btree_iter iter;
	struct bkey_i_subvolume *n;
	struct subvolume_unlink_hook *h;
	int ret = 0;

	h = bch2_trans_kmalloc(trans, sizeof(*h));
	ret = PTR_ERR_OR_ZERO(h);
	if (ret)
		return ret;

	h->h.fn		= bch2_subvolume_wait_for_pagecache_and_delete_hook;
	h->subvol	= subvolid;
	bch2_trans_commit_hook(trans, &h->h);

	n = bch2_bkey_get_mut_typed(trans, &iter,
			BTREE_ID_subvolumes, POS(0, subvolid),
			BTREE_ITER_CACHED, subvolume);
	ret = PTR_ERR_OR_ZERO(n);
	if (unlikely(ret)) {
		bch2_fs_inconsistent_on(bch2_err_matches(ret, ENOENT), trans->c,
					"missing subvolume %u", subvolid);
		return ret;
	}

	SET_BCH_SUBVOLUME_UNLINKED(&n->v, true);
	bch2_trans_iter_exit(trans, &iter);
	return ret;
}

int bch2_subvolume_create(struct btree_trans *trans, u64 inode,
			  u32 src_subvolid,
			  u32 *new_subvolid,
			  u32 *new_snapshotid,
			  bool ro)
{
	struct bch_fs *c = trans->c;
	struct btree_iter dst_iter, src_iter = (struct btree_iter) { NULL };
	struct bkey_i_subvolume *new_subvol = NULL;
	struct bkey_i_subvolume *src_subvol = NULL;
	u32 parent = 0, new_nodes[2], snapshot_subvols[2];
	int ret = 0;

	ret = bch2_bkey_get_empty_slot(trans, &dst_iter,
				BTREE_ID_subvolumes, POS(0, U32_MAX));
	if (ret == -BCH_ERR_ENOSPC_btree_slot)
		ret = -BCH_ERR_ENOSPC_subvolume_create;
	if (ret)
		return ret;

	snapshot_subvols[0] = dst_iter.pos.offset;
	snapshot_subvols[1] = src_subvolid;

	if (src_subvolid) {
		/* Creating a snapshot: */

		src_subvol = bch2_bkey_get_mut_typed(trans, &src_iter,
				BTREE_ID_subvolumes, POS(0, src_subvolid),
				BTREE_ITER_CACHED, subvolume);
		ret = PTR_ERR_OR_ZERO(src_subvol);
		if (unlikely(ret)) {
			bch2_fs_inconsistent_on(bch2_err_matches(ret, ENOENT), c,
						"subvolume %u not found", src_subvolid);
			goto err;
		}

		parent = le32_to_cpu(src_subvol->v.snapshot);
	}

	ret = bch2_snapshot_node_create(trans, parent, new_nodes,
					snapshot_subvols,
					src_subvolid ? 2 : 1);
	if (ret)
		goto err;

	if (src_subvolid) {
		src_subvol->v.snapshot = cpu_to_le32(new_nodes[1]);
		ret = bch2_trans_update(trans, &src_iter, &src_subvol->k_i, 0);
		if (ret)
			goto err;
	}

	new_subvol = bch2_bkey_alloc(trans, &dst_iter, 0, subvolume);
	ret = PTR_ERR_OR_ZERO(new_subvol);
	if (ret)
		goto err;

	new_subvol->v.flags	= 0;
	new_subvol->v.snapshot	= cpu_to_le32(new_nodes[0]);
	new_subvol->v.inode	= cpu_to_le64(inode);
	new_subvol->v.parent	= cpu_to_le32(src_subvolid);
	new_subvol->v.otime.lo	= cpu_to_le64(bch2_current_time(c));
	new_subvol->v.otime.hi	= 0;

	SET_BCH_SUBVOLUME_RO(&new_subvol->v, ro);
	SET_BCH_SUBVOLUME_SNAP(&new_subvol->v, src_subvolid != 0);

	*new_subvolid	= new_subvol->k.p.offset;
	*new_snapshotid	= new_nodes[0];
err:
	bch2_trans_iter_exit(trans, &src_iter);
	bch2_trans_iter_exit(trans, &dst_iter);
	return ret;
}

int bch2_fs_subvolumes_init(struct bch_fs *c)
{
	INIT_WORK(&c->snapshot_delete_work, bch2_delete_dead_snapshots_work);
	INIT_WORK(&c->snapshot_wait_for_pagecache_and_delete_work,
		  bch2_subvolume_wait_for_pagecache_and_delete);
	mutex_init(&c->snapshots_unlinked_lock);
	return 0;
}
