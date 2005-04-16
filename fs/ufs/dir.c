/*
 *  linux/fs/ufs/ufs_dir.c
 *
 * Copyright (C) 1996
 * Adrian Rodriguez (adrian@franklins-tower.rutgers.edu)
 * Laboratory for Computer Science Research Computing Facility
 * Rutgers, The State University of New Jersey
 *
 * swab support by Francois-Rene Rideau <fare@tunes.org> 19970406
 *
 * 4.4BSD (FreeBSD) support added on February 1st 1998 by
 * Niels Kristian Bech Jensen <nkbj@image.dk> partially based
 * on code by Martin von Loewis <martin@mira.isdn.cs.tu-berlin.de>.
 */

#include <linux/time.h>
#include <linux/fs.h>
#include <linux/ufs_fs.h>
#include <linux/smp_lock.h>
#include <linux/buffer_head.h>
#include <linux/sched.h>

#include "swab.h"
#include "util.h"

#undef UFS_DIR_DEBUG

#ifdef UFS_DIR_DEBUG
#define UFSD(x) printk("(%s, %d), %s: ", __FILE__, __LINE__, __FUNCTION__); printk x;
#else
#define UFSD(x)
#endif

static int
ufs_check_dir_entry (const char *, struct inode *, struct ufs_dir_entry *,
		     struct buffer_head *, unsigned long);


/*
 * NOTE! unlike strncmp, ufs_match returns 1 for success, 0 for failure.
 *
 * len <= UFS_MAXNAMLEN and de != NULL are guaranteed by caller.
 */
static inline int ufs_match(struct super_block *sb, int len,
		const char * const name, struct ufs_dir_entry * de)
{
	if (len != ufs_get_de_namlen(sb, de))
		return 0;
	if (!de->d_ino)
		return 0;
	return !memcmp(name, de->d_name, len);
}

/*
 * This is blatantly stolen from ext2fs
 */
