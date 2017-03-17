// SPDX-License-Identifier: GPL-2.0

#include "bcachefs.h"
#include "replicas.h"
#include "super-io.h"

static int bch2_cpu_replicas_to_sb_replicas(struct bch_fs *,
					    struct bch_replicas_cpu *);

/* Replicas tracking - in memory: */

#define for_each_cpu_replicas_entry(_r, _i)				\
	for (_i = (_r)->entries;					\
	     (void *) (_i) < (void *) (_r)->entries + (_r)->nr * (_r)->entry_size;\
	     _i = (void *) (_i) + (_r)->entry_size)

static inline struct bch_replicas_cpu_entry *
cpu_replicas_entry(struct bch_replicas_cpu *r, unsigned i)
{
	return (void *) r->entries + r->entry_size * i;
}

static void bch2_cpu_replicas_sort(struct bch_replicas_cpu *r)
{
	eytzinger0_sort(r->entries, r->nr, r->entry_size, memcmp, NULL);
}

static inline bool replicas_test_dev(struct bch_replicas_cpu_entry *e,
				     unsigned dev)
{
	return (e->devs[dev >> 3] & (1 << (dev & 7))) != 0;
}

static inline void replicas_set_dev(struct bch_replicas_cpu_entry *e,
				    unsigned dev)
{
	e->devs[dev >> 3] |= 1 << (dev & 7);
}

static inline unsigned replicas_dev_slots(struct bch_replicas_cpu *r)
{
	return (r->entry_size -
		offsetof(struct bch_replicas_cpu_entry, devs)) * 8;
}

int bch2_cpu_replicas_to_text(struct bch_replicas_cpu *r,
			      char *buf, size_t size)
{
	char *out = buf, *end = out + size;
	struct bch_replicas_cpu_entry *e;
	bool first = true;
	unsigned i;

	for_each_cpu_replicas_entry(r, e) {
		bool first_e = true;

		if (!first)
			out += scnprintf(out, end - out, " ");
		first = false;

		out += scnprintf(out, end - out, "%u: [", e->data_type);

		for (i = 0; i < replicas_dev_slots(r); i++)
			if (replicas_test_dev(e, i)) {
				if (!first_e)
					out += scnprintf(out, end - out, " ");
				first_e = false;
				out += scnprintf(out, end - out, "%u", i);
			}
		out += scnprintf(out, end - out, "]");
	}

	return out - buf;
}

static inline unsigned bkey_to_replicas(struct bkey_s_c_extent e,
					enum bch_data_type data_type,
					struct bch_replicas_cpu_entry *r,
					unsigned *max_dev)
{
	const struct bch_extent_ptr *ptr;
	unsigned nr = 0;

	BUG_ON(!data_type ||
	       data_type == BCH_DATA_SB ||
	       data_type >= BCH_DATA_NR);

	memset(r, 0, sizeof(*r));
	r->data_type = data_type;

	*max_dev = 0;

	extent_for_each_ptr(e, ptr)
		if (!ptr->cached) {
			*max_dev = max_t(unsigned, *max_dev, ptr->dev);
			replicas_set_dev(r, ptr->dev);
			nr++;
		}
	return nr;
}

static inline void devlist_to_replicas(struct bch_devs_list devs,
				       enum bch_data_type data_type,
				       struct bch_replicas_cpu_entry *r,
				       unsigned *max_dev)
{
	unsigned i;

	BUG_ON(!data_type ||
	       data_type == BCH_DATA_SB ||
	       data_type >= BCH_DATA_NR);

	memset(r, 0, sizeof(*r));
	r->data_type = data_type;

	*max_dev = 0;

	for (i = 0; i < devs.nr; i++) {
		*max_dev = max_t(unsigned, *max_dev, devs.devs[i]);
		replicas_set_dev(r, devs.devs[i]);
	}
}

