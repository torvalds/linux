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
#include "async_objs.h"
#include "backpointers.h"
#include "bkey_sort.h"
#include "btree_cache.h"
#include "btree_gc.h"
#include "btree_journal_iter.h"
#include "btree_key_cache.h"
#include "btree_node_scan.h"
#include "btree_update_interior.h"
#include "btree_io.h"
#include "btree_write_buffer.h"
#include "buckets_waiting_for_journal.h"
#include "chardev.h"
#include "checksum.h"
#include "clock.h"
#include "compress.h"
#include "debug.h"
#include "disk_accounting.h"
#include "disk_groups.h"
#include "ec.h"
#include "enumerated_ref.h"
#include "errcode.h"
#include "error.h"
#include "fs.h"
#include "fs-io.h"
#include "fs-io-buffered.h"
#include "fs-io-direct.h"
#include "fsck.h"
#include "inode.h"
#include "io_read.h"
#include "io_write.h"
#include "journal.h"
#include "journal_reclaim.h"
#include "journal_seq_blacklist.h"
#include "move.h"
#include "migrate.h"
#include "movinggc.h"
#include "nocow_locking.h"
#include "quota.h"
#include "rebalance.h"
#include "recovery.h"
#include "recovery_passes.h"
#include "replicas.h"
#include "sb-clean.h"
#include "sb-counters.h"
#include "sb-errors.h"
#include "sb-members.h"
#include "snapshot.h"
#include "subvolume.h"
#include "super.h"
#include "super-io.h"
#include "sysfs.h"
#include "thread_with_file.h"
#include "trace.h"

#include <linux/backing-dev.h>
#include <linux/blkdev.h>
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/idr.h>
#include <linux/module.h>
#include <linux/percpu.h>
#include <linux/random.h>
#include <linux/sysfs.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kent Overstreet <kent.overstreet@gmail.com>");
MODULE_DESCRIPTION("bcachefs filesystem");

typedef DARRAY(struct bch_sb_handle) bch_sb_handles;

#define x(n)		#n,
const char * const bch2_fs_flag_strs[] = {
	BCH_FS_FLAGS()
	NULL
};

const char * const bch2_write_refs[] = {
	BCH_WRITE_REFS()
	NULL
};

const char * const bch2_dev_read_refs[] = {
	BCH_DEV_READ_REFS()
	NULL
};

const char * const bch2_dev_write_refs[] = {
	BCH_DEV_WRITE_REFS()
	NULL
};
#undef x

static void __bch2_print_str(struct bch_fs *c, const char *prefix,
			     const char *str)
{
#ifdef __KERNEL__
	struct stdio_redirect *stdio = bch2_fs_stdio_redirect(c);

	if (unlikely(stdio)) {
		bch2_stdio_redirect_printf(stdio, true, "%s", str);
		return;
	}
#endif
	bch2_print_string_as_lines(KERN_ERR, str);
}

void bch2_print_str(struct bch_fs *c, const char *prefix, const char *str)
{
	__bch2_print_str(c, prefix, str);
}

__printf(2, 0)
static void bch2_print_maybe_redirect(struct stdio_redirect *stdio, const char *fmt, va_list args)
{
#ifdef __KERNEL__
	if (unlikely(stdio)) {
		if (fmt[0] == KERN_SOH[0])
			fmt += 2;

		bch2_stdio_redirect_vprintf(stdio, true, fmt, args);
		return;
	}
#endif
	vprintk(fmt, args);
}

void bch2_print_opts(struct bch_opts *opts, const char *fmt, ...)
{
	struct stdio_redirect *stdio = (void *)(unsigned long)opts->stdio;

	va_list args;
	va_start(args, fmt);
	bch2_print_maybe_redirect(stdio, fmt, args);
	va_end(args);
}

void __bch2_print(struct bch_fs *c, const char *fmt, ...)
{
	struct stdio_redirect *stdio = bch2_fs_stdio_redirect(c);

	va_list args;
	va_start(args, fmt);
	bch2_print_maybe_redirect(stdio, fmt, args);
	va_end(args);
}

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
static void bch2_fs_counters_release(struct kobject *k)
{
}

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
KTYPE(bch2_fs_counters);
KTYPE(bch2_fs_internal);
KTYPE(bch2_fs_opts_dir);
KTYPE(bch2_fs_time_stats);
KTYPE(bch2_dev);

static struct kset *bcachefs_kset;
static LIST_HEAD(bch_fs_list);
static DEFINE_MUTEX(bch_fs_list_lock);

DECLARE_WAIT_QUEUE_HEAD(bch2_read_only_wait);

static void bch2_dev_unlink(struct bch_dev *);
static void bch2_dev_free(struct bch_dev *);
static int bch2_dev_alloc(struct bch_fs *, unsigned);
static int bch2_dev_sysfs_online(struct bch_fs *, struct bch_dev *);
static void bch2_dev_io_ref_stop(struct bch_dev *, int);
static void __bch2_dev_read_only(struct bch_fs *, struct bch_dev *);
static int bch2_fs_init_rw(struct bch_fs *);

struct bch_fs *bch2_dev_to_fs(dev_t dev)
{
	guard(mutex)(&bch_fs_list_lock);
	guard(rcu)();

	struct bch_fs *c;
	list_for_each_entry(c, &bch_fs_list, list)
		for_each_member_device_rcu(c, ca, NULL)
			if (ca->disk_sb.bdev && ca->disk_sb.bdev->bd_dev == dev) {
				closure_get(&c->cl);
				return c;
			}
	return NULL;
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
	unsigned clean_passes = 0;
	u64 seq = 0;

	bch2_fs_ec_stop(c);
	bch2_open_buckets_stop(c, NULL, true);
	bch2_rebalance_stop(c);
	bch2_copygc_stop(c);
	bch2_fs_ec_flush(c);

	bch_verbose(c, "flushing journal and stopping allocators, journal seq %llu",
		    journal_cur_seq(&c->journal));

	do {
		clean_passes++;

		if (bch2_btree_interior_updates_flush(c) ||
		    bch2_btree_write_buffer_flush_going_ro(c) ||
		    bch2_journal_flush_all_pins(&c->journal) ||
		    bch2_btree_flush_all_writes(c) ||
		    seq != atomic64_read(&c->journal.seq)) {
			seq = atomic64_read(&c->journal.seq);
			clean_passes = 0;
		}
	} while (clean_passes < 2);

	bch_verbose(c, "flushing journal and stopping allocators complete, journal seq %llu",
		    journal_cur_seq(&c->journal));

	if (test_bit(JOURNAL_replay_done, &c->journal.flags) &&
	    !test_bit(BCH_FS_emergency_ro, &c->flags))
		set_bit(BCH_FS_clean_shutdown, &c->flags);

	bch2_fs_journal_stop(&c->journal);

	bch_info(c, "%sclean shutdown complete, journal seq %llu",
		 test_bit(BCH_FS_clean_shutdown, &c->flags) ? "" : "un",
		 c->journal.seq_ondisk);

	/*
	 * After stopping journal:
	 */
	for_each_member_device(c, ca) {
		bch2_dev_io_ref_stop(ca, WRITE);
		bch2_dev_allocator_remove(c, ca);
	}
}

static void bch2_writes_disabled(struct enumerated_ref *writes)
{
	struct bch_fs *c = container_of(writes, struct bch_fs, writes);

	set_bit(BCH_FS_write_disable_complete, &c->flags);
	wake_up(&bch2_read_only_wait);
}

void bch2_fs_read_only(struct bch_fs *c)
{
	if (!test_bit(BCH_FS_rw, &c->flags)) {
		bch2_journal_reclaim_stop(&c->journal);
		return;
	}

	BUG_ON(test_bit(BCH_FS_write_disable_complete, &c->flags));

	bch_verbose(c, "going read-only");

	/*
	 * Block new foreground-end write operations from starting - any new
	 * writes will return -EROFS:
	 */
	set_bit(BCH_FS_going_ro, &c->flags);
	enumerated_ref_stop_async(&c->writes);

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
	wait_event(bch2_read_only_wait,
		   test_bit(BCH_FS_write_disable_complete, &c->flags) ||
		   test_bit(BCH_FS_emergency_ro, &c->flags));

	bool writes_disabled = test_bit(BCH_FS_write_disable_complete, &c->flags);
	if (writes_disabled)
		bch_verbose(c, "finished waiting for writes to stop");

	__bch2_fs_read_only(c);

	wait_event(bch2_read_only_wait,
		   test_bit(BCH_FS_write_disable_complete, &c->flags));

	if (!writes_disabled)
		bch_verbose(c, "finished waiting for writes to stop");

	clear_bit(BCH_FS_write_disable_complete, &c->flags);
	clear_bit(BCH_FS_going_ro, &c->flags);
	clear_bit(BCH_FS_rw, &c->flags);

	if (!bch2_journal_error(&c->journal) &&
	    !test_bit(BCH_FS_error, &c->flags) &&
	    !test_bit(BCH_FS_emergency_ro, &c->flags) &&
	    test_bit(BCH_FS_started, &c->flags) &&
	    test_bit(BCH_FS_clean_shutdown, &c->flags) &&
	    c->recovery.pass_done >= BCH_RECOVERY_PASS_journal_replay) {
		BUG_ON(c->journal.last_empty_seq != journal_cur_seq(&c->journal));
		BUG_ON(atomic_long_read(&c->btree_cache.nr_dirty));
		BUG_ON(atomic_long_read(&c->btree_key_cache.nr_dirty));
		BUG_ON(c->btree_write_buffer.inc.keys.nr);
		BUG_ON(c->btree_write_buffer.flushing.keys.nr);
		bch2_verify_accounting_clean(c);

		bch_verbose(c, "marking filesystem clean");
		bch2_fs_mark_clean(c);
	} else {
		/* Make sure error counts/counters are persisted */
		mutex_lock(&c->sb_lock);
		bch2_write_super(c);
		mutex_unlock(&c->sb_lock);

		bch_verbose(c, "done going read-only, filesystem not clean");
	}
}

static void bch2_fs_read_only_work(struct work_struct *work)
{
	struct bch_fs *c =
		container_of(work, struct bch_fs, read_only_work);

	down_write(&c->state_lock);
	bch2_fs_read_only(c);
	up_write(&c->state_lock);
}

