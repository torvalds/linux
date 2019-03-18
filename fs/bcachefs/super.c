// SPDX-License-Identifier: GPL-2.0
/*
 * bcachefs setup/teardown code, and some metadata io - read a superblock and
 * figure out what to do with it.
 *
 * Copyright 2010, 2011 Kent Overstreet <kent.overstreet@gmail.com>
 * Copyright 2012 Google, Inc.
 */

#include "bcachefs.h"
#include "alloc_background.h"
#include "alloc_foreground.h"
#include "bkey_sort.h"
#include "btree_cache.h"
#include "btree_gc.h"
#include "btree_update_interior.h"
#include "btree_io.h"
#include "chardev.h"
#include "checksum.h"
#include "clock.h"
#include "compress.h"
#include "debug.h"
#include "disk_groups.h"
#include "ec.h"
#include "error.h"
#include "fs.h"
#include "fs-io.h"
#include "fsck.h"
#include "inode.h"
#include "io.h"
#include "journal.h"
#include "journal_reclaim.h"
#include "move.h"
#include "migrate.h"
#include "movinggc.h"
#include "quota.h"
#include "rebalance.h"
#include "recovery.h"
#include "replicas.h"
#include "super.h"
#include "super-io.h"
#include "sysfs.h"
#include "trace.h"

#include <linux/backing-dev.h>
#include <linux/blkdev.h>
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/idr.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/percpu.h>
#include <linux/random.h>
#include <linux/sysfs.h>
#include <crypto/hash.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kent Overstreet <kent.overstreet@gmail.com>");

#define KTYPE(type)							\
static const struct attribute_group type ## _group = {			\
	.attrs = type ## _files						\
};									\
									\
static const struct attribute_group *type ## _groups[] = {		\
	&type ## _group,						\
	NULL								\
};									\
									\
static const struct kobj_type type ## _ktype = {			\
	.release	= type ## _release,				\
	.sysfs_ops	= &type ## _sysfs_ops,				\
	.default_groups = type ## _groups				\
}

static void bch2_fs_release(struct kobject *);
static void bch2_dev_release(struct kobject *);

static void bch2_fs_internal_release(struct kobject *k)
{
}

static void bch2_fs_opts_dir_release(struct kobject *k)
{
}

static void bch2_fs_time_stats_release(struct kobject *k)
{
}

KTYPE(bch2_fs);
KTYPE(bch2_fs_internal);
KTYPE(bch2_fs_opts_dir);
KTYPE(bch2_fs_time_stats);
KTYPE(bch2_dev);

static struct kset *bcachefs_kset;
static LIST_HEAD(bch_fs_list);
static DEFINE_MUTEX(bch_fs_list_lock);

static DECLARE_WAIT_QUEUE_HEAD(bch_read_only_wait);

static void bch2_dev_free(struct bch_dev *);
static int bch2_dev_alloc(struct bch_fs *, unsigned);
static int bch2_dev_sysfs_online(struct bch_fs *, struct bch_dev *);
static void __bch2_dev_read_only(struct bch_fs *, struct bch_dev *);

struct bch_fs *bch2_dev_to_fs(dev_t dev)
{
	struct bch_fs *c;
	struct bch_dev *ca;
	unsigned i;

	mutex_lock(&bch_fs_list_lock);
	rcu_read_lock();

	list_for_each_entry(c, &bch_fs_list, list)
		for_each_member_device_rcu(ca, c, i, NULL)
			if (ca->disk_sb.bdev->bd_dev == dev) {
				closure_get(&c->cl);
				goto found;
			}
	c = NULL;
found:
	rcu_read_unlock();
	mutex_unlock(&bch_fs_list_lock);

	return c;
}

static struct bch_fs *__bch2_uuid_to_fs(__uuid_t uuid)
{
	struct bch_fs *c;

	lockdep_assert_held(&bch_fs_list_lock);

	list_for_each_entry(c, &bch_fs_list, list)
		if (!memcmp(&c->disk_sb.sb->uuid, &uuid, sizeof(uuid)))
			return c;

	return NULL;
}

struct bch_fs *bch2_uuid_to_fs(__uuid_t uuid)
{
	struct bch_fs *c;

	mutex_lock(&bch_fs_list_lock);
	c = __bch2_uuid_to_fs(uuid);
	if (c)
		closure_get(&c->cl);
	mutex_unlock(&bch_fs_list_lock);

	return c;
}

/* Filesystem RO/RW: */

/*
 * For startup/shutdown of RW stuff, the dependencies are:
 *
 * - foreground writes depend on copygc and rebalance (to free up space)
 *
 * - copygc and rebalance depend on mark and sweep gc (they actually probably
 *   don't because they either reserve ahead of time or don't block if
 *   allocations fail, but allocations can require mark and sweep gc to run
 *   because of generation number wraparound)
 *
 * - all of the above depends on the allocator threads
 *
 * - allocator depends on the journal (when it rewrites prios and gens)
 */

static void __bch2_fs_read_only(struct bch_fs *c)
{
	struct bch_dev *ca;
	bool wrote;
	unsigned i;
	int ret;

	bch2_rebalance_stop(c);

	for_each_member_device(ca, c, i)
		bch2_copygc_stop(ca);

	bch2_gc_thread_stop(c);

	/*
	 * Flush journal before stopping allocators, because flushing journal
	 * blacklist entries involves allocating new btree nodes:
	 */
	bch2_journal_flush_all_pins(&c->journal);

	if (!test_bit(BCH_FS_ALLOCATOR_RUNNING, &c->flags))
		goto allocator_not_running;

	do {
		ret = bch2_alloc_write(c, false, &wrote);
		if (ret) {
			bch2_fs_inconsistent(c, "error writing out alloc info %i", ret);
			break;
		}

		ret = bch2_stripes_write(c, &wrote);
		if (ret) {
			bch2_fs_inconsistent(c, "error writing out stripes");
			break;
		}

		for_each_member_device(ca, c, i)
			bch2_dev_allocator_quiesce(c, ca);

		bch2_journal_flush_all_pins(&c->journal);

		/*
		 * We need to explicitly wait on btree interior updates to complete
		 * before stopping the journal, flushing all journal pins isn't
		 * sufficient, because in the BTREE_INTERIOR_UPDATING_ROOT case btree
		 * interior updates have to drop their journal pin before they're
		 * fully complete:
		 */
		closure_wait_event(&c->btree_interior_update_wait,
				   !bch2_btree_interior_updates_nr_pending(c));
	} while (wrote);
allocator_not_running:
	for_each_member_device(ca, c, i)
		bch2_dev_allocator_stop(ca);

	clear_bit(BCH_FS_ALLOCATOR_RUNNING, &c->flags);

	bch2_fs_journal_stop(&c->journal);

	/* XXX: mark super that alloc info is persistent */

	/*
	 * the journal kicks off btree writes via reclaim - wait for in flight
	 * writes after stopping journal:
	 */
	if (test_bit(BCH_FS_EMERGENCY_RO, &c->flags))
		bch2_btree_flush_all_writes(c);
	else
		bch2_btree_verify_flushed(c);

	/*
	 * After stopping journal:
	 */
	for_each_member_device(ca, c, i)
		bch2_dev_allocator_remove(c, ca);
}

