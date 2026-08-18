// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2012-2013 Samsung Electronics Co., Ltd.
 */

#include <linux/init.h>
#include <linux/buffer_head.h>
#include <linux/mpage.h>
#include <linux/bio.h>
#include <linux/blkdev.h>
#include <linux/time.h>
#include <linux/writeback.h>
#include <linux/uio.h>
#include <linux/random.h>
#include <linux/iversion.h>
#include <linux/iomap.h>

#include "exfat_raw.h"
#include "exfat_fs.h"
#include "iomap.h"

int __exfat_write_inode(struct inode *inode, int sync)
{
	unsigned long long on_disk_size;
	unsigned long long on_disk_valid_size;
	struct exfat_dentry *ep, *ep2;
	struct exfat_entry_set_cache es;
	struct super_block *sb = inode->i_sb;
	struct exfat_sb_info *sbi = EXFAT_SB(sb);
	struct exfat_inode_info *ei = EXFAT_I(inode);
	bool is_dir = (ei->type == TYPE_DIR);
	struct timespec64 ts;

	if (inode->i_ino == EXFAT_ROOT_INO)
		return 0;

	/*
	 * If the inode is already unlinked, there is no need for updating it.
	 */
	if (ei->dir.dir == DIR_DELETED)
		return 0;

	if (is_dir && ei->dir.dir == sbi->root_dir && ei->entry == -1)
		return 0;

	exfat_set_volume_dirty(sb);

	/* get the directory entry of given file or directory */
	if (exfat_get_dentry_set_by_ei(&es, sb, ei))
		return -EIO;
	ep = exfat_get_dentry_cached(&es, ES_IDX_FILE);
	ep2 = exfat_get_dentry_cached(&es, ES_IDX_STREAM);

	ep->dentry.file.attr = cpu_to_le16(exfat_make_attr(inode));

	/* set FILE_INFO structure using the acquired struct exfat_dentry */
	exfat_set_entry_time(sbi, &ei->i_crtime,
			&ep->dentry.file.create_tz,
			&ep->dentry.file.create_time,
			&ep->dentry.file.create_date,
			&ep->dentry.file.create_time_cs);
	ts = inode_get_mtime(inode);
	exfat_set_entry_time(sbi, &ts,
			     &ep->dentry.file.modify_tz,
			     &ep->dentry.file.modify_time,
			     &ep->dentry.file.modify_date,
			     &ep->dentry.file.modify_time_cs);
	ts = inode_get_atime(inode);
	exfat_set_entry_time(sbi, &ts,
			     &ep->dentry.file.access_tz,
			     &ep->dentry.file.access_time,
			     &ep->dentry.file.access_date,
			     NULL);

	/*
	 * During a DIO write, valid_size is updated eagerly in iomap_end (so
	 * that concurrent buffered reads see IOMAP_MAPPED) while i_size is
	 * updated asynchronously in end_io.  The FAT chain was already
	 * extended to cover ceil(valid_size/cluster_size) clusters.  Use the
	 * maximum so the on-disk size field always covers the FAT chain,
	 * preventing fsck from reporting "more clusters are allocated".
	 */
	on_disk_size = max_t(unsigned long long, i_size_read(inode),
			ei->valid_size);

	if (ei->start_clu == EXFAT_EOF_CLUSTER)
		on_disk_size = 0;
	/*
	 * valid_size on disk must reflect only confirmed data (up to i_size)
	 * and must not exceed on_disk_size.
	 */
	on_disk_valid_size = min_t(unsigned long long, ei->valid_size,
			i_size_read(inode));
	if (ei->start_clu == EXFAT_EOF_CLUSTER)
		on_disk_valid_size = 0;

	ep2->dentry.stream.size = cpu_to_le64(on_disk_size);
	ep2->dentry.stream.valid_size = cpu_to_le64(on_disk_valid_size);

	if (on_disk_size) {
		ep2->dentry.stream.flags = ei->flags;
		ep2->dentry.stream.start_clu = cpu_to_le32(ei->start_clu);
	} else {
		ep2->dentry.stream.flags = ALLOC_FAT_CHAIN;
		ep2->dentry.stream.start_clu = EXFAT_FREE_CLUSTER;
	}

	exfat_update_dir_chksum(&es);
	return exfat_put_dentry_set(&es, sync);
}

