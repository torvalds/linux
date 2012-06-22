/*
 * Copyright (C) 2007 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License v2 as published by the Free Software Foundation.
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
#include <linux/sched.h>
#include <linux/bio.h>
#include <linux/slab.h>
#include <linux/buffer_head.h>
#include <linux/blkdev.h>
#include <linux/random.h>
#include <linux/iocontext.h>
#include <linux/capability.h>
#include <linux/ratelimit.h>
#include <linux/kthread.h>
#include <asm/div64.h>
#include "compat.h"
#include "ctree.h"
#include "extent_map.h"
#include "disk-io.h"
#include "transaction.h"
#include "print-tree.h"
#include "volumes.h"
#include "async-thread.h"
#include "check-integrity.h"
#include "rcu-string.h"

static int init_first_rw_device(struct btrfs_trans_handle *trans,
				struct btrfs_root *root,
				struct btrfs_device *device);
static int btrfs_relocate_sys_chunks(struct btrfs_root *root);
static void __btrfs_reset_dev_stats(struct btrfs_device *dev);
static void btrfs_dev_stat_print_on_load(struct btrfs_device *device);

static DEFINE_MUTEX(uuid_mutex);
static LIST_HEAD(fs_uuids);

static void lock_chunks(struct btrfs_root *root)
{
	mutex_lock(&root->fs_info->chunk_mutex);
}

static void unlock_chunks(struct btrfs_root *root)
{
	mutex_unlock(&root->fs_info->chunk_mutex);
}

static void free_fs_devices(struct btrfs_fs_devices *fs_devices)
{
	struct btrfs_device *device;
	WARN_ON(fs_devices->opened);
	while (!list_empty(&fs_devices->devices)) {
		device = list_entry(fs_devices->devices.next,
				    struct btrfs_device, dev_list);
		list_del(&device->dev_list);
		rcu_string_free(device->name);
		kfree(device);
	}
	kfree(fs_devices);
}

void btrfs_cleanup_fs_uuids(void)
{
	struct btrfs_fs_devices *fs_devices;

	while (!list_empty(&fs_uuids)) {
		fs_devices = list_entry(fs_uuids.next,
					struct btrfs_fs_devices, list);
		list_del(&fs_devices->list);
		free_fs_devices(fs_devices);
	}
}

static noinline struct btrfs_device *__find_device(struct list_head *head,
						   u64 devid, u8 *uuid)
{
	struct btrfs_device *dev;

	list_for_each_entry(dev, head, dev_list) {
		if (dev->devid == devid &&
		    (!uuid || !memcmp(dev->uuid, uuid, BTRFS_UUID_SIZE))) {
			return dev;
		}
	}
	return NULL;
}

static noinline struct btrfs_fs_devices *find_fsid(u8 *fsid)
{
	struct btrfs_fs_devices *fs_devices;

	list_for_each_entry(fs_devices, &fs_uuids, list) {
		if (memcmp(fsid, fs_devices->fsid, BTRFS_FSID_SIZE) == 0)
			return fs_devices;
	}
	return NULL;
}

static void requeue_list(struct btrfs_pending_bios *pending_bios,
			struct bio *head, struct bio *tail)
{

	struct bio *old_head;

	old_head = pending_bios->head;
	pending_bios->head = head;
	if (pending_bios->tail)
		tail->bi_next = old_head;
	else
		pending_bios->tail = tail;
}

/*
 * we try to collect pending bios for a device so we don't get a large
 * number of procs sending bios down to the same device.  This greatly
 * improves the schedulers ability to collect and merge the bios.
 *
 * But, it also turns into a long list of bios to process and that is sure
 * to eventually make the worker thread block.  The solution here is to
 * make some progress and then put this work struct back at the end of
 * the list if the block device is congested.  This way, multiple devices
 * can make progress from a single worker thread.
 */
static noinline void run_scheduled_bios(struct btrfs_device *device)
{
	struct bio *pending;
	struct backing_dev_info *bdi;
	struct btrfs_fs_info *fs_info;
	struct btrfs_pending_bios *pending_bios;
	struct bio *tail;
	struct bio *cur;
	int again = 0;
	unsigned long num_run;
	unsigned long batch_run = 0;
	unsigned long limit;
	unsigned long last_waited = 0;
	int force_reg = 0;
	int sync_pending = 0;
	struct blk_plug plug;

	/*
	 * this function runs all the bios we've collected for
	 * a particular device.  We don't want to wander off to
	 * another device without first sending all of these down.
	 * So, setup a plug here and finish it off before we return
	 */
	blk_start_plug(&plug);

	bdi = blk_get_backing_dev_info(device->bdev);
	fs_info = device->dev_root->fs_info;
	limit = btrfs_async_submit_limit(fs_info);
	limit = limit * 2 / 3;

loop:
	spin_lock(&device->io_lock);

loop_lock:
	num_run = 0;

	/* take all the bios off the list at once and process them
	 * later on (without the lock held).  But, remember the
	 * tail and other pointers so the bios can be properly reinserted
	 * into the list if we hit congestion
	 */
	if (!force_reg && device->pending_sync_bios.head) {
		pending_bios = &device->pending_sync_bios;
		force_reg = 1;
	} else {
		pending_bios = &device->pending_bios;
		force_reg = 0;
	}

	pending = pending_bios->head;
	tail = pending_bios->tail;
	WARN_ON(pending && !tail);

	/*
	 * if pending was null this time around, no bios need processing
	 * at all and we can stop.  Otherwise it'll loop back up again
	 * and do an additional check so no bios are missed.
	 *
	 * device->running_pending is used to synchronize with the
	 * schedule_bio code.
	 */
	if (device->pending_sync_bios.head == NULL &&
	    device->pending_bios.head == NULL) {
		again = 0;
		device->running_pending = 0;
	} else {
		again = 1;
		device->running_pending = 1;
	}

	pending_bios->head = NULL;
	pending_bios->tail = NULL;

	spin_unlock(&device->io_lock);

	while (pending) {

		rmb();
		/* we want to work on both lists, but do more bios on the
		 * sync list than the regular list
		 */
		if ((num_run > 32 &&
		    pending_bios != &device->pending_sync_bios &&
		    device->pending_sync_bios.head) ||
		   (num_run > 64 && pending_bios == &device->pending_sync_bios &&
		    device->pending_bios.head)) {
			spin_lock(&device->io_lock);
			requeue_list(pending_bios, pending, tail);
			goto loop_lock;
		}

		cur = pending;
		pending = pending->bi_next;
		cur->bi_next = NULL;
		atomic_dec(&fs_info->nr_async_bios);

		if (atomic_read(&fs_info->nr_async_bios) < limit &&
		    waitqueue_active(&fs_info->async_submit_wait))
			wake_up(&fs_info->async_submit_wait);

		BUG_ON(atomic_read(&cur->bi_cnt) == 0);

		/*
		 * if we're doing the sync list, record that our
		 * plug has some sync requests on it
		 *
		 * If we're doing the regular list and there are
		 * sync requests sitting around, unplug before
		 * we add more
		 */
		if (pending_bios == &device->pending_sync_bios) {
			sync_pending = 1;
		} else if (sync_pending) {
			blk_finish_plug(&plug);
			blk_start_plug(&plug);
			sync_pending = 0;
		}

		btrfsic_submit_bio(cur->bi_rw, cur);
		num_run++;
		batch_run++;
		if (need_resched())
			cond_resched();

		/*
		 * we made progress, there is more work to do and the bdi
		 * is now congested.  Back off and let other work structs
		 * run instead
		 */
		if (pending && bdi_write_congested(bdi) && batch_run > 8 &&
		    fs_info->fs_devices->open_devices > 1) {
			struct io_context *ioc;

			ioc = current->io_context;

			/*
			 * the main goal here is that we don't want to
			 * block if we're going to be able to submit
			 * more requests without blocking.
			 *
			 * This code does two great things, it pokes into
			 * the elevator code from a filesystem _and_
			 * it makes assumptions about how batching works.
			 */
			if (ioc && ioc->nr_batch_requests > 0 &&
			    time_before(jiffies, ioc->last_waited + HZ/50UL) &&
			    (last_waited == 0 ||
			     ioc->last_waited == last_waited)) {
				/*
				 * we want to go through our batch of
				 * requests and stop.  So, we copy out
				 * the ioc->last_waited time and test
				 * against it before looping
				 */
				last_waited = ioc->last_waited;
				if (need_resched())
					cond_resched();
				continue;
			}
			spin_lock(&device->io_lock);
			requeue_list(pending_bios, pending, tail);
			device->running_pending = 1;

			spin_unlock(&device->io_lock);
			btrfs_requeue_work(&device->work);
			goto done;
		}
		/* unplug every 64 requests just for good measure */
		if (batch_run % 64 == 0) {
			blk_finish_plug(&plug);
			blk_start_plug(&plug);
			sync_pending = 0;
		}
	}

	cond_resched();
	if (again)
		goto loop;

	spin_lock(&device->io_lock);
	if (device->pending_bios.head || device->pending_sync_bios.head)
		goto loop_lock;
	spin_unlock(&device->io_lock);

done:
	blk_finish_plug(&plug);
}

static void pending_bios_fn(struct btrfs_work *work)
{
	struct btrfs_device *device;

	device = container_of(work, struct btrfs_device, work);
	run_scheduled_bios(device);
}

static noinline int device_list_add(const char *path,
			   struct btrfs_super_block *disk_super,
			   u64 devid, struct btrfs_fs_devices **fs_devices_ret)
{
	struct btrfs_device *device;
	struct btrfs_fs_devices *fs_devices;
	struct rcu_string *name;
	u64 found_transid = btrfs_super_generation(disk_super);

	fs_devices = find_fsid(disk_super->fsid);
	if (!fs_devices) {
		fs_devices = kzalloc(sizeof(*fs_devices), GFP_NOFS);
		if (!fs_devices)
			return -ENOMEM;
		INIT_LIST_HEAD(&fs_devices->devices);
		INIT_LIST_HEAD(&fs_devices->alloc_list);
		list_add(&fs_devices->list, &fs_uuids);
		memcpy(fs_devices->fsid, disk_super->fsid, BTRFS_FSID_SIZE);
		fs_devices->latest_devid = devid;
		fs_devices->latest_trans = found_transid;
		mutex_init(&fs_devices->device_list_mutex);
		device = NULL;
	} else {
		device = __find_device(&fs_devices->devices, devid,
				       disk_super->dev_item.uuid);
	}
	if (!device) {
		if (fs_devices->opened)
			return -EBUSY;

		device = kzalloc(sizeof(*device), GFP_NOFS);
		if (!device) {
			/* we can safely leave the fs_devices entry around */
			return -ENOMEM;
		}
		device->devid = devid;
		device->dev_stats_valid = 0;
		device->work.func = pending_bios_fn;
		memcpy(device->uuid, disk_super->dev_item.uuid,
		       BTRFS_UUID_SIZE);
		spin_lock_init(&device->io_lock);

		name = rcu_string_strdup(path, GFP_NOFS);
		if (!name) {
			kfree(device);
			return -ENOMEM;
		}
		rcu_assign_pointer(device->name, name);
		INIT_LIST_HEAD(&device->dev_alloc_list);

		/* init readahead state */
		spin_lock_init(&device->reada_lock);
		device->reada_curr_zone = NULL;
		atomic_set(&device->reada_in_flight, 0);
		device->reada_next = 0;
		INIT_RADIX_TREE(&device->reada_zones, GFP_NOFS & ~__GFP_WAIT);
		INIT_RADIX_TREE(&device->reada_extents, GFP_NOFS & ~__GFP_WAIT);

		mutex_lock(&fs_devices->device_list_mutex);
		list_add_rcu(&device->dev_list, &fs_devices->devices);
		mutex_unlock(&fs_devices->device_list_mutex);

		device->fs_devices = fs_devices;
		fs_devices->num_devices++;
	} else if (!device->name || strcmp(device->name->str, path)) {
		name = rcu_string_strdup(path, GFP_NOFS);
		if (!name)
			return -ENOMEM;
		rcu_string_free(device->name);
		rcu_assign_pointer(device->name, name);
		if (device->missing) {
			fs_devices->missing_devices--;
			device->missing = 0;
		}
	}

	if (found_transid > fs_devices->latest_trans) {
		fs_devices->latest_devid = devid;
		fs_devices->latest_trans = found_transid;
	}
	*fs_devices_ret = fs_devices;
	return 0;
}

static struct btrfs_fs_devices *clone_fs_devices(struct btrfs_fs_devices *orig)
{
	struct btrfs_fs_devices *fs_devices;
	struct btrfs_device *device;
	struct btrfs_device *orig_dev;

	fs_devices = kzalloc(sizeof(*fs_devices), GFP_NOFS);
	if (!fs_devices)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&fs_devices->devices);
	INIT_LIST_HEAD(&fs_devices->alloc_list);
	INIT_LIST_HEAD(&fs_devices->list);
	mutex_init(&fs_devices->device_list_mutex);
	fs_devices->latest_devid = orig->latest_devid;
	fs_devices->latest_trans = orig->latest_trans;
	memcpy(fs_devices->fsid, orig->fsid, sizeof(fs_devices->fsid));

	/* We have held the volume lock, it is safe to get the devices. */
	list_for_each_entry(orig_dev, &orig->devices, dev_list) {
		struct rcu_string *name;

		device = kzalloc(sizeof(*device), GFP_NOFS);
		if (!device)
			goto error;

		/*
		 * This is ok to do without rcu read locked because we hold the
		 * uuid mutex so nothing we touch in here is going to disappear.
		 */
		name = rcu_string_strdup(orig_dev->name->str, GFP_NOFS);
		if (!name) {
			kfree(device);
			goto error;
		}
		rcu_assign_pointer(device->name, name);

		device->devid = orig_dev->devid;
		device->work.func = pending_bios_fn;
		memcpy(device->uuid, orig_dev->uuid, sizeof(device->uuid));
		spin_lock_init(&device->io_lock);
		INIT_LIST_HEAD(&device->dev_list);
		INIT_LIST_HEAD(&device->dev_alloc_list);

		list_add(&device->dev_list, &fs_devices->devices);
		device->fs_devices = fs_devices;
		fs_devices->num_devices++;
	}
	return fs_devices;
error:
	free_fs_devices(fs_devices);
	return ERR_PTR(-ENOMEM);
}

void btrfs_close_extra_devices(struct btrfs_fs_devices *fs_devices)
{
	struct btrfs_device *device, *next;

	struct block_device *latest_bdev = NULL;
	u64 latest_devid = 0;
	u64 latest_transid = 0;

	mutex_lock(&uuid_mutex);
again:
	/* This is the initialized path, it is safe to release the devices. */
	list_for_each_entry_safe(device, next, &fs_devices->devices, dev_list) {
		if (device->in_fs_metadata) {
			if (!latest_transid ||
			    device->generation > latest_transid) {
				latest_devid = device->devid;
				latest_transid = device->generation;
				latest_bdev = device->bdev;
			}
			continue;
		}

		if (device->bdev) {
			blkdev_put(device->bdev, device->mode);
			device->bdev = NULL;
			fs_devices->open_devices--;
		}
		if (device->writeable) {
			list_del_init(&device->dev_alloc_list);
			device->writeable = 0;
			fs_devices->rw_devices--;
		}
		list_del_init(&device->dev_list);
		fs_devices->num_devices--;
		rcu_string_free(device->name);
		kfree(device);
	}

	if (fs_devices->seed) {
		fs_devices = fs_devices->seed;
		goto again;
	}

	fs_devices->latest_bdev = latest_bdev;
	fs_devices->latest_devid = latest_devid;
	fs_devices->latest_trans = latest_transid;

	mutex_unlock(&uuid_mutex);
}

static void __free_device(struct work_struct *work)
{
	struct btrfs_device *device;

	device = container_of(work, struct btrfs_device, rcu_work);

	if (device->bdev)
		blkdev_put(device->bdev, device->mode);

	rcu_string_free(device->name);
	kfree(device);
}

static void free_device(struct rcu_head *head)
{
	struct btrfs_device *device;

	device = container_of(head, struct btrfs_device, rcu);

	INIT_WORK(&device->rcu_work, __free_device);
	schedule_work(&device->rcu_work);
}

static int __btrfs_close_devices(struct btrfs_fs_devices *fs_devices)
{
	struct btrfs_device *device;

	if (--fs_devices->opened > 0)
		return 0;

	mutex_lock(&fs_devices->device_list_mutex);
	list_for_each_entry(device, &fs_devices->devices, dev_list) {
		struct btrfs_device *new_device;
		struct rcu_string *name;

		if (device->bdev)
			fs_devices->open_devices--;

		if (device->writeable) {
			list_del_init(&device->dev_alloc_list);
			fs_devices->rw_devices--;
		}

		if (device->can_discard)
			fs_devices->num_can_discard--;

		new_device = kmalloc(sizeof(*new_device), GFP_NOFS);
		BUG_ON(!new_device); /* -ENOMEM */
		memcpy(new_device, device, sizeof(*new_device));

		/* Safe because we are under uuid_mutex */
		name = rcu_string_strdup(device->name->str, GFP_NOFS);
		BUG_ON(device->name && !name); /* -ENOMEM */
		rcu_assign_pointer(new_device->name, name);
		new_device->bdev = NULL;
		new_device->writeable = 0;
		new_device->in_fs_metadata = 0;
		new_device->can_discard = 0;
		list_replace_rcu(&device->dev_list, &new_device->dev_list);

		call_rcu(&device->rcu, free_device);
	}
	mutex_unlock(&fs_devices->device_list_mutex);

	WARN_ON(fs_devices->open_devices);
	WARN_ON(fs_devices->rw_devices);
	fs_devices->opened = 0;
	fs_devices->seeding = 0;

	return 0;
}

int btrfs_close_devices(struct btrfs_fs_devices *fs_devices)
{
	struct btrfs_fs_devices *seed_devices = NULL;
	int ret;

	mutex_lock(&uuid_mutex);
	ret = __btrfs_close_devices(fs_devices);
	if (!fs_devices->opened) {
		seed_devices = fs_devices->seed;
		fs_devices->seed = NULL;
	}
	mutex_unlock(&uuid_mutex);

	while (seed_devices) {
		fs_devices = seed_devices;
		seed_devices = fs_devices->seed;
		__btrfs_close_devices(fs_devices);
		free_fs_devices(fs_devices);
	}
	return ret;
}

