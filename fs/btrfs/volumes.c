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
#include <linux/buffer_head.h>
#include <linux/blkdev.h>
#include <linux/random.h>
#include <asm/div64.h>
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

int btrfs_cleanup_fs_uuids(void)
{
	struct btrfs_fs_devices *fs_devices;
	struct list_head *uuid_cur;
	struct list_head *devices_cur;
	struct btrfs_device *dev;

	list_for_each(uuid_cur, &fs_uuids) {
		fs_devices = list_entry(uuid_cur, struct btrfs_fs_devices,
					list);
		while(!list_empty(&fs_devices->devices)) {
			devices_cur = fs_devices->devices.next;
			dev = list_entry(devices_cur, struct btrfs_device,
					 dev_list);
			if (dev->bdev) {
				close_bdev_excl(dev->bdev);
				fs_devices->open_devices--;
			}
			list_del(&dev->dev_list);
			kfree(dev->name);
			kfree(dev);
		}
	}
	return 0;
}

static struct btrfs_device *__find_device(struct list_head *head, u64 devid,
					  u8 *uuid)
{
	struct btrfs_device *dev;
	struct list_head *cur;

	list_for_each(cur, head) {
		dev = list_entry(cur, struct btrfs_device, dev_list);
		if (dev->devid == devid &&
		    (!uuid || !memcmp(dev->uuid, uuid, BTRFS_UUID_SIZE))) {
			return dev;
		}
	}
	return NULL;
}

static struct btrfs_fs_devices *find_fsid(u8 *fsid)
{
	struct list_head *cur;
	struct btrfs_fs_devices *fs_devices;

	list_for_each(cur, &fs_uuids) {
		fs_devices = list_entry(cur, struct btrfs_fs_devices, list);
		if (memcmp(fsid, fs_devices->fsid, BTRFS_FSID_SIZE) == 0)
			return fs_devices;
	}
	return NULL;
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
int run_scheduled_bios(struct btrfs_device *device)
{
	struct bio *pending;
	struct backing_dev_info *bdi;
	struct bio *tail;
	struct bio *cur;
	int again = 0;
	unsigned long num_run = 0;

	bdi = device->bdev->bd_inode->i_mapping->backing_dev_info;
loop:
	spin_lock(&device->io_lock);

	/* take all the bios off the list at once and process them
	 * later on (without the lock held).  But, remember the
	 * tail and other pointers so the bios can be properly reinserted
	 * into the list if we hit congestion
	 */
	pending = device->pending_bios;
	tail = device->pending_bio_tail;
	WARN_ON(pending && !tail);
	device->pending_bios = NULL;
	device->pending_bio_tail = NULL;

	/*
	 * if pending was null this time around, no bios need processing
	 * at all and we can stop.  Otherwise it'll loop back up again
	 * and do an additional check so no bios are missed.
	 *
	 * device->running_pending is used to synchronize with the
	 * schedule_bio code.
	 */
	if (pending) {
		again = 1;
		device->running_pending = 1;
	} else {
		again = 0;
		device->running_pending = 0;
	}
	spin_unlock(&device->io_lock);

	while(pending) {
		cur = pending;
		pending = pending->bi_next;
		cur->bi_next = NULL;
		atomic_dec(&device->dev_root->fs_info->nr_async_submits);
		submit_bio(cur->bi_rw, cur);
		num_run++;

		/*
		 * we made progress, there is more work to do and the bdi
		 * is now congested.  Back off and let other work structs
		 * run instead
		 */
		if (pending && num_run && bdi_write_congested(bdi)) {
			struct bio *old_head;

			spin_lock(&device->io_lock);
			old_head = device->pending_bios;
			device->pending_bios = pending;
			if (device->pending_bio_tail)
				tail->bi_next = old_head;
			else
				device->pending_bio_tail = tail;

			spin_unlock(&device->io_lock);
			btrfs_requeue_work(&device->work);
			goto done;
		}
	}
	if (again)
		goto loop;
done:
	return 0;
}

void pending_bios_fn(struct btrfs_work *work)
{
	struct btrfs_device *device;

	device = container_of(work, struct btrfs_device, work);
	run_scheduled_bios(device);
}

static int device_list_add(const char *path,
			   struct btrfs_super_block *disk_super,
			   u64 devid, struct btrfs_fs_devices **fs_devices_ret)
{
	struct btrfs_device *device;
	struct btrfs_fs_devices *fs_devices;
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
		device = NULL;
	} else {
		device = __find_device(&fs_devices->devices, devid,
				       disk_super->dev_item.uuid);
	}
	if (!device) {
		device = kzalloc(sizeof(*device), GFP_NOFS);
		if (!device) {
			/* we can safely leave the fs_devices entry around */
			return -ENOMEM;
		}
		device->devid = devid;
		device->work.func = pending_bios_fn;
		memcpy(device->uuid, disk_super->dev_item.uuid,
		       BTRFS_UUID_SIZE);
		device->barriers = 1;
		spin_lock_init(&device->io_lock);
		device->name = kstrdup(path, GFP_NOFS);
		if (!device->name) {
			kfree(device);
			return -ENOMEM;
		}
		list_add(&device->dev_list, &fs_devices->devices);
		list_add(&device->dev_alloc_list, &fs_devices->alloc_list);
		fs_devices->num_devices++;
	}

	if (found_transid > fs_devices->latest_trans) {
		fs_devices->latest_devid = devid;
		fs_devices->latest_trans = found_transid;
	}
	*fs_devices_ret = fs_devices;
	return 0;
}

int btrfs_close_extra_devices(struct btrfs_fs_devices *fs_devices)
{
	struct list_head *head = &fs_devices->devices;
	struct list_head *cur;
	struct btrfs_device *device;

	mutex_lock(&uuid_mutex);
again:
	list_for_each(cur, head) {
		device = list_entry(cur, struct btrfs_device, dev_list);
		if (!device->in_fs_metadata) {
			if (device->bdev) {
				close_bdev_excl(device->bdev);
				fs_devices->open_devices--;
			}
			list_del(&device->dev_list);
			list_del(&device->dev_alloc_list);
			fs_devices->num_devices--;
			kfree(device->name);
			kfree(device);
			goto again;
		}
	}
	mutex_unlock(&uuid_mutex);
	return 0;
}