static int
ufs_readdir (struct file * filp, void * dirent, filldir_t filldir)
{
	struct inode *inode = filp->f_dentry->d_inode;
	int error = 0;
	unsigned long offset, lblk;
	int i, stored;
	struct buffer_head * bh;
	struct ufs_dir_entry * de;
	struct super_block * sb;
	int de_reclen;
	unsigned flags;
	u64     blk= 0L;

	lock_kernel();

	sb = inode->i_sb;
	flags = UFS_SB(sb)->s_flags;

	UFSD(("ENTER, ino %lu  f_pos %lu\n", inode->i_ino, (unsigned long) filp->f_pos))

	stored = 0;
	bh = NULL;
	offset = filp->f_pos & (sb->s_blocksize - 1);

	while (!error && !stored && filp->f_pos < inode->i_size) {
		lblk = (filp->f_pos) >> sb->s_blocksize_bits;
		blk = ufs_frag_map(inode, lblk);
		if (!blk || !(bh = sb_bread(sb, blk))) {
			/* XXX - error - skip to the next block */
			printk("ufs_readdir: "
			       "dir inode %lu has a hole at offset %lu\n",
			       inode->i_ino, (unsigned long int)filp->f_pos);
			filp->f_pos += sb->s_blocksize - offset;
			continue;
		}

revalidate:
		/* If the dir block has changed since the last call to
		 * readdir(2), then we might be pointing to an invalid
		 * dirent right now.  Scan from the start of the block
		 * to make sure. */
		if (filp->f_version != inode->i_version) {
			for (i = 0; i < sb->s_blocksize && i < offset; ) {
				de = (struct ufs_dir_entry *)(bh->b_data + i);
				/* It's too expensive to do a full
				 * dirent test each time round this
				 * loop, but we do have to test at
				 * least that it is non-zero.  A
				 * failure will be detected in the
				 * dirent test below. */
				de_reclen = fs16_to_cpu(sb, de->d_reclen);
				if (de_reclen < 1)
					break;
				i += de_reclen;
			}
			offset = i;
			filp->f_pos = (filp->f_pos & ~(sb->s_blocksize - 1))
				| offset;
			filp->f_version = inode->i_version;
		}

		while (!error && filp->f_pos < inode->i_size
		       && offset < sb->s_blocksize) {
			de = (struct ufs_dir_entry *) (bh->b_data + offset);
			/* XXX - put in a real ufs_check_dir_entry() */
			if ((de->d_reclen == 0) || (ufs_get_de_namlen(sb, de) == 0)) {
				filp->f_pos = (filp->f_pos &
				              (sb->s_blocksize - 1)) +
				               sb->s_blocksize;
				brelse(bh);
				unlock_kernel();
				return stored;
			}
			if (!ufs_check_dir_entry ("ufs_readdir", inode, de,
						   bh, offset)) {
				/* On error, skip the f_pos to the
				   next block. */
				filp->f_pos = (filp->f_pos |
				              (sb->s_blocksize - 1)) +
					       1;
				brelse (bh);
				unlock_kernel();
				return stored;
			}
			offset += fs16_to_cpu(sb, de->d_reclen);
			if (de->d_ino) {
				/* We might block in the next section
				 * if the data destination is
				 * currently swapped out.  So, use a
				 * version stamp to detect whether or
				 * not the directory has been modified
				 * during the copy operation. */
				unsigned long version = filp->f_version;
				unsigned char d_type = DT_UNKNOWN;

				UFSD(("filldir(%s,%u)\n", de->d_name,
							fs32_to_cpu(sb, de->d_ino)))
				UFSD(("namlen %u\n", ufs_get_de_namlen(sb, de)))

				if ((flags & UFS_DE_MASK) == UFS_DE_44BSD)
					d_type = de->d_u.d_44.d_type;
				error = filldir(dirent, de->d_name,
						ufs_get_de_namlen(sb, de), filp->f_pos,
						fs32_to_cpu(sb, de->d_ino), d_type);
				if (error)
					break;
				if (version != filp->f_version)
					goto revalidate;
				stored ++;
			}
			filp->f_pos += fs16_to_cpu(sb, de->d_reclen);
		}
		offset = 0;
		brelse (bh);
	}
	unlock_kernel();
	return 0;
}

/*
 * define how far ahead to read directories while searching them.
 */
#define NAMEI_RA_CHUNKS  2
#define NAMEI_RA_BLOCKS  4
#define NAMEI_RA_SIZE        (NAMEI_RA_CHUNKS * NAMEI_RA_BLOCKS)
#define NAMEI_RA_INDEX(c,b)  (((c) * NAMEI_RA_BLOCKS) + (b))

/*
 *	ufs_find_entry()
 *
 * finds an entry in the specified directory with the wanted name. It
 * returns the cache buffer in which the entry was found, and the entry
 * itself (as a parameter - res_bh). It does NOT read the inode of the
 * entry - you'll have to do that yourself if you want to.
 */
struct ufs_dir_entry * ufs_find_entry (struct dentry *dentry,
	struct buffer_head ** res_bh)
{
	struct super_block * sb;
	struct buffer_head * bh_use[NAMEI_RA_SIZE];
	struct buffer_head * bh_read[NAMEI_RA_SIZE];
	unsigned long offset;
	int block, toread, i, err;
	struct inode *dir = dentry->d_parent->d_inode;
	const char *name = dentry->d_name.name;
	int namelen = dentry->d_name.len;

	UFSD(("ENTER, dir_ino %lu, name %s, namlen %u\n", dir->i_ino, name, namelen))
	
	*res_bh = NULL;
	
	sb = dir->i_sb;
	
	if (namelen > UFS_MAXNAMLEN)
		return NULL;