static struct bch_replicas_cpu *
cpu_replicas_add_entry(struct bch_replicas_cpu *old,
		       struct bch_replicas_cpu_entry new_entry,
		       unsigned max_dev)
{
	struct bch_replicas_cpu *new;
	unsigned i, nr, entry_size;

	entry_size = offsetof(struct bch_replicas_cpu_entry, devs) +
		DIV_ROUND_UP(max_dev + 1, 8);
	entry_size = max(entry_size, old->entry_size);
	nr = old->nr + 1;

	new = kzalloc(sizeof(struct bch_replicas_cpu) +
		      nr * entry_size, GFP_NOIO);
	if (!new)
		return NULL;

	new->nr		= nr;
	new->entry_size	= entry_size;

	for (i = 0; i < old->nr; i++)
		memcpy(cpu_replicas_entry(new, i),
		       cpu_replicas_entry(old, i),
		       min(new->entry_size, old->entry_size));

	memcpy(cpu_replicas_entry(new, old->nr),
	       &new_entry,
	       new->entry_size);

	bch2_cpu_replicas_sort(new);
	return new;
}

static bool replicas_has_entry(struct bch_replicas_cpu *r,
				struct bch_replicas_cpu_entry search,
				unsigned max_dev)
{
	return max_dev < replicas_dev_slots(r) &&
		eytzinger0_find(r->entries, r->nr,
				r->entry_size,
				memcmp, &search) < r->nr;
}

noinline
static int bch2_mark_replicas_slowpath(struct bch_fs *c,
				struct bch_replicas_cpu_entry new_entry,
				unsigned max_dev)
{
	struct bch_replicas_cpu *old_gc, *new_gc = NULL, *old_r, *new_r = NULL;
	int ret = -ENOMEM;

	mutex_lock(&c->sb_lock);

	old_gc = rcu_dereference_protected(c->replicas_gc,
					   lockdep_is_held(&c->sb_lock));
	if (old_gc && !replicas_has_entry(old_gc, new_entry, max_dev)) {
		new_gc = cpu_replicas_add_entry(old_gc, new_entry, max_dev);
		if (!new_gc)
			goto err;
	}

	old_r = rcu_dereference_protected(c->replicas,
					  lockdep_is_held(&c->sb_lock));
	if (!replicas_has_entry(old_r, new_entry, max_dev)) {
		new_r = cpu_replicas_add_entry(old_r, new_entry, max_dev);
		if (!new_r)
			goto err;

		ret = bch2_cpu_replicas_to_sb_replicas(c, new_r);
		if (ret)
			goto err;
	}

	/* allocations done, now commit: */

	if (new_r)
		bch2_write_super(c);

	/* don't update in memory replicas until changes are persistent */

	if (new_gc) {
		rcu_assign_pointer(c->replicas_gc, new_gc);
		kfree_rcu(old_gc, rcu);
	}

	if (new_r) {
		rcu_assign_pointer(c->replicas, new_r);
		kfree_rcu(old_r, rcu);
	}

	mutex_unlock(&c->sb_lock);
	return 0;
err:
	mutex_unlock(&c->sb_lock);
	kfree(new_gc);
	kfree(new_r);
	return ret;
}

int bch2_mark_replicas(struct bch_fs *c,
		       enum bch_data_type data_type,
		       struct bch_devs_list devs)
{
	struct bch_replicas_cpu_entry search;
	struct bch_replicas_cpu *r, *gc_r;
	unsigned max_dev;
	bool marked;

	if (!devs.nr)
		return 0;

	BUG_ON(devs.nr >= BCH_REPLICAS_MAX);

	devlist_to_replicas(devs, data_type, &search, &max_dev);

	rcu_read_lock();
	r = rcu_dereference(c->replicas);
	gc_r = rcu_dereference(c->replicas_gc);
	marked = replicas_has_entry(r, search, max_dev) &&
		(!likely(gc_r) || replicas_has_entry(gc_r, search, max_dev));
	rcu_read_unlock();

	return likely(marked) ? 0
		: bch2_mark_replicas_slowpath(c, search, max_dev);
}

int bch2_mark_bkey_replicas(struct bch_fs *c,
			    enum bch_data_type data_type,
			    struct bkey_s_c k)
{
	struct bch_devs_list cached = bch2_bkey_cached_devs(k);
	unsigned i;
	int ret;

	for (i = 0; i < cached.nr; i++)
		if ((ret = bch2_mark_replicas(c, BCH_DATA_CACHED,
					      bch2_dev_list_single(cached.devs[i]))))
			return ret;

	return bch2_mark_replicas(c, data_type, bch2_bkey_dirty_devs(k));
}