static int __btrfs_open_devices(struct btrfs_fs_devices *fs_devices,
				fmode_t flags, void *holder)
{
	struct request_queue *q;
	struct block_device *bdev;
	struct list_head *head = &fs_devices->devices;
	struct btrfs_device *device;
	struct block_device *latest_bdev = NULL;
	struct buffer_head *bh;
	struct btrfs_super_block *disk_super;
	u64 latest_devid = 0;
	u64 latest_transid = 0;
	u64 devid;
	int seeding = 1;
	int ret = 0;

	flags |= FMODE_EXCL;

	list_for_each_entry(device, head, dev_list) {
		if (device->bdev)
			continue;
		if (!device->name)
			continue;

		bdev = blkdev_get_by_path(device->name->str, flags, holder);
		if (IS_ERR(bdev)) {
			printk(KERN_INFO "open %s failed\n", device->name->str);
			goto error;
		}
		filemap_write_and_wait(bdev->bd_inode->i_mapping);
		invalidate_bdev(bdev);
		set_blocksize(bdev, 4096);

		bh = btrfs_read_dev_super(bdev);
		if (!bh)
			goto error_close;

		disk_super = (struct btrfs_super_block *)bh->b_data;
		devid = btrfs_stack_device_id(&disk_super->dev_item);
		if (devid != device->devid)
			goto error_brelse;

		if (memcmp(device->uuid, disk_super->dev_item.uuid,
			   BTRFS_UUID_SIZE))
			goto error_brelse;

		device->generation = btrfs_super_generation(disk_super);
		if (!latest_transid || device->generation > latest_transid) {
			latest_devid = devid;
			latest_transid = device->generation;
			latest_bdev = bdev;
		}

		if (btrfs_super_flags(disk_super) & BTRFS_SUPER_FLAG_SEEDING) {
			device->writeable = 0;
		} else {
			device->writeable = !bdev_read_only(bdev);
			seeding = 0;
		}

		q = bdev_get_queue(bdev);
		if (blk_queue_discard(q)) {
			device->can_discard = 1;
			fs_devices->num_can_discard++;
		}

		device->bdev = bdev;
		device->in_fs_metadata = 0;
		device->mode = flags;

		if (!blk_queue_nonrot(bdev_get_queue(bdev)))
			fs_devices->rotating = 1;

		fs_devices->open_devices++;
		if (device->writeable) {
			fs_devices->rw_devices++;
			list_add(&device->dev_alloc_list,
				 &fs_devices->alloc_list);
		}
		brelse(bh);
		continue;

error_brelse:
		brelse(bh);
error_close:
		blkdev_put(bdev, flags);
error:
		continue;
	}
	if (fs_devices->open_devices == 0) {
		ret = -EINVAL;
		goto out;
	}
	fs_devices->seeding = seeding;
	fs_devices->opened = 1;
	fs_devices->latest_bdev = latest_bdev;
	fs_devices->latest_devid = latest_devid;
	fs_devices->latest_trans = latest_transid;
	fs_devices->total_rw_bytes = 0;
out:
	return ret;
}

int btrfs_open_devices(struct btrfs_fs_devices *fs_devices,
		       fmode_t flags, void *holder)
{
	int ret;

	mutex_lock(&uuid_mutex);
	if (fs_devices->opened) {
		fs_devices->opened++;
		ret = 0;
	} else {
		ret = __btrfs_open_devices(fs_devices, flags, holder);
	}
	mutex_unlock(&uuid_mutex);
	return ret;
}

int btrfs_scan_one_device(const char *path, fmode_t flags, void *holder,
			  struct btrfs_fs_devices **fs_devices_ret)
{
	struct btrfs_super_block *disk_super;
	struct block_device *bdev;
	struct buffer_head *bh;
	int ret;
	u64 devid;
	u64 transid;

	flags |= FMODE_EXCL;
	bdev = blkdev_get_by_path(path, flags, holder);

	if (IS_ERR(bdev)) {
		ret = PTR_ERR(bdev);
		goto error;
	}

	mutex_lock(&uuid_mutex);
	ret = set_blocksize(bdev, 4096);
	if (ret)
		goto error_close;
	bh = btrfs_read_dev_super(bdev);
	if (!bh) {
		ret = -EINVAL;
		goto error_close;
	}
	disk_super = (struct btrfs_super_block *)bh->b_data;
	devid = btrfs_stack_device_id(&disk_super->dev_item);
	transid = btrfs_super_generation(disk_super);
	if (disk_super->label[0])
		printk(KERN_INFO "device label %s ", disk_super->label);
	else
		printk(KERN_INFO "device fsid %pU ", disk_super->fsid);
	printk(KERN_CONT "devid %llu transid %llu %s\n",
	       (unsigned long long)devid, (unsigned long long)transid, path);
	ret = device_list_add(path, disk_super, devid, fs_devices_ret);

	brelse(bh);
error_close:
	mutex_unlock(&uuid_mutex);
	blkdev_put(bdev, flags);
error:
	return ret;
}

/* helper to account the used device space in the range */
int btrfs_account_dev_extents_size(struct btrfs_device *device, u64 start,
				   u64 end, u64 *length)
{
	struct btrfs_key key;
	struct btrfs_root *root = device->dev_root;
	struct btrfs_dev_extent *dev_extent;
	struct btrfs_path *path;
	u64 extent_end;
	int ret;
	int slot;
	struct extent_buffer *l;

	*length = 0;

	if (start >= device->total_bytes)
		return 0;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;
	path->reada = 2;

	key.objectid = device->devid;
	key.offset = start;
	key.type = BTRFS_DEV_EXTENT_KEY;

	ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
	if (ret < 0)
		goto out;
	if (ret > 0) {
		ret = btrfs_previous_item(root, path, key.objectid, key.type);
		if (ret < 0)
			goto out;
	}

	while (1) {
		l = path->nodes[0];
		slot = path->slots[0];
		if (slot >= btrfs_header_nritems(l)) {
			ret = btrfs_next_leaf(root, path);
			if (ret == 0)
				continue;
			if (ret < 0)
				goto out;

			break;
		}
		btrfs_item_key_to_cpu(l, &key, slot);

		if (key.objectid < device->devid)
			goto next;

		if (key.objectid > device->devid)
			break;

		if (btrfs_key_type(&key) != BTRFS_DEV_EXTENT_KEY)
			goto next;

		dev_extent = btrfs_item_ptr(l, slot, struct btrfs_dev_extent);
		extent_end = key.offset + btrfs_dev_extent_length(l,
								  dev_extent);
		if (key.offset <= start && extent_end > end) {
			*length = end - start + 1;
			break;
		} else if (key.offset <= start && extent_end > start)
			*length += extent_end - start;
		else if (key.offset > start && extent_end <= end)
			*length += extent_end - key.offset;
		else if (key.offset > start && key.offset <= end) {
			*length += end - key.offset + 1;
			break;
		} else if (key.offset > end)
			break;

next:
		path->slots[0]++;
	}
	ret = 0;
out:
	btrfs_free_path(path);
	return ret;
}

/*
 * find_free_dev_extent - find free space in the specified device
 * @device:	the device which we search the free space in
 * @num_bytes:	the size of the free space that we need
 * @start:	store the start of the free space.
 * @len:	the size of the free space. that we find, or the size of the max
 * 		free space if we don't find suitable free space
 *
 * this uses a pretty simple search, the expectation is that it is
 * called very infrequently and that a given device has a small number
 * of extents
 *
 * @start is used to store the start of the free space if we find. But if we
 * don't find suitable free space, it will be used to store the start position
 * of the max free space.
 *
 * @len is used to store the size of the free space that we find.
 * But if we don't find suitable free space, it is used to store the size of
 * the max free space.
 */
int find_free_dev_extent(struct btrfs_device *device, u64 num_bytes,
			 u64 *start, u64 *len)
{
	struct btrfs_key key;
	struct btrfs_root *root = device->dev_root;
	struct btrfs_dev_extent *dev_extent;
	struct btrfs_path *path;
	u64 hole_size;
	u64 max_hole_start;
	u64 max_hole_size;
	u64 extent_end;
	u64 search_start;
	u64 search_end = device->total_bytes;
	int ret;
	int slot;
	struct extent_buffer *l;

	/* FIXME use last free of some kind */

	/* we don't want to overwrite the superblock on the drive,
	 * so we make sure to start at an offset of at least 1MB
	 */
	search_start = max(root->fs_info->alloc_start, 1024ull * 1024);

	max_hole_start = search_start;
	max_hole_size = 0;
	hole_size = 0;

	if (search_start >= search_end) {
		ret = -ENOSPC;
		goto error;
	}

	path = btrfs_alloc_path();
	if (!path) {
		ret = -ENOMEM;
		goto error;
	}
	path->reada = 2;

	key.objectid = device->devid;
	key.offset = search_start;
	key.type = BTRFS_DEV_EXTENT_KEY;

	ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
	if (ret < 0)
		goto out;
	if (ret > 0) {
		ret = btrfs_previous_item(root, path, key.objectid, key.type);
		if (ret < 0)
			goto out;
	}

	while (1) {
		l = path->nodes[0];
		slot = path->slots[0];
		if (slot >= btrfs_header_nritems(l)) {
			ret = btrfs_next_leaf(root, path);
			if (ret == 0)
				continue;
			if (ret < 0)
				goto out;

			break;
		}
		btrfs_item_key_to_cpu(l, &key, slot);

		if (key.objectid < device->devid)
			goto next;

		if (key.objectid > device->devid)
			break;

		if (btrfs_key_type(&key) != BTRFS_DEV_EXTENT_KEY)
			goto next;

		if (key.offset > search_start) {
			hole_size = key.offset - search_start;

			if (hole_size > max_hole_size) {
				max_hole_start = search_start;
				max_hole_size = hole_size;
			}

			/*
			 * If this free space is greater than which we need,
			 * it must be the max free space that we have found
			 * until now, so max_hole_start must point to the start
			 * of this free space and the length of this free space
			 * is stored in max_hole_size. Thus, we return
			 * max_hole_start and max_hole_size and go back to the
			 * caller.
			 */
			if (hole_size >= num_bytes) {
				ret = 0;
				goto out;
			}
		}

		dev_extent = btrfs_item_ptr(l, slot, struct btrfs_dev_extent);
		extent_end = key.offset + btrfs_dev_extent_length(l,
								  dev_extent);
		if (extent_end > search_start)
			search_start = extent_end;
next:
		path->slots[0]++;
		cond_resched();
	}

	/*
	 * At this point, search_start should be the end of
	 * allocated dev extents, and when shrinking the device,
	 * search_end may be smaller than search_start.
	 */
	if (search_end > search_start)
		hole_size = search_end - search_start;

	if (hole_size > max_hole_size) {
		max_hole_start = search_start;
		max_hole_size = hole_size;
	}

	/* See above. */
	if (hole_size < num_bytes)
		ret = -ENOSPC;
	else
		ret = 0;

out:
	btrfs_free_path(path);
error:
	*start = max_hole_start;
	if (len)
		*len = max_hole_size;
	return ret;
}

static int btrfs_free_dev_extent(struct btrfs_trans_handle *trans,
			  struct btrfs_device *device,
			  u64 start)
{
	int ret;
	struct btrfs_path *path;
	struct btrfs_root *root = device->dev_root;
	struct btrfs_key key;
	struct btrfs_key found_key;
	struct extent_buffer *leaf = NULL;
	struct btrfs_dev_extent *extent = NULL;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	key.objectid = device->devid;
	key.offset = start;
	key.type = BTRFS_DEV_EXTENT_KEY;
again:
	ret = btrfs_search_slot(trans, root, &key, path, -1, 1);
	if (ret > 0) {
		ret = btrfs_previous_item(root, path, key.objectid,
					  BTRFS_DEV_EXTENT_KEY);
		if (ret)
			goto out;
		leaf = path->nodes[0];
		btrfs_item_key_to_cpu(leaf, &found_key, path->slots[0]);
		extent = btrfs_item_ptr(leaf, path->slots[0],
					struct btrfs_dev_extent);
		BUG_ON(found_key.offset > start || found_key.offset +
		       btrfs_dev_extent_length(leaf, extent) < start);
		key = found_key;
		btrfs_release_path(path);
		goto again;
	} else if (ret == 0) {
		leaf = path->nodes[0];
		extent = btrfs_item_ptr(leaf, path->slots[0],
					struct btrfs_dev_extent);
	} else {
		btrfs_error(root->fs_info, ret, "Slot search failed");
		goto out;
	}

	if (device->bytes_used > 0) {
		u64 len = btrfs_dev_extent_length(leaf, extent);
		device->bytes_used -= len;
		spin_lock(&root->fs_info->free_chunk_lock);
		root->fs_info->free_chunk_space += len;
		spin_unlock(&root->fs_info->free_chunk_lock);
	}
	ret = btrfs_del_item(trans, root, path);
	if (ret) {
		btrfs_error(root->fs_info, ret,
			    "Failed to remove dev extent item");
	}
out:
	btrfs_free_path(path);
	return ret;
}

int btrfs_alloc_dev_extent(struct btrfs_trans_handle *trans,
			   struct btrfs_device *device,
			   u64 chunk_tree, u64 chunk_objectid,
			   u64 chunk_offset, u64 start, u64 num_bytes)
{
	int ret;
	struct btrfs_path *path;
	struct btrfs_root *root = device->dev_root;
	struct btrfs_dev_extent *extent;
	struct extent_buffer *leaf;
	struct btrfs_key key;

	WARN_ON(!device->in_fs_metadata);
	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	key.objectid = device->devid;
	key.offset = start;
	key.type = BTRFS_DEV_EXTENT_KEY;
	ret = btrfs_insert_empty_item(trans, root, path, &key,
				      sizeof(*extent));
	if (ret)
		goto out;

	leaf = path->nodes[0];
	extent = btrfs_item_ptr(leaf, path->slots[0],
				struct btrfs_dev_extent);
	btrfs_set_dev_extent_chunk_tree(leaf, extent, chunk_tree);
	btrfs_set_dev_extent_chunk_objectid(leaf, extent, chunk_objectid);
	btrfs_set_dev_extent_chunk_offset(leaf, extent, chunk_offset);

	write_extent_buffer(leaf, root->fs_info->chunk_tree_uuid,
		    (unsigned long)btrfs_dev_extent_chunk_tree_uuid(extent),
		    BTRFS_UUID_SIZE);

	btrfs_set_dev_extent_length(leaf, extent, num_bytes);
	btrfs_mark_buffer_dirty(leaf);
out:
	btrfs_free_path(path);
	return ret;
}

static noinline int find_next_chunk(struct btrfs_root *root,
				    u64 objectid, u64 *offset)
{
	struct btrfs_path *path;
	int ret;
	struct btrfs_key key;
	struct btrfs_chunk *chunk;
	struct btrfs_key found_key;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	key.objectid = objectid;
	key.offset = (u64)-1;
	key.type = BTRFS_CHUNK_ITEM_KEY;

	ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
	if (ret < 0)
		goto error;

	BUG_ON(ret == 0); /* Corruption */

	ret = btrfs_previous_item(root, path, 0, BTRFS_CHUNK_ITEM_KEY);
	if (ret) {
		*offset = 0;
	} else {
		btrfs_item_key_to_cpu(path->nodes[0], &found_key,
				      path->slots[0]);
		if (found_key.objectid != objectid)
			*offset = 0;
		else {
			chunk = btrfs_item_ptr(path->nodes[0], path->slots[0],
					       struct btrfs_chunk);
			*offset = found_key.offset +
				btrfs_chunk_length(path->nodes[0], chunk);
		}
	}
	ret = 0;
error:
	btrfs_free_path(path);
	return ret;
}

static noinline int find_next_devid(struct btrfs_root *root, u64 *objectid)
{
	int ret;
	struct btrfs_key key;
	struct btrfs_key found_key;
	struct btrfs_path *path;

	root = root->fs_info->chunk_root;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	key.objectid = BTRFS_DEV_ITEMS_OBJECTID;
	key.type = BTRFS_DEV_ITEM_KEY;
	key.offset = (u64)-1;

	ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
	if (ret < 0)
		goto error;

	BUG_ON(ret == 0); /* Corruption */

	ret = btrfs_previous_item(root, path, BTRFS_DEV_ITEMS_OBJECTID,
				  BTRFS_DEV_ITEM_KEY);
	if (ret) {
		*objectid = 1;
	} else {
		btrfs_item_key_to_cpu(path->nodes[0], &found_key,
				      path->slots[0]);
		*objectid = found_key.offset + 1;
	}
	ret = 0;
error:
	btrfs_free_path(path);
	return ret;
}

/*
 * the device information is stored in the chunk root
 * the btrfs_device struct should be fully filled in
 */
int btrfs_add_device(struct btrfs_trans_handle *trans,
		     struct btrfs_root *root,
		     struct btrfs_device *device)
{
	int ret;
	struct btrfs_path *path;
	struct btrfs_dev_item *dev_item;
	struct extent_buffer *leaf;
	struct btrfs_key key;
	unsigned long ptr;

	root = root->fs_info->chunk_root;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	key.objectid = BTRFS_DEV_ITEMS_OBJECTID;
	key.type = BTRFS_DEV_ITEM_KEY;
	key.offset = device->devid;

	ret = btrfs_insert_empty_item(trans, root, path, &key,
				      sizeof(*dev_item));
	if (ret)
		goto out;

	leaf = path->nodes[0];
	dev_item = btrfs_item_ptr(leaf, path->slots[0], struct btrfs_dev_item);

	btrfs_set_device_id(leaf, dev_item, device->devid);
	btrfs_set_device_generation(leaf, dev_item, 0);
	btrfs_set_device_type(leaf, dev_item, device->type);
	btrfs_set_device_io_align(leaf, dev_item, device->io_align);
	btrfs_set_device_io_width(leaf, dev_item, device->io_width);
	btrfs_set_device_sector_size(leaf, dev_item, device->sector_size);
	btrfs_set_device_total_bytes(leaf, dev_item, device->total_bytes);
	btrfs_set_device_bytes_used(leaf, dev_item, device->bytes_used);
	btrfs_set_device_group(leaf, dev_item, 0);
	btrfs_set_device_seek_speed(leaf, dev_item, 0);
	btrfs_set_device_bandwidth(leaf, dev_item, 0);
	btrfs_set_device_start_offset(leaf, dev_item, 0);

	ptr = (unsigned long)btrfs_device_uuid(dev_item);
	write_extent_buffer(leaf, device->uuid, ptr, BTRFS_UUID_SIZE);
	ptr = (unsigned long)btrfs_device_fsid(dev_item);
	write_extent_buffer(leaf, root->fs_info->fsid, ptr, BTRFS_UUID_SIZE);
	btrfs_mark_buffer_dirty(leaf);

	ret = 0;
out:
	btrfs_free_path(path);
	return ret;
}

static int btrfs_rm_dev_item(struct btrfs_root *root,
			     struct btrfs_device *device)
{
	int ret;
	struct btrfs_path *path;
	struct btrfs_key key;
	struct btrfs_trans_handle *trans;

	root = root->fs_info->chunk_root;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	trans = btrfs_start_transaction(root, 0);
	if (IS_ERR(trans)) {
		btrfs_free_path(path);
		return PTR_ERR(trans);
	}
	key.objectid = BTRFS_DEV_ITEMS_OBJECTID;
	key.type = BTRFS_DEV_ITEM_KEY;
	key.offset = device->devid;
	lock_chunks(root);

	ret = btrfs_search_slot(trans, root, &key, path, -1, 1);
	if (ret < 0)
		goto out;

	if (ret > 0) {
		ret = -ENOENT;
		goto out;
	}

	ret = btrfs_del_item(trans, root, path);
	if (ret)
		goto out;
out:
	btrfs_free_path(path);
	unlock_chunks(root);
	btrfs_commit_transaction(trans, root);
	return ret;
}

