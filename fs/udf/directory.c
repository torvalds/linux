// SPDX-License-Identifier: GPL-2.0-only
/*
 * directory.c
 *
 * PURPOSE
 *	Directory related functions
 *
 */

#include "udfdecl.h"
#include "udf_i.h"

#include <linux/fs.h>
#include <linux/string.h>
#include <linux/bio.h>
#include <linux/crc-itu-t.h>
#include <linux/iversion.h>

static int udf_verify_fi(struct udf_fileident_iter *iter)
{
	unsigned int len;

	if (iter->fi.descTag.tagIdent != cpu_to_le16(TAG_IDENT_FID)) {
		udf_err(iter->dir->i_sb,
			"directory (ino %lu) has entry at pos %llu with incorrect tag %x\n",
			iter->dir->i_ino, (unsigned long long)iter->pos,
			le16_to_cpu(iter->fi.descTag.tagIdent));
		return -EFSCORRUPTED;
	}
	len = udf_dir_entry_len(&iter->fi);
	if (le16_to_cpu(iter->fi.lengthOfImpUse) & 3) {
		udf_err(iter->dir->i_sb,
			"directory (ino %lu) has entry at pos %llu with unaligned length of impUse field\n",
			iter->dir->i_ino, (unsigned long long)iter->pos);
		return -EFSCORRUPTED;
	}
	/*
	 * This is in fact allowed by the spec due to long impUse field but
	 * we don't support it. If there is real media with this large impUse
	 * field, support can be added.
	 */
	if (len > 1 << iter->dir->i_blkbits) {
		udf_err(iter->dir->i_sb,
			"directory (ino %lu) has too big (%u) entry at pos %llu\n",
			iter->dir->i_ino, len, (unsigned long long)iter->pos);
		return -EFSCORRUPTED;
	}
	if (iter->pos + len > iter->dir->i_size) {
		udf_err(iter->dir->i_sb,
			"directory (ino %lu) has entry past directory size at pos %llu\n",
			iter->dir->i_ino, (unsigned long long)iter->pos);
		return -EFSCORRUPTED;
	}
	if (udf_dir_entry_len(&iter->fi) !=
	    sizeof(struct tag) + le16_to_cpu(iter->fi.descTag.descCRCLength)) {
		udf_err(iter->dir->i_sb,
			"directory (ino %lu) has entry where CRC length (%u) does not match entry length (%u)\n",
			iter->dir->i_ino,
			(unsigned)le16_to_cpu(iter->fi.descTag.descCRCLength),
			(unsigned)(udf_dir_entry_len(&iter->fi) -
							sizeof(struct tag)));
		return -EFSCORRUPTED;
	}
	return 0;
}

