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
#include "ctree.h"
#include "extent_map.h"
#include "disk-io.h"
#include "transaction.h"
#include "print-tree.h"
#include "volumes.h"

struct map_lookup {
	struct btrfs_device *dev;
	u64 physical;
};

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
			   u64 owner, u64 num_bytes, u64 *start)
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
	btrfs_set_dev_extent_owner(leaf, extent, owner);
	btrfs_set_dev_extent_length(leaf, extent, num_bytes);
	btrfs_mark_buffer_dirty(leaf);
err:
	btrfs_free_path(path);
	return ret;
}

static int find_next_chunk(struct btrfs_root *root, u64 *objectid)
{
	struct btrfs_path *path;
	int ret;
	struct btrfs_key key;
	struct btrfs_key found_key;

	path = btrfs_alloc_path();
	BUG_ON(!path);

	key.objectid = (u64)-1;
	key.offset = (u64)-1;
	key.type = BTRFS_CHUNK_ITEM_KEY;

	ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
	if (ret < 0)
		goto error;

	BUG_ON(ret == 0);

	ret = btrfs_previous_item(root, path, 0, BTRFS_CHUNK_ITEM_KEY);
	if (ret) {
		*objectid = 0;
	} else {
		btrfs_item_key_to_cpu(path->nodes[0], &found_key,
				      path->slots[0]);
		*objectid = found_key.objectid + found_key.offset;
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

	btrfs_set_device_id(leaf, dev_item, device->devid);
	btrfs_set_device_type(leaf, dev_item, device->type);
	btrfs_set_device_io_align(leaf, dev_item, device->io_align);
	btrfs_set_device_io_width(leaf, dev_item, device->io_width);
	btrfs_set_device_sector_size(leaf, dev_item, device->sector_size);
	btrfs_set_device_total_bytes(leaf, dev_item, device->total_bytes);
	btrfs_set_device_bytes_used(leaf, dev_item, device->bytes_used);

	ptr = (unsigned long)btrfs_device_uuid(dev_item);
	write_extent_buffer(leaf, device->uuid, ptr, BTRFS_DEV_UUID_SIZE);
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
	struct btrfs_root *chunk_root = extent_root->fs_info->chunk_root;
	struct btrfs_stripe *stripes;
	struct btrfs_device *device = NULL;
	struct btrfs_chunk *chunk;
	struct list_head private_devs;
	struct list_head *dev_list = &extent_root->fs_info->devices;
	struct list_head *cur;
	struct extent_map_tree *em_tree;
	struct map_lookup *map;
	struct extent_map *em;
	u64 physical;
	u64 calc_size = 1024 * 1024 * 1024;
	u64 avail;
	u64 max_avail = 0;
	int num_stripes = 1;
	int looped = 0;
	int ret;
	int index;
	struct btrfs_key key;

	if (list_empty(dev_list))
		return -ENOSPC;
again:
	INIT_LIST_HEAD(&private_devs);
	cur = dev_list->next;
	index = 0;
	/* build a private list of devices we will allocate from */
	while(index < num_stripes) {
		device = list_entry(cur, struct btrfs_device, dev_list);
		avail = device->total_bytes - device->bytes_used;
		cur = cur->next;
		if (avail > max_avail)
			max_avail = avail;
		if (avail >= calc_size) {
			list_move_tail(&device->dev_list, &private_devs);
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

	ret = find_next_chunk(chunk_root, &key.objectid);
	if (ret)
		return ret;

	chunk = kmalloc(btrfs_chunk_item_size(num_stripes), GFP_NOFS);
	if (!chunk)
		return -ENOMEM;

	stripes = &chunk->stripe;

	*num_bytes = calc_size;
	index = 0;
	while(index < num_stripes) {
		BUG_ON(list_empty(&private_devs));
		cur = private_devs.next;
		device = list_entry(cur, struct btrfs_device, dev_list);
		list_move_tail(&device->dev_list, dev_list);

		ret = btrfs_alloc_dev_extent(trans, device,
					     key.objectid,
					     calc_size, &dev_offset);
		BUG_ON(ret);

		device->bytes_used += calc_size;
		ret = btrfs_update_device(trans, device);
		BUG_ON(ret);

		btrfs_set_stack_stripe_devid(stripes + index, device->devid);
		btrfs_set_stack_stripe_offset(stripes + index, dev_offset);
		physical = dev_offset;
		index++;
	}
	BUG_ON(!list_empty(&private_devs));

	/* key.objectid was set above */
	key.offset = *num_bytes;
	key.type = BTRFS_CHUNK_ITEM_KEY;
	btrfs_set_stack_chunk_owner(chunk, extent_root->root_key.objectid);
	btrfs_set_stack_chunk_stripe_len(chunk, 64 * 1024);
	btrfs_set_stack_chunk_type(chunk, type);
	btrfs_set_stack_chunk_num_stripes(chunk, num_stripes);
	btrfs_set_stack_chunk_io_align(chunk, extent_root->sectorsize);
	btrfs_set_stack_chunk_io_width(chunk, extent_root->sectorsize);
	btrfs_set_stack_chunk_sector_size(chunk, extent_root->sectorsize);

	ret = btrfs_insert_item(trans, chunk_root, &key, chunk,
				btrfs_chunk_item_size(num_stripes));
	BUG_ON(ret);
	*start = key.objectid;

	em = alloc_extent_map(GFP_NOFS);
	if (!em)
		return -ENOMEM;
	map = kmalloc(sizeof(*map), GFP_NOFS);
	if (!map) {
		free_extent_map(em);
		return -ENOMEM;
	}

	em->bdev = (struct block_device *)map;
	em->start = key.objectid;
	em->len = key.offset;
	em->block_start = 0;

	map->physical = physical;
	map->dev = device;

	if (!map->dev) {
		kfree(map);
		free_extent_map(em);
		return -EIO;
	}
	kfree(chunk);

	em_tree = &extent_root->fs_info->mapping_tree.map_tree;
	spin_lock(&em_tree->lock);
	ret = add_extent_mapping(em_tree, em);
	BUG_ON(ret);
	spin_unlock(&em_tree->lock);
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

int btrfs_map_block(struct btrfs_mapping_tree *map_tree,
		    u64 logical, u64 *phys, u64 *length,
		    struct btrfs_device **dev)
{
	struct extent_map *em;
	struct map_lookup *map;
	struct extent_map_tree *em_tree = &map_tree->map_tree;
	u64 offset;


	spin_lock(&em_tree->lock);
	em = lookup_extent_mapping(em_tree, logical, *length);
	BUG_ON(!em);

	BUG_ON(em->start > logical || em->start + em->len < logical);
	map = (struct map_lookup *)em->bdev;
	offset = logical - em->start;
	*phys = map->physical + offset;
	*length = em->len - offset;
	*dev = map->dev;
	free_extent_map(em);
	spin_unlock(&em_tree->lock);
	return 0;
}

int btrfs_map_bio(struct btrfs_root *root, int rw, struct bio *bio)
{
	struct btrfs_mapping_tree *map_tree;
	struct btrfs_device *dev;
	u64 logical = bio->bi_sector << 9;
	u64 physical;
	u64 length = 0;
	u64 map_length;
	struct bio_vec *bvec;
	int i;
	int ret;

	bio_for_each_segment(bvec, bio, i) {
		length += bvec->bv_len;
	}
	map_tree = &root->fs_info->mapping_tree;
	map_length = length;
	ret = btrfs_map_block(map_tree, logical, &physical, &map_length, &dev);
	BUG_ON(map_length < length);
	bio->bi_sector = physical >> 9;
	bio->bi_bdev = dev->bdev;
	submit_bio(rw, bio);
	return 0;
}

struct btrfs_device *btrfs_find_device(struct btrfs_root *root, u64 devid)
{
	struct btrfs_device *dev;
	struct list_head *cur = root->fs_info->devices.next;
	struct list_head *head = &root->fs_info->devices;

	while(cur != head) {
		dev = list_entry(cur, struct btrfs_device, dev_list);
		if (dev->devid == devid)
			return dev;
		cur = cur->next;
	}
	return NULL;
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
	int ret;

	logical = key->objectid;
	length = key->offset;
	spin_lock(&map_tree->map_tree.lock);
	em = lookup_extent_mapping(&map_tree->map_tree, logical, 1);

	/* already mapped? */
	if (em && em->start <= logical && em->start + em->len > logical) {
		free_extent_map(em);
		spin_unlock(&map_tree->map_tree.lock);
		return 0;
	} else if (em) {
		free_extent_map(em);
	}
	spin_unlock(&map_tree->map_tree.lock);

	map = kzalloc(sizeof(*map), GFP_NOFS);
	if (!map)
		return -ENOMEM;

	em = alloc_extent_map(GFP_NOFS);
	if (!em)
		return -ENOMEM;
	map = kmalloc(sizeof(*map), GFP_NOFS);
	if (!map) {
		free_extent_map(em);
		return -ENOMEM;
	}

	em->bdev = (struct block_device *)map;
	em->start = logical;
	em->len = length;
	em->block_start = 0;

	map->physical = btrfs_stripe_offset_nr(leaf, chunk, 0);
	devid = btrfs_stripe_devid_nr(leaf, chunk, 0);
	map->dev = btrfs_find_device(root, devid);
	if (!map->dev) {
		kfree(map);
		free_extent_map(em);
		return -EIO;
	}

	spin_lock(&map_tree->map_tree.lock);
	ret = add_extent_mapping(&map_tree->map_tree, em);
	BUG_ON(ret);
	spin_unlock(&map_tree->map_tree.lock);
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
	read_extent_buffer(leaf, device->uuid, ptr, BTRFS_DEV_UUID_SIZE);

	return 0;
}

static int read_one_dev(struct btrfs_root *root,
			struct extent_buffer *leaf,
			struct btrfs_dev_item *dev_item)
{
	struct btrfs_device *device;
	u64 devid;
	int ret;

	devid = btrfs_device_id(leaf, dev_item);
	device = btrfs_find_device(root, devid);
	if (!device) {
		device = kmalloc(sizeof(*device), GFP_NOFS);
		if (!device)
			return -ENOMEM;
		list_add(&device->dev_list, &root->fs_info->devices);
	}

	fill_device_from_item(leaf, dev_item, device);
	device->dev_root = root->fs_info->dev_root;
	device->bdev = root->fs_info->sb->s_bdev;
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