int btrfs_rm_device(struct btrfs_root *root, char *device_path)
{
	struct btrfs_device *device;
	struct btrfs_device *next_device;
	struct block_device *bdev;
	struct buffer_head *bh = NULL;
	struct btrfs_super_block *disk_super;
	struct btrfs_fs_devices *cur_devices;
	u64 all_avail;
	u64 devid;
	u64 num_devices;
	u8 *dev_uuid;
	int ret = 0;
	bool clear_super = false;

	mutex_lock(&uuid_mutex);

	all_avail = root->fs_info->avail_data_alloc_bits |
		root->fs_info->avail_system_alloc_bits |
		root->fs_info->avail_metadata_alloc_bits;

	if ((all_avail & BTRFS_BLOCK_GROUP_RAID10) &&
	    root->fs_info->fs_devices->num_devices <= 4) {
		printk(KERN_ERR "btrfs: unable to go below four devices "
		       "on raid10\n");
		ret = -EINVAL;
		goto out;
	}

	if ((all_avail & BTRFS_BLOCK_GROUP_RAID1) &&
	    root->fs_info->fs_devices->num_devices <= 2) {
		printk(KERN_ERR "btrfs: unable to go below two "
		       "devices on raid1\n");
		ret = -EINVAL;
		goto out;
	}

	if (strcmp(device_path, "missing") == 0) {
		struct list_head *devices;
		struct btrfs_device *tmp;

		device = NULL;
		devices = &root->fs_info->fs_devices->devices;
		/*
		 * It is safe to read the devices since the volume_mutex
		 * is held.
		 */
		list_for_each_entry(tmp, devices, dev_list) {
			if (tmp->in_fs_metadata && !tmp->bdev) {
				device = tmp;
				break;
			}
		}
		bdev = NULL;
		bh = NULL;
		disk_super = NULL;
		if (!device) {
			printk(KERN_ERR "btrfs: no missing devices found to "
			       "remove\n");
			goto out;
		}
	} else {
		bdev = blkdev_get_by_path(device_path, FMODE_READ | FMODE_EXCL,
					  root->fs_info->bdev_holder);
		if (IS_ERR(bdev)) {
			ret = PTR_ERR(bdev);
			goto out;
		}

		set_blocksize(bdev, 4096);
		invalidate_bdev(bdev);
		bh = btrfs_read_dev_super(bdev);
		if (!bh) {
			ret = -EINVAL;
			goto error_close;
		}
		disk_super = (struct btrfs_super_block *)bh->b_data;
		devid = btrfs_stack_device_id(&disk_super->dev_item);
		dev_uuid = disk_super->dev_item.uuid;
		device = btrfs_find_device(root, devid, dev_uuid,
					   disk_super->fsid);
		if (!device) {
			ret = -ENOENT;
			goto error_brelse;
		}
	}

	if (device->writeable && root->fs_info->fs_devices->rw_devices == 1) {
		printk(KERN_ERR "btrfs: unable to remove the only writeable "
		       "device\n");
		ret = -EINVAL;
		goto error_brelse;
	}

	if (device->writeable) {
		lock_chunks(root);
		list_del_init(&device->dev_alloc_list);
		unlock_chunks(root);
		root->fs_info->fs_devices->rw_devices--;
		clear_super = true;
	}

	ret = btrfs_shrink_device(device, 0);
	if (ret)
		goto error_undo;

	ret = btrfs_rm_dev_item(root->fs_info->chunk_root, device);
	if (ret)
		goto error_undo;

	spin_lock(&root->fs_info->free_chunk_lock);
	root->fs_info->free_chunk_space = device->total_bytes -
		device->bytes_used;
	spin_unlock(&root->fs_info->free_chunk_lock);

	device->in_fs_metadata = 0;
	btrfs_scrub_cancel_dev(root, device);

	/*
	 * the device list mutex makes sure that we don't change
	 * the device list while someone else is writing out all
	 * the device supers.
	 */

	cur_devices = device->fs_devices;
	mutex_lock(&root->fs_info->fs_devices->device_list_mutex);
	list_del_rcu(&device->dev_list);

	device->fs_devices->num_devices--;

	if (device->missing)
		root->fs_info->fs_devices->missing_devices--;

	next_device = list_entry(root->fs_info->fs_devices->devices.next,
				 struct btrfs_device, dev_list);
	if (device->bdev == root->fs_info->sb->s_bdev)
		root->fs_info->sb->s_bdev = next_device->bdev;
	if (device->bdev == root->fs_info->fs_devices->latest_bdev)
		root->fs_info->fs_devices->latest_bdev = next_device->bdev;

	if (device->bdev)
		device->fs_devices->open_devices--;

	call_rcu(&device->rcu, free_device);
	mutex_unlock(&root->fs_info->fs_devices->device_list_mutex);

	num_devices = btrfs_super_num_devices(root->fs_info->super_copy) - 1;
	btrfs_set_super_num_devices(root->fs_info->super_copy, num_devices);

	if (cur_devices->open_devices == 0) {
		struct btrfs_fs_devices *fs_devices;
		fs_devices = root->fs_info->fs_devices;
		while (fs_devices) {
			if (fs_devices->seed == cur_devices)
				break;
			fs_devices = fs_devices->seed;
		}
		fs_devices->seed = cur_devices->seed;
		cur_devices->seed = NULL;
		lock_chunks(root);
		__btrfs_close_devices(cur_devices);
		unlock_chunks(root);
		free_fs_devices(cur_devices);
	}

	/*
	 * at this point, the device is zero sized.  We want to
	 * remove it from the devices list and zero out the old super
	 */
	if (clear_super) {
		/* make sure this device isn't detected as part of
		 * the FS anymore
		 */
		memset(&disk_super->magic, 0, sizeof(disk_super->magic));
		set_buffer_dirty(bh);
		sync_dirty_buffer(bh);
	}

	ret = 0;

error_brelse:
	brelse(bh);
error_close:
	if (bdev)
		blkdev_put(bdev, FMODE_READ | FMODE_EXCL);
out:
	mutex_unlock(&uuid_mutex);
	return ret;
error_undo:
	if (device->writeable) {
		lock_chunks(root);
		list_add(&device->dev_alloc_list,
			 &root->fs_info->fs_devices->alloc_list);
		unlock_chunks(root);
		root->fs_info->fs_devices->rw_devices++;
	}
	goto error_brelse;
}

/*
 * does all the dirty work required for changing file system's UUID.
 */
static int btrfs_prepare_sprout(struct btrfs_root *root)
{
	struct btrfs_fs_devices *fs_devices = root->fs_info->fs_devices;
	struct btrfs_fs_devices *old_devices;
	struct btrfs_fs_devices *seed_devices;
	struct btrfs_super_block *disk_super = root->fs_info->super_copy;
	struct btrfs_device *device;
	u64 super_flags;

	BUG_ON(!mutex_is_locked(&uuid_mutex));
	if (!fs_devices->seeding)
		return -EINVAL;

	seed_devices = kzalloc(sizeof(*fs_devices), GFP_NOFS);
	if (!seed_devices)
		return -ENOMEM;

	old_devices = clone_fs_devices(fs_devices);
	if (IS_ERR(old_devices)) {
		kfree(seed_devices);
		return PTR_ERR(old_devices);
	}

	list_add(&old_devices->list, &fs_uuids);

	memcpy(seed_devices, fs_devices, sizeof(*seed_devices));
	seed_devices->opened = 1;
	INIT_LIST_HEAD(&seed_devices->devices);
	INIT_LIST_HEAD(&seed_devices->alloc_list);
	mutex_init(&seed_devices->device_list_mutex);

	mutex_lock(&root->fs_info->fs_devices->device_list_mutex);
	list_splice_init_rcu(&fs_devices->devices, &seed_devices->devices,
			      synchronize_rcu);
	mutex_unlock(&root->fs_info->fs_devices->device_list_mutex);

	list_splice_init(&fs_devices->alloc_list, &seed_devices->alloc_list);
	list_for_each_entry(device, &seed_devices->devices, dev_list) {
		device->fs_devices = seed_devices;
	}

	fs_devices->seeding = 0;
	fs_devices->num_devices = 0;
	fs_devices->open_devices = 0;
	fs_devices->seed = seed_devices;

	generate_random_uuid(fs_devices->fsid);
	memcpy(root->fs_info->fsid, fs_devices->fsid, BTRFS_FSID_SIZE);
	memcpy(disk_super->fsid, fs_devices->fsid, BTRFS_FSID_SIZE);
	super_flags = btrfs_super_flags(disk_super) &
		      ~BTRFS_SUPER_FLAG_SEEDING;
	btrfs_set_super_flags(disk_super, super_flags);

	return 0;
}

/*
 * strore the expected generation for seed devices in device items.
 */
static int btrfs_finish_sprout(struct btrfs_trans_handle *trans,
			       struct btrfs_root *root)
{
	struct btrfs_path *path;
	struct extent_buffer *leaf;
	struct btrfs_dev_item *dev_item;
	struct btrfs_device *device;
	struct btrfs_key key;
	u8 fs_uuid[BTRFS_UUID_SIZE];
	u8 dev_uuid[BTRFS_UUID_SIZE];
	u64 devid;
	int ret;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	root = root->fs_info->chunk_root;
	key.objectid = BTRFS_DEV_ITEMS_OBJECTID;
	key.offset = 0;
	key.type = BTRFS_DEV_ITEM_KEY;

	while (1) {
		ret = btrfs_search_slot(trans, root, &key, path, 0, 1);
		if (ret < 0)
			goto error;

		leaf = path->nodes[0];
next_slot:
		if (path->slots[0] >= btrfs_header_nritems(leaf)) {
			ret = btrfs_next_leaf(root, path);
			if (ret > 0)
				break;
			if (ret < 0)
				goto error;
			leaf = path->nodes[0];
			btrfs_item_key_to_cpu(leaf, &key, path->slots[0]);
			btrfs_release_path(path);
			continue;
		}

		btrfs_item_key_to_cpu(leaf, &key, path->slots[0]);
		if (key.objectid != BTRFS_DEV_ITEMS_OBJECTID ||
		    key.type != BTRFS_DEV_ITEM_KEY)
			break;

		dev_item = btrfs_item_ptr(leaf, path->slots[0],
					  struct btrfs_dev_item);
		devid = btrfs_device_id(leaf, dev_item);
		read_extent_buffer(leaf, dev_uuid,
				   (unsigned long)btrfs_device_uuid(dev_item),
				   BTRFS_UUID_SIZE);
		read_extent_buffer(leaf, fs_uuid,
				   (unsigned long)btrfs_device_fsid(dev_item),
				   BTRFS_UUID_SIZE);
		device = btrfs_find_device(root, devid, dev_uuid, fs_uuid);
		BUG_ON(!device); /* Logic error */

		if (device->fs_devices->seeding) {
			btrfs_set_device_generation(leaf, dev_item,
						    device->generation);
			btrfs_mark_buffer_dirty(leaf);
		}

		path->slots[0]++;
		goto next_slot;
	}
	ret = 0;
error:
	btrfs_free_path(path);
	return ret;
}

int btrfs_init_new_device(struct btrfs_root *root, char *device_path)
{
	struct request_queue *q;
	struct btrfs_trans_handle *trans;
	struct btrfs_device *device;
	struct block_device *bdev;
	struct list_head *devices;
	struct super_block *sb = root->fs_info->sb;
	struct rcu_string *name;
	u64 total_bytes;
	int seeding_dev = 0;
	int ret = 0;

	if ((sb->s_flags & MS_RDONLY) && !root->fs_info->fs_devices->seeding)
		return -EROFS;

	bdev = blkdev_get_by_path(device_path, FMODE_WRITE | FMODE_EXCL,
				  root->fs_info->bdev_holder);
	if (IS_ERR(bdev))
		return PTR_ERR(bdev);

	if (root->fs_info->fs_devices->seeding) {
		seeding_dev = 1;
		down_write(&sb->s_umount);
		mutex_lock(&uuid_mutex);
	}

	filemap_write_and_wait(bdev->bd_inode->i_mapping);

	devices = &root->fs_info->fs_devices->devices;
	/*
	 * we have the volume lock, so we don't need the extra
	 * device list mutex while reading the list here.
	 */
	list_for_each_entry(device, devices, dev_list) {
		if (device->bdev == bdev) {
			ret = -EEXIST;
			goto error;
		}
	}

	device = kzalloc(sizeof(*device), GFP_NOFS);
	if (!device) {
		/* we can safely leave the fs_devices entry around */
		ret = -ENOMEM;
		goto error;
	}

	name = rcu_string_strdup(device_path, GFP_NOFS);
	if (!name) {
		kfree(device);
		ret = -ENOMEM;
		goto error;
	}
	rcu_assign_pointer(device->name, name);

	ret = find_next_devid(root, &device->devid);
	if (ret) {
		rcu_string_free(device->name);
		kfree(device);
		goto error;
	}

	trans = btrfs_start_transaction(root, 0);
	if (IS_ERR(trans)) {
		rcu_string_free(device->name);
		kfree(device);
		ret = PTR_ERR(trans);
		goto error;
	}

	lock_chunks(root);

	q = bdev_get_queue(bdev);
	if (blk_queue_discard(q))
		device->can_discard = 1;
	device->writeable = 1;
	device->work.func = pending_bios_fn;
	generate_random_uuid(device->uuid);
	spin_lock_init(&device->io_lock);
	device->generation = trans->transid;
	device->io_width = root->sectorsize;
	device->io_align = root->sectorsize;
	device->sector_size = root->sectorsize;
	device->total_bytes = i_size_read(bdev->bd_inode);
	device->disk_total_bytes = device->total_bytes;
	device->dev_root = root->fs_info->dev_root;
	device->bdev = bdev;
	device->in_fs_metadata = 1;
	device->mode = FMODE_EXCL;
	set_blocksize(device->bdev, 4096);

	if (seeding_dev) {
		sb->s_flags &= ~MS_RDONLY;
		ret = btrfs_prepare_sprout(root);
		BUG_ON(ret); /* -ENOMEM */
	}

	device->fs_devices = root->fs_info->fs_devices;

	/*
	 * we don't want write_supers to jump in here with our device
	 * half setup
	 */
	mutex_lock(&root->fs_info->fs_devices->device_list_mutex);
	list_add_rcu(&device->dev_list, &root->fs_info->fs_devices->devices);
	list_add(&device->dev_alloc_list,
		 &root->fs_info->fs_devices->alloc_list);
	root->fs_info->fs_devices->num_devices++;
	root->fs_info->fs_devices->open_devices++;
	root->fs_info->fs_devices->rw_devices++;
	if (device->can_discard)
		root->fs_info->fs_devices->num_can_discard++;
	root->fs_info->fs_devices->total_rw_bytes += device->total_bytes;

	spin_lock(&root->fs_info->free_chunk_lock);
	root->fs_info->free_chunk_space += device->total_bytes;
	spin_unlock(&root->fs_info->free_chunk_lock);

	if (!blk_queue_nonrot(bdev_get_queue(bdev)))
		root->fs_info->fs_devices->rotating = 1;

	total_bytes = btrfs_super_total_bytes(root->fs_info->super_copy);
	btrfs_set_super_total_bytes(root->fs_info->super_copy,
				    total_bytes + device->total_bytes);

	total_bytes = btrfs_super_num_devices(root->fs_info->super_copy);
	btrfs_set_super_num_devices(root->fs_info->super_copy,
				    total_bytes + 1);
	mutex_unlock(&root->fs_info->fs_devices->device_list_mutex);

	if (seeding_dev) {
		ret = init_first_rw_device(trans, root, device);
		if (ret)
			goto error_trans;
		ret = btrfs_finish_sprout(trans, root);
		if (ret)
			goto error_trans;
	} else {
		ret = btrfs_add_device(trans, root, device);
		if (ret)
			goto error_trans;
	}

	/*
	 * we've got more storage, clear any full flags on the space
	 * infos
	 */
	btrfs_clear_space_info_full(root->fs_info);

	unlock_chunks(root);
	ret = btrfs_commit_transaction(trans, root);

	if (seeding_dev) {
		mutex_unlock(&uuid_mutex);
		up_write(&sb->s_umount);

		if (ret) /* transaction commit */
			return ret;

		ret = btrfs_relocate_sys_chunks(root);
		if (ret < 0)
			btrfs_error(root->fs_info, ret,
				    "Failed to relocate sys chunks after "
				    "device initialization. This can be fixed "
				    "using the \"btrfs balance\" command.");
	}

	return ret;

error_trans:
	unlock_chunks(root);
	btrfs_abort_transaction(trans, root, ret);
	btrfs_end_transaction(trans, root);
	rcu_string_free(device->name);
	kfree(device);
error:
	blkdev_put(bdev, FMODE_EXCL);
	if (seeding_dev) {
		mutex_unlock(&uuid_mutex);
		up_write(&sb->s_umount);
	}
	return ret;
}

static noinline int btrfs_update_device(struct btrfs_trans_handle *trans,
					struct btrfs_device *device)
{
	int ret;
	struct btrfs_path *path;
	struct btrfs_root *root;
	struct btrfs_dev_item *dev_item;
	struct extent_buffer *leaf;
	struct btrfs_key key;

	root = device->dev_root->fs_info->chunk_root;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	key.objectid = BTRFS_DEV_ITEMS_OBJECTID;
	key.type = BTRFS_DEV_ITEM_KEY;
	key.offset = device->devid;

	ret = btrfs_search_slot(trans, root, &key, path, 0, 1);
	if (ret < 0)
		goto out;

	if (ret > 0) {
		ret = -ENOENT;
		goto out;
	}

	leaf = path->nodes[0];
	dev_item = btrfs_item_ptr(leaf, path->slots[0], struct btrfs_dev_item);

	btrfs_set_device_id(leaf, dev_item, device->devid);
	btrfs_set_device_type(leaf, dev_item, device->type);
	btrfs_set_device_io_align(leaf, dev_item, device->io_align);
	btrfs_set_device_io_width(leaf, dev_item, device->io_width);
	btrfs_set_device_sector_size(leaf, dev_item, device->sector_size);
	btrfs_set_device_total_bytes(leaf, dev_item, device->disk_total_bytes);
	btrfs_set_device_bytes_used(leaf, dev_item, device->bytes_used);
	btrfs_mark_buffer_dirty(leaf);

out:
	btrfs_free_path(path);
	return ret;
}

static int __btrfs_grow_device(struct btrfs_trans_handle *trans,
		      struct btrfs_device *device, u64 new_size)
{
	struct btrfs_super_block *super_copy =
		device->dev_root->fs_info->super_copy;
	u64 old_total = btrfs_super_total_bytes(super_copy);
	u64 diff = new_size - device->total_bytes;

	if (!device->writeable)
		return -EACCES;
	if (new_size <= device->total_bytes)
		return -EINVAL;

	btrfs_set_super_total_bytes(super_copy, old_total + diff);
	device->fs_devices->total_rw_bytes += diff;

	device->total_bytes = new_size;
	device->disk_total_bytes = new_size;
	btrfs_clear_space_info_full(device->dev_root->fs_info);

	return btrfs_update_device(trans, device);
}

int btrfs_grow_device(struct btrfs_trans_handle *trans,
		      struct btrfs_device *device, u64 new_size)
{
	int ret;
	lock_chunks(device->dev_root);
	ret = __btrfs_grow_device(trans, device, new_size);
	unlock_chunks(device->dev_root);
	return ret;
}

static int btrfs_free_chunk(struct btrfs_trans_handle *trans,
			    struct btrfs_root *root,
			    u64 chunk_tree, u64 chunk_objectid,
			    u64 chunk_offset)
{
	int ret;
	struct btrfs_path *path;
	struct btrfs_key key;

	root = root->fs_info->chunk_root;
	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	key.objectid = chunk_objectid;
	key.offset = chunk_offset;
	key.type = BTRFS_CHUNK_ITEM_KEY;

	ret = btrfs_search_slot(trans, root, &key, path, -1, 1);
	if (ret < 0)
		goto out;
	else if (ret > 0) { /* Logic error or corruption */
		btrfs_error(root->fs_info, -ENOENT,
			    "Failed lookup while freeing chunk.");
		ret = -ENOENT;
		goto out;
	}

	ret = btrfs_del_item(trans, root, path);
	if (ret < 0)
		btrfs_error(root->fs_info, ret,
			    "Failed to delete chunk item.");
out:
	btrfs_free_path(path);
	return ret;
}

