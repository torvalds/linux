// SPDX-License-Identifier: GPL-2.0
/*
 * fs/f2fs/checkpoint.c
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *             http://www.samsung.com/
 */
#include <linux/fs.h>
#include <linux/bio.h>
#include <linux/writeback.h>
#include <linux/blkdev.h>
#include <linux/f2fs_fs.h>
#include <linux/folio_batch.h>
#include <linux/swap.h>
#include <linux/kthread.h>
#include <linux/delayacct.h>
#include <linux/ioprio.h>
#include <linux/math64.h>
#include <linux/freezer.h>

#include "f2fs.h"
#include "node.h"
#include "segment.h"
#include "iostat.h"
#include <trace/events/f2fs.h>

static inline void get_lock_elapsed_time(struct f2fs_time_stat *ts)
{
	ts->total_time = ktime_get();
#ifdef CONFIG_64BIT
	ts->running_time = current->se.sum_exec_runtime;
#endif
#if defined(CONFIG_SCHED_INFO) && defined(CONFIG_SCHEDSTATS)
	ts->runnable_time = current->sched_info.run_delay;
#endif
#ifdef CONFIG_TASK_DELAY_ACCT
	if (current->delays)
		ts->io_sleep_time = current->delays->blkio_delay;
#endif
}

static inline void trace_lock_elapsed_time_start(struct f2fs_rwsem *sem,
						struct f2fs_lock_context *lc)
{
	lc->lock_trace = trace_f2fs_lock_elapsed_time_enabled();
	if (!lc->lock_trace)
		return;

	get_lock_elapsed_time(&lc->ts);
}

static inline void trace_lock_elapsed_time_end(struct f2fs_rwsem *sem,
				struct f2fs_lock_context *lc, bool is_write)
{
	struct f2fs_time_stat tts;
	unsigned long long total_time;
	unsigned long long running_time = 0;
	unsigned long long runnable_time = 0;
	unsigned long long io_sleep_time = 0;
	unsigned long long other_time = 0;
	unsigned npm = NSEC_PER_MSEC;

	if (!lc->lock_trace)
		return;

	if (time_to_inject(sem->sbi, FAULT_LOCK_TIMEOUT))
		f2fs_schedule_timeout_killable(DEFAULT_FAULT_TIMEOUT, true);

	get_lock_elapsed_time(&tts);

	total_time = div_u64(tts.total_time - lc->ts.total_time, npm);
	if (total_time <= sem->sbi->max_lock_elapsed_time)
		return;

#ifdef CONFIG_64BIT
	running_time = div_u64(tts.running_time - lc->ts.running_time, npm);
#endif
#if defined(CONFIG_SCHED_INFO) && defined(CONFIG_SCHEDSTATS)
	runnable_time = div_u64(tts.runnable_time - lc->ts.runnable_time, npm);
#endif
#ifdef CONFIG_TASK_DELAY_ACCT
	io_sleep_time = div_u64(tts.io_sleep_time - lc->ts.io_sleep_time, npm);
#endif
	if (total_time > running_time + io_sleep_time + runnable_time)
		other_time = total_time - running_time -
				io_sleep_time - runnable_time;

	trace_f2fs_lock_elapsed_time(sem->sbi, sem->name, is_write, current,
			get_current_ioprio(), total_time, running_time,
			runnable_time, io_sleep_time, other_time);
}

static bool need_uplift_priority(struct f2fs_rwsem *sem, bool is_write)
{
	if (!(sem->sbi->adjust_lock_priority & BIT(sem->name - 1)))
		return false;

	switch (sem->name) {
	/*
	 * writer is checkpoint which has high priority, let's just uplift
	 * priority for reader
	 */
	case LOCK_NAME_CP_RWSEM:
	case LOCK_NAME_NODE_CHANGE:
	case LOCK_NAME_NODE_WRITE:
		return !is_write;
	case LOCK_NAME_GC_LOCK:
	case LOCK_NAME_CP_GLOBAL:
	case LOCK_NAME_IO_RWSEM:
	case LOCK_NAME_NAT_TREE_LOCK:
		return true;
	default:
		f2fs_bug_on(sem->sbi, 1);
	}
	return false;
}

static void uplift_priority(struct f2fs_rwsem *sem, struct f2fs_lock_context *lc,
						bool is_write)
{
	lc->need_restore = false;
	if (!sem->sbi->adjust_lock_priority)
		return;
	if (rt_task(current))
		return;
	if (!need_uplift_priority(sem, is_write))
		return;
	lc->orig_nice = task_nice(current);
	lc->new_nice = PRIO_TO_NICE(sem->sbi->lock_duration_priority);
	if (lc->orig_nice <= lc->new_nice)
		return;
	set_user_nice(current, lc->new_nice);
	lc->need_restore = true;

	trace_f2fs_priority_uplift(sem->sbi, sem->name, is_write, current,
		NICE_TO_PRIO(lc->orig_nice), NICE_TO_PRIO(lc->new_nice));
}

static void restore_priority(struct f2fs_rwsem *sem, struct f2fs_lock_context *lc,
						bool is_write)
{
	if (!lc->need_restore)
		return;
	/* someone has updated the priority */
	if (task_nice(current) != lc->new_nice)
		return;
	set_user_nice(current, lc->orig_nice);

	trace_f2fs_priority_restore(sem->sbi, sem->name, is_write, current,
		NICE_TO_PRIO(lc->orig_nice), NICE_TO_PRIO(lc->new_nice));
}

void f2fs_down_read_trace(struct f2fs_rwsem *sem, struct f2fs_lock_context *lc)
{
	uplift_priority(sem, lc, false);
	f2fs_down_read(sem);
	trace_lock_elapsed_time_start(sem, lc);
}

int f2fs_down_read_trylock_trace(struct f2fs_rwsem *sem, struct f2fs_lock_context *lc)
{
	uplift_priority(sem, lc, false);
	if (!f2fs_down_read_trylock(sem)) {
		restore_priority(sem, lc, false);
		return 0;
	}
	trace_lock_elapsed_time_start(sem, lc);
	return 1;
}

void f2fs_up_read_trace(struct f2fs_rwsem *sem, struct f2fs_lock_context *lc)
{
	f2fs_up_read(sem);
	restore_priority(sem, lc, false);
	trace_lock_elapsed_time_end(sem, lc, false);
}

void f2fs_down_write_trace(struct f2fs_rwsem *sem, struct f2fs_lock_context *lc)
{
	uplift_priority(sem, lc, true);
	f2fs_down_write(sem);
	trace_lock_elapsed_time_start(sem, lc);
}

int f2fs_down_write_trylock_trace(struct f2fs_rwsem *sem, struct f2fs_lock_context *lc)
{
	uplift_priority(sem, lc, true);
	if (!f2fs_down_write_trylock(sem)) {
		restore_priority(sem, lc, true);
		return 0;
	}
	trace_lock_elapsed_time_start(sem, lc);
	return 1;
}

void f2fs_up_write_trace(struct f2fs_rwsem *sem, struct f2fs_lock_context *lc)
{
	f2fs_up_write(sem);
	restore_priority(sem, lc, true);
	trace_lock_elapsed_time_end(sem, lc, true);
}

void f2fs_lock_op(struct f2fs_sb_info *sbi, struct f2fs_lock_context *lc)
{
	f2fs_down_read_trace(&sbi->cp_rwsem, lc);
}

int f2fs_trylock_op(struct f2fs_sb_info *sbi, struct f2fs_lock_context *lc)
{
	if (time_to_inject(sbi, FAULT_LOCK_OP))
		return 0;

	return f2fs_down_read_trylock_trace(&sbi->cp_rwsem, lc);
}

void f2fs_unlock_op(struct f2fs_sb_info *sbi, struct f2fs_lock_context *lc)
{
	f2fs_up_read_trace(&sbi->cp_rwsem, lc);
}

static inline void f2fs_lock_all(struct f2fs_sb_info *sbi)
{
	f2fs_down_write(&sbi->cp_rwsem);
}

static inline void f2fs_unlock_all(struct f2fs_sb_info *sbi)
{
	f2fs_up_write(&sbi->cp_rwsem);
}

#define DEFAULT_CHECKPOINT_IOPRIO (IOPRIO_PRIO_VALUE(IOPRIO_CLASS_RT, 3))

static struct kmem_cache *ino_entry_slab;
struct kmem_cache *f2fs_inode_entry_slab;

/*
 * We guarantee no failure on the returned cache.
 */
struct f2fs_cached_block *f2fs_grab_meta_cache(struct f2fs_sb_info *sbi,
							pgoff_t index)
{
	struct f2fs_cached_block *entry;
repeat:
	entry = f2fs_grab_cache(META_CACHE(sbi), index,
					F2FS_CACHE_LOCK_CREATE);
	if (IS_ERR(entry)) {
		cond_resched();
		goto repeat;
	}
	f2fs_cache_wait_writeback(entry);
	if (!f2fs_cache_test_uptodate(entry))
		f2fs_cache_set_uptodate(entry);
	return entry;
}

static struct f2fs_cached_block *__get_meta_cache(struct f2fs_sb_info *sbi,
					pgoff_t index, bool is_meta)
{
	struct f2fs_cached_block *entry;
	struct f2fs_io_info fio = {
		.sbi = sbi,
		.type = META,
		.op = REQ_OP_READ,
		.op_flags = REQ_META | REQ_PRIO,
		.old_blkaddr = index,
		.new_blkaddr = index,
		.encrypted_page = NULL,
		.is_por = !is_meta ? 1 : 0,
		.is_cache = 1,
	};
	int err;

	if (unlikely(!is_meta))
		fio.op_flags &= ~REQ_META;
repeat:
	entry = f2fs_grab_cache(META_CACHE(sbi), index,
					F2FS_CACHE_LOCK_CREATE);
	if (IS_ERR(entry)) {
		cond_resched();
		goto repeat;
	}
	if (f2fs_cache_test_uptodate(entry))
		goto out;

	fio.cache_entry = entry;

	err = f2fs_submit_cache_read(&fio);
	if (err) {
		f2fs_put_cache(entry, true);
		return ERR_PTR(err);
	}

	f2fs_update_iostat(sbi, NULL, FS_META_READ_IO, F2FS_BLKSIZE(sbi));

	f2fs_lock_cache(entry);
	if (unlikely(!f2fs_is_meta_cache(entry))) {
		f2fs_put_cache(entry, true);
		goto repeat;
	}

	if (unlikely(!f2fs_cache_test_uptodate(entry))) {
		f2fs_handle_page_eio(sbi, entry->index, META);
		f2fs_put_cache(entry, true);
		return ERR_PTR(-EIO);
	}
out:
	return entry;
}

