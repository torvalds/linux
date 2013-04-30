/*
 * Cleancache frontend
 *
 * This code provides the generic "frontend" layer to call a matching
 * "backend" driver implementation of cleancache.  See
 * Documentation/vm/cleancache.txt for more information.
 *
 * Copyright (C) 2009-2010 Oracle Corp. All rights reserved.
 * Author: Dan Magenheimer
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/exportfs.h>
#include <linux/mm.h>
#include <linux/debugfs.h>
#include <linux/cleancache.h>

/*
 * cleancache_ops is set by cleancache_ops_register to contain the pointers
 * to the cleancache "backend" implementation functions.
 */
static struct cleancache_ops *cleancache_ops __read_mostly;

/*
 * Counters available via /sys/kernel/debug/frontswap (if debugfs is
 * properly configured.  These are for information only so are not protected
 * against increment races.
 */
static u64 cleancache_succ_gets;
static u64 cleancache_failed_gets;
static u64 cleancache_puts;
static u64 cleancache_invalidates;

/*
 * When no backend is registered all calls to init_fs and init_shared_fs
 * are registered and fake poolids (FAKE_FS_POOLID_OFFSET or
 * FAKE_SHARED_FS_POOLID_OFFSET, plus offset in the respective array
 * [shared_|]fs_poolid_map) are given to the respective super block
 * (sb->cleancache_poolid) and no tmem_pools are created. When a backend
 * registers with cleancache the previous calls to init_fs and init_shared_fs
 * are executed to create tmem_pools and set the respective poolids. While no
 * backend is registered all "puts", "gets" and "flushes" are ignored or failed.
 */
#define MAX_INITIALIZABLE_FS 32
#define FAKE_FS_POOLID_OFFSET 1000
#define FAKE_SHARED_FS_POOLID_OFFSET 2000

#define FS_NO_BACKEND (-1)
#define FS_UNKNOWN (-2)
static int fs_poolid_map[MAX_INITIALIZABLE_FS];
static int shared_fs_poolid_map[MAX_INITIALIZABLE_FS];
static char *uuids[MAX_INITIALIZABLE_FS];
/*
 * Mutex for the [shared_|]fs_poolid_map to guard against multiple threads
 * invoking umount (and ending in __cleancache_invalidate_fs) and also multiple
 * threads calling mount (and ending up in __cleancache_init_[shared|]fs).
 */
static DEFINE_MUTEX(poolid_mutex);
/*
 * When set to false (default) all calls to the cleancache functions, except
 * the __cleancache_invalidate_fs and __cleancache_init_[shared|]fs are guarded
 * by the if (!cleancache_ops) return. This means multiple threads (from
 * different filesystems) will be checking cleancache_ops. The usage of a
 * bool instead of a atomic_t or a bool guarded by a spinlock is OK - we are
 * OK if the time between the backend's have been initialized (and
 * cleancache_ops has been set to not NULL) and when the filesystems start
 * actually calling the backends. The inverse (when unloading) is obviously
 * not good - but this shim does not do that (yet).
 */

/*
 * The backends and filesystems work all asynchronously. This is b/c the
 * backends can be built as modules.
 * The usual sequence of events is:
 *	a) mount /	-> __cleancache_init_fs is called. We set the
 *		[shared_|]fs_poolid_map and uuids for.
 *
 *	b). user does I/Os -> we call the rest of __cleancache_* functions
 *		which return immediately as cleancache_ops is false.
 *
 *	c). modprobe zcache -> cleancache_register_ops. We init the backend
 *		and set cleancache_ops to true, and for any fs_poolid_map
 *		(which is set by __cleancache_init_fs) we initialize the poolid.
 *
 *	d). user does I/Os -> now that cleancache_ops is true all the
 *		__cleancache_* functions can call the backend. They all check
 *		that fs_poolid_map is valid and if so invoke the backend.
 *
 *	e). umount /	-> __cleancache_invalidate_fs, the fs_poolid_map is
 *		reset (which is the second check in the __cleancache_* ops
 *		to call the backend).
 *
 * The sequence of event could also be c), followed by a), and d). and e). The
 * c) would not happen anymore. There is also the chance of c), and one thread
 * doing a) + d), and another doing e). For that case we depend on the
 * filesystem calling __cleancache_invalidate_fs in the proper sequence (so
 * that it handles all I/Os before it invalidates the fs (which is last part
 * of unmounting process).
 *
 * Note: The acute reader will notice that there is no "rmmod zcache" case.
 * This is b/c the functionality for that is not yet implemented and when
 * done, will require some extra locking not yet devised.
 */