int bch2_replicas_gc_end(struct bch_fs *c, int ret)
{
	struct bch_replicas_cpu *new_r, *old_r;

	lockdep_assert_held(&c->replicas_gc_lock);

	mutex_lock(&c->sb_lock);

	new_r = rcu_dereference_protected(c->replicas_gc,
					  lockdep_is_held(&c->sb_lock));
	rcu_assign_pointer(c->replicas_gc, NULL);

	if (ret)
		goto err;

	if (bch2_cpu_replicas_to_sb_replicas(c, new_r)) {
		ret = -ENOSPC;
		goto err;
	}

	bch2_write_super(c);

	/* don't update in memory replicas until changes are persistent */

	old_r = rcu_dereference_protected(c->replicas,
					  lockdep_is_held(&c->sb_lock));

	rcu_assign_pointer(c->replicas, new_r);
	kfree_rcu(old_r, rcu);
out:
	mutex_unlock(&c->sb_lock);
	return ret;
err:
	kfree_rcu(new_r, rcu);
	goto out;
}

int bch2_replicas_gc_start(struct bch_fs *c, unsigned typemask)
{
	struct bch_replicas_cpu *dst, *src;
	struct bch_replicas_cpu_entry *e;

	lockdep_assert_held(&c->replicas_gc_lock);

	mutex_lock(&c->sb_lock);
	BUG_ON(c->replicas_gc);

	src = rcu_dereference_protected(c->replicas,
					lockdep_is_held(&c->sb_lock));

	dst = kzalloc(sizeof(struct bch_replicas_cpu) +
		      src->nr * src->entry_size, GFP_NOIO);
	if (!dst) {
		mutex_unlock(&c->sb_lock);
		return -ENOMEM;
	}

	dst->nr		= 0;
	dst->entry_size	= src->entry_size;

	for_each_cpu_replicas_entry(src, e)
		if (!((1 << e->data_type) & typemask))
			memcpy(cpu_replicas_entry(dst, dst->nr++),
			       e, dst->entry_size);

	bch2_cpu_replicas_sort(dst);

	rcu_assign_pointer(c->replicas_gc, dst);
	mutex_unlock(&c->sb_lock);

	return 0;
}

/* Replicas tracking - superblock: */

static void bch2_sb_replicas_nr_entries(struct bch_sb_field_replicas *r,
					unsigned *nr,
					unsigned *bytes,
					unsigned *max_dev)
{
	struct bch_replicas_entry *i;
	unsigned j;

	*nr	= 0;
	*bytes	= sizeof(*r);
	*max_dev = 0;

	if (!r)
		return;

	for_each_replicas_entry(r, i) {
		for (j = 0; j < i->nr; j++)
			*max_dev = max_t(unsigned, *max_dev, i->devs[j]);
		(*nr)++;
	}

	*bytes = (void *) i - (void *) r;
}

static struct bch_replicas_cpu *
__bch2_sb_replicas_to_cpu_replicas(struct bch_sb_field_replicas *sb_r)
{
	struct bch_replicas_cpu *cpu_r;
	unsigned i, nr, bytes, max_dev, entry_size;

	bch2_sb_replicas_nr_entries(sb_r, &nr, &bytes, &max_dev);

	entry_size = offsetof(struct bch_replicas_cpu_entry, devs) +
		DIV_ROUND_UP(max_dev + 1, 8);

	cpu_r = kzalloc(sizeof(struct bch_replicas_cpu) +
			nr * entry_size, GFP_NOIO);
	if (!cpu_r)
		return NULL;

	cpu_r->nr		= nr;
	cpu_r->entry_size	= entry_size;

	if (nr) {
		struct bch_replicas_cpu_entry *dst =
			cpu_replicas_entry(cpu_r, 0);
		struct bch_replicas_entry *src = sb_r->entries;

		while (dst < cpu_replicas_entry(cpu_r, nr)) {
			dst->data_type = src->data_type;
			for (i = 0; i < src->nr; i++)
				replicas_set_dev(dst, src->devs[i]);

			src	= replicas_entry_next(src);
			dst	= (void *) dst + entry_size;
		}
	}

	bch2_cpu_replicas_sort(cpu_r);
	return cpu_r;
}

