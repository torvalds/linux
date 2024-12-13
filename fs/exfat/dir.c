// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2012-2013 Samsung Electronics Co., Ltd.
 */

#include <linux/slab.h>
#include <linux/compat.h>
#include <linux/bio.h>
#include <linux/buffer_head.h>

#include "exfat_raw.h"
#include "exfat_fs.h"

static int exfat_extract_uni_name(struct exfat_dentry *ep,
		unsigned short *uniname)
{
	int i, len = 0;

	for (i = 0; i < EXFAT_FILE_NAME_LEN; i++) {
		*uniname = le16_to_cpu(ep->dentry.name.unicode_0_14[i]);
		if (*uniname == 0x0)
			return len;
		uniname++;
		len++;
	}

	*uniname = 0x0;
	return len;

}

static int exfat_get_uniname_from_ext_entry(struct super_block *sb,
		struct exfat_chain *p_dir, int entry, unsigned short *uniname)
{
	int i, err;
	struct exfat_entry_set_cache es;
	unsigned int uni_len = 0, len;

	err = exfat_get_dentry_set(&es, sb, p_dir, entry, ES_ALL_ENTRIES);
	if (err)
		return err;

	/*
	 * First entry  : file entry
	 * Second entry : stream-extension entry
	 * Third entry  : first file-name entry
	 * So, the index of first file-name dentry should start from 2.
	 */
	for (i = ES_IDX_FIRST_FILENAME; i < es.num_entries; i++) {
		struct exfat_dentry *ep = exfat_get_dentry_cached(&es, i);

		/* end of name entry */
		if (exfat_get_entry_type(ep) != TYPE_EXTEND)
			break;

		len = exfat_extract_uni_name(ep, uniname);
		uni_len += len;
		if (len != EXFAT_FILE_NAME_LEN || uni_len >= MAX_NAME_LENGTH)
			break;
		uniname += EXFAT_FILE_NAME_LEN;
	}

	exfat_put_dentry_set(&es, false);
	return 0;
}

/* read a directory entry from the opened directory */
static int exfat_readdir(struct inode *inode, loff_t *cpos, struct exfat_dir_entry *dir_entry)
{
	int i, dentries_per_clu, num_ext, err;
	unsigned int type, clu_offset, max_dentries;
	struct exfat_chain dir, clu;
	struct exfat_uni_name uni_name;
	struct exfat_dentry *ep;
	struct super_block *sb = inode->i_sb;
	struct exfat_sb_info *sbi = EXFAT_SB(sb);
	struct exfat_inode_info *ei = EXFAT_I(inode);
	unsigned int dentry = EXFAT_B_TO_DEN(*cpos) & 0xFFFFFFFF;
	struct buffer_head *bh;

	/* check if the given file ID is opened */
	if (ei->type != TYPE_DIR)
		return -EPERM;

	if (ei->entry == -1)
		exfat_chain_set(&dir, sbi->root_dir, 0, ALLOC_FAT_CHAIN);
	else
		exfat_chain_set(&dir, ei->start_clu,
			EXFAT_B_TO_CLU(i_size_read(inode), sbi), ei->flags);

	dentries_per_clu = sbi->dentries_per_clu;
	max_dentries = (unsigned int)min_t(u64, MAX_EXFAT_DENTRIES,
				(u64)EXFAT_CLU_TO_DEN(sbi->num_clusters, sbi));

	clu_offset = EXFAT_DEN_TO_CLU(dentry, sbi);
	exfat_chain_dup(&clu, &dir);

	if (clu.flags == ALLOC_NO_FAT_CHAIN) {
		clu.dir += clu_offset;
		clu.size -= clu_offset;
	} else {
		/* hint_information */
		if (clu_offset > 0 && ei->hint_bmap.off != EXFAT_EOF_CLUSTER &&
		    ei->hint_bmap.off > 0 && clu_offset >= ei->hint_bmap.off) {
			clu_offset -= ei->hint_bmap.off;
			clu.dir = ei->hint_bmap.clu;
		}

		while (clu_offset > 0 && clu.dir != EXFAT_EOF_CLUSTER) {
			if (exfat_get_next_cluster(sb, &(clu.dir)))
				return -EIO;

			clu_offset--;
		}
	}

	while (clu.dir != EXFAT_EOF_CLUSTER && dentry < max_dentries) {
		i = dentry & (dentries_per_clu - 1);

		for ( ; i < dentries_per_clu; i++, dentry++) {
			ep = exfat_get_dentry(sb, &clu, i, &bh);
			if (!ep)
				return -EIO;

			type = exfat_get_entry_type(ep);
			if (type == TYPE_UNUSED) {
				brelse(bh);
				goto out;
			}

			if (type != TYPE_FILE && type != TYPE_DIR) {
				brelse(bh);
				continue;
			}

			num_ext = ep->dentry.file.num_ext;
			dir_entry->attr = le16_to_cpu(ep->dentry.file.attr);
			exfat_get_entry_time(sbi, &dir_entry->crtime,
					ep->dentry.file.create_tz,
					ep->dentry.file.create_time,
					ep->dentry.file.create_date,
					ep->dentry.file.create_time_cs);
			exfat_get_entry_time(sbi, &dir_entry->mtime,
					ep->dentry.file.modify_tz,
					ep->dentry.file.modify_time,
					ep->dentry.file.modify_date,
					ep->dentry.file.modify_time_cs);
			exfat_get_entry_time(sbi, &dir_entry->atime,
					ep->dentry.file.access_tz,
					ep->dentry.file.access_time,
					ep->dentry.file.access_date,
					0);

			*uni_name.name = 0x0;
			err = exfat_get_uniname_from_ext_entry(sb, &clu, i,
				uni_name.name);
			if (err) {
				brelse(bh);
				continue;
			}
			exfat_utf16_to_nls(sb, &uni_name,
				dir_entry->namebuf.lfn,
				dir_entry->namebuf.lfnbuf_len);
			brelse(bh);

			ep = exfat_get_dentry(sb, &clu, i + 1, &bh);
			if (!ep)
				return -EIO;
			dir_entry->size =
				le64_to_cpu(ep->dentry.stream.valid_size);
			dir_entry->entry = dentry;
			brelse(bh);

			ei->hint_bmap.off = EXFAT_DEN_TO_CLU(dentry, sbi);
			ei->hint_bmap.clu = clu.dir;

			*cpos = EXFAT_DEN_TO_B(dentry + 1 + num_ext);
			return 0;
		}

		if (clu.flags == ALLOC_NO_FAT_CHAIN) {
			if (--clu.size > 0)
				clu.dir++;
			else
				clu.dir = EXFAT_EOF_CLUSTER;
		} else {
			if (exfat_get_next_cluster(sb, &(clu.dir)))
				return -EIO;
		}
	}

out:
	dir_entry->namebuf.lfn[0] = '\0';
	*cpos = EXFAT_DEN_TO_B(dentry);
	return 0;
}

