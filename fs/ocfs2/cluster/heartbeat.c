/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * Copyright (C) 2004, 2005 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/bio.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <linux/file.h>
#include <linux/kthread.h>
#include <linux/configfs.h>
#include <linux/random.h>
#include <linux/crc32.h>
#include <linux/time.h>

#include "heartbeat.h"
#include "tcp.h"
#include "nodemanager.h"
#include "quorum.h"

#include "masklog.h"


/*
 * The first heartbeat pass had one global thread that would serialize all hb
 * callback calls.  This global serializing sem should only be removed once
 * we've made sure that all callees can deal with being called concurrently
 * from multiple hb region threads.
 */
static DECLARE_RWSEM(o2hb_callback_sem);

/*
 * multiple hb threads are watching multiple regions.  A node is live
 * whenever any of the threads sees activity from the node in its region.
 */
static spinlock_t o2hb_live_lock = SPIN_LOCK_UNLOCKED;
static struct list_head o2hb_live_slots[O2NM_MAX_NODES];
static unsigned long o2hb_live_node_bitmap[BITS_TO_LONGS(O2NM_MAX_NODES)];
static LIST_HEAD(o2hb_node_events);
static DECLARE_WAIT_QUEUE_HEAD(o2hb_steady_queue);

static LIST_HEAD(o2hb_all_regions);

static struct o2hb_callback {
	struct list_head list;
} o2hb_callbacks[O2HB_NUM_CB];

static struct o2hb_callback *hbcall_from_type(enum o2hb_callback_type type);

#define O2HB_DEFAULT_BLOCK_BITS       9

unsigned int o2hb_dead_threshold = O2HB_DEFAULT_DEAD_THRESHOLD;

/* Only sets a new threshold if there are no active regions. 
 *
 * No locking or otherwise interesting code is required for reading
 * o2hb_dead_threshold as it can't change once regions are active and
 * it's not interesting to anyone until then anyway. */
static void o2hb_dead_threshold_set(unsigned int threshold)
{
	if (threshold > O2HB_MIN_DEAD_THRESHOLD) {
		spin_lock(&o2hb_live_lock);
		if (list_empty(&o2hb_all_regions))
			o2hb_dead_threshold = threshold;
		spin_unlock(&o2hb_live_lock);
	}
}

struct o2hb_node_event {
	struct list_head        hn_item;
	enum o2hb_callback_type hn_event_type;
	struct o2nm_node        *hn_node;
	int                     hn_node_num;
};

struct o2hb_disk_slot {
	struct o2hb_disk_heartbeat_block *ds_raw_block;
	u8			ds_node_num;
	u64			ds_last_time;
	u64			ds_last_generation;
	u16			ds_equal_samples;
	u16			ds_changed_samples;
	struct list_head	ds_live_item;
};

/* each thread owns a region.. when we're asked to tear down the region
 * we ask the thread to stop, who cleans up the region */
struct o2hb_region {
	struct config_item	hr_item;

	struct list_head	hr_all_item;
	unsigned		hr_unclean_stop:1;

	/* protected by the hr_callback_sem */
	struct task_struct 	*hr_task;

	unsigned int		hr_blocks;
	unsigned long long	hr_start_block;

	unsigned int		hr_block_bits;
	unsigned int		hr_block_bytes;

	unsigned int		hr_slots_per_page;
	unsigned int		hr_num_pages;

	struct page             **hr_slot_data;
	struct block_device	*hr_bdev;
	struct o2hb_disk_slot	*hr_slots;

	/* let the person setting up hb wait for it to return until it
	 * has reached a 'steady' state.  This will be fixed when we have
	 * a more complete api that doesn't lead to this sort of fragility. */
	atomic_t		hr_steady_iterations;

	char			hr_dev_name[BDEVNAME_SIZE];

	unsigned int		hr_timeout_ms;

	/* randomized as the region goes up and down so that a node
	 * recognizes a node going up and down in one iteration */
	u64			hr_generation;

	struct work_struct	hr_write_timeout_work;
	unsigned long		hr_last_timeout_start;

	/* Used during o2hb_check_slot to hold a copy of the block
	 * being checked because we temporarily have to zero out the
	 * crc field. */
	struct o2hb_disk_heartbeat_block *hr_tmp_block;
};

struct o2hb_bio_wait_ctxt {
	atomic_t          wc_num_reqs;
	struct completion wc_io_complete;
	int               wc_error;
};

static void o2hb_write_timeout(void *arg)
{
	struct o2hb_region *reg = arg;

	mlog(ML_ERROR, "Heartbeat write timeout to device %s after %u "
	     "milliseconds\n", reg->hr_dev_name,
	     jiffies_to_msecs(jiffies - reg->hr_last_timeout_start)); 
	o2quo_disk_timeout();
}

static void o2hb_arm_write_timeout(struct o2hb_region *reg)
{
	mlog(0, "Queue write timeout for %u ms\n", O2HB_MAX_WRITE_TIMEOUT_MS);

	cancel_delayed_work(&reg->hr_write_timeout_work);
	reg->hr_last_timeout_start = jiffies;
	schedule_delayed_work(&reg->hr_write_timeout_work,
			      msecs_to_jiffies(O2HB_MAX_WRITE_TIMEOUT_MS));
}

static void o2hb_disarm_write_timeout(struct o2hb_region *reg)
{
	cancel_delayed_work(&reg->hr_write_timeout_work);
	flush_scheduled_work();
}

static inline void o2hb_bio_wait_init(struct o2hb_bio_wait_ctxt *wc,
				      unsigned int num_ios)
{
	atomic_set(&wc->wc_num_reqs, num_ios);
	init_completion(&wc->wc_io_complete);
	wc->wc_error = 0;
}

/* Used in error paths too */
static inline void o2hb_bio_wait_dec(struct o2hb_bio_wait_ctxt *wc,
				     unsigned int num)
{
	/* sadly atomic_sub_and_test() isn't available on all platforms.  The
	 * good news is that the fast path only completes one at a time */
	while(num--) {
		if (atomic_dec_and_test(&wc->wc_num_reqs)) {
			BUG_ON(num > 0);
			complete(&wc->wc_io_complete);
		}
	}
}

static void o2hb_wait_on_io(struct o2hb_region *reg,
			    struct o2hb_bio_wait_ctxt *wc)
{
	struct address_space *mapping = reg->hr_bdev->bd_inode->i_mapping;

	blk_run_address_space(mapping);

	wait_for_completion(&wc->wc_io_complete);
}

static int o2hb_bio_end_io(struct bio *bio,
			   unsigned int bytes_done,
			   int error)
{
	struct o2hb_bio_wait_ctxt *wc = bio->bi_private;

	if (error) {
		mlog(ML_ERROR, "IO Error %d\n", error);
		wc->wc_error = error;
	}

	if (bio->bi_size)
		return 1;

	o2hb_bio_wait_dec(wc, 1);
	return 0;
}

/* Setup a Bio to cover I/O against num_slots slots starting at
 * start_slot. */