int btrfs_close_devices(struct btrfs_fs_devices *fs_devices)
{
	struct list_head *head = &fs_devices->devices;
	struct list_head *cur;
	struct btrfs_device *device;

	mutex_lock(&uuid_mutex);
	list_for_each(cur, head) {
		device = list_entry(cur, struct btrfs_device, dev_list);
		if (device->bdev) {
			close_bdev_excl(device->bdev);
			fs_devices->open_devices--;
		}
		device->bdev = NULL;
		device->in_fs_metadata = 0;
	}
	fs_devices->mounted = 0;
	mutex_unlock(&uuid_mutex);
	return 0;
}

int btrfs_open_devices(struct btrfs_fs_devices *fs_devices,
		       int flags, void *holder)
{
	struct block_device *bdev;
	struct list_head *head = &fs_devices->devices;
	struct list_head *cur;
	struct btrfs_device *device;
	struct block_device *latest_bdev = NULL;
	struct buffer_head *bh;
	struct btrfs_super_block *disk_super;
	u64 latest_devid = 0;
	u64 latest_transid = 0;
	u64 transid;
	u64 devid;
	int ret = 0;

	mutex_lock(&uuid_mutex);
	if (fs_devices->mounted)
		goto out;

	list_for_each(cur, head) {
		device = list_entry(cur, struct btrfs_device, dev_list);
		if (device->bdev)
			continue;

		if (!device->name)
			continue;

		bdev = open_bdev_excl(device->name, flags, holder);

		if (IS_ERR(bdev)) {
			printk("open %s failed\n", device->name);
			goto error;
		}
		set_blocksize(bdev, 4096);

		bh = __bread(bdev, BTRFS_SUPER_INFO_OFFSET / 4096, 4096);
		if (!bh)
			goto error_close;

		disk_super = (struct btrfs_super_block *)bh->b_data;
		if (strncmp((char *)(&disk_super->magic), BTRFS_MAGIC,
		    sizeof(disk_super->magic)))
			goto error_brelse;

		devid = le64_to_cpu(disk_super->dev_item.devid);
		if (devid != device->devid)
			goto error_brelse;

		transid = btrfs_super_generation(disk_super);
		if (!latest_transid || transid > latest_transid) {
			latest_devid = devid;
			latest_transid = transid;
			latest_bdev = bdev;
		}

		device->bdev = bdev;
		device->in_fs_metadata = 0;
		fs_devices->open_devices++;
		continue;

error_brelse:
		brelse(bh);
error_close:
		close_bdev_excl(bdev);
error:
		continue;
	}
	if (fs_devices->open_devices == 0) {
		ret = -EIO;
		goto out;
	}
	fs_devices->mounted = 1;
	fs_devices->latest_bdev = latest_bdev;
	fs_devices->latest_devid = latest_devid;
	fs_devices->latest_trans = latest_transid;
out:
	mutex_unlock(&uuid_mutex);
	return ret;
}

int btrfs_scan_one_device(const char *path, int flags, void *holder,
			  struct btrfs_fs_devices **fs_devices_ret)
{
	struct btrfs_super_block *disk_super;
	struct block_device *bdev;
	struct buffer_head *bh;
	int ret;
	u64 devid;
	u64 transid;

	mutex_lock(&uuid_mutex);

	bdev = open_bdev_excl(path, flags, holder);

	if (IS_ERR(bdev)) {
		ret = PTR_ERR(bdev);
		goto error;
	}

	ret = set_blocksize(bdev, 4096);
	if (ret)
		goto error_close;
	bh = __bread(bdev, BTRFS_SUPER_INFO_OFFSET / 4096, 4096);
	if (!bh) {
		ret = -EIO;
		goto error_close;
	}
	disk_super = (struct btrfs_super_block *)bh->b_data;
	if (strncmp((char *)(&disk_super->magic), BTRFS_MAGIC,
	    sizeof(disk_super->magic))) {
		ret = -EINVAL;
		goto error_brelse;
	}
	devid = le64_to_cpu(disk_super->dev_item.devid);
	transid = btrfs_super_generation(disk_super);
	if (disk_super->label[0])
		printk("device label %s ", disk_super->label);
	else {
		/* FIXME, make a readl uuid parser */
		printk("device fsid %llx-%llx ",
		       *(unsigned long long *)disk_super->fsid,
		       *(unsigned long long *)(disk_super->fsid + 8));
	}
	printk("devid %Lu transid %Lu %s\n", devid, transid, path);
	ret = device_list_add(path, disk_super, devid, fs_devices_ret);

error_brelse:
	brelse(bh);
error_close:
	close_bdev_excl(bdev);
error:
	mutex_unlock(&uuid_mutex);
	return ret;
}

/*
 * this uses a pretty simple search, the expectation is that it is
 * called very infrequently and that a given device has a small number
 * of extents
 */
static int find_free_dev_extent(struct btrfs_trans_handle *trans,
				struct btrfs_device *device,
				struct btrfs_path *path,
				u64 num_bytes, u64 *start)
{
	struct btrfs_key key;
	struct btrfs_root *root = device->dev_root;
	struct btrfs_dev_extent *dev_extent = NULL;
	u64 hole_size = 0;
	u64 last_byte = 0;
	u64 search_start = 0;
	u64 search_end = device->total_bytes;
	int ret;
	int slot = 0;
	int start_found;
	struct extent_buffer *l;

	start_found = 0;
	path->reada = 2;

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
	ret = btrfs_previous_item(root, path, 0, key.type);
	if (ret < 0)
		goto error;
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
			if (key.offset > last_byte &&
			    hole_size >= num_bytes) {
				*start = last_byte;
				goto check_pending;
			}
		}
		if (btrfs_key_type(&key) != BTRFS_DEV_EXTENT_KEY) {
			goto next;
		}

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
	btrfs_release_path(root, path);
	BUG_ON(*start < search_start);

	if (*start + num_bytes > search_end) {
		ret = -ENOSPC;
		goto error;
	}
	/* check for pending inserts here */
	return 0;

