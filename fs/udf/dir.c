/*
 * dir.c
 *
 * PURPOSE
 *  Directory handling routines for the OSTA-UDF(tm) filesystem.
 *
 * COPYRIGHT
 *	This file is distributed under the terms of the GNU General Public
 *	License (GPL). Copies of the GPL can be obtained from:
 *		ftp://prep.ai.mit.edu/pub/gnu/GPL
 *	Each contributing author retains all rights to their own work.
 *
 *  (C) 1998-2004 Ben Fennema
 *
 * HISTORY
 *
 *  10/05/98 dgb  Split directory operations into its own file
 *                Implemented directory reads via do_udf_readdir
 *  10/06/98      Made directory operations work!
 *  11/17/98      Rewrote directory to support ICBTAG_FLAG_AD_LONG
 *  11/25/98 blf  Rewrote directory handling (readdir+lookup) to support reading
 *                across blocks.
 *  12/12/98      Split out the lookup code to namei.c. bulk of directory
 *                code now in directory.c:udf_fileident_read.
 */

#include "udfdecl.h"

#include <linux/string.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/smp_lock.h>
#include <linux/buffer_head.h>

#include "udf_i.h"
#include "udf_sb.h"

/* Prototypes for file operations */
static int udf_readdir(struct file *, void *, filldir_t);
static int do_udf_readdir(struct inode *, struct file *, filldir_t, void *);

/* readdir and lookup functions */

const struct file_operations udf_dir_operations = {
	.read			= generic_read_dir,
	.readdir		= udf_readdir,
	.ioctl			= udf_ioctl,
	.fsync			= udf_fsync_file,
};

/*
 * udf_readdir
 *
 * PURPOSE
 *	Read a directory entry.
 *
 * DESCRIPTION
 *	Optional - sys_getdents() will return -ENOTDIR if this routine is not
 *	available.
 *
 *	Refer to sys_getdents() in fs/readdir.c
 *	sys_getdents() -> .
 *
 * PRE-CONDITIONS
 *	filp			Pointer to directory file.
 *	buf			Pointer to directory entry buffer.
 *	filldir			Pointer to filldir function.
 *
 * POST-CONDITIONS
 *	<return>		>=0 on success.
 *
 * HISTORY
 *	July 1, 1997 - Andrew E. Mileski
 *	Written, tested, and released.
 */

int udf_readdir(struct file *filp, void *dirent, filldir_t filldir)
{
	struct inode *dir = filp->f_path.dentry->d_inode;
	int result;

	lock_kernel();

	if ( filp->f_pos == 0 ) 
	{
		if (filldir(dirent, ".", 1, filp->f_pos, dir->i_ino, DT_DIR) < 0)
		{
			unlock_kernel();
			return 0;
		}
		filp->f_pos ++;
	}

	result = do_udf_readdir(dir, filp, filldir, dirent);
	unlock_kernel();
 	return result;
}

