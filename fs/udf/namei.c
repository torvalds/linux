// SPDX-License-Identifier: GPL-2.0-only
/*
 * namei.c
 *
 * PURPOSE
 *      Inode name handling routines for the OSTA-UDF(tm) filesystem.
 *
 * COPYRIGHT
 *  (C) 1998-2004 Ben Fennema
 *  (C) 1999-2000 Stelias Computing Inc
 *
 * HISTORY
 *
 *  12/12/98 blf  Created. Split out the lookup code from dir.c
 *  04/19/99 blf  link, mknod, symlink support
 */

#include "udfdecl.h"

#include "udf_i.h"
#include "udf_sb.h"
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/crc-itu-t.h>
#include <linux/exportfs.h>
#include <linux/iversion.h>

static inline int udf_match(int len1, const unsigned char *name1, int len2,
			    const unsigned char *name2)
{
	if (len1 != len2)
		return 0;

	return !memcmp(name1, name2, len1);
}

/**
 * udf_fiiter_find_entry - find entry in given directory.
 *
 * @dir:	directory inode to search in
 * @child:	qstr of the name
 * @iter:	iter to use for searching
 *
 * This function searches in the directory @dir for a file name @child. When
 * found, @iter points to the position in the directory with given entry.
 *
 * Returns 0 on success, < 0 on error (including -ENOENT).
 */
static int udf_fiiter_find_entry(struct inode *dir, const struct qstr *child,
				 struct udf_fileident_iter *iter)
{
	int flen;
	unsigned char *fname = NULL;
	struct super_block *sb = dir->i_sb;
	int isdotdot = child->len == 2 &&
		child->name[0] == '.' && child->name[1] == '.';
	int ret;

	fname = kmalloc(UDF_NAME_LEN, GFP_KERNEL);
	if (!fname)
		return -ENOMEM;

	for (ret = udf_fiiter_init(iter, dir, 0);
	     !ret && iter->pos < dir->i_size;
	     ret = udf_fiiter_advance(iter)) {
		if (iter->fi.fileCharacteristics & FID_FILE_CHAR_DELETED) {
			if (!UDF_QUERY_FLAG(sb, UDF_FLAG_UNDELETE))
				continue;
		}

		if (iter->fi.fileCharacteristics & FID_FILE_CHAR_HIDDEN) {
			if (!UDF_QUERY_FLAG(sb, UDF_FLAG_UNHIDE))
				continue;
		}

		if ((iter->fi.fileCharacteristics & FID_FILE_CHAR_PARENT) &&
		    isdotdot)
			goto out_ok;

		if (!iter->fi.lengthFileIdent)
			continue;

		flen = udf_get_filename(sb, iter->name,
				iter->fi.lengthFileIdent, fname, UDF_NAME_LEN);
		if (flen < 0) {
			ret = flen;
			goto out_err;
		}

		if (udf_match(flen, fname, child->len, child->name))
			goto out_ok;
	}
	if (!ret)
		ret = -ENOENT;

out_err:
	udf_fiiter_release(iter);
out_ok:
	kfree(fname);

	return ret;
}

static struct dentry *udf_lookup(struct inode *dir, struct dentry *dentry,
				 unsigned int flags)
{
	struct inode *inode = NULL;
	struct udf_fileident_iter iter;
	int err;

	if (dentry->d_name.len > UDF_NAME_LEN)
		return ERR_PTR(-ENAMETOOLONG);

	err = udf_fiiter_find_entry(dir, &dentry->d_name, &iter);
	if (err < 0 && err != -ENOENT)
		return ERR_PTR(err);

	if (err == 0) {
		struct kernel_lb_addr loc;

		loc = lelb_to_cpu(iter.fi.icb.extLocation);
		udf_fiiter_release(&iter);

		inode = udf_iget(dir->i_sb, &loc);
	}

	return d_splice_alias(inode, dentry);
}

