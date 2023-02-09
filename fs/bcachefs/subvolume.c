// SPDX-License-Identifier: GPL-2.0

#include "bcachefs.h"
#include "btree_key_cache.h"
#include "btree_update.h"
#include "errcode.h"
#include "error.h"
#include "fs.h"
#include "subvolume.h"

/* Snapshot tree: */

void bch2_snapshot_to_text(struct printbuf *out, struct bch_fs *c,
			   struct bkey_s_c k)
{
	struct bkey_s_c_snapshot s = bkey_s_c_to_snapshot(k);

	prt_printf(out, "is_subvol %llu deleted %llu parent %10u children %10u %10u subvol %u",
	       BCH_SNAPSHOT_SUBVOL(s.v),
	       BCH_SNAPSHOT_DELETED(s.v),
	       le32_to_cpu(s.v->parent),
	       le32_to_cpu(s.v->children[0]),
	       le32_to_cpu(s.v->children[1]),
	       le32_to_cpu(s.v->subvol));
}

int bch2_snapshot_invalid(const struct bch_fs *c, struct bkey_s_c k,
			  int rw, struct printbuf *err)
{
	struct bkey_s_c_snapshot s;
	u32 i, id;

	if (bkey_gt(k.k->p, POS(0, U32_MAX)) ||
	    bkey_lt(k.k->p, POS(0, 1))) {
		prt_printf(err, "bad pos");
		return -BCH_ERR_invalid_bkey;
	}

	if (bkey_val_bytes(k.k) != sizeof(struct bch_snapshot)) {
		prt_printf(err, "bad val size (%zu != %zu)",
		       bkey_val_bytes(k.k), sizeof(struct bch_snapshot));
		return -BCH_ERR_invalid_bkey;
	}

	s = bkey_s_c_to_snapshot(k);

	id = le32_to_cpu(s.v->parent);
	if (id && id <= k.k->p.offset) {
		prt_printf(err, "bad parent node (%u <= %llu)",
		       id, k.k->p.offset);
		return -BCH_ERR_invalid_bkey;
	}

	if (le32_to_cpu(s.v->children[0]) < le32_to_cpu(s.v->children[1])) {
		prt_printf(err, "children not normalized");
		return -BCH_ERR_invalid_bkey;
	}

	if (s.v->children[0] &&
	    s.v->children[0] == s.v->children[1]) {
		prt_printf(err, "duplicate child nodes");
		return -BCH_ERR_invalid_bkey;
	}

	for (i = 0; i < 2; i++) {
		id = le32_to_cpu(s.v->children[i]);

		if (id >= k.k->p.offset) {
			prt_printf(err, "bad child node (%u >= %llu)",
			       id, k.k->p.offset);
			return -BCH_ERR_invalid_bkey;
		}
	}

	return 0;
}

int bch2_mark_snapshot(struct btree_trans *trans,
		       struct bkey_s_c old, struct bkey_s_c new,
		       unsigned flags)
{
	struct bch_fs *c = trans->c;
	struct snapshot_t *t;

	t = genradix_ptr_alloc(&c->snapshots,
			       U32_MAX - new.k->p.offset,
			       GFP_KERNEL);
	if (!t)
		return -ENOMEM;

	if (new.k->type == KEY_TYPE_snapshot) {
		struct bkey_s_c_snapshot s = bkey_s_c_to_snapshot(new);

		t->parent	= le32_to_cpu(s.v->parent);
		t->children[0]	= le32_to_cpu(s.v->children[0]);
		t->children[1]	= le32_to_cpu(s.v->children[1]);
		t->subvol	= BCH_SNAPSHOT_SUBVOL(s.v) ? le32_to_cpu(s.v->subvol) : 0;
	} else {
		t->parent	= 0;
		t->children[0]	= 0;
		t->children[1]	= 0;
		t->subvol	= 0;
	}

	return 0;
}

static int snapshot_lookup(struct btree_trans *trans, u32 id,
			   struct bch_snapshot *s)
{
	struct btree_iter iter;
	struct bkey_s_c k;
	int ret;

