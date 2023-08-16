/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_SNAPSHOT_H
#define _BCACHEFS_SNAPSHOT_H

enum bkey_invalid_flags;

void bch2_snapshot_tree_to_text(struct printbuf *, struct bch_fs *, struct bkey_s_c);
int bch2_snapshot_tree_invalid(const struct bch_fs *, struct bkey_s_c,
			       enum bkey_invalid_flags, struct printbuf *);

#define bch2_bkey_ops_snapshot_tree ((struct bkey_ops) {	\
	.key_invalid	= bch2_snapshot_tree_invalid,		\
	.val_to_text	= bch2_snapshot_tree_to_text,		\
	.min_val_size	= 8,					\
})

struct bkey_i_snapshot_tree *__bch2_snapshot_tree_create(struct btree_trans *);

int bch2_snapshot_tree_lookup(struct btree_trans *, u32, struct bch_snapshot_tree *);

void bch2_snapshot_to_text(struct printbuf *, struct bch_fs *, struct bkey_s_c);
int bch2_snapshot_invalid(const struct bch_fs *, struct bkey_s_c,
			  enum bkey_invalid_flags, struct printbuf *);
int bch2_mark_snapshot(struct btree_trans *, enum btree_id, unsigned,
		       struct bkey_s_c, struct bkey_s_c, unsigned);

#define bch2_bkey_ops_snapshot ((struct bkey_ops) {		\
	.key_invalid	= bch2_snapshot_invalid,		\
	.val_to_text	= bch2_snapshot_to_text,		\
	.atomic_trigger	= bch2_mark_snapshot,			\
	.min_val_size	= 24,					\
})

static inline struct snapshot_t *__snapshot_t(struct snapshot_table *t, u32 id)
{
	return &t->s[U32_MAX - id];
}

static inline const struct snapshot_t *snapshot_t(struct bch_fs *c, u32 id)
{
	return __snapshot_t(rcu_dereference(c->snapshots), id);
}

static inline u32 bch2_snapshot_tree(struct bch_fs *c, u32 id)
{
	rcu_read_lock();
	id = snapshot_t(c, id)->tree;
	rcu_read_unlock();

	return id;
}

static inline u32 __bch2_snapshot_parent_early(struct bch_fs *c, u32 id)
{
	return snapshot_t(c, id)->parent;
}

static inline u32 bch2_snapshot_parent_early(struct bch_fs *c, u32 id)
{
	rcu_read_lock();
	id = __bch2_snapshot_parent_early(c, id);
	rcu_read_unlock();

	return id;
}

static inline u32 __bch2_snapshot_parent(struct bch_fs *c, u32 id)
{
#ifdef CONFIG_BCACHEFS_DEBUG
	u32 parent = snapshot_t(c, id)->parent;

	if (parent &&
	    snapshot_t(c, id)->depth != snapshot_t(c, parent)->depth + 1)
		panic("id %u depth=%u parent %u depth=%u\n",
		      id, snapshot_t(c, id)->depth,
		      parent, snapshot_t(c, parent)->depth);

	return parent;
#else
	return snapshot_t(c, id)->parent;
#endif
}

static inline u32 bch2_snapshot_parent(struct bch_fs *c, u32 id)
{
	rcu_read_lock();
	id = __bch2_snapshot_parent(c, id);
	rcu_read_unlock();

	return id;
}

static inline u32 bch2_snapshot_nth_parent(struct bch_fs *c, u32 id, u32 n)
{
	rcu_read_lock();
	while (n--)
		id = __bch2_snapshot_parent(c, id);
	rcu_read_unlock();

	return id;
}

u32 bch2_snapshot_skiplist_get(struct bch_fs *, u32);

static inline u32 bch2_snapshot_root(struct bch_fs *c, u32 id)
{
	u32 parent;

	rcu_read_lock();
	while ((parent = __bch2_snapshot_parent(c, id)))
		id = parent;
	rcu_read_unlock();

	return id;
}

static inline u32 __bch2_snapshot_equiv(struct bch_fs *c, u32 id)
{
	return snapshot_t(c, id)->equiv;
}

