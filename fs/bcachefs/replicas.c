// SPDX-License-Identifier: GPL-2.0

#include "bcachefs.h"
#include "replicas.h"
#include "super-io.h"

static int bch2_cpu_replicas_to_sb_replicas(struct bch_fs *,
					    struct bch_replicas_cpu *);

/* Replicas tracking - in memory: */

static inline int u8_cmp(u8 l, u8 r)
{
	return (l > r) - (l < r);
}

static void verify_replicas_entry_sorted(struct bch_replicas_entry *e)
{
#ifdef CONFIG_BCACHES_DEBUG
	unsigned i;

	for (i = 0; i + 1 < e->nr_devs; i++)
		BUG_ON(e->devs[i] >= e->devs[i + 1]);
#endif
}

static void replicas_entry_sort(struct bch_replicas_entry *e)
{
	bubble_sort(e->devs, e->nr_devs, u8_cmp);
}

#define for_each_cpu_replicas_entry(_r, _i)				\
	for (_i = (_r)->entries;					\
	     (void *) (_i) < (void *) (_r)->entries + (_r)->nr * (_r)->entry_size;\
	     _i = (void *) (_i) + (_r)->entry_size)

static void bch2_cpu_replicas_sort(struct bch_replicas_cpu *r)
{
	eytzinger0_sort(r->entries, r->nr, r->entry_size, memcmp, NULL);
}

void bch2_replicas_entry_to_text(struct printbuf *out,
				 struct bch_replicas_entry *e)
{
	unsigned i;

	pr_buf(out, "%s: %u/%u [",
	       bch2_data_types[e->data_type],
	       e->nr_required,
	       e->nr_devs);

	for (i = 0; i < e->nr_devs; i++)
		pr_buf(out, i ? " %u" : "%u", e->devs[i]);
	pr_buf(out, "]");
}

void bch2_cpu_replicas_to_text(struct printbuf *out,
			      struct bch_replicas_cpu *r)
{
	struct bch_replicas_entry *e;
	bool first = true;

	for_each_cpu_replicas_entry(r, e) {
		if (!first)
			pr_buf(out, " ");
		first = false;

		bch2_replicas_entry_to_text(out, e);
	}
}

static void extent_to_replicas(struct bkey_s_c k,
			       struct bch_replicas_entry *r)
{
	struct bkey_ptrs_c ptrs = bch2_bkey_ptrs_c(k);
	const union bch_extent_entry *entry;
	struct extent_ptr_decoded p;

	r->nr_required	= 1;

	bkey_for_each_ptr_decode(k.k, ptrs, p, entry) {
		if (p.ptr.cached)
			continue;

		if (p.ec_nr) {
			r->nr_devs = 0;
			break;
		}

		r->devs[r->nr_devs++] = p.ptr.dev;
	}
}

static void stripe_to_replicas(struct bkey_s_c k,
			       struct bch_replicas_entry *r)
{
	struct bkey_s_c_stripe s = bkey_s_c_to_stripe(k);
	const struct bch_extent_ptr *ptr;

	r->nr_required	= s.v->nr_blocks - s.v->nr_redundant;

	for (ptr = s.v->ptrs;
	     ptr < s.v->ptrs + s.v->nr_blocks;
	     ptr++)
		r->devs[r->nr_devs++] = ptr->dev;
}

static void bkey_to_replicas(struct bch_replicas_entry *e,
			     struct bkey_s_c k)
{
	e->nr_devs = 0;

	switch (k.k->type) {
	case KEY_TYPE_btree_ptr:
		e->data_type = BCH_DATA_BTREE;
		extent_to_replicas(k, e);
		break;
	case KEY_TYPE_extent:
		e->data_type = BCH_DATA_USER;
		extent_to_replicas(k, e);
		break;
	case KEY_TYPE_stripe:
		e->data_type = BCH_DATA_USER;
		stripe_to_replicas(k, e);
		break;
	}

	replicas_entry_sort(e);
}

void bch2_devlist_to_replicas(struct bch_replicas_entry *e,
			      enum bch_data_type data_type,
			      struct bch_devs_list devs)
{
	unsigned i;