struct f2fs_cached_block *f2fs_get_meta_cache(struct f2fs_sb_info *sbi,
							pgoff_t index)
{
	return __get_meta_cache(sbi, index, true);
}

struct f2fs_cached_block *f2fs_get_meta_cache_retry(struct f2fs_sb_info *sbi,
							pgoff_t index)
{
	struct f2fs_cached_block *entry;
	int count = 0;

retry:
	entry = __get_meta_cache(sbi, index, true);
	if (IS_ERR(entry)) {
		if (PTR_ERR(entry) == -EIO &&
				++count <= DEFAULT_RETRY_IO_COUNT)
			goto retry;
		f2fs_stop_checkpoint(sbi, false, STOP_CP_REASON_META_PAGE);
	}
	return entry;
}

/* for POR only */
struct f2fs_cached_block *f2fs_get_tmp_cache(struct f2fs_sb_info *sbi,
							pgoff_t index)
{
	return __get_meta_cache(sbi, index, false);
}

static bool __is_bitmap_valid(struct f2fs_sb_info *sbi, block_t blkaddr,
							int type)
{
	struct seg_entry *se;
	unsigned int segno, offset;
	bool exist;

	if (type == DATA_GENERIC)
		return true;

	segno = GET_SEGNO(sbi, blkaddr);
	offset = GET_BLKOFF_FROM_SEG0(sbi, blkaddr);
	se = get_seg_entry(sbi, segno);

	exist = f2fs_test_bit(offset, se->cur_valid_map);

	/* skip data, if we already have an error in checkpoint. */
	if (unlikely(f2fs_cp_error(sbi)))
		return exist;

	if ((exist && type == DATA_GENERIC_ENHANCE_UPDATE) ||
		(!exist && type == DATA_GENERIC_ENHANCE))
		goto out_err;
	if (!exist && type != DATA_GENERIC_ENHANCE_UPDATE)
		goto out_handle;
	return exist;

out_err:
	f2fs_err(sbi, "Inconsistent error blkaddr:%u, sit bitmap:%d",
		 blkaddr, exist);
	set_sbi_flag(sbi, SBI_NEED_FSCK);
	dump_stack();
out_handle:
	f2fs_handle_error(sbi, ERROR_INVALID_BLKADDR);
	return exist;
}

static bool __f2fs_is_valid_blkaddr(struct f2fs_sb_info *sbi,
					block_t blkaddr, int type)
{
	switch (type) {
	case META_NAT:
		break;
	case META_SIT:
		if (unlikely(blkaddr >= SIT_BLK_CNT(sbi)))
			goto check_only;
		break;
	case META_SSA:
		if (unlikely(blkaddr >= MAIN_BLKADDR(sbi) ||
			blkaddr < SM_I(sbi)->ssa_blkaddr))
			goto check_only;
		break;
	case META_CP:
		if (unlikely(blkaddr >= SIT_I(sbi)->sit_base_addr ||
			blkaddr < __start_cp_addr(sbi)))
			goto check_only;
		break;
	case META_POR:
		if (unlikely(blkaddr >= MAX_BLKADDR(sbi) ||
			blkaddr < MAIN_BLKADDR(sbi)))
			goto check_only;
		break;
	case DATA_GENERIC:
	case DATA_GENERIC_ENHANCE:
	case DATA_GENERIC_ENHANCE_READ:
	case DATA_GENERIC_ENHANCE_UPDATE:
		if (unlikely(blkaddr >= MAX_BLKADDR(sbi) ||
				blkaddr < MAIN_BLKADDR(sbi))) {

			/* Skip to emit an error message. */
			if (unlikely(f2fs_cp_error(sbi)))
				return false;

			f2fs_warn(sbi, "access invalid blkaddr:%u",
				  blkaddr);
			set_sbi_flag(sbi, SBI_NEED_FSCK);
			dump_stack();
			goto err;
		} else {
			return __is_bitmap_valid(sbi, blkaddr, type);
		}
		break;
	case META_GENERIC:
		if (unlikely(blkaddr < SEG0_BLKADDR(sbi) ||
			blkaddr >= MAIN_BLKADDR(sbi)))
			goto err;
		break;
	default:
		BUG();
	}

	return true;
err:
	f2fs_handle_error(sbi, ERROR_INVALID_BLKADDR);
check_only:
	return false;
}

bool f2fs_is_valid_blkaddr(struct f2fs_sb_info *sbi,
					block_t blkaddr, int type)
{
	if (time_to_inject(sbi, FAULT_BLKADDR_VALIDITY))
		return false;
	return __f2fs_is_valid_blkaddr(sbi, blkaddr, type);
}

bool f2fs_is_valid_blkaddr_raw(struct f2fs_sb_info *sbi,
					block_t blkaddr, int type)
{
	return __f2fs_is_valid_blkaddr(sbi, blkaddr, type);
}

/*
 * Readahead CP/NAT/SIT/SSA/POR blocks
 */
int f2fs_ra_meta_caches(struct f2fs_sb_info *sbi, block_t start,
				int nrblocks, int type, bool sync)
{
	struct f2fs_cached_block_list *cache = META_CACHE(sbi);
	block_t blkno = start;
	struct f2fs_io_info fio = {
		.sbi = sbi,
		.type = META,
		.op = REQ_OP_READ,
		.op_flags = sync ? (REQ_META | REQ_PRIO) : REQ_RAHEAD,
		.encrypted_page = NULL,
		.in_list = 0,
		.is_por = (type == META_POR) ? 1 : 0,
		.is_cache = 1,
	};
	struct blk_plug plug;
	int err;

	if (unlikely(type == META_POR))
		fio.op_flags &= ~REQ_META;

	blk_start_plug(&plug);
	for (; nrblocks-- > 0; blkno++) {
		struct f2fs_cached_block *entry;

		if (!f2fs_is_valid_blkaddr(sbi, blkno, type))
			goto out;

		switch (type) {
		case META_NAT:
			if (unlikely(blkno >=
					NAT_BLOCK_OFFSET(sbi, NM_I(sbi)->max_nid)))
				blkno = 0;
			/* get nat block addr */
			fio.new_blkaddr = current_nat_addr(sbi,
					blkno * NAT_ENTRY_PER_BLOCK(sbi));
			break;
		case META_SIT:
			if (unlikely(blkno >= TOTAL_SEGS(sbi)))
				goto out;
			/* get sit block addr */
			fio.new_blkaddr = current_sit_addr(sbi,
					blkno * SIT_ENTRY_PER_BLOCK(sbi));
			break;
		case META_SSA:
		case META_CP:
		case META_POR:
			fio.new_blkaddr = blkno;
			break;
		default:
			f2fs_bug_on(sbi, 1);
		}

		entry = f2fs_grab_cache(cache, fio.new_blkaddr,
						F2FS_CACHE_LOCK_CREATE);
		if (IS_ERR(entry))
			continue;
		if (f2fs_cache_test_uptodate(entry)) {
			f2fs_put_cache(entry, true);
			continue;
		}

		fio.cache_entry = entry;
		err = f2fs_submit_cache_read(&fio);
		f2fs_put_cache(entry, err ? true : false);

		if (!err)
			f2fs_update_iostat(sbi, NULL, FS_META_READ_IO,
							F2FS_BLKSIZE(sbi));
	}
out:
	blk_finish_plug(&plug);
	return blkno - start;
}

void f2fs_ra_meta_caches_cond(struct f2fs_sb_info *sbi, pgoff_t index,
						unsigned int ra_blocks)
{
	struct f2fs_cached_block *entry;
	bool readahead = false;

	if (ra_blocks == RECOVERY_MIN_RA_BLOCKS)
		return;

	entry = f2fs_find_cache(META_CACHE(sbi), index, 0);
	if (IS_ERR(entry) || !f2fs_cache_test_uptodate(entry))
		readahead = true;
	f2fs_put_cache(entry, false);

	if (readahead)
		f2fs_ra_meta_caches(sbi, index, ra_blocks, META_POR, true);
}

static bool __f2fs_write_meta_cache(struct f2fs_cached_block *entry,
				enum iostat_type io_type)
{
	struct f2fs_sb_info *sbi = entry->cache->sbi;

	trace_f2fs_write_cache(entry, META);

	if (unlikely(f2fs_cp_error(sbi))) {
		if (is_sbi_flag_set(sbi, SBI_IS_CLOSE)) {
			f2fs_cache_clear_uptodate(entry);
			dec_cache_count(sbi, F2FS_DIRTY_META);
			f2fs_cache_update_tag(entry, F2FS_CACHE_TAG_DIRTY,
						F2FS_CACHE_TAG_NONE);
			f2fs_unlock_cache(entry);
			return true;
		}
		goto redirty_out;
	}
	if (unlikely(is_sbi_flag_set(sbi, SBI_POR_DOING)))
		goto redirty_out;

	f2fs_do_write_meta_cache(sbi, entry, io_type);
	dec_cache_count(sbi, F2FS_DIRTY_META);

	f2fs_unlock_cache(entry);

	if (unlikely(f2fs_cp_error(sbi)))
		f2fs_submit_merged_write(sbi, META);

	return true;

redirty_out:
	f2fs_cache_set_dirty(entry);
	return false;
}

void f2fs_write_meta_caches(struct f2fs_sb_info *sbi, long *nr_written)
{
	struct f2fs_lock_context lc;
	long nr_to_write = LONG_MAX;
	long nwritten;

	if (unlikely(is_sbi_flag_set(sbi, SBI_POR_DOING)))
		return;

	/* collect a number of dirty meta caches and write together */
	if (get_nr_caches(sbi, F2FS_DIRTY_META) <
	    nr_caches_to_skip(sbi, META))
		return;

	/* if locked failed, cp will flush dirty caches instead */
	if (!f2fs_down_write_trylock_trace(&sbi->cp_global_sem, &lc))
		return;

	nr_to_write = adjust_flush_cache_number(sbi, META);
	nwritten = f2fs_sync_meta_caches(sbi, nr_to_write, false, FS_META_IO);
	f2fs_up_write_trace(&sbi->cp_global_sem, &lc);

	if (nr_written)
		*nr_written = nwritten;
}