static void bch2_writes_disabled(struct percpu_ref *writes)
{
	struct bch_fs *c = container_of(writes, struct bch_fs, writes);

	set_bit(BCH_FS_WRITE_DISABLE_COMPLETE, &c->flags);
	wake_up(&bch_read_only_wait);
}

void bch2_fs_read_only(struct bch_fs *c)
{
	if (c->state == BCH_FS_RO)
		return;

	BUG_ON(test_bit(BCH_FS_WRITE_DISABLE_COMPLETE, &c->flags));

	/*
	 * Block new foreground-end write operations from starting - any new
	 * writes will return -EROFS:
	 *
	 * (This is really blocking new _allocations_, writes to previously
	 * allocated space can still happen until stopping the allocator in
	 * bch2_dev_allocator_stop()).
	 */
	percpu_ref_kill(&c->writes);

	cancel_delayed_work(&c->pd_controllers_update);

	/*
	 * If we're not doing an emergency shutdown, we want to wait on
	 * outstanding writes to complete so they don't see spurious errors due
	 * to shutting down the allocator:
	 *
	 * If we are doing an emergency shutdown outstanding writes may
	 * hang until we shutdown the allocator so we don't want to wait
	 * on outstanding writes before shutting everything down - but
	 * we do need to wait on them before returning and signalling
	 * that going RO is complete:
	 */
	wait_event(bch_read_only_wait,
		   test_bit(BCH_FS_WRITE_DISABLE_COMPLETE, &c->flags) ||
		   test_bit(BCH_FS_EMERGENCY_RO, &c->flags));

	__bch2_fs_read_only(c);

	wait_event(bch_read_only_wait,
		   test_bit(BCH_FS_WRITE_DISABLE_COMPLETE, &c->flags));

	clear_bit(BCH_FS_WRITE_DISABLE_COMPLETE, &c->flags);

	if (!bch2_journal_error(&c->journal) &&
	    !test_bit(BCH_FS_ERROR, &c->flags) &&
	    !test_bit(BCH_FS_EMERGENCY_RO, &c->flags) &&
	    test_bit(BCH_FS_STARTED, &c->flags))
		bch2_fs_mark_clean(c, true);

	if (c->state != BCH_FS_STOPPING)
		c->state = BCH_FS_RO;
}

static void bch2_fs_read_only_work(struct work_struct *work)
{
	struct bch_fs *c =
		container_of(work, struct bch_fs, read_only_work);

	mutex_lock(&c->state_lock);
	bch2_fs_read_only(c);
	mutex_unlock(&c->state_lock);
}

static void bch2_fs_read_only_async(struct bch_fs *c)
{
	queue_work(system_long_wq, &c->read_only_work);
}

bool bch2_fs_emergency_read_only(struct bch_fs *c)
{
	bool ret = !test_and_set_bit(BCH_FS_EMERGENCY_RO, &c->flags);

	bch2_fs_read_only_async(c);
	bch2_journal_halt(&c->journal);

	wake_up(&bch_read_only_wait);
	return ret;
}

const char *bch2_fs_read_write(struct bch_fs *c)
{
	struct bch_dev *ca;
	const char *err = NULL;
	unsigned i;

	if (c->state == BCH_FS_RW)
		return NULL;

	bch2_fs_mark_clean(c, false);

	for_each_rw_member(ca, c, i)
		bch2_dev_allocator_add(c, ca);
	bch2_recalc_capacity(c);

	err = "error starting allocator thread";
	for_each_rw_member(ca, c, i)
		if (bch2_dev_allocator_start(ca)) {
			percpu_ref_put(&ca->io_ref);
			goto err;
		}

	set_bit(BCH_FS_ALLOCATOR_RUNNING, &c->flags);

	err = "error starting btree GC thread";
	if (bch2_gc_thread_start(c))
		goto err;

	err = "error starting copygc thread";
	for_each_rw_member(ca, c, i)
		if (bch2_copygc_start(c, ca)) {
			percpu_ref_put(&ca->io_ref);
			goto err;
		}

	err = "error starting rebalance thread";
	if (bch2_rebalance_start(c))
		goto err;

	schedule_delayed_work(&c->pd_controllers_update, 5 * HZ);

	if (c->state != BCH_FS_STARTING)
		percpu_ref_reinit(&c->writes);

	c->state = BCH_FS_RW;
	return NULL;
err:
	__bch2_fs_read_only(c);
	return err;
}

/* Filesystem startup/shutdown: */

static void bch2_fs_free(struct bch_fs *c)
{
	unsigned i;

	for (i = 0; i < BCH_TIME_STAT_NR; i++)
		bch2_time_stats_exit(&c->times[i]);

	bch2_fs_quota_exit(c);
	bch2_fs_fsio_exit(c);
	bch2_fs_ec_exit(c);
	bch2_fs_encryption_exit(c);
	bch2_fs_io_exit(c);
	bch2_fs_btree_cache_exit(c);
	bch2_fs_journal_exit(&c->journal);
	bch2_io_clock_exit(&c->io_clock[WRITE]);
	bch2_io_clock_exit(&c->io_clock[READ]);
	bch2_fs_compress_exit(c);
	percpu_free_rwsem(&c->mark_lock);
	kfree(c->usage_scratch);
	free_percpu(c->usage[0]);
	free_percpu(c->pcpu);
	mempool_exit(&c->btree_iters_pool);
	mempool_exit(&c->btree_bounce_pool);
	bioset_exit(&c->btree_bio);
	mempool_exit(&c->btree_interior_update_pool);
	mempool_exit(&c->btree_reserve_pool);
	mempool_exit(&c->fill_iter);
	percpu_ref_exit(&c->writes);
	kfree(c->replicas.entries);
	kfree(c->replicas_gc.entries);
	kfree(rcu_dereference_protected(c->disk_groups, 1));

	if (c->journal_reclaim_wq)
		destroy_workqueue(c->journal_reclaim_wq);
	if (c->copygc_wq)
		destroy_workqueue(c->copygc_wq);
	if (c->wq)
		destroy_workqueue(c->wq);

	free_pages((unsigned long) c->disk_sb.sb,
		   c->disk_sb.page_order);
	kvpfree(c, sizeof(*c));
	module_put(THIS_MODULE);
}

static void bch2_fs_release(struct kobject *kobj)
{
	struct bch_fs *c = container_of(kobj, struct bch_fs, kobj);

	bch2_fs_free(c);
}

void bch2_fs_stop(struct bch_fs *c)
{
	struct bch_dev *ca;
	unsigned i;

	bch_verbose(c, "shutting down");

	for_each_member_device(ca, c, i)
		if (ca->kobj.state_in_sysfs &&
		    ca->disk_sb.bdev)
			sysfs_remove_link(bdev_kobj(ca->disk_sb.bdev), "bcachefs");

	if (c->kobj.state_in_sysfs)
		kobject_del(&c->kobj);

	bch2_fs_debug_exit(c);
	bch2_fs_chardev_exit(c);

	kobject_put(&c->time_stats);
	kobject_put(&c->opts_dir);
	kobject_put(&c->internal);

	mutex_lock(&bch_fs_list_lock);
	list_del(&c->list);
	mutex_unlock(&bch_fs_list_lock);

	closure_sync(&c->cl);
	closure_debug_destroy(&c->cl);

	mutex_lock(&c->state_lock);
	bch2_fs_read_only(c);
	mutex_unlock(&c->state_lock);

	/* btree prefetch might have kicked off reads in the background: */
	bch2_btree_flush_all_reads(c);

	for_each_member_device(ca, c, i)
		cancel_work_sync(&ca->io_error_work);

	cancel_work_sync(&c->btree_write_error_work);
	cancel_delayed_work_sync(&c->pd_controllers_update);
	cancel_work_sync(&c->read_only_work);

	for (i = 0; i < c->sb.nr_devices; i++)
		if (c->devs[i])
			bch2_dev_free(rcu_dereference_protected(c->devs[i], 1));

	bch_verbose(c, "shutdown complete");

	kobject_put(&c->kobj);
}

