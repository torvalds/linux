// SPDX-License-Identifier: GPL-2.0
/*
 * bcache sysfs interfaces
 *
 * Copyright 2010, 2011 Kent Overstreet <kent.overstreet@gmail.com>
 * Copyright 2012 Google, Inc.
 */

#ifndef NO_BCACHEFS_SYSFS

#include "bcachefs.h"
#include "alloc_background.h"
#include "sysfs.h"
#include "btree_cache.h"
#include "btree_io.h"
#include "btree_iter.h"
#include "btree_update.h"
#include "btree_update_interior.h"
#include "btree_gc.h"
#include "buckets.h"
#include "disk_groups.h"
#include "ec.h"
#include "inode.h"
#include "journal.h"
#include "keylist.h"
#include "move.h"
#include "opts.h"
#include "rebalance.h"
#include "replicas.h"
#include "super-io.h"
#include "tests.h"

#include <linux/blkdev.h>
#include <linux/sort.h>
#include <linux/sched/clock.h>

#include "util.h"

#define SYSFS_OPS(type)							\
struct sysfs_ops type ## _sysfs_ops = {					\
	.show	= type ## _show,					\
	.store	= type ## _store					\
}

#define SHOW(fn)							\
static ssize_t fn ## _show(struct kobject *kobj, struct attribute *attr,\
			   char *buf)					\

#define STORE(fn)							\
static ssize_t fn ## _store(struct kobject *kobj, struct attribute *attr,\
			    const char *buf, size_t size)		\

