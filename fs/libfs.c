// SPDX-License-Identifier: GPL-2.0-only
/*
 *	fs/libfs.c
 *	Library for filesystems writers.
 */

#include <linux/blkdev.h>
#include <linux/export.h>
#include <linux/pagemap.h>
#include <linux/slab.h>
#include <linux/cred.h>
#include <linux/mount.h>
#include <linux/vfs.h>
#include <linux/quotaops.h>
#include <linux/mutex.h>
#include <linux/namei.h>
#include <linux/exportfs.h>
#include <linux/iversion.h>
#include <linux/writeback.h>
#include <linux/buffer_head.h> /* sync_mapping_buffers */
#include <linux/fs_context.h>
#include <linux/pseudo_fs.h>
#include <linux/fsnotify.h>
#include <linux/unicode.h>
#include <linux/fscrypt.h>
#include <linux/pidfs.h>

#include <linux/uaccess.h>

#include "internal.h"

int simple_getattr(struct mnt_idmap *idmap, const struct path *path,
		   struct kstat *stat, u32 request_mask,
		   unsigned int query_flags)
{
	struct inode *inode = d_inode(path->dentry);
	generic_fillattr(&nop_mnt_idmap, request_mask, inode, stat);
	stat->blocks = inode->i_mapping->nrpages << (PAGE_SHIFT - 9);
	return 0;
}
EXPORT_SYMBOL(simple_getattr);

int simple_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	u64 id = huge_encode_dev(dentry->d_sb->s_dev);

	buf->f_fsid = u64_to_fsid(id);
	buf->f_type = dentry->d_sb->s_magic;
	buf->f_bsize = PAGE_SIZE;
	buf->f_namelen = NAME_MAX;
	return 0;
}
EXPORT_SYMBOL(simple_statfs);

/*
 * Retaining negative dentries for an in-memory filesystem just wastes
 * memory and lookup time: arrange for them to be deleted immediately.
 */
int always_delete_dentry(const struct dentry *dentry)
{
	return 1;
}
EXPORT_SYMBOL(always_delete_dentry);

const struct dentry_operations simple_dentry_operations = {
	.d_delete = always_delete_dentry,
};
EXPORT_SYMBOL(simple_dentry_operations);

/*
 * Lookup the data. This is trivial - if the dentry didn't already
 * exist, we know it is negative.  Set d_op to delete negative dentries.
 */
struct dentry *simple_lookup(struct inode *dir, struct dentry *dentry, unsigned int flags)
{
	if (dentry->d_name.len > NAME_MAX)
		return ERR_PTR(-ENAMETOOLONG);
	if (!dentry->d_sb->s_d_op)
		d_set_d_op(dentry, &simple_dentry_operations);

	if (IS_ENABLED(CONFIG_UNICODE) && IS_CASEFOLDED(dir))
		return NULL;

	d_add(dentry, NULL);
	return NULL;
}
EXPORT_SYMBOL(simple_lookup);

int dcache_dir_open(struct inode *inode, struct file *file)
{
	file->private_data = d_alloc_cursor(file->f_path.dentry);

	return file->private_data ? 0 : -ENOMEM;
}
EXPORT_SYMBOL(dcache_dir_open);

int dcache_dir_close(struct inode *inode, struct file *file)
{
	dput(file->private_data);
	return 0;
}
EXPORT_SYMBOL(dcache_dir_close);

/* parent is locked at least shared */
/*
 * Returns an element of siblings' list.
 * We are looking for <count>th positive after <p>; if
 * found, dentry is grabbed and returned to caller.
 * If no such element exists, NULL is returned.
 */
static struct dentry *scan_positives(struct dentry *cursor,
					struct hlist_node **p,
					loff_t count,
					struct dentry *last)
{
	struct dentry *dentry = cursor->d_parent, *found = NULL;

	spin_lock(&dentry->d_lock);
	while (*p) {
		struct dentry *d = hlist_entry(*p, struct dentry, d_sib);
		p = &d->d_sib.next;
		// we must at least skip cursors, to avoid livelocks
		if (d->d_flags & DCACHE_DENTRY_CURSOR)
			continue;
		if (simple_positive(d) && !--count) {
			spin_lock_nested(&d->d_lock, DENTRY_D_LOCK_NESTED);
			if (simple_positive(d))
				found = dget_dlock(d);
			spin_unlock(&d->d_lock);
			if (likely(found))
				break;
			count = 1;
		}
		if (need_resched()) {
			if (!hlist_unhashed(&cursor->d_sib))
				__hlist_del(&cursor->d_sib);
			hlist_add_behind(&cursor->d_sib, &d->d_sib);
			p = &cursor->d_sib.next;
			spin_unlock(&dentry->d_lock);
			cond_resched();
			spin_lock(&dentry->d_lock);
		}
	}
	spin_unlock(&dentry->d_lock);
	dput(last);
	return found;
}

loff_t dcache_dir_lseek(struct file *file, loff_t offset, int whence)
{
	struct dentry *dentry = file->f_path.dentry;
	switch (whence) {
		case 1:
			offset += file->f_pos;
			fallthrough;
		case 0:
			if (offset >= 0)
				break;
			fallthrough;
		default:
			return -EINVAL;
	}
	if (offset != file->f_pos) {
		struct dentry *cursor = file->private_data;
		struct dentry *to = NULL;

		inode_lock_shared(dentry->d_inode);

		if (offset > 2)
			to = scan_positives(cursor, &dentry->d_children.first,
					    offset - 2, NULL);
		spin_lock(&dentry->d_lock);
		hlist_del_init(&cursor->d_sib);
		if (to)
			hlist_add_behind(&cursor->d_sib, &to->d_sib);
		spin_unlock(&dentry->d_lock);
		dput(to);

		file->f_pos = offset;

		inode_unlock_shared(dentry->d_inode);
	}
	return offset;
}
EXPORT_SYMBOL(dcache_dir_lseek);

/*
 * Directory is locked and all positive dentries in it are safe, since
 * for ramfs-type trees they can't go away without unlink() or rmdir(),
 * both impossible due to the lock on directory.
 */

int dcache_readdir(struct file *file, struct dir_context *ctx)
{
	struct dentry *dentry = file->f_path.dentry;
	struct dentry *cursor = file->private_data;
	struct dentry *next = NULL;
	struct hlist_node **p;

	if (!dir_emit_dots(file, ctx))
		return 0;

	if (ctx->pos == 2)
		p = &dentry->d_children.first;
	else
		p = &cursor->d_sib.next;

	while ((next = scan_positives(cursor, p, 1, next)) != NULL) {
		if (!dir_emit(ctx, next->d_name.name, next->d_name.len,
			      d_inode(next)->i_ino,
			      fs_umode_to_dtype(d_inode(next)->i_mode)))
			break;
		ctx->pos++;
		p = &next->d_sib.next;
	}
	spin_lock(&dentry->d_lock);
	hlist_del_init(&cursor->d_sib);
	if (next)
		hlist_add_before(&cursor->d_sib, &next->d_sib);
	spin_unlock(&dentry->d_lock);
	dput(next);

	return 0;
}
EXPORT_SYMBOL(dcache_readdir);

ssize_t generic_read_dir(struct file *filp, char __user *buf, size_t siz, loff_t *ppos)
{
	return -EISDIR;
}
EXPORT_SYMBOL(generic_read_dir);

const struct file_operations simple_dir_operations = {
	.open		= dcache_dir_open,
	.release	= dcache_dir_close,
	.llseek		= dcache_dir_lseek,
	.read		= generic_read_dir,
	.iterate_shared	= dcache_readdir,
	.fsync		= noop_fsync,
};
EXPORT_SYMBOL(simple_dir_operations);

const struct inode_operations simple_dir_inode_operations = {
	.lookup		= simple_lookup,
};
EXPORT_SYMBOL(simple_dir_inode_operations);

/* simple_offset_add() never assigns these to a dentry */
enum {
	DIR_OFFSET_FIRST	= 2,		/* Find first real entry */
	DIR_OFFSET_EOD		= S32_MAX,
};

/* simple_offset_add() allocation range */
enum {
	DIR_OFFSET_MIN		= DIR_OFFSET_FIRST + 1,
	DIR_OFFSET_MAX		= DIR_OFFSET_EOD - 1,
};

static void offset_set(struct dentry *dentry, long offset)
{
	dentry->d_fsdata = (void *)offset;
}

static long dentry2offset(struct dentry *dentry)
{
	return (long)dentry->d_fsdata;
}

static struct lock_class_key simple_offset_lock_class;

/**
 * simple_offset_init - initialize an offset_ctx
 * @octx: directory offset map to be initialized
 *
 */
void simple_offset_init(struct offset_ctx *octx)
{
	mt_init_flags(&octx->mt, MT_FLAGS_ALLOC_RANGE);
	lockdep_set_class(&octx->mt.ma_lock, &simple_offset_lock_class);
	octx->next_offset = DIR_OFFSET_MIN;
}

