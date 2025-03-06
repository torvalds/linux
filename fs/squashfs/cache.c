// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Squashfs - a compressed read only filesystem for Linux
 *
 * Copyright (c) 2002, 2003, 2004, 2005, 2006, 2007, 2008
 * Phillip Lougher <phillip@squashfs.org.uk>
 *
 * cache.c
 */

/*
 * Blocks in Squashfs are compressed.  To avoid repeatedly decompressing
 * recently accessed data Squashfs uses two small metadata and fragment caches.
 *
 * This file implements a generic cache implementation used for both caches,
 * plus functions layered ontop of the generic cache implementation to
 * access the metadata and fragment caches.
 *
 * To avoid out of memory and fragmentation issues with vmalloc the cache
 * uses sequences of kmalloced PAGE_SIZE buffers.
 *
 * It should be noted that the cache is not used for file datablocks, these
 * are decompressed and cached in the page-cache in the normal way.  The
 * cache is only used to temporarily cache fragment and metadata blocks
 * which have been read as as a result of a metadata (i.e. inode or
 * directory) or fragment access.  Because metadata and fragments are packed
 * together into blocks (to gain greater compression) the read of a particular
 * piece of metadata or fragment will retrieve other metadata/fragments which
 * have been packed with it, these because of locality-of-reference may be read
 * in the near future. Temporarily caching them ensures they are available for
 * near future access without requiring an additional read and decompress.
 */

#include <linux/fs.h>
#include <linux/vfs.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/wait.h>
#include <linux/pagemap.h>

#include "squashfs_fs.h"
#include "squashfs_fs_sb.h"
#include "squashfs.h"
#include "page_actor.h"

/*
 * Look-up block in cache, and increment usage count.  If not in cache, read
 * and decompress it from disk.
 */
struct squashfs_cache_entry *squashfs_cache_get(struct super_block *sb,
	struct squashfs_cache *cache, u64 block, int length)
{
	int i, n;
	struct squashfs_cache_entry *entry;

	spin_lock(&cache->lock);

	while (1) {
		for (i = cache->curr_blk, n = 0; n < cache->entries; n++) {
			if (cache->entry[i].block == block) {
				cache->curr_blk = i;
				break;
			}
			i = (i + 1) % cache->entries;
		}

		if (n == cache->entries) {
			/*
			 * Block not in cache, if all cache entries are used
			 * go to sleep waiting for one to become available.
			 */
			if (cache->unused == 0) {
				cache->num_waiters++;
				spin_unlock(&cache->lock);
				wait_event(cache->wait_queue, cache->unused);
				spin_lock(&cache->lock);
				cache->num_waiters--;
				continue;
			}

			/*
			 * At least one unused cache entry.  A simple
			 * round-robin strategy is used to choose the entry to
			 * be evicted from the cache.
			 */
			i = cache->next_blk;
			for (n = 0; n < cache->entries; n++) {
				if (cache->entry[i].refcount == 0)
					break;
				i = (i + 1) % cache->entries;
			}

			cache->next_blk = (i + 1) % cache->entries;
			entry = &cache->entry[i];

			/*
			 * Initialise chosen cache entry, and fill it in from
			 * disk.
			 */
			cache->unused--;
			entry->block = block;
			entry->refcount = 1;
			entry->pending = 1;
			entry->num_waiters = 0;
			entry->error = 0;
			spin_unlock(&cache->lock);

			entry->length = squashfs_read_data(sb, block, length,
				&entry->next_index, entry->actor);

			spin_lock(&cache->lock);

			if (entry->length < 0)
				entry->error = entry->length;

			entry->pending = 0;

			/*
			 * While filling this entry one or more other processes
			 * have looked it up in the cache, and have slept
			 * waiting for it to become available.
			 */
			if (entry->num_waiters) {
				spin_unlock(&cache->lock);
				wake_up_all(&entry->wait_queue);
			} else
				spin_unlock(&cache->lock);

			goto out;
		}

		/*
		 * Block already in cache.  Increment refcount so it doesn't
		 * get reused until we're finished with it, if it was
		 * previously unused there's one less cache entry available
		 * for reuse.
		 */
		entry = &cache->entry[i];
		if (entry->refcount == 0)
			cache->unused--;
		entry->refcount++;

		/*
		 * If the entry is currently being filled in by another process
		 * go to sleep waiting for it to become available.
		 */
		if (entry->pending) {
			entry->num_waiters++;
			spin_unlock(&cache->lock);
			wait_event(entry->wait_queue, !entry->pending);
		} else
			spin_unlock(&cache->lock);

		goto out;
	}

out:
	TRACE("Got %s %d, start block %lld, refcount %d, error %d\n",
		cache->name, i, entry->block, entry->refcount, entry->error);