static struct bio *o2hb_setup_one_bio(struct o2hb_region *reg,
				      struct o2hb_bio_wait_ctxt *wc,
				      unsigned int start_slot,
				      unsigned int num_slots)
{
	int i, nr_vecs, len, first_page, last_page;
	unsigned int vec_len, vec_start;
	unsigned int bits = reg->hr_block_bits;
	unsigned int spp = reg->hr_slots_per_page;
	struct bio *bio;
	struct page *page;

	nr_vecs = (num_slots + spp - 1) / spp;

	/* Testing has shown this allocation to take long enough under
	 * GFP_KERNEL that the local node can get fenced. It would be
	 * nicest if we could pre-allocate these bios and avoid this
	 * all together. */
	bio = bio_alloc(GFP_ATOMIC, nr_vecs);
	if (!bio) {
		mlog(ML_ERROR, "Could not alloc slots BIO!\n");
		bio = ERR_PTR(-ENOMEM);
		goto bail;
	}

	/* Must put everything in 512 byte sectors for the bio... */
	bio->bi_sector = (reg->hr_start_block + start_slot) << (bits - 9);
	bio->bi_bdev = reg->hr_bdev;
	bio->bi_private = wc;
	bio->bi_end_io = o2hb_bio_end_io;

	first_page = start_slot / spp;
	last_page = first_page + nr_vecs;
	vec_start = (start_slot << bits) % PAGE_CACHE_SIZE;
	for(i = first_page; i < last_page; i++) {
		page = reg->hr_slot_data[i];

		vec_len = PAGE_CACHE_SIZE;
		/* last page might be short */
		if (((i + 1) * spp) > (start_slot + num_slots))
			vec_len = ((num_slots + start_slot) % spp) << bits;
		vec_len -=  vec_start;

		mlog(ML_HB_BIO, "page %d, vec_len = %u, vec_start = %u\n",
		     i, vec_len, vec_start);

		len = bio_add_page(bio, page, vec_len, vec_start);
		if (len != vec_len) {
			bio_put(bio);
			bio = ERR_PTR(-EIO);

			mlog(ML_ERROR, "Error adding page to bio i = %d, "
			     "vec_len = %u, len = %d\n, start = %u\n",
			     i, vec_len, len, vec_start);
			goto bail;
		}

		vec_start = 0;
	}

bail:
	return bio;
}

/*
 * Compute the maximum number of sectors the bdev can handle in one bio,
 * as a power of two.
 *
 * Stolen from oracleasm, thanks Joel!
 */
static int compute_max_sectors(struct block_device *bdev)
{
	int max_pages, max_sectors, pow_two_sectors;

	struct request_queue *q;

	q = bdev_get_queue(bdev);
	max_pages = q->max_sectors >> (PAGE_SHIFT - 9);
	if (max_pages > BIO_MAX_PAGES)
		max_pages = BIO_MAX_PAGES;
	if (max_pages > q->max_phys_segments)
		max_pages = q->max_phys_segments;
	if (max_pages > q->max_hw_segments)
		max_pages = q->max_hw_segments;
	max_pages--; /* Handle I/Os that straddle a page */

	max_sectors = max_pages << (PAGE_SHIFT - 9);

	/* Why is fls() 1-based???? */
	pow_two_sectors = 1 << (fls(max_sectors) - 1);

	return pow_two_sectors;
}

static inline void o2hb_compute_request_limits(struct o2hb_region *reg,
					       unsigned int num_slots,
					       unsigned int *num_bios,
					       unsigned int *slots_per_bio)
{
	unsigned int max_sectors, io_sectors;

	max_sectors = compute_max_sectors(reg->hr_bdev);

	io_sectors = num_slots << (reg->hr_block_bits - 9);

	*num_bios = (io_sectors + max_sectors - 1) / max_sectors;
	*slots_per_bio = max_sectors >> (reg->hr_block_bits - 9);

	mlog(ML_HB_BIO, "My io size is %u sectors for %u slots. This "
	     "device can handle %u sectors of I/O\n", io_sectors, num_slots,
	     max_sectors);
	mlog(ML_HB_BIO, "Will need %u bios holding %u slots each\n",
	     *num_bios, *slots_per_bio);
}

static int o2hb_read_slots(struct o2hb_region *reg,
			   unsigned int max_slots)
{
	unsigned int num_bios, slots_per_bio, start_slot, num_slots;
	int i, status;
	struct o2hb_bio_wait_ctxt wc;
	struct bio **bios;
	struct bio *bio;

	o2hb_compute_request_limits(reg, max_slots, &num_bios, &slots_per_bio);

	bios = kcalloc(num_bios, sizeof(struct bio *), GFP_KERNEL);
	if (!bios) {
		status = -ENOMEM;
		mlog_errno(status);
		return status;
	}

	o2hb_bio_wait_init(&wc, num_bios);

	num_slots = slots_per_bio;
	for(i = 0; i < num_bios; i++) {
		start_slot = i * slots_per_bio;

		/* adjust num_slots at last bio */
		if (max_slots < (start_slot + num_slots))
			num_slots = max_slots - start_slot;

		bio = o2hb_setup_one_bio(reg, &wc, start_slot, num_slots);
		if (IS_ERR(bio)) {
			o2hb_bio_wait_dec(&wc, num_bios - i);

			status = PTR_ERR(bio);
			mlog_errno(status);
			goto bail_and_wait;
		}
		bios[i] = bio;

		submit_bio(READ, bio);
	}

	status = 0;

bail_and_wait:
	o2hb_wait_on_io(reg, &wc);
	if (wc.wc_error && !status)
		status = wc.wc_error;

	if (bios) {
		for(i = 0; i < num_bios; i++)
			if (bios[i])
				bio_put(bios[i]);
		kfree(bios);
	}

	return status;
}

static int o2hb_issue_node_write(struct o2hb_region *reg,
				 struct bio **write_bio,
				 struct o2hb_bio_wait_ctxt *write_wc)
{
	int status;
	unsigned int slot;
	struct bio *bio;

	o2hb_bio_wait_init(write_wc, 1);

	slot = o2nm_this_node();

	bio = o2hb_setup_one_bio(reg, write_wc, slot, 1);
	if (IS_ERR(bio)) {
		status = PTR_ERR(bio);
		mlog_errno(status);
		goto bail;
	}

	submit_bio(WRITE, bio);

	*write_bio = bio;
	status = 0;
bail:
	return status;
}

static u32 o2hb_compute_block_crc_le(struct o2hb_region *reg,
				     struct o2hb_disk_heartbeat_block *hb_block)
{
	__le32 old_cksum;
	u32 ret;

	/* We want to compute the block crc with a 0 value in the
	 * hb_cksum field. Save it off here and replace after the
	 * crc. */
	old_cksum = hb_block->hb_cksum;
	hb_block->hb_cksum = 0;

	ret = crc32_le(0, (unsigned char *) hb_block, reg->hr_block_bytes);

	hb_block->hb_cksum = old_cksum;

	return ret;
}

static void o2hb_dump_slot(struct o2hb_disk_heartbeat_block *hb_block)
{
	mlog(ML_ERROR, "Dump slot information: seq = 0x%llx, node = %u, "
	     "cksum = 0x%x, generation 0x%llx\n",
	     (long long)le64_to_cpu(hb_block->hb_seq),
	     hb_block->hb_node, le32_to_cpu(hb_block->hb_cksum),
	     (long long)le64_to_cpu(hb_block->hb_generation));
}

static int o2hb_verify_crc(struct o2hb_region *reg,
			   struct o2hb_disk_heartbeat_block *hb_block)
{
	u32 read, computed;

	read = le32_to_cpu(hb_block->hb_cksum);
	computed = o2hb_compute_block_crc_le(reg, hb_block);

	return read == computed;
}