error:
	btrfs_release_path(root, path);
	return ret;
}

int btrfs_free_dev_extent(struct btrfs_trans_handle *trans,
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
			   u64 chunk_offset,
			   u64 num_bytes, u64 *start)
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

	ret = find_free_dev_extent(trans, device, path, num_bytes, start);
	if (ret) {
		goto err;
	}

	key.objectid = device->devid;
	key.offset = *start;
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
err:
	btrfs_free_path(path);
	return ret;
}

static int find_next_chunk(struct btrfs_root *root, u64 objectid, u64 *offset)
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

static int find_next_devid(struct btrfs_root *root, struct btrfs_path *path,
			   u64 *objectid)
{
	int ret;
	struct btrfs_key key;
	struct btrfs_key found_key;

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
	btrfs_release_path(root, path);
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
	u64 free_devid = 0;

	root = root->fs_info->chunk_root;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	ret = find_next_devid(root, path, &free_devid);
	if (ret)
		goto out;

	key.objectid = BTRFS_DEV_ITEMS_OBJECTID;
	key.type = BTRFS_DEV_ITEM_KEY;
	key.offset = free_devid;

	ret = btrfs_insert_empty_item(trans, root, path, &key,
				      sizeof(*dev_item));
	if (ret)
		goto out;

	leaf = path->nodes[0];
	dev_item = btrfs_item_ptr(leaf, path->slots[0], struct btrfs_dev_item);

	device->devid = free_devid;
	btrfs_set_device_id(leaf, dev_item, device->devid);
	btrfs_set_device_type(leaf, dev_item, device->type);
	btrfs_set_device_io_align(leaf, dev_item, device->io_align);
	btrfs_set_device_io_width(leaf, dev_item, device->io_width);
	btrfs_set_device_sector_size(leaf, dev_item, device->sector_size);
	btrfs_set_device_total_bytes(leaf, dev_item, device->total_bytes);
	btrfs_set_device_bytes_used(leaf, dev_item, device->bytes_used);
	btrfs_set_device_group(leaf, dev_item, 0);
	btrfs_set_device_seek_speed(leaf, dev_item, 0);
	btrfs_set_device_bandwidth(leaf, dev_item, 0);

	ptr = (unsigned long)btrfs_device_uuid(dev_item);
	write_extent_buffer(leaf, device->uuid, ptr, BTRFS_UUID_SIZE);
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
	struct block_device *bdev = device->bdev;
	struct btrfs_device *next_dev;
	struct btrfs_key key;
	u64 total_bytes;
	struct btrfs_fs_devices *fs_devices;
	struct btrfs_trans_handle *trans;

	root = root->fs_info->chunk_root;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	trans = btrfs_start_transaction(root, 1);
	key.objectid = BTRFS_DEV_ITEMS_OBJECTID;
	key.type = BTRFS_DEV_ITEM_KEY;
	key.offset = device->devid;

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

	/*
	 * at this point, the device is zero sized.  We want to
	 * remove it from the devices list and zero out the old super
	 */
	list_del_init(&device->dev_list);
	list_del_init(&device->dev_alloc_list);
	fs_devices = root->fs_info->fs_devices;

	next_dev = list_entry(fs_devices->devices.next, struct btrfs_device,
			      dev_list);
	if (bdev == root->fs_info->sb->s_bdev)
		root->fs_info->sb->s_bdev = next_dev->bdev;
	if (bdev == fs_devices->latest_bdev)
		fs_devices->latest_bdev = next_dev->bdev;

	total_bytes = btrfs_super_num_devices(&root->fs_info->super_copy);
	btrfs_set_super_num_devices(&root->fs_info->super_copy,
				    total_bytes - 1);
out:
	btrfs_free_path(path);
	btrfs_commit_transaction(trans, root);
	return ret;
}

int btrfs_rm_device(struct btrfs_root *root, char *device_path)
{
	struct btrfs_device *device;
	struct block_device *bdev;
	struct buffer_head *bh = NULL;
	struct btrfs_super_block *disk_super;
	u64 all_avail;
	u64 devid;
	int ret = 0;

	mutex_lock(&root->fs_info->fs_mutex);
	mutex_lock(&uuid_mutex);

	all_avail = root->fs_info->avail_data_alloc_bits |
		root->fs_info->avail_system_alloc_bits |
		root->fs_info->avail_metadata_alloc_bits;

	if ((all_avail & BTRFS_BLOCK_GROUP_RAID10) &&
	    btrfs_super_num_devices(&root->fs_info->super_copy) <= 4) {
		printk("btrfs: unable to go below four devices on raid10\n");
		ret = -EINVAL;
		goto out;
	}

	if ((all_avail & BTRFS_BLOCK_GROUP_RAID1) &&
	    btrfs_super_num_devices(&root->fs_info->super_copy) <= 2) {
		printk("btrfs: unable to go below two devices on raid1\n");
		ret = -EINVAL;
		goto out;
	}

	if (strcmp(device_path, "missing") == 0) {
		struct list_head *cur;
		struct list_head *devices;
		struct btrfs_device *tmp;

		device = NULL;
		devices = &root->fs_info->fs_devices->devices;
		list_for_each(cur, devices) {
			tmp = list_entry(cur, struct btrfs_device, dev_list);
			if (tmp->in_fs_metadata && !tmp->bdev) {
				device = tmp;
				break;
			}
		}
		bdev = NULL;
		bh = NULL;
		disk_super = NULL;
		if (!device) {
			printk("btrfs: no missing devices found to remove\n");
			goto out;
		}

	} else {
		bdev = open_bdev_excl(device_path, 0,
				      root->fs_info->bdev_holder);
		if (IS_ERR(bdev)) {
			ret = PTR_ERR(bdev);
			goto out;
		}

		bh = __bread(bdev, BTRFS_SUPER_INFO_OFFSET / 4096, 4096);
		if (!bh) {
			ret = -EIO;
			goto error_close;
		}
		disk_super = (struct btrfs_super_block *)bh->b_data;
		if (strncmp((char *)(&disk_super->magic), BTRFS_MAGIC,
		    sizeof(disk_super->magic))) {
			ret = -ENOENT;
			goto error_brelse;
		}
		if (memcmp(disk_super->fsid, root->fs_info->fsid,
			   BTRFS_FSID_SIZE)) {
			ret = -ENOENT;
			goto error_brelse;
		}
		devid = le64_to_cpu(disk_super->dev_item.devid);
		device = btrfs_find_device(root, devid, NULL);
		if (!device) {
			ret = -ENOENT;
			goto error_brelse;
		}

	}
	root->fs_info->fs_devices->num_devices--;
	root->fs_info->fs_devices->open_devices--;

	ret = btrfs_shrink_device(device, 0);
	if (ret)
		goto error_brelse;


	ret = btrfs_rm_dev_item(root->fs_info->chunk_root, device);
	if (ret)
		goto error_brelse;

	if (bh) {
		/* make sure this device isn't detected as part of
		 * the FS anymore
		 */
		memset(&disk_super->magic, 0, sizeof(disk_super->magic));
		set_buffer_dirty(bh);
		sync_dirty_buffer(bh);

		brelse(bh);
	}

	if (device->bdev) {
		/* one close for the device struct or super_block */
		close_bdev_excl(device->bdev);
	}
	if (bdev) {
		/* one close for us */
		close_bdev_excl(bdev);
	}
	kfree(device->name);
	kfree(device);
	ret = 0;
	goto out;

error_brelse:
	brelse(bh);
error_close:
	if (bdev)
		close_bdev_excl(bdev);
out:
	mutex_unlock(&uuid_mutex);
	mutex_unlock(&root->fs_info->fs_mutex);
	return ret;
}