	if (entry->error)
		ERROR("Unable to read %s cache entry [%llx]\n", cache->name,
							block);
	return entry;
}


/*
 * Release cache entry, once usage count is zero it can be reused.
 */
void squashfs_cache_put(struct squashfs_cache_entry *entry)
{
	struct squashfs_cache *cache = entry->cache;

	spin_lock(&cache->lock);
	entry->refcount--;
	if (entry->refcount == 0) {
		cache->unused++;
		/*
		 * If there's any processes waiting for a block to become
		 * available, wake one up.
		 */
		if (cache->num_waiters) {
			spin_unlock(&cache->lock);
			wake_up(&cache->wait_queue);
			return;
		}
	}
	spin_unlock(&cache->lock);
}

/*
 * Delete cache reclaiming all kmalloced buffers.
 */
void squashfs_cache_delete(struct squashfs_cache *cache)
{
	int i, j;

	if (IS_ERR(cache) || cache == NULL)
		return;

	for (i = 0; i < cache->entries; i++) {
		if (cache->entry[i].data) {
			for (j = 0; j < cache->pages; j++)
				kfree(cache->entry[i].data[j]);
			kfree(cache->entry[i].data);
		}
		kfree(cache->entry[i].actor);
	}

	kfree(cache->entry);
	kfree(cache);
}


/*
 * Initialise cache allocating the specified number of entries, each of
 * size block_size.  To avoid vmalloc fragmentation issues each entry
 * is allocated as a sequence of kmalloced PAGE_SIZE buffers.
 */
struct squashfs_cache *squashfs_cache_init(char *name, int entries,
	int block_size)
{
	int i, j;
	struct squashfs_cache *cache;

	if (entries == 0)
		return NULL;

	cache = kzalloc(sizeof(*cache), GFP_KERNEL);
	if (cache == NULL) {
		ERROR("Failed to allocate %s cache\n", name);
		return ERR_PTR(-ENOMEM);
	}

	cache->entry = kcalloc(entries, sizeof(*(cache->entry)), GFP_KERNEL);
	if (cache->entry == NULL) {
		ERROR("Failed to allocate %s cache\n", name);
		goto cleanup;
	}

	cache->curr_blk = 0;
	cache->next_blk = 0;
	cache->unused = entries;
	cache->entries = entries;
	cache->block_size = block_size;
	cache->pages = block_size >> PAGE_SHIFT;
	cache->pages = cache->pages ? cache->pages : 1;
	cache->name = name;
	cache->num_waiters = 0;
	spin_lock_init(&cache->lock);
	init_waitqueue_head(&cache->wait_queue);

	for (i = 0; i < entries; i++) {
		struct squashfs_cache_entry *entry = &cache->entry[i];

		init_waitqueue_head(&cache->entry[i].wait_queue);
		entry->cache = cache;
		entry->block = SQUASHFS_INVALID_BLK;
		entry->data = kcalloc(cache->pages, sizeof(void *), GFP_KERNEL);
		if (entry->data == NULL) {
			ERROR("Failed to allocate %s cache entry\n", name);
			goto cleanup;
		}

		for (j = 0; j < cache->pages; j++) {
			entry->data[j] = kmalloc(PAGE_SIZE, GFP_KERNEL);
			if (entry->data[j] == NULL) {
				ERROR("Failed to allocate %s buffer\n", name);
				goto cleanup;
			}
		}

		entry->actor = squashfs_page_actor_init(entry->data,
						cache->pages, 0);
		if (entry->actor == NULL) {
			ERROR("Failed to allocate %s cache entry\n", name);
			goto cleanup;
		}
	}

	return cache;

cleanup:
	squashfs_cache_delete(cache);
	return ERR_PTR(-ENOMEM);
}


