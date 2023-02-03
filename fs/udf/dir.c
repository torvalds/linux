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
#include <linux/bio.h>
#include <linux/iversion.h>

#include "udf_i.h"
#include "udf_sb.h"

static int udf_readdir(struct file *file, struct dir_context *ctx)
{
	struct inode *dir = file_inode(file);
	struct udf_inode_info *iinfo = UDF_I(dir);
	struct udf_fileident_bh fibh = { .sbh = NULL, .ebh = NULL};
	struct fileIdentDesc *fi = NULL;
	struct fileIdentDesc cfi;
	udf_pblk_t block, iblock;
	loff_t nf_pos, emit_pos = 0;
	int flen;
	unsigned char *fname = NULL, *copy_name = NULL;
	unsigned char *nameptr;
	uint16_t liu;
	uint8_t lfi;
	loff_t size = udf_ext0_offset(dir) + dir->i_size;
	struct buffer_head *tmp, *bha[16];
	struct kernel_lb_addr eloc;
	uint32_t elen;
	sector_t offset;
	int i, num, ret = 0;
	struct extent_position epos = { NULL, 0, {0, 0} };
	struct super_block *sb = dir->i_sb;
	bool pos_valid = false;

	if (ctx->pos == 0) {
		if (!dir_emit_dot(file, ctx))
			return 0;
		ctx->pos = 1;
	}
	nf_pos = (ctx->pos - 1) << 2;
	if (nf_pos >= size)
		goto out;

	/*
	 * Something changed since last readdir (either lseek was called or dir
	 * changed)?  We need to verify the position correctly points at the
	 * beginning of some dir entry so that the directory parsing code does
	 * not get confused. Since UDF does not have any reliable way of
	 * identifying beginning of dir entry (names are under user control),
	 * we need to scan the directory from the beginning.
	 */
	if (!inode_eq_iversion(dir, file->f_version)) {
		emit_pos = nf_pos;
		nf_pos = 0;
	} else {
		pos_valid = true;
	}

	fname = kmalloc(UDF_NAME_LEN, GFP_NOFS);
	if (!fname) {
		ret = -ENOMEM;
		goto out;
	}

	if (nf_pos == 0)
		nf_pos = udf_ext0_offset(dir);

	fibh.soffset = fibh.eoffset = nf_pos & (sb->s_blocksize - 1);
	if (iinfo->i_alloc_type != ICBTAG_FLAG_AD_IN_ICB) {
		if (inode_bmap(dir, nf_pos >> sb->s_blocksize_bits,
		    &epos, &eloc, &elen, &offset)
		    != (EXT_RECORDED_ALLOCATED >> 30)) {
			ret = -ENOENT;
			goto out;
		}
		block = udf_get_lb_pblock(sb, &eloc, offset);
		if ((++offset << sb->s_blocksize_bits) < elen) {
			if (iinfo->i_alloc_type == ICBTAG_FLAG_AD_SHORT)
				epos.offset -= sizeof(struct short_ad);
			else if (iinfo->i_alloc_type ==
					ICBTAG_FLAG_AD_LONG)
				epos.offset -= sizeof(struct long_ad);
		} else {
			offset = 0;
		}

		if (!(fibh.sbh = fibh.ebh = udf_tread(sb, block))) {
			ret = -EIO;
			goto out;
		}

		if (!(offset & ((16 >> (sb->s_blocksize_bits - 9)) - 1))) {
			i = 16 >> (sb->s_blocksize_bits - 9);
			if (i + offset > (elen >> sb->s_blocksize_bits))
				i = (elen >> sb->s_blocksize_bits) - offset;
			for (num = 0; i > 0; i--) {
				block = udf_get_lb_pblock(sb, &eloc, offset + i);
				tmp = udf_tgetblk(sb, block);
				if (tmp && !buffer_uptodate(tmp) && !buffer_locked(tmp))
					bha[num++] = tmp;
				else
					brelse(tmp);
			}
			if (num) {
				bh_readahead_batch(num, bha, REQ_RAHEAD);
				for (i = 0; i < num; i++)
					brelse(bha[i]);
			}
		}
	}

	while (nf_pos < size) {
		struct kernel_lb_addr tloc;
		loff_t cur_pos = nf_pos;

		/* Update file position only if we got past the current one */
		if (nf_pos >= emit_pos) {
			ctx->pos = (nf_pos >> 2) + 1;
			pos_valid = true;
		}

		fi = udf_fileident_read(dir, &nf_pos, &fibh, &cfi, &epos, &eloc,
					&elen, &offset);
		if (!fi)
			goto out;
		/* Still not at offset where user asked us to read from? */
		if (cur_pos < emit_pos)
			continue;

		liu = le16_to_cpu(cfi.lengthOfImpUse);
		lfi = cfi.lengthFileIdent;

		if (fibh.sbh == fibh.ebh) {
			nameptr = udf_get_fi_ident(fi);
		} else {
			int poffset;	/* Unpaded ending offset */

			poffset = fibh.soffset + sizeof(struct fileIdentDesc) + liu + lfi;

			if (poffset >= lfi) {
				nameptr = (char *)(fibh.ebh->b_data + poffset - lfi);
			} else {
				if (!copy_name) {
					copy_name = kmalloc(UDF_NAME_LEN,
							    GFP_NOFS);
					if (!copy_name) {
						ret = -ENOMEM;
						goto out;
					}
				}
				nameptr = copy_name;
				memcpy(nameptr, udf_get_fi_ident(fi),
				       lfi - poffset);
				memcpy(nameptr + lfi - poffset,
				       fibh.ebh->b_data, poffset);
			}
		}

		if ((cfi.fileCharacteristics & FID_FILE_CHAR_DELETED) != 0) {
			if (!UDF_QUERY_FLAG(sb, UDF_FLAG_UNDELETE))
				continue;
		}

		if ((cfi.fileCharacteristics & FID_FILE_CHAR_HIDDEN) != 0) {
			if (!UDF_QUERY_FLAG(sb, UDF_FLAG_UNHIDE))
				continue;
		}

		if (cfi.fileCharacteristics & FID_FILE_CHAR_PARENT) {
			if (!dir_emit_dotdot(file, ctx))
				goto out;
			continue;
		}

		flen = udf_get_filename(sb, nameptr, lfi, fname, UDF_NAME_LEN);
		if (flen < 0)
			continue;

		tloc = lelb_to_cpu(cfi.icb.extLocation);
		iblock = udf_get_lb_pblock(sb, &tloc, 0);
		if (!dir_emit(ctx, fname, flen, iblock, DT_UNKNOWN))
			goto out;
	} /* end while */

	ctx->pos = (nf_pos >> 2) + 1;
	pos_valid = true;

out:
	if (pos_valid)
		file->f_version = inode_query_iversion(dir);
	if (fibh.sbh != fibh.ebh)
		brelse(fibh.ebh);
	brelse(fibh.sbh);
	brelse(epos.bh);
	kfree(fname);
	kfree(copy_name);

	return ret;
}

/* readdir and lookup functions */
const struct file_operations udf_dir_operations = {
	.llseek			= generic_file_llseek,
	.read			= generic_read_dir,
	.iterate_shared		= udf_readdir,
	.unlocked_ioctl		= udf_ioctl,
	.fsync			= generic_file_fsync,
};