static int udf_expand_dir_adinicb(struct inode *inode, udf_pblk_t *block)
{
	udf_pblk_t newblock;
	struct buffer_head *dbh = NULL;
	struct kernel_lb_addr eloc;
	struct extent_position epos;
	uint8_t alloctype;
	struct udf_inode_info *iinfo = UDF_I(inode);
	struct udf_fileident_iter iter;
	uint8_t *impuse;
	int ret;

	if (UDF_QUERY_FLAG(inode->i_sb, UDF_FLAG_USE_SHORT_AD))
		alloctype = ICBTAG_FLAG_AD_SHORT;
	else
		alloctype = ICBTAG_FLAG_AD_LONG;

	if (!inode->i_size) {
		iinfo->i_alloc_type = alloctype;
		mark_inode_dirty(inode);
		return 0;
	}

	/* alloc block, and copy data to it */
	*block = udf_new_block(inode->i_sb, inode,
			       iinfo->i_location.partitionReferenceNum,
			       iinfo->i_location.logicalBlockNum, &ret);
	if (!(*block))
		return ret;
	newblock = udf_get_pblock(inode->i_sb, *block,
				  iinfo->i_location.partitionReferenceNum,
				0);
	if (newblock == 0xffffffff)
		return -EFSCORRUPTED;
	dbh = sb_getblk(inode->i_sb, newblock);
	if (!dbh)
		return -ENOMEM;
	lock_buffer(dbh);
	memcpy(dbh->b_data, iinfo->i_data, inode->i_size);
	memset(dbh->b_data + inode->i_size, 0,
	       inode->i_sb->s_blocksize - inode->i_size);
	set_buffer_uptodate(dbh);
	unlock_buffer(dbh);

	/* Drop inline data, add block instead */
	iinfo->i_alloc_type = alloctype;
	memset(iinfo->i_data + iinfo->i_lenEAttr, 0, iinfo->i_lenAlloc);
	iinfo->i_lenAlloc = 0;
	eloc.logicalBlockNum = *block;
	eloc.partitionReferenceNum =
				iinfo->i_location.partitionReferenceNum;
	iinfo->i_lenExtents = inode->i_size;
	epos.bh = NULL;
	epos.block = iinfo->i_location;
	epos.offset = udf_file_entry_alloc_offset(inode);
	ret = udf_add_aext(inode, &epos, &eloc, inode->i_size, 0);
	brelse(epos.bh);
	if (ret < 0) {
		brelse(dbh);
		udf_free_blocks(inode->i_sb, inode, &eloc, 0, 1);
		return ret;
	}
	mark_inode_dirty(inode);

	/* Now fixup tags in moved directory entries */
	for (ret = udf_fiiter_init(&iter, inode, 0);
	     !ret && iter.pos < inode->i_size;
	     ret = udf_fiiter_advance(&iter)) {
		iter.fi.descTag.tagLocation = cpu_to_le32(*block);
		if (iter.fi.lengthOfImpUse != cpu_to_le16(0))
			impuse = dbh->b_data + iter.pos +
						sizeof(struct fileIdentDesc);
		else
			impuse = NULL;
		udf_fiiter_write_fi(&iter, impuse);
	}
	brelse(dbh);
	/*
	 * We don't expect the iteration to fail as the directory has been
	 * already verified to be correct
	 */
	WARN_ON_ONCE(ret);
	udf_fiiter_release(&iter);

	return 0;
}

static int udf_fiiter_add_entry(struct inode *dir, struct dentry *dentry,
				struct udf_fileident_iter *iter)
{
	struct udf_inode_info *dinfo = UDF_I(dir);
	int nfidlen, namelen = 0;
	int ret;
	int off, blksize = 1 << dir->i_blkbits;
	udf_pblk_t block;
	char name[UDF_NAME_LEN_CS0];

	if (dentry) {
		namelen = udf_put_filename(dir->i_sb, dentry->d_name.name,
					   dentry->d_name.len,
					   name, UDF_NAME_LEN_CS0);
		if (!namelen)
			return -ENAMETOOLONG;
	}
	nfidlen = ALIGN(sizeof(struct fileIdentDesc) + namelen, UDF_NAME_PAD);

	for (ret = udf_fiiter_init(iter, dir, 0);
	     !ret && iter->pos < dir->i_size;
	     ret = udf_fiiter_advance(iter)) {
		if (iter->fi.fileCharacteristics & FID_FILE_CHAR_DELETED) {
			if (udf_dir_entry_len(&iter->fi) == nfidlen) {
				iter->fi.descTag.tagSerialNum = cpu_to_le16(1);
				iter->fi.fileVersionNum = cpu_to_le16(1);
				iter->fi.fileCharacteristics = 0;
				iter->fi.lengthFileIdent = namelen;
				iter->fi.lengthOfImpUse = cpu_to_le16(0);
				memcpy(iter->namebuf, name, namelen);
				iter->name = iter->namebuf;
				return 0;
			}
		}
	}
	if (ret) {
		udf_fiiter_release(iter);
		return ret;
	}
	if (dinfo->i_alloc_type == ICBTAG_FLAG_AD_IN_ICB &&
	    blksize - udf_ext0_offset(dir) - iter->pos < nfidlen) {
		udf_fiiter_release(iter);
		ret = udf_expand_dir_adinicb(dir, &block);
		if (ret)
			return ret;
		ret = udf_fiiter_init(iter, dir, dir->i_size);
		if (ret < 0)
			return ret;
	}