	bch2_trans_iter_init(trans, &iter, BTREE_ID_snapshots, POS(0, id),
			     BTREE_ITER_WITH_UPDATES);
	k = bch2_btree_iter_peek_slot(&iter);
	ret = bkey_err(k) ?: k.k->type == KEY_TYPE_snapshot ? 0 : -ENOENT;

	if (!ret)
		*s = *bkey_s_c_to_snapshot(k).v;

	bch2_trans_iter_exit(trans, &iter);
	return ret;
}

static int snapshot_live(struct btree_trans *trans, u32 id)
{
	struct bch_snapshot v;
	int ret;

	if (!id)
		return 0;

	ret = snapshot_lookup(trans, id, &v);
	if (ret == -ENOENT)
		bch_err(trans->c, "snapshot node %u not found", id);
	if (ret)
		return ret;

	return !BCH_SNAPSHOT_DELETED(&v);
}

static int bch2_snapshot_set_equiv(struct btree_trans *trans, struct bkey_s_c k)
{
	struct bch_fs *c = trans->c;
	unsigned i, nr_live = 0, live_idx = 0;
	struct bkey_s_c_snapshot snap;
	u32 id = k.k->p.offset, child[2];

	if (k.k->type != KEY_TYPE_snapshot)
		return 0;

	snap = bkey_s_c_to_snapshot(k);

	child[0] = le32_to_cpu(snap.v->children[0]);
	child[1] = le32_to_cpu(snap.v->children[1]);

	for (i = 0; i < 2; i++) {
		int ret = snapshot_live(trans, child[i]);

		if (ret < 0)
			return ret;

		if (ret)
			live_idx = i;
		nr_live += ret;
	}

	snapshot_t(c, id)->equiv = nr_live == 1
		? snapshot_t(c, child[live_idx])->equiv
		: id;
	return 0;
}

/* fsck: */
static int check_snapshot(struct btree_trans *trans,
			  struct btree_iter *iter,
			  struct bkey_s_c k)
{
	struct bch_fs *c = trans->c;
	struct bkey_s_c_snapshot s;
	struct bch_subvolume subvol;
	struct bch_snapshot v;
	struct printbuf buf = PRINTBUF;
	bool should_have_subvol;
	u32 i, id;
	int ret = 0;

	if (k.k->type != KEY_TYPE_snapshot)
		return 0;

	s = bkey_s_c_to_snapshot(k);
	id = le32_to_cpu(s.v->parent);
	if (id) {
		ret = snapshot_lookup(trans, id, &v);
		if (ret == -ENOENT)
			bch_err(c, "snapshot with nonexistent parent:\n  %s",
				(bch2_bkey_val_to_text(&buf, c, s.s_c), buf.buf));
		if (ret)
			goto err;

		if (le32_to_cpu(v.children[0]) != s.k->p.offset &&
		    le32_to_cpu(v.children[1]) != s.k->p.offset) {
			bch_err(c, "snapshot parent %u missing pointer to child %llu",
				id, s.k->p.offset);
			ret = -EINVAL;
			goto err;
		}
	}

	for (i = 0; i < 2 && s.v->children[i]; i++) {
		id = le32_to_cpu(s.v->children[i]);

		ret = snapshot_lookup(trans, id, &v);
		if (ret == -ENOENT)
			bch_err(c, "snapshot node %llu has nonexistent child %u",
				s.k->p.offset, id);
		if (ret)
			goto err;

		if (le32_to_cpu(v.parent) != s.k->p.offset) {
			bch_err(c, "snapshot child %u has wrong parent (got %u should be %llu)",
				id, le32_to_cpu(v.parent), s.k->p.offset);
			ret = -EINVAL;
			goto err;
		}
	}

	should_have_subvol = BCH_SNAPSHOT_SUBVOL(s.v) &&
		!BCH_SNAPSHOT_DELETED(s.v);

