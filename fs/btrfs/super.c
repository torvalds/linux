#include <linux/module.h>
#include <linux/buffer_head.h>
#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/highmem.h>
#include <linux/time.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/smp_lock.h>
#include <linux/backing-dev.h>
#include "ctree.h"
#include "disk-io.h"
#include "transaction.h"

#define BTRFS_SUPER_MAGIC 0x9123682E

static struct inode_operations btrfs_dir_inode_operations;
static struct super_operations btrfs_super_ops;
static struct file_operations btrfs_dir_file_operations;

#if 0
/* some random number */

static struct super_operations ramfs_ops;

static struct backing_dev_info ramfs_backing_dev_info = {
	.ra_pages	= 0,	/* No readahead */
	.capabilities	= BDI_CAP_NO_ACCT_DIRTY | BDI_CAP_NO_WRITEBACK |
			  BDI_CAP_MAP_DIRECT | BDI_CAP_MAP_COPY |
			  BDI_CAP_READ_MAP | BDI_CAP_WRITE_MAP | BDI_CAP_EXEC_MAP,
};

struct inode *ramfs_get_inode(struct super_block *sb, int mode, dev_t dev)
{
	struct inode * inode = new_inode(sb);

	if (inode) {
		inode->i_mode = mode;
		inode->i_uid = current->fsuid;
		inode->i_gid = current->fsgid;
		inode->i_blocks = 0;
		inode->i_mapping->a_ops = &ramfs_aops;
		inode->i_mapping->backing_dev_info = &ramfs_backing_dev_info;
		inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
		switch (mode & S_IFMT) {
		default:
			init_special_inode(inode, mode, dev);
			break;
		case S_IFREG:
			inode->i_op = &ramfs_file_inode_operations;
			inode->i_fop = &ramfs_file_operations;
			break;
		case S_IFDIR:
			inode->i_op = &ramfs_dir_inode_operations;
			inode->i_fop = &simple_dir_operations;

			/* directory inodes start off with i_nlink == 2 (for "." entry) */
			inc_nlink(inode);
			break;
		case S_IFLNK:
			inode->i_op = &page_symlink_inode_operations;
			break;
		}
	}
	return inode;
}

/*
 * File creation. Allocate an inode, and we're done..
 */
/* SMP-safe */
static int
ramfs_mknod(struct inode *dir, struct dentry *dentry, int mode, dev_t dev)
{
	struct inode * inode = ramfs_get_inode(dir->i_sb, mode, dev);
	int error = -ENOSPC;

	if (inode) {
		if (dir->i_mode & S_ISGID) {
			inode->i_gid = dir->i_gid;
			if (S_ISDIR(mode))
				inode->i_mode |= S_ISGID;
		}
		d_instantiate(dentry, inode);
		dget(dentry);	/* Extra count - pin the dentry in core */
		error = 0;
		dir->i_mtime = dir->i_ctime = CURRENT_TIME;
	}
	return error;
}

static int ramfs_mkdir(struct inode * dir, struct dentry * dentry, int mode)
{
	int retval = ramfs_mknod(dir, dentry, mode | S_IFDIR, 0);
	if (!retval)
		inc_nlink(dir);
	return retval;
}

static int ramfs_create(struct inode *dir, struct dentry *dentry, int mode, struct nameidata *nd)
{
	return ramfs_mknod(dir, dentry, mode | S_IFREG, 0);
}

static int ramfs_symlink(struct inode * dir, struct dentry *dentry, const char * symname)
{
	struct inode *inode;
	int error = -ENOSPC;

	inode = ramfs_get_inode(dir->i_sb, S_IFLNK|S_IRWXUGO, 0);
	if (inode) {
		int l = strlen(symname)+1;
		error = page_symlink(inode, symname, l);
		if (!error) {
			if (dir->i_mode & S_ISGID)
				inode->i_gid = dir->i_gid;
			d_instantiate(dentry, inode);
			dget(dentry);
			dir->i_mtime = dir->i_ctime = CURRENT_TIME;
		} else
			iput(inode);
	}
	return error;
}

static struct inode_operations ramfs_dir_inode_operations = {
	.create		= ramfs_create,
	.lookup		= simple_lookup,
	.link		= simple_link,
	.unlink		= simple_unlink,
	.symlink	= ramfs_symlink,
	.mkdir		= ramfs_mkdir,
	.rmdir		= simple_rmdir,
	.mknod		= ramfs_mknod,
	.rename		= simple_rename,
};
#endif