long f2fs_sync_meta_caches(struct f2fs_sb_info *sbi, long nr_to_write,
				bool sync, enum iostat_type io_type)
{
	pgoff_t index = 0, next = ULONG_MAX;
	struct f2fs_cached_block *entries[F2FS_ONSTACK_CACHES];
	long nwritten = 0;
	int nr;
	struct blk_plug plug;
	bool background = nr_to_write != LONG_MAX;

	trace_f2fs_write_caches(sbi, nr_to_write, 0, META);

	blk_start_plug(&plug);

	while ((nr = f2fs_cache_gang_lookup_tag(META_CACHE(sbi), entries,
			&index, F2FS_ONSTACK_CACHES, F2FS_CACHE_TAG_DIRTY))) {
		int i;

		for (i = 0; i < nr; i++) {
			struct f2fs_cached_block *entry = entries[i];

			if (background && unlikely(freezing(current))) {
				f2fs_cache_gang_release(entries, nr);
				goto stop;
			}

			if (background && next != ULONG_MAX &&
					entry->index != next) {
				f2fs_cache_gang_release(entries, nr);
				goto stop;
			}

			f2fs_lock_cache(entry);

			if (unlikely(!f2fs_is_meta_cache(entry))) {
continue_unlock:
				f2fs_unlock_cache(entry);
				continue;
			}
			if (!f2fs_cache_test_dirty(entry)) {
				/* someone wrote it for us */
				goto continue_unlock;
			}

			f2fs_cache_wait_writeback(entry);

			if (!f2fs_cache_test_and_clear_dirty(entry))
				goto continue_unlock;

			if (!__f2fs_write_meta_cache(entry, io_type)) {
				f2fs_unlock_cache(entry);
				f2fs_cache_gang_release(entries, nr);
				goto stop;
			}
			nwritten++;
			next = entry->index + 1;
			if (unlikely(nwritten >= nr_to_write))
				break;
		}
		f2fs_cache_gang_release(entries, nr);
		cond_resched();

		if (unlikely(nwritten >= nr_to_write))
			break;
	}
stop:
	if (nwritten)
		f2fs_submit_merged_write(sbi, META);

	blk_finish_plug(&plug);

	trace_f2fs_write_caches(sbi, nr_to_write, nwritten, META);

	return nwritten;
}

static void __add_ino_entry(struct f2fs_sb_info *sbi, nid_t ino,
						unsigned int devidx, int type)
{
	struct inode_management *im = &sbi->im[type];
	struct ino_entry *e = NULL, *new = NULL;
	int ret;

	if (type == FLUSH_INO) {
		rcu_read_lock();
		e = radix_tree_lookup(&im->ino_root, ino);
		rcu_read_unlock();
	}

retry:
	if (!e)
		new = f2fs_kmem_cache_alloc(ino_entry_slab,
						GFP_NOFS, true, NULL);

	ret = radix_tree_preload(GFP_NOFS | __GFP_NOFAIL);
	f2fs_bug_on(sbi, ret);

	spin_lock(&im->ino_lock);
	e = radix_tree_lookup(&im->ino_root, ino);
	if (!e) {
		if (!new) {
			spin_unlock(&im->ino_lock);
			radix_tree_preload_end();
			goto retry;
		}
		e = new;
		if (unlikely(radix_tree_insert(&im->ino_root, ino, e)))
			f2fs_bug_on(sbi, 1);

		memset(e, 0, sizeof(struct ino_entry));
		e->ino = ino;

		list_add_tail(&e->list, &im->ino_list);
		if (type != ORPHAN_INO)
			im->ino_num++;
	}

	if (type == FLUSH_INO)
		f2fs_set_bit(devidx, (char *)&e->dirty_device);

	spin_unlock(&im->ino_lock);
	radix_tree_preload_end();

	if (new && e != new)
		kmem_cache_free(ino_entry_slab, new);
}

static void __remove_ino_entry(struct f2fs_sb_info *sbi, nid_t ino, int type)
{
	struct inode_management *im = &sbi->im[type];
	struct ino_entry *e;

	spin_lock(&im->ino_lock);
	e = radix_tree_lookup(&im->ino_root, ino);
	if (e) {
		list_del(&e->list);
		radix_tree_delete(&im->ino_root, ino);
		im->ino_num--;
		spin_unlock(&im->ino_lock);
		kmem_cache_free(ino_entry_slab, e);
		return;
	}
	spin_unlock(&im->ino_lock);
}

static void __set_ino_bitmap(struct f2fs_sb_info *sbi, nid_t ino, int type)
{
	struct inode_management *im = &sbi->im[type];
	unsigned long index = INO_SLOT_INDEX(ino);
	unsigned int ofs = INO_BIT_OFFSET(ino);
	void **slot, *entry;
	unsigned long bitmap = 0;
	int ret;

	ret = radix_tree_preload(GFP_NOFS | __GFP_NOFAIL);
	f2fs_bug_on(sbi, ret);

	spin_lock(&im->ino_lock);
	slot = radix_tree_lookup_slot(&im->ino_root, index);
	if (slot) {
		entry = radix_tree_deref_slot_protected(slot, &im->ino_lock);
		bitmap = xa_to_value(entry);
		if (!(bitmap & (1UL << ofs))) {
			bitmap |= (1UL << ofs);
			entry = xa_mk_value(bitmap);
			radix_tree_replace_slot(&im->ino_root, slot, entry);
		}
	} else {
		bitmap |= (1UL << ofs);
		entry = xa_mk_value(bitmap);
		if (unlikely(radix_tree_insert(&im->ino_root, index, entry)))
			f2fs_bug_on(sbi, 1);
	}
	spin_unlock(&im->ino_lock);
	radix_tree_preload_end();
}

static void __clear_ino_bitmap(struct f2fs_sb_info *sbi, nid_t ino, int type)
{
	struct inode_management *im = &sbi->im[type];
	unsigned long index = INO_SLOT_INDEX(ino);
	unsigned int ofs = INO_BIT_OFFSET(ino);
	void **slot, *entry;
	unsigned long bitmap;

	spin_lock(&im->ino_lock);
	slot = radix_tree_lookup_slot(&im->ino_root, index);
	if (slot) {
		entry = radix_tree_deref_slot_protected(slot, &im->ino_lock);
		bitmap = xa_to_value(entry);
		if (bitmap & (1UL << ofs))
			bitmap &= ~(1UL << ofs);

		if (bitmap) {
			entry = xa_mk_value(bitmap);
			radix_tree_replace_slot(&im->ino_root, slot, entry);
		} else {
			radix_tree_delete(&im->ino_root, index);
		}
	}
	spin_unlock(&im->ino_lock);
}

static void __f2fs_add_ino_entry(struct f2fs_sb_info *sbi, nid_t ino,
					unsigned int devidx, int type)
{
	if (type <= FLUSH_INO)
		__add_ino_entry(sbi, ino, devidx, type);
	else
		__set_ino_bitmap(sbi, ino, type);
}

void f2fs_add_ino_entry(struct f2fs_sb_info *sbi, nid_t ino, int type)
{
	__f2fs_add_ino_entry(sbi, ino, 0, type);
}

void f2fs_remove_ino_entry(struct f2fs_sb_info *sbi, nid_t ino, int type)
{
	if (type <= FLUSH_INO)
		__remove_ino_entry(sbi, ino, type);
	else
		__clear_ino_bitmap(sbi, ino, type);
}

/* mode should be APPEND_INO, UPDATE_INO, TRANS_DIR_INO and XATTR_DIR_INO */
bool f2fs_exist_written_data(struct f2fs_sb_info *sbi, nid_t ino, int mode)
{
	struct inode_management *im = &sbi->im[mode];
	unsigned long index = INO_SLOT_INDEX(ino);
	unsigned int ofs = INO_BIT_OFFSET(ino);
	void *entry;
	unsigned long bitmap;

	f2fs_bug_on(sbi, mode <= FLUSH_INO);

	spin_lock(&im->ino_lock);
	entry = radix_tree_lookup(&im->ino_root, index);
	if (!entry) {
		spin_unlock(&im->ino_lock);
		return false;
	}
	bitmap = xa_to_value(entry);
	spin_unlock(&im->ino_lock);

	return bitmap & (1UL << ofs);
}

void f2fs_release_ino_entry(struct f2fs_sb_info *sbi, bool all)
{
	struct ino_entry *e, *tmp;
	int i;

	for (i = all ? ORPHAN_INO : FLUSH_INO; i <= FLUSH_INO; i++) {
		struct inode_management *im = &sbi->im[i];

		spin_lock(&im->ino_lock);
		list_for_each_entry_safe(e, tmp, &im->ino_list, list) {
			list_del(&e->list);
			radix_tree_delete(&im->ino_root, e->ino);
			kmem_cache_free(ino_entry_slab, e);
			im->ino_num--;
		}
		spin_unlock(&im->ino_lock);
	}

	/* Wait for pending APPEND/UPDATE inode state updates. */
	flush_workqueue(sbi->evict_wq);

	for (i = APPEND_INO; i < MAX_INO_ENTRY; i++) {
		struct inode_management *im = &sbi->im[i];

		spin_lock(&im->ino_lock);
		xa_destroy(&im->ino_root);
		spin_unlock(&im->ino_lock);
	}
}

void f2fs_set_dirty_device(struct f2fs_sb_info *sbi, nid_t ino,
					unsigned int devidx, int type)
{
	__f2fs_add_ino_entry(sbi, ino, devidx, type);
}

bool f2fs_is_dirty_device(struct f2fs_sb_info *sbi, nid_t ino,
					unsigned int devidx, int type)
{
	struct inode_management *im = &sbi->im[type];
	struct ino_entry *e;
	bool is_dirty = false;

	spin_lock(&im->ino_lock);
	e = radix_tree_lookup(&im->ino_root, ino);
	if (e && f2fs_test_bit(devidx, (char *)&e->dirty_device))
		is_dirty = true;
	spin_unlock(&im->ino_lock);
	return is_dirty;
}

int f2fs_acquire_orphan_inode(struct f2fs_sb_info *sbi)
{
	struct inode_management *im = &sbi->im[ORPHAN_INO];
	int err = 0;

	spin_lock(&im->ino_lock);

	if (time_to_inject(sbi, FAULT_ORPHAN)) {
		spin_unlock(&im->ino_lock);
		return -ENOSPC;
	}

	if (unlikely(im->ino_num >= sbi->max_orphans))
		err = -ENOSPC;
	else
		im->ino_num++;
	spin_unlock(&im->ino_lock);

	return err;
}