	if (should_have_subvol) {
		id = le32_to_cpu(s.v->subvol);
		ret = bch2_subvolume_get(trans, id, 0, false, &subvol);
		if (ret == -ENOENT)
			bch_err(c, "snapshot points to nonexistent subvolume:\n  %s",
				(bch2_bkey_val_to_text(&buf, c, s.s_c), buf.buf));
		if (ret)
			goto err;

		if (BCH_SNAPSHOT_SUBVOL(s.v) != (le32_to_cpu(subvol.snapshot) == s.k->p.offset)) {
			bch_err(c, "snapshot node %llu has wrong BCH_SNAPSHOT_SUBVOL",
				s.k->p.offset);
			ret = -EINVAL;
			goto err;
		}
	} else {
		if (fsck_err_on(s.v->subvol, c, "snapshot should not point to subvol:\n  %s",
				(bch2_bkey_val_to_text(&buf, c, s.s_c), buf.buf))) {
			struct bkey_i_snapshot *u = bch2_trans_kmalloc(trans, sizeof(*u));

			ret = PTR_ERR_OR_ZERO(u);
			if (ret)
				goto err;

			bkey_reassemble(&u->k_i, s.s_c);
			u->v.subvol = 0;
			ret = bch2_trans_update(trans, iter, &u->k_i, 0);
			if (ret)
				goto err;
		}
	}

	if (BCH_SNAPSHOT_DELETED(s.v))
		set_bit(BCH_FS_HAVE_DELETED_SNAPSHOTS, &c->flags);
err:
fsck_err:
	printbuf_exit(&buf);
	return ret;
}

int bch2_fs_check_snapshots(struct bch_fs *c)
{
	struct btree_trans trans;
	struct btree_iter iter;
	struct bkey_s_c k;
	int ret;

	bch2_trans_init(&trans, c, 0, 0);

	ret = for_each_btree_key_commit(&trans, iter,
			BTREE_ID_snapshots, POS_MIN,
			BTREE_ITER_PREFETCH, k,
			NULL, NULL, BTREE_INSERT_LAZY_RW|BTREE_INSERT_NOFAIL,
		check_snapshot(&trans, &iter, k));

	if (ret)
		bch_err(c, "error %i checking snapshots", ret);

	bch2_trans_exit(&trans);
	return ret;
}

static int check_subvol(struct btree_trans *trans,
			struct btree_iter *iter,
			struct bkey_s_c k)
{
	struct bkey_s_c_subvolume subvol;
	struct bch_snapshot snapshot;
	unsigned snapid;
	int ret;

	if (k.k->type != KEY_TYPE_subvolume)
		return 0;

	subvol = bkey_s_c_to_subvolume(k);
	snapid = le32_to_cpu(subvol.v->snapshot);
	ret = snapshot_lookup(trans, snapid, &snapshot);

	if (ret == -ENOENT)
		bch_err(trans->c, "subvolume %llu points to nonexistent snapshot %u",
			k.k->p.offset, snapid);
	if (ret)
		return ret;

	if (BCH_SUBVOLUME_UNLINKED(subvol.v)) {
		ret = bch2_subvolume_delete(trans, iter->pos.offset);
		if (ret && !bch2_err_matches(ret, BCH_ERR_transaction_restart))
			bch_err(trans->c, "error deleting subvolume %llu: %s",
				iter->pos.offset, bch2_err_str(ret));
		if (ret)
			return ret;
	}

	return 0;
}

int bch2_fs_check_subvols(struct bch_fs *c)
{
	struct btree_trans trans;
	struct btree_iter iter;
	struct bkey_s_c k;
	int ret;

	bch2_trans_init(&trans, c, 0, 0);

	ret = for_each_btree_key_commit(&trans, iter,
			BTREE_ID_subvolumes, POS_MIN, BTREE_ITER_PREFETCH, k,
			NULL, NULL, BTREE_INSERT_LAZY_RW|BTREE_INSERT_NOFAIL,
		check_subvol(&trans, &iter, k));

	bch2_trans_exit(&trans);

	return ret;
}

void bch2_fs_snapshots_exit(struct bch_fs *c)
{
	genradix_free(&c->snapshots);
}