/*
 * Register operations for cleancache, returning previous thus allowing
 * detection of multiple backends and possible nesting.
 */
struct cleancache_ops *cleancache_register_ops(struct cleancache_ops *ops)
{
	struct cleancache_ops *old = cleancache_ops;
	int i;

	mutex_lock(&poolid_mutex);
	for (i = 0; i < MAX_INITIALIZABLE_FS; i++) {
		if (fs_poolid_map[i] == FS_NO_BACKEND)
			fs_poolid_map[i] = ops->init_fs(PAGE_SIZE);
		if (shared_fs_poolid_map[i] == FS_NO_BACKEND)
			shared_fs_poolid_map[i] = ops->init_shared_fs
					(uuids[i], PAGE_SIZE);
	}
	/*
	 * We MUST set cleancache_ops _after_ we have called the backends
	 * init_fs or init_shared_fs functions. Otherwise the compiler might
	 * re-order where cleancache_ops is set in this function.
	 */
	barrier();
	cleancache_ops = ops;
	mutex_unlock(&poolid_mutex);
	return old;
}
EXPORT_SYMBOL(cleancache_register_ops);

/* Called by a cleancache-enabled filesystem at time of mount */
void __cleancache_init_fs(struct super_block *sb)
{
	int i;

	mutex_lock(&poolid_mutex);
	for (i = 0; i < MAX_INITIALIZABLE_FS; i++) {
		if (fs_poolid_map[i] == FS_UNKNOWN) {
			sb->cleancache_poolid = i + FAKE_FS_POOLID_OFFSET;
			if (cleancache_ops)
				fs_poolid_map[i] = cleancache_ops->init_fs(PAGE_SIZE);
			else
				fs_poolid_map[i] = FS_NO_BACKEND;
			break;
		}
	}
	mutex_unlock(&poolid_mutex);
}
EXPORT_SYMBOL(__cleancache_init_fs);

/* Called by a cleancache-enabled clustered filesystem at time of mount */
void __cleancache_init_shared_fs(char *uuid, struct super_block *sb)
{
	int i;

	mutex_lock(&poolid_mutex);
	for (i = 0; i < MAX_INITIALIZABLE_FS; i++) {
		if (shared_fs_poolid_map[i] == FS_UNKNOWN) {
			sb->cleancache_poolid = i + FAKE_SHARED_FS_POOLID_OFFSET;
			uuids[i] = uuid;
			if (cleancache_ops)
				shared_fs_poolid_map[i] = cleancache_ops->init_shared_fs
						(uuid, PAGE_SIZE);
			else
				shared_fs_poolid_map[i] = FS_NO_BACKEND;
			break;
		}
	}
	mutex_unlock(&poolid_mutex);
}
EXPORT_SYMBOL(__cleancache_init_shared_fs);

/*
 * If the filesystem uses exportable filehandles, use the filehandle as
 * the key, else use the inode number.
 */
static int cleancache_get_key(struct inode *inode,
			      struct cleancache_filekey *key)
{
	int (*fhfn)(struct inode *, __u32 *fh, int *, struct inode *);
	int len = 0, maxlen = CLEANCACHE_KEY_MAX;
	struct super_block *sb = inode->i_sb;