static int udf_copy_fi(struct udf_fileident_iter *iter)
{
	struct udf_inode_info *iinfo = UDF_I(iter->dir);
	u32 blksize = 1 << iter->dir->i_blkbits;
	u32 off, len, nameoff;
	int err;

	/* Skip copying when we are at EOF */
	if (iter->pos >= iter->dir->i_size) {
		iter->name = NULL;
		return 0;
	}
	if (iter->dir->i_size < iter->pos + sizeof(struct fileIdentDesc)) {
		udf_err(iter->dir->i_sb,
			"directory (ino %lu) has entry straddling EOF\n",
			iter->dir->i_ino);
		return -EFSCORRUPTED;
	}
	if (iinfo->i_alloc_type == ICBTAG_FLAG_AD_IN_ICB) {
		memcpy(&iter->fi, iinfo->i_data + iinfo->i_lenEAttr + iter->pos,
		       sizeof(struct fileIdentDesc));
		err = udf_verify_fi(iter);
		if (err < 0)
			return err;
		iter->name = iinfo->i_data + iinfo->i_lenEAttr + iter->pos +
			sizeof(struct fileIdentDesc) +
			le16_to_cpu(iter->fi.lengthOfImpUse);
		return 0;
	}

	off = iter->pos & (blksize - 1);
	len = min_t(int, sizeof(struct fileIdentDesc), blksize - off);
	memcpy(&iter->fi, iter->bh[0]->b_data + off, len);
	if (len < sizeof(struct fileIdentDesc))
		memcpy((char *)(&iter->fi) + len, iter->bh[1]->b_data,
		       sizeof(struct fileIdentDesc) - len);
	err = udf_verify_fi(iter);
	if (err < 0)
		return err;

	/* Handle directory entry name */
	nameoff = off + sizeof(struct fileIdentDesc) +
				le16_to_cpu(iter->fi.lengthOfImpUse);
	if (off + udf_dir_entry_len(&iter->fi) <= blksize) {
		iter->name = iter->bh[0]->b_data + nameoff;
	} else if (nameoff >= blksize) {
		iter->name = iter->bh[1]->b_data + (nameoff - blksize);
	} else {
		iter->name = iter->namebuf;
		len = blksize - nameoff;
		memcpy(iter->name, iter->bh[0]->b_data + nameoff, len);
		memcpy(iter->name + len, iter->bh[1]->b_data,
		       iter->fi.lengthFileIdent - len);
	}
	return 0;
}