void f2fs_release_orphan_inode(struct f2fs_sb_info *sbi)
{
	struct inode_management *im = &sbi->im[ORPHAN_INO];

	spin_lock(&im->ino_lock);
	f2fs_bug_on(sbi, im->ino_num == 0);
	im->ino_num--;
	spin_unlock(&im->ino_lock);
}

void f2fs_add_orphan_inode(struct inode *inode)
{
	/* add new orphan ino entry into list */
	f2fs_add_ino_entry(F2FS_I_SB(inode), inode->i_ino, ORPHAN_INO);
	f2fs_update_inode_cache(inode);
}

void f2fs_remove_orphan_inode(struct f2fs_sb_info *sbi, nid_t ino)
{
	/* remove orphan entry from orphan list */
	f2fs_remove_ino_entry(sbi, ino, ORPHAN_INO);
}

static int recover_orphan_inode(struct f2fs_sb_info *sbi, nid_t ino)
{
	struct inode *inode;
	struct node_info ni;
	int err;

	inode = f2fs_iget_retry(sbi->sb, ino);
	if (IS_ERR(inode)) {
		/*
		 * there should be a bug that we can't find the entry
		 * to orphan inode.
		 */
		f2fs_bug_on(sbi, PTR_ERR(inode) == -ENOENT);
		return PTR_ERR(inode);
	}

	err = f2fs_dquot_initialize(inode);
	if (err) {
		iput(inode);
		goto err_out;
	}

	clear_nlink(inode);

	/* truncate all the data during iput */
	iput(inode);

	err = f2fs_get_node_info(sbi, ino, &ni, false);
	if (err)
		goto err_out;

	/* ENOMEM was fully retried in f2fs_evict_inode. */
	if (ni.blk_addr != NULL_ADDR) {
		err = -EIO;
		goto err_out;
	}
	return 0;

err_out:
	set_sbi_flag(sbi, SBI_NEED_FSCK);
	f2fs_warn(sbi, "%s: orphan failed (ino=%x), run fsck to fix.",
		  __func__, ino);
	return err;
}

int f2fs_recover_orphan_inodes(struct f2fs_sb_info *sbi)
{
	block_t start_blk, orphan_blocks, i, j;
	int err = 0;

	if (!is_set_ckpt_flags(sbi, CP_ORPHAN_PRESENT_FLAG))
		return 0;

	if (f2fs_hw_is_readonly(sbi)) {
		f2fs_info(sbi, "write access unavailable, skipping orphan cleanup");
		return 0;
	}

	if (is_sbi_flag_set(sbi, SBI_IS_WRITABLE))
		f2fs_info(sbi, "orphan cleanup on readonly fs");

	start_blk = __start_cp_addr(sbi) + 1 + __cp_payload(sbi);
	orphan_blocks = __start_sum_addr(sbi) - 1 - __cp_payload(sbi);

	f2fs_ra_meta_caches(sbi, start_blk, orphan_blocks, META_CP, true);

	for (i = 0; i < orphan_blocks; i++) {
		struct f2fs_cached_block *entry;
		struct f2fs_orphan_block *orphan_blk;
		struct f2fs_orphan_footer *footer;
		unsigned int entry_count;

		entry = f2fs_get_meta_cache(sbi, start_blk + i);
		if (IS_ERR(entry)) {
			err = PTR_ERR(entry);
			goto out;
		}

		orphan_blk = cache_address(entry);
		footer = f2fs_orphan_footer(orphan_blk, sbi);
		entry_count = le32_to_cpu(footer->entry_count);
		if (entry_count > F2FS_ORPHANS_PER_BLOCK(sbi)) {
			f2fs_err(sbi, "invalid orphan inode entry count %u",
				 entry_count);
			set_sbi_flag(sbi, SBI_NEED_FSCK);
			f2fs_handle_error(sbi, ERROR_INCONSISTENT_ORPHAN);
			err = -EFSCORRUPTED;
			f2fs_put_cache(entry, true);
			goto out;
		}

		for (j = 0; j < entry_count; j++) {
			nid_t ino = le32_to_cpu(orphan_blk->ino[j]);

			err = recover_orphan_inode(sbi, ino);
			if (err) {
				f2fs_put_cache(entry, true);
				goto out;
			}
		}
		f2fs_put_cache(entry, true);
	}
	/* clear Orphan Flag */
	clear_ckpt_flags(sbi, CP_ORPHAN_PRESENT_FLAG);
out:
	set_sbi_flag(sbi, SBI_IS_RECOVERED);

	return err;
}

static void write_orphan_inodes(struct f2fs_sb_info *sbi, block_t start_blk)
{
	struct list_head *head;
	struct f2fs_orphan_block *orphan_blk = NULL;
	struct f2fs_orphan_footer *footer = NULL;
	unsigned int nentries = 0;
	unsigned short index = 1;
	unsigned short orphan_blocks;
	struct ino_entry *orphan = NULL;
	struct inode_management *im = &sbi->im[ORPHAN_INO];
	struct f2fs_cached_block *entry = NULL;

	orphan_blocks = GET_ORPHAN_BLOCKS(sbi, im->ino_num);

	/*
	 * we don't need to do spin_lock(&im->ino_lock) here, since all the
	 * orphan inode operations are covered under f2fs_lock_op().
	 * And, spin_lock should be avoided due to cache operations below.
	 */
	head = &im->ino_list;

	/* loop for each orphan inode entry and write them in journal block */
	list_for_each_entry(orphan, head, list) {
		if (!entry) {
			entry = f2fs_grab_meta_cache(sbi, start_blk++);
			orphan_blk = cache_address(entry);
			footer = f2fs_orphan_footer(orphan_blk, sbi);
			memset(orphan_blk, 0, sbi->blocksize);
		}

		orphan_blk->ino[nentries++] = cpu_to_le32(orphan->ino);

		if (nentries == F2FS_ORPHANS_PER_BLOCK(sbi)) {
			/*
			 * an orphan block is full,
			 * then we need to flush current orphan blocks
			 * and bring another one in memory
			 */
			footer->blk_addr = cpu_to_le16(index);
			footer->blk_count = cpu_to_le16(orphan_blocks);
			footer->entry_count = cpu_to_le32(nentries);
			f2fs_mark_cache_dirty(entry);
			f2fs_put_cache(entry, true);
			index++;
			nentries = 0;
			entry = NULL;
		}
	}

	if (entry) {
		footer->blk_addr = cpu_to_le16(index);
		footer->blk_count = cpu_to_le16(orphan_blocks);
		footer->entry_count = cpu_to_le32(nentries);
		f2fs_mark_cache_dirty(entry);
		f2fs_put_cache(entry, true);
	}
}

static __u32 f2fs_checkpoint_chksum(struct f2fs_sb_info *sbi,
				     struct f2fs_checkpoint *ckpt)
{
	unsigned int chksum_ofs = le32_to_cpu(ckpt->checksum_offset);
	__u32 chksum;

	chksum = f2fs_crc32(ckpt, chksum_ofs);
	if (chksum_ofs < CP_CHKSUM_OFFSET(sbi)) {
		chksum_ofs += sizeof(chksum);
		chksum = f2fs_chksum(chksum, (__u8 *)ckpt + chksum_ofs,
					F2FS_BLKSIZE(sbi) - chksum_ofs);
	}
	return chksum;
}

static int get_checkpoint_version(struct f2fs_sb_info *sbi, block_t cp_addr,
		struct f2fs_checkpoint **cp_block, struct f2fs_cached_block **cp_entry,
		unsigned long long *version)
{
	size_t crc_offset = 0;
	__u32 crc;

	*cp_entry = f2fs_get_meta_cache(sbi, cp_addr);
	if (IS_ERR(*cp_entry))
		return PTR_ERR(*cp_entry);

	*cp_block = cache_address(*cp_entry);

	crc_offset = le32_to_cpu((*cp_block)->checksum_offset);
	if (crc_offset < CP_MIN_CHKSUM_OFFSET ||
			crc_offset > CP_CHKSUM_OFFSET(sbi)) {
		f2fs_put_cache(*cp_entry, true);
		f2fs_warn(sbi, "invalid crc_offset: %zu", crc_offset);
		return -EINVAL;
	}

	crc = f2fs_checkpoint_chksum(sbi, *cp_block);
	if (crc != cur_cp_crc(*cp_block)) {
		f2fs_put_cache(*cp_entry, true);
		f2fs_warn(sbi, "invalid crc value");
		return -EINVAL;
	}

	*version = cur_cp_version(*cp_block);
	return 0;
}

static struct f2fs_cached_block *validate_checkpoint(struct f2fs_sb_info *sbi,
				block_t cp_addr, unsigned long long *version)
{
	struct f2fs_cached_block *cp_entry_1 = NULL, *cp_entry_2 = NULL;
	struct f2fs_checkpoint *cp_block = NULL;
	unsigned long long cur_version = 0, pre_version = 0;
	unsigned int cp_blocks;
	int err;

	err = get_checkpoint_version(sbi, cp_addr, &cp_block,
					&cp_entry_1, version);
	if (err)
		return NULL;

	cp_blocks = le32_to_cpu(cp_block->cp_pack_total_block_count);

	if (cp_blocks > BLKS_PER_SEG(sbi) || cp_blocks <= F2FS_CP_PACKS) {
		f2fs_warn(sbi, "invalid cp_pack_total_block_count:%u",
			  le32_to_cpu(cp_block->cp_pack_total_block_count));
		goto invalid_cp;
	}
	pre_version = *version;

	cp_addr += cp_blocks - 1;
	err = get_checkpoint_version(sbi, cp_addr, &cp_block,
					&cp_entry_2, version);
	if (err)
		goto invalid_cp;
	cur_version = *version;

	if (cur_version == pre_version) {
		*version = cur_version;
		f2fs_put_cache(cp_entry_2, true);
		return cp_entry_1;
	}
	f2fs_put_cache(cp_entry_2, true);
invalid_cp:
	f2fs_put_cache(cp_entry_1, true);
	return NULL;
}