int exfat_write_inode(struct inode *inode, struct writeback_control *wbc)
{
	int ret;

	if (unlikely(exfat_forced_shutdown(inode->i_sb)))
		return -EIO;

	mutex_lock(&EXFAT_SB(inode->i_sb)->s_lock);
	ret = __exfat_write_inode(inode, wbc->sync_mode == WB_SYNC_ALL);
	mutex_unlock(&EXFAT_SB(inode->i_sb)->s_lock);

	return ret;
}

void exfat_sync_inode(struct inode *inode)
{
	lockdep_assert_held(&EXFAT_SB(inode->i_sb)->s_lock);
	__exfat_write_inode(inode, 1);
}

/*
 * Input: inode, (logical) clu_offset, target allocation area
 * Output: errcode, cluster number
 * *clu = (~0), if it's unable to allocate a new cluster
 */
int exfat_map_cluster(struct inode *inode, unsigned int clu_offset,
		unsigned int *clu, unsigned int *count, int create,
		bool *balloc)
{
	int ret;
	unsigned int last_clu;
	struct exfat_chain new_clu;
	struct super_block *sb = inode->i_sb;
	struct exfat_sb_info *sbi = EXFAT_SB(sb);
	struct exfat_inode_info *ei = EXFAT_I(inode);
	unsigned int local_clu_offset = clu_offset;
	unsigned int num_to_be_allocated = 0, num_clusters;

	num_clusters = exfat_bytes_to_cluster(sbi, exfat_ondisk_size(inode));
	if (clu_offset > num_clusters ||
	    *count > num_clusters - clu_offset)
		num_to_be_allocated = clu_offset + *count - num_clusters;

	if (!create && (num_to_be_allocated > 0)) {
		*clu = EXFAT_EOF_CLUSTER;
		return 0;
	}

	*clu = last_clu = ei->start_clu;

	if (*clu == EXFAT_EOF_CLUSTER) {
		*count = 0;
	} else if (ei->flags == ALLOC_NO_FAT_CHAIN) {
		last_clu += num_clusters - 1;
		if (clu_offset < num_clusters) {
			*clu += clu_offset;
			*count = min(num_clusters - clu_offset, *count);
		} else {
			*clu = EXFAT_EOF_CLUSTER;
			*count = 0;
		}
	} else {
		int err = exfat_get_cluster(inode, clu_offset,
				clu, count, &last_clu);
		if (err)
			return -EIO;
	}

	if (*clu == EXFAT_EOF_CLUSTER) {
		exfat_set_volume_dirty(sb);

		new_clu.dir = (last_clu == EXFAT_EOF_CLUSTER) ?
				EXFAT_EOF_CLUSTER : last_clu + 1;
		new_clu.size = 0;
		new_clu.flags = ei->flags;

		/* allocate a cluster */
		if (num_to_be_allocated < 1) {
			/* Broken FAT (i_sze > allocated FAT) */
			exfat_fs_error(sb, "broken FAT chain.");
			return -EIO;
		}

		ret = exfat_alloc_cluster(inode, num_to_be_allocated, &new_clu,
				inode_needs_sync(inode), true);
		if (ret)
			return ret;

		if (new_clu.dir == EXFAT_EOF_CLUSTER ||
		    new_clu.dir == EXFAT_FREE_CLUSTER) {
			exfat_fs_error(sb,
				"bogus cluster new allocated (last_clu : %u, new_clu : %u)",
				last_clu, new_clu.dir);
			return -EIO;
		}

		/* append to the FAT chain */
		if (last_clu == EXFAT_EOF_CLUSTER) {
			if (new_clu.flags == ALLOC_FAT_CHAIN)
				ei->flags = ALLOC_FAT_CHAIN;
			ei->start_clu = new_clu.dir;
		} else {
			if (new_clu.flags != ei->flags) {
				/* no-fat-chain bit is disabled,
				 * so fat-chain should be synced with
				 * alloc-bitmap
				 */
				if (exfat_chain_cont_cluster(sb, ei->start_clu,
						num_clusters))
					return -EIO;
				ei->flags = ALLOC_FAT_CHAIN;
			}
			if (new_clu.flags == ALLOC_FAT_CHAIN)
				if (exfat_ent_set(sb, last_clu, new_clu.dir))
					return -EIO;
		}

		*clu = new_clu.dir;
		*count = new_clu.size;

		inode->i_blocks += exfat_cluster_to_sectors(sbi, new_clu.size);
		if (balloc)
			*balloc = true;
	}

	/* hint information */
	ei->hint_bmap.off = local_clu_offset;
	ei->hint_bmap.clu = *clu;

	return 0;
}