static void bch2_fs_read_only_async(struct bch_fs *c)
{
	queue_work(system_long_wq, &c->read_only_work);
}

bool bch2_fs_emergency_read_only(struct bch_fs *c)
{
	bool ret = !test_and_set_bit(BCH_FS_emergency_ro, &c->flags);

	bch2_journal_halt(&c->journal);
	bch2_fs_read_only_async(c);

	wake_up(&bch2_read_only_wait);
	return ret;
}

static bool __bch2_fs_emergency_read_only2(struct bch_fs *c, struct printbuf *out,
					   bool locked)
{
	bool ret = !test_and_set_bit(BCH_FS_emergency_ro, &c->flags);

	if (!locked)
		bch2_journal_halt(&c->journal);
	else
		bch2_journal_halt_locked(&c->journal);
	bch2_fs_read_only_async(c);
	wake_up(&bch2_read_only_wait);

	if (ret)
		prt_printf(out, "emergency read only at seq %llu\n",
			   journal_cur_seq(&c->journal));

	return ret;
}

bool bch2_fs_emergency_read_only2(struct bch_fs *c, struct printbuf *out)
{
	return __bch2_fs_emergency_read_only2(c, out, false);
}

bool bch2_fs_emergency_read_only_locked(struct bch_fs *c)
{
	bool ret = !test_and_set_bit(BCH_FS_emergency_ro, &c->flags);

	bch2_journal_halt_locked(&c->journal);
	bch2_fs_read_only_async(c);

	wake_up(&bch2_read_only_wait);
	return ret;
}

static int __bch2_fs_read_write(struct bch_fs *c, bool early)
{
	int ret;

	BUG_ON(!test_bit(BCH_FS_may_go_rw, &c->flags));

	if (WARN_ON(c->sb.features & BIT_ULL(BCH_FEATURE_no_alloc_info)))
		return bch_err_throw(c, erofs_no_alloc_info);

	if (test_bit(BCH_FS_initial_gc_unfixed, &c->flags)) {
		bch_err(c, "cannot go rw, unfixed btree errors");
		return bch_err_throw(c, erofs_unfixed_errors);
	}

	if (c->sb.features & BIT_ULL(BCH_FEATURE_small_image)) {
		bch_err(c, "cannot go rw, filesystem is an unresized image file");
		return bch_err_throw(c, erofs_filesystem_full);
	}

	if (test_bit(BCH_FS_rw, &c->flags))
		return 0;

	bch_info(c, "going read-write");

	ret = bch2_fs_init_rw(c);
	if (ret)
		goto err;

	ret = bch2_sb_members_v2_init(c);
	if (ret)
		goto err;

	clear_bit(BCH_FS_clean_shutdown, &c->flags);

	scoped_guard(rcu)
		for_each_online_member_rcu(c, ca)
			if (ca->mi.state == BCH_MEMBER_STATE_rw) {
				bch2_dev_allocator_add(c, ca);
				enumerated_ref_start(&ca->io_ref[WRITE]);
			}

	bch2_recalc_capacity(c);

	/*
	 * First journal write must be a flush write: after a clean shutdown we
	 * don't read the journal, so the first journal write may end up
	 * overwriting whatever was there previously, and there must always be
	 * at least one non-flush write in the journal or recovery will fail:
	 */
	spin_lock(&c->journal.lock);
	set_bit(JOURNAL_need_flush_write, &c->journal.flags);
	set_bit(JOURNAL_running, &c->journal.flags);
	bch2_journal_space_available(&c->journal);
	spin_unlock(&c->journal.lock);

	ret = bch2_fs_mark_dirty(c);
	if (ret)
		goto err;

	ret = bch2_journal_reclaim_start(&c->journal);
	if (ret)
		goto err;

	set_bit(BCH_FS_rw, &c->flags);
	set_bit(BCH_FS_was_rw, &c->flags);

	enumerated_ref_start(&c->writes);

	ret = bch2_copygc_start(c);
	if (ret) {
		bch_err_msg(c, ret, "error starting copygc thread");
		goto err;
	}

	ret = bch2_rebalance_start(c);
	if (ret) {
		bch_err_msg(c, ret, "error starting rebalance thread");
		goto err;
	}

	bch2_do_discards(c);
	bch2_do_invalidates(c);
	bch2_do_stripe_deletes(c);
	bch2_do_pending_node_rewrites(c);
	return 0;
err:
	if (test_bit(BCH_FS_rw, &c->flags))
		bch2_fs_read_only(c);
	else
		__bch2_fs_read_only(c);
	return ret;
}

int bch2_fs_read_write(struct bch_fs *c)
{
	if (c->opts.recovery_pass_last &&
	    c->opts.recovery_pass_last < BCH_RECOVERY_PASS_journal_replay)
		return bch_err_throw(c, erofs_norecovery);

	if (c->opts.nochanges)
		return bch_err_throw(c, erofs_nochanges);

	if (c->sb.features & BIT_ULL(BCH_FEATURE_no_alloc_info))
		return bch_err_throw(c, erofs_no_alloc_info);

	return __bch2_fs_read_write(c, false);
}

int bch2_fs_read_write_early(struct bch_fs *c)
{
	down_write(&c->state_lock);
	int ret = __bch2_fs_read_write(c, true);
	up_write(&c->state_lock);

	return ret;
}

/* Filesystem startup/shutdown: */

static void __bch2_fs_free(struct bch_fs *c)
{
	for (unsigned i = 0; i < BCH_TIME_STAT_NR; i++)
		bch2_time_stats_exit(&c->times[i]);

#ifdef CONFIG_UNICODE
	utf8_unload(c->cf_encoding);
#endif

	bch2_find_btree_nodes_exit(&c->found_btree_nodes);
	bch2_free_pending_node_rewrites(c);
	bch2_free_fsck_errs(c);
	bch2_fs_vfs_exit(c);
	bch2_fs_snapshots_exit(c);
	bch2_fs_sb_errors_exit(c);
	bch2_fs_replicas_exit(c);
	bch2_fs_rebalance_exit(c);
	bch2_fs_quota_exit(c);
	bch2_fs_nocow_locking_exit(c);
	bch2_fs_journal_exit(&c->journal);
	bch2_fs_fs_io_direct_exit(c);
	bch2_fs_fs_io_buffered_exit(c);
	bch2_fs_fsio_exit(c);
	bch2_fs_io_write_exit(c);
	bch2_fs_io_read_exit(c);
	bch2_fs_encryption_exit(c);
	bch2_fs_ec_exit(c);
	bch2_fs_counters_exit(c);
	bch2_fs_compress_exit(c);
	bch2_io_clock_exit(&c->io_clock[WRITE]);
	bch2_io_clock_exit(&c->io_clock[READ]);
	bch2_fs_buckets_waiting_for_journal_exit(c);
	bch2_fs_btree_write_buffer_exit(c);
	bch2_fs_btree_key_cache_exit(&c->btree_key_cache);
	bch2_fs_btree_iter_exit(c);
	bch2_fs_btree_interior_update_exit(c);
	bch2_fs_btree_cache_exit(c);
	bch2_fs_accounting_exit(c);
	bch2_fs_async_obj_exit(c);
	bch2_journal_keys_put_initial(c);
	bch2_find_btree_nodes_exit(&c->found_btree_nodes);

	BUG_ON(atomic_read(&c->journal_keys.ref));
	percpu_free_rwsem(&c->mark_lock);
	if (c->online_reserved) {
		u64 v = percpu_u64_get(c->online_reserved);
		WARN(v, "online_reserved not 0 at shutdown: %lli", v);
		free_percpu(c->online_reserved);
	}

	darray_exit(&c->incompat_versions_requested);
	darray_exit(&c->btree_roots_extra);
	free_percpu(c->pcpu);
	free_percpu(c->usage);
	mempool_exit(&c->large_bkey_pool);
	mempool_exit(&c->btree_bounce_pool);
	bioset_exit(&c->btree_bio);
	mempool_exit(&c->fill_iter);
	enumerated_ref_exit(&c->writes);
	kfree(rcu_dereference_protected(c->disk_groups, 1));
	kfree(c->journal_seq_blacklist_table);

	if (c->write_ref_wq)
		destroy_workqueue(c->write_ref_wq);
	if (c->btree_write_submit_wq)
		destroy_workqueue(c->btree_write_submit_wq);
	if (c->btree_read_complete_wq)
		destroy_workqueue(c->btree_read_complete_wq);
	if (c->copygc_wq)
		destroy_workqueue(c->copygc_wq);
	if (c->btree_write_complete_wq)
		destroy_workqueue(c->btree_write_complete_wq);
	if (c->btree_update_wq)
		destroy_workqueue(c->btree_update_wq);

	bch2_free_super(&c->disk_sb);
	kvfree(c);
	module_put(THIS_MODULE);
}

static void bch2_fs_release(struct kobject *kobj)
{
	struct bch_fs *c = container_of(kobj, struct bch_fs, kobj);

	__bch2_fs_free(c);
}

void __bch2_fs_stop(struct bch_fs *c)
{
	bch_verbose(c, "shutting down");

	set_bit(BCH_FS_stopping, &c->flags);

	down_write(&c->state_lock);
	bch2_fs_read_only(c);
	up_write(&c->state_lock);

	for (unsigned i = 0; i < c->sb.nr_devices; i++) {
		struct bch_dev *ca = rcu_dereference_protected(c->devs[i], true);
		if (ca)
			bch2_dev_io_ref_stop(ca, READ);
	}

	for_each_member_device(c, ca)
		bch2_dev_unlink(ca);

	if (c->kobj.state_in_sysfs)
		kobject_del(&c->kobj);

	bch2_fs_debug_exit(c);
	bch2_fs_chardev_exit(c);

	bch2_ro_ref_put(c);
	wait_event(c->ro_ref_wait, !refcount_read(&c->ro_ref));

	kobject_put(&c->counters_kobj);
	kobject_put(&c->time_stats);
	kobject_put(&c->opts_dir);
	kobject_put(&c->internal);

	/* btree prefetch might have kicked off reads in the background: */
	bch2_btree_flush_all_reads(c);

	for_each_member_device(c, ca)
		cancel_work_sync(&ca->io_error_work);

	cancel_work_sync(&c->read_only_work);
}