static void exfat_init_namebuf(struct exfat_dentry_namebuf *nb)
{
	nb->lfn = NULL;
	nb->lfnbuf_len = 0;
}

static int exfat_alloc_namebuf(struct exfat_dentry_namebuf *nb)
{
	nb->lfn = __getname();
	if (!nb->lfn)
		return -ENOMEM;
	nb->lfnbuf_len = MAX_VFSNAME_BUF_SIZE;
	return 0;
}

static void exfat_free_namebuf(struct exfat_dentry_namebuf *nb)
{
	if (!nb->lfn)
		return;

	__putname(nb->lfn);
	exfat_init_namebuf(nb);
}

/*
 * Before calling dir_emit*(), sbi->s_lock should be released
 * because page fault can occur in dir_emit*().
 */
#define ITER_POS_FILLED_DOTS    (2)
static int exfat_iterate(struct file *file, struct dir_context *ctx)
{
	struct inode *inode = file_inode(file);
	struct super_block *sb = inode->i_sb;
	struct inode *tmp;
	struct exfat_dir_entry de;
	struct exfat_dentry_namebuf *nb = &(de.namebuf);
	struct exfat_inode_info *ei = EXFAT_I(inode);
	unsigned long inum;
	loff_t cpos, i_pos;
	int err = 0, fake_offset = 0;

	exfat_init_namebuf(nb);

	cpos = ctx->pos;
	if (!dir_emit_dots(file, ctx))
		goto out;

	if (ctx->pos == ITER_POS_FILLED_DOTS) {
		cpos = 0;
		fake_offset = 1;
	}

	cpos = round_up(cpos, DENTRY_SIZE);

	/* name buffer should be allocated before use */
	err = exfat_alloc_namebuf(nb);
	if (err)
		goto out;
get_new:
	mutex_lock(&EXFAT_SB(sb)->s_lock);

	if (ei->flags == ALLOC_NO_FAT_CHAIN && cpos >= i_size_read(inode))
		goto end_of_dir;

	err = exfat_readdir(inode, &cpos, &de);
	if (err) {
		/*
		 * At least we tried to read a sector.
		 * Move cpos to next sector position (should be aligned).
		 */
		if (err == -EIO) {
			cpos += 1 << (sb->s_blocksize_bits);
			cpos &= ~(sb->s_blocksize - 1);
		}

		err = -EIO;
		goto end_of_dir;
	}

	if (!nb->lfn[0])
		goto end_of_dir;

	i_pos = ((loff_t)ei->start_clu << 32) |	(de.entry & 0xffffffff);
	tmp = exfat_iget(sb, i_pos);
	if (tmp) {
		inum = tmp->i_ino;
		iput(tmp);
	} else {
		inum = iunique(sb, EXFAT_ROOT_INO);
	}

	mutex_unlock(&EXFAT_SB(sb)->s_lock);
	if (!dir_emit(ctx, nb->lfn, strlen(nb->lfn), inum,
			(de.attr & EXFAT_ATTR_SUBDIR) ? DT_DIR : DT_REG))
		goto out;
	ctx->pos = cpos;
	goto get_new;

end_of_dir:
	if (!cpos && fake_offset)
		cpos = ITER_POS_FILLED_DOTS;
	ctx->pos = cpos;
	mutex_unlock(&EXFAT_SB(sb)->s_lock);
out:
	/*
	 * To improve performance, free namebuf after unlock sb_lock.
	 * If namebuf is not allocated, this function do nothing
	 */
	exfat_free_namebuf(nb);
	return err;
}

