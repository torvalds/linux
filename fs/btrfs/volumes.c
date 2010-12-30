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
#include <asm/div64.h>
#include "compat.h"
#include "ctree.h"
#include "extent_map.h"
#include "disk-io.h"
#include "transaction.h"
#include "print-tree.h"
#include "volumes.h"
#include "async-thread.h"

struct map_lookup {
	u64 type;
	int io_align;
	int io_width;
	int stripe_len;
	int sector_size;
	int num_stripes;
	int sub_stripes;
	struct btrfs_bio_stripe stripes[];
};

static int init_first_rw_device(struct btrfs_trans_handle *trans,
				struct btrfs_root *root,
				struct btrfs_device *device);
static int btrfs_relocate_sys_chunks(struct btrfs_root *root);

#define map_lookup_size(n) (sizeof(struct map_lookup) + \
			    (sizeof(struct btrfs_bio_stripe) * (n)))

static DEFINE_MUTEX(uuid_mutex);
static LIST_HEAD(fs_uuids);

void btrfs_lock_volumes(void)
{
	mutex_lock(&uuid_mutex);
}

void btrfs_unlock_volumes(void)
{
	mutex_unlock(&uuid_mutex);
}

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
		kfree(device->name);
		kfree(device);
	}
	kfree(fs_devices);
}

int btrfs_cleanup_fs_uuids(void)
{
	struct btrfs_fs_devices *fs_devices;

	while (!list_empty(&fs_uuids)) {
		fs_devices = list_entry(fs_uuids.next,
					struct btrfs_fs_devices, list);
		list_del(&fs_devices->list);
		free_fs_devices(fs_devices);
	}
	return 0;
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
static noinline int run_scheduled_bios(struct btrfs_device *device)
{
	struct bio *pending;
	struct backing_dev_info *bdi;
	struct btrfs_fs_info *fs_info;
	struct btrfs_pending_bios *pending_bios;
	struct bio *tail;
	struct bio *cur;
	int again = 0;
	unsigned long num_run;
	unsigned long num_sync_run;
	unsigned long batch_run = 0;
	unsigned long limit;
	unsigned long last_waited = 0;
	int force_reg = 0;

	bdi = blk_get_backing_dev_info(device->bdev);
	fs_info = device->dev_root->fs_info;
	limit = btrfs_async_submit_limit(fs_info);
	limit = limit * 2 / 3;

	/* we want to make sure that every time we switch from the sync
	 * list to the normal list, we unplug
	 */
	num_sync_run = 0;

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

	/*
	 * if we're doing the regular priority list, make sure we unplug
	 * for any high prio bios we've sent down
	 */
	if (pending_bios == &device->pending_bios && num_sync_run > 0) {
		num_sync_run = 0;
		blk_run_backing_dev(bdi, NULL);
	}

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

		if (cur->bi_rw & REQ_SYNC)
			num_sync_run++;

		submit_bio(cur->bi_rw, cur);
		num_run++;
		batch_run++;
		if (need_resched()) {
			if (num_sync_run) {
				blk_run_backing_dev(bdi, NULL);
				num_sync_run = 0;
			}
			cond_resched();
		}

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
				if (need_resched()) {
					if (num_sync_run) {
						blk_run_backing_dev(bdi, NULL);
						num_sync_run = 0;
					}
					cond_resched();
				}
				continue;
			}
			spin_lock(&device->io_lock);
			requeue_list(pending_bios, pending, tail);
			device->running_pending = 1;

			spin_unlock(&device->io_lock);
			btrfs_requeue_work(&device->work);
			goto done;
		}
	}

	if (num_sync_run) {
		num_sync_run = 0;
		blk_run_backing_dev(bdi, NULL);
	}
	/*
	 * IO has already been through a long path to get here.  Checksumming,
	 * async helper threads, perhaps compression.  We've done a pretty
	 * good job of collecting a batch of IO and should just unplug
	 * the device right away.
	 *
	 * This will help anyone who is waiting on the IO, they might have
	 * already unplugged, but managed to do so before the bio they
	 * cared about found its way down here.
	 */
	blk_run_backing_dev(bdi, NULL);

	cond_resched();
	if (again)
		goto loop;

	spin_lock(&device->io_lock);
	if (device->pending_bios.head || device->pending_sync_bios.head)
		goto loop_lock;
	spin_unlock(&device->io_lock);

