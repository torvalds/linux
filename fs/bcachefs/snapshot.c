// SPDX-License-Identifier: GPL-2.0

#include "bcachefs.h"
#include "bkey_buf.h"
#include "btree_cache.h"
#include "btree_key_cache.h"
#include "btree_update.h"
#include "buckets.h"
#include "errcode.h"
#include "error.h"
#include "fs.h"
#include "recovery_passes.h"
#include "snapshot.h"

#include <linux/random.h>

/*
 * Snapshot trees:
 *
 * Keys in BTREE_ID_snapshot_trees identify a whole tree of snapshot nodes; they
 * exist to provide a stable identifier for the whole lifetime of a snapshot
 * tree.
 */

void bch2_snapshot_tree_to_text(struct printbuf *out, struct bch_fs *c,
				struct bkey_s_c k)
{
	struct bkey_s_c_snapshot_tree t = bkey_s_c_to_snapshot_tree(k);

	prt_printf(out, "subvol %u root snapshot %u",
		   le32_to_cpu(t.v->master_subvol),
		   le32_to_cpu(t.v->root_snapshot));
}

int bch2_snapshot_tree_validate(struct bch_fs *c, struct bkey_s_c k,
				struct bkey_validate_context from)
{
	int ret = 0;

	bkey_fsck_err_on(bkey_gt(k.k->p, POS(0, U32_MAX)) ||
			 bkey_lt(k.k->p, POS(0, 1)),
			 c, snapshot_tree_pos_bad,
			 "bad pos");
fsck_err:
	return ret;
}

int bch2_snapshot_tree_lookup(struct btree_trans *trans, u32 id,
			      struct bch_snapshot_tree *s)
{
	int ret = bch2_bkey_get_val_typed(trans, BTREE_ID_snapshot_trees, POS(0, id),
					  BTREE_ITER_with_updates, snapshot_tree, s);

	if (bch2_err_matches(ret, ENOENT))
		ret = -BCH_ERR_ENOENT_snapshot_tree;
	return ret;
}

struct bkey_i_snapshot_tree *
__bch2_snapshot_tree_create(struct btree_trans *trans)
{
	struct btree_iter iter;
	int ret = bch2_bkey_get_empty_slot(trans, &iter,
			BTREE_ID_snapshot_trees, POS(0, U32_MAX));
	struct bkey_i_snapshot_tree *s_t;

	if (ret == -BCH_ERR_ENOSPC_btree_slot)
		ret = -BCH_ERR_ENOSPC_snapshot_tree;
	if (ret)
		return ERR_PTR(ret);

	s_t = bch2_bkey_alloc(trans, &iter, 0, snapshot_tree);
	ret = PTR_ERR_OR_ZERO(s_t);
	bch2_trans_iter_exit(trans, &iter);
	return ret ? ERR_PTR(ret) : s_t;
}

static int bch2_snapshot_tree_create(struct btree_trans *trans,
				u32 root_id, u32 subvol_id, u32 *tree_id)
{
	struct bkey_i_snapshot_tree *n_tree =
		__bch2_snapshot_tree_create(trans);

	if (IS_ERR(n_tree))
		return PTR_ERR(n_tree);

	n_tree->v.master_subvol	= cpu_to_le32(subvol_id);
	n_tree->v.root_snapshot	= cpu_to_le32(root_id);
	*tree_id = n_tree->k.p.offset;
	return 0;
}

/* Snapshot nodes: */

static bool __bch2_snapshot_is_ancestor_early(struct snapshot_table *t, u32 id, u32 ancestor)
{
	while (id && id < ancestor) {
		const struct snapshot_t *s = __snapshot_t(t, id);
		id = s ? s->parent : 0;
	}
	return id == ancestor;
}

static bool bch2_snapshot_is_ancestor_early(struct bch_fs *c, u32 id, u32 ancestor)
{
	rcu_read_lock();
	bool ret = __bch2_snapshot_is_ancestor_early(rcu_dereference(c->snapshots), id, ancestor);
	rcu_read_unlock();

	return ret;
}

static inline u32 get_ancestor_below(struct snapshot_table *t, u32 id, u32 ancestor)
{
	const struct snapshot_t *s = __snapshot_t(t, id);
	if (!s)
		return 0;

	if (s->skip[2] <= ancestor)
		return s->skip[2];
	if (s->skip[1] <= ancestor)
		return s->skip[1];
	if (s->skip[0] <= ancestor)
		return s->skip[0];
	return s->parent;
}

static bool test_ancestor_bitmap(struct snapshot_table *t, u32 id, u32 ancestor)
{
	const struct snapshot_t *s = __snapshot_t(t, id);
	if (!s)
		return false;

	return test_bit(ancestor - id - 1, s->is_ancestor);
}

bool __bch2_snapshot_is_ancestor(struct bch_fs *c, u32 id, u32 ancestor)
{
	bool ret;

	rcu_read_lock();
	struct snapshot_table *t = rcu_dereference(c->snapshots);

	if (unlikely(c->recovery_pass_done < BCH_RECOVERY_PASS_check_snapshots)) {
		ret = __bch2_snapshot_is_ancestor_early(t, id, ancestor);
		goto out;
	}

	if (likely(ancestor >= IS_ANCESTOR_BITMAP))
		while (id && id < ancestor - IS_ANCESTOR_BITMAP)
			id = get_ancestor_below(t, id, ancestor);

	ret = id && id < ancestor
		? test_ancestor_bitmap(t, id, ancestor)
		: id == ancestor;

	EBUG_ON(ret != __bch2_snapshot_is_ancestor_early(t, id, ancestor));
out:
	rcu_read_unlock();

	return ret;
}

static noinline struct snapshot_t *__snapshot_t_mut(struct bch_fs *c, u32 id)
{
	size_t idx = U32_MAX - id;
	struct snapshot_table *new, *old;

	size_t new_bytes = kmalloc_size_roundup(struct_size(new, s, idx + 1));
	size_t new_size = (new_bytes - sizeof(*new)) / sizeof(new->s[0]);

	if (unlikely(new_bytes > INT_MAX))
		return NULL;

	new = kvzalloc(new_bytes, GFP_KERNEL);
	if (!new)
		return NULL;

	new->nr = new_size;

	old = rcu_dereference_protected(c->snapshots, true);
	if (old)
		memcpy(new->s, old->s, sizeof(old->s[0]) * old->nr);

	rcu_assign_pointer(c->snapshots, new);
	kvfree_rcu(old, rcu);

	return &rcu_dereference_protected(c->snapshots,
				lockdep_is_held(&c->snapshot_table_lock))->s[idx];
}

static inline struct snapshot_t *snapshot_t_mut(struct bch_fs *c, u32 id)
{
	size_t idx = U32_MAX - id;
	struct snapshot_table *table =
		rcu_dereference_protected(c->snapshots,
				lockdep_is_held(&c->snapshot_table_lock));

	lockdep_assert_held(&c->snapshot_table_lock);

	if (likely(table && idx < table->nr))
		return &table->s[idx];

	return __snapshot_t_mut(c, id);
}

void bch2_snapshot_to_text(struct printbuf *out, struct bch_fs *c,
			   struct bkey_s_c k)
{
	struct bkey_s_c_snapshot s = bkey_s_c_to_snapshot(k);

	prt_printf(out, "is_subvol %llu deleted %llu parent %10u children %10u %10u subvol %u tree %u",
	       BCH_SNAPSHOT_SUBVOL(s.v),
	       BCH_SNAPSHOT_DELETED(s.v),
	       le32_to_cpu(s.v->parent),
	       le32_to_cpu(s.v->children[0]),
	       le32_to_cpu(s.v->children[1]),
	       le32_to_cpu(s.v->subvol),
	       le32_to_cpu(s.v->tree));