	BUG_ON(!data_type ||
	       data_type == BCH_DATA_SB ||
	       data_type >= BCH_DATA_NR);

	e->data_type	= data_type;
	e->nr_devs	= 0;
	e->nr_required	= 1;

	for (i = 0; i < devs.nr; i++)
		e->devs[e->nr_devs++] = devs.devs[i];

	replicas_entry_sort(e);
}

static struct bch_replicas_cpu
cpu_replicas_add_entry(struct bch_replicas_cpu *old,
		       struct bch_replicas_entry *new_entry)
{
	unsigned i;
	struct bch_replicas_cpu new = {
		.nr		= old->nr + 1,
		.entry_size	= max_t(unsigned, old->entry_size,
					replicas_entry_bytes(new_entry)),
	};

	BUG_ON(!new_entry->data_type);
	verify_replicas_entry_sorted(new_entry);

	new.entries = kcalloc(new.nr, new.entry_size, GFP_NOIO);
	if (!new.entries)
		return new;

	for (i = 0; i < old->nr; i++)
		memcpy(cpu_replicas_entry(&new, i),
		       cpu_replicas_entry(old, i),
		       old->entry_size);

	memcpy(cpu_replicas_entry(&new, old->nr),
	       new_entry,
	       replicas_entry_bytes(new_entry));

	bch2_cpu_replicas_sort(&new);
	return new;
}

static inline int __replicas_entry_idx(struct bch_replicas_cpu *r,
				       struct bch_replicas_entry *search)
{
	int idx, entry_size = replicas_entry_bytes(search);

	if (unlikely(entry_size > r->entry_size))
		return -1;

	verify_replicas_entry_sorted(search);

#define entry_cmp(_l, _r, size)	memcmp(_l, _r, entry_size)
	idx = eytzinger0_find(r->entries, r->nr, r->entry_size,
			      entry_cmp, search);
#undef entry_cmp

	return idx < r->nr ? idx : -1;
}

int bch2_replicas_entry_idx(struct bch_fs *c,
			    struct bch_replicas_entry *search)
{
	replicas_entry_sort(search);

	return __replicas_entry_idx(&c->replicas, search);
}

static bool __replicas_has_entry(struct bch_replicas_cpu *r,
				 struct bch_replicas_entry *search)
{
	return __replicas_entry_idx(r, search) >= 0;
}

bool bch2_replicas_marked(struct bch_fs *c,
			  struct bch_replicas_entry *search,
			  bool check_gc_replicas)
{
	bool marked;

	if (!search->nr_devs)
		return true;

	verify_replicas_entry_sorted(search);

	percpu_down_read(&c->mark_lock);
	marked = __replicas_has_entry(&c->replicas, search) &&
		(!check_gc_replicas ||
		 likely((!c->replicas_gc.entries)) ||
		 __replicas_has_entry(&c->replicas_gc, search));
	percpu_up_read(&c->mark_lock);

	return marked;
}

static void __replicas_table_update(struct bch_fs_usage __percpu *dst_p,
				    struct bch_replicas_cpu *dst_r,
				    struct bch_fs_usage __percpu *src_p,
				    struct bch_replicas_cpu *src_r)
{
	unsigned src_nr = sizeof(struct bch_fs_usage) / sizeof(u64) + src_r->nr;
	struct bch_fs_usage *dst, *src = (void *)
		bch2_acc_percpu_u64s((void *) src_p, src_nr);
	int src_idx, dst_idx;

	preempt_disable();
	dst = this_cpu_ptr(dst_p);
	preempt_enable();

	*dst = *src;

	for (src_idx = 0; src_idx < src_r->nr; src_idx++) {
		if (!src->data[src_idx])
			continue;

		dst_idx = __replicas_entry_idx(dst_r,
				cpu_replicas_entry(src_r, src_idx));
		BUG_ON(dst_idx < 0);

		dst->data[dst_idx] = src->data[src_idx];
	}
}

/*
 * Resize filesystem accounting:
 */
static int replicas_table_update(struct bch_fs *c,
				 struct bch_replicas_cpu *new_r)
{
	struct bch_fs_usage __percpu *new_usage[3] = { NULL, NULL, NULL };
	unsigned bytes = sizeof(struct bch_fs_usage) +
		sizeof(u64) * new_r->nr;
	unsigned i;
	int ret = -ENOMEM;