	/* Get blocknumber to use for entry tag */
	if (dinfo->i_alloc_type == ICBTAG_FLAG_AD_IN_ICB) {
		block = dinfo->i_location.logicalBlockNum;
	} else {
		block = iter->eloc.logicalBlockNum +
				((iter->elen - 1) >> dir->i_blkbits);
	}
	off = iter->pos & (blksize - 1);
	if (!off)
		off = blksize;
	/* Entry fits into current block? */
	if (blksize - udf_ext0_offset(dir) - off >= nfidlen)
		goto store_fi;

	ret = udf_fiiter_append_blk(iter);
	if (ret) {
		udf_fiiter_release(iter);
		return ret;
	}

	/* Entry will be completely in the new block? Update tag location... */
	if (!(iter->pos & (blksize - 1)))
		block = iter->eloc.logicalBlockNum +
				((iter->elen - 1) >> dir->i_blkbits);
store_fi:
	memset(&iter->fi, 0, sizeof(struct fileIdentDesc));
	if (UDF_SB(dir->i_sb)->s_udfrev >= 0x0200)
		udf_new_tag((char *)(&iter->fi), TAG_IDENT_FID, 3, 1, block,
			    sizeof(struct tag));
	else
		udf_new_tag((char *)(&iter->fi), TAG_IDENT_FID, 2, 1, block,
			    sizeof(struct tag));
	iter->fi.fileVersionNum = cpu_to_le16(1);
	iter->fi.lengthFileIdent = namelen;
	iter->fi.lengthOfImpUse = cpu_to_le16(0);
	memcpy(iter->namebuf, name, namelen);
	iter->name = iter->namebuf;

	dir->i_size += nfidlen;
	if (dinfo->i_alloc_type == ICBTAG_FLAG_AD_IN_ICB) {
		dinfo->i_lenAlloc += nfidlen;
	} else {
		/* Truncate last extent to proper size */
		udf_fiiter_update_elen(iter, iter->elen -
					(dinfo->i_lenExtents - dir->i_size));
	}
	mark_inode_dirty(dir);

	return 0;
}

static void udf_fiiter_delete_entry(struct udf_fileident_iter *iter)
{
	iter->fi.fileCharacteristics |= FID_FILE_CHAR_DELETED;

	if (UDF_QUERY_FLAG(iter->dir->i_sb, UDF_FLAG_STRICT))
		memset(&iter->fi.icb, 0x00, sizeof(struct long_ad));

	udf_fiiter_write_fi(iter, NULL);
}

static void udf_add_fid_counter(struct super_block *sb, bool dir, int val)
{
	struct logicalVolIntegrityDescImpUse *lvidiu = udf_sb_lvidiu(sb);

	if (!lvidiu)
		return;
	mutex_lock(&UDF_SB(sb)->s_alloc_mutex);
	if (dir)
		le32_add_cpu(&lvidiu->numDirs, val);
	else
		le32_add_cpu(&lvidiu->numFiles, val);
	udf_updated_lvid(sb);
	mutex_unlock(&UDF_SB(sb)->s_alloc_mutex);
}

static int udf_add_nondir(struct dentry *dentry, struct inode *inode)
{
	struct udf_inode_info *iinfo = UDF_I(inode);
	struct inode *dir = d_inode(dentry->d_parent);
	struct udf_fileident_iter iter;
	int err;

	err = udf_fiiter_add_entry(dir, dentry, &iter);
	if (err) {
		inode_dec_link_count(inode);
		discard_new_inode(inode);
		return err;
	}
	iter.fi.icb.extLength = cpu_to_le32(inode->i_sb->s_blocksize);
	iter.fi.icb.extLocation = cpu_to_lelb(iinfo->i_location);
	*(__le32 *)((struct allocDescImpUse *)iter.fi.icb.impUse)->impUse =
		cpu_to_le32(iinfo->i_unique & 0x00000000FFFFFFFFUL);
	udf_fiiter_write_fi(&iter, NULL);
	inode_set_mtime_to_ts(dir, inode_set_ctime_current(dir));
	mark_inode_dirty(dir);
	udf_fiiter_release(&iter);
	udf_add_fid_counter(dir->i_sb, false, 1);
	d_instantiate_new(dentry, inode);

	return 0;
}