int bch2_fs_snapshots_start(struct bch_fs *c)
{
	struct btree_trans trans;
	struct btree_iter iter;
	struct bkey_s_c k;
	int ret = 0;

	bch2_trans_init(&trans, c, 0, 0);

	for_each_btree_key2(&trans, iter, BTREE_ID_snapshots,
			   POS_MIN, 0, k,
		bch2_mark_snapshot(&trans, bkey_s_c_null, k, 0) ?:
		bch2_snapshot_set_equiv(&trans, k));

	bch2_trans_exit(&trans);

	if (ret)
		bch_err(c, "error starting snapshots: %s", bch2_err_str(ret));
	return ret;
}

/*
 * Mark a snapshot as deleted, for future cleanup:
 */
static int bch2_snapshot_node_set_deleted(struct btree_trans *trans, u32 id)
{
	struct btree_iter iter;
	struct bkey_i_snapshot *s;
	int ret = 0;

	bch2_trans_iter_init(trans, &iter, BTREE_ID_snapshots, POS(0, id),
			     BTREE_ITER_INTENT);
	s = bch2_bkey_get_mut_typed(trans, &iter, snapshot);
	ret = PTR_ERR_OR_ZERO(s);
	if (unlikely(ret)) {
		bch2_fs_inconsistent_on(ret == -ENOENT, trans->c, "missing snapshot %u", id);
		goto err;
	}

	/* already deleted? */
	if (BCH_SNAPSHOT_DELETED(&s->v))
		goto err;

	SET_BCH_SNAPSHOT_DELETED(&s->v, true);
	SET_BCH_SNAPSHOT_SUBVOL(&s->v, false);
	s->v.subvol = 0;

	ret = bch2_trans_update(trans, &iter, &s->k_i, 0);
	if (ret)
		goto err;
err:
	bch2_trans_iter_exit(trans, &iter);
	return ret;
}

static int bch2_snapshot_node_delete(struct btree_trans *trans, u32 id)
{
	struct btree_iter iter, p_iter = (struct btree_iter) { NULL };
	struct bkey_s_c k;
	struct bkey_s_c_snapshot s;
	u32 parent_id;
	unsigned i;
	int ret = 0;

	bch2_trans_iter_init(trans, &iter, BTREE_ID_snapshots, POS(0, id),
			     BTREE_ITER_INTENT);
	k = bch2_btree_iter_peek_slot(&iter);
	ret = bkey_err(k);
	if (ret)
		goto err;

	if (k.k->type != KEY_TYPE_snapshot) {
		bch2_fs_inconsistent(trans->c, "missing snapshot %u", id);
		ret = -ENOENT;
		goto err;
	}

	s = bkey_s_c_to_snapshot(k);

	BUG_ON(!BCH_SNAPSHOT_DELETED(s.v));
	parent_id = le32_to_cpu(s.v->parent);

	if (parent_id) {
		struct bkey_i_snapshot *parent;

		bch2_trans_iter_init(trans, &p_iter, BTREE_ID_snapshots,
				     POS(0, parent_id),
				     BTREE_ITER_INTENT);
		parent = bch2_bkey_get_mut_typed(trans, &p_iter, snapshot);
		ret = PTR_ERR_OR_ZERO(parent);
		if (unlikely(ret)) {
			bch2_fs_inconsistent_on(ret == -ENOENT, trans->c, "missing snapshot %u", parent_id);
			goto err;
		}

		for (i = 0; i < 2; i++)
			if (le32_to_cpu(parent->v.children[i]) == id)
				break;

		if (i == 2)
			bch_err(trans->c, "snapshot %u missing child pointer to %u",
				parent_id, id);
		else
			parent->v.children[i] = 0;

		if (le32_to_cpu(parent->v.children[0]) <
		    le32_to_cpu(parent->v.children[1]))
			swap(parent->v.children[0],
			     parent->v.children[1]);

		ret = bch2_trans_update(trans, &p_iter, &parent->k_i, 0);
		if (ret)
			goto err;
	}

	ret = bch2_btree_delete_at(trans, &iter, 0);
err:
	bch2_trans_iter_exit(trans, &p_iter);
	bch2_trans_iter_exit(trans, &iter);
	return ret;
}