	for (i = 0; i < 3; i++) {
		if (i < 2 && !c->usage[i])
			continue;

		new_usage[i] = __alloc_percpu_gfp(bytes, sizeof(u64),
						  GFP_NOIO);
		if (!new_usage[i])
			goto err;
	}

	for (i = 0; i < 2; i++) {
		if (!c->usage[i])
			continue;

		__replicas_table_update(new_usage[i],	new_r,
					c->usage[i],	&c->replicas);

		swap(c->usage[i], new_usage[i]);
	}

	swap(c->usage_scratch, new_usage[2]);

	swap(c->replicas, *new_r);
	ret = 0;
err:
	for (i = 0; i < 3; i++)
		free_percpu(new_usage[i]);
	return ret;
}

noinline
static int bch2_mark_replicas_slowpath(struct bch_fs *c,
				struct bch_replicas_entry *new_entry)
{
	struct bch_replicas_cpu new_r, new_gc;
	int ret = -ENOMEM;

	memset(&new_r, 0, sizeof(new_r));
	memset(&new_gc, 0, sizeof(new_gc));

	mutex_lock(&c->sb_lock);

	if (c->replicas_gc.entries &&
	    !__replicas_has_entry(&c->replicas_gc, new_entry)) {
		new_gc = cpu_replicas_add_entry(&c->replicas_gc, new_entry);
		if (!new_gc.entries)
			goto err;
	}

	if (!__replicas_has_entry(&c->replicas, new_entry)) {
		new_r = cpu_replicas_add_entry(&c->replicas, new_entry);
		if (!new_r.entries)
			goto err;

		ret = bch2_cpu_replicas_to_sb_replicas(c, &new_r);
		if (ret)
			goto err;
	}

	if (!new_r.entries &&
	    !new_gc.entries)
		goto out;

	/* allocations done, now commit: */

	if (new_r.entries)
		bch2_write_super(c);

	/* don't update in memory replicas until changes are persistent */
	percpu_down_write(&c->mark_lock);
	if (new_r.entries)
		ret = replicas_table_update(c, &new_r);
	if (new_gc.entries)
		swap(new_gc, c->replicas_gc);
	percpu_up_write(&c->mark_lock);
out:
	ret = 0;
err:
	mutex_unlock(&c->sb_lock);

	kfree(new_r.entries);
	kfree(new_gc.entries);

	return ret;
}

int bch2_mark_replicas(struct bch_fs *c,
		       struct bch_replicas_entry *r)
{
	return likely(bch2_replicas_marked(c, r, true))
		? 0
		: bch2_mark_replicas_slowpath(c, r);
}

bool bch2_bkey_replicas_marked(struct bch_fs *c,
			       struct bkey_s_c k,
			       bool check_gc_replicas)
{
	struct bch_replicas_padded search;
	struct bch_devs_list cached = bch2_bkey_cached_devs(k);
	unsigned i;

	for (i = 0; i < cached.nr; i++) {
		bch2_replicas_entry_cached(&search.e, cached.devs[i]);

		if (!bch2_replicas_marked(c, &search.e, check_gc_replicas))
			return false;
	}

	bkey_to_replicas(&search.e, k);

	return bch2_replicas_marked(c, &search.e, check_gc_replicas);
}

int bch2_mark_bkey_replicas(struct bch_fs *c, struct bkey_s_c k)
{
	struct bch_replicas_padded search;
	struct bch_devs_list cached = bch2_bkey_cached_devs(k);
	unsigned i;
	int ret;

	for (i = 0; i < cached.nr; i++) {
		bch2_replicas_entry_cached(&search.e, cached.devs[i]);

		ret = bch2_mark_replicas(c, &search.e);
		if (ret)
			return ret;
	}

	bkey_to_replicas(&search.e, k);

	return bch2_mark_replicas(c, &search.e);
}

