/*
 *	fs/libfs.c
 *	Library for filesystems writers.
 */

#include <linux/module.h>
#include <linux/pagemap.h>
#include <linux/slab.h>
#include <linux/mount.h>
#include <linux/vfs.h>
#include <linux/quotaops.h>
#include <linux/mutex.h>
#include <linux/exportfs.h>
#include <linux/writeback.h>
#include <linux/buffer_head.h>

#include <asm/uaccess.h>

int simple_getattr(struct vfsmount *mnt, struct dentry *dentry,
		   struct kstat *stat)
{
	struct inode *inode = dentry->d_inode;
	generic_fillattr(inode, stat);
	stat->blocks = inode->i_mapping->nrpages << (PAGE_CACHE_SHIFT - 9);
	return 0;
}

int simple_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	buf->f_type = dentry->d_sb->s_magic;
	buf->f_bsize = PAGE_CACHE_SIZE;
	buf->f_namelen = NAME_MAX;
	return 0;
}

/*
 * Retaining negative dentries for an in-memory filesystem just wastes
 * memory and lookup time: arrange for them to be deleted immediately.
 */
static int simple_delete_dentry(struct dentry *dentry)
{
	return 1;
}

/*
 * Lookup the data. This is trivial - if the dentry didn't already
 * exist, we know it is negative.  Set d_op to delete negative dentries.
 */
struct dentry *simple_lookup(struct inode *dir, struct dentry *dentry, struct nameidata *nd)
{
	static const struct dentry_operations simple_dentry_operations = {
		.d_delete = simple_delete_dentry,
	};

	if (dentry->d_name.len > NAME_MAX)
		return ERR_PTR(-ENAMETOOLONG);
	dentry->d_op = &simple_dentry_operations;
	d_add(dentry, NULL);
	return NULL;
}

int dcache_dir_open(struct inode *inode, struct file *file)
{
	static struct qstr cursor_name = {.len = 1, .name = "."};

	file->private_data = d_alloc(file->f_path.dentry, &cursor_name);

	return file->private_data ? 0 : -ENOMEM;
}

int dcache_dir_close(struct inode *inode, struct file *file)
{
	dput(file->private_data);
	return 0;
}

loff_t dcache_dir_lseek(struct file *file, loff_t offset, int origin)
{
	mutex_lock(&file->f_path.dentry->d_inode->i_mutex);
	switch (origin) {
		case 1:
			offset += file->f_pos;
		case 0:
			if (offset >= 0)
				break;
		default:
			mutex_unlock(&file->f_path.dentry->d_inode->i_mutex);
			return -EINVAL;
	}
	if (offset != file->f_pos) {
		file->f_pos = offset;
		if (file->f_pos >= 2) {
			struct list_head *p;
			struct dentry *cursor = file->private_data;
			loff_t n = file->f_pos - 2;

			spin_lock(&dcache_lock);
			list_del(&cursor->d_u.d_child);
			p = file->f_path.dentry->d_subdirs.next;
			while (n && p != &file->f_path.dentry->d_subdirs) {
				struct dentry *next;
				next = list_entry(p, struct dentry, d_u.d_child);
				if (!d_unhashed(next) && next->d_inode)
					n--;
				p = p->next;
			}
			list_add_tail(&cursor->d_u.d_child, p);
			spin_unlock(&dcache_lock);
		}
	}
	mutex_unlock(&file->f_path.dentry->d_inode->i_mutex);
	return offset;
}

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

int dcache_readdir(struct file * filp, void * dirent, filldir_t filldir)
{
	struct dentry *dentry = filp->f_path.dentry;
	struct dentry *cursor = filp->private_data;
	struct list_head *p, *q = &cursor->d_u.d_child;
	ino_t ino;
	int i = filp->f_pos;

	switch (i) {
		case 0:
			ino = dentry->d_inode->i_ino;
			if (filldir(dirent, ".", 1, i, ino, DT_DIR) < 0)
				break;
			filp->f_pos++;
			i++;
			/* fallthrough */
		case 1:
			ino = parent_ino(dentry);
			if (filldir(dirent, "..", 2, i, ino, DT_DIR) < 0)
				break;
			filp->f_pos++;
			i++;
			/* fallthrough */
		default:
			spin_lock(&dcache_lock);
			if (filp->f_pos == 2)
				list_move(q, &dentry->d_subdirs);

			for (p=q->next; p != &dentry->d_subdirs; p=p->next) {
				struct dentry *next;
				next = list_entry(p, struct dentry, d_u.d_child);
				if (d_unhashed(next) || !next->d_inode)
					continue;

				spin_unlock(&dcache_lock);
				if (filldir(dirent, next->d_name.name, 
					    next->d_name.len, filp->f_pos, 
					    next->d_inode->i_ino, 
					    dt_type(next->d_inode)) < 0)
					return 0;
				spin_lock(&dcache_lock);
				/* next is still alive */
				list_move(q, p);
				p = q;
				filp->f_pos++;
			}
			spin_unlock(&dcache_lock);
	}
	return 0;
}