int bch2_snapshot_node_create(struct btree_trans *trans, u32 parent,
			      u32 *new_snapids,
			      u32 *snapshot_subvols,
			      unsigned nr_snapids)
{
	struct btree_iter iter;
	struct bkey_i_snapshot *n;
	struct bkey_s_c k;
	unsigned i;
	int ret = 0;

	bch2_trans_iter_init(trans, &iter, BTREE_ID_snapshots,
			     POS_MIN, BTREE_ITER_INTENT);
	k = bch2_btree_iter_peek(&iter);
	ret = bkey_err(k);
	if (ret)
		goto err;

	for (i = 0; i < nr_snapids; i++) {
		k = bch2_btree_iter_prev_slot(&iter);
		ret = bkey_err(k);
		if (ret)
			goto err;

		if (!k.k || !k.k->p.offset) {
			ret = -BCH_ERR_ENOSPC_snapshot_create;
			goto err;
		}

		n = bch2_bkey_alloc(trans, &iter, snapshot);
		ret = PTR_ERR_OR_ZERO(n);
		if (ret)
			goto err;

		n->v.flags	= 0;
		n->v.parent	= cpu_to_le32(parent);
		n->v.subvol	= cpu_to_le32(snapshot_subvols[i]);
		n->v.pad	= 0;
		SET_BCH_SNAPSHOT_SUBVOL(&n->v, true);

		ret   = bch2_trans_update(trans, &iter, &n->k_i, 0) ?:
			bch2_mark_snapshot(trans, bkey_s_c_null, bkey_i_to_s_c(&n->k_i), 0);
		if (ret)
			goto err;

		new_snapids[i]	= iter.pos.offset;
	}

	if (parent) {
		bch2_btree_iter_set_pos(&iter, POS(0, parent));
		n = bch2_bkey_get_mut_typed(trans, &iter, snapshot);
		ret = PTR_ERR_OR_ZERO(n);
		if (unlikely(ret)) {
			if (ret == -ENOENT)
				bch_err(trans->c, "snapshot %u not found", parent);
			goto err;
		}

		if (n->v.children[0] || n->v.children[1]) {
			bch_err(trans->c, "Trying to add child snapshot nodes to parent that already has children");
			ret = -EINVAL;
			goto err;
		}

		n->v.children[0] = cpu_to_le32(new_snapids[0]);
		n->v.children[1] = cpu_to_le32(new_snapids[1]);
		n->v.subvol = 0;
		SET_BCH_SNAPSHOT_SUBVOL(&n->v, false);
		ret = bch2_trans_update(trans, &iter, &n->k_i, 0);
		if (ret)
			goto err;
	}
err:
	bch2_trans_iter_exit(trans, &iter);
	return ret;
}

static int snapshot_delete_key(struct btree_trans *trans,
			       struct btree_iter *iter,
			       struct bkey_s_c k,
			       snapshot_id_list *deleted,
			       snapshot_id_list *equiv_seen,
			       struct bpos *last_pos)
{
	struct bch_fs *c = trans->c;
	u32 equiv = snapshot_t(c, k.k->p.snapshot)->equiv;

	if (!bkey_eq(k.k->p, *last_pos))
		equiv_seen->nr = 0;
	*last_pos = k.k->p;

	if (snapshot_list_has_id(deleted, k.k->p.snapshot) ||
	    snapshot_list_has_id(equiv_seen, equiv)) {
		return bch2_btree_delete_at(trans, iter,
					    BTREE_UPDATE_INTERNAL_SNAPSHOT_NODE);
	} else {
		return snapshot_list_add(c, equiv_seen, equiv);
	}
}

static int bch2_delete_redundant_snapshot(struct btree_trans *trans, struct btree_iter *iter,
					  struct bkey_s_c k)
{
	struct bkey_s_c_snapshot snap;
	u32 children[2];
	int ret;

	if (k.k->type != KEY_TYPE_snapshot)
		return 0;

	snap = bkey_s_c_to_snapshot(k);
	if (BCH_SNAPSHOT_DELETED(snap.v) ||
	    BCH_SNAPSHOT_SUBVOL(snap.v))
		return 0;

	children[0] = le32_to_cpu(snap.v->children[0]);
	children[1] = le32_to_cpu(snap.v->children[1]);

