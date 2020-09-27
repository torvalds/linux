// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2019, 2020 Amazon.com, Inc. or its affiliates. All rights reserved.
 *
 * User extended attribute client side cache functions.
 *
 * Author: Frank van der Linden <fllinden@amazon.com>
 */
#include <linux/errno.h>
#include <linux/nfs_fs.h>
#include <linux/hashtable.h>
#include <linux/refcount.h>
#include <uapi/linux/xattr.h>

#include "nfs4_fs.h"
#include "internal.h"

/*
 * User extended attributes client side caching is implemented by having
 * a cache structure attached to NFS inodes. This structure is allocated
 * when needed, and freed when the cache is zapped.
 *
 * The cache structure contains as hash table of entries, and a pointer
 * to a special-cased entry for the listxattr cache.
 *
 * Accessing and allocating / freeing the caches is done via reference
 * counting. The cache entries use a similar refcounting scheme.
 *
 * This makes freeing a cache, both from the shrinker and from the
 * zap cache path, easy. It also means that, in current use cases,
 * the large majority of inodes will not waste any memory, as they
 * will never have any user extended attributes assigned to them.
 *
 * Attribute entries are hashed in to a simple hash table. They are
 * also part of an LRU.
 *
 * There are three shrinkers.
 *
 * Two shrinkers deal with the cache entries themselves: one for
 * large entries (> PAGE_SIZE), and one for smaller entries. The
 * shrinker for the larger entries works more aggressively than
 * those for the smaller entries.
 *
 * The other shrinker frees the cache structures themselves.
 */

/*
 * 64 buckets is a good default. There is likely no reasonable
 * workload that uses more than even 64 user extended attributes.
 * You can certainly add a lot more - but you get what you ask for
 * in those circumstances.
 */
#define NFS4_XATTR_HASH_SIZE	64

#define NFSDBG_FACILITY	NFSDBG_XATTRCACHE

struct nfs4_xattr_cache;
struct nfs4_xattr_entry;

struct nfs4_xattr_bucket {
	spinlock_t lock;
	struct hlist_head hlist;
	struct nfs4_xattr_cache *cache;
	bool draining;
};

struct nfs4_xattr_cache {
	struct kref ref;
	struct nfs4_xattr_bucket buckets[NFS4_XATTR_HASH_SIZE];
	struct list_head lru;
	struct list_head dispose;
	atomic_long_t nent;
	spinlock_t listxattr_lock;
	struct inode *inode;
	struct nfs4_xattr_entry *listxattr;
};

struct nfs4_xattr_entry {
	struct kref ref;
	struct hlist_node hnode;
	struct list_head lru;
	struct list_head dispose;
	char *xattr_name;
	void *xattr_value;
	size_t xattr_size;
	struct nfs4_xattr_bucket *bucket;
	uint32_t flags;
};

#define	NFS4_XATTR_ENTRY_EXTVAL	0x0001

/*
 * LRU list of NFS inodes that have xattr caches.
 */
static struct list_lru nfs4_xattr_cache_lru;
static struct list_lru nfs4_xattr_entry_lru;
static struct list_lru nfs4_xattr_large_entry_lru;

static struct kmem_cache *nfs4_xattr_cache_cachep;

/*
 * Hashing helper functions.
 */
static void
nfs4_xattr_hash_init(struct nfs4_xattr_cache *cache)
{
	unsigned int i;

	for (i = 0; i < NFS4_XATTR_HASH_SIZE; i++) {
		INIT_HLIST_HEAD(&cache->buckets[i].hlist);
		spin_lock_init(&cache->buckets[i].lock);
		cache->buckets[i].cache = cache;
		cache->buckets[i].draining = false;
	}
}

/*
 * Locking order:
 * 1. inode i_lock or bucket lock
 * 2. list_lru lock (taken by list_lru_* functions)
 */

/*
 * Wrapper functions to add a cache entry to the right LRU.
 */
static bool
nfs4_xattr_entry_lru_add(struct nfs4_xattr_entry *entry)
{
	struct list_lru *lru;

	lru = (entry->flags & NFS4_XATTR_ENTRY_EXTVAL) ?
	    &nfs4_xattr_large_entry_lru : &nfs4_xattr_entry_lru;

	return list_lru_add(lru, &entry->lru);
}