	key->u.ino = inode->i_ino;
	if (sb->s_export_op != NULL) {
		fhfn = sb->s_export_op->encode_fh;
		if  (fhfn) {
			len = (*fhfn)(inode, &key->u.fh[0], &maxlen, NULL);
			if (len <= FILEID_ROOT || len == FILEID_INVALID)
				return -1;
			if (maxlen > CLEANCACHE_KEY_MAX)
				return -1;
		}
	}
	return 0;
}

/*
 * Returns a pool_id that is associated with a given fake poolid.
 */
static int get_poolid_from_fake(int fake_pool_id)
{
	if (fake_pool_id >= FAKE_SHARED_FS_POOLID_OFFSET)
		return shared_fs_poolid_map[fake_pool_id -
			FAKE_SHARED_FS_POOLID_OFFSET];
	else if (fake_pool_id >= FAKE_FS_POOLID_OFFSET)
		return fs_poolid_map[fake_pool_id - FAKE_FS_POOLID_OFFSET];
	return FS_NO_BACKEND;
}

/*
 * "Get" data from cleancache associated with the poolid/inode/index
 * that were specified when the data was put to cleanache and, if
 * successful, use it to fill the specified page with data and return 0.
 * The pageframe is unchanged and returns -1 if the get fails.
 * Page must be locked by caller.
 *
 * The function has two checks before any action is taken - whether
 * a backend is registered and whether the sb->cleancache_poolid
 * is correct.
 */
int __cleancache_get_page(struct page *page)
{
	int ret = -1;
	int pool_id;
	int fake_pool_id;
	struct cleancache_filekey key = { .u.key = { 0 } };

	if (!cleancache_ops) {
		cleancache_failed_gets++;
		goto out;
	}

	VM_BUG_ON(!PageLocked(page));
	fake_pool_id = page->mapping->host->i_sb->cleancache_poolid;
	if (fake_pool_id < 0)
		goto out;
	pool_id = get_poolid_from_fake(fake_pool_id);

	if (cleancache_get_key(page->mapping->host, &key) < 0)
		goto out;

	if (pool_id >= 0)
		ret = cleancache_ops->get_page(pool_id,
				key, page->index, page);
	if (ret == 0)
		cleancache_succ_gets++;
	else
		cleancache_failed_gets++;
out:
	return ret;
}
EXPORT_SYMBOL(__cleancache_get_page);

/*
 * "Put" data from a page to cleancache and associate it with the
 * (previously-obtained per-filesystem) poolid and the page's,
 * inode and page index.  Page must be locked.  Note that a put_page
 * always "succeeds", though a subsequent get_page may succeed or fail.
 *
 * The function has two checks before any action is taken - whether
 * a backend is registered and whether the sb->cleancache_poolid
 * is correct.
 */
void __cleancache_put_page(struct page *page)
{
	int pool_id;
	int fake_pool_id;
	struct cleancache_filekey key = { .u.key = { 0 } };

	if (!cleancache_ops) {
		cleancache_puts++;
		return;
	}

	VM_BUG_ON(!PageLocked(page));
	fake_pool_id = page->mapping->host->i_sb->cleancache_poolid;
	if (fake_pool_id < 0)
		return;

	pool_id = get_poolid_from_fake(fake_pool_id);

	if (pool_id >= 0 &&
		cleancache_get_key(page->mapping->host, &key) >= 0) {
		cleancache_ops->put_page(pool_id, key, page->index, page);
		cleancache_puts++;
	}
}
EXPORT_SYMBOL(__cleancache_put_page);

/*
 * Invalidate any data from cleancache associated with the poolid and the
 * page's inode and page index so that a subsequent "get" will fail.
 *
 * The function has two checks before any action is taken - whether
 * a backend is registered and whether the sb->cleancache_poolid
 * is correct.
 */
