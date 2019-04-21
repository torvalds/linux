/*
 * SO2 Lab - Filesystem drivers
 * Exercise #2 (dev filesystem)
 */

#include <linux/buffer_head.h>
#include <linux/cred.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pagemap.h>
#include <linux/sched.h>
#include <linux/slab.h>

#include "minfs.h"

MODULE_DESCRIPTION("Simple filesystem");
MODULE_AUTHOR("SO2");
MODULE_LICENSE("GPL");

#define LOG_LEVEL	KERN_ALERT


struct minfs_sb_info {
	__u8 version;
	unsigned long imap;
	struct buffer_head *sbh;
};

struct minfs_inode_info {
	__u16 data_block;
	struct inode vfs_inode;
};

/* declarations of functions that are part of operation structures */

static int minfs_readdir(struct file *filp, struct dir_context *ctx);
static struct dentry *minfs_lookup(struct inode *dir,
		struct dentry *dentry, unsigned int flags);
static int minfs_create(struct inode *dir, struct dentry *dentry,
		umode_t mode, bool excl);

/* dir and inode operation structures */

static const struct file_operations minfs_dir_operations = {
	.read		= generic_read_dir,
	.iterate	= minfs_readdir,
};

static const struct inode_operations minfs_dir_inode_operations = {
	.lookup		= minfs_lookup,
	/* TODO 7/1: Use minfs_create as the create function. */
	.create		= minfs_create,
};

static const struct address_space_operations minfs_aops = {
	.readpage       = simple_readpage,
	.write_begin    = simple_write_begin,
	.write_end      = simple_write_end,
};

static const struct file_operations minfs_file_operations = {
	.read_iter	= generic_file_read_iter,
	.write_iter	= generic_file_write_iter,
	.mmap		= generic_file_mmap,
	.llseek		= generic_file_llseek,
};

static const struct inode_operations minfs_file_inode_operations = {
	.getattr	= simple_getattr,
};

static struct inode *minfs_iget(struct super_block *s, unsigned long ino)
{
	struct minfs_inode *mi;
	struct buffer_head *bh;
	struct inode *inode;
	struct minfs_inode_info *mii;

	/* Allocate VFS inode. */
	inode = iget_locked(s, ino);
	if (inode == NULL) {
		printk(LOG_LEVEL "error aquiring inode\n");
		return ERR_PTR(-ENOMEM);
	}

	/* Return inode from cache */
	if (!(inode->i_state & I_NEW))
		return inode;

	/* TODO 4/2: Read block with inodes. It's the second block on
	 * the device, i.e. the block with the index 1. This is the index
	 * to be passed to sb_bread().
	 */
	if (!(bh = sb_bread(s, MINFS_INODE_BLOCK)))
		goto out_bad_sb;

	/* TODO 4/1: Get inode with index ino from the block. */
	mi = ((struct minfs_inode *) bh->b_data) + ino;

	/* TODO 4/6: fill VFS inode */
	inode->i_mode = mi->mode;
	i_uid_write(inode, mi->uid);
	i_gid_write(inode, mi->gid);
	inode->i_size = mi->size;
	inode->i_blocks = 0;
	inode->i_mtime = inode->i_atime = inode->i_ctime = current_time(inode);

	/* TODO 7/1: Fill address space operations (inode->i_mapping->a_ops) */
	inode->i_mapping->a_ops = &minfs_aops;

	if (S_ISDIR(inode->i_mode)) {
		/* TODO 4/2: Fill dir inode operations. */
		inode->i_op = &simple_dir_inode_operations;
		inode->i_fop = &simple_dir_operations;

		/* TODO 5/2: Use minfs_dir_inode_operations for i_op
		 * and minfs_dir_operations for i_fop. */
		inode->i_op = &minfs_dir_inode_operations;
		inode->i_fop = &minfs_dir_operations;

		/* TODO 4/1: Directory inodes start off with i_nlink == 2.
		 * (use inc_link) */
		inc_nlink(inode);
	}

	/* TODO 7/4: Fill inode and file operations for regular files
	 * (i_op and i_fop). Use the S_ISREG macro.
	 */
	if (S_ISREG(inode->i_mode)) {
		inode->i_op = &minfs_file_inode_operations;
		inode->i_fop = &minfs_file_operations;
	}

	/* fill data for mii */
	mii = container_of(inode, struct minfs_inode_info, vfs_inode);

	/* TODO 4/1: uncomment after the minfs_inode is initialized */
	mii->data_block = mi->data_block;
	//mii->data_block = mi->data_block;

	/* Free resources. */
	/* TODO 4/1: uncomment after the buffer_head is initialized */
	brelse(bh);
	//brelse(bh);
	unlock_new_inode(inode);

	return inode;

out_bad_sb:
	iget_failed(inode);
	return NULL;
}

