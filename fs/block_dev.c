/*
 *  linux/fs/block_dev.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *  Copyright (C) 2001  Andrea Arcangeli <andrea@suse.de> SuSE
 */

#include <linux/init.h>
#include <linux/mm.h>
#include <linux/fcntl.h>
#include <linux/slab.h>
#include <linux/kmod.h>
#include <linux/major.h>
#include <linux/smp_lock.h>
#include <linux/device_cgroup.h>
#include <linux/highmem.h>
#include <linux/blkdev.h>
#include <linux/module.h>
#include <linux/blkpg.h>
#include <linux/buffer_head.h>
#include <linux/pagevec.h>
#include <linux/writeback.h>
#include <linux/mpage.h>
#include <linux/mount.h>
#include <linux/uio.h>
#include <linux/namei.h>
#include <linux/log2.h>
#include <linux/kmemleak.h>
#include <asm/uaccess.h>
#include "internal.h"

struct bdev_inode {
	struct block_device bdev;
	struct inode vfs_inode;
};

static const struct address_space_operations def_blk_aops;

static inline struct bdev_inode *BDEV_I(struct inode *inode)
{
	return container_of(inode, struct bdev_inode, vfs_inode);
}

inline struct block_device *I_BDEV(struct inode *inode)
{
	return &BDEV_I(inode)->bdev;
}

EXPORT_SYMBOL(I_BDEV);

static sector_t max_block(struct block_device *bdev)
{
	sector_t retval = ~((sector_t)0);
	loff_t sz = i_size_read(bdev->bd_inode);

	if (sz) {
		unsigned int size = block_size(bdev);
		unsigned int sizebits = blksize_bits(size);
		retval = (sz >> sizebits);
	}
	return retval;
}

/* Kill _all_ buffers and pagecache , dirty or not.. */
static void kill_bdev(struct block_device *bdev)
{
	if (bdev->bd_inode->i_mapping->nrpages == 0)
		return;
	invalidate_bh_lrus();
	truncate_inode_pages(bdev->bd_inode->i_mapping, 0);
}	

int set_blocksize(struct block_device *bdev, int size)
{
	/* Size must be a power of two, and between 512 and PAGE_SIZE */
	if (size > PAGE_SIZE || size < 512 || !is_power_of_2(size))
		return -EINVAL;

	/* Size cannot be smaller than the size supported by the device */
	if (size < bdev_logical_block_size(bdev))
		return -EINVAL;

	/* Don't change the size if it is same as current */
	if (bdev->bd_block_size != size) {
		sync_blockdev(bdev);
		bdev->bd_block_size = size;
		bdev->bd_inode->i_blkbits = blksize_bits(size);
		kill_bdev(bdev);
	}
	return 0;
}

EXPORT_SYMBOL(set_blocksize);

int sb_set_blocksize(struct super_block *sb, int size)
{
	if (set_blocksize(sb->s_bdev, size))
		return 0;
	/* If we get here, we know size is power of two
	 * and it's value is between 512 and PAGE_SIZE */
	sb->s_blocksize = size;
	sb->s_blocksize_bits = blksize_bits(size);
	return sb->s_blocksize;
}

EXPORT_SYMBOL(sb_set_blocksize);

int sb_min_blocksize(struct super_block *sb, int size)
{
	int minsize = bdev_logical_block_size(sb->s_bdev);
	if (size < minsize)
		size = minsize;
	return sb_set_blocksize(sb, size);
}

EXPORT_SYMBOL(sb_min_blocksize);

static int
blkdev_get_block(struct inode *inode, sector_t iblock,
		struct buffer_head *bh, int create)
{
	if (iblock >= max_block(I_BDEV(inode))) {
		if (create)
			return -EIO;

		/*
		 * for reads, we're just trying to fill a partial page.
		 * return a hole, they will have to call get_block again
		 * before they can fill it, and they will get -EIO at that
		 * time
		 */
		return 0;
	}
	bh->b_bdev = I_BDEV(inode);
	bh->b_blocknr = iblock;
	set_buffer_mapped(bh);
	return 0;
}

static int
blkdev_get_blocks(struct inode *inode, sector_t iblock,
		struct buffer_head *bh, int create)
{
	sector_t end_block = max_block(I_BDEV(inode));
	unsigned long max_blocks = bh->b_size >> inode->i_blkbits;

	if ((iblock + max_blocks) > end_block) {
		max_blocks = end_block - iblock;
		if ((long)max_blocks <= 0) {
			if (create)
				return -EIO;	/* write fully beyond EOF */
			/*
			 * It is a read which is fully beyond EOF.  We return
			 * a !buffer_mapped buffer
			 */
			max_blocks = 0;
		}
	}

	bh->b_bdev = I_BDEV(inode);
	bh->b_blocknr = iblock;
	bh->b_size = max_blocks << inode->i_blkbits;
	if (max_blocks)
		set_buffer_mapped(bh);
	return 0;
}

static ssize_t
blkdev_direct_IO(int rw, struct kiocb *iocb, const struct iovec *iov,
			loff_t offset, unsigned long nr_segs)
{
	struct file *file = iocb->ki_filp;
	struct inode *inode = file->f_mapping->host;

	return blockdev_direct_IO_no_locking(rw, iocb, inode, I_BDEV(inode),
				iov, offset, nr_segs, blkdev_get_blocks, NULL);
}

int __sync_blockdev(struct block_device *bdev, int wait)
{
	if (!bdev)
		return 0;
	if (!wait)
		return filemap_flush(bdev->bd_inode->i_mapping);
	return filemap_write_and_wait(bdev->bd_inode->i_mapping);
}

/*
 * Write out and wait upon all the dirty data associated with a block
 * device via its mapping.  Does not take the superblock lock.
 */
int sync_blockdev(struct block_device *bdev)
{
	return __sync_blockdev(bdev, 1);
}
EXPORT_SYMBOL(sync_blockdev);

/*
 * Write out and wait upon all dirty data associated with this
 * device.   Filesystem data as well as the underlying block
 * device.  Takes the superblock lock.
 */