	ret   = snapshot_live(trans, children[0]) ?:
		snapshot_live(trans, children[1]);
	if (ret < 0)
		return ret;

	if (!ret)
		return bch2_snapshot_node_set_deleted(trans, k.k->p.offset);
	return 0;
}

int bch2_delete_dead_snapshots(struct bch_fs *c)
{
	struct btree_trans trans;
	struct btree_iter iter;
	struct bkey_s_c k;
	struct bkey_s_c_snapshot snap;
	snapshot_id_list deleted = { 0 };
	u32 i, id;
	int ret = 0;

	if (!test_bit(BCH_FS_HAVE_DELETED_SNAPSHOTS, &c->flags))
		return 0;

	if (!test_bit(BCH_FS_STARTED, &c->flags)) {
		ret = bch2_fs_read_write_early(c);
		if (ret) {
			bch_err(c, "error deleleting dead snapshots: error going rw: %s", bch2_err_str(ret));
			return ret;
		}
	}

	bch2_trans_init(&trans, c, 0, 0);

	/*
	 * For every snapshot node: If we have no live children and it's not
	 * pointed to by a subvolume, delete it:
	 */
	ret = for_each_btree_key_commit(&trans, iter, BTREE_ID_snapshots,
			POS_MIN, 0, k,
			NULL, NULL, 0,
		bch2_delete_redundant_snapshot(&trans, &iter, k));
	if (ret) {
		bch_err(c, "error deleting redundant snapshots: %s", bch2_err_str(ret));
		goto err;
	}

	for_each_btree_key2(&trans, iter, BTREE_ID_snapshots,
			   POS_MIN, 0, k,
		bch2_snapshot_set_equiv(&trans, k));
	if (ret) {
		bch_err(c, "error in bch2_snapshots_set_equiv: %s", bch2_err_str(ret));
		goto err;
	}

	for_each_btree_key(&trans, iter, BTREE_ID_snapshots,
			   POS_MIN, 0, k, ret) {
		if (k.k->type != KEY_TYPE_snapshot)
			continue;

		snap = bkey_s_c_to_snapshot(k);
		if (BCH_SNAPSHOT_DELETED(snap.v)) {
			ret = snapshot_list_add(c, &deleted, k.k->p.offset);
			if (ret)
				break;
		}
	}
	bch2_trans_iter_exit(&trans, &iter);

	if (ret) {
		bch_err(c, "error walking snapshots: %s", bch2_err_str(ret));
		goto err;
	}

	for (id = 0; id < BTREE_ID_NR; id++) {
		struct bpos last_pos = POS_MIN;
		snapshot_id_list equiv_seen = { 0 };

		if (!btree_type_has_snapshots(id))
			continue;

		ret = for_each_btree_key_commit(&trans, iter,
				id, POS_MIN,
				BTREE_ITER_PREFETCH|BTREE_ITER_ALL_SNAPSHOTS, k,
				NULL, NULL, BTREE_INSERT_NOFAIL,
			snapshot_delete_key(&trans, &iter, k, &deleted, &equiv_seen, &last_pos));

		darray_exit(&equiv_seen);

		if (ret) {
			bch_err(c, "error deleting snapshot keys: %s", bch2_err_str(ret));
			goto err;
		}
	}

	for (i = 0; i < deleted.nr; i++) {
		ret = commit_do(&trans, NULL, NULL, 0,
			bch2_snapshot_node_delete(&trans, deleted.data[i]));
		if (ret) {
			bch_err(c, "error deleting snapshot %u: %s",
				deleted.data[i], bch2_err_str(ret));
			goto err;
		}
	}

	clear_bit(BCH_FS_HAVE_DELETED_SNAPSHOTS, &c->flags);
err:
	darray_exit(&deleted);
	bch2_trans_exit(&trans);
	return ret;
}

static void bch2_delete_dead_snapshots_work(struct work_struct *work)
{
	struct bch_fs *c = container_of(work, struct bch_fs, snapshot_delete_work);

	bch2_delete_dead_snapshots(c);
	bch2_write_ref_put(c, BCH_WRITE_REF_delete_dead_snapshots);
}

