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
#include <linux/writeback.h>
#include <linux/buffer_head.h> /* sync_mapping_buffers */
#include <linux/fs_context.h>
#include <linux/pseudo_fs.h>
#include <linux/fsnotify.h>
#include <linux/unicode.h>
#include <linux/fscrypt.h>

#include <linux/uaccess.h>

#include "internal.h"

int simple_getattr(const struct path *path, struct kstat *stat,
		   u32 request_mask, unsigned int query_flags)
{
	struct inode *inode = d_inode(path->dentry);
	generic_fillattr(inode, stat);
	stat->blocks = inode->i_mapping->nrpages << (PAGE_SHIFT - 9);
	return 0;
}
EXPORT_SYMBOL(simple_getattr);

int simple_statfs(struct dentry *dentry, struct kstatfs *buf)
{
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
					struct list_head *p,
					loff_t count,
					struct dentry *last)
{
	struct dentry *dentry = cursor->d_parent, *found = NULL;

	spin_lock(&dentry->d_lock);
	while ((p = p->next) != &dentry->d_subdirs) {
		struct dentry *d = list_entry(p, struct dentry, d_child);
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
			list_move(&cursor->d_child, p);
			p = &cursor->d_child;
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
			to = scan_positives(cursor, &dentry->d_subdirs,
					    offset - 2, NULL);
		spin_lock(&dentry->d_lock);
		if (to)
			list_move(&cursor->d_child, &to->d_child);
		else
			list_del_init(&cursor->d_child);
		spin_unlock(&dentry->d_lock);
		dput(to);

		file->f_pos = offset;

		inode_unlock_shared(dentry->d_inode);
	}
	return offset;
}
EXPORT_SYMBOL(dcache_dir_lseek);

/* Relationship between i_mode and the DT_xxx types */
static inline unsigned char dt_type(struct inode *inode)
{
	return (inode->i_mode >> 12) & 15;
}

/*
 * Directory is locked and all positive dentries in it are safe, since
 * for ramfs-type trees they can't go away without unlink() or rmdir(),
 * both impossible due to the lock on directory.
 */