int fsync_bdev(struct block_device *bdev)
{
	struct super_block *sb = get_super(bdev);
	if (sb) {
		int res = sync_filesystem(sb);
		drop_super(sb);
		return res;
	}
	return sync_blockdev(bdev);
}
EXPORT_SYMBOL(fsync_bdev);

/**
 * freeze_bdev  --  lock a filesystem and force it into a consistent state
 * @bdev:	blockdevice to lock
 *
 * If a superblock is found on this device, we take the s_umount semaphore
 * on it to make sure nobody unmounts until the snapshot creation is done.
 * The reference counter (bd_fsfreeze_count) guarantees that only the last
 * unfreeze process can unfreeze the frozen filesystem actually when multiple
 * freeze requests arrive simultaneously. It counts up in freeze_bdev() and
 * count down in thaw_bdev(). When it becomes 0, thaw_bdev() will unfreeze
 * actually.
 */
struct super_block *freeze_bdev(struct block_device *bdev)
{
	struct super_block *sb;
	int error = 0;

	mutex_lock(&bdev->bd_fsfreeze_mutex);
	if (++bdev->bd_fsfreeze_count > 1) {
		/*
		 * We don't even need to grab a reference - the first call
		 * to freeze_bdev grab an active reference and only the last
		 * thaw_bdev drops it.
		 */
		sb = get_super(bdev);
		drop_super(sb);
		mutex_unlock(&bdev->bd_fsfreeze_mutex);
		return sb;
	}

	sb = get_active_super(bdev);
	if (!sb)
		goto out;
	if (sb->s_flags & MS_RDONLY) {
		sb->s_frozen = SB_FREEZE_TRANS;
		up_write(&sb->s_umount);
		mutex_unlock(&bdev->bd_fsfreeze_mutex);
		return sb;
	}

	sb->s_frozen = SB_FREEZE_WRITE;
	smp_wmb();

	sync_filesystem(sb);

	sb->s_frozen = SB_FREEZE_TRANS;
	smp_wmb();

	sync_blockdev(sb->s_bdev);

	if (sb->s_op->freeze_fs) {
		error = sb->s_op->freeze_fs(sb);
		if (error) {
			printk(KERN_ERR
				"VFS:Filesystem freeze failed\n");
			sb->s_frozen = SB_UNFROZEN;
			deactivate_locked_super(sb);
			bdev->bd_fsfreeze_count--;
			mutex_unlock(&bdev->bd_fsfreeze_mutex);
			return ERR_PTR(error);
		}
	}
	up_write(&sb->s_umount);

 out:
	sync_blockdev(bdev);
	mutex_unlock(&bdev->bd_fsfreeze_mutex);
	return sb;	/* thaw_bdev releases s->s_umount */
}
EXPORT_SYMBOL(freeze_bdev);

/**
 * thaw_bdev  -- unlock filesystem
 * @bdev:	blockdevice to unlock
 * @sb:		associated superblock
 *
 * Unlocks the filesystem and marks it writeable again after freeze_bdev().
 */
int thaw_bdev(struct block_device *bdev, struct super_block *sb)
{
	int error = -EINVAL;

	mutex_lock(&bdev->bd_fsfreeze_mutex);
	if (!bdev->bd_fsfreeze_count)
		goto out_unlock;

	error = 0;
	if (--bdev->bd_fsfreeze_count > 0)
		goto out_unlock;

	if (!sb)
		goto out_unlock;

	BUG_ON(sb->s_bdev != bdev);
	down_write(&sb->s_umount);
	if (sb->s_flags & MS_RDONLY)
		goto out_unfrozen;

	if (sb->s_op->unfreeze_fs) {
		error = sb->s_op->unfreeze_fs(sb);
		if (error) {
			printk(KERN_ERR
				"VFS:Filesystem thaw failed\n");
			sb->s_frozen = SB_FREEZE_TRANS;
			bdev->bd_fsfreeze_count++;
			mutex_unlock(&bdev->bd_fsfreeze_mutex);
			return error;
		}
	}

out_unfrozen:
	sb->s_frozen = SB_UNFROZEN;
	smp_wmb();
	wake_up(&sb->s_wait_unfrozen);

	if (sb)
		deactivate_locked_super(sb);
out_unlock:
	mutex_unlock(&bdev->bd_fsfreeze_mutex);
	return 0;
}
EXPORT_SYMBOL(thaw_bdev);

static int blkdev_writepage(struct page *page, struct writeback_control *wbc)
{
	return block_write_full_page(page, blkdev_get_block, wbc);
}

static int blkdev_readpage(struct file * file, struct page * page)
{
	return block_read_full_page(page, blkdev_get_block);
}

static int blkdev_write_begin(struct file *file, struct address_space *mapping,
			loff_t pos, unsigned len, unsigned flags,
			struct page **pagep, void **fsdata)
{
	*pagep = NULL;
	return block_write_begin(file, mapping, pos, len, flags, pagep, fsdata,
				blkdev_get_block);
}

static int blkdev_write_end(struct file *file, struct address_space *mapping,
			loff_t pos, unsigned len, unsigned copied,
			struct page *page, void *fsdata)
{
	int ret;
	ret = block_write_end(file, mapping, pos, len, copied, page, fsdata);

	unlock_page(page);
	page_cache_release(page);

	return ret;
}

/*
 * private llseek:
 * for a block special file file->f_path.dentry->d_inode->i_size is zero
 * so we compute the size by hand (just as in block_read/write above)
 */
static loff_t block_llseek(struct file *file, loff_t offset, int origin)
{
	struct inode *bd_inode = file->f_mapping->host;
	loff_t size;
	loff_t retval;

	mutex_lock(&bd_inode->i_mutex);
	size = i_size_read(bd_inode);

	switch (origin) {
		case 2:
			offset += size;
			break;
		case 1:
			offset += file->f_pos;
	}
	retval = -EINVAL;
	if (offset >= 0 && offset <= size) {
		if (offset != file->f_pos) {
			file->f_pos = offset;
		}
		retval = offset;
	}
	mutex_unlock(&bd_inode->i_mutex);
	return retval;
}
	
/*
 *	Filp is never NULL; the only case when ->fsync() is called with
 *	NULL first argument is nfsd_sync_dir() and that's not a directory.
 */
 
