/*
 *  linux/fs/block_dev.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *  Copyright (C) 2001  Andrea Arcangeli <andrea@suse.de> SuSE
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/fcntl.h>
#include <linux/slab.h>
#include <linux/kmod.h>
#include <linux/major.h>
#include <linux/devfs_fs_kernel.h>
#include <linux/smp_lock.h>
#include <linux/highmem.h>
#include <linux/blkdev.h>
#include <linux/module.h>
#include <linux/blkpg.h>
#include <linux/buffer_head.h>
#include <linux/mpage.h>
#include <linux/mount.h>
#include <linux/uio.h>
#include <linux/namei.h>
#include <asm/uaccess.h>

struct bdev_inode {
	struct block_device bdev;
	struct inode vfs_inode;
};

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

/* Kill _all_ buffers, dirty or not.. */
static void kill_bdev(struct block_device *bdev)
{
	invalidate_bdev(bdev, 1);
	truncate_inode_pages(bdev->bd_inode->i_mapping, 0);
}	

int set_blocksize(struct block_device *bdev, int size)
{
	/* Size must be a power of two, and between 512 and PAGE_SIZE */
	if (size > PAGE_SIZE || size < 512 || (size & (size-1)))
		return -EINVAL;

	/* Size cannot be smaller than the size supported by the device */
	if (size < bdev_hardsect_size(bdev))
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
	int bits = 9; /* 2^9 = 512 */

	if (set_blocksize(sb->s_bdev, size))
		return 0;
	/* If we get here, we know size is power of two
	 * and it's value is between 512 and PAGE_SIZE */
	sb->s_blocksize = size;
	for (size >>= 10; size; size >>= 1)
		++bits;
	sb->s_blocksize_bits = bits;
	return sb->s_blocksize;
}

EXPORT_SYMBOL(sb_set_blocksize);