static int udf_create(struct mnt_idmap *idmap, struct inode *dir,
		      struct dentry *dentry, umode_t mode, bool excl)
{
	struct inode *inode = udf_new_inode(dir, mode);

	if (IS_ERR(inode))
		return PTR_ERR(inode);

	inode->i_data.a_ops = &udf_aops;
	inode->i_op = &udf_file_inode_operations;
	inode->i_fop = &udf_file_operations;
	mark_inode_dirty(inode);

	return udf_add_nondir(dentry, inode);
}

static int udf_tmpfile(struct mnt_idmap *idmap, struct inode *dir,
		       struct file *file, umode_t mode)
{
	struct inode *inode = udf_new_inode(dir, mode);

	if (IS_ERR(inode))
		return PTR_ERR(inode);

	inode->i_data.a_ops = &udf_aops;
	inode->i_op = &udf_file_inode_operations;
	inode->i_fop = &udf_file_operations;
	mark_inode_dirty(inode);
	d_tmpfile(file, inode);
	unlock_new_inode(inode);
	return finish_open_simple(file, 0);
}

static int udf_mknod(struct mnt_idmap *idmap, struct inode *dir,
		     struct dentry *dentry, umode_t mode, dev_t rdev)
{
	struct inode *inode;

	if (!old_valid_dev(rdev))
		return -EINVAL;

	inode = udf_new_inode(dir, mode);
	if (IS_ERR(inode))
		return PTR_ERR(inode);

	init_special_inode(inode, mode, rdev);
	return udf_add_nondir(dentry, inode);
}

static int udf_mkdir(struct mnt_idmap *idmap, struct inode *dir,
		     struct dentry *dentry, umode_t mode)
{
	struct inode *inode;
	struct udf_fileident_iter iter;
	int err;
	struct udf_inode_info *dinfo = UDF_I(dir);
	struct udf_inode_info *iinfo;

	inode = udf_new_inode(dir, S_IFDIR | mode);
	if (IS_ERR(inode))
		return PTR_ERR(inode);

	iinfo = UDF_I(inode);
	inode->i_op = &udf_dir_inode_operations;
	inode->i_fop = &udf_dir_operations;
	err = udf_fiiter_add_entry(inode, NULL, &iter);
	if (err) {
		clear_nlink(inode);
		discard_new_inode(inode);
		return err;
	}
	set_nlink(inode, 2);
	iter.fi.icb.extLength = cpu_to_le32(inode->i_sb->s_blocksize);
	iter.fi.icb.extLocation = cpu_to_lelb(dinfo->i_location);
	*(__le32 *)((struct allocDescImpUse *)iter.fi.icb.impUse)->impUse =
		cpu_to_le32(dinfo->i_unique & 0x00000000FFFFFFFFUL);
	iter.fi.fileCharacteristics =
			FID_FILE_CHAR_DIRECTORY | FID_FILE_CHAR_PARENT;
	udf_fiiter_write_fi(&iter, NULL);
	udf_fiiter_release(&iter);
	mark_inode_dirty(inode);

	err = udf_fiiter_add_entry(dir, dentry, &iter);
	if (err) {
		clear_nlink(inode);
		discard_new_inode(inode);
		return err;
	}
	iter.fi.icb.extLength = cpu_to_le32(inode->i_sb->s_blocksize);
	iter.fi.icb.extLocation = cpu_to_lelb(iinfo->i_location);
	*(__le32 *)((struct allocDescImpUse *)iter.fi.icb.impUse)->impUse =
		cpu_to_le32(iinfo->i_unique & 0x00000000FFFFFFFFUL);
	iter.fi.fileCharacteristics |= FID_FILE_CHAR_DIRECTORY;
	udf_fiiter_write_fi(&iter, NULL);
	udf_fiiter_release(&iter);
	udf_add_fid_counter(dir->i_sb, true, 1);
	inc_nlink(dir);
	inode_set_mtime_to_ts(dir, inode_set_ctime_current(dir));
	mark_inode_dirty(dir);
	d_instantiate_new(dentry, inode);

	return 0;
}

static int empty_dir(struct inode *dir)
{
	struct udf_fileident_iter iter;
	int ret;

	for (ret = udf_fiiter_init(&iter, dir, 0);
	     !ret && iter.pos < dir->i_size;
	     ret = udf_fiiter_advance(&iter)) {
		if (iter.fi.lengthFileIdent &&
		    !(iter.fi.fileCharacteristics & FID_FILE_CHAR_DELETED)) {
			udf_fiiter_release(&iter);
			return 0;
		}
	}
	udf_fiiter_release(&iter);

	return 1;
}