	if (bkey_val_bytes(k.k) > offsetof(struct bch_snapshot, depth))
		prt_printf(out, " depth %u skiplist %u %u %u",
			   le32_to_cpu(s.v->depth),
			   le32_to_cpu(s.v->skip[0]),
			   le32_to_cpu(s.v->skip[1]),
			   le32_to_cpu(s.v->skip[2]));
}

int bch2_snapshot_validate(struct bch_fs *c, struct bkey_s_c k,
			   struct bkey_validate_context from)
{
	struct bkey_s_c_snapshot s;
	u32 i, id;
	int ret = 0;

	bkey_fsck_err_on(bkey_gt(k.k->p, POS(0, U32_MAX)) ||
			 bkey_lt(k.k->p, POS(0, 1)),
			 c, snapshot_pos_bad,
			 "bad pos");

	s = bkey_s_c_to_snapshot(k);

	id = le32_to_cpu(s.v->parent);
	bkey_fsck_err_on(id && id <= k.k->p.offset,
			 c, snapshot_parent_bad,
			 "bad parent node (%u <= %llu)",
			 id, k.k->p.offset);

	bkey_fsck_err_on(le32_to_cpu(s.v->children[0]) < le32_to_cpu(s.v->children[1]),
			 c, snapshot_children_not_normalized,
			 "children not normalized");

	bkey_fsck_err_on(s.v->children[0] && s.v->children[0] == s.v->children[1],
			 c, snapshot_child_duplicate,
			 "duplicate child nodes");

	for (i = 0; i < 2; i++) {
		id = le32_to_cpu(s.v->children[i]);

		bkey_fsck_err_on(id >= k.k->p.offset,
				 c, snapshot_child_bad,
				 "bad child node (%u >= %llu)",
				 id, k.k->p.offset);
	}

	if (bkey_val_bytes(k.k) > offsetof(struct bch_snapshot, skip)) {
		bkey_fsck_err_on(le32_to_cpu(s.v->skip[0]) > le32_to_cpu(s.v->skip[1]) ||
				 le32_to_cpu(s.v->skip[1]) > le32_to_cpu(s.v->skip[2]),
				 c, snapshot_skiplist_not_normalized,
				 "skiplist not normalized");

		for (i = 0; i < ARRAY_SIZE(s.v->skip); i++) {
			id = le32_to_cpu(s.v->skip[i]);

			bkey_fsck_err_on(id && id < le32_to_cpu(s.v->parent),
					 c, snapshot_skiplist_bad,
					 "bad skiplist node %u", id);
		}
	}
fsck_err:
	return ret;
}

static int __bch2_mark_snapshot(struct btree_trans *trans,
		       enum btree_id btree, unsigned level,
		       struct bkey_s_c old, struct bkey_s_c new,
		       enum btree_iter_update_trigger_flags flags)
{
	struct bch_fs *c = trans->c;
	struct snapshot_t *t;
	u32 id = new.k->p.offset;
	int ret = 0;

	mutex_lock(&c->snapshot_table_lock);

	t = snapshot_t_mut(c, id);
	if (!t) {
		ret = -BCH_ERR_ENOMEM_mark_snapshot;
		goto err;
	}

	if (new.k->type == KEY_TYPE_snapshot) {
		struct bkey_s_c_snapshot s = bkey_s_c_to_snapshot(new);

		t->live		= true;
		t->parent	= le32_to_cpu(s.v->parent);
		t->children[0]	= le32_to_cpu(s.v->children[0]);
		t->children[1]	= le32_to_cpu(s.v->children[1]);
		t->subvol	= BCH_SNAPSHOT_SUBVOL(s.v) ? le32_to_cpu(s.v->subvol) : 0;
		t->tree		= le32_to_cpu(s.v->tree);

		if (bkey_val_bytes(s.k) > offsetof(struct bch_snapshot, depth)) {
			t->depth	= le32_to_cpu(s.v->depth);
			t->skip[0]	= le32_to_cpu(s.v->skip[0]);
			t->skip[1]	= le32_to_cpu(s.v->skip[1]);
			t->skip[2]	= le32_to_cpu(s.v->skip[2]);
		} else {
			t->depth	= 0;
			t->skip[0]	= 0;
			t->skip[1]	= 0;
			t->skip[2]	= 0;
		}

		u32 parent = id;

		while ((parent = bch2_snapshot_parent_early(c, parent)) &&
		       parent - id - 1 < IS_ANCESTOR_BITMAP)
			__set_bit(parent - id - 1, t->is_ancestor);

		if (BCH_SNAPSHOT_DELETED(s.v)) {
			set_bit(BCH_FS_need_delete_dead_snapshots, &c->flags);
			if (c->curr_recovery_pass > BCH_RECOVERY_PASS_delete_dead_snapshots)
				bch2_delete_dead_snapshots_async(c);
		}
	} else {
		memset(t, 0, sizeof(*t));
	}
err:
	mutex_unlock(&c->snapshot_table_lock);
	return ret;
}

int bch2_mark_snapshot(struct btree_trans *trans,
		       enum btree_id btree, unsigned level,
		       struct bkey_s_c old, struct bkey_s new,
		       enum btree_iter_update_trigger_flags flags)
{
	return __bch2_mark_snapshot(trans, btree, level, old, new.s_c, flags);
}

int bch2_snapshot_lookup(struct btree_trans *trans, u32 id,
			 struct bch_snapshot *s)
{
	return bch2_bkey_get_val_typed(trans, BTREE_ID_snapshots, POS(0, id),
				       BTREE_ITER_with_updates, snapshot, s);
}

/* fsck: */

static u32 bch2_snapshot_child(struct bch_fs *c, u32 id, unsigned child)
{
	return snapshot_t(c, id)->children[child];
}

static u32 bch2_snapshot_left_child(struct bch_fs *c, u32 id)
{
	return bch2_snapshot_child(c, id, 0);
}

static u32 bch2_snapshot_right_child(struct bch_fs *c, u32 id)
{
	return bch2_snapshot_child(c, id, 1);
}

static u32 bch2_snapshot_tree_next(struct bch_fs *c, u32 id)
{
	u32 n, parent;

	n = bch2_snapshot_left_child(c, id);
	if (n)
		return n;

	while ((parent = bch2_snapshot_parent(c, id))) {
		n = bch2_snapshot_right_child(c, parent);
		if (n && n != id)
			return n;
		id = parent;
	}

	return 0;
}

u32 bch2_snapshot_tree_oldest_subvol(struct bch_fs *c, u32 snapshot_root)
{
	u32 id = snapshot_root;
	u32 subvol = 0, s;

	rcu_read_lock();
	while (id) {
		s = snapshot_t(c, id)->subvol;

		if (s && (!subvol || s < subvol))
			subvol = s;

		id = bch2_snapshot_tree_next(c, id);
	}
	rcu_read_unlock();

	return subvol;
}

static int bch2_snapshot_tree_master_subvol(struct btree_trans *trans,
					    u32 snapshot_root, u32 *subvol_id)
{
	struct bch_fs *c = trans->c;
	struct btree_iter iter;
	struct bkey_s_c k;
	bool found = false;
	int ret;

	for_each_btree_key_norestart(trans, iter, BTREE_ID_subvolumes, POS_MIN,
				     0, k, ret) {
		if (k.k->type != KEY_TYPE_subvolume)
			continue;

		struct bkey_s_c_subvolume s = bkey_s_c_to_subvolume(k);
		if (!bch2_snapshot_is_ancestor(c, le32_to_cpu(s.v->snapshot), snapshot_root))
			continue;
		if (!BCH_SUBVOLUME_SNAP(s.v)) {
			*subvol_id = s.k->p.offset;
			found = true;
			break;
		}
	}
	bch2_trans_iter_exit(trans, &iter);

	if (!ret && !found) {
		struct bkey_i_subvolume *u;

		*subvol_id = bch2_snapshot_tree_oldest_subvol(c, snapshot_root);

		u = bch2_bkey_get_mut_typed(trans, &iter,
					    BTREE_ID_subvolumes, POS(0, *subvol_id),
					    0, subvolume);
		ret = PTR_ERR_OR_ZERO(u);
		if (ret)
			return ret;

		SET_BCH_SUBVOLUME_SNAP(&u->v, false);
	}

	return ret;
}