/**
 * simple_offset_add - Add an entry to a directory's offset map
 * @octx: directory offset ctx to be updated
 * @dentry: new dentry being added
 *
 * Returns zero on success. @octx and the dentry's offset are updated.
 * Otherwise, a negative errno value is returned.
 */
int simple_offset_add(struct offset_ctx *octx, struct dentry *dentry)
{
	unsigned long offset;
	int ret;

	if (dentry2offset(dentry) != 0)
		return -EBUSY;

	ret = mtree_alloc_cyclic(&octx->mt, &offset, dentry, DIR_OFFSET_MIN,
				 DIR_OFFSET_MAX, &octx->next_offset,
				 GFP_KERNEL);
	if (unlikely(ret < 0))
		return ret == -EBUSY ? -ENOSPC : ret;

	offset_set(dentry, offset);
	return 0;
}

static int simple_offset_replace(struct offset_ctx *octx, struct dentry *dentry,
				 long offset)
{
	int ret;

	ret = mtree_store(&octx->mt, offset, dentry, GFP_KERNEL);
	if (ret)
		return ret;
	offset_set(dentry, offset);
	return 0;
}

/**
 * simple_offset_remove - Remove an entry to a directory's offset map
 * @octx: directory offset ctx to be updated
 * @dentry: dentry being removed
 *
 */
void simple_offset_remove(struct offset_ctx *octx, struct dentry *dentry)
{
	long offset;

	offset = dentry2offset(dentry);
	if (offset == 0)
		return;

	mtree_erase(&octx->mt, offset);
	offset_set(dentry, 0);
}

/**
 * simple_offset_rename - handle directory offsets for rename
 * @old_dir: parent directory of source entry
 * @old_dentry: dentry of source entry
 * @new_dir: parent_directory of destination entry
 * @new_dentry: dentry of destination
 *
 * Caller provides appropriate serialization.
 *
 * User space expects the directory offset value of the replaced
 * (new) directory entry to be unchanged after a rename.
 *
 * Returns zero on success, a negative errno value on failure.
 */
int simple_offset_rename(struct inode *old_dir, struct dentry *old_dentry,
			 struct inode *new_dir, struct dentry *new_dentry)
{
	struct offset_ctx *old_ctx = old_dir->i_op->get_offset_ctx(old_dir);
	struct offset_ctx *new_ctx = new_dir->i_op->get_offset_ctx(new_dir);
	long new_offset = dentry2offset(new_dentry);

	simple_offset_remove(old_ctx, old_dentry);

	if (new_offset) {
		offset_set(new_dentry, 0);
		return simple_offset_replace(new_ctx, old_dentry, new_offset);
	}
	return simple_offset_add(new_ctx, old_dentry);
}

/**
 * simple_offset_rename_exchange - exchange rename with directory offsets
 * @old_dir: parent of dentry being moved
 * @old_dentry: dentry being moved
 * @new_dir: destination parent
 * @new_dentry: destination dentry
 *
 * This API preserves the directory offset values. Caller provides
 * appropriate serialization.
 *
 * Returns zero on success. Otherwise a negative errno is returned and the
 * rename is rolled back.
 */
int simple_offset_rename_exchange(struct inode *old_dir,
				  struct dentry *old_dentry,
				  struct inode *new_dir,
				  struct dentry *new_dentry)
{
	struct offset_ctx *old_ctx = old_dir->i_op->get_offset_ctx(old_dir);
	struct offset_ctx *new_ctx = new_dir->i_op->get_offset_ctx(new_dir);
	long old_index = dentry2offset(old_dentry);
	long new_index = dentry2offset(new_dentry);
	int ret;

	simple_offset_remove(old_ctx, old_dentry);
	simple_offset_remove(new_ctx, new_dentry);

	ret = simple_offset_replace(new_ctx, old_dentry, new_index);
	if (ret)
		goto out_restore;

	ret = simple_offset_replace(old_ctx, new_dentry, old_index);
	if (ret) {
		simple_offset_remove(new_ctx, old_dentry);
		goto out_restore;
	}

	ret = simple_rename_exchange(old_dir, old_dentry, new_dir, new_dentry);
	if (ret) {
		simple_offset_remove(new_ctx, old_dentry);
		simple_offset_remove(old_ctx, new_dentry);
		goto out_restore;
	}
	return 0;

out_restore:
	(void)simple_offset_replace(old_ctx, old_dentry, old_index);
	(void)simple_offset_replace(new_ctx, new_dentry, new_index);
	return ret;
}

/**
 * simple_offset_destroy - Release offset map
 * @octx: directory offset ctx that is about to be destroyed
 *
 * During fs teardown (eg. umount), a directory's offset map might still
 * contain entries. xa_destroy() cleans out anything that remains.
 */
void simple_offset_destroy(struct offset_ctx *octx)
{
	mtree_destroy(&octx->mt);
}

/**
 * offset_dir_llseek - Advance the read position of a directory descriptor
 * @file: an open directory whose position is to be updated
 * @offset: a byte offset
 * @whence: enumerator describing the starting position for this update
 *
 * SEEK_END, SEEK_DATA, and SEEK_HOLE are not supported for directories.
 *
 * Returns the updated read position if successful; otherwise a
 * negative errno is returned and the read position remains unchanged.
 */
static loff_t offset_dir_llseek(struct file *file, loff_t offset, int whence)
{
	switch (whence) {
	case SEEK_CUR:
		offset += file->f_pos;
		fallthrough;
	case SEEK_SET:
		if (offset >= 0)
			break;
		fallthrough;
	default:
		return -EINVAL;
	}

	return vfs_setpos(file, offset, LONG_MAX);
}

static struct dentry *find_positive_dentry(struct dentry *parent,
					   struct dentry *dentry,
					   bool next)
{
	struct dentry *found = NULL;

	spin_lock(&parent->d_lock);
	if (next)
		dentry = d_next_sibling(dentry);
	else if (!dentry)
		dentry = d_first_child(parent);
	hlist_for_each_entry_from(dentry, d_sib) {
		if (!simple_positive(dentry))
			continue;
		spin_lock_nested(&dentry->d_lock, DENTRY_D_LOCK_NESTED);
		if (simple_positive(dentry))
			found = dget_dlock(dentry);
		spin_unlock(&dentry->d_lock);
		if (likely(found))
			break;
	}
	spin_unlock(&parent->d_lock);
	return found;
}

static noinline_for_stack struct dentry *
offset_dir_lookup(struct dentry *parent, loff_t offset)
{
	struct inode *inode = d_inode(parent);
	struct offset_ctx *octx = inode->i_op->get_offset_ctx(inode);
	struct dentry *child, *found = NULL;

	MA_STATE(mas, &octx->mt, offset, offset);

	if (offset == DIR_OFFSET_FIRST)
		found = find_positive_dentry(parent, NULL, false);
	else {
		rcu_read_lock();
		child = mas_find_rev(&mas, DIR_OFFSET_MIN);
		found = find_positive_dentry(parent, child, false);
		rcu_read_unlock();
	}
	return found;
}

static bool offset_dir_emit(struct dir_context *ctx, struct dentry *dentry)
{
	struct inode *inode = d_inode(dentry);

	return dir_emit(ctx, dentry->d_name.name, dentry->d_name.len,
			inode->i_ino, fs_umode_to_dtype(inode->i_mode));
}

static void offset_iterate_dir(struct file *file, struct dir_context *ctx)
{
	struct dentry *dir = file->f_path.dentry;
	struct dentry *dentry;

	dentry = offset_dir_lookup(dir, ctx->pos);
	if (!dentry)
		goto out_eod;
	while (true) {
		struct dentry *next;

		ctx->pos = dentry2offset(dentry);
		if (!offset_dir_emit(ctx, dentry))
			break;

		next = find_positive_dentry(dir, dentry, true);
		dput(dentry);

		if (!next)
			goto out_eod;
		dentry = next;
	}
	dput(dentry);
	return;

out_eod:
	ctx->pos = DIR_OFFSET_EOD;
}

/**
 * offset_readdir - Emit entries starting at offset @ctx->pos
 * @file: an open directory to iterate over
 * @ctx: directory iteration context
 *
 * Caller must hold @file's i_rwsem to prevent insertion or removal of
 * entries during this call.
 *
 * On entry, @ctx->pos contains an offset that represents the first entry
 * to be read from the directory.
 *
 * The operation continues until there are no more entries to read, or
 * until the ctx->actor indicates there is no more space in the caller's
 * output buffer.
 *
 * On return, @ctx->pos contains an offset that will read the next entry
 * in this directory when offset_readdir() is called again with @ctx.
 * Caller places this value in the d_off field of the last entry in the
 * user's buffer.
 *
 * Return values:
 *   %0 - Complete
 */
static int offset_readdir(struct file *file, struct dir_context *ctx)
{
	struct dentry *dir = file->f_path.dentry;

	lockdep_assert_held(&d_inode(dir)->i_rwsem);

	if (!dir_emit_dots(file, ctx))
		return 0;
	if (ctx->pos != DIR_OFFSET_EOD)
		offset_iterate_dir(file, ctx);
	return 0;
}