int btrfs_init_new_device(struct btrfs_root *root, char *device_path)
{
	struct btrfs_trans_handle *trans;
	struct btrfs_device *device;
	struct block_device *bdev;
	struct list_head *cur;
	struct list_head *devices;
	u64 total_bytes;
	int ret = 0;


	bdev = open_bdev_excl(device_path, 0, root->fs_info->bdev_holder);
	if (!bdev) {
		return -EIO;
	}
	mutex_lock(&root->fs_info->fs_mutex);
	trans = btrfs_start_transaction(root, 1);
	devices = &root->fs_info->fs_devices->devices;
	list_for_each(cur, devices) {
		device = list_entry(cur, struct btrfs_device, dev_list);
		if (device->bdev == bdev) {
			ret = -EEXIST;
			goto out;
		}
	}

	device = kzalloc(sizeof(*device), GFP_NOFS);
	if (!device) {
		/* we can safely leave the fs_devices entry around */
		ret = -ENOMEM;
		goto out_close_bdev;
	}

	device->barriers = 1;
	device->work.func = pending_bios_fn;
	generate_random_uuid(device->uuid);
	spin_lock_init(&device->io_lock);
	device->name = kstrdup(device_path, GFP_NOFS);
	if (!device->name) {
		kfree(device);
		goto out_close_bdev;
	}
	device->io_width = root->sectorsize;
	device->io_align = root->sectorsize;
	device->sector_size = root->sectorsize;
	device->total_bytes = i_size_read(bdev->bd_inode);
	device->dev_root = root->fs_info->dev_root;
	device->bdev = bdev;
	device->in_fs_metadata = 1;

	ret = btrfs_add_device(trans, root, device);
	if (ret)
		goto out_close_bdev;

	total_bytes = btrfs_super_total_bytes(&root->fs_info->super_copy);
	btrfs_set_super_total_bytes(&root->fs_info->super_copy,
				    total_bytes + device->total_bytes);

	total_bytes = btrfs_super_num_devices(&root->fs_info->super_copy);
	btrfs_set_super_num_devices(&root->fs_info->super_copy,
				    total_bytes + 1);

	list_add(&device->dev_list, &root->fs_info->fs_devices->devices);
	list_add(&device->dev_alloc_list,
		 &root->fs_info->fs_devices->alloc_list);
	root->fs_info->fs_devices->num_devices++;
	root->fs_info->fs_devices->open_devices++;
out:
	btrfs_end_transaction(trans, root);
	mutex_unlock(&root->fs_info->fs_mutex);
	return ret;

out_close_bdev:
	close_bdev_excl(bdev);
	goto out;
}

int btrfs_update_device(struct btrfs_trans_handle *trans,
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
	btrfs_set_device_total_bytes(leaf, dev_item, device->total_bytes);
	btrfs_set_device_bytes_used(leaf, dev_item, device->bytes_used);
	btrfs_mark_buffer_dirty(leaf);

out:
	btrfs_free_path(path);
	return ret;
}

int btrfs_grow_device(struct btrfs_trans_handle *trans,
		      struct btrfs_device *device, u64 new_size)
{
	struct btrfs_super_block *super_copy =
		&device->dev_root->fs_info->super_copy;
	u64 old_total = btrfs_super_total_bytes(super_copy);
	u64 diff = new_size - device->total_bytes;

