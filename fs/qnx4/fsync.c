/* 
 * QNX4 file system, Linux implementation.
 * 
 * Version : 0.1
 * 
 * Using parts of the xiafs filesystem.
 * 
 * History :
 * 
 * 24-03-1998 by Richard Frowijn : first release.
 */

#include <linux/errno.h>
#include <linux/time.h>
#include <linux/stat.h>
#include <linux/fcntl.h>
#include <linux/smp_lock.h>
#include <linux/buffer_head.h>

#include <linux/fs.h>
#include <linux/qnx4_fs.h>

#include <asm/system.h>

/*
 * The functions for qnx4 fs file synchronization.
 */

#ifdef CONFIG_QNX4FS_RW

static int sync_block(struct inode *inode, unsigned short *block, int wait)
{
	struct buffer_head *bh;
	unsigned short tmp;

	if (!*block)
		return 0;
	tmp = *block;
	bh = sb_find_get_block(inode->i_sb, *block);
	if (!bh)
		return 0;
	if (*block != tmp) {
		brelse(bh);
		return 1;
	}
	if (wait && buffer_req(bh) && !buffer_uptodate(bh)) {
		brelse(bh);
		return -1;
	}
	if (wait || !buffer_uptodate(bh) || !buffer_dirty(bh)) {
		brelse(bh);
		return 0;
	}
	ll_rw_block(WRITE, 1, &bh);
	atomic_dec(&bh->b_count);
	return 0;
}

#ifdef WTF
static int sync_iblock(struct inode *inode, unsigned short *iblock,
		       struct buffer_head **bh, int wait)
{
	int rc;
	unsigned short tmp;

	*bh = NULL;
	tmp = *iblock;
	if (!tmp)
		return 0;
	rc = sync_block(inode, iblock, wait);
	if (rc)
		return rc;
	*bh = sb_bread(inode->i_sb, tmp);
	if (tmp != *iblock) {
		brelse(*bh);
		*bh = NULL;
		return 1;
	}
	if (!*bh)
		return -1;
	return 0;
}
#endif

static int sync_direct(struct inode *inode, int wait)
{
	int i;
	int rc, err = 0;

	for (i = 0; i < 7; i++) {
		rc = sync_block(inode,
				(unsigned short *) qnx4_raw_inode(inode)->di_first_xtnt.xtnt_blk + i, wait);
		if (rc > 0)
			break;
		if (rc)
			err = rc;
	}
	return err;
}

#ifdef WTF
static int sync_indirect(struct inode *inode, unsigned short *iblock, int wait)
{
	int i;
	struct buffer_head *ind_bh;
	int rc, err = 0;

	rc = sync_iblock(inode, iblock, &ind_bh, wait);
	if (rc || !ind_bh)
		return rc;

	for (i = 0; i < 512; i++) {
		rc = sync_block(inode,
				((unsigned short *) ind_bh->b_data) + i,
				wait);
		if (rc > 0)
			break;
		if (rc)
			err = rc;
	}
	brelse(ind_bh);
	return err;
}

static int sync_dindirect(struct inode *inode, unsigned short *diblock,
			  int wait)
{
	int i;
	struct buffer_head *dind_bh;
	int rc, err = 0;

	rc = sync_iblock(inode, diblock, &dind_bh, wait);
	if (rc || !dind_bh)
		return rc;

	for (i = 0; i < 512; i++) {
		rc = sync_indirect(inode,
				((unsigned short *) dind_bh->b_data) + i,
				   wait);
		if (rc > 0)
			break;
		if (rc)
			err = rc;
	}
	brelse(dind_bh);
	return err;
}
#endif

int qnx4_sync_file(struct file *file, struct dentry *dentry, int unused)
{
        struct inode *inode = dentry->d_inode;
	int wait, err = 0;
        
        (void) file;
	if (!(S_ISREG(inode->i_mode) || S_ISDIR(inode->i_mode) ||
	      S_ISLNK(inode->i_mode)))
		return -EINVAL;

	lock_kernel();
	for (wait = 0; wait <= 1; wait++) {
		err |= sync_direct(inode, wait);
	}
	err |= qnx4_sync_inode(inode);
	unlock_kernel();
	return (err < 0) ? -EIO : 0;
}

#endif