	memset (bh_use, 0, sizeof (bh_use));
	toread = 0;
	for (block = 0; block < NAMEI_RA_SIZE; ++block) {
		struct buffer_head * bh;

		if ((block << sb->s_blocksize_bits) >= dir->i_size)
			break;
		bh = ufs_getfrag (dir, block, 0, &err);
		bh_use[block] = bh;
		if (bh && !buffer_uptodate(bh))
			bh_read[toread++] = bh;
	}

	for (block = 0, offset = 0; offset < dir->i_size; block++) {
		struct buffer_head * bh;
		struct ufs_dir_entry * de;
		char * dlimit;

		if ((block % NAMEI_RA_BLOCKS) == 0 && toread) {
			ll_rw_block (READ, toread, bh_read);
			toread = 0;
		}
		bh = bh_use[block % NAMEI_RA_SIZE];
		if (!bh) {
			ufs_error (sb, "ufs_find_entry", 
				"directory #%lu contains a hole at offset %lu",
				dir->i_ino, offset);
			offset += sb->s_blocksize;
			continue;
		}
		wait_on_buffer (bh);
		if (!buffer_uptodate(bh)) {
			/*
			 * read error: all bets are off
			 */
			break;
		}

		de = (struct ufs_dir_entry *) bh->b_data;
		dlimit = bh->b_data + sb->s_blocksize;
		while ((char *) de < dlimit && offset < dir->i_size) {
			/* this code is executed quadratically often */
			/* do minimal checking by hand */
			int de_len;

			if ((char *) de + namelen <= dlimit &&
			    ufs_match(sb, namelen, name, de)) {
				/* found a match -
				just to be sure, do a full check */
				if (!ufs_check_dir_entry("ufs_find_entry",
				    dir, de, bh, offset))
					goto failed;
				for (i = 0; i < NAMEI_RA_SIZE; ++i) {
					if (bh_use[i] != bh)
						brelse (bh_use[i]);
				}
				*res_bh = bh;
				return de;
			}
                        /* prevent looping on a bad block */
			de_len = fs16_to_cpu(sb, de->d_reclen);
			if (de_len <= 0)
				goto failed;
			offset += de_len;
			de = (struct ufs_dir_entry *) ((char *) de + de_len);
		}

		brelse (bh);
		if (((block + NAMEI_RA_SIZE) << sb->s_blocksize_bits ) >=
		    dir->i_size)
			bh = NULL;
		else
			bh = ufs_getfrag (dir, block + NAMEI_RA_SIZE, 0, &err);
		bh_use[block % NAMEI_RA_SIZE] = bh;
		if (bh && !buffer_uptodate(bh))
			bh_read[toread++] = bh;
	}

failed:
	for (i = 0; i < NAMEI_RA_SIZE; ++i) brelse (bh_use[i]);
	UFSD(("EXIT\n"))
	return NULL;
}

static int
ufs_check_dir_entry (const char *function, struct inode *dir,
		     struct ufs_dir_entry *de, struct buffer_head *bh,
		     unsigned long offset)
{
	struct super_block *sb = dir->i_sb;
	const char *error_msg = NULL;
	int rlen = fs16_to_cpu(sb, de->d_reclen);

	if (rlen < UFS_DIR_REC_LEN(1))
		error_msg = "reclen is smaller than minimal";
	else if (rlen % 4 != 0)
		error_msg = "reclen % 4 != 0";
	else if (rlen < UFS_DIR_REC_LEN(ufs_get_de_namlen(sb, de)))
		error_msg = "reclen is too small for namlen";
	else if (((char *) de - bh->b_data) + rlen > dir->i_sb->s_blocksize)
		error_msg = "directory entry across blocks";
	else if (fs32_to_cpu(sb, de->d_ino) > (UFS_SB(sb)->s_uspi->s_ipg *
				      UFS_SB(sb)->s_uspi->s_ncg))
		error_msg = "inode out of bounds";

	if (error_msg != NULL)
		ufs_error (sb, function, "bad entry in directory #%lu, size %Lu: %s - "
			    "offset=%lu, inode=%lu, reclen=%d, namlen=%d",
			    dir->i_ino, dir->i_size, error_msg, offset,
			    (unsigned long)fs32_to_cpu(sb, de->d_ino),
			    rlen, ufs_get_de_namlen(sb, de));
	
	return (error_msg == NULL ? 1 : 0);
}

