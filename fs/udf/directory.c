/*
 * directory.c
 *
 * PURPOSE
 *	Directory related functions
 *
 * COPYRIGHT
 *	This file is distributed under the terms of the GNU General Public
 *	License (GPL). Copies of the GPL can be obtained from:
 *		ftp://prep.ai.mit.edu/pub/gnu/GPL
 *	Each contributing author retains all rights to their own work.
 */

#include "udfdecl.h"
#include "udf_i.h"

#include <linux/fs.h>
#include <linux/string.h>
#include <linux/buffer_head.h>

#if 0
static uint8_t *udf_filead_read(struct inode *dir, uint8_t *tmpad,
				uint8_t ad_size, kernel_lb_addr fe_loc,
				int *pos, int *offset, struct buffer_head **bh,
				int *error)
{
	int loffset = *offset;
	int block;
	uint8_t *ad;
	int remainder;

	*error = 0;

	ad = (uint8_t *)(*bh)->b_data + *offset;
	*offset += ad_size;

	if (!ad) {
		brelse(*bh);
		*error = 1;
		return NULL;
	}

	if (*offset == dir->i_sb->s_blocksize) {
		brelse(*bh);
		block = udf_get_lb_pblock(dir->i_sb, fe_loc, ++*pos);
		if (!block)
			return NULL;
		*bh = udf_tread(dir->i_sb, block);
		if (!*bh)
			return NULL;
	} else if (*offset > dir->i_sb->s_blocksize) {
		ad = tmpad;

		remainder = dir->i_sb->s_blocksize - loffset;
		memcpy((uint8_t *)ad, (*bh)->b_data + loffset, remainder);

		brelse(*bh);
		block = udf_get_lb_pblock(dir->i_sb, fe_loc, ++*pos);
		if (!block)
			return NULL;
		(*bh) = udf_tread(dir->i_sb, block);
		if (!*bh)
			return NULL;

		memcpy((uint8_t *)ad + remainder, (*bh)->b_data,
			ad_size - remainder);
		*offset = ad_size - remainder;
	}

	return ad;
}
#endif

struct fileIdentDesc *udf_fileident_read(struct inode *dir, loff_t *nf_pos,
					 struct udf_fileident_bh *fibh,
					 struct fileIdentDesc *cfi,
					 struct extent_position *epos,
					 kernel_lb_addr *eloc, uint32_t *elen,
					 sector_t *offset)
{
	struct fileIdentDesc *fi;
	int i, num, block;
	struct buffer_head *tmp, *bha[16];
	struct udf_inode_info *iinfo = UDF_I(dir);

	fibh->soffset = fibh->eoffset;

	if (iinfo->i_alloc_type == ICBTAG_FLAG_AD_IN_ICB) {
		fi = udf_get_fileident(iinfo->i_ext.i_data -
				       (iinfo->i_efe ?
					sizeof(struct extendedFileEntry) :
					sizeof(struct fileEntry)),
				       dir->i_sb->s_blocksize,
				       &(fibh->eoffset));
		if (!fi)
			return NULL;

		*nf_pos += ((fibh->eoffset - fibh->soffset) >> 2);

		memcpy((uint8_t *)cfi, (uint8_t *)fi,
		       sizeof(struct fileIdentDesc));

		return fi;
	}

	if (fibh->eoffset == dir->i_sb->s_blocksize) {
		int lextoffset = epos->offset;
		unsigned char blocksize_bits = dir->i_sb->s_blocksize_bits;

		if (udf_next_aext(dir, epos, eloc, elen, 1) !=
		    (EXT_RECORDED_ALLOCATED >> 30))
			return NULL;

		block = udf_get_lb_pblock(dir->i_sb, *eloc, *offset);

		(*offset)++;

		if ((*offset << blocksize_bits) >= *elen)
			*offset = 0;
		else
			epos->offset = lextoffset;

		brelse(fibh->sbh);
		fibh->sbh = fibh->ebh = udf_tread(dir->i_sb, block);
		if (!fibh->sbh)
			return NULL;
		fibh->soffset = fibh->eoffset = 0;

		if (!(*offset & ((16 >> (blocksize_bits - 9)) - 1))) {
			i = 16 >> (blocksize_bits - 9);
			if (i + *offset > (*elen >> blocksize_bits))
				i = (*elen >> blocksize_bits)-*offset;
			for (num = 0; i > 0; i--) {
				block = udf_get_lb_pblock(dir->i_sb, *eloc,
							  *offset + i);
				tmp = udf_tgetblk(dir->i_sb, block);
				if (tmp && !buffer_uptodate(tmp) &&
						!buffer_locked(tmp))
					bha[num++] = tmp;
				else
					brelse(tmp);
			}
			if (num) {
				ll_rw_block(READA, num, bha);
				for (i = 0; i < num; i++)
					brelse(bha[i]);
			}
		}
	} else if (fibh->sbh != fibh->ebh) {
		brelse(fibh->sbh);
		fibh->sbh = fibh->ebh;
	}

	fi = udf_get_fileident(fibh->sbh->b_data, dir->i_sb->s_blocksize,
			       &(fibh->eoffset));

	if (!fi)
		return NULL;

	*nf_pos += ((fibh->eoffset - fibh->soffset) >> 2);

	if (fibh->eoffset <= dir->i_sb->s_blocksize) {
		memcpy((uint8_t *)cfi, (uint8_t *)fi,
		       sizeof(struct fileIdentDesc));
	} else if (fibh->eoffset > dir->i_sb->s_blocksize) {
		int lextoffset = epos->offset;

		if (udf_next_aext(dir, epos, eloc, elen, 1) !=
		    (EXT_RECORDED_ALLOCATED >> 30))
			return NULL;

		block = udf_get_lb_pblock(dir->i_sb, *eloc, *offset);

		(*offset)++;

		if ((*offset << dir->i_sb->s_blocksize_bits) >= *elen)
			*offset = 0;
		else
			epos->offset = lextoffset;

		fibh->soffset -= dir->i_sb->s_blocksize;
		fibh->eoffset -= dir->i_sb->s_blocksize;

		fibh->ebh = udf_tread(dir->i_sb, block);
		if (!fibh->ebh)
			return NULL;

		if (sizeof(struct fileIdentDesc) > -fibh->soffset) {
			int fi_len;

			memcpy((uint8_t *)cfi, (uint8_t *)fi, -fibh->soffset);
			memcpy((uint8_t *)cfi - fibh->soffset,
			       fibh->ebh->b_data,
			       sizeof(struct fileIdentDesc) + fibh->soffset);

			fi_len = (sizeof(struct fileIdentDesc) +
				  cfi->lengthFileIdent +
				  le16_to_cpu(cfi->lengthOfImpUse) + 3) & ~3;

			*nf_pos += (fi_len - (fibh->eoffset - fibh->soffset))
					>> 2;
			fibh->eoffset = fibh->soffset + fi_len;
		} else {
			memcpy((uint8_t *)cfi, (uint8_t *)fi,
			       sizeof(struct fileIdentDesc));
		}
	}
	return fi;
}