int dcache_readdir(struct file *file, struct dir_context *ctx)
{
	struct dentry *dentry = file->f_path.dentry;
	struct dentry *cursor = file->private_data;
	struct list_head *anchor = &dentry->d_subdirs;
	struct dentry *next = NULL;
	struct list_head *p;

	if (!dir_emit_dots(file, ctx))
		return 0;

	if (ctx->pos == 2)
		p = anchor;
	else if (!list_empty(&cursor->d_child))
		p = &cursor->d_child;
	else
		return 0;

	while ((next = scan_positives(cursor, p, 1, next)) != NULL) {
		if (!dir_emit(ctx, next->d_name.name, next->d_name.len,
			      d_inode(next)->i_ino, dt_type(d_inode(next))))
			break;
		ctx->pos++;
		p = &next->d_child;
	}
	spin_lock(&dentry->d_lock);
	if (next)
		list_move_tail(&cursor->d_child, &next->d_child);
	else
		list_del_init(&cursor->d_child);
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

static struct dentry *find_next_child(struct dentry *parent, struct dentry *prev)
{
	struct dentry *child = NULL;
	struct list_head *p = prev ? &prev->d_child : &parent->d_subdirs;

	spin_lock(&parent->d_lock);
	while ((p = p->next) != &parent->d_subdirs) {
		struct dentry *d = container_of(p, struct dentry, d_child);
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
			inode->i_ctime = current_time(inode);
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
				inode->i_ctime = inode->i_mtime =
					current_time(inode);
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
	root->i_atime = root->i_mtime = root->i_ctime = current_time(root);
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

	inode->i_ctime = dir->i_ctime = dir->i_mtime = current_time(inode);
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
	list_for_each_entry(child, &dentry->d_subdirs, d_child) {
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

	inode->i_ctime = dir->i_ctime = dir->i_mtime = current_time(inode);
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

int simple_rename(struct inode *old_dir, struct dentry *old_dentry,
		  struct inode *new_dir, struct dentry *new_dentry,
		  unsigned int flags)
{
	struct inode *inode = d_inode(old_dentry);
	int they_are_dirs = d_is_dir(old_dentry);

	if (flags & ~RENAME_NOREPLACE)
		return -EINVAL;

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

	old_dir->i_ctime = old_dir->i_mtime = new_dir->i_ctime =
		new_dir->i_mtime = inode->i_ctime = current_time(old_dir);

	return 0;
}
EXPORT_SYMBOL(simple_rename);

/**
 * simple_setattr - setattr for simple filesystem
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
int simple_setattr(struct dentry *dentry, struct iattr *iattr)
{
	struct inode *inode = d_inode(dentry);
	int error;

	error = setattr_prepare(dentry, iattr);
	if (error)
		return error;

	if (iattr->ia_valid & ATTR_SIZE)
		truncate_setsize(inode, iattr->ia_size);
	setattr_copy(inode, iattr);
	mark_inode_dirty(inode);
	return 0;
}
EXPORT_SYMBOL(simple_setattr);

int simple_readpage(struct file *file, struct page *page)
{
	clear_highpage(page);
	flush_dcache_page(page);
	SetPageUptodate(page);
	unlock_page(page);
	return 0;
}
EXPORT_SYMBOL(simple_readpage);

int simple_write_begin(struct file *file, struct address_space *mapping,
			loff_t pos, unsigned len, unsigned flags,
			struct page **pagep, void **fsdata)
{
	struct page *page;
	pgoff_t index;

	index = pos >> PAGE_SHIFT;

	page = grab_cache_page_write_begin(mapping, index, flags);
	if (!page)
		return -ENOMEM;

	*pagep = page;

	if (!PageUptodate(page) && (len != PAGE_SIZE)) {
		unsigned from = pos & (PAGE_SIZE - 1);

		zero_user_segments(page, 0, from, from + len, PAGE_SIZE);
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
 * @page: 		"
 * @fsdata: 		"
 *
 * simple_write_end does the minimum needed for updating a page after writing is
 * done. It has the same API signature as the .write_end of
 * address_space_operations vector. So it can just be set onto .write_end for
 * FSes that don't need any other processing. i_mutex is assumed to be held.
 * Block based filesystems should use generic_write_end().
 * NOTE: Even though i_size might get updated by this function, mark_inode_dirty
 * is not called, so a filesystem that actually does store data in .write_inode
 * should extend on what's done here with a call to mark_inode_dirty() in the
 * case that i_size has changed.
 *
 * Use *ONLY* with simple_readpage()
 */
int simple_write_end(struct file *file, struct address_space *mapping,
			loff_t pos, unsigned len, unsigned copied,
			struct page *page, void *fsdata)
{
	struct inode *inode = page->mapping->host;
	loff_t last_pos = pos + copied;

	/* zero the stale part of the page if we did a short copy */
	if (!PageUptodate(page)) {
		if (copied < len) {
			unsigned from = pos & (PAGE_SIZE - 1);

			zero_user(page, from + copied, len - copied);
		}
		SetPageUptodate(page);
	}
	/*
	 * No need to use i_size_read() here, the i_size
	 * cannot change under us because we hold the i_mutex.
	 */
	if (last_pos > inode->i_size)
		i_size_write(inode, last_pos);

	set_page_dirty(page);
	unlock_page(page);
	put_page(page);

	return copied;
}
EXPORT_SYMBOL(simple_write_end);

/*
 * the inodes created here are not hashed. If you use iunique to generate
 * unique inode values later for this filesystem, then you must take care
 * to pass it an appropriate max_reserved value to avoid collisions.
 */
int simple_fill_super(struct super_block *s, unsigned long magic,
		      const struct tree_descr *files)
{
	struct inode *inode;
	struct dentry *root;
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
	inode->i_atime = inode->i_mtime = inode->i_ctime = current_time(inode);
	inode->i_op = &simple_dir_inode_operations;
	inode->i_fop = &simple_dir_operations;
	set_nlink(inode, 2);
	root = d_make_root(inode);
	if (!root)
		return -ENOMEM;
	for (i = 0; !files->name || files->name[0]; i++, files++) {
		if (!files->name)
			continue;

		/* warn if it tries to conflict with the root inode */
		if (unlikely(i == 1))
			printk(KERN_WARNING "%s: %s passed in a files array"
				"with an index of 1!\n", __func__,
				s->s_type->name);

		dentry = d_alloc_name(root, files->name);
		if (!dentry)
			goto out;
		inode = new_inode(s);
		if (!inode) {
			dput(dentry);
			goto out;
		}
		inode->i_mode = S_IFREG | files->mode;
		inode->i_atime = inode->i_mtime = inode->i_ctime = current_time(inode);
		inode->i_fop = files->ops;
		inode->i_ino = i;
		d_add(dentry, inode);
	}
	s->s_root = root;
	return 0;
out:
	d_genocide(root);
	shrink_dcache_parent(root);
	dput(root);
	return -ENOMEM;
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
ssize_t simple_attr_write(struct file *file, const char __user *buf,
			  size_t len, loff_t *ppos)
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
EXPORT_SYMBOL_GPL(simple_attr_write);

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
	return blkdev_issue_flush(inode->i_sb->s_bdev, GFP_KERNEL);
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

int noop_set_page_dirty(struct page *page)
{
	/*
	 * Unlike __set_page_dirty_no_writeback that handles dirty page
	 * tracking in the page object, dax does all dirty tracking in
	 * the inode address_space in response to mkwrite faults. In the
	 * dax case we only need to worry about potentially dirty CPU
	 * caches, not dirty page cache pages to write back.
	 *
	 * This callback is defined to prevent fallback to
	 * __set_page_dirty_buffers() in set_page_dirty().
	 */
	return 0;
}
EXPORT_SYMBOL_GPL(noop_set_page_dirty);

void noop_invalidatepage(struct page *page, unsigned int offset,
		unsigned int length)
{
	/*
	 * There is no page cache to invalidate in the dax case, however
	 * we need this callback defined to prevent falling back to
	 * block_invalidatepage() in do_invalidatepage().
	 */
}
EXPORT_SYMBOL_GPL(noop_invalidatepage);

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

/*
 * nop .set_page_dirty method so that people can use .page_mkwrite on
 * anon inodes.
 */
static int anon_set_page_dirty(struct page *page)
{
	return 0;
};

/*
 * A single inode exists for all anon_inode files. Contrary to pipes,
 * anon_inode inodes have no associated per-instance data, so we need
 * only allocate one of them.
 */
struct inode *alloc_anon_inode(struct super_block *s)
{
	static const struct address_space_operations anon_aops = {
		.set_page_dirty = anon_set_page_dirty,
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
	inode->i_atime = inode->i_mtime = inode->i_ctime = current_time(inode);
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
simple_nosetlease(struct file *filp, long arg, struct file_lock **flp,
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

static int empty_dir_getattr(const struct path *path, struct kstat *stat,
			     u32 request_mask, unsigned int query_flags)
{
	struct inode *inode = d_inode(path->dentry);
	generic_fillattr(inode, stat);
	return 0;
}

static int empty_dir_setattr(struct dentry *dentry, struct iattr *attr)
{
	return -EPERM;
}

static ssize_t empty_dir_listxattr(struct dentry *dentry, char *list, size_t size)
{
	return -EOPNOTSUPP;
}

static const struct inode_operations empty_dir_inode_operations = {
	.lookup		= empty_dir_lookup,
	.permission	= generic_permission,
	.setattr	= empty_dir_setattr,
	.getattr	= empty_dir_getattr,
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

#ifdef CONFIG_UNICODE
/*
 * Determine if the name of a dentry should be casefolded.
 *
 * Return: if names will need casefolding
 */
static bool needs_casefold(const struct inode *dir)
{
	return IS_CASEFOLDED(dir) && dir->i_sb->s_encoding;
}

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
	const struct dentry *parent = READ_ONCE(dentry->d_parent);
	const struct inode *dir = READ_ONCE(parent->d_inode);
	const struct super_block *sb = dentry->d_sb;
	const struct unicode_map *um = sb->s_encoding;
	struct qstr qstr = QSTR_INIT(str, len);
	char strbuf[DNAME_INLINE_LEN];
	int ret;

	if (!dir || !needs_casefold(dir))
		goto fallback;
	/*
	 * If the dentry name is stored in-line, then it may be concurrently
	 * modified by a rename.  If this happens, the VFS will eventually retry
	 * the lookup, so it doesn't matter what ->d_compare() returns.
	 * However, it's unsafe to call utf8_strncasecmp() with an unstable
	 * string.  Therefore, we have to copy the name into a temporary buffer.
	 */
	if (len <= DNAME_INLINE_LEN - 1) {
		memcpy(strbuf, str, len);
		strbuf[len] = 0;
		qstr.name = strbuf;
		/* prevent compiler from optimizing out the temporary buffer */
		barrier();
	}
	ret = utf8_strncasecmp(um, name, &qstr);
	if (ret >= 0)
		return ret;

	if (sb_has_strict_encoding(sb))
		return -EINVAL;
fallback:
	if (len != name->len)
		return 1;
	return !!memcmp(str, name->name, len);
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
	int ret = 0;

	if (!dir || !needs_casefold(dir))
		return 0;

	ret = utf8_casefold_hash(um, dentry, str);
	if (ret < 0 && sb_has_strict_encoding(sb))
		return -EINVAL;
	return 0;
}
EXPORT_SYMBOL(generic_ci_d_hash);
#endif