const struct file_operations simple_offset_dir_operations = {
	.llseek		= offset_dir_llseek,
	.iterate_shared	= offset_readdir,
	.read		= generic_read_dir,
	.fsync		= noop_fsync,
};

static struct dentry *find_next_child(struct dentry *parent, struct dentry *prev)
{
	struct dentry *child = NULL, *d;

	spin_lock(&parent->d_lock);
	d = prev ? d_next_sibling(prev) : d_first_child(parent);
	hlist_for_each_entry_from(d, d_sib) {
		if (simple_positive(d)) {
			spin_lock_nested(&d->d_lock, DENTRY_D_LOCK_NESTED);
			if (simple_positive(d))
				child = dget_dlock(d);
			spin_unlock(&d->d_lock);
			if (likely(child))
				break;
		}
	}
	spin_unlock(&parent->d_lock);
	dput(prev);
	return child;
}

void simple_recursive_removal(struct dentry *dentry,
                              void (*callback)(struct dentry *))
{
	struct dentry *this = dget(dentry);
	while (true) {
		struct dentry *victim = NULL, *child;
		struct inode *inode = this->d_inode;

		inode_lock(inode);
		if (d_is_dir(this))
			inode->i_flags |= S_DEAD;
		while ((child = find_next_child(this, victim)) == NULL) {
			// kill and ascend
			// update metadata while it's still locked
			inode_set_ctime_current(inode);
			clear_nlink(inode);
			inode_unlock(inode);
			victim = this;
			this = this->d_parent;
			inode = this->d_inode;
			inode_lock(inode);
			if (simple_positive(victim)) {
				d_invalidate(victim);	// avoid lost mounts
				if (d_is_dir(victim))
					fsnotify_rmdir(inode, victim);
				else
					fsnotify_unlink(inode, victim);
				if (callback)
					callback(victim);
				dput(victim);		// unpin it
			}
			if (victim == dentry) {
				inode_set_mtime_to_ts(inode,
						      inode_set_ctime_current(inode));
				if (d_is_dir(dentry))
					drop_nlink(inode);
				inode_unlock(inode);
				dput(dentry);
				return;
			}
		}
		inode_unlock(inode);
		this = child;
	}
}
EXPORT_SYMBOL(simple_recursive_removal);

static const struct super_operations simple_super_operations = {
	.statfs		= simple_statfs,
};

static int pseudo_fs_fill_super(struct super_block *s, struct fs_context *fc)
{
	struct pseudo_fs_context *ctx = fc->fs_private;
	struct inode *root;

	s->s_maxbytes = MAX_LFS_FILESIZE;
	s->s_blocksize = PAGE_SIZE;
	s->s_blocksize_bits = PAGE_SHIFT;
	s->s_magic = ctx->magic;
	s->s_op = ctx->ops ?: &simple_super_operations;
	s->s_export_op = ctx->eops;
	s->s_xattr = ctx->xattr;
	s->s_time_gran = 1;
	root = new_inode(s);
	if (!root)
		return -ENOMEM;

	/*
	 * since this is the first inode, make it number 1. New inodes created
	 * after this must take care not to collide with it (by passing
	 * max_reserved of 1 to iunique).
	 */
	root->i_ino = 1;
	root->i_mode = S_IFDIR | S_IRUSR | S_IWUSR;
	simple_inode_init_ts(root);
	s->s_root = d_make_root(root);
	if (!s->s_root)
		return -ENOMEM;
	s->s_d_op = ctx->dops;
	return 0;
}

static int pseudo_fs_get_tree(struct fs_context *fc)
{
	return get_tree_nodev(fc, pseudo_fs_fill_super);
}

static void pseudo_fs_free(struct fs_context *fc)
{
	kfree(fc->fs_private);
}

static const struct fs_context_operations pseudo_fs_context_ops = {
	.free		= pseudo_fs_free,
	.get_tree	= pseudo_fs_get_tree,
};

/*
 * Common helper for pseudo-filesystems (sockfs, pipefs, bdev - stuff that
 * will never be mountable)
 */
struct pseudo_fs_context *init_pseudo(struct fs_context *fc,
					unsigned long magic)
{
	struct pseudo_fs_context *ctx;

	ctx = kzalloc(sizeof(struct pseudo_fs_context), GFP_KERNEL);
	if (likely(ctx)) {
		ctx->magic = magic;
		fc->fs_private = ctx;
		fc->ops = &pseudo_fs_context_ops;
		fc->sb_flags |= SB_NOUSER;
		fc->global = true;
	}
	return ctx;
}
EXPORT_SYMBOL(init_pseudo);

int simple_open(struct inode *inode, struct file *file)
{
	if (inode->i_private)
		file->private_data = inode->i_private;
	return 0;
}
EXPORT_SYMBOL(simple_open);

int simple_link(struct dentry *old_dentry, struct inode *dir, struct dentry *dentry)
{
	struct inode *inode = d_inode(old_dentry);

	inode_set_mtime_to_ts(dir,
			      inode_set_ctime_to_ts(dir, inode_set_ctime_current(inode)));
	inc_nlink(inode);
	ihold(inode);
	dget(dentry);
	d_instantiate(dentry, inode);
	return 0;
}
EXPORT_SYMBOL(simple_link);

int simple_empty(struct dentry *dentry)
{
	struct dentry *child;
	int ret = 0;

	spin_lock(&dentry->d_lock);
	hlist_for_each_entry(child, &dentry->d_children, d_sib) {
		spin_lock_nested(&child->d_lock, DENTRY_D_LOCK_NESTED);
		if (simple_positive(child)) {
			spin_unlock(&child->d_lock);
			goto out;
		}
		spin_unlock(&child->d_lock);
	}
	ret = 1;
out:
	spin_unlock(&dentry->d_lock);
	return ret;
}
EXPORT_SYMBOL(simple_empty);

int simple_unlink(struct inode *dir, struct dentry *dentry)
{
	struct inode *inode = d_inode(dentry);

	inode_set_mtime_to_ts(dir,
			      inode_set_ctime_to_ts(dir, inode_set_ctime_current(inode)));
	drop_nlink(inode);
	dput(dentry);
	return 0;
}
EXPORT_SYMBOL(simple_unlink);

int simple_rmdir(struct inode *dir, struct dentry *dentry)
{
	if (!simple_empty(dentry))
		return -ENOTEMPTY;

	drop_nlink(d_inode(dentry));
	simple_unlink(dir, dentry);
	drop_nlink(dir);
	return 0;
}
EXPORT_SYMBOL(simple_rmdir);

/**
 * simple_rename_timestamp - update the various inode timestamps for rename
 * @old_dir: old parent directory
 * @old_dentry: dentry that is being renamed
 * @new_dir: new parent directory
 * @new_dentry: target for rename
 *
 * POSIX mandates that the old and new parent directories have their ctime and
 * mtime updated, and that inodes of @old_dentry and @new_dentry (if any), have
 * their ctime updated.
 */
void simple_rename_timestamp(struct inode *old_dir, struct dentry *old_dentry,
			     struct inode *new_dir, struct dentry *new_dentry)
{
	struct inode *newino = d_inode(new_dentry);

	inode_set_mtime_to_ts(old_dir, inode_set_ctime_current(old_dir));
	if (new_dir != old_dir)
		inode_set_mtime_to_ts(new_dir,
				      inode_set_ctime_current(new_dir));
	inode_set_ctime_current(d_inode(old_dentry));
	if (newino)
		inode_set_ctime_current(newino);
}
EXPORT_SYMBOL_GPL(simple_rename_timestamp);

int simple_rename_exchange(struct inode *old_dir, struct dentry *old_dentry,
			   struct inode *new_dir, struct dentry *new_dentry)
{
	bool old_is_dir = d_is_dir(old_dentry);
	bool new_is_dir = d_is_dir(new_dentry);

	if (old_dir != new_dir && old_is_dir != new_is_dir) {
		if (old_is_dir) {
			drop_nlink(old_dir);
			inc_nlink(new_dir);
		} else {
			drop_nlink(new_dir);
			inc_nlink(old_dir);
		}
	}
	simple_rename_timestamp(old_dir, old_dentry, new_dir, new_dentry);
	return 0;
}
EXPORT_SYMBOL_GPL(simple_rename_exchange);

int simple_rename(struct mnt_idmap *idmap, struct inode *old_dir,
		  struct dentry *old_dentry, struct inode *new_dir,
		  struct dentry *new_dentry, unsigned int flags)
{
	int they_are_dirs = d_is_dir(old_dentry);

	if (flags & ~(RENAME_NOREPLACE | RENAME_EXCHANGE))
		return -EINVAL;

	if (flags & RENAME_EXCHANGE)
		return simple_rename_exchange(old_dir, old_dentry, new_dir, new_dentry);

	if (!simple_empty(new_dentry))
		return -ENOTEMPTY;