/* Readahead 8k once we are at 8k boundary */
static void udf_readahead_dir(struct udf_fileident_iter *iter)
{
	unsigned int ralen = 16 >> (iter->dir->i_blkbits - 9);
	struct buffer_head *tmp, *bha[16];
	int i, num;
	udf_pblk_t blk;

	if (iter->loffset & (ralen - 1))
		return;

	if (iter->loffset + ralen > (iter->elen >> iter->dir->i_blkbits))
		ralen = (iter->elen >> iter->dir->i_blkbits) - iter->loffset;
	num = 0;
	for (i = 0; i < ralen; i++) {
		blk = udf_get_lb_pblock(iter->dir->i_sb, &iter->eloc,
					iter->loffset + i);
		tmp = sb_getblk(iter->dir->i_sb, blk);
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

static struct buffer_head *udf_fiiter_bread_blk(struct udf_fileident_iter *iter)
{
	udf_pblk_t blk;

	udf_readahead_dir(iter);
	blk = udf_get_lb_pblock(iter->dir->i_sb, &iter->eloc, iter->loffset);
	return sb_bread(iter->dir->i_sb, blk);
}

/*
 * Updates loffset to point to next directory block; eloc, elen & epos are
 * updated if we need to traverse to the next extent as well.
 */
static int udf_fiiter_advance_blk(struct udf_fileident_iter *iter)
{
	iter->loffset++;
	if (iter->loffset < DIV_ROUND_UP(iter->elen, 1<<iter->dir->i_blkbits))
		return 0;

	iter->loffset = 0;
	if (udf_next_aext(iter->dir, &iter->epos, &iter->eloc, &iter->elen, 1)
			!= (EXT_RECORDED_ALLOCATED >> 30)) {
		if (iter->pos == iter->dir->i_size) {
			iter->elen = 0;
			return 0;
		}
		udf_err(iter->dir->i_sb,
			"extent after position %llu not allocated in directory (ino %lu)\n",
			(unsigned long long)iter->pos, iter->dir->i_ino);
		return -EFSCORRUPTED;
	}
	return 0;
}

static int udf_fiiter_load_bhs(struct udf_fileident_iter *iter)
{
	int blksize = 1 << iter->dir->i_blkbits;
	int off = iter->pos & (blksize - 1);
	int err;
	struct fileIdentDesc *fi;

	/* Is there any further extent we can map from? */
	if (!iter->bh[0] && iter->elen) {
		iter->bh[0] = udf_fiiter_bread_blk(iter);
		if (!iter->bh[0]) {
			err = -ENOMEM;
			goto out_brelse;
		}
		if (!buffer_uptodate(iter->bh[0])) {
			err = -EIO;
			goto out_brelse;
		}
	}
	/* There's no next block so we are done */
	if (iter->pos >= iter->dir->i_size)
		return 0;
	/* Need to fetch next block as well? */
	if (off + sizeof(struct fileIdentDesc) > blksize)
		goto fetch_next;
	fi = (struct fileIdentDesc *)(iter->bh[0]->b_data + off);
	/* Need to fetch next block to get name? */
	if (off + udf_dir_entry_len(fi) > blksize) {
fetch_next:
		err = udf_fiiter_advance_blk(iter);
		if (err)
			goto out_brelse;
		iter->bh[1] = udf_fiiter_bread_blk(iter);
		if (!iter->bh[1]) {
			err = -ENOMEM;
			goto out_brelse;
		}
		if (!buffer_uptodate(iter->bh[1])) {
			err = -EIO;
			goto out_brelse;
		}
	}
	return 0;
out_brelse:
	brelse(iter->bh[0]);
	brelse(iter->bh[1]);
	iter->bh[0] = iter->bh[1] = NULL;
	return err;
}

int udf_fiiter_init(struct udf_fileident_iter *iter, struct inode *dir,
		    loff_t pos)
{
	struct udf_inode_info *iinfo = UDF_I(dir);
	int err = 0;

	iter->dir = dir;
	iter->bh[0] = iter->bh[1] = NULL;
	iter->pos = pos;
	iter->elen = 0;
	iter->epos.bh = NULL;
	iter->name = NULL;
	/*
	 * When directory is verified, we don't expect directory iteration to
	 * fail and it can be difficult to undo without corrupting filesystem.
	 * So just do not allow memory allocation failures here.
	 */
	iter->namebuf = kmalloc(UDF_NAME_LEN_CS0, GFP_KERNEL | __GFP_NOFAIL);

	if (iinfo->i_alloc_type == ICBTAG_FLAG_AD_IN_ICB) {
		err = udf_copy_fi(iter);
		goto out;
	}

	if (inode_bmap(dir, iter->pos >> dir->i_blkbits, &iter->epos,
		       &iter->eloc, &iter->elen, &iter->loffset) !=
	    (EXT_RECORDED_ALLOCATED >> 30)) {
		if (pos == dir->i_size)
			return 0;
		udf_err(dir->i_sb,
			"position %llu not allocated in directory (ino %lu)\n",
			(unsigned long long)pos, dir->i_ino);
		err = -EFSCORRUPTED;
		goto out;
	}
	err = udf_fiiter_load_bhs(iter);
	if (err < 0)
		goto out;
	err = udf_copy_fi(iter);
out:
	if (err < 0)
		udf_fiiter_release(iter);
	return err;
}

int udf_fiiter_advance(struct udf_fileident_iter *iter)
{
	unsigned int oldoff, len;
	int blksize = 1 << iter->dir->i_blkbits;
	int err;

	oldoff = iter->pos & (blksize - 1);
	len = udf_dir_entry_len(&iter->fi);
	iter->pos += len;
	if (UDF_I(iter->dir)->i_alloc_type != ICBTAG_FLAG_AD_IN_ICB) {
		if (oldoff + len >= blksize) {
			brelse(iter->bh[0]);
			iter->bh[0] = NULL;
			/* Next block already loaded? */
			if (iter->bh[1]) {
				iter->bh[0] = iter->bh[1];
				iter->bh[1] = NULL;
			} else {
				err = udf_fiiter_advance_blk(iter);
				if (err < 0)
					return err;
			}
		}
		err = udf_fiiter_load_bhs(iter);
		if (err < 0)
			return err;
	}
	return udf_copy_fi(iter);
}

void udf_fiiter_release(struct udf_fileident_iter *iter)
{
	iter->dir = NULL;
	brelse(iter->bh[0]);
	brelse(iter->bh[1]);
	iter->bh[0] = iter->bh[1] = NULL;
	kfree(iter->namebuf);
	iter->namebuf = NULL;
}

static void udf_copy_to_bufs(void *buf1, int len1, void *buf2, int len2,
			     int off, void *src, int len)
{
	int copy;

	if (off >= len1) {
		off -= len1;
	} else {
		copy = min(off + len, len1) - off;
		memcpy(buf1 + off, src, copy);
		src += copy;
		len -= copy;
		off = 0;
	}
	if (len > 0) {
		if (WARN_ON_ONCE(off + len > len2 || !buf2))
			return;
		memcpy(buf2 + off, src, len);
	}
}

static uint16_t udf_crc_fi_bufs(void *buf1, int len1, void *buf2, int len2,
				int off, int len)
{
	int copy;
	uint16_t crc = 0;

	if (off >= len1) {
		off -= len1;
	} else {
		copy = min(off + len, len1) - off;
		crc = crc_itu_t(crc, buf1 + off, copy);
		len -= copy;
		off = 0;
	}
	if (len > 0) {
		if (WARN_ON_ONCE(off + len > len2 || !buf2))
			return 0;
		crc = crc_itu_t(crc, buf2 + off, len);
	}
	return crc;
}

static void udf_copy_fi_to_bufs(char *buf1, int len1, char *buf2, int len2,
				int off, struct fileIdentDesc *fi,
				uint8_t *impuse, uint8_t *name)
{
	uint16_t crc;
	int fioff = off;
	int crcoff = off + sizeof(struct tag);
	unsigned int crclen = udf_dir_entry_len(fi) - sizeof(struct tag);
	char zeros[UDF_NAME_PAD] = {};
	int endoff = off + udf_dir_entry_len(fi);

	udf_copy_to_bufs(buf1, len1, buf2, len2, off, fi,
			 sizeof(struct fileIdentDesc));
	off += sizeof(struct fileIdentDesc);
	if (impuse)
		udf_copy_to_bufs(buf1, len1, buf2, len2, off, impuse,
				 le16_to_cpu(fi->lengthOfImpUse));
	off += le16_to_cpu(fi->lengthOfImpUse);
	if (name) {
		udf_copy_to_bufs(buf1, len1, buf2, len2, off, name,
				 fi->lengthFileIdent);
		off += fi->lengthFileIdent;
		udf_copy_to_bufs(buf1, len1, buf2, len2, off, zeros,
				 endoff - off);
	}

	crc = udf_crc_fi_bufs(buf1, len1, buf2, len2, crcoff, crclen);
	fi->descTag.descCRC = cpu_to_le16(crc);
	fi->descTag.descCRCLength = cpu_to_le16(crclen);
	fi->descTag.tagChecksum = udf_tag_checksum(&fi->descTag);

	udf_copy_to_bufs(buf1, len1, buf2, len2, fioff, fi, sizeof(struct tag));
}

void udf_fiiter_write_fi(struct udf_fileident_iter *iter, uint8_t *impuse)
{
	struct udf_inode_info *iinfo = UDF_I(iter->dir);
	void *buf1, *buf2 = NULL;
	int len1, len2 = 0, off;
	int blksize = 1 << iter->dir->i_blkbits;

	off = iter->pos & (blksize - 1);
	if (iinfo->i_alloc_type == ICBTAG_FLAG_AD_IN_ICB) {
		buf1 = iinfo->i_data + iinfo->i_lenEAttr;
		len1 = iter->dir->i_size;
	} else {
		buf1 = iter->bh[0]->b_data;
		len1 = blksize;
		if (iter->bh[1]) {
			buf2 = iter->bh[1]->b_data;
			len2 = blksize;
		}
	}

	udf_copy_fi_to_bufs(buf1, len1, buf2, len2, off, &iter->fi, impuse,
			    iter->name == iter->namebuf ? iter->name : NULL);

	if (iinfo->i_alloc_type == ICBTAG_FLAG_AD_IN_ICB) {
		mark_inode_dirty(iter->dir);
	} else {
		mark_buffer_dirty_inode(iter->bh[0], iter->dir);
		if (iter->bh[1])
			mark_buffer_dirty_inode(iter->bh[1], iter->dir);
	}
	inode_inc_iversion(iter->dir);
}

void udf_fiiter_update_elen(struct udf_fileident_iter *iter, uint32_t new_elen)
{
	struct udf_inode_info *iinfo = UDF_I(iter->dir);
	int diff = new_elen - iter->elen;

	/* Skip update when we already went past the last extent */
	if (!iter->elen)
		return;
	iter->elen = new_elen;
	if (iinfo->i_alloc_type == ICBTAG_FLAG_AD_SHORT)
		iter->epos.offset -= sizeof(struct short_ad);
	else if (iinfo->i_alloc_type == ICBTAG_FLAG_AD_LONG)
		iter->epos.offset -= sizeof(struct long_ad);
	udf_write_aext(iter->dir, &iter->epos, &iter->eloc, iter->elen, 1);
	iinfo->i_lenExtents += diff;
	mark_inode_dirty(iter->dir);
}

/* Append new block to directory. @iter is expected to point at EOF */
int udf_fiiter_append_blk(struct udf_fileident_iter *iter)
{
	struct udf_inode_info *iinfo = UDF_I(iter->dir);
	int blksize = 1 << iter->dir->i_blkbits;
	struct buffer_head *bh;
	sector_t block;
	uint32_t old_elen = iter->elen;
	int err;

	if (WARN_ON_ONCE(iinfo->i_alloc_type == ICBTAG_FLAG_AD_IN_ICB))
		return -EINVAL;

	/* Round up last extent in the file */
	udf_fiiter_update_elen(iter, ALIGN(iter->elen, blksize));

	/* Allocate new block and refresh mapping information */
	block = iinfo->i_lenExtents >> iter->dir->i_blkbits;
	bh = udf_bread(iter->dir, block, 1, &err);
	if (!bh) {
		udf_fiiter_update_elen(iter, old_elen);
		return err;
	}
	if (inode_bmap(iter->dir, block, &iter->epos, &iter->eloc, &iter->elen,
		       &iter->loffset) != (EXT_RECORDED_ALLOCATED >> 30)) {
		udf_err(iter->dir->i_sb,
			"block %llu not allocated in directory (ino %lu)\n",
			(unsigned long long)block, iter->dir->i_ino);
		return -EFSCORRUPTED;
	}
	if (!(iter->pos & (blksize - 1))) {
		brelse(iter->bh[0]);
		iter->bh[0] = bh;
	} else {
		iter->bh[1] = bh;
	}
	return 0;
}

struct short_ad *udf_get_fileshortad(uint8_t *ptr, int maxoffset, uint32_t *offset,
			      int inc)
{
	struct short_ad *sa;

	if ((!ptr) || (!offset)) {
		pr_err("%s: invalidparms\n", __func__);
		return NULL;
	}

	if ((*offset + sizeof(struct short_ad)) > maxoffset)
		return NULL;
	else {
		sa = (struct short_ad *)ptr;
		if (sa->extLength == 0)
			return NULL;
	}

	if (inc)
		*offset += sizeof(struct short_ad);
	return sa;
}

struct long_ad *udf_get_filelongad(uint8_t *ptr, int maxoffset, uint32_t *offset, int inc)
{
	struct long_ad *la;

	if ((!ptr) || (!offset)) {
		pr_err("%s: invalidparms\n", __func__);
		return NULL;
	}

	if ((*offset + sizeof(struct long_ad)) > maxoffset)
		return NULL;
	else {
		la = (struct long_ad *)ptr;
		if (la->extLength == 0)
			return NULL;
	}

	if (inc)
		*offset += sizeof(struct long_ad);
	return la;
}
