/*
 *	fs/libfs.c
 *	Library for filesystems writers.
 */

#include <linux/module.h>
#include <linux/pagemap.h>
#include <linux/mount.h>
#include <linux/vfs.h>
#include <asm/uaccess.h>

int simple_getattr(struct vfsmount *mnt, struct dentry *dentry,
		   struct kstat *stat)
{
	struct inode *inode = dentry->d_inode;
	generic_fillattr(inode, stat);
	stat->blocks = inode->i_mapping->nrpages << (PAGE_CACHE_SHIFT - 9);
	return 0;
}

int simple_statfs(struct super_block *sb, struct kstatfs *buf)
{
	buf->f_type = sb->s_magic;
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
	static struct dentry_operations simple_dentry_operations = {
		.d_delete = simple_delete_dentry,
	};

	if (dentry->d_name.len > NAME_MAX)
		return ERR_PTR(-ENAMETOOLONG);
	dentry->d_op = &simple_dentry_operations;
	d_add(dentry, NULL);
	return NULL;
}

int simple_sync_file(struct file * file, struct dentry *dentry, int datasync)
{
	return 0;
}
 
int dcache_dir_open(struct inode *inode, struct file *file)
{
	static struct qstr cursor_name = {.len = 1, .name = "."};

	file->private_data = d_alloc(file->f_dentry, &cursor_name);

	return file->private_data ? 0 : -ENOMEM;
}

int dcache_dir_close(struct inode *inode, struct file *file)
{
	dput(file->private_data);
	return 0;
}

