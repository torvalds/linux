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
#include <asm/div64.h>
#include "ctree.h"
#include "extent_map.h"
#include "disk-io.h"
#include "transaction.h"
#include "print-tree.h"
#include "volumes.h"

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
			printk("uuid cleanup finds %s\n", dev->name);
			if (dev->bdev) {
				printk("closing\n");
				close_bdev_excl(dev->bdev);
			}
			list_del(&dev->dev_list);
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
		    !memcmp(dev->uuid, uuid, BTRFS_UUID_SIZE)) {
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

static int device_list_add(const char *path,
			   struct btrfs_super_block *disk_super,
			   u64 devid, struct btrfs_fs_devices **fs_devices_ret)
{
	struct btrfs_device *device;
	struct btrfs_fs_devices *fs_devices;
	u64 found_transid = btrfs_super_generation(disk_super);

	fs_devices = find_fsid(disk_super->fsid);
	if (!fs_devices) {
		fs_devices = kmalloc(sizeof(*fs_devices), GFP_NOFS);
		if (!fs_devices)
			return -ENOMEM;
		INIT_LIST_HEAD(&fs_devices->devices);
		list_add(&fs_devices->list, &fs_uuids);
		memcpy(fs_devices->fsid, disk_super->fsid, BTRFS_FSID_SIZE);
		fs_devices->latest_devid = devid;
		fs_devices->latest_trans = found_transid;
		fs_devices->lowest_devid = (u64)-1;
		fs_devices->num_devices = 0;
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
		fs_devices->num_devices++;
	}

	if (found_transid > fs_devices->latest_trans) {
		fs_devices->latest_devid = devid;
		fs_devices->latest_trans = found_transid;
	}
	if (fs_devices->lowest_devid > devid) {
		fs_devices->lowest_devid = devid;
		printk("lowest devid now %Lu\n", devid);
	}
	*fs_devices_ret = fs_devices;
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
			printk("close devices closes %s\n", device->name);
		}
		device->bdev = NULL;
	}
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
	int ret;

	mutex_lock(&uuid_mutex);
	list_for_each(cur, head) {
		device = list_entry(cur, struct btrfs_device, dev_list);
		bdev = open_bdev_excl(device->name, flags, holder);

		if (IS_ERR(bdev)) {
			printk("open %s failed\n", device->name);
			ret = PTR_ERR(bdev);
			goto fail;
		}
		if (device->devid == fs_devices->latest_devid)
			fs_devices->latest_bdev = bdev;
		if (device->devid == fs_devices->lowest_devid) {
			fs_devices->lowest_bdev = bdev;
		}
		device->bdev = bdev;
	}
	mutex_unlock(&uuid_mutex);
	return 0;
fail:
	mutex_unlock(&uuid_mutex);
	btrfs_close_devices(fs_devices);
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

	printk("scan one opens %s\n", path);
	bdev = open_bdev_excl(path, flags, holder);

	if (IS_ERR(bdev)) {
		printk("open failed\n");
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
		printk("no btrfs found on %s\n", path);
		ret = -EINVAL;
		goto error_brelse;
	}
	devid = le64_to_cpu(disk_super->dev_item.devid);
	transid = btrfs_super_generation(disk_super);
	printk("found device %Lu transid %Lu on %s\n", devid, transid, path);
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
	u64 free_devid;

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

