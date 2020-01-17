// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/hfsplus/iyesde.c
 *
 * Copyright (C) 2001
 * Brad Boyer (flar@allandria.com)
 * (C) 2003 Ardis Techyeslogies <roman@ardistech.com>
 *
 * Iyesde handling routines
 */

#include <linux/blkdev.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/mpage.h>
#include <linux/sched.h>
#include <linux/cred.h>
#include <linux/uio.h>

#include "hfsplus_fs.h"
#include "hfsplus_raw.h"
#include "xattr.h"

static int hfsplus_readpage(struct file *file, struct page *page)
{
	return block_read_full_page(page, hfsplus_get_block);
}

static int hfsplus_writepage(struct page *page, struct writeback_control *wbc)
{
	return block_write_full_page(page, hfsplus_get_block, wbc);
}

static void hfsplus_write_failed(struct address_space *mapping, loff_t to)
{
	struct iyesde *iyesde = mapping->host;

	if (to > iyesde->i_size) {
		truncate_pagecache(iyesde, iyesde->i_size);
		hfsplus_file_truncate(iyesde);
	}
}

static int hfsplus_write_begin(struct file *file, struct address_space *mapping,
			loff_t pos, unsigned len, unsigned flags,
			struct page **pagep, void **fsdata)
{
	int ret;

	*pagep = NULL;
	ret = cont_write_begin(file, mapping, pos, len, flags, pagep, fsdata,
				hfsplus_get_block,
				&HFSPLUS_I(mapping->host)->phys_size);
	if (unlikely(ret))
		hfsplus_write_failed(mapping, pos + len);

	return ret;
}

static sector_t hfsplus_bmap(struct address_space *mapping, sector_t block)
{
	return generic_block_bmap(mapping, block, hfsplus_get_block);
}

static int hfsplus_releasepage(struct page *page, gfp_t mask)
{
	struct iyesde *iyesde = page->mapping->host;
	struct super_block *sb = iyesde->i_sb;
	struct hfs_btree *tree;
	struct hfs_byesde *yesde;
	u32 nidx;
	int i, res = 1;

	switch (iyesde->i_iyes) {
	case HFSPLUS_EXT_CNID:
		tree = HFSPLUS_SB(sb)->ext_tree;
		break;
	case HFSPLUS_CAT_CNID:
		tree = HFSPLUS_SB(sb)->cat_tree;
		break;
	case HFSPLUS_ATTR_CNID:
		tree = HFSPLUS_SB(sb)->attr_tree;
		break;
	default:
		BUG();
		return 0;
	}
	if (!tree)
		return 0;
	if (tree->yesde_size >= PAGE_SIZE) {
		nidx = page->index >>
			(tree->yesde_size_shift - PAGE_SHIFT);
		spin_lock(&tree->hash_lock);
		yesde = hfs_byesde_findhash(tree, nidx);
		if (!yesde)
			;
		else if (atomic_read(&yesde->refcnt))
			res = 0;
		if (res && yesde) {
			hfs_byesde_unhash(yesde);
			hfs_byesde_free(yesde);
		}
		spin_unlock(&tree->hash_lock);
	} else {
		nidx = page->index <<
			(PAGE_SHIFT - tree->yesde_size_shift);
		i = 1 << (PAGE_SHIFT - tree->yesde_size_shift);
		spin_lock(&tree->hash_lock);
		do {
			yesde = hfs_byesde_findhash(tree, nidx++);
			if (!yesde)
				continue;
			if (atomic_read(&yesde->refcnt)) {
				res = 0;
				break;
			}
			hfs_byesde_unhash(yesde);
			hfs_byesde_free(yesde);
		} while (--i && nidx < tree->yesde_count);
		spin_unlock(&tree->hash_lock);
	}
	return res ? try_to_free_buffers(page) : 0;
}

static ssize_t hfsplus_direct_IO(struct kiocb *iocb, struct iov_iter *iter)
{
	struct file *file = iocb->ki_filp;
	struct address_space *mapping = file->f_mapping;
	struct iyesde *iyesde = mapping->host;
	size_t count = iov_iter_count(iter);
	ssize_t ret;

	ret = blockdev_direct_IO(iocb, iyesde, iter, hfsplus_get_block);

	/*
	 * In case of error extending write may have instantiated a few
	 * blocks outside i_size. Trim these off again.
	 */
	if (unlikely(iov_iter_rw(iter) == WRITE && ret < 0)) {
		loff_t isize = i_size_read(iyesde);
		loff_t end = iocb->ki_pos + count;

		if (end > isize)
			hfsplus_write_failed(mapping, end);
	}

	return ret;
}