int sb_min_blocksize(struct super_block *sb, int size)
{
	int minsize = bdev_hardsect_size(sb->s_bdev);
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
		unsigned long max_blocks, struct buffer_head *bh, int create)
{
	sector_t end_block = max_block(I_BDEV(inode));

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

static int blkdev_writepage(struct page *page, struct writeback_control *wbc)
{
	return block_write_full_page(page, blkdev_get_block, wbc);
}

static int blkdev_readpage(struct file * file, struct page * page)
{
	return block_read_full_page(page, blkdev_get_block);
}

static int blkdev_prepare_write(struct file *file, struct page *page, unsigned from, unsigned to)
{
	return block_prepare_write(page, from, to, blkdev_get_block);
}

static int blkdev_commit_write(struct file *file, struct page *page, unsigned from, unsigned to)
{
	return block_commit_write(page, from, to);
}

/*
 * private llseek:
 * for a block special file file->f_dentry->d_inode->i_size is zero
 * so we compute the size by hand (just as in block_read/write above)
 */
static loff_t block_llseek(struct file *file, loff_t offset, int origin)
{
	struct inode *bd_inode = file->f_mapping->host;
	loff_t size;
	loff_t retval;

	down(&bd_inode->i_sem);
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
	up(&bd_inode->i_sem);
	return retval;
}
	
/*
 *	Filp is never NULL; the only case when ->fsync() is called with
 *	NULL first argument is nfsd_sync_dir() and that's not a directory.
 */
 
static int block_fsync(struct file *filp, struct dentry *dentry, int datasync)
{
	return sync_blockdev(I_BDEV(filp->f_mapping->host));
}

/*
 * pseudo-fs
 */

static  __cacheline_aligned_in_smp DEFINE_SPINLOCK(bdev_lock);
static kmem_cache_t * bdev_cachep;

static struct inode *bdev_alloc_inode(struct super_block *sb)
{
	struct bdev_inode *ei = kmem_cache_alloc(bdev_cachep, SLAB_KERNEL);
	if (!ei)
		return NULL;
	return &ei->vfs_inode;
}

static void bdev_destroy_inode(struct inode *inode)
{
	struct bdev_inode *bdi = BDEV_I(inode);

	bdi->bdev.bd_inode_backing_dev_info = NULL;
	kmem_cache_free(bdev_cachep, bdi);
}

static void init_once(void * foo, kmem_cache_t * cachep, unsigned long flags)
{
	struct bdev_inode *ei = (struct bdev_inode *) foo;
	struct block_device *bdev = &ei->bdev;

	if ((flags & (SLAB_CTOR_VERIFY|SLAB_CTOR_CONSTRUCTOR)) ==
	    SLAB_CTOR_CONSTRUCTOR)
	{
		memset(bdev, 0, sizeof(*bdev));
		sema_init(&bdev->bd_sem, 1);
		sema_init(&bdev->bd_mount_sem, 1);
		INIT_LIST_HEAD(&bdev->bd_inodes);
		INIT_LIST_HEAD(&bdev->bd_list);
		inode_init_once(&ei->vfs_inode);
	}
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

static struct super_operations bdev_sops = {
	.statfs = simple_statfs,
	.alloc_inode = bdev_alloc_inode,
	.destroy_inode = bdev_destroy_inode,
	.drop_inode = generic_delete_inode,
	.clear_inode = bdev_clear_inode,
};

static struct super_block *bd_get_sb(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data)
{
	return get_sb_pseudo(fs_type, "bdev:", &bdev_sops, 0x62646576);
}

static struct file_system_type bd_type = {
	.name		= "bdev",
	.get_sb		= bd_get_sb,
	.kill_sb	= kill_anon_super,
};

static struct vfsmount *bd_mnt;
struct super_block *blockdev_superblock;

void __init bdev_cache_init(void)
{
	int err;
	bdev_cachep = kmem_cache_create("bdev_cache", sizeof(struct bdev_inode),
			0, SLAB_HWCACHE_ALIGN|SLAB_RECLAIM_ACCOUNT|SLAB_PANIC,
			init_once, NULL);
	err = register_filesystem(&bd_type);
	if (err)
		panic("Cannot register bdev pseudo-fs");
	bd_mnt = kern_mount(&bd_type);
	err = PTR_ERR(bd_mnt);
	if (IS_ERR(bd_mnt))
		panic("Cannot create bdev pseudo-fs");
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

	inode = iget5_locked(bd_mnt->mnt_sb, hash(dev),
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

long nr_blockdev_pages(void)
{
	struct list_head *p;
	long ret = 0;
	spin_lock(&bdev_lock);
	list_for_each(p, &all_bdevs) {
		struct block_device *bdev;
		bdev = list_entry(p, struct block_device, bd_list);
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
	if (bdev && igrab(bdev->bd_inode)) {
		spin_unlock(&bdev_lock);
		return bdev;
	}
	spin_unlock(&bdev_lock);
	bdev = bdget(inode->i_rdev);
	if (bdev) {
		spin_lock(&bdev_lock);
		if (inode->i_bdev)
			__bd_forget(inode);
		inode->i_bdev = bdev;
		inode->i_mapping = bdev->bd_inode->i_mapping;
		list_add(&inode->i_devices, &bdev->bd_inodes);
		spin_unlock(&bdev_lock);
	}
	return bdev;
}

/* Call when you free inode */

void bd_forget(struct inode *inode)
{
	spin_lock(&bdev_lock);
	if (inode->i_bdev)
		__bd_forget(inode);
	spin_unlock(&bdev_lock);
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

/*
 * Tries to open block device by device number.  Use it ONLY if you
 * really do not have anything better - i.e. when you are behind a
 * truly sucky interface and all you are given is a device number.  _Never_
 * to be used for internal purposes.  If you ever need it - reconsider
 * your API.
 */
struct block_device *open_by_devnum(dev_t dev, unsigned mode)
{
	struct block_device *bdev = bdget(dev);
	int err = -ENOMEM;
	int flags = mode & FMODE_WRITE ? O_RDWR : O_RDONLY;
	if (bdev)
		err = blkdev_get(bdev, mode, flags);
	return err ? ERR_PTR(err) : bdev;
}

EXPORT_SYMBOL(open_by_devnum);

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
	struct block_device_operations * bdops = disk->fops;

	if (!bdops->media_changed)
		return 0;
	if (!bdops->media_changed(bdev->bd_disk))
		return 0;

	if (__invalidate_device(bdev, 0))
		printk("VFS: busy inodes on changed media.\n");

	if (bdops->revalidate_disk)
		bdops->revalidate_disk(bdev->bd_disk);
	if (bdev->bd_disk->minors > 1)
		bdev->bd_invalidated = 1;
	return 1;
}

EXPORT_SYMBOL(check_disk_change);

void bd_set_size(struct block_device *bdev, loff_t size)
{
	unsigned bsize = bdev_hardsect_size(bdev);

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

static int do_open(struct block_device *bdev, struct file *file)
{
	struct module *owner = NULL;
	struct gendisk *disk;
	int ret = -ENXIO;
	int part;

	file->f_mapping = bdev->bd_inode->i_mapping;
	lock_kernel();
	disk = get_gendisk(bdev->bd_dev, &part);
	if (!disk) {
		unlock_kernel();
		bdput(bdev);
		return ret;
	}
	owner = disk->fops->owner;

	down(&bdev->bd_sem);
	if (!bdev->bd_openers) {
		bdev->bd_disk = disk;
		bdev->bd_contains = bdev;
		if (!part) {
			struct backing_dev_info *bdi;
			if (disk->fops->open) {
				ret = disk->fops->open(bdev->bd_inode, file);
				if (ret)
					goto out_first;
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
			struct hd_struct *p;
			struct block_device *whole;
			whole = bdget_disk(disk, 0);
			ret = -ENOMEM;
			if (!whole)
				goto out_first;
			ret = blkdev_get(whole, file->f_mode, file->f_flags);
			if (ret)
				goto out_first;
			bdev->bd_contains = whole;
			down(&whole->bd_sem);
			whole->bd_part_count++;
			p = disk->part[part - 1];
			bdev->bd_inode->i_data.backing_dev_info =
			   whole->bd_inode->i_data.backing_dev_info;
			if (!(disk->flags & GENHD_FL_UP) || !p || !p->nr_sects) {
				whole->bd_part_count--;
				up(&whole->bd_sem);
				ret = -ENXIO;
				goto out_first;
			}
			kobject_get(&p->kobj);
			bdev->bd_part = p;
			bd_set_size(bdev, (loff_t) p->nr_sects << 9);
			up(&whole->bd_sem);
		}
	} else {
		put_disk(disk);
		module_put(owner);
		if (bdev->bd_contains == bdev) {
			if (bdev->bd_disk->fops->open) {
				ret = bdev->bd_disk->fops->open(bdev->bd_inode, file);
				if (ret)
					goto out;
			}
			if (bdev->bd_invalidated)
				rescan_partitions(bdev->bd_disk, bdev);
		} else {
			down(&bdev->bd_contains->bd_sem);
			bdev->bd_contains->bd_part_count++;
			up(&bdev->bd_contains->bd_sem);
		}
	}
	bdev->bd_openers++;
	up(&bdev->bd_sem);
	unlock_kernel();
	return 0;

out_first:
	bdev->bd_disk = NULL;
	bdev->bd_inode->i_data.backing_dev_info = &default_backing_dev_info;
	if (bdev != bdev->bd_contains)
		blkdev_put(bdev->bd_contains);
	bdev->bd_contains = NULL;
	put_disk(disk);
	module_put(owner);
out:
	up(&bdev->bd_sem);
	unlock_kernel();
	if (ret)
		bdput(bdev);
	return ret;
}

int blkdev_get(struct block_device *bdev, mode_t mode, unsigned flags)
{
	/*
	 * This crockload is due to bad choice of ->open() type.
	 * It will go away.
	 * For now, block device ->open() routine must _not_
	 * examine anything in 'inode' argument except ->i_rdev.
	 */
	struct file fake_file = {};
	struct dentry fake_dentry = {};
	fake_file.f_mode = mode;
	fake_file.f_flags = flags;
	fake_file.f_dentry = &fake_dentry;
	fake_dentry.d_inode = bdev->bd_inode;

	return do_open(bdev, &fake_file);
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

	bdev = bd_acquire(inode);

	res = do_open(bdev, filp);
	if (res)
		return res;

	if (!(filp->f_flags & O_EXCL) )
		return 0;

	if (!(res = bd_claim(bdev, filp)))
		return 0;

	blkdev_put(bdev);
	return res;
}

int blkdev_put(struct block_device *bdev)
{
	int ret = 0;
	struct inode *bd_inode = bdev->bd_inode;
	struct gendisk *disk = bdev->bd_disk;

	down(&bdev->bd_sem);
	lock_kernel();
	if (!--bdev->bd_openers) {
		sync_blockdev(bdev);
		kill_bdev(bdev);
	}
	if (bdev->bd_contains == bdev) {
		if (disk->fops->release)
			ret = disk->fops->release(bd_inode, NULL);
	} else {
		down(&bdev->bd_contains->bd_sem);
		bdev->bd_contains->bd_part_count--;
		up(&bdev->bd_contains->bd_sem);
	}
	if (!bdev->bd_openers) {
		struct module *owner = disk->fops->owner;

		put_disk(disk);
		module_put(owner);

		if (bdev->bd_contains != bdev) {
			kobject_put(&bdev->bd_part->kobj);
			bdev->bd_part = NULL;
		}
		bdev->bd_disk = NULL;
		bdev->bd_inode->i_data.backing_dev_info = &default_backing_dev_info;
		if (bdev != bdev->bd_contains) {
			blkdev_put(bdev->bd_contains);
		}
		bdev->bd_contains = NULL;
	}
	unlock_kernel();
	up(&bdev->bd_sem);
	bdput(bdev);
	return ret;
}

EXPORT_SYMBOL(blkdev_put);

static int blkdev_close(struct inode * inode, struct file * filp)
{
	struct block_device *bdev = I_BDEV(filp->f_mapping->host);
	if (bdev->bd_holder == filp)
		bd_release(bdev);
	return blkdev_put(bdev);
}

static ssize_t blkdev_file_write(struct file *file, const char __user *buf,
				   size_t count, loff_t *ppos)
{
	struct iovec local_iov = { .iov_base = (void __user *)buf, .iov_len = count };

	return generic_file_write_nolock(file, &local_iov, 1, ppos);
}

static ssize_t blkdev_file_aio_write(struct kiocb *iocb, const char __user *buf,
				   size_t count, loff_t pos)
{
	struct iovec local_iov = { .iov_base = (void __user *)buf, .iov_len = count };

	return generic_file_aio_write_nolock(iocb, &local_iov, 1, &iocb->ki_pos);
}

static int block_ioctl(struct inode *inode, struct file *file, unsigned cmd,
			unsigned long arg)
{
	return blkdev_ioctl(file->f_mapping->host, file, cmd, arg);
}

struct address_space_operations def_blk_aops = {
	.readpage	= blkdev_readpage,
	.writepage	= blkdev_writepage,
	.sync_page	= block_sync_page,
	.prepare_write	= blkdev_prepare_write,
	.commit_write	= blkdev_commit_write,
	.writepages	= generic_writepages,
	.direct_IO	= blkdev_direct_IO,
};

struct file_operations def_blk_fops = {
	.open		= blkdev_open,
	.release	= blkdev_close,
	.llseek		= block_llseek,
	.read		= generic_file_read,
	.write		= blkdev_file_write,
  	.aio_read	= generic_file_aio_read,
  	.aio_write	= blkdev_file_aio_write, 
	.mmap		= generic_file_mmap,
	.fsync		= block_fsync,
	.ioctl		= block_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= compat_blkdev_ioctl,
#endif
	.readv		= generic_file_readv,
	.writev		= generic_file_write_nolock,
	.sendfile	= generic_file_sendfile,
};

int ioctl_by_bdev(struct block_device *bdev, unsigned cmd, unsigned long arg)
{
	int res;
	mm_segment_t old_fs = get_fs();
	set_fs(KERNEL_DS);
	res = blkdev_ioctl(bdev->bd_inode, NULL, cmd, arg);
	set_fs(old_fs);
	return res;
}

EXPORT_SYMBOL(ioctl_by_bdev);

/**
 * lookup_bdev  - lookup a struct block_device by name
 *
 * @path:	special file representing the block device
 *
 * Get a reference to the blockdevice at @path in the current
 * namespace if possible and return it.  Return ERR_PTR(error)
 * otherwise.
 */
struct block_device *lookup_bdev(const char *path)
{
	struct block_device *bdev;
	struct inode *inode;
	struct nameidata nd;
	int error;

	if (!path || !*path)
		return ERR_PTR(-EINVAL);

	error = path_lookup(path, LOOKUP_FOLLOW, &nd);
	if (error)
		return ERR_PTR(error);

	inode = nd.dentry->d_inode;
	error = -ENOTBLK;
	if (!S_ISBLK(inode->i_mode))
		goto fail;
	error = -EACCES;
	if (nd.mnt->mnt_flags & MNT_NODEV)
		goto fail;
	error = -ENOMEM;
	bdev = bd_acquire(inode);
	if (!bdev)
		goto fail;
out:
	path_release(&nd);
	return bdev;
fail:
	bdev = ERR_PTR(error);
	goto out;
}

/**
 * open_bdev_excl  -  open a block device by name and set it up for use
 *
 * @path:	special file representing the block device
 * @flags:	%MS_RDONLY for opening read-only
 * @holder:	owner for exclusion
 *
 * Open the blockdevice described by the special file at @path, claim it
 * for the @holder.
 */
struct block_device *open_bdev_excl(const char *path, int flags, void *holder)
{
	struct block_device *bdev;
	mode_t mode = FMODE_READ;
	int error = 0;

	bdev = lookup_bdev(path);
	if (IS_ERR(bdev))
		return bdev;

	if (!(flags & MS_RDONLY))
		mode |= FMODE_WRITE;
	error = blkdev_get(bdev, mode, 0);
	if (error)
		return ERR_PTR(error);
	error = -EACCES;
	if (!(flags & MS_RDONLY) && bdev_read_only(bdev))
		goto blkdev_put;
	error = bd_claim(bdev, holder);
	if (error)
		goto blkdev_put;

	return bdev;
	
blkdev_put:
	blkdev_put(bdev);
	return ERR_PTR(error);
}

EXPORT_SYMBOL(open_bdev_excl);

/**
 * close_bdev_excl  -  release a blockdevice openen by open_bdev_excl()
 *
 * @bdev:	blockdevice to close
 *
 * This is the counterpart to open_bdev_excl().
 */
void close_bdev_excl(struct block_device *bdev)
{
	bd_release(bdev);
	blkdev_put(bdev);
}

EXPORT_SYMBOL(close_bdev_excl);