void bch2_fs_free(struct bch_fs *c)
{
	mutex_lock(&bch_fs_list_lock);
	list_del(&c->list);
	mutex_unlock(&bch_fs_list_lock);

	closure_sync(&c->cl);
	closure_debug_destroy(&c->cl);

	for (unsigned i = 0; i < c->sb.nr_devices; i++) {
		struct bch_dev *ca = rcu_dereference_protected(c->devs[i], true);

		if (ca) {
			EBUG_ON(atomic_long_read(&ca->ref) != 1);
			bch2_dev_io_ref_stop(ca, READ);
			bch2_free_super(&ca->disk_sb);
			bch2_dev_free(ca);
		}
	}

	bch_verbose(c, "shutdown complete");

	kobject_put(&c->kobj);
}

void bch2_fs_stop(struct bch_fs *c)
{
	__bch2_fs_stop(c);
	bch2_fs_free(c);
}

static int bch2_fs_online(struct bch_fs *c)
{
	int ret = 0;

	lockdep_assert_held(&bch_fs_list_lock);

	if (c->sb.multi_device &&
	    __bch2_uuid_to_fs(c->sb.uuid)) {
		bch_err(c, "filesystem UUID already open");
		return bch_err_throw(c, filesystem_uuid_already_open);
	}

	ret = bch2_fs_chardev_init(c);
	if (ret) {
		bch_err(c, "error creating character device");
		return ret;
	}

	bch2_fs_debug_init(c);

	ret = (c->sb.multi_device
	       ? kobject_add(&c->kobj, NULL, "%pU", c->sb.user_uuid.b)
	       : kobject_add(&c->kobj, NULL, "%s", c->name)) ?:
	    kobject_add(&c->internal, &c->kobj, "internal") ?:
	    kobject_add(&c->opts_dir, &c->kobj, "options") ?:
#ifndef CONFIG_BCACHEFS_NO_LATENCY_ACCT
	    kobject_add(&c->time_stats, &c->kobj, "time_stats") ?:
#endif
	    kobject_add(&c->counters_kobj, &c->kobj, "counters") ?:
	    bch2_opts_create_sysfs_files(&c->opts_dir, OPT_FS);
	if (ret) {
		bch_err(c, "error creating sysfs objects");
		return ret;
	}

	down_write(&c->state_lock);

	for_each_member_device(c, ca) {
		ret = bch2_dev_sysfs_online(c, ca);
		if (ret) {
			bch_err(c, "error creating sysfs objects");
			bch2_dev_put(ca);
			goto err;
		}
	}

	BUG_ON(!list_empty(&c->list));
	list_add(&c->list, &bch_fs_list);
err:
	up_write(&c->state_lock);
	return ret;
}

static int bch2_fs_init_rw(struct bch_fs *c)
{
	if (test_bit(BCH_FS_rw_init_done, &c->flags))
		return 0;

	if (!(c->btree_update_wq = alloc_workqueue("bcachefs",
				WQ_HIGHPRI|WQ_FREEZABLE|WQ_MEM_RECLAIM|WQ_UNBOUND, 512)) ||
	    !(c->btree_write_complete_wq = alloc_workqueue("bcachefs_btree_write_complete",
				WQ_HIGHPRI|WQ_FREEZABLE|WQ_MEM_RECLAIM, 1)) ||
	    !(c->copygc_wq = alloc_workqueue("bcachefs_copygc",
				WQ_HIGHPRI|WQ_FREEZABLE|WQ_MEM_RECLAIM|WQ_CPU_INTENSIVE, 1)) ||
	    !(c->btree_write_submit_wq = alloc_workqueue("bcachefs_btree_write_sumit",
				WQ_HIGHPRI|WQ_FREEZABLE|WQ_MEM_RECLAIM, 1)) ||
	    !(c->write_ref_wq = alloc_workqueue("bcachefs_write_ref",
				WQ_FREEZABLE, 0)))
		return bch_err_throw(c, ENOMEM_fs_other_alloc);

	int ret = bch2_fs_btree_interior_update_init(c) ?:
		bch2_fs_btree_write_buffer_init(c) ?:
		bch2_fs_fs_io_buffered_init(c) ?:
		bch2_fs_io_write_init(c) ?:
		bch2_fs_journal_init(&c->journal);
	if (ret)
		return ret;

	set_bit(BCH_FS_rw_init_done, &c->flags);
	return 0;
}

static struct bch_fs *bch2_fs_alloc(struct bch_sb *sb, struct bch_opts *opts,
				    bch_sb_handles *sbs)
{
	struct bch_fs *c;
	struct printbuf name = PRINTBUF;
	unsigned i, iter_size;
	int ret = 0;

	c = kvmalloc(sizeof(struct bch_fs), GFP_KERNEL|__GFP_ZERO);
	if (!c) {
		c = ERR_PTR(-BCH_ERR_ENOMEM_fs_alloc);
		goto out;
	}

	c->stdio = (void *)(unsigned long) opts->stdio;

	__module_get(THIS_MODULE);

	closure_init(&c->cl, NULL);

	c->kobj.kset = bcachefs_kset;
	kobject_init(&c->kobj, &bch2_fs_ktype);
	kobject_init(&c->internal, &bch2_fs_internal_ktype);
	kobject_init(&c->opts_dir, &bch2_fs_opts_dir_ktype);
	kobject_init(&c->time_stats, &bch2_fs_time_stats_ktype);
	kobject_init(&c->counters_kobj, &bch2_fs_counters_ktype);

	c->minor		= -1;
	c->disk_sb.fs_sb	= true;

	init_rwsem(&c->state_lock);
	mutex_init(&c->sb_lock);
	mutex_init(&c->replicas_gc_lock);
	mutex_init(&c->btree_root_lock);
	INIT_WORK(&c->read_only_work, bch2_fs_read_only_work);

	refcount_set(&c->ro_ref, 1);
	init_waitqueue_head(&c->ro_ref_wait);

	for (i = 0; i < BCH_TIME_STAT_NR; i++)
		bch2_time_stats_init(&c->times[i]);

	bch2_fs_allocator_background_init(c);
	bch2_fs_allocator_foreground_init(c);
	bch2_fs_btree_cache_init_early(&c->btree_cache);
	bch2_fs_btree_gc_init_early(c);
	bch2_fs_btree_interior_update_init_early(c);
	bch2_fs_btree_iter_init_early(c);
	bch2_fs_btree_key_cache_init_early(&c->btree_key_cache);
	bch2_fs_btree_write_buffer_init_early(c);
	bch2_fs_copygc_init(c);
	bch2_fs_ec_init_early(c);
	bch2_fs_journal_init_early(&c->journal);
	bch2_fs_journal_keys_init(c);
	bch2_fs_move_init(c);
	bch2_fs_nocow_locking_init_early(c);
	bch2_fs_quota_init(c);
	bch2_fs_recovery_passes_init(c);
	bch2_fs_sb_errors_init_early(c);
	bch2_fs_snapshots_init_early(c);
	bch2_fs_subvolumes_init_early(c);

	INIT_LIST_HEAD(&c->list);

	mutex_init(&c->bio_bounce_pages_lock);
	mutex_init(&c->snapshot_table_lock);
	init_rwsem(&c->snapshot_create_lock);

	spin_lock_init(&c->btree_write_error_lock);

	INIT_LIST_HEAD(&c->journal_iters);

	INIT_LIST_HEAD(&c->fsck_error_msgs);
	mutex_init(&c->fsck_error_msgs_lock);

	seqcount_init(&c->usage_lock);

	sema_init(&c->io_in_flight, 128);

	INIT_LIST_HEAD(&c->vfs_inodes_list);
	mutex_init(&c->vfs_inodes_lock);

	c->journal.flush_write_time	= &c->times[BCH_TIME_journal_flush_write];
	c->journal.noflush_write_time	= &c->times[BCH_TIME_journal_noflush_write];
	c->journal.flush_seq_time	= &c->times[BCH_TIME_journal_flush_seq];

	mutex_init(&c->sectors_available_lock);

	ret = percpu_init_rwsem(&c->mark_lock);
	if (ret)
		goto err;

	mutex_lock(&c->sb_lock);
	ret = bch2_sb_to_fs(c, sb);
	mutex_unlock(&c->sb_lock);

	if (ret)
		goto err;

	/* Compat: */
	if (le16_to_cpu(sb->version) <= bcachefs_metadata_version_inode_v2 &&
	    !BCH_SB_JOURNAL_FLUSH_DELAY(sb))
		SET_BCH_SB_JOURNAL_FLUSH_DELAY(sb, 1000);

	if (le16_to_cpu(sb->version) <= bcachefs_metadata_version_inode_v2 &&
	    !BCH_SB_JOURNAL_RECLAIM_DELAY(sb))
		SET_BCH_SB_JOURNAL_RECLAIM_DELAY(sb, 100);

	c->opts = bch2_opts_default;
	ret = bch2_opts_from_sb(&c->opts, sb);
	if (ret)
		goto err;

	bch2_opts_apply(&c->opts, *opts);

	if (!IS_ENABLED(CONFIG_TRANSPARENT_HUGEPAGE) &&
	    c->opts.block_size > PAGE_SIZE) {
		bch_err(c, "cannot mount bs > ps filesystem without CONFIG_TRANSPARENT_HUGEPAGE");
		ret = -EINVAL;
		goto err;
	}

	c->btree_key_cache_btrees |= 1U << BTREE_ID_alloc;
	if (c->opts.inodes_use_key_cache)
		c->btree_key_cache_btrees |= 1U << BTREE_ID_inodes;
	c->btree_key_cache_btrees |= 1U << BTREE_ID_logged_ops;

	c->block_bits		= ilog2(block_sectors(c));
	c->btree_foreground_merge_threshold = BTREE_FOREGROUND_MERGE_THRESHOLD(c);

	if (bch2_fs_init_fault("fs_alloc")) {
		bch_err(c, "fs_alloc fault injected");
		ret = -EFAULT;
		goto err;
	}

	if (c->sb.multi_device)
		pr_uuid(&name, c->sb.user_uuid.b);
	else
		prt_bdevname(&name, sbs->data[0].bdev);

	ret = name.allocation_failure ? -BCH_ERR_ENOMEM_fs_name_alloc : 0;
	if (ret)
		goto err;

	strscpy(c->name, name.buf, sizeof(c->name));
	printbuf_exit(&name);

	iter_size = sizeof(struct sort_iter) +
		(btree_blocks(c) + 1) * 2 *
		sizeof(struct sort_iter_set);

	if (!(c->btree_read_complete_wq = alloc_workqueue("bcachefs_btree_read_complete",
				WQ_HIGHPRI|WQ_FREEZABLE|WQ_MEM_RECLAIM, 512)) ||
	    enumerated_ref_init(&c->writes, BCH_WRITE_REF_NR,
				bch2_writes_disabled) ||
	    mempool_init_kmalloc_pool(&c->fill_iter, 1, iter_size) ||
	    bioset_init(&c->btree_bio, 1,
			max(offsetof(struct btree_read_bio, bio),
			    offsetof(struct btree_write_bio, wbio.bio)),
			BIOSET_NEED_BVECS) ||
	    !(c->pcpu = alloc_percpu(struct bch_fs_pcpu)) ||
	    !(c->usage = alloc_percpu(struct bch_fs_usage_base)) ||
	    !(c->online_reserved = alloc_percpu(u64)) ||
	    mempool_init_kvmalloc_pool(&c->btree_bounce_pool, 1,
				       c->opts.btree_node_size) ||
	    mempool_init_kmalloc_pool(&c->large_bkey_pool, 1, 2048)) {
		ret = bch_err_throw(c, ENOMEM_fs_other_alloc);
		goto err;
	}

	ret =
	    bch2_fs_async_obj_init(c) ?:
	    bch2_fs_btree_cache_init(c) ?:
	    bch2_fs_btree_iter_init(c) ?:
	    bch2_fs_btree_key_cache_init(&c->btree_key_cache) ?:
	    bch2_fs_buckets_waiting_for_journal_init(c) ?:
	    bch2_io_clock_init(&c->io_clock[READ]) ?:
	    bch2_io_clock_init(&c->io_clock[WRITE]) ?:
	    bch2_fs_compress_init(c) ?:
	    bch2_fs_counters_init(c) ?:
	    bch2_fs_ec_init(c) ?:
	    bch2_fs_encryption_init(c) ?:
	    bch2_fs_fsio_init(c) ?:
	    bch2_fs_fs_io_direct_init(c) ?:
	    bch2_fs_io_read_init(c) ?:
	    bch2_fs_rebalance_init(c) ?:
	    bch2_fs_sb_errors_init(c) ?:
	    bch2_fs_vfs_init(c);
	if (ret)
		goto err;

#ifdef CONFIG_UNICODE
	/* Default encoding until we can potentially have more as an option. */
	c->cf_encoding = utf8_load(BCH_FS_DEFAULT_UTF8_ENCODING);
	if (IS_ERR(c->cf_encoding)) {
		printk(KERN_ERR "Cannot load UTF-8 encoding for filesystem. Version: %u.%u.%u",
			unicode_major(BCH_FS_DEFAULT_UTF8_ENCODING),
			unicode_minor(BCH_FS_DEFAULT_UTF8_ENCODING),
			unicode_rev(BCH_FS_DEFAULT_UTF8_ENCODING));
		ret = -EINVAL;
		goto err;
	}
#else
	if (c->sb.features & BIT_ULL(BCH_FEATURE_casefolding)) {
		printk(KERN_ERR "Cannot mount a filesystem with casefolding on a kernel without CONFIG_UNICODE\n");
		ret = -EINVAL;
		goto err;
	}
#endif

	for (i = 0; i < c->sb.nr_devices; i++) {
		if (!bch2_member_exists(c->disk_sb.sb, i))
			continue;
		ret = bch2_dev_alloc(c, i);
		if (ret)
			goto err;
	}

	bch2_journal_entry_res_resize(&c->journal,
			&c->btree_root_journal_res,
			BTREE_ID_NR * (JSET_KEYS_U64s + BKEY_BTREE_PTR_U64s_MAX));
	bch2_journal_entry_res_resize(&c->journal,
			&c->clock_journal_res,
			(sizeof(struct jset_entry_clock) / sizeof(u64)) * 2);

	mutex_lock(&bch_fs_list_lock);
	ret = bch2_fs_online(c);
	mutex_unlock(&bch_fs_list_lock);

	if (ret)
		goto err;
out:
	return c;
err:
	bch2_fs_free(c);
	c = ERR_PTR(ret);
	goto out;
}