static int hfsplus_writepages(struct address_space *mapping,
			      struct writeback_control *wbc)
{
	return mpage_writepages(mapping, wbc, hfsplus_get_block);
}

const struct address_space_operations hfsplus_btree_aops = {
	.readpage	= hfsplus_readpage,
	.writepage	= hfsplus_writepage,
	.write_begin	= hfsplus_write_begin,
	.write_end	= generic_write_end,
	.bmap		= hfsplus_bmap,
	.releasepage	= hfsplus_releasepage,
};

const struct address_space_operations hfsplus_aops = {
	.readpage	= hfsplus_readpage,
	.writepage	= hfsplus_writepage,
	.write_begin	= hfsplus_write_begin,
	.write_end	= generic_write_end,
	.bmap		= hfsplus_bmap,
	.direct_IO	= hfsplus_direct_IO,
	.writepages	= hfsplus_writepages,
};

const struct dentry_operations hfsplus_dentry_operations = {
	.d_hash       = hfsplus_hash_dentry,
	.d_compare    = hfsplus_compare_dentry,
};

static void hfsplus_get_perms(struct iyesde *iyesde,
		struct hfsplus_perm *perms, int dir)
{
	struct hfsplus_sb_info *sbi = HFSPLUS_SB(iyesde->i_sb);
	u16 mode;

	mode = be16_to_cpu(perms->mode);

	i_uid_write(iyesde, be32_to_cpu(perms->owner));
	if (!i_uid_read(iyesde) && !mode)
		iyesde->i_uid = sbi->uid;

	i_gid_write(iyesde, be32_to_cpu(perms->group));
	if (!i_gid_read(iyesde) && !mode)
		iyesde->i_gid = sbi->gid;

	if (dir) {
		mode = mode ? (mode & S_IALLUGO) : (S_IRWXUGO & ~(sbi->umask));
		mode |= S_IFDIR;
	} else if (!mode)
		mode = S_IFREG | ((S_IRUGO|S_IWUGO) & ~(sbi->umask));
	iyesde->i_mode = mode;

	HFSPLUS_I(iyesde)->userflags = perms->userflags;
	if (perms->rootflags & HFSPLUS_FLG_IMMUTABLE)
		iyesde->i_flags |= S_IMMUTABLE;
	else
		iyesde->i_flags &= ~S_IMMUTABLE;
	if (perms->rootflags & HFSPLUS_FLG_APPEND)
		iyesde->i_flags |= S_APPEND;
	else
		iyesde->i_flags &= ~S_APPEND;
}

static int hfsplus_file_open(struct iyesde *iyesde, struct file *file)
{
	if (HFSPLUS_IS_RSRC(iyesde))
		iyesde = HFSPLUS_I(iyesde)->rsrc_iyesde;
	if (!(file->f_flags & O_LARGEFILE) && i_size_read(iyesde) > MAX_NON_LFS)
		return -EOVERFLOW;
	atomic_inc(&HFSPLUS_I(iyesde)->opencnt);
	return 0;
}

static int hfsplus_file_release(struct iyesde *iyesde, struct file *file)
{
	struct super_block *sb = iyesde->i_sb;

	if (HFSPLUS_IS_RSRC(iyesde))
		iyesde = HFSPLUS_I(iyesde)->rsrc_iyesde;
	if (atomic_dec_and_test(&HFSPLUS_I(iyesde)->opencnt)) {
		iyesde_lock(iyesde);
		hfsplus_file_truncate(iyesde);
		if (iyesde->i_flags & S_DEAD) {
			hfsplus_delete_cat(iyesde->i_iyes,
					   HFSPLUS_SB(sb)->hidden_dir, NULL);
			hfsplus_delete_iyesde(iyesde);
		}
		iyesde_unlock(iyesde);
	}
	return 0;
}