static int btrfs_del_sys_chunk(struct btrfs_root *root, u64 chunk_objectid, u64
			chunk_offset)
{
	struct btrfs_super_block *super_copy = root->fs_info->super_copy;
	struct btrfs_disk_key *disk_key;
	struct btrfs_chunk *chunk;
	u8 *ptr;
	int ret = 0;
	u32 num_stripes;
	u32 array_size;
	u32 len = 0;
	u32 cur;
	struct btrfs_key key;

	array_size = btrfs_super_sys_array_size(super_copy);

	ptr = super_copy->sys_chunk_array;
	cur = 0;

	while (cur < array_size) {
		disk_key = (struct btrfs_disk_key *)ptr;
		btrfs_disk_key_to_cpu(&key, disk_key);

		len = sizeof(*disk_key);

		if (key.type == BTRFS_CHUNK_ITEM_KEY) {
			chunk = (struct btrfs_chunk *)(ptr + len);
			num_stripes = btrfs_stack_chunk_num_stripes(chunk);
			len += btrfs_chunk_item_size(num_stripes);
		} else {
			ret = -EIO;
			break;
		}
		if (key.objectid == chunk_objectid &&
		    key.offset == chunk_offset) {
			memmove(ptr, ptr + len, array_size - (cur + len));
			array_size -= len;
			btrfs_set_super_sys_array_size(super_copy, array_size);
		} else {
			ptr += len;
			cur += len;
		}
	}
	return ret;
}

static int btrfs_relocate_chunk(struct btrfs_root *root,
			 u64 chunk_tree, u64 chunk_objectid,
			 u64 chunk_offset)
{
	struct extent_map_tree *em_tree;
	struct btrfs_root *extent_root;
	struct btrfs_trans_handle *trans;
	struct extent_map *em;
	struct map_lookup *map;
	int ret;
	int i;

	root = root->fs_info->chunk_root;
	extent_root = root->fs_info->extent_root;
	em_tree = &root->fs_info->mapping_tree.map_tree;

	ret = btrfs_can_relocate(extent_root, chunk_offset);
	if (ret)
		return -ENOSPC;

	/* step one, relocate all the extents inside this chunk */
	ret = btrfs_relocate_block_group(extent_root, chunk_offset);
	if (ret)
		return ret;

	trans = btrfs_start_transaction(root, 0);
	BUG_ON(IS_ERR(trans));

	lock_chunks(root);

	/*
	 * step two, delete the device extents and the
	 * chunk tree entries
	 */
	read_lock(&em_tree->lock);
	em = lookup_extent_mapping(em_tree, chunk_offset, 1);
	read_unlock(&em_tree->lock);

	BUG_ON(!em || em->start > chunk_offset ||
	       em->start + em->len < chunk_offset);
	map = (struct map_lookup *)em->bdev;

	for (i = 0; i < map->num_stripes; i++) {
		ret = btrfs_free_dev_extent(trans, map->stripes[i].dev,
					    map->stripes[i].physical);
		BUG_ON(ret);

		if (map->stripes[i].dev) {
			ret = btrfs_update_device(trans, map->stripes[i].dev);
			BUG_ON(ret);
		}
	}
	ret = btrfs_free_chunk(trans, root, chunk_tree, chunk_objectid,
			       chunk_offset);

	BUG_ON(ret);

	trace_btrfs_chunk_free(root, map, chunk_offset, em->len);

	if (map->type & BTRFS_BLOCK_GROUP_SYSTEM) {
		ret = btrfs_del_sys_chunk(root, chunk_objectid, chunk_offset);
		BUG_ON(ret);
	}

	ret = btrfs_remove_block_group(trans, extent_root, chunk_offset);
	BUG_ON(ret);

	write_lock(&em_tree->lock);
	remove_extent_mapping(em_tree, em);
	write_unlock(&em_tree->lock);

	kfree(map);
	em->bdev = NULL;

	/* once for the tree */
	free_extent_map(em);
	/* once for us */
	free_extent_map(em);

	unlock_chunks(root);
	btrfs_end_transaction(trans, root);
	return 0;
}

static int btrfs_relocate_sys_chunks(struct btrfs_root *root)
{
	struct btrfs_root *chunk_root = root->fs_info->chunk_root;
	struct btrfs_path *path;
	struct extent_buffer *leaf;
	struct btrfs_chunk *chunk;
	struct btrfs_key key;
	struct btrfs_key found_key;
	u64 chunk_tree = chunk_root->root_key.objectid;
	u64 chunk_type;
	bool retried = false;
	int failed = 0;
	int ret;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

again:
	key.objectid = BTRFS_FIRST_CHUNK_TREE_OBJECTID;
	key.offset = (u64)-1;
	key.type = BTRFS_CHUNK_ITEM_KEY;

	while (1) {
		ret = btrfs_search_slot(NULL, chunk_root, &key, path, 0, 0);
		if (ret < 0)
			goto error;
		BUG_ON(ret == 0); /* Corruption */

		ret = btrfs_previous_item(chunk_root, path, key.objectid,
					  key.type);
		if (ret < 0)
			goto error;
		if (ret > 0)
			break;

		leaf = path->nodes[0];
		btrfs_item_key_to_cpu(leaf, &found_key, path->slots[0]);

		chunk = btrfs_item_ptr(leaf, path->slots[0],
				       struct btrfs_chunk);
		chunk_type = btrfs_chunk_type(leaf, chunk);
		btrfs_release_path(path);

		if (chunk_type & BTRFS_BLOCK_GROUP_SYSTEM) {
			ret = btrfs_relocate_chunk(chunk_root, chunk_tree,
						   found_key.objectid,
						   found_key.offset);
			if (ret == -ENOSPC)
				failed++;
			else if (ret)
				BUG();
		}

		if (found_key.offset == 0)
			break;
		key.offset = found_key.offset - 1;
	}
	ret = 0;
	if (failed && !retried) {
		failed = 0;
		retried = true;
		goto again;
	} else if (failed && retried) {
		WARN_ON(1);
		ret = -ENOSPC;
	}
error:
	btrfs_free_path(path);
	return ret;
}

static int insert_balance_item(struct btrfs_root *root,
			       struct btrfs_balance_control *bctl)
{
	struct btrfs_trans_handle *trans;
	struct btrfs_balance_item *item;
	struct btrfs_disk_balance_args disk_bargs;
	struct btrfs_path *path;
	struct extent_buffer *leaf;
	struct btrfs_key key;
	int ret, err;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	trans = btrfs_start_transaction(root, 0);
	if (IS_ERR(trans)) {
		btrfs_free_path(path);
		return PTR_ERR(trans);
	}

	key.objectid = BTRFS_BALANCE_OBJECTID;
	key.type = BTRFS_BALANCE_ITEM_KEY;
	key.offset = 0;

	ret = btrfs_insert_empty_item(trans, root, path, &key,
				      sizeof(*item));
	if (ret)
		goto out;

	leaf = path->nodes[0];
	item = btrfs_item_ptr(leaf, path->slots[0], struct btrfs_balance_item);

	memset_extent_buffer(leaf, 0, (unsigned long)item, sizeof(*item));

	btrfs_cpu_balance_args_to_disk(&disk_bargs, &bctl->data);
	btrfs_set_balance_data(leaf, item, &disk_bargs);
	btrfs_cpu_balance_args_to_disk(&disk_bargs, &bctl->meta);
	btrfs_set_balance_meta(leaf, item, &disk_bargs);
	btrfs_cpu_balance_args_to_disk(&disk_bargs, &bctl->sys);
	btrfs_set_balance_sys(leaf, item, &disk_bargs);

	btrfs_set_balance_flags(leaf, item, bctl->flags);

	btrfs_mark_buffer_dirty(leaf);
out:
	btrfs_free_path(path);
	err = btrfs_commit_transaction(trans, root);
	if (err && !ret)
		ret = err;
	return ret;
}

static int del_balance_item(struct btrfs_root *root)
{
	struct btrfs_trans_handle *trans;
	struct btrfs_path *path;
	struct btrfs_key key;
	int ret, err;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	trans = btrfs_start_transaction(root, 0);
	if (IS_ERR(trans)) {
		btrfs_free_path(path);
		return PTR_ERR(trans);
	}

	key.objectid = BTRFS_BALANCE_OBJECTID;
	key.type = BTRFS_BALANCE_ITEM_KEY;
	key.offset = 0;

	ret = btrfs_search_slot(trans, root, &key, path, -1, 1);
	if (ret < 0)
		goto out;
	if (ret > 0) {
		ret = -ENOENT;
		goto out;
	}

	ret = btrfs_del_item(trans, root, path);
out:
	btrfs_free_path(path);
	err = btrfs_commit_transaction(trans, root);
	if (err && !ret)
		ret = err;
	return ret;
}

/*
 * This is a heuristic used to reduce the number of chunks balanced on
 * resume after balance was interrupted.
 */
static void update_balance_args(struct btrfs_balance_control *bctl)
{
	/*
	 * Turn on soft mode for chunk types that were being converted.
	 */
	if (bctl->data.flags & BTRFS_BALANCE_ARGS_CONVERT)
		bctl->data.flags |= BTRFS_BALANCE_ARGS_SOFT;
	if (bctl->sys.flags & BTRFS_BALANCE_ARGS_CONVERT)
		bctl->sys.flags |= BTRFS_BALANCE_ARGS_SOFT;
	if (bctl->meta.flags & BTRFS_BALANCE_ARGS_CONVERT)
		bctl->meta.flags |= BTRFS_BALANCE_ARGS_SOFT;

	/*
	 * Turn on usage filter if is not already used.  The idea is
	 * that chunks that we have already balanced should be
	 * reasonably full.  Don't do it for chunks that are being
	 * converted - that will keep us from relocating unconverted
	 * (albeit full) chunks.
	 */
	if (!(bctl->data.flags & BTRFS_BALANCE_ARGS_USAGE) &&
	    !(bctl->data.flags & BTRFS_BALANCE_ARGS_CONVERT)) {
		bctl->data.flags |= BTRFS_BALANCE_ARGS_USAGE;
		bctl->data.usage = 90;
	}
	if (!(bctl->sys.flags & BTRFS_BALANCE_ARGS_USAGE) &&
	    !(bctl->sys.flags & BTRFS_BALANCE_ARGS_CONVERT)) {
		bctl->sys.flags |= BTRFS_BALANCE_ARGS_USAGE;
		bctl->sys.usage = 90;
	}
	if (!(bctl->meta.flags & BTRFS_BALANCE_ARGS_USAGE) &&
	    !(bctl->meta.flags & BTRFS_BALANCE_ARGS_CONVERT)) {
		bctl->meta.flags |= BTRFS_BALANCE_ARGS_USAGE;
		bctl->meta.usage = 90;
	}
}

/*
 * Should be called with both balance and volume mutexes held to
 * serialize other volume operations (add_dev/rm_dev/resize) with
 * restriper.  Same goes for unset_balance_control.
 */
static void set_balance_control(struct btrfs_balance_control *bctl)
{
	struct btrfs_fs_info *fs_info = bctl->fs_info;

	BUG_ON(fs_info->balance_ctl);

	spin_lock(&fs_info->balance_lock);
	fs_info->balance_ctl = bctl;
	spin_unlock(&fs_info->balance_lock);
}

static void unset_balance_control(struct btrfs_fs_info *fs_info)
{
	struct btrfs_balance_control *bctl = fs_info->balance_ctl;

	BUG_ON(!fs_info->balance_ctl);

	spin_lock(&fs_info->balance_lock);
	fs_info->balance_ctl = NULL;
	spin_unlock(&fs_info->balance_lock);

	kfree(bctl);
}

/*
 * Balance filters.  Return 1 if chunk should be filtered out
 * (should not be balanced).
 */
static int chunk_profiles_filter(u64 chunk_type,
				 struct btrfs_balance_args *bargs)
{
	chunk_type = chunk_to_extended(chunk_type) &
				BTRFS_EXTENDED_PROFILE_MASK;

	if (bargs->profiles & chunk_type)
		return 0;

	return 1;
}

static u64 div_factor_fine(u64 num, int factor)
{
	if (factor <= 0)
		return 0;
	if (factor >= 100)
		return num;

	num *= factor;
	do_div(num, 100);
	return num;
}

static int chunk_usage_filter(struct btrfs_fs_info *fs_info, u64 chunk_offset,
			      struct btrfs_balance_args *bargs)
{
	struct btrfs_block_group_cache *cache;
	u64 chunk_used, user_thresh;
	int ret = 1;

	cache = btrfs_lookup_block_group(fs_info, chunk_offset);
	chunk_used = btrfs_block_group_used(&cache->item);

	user_thresh = div_factor_fine(cache->key.offset, bargs->usage);
	if (chunk_used < user_thresh)
		ret = 0;

	btrfs_put_block_group(cache);
	return ret;
}

static int chunk_devid_filter(struct extent_buffer *leaf,
			      struct btrfs_chunk *chunk,
			      struct btrfs_balance_args *bargs)
{
	struct btrfs_stripe *stripe;
	int num_stripes = btrfs_chunk_num_stripes(leaf, chunk);
	int i;

	for (i = 0; i < num_stripes; i++) {
		stripe = btrfs_stripe_nr(chunk, i);
		if (btrfs_stripe_devid(leaf, stripe) == bargs->devid)
			return 0;
	}

	return 1;
}

/* [pstart, pend) */
static int chunk_drange_filter(struct extent_buffer *leaf,
			       struct btrfs_chunk *chunk,
			       u64 chunk_offset,
			       struct btrfs_balance_args *bargs)
{
	struct btrfs_stripe *stripe;
	int num_stripes = btrfs_chunk_num_stripes(leaf, chunk);
	u64 stripe_offset;
	u64 stripe_length;
	int factor;
	int i;

	if (!(bargs->flags & BTRFS_BALANCE_ARGS_DEVID))
		return 0;

	if (btrfs_chunk_type(leaf, chunk) & (BTRFS_BLOCK_GROUP_DUP |
	     BTRFS_BLOCK_GROUP_RAID1 | BTRFS_BLOCK_GROUP_RAID10))
		factor = 2;
	else
		factor = 1;
	factor = num_stripes / factor;

	for (i = 0; i < num_stripes; i++) {
		stripe = btrfs_stripe_nr(chunk, i);
		if (btrfs_stripe_devid(leaf, stripe) != bargs->devid)
			continue;

		stripe_offset = btrfs_stripe_offset(leaf, stripe);
		stripe_length = btrfs_chunk_length(leaf, chunk);
		do_div(stripe_length, factor);

		if (stripe_offset < bargs->pend &&
		    stripe_offset + stripe_length > bargs->pstart)
			return 0;
	}

	return 1;
}

/* [vstart, vend) */
static int chunk_vrange_filter(struct extent_buffer *leaf,
			       struct btrfs_chunk *chunk,
			       u64 chunk_offset,
			       struct btrfs_balance_args *bargs)
{
	if (chunk_offset < bargs->vend &&
	    chunk_offset + btrfs_chunk_length(leaf, chunk) > bargs->vstart)
		/* at least part of the chunk is inside this vrange */
		return 0;

	return 1;
}

static int chunk_soft_convert_filter(u64 chunk_type,
				     struct btrfs_balance_args *bargs)
{
	if (!(bargs->flags & BTRFS_BALANCE_ARGS_CONVERT))
		return 0;

	chunk_type = chunk_to_extended(chunk_type) &
				BTRFS_EXTENDED_PROFILE_MASK;

	if (bargs->target == chunk_type)
		return 1;

	return 0;
}

static int should_balance_chunk(struct btrfs_root *root,
				struct extent_buffer *leaf,
				struct btrfs_chunk *chunk, u64 chunk_offset)
{
	struct btrfs_balance_control *bctl = root->fs_info->balance_ctl;
	struct btrfs_balance_args *bargs = NULL;
	u64 chunk_type = btrfs_chunk_type(leaf, chunk);

	/* type filter */
	if (!((chunk_type & BTRFS_BLOCK_GROUP_TYPE_MASK) &
	      (bctl->flags & BTRFS_BALANCE_TYPE_MASK))) {
		return 0;
	}

	if (chunk_type & BTRFS_BLOCK_GROUP_DATA)
		bargs = &bctl->data;
	else if (chunk_type & BTRFS_BLOCK_GROUP_SYSTEM)
		bargs = &bctl->sys;
	else if (chunk_type & BTRFS_BLOCK_GROUP_METADATA)
		bargs = &bctl->meta;

	/* profiles filter */
	if ((bargs->flags & BTRFS_BALANCE_ARGS_PROFILES) &&
	    chunk_profiles_filter(chunk_type, bargs)) {
		return 0;
	}

	/* usage filter */
	if ((bargs->flags & BTRFS_BALANCE_ARGS_USAGE) &&
	    chunk_usage_filter(bctl->fs_info, chunk_offset, bargs)) {
		return 0;
	}

	/* devid filter */
	if ((bargs->flags & BTRFS_BALANCE_ARGS_DEVID) &&
	    chunk_devid_filter(leaf, chunk, bargs)) {
		return 0;
	}

	/* drange filter, makes sense only with devid filter */
	if ((bargs->flags & BTRFS_BALANCE_ARGS_DRANGE) &&
	    chunk_drange_filter(leaf, chunk, chunk_offset, bargs)) {
		return 0;
	}

	/* vrange filter */
	if ((bargs->flags & BTRFS_BALANCE_ARGS_VRANGE) &&
	    chunk_vrange_filter(leaf, chunk, chunk_offset, bargs)) {
		return 0;
	}

	/* soft profile changing mode */
	if ((bargs->flags & BTRFS_BALANCE_ARGS_SOFT) &&
	    chunk_soft_convert_filter(chunk_type, bargs)) {
		return 0;
	}

	return 1;
}

static u64 div_factor(u64 num, int factor)
{
	if (factor == 10)
		return num;
	num *= factor;
	do_div(num, 10);
	return num;
}

static int __btrfs_balance(struct btrfs_fs_info *fs_info)
{
	struct btrfs_balance_control *bctl = fs_info->balance_ctl;
	struct btrfs_root *chunk_root = fs_info->chunk_root;
	struct btrfs_root *dev_root = fs_info->dev_root;
	struct list_head *devices;
	struct btrfs_device *device;
	u64 old_size;
	u64 size_to_free;
	struct btrfs_chunk *chunk;
	struct btrfs_path *path;
	struct btrfs_key key;
	struct btrfs_key found_key;
	struct btrfs_trans_handle *trans;
	struct extent_buffer *leaf;
	int slot;
	int ret;
	int enospc_errors = 0;
	bool counting = true;

	/* step one make some room on all the devices */
	devices = &fs_info->fs_devices->devices;
	list_for_each_entry(device, devices, dev_list) {
		old_size = device->total_bytes;
		size_to_free = div_factor(old_size, 1);
		size_to_free = min(size_to_free, (u64)1 * 1024 * 1024);
		if (!device->writeable ||
		    device->total_bytes - device->bytes_used > size_to_free)
			continue;

		ret = btrfs_shrink_device(device, old_size - size_to_free);
		if (ret == -ENOSPC)
			break;
		BUG_ON(ret);

		trans = btrfs_start_transaction(dev_root, 0);
		BUG_ON(IS_ERR(trans));

		ret = btrfs_grow_device(trans, device, old_size);
		BUG_ON(ret);

		btrfs_end_transaction(trans, dev_root);
	}

	/* step two, relocate all the chunks */
	path = btrfs_alloc_path();
	if (!path) {
		ret = -ENOMEM;
		goto error;
	}

	/* zero out stat counters */
	spin_lock(&fs_info->balance_lock);
	memset(&bctl->stat, 0, sizeof(bctl->stat));
	spin_unlock(&fs_info->balance_lock);
again:
	key.objectid = BTRFS_FIRST_CHUNK_TREE_OBJECTID;
	key.offset = (u64)-1;
	key.type = BTRFS_CHUNK_ITEM_KEY;

	while (1) {
		if ((!counting && atomic_read(&fs_info->balance_pause_req)) ||
		    atomic_read(&fs_info->balance_cancel_req)) {
			ret = -ECANCELED;
			goto error;
		}

		ret = btrfs_search_slot(NULL, chunk_root, &key, path, 0, 0);
		if (ret < 0)
			goto error;

		/*
		 * this shouldn't happen, it means the last relocate
		 * failed
		 */
		if (ret == 0)
			BUG(); /* FIXME break ? */

		ret = btrfs_previous_item(chunk_root, path, 0,
					  BTRFS_CHUNK_ITEM_KEY);
		if (ret) {
			ret = 0;
			break;
		}

		leaf = path->nodes[0];
		slot = path->slots[0];
		btrfs_item_key_to_cpu(leaf, &found_key, slot);

		if (found_key.objectid != key.objectid)
			break;

		/* chunk zero is special */
		if (found_key.offset == 0)
			break;

		chunk = btrfs_item_ptr(leaf, slot, struct btrfs_chunk);

		if (!counting) {
			spin_lock(&fs_info->balance_lock);
			bctl->stat.considered++;
			spin_unlock(&fs_info->balance_lock);
		}

		ret = should_balance_chunk(chunk_root, leaf, chunk,
					   found_key.offset);
		btrfs_release_path(path);
		if (!ret)
			goto loop;

		if (counting) {
			spin_lock(&fs_info->balance_lock);
			bctl->stat.expected++;
			spin_unlock(&fs_info->balance_lock);
			goto loop;
		}

		ret = btrfs_relocate_chunk(chunk_root,
					   chunk_root->root_key.objectid,
					   found_key.objectid,
					   found_key.offset);
		if (ret && ret != -ENOSPC)
			goto error;
		if (ret == -ENOSPC) {
			enospc_errors++;
		} else {
			spin_lock(&fs_info->balance_lock);
			bctl->stat.completed++;
			spin_unlock(&fs_info->balance_lock);
		}
loop:
		key.offset = found_key.offset - 1;
	}

	if (counting) {
		btrfs_release_path(path);
		counting = false;
		goto again;
	}
error:
	btrfs_free_path(path);
	if (enospc_errors) {
		printk(KERN_INFO "btrfs: %d enospc errors during balance\n",
		       enospc_errors);
		if (!ret)
			ret = -ENOSPC;
	}

	return ret;
}