static int block_fsync(struct file *filp, struct dentry *dentry, int datasync)
{
	struct block_device *bdev = I_BDEV(filp->f_mapping->host);
	int error;

	error = sync_blockdev(bdev);
	if (error)
		return error;
	
	error = blkdev_issue_flush(bdev, NULL);
	if (error == -EOPNOTSUPP)
		error = 0;
	return error;
}

/*
 * pseudo-fs
 */

static  __cacheline_aligned_in_smp DEFINE_SPINLOCK(bdev_lock);
static struct kmem_cache * bdev_cachep __read_mostly;

static struct inode *bdev_alloc_inode(struct super_block *sb)
{
	struct bdev_inode *ei = kmem_cache_alloc(bdev_cachep, GFP_KERNEL);
	if (!ei)
		return NULL;
	return &ei->vfs_inode;
}

static void bdev_destroy_inode(struct inode *inode)
{
	struct bdev_inode *bdi = BDEV_I(inode);

	kmem_cache_free(bdev_cachep, bdi);
}

static void init_once(void *foo)
{
	struct bdev_inode *ei = (struct bdev_inode *) foo;
	struct block_device *bdev = &ei->bdev;

	memset(bdev, 0, sizeof(*bdev));
	mutex_init(&bdev->bd_mutex);
	INIT_LIST_HEAD(&bdev->bd_inodes);
	INIT_LIST_HEAD(&bdev->bd_list);
#ifdef CONFIG_SYSFS
	INIT_LIST_HEAD(&bdev->bd_holder_list);
#endif
	inode_init_once(&ei->vfs_inode);
	/* Initialize mutex for freeze. */
	mutex_init(&bdev->bd_fsfreeze_mutex);
}

static inline void __bd_forget(struct inode *inode)
{
	list_del_init(&inode->i_devices);
	inode->i_bdev = NULL;
	inode->i_mapping = &inode->i_data;
}

static void bdev_clear_inode(struct inode *inode)
{
	struct block_device *bdev = &BDEV_I(inode)->bdev;
	struct list_head *p;
	spin_lock(&bdev_lock);
	while ( (p = bdev->bd_inodes.next) != &bdev->bd_inodes ) {
		__bd_forget(list_entry(p, struct inode, i_devices));
	}
	list_del_init(&bdev->bd_list);
	spin_unlock(&bdev_lock);
}

static const struct super_operations bdev_sops = {
	.statfs = simple_statfs,
	.alloc_inode = bdev_alloc_inode,
	.destroy_inode = bdev_destroy_inode,
	.drop_inode = generic_delete_inode,
	.clear_inode = bdev_clear_inode,
};

static int bd_get_sb(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data, struct vfsmount *mnt)
{
	return get_sb_pseudo(fs_type, "bdev:", &bdev_sops, 0x62646576, mnt);
}

static struct file_system_type bd_type = {
	.name		= "bdev",
	.get_sb		= bd_get_sb,
	.kill_sb	= kill_anon_super,
};

struct super_block *blockdev_superblock __read_mostly;

void __init bdev_cache_init(void)
{
	int err;
	struct vfsmount *bd_mnt;

	bdev_cachep = kmem_cache_create("bdev_cache", sizeof(struct bdev_inode),
			0, (SLAB_HWCACHE_ALIGN|SLAB_RECLAIM_ACCOUNT|
				SLAB_MEM_SPREAD|SLAB_PANIC),
			init_once);
	err = register_filesystem(&bd_type);
	if (err)
		panic("Cannot register bdev pseudo-fs");
	bd_mnt = kern_mount(&bd_type);
	if (IS_ERR(bd_mnt))
		panic("Cannot create bdev pseudo-fs");
	/*
	 * This vfsmount structure is only used to obtain the
	 * blockdev_superblock, so tell kmemleak not to report it.
	 */
	kmemleak_not_leak(bd_mnt);
	blockdev_superblock = bd_mnt->mnt_sb;	/* For writeback */
}

/*
 * Most likely _very_ bad one - but then it's hardly critical for small
 * /dev and can be fixed when somebody will need really large one.
 * Keep in mind that it will be fed through icache hash function too.
 */
static inline unsigned long hash(dev_t dev)
{
	return MAJOR(dev)+MINOR(dev);
}

static int bdev_test(struct inode *inode, void *data)
{
	return BDEV_I(inode)->bdev.bd_dev == *(dev_t *)data;
}

static int bdev_set(struct inode *inode, void *data)
{
	BDEV_I(inode)->bdev.bd_dev = *(dev_t *)data;
	return 0;
}

static LIST_HEAD(all_bdevs);

struct block_device *bdget(dev_t dev)
{
	struct block_device *bdev;
	struct inode *inode;

	inode = iget5_locked(blockdev_superblock, hash(dev),
			bdev_test, bdev_set, &dev);

	if (!inode)
		return NULL;

	bdev = &BDEV_I(inode)->bdev;

	if (inode->i_state & I_NEW) {
		bdev->bd_contains = NULL;
		bdev->bd_inode = inode;
		bdev->bd_block_size = (1 << inode->i_blkbits);
		bdev->bd_part_count = 0;
		bdev->bd_invalidated = 0;
		inode->i_mode = S_IFBLK;
		inode->i_rdev = dev;
		inode->i_bdev = bdev;
		inode->i_data.a_ops = &def_blk_aops;
		mapping_set_gfp_mask(&inode->i_data, GFP_USER);
		inode->i_data.backing_dev_info = &default_backing_dev_info;
		spin_lock(&bdev_lock);
		list_add(&bdev->bd_list, &all_bdevs);
		spin_unlock(&bdev_lock);
		unlock_new_inode(inode);
	}
	return bdev;
}

EXPORT_SYMBOL(bdget);

/**
 * bdgrab -- Grab a reference to an already referenced block device
 * @bdev:	Block device to grab a reference to.
 */
struct block_device *bdgrab(struct block_device *bdev)
{
	atomic_inc(&bdev->bd_inode->i_count);
	return bdev;
}

long nr_blockdev_pages(void)
{
	struct block_device *bdev;
	long ret = 0;
	spin_lock(&bdev_lock);
	list_for_each_entry(bdev, &all_bdevs, bd_list) {
		ret += bdev->bd_inode->i_mapping->nrpages;
	}
	spin_unlock(&bdev_lock);
	return ret;
}