static bool
nfs4_xattr_entry_lru_del(struct nfs4_xattr_entry *entry)
{
	struct list_lru *lru;

	lru = (entry->flags & NFS4_XATTR_ENTRY_EXTVAL) ?
	    &nfs4_xattr_large_entry_lru : &nfs4_xattr_entry_lru;

	return list_lru_del(lru, &entry->lru);
}

/*
 * This function allocates cache entries. They are the normal
 * extended attribute name/value pairs, but may also be a listxattr
 * cache. Those allocations use the same entry so that they can be
 * treated as one by the memory shrinker.
 *
 * xattr cache entries are allocated together with names. If the
 * value fits in to one page with the entry structure and the name,
 * it will also be part of the same allocation (kmalloc). This is
 * expected to be the vast majority of cases. Larger allocations
 * have a value pointer that is allocated separately by kvmalloc.
 *
 * Parameters:
 *
 * @name:  Name of the extended attribute. NULL for listxattr cache
 *         entry.
 * @value: Value of attribute, or listxattr cache. NULL if the
 *         value is to be copied from pages instead.
 * @pages: Pages to copy the value from, if not NULL. Passed in to
 *	   make it easier to copy the value after an RPC, even if
 *	   the value will not be passed up to application (e.g.
 *	   for a 'query' getxattr with NULL buffer).
 * @len:   Length of the value. Can be 0 for zero-length attribues.
 *         @value and @pages will be NULL if @len is 0.
 */
static struct nfs4_xattr_entry *
nfs4_xattr_alloc_entry(const char *name, const void *value,
		       struct page **pages, size_t len)
{
	struct nfs4_xattr_entry *entry;
	void *valp;
	char *namep;
	size_t alloclen, slen;
	char *buf;
	uint32_t flags;

	BUILD_BUG_ON(sizeof(struct nfs4_xattr_entry) +
	    XATTR_NAME_MAX + 1 > PAGE_SIZE);

	alloclen = sizeof(struct nfs4_xattr_entry);
	if (name != NULL) {
		slen = strlen(name) + 1;
		alloclen += slen;
	} else
		slen = 0;

	if (alloclen + len <= PAGE_SIZE) {
		alloclen += len;
		flags = 0;
	} else {
		flags = NFS4_XATTR_ENTRY_EXTVAL;
	}

	buf = kmalloc(alloclen, GFP_KERNEL_ACCOUNT | GFP_NOFS);
	if (buf == NULL)
		return NULL;
	entry = (struct nfs4_xattr_entry *)buf;

	if (name != NULL) {
		namep = buf + sizeof(struct nfs4_xattr_entry);
		memcpy(namep, name, slen);
	} else {
		namep = NULL;
	}


	if (flags & NFS4_XATTR_ENTRY_EXTVAL) {
		valp = kvmalloc(len, GFP_KERNEL_ACCOUNT | GFP_NOFS);
		if (valp == NULL) {
			kfree(buf);
			return NULL;
		}
	} else if (len != 0) {
		valp = buf + sizeof(struct nfs4_xattr_entry) + slen;
	} else
		valp = NULL;

	if (valp != NULL) {
		if (value != NULL)
			memcpy(valp, value, len);
		else
			_copy_from_pages(valp, pages, 0, len);
	}

	entry->flags = flags;
	entry->xattr_value = valp;
	kref_init(&entry->ref);
	entry->xattr_name = namep;
	entry->xattr_size = len;
	entry->bucket = NULL;
	INIT_LIST_HEAD(&entry->lru);
	INIT_LIST_HEAD(&entry->dispose);
	INIT_HLIST_NODE(&entry->hnode);

	return entry;
}

static void
nfs4_xattr_free_entry(struct nfs4_xattr_entry *entry)
{
	if (entry->flags & NFS4_XATTR_ENTRY_EXTVAL)
		kvfree(entry->xattr_value);
	kfree(entry);
}

static void
nfs4_xattr_free_entry_cb(struct kref *kref)
{
	struct nfs4_xattr_entry *entry;

	entry = container_of(kref, struct nfs4_xattr_entry, ref);

	if (WARN_ON(!list_empty(&entry->lru)))
		return;

	nfs4_xattr_free_entry(entry);
}