static int exfat_read_folio(struct file *file, struct folio *folio)
{
	struct iomap_read_folio_ctx ctx = {
		.cur_folio = folio,
		.ops = &exfat_iomap_bio_read_ops,
	};

	iomap_read_folio(&exfat_iomap_ops, &ctx, NULL);
	return 0;
}

static void exfat_readahead(struct readahead_control *rac)
{
	struct address_space *mapping = rac->mapping;
	struct inode *inode = mapping->host;
	struct exfat_inode_info *ei = EXFAT_I(inode);
	loff_t pos = readahead_pos(rac);
	struct iomap_read_folio_ctx ctx = {
		.ops = &exfat_iomap_bio_read_ops,
		.rac = rac,
	};

	/* Range cross valid_size, read it page by page. */
	if (ei->valid_size < i_size_read(inode) &&
	    pos <= ei->valid_size &&
	    ei->valid_size < pos + readahead_length(rac))
		return;

	iomap_readahead(&exfat_iomap_ops, &ctx, NULL);
}

static int exfat_writepages(struct address_space *mapping,
		struct writeback_control *wbc)
{
	struct iomap_writepage_ctx wpc = {
		.inode		= mapping->host,
		.wbc		= wbc,
		.ops		= &exfat_writeback_ops,
	};

	if (unlikely(exfat_forced_shutdown(mapping->host->i_sb)))
		return -EIO;

	return iomap_writepages(&wpc);
}

static sector_t exfat_aop_bmap(struct address_space *mapping, sector_t block)
{
	sector_t blocknr;

	/* exfat_get_cluster() assumes the requested blocknr isn't truncated. */
	down_read(&EXFAT_I(mapping->host)->truncate_lock);
	blocknr = iomap_bmap(mapping, block, &exfat_iomap_ops);
	up_read(&EXFAT_I(mapping->host)->truncate_lock);
	return blocknr;
}

static const struct address_space_operations exfat_aops = {
	.read_folio		= exfat_read_folio,
	.readahead		= exfat_readahead,
	.writepages		= exfat_writepages,
	.dirty_folio		= iomap_dirty_folio,
	.bmap			= exfat_aop_bmap,
	.migrate_folio		= filemap_migrate_folio,
	.is_partially_uptodate	= iomap_is_partially_uptodate,
	.error_remove_folio	= generic_error_remove_folio,
	.release_folio		= iomap_release_folio,
	.invalidate_folio	= iomap_invalidate_folio,
	.swap_activate		= exfat_iomap_swap_activate,
};

static inline unsigned long exfat_hash(loff_t i_pos)
{
	return hash_32(i_pos, EXFAT_HASH_BITS);
}

void exfat_hash_inode(struct inode *inode, loff_t i_pos)
{
	struct exfat_sb_info *sbi = EXFAT_SB(inode->i_sb);
	struct hlist_head *head = sbi->inode_hashtable + exfat_hash(i_pos);

	spin_lock(&sbi->inode_hash_lock);
	EXFAT_I(inode)->i_pos = i_pos;
	hlist_add_head(&EXFAT_I(inode)->i_hash_fat, head);
	spin_unlock(&sbi->inode_hash_lock);
}

void exfat_unhash_inode(struct inode *inode)
{
	struct exfat_sb_info *sbi = EXFAT_SB(inode->i_sb);

	spin_lock(&sbi->inode_hash_lock);
	hlist_del_init(&EXFAT_I(inode)->i_hash_fat);
	EXFAT_I(inode)->i_pos = 0;
	spin_unlock(&sbi->inode_hash_lock);
}