int btrfs_alloc_chunk(struct btrfs_trans_handle *trans,
		      struct btrfs_root *extent_root, u64 *start,
		      u64 *num_bytes, u64 type)
{
	u64 dev_offset;
	struct btrfs_fs_info *info = extent_root->fs_info;
	struct btrfs_root *chunk_root = extent_root->fs_info->chunk_root;
	struct btrfs_stripe *stripes;
	struct btrfs_device *device = NULL;
	struct btrfs_chunk *chunk;
	struct list_head private_devs;
	struct list_head *dev_list = &extent_root->fs_info->fs_devices->devices;
	struct list_head *cur;
	struct extent_map_tree *em_tree;
	struct map_lookup *map;
	struct extent_map *em;
	u64 physical;
	u64 calc_size = 1024 * 1024 * 1024;
	u64 min_free = calc_size;
	u64 avail;
	u64 max_avail = 0;
	int num_stripes = 1;
	int sub_stripes = 0;
	int looped = 0;
	int ret;
	int index;
	int stripe_len = 64 * 1024;
	struct btrfs_key key;

	if (list_empty(dev_list))
		return -ENOSPC;

	if (type & (BTRFS_BLOCK_GROUP_RAID0))
		num_stripes = btrfs_super_num_devices(&info->super_copy);
	if (type & (BTRFS_BLOCK_GROUP_DUP))
		num_stripes = 2;
	if (type & (BTRFS_BLOCK_GROUP_RAID1)) {
		num_stripes = min_t(u64, 2,
				  btrfs_super_num_devices(&info->super_copy));
	}
	if (type & (BTRFS_BLOCK_GROUP_RAID10)) {
		num_stripes = btrfs_super_num_devices(&info->super_copy);
		if (num_stripes < 4)
			return -ENOSPC;
		num_stripes &= ~(u32)1;
		sub_stripes = 2;
	}
again:
	INIT_LIST_HEAD(&private_devs);
	cur = dev_list->next;
	index = 0;

	if (type & BTRFS_BLOCK_GROUP_DUP)
		min_free = calc_size * 2;

	/* build a private list of devices we will allocate from */
	while(index < num_stripes) {
		device = list_entry(cur, struct btrfs_device, dev_list);

		avail = device->total_bytes - device->bytes_used;
		cur = cur->next;
		if (avail > max_avail)
			max_avail = avail;
		if (avail >= min_free) {
			list_move_tail(&device->dev_list, &private_devs);
			index++;
			if (type & BTRFS_BLOCK_GROUP_DUP)
				index++;
		}
		if (cur == dev_list)
			break;
	}
	if (index < num_stripes) {
		list_splice(&private_devs, dev_list);
		if (!looped && max_avail > 0) {
			looped = 1;
			calc_size = max_avail;
			goto again;
		}
		return -ENOSPC;
	}

	key.objectid = BTRFS_FIRST_CHUNK_TREE_OBJECTID;
	key.type = BTRFS_CHUNK_ITEM_KEY;
	ret = find_next_chunk(chunk_root, BTRFS_FIRST_CHUNK_TREE_OBJECTID,
			      &key.offset);
	if (ret)
		return ret;

	chunk = kmalloc(btrfs_chunk_item_size(num_stripes), GFP_NOFS);
	if (!chunk)
		return -ENOMEM;

	map = kmalloc(map_lookup_size(num_stripes), GFP_NOFS);
	if (!map) {
		kfree(chunk);
		return -ENOMEM;
	}

	stripes = &chunk->stripe;

	if (type & (BTRFS_BLOCK_GROUP_RAID1 | BTRFS_BLOCK_GROUP_DUP))
		*num_bytes = calc_size;
	else if (type & BTRFS_BLOCK_GROUP_RAID10)
		*num_bytes = calc_size * (num_stripes / sub_stripes);
	else
		*num_bytes = calc_size * num_stripes;

	index = 0;
printk("new chunk type %Lu start %Lu size %Lu\n", type, key.offset, *num_bytes);
	while(index < num_stripes) {
		struct btrfs_stripe *stripe;
		BUG_ON(list_empty(&private_devs));
		cur = private_devs.next;
		device = list_entry(cur, struct btrfs_device, dev_list);

		/* loop over this device again if we're doing a dup group */
		if (!(type & BTRFS_BLOCK_GROUP_DUP) ||
		    (index == num_stripes - 1))
			list_move_tail(&device->dev_list, dev_list);

		ret = btrfs_alloc_dev_extent(trans, device,
			     info->chunk_root->root_key.objectid,
			     BTRFS_FIRST_CHUNK_TREE_OBJECTID, key.offset,
			     calc_size, &dev_offset);
		BUG_ON(ret);
printk("alloc chunk start %Lu size %Lu from dev %Lu type %Lu\n", key.offset, calc_size, device->devid, type);
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

int btrfs_map_block(struct btrfs_mapping_tree *map_tree, int rw,
		    u64 logical, u64 *length,
		    struct btrfs_multi_bio **multi_ret, int mirror_num)
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
	}

	spin_lock(&em_tree->lock);
	em = lookup_extent_mapping(em_tree, logical, *length);
	spin_unlock(&em_tree->lock);
	if (!em) {
		printk("unable to find logical %Lu\n", logical);
	}
	BUG_ON(!em);

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
		} else if (map->type & BTRFS_BLOCK_GROUP_RAID10) {
			stripes_required = map->sub_stripes;
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
	if (!multi_ret)
		goto out;

	multi->num_stripes = 1;
	stripe_index = 0;
	if (map->type & BTRFS_BLOCK_GROUP_RAID1) {
		if (rw & (1 << BIO_RW))
			multi->num_stripes = map->num_stripes;
		else if (mirror_num) {
			stripe_index = mirror_num - 1;
		} else {
			int i;
			u64 least = (u64)-1;
			struct btrfs_device *cur;

			for (i = 0; i < map->num_stripes; i++) {
				cur = map->stripes[i].dev;
				spin_lock(&cur->io_lock);
				if (cur->total_ios < least) {
					least = cur->total_ios;
					stripe_index = i;
				}
				spin_unlock(&cur->io_lock);
			}
		}
	} else if (map->type & BTRFS_BLOCK_GROUP_DUP) {
		if (rw & (1 << BIO_RW))
			multi->num_stripes = map->num_stripes;
		else if (mirror_num)
			stripe_index = mirror_num - 1;
	} else if (map->type & BTRFS_BLOCK_GROUP_RAID10) {
		int factor = map->num_stripes / map->sub_stripes;
		int orig_stripe_nr = stripe_nr;

		stripe_index = do_div(stripe_nr, factor);
		stripe_index *= map->sub_stripes;

		if (rw & (1 << BIO_RW))
			multi->num_stripes = map->sub_stripes;
		else if (mirror_num)
			stripe_index += mirror_num - 1;
		else
			stripe_index += orig_stripe_nr % map->sub_stripes;
	} else {
		/*
		 * after this do_div call, stripe_nr is the number of stripes
		 * on this device we have to walk to find the data, and
		 * stripe_index is the number of our device in the stripe array
		 */
		stripe_index = do_div(stripe_nr, map->num_stripes);
	}
	BUG_ON(stripe_index >= map->num_stripes);

	for (i = 0; i < multi->num_stripes; i++) {
		multi->stripes[i].physical =
			map->stripes[stripe_index].physical + stripe_offset +
			stripe_nr * map->stripe_len;
		multi->stripes[i].dev = map->stripes[stripe_index].dev;
		stripe_index++;
	}
	*multi_ret = multi;
out:
	free_extent_map(em);
	return 0;
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
		multi->error = err;

	if (atomic_dec_and_test(&multi->stripes_pending)) {
		bio->bi_private = multi->private;
		bio->bi_end_io = multi->end_io;

		if (!err && multi->error)
			err = multi->error;
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

int btrfs_map_bio(struct btrfs_root *root, int rw, struct bio *bio,
		  int mirror_num)
{
	struct btrfs_mapping_tree *map_tree;
	struct btrfs_device *dev;
	struct bio *first_bio = bio;
	u64 logical = bio->bi_sector << 9;
	u64 length = 0;
	u64 map_length;
	struct bio_vec *bvec;
	struct btrfs_multi_bio *multi = NULL;
	int i;
	int ret;
	int dev_nr = 0;
	int total_devs = 1;

	bio_for_each_segment(bvec, bio, i) {
		length += bvec->bv_len;
	}

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
		bio->bi_bdev = dev->bdev;
		spin_lock(&dev->io_lock);
		dev->total_ios++;
		spin_unlock(&dev->io_lock);
		submit_bio(rw, bio);
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
		if (!map->stripes[i].dev) {
			kfree(map);
			free_extent_map(em);
			return -EIO;
		}
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
		printk("warning devid %Lu not found already\n", devid);
		device = kzalloc(sizeof(*device), GFP_NOFS);
		if (!device)
			return -ENOMEM;
		list_add(&device->dev_list,
			 &root->fs_info->fs_devices->devices);
		device->barriers = 1;
		spin_lock_init(&device->io_lock);
	}

	fill_device_from_item(leaf, dev_item, device);
	device->dev_root = root->fs_info->dev_root;
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
	struct extent_buffer *sb = root->fs_info->sb_buffer;
	struct btrfs_disk_key *disk_key;
	struct btrfs_chunk *chunk;
	struct btrfs_key key;
	u32 num_stripes;
	u32 array_size;
	u32 len = 0;
	u8 *ptr;
	unsigned long sb_ptr;
	u32 cur;
	int ret;

	array_size = btrfs_super_sys_array_size(super_copy);

	/*
	 * we do this loop twice, once for the device items and
	 * once for all of the chunks.  This way there are device
	 * structs filled in for every chunk
	 */
	ptr = super_copy->sys_chunk_array;
	sb_ptr = offsetof(struct btrfs_super_block, sys_chunk_array);
	cur = 0;

	while (cur < array_size) {
		disk_key = (struct btrfs_disk_key *)ptr;
		btrfs_disk_key_to_cpu(&key, disk_key);

		len = sizeof(*disk_key);
		ptr += len;
		sb_ptr += len;
		cur += len;

		if (key.type == BTRFS_CHUNK_ITEM_KEY) {
			chunk = (struct btrfs_chunk *)sb_ptr;
			ret = read_one_chunk(root, &key, sb, chunk);
			BUG_ON(ret);
			num_stripes = btrfs_chunk_num_stripes(sb, chunk);
			len = btrfs_chunk_item_size(num_stripes);
		} else {
			BUG();
		}
		ptr += len;
		sb_ptr += len;
		cur += len;
	}
	return 0;
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