int bch2_replicas_gc_end(struct bch_fs *c, int ret)
{
	unsigned i;

	lockdep_assert_held(&c->replicas_gc_lock);

	mutex_lock(&c->sb_lock);

	if (ret)
		goto err;

	/*
	 * this is kind of crappy; the replicas gc mechanism needs to be ripped
	 * out
	 */

	for (i = 0; i < c->replicas.nr; i++) {
		struct bch_replicas_entry *e =
			cpu_replicas_entry(&c->replicas, i);
		struct bch_replicas_cpu n;
		u64 v = 0;
		int cpu;

		if (__replicas_has_entry(&c->replicas_gc, e))
			continue;

		for_each_possible_cpu(cpu)
			v += *per_cpu_ptr(&c->usage[0]->data[i], cpu);
		if (!v)
			continue;

		n = cpu_replicas_add_entry(&c->replicas_gc, e);
		if (!n.entries) {
			ret = -ENOSPC;
			goto err;
		}

		percpu_down_write(&c->mark_lock);
		swap(n, c->replicas_gc);
		percpu_up_write(&c->mark_lock);

		kfree(n.entries);
	}

	if (bch2_cpu_replicas_to_sb_replicas(c, &c->replicas_gc)) {
		ret = -ENOSPC;
		goto err;
	}

	bch2_write_super(c);

	/* don't update in memory replicas until changes are persistent */
err:
	percpu_down_write(&c->mark_lock);
	if (!ret)
		ret = replicas_table_update(c, &c->replicas_gc);

	kfree(c->replicas_gc.entries);
	c->replicas_gc.entries = NULL;
	percpu_up_write(&c->mark_lock);

	mutex_unlock(&c->sb_lock);
	return ret;
}

int bch2_replicas_gc_start(struct bch_fs *c, unsigned typemask)
{
	struct bch_replicas_entry *e;
	unsigned i = 0;

	lockdep_assert_held(&c->replicas_gc_lock);

	mutex_lock(&c->sb_lock);
	BUG_ON(c->replicas_gc.entries);

	c->replicas_gc.nr		= 0;
	c->replicas_gc.entry_size	= 0;

	for_each_cpu_replicas_entry(&c->replicas, e)
		if (!((1 << e->data_type) & typemask)) {
			c->replicas_gc.nr++;
			c->replicas_gc.entry_size =
				max_t(unsigned, c->replicas_gc.entry_size,
				      replicas_entry_bytes(e));
		}

	c->replicas_gc.entries = kcalloc(c->replicas_gc.nr,
					 c->replicas_gc.entry_size,
					 GFP_NOIO);
	if (!c->replicas_gc.entries) {
		mutex_unlock(&c->sb_lock);
		return -ENOMEM;
	}

	for_each_cpu_replicas_entry(&c->replicas, e)
		if (!((1 << e->data_type) & typemask))
			memcpy(cpu_replicas_entry(&c->replicas_gc, i++),
			       e, c->replicas_gc.entry_size);

	bch2_cpu_replicas_sort(&c->replicas_gc);
	mutex_unlock(&c->sb_lock);

	return 0;
}

/* Replicas tracking - superblock: */

static int
__bch2_sb_replicas_to_cpu_replicas(struct bch_sb_field_replicas *sb_r,
				   struct bch_replicas_cpu *cpu_r)
{
	struct bch_replicas_entry *e, *dst;
	unsigned nr = 0, entry_size = 0, idx = 0;

	for_each_replicas_entry(sb_r, e) {
		entry_size = max_t(unsigned, entry_size,
				   replicas_entry_bytes(e));
		nr++;
	}

	cpu_r->entries = kcalloc(nr, entry_size, GFP_NOIO);
	if (!cpu_r->entries)
		return -ENOMEM;

	cpu_r->nr		= nr;
	cpu_r->entry_size	= entry_size;

	for_each_replicas_entry(sb_r, e) {
		dst = cpu_replicas_entry(cpu_r, idx++);
		memcpy(dst, e, replicas_entry_bytes(e));
		replicas_entry_sort(dst);
	}

	return 0;
}

static int
__bch2_sb_replicas_v0_to_cpu_replicas(struct bch_sb_field_replicas_v0 *sb_r,
				      struct bch_replicas_cpu *cpu_r)
{
	struct bch_replicas_entry_v0 *e;
	unsigned nr = 0, entry_size = 0, idx = 0;