noinline_for_stack
static void print_mount_opts(struct bch_fs *c)
{
	enum bch_opt_id i;
	CLASS(printbuf, p)();
	bch2_log_msg_start(c, &p);

	prt_str(&p, "starting version ");
	bch2_version_to_text(&p, c->sb.version);

	bool first = true;
	for (i = 0; i < bch2_opts_nr; i++) {
		const struct bch_option *opt = &bch2_opt_table[i];
		u64 v = bch2_opt_get_by_id(&c->opts, i);

		if (!(opt->flags & OPT_MOUNT))
			continue;

		if (v == bch2_opt_get_by_id(&bch2_opts_default, i))
			continue;

		prt_str(&p, first ? " opts=" : ",");
		first = false;
		bch2_opt_to_text(&p, c, c->disk_sb.sb, opt, v, OPT_SHOW_MOUNT_STYLE);
	}

	if (c->sb.version_incompat_allowed != c->sb.version) {
		prt_printf(&p, "\nallowing incompatible features above ");
		bch2_version_to_text(&p, c->sb.version_incompat_allowed);
	}

	if (c->opts.verbose) {
		prt_printf(&p, "\nfeatures: ");
		prt_bitflags(&p, bch2_sb_features, c->sb.features);
	}

	if (c->sb.multi_device) {
		prt_printf(&p, "\nwith devices");
		for_each_online_member(c, ca, BCH_DEV_READ_REF_bch2_online_devs) {
			prt_char(&p, ' ');
			prt_str(&p, ca->name);
		}
	}

	bch2_print_str(c, KERN_INFO, p.buf);
}

static bool bch2_fs_may_start(struct bch_fs *c)
{
	struct bch_dev *ca;
	unsigned flags = 0;

	switch (c->opts.degraded) {
	case BCH_DEGRADED_very:
		flags |= BCH_FORCE_IF_DEGRADED|BCH_FORCE_IF_LOST;
		break;
	case BCH_DEGRADED_yes:
		flags |= BCH_FORCE_IF_DEGRADED;
		break;
	default:
		mutex_lock(&c->sb_lock);
		for (unsigned i = 0; i < c->disk_sb.sb->nr_devices; i++) {
			if (!bch2_member_exists(c->disk_sb.sb, i))
				continue;

			ca = bch2_dev_locked(c, i);

			if (!bch2_dev_is_online(ca) &&
			    (ca->mi.state == BCH_MEMBER_STATE_rw ||
			     ca->mi.state == BCH_MEMBER_STATE_ro)) {
				mutex_unlock(&c->sb_lock);
				return false;
			}
		}
		mutex_unlock(&c->sb_lock);
		break;
	}

	return bch2_have_enough_devs(c, c->online_devs, flags, true);
}

int bch2_fs_start(struct bch_fs *c)
{
	time64_t now = ktime_get_real_seconds();
	int ret = 0;

	print_mount_opts(c);

#ifdef CONFIG_UNICODE
	bch_info(c, "Using encoding defined by superblock: utf8-%u.%u.%u",
		 unicode_major(BCH_FS_DEFAULT_UTF8_ENCODING),
		 unicode_minor(BCH_FS_DEFAULT_UTF8_ENCODING),
		 unicode_rev(BCH_FS_DEFAULT_UTF8_ENCODING));
#endif

	if (!bch2_fs_may_start(c))
		return bch_err_throw(c, insufficient_devices_to_start);

	down_write(&c->state_lock);
	mutex_lock(&c->sb_lock);

	BUG_ON(test_bit(BCH_FS_started, &c->flags));

	if (!bch2_sb_field_get_minsize(&c->disk_sb, ext,
			sizeof(struct bch_sb_field_ext) / sizeof(u64))) {
		mutex_unlock(&c->sb_lock);
		up_write(&c->state_lock);
		ret = bch_err_throw(c, ENOSPC_sb);
		goto err;
	}

	ret = bch2_sb_members_v2_init(c);
	if (ret) {
		mutex_unlock(&c->sb_lock);
		up_write(&c->state_lock);
		goto err;
	}

	scoped_guard(rcu)
		for_each_online_member_rcu(c, ca)
			bch2_members_v2_get_mut(c->disk_sb.sb, ca->dev_idx)->last_mount =
			cpu_to_le64(now);

	/*
	 * Dno't write superblock yet: recovery might have to downgrade
	 */
	mutex_unlock(&c->sb_lock);

	scoped_guard(rcu)
		for_each_online_member_rcu(c, ca)
			if (ca->mi.state == BCH_MEMBER_STATE_rw)
				bch2_dev_allocator_add(c, ca);
	bch2_recalc_capacity(c);
	up_write(&c->state_lock);

	c->recovery_task = current;
	ret = BCH_SB_INITIALIZED(c->disk_sb.sb)
		? bch2_fs_recovery(c)
		: bch2_fs_initialize(c);
	c->recovery_task = NULL;

	if (ret)
		goto err;

	ret = bch2_opts_hooks_pre_set(c);
	if (ret)
		goto err;

	if (bch2_fs_init_fault("fs_start")) {
		ret = bch_err_throw(c, injected_fs_start);
		goto err;
	}

	set_bit(BCH_FS_started, &c->flags);
	wake_up(&c->ro_ref_wait);

	down_write(&c->state_lock);
	if (c->opts.read_only)
		bch2_fs_read_only(c);
	else if (!test_bit(BCH_FS_rw, &c->flags))
		ret = bch2_fs_read_write(c);
	up_write(&c->state_lock);

err:
	if (ret)
		bch_err_msg(c, ret, "starting filesystem");
	else
		bch_verbose(c, "done starting filesystem");
	return ret;
}