/**
 * alloc_profile_is_valid - see if a given profile is valid and reduced
 * @flags: profile to validate
 * @extended: if true @flags is treated as an extended profile
 */
static int alloc_profile_is_valid(u64 flags, int extended)
{
	u64 mask = (extended ? BTRFS_EXTENDED_PROFILE_MASK :
			       BTRFS_BLOCK_GROUP_PROFILE_MASK);

	flags &= ~BTRFS_BLOCK_GROUP_TYPE_MASK;

	/* 1) check that all other bits are zeroed */
	if (flags & ~mask)
		return 0;

	/* 2) see if profile is reduced */
	if (flags == 0)
		return !extended; /* "0" is valid for usual profiles */

	/* true if exactly one bit set */
	return (flags & (flags - 1)) == 0;
}

static inline int balance_need_close(struct btrfs_fs_info *fs_info)
{
	/* cancel requested || normal exit path */
	return atomic_read(&fs_info->balance_cancel_req) ||
		(atomic_read(&fs_info->balance_pause_req) == 0 &&
		 atomic_read(&fs_info->balance_cancel_req) == 0);
}

static void __cancel_balance(struct btrfs_fs_info *fs_info)
{
	int ret;

	unset_balance_control(fs_info);
	ret = del_balance_item(fs_info->tree_root);
	BUG_ON(ret);
}

void update_ioctl_balance_args(struct btrfs_fs_info *fs_info, int lock,
			       struct btrfs_ioctl_balance_args *bargs);

/*
 * Should be called with both balance and volume mutexes held
 */
int btrfs_balance(struct btrfs_balance_control *bctl,
		  struct btrfs_ioctl_balance_args *bargs)
{
	struct btrfs_fs_info *fs_info = bctl->fs_info;
	u64 allowed;
	int mixed = 0;
	int ret;

	if (btrfs_fs_closing(fs_info) ||
	    atomic_read(&fs_info->balance_pause_req) ||
	    atomic_read(&fs_info->balance_cancel_req)) {
		ret = -EINVAL;
		goto out;
	}

	allowed = btrfs_super_incompat_flags(fs_info->super_copy);
	if (allowed & BTRFS_FEATURE_INCOMPAT_MIXED_GROUPS)
		mixed = 1;

	/*
	 * In case of mixed groups both data and meta should be picked,
	 * and identical options should be given for both of them.
	 */
	allowed = BTRFS_BALANCE_DATA | BTRFS_BALANCE_METADATA;
	if (mixed && (bctl->flags & allowed)) {
		if (!(bctl->flags & BTRFS_BALANCE_DATA) ||
		    !(bctl->flags & BTRFS_BALANCE_METADATA) ||
		    memcmp(&bctl->data, &bctl->meta, sizeof(bctl->data))) {
			printk(KERN_ERR "btrfs: with mixed groups data and "
			       "metadata balance options must be the same\n");
			ret = -EINVAL;
			goto out;
		}
	}

	allowed = BTRFS_AVAIL_ALLOC_BIT_SINGLE;
	if (fs_info->fs_devices->num_devices == 1)
		allowed |= BTRFS_BLOCK_GROUP_DUP;
	else if (fs_info->fs_devices->num_devices < 4)
		allowed |= (BTRFS_BLOCK_GROUP_RAID0 | BTRFS_BLOCK_GROUP_RAID1);
	else
		allowed |= (BTRFS_BLOCK_GROUP_RAID0 | BTRFS_BLOCK_GROUP_RAID1 |
				BTRFS_BLOCK_GROUP_RAID10);

	if ((bctl->data.flags & BTRFS_BALANCE_ARGS_CONVERT) &&
	    (!alloc_profile_is_valid(bctl->data.target, 1) ||
	     (bctl->data.target & ~allowed))) {
		printk(KERN_ERR "btrfs: unable to start balance with target "
		       "data profile %llu\n",
		       (unsigned long long)bctl->data.target);
		ret = -EINVAL;
		goto out;
	}
	if ((bctl->meta.flags & BTRFS_BALANCE_ARGS_CONVERT) &&
	    (!alloc_profile_is_valid(bctl->meta.target, 1) ||
	     (bctl->meta.target & ~allowed))) {
		printk(KERN_ERR "btrfs: unable to start balance with target "
		       "metadata profile %llu\n",
		       (unsigned long long)bctl->meta.target);
		ret = -EINVAL;
		goto out;
	}
	if ((bctl->sys.flags & BTRFS_BALANCE_ARGS_CONVERT) &&
	    (!alloc_profile_is_valid(bctl->sys.target, 1) ||
	     (bctl->sys.target & ~allowed))) {
		printk(KERN_ERR "btrfs: unable to start balance with target "
		       "system profile %llu\n",
		       (unsigned long long)bctl->sys.target);
		ret = -EINVAL;
		goto out;
	}

	/* allow dup'ed data chunks only in mixed mode */
	if (!mixed && (bctl->data.flags & BTRFS_BALANCE_ARGS_CONVERT) &&
	    (bctl->data.target & BTRFS_BLOCK_GROUP_DUP)) {
		printk(KERN_ERR "btrfs: dup for data is not allowed\n");
		ret = -EINVAL;
		goto out;
	}

	/* allow to reduce meta or sys integrity only if force set */
	allowed = BTRFS_BLOCK_GROUP_DUP | BTRFS_BLOCK_GROUP_RAID1 |
			BTRFS_BLOCK_GROUP_RAID10;
	if (((bctl->sys.flags & BTRFS_BALANCE_ARGS_CONVERT) &&
	     (fs_info->avail_system_alloc_bits & allowed) &&
	     !(bctl->sys.target & allowed)) ||
	    ((bctl->meta.flags & BTRFS_BALANCE_ARGS_CONVERT) &&
	     (fs_info->avail_metadata_alloc_bits & allowed) &&
	     !(bctl->meta.target & allowed))) {
		if (bctl->flags & BTRFS_BALANCE_FORCE) {
			printk(KERN_INFO "btrfs: force reducing metadata "
			       "integrity\n");
		} else {
			printk(KERN_ERR "btrfs: balance will reduce metadata "
			       "integrity, use force if you want this\n");
			ret = -EINVAL;
			goto out;
		}
	}

	ret = insert_balance_item(fs_info->tree_root, bctl);
	if (ret && ret != -EEXIST)
		goto out;

	if (!(bctl->flags & BTRFS_BALANCE_RESUME)) {
		BUG_ON(ret == -EEXIST);
		set_balance_control(bctl);
	} else {
		BUG_ON(ret != -EEXIST);
		spin_lock(&fs_info->balance_lock);
		update_balance_args(bctl);
		spin_unlock(&fs_info->balance_lock);
	}

	atomic_inc(&fs_info->balance_running);
	mutex_unlock(&fs_info->balance_mutex);

	ret = __btrfs_balance(fs_info);

	mutex_lock(&fs_info->balance_mutex);
	atomic_dec(&fs_info->balance_running);

	if (bargs) {
		memset(bargs, 0, sizeof(*bargs));
		update_ioctl_balance_args(fs_info, 0, bargs);
	}

	if ((ret && ret != -ECANCELED && ret != -ENOSPC) ||
	    balance_need_close(fs_info)) {
		__cancel_balance(fs_info);
	}

	wake_up(&fs_info->balance_wait_q);

	return ret;
out:
	if (bctl->flags & BTRFS_BALANCE_RESUME)
		__cancel_balance(fs_info);
	else
		kfree(bctl);
	return ret;
}

static int balance_kthread(void *data)
{
	struct btrfs_fs_info *fs_info = data;
	int ret = 0;

	mutex_lock(&fs_info->volume_mutex);
	mutex_lock(&fs_info->balance_mutex);

	if (fs_info->balance_ctl) {
		printk(KERN_INFO "btrfs: continuing balance\n");
		ret = btrfs_balance(fs_info->balance_ctl, NULL);
	}

	mutex_unlock(&fs_info->balance_mutex);
	mutex_unlock(&fs_info->volume_mutex);

	return ret;
}

int btrfs_resume_balance_async(struct btrfs_fs_info *fs_info)
{
	struct task_struct *tsk;

	spin_lock(&fs_info->balance_lock);
	if (!fs_info->balance_ctl) {
		spin_unlock(&fs_info->balance_lock);
		return 0;
	}
	spin_unlock(&fs_info->balance_lock);

	if (btrfs_test_opt(fs_info->tree_root, SKIP_BALANCE)) {
		printk(KERN_INFO "btrfs: force skipping balance\n");
		return 0;
	}

	tsk = kthread_run(balance_kthread, fs_info, "btrfs-balance");
	if (IS_ERR(tsk))
		return PTR_ERR(tsk);

	return 0;
}

int btrfs_recover_balance(struct btrfs_fs_info *fs_info)
{
	struct btrfs_balance_control *bctl;
	struct btrfs_balance_item *item;
	struct btrfs_disk_balance_args disk_bargs;
	struct btrfs_path *path;
	struct extent_buffer *leaf;
	struct btrfs_key key;
	int ret;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	key.objectid = BTRFS_BALANCE_OBJECTID;
	key.type = BTRFS_BALANCE_ITEM_KEY;
	key.offset = 0;

	ret = btrfs_search_slot(NULL, fs_info->tree_root, &key, path, 0, 0);
	if (ret < 0)
		goto out;
	if (ret > 0) { /* ret = -ENOENT; */
		ret = 0;
		goto out;
	}

	bctl = kzalloc(sizeof(*bctl), GFP_NOFS);
	if (!bctl) {
		ret = -ENOMEM;
		goto out;
	}

	leaf = path->nodes[0];
	item = btrfs_item_ptr(leaf, path->slots[0], struct btrfs_balance_item);

	bctl->fs_info = fs_info;
	bctl->flags = btrfs_balance_flags(leaf, item);
	bctl->flags |= BTRFS_BALANCE_RESUME;

	btrfs_balance_data(leaf, item, &disk_bargs);
	btrfs_disk_balance_args_to_cpu(&bctl->data, &disk_bargs);
	btrfs_balance_meta(leaf, item, &disk_bargs);
	btrfs_disk_balance_args_to_cpu(&bctl->meta, &disk_bargs);
	btrfs_balance_sys(leaf, item, &disk_bargs);
	btrfs_disk_balance_args_to_cpu(&bctl->sys, &disk_bargs);

	mutex_lock(&fs_info->volume_mutex);
	mutex_lock(&fs_info->balance_mutex);

	set_balance_control(bctl);

	mutex_unlock(&fs_info->balance_mutex);
	mutex_unlock(&fs_info->volume_mutex);
out:
	btrfs_free_path(path);
	return ret;
}

int btrfs_pause_balance(struct btrfs_fs_info *fs_info)
{
	int ret = 0;

	mutex_lock(&fs_info->balance_mutex);
	if (!fs_info->balance_ctl) {
		mutex_unlock(&fs_info->balance_mutex);
		return -ENOTCONN;
	}

	if (atomic_read(&fs_info->balance_running)) {
		atomic_inc(&fs_info->balance_pause_req);
		mutex_unlock(&fs_info->balance_mutex);

		wait_event(fs_info->balance_wait_q,
			   atomic_read(&fs_info->balance_running) == 0);

		mutex_lock(&fs_info->balance_mutex);
		/* we are good with balance_ctl ripped off from under us */
		BUG_ON(atomic_read(&fs_info->balance_running));
		atomic_dec(&fs_info->balance_pause_req);
	} else {
		ret = -ENOTCONN;
	}

	mutex_unlock(&fs_info->balance_mutex);
	return ret;
}

int btrfs_cancel_balance(struct btrfs_fs_info *fs_info)
{
	mutex_lock(&fs_info->balance_mutex);
	if (!fs_info->balance_ctl) {
		mutex_unlock(&fs_info->balance_mutex);
		return -ENOTCONN;
	}

	atomic_inc(&fs_info->balance_cancel_req);
	/*
	 * if we are running just wait and return, balance item is
	 * deleted in btrfs_balance in this case
	 */
	if (atomic_read(&fs_info->balance_running)) {
		mutex_unlock(&fs_info->balance_mutex);
		wait_event(fs_info->balance_wait_q,
			   atomic_read(&fs_info->balance_running) == 0);
		mutex_lock(&fs_info->balance_mutex);
	} else {
		/* __cancel_balance needs volume_mutex */
		mutex_unlock(&fs_info->balance_mutex);
		mutex_lock(&fs_info->volume_mutex);
		mutex_lock(&fs_info->balance_mutex);

		if (fs_info->balance_ctl)
			__cancel_balance(fs_info);

		mutex_unlock(&fs_info->volume_mutex);
	}

	BUG_ON(fs_info->balance_ctl || atomic_read(&fs_info->balance_running));
	atomic_dec(&fs_info->balance_cancel_req);
	mutex_unlock(&fs_info->balance_mutex);
	return 0;
}

/*
 * shrinking a device means finding all of the device extents past
 * the new size, and then following the back refs to the chunks.
 * The chunk relocation code actually frees the device extent
 */
int btrfs_shrink_device(struct btrfs_device *device, u64 new_size)
{
	struct btrfs_trans_handle *trans;
	struct btrfs_root *root = device->dev_root;
	struct btrfs_dev_extent *dev_extent = NULL;
	struct btrfs_path *path;
	u64 length;
	u64 chunk_tree;
	u64 chunk_objectid;
	u64 chunk_offset;
	int ret;
	int slot;
	int failed = 0;
	bool retried = false;
	struct extent_buffer *l;
	struct btrfs_key key;
	struct btrfs_super_block *super_copy = root->fs_info->super_copy;
	u64 old_total = btrfs_super_total_bytes(super_copy);
	u64 old_size = device->total_bytes;
	u64 diff = device->total_bytes - new_size;

	if (new_size >= device->total_bytes)
		return -EINVAL;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	path->reada = 2;

	lock_chunks(root);

	device->total_bytes = new_size;
	if (device->writeable) {
		device->fs_devices->total_rw_bytes -= diff;
		spin_lock(&root->fs_info->free_chunk_lock);
		root->fs_info->free_chunk_space -= diff;
		spin_unlock(&root->fs_info->free_chunk_lock);
	}
	unlock_chunks(root);

again:
	key.objectid = device->devid;
	key.offset = (u64)-1;
	key.type = BTRFS_DEV_EXTENT_KEY;

	do {
		ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
		if (ret < 0)
			goto done;

		ret = btrfs_previous_item(root, path, 0, key.type);
		if (ret < 0)
			goto done;
		if (ret) {
			ret = 0;
			btrfs_release_path(path);
			break;
		}

		l = path->nodes[0];
		slot = path->slots[0];
		btrfs_item_key_to_cpu(l, &key, path->slots[0]);

		if (key.objectid != device->devid) {
			btrfs_release_path(path);
			break;
		}

		dev_extent = btrfs_item_ptr(l, slot, struct btrfs_dev_extent);
		length = btrfs_dev_extent_length(l, dev_extent);

		if (key.offset + length <= new_size) {
			btrfs_release_path(path);
			break;
		}

		chunk_tree = btrfs_dev_extent_chunk_tree(l, dev_extent);
		chunk_objectid = btrfs_dev_extent_chunk_objectid(l, dev_extent);
		chunk_offset = btrfs_dev_extent_chunk_offset(l, dev_extent);
		btrfs_release_path(path);

		ret = btrfs_relocate_chunk(root, chunk_tree, chunk_objectid,
					   chunk_offset);
		if (ret && ret != -ENOSPC)
			goto done;
		if (ret == -ENOSPC)
			failed++;
	} while (key.offset-- > 0);

	if (failed && !retried) {
		failed = 0;
		retried = true;
		goto again;
	} else if (failed && retried) {
		ret = -ENOSPC;
		lock_chunks(root);

		device->total_bytes = old_size;
		if (device->writeable)
			device->fs_devices->total_rw_bytes += diff;
		spin_lock(&root->fs_info->free_chunk_lock);
		root->fs_info->free_chunk_space += diff;
		spin_unlock(&root->fs_info->free_chunk_lock);
		unlock_chunks(root);
		goto done;
	}

	/* Shrinking succeeded, else we would be at "done". */
	trans = btrfs_start_transaction(root, 0);
	if (IS_ERR(trans)) {
		ret = PTR_ERR(trans);
		goto done;
	}

	lock_chunks(root);

	device->disk_total_bytes = new_size;
	/* Now btrfs_update_device() will change the on-disk size. */
	ret = btrfs_update_device(trans, device);
	if (ret) {
		unlock_chunks(root);
		btrfs_end_transaction(trans, root);
		goto done;
	}
	WARN_ON(diff > old_total);
	btrfs_set_super_total_bytes(super_copy, old_total - diff);
	unlock_chunks(root);
	btrfs_end_transaction(trans, root);
done:
	btrfs_free_path(path);
	return ret;
}

static int btrfs_add_system_chunk(struct btrfs_root *root,
			   struct btrfs_key *key,
			   struct btrfs_chunk *chunk, int item_size)
{
	struct btrfs_super_block *super_copy = root->fs_info->super_copy;
	struct btrfs_disk_key disk_key;
	u32 array_size;
	u8 *ptr;

	array_size = btrfs_super_sys_array_size(super_copy);
	if (array_size + item_size > BTRFS_SYSTEM_CHUNK_ARRAY_SIZE)
		return -EFBIG;

	ptr = super_copy->sys_chunk_array + array_size;
	btrfs_cpu_key_to_disk(&disk_key, key);
	memcpy(ptr, &disk_key, sizeof(disk_key));
	ptr += sizeof(disk_key);
	memcpy(ptr, chunk, item_size);
	item_size += sizeof(disk_key);
	btrfs_set_super_sys_array_size(super_copy, array_size + item_size);
	return 0;
}

/*
 * sort the devices in descending order by max_avail, total_avail
 */
static int btrfs_cmp_device_info(const void *a, const void *b)
{
	const struct btrfs_device_info *di_a = a;
	const struct btrfs_device_info *di_b = b;

	if (di_a->max_avail > di_b->max_avail)
		return -1;
	if (di_a->max_avail < di_b->max_avail)
		return 1;
	if (di_a->total_avail > di_b->total_avail)
		return -1;
	if (di_a->total_avail < di_b->total_avail)
		return 1;
	return 0;
}