static int check_snapshot_tree(struct btree_trans *trans,
			       struct btree_iter *iter,
			       struct bkey_s_c k)
{
	struct bch_fs *c = trans->c;
	struct bkey_s_c_snapshot_tree st;
	struct bch_snapshot s;
	struct bch_subvolume subvol;
	struct printbuf buf = PRINTBUF;
	struct btree_iter snapshot_iter = {};
	u32 root_id;
	int ret;

	if (k.k->type != KEY_TYPE_snapshot_tree)
		return 0;

	st = bkey_s_c_to_snapshot_tree(k);
	root_id = le32_to_cpu(st.v->root_snapshot);

	struct bkey_s_c_snapshot snapshot_k =
		bch2_bkey_get_iter_typed(trans, &snapshot_iter, BTREE_ID_snapshots,
					 POS(0, root_id), 0, snapshot);
	ret = bkey_err(snapshot_k);
	if (ret && !bch2_err_matches(ret, ENOENT))
		goto err;

	if (!ret)
		bkey_val_copy(&s, snapshot_k);

	if (fsck_err_on(ret ||
			root_id != bch2_snapshot_root(c, root_id) ||
			st.k->p.offset != le32_to_cpu(s.tree),
			trans, snapshot_tree_to_missing_snapshot,
			"snapshot tree points to missing/incorrect snapshot:\n%s",
			(bch2_bkey_val_to_text(&buf, c, st.s_c),
			 prt_newline(&buf),
			 ret
			 ? prt_printf(&buf, "(%s)", bch2_err_str(ret))
			 : bch2_bkey_val_to_text(&buf, c, snapshot_k.s_c),
			 buf.buf))) {
		ret = bch2_btree_delete_at(trans, iter, 0);
		goto err;
	}

	if (!st.v->master_subvol)
		goto out;

	ret = bch2_subvolume_get(trans, le32_to_cpu(st.v->master_subvol), false, &subvol);
	if (ret && !bch2_err_matches(ret, ENOENT))
		goto err;

	if (fsck_err_on(ret,
			trans, snapshot_tree_to_missing_subvol,
			"snapshot tree points to missing subvolume:\n%s",
			(printbuf_reset(&buf),
			 bch2_bkey_val_to_text(&buf, c, st.s_c), buf.buf)) ||
	    fsck_err_on(!bch2_snapshot_is_ancestor(c,
						le32_to_cpu(subvol.snapshot),
						root_id),
			trans, snapshot_tree_to_wrong_subvol,
			"snapshot tree points to subvolume that does not point to snapshot in this tree:\n%s",
			(printbuf_reset(&buf),
			 bch2_bkey_val_to_text(&buf, c, st.s_c), buf.buf)) ||
	    fsck_err_on(BCH_SUBVOLUME_SNAP(&subvol),
			trans, snapshot_tree_to_snapshot_subvol,
			"snapshot tree points to snapshot subvolume:\n%s",
			(printbuf_reset(&buf),
			 bch2_bkey_val_to_text(&buf, c, st.s_c), buf.buf))) {
		struct bkey_i_snapshot_tree *u;
		u32 subvol_id;

		ret = bch2_snapshot_tree_master_subvol(trans, root_id, &subvol_id);
		bch_err_fn(c, ret);

		if (bch2_err_matches(ret, ENOENT)) { /* nothing to be done here */
			ret = 0;
			goto err;
		}

		if (ret)
			goto err;

		u = bch2_bkey_make_mut_typed(trans, iter, &k, 0, snapshot_tree);
		ret = PTR_ERR_OR_ZERO(u);
		if (ret)
			goto err;

		u->v.master_subvol = cpu_to_le32(subvol_id);
		st = snapshot_tree_i_to_s_c(u);
	}
out:
err:
fsck_err:
	bch2_trans_iter_exit(trans, &snapshot_iter);
	printbuf_exit(&buf);
	return ret;
}

/*
 * For each snapshot_tree, make sure it points to the root of a snapshot tree
 * and that snapshot entry points back to it, or delete it.
 *
 * And, make sure it points to a subvolume within that snapshot tree, or correct
 * it to point to the oldest subvolume within that snapshot tree.
 */
int bch2_check_snapshot_trees(struct bch_fs *c)
{
	int ret = bch2_trans_run(c,
		for_each_btree_key_commit(trans, iter,
			BTREE_ID_snapshot_trees, POS_MIN,
			BTREE_ITER_prefetch, k,
			NULL, NULL, BCH_TRANS_COMMIT_no_enospc,
		check_snapshot_tree(trans, &iter, k)));
	bch_err_fn(c, ret);
	return ret;
}

/*
 * Look up snapshot tree for @tree_id and find root,
 * make sure @snap_id is a descendent:
 */
static int snapshot_tree_ptr_good(struct btree_trans *trans,
				  u32 snap_id, u32 tree_id)
{
	struct bch_snapshot_tree s_t;
	int ret = bch2_snapshot_tree_lookup(trans, tree_id, &s_t);

	if (bch2_err_matches(ret, ENOENT))
		return 0;
	if (ret)
		return ret;

	return bch2_snapshot_is_ancestor_early(trans->c, snap_id, le32_to_cpu(s_t.root_snapshot));
}

u32 bch2_snapshot_skiplist_get(struct bch_fs *c, u32 id)
{
	const struct snapshot_t *s;

	if (!id)
		return 0;

	rcu_read_lock();
	s = snapshot_t(c, id);
	if (s->parent)
		id = bch2_snapshot_nth_parent(c, id, get_random_u32_below(s->depth));
	rcu_read_unlock();

	return id;
}

static int snapshot_skiplist_good(struct btree_trans *trans, u32 id, struct bch_snapshot s)
{
	unsigned i;

	for (i = 0; i < 3; i++)
		if (!s.parent) {
			if (s.skip[i])
				return false;
		} else {
			if (!bch2_snapshot_is_ancestor_early(trans->c, id, le32_to_cpu(s.skip[i])))
				return false;
		}

	return true;
}

/*
 * snapshot_tree pointer was incorrect: look up root snapshot node, make sure
 * its snapshot_tree pointer is correct (allocate new one if necessary), then
 * update this node's pointer to root node's pointer:
 */
static int snapshot_tree_ptr_repair(struct btree_trans *trans,
				    struct btree_iter *iter,
				    struct bkey_s_c k,
				    struct bch_snapshot *s)
{
	struct bch_fs *c = trans->c;
	struct btree_iter root_iter;
	struct bch_snapshot_tree s_t;
	struct bkey_s_c_snapshot root;
	struct bkey_i_snapshot *u;
	u32 root_id = bch2_snapshot_root(c, k.k->p.offset), tree_id;
	int ret;

	root = bch2_bkey_get_iter_typed(trans, &root_iter,
			       BTREE_ID_snapshots, POS(0, root_id),
			       BTREE_ITER_with_updates, snapshot);
	ret = bkey_err(root);
	if (ret)
		goto err;

	tree_id = le32_to_cpu(root.v->tree);

	ret = bch2_snapshot_tree_lookup(trans, tree_id, &s_t);
	if (ret && !bch2_err_matches(ret, ENOENT))
		return ret;