static int udf_rmdir(struct inode *dir, struct dentry *dentry)
{
	int ret;
	struct inode *inode = d_inode(dentry);
	struct udf_fileident_iter iter;
	struct kernel_lb_addr tloc;

	ret = udf_fiiter_find_entry(dir, &dentry->d_name, &iter);
	if (ret)
		goto out;

	ret = -EFSCORRUPTED;
	tloc = lelb_to_cpu(iter.fi.icb.extLocation);
	if (udf_get_lb_pblock(dir->i_sb, &tloc, 0) != inode->i_ino)
		goto end_rmdir;
	ret = -ENOTEMPTY;
	if (!empty_dir(inode))
		goto end_rmdir;
	udf_fiiter_delete_entry(&iter);
	if (inode->i_nlink != 2)
		udf_warn(inode->i_sb, "empty directory has nlink != 2 (%u)\n",
			 inode->i_nlink);
	clear_nlink(inode);
	inode->i_size = 0;
	inode_dec_link_count(dir);
	udf_add_fid_counter(dir->i_sb, true, -1);
	inode_set_mtime_to_ts(dir,
			      inode_set_ctime_to_ts(dir, inode_set_ctime_current(inode)));
	mark_inode_dirty(dir);
	ret = 0;
end_rmdir:
	udf_fiiter_release(&iter);
out:
	return ret;
}

static int udf_unlink(struct inode *dir, struct dentry *dentry)
{
	int ret;
	struct inode *inode = d_inode(dentry);
	struct udf_fileident_iter iter;
	struct kernel_lb_addr tloc;

	ret = udf_fiiter_find_entry(dir, &dentry->d_name, &iter);
	if (ret)
		goto out;

	ret = -EFSCORRUPTED;
	tloc = lelb_to_cpu(iter.fi.icb.extLocation);
	if (udf_get_lb_pblock(dir->i_sb, &tloc, 0) != inode->i_ino)
		goto end_unlink;

	if (!inode->i_nlink) {
		udf_debug("Deleting nonexistent file (%lu), %u\n",
			  inode->i_ino, inode->i_nlink);
		set_nlink(inode, 1);
	}
	udf_fiiter_delete_entry(&iter);
	inode_set_mtime_to_ts(dir, inode_set_ctime_current(dir));
	mark_inode_dirty(dir);
	inode_dec_link_count(inode);
	udf_add_fid_counter(dir->i_sb, false, -1);
	inode_set_ctime_to_ts(inode, inode_get_ctime(dir));
	ret = 0;
end_unlink:
	udf_fiiter_release(&iter);
out:
	return ret;
}

static int udf_symlink(struct mnt_idmap *idmap, struct inode *dir,
		       struct dentry *dentry, const char *symname)
{
	struct inode *inode;
	struct pathComponent *pc;
	const char *compstart;
	struct extent_position epos = {};
	int eoffset, elen = 0;
	uint8_t *ea;
	int err;
	udf_pblk_t block;
	unsigned char *name = NULL;
	int namelen;
	struct udf_inode_info *iinfo;
	struct super_block *sb = dir->i_sb;

	name = kmalloc(UDF_NAME_LEN_CS0, GFP_KERNEL);
	if (!name) {
		err = -ENOMEM;
		goto out;
	}

	inode = udf_new_inode(dir, S_IFLNK | 0777);
	if (IS_ERR(inode)) {
		err = PTR_ERR(inode);
		goto out;
	}

	iinfo = UDF_I(inode);
	down_write(&iinfo->i_data_sem);
	inode->i_data.a_ops = &udf_symlink_aops;
	inode->i_op = &udf_symlink_inode_operations;
	inode_nohighmem(inode);