static int bch2_dev_may_add(struct bch_sb *sb, struct bch_fs *c)
{
	struct bch_member m = bch2_sb_member_get(sb, sb->dev_idx);

	if (le16_to_cpu(sb->block_size) != block_sectors(c))
		return bch_err_throw(c, mismatched_block_size);

	if (le16_to_cpu(m.bucket_size) <
	    BCH_SB_BTREE_NODE_SIZE(c->disk_sb.sb))
		return bch_err_throw(c, bucket_size_too_small);

	return 0;
}

static int bch2_dev_in_fs(struct bch_sb_handle *fs,
			  struct bch_sb_handle *sb,
			  struct bch_opts *opts)
{
	if (fs == sb)
		return 0;

	if (!uuid_equal(&fs->sb->uuid, &sb->sb->uuid))
		return -BCH_ERR_device_not_a_member_of_filesystem;

	if (!bch2_member_exists(fs->sb, sb->sb->dev_idx))
		return -BCH_ERR_device_has_been_removed;

	if (fs->sb->block_size != sb->sb->block_size)
		return -BCH_ERR_mismatched_block_size;

	if (le16_to_cpu(fs->sb->version) < bcachefs_metadata_version_member_seq ||
	    le16_to_cpu(sb->sb->version) < bcachefs_metadata_version_member_seq)
		return 0;

	if (fs->sb->seq == sb->sb->seq &&
	    fs->sb->write_time != sb->sb->write_time) {
		struct printbuf buf = PRINTBUF;

		prt_str(&buf, "Split brain detected between ");
		prt_bdevname(&buf, sb->bdev);
		prt_str(&buf, " and ");
		prt_bdevname(&buf, fs->bdev);
		prt_char(&buf, ':');
		prt_newline(&buf);
		prt_printf(&buf, "seq=%llu but write_time different, got", le64_to_cpu(sb->sb->seq));
		prt_newline(&buf);

		prt_bdevname(&buf, fs->bdev);
		prt_char(&buf, ' ');
		bch2_prt_datetime(&buf, le64_to_cpu(fs->sb->write_time));
		prt_newline(&buf);

		prt_bdevname(&buf, sb->bdev);
		prt_char(&buf, ' ');
		bch2_prt_datetime(&buf, le64_to_cpu(sb->sb->write_time));
		prt_newline(&buf);

		if (!opts->no_splitbrain_check)
			prt_printf(&buf, "Not using older sb");

		pr_err("%s", buf.buf);
		printbuf_exit(&buf);

		if (!opts->no_splitbrain_check)
			return -BCH_ERR_device_splitbrain;
	}

	struct bch_member m = bch2_sb_member_get(fs->sb, sb->sb->dev_idx);
	u64 seq_from_fs		= le64_to_cpu(m.seq);
	u64 seq_from_member	= le64_to_cpu(sb->sb->seq);

	if (seq_from_fs && seq_from_fs < seq_from_member) {
		struct printbuf buf = PRINTBUF;

		prt_str(&buf, "Split brain detected between ");
		prt_bdevname(&buf, sb->bdev);
		prt_str(&buf, " and ");
		prt_bdevname(&buf, fs->bdev);
		prt_char(&buf, ':');
		prt_newline(&buf);

		prt_bdevname(&buf, fs->bdev);
		prt_str(&buf, " believes seq of ");
		prt_bdevname(&buf, sb->bdev);
		prt_printf(&buf, " to be %llu, but ", seq_from_fs);
		prt_bdevname(&buf, sb->bdev);
		prt_printf(&buf, " has %llu\n", seq_from_member);

		if (!opts->no_splitbrain_check) {
			prt_str(&buf, "Not using ");
			prt_bdevname(&buf, sb->bdev);
		}

		pr_err("%s", buf.buf);
		printbuf_exit(&buf);

		if (!opts->no_splitbrain_check)
			return -BCH_ERR_device_splitbrain;
	}

	return 0;
}

/* Device startup/shutdown: */

static void bch2_dev_io_ref_stop(struct bch_dev *ca, int rw)
{
	if (rw == READ)
		clear_bit(ca->dev_idx, ca->fs->online_devs.d);

	if (!enumerated_ref_is_zero(&ca->io_ref[rw]))
		enumerated_ref_stop(&ca->io_ref[rw],
				    rw == READ
				    ? bch2_dev_read_refs
				    : bch2_dev_write_refs);
}

static void bch2_dev_release(struct kobject *kobj)
{
	struct bch_dev *ca = container_of(kobj, struct bch_dev, kobj);

	kfree(ca);
}

static void bch2_dev_free(struct bch_dev *ca)
{
	WARN_ON(!enumerated_ref_is_zero(&ca->io_ref[WRITE]));
	WARN_ON(!enumerated_ref_is_zero(&ca->io_ref[READ]));

	cancel_work_sync(&ca->io_error_work);

	bch2_dev_unlink(ca);

	if (ca->kobj.state_in_sysfs)
		kobject_del(&ca->kobj);

	bch2_bucket_bitmap_free(&ca->bucket_backpointer_mismatch);
	bch2_bucket_bitmap_free(&ca->bucket_backpointer_empty);

	bch2_free_super(&ca->disk_sb);
	bch2_dev_allocator_background_exit(ca);
	bch2_dev_journal_exit(ca);

	free_percpu(ca->io_done);
	bch2_dev_buckets_free(ca);
	kfree(ca->sb_read_scratch);

	bch2_time_stats_quantiles_exit(&ca->io_latency[WRITE]);
	bch2_time_stats_quantiles_exit(&ca->io_latency[READ]);

	enumerated_ref_exit(&ca->io_ref[WRITE]);
	enumerated_ref_exit(&ca->io_ref[READ]);
#ifndef CONFIG_BCACHEFS_DEBUG
	percpu_ref_exit(&ca->ref);
#endif
	kobject_put(&ca->kobj);
}

static void __bch2_dev_offline(struct bch_fs *c, struct bch_dev *ca)
{

	lockdep_assert_held(&c->state_lock);

	if (enumerated_ref_is_zero(&ca->io_ref[READ]))
		return;

	__bch2_dev_read_only(c, ca);

	bch2_dev_io_ref_stop(ca, READ);

	bch2_dev_unlink(ca);

	bch2_free_super(&ca->disk_sb);
	bch2_dev_journal_exit(ca);
}

#ifndef CONFIG_BCACHEFS_DEBUG
static void bch2_dev_ref_complete(struct percpu_ref *ref)
{
	struct bch_dev *ca = container_of(ref, struct bch_dev, ref);

	complete(&ca->ref_completion);
}
#endif

static void bch2_dev_unlink(struct bch_dev *ca)
{
	struct kobject *b;

	/*
	 * This is racy w.r.t. the underlying block device being hot-removed,
	 * which removes it from sysfs.
	 *
	 * It'd be lovely if we had a way to handle this race, but the sysfs
	 * code doesn't appear to provide a good method and block/holder.c is
	 * susceptible as well:
	 */
	if (ca->kobj.state_in_sysfs &&
	    ca->disk_sb.bdev &&
	    (b = bdev_kobj(ca->disk_sb.bdev))->state_in_sysfs) {
		sysfs_remove_link(b, "bcachefs");
		sysfs_remove_link(&ca->kobj, "block");
	}
}