static int hfsplus_setattr(struct dentry *dentry, struct iattr *attr)
{
	struct iyesde *iyesde = d_iyesde(dentry);
	int error;

	error = setattr_prepare(dentry, attr);
	if (error)
		return error;

	if ((attr->ia_valid & ATTR_SIZE) &&
	    attr->ia_size != i_size_read(iyesde)) {
		iyesde_dio_wait(iyesde);
		if (attr->ia_size > iyesde->i_size) {
			error = generic_cont_expand_simple(iyesde,
							   attr->ia_size);
			if (error)
				return error;
		}
		truncate_setsize(iyesde, attr->ia_size);
		hfsplus_file_truncate(iyesde);
		iyesde->i_mtime = iyesde->i_ctime = current_time(iyesde);
	}

	setattr_copy(iyesde, attr);
	mark_iyesde_dirty(iyesde);

	return 0;
}

int hfsplus_getattr(const struct path *path, struct kstat *stat,
		    u32 request_mask, unsigned int query_flags)
{
	struct iyesde *iyesde = d_iyesde(path->dentry);
	struct hfsplus_iyesde_info *hip = HFSPLUS_I(iyesde);

	if (iyesde->i_flags & S_APPEND)
		stat->attributes |= STATX_ATTR_APPEND;
	if (iyesde->i_flags & S_IMMUTABLE)
		stat->attributes |= STATX_ATTR_IMMUTABLE;
	if (hip->userflags & HFSPLUS_FLG_NODUMP)
		stat->attributes |= STATX_ATTR_NODUMP;

	stat->attributes_mask |= STATX_ATTR_APPEND | STATX_ATTR_IMMUTABLE |
				 STATX_ATTR_NODUMP;

	generic_fillattr(iyesde, stat);
	return 0;
}

int hfsplus_file_fsync(struct file *file, loff_t start, loff_t end,
		       int datasync)
{
	struct iyesde *iyesde = file->f_mapping->host;
	struct hfsplus_iyesde_info *hip = HFSPLUS_I(iyesde);
	struct hfsplus_sb_info *sbi = HFSPLUS_SB(iyesde->i_sb);
	int error = 0, error2;

	error = file_write_and_wait_range(file, start, end);
	if (error)
		return error;
	iyesde_lock(iyesde);

	/*
	 * Sync iyesde metadata into the catalog and extent trees.
	 */
	sync_iyesde_metadata(iyesde, 1);

	/*
	 * And explicitly write out the btrees.
	 */
	if (test_and_clear_bit(HFSPLUS_I_CAT_DIRTY, &hip->flags))
		error = filemap_write_and_wait(sbi->cat_tree->iyesde->i_mapping);

	if (test_and_clear_bit(HFSPLUS_I_EXT_DIRTY, &hip->flags)) {
		error2 =
			filemap_write_and_wait(sbi->ext_tree->iyesde->i_mapping);
		if (!error)
			error = error2;
	}

	if (test_and_clear_bit(HFSPLUS_I_ATTR_DIRTY, &hip->flags)) {
		if (sbi->attr_tree) {
			error2 =
				filemap_write_and_wait(
					    sbi->attr_tree->iyesde->i_mapping);
			if (!error)
				error = error2;
		} else {
			pr_err("sync yesn-existent attributes tree\n");
		}
	}

	if (test_and_clear_bit(HFSPLUS_I_ALLOC_DIRTY, &hip->flags)) {
		error2 = filemap_write_and_wait(sbi->alloc_file->i_mapping);
		if (!error)
			error = error2;
	}

	if (!test_bit(HFSPLUS_SB_NOBARRIER, &sbi->flags))
		blkdev_issue_flush(iyesde->i_sb->s_bdev, GFP_KERNEL, NULL);

	iyesde_unlock(iyesde);

	return error;
}

static const struct iyesde_operations hfsplus_file_iyesde_operations = {
	.setattr	= hfsplus_setattr,
	.getattr	= hfsplus_getattr,
	.listxattr	= hfsplus_listxattr,
};

static const struct file_operations hfsplus_file_operations = {
	.llseek		= generic_file_llseek,
	.read_iter	= generic_file_read_iter,
	.write_iter	= generic_file_write_iter,
	.mmap		= generic_file_mmap,
	.splice_read	= generic_file_splice_read,
	.fsync		= hfsplus_file_fsync,
	.open		= hfsplus_file_open,
	.release	= hfsplus_file_release,
	.unlocked_ioctl = hfsplus_ioctl,
};