static const char *bch2_fs_online(struct bch_fs *c)
{
	struct bch_dev *ca;
	const char *err = NULL;
	unsigned i;
	int ret;

	lockdep_assert_held(&bch_fs_list_lock);

	if (!list_empty(&c->list))
		return NULL;

	if (__bch2_uuid_to_fs(c->sb.uuid))
		return "filesystem UUID already open";

	ret = bch2_fs_chardev_init(c);
	if (ret)
		return "error creating character device";

	bch2_fs_debug_init(c);

	if (kobject_add(&c->kobj, NULL, "%pU", c->sb.user_uuid.b) ||
	    kobject_add(&c->internal, &c->kobj, "internal") ||
	    kobject_add(&c->opts_dir, &c->kobj, "options") ||
	    kobject_add(&c->time_stats, &c->kobj, "time_stats") ||
	    bch2_opts_create_sysfs_files(&c->opts_dir))
		return "error creating sysfs objects";

	mutex_lock(&c->state_lock);

	err = "error creating sysfs objects";
	__for_each_member_device(ca, c, i, NULL)
		if (bch2_dev_sysfs_online(c, ca))
			goto err;

	list_add(&c->list, &bch_fs_list);
	err = NULL;
err:
	mutex_unlock(&c->state_lock);
	return err;
}

static struct bch_fs *bch2_fs_alloc(struct bch_sb *sb, struct bch_opts opts)
{
	struct bch_sb_field_members *mi;
	struct bch_fs *c;
	unsigned i, iter_size;
	const char *err;

	pr_verbose_init(opts, "");

	c = kvpmalloc(sizeof(struct bch_fs), GFP_KERNEL|__GFP_ZERO);
	if (!c)
		goto out;

	__module_get(THIS_MODULE);

	c->minor		= -1;
	c->disk_sb.fs_sb	= true;

	mutex_init(&c->state_lock);
	mutex_init(&c->sb_lock);
	mutex_init(&c->replicas_gc_lock);
	mutex_init(&c->btree_root_lock);
	INIT_WORK(&c->read_only_work, bch2_fs_read_only_work);

	init_rwsem(&c->gc_lock);

	for (i = 0; i < BCH_TIME_STAT_NR; i++)
		bch2_time_stats_init(&c->times[i]);

	bch2_fs_allocator_background_init(c);
	bch2_fs_allocator_foreground_init(c);
	bch2_fs_rebalance_init(c);
	bch2_fs_quota_init(c);

	INIT_LIST_HEAD(&c->list);

	INIT_LIST_HEAD(&c->btree_interior_update_list);
	mutex_init(&c->btree_reserve_cache_lock);
	mutex_init(&c->btree_interior_update_lock);

	mutex_init(&c->usage_scratch_lock);

	mutex_init(&c->bio_bounce_pages_lock);

	bio_list_init(&c->btree_write_error_list);
	spin_lock_init(&c->btree_write_error_lock);
	INIT_WORK(&c->btree_write_error_work, bch2_btree_write_error_work);

	INIT_LIST_HEAD(&c->fsck_errors);
	mutex_init(&c->fsck_error_lock);

	INIT_LIST_HEAD(&c->ec_new_stripe_list);
	mutex_init(&c->ec_new_stripe_lock);
	mutex_init(&c->ec_stripe_create_lock);
	spin_lock_init(&c->ec_stripes_heap_lock);

	seqcount_init(&c->gc_pos_lock);

	c->copy_gc_enabled		= 1;
	c->rebalance.enabled		= 1;
	c->promote_whole_extents	= true;

	c->journal.write_time	= &c->times[BCH_TIME_journal_write];
	c->journal.delay_time	= &c->times[BCH_TIME_journal_delay];
	c->journal.blocked_time	= &c->times[BCH_TIME_blocked_journal];
	c->journal.flush_seq_time = &c->times[BCH_TIME_journal_flush_seq];

	bch2_fs_btree_cache_init_early(&c->btree_cache);

	if (percpu_init_rwsem(&c->mark_lock))
		goto err;

	mutex_lock(&c->sb_lock);

	if (bch2_sb_to_fs(c, sb)) {
		mutex_unlock(&c->sb_lock);
		goto err;
	}

	mutex_unlock(&c->sb_lock);

	scnprintf(c->name, sizeof(c->name), "%pU", &c->sb.user_uuid);

	c->opts = bch2_opts_default;
	bch2_opts_apply(&c->opts, bch2_opts_from_sb(sb));
	bch2_opts_apply(&c->opts, opts);

	c->block_bits		= ilog2(c->opts.block_size);
	c->btree_foreground_merge_threshold = BTREE_FOREGROUND_MERGE_THRESHOLD(c);

	c->opts.nochanges	|= c->opts.noreplay;
	c->opts.read_only	|= c->opts.nochanges;

	if (bch2_fs_init_fault("fs_alloc"))
		goto err;

	iter_size = sizeof(struct btree_node_iter_large) +
		(btree_blocks(c) + 1) * 2 *
		sizeof(struct btree_node_iter_set);

	if (!(c->wq = alloc_workqueue("bcachefs",
				WQ_FREEZABLE|WQ_MEM_RECLAIM|WQ_HIGHPRI, 1)) ||
	    !(c->copygc_wq = alloc_workqueue("bcache_copygc",
				WQ_FREEZABLE|WQ_MEM_RECLAIM|WQ_HIGHPRI, 1)) ||
	    !(c->journal_reclaim_wq = alloc_workqueue("bcache_journal",
				WQ_FREEZABLE|WQ_MEM_RECLAIM|WQ_HIGHPRI, 1)) ||
	    percpu_ref_init(&c->writes, bch2_writes_disabled, 0, GFP_KERNEL) ||
	    mempool_init_kmalloc_pool(&c->btree_reserve_pool, 1,
				      sizeof(struct btree_reserve)) ||
	    mempool_init_kmalloc_pool(&c->btree_interior_update_pool, 1,
				      sizeof(struct btree_update)) ||
	    mempool_init_kmalloc_pool(&c->fill_iter, 1, iter_size) ||
	    bioset_init(&c->btree_bio, 1,
			max(offsetof(struct btree_read_bio, bio),
			    offsetof(struct btree_write_bio, wbio.bio)),
			BIOSET_NEED_BVECS) ||
	    !(c->pcpu = alloc_percpu(struct bch_fs_pcpu)) ||
	    mempool_init_kvpmalloc_pool(&c->btree_bounce_pool, 1,
					btree_bytes(c)) ||
	    mempool_init_kmalloc_pool(&c->btree_iters_pool, 1,
			sizeof(struct btree_iter) * BTREE_ITER_MAX +
			sizeof(struct btree_insert_entry) *
			(BTREE_ITER_MAX + 4)) ||
	    bch2_io_clock_init(&c->io_clock[READ]) ||
	    bch2_io_clock_init(&c->io_clock[WRITE]) ||
	    bch2_fs_journal_init(&c->journal) ||
	    bch2_fs_replicas_init(c) ||
	    bch2_fs_btree_cache_init(c) ||
	    bch2_fs_io_init(c) ||
	    bch2_fs_encryption_init(c) ||
	    bch2_fs_compress_init(c) ||
	    bch2_fs_ec_init(c) ||
	    bch2_fs_fsio_init(c))
		goto err;