int bch2_sb_replicas_to_cpu_replicas(struct bch_fs *c)
{
	struct bch_sb_field_replicas *sb_r;
	struct bch_replicas_cpu *cpu_r, *old_r;

	sb_r	= bch2_sb_get_replicas(c->disk_sb.sb);
	cpu_r	= __bch2_sb_replicas_to_cpu_replicas(sb_r);
	if (!cpu_r)
		return -ENOMEM;

	old_r = rcu_dereference_check(c->replicas, lockdep_is_held(&c->sb_lock));
	rcu_assign_pointer(c->replicas, cpu_r);
	if (old_r)
		kfree_rcu(old_r, rcu);

	return 0;
}

static int bch2_cpu_replicas_to_sb_replicas(struct bch_fs *c,
					    struct bch_replicas_cpu *r)
{
	struct bch_sb_field_replicas *sb_r;
	struct bch_replicas_entry *sb_e;
	struct bch_replicas_cpu_entry *e;
	size_t i, bytes;

	bytes = sizeof(struct bch_sb_field_replicas);

	for_each_cpu_replicas_entry(r, e) {
		bytes += sizeof(struct bch_replicas_entry);
		for (i = 0; i < r->entry_size - 1; i++)
			bytes += hweight8(e->devs[i]);
	}

	sb_r = bch2_sb_resize_replicas(&c->disk_sb,
			DIV_ROUND_UP(sizeof(*sb_r) + bytes, sizeof(u64)));
	if (!sb_r)
		return -ENOSPC;

	memset(&sb_r->entries, 0,
	       vstruct_end(&sb_r->field) -
	       (void *) &sb_r->entries);

	sb_e = sb_r->entries;
	for_each_cpu_replicas_entry(r, e) {
		sb_e->data_type = e->data_type;

		for (i = 0; i < replicas_dev_slots(r); i++)
			if (replicas_test_dev(e, i))
				sb_e->devs[sb_e->nr++] = i;

		sb_e = replicas_entry_next(sb_e);

		BUG_ON((void *) sb_e > vstruct_end(&sb_r->field));
	}

	return 0;
}

static const char *bch2_sb_validate_replicas(struct bch_sb *sb, struct bch_sb_field *f)
{
	struct bch_sb_field_replicas *sb_r = field_to_type(f, replicas);
	struct bch_sb_field_members *mi = bch2_sb_get_members(sb);
	struct bch_replicas_cpu *cpu_r = NULL;
	struct bch_replicas_entry *e;
	const char *err;
	unsigned i;

	for_each_replicas_entry(sb_r, e) {
		err = "invalid replicas entry: invalid data type";
		if (e->data_type >= BCH_DATA_NR)
			goto err;

		err = "invalid replicas entry: no devices";
		if (!e->nr)
			goto err;

		err = "invalid replicas entry: too many devices";
		if (e->nr >= BCH_REPLICAS_MAX)
			goto err;

		err = "invalid replicas entry: invalid device";
		for (i = 0; i < e->nr; i++)
			if (!bch2_dev_exists(sb, mi, e->devs[i]))
				goto err;
	}

	err = "cannot allocate memory";
	cpu_r = __bch2_sb_replicas_to_cpu_replicas(sb_r);
	if (!cpu_r)
		goto err;

	sort_cmp_size(cpu_r->entries,
		      cpu_r->nr,
		      cpu_r->entry_size,
		      memcmp, NULL);

	for (i = 0; i + 1 < cpu_r->nr; i++) {
		struct bch_replicas_cpu_entry *l =
			cpu_replicas_entry(cpu_r, i);
		struct bch_replicas_cpu_entry *r =
			cpu_replicas_entry(cpu_r, i + 1);

		BUG_ON(memcmp(l, r, cpu_r->entry_size) > 0);

		err = "duplicate replicas entry";
		if (!memcmp(l, r, cpu_r->entry_size))
			goto err;
	}

	err = NULL;
err:
	kfree(cpu_r);
	return err;
}

const struct bch_sb_field_ops bch_sb_field_ops_replicas = {
	.validate	= bch2_sb_validate_replicas,
};