static void
nfs4_xattr_free_cache_cb(struct kref *kref)
{
	struct nfs4_xattr_cache *cache;
	int i;

	cache = container_of(kref, struct nfs4_xattr_cache, ref);

	for (i = 0; i < NFS4_XATTR_HASH_SIZE; i++) {
		if (WARN_ON(!hlist_empty(&cache->buckets[i].hlist)))
			return;
		cache->buckets[i].draining = false;
	}

	cache->listxattr = NULL;

	kmem_cache_free(nfs4_xattr_cache_cachep, cache);

}

static struct nfs4_xattr_cache *
nfs4_xattr_alloc_cache(void)
{
	struct nfs4_xattr_cache *cache;

	cache = kmem_cache_alloc(nfs4_xattr_cache_cachep,
	    GFP_KERNEL_ACCOUNT | GFP_NOFS);
	if (cache == NULL)
		return NULL;

	kref_init(&cache->ref);
	atomic_long_set(&cache->nent, 0);

	return cache;
}

/*
 * Set the listxattr cache, which is a special-cased cache entry.
 * The special value ERR_PTR(-ESTALE) is used to indicate that
 * the cache is being drained - this prevents a new listxattr
 * cache from being added to what is now a stale cache.
 */
static int
nfs4_xattr_set_listcache(struct nfs4_xattr_cache *cache,
			 struct nfs4_xattr_entry *new)
{
	struct nfs4_xattr_entry *old;
	int ret = 1;

	spin_lock(&cache->listxattr_lock);

	old = cache->listxattr;

	if (old == ERR_PTR(-ESTALE)) {
		ret = 0;
		goto out;
	}

	cache->listxattr = new;
	if (new != NULL && new != ERR_PTR(-ESTALE))
		nfs4_xattr_entry_lru_add(new);

	if (old != NULL) {
		nfs4_xattr_entry_lru_del(old);
		kref_put(&old->ref, nfs4_xattr_free_entry_cb);
	}
out:
	spin_unlock(&cache->listxattr_lock);

	return ret;
}

/*
 * Unlink a cache from its parent inode, clearing out an invalid
 * cache. Must be called with i_lock held.
 */
static struct nfs4_xattr_cache *
nfs4_xattr_cache_unlink(struct inode *inode)
{
	struct nfs_inode *nfsi;
	struct nfs4_xattr_cache *oldcache;

	nfsi = NFS_I(inode);

	oldcache = nfsi->xattr_cache;
	if (oldcache != NULL) {
		list_lru_del(&nfs4_xattr_cache_lru, &oldcache->lru);
		oldcache->inode = NULL;
	}
	nfsi->xattr_cache = NULL;
	nfsi->cache_validity &= ~NFS_INO_INVALID_XATTR;

	return oldcache;

}

/*
 * Discard a cache. Called by get_cache() if there was an old,
 * invalid cache. Can also be called from a shrinker callback.
 *
 * The cache is dead, it has already been unlinked from its inode,
 * and no longer appears on the cache LRU list.
 *
 * Mark all buckets as draining, so that no new entries are added. This
 * could still happen in the unlikely, but possible case that another
 * thread had grabbed a reference before it was unlinked from the inode,
 * and is still holding it for an add operation.
 *
 * Remove all entries from the LRU lists, so that there is no longer
 * any way to 'find' this cache. Then, remove the entries from the hash
 * table.
 *
 * At that point, the cache will remain empty and can be freed when the final
 * reference drops, which is very likely the kref_put at the end of
 * this function, or the one called immediately afterwards in the
 * shrinker callback.
 */
static void
nfs4_xattr_discard_cache(struct nfs4_xattr_cache *cache)
{
	unsigned int i;
	struct nfs4_xattr_entry *entry;
	struct nfs4_xattr_bucket *bucket;
	struct hlist_node *n;

	nfs4_xattr_set_listcache(cache, ERR_PTR(-ESTALE));

	for (i = 0; i < NFS4_XATTR_HASH_SIZE; i++) {
		bucket = &cache->buckets[i];

		spin_lock(&bucket->lock);
		bucket->draining = true;
		hlist_for_each_entry_safe(entry, n, &bucket->hlist, hnode) {
			nfs4_xattr_entry_lru_del(entry);
			hlist_del_init(&entry->hnode);
			kref_put(&entry->ref, nfs4_xattr_free_entry_cb);
		}
		spin_unlock(&bucket->lock);
	}

	atomic_long_set(&cache->nent, 0);

	kref_put(&cache->ref, nfs4_xattr_free_cache_cb);
}