WRAP_DIR_ITER(exfat_iterate) // FIXME!
const struct file_operations exfat_dir_operations = {
	.llseek		= generic_file_llseek,
	.read		= generic_read_dir,
	.iterate_shared	= shared_exfat_iterate,
	.unlocked_ioctl = exfat_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = exfat_compat_ioctl,
#endif
	.fsync		= exfat_file_fsync,
};

int exfat_alloc_new_dir(struct inode *inode, struct exfat_chain *clu)
{
	int ret;

	exfat_chain_set(clu, EXFAT_EOF_CLUSTER, 0, ALLOC_NO_FAT_CHAIN);

	ret = exfat_alloc_cluster(inode, 1, clu, IS_DIRSYNC(inode));
	if (ret)
		return ret;

	return exfat_zeroed_cluster(inode, clu->dir);
}

int exfat_calc_num_entries(struct exfat_uni_name *p_uniname)
{
	int len;

	len = p_uniname->name_len;
	if (len == 0)
		return -EINVAL;

	/* 1 file entry + 1 stream entry + name entries */
	return ES_ENTRY_NUM(len);
}

unsigned int exfat_get_entry_type(struct exfat_dentry *ep)
{
	if (ep->type == EXFAT_UNUSED)
		return TYPE_UNUSED;
	if (IS_EXFAT_DELETED(ep->type))
		return TYPE_DELETED;
	if (ep->type == EXFAT_INVAL)
		return TYPE_INVALID;
	if (IS_EXFAT_CRITICAL_PRI(ep->type)) {
		if (ep->type == EXFAT_BITMAP)
			return TYPE_BITMAP;
		if (ep->type == EXFAT_UPCASE)
			return TYPE_UPCASE;
		if (ep->type == EXFAT_VOLUME)
			return TYPE_VOLUME;
		if (ep->type == EXFAT_FILE) {
			if (le16_to_cpu(ep->dentry.file.attr) & EXFAT_ATTR_SUBDIR)
				return TYPE_DIR;
			return TYPE_FILE;
		}
		return TYPE_CRITICAL_PRI;
	}
	if (IS_EXFAT_BENIGN_PRI(ep->type)) {
		if (ep->type == EXFAT_GUID)
			return TYPE_GUID;
		if (ep->type == EXFAT_PADDING)
			return TYPE_PADDING;
		if (ep->type == EXFAT_ACLTAB)
			return TYPE_ACLTAB;
		return TYPE_BENIGN_PRI;
	}
	if (IS_EXFAT_CRITICAL_SEC(ep->type)) {
		if (ep->type == EXFAT_STREAM)
			return TYPE_STREAM;
		if (ep->type == EXFAT_NAME)
			return TYPE_EXTEND;
		if (ep->type == EXFAT_ACL)
			return TYPE_ACL;
		return TYPE_CRITICAL_SEC;
	}

	if (ep->type == EXFAT_VENDOR_EXT)
		return TYPE_VENDOR_EXT;
	if (ep->type == EXFAT_VENDOR_ALLOC)
		return TYPE_VENDOR_ALLOC;

	return TYPE_BENIGN_SEC;
}

static void exfat_set_entry_type(struct exfat_dentry *ep, unsigned int type)
{
	if (type == TYPE_UNUSED) {
		ep->type = EXFAT_UNUSED;
	} else if (type == TYPE_DELETED) {
		ep->type &= EXFAT_DELETE;
	} else if (type == TYPE_STREAM) {
		ep->type = EXFAT_STREAM;
	} else if (type == TYPE_EXTEND) {
		ep->type = EXFAT_NAME;
	} else if (type == TYPE_BITMAP) {
		ep->type = EXFAT_BITMAP;
	} else if (type == TYPE_UPCASE) {
		ep->type = EXFAT_UPCASE;
	} else if (type == TYPE_VOLUME) {
		ep->type = EXFAT_VOLUME;
	} else if (type == TYPE_DIR) {
		ep->type = EXFAT_FILE;
		ep->dentry.file.attr = cpu_to_le16(EXFAT_ATTR_SUBDIR);
	} else if (type == TYPE_FILE) {
		ep->type = EXFAT_FILE;
		ep->dentry.file.attr = cpu_to_le16(EXFAT_ATTR_ARCHIVE);
	}
}

static void exfat_init_stream_entry(struct exfat_dentry *ep,
		unsigned int start_clu, unsigned long long size)
{
	memset(ep, 0, sizeof(*ep));
	exfat_set_entry_type(ep, TYPE_STREAM);
	if (size == 0)
		ep->dentry.stream.flags = ALLOC_FAT_CHAIN;
	else
		ep->dentry.stream.flags = ALLOC_NO_FAT_CHAIN;
	ep->dentry.stream.start_clu = cpu_to_le32(start_clu);
	ep->dentry.stream.valid_size = cpu_to_le64(size);
	ep->dentry.stream.size = cpu_to_le64(size);
}

static void exfat_init_name_entry(struct exfat_dentry *ep,
		unsigned short *uniname)
{
	int i;

	exfat_set_entry_type(ep, TYPE_EXTEND);
	ep->dentry.name.flags = 0x0;

	for (i = 0; i < EXFAT_FILE_NAME_LEN; i++) {
		if (*uniname != 0x0) {
			ep->dentry.name.unicode_0_14[i] = cpu_to_le16(*uniname);
			uniname++;
		} else {
			ep->dentry.name.unicode_0_14[i] = 0x0;
		}
	}
}