static int __btrfs_alloc_chunk(struct btrfs_trans_handle *trans,
			       struct btrfs_root *extent_root,
			       struct map_lookup **map_ret,
			       u64 *num_bytes_out, u64 *stripe_size_out,
			       u64 start, u64 type)
{
	struct btrfs_fs_info *info = extent_root->fs_info;
	struct btrfs_fs_devices *fs_devices = info->fs_devices;
	struct list_head *cur;
	struct map_lookup *map = NULL;
	struct extent_map_tree *em_tree;
	struct extent_map *em;
	struct btrfs_device_info *devices_info = NULL;
	u64 total_avail;
	int num_stripes;	/* total number of stripes to allocate */
	int sub_stripes;	/* sub_stripes info for map */
	int dev_stripes;	/* stripes per dev */
	int devs_max;		/* max devs to use */
	int devs_min;		/* min devs needed */
	int devs_increment;	/* ndevs has to be a multiple of this */
	int ncopies;		/* how many copies to data has */
	int ret;
	u64 max_stripe_size;
	u64 max_chunk_size;
	u64 stripe_size;
	u64 num_bytes;
	int ndevs;
	int i;
	int j;

	BUG_ON(!alloc_profile_is_valid(type, 0));

	if (list_empty(&fs_devices->alloc_list))
		return -ENOSPC;

	sub_stripes = 1;
	dev_stripes = 1;
	devs_increment = 1;
	ncopies = 1;
	devs_max = 0;	/* 0 == as many as possible */
	devs_min = 1;

	/*
	 * define the properties of each RAID type.
	 * FIXME: move this to a global table and use it in all RAID
	 * calculation code
	 */
	if (type & (BTRFS_BLOCK_GROUP_DUP)) {
		dev_stripes = 2;
		ncopies = 2;
		devs_max = 1;
	} else if (type & (BTRFS_BLOCK_GROUP_RAID0)) {
		devs_min = 2;
	} else if (type & (BTRFS_BLOCK_GROUP_RAID1)) {
		devs_increment = 2;
		ncopies = 2;
		devs_max = 2;
		devs_min = 2;
	} else if (type & (BTRFS_BLOCK_GROUP_RAID10)) {
		sub_stripes = 2;
		devs_increment = 2;
		ncopies = 2;
		devs_min = 4;
	} else {
		devs_max = 1;
	}

	if (type & BTRFS_BLOCK_GROUP_DATA) {
		max_stripe_size = 1024 * 1024 * 1024;
		max_chunk_size = 10 * max_stripe_size;
	} else if (type & BTRFS_BLOCK_GROUP_METADATA) {
		/* for larger filesystems, use larger metadata chunks */
		if (fs_devices->total_rw_bytes > 50ULL * 1024 * 1024 * 1024)
			max_stripe_size = 1024 * 1024 * 1024;
		else
			max_stripe_size = 256 * 1024 * 1024;
		max_chunk_size = max_stripe_size;
	} else if (type & BTRFS_BLOCK_GROUP_SYSTEM) {
		max_stripe_size = 32 * 1024 * 1024;
		max_chunk_size = 2 * max_stripe_size;
	} else {
		printk(KERN_ERR "btrfs: invalid chunk type 0x%llx requested\n",
		       type);
		BUG_ON(1);
	}

	/* we don't want a chunk larger than 10% of writeable space */
	max_chunk_size = min(div_factor(fs_devices->total_rw_bytes, 1),
			     max_chunk_size);

	devices_info = kzalloc(sizeof(*devices_info) * fs_devices->rw_devices,
			       GFP_NOFS);
	if (!devices_info)
		return -ENOMEM;

	cur = fs_devices->alloc_list.next;

	/*
	 * in the first pass through the devices list, we gather information
	 * about the available holes on each device.
	 */
	ndevs = 0;
	while (cur != &fs_devices->alloc_list) {
		struct btrfs_device *device;
		u64 max_avail;
		u64 dev_offset;

		device = list_entry(cur, struct btrfs_device, dev_alloc_list);

		cur = cur->next;

		if (!device->writeable) {
			printk(KERN_ERR
			       "btrfs: read-only device in alloc_list\n");
			WARN_ON(1);
			continue;
		}

		if (!device->in_fs_metadata)
			continue;

		if (device->total_bytes > device->bytes_used)
			total_avail = device->total_bytes - device->bytes_used;
		else
			total_avail = 0;

		/* If there is no space on this device, skip it. */
		if (total_avail == 0)
			continue;

		ret = find_free_dev_extent(device,
					   max_stripe_size * dev_stripes,
					   &dev_offset, &max_avail);
		if (ret && ret != -ENOSPC)
			goto error;

		if (ret == 0)
			max_avail = max_stripe_size * dev_stripes;

		if (max_avail < BTRFS_STRIPE_LEN * dev_stripes)
			continue;

		devices_info[ndevs].dev_offset = dev_offset;
		devices_info[ndevs].max_avail = max_avail;
		devices_info[ndevs].total_avail = total_avail;
		devices_info[ndevs].dev = device;
		++ndevs;
	}

	/*
	 * now sort the devices by hole size / available space
	 */
	sort(devices_info, ndevs, sizeof(struct btrfs_device_info),
	     btrfs_cmp_device_info, NULL);

	/* round down to number of usable stripes */
	ndevs -= ndevs % devs_increment;

	if (ndevs < devs_increment * sub_stripes || ndevs < devs_min) {
		ret = -ENOSPC;
		goto error;
	}

	if (devs_max && ndevs > devs_max)
		ndevs = devs_max;
	/*
	 * the primary goal is to maximize the number of stripes, so use as many
	 * devices as possible, even if the stripes are not maximum sized.
	 */
	stripe_size = devices_info[ndevs-1].max_avail;
	num_stripes = ndevs * dev_stripes;

	if (stripe_size * ndevs > max_chunk_size * ncopies) {
		stripe_size = max_chunk_size * ncopies;
		do_div(stripe_size, ndevs);
	}

	do_div(stripe_size, dev_stripes);

	/* align to BTRFS_STRIPE_LEN */
	do_div(stripe_size, BTRFS_STRIPE_LEN);
	stripe_size *= BTRFS_STRIPE_LEN;

	map = kmalloc(map_lookup_size(num_stripes), GFP_NOFS);
	if (!map) {
		ret = -ENOMEM;
		goto error;
	}
	map->num_stripes = num_stripes;

	for (i = 0; i < ndevs; ++i) {
		for (j = 0; j < dev_stripes; ++j) {
			int s = i * dev_stripes + j;
			map->stripes[s].dev = devices_info[i].dev;
			map->stripes[s].physical = devices_info[i].dev_offset +
						   j * stripe_size;
		}
	}
	map->sector_size = extent_root->sectorsize;
	map->stripe_len = BTRFS_STRIPE_LEN;
	map->io_align = BTRFS_STRIPE_LEN;
	map->io_width = BTRFS_STRIPE_LEN;
	map->type = type;
	map->sub_stripes = sub_stripes;

	*map_ret = map;
	num_bytes = stripe_size * (num_stripes / ncopies);

	*stripe_size_out = stripe_size;
	*num_bytes_out = num_bytes;

	trace_btrfs_chunk_alloc(info->chunk_root, map, start, num_bytes);

	em = alloc_extent_map();
	if (!em) {
		ret = -ENOMEM;
		goto error;
	}
	em->bdev = (struct block_device *)map;
	em->start = start;
	em->len = num_bytes;
	em->block_start = 0;
	em->block_len = em->len;

	em_tree = &extent_root->fs_info->mapping_tree.map_tree;
	write_lock(&em_tree->lock);
	ret = add_extent_mapping(em_tree, em);
	write_unlock(&em_tree->lock);
	free_extent_map(em);
	if (ret)
		goto error;

	ret = btrfs_make_block_group(trans, extent_root, 0, type,
				     BTRFS_FIRST_CHUNK_TREE_OBJECTID,
				     start, num_bytes);
	if (ret)
		goto error;

	for (i = 0; i < map->num_stripes; ++i) {
		struct btrfs_device *device;
		u64 dev_offset;

		device = map->stripes[i].dev;
		dev_offset = map->stripes[i].physical;

		ret = btrfs_alloc_dev_extent(trans, device,
				info->chunk_root->root_key.objectid,
				BTRFS_FIRST_CHUNK_TREE_OBJECTID,
				start, dev_offset, stripe_size);
		if (ret) {
			btrfs_abort_transaction(trans, extent_root, ret);
			goto error;
		}
	}

	kfree(devices_info);
	return 0;

error:
	kfree(map);
	kfree(devices_info);
	return ret;
}

static int __finish_chunk_alloc(struct btrfs_trans_handle *trans,
				struct btrfs_root *extent_root,
				struct map_lookup *map, u64 chunk_offset,
				u64 chunk_size, u64 stripe_size)
{
	u64 dev_offset;
	struct btrfs_key key;
	struct btrfs_root *chunk_root = extent_root->fs_info->chunk_root;
	struct btrfs_device *device;
	struct btrfs_chunk *chunk;
	struct btrfs_stripe *stripe;
	size_t item_size = btrfs_chunk_item_size(map->num_stripes);
	int index = 0;
	int ret;

	chunk = kzalloc(item_size, GFP_NOFS);
	if (!chunk)
		return -ENOMEM;

	index = 0;
	while (index < map->num_stripes) {
		device = map->stripes[index].dev;
		device->bytes_used += stripe_size;
		ret = btrfs_update_device(trans, device);
		if (ret)
			goto out_free;
		index++;
	}

	spin_lock(&extent_root->fs_info->free_chunk_lock);
	extent_root->fs_info->free_chunk_space -= (stripe_size *
						   map->num_stripes);
	spin_unlock(&extent_root->fs_info->free_chunk_lock);

	index = 0;
	stripe = &chunk->stripe;
	while (index < map->num_stripes) {
		device = map->stripes[index].dev;
		dev_offset = map->stripes[index].physical;

		btrfs_set_stack_stripe_devid(stripe, device->devid);
		btrfs_set_stack_stripe_offset(stripe, dev_offset);
		memcpy(stripe->dev_uuid, device->uuid, BTRFS_UUID_SIZE);
		stripe++;
		index++;
	}

	btrfs_set_stack_chunk_length(chunk, chunk_size);
	btrfs_set_stack_chunk_owner(chunk, extent_root->root_key.objectid);
	btrfs_set_stack_chunk_stripe_len(chunk, map->stripe_len);
	btrfs_set_stack_chunk_type(chunk, map->type);
	btrfs_set_stack_chunk_num_stripes(chunk, map->num_stripes);
	btrfs_set_stack_chunk_io_align(chunk, map->stripe_len);
	btrfs_set_stack_chunk_io_width(chunk, map->stripe_len);
	btrfs_set_stack_chunk_sector_size(chunk, extent_root->sectorsize);
	btrfs_set_stack_chunk_sub_stripes(chunk, map->sub_stripes);

	key.objectid = BTRFS_FIRST_CHUNK_TREE_OBJECTID;
	key.type = BTRFS_CHUNK_ITEM_KEY;
	key.offset = chunk_offset;

	ret = btrfs_insert_item(trans, chunk_root, &key, chunk, item_size);

	if (ret == 0 && map->type & BTRFS_BLOCK_GROUP_SYSTEM) {
		/*
		 * TODO: Cleanup of inserted chunk root in case of
		 * failure.
		 */
		ret = btrfs_add_system_chunk(chunk_root, &key, chunk,
					     item_size);
	}

out_free:
	kfree(chunk);
	return ret;
}

/*
 * Chunk allocation falls into two parts. The first part does works
 * that make the new allocated chunk useable, but not do any operation
 * that modifies the chunk tree. The second part does the works that
 * require modifying the chunk tree. This division is important for the
 * bootstrap process of adding storage to a seed btrfs.
 */
int btrfs_alloc_chunk(struct btrfs_trans_handle *trans,
		      struct btrfs_root *extent_root, u64 type)
{
	u64 chunk_offset;
	u64 chunk_size;
	u64 stripe_size;
	struct map_lookup *map;
	struct btrfs_root *chunk_root = extent_root->fs_info->chunk_root;
	int ret;

	ret = find_next_chunk(chunk_root, BTRFS_FIRST_CHUNK_TREE_OBJECTID,
			      &chunk_offset);
	if (ret)
		return ret;

	ret = __btrfs_alloc_chunk(trans, extent_root, &map, &chunk_size,
				  &stripe_size, chunk_offset, type);
	if (ret)
		return ret;

	ret = __finish_chunk_alloc(trans, extent_root, map, chunk_offset,
				   chunk_size, stripe_size);
	if (ret)
		return ret;
	return 0;
}

static noinline int init_first_rw_device(struct btrfs_trans_handle *trans,
					 struct btrfs_root *root,
					 struct btrfs_device *device)
{
	u64 chunk_offset;
	u64 sys_chunk_offset;
	u64 chunk_size;
	u64 sys_chunk_size;
	u64 stripe_size;
	u64 sys_stripe_size;
	u64 alloc_profile;
	struct map_lookup *map;
	struct map_lookup *sys_map;
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_root *extent_root = fs_info->extent_root;
	int ret;

	ret = find_next_chunk(fs_info->chunk_root,
			      BTRFS_FIRST_CHUNK_TREE_OBJECTID, &chunk_offset);
	if (ret)
		return ret;

	alloc_profile = BTRFS_BLOCK_GROUP_METADATA |
				fs_info->avail_metadata_alloc_bits;
	alloc_profile = btrfs_reduce_alloc_profile(root, alloc_profile);

	ret = __btrfs_alloc_chunk(trans, extent_root, &map, &chunk_size,
				  &stripe_size, chunk_offset, alloc_profile);
	if (ret)
		return ret;

	sys_chunk_offset = chunk_offset + chunk_size;

	alloc_profile = BTRFS_BLOCK_GROUP_SYSTEM |
				fs_info->avail_system_alloc_bits;
	alloc_profile = btrfs_reduce_alloc_profile(root, alloc_profile);

	ret = __btrfs_alloc_chunk(trans, extent_root, &sys_map,
				  &sys_chunk_size, &sys_stripe_size,
				  sys_chunk_offset, alloc_profile);
	if (ret)
		goto abort;

	ret = btrfs_add_device(trans, fs_info->chunk_root, device);
	if (ret)
		goto abort;

	/*
	 * Modifying chunk tree needs allocating new blocks from both
	 * system block group and metadata block group. So we only can
	 * do operations require modifying the chunk tree after both
	 * block groups were created.
	 */
	ret = __finish_chunk_alloc(trans, extent_root, map, chunk_offset,
				   chunk_size, stripe_size);
	if (ret)
		goto abort;

	ret = __finish_chunk_alloc(trans, extent_root, sys_map,
				   sys_chunk_offset, sys_chunk_size,
				   sys_stripe_size);
	if (ret)
		goto abort;

	return 0;

abort:
	btrfs_abort_transaction(trans, root, ret);
	return ret;
}

int btrfs_chunk_readonly(struct btrfs_root *root, u64 chunk_offset)
{
	struct extent_map *em;
	struct map_lookup *map;
	struct btrfs_mapping_tree *map_tree = &root->fs_info->mapping_tree;
	int readonly = 0;
	int i;

	read_lock(&map_tree->map_tree.lock);
	em = lookup_extent_mapping(&map_tree->map_tree, chunk_offset, 1);
	read_unlock(&map_tree->map_tree.lock);
	if (!em)
		return 1;

	if (btrfs_test_opt(root, DEGRADED)) {
		free_extent_map(em);
		return 0;
	}

	map = (struct map_lookup *)em->bdev;
	for (i = 0; i < map->num_stripes; i++) {
		if (!map->stripes[i].dev->writeable) {
			readonly = 1;
			break;
		}
	}
	free_extent_map(em);
	return readonly;
}

void btrfs_mapping_init(struct btrfs_mapping_tree *tree)
{
	extent_map_tree_init(&tree->map_tree);
}

void btrfs_mapping_tree_free(struct btrfs_mapping_tree *tree)
{
	struct extent_map *em;

	while (1) {
		write_lock(&tree->map_tree.lock);
		em = lookup_extent_mapping(&tree->map_tree, 0, (u64)-1);
		if (em)
			remove_extent_mapping(&tree->map_tree, em);
		write_unlock(&tree->map_tree.lock);
		if (!em)
			break;
		kfree(em->bdev);
		/* once for us */
		free_extent_map(em);
		/* once for the tree */
		free_extent_map(em);
	}
}

int btrfs_num_copies(struct btrfs_mapping_tree *map_tree, u64 logical, u64 len)
{
	struct extent_map *em;
	struct map_lookup *map;
	struct extent_map_tree *em_tree = &map_tree->map_tree;
	int ret;

	read_lock(&em_tree->lock);
	em = lookup_extent_mapping(em_tree, logical, len);
	read_unlock(&em_tree->lock);
	BUG_ON(!em);

	BUG_ON(em->start > logical || em->start + em->len < logical);
	map = (struct map_lookup *)em->bdev;
	if (map->type & (BTRFS_BLOCK_GROUP_DUP | BTRFS_BLOCK_GROUP_RAID1))
		ret = map->num_stripes;
	else if (map->type & BTRFS_BLOCK_GROUP_RAID10)
		ret = map->sub_stripes;
	else
		ret = 1;
	free_extent_map(em);
	return ret;
}

static int find_live_mirror(struct map_lookup *map, int first, int num,
			    int optimal)
{
	int i;
	if (map->stripes[optimal].dev->bdev)
		return optimal;
	for (i = first; i < first + num; i++) {
		if (map->stripes[i].dev->bdev)
			return i;
	}
	/* we couldn't find one that doesn't fail.  Just return something
	 * and the io error handling code will clean up eventually
	 */
	return optimal;
}

