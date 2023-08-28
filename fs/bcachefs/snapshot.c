// SPDX-License-Identifier: GPL-2.0

#include "bcachefs.h"
#include "bkey_buf.h"
#include "btree_key_cache.h"
#include "btree_update.h"
#include "buckets.h"
#include "errcode.h"
#include "error.h"
#include "fs.h"
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

int bch2_snapshot_tree_invalid(const struct bch_fs *c, struct bkey_s_c k,
			       enum bkey_invalid_flags flags,
			       struct printbuf *err)
{
	if (bkey_gt(k.k->p, POS(0, U32_MAX)) ||
	    bkey_lt(k.k->p, POS(0, 1))) {
		prt_printf(err, "bad pos");
		return -BCH_ERR_invalid_bkey;
	}

	return 0;
}

int bch2_snapshot_tree_lookup(struct btree_trans *trans, u32 id,
			      struct bch_snapshot_tree *s)
{
	int ret = bch2_bkey_get_val_typed(trans, BTREE_ID_snapshot_trees, POS(0, id),
					  BTREE_ITER_WITH_UPDATES, snapshot_tree, s);

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

static bool bch2_snapshot_is_ancestor_early(struct bch_fs *c, u32 id, u32 ancestor)
{
	struct snapshot_table *t;

	rcu_read_lock();
	t = rcu_dereference(c->snapshots);

	while (id && id < ancestor)
		id = __snapshot_t(t, id)->parent;
	rcu_read_unlock();

	return id == ancestor;
}

static inline u32 get_ancestor_below(struct snapshot_table *t, u32 id, u32 ancestor)
{
	const struct snapshot_t *s = __snapshot_t(t, id);

	if (s->skip[2] <= ancestor)
		return s->skip[2];
	if (s->skip[1] <= ancestor)
		return s->skip[1];
	if (s->skip[0] <= ancestor)
		return s->skip[0];
	return s->parent;
}

bool __bch2_snapshot_is_ancestor(struct bch_fs *c, u32 id, u32 ancestor)
{
	struct snapshot_table *t;
	bool ret;

	EBUG_ON(c->curr_recovery_pass <= BCH_RECOVERY_PASS_check_snapshots);

	rcu_read_lock();
	t = rcu_dereference(c->snapshots);

	while (id && id < ancestor - IS_ANCESTOR_BITMAP)
		id = get_ancestor_below(t, id, ancestor);

	if (id && id < ancestor) {
		ret = test_bit(ancestor - id - 1, __snapshot_t(t, id)->is_ancestor);

		EBUG_ON(ret != bch2_snapshot_is_ancestor_early(c, id, ancestor));
	} else {
		ret = id == ancestor;
	}

	rcu_read_unlock();

	return ret;
}

struct snapshot_t_free_rcu {
	struct rcu_head		rcu;
	struct snapshot_table	*t;
};

static void snapshot_t_free_rcu(struct rcu_head *rcu)
{
	struct snapshot_t_free_rcu *free_rcu =
		container_of(rcu, struct snapshot_t_free_rcu, rcu);

	kvfree(free_rcu->t);
	kfree(free_rcu);
}

static noinline struct snapshot_t *__snapshot_t_mut(struct bch_fs *c, u32 id)
{
	size_t idx = U32_MAX - id;
	size_t new_size;
	struct snapshot_table *new, *old;

	new_size = max(16UL, roundup_pow_of_two(idx + 1));

	new = kvzalloc(struct_size(new, s, new_size), GFP_KERNEL);
	if (!new)
		return NULL;

	old = rcu_dereference_protected(c->snapshots, true);
	if (old)
		memcpy(new->s,
		       rcu_dereference_protected(c->snapshots, true)->s,
		       sizeof(new->s[0]) * c->snapshot_table_size);

	rcu_assign_pointer(c->snapshots, new);
	c->snapshot_table_size = new_size;
	if (old) {
		struct snapshot_t_free_rcu *rcu =
			kmalloc(sizeof(*rcu), GFP_KERNEL|__GFP_NOFAIL);

		rcu->t = old;
		call_rcu(&rcu->rcu, snapshot_t_free_rcu);
	}

	return &rcu_dereference_protected(c->snapshots, true)->s[idx];
}

static inline struct snapshot_t *snapshot_t_mut(struct bch_fs *c, u32 id)
{
	size_t idx = U32_MAX - id;

	lockdep_assert_held(&c->snapshot_table_lock);

	if (likely(idx < c->snapshot_table_size))
		return &rcu_dereference_protected(c->snapshots, true)->s[idx];

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

int bch2_snapshot_invalid(const struct bch_fs *c, struct bkey_s_c k,
			  enum bkey_invalid_flags flags,
			  struct printbuf *err)
{
	struct bkey_s_c_snapshot s;
	u32 i, id;

	if (bkey_gt(k.k->p, POS(0, U32_MAX)) ||
	    bkey_lt(k.k->p, POS(0, 1))) {
		prt_printf(err, "bad pos");
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

	if (bkey_val_bytes(k.k) > offsetof(struct bch_snapshot, skip)) {
		if (le32_to_cpu(s.v->skip[0]) > le32_to_cpu(s.v->skip[1]) ||
		    le32_to_cpu(s.v->skip[1]) > le32_to_cpu(s.v->skip[2])) {
			prt_printf(err, "skiplist not normalized");
			return -BCH_ERR_invalid_bkey;
		}

		for (i = 0; i < ARRAY_SIZE(s.v->skip); i++) {
			id = le32_to_cpu(s.v->skip[i]);

			if ((id && !s.v->parent) ||
			    (id && id <= k.k->p.offset)) {
				prt_printf(err, "bad skiplist node %u", id);
				return -BCH_ERR_invalid_bkey;
			}
		}
	}

	return 0;
}

static void __set_is_ancestor_bitmap(struct bch_fs *c, u32 id)
{
	struct snapshot_t *t = snapshot_t_mut(c, id);
	u32 parent = id;

	while ((parent = bch2_snapshot_parent_early(c, parent)) &&
	       parent - id - 1 < IS_ANCESTOR_BITMAP)
		__set_bit(parent - id - 1, t->is_ancestor);
}

static void set_is_ancestor_bitmap(struct bch_fs *c, u32 id)
{
	mutex_lock(&c->snapshot_table_lock);
	__set_is_ancestor_bitmap(c, id);
	mutex_unlock(&c->snapshot_table_lock);
}

int bch2_mark_snapshot(struct btree_trans *trans,
		       enum btree_id btree, unsigned level,
		       struct bkey_s_c old, struct bkey_s_c new,
		       unsigned flags)
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

		__set_is_ancestor_bitmap(c, id);

		if (BCH_SNAPSHOT_DELETED(s.v)) {
			set_bit(BCH_FS_HAVE_DELETED_SNAPSHOTS, &c->flags);
			c->recovery_passes_explicit |= BIT_ULL(BCH_RECOVERY_PASS_delete_dead_snapshots);
		}
	} else {
		memset(t, 0, sizeof(*t));
	}
err:
	mutex_unlock(&c->snapshot_table_lock);
	return ret;
}

int bch2_snapshot_lookup(struct btree_trans *trans, u32 id,
			 struct bch_snapshot *s)
{
	return bch2_bkey_get_val_typed(trans, BTREE_ID_snapshots, POS(0, id),
				       BTREE_ITER_WITH_UPDATES, snapshot, s);
}

int bch2_snapshot_live(struct btree_trans *trans, u32 id)
{
	struct bch_snapshot v;
	int ret;

	if (!id)
		return 0;

	ret = bch2_snapshot_lookup(trans, id, &v);
	if (bch2_err_matches(ret, ENOENT))
		bch_err(trans->c, "snapshot node %u not found", id);
	if (ret)
		return ret;

	return !BCH_SNAPSHOT_DELETED(&v);
}

/*
 * If @k is a snapshot with just one live child, it's part of a linear chain,
 * which we consider to be an equivalence class: and then after snapshot
 * deletion cleanup, there should only be a single key at a given position in
 * this equivalence class.
 *
 * This sets the equivalence class of @k to be the child's equivalence class, if
 * it's part of such a linear chain: this correctly sets equivalence classes on
 * startup if we run leaf to root (i.e. in natural key order).
 */
int bch2_snapshot_set_equiv(struct btree_trans *trans, struct bkey_s_c k)
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
		int ret = bch2_snapshot_live(trans, child[i]);