	if (iinfo->i_alloc_type != ICBTAG_FLAG_AD_IN_ICB) {
		struct kernel_lb_addr eloc;
		uint32_t bsize;

		block = udf_new_block(sb, inode,
				iinfo->i_location.partitionReferenceNum,
				iinfo->i_location.logicalBlockNum, &err);
		if (!block)
			goto out_no_entry;
		epos.block = iinfo->i_location;
		epos.offset = udf_file_entry_alloc_offset(inode);
		epos.bh = NULL;
		eloc.logicalBlockNum = block;
		eloc.partitionReferenceNum =
				iinfo->i_location.partitionReferenceNum;
		bsize = sb->s_blocksize;
		iinfo->i_lenExtents = bsize;
		err = udf_add_aext(inode, &epos, &eloc, bsize, 0);
		brelse(epos.bh);
		if (err < 0) {
			udf_free_blocks(sb, inode, &eloc, 0, 1);
			goto out_no_entry;
		}

		block = udf_get_pblock(sb, block,
				iinfo->i_location.partitionReferenceNum,
				0);
		epos.bh = sb_getblk(sb, block);
		if (unlikely(!epos.bh)) {
			err = -ENOMEM;
			udf_free_blocks(sb, inode, &eloc, 0, 1);
			goto out_no_entry;
		}
		lock_buffer(epos.bh);
		memset(epos.bh->b_data, 0x00, bsize);
		set_buffer_uptodate(epos.bh);
		unlock_buffer(epos.bh);
		mark_buffer_dirty_inode(epos.bh, inode);
		ea = epos.bh->b_data + udf_ext0_offset(inode);
	} else
		ea = iinfo->i_data + iinfo->i_lenEAttr;

	eoffset = sb->s_blocksize - udf_ext0_offset(inode);
	pc = (struct pathComponent *)ea;

	if (*symname == '/') {
		do {
			symname++;
		} while (*symname == '/');

		pc->componentType = 1;
		pc->lengthComponentIdent = 0;
		pc->componentFileVersionNum = 0;
		elen += sizeof(struct pathComponent);
	}

	err = -ENAMETOOLONG;

	while (*symname) {
		if (elen + sizeof(struct pathComponent) > eoffset)
			goto out_no_entry;

		pc = (struct pathComponent *)(ea + elen);

		compstart = symname;

		do {
			symname++;
		} while (*symname && *symname != '/');

		pc->componentType = 5;
		pc->lengthComponentIdent = 0;
		pc->componentFileVersionNum = 0;
		if (compstart[0] == '.') {
			if ((symname - compstart) == 1)
				pc->componentType = 4;
			else if ((symname - compstart) == 2 &&
					compstart[1] == '.')
				pc->componentType = 3;
		}

		if (pc->componentType == 5) {
			namelen = udf_put_filename(sb, compstart,
						   symname - compstart,
						   name, UDF_NAME_LEN_CS0);
			if (!namelen)
				goto out_no_entry;

			if (elen + sizeof(struct pathComponent) + namelen >
					eoffset)
				goto out_no_entry;
			else
				pc->lengthComponentIdent = namelen;

			memcpy(pc->componentIdent, name, namelen);
		}

		elen += sizeof(struct pathComponent) + pc->lengthComponentIdent;

		if (*symname) {
			do {
				symname++;
			} while (*symname == '/');
		}
	}

	brelse(epos.bh);
	inode->i_size = elen;
	if (iinfo->i_alloc_type == ICBTAG_FLAG_AD_IN_ICB)
		iinfo->i_lenAlloc = inode->i_size;
	else
		udf_truncate_tail_extent(inode);
	mark_inode_dirty(inode);
	up_write(&iinfo->i_data_sem);

	err = udf_add_nondir(dentry, inode);
out:
	kfree(name);
	return err;

out_no_entry:
	up_write(&iinfo->i_data_sem);
	inode_dec_link_count(inode);
	discard_new_inode(inode);
	goto out;
}

static int udf_link(struct dentry *old_dentry, struct inode *dir,
		    struct dentry *dentry)
{
	struct inode *inode = d_inode(old_dentry);
	struct udf_fileident_iter iter;
	int err;

	err = udf_fiiter_add_entry(dir, dentry, &iter);
	if (err)
		return err;
	iter.fi.icb.extLength = cpu_to_le32(inode->i_sb->s_blocksize);
	iter.fi.icb.extLocation = cpu_to_lelb(UDF_I(inode)->i_location);
	if (UDF_SB(inode->i_sb)->s_lvid_bh) {
		*(__le32 *)((struct allocDescImpUse *)iter.fi.icb.impUse)->impUse =
			cpu_to_le32(lvid_get_unique_id(inode->i_sb));
	}
	udf_fiiter_write_fi(&iter, NULL);
	udf_fiiter_release(&iter);

	inc_nlink(inode);
	udf_add_fid_counter(dir->i_sb, false, 1);
	inode_set_ctime_current(inode);
	mark_inode_dirty(inode);
	inode_set_mtime_to_ts(dir, inode_set_ctime_current(dir));
	mark_inode_dirty(dir);
	ihold(inode);
	d_instantiate(dentry, inode);