int f2fs_get_valid_checkpoint(struct f2fs_sb_info *sbi)
{
	struct f2fs_checkpoint *cp_block;
	struct f2fs_super_block *fsb = sbi->raw_super;
	struct f2fs_cached_block *cp1 = NULL, *cp2 = NULL, *cur_entry;
	unsigned long blk_size = sbi->blocksize;
	unsigned long long cp1_version = 0, cp2_version = 0;
	unsigned long long cp_start_blk_no;
	unsigned int cp_blks = 1 + __cp_payload(sbi);
	block_t cp_blk_no;
	int i;
	int err;

	sbi->ckpt = f2fs_kvzalloc(sbi, array_size(blk_size, cp_blks),
				  GFP_KERNEL);
	if (!sbi->ckpt)
		return -ENOMEM;
	/*
	 * Finding out valid cp block involves read both
	 * sets( cp pack 1 and cp pack 2)
	 */
	cp_start_blk_no = le32_to_cpu(fsb->cp_blkaddr);
	cp1 = validate_checkpoint(sbi, cp_start_blk_no, &cp1_version);

	/* The second checkpoint pack should start at the next segment */
	cp_start_blk_no += ((unsigned long long)1) <<
				le32_to_cpu(fsb->log_blocks_per_seg);
	cp2 = validate_checkpoint(sbi, cp_start_blk_no, &cp2_version);

	if (cp1 && cp2) {
		if (ver_after(cp2_version, cp1_version))
			cur_entry = cp2;
		else
			cur_entry = cp1;
	} else if (cp1) {
		cur_entry = cp1;
	} else if (cp2) {
		cur_entry = cp2;
	} else {
		err = -EFSCORRUPTED;
		goto fail_no_cp;
	}

	cp_block = cache_address(cur_entry);
	memcpy(sbi->ckpt, cp_block, blk_size);

	if (cur_entry == cp1)
		sbi->cur_cp_pack = 1;
	else
		sbi->cur_cp_pack = 2;

	/* Sanity checking of checkpoint */
	if (f2fs_sanity_check_ckpt(sbi)) {
		err = -EFSCORRUPTED;
		goto free_fail_no_cp;
	}

	if (cp_blks <= 1)
		goto done;

	cp_blk_no = le32_to_cpu(fsb->cp_blkaddr);
	if (cur_entry == cp2)
		cp_blk_no += BIT(le32_to_cpu(fsb->log_blocks_per_seg));

	for (i = 1; i < cp_blks; i++) {
		struct f2fs_cached_block *cur_entry_payload;
		void *sit_bitmap_ptr;
		unsigned char *ckpt = (unsigned char *)sbi->ckpt;

		cur_entry_payload = f2fs_get_meta_cache(sbi, cp_blk_no + i);
		if (IS_ERR(cur_entry_payload)) {
			err = PTR_ERR(cur_entry_payload);
			goto free_fail_no_cp;
		}
		sit_bitmap_ptr = cache_address(cur_entry_payload);
		memcpy(ckpt + i * blk_size, sit_bitmap_ptr, blk_size);
		f2fs_put_cache(cur_entry_payload, true);
	}
done:
	f2fs_put_cache(cp1, true);
	f2fs_put_cache(cp2, true);
	return 0;

free_fail_no_cp:
	f2fs_put_cache(cp1, true);
	f2fs_put_cache(cp2, true);
fail_no_cp:
	kvfree(sbi->ckpt);
	return err;
}

static void __add_dirty_inode(struct inode *inode, enum inode_type type)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	int flag = (type == DIR_INODE) ? FI_DIRTY_DIR : FI_DIRTY_FILE;

	if (is_inode_flag_set(inode, flag))
		return;

	set_inode_flag(inode, flag);
	list_add_tail(&F2FS_I(inode)->dirty_list, &sbi->inode_list[type]);
	stat_inc_dirty_inode(sbi, type);
}

static void __remove_dirty_inode(struct inode *inode, enum inode_type type)
{
	int flag = (type == DIR_INODE) ? FI_DIRTY_DIR : FI_DIRTY_FILE;

	if (get_dirty_pages(inode) || !is_inode_flag_set(inode, flag))
		return;

	list_del_init(&F2FS_I(inode)->dirty_list);
	clear_inode_flag(inode, flag);
	stat_dec_dirty_inode(F2FS_I_SB(inode), type);
}

void f2fs_update_dirty_folio(struct inode *inode, struct folio *folio)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	enum inode_type type = S_ISDIR(inode->i_mode) ? DIR_INODE : FILE_INODE;

	if (!S_ISDIR(inode->i_mode) && !S_ISREG(inode->i_mode) &&
			!S_ISLNK(inode->i_mode))
		return;

	spin_lock(&sbi->inode_lock[type]);
	if (type != FILE_INODE || test_opt(sbi, DATA_FLUSH))
		__add_dirty_inode(inode, type);
	inode_inc_dirty_pages(inode);
	spin_unlock(&sbi->inode_lock[type]);

	folio_set_f2fs_reference(folio);
}

void f2fs_remove_dirty_inode(struct inode *inode)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	enum inode_type type = S_ISDIR(inode->i_mode) ? DIR_INODE : FILE_INODE;

	if (!S_ISDIR(inode->i_mode) && !S_ISREG(inode->i_mode) &&
			!S_ISLNK(inode->i_mode))
		return;

	if (type == FILE_INODE && !test_opt(sbi, DATA_FLUSH))
		return;

	spin_lock(&sbi->inode_lock[type]);
	__remove_dirty_inode(inode, type);
	spin_unlock(&sbi->inode_lock[type]);
}

int f2fs_sync_dirty_inodes(struct f2fs_sb_info *sbi, enum inode_type type,
						bool from_cp)
{
	struct list_head *head;
	struct inode *inode;
	struct f2fs_inode_info *fi;
	bool is_dir = (type == DIR_INODE);
	unsigned long ino = 0;

	trace_f2fs_sync_dirty_inodes_enter(sbi->sb, is_dir,
				get_nr_caches(sbi, is_dir ?
				F2FS_DIRTY_DENTS : F2FS_DIRTY_DATA));
retry:
	if (unlikely(f2fs_cp_error(sbi))) {
		trace_f2fs_sync_dirty_inodes_exit(sbi->sb, is_dir,
				get_nr_caches(sbi, is_dir ?
				F2FS_DIRTY_DENTS : F2FS_DIRTY_DATA));
		return -EIO;
	}

	spin_lock(&sbi->inode_lock[type]);

	head = &sbi->inode_list[type];
	if (list_empty(head)) {
		spin_unlock(&sbi->inode_lock[type]);
		trace_f2fs_sync_dirty_inodes_exit(sbi->sb, is_dir,
				get_nr_caches(sbi, is_dir ?
				F2FS_DIRTY_DENTS : F2FS_DIRTY_DATA));
		return 0;
	}
	fi = list_first_entry(head, struct f2fs_inode_info, dirty_list);
	list_move_tail(&fi->dirty_list, head);
	inode = igrab(&fi->vfs_inode);
	spin_unlock(&sbi->inode_lock[type]);
	if (inode) {
		unsigned long cur_ino = inode->i_ino;

		if (from_cp)
			F2FS_I(inode)->cp_task = current;
		F2FS_I(inode)->wb_task = current;

		filemap_fdatawrite(inode->i_mapping);

		F2FS_I(inode)->wb_task = NULL;
		if (from_cp)
			F2FS_I(inode)->cp_task = NULL;

		iput(inode);
		/* We need to give cpu to another writers. */
		if (ino == cur_ino)
			cond_resched();
		else
			ino = cur_ino;
	} else {
		cond_resched();
	}
	goto retry;
}

static int f2fs_sync_inode_meta(struct f2fs_sb_info *sbi)
{
	struct list_head *head = &sbi->inode_list[DIRTY_META];
	struct inode *inode;
	struct f2fs_inode_info *fi;
	s64 total = get_nr_caches(sbi, F2FS_DIRTY_IMETA);

	while (total--) {
		if (unlikely(f2fs_cp_error(sbi)))
			return -EIO;

		spin_lock(&sbi->inode_lock[DIRTY_META]);
		if (list_empty(head)) {
			spin_unlock(&sbi->inode_lock[DIRTY_META]);
			return 0;
		}
		fi = list_first_entry(head, struct f2fs_inode_info,
							gdirty_list);
		list_move_tail(&fi->gdirty_list, head);
		inode = igrab(&fi->vfs_inode);
		spin_unlock(&sbi->inode_lock[DIRTY_META]);
		if (inode) {
			sync_inode_metadata(inode, 0);

			/* it's on eviction */
			if (is_inode_flag_set(inode, FI_DIRTY_INODE))
				f2fs_update_inode_cache(inode);
			iput(inode);
		} else {
			cond_resched();
		}
	}
	return 0;
}

static void __prepare_cp_block(struct f2fs_sb_info *sbi)
{
	struct f2fs_checkpoint *ckpt = F2FS_CKPT(sbi);
	struct f2fs_nm_info *nm_i = NM_I(sbi);
	nid_t last_nid = nm_i->next_scan_nid;

	next_free_nid(sbi, &last_nid);
	ckpt->valid_block_count = cpu_to_le64(valid_user_blocks(sbi));
	ckpt->valid_node_count = cpu_to_le32(valid_node_count(sbi));
	ckpt->valid_inode_count = cpu_to_le32(valid_inode_count(sbi));
	ckpt->next_free_nid = cpu_to_le32(last_nid);

	/* update user_block_counts */
	sbi->last_valid_block_count = sbi->total_valid_block_count;
	percpu_counter_set(&sbi->alloc_valid_block_count, 0);
	percpu_counter_set(&sbi->rf_node_block_count, 0);
}

static bool __need_flush_quota(struct f2fs_sb_info *sbi)
{
	bool ret = false;

	if (!is_journalled_quota(sbi))
		return false;

	if (!f2fs_down_write_trylock(&sbi->quota_sem))
		return true;
	if (is_sbi_flag_set(sbi, SBI_QUOTA_SKIP_FLUSH)) {
		ret = false;
	} else if (is_sbi_flag_set(sbi, SBI_QUOTA_NEED_REPAIR)) {
		ret = false;
	} else if (is_sbi_flag_set(sbi, SBI_QUOTA_NEED_FLUSH)) {
		clear_sbi_flag(sbi, SBI_QUOTA_NEED_FLUSH);
		ret = true;
	} else if (get_nr_caches(sbi, F2FS_DIRTY_QDATA)) {
		ret = true;
	}
	f2fs_up_write(&sbi->quota_sem);
	return ret;
}

/*
 * Freeze all the FS-operations for checkpoint.
 */