/* We want to make sure that nobody is heartbeating on top of us --
 * this will help detect an invalid configuration. */
static int o2hb_check_last_timestamp(struct o2hb_region *reg)
{
	int node_num, ret;
	struct o2hb_disk_slot *slot;
	struct o2hb_disk_heartbeat_block *hb_block;

	node_num = o2nm_this_node();

	ret = 1;
	slot = &reg->hr_slots[node_num];
	/* Don't check on our 1st timestamp */
	if (slot->ds_last_time) {
		hb_block = slot->ds_raw_block;

		if (le64_to_cpu(hb_block->hb_seq) != slot->ds_last_time)
			ret = 0;
	}

	return ret;
}

static inline void o2hb_prepare_block(struct o2hb_region *reg,
				      u64 generation)
{
	int node_num;
	u64 cputime;
	struct o2hb_disk_slot *slot;
	struct o2hb_disk_heartbeat_block *hb_block;

	node_num = o2nm_this_node();
	slot = &reg->hr_slots[node_num];

	hb_block = (struct o2hb_disk_heartbeat_block *)slot->ds_raw_block;
	memset(hb_block, 0, reg->hr_block_bytes);
	/* TODO: time stuff */
	cputime = CURRENT_TIME.tv_sec;
	if (!cputime)
		cputime = 1;

	hb_block->hb_seq = cpu_to_le64(cputime);
	hb_block->hb_node = node_num;
	hb_block->hb_generation = cpu_to_le64(generation);

	/* This step must always happen last! */
	hb_block->hb_cksum = cpu_to_le32(o2hb_compute_block_crc_le(reg,
								   hb_block));

	mlog(ML_HB_BIO, "our node generation = 0x%llx, cksum = 0x%x\n",
	     (long long)cpu_to_le64(generation),
	     le32_to_cpu(hb_block->hb_cksum));
}

static void o2hb_fire_callbacks(struct o2hb_callback *hbcall,
				struct o2nm_node *node,
				int idx)
{
	struct list_head *iter;
	struct o2hb_callback_func *f;

	list_for_each(iter, &hbcall->list) {
		f = list_entry(iter, struct o2hb_callback_func, hc_item);
		mlog(ML_HEARTBEAT, "calling funcs %p\n", f);
		(f->hc_func)(node, idx, f->hc_data);
	}
}

/* Will run the list in order until we process the passed event */
static void o2hb_run_event_list(struct o2hb_node_event *queued_event)
{
	int empty;
	struct o2hb_callback *hbcall;
	struct o2hb_node_event *event;

	spin_lock(&o2hb_live_lock);
	empty = list_empty(&queued_event->hn_item);
	spin_unlock(&o2hb_live_lock);
	if (empty)
		return;

	/* Holding callback sem assures we don't alter the callback
	 * lists when doing this, and serializes ourselves with other
	 * processes wanting callbacks. */
	down_write(&o2hb_callback_sem);

	spin_lock(&o2hb_live_lock);
	while (!list_empty(&o2hb_node_events)
	       && !list_empty(&queued_event->hn_item)) {
		event = list_entry(o2hb_node_events.next,
				   struct o2hb_node_event,
				   hn_item);
		list_del_init(&event->hn_item);
		spin_unlock(&o2hb_live_lock);

		mlog(ML_HEARTBEAT, "Node %s event for %d\n",
		     event->hn_event_type == O2HB_NODE_UP_CB ? "UP" : "DOWN",
		     event->hn_node_num);

		hbcall = hbcall_from_type(event->hn_event_type);

		/* We should *never* have gotten on to the list with a
		 * bad type... This isn't something that we should try
		 * to recover from. */
		BUG_ON(IS_ERR(hbcall));

		o2hb_fire_callbacks(hbcall, event->hn_node, event->hn_node_num);

		spin_lock(&o2hb_live_lock);
	}
	spin_unlock(&o2hb_live_lock);

	up_write(&o2hb_callback_sem);
}

static void o2hb_queue_node_event(struct o2hb_node_event *event,
				  enum o2hb_callback_type type,
				  struct o2nm_node *node,
				  int node_num)
{
	assert_spin_locked(&o2hb_live_lock);

	event->hn_event_type = type;
	event->hn_node = node;
	event->hn_node_num = node_num;

	mlog(ML_HEARTBEAT, "Queue node %s event for node %d\n",
	     type == O2HB_NODE_UP_CB ? "UP" : "DOWN", node_num);

	list_add_tail(&event->hn_item, &o2hb_node_events);
}

static void o2hb_shutdown_slot(struct o2hb_disk_slot *slot)
{
	struct o2hb_node_event event =
		{ .hn_item = LIST_HEAD_INIT(event.hn_item), };
	struct o2nm_node *node;

	node = o2nm_get_node_by_num(slot->ds_node_num);
	if (!node)
		return;

	spin_lock(&o2hb_live_lock);
	if (!list_empty(&slot->ds_live_item)) {
		mlog(ML_HEARTBEAT, "Shutdown, node %d leaves region\n",
		     slot->ds_node_num);

		list_del_init(&slot->ds_live_item);

		if (list_empty(&o2hb_live_slots[slot->ds_node_num])) {
			clear_bit(slot->ds_node_num, o2hb_live_node_bitmap);

			o2hb_queue_node_event(&event, O2HB_NODE_DOWN_CB, node,
					      slot->ds_node_num);
		}
	}
	spin_unlock(&o2hb_live_lock);

	o2hb_run_event_list(&event);

	o2nm_node_put(node);
}

static int o2hb_check_slot(struct o2hb_region *reg,
			   struct o2hb_disk_slot *slot)
{
	int changed = 0, gen_changed = 0;
	struct o2hb_node_event event =
		{ .hn_item = LIST_HEAD_INIT(event.hn_item), };
	struct o2nm_node *node;
	struct o2hb_disk_heartbeat_block *hb_block = reg->hr_tmp_block;
	u64 cputime;

	memcpy(hb_block, slot->ds_raw_block, reg->hr_block_bytes);

	/* Is this correct? Do we assume that the node doesn't exist
	 * if we're not configured for him? */
	node = o2nm_get_node_by_num(slot->ds_node_num);
	if (!node)
		return 0;

	if (!o2hb_verify_crc(reg, hb_block)) {
		/* all paths from here will drop o2hb_live_lock for
		 * us. */
		spin_lock(&o2hb_live_lock);

		/* Don't print an error on the console in this case -
		 * a freshly formatted heartbeat area will not have a
		 * crc set on it. */
		if (list_empty(&slot->ds_live_item))
			goto out;

		/* The node is live but pushed out a bad crc. We
		 * consider it a transient miss but don't populate any
		 * other values as they may be junk. */
		mlog(ML_ERROR, "Node %d has written a bad crc to %s\n",
		     slot->ds_node_num, reg->hr_dev_name);
		o2hb_dump_slot(hb_block);

		slot->ds_equal_samples++;
		goto fire_callbacks;
	}

	/* we don't care if these wrap.. the state transitions below
	 * clear at the right places */
	cputime = le64_to_cpu(hb_block->hb_seq);
	if (slot->ds_last_time != cputime)
		slot->ds_changed_samples++;
	else
		slot->ds_equal_samples++;
	slot->ds_last_time = cputime;