	if (ret || le32_to_cpu(s_t.root_snapshot) != root_id) {
		u = bch2_bkey_make_mut_typed(trans, &root_iter, &root.s_c, 0, snapshot);
		ret =   PTR_ERR_OR_ZERO(u) ?:
			bch2_snapshot_tree_create(trans, root_id,
				bch2_snapshot_tree_oldest_subvol(c, root_id),
				&tree_id);
		if (ret)
			goto err;

		u->v.tree = cpu_to_le32(tree_id);
		if (k.k->p.offset == root_id)
			*s = u->v;
	}

	if (k.k->p.offset != root_id) {
		u = bch2_bkey_make_mut_typed(trans, iter, &k, 0, snapshot);
		ret = PTR_ERR_OR_ZERO(u);
		if (ret)
			goto err;

		u->v.tree = cpu_to_le32(tree_id);
		*s = u->v;
	}
err:
	bch2_trans_iter_exit(trans, &root_iter);
	return ret;
}

static int check_snapshot(struct btree_trans *trans,
			  struct btree_iter *iter,
			  struct bkey_s_c k)
{
	struct bch_fs *c = trans->c;
	struct bch_snapshot s;
	struct bch_subvolume subvol;
	struct bch_snapshot v;
	struct bkey_i_snapshot *u;
	u32 parent_id = bch2_snapshot_parent_early(c, k.k->p.offset);
	u32 real_depth;
	struct printbuf buf = PRINTBUF;
	u32 i, id;
	int ret = 0;

	if (k.k->type != KEY_TYPE_snapshot)
		return 0;

	memset(&s, 0, sizeof(s));
	memcpy(&s, k.v, min(sizeof(s), bkey_val_bytes(k.k)));

	id = le32_to_cpu(s.parent);
	if (id) {
		ret = bch2_snapshot_lookup(trans, id, &v);
		if (bch2_err_matches(ret, ENOENT))
			bch_err(c, "snapshot with nonexistent parent:\n  %s",
				(bch2_bkey_val_to_text(&buf, c, k), buf.buf));
		if (ret)
			goto err;

		if (le32_to_cpu(v.children[0]) != k.k->p.offset &&
		    le32_to_cpu(v.children[1]) != k.k->p.offset) {
			bch_err(c, "snapshot parent %u missing pointer to child %llu",
				id, k.k->p.offset);
			ret = -EINVAL;
			goto err;
		}
	}

	for (i = 0; i < 2 && s.children[i]; i++) {
		id = le32_to_cpu(s.children[i]);

		ret = bch2_snapshot_lookup(trans, id, &v);
		if (bch2_err_matches(ret, ENOENT))
			bch_err(c, "snapshot node %llu has nonexistent child %u",
				k.k->p.offset, id);
		if (ret)
			goto err;

		if (le32_to_cpu(v.parent) != k.k->p.offset) {
			bch_err(c, "snapshot child %u has wrong parent (got %u should be %llu)",
				id, le32_to_cpu(v.parent), k.k->p.offset);
			ret = -EINVAL;
			goto err;
		}
	}

	bool should_have_subvol = BCH_SNAPSHOT_SUBVOL(&s) &&
		!BCH_SNAPSHOT_DELETED(&s);

	if (should_have_subvol) {
		id = le32_to_cpu(s.subvol);
		ret = bch2_subvolume_get(trans, id, false, &subvol);
		if (bch2_err_matches(ret, ENOENT))
			bch_err(c, "snapshot points to nonexistent subvolume:\n  %s",
				(bch2_bkey_val_to_text(&buf, c, k), buf.buf));
		if (ret)
			goto err;

		if (BCH_SNAPSHOT_SUBVOL(&s) != (le32_to_cpu(subvol.snapshot) == k.k->p.offset)) {
			bch_err(c, "snapshot node %llu has wrong BCH_SNAPSHOT_SUBVOL",
				k.k->p.offset);
			ret = -EINVAL;
			goto err;
		}
	} else {
		if (fsck_err_on(s.subvol,
				trans, snapshot_should_not_have_subvol,
				"snapshot should not point to subvol:\n%s",
				(bch2_bkey_val_to_text(&buf, c, k), buf.buf))) {
			u = bch2_bkey_make_mut_typed(trans, iter, &k, 0, snapshot);
			ret = PTR_ERR_OR_ZERO(u);
			if (ret)
				goto err;

			u->v.subvol = 0;
			s = u->v;
		}
	}

	ret = snapshot_tree_ptr_good(trans, k.k->p.offset, le32_to_cpu(s.tree));
	if (ret < 0)
		goto err;

	if (fsck_err_on(!ret,
			trans, snapshot_to_bad_snapshot_tree,
			"snapshot points to missing/incorrect tree:\n%s",
			(bch2_bkey_val_to_text(&buf, c, k), buf.buf))) {
		ret = snapshot_tree_ptr_repair(trans, iter, k, &s);
		if (ret)
			goto err;
	}
	ret = 0;

	real_depth = bch2_snapshot_depth(c, parent_id);

	if (fsck_err_on(le32_to_cpu(s.depth) != real_depth,
			trans, snapshot_bad_depth,
			"snapshot with incorrect depth field, should be %u:\n%s",
			real_depth, (bch2_bkey_val_to_text(&buf, c, k), buf.buf))) {
		u = bch2_bkey_make_mut_typed(trans, iter, &k, 0, snapshot);
		ret = PTR_ERR_OR_ZERO(u);
		if (ret)
			goto err;

		u->v.depth = cpu_to_le32(real_depth);
		s = u->v;
	}

	ret = snapshot_skiplist_good(trans, k.k->p.offset, s);
	if (ret < 0)
		goto err;

	if (fsck_err_on(!ret,
			trans, snapshot_bad_skiplist,
			"snapshot with bad skiplist field:\n%s",
			(bch2_bkey_val_to_text(&buf, c, k), buf.buf))) {
		u = bch2_bkey_make_mut_typed(trans, iter, &k, 0, snapshot);
		ret = PTR_ERR_OR_ZERO(u);
		if (ret)
			goto err;

		for (i = 0; i < ARRAY_SIZE(u->v.skip); i++)
			u->v.skip[i] = cpu_to_le32(bch2_snapshot_skiplist_get(c, parent_id));

		bubble_sort(u->v.skip, ARRAY_SIZE(u->v.skip), cmp_le32);
		s = u->v;
	}
	ret = 0;
err:
fsck_err:
	printbuf_exit(&buf);
	return ret;
}

int bch2_check_snapshots(struct bch_fs *c)
{
	/*
	 * We iterate backwards as checking/fixing the depth field requires that
	 * the parent's depth already be correct:
	 */
	int ret = bch2_trans_run(c,
		for_each_btree_key_reverse_commit(trans, iter,
				BTREE_ID_snapshots, POS_MAX,
				BTREE_ITER_prefetch, k,
				NULL, NULL, BCH_TRANS_COMMIT_no_enospc,
			check_snapshot(trans, &iter, k)));
	bch_err_fn(c, ret);
	return ret;
}