ssize_t generic_read_dir(struct file *filp, char __user *buf, size_t siz, loff_t *ppos)
{
	return -EISDIR;
}

const struct file_operations simple_dir_operations = {
	.open		= dcache_dir_open,
	.release	= dcache_dir_close,
	.llseek		= dcache_dir_lseek,
	.read		= generic_read_dir,
	.readdir	= dcache_readdir,
	.fsync		= noop_fsync,
};

const struct inode_operations simple_dir_inode_operations = {
	.lookup		= simple_lookup,
};

static const struct super_operations simple_super_operations = {
	.statfs		= simple_statfs,
};

/*
 * Common helper for pseudo-filesystems (sockfs, pipefs, bdev - stuff that
 * will never be mountable)
 */
int get_sb_pseudo(struct file_system_type *fs_type, char *name,
	const struct super_operations *ops, unsigned long magic,
	struct vfsmount *mnt)
{
	struct super_block *s = sget(fs_type, NULL, set_anon_super, NULL);
	struct dentry *dentry;
	struct inode *root;
	struct qstr d_name = {.name = name, .len = strlen(name)};

	if (IS_ERR(s))
		return PTR_ERR(s);

	s->s_flags = MS_NOUSER;
	s->s_maxbytes = MAX_LFS_FILESIZE;
	s->s_blocksize = PAGE_SIZE;
	s->s_blocksize_bits = PAGE_SHIFT;
	s->s_magic = magic;
	s->s_op = ops ? ops : &simple_super_operations;
	s->s_time_gran = 1;
	root = new_inode(s);
	if (!root)
		goto Enomem;
	/*
	 * since this is the first inode, make it number 1. New inodes created
	 * after this must take care not to collide with it (by passing
	 * max_reserved of 1 to iunique).
	 */
	root->i_ino = 1;
	root->i_mode = S_IFDIR | S_IRUSR | S_IWUSR;
	root->i_atime = root->i_mtime = root->i_ctime = CURRENT_TIME;
	dentry = d_alloc(NULL, &d_name);
	if (!dentry) {
		iput(root);
		goto Enomem;
	}
	dentry->d_sb = s;
	dentry->d_parent = dentry;
	d_instantiate(dentry, root);
	s->s_root = dentry;
	s->s_flags |= MS_ACTIVE;
	simple_set_mnt(mnt, s);
	return 0;

Enomem:
	deactivate_locked_super(s);
	return -ENOMEM;
}

int simple_link(struct dentry *old_dentry, struct inode *dir, struct dentry *dentry)
{
	struct inode *inode = old_dentry->d_inode;

	inode->i_ctime = dir->i_ctime = dir->i_mtime = CURRENT_TIME;
	inc_nlink(inode);
	atomic_inc(&inode->i_count);
	dget(dentry);
	d_instantiate(dentry, inode);
	return 0;
}

static inline int simple_positive(struct dentry *dentry)
{
	return dentry->d_inode && !d_unhashed(dentry);
}

int simple_empty(struct dentry *dentry)
{
	struct dentry *child;
	int ret = 0;

	spin_lock(&dcache_lock);
	list_for_each_entry(child, &dentry->d_subdirs, d_u.d_child)
		if (simple_positive(child))
			goto out;
	ret = 1;
out:
	spin_unlock(&dcache_lock);
	return ret;
}

int simple_unlink(struct inode *dir, struct dentry *dentry)
{
	struct inode *inode = dentry->d_inode;

	inode->i_ctime = dir->i_ctime = dir->i_mtime = CURRENT_TIME;
	drop_nlink(inode);
	dput(dentry);
	return 0;
}

int simple_rmdir(struct inode *dir, struct dentry *dentry)
{
	if (!simple_empty(dentry))
		return -ENOTEMPTY;

	drop_nlink(dentry->d_inode);
	simple_unlink(dir, dentry);
	drop_nlink(dir);
	return 0;
}