/*
 * Get a referenced copy of the cache structure. Avoid doing allocs
 * while holding i_lock. Which means that we do some optimistic allocation,
 * and might have to free the result in rare cases.
 *
 * This function only checks the NFS_INO_INVALID_XATTR cache validity bit
 * and acts accordingly, replacing the cache when needed. For the read case
 * (!add), this means that the caller must make sure that the cache
 * is valid before caling this function. getxattr and listxattr call
 * revalidate_inode to do this. The attribute cache timeout (for the
 * non-delegated case) is expected to be dealt with in the revalidate
 * call.
 */

static struct nfs4_xattr_cache *
nfs4_xattr_get_cache(struct inode *inode, int add)
{
	struct nfs_inode *nfsi;
	struct nfs4_xattr_cache *cache, *oldcache, *newcache;

	nfsi = NFS_I(inode);

	cache = oldcache = NULL;

	spin_lock(&inode->i_lock);

	if (nfsi->cache_validity & NFS_INO_INVALID_XATTR)
		oldcache = nfs4_xattr_cache_unlink(inode);
	else
		cache = nfsi->xattr_cache;

	if (cache != NULL)
		kref_get(&cache->ref);

	spin_unlock(&inode->i_lock);

	if (add && cache == NULL) {
		newcache = NULL;

		cache = nfs4_xattr_alloc_cache();
		if (cache == NULL)
			goto out;

		spin_lock(&inode->i_lock);
		if (nfsi->cache_validity & NFS_INO_INVALID_XATTR) {
			/*
			 * The cache was invalidated again. Give up,
			 * since what we want to enter is now likely
			 * outdated anyway.
			 */
			spin_unlock(&inode->i_lock);
			kref_put(&cache->ref, nfs4_xattr_free_cache_cb);
			cache = NULL;
			goto out;
		}

		/*
		 * Check if someone beat us to it.
		 */
		if (nfsi->xattr_cache != NULL) {
			newcache = nfsi->xattr_cache;
			kref_get(&newcache->ref);
		} else {
			kref_get(&cache->ref);
			nfsi->xattr_cache = cache;
			cache->inode = inode;
			list_lru_add(&nfs4_xattr_cache_lru, &cache->lru);
		}

		spin_unlock(&inode->i_lock);

		/*
		 * If there was a race, throw away the cache we just
		 * allocated, and use the new one allocated by someone
		 * else.
		 */
		if (newcache != NULL) {
			kref_put(&cache->ref, nfs4_xattr_free_cache_cb);
			cache = newcache;
		}
	}

out:
	/*
	 * Discard the now orphaned old cache.
	 */
	if (oldcache != NULL)
		nfs4_xattr_discard_cache(oldcache);

	return cache;
}

static inline struct nfs4_xattr_bucket *
nfs4_xattr_hash_bucket(struct nfs4_xattr_cache *cache, const char *name)
{
	return &cache->buckets[jhash(name, strlen(name), 0) &
	    (ARRAY_SIZE(cache->buckets) - 1)];
}

static struct nfs4_xattr_entry *
nfs4_xattr_get_entry(struct nfs4_xattr_bucket *bucket, const char *name)
{
	struct nfs4_xattr_entry *entry;

	entry = NULL;

	hlist_for_each_entry(entry, &bucket->hlist, hnode) {
		if (!strcmp(entry->xattr_name, name))
			break;
	}

	return entry;
}

static int
nfs4_xattr_hash_add(struct nfs4_xattr_cache *cache,
		    struct nfs4_xattr_entry *entry)
{
	struct nfs4_xattr_bucket *bucket;
	struct nfs4_xattr_entry *oldentry = NULL;
	int ret = 1;

	bucket = nfs4_xattr_hash_bucket(cache, entry->xattr_name);
	entry->bucket = bucket;