	if (d_really_is_positive(new_dentry)) {
		simple_unlink(new_dir, new_dentry);
		if (they_are_dirs) {
			drop_nlink(d_inode(new_dentry));
			drop_nlink(old_dir);
		}
	} else if (they_are_dirs) {
		drop_nlink(old_dir);
		inc_nlink(new_dir);
	}

	simple_rename_timestamp(old_dir, old_dentry, new_dir, new_dentry);
	return 0;
}
EXPORT_SYMBOL(simple_rename);

/**
 * simple_setattr - setattr for simple filesystem
 * @idmap: idmap of the target mount
 * @dentry: dentry
 * @iattr: iattr structure
 *
 * Returns 0 on success, -error on failure.
 *
 * simple_setattr is a simple ->setattr implementation without a proper
 * implementation of size changes.
 *
 * It can either be used for in-memory filesystems or special files
 * on simple regular filesystems.  Anything that needs to change on-disk
 * or wire state on size changes needs its own setattr method.
 */
int simple_setattr(struct mnt_idmap *idmap, struct dentry *dentry,
		   struct iattr *iattr)
{
	struct inode *inode = d_inode(dentry);
	int error;

	error = setattr_prepare(idmap, dentry, iattr);
	if (error)
		return error;

	if (iattr->ia_valid & ATTR_SIZE)
		truncate_setsize(inode, iattr->ia_size);
	setattr_copy(idmap, inode, iattr);
	mark_inode_dirty(inode);
	return 0;
}
EXPORT_SYMBOL(simple_setattr);

static int simple_read_folio(struct file *file, struct folio *folio)
{
	folio_zero_range(folio, 0, folio_size(folio));
	flush_dcache_folio(folio);
	folio_mark_uptodate(folio);
	folio_unlock(folio);
	return 0;
}

int simple_write_begin(struct file *file, struct address_space *mapping,
			loff_t pos, unsigned len,
			struct folio **foliop, void **fsdata)
{
	struct folio *folio;

	folio = __filemap_get_folio(mapping, pos / PAGE_SIZE, FGP_WRITEBEGIN,
			mapping_gfp_mask(mapping));
	if (IS_ERR(folio))
		return PTR_ERR(folio);

	*foliop = folio;

	if (!folio_test_uptodate(folio) && (len != folio_size(folio))) {
		size_t from = offset_in_folio(folio, pos);

		folio_zero_segments(folio, 0, from,
				from + len, folio_size(folio));
	}
	return 0;
}
EXPORT_SYMBOL(simple_write_begin);

/**
 * simple_write_end - .write_end helper for non-block-device FSes
 * @file: See .write_end of address_space_operations
 * @mapping: 		"
 * @pos: 		"
 * @len: 		"
 * @copied: 		"
 * @folio: 		"
 * @fsdata: 		"
 *
 * simple_write_end does the minimum needed for updating a folio after
 * writing is done. It has the same API signature as the .write_end of
 * address_space_operations vector. So it can just be set onto .write_end for
 * FSes that don't need any other processing. i_mutex is assumed to be held.
 * Block based filesystems should use generic_write_end().
 * NOTE: Even though i_size might get updated by this function, mark_inode_dirty
 * is not called, so a filesystem that actually does store data in .write_inode
 * should extend on what's done here with a call to mark_inode_dirty() in the
 * case that i_size has changed.
 *
 * Use *ONLY* with simple_read_folio()
 */
static int simple_write_end(struct file *file, struct address_space *mapping,
			loff_t pos, unsigned len, unsigned copied,
			struct folio *folio, void *fsdata)
{
	struct inode *inode = folio->mapping->host;
	loff_t last_pos = pos + copied;

	/* zero the stale part of the folio if we did a short copy */
	if (!folio_test_uptodate(folio)) {
		if (copied < len) {
			size_t from = offset_in_folio(folio, pos);

			folio_zero_range(folio, from + copied, len - copied);
		}
		folio_mark_uptodate(folio);
	}
	/*
	 * No need to use i_size_read() here, the i_size
	 * cannot change under us because we hold the i_mutex.
	 */
	if (last_pos > inode->i_size)
		i_size_write(inode, last_pos);

	folio_mark_dirty(folio);
	folio_unlock(folio);
	folio_put(folio);

	return copied;
}

/*
 * Provides ramfs-style behavior: data in the pagecache, but no writeback.
 */
const struct address_space_operations ram_aops = {
	.read_folio	= simple_read_folio,
	.write_begin	= simple_write_begin,
	.write_end	= simple_write_end,
	.dirty_folio	= noop_dirty_folio,
};
EXPORT_SYMBOL(ram_aops);

/*
 * the inodes created here are not hashed. If you use iunique to generate
 * unique inode values later for this filesystem, then you must take care
 * to pass it an appropriate max_reserved value to avoid collisions.
 */
int simple_fill_super(struct super_block *s, unsigned long magic,
		      const struct tree_descr *files)
{
	struct inode *inode;
	struct dentry *dentry;
	int i;

	s->s_blocksize = PAGE_SIZE;
	s->s_blocksize_bits = PAGE_SHIFT;
	s->s_magic = magic;
	s->s_op = &simple_super_operations;
	s->s_time_gran = 1;

	inode = new_inode(s);
	if (!inode)
		return -ENOMEM;
	/*
	 * because the root inode is 1, the files array must not contain an
	 * entry at index 1
	 */
	inode->i_ino = 1;
	inode->i_mode = S_IFDIR | 0755;
	simple_inode_init_ts(inode);
	inode->i_op = &simple_dir_inode_operations;
	inode->i_fop = &simple_dir_operations;
	set_nlink(inode, 2);
	s->s_root = d_make_root(inode);
	if (!s->s_root)
		return -ENOMEM;
	for (i = 0; !files->name || files->name[0]; i++, files++) {
		if (!files->name)
			continue;

		/* warn if it tries to conflict with the root inode */
		if (unlikely(i == 1))
			printk(KERN_WARNING "%s: %s passed in a files array"
				"with an index of 1!\n", __func__,
				s->s_type->name);

		dentry = d_alloc_name(s->s_root, files->name);
		if (!dentry)
			return -ENOMEM;
		inode = new_inode(s);
		if (!inode) {
			dput(dentry);
			return -ENOMEM;
		}
		inode->i_mode = S_IFREG | files->mode;
		simple_inode_init_ts(inode);
		inode->i_fop = files->ops;
		inode->i_ino = i;
		d_add(dentry, inode);
	}
	return 0;
}
EXPORT_SYMBOL(simple_fill_super);

static DEFINE_SPINLOCK(pin_fs_lock);

int simple_pin_fs(struct file_system_type *type, struct vfsmount **mount, int *count)
{
	struct vfsmount *mnt = NULL;
	spin_lock(&pin_fs_lock);
	if (unlikely(!*mount)) {
		spin_unlock(&pin_fs_lock);
		mnt = vfs_kern_mount(type, SB_KERNMOUNT, type->name, NULL);
		if (IS_ERR(mnt))
			return PTR_ERR(mnt);
		spin_lock(&pin_fs_lock);
		if (!*mount)
			*mount = mnt;
	}
	mntget(*mount);
	++*count;
	spin_unlock(&pin_fs_lock);
	mntput(mnt);
	return 0;
}
EXPORT_SYMBOL(simple_pin_fs);

void simple_release_fs(struct vfsmount **mount, int *count)
{
	struct vfsmount *mnt;
	spin_lock(&pin_fs_lock);
	mnt = *mount;
	if (!--*count)
		*mount = NULL;
	spin_unlock(&pin_fs_lock);
	mntput(mnt);
}
EXPORT_SYMBOL(simple_release_fs);

/**
 * simple_read_from_buffer - copy data from the buffer to user space
 * @to: the user space buffer to read to
 * @count: the maximum number of bytes to read
 * @ppos: the current position in the buffer
 * @from: the buffer to read from
 * @available: the size of the buffer
 *
 * The simple_read_from_buffer() function reads up to @count bytes from the
 * buffer @from at offset @ppos into the user space address starting at @to.
 *
 * On success, the number of bytes read is returned and the offset @ppos is
 * advanced by this number, or negative value is returned on error.
 **/
ssize_t simple_read_from_buffer(void __user *to, size_t count, loff_t *ppos,
				const void *from, size_t available)
{
	loff_t pos = *ppos;
	size_t ret;

	if (pos < 0)
		return -EINVAL;
	if (pos >= available || !count)
		return 0;
	if (count > available - pos)
		count = available - pos;
	ret = copy_to_user(to, from + pos, count);
	if (ret == count)
		return -EFAULT;
	count -= ret;
	*ppos = pos + count;
	return count;
}
EXPORT_SYMBOL(simple_read_from_buffer);

/**
 * simple_write_to_buffer - copy data from user space to the buffer
 * @to: the buffer to write to
 * @available: the size of the buffer
 * @ppos: the current position in the buffer
 * @from: the user space buffer to read from
 * @count: the maximum number of bytes to read
 *
 * The simple_write_to_buffer() function reads up to @count bytes from the user
 * space address starting at @from into the buffer @to at offset @ppos.
 *
 * On success, the number of bytes written is returned and the offset @ppos is
 * advanced by this number, or negative value is returned on error.
 **/