void exfat_init_dir_entry(struct exfat_entry_set_cache *es,
		unsigned int type, unsigned int start_clu,
		unsigned long long size, struct timespec64 *ts)
{
	struct super_block *sb = es->sb;
	struct exfat_sb_info *sbi = EXFAT_SB(sb);
	struct exfat_dentry *ep;

	ep = exfat_get_dentry_cached(es, ES_IDX_FILE);
	memset(ep, 0, sizeof(*ep));
	exfat_set_entry_type(ep, type);
	exfat_set_entry_time(sbi, ts,
			&ep->dentry.file.create_tz,
			&ep->dentry.file.create_time,
			&ep->dentry.file.create_date,
			&ep->dentry.file.create_time_cs);
	exfat_set_entry_time(sbi, ts,
			&ep->dentry.file.modify_tz,
			&ep->dentry.file.modify_time,
			&ep->dentry.file.modify_date,
			&ep->dentry.file.modify_time_cs);
	exfat_set_entry_time(sbi, ts,
			&ep->dentry.file.access_tz,
			&ep->dentry.file.access_time,
			&ep->dentry.file.access_date,
			NULL);

	ep = exfat_get_dentry_cached(es, ES_IDX_STREAM);
	exfat_init_stream_entry(ep, start_clu, size);
}

static void exfat_free_benign_secondary_clusters(struct inode *inode,
		struct exfat_dentry *ep)
{
	struct super_block *sb = inode->i_sb;
	struct exfat_chain dir;
	unsigned int start_clu =
		le32_to_cpu(ep->dentry.generic_secondary.start_clu);
	u64 size = le64_to_cpu(ep->dentry.generic_secondary.size);
	unsigned char flags = ep->dentry.generic_secondary.flags;

	if (!(flags & ALLOC_POSSIBLE) || !start_clu || !size)
		return;

	exfat_chain_set(&dir, start_clu,
			EXFAT_B_TO_CLU_ROUND_UP(size, EXFAT_SB(sb)),
			flags);
	exfat_free_cluster(inode, &dir);
}

void exfat_init_ext_entry(struct exfat_entry_set_cache *es, int num_entries,
		struct exfat_uni_name *p_uniname)
{
	int i;
	unsigned short *uniname = p_uniname->name;
	struct exfat_dentry *ep;

	ep = exfat_get_dentry_cached(es, ES_IDX_FILE);
	ep->dentry.file.num_ext = (unsigned char)(num_entries - 1);

	ep = exfat_get_dentry_cached(es, ES_IDX_STREAM);
	ep->dentry.stream.name_len = p_uniname->name_len;
	ep->dentry.stream.name_hash = cpu_to_le16(p_uniname->name_hash);

	for (i = ES_IDX_FIRST_FILENAME; i < num_entries; i++) {
		ep = exfat_get_dentry_cached(es, i);
		exfat_init_name_entry(ep, uniname);
		uniname += EXFAT_FILE_NAME_LEN;
	}

	exfat_update_dir_chksum(es);
}

void exfat_remove_entries(struct inode *inode, struct exfat_entry_set_cache *es,
		int order)
{
	int i;
	struct exfat_dentry *ep;

	for (i = order; i < es->num_entries; i++) {
		ep = exfat_get_dentry_cached(es, i);

		if (exfat_get_entry_type(ep) & TYPE_BENIGN_SEC)
			exfat_free_benign_secondary_clusters(inode, ep);

		exfat_set_entry_type(ep, TYPE_DELETED);
	}

	if (order < es->num_entries)
		es->modified = true;
}

void exfat_update_dir_chksum(struct exfat_entry_set_cache *es)
{
	int chksum_type = CS_DIR_ENTRY, i;
	unsigned short chksum = 0;
	struct exfat_dentry *ep;

	for (i = ES_IDX_FILE; i < es->num_entries; i++) {
		ep = exfat_get_dentry_cached(es, i);
		chksum = exfat_calc_chksum16(ep, DENTRY_SIZE, chksum,
					     chksum_type);
		chksum_type = CS_DEFAULT;
	}
	ep = exfat_get_dentry_cached(es, ES_IDX_FILE);
	ep->dentry.file.checksum = cpu_to_le16(chksum);
	es->modified = true;
}

int exfat_put_dentry_set(struct exfat_entry_set_cache *es, int sync)
{
	int i, err = 0;

	if (es->modified)
		err = exfat_update_bhs(es->bh, es->num_bh, sync);

	for (i = 0; i < es->num_bh; i++)
		if (err)
			bforget(es->bh[i]);
		else
			brelse(es->bh[i]);

	if (IS_DYNAMIC_ES(es))
		kfree(es->bh);

	return err;
}

static int exfat_walk_fat_chain(struct super_block *sb,
		struct exfat_chain *p_dir, unsigned int byte_offset,
		unsigned int *clu)
{
	struct exfat_sb_info *sbi = EXFAT_SB(sb);
	unsigned int clu_offset;
	unsigned int cur_clu;

	clu_offset = EXFAT_B_TO_CLU(byte_offset, sbi);
	cur_clu = p_dir->dir;

	if (p_dir->flags == ALLOC_NO_FAT_CHAIN) {
		cur_clu += clu_offset;
	} else {
		while (clu_offset > 0) {
			if (exfat_get_next_cluster(sb, &cur_clu))
				return -EIO;
			if (cur_clu == EXFAT_EOF_CLUSTER) {
				exfat_fs_error(sb,
					"invalid dentry access beyond EOF (clu : %u, eidx : %d)",
					p_dir->dir,
					EXFAT_B_TO_DEN(byte_offset));
				return -EIO;
			}
			clu_offset--;
		}
	}

	*clu = cur_clu;
	return 0;
}