static int check_snapshot_exists(struct btree_trans *trans, u32 id)
{
	struct bch_fs *c = trans->c;

	/* Do we need to reconstruct the snapshot_tree entry as well? */
	struct btree_iter iter;
	struct bkey_s_c k;
	int ret = 0;
	u32 tree_id = 0;

	for_each_btree_key_norestart(trans, iter, BTREE_ID_snapshot_trees, POS_MIN,
				     0, k, ret) {
		if (le32_to_cpu(bkey_s_c_to_snapshot_tree(k).v->root_snapshot) == id) {
			tree_id = k.k->p.offset;
			break;
		}
	}
	bch2_trans_iter_exit(trans, &iter);

	if (ret)
		return ret;

	if (!tree_id) {
		ret = bch2_snapshot_tree_create(trans, id, 0, &tree_id);
		if (ret)
			return ret;
	}

	struct bkey_i_snapshot *snapshot = bch2_trans_kmalloc(trans, sizeof(*snapshot));
	ret = PTR_ERR_OR_ZERO(snapshot);
	if (ret)
		return ret;

	bkey_snapshot_init(&snapshot->k_i);
	snapshot->k.p		= POS(0, id);
	snapshot->v.tree	= cpu_to_le32(tree_id);
	snapshot->v.btime.lo	= cpu_to_le64(bch2_current_time(c));

	for_each_btree_key_norestart(trans, iter, BTREE_ID_subvolumes, POS_MIN,
				     0, k, ret) {
		if (le32_to_cpu(bkey_s_c_to_subvolume(k).v->snapshot) == id) {
			snapshot->v.subvol = cpu_to_le32(k.k->p.offset);
			SET_BCH_SNAPSHOT_SUBVOL(&snapshot->v, true);
			break;
		}
	}
	bch2_trans_iter_exit(trans, &iter);

	return  bch2_btree_insert_trans(trans, BTREE_ID_snapshots, &snapshot->k_i, 0) ?:
		bch2_mark_snapshot(trans, BTREE_ID_snapshots, 0,
				   bkey_s_c_null, bkey_i_to_s(&snapshot->k_i), 0);
}

/* Figure out which snapshot nodes belong in the same tree: */
struct snapshot_tree_reconstruct {
	enum btree_id			btree;
	struct bpos			cur_pos;
	snapshot_id_list		cur_ids;
	DARRAY(snapshot_id_list)	trees;
};

static void snapshot_tree_reconstruct_exit(struct snapshot_tree_reconstruct *r)
{
	darray_for_each(r->trees, i)
		darray_exit(i);
	darray_exit(&r->trees);
	darray_exit(&r->cur_ids);
}

static inline bool same_snapshot(struct snapshot_tree_reconstruct *r, struct bpos pos)
{
	return r->btree == BTREE_ID_inodes
		? r->cur_pos.offset == pos.offset
		: r->cur_pos.inode == pos.inode;
}

static inline bool snapshot_id_lists_have_common(snapshot_id_list *l, snapshot_id_list *r)
{
	darray_for_each(*l, i)
		if (snapshot_list_has_id(r, *i))
			return true;
	return false;
}

static void snapshot_id_list_to_text(struct printbuf *out, snapshot_id_list *s)
{
	bool first = true;
	darray_for_each(*s, i) {
		if (!first)
			prt_char(out, ' ');
		first = false;
		prt_printf(out, "%u", *i);
	}
}

static int snapshot_tree_reconstruct_next(struct bch_fs *c, struct snapshot_tree_reconstruct *r)
{
	if (r->cur_ids.nr) {
		darray_for_each(r->trees, i)
			if (snapshot_id_lists_have_common(i, &r->cur_ids)) {
				int ret = snapshot_list_merge(c, i, &r->cur_ids);
				if (ret)
					return ret;
				goto out;
			}
		darray_push(&r->trees, r->cur_ids);
		darray_init(&r->cur_ids);
	}
out:
	r->cur_ids.nr = 0;
	return 0;
}

static int get_snapshot_trees(struct bch_fs *c, struct snapshot_tree_reconstruct *r, struct bpos pos)
{
	if (!same_snapshot(r, pos))
		snapshot_tree_reconstruct_next(c, r);
	r->cur_pos = pos;
	return snapshot_list_add_nodup(c, &r->cur_ids, pos.snapshot);
}

int bch2_reconstruct_snapshots(struct bch_fs *c)
{
	struct btree_trans *trans = bch2_trans_get(c);
	struct printbuf buf = PRINTBUF;
	struct snapshot_tree_reconstruct r = {};
	int ret = 0;

	for (unsigned btree = 0; btree < BTREE_ID_NR; btree++) {
		if (btree_type_has_snapshots(btree)) {
			r.btree = btree;

			ret = for_each_btree_key(trans, iter, btree, POS_MIN,
					BTREE_ITER_all_snapshots|BTREE_ITER_prefetch, k, ({
				get_snapshot_trees(c, &r, k.k->p);
			}));
			if (ret)
				goto err;

			snapshot_tree_reconstruct_next(c, &r);
		}
	}

	darray_for_each(r.trees, t) {
		printbuf_reset(&buf);
		snapshot_id_list_to_text(&buf, t);

		darray_for_each(*t, id) {
			if (fsck_err_on(!bch2_snapshot_exists(c, *id),
					trans, snapshot_node_missing,
					"snapshot node %u from tree %s missing, recreate?", *id, buf.buf)) {
				if (t->nr > 1) {
					bch_err(c, "cannot reconstruct snapshot trees with multiple nodes");
					ret = -BCH_ERR_fsck_repair_unimplemented;
					goto err;
				}

				ret = commit_do(trans, NULL, NULL, BCH_TRANS_COMMIT_no_enospc,
						check_snapshot_exists(trans, *id));
				if (ret)
					goto err;
			}
		}
	}
fsck_err:
err:
	bch2_trans_put(trans);
	snapshot_tree_reconstruct_exit(&r);
	printbuf_exit(&buf);
	bch_err_fn(c, ret);
	return ret;
}

int bch2_check_key_has_snapshot(struct btree_trans *trans,
				struct btree_iter *iter,
				struct bkey_s_c k)
{
	struct bch_fs *c = trans->c;
	struct printbuf buf = PRINTBUF;
	int ret = 0;

	if (fsck_err_on(!bch2_snapshot_exists(c, k.k->p.snapshot),
			trans, bkey_in_missing_snapshot,
			"key in missing snapshot %s, delete?",
			(bch2_btree_id_to_text(&buf, iter->btree_id),
			 prt_char(&buf, ' '),
			 bch2_bkey_val_to_text(&buf, c, k), buf.buf)))
		ret = bch2_btree_delete_at(trans, iter,
					    BTREE_UPDATE_internal_snapshot_node) ?: 1;
fsck_err:
	printbuf_exit(&buf);
	return ret;
}

/*
 * Mark a snapshot as deleted, for future cleanup:
 */
int bch2_snapshot_node_set_deleted(struct btree_trans *trans, u32 id)
{
	struct btree_iter iter;
	struct bkey_i_snapshot *s =
		bch2_bkey_get_mut_typed(trans, &iter,
				    BTREE_ID_snapshots, POS(0, id),
				    0, snapshot);
	int ret = PTR_ERR_OR_ZERO(s);
	if (unlikely(ret)) {
		bch2_fs_inconsistent_on(bch2_err_matches(ret, ENOENT),
					trans->c, "missing snapshot %u", id);
		return ret;
	}

	/* already deleted? */
	if (BCH_SNAPSHOT_DELETED(&s->v))
		goto err;

	SET_BCH_SNAPSHOT_DELETED(&s->v, true);
	SET_BCH_SNAPSHOT_SUBVOL(&s->v, false);
	s->v.subvol = 0;
err:
	bch2_trans_iter_exit(trans, &iter);
	return ret;
}

static inline void normalize_snapshot_child_pointers(struct bch_snapshot *s)
{
	if (le32_to_cpu(s->children[0]) < le32_to_cpu(s->children[1]))
		swap(s->children[0], s->children[1]);
}