#define __sysfs_attribute(_name, _mode)					\
	static struct attribute sysfs_##_name =				\
		{ .name = #_name, .mode = _mode }

#define write_attribute(n)	__sysfs_attribute(n, S_IWUSR)
#define read_attribute(n)	__sysfs_attribute(n, S_IRUGO)
#define rw_attribute(n)		__sysfs_attribute(n, S_IRUGO|S_IWUSR)

#define sysfs_printf(file, fmt, ...)					\
do {									\
	if (attr == &sysfs_ ## file)					\
		return scnprintf(buf, PAGE_SIZE, fmt "\n", __VA_ARGS__);\
} while (0)

#define sysfs_print(file, var)						\
do {									\
	if (attr == &sysfs_ ## file)					\
		return snprint(buf, PAGE_SIZE, var);			\
} while (0)

#define sysfs_hprint(file, val)						\
do {									\
	if (attr == &sysfs_ ## file) {					\
		struct printbuf out = _PBUF(buf, PAGE_SIZE);		\
		bch2_hprint(&out, val);					\
		pr_buf(&out, "\n");					\
		return out.pos - buf;					\
	}								\
} while (0)

#define var_printf(_var, fmt)	sysfs_printf(_var, fmt, var(_var))
#define var_print(_var)		sysfs_print(_var, var(_var))
#define var_hprint(_var)	sysfs_hprint(_var, var(_var))

#define sysfs_strtoul(file, var)					\
do {									\
	if (attr == &sysfs_ ## file)					\
		return strtoul_safe(buf, var) ?: (ssize_t) size;	\
} while (0)

#define sysfs_strtoul_clamp(file, var, min, max)			\
do {									\
	if (attr == &sysfs_ ## file)					\
		return strtoul_safe_clamp(buf, var, min, max)		\
			?: (ssize_t) size;				\
} while (0)

#define strtoul_or_return(cp)						\
({									\
	unsigned long _v;						\
	int _r = kstrtoul(cp, 10, &_v);					\
	if (_r)								\
		return _r;						\
	_v;								\
})

#define strtoul_restrict_or_return(cp, min, max)			\
({									\
	unsigned long __v = 0;						\
	int _r = strtoul_safe_restrict(cp, __v, min, max);		\
	if (_r)								\
		return _r;						\
	__v;								\
})

#define strtoi_h_or_return(cp)						\
({									\
	u64 _v;								\
	int _r = strtoi_h(cp, &_v);					\
	if (_r)								\
		return _r;						\
	_v;								\
})

#define sysfs_hatoi(file, var)						\
do {									\
	if (attr == &sysfs_ ## file)					\
		return strtoi_h(buf, &var) ?: (ssize_t) size;		\
} while (0)

write_attribute(trigger_journal_flush);
write_attribute(trigger_btree_coalesce);
write_attribute(trigger_gc);
write_attribute(trigger_alloc_write);
write_attribute(prune_cache);
rw_attribute(btree_gc_periodic);

read_attribute(uuid);
read_attribute(minor);
read_attribute(bucket_size);
read_attribute(block_size);
read_attribute(btree_node_size);
read_attribute(first_bucket);
read_attribute(nbuckets);
read_attribute(durability);
read_attribute(iodone);

read_attribute(io_latency_read);
read_attribute(io_latency_write);
read_attribute(io_latency_stats_read);
read_attribute(io_latency_stats_write);
read_attribute(congested);

read_attribute(bucket_quantiles_last_read);
read_attribute(bucket_quantiles_last_write);
read_attribute(bucket_quantiles_fragmentation);
read_attribute(bucket_quantiles_oldest_gen);

read_attribute(reserve_stats);
read_attribute(btree_cache_size);
read_attribute(compression_stats);
read_attribute(journal_debug);
read_attribute(journal_pins);
read_attribute(btree_updates);
read_attribute(dirty_btree_nodes);

read_attribute(internal_uuid);

read_attribute(has_data);
read_attribute(alloc_debug);
write_attribute(wake_allocator);

read_attribute(read_realloc_races);
read_attribute(extent_migrate_done);
read_attribute(extent_migrate_raced);

rw_attribute(journal_write_delay_ms);
rw_attribute(journal_reclaim_delay_ms);

rw_attribute(discard);
rw_attribute(cache_replacement_policy);
rw_attribute(label);

rw_attribute(copy_gc_enabled);
sysfs_pd_controller_attribute(copy_gc);

rw_attribute(rebalance_enabled);
sysfs_pd_controller_attribute(rebalance);
read_attribute(rebalance_work);
rw_attribute(promote_whole_extents);

read_attribute(new_stripes);

rw_attribute(pd_controllers_update_seconds);

read_attribute(meta_replicas_have);
read_attribute(data_replicas_have);

#ifdef CONFIG_BCACHEFS_TESTS
write_attribute(perf_test);
#endif /* CONFIG_BCACHEFS_TESTS */

#define BCH_DEBUG_PARAM(name, description)				\
	rw_attribute(name);

	BCH_DEBUG_PARAMS()
#undef BCH_DEBUG_PARAM

#define x(_name)						\
	static struct attribute sysfs_time_stat_##_name =		\
		{ .name = #_name, .mode = S_IRUGO };
	BCH_TIME_STATS()
#undef x

static struct attribute sysfs_state_rw = {
	.name = "state",
	.mode = S_IRUGO
};

static size_t bch2_btree_cache_size(struct bch_fs *c)
{
	size_t ret = 0;
	struct btree *b;

	mutex_lock(&c->btree_cache.lock);
	list_for_each_entry(b, &c->btree_cache.live, list)
		ret += btree_bytes(c);

	mutex_unlock(&c->btree_cache.lock);
	return ret;
}

static ssize_t show_fs_alloc_debug(struct bch_fs *c, char *buf)
{
	struct printbuf out = _PBUF(buf, PAGE_SIZE);
	struct bch_fs_usage_online *fs_usage = bch2_fs_usage_read(c);

	if (!fs_usage)
		return -ENOMEM;

	bch2_fs_usage_to_text(&out, c, fs_usage);

	percpu_up_read(&c->mark_lock);

	kfree(fs_usage);

	return out.pos - buf;
}

static ssize_t bch2_compression_stats(struct bch_fs *c, char *buf)
{
	struct btree_trans trans;
	struct btree_iter *iter;
	struct bkey_s_c k;
	u64 nr_uncompressed_extents = 0, uncompressed_sectors = 0,
	    nr_compressed_extents = 0,
	    compressed_sectors_compressed = 0,
	    compressed_sectors_uncompressed = 0;
	int ret;

	if (!test_bit(BCH_FS_STARTED, &c->flags))
		return -EPERM;

	bch2_trans_init(&trans, c, 0, 0);

	for_each_btree_key(&trans, iter, BTREE_ID_EXTENTS, POS_MIN, 0, k, ret)
		if (k.k->type == KEY_TYPE_extent) {
			struct bkey_s_c_extent e = bkey_s_c_to_extent(k);
			const union bch_extent_entry *entry;
			struct extent_ptr_decoded p;

			extent_for_each_ptr_decode(e, p, entry) {
				if (p.crc.compression_type == BCH_COMPRESSION_NONE) {
					nr_uncompressed_extents++;
					uncompressed_sectors += e.k->size;
				} else {
					nr_compressed_extents++;
					compressed_sectors_compressed +=
						p.crc.compressed_size;
					compressed_sectors_uncompressed +=
						p.crc.uncompressed_size;
				}

				/* only looking at the first ptr */
				break;
			}
		}

	ret = bch2_trans_exit(&trans) ?: ret;
	if (ret)
		return ret;

	return scnprintf(buf, PAGE_SIZE,
			"uncompressed data:\n"
			"	nr extents:			%llu\n"
			"	size (bytes):			%llu\n"
			"compressed data:\n"
			"	nr extents:			%llu\n"
			"	compressed size (bytes):	%llu\n"
			"	uncompressed size (bytes):	%llu\n",
			nr_uncompressed_extents,
			uncompressed_sectors << 9,
			nr_compressed_extents,
			compressed_sectors_compressed << 9,
			compressed_sectors_uncompressed << 9);
}

static ssize_t bch2_new_stripes(struct bch_fs *c, char *buf)
{
	char *out = buf, *end = buf + PAGE_SIZE;
	struct ec_stripe_head *h;
	struct ec_stripe_new *s;

	mutex_lock(&c->ec_new_stripe_lock);
	list_for_each_entry(h, &c->ec_new_stripe_list, list) {
		out += scnprintf(out, end - out,
				 "target %u algo %u redundancy %u:\n",
				 h->target, h->algo, h->redundancy);

		if (h->s)
			out += scnprintf(out, end - out,
					 "\tpending: blocks %u allocated %u\n",
					 h->s->blocks.nr,
					 bitmap_weight(h->s->blocks_allocated,
						       h->s->blocks.nr));

		mutex_lock(&h->lock);
		list_for_each_entry(s, &h->stripes, list)
			out += scnprintf(out, end - out,
					 "\tin flight: blocks %u allocated %u pin %u\n",
					 s->blocks.nr,
					 bitmap_weight(s->blocks_allocated,
						       s->blocks.nr),
					 atomic_read(&s->pin));
		mutex_unlock(&h->lock);

	}
	mutex_unlock(&c->ec_new_stripe_lock);

	return out - buf;
}

SHOW(bch2_fs)
{
	struct bch_fs *c = container_of(kobj, struct bch_fs, kobj);

	sysfs_print(minor,			c->minor);
	sysfs_printf(internal_uuid, "%pU",	c->sb.uuid.b);

	sysfs_print(journal_write_delay_ms,	c->journal.write_delay_ms);
	sysfs_print(journal_reclaim_delay_ms,	c->journal.reclaim_delay_ms);

	sysfs_print(block_size,			block_bytes(c));
	sysfs_print(btree_node_size,		btree_bytes(c));
	sysfs_hprint(btree_cache_size,		bch2_btree_cache_size(c));

	sysfs_print(read_realloc_races,
		    atomic_long_read(&c->read_realloc_races));
	sysfs_print(extent_migrate_done,
		    atomic_long_read(&c->extent_migrate_done));
	sysfs_print(extent_migrate_raced,
		    atomic_long_read(&c->extent_migrate_raced));

	sysfs_printf(btree_gc_periodic, "%u",	(int) c->btree_gc_periodic);

	sysfs_printf(copy_gc_enabled, "%i", c->copy_gc_enabled);

	sysfs_print(pd_controllers_update_seconds,
		    c->pd_controllers_update_seconds);

	sysfs_printf(rebalance_enabled,		"%i", c->rebalance.enabled);
	sysfs_pd_controller_show(rebalance,	&c->rebalance.pd); /* XXX */

	if (attr == &sysfs_rebalance_work)
		return bch2_rebalance_work_show(c, buf);

	sysfs_print(promote_whole_extents,	c->promote_whole_extents);

	sysfs_printf(meta_replicas_have, "%i",	bch2_replicas_online(c, true));
	sysfs_printf(data_replicas_have, "%i",	bch2_replicas_online(c, false));

	/* Debugging: */

	if (attr == &sysfs_alloc_debug)
		return show_fs_alloc_debug(c, buf);

	if (attr == &sysfs_journal_debug)
		return bch2_journal_print_debug(&c->journal, buf);

	if (attr == &sysfs_journal_pins)
		return bch2_journal_print_pins(&c->journal, buf);

	if (attr == &sysfs_btree_updates)
		return bch2_btree_updates_print(c, buf);

	if (attr == &sysfs_dirty_btree_nodes)
		return bch2_dirty_btree_nodes_print(c, buf);

	if (attr == &sysfs_compression_stats)
		return bch2_compression_stats(c, buf);

	if (attr == &sysfs_new_stripes)
		return bch2_new_stripes(c, buf);

#define BCH_DEBUG_PARAM(name, description) sysfs_print(name, c->name);
	BCH_DEBUG_PARAMS()
#undef BCH_DEBUG_PARAM

	return 0;
}

STORE(__bch2_fs)
{
	struct bch_fs *c = container_of(kobj, struct bch_fs, kobj);

	sysfs_strtoul(journal_write_delay_ms, c->journal.write_delay_ms);
	sysfs_strtoul(journal_reclaim_delay_ms, c->journal.reclaim_delay_ms);

	if (attr == &sysfs_btree_gc_periodic) {
		ssize_t ret = strtoul_safe(buf, c->btree_gc_periodic)
			?: (ssize_t) size;

		wake_up_process(c->gc_thread);
		return ret;
	}

	if (attr == &sysfs_copy_gc_enabled) {
		struct bch_dev *ca;
		unsigned i;
		ssize_t ret = strtoul_safe(buf, c->copy_gc_enabled)
			?: (ssize_t) size;

		for_each_member_device(ca, c, i)
			if (ca->copygc_thread)
				wake_up_process(ca->copygc_thread);
		return ret;
	}

	if (attr == &sysfs_rebalance_enabled) {
		ssize_t ret = strtoul_safe(buf, c->rebalance.enabled)
			?: (ssize_t) size;

		rebalance_wakeup(c);
		return ret;
	}

	sysfs_strtoul(pd_controllers_update_seconds,
		      c->pd_controllers_update_seconds);
	sysfs_pd_controller_store(rebalance,	&c->rebalance.pd);

	sysfs_strtoul(promote_whole_extents,	c->promote_whole_extents);

	/* Debugging: */

#define BCH_DEBUG_PARAM(name, description) sysfs_strtoul(name, c->name);
	BCH_DEBUG_PARAMS()
#undef BCH_DEBUG_PARAM

	if (!test_bit(BCH_FS_STARTED, &c->flags))
		return -EPERM;

	/* Debugging: */

	if (attr == &sysfs_trigger_journal_flush)
		bch2_journal_meta_async(&c->journal, NULL);

	if (attr == &sysfs_trigger_btree_coalesce)
		bch2_coalesce(c);

	if (attr == &sysfs_trigger_gc)
		bch2_gc(c, NULL, false, false);

	if (attr == &sysfs_trigger_alloc_write) {
		bool wrote;

		bch2_alloc_write(c, 0, &wrote);
	}

	if (attr == &sysfs_prune_cache) {
		struct shrink_control sc;

		sc.gfp_mask = GFP_KERNEL;
		sc.nr_to_scan = strtoul_or_return(buf);
		c->btree_cache.shrink.scan_objects(&c->btree_cache.shrink, &sc);
	}
#ifdef CONFIG_BCACHEFS_TESTS
	if (attr == &sysfs_perf_test) {
		char *tmp = kstrdup(buf, GFP_KERNEL), *p = tmp;
		char *test		= strsep(&p, " \t\n");
		char *nr_str		= strsep(&p, " \t\n");
		char *threads_str	= strsep(&p, " \t\n");
		unsigned threads;
		u64 nr;
		int ret = -EINVAL;

		if (threads_str &&
		    !(ret = kstrtouint(threads_str, 10, &threads)) &&
		    !(ret = bch2_strtoull_h(nr_str, &nr)))
			bch2_btree_perf_test(c, test, nr, threads);
		else
			size = ret;
		kfree(tmp);
	}
#endif
	return size;
}

STORE(bch2_fs)
{
	struct bch_fs *c = container_of(kobj, struct bch_fs, kobj);

	mutex_lock(&c->state_lock);
	size = __bch2_fs_store(kobj, attr, buf, size);
	mutex_unlock(&c->state_lock);

	return size;
}
SYSFS_OPS(bch2_fs);

struct attribute *bch2_fs_files[] = {
	&sysfs_minor,
	&sysfs_block_size,
	&sysfs_btree_node_size,
	&sysfs_btree_cache_size,

	&sysfs_meta_replicas_have,
	&sysfs_data_replicas_have,

	&sysfs_journal_write_delay_ms,
	&sysfs_journal_reclaim_delay_ms,

	&sysfs_promote_whole_extents,

	&sysfs_compression_stats,

#ifdef CONFIG_BCACHEFS_TESTS
	&sysfs_perf_test,
#endif
	NULL
};

/* internal dir - just a wrapper */

SHOW(bch2_fs_internal)
{
	struct bch_fs *c = container_of(kobj, struct bch_fs, internal);
	return bch2_fs_show(&c->kobj, attr, buf);
}

STORE(bch2_fs_internal)
{
	struct bch_fs *c = container_of(kobj, struct bch_fs, internal);
	return bch2_fs_store(&c->kobj, attr, buf, size);
}
SYSFS_OPS(bch2_fs_internal);

struct attribute *bch2_fs_internal_files[] = {
	&sysfs_alloc_debug,
	&sysfs_journal_debug,
	&sysfs_journal_pins,
	&sysfs_btree_updates,
	&sysfs_dirty_btree_nodes,

	&sysfs_read_realloc_races,
	&sysfs_extent_migrate_done,
	&sysfs_extent_migrate_raced,

	&sysfs_trigger_journal_flush,
	&sysfs_trigger_btree_coalesce,
	&sysfs_trigger_gc,
	&sysfs_trigger_alloc_write,
	&sysfs_prune_cache,

	&sysfs_copy_gc_enabled,

	&sysfs_rebalance_enabled,
	&sysfs_rebalance_work,
	sysfs_pd_controller_files(rebalance),

	&sysfs_new_stripes,

	&sysfs_internal_uuid,

#define BCH_DEBUG_PARAM(name, description) &sysfs_##name,
	BCH_DEBUG_PARAMS()
#undef BCH_DEBUG_PARAM

	NULL
};

/* options */

SHOW(bch2_fs_opts_dir)
{
	struct printbuf out = _PBUF(buf, PAGE_SIZE);
	struct bch_fs *c = container_of(kobj, struct bch_fs, opts_dir);
	const struct bch_option *opt = container_of(attr, struct bch_option, attr);
	int id = opt - bch2_opt_table;
	u64 v = bch2_opt_get_by_id(&c->opts, id);

	bch2_opt_to_text(&out, c, opt, v, OPT_SHOW_FULL_LIST);
	pr_buf(&out, "\n");

	return out.pos - buf;
}

STORE(bch2_fs_opts_dir)
{
	struct bch_fs *c = container_of(kobj, struct bch_fs, opts_dir);
	const struct bch_option *opt = container_of(attr, struct bch_option, attr);
	int ret, id = opt - bch2_opt_table;
	char *tmp;
	u64 v;

	tmp = kstrdup(buf, GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;

	ret = bch2_opt_parse(c, opt, strim(tmp), &v);
	kfree(tmp);

	if (ret < 0)
		return ret;

	ret = bch2_opt_check_may_set(c, id, v);
	if (ret < 0)
		return ret;

	if (opt->set_sb != SET_NO_SB_OPT) {
		mutex_lock(&c->sb_lock);
		opt->set_sb(c->disk_sb.sb, v);
		bch2_write_super(c);
		mutex_unlock(&c->sb_lock);
	}

	bch2_opt_set_by_id(&c->opts, id, v);

	if ((id == Opt_background_target ||
	     id == Opt_background_compression) && v) {
		bch2_rebalance_add_work(c, S64_MAX);
		rebalance_wakeup(c);
	}

	return size;
}
SYSFS_OPS(bch2_fs_opts_dir);

struct attribute *bch2_fs_opts_dir_files[] = { NULL };

int bch2_opts_create_sysfs_files(struct kobject *kobj)
{
	const struct bch_option *i;
	int ret;

	for (i = bch2_opt_table;
	     i < bch2_opt_table + bch2_opts_nr;
	     i++) {
		if (!(i->mode & (OPT_FORMAT|OPT_MOUNT|OPT_RUNTIME)))
			continue;

		ret = sysfs_create_file(kobj, &i->attr);
		if (ret)
			return ret;
	}

	return 0;
}

/* time stats */

SHOW(bch2_fs_time_stats)
{
	struct bch_fs *c = container_of(kobj, struct bch_fs, time_stats);

#define x(name)						\
	if (attr == &sysfs_time_stat_##name)				\
		return bch2_time_stats_print(&c->times[BCH_TIME_##name],\
					     buf, PAGE_SIZE);
	BCH_TIME_STATS()
#undef x

	return 0;
}

STORE(bch2_fs_time_stats)
{
	return size;
}
SYSFS_OPS(bch2_fs_time_stats);

struct attribute *bch2_fs_time_stats_files[] = {
#define x(name)						\
	&sysfs_time_stat_##name,
	BCH_TIME_STATS()
#undef x
	NULL
};

typedef unsigned (bucket_map_fn)(struct bch_fs *, struct bch_dev *,
				 size_t, void *);

static unsigned bucket_last_io_fn(struct bch_fs *c, struct bch_dev *ca,
				  size_t b, void *private)
{
	int rw = (private ? 1 : 0);

	return bucket_last_io(c, bucket(ca, b), rw);
}

static unsigned bucket_sectors_used_fn(struct bch_fs *c, struct bch_dev *ca,
				       size_t b, void *private)
{
	struct bucket *g = bucket(ca, b);
	return bucket_sectors_used(g->mark);
}

static unsigned bucket_oldest_gen_fn(struct bch_fs *c, struct bch_dev *ca,
				     size_t b, void *private)
{
	return bucket_gc_gen(ca, b);
}

static int unsigned_cmp(const void *_l, const void *_r)
{
	const unsigned *l = _l;
	const unsigned *r = _r;

	return cmp_int(*l, *r);
}

static ssize_t show_quantiles(struct bch_fs *c, struct bch_dev *ca,
			      char *buf, bucket_map_fn *fn, void *private)
{
	size_t i, n;
	/* Compute 31 quantiles */
	unsigned q[31], *p;
	ssize_t ret = 0;

	down_read(&ca->bucket_lock);
	n = ca->mi.nbuckets;

	p = vzalloc(n * sizeof(unsigned));
	if (!p) {
		up_read(&ca->bucket_lock);
		return -ENOMEM;
	}

	for (i = ca->mi.first_bucket; i < n; i++)
		p[i] = fn(c, ca, i, private);

	sort(p, n, sizeof(unsigned), unsigned_cmp, NULL);
	up_read(&ca->bucket_lock);

	while (n &&
	       !p[n - 1])
		--n;

	for (i = 0; i < ARRAY_SIZE(q); i++)
		q[i] = p[n * (i + 1) / (ARRAY_SIZE(q) + 1)];

	vfree(p);

	for (i = 0; i < ARRAY_SIZE(q); i++)
		ret += scnprintf(buf + ret, PAGE_SIZE - ret,
				 "%u ", q[i]);
	buf[ret - 1] = '\n';

	return ret;
}

static ssize_t show_reserve_stats(struct bch_dev *ca, char *buf)
{
	struct printbuf out = _PBUF(buf, PAGE_SIZE);
	enum alloc_reserve i;

	spin_lock(&ca->freelist_lock);

	pr_buf(&out, "free_inc:\t%zu\t%zu\n",
	       fifo_used(&ca->free_inc),
	       ca->free_inc.size);

	for (i = 0; i < RESERVE_NR; i++)
		pr_buf(&out, "free[%u]:\t%zu\t%zu\n", i,
		       fifo_used(&ca->free[i]),
		       ca->free[i].size);

	spin_unlock(&ca->freelist_lock);

	return out.pos - buf;
}

static ssize_t show_dev_alloc_debug(struct bch_dev *ca, char *buf)
{
	struct bch_fs *c = ca->fs;
	struct bch_dev_usage stats = bch2_dev_usage_read(c, ca);
	unsigned i, nr[BCH_DATA_NR];

	memset(nr, 0, sizeof(nr));

	for (i = 0; i < ARRAY_SIZE(c->open_buckets); i++)
		nr[c->open_buckets[i].type]++;

	return scnprintf(buf, PAGE_SIZE,
		"free_inc:               %zu/%zu\n"
		"free[RESERVE_BTREE]:    %zu/%zu\n"
		"free[RESERVE_MOVINGGC]: %zu/%zu\n"
		"free[RESERVE_NONE]:     %zu/%zu\n"
		"buckets:\n"
		"    capacity:           %llu\n"
		"    sb:                 %llu\n"
		"    journal:            %llu\n"
		"    meta:               %llu\n"
		"    user:               %llu\n"
		"    cached:             %llu\n"
		"    erasure coded:      %llu\n"
		"    available:          %lli\n"
		"sectors:\n"
		"    sb:                 %llu\n"
		"    journal:            %llu\n"
		"    meta:               %llu\n"
		"    user:               %llu\n"
		"    cached:             %llu\n"
		"    fragmented:         %llu\n"
		"    copygc threshold:   %llu\n"
		"freelist_wait:          %s\n"
		"open buckets:           %u/%u (reserved %u)\n"
		"open_buckets_wait:      %s\n"
		"open_buckets_btree:     %u\n"
		"open_buckets_user:      %u\n"
		"btree reserve cache:    %u\n",
		fifo_used(&ca->free_inc),		ca->free_inc.size,
		fifo_used(&ca->free[RESERVE_BTREE]),	ca->free[RESERVE_BTREE].size,
		fifo_used(&ca->free[RESERVE_MOVINGGC]),	ca->free[RESERVE_MOVINGGC].size,
		fifo_used(&ca->free[RESERVE_NONE]),	ca->free[RESERVE_NONE].size,
		ca->mi.nbuckets - ca->mi.first_bucket,
		stats.buckets[BCH_DATA_SB],
		stats.buckets[BCH_DATA_JOURNAL],
		stats.buckets[BCH_DATA_BTREE],
		stats.buckets[BCH_DATA_USER],
		stats.buckets[BCH_DATA_CACHED],
		stats.buckets_ec,
		ca->mi.nbuckets - ca->mi.first_bucket - stats.buckets_unavailable,
		stats.sectors[BCH_DATA_SB],
		stats.sectors[BCH_DATA_JOURNAL],
		stats.sectors[BCH_DATA_BTREE],
		stats.sectors[BCH_DATA_USER],
		stats.sectors[BCH_DATA_CACHED],
		stats.sectors_fragmented,
		ca->copygc_threshold,
		c->freelist_wait.list.first		? "waiting" : "empty",
		c->open_buckets_nr_free, OPEN_BUCKETS_COUNT,
		BTREE_NODE_OPEN_BUCKET_RESERVE,
		c->open_buckets_wait.list.first		? "waiting" : "empty",
		nr[BCH_DATA_BTREE],
		nr[BCH_DATA_USER],
		c->btree_reserve_cache_nr);
}

static const char * const bch2_rw[] = {
	"read",
	"write",
	NULL
};

static ssize_t show_dev_iodone(struct bch_dev *ca, char *buf)
{
	struct printbuf out = _PBUF(buf, PAGE_SIZE);
	int rw, i;

	for (rw = 0; rw < 2; rw++) {
		pr_buf(&out, "%s:\n", bch2_rw[rw]);

		for (i = 1; i < BCH_DATA_NR; i++)
			pr_buf(&out, "%-12s:%12llu\n",
			       bch2_data_types[i],
			       percpu_u64_get(&ca->io_done->sectors[rw][i]) << 9);
	}

	return out.pos - buf;
}

SHOW(bch2_dev)
{
	struct bch_dev *ca = container_of(kobj, struct bch_dev, kobj);
	struct bch_fs *c = ca->fs;
	struct printbuf out = _PBUF(buf, PAGE_SIZE);

	sysfs_printf(uuid,		"%pU\n", ca->uuid.b);

	sysfs_print(bucket_size,	bucket_bytes(ca));
	sysfs_print(block_size,		block_bytes(c));
	sysfs_print(first_bucket,	ca->mi.first_bucket);
	sysfs_print(nbuckets,		ca->mi.nbuckets);
	sysfs_print(durability,		ca->mi.durability);
	sysfs_print(discard,		ca->mi.discard);

	if (attr == &sysfs_label) {
		if (ca->mi.group) {
			mutex_lock(&c->sb_lock);
			bch2_disk_path_to_text(&out, &c->disk_sb,
					       ca->mi.group - 1);
			mutex_unlock(&c->sb_lock);
		} else {
			pr_buf(&out, "none");
		}

		pr_buf(&out, "\n");
		return out.pos - buf;
	}

	if (attr == &sysfs_has_data) {
		bch2_flags_to_text(&out, bch2_data_types,
				   bch2_dev_has_data(c, ca));
		pr_buf(&out, "\n");
		return out.pos - buf;
	}

	sysfs_pd_controller_show(copy_gc, &ca->copygc_pd);

	if (attr == &sysfs_cache_replacement_policy) {
		bch2_string_opt_to_text(&out,
					bch2_cache_replacement_policies,
					ca->mi.replacement);
		pr_buf(&out, "\n");
		return out.pos - buf;
	}

	if (attr == &sysfs_state_rw) {
		bch2_string_opt_to_text(&out, bch2_dev_state,
					ca->mi.state);
		pr_buf(&out, "\n");
		return out.pos - buf;
	}

	if (attr == &sysfs_iodone)
		return show_dev_iodone(ca, buf);

	sysfs_print(io_latency_read,		atomic64_read(&ca->cur_latency[READ]));
	sysfs_print(io_latency_write,		atomic64_read(&ca->cur_latency[WRITE]));

	if (attr == &sysfs_io_latency_stats_read)
		return bch2_time_stats_print(&ca->io_latency[READ], buf, PAGE_SIZE);
	if (attr == &sysfs_io_latency_stats_write)
		return bch2_time_stats_print(&ca->io_latency[WRITE], buf, PAGE_SIZE);

	sysfs_printf(congested,			"%u%%",
		     clamp(atomic_read(&ca->congested), 0, CONGESTED_MAX)
		     * 100 / CONGESTED_MAX);

	if (attr == &sysfs_bucket_quantiles_last_read)
		return show_quantiles(c, ca, buf, bucket_last_io_fn, (void *) 0);
	if (attr == &sysfs_bucket_quantiles_last_write)
		return show_quantiles(c, ca, buf, bucket_last_io_fn, (void *) 1);
	if (attr == &sysfs_bucket_quantiles_fragmentation)
		return show_quantiles(c, ca, buf, bucket_sectors_used_fn, NULL);
	if (attr == &sysfs_bucket_quantiles_oldest_gen)
		return show_quantiles(c, ca, buf, bucket_oldest_gen_fn, NULL);

	if (attr == &sysfs_reserve_stats)
		return show_reserve_stats(ca, buf);
	if (attr == &sysfs_alloc_debug)
		return show_dev_alloc_debug(ca, buf);

	return 0;
}

STORE(bch2_dev)
{
	struct bch_dev *ca = container_of(kobj, struct bch_dev, kobj);
	struct bch_fs *c = ca->fs;
	struct bch_member *mi;

	sysfs_pd_controller_store(copy_gc, &ca->copygc_pd);

	if (attr == &sysfs_discard) {
		bool v = strtoul_or_return(buf);

		mutex_lock(&c->sb_lock);
		mi = &bch2_sb_get_members(c->disk_sb.sb)->members[ca->dev_idx];

		if (v != BCH_MEMBER_DISCARD(mi)) {
			SET_BCH_MEMBER_DISCARD(mi, v);
			bch2_write_super(c);
		}
		mutex_unlock(&c->sb_lock);
	}

	if (attr == &sysfs_cache_replacement_policy) {
		ssize_t v = __sysfs_match_string(bch2_cache_replacement_policies, -1, buf);

		if (v < 0)
			return v;

		mutex_lock(&c->sb_lock);
		mi = &bch2_sb_get_members(c->disk_sb.sb)->members[ca->dev_idx];

		if ((unsigned) v != BCH_MEMBER_REPLACEMENT(mi)) {
			SET_BCH_MEMBER_REPLACEMENT(mi, v);
			bch2_write_super(c);
		}
		mutex_unlock(&c->sb_lock);
	}

	if (attr == &sysfs_label) {
		char *tmp;
		int ret;

		tmp = kstrdup(buf, GFP_KERNEL);
		if (!tmp)
			return -ENOMEM;

		ret = bch2_dev_group_set(c, ca, strim(tmp));
		kfree(tmp);
		if (ret)
			return ret;
	}

	if (attr == &sysfs_wake_allocator)
		bch2_wake_allocator(ca);

	return size;
}
SYSFS_OPS(bch2_dev);

struct attribute *bch2_dev_files[] = {
	&sysfs_uuid,
	&sysfs_bucket_size,
	&sysfs_block_size,
	&sysfs_first_bucket,
	&sysfs_nbuckets,
	&sysfs_durability,

	/* settings: */
	&sysfs_discard,
	&sysfs_cache_replacement_policy,
	&sysfs_state_rw,
	&sysfs_label,

	&sysfs_has_data,
	&sysfs_iodone,

	&sysfs_io_latency_read,
	&sysfs_io_latency_write,
	&sysfs_io_latency_stats_read,
	&sysfs_io_latency_stats_write,
	&sysfs_congested,

	/* alloc info - other stats: */
	&sysfs_bucket_quantiles_last_read,
	&sysfs_bucket_quantiles_last_write,
	&sysfs_bucket_quantiles_fragmentation,
	&sysfs_bucket_quantiles_oldest_gen,

	&sysfs_reserve_stats,

	/* debug: */
	&sysfs_alloc_debug,
	&sysfs_wake_allocator,

	sysfs_pd_controller_files(copy_gc),
	NULL
};

#endif  /* _BCACHEFS_SYSFS_H_ */