	mi = bch2_sb_get_members(c->disk_sb.sb);
	for (i = 0; i < c->sb.nr_devices; i++)
		if (bch2_dev_exists(c->disk_sb.sb, mi, i) &&
		    bch2_dev_alloc(c, i))
			goto err;

	/*
	 * Now that all allocations have succeeded, init various refcounty
	 * things that let us shutdown:
	 */
	closure_init(&c->cl, NULL);

	c->kobj.kset = bcachefs_kset;
	kobject_init(&c->kobj, &bch2_fs_ktype);
	kobject_init(&c->internal, &bch2_fs_internal_ktype);
	kobject_init(&c->opts_dir, &bch2_fs_opts_dir_ktype);
	kobject_init(&c->time_stats, &bch2_fs_time_stats_ktype);

	mutex_lock(&bch_fs_list_lock);
	err = bch2_fs_online(c);
	mutex_unlock(&bch_fs_list_lock);
	if (err) {
		bch_err(c, "bch2_fs_online() error: %s", err);
		goto err;
	}
out:
	pr_verbose_init(opts, "ret %i", c ? 0 : -ENOMEM);
	return c;
err:
	bch2_fs_free(c);
	c = NULL;
	goto out;
}

const char *bch2_fs_start(struct bch_fs *c)
{
	const char *err = "cannot allocate memory";
	struct bch_sb_field_members *mi;
	struct bch_dev *ca;
	time64_t now = ktime_get_real_seconds();
	unsigned i;
	int ret = -EINVAL;

	mutex_lock(&c->state_lock);

	BUG_ON(c->state != BCH_FS_STARTING);

	mutex_lock(&c->sb_lock);

	for_each_online_member(ca, c, i)
		bch2_sb_from_fs(c, ca);

	mi = bch2_sb_get_members(c->disk_sb.sb);
	for_each_online_member(ca, c, i)
		mi->members[ca->dev_idx].last_mount = cpu_to_le64(now);

	mutex_unlock(&c->sb_lock);

	for_each_rw_member(ca, c, i)
		bch2_dev_allocator_add(c, ca);
	bch2_recalc_capacity(c);

	ret = BCH_SB_INITIALIZED(c->disk_sb.sb)
		? bch2_fs_recovery(c)
		: bch2_fs_initialize(c);
	if (ret)
		goto err;

	ret = bch2_opts_check_may_set(c);
	if (ret)
		goto err;

	err = "dynamic fault";
	if (bch2_fs_init_fault("fs_start"))
		goto err;

	if (c->opts.read_only) {
		bch2_fs_read_only(c);
	} else {
		err = bch2_fs_read_write(c);
		if (err)
			goto err;
	}

	set_bit(BCH_FS_STARTED, &c->flags);

	err = NULL;
out:
	mutex_unlock(&c->state_lock);
	return err;
err:
	switch (ret) {
	case BCH_FSCK_ERRORS_NOT_FIXED:
		bch_err(c, "filesystem contains errors: please report this to the developers");
		pr_cont("mount with -o fix_errors to repair\n");
		err = "fsck error";
		break;
	case BCH_FSCK_REPAIR_UNIMPLEMENTED:
		bch_err(c, "filesystem contains errors: please report this to the developers");
		pr_cont("repair unimplemented: inform the developers so that it can be added\n");
		err = "fsck error";
		break;
	case BCH_FSCK_REPAIR_IMPOSSIBLE:
		bch_err(c, "filesystem contains errors, but repair impossible");
		err = "fsck error";
		break;
	case BCH_FSCK_UNKNOWN_VERSION:
		err = "unknown metadata version";;
		break;
	case -ENOMEM:
		err = "cannot allocate memory";
		break;
	case -EIO:
		err = "IO error";
		break;
	}

	BUG_ON(!err);
	set_bit(BCH_FS_ERROR, &c->flags);
	goto out;
}

static const char *bch2_dev_may_add(struct bch_sb *sb, struct bch_fs *c)
{
	struct bch_sb_field_members *sb_mi;

	sb_mi = bch2_sb_get_members(sb);
	if (!sb_mi)
		return "Invalid superblock: member info area missing";

	if (le16_to_cpu(sb->block_size) != c->opts.block_size)
		return "mismatched block size";

	if (le16_to_cpu(sb_mi->members[sb->dev_idx].bucket_size) <
	    BCH_SB_BTREE_NODE_SIZE(c->disk_sb.sb))
		return "new cache bucket size is too small";

	return NULL;
}

static const char *bch2_dev_in_fs(struct bch_sb *fs, struct bch_sb *sb)
{
	struct bch_sb *newest =
		le64_to_cpu(fs->seq) > le64_to_cpu(sb->seq) ? fs : sb;
	struct bch_sb_field_members *mi = bch2_sb_get_members(newest);

	if (!uuid_equal(&fs->uuid, &sb->uuid))
		return "device not a member of filesystem";

	if (!bch2_dev_exists(newest, mi, sb->dev_idx))
		return "device has been removed";

	if (fs->block_size != sb->block_size)
		return "mismatched block size";

	return NULL;
}

/* Device startup/shutdown: */

static void bch2_dev_release(struct kobject *kobj)
{
	struct bch_dev *ca = container_of(kobj, struct bch_dev, kobj);

	kfree(ca);
}

static void bch2_dev_free(struct bch_dev *ca)
{
	cancel_work_sync(&ca->io_error_work);

	if (ca->kobj.state_in_sysfs &&
	    ca->disk_sb.bdev)
		sysfs_remove_link(bdev_kobj(ca->disk_sb.bdev), "bcachefs");

	if (ca->kobj.state_in_sysfs)
		kobject_del(&ca->kobj);

	bch2_free_super(&ca->disk_sb);
	bch2_dev_journal_exit(ca);

	free_percpu(ca->io_done);
	bioset_exit(&ca->replica_set);
	bch2_dev_buckets_free(ca);

	bch2_time_stats_exit(&ca->io_latency[WRITE]);
	bch2_time_stats_exit(&ca->io_latency[READ]);

	percpu_ref_exit(&ca->io_ref);
	percpu_ref_exit(&ca->ref);
	kobject_put(&ca->kobj);
}

static void __bch2_dev_offline(struct bch_fs *c, struct bch_dev *ca)
{

	lockdep_assert_held(&c->state_lock);

	if (percpu_ref_is_zero(&ca->io_ref))
		return;

	__bch2_dev_read_only(c, ca);

	reinit_completion(&ca->io_ref_completion);
	percpu_ref_kill(&ca->io_ref);
	wait_for_completion(&ca->io_ref_completion);

	if (ca->kobj.state_in_sysfs) {
		sysfs_remove_link(bdev_kobj(ca->disk_sb.bdev), "bcachefs");
		sysfs_remove_link(&ca->kobj, "block");
	}

	bch2_free_super(&ca->disk_sb);
	bch2_dev_journal_exit(ca);
}