static inline u32 bch2_snapshot_equiv(struct bch_fs *c, u32 id)
{
	rcu_read_lock();
	id = __bch2_snapshot_equiv(c, id);
	rcu_read_unlock();

	return id;
}

static inline bool bch2_snapshot_is_equiv(struct bch_fs *c, u32 id)
{
	return id == bch2_snapshot_equiv(c, id);
}

static inline bool bch2_snapshot_is_internal_node(struct bch_fs *c, u32 id)
{
	const struct snapshot_t *s;
	bool ret;

	rcu_read_lock();
	s = snapshot_t(c, id);
	ret = s->children[0];
	rcu_read_unlock();

	return ret;
}

static inline u32 bch2_snapshot_is_leaf(struct bch_fs *c, u32 id)
{
	return !bch2_snapshot_is_internal_node(c, id);
}

static inline u32 bch2_snapshot_sibling(struct bch_fs *c, u32 id)
{
	const struct snapshot_t *s;
	u32 parent = __bch2_snapshot_parent(c, id);

	if (!parent)
		return 0;

	s = snapshot_t(c, __bch2_snapshot_parent(c, id));
	if (id == s->children[0])
		return s->children[1];
	if (id == s->children[1])
		return s->children[0];
	return 0;
}

static inline u32 bch2_snapshot_depth(struct bch_fs *c, u32 parent)
{
	u32 depth;

	rcu_read_lock();
	depth = parent ? snapshot_t(c, parent)->depth + 1 : 0;
	rcu_read_unlock();

	return depth;
}

bool __bch2_snapshot_is_ancestor(struct bch_fs *, u32, u32);

static inline bool bch2_snapshot_is_ancestor(struct bch_fs *c, u32 id, u32 ancestor)
{
	return id == ancestor
		? true
		: __bch2_snapshot_is_ancestor(c, id, ancestor);
}

static inline bool bch2_snapshot_has_children(struct bch_fs *c, u32 id)
{
	const struct snapshot_t *t;
	bool ret;

	rcu_read_lock();
	t = snapshot_t(c, id);
	ret = (t->children[0]|t->children[1]) != 0;
	rcu_read_unlock();

	return ret;
}

static inline bool snapshot_list_has_id(snapshot_id_list *s, u32 id)
{
	u32 *i;

	darray_for_each(*s, i)
		if (*i == id)
			return true;
	return false;
}

static inline bool snapshot_list_has_ancestor(struct bch_fs *c, snapshot_id_list *s, u32 id)
{
	u32 *i;

	darray_for_each(*s, i)
		if (bch2_snapshot_is_ancestor(c, id, *i))
			return true;
	return false;
}

static inline int snapshot_list_add(struct bch_fs *c, snapshot_id_list *s, u32 id)
{
	int ret;

	BUG_ON(snapshot_list_has_id(s, id));
	ret = darray_push(s, id);
	if (ret)
		bch_err(c, "error reallocating snapshot_id_list (size %zu)", s->size);
	return ret;
}

int bch2_snapshot_lookup(struct btree_trans *trans, u32 id,
			 struct bch_snapshot *s);
int bch2_snapshot_get_subvol(struct btree_trans *, u32,
			     struct bch_subvolume *);
int bch2_snapshot_live(struct btree_trans *trans, u32 id);
int bch2_snapshot_set_equiv(struct btree_trans *trans, struct bkey_s_c k);

/* only exported for tests: */
int bch2_snapshot_node_create(struct btree_trans *, u32,
			      u32 *, u32 *, unsigned);

int bch2_check_snapshot_trees(struct bch_fs *);
int bch2_check_snapshots(struct bch_fs *);

int bch2_snapshot_node_set_deleted(struct btree_trans *, u32);
int bch2_delete_dead_snapshots_hook(struct btree_trans *,
				    struct btree_trans_commit_hook *);
void bch2_delete_dead_snapshots_work(struct work_struct *);

int bch2_snapshots_read(struct bch_fs *);
void bch2_fs_snapshots_exit(struct bch_fs *);

#endif /* _BCACHEFS_SNAPSHOT_H */