	/* The node changed heartbeat generations. We assume this to
	 * mean it dropped off but came back before we timed out. We
	 * want to consider it down for the time being but don't want
	 * to lose any changed_samples state we might build up to
	 * considering it live again. */
	if (slot->ds_last_generation != le64_to_cpu(hb_block->hb_generation)) {
		gen_changed = 1;
		slot->ds_equal_samples = 0;
		mlog(ML_HEARTBEAT, "Node %d changed generation (0x%llx "
		     "to 0x%llx)\n", slot->ds_node_num,
		     (long long)slot->ds_last_generation,
		     (long long)le64_to_cpu(hb_block->hb_generation));
	}

	slot->ds_last_generation = le64_to_cpu(hb_block->hb_generation);

	mlog(ML_HEARTBEAT, "Slot %d gen 0x%llx cksum 0x%x "
	     "seq %llu last %llu changed %u equal %u\n",
	     slot->ds_node_num, (long long)slot->ds_last_generation,
	     le32_to_cpu(hb_block->hb_cksum),
	     (unsigned long long)le64_to_cpu(hb_block->hb_seq), 
	     (unsigned long long)slot->ds_last_time, slot->ds_changed_samples,
	     slot->ds_equal_samples);

	spin_lock(&o2hb_live_lock);

fire_callbacks:
	/* dead nodes only come to life after some number of
	 * changes at any time during their dead time */
	if (list_empty(&slot->ds_live_item) &&
	    slot->ds_changed_samples >= O2HB_LIVE_THRESHOLD) {
		mlog(ML_HEARTBEAT, "Node %d (id 0x%llx) joined my region\n",
		     slot->ds_node_num, (long long)slot->ds_last_generation);

		/* first on the list generates a callback */
		if (list_empty(&o2hb_live_slots[slot->ds_node_num])) {
			set_bit(slot->ds_node_num, o2hb_live_node_bitmap);

			o2hb_queue_node_event(&event, O2HB_NODE_UP_CB, node,
					      slot->ds_node_num);

			changed = 1;
		}

		list_add_tail(&slot->ds_live_item,
			      &o2hb_live_slots[slot->ds_node_num]);

		slot->ds_equal_samples = 0;
		goto out;
	}

	/* if the list is dead, we're done.. */
	if (list_empty(&slot->ds_live_item))
		goto out;

	/* live nodes only go dead after enough consequtive missed
	 * samples..  reset the missed counter whenever we see
	 * activity */
	if (slot->ds_equal_samples >= o2hb_dead_threshold || gen_changed) {
		mlog(ML_HEARTBEAT, "Node %d left my region\n",
		     slot->ds_node_num);

		/* last off the live_slot generates a callback */
		list_del_init(&slot->ds_live_item);
		if (list_empty(&o2hb_live_slots[slot->ds_node_num])) {
			clear_bit(slot->ds_node_num, o2hb_live_node_bitmap);

			o2hb_queue_node_event(&event, O2HB_NODE_DOWN_CB, node,
					      slot->ds_node_num);

			changed = 1;
		}

		/* We don't clear this because the node is still
		 * actually writing new blocks. */
		if (!gen_changed)
			slot->ds_changed_samples = 0;
		goto out;
	}
	if (slot->ds_changed_samples) {
		slot->ds_changed_samples = 0;
		slot->ds_equal_samples = 0;
	}
out:
	spin_unlock(&o2hb_live_lock);

	o2hb_run_event_list(&event);

	o2nm_node_put(node);
	return changed;
}

/* This could be faster if we just implmented a find_last_bit, but I
 * don't think the circumstances warrant it. */
static int o2hb_highest_node(unsigned long *nodes,
			     int numbits)
{
	int highest, node;

	highest = numbits;
	node = -1;
	while ((node = find_next_bit(nodes, numbits, node + 1)) != -1) {
		if (node >= numbits)
			break;

		highest = node;
	}

	return highest;
}

static int o2hb_do_disk_heartbeat(struct o2hb_region *reg)
{
	int i, ret, highest_node, change = 0;
	unsigned long configured_nodes[BITS_TO_LONGS(O2NM_MAX_NODES)];
	struct bio *write_bio;
	struct o2hb_bio_wait_ctxt write_wc;

	ret = o2nm_configured_node_map(configured_nodes,
				       sizeof(configured_nodes));
	if (ret) {
		mlog_errno(ret);
		return ret;
	}

	highest_node = o2hb_highest_node(configured_nodes, O2NM_MAX_NODES);
	if (highest_node >= O2NM_MAX_NODES) {
		mlog(ML_NOTICE, "ocfs2_heartbeat: no configured nodes found!\n");
		return -EINVAL;
	}

	/* No sense in reading the slots of nodes that don't exist
	 * yet. Of course, if the node definitions have holes in them
	 * then we're reading an empty slot anyway... Consider this
	 * best-effort. */
	ret = o2hb_read_slots(reg, highest_node + 1);
	if (ret < 0) {
		mlog_errno(ret);
		return ret;
	}

	/* With an up to date view of the slots, we can check that no
	 * other node has been improperly configured to heartbeat in
	 * our slot. */
	if (!o2hb_check_last_timestamp(reg))
		mlog(ML_ERROR, "Device \"%s\": another node is heartbeating "
		     "in our slot!\n", reg->hr_dev_name);

	/* fill in the proper info for our next heartbeat */
	o2hb_prepare_block(reg, reg->hr_generation);

	/* And fire off the write. Note that we don't wait on this I/O
	 * until later. */
	ret = o2hb_issue_node_write(reg, &write_bio, &write_wc);
	if (ret < 0) {
		mlog_errno(ret);
		return ret;
	}

	i = -1;
	while((i = find_next_bit(configured_nodes, O2NM_MAX_NODES, i + 1)) < O2NM_MAX_NODES) {

		change |= o2hb_check_slot(reg, &reg->hr_slots[i]);
	}

	/*
	 * We have to be sure we've advertised ourselves on disk
	 * before we can go to steady state.  This ensures that
	 * people we find in our steady state have seen us.
	 */
	o2hb_wait_on_io(reg, &write_wc);
	bio_put(write_bio);
	if (write_wc.wc_error) {
		/* Do not re-arm the write timeout on I/O error - we
		 * can't be sure that the new block ever made it to
		 * disk */
		mlog(ML_ERROR, "Write error %d on device \"%s\"\n",
		     write_wc.wc_error, reg->hr_dev_name);
		return write_wc.wc_error;
	}

	o2hb_arm_write_timeout(reg);

	/* let the person who launched us know when things are steady */
	if (!change && (atomic_read(&reg->hr_steady_iterations) != 0)) {
		if (atomic_dec_and_test(&reg->hr_steady_iterations))
			wake_up(&o2hb_steady_queue);
	}

	return 0;
}

/* Subtract b from a, storing the result in a. a *must* have a larger
 * value than b. */
static void o2hb_tv_subtract(struct timeval *a,
			     struct timeval *b)
{
	/* just return 0 when a is after b */
	if (a->tv_sec < b->tv_sec ||
	    (a->tv_sec == b->tv_sec && a->tv_usec < b->tv_usec)) {
		a->tv_sec = 0;
		a->tv_usec = 0;
		return;
	}

	a->tv_sec -= b->tv_sec;
	a->tv_usec -= b->tv_usec;
	while ( a->tv_usec < 0 ) {
		a->tv_sec--;
		a->tv_usec += 1000000;
	}
}

static unsigned int o2hb_elapsed_msecs(struct timeval *start,
				       struct timeval *end)
{
	struct timeval res = *end;

