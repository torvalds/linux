#include <linux/module.h>
#include <linux/fs.h>
#include <linux/fs_stack.h>

/* does _NOT_ require i_mutex to be held.
 *
 * This function cannot be inlined since i_size_{read,write} is rather
 * heavy-weight on 32-bit systems
 */
void fsstack_copy_inode_size(struct inode *dst, struct inode *src)
{
	loff_t i_size;
	blkcnt_t i_blocks;

	/*
	 * i_size_read() includes its own seqlocking and protection from
	 * preemption (see include/linux/fs.h): we need nothing extra for
	 * that here, and prefer to avoid nesting locks than attempt to keep
	 * i_size and i_blocks in sync together.
	 */
	i_size = i_size_read(src);

	/*
	 * But if CONFIG_LBDAF (on 32-bit), we ought to make an effort to
	 * keep the two halves of i_blocks in sync despite SMP or PREEMPT -
	 * though stat's generic_fillattr() doesn't bother, and we won't be
	 * applying quotas (where i_blocks does become important) at the
	 * upper level.
	 *
	 * We don't actually know what locking is used at the lower level;
	 * but if it's a filesystem that supports quotas, it will be using
	 * i_lock as in inode_add_bytes().
	 */
	if (sizeof(i_blocks) > sizeof(long))
		spin_lock(&src->i_lock);
	i_blocks = src->i_blocks;
	if (sizeof(i_blocks) > sizeof(long))
		spin_unlock(&src->i_lock);

	/*
	 * If CONFIG_SMP or CONFIG_PREEMPT on 32-bit, it's vital for
	 * fsstack_copy_inode_size() to hold some lock around
	 * i_size_write(), otherwise i_size_read() may spin forever (see
	 * include/linux/fs.h).  We don't necessarily hold i_mutex when this
	 * is called, so take i_lock for that case.
	 *
	 * And if CONFIG_LBADF (on 32-bit), continue our effort to keep the
	 * two halves of i_blocks in sync despite SMP or PREEMPT: use i_lock
	 * for that case too, and do both at once by combining the tests.
	 *
	 * There is none of this locking overhead in the 64-bit case.
	 */
	if (sizeof(i_size) > sizeof(long) || sizeof(i_blocks) > sizeof(long))
		spin_lock(&dst->i_lock);
	i_size_write(dst, i_size);
	dst->i_blocks = i_blocks;
	if (sizeof(i_size) > sizeof(long) || sizeof(i_blocks) > sizeof(long))
		spin_unlock(&dst->i_lock);
}
EXPORT_SYMBOL_GPL(fsstack_copy_inode_size);

/* copy all attributes */
void fsstack_copy_attr_all(struct inode *dest, const struct inode *src)
{
	dest->i_mode = src->i_mode;
	dest->i_uid = src->i_uid;
	dest->i_gid = src->i_gid;
	dest->i_rdev = src->i_rdev;
	dest->i_atime = src->i_atime;
	dest->i_mtime = src->i_mtime;
	dest->i_ctime = src->i_ctime;
	dest->i_blkbits = src->i_blkbits;
	dest->i_flags = src->i_flags;
	dest->i_nlink = src->i_nlink;
}
EXPORT_SYMBOL_GPL(fsstack_copy_attr_all);