		if (ret < 0)
			return ret;

		if (ret)
			live_idx = i;
		nr_live += ret;
	}

	mutex_lock(&c->snapshot_table_lock);

	snapshot_t_mut(c, id)->equiv = nr_live == 1
		? snapshot_t_mut(c, child[live_idx])->equiv
		: id;

	mutex_unlock(&c->snapshot_table_lock);

	return 0;
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

static u32 bch2_snapshot_tree_oldest_subvol(struct bch_fs *c, u32 snapshot_root)
{
	u32 id = snapshot_root;
	u32 subvol = 0, s;

	while (id) {
		s = snapshot_t(c, id)->subvol;

		if (s && (!subvol || s < subvol))
			subvol = s;

		id = bch2_snapshot_tree_next(c, id);
	}

	return subvol;
}

static int bch2_snapshot_tree_master_subvol(struct btree_trans *trans,
					    u32 snapshot_root, u32 *subvol_id)
{
	struct bch_fs *c = trans->c;
	struct btree_iter iter;
	struct bkey_s_c k;
	struct bkey_s_c_subvolume s;
	bool found = false;
	int ret;

	for_each_btree_key_norestart(trans, iter, BTREE_ID_subvolumes, POS_MIN,
				     0, k, ret) {
		if (k.k->type != KEY_TYPE_subvolume)
			continue;

		s = bkey_s_c_to_subvolume(k);
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
		struct bkey_i_subvolume *s;

		*subvol_id = bch2_snapshot_tree_oldest_subvol(c, snapshot_root);

		s = bch2_bkey_get_mut_typed(trans, &iter,
					    BTREE_ID_subvolumes, POS(0, *subvol_id),
					    0, subvolume);
		ret = PTR_ERR_OR_ZERO(s);
		if (ret)
			return ret;

		SET_BCH_SUBVOLUME_SNAP(&s->v, false);
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
	u32 root_id;
	int ret;

	if (k.k->type != KEY_TYPE_snapshot_tree)
		return 0;

	st = bkey_s_c_to_snapshot_tree(k);
	root_id = le32_to_cpu(st.v->root_snapshot);

	ret = bch2_snapshot_lookup(trans, root_id, &s);
	if (ret && !bch2_err_matches(ret, ENOENT))
		goto err;

	if (fsck_err_on(ret ||
			root_id != bch2_snapshot_root(c, root_id) ||
			st.k->p.offset != le32_to_cpu(s.tree),
			c,
			"snapshot tree points to missing/incorrect snapshot:\n  %s",
			(bch2_bkey_val_to_text(&buf, c, st.s_c), buf.buf))) {
		ret = bch2_btree_delete_at(trans, iter, 0);
		goto err;
	}

	ret = bch2_subvolume_get(trans, le32_to_cpu(st.v->master_subvol),
				 false, 0, &subvol);
	if (ret && !bch2_err_matches(ret, ENOENT))
		goto err;

	if (fsck_err_on(ret, c,
			"snapshot tree points to missing subvolume:\n  %s",
			(printbuf_reset(&buf),
			 bch2_bkey_val_to_text(&buf, c, st.s_c), buf.buf)) ||
	    fsck_err_on(!bch2_snapshot_is_ancestor_early(c,
						le32_to_cpu(subvol.snapshot),
						root_id), c,
			"snapshot tree points to subvolume that does not point to snapshot in this tree:\n  %s",
			(printbuf_reset(&buf),
			 bch2_bkey_val_to_text(&buf, c, st.s_c), buf.buf)) ||
	    fsck_err_on(BCH_SUBVOLUME_SNAP(&subvol), c,
			"snapshot tree points to snapshot subvolume:\n  %s",
			(printbuf_reset(&buf),
			 bch2_bkey_val_to_text(&buf, c, st.s_c), buf.buf))) {
		struct bkey_i_snapshot_tree *u;
		u32 subvol_id;

		ret = bch2_snapshot_tree_master_subvol(trans, root_id, &subvol_id);
		if (ret)
			goto err;

		u = bch2_bkey_make_mut_typed(trans, iter, &k, 0, snapshot_tree);
		ret = PTR_ERR_OR_ZERO(u);
		if (ret)
			goto err;

		u->v.master_subvol = cpu_to_le32(subvol_id);
		st = snapshot_tree_i_to_s_c(u);
	}
err:
fsck_err:
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
	struct btree_iter iter;
	struct bkey_s_c k;
	int ret;

	ret = bch2_trans_run(c,
		for_each_btree_key_commit(&trans, iter,
			BTREE_ID_snapshot_trees, POS_MIN,
			BTREE_ITER_PREFETCH, k,
			NULL, NULL, BTREE_INSERT_LAZY_RW|BTREE_INSERT_NOFAIL,
		check_snapshot_tree(&trans, &iter, k)));

	if (ret)
		bch_err(c, "error %i checking snapshot trees", ret);
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
			       BTREE_ITER_WITH_UPDATES, snapshot);
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
	bool should_have_subvol;
	u32 i, id;
	int ret = 0;

	if (k.k->type != KEY_TYPE_snapshot)
		return 0;

	memset(&s, 0, sizeof(s));
	memcpy(&s, k.v, bkey_val_bytes(k.k));

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

	should_have_subvol = BCH_SNAPSHOT_SUBVOL(&s) &&
		!BCH_SNAPSHOT_DELETED(&s);

	if (should_have_subvol) {
		id = le32_to_cpu(s.subvol);
		ret = bch2_subvolume_get(trans, id, 0, false, &subvol);
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
		if (fsck_err_on(s.subvol, c, "snapshot should not point to subvol:\n  %s",
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

	if (fsck_err_on(!ret, c, "snapshot points to missing/incorrect tree:\n  %s",
			(bch2_bkey_val_to_text(&buf, c, k), buf.buf))) {
		ret = snapshot_tree_ptr_repair(trans, iter, k, &s);
		if (ret)
			goto err;
	}
	ret = 0;

	real_depth = bch2_snapshot_depth(c, parent_id);

	if (le32_to_cpu(s.depth) != real_depth &&
	    (c->sb.version_upgrade_complete < bcachefs_metadata_version_snapshot_skiplists ||
	     fsck_err(c, "snapshot with incorrect depth field, should be %u:\n  %s",
		      real_depth, (bch2_bkey_val_to_text(&buf, c, k), buf.buf)))) {
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

	if (!ret &&
	    (c->sb.version_upgrade_complete < bcachefs_metadata_version_snapshot_skiplists ||
	     fsck_err(c, "snapshot with bad skiplist field:\n  %s",
		      (bch2_bkey_val_to_text(&buf, c, k), buf.buf)))) {
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
	struct btree_iter iter;
	struct bkey_s_c k;
	int ret;

	/*
	 * We iterate backwards as checking/fixing the depth field requires that
	 * the parent's depth already be correct:
	 */
	ret = bch2_trans_run(c,
		for_each_btree_key_reverse_commit(&trans, iter,
			BTREE_ID_snapshots, POS_MAX,
			BTREE_ITER_PREFETCH, k,
			NULL, NULL, BTREE_INSERT_LAZY_RW|BTREE_INSERT_NOFAIL,
		check_snapshot(&trans, &iter, k)));
	if (ret)
		bch_err_fn(c, ret);
	return ret;
}

/*
 * Mark a snapshot as deleted, for future cleanup:
 */
int bch2_snapshot_node_set_deleted(struct btree_trans *trans, u32 id)
{
	struct btree_iter iter;
	struct bkey_i_snapshot *s;
	int ret = 0;

	s = bch2_bkey_get_mut_typed(trans, &iter,
				    BTREE_ID_snapshots, POS(0, id),
				    0, snapshot);
	ret = PTR_ERR_OR_ZERO(s);
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

int bch2_snapshot_node_delete(struct btree_trans *trans, u32 id)
{
	struct bch_fs *c = trans->c;
	struct btree_iter iter, p_iter = (struct btree_iter) { NULL };
	struct btree_iter c_iter = (struct btree_iter) { NULL };
	struct btree_iter tree_iter = (struct btree_iter) { NULL };
	struct bkey_s_c_snapshot s;
	u32 parent_id, child_id;
	unsigned i;
	int ret = 0;

	s = bch2_bkey_get_iter_typed(trans, &iter, BTREE_ID_snapshots, POS(0, id),
				     BTREE_ITER_INTENT, snapshot);
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

		parent->v.children[i] = le32_to_cpu(child_id);

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

		n = bch2_bkey_alloc(trans, &iter, 0, snapshot);
		ret = PTR_ERR_OR_ZERO(n);
		if (ret)
			goto err;

		n->v.flags	= 0;
		n->v.parent	= cpu_to_le32(parent);
		n->v.subvol	= cpu_to_le32(snapshot_subvols[i]);
		n->v.tree	= cpu_to_le32(tree);
		n->v.depth	= cpu_to_le32(depth);

		for (j = 0; j < ARRAY_SIZE(n->v.skip); j++)
			n->v.skip[j] = cpu_to_le32(bch2_snapshot_skiplist_get(c, parent));

		bubble_sort(n->v.skip, ARRAY_SIZE(n->v.skip), cmp_le32);
		SET_BCH_SNAPSHOT_SUBVOL(&n->v, true);

		ret = bch2_mark_snapshot(trans, BTREE_ID_snapshots, 0,
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

static int snapshot_delete_key(struct btree_trans *trans,
			       struct btree_iter *iter,
			       struct bkey_s_c k,
			       snapshot_id_list *deleted,
			       snapshot_id_list *equiv_seen,
			       struct bpos *last_pos)
{
	struct bch_fs *c = trans->c;
	u32 equiv = bch2_snapshot_equiv(c, k.k->p.snapshot);

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

static int move_key_to_correct_snapshot(struct btree_trans *trans,
			       struct btree_iter *iter,
			       struct bkey_s_c k)
{
	struct bch_fs *c = trans->c;
	u32 equiv = bch2_snapshot_equiv(c, k.k->p.snapshot);

	/*
	 * When we have a linear chain of snapshot nodes, we consider
	 * those to form an equivalence class: we're going to collapse
	 * them all down to a single node, and keep the leaf-most node -
	 * which has the same id as the equivalence class id.
	 *
	 * If there are multiple keys in different snapshots at the same
	 * position, we're only going to keep the one in the newest
	 * snapshot - the rest have been overwritten and are redundant,
	 * and for the key we're going to keep we need to move it to the
	 * equivalance class ID if it's not there already.
	 */
	if (equiv != k.k->p.snapshot) {
		struct bkey_i *new = bch2_bkey_make_mut_noupdate(trans, k);
		struct btree_iter new_iter;
		int ret;

		ret = PTR_ERR_OR_ZERO(new);
		if (ret)
			return ret;

		new->k.p.snapshot = equiv;

		bch2_trans_iter_init(trans, &new_iter, iter->btree_id, new->k.p,
				     BTREE_ITER_ALL_SNAPSHOTS|
				     BTREE_ITER_CACHED|
				     BTREE_ITER_INTENT);

		ret =   bch2_btree_iter_traverse(&new_iter) ?:
			bch2_trans_update(trans, &new_iter, new,
					BTREE_UPDATE_INTERNAL_SNAPSHOT_NODE) ?:
			bch2_btree_delete_at(trans, iter,
					BTREE_UPDATE_INTERNAL_SNAPSHOT_NODE);
		bch2_trans_iter_exit(trans, &new_iter);
		if (ret)
			return ret;
	}

	return 0;
}

/*
 * For a given snapshot, if it doesn't have a subvolume that points to it, and
 * it doesn't have child snapshot nodes - it's now redundant and we can mark it
 * as deleted.
 */
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

	ret   = bch2_snapshot_live(trans, children[0]) ?:
		bch2_snapshot_live(trans, children[1]);
	if (ret < 0)
		return ret;

	if (!ret)
		return bch2_snapshot_node_set_deleted(trans, k.k->p.offset);
	return 0;
}

static inline u32 bch2_snapshot_nth_parent_skip(struct bch_fs *c, u32 id, u32 n,
						snapshot_id_list *skip)
{
	rcu_read_lock();
	while (n--) {
		do {
			id = __bch2_snapshot_parent(c, id);
		} while (snapshot_list_has_id(skip, id));
	}
	rcu_read_unlock();

	return id;
}

static int bch2_fix_child_of_deleted_snapshot(struct btree_trans *trans,
					      struct btree_iter *iter, struct bkey_s_c k,
					      snapshot_id_list *deleted)
{
	struct bch_fs *c = trans->c;
	u32 nr_deleted_ancestors = 0;
	struct bkey_i_snapshot *s;
	u32 *i;
	int ret;

	if (k.k->type != KEY_TYPE_snapshot)
		return 0;

	if (snapshot_list_has_id(deleted, k.k->p.offset))
		return 0;

	s = bch2_bkey_make_mut_noupdate_typed(trans, k, snapshot);
	ret = PTR_ERR_OR_ZERO(s);
	if (ret)
		return ret;

	darray_for_each(*deleted, i)
		nr_deleted_ancestors += bch2_snapshot_is_ancestor(c, s->k.p.offset, *i);

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

			if (snapshot_list_has_id(deleted, id)) {
				id = depth > 1
					? bch2_snapshot_nth_parent_skip(c,
							parent,
							get_random_u32_below(depth - 1),
							deleted)
					: parent;
				s->v.skip[j] = cpu_to_le32(id);
			}
		}

		bubble_sort(s->v.skip, ARRAY_SIZE(s->v.skip), cmp_le32);
	}

	return bch2_trans_update(trans, iter, &s->k_i, 0);
}

int bch2_delete_dead_snapshots(struct bch_fs *c)
{
	struct btree_trans trans;
	struct btree_iter iter;
	struct bkey_s_c k;
	struct bkey_s_c_snapshot snap;
	snapshot_id_list deleted = { 0 };
	snapshot_id_list deleted_interior = { 0 };
	u32 *i, id;
	int ret = 0;

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
		bch_err_msg(c, ret, "walking snapshots");
		goto err;
	}

	for (id = 0; id < BTREE_ID_NR; id++) {
		struct bpos last_pos = POS_MIN;
		snapshot_id_list equiv_seen = { 0 };
		struct disk_reservation res = { 0 };

		if (!btree_type_has_snapshots(id))
			continue;

		ret = for_each_btree_key_commit(&trans, iter,
				id, POS_MIN,
				BTREE_ITER_PREFETCH|BTREE_ITER_ALL_SNAPSHOTS, k,
				&res, NULL, BTREE_INSERT_NOFAIL,
			snapshot_delete_key(&trans, &iter, k, &deleted, &equiv_seen, &last_pos)) ?:
		      for_each_btree_key_commit(&trans, iter,
				id, POS_MIN,
				BTREE_ITER_PREFETCH|BTREE_ITER_ALL_SNAPSHOTS, k,
				&res, NULL, BTREE_INSERT_NOFAIL,
			move_key_to_correct_snapshot(&trans, &iter, k));

		bch2_disk_reservation_put(c, &res);
		darray_exit(&equiv_seen);

		if (ret) {
			bch_err_msg(c, ret, "deleting keys from dying snapshots");
			goto err;
		}
	}

	for_each_btree_key(&trans, iter, BTREE_ID_snapshots,
			   POS_MIN, 0, k, ret) {
		u32 snapshot = k.k->p.offset;
		u32 equiv = bch2_snapshot_equiv(c, snapshot);

		if (equiv != snapshot)
			snapshot_list_add(c, &deleted_interior, snapshot);
	}
	bch2_trans_iter_exit(&trans, &iter);

	/*
	 * Fixing children of deleted snapshots can't be done completely
	 * atomically, if we crash between here and when we delete the interior
	 * nodes some depth fields will be off:
	 */
	ret = for_each_btree_key_commit(&trans, iter, BTREE_ID_snapshots, POS_MIN,
				  BTREE_ITER_INTENT, k,
				  NULL, NULL, BTREE_INSERT_NOFAIL,
		bch2_fix_child_of_deleted_snapshot(&trans, &iter, k, &deleted_interior));
	if (ret)
		goto err;

	darray_for_each(deleted, i) {
		ret = commit_do(&trans, NULL, NULL, 0,
			bch2_snapshot_node_delete(&trans, *i));
		if (ret) {
			bch_err_msg(c, ret, "deleting snapshot %u", *i);
			goto err;
		}
	}

	darray_for_each(deleted_interior, i) {
		ret = commit_do(&trans, NULL, NULL, 0,
			bch2_snapshot_node_delete(&trans, *i));
		if (ret) {
			bch_err_msg(c, ret, "deleting snapshot %u", *i);
			goto err;
		}
	}

	clear_bit(BCH_FS_HAVE_DELETED_SNAPSHOTS, &c->flags);
err:
	darray_exit(&deleted_interior);
	darray_exit(&deleted);
	bch2_trans_exit(&trans);
	if (ret)
		bch_err_fn(c, ret);
	return ret;
}

void bch2_delete_dead_snapshots_work(struct work_struct *work)
{
	struct bch_fs *c = container_of(work, struct bch_fs, snapshot_delete_work);

	if (test_bit(BCH_FS_HAVE_DELETED_SNAPSHOTS, &c->flags))
		bch2_delete_dead_snapshots(c);
	bch2_write_ref_put(c, BCH_WRITE_REF_delete_dead_snapshots);
}

void bch2_delete_dead_snapshots_async(struct bch_fs *c)
{
	if (bch2_write_ref_tryget(c, BCH_WRITE_REF_delete_dead_snapshots) &&
	    !queue_work(c->write_ref_wq, &c->snapshot_delete_work))
		bch2_write_ref_put(c, BCH_WRITE_REF_delete_dead_snapshots);
}

int bch2_delete_dead_snapshots_hook(struct btree_trans *trans,
				    struct btree_trans_commit_hook *h)
{
	struct bch_fs *c = trans->c;

	set_bit(BCH_FS_HAVE_DELETED_SNAPSHOTS, &c->flags);

	if (c->curr_recovery_pass <= BCH_RECOVERY_PASS_delete_dead_snapshots)
		return 0;

	bch2_delete_dead_snapshots_async(c);
	return 0;
}

int __bch2_key_has_snapshot_overwrites(struct btree_trans *trans,
				       enum btree_id id,
				       struct bpos pos)
{
	struct bch_fs *c = trans->c;
	struct btree_iter iter;
	struct bkey_s_c k;
	int ret;

	bch2_trans_iter_init(trans, &iter, id, pos,
			     BTREE_ITER_NOT_EXTENTS|
			     BTREE_ITER_ALL_SNAPSHOTS);
	while (1) {
		k = bch2_btree_iter_prev(&iter);
		ret = bkey_err(k);
		if (ret)
			break;

		if (!k.k)
			break;

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

static u32 bch2_snapshot_smallest_child(struct bch_fs *c, u32 id)
{
	const struct snapshot_t *s = snapshot_t(c, id);

	return s->children[1] ?: s->children[0];
}

static u32 bch2_snapshot_smallest_descendent(struct bch_fs *c, u32 id)
{
	u32 child;

	while ((child = bch2_snapshot_smallest_child(c, id)))
		id = child;
	return id;
}

static int bch2_propagate_key_to_snapshot_leaf(struct btree_trans *trans,
					       enum btree_id btree,
					       struct bkey_s_c interior_k,
					       u32 leaf_id, struct bpos *new_min_pos)
{
	struct btree_iter iter;
	struct bpos pos = interior_k.k->p;
	struct bkey_s_c k;
	struct bkey_i *new;
	int ret;

	pos.snapshot = leaf_id;

	bch2_trans_iter_init(trans, &iter, btree, pos, BTREE_ITER_INTENT);
	k = bch2_btree_iter_peek_slot(&iter);
	ret = bkey_err(k);
	if (ret)
		goto out;

	/* key already overwritten in this snapshot? */
	if (k.k->p.snapshot != interior_k.k->p.snapshot)
		goto out;

	if (bpos_eq(*new_min_pos, POS_MIN)) {
		*new_min_pos = k.k->p;
		new_min_pos->snapshot = leaf_id;
	}

	new = bch2_bkey_make_mut_noupdate(trans, interior_k);
	ret = PTR_ERR_OR_ZERO(new);
	if (ret)
		goto out;

	new->k.p.snapshot = leaf_id;
	ret = bch2_trans_update(trans, &iter, new, 0);
out:
	bch2_trans_iter_exit(trans, &iter);
	return ret;
}

int bch2_propagate_key_to_snapshot_leaves(struct btree_trans *trans,
					  enum btree_id btree,
					  struct bkey_s_c k,
					  struct bpos *new_min_pos)
{
	struct bch_fs *c = trans->c;
	struct bkey_buf sk;
	int ret;

	bch2_bkey_buf_init(&sk);
	bch2_bkey_buf_reassemble(&sk, c, k);
	k = bkey_i_to_s_c(sk.k);

	*new_min_pos = POS_MIN;

	for (u32 id = bch2_snapshot_smallest_descendent(c, k.k->p.snapshot);
	     id < k.k->p.snapshot;
	     id++) {
		if (!bch2_snapshot_is_ancestor(c, id, k.k->p.snapshot) ||
		    !bch2_snapshot_is_leaf(c, id))
			continue;

		ret = commit_do(trans, NULL, NULL, 0,
				bch2_propagate_key_to_snapshot_leaf(trans, btree, k, id, new_min_pos));
		if (ret)
			break;
	}

	bch2_bkey_buf_exit(&sk, c);
	return ret;
}

int bch2_snapshots_read(struct bch_fs *c)
{
	struct btree_iter iter;
	struct bkey_s_c k;
	int ret = 0;

	ret = bch2_trans_run(c,
		for_each_btree_key2(&trans, iter, BTREE_ID_snapshots,
			   POS_MIN, 0, k,
			bch2_mark_snapshot(&trans, BTREE_ID_snapshots, 0, bkey_s_c_null, k, 0) ?:
			bch2_snapshot_set_equiv(&trans, k)) ?:
		for_each_btree_key2(&trans, iter, BTREE_ID_snapshots,
			   POS_MIN, 0, k,
			   (set_is_ancestor_bitmap(c, k.k->p.offset), 0)));
	if (ret)
		bch_err_fn(c, ret);
	return ret;
}

void bch2_fs_snapshots_exit(struct bch_fs *c)
{
	kfree(rcu_dereference_protected(c->snapshots, true));
}