void bdput(struct block_device *bdev)
{
	iput(bdev->bd_inode);
}

EXPORT_SYMBOL(bdput);
 
static struct block_device *bd_acquire(struct inode *inode)
{
	struct block_device *bdev;

	spin_lock(&bdev_lock);
	bdev = inode->i_bdev;
	if (bdev) {
		atomic_inc(&bdev->bd_inode->i_count);
		spin_unlock(&bdev_lock);
		return bdev;
	}
	spin_unlock(&bdev_lock);

	bdev = bdget(inode->i_rdev);
	if (bdev) {
		spin_lock(&bdev_lock);
		if (!inode->i_bdev) {
			/*
			 * We take an additional bd_inode->i_count for inode,
			 * and it's released in clear_inode() of inode.
			 * So, we can access it via ->i_mapping always
			 * without igrab().
			 */
			atomic_inc(&bdev->bd_inode->i_count);
			inode->i_bdev = bdev;
			inode->i_mapping = bdev->bd_inode->i_mapping;
			list_add(&inode->i_devices, &bdev->bd_inodes);
		}
		spin_unlock(&bdev_lock);
	}
	return bdev;
}

/* Call when you free inode */

void bd_forget(struct inode *inode)
{
	struct block_device *bdev = NULL;

	spin_lock(&bdev_lock);
	if (inode->i_bdev) {
		if (!sb_is_blkdev_sb(inode->i_sb))
			bdev = inode->i_bdev;
		__bd_forget(inode);
	}
	spin_unlock(&bdev_lock);

	if (bdev)
		iput(bdev->bd_inode);
}

int bd_claim(struct block_device *bdev, void *holder)
{
	int res;
	spin_lock(&bdev_lock);

	/* first decide result */
	if (bdev->bd_holder == holder)
		res = 0;	 /* already a holder */
	else if (bdev->bd_holder != NULL)
		res = -EBUSY; 	 /* held by someone else */
	else if (bdev->bd_contains == bdev)
		res = 0;  	 /* is a whole device which isn't held */

	else if (bdev->bd_contains->bd_holder == bd_claim)
		res = 0; 	 /* is a partition of a device that is being partitioned */
	else if (bdev->bd_contains->bd_holder != NULL)
		res = -EBUSY;	 /* is a partition of a held device */
	else
		res = 0;	 /* is a partition of an un-held device */

	/* now impose change */
	if (res==0) {
		/* note that for a whole device bd_holders
		 * will be incremented twice, and bd_holder will
		 * be set to bd_claim before being set to holder
		 */
		bdev->bd_contains->bd_holders ++;
		bdev->bd_contains->bd_holder = bd_claim;
		bdev->bd_holders++;
		bdev->bd_holder = holder;
	}
	spin_unlock(&bdev_lock);
	return res;
}

EXPORT_SYMBOL(bd_claim);

void bd_release(struct block_device *bdev)
{
	spin_lock(&bdev_lock);
	if (!--bdev->bd_contains->bd_holders)
		bdev->bd_contains->bd_holder = NULL;
	if (!--bdev->bd_holders)
		bdev->bd_holder = NULL;
	spin_unlock(&bdev_lock);
}

EXPORT_SYMBOL(bd_release);

#ifdef CONFIG_SYSFS
/*
 * Functions for bd_claim_by_kobject / bd_release_from_kobject
 *
 *     If a kobject is passed to bd_claim_by_kobject()
 *     and the kobject has a parent directory,
 *     following symlinks are created:
 *        o from the kobject to the claimed bdev
 *        o from "holders" directory of the bdev to the parent of the kobject
 *     bd_release_from_kobject() removes these symlinks.
 *
 *     Example:
 *        If /dev/dm-0 maps to /dev/sda, kobject corresponding to
 *        /sys/block/dm-0/slaves is passed to bd_claim_by_kobject(), then:
 *           /sys/block/dm-0/slaves/sda --> /sys/block/sda
 *           /sys/block/sda/holders/dm-0 --> /sys/block/dm-0
 */

static int add_symlink(struct kobject *from, struct kobject *to)
{
	if (!from || !to)
		return 0;
	return sysfs_create_link(from, to, kobject_name(to));
}

static void del_symlink(struct kobject *from, struct kobject *to)
{
	if (!from || !to)
		return;
	sysfs_remove_link(from, kobject_name(to));
}

/*
 * 'struct bd_holder' contains pointers to kobjects symlinked by
 * bd_claim_by_kobject.
 * It's connected to bd_holder_list which is protected by bdev->bd_sem.
 */
struct bd_holder {
	struct list_head list;	/* chain of holders of the bdev */
	int count;		/* references from the holder */
	struct kobject *sdir;	/* holder object, e.g. "/block/dm-0/slaves" */
	struct kobject *hdev;	/* e.g. "/block/dm-0" */
	struct kobject *hdir;	/* e.g. "/block/sda/holders" */
	struct kobject *sdev;	/* e.g. "/block/sda" */
};

/*
 * Get references of related kobjects at once.
 * Returns 1 on success. 0 on failure.
 *
 * Should call bd_holder_release_dirs() after successful use.
 */
static int bd_holder_grab_dirs(struct block_device *bdev,
			struct bd_holder *bo)
{
	if (!bdev || !bo)
		return 0;

	bo->sdir = kobject_get(bo->sdir);
	if (!bo->sdir)
		return 0;

	bo->hdev = kobject_get(bo->sdir->parent);
	if (!bo->hdev)
		goto fail_put_sdir;

	bo->sdev = kobject_get(&part_to_dev(bdev->bd_part)->kobj);
	if (!bo->sdev)
		goto fail_put_hdev;

	bo->hdir = kobject_get(bdev->bd_part->holder_dir);
	if (!bo->hdir)
		goto fail_put_sdev;

	return 1;

fail_put_sdev:
	kobject_put(bo->sdev);
fail_put_hdev:
	kobject_put(bo->hdev);
fail_put_sdir:
	kobject_put(bo->sdir);

	return 0;
}