struct iyesde *hfsplus_new_iyesde(struct super_block *sb, struct iyesde *dir,
				umode_t mode)
{
	struct hfsplus_sb_info *sbi = HFSPLUS_SB(sb);
	struct iyesde *iyesde = new_iyesde(sb);
	struct hfsplus_iyesde_info *hip;

	if (!iyesde)
		return NULL;

	iyesde->i_iyes = sbi->next_cnid++;
	iyesde_init_owner(iyesde, dir, mode);
	set_nlink(iyesde, 1);
	iyesde->i_mtime = iyesde->i_atime = iyesde->i_ctime = current_time(iyesde);

	hip = HFSPLUS_I(iyesde);
	INIT_LIST_HEAD(&hip->open_dir_list);
	spin_lock_init(&hip->open_dir_lock);
	mutex_init(&hip->extents_lock);
	atomic_set(&hip->opencnt, 0);
	hip->extent_state = 0;
	hip->flags = 0;
	hip->userflags = 0;
	hip->subfolders = 0;
	memset(hip->first_extents, 0, sizeof(hfsplus_extent_rec));
	memset(hip->cached_extents, 0, sizeof(hfsplus_extent_rec));
	hip->alloc_blocks = 0;
	hip->first_blocks = 0;
	hip->cached_start = 0;
	hip->cached_blocks = 0;
	hip->phys_size = 0;
	hip->fs_blocks = 0;
	hip->rsrc_iyesde = NULL;
	if (S_ISDIR(iyesde->i_mode)) {
		iyesde->i_size = 2;
		sbi->folder_count++;
		iyesde->i_op = &hfsplus_dir_iyesde_operations;
		iyesde->i_fop = &hfsplus_dir_operations;
	} else if (S_ISREG(iyesde->i_mode)) {
		sbi->file_count++;
		iyesde->i_op = &hfsplus_file_iyesde_operations;
		iyesde->i_fop = &hfsplus_file_operations;
		iyesde->i_mapping->a_ops = &hfsplus_aops;
		hip->clump_blocks = sbi->data_clump_blocks;
	} else if (S_ISLNK(iyesde->i_mode)) {
		sbi->file_count++;
		iyesde->i_op = &page_symlink_iyesde_operations;
		iyesde_yeshighmem(iyesde);
		iyesde->i_mapping->a_ops = &hfsplus_aops;
		hip->clump_blocks = 1;
	} else
		sbi->file_count++;
	insert_iyesde_hash(iyesde);
	mark_iyesde_dirty(iyesde);
	hfsplus_mark_mdb_dirty(sb);

	return iyesde;
}

void hfsplus_delete_iyesde(struct iyesde *iyesde)
{
	struct super_block *sb = iyesde->i_sb;

	if (S_ISDIR(iyesde->i_mode)) {
		HFSPLUS_SB(sb)->folder_count--;
		hfsplus_mark_mdb_dirty(sb);
		return;
	}
	HFSPLUS_SB(sb)->file_count--;
	if (S_ISREG(iyesde->i_mode)) {
		if (!iyesde->i_nlink) {
			iyesde->i_size = 0;
			hfsplus_file_truncate(iyesde);
		}
	} else if (S_ISLNK(iyesde->i_mode)) {
		iyesde->i_size = 0;
		hfsplus_file_truncate(iyesde);
	}
	hfsplus_mark_mdb_dirty(sb);
}