static void bch2_dev_ref_complete(struct percpu_ref *ref)
{
	struct bch_dev *ca = container_of(ref, struct bch_dev, ref);

	complete(&ca->ref_completion);
}

static void bch2_dev_io_ref_complete(struct percpu_ref *ref)
{
	struct bch_dev *ca = container_of(ref, struct bch_dev, io_ref);

	complete(&ca->io_ref_completion);
}

static int bch2_dev_sysfs_online(struct bch_fs *c, struct bch_dev *ca)
{
	int ret;

	if (!c->kobj.state_in_sysfs)
		return 0;

	if (!ca->kobj.state_in_sysfs) {
		ret = kobject_add(&ca->kobj, &c->kobj,
				  "dev-%u", ca->dev_idx);
		if (ret)
			return ret;
	}

	if (ca->disk_sb.bdev) {
		struct kobject *block = bdev_kobj(ca->disk_sb.bdev);

		ret = sysfs_create_link(block, &ca->kobj, "bcachefs");
		if (ret)
			return ret;

		ret = sysfs_create_link(&ca->kobj, block, "block");
		if (ret)
			return ret;
	}

	return 0;
}

static struct bch_dev *__bch2_dev_alloc(struct bch_fs *c,
					struct bch_member *member)
{
	struct bch_dev *ca;

	ca = kzalloc(sizeof(*ca), GFP_KERNEL);
	if (!ca)
		return NULL;

	kobject_init(&ca->kobj, &bch2_dev_ktype);
	init_completion(&ca->ref_completion);
	init_completion(&ca->io_ref_completion);

	init_rwsem(&ca->bucket_lock);

	writepoint_init(&ca->copygc_write_point, BCH_DATA_USER);

	spin_lock_init(&ca->freelist_lock);
	bch2_dev_copygc_init(ca);

	INIT_WORK(&ca->io_error_work, bch2_io_error_work);

	bch2_time_stats_init(&ca->io_latency[READ]);
	bch2_time_stats_init(&ca->io_latency[WRITE]);

	ca->mi = bch2_mi_to_cpu(member);
	ca->uuid = member->uuid;

	if (opt_defined(c->opts, discard))
		ca->mi.discard = opt_get(c->opts, discard);

	if (percpu_ref_init(&ca->ref, bch2_dev_ref_complete,
			    0, GFP_KERNEL) ||
	    percpu_ref_init(&ca->io_ref, bch2_dev_io_ref_complete,
			    PERCPU_REF_INIT_DEAD, GFP_KERNEL) ||
	    bch2_dev_buckets_alloc(c, ca) ||
	    bioset_init(&ca->replica_set, 4,
			offsetof(struct bch_write_bio, bio), 0) ||
	    !(ca->io_done	= alloc_percpu(*ca->io_done)))
		goto err;

	return ca;
err:
	bch2_dev_free(ca);
	return NULL;
}

static void bch2_dev_attach(struct bch_fs *c, struct bch_dev *ca,
			    unsigned dev_idx)
{
	ca->dev_idx = dev_idx;
	__set_bit(ca->dev_idx, ca->self.d);
	scnprintf(ca->name, sizeof(ca->name), "dev-%u", dev_idx);

	ca->fs = c;
	rcu_assign_pointer(c->devs[ca->dev_idx], ca);

	if (bch2_dev_sysfs_online(c, ca))
		pr_warn("error creating sysfs objects");
}

static int bch2_dev_alloc(struct bch_fs *c, unsigned dev_idx)
{
	struct bch_member *member =
		bch2_sb_get_members(c->disk_sb.sb)->members + dev_idx;
	struct bch_dev *ca = NULL;
	int ret = 0;

	pr_verbose_init(c->opts, "");

	if (bch2_fs_init_fault("dev_alloc"))
		goto err;

	ca = __bch2_dev_alloc(c, member);
	if (!ca)
		goto err;

	bch2_dev_attach(c, ca, dev_idx);
out:
	pr_verbose_init(c->opts, "ret %i", ret);
	return ret;
err:
	if (ca)
		bch2_dev_free(ca);
	ret = -ENOMEM;
	goto out;
}

static int __bch2_dev_attach_bdev(struct bch_dev *ca, struct bch_sb_handle *sb)
{
	unsigned ret;

	if (bch2_dev_is_online(ca)) {
		bch_err(ca, "already have device online in slot %u",
			sb->sb->dev_idx);
		return -EINVAL;
	}

	if (get_capacity(sb->bdev->bd_disk) <
	    ca->mi.bucket_size * ca->mi.nbuckets) {
		bch_err(ca, "cannot online: device too small");
		return -EINVAL;
	}

	BUG_ON(!percpu_ref_is_zero(&ca->io_ref));

	if (get_capacity(sb->bdev->bd_disk) <
	    ca->mi.bucket_size * ca->mi.nbuckets) {
		bch_err(ca, "device too small");
		return -EINVAL;
	}

	ret = bch2_dev_journal_init(ca, sb->sb);
	if (ret)
		return ret;

	/* Commit: */
	ca->disk_sb = *sb;
	memset(sb, 0, sizeof(*sb));

	percpu_ref_reinit(&ca->io_ref);

	return 0;
}

static int bch2_dev_attach_bdev(struct bch_fs *c, struct bch_sb_handle *sb)
{
	struct bch_dev *ca;
	int ret;

	lockdep_assert_held(&c->state_lock);

	if (le64_to_cpu(sb->sb->seq) >
	    le64_to_cpu(c->disk_sb.sb->seq))
		bch2_sb_to_fs(c, sb->sb);

	BUG_ON(sb->sb->dev_idx >= c->sb.nr_devices ||
	       !c->devs[sb->sb->dev_idx]);

	ca = bch_dev_locked(c, sb->sb->dev_idx);

	ret = __bch2_dev_attach_bdev(ca, sb);
	if (ret)
		return ret;

	if (test_bit(BCH_FS_ALLOC_READ_DONE, &c->flags) &&
	    !percpu_u64_get(&ca->usage[0]->buckets[BCH_DATA_SB])) {
		mutex_lock(&c->sb_lock);
		bch2_mark_dev_superblock(ca->fs, ca, 0);
		mutex_unlock(&c->sb_lock);
	}

	bch2_dev_sysfs_online(c, ca);

	if (c->sb.nr_devices == 1)
		snprintf(c->name, sizeof(c->name), "%pg", ca->disk_sb.bdev);
	snprintf(ca->name, sizeof(ca->name), "%pg", ca->disk_sb.bdev);

	rebalance_wakeup(c);
	return 0;
}

/* Device management: */

/*
 * Note: this function is also used by the error paths - when a particular
 * device sees an error, we call it to determine whether we can just set the
 * device RO, or - if this function returns false - we'll set the whole
 * filesystem RO:
 *
 * XXX: maybe we should be more explicit about whether we're changing state
 * because we got an error or what have you?
 */
bool bch2_dev_state_allowed(struct bch_fs *c, struct bch_dev *ca,
			    enum bch_member_state new_state, int flags)
{
	struct bch_devs_mask new_online_devs;
	struct replicas_status s;
	struct bch_dev *ca2;
	int i, nr_rw = 0, required;

	lockdep_assert_held(&c->state_lock);