/*
 * Copy up to length bytes from cache entry to buffer starting at offset bytes
 * into the cache entry.  If there's not length bytes then copy the number of
 * bytes available.  In all cases return the number of bytes copied.
 */
int squashfs_copy_data(void *buffer, struct squashfs_cache_entry *entry,
		int offset, int length)
{
	int remaining = length;

	if (length == 0)
		return 0;
	else if (buffer == NULL)
		return min(length, entry->length - offset);

	while (offset < entry->length) {
		void *buff = entry->data[offset / PAGE_SIZE]
				+ (offset % PAGE_SIZE);
		int bytes = min_t(int, entry->length - offset,
				PAGE_SIZE - (offset % PAGE_SIZE));

		if (bytes >= remaining) {
			memcpy(buffer, buff, remaining);
			remaining = 0;
			break;
		}

		memcpy(buffer, buff, bytes);
		buffer += bytes;
		remaining -= bytes;
		offset += bytes;
	}

	return length - remaining;
}


/*
 * Read length bytes from metadata position <block, offset> (block is the
 * start of the compressed block on disk, and offset is the offset into
 * the block once decompressed).  Data is packed into consecutive blocks,
 * and length bytes may require reading more than one block.
 */
int squashfs_read_metadata(struct super_block *sb, void *buffer,
		u64 *block, int *offset, int length)
{
	struct squashfs_sb_info *msblk = sb->s_fs_info;
	int bytes, res = length;
	struct squashfs_cache_entry *entry;

	TRACE("Entered squashfs_read_metadata [%llx:%x]\n", *block, *offset);

	if (unlikely(length < 0))
		return -EIO;

	while (length) {
		entry = squashfs_cache_get(sb, msblk->block_cache, *block, 0);
		if (entry->error) {
			res = entry->error;
			goto error;
		} else if (*offset >= entry->length) {
			res = -EIO;
			goto error;
		}

		bytes = squashfs_copy_data(buffer, entry, *offset, length);
		if (buffer)
			buffer += bytes;
		length -= bytes;
		*offset += bytes;

		if (*offset == entry->length) {
			*block = entry->next_index;
			*offset = 0;
		}

		squashfs_cache_put(entry);
	}

	return res;

error:
	squashfs_cache_put(entry);
	return res;
}


/*
 * Look-up in the fragmment cache the fragment located at <start_block> in the
 * filesystem.  If necessary read and decompress it from disk.
 */
struct squashfs_cache_entry *squashfs_get_fragment(struct super_block *sb,
				u64 start_block, int length)
{
	struct squashfs_sb_info *msblk = sb->s_fs_info;

	return squashfs_cache_get(sb, msblk->fragment_cache, start_block,
		length);
}


/*
 * Read and decompress the datablock located at <start_block> in the
 * filesystem.  The cache is used here to avoid duplicating locking and
 * read/decompress code.
 */
struct squashfs_cache_entry *squashfs_get_datablock(struct super_block *sb,
				u64 start_block, int length)
{
	struct squashfs_sb_info *msblk = sb->s_fs_info;

	return squashfs_cache_get(sb, msblk->read_page, start_block, length);
}


/*
 * Read a filesystem table (uncompressed sequence of bytes) from disk
 */
void *squashfs_read_table(struct super_block *sb, u64 block, int length)
{
	int pages = (length + PAGE_SIZE - 1) >> PAGE_SHIFT;
	int i, res;
	void *table, *buffer, **data;
	struct squashfs_page_actor *actor;

	table = buffer = kmalloc(length, GFP_KERNEL);
	if (table == NULL)
		return ERR_PTR(-ENOMEM);

	data = kcalloc(pages, sizeof(void *), GFP_KERNEL);
	if (data == NULL) {
		res = -ENOMEM;
		goto failed;
	}

	actor = squashfs_page_actor_init(data, pages, length);
	if (actor == NULL) {
		res = -ENOMEM;
		goto failed2;
	}

	for (i = 0; i < pages; i++, buffer += PAGE_SIZE)
		data[i] = buffer;

	res = squashfs_read_data(sb, block, length |
		SQUASHFS_COMPRESSED_BIT_BLOCK, NULL, actor);

	kfree(data);
	kfree(actor);

	if (res < 0)
		goto failed;

	return table;

failed2:
	kfree(data);
failed:
	kfree(table);
	return ERR_PTR(res);
}