static void btrfs_read_locked_inode(struct inode *inode)
{
	struct btrfs_path path;
	struct btrfs_inode_item *inode_item;
	struct btrfs_root *root = btrfs_sb(inode->i_sb);
	int ret;
	btrfs_init_path(&path);
	ret = btrfs_lookup_inode(NULL, root, &path, inode->i_ino, 0);
	if (ret) {
		make_bad_inode(inode);
		return;
	}
	inode_item = btrfs_item_ptr(btrfs_buffer_leaf(path.nodes[0]),
				  path.slots[0],
				  struct btrfs_inode_item);

	inode->i_mode = btrfs_inode_mode(inode_item);
	inode->i_nlink = btrfs_inode_nlink(inode_item);
	inode->i_uid = btrfs_inode_uid(inode_item);
	inode->i_gid = btrfs_inode_gid(inode_item);
	inode->i_size = btrfs_inode_size(inode_item);
	inode->i_atime.tv_sec = btrfs_timespec_sec(&inode_item->atime);
	inode->i_atime.tv_nsec = btrfs_timespec_nsec(&inode_item->atime);
	inode->i_mtime.tv_sec = btrfs_timespec_sec(&inode_item->mtime);
	inode->i_mtime.tv_nsec = btrfs_timespec_nsec(&inode_item->mtime);
	inode->i_ctime.tv_sec = btrfs_timespec_sec(&inode_item->ctime);
	inode->i_ctime.tv_nsec = btrfs_timespec_nsec(&inode_item->ctime);
	inode->i_blocks = btrfs_inode_nblocks(inode_item);
	inode->i_generation = btrfs_inode_generation(inode_item);
	btrfs_release_path(root, &path);
	switch (inode->i_mode & S_IFMT) {
#if 0
	default:
		init_special_inode(inode, inode->i_mode,
				   btrfs_inode_rdev(inode_item));
		break;
#endif
	case S_IFREG:
		break;
	case S_IFDIR:
		inode->i_op = &btrfs_dir_inode_operations;
		inode->i_fop = &btrfs_dir_file_operations;
		break;
	case S_IFLNK:
		// inode->i_op = &page_symlink_inode_operations;
		break;
	}
	return;
}

static int btrfs_inode_by_name(struct inode *dir, struct dentry *dentry,
			      ino_t *ino)
{
	const char *name = dentry->d_name.name;
	int namelen = dentry->d_name.len;
	struct btrfs_dir_item *di;
	struct btrfs_path path;
	struct btrfs_root *root = btrfs_sb(dir->i_sb);
	int ret;

	btrfs_init_path(&path);
	ret = btrfs_lookup_dir_item(NULL, root, &path, dir->i_ino, name,
				    namelen, 0);
	if (ret) {
		*ino = 0;
		goto out;
	}
	di = btrfs_item_ptr(btrfs_buffer_leaf(path.nodes[0]), path.slots[0],
			    struct btrfs_dir_item);
	*ino = btrfs_dir_objectid(di);
out:
	btrfs_release_path(root, &path);
	return ret;
}

static struct dentry *btrfs_lookup(struct inode *dir, struct dentry *dentry,
				   struct nameidata *nd)
{
	struct inode * inode;
	ino_t ino;
	int ret;

	if (dentry->d_name.len > BTRFS_NAME_LEN)
		return ERR_PTR(-ENAMETOOLONG);

	ret = btrfs_inode_by_name(dir, dentry, &ino);
	if (ret < 0)
		return ERR_PTR(ret);
	inode = NULL;
	if (ino) {
		inode = iget(dir->i_sb, ino);
		if (!inode)
			return ERR_PTR(-EACCES);
	}
	return d_splice_alias(inode, dentry);
}