struct fileIdentDesc *udf_get_fileident(void *buffer, int bufsize, int *offset)
{
	struct fileIdentDesc *fi;
	int lengthThisIdent;
	uint8_t *ptr;
	int padlen;

	if ((!buffer) || (!offset)) {
		udf_debug("invalidparms\n, buffer=%p, offset=%p\n", buffer,
			  offset);
		return NULL;
	}

	ptr = buffer;

	if ((*offset > 0) && (*offset < bufsize))
		ptr += *offset;
	fi = (struct fileIdentDesc *)ptr;
	if (fi->descTag.tagIdent != cpu_to_le16(TAG_IDENT_FID)) {
		udf_debug("0x%x != TAG_IDENT_FID\n",
			  le16_to_cpu(fi->descTag.tagIdent));
		udf_debug("offset: %u sizeof: %lu bufsize: %u\n",
			  *offset, (unsigned long)sizeof(struct fileIdentDesc),
			  bufsize);
		return NULL;
	}
	if ((*offset + sizeof(struct fileIdentDesc)) > bufsize)
		lengthThisIdent = sizeof(struct fileIdentDesc);
	else
		lengthThisIdent = sizeof(struct fileIdentDesc) +
			fi->lengthFileIdent + le16_to_cpu(fi->lengthOfImpUse);

	/* we need to figure padding, too! */
	padlen = lengthThisIdent % UDF_NAME_PAD;
	if (padlen)
		lengthThisIdent += (UDF_NAME_PAD - padlen);
	*offset = *offset + lengthThisIdent;

	return fi;
}

#if 0
static extent_ad *udf_get_fileextent(void *buffer, int bufsize, int *offset)
{
	extent_ad *ext;
	struct fileEntry *fe;
	uint8_t *ptr;

	if ((!buffer) || (!offset)) {
		printk(KERN_ERR "udf: udf_get_fileextent() invalidparms\n");
		return NULL;
	}

	fe = (struct fileEntry *)buffer;

	if (fe->descTag.tagIdent != cpu_to_le16(TAG_IDENT_FE)) {
		udf_debug("0x%x != TAG_IDENT_FE\n",
			  le16_to_cpu(fe->descTag.tagIdent));
		return NULL;
	}

	ptr = (uint8_t *)(fe->extendedAttr) +
		le32_to_cpu(fe->lengthExtendedAttr);

	if ((*offset > 0) && (*offset < le32_to_cpu(fe->lengthAllocDescs)))
		ptr += *offset;

	ext = (extent_ad *)ptr;

	*offset = *offset + sizeof(extent_ad);
	return ext;
}
#endif

short_ad *udf_get_fileshortad(uint8_t *ptr, int maxoffset, int *offset,
			      int inc)
{
	short_ad *sa;

	if ((!ptr) || (!offset)) {
		printk(KERN_ERR "udf: udf_get_fileshortad() invalidparms\n");
		return NULL;
	}

	if ((*offset < 0) || ((*offset + sizeof(short_ad)) > maxoffset))
		return NULL;
	else {
		sa = (short_ad *)ptr;
		if (sa->extLength == 0)
			return NULL;
	}

	if (inc)
		*offset += sizeof(short_ad);
	return sa;
}

long_ad *udf_get_filelongad(uint8_t *ptr, int maxoffset, int *offset, int inc)
{
	long_ad *la;

	if ((!ptr) || (!offset)) {
		printk(KERN_ERR "udf: udf_get_filelongad() invalidparms\n");
		return NULL;
	}

	if ((*offset < 0) || ((*offset + sizeof(long_ad)) > maxoffset))
		return NULL;
	else {
		la = (long_ad *)ptr;
		if (la->extLength == 0)
			return NULL;
	}

	if (inc)
		*offset += sizeof(long_ad);
	return la;
}
