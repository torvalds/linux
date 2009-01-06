#ifndef _COMPAT_H_
#define _COMPAT_H_

#define btrfs_drop_nlink(inode) drop_nlink(inode)
#define btrfs_inc_nlink(inode)	inc_nlink(inode)

#if LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 27)
static inline struct dentry *d_obtain_alias(struct inode *inode)
{
	struct dentry *d;

	if (!inode)
		return NULL;
	if (IS_ERR(inode))
		return ERR_CAST(inode);

	d = d_alloc_anon(inode);
	if (!d)
		iput(inode);
	return d;
}
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 28)
# define  __pagevec_lru_add_file __pagevec_lru_add
# define open_bdev_exclusive open_bdev_excl
# define close_bdev_exclusive(bdev, mode) close_bdev_excl(bdev)
typedef unsigned __bitwise__ fmode_t;
#endif


#endif /* _COMPAT_H_ */