void bch2_delete_dead_snapshots_async(struct bch_fs *c)
{
	if (bch2_write_ref_tryget(c, BCH_WRITE_REF_delete_dead_snapshots) &&
	    !queue_work(system_long_wq, &c->snapshot_delete_work))
		bch2_write_ref_put(c, BCH_WRITE_REF_delete_dead_snapshots);
}

static int bch2_delete_dead_snapshots_hook(struct btree_trans *trans,
					   struct btree_trans_commit_hook *h)
{
	struct bch_fs *c = trans->c;

	set_bit(BCH_FS_HAVE_DELETED_SNAPSHOTS, &c->flags);

	if (!test_bit(BCH_FS_FSCK_DONE, &c->flags))
		return 0;

	bch2_delete_dead_snapshots_async(c);
	return 0;
}

/* Subvolumes: */

int bch2_subvolume_invalid(const struct bch_fs *c, struct bkey_s_c k,
			   int rw, struct printbuf *err)
{
	if (bkey_lt(k.k->p, SUBVOL_POS_MIN) ||
	    bkey_gt(k.k->p, SUBVOL_POS_MAX)) {
		prt_printf(err, "invalid pos");
		return -BCH_ERR_invalid_bkey;
	}

	if (bkey_val_bytes(k.k) != sizeof(struct bch_subvolume)) {
		prt_printf(err, "incorrect value size (%zu != %zu)",
		       bkey_val_bytes(k.k), sizeof(struct bch_subvolume));
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
}

static __always_inline int
bch2_subvolume_get_inlined(struct btree_trans *trans, unsigned subvol,
			   bool inconsistent_if_not_found,
			   int iter_flags,
			   struct bch_subvolume *s)
{
	struct btree_iter iter;
	struct bkey_s_c k;
	int ret;

	bch2_trans_iter_init(trans, &iter, BTREE_ID_subvolumes, POS(0, subvol),
			     iter_flags);
	k = bch2_btree_iter_peek_slot(&iter);
	ret = bkey_err(k) ?: k.k->type == KEY_TYPE_subvolume ? 0 : -ENOENT;

	if (ret == -ENOENT && inconsistent_if_not_found)
		bch2_fs_inconsistent(trans->c, "missing subvolume %u", subvol);
	if (!ret)
		*s = *bkey_s_c_to_subvolume(k).v;

	bch2_trans_iter_exit(trans, &iter);
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

	return  snapshot_lookup(trans, snapshot, &snap) ?:
		bch2_subvolume_get(trans, le32_to_cpu(snap.subvol), true, 0, subvol);
}

int bch2_subvolume_get_snapshot(struct btree_trans *trans, u32 subvol,
				u32 *snapid)
{
	struct bch_subvolume s;
	int ret;

	ret = bch2_subvolume_get_inlined(trans, subvol, true,
					 BTREE_ITER_CACHED|
					 BTREE_ITER_WITH_UPDATES,
					 &s);
	if (!ret)
		*snapid = le32_to_cpu(s.snapshot);
	return ret;
}

/*
 * Delete subvolume, mark snapshot ID as deleted, queue up snapshot
 * deletion/cleanup:
 */
int bch2_subvolume_delete(struct btree_trans *trans, u32 subvolid)
{
	struct btree_iter iter;
	struct bkey_s_c k;
	struct bkey_s_c_subvolume subvol;
	struct btree_trans_commit_hook *h;
	u32 snapid;
	int ret = 0;

	bch2_trans_iter_init(trans, &iter, BTREE_ID_subvolumes,
			     POS(0, subvolid),
			     BTREE_ITER_CACHED|
			     BTREE_ITER_INTENT);
	k = bch2_btree_iter_peek_slot(&iter);
	ret = bkey_err(k);
	if (ret)
		goto err;

	if (k.k->type != KEY_TYPE_subvolume) {
		bch2_fs_inconsistent(trans->c, "missing subvolume %u", subvolid);
		ret = -EIO;
		goto err;
	}

	subvol = bkey_s_c_to_subvolume(k);
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

void bch2_subvolume_wait_for_pagecache_and_delete(struct work_struct *work)
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
			ret = bch2_trans_do(c, NULL, NULL, BTREE_INSERT_NOFAIL,
				      bch2_subvolume_delete(&trans, *id));
			if (ret) {
				bch_err(c, "error deleting subvolume %u: %s", *id, bch2_err_str(ret));
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

int bch2_subvolume_wait_for_pagecache_and_delete_hook(struct btree_trans *trans,
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

	if (!queue_work(system_long_wq, &c->snapshot_wait_for_pagecache_and_delete_work))
		bch2_write_ref_put(c, BCH_WRITE_REF_snapshot_delete_pagecache);
	return 0;
}

int bch2_subvolume_unlink(struct btree_trans *trans, u32 subvolid)
{
	struct btree_iter iter;
	struct bkey_i_subvolume *n;
	struct subvolume_unlink_hook *h;
	int ret = 0;

	bch2_trans_iter_init(trans, &iter, BTREE_ID_subvolumes,
			     POS(0, subvolid),
			     BTREE_ITER_CACHED|
			     BTREE_ITER_INTENT);
	n = bch2_bkey_get_mut_typed(trans, &iter, subvolume);
	ret = PTR_ERR_OR_ZERO(n);
	if (unlikely(ret)) {
		bch2_fs_inconsistent_on(ret == -ENOENT, trans->c, "missing subvolume %u", subvolid);
		goto err;
	}

	SET_BCH_SUBVOLUME_UNLINKED(&n->v, true);

	ret = bch2_trans_update(trans, &iter, &n->k_i, 0);
	if (ret)
		goto err;

	h = bch2_trans_kmalloc(trans, sizeof(*h));
	ret = PTR_ERR_OR_ZERO(h);
	if (ret)
		goto err;

	h->h.fn		= bch2_subvolume_wait_for_pagecache_and_delete_hook;
	h->subvol	= subvolid;
	bch2_trans_commit_hook(trans, &h->h);
err:
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
	struct bkey_s_c k;
	u32 parent = 0, new_nodes[2], snapshot_subvols[2];
	int ret = 0;

	for_each_btree_key(trans, dst_iter, BTREE_ID_subvolumes, SUBVOL_POS_MIN,
			   BTREE_ITER_SLOTS|BTREE_ITER_INTENT, k, ret) {
		if (bkey_gt(k.k->p, SUBVOL_POS_MAX))
			break;

		/*
		 * bch2_subvolume_delete() doesn't flush the btree key cache -
		 * ideally it would but that's tricky
		 */
		if (bkey_deleted(k.k) &&
		    !bch2_btree_key_cache_find(c, BTREE_ID_subvolumes, dst_iter.pos))
			goto found_slot;
	}

	if (!ret)
		ret = -BCH_ERR_ENOSPC_subvolume_create;
	goto err;
found_slot:
	snapshot_subvols[0] = dst_iter.pos.offset;
	snapshot_subvols[1] = src_subvolid;

	if (src_subvolid) {
		/* Creating a snapshot: */

		bch2_trans_iter_init(trans, &src_iter, BTREE_ID_subvolumes,
				     POS(0, src_subvolid),
				     BTREE_ITER_CACHED|
				     BTREE_ITER_INTENT);
		src_subvol = bch2_bkey_get_mut_typed(trans, &src_iter, subvolume);
		ret = PTR_ERR_OR_ZERO(src_subvol);
		if (unlikely(ret)) {
			bch2_fs_inconsistent_on(ret == -ENOENT, trans->c,
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

	new_subvol = bch2_bkey_alloc(trans, &dst_iter, subvolume);
	ret = PTR_ERR_OR_ZERO(new_subvol);
	if (ret)
		goto err;

	new_subvol->v.flags	= 0;
	new_subvol->v.snapshot	= cpu_to_le32(new_nodes[0]);
	new_subvol->v.inode	= cpu_to_le64(inode);
	SET_BCH_SUBVOLUME_RO(&new_subvol->v, ro);
	SET_BCH_SUBVOLUME_SNAP(&new_subvol->v, src_subvolid != 0);
	ret = bch2_trans_update(trans, &dst_iter, &new_subvol->k_i, 0);
	if (ret)
		goto err;

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