static int block_operations(struct f2fs_sb_info *sbi)
{
	int err = 0, cnt = 0;

	/*
	 * Let's flush inline_data in dirty node caches.
	 */
	f2fs_flush_inline_data(sbi);

retry_flush_quotas:
	f2fs_lock_all(sbi);
	if (__need_flush_quota(sbi)) {
		bool need_lock = sbi->umount_lock_holder != current;

		if (++cnt > DEFAULT_RETRY_QUOTA_FLUSH_COUNT) {
			set_sbi_flag(sbi, SBI_QUOTA_SKIP_FLUSH);
			set_sbi_flag(sbi, SBI_QUOTA_NEED_FLUSH);
			goto retry_flush_dents;
		}
		f2fs_unlock_all(sbi);

		/* don't grab s_umount lock during mount/umount/remount/freeze/quotactl */
		if (!need_lock) {
			f2fs_do_quota_sync(sbi->sb, -1);
		} else if (down_read_trylock(&sbi->sb->s_umount)) {
			f2fs_do_quota_sync(sbi->sb, -1);
			up_read(&sbi->sb->s_umount);
		}
		cond_resched();
		goto retry_flush_quotas;
	}

retry_flush_dents:
	/* write all the dirty dentry pages */
	if (get_nr_caches(sbi, F2FS_DIRTY_DENTS)) {
		f2fs_unlock_all(sbi);
		err = f2fs_sync_dirty_inodes(sbi, DIR_INODE, true);
		if (err)
			return err;
		cond_resched();
		goto retry_flush_quotas;
	}

	/*
	 * POR: we should ensure that there are no dirty node caches
	 * until finishing nat/sit flush. inode->i_blocks can be updated.
	 */
	f2fs_down_write(&sbi->node_change);

	if (get_nr_caches(sbi, F2FS_DIRTY_IMETA)) {
		f2fs_up_write(&sbi->node_change);
		f2fs_unlock_all(sbi);
		err = f2fs_sync_inode_meta(sbi);
		if (err)
			return err;
		cond_resched();
		goto retry_flush_quotas;
	}

retry_flush_nodes:
	f2fs_down_write(&sbi->node_write);

	if (get_nr_caches(sbi, F2FS_DIRTY_NODES)) {
		f2fs_up_write(&sbi->node_write);
		atomic_inc(&sbi->wb_sync_req[NODE]);
		err = f2fs_writeback_node_caches(sbi, LONG_MAX,
			true, false, FS_CP_NODE_IO, NULL);
		atomic_dec(&sbi->wb_sync_req[NODE]);
		if (err) {
			f2fs_up_write(&sbi->node_change);
			f2fs_unlock_all(sbi);
			return err;
		}
		cond_resched();
		goto retry_flush_nodes;
	}

	/*
	 * sbi->node_change is used only for AIO write_begin path which produces
	 * dirty node blocks and some checkpoint values by block allocation.
	 */
	__prepare_cp_block(sbi);
	f2fs_up_write(&sbi->node_change);
	return err;
}

static void unblock_operations(struct f2fs_sb_info *sbi)
{
	f2fs_up_write(&sbi->node_write);
	f2fs_unlock_all(sbi);
}

void f2fs_sync_dirty_data(struct f2fs_sb_info *sbi, int type)
{
	DEFINE_WAIT(wait);

	for (;;) {
		if (!get_nr_caches(sbi, type))
			break;

		if (unlikely(f2fs_cp_error(sbi) &&
			!is_sbi_flag_set(sbi, SBI_IS_CLOSE)))
			break;

		if (type == F2FS_DIRTY_META)
			f2fs_sync_meta_caches(sbi, LONG_MAX, true, FS_META_IO);
		else if (type == F2FS_WB_CP_DATA)
			f2fs_submit_merged_write(sbi, DATA);

		prepare_to_wait(&sbi->cp_wait, &wait, TASK_UNINTERRUPTIBLE);
		io_schedule_timeout(DEFAULT_SCHEDULE_TIMEOUT);
	}
	finish_wait(&sbi->cp_wait, &wait);
}

static void update_ckpt_flags(struct f2fs_sb_info *sbi, struct cp_control *cpc)
{
	unsigned long orphan_num = sbi->im[ORPHAN_INO].ino_num;
	struct f2fs_checkpoint *ckpt = F2FS_CKPT(sbi);
	unsigned long flags;

	spin_lock_irqsave(&sbi->cp_lock, flags);

	if ((cpc->reason & CP_UMOUNT) &&
			le32_to_cpu(ckpt->cp_pack_total_block_count) >
			sbi->blocks_per_seg - NM_I(sbi)->nat_bits_blocks)
		disable_nat_bits(sbi, false);

	if (cpc->reason & CP_TRIMMED)
		__set_ckpt_flags(ckpt, CP_TRIMMED_FLAG);
	else
		__clear_ckpt_flags(ckpt, CP_TRIMMED_FLAG);

	if (cpc->reason & CP_UMOUNT)
		__set_ckpt_flags(ckpt, CP_UMOUNT_FLAG);
	else
		__clear_ckpt_flags(ckpt, CP_UMOUNT_FLAG);

	if (cpc->reason & CP_FASTBOOT)
		__set_ckpt_flags(ckpt, CP_FASTBOOT_FLAG);
	else
		__clear_ckpt_flags(ckpt, CP_FASTBOOT_FLAG);

	if (orphan_num)
		__set_ckpt_flags(ckpt, CP_ORPHAN_PRESENT_FLAG);
	else
		__clear_ckpt_flags(ckpt, CP_ORPHAN_PRESENT_FLAG);

	if (is_sbi_flag_set(sbi, SBI_NEED_FSCK))
		__set_ckpt_flags(ckpt, CP_FSCK_FLAG);

	if (is_sbi_flag_set(sbi, SBI_IS_RESIZEFS))
		__set_ckpt_flags(ckpt, CP_RESIZEFS_FLAG);
	else
		__clear_ckpt_flags(ckpt, CP_RESIZEFS_FLAG);

	if (is_sbi_flag_set(sbi, SBI_CP_DISABLED))
		__set_ckpt_flags(ckpt, CP_DISABLED_FLAG);
	else
		__clear_ckpt_flags(ckpt, CP_DISABLED_FLAG);

	if (is_sbi_flag_set(sbi, SBI_CP_DISABLED_QUICK))
		__set_ckpt_flags(ckpt, CP_DISABLED_QUICK_FLAG);
	else
		__clear_ckpt_flags(ckpt, CP_DISABLED_QUICK_FLAG);

	if (is_sbi_flag_set(sbi, SBI_QUOTA_SKIP_FLUSH))
		__set_ckpt_flags(ckpt, CP_QUOTA_NEED_FSCK_FLAG);
	else
		__clear_ckpt_flags(ckpt, CP_QUOTA_NEED_FSCK_FLAG);

	if (is_sbi_flag_set(sbi, SBI_QUOTA_NEED_REPAIR))
		__set_ckpt_flags(ckpt, CP_QUOTA_NEED_FSCK_FLAG);

	/* set this flag to activate crc|cp_ver for recovery */
	__set_ckpt_flags(ckpt, CP_CRC_RECOVERY_FLAG);
	__clear_ckpt_flags(ckpt, CP_NOCRC_RECOVERY_FLAG);

	spin_unlock_irqrestore(&sbi->cp_lock, flags);
}

static void commit_checkpoint(struct f2fs_sb_info *sbi,
	void *src, block_t blk_addr)
{
	struct f2fs_cached_block *entry = f2fs_grab_meta_cache(sbi, blk_addr);

	memcpy(cache_address(entry), src, F2FS_BLKSIZE(sbi));

	f2fs_mark_cache_dirty(entry);
	if (unlikely(!f2fs_cache_test_and_clear_dirty(entry)))
		f2fs_bug_on(sbi, 1);

	/* writeout cp pack 2 cache */
	if (unlikely(!__f2fs_write_meta_cache(entry, FS_CP_META_IO))) {
		if (f2fs_cp_error(sbi)) {
			f2fs_put_cache(entry, true);
			return;
		}
		f2fs_bug_on(sbi, true);
	}

	f2fs_put_cache(entry, false);

	/* submit checkpoint (with barrier if NOBARRIER is not set) */
	f2fs_submit_merged_write(sbi, META_FLUSH);
}

static inline u64 get_sectors_written(struct block_device *bdev)
{
	return (u64)part_stat_read(bdev, sectors[STAT_WRITE]);
}

u64 f2fs_get_sectors_written(struct f2fs_sb_info *sbi)
{
	if (f2fs_is_multi_device(sbi)) {
		u64 sectors = 0;
		int i;

		for (i = 0; i < sbi->s_ndevs; i++)
			sectors += get_sectors_written(FDEV(i).bdev);

		return sectors;
	}

	return get_sectors_written(sbi->sb->s_bdev);
}

static inline void stat_cp_time(struct cp_control *cpc, enum cp_time type)
{
	cpc->stats.times[type] = ktime_get();
}

static inline void check_cp_time(struct f2fs_sb_info *sbi, struct cp_control *cpc)
{
	unsigned long long sb_diff, cur_diff;
	enum cp_time ct;

	sb_diff = (u64)ktime_ms_delta(sbi->cp_stats.times[CP_TIME_END],
					sbi->cp_stats.times[CP_TIME_START]);
	cur_diff = (u64)ktime_ms_delta(cpc->stats.times[CP_TIME_END],
					cpc->stats.times[CP_TIME_START]);

	if (cur_diff > sb_diff) {
		sbi->cp_stats = cpc->stats;
		if (cur_diff < CP_LONG_LATENCY_THRESHOLD)
			return;

		f2fs_warn(sbi, "checkpoint was blocked for %llu ms", cur_diff);
		for (ct = CP_TIME_START; ct < CP_TIME_MAX - 1; ct++)
			f2fs_warn(sbi, "Step#%d: %llu ms", ct,
				(u64)ktime_ms_delta(cpc->stats.times[ct + 1],
						cpc->stats.times[ct]));
	}
}