struct ufs_dir_entry *ufs_dotdot(struct inode *dir, struct buffer_head **p)
{
	int err;
	struct buffer_head *bh = ufs_bread (dir, 0, 0, &err);
	struct ufs_dir_entry *res = NULL;

	if (bh) {
		res = (struct ufs_dir_entry *) bh->b_data;
		res = (struct ufs_dir_entry *)((char *)res +
			fs16_to_cpu(dir->i_sb, res->d_reclen));
	}
	*p = bh;
	return res;
}
ino_t ufs_inode_by_name(struct inode * dir, struct dentry *dentry)
{
	ino_t res = 0;
	struct ufs_dir_entry * de;
	struct buffer_head *bh;

	de = ufs_find_entry (dentry, &bh);
	if (de) {
		res = fs32_to_cpu(dir->i_sb, de->d_ino);
		brelse(bh);
	}
	return res;
}

void ufs_set_link(struct inode *dir, struct ufs_dir_entry *de,
		struct buffer_head *bh, struct inode *inode)
{
	dir->i_version++;
	de->d_ino = cpu_to_fs32(dir->i_sb, inode->i_ino);
	mark_buffer_dirty(bh);
	if (IS_DIRSYNC(dir))
		sync_dirty_buffer(bh);
	brelse (bh);
}

/*
 *	ufs_add_entry()
 *
 * adds a file entry to the specified directory, using the same
 * semantics as ufs_find_entry(). It returns NULL if it failed.
 */
int ufs_add_link(struct dentry *dentry, struct inode *inode)
{
	struct super_block * sb;
	struct ufs_sb_private_info * uspi;
	unsigned long offset;
	unsigned fragoff;
	unsigned short rec_len;
	struct buffer_head * bh;
	struct ufs_dir_entry * de, * de1;
	struct inode *dir = dentry->d_parent->d_inode;
	const char *name = dentry->d_name.name;
	int namelen = dentry->d_name.len;
	int err;

	UFSD(("ENTER, name %s, namelen %u\n", name, namelen))
	
	sb = dir->i_sb;
	uspi = UFS_SB(sb)->s_uspi;

	if (!namelen)
		return -EINVAL;
	bh = ufs_bread (dir, 0, 0, &err);
	if (!bh)
		return err;
	rec_len = UFS_DIR_REC_LEN(namelen);
	offset = 0;
	de = (struct ufs_dir_entry *) bh->b_data;
	while (1) {
		if ((char *)de >= UFS_SECTOR_SIZE + bh->b_data) {
			fragoff = offset & ~uspi->s_fmask;
			if (fragoff != 0 && fragoff != UFS_SECTOR_SIZE)
				ufs_error (sb, "ufs_add_entry", "internal error"
					" fragoff %u", fragoff);
			if (!fragoff) {
				brelse (bh);
				bh = ufs_bread (dir, offset >> sb->s_blocksize_bits, 1, &err);
				if (!bh)
					return err;
			}
			if (dir->i_size <= offset) {
				if (dir->i_size == 0) {
					brelse(bh);
					return -ENOENT;
				}
				de = (struct ufs_dir_entry *) (bh->b_data + fragoff);
				de->d_ino = 0;
				de->d_reclen = cpu_to_fs16(sb, UFS_SECTOR_SIZE);
				ufs_set_de_namlen(sb, de, 0);
				dir->i_size = offset + UFS_SECTOR_SIZE;
				mark_inode_dirty(dir);
			} else {
				de = (struct ufs_dir_entry *) bh->b_data;
			}
		}
		if (!ufs_check_dir_entry ("ufs_add_entry", dir, de, bh, offset)) {
			brelse (bh);
			return -ENOENT;
		}
		if (ufs_match(sb, namelen, name, de)) {
			brelse (bh);
			return -EEXIST;
		}
		if (de->d_ino == 0 && fs16_to_cpu(sb, de->d_reclen) >= rec_len)
			break;
			
		if (fs16_to_cpu(sb, de->d_reclen) >=
		     UFS_DIR_REC_LEN(ufs_get_de_namlen(sb, de)) + rec_len)
			break;
		offset += fs16_to_cpu(sb, de->d_reclen);
		de = (struct ufs_dir_entry *) ((char *) de + fs16_to_cpu(sb, de->d_reclen));
	}

	if (de->d_ino) {
		de1 = (struct ufs_dir_entry *) ((char *) de +
			UFS_DIR_REC_LEN(ufs_get_de_namlen(sb, de)));
		de1->d_reclen =
			cpu_to_fs16(sb, fs16_to_cpu(sb, de->d_reclen) -
				UFS_DIR_REC_LEN(ufs_get_de_namlen(sb, de)));
		de->d_reclen =
			cpu_to_fs16(sb, UFS_DIR_REC_LEN(ufs_get_de_namlen(sb, de)));
		de = de1;
	}
	de->d_ino = 0;
	ufs_set_de_namlen(sb, de, namelen);
	memcpy (de->d_name, name, namelen + 1);
	de->d_ino = cpu_to_fs32(sb, inode->i_ino);
	ufs_set_de_type(sb, de, inode->i_mode);
	mark_buffer_dirty(bh);
	if (IS_DIRSYNC(dir))
		sync_dirty_buffer(bh);
	brelse (bh);
	dir->i_mtime = dir->i_ctime = CURRENT_TIME_SEC;
	dir->i_version++;
	mark_inode_dirty(dir);

	UFSD(("EXIT\n"))
	return 0;
}