	switch (new_state) {
	case BCH_MEMBER_STATE_RW:
		return true;
	case BCH_MEMBER_STATE_RO:
		if (ca->mi.state != BCH_MEMBER_STATE_RW)
			return true;

		/* do we have enough devices to write to?  */
		for_each_member_device(ca2, c, i)
			if (ca2 != ca)
				nr_rw += ca2->mi.state == BCH_MEMBER_STATE_RW;

		required = max(!(flags & BCH_FORCE_IF_METADATA_DEGRADED)
			       ? c->opts.metadata_replicas
			       : c->opts.metadata_replicas_required,
			       !(flags & BCH_FORCE_IF_DATA_DEGRADED)
			       ? c->opts.data_replicas
			       : c->opts.data_replicas_required);

		return nr_rw >= required;
	case BCH_MEMBER_STATE_FAILED:
	case BCH_MEMBER_STATE_SPARE:
		if (ca->mi.state != BCH_MEMBER_STATE_RW &&
		    ca->mi.state != BCH_MEMBER_STATE_RO)
			return true;

		/* do we have enough devices to read from?  */
		new_online_devs = bch2_online_devs(c);
		__clear_bit(ca->dev_idx, new_online_devs.d);

		s = __bch2_replicas_status(c, new_online_devs);

		return bch2_have_enough_devs(s, flags);
	default:
		BUG();
	}
}

static bool bch2_fs_may_start(struct bch_fs *c)
{
	struct replicas_status s;
	struct bch_sb_field_members *mi;
	struct bch_dev *ca;
	unsigned i, flags = c->opts.degraded
		? BCH_FORCE_IF_DEGRADED
		: 0;

	if (!c->opts.degraded) {
		mutex_lock(&c->sb_lock);
		mi = bch2_sb_get_members(c->disk_sb.sb);

		for (i = 0; i < c->disk_sb.sb->nr_devices; i++) {
			if (!bch2_dev_exists(c->disk_sb.sb, mi, i))
				continue;

			ca = bch_dev_locked(c, i);

			if (!bch2_dev_is_online(ca) &&
			    (ca->mi.state == BCH_MEMBER_STATE_RW ||
			     ca->mi.state == BCH_MEMBER_STATE_RO)) {
				mutex_unlock(&c->sb_lock);
				return false;
			}
		}
		mutex_unlock(&c->sb_lock);
	}

	s = bch2_replicas_status(c);

	return bch2_have_enough_devs(s, flags);
}

static void __bch2_dev_read_only(struct bch_fs *c, struct bch_dev *ca)
{
	bch2_copygc_stop(ca);

	/*
	 * The allocator thread itself allocates btree nodes, so stop it first:
	 */
	bch2_dev_allocator_stop(ca);
	bch2_dev_allocator_remove(c, ca);
	bch2_dev_journal_stop(&c->journal, ca);
}

static const char *__bch2_dev_read_write(struct bch_fs *c, struct bch_dev *ca)
{
	lockdep_assert_held(&c->state_lock);

	BUG_ON(ca->mi.state != BCH_MEMBER_STATE_RW);

	bch2_dev_allocator_add(c, ca);
	bch2_recalc_capacity(c);

	if (bch2_dev_allocator_start(ca))
		return "error starting allocator thread";

	if (bch2_copygc_start(c, ca))
		return "error starting copygc thread";

	return NULL;
}

int __bch2_dev_set_state(struct bch_fs *c, struct bch_dev *ca,
			 enum bch_member_state new_state, int flags)
{
	struct bch_sb_field_members *mi;
	int ret = 0;

	if (ca->mi.state == new_state)
		return 0;

	if (!bch2_dev_state_allowed(c, ca, new_state, flags))
		return -EINVAL;

	if (new_state != BCH_MEMBER_STATE_RW)
		__bch2_dev_read_only(c, ca);

	bch_notice(ca, "%s", bch2_dev_state[new_state]);

	mutex_lock(&c->sb_lock);
	mi = bch2_sb_get_members(c->disk_sb.sb);
	SET_BCH_MEMBER_STATE(&mi->members[ca->dev_idx], new_state);
	bch2_write_super(c);
	mutex_unlock(&c->sb_lock);

	if (new_state == BCH_MEMBER_STATE_RW &&
	    __bch2_dev_read_write(c, ca))
		ret = -ENOMEM;

	rebalance_wakeup(c);

	return ret;
}

int bch2_dev_set_state(struct bch_fs *c, struct bch_dev *ca,
		       enum bch_member_state new_state, int flags)
{
	int ret;

	mutex_lock(&c->state_lock);
	ret = __bch2_dev_set_state(c, ca, new_state, flags);
	mutex_unlock(&c->state_lock);

	return ret;
}

/* Device add/removal: */

int bch2_dev_remove(struct bch_fs *c, struct bch_dev *ca, int flags)
{
	struct bch_sb_field_members *mi;
	unsigned dev_idx = ca->dev_idx, data;
	int ret = -EINVAL;

	mutex_lock(&c->state_lock);

	percpu_ref_put(&ca->ref); /* XXX */

	if (!bch2_dev_state_allowed(c, ca, BCH_MEMBER_STATE_FAILED, flags)) {
		bch_err(ca, "Cannot remove without losing data");
		goto err;
	}

	__bch2_dev_read_only(c, ca);

	/*
	 * XXX: verify that dev_idx is really not in use anymore, anywhere
	 *
	 * flag_data_bad() does not check btree pointers
	 */
	ret = bch2_dev_data_drop(c, ca->dev_idx, flags);
	if (ret) {
		bch_err(ca, "Remove failed: error %i dropping data", ret);
		goto err;
	}

	ret = bch2_journal_flush_device_pins(&c->journal, ca->dev_idx);
	if (ret) {
		bch_err(ca, "Remove failed: error %i flushing journal", ret);
		goto err;
	}

	data = bch2_dev_has_data(c, ca);
	if (data) {
		char data_has_str[100];

		bch2_flags_to_text(&PBUF(data_has_str),
				   bch2_data_types, data);
		bch_err(ca, "Remove failed, still has data (%s)", data_has_str);
		ret = -EBUSY;
		goto err;
	}

	ret = bch2_btree_delete_range(c, BTREE_ID_ALLOC,
				      POS(ca->dev_idx, 0),
				      POS(ca->dev_idx + 1, 0),
				      NULL);
	if (ret) {
		bch_err(ca, "Remove failed, error deleting alloc info");
		goto err;
	}

	/*
	 * must flush all existing journal entries, they might have
	 * (overwritten) keys that point to the device we're removing:
	 */
	bch2_journal_flush_all_pins(&c->journal);
	ret = bch2_journal_error(&c->journal);
	if (ret) {
		bch_err(ca, "Remove failed, journal error");
		goto err;
	}

	__bch2_dev_offline(c, ca);

	mutex_lock(&c->sb_lock);
	rcu_assign_pointer(c->devs[ca->dev_idx], NULL);
	mutex_unlock(&c->sb_lock);

	percpu_ref_kill(&ca->ref);
	wait_for_completion(&ca->ref_completion);

	bch2_dev_free(ca);

	/*
	 * Free this device's slot in the bch_member array - all pointers to
	 * this device must be gone:
	 */
	mutex_lock(&c->sb_lock);
	mi = bch2_sb_get_members(c->disk_sb.sb);
	memset(&mi->members[dev_idx].uuid, 0, sizeof(mi->members[dev_idx].uuid));

	bch2_write_super(c);

	mutex_unlock(&c->sb_lock);
	mutex_unlock(&c->state_lock);
	return 0;
err:
	if (ca->mi.state == BCH_MEMBER_STATE_RW &&
	    !percpu_ref_is_zero(&ca->io_ref))
		__bch2_dev_read_write(c, ca);
	mutex_unlock(&c->state_lock);
	return ret;
}