	spin_lock(&bucket->lock);

	if (bucket->draining) {
		ret = 0;
		goto out;
	}

	oldentry = nfs4_xattr_get_entry(bucket, entry->xattr_name);
	if (oldentry != NULL) {
		hlist_del_init(&oldentry->hnode);
		nfs4_xattr_entry_lru_del(oldentry);
	} else {
		atomic_long_inc(&cache->nent);
	}

	hlist_add_head(&entry->hnode, &bucket->hlist);
	nfs4_xattr_entry_lru_add(entry);

out:
	spin_unlock(&bucket->lock);

	if (oldentry != NULL)
		kref_put(&oldentry->ref, nfs4_xattr_free_entry_cb);

	return ret;
}

static void
nfs4_xattr_hash_remove(struct nfs4_xattr_cache *cache, const char *name)
{
	struct nfs4_xattr_bucket *bucket;
	struct nfs4_xattr_entry *entry;

	bucket = nfs4_xattr_hash_bucket(cache, name);

	spin_lock(&bucket->lock);

	entry = nfs4_xattr_get_entry(bucket, name);
	if (entry != NULL) {
		hlist_del_init(&entry->hnode);
		nfs4_xattr_entry_lru_del(entry);
		atomic_long_dec(&cache->nent);
	}

	spin_unlock(&bucket->lock);

	if (entry != NULL)
		kref_put(&entry->ref, nfs4_xattr_free_entry_cb);
}

static struct nfs4_xattr_entry *
nfs4_xattr_hash_find(struct nfs4_xattr_cache *cache, const char *name)
{
	struct nfs4_xattr_bucket *bucket;
	struct nfs4_xattr_entry *entry;

	bucket = nfs4_xattr_hash_bucket(cache, name);

	spin_lock(&bucket->lock);

	entry = nfs4_xattr_get_entry(bucket, name);
	if (entry != NULL)
		kref_get(&entry->ref);

	spin_unlock(&bucket->lock);

	return entry;
}

/*
 * Entry point to retrieve an entry from the cache.
 */
ssize_t nfs4_xattr_cache_get(struct inode *inode, const char *name, char *buf,
			 ssize_t buflen)
{
	struct nfs4_xattr_cache *cache;
	struct nfs4_xattr_entry *entry;
	ssize_t ret;

	cache = nfs4_xattr_get_cache(inode, 0);
	if (cache == NULL)
		return -ENOENT;

	ret = 0;
	entry = nfs4_xattr_hash_find(cache, name);

	if (entry != NULL) {
		dprintk("%s: cache hit '%s', len %lu\n", __func__,
		    entry->xattr_name, (unsigned long)entry->xattr_size);
		if (buflen == 0) {
			/* Length probe only */
			ret = entry->xattr_size;
		} else if (buflen < entry->xattr_size)
			ret = -ERANGE;
		else {
			memcpy(buf, entry->xattr_value, entry->xattr_size);
			ret = entry->xattr_size;
		}
		kref_put(&entry->ref, nfs4_xattr_free_entry_cb);
	} else {
		dprintk("%s: cache miss '%s'\n", __func__, name);
		ret = -ENOENT;
	}

	kref_put(&cache->ref, nfs4_xattr_free_cache_cb);

	return ret;
}

/*
 * Retrieve a cached list of xattrs from the cache.
 */
ssize_t nfs4_xattr_cache_list(struct inode *inode, char *buf, ssize_t buflen)
{
	struct nfs4_xattr_cache *cache;
	struct nfs4_xattr_entry *entry;
	ssize_t ret;

	cache = nfs4_xattr_get_cache(inode, 0);
	if (cache == NULL)
		return -ENOENT;

	spin_lock(&cache->listxattr_lock);

	entry = cache->listxattr;

	if (entry != NULL && entry != ERR_PTR(-ESTALE)) {
		if (buflen == 0) {
			/* Length probe only */
			ret = entry->xattr_size;
		} else if (entry->xattr_size > buflen)
			ret = -ERANGE;
		else {
			memcpy(buf, entry->xattr_value, entry->xattr_size);
			ret = entry->xattr_size;
		}
	} else {
		ret = -ENOENT;
	}

	spin_unlock(&cache->listxattr_lock);

	kref_put(&cache->ref, nfs4_xattr_free_cache_cb);

	return ret;
}

