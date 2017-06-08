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
 * cleancache_ops is set by cleancache_register_ops to contain the pointers
 * to the cleancache "backend" implementation functions.
 */
static const struct cleancache_ops *cleancache_ops __read_mostly;

/*
 * Counters available via /sys/kernel/debug/cleancache (if debugfs is
 * properly configured.  These are for information only so are not protected
 * against increment races.
 */
static u64 cleancache_succ_gets;
static u64 cleancache_failed_gets;
static u64 cleancache_puts;
static u64 cleancache_invalidates;

static void cleancache_register_ops_sb(struct super_block *sb, void *unused)
{
	switch (sb->cleancache_poolid) {
	case CLEANCACHE_NO_BACKEND:
		__cleancache_init_fs(sb);
		break;
	case CLEANCACHE_NO_BACKEND_SHARED:
		__cleancache_init_shared_fs(sb);
		break;
	}
}

/*
 * Register operations for cleancache. Returns 0 on success.
 */
int cleancache_register_ops(const struct cleancache_ops *ops)
{
	if (cmpxchg(&cleancache_ops, NULL, ops))
		return -EBUSY;

	/*
	 * A cleancache backend can be built as a module and hence loaded after
	 * a cleancache enabled filesystem has called cleancache_init_fs. To
	 * handle such a scenario, here we call ->init_fs or ->init_shared_fs
	 * for each active super block. To differentiate between local and
	 * shared filesystems, we temporarily initialize sb->cleancache_poolid
	 * to CLEANCACHE_NO_BACKEND or CLEANCACHE_NO_BACKEND_SHARED
	 * respectively in case there is no backend registered at the time
	 * cleancache_init_fs or cleancache_init_shared_fs is called.
	 *
	 * Since filesystems can be mounted concurrently with cleancache
	 * backend registration, we have to be careful to guarantee that all
	 * cleancache enabled filesystems that has been mounted by the time
	 * cleancache_register_ops is called has got and all mounted later will
	 * get cleancache_poolid. This is assured by the following statements
	 * tied together:
	 *
	 * a) iterate_supers skips only those super blocks that has started
	 *    ->kill_sb
	 *
	 * b) if iterate_supers encounters a super block that has not finished
	 *    ->mount yet, it waits until it is finished
	 *
	 * c) cleancache_init_fs is called from ->mount and
	 *    cleancache_invalidate_fs is called from ->kill_sb
	 *
	 * d) we call iterate_supers after cleancache_ops has been set
	 *
	 * From a) it follows that if iterate_supers skips a super block, then
	 * either the super block is already dead, in which case we do not need
	 * to bother initializing cleancache for it, or it was mounted after we
	 * initiated iterate_supers. In the latter case, it must have seen
	 * cleancache_ops set according to d) and initialized cleancache from
	 * ->mount by itself according to c). This proves that we call
	 * ->init_fs at least once for each active super block.
	 *
	 * From b) and c) it follows that if iterate_supers encounters a super
	 * block that has already started ->init_fs, it will wait until ->mount
	 * and hence ->init_fs has finished, then check cleancache_poolid, see
	 * that it has already been set and therefore do nothing. This proves
	 * that we call ->init_fs no more than once for each super block.
	 *
	 * Combined together, the last two paragraphs prove the function
	 * correctness.
	 *
	 * Note that various cleancache callbacks may proceed before this
	 * function is called or even concurrently with it, but since
	 * CLEANCACHE_NO_BACKEND is negative, they will all result in a noop
	 * until the corresponding ->init_fs has been actually called and
	 * cleancache_ops has been set.
	 */
	iterate_supers(cleancache_register_ops_sb, NULL);
	return 0;
}
EXPORT_SYMBOL(cleancache_register_ops);

/* Called by a cleancache-enabled filesystem at time of mount */
void __cleancache_init_fs(struct super_block *sb)
{
	int pool_id = CLEANCACHE_NO_BACKEND;

	if (cleancache_ops) {
		pool_id = cleancache_ops->init_fs(PAGE_SIZE);
		if (pool_id < 0)
			pool_id = CLEANCACHE_NO_POOL;
	}
	sb->cleancache_poolid = pool_id;
}
EXPORT_SYMBOL(__cleancache_init_fs);

/* Called by a cleancache-enabled clustered filesystem at time of mount */
void __cleancache_init_shared_fs(struct super_block *sb)
{
	int pool_id = CLEANCACHE_NO_BACKEND_SHARED;

	if (cleancache_ops) {
		pool_id = cleancache_ops->init_shared_fs(sb->s_uuid, PAGE_SIZE);
		if (pool_id < 0)
			pool_id = CLEANCACHE_NO_POOL;
	}
	sb->cleancache_poolid = pool_id;
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
	struct cleancache_filekey key = { .u.key = { 0 } };

	if (!cleancache_ops) {
		cleancache_failed_gets++;
		goto out;
	}

	VM_BUG_ON_PAGE(!PageLocked(page), page);
	pool_id = page->mapping->host->i_sb->cleancache_poolid;
	if (pool_id < 0)
		goto out;

	if (cleancache_get_key(page->mapping->host, &key) < 0)
		goto out;

	ret = cleancache_ops->get_page(pool_id, key, page->index, page);
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
	struct cleancache_filekey key = { .u.key = { 0 } };

	if (!cleancache_ops) {
		cleancache_puts++;
		return;
	}

	VM_BUG_ON_PAGE(!PageLocked(page), page);
	pool_id = page->mapping->host->i_sb->cleancache_poolid;
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
	int pool_id = mapping->host->i_sb->cleancache_poolid;
	struct cleancache_filekey key = { .u.key = { 0 } };

	if (!cleancache_ops)
		return;

	if (pool_id >= 0) {
		VM_BUG_ON_PAGE(!PageLocked(page), page);
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
	int pool_id = mapping->host->i_sb->cleancache_poolid;
	struct cleancache_filekey key = { .u.key = { 0 } };

	if (!cleancache_ops)
		return;

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
	int pool_id;

	pool_id = sb->cleancache_poolid;
	sb->cleancache_poolid = CLEANCACHE_NO_POOL;

	if (cleancache_ops && pool_id >= 0)
		cleancache_ops->invalidate_fs(pool_id);
}
EXPORT_SYMBOL(__cleancache_invalidate_fs);

static int __init init_cleancache(void)
{
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
	return 0;
}
module_init(init_cleancache)