static void dev_usage_clear(struct bch_dev *ca)
{
	struct bucket_array *buckets;
	int cpu;

	for_each_possible_cpu(cpu) {
		struct bch_dev_usage *p =
			per_cpu_ptr(ca->usage[0], cpu);
		memset(p, 0, sizeof(*p));
	}

	down_read(&ca->bucket_lock);
	buckets = bucket_array(ca);

	memset(buckets->b, 0, sizeof(buckets->b[0]) * buckets->nbuckets);
	up_read(&ca->bucket_lock);
}

/* Add new device to running filesystem: */
int bch2_dev_add(struct bch_fs *c, const char *path)
{
	struct bch_opts opts = bch2_opts_empty();
	struct bch_sb_handle sb;
	const char *err;
	struct bch_dev *ca = NULL;
	struct bch_sb_field_members *mi;
	struct bch_member dev_mi;
	unsigned dev_idx, nr_devices, u64s;
	int ret;

	ret = bch2_read_super(path, &opts, &sb);
	if (ret)
		return ret;

	err = bch2_sb_validate(&sb);
	if (err)
		return -EINVAL;

	dev_mi = bch2_sb_get_members(sb.sb)->members[sb.sb->dev_idx];

	err = bch2_dev_may_add(sb.sb, c);
	if (err)
		return -EINVAL;

	ca = __bch2_dev_alloc(c, &dev_mi);
	if (!ca) {
		bch2_free_super(&sb);
		return -ENOMEM;
	}

	ret = __bch2_dev_attach_bdev(ca, &sb);
	if (ret) {
		bch2_dev_free(ca);
		return ret;
	}

	/*
	 * We want to allocate journal on the new device before adding the new
	 * device to the filesystem because allocating after we attach requires
	 * spinning up the allocator thread, and the allocator thread requires
	 * doing btree writes, which if the existing devices are RO isn't going
	 * to work
	 *
	 * So we have to mark where the superblocks are, but marking allocated
	 * data normally updates the filesystem usage too, so we have to mark,
	 * allocate the journal, reset all the marks, then remark after we
	 * attach...
	 */
	bch2_mark_dev_superblock(ca->fs, ca, 0);

	err = "journal alloc failed";
	ret = bch2_dev_journal_alloc(ca);
	if (ret)
		goto err;

	dev_usage_clear(ca);

	mutex_lock(&c->state_lock);
	mutex_lock(&c->sb_lock);

	err = "insufficient space in new superblock";
	ret = bch2_sb_from_fs(c, ca);
	if (ret)
		goto err_unlock;

	mi = bch2_sb_get_members(ca->disk_sb.sb);

	if (!bch2_sb_resize_members(&ca->disk_sb,
				le32_to_cpu(mi->field.u64s) +
				sizeof(dev_mi) / sizeof(u64))) {
		ret = -ENOSPC;
		goto err_unlock;
	}

	if (dynamic_fault("bcachefs:add:no_slot"))
		goto no_slot;

	mi = bch2_sb_get_members(c->disk_sb.sb);
	for (dev_idx = 0; dev_idx < BCH_SB_MEMBERS_MAX; dev_idx++)
		if (!bch2_dev_exists(c->disk_sb.sb, mi, dev_idx))
			goto have_slot;
no_slot:
	err = "no slots available in superblock";
	ret = -ENOSPC;
	goto err_unlock;

have_slot:
	nr_devices = max_t(unsigned, dev_idx + 1, c->sb.nr_devices);
	u64s = (sizeof(struct bch_sb_field_members) +
		sizeof(struct bch_member) * nr_devices) / sizeof(u64);

	err = "no space in superblock for member info";
	ret = -ENOSPC;

	mi = bch2_sb_resize_members(&c->disk_sb, u64s);
	if (!mi)
		goto err_unlock;

	/* success: */

	mi->members[dev_idx] = dev_mi;
	mi->members[dev_idx].last_mount = cpu_to_le64(ktime_get_real_seconds());
	c->disk_sb.sb->nr_devices	= nr_devices;

	ca->disk_sb.sb->dev_idx	= dev_idx;
	bch2_dev_attach(c, ca, dev_idx);

	bch2_mark_dev_superblock(c, ca, 0);

	bch2_write_super(c);
	mutex_unlock(&c->sb_lock);

	if (ca->mi.state == BCH_MEMBER_STATE_RW) {
		err = __bch2_dev_read_write(c, ca);
		if (err)
			goto err_late;
	}

	mutex_unlock(&c->state_lock);
	return 0;

err_unlock:
	mutex_unlock(&c->sb_lock);
	mutex_unlock(&c->state_lock);
err:
	if (ca)
		bch2_dev_free(ca);
	bch2_free_super(&sb);
	bch_err(c, "Unable to add device: %s", err);
	return ret;
err_late:
	bch_err(c, "Error going rw after adding device: %s", err);
	return -EINVAL;
}

/* Hot add existing device to running filesystem: */
int bch2_dev_online(struct bch_fs *c, const char *path)
{
	struct bch_opts opts = bch2_opts_empty();
	struct bch_sb_handle sb = { NULL };
	struct bch_sb_field_members *mi;
	struct bch_dev *ca;
	unsigned dev_idx;
	const char *err;
	int ret;

	mutex_lock(&c->state_lock);

	ret = bch2_read_super(path, &opts, &sb);
	if (ret) {
		mutex_unlock(&c->state_lock);
		return ret;
	}

	dev_idx = sb.sb->dev_idx;

	err = bch2_dev_in_fs(c->disk_sb.sb, sb.sb);
	if (err)
		goto err;

	if (bch2_dev_attach_bdev(c, &sb)) {
		err = "bch2_dev_attach_bdev() error";
		goto err;
	}

	ca = bch_dev_locked(c, dev_idx);
	if (ca->mi.state == BCH_MEMBER_STATE_RW) {
		err = __bch2_dev_read_write(c, ca);
		if (err)
			goto err;
	}

	mutex_lock(&c->sb_lock);
	mi = bch2_sb_get_members(c->disk_sb.sb);

	mi->members[ca->dev_idx].last_mount =
		cpu_to_le64(ktime_get_real_seconds());

	bch2_write_super(c);
	mutex_unlock(&c->sb_lock);

	mutex_unlock(&c->state_lock);
	return 0;
err:
	mutex_unlock(&c->state_lock);
	bch2_free_super(&sb);
	bch_err(c, "error bringing %s online: %s", path, err);
	return -EINVAL;
}

int bch2_dev_offline(struct bch_fs *c, struct bch_dev *ca, int flags)
{
	mutex_lock(&c->state_lock);

	if (!bch2_dev_is_online(ca)) {
		bch_err(ca, "Already offline");
		mutex_unlock(&c->state_lock);
		return 0;
	}

	if (!bch2_dev_state_allowed(c, ca, BCH_MEMBER_STATE_FAILED, flags)) {
		bch_err(ca, "Cannot offline required disk");
		mutex_unlock(&c->state_lock);
		return -EINVAL;
	}

	__bch2_dev_offline(c, ca);

	mutex_unlock(&c->state_lock);
	return 0;
}