done:
	return 0;
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
	u64 found_transid = btrfs_super_generation(disk_super);
	char *name;

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
		device->work.func = pending_bios_fn;
		memcpy(device->uuid, disk_super->dev_item.uuid,
		       BTRFS_UUID_SIZE);
		spin_lock_init(&device->io_lock);
		device->name = kstrdup(path, GFP_NOFS);
		if (!device->name) {
			kfree(device);
			return -ENOMEM;
		}
		INIT_LIST_HEAD(&device->dev_alloc_list);

		mutex_lock(&fs_devices->device_list_mutex);
		list_add(&device->dev_list, &fs_devices->devices);
		mutex_unlock(&fs_devices->device_list_mutex);

		device->fs_devices = fs_devices;
		fs_devices->num_devices++;
	} else if (!device->name || strcmp(device->name, path)) {
		name = kstrdup(path, GFP_NOFS);
		if (!name)
			return -ENOMEM;
		kfree(device->name);
		device->name = name;
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

	mutex_lock(&orig->device_list_mutex);
	list_for_each_entry(orig_dev, &orig->devices, dev_list) {
		device = kzalloc(sizeof(*device), GFP_NOFS);
		if (!device)
			goto error;

		device->name = kstrdup(orig_dev->name, GFP_NOFS);
		if (!device->name) {
			kfree(device);
			goto error;
		}

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
	mutex_unlock(&orig->device_list_mutex);
	return fs_devices;
error:
	mutex_unlock(&orig->device_list_mutex);
	free_fs_devices(fs_devices);
	return ERR_PTR(-ENOMEM);
}

int btrfs_close_extra_devices(struct btrfs_fs_devices *fs_devices)
{
	struct btrfs_device *device, *next;

	mutex_lock(&uuid_mutex);
again:
	mutex_lock(&fs_devices->device_list_mutex);
	list_for_each_entry_safe(device, next, &fs_devices->devices, dev_list) {
		if (device->in_fs_metadata)
			continue;

		if (device->bdev) {
			close_bdev_exclusive(device->bdev, device->mode);
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
		kfree(device->name);
		kfree(device);
	}
	mutex_unlock(&fs_devices->device_list_mutex);

	if (fs_devices->seed) {
		fs_devices = fs_devices->seed;
		goto again;
	}

	mutex_unlock(&uuid_mutex);
	return 0;
}

static int __btrfs_close_devices(struct btrfs_fs_devices *fs_devices)
{
	struct btrfs_device *device;

	if (--fs_devices->opened > 0)
		return 0;

	list_for_each_entry(device, &fs_devices->devices, dev_list) {
		if (device->bdev) {
			close_bdev_exclusive(device->bdev, device->mode);
			fs_devices->open_devices--;
		}
		if (device->writeable) {
			list_del_init(&device->dev_alloc_list);
			fs_devices->rw_devices--;
		}

		device->bdev = NULL;
		device->writeable = 0;
		device->in_fs_metadata = 0;
	}
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

	list_for_each_entry(device, head, dev_list) {
		if (device->bdev)
			continue;
		if (!device->name)
			continue;

		bdev = open_bdev_exclusive(device->name, flags, holder);
		if (IS_ERR(bdev)) {
			printk(KERN_INFO "open %s failed\n", device->name);
			goto error;
		}
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
		continue;

error_brelse:
		brelse(bh);
error_close:
		close_bdev_exclusive(bdev, FMODE_READ);
error:
		continue;
	}
	if (fs_devices->open_devices == 0) {
		ret = -EIO;
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

	mutex_lock(&uuid_mutex);

	bdev = open_bdev_exclusive(path, flags, holder);

	if (IS_ERR(bdev)) {
		ret = PTR_ERR(bdev);
		goto error;
	}

	ret = set_blocksize(bdev, 4096);
	if (ret)
		goto error_close;
	bh = btrfs_read_dev_super(bdev);
	if (!bh) {
		ret = -EIO;
		goto error_close;
	}
	disk_super = (struct btrfs_super_block *)bh->b_data;
	devid = btrfs_stack_device_id(&disk_super->dev_item);
	transid = btrfs_super_generation(disk_super);
	if (disk_super->label[0])
		printk(KERN_INFO "device label %s ", disk_super->label);
	else {
		/* FIXME, make a readl uuid parser */
		printk(KERN_INFO "device fsid %llx-%llx ",
		       *(unsigned long long *)disk_super->fsid,
		       *(unsigned long long *)(disk_super->fsid + 8));
	}
	printk(KERN_CONT "devid %llu transid %llu %s\n",
	       (unsigned long long)devid, (unsigned long long)transid, path);
	ret = device_list_add(path, disk_super, devid, fs_devices_ret);

	brelse(bh);
error_close:
	close_bdev_exclusive(bdev, flags);
error:
	mutex_unlock(&uuid_mutex);
	return ret;
}

/*
 * this uses a pretty simple search, the expectation is that it is
 * called very infrequently and that a given device has a small number
 * of extents
 */
int find_free_dev_extent(struct btrfs_trans_handle *trans,
			 struct btrfs_device *device, u64 num_bytes,
			 u64 *start, u64 *max_avail)
{
	struct btrfs_key key;
	struct btrfs_root *root = device->dev_root;
	struct btrfs_dev_extent *dev_extent = NULL;
	struct btrfs_path *path;
	u64 hole_size = 0;
	u64 last_byte = 0;
	u64 search_start = 0;
	u64 search_end = device->total_bytes;
	int ret;
	int slot = 0;
	int start_found;
	struct extent_buffer *l;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;
	path->reada = 2;
	start_found = 0;

	/* FIXME use last free of some kind */

	/* we don't want to overwrite the superblock on the drive,
	 * so we make sure to start at an offset of at least 1MB
	 */
	search_start = max((u64)1024 * 1024, search_start);

	if (root->fs_info->alloc_start + num_bytes <= device->total_bytes)
		search_start = max(root->fs_info->alloc_start, search_start);

	key.objectid = device->devid;
	key.offset = search_start;
	key.type = BTRFS_DEV_EXTENT_KEY;
	ret = btrfs_search_slot(trans, root, &key, path, 0, 0);
	if (ret < 0)
		goto error;
	if (ret > 0) {
		ret = btrfs_previous_item(root, path, key.objectid, key.type);
		if (ret < 0)
			goto error;
		if (ret > 0)
			start_found = 1;
	}
	l = path->nodes[0];
	btrfs_item_key_to_cpu(l, &key, path->slots[0]);
	while (1) {
		l = path->nodes[0];
		slot = path->slots[0];
		if (slot >= btrfs_header_nritems(l)) {
			ret = btrfs_next_leaf(root, path);
			if (ret == 0)
				continue;
			if (ret < 0)
				goto error;
no_more_items:
			if (!start_found) {
				if (search_start >= search_end) {
					ret = -ENOSPC;
					goto error;
				}
				*start = search_start;
				start_found = 1;
				goto check_pending;
			}
			*start = last_byte > search_start ?
				last_byte : search_start;
			if (search_end <= *start) {
				ret = -ENOSPC;
				goto error;
			}
			goto check_pending;
		}
		btrfs_item_key_to_cpu(l, &key, slot);

		if (key.objectid < device->devid)
			goto next;

		if (key.objectid > device->devid)
			goto no_more_items;

		if (key.offset >= search_start && key.offset > last_byte &&
		    start_found) {
			if (last_byte < search_start)
				last_byte = search_start;
			hole_size = key.offset - last_byte;

			if (hole_size > *max_avail)
				*max_avail = hole_size;

			if (key.offset > last_byte &&
			    hole_size >= num_bytes) {
				*start = last_byte;
				goto check_pending;
			}
		}
		if (btrfs_key_type(&key) != BTRFS_DEV_EXTENT_KEY)
			goto next;

		start_found = 1;
		dev_extent = btrfs_item_ptr(l, slot, struct btrfs_dev_extent);
		last_byte = key.offset + btrfs_dev_extent_length(l, dev_extent);
next:
		path->slots[0]++;
		cond_resched();
	}
check_pending:
	/* we have to make sure we didn't find an extent that has already
	 * been allocated by the map tree or the original allocation
	 */
	BUG_ON(*start < search_start);

	if (*start + num_bytes > search_end) {
		ret = -ENOSPC;
		goto error;
	}
	/* check for pending inserts here */
	ret = 0;

error:
	btrfs_free_path(path);
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

	ret = btrfs_search_slot(trans, root, &key, path, -1, 1);
	if (ret > 0) {
		ret = btrfs_previous_item(root, path, key.objectid,
					  BTRFS_DEV_EXTENT_KEY);
		BUG_ON(ret);
		leaf = path->nodes[0];
		btrfs_item_key_to_cpu(leaf, &found_key, path->slots[0]);
		extent = btrfs_item_ptr(leaf, path->slots[0],
					struct btrfs_dev_extent);
		BUG_ON(found_key.offset > start || found_key.offset +
		       btrfs_dev_extent_length(leaf, extent) < start);
		ret = 0;
	} else if (ret == 0) {
		leaf = path->nodes[0];
		extent = btrfs_item_ptr(leaf, path->slots[0],
					struct btrfs_dev_extent);
	}
	BUG_ON(ret);

	if (device->bytes_used > 0)
		device->bytes_used -= btrfs_dev_extent_length(leaf, extent);
	ret = btrfs_del_item(trans, root, path);
	BUG_ON(ret);

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
	BUG_ON(ret);

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
	BUG_ON(!path);

	key.objectid = objectid;
	key.offset = (u64)-1;
	key.type = BTRFS_CHUNK_ITEM_KEY;

	ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
	if (ret < 0)
		goto error;

	BUG_ON(ret == 0);

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

	BUG_ON(ret == 0);

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
	u64 all_avail;
	u64 devid;
	u64 num_devices;
	u8 *dev_uuid;
	int ret = 0;

	mutex_lock(&uuid_mutex);
	mutex_lock(&root->fs_info->volume_mutex);

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
		mutex_lock(&root->fs_info->fs_devices->device_list_mutex);
		list_for_each_entry(tmp, devices, dev_list) {
			if (tmp->in_fs_metadata && !tmp->bdev) {
				device = tmp;
				break;
			}
		}
		mutex_unlock(&root->fs_info->fs_devices->device_list_mutex);
		bdev = NULL;
		bh = NULL;
		disk_super = NULL;
		if (!device) {
			printk(KERN_ERR "btrfs: no missing devices found to "
			       "remove\n");
			goto out;
		}
	} else {
		bdev = open_bdev_exclusive(device_path, FMODE_READ,
				      root->fs_info->bdev_holder);
		if (IS_ERR(bdev)) {
			ret = PTR_ERR(bdev);
			goto out;
		}

		set_blocksize(bdev, 4096);
		bh = btrfs_read_dev_super(bdev);
		if (!bh) {
			ret = -EIO;
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
		list_del_init(&device->dev_alloc_list);
		root->fs_info->fs_devices->rw_devices--;
	}

	ret = btrfs_shrink_device(device, 0);
	if (ret)
		goto error_brelse;

	ret = btrfs_rm_dev_item(root->fs_info->chunk_root, device);
	if (ret)
		goto error_brelse;

	device->in_fs_metadata = 0;

	/*
	 * the device list mutex makes sure that we don't change
	 * the device list while someone else is writing out all
	 * the device supers.
	 */
	mutex_lock(&root->fs_info->fs_devices->device_list_mutex);
	list_del_init(&device->dev_list);
	mutex_unlock(&root->fs_info->fs_devices->device_list_mutex);

	device->fs_devices->num_devices--;

	if (device->missing)
		root->fs_info->fs_devices->missing_devices--;

	next_device = list_entry(root->fs_info->fs_devices->devices.next,
				 struct btrfs_device, dev_list);
	if (device->bdev == root->fs_info->sb->s_bdev)
		root->fs_info->sb->s_bdev = next_device->bdev;
	if (device->bdev == root->fs_info->fs_devices->latest_bdev)
		root->fs_info->fs_devices->latest_bdev = next_device->bdev;

	if (device->bdev) {
		close_bdev_exclusive(device->bdev, device->mode);
		device->bdev = NULL;
		device->fs_devices->open_devices--;
	}

	num_devices = btrfs_super_num_devices(&root->fs_info->super_copy) - 1;
	btrfs_set_super_num_devices(&root->fs_info->super_copy, num_devices);

	if (device->fs_devices->open_devices == 0) {
		struct btrfs_fs_devices *fs_devices;
		fs_devices = root->fs_info->fs_devices;
		while (fs_devices) {
			if (fs_devices->seed == device->fs_devices)
				break;
			fs_devices = fs_devices->seed;
		}
		fs_devices->seed = device->fs_devices->seed;
		device->fs_devices->seed = NULL;
		__btrfs_close_devices(device->fs_devices);
		free_fs_devices(device->fs_devices);
	}

	/*
	 * at this point, the device is zero sized.  We want to
	 * remove it from the devices list and zero out the old super
	 */
	if (device->writeable) {
		/* make sure this device isn't detected as part of
		 * the FS anymore
		 */
		memset(&disk_super->magic, 0, sizeof(disk_super->magic));
		set_buffer_dirty(bh);
		sync_dirty_buffer(bh);
	}

	kfree(device->name);
	kfree(device);
	ret = 0;

error_brelse:
	brelse(bh);
error_close:
	if (bdev)
		close_bdev_exclusive(bdev, FMODE_READ);
out:
	mutex_unlock(&root->fs_info->volume_mutex);
	mutex_unlock(&uuid_mutex);
	return ret;
}

/*
 * does all the dirty work required for changing file system's UUID.
 */
static int btrfs_prepare_sprout(struct btrfs_trans_handle *trans,
				struct btrfs_root *root)
{
	struct btrfs_fs_devices *fs_devices = root->fs_info->fs_devices;
	struct btrfs_fs_devices *old_devices;
	struct btrfs_fs_devices *seed_devices;
	struct btrfs_super_block *disk_super = &root->fs_info->super_copy;
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
	list_splice_init(&fs_devices->devices, &seed_devices->devices);
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
			btrfs_release_path(root, path);
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
		BUG_ON(!device);

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
	struct btrfs_trans_handle *trans;
	struct btrfs_device *device;
	struct block_device *bdev;
	struct list_head *devices;
	struct super_block *sb = root->fs_info->sb;
	u64 total_bytes;
	int seeding_dev = 0;
	int ret = 0;

	if ((sb->s_flags & MS_RDONLY) && !root->fs_info->fs_devices->seeding)
		return -EINVAL;

	bdev = open_bdev_exclusive(device_path, 0, root->fs_info->bdev_holder);
	if (IS_ERR(bdev))
		return PTR_ERR(bdev);

	if (root->fs_info->fs_devices->seeding) {
		seeding_dev = 1;
		down_write(&sb->s_umount);
		mutex_lock(&uuid_mutex);
	}

	filemap_write_and_wait(bdev->bd_inode->i_mapping);
	mutex_lock(&root->fs_info->volume_mutex);

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

	device->name = kstrdup(device_path, GFP_NOFS);
	if (!device->name) {
		kfree(device);
		ret = -ENOMEM;
		goto error;
	}

	ret = find_next_devid(root, &device->devid);
	if (ret) {
		kfree(device);
		goto error;
	}

	trans = btrfs_start_transaction(root, 0);
	lock_chunks(root);

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
	device->mode = 0;
	set_blocksize(device->bdev, 4096);

	if (seeding_dev) {
		sb->s_flags &= ~MS_RDONLY;
		ret = btrfs_prepare_sprout(trans, root);
		BUG_ON(ret);
	}

	device->fs_devices = root->fs_info->fs_devices;

	/*
	 * we don't want write_supers to jump in here with our device
	 * half setup
	 */
	mutex_lock(&root->fs_info->fs_devices->device_list_mutex);
	list_add(&device->dev_list, &root->fs_info->fs_devices->devices);
	list_add(&device->dev_alloc_list,
		 &root->fs_info->fs_devices->alloc_list);
	root->fs_info->fs_devices->num_devices++;
	root->fs_info->fs_devices->open_devices++;
	root->fs_info->fs_devices->rw_devices++;
	root->fs_info->fs_devices->total_rw_bytes += device->total_bytes;

	if (!blk_queue_nonrot(bdev_get_queue(bdev)))
		root->fs_info->fs_devices->rotating = 1;

	total_bytes = btrfs_super_total_bytes(&root->fs_info->super_copy);
	btrfs_set_super_total_bytes(&root->fs_info->super_copy,
				    total_bytes + device->total_bytes);

	total_bytes = btrfs_super_num_devices(&root->fs_info->super_copy);
	btrfs_set_super_num_devices(&root->fs_info->super_copy,
				    total_bytes + 1);
	mutex_unlock(&root->fs_info->fs_devices->device_list_mutex);

	if (seeding_dev) {
		ret = init_first_rw_device(trans, root, device);
		BUG_ON(ret);
		ret = btrfs_finish_sprout(trans, root);
		BUG_ON(ret);
	} else {
		ret = btrfs_add_device(trans, root, device);
	}

	/*
	 * we've got more storage, clear any full flags on the space
	 * infos
	 */
	btrfs_clear_space_info_full(root->fs_info);

	unlock_chunks(root);
	btrfs_commit_transaction(trans, root);

	if (seeding_dev) {
		mutex_unlock(&uuid_mutex);
		up_write(&sb->s_umount);

		ret = btrfs_relocate_sys_chunks(root);
		BUG_ON(ret);
	}
out:
	mutex_unlock(&root->fs_info->volume_mutex);
	return ret;
error:
	close_bdev_exclusive(bdev, 0);
	if (seeding_dev) {
		mutex_unlock(&uuid_mutex);
		up_write(&sb->s_umount);
	}
	goto out;
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
		&device->dev_root->fs_info->super_copy;
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
	BUG_ON(ret);

	ret = btrfs_del_item(trans, root, path);
	BUG_ON(ret);

	btrfs_free_path(path);
	return 0;
}

static int btrfs_del_sys_chunk(struct btrfs_root *root, u64 chunk_objectid, u64
			chunk_offset)
{
	struct btrfs_super_block *super_copy = &root->fs_info->super_copy;
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
	BUG_ON(!trans);

	lock_chunks(root);

	/*
	 * step two, delete the device extents and the
	 * chunk tree entries
	 */
	read_lock(&em_tree->lock);
	em = lookup_extent_mapping(em_tree, chunk_offset, 1);
	read_unlock(&em_tree->lock);

	BUG_ON(em->start > chunk_offset ||
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
		BUG_ON(ret == 0);

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
		btrfs_release_path(chunk_root, path);

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

static u64 div_factor(u64 num, int factor)
{
	if (factor == 10)
		return num;
	num *= factor;
	do_div(num, 10);
	return num;
}

int btrfs_balance(struct btrfs_root *dev_root)
{
	int ret;
	struct list_head *devices = &dev_root->fs_info->fs_devices->devices;
	struct btrfs_device *device;
	u64 old_size;
	u64 size_to_free;
	struct btrfs_path *path;
	struct btrfs_key key;
	struct btrfs_root *chunk_root = dev_root->fs_info->chunk_root;
	struct btrfs_trans_handle *trans;
	struct btrfs_key found_key;

	if (dev_root->fs_info->sb->s_flags & MS_RDONLY)
		return -EROFS;

	mutex_lock(&dev_root->fs_info->volume_mutex);
	dev_root = dev_root->fs_info->dev_root;

	/* step one make some room on all the devices */
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
		BUG_ON(!trans);

		ret = btrfs_grow_device(trans, device, old_size);
		BUG_ON(ret);

		btrfs_end_transaction(trans, dev_root);
	}

	/* step two, relocate all the chunks */
	path = btrfs_alloc_path();
	BUG_ON(!path);

	key.objectid = BTRFS_FIRST_CHUNK_TREE_OBJECTID;
	key.offset = (u64)-1;
	key.type = BTRFS_CHUNK_ITEM_KEY;

	while (1) {
		ret = btrfs_search_slot(NULL, chunk_root, &key, path, 0, 0);
		if (ret < 0)
			goto error;

		/*
		 * this shouldn't happen, it means the last relocate
		 * failed
		 */
		if (ret == 0)
			break;

		ret = btrfs_previous_item(chunk_root, path, 0,
					  BTRFS_CHUNK_ITEM_KEY);
		if (ret)
			break;

		btrfs_item_key_to_cpu(path->nodes[0], &found_key,
				      path->slots[0]);
		if (found_key.objectid != key.objectid)
			break;

		/* chunk zero is special */
		if (found_key.offset == 0)
			break;

		btrfs_release_path(chunk_root, path);
		ret = btrfs_relocate_chunk(chunk_root,
					   chunk_root->root_key.objectid,
					   found_key.objectid,
					   found_key.offset);
		BUG_ON(ret && ret != -ENOSPC);
		key.offset = found_key.offset - 1;
	}
	ret = 0;
error:
	btrfs_free_path(path);
	mutex_unlock(&dev_root->fs_info->volume_mutex);
	return ret;
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
	struct btrfs_super_block *super_copy = &root->fs_info->super_copy;
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
	if (device->writeable)
		device->fs_devices->total_rw_bytes -= diff;
	unlock_chunks(root);

again:
	key.objectid = device->devid;
	key.offset = (u64)-1;
	key.type = BTRFS_DEV_EXTENT_KEY;

	while (1) {
		ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
		if (ret < 0)
			goto done;

		ret = btrfs_previous_item(root, path, 0, key.type);
		if (ret < 0)
			goto done;
		if (ret) {
			ret = 0;
			btrfs_release_path(root, path);
			break;
		}

		l = path->nodes[0];
		slot = path->slots[0];
		btrfs_item_key_to_cpu(l, &key, path->slots[0]);

		if (key.objectid != device->devid) {
			btrfs_release_path(root, path);
			break;
		}

		dev_extent = btrfs_item_ptr(l, slot, struct btrfs_dev_extent);
		length = btrfs_dev_extent_length(l, dev_extent);

		if (key.offset + length <= new_size) {
			btrfs_release_path(root, path);
			break;
		}

		chunk_tree = btrfs_dev_extent_chunk_tree(l, dev_extent);
		chunk_objectid = btrfs_dev_extent_chunk_objectid(l, dev_extent);
		chunk_offset = btrfs_dev_extent_chunk_offset(l, dev_extent);
		btrfs_release_path(root, path);

		ret = btrfs_relocate_chunk(root, chunk_tree, chunk_objectid,
					   chunk_offset);
		if (ret && ret != -ENOSPC)
			goto done;
		if (ret == -ENOSPC)
			failed++;
		key.offset -= 1;
	}

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
		unlock_chunks(root);
		goto done;
	}

	/* Shrinking succeeded, else we would be at "done". */
	trans = btrfs_start_transaction(root, 0);
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

static int btrfs_add_system_chunk(struct btrfs_trans_handle *trans,
			   struct btrfs_root *root,
			   struct btrfs_key *key,
			   struct btrfs_chunk *chunk, int item_size)
{
	struct btrfs_super_block *super_copy = &root->fs_info->super_copy;
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

static noinline u64 chunk_bytes_by_type(u64 type, u64 calc_size,
					int num_stripes, int sub_stripes)
{
	if (type & (BTRFS_BLOCK_GROUP_RAID1 | BTRFS_BLOCK_GROUP_DUP))
		return calc_size;
	else if (type & BTRFS_BLOCK_GROUP_RAID10)
		return calc_size * (num_stripes / sub_stripes);
	else
		return calc_size * num_stripes;
}

static int __btrfs_alloc_chunk(struct btrfs_trans_handle *trans,
			       struct btrfs_root *extent_root,
			       struct map_lookup **map_ret,
			       u64 *num_bytes, u64 *stripe_size,
			       u64 start, u64 type)
{
	struct btrfs_fs_info *info = extent_root->fs_info;
	struct btrfs_device *device = NULL;
	struct btrfs_fs_devices *fs_devices = info->fs_devices;
	struct list_head *cur;
	struct map_lookup *map = NULL;
	struct extent_map_tree *em_tree;
	struct extent_map *em;
	struct list_head private_devs;
	int min_stripe_size = 1 * 1024 * 1024;
	u64 calc_size = 1024 * 1024 * 1024;
	u64 max_chunk_size = calc_size;
	u64 min_free;
	u64 avail;
	u64 max_avail = 0;
	u64 dev_offset;
	int num_stripes = 1;
	int min_stripes = 1;
	int sub_stripes = 0;
	int looped = 0;
	int ret;
	int index;
	int stripe_len = 64 * 1024;

	if ((type & BTRFS_BLOCK_GROUP_RAID1) &&
	    (type & BTRFS_BLOCK_GROUP_DUP)) {
		WARN_ON(1);
		type &= ~BTRFS_BLOCK_GROUP_DUP;
	}
	if (list_empty(&fs_devices->alloc_list))
		return -ENOSPC;

	if (type & (BTRFS_BLOCK_GROUP_RAID0)) {
		num_stripes = fs_devices->rw_devices;
		min_stripes = 2;
	}
	if (type & (BTRFS_BLOCK_GROUP_DUP)) {
		num_stripes = 2;
		min_stripes = 2;
	}
	if (type & (BTRFS_BLOCK_GROUP_RAID1)) {
		if (fs_devices->rw_devices < 2)
			return -ENOSPC;
		num_stripes = 2;
		min_stripes = 2;
	}
	if (type & (BTRFS_BLOCK_GROUP_RAID10)) {
		num_stripes = fs_devices->rw_devices;
		if (num_stripes < 4)
			return -ENOSPC;
		num_stripes &= ~(u32)1;
		sub_stripes = 2;
		min_stripes = 4;
	}

	if (type & BTRFS_BLOCK_GROUP_DATA) {
		max_chunk_size = 10 * calc_size;
		min_stripe_size = 64 * 1024 * 1024;
	} else if (type & BTRFS_BLOCK_GROUP_METADATA) {
		max_chunk_size = 256 * 1024 * 1024;
		min_stripe_size = 32 * 1024 * 1024;
	} else if (type & BTRFS_BLOCK_GROUP_SYSTEM) {
		calc_size = 8 * 1024 * 1024;
		max_chunk_size = calc_size * 2;
		min_stripe_size = 1 * 1024 * 1024;
	}

	/* we don't want a chunk larger than 10% of writeable space */
	max_chunk_size = min(div_factor(fs_devices->total_rw_bytes, 1),
			     max_chunk_size);

again:
	max_avail = 0;
	if (!map || map->num_stripes != num_stripes) {
		kfree(map);
		map = kmalloc(map_lookup_size(num_stripes), GFP_NOFS);
		if (!map)
			return -ENOMEM;
		map->num_stripes = num_stripes;
	}

	if (calc_size * num_stripes > max_chunk_size) {
		calc_size = max_chunk_size;
		do_div(calc_size, num_stripes);
		do_div(calc_size, stripe_len);
		calc_size *= stripe_len;
	}

	/* we don't want tiny stripes */
	if (!looped)
		calc_size = max_t(u64, min_stripe_size, calc_size);

	/*
	 * we're about to do_div by the stripe_len so lets make sure
	 * we end up with something bigger than a stripe
	 */
	calc_size = max_t(u64, calc_size, stripe_len * 4);

	do_div(calc_size, stripe_len);
	calc_size *= stripe_len;

	cur = fs_devices->alloc_list.next;
	index = 0;

	if (type & BTRFS_BLOCK_GROUP_DUP)
		min_free = calc_size * 2;
	else
		min_free = calc_size;

	/*
	 * we add 1MB because we never use the first 1MB of the device, unless
	 * we've looped, then we are likely allocating the maximum amount of
	 * space left already
	 */
	if (!looped)
		min_free += 1024 * 1024;

	INIT_LIST_HEAD(&private_devs);
	while (index < num_stripes) {
		device = list_entry(cur, struct btrfs_device, dev_alloc_list);
		BUG_ON(!device->writeable);
		if (device->total_bytes > device->bytes_used)
			avail = device->total_bytes - device->bytes_used;
		else
			avail = 0;
		cur = cur->next;

		if (device->in_fs_metadata && avail >= min_free) {
			ret = find_free_dev_extent(trans, device,
						   min_free, &dev_offset,
						   &max_avail);
			if (ret == 0) {
				list_move_tail(&device->dev_alloc_list,
					       &private_devs);
				map->stripes[index].dev = device;
				map->stripes[index].physical = dev_offset;
				index++;
				if (type & BTRFS_BLOCK_GROUP_DUP) {
					map->stripes[index].dev = device;
					map->stripes[index].physical =
						dev_offset + calc_size;
					index++;
				}
			}
		} else if (device->in_fs_metadata && avail > max_avail)
			max_avail = avail;
		if (cur == &fs_devices->alloc_list)
			break;
	}
	list_splice(&private_devs, &fs_devices->alloc_list);
	if (index < num_stripes) {
		if (index >= min_stripes) {
			num_stripes = index;
			if (type & (BTRFS_BLOCK_GROUP_RAID10)) {
				num_stripes /= sub_stripes;
				num_stripes *= sub_stripes;
			}
			looped = 1;
			goto again;
		}
		if (!looped && max_avail > 0) {
			looped = 1;
			calc_size = max_avail;
			goto again;
		}
		kfree(map);
		return -ENOSPC;
	}
	map->sector_size = extent_root->sectorsize;
	map->stripe_len = stripe_len;
	map->io_align = stripe_len;
	map->io_width = stripe_len;
	map->type = type;
	map->num_stripes = num_stripes;
	map->sub_stripes = sub_stripes;

	*map_ret = map;
	*stripe_size = calc_size;
	*num_bytes = chunk_bytes_by_type(type, calc_size,
					 num_stripes, sub_stripes);

	em = alloc_extent_map(GFP_NOFS);
	if (!em) {
		kfree(map);
		return -ENOMEM;
	}
	em->bdev = (struct block_device *)map;
	em->start = start;
	em->len = *num_bytes;
	em->block_start = 0;
	em->block_len = em->len;

	em_tree = &extent_root->fs_info->mapping_tree.map_tree;
	write_lock(&em_tree->lock);
	ret = add_extent_mapping(em_tree, em);
	write_unlock(&em_tree->lock);
	BUG_ON(ret);
	free_extent_map(em);

	ret = btrfs_make_block_group(trans, extent_root, 0, type,
				     BTRFS_FIRST_CHUNK_TREE_OBJECTID,
				     start, *num_bytes);
	BUG_ON(ret);

	index = 0;
	while (index < map->num_stripes) {
		device = map->stripes[index].dev;
		dev_offset = map->stripes[index].physical;

		ret = btrfs_alloc_dev_extent(trans, device,
				info->chunk_root->root_key.objectid,
				BTRFS_FIRST_CHUNK_TREE_OBJECTID,
				start, dev_offset, calc_size);
		BUG_ON(ret);
		index++;
	}

	return 0;
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
		BUG_ON(ret);
		index++;
	}

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
	BUG_ON(ret);

	if (map->type & BTRFS_BLOCK_GROUP_SYSTEM) {
		ret = btrfs_add_system_chunk(trans, chunk_root, &key, chunk,
					     item_size);
		BUG_ON(ret);
	}
	kfree(chunk);
	return 0;
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
	BUG_ON(ret);
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
	BUG_ON(ret);

	alloc_profile = BTRFS_BLOCK_GROUP_METADATA |
			(fs_info->metadata_alloc_profile &
			 fs_info->avail_metadata_alloc_bits);
	alloc_profile = btrfs_reduce_alloc_profile(root, alloc_profile);

	ret = __btrfs_alloc_chunk(trans, extent_root, &map, &chunk_size,
				  &stripe_size, chunk_offset, alloc_profile);
	BUG_ON(ret);

	sys_chunk_offset = chunk_offset + chunk_size;

	alloc_profile = BTRFS_BLOCK_GROUP_SYSTEM |
			(fs_info->system_alloc_profile &
			 fs_info->avail_system_alloc_bits);
	alloc_profile = btrfs_reduce_alloc_profile(root, alloc_profile);

	ret = __btrfs_alloc_chunk(trans, extent_root, &sys_map,
				  &sys_chunk_size, &sys_stripe_size,
				  sys_chunk_offset, alloc_profile);
	BUG_ON(ret);

	ret = btrfs_add_device(trans, fs_info->chunk_root, device);
	BUG_ON(ret);

	/*
	 * Modifying chunk tree needs allocating new blocks from both
	 * system block group and metadata block group. So we only can
	 * do operations require modifying the chunk tree after both
	 * block groups were created.
	 */
	ret = __finish_chunk_alloc(trans, extent_root, map, chunk_offset,
				   chunk_size, stripe_size);
	BUG_ON(ret);

	ret = __finish_chunk_alloc(trans, extent_root, sys_map,
				   sys_chunk_offset, sys_chunk_size,
				   sys_stripe_size);
	BUG_ON(ret);
	return 0;
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
	extent_map_tree_init(&tree->map_tree, GFP_NOFS);
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
			     struct btrfs_multi_bio **multi_ret,
			     int mirror_num, struct page *unplug_page)
{
	struct extent_map *em;
	struct map_lookup *map;
	struct extent_map_tree *em_tree = &map_tree->map_tree;
	u64 offset;
	u64 stripe_offset;
	u64 stripe_nr;
	int stripes_allocated = 8;
	int stripes_required = 1;
	int stripe_index;
	int i;
	int num_stripes;
	int max_errors = 0;
	struct btrfs_multi_bio *multi = NULL;

	if (multi_ret && !(rw & REQ_WRITE))
		stripes_allocated = 1;
again:
	if (multi_ret) {
		multi = kzalloc(btrfs_multi_bio_size(stripes_allocated),
				GFP_NOFS);
		if (!multi)
			return -ENOMEM;

		atomic_set(&multi->error, 0);
	}

	read_lock(&em_tree->lock);
	em = lookup_extent_mapping(em_tree, logical, *length);
	read_unlock(&em_tree->lock);

	if (!em && unplug_page) {
		kfree(multi);
		return 0;
	}

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

	/* if our multi bio struct is too small, back off and try again */
	if (rw & REQ_WRITE) {
		if (map->type & (BTRFS_BLOCK_GROUP_RAID1 |
				 BTRFS_BLOCK_GROUP_DUP)) {
			stripes_required = map->num_stripes;
			max_errors = 1;
		} else if (map->type & BTRFS_BLOCK_GROUP_RAID10) {
			stripes_required = map->sub_stripes;
			max_errors = 1;
		}
	}
	if (multi_ret && (rw & REQ_WRITE) &&
	    stripes_allocated < stripes_required) {
		stripes_allocated = map->num_stripes;
		free_extent_map(em);
		kfree(multi);
		goto again;
	}
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

	if (map->type & (BTRFS_BLOCK_GROUP_RAID0 | BTRFS_BLOCK_GROUP_RAID1 |
			 BTRFS_BLOCK_GROUP_RAID10 |
			 BTRFS_BLOCK_GROUP_DUP)) {
		/* we limit the length of each bio to what fits in a stripe */
		*length = min_t(u64, em->len - offset,
			      map->stripe_len - stripe_offset);
	} else {
		*length = em->len - offset;
	}

	if (!multi_ret && !unplug_page)
		goto out;

	num_stripes = 1;
	stripe_index = 0;
	if (map->type & BTRFS_BLOCK_GROUP_RAID1) {
		if (unplug_page || (rw & REQ_WRITE))
			num_stripes = map->num_stripes;
		else if (mirror_num)
			stripe_index = mirror_num - 1;
		else {
			stripe_index = find_live_mirror(map, 0,
					    map->num_stripes,
					    current->pid % map->num_stripes);
		}

	} else if (map->type & BTRFS_BLOCK_GROUP_DUP) {
		if (rw & REQ_WRITE)
			num_stripes = map->num_stripes;
		else if (mirror_num)
			stripe_index = mirror_num - 1;

	} else if (map->type & BTRFS_BLOCK_GROUP_RAID10) {
		int factor = map->num_stripes / map->sub_stripes;

		stripe_index = do_div(stripe_nr, factor);
		stripe_index *= map->sub_stripes;

		if (unplug_page || (rw & REQ_WRITE))
			num_stripes = map->sub_stripes;
		else if (mirror_num)
			stripe_index += mirror_num - 1;
		else {
			stripe_index = find_live_mirror(map, stripe_index,
					      map->sub_stripes, stripe_index +
					      current->pid % map->sub_stripes);
		}
	} else {
		/*
		 * after this do_div call, stripe_nr is the number of stripes
		 * on this device we have to walk to find the data, and
		 * stripe_index is the number of our device in the stripe array
		 */
		stripe_index = do_div(stripe_nr, map->num_stripes);
	}
	BUG_ON(stripe_index >= map->num_stripes);

	for (i = 0; i < num_stripes; i++) {
		if (unplug_page) {
			struct btrfs_device *device;
			struct backing_dev_info *bdi;

			device = map->stripes[stripe_index].dev;
			if (device->bdev) {
				bdi = blk_get_backing_dev_info(device->bdev);
				if (bdi->unplug_io_fn)
					bdi->unplug_io_fn(bdi, unplug_page);
			}
		} else {
			multi->stripes[i].physical =
				map->stripes[stripe_index].physical +
				stripe_offset + stripe_nr * map->stripe_len;
			multi->stripes[i].dev = map->stripes[stripe_index].dev;
		}
		stripe_index++;
	}
	if (multi_ret) {
		*multi_ret = multi;
		multi->num_stripes = num_stripes;
		multi->max_errors = max_errors;
	}
out:
	free_extent_map(em);
	return 0;
}

int btrfs_map_block(struct btrfs_mapping_tree *map_tree, int rw,
		      u64 logical, u64 *length,
		      struct btrfs_multi_bio **multi_ret, int mirror_num)
{
	return __btrfs_map_block(map_tree, rw, logical, length, multi_ret,
				 mirror_num, NULL);
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
	BUG_ON(!buf);

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

int btrfs_unplug_page(struct btrfs_mapping_tree *map_tree,
		      u64 logical, struct page *page)
{
	u64 length = PAGE_CACHE_SIZE;
	return __btrfs_map_block(map_tree, READ, logical, &length,
				 NULL, 0, page);
}

static void end_bio_multi_stripe(struct bio *bio, int err)
{
	struct btrfs_multi_bio *multi = bio->bi_private;
	int is_orig_bio = 0;

	if (err)
		atomic_inc(&multi->error);

	if (bio == multi->orig_bio)
		is_orig_bio = 1;

	if (atomic_dec_and_test(&multi->stripes_pending)) {
		if (!is_orig_bio) {
			bio_put(bio);
			bio = multi->orig_bio;
		}
		bio->bi_private = multi->private;
		bio->bi_end_io = multi->end_io;
		/* only send an error to the higher layers if it is
		 * beyond the tolerance of the multi-bio
		 */
		if (atomic_read(&multi->error) > multi->max_errors) {
			err = -EIO;
		} else if (err) {
			/*
			 * this bio is actually up to date, we didn't
			 * go over the max number of errors
			 */
			set_bit(BIO_UPTODATE, &bio->bi_flags);
			err = 0;
		}
		kfree(multi);

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
static noinline int schedule_bio(struct btrfs_root *root,
				 struct btrfs_device *device,
				 int rw, struct bio *bio)
{
	int should_queue = 1;
	struct btrfs_pending_bios *pending_bios;

	/* don't bother with additional async steps for reads, right now */
	if (!(rw & REQ_WRITE)) {
		bio_get(bio);
		submit_bio(rw, bio);
		bio_put(bio);
		return 0;
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
	return 0;
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
	struct btrfs_multi_bio *multi = NULL;
	int ret;
	int dev_nr = 0;
	int total_devs = 1;

	length = bio->bi_size;
	map_tree = &root->fs_info->mapping_tree;
	map_length = length;

	ret = btrfs_map_block(map_tree, rw, logical, &map_length, &multi,
			      mirror_num);
	BUG_ON(ret);

	total_devs = multi->num_stripes;
	if (map_length < length) {
		printk(KERN_CRIT "mapping failed logical %llu bio len %llu "
		       "len %llu\n", (unsigned long long)logical,
		       (unsigned long long)length,
		       (unsigned long long)map_length);
		BUG();
	}
	multi->end_io = first_bio->bi_end_io;
	multi->private = first_bio->bi_private;
	multi->orig_bio = first_bio;
	atomic_set(&multi->stripes_pending, multi->num_stripes);

	while (dev_nr < total_devs) {
		if (total_devs > 1) {
			if (dev_nr < total_devs - 1) {
				bio = bio_clone(first_bio, GFP_NOFS);
				BUG_ON(!bio);
			} else {
				bio = first_bio;
			}
			bio->bi_private = multi;
			bio->bi_end_io = end_bio_multi_stripe;
		}
		bio->bi_sector = multi->stripes[dev_nr].physical >> 9;
		dev = multi->stripes[dev_nr].dev;
		if (dev && dev->bdev && (rw != WRITE || dev->writeable)) {
			bio->bi_bdev = dev->bdev;
			if (async_submit)
				schedule_bio(root, dev, rw, bio);
			else
				submit_bio(rw, bio);
		} else {
			bio->bi_bdev = root->fs_info->fs_devices->latest_bdev;
			bio->bi_sector = logical >> 9;
			bio_endio(bio, -EIO);
		}
		dev_nr++;
	}
	if (total_devs == 1)
		kfree(multi);
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

	em = alloc_extent_map(GFP_NOFS);
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
	BUG_ON(ret);
	free_extent_map(em);

	return 0;
}

static int fill_device_from_item(struct extent_buffer *leaf,
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

	return 0;
}

static int open_seed_devices(struct btrfs_root *root, u8 *fsid)
{
	struct btrfs_fs_devices *fs_devices;
	int ret;

	mutex_lock(&uuid_mutex);

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
	if (ret)
		goto out;

	if (!fs_devices->seeding) {
		__btrfs_close_devices(fs_devices);
		free_fs_devices(fs_devices);
		ret = -EINVAL;
		goto out;
	}

	fs_devices->seed = root->fs_info->fs_devices->seed;
	root->fs_info->fs_devices->seed = fs_devices;
out:
	mutex_unlock(&uuid_mutex);
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
	if (device->writeable)
		device->fs_devices->total_rw_bytes += device->total_bytes;
	ret = 0;
	return ret;
}

int btrfs_read_super_device(struct btrfs_root *root, struct extent_buffer *buf)
{
	struct btrfs_dev_item *dev_item;

	dev_item = (struct btrfs_dev_item *)offsetof(struct btrfs_super_block,
						     dev_item);
	return read_one_dev(root, buf, dev_item);
}

int btrfs_read_sys_array(struct btrfs_root *root)
{
	struct btrfs_super_block *super_copy = &root->fs_info->super_copy;
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
	btrfs_set_buffer_lockdep_class(sb, 0);

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
		btrfs_release_path(root, path);
		goto again;
	}
	ret = 0;
error:
	btrfs_free_path(path);
	return ret;
}