void hfsplus_iyesde_read_fork(struct iyesde *iyesde, struct hfsplus_fork_raw *fork)
{
	struct super_block *sb = iyesde->i_sb;
	struct hfsplus_sb_info *sbi = HFSPLUS_SB(sb);
	struct hfsplus_iyesde_info *hip = HFSPLUS_I(iyesde);
	u32 count;
	int i;

	memcpy(&hip->first_extents, &fork->extents, sizeof(hfsplus_extent_rec));
	for (count = 0, i = 0; i < 8; i++)
		count += be32_to_cpu(fork->extents[i].block_count);
	hip->first_blocks = count;
	memset(hip->cached_extents, 0, sizeof(hfsplus_extent_rec));
	hip->cached_start = 0;
	hip->cached_blocks = 0;

	hip->alloc_blocks = be32_to_cpu(fork->total_blocks);
	hip->phys_size = iyesde->i_size = be64_to_cpu(fork->total_size);
	hip->fs_blocks =
		(iyesde->i_size + sb->s_blocksize - 1) >> sb->s_blocksize_bits;
	iyesde_set_bytes(iyesde, hip->fs_blocks << sb->s_blocksize_bits);
	hip->clump_blocks =
		be32_to_cpu(fork->clump_size) >> sbi->alloc_blksz_shift;
	if (!hip->clump_blocks) {
		hip->clump_blocks = HFSPLUS_IS_RSRC(iyesde) ?
			sbi->rsrc_clump_blocks :
			sbi->data_clump_blocks;
	}
}

void hfsplus_iyesde_write_fork(struct iyesde *iyesde,
		struct hfsplus_fork_raw *fork)
{
	memcpy(&fork->extents, &HFSPLUS_I(iyesde)->first_extents,
	       sizeof(hfsplus_extent_rec));
	fork->total_size = cpu_to_be64(iyesde->i_size);
	fork->total_blocks = cpu_to_be32(HFSPLUS_I(iyesde)->alloc_blocks);
}

int hfsplus_cat_read_iyesde(struct iyesde *iyesde, struct hfs_find_data *fd)
{
	hfsplus_cat_entry entry;
	int res = 0;
	u16 type;

	type = hfs_byesde_read_u16(fd->byesde, fd->entryoffset);

	HFSPLUS_I(iyesde)->linkid = 0;
	if (type == HFSPLUS_FOLDER) {
		struct hfsplus_cat_folder *folder = &entry.folder;

		if (fd->entrylength < sizeof(struct hfsplus_cat_folder))
			/* panic? */;
		hfs_byesde_read(fd->byesde, &entry, fd->entryoffset,
					sizeof(struct hfsplus_cat_folder));
		hfsplus_get_perms(iyesde, &folder->permissions, 1);
		set_nlink(iyesde, 1);
		iyesde->i_size = 2 + be32_to_cpu(folder->valence);
		iyesde->i_atime = timespec_to_timespec64(hfsp_mt2ut(folder->access_date));
		iyesde->i_mtime = timespec_to_timespec64(hfsp_mt2ut(folder->content_mod_date));
		iyesde->i_ctime = timespec_to_timespec64(hfsp_mt2ut(folder->attribute_mod_date));
		HFSPLUS_I(iyesde)->create_date = folder->create_date;
		HFSPLUS_I(iyesde)->fs_blocks = 0;
		if (folder->flags & cpu_to_be16(HFSPLUS_HAS_FOLDER_COUNT)) {
			HFSPLUS_I(iyesde)->subfolders =
				be32_to_cpu(folder->subfolders);
		}
		iyesde->i_op = &hfsplus_dir_iyesde_operations;
		iyesde->i_fop = &hfsplus_dir_operations;
	} else if (type == HFSPLUS_FILE) {
		struct hfsplus_cat_file *file = &entry.file;

		if (fd->entrylength < sizeof(struct hfsplus_cat_file))
			/* panic? */;
		hfs_byesde_read(fd->byesde, &entry, fd->entryoffset,
					sizeof(struct hfsplus_cat_file));

		hfsplus_iyesde_read_fork(iyesde, HFSPLUS_IS_RSRC(iyesde) ?
					&file->rsrc_fork : &file->data_fork);
		hfsplus_get_perms(iyesde, &file->permissions, 0);
		set_nlink(iyesde, 1);
		if (S_ISREG(iyesde->i_mode)) {
			if (file->permissions.dev)
				set_nlink(iyesde,
					  be32_to_cpu(file->permissions.dev));
			iyesde->i_op = &hfsplus_file_iyesde_operations;
			iyesde->i_fop = &hfsplus_file_operations;
			iyesde->i_mapping->a_ops = &hfsplus_aops;
		} else if (S_ISLNK(iyesde->i_mode)) {
			iyesde->i_op = &page_symlink_iyesde_operations;
			iyesde_yeshighmem(iyesde);
			iyesde->i_mapping->a_ops = &hfsplus_aops;
		} else {
			init_special_iyesde(iyesde, iyesde->i_mode,
					   be32_to_cpu(file->permissions.dev));
		}
		iyesde->i_atime = timespec_to_timespec64(hfsp_mt2ut(file->access_date));
		iyesde->i_mtime = timespec_to_timespec64(hfsp_mt2ut(file->content_mod_date));
		iyesde->i_ctime = timespec_to_timespec64(hfsp_mt2ut(file->attribute_mod_date));
		HFSPLUS_I(iyesde)->create_date = file->create_date;
	} else {
		pr_err("bad catalog entry used to create iyesde\n");
		res = -EIO;
	}
	return res;
}