	o2hb_tv_subtract(&res, start);

	return res.tv_sec * 1000 + res.tv_usec / 1000;
}

/*
 * we ride the region ref that the region dir holds.  before the region
 * dir is removed and drops it ref it will wait to tear down this
 * thread.
 */
static int o2hb_thread(void *data)
{
	int i, ret;
	struct o2hb_region *reg = data;
	struct bio *write_bio;
	struct o2hb_bio_wait_ctxt write_wc;
	struct timeval before_hb, after_hb;
	unsigned int elapsed_msec;

	mlog(ML_HEARTBEAT|ML_KTHREAD, "hb thread running\n");

	set_user_nice(current, -20);

	while (!kthread_should_stop() && !reg->hr_unclean_stop) {
		/* We track the time spent inside
		 * o2hb_do_disk_heartbeat so that we avoid more then
		 * hr_timeout_ms between disk writes. On busy systems
		 * this should result in a heartbeat which is less
		 * likely to time itself out. */
		do_gettimeofday(&before_hb);

		i = 0;
		do {
			ret = o2hb_do_disk_heartbeat(reg);
		} while (ret && ++i < 2);

		do_gettimeofday(&after_hb);
		elapsed_msec = o2hb_elapsed_msecs(&before_hb, &after_hb);

		mlog(0, "start = %lu.%lu, end = %lu.%lu, msec = %u\n",
		     before_hb.tv_sec, (unsigned long) before_hb.tv_usec,
		     after_hb.tv_sec, (unsigned long) after_hb.tv_usec,
		     elapsed_msec);

		if (elapsed_msec < reg->hr_timeout_ms) {
			/* the kthread api has blocked signals for us so no
			 * need to record the return value. */
			msleep_interruptible(reg->hr_timeout_ms - elapsed_msec);
		}
	}

	o2hb_disarm_write_timeout(reg);

	/* unclean stop is only used in very bad situation */
	for(i = 0; !reg->hr_unclean_stop && i < reg->hr_blocks; i++)
		o2hb_shutdown_slot(&reg->hr_slots[i]);

	/* Explicit down notification - avoid forcing the other nodes
	 * to timeout on this region when we could just as easily
	 * write a clear generation - thus indicating to them that
	 * this node has left this region.
	 *
	 * XXX: Should we skip this on unclean_stop? */
	o2hb_prepare_block(reg, 0);
	ret = o2hb_issue_node_write(reg, &write_bio, &write_wc);
	if (ret == 0) {
		o2hb_wait_on_io(reg, &write_wc);
		bio_put(write_bio);
	} else {
		mlog_errno(ret);
	}

	mlog(ML_HEARTBEAT|ML_KTHREAD, "hb thread exiting\n");

	return 0;
}

void o2hb_init(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(o2hb_callbacks); i++)
		INIT_LIST_HEAD(&o2hb_callbacks[i].list);

	for (i = 0; i < ARRAY_SIZE(o2hb_live_slots); i++)
		INIT_LIST_HEAD(&o2hb_live_slots[i]);

	INIT_LIST_HEAD(&o2hb_node_events);

	memset(o2hb_live_node_bitmap, 0, sizeof(o2hb_live_node_bitmap));
}

/* if we're already in a callback then we're already serialized by the sem */
static void o2hb_fill_node_map_from_callback(unsigned long *map,
					     unsigned bytes)
{
	BUG_ON(bytes < (BITS_TO_LONGS(O2NM_MAX_NODES) * sizeof(unsigned long)));

	memcpy(map, &o2hb_live_node_bitmap, bytes);
}

/*
 * get a map of all nodes that are heartbeating in any regions
 */
void o2hb_fill_node_map(unsigned long *map, unsigned bytes)
{
	/* callers want to serialize this map and callbacks so that they
	 * can trust that they don't miss nodes coming to the party */
	down_read(&o2hb_callback_sem);
	spin_lock(&o2hb_live_lock);
	o2hb_fill_node_map_from_callback(map, bytes);
	spin_unlock(&o2hb_live_lock);
	up_read(&o2hb_callback_sem);
}
EXPORT_SYMBOL_GPL(o2hb_fill_node_map);

/*
 * heartbeat configfs bits.  The heartbeat set is a default set under
 * the cluster set in nodemanager.c.
 */

static struct o2hb_region *to_o2hb_region(struct config_item *item)
{
	return item ? container_of(item, struct o2hb_region, hr_item) : NULL;
}

/* drop_item only drops its ref after killing the thread, nothing should
 * be using the region anymore.  this has to clean up any state that
 * attributes might have built up. */
static void o2hb_region_release(struct config_item *item)
{
	int i;
	struct page *page;
	struct o2hb_region *reg = to_o2hb_region(item);

	if (reg->hr_tmp_block)
		kfree(reg->hr_tmp_block);

	if (reg->hr_slot_data) {
		for (i = 0; i < reg->hr_num_pages; i++) {
			page = reg->hr_slot_data[i];
			if (page)
				__free_page(page);
		}
		kfree(reg->hr_slot_data);
	}

	if (reg->hr_bdev)
		blkdev_put(reg->hr_bdev);

	if (reg->hr_slots)
		kfree(reg->hr_slots);

	spin_lock(&o2hb_live_lock);
	list_del(&reg->hr_all_item);
	spin_unlock(&o2hb_live_lock);

	kfree(reg);
}

static int o2hb_read_block_input(struct o2hb_region *reg,
				 const char *page,
				 size_t count,
				 unsigned long *ret_bytes,
				 unsigned int *ret_bits)
{
	unsigned long bytes;
	char *p = (char *)page;

	bytes = simple_strtoul(p, &p, 0);
	if (!p || (*p && (*p != '\n')))
		return -EINVAL;

	/* Heartbeat and fs min / max block sizes are the same. */
	if (bytes > 4096 || bytes < 512)
		return -ERANGE;
	if (hweight16(bytes) != 1)
		return -EINVAL;

	if (ret_bytes)
		*ret_bytes = bytes;
	if (ret_bits)
		*ret_bits = ffs(bytes) - 1;

	return 0;
}

static ssize_t o2hb_region_block_bytes_read(struct o2hb_region *reg,
					    char *page)
{
	return sprintf(page, "%u\n", reg->hr_block_bytes);
}

static ssize_t o2hb_region_block_bytes_write(struct o2hb_region *reg,
					     const char *page,
					     size_t count)
{
	int status;
	unsigned long block_bytes;
	unsigned int block_bits;

	if (reg->hr_bdev)
		return -EINVAL;

	status = o2hb_read_block_input(reg, page, count,
				       &block_bytes, &block_bits);
	if (status)
		return status;

	reg->hr_block_bytes = (unsigned int)block_bytes;
	reg->hr_block_bits = block_bits;

	return count;
}

static ssize_t o2hb_region_start_block_read(struct o2hb_region *reg,
					    char *page)
{
	return sprintf(page, "%llu\n", reg->hr_start_block);
}

static ssize_t o2hb_region_start_block_write(struct o2hb_region *reg,
					     const char *page,
					     size_t count)
{
	unsigned long long tmp;
	char *p = (char *)page;

	if (reg->hr_bdev)
		return -EINVAL;

	tmp = simple_strtoull(p, &p, 0);
	if (!p || (*p && (*p != '\n')))
		return -EINVAL;

	reg->hr_start_block = tmp;

	return count;
}