static int __btrfs_map_block(struct btrfs_mapping_tree *map_tree, int rw,
			     u64 logical, u64 *length,
			     struct btrfs_bio **bbio_ret,
			     int mirror_num)
{
	struct extent_map *em;
	struct map_lookup *map;
	struct extent_map_tree *em_tree = &map_tree->map_tree;
	u64 offset;
	u64 stripe_offset;
	u64 stripe_end_offset;
	u64 stripe_nr;
	u64 stripe_nr_orig;
	u64 stripe_nr_end;
	int stripe_index;
	int i;
	int ret = 0;
	int num_stripes;
	int max_errors = 0;
	struct btrfs_bio *bbio = NULL;

	read_lock(&em_tree->lock);
	em = lookup_extent_mapping(em_tree, logical, *length);
	read_unlock(&em_tree->lock);

	if (!em) {
		printk(KERN_CRIT "unable to find logical %llu len %llu\n",
		       (unsigned long long)logical,
		       (unsigned long long)*length);
		BUG();
	}

	BUG_ON(em->start > logical || em->start + em->len < logical);
	map = (struct map_lookup *)em->bdev;
	offset = logical - em->start;

	if (mirror_num > map->num_stripes)
		mirror_num = 0;

	stripe_nr = offset;
	/*
	 * stripe_nr counts the total number of stripes we have to stride
	 * to get to this block
	 */
	do_div(stripe_nr, map->stripe_len);

	stripe_offset = stripe_nr * map->stripe_len;
	BUG_ON(offset < stripe_offset);

	/* stripe_offset is the offset of this block in its stripe*/
	stripe_offset = offset - stripe_offset;

	if (rw & REQ_DISCARD)
		*length = min_t(u64, em->len - offset, *length);
	else if (map->type & BTRFS_BLOCK_GROUP_PROFILE_MASK) {
		/* we limit the length of each bio to what fits in a stripe */
		*length = min_t(u64, em->len - offset,
				map->stripe_len - stripe_offset);
	} else {
		*length = em->len - offset;
	}

	if (!bbio_ret)
		goto out;

	num_stripes = 1;
	stripe_index = 0;
	stripe_nr_orig = stripe_nr;
	stripe_nr_end = (offset + *length + map->stripe_len - 1) &
			(~(map->stripe_len - 1));
	do_div(stripe_nr_end, map->stripe_len);
	stripe_end_offset = stripe_nr_end * map->stripe_len -
			    (offset + *length);
	if (map->type & BTRFS_BLOCK_GROUP_RAID0) {
		if (rw & REQ_DISCARD)
			num_stripes = min_t(u64, map->num_stripes,
					    stripe_nr_end - stripe_nr_orig);
		stripe_index = do_div(stripe_nr, map->num_stripes);
	} else if (map->type & BTRFS_BLOCK_GROUP_RAID1) {
		if (rw & (REQ_WRITE | REQ_DISCARD))
			num_stripes = map->num_stripes;
		else if (mirror_num)
			stripe_index = mirror_num - 1;
		else {
			stripe_index = find_live_mirror(map, 0,
					    map->num_stripes,
					    current->pid % map->num_stripes);
			mirror_num = stripe_index + 1;
		}

	} else if (map->type & BTRFS_BLOCK_GROUP_DUP) {
		if (rw & (REQ_WRITE | REQ_DISCARD)) {
			num_stripes = map->num_stripes;
		} else if (mirror_num) {
			stripe_index = mirror_num - 1;
		} else {
			mirror_num = 1;
		}

	} else if (map->type & BTRFS_BLOCK_GROUP_RAID10) {
		int factor = map->num_stripes / map->sub_stripes;

		stripe_index = do_div(stripe_nr, factor);
		stripe_index *= map->sub_stripes;

		if (rw & REQ_WRITE)
			num_stripes = map->sub_stripes;
		else if (rw & REQ_DISCARD)
			num_stripes = min_t(u64, map->sub_stripes *
					    (stripe_nr_end - stripe_nr_orig),
					    map->num_stripes);
		else if (mirror_num)
			stripe_index += mirror_num - 1;
		else {
			int old_stripe_index = stripe_index;
			stripe_index = find_live_mirror(map, stripe_index,
					      map->sub_stripes, stripe_index +
					      current->pid % map->sub_stripes);
			mirror_num = stripe_index - old_stripe_index + 1;
		}
	} else {
		/*
		 * after this do_div call, stripe_nr is the number of stripes
		 * on this device we have to walk to find the data, and
		 * stripe_index is the number of our device in the stripe array
		 */
		stripe_index = do_div(stripe_nr, map->num_stripes);
		mirror_num = stripe_index + 1;
	}
	BUG_ON(stripe_index >= map->num_stripes);

	bbio = kzalloc(btrfs_bio_size(num_stripes), GFP_NOFS);
	if (!bbio) {
		ret = -ENOMEM;
		goto out;
	}
	atomic_set(&bbio->error, 0);

	if (rw & REQ_DISCARD) {
		int factor = 0;
		int sub_stripes = 0;
		u64 stripes_per_dev = 0;
		u32 remaining_stripes = 0;
		u32 last_stripe = 0;

		if (map->type &
		    (BTRFS_BLOCK_GROUP_RAID0 | BTRFS_BLOCK_GROUP_RAID10)) {
			if (map->type & BTRFS_BLOCK_GROUP_RAID0)
				sub_stripes = 1;
			else
				sub_stripes = map->sub_stripes;

			factor = map->num_stripes / sub_stripes;
			stripes_per_dev = div_u64_rem(stripe_nr_end -
						      stripe_nr_orig,
						      factor,
						      &remaining_stripes);
			div_u64_rem(stripe_nr_end - 1, factor, &last_stripe);
			last_stripe *= sub_stripes;
		}

		for (i = 0; i < num_stripes; i++) {
			bbio->stripes[i].physical =
				map->stripes[stripe_index].physical +
				stripe_offset + stripe_nr * map->stripe_len;
			bbio->stripes[i].dev = map->stripes[stripe_index].dev;

			if (map->type & (BTRFS_BLOCK_GROUP_RAID0 |
					 BTRFS_BLOCK_GROUP_RAID10)) {
				bbio->stripes[i].length = stripes_per_dev *
							  map->stripe_len;

				if (i / sub_stripes < remaining_stripes)
					bbio->stripes[i].length +=
						map->stripe_len;

				/*
				 * Special for the first stripe and
				 * the last stripe:
				 *
				 * |-------|...|-------|
				 *     |----------|
				 *    off     end_off
				 */
				if (i < sub_stripes)
					bbio->stripes[i].length -=
						stripe_offset;

				if (stripe_index >= last_stripe &&
				    stripe_index <= (last_stripe +
						     sub_stripes - 1))
					bbio->stripes[i].length -=
						stripe_end_offset;

				if (i == sub_stripes - 1)
					stripe_offset = 0;
			} else
				bbio->stripes[i].length = *length;

			stripe_index++;
			if (stripe_index == map->num_stripes) {
				/* This could only happen for RAID0/10 */
				stripe_index = 0;
				stripe_nr++;
			}
		}
	} else {
		for (i = 0; i < num_stripes; i++) {
			bbio->stripes[i].physical =
				map->stripes[stripe_index].physical +
				stripe_offset +
				stripe_nr * map->stripe_len;
			bbio->stripes[i].dev =
				map->stripes[stripe_index].dev;
			stripe_index++;
		}
	}

	if (rw & REQ_WRITE) {
		if (map->type & (BTRFS_BLOCK_GROUP_RAID1 |
				 BTRFS_BLOCK_GROUP_RAID10 |
				 BTRFS_BLOCK_GROUP_DUP)) {
			max_errors = 1;
		}
	}

	*bbio_ret = bbio;
	bbio->num_stripes = num_stripes;
	bbio->max_errors = max_errors;
	bbio->mirror_num = mirror_num;
out:
	free_extent_map(em);
	return ret;
}

int btrfs_map_block(struct btrfs_mapping_tree *map_tree, int rw,
		      u64 logical, u64 *length,
		      struct btrfs_bio **bbio_ret, int mirror_num)
{
	return __btrfs_map_block(map_tree, rw, logical, length, bbio_ret,
				 mirror_num);
}

int btrfs_rmap_block(struct btrfs_mapping_tree *map_tree,
		     u64 chunk_start, u64 physical, u64 devid,
		     u64 **logical, int *naddrs, int *stripe_len)
{
	struct extent_map_tree *em_tree = &map_tree->map_tree;
	struct extent_map *em;
	struct map_lookup *map;
	u64 *buf;
	u64 bytenr;
	u64 length;
	u64 stripe_nr;
	int i, j, nr = 0;

	read_lock(&em_tree->lock);
	em = lookup_extent_mapping(em_tree, chunk_start, 1);
	read_unlock(&em_tree->lock);

	BUG_ON(!em || em->start != chunk_start);
	map = (struct map_lookup *)em->bdev;

	length = em->len;
	if (map->type & BTRFS_BLOCK_GROUP_RAID10)
		do_div(length, map->num_stripes / map->sub_stripes);
	else if (map->type & BTRFS_BLOCK_GROUP_RAID0)
		do_div(length, map->num_stripes);

	buf = kzalloc(sizeof(u64) * map->num_stripes, GFP_NOFS);
	BUG_ON(!buf); /* -ENOMEM */

	for (i = 0; i < map->num_stripes; i++) {
		if (devid && map->stripes[i].dev->devid != devid)
			continue;
		if (map->stripes[i].physical > physical ||
		    map->stripes[i].physical + length <= physical)
			continue;

		stripe_nr = physical - map->stripes[i].physical;
		do_div(stripe_nr, map->stripe_len);

		if (map->type & BTRFS_BLOCK_GROUP_RAID10) {
			stripe_nr = stripe_nr * map->num_stripes + i;
			do_div(stripe_nr, map->sub_stripes);
		} else if (map->type & BTRFS_BLOCK_GROUP_RAID0) {
			stripe_nr = stripe_nr * map->num_stripes + i;
		}
		bytenr = chunk_start + stripe_nr * map->stripe_len;
		WARN_ON(nr >= map->num_stripes);
		for (j = 0; j < nr; j++) {
			if (buf[j] == bytenr)
				break;
		}
		if (j == nr) {
			WARN_ON(nr >= map->num_stripes);
			buf[nr++] = bytenr;
		}
	}

	*logical = buf;
	*naddrs = nr;
	*stripe_len = map->stripe_len;

	free_extent_map(em);
	return 0;
}

static void *merge_stripe_index_into_bio_private(void *bi_private,
						 unsigned int stripe_index)
{
	/*
	 * with single, dup, RAID0, RAID1 and RAID10, stripe_index is
	 * at most 1.
	 * The alternative solution (instead of stealing bits from the
	 * pointer) would be to allocate an intermediate structure
	 * that contains the old private pointer plus the stripe_index.
	 */
	BUG_ON((((uintptr_t)bi_private) & 3) != 0);
	BUG_ON(stripe_index > 3);
	return (void *)(((uintptr_t)bi_private) | stripe_index);
}

static struct btrfs_bio *extract_bbio_from_bio_private(void *bi_private)
{
	return (struct btrfs_bio *)(((uintptr_t)bi_private) & ~((uintptr_t)3));
}

static unsigned int extract_stripe_index_from_bio_private(void *bi_private)
{
	return (unsigned int)((uintptr_t)bi_private) & 3;
}

static void btrfs_end_bio(struct bio *bio, int err)
{
	struct btrfs_bio *bbio = extract_bbio_from_bio_private(bio->bi_private);
	int is_orig_bio = 0;

	if (err) {
		atomic_inc(&bbio->error);
		if (err == -EIO || err == -EREMOTEIO) {
			unsigned int stripe_index =
				extract_stripe_index_from_bio_private(
					bio->bi_private);
			struct btrfs_device *dev;

			BUG_ON(stripe_index >= bbio->num_stripes);
			dev = bbio->stripes[stripe_index].dev;
			if (dev->bdev) {
				if (bio->bi_rw & WRITE)
					btrfs_dev_stat_inc(dev,
						BTRFS_DEV_STAT_WRITE_ERRS);
				else
					btrfs_dev_stat_inc(dev,
						BTRFS_DEV_STAT_READ_ERRS);
				if ((bio->bi_rw & WRITE_FLUSH) == WRITE_FLUSH)
					btrfs_dev_stat_inc(dev,
						BTRFS_DEV_STAT_FLUSH_ERRS);
				btrfs_dev_stat_print_on_error(dev);
			}
		}
	}

	if (bio == bbio->orig_bio)
		is_orig_bio = 1;

	if (atomic_dec_and_test(&bbio->stripes_pending)) {
		if (!is_orig_bio) {
			bio_put(bio);
			bio = bbio->orig_bio;
		}
		bio->bi_private = bbio->private;
		bio->bi_end_io = bbio->end_io;
		bio->bi_bdev = (struct block_device *)
					(unsigned long)bbio->mirror_num;
		/* only send an error to the higher layers if it is
		 * beyond the tolerance of the multi-bio
		 */
		if (atomic_read(&bbio->error) > bbio->max_errors) {
			err = -EIO;
		} else {
			/*
			 * this bio is actually up to date, we didn't
			 * go over the max number of errors
			 */
			set_bit(BIO_UPTODATE, &bio->bi_flags);
			err = 0;
		}
		kfree(bbio);

		bio_endio(bio, err);
	} else if (!is_orig_bio) {
		bio_put(bio);
	}
}

struct async_sched {
	struct bio *bio;
	int rw;
	struct btrfs_fs_info *info;
	struct btrfs_work work;
};

/*
 * see run_scheduled_bios for a description of why bios are collected for
 * async submit.
 *
 * This will add one bio to the pending list for a device and make sure
 * the work struct is scheduled.
 */
static noinline void schedule_bio(struct btrfs_root *root,
				 struct btrfs_device *device,
				 int rw, struct bio *bio)
{
	int should_queue = 1;
	struct btrfs_pending_bios *pending_bios;

	/* don't bother with additional async steps for reads, right now */
	if (!(rw & REQ_WRITE)) {
		bio_get(bio);
		btrfsic_submit_bio(rw, bio);
		bio_put(bio);
		return;
	}

	/*
	 * nr_async_bios allows us to reliably return congestion to the
	 * higher layers.  Otherwise, the async bio makes it appear we have
	 * made progress against dirty pages when we've really just put it
	 * on a queue for later
	 */
	atomic_inc(&root->fs_info->nr_async_bios);
	WARN_ON(bio->bi_next);
	bio->bi_next = NULL;
	bio->bi_rw |= rw;

	spin_lock(&device->io_lock);
	if (bio->bi_rw & REQ_SYNC)
		pending_bios = &device->pending_sync_bios;
	else
		pending_bios = &device->pending_bios;

	if (pending_bios->tail)
		pending_bios->tail->bi_next = bio;

	pending_bios->tail = bio;
	if (!pending_bios->head)
		pending_bios->head = bio;
	if (device->running_pending)
		should_queue = 0;

	spin_unlock(&device->io_lock);

	if (should_queue)
		btrfs_queue_worker(&root->fs_info->submit_workers,
				   &device->work);
}

int btrfs_map_bio(struct btrfs_root *root, int rw, struct bio *bio,
		  int mirror_num, int async_submit)
{
	struct btrfs_mapping_tree *map_tree;
	struct btrfs_device *dev;
	struct bio *first_bio = bio;
	u64 logical = (u64)bio->bi_sector << 9;
	u64 length = 0;
	u64 map_length;
	int ret;
	int dev_nr = 0;
	int total_devs = 1;
	struct btrfs_bio *bbio = NULL;

	length = bio->bi_size;
	map_tree = &root->fs_info->mapping_tree;
	map_length = length;

	ret = btrfs_map_block(map_tree, rw, logical, &map_length, &bbio,
			      mirror_num);
	if (ret) /* -ENOMEM */
		return ret;

	total_devs = bbio->num_stripes;
	if (map_length < length) {
		printk(KERN_CRIT "mapping failed logical %llu bio len %llu "
		       "len %llu\n", (unsigned long long)logical,
		       (unsigned long long)length,
		       (unsigned long long)map_length);
		BUG();
	}

	bbio->orig_bio = first_bio;
	bbio->private = first_bio->bi_private;
	bbio->end_io = first_bio->bi_end_io;
	atomic_set(&bbio->stripes_pending, bbio->num_stripes);

	while (dev_nr < total_devs) {
		if (dev_nr < total_devs - 1) {
			bio = bio_clone(first_bio, GFP_NOFS);
			BUG_ON(!bio); /* -ENOMEM */
		} else {
			bio = first_bio;
		}
		bio->bi_private = bbio;
		bio->bi_private = merge_stripe_index_into_bio_private(
				bio->bi_private, (unsigned int)dev_nr);
		bio->bi_end_io = btrfs_end_bio;
		bio->bi_sector = bbio->stripes[dev_nr].physical >> 9;
		dev = bbio->stripes[dev_nr].dev;
		if (dev && dev->bdev && (rw != WRITE || dev->writeable)) {
#ifdef DEBUG
			struct rcu_string *name;

			rcu_read_lock();
			name = rcu_dereference(dev->name);
			pr_debug("btrfs_map_bio: rw %d, secor=%llu, dev=%lu "
				 "(%s id %llu), size=%u\n", rw,
				 (u64)bio->bi_sector, (u_long)dev->bdev->bd_dev,
				 name->str, dev->devid, bio->bi_size);
			rcu_read_unlock();
#endif
			bio->bi_bdev = dev->bdev;
			if (async_submit)
				schedule_bio(root, dev, rw, bio);
			else
				btrfsic_submit_bio(rw, bio);
		} else {
			bio->bi_bdev = root->fs_info->fs_devices->latest_bdev;
			bio->bi_sector = logical >> 9;
			bio_endio(bio, -EIO);
		}
		dev_nr++;
	}
	return 0;
}

struct btrfs_device *btrfs_find_device(struct btrfs_root *root, u64 devid,
				       u8 *uuid, u8 *fsid)
{
	struct btrfs_device *device;
	struct btrfs_fs_devices *cur_devices;

	cur_devices = root->fs_info->fs_devices;
	while (cur_devices) {
		if (!fsid ||
		    !memcmp(cur_devices->fsid, fsid, BTRFS_UUID_SIZE)) {
			device = __find_device(&cur_devices->devices,
					       devid, uuid);
			if (device)
				return device;
		}
		cur_devices = cur_devices->seed;
	}
	return NULL;
}

static struct btrfs_device *add_missing_dev(struct btrfs_root *root,
					    u64 devid, u8 *dev_uuid)
{
	struct btrfs_device *device;
	struct btrfs_fs_devices *fs_devices = root->fs_info->fs_devices;

	device = kzalloc(sizeof(*device), GFP_NOFS);
	if (!device)
		return NULL;
	list_add(&device->dev_list,
		 &fs_devices->devices);
	device->dev_root = root->fs_info->dev_root;
	device->devid = devid;
	device->work.func = pending_bios_fn;
	device->fs_devices = fs_devices;
	device->missing = 1;
	fs_devices->num_devices++;
	fs_devices->missing_devices++;
	spin_lock_init(&device->io_lock);
	INIT_LIST_HEAD(&device->dev_alloc_list);
	memcpy(device->uuid, dev_uuid, BTRFS_UUID_SIZE);
	return device;
}

static int read_one_chunk(struct btrfs_root *root, struct btrfs_key *key,
			  struct extent_buffer *leaf,
			  struct btrfs_chunk *chunk)
{
	struct btrfs_mapping_tree *map_tree = &root->fs_info->mapping_tree;
	struct map_lookup *map;
	struct extent_map *em;
	u64 logical;
	u64 length;
	u64 devid;
	u8 uuid[BTRFS_UUID_SIZE];
	int num_stripes;
	int ret;
	int i;

	logical = key->offset;
	length = btrfs_chunk_length(leaf, chunk);

	read_lock(&map_tree->map_tree.lock);
	em = lookup_extent_mapping(&map_tree->map_tree, logical, 1);
	read_unlock(&map_tree->map_tree.lock);

	/* already mapped? */
	if (em && em->start <= logical && em->start + em->len > logical) {
		free_extent_map(em);
		return 0;
	} else if (em) {
		free_extent_map(em);
	}

	em = alloc_extent_map();
	if (!em)
		return -ENOMEM;
	num_stripes = btrfs_chunk_num_stripes(leaf, chunk);
	map = kmalloc(map_lookup_size(num_stripes), GFP_NOFS);
	if (!map) {
		free_extent_map(em);
		return -ENOMEM;
	}

	em->bdev = (struct block_device *)map;
	em->start = logical;
	em->len = length;
	em->block_start = 0;
	em->block_len = em->len;

	map->num_stripes = num_stripes;
	map->io_width = btrfs_chunk_io_width(leaf, chunk);
	map->io_align = btrfs_chunk_io_align(leaf, chunk);
	map->sector_size = btrfs_chunk_sector_size(leaf, chunk);
	map->stripe_len = btrfs_chunk_stripe_len(leaf, chunk);
	map->type = btrfs_chunk_type(leaf, chunk);
	map->sub_stripes = btrfs_chunk_sub_stripes(leaf, chunk);
	for (i = 0; i < num_stripes; i++) {
		map->stripes[i].physical =
			btrfs_stripe_offset_nr(leaf, chunk, i);
		devid = btrfs_stripe_devid_nr(leaf, chunk, i);
		read_extent_buffer(leaf, uuid, (unsigned long)
				   btrfs_stripe_dev_uuid_nr(chunk, i),
				   BTRFS_UUID_SIZE);
		map->stripes[i].dev = btrfs_find_device(root, devid, uuid,
							NULL);
		if (!map->stripes[i].dev && !btrfs_test_opt(root, DEGRADED)) {
			kfree(map);
			free_extent_map(em);
			return -EIO;
		}
		if (!map->stripes[i].dev) {
			map->stripes[i].dev =
				add_missing_dev(root, devid, uuid);
			if (!map->stripes[i].dev) {
				kfree(map);
				free_extent_map(em);
				return -EIO;
			}
		}
		map->stripes[i].dev->in_fs_metadata = 1;
	}