static int btrfs_readdir(struct file *filp, void *dirent, filldir_t filldir)
{
	struct inode *inode = filp->f_path.dentry->d_inode;
	struct btrfs_root *root = btrfs_sb(inode->i_sb);
	struct btrfs_item *item;
	struct btrfs_dir_item *di;
	struct btrfs_key key;
	struct btrfs_path path;
	int ret;
	u32 nritems;
	struct btrfs_leaf *leaf;
	int slot;
	int advance;
	unsigned char d_type = DT_UNKNOWN;
	int over;

	key.objectid = inode->i_ino;
	key.flags = 0;
	btrfs_set_key_type(&key, BTRFS_DIR_ITEM_KEY);
	key.offset = filp->f_pos;
	btrfs_init_path(&path);
	ret = btrfs_search_slot(NULL, root, &key, &path, 0, 0);
	if (ret < 0) {
		goto err;
	}
	advance = filp->f_pos > 0 && ret != 0;
	while(1) {
		leaf = btrfs_buffer_leaf(path.nodes[0]);
		nritems = btrfs_header_nritems(&leaf->header);
		slot = path.slots[0];
		if (advance) {
			if (slot == nritems -1) {
				ret = btrfs_next_leaf(root, &path);
				if (ret)
					break;
				leaf = btrfs_buffer_leaf(path.nodes[0]);
				nritems = btrfs_header_nritems(&leaf->header);
				slot = path.slots[0];
			} else {
				slot++;
				path.slots[0]++;
			}
		}
		advance = 1;
		item = leaf->items + slot;
		if (btrfs_disk_key_objectid(&item->key) != key.objectid)
			break;
		if (btrfs_disk_key_type(&item->key) != BTRFS_DIR_ITEM_KEY)
			continue;
		di = btrfs_item_ptr(leaf, slot, struct btrfs_dir_item);
		over = filldir(dirent, (const char *)(di + 1),
			       btrfs_dir_name_len(di),
			       btrfs_disk_key_offset(&item->key),
			       btrfs_dir_objectid(di), d_type);
		if (over)
			break;
		filp->f_pos = btrfs_disk_key_offset(&item->key) + 1;
	}
	ret = 0;
err:
	btrfs_release_path(root, &path);
	return ret;
}

static void btrfs_put_super (struct super_block * sb)
{
	struct btrfs_root *root = btrfs_sb(sb);
	int ret;

	ret = close_ctree(root);
	if (ret) {
		printk("close ctree returns %d\n", ret);
	}
	sb->s_fs_info = NULL;
}

static int btrfs_fill_super(struct super_block * sb, void * data, int silent)
{
	struct inode * inode;
	struct dentry * root_dentry;
	struct btrfs_super_block *disk_super;
	struct buffer_head *bh;
	struct btrfs_root *root;

	sb->s_maxbytes = MAX_LFS_FILESIZE;
	sb->s_blocksize = PAGE_CACHE_SIZE;
	sb->s_blocksize_bits = PAGE_CACHE_SHIFT;
	sb->s_magic = BTRFS_SUPER_MAGIC;
	sb->s_op = &btrfs_super_ops;
	sb->s_time_gran = 1;

	bh = sb_bread(sb, BTRFS_SUPER_INFO_OFFSET / sb->s_blocksize);
	if (!bh) {
		printk("btrfs: unable to read on disk super\n");
		return -EIO;
	}
	disk_super = (struct btrfs_super_block *)bh->b_data;
	root = open_ctree(sb, bh, disk_super);
	sb->s_fs_info = root;
	if (!root) {
		printk("btrfs: open_ctree failed\n");
		return -EIO;
	}
	printk("read in super total blocks %Lu root %Lu\n",
	       btrfs_super_total_blocks(disk_super),
	       btrfs_super_root_dir(disk_super));

	inode = iget_locked(sb, btrfs_super_root_dir(disk_super));
	if (!inode)
		return -ENOMEM;
	if (inode->i_state & I_NEW) {
		btrfs_read_locked_inode(inode);
		unlock_new_inode(inode);
	}

	root_dentry = d_alloc_root(inode);
	if (!root_dentry) {
		iput(inode);
		return -ENOMEM;
	}
	sb->s_root = root_dentry;

	return 0;
}

static void fill_inode_item(struct btrfs_inode_item *item,
			    struct inode *inode)
{
	btrfs_set_inode_uid(item, inode->i_uid);
	btrfs_set_inode_gid(item, inode->i_gid);
	btrfs_set_inode_size(item, inode->i_size);
	btrfs_set_inode_mode(item, inode->i_mode);
	btrfs_set_inode_nlink(item, inode->i_nlink);
	btrfs_set_timespec_sec(&item->atime, inode->i_atime.tv_sec);
	btrfs_set_timespec_nsec(&item->atime, inode->i_atime.tv_nsec);
	btrfs_set_timespec_sec(&item->mtime, inode->i_mtime.tv_sec);
	btrfs_set_timespec_nsec(&item->mtime, inode->i_mtime.tv_nsec);
	btrfs_set_timespec_sec(&item->ctime, inode->i_ctime.tv_sec);
	btrfs_set_timespec_nsec(&item->ctime, inode->i_ctime.tv_nsec);
	btrfs_set_inode_nblocks(item, inode->i_blocks);
	btrfs_set_inode_generation(item, inode->i_generation);
}

static struct inode *btrfs_new_inode(struct btrfs_trans_handle *trans,
				     struct inode *dir, int mode)
{
	struct inode *inode;
	struct btrfs_inode_item inode_item;
	struct btrfs_root *root = btrfs_sb(dir->i_sb);
	struct btrfs_key key;
	int ret;
	u64 objectid;