static ssize_t o2hb_region_blocks_read(struct o2hb_region *reg,
				       char *page)
{
	return sprintf(page, "%d\n", reg->hr_blocks);
}

static ssize_t o2hb_region_blocks_write(struct o2hb_region *reg,
					const char *page,
					size_t count)
{
	unsigned long tmp;
	char *p = (char *)page;

	if (reg->hr_bdev)
		return -EINVAL;

	tmp = simple_strtoul(p, &p, 0);
	if (!p || (*p && (*p != '\n')))
		return -EINVAL;

	if (tmp > O2NM_MAX_NODES || tmp == 0)
		return -ERANGE;

	reg->hr_blocks = (unsigned int)tmp;

	return count;
}

static ssize_t o2hb_region_dev_read(struct o2hb_region *reg,
				    char *page)
{
	unsigned int ret = 0;

	if (reg->hr_bdev)
		ret = sprintf(page, "%s\n", reg->hr_dev_name);

	return ret;
}

static void o2hb_init_region_params(struct o2hb_region *reg)
{
	reg->hr_slots_per_page = PAGE_CACHE_SIZE >> reg->hr_block_bits;
	reg->hr_timeout_ms = O2HB_REGION_TIMEOUT_MS;

	mlog(ML_HEARTBEAT, "hr_start_block = %llu, hr_blocks = %u\n",
	     reg->hr_start_block, reg->hr_blocks);
	mlog(ML_HEARTBEAT, "hr_block_bytes = %u, hr_block_bits = %u\n",
	     reg->hr_block_bytes, reg->hr_block_bits);
	mlog(ML_HEARTBEAT, "hr_timeout_ms = %u\n", reg->hr_timeout_ms);
	mlog(ML_HEARTBEAT, "dead threshold = %u\n", o2hb_dead_threshold);
}

static int o2hb_map_slot_data(struct o2hb_region *reg)
{
	int i, j;
	unsigned int last_slot;
	unsigned int spp = reg->hr_slots_per_page;
	struct page *page;
	char *raw;
	struct o2hb_disk_slot *slot;

	reg->hr_tmp_block = kmalloc(reg->hr_block_bytes, GFP_KERNEL);
	if (reg->hr_tmp_block == NULL) {
		mlog_errno(-ENOMEM);
		return -ENOMEM;
	}

	reg->hr_slots = kcalloc(reg->hr_blocks,
				sizeof(struct o2hb_disk_slot), GFP_KERNEL);
	if (reg->hr_slots == NULL) {
		mlog_errno(-ENOMEM);
		return -ENOMEM;
	}

	for(i = 0; i < reg->hr_blocks; i++) {
		slot = &reg->hr_slots[i];
		slot->ds_node_num = i;
		INIT_LIST_HEAD(&slot->ds_live_item);
		slot->ds_raw_block = NULL;
	}

	reg->hr_num_pages = (reg->hr_blocks + spp - 1) / spp;
	mlog(ML_HEARTBEAT, "Going to require %u pages to cover %u blocks "
			   "at %u blocks per page\n",
	     reg->hr_num_pages, reg->hr_blocks, spp);

	reg->hr_slot_data = kcalloc(reg->hr_num_pages, sizeof(struct page *),
				    GFP_KERNEL);
	if (!reg->hr_slot_data) {
		mlog_errno(-ENOMEM);
		return -ENOMEM;
	}

	for(i = 0; i < reg->hr_num_pages; i++) {
		page = alloc_page(GFP_KERNEL);
		if (!page) {
			mlog_errno(-ENOMEM);
			return -ENOMEM;
		}

		reg->hr_slot_data[i] = page;

		last_slot = i * spp;
		raw = page_address(page);
		for (j = 0;
		     (j < spp) && ((j + last_slot) < reg->hr_blocks);
		     j++) {
			BUG_ON((j + last_slot) >= reg->hr_blocks);

			slot = &reg->hr_slots[j + last_slot];
			slot->ds_raw_block =
				(struct o2hb_disk_heartbeat_block *) raw;

			raw += reg->hr_block_bytes;
		}
	}

	return 0;
}

/* Read in all the slots available and populate the tracking
 * structures so that we can start with a baseline idea of what's
 * there. */
static int o2hb_populate_slot_data(struct o2hb_region *reg)
{
	int ret, i;
	struct o2hb_disk_slot *slot;
	struct o2hb_disk_heartbeat_block *hb_block;

	mlog_entry_void();

	ret = o2hb_read_slots(reg, reg->hr_blocks);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	/* We only want to get an idea of the values initially in each
	 * slot, so we do no verification - o2hb_check_slot will
	 * actually determine if each configured slot is valid and
	 * whether any values have changed. */
	for(i = 0; i < reg->hr_blocks; i++) {
		slot = &reg->hr_slots[i];
		hb_block = (struct o2hb_disk_heartbeat_block *) slot->ds_raw_block;

		/* Only fill the values that o2hb_check_slot uses to
		 * determine changing slots */
		slot->ds_last_time = le64_to_cpu(hb_block->hb_seq);
		slot->ds_last_generation = le64_to_cpu(hb_block->hb_generation);
	}

out:
	mlog_exit(ret);
	return ret;
}

/* this is acting as commit; we set up all of hr_bdev and hr_task or nothing */
static ssize_t o2hb_region_dev_write(struct o2hb_region *reg,
				     const char *page,
				     size_t count)
{
	long fd;
	int sectsize;
	char *p = (char *)page;
	struct file *filp = NULL;
	struct inode *inode = NULL;
	ssize_t ret = -EINVAL;

	if (reg->hr_bdev)
		goto out;

	/* We can't heartbeat without having had our node number
	 * configured yet. */
	if (o2nm_this_node() == O2NM_MAX_NODES)
		goto out;

	fd = simple_strtol(p, &p, 0);
	if (!p || (*p && (*p != '\n')))
		goto out;

	if (fd < 0 || fd >= INT_MAX)
		goto out;

	filp = fget(fd);
	if (filp == NULL)
		goto out;

	if (reg->hr_blocks == 0 || reg->hr_start_block == 0 ||
	    reg->hr_block_bytes == 0)
		goto out;

	inode = igrab(filp->f_mapping->host);
	if (inode == NULL)
		goto out;

	if (!S_ISBLK(inode->i_mode))
		goto out;

	reg->hr_bdev = I_BDEV(filp->f_mapping->host);
	ret = blkdev_get(reg->hr_bdev, FMODE_WRITE | FMODE_READ, 0);
	if (ret) {
		reg->hr_bdev = NULL;
		goto out;
	}
	inode = NULL;

	bdevname(reg->hr_bdev, reg->hr_dev_name);

	sectsize = bdev_hardsect_size(reg->hr_bdev);
	if (sectsize != reg->hr_block_bytes) {
		mlog(ML_ERROR,
		     "blocksize %u incorrect for device, expected %d",
		     reg->hr_block_bytes, sectsize);
		ret = -EINVAL;
		goto out;
	}

	o2hb_init_region_params(reg);

	/* Generation of zero is invalid */
	do {
		get_random_bytes(&reg->hr_generation,
				 sizeof(reg->hr_generation));
	} while (reg->hr_generation == 0);

	ret = o2hb_map_slot_data(reg);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	ret = o2hb_populate_slot_data(reg);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	INIT_WORK(&reg->hr_write_timeout_work, o2hb_write_timeout, reg);

	/*
	 * A node is considered live after it has beat LIVE_THRESHOLD
	 * times.  We're not steady until we've given them a chance
	 * _after_ our first read.
	 */
	atomic_set(&reg->hr_steady_iterations, O2HB_LIVE_THRESHOLD + 1);

	reg->hr_task = kthread_run(o2hb_thread, reg, "o2hb-%s",
				   reg->hr_item.ci_name);
	if (IS_ERR(reg->hr_task)) {
		ret = PTR_ERR(reg->hr_task);
		mlog_errno(ret);
		reg->hr_task = NULL;
		goto out;
	}

	ret = wait_event_interruptible(o2hb_steady_queue,
				atomic_read(&reg->hr_steady_iterations) == 0);
	if (ret) {
		kthread_stop(reg->hr_task);
		reg->hr_task = NULL;
		goto out;
	}

	ret = count;
out:
	if (filp)
		fput(filp);
	if (inode)
		iput(inode);
	if (ret < 0) {
		if (reg->hr_bdev) {
			blkdev_put(reg->hr_bdev);
			reg->hr_bdev = NULL;
		}
	}
	return ret;
}