int bch2_dev_resize(struct bch_fs *c, struct bch_dev *ca, u64 nbuckets)
{
	struct bch_member *mi;
	int ret = 0;

	mutex_lock(&c->state_lock);

	if (nbuckets < ca->mi.nbuckets) {
		bch_err(ca, "Cannot shrink yet");
		ret = -EINVAL;
		goto err;
	}

	if (bch2_dev_is_online(ca) &&
	    get_capacity(ca->disk_sb.bdev->bd_disk) <
	    ca->mi.bucket_size * nbuckets) {
		bch_err(ca, "New size larger than device");
		ret = -EINVAL;
		goto err;
	}

	ret = bch2_dev_buckets_resize(c, ca, nbuckets);
	if (ret) {
		bch_err(ca, "Resize error: %i", ret);
		goto err;
	}

	mutex_lock(&c->sb_lock);
	mi = &bch2_sb_get_members(c->disk_sb.sb)->members[ca->dev_idx];
	mi->nbuckets = cpu_to_le64(nbuckets);

	bch2_write_super(c);
	mutex_unlock(&c->sb_lock);

	bch2_recalc_capacity(c);
err:
	mutex_unlock(&c->state_lock);
	return ret;
}

/* return with ref on ca->ref: */
struct bch_dev *bch2_dev_lookup(struct bch_fs *c, const char *path)
{

	struct bch_dev *ca;
	dev_t dev;
	unsigned i;
	int ret;

	ret = lookup_bdev(path, &dev);
	if (ret)
		return ERR_PTR(ret);

	for_each_member_device(ca, c, i)
		if (ca->disk_sb.bdev->bd_dev == dev)
			goto found;

	ca = ERR_PTR(-ENOENT);
found:
	return ca;
}

/* Filesystem open: */

struct bch_fs *bch2_fs_open(char * const *devices, unsigned nr_devices,
			    struct bch_opts opts)
{
	struct bch_sb_handle *sb = NULL;
	struct bch_fs *c = NULL;
	unsigned i, best_sb = 0;
	const char *err;
	int ret = -ENOMEM;

	pr_verbose_init(opts, "");

	if (!nr_devices) {
		c = ERR_PTR(-EINVAL);
		goto out2;
	}

	if (!try_module_get(THIS_MODULE)) {
		c = ERR_PTR(-ENODEV);
		goto out2;
	}

	sb = kcalloc(nr_devices, sizeof(*sb), GFP_KERNEL);
	if (!sb)
		goto err;

	for (i = 0; i < nr_devices; i++) {
		ret = bch2_read_super(devices[i], &opts, &sb[i]);
		if (ret)
			goto err;

		err = bch2_sb_validate(&sb[i]);
		if (err)
			goto err_print;
	}

	for (i = 1; i < nr_devices; i++)
		if (le64_to_cpu(sb[i].sb->seq) >
		    le64_to_cpu(sb[best_sb].sb->seq))
			best_sb = i;

	for (i = 0; i < nr_devices; i++) {
		err = bch2_dev_in_fs(sb[best_sb].sb, sb[i].sb);
		if (err)
			goto err_print;
	}

	ret = -ENOMEM;
	c = bch2_fs_alloc(sb[best_sb].sb, opts);
	if (!c)
		goto err;

	err = "bch2_dev_online() error";
	mutex_lock(&c->state_lock);
	for (i = 0; i < nr_devices; i++)
		if (bch2_dev_attach_bdev(c, &sb[i])) {
			mutex_unlock(&c->state_lock);
			goto err_print;
		}
	mutex_unlock(&c->state_lock);

	err = "insufficient devices";
	if (!bch2_fs_may_start(c))
		goto err_print;

	if (!c->opts.nostart) {
		err = bch2_fs_start(c);
		if (err)
			goto err_print;
	}
out:
	kfree(sb);
	module_put(THIS_MODULE);
out2:
	pr_verbose_init(opts, "ret %i", PTR_ERR_OR_ZERO(c));
	return c;
err_print:
	pr_err("bch_fs_open err opening %s: %s",
	       devices[0], err);
	ret = -EINVAL;
err:
	if (c)
		bch2_fs_stop(c);
	for (i = 0; i < nr_devices; i++)
		bch2_free_super(&sb[i]);
	c = ERR_PTR(ret);
	goto out;
}

static const char *__bch2_fs_open_incremental(struct bch_sb_handle *sb,
					      struct bch_opts opts)
{
	const char *err;
	struct bch_fs *c;
	bool allocated_fs = false;

	err = bch2_sb_validate(sb);
	if (err)
		return err;

	mutex_lock(&bch_fs_list_lock);
	c = __bch2_uuid_to_fs(sb->sb->uuid);
	if (c) {
		closure_get(&c->cl);

		err = bch2_dev_in_fs(c->disk_sb.sb, sb->sb);
		if (err)
			goto err;
	} else {
		c = bch2_fs_alloc(sb->sb, opts);
		err = "cannot allocate memory";
		if (!c)
			goto err;

		allocated_fs = true;
	}

	err = "bch2_dev_online() error";

	mutex_lock(&c->sb_lock);
	if (bch2_dev_attach_bdev(c, sb)) {
		mutex_unlock(&c->sb_lock);
		goto err;
	}
	mutex_unlock(&c->sb_lock);

	if (!c->opts.nostart && bch2_fs_may_start(c)) {
		err = bch2_fs_start(c);
		if (err)
			goto err;
	}

	closure_put(&c->cl);
	mutex_unlock(&bch_fs_list_lock);

	return NULL;
err:
	mutex_unlock(&bch_fs_list_lock);

	if (allocated_fs)
		bch2_fs_stop(c);
	else if (c)
		closure_put(&c->cl);

	return err;
}

const char *bch2_fs_open_incremental(const char *path)
{
	struct bch_sb_handle sb;
	struct bch_opts opts = bch2_opts_empty();
	const char *err;

	if (bch2_read_super(path, &opts, &sb))
		return "error reading superblock";

	err = __bch2_fs_open_incremental(&sb, opts);
	bch2_free_super(&sb);

	return err;
}

/* Global interfaces/init */

static void bcachefs_exit(void)
{
	bch2_debug_exit();
	bch2_vfs_exit();
	bch2_chardev_exit();
	if (bcachefs_kset)
		kset_unregister(bcachefs_kset);
}

static int __init bcachefs_init(void)
{
	bch2_bkey_pack_test();
	bch2_inode_pack_test();

	if (!(bcachefs_kset = kset_create_and_add("bcachefs", NULL, fs_kobj)) ||
	    bch2_chardev_init() ||
	    bch2_vfs_init() ||
	    bch2_debug_init())
		goto err;

	return 0;
err:
	bcachefs_exit();
	return -ENOMEM;
}

#define BCH_DEBUG_PARAM(name, description)			\
	bool bch2_##name;					\
	module_param_named(name, bch2_##name, bool, 0644);	\
	MODULE_PARM_DESC(name, description);
BCH_DEBUG_PARAMS()
#undef BCH_DEBUG_PARAM

unsigned bch2_metadata_version = bcachefs_metadata_version_current;
module_param_named(version, bch2_metadata_version, uint, 0400);

module_exit(bcachefs_exit);
module_init(bcachefs_init);