static int do_checkpoint(struct f2fs_sb_info *sbi, struct cp_control *cpc)
{
	struct f2fs_checkpoint *ckpt = F2FS_CKPT(sbi);
	struct f2fs_nm_info *nm_i = NM_I(sbi);
	unsigned long orphan_num = sbi->im[ORPHAN_INO].ino_num, flags;
	block_t start_blk;
	unsigned int data_sum_blocks, orphan_blocks;
	__u32 crc32 = 0;
	int i;
	int cp_payload_blks = __cp_payload(sbi);
	struct curseg_info *seg_i = CURSEG_I(sbi, CURSEG_HOT_NODE);
	u64 kbytes_written;
	int err;

	/* Flush all the NAT/SIT caches */
	f2fs_sync_meta_caches(sbi, LONG_MAX, true, FS_CP_META_IO);

	stat_cp_time(cpc, CP_TIME_SYNC_META);

	/* start to update checkpoint, cp ver is already updated previously */
	ckpt->elapsed_time = cpu_to_le64(get_mtime(sbi, true));
	ckpt->free_segment_count = cpu_to_le32(free_segments(sbi));
	for (i = 0; i < NR_CURSEG_NODE_TYPE; i++) {
		struct curseg_info *curseg = CURSEG_I(sbi, i + CURSEG_HOT_NODE);

		ckpt->cur_node_segno[i] = cpu_to_le32(curseg->segno);
		ckpt->cur_node_blkoff[i] = cpu_to_le16(curseg->next_blkoff);
		ckpt->alloc_type[i + CURSEG_HOT_NODE] = curseg->alloc_type;
	}
	for (i = 0; i < NR_CURSEG_DATA_TYPE; i++) {
		struct curseg_info *curseg = CURSEG_I(sbi, i + CURSEG_HOT_DATA);

		ckpt->cur_data_segno[i] = cpu_to_le32(curseg->segno);
		ckpt->cur_data_blkoff[i] = cpu_to_le16(curseg->next_blkoff);
		ckpt->alloc_type[i + CURSEG_HOT_DATA] = curseg->alloc_type;
	}

	/* 2 cp + n data seg summary + orphan inode blocks */
	data_sum_blocks = f2fs_nblocks_for_summary_flush(sbi, false);
	spin_lock_irqsave(&sbi->cp_lock, flags);
	if (data_sum_blocks < NR_CURSEG_DATA_TYPE)
		__set_ckpt_flags(ckpt, CP_COMPACT_SUM_FLAG);
	else
		__clear_ckpt_flags(ckpt, CP_COMPACT_SUM_FLAG);
	spin_unlock_irqrestore(&sbi->cp_lock, flags);

	orphan_blocks = GET_ORPHAN_BLOCKS(sbi, orphan_num);
	ckpt->cp_pack_start_sum = cpu_to_le32(1 + cp_payload_blks +
			orphan_blocks);

	if (__remain_node_summaries(cpc->reason))
		ckpt->cp_pack_total_block_count = cpu_to_le32(F2FS_CP_PACKS +
				cp_payload_blks + data_sum_blocks +
				orphan_blocks + NR_CURSEG_NODE_TYPE);
	else
		ckpt->cp_pack_total_block_count = cpu_to_le32(F2FS_CP_PACKS +
				cp_payload_blks + data_sum_blocks +
				orphan_blocks);

	/* update ckpt flag for checkpoint */
	update_ckpt_flags(sbi, cpc);

	/* update SIT/NAT bitmap */
	get_sit_bitmap(sbi, __bitmap_ptr(sbi, SIT_BITMAP));
	get_nat_bitmap(sbi, __bitmap_ptr(sbi, NAT_BITMAP));

	crc32 = f2fs_checkpoint_chksum(sbi, ckpt);
	*((__le32 *)((unsigned char *)ckpt +
				le32_to_cpu(ckpt->checksum_offset)))
				= cpu_to_le32(crc32);

	start_blk = __start_cp_next_addr(sbi);

	/* write nat bits */
	if (enabled_nat_bits(sbi, cpc)) {
		__u64 cp_ver = cur_cp_version(ckpt);
		block_t blk;

		cp_ver |= ((__u64)crc32 << 32);
		*(__le64 *)nm_i->nat_bits = cpu_to_le64(cp_ver);

		blk = start_blk + BLKS_PER_SEG(sbi) - nm_i->nat_bits_blocks;
		for (i = 0; i < nm_i->nat_bits_blocks; i++)
			f2fs_update_meta_block(sbi, nm_i->nat_bits +
					F2FS_BLK_TO_BYTES(sbi, i), blk + i);
	}

	/* write out checkpoint buffer at block 0 */
	f2fs_update_meta_block(sbi, ckpt, start_blk++);

	for (i = 1; i < 1 + cp_payload_blks; i++)
		f2fs_update_meta_block(sbi, (char *)ckpt +
				i * F2FS_BLKSIZE(sbi), start_blk++);

	if (orphan_num) {
		write_orphan_inodes(sbi, start_blk);
		start_blk += orphan_blocks;
	}

	f2fs_write_data_summaries(sbi, start_blk);
	start_blk += data_sum_blocks;

	/* Record write statistics in the hot node summary */
	kbytes_written = sbi->kbytes_written;
	kbytes_written += (f2fs_get_sectors_written(sbi) -
				sbi->sectors_written_start) >> 1;
	seg_i->journal->info.kbytes_written = cpu_to_le64(kbytes_written);

	if (__remain_node_summaries(cpc->reason)) {
		f2fs_write_node_summaries(sbi, start_blk);
		start_blk += NR_CURSEG_NODE_TYPE;
	}

	/* Here, we have one bio having CP pack except cp pack 2 blocks */
	f2fs_sync_meta_caches(sbi, LONG_MAX, true, FS_CP_META_IO);
	stat_cp_time(cpc, CP_TIME_SYNC_CP_META);

	/* Wait for all dirty meta blocks to be submitted for IO */
	f2fs_sync_dirty_data(sbi, F2FS_DIRTY_META);
	stat_cp_time(cpc, CP_TIME_WAIT_DIRTY_META);

	/* wait for previous submitted meta blocks writeback */
	f2fs_sync_dirty_data(sbi, F2FS_WB_CP_DATA);
	stat_cp_time(cpc, CP_TIME_WAIT_CP_DATA);

	/* flush all device cache */
	err = f2fs_flush_device_cache(sbi);
	if (err)
		return err;
	stat_cp_time(cpc, CP_TIME_FLUSH_DEVICE);

	/* barrier and flush checkpoint cp pack 2 blocks if it can */
	commit_checkpoint(sbi, ckpt, start_blk);
	f2fs_sync_dirty_data(sbi, F2FS_WB_CP_DATA);
	stat_cp_time(cpc, CP_TIME_WAIT_LAST_CP);

	/*
	 * invalidate intermediate meta cache used for migration of
	 * encrypted, verity or compressed inode's blocks.
	 */
	if (f2fs_sb_has_encrypt(sbi) || f2fs_sb_has_verity(sbi) ||
		f2fs_sb_has_compression(sbi)) {
		f2fs_truncate_meta_caches(sbi, MAIN_BLKADDR(sbi),
				MAX_BLKADDR(sbi) - MAIN_BLKADDR(sbi));
	}

	f2fs_release_ino_entry(sbi, false);

	f2fs_reset_fsync_node_info(sbi);

	clear_sbi_flag(sbi, SBI_IS_DIRTY);
	clear_sbi_flag(sbi, SBI_NEED_CP);
	clear_sbi_flag(sbi, SBI_QUOTA_SKIP_FLUSH);

	spin_lock(&sbi->stat_lock);
	sbi->unusable_block_count = 0;
	spin_unlock(&sbi->stat_lock);

	__set_cp_next_pack(sbi);

	/*
	 * redirty superblock if metadata like node caches or inode cache is
	 * updated during writing checkpoint.
	 */
	if (get_nr_caches(sbi, F2FS_DIRTY_NODES) ||
			get_nr_caches(sbi, F2FS_DIRTY_IMETA))
		set_sbi_flag(sbi, SBI_IS_DIRTY);

	f2fs_bug_on(sbi, get_nr_caches(sbi, F2FS_DIRTY_DENTS));

	return unlikely(f2fs_cp_error(sbi)) ? -EIO : 0;
}

int f2fs_write_checkpoint(struct f2fs_sb_info *sbi, struct cp_control *cpc)
{
	struct f2fs_checkpoint *ckpt = F2FS_CKPT(sbi);
	struct f2fs_lock_context lc;
	unsigned long long ckpt_ver;
	int err = 0;

	stat_cp_time(cpc, CP_TIME_START);

	if (f2fs_readonly(sbi->sb) || f2fs_hw_is_readonly(sbi))
		return -EROFS;

	if (unlikely(is_sbi_flag_set(sbi, SBI_CP_DISABLED))) {
		if (cpc->reason != CP_PAUSE)
			return 0;
		f2fs_warn(sbi, "Start checkpoint disabled!");
	}
	if (cpc->reason != CP_RESIZE)
		f2fs_down_write_trace(&sbi->cp_global_sem, &lc);

	stat_cp_time(cpc, CP_TIME_LOCK);

	if (!is_sbi_flag_set(sbi, SBI_IS_DIRTY) &&
		((cpc->reason & CP_FASTBOOT) || (cpc->reason & CP_SYNC) ||
		((cpc->reason & CP_DISCARD) && !sbi->discard_blks)))
		goto out;
	if (unlikely(f2fs_cp_error(sbi))) {
		err = -EIO;
		goto out;
	}

	trace_f2fs_write_checkpoint(sbi->sb, cpc->reason, CP_PHASE_START_BLOCK_OPS);

	err = block_operations(sbi);
	if (err)
		goto out;

	stat_cp_time(cpc, CP_TIME_OP_LOCK);

	trace_f2fs_write_checkpoint(sbi->sb, cpc->reason, CP_PHASE_FINISH_BLOCK_OPS);

	f2fs_flush_merged_writes(sbi);

	/* this is the case of multiple fstrims without any changes */
	if (cpc->reason & CP_DISCARD) {
		if (!f2fs_exist_trim_candidates(sbi, cpc)) {
			unblock_operations(sbi);
			goto out;
		}

		if (NM_I(sbi)->nat_cnt[DIRTY_NAT] == 0 &&
				SIT_I(sbi)->dirty_sentries == 0 &&
				prefree_segments(sbi) == 0) {
			f2fs_flush_sit_entries(sbi, cpc);
			f2fs_clear_prefree_segments(sbi, cpc);
			unblock_operations(sbi);
			goto out;
		}
	}
	stat_cp_time(cpc, CP_TIME_MERGE_WRITE);

	/*
	 * update checkpoint pack index
	 * Increase the version number so that
	 * SIT entries and seg summaries are written at correct place
	 */
	ckpt_ver = cur_cp_version(ckpt);
	ckpt->checkpoint_ver = cpu_to_le64(++ckpt_ver);

	/* write cached NAT/SIT entries to NAT/SIT area */
	err = f2fs_flush_nat_entries(sbi, cpc);
	if (err) {
		f2fs_err(sbi, "f2fs_flush_nat_entries failed err:%d, stop checkpoint", err);
		f2fs_bug_on(sbi, !f2fs_cp_error(sbi));
		goto stop;
	}
	stat_cp_time(cpc, CP_TIME_FLUSH_NAT);

	f2fs_flush_sit_entries(sbi, cpc);

	stat_cp_time(cpc, CP_TIME_FLUSH_SIT);

	/* save inmem log status */
	f2fs_save_inmem_curseg(sbi);

	err = do_checkpoint(sbi, cpc);
	if (err) {
		f2fs_err(sbi, "do_checkpoint failed err:%d, stop checkpoint", err);
		f2fs_bug_on(sbi, !f2fs_cp_error(sbi));
		f2fs_release_discard_addrs(sbi);
	} else {
		f2fs_clear_prefree_segments(sbi, cpc);
	}

	f2fs_restore_inmem_curseg(sbi);
	f2fs_reinit_atgc_curseg(sbi);
	stat_inc_cp_count(sbi);
stop:
	unblock_operations(sbi);
	stat_cp_time(cpc, CP_TIME_END);
	check_cp_time(sbi, cpc);

	if (cpc->reason & CP_RECOVERY)
		f2fs_notice(sbi, "checkpoint: version = %llx", ckpt_ver);

	/* update CP_TIME to trigger checkpoint periodically */
	f2fs_update_time(sbi, CP_TIME);
	trace_f2fs_write_checkpoint(sbi->sb, cpc->reason, CP_PHASE_FINISH_CHECKPOINT);
out:
	if (cpc->reason != CP_RESIZE)
		f2fs_up_write_trace(&sbi->cp_global_sem, &lc);
	return err;
}