/*
 * ufs_delete_entry deletes a directory entry by merging it with the
 * previous entry.
 */
int ufs_delete_entry (struct inode * inode, struct ufs_dir_entry * dir,
	struct buffer_head * bh )
	
{
	struct super_block * sb;
	struct ufs_dir_entry * de, * pde;
	unsigned i;
	
	UFSD(("ENTER\n"))

	sb = inode->i_sb;
	i = 0;
	pde = NULL;
	de = (struct ufs_dir_entry *) bh->b_data;
	
	UFSD(("ino %u, reclen %u, namlen %u, name %s\n",
		fs32_to_cpu(sb, de->d_ino),
		fs16to_cpu(sb, de->d_reclen),
		ufs_get_de_namlen(sb, de), de->d_name))

	while (i < bh->b_size) {
		if (!ufs_check_dir_entry ("ufs_delete_entry", inode, de, bh, i)) {
			brelse(bh);
			return -EIO;
		}
		if (de == dir)  {
			if (pde)
				fs16_add(sb, &pde->d_reclen,
					fs16_to_cpu(sb, dir->d_reclen));
			dir->d_ino = 0;
			inode->i_version++;
			inode->i_ctime = inode->i_mtime = CURRENT_TIME_SEC;
			mark_inode_dirty(inode);
			mark_buffer_dirty(bh);
			if (IS_DIRSYNC(inode))
				sync_dirty_buffer(bh);
			brelse(bh);
			UFSD(("EXIT\n"))
			return 0;
		}
		i += fs16_to_cpu(sb, de->d_reclen);
		if (i == UFS_SECTOR_SIZE) pde = NULL;
		else pde = de;
		de = (struct ufs_dir_entry *)
		    ((char *) de + fs16_to_cpu(sb, de->d_reclen));
		if (i == UFS_SECTOR_SIZE && de->d_reclen == 0)
			break;
	}
	UFSD(("EXIT\n"))
	brelse(bh);
	return -ENOENT;
}