/*
 * Add an xattr to the cache.
 *
 * This also invalidates the xattr list cache.
 */
void nfs4_xattr_cache_add(struct inode *inode, const char *name,
			  const char *buf, struct page **pages, ssize_t buflen)
{
	struct nfs4_xattr_cache *cache;
	struct nfs4_xattr_entry *entry;

	dprintk("%s: add '%s' len %lu\n", __func__,
	    name, (unsigned long)buflen);

	cache = nfs4_xattr_get_cache(inode, 1);
	if (cache == NULL)
		return;

	entry = nfs4_xattr_alloc_entry(name, buf, pages, buflen);
	if (entry == NULL)
		goto out;

	(void)nfs4_xattr_set_listcache(cache, NULL);

	if (!nfs4_xattr_hash_add(cache, entry))
		kref_put(&entry->ref, nfs4_xattr_free_entry_cb);

out:
	kref_put(&cache->ref, nfs4_xattr_free_cache_cb);
}


/*
 * Remove an xattr from the cache.
 *
 * This also invalidates the xattr list cache.
 */
void nfs4_xattr_cache_remove(struct inode *inode, const char *name)
{
	struct nfs4_xattr_cache *cache;

	dprintk("%s: remove '%s'\n", __func__, name);

	cache = nfs4_xattr_get_cache(inode, 0);
	if (cache == NULL)
		return;

	(void)nfs4_xattr_set_listcache(cache, NULL);
	nfs4_xattr_hash_remove(cache, name);

	kref_put(&cache->ref, nfs4_xattr_free_cache_cb);
}

/*
 * Cache listxattr output, replacing any possible old one.
 */
void nfs4_xattr_cache_set_list(struct inode *inode, const char *buf,
			       ssize_t buflen)
{
	struct nfs4_xattr_cache *cache;
	struct nfs4_xattr_entry *entry;

	cache = nfs4_xattr_get_cache(inode, 1);
	if (cache == NULL)
		return;

	entry = nfs4_xattr_alloc_entry(NULL, buf, NULL, buflen);
	if (entry == NULL)
		goto out;

	/*
	 * This is just there to be able to get to bucket->cache,
	 * which is obviously the same for all buckets, so just
	 * use bucket 0.
	 */
	entry->bucket = &cache->buckets[0];

	if (!nfs4_xattr_set_listcache(cache, entry))
		kref_put(&entry->ref, nfs4_xattr_free_entry_cb);

out:
	kref_put(&cache->ref, nfs4_xattr_free_cache_cb);
}

/*
 * Zap the entire cache. Called when an inode is evicted.
 */
void nfs4_xattr_cache_zap(struct inode *inode)
{
	struct nfs4_xattr_cache *oldcache;

	spin_lock(&inode->i_lock);
	oldcache = nfs4_xattr_cache_unlink(inode);
	spin_unlock(&inode->i_lock);

	if (oldcache)
		nfs4_xattr_discard_cache(oldcache);
}

/*
 * The entry LRU is shrunk more aggressively than the cache LRU,
 * by settings @seeks to 1.
 *
 * Cache structures are freed only when they've become empty, after
 * pruning all but one entry.
 */

static unsigned long nfs4_xattr_cache_count(struct shrinker *shrink,
					    struct shrink_control *sc);
static unsigned long nfs4_xattr_entry_count(struct shrinker *shrink,
					    struct shrink_control *sc);
static unsigned long nfs4_xattr_cache_scan(struct shrinker *shrink,
					   struct shrink_control *sc);
static unsigned long nfs4_xattr_entry_scan(struct shrinker *shrink,
					   struct shrink_control *sc);

static struct shrinker nfs4_xattr_cache_shrinker = {
	.count_objects	= nfs4_xattr_cache_count,
	.scan_objects	= nfs4_xattr_cache_scan,
	.seeks		= DEFAULT_SEEKS,
	.flags		= SHRINKER_MEMCG_AWARE,
};