static int bch2_snapshot_node_delete(struct btree_trans *trans, u32 id)
{
	struct bch_fs *c = trans->c;
	struct btree_iter iter, p_iter = {};
	struct btree_iter c_iter = {};
	struct btree_iter tree_iter = {};
	struct bkey_s_c_snapshot s;
	u32 parent_id, child_id;
	unsigned i;
	int ret = 0;

	s = bch2_bkey_get_iter_typed(trans, &iter, BTREE_ID_snapshots, POS(0, id),
				     BTREE_ITER_intent, snapshot);
	ret = bkey_err(s);
	bch2_fs_inconsistent_on(bch2_err_matches(ret, ENOENT), c,
				"missing snapshot %u", id);

	if (ret)
		goto err;

	BUG_ON(s.v->children[1]);

	parent_id = le32_to_cpu(s.v->parent);
	child_id = le32_to_cpu(s.v->children[0]);

	if (parent_id) {
		struct bkey_i_snapshot *parent;

		parent = bch2_bkey_get_mut_typed(trans, &p_iter,
				     BTREE_ID_snapshots, POS(0, parent_id),
				     0, snapshot);
		ret = PTR_ERR_OR_ZERO(parent);
		bch2_fs_inconsistent_on(bch2_err_matches(ret, ENOENT), c,
					"missing snapshot %u", parent_id);
		if (unlikely(ret))
			goto err;

		/* find entry in parent->children for node being deleted */
		for (i = 0; i < 2; i++)
			if (le32_to_cpu(parent->v.children[i]) == id)
				break;

		if (bch2_fs_inconsistent_on(i == 2, c,
					"snapshot %u missing child pointer to %u",
					parent_id, id))
			goto err;

		parent->v.children[i] = cpu_to_le32(child_id);

		normalize_snapshot_child_pointers(&parent->v);
	}

	if (child_id) {
		struct bkey_i_snapshot *child;

		child = bch2_bkey_get_mut_typed(trans, &c_iter,
				     BTREE_ID_snapshots, POS(0, child_id),
				     0, snapshot);
		ret = PTR_ERR_OR_ZERO(child);
		bch2_fs_inconsistent_on(bch2_err_matches(ret, ENOENT), c,
					"missing snapshot %u", child_id);
		if (unlikely(ret))
			goto err;

		child->v.parent = cpu_to_le32(parent_id);

		if (!child->v.parent) {
			child->v.skip[0] = 0;
			child->v.skip[1] = 0;
			child->v.skip[2] = 0;
		}
	}

	if (!parent_id) {
		/*
		 * We're deleting the root of a snapshot tree: update the
		 * snapshot_tree entry to point to the new root, or delete it if
		 * this is the last snapshot ID in this tree:
		 */
		struct bkey_i_snapshot_tree *s_t;

		BUG_ON(s.v->children[1]);

		s_t = bch2_bkey_get_mut_typed(trans, &tree_iter,
				BTREE_ID_snapshot_trees, POS(0, le32_to_cpu(s.v->tree)),
				0, snapshot_tree);
		ret = PTR_ERR_OR_ZERO(s_t);
		if (ret)
			goto err;

		if (s.v->children[0]) {
			s_t->v.root_snapshot = s.v->children[0];
		} else {
			s_t->k.type = KEY_TYPE_deleted;
			set_bkey_val_u64s(&s_t->k, 0);
		}
	}

	ret = bch2_btree_delete_at(trans, &iter, 0);
err:
	bch2_trans_iter_exit(trans, &tree_iter);
	bch2_trans_iter_exit(trans, &p_iter);
	bch2_trans_iter_exit(trans, &c_iter);
	bch2_trans_iter_exit(trans, &iter);
	return ret;
}

static int create_snapids(struct btree_trans *trans, u32 parent, u32 tree,
			  u32 *new_snapids,
			  u32 *snapshot_subvols,
			  unsigned nr_snapids)
{
	struct bch_fs *c = trans->c;
	struct btree_iter iter;
	struct bkey_i_snapshot *n;
	struct bkey_s_c k;
	unsigned i, j;
	u32 depth = bch2_snapshot_depth(c, parent);
	int ret;

	bch2_trans_iter_init(trans, &iter, BTREE_ID_snapshots,
			     POS_MIN, BTREE_ITER_intent);
	k = bch2_btree_iter_peek(trans, &iter);
	ret = bkey_err(k);
	if (ret)
		goto err;

	for (i = 0; i < nr_snapids; i++) {
		k = bch2_btree_iter_prev_slot(trans, &iter);
		ret = bkey_err(k);
		if (ret)
			goto err;

		if (!k.k || !k.k->p.offset) {
			ret = -BCH_ERR_ENOSPC_snapshot_create;
			goto err;
		}

		n = bch2_bkey_alloc(trans, &iter, 0, snapshot);
		ret = PTR_ERR_OR_ZERO(n);
		if (ret)
			goto err;

		n->v.flags	= 0;
		n->v.parent	= cpu_to_le32(parent);
		n->v.subvol	= cpu_to_le32(snapshot_subvols[i]);
		n->v.tree	= cpu_to_le32(tree);
		n->v.depth	= cpu_to_le32(depth);
		n->v.btime.lo	= cpu_to_le64(bch2_current_time(c));
		n->v.btime.hi	= 0;

		for (j = 0; j < ARRAY_SIZE(n->v.skip); j++)
			n->v.skip[j] = cpu_to_le32(bch2_snapshot_skiplist_get(c, parent));

		bubble_sort(n->v.skip, ARRAY_SIZE(n->v.skip), cmp_le32);
		SET_BCH_SNAPSHOT_SUBVOL(&n->v, true);

		ret = __bch2_mark_snapshot(trans, BTREE_ID_snapshots, 0,
					 bkey_s_c_null, bkey_i_to_s_c(&n->k_i), 0);
		if (ret)
			goto err;

		new_snapids[i]	= iter.pos.offset;
	}
err:
	bch2_trans_iter_exit(trans, &iter);
	return ret;
}

/*
 * Create new snapshot IDs as children of an existing snapshot ID:
 */
static int bch2_snapshot_node_create_children(struct btree_trans *trans, u32 parent,
			      u32 *new_snapids,
			      u32 *snapshot_subvols,
			      unsigned nr_snapids)
{
	struct btree_iter iter;
	struct bkey_i_snapshot *n_parent;
	int ret = 0;

	n_parent = bch2_bkey_get_mut_typed(trans, &iter,
			BTREE_ID_snapshots, POS(0, parent),
			0, snapshot);
	ret = PTR_ERR_OR_ZERO(n_parent);
	if (unlikely(ret)) {
		if (bch2_err_matches(ret, ENOENT))
			bch_err(trans->c, "snapshot %u not found", parent);
		return ret;
	}

	if (n_parent->v.children[0] || n_parent->v.children[1]) {
		bch_err(trans->c, "Trying to add child snapshot nodes to parent that already has children");
		ret = -EINVAL;
		goto err;
	}

	ret = create_snapids(trans, parent, le32_to_cpu(n_parent->v.tree),
			     new_snapids, snapshot_subvols, nr_snapids);
	if (ret)
		goto err;

	n_parent->v.children[0] = cpu_to_le32(new_snapids[0]);
	n_parent->v.children[1] = cpu_to_le32(new_snapids[1]);
	n_parent->v.subvol = 0;
	SET_BCH_SNAPSHOT_SUBVOL(&n_parent->v, false);
err:
	bch2_trans_iter_exit(trans, &iter);
	return ret;
}

/*
 * Create a snapshot node that is the root of a new tree:
 */
static int bch2_snapshot_node_create_tree(struct btree_trans *trans,
			      u32 *new_snapids,
			      u32 *snapshot_subvols,
			      unsigned nr_snapids)
{
	struct bkey_i_snapshot_tree *n_tree;
	int ret;

	n_tree = __bch2_snapshot_tree_create(trans);
	ret =   PTR_ERR_OR_ZERO(n_tree) ?:
		create_snapids(trans, 0, n_tree->k.p.offset,
			     new_snapids, snapshot_subvols, nr_snapids);
	if (ret)
		return ret;

	n_tree->v.master_subvol	= cpu_to_le32(snapshot_subvols[0]);
	n_tree->v.root_snapshot	= cpu_to_le32(new_snapids[0]);
	return 0;
}