int bch2_sb_replicas_to_text(struct bch_sb_field_replicas *r, char *buf, size_t size)
{
	char *out = buf, *end = out + size;
	struct bch_replicas_entry *e;
	bool first = true;
	unsigned i;

	if (!r) {
		out += scnprintf(out, end - out, "(no replicas section found)");
		return out - buf;
	}

	for_each_replicas_entry(r, e) {
		if (!first)
			out += scnprintf(out, end - out, " ");
		first = false;

		out += scnprintf(out, end - out, "%u: [", e->data_type);

		for (i = 0; i < e->nr; i++)
			out += scnprintf(out, end - out,
					 i ? " %u" : "%u", e->devs[i]);
		out += scnprintf(out, end - out, "]");
	}

	return out - buf;
}

/* Query replicas: */

bool bch2_replicas_marked(struct bch_fs *c,
			  enum bch_data_type data_type,
			  struct bch_devs_list devs)
{
	struct bch_replicas_cpu_entry search;
	unsigned max_dev;
	bool ret;

	if (!devs.nr)
		return true;

	devlist_to_replicas(devs, data_type, &search, &max_dev);

	rcu_read_lock();
	ret = replicas_has_entry(rcu_dereference(c->replicas),
				 search, max_dev);
	rcu_read_unlock();

	return ret;
}

bool bch2_bkey_replicas_marked(struct bch_fs *c,
			       enum bch_data_type data_type,
			       struct bkey_s_c k)
{
	struct bch_devs_list cached = bch2_bkey_cached_devs(k);
	unsigned i;

	for (i = 0; i < cached.nr; i++)
		if (!bch2_replicas_marked(c, BCH_DATA_CACHED,
					  bch2_dev_list_single(cached.devs[i])))
			return false;

	return bch2_replicas_marked(c, data_type, bch2_bkey_dirty_devs(k));
}

struct replicas_status __bch2_replicas_status(struct bch_fs *c,
					      struct bch_devs_mask online_devs)
{
	struct bch_sb_field_members *mi;
	struct bch_replicas_cpu_entry *e;
	struct bch_replicas_cpu *r;
	unsigned i, dev, dev_slots, nr_online, nr_offline;
	struct replicas_status ret;

	memset(&ret, 0, sizeof(ret));

	for (i = 0; i < ARRAY_SIZE(ret.replicas); i++)
		ret.replicas[i].nr_online = UINT_MAX;

	mi = bch2_sb_get_members(c->disk_sb.sb);
	rcu_read_lock();

	r = rcu_dereference(c->replicas);
	dev_slots = replicas_dev_slots(r);

	for_each_cpu_replicas_entry(r, e) {
		if (e->data_type >= ARRAY_SIZE(ret.replicas))
			panic("e %p data_type %u\n", e, e->data_type);

		nr_online = nr_offline = 0;

		for (dev = 0; dev < dev_slots; dev++) {
			if (!replicas_test_dev(e, dev))
				continue;

			BUG_ON(!bch2_dev_exists(c->disk_sb.sb, mi, dev));

			if (test_bit(dev, online_devs.d))
				nr_online++;
			else
				nr_offline++;
		}

		ret.replicas[e->data_type].nr_online =
			min(ret.replicas[e->data_type].nr_online,
			    nr_online);

		ret.replicas[e->data_type].nr_offline =
			max(ret.replicas[e->data_type].nr_offline,
			    nr_offline);
	}

	rcu_read_unlock();

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
		(s.replicas[type].nr_online || force_if_lost);
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

unsigned bch2_replicas_online(struct bch_fs *c, bool meta)
{
	struct replicas_status s = bch2_replicas_status(c);

	return meta
		? min(s.replicas[BCH_DATA_JOURNAL].nr_online,
		      s.replicas[BCH_DATA_BTREE].nr_online)
		: s.replicas[BCH_DATA_USER].nr_online;
}

unsigned bch2_dev_has_data(struct bch_fs *c, struct bch_dev *ca)
{
	struct bch_replicas_cpu_entry *e;
	struct bch_replicas_cpu *r;
	unsigned ret = 0;

	rcu_read_lock();
	r = rcu_dereference(c->replicas);

	if (ca->dev_idx >= replicas_dev_slots(r))
		goto out;

	for_each_cpu_replicas_entry(r, e)
		if (replicas_test_dev(e, ca->dev_idx))
			ret |= 1 << e->data_type;
out:
	rcu_read_unlock();

	return ret;
}