struct o2hb_region_attribute {
	struct configfs_attribute attr;
	ssize_t (*show)(struct o2hb_region *, char *);
	ssize_t (*store)(struct o2hb_region *, const char *, size_t);
};

static struct o2hb_region_attribute o2hb_region_attr_block_bytes = {
	.attr	= { .ca_owner = THIS_MODULE,
		    .ca_name = "block_bytes",
		    .ca_mode = S_IRUGO | S_IWUSR },
	.show	= o2hb_region_block_bytes_read,
	.store	= o2hb_region_block_bytes_write,
};

static struct o2hb_region_attribute o2hb_region_attr_start_block = {
	.attr	= { .ca_owner = THIS_MODULE,
		    .ca_name = "start_block",
		    .ca_mode = S_IRUGO | S_IWUSR },
	.show	= o2hb_region_start_block_read,
	.store	= o2hb_region_start_block_write,
};

static struct o2hb_region_attribute o2hb_region_attr_blocks = {
	.attr	= { .ca_owner = THIS_MODULE,
		    .ca_name = "blocks",
		    .ca_mode = S_IRUGO | S_IWUSR },
	.show	= o2hb_region_blocks_read,
	.store	= o2hb_region_blocks_write,
};

static struct o2hb_region_attribute o2hb_region_attr_dev = {
	.attr	= { .ca_owner = THIS_MODULE,
		    .ca_name = "dev",
		    .ca_mode = S_IRUGO | S_IWUSR },
	.show	= o2hb_region_dev_read,
	.store	= o2hb_region_dev_write,
};

static struct configfs_attribute *o2hb_region_attrs[] = {
	&o2hb_region_attr_block_bytes.attr,
	&o2hb_region_attr_start_block.attr,
	&o2hb_region_attr_blocks.attr,
	&o2hb_region_attr_dev.attr,
	NULL,
};

static ssize_t o2hb_region_show(struct config_item *item,
				struct configfs_attribute *attr,
				char *page)
{
	struct o2hb_region *reg = to_o2hb_region(item);
	struct o2hb_region_attribute *o2hb_region_attr =
		container_of(attr, struct o2hb_region_attribute, attr);
	ssize_t ret = 0;

	if (o2hb_region_attr->show)
		ret = o2hb_region_attr->show(reg, page);
	return ret;
}

static ssize_t o2hb_region_store(struct config_item *item,
				 struct configfs_attribute *attr,
				 const char *page, size_t count)
{
	struct o2hb_region *reg = to_o2hb_region(item);
	struct o2hb_region_attribute *o2hb_region_attr =
		container_of(attr, struct o2hb_region_attribute, attr);
	ssize_t ret = -EINVAL;

	if (o2hb_region_attr->store)
		ret = o2hb_region_attr->store(reg, page, count);
	return ret;
}

static struct configfs_item_operations o2hb_region_item_ops = {
	.release		= o2hb_region_release,
	.show_attribute		= o2hb_region_show,
	.store_attribute	= o2hb_region_store,
};

static struct config_item_type o2hb_region_type = {
	.ct_item_ops	= &o2hb_region_item_ops,
	.ct_attrs	= o2hb_region_attrs,
	.ct_owner	= THIS_MODULE,
};

/* heartbeat set */

struct o2hb_heartbeat_group {
	struct config_group hs_group;
	/* some stuff? */
};

static struct o2hb_heartbeat_group *to_o2hb_heartbeat_group(struct config_group *group)
{
	return group ?
		container_of(group, struct o2hb_heartbeat_group, hs_group)
		: NULL;
}

static struct config_item *o2hb_heartbeat_group_make_item(struct config_group *group,
							  const char *name)
{
	struct o2hb_region *reg = NULL;
	struct config_item *ret = NULL;

	reg = kcalloc(1, sizeof(struct o2hb_region), GFP_KERNEL);
	if (reg == NULL)
		goto out; /* ENOMEM */

	config_item_init_type_name(&reg->hr_item, name, &o2hb_region_type);

	ret = &reg->hr_item;

	spin_lock(&o2hb_live_lock);
	list_add_tail(&reg->hr_all_item, &o2hb_all_regions);
	spin_unlock(&o2hb_live_lock);
out:
	if (ret == NULL)
		kfree(reg);

	return ret;
}

static void o2hb_heartbeat_group_drop_item(struct config_group *group,
					   struct config_item *item)
{
	struct o2hb_region *reg = to_o2hb_region(item);

	/* stop the thread when the user removes the region dir */
	if (reg->hr_task) {
		kthread_stop(reg->hr_task);
		reg->hr_task = NULL;
	}

	config_item_put(item);
}

struct o2hb_heartbeat_group_attribute {
	struct configfs_attribute attr;
	ssize_t (*show)(struct o2hb_heartbeat_group *, char *);
	ssize_t (*store)(struct o2hb_heartbeat_group *, const char *, size_t);
};

static ssize_t o2hb_heartbeat_group_show(struct config_item *item,
					 struct configfs_attribute *attr,
					 char *page)
{
	struct o2hb_heartbeat_group *reg = to_o2hb_heartbeat_group(to_config_group(item));
	struct o2hb_heartbeat_group_attribute *o2hb_heartbeat_group_attr =
		container_of(attr, struct o2hb_heartbeat_group_attribute, attr);
	ssize_t ret = 0;

	if (o2hb_heartbeat_group_attr->show)
		ret = o2hb_heartbeat_group_attr->show(reg, page);
	return ret;
}

static ssize_t o2hb_heartbeat_group_store(struct config_item *item,
					  struct configfs_attribute *attr,
					  const char *page, size_t count)
{
	struct o2hb_heartbeat_group *reg = to_o2hb_heartbeat_group(to_config_group(item));
	struct o2hb_heartbeat_group_attribute *o2hb_heartbeat_group_attr =
		container_of(attr, struct o2hb_heartbeat_group_attribute, attr);
	ssize_t ret = -EINVAL;

	if (o2hb_heartbeat_group_attr->store)
		ret = o2hb_heartbeat_group_attr->store(reg, page, count);
	return ret;
}

static ssize_t o2hb_heartbeat_group_threshold_show(struct o2hb_heartbeat_group *group,
						     char *page)
{
	return sprintf(page, "%u\n", o2hb_dead_threshold);
}