static int exfat_find_location(struct super_block *sb, struct exfat_chain *p_dir,
			       int entry, sector_t *sector, int *offset)
{
	int ret;
	unsigned int off, clu = 0;
	struct exfat_sb_info *sbi = EXFAT_SB(sb);

	off = EXFAT_DEN_TO_B(entry);

	ret = exfat_walk_fat_chain(sb, p_dir, off, &clu);
	if (ret)
		return ret;

	/* byte offset in cluster */
	off = EXFAT_CLU_OFFSET(off, sbi);

	/* byte offset in sector    */
	*offset = EXFAT_BLK_OFFSET(off, sb);

	/* sector offset in cluster */
	*sector = EXFAT_B_TO_BLK(off, sb);
	*sector += exfat_cluster_to_sector(sbi, clu);
	return 0;
}

#define EXFAT_MAX_RA_SIZE     (128*1024)
static int exfat_dir_readahead(struct super_block *sb, sector_t sec)
{
	struct exfat_sb_info *sbi = EXFAT_SB(sb);
	struct buffer_head *bh;
	unsigned int max_ra_count = EXFAT_MAX_RA_SIZE >> sb->s_blocksize_bits;
	unsigned int page_ra_count = PAGE_SIZE >> sb->s_blocksize_bits;
	unsigned int adj_ra_count = max(sbi->sect_per_clus, page_ra_count);
	unsigned int ra_count = min(adj_ra_count, max_ra_count);

	/* Read-ahead is not required */
	if (sbi->sect_per_clus == 1)
		return 0;

	if (sec < sbi->data_start_sector) {
		exfat_err(sb, "requested sector is invalid(sect:%llu, root:%llu)",
			  (unsigned long long)sec, sbi->data_start_sector);
		return -EIO;
	}

	/* Not sector aligned with ra_count, resize ra_count to page size */
	if ((sec - sbi->data_start_sector) & (ra_count - 1))
		ra_count = page_ra_count;

	bh = sb_find_get_block(sb, sec);
	if (!bh || !buffer_uptodate(bh)) {
		unsigned int i;

		for (i = 0; i < ra_count; i++)
			sb_breadahead(sb, (sector_t)(sec + i));
	}
	brelse(bh);
	return 0;
}

struct exfat_dentry *exfat_get_dentry(struct super_block *sb,
		struct exfat_chain *p_dir, int entry, struct buffer_head **bh)
{
	unsigned int dentries_per_page = EXFAT_B_TO_DEN(PAGE_SIZE);
	int off;
	sector_t sec;

	if (p_dir->dir == DIR_DELETED) {
		exfat_err(sb, "abnormal access to deleted dentry");
		return NULL;
	}

	if (exfat_find_location(sb, p_dir, entry, &sec, &off))
		return NULL;

	if (p_dir->dir != EXFAT_FREE_CLUSTER &&
			!(entry & (dentries_per_page - 1)))
		exfat_dir_readahead(sb, sec);

	*bh = sb_bread(sb, sec);
	if (!*bh)
		return NULL;

	return (struct exfat_dentry *)((*bh)->b_data + off);
}

enum exfat_validate_dentry_mode {
	ES_MODE_GET_FILE_ENTRY,
	ES_MODE_GET_STRM_ENTRY,
	ES_MODE_GET_NAME_ENTRY,
	ES_MODE_GET_CRITICAL_SEC_ENTRY,
	ES_MODE_GET_BENIGN_SEC_ENTRY,
};

static bool exfat_validate_entry(unsigned int type,
		enum exfat_validate_dentry_mode *mode)
{
	if (type == TYPE_UNUSED || type == TYPE_DELETED)
		return false;

	switch (*mode) {
	case ES_MODE_GET_FILE_ENTRY:
		if (type != TYPE_STREAM)
			return false;
		*mode = ES_MODE_GET_STRM_ENTRY;
		break;
	case ES_MODE_GET_STRM_ENTRY:
		if (type != TYPE_EXTEND)
			return false;
		*mode = ES_MODE_GET_NAME_ENTRY;
		break;
	case ES_MODE_GET_NAME_ENTRY:
		if (type & TYPE_BENIGN_SEC)
			*mode = ES_MODE_GET_BENIGN_SEC_ENTRY;
		else if (type != TYPE_EXTEND)
			return false;
		break;
	case ES_MODE_GET_BENIGN_SEC_ENTRY:
		/* Assume unreconized benign secondary entry */
		if (!(type & TYPE_BENIGN_SEC))
			return false;
		break;
	default:
		return false;
	}

	return true;
}

struct exfat_dentry *exfat_get_dentry_cached(
	struct exfat_entry_set_cache *es, int num)
{
	int off = es->start_off + num * DENTRY_SIZE;
	struct buffer_head *bh = es->bh[EXFAT_B_TO_BLK(off, es->sb)];
	char *p = bh->b_data + EXFAT_BLK_OFFSET(off, es->sb);