static struct shrinker nfs4_xattr_entry_shrinker = {
	.count_objects	= nfs4_xattr_entry_count,
	.scan_objects	= nfs4_xattr_entry_scan,
	.seeks		= DEFAULT_SEEKS,
	.batch		= 512,
	.flags		= SHRINKER_MEMCG_AWARE,
};

static struct shrinker nfs4_xattr_large_entry_shrinker = {
	.count_objects	= nfs4_xattr_entry_count,
	.scan_objects	= nfs4_xattr_entry_scan,
	.seeks		= 1,
	.batch		= 512,
	.flags		= SHRINKER_MEMCG_AWARE,
};

static enum lru_status
cache_lru_isolate(struct list_head *item,
	struct list_lru_one *lru, spinlock_t *lru_lock, void *arg)
{
	struct list_head *dispose = arg;
	struct inode *inode;
	struct nfs4_xattr_cache *cache = container_of(item,
	    struct nfs4_xattr_cache, lru);

	if (atomic_long_read(&cache->nent) > 1)
		return LRU_SKIP;

	/*
	 * If a cache structure is on the LRU list, we know that
	 * its inode is valid. Try to lock it to break the link.
	 * Since we're inverting the lock order here, only try.
	 */
	inode = cache->inode;

	if (!spin_trylock(&inode->i_lock))
		return LRU_SKIP;

	kref_get(&cache->ref);

	cache->inode = NULL;
	NFS_I(inode)->xattr_cache = NULL;
	NFS_I(inode)->cache_validity &= ~NFS_INO_INVALID_XATTR;
	list_lru_isolate(lru, &cache->lru);

	spin_unlock(&inode->i_lock);

	list_add_tail(&cache->dispose, dispose);
	return LRU_REMOVED;
}

static unsigned long
nfs4_xattr_cache_scan(struct shrinker *shrink, struct shrink_control *sc)
{
	LIST_HEAD(dispose);
	unsigned long freed;
	struct nfs4_xattr_cache *cache;

	freed = list_lru_shrink_walk(&nfs4_xattr_cache_lru, sc,
	    cache_lru_isolate, &dispose);
	while (!list_empty(&dispose)) {
		cache = list_first_entry(&dispose, struct nfs4_xattr_cache,
		    dispose);
		list_del_init(&cache->dispose);
		nfs4_xattr_discard_cache(cache);
		kref_put(&cache->ref, nfs4_xattr_free_cache_cb);
	}

	return freed;
}


static unsigned long
nfs4_xattr_cache_count(struct shrinker *shrink, struct shrink_control *sc)
{
	unsigned long count;

	count = list_lru_shrink_count(&nfs4_xattr_cache_lru, sc);
	return vfs_pressure_ratio(count);
}

static enum lru_status
entry_lru_isolate(struct list_head *item,
	struct list_lru_one *lru, spinlock_t *lru_lock, void *arg)
{
	struct list_head *dispose = arg;
	struct nfs4_xattr_bucket *bucket;
	struct nfs4_xattr_cache *cache;
	struct nfs4_xattr_entry *entry = container_of(item,
	    struct nfs4_xattr_entry, lru);

	bucket = entry->bucket;
	cache = bucket->cache;

	/*
	 * Unhook the entry from its parent (either a cache bucket
	 * or a cache structure if it's a listxattr buf), so that
	 * it's no longer found. Then add it to the isolate list,
	 * to be freed later.
	 *
	 * In both cases, we're reverting lock order, so use
	 * trylock and skip the entry if we can't get the lock.
	 */
	if (entry->xattr_name != NULL) {
		/* Regular cache entry */
		if (!spin_trylock(&bucket->lock))
			return LRU_SKIP;

		kref_get(&entry->ref);

		hlist_del_init(&entry->hnode);
		atomic_long_dec(&cache->nent);
		list_lru_isolate(lru, &entry->lru);

		spin_unlock(&bucket->lock);
	} else {
		/* Listxattr cache entry */
		if (!spin_trylock(&cache->listxattr_lock))
			return LRU_SKIP;

		kref_get(&entry->ref);

		cache->listxattr = NULL;
		list_lru_isolate(lru, &entry->lru);

		spin_unlock(&cache->listxattr_lock);
	}

	list_add_tail(&entry->dispose, dispose);
	return LRU_REMOVED;
}