static ssize_t o2hb_heartbeat_group_threshold_store(struct o2hb_heartbeat_group *group,
						    const char *page,
						    size_t count)
{
	unsigned long tmp;
	char *p = (char *)page;

	tmp = simple_strtoul(p, &p, 10);
	if (!p || (*p && (*p != '\n')))
                return -EINVAL;

	/* this will validate ranges for us. */
	o2hb_dead_threshold_set((unsigned int) tmp);

	return count;
}

static struct o2hb_heartbeat_group_attribute o2hb_heartbeat_group_attr_threshold = {
	.attr	= { .ca_owner = THIS_MODULE,
		    .ca_name = "dead_threshold",
		    .ca_mode = S_IRUGO | S_IWUSR },
	.show	= o2hb_heartbeat_group_threshold_show,
	.store	= o2hb_heartbeat_group_threshold_store,
};

static struct configfs_attribute *o2hb_heartbeat_group_attrs[] = {
	&o2hb_heartbeat_group_attr_threshold.attr,
	NULL,
};

static struct configfs_item_operations o2hb_hearbeat_group_item_ops = {
	.show_attribute		= o2hb_heartbeat_group_show,
	.store_attribute	= o2hb_heartbeat_group_store,
};

static struct configfs_group_operations o2hb_heartbeat_group_group_ops = {
	.make_item	= o2hb_heartbeat_group_make_item,
	.drop_item	= o2hb_heartbeat_group_drop_item,
};

static struct config_item_type o2hb_heartbeat_group_type = {
	.ct_group_ops	= &o2hb_heartbeat_group_group_ops,
	.ct_item_ops	= &o2hb_hearbeat_group_item_ops,
	.ct_attrs	= o2hb_heartbeat_group_attrs,
	.ct_owner	= THIS_MODULE,
};

/* this is just here to avoid touching group in heartbeat.h which the
 * entire damn world #includes */
struct config_group *o2hb_alloc_hb_set(void)
{
	struct o2hb_heartbeat_group *hs = NULL;
	struct config_group *ret = NULL;

	hs = kcalloc(1, sizeof(struct o2hb_heartbeat_group), GFP_KERNEL);
	if (hs == NULL)
		goto out;

	config_group_init_type_name(&hs->hs_group, "heartbeat",
				    &o2hb_heartbeat_group_type);

	ret = &hs->hs_group;
out:
	if (ret == NULL)
		kfree(hs);
	return ret;
}

void o2hb_free_hb_set(struct config_group *group)
{
	struct o2hb_heartbeat_group *hs = to_o2hb_heartbeat_group(group);
	kfree(hs);
}

/* hb callback registration and issueing */

static struct o2hb_callback *hbcall_from_type(enum o2hb_callback_type type)
{
	if (type == O2HB_NUM_CB)
		return ERR_PTR(-EINVAL);

	return &o2hb_callbacks[type];
}

void o2hb_setup_callback(struct o2hb_callback_func *hc,
			 enum o2hb_callback_type type,
			 o2hb_cb_func *func,
			 void *data,
			 int priority)
{
	INIT_LIST_HEAD(&hc->hc_item);
	hc->hc_func = func;
	hc->hc_data = data;
	hc->hc_priority = priority;
	hc->hc_type = type;
	hc->hc_magic = O2HB_CB_MAGIC;
}
EXPORT_SYMBOL_GPL(o2hb_setup_callback);

int o2hb_register_callback(struct o2hb_callback_func *hc)
{
	struct o2hb_callback_func *tmp;
	struct list_head *iter;
	struct o2hb_callback *hbcall;
	int ret;

	BUG_ON(hc->hc_magic != O2HB_CB_MAGIC);
	BUG_ON(!list_empty(&hc->hc_item));

	hbcall = hbcall_from_type(hc->hc_type);
	if (IS_ERR(hbcall)) {
		ret = PTR_ERR(hbcall);
		goto out;
	}

	down_write(&o2hb_callback_sem);

	list_for_each(iter, &hbcall->list) {
		tmp = list_entry(iter, struct o2hb_callback_func, hc_item);
		if (hc->hc_priority < tmp->hc_priority) {
			list_add_tail(&hc->hc_item, iter);
			break;
		}
	}
	if (list_empty(&hc->hc_item))
		list_add_tail(&hc->hc_item, &hbcall->list);

	up_write(&o2hb_callback_sem);
	ret = 0;
out:
	mlog(ML_HEARTBEAT, "returning %d on behalf of %p for funcs %p\n",
	     ret, __builtin_return_address(0), hc);
	return ret;
}
EXPORT_SYMBOL_GPL(o2hb_register_callback);

int o2hb_unregister_callback(struct o2hb_callback_func *hc)
{
	BUG_ON(hc->hc_magic != O2HB_CB_MAGIC);

	mlog(ML_HEARTBEAT, "on behalf of %p for funcs %p\n",
	     __builtin_return_address(0), hc);

	if (list_empty(&hc->hc_item))
		return 0;

	down_write(&o2hb_callback_sem);

	list_del_init(&hc->hc_item);

	up_write(&o2hb_callback_sem);

	return 0;
}
EXPORT_SYMBOL_GPL(o2hb_unregister_callback);

int o2hb_check_node_heartbeating(u8 node_num)
{
	unsigned long testing_map[BITS_TO_LONGS(O2NM_MAX_NODES)];

	o2hb_fill_node_map(testing_map, sizeof(testing_map));
	if (!test_bit(node_num, testing_map)) {
		mlog(ML_HEARTBEAT,
		     "node (%u) does not have heartbeating enabled.\n",
		     node_num);
		return 0;
	}

	return 1;
}
EXPORT_SYMBOL_GPL(o2hb_check_node_heartbeating);

int o2hb_check_node_heartbeating_from_callback(u8 node_num)
{
	unsigned long testing_map[BITS_TO_LONGS(O2NM_MAX_NODES)];

	o2hb_fill_node_map_from_callback(testing_map, sizeof(testing_map));
	if (!test_bit(node_num, testing_map)) {
		mlog(ML_HEARTBEAT,
		     "node (%u) does not have heartbeating enabled.\n",
		     node_num);
		return 0;
	}

	return 1;
}
EXPORT_SYMBOL_GPL(o2hb_check_node_heartbeating_from_callback);

/* Makes sure our local node is configured with a node number, and is
 * heartbeating. */
int o2hb_check_local_node_heartbeating(void)
{
	u8 node_num;

	/* if this node was set then we have networking */
	node_num = o2nm_this_node();
	if (node_num == O2NM_MAX_NODES) {
		mlog(ML_HEARTBEAT, "this node has not been configured.\n");
		return 0;
	}

	return o2hb_check_node_heartbeating(node_num);
}
EXPORT_SYMBOL_GPL(o2hb_check_local_node_heartbeating);

/*
 * this is just a hack until we get the plumbing which flips file systems
 * read only and drops the hb ref instead of killing the node dead.
 */
void o2hb_stop_all_regions(void)
{
	struct o2hb_region *reg;

	mlog(ML_ERROR, "stopping heartbeat on all active regions.\n");

	spin_lock(&o2hb_live_lock);

	list_for_each_entry(reg, &o2hb_all_regions, hr_all_item)
		reg->hr_unclean_stop = 1;

	spin_unlock(&o2hb_live_lock);
}
EXPORT_SYMBOL_GPL(o2hb_stop_all_regions);