loff_t dcache_dir_lseek(struct file *file, loff_t offset, int origin)
{
	down(&file->f_dentry->d_inode->i_sem);
	switch (origin) {
		case 1:
			offset += file->f_pos;
		case 0:
			if (offset >= 0)
				break;
		default:
			up(&file->f_dentry->d_inode->i_sem);
			return -EINVAL;
	}
	if (offset != file->f_pos) {
		file->f_pos = offset;
		if (file->f_pos >= 2) {
			struct list_head *p;
			struct dentry *cursor = file->private_data;
			loff_t n = file->f_pos - 2;

			spin_lock(&dcache_lock);
			list_del(&cursor->d_child);
			p = file->f_dentry->d_subdirs.next;
			while (n && p != &file->f_dentry->d_subdirs) {
				struct dentry *next;
				next = list_entry(p, struct dentry, d_child);
				if (!d_unhashed(next) && next->d_inode)
					n--;
				p = p->next;
			}
			list_add_tail(&cursor->d_child, p);
			spin_unlock(&dcache_lock);
		}
	}
	up(&file->f_dentry->d_inode->i_sem);
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
	struct dentry *dentry = filp->f_dentry;
	struct dentry *cursor = filp->private_data;
	struct list_head *p, *q = &cursor->d_child;
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
			if (filp->f_pos == 2) {
				list_del(q);
				list_add(q, &dentry->d_subdirs);
			}
			for (p=q->next; p != &dentry->d_subdirs; p=p->next) {
				struct dentry *next;
				next = list_entry(p, struct dentry, d_child);
				if (d_unhashed(next) || !next->d_inode)
					continue;

				spin_unlock(&dcache_lock);
				if (filldir(dirent, next->d_name.name, next->d_name.len, filp->f_pos, next->d_inode->i_ino, dt_type(next->d_inode)) < 0)
					return 0;
				spin_lock(&dcache_lock);
				/* next is still alive */
				list_del(q);
				list_add(q, p);
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

struct file_operations simple_dir_operations = {
	.open		= dcache_dir_open,
	.release	= dcache_dir_close,
	.llseek		= dcache_dir_lseek,
	.read		= generic_read_dir,
	.readdir	= dcache_readdir,
	.fsync		= simple_sync_file,
};

struct inode_operations simple_dir_inode_operations = {
	.lookup		= simple_lookup,
};

/*
 * Common helper for pseudo-filesystems (sockfs, pipefs, bdev - stuff that
 * will never be mountable)
 */
struct super_block *
get_sb_pseudo(struct file_system_type *fs_type, char *name,
	struct super_operations *ops, unsigned long magic)
{
	struct super_block *s = sget(fs_type, NULL, set_anon_super, NULL);
	static struct super_operations default_ops = {.statfs = simple_statfs};
	struct dentry *dentry;
	struct inode *root;
	struct qstr d_name = {.name = name, .len = strlen(name)};

	if (IS_ERR(s))
		return s;

	s->s_flags = MS_NOUSER;
	s->s_maxbytes = ~0ULL;
	s->s_blocksize = 1024;
	s->s_blocksize_bits = 10;
	s->s_magic = magic;
	s->s_op = ops ? ops : &default_ops;
	s->s_time_gran = 1;
	root = new_inode(s);
	if (!root)
		goto Enomem;
	root->i_mode = S_IFDIR | S_IRUSR | S_IWUSR;
	root->i_uid = root->i_gid = 0;
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
	return s;

Enomem:
	up_write(&s->s_umount);
	deactivate_super(s);
	return ERR_PTR(-ENOMEM);
}

int simple_link(struct dentry *old_dentry, struct inode *dir, struct dentry *dentry)
{
	struct inode *inode = old_dentry->d_inode;

	inode->i_ctime = dir->i_ctime = dir->i_mtime = CURRENT_TIME;
	inode->i_nlink++;
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
	list_for_each_entry(child, &dentry->d_subdirs, d_child)
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
	inode->i_nlink--;
	dput(dentry);
	return 0;
}

int simple_rmdir(struct inode *dir, struct dentry *dentry)
{
	if (!simple_empty(dentry))
		return -ENOTEMPTY;

	dentry->d_inode->i_nlink--;
	simple_unlink(dir, dentry);
	dir->i_nlink--;
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
			old_dir->i_nlink--;
	} else if (they_are_dirs) {
		old_dir->i_nlink--;
		new_dir->i_nlink++;
	}

	old_dir->i_ctime = old_dir->i_mtime = new_dir->i_ctime =
		new_dir->i_mtime = inode->i_ctime = CURRENT_TIME;

	return 0;
}

int simple_readpage(struct file *file, struct page *page)
{
	void *kaddr;

	if (PageUptodate(page))
		goto out;

	kaddr = kmap_atomic(page, KM_USER0);
	memset(kaddr, 0, PAGE_CACHE_SIZE);
	kunmap_atomic(kaddr, KM_USER0);
	flush_dcache_page(page);
	SetPageUptodate(page);
out:
	unlock_page(page);
	return 0;
}

int simple_prepare_write(struct file *file, struct page *page,
			unsigned from, unsigned to)
{
	if (!PageUptodate(page)) {
		if (to - from != PAGE_CACHE_SIZE) {
			void *kaddr = kmap_atomic(page, KM_USER0);
			memset(kaddr, 0, from);
			memset(kaddr + to, 0, PAGE_CACHE_SIZE - to);
			flush_dcache_page(page);
			kunmap_atomic(kaddr, KM_USER0);
		}
		SetPageUptodate(page);
	}
	return 0;
}

int simple_commit_write(struct file *file, struct page *page,
			unsigned offset, unsigned to)
{
	struct inode *inode = page->mapping->host;
	loff_t pos = ((loff_t)page->index << PAGE_CACHE_SHIFT) + to;

	/*
	 * No need to use i_size_read() here, the i_size
	 * cannot change under us because we hold the i_sem.
	 */
	if (pos > inode->i_size)
		i_size_write(inode, pos);
	set_page_dirty(page);
	return 0;
}

int simple_fill_super(struct super_block *s, int magic, struct tree_descr *files)
{
	static struct super_operations s_ops = {.statfs = simple_statfs};
	struct inode *inode;
	struct dentry *root;
	struct dentry *dentry;
	int i;

	s->s_blocksize = PAGE_CACHE_SIZE;
	s->s_blocksize_bits = PAGE_CACHE_SHIFT;
	s->s_magic = magic;
	s->s_op = &s_ops;
	s->s_time_gran = 1;

	inode = new_inode(s);
	if (!inode)
		return -ENOMEM;
	inode->i_mode = S_IFDIR | 0755;
	inode->i_uid = inode->i_gid = 0;
	inode->i_blksize = PAGE_CACHE_SIZE;
	inode->i_blocks = 0;
	inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
	inode->i_op = &simple_dir_inode_operations;
	inode->i_fop = &simple_dir_operations;
	root = d_alloc_root(inode);
	if (!root) {
		iput(inode);
		return -ENOMEM;
	}
	for (i = 0; !files->name || files->name[0]; i++, files++) {
		if (!files->name)
			continue;
		dentry = d_alloc_name(root, files->name);
		if (!dentry)
			goto out;
		inode = new_inode(s);
		if (!inode)
			goto out;
		inode->i_mode = S_IFREG | files->mode;
		inode->i_uid = inode->i_gid = 0;
		inode->i_blksize = PAGE_CACHE_SIZE;
		inode->i_blocks = 0;
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

int simple_pin_fs(char *name, struct vfsmount **mount, int *count)
{
	struct vfsmount *mnt = NULL;
	spin_lock(&pin_fs_lock);
	if (unlikely(!*mount)) {
		spin_unlock(&pin_fs_lock);
		mnt = do_kern_mount(name, 0, name, NULL);
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

ssize_t simple_read_from_buffer(void __user *to, size_t count, loff_t *ppos,
				const void *from, size_t available)
{
	loff_t pos = *ppos;
	if (pos < 0)
		return -EINVAL;
	if (pos >= available)
		return 0;
	if (count > available - pos)
		count = available - pos;
	if (copy_to_user(to, from + pos, count))
		return -EFAULT;
	*ppos = pos + count;
	return count;
}

/*
 * Transaction based IO.
 * The file expects a single write which triggers the transaction, and then
 * possibly a read which collects the result - which is stored in a
 * file-local buffer.
 */
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
	u64 (*get)(void *);
	void (*set)(void *, u64);
	char get_buf[24];	/* enough to store a u64 and "\n\0" */
	char set_buf[24];
	void *data;
	const char *fmt;	/* format for read operation */
	struct semaphore sem;	/* protects access to these buffers */
};

/* simple_attr_open is called by an actual attribute open file operation
 * to set the attribute specific access operations. */
int simple_attr_open(struct inode *inode, struct file *file,
		     u64 (*get)(void *), void (*set)(void *, u64),
		     const char *fmt)
{
	struct simple_attr *attr;

	attr = kmalloc(sizeof(*attr), GFP_KERNEL);
	if (!attr)
		return -ENOMEM;

	attr->get = get;
	attr->set = set;
	attr->data = inode->u.generic_ip;
	attr->fmt = fmt;
	init_MUTEX(&attr->sem);

	file->private_data = attr;

	return nonseekable_open(inode, file);
}

int simple_attr_close(struct inode *inode, struct file *file)
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

	down(&attr->sem);
	if (*ppos) /* continued read */
		size = strlen(attr->get_buf);
	else	  /* first read */
		size = scnprintf(attr->get_buf, sizeof(attr->get_buf),
				 attr->fmt,
				 (unsigned long long)attr->get(attr->data));

	ret = simple_read_from_buffer(buf, len, ppos, attr->get_buf, size);
	up(&attr->sem);
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

	down(&attr->sem);
	ret = -EFAULT;
	size = min(sizeof(attr->set_buf) - 1, len);
	if (copy_from_user(attr->set_buf, buf, size))
		goto out;

	ret = len; /* claim we got the whole input */
	attr->set_buf[size] = '\0';
	val = simple_strtol(attr->set_buf, NULL, 0);
	attr->set(attr->data, val);
out:
	up(&attr->sem);
	return ret;
}

EXPORT_SYMBOL(dcache_dir_close);
EXPORT_SYMBOL(dcache_dir_lseek);
EXPORT_SYMBOL(dcache_dir_open);
EXPORT_SYMBOL(dcache_readdir);
EXPORT_SYMBOL(generic_read_dir);
EXPORT_SYMBOL(get_sb_pseudo);
EXPORT_SYMBOL(simple_commit_write);
EXPORT_SYMBOL(simple_dir_inode_operations);
EXPORT_SYMBOL(simple_dir_operations);
EXPORT_SYMBOL(simple_empty);
EXPORT_SYMBOL(d_alloc_name);
EXPORT_SYMBOL(simple_fill_super);
EXPORT_SYMBOL(simple_getattr);
EXPORT_SYMBOL(simple_link);
EXPORT_SYMBOL(simple_lookup);
EXPORT_SYMBOL(simple_pin_fs);
EXPORT_SYMBOL(simple_prepare_write);
EXPORT_SYMBOL(simple_readpage);
EXPORT_SYMBOL(simple_release_fs);
EXPORT_SYMBOL(simple_rename);
EXPORT_SYMBOL(simple_rmdir);
EXPORT_SYMBOL(simple_statfs);
EXPORT_SYMBOL(simple_sync_file);
EXPORT_SYMBOL(simple_unlink);
EXPORT_SYMBOL(simple_read_from_buffer);
EXPORT_SYMBOL(simple_transaction_get);
EXPORT_SYMBOL(simple_transaction_read);
EXPORT_SYMBOL(simple_transaction_release);
EXPORT_SYMBOL_GPL(simple_attr_open);
EXPORT_SYMBOL_GPL(simple_attr_close);
EXPORT_SYMBOL_GPL(simple_attr_read);
EXPORT_SYMBOL_GPL(simple_attr_write);