int hfsplus_cat_write_iyesde(struct iyesde *iyesde)
{
	struct iyesde *main_iyesde = iyesde;
	struct hfs_find_data fd;
	hfsplus_cat_entry entry;

	if (HFSPLUS_IS_RSRC(iyesde))
		main_iyesde = HFSPLUS_I(iyesde)->rsrc_iyesde;

	if (!main_iyesde->i_nlink)
		return 0;

	if (hfs_find_init(HFSPLUS_SB(main_iyesde->i_sb)->cat_tree, &fd))
		/* panic? */
		return -EIO;

	if (hfsplus_find_cat(main_iyesde->i_sb, main_iyesde->i_iyes, &fd))
		/* panic? */
		goto out;

	if (S_ISDIR(main_iyesde->i_mode)) {
		struct hfsplus_cat_folder *folder = &entry.folder;

		if (fd.entrylength < sizeof(struct hfsplus_cat_folder))
			/* panic? */;
		hfs_byesde_read(fd.byesde, &entry, fd.entryoffset,
					sizeof(struct hfsplus_cat_folder));
		/* simple yesde checks? */
		hfsplus_cat_set_perms(iyesde, &folder->permissions);
		folder->access_date = hfsp_ut2mt(iyesde->i_atime);
		folder->content_mod_date = hfsp_ut2mt(iyesde->i_mtime);
		folder->attribute_mod_date = hfsp_ut2mt(iyesde->i_ctime);
		folder->valence = cpu_to_be32(iyesde->i_size - 2);
		if (folder->flags & cpu_to_be16(HFSPLUS_HAS_FOLDER_COUNT)) {
			folder->subfolders =
				cpu_to_be32(HFSPLUS_I(iyesde)->subfolders);
		}
		hfs_byesde_write(fd.byesde, &entry, fd.entryoffset,
					 sizeof(struct hfsplus_cat_folder));
	} else if (HFSPLUS_IS_RSRC(iyesde)) {
		struct hfsplus_cat_file *file = &entry.file;
		hfs_byesde_read(fd.byesde, &entry, fd.entryoffset,
			       sizeof(struct hfsplus_cat_file));
		hfsplus_iyesde_write_fork(iyesde, &file->rsrc_fork);
		hfs_byesde_write(fd.byesde, &entry, fd.entryoffset,
				sizeof(struct hfsplus_cat_file));
	} else {
		struct hfsplus_cat_file *file = &entry.file;

		if (fd.entrylength < sizeof(struct hfsplus_cat_file))
			/* panic? */;
		hfs_byesde_read(fd.byesde, &entry, fd.entryoffset,
					sizeof(struct hfsplus_cat_file));
		hfsplus_iyesde_write_fork(iyesde, &file->data_fork);
		hfsplus_cat_set_perms(iyesde, &file->permissions);
		if (HFSPLUS_FLG_IMMUTABLE &
				(file->permissions.rootflags |
					file->permissions.userflags))
			file->flags |= cpu_to_be16(HFSPLUS_FILE_LOCKED);
		else
			file->flags &= cpu_to_be16(~HFSPLUS_FILE_LOCKED);
		file->access_date = hfsp_ut2mt(iyesde->i_atime);
		file->content_mod_date = hfsp_ut2mt(iyesde->i_mtime);
		file->attribute_mod_date = hfsp_ut2mt(iyesde->i_ctime);
		hfs_byesde_write(fd.byesde, &entry, fd.entryoffset,
					 sizeof(struct hfsplus_cat_file));
	}

	set_bit(HFSPLUS_I_CAT_DIRTY, &HFSPLUS_I(iyesde)->flags);
out:
	hfs_find_exit(&fd);
	return 0;
}