static int minfs_readdir(struct file *filp, struct dir_context *ctx)
{
	struct buffer_head *bh;
	struct minfs_dir_entry *de;
	struct minfs_inode_info *mii;
	struct inode *inode;
	struct super_block *sb;
	int over;
	int err = 0;

	/* TODO 5/2: Get inode of directory and container inode. */
	inode = file_inode(filp);
	mii = container_of(inode, struct minfs_inode_info, vfs_inode);

	/* TODO 5/1: Get superblock from inode (i_sb). */
	sb = inode->i_sb;

	/* TODO 5/6: Read data block for directory inode. */
	bh = sb_bread(sb, mii->data_block);
	if (bh == NULL) {
		printk(LOG_LEVEL "could not read block\n");
		err = -ENOMEM;
		goto out_bad_sb;
	}

	for (; ctx->pos < MINFS_NUM_ENTRIES; ctx->pos++) {
		/* TODO 5/1: Data block contains an array of
		 * "struct minfs_dir_entry". Use `de' for storing.
		 */
		de = (struct minfs_dir_entry *) bh->b_data + ctx->pos;

		/* TODO 5/3: Step over empty entries (de->ino == 0). */
		if (de->ino == 0) {
			continue;
		}

		/*
		 * Use `over` to store return value of dir_emit and exit
		 * if required.
		 */
		over = dir_emit(ctx, de->name, MINFS_NAME_LEN, de->ino,
				DT_UNKNOWN);
		if (over) {
			printk(KERN_DEBUG "Read %s from folder %s, ctx->pos: %lld\n",
				de->name,
				filp->f_path.dentry->d_name.name,
				ctx->pos);
			ctx->pos++;
			goto done;
		}
	}

done:
	brelse(bh);
out_bad_sb:
	return err;
}

/*
 * Find dentry in parent folder. Return parent folder's data buffer_head.
 */

static struct minfs_dir_entry *minfs_find_entry(struct dentry *dentry,
		struct buffer_head **bhp)
{
	struct buffer_head *bh;
	struct inode *dir = dentry->d_parent->d_inode;
	struct minfs_inode_info *mii = container_of(dir,
			struct minfs_inode_info, vfs_inode);
	struct super_block *sb = dir->i_sb;
	const char *name = dentry->d_name.name;
	struct minfs_dir_entry *final_de = NULL;
	struct minfs_dir_entry *de;
	int i;

	/* TODO 6/6: Read parent folder data block (contains dentries).
	 * Fill bhp with return value.
	 */
	bh = sb_bread(sb, mii->data_block);
	if (bh == NULL) {
		printk(LOG_LEVEL "could not read block\n");
		return NULL;
	}
	*bhp = bh;

	for (i = 0; i < MINFS_NUM_ENTRIES; i++) {
		/* TODO 6/10: Traverse all entries, find entry by name
		 * Use `de' to traverse. Use `final_de' to store dentry
		 * found, if existing.
		 */
		de = ((struct minfs_dir_entry *) bh->b_data) + i;
		if (de->ino != 0) {
			/* found it */
			if (strcmp(name, de->name) == 0) {
				printk(KERN_DEBUG "Found entry %s on position: %zd\n",
					name, i);
				final_de = de;
				break;
			}
		}
	}

	/* bh needs to be released by caller. */
	return final_de;
}

static struct dentry *minfs_lookup(struct inode *dir,
		struct dentry *dentry, unsigned int flags)
{
	/* TODO 6/1: Comment line. */
	// \
	return simple_lookup(dir, dentry, flags);

	struct super_block *sb = dir->i_sb;
	struct minfs_dir_entry *de;
	struct buffer_head *bh = NULL;
	struct inode *inode = NULL;

	dentry->d_op = sb->s_root->d_op;

	de = minfs_find_entry(dentry, &bh);
	if (de != NULL) {
		printk(KERN_DEBUG "getting entry: name: %s, ino: %d\n",
			de->name, de->ino);
		inode = minfs_iget(sb, de->ino);
		if (IS_ERR(inode))
			return ERR_CAST(inode);
	}

	d_add(dentry, inode);
	brelse(bh);

	printk(KERN_DEBUG "looked up dentry %s\n", dentry->d_name.name);

	return NULL;
}

static struct inode *minfs_alloc_inode(struct super_block *s)
{
	struct minfs_inode_info *mii;

	/* TODO 3/4: Allocate minfs_inode_info. */
	mii = kzalloc(sizeof(struct minfs_inode_info), GFP_KERNEL);
	if (mii == NULL)
		return NULL;

	/* TODO 3/1: init VFS inode in minfs_inode_info */
	inode_init_once(&mii->vfs_inode);

	return &mii->vfs_inode;
}

