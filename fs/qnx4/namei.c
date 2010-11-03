/* 
 * QNX4 file system, Linux implementation.
 * 
 * Version : 0.2.1
 * 
 * Using parts of the xiafs filesystem.
 * 
 * History :
 * 
 * 01-06-1998 by Richard Frowijn : first release.
 * 21-06-1998 by Frank Denis : dcache support, fixed error codes.
 * 04-07-1998 by Frank Denis : first step for rmdir/unlink.
 */

#include <linux/buffer_head.h>
#include "qnx4.h"


/*
 * check if the filename is correct. For some obscure reason, qnx writes a
 * new file twice in the directory entry, first with all possible options at 0
 * and for a second time the way it is, they want us not to access the qnx
 * filesystem when whe are using linux.
 */
static int qnx4_match(int len, const char *name,
		      struct buffer_head *bh, unsigned long *offset)
{
	struct qnx4_inode_entry *de;
	int namelen, thislen;

	if (bh == NULL) {
		printk(KERN_WARNING "qnx4: matching unassigned buffer !\n");
		return 0;
	}
	de = (struct qnx4_inode_entry *) (bh->b_data + *offset);
	*offset += QNX4_DIR_ENTRY_SIZE;
	if ((de->di_status & QNX4_FILE_LINK) != 0) {
		namelen = QNX4_NAME_MAX;
	} else {
		namelen = QNX4_SHORT_NAME_MAX;
	}
	/* "" means "." ---> so paths like "/usr/lib//libc.a" work */
	if (!len && (de->di_fname[0] == '.') && (de->di_fname[1] == '\0')) {
		return 1;
	}
	thislen = strlen( de->di_fname );
	if ( thislen > namelen )
		thislen = namelen;
	if (len != thislen) {
		return 0;
	}
	if (strncmp(name, de->di_fname, len) == 0) {
		if ((de->di_status & (QNX4_FILE_USED|QNX4_FILE_LINK)) != 0) {
			return 1;
		}
	}
	return 0;
}

static struct buffer_head *qnx4_find_entry(int len, struct inode *dir,
	   const char *name, struct qnx4_inode_entry **res_dir, int *ino)
{
	unsigned long block, offset, blkofs;
	struct buffer_head *bh;

	*res_dir = NULL;
	if (!dir->i_sb) {
		printk(KERN_WARNING "qnx4: no superblock on dir.\n");
		return NULL;
	}
	bh = NULL;
	block = offset = blkofs = 0;
	while (blkofs * QNX4_BLOCK_SIZE + offset < dir->i_size) {
		if (!bh) {
			bh = qnx4_bread(dir, blkofs, 0);
			if (!bh) {
				blkofs++;
				continue;
			}
		}
		*res_dir = (struct qnx4_inode_entry *) (bh->b_data + offset);
		if (qnx4_match(len, name, bh, &offset)) {
			block = qnx4_block_map( dir, blkofs );
			*ino = block * QNX4_INODES_PER_BLOCK +
			    (offset / QNX4_DIR_ENTRY_SIZE) - 1;
			return bh;
		}
		if (offset < bh->b_size) {
			continue;
		}
		brelse(bh);
		bh = NULL;
		offset = 0;
		blkofs++;
	}
	brelse(bh);
	*res_dir = NULL;
	return NULL;
}

struct dentry * qnx4_lookup(struct inode *dir, struct dentry *dentry, struct nameidata *nd)
{
	int ino;
	struct qnx4_inode_entry *de;
	struct qnx4_link_info *lnk;
	struct buffer_head *bh;
	const char *name = dentry->d_name.name;
	int len = dentry->d_name.len;
	struct inode *foundinode = NULL;

	if (!(bh = qnx4_find_entry(len, dir, name, &de, &ino)))
		goto out;
	/* The entry is linked, let's get the real info */
	if ((de->di_status & QNX4_FILE_LINK) == QNX4_FILE_LINK) {
		lnk = (struct qnx4_link_info *) de;
		ino = (le32_to_cpu(lnk->dl_inode_blk) - 1) *
                    QNX4_INODES_PER_BLOCK +
		    lnk->dl_inode_ndx;
	}
	brelse(bh);

	foundinode = qnx4_iget(dir->i_sb, ino);
	if (IS_ERR(foundinode)) {
		QNX4DEBUG((KERN_ERR "qnx4: lookup->iget -> error %ld\n",
			   PTR_ERR(foundinode)));
		return ERR_CAST(foundinode);
	}
out:
	d_add(dentry, foundinode);

	return NULL;
}