int ufs_make_empty(struct inode * inode, struct inode *dir)
{
	struct super_block * sb = dir->i_sb;
	struct buffer_head * dir_block;
	struct ufs_dir_entry * de;
	int err;

	dir_block = ufs_bread (inode, 0, 1, &err);
	if (!dir_block)
		return err;

	inode->i_blocks = sb->s_blocksize / UFS_SECTOR_SIZE;
	de = (struct ufs_dir_entry *) dir_block->b_data;
	de->d_ino = cpu_to_fs32(sb, inode->i_ino);
	ufs_set_de_type(sb, de, inode->i_mode);
	ufs_set_de_namlen(sb, de, 1);
	de->d_reclen = cpu_to_fs16(sb, UFS_DIR_REC_LEN(1));
	strcpy (de->d_name, ".");
	de = (struct ufs_dir_entry *)
		((char *)de + fs16_to_cpu(sb, de->d_reclen));
	de->d_ino = cpu_to_fs32(sb, dir->i_ino);
	ufs_set_de_type(sb, de, dir->i_mode);
	de->d_reclen = cpu_to_fs16(sb, UFS_SECTOR_SIZE - UFS_DIR_REC_LEN(1));
	ufs_set_de_namlen(sb, de, 2);
	strcpy (de->d_name, "..");
	mark_buffer_dirty(dir_block);
	brelse (dir_block);
	mark_inode_dirty(inode);
	return 0;
}

/*
 * routine to check that the specified directory is empty (for rmdir)
 */
int ufs_empty_dir (struct inode * inode)
{
	struct super_block * sb;
	unsigned long offset;
	struct buffer_head * bh;
	struct ufs_dir_entry * de, * de1;
	int err;
	
	sb = inode->i_sb;

	if (inode->i_size < UFS_DIR_REC_LEN(1) + UFS_DIR_REC_LEN(2) ||
	    !(bh = ufs_bread (inode, 0, 0, &err))) {
	    	ufs_warning (inode->i_sb, "empty_dir",
			      "bad directory (dir #%lu) - no data block",
			      inode->i_ino);
		return 1;
	}
	de = (struct ufs_dir_entry *) bh->b_data;
	de1 = (struct ufs_dir_entry *)
		((char *)de + fs16_to_cpu(sb, de->d_reclen));
	if (fs32_to_cpu(sb, de->d_ino) != inode->i_ino || de1->d_ino == 0 ||
	     strcmp (".", de->d_name) || strcmp ("..", de1->d_name)) {
	    	ufs_warning (inode->i_sb, "empty_dir",
			      "bad directory (dir #%lu) - no `.' or `..'",
			      inode->i_ino);
		return 1;
	}
	offset = fs16_to_cpu(sb, de->d_reclen) + fs16_to_cpu(sb, de1->d_reclen);
	de = (struct ufs_dir_entry *)
		((char *)de1 + fs16_to_cpu(sb, de1->d_reclen));
	while (offset < inode->i_size ) {
		if (!bh || (void *) de >= (void *) (bh->b_data + sb->s_blocksize)) {
			brelse (bh);
			bh = ufs_bread (inode, offset >> sb->s_blocksize_bits, 1, &err);
	 		if (!bh) {
				ufs_error (sb, "empty_dir",
					    "directory #%lu contains a hole at offset %lu",
					    inode->i_ino, offset);
				offset += sb->s_blocksize;
				continue;
			}
			de = (struct ufs_dir_entry *) bh->b_data;
		}
		if (!ufs_check_dir_entry ("empty_dir", inode, de, bh, offset)) {
			brelse (bh);
			return 1;
		}
		if (de->d_ino) {
			brelse (bh);
			return 0;
		}
		offset += fs16_to_cpu(sb, de->d_reclen);
		de = (struct ufs_dir_entry *)
			((char *)de + fs16_to_cpu(sb, de->d_reclen));
	}
	brelse (bh);
	return 1;
}

struct file_operations ufs_dir_operations = {
	.read		= generic_read_dir,
	.readdir	= ufs_readdir,
	.fsync		= file_fsync,
};