	return (struct exfat_dentry *)p;
}

/*
 * Returns a set of dentries.
 *
 * Note It provides a direct pointer to bh->data via exfat_get_dentry_cached().
 * User should call exfat_get_dentry_set() after setting 'modified' to apply
 * changes made in this entry set to the real device.
 *
 * in:
 *   sb+p_dir+entry: indicates a file/dir
 *   num_entries: specifies how many dentries should be included.
 *                It will be set to es->num_entries if it is not 0.
 *                If num_entries is 0, es->num_entries will be obtained
 *                from the first dentry.
 * out:
 *   es: pointer of entry set on success.
 * return:
 *   0 on success
 *   -error code on failure
 */
static int __exfat_get_dentry_set(struct exfat_entry_set_cache *es,
		struct super_block *sb, struct exfat_chain *p_dir, int entry,
		unsigned int num_entries)
{
	int ret, i, num_bh;
	unsigned int off;
	sector_t sec;
	struct exfat_sb_info *sbi = EXFAT_SB(sb);
	struct buffer_head *bh;

	if (p_dir->dir == DIR_DELETED) {
		exfat_err(sb, "access to deleted dentry");
		return -EIO;
	}

	ret = exfat_find_location(sb, p_dir, entry, &sec, &off);
	if (ret)
		return ret;

	memset(es, 0, sizeof(*es));
	es->sb = sb;
	es->modified = false;
	es->start_off = off;
	es->bh = es->__bh;

	bh = sb_bread(sb, sec);
	if (!bh)
		return -EIO;
	es->bh[es->num_bh++] = bh;

	if (num_entries == ES_ALL_ENTRIES) {
		struct exfat_dentry *ep;

		ep = exfat_get_dentry_cached(es, ES_IDX_FILE);
		if (ep->type != EXFAT_FILE) {
			brelse(bh);
			return -EIO;
		}

		num_entries = ep->dentry.file.num_ext + 1;
	}

	es->num_entries = num_entries;

	num_bh = EXFAT_B_TO_BLK_ROUND_UP(off + num_entries * DENTRY_SIZE, sb);
	if (num_bh > ARRAY_SIZE(es->__bh)) {
		es->bh = kmalloc_array(num_bh, sizeof(*es->bh), GFP_NOFS);
		if (!es->bh) {
			brelse(bh);
			return -ENOMEM;
		}
		es->bh[0] = bh;
	}

	for (i = 1; i < num_bh; i++) {
		/* get the next sector */
		if (exfat_is_last_sector_in_cluster(sbi, sec)) {
			unsigned int clu = exfat_sector_to_cluster(sbi, sec);

			if (p_dir->flags == ALLOC_NO_FAT_CHAIN)
				clu++;
			else if (exfat_get_next_cluster(sb, &clu))
				goto put_es;
			sec = exfat_cluster_to_sector(sbi, clu);
		} else {
			sec++;
		}

		bh = sb_bread(sb, sec);
		if (!bh)
			goto put_es;
		es->bh[es->num_bh++] = bh;
	}

	return 0;

put_es:
	exfat_put_dentry_set(es, false);
	return -EIO;
}

int exfat_get_dentry_set(struct exfat_entry_set_cache *es,
		struct super_block *sb, struct exfat_chain *p_dir,
		int entry, unsigned int num_entries)
{
	int ret, i;
	struct exfat_dentry *ep;
	enum exfat_validate_dentry_mode mode = ES_MODE_GET_FILE_ENTRY;

	ret = __exfat_get_dentry_set(es, sb, p_dir, entry, num_entries);
	if (ret < 0)
		return ret;

	/* validate cached dentries */
	for (i = ES_IDX_STREAM; i < es->num_entries; i++) {
		ep = exfat_get_dentry_cached(es, i);
		if (!exfat_validate_entry(exfat_get_entry_type(ep), &mode))
			goto put_es;
	}
	return 0;

put_es:
	exfat_put_dentry_set(es, false);
	return -EIO;
}

static int exfat_validate_empty_dentry_set(struct exfat_entry_set_cache *es)
{
	struct exfat_dentry *ep;
	struct buffer_head *bh;
	int i, off;
	bool unused_hit = false;

	/*
	 * ONLY UNUSED OR DELETED DENTRIES ARE ALLOWED:
	 * Although it violates the specification for a deleted entry to
	 * follow an unused entry, some exFAT implementations could work
	 * like this. Therefore, to improve compatibility, let's allow it.
	 */
	for (i = 0; i < es->num_entries; i++) {
		ep = exfat_get_dentry_cached(es, i);
		if (ep->type == EXFAT_UNUSED) {
			unused_hit = true;
		} else if (!IS_EXFAT_DELETED(ep->type)) {
			if (unused_hit)
				goto err_used_follow_unused;
			i++;
			goto count_skip_entries;
		}
	}

	return 0;

err_used_follow_unused:
	off = es->start_off + (i << DENTRY_SIZE_BITS);
	bh = es->bh[EXFAT_B_TO_BLK(off, es->sb)];

	exfat_fs_error(es->sb,
		"in sector %lld, dentry %d should be unused, but 0x%x",
		bh->b_blocknr, off >> DENTRY_SIZE_BITS, ep->type);

	return -EIO;

count_skip_entries:
	es->num_entries = EXFAT_B_TO_DEN(EXFAT_BLK_TO_B(es->num_bh, es->sb) - es->start_off);
	for (; i < es->num_entries; i++) {
		ep = exfat_get_dentry_cached(es, i);
		if (IS_EXFAT_DELETED(ep->type))
			break;
	}

	return i;
}