ssize_t simple_write_to_buffer(void *to, size_t available, loff_t *ppos,
		const void __user *from, size_t count)
{
	loff_t pos = *ppos;
	size_t res;

	if (pos < 0)
		return -EINVAL;
	if (pos >= available || !count)
		return 0;
	if (count > available - pos)
		count = available - pos;
	res = copy_from_user(to + pos, from, count);
	if (res == count)
		return -EFAULT;
	count -= res;
	*ppos = pos + count;
	return count;
}
EXPORT_SYMBOL(simple_write_to_buffer);

/**
 * memory_read_from_buffer - copy data from the buffer
 * @to: the kernel space buffer to read to
 * @count: the maximum number of bytes to read
 * @ppos: the current position in the buffer
 * @from: the buffer to read from
 * @available: the size of the buffer
 *
 * The memory_read_from_buffer() function reads up to @count bytes from the
 * buffer @from at offset @ppos into the kernel space address starting at @to.
 *
 * On success, the number of bytes read is returned and the offset @ppos is
 * advanced by this number, or negative value is returned on error.
 **/
ssize_t memory_read_from_buffer(void *to, size_t count, loff_t *ppos,
				const void *from, size_t available)
{
	loff_t pos = *ppos;

	if (pos < 0)
		return -EINVAL;
	if (pos >= available)
		return 0;
	if (count > available - pos)
		count = available - pos;
	memcpy(to, from + pos, count);
	*ppos = pos + count;

	return count;
}
EXPORT_SYMBOL(memory_read_from_buffer);

/*
 * Transaction based IO.
 * The file expects a single write which triggers the transaction, and then
 * possibly a read which collects the result - which is stored in a
 * file-local buffer.
 */

void simple_transaction_set(struct file *file, size_t n)
{
	struct simple_transaction_argresp *ar = file->private_data;

	BUG_ON(n > SIMPLE_TRANSACTION_LIMIT);

	/*
	 * The barrier ensures that ar->size will really remain zero until
	 * ar->data is ready for reading.
	 */
	smp_mb();
	ar->size = n;
}
EXPORT_SYMBOL(simple_transaction_set);

char *simple_transaction_get(struct file *file, const char __user *buf, size_t size)
{
	struct simple_transaction_argresp *ar;
	static DEFINE_SPINLOCK(simple_transaction_lock);

	if (size > SIMPLE_TRANSACTION_LIMIT - 1)
		return ERR_PTR(-EFBIG);

	ar = (struct simple_transaction_argresp *)get_zeroed_page(GFP_KERNEL);
	if (!ar)
		return ERR_PTR(-ENOMEM);

	spin_lock(&simple_transaction_lock);

	/* only one write allowed per open */
	if (file->private_data) {
		spin_unlock(&simple_transaction_lock);
		free_page((unsigned long)ar);
		return ERR_PTR(-EBUSY);
	}

	file->private_data = ar;

	spin_unlock(&simple_transaction_lock);

	if (copy_from_user(ar->data, buf, size))
		return ERR_PTR(-EFAULT);

	return ar->data;
}
EXPORT_SYMBOL(simple_transaction_get);

ssize_t simple_transaction_read(struct file *file, char __user *buf, size_t size, loff_t *pos)
{
	struct simple_transaction_argresp *ar = file->private_data;

	if (!ar)
		return 0;
	return simple_read_from_buffer(buf, size, pos, ar->data, ar->size);
}
EXPORT_SYMBOL(simple_transaction_read);

int simple_transaction_release(struct inode *inode, struct file *file)
{
	free_page((unsigned long)file->private_data);
	return 0;
}
EXPORT_SYMBOL(simple_transaction_release);

/* Simple attribute files */

struct simple_attr {
	int (*get)(void *, u64 *);
	int (*set)(void *, u64);
	char get_buf[24];	/* enough to store a u64 and "\n\0" */
	char set_buf[24];
	void *data;
	const char *fmt;	/* format for read operation */
	struct mutex mutex;	/* protects access to these buffers */
};

/* simple_attr_open is called by an actual attribute open file operation
 * to set the attribute specific access operations. */
int simple_attr_open(struct inode *inode, struct file *file,
		     int (*get)(void *, u64 *), int (*set)(void *, u64),
		     const char *fmt)
{
	struct simple_attr *attr;

	attr = kzalloc(sizeof(*attr), GFP_KERNEL);
	if (!attr)
		return -ENOMEM;

	attr->get = get;
	attr->set = set;
	attr->data = inode->i_private;
	attr->fmt = fmt;
	mutex_init(&attr->mutex);

	file->private_data = attr;

	return nonseekable_open(inode, file);
}
EXPORT_SYMBOL_GPL(simple_attr_open);

int simple_attr_release(struct inode *inode, struct file *file)
{
	kfree(file->private_data);
	return 0;
}
EXPORT_SYMBOL_GPL(simple_attr_release);	/* GPL-only?  This?  Really? */