void __cleancache_invalidate_page(struct address_space *mapping,
					struct page *page)
{
	/* careful... page->mapping is NULL sometimes when this is called */
	int pool_id;
	int fake_pool_id = mapping->host->i_sb->cleancache_poolid;
	struct cleancache_filekey key = { .u.key = { 0 } };

	if (!cleancache_ops)
		return;

	if (fake_pool_id >= 0) {
		pool_id = get_poolid_from_fake(fake_pool_id);
		if (pool_id < 0)
			return;

		VM_BUG_ON(!PageLocked(page));
		if (cleancache_get_key(mapping->host, &key) >= 0) {
			cleancache_ops->invalidate_page(pool_id,
					key, page->index);
			cleancache_invalidates++;
		}
	}
}
EXPORT_SYMBOL(__cleancache_invalidate_page);

/*
 * Invalidate all data from cleancache associated with the poolid and the
 * mappings's inode so that all subsequent gets to this poolid/inode
 * will fail.
 *
 * The function has two checks before any action is taken - whether
 * a backend is registered and whether the sb->cleancache_poolid
 * is correct.
 */
void __cleancache_invalidate_inode(struct address_space *mapping)
{
	int pool_id;
	int fake_pool_id = mapping->host->i_sb->cleancache_poolid;
	struct cleancache_filekey key = { .u.key = { 0 } };

	if (!cleancache_ops)
		return;

	if (fake_pool_id < 0)
		return;

	pool_id = get_poolid_from_fake(fake_pool_id);

	if (pool_id >= 0 && cleancache_get_key(mapping->host, &key) >= 0)
		cleancache_ops->invalidate_inode(pool_id, key);
}
EXPORT_SYMBOL(__cleancache_invalidate_inode);

/*
 * Called by any cleancache-enabled filesystem at time of unmount;
 * note that pool_id is surrendered and may be returned by a subsequent
 * cleancache_init_fs or cleancache_init_shared_fs.
 */
void __cleancache_invalidate_fs(struct super_block *sb)
{
	int index;
	int fake_pool_id = sb->cleancache_poolid;
	int old_poolid = fake_pool_id;

	mutex_lock(&poolid_mutex);
	if (fake_pool_id >= FAKE_SHARED_FS_POOLID_OFFSET) {
		index = fake_pool_id - FAKE_SHARED_FS_POOLID_OFFSET;
		old_poolid = shared_fs_poolid_map[index];
		shared_fs_poolid_map[index] = FS_UNKNOWN;
		uuids[index] = NULL;
	} else if (fake_pool_id >= FAKE_FS_POOLID_OFFSET) {
		index = fake_pool_id - FAKE_FS_POOLID_OFFSET;
		old_poolid = fs_poolid_map[index];
		fs_poolid_map[index] = FS_UNKNOWN;
	}
	sb->cleancache_poolid = -1;
	if (cleancache_ops)
		cleancache_ops->invalidate_fs(old_poolid);
	mutex_unlock(&poolid_mutex);
}
EXPORT_SYMBOL(__cleancache_invalidate_fs);

static int __init init_cleancache(void)
{
	int i;

#ifdef CONFIG_DEBUG_FS
	struct dentry *root = debugfs_create_dir("cleancache", NULL);
	if (root == NULL)
		return -ENXIO;
	debugfs_create_u64("succ_gets", S_IRUGO, root, &cleancache_succ_gets);
	debugfs_create_u64("failed_gets", S_IRUGO,
				root, &cleancache_failed_gets);
	debugfs_create_u64("puts", S_IRUGO, root, &cleancache_puts);
	debugfs_create_u64("invalidates", S_IRUGO,
				root, &cleancache_invalidates);
#endif
	for (i = 0; i < MAX_INITIALIZABLE_FS; i++) {
		fs_poolid_map[i] = FS_UNKNOWN;
		shared_fs_poolid_map[i] = FS_UNKNOWN;
	}
	return 0;
}
module_init(init_cleancache)