/*
 * Get an empty dentry set.
 *
 * in:
 *   sb+p_dir+entry: indicates the empty dentry location
 *   num_entries: specifies how many empty dentries should be included.
 * out:
 *   es: pointer of empty dentry set on success.
 * return:
 *   0  : on success
 *   >0 : the dentries are not empty, the return value is the number of
 *        dentries to be skipped for the next lookup.
 *   <0 : on failure
 */
int exfat_get_empty_dentry_set(struct exfat_entry_set_cache *es,
		struct super_block *sb, struct exfat_chain *p_dir,
		int entry, unsigned int num_entries)
{
	int ret;

	ret = __exfat_get_dentry_set(es, sb, p_dir, entry, num_entries);
	if (ret < 0)
		return ret;

	ret = exfat_validate_empty_dentry_set(es);
	if (ret)
		exfat_put_dentry_set(es, false);

	return ret;
}

static inline void exfat_reset_empty_hint(struct exfat_hint_femp *hint_femp)
{
	hint_femp->eidx = EXFAT_HINT_NONE;
	hint_femp->count = 0;
}

static inline void exfat_set_empty_hint(struct exfat_inode_info *ei,
		struct exfat_hint_femp *candi_empty, struct exfat_chain *clu,
		int dentry, int num_entries, int entry_type)
{
	if (ei->hint_femp.eidx == EXFAT_HINT_NONE ||
	    ei->hint_femp.eidx > dentry) {
		int total_entries = EXFAT_B_TO_DEN(i_size_read(&ei->vfs_inode));

		if (candi_empty->count == 0) {
			candi_empty->cur = *clu;
			candi_empty->eidx = dentry;
		}

		if (entry_type == TYPE_UNUSED)
			candi_empty->count += total_entries - dentry;
		else
			candi_empty->count++;

		if (candi_empty->count == num_entries ||
		    candi_empty->count + candi_empty->eidx == total_entries)
			ei->hint_femp = *candi_empty;
	}
}

enum {
	DIRENT_STEP_FILE,
	DIRENT_STEP_STRM,
	DIRENT_STEP_NAME,
	DIRENT_STEP_SECD,
};

/*
 * @ei:         inode info of parent directory
 * @p_dir:      directory structure of parent directory
 * @num_entries:entry size of p_uniname
 * @hint_opt:   If p_uniname is found, filled with optimized dir/entry
 *              for traversing cluster chain.
 * @return:
 *   >= 0:      file directory entry position where the name exists
 *   -ENOENT:   entry with the name does not exist
 *   -EIO:      I/O error
 */
int exfat_find_dir_entry(struct super_block *sb, struct exfat_inode_info *ei,
		struct exfat_chain *p_dir, struct exfat_uni_name *p_uniname,
		struct exfat_hint *hint_opt)
{
	int i, rewind = 0, dentry = 0, end_eidx = 0, num_ext = 0, len;
	int order, step, name_len = 0;
	int dentries_per_clu;
	unsigned int entry_type;
	unsigned short *uniname = NULL;
	struct exfat_chain clu;
	struct exfat_hint *hint_stat = &ei->hint_stat;
	struct exfat_hint_femp candi_empty;
	struct exfat_sb_info *sbi = EXFAT_SB(sb);
	int num_entries = exfat_calc_num_entries(p_uniname);

	if (num_entries < 0)
		return num_entries;

	dentries_per_clu = sbi->dentries_per_clu;

	exfat_chain_dup(&clu, p_dir);

	if (hint_stat->eidx) {
		clu.dir = hint_stat->clu;
		dentry = hint_stat->eidx;
		end_eidx = dentry;
	}

	exfat_reset_empty_hint(&ei->hint_femp);

rewind:
	order = 0;
	step = DIRENT_STEP_FILE;
	exfat_reset_empty_hint(&candi_empty);