void f2fs_init_ino_entry_info(struct f2fs_sb_info *sbi)
{
	int i;

	for (i = 0; i < MAX_INO_ENTRY; i++) {
		struct inode_management *im = &sbi->im[i];

		INIT_RADIX_TREE(&im->ino_root, GFP_ATOMIC);
		spin_lock_init(&im->ino_lock);
		INIT_LIST_HEAD(&im->ino_list);
		im->ino_num = 0;
	}

	sbi->max_orphans = (BLKS_PER_SEG(sbi) - F2FS_CP_PACKS -
			NR_CURSEG_PERSIST_TYPE - __cp_payload(sbi)) *
			F2FS_ORPHANS_PER_BLOCK(sbi);
}

int __init f2fs_create_checkpoint_caches(void)
{
	ino_entry_slab = f2fs_kmem_cache_create("f2fs_ino_entry",
			sizeof(struct ino_entry));
	if (!ino_entry_slab)
		return -ENOMEM;
	f2fs_inode_entry_slab = f2fs_kmem_cache_create("f2fs_inode_entry",
			sizeof(struct inode_entry));
	if (!f2fs_inode_entry_slab) {
		kmem_cache_destroy(ino_entry_slab);
		return -ENOMEM;
	}
	return 0;
}

void f2fs_destroy_checkpoint_caches(void)
{
	kmem_cache_destroy(ino_entry_slab);
	kmem_cache_destroy(f2fs_inode_entry_slab);
}

static int __write_checkpoint_sync(struct f2fs_sb_info *sbi)
{
	struct cp_control cpc = { .reason = CP_SYNC, };
	struct f2fs_lock_context lc;
	int err;

	f2fs_down_write_trace(&sbi->gc_lock, &lc);
	err = f2fs_write_checkpoint(sbi, &cpc);
	f2fs_up_write_trace(&sbi->gc_lock, &lc);

	return err;
}

static void __checkpoint_and_complete_reqs(struct f2fs_sb_info *sbi)
{
	struct ckpt_req_control *cprc = &sbi->cprc_info;
	struct ckpt_req *req, *next;
	struct llist_node *dispatch_list;
	u64 sum_diff = 0, diff, count = 0;
	int ret;

	dispatch_list = llist_del_all(&cprc->issue_list);
	if (!dispatch_list)
		return;
	dispatch_list = llist_reverse_order(dispatch_list);

	ret = __write_checkpoint_sync(sbi);
	atomic_inc(&cprc->issued_ckpt);

	llist_for_each_entry_safe(req, next, dispatch_list, llnode) {
		diff = (u64)ktime_ms_delta(ktime_get(), req->queue_time);
		req->ret = ret;
		req->delta_time = diff;
		complete(&req->wait);

		sum_diff += diff;
		count++;
	}
	atomic_sub(count, &cprc->queued_ckpt);
	atomic_add(count, &cprc->total_ckpt);

	spin_lock(&cprc->stat_lock);
	cprc->cur_time = (unsigned int)div64_u64(sum_diff, count);
	if (cprc->peak_time < cprc->cur_time)
		cprc->peak_time = cprc->cur_time;
	spin_unlock(&cprc->stat_lock);
}

static int issue_checkpoint_thread(void *data)
{
	struct f2fs_sb_info *sbi = data;
	struct ckpt_req_control *cprc = &sbi->cprc_info;
	wait_queue_head_t *q = &cprc->ckpt_wait_queue;
repeat:
	if (kthread_should_stop())
		return 0;

	if (!llist_empty(&cprc->issue_list))
		__checkpoint_and_complete_reqs(sbi);

	wait_event_interruptible(*q,
		kthread_should_stop() || !llist_empty(&cprc->issue_list));
	goto repeat;
}

static void flush_remained_ckpt_reqs(struct f2fs_sb_info *sbi,
		struct ckpt_req *wait_req)
{
	struct ckpt_req_control *cprc = &sbi->cprc_info;

	if (!llist_empty(&cprc->issue_list)) {
		__checkpoint_and_complete_reqs(sbi);
	} else {
		/* already dispatched by issue_checkpoint_thread */
		if (wait_req)
			wait_for_completion(&wait_req->wait);
	}
}

static void init_ckpt_req(struct ckpt_req *req)
{
	memset(req, 0, sizeof(struct ckpt_req));

	init_completion(&req->wait);
	req->queue_time = ktime_get();
}

int f2fs_issue_checkpoint(struct f2fs_sb_info *sbi)
{
	struct ckpt_req_control *cprc = &sbi->cprc_info;
	struct ckpt_req req;
	struct cp_control cpc;

	cpc.reason = __get_cp_reason(sbi);
	if (!test_opt(sbi, MERGE_CHECKPOINT) || cpc.reason != CP_SYNC ||
		sbi->umount_lock_holder == current) {
		struct f2fs_lock_context lc;
		int ret;

		f2fs_down_write_trace(&sbi->gc_lock, &lc);
		ret = f2fs_write_checkpoint(sbi, &cpc);
		f2fs_up_write_trace(&sbi->gc_lock, &lc);

		return ret;
	}

	if (!cprc->f2fs_issue_ckpt)
		return __write_checkpoint_sync(sbi);

	init_ckpt_req(&req);

	llist_add(&req.llnode, &cprc->issue_list);
	atomic_inc(&cprc->queued_ckpt);

	/*
	 * update issue_list before we wake up issue_checkpoint thread,
	 * this smp_mb() pairs with another barrier in ___wait_event(),
	 * see more details in comments of waitqueue_active().
	 */
	smp_mb();

	if (waitqueue_active(&cprc->ckpt_wait_queue))
		wake_up(&cprc->ckpt_wait_queue);

	if (cprc->f2fs_issue_ckpt)
		wait_for_completion(&req.wait);
	else
		flush_remained_ckpt_reqs(sbi, &req);

	if (unlikely(req.delta_time >= CP_LONG_LATENCY_THRESHOLD)) {
		f2fs_warn_ratelimited(sbi,
			"blocked on checkpoint for %u ms", cprc->peak_time);
		dump_stack();
	}

	return req.ret;
}

int f2fs_start_ckpt_thread(struct f2fs_sb_info *sbi)
{
	dev_t dev = sbi->sb->s_bdev->bd_dev;
	struct ckpt_req_control *cprc = &sbi->cprc_info;

	if (cprc->f2fs_issue_ckpt)
		return 0;

	cprc->f2fs_issue_ckpt = kthread_run(issue_checkpoint_thread, sbi,
			"f2fs_ckpt-%u:%u", MAJOR(dev), MINOR(dev));
	if (IS_ERR(cprc->f2fs_issue_ckpt)) {
		int err = PTR_ERR(cprc->f2fs_issue_ckpt);

		cprc->f2fs_issue_ckpt = NULL;
		return err;
	}

	set_task_ioprio(cprc->f2fs_issue_ckpt, cprc->ckpt_thread_ioprio);
	set_user_nice(cprc->f2fs_issue_ckpt,
			PRIO_TO_NICE(sbi->critical_task_priority));

	return 0;
}

void f2fs_stop_ckpt_thread(struct f2fs_sb_info *sbi)
{
	struct ckpt_req_control *cprc = &sbi->cprc_info;
	struct task_struct *ckpt_task;

	if (!cprc->f2fs_issue_ckpt)
		return;

	ckpt_task = cprc->f2fs_issue_ckpt;
	cprc->f2fs_issue_ckpt = NULL;
	kthread_stop(ckpt_task);

	f2fs_flush_ckpt_thread(sbi);
}

void f2fs_flush_ckpt_thread(struct f2fs_sb_info *sbi)
{
	struct ckpt_req_control *cprc = &sbi->cprc_info;

	flush_remained_ckpt_reqs(sbi, NULL);

	/* Let's wait for the previous dispatched checkpoint. */
	while (atomic_read(&cprc->queued_ckpt))
		io_schedule_timeout(DEFAULT_SCHEDULE_TIMEOUT);
}

void f2fs_init_ckpt_req_control(struct f2fs_sb_info *sbi)
{
	struct ckpt_req_control *cprc = &sbi->cprc_info;

	atomic_set(&cprc->issued_ckpt, 0);
	atomic_set(&cprc->total_ckpt, 0);
	atomic_set(&cprc->queued_ckpt, 0);
	cprc->ckpt_thread_ioprio = DEFAULT_CHECKPOINT_IOPRIO;
	init_waitqueue_head(&cprc->ckpt_wait_queue);
	init_llist_head(&cprc->issue_list);
	spin_lock_init(&cprc->stat_lock);
}