static unsigned long
nfs4_xattr_entry_scan(struct shrinker *shrink, struct shrink_control *sc)
{
	LIST_HEAD(dispose);
	unsigned long freed;
	struct nfs4_xattr_entry *entry;
	struct list_lru *lru;

	lru = (shrink == &nfs4_xattr_large_entry_shrinker) ?
	    &nfs4_xattr_large_entry_lru : &nfs4_xattr_entry_lru;

	freed = list_lru_shrink_walk(lru, sc, entry_lru_isolate, &dispose);

	while (!list_empty(&dispose)) {
		entry = list_first_entry(&dispose, struct nfs4_xattr_entry,
		    dispose);
		list_del_init(&entry->dispose);

		/*
		 * Drop two references: the one that we just grabbed
		 * in entry_lru_isolate, and the one that was set
		 * when the entry was first allocated.
		 */
		kref_put(&entry->ref, nfs4_xattr_free_entry_cb);
		kref_put(&entry->ref, nfs4_xattr_free_entry_cb);
	}

	return freed;
}

static unsigned long
nfs4_xattr_entry_count(struct shrinker *shrink, struct shrink_control *sc)
{
	unsigned long count;
	struct list_lru *lru;

	lru = (shrink == &nfs4_xattr_large_entry_shrinker) ?
	    &nfs4_xattr_large_entry_lru : &nfs4_xattr_entry_lru;

	count = list_lru_shrink_count(lru, sc);
	return vfs_pressure_ratio(count);
}


static void nfs4_xattr_cache_init_once(void *p)
{
	struct nfs4_xattr_cache *cache = (struct nfs4_xattr_cache *)p;

	spin_lock_init(&cache->listxattr_lock);
	atomic_long_set(&cache->nent, 0);
	nfs4_xattr_hash_init(cache);
	cache->listxattr = NULL;
	INIT_LIST_HEAD(&cache->lru);
	INIT_LIST_HEAD(&cache->dispose);
}

int __init nfs4_xattr_cache_init(void)
{
	int ret = 0;

	nfs4_xattr_cache_cachep = kmem_cache_create("nfs4_xattr_cache_cache",
	    sizeof(struct nfs4_xattr_cache), 0,
	    (SLAB_RECLAIM_ACCOUNT|SLAB_MEM_SPREAD|SLAB_ACCOUNT),
	    nfs4_xattr_cache_init_once);
	if (nfs4_xattr_cache_cachep == NULL)
		return -ENOMEM;

	ret = list_lru_init_memcg(&nfs4_xattr_large_entry_lru,
	    &nfs4_xattr_large_entry_shrinker);
	if (ret)
		goto out4;

	ret = list_lru_init_memcg(&nfs4_xattr_entry_lru,
	    &nfs4_xattr_entry_shrinker);
	if (ret)
		goto out3;

	ret = list_lru_init_memcg(&nfs4_xattr_cache_lru,
	    &nfs4_xattr_cache_shrinker);
	if (ret)
		goto out2;

	ret = register_shrinker(&nfs4_xattr_cache_shrinker);
	if (ret)
		goto out1;

	ret = register_shrinker(&nfs4_xattr_entry_shrinker);
	if (ret)
		goto out;

	ret = register_shrinker(&nfs4_xattr_large_entry_shrinker);
	if (!ret)
		return 0;

	unregister_shrinker(&nfs4_xattr_entry_shrinker);
out:
	unregister_shrinker(&nfs4_xattr_cache_shrinker);
out1:
	list_lru_destroy(&nfs4_xattr_cache_lru);
out2:
	list_lru_destroy(&nfs4_xattr_entry_lru);
out3:
	list_lru_destroy(&nfs4_xattr_large_entry_lru);
out4:
	kmem_cache_destroy(nfs4_xattr_cache_cachep);

	return ret;
}

void nfs4_xattr_cache_exit(void)
{
	unregister_shrinker(&nfs4_xattr_entry_shrinker);
	unregister_shrinker(&nfs4_xattr_cache_shrinker);
	list_lru_destroy(&nfs4_xattr_entry_lru);
	list_lru_destroy(&nfs4_xattr_cache_lru);
	kmem_cache_destroy(nfs4_xattr_cache_cachep);
}