	return 0;
}

/* Anybody can rename anything with this: the permission checks are left to the
 * higher-level routines.
 */
static int udf_rename(struct mnt_idmap *idmap, struct inode *old_dir,
		      struct dentry *old_dentry, struct inode *new_dir,
		      struct dentry *new_dentry, unsigned int flags)
{
	struct inode *old_inode = d_inode(old_dentry);
	struct inode *new_inode = d_inode(new_dentry);
	struct udf_fileident_iter oiter, niter, diriter;
	bool has_diriter = false, is_dir = false;
	int retval;
	struct kernel_lb_addr tloc;

	if (flags & ~RENAME_NOREPLACE)
		return -EINVAL;

	retval = udf_fiiter_find_entry(old_dir, &old_dentry->d_name, &oiter);
	if (retval)
		return retval;

	tloc = lelb_to_cpu(oiter.fi.icb.extLocation);
	if (udf_get_lb_pblock(old_dir->i_sb, &tloc, 0) != old_inode->i_ino) {
		retval = -ENOENT;
		goto out_oiter;
	}

	if (S_ISDIR(old_inode->i_mode)) {
		if (new_inode) {
			retval = -ENOTEMPTY;
			if (!empty_dir(new_inode))
				goto out_oiter;
		}
		is_dir = true;
	}
	if (is_dir && old_dir != new_dir) {
		retval = udf_fiiter_find_entry(old_inode, &dotdot_name,
					       &diriter);
		if (retval == -ENOENT) {
			udf_err(old_inode->i_sb,
				"directory (ino %lu) has no '..' entry\n",
				old_inode->i_ino);
			retval = -EFSCORRUPTED;
		}
		if (retval)
			goto out_oiter;
		has_diriter = true;
		tloc = lelb_to_cpu(diriter.fi.icb.extLocation);
		if (udf_get_lb_pblock(old_inode->i_sb, &tloc, 0) !=
				old_dir->i_ino) {
			retval = -EFSCORRUPTED;
			udf_err(old_inode->i_sb,
				"directory (ino %lu) has parent entry pointing to another inode (%lu != %u)\n",
				old_inode->i_ino, old_dir->i_ino,
				udf_get_lb_pblock(old_inode->i_sb, &tloc, 0));
			goto out_oiter;
		}
	}

	retval = udf_fiiter_find_entry(new_dir, &new_dentry->d_name, &niter);
	if (retval && retval != -ENOENT)
		goto out_oiter;
	/* Entry found but not passed by VFS? */
	if (!retval && !new_inode) {
		retval = -EFSCORRUPTED;
		udf_fiiter_release(&niter);
		goto out_oiter;
	}
	/* Entry not found? Need to add one... */
	if (retval) {
		udf_fiiter_release(&niter);
		retval = udf_fiiter_add_entry(new_dir, new_dentry, &niter);
		if (retval)
			goto out_oiter;
	}

	/*
	 * Like most other Unix systems, set the ctime for inodes on a
	 * rename.
	 */
	inode_set_ctime_current(old_inode);
	mark_inode_dirty(old_inode);

	/*
	 * ok, that's it
	 */
	niter.fi.fileVersionNum = oiter.fi.fileVersionNum;
	niter.fi.fileCharacteristics = oiter.fi.fileCharacteristics;
	memcpy(&(niter.fi.icb), &(oiter.fi.icb), sizeof(oiter.fi.icb));
	udf_fiiter_write_fi(&niter, NULL);
	udf_fiiter_release(&niter);

	/*
	 * The old entry may have moved due to new entry allocation. Find it
	 * again.
	 */
	udf_fiiter_release(&oiter);
	retval = udf_fiiter_find_entry(old_dir, &old_dentry->d_name, &oiter);
	if (retval) {
		udf_err(old_dir->i_sb,
			"failed to find renamed entry again in directory (ino %lu)\n",
			old_dir->i_ino);
	} else {
		udf_fiiter_delete_entry(&oiter);
		udf_fiiter_release(&oiter);
	}

	if (new_inode) {
		inode_set_ctime_current(new_inode);
		inode_dec_link_count(new_inode);
		udf_add_fid_counter(old_dir->i_sb, S_ISDIR(new_inode->i_mode),
				    -1);
	}
	inode_set_mtime_to_ts(old_dir, inode_set_ctime_current(old_dir));
	inode_set_mtime_to_ts(new_dir, inode_set_ctime_current(new_dir));
	mark_inode_dirty(old_dir);
	mark_inode_dirty(new_dir);