static void minfs_destroy_inode(struct inode *inode)
{
	/* TODO 3/1: free minfs_inode_info */
	kfree(container_of(inode, struct minfs_inode_info, vfs_inode));
}

/*
 * Create a new VFS inode. Do basic initialization and fill imap.
 */

static struct inode *minfs_new_inode(struct inode *dir)
{
	struct super_block *sb = dir->i_sb;
	struct minfs_sb_info *sbi = sb->s_fs_info;
	struct inode *inode;
	int idx;

	/* TODO 7/5: Find first available inode. */
	idx = find_first_zero_bit(&sbi->imap, MINFS_NUM_INODES);
	if (idx < 0) {
		printk(LOG_LEVEL "no space left in imap\n");
		return NULL;
	}

	/* TODO 7/2: Mark the inode as used in the bitmap and mark
	 * the superblock buffer head as dirty.
	 */
	__test_and_set_bit(idx, &sbi->imap);
	mark_buffer_dirty(sbi->sbh);

	/* TODO 7/8: Call new_inode(), fill inode fields
	 * and insert inode into inode hash table.
	 */
	inode = new_inode(sb);
	inode->i_uid = current_fsuid();
	inode->i_gid = current_fsgid();
	inode->i_ino = idx;
	inode->i_mtime = inode->i_atime = inode->i_ctime = current_time(inode);
	inode->i_blocks = 0;

	insert_inode_hash(inode);

	/* Actual writing to the disk will be done in minfs_write_inode,
	 * which will be called at a later time.
	 */

	return inode;
}

/*
 * Add dentry link on parent inode disk structure.
 */

static int minfs_add_link(struct dentry *dentry, struct inode *inode)
{
	struct buffer_head *bh;
	struct inode *dir;
	struct super_block *sb;
	struct minfs_inode_info *mii;
	struct minfs_dir_entry *de;
	int i;
	int err = 0;

	/* TODO 7/3: Get: directory inode (in inode); containing inode (in mii); superblock (in sb). */
	dir = dentry->d_parent->d_inode;
	mii = container_of(dir, struct minfs_inode_info, vfs_inode);
	sb = dir->i_sb;

	/* TODO 7/1: Read dir data block (use sb_bread). */
	bh = sb_bread(sb, mii->data_block);

	/* TODO 7/10: Find first free dentry (de->ino == 0). */
	for (i = 0; i < MINFS_NUM_ENTRIES; i++) {
		de = (struct minfs_dir_entry *) bh->b_data + i;
		if (de->ino == 0)
			break;
	}

	if (i == MINFS_NUM_ENTRIES) {
		err = -ENOSPC;
		goto out;
	}

	/* TODO 7/5: Place new entry in the available slot. Mark buffer_head
	 * as dirty. */
	de->ino = inode->i_ino;
	memcpy(de->name, dentry->d_name.name, MINFS_NAME_LEN);
	dir->i_mtime = dir->i_ctime = current_time(inode);

	mark_buffer_dirty(bh);

out:
	brelse(bh);

	return err;
}

/*
 * Create a VFS file inode. Use minfs_file_... operations.
 */

static int minfs_create(struct inode *dir, struct dentry *dentry, umode_t mode,
		bool excl)
{
	struct inode *inode;
	struct minfs_inode_info *mii;
	int err;

	inode = minfs_new_inode(dir);
	if (inode == NULL) {
		printk(LOG_LEVEL "error allocating new inode\n");
		err = -ENOMEM;
		goto err_new_inode;
	}

	inode->i_mode = mode;
	inode->i_op = &minfs_file_inode_operations;
	inode->i_fop = &minfs_file_operations;
	mii = container_of(inode, struct minfs_inode_info, vfs_inode);
	mii->data_block = MINFS_FIRST_DATA_BLOCK + inode->i_ino;

	err = minfs_add_link(dentry, inode);
	if (err != 0)
		goto err_add_link;

	d_instantiate(dentry, inode);
	mark_inode_dirty(inode);

	printk(KERN_DEBUG "new file inode created (ino = %lu)\n",
		inode->i_ino);

	return 0;

err_add_link:
	inode_dec_link_count(inode);
	iput(inode);
err_new_inode:
	return err;
}

/*
 * Write VFS inode contents to disk inode.
 */

static int minfs_write_inode(struct inode *inode,
		struct writeback_control *wbc)
{
	struct super_block *sb = inode->i_sb;
	struct minfs_inode *mi;
	struct minfs_inode_info *mii = container_of(inode,
			struct minfs_inode_info, vfs_inode);
	struct buffer_head *bh;
	int err = 0;

	bh = sb_bread(sb, MINFS_INODE_BLOCK);
	if (bh == NULL) {
		printk(LOG_LEVEL "could not read block\n");
		err = -ENOMEM;
		goto out;
	}