	for_each_replicas_entry(sb_r, e) {
		entry_size = max_t(unsigned, entry_size,
				   replicas_entry_bytes(e));
		nr++;
	}

	entry_size += sizeof(struct bch_replicas_entry) -
		sizeof(struct bch_replicas_entry_v0);

	cpu_r->entries = kcalloc(nr, entry_size, GFP_NOIO);
	if (!cpu_r->entries)
		return -ENOMEM;

	cpu_r->nr		= nr;
	cpu_r->entry_size	= entry_size;

	for_each_replicas_entry(sb_r, e) {
		struct bch_replicas_entry *dst =
			cpu_replicas_entry(cpu_r, idx++);

		dst->data_type	= e->data_type;
		dst->nr_devs	= e->nr_devs;
		dst->nr_required = 1;
		memcpy(dst->devs, e->devs, e->nr_devs);
		replicas_entry_sort(dst);
	}

	return 0;
}

int bch2_sb_replicas_to_cpu_replicas(struct bch_fs *c)
{
	struct bch_sb_field_replicas *sb_v1;
	struct bch_sb_field_replicas_v0 *sb_v0;
	struct bch_replicas_cpu new_r = { 0, 0, NULL };
	int ret = 0;

	if ((sb_v1 = bch2_sb_get_replicas(c->disk_sb.sb)))
		ret = __bch2_sb_replicas_to_cpu_replicas(sb_v1, &new_r);
	else if ((sb_v0 = bch2_sb_get_replicas_v0(c->disk_sb.sb)))
		ret = __bch2_sb_replicas_v0_to_cpu_replicas(sb_v0, &new_r);

	if (ret)
		return -ENOMEM;

	bch2_cpu_replicas_sort(&new_r);

	percpu_down_write(&c->mark_lock);
	ret = replicas_table_update(c, &new_r);
	percpu_up_write(&c->mark_lock);

	kfree(new_r.entries);

	return 0;
}

static int bch2_cpu_replicas_to_sb_replicas_v0(struct bch_fs *c,
					       struct bch_replicas_cpu *r)
{
	struct bch_sb_field_replicas_v0 *sb_r;
	struct bch_replicas_entry_v0 *dst;
	struct bch_replicas_entry *src;
	size_t bytes;

	bytes = sizeof(struct bch_sb_field_replicas);

	for_each_cpu_replicas_entry(r, src)
		bytes += replicas_entry_bytes(src) - 1;

	sb_r = bch2_sb_resize_replicas_v0(&c->disk_sb,
			DIV_ROUND_UP(bytes, sizeof(u64)));
	if (!sb_r)
		return -ENOSPC;

	bch2_sb_field_delete(&c->disk_sb, BCH_SB_FIELD_replicas);
	sb_r = bch2_sb_get_replicas_v0(c->disk_sb.sb);

	memset(&sb_r->entries, 0,
	       vstruct_end(&sb_r->field) -
	       (void *) &sb_r->entries);

	dst = sb_r->entries;
	for_each_cpu_replicas_entry(r, src) {
		dst->data_type	= src->data_type;
		dst->nr_devs	= src->nr_devs;
		memcpy(dst->devs, src->devs, src->nr_devs);

		dst = replicas_entry_next(dst);

		BUG_ON((void *) dst > vstruct_end(&sb_r->field));
	}

	return 0;
}

static int bch2_cpu_replicas_to_sb_replicas(struct bch_fs *c,
					    struct bch_replicas_cpu *r)
{
	struct bch_sb_field_replicas *sb_r;
	struct bch_replicas_entry *dst, *src;
	bool need_v1 = false;
	size_t bytes;

	bytes = sizeof(struct bch_sb_field_replicas);

	for_each_cpu_replicas_entry(r, src) {
		bytes += replicas_entry_bytes(src);
		if (src->nr_required != 1)
			need_v1 = true;
	}

	if (!need_v1)
		return bch2_cpu_replicas_to_sb_replicas_v0(c, r);

	sb_r = bch2_sb_resize_replicas(&c->disk_sb,
			DIV_ROUND_UP(bytes, sizeof(u64)));
	if (!sb_r)
		return -ENOSPC;