	if (has_diriter) {
		diriter.fi.icb.extLocation =
					cpu_to_lelb(UDF_I(new_dir)->i_location);
		udf_fiiter_write_fi(&diriter, NULL);
		udf_fiiter_release(&diriter);
	}

	if (is_dir) {
		inode_dec_link_count(old_dir);
		if (new_inode)
			inode_dec_link_count(new_inode);
		else {
			inc_nlink(new_dir);
			mark_inode_dirty(new_dir);
		}
	}
	return 0;
out_oiter:
	if (has_diriter)
		udf_fiiter_release(&diriter);
	udf_fiiter_release(&oiter);

	return retval;
}

static struct dentry *udf_get_parent(struct dentry *child)
{
	struct kernel_lb_addr tloc;
	struct udf_fileident_iter iter;
	int err;

	err = udf_fiiter_find_entry(d_inode(child), &dotdot_name, &iter);
	if (err)
		return ERR_PTR(err);

	tloc = lelb_to_cpu(iter.fi.icb.extLocation);
	udf_fiiter_release(&iter);
	return d_obtain_alias(udf_iget(child->d_sb, &tloc));
}


static struct dentry *udf_nfs_get_inode(struct super_block *sb, u32 block,
					u16 partref, __u32 generation)
{
	struct inode *inode;
	struct kernel_lb_addr loc;

	if (block == 0)
		return ERR_PTR(-ESTALE);

	loc.logicalBlockNum = block;
	loc.partitionReferenceNum = partref;
	inode = udf_iget(sb, &loc);

	if (IS_ERR(inode))
		return ERR_CAST(inode);

	if (generation && inode->i_generation != generation) {
		iput(inode);
		return ERR_PTR(-ESTALE);
	}
	return d_obtain_alias(inode);
}

static struct dentry *udf_fh_to_dentry(struct super_block *sb,
				       struct fid *fid, int fh_len, int fh_type)
{
	if (fh_len < 3 ||
	    (fh_type != FILEID_UDF_WITH_PARENT &&
	     fh_type != FILEID_UDF_WITHOUT_PARENT))
		return NULL;

	return udf_nfs_get_inode(sb, fid->udf.block, fid->udf.partref,
			fid->udf.generation);
}

static struct dentry *udf_fh_to_parent(struct super_block *sb,
				       struct fid *fid, int fh_len, int fh_type)
{
	if (fh_len < 5 || fh_type != FILEID_UDF_WITH_PARENT)
		return NULL;

	return udf_nfs_get_inode(sb, fid->udf.parent_block,
				 fid->udf.parent_partref,
				 fid->udf.parent_generation);
}
static int udf_encode_fh(struct inode *inode, __u32 *fh, int *lenp,
			 struct inode *parent)
{
	int len = *lenp;
	struct kernel_lb_addr location = UDF_I(inode)->i_location;
	struct fid *fid = (struct fid *)fh;
	int type = FILEID_UDF_WITHOUT_PARENT;

	if (parent && (len < 5)) {
		*lenp = 5;
		return FILEID_INVALID;
	} else if (len < 3) {
		*lenp = 3;
		return FILEID_INVALID;
	}

	*lenp = 3;
	fid->udf.block = location.logicalBlockNum;
	fid->udf.partref = location.partitionReferenceNum;
	fid->udf.parent_partref = 0;
	fid->udf.generation = inode->i_generation;

	if (parent) {
		location = UDF_I(parent)->i_location;
		fid->udf.parent_block = location.logicalBlockNum;
		fid->udf.parent_partref = location.partitionReferenceNum;
		fid->udf.parent_generation = inode->i_generation;
		*lenp = 5;
		type = FILEID_UDF_WITH_PARENT;
	}

	return type;
}

const struct export_operations udf_export_ops = {
	.encode_fh	= udf_encode_fh,
	.fh_to_dentry   = udf_fh_to_dentry,
	.fh_to_parent   = udf_fh_to_parent,
	.get_parent     = udf_get_parent,
};

const struct inode_operations udf_dir_inode_operations = {
	.lookup				= udf_lookup,
	.create				= udf_create,
	.link				= udf_link,
	.unlink				= udf_unlink,
	.symlink			= udf_symlink,
	.mkdir				= udf_mkdir,
	.rmdir				= udf_rmdir,
	.mknod				= udf_mknod,
	.rename				= udf_rename,
	.tmpfile			= udf_tmpfile,
};