/* read from the buffer that is filled with the get function */
ssize_t simple_attr_read(struct file *file, char __user *buf,
			 size_t len, loff_t *ppos)
{
	struct simple_attr *attr;
	size_t size;
	ssize_t ret;

	attr = file->private_data;

	if (!attr->get)
		return -EACCES;

	ret = mutex_lock_interruptible(&attr->mutex);
	if (ret)
		return ret;

	if (*ppos && attr->get_buf[0]) {
		/* continued read */
		size = strlen(attr->get_buf);
	} else {
		/* first read */
		u64 val;
		ret = attr->get(attr->data, &val);
		if (ret)
			goto out;

		size = scnprintf(attr->get_buf, sizeof(attr->get_buf),
				 attr->fmt, (unsigned long long)val);
	}

	ret = simple_read_from_buffer(buf, len, ppos, attr->get_buf, size);
out:
	mutex_unlock(&attr->mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(simple_attr_read);

/* interpret the buffer as a number to call the set function with */
static ssize_t simple_attr_write_xsigned(struct file *file, const char __user *buf,
			  size_t len, loff_t *ppos, bool is_signed)
{
	struct simple_attr *attr;
	unsigned long long val;
	size_t size;
	ssize_t ret;

	attr = file->private_data;
	if (!attr->set)
		return -EACCES;

	ret = mutex_lock_interruptible(&attr->mutex);
	if (ret)
		return ret;

	ret = -EFAULT;
	size = min(sizeof(attr->set_buf) - 1, len);
	if (copy_from_user(attr->set_buf, buf, size))
		goto out;

	attr->set_buf[size] = '\0';
	if (is_signed)
		ret = kstrtoll(attr->set_buf, 0, &val);
	else
		ret = kstrtoull(attr->set_buf, 0, &val);
	if (ret)
		goto out;
	ret = attr->set(attr->data, val);
	if (ret == 0)
		ret = len; /* on success, claim we got the whole input */
out:
	mutex_unlock(&attr->mutex);
	return ret;
}

ssize_t simple_attr_write(struct file *file, const char __user *buf,
			  size_t len, loff_t *ppos)
{
	return simple_attr_write_xsigned(file, buf, len, ppos, false);
}
EXPORT_SYMBOL_GPL(simple_attr_write);

ssize_t simple_attr_write_signed(struct file *file, const char __user *buf,
			  size_t len, loff_t *ppos)
{
	return simple_attr_write_xsigned(file, buf, len, ppos, true);
}
EXPORT_SYMBOL_GPL(simple_attr_write_signed);

/**
 * generic_encode_ino32_fh - generic export_operations->encode_fh function
 * @inode:   the object to encode
 * @fh:      where to store the file handle fragment
 * @max_len: maximum length to store there (in 4 byte units)
 * @parent:  parent directory inode, if wanted
 *
 * This generic encode_fh function assumes that the 32 inode number
 * is suitable for locating an inode, and that the generation number
 * can be used to check that it is still valid.  It places them in the
 * filehandle fragment where export_decode_fh expects to find them.
 */
int generic_encode_ino32_fh(struct inode *inode, __u32 *fh, int *max_len,
			    struct inode *parent)
{
	struct fid *fid = (void *)fh;
	int len = *max_len;
	int type = FILEID_INO32_GEN;

	if (parent && (len < 4)) {
		*max_len = 4;
		return FILEID_INVALID;
	} else if (len < 2) {
		*max_len = 2;
		return FILEID_INVALID;
	}

	len = 2;
	fid->i32.ino = inode->i_ino;
	fid->i32.gen = inode->i_generation;
	if (parent) {
		fid->i32.parent_ino = parent->i_ino;
		fid->i32.parent_gen = parent->i_generation;
		len = 4;
		type = FILEID_INO32_GEN_PARENT;
	}
	*max_len = len;
	return type;
}
EXPORT_SYMBOL_GPL(generic_encode_ino32_fh);

/**
 * generic_fh_to_dentry - generic helper for the fh_to_dentry export operation
 * @sb:		filesystem to do the file handle conversion on
 * @fid:	file handle to convert
 * @fh_len:	length of the file handle in bytes
 * @fh_type:	type of file handle
 * @get_inode:	filesystem callback to retrieve inode
 *
 * This function decodes @fid as long as it has one of the well-known
 * Linux filehandle types and calls @get_inode on it to retrieve the
 * inode for the object specified in the file handle.
 */
struct dentry *generic_fh_to_dentry(struct super_block *sb, struct fid *fid,
		int fh_len, int fh_type, struct inode *(*get_inode)
			(struct super_block *sb, u64 ino, u32 gen))
{
	struct inode *inode = NULL;

	if (fh_len < 2)
		return NULL;

	switch (fh_type) {
	case FILEID_INO32_GEN:
	case FILEID_INO32_GEN_PARENT:
		inode = get_inode(sb, fid->i32.ino, fid->i32.gen);
		break;
	}

	return d_obtain_alias(inode);
}
EXPORT_SYMBOL_GPL(generic_fh_to_dentry);

/**
 * generic_fh_to_parent - generic helper for the fh_to_parent export operation
 * @sb:		filesystem to do the file handle conversion on
 * @fid:	file handle to convert
 * @fh_len:	length of the file handle in bytes
 * @fh_type:	type of file handle
 * @get_inode:	filesystem callback to retrieve inode
 *
 * This function decodes @fid as long as it has one of the well-known
 * Linux filehandle types and calls @get_inode on it to retrieve the
 * inode for the _parent_ object specified in the file handle if it
 * is specified in the file handle, or NULL otherwise.
 */
struct dentry *generic_fh_to_parent(struct super_block *sb, struct fid *fid,
		int fh_len, int fh_type, struct inode *(*get_inode)
			(struct super_block *sb, u64 ino, u32 gen))
{
	struct inode *inode = NULL;

	if (fh_len <= 2)
		return NULL;

	switch (fh_type) {
	case FILEID_INO32_GEN_PARENT:
		inode = get_inode(sb, fid->i32.parent_ino,
				  (fh_len > 3 ? fid->i32.parent_gen : 0));
		break;
	}

	return d_obtain_alias(inode);
}
EXPORT_SYMBOL_GPL(generic_fh_to_parent);

/**
 * __generic_file_fsync - generic fsync implementation for simple filesystems
 *
 * @file:	file to synchronize
 * @start:	start offset in bytes
 * @end:	end offset in bytes (inclusive)
 * @datasync:	only synchronize essential metadata if true
 *
 * This is a generic implementation of the fsync method for simple
 * filesystems which track all non-inode metadata in the buffers list
 * hanging off the address_space structure.
 */
int __generic_file_fsync(struct file *file, loff_t start, loff_t end,
				 int datasync)
{
	struct inode *inode = file->f_mapping->host;
	int err;
	int ret;

	err = file_write_and_wait_range(file, start, end);
	if (err)
		return err;

	inode_lock(inode);
	ret = sync_mapping_buffers(inode->i_mapping);
	if (!(inode->i_state & I_DIRTY_ALL))
		goto out;
	if (datasync && !(inode->i_state & I_DIRTY_DATASYNC))
		goto out;

	err = sync_inode_metadata(inode, 1);
	if (ret == 0)
		ret = err;

out:
	inode_unlock(inode);
	/* check and advance again to catch errors after syncing out buffers */
	err = file_check_and_advance_wb_err(file);
	if (ret == 0)
		ret = err;
	return ret;
}
EXPORT_SYMBOL(__generic_file_fsync);

/**
 * generic_file_fsync - generic fsync implementation for simple filesystems
 *			with flush
 * @file:	file to synchronize
 * @start:	start offset in bytes
 * @end:	end offset in bytes (inclusive)
 * @datasync:	only synchronize essential metadata if true
 *
 */

int generic_file_fsync(struct file *file, loff_t start, loff_t end,
		       int datasync)
{
	struct inode *inode = file->f_mapping->host;
	int err;

	err = __generic_file_fsync(file, start, end, datasync);
	if (err)
		return err;
	return blkdev_issue_flush(inode->i_sb->s_bdev);
}
EXPORT_SYMBOL(generic_file_fsync);

/**
 * generic_check_addressable - Check addressability of file system
 * @blocksize_bits:	log of file system block size
 * @num_blocks:		number of blocks in file system
 *
 * Determine whether a file system with @num_blocks blocks (and a
 * block size of 2**@blocksize_bits) is addressable by the sector_t
 * and page cache of the system.  Return 0 if so and -EFBIG otherwise.
 */
int generic_check_addressable(unsigned blocksize_bits, u64 num_blocks)
{
	u64 last_fs_block = num_blocks - 1;
	u64 last_fs_page =
		last_fs_block >> (PAGE_SHIFT - blocksize_bits);

	if (unlikely(num_blocks == 0))
		return 0;

	if ((blocksize_bits < 9) || (blocksize_bits > PAGE_SHIFT))
		return -EINVAL;

	if ((last_fs_block > (sector_t)(~0ULL) >> (blocksize_bits - 9)) ||
	    (last_fs_page > (pgoff_t)(~0ULL))) {
		return -EFBIG;
	}
	return 0;
}
EXPORT_SYMBOL(generic_check_addressable);

/*
 * No-op implementation of ->fsync for in-memory filesystems.
 */
int noop_fsync(struct file *file, loff_t start, loff_t end, int datasync)
{
	return 0;
}
EXPORT_SYMBOL(noop_fsync);

ssize_t noop_direct_IO(struct kiocb *iocb, struct iov_iter *iter)
{
	/*
	 * iomap based filesystems support direct I/O without need for
	 * this callback. However, it still needs to be set in
	 * inode->a_ops so that open/fcntl know that direct I/O is
	 * generally supported.
	 */
	return -EINVAL;
}
EXPORT_SYMBOL_GPL(noop_direct_IO);

/* Because kfree isn't assignment-compatible with void(void*) ;-/ */
void kfree_link(void *p)
{
	kfree(p);
}
EXPORT_SYMBOL(kfree_link);

struct inode *alloc_anon_inode(struct super_block *s)
{
	static const struct address_space_operations anon_aops = {
		.dirty_folio	= noop_dirty_folio,
	};
	struct inode *inode = new_inode_pseudo(s);

	if (!inode)
		return ERR_PTR(-ENOMEM);

	inode->i_ino = get_next_ino();
	inode->i_mapping->a_ops = &anon_aops;

	/*
	 * Mark the inode dirty from the very beginning,
	 * that way it will never be moved to the dirty
	 * list because mark_inode_dirty() will think
	 * that it already _is_ on the dirty list.
	 */
	inode->i_state = I_DIRTY;
	inode->i_mode = S_IRUSR | S_IWUSR;
	inode->i_uid = current_fsuid();
	inode->i_gid = current_fsgid();
	inode->i_flags |= S_PRIVATE;
	simple_inode_init_ts(inode);
	return inode;
}
EXPORT_SYMBOL(alloc_anon_inode);

/**
 * simple_nosetlease - generic helper for prohibiting leases
 * @filp: file pointer
 * @arg: type of lease to obtain
 * @flp: new lease supplied for insertion
 * @priv: private data for lm_setup operation
 *
 * Generic helper for filesystems that do not wish to allow leases to be set.
 * All arguments are ignored and it just returns -EINVAL.
 */
int
simple_nosetlease(struct file *filp, int arg, struct file_lease **flp,
		  void **priv)
{
	return -EINVAL;
}
EXPORT_SYMBOL(simple_nosetlease);

/**
 * simple_get_link - generic helper to get the target of "fast" symlinks
 * @dentry: not used here
 * @inode: the symlink inode
 * @done: not used here
 *
 * Generic helper for filesystems to use for symlink inodes where a pointer to
 * the symlink target is stored in ->i_link.  NOTE: this isn't normally called,
 * since as an optimization the path lookup code uses any non-NULL ->i_link
 * directly, without calling ->get_link().  But ->get_link() still must be set,
 * to mark the inode_operations as being for a symlink.
 *
 * Return: the symlink target
 */
const char *simple_get_link(struct dentry *dentry, struct inode *inode,
			    struct delayed_call *done)
{
	return inode->i_link;
}
EXPORT_SYMBOL(simple_get_link);

const struct inode_operations simple_symlink_inode_operations = {
	.get_link = simple_get_link,
};
EXPORT_SYMBOL(simple_symlink_inode_operations);

/*
 * Operations for a permanently empty directory.
 */
static struct dentry *empty_dir_lookup(struct inode *dir, struct dentry *dentry, unsigned int flags)
{
	return ERR_PTR(-ENOENT);
}

static int empty_dir_setattr(struct mnt_idmap *idmap,
			     struct dentry *dentry, struct iattr *attr)
{
	return -EPERM;
}

static ssize_t empty_dir_listxattr(struct dentry *dentry, char *list, size_t size)
{
	return -EOPNOTSUPP;
}

static const struct inode_operations empty_dir_inode_operations = {
	.lookup		= empty_dir_lookup,
	.setattr	= empty_dir_setattr,
	.listxattr	= empty_dir_listxattr,
};

static loff_t empty_dir_llseek(struct file *file, loff_t offset, int whence)
{
	/* An empty directory has two entries . and .. at offsets 0 and 1 */
	return generic_file_llseek_size(file, offset, whence, 2, 2);
}

static int empty_dir_readdir(struct file *file, struct dir_context *ctx)
{
	dir_emit_dots(file, ctx);
	return 0;
}

static const struct file_operations empty_dir_operations = {
	.llseek		= empty_dir_llseek,
	.read		= generic_read_dir,
	.iterate_shared	= empty_dir_readdir,
	.fsync		= noop_fsync,
};


void make_empty_dir_inode(struct inode *inode)
{
	set_nlink(inode, 2);
	inode->i_mode = S_IFDIR | S_IRUGO | S_IXUGO;
	inode->i_uid = GLOBAL_ROOT_UID;
	inode->i_gid = GLOBAL_ROOT_GID;
	inode->i_rdev = 0;
	inode->i_size = 0;
	inode->i_blkbits = PAGE_SHIFT;
	inode->i_blocks = 0;

	inode->i_op = &empty_dir_inode_operations;
	inode->i_opflags &= ~IOP_XATTR;
	inode->i_fop = &empty_dir_operations;
}

bool is_empty_dir_inode(struct inode *inode)
{
	return (inode->i_fop == &empty_dir_operations) &&
		(inode->i_op == &empty_dir_inode_operations);
}

#if IS_ENABLED(CONFIG_UNICODE)
/**
 * generic_ci_d_compare - generic d_compare implementation for casefolding filesystems
 * @dentry:	dentry whose name we are checking against
 * @len:	len of name of dentry
 * @str:	str pointer to name of dentry
 * @name:	Name to compare against
 *
 * Return: 0 if names match, 1 if mismatch, or -ERRNO
 */
int generic_ci_d_compare(const struct dentry *dentry, unsigned int len,
			 const char *str, const struct qstr *name)
{
	const struct dentry *parent;
	const struct inode *dir;
	union shortname_store strbuf;
	struct qstr qstr;

	/*
	 * Attempt a case-sensitive match first. It is cheaper and
	 * should cover most lookups, including all the sane
	 * applications that expect a case-sensitive filesystem.
	 *
	 * This comparison is safe under RCU because the caller
	 * guarantees the consistency between str and len. See
	 * __d_lookup_rcu_op_compare() for details.
	 */
	if (len == name->len && !memcmp(str, name->name, len))
		return 0;

	parent = READ_ONCE(dentry->d_parent);
	dir = READ_ONCE(parent->d_inode);
	if (!dir || !IS_CASEFOLDED(dir))
		return 1;

	qstr.len = len;
	qstr.name = str;
	/*
	 * If the dentry name is stored in-line, then it may be concurrently
	 * modified by a rename.  If this happens, the VFS will eventually retry
	 * the lookup, so it doesn't matter what ->d_compare() returns.
	 * However, it's unsafe to call utf8_strncasecmp() with an unstable
	 * string.  Therefore, we have to copy the name into a temporary buffer.
	 * As above, len is guaranteed to match str, so the shortname case
	 * is exactly when str points to ->d_shortname.
	 */
	if (qstr.name == dentry->d_shortname.string) {
		strbuf = dentry->d_shortname; // NUL is guaranteed to be in there
		qstr.name = strbuf.string;
		/* prevent compiler from optimizing out the temporary buffer */
		barrier();
	}

	return utf8_strncasecmp(dentry->d_sb->s_encoding, name, &qstr);
}
EXPORT_SYMBOL(generic_ci_d_compare);

/**
 * generic_ci_d_hash - generic d_hash implementation for casefolding filesystems
 * @dentry:	dentry of the parent directory
 * @str:	qstr of name whose hash we should fill in
 *
 * Return: 0 if hash was successful or unchanged, and -EINVAL on error
 */
int generic_ci_d_hash(const struct dentry *dentry, struct qstr *str)
{
	const struct inode *dir = READ_ONCE(dentry->d_inode);
	struct super_block *sb = dentry->d_sb;
	const struct unicode_map *um = sb->s_encoding;
	int ret;

	if (!dir || !IS_CASEFOLDED(dir))
		return 0;

	ret = utf8_casefold_hash(um, dentry, str);
	if (ret < 0 && sb_has_strict_encoding(sb))
		return -EINVAL;
	return 0;
}
EXPORT_SYMBOL(generic_ci_d_hash);

static const struct dentry_operations generic_ci_dentry_ops = {
	.d_hash = generic_ci_d_hash,
	.d_compare = generic_ci_d_compare,
#ifdef CONFIG_FS_ENCRYPTION
	.d_revalidate = fscrypt_d_revalidate,
#endif
};

/**
 * generic_ci_match() - Match a name (case-insensitively) with a dirent.
 * This is a filesystem helper for comparison with directory entries.
 * generic_ci_d_compare should be used in VFS' ->d_compare instead.
 *
 * @parent: Inode of the parent of the dirent under comparison
 * @name: name under lookup.
 * @folded_name: Optional pre-folded name under lookup
 * @de_name: Dirent name.
 * @de_name_len: dirent name length.
 *
 * Test whether a case-insensitive directory entry matches the filename
 * being searched.  If @folded_name is provided, it is used instead of
 * recalculating the casefold of @name.
 *
 * Return: > 0 if the directory entry matches, 0 if it doesn't match, or
 * < 0 on error.
 */
int generic_ci_match(const struct inode *parent,
		     const struct qstr *name,
		     const struct qstr *folded_name,
		     const u8 *de_name, u32 de_name_len)
{
	const struct super_block *sb = parent->i_sb;
	const struct unicode_map *um = sb->s_encoding;
	struct fscrypt_str decrypted_name = FSTR_INIT(NULL, de_name_len);
	struct qstr dirent = QSTR_INIT(de_name, de_name_len);
	int res = 0;

	if (IS_ENCRYPTED(parent)) {
		const struct fscrypt_str encrypted_name =
			FSTR_INIT((u8 *) de_name, de_name_len);

		if (WARN_ON_ONCE(!fscrypt_has_encryption_key(parent)))
			return -EINVAL;

		decrypted_name.name = kmalloc(de_name_len, GFP_KERNEL);
		if (!decrypted_name.name)
			return -ENOMEM;
		res = fscrypt_fname_disk_to_usr(parent, 0, 0, &encrypted_name,
						&decrypted_name);
		if (res < 0) {
			kfree(decrypted_name.name);
			return res;
		}
		dirent.name = decrypted_name.name;
		dirent.len = decrypted_name.len;
	}

	/*
	 * Attempt a case-sensitive match first. It is cheaper and
	 * should cover most lookups, including all the sane
	 * applications that expect a case-sensitive filesystem.
	 */

	if (dirent.len == name->len &&
	    !memcmp(name->name, dirent.name, dirent.len))
		goto out;

	if (folded_name->name)
		res = utf8_strncasecmp_folded(um, folded_name, &dirent);
	else
		res = utf8_strncasecmp(um, name, &dirent);

out:
	kfree(decrypted_name.name);
	if (res < 0 && sb_has_strict_encoding(sb)) {
		pr_err_ratelimited("Directory contains filename that is invalid UTF-8");
		return 0;
	}
	return !res;
}
EXPORT_SYMBOL(generic_ci_match);
#endif

#ifdef CONFIG_FS_ENCRYPTION
static const struct dentry_operations generic_encrypted_dentry_ops = {
	.d_revalidate = fscrypt_d_revalidate,
};
#endif

/**
 * generic_set_sb_d_ops - helper for choosing the set of
 * filesystem-wide dentry operations for the enabled features
 * @sb: superblock to be configured
 *
 * Filesystems supporting casefolding and/or fscrypt can call this
 * helper at mount-time to configure sb->s_d_op to best set of dentry
 * operations required for the enabled features. The helper must be
 * called after these have been configured, but before the root dentry
 * is created.
 */
void generic_set_sb_d_ops(struct super_block *sb)
{
#if IS_ENABLED(CONFIG_UNICODE)
	if (sb->s_encoding) {
		sb->s_d_op = &generic_ci_dentry_ops;
		return;
	}
#endif
#ifdef CONFIG_FS_ENCRYPTION
	if (sb->s_cop) {
		sb->s_d_op = &generic_encrypted_dentry_ops;
		return;
	}
#endif
}
EXPORT_SYMBOL(generic_set_sb_d_ops);

/**
 * inode_maybe_inc_iversion - increments i_version
 * @inode: inode with the i_version that should be updated
 * @force: increment the counter even if it's not necessary?
 *
 * Every time the inode is modified, the i_version field must be seen to have
 * changed by any observer.
 *
 * If "force" is set or the QUERIED flag is set, then ensure that we increment
 * the value, and clear the queried flag.
 *
 * In the common case where neither is set, then we can return "false" without
 * updating i_version.
 *
 * If this function returns false, and no other metadata has changed, then we
 * can avoid logging the metadata.
 */
bool inode_maybe_inc_iversion(struct inode *inode, bool force)
{
	u64 cur, new;

	/*
	 * The i_version field is not strictly ordered with any other inode
	 * information, but the legacy inode_inc_iversion code used a spinlock
	 * to serialize increments.
	 *
	 * We add a full memory barrier to ensure that any de facto ordering
	 * with other state is preserved (either implicitly coming from cmpxchg
	 * or explicitly from smp_mb if we don't know upfront if we will execute
	 * the former).
	 *
	 * These barriers pair with inode_query_iversion().
	 */
	cur = inode_peek_iversion_raw(inode);
	if (!force && !(cur & I_VERSION_QUERIED)) {
		smp_mb();
		cur = inode_peek_iversion_raw(inode);
	}

	do {
		/* If flag is clear then we needn't do anything */
		if (!force && !(cur & I_VERSION_QUERIED))
			return false;

		/* Since lowest bit is flag, add 2 to avoid it */
		new = (cur & ~I_VERSION_QUERIED) + I_VERSION_INCREMENT;
	} while (!atomic64_try_cmpxchg(&inode->i_version, &cur, new));
	return true;
}
EXPORT_SYMBOL(inode_maybe_inc_iversion);

/**
 * inode_query_iversion - read i_version for later use
 * @inode: inode from which i_version should be read
 *
 * Read the inode i_version counter. This should be used by callers that wish
 * to store the returned i_version for later comparison. This will guarantee
 * that a later query of the i_version will result in a different value if
 * anything has changed.
 *
 * In this implementation, we fetch the current value, set the QUERIED flag and
 * then try to swap it into place with a cmpxchg, if it wasn't already set. If
 * that fails, we try again with the newly fetched value from the cmpxchg.
 */
u64 inode_query_iversion(struct inode *inode)
{
	u64 cur, new;
	bool fenced = false;

	/*
	 * Memory barriers (implicit in cmpxchg, explicit in smp_mb) pair with
	 * inode_maybe_inc_iversion(), see that routine for more details.
	 */
	cur = inode_peek_iversion_raw(inode);
	do {
		/* If flag is already set, then no need to swap */
		if (cur & I_VERSION_QUERIED) {
			if (!fenced)
				smp_mb();
			break;
		}

		fenced = true;
		new = cur | I_VERSION_QUERIED;
	} while (!atomic64_try_cmpxchg(&inode->i_version, &cur, new));
	return cur >> I_VERSION_QUERIED_SHIFT;
}
EXPORT_SYMBOL(inode_query_iversion);

ssize_t direct_write_fallback(struct kiocb *iocb, struct iov_iter *iter,
		ssize_t direct_written, ssize_t buffered_written)
{
	struct address_space *mapping = iocb->ki_filp->f_mapping;
	loff_t pos = iocb->ki_pos - buffered_written;
	loff_t end = iocb->ki_pos - 1;
	int err;

	/*
	 * If the buffered write fallback returned an error, we want to return
	 * the number of bytes which were written by direct I/O, or the error
	 * code if that was zero.
	 *
	 * Note that this differs from normal direct-io semantics, which will
	 * return -EFOO even if some bytes were written.
	 */
	if (unlikely(buffered_written < 0)) {
		if (direct_written)
			return direct_written;
		return buffered_written;
	}

	/*
	 * We need to ensure that the page cache pages are written to disk and
	 * invalidated to preserve the expected O_DIRECT semantics.
	 */
	err = filemap_write_and_wait_range(mapping, pos, end);
	if (err < 0) {
		/*
		 * We don't know how much we wrote, so just return the number of
		 * bytes which were direct-written
		 */
		iocb->ki_pos -= buffered_written;
		if (direct_written)
			return direct_written;
		return err;
	}
	invalidate_mapping_pages(mapping, pos >> PAGE_SHIFT, end >> PAGE_SHIFT);
	return direct_written + buffered_written;
}
EXPORT_SYMBOL_GPL(direct_write_fallback);

/**
 * simple_inode_init_ts - initialize the timestamps for a new inode
 * @inode: inode to be initialized
 *
 * When a new inode is created, most filesystems set the timestamps to the
 * current time. Add a helper to do this.
 */
struct timespec64 simple_inode_init_ts(struct inode *inode)
{
	struct timespec64 ts = inode_set_ctime_current(inode);

	inode_set_atime_to_ts(inode, ts);
	inode_set_mtime_to_ts(inode, ts);
	return ts;
}
EXPORT_SYMBOL(simple_inode_init_ts);

static inline struct dentry *get_stashed_dentry(struct dentry **stashed)
{
	struct dentry *dentry;

	guard(rcu)();
	dentry = rcu_dereference(*stashed);
	if (!dentry)
		return NULL;
	if (!lockref_get_not_dead(&dentry->d_lockref))
		return NULL;
	return dentry;
}

static struct dentry *prepare_anon_dentry(struct dentry **stashed,
					  struct super_block *sb,
					  void *data)
{
	struct dentry *dentry;
	struct inode *inode;
	const struct stashed_operations *sops = sb->s_fs_info;
	int ret;

	inode = new_inode_pseudo(sb);
	if (!inode) {
		sops->put_data(data);
		return ERR_PTR(-ENOMEM);
	}

	inode->i_flags |= S_IMMUTABLE;
	inode->i_mode = S_IFREG;
	simple_inode_init_ts(inode);

	ret = sops->init_inode(inode, data);
	if (ret < 0) {
		iput(inode);
		return ERR_PTR(ret);
	}

	/* Notice when this is changed. */
	WARN_ON_ONCE(!S_ISREG(inode->i_mode));
	WARN_ON_ONCE(!IS_IMMUTABLE(inode));

	dentry = d_alloc_anon(sb);
	if (!dentry) {
		iput(inode);
		return ERR_PTR(-ENOMEM);
	}

	/* Store address of location where dentry's supposed to be stashed. */
	dentry->d_fsdata = stashed;

	/* @data is now owned by the fs */
	d_instantiate(dentry, inode);
	return dentry;
}

static struct dentry *stash_dentry(struct dentry **stashed,
				   struct dentry *dentry)
{
	guard(rcu)();
	for (;;) {
		struct dentry *old;

		/* Assume any old dentry was cleared out. */
		old = cmpxchg(stashed, NULL, dentry);
		if (likely(!old))
			return dentry;

		/* Check if somebody else installed a reusable dentry. */
		if (lockref_get_not_dead(&old->d_lockref))
			return old;

		/* There's an old dead dentry there, try to take it over. */
		if (likely(try_cmpxchg(stashed, &old, dentry)))
			return dentry;
	}
}

/**
 * path_from_stashed - create path from stashed or new dentry
 * @stashed:    where to retrieve or stash dentry
 * @mnt:        mnt of the filesystems to use
 * @data:       data to store in inode->i_private
 * @path:       path to create
 *
 * The function tries to retrieve a stashed dentry from @stashed. If the dentry
 * is still valid then it will be reused. If the dentry isn't able the function
 * will allocate a new dentry and inode. It will then check again whether it
 * can reuse an existing dentry in case one has been added in the meantime or
 * update @stashed with the newly added dentry.
 *
 * Special-purpose helper for nsfs and pidfs.
 *
 * Return: On success zero and on failure a negative error is returned.
 */
int path_from_stashed(struct dentry **stashed, struct vfsmount *mnt, void *data,
		      struct path *path)
{
	struct dentry *dentry;
	const struct stashed_operations *sops = mnt->mnt_sb->s_fs_info;

	/* See if dentry can be reused. */
	path->dentry = get_stashed_dentry(stashed);
	if (path->dentry) {
		sops->put_data(data);
		goto out_path;
	}

	/* Allocate a new dentry. */
	dentry = prepare_anon_dentry(stashed, mnt->mnt_sb, data);
	if (IS_ERR(dentry))
		return PTR_ERR(dentry);

	/* Added a new dentry. @data is now owned by the filesystem. */
	path->dentry = stash_dentry(stashed, dentry);
	if (path->dentry != dentry)
		dput(dentry);

out_path:
	WARN_ON_ONCE(path->dentry->d_fsdata != stashed);
	WARN_ON_ONCE(d_inode(path->dentry)->i_private != data);
	path->mnt = mntget(mnt);
	return 0;
}

void stashed_dentry_prune(struct dentry *dentry)
{
	struct dentry **stashed = dentry->d_fsdata;
	struct inode *inode = d_inode(dentry);

	if (WARN_ON_ONCE(!stashed))
		return;

	if (!inode)
		return;

	/*
	 * Only replace our own @dentry as someone else might've
	 * already cleared out @dentry and stashed their own
	 * dentry in there.
	 */
	cmpxchg(stashed, dentry, NULL);
}