int bch2_snapshot_node_create(struct btree_trans *trans, u32 parent,
			      u32 *new_snapids,
			      u32 *snapshot_subvols,
			      unsigned nr_snapids)
{
	BUG_ON((parent == 0) != (nr_snapids == 1));
	BUG_ON((parent != 0) != (nr_snapids == 2));

	return parent
		? bch2_snapshot_node_create_children(trans, parent,
				new_snapids, snapshot_subvols, nr_snapids)
		: bch2_snapshot_node_create_tree(trans,
				new_snapids, snapshot_subvols, nr_snapids);

}

/*
 * If we have an unlinked inode in an internal snapshot node, and the inode
 * really has been deleted in all child snapshots, how does this get cleaned up?
 *
 * first there is the problem of how keys that have been overwritten in all
 * child snapshots get deleted (unimplemented?), but inodes may perhaps be
 * special?
 *
 * also: unlinked inode in internal snapshot appears to not be getting deleted
 * correctly if inode doesn't exist in leaf snapshots
 *
 * solution:
 *
 * for a key in an interior snapshot node that needs work to be done that
 * requires it to be mutated: iterate over all descendent leaf nodes and copy
 * that key to snapshot leaf nodes, where we can mutate it
 */

struct snapshot_interior_delete {
	u32	id;
	u32	live_child;
};
typedef DARRAY(struct snapshot_interior_delete) interior_delete_list;

static inline u32 interior_delete_has_id(interior_delete_list *l, u32 id)
{
	darray_for_each(*l, i)
		if (i->id == id)
			return i->live_child;
	return 0;
}

static unsigned __live_child(struct snapshot_table *t, u32 id,
			     snapshot_id_list *delete_leaves,
			     interior_delete_list *delete_interior)
{
	struct snapshot_t *s = __snapshot_t(t, id);
	if (!s)
		return 0;

	for (unsigned i = 0; i < ARRAY_SIZE(s->children); i++)
		if (s->children[i] &&
		    !snapshot_list_has_id(delete_leaves, s->children[i]) &&
		    !interior_delete_has_id(delete_interior, s->children[i]))
			return s->children[i];

	for (unsigned i = 0; i < ARRAY_SIZE(s->children); i++) {
		u32 live_child = s->children[i]
			? __live_child(t, s->children[i], delete_leaves, delete_interior)
			: 0;
		if (live_child)
			return live_child;
	}

	return 0;
}

static unsigned live_child(struct bch_fs *c, u32 id,
			   snapshot_id_list *delete_leaves,
			   interior_delete_list *delete_interior)
{
	rcu_read_lock();
	u32 ret = __live_child(rcu_dereference(c->snapshots), id,
			       delete_leaves, delete_interior);
	rcu_read_unlock();
	return ret;
}

static int delete_dead_snapshots_process_key(struct btree_trans *trans,
					     struct btree_iter *iter,
					     struct bkey_s_c k,
					     snapshot_id_list *delete_leaves,
					     interior_delete_list *delete_interior)
{
	if (snapshot_list_has_id(delete_leaves, k.k->p.snapshot))
		return bch2_btree_delete_at(trans, iter,
					    BTREE_UPDATE_internal_snapshot_node);

	u32 live_child = interior_delete_has_id(delete_interior, k.k->p.snapshot);
	if (live_child) {
		struct bkey_i *new = bch2_bkey_make_mut_noupdate(trans, k);
		int ret = PTR_ERR_OR_ZERO(new);
		if (ret)
			return ret;

		new->k.p.snapshot = live_child;

		struct btree_iter dst_iter;
		struct bkey_s_c dst_k = bch2_bkey_get_iter(trans, &dst_iter,
							   iter->btree_id, new->k.p,
							   BTREE_ITER_all_snapshots|
							   BTREE_ITER_intent);
		ret = bkey_err(dst_k);
		if (ret)
			return ret;

		ret =   (bkey_deleted(dst_k.k)
			 ? bch2_trans_update(trans, &dst_iter, new,
					     BTREE_UPDATE_internal_snapshot_node)
			 : 0) ?:
			bch2_btree_delete_at(trans, iter,
					     BTREE_UPDATE_internal_snapshot_node);
		bch2_trans_iter_exit(trans, &dst_iter);
		return ret;
	}

	return 0;
}

/*
 * For a given snapshot, if it doesn't have a subvolume that points to it, and
 * it doesn't have child snapshot nodes - it's now redundant and we can mark it
 * as deleted.
 */
static int check_should_delete_snapshot(struct btree_trans *trans, struct bkey_s_c k,
					snapshot_id_list *delete_leaves,
					interior_delete_list *delete_interior)
{
	if (k.k->type != KEY_TYPE_snapshot)
		return 0;

	struct bch_fs *c = trans->c;
	struct bkey_s_c_snapshot s = bkey_s_c_to_snapshot(k);
	unsigned live_children = 0;

	if (BCH_SNAPSHOT_SUBVOL(s.v))
		return 0;

	for (unsigned i = 0; i < 2; i++) {
		u32 child = le32_to_cpu(s.v->children[i]);

		live_children += child &&
			!snapshot_list_has_id(delete_leaves, child);
	}

	if (live_children == 0) {
		return snapshot_list_add(c, delete_leaves, s.k->p.offset);
	} else if (live_children == 1) {
		struct snapshot_interior_delete d = {
			.id		= s.k->p.offset,
			.live_child	= live_child(c, s.k->p.offset, delete_leaves, delete_interior),
		};

		if (!d.live_child) {
			bch_err(c, "error finding live child of snapshot %u", d.id);
			return -EINVAL;
		}

		return darray_push(delete_interior, d);
	} else {
		return 0;
	}
}

static inline u32 bch2_snapshot_nth_parent_skip(struct bch_fs *c, u32 id, u32 n,
						interior_delete_list *skip)
{
	rcu_read_lock();
	while (interior_delete_has_id(skip, id))
		id = __bch2_snapshot_parent(c, id);

	while (n--) {
		do {
			id = __bch2_snapshot_parent(c, id);
		} while (interior_delete_has_id(skip, id));
	}
	rcu_read_unlock();

	return id;
}

static int bch2_fix_child_of_deleted_snapshot(struct btree_trans *trans,
					      struct btree_iter *iter, struct bkey_s_c k,
					      interior_delete_list *deleted)
{
	struct bch_fs *c = trans->c;
	u32 nr_deleted_ancestors = 0;
	struct bkey_i_snapshot *s;
	int ret;

	if (k.k->type != KEY_TYPE_snapshot)
		return 0;

	if (interior_delete_has_id(deleted, k.k->p.offset))
		return 0;

	s = bch2_bkey_make_mut_noupdate_typed(trans, k, snapshot);
	ret = PTR_ERR_OR_ZERO(s);
	if (ret)
		return ret;

	darray_for_each(*deleted, i)
		nr_deleted_ancestors += bch2_snapshot_is_ancestor(c, s->k.p.offset, i->id);

	if (!nr_deleted_ancestors)
		return 0;

	le32_add_cpu(&s->v.depth, -nr_deleted_ancestors);

	if (!s->v.depth) {
		s->v.skip[0] = 0;
		s->v.skip[1] = 0;
		s->v.skip[2] = 0;
	} else {
		u32 depth = le32_to_cpu(s->v.depth);
		u32 parent = bch2_snapshot_parent(c, s->k.p.offset);

		for (unsigned j = 0; j < ARRAY_SIZE(s->v.skip); j++) {
			u32 id = le32_to_cpu(s->v.skip[j]);

			if (interior_delete_has_id(deleted, id)) {
				id = bch2_snapshot_nth_parent_skip(c,
							parent,
							depth > 1
							? get_random_u32_below(depth - 1)
							: 0,
							deleted);
				s->v.skip[j] = cpu_to_le32(id);
			}
		}

		bubble_sort(s->v.skip, ARRAY_SIZE(s->v.skip), cmp_le32);
	}

	return bch2_trans_update(trans, iter, &s->k_i, 0);
}