int simple_rename(struct inode *old_dir, struct dentry *old_dentry,
		struct inode *new_dir, struct dentry *new_dentry)
{
	struct inode *inode = old_dentry->d_inode;
	int they_are_dirs = S_ISDIR(old_dentry->d_inode->i_mode);

	if (!simple_empty(new_dentry))
		return -ENOTEMPTY;

	if (new_dentry->d_inode) {
		simple_unlink(new_dir, new_dentry);
		if (they_are_dirs)
			drop_nlink(old_dir);
	} else if (they_are_dirs) {
		drop_nlink(old_dir);
		inc_nlink(new_dir);
	}

	old_dir->i_ctime = old_dir->i_mtime = new_dir->i_ctime =
		new_dir->i_mtime = inode->i_ctime = CURRENT_TIME;

	return 0;
}

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
	struct inode *inode = dentry->d_inode;
	int error;

	WARN_ON_ONCE(inode->i_op->truncate);

	error = inode_change_ok(inode, iattr);
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

int simple_write_begin(struct file *file, struct address_space *mapping,
			loff_t pos, unsigned len, unsigned flags,
			struct page **pagep, void **fsdata)
{
	struct page *page;
	pgoff_t index;

	index = pos >> PAGE_CACHE_SHIFT;

	page = grab_cache_page_write_begin(mapping, index, flags);
	if (!page)
		return -ENOMEM;

	*pagep = page;

	if (!PageUptodate(page) && (len != PAGE_CACHE_SIZE)) {
		unsigned from = pos & (PAGE_CACHE_SIZE - 1);

		zero_user_segments(page, 0, from, from + len, PAGE_CACHE_SIZE);
	}
	return 0;
}

/**
 * simple_write_end - .write_end helper for non-block-device FSes
 * @available: See .write_end of address_space_operations
 * @file: 		"
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
 */
int simple_write_end(struct file *file, struct address_space *mapping,
			loff_t pos, unsigned len, unsigned copied,
			struct page *page, void *fsdata)
{
	struct inode *inode = page->mapping->host;
	loff_t last_pos = pos + copied;

	/* zero the stale part of the page if we did a short copy */
	if (copied < len) {
		unsigned from = pos & (PAGE_CACHE_SIZE - 1);

		zero_user(page, from + copied, len - copied);
	}

	if (!PageUptodate(page))
		SetPageUptodate(page);
	/*
	 * No need to use i_size_read() here, the i_size
	 * cannot change under us because we hold the i_mutex.
	 */
	if (last_pos > inode->i_size)
		i_size_write(inode, last_pos);

	set_page_dirty(page);
	unlock_page(page);
	page_cache_release(page);

	return copied;
}

/*
 * the inodes created here are not hashed. If you use iunique to generate
 * unique inode values later for this filesystem, then you must take care
 * to pass it an appropriate max_reserved value to avoid collisions.
 */
int simple_fill_super(struct super_block *s, unsigned long magic,
		      struct tree_descr *files)
{
	struct inode *inode;
	struct dentry *root;
	struct dentry *dentry;
	int i;

	s->s_blocksize = PAGE_CACHE_SIZE;
	s->s_blocksize_bits = PAGE_CACHE_SHIFT;
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
	inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
	inode->i_op = &simple_dir_inode_operations;
	inode->i_fop = &simple_dir_operations;
	inode->i_nlink = 2;
	root = d_alloc_root(inode);
	if (!root) {
		iput(inode);
		return -ENOMEM;
	}
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
		if (!inode)
			goto out;
		inode->i_mode = S_IFREG | files->mode;
		inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
		inode->i_fop = files->ops;
		inode->i_ino = i;
		d_add(dentry, inode);
	}
	s->s_root = root;
	return 0;
out:
	d_genocide(root);
	dput(root);
	return -ENOMEM;
}

static DEFINE_SPINLOCK(pin_fs_lock);