static int 
do_udf_readdir(struct inode * dir, struct file *filp, filldir_t filldir, void *dirent)
{
	struct udf_fileident_bh fibh;
	struct fileIdentDesc *fi=NULL;
	struct fileIdentDesc cfi;
	int block, iblock;
	loff_t nf_pos = filp->f_pos - 1;
	int flen;
	char fname[UDF_NAME_LEN];
	char *nameptr;
	uint16_t liu;
	uint8_t lfi;
	loff_t size = (udf_ext0_offset(dir) + dir->i_size) >> 2;
	struct buffer_head *tmp, *bha[16];
	kernel_lb_addr eloc;
	uint32_t elen;
	sector_t offset;
	int i, num;
	unsigned int dt_type;
	struct extent_position epos = { NULL, 0, {0, 0}};

	if (nf_pos >= size)
		return 0;

	if (nf_pos == 0)
		nf_pos = (udf_ext0_offset(dir) >> 2);

	fibh.soffset = fibh.eoffset = (nf_pos & ((dir->i_sb->s_blocksize - 1) >> 2)) << 2;
	if (UDF_I_ALLOCTYPE(dir) == ICBTAG_FLAG_AD_IN_ICB)
		fibh.sbh = fibh.ebh = NULL;
	else if (inode_bmap(dir, nf_pos >> (dir->i_sb->s_blocksize_bits - 2),
		&epos, &eloc, &elen, &offset) == (EXT_RECORDED_ALLOCATED >> 30))
	{
		block = udf_get_lb_pblock(dir->i_sb, eloc, offset);
		if ((++offset << dir->i_sb->s_blocksize_bits) < elen)
		{
			if (UDF_I_ALLOCTYPE(dir) == ICBTAG_FLAG_AD_SHORT)
				epos.offset -= sizeof(short_ad);
			else if (UDF_I_ALLOCTYPE(dir) == ICBTAG_FLAG_AD_LONG)
				epos.offset -= sizeof(long_ad);
		}
		else
			offset = 0;

		if (!(fibh.sbh = fibh.ebh = udf_tread(dir->i_sb, block)))
		{
			brelse(epos.bh);
			return -EIO;
		}
	
		if (!(offset & ((16 >> (dir->i_sb->s_blocksize_bits - 9))-1)))
		{
			i = 16 >> (dir->i_sb->s_blocksize_bits - 9);
			if (i+offset > (elen >> dir->i_sb->s_blocksize_bits))
				i = (elen >> dir->i_sb->s_blocksize_bits)-offset;
			for (num=0; i>0; i--)
			{
				block = udf_get_lb_pblock(dir->i_sb, eloc, offset+i);
				tmp = udf_tgetblk(dir->i_sb, block);
				if (tmp && !buffer_uptodate(tmp) && !buffer_locked(tmp))
					bha[num++] = tmp;
				else
					brelse(tmp);
			}
			if (num)
			{
				ll_rw_block(READA, num, bha);
				for (i=0; i<num; i++)
					brelse(bha[i]);
			}
		}
	}
	else
	{
		brelse(epos.bh);
		return -ENOENT;
	}

	while ( nf_pos < size )
	{
		filp->f_pos = nf_pos + 1;

		fi = udf_fileident_read(dir, &nf_pos, &fibh, &cfi, &epos, &eloc, &elen, &offset);

		if (!fi)
		{
			if (fibh.sbh != fibh.ebh)
				brelse(fibh.ebh);
			brelse(fibh.sbh);
			brelse(epos.bh);
			return 0;
		}

		liu = le16_to_cpu(cfi.lengthOfImpUse);
		lfi = cfi.lengthFileIdent;

		if (fibh.sbh == fibh.ebh)
			nameptr = fi->fileIdent + liu;
		else
		{
			int poffset;	/* Unpaded ending offset */

			poffset = fibh.soffset + sizeof(struct fileIdentDesc) + liu + lfi;

			if (poffset >= lfi)
				nameptr = (char *)(fibh.ebh->b_data + poffset - lfi);
			else
			{
				nameptr = fname;
				memcpy(nameptr, fi->fileIdent + liu, lfi - poffset);
				memcpy(nameptr + lfi - poffset, fibh.ebh->b_data, poffset);
			}
		}

		if ( (cfi.fileCharacteristics & FID_FILE_CHAR_DELETED) != 0 )
		{
			if ( !UDF_QUERY_FLAG(dir->i_sb, UDF_FLAG_UNDELETE) )
				continue;
		}
		
		if ( (cfi.fileCharacteristics & FID_FILE_CHAR_HIDDEN) != 0 )
		{
			if ( !UDF_QUERY_FLAG(dir->i_sb, UDF_FLAG_UNHIDE) )
				continue;
		}

		if ( cfi.fileCharacteristics & FID_FILE_CHAR_PARENT )
		{
			iblock = parent_ino(filp->f_path.dentry);
			flen = 2;
			memcpy(fname, "..", flen);
			dt_type = DT_DIR;
		}
		else
		{
			kernel_lb_addr tloc = lelb_to_cpu(cfi.icb.extLocation);

			iblock = udf_get_lb_pblock(dir->i_sb, tloc, 0);
			flen = udf_get_filename(dir->i_sb, nameptr, fname, lfi);
			dt_type = DT_UNKNOWN;
		}

		if (flen)
		{
			if (filldir(dirent, fname, flen, filp->f_pos, iblock, dt_type) < 0)
			{
				if (fibh.sbh != fibh.ebh)
					brelse(fibh.ebh);
				brelse(fibh.sbh);
				brelse(epos.bh);
	 			return 0;
			}
		}
	} /* end while */

	filp->f_pos = nf_pos + 1;

	if (fibh.sbh != fibh.ebh)
		brelse(fibh.ebh);
	brelse(fibh.sbh);
	brelse(epos.bh);

	return 0;
}