struct inode *exfat_iget(struct super_block *sb, loff_t i_pos)
{
	struct exfat_sb_info *sbi = EXFAT_SB(sb);
	struct exfat_inode_info *info;
	struct hlist_head *head = sbi->inode_hashtable + exfat_hash(i_pos);
	struct inode *inode = NULL;

	spin_lock(&sbi->inode_hash_lock);
	hlist_for_each_entry(info, head, i_hash_fat) {
		WARN_ON(info->vfs_inode.i_sb != sb);

		if (i_pos != info->i_pos)
			continue;
		inode = igrab(&info->vfs_inode);
		if (inode)
			break;
	}
	spin_unlock(&sbi->inode_hash_lock);
	return inode;
}

/* doesn't deal with root inode */
static int exfat_fill_inode(struct inode *inode, struct exfat_dir_entry *info)
{
	struct exfat_sb_info *sbi = EXFAT_SB(inode->i_sb);
	struct exfat_inode_info *ei = EXFAT_I(inode);
	loff_t size = info->size;

	ei->dir = info->dir;
	ei->entry = info->entry;
	ei->attr = info->attr;
	ei->start_clu = info->start_clu;
	ei->flags = info->flags;
	ei->type = info->type;
	ei->valid_size = info->valid_size;
	ei->zeroed_size = info->valid_size;

	ei->version = 0;
	ei->hint_stat.eidx = 0;
	ei->hint_stat.clu = info->start_clu;
	ei->hint_femp.eidx = EXFAT_HINT_NONE;
	ei->hint_bmap.off = EXFAT_EOF_CLUSTER;
	ei->i_pos = 0;

	inode->i_uid = sbi->options.fs_uid;
	inode->i_gid = sbi->options.fs_gid;
	inode_inc_iversion(inode);
	inode->i_generation = get_random_u32();

	if (info->attr & EXFAT_ATTR_SUBDIR) { /* directory */
		inode->i_generation &= ~1;
		inode->i_mode = exfat_make_mode(sbi, info->attr, 0777);
		inode->i_op = &exfat_dir_inode_operations;
		inode->i_fop = &exfat_dir_operations;
		set_nlink(inode, info->num_subdirs);
	} else { /* regular file */
		inode->i_generation |= 1;
		inode->i_mode = exfat_make_mode(sbi, info->attr, 0777);
		inode->i_op = &exfat_file_inode_operations;
		inode->i_fop = &exfat_file_operations;
		inode->i_mapping->a_ops = &exfat_aops;
		inode->i_mapping->nrpages = 0;
	}

	i_size_write(inode, size);

	exfat_save_attr(inode, info->attr);

	inode->i_blocks = round_up(i_size_read(inode), sbi->cluster_size) >> 9;
	inode_set_mtime_to_ts(inode, info->mtime);
	inode_set_ctime_to_ts(inode, info->mtime);
	ei->i_crtime = info->crtime;
	inode_set_atime_to_ts(inode, info->atime);

	return 0;
}

struct inode *exfat_build_inode(struct super_block *sb,
		struct exfat_dir_entry *info, loff_t i_pos)
{
	struct inode *inode;
	int err;

	inode = exfat_iget(sb, i_pos);
	if (inode)
		goto out;
	inode = new_inode(sb);
	if (!inode) {
		inode = ERR_PTR(-ENOMEM);
		goto out;
	}
	inode->i_ino = iunique(sb, EXFAT_ROOT_INO);
	inode_set_iversion(inode, 1);
	err = exfat_fill_inode(inode, info);
	if (err) {
		iput(inode);
		inode = ERR_PTR(err);
		goto out;
	}
	exfat_hash_inode(inode, i_pos);
	insert_inode_hash(inode);
out:
	return inode;
}

void exfat_evict_inode(struct inode *inode)
{
	truncate_inode_pages_final(&inode->i_data);

	if (!inode->i_nlink) {
		i_size_write(inode, 0);
		mutex_lock(&EXFAT_SB(inode->i_sb)->s_lock);
		__exfat_truncate(inode);
		mutex_unlock(&EXFAT_SB(inode->i_sb)->s_lock);
	}

	clear_inode(inode);
	exfat_cache_inval_inode(inode);
	exfat_unhash_inode(inode);
}