	write_lock(&map_tree->map_tree.lock);
	ret = add_extent_mapping(&map_tree->map_tree, em);
	write_unlock(&map_tree->map_tree.lock);
	BUG_ON(ret); /* Tree corruption */
	free_extent_map(em);

	return 0;
}

static void fill_device_from_item(struct extent_buffer *leaf,
				 struct btrfs_dev_item *dev_item,
				 struct btrfs_device *device)
{
	unsigned long ptr;

	device->devid = btrfs_device_id(leaf, dev_item);
	device->disk_total_bytes = btrfs_device_total_bytes(leaf, dev_item);
	device->total_bytes = device->disk_total_bytes;
	device->bytes_used = btrfs_device_bytes_used(leaf, dev_item);
	device->type = btrfs_device_type(leaf, dev_item);
	device->io_align = btrfs_device_io_align(leaf, dev_item);
	device->io_width = btrfs_device_io_width(leaf, dev_item);
	device->sector_size = btrfs_device_sector_size(leaf, dev_item);

	ptr = (unsigned long)btrfs_device_uuid(dev_item);
	read_extent_buffer(leaf, device->uuid, ptr, BTRFS_UUID_SIZE);
}

static int open_seed_devices(struct btrfs_root *root, u8 *fsid)
{
	struct btrfs_fs_devices *fs_devices;
	int ret;

	BUG_ON(!mutex_is_locked(&uuid_mutex));

	fs_devices = root->fs_info->fs_devices->seed;
	while (fs_devices) {
		if (!memcmp(fs_devices->fsid, fsid, BTRFS_UUID_SIZE)) {
			ret = 0;
			goto out;
		}
		fs_devices = fs_devices->seed;
	}

	fs_devices = find_fsid(fsid);
	if (!fs_devices) {
		ret = -ENOENT;
		goto out;
	}

	fs_devices = clone_fs_devices(fs_devices);
	if (IS_ERR(fs_devices)) {
		ret = PTR_ERR(fs_devices);
		goto out;
	}

	ret = __btrfs_open_devices(fs_devices, FMODE_READ,
				   root->fs_info->bdev_holder);
	if (ret) {
		free_fs_devices(fs_devices);
		goto out;
	}

	if (!fs_devices->seeding) {
		__btrfs_close_devices(fs_devices);
		free_fs_devices(fs_devices);
		ret = -EINVAL;
		goto out;
	}

	fs_devices->seed = root->fs_info->fs_devices->seed;
	root->fs_info->fs_devices->seed = fs_devices;
out:
	return ret;
}

static int read_one_dev(struct btrfs_root *root,
			struct extent_buffer *leaf,
			struct btrfs_dev_item *dev_item)
{
	struct btrfs_device *device;
	u64 devid;
	int ret;
	u8 fs_uuid[BTRFS_UUID_SIZE];
	u8 dev_uuid[BTRFS_UUID_SIZE];

	devid = btrfs_device_id(leaf, dev_item);
	read_extent_buffer(leaf, dev_uuid,
			   (unsigned long)btrfs_device_uuid(dev_item),
			   BTRFS_UUID_SIZE);
	read_extent_buffer(leaf, fs_uuid,
			   (unsigned long)btrfs_device_fsid(dev_item),
			   BTRFS_UUID_SIZE);

	if (memcmp(fs_uuid, root->fs_info->fsid, BTRFS_UUID_SIZE)) {
		ret = open_seed_devices(root, fs_uuid);
		if (ret && !btrfs_test_opt(root, DEGRADED))
			return ret;
	}

	device = btrfs_find_device(root, devid, dev_uuid, fs_uuid);
	if (!device || !device->bdev) {
		if (!btrfs_test_opt(root, DEGRADED))
			return -EIO;

		if (!device) {
			printk(KERN_WARNING "warning devid %llu missing\n",
			       (unsigned long long)devid);
			device = add_missing_dev(root, devid, dev_uuid);
			if (!device)
				return -ENOMEM;
		} else if (!device->missing) {
			/*
			 * this happens when a device that was properly setup
			 * in the device info lists suddenly goes bad.
			 * device->bdev is NULL, and so we have to set
			 * device->missing to one here
			 */
			root->fs_info->fs_devices->missing_devices++;
			device->missing = 1;
		}
	}

	if (device->fs_devices != root->fs_info->fs_devices) {
		BUG_ON(device->writeable);
		if (device->generation !=
		    btrfs_device_generation(leaf, dev_item))
			return -EINVAL;
	}

	fill_device_from_item(leaf, dev_item, device);
	device->dev_root = root->fs_info->dev_root;
	device->in_fs_metadata = 1;
	if (device->writeable) {
		device->fs_devices->total_rw_bytes += device->total_bytes;
		spin_lock(&root->fs_info->free_chunk_lock);
		root->fs_info->free_chunk_space += device->total_bytes -
			device->bytes_used;
		spin_unlock(&root->fs_info->free_chunk_lock);
	}
	ret = 0;
	return ret;
}

int btrfs_read_sys_array(struct btrfs_root *root)
{
	struct btrfs_super_block *super_copy = root->fs_info->super_copy;
	struct extent_buffer *sb;
	struct btrfs_disk_key *disk_key;
	struct btrfs_chunk *chunk;
	u8 *ptr;
	unsigned long sb_ptr;
	int ret = 0;
	u32 num_stripes;
	u32 array_size;
	u32 len = 0;
	u32 cur;
	struct btrfs_key key;

	sb = btrfs_find_create_tree_block(root, BTRFS_SUPER_INFO_OFFSET,
					  BTRFS_SUPER_INFO_SIZE);
	if (!sb)
		return -ENOMEM;
	btrfs_set_buffer_uptodate(sb);
	btrfs_set_buffer_lockdep_class(root->root_key.objectid, sb, 0);
	/*
	 * The sb extent buffer is artifical and just used to read the system array.
	 * btrfs_set_buffer_uptodate() call does not properly mark all it's
	 * pages up-to-date when the page is larger: extent does not cover the
	 * whole page and consequently check_page_uptodate does not find all
	 * the page's extents up-to-date (the hole beyond sb),
	 * write_extent_buffer then triggers a WARN_ON.
	 *
	 * Regular short extents go through mark_extent_buffer_dirty/writeback cycle,
	 * but sb spans only this function. Add an explicit SetPageUptodate call
	 * to silence the warning eg. on PowerPC 64.
	 */
	if (PAGE_CACHE_SIZE > BTRFS_SUPER_INFO_SIZE)
		SetPageUptodate(sb->pages[0]);

	write_extent_buffer(sb, super_copy, 0, BTRFS_SUPER_INFO_SIZE);
	array_size = btrfs_super_sys_array_size(super_copy);

	ptr = super_copy->sys_chunk_array;
	sb_ptr = offsetof(struct btrfs_super_block, sys_chunk_array);
	cur = 0;

	while (cur < array_size) {
		disk_key = (struct btrfs_disk_key *)ptr;
		btrfs_disk_key_to_cpu(&key, disk_key);

		len = sizeof(*disk_key); ptr += len;
		sb_ptr += len;
		cur += len;

		if (key.type == BTRFS_CHUNK_ITEM_KEY) {
			chunk = (struct btrfs_chunk *)sb_ptr;
			ret = read_one_chunk(root, &key, sb, chunk);
			if (ret)
				break;
			num_stripes = btrfs_chunk_num_stripes(sb, chunk);
			len = btrfs_chunk_item_size(num_stripes);
		} else {
			ret = -EIO;
			break;
		}
		ptr += len;
		sb_ptr += len;
		cur += len;
	}
	free_extent_buffer(sb);
	return ret;
}

struct btrfs_device *btrfs_find_device_for_logical(struct btrfs_root *root,
						   u64 logical, int mirror_num)
{
	struct btrfs_mapping_tree *map_tree = &root->fs_info->mapping_tree;
	int ret;
	u64 map_length = 0;
	struct btrfs_bio *bbio = NULL;
	struct btrfs_device *device;

	BUG_ON(mirror_num == 0);
	ret = btrfs_map_block(map_tree, WRITE, logical, &map_length, &bbio,
			      mirror_num);
	if (ret) {
		BUG_ON(bbio != NULL);
		return NULL;
	}
	BUG_ON(mirror_num != bbio->mirror_num);
	device = bbio->stripes[mirror_num - 1].dev;
	kfree(bbio);
	return device;
}

int btrfs_read_chunk_tree(struct btrfs_root *root)
{
	struct btrfs_path *path;
	struct extent_buffer *leaf;
	struct btrfs_key key;
	struct btrfs_key found_key;
	int ret;
	int slot;

	root = root->fs_info->chunk_root;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	mutex_lock(&uuid_mutex);
	lock_chunks(root);

	/* first we search for all of the device items, and then we
	 * read in all of the chunk items.  This way we can create chunk
	 * mappings that reference all of the devices that are afound
	 */
	key.objectid = BTRFS_DEV_ITEMS_OBJECTID;
	key.offset = 0;
	key.type = 0;
again:
	ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
	if (ret < 0)
		goto error;
	while (1) {
		leaf = path->nodes[0];
		slot = path->slots[0];
		if (slot >= btrfs_header_nritems(leaf)) {
			ret = btrfs_next_leaf(root, path);
			if (ret == 0)
				continue;
			if (ret < 0)
				goto error;
			break;
		}
		btrfs_item_key_to_cpu(leaf, &found_key, slot);
		if (key.objectid == BTRFS_DEV_ITEMS_OBJECTID) {
			if (found_key.objectid != BTRFS_DEV_ITEMS_OBJECTID)
				break;
			if (found_key.type == BTRFS_DEV_ITEM_KEY) {
				struct btrfs_dev_item *dev_item;
				dev_item = btrfs_item_ptr(leaf, slot,
						  struct btrfs_dev_item);
				ret = read_one_dev(root, leaf, dev_item);
				if (ret)
					goto error;
			}
		} else if (found_key.type == BTRFS_CHUNK_ITEM_KEY) {
			struct btrfs_chunk *chunk;
			chunk = btrfs_item_ptr(leaf, slot, struct btrfs_chunk);
			ret = read_one_chunk(root, &found_key, leaf, chunk);
			if (ret)
				goto error;
		}
		path->slots[0]++;
	}
	if (key.objectid == BTRFS_DEV_ITEMS_OBJECTID) {
		key.objectid = 0;
		btrfs_release_path(path);
		goto again;
	}
	ret = 0;
error:
	unlock_chunks(root);
	mutex_unlock(&uuid_mutex);

	btrfs_free_path(path);
	return ret;
}

static void __btrfs_reset_dev_stats(struct btrfs_device *dev)
{
	int i;

	for (i = 0; i < BTRFS_DEV_STAT_VALUES_MAX; i++)
		btrfs_dev_stat_reset(dev, i);
}

int btrfs_init_dev_stats(struct btrfs_fs_info *fs_info)
{
	struct btrfs_key key;
	struct btrfs_key found_key;
	struct btrfs_root *dev_root = fs_info->dev_root;
	struct btrfs_fs_devices *fs_devices = fs_info->fs_devices;
	struct extent_buffer *eb;
	int slot;
	int ret = 0;
	struct btrfs_device *device;
	struct btrfs_path *path = NULL;
	int i;

	path = btrfs_alloc_path();
	if (!path) {
		ret = -ENOMEM;
		goto out;
	}

	mutex_lock(&fs_devices->device_list_mutex);
	list_for_each_entry(device, &fs_devices->devices, dev_list) {
		int item_size;
		struct btrfs_dev_stats_item *ptr;

		key.objectid = 0;
		key.type = BTRFS_DEV_STATS_KEY;
		key.offset = device->devid;
		ret = btrfs_search_slot(NULL, dev_root, &key, path, 0, 0);
		if (ret) {
			printk_in_rcu(KERN_WARNING "btrfs: no dev_stats entry found for device %s (devid %llu) (OK on first mount after mkfs)\n",
				      rcu_str_deref(device->name),
				      (unsigned long long)device->devid);
			__btrfs_reset_dev_stats(device);
			device->dev_stats_valid = 1;
			btrfs_release_path(path);
			continue;
		}
		slot = path->slots[0];
		eb = path->nodes[0];
		btrfs_item_key_to_cpu(eb, &found_key, slot);
		item_size = btrfs_item_size_nr(eb, slot);

		ptr = btrfs_item_ptr(eb, slot,
				     struct btrfs_dev_stats_item);

		for (i = 0; i < BTRFS_DEV_STAT_VALUES_MAX; i++) {
			if (item_size >= (1 + i) * sizeof(__le64))
				btrfs_dev_stat_set(device, i,
					btrfs_dev_stats_value(eb, ptr, i));
			else
				btrfs_dev_stat_reset(device, i);
		}

		device->dev_stats_valid = 1;
		btrfs_dev_stat_print_on_load(device);
		btrfs_release_path(path);
	}
	mutex_unlock(&fs_devices->device_list_mutex);

out:
	btrfs_free_path(path);
	return ret < 0 ? ret : 0;
}

static int update_dev_stat_item(struct btrfs_trans_handle *trans,
				struct btrfs_root *dev_root,
				struct btrfs_device *device)
{
	struct btrfs_path *path;
	struct btrfs_key key;
	struct extent_buffer *eb;
	struct btrfs_dev_stats_item *ptr;
	int ret;
	int i;

	key.objectid = 0;
	key.type = BTRFS_DEV_STATS_KEY;
	key.offset = device->devid;

	path = btrfs_alloc_path();
	BUG_ON(!path);
	ret = btrfs_search_slot(trans, dev_root, &key, path, -1, 1);
	if (ret < 0) {
		printk_in_rcu(KERN_WARNING "btrfs: error %d while searching for dev_stats item for device %s!\n",
			      ret, rcu_str_deref(device->name));
		goto out;
	}

	if (ret == 0 &&
	    btrfs_item_size_nr(path->nodes[0], path->slots[0]) < sizeof(*ptr)) {
		/* need to delete old one and insert a new one */
		ret = btrfs_del_item(trans, dev_root, path);
		if (ret != 0) {
			printk_in_rcu(KERN_WARNING "btrfs: delete too small dev_stats item for device %s failed %d!\n",
				      rcu_str_deref(device->name), ret);
			goto out;
		}
		ret = 1;
	}

	if (ret == 1) {
		/* need to insert a new item */
		btrfs_release_path(path);
		ret = btrfs_insert_empty_item(trans, dev_root, path,
					      &key, sizeof(*ptr));
		if (ret < 0) {
			printk_in_rcu(KERN_WARNING "btrfs: insert dev_stats item for device %s failed %d!\n",
				      rcu_str_deref(device->name), ret);
			goto out;
		}
	}

	eb = path->nodes[0];
	ptr = btrfs_item_ptr(eb, path->slots[0], struct btrfs_dev_stats_item);
	for (i = 0; i < BTRFS_DEV_STAT_VALUES_MAX; i++)
		btrfs_set_dev_stats_value(eb, ptr, i,
					  btrfs_dev_stat_read(device, i));
	btrfs_mark_buffer_dirty(eb);

out:
	btrfs_free_path(path);
	return ret;
}

/*
 * called from commit_transaction. Writes all changed device stats to disk.
 */
int btrfs_run_dev_stats(struct btrfs_trans_handle *trans,
			struct btrfs_fs_info *fs_info)
{
	struct btrfs_root *dev_root = fs_info->dev_root;
	struct btrfs_fs_devices *fs_devices = fs_info->fs_devices;
	struct btrfs_device *device;
	int ret = 0;

	mutex_lock(&fs_devices->device_list_mutex);
	list_for_each_entry(device, &fs_devices->devices, dev_list) {
		if (!device->dev_stats_valid || !device->dev_stats_dirty)
			continue;

		ret = update_dev_stat_item(trans, dev_root, device);
		if (!ret)
			device->dev_stats_dirty = 0;
	}
	mutex_unlock(&fs_devices->device_list_mutex);

	return ret;
}

void btrfs_dev_stat_inc_and_print(struct btrfs_device *dev, int index)
{
	btrfs_dev_stat_inc(dev, index);
	btrfs_dev_stat_print_on_error(dev);
}

void btrfs_dev_stat_print_on_error(struct btrfs_device *dev)
{
	if (!dev->dev_stats_valid)
		return;
	printk_ratelimited_in_rcu(KERN_ERR
			   "btrfs: bdev %s errs: wr %u, rd %u, flush %u, corrupt %u, gen %u\n",
			   rcu_str_deref(dev->name),
			   btrfs_dev_stat_read(dev, BTRFS_DEV_STAT_WRITE_ERRS),
			   btrfs_dev_stat_read(dev, BTRFS_DEV_STAT_READ_ERRS),
			   btrfs_dev_stat_read(dev, BTRFS_DEV_STAT_FLUSH_ERRS),
			   btrfs_dev_stat_read(dev,
					       BTRFS_DEV_STAT_CORRUPTION_ERRS),
			   btrfs_dev_stat_read(dev,
					       BTRFS_DEV_STAT_GENERATION_ERRS));
}

static void btrfs_dev_stat_print_on_load(struct btrfs_device *dev)
{
	printk_in_rcu(KERN_INFO "btrfs: bdev %s errs: wr %u, rd %u, flush %u, corrupt %u, gen %u\n",
	       rcu_str_deref(dev->name),
	       btrfs_dev_stat_read(dev, BTRFS_DEV_STAT_WRITE_ERRS),
	       btrfs_dev_stat_read(dev, BTRFS_DEV_STAT_READ_ERRS),
	       btrfs_dev_stat_read(dev, BTRFS_DEV_STAT_FLUSH_ERRS),
	       btrfs_dev_stat_read(dev, BTRFS_DEV_STAT_CORRUPTION_ERRS),
	       btrfs_dev_stat_read(dev, BTRFS_DEV_STAT_GENERATION_ERRS));
}

int btrfs_get_dev_stats(struct btrfs_root *root,
			struct btrfs_ioctl_get_dev_stats *stats)
{
	struct btrfs_device *dev;
	struct btrfs_fs_devices *fs_devices = root->fs_info->fs_devices;
	int i;

	mutex_lock(&fs_devices->device_list_mutex);
	dev = btrfs_find_device(root, stats->devid, NULL, NULL);
	mutex_unlock(&fs_devices->device_list_mutex);

	if (!dev) {
		printk(KERN_WARNING
		       "btrfs: get dev_stats failed, device not found\n");
		return -ENODEV;
	} else if (!dev->dev_stats_valid) {
		printk(KERN_WARNING
		       "btrfs: get dev_stats failed, not yet valid\n");
		return -ENODEV;
	} else if (stats->flags & BTRFS_DEV_STATS_RESET) {
		for (i = 0; i < BTRFS_DEV_STAT_VALUES_MAX; i++) {
			if (stats->nr_items > i)
				stats->values[i] =
					btrfs_dev_stat_read_and_reset(dev, i);
			else
				btrfs_dev_stat_reset(dev, i);
		}
	} else {
		for (i = 0; i < BTRFS_DEV_STAT_VALUES_MAX; i++)
			if (stats->nr_items > i)
				stats->values[i] = btrfs_dev_stat_read(dev, i);
	}
	if (stats->nr_items > BTRFS_DEV_STAT_VALUES_MAX)
		stats->nr_items = BTRFS_DEV_STAT_VALUES_MAX;
	return 0;
}