	bch2_sb_field_delete(&c->disk_sb, BCH_SB_FIELD_replicas_v0);
	sb_r = bch2_sb_get_replicas(c->disk_sb.sb);

	memset(&sb_r->entries, 0,
	       vstruct_end(&sb_r->field) -
	       (void *) &sb_r->entries);

	dst = sb_r->entries;
	for_each_cpu_replicas_entry(r, src) {
		memcpy(dst, src, replicas_entry_bytes(src));

		dst = replicas_entry_next(dst);

		BUG_ON((void *) dst > vstruct_end(&sb_r->field));
	}

	return 0;
}

static const char *check_dup_replicas_entries(struct bch_replicas_cpu *cpu_r)
{
	unsigned i;

	sort_cmp_size(cpu_r->entries,
		      cpu_r->nr,
		      cpu_r->entry_size,
		      memcmp, NULL);

	for (i = 0; i + 1 < cpu_r->nr; i++) {
		struct bch_replicas_entry *l =
			cpu_replicas_entry(cpu_r, i);
		struct bch_replicas_entry *r =
			cpu_replicas_entry(cpu_r, i + 1);

		BUG_ON(memcmp(l, r, cpu_r->entry_size) > 0);

		if (!memcmp(l, r, cpu_r->entry_size))
			return "duplicate replicas entry";
	}

	return NULL;
}

static const char *bch2_sb_validate_replicas(struct bch_sb *sb, struct bch_sb_field *f)
{
	struct bch_sb_field_replicas *sb_r = field_to_type(f, replicas);
	struct bch_sb_field_members *mi = bch2_sb_get_members(sb);
	struct bch_replicas_cpu cpu_r = { .entries = NULL };
	struct bch_replicas_entry *e;
	const char *err;
	unsigned i;

	for_each_replicas_entry(sb_r, e) {
		err = "invalid replicas entry: invalid data type";
		if (e->data_type >= BCH_DATA_NR)
			goto err;

		err = "invalid replicas entry: no devices";
		if (!e->nr_devs)
			goto err;

		err = "invalid replicas entry: bad nr_required";
		if (!e->nr_required ||
		    (e->nr_required > 1 &&
		     e->nr_required >= e->nr_devs))
			goto err;

		err = "invalid replicas entry: invalid device";
		for (i = 0; i < e->nr_devs; i++)
			if (!bch2_dev_exists(sb, mi, e->devs[i]))
				goto err;
	}

	err = "cannot allocate memory";
	if (__bch2_sb_replicas_to_cpu_replicas(sb_r, &cpu_r))
		goto err;

	err = check_dup_replicas_entries(&cpu_r);
err:
	kfree(cpu_r.entries);
	return err;
}

static void bch2_sb_replicas_to_text(struct printbuf *out,
				     struct bch_sb *sb,
				     struct bch_sb_field *f)
{
	struct bch_sb_field_replicas *r = field_to_type(f, replicas);
	struct bch_replicas_entry *e;
	bool first = true;

	for_each_replicas_entry(r, e) {
		if (!first)
			pr_buf(out, " ");
		first = false;

		bch2_replicas_entry_to_text(out, e);
	}
}

const struct bch_sb_field_ops bch_sb_field_ops_replicas = {
	.validate	= bch2_sb_validate_replicas,
	.to_text	= bch2_sb_replicas_to_text,
};

static const char *bch2_sb_validate_replicas_v0(struct bch_sb *sb, struct bch_sb_field *f)
{
	struct bch_sb_field_replicas_v0 *sb_r = field_to_type(f, replicas_v0);
	struct bch_sb_field_members *mi = bch2_sb_get_members(sb);
	struct bch_replicas_cpu cpu_r = { .entries = NULL };
	struct bch_replicas_entry_v0 *e;
	const char *err;
	unsigned i;

	for_each_replicas_entry_v0(sb_r, e) {
		err = "invalid replicas entry: invalid data type";
		if (e->data_type >= BCH_DATA_NR)
			goto err;

		err = "invalid replicas entry: no devices";
		if (!e->nr_devs)
			goto err;

		err = "invalid replicas entry: invalid device";
		for (i = 0; i < e->nr_devs; i++)
			if (!bch2_dev_exists(sb, mi, e->devs[i]))
				goto err;
	}

	err = "cannot allocate memory";
	if (__bch2_sb_replicas_v0_to_cpu_replicas(sb_r, &cpu_r))
		goto err;

	err = check_dup_replicas_entries(&cpu_r);
err:
	kfree(cpu_r.entries);
	return err;
}