	btrfs_set_super_total_bytes(super_copy, old_total + diff);
	return btrfs_update_device(trans, device);
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

int btrfs_del_sys_chunk(struct btrfs_root *root, u64 chunk_objectid, u64
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


int btrfs_relocate_chunk(struct btrfs_root *root,
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

	printk("btrfs relocating chunk %llu\n",
	       (unsigned long long)chunk_offset);
	root = root->fs_info->chunk_root;
	extent_root = root->fs_info->extent_root;
	em_tree = &root->fs_info->mapping_tree.map_tree;

	/* step one, relocate all the extents inside this chunk */
	ret = btrfs_shrink_extent_tree(extent_root, chunk_offset);
	BUG_ON(ret);

	trans = btrfs_start_transaction(root, 1);
	BUG_ON(!trans);

	/*
	 * step two, delete the device extents and the
	 * chunk tree entries
	 */
	spin_lock(&em_tree->lock);
	em = lookup_extent_mapping(em_tree, chunk_offset, 1);
	spin_unlock(&em_tree->lock);

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

	spin_lock(&em_tree->lock);
	remove_extent_mapping(em_tree, em);
	kfree(map);
	em->bdev = NULL;

	/* once for the tree */
	free_extent_map(em);
	spin_unlock(&em_tree->lock);

	/* once for us */
	free_extent_map(em);

	btrfs_end_transaction(trans, root);
	return 0;
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
	struct list_head *cur;
	struct list_head *devices = &dev_root->fs_info->fs_devices->devices;
	struct btrfs_device *device;
	u64 old_size;
	u64 size_to_free;
	struct btrfs_path *path;
	struct btrfs_key key;
	struct btrfs_chunk *chunk;
	struct btrfs_root *chunk_root = dev_root->fs_info->chunk_root;
	struct btrfs_trans_handle *trans;
	struct btrfs_key found_key;


	dev_root = dev_root->fs_info->dev_root;

	mutex_lock(&dev_root->fs_info->fs_mutex);
	/* step one make some room on all the devices */
	list_for_each(cur, devices) {
		device = list_entry(cur, struct btrfs_device, dev_list);
		old_size = device->total_bytes;
		size_to_free = div_factor(old_size, 1);
		size_to_free = min(size_to_free, (u64)1 * 1024 * 1024);
		if (device->total_bytes - device->bytes_used > size_to_free)
			continue;

		ret = btrfs_shrink_device(device, old_size - size_to_free);
		BUG_ON(ret);

		trans = btrfs_start_transaction(dev_root, 1);
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

	while(1) {
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
		if (ret) {
			break;
		}
		btrfs_item_key_to_cpu(path->nodes[0], &found_key,
				      path->slots[0]);
		if (found_key.objectid != key.objectid)
			break;
		chunk = btrfs_item_ptr(path->nodes[0],
				       path->slots[0],
				       struct btrfs_chunk);
		key.offset = found_key.offset;
		/* chunk zero is special */
		if (key.offset == 0)
			break;

		ret = btrfs_relocate_chunk(chunk_root,
					   chunk_root->root_key.objectid,
					   found_key.objectid,
					   found_key.offset);
		BUG_ON(ret);
		btrfs_release_path(chunk_root, path);
	}
	ret = 0;
error:
	btrfs_free_path(path);
	mutex_unlock(&dev_root->fs_info->fs_mutex);
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
	struct extent_buffer *l;
	struct btrfs_key key;
	struct btrfs_super_block *super_copy = &root->fs_info->super_copy;
	u64 old_total = btrfs_super_total_bytes(super_copy);
	u64 diff = device->total_bytes - new_size;


	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	trans = btrfs_start_transaction(root, 1);
	if (!trans) {
		ret = -ENOMEM;
		goto done;
	}

	path->reada = 2;

	device->total_bytes = new_size;
	ret = btrfs_update_device(trans, device);
	if (ret) {
		btrfs_end_transaction(trans, root);
		goto done;
	}
	WARN_ON(diff > old_total);
	btrfs_set_super_total_bytes(super_copy, old_total - diff);
	btrfs_end_transaction(trans, root);

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
			goto done;
		}

		l = path->nodes[0];
		slot = path->slots[0];
		btrfs_item_key_to_cpu(l, &key, path->slots[0]);

		if (key.objectid != device->devid)
			goto done;

		dev_extent = btrfs_item_ptr(l, slot, struct btrfs_dev_extent);
		length = btrfs_dev_extent_length(l, dev_extent);

		if (key.offset + length <= new_size)
			goto done;

		chunk_tree = btrfs_dev_extent_chunk_tree(l, dev_extent);
		chunk_objectid = btrfs_dev_extent_chunk_objectid(l, dev_extent);
		chunk_offset = btrfs_dev_extent_chunk_offset(l, dev_extent);
		btrfs_release_path(root, path);

		ret = btrfs_relocate_chunk(root, chunk_tree, chunk_objectid,
					   chunk_offset);
		if (ret)
			goto done;
	}

done:
	btrfs_free_path(path);
	return ret;
}

int btrfs_add_system_chunk(struct btrfs_trans_handle *trans,
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

static u64 chunk_bytes_by_type(u64 type, u64 calc_size, int num_stripes,
			       int sub_stripes)
{
	if (type & (BTRFS_BLOCK_GROUP_RAID1 | BTRFS_BLOCK_GROUP_DUP))
		return calc_size;
	else if (type & BTRFS_BLOCK_GROUP_RAID10)
		return calc_size * (num_stripes / sub_stripes);
	else
		return calc_size * num_stripes;
}


int btrfs_alloc_chunk(struct btrfs_trans_handle *trans,
		      struct btrfs_root *extent_root, u64 *start,
		      u64 *num_bytes, u64 type)
{
	u64 dev_offset;
	struct btrfs_fs_info *info = extent_root->fs_info;
	struct btrfs_root *chunk_root = extent_root->fs_info->chunk_root;
	struct btrfs_path *path;
	struct btrfs_stripe *stripes;
	struct btrfs_device *device = NULL;
	struct btrfs_chunk *chunk;
	struct list_head private_devs;
	struct list_head *dev_list;
	struct list_head *cur;
	struct extent_map_tree *em_tree;
	struct map_lookup *map;
	struct extent_map *em;
	int min_stripe_size = 1 * 1024 * 1024;
	u64 physical;
	u64 calc_size = 1024 * 1024 * 1024;
	u64 max_chunk_size = calc_size;
	u64 min_free;
	u64 avail;
	u64 max_avail = 0;
	u64 percent_max;
	int num_stripes = 1;
	int min_stripes = 1;
	int sub_stripes = 0;
	int looped = 0;
	int ret;
	int index;
	int stripe_len = 64 * 1024;
	struct btrfs_key key;

	if ((type & BTRFS_BLOCK_GROUP_RAID1) &&
	    (type & BTRFS_BLOCK_GROUP_DUP)) {
		WARN_ON(1);
		type &= ~BTRFS_BLOCK_GROUP_DUP;
	}
	dev_list = &extent_root->fs_info->fs_devices->alloc_list;
	if (list_empty(dev_list))
		return -ENOSPC;

	if (type & (BTRFS_BLOCK_GROUP_RAID0)) {
		num_stripes = extent_root->fs_info->fs_devices->open_devices;
		min_stripes = 2;
	}
	if (type & (BTRFS_BLOCK_GROUP_DUP)) {
		num_stripes = 2;
		min_stripes = 2;
	}
	if (type & (BTRFS_BLOCK_GROUP_RAID1)) {
		num_stripes = min_t(u64, 2,
			    extent_root->fs_info->fs_devices->open_devices);
		if (num_stripes < 2)
			return -ENOSPC;
		min_stripes = 2;
	}
	if (type & (BTRFS_BLOCK_GROUP_RAID10)) {
		num_stripes = extent_root->fs_info->fs_devices->open_devices;
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
		max_chunk_size = 4 * calc_size;
		min_stripe_size = 32 * 1024 * 1024;
	} else if (type & BTRFS_BLOCK_GROUP_SYSTEM) {
		calc_size = 8 * 1024 * 1024;
		max_chunk_size = calc_size * 2;
		min_stripe_size = 1 * 1024 * 1024;
	}

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	/* we don't want a chunk larger than 10% of the FS */
	percent_max = div_factor(btrfs_super_total_bytes(&info->super_copy), 1);
	max_chunk_size = min(percent_max, max_chunk_size);

again:
	if (calc_size * num_stripes > max_chunk_size) {
		calc_size = max_chunk_size;
		do_div(calc_size, num_stripes);
		do_div(calc_size, stripe_len);
		calc_size *= stripe_len;
	}
	/* we don't want tiny stripes */
	calc_size = max_t(u64, min_stripe_size, calc_size);

	do_div(calc_size, stripe_len);
	calc_size *= stripe_len;

	INIT_LIST_HEAD(&private_devs);
	cur = dev_list->next;
	index = 0;

	if (type & BTRFS_BLOCK_GROUP_DUP)
		min_free = calc_size * 2;
	else
		min_free = calc_size;

	/* we add 1MB because we never use the first 1MB of the device */
	min_free += 1024 * 1024;

	/* build a private list of devices we will allocate from */
	while(index < num_stripes) {
		device = list_entry(cur, struct btrfs_device, dev_alloc_list);

		if (device->total_bytes > device->bytes_used)
			avail = device->total_bytes - device->bytes_used;
		else
			avail = 0;
		cur = cur->next;

		if (device->in_fs_metadata && avail >= min_free) {
			u64 ignored_start = 0;
			ret = find_free_dev_extent(trans, device, path,
						   min_free,
						   &ignored_start);
			if (ret == 0) {
				list_move_tail(&device->dev_alloc_list,
					       &private_devs);
				index++;
				if (type & BTRFS_BLOCK_GROUP_DUP)
					index++;
			}
		} else if (device->in_fs_metadata && avail > max_avail)
			max_avail = avail;
		if (cur == dev_list)
			break;
	}
	if (index < num_stripes) {
		list_splice(&private_devs, dev_list);
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
		btrfs_free_path(path);
		return -ENOSPC;
	}
	key.objectid = BTRFS_FIRST_CHUNK_TREE_OBJECTID;
	key.type = BTRFS_CHUNK_ITEM_KEY;
	ret = find_next_chunk(chunk_root, BTRFS_FIRST_CHUNK_TREE_OBJECTID,
			      &key.offset);
	if (ret) {
		btrfs_free_path(path);
		return ret;
	}

	chunk = kmalloc(btrfs_chunk_item_size(num_stripes), GFP_NOFS);
	if (!chunk) {
		btrfs_free_path(path);
		return -ENOMEM;
	}

	map = kmalloc(map_lookup_size(num_stripes), GFP_NOFS);
	if (!map) {
		kfree(chunk);
		btrfs_free_path(path);
		return -ENOMEM;
	}
	btrfs_free_path(path);
	path = NULL;

	stripes = &chunk->stripe;
	*num_bytes = chunk_bytes_by_type(type, calc_size,
					 num_stripes, sub_stripes);

	index = 0;
	while(index < num_stripes) {
		struct btrfs_stripe *stripe;
		BUG_ON(list_empty(&private_devs));
		cur = private_devs.next;
		device = list_entry(cur, struct btrfs_device, dev_alloc_list);

		/* loop over this device again if we're doing a dup group */
		if (!(type & BTRFS_BLOCK_GROUP_DUP) ||
		    (index == num_stripes - 1))
			list_move_tail(&device->dev_alloc_list, dev_list);

		ret = btrfs_alloc_dev_extent(trans, device,
			     info->chunk_root->root_key.objectid,
			     BTRFS_FIRST_CHUNK_TREE_OBJECTID, key.offset,
			     calc_size, &dev_offset);
		BUG_ON(ret);
		device->bytes_used += calc_size;
		ret = btrfs_update_device(trans, device);
		BUG_ON(ret);

		map->stripes[index].dev = device;
		map->stripes[index].physical = dev_offset;
		stripe = stripes + index;
		btrfs_set_stack_stripe_devid(stripe, device->devid);
		btrfs_set_stack_stripe_offset(stripe, dev_offset);
		memcpy(stripe->dev_uuid, device->uuid, BTRFS_UUID_SIZE);
		physical = dev_offset;
		index++;
	}
	BUG_ON(!list_empty(&private_devs));

	/* key was set above */
	btrfs_set_stack_chunk_length(chunk, *num_bytes);
	btrfs_set_stack_chunk_owner(chunk, extent_root->root_key.objectid);
	btrfs_set_stack_chunk_stripe_len(chunk, stripe_len);
	btrfs_set_stack_chunk_type(chunk, type);
	btrfs_set_stack_chunk_num_stripes(chunk, num_stripes);
	btrfs_set_stack_chunk_io_align(chunk, stripe_len);
	btrfs_set_stack_chunk_io_width(chunk, stripe_len);
	btrfs_set_stack_chunk_sector_size(chunk, extent_root->sectorsize);
	btrfs_set_stack_chunk_sub_stripes(chunk, sub_stripes);
	map->sector_size = extent_root->sectorsize;
	map->stripe_len = stripe_len;
	map->io_align = stripe_len;
	map->io_width = stripe_len;
	map->type = type;
	map->num_stripes = num_stripes;
	map->sub_stripes = sub_stripes;

	ret = btrfs_insert_item(trans, chunk_root, &key, chunk,
				btrfs_chunk_item_size(num_stripes));
	BUG_ON(ret);
	*start = key.offset;;

	em = alloc_extent_map(GFP_NOFS);
	if (!em)
		return -ENOMEM;
	em->bdev = (struct block_device *)map;
	em->start = key.offset;
	em->len = *num_bytes;
	em->block_start = 0;

	if (type & BTRFS_BLOCK_GROUP_SYSTEM) {
		ret = btrfs_add_system_chunk(trans, chunk_root, &key,
				    chunk, btrfs_chunk_item_size(num_stripes));
		BUG_ON(ret);
	}
	kfree(chunk);

	em_tree = &extent_root->fs_info->mapping_tree.map_tree;
	spin_lock(&em_tree->lock);
	ret = add_extent_mapping(em_tree, em);
	spin_unlock(&em_tree->lock);
	BUG_ON(ret);
	free_extent_map(em);
	return ret;
}

void btrfs_mapping_init(struct btrfs_mapping_tree *tree)
{
	extent_map_tree_init(&tree->map_tree, GFP_NOFS);
}

void btrfs_mapping_tree_free(struct btrfs_mapping_tree *tree)
{
	struct extent_map *em;

	while(1) {
		spin_lock(&tree->map_tree.lock);
		em = lookup_extent_mapping(&tree->map_tree, 0, (u64)-1);
		if (em)
			remove_extent_mapping(&tree->map_tree, em);
		spin_unlock(&tree->map_tree.lock);
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

	spin_lock(&em_tree->lock);
	em = lookup_extent_mapping(em_tree, logical, len);
	spin_unlock(&em_tree->lock);
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

	if (multi_ret && !(rw & (1 << BIO_RW))) {
		stripes_allocated = 1;
	}
again:
	if (multi_ret) {
		multi = kzalloc(btrfs_multi_bio_size(stripes_allocated),
				GFP_NOFS);
		if (!multi)
			return -ENOMEM;

		atomic_set(&multi->error, 0);
	}

	spin_lock(&em_tree->lock);
	em = lookup_extent_mapping(em_tree, logical, *length);
	spin_unlock(&em_tree->lock);

	if (!em && unplug_page)
		return 0;

	if (!em) {
		printk("unable to find logical %Lu len %Lu\n", logical, *length);
		BUG();
	}

	BUG_ON(em->start > logical || em->start + em->len < logical);
	map = (struct map_lookup *)em->bdev;
	offset = logical - em->start;

	if (mirror_num > map->num_stripes)
		mirror_num = 0;

	/* if our multi bio struct is too small, back off and try again */
	if (rw & (1 << BIO_RW)) {
		if (map->type & (BTRFS_BLOCK_GROUP_RAID1 |
				 BTRFS_BLOCK_GROUP_DUP)) {
			stripes_required = map->num_stripes;
			max_errors = 1;
		} else if (map->type & BTRFS_BLOCK_GROUP_RAID10) {
			stripes_required = map->sub_stripes;
			max_errors = 1;
		}
	}
	if (multi_ret && rw == WRITE &&
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
		if (unplug_page || (rw & (1 << BIO_RW)))
			num_stripes = map->num_stripes;
		else if (mirror_num)
			stripe_index = mirror_num - 1;
		else {
			stripe_index = find_live_mirror(map, 0,
					    map->num_stripes,
					    current->pid % map->num_stripes);
		}

	} else if (map->type & BTRFS_BLOCK_GROUP_DUP) {
		if (rw & (1 << BIO_RW))
			num_stripes = map->num_stripes;
		else if (mirror_num)
			stripe_index = mirror_num - 1;

	} else if (map->type & BTRFS_BLOCK_GROUP_RAID10) {
		int factor = map->num_stripes / map->sub_stripes;

		stripe_index = do_div(stripe_nr, factor);
		stripe_index *= map->sub_stripes;

		if (unplug_page || (rw & (1 << BIO_RW)))
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
				if (bdi->unplug_io_fn) {
					bdi->unplug_io_fn(bdi, unplug_page);
				}
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

int btrfs_unplug_page(struct btrfs_mapping_tree *map_tree,
		      u64 logical, struct page *page)
{
	u64 length = PAGE_CACHE_SIZE;
	return __btrfs_map_block(map_tree, READ, logical, &length,
				 NULL, 0, page);
}


#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,23)
static void end_bio_multi_stripe(struct bio *bio, int err)
#else
static int end_bio_multi_stripe(struct bio *bio,
				   unsigned int bytes_done, int err)
#endif
{
	struct btrfs_multi_bio *multi = bio->bi_private;

#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,23)
	if (bio->bi_size)
		return 1;
#endif
	if (err)
		atomic_inc(&multi->error);

	if (atomic_dec_and_test(&multi->stripes_pending)) {
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

#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,23)
		bio_endio(bio, bio->bi_size, err);
#else
		bio_endio(bio, err);
#endif
	} else {
		bio_put(bio);
	}
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,23)
	return 0;
#endif
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
int schedule_bio(struct btrfs_root *root, struct btrfs_device *device,
		 int rw, struct bio *bio)
{
	int should_queue = 1;

	/* don't bother with additional async steps for reads, right now */
	if (!(rw & (1 << BIO_RW))) {
		submit_bio(rw, bio);
		return 0;
	}

	/*
	 * nr_async_sumbits allows us to reliably return congestion to the
	 * higher layers.  Otherwise, the async bio makes it appear we have
	 * made progress against dirty pages when we've really just put it
	 * on a queue for later
	 */
	atomic_inc(&root->fs_info->nr_async_submits);
	bio->bi_next = NULL;
	bio->bi_rw |= rw;

	spin_lock(&device->io_lock);

	if (device->pending_bio_tail)
		device->pending_bio_tail->bi_next = bio;

	device->pending_bio_tail = bio;
	if (!device->pending_bios)
		device->pending_bios = bio;
	if (device->running_pending)
		should_queue = 0;

	spin_unlock(&device->io_lock);

	if (should_queue)
		btrfs_queue_worker(&root->fs_info->workers, &device->work);
	return 0;
}

int btrfs_map_bio(struct btrfs_root *root, int rw, struct bio *bio,
		  int mirror_num, int async_submit)
{
	struct btrfs_mapping_tree *map_tree;
	struct btrfs_device *dev;
	struct bio *first_bio = bio;
	u64 logical = bio->bi_sector << 9;
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
		printk("mapping failed logical %Lu bio len %Lu "
		       "len %Lu\n", logical, length, map_length);
		BUG();
	}
	multi->end_io = first_bio->bi_end_io;
	multi->private = first_bio->bi_private;
	atomic_set(&multi->stripes_pending, multi->num_stripes);

	while(dev_nr < total_devs) {
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
		if (dev && dev->bdev) {
			bio->bi_bdev = dev->bdev;
			if (async_submit)
				schedule_bio(root, dev, rw, bio);
			else
				submit_bio(rw, bio);
		} else {
			bio->bi_bdev = root->fs_info->fs_devices->latest_bdev;
			bio->bi_sector = logical >> 9;
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,23)
			bio_endio(bio, bio->bi_size, -EIO);
#else
			bio_endio(bio, -EIO);
#endif
		}
		dev_nr++;
	}
	if (total_devs == 1)
		kfree(multi);
	return 0;
}

struct btrfs_device *btrfs_find_device(struct btrfs_root *root, u64 devid,
				       u8 *uuid)
{
	struct list_head *head = &root->fs_info->fs_devices->devices;

	return __find_device(head, devid, uuid);
}

static struct btrfs_device *add_missing_dev(struct btrfs_root *root,
					    u64 devid, u8 *dev_uuid)
{
	struct btrfs_device *device;
	struct btrfs_fs_devices *fs_devices = root->fs_info->fs_devices;

	device = kzalloc(sizeof(*device), GFP_NOFS);
	list_add(&device->dev_list,
		 &fs_devices->devices);
	list_add(&device->dev_alloc_list,
		 &fs_devices->alloc_list);
	device->barriers = 1;
	device->dev_root = root->fs_info->dev_root;
	device->devid = devid;
	device->work.func = pending_bios_fn;
	fs_devices->num_devices++;
	spin_lock_init(&device->io_lock);
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

	spin_lock(&map_tree->map_tree.lock);
	em = lookup_extent_mapping(&map_tree->map_tree, logical, 1);
	spin_unlock(&map_tree->map_tree.lock);

	/* already mapped? */
	if (em && em->start <= logical && em->start + em->len > logical) {
		free_extent_map(em);
		return 0;
	} else if (em) {
		free_extent_map(em);
	}

	map = kzalloc(sizeof(*map), GFP_NOFS);
	if (!map)
		return -ENOMEM;

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
		map->stripes[i].dev = btrfs_find_device(root, devid, uuid);

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

	spin_lock(&map_tree->map_tree.lock);
	ret = add_extent_mapping(&map_tree->map_tree, em);
	spin_unlock(&map_tree->map_tree.lock);
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
	device->total_bytes = btrfs_device_total_bytes(leaf, dev_item);
	device->bytes_used = btrfs_device_bytes_used(leaf, dev_item);
	device->type = btrfs_device_type(leaf, dev_item);
	device->io_align = btrfs_device_io_align(leaf, dev_item);
	device->io_width = btrfs_device_io_width(leaf, dev_item);
	device->sector_size = btrfs_device_sector_size(leaf, dev_item);

	ptr = (unsigned long)btrfs_device_uuid(dev_item);
	read_extent_buffer(leaf, device->uuid, ptr, BTRFS_UUID_SIZE);

	return 0;
}

static int read_one_dev(struct btrfs_root *root,
			struct extent_buffer *leaf,
			struct btrfs_dev_item *dev_item)
{
	struct btrfs_device *device;
	u64 devid;
	int ret;
	u8 dev_uuid[BTRFS_UUID_SIZE];

	devid = btrfs_device_id(leaf, dev_item);
	read_extent_buffer(leaf, dev_uuid,
			   (unsigned long)btrfs_device_uuid(dev_item),
			   BTRFS_UUID_SIZE);
	device = btrfs_find_device(root, devid, dev_uuid);
	if (!device) {
		printk("warning devid %Lu missing\n", devid);
		device = add_missing_dev(root, devid, dev_uuid);
		if (!device)
			return -ENOMEM;
	}

	fill_device_from_item(leaf, dev_item, device);
	device->dev_root = root->fs_info->dev_root;
	device->in_fs_metadata = 1;
	ret = 0;
#if 0
	ret = btrfs_open_device(device);
	if (ret) {
		kfree(device);
	}
#endif
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
	while(1) {
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
				BUG_ON(ret);
			}
		} else if (found_key.type == BTRFS_CHUNK_ITEM_KEY) {
			struct btrfs_chunk *chunk;
			chunk = btrfs_item_ptr(leaf, slot, struct btrfs_chunk);
			ret = read_one_chunk(root, &found_key, leaf, chunk);
		}
		path->slots[0]++;
	}
	if (key.objectid == BTRFS_DEV_ITEMS_OBJECTID) {
		key.objectid = 0;
		btrfs_release_path(root, path);
		goto again;
	}

	btrfs_free_path(path);
	ret = 0;
error:
	return ret;
}