	while (clu.dir != EXFAT_EOF_CLUSTER) {
		i = dentry & (dentries_per_clu - 1);
		for (; i < dentries_per_clu; i++, dentry++) {
			struct exfat_dentry *ep;
			struct buffer_head *bh;

			if (rewind && dentry == end_eidx)
				goto not_found;

			ep = exfat_get_dentry(sb, &clu, i, &bh);
			if (!ep)
				return -EIO;

			entry_type = exfat_get_entry_type(ep);

			if (entry_type == TYPE_UNUSED ||
			    entry_type == TYPE_DELETED) {
				step = DIRENT_STEP_FILE;

				exfat_set_empty_hint(ei, &candi_empty, &clu,
						dentry, num_entries,
						entry_type);

				brelse(bh);
				if (entry_type == TYPE_UNUSED)
					goto not_found;
				continue;
			}

			exfat_reset_empty_hint(&candi_empty);

			if (entry_type == TYPE_FILE || entry_type == TYPE_DIR) {
				step = DIRENT_STEP_FILE;
				hint_opt->clu = clu.dir;
				hint_opt->eidx = i;
				num_ext = ep->dentry.file.num_ext;
				step = DIRENT_STEP_STRM;
				brelse(bh);
				continue;
			}

			if (entry_type == TYPE_STREAM) {
				u16 name_hash;

				if (step != DIRENT_STEP_STRM) {
					step = DIRENT_STEP_FILE;
					brelse(bh);
					continue;
				}
				step = DIRENT_STEP_FILE;
				name_hash = le16_to_cpu(
						ep->dentry.stream.name_hash);
				if (p_uniname->name_hash == name_hash &&
				    p_uniname->name_len ==
						ep->dentry.stream.name_len) {
					step = DIRENT_STEP_NAME;
					order = 1;
					name_len = 0;
				}
				brelse(bh);
				continue;
			}

			brelse(bh);
			if (entry_type == TYPE_EXTEND) {
				unsigned short entry_uniname[16], unichar;

				if (step != DIRENT_STEP_NAME ||
				    name_len >= MAX_NAME_LENGTH) {
					step = DIRENT_STEP_FILE;
					continue;
				}

				if (++order == 2)
					uniname = p_uniname->name;
				else
					uniname += EXFAT_FILE_NAME_LEN;

				len = exfat_extract_uni_name(ep, entry_uniname);
				name_len += len;

				unichar = *(uniname+len);
				*(uniname+len) = 0x0;

				if (exfat_uniname_ncmp(sb, uniname,
					entry_uniname, len)) {
					step = DIRENT_STEP_FILE;
				} else if (p_uniname->name_len == name_len) {
					if (order == num_ext)
						goto found;
					step = DIRENT_STEP_SECD;
				}

				*(uniname+len) = unichar;
				continue;
			}

			if (entry_type &
					(TYPE_CRITICAL_SEC | TYPE_BENIGN_SEC)) {
				if (step == DIRENT_STEP_SECD) {
					if (++order == num_ext)
						goto found;
					continue;
				}
			}
			step = DIRENT_STEP_FILE;
		}

		if (clu.flags == ALLOC_NO_FAT_CHAIN) {
			if (--clu.size > 0)
				clu.dir++;
			else
				clu.dir = EXFAT_EOF_CLUSTER;
		} else {
			if (exfat_get_next_cluster(sb, &clu.dir))
				return -EIO;
		}
	}

not_found:
	/*
	 * We started at not 0 index,so we should try to find target
	 * from 0 index to the index we started at.
	 */
	if (!rewind && end_eidx) {
		rewind = 1;
		dentry = 0;
		clu.dir = p_dir->dir;
		goto rewind;
	}

	/*
	 * set the EXFAT_EOF_CLUSTER flag to avoid search
	 * from the beginning again when allocated a new cluster
	 */
	if (ei->hint_femp.eidx == EXFAT_HINT_NONE) {
		ei->hint_femp.cur.dir = EXFAT_EOF_CLUSTER;
		ei->hint_femp.eidx = p_dir->size * dentries_per_clu;
		ei->hint_femp.count = 0;
	}

	/* initialized hint_stat */
	hint_stat->clu = p_dir->dir;
	hint_stat->eidx = 0;
	return -ENOENT;

found:
	/* next dentry we'll find is out of this cluster */
	if (!((dentry + 1) & (dentries_per_clu - 1))) {
		int ret = 0;

		if (clu.flags == ALLOC_NO_FAT_CHAIN) {
			if (--clu.size > 0)
				clu.dir++;
			else
				clu.dir = EXFAT_EOF_CLUSTER;
		} else {
			ret = exfat_get_next_cluster(sb, &clu.dir);
		}

		if (ret || clu.dir == EXFAT_EOF_CLUSTER) {
			/* just initialized hint_stat */
			hint_stat->clu = p_dir->dir;
			hint_stat->eidx = 0;
			return (dentry - num_ext);
		}
	}

	hint_stat->clu = clu.dir;
	hint_stat->eidx = dentry + 1;
	return dentry - num_ext;
}

int exfat_count_dir_entries(struct super_block *sb, struct exfat_chain *p_dir)
{
	int i, count = 0;
	int dentries_per_clu;
	unsigned int entry_type;
	struct exfat_chain clu;
	struct exfat_dentry *ep;
	struct exfat_sb_info *sbi = EXFAT_SB(sb);
	struct buffer_head *bh;

	dentries_per_clu = sbi->dentries_per_clu;

	exfat_chain_dup(&clu, p_dir);

	while (clu.dir != EXFAT_EOF_CLUSTER) {
		for (i = 0; i < dentries_per_clu; i++) {
			ep = exfat_get_dentry(sb, &clu, i, &bh);
			if (!ep)
				return -EIO;
			entry_type = exfat_get_entry_type(ep);
			brelse(bh);

			if (entry_type == TYPE_UNUSED)
				return count;
			if (entry_type != TYPE_DIR)
				continue;
			count++;
		}

		if (clu.flags == ALLOC_NO_FAT_CHAIN) {
			if (--clu.size > 0)
				clu.dir++;
			else
				clu.dir = EXFAT_EOF_CLUSTER;
		} else {
			if (exfat_get_next_cluster(sb, &(clu.dir)))
				return -EIO;
		}
	}

	return count;
}