/* Put references of related kobjects at once. */
static void bd_holder_release_dirs(struct bd_holder *bo)
{
	kobject_put(bo->hdir);
	kobject_put(bo->sdev);
	kobject_put(bo->hdev);
	kobject_put(bo->sdir);
}

static struct bd_holder *alloc_bd_holder(struct kobject *kobj)
{
	struct bd_holder *bo;

	bo = kzalloc(sizeof(*bo), GFP_KERNEL);
	if (!bo)
		return NULL;

	bo->count = 1;
	bo->sdir = kobj;

	return bo;
}

static void free_bd_holder(struct bd_holder *bo)
{
	kfree(bo);
}

/**
 * find_bd_holder - find matching struct bd_holder from the block device
 *
 * @bdev:	struct block device to be searched
 * @bo:		target struct bd_holder
 *
 * Returns matching entry with @bo in @bdev->bd_holder_list.
 * If found, increment the reference count and return the pointer.
 * If not found, returns NULL.
 */
static struct bd_holder *find_bd_holder(struct block_device *bdev,
					struct bd_holder *bo)
{
	struct bd_holder *tmp;

	list_for_each_entry(tmp, &bdev->bd_holder_list, list)
		if (tmp->sdir == bo->sdir) {
			tmp->count++;
			return tmp;
		}

	return NULL;
}

/**
 * add_bd_holder - create sysfs symlinks for bd_claim() relationship
 *
 * @bdev:	block device to be bd_claimed
 * @bo:		preallocated and initialized by alloc_bd_holder()
 *
 * Add @bo to @bdev->bd_holder_list, create symlinks.
 *
 * Returns 0 if symlinks are created.
 * Returns -ve if something fails.
 */
static int add_bd_holder(struct block_device *bdev, struct bd_holder *bo)
{
	int err;

	if (!bo)
		return -EINVAL;

	if (!bd_holder_grab_dirs(bdev, bo))
		return -EBUSY;

	err = add_symlink(bo->sdir, bo->sdev);
	if (err)
		return err;

	err = add_symlink(bo->hdir, bo->hdev);
	if (err) {
		del_symlink(bo->sdir, bo->sdev);
		return err;
	}

	list_add_tail(&bo->list, &bdev->bd_holder_list);
	return 0;
}

/**
 * del_bd_holder - delete sysfs symlinks for bd_claim() relationship
 *
 * @bdev:	block device to be bd_claimed
 * @kobj:	holder's kobject
 *
 * If there is matching entry with @kobj in @bdev->bd_holder_list
 * and no other bd_claim() from the same kobject,
 * remove the struct bd_holder from the list, delete symlinks for it.
 *
 * Returns a pointer to the struct bd_holder when it's removed from the list
 * and ready to be freed.
 * Returns NULL if matching claim isn't found or there is other bd_claim()
 * by the same kobject.
 */
static struct bd_holder *del_bd_holder(struct block_device *bdev,
					struct kobject *kobj)
{
	struct bd_holder *bo;

	list_for_each_entry(bo, &bdev->bd_holder_list, list) {
		if (bo->sdir == kobj) {
			bo->count--;
			BUG_ON(bo->count < 0);
			if (!bo->count) {
				list_del(&bo->list);
				del_symlink(bo->sdir, bo->sdev);
				del_symlink(bo->hdir, bo->hdev);
				bd_holder_release_dirs(bo);
				return bo;
			}
			break;
		}
	}

	return NULL;
}

/**
 * bd_claim_by_kobject - bd_claim() with additional kobject signature
 *
 * @bdev:	block device to be claimed
 * @holder:	holder's signature
 * @kobj:	holder's kobject
 *
 * Do bd_claim() and if it succeeds, create sysfs symlinks between
 * the bdev and the holder's kobject.
 * Use bd_release_from_kobject() when relesing the claimed bdev.
 *
 * Returns 0 on success. (same as bd_claim())
 * Returns errno on failure.
 */
static int bd_claim_by_kobject(struct block_device *bdev, void *holder,
				struct kobject *kobj)
{
	int err;
	struct bd_holder *bo, *found;

	if (!kobj)
		return -EINVAL;

	bo = alloc_bd_holder(kobj);
	if (!bo)
		return -ENOMEM;

	mutex_lock(&bdev->bd_mutex);

	err = bd_claim(bdev, holder);
	if (err)
		goto fail;

	found = find_bd_holder(bdev, bo);
	if (found)
		goto fail;

	err = add_bd_holder(bdev, bo);
	if (err)
		bd_release(bdev);
	else
		bo = NULL;
fail:
	mutex_unlock(&bdev->bd_mutex);
	free_bd_holder(bo);
	return err;
}

/**
 * bd_release_from_kobject - bd_release() with additional kobject signature
 *
 * @bdev:	block device to be released
 * @kobj:	holder's kobject
 *
 * Do bd_release() and remove sysfs symlinks created by bd_claim_by_kobject().
 */
static void bd_release_from_kobject(struct block_device *bdev,
					struct kobject *kobj)
{
	if (!kobj)
		return;

	mutex_lock(&bdev->bd_mutex);
	bd_release(bdev);
	free_bd_holder(del_bd_holder(bdev, kobj));
	mutex_unlock(&bdev->bd_mutex);
}

/**
 * bd_claim_by_disk - wrapper function for bd_claim_by_kobject()
 *
 * @bdev:	block device to be claimed
 * @holder:	holder's signature
 * @disk:	holder's gendisk
 *
 * Call bd_claim_by_kobject() with getting @disk->slave_dir.
 */
int bd_claim_by_disk(struct block_device *bdev, void *holder,
			struct gendisk *disk)
{
	return bd_claim_by_kobject(bdev, holder, kobject_get(disk->slave_dir));
}
EXPORT_SYMBOL_GPL(bd_claim_by_disk);

/**
 * bd_release_from_disk - wrapper function for bd_release_from_kobject()
 *
 * @bdev:	block device to be claimed
 * @disk:	holder's gendisk
 *
 * Call bd_release_from_kobject() and put @disk->slave_dir.
 */
void bd_release_from_disk(struct block_device *bdev, struct gendisk *disk)
{
	bd_release_from_kobject(bdev, disk->slave_dir);
	kobject_put(disk->slave_dir);
}
EXPORT_SYMBOL_GPL(bd_release_from_disk);
#endif