const struct bch_sb_field_ops bch_sb_field_ops_replicas_v0 = {
	.validate	= bch2_sb_validate_replicas_v0,
};

/* Query replicas: */

struct replicas_status __bch2_replicas_status(struct bch_fs *c,
					      struct bch_devs_mask online_devs)
{
	struct bch_sb_field_members *mi;
	struct bch_replicas_entry *e;
	unsigned i, nr_online, nr_offline;
	struct replicas_status ret;

	memset(&ret, 0, sizeof(ret));

	for (i = 0; i < ARRAY_SIZE(ret.replicas); i++)
		ret.replicas[i].redundancy = INT_MAX;

	mi = bch2_sb_get_members(c->disk_sb.sb);

	percpu_down_read(&c->mark_lock);

	for_each_cpu_replicas_entry(&c->replicas, e) {
		if (e->data_type >= ARRAY_SIZE(ret.replicas))
			panic("e %p data_type %u\n", e, e->data_type);

		nr_online = nr_offline = 0;

		for (i = 0; i < e->nr_devs; i++) {
			BUG_ON(!bch2_dev_exists(c->disk_sb.sb, mi,
						e->devs[i]));

			if (test_bit(e->devs[i], online_devs.d))
				nr_online++;
			else
				nr_offline++;
		}

		ret.replicas[e->data_type].redundancy =
			min(ret.replicas[e->data_type].redundancy,
			    (int) nr_online - (int) e->nr_required);

		ret.replicas[e->data_type].nr_offline =
			max(ret.replicas[e->data_type].nr_offline,
			    nr_offline);
	}

	percpu_up_read(&c->mark_lock);

	for (i = 0; i < ARRAY_SIZE(ret.replicas); i++)
		if (ret.replicas[i].redundancy == INT_MAX)
			ret.replicas[i].redundancy = 0;

	return ret;
}

struct replicas_status bch2_replicas_status(struct bch_fs *c)
{
	return __bch2_replicas_status(c, bch2_online_devs(c));
}

static bool have_enough_devs(struct replicas_status s,
			     enum bch_data_type type,
			     bool force_if_degraded,
			     bool force_if_lost)
{
	return (!s.replicas[type].nr_offline || force_if_degraded) &&
		(s.replicas[type].redundancy >= 0 || force_if_lost);
}

bool bch2_have_enough_devs(struct replicas_status s, unsigned flags)
{
	return (have_enough_devs(s, BCH_DATA_JOURNAL,
				 flags & BCH_FORCE_IF_METADATA_DEGRADED,
				 flags & BCH_FORCE_IF_METADATA_LOST) &&
		have_enough_devs(s, BCH_DATA_BTREE,
				 flags & BCH_FORCE_IF_METADATA_DEGRADED,
				 flags & BCH_FORCE_IF_METADATA_LOST) &&
		have_enough_devs(s, BCH_DATA_USER,
				 flags & BCH_FORCE_IF_DATA_DEGRADED,
				 flags & BCH_FORCE_IF_DATA_LOST));
}

int bch2_replicas_online(struct bch_fs *c, bool meta)
{
	struct replicas_status s = bch2_replicas_status(c);

	return (meta
		? min(s.replicas[BCH_DATA_JOURNAL].redundancy,
		      s.replicas[BCH_DATA_BTREE].redundancy)
		: s.replicas[BCH_DATA_USER].redundancy) + 1;
}

unsigned bch2_dev_has_data(struct bch_fs *c, struct bch_dev *ca)
{
	struct bch_replicas_entry *e;
	unsigned i, ret = 0;

	percpu_down_read(&c->mark_lock);

	for_each_cpu_replicas_entry(&c->replicas, e)
		for (i = 0; i < e->nr_devs; i++)
			if (e->devs[i] == ca->dev_idx)
				ret |= 1 << e->data_type;

	percpu_up_read(&c->mark_lock);

	return ret;
}