	mi = (struct minfs_inode *) bh->b_data + inode->i_ino;

	/* fill disk inode */
	mi->mode = inode->i_mode;
	i_uid_write(inode, mi->uid);
	i_gid_write(inode, mi->gid);
	mi->size = inode->i_size;
	mi->data_block = mii->data_block;

	printk(KERN_DEBUG "mode is %05o; data_block is %d\n", mi->mode,
		mii->data_block);

	mark_buffer_dirty(bh);
	brelse(bh);

	printk(KERN_DEBUG "wrote inode %lu\n", inode->i_ino);

out:
	return err;
}

static void minfs_put_super(struct super_block *sb)
{
	struct minfs_sb_info *sbi = sb->s_fs_info;

	/* Free superblock buffer head. */
	mark_buffer_dirty(sbi->sbh);
	brelse(sbi->sbh);

	printk(KERN_DEBUG "released superblock resources\n");
}

static const struct super_operations minfs_ops = {
	.statfs		= simple_statfs,
	.put_super	= minfs_put_super,
	/* TODO 4/2: add alloc and destroy inode functions */
	.alloc_inode	= minfs_alloc_inode,
	.destroy_inode	= minfs_destroy_inode,
	/* TODO 7/1:	= set write_inode function. */
	.write_inode	= minfs_write_inode,
};

static int minfs_fill_super(struct super_block *s, void *data, int silent)
{
	struct minfs_sb_info *sbi;
	struct minfs_super_block *ms;
	struct inode *root_inode;
	struct dentry *root_dentry;
	struct buffer_head *bh;
	int ret = -EINVAL;

	sbi = kzalloc(sizeof(struct minfs_sb_info), GFP_KERNEL);
	if (!sbi)
		return -ENOMEM;
	s->s_fs_info = sbi;

	/* Set block size for superblock. */
	if (!sb_set_blocksize(s, MINFS_BLOCK_SIZE))
		goto out_bad_blocksize;

	/* TODO 2/3: Read block with superblock. It's the first block on
	 * the device, i.e. the block with the index 0. This is the index
	 * to be passed to sb_bread().
	 */
	bh = sb_bread(s, MINFS_SUPER_BLOCK);
	if (bh == NULL)
		goto out_bad_sb;

	/* TODO 2/1: interpret read data as minfs_super_block */
	ms = (struct minfs_super_block *) bh->b_data;

	/* TODO 2/2: check magic number with value defined in minfs.h. jump to out_bad_magic if not suitable */
	if (ms->magic != MINFS_MAGIC)
		goto out_bad_magic;

	/* TODO 2/2: fill super_block with magic_number, super_operations */
	s->s_magic = MINFS_MAGIC;
	s->s_op = &minfs_ops;

	/* TODO 2/2: Fill sbi with rest of information from disk superblock
	 * (i.e. version).
	 */
	sbi->version = ms->version;
	sbi->imap = ms->imap;

	/* allocate root inode and root dentry */
	/* TODO 2/0: use myfs_get_inode instead of minfs_iget */
	root_inode = minfs_iget(s, MINFS_ROOT_INODE);
	if (!root_inode)
		goto out_bad_inode;

	root_dentry = d_make_root(root_inode);
	if (!root_dentry)
		goto out_iput;
	s->s_root = root_dentry;

	/* Store superblock buffer_head for further use. */
	sbi->sbh = bh;

	return 0;

out_iput:
	iput(root_inode);
out_bad_inode:
	printk(LOG_LEVEL "bad inode\n");
out_bad_magic:
	printk(LOG_LEVEL "bad magic number\n");
	brelse(bh);
out_bad_sb:
	printk(LOG_LEVEL "error reading buffer_head\n");
out_bad_blocksize:
	printk(LOG_LEVEL "bad block size\n");
	s->s_fs_info = NULL;
	kfree(sbi);
	return ret;
}

static struct dentry *minfs_mount(struct file_system_type *fs_type,
		int flags, const char *dev_name, void *data)
{
	/* TODO 1/1: call superblock mount function */
	return mount_bdev(fs_type, flags, dev_name, data, minfs_fill_super);
}

static struct file_system_type minfs_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "minfs",
	/* TODO 1/3: add mount, kill_sb and fs_flags */
	.mount		= minfs_mount,
	.kill_sb	= kill_block_super,
	.fs_flags	= FS_REQUIRES_DEV,
};

static int __init minfs_init(void)
{
	int err;

	err = register_filesystem(&minfs_fs_type);
	if (err) {
		printk(LOG_LEVEL "register_filesystem failed\n");
		return err;
	}

	return 0;
}

static void __exit minfs_exit(void)
{
	unregister_filesystem(&minfs_fs_type);
}

module_init(minfs_init);
module_exit(minfs_exit);