	inode = new_inode(dir->i_sb);
	if (!inode)
		return ERR_PTR(-ENOMEM);

	ret = btrfs_find_free_objectid(trans, root, dir->i_ino, &objectid);
	BUG_ON(ret);

	inode->i_uid = current->fsuid;
	inode->i_gid = current->fsgid;
	inode->i_mode = mode;
	inode->i_ino = objectid;
	inode->i_blocks = 0;
	inode->i_mtime = inode->i_atime = inode->i_ctime = CURRENT_TIME_SEC;
	fill_inode_item(&inode_item, inode);


	key.objectid = objectid;
	key.flags = 0;
	key.offset = 0;
	btrfs_set_key_type(&key, BTRFS_INODE_ITEM_KEY);
	ret = btrfs_insert_inode_map(trans, root, objectid, &key);
	BUG_ON(ret);

	ret = btrfs_insert_inode(trans, root, objectid, &inode_item);
	BUG_ON(ret);

	insert_inode_hash(inode);
	// FIXME mark_inode_dirty(inode)
	return inode;
}

static int btrfs_add_link(struct btrfs_trans_handle *trans,
			    struct dentry *dentry, struct inode *inode)
{
	int ret;
	ret = btrfs_insert_dir_item(trans, btrfs_sb(inode->i_sb),
				    dentry->d_name.name, dentry->d_name.len,
				    dentry->d_parent->d_inode->i_ino,
				    inode->i_ino, 0);
	BUG_ON(ret);
	return ret;
}

static int btrfs_add_nondir(struct btrfs_trans_handle *trans,
			    struct dentry *dentry, struct inode *inode)
{
	int err = btrfs_add_link(trans, dentry, inode);
	if (!err) {
		d_instantiate(dentry, inode);
		return 0;
	}
	inode_dec_link_count(inode);
	iput(inode);
	return err;
}

static int btrfs_create(struct inode *dir, struct dentry *dentry,
			int mode, struct nameidata *nd)
{
	struct btrfs_trans_handle *trans;
	struct btrfs_root *root = btrfs_sb(dir->i_sb);
	struct inode *inode;
	int err;

	trans = btrfs_start_transaction(root, 1);
	inode = btrfs_new_inode(trans, dir, mode);
	err = PTR_ERR(inode);
	if (IS_ERR(inode))
		return err;
	// FIXME mark the inode dirty
	err = btrfs_add_nondir(trans, dentry, inode);
	dir->i_sb->s_dirt = 1;
	btrfs_end_transaction(trans, root);
	return err;
}

static void btrfs_write_super(struct super_block *sb)
{
	sb->s_dirt = 0;
printk("btrfs write_super!\n");
}

static int btrfs_sync_fs(struct super_block *sb, int wait)
{
	struct btrfs_trans_handle *trans;
	struct btrfs_root *root;
	int ret;

	sb->s_dirt = 0;
	return 0;

	root = btrfs_sb(sb);
	trans = btrfs_start_transaction(root, 1);
	ret = btrfs_commit_transaction(trans, root);
	sb->s_dirt = 0;
	BUG_ON(ret);
printk("btrfs sync_fs\n");
	return 0;
}

static int btrfs_get_sb(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data, struct vfsmount *mnt)
{
	return get_sb_bdev(fs_type, flags, dev_name, data,
			   btrfs_fill_super, mnt);
}

static struct file_system_type btrfs_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "btrfs",
	.get_sb		= btrfs_get_sb,
	.kill_sb	= kill_block_super,
	.fs_flags	= FS_REQUIRES_DEV,
};

static struct super_operations btrfs_super_ops = {
	.statfs		= simple_statfs,
	.drop_inode	= generic_delete_inode,
	.put_super	= btrfs_put_super,
	.read_inode	= btrfs_read_locked_inode,
	.write_super	= btrfs_write_super,
	.sync_fs	= btrfs_sync_fs,
};

static struct inode_operations btrfs_dir_inode_operations = {
	.lookup		= btrfs_lookup,
	.create		= btrfs_create,
};

static struct file_operations btrfs_dir_file_operations = {
	.llseek		= generic_file_llseek,
	.read		= generic_read_dir,
	.readdir	= btrfs_readdir,
};


static int __init init_btrfs_fs(void)
{
	printk("btrfs loaded!\n");
	return register_filesystem(&btrfs_fs_type);
}

static void __exit exit_btrfs_fs(void)
{
	unregister_filesystem(&btrfs_fs_type);
	printk("btrfs unloaded\n");
}

module_init(init_btrfs_fs)
module_exit(exit_btrfs_fs)

MODULE_LICENSE("GPL");