/*
 * Tries to open block device by device number.  Use it ONLY if you
 * really do not have anything better - i.e. when you are behind a
 * truly sucky interface and all you are given is a device number.  _Never_
 * to be used for internal purposes.  If you ever need it - reconsider
 * your API.
 */
struct block_device *open_by_devnum(dev_t dev, fmode_t mode)
{
	struct block_device *bdev = bdget(dev);
	int err = -ENOMEM;
	if (bdev)
		err = blkdev_get(bdev, mode);
	return err ? ERR_PTR(err) : bdev;
}

EXPORT_SYMBOL(open_by_devnum);

/**
 * flush_disk - invalidates all buffer-cache entries on a disk
 *
 * @bdev:      struct block device to be flushed
 *
 * Invalidates all buffer-cache entries on a disk. It should be called
 * when a disk has been changed -- either by a media change or online
 * resize.
 */
static void flush_disk(struct block_device *bdev)
{
	if (__invalidate_device(bdev)) {
		char name[BDEVNAME_SIZE] = "";

		if (bdev->bd_disk)
			disk_name(bdev->bd_disk, 0, name);
		printk(KERN_WARNING "VFS: busy inodes on changed media or "
		       "resized disk %s\n", name);
	}

	if (!bdev->bd_disk)
		return;
	if (disk_partitionable(bdev->bd_disk))
		bdev->bd_invalidated = 1;
}

/**
 * check_disk_size_change - checks for disk size change and adjusts bdev size.
 * @disk: struct gendisk to check
 * @bdev: struct bdev to adjust.
 *
 * This routine checks to see if the bdev size does not match the disk size
 * and adjusts it if it differs.
 */
void check_disk_size_change(struct gendisk *disk, struct block_device *bdev)
{
	loff_t disk_size, bdev_size;

	disk_size = (loff_t)get_capacity(disk) << 9;
	bdev_size = i_size_read(bdev->bd_inode);
	if (disk_size != bdev_size) {
		char name[BDEVNAME_SIZE];

		disk_name(disk, 0, name);
		printk(KERN_INFO
		       "%s: detected capacity change from %lld to %lld\n",
		       name, bdev_size, disk_size);
		i_size_write(bdev->bd_inode, disk_size);
		flush_disk(bdev);
	}
}
EXPORT_SYMBOL(check_disk_size_change);

/**
 * revalidate_disk - wrapper for lower-level driver's revalidate_disk call-back
 * @disk: struct gendisk to be revalidated
 *
 * This routine is a wrapper for lower-level driver's revalidate_disk
 * call-backs.  It is used to do common pre and post operations needed
 * for all revalidate_disk operations.
 */
int revalidate_disk(struct gendisk *disk)
{
	struct block_device *bdev;
	int ret = 0;

	if (disk->fops->revalidate_disk)
		ret = disk->fops->revalidate_disk(disk);

	bdev = bdget_disk(disk, 0);
	if (!bdev)
		return ret;

	mutex_lock(&bdev->bd_mutex);
	check_disk_size_change(disk, bdev);
	mutex_unlock(&bdev->bd_mutex);
	bdput(bdev);
	return ret;
}
EXPORT_SYMBOL(revalidate_disk);

/*
 * This routine checks whether a removable media has been changed,
 * and invalidates all buffer-cache-entries in that case. This
 * is a relatively slow routine, so we have to try to minimize using
 * it. Thus it is called only upon a 'mount' or 'open'. This
 * is the best way of combining speed and utility, I think.
 * People changing diskettes in the middle of an operation deserve
 * to lose :-)
 */
int check_disk_change(struct block_device *bdev)
{
	struct gendisk *disk = bdev->bd_disk;
	const struct block_device_operations *bdops = disk->fops;

	if (!bdops->media_changed)
		return 0;
	if (!bdops->media_changed(bdev->bd_disk))
		return 0;

	flush_disk(bdev);
	if (bdops->revalidate_disk)
		bdops->revalidate_disk(bdev->bd_disk);
	return 1;
}

EXPORT_SYMBOL(check_disk_change);

void bd_set_size(struct block_device *bdev, loff_t size)
{
	unsigned bsize = bdev_logical_block_size(bdev);

	bdev->bd_inode->i_size = size;
	while (bsize < PAGE_CACHE_SIZE) {
		if (size & bsize)
			break;
		bsize <<= 1;
	}
	bdev->bd_block_size = bsize;
	bdev->bd_inode->i_blkbits = blksize_bits(bsize);
}
EXPORT_SYMBOL(bd_set_size);

static int __blkdev_put(struct block_device *bdev, fmode_t mode, int for_part);

/*
 * bd_mutex locking:
 *
 *  mutex_lock(part->bd_mutex)
 *    mutex_lock_nested(whole->bd_mutex, 1)
 */