static int bch2_dev_sysfs_online(struct bch_fs *c, struct bch_dev *ca)
{
	int ret;

	if (!c->kobj.state_in_sysfs)
		return 0;

	if (!ca->kobj.state_in_sysfs) {
		ret =   kobject_add(&ca->kobj, &c->kobj, "dev-%u", ca->dev_idx) ?:
			bch2_opts_create_sysfs_files(&ca->kobj, OPT_DEVICE);
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
	unsigned i;

	ca = kzalloc(sizeof(*ca), GFP_KERNEL);
	if (!ca)
		return NULL;

	kobject_init(&ca->kobj, &bch2_dev_ktype);
	init_completion(&ca->ref_completion);

	INIT_WORK(&ca->io_error_work, bch2_io_error_work);

	bch2_time_stats_quantiles_init(&ca->io_latency[READ]);
	bch2_time_stats_quantiles_init(&ca->io_latency[WRITE]);

	ca->mi = bch2_mi_to_cpu(member);

	for (i = 0; i < ARRAY_SIZE(member->errors); i++)
		atomic64_set(&ca->errors[i], le64_to_cpu(member->errors[i]));

	ca->uuid = member->uuid;

	ca->nr_btree_reserve = DIV_ROUND_UP(BTREE_NODE_RESERVE,
			     ca->mi.bucket_size / btree_sectors(c));

#ifndef CONFIG_BCACHEFS_DEBUG
	if (percpu_ref_init(&ca->ref, bch2_dev_ref_complete, 0, GFP_KERNEL))
		goto err;
#else
	atomic_long_set(&ca->ref, 1);
#endif

	mutex_init(&ca->bucket_backpointer_mismatch.lock);
	mutex_init(&ca->bucket_backpointer_empty.lock);

	bch2_dev_allocator_background_init(ca);

	if (enumerated_ref_init(&ca->io_ref[READ],  BCH_DEV_READ_REF_NR,  NULL) ||
	    enumerated_ref_init(&ca->io_ref[WRITE], BCH_DEV_WRITE_REF_NR, NULL) ||
	    !(ca->sb_read_scratch = kmalloc(BCH_SB_READ_SCRATCH_BUF_SIZE, GFP_KERNEL)) ||
	    bch2_dev_buckets_alloc(c, ca) ||
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

	if (!ca->name[0])
		scnprintf(ca->name, sizeof(ca->name), "dev-%u", dev_idx);

	ca->fs = c;
	rcu_assign_pointer(c->devs[ca->dev_idx], ca);

	if (bch2_dev_sysfs_online(c, ca))
		pr_warn("error creating sysfs objects");
}

static int bch2_dev_alloc(struct bch_fs *c, unsigned dev_idx)
{
	struct bch_member member = bch2_sb_member_get(c->disk_sb.sb, dev_idx);
	struct bch_dev *ca = NULL;

	if (bch2_fs_init_fault("dev_alloc"))
		goto err;

	ca = __bch2_dev_alloc(c, &member);
	if (!ca)
		goto err;

	ca->fs = c;

	bch2_dev_attach(c, ca, dev_idx);
	return 0;
err:
	return bch_err_throw(c, ENOMEM_dev_alloc);
}

static int __bch2_dev_attach_bdev(struct bch_dev *ca, struct bch_sb_handle *sb)
{
	unsigned ret;

	if (bch2_dev_is_online(ca)) {
		bch_err(ca, "already have device online in slot %u",
			sb->sb->dev_idx);
		return bch_err_throw(ca->fs, device_already_online);
	}

	if (get_capacity(sb->bdev->bd_disk) <
	    ca->mi.bucket_size * ca->mi.nbuckets) {
		bch_err(ca, "cannot online: device too small");
		return bch_err_throw(ca->fs, device_size_too_small);
	}

	BUG_ON(!enumerated_ref_is_zero(&ca->io_ref[READ]));
	BUG_ON(!enumerated_ref_is_zero(&ca->io_ref[WRITE]));

	ret = bch2_dev_journal_init(ca, sb->sb);
	if (ret)
		return ret;

	struct printbuf name = PRINTBUF;
	prt_bdevname(&name, sb->bdev);
	strscpy(ca->name, name.buf, sizeof(ca->name));
	printbuf_exit(&name);

	/* Commit: */
	ca->disk_sb = *sb;
	memset(sb, 0, sizeof(*sb));

	/*
	 * Stash pointer to the filesystem for blk_holder_ops - note that once
	 * attached to a filesystem, we will always close the block device
	 * before tearing down the filesystem object.
	 */
	ca->disk_sb.holder->c = ca->fs;

	ca->dev = ca->disk_sb.bdev->bd_dev;

	enumerated_ref_start(&ca->io_ref[READ]);

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

	BUG_ON(!bch2_dev_exists(c, sb->sb->dev_idx));

	ca = bch2_dev_locked(c, sb->sb->dev_idx);

	ret = __bch2_dev_attach_bdev(ca, sb);
	if (ret)
		return ret;

	set_bit(ca->dev_idx, c->online_devs.d);

	bch2_dev_sysfs_online(c, ca);

	bch2_rebalance_wakeup(c);
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
	int nr_rw = 0, required;

	lockdep_assert_held(&c->state_lock);

	switch (new_state) {
	case BCH_MEMBER_STATE_rw:
		return true;
	case BCH_MEMBER_STATE_ro:
		if (ca->mi.state != BCH_MEMBER_STATE_rw)
			return true;

		/* do we have enough devices to write to?  */
		for_each_member_device(c, ca2)
			if (ca2 != ca)
				nr_rw += ca2->mi.state == BCH_MEMBER_STATE_rw;

		required = max(!(flags & BCH_FORCE_IF_METADATA_DEGRADED)
			       ? c->opts.metadata_replicas
			       : metadata_replicas_required(c),
			       !(flags & BCH_FORCE_IF_DATA_DEGRADED)
			       ? c->opts.data_replicas
			       : data_replicas_required(c));

		return nr_rw >= required;
	case BCH_MEMBER_STATE_failed:
	case BCH_MEMBER_STATE_spare:
		if (ca->mi.state != BCH_MEMBER_STATE_rw &&
		    ca->mi.state != BCH_MEMBER_STATE_ro)
			return true;

		/* do we have enough devices to read from?  */
		new_online_devs = c->online_devs;
		__clear_bit(ca->dev_idx, new_online_devs.d);

		return bch2_have_enough_devs(c, new_online_devs, flags, false);
	default:
		BUG();
	}
}

static void __bch2_dev_read_only(struct bch_fs *c, struct bch_dev *ca)
{
	bch2_dev_io_ref_stop(ca, WRITE);

	/*
	 * The allocator thread itself allocates btree nodes, so stop it first:
	 */
	bch2_dev_allocator_remove(c, ca);
	bch2_recalc_capacity(c);
	bch2_dev_journal_stop(&c->journal, ca);
}

static void __bch2_dev_read_write(struct bch_fs *c, struct bch_dev *ca)
{
	lockdep_assert_held(&c->state_lock);

	BUG_ON(ca->mi.state != BCH_MEMBER_STATE_rw);

	bch2_dev_allocator_add(c, ca);
	bch2_recalc_capacity(c);

	if (enumerated_ref_is_zero(&ca->io_ref[WRITE]))
		enumerated_ref_start(&ca->io_ref[WRITE]);

	bch2_dev_do_discards(ca);
}

int __bch2_dev_set_state(struct bch_fs *c, struct bch_dev *ca,
			 enum bch_member_state new_state, int flags)
{
	struct bch_member *m;
	int ret = 0;

	if (ca->mi.state == new_state)
		return 0;

	if (!bch2_dev_state_allowed(c, ca, new_state, flags))
		return bch_err_throw(c, device_state_not_allowed);

	if (new_state != BCH_MEMBER_STATE_rw)
		__bch2_dev_read_only(c, ca);

	bch_notice(ca, "%s", bch2_member_states[new_state]);

	mutex_lock(&c->sb_lock);
	m = bch2_members_v2_get_mut(c->disk_sb.sb, ca->dev_idx);
	SET_BCH_MEMBER_STATE(m, new_state);
	bch2_write_super(c);
	mutex_unlock(&c->sb_lock);

	if (new_state == BCH_MEMBER_STATE_rw)
		__bch2_dev_read_write(c, ca);

	bch2_rebalance_wakeup(c);

	return ret;
}

int bch2_dev_set_state(struct bch_fs *c, struct bch_dev *ca,
		       enum bch_member_state new_state, int flags)
{
	int ret;

	down_write(&c->state_lock);
	ret = __bch2_dev_set_state(c, ca, new_state, flags);
	up_write(&c->state_lock);

	return ret;
}

/* Device add/removal: */

int bch2_dev_remove(struct bch_fs *c, struct bch_dev *ca, int flags)
{
	struct bch_member *m;
	unsigned dev_idx = ca->dev_idx, data;
	bool fast_device_removal = !bch2_request_incompat_feature(c,
					bcachefs_metadata_version_fast_device_removal);
	int ret;

	down_write(&c->state_lock);

	/*
	 * We consume a reference to ca->ref, regardless of whether we succeed
	 * or fail:
	 */
	bch2_dev_put(ca);

	if (!bch2_dev_state_allowed(c, ca, BCH_MEMBER_STATE_failed, flags)) {
		bch_err(ca, "Cannot remove without losing data");
		ret = bch_err_throw(c, device_state_not_allowed);
		goto err;
	}

	__bch2_dev_read_only(c, ca);

	ret = fast_device_removal
		? bch2_dev_data_drop_by_backpointers(c, ca->dev_idx, flags)
		: (bch2_dev_data_drop(c, ca->dev_idx, flags) ?:
		   bch2_dev_remove_stripes(c, ca->dev_idx, flags));
	if (ret)
		goto err;

	/* Check if device still has data before blowing away alloc info */
	struct bch_dev_usage usage = bch2_dev_usage_read(ca);
	for (unsigned i = 0; i < BCH_DATA_NR; i++)
		if (!data_type_is_empty(i) &&
		    !data_type_is_hidden(i) &&
		    usage.buckets[i]) {
			bch_err(ca, "Remove failed: still has data (%s, %llu buckets)",
				__bch2_data_types[i], usage.buckets[i]);
			ret = -EBUSY;
			goto err;
		}

	ret = bch2_dev_remove_alloc(c, ca);
	bch_err_msg(ca, ret, "bch2_dev_remove_alloc()");
	if (ret)
		goto err;

	/*
	 * We need to flush the entire journal to get rid of keys that reference
	 * the device being removed before removing the superblock entry
	 */
	bch2_journal_flush_all_pins(&c->journal);

	/*
	 * this is really just needed for the bch2_replicas_gc_(start|end)
	 * calls, and could be cleaned up:
	 */
	ret = bch2_journal_flush_device_pins(&c->journal, ca->dev_idx);
	bch_err_msg(ca, ret, "bch2_journal_flush_device_pins()");
	if (ret)
		goto err;

	ret = bch2_journal_flush(&c->journal);
	bch_err_msg(ca, ret, "bch2_journal_flush()");
	if (ret)
		goto err;

	ret = bch2_replicas_gc2(c);
	bch_err_msg(ca, ret, "bch2_replicas_gc2()");
	if (ret)
		goto err;

	data = bch2_dev_has_data(c, ca);
	if (data) {
		struct printbuf data_has = PRINTBUF;

		prt_bitflags(&data_has, __bch2_data_types, data);
		bch_err(ca, "Remove failed, still has data (%s)", data_has.buf);
		printbuf_exit(&data_has);
		ret = -EBUSY;
		goto err;
	}

	__bch2_dev_offline(c, ca);

	mutex_lock(&c->sb_lock);
	rcu_assign_pointer(c->devs[ca->dev_idx], NULL);
	mutex_unlock(&c->sb_lock);

#ifndef CONFIG_BCACHEFS_DEBUG
	percpu_ref_kill(&ca->ref);
#else
	ca->dying = true;
	bch2_dev_put(ca);
#endif
	wait_for_completion(&ca->ref_completion);

	bch2_dev_free(ca);

	/*
	 * Free this device's slot in the bch_member array - all pointers to
	 * this device must be gone:
	 */
	mutex_lock(&c->sb_lock);
	m = bch2_members_v2_get_mut(c->disk_sb.sb, dev_idx);

	if (fast_device_removal)
		m->uuid = BCH_SB_MEMBER_DELETED_UUID;
	else
		memset(&m->uuid, 0, sizeof(m->uuid));

	bch2_write_super(c);

	mutex_unlock(&c->sb_lock);
	up_write(&c->state_lock);
	return 0;
err:
	if (test_bit(BCH_FS_rw, &c->flags) &&
	    ca->mi.state == BCH_MEMBER_STATE_rw &&
	    !enumerated_ref_is_zero(&ca->io_ref[READ]))
		__bch2_dev_read_write(c, ca);
	up_write(&c->state_lock);
	return ret;
}

/* Add new device to running filesystem: */
int bch2_dev_add(struct bch_fs *c, const char *path)
{
	struct bch_opts opts = bch2_opts_empty();
	struct bch_sb_handle sb = {};
	struct bch_dev *ca = NULL;
	struct printbuf errbuf = PRINTBUF;
	struct printbuf label = PRINTBUF;
	int ret = 0;

	ret = bch2_read_super(path, &opts, &sb);
	bch_err_msg(c, ret, "reading super");
	if (ret)
		goto err;

	struct bch_member dev_mi = bch2_sb_member_get(sb.sb, sb.sb->dev_idx);

	if (BCH_MEMBER_GROUP(&dev_mi)) {
		bch2_disk_path_to_text_sb(&label, sb.sb, BCH_MEMBER_GROUP(&dev_mi) - 1);
		if (label.allocation_failure) {
			ret = -ENOMEM;
			goto err;
		}
	}

	if (list_empty(&c->list)) {
		mutex_lock(&bch_fs_list_lock);
		if (__bch2_uuid_to_fs(c->sb.uuid))
			ret = bch_err_throw(c, filesystem_uuid_already_open);
		else
			list_add(&c->list, &bch_fs_list);
		mutex_unlock(&bch_fs_list_lock);

		if (ret) {
			bch_err(c, "filesystem UUID already open");
			goto err;
		}
	}

	ret = bch2_dev_may_add(sb.sb, c);
	if (ret)
		goto err;

	ca = __bch2_dev_alloc(c, &dev_mi);
	if (!ca) {
		ret = -ENOMEM;
		goto err;
	}

	ret = __bch2_dev_attach_bdev(ca, &sb);
	if (ret)
		goto err;

	down_write(&c->state_lock);
	mutex_lock(&c->sb_lock);
	SET_BCH_SB_MULTI_DEVICE(c->disk_sb.sb, true);

	ret = bch2_sb_from_fs(c, ca);
	bch_err_msg(c, ret, "setting up new superblock");
	if (ret)
		goto err_unlock;

	if (dynamic_fault("bcachefs:add:no_slot"))
		goto err_unlock;

	ret = bch2_sb_member_alloc(c);
	if (ret < 0) {
		bch_err_msg(c, ret, "setting up new superblock");
		goto err_unlock;
	}
	unsigned dev_idx = ret;
	ret = 0;

	/* success: */

	dev_mi.last_mount = cpu_to_le64(ktime_get_real_seconds());
	*bch2_members_v2_get_mut(c->disk_sb.sb, dev_idx) = dev_mi;

	ca->disk_sb.sb->dev_idx	= dev_idx;
	bch2_dev_attach(c, ca, dev_idx);

	if (BCH_MEMBER_GROUP(&dev_mi)) {
		ret = __bch2_dev_group_set(c, ca, label.buf);
		bch_err_msg(c, ret, "creating new label");
		if (ret)
			goto err_unlock;
	}

	bch2_write_super(c);
	mutex_unlock(&c->sb_lock);

	if (test_bit(BCH_FS_started, &c->flags)) {
		ret = bch2_dev_usage_init(ca, false);
		if (ret)
			goto err_late;

		ret = bch2_trans_mark_dev_sb(c, ca, BTREE_TRIGGER_transactional);
		bch_err_msg(ca, ret, "marking new superblock");
		if (ret)
			goto err_late;

		ret = bch2_fs_freespace_init(c);
		bch_err_msg(ca, ret, "initializing free space");
		if (ret)
			goto err_late;

		if (ca->mi.state == BCH_MEMBER_STATE_rw)
			__bch2_dev_read_write(c, ca);

		ret = bch2_dev_journal_alloc(ca, false);
		bch_err_msg(c, ret, "allocating journal");
		if (ret)
			goto err_late;
	}

	/*
	 * We just changed the superblock UUID, invalidate cache and send a
	 * uevent to update /dev/disk/by-uuid
	 */
	invalidate_bdev(ca->disk_sb.bdev);

	char uuid_str[37];
	snprintf(uuid_str, sizeof(uuid_str), "UUID=%pUb", &c->sb.uuid);

	char *envp[] = {
		"CHANGE=uuid",
		uuid_str,
		NULL,
	};
	kobject_uevent_env(&ca->disk_sb.bdev->bd_device.kobj, KOBJ_CHANGE, envp);

	up_write(&c->state_lock);
out:
	printbuf_exit(&label);
	printbuf_exit(&errbuf);
	bch_err_fn(c, ret);
	return ret;

err_unlock:
	mutex_unlock(&c->sb_lock);
	up_write(&c->state_lock);
err:
	if (ca)
		bch2_dev_free(ca);
	bch2_free_super(&sb);
	goto out;
err_late:
	up_write(&c->state_lock);
	ca = NULL;
	goto err;
}

/* Hot add existing device to running filesystem: */
int bch2_dev_online(struct bch_fs *c, const char *path)
{
	struct bch_opts opts = bch2_opts_empty();
	struct bch_sb_handle sb = { NULL };
	struct bch_dev *ca;
	unsigned dev_idx;
	int ret;

	down_write(&c->state_lock);

	ret = bch2_read_super(path, &opts, &sb);
	if (ret) {
		up_write(&c->state_lock);
		return ret;
	}

	dev_idx = sb.sb->dev_idx;

	ret = bch2_dev_in_fs(&c->disk_sb, &sb, &c->opts);
	bch_err_msg(c, ret, "bringing %s online", path);
	if (ret)
		goto err;

	ret = bch2_dev_attach_bdev(c, &sb);
	if (ret)
		goto err;

	ca = bch2_dev_locked(c, dev_idx);

	ret = bch2_trans_mark_dev_sb(c, ca, BTREE_TRIGGER_transactional);
	bch_err_msg(c, ret, "bringing %s online: error from bch2_trans_mark_dev_sb", path);
	if (ret)
		goto err;

	if (ca->mi.state == BCH_MEMBER_STATE_rw)
		__bch2_dev_read_write(c, ca);

	if (!ca->mi.freespace_initialized) {
		ret = bch2_dev_freespace_init(c, ca, 0, ca->mi.nbuckets);
		bch_err_msg(ca, ret, "initializing free space");
		if (ret)
			goto err;
	}

	if (!ca->journal.nr) {
		ret = bch2_dev_journal_alloc(ca, false);
		bch_err_msg(ca, ret, "allocating journal");
		if (ret)
			goto err;
	}

	mutex_lock(&c->sb_lock);
	bch2_members_v2_get_mut(c->disk_sb.sb, ca->dev_idx)->last_mount =
		cpu_to_le64(ktime_get_real_seconds());
	bch2_write_super(c);
	mutex_unlock(&c->sb_lock);

	up_write(&c->state_lock);
	return 0;
err:
	up_write(&c->state_lock);
	bch2_free_super(&sb);
	return ret;
}

int bch2_dev_offline(struct bch_fs *c, struct bch_dev *ca, int flags)
{
	down_write(&c->state_lock);

	if (!bch2_dev_is_online(ca)) {
		bch_err(ca, "Already offline");
		up_write(&c->state_lock);
		return 0;
	}

	if (!bch2_dev_state_allowed(c, ca, BCH_MEMBER_STATE_failed, flags)) {
		bch_err(ca, "Cannot offline required disk");
		up_write(&c->state_lock);
		return bch_err_throw(c, device_state_not_allowed);
	}

	__bch2_dev_offline(c, ca);

	up_write(&c->state_lock);
	return 0;
}

static int __bch2_dev_resize_alloc(struct bch_dev *ca, u64 old_nbuckets, u64 new_nbuckets)
{
	struct bch_fs *c = ca->fs;
	u64 v[3] = { new_nbuckets - old_nbuckets, 0, 0 };

	return bch2_trans_commit_do(ca->fs, NULL, NULL, 0,
			bch2_disk_accounting_mod2(trans, false, v, dev_data_type,
						  .dev = ca->dev_idx,
						  .data_type = BCH_DATA_free)) ?:
		bch2_dev_freespace_init(c, ca, old_nbuckets, new_nbuckets);
}

int bch2_dev_resize(struct bch_fs *c, struct bch_dev *ca, u64 nbuckets)
{
	struct bch_member *m;
	u64 old_nbuckets;
	int ret = 0;

	down_write(&c->state_lock);
	old_nbuckets = ca->mi.nbuckets;

	if (nbuckets < ca->mi.nbuckets) {
		bch_err(ca, "Cannot shrink yet");
		ret = -EINVAL;
		goto err;
	}

	if (nbuckets > BCH_MEMBER_NBUCKETS_MAX) {
		bch_err(ca, "New device size too big (%llu greater than max %u)",
			nbuckets, BCH_MEMBER_NBUCKETS_MAX);
		ret = bch_err_throw(c, device_size_too_big);
		goto err;
	}

	if (bch2_dev_is_online(ca) &&
	    get_capacity(ca->disk_sb.bdev->bd_disk) <
	    ca->mi.bucket_size * nbuckets) {
		bch_err(ca, "New size larger than device");
		ret = bch_err_throw(c, device_size_too_small);
		goto err;
	}

	ret = bch2_dev_buckets_resize(c, ca, nbuckets);
	bch_err_msg(ca, ret, "resizing buckets");
	if (ret)
		goto err;

	ret = bch2_trans_mark_dev_sb(c, ca, BTREE_TRIGGER_transactional);
	if (ret)
		goto err;

	mutex_lock(&c->sb_lock);
	m = bch2_members_v2_get_mut(c->disk_sb.sb, ca->dev_idx);
	m->nbuckets = cpu_to_le64(nbuckets);

	bch2_write_super(c);
	mutex_unlock(&c->sb_lock);

	if (ca->mi.freespace_initialized) {
		ret = __bch2_dev_resize_alloc(ca, old_nbuckets, nbuckets);
		if (ret)
			goto err;
	}

	bch2_recalc_capacity(c);
err:
	up_write(&c->state_lock);
	return ret;
}

int bch2_fs_resize_on_mount(struct bch_fs *c)
{
	for_each_online_member(c, ca, BCH_DEV_READ_REF_fs_resize_on_mount) {
		u64 old_nbuckets = ca->mi.nbuckets;
		u64 new_nbuckets = div64_u64(get_capacity(ca->disk_sb.bdev->bd_disk),
					 ca->mi.bucket_size);

		if (ca->mi.resize_on_mount &&
		    new_nbuckets > ca->mi.nbuckets) {
			bch_info(ca, "resizing to size %llu", new_nbuckets * ca->mi.bucket_size);
			int ret = bch2_dev_buckets_resize(c, ca, new_nbuckets);
			bch_err_fn(ca, ret);
			if (ret) {
				enumerated_ref_put(&ca->io_ref[READ],
						   BCH_DEV_READ_REF_fs_resize_on_mount);
				up_write(&c->state_lock);
				return ret;
			}

			mutex_lock(&c->sb_lock);
			struct bch_member *m =
				bch2_members_v2_get_mut(c->disk_sb.sb, ca->dev_idx);
			m->nbuckets = cpu_to_le64(new_nbuckets);
			SET_BCH_MEMBER_RESIZE_ON_MOUNT(m, false);

			c->disk_sb.sb->features[0] &= ~cpu_to_le64(BIT_ULL(BCH_FEATURE_small_image));
			bch2_write_super(c);
			mutex_unlock(&c->sb_lock);

			if (ca->mi.freespace_initialized) {
				ret = __bch2_dev_resize_alloc(ca, old_nbuckets, new_nbuckets);
				if (ret) {
					enumerated_ref_put(&ca->io_ref[READ],
							BCH_DEV_READ_REF_fs_resize_on_mount);
					up_write(&c->state_lock);
					return ret;
				}
			}
		}
	}
	return 0;
}

/* return with ref on ca->ref: */
struct bch_dev *bch2_dev_lookup(struct bch_fs *c, const char *name)
{
	if (!strncmp(name, "/dev/", strlen("/dev/")))
		name += strlen("/dev/");

	for_each_member_device(c, ca)
		if (!strcmp(name, ca->name))
			return ca;
	return ERR_PTR(-BCH_ERR_ENOENT_dev_not_found);
}

/* blk_holder_ops: */

static struct bch_fs *bdev_get_fs(struct block_device *bdev)
	__releases(&bdev->bd_holder_lock)
{
	struct bch_sb_handle_holder *holder = bdev->bd_holder;
	struct bch_fs *c = holder->c;

	if (c && !bch2_ro_ref_tryget(c))
		c = NULL;

	mutex_unlock(&bdev->bd_holder_lock);

	if (c)
		wait_event(c->ro_ref_wait, test_bit(BCH_FS_started, &c->flags));
	return c;
}

/* returns with ref on ca->ref */
static struct bch_dev *bdev_to_bch_dev(struct bch_fs *c, struct block_device *bdev)
{
	for_each_member_device(c, ca)
		if (ca->disk_sb.bdev == bdev)
			return ca;
	return NULL;
}

static void bch2_fs_bdev_mark_dead(struct block_device *bdev, bool surprise)
{
	struct bch_fs *c = bdev_get_fs(bdev);
	if (!c)
		return;

	struct super_block *sb = c->vfs_sb;
	if (sb) {
		/*
		 * Not necessary, c->ro_ref guards against the filesystem being
		 * unmounted - we only take this to avoid a warning in
		 * sync_filesystem:
		 */
		down_read(&sb->s_umount);
	}

	down_write(&c->state_lock);
	struct bch_dev *ca = bdev_to_bch_dev(c, bdev);
	if (!ca)
		goto unlock;

	bool dev = bch2_dev_state_allowed(c, ca,
					  BCH_MEMBER_STATE_failed,
					  BCH_FORCE_IF_DEGRADED);

	if (!dev && sb) {
		if (!surprise)
			sync_filesystem(sb);
		shrink_dcache_sb(sb);
		evict_inodes(sb);
	}

	struct printbuf buf = PRINTBUF;
	__bch2_log_msg_start(ca->name, &buf);

	prt_printf(&buf, "offline from block layer");

	if (dev) {
		__bch2_dev_offline(c, ca);
	} else {
		bch2_journal_flush(&c->journal);
		bch2_fs_emergency_read_only2(c, &buf);
	}

	bch2_print_str(c, KERN_ERR, buf.buf);
	printbuf_exit(&buf);

	bch2_dev_put(ca);
unlock:
	if (sb)
		up_read(&sb->s_umount);
	up_write(&c->state_lock);
	bch2_ro_ref_put(c);
}

static void bch2_fs_bdev_sync(struct block_device *bdev)
{
	struct bch_fs *c = bdev_get_fs(bdev);
	if (!c)
		return;

	struct super_block *sb = c->vfs_sb;
	if (sb) {
		/*
		 * Not necessary, c->ro_ref guards against the filesystem being
		 * unmounted - we only take this to avoid a warning in
		 * sync_filesystem:
		 */
		down_read(&sb->s_umount);
		sync_filesystem(sb);
		up_read(&sb->s_umount);
	}

	bch2_ro_ref_put(c);
}

const struct blk_holder_ops bch2_sb_handle_bdev_ops = {
	.mark_dead		= bch2_fs_bdev_mark_dead,
	.sync			= bch2_fs_bdev_sync,
};

/* Filesystem open: */

static inline int sb_cmp(struct bch_sb *l, struct bch_sb *r)
{
	return  cmp_int(le64_to_cpu(l->seq), le64_to_cpu(r->seq)) ?:
		cmp_int(le64_to_cpu(l->write_time), le64_to_cpu(r->write_time));
}

struct bch_fs *bch2_fs_open(darray_const_str *devices,
			    struct bch_opts *opts)
{
	bch_sb_handles sbs = {};
	struct bch_fs *c = NULL;
	struct bch_sb_handle *best = NULL;
	struct printbuf errbuf = PRINTBUF;
	int ret = 0;

	if (!try_module_get(THIS_MODULE))
		return ERR_PTR(-ENODEV);

	if (!devices->nr) {
		ret = -EINVAL;
		goto err;
	}

	ret = darray_make_room(&sbs, devices->nr);
	if (ret)
		goto err;

	darray_for_each(*devices, i) {
		struct bch_sb_handle sb = { NULL };

		ret = bch2_read_super(*i, opts, &sb);
		if (ret)
			goto err;

		BUG_ON(darray_push(&sbs, sb));
	}

	if (opts->nochanges && !opts->read_only) {
		ret = bch_err_throw(c, erofs_nochanges);
		goto err_print;
	}

	darray_for_each(sbs, sb)
		if (!best || sb_cmp(sb->sb, best->sb) > 0)
			best = sb;

	darray_for_each_reverse(sbs, sb) {
		ret = bch2_dev_in_fs(best, sb, opts);

		if (ret == -BCH_ERR_device_has_been_removed ||
		    ret == -BCH_ERR_device_splitbrain) {
			bch2_free_super(sb);
			darray_remove_item(&sbs, sb);
			best -= best > sb;
			ret = 0;
			continue;
		}

		if (ret)
			goto err_print;
	}

	c = bch2_fs_alloc(best->sb, opts, &sbs);
	ret = PTR_ERR_OR_ZERO(c);
	if (ret)
		goto err;

	down_write(&c->state_lock);
	darray_for_each(sbs, sb) {
		ret = bch2_dev_attach_bdev(c, sb);
		if (ret) {
			up_write(&c->state_lock);
			goto err;
		}
	}
	up_write(&c->state_lock);

	if (!c->opts.nostart) {
		ret = bch2_fs_start(c);
		if (ret)
			goto err;
	}
out:
	darray_for_each(sbs, sb)
		bch2_free_super(sb);
	darray_exit(&sbs);
	printbuf_exit(&errbuf);
	module_put(THIS_MODULE);
	return c;
err_print:
	pr_err("bch_fs_open err opening %s: %s",
	       devices->data[0], bch2_err_str(ret));
err:
	if (!IS_ERR_OR_NULL(c))
		bch2_fs_stop(c);
	c = ERR_PTR(ret);
	goto out;
}

/* Global interfaces/init */

static void bcachefs_exit(void)
{
	bch2_debug_exit();
	bch2_vfs_exit();
	bch2_chardev_exit();
	bch2_btree_key_cache_exit();
	if (bcachefs_kset)
		kset_unregister(bcachefs_kset);
}

static int __init bcachefs_init(void)
{
	bch2_bkey_pack_test();

	if (!(bcachefs_kset = kset_create_and_add("bcachefs", NULL, fs_kobj)) ||
	    bch2_btree_key_cache_init() ||
	    bch2_chardev_init() ||
	    bch2_vfs_init() ||
	    bch2_debug_init())
		goto err;

	return 0;
err:
	bcachefs_exit();
	return -ENOMEM;
}

#define BCH_DEBUG_PARAM(name, description) DEFINE_STATIC_KEY_FALSE(bch2_##name);
BCH_DEBUG_PARAMS_ALL()
#undef BCH_DEBUG_PARAM

static int bch2_param_set_static_key_t(const char *val, const struct kernel_param *kp)
{
	/* Match bool exactly, by re-using it. */
	struct static_key *key = kp->arg;
	struct kernel_param boolkp = *kp;
	bool v;
	int ret;

	boolkp.arg = &v;

	ret = param_set_bool(val, &boolkp);
	if (ret)
		return ret;
	if (v)
		static_key_enable(key);
	else
		static_key_disable(key);
	return 0;
}

static int bch2_param_get_static_key_t(char *buffer, const struct kernel_param *kp)
{
	struct static_key *key = kp->arg;
	return sprintf(buffer, "%c\n", static_key_enabled(key) ? 'N' : 'Y');
}

static const struct kernel_param_ops bch2_param_ops_static_key_t = {
	.flags = KERNEL_PARAM_OPS_FL_NOARG,
	.set = bch2_param_set_static_key_t,
	.get = bch2_param_get_static_key_t,
};

#define BCH_DEBUG_PARAM(name, description)				\
	module_param_cb(name, &bch2_param_ops_static_key_t, &bch2_##name.key, 0644);\
	__MODULE_PARM_TYPE(name, "static_key_t");			\
	MODULE_PARM_DESC(name, description);
BCH_DEBUG_PARAMS()
#undef BCH_DEBUG_PARAM

__maybe_unused
static unsigned bch2_metadata_version = bcachefs_metadata_version_current;
module_param_named(version, bch2_metadata_version, uint, 0444);

module_exit(bcachefs_exit);
module_init(bcachefs_init);