int simple_pin_fs(struct file_system_type *type, struct vfsmount **mount, int *count)
{
	struct vfsmount *mnt = NULL;
	spin_lock(&pin_fs_lock);
	if (unlikely(!*mount)) {
		spin_unlock(&pin_fs_lock);
		mnt = vfs_kern_mount(type, 0, type->name, NULL);
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

ssize_t simple_transaction_read(struct file *file, char __user *buf, size_t size, loff_t *pos)
{
	struct simple_transaction_argresp *ar = file->private_data;

	if (!ar)
		return 0;
	return simple_read_from_buffer(buf, size, pos, ar->data, ar->size);
}

int simple_transaction_release(struct inode *inode, struct file *file)
{
	free_page((unsigned long)file->private_data);
	return 0;
}

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

	attr = kmalloc(sizeof(*attr), GFP_KERNEL);
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

int simple_attr_release(struct inode *inode, struct file *file)
{
	kfree(file->private_data);
	return 0;
}

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

	if (*ppos) {		/* continued read */
		size = strlen(attr->get_buf);
	} else {		/* first read */
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

/* interpret the buffer as a number to call the set function with */
ssize_t simple_attr_write(struct file *file, const char __user *buf,
			  size_t len, loff_t *ppos)
{
	struct simple_attr *attr;
	u64 val;
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
	val = simple_strtol(attr->set_buf, NULL, 0);
	ret = attr->set(attr->data, val);
	if (ret == 0)
		ret = len; /* on success, claim we got the whole input */
out:
	mutex_unlock(&attr->mutex);
	return ret;
}

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
 * generic_fh_to_dentry - generic helper for the fh_to_parent export operation
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
 * generic_file_fsync - generic fsync implementation for simple filesystems
 * @file:	file to synchronize
 * @datasync:	only synchronize essential metadata if true
 *
 * This is a generic implementation of the fsync method for simple
 * filesystems which track all non-inode metadata in the buffers list
 * hanging off the address_space structure.
 */
int generic_file_fsync(struct file *file, int datasync)
{
	struct writeback_control wbc = {
		.sync_mode = WB_SYNC_ALL,
		.nr_to_write = 0, /* metadata-only; caller takes care of data */
	};
	struct inode *inode = file->f_mapping->host;
	int err;
	int ret;

	ret = sync_mapping_buffers(inode->i_mapping);
	if (!(inode->i_state & I_DIRTY))
		return ret;
	if (datasync && !(inode->i_state & I_DIRTY_DATASYNC))
		return ret;

	err = sync_inode(inode, &wbc);
	if (ret == 0)
		ret = err;
	return ret;
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
		last_fs_block >> (PAGE_CACHE_SHIFT - blocksize_bits);

	if (unlikely(num_blocks == 0))
		return 0;

	if ((blocksize_bits < 9) || (blocksize_bits > PAGE_CACHE_SHIFT))
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
int noop_fsync(struct file *file, int datasync)
{
	return 0;
}

EXPORT_SYMBOL(dcache_dir_close);
EXPORT_SYMBOL(dcache_dir_lseek);
EXPORT_SYMBOL(dcache_dir_open);
EXPORT_SYMBOL(dcache_readdir);
EXPORT_SYMBOL(generic_read_dir);
EXPORT_SYMBOL(get_sb_pseudo);
EXPORT_SYMBOL(simple_write_begin);
EXPORT_SYMBOL(simple_write_end);
EXPORT_SYMBOL(simple_dir_inode_operations);
EXPORT_SYMBOL(simple_dir_operations);
EXPORT_SYMBOL(simple_empty);
EXPORT_SYMBOL(simple_fill_super);
EXPORT_SYMBOL(simple_getattr);
EXPORT_SYMBOL(simple_link);
EXPORT_SYMBOL(simple_lookup);
EXPORT_SYMBOL(simple_pin_fs);
EXPORT_SYMBOL(simple_readpage);
EXPORT_SYMBOL(simple_release_fs);
EXPORT_SYMBOL(simple_rename);
EXPORT_SYMBOL(simple_rmdir);
EXPORT_SYMBOL(simple_statfs);
EXPORT_SYMBOL(noop_fsync);
EXPORT_SYMBOL(simple_unlink);
EXPORT_SYMBOL(simple_read_from_buffer);
EXPORT_SYMBOL(simple_write_to_buffer);
EXPORT_SYMBOL(memory_read_from_buffer);
EXPORT_SYMBOL(simple_transaction_set);
EXPORT_SYMBOL(simple_transaction_get);
EXPORT_SYMBOL(simple_transaction_read);
EXPORT_SYMBOL(simple_transaction_release);
EXPORT_SYMBOL_GPL(simple_attr_open);
EXPORT_SYMBOL_GPL(simple_attr_release);
EXPORT_SYMBOL_GPL(simple_attr_read);
EXPORT_SYMBOL_GPL(simple_attr_write);