static int __blkdev_get(struct block_device *bdev, fmode_t mode, int for_part)
{
	struct gendisk *disk;
	int ret;
	int partno;
	int perm = 0;

	if (mode & FMODE_READ)
		perm |= MAY_READ;
	if (mode & FMODE_WRITE)
		perm |= MAY_WRITE;
	/*
	 * hooks: /n/, see "layering violations".
	 */
	ret = devcgroup_inode_permission(bdev->bd_inode, perm);
	if (ret != 0) {
		bdput(bdev);
		return ret;
	}

	lock_kernel();
 restart:

	ret = -ENXIO;
	disk = get_gendisk(bdev->bd_dev, &partno);
	if (!disk)
		goto out_unlock_kernel;

	mutex_lock_nested(&bdev->bd_mutex, for_part);
	if (!bdev->bd_openers) {
		bdev->bd_disk = disk;
		bdev->bd_contains = bdev;
		if (!partno) {
			struct backing_dev_info *bdi;

			ret = -ENXIO;
			bdev->bd_part = disk_get_part(disk, partno);
			if (!bdev->bd_part)
				goto out_clear;

			if (disk->fops->open) {
				ret = disk->fops->open(bdev, mode);
				if (ret == -ERESTARTSYS) {
					/* Lost a race with 'disk' being
					 * deleted, try again.
					 * See md.c
					 */
					disk_put_part(bdev->bd_part);
					bdev->bd_part = NULL;
					module_put(disk->fops->owner);
					put_disk(disk);
					bdev->bd_disk = NULL;
					mutex_unlock(&bdev->bd_mutex);
					goto restart;
				}
				if (ret)
					goto out_clear;
			}
			if (!bdev->bd_openers) {
				bd_set_size(bdev,(loff_t)get_capacity(disk)<<9);
				bdi = blk_get_backing_dev_info(bdev);
				if (bdi == NULL)
					bdi = &default_backing_dev_info;
				bdev->bd_inode->i_data.backing_dev_info = bdi;
			}
			if (bdev->bd_invalidated)
				rescan_partitions(disk, bdev);
		} else {
			struct block_device *whole;
			whole = bdget_disk(disk, 0);
			ret = -ENOMEM;
			if (!whole)
				goto out_clear;
			BUG_ON(for_part);
			ret = __blkdev_get(whole, mode, 1);
			if (ret)
				goto out_clear;
			bdev->bd_contains = whole;
			bdev->bd_inode->i_data.backing_dev_info =
			   whole->bd_inode->i_data.backing_dev_info;
			bdev->bd_part = disk_get_part(disk, partno);
			if (!(disk->flags & GENHD_FL_UP) ||
			    !bdev->bd_part || !bdev->bd_part->nr_sects) {
				ret = -ENXIO;
				goto out_clear;
			}
			bd_set_size(bdev, (loff_t)bdev->bd_part->nr_sects << 9);
		}
	} else {
		module_put(disk->fops->owner);
		put_disk(disk);
		disk = NULL;
		if (bdev->bd_contains == bdev) {
			if (bdev->bd_disk->fops->open) {
				ret = bdev->bd_disk->fops->open(bdev, mode);
				if (ret)
					goto out_unlock_bdev;
			}
			if (bdev->bd_invalidated)
				rescan_partitions(bdev->bd_disk, bdev);
		}
	}
	bdev->bd_openers++;
	if (for_part)
		bdev->bd_part_count++;
	mutex_unlock(&bdev->bd_mutex);
	unlock_kernel();
	return 0;

 out_clear:
	disk_put_part(bdev->bd_part);
	bdev->bd_disk = NULL;
	bdev->bd_part = NULL;
	bdev->bd_inode->i_data.backing_dev_info = &default_backing_dev_info;
	if (bdev != bdev->bd_contains)
		__blkdev_put(bdev->bd_contains, mode, 1);
	bdev->bd_contains = NULL;
 out_unlock_bdev:
	mutex_unlock(&bdev->bd_mutex);
 out_unlock_kernel:
	unlock_kernel();

	if (disk)
		module_put(disk->fops->owner);
	put_disk(disk);
	bdput(bdev);

	return ret;
}

int blkdev_get(struct block_device *bdev, fmode_t mode)
{
	return __blkdev_get(bdev, mode, 0);
}
EXPORT_SYMBOL(blkdev_get);

static int blkdev_open(struct inode * inode, struct file * filp)
{
	struct block_device *bdev;
	int res;

	/*
	 * Preserve backwards compatibility and allow large file access
	 * even if userspace doesn't ask for it explicitly. Some mkfs
	 * binary needs it. We might want to drop this workaround
	 * during an unstable branch.
	 */
	filp->f_flags |= O_LARGEFILE;

	if (filp->f_flags & O_NDELAY)
		filp->f_mode |= FMODE_NDELAY;
	if (filp->f_flags & O_EXCL)
		filp->f_mode |= FMODE_EXCL;
	if ((filp->f_flags & O_ACCMODE) == 3)
		filp->f_mode |= FMODE_WRITE_IOCTL;

	bdev = bd_acquire(inode);
	if (bdev == NULL)
		return -ENOMEM;

	filp->f_mapping = bdev->bd_inode->i_mapping;

	res = blkdev_get(bdev, filp->f_mode);
	if (res)
		return res;

	if (filp->f_mode & FMODE_EXCL) {
		res = bd_claim(bdev, filp);
		if (res)
			goto out_blkdev_put;
	}

	return 0;

 out_blkdev_put:
	blkdev_put(bdev, filp->f_mode);
	return res;
}

static int __blkdev_put(struct block_device *bdev, fmode_t mode, int for_part)
{
	int ret = 0;
	struct gendisk *disk = bdev->bd_disk;
	struct block_device *victim = NULL;

	mutex_lock_nested(&bdev->bd_mutex, for_part);
	lock_kernel();
	if (for_part)
		bdev->bd_part_count--;

	if (!--bdev->bd_openers) {
		sync_blockdev(bdev);
		kill_bdev(bdev);
	}
	if (bdev->bd_contains == bdev) {
		if (disk->fops->release)
			ret = disk->fops->release(disk, mode);
	}
	if (!bdev->bd_openers) {
		struct module *owner = disk->fops->owner;

		put_disk(disk);
		module_put(owner);
		disk_put_part(bdev->bd_part);
		bdev->bd_part = NULL;
		bdev->bd_disk = NULL;
		bdev->bd_inode->i_data.backing_dev_info = &default_backing_dev_info;
		if (bdev != bdev->bd_contains)
			victim = bdev->bd_contains;
		bdev->bd_contains = NULL;
	}
	unlock_kernel();
	mutex_unlock(&bdev->bd_mutex);
	bdput(bdev);
	if (victim)
		__blkdev_put(victim, mode, 1);
	return ret;
}

int blkdev_put(struct block_device *bdev, fmode_t mode)
{
	return __blkdev_put(bdev, mode, 0);
}
EXPORT_SYMBOL(blkdev_put);

static int blkdev_close(struct inode * inode, struct file * filp)
{
	struct block_device *bdev = I_BDEV(filp->f_mapping->host);
	if (bdev->bd_holder == filp)
		bd_release(bdev);
	return blkdev_put(bdev, filp->f_mode);
}