int bch2_delete_dead_snapshots(struct bch_fs *c)
{
	if (!test_and_clear_bit(BCH_FS_need_delete_dead_snapshots, &c->flags))
		return 0;

	struct btree_trans *trans = bch2_trans_get(c);
	snapshot_id_list delete_leaves = {};
	interior_delete_list delete_interior = {};
	int ret = 0;

	/*
	 * For every snapshot node: If we have no live children and it's not
	 * pointed to by a subvolume, delete it:
	 */
	ret = for_each_btree_key(trans, iter, BTREE_ID_snapshots, POS_MIN, 0, k,
		check_should_delete_snapshot(trans, k, &delete_leaves, &delete_interior));
	if (!bch2_err_matches(ret, EROFS))
		bch_err_msg(c, ret, "walking snapshots");
	if (ret)
		goto err;

	if (!delete_leaves.nr && !delete_interior.nr)
		goto err;

	{
		struct printbuf buf = PRINTBUF;
		prt_printf(&buf, "deleting leaves");
		darray_for_each(delete_leaves, i)
			prt_printf(&buf, " %u", *i);

		prt_printf(&buf, " interior");
		darray_for_each(delete_interior, i)
			prt_printf(&buf, " %u->%u", i->id, i->live_child);

		ret = commit_do(trans, NULL, NULL, 0, bch2_trans_log_msg(trans, &buf));
		printbuf_exit(&buf);
		if (ret)
			goto err;
	}

	for (unsigned btree = 0; btree < BTREE_ID_NR; btree++) {
		struct disk_reservation res = { 0 };

		if (!btree_type_has_snapshots(btree))
			continue;

		ret = for_each_btree_key_commit(trans, iter,
				btree, POS_MIN,
				BTREE_ITER_prefetch|BTREE_ITER_all_snapshots, k,
				&res, NULL, BCH_TRANS_COMMIT_no_enospc,
			delete_dead_snapshots_process_key(trans, &iter, k,
							  &delete_leaves,
							  &delete_interior));

		bch2_disk_reservation_put(c, &res);

		if (!bch2_err_matches(ret, EROFS))
			bch_err_msg(c, ret, "deleting keys from dying snapshots");
		if (ret)
			goto err;
	}

	darray_for_each(delete_leaves, i) {
		ret = commit_do(trans, NULL, NULL, 0,
			bch2_snapshot_node_delete(trans, *i));
		if (!bch2_err_matches(ret, EROFS))
			bch_err_msg(c, ret, "deleting snapshot %u", *i);
		if (ret)
			goto err;
	}

	/*
	 * Fixing children of deleted snapshots can't be done completely
	 * atomically, if we crash between here and when we delete the interior
	 * nodes some depth fields will be off:
	 */
	ret = for_each_btree_key_commit(trans, iter, BTREE_ID_snapshots, POS_MIN,
				  BTREE_ITER_intent, k,
				  NULL, NULL, BCH_TRANS_COMMIT_no_enospc,
		bch2_fix_child_of_deleted_snapshot(trans, &iter, k, &delete_interior));
	if (ret)
		goto err;

	darray_for_each(delete_interior, i) {
		ret = commit_do(trans, NULL, NULL, 0,
			bch2_snapshot_node_delete(trans, i->id));
		if (!bch2_err_matches(ret, EROFS))
			bch_err_msg(c, ret, "deleting snapshot %u", i->id);
		if (ret)
			goto err;
	}
err:
	darray_exit(&delete_interior);
	darray_exit(&delete_leaves);
	bch2_trans_put(trans);
	if (!bch2_err_matches(ret, EROFS))
		bch_err_fn(c, ret);
	return ret;
}

void bch2_delete_dead_snapshots_work(struct work_struct *work)
{
	struct bch_fs *c = container_of(work, struct bch_fs, snapshot_delete_work);

	set_worker_desc("bcachefs-delete-dead-snapshots/%s", c->name);

	bch2_delete_dead_snapshots(c);
	bch2_write_ref_put(c, BCH_WRITE_REF_delete_dead_snapshots);
}

void bch2_delete_dead_snapshots_async(struct bch_fs *c)
{
	if (!bch2_write_ref_tryget(c, BCH_WRITE_REF_delete_dead_snapshots))
		return;

	BUG_ON(!test_bit(BCH_FS_may_go_rw, &c->flags));

	if (!queue_work(c->write_ref_wq, &c->snapshot_delete_work))
		bch2_write_ref_put(c, BCH_WRITE_REF_delete_dead_snapshots);
}

int __bch2_key_has_snapshot_overwrites(struct btree_trans *trans,
				       enum btree_id id,
				       struct bpos pos)
{
	struct bch_fs *c = trans->c;
	struct btree_iter iter;
	struct bkey_s_c k;
	int ret;

	for_each_btree_key_reverse_norestart(trans, iter, id, bpos_predecessor(pos),
					     BTREE_ITER_not_extents|
					     BTREE_ITER_all_snapshots,
					     k, ret) {
		if (!bkey_eq(pos, k.k->p))
			break;

		if (bch2_snapshot_is_ancestor(c, k.k->p.snapshot, pos.snapshot)) {
			ret = 1;
			break;
		}
	}
	bch2_trans_iter_exit(trans, &iter);

	return ret;
}

static bool interior_snapshot_needs_delete(struct bkey_s_c_snapshot snap)
{
	/* If there's one child, it's redundant and keys will be moved to the child */
	return !!snap.v->children[0] + !!snap.v->children[1] == 1;
}

static int bch2_check_snapshot_needs_deletion(struct btree_trans *trans, struct bkey_s_c k)
{
	if (k.k->type != KEY_TYPE_snapshot)
		return 0;

	struct bkey_s_c_snapshot snap = bkey_s_c_to_snapshot(k);
	if (BCH_SNAPSHOT_DELETED(snap.v) ||
	    interior_snapshot_needs_delete(snap))
		set_bit(BCH_FS_need_delete_dead_snapshots, &trans->c->flags);

	return 0;
}

int bch2_snapshots_read(struct bch_fs *c)
{
	/*
	 * Initializing the is_ancestor bitmaps requires ancestors to already be
	 * initialized - so mark in reverse:
	 */
	int ret = bch2_trans_run(c,
		for_each_btree_key_reverse(trans, iter, BTREE_ID_snapshots,
				   POS_MAX, 0, k,
			__bch2_mark_snapshot(trans, BTREE_ID_snapshots, 0, bkey_s_c_null, k, 0) ?:
			bch2_check_snapshot_needs_deletion(trans, k)));
	bch_err_fn(c, ret);

	/*
	 * It's important that we check if we need to reconstruct snapshots
	 * before going RW, so we mark that pass as required in the superblock -
	 * otherwise, we could end up deleting keys with missing snapshot nodes
	 * instead
	 */
	BUG_ON(!test_bit(BCH_FS_new_fs, &c->flags) &&
	       test_bit(BCH_FS_may_go_rw, &c->flags));

	if (bch2_err_matches(ret, EIO) ||
	    (c->sb.btrees_lost_data & BIT_ULL(BTREE_ID_snapshots)))
		ret = bch2_run_explicit_recovery_pass_persistent(c, BCH_RECOVERY_PASS_reconstruct_snapshots);

	return ret;
}

void bch2_fs_snapshots_exit(struct bch_fs *c)
{
	kvfree(rcu_dereference_protected(c->snapshots, true));
}