static long block_ioctl(struct file *file, unsigned cmd, unsigned long arg)
{
	struct block_device *bdev = I_BDEV(file->f_mapping->host);
	fmode_t mode = file->f_mode;

	/*
	 * O_NDELAY can be altered using fcntl(.., F_SETFL, ..), so we have
	 * to updated it before every ioctl.
	 */
	if (file->f_flags & O_NDELAY)
		mode |= FMODE_NDELAY;
	else
		mode &= ~FMODE_NDELAY;

	return blkdev_ioctl(bdev, mode, cmd, arg);
}

/*
 * Write data to the block device.  Only intended for the block device itself
 * and the raw driver which basically is a fake block device.
 *
 * Does not take i_mutex for the write and thus is not for general purpose
 * use.
 */
ssize_t blkdev_aio_write(struct kiocb *iocb, const struct iovec *iov,
			 unsigned long nr_segs, loff_t pos)
{
	struct file *file = iocb->ki_filp;
	ssize_t ret;

	BUG_ON(iocb->ki_pos != pos);

	ret = __generic_file_aio_write(iocb, iov, nr_segs, &iocb->ki_pos);
	if (ret > 0 || ret == -EIOCBQUEUED) {
		ssize_t err;

		err = generic_write_sync(file, pos, ret);
		if (err < 0 && ret > 0)
			ret = err;
	}
	return ret;
}
EXPORT_SYMBOL_GPL(blkdev_aio_write);

/*
 * Try to release a page associated with block device when the system
 * is under memory pressure.
 */
static int blkdev_releasepage(struct page *page, gfp_t wait)
{
	struct super_block *super = BDEV_I(page->mapping->host)->bdev.bd_super;

	if (super && super->s_op->bdev_try_to_free_page)
		return super->s_op->bdev_try_to_free_page(super, page, wait);

	return try_to_free_buffers(page);
}

static const struct address_space_operations def_blk_aops = {
	.readpage	= blkdev_readpage,
	.writepage	= blkdev_writepage,
	.sync_page	= block_sync_page,
	.write_begin	= blkdev_write_begin,
	.write_end	= blkdev_write_end,
	.writepages	= generic_writepages,
	.releasepage	= blkdev_releasepage,
	.direct_IO	= blkdev_direct_IO,
};

const struct file_operations def_blk_fops = {
	.open		= blkdev_open,
	.release	= blkdev_close,
	.llseek		= block_llseek,
	.read		= do_sync_read,
	.write		= do_sync_write,
  	.aio_read	= generic_file_aio_read,
	.aio_write	= blkdev_aio_write,
	.mmap		= generic_file_mmap,
	.fsync		= block_fsync,
	.unlocked_ioctl	= block_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= compat_blkdev_ioctl,
#endif
	.splice_read	= generic_file_splice_read,
	.splice_write	= generic_file_splice_write,
};

int ioctl_by_bdev(struct block_device *bdev, unsigned cmd, unsigned long arg)
{
	int res;
	mm_segment_t old_fs = get_fs();
	set_fs(KERNEL_DS);
	res = blkdev_ioctl(bdev, 0, cmd, arg);
	set_fs(old_fs);
	return res;
}

EXPORT_SYMBOL(ioctl_by_bdev);

/**
 * lookup_bdev  - lookup a struct block_device by name
 * @pathname:	special file representing the block device
 *
 * Get a reference to the blockdevice at @pathname in the current
 * namespace if possible and return it.  Return ERR_PTR(error)
 * otherwise.
 */
struct block_device *lookup_bdev(const char *pathname)
{
	struct block_device *bdev;
	struct inode *inode;
	struct path path;
	int error;

	if (!pathname || !*pathname)
		return ERR_PTR(-EINVAL);

	error = kern_path(pathname, LOOKUP_FOLLOW, &path);
	if (error)
		return ERR_PTR(error);

	inode = path.dentry->d_inode;
	error = -ENOTBLK;
	if (!S_ISBLK(inode->i_mode))
		goto fail;
	error = -EACCES;
	if (path.mnt->mnt_flags & MNT_NODEV)
		goto fail;
	error = -ENOMEM;
	bdev = bd_acquire(inode);
	if (!bdev)
		goto fail;
out:
	path_put(&path);
	return bdev;
fail:
	bdev = ERR_PTR(error);
	goto out;
}
EXPORT_SYMBOL(lookup_bdev);

/**
 * open_bdev_exclusive  -  open a block device by name and set it up for use
 *
 * @path:	special file representing the block device
 * @mode:	FMODE_... combination to pass be used
 * @holder:	owner for exclusion
 *
 * Open the blockdevice described by the special file at @path, claim it
 * for the @holder.
 */
struct block_device *open_bdev_exclusive(const char *path, fmode_t mode, void *holder)
{
	struct block_device *bdev;
	int error = 0;

	bdev = lookup_bdev(path);
	if (IS_ERR(bdev))
		return bdev;

	error = blkdev_get(bdev, mode);
	if (error)
		return ERR_PTR(error);
	error = -EACCES;
	if ((mode & FMODE_WRITE) && bdev_read_only(bdev))
		goto blkdev_put;
	error = bd_claim(bdev, holder);
	if (error)
		goto blkdev_put;

	return bdev;
	
blkdev_put:
	blkdev_put(bdev, mode);
	return ERR_PTR(error);
}

EXPORT_SYMBOL(open_bdev_exclusive);

/**
 * close_bdev_exclusive  -  close a blockdevice opened by open_bdev_exclusive()
 *
 * @bdev:	blockdevice to close
 * @mode:	mode, must match that used to open.
 *
 * This is the counterpart to open_bdev_exclusive().
 */
void close_bdev_exclusive(struct block_device *bdev, fmode_t mode)
{
	bd_release(bdev);
	blkdev_put(bdev, mode);
}

EXPORT_SYMBOL(close_bdev_exclusive);

int __invalidate_device(struct block_device *bdev)
{
	struct super_block *sb = get_super(bdev);
	int res = 0;

	if (sb) {
		/*
		 * no need to lock the super, get_super holds the
		 * read mutex so the filesystem cannot go away
		 * under us (->put_super runs with the write lock
		 * hold).
		 */
		shrink_dcache_sb(sb);
		res = invalidate_inodes(sb);
		drop_super(sb);
	}
	invalidate_bdev(bdev);
	return res;
}
EXPORT_SYMBOL(__invalidate_device);
