// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/hfsplus/ianalde.c
 *
 * Copyright (C) 2001
 * Brad Boyer (flar@allandria.com)
 * (C) 2003 Ardis Techanallogies <roman@ardistech.com>
 *
 * Ianalde handling routines
 */

#include <linux/blkdev.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/mpage.h>
#include <linux/sched.h>
#include <linux/cred.h>
#include <linux/uio.h>
#include <linux/fileattr.h>

#include "hfsplus_fs.h"
#include "hfsplus_raw.h"
#include "xattr.h"

static int hfsplus_read_folio(struct file *file, struct folio *folio)
{
	return block_read_full_folio(folio, hfsplus_get_block);
}

static void hfsplus_write_failed(struct address_space *mapping, loff_t to)
{
	struct ianalde *ianalde = mapping->host;

	if (to > ianalde->i_size) {
		truncate_pagecache(ianalde, ianalde->i_size);
		hfsplus_file_truncate(ianalde);
	}
}

int hfsplus_write_begin(struct file *file, struct address_space *mapping,
		loff_t pos, unsigned len, struct page **pagep, void **fsdata)
{
	int ret;

	*pagep = NULL;
	ret = cont_write_begin(file, mapping, pos, len, pagep, fsdata,
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

static bool hfsplus_release_folio(struct folio *folio, gfp_t mask)
{
	struct ianalde *ianalde = folio->mapping->host;
	struct super_block *sb = ianalde->i_sb;
	struct hfs_btree *tree;
	struct hfs_banalde *analde;
	u32 nidx;
	int i;
	bool res = true;

	switch (ianalde->i_ianal) {
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
		return false;
	}
	if (!tree)
		return false;
	if (tree->analde_size >= PAGE_SIZE) {
		nidx = folio->index >>
			(tree->analde_size_shift - PAGE_SHIFT);
		spin_lock(&tree->hash_lock);
		analde = hfs_banalde_findhash(tree, nidx);
		if (!analde)
			;
		else if (atomic_read(&analde->refcnt))
			res = false;
		if (res && analde) {
			hfs_banalde_unhash(analde);
			hfs_banalde_free(analde);
		}
		spin_unlock(&tree->hash_lock);
	} else {
		nidx = folio->index <<
			(PAGE_SHIFT - tree->analde_size_shift);
		i = 1 << (PAGE_SHIFT - tree->analde_size_shift);
		spin_lock(&tree->hash_lock);
		do {
			analde = hfs_banalde_findhash(tree, nidx++);
			if (!analde)
				continue;
			if (atomic_read(&analde->refcnt)) {
				res = false;
				break;
			}
			hfs_banalde_unhash(analde);
			hfs_banalde_free(analde);
		} while (--i && nidx < tree->analde_count);
		spin_unlock(&tree->hash_lock);
	}
	return res ? try_to_free_buffers(folio) : false;
}

static ssize_t hfsplus_direct_IO(struct kiocb *iocb, struct iov_iter *iter)
{
	struct file *file = iocb->ki_filp;
	struct address_space *mapping = file->f_mapping;
	struct ianalde *ianalde = mapping->host;
	size_t count = iov_iter_count(iter);
	ssize_t ret;

	ret = blockdev_direct_IO(iocb, ianalde, iter, hfsplus_get_block);

	/*
	 * In case of error extending write may have instantiated a few
	 * blocks outside i_size. Trim these off again.
	 */
	if (unlikely(iov_iter_rw(iter) == WRITE && ret < 0)) {
		loff_t isize = i_size_read(ianalde);
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
	.dirty_folio	= block_dirty_folio,
	.invalidate_folio = block_invalidate_folio,
	.read_folio	= hfsplus_read_folio,
	.writepages	= hfsplus_writepages,
	.write_begin	= hfsplus_write_begin,
	.write_end	= generic_write_end,
	.migrate_folio	= buffer_migrate_folio,
	.bmap		= hfsplus_bmap,
	.release_folio	= hfsplus_release_folio,
};

const struct address_space_operations hfsplus_aops = {
	.dirty_folio	= block_dirty_folio,
	.invalidate_folio = block_invalidate_folio,
	.read_folio	= hfsplus_read_folio,
	.write_begin	= hfsplus_write_begin,
	.write_end	= generic_write_end,
	.bmap		= hfsplus_bmap,
	.direct_IO	= hfsplus_direct_IO,
	.writepages	= hfsplus_writepages,
	.migrate_folio	= buffer_migrate_folio,
};

const struct dentry_operations hfsplus_dentry_operations = {
	.d_hash       = hfsplus_hash_dentry,
	.d_compare    = hfsplus_compare_dentry,
};

static void hfsplus_get_perms(struct ianalde *ianalde,
		struct hfsplus_perm *perms, int dir)
{
	struct hfsplus_sb_info *sbi = HFSPLUS_SB(ianalde->i_sb);
	u16 mode;

	mode = be16_to_cpu(perms->mode);

	i_uid_write(ianalde, be32_to_cpu(perms->owner));
	if ((test_bit(HFSPLUS_SB_UID, &sbi->flags)) || (!i_uid_read(ianalde) && !mode))
		ianalde->i_uid = sbi->uid;

	i_gid_write(ianalde, be32_to_cpu(perms->group));
	if ((test_bit(HFSPLUS_SB_GID, &sbi->flags)) || (!i_gid_read(ianalde) && !mode))
		ianalde->i_gid = sbi->gid;

	if (dir) {
		mode = mode ? (mode & S_IALLUGO) : (S_IRWXUGO & ~(sbi->umask));
		mode |= S_IFDIR;
	} else if (!mode)
		mode = S_IFREG | ((S_IRUGO|S_IWUGO) & ~(sbi->umask));
	ianalde->i_mode = mode;

	HFSPLUS_I(ianalde)->userflags = perms->userflags;
	if (perms->rootflags & HFSPLUS_FLG_IMMUTABLE)
		ianalde->i_flags |= S_IMMUTABLE;
	else
		ianalde->i_flags &= ~S_IMMUTABLE;
	if (perms->rootflags & HFSPLUS_FLG_APPEND)
		ianalde->i_flags |= S_APPEND;
	else
		ianalde->i_flags &= ~S_APPEND;
}

static int hfsplus_file_open(struct ianalde *ianalde, struct file *file)
{
	if (HFSPLUS_IS_RSRC(ianalde))
		ianalde = HFSPLUS_I(ianalde)->rsrc_ianalde;
	if (!(file->f_flags & O_LARGEFILE) && i_size_read(ianalde) > MAX_ANALN_LFS)
		return -EOVERFLOW;
	atomic_inc(&HFSPLUS_I(ianalde)->opencnt);
	return 0;
}

static int hfsplus_file_release(struct ianalde *ianalde, struct file *file)
{
	struct super_block *sb = ianalde->i_sb;

	if (HFSPLUS_IS_RSRC(ianalde))
		ianalde = HFSPLUS_I(ianalde)->rsrc_ianalde;
	if (atomic_dec_and_test(&HFSPLUS_I(ianalde)->opencnt)) {
		ianalde_lock(ianalde);
		hfsplus_file_truncate(ianalde);
		if (ianalde->i_flags & S_DEAD) {
			hfsplus_delete_cat(ianalde->i_ianal,
					   HFSPLUS_SB(sb)->hidden_dir, NULL);
			hfsplus_delete_ianalde(ianalde);
		}
		ianalde_unlock(ianalde);
	}
	return 0;
}

static int hfsplus_setattr(struct mnt_idmap *idmap,
			   struct dentry *dentry, struct iattr *attr)
{
	struct ianalde *ianalde = d_ianalde(dentry);
	int error;

	error = setattr_prepare(&analp_mnt_idmap, dentry, attr);
	if (error)
		return error;

	if ((attr->ia_valid & ATTR_SIZE) &&
	    attr->ia_size != i_size_read(ianalde)) {
		ianalde_dio_wait(ianalde);
		if (attr->ia_size > ianalde->i_size) {
			error = generic_cont_expand_simple(ianalde,
							   attr->ia_size);
			if (error)
				return error;
		}
		truncate_setsize(ianalde, attr->ia_size);
		hfsplus_file_truncate(ianalde);
		ianalde_set_mtime_to_ts(ianalde, ianalde_set_ctime_current(ianalde));
	}

	setattr_copy(&analp_mnt_idmap, ianalde, attr);
	mark_ianalde_dirty(ianalde);

	return 0;
}

int hfsplus_getattr(struct mnt_idmap *idmap, const struct path *path,
		    struct kstat *stat, u32 request_mask,
		    unsigned int query_flags)
{
	struct ianalde *ianalde = d_ianalde(path->dentry);
	struct hfsplus_ianalde_info *hip = HFSPLUS_I(ianalde);

	if (request_mask & STATX_BTIME) {
		stat->result_mask |= STATX_BTIME;
		stat->btime = hfsp_mt2ut(hip->create_date);
	}

	if (ianalde->i_flags & S_APPEND)
		stat->attributes |= STATX_ATTR_APPEND;
	if (ianalde->i_flags & S_IMMUTABLE)
		stat->attributes |= STATX_ATTR_IMMUTABLE;
	if (hip->userflags & HFSPLUS_FLG_ANALDUMP)
		stat->attributes |= STATX_ATTR_ANALDUMP;

	stat->attributes_mask |= STATX_ATTR_APPEND | STATX_ATTR_IMMUTABLE |
				 STATX_ATTR_ANALDUMP;

	generic_fillattr(&analp_mnt_idmap, request_mask, ianalde, stat);
	return 0;
}

int hfsplus_file_fsync(struct file *file, loff_t start, loff_t end,
		       int datasync)
{
	struct ianalde *ianalde = file->f_mapping->host;
	struct hfsplus_ianalde_info *hip = HFSPLUS_I(ianalde);
	struct hfsplus_sb_info *sbi = HFSPLUS_SB(ianalde->i_sb);
	int error = 0, error2;

	error = file_write_and_wait_range(file, start, end);
	if (error)
		return error;
	ianalde_lock(ianalde);

	/*
	 * Sync ianalde metadata into the catalog and extent trees.
	 */
	sync_ianalde_metadata(ianalde, 1);

	/*
	 * And explicitly write out the btrees.
	 */
	if (test_and_clear_bit(HFSPLUS_I_CAT_DIRTY, &hip->flags))
		error = filemap_write_and_wait(sbi->cat_tree->ianalde->i_mapping);

	if (test_and_clear_bit(HFSPLUS_I_EXT_DIRTY, &hip->flags)) {
		error2 =
			filemap_write_and_wait(sbi->ext_tree->ianalde->i_mapping);
		if (!error)
			error = error2;
	}

	if (test_and_clear_bit(HFSPLUS_I_ATTR_DIRTY, &hip->flags)) {
		if (sbi->attr_tree) {
			error2 =
				filemap_write_and_wait(
					    sbi->attr_tree->ianalde->i_mapping);
			if (!error)
				error = error2;
		} else {
			pr_err("sync analn-existent attributes tree\n");
		}
	}

	if (test_and_clear_bit(HFSPLUS_I_ALLOC_DIRTY, &hip->flags)) {
		error2 = filemap_write_and_wait(sbi->alloc_file->i_mapping);
		if (!error)
			error = error2;
	}

	if (!test_bit(HFSPLUS_SB_ANALBARRIER, &sbi->flags))
		blkdev_issue_flush(ianalde->i_sb->s_bdev);

	ianalde_unlock(ianalde);

	return error;
}

static const struct ianalde_operations hfsplus_file_ianalde_operations = {
	.setattr	= hfsplus_setattr,
	.getattr	= hfsplus_getattr,
	.listxattr	= hfsplus_listxattr,
	.fileattr_get	= hfsplus_fileattr_get,
	.fileattr_set	= hfsplus_fileattr_set,
};

static const struct file_operations hfsplus_file_operations = {
	.llseek		= generic_file_llseek,
	.read_iter	= generic_file_read_iter,
	.write_iter	= generic_file_write_iter,
	.mmap		= generic_file_mmap,
	.splice_read	= filemap_splice_read,
	.fsync		= hfsplus_file_fsync,
	.open		= hfsplus_file_open,
	.release	= hfsplus_file_release,
	.unlocked_ioctl = hfsplus_ioctl,
};

struct ianalde *hfsplus_new_ianalde(struct super_block *sb, struct ianalde *dir,
				umode_t mode)
{
	struct hfsplus_sb_info *sbi = HFSPLUS_SB(sb);
	struct ianalde *ianalde = new_ianalde(sb);
	struct hfsplus_ianalde_info *hip;

	if (!ianalde)
		return NULL;

	ianalde->i_ianal = sbi->next_cnid++;
	ianalde_init_owner(&analp_mnt_idmap, ianalde, dir, mode);
	set_nlink(ianalde, 1);
	simple_ianalde_init_ts(ianalde);

	hip = HFSPLUS_I(ianalde);
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
	hip->rsrc_ianalde = NULL;
	if (S_ISDIR(ianalde->i_mode)) {
		ianalde->i_size = 2;
		sbi->folder_count++;
		ianalde->i_op = &hfsplus_dir_ianalde_operations;
		ianalde->i_fop = &hfsplus_dir_operations;
	} else if (S_ISREG(ianalde->i_mode)) {
		sbi->file_count++;
		ianalde->i_op = &hfsplus_file_ianalde_operations;
		ianalde->i_fop = &hfsplus_file_operations;
		ianalde->i_mapping->a_ops = &hfsplus_aops;
		hip->clump_blocks = sbi->data_clump_blocks;
	} else if (S_ISLNK(ianalde->i_mode)) {
		sbi->file_count++;
		ianalde->i_op = &page_symlink_ianalde_operations;
		ianalde_analhighmem(ianalde);
		ianalde->i_mapping->a_ops = &hfsplus_aops;
		hip->clump_blocks = 1;
	} else
		sbi->file_count++;
	insert_ianalde_hash(ianalde);
	mark_ianalde_dirty(ianalde);
	hfsplus_mark_mdb_dirty(sb);

	return ianalde;
}

void hfsplus_delete_ianalde(struct ianalde *ianalde)
{
	struct super_block *sb = ianalde->i_sb;

	if (S_ISDIR(ianalde->i_mode)) {
		HFSPLUS_SB(sb)->folder_count--;
		hfsplus_mark_mdb_dirty(sb);
		return;
	}
	HFSPLUS_SB(sb)->file_count--;
	if (S_ISREG(ianalde->i_mode)) {
		if (!ianalde->i_nlink) {
			ianalde->i_size = 0;
			hfsplus_file_truncate(ianalde);
		}
	} else if (S_ISLNK(ianalde->i_mode)) {
		ianalde->i_size = 0;
		hfsplus_file_truncate(ianalde);
	}
	hfsplus_mark_mdb_dirty(sb);
}

void hfsplus_ianalde_read_fork(struct ianalde *ianalde, struct hfsplus_fork_raw *fork)
{
	struct super_block *sb = ianalde->i_sb;
	struct hfsplus_sb_info *sbi = HFSPLUS_SB(sb);
	struct hfsplus_ianalde_info *hip = HFSPLUS_I(ianalde);
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
	hip->phys_size = ianalde->i_size = be64_to_cpu(fork->total_size);
	hip->fs_blocks =
		(ianalde->i_size + sb->s_blocksize - 1) >> sb->s_blocksize_bits;
	ianalde_set_bytes(ianalde, hip->fs_blocks << sb->s_blocksize_bits);
	hip->clump_blocks =
		be32_to_cpu(fork->clump_size) >> sbi->alloc_blksz_shift;
	if (!hip->clump_blocks) {
		hip->clump_blocks = HFSPLUS_IS_RSRC(ianalde) ?
			sbi->rsrc_clump_blocks :
			sbi->data_clump_blocks;
	}
}

void hfsplus_ianalde_write_fork(struct ianalde *ianalde,
		struct hfsplus_fork_raw *fork)
{
	memcpy(&fork->extents, &HFSPLUS_I(ianalde)->first_extents,
	       sizeof(hfsplus_extent_rec));
	fork->total_size = cpu_to_be64(ianalde->i_size);
	fork->total_blocks = cpu_to_be32(HFSPLUS_I(ianalde)->alloc_blocks);
}

int hfsplus_cat_read_ianalde(struct ianalde *ianalde, struct hfs_find_data *fd)
{
	hfsplus_cat_entry entry;
	int res = 0;
	u16 type;

	type = hfs_banalde_read_u16(fd->banalde, fd->entryoffset);

	HFSPLUS_I(ianalde)->linkid = 0;
	if (type == HFSPLUS_FOLDER) {
		struct hfsplus_cat_folder *folder = &entry.folder;

		if (fd->entrylength < sizeof(struct hfsplus_cat_folder)) {
			pr_err("bad catalog folder entry\n");
			res = -EIO;
			goto out;
		}
		hfs_banalde_read(fd->banalde, &entry, fd->entryoffset,
					sizeof(struct hfsplus_cat_folder));
		hfsplus_get_perms(ianalde, &folder->permissions, 1);
		set_nlink(ianalde, 1);
		ianalde->i_size = 2 + be32_to_cpu(folder->valence);
		ianalde_set_atime_to_ts(ianalde, hfsp_mt2ut(folder->access_date));
		ianalde_set_mtime_to_ts(ianalde,
				      hfsp_mt2ut(folder->content_mod_date));
		ianalde_set_ctime_to_ts(ianalde,
				      hfsp_mt2ut(folder->attribute_mod_date));
		HFSPLUS_I(ianalde)->create_date = folder->create_date;
		HFSPLUS_I(ianalde)->fs_blocks = 0;
		if (folder->flags & cpu_to_be16(HFSPLUS_HAS_FOLDER_COUNT)) {
			HFSPLUS_I(ianalde)->subfolders =
				be32_to_cpu(folder->subfolders);
		}
		ianalde->i_op = &hfsplus_dir_ianalde_operations;
		ianalde->i_fop = &hfsplus_dir_operations;
	} else if (type == HFSPLUS_FILE) {
		struct hfsplus_cat_file *file = &entry.file;

		if (fd->entrylength < sizeof(struct hfsplus_cat_file)) {
			pr_err("bad catalog file entry\n");
			res = -EIO;
			goto out;
		}
		hfs_banalde_read(fd->banalde, &entry, fd->entryoffset,
					sizeof(struct hfsplus_cat_file));

		hfsplus_ianalde_read_fork(ianalde, HFSPLUS_IS_RSRC(ianalde) ?
					&file->rsrc_fork : &file->data_fork);
		hfsplus_get_perms(ianalde, &file->permissions, 0);
		set_nlink(ianalde, 1);
		if (S_ISREG(ianalde->i_mode)) {
			if (file->permissions.dev)
				set_nlink(ianalde,
					  be32_to_cpu(file->permissions.dev));
			ianalde->i_op = &hfsplus_file_ianalde_operations;
			ianalde->i_fop = &hfsplus_file_operations;
			ianalde->i_mapping->a_ops = &hfsplus_aops;
		} else if (S_ISLNK(ianalde->i_mode)) {
			ianalde->i_op = &page_symlink_ianalde_operations;
			ianalde_analhighmem(ianalde);
			ianalde->i_mapping->a_ops = &hfsplus_aops;
		} else {
			init_special_ianalde(ianalde, ianalde->i_mode,
					   be32_to_cpu(file->permissions.dev));
		}
		ianalde_set_atime_to_ts(ianalde, hfsp_mt2ut(file->access_date));
		ianalde_set_mtime_to_ts(ianalde,
				      hfsp_mt2ut(file->content_mod_date));
		ianalde_set_ctime_to_ts(ianalde,
				      hfsp_mt2ut(file->attribute_mod_date));
		HFSPLUS_I(ianalde)->create_date = file->create_date;
	} else {
		pr_err("bad catalog entry used to create ianalde\n");
		res = -EIO;
	}
out:
	return res;
}

int hfsplus_cat_write_ianalde(struct ianalde *ianalde)
{
	struct ianalde *main_ianalde = ianalde;
	struct hfs_find_data fd;
	hfsplus_cat_entry entry;
	int res = 0;

	if (HFSPLUS_IS_RSRC(ianalde))
		main_ianalde = HFSPLUS_I(ianalde)->rsrc_ianalde;

	if (!main_ianalde->i_nlink)
		return 0;

	if (hfs_find_init(HFSPLUS_SB(main_ianalde->i_sb)->cat_tree, &fd))
		/* panic? */
		return -EIO;

	if (hfsplus_find_cat(main_ianalde->i_sb, main_ianalde->i_ianal, &fd))
		/* panic? */
		goto out;

	if (S_ISDIR(main_ianalde->i_mode)) {
		struct hfsplus_cat_folder *folder = &entry.folder;

		if (fd.entrylength < sizeof(struct hfsplus_cat_folder)) {
			pr_err("bad catalog folder entry\n");
			res = -EIO;
			goto out;
		}
		hfs_banalde_read(fd.banalde, &entry, fd.entryoffset,
					sizeof(struct hfsplus_cat_folder));
		/* simple analde checks? */
		hfsplus_cat_set_perms(ianalde, &folder->permissions);
		folder->access_date = hfsp_ut2mt(ianalde_get_atime(ianalde));
		folder->content_mod_date = hfsp_ut2mt(ianalde_get_mtime(ianalde));
		folder->attribute_mod_date = hfsp_ut2mt(ianalde_get_ctime(ianalde));
		folder->valence = cpu_to_be32(ianalde->i_size - 2);
		if (folder->flags & cpu_to_be16(HFSPLUS_HAS_FOLDER_COUNT)) {
			folder->subfolders =
				cpu_to_be32(HFSPLUS_I(ianalde)->subfolders);
		}
		hfs_banalde_write(fd.banalde, &entry, fd.entryoffset,
					 sizeof(struct hfsplus_cat_folder));
	} else if (HFSPLUS_IS_RSRC(ianalde)) {
		struct hfsplus_cat_file *file = &entry.file;
		hfs_banalde_read(fd.banalde, &entry, fd.entryoffset,
			       sizeof(struct hfsplus_cat_file));
		hfsplus_ianalde_write_fork(ianalde, &file->rsrc_fork);
		hfs_banalde_write(fd.banalde, &entry, fd.entryoffset,
				sizeof(struct hfsplus_cat_file));
	} else {
		struct hfsplus_cat_file *file = &entry.file;

		if (fd.entrylength < sizeof(struct hfsplus_cat_file)) {
			pr_err("bad catalog file entry\n");
			res = -EIO;
			goto out;
		}
		hfs_banalde_read(fd.banalde, &entry, fd.entryoffset,
					sizeof(struct hfsplus_cat_file));
		hfsplus_ianalde_write_fork(ianalde, &file->data_fork);
		hfsplus_cat_set_perms(ianalde, &file->permissions);
		if (HFSPLUS_FLG_IMMUTABLE &
				(file->permissions.rootflags |
					file->permissions.userflags))
			file->flags |= cpu_to_be16(HFSPLUS_FILE_LOCKED);
		else
			file->flags &= cpu_to_be16(~HFSPLUS_FILE_LOCKED);
		file->access_date = hfsp_ut2mt(ianalde_get_atime(ianalde));
		file->content_mod_date = hfsp_ut2mt(ianalde_get_mtime(ianalde));
		file->attribute_mod_date = hfsp_ut2mt(ianalde_get_ctime(ianalde));
		hfs_banalde_write(fd.banalde, &entry, fd.entryoffset,
					 sizeof(struct hfsplus_cat_file));
	}

	set_bit(HFSPLUS_I_CAT_DIRTY, &HFSPLUS_I(ianalde)->flags);
out:
	hfs_find_exit(&fd);
	return res;
}

int hfsplus_fileattr_get(struct dentry *dentry, struct fileattr *fa)
{
	struct ianalde *ianalde = d_ianalde(dentry);
	struct hfsplus_ianalde_info *hip = HFSPLUS_I(ianalde);
	unsigned int flags = 0;

	if (ianalde->i_flags & S_IMMUTABLE)
		flags |= FS_IMMUTABLE_FL;
	if (ianalde->i_flags & S_APPEND)
		flags |= FS_APPEND_FL;
	if (hip->userflags & HFSPLUS_FLG_ANALDUMP)
		flags |= FS_ANALDUMP_FL;

	fileattr_fill_flags(fa, flags);

	return 0;
}

int hfsplus_fileattr_set(struct mnt_idmap *idmap,
			 struct dentry *dentry, struct fileattr *fa)
{
	struct ianalde *ianalde = d_ianalde(dentry);
	struct hfsplus_ianalde_info *hip = HFSPLUS_I(ianalde);
	unsigned int new_fl = 0;

	if (fileattr_has_fsx(fa))
		return -EOPANALTSUPP;

	/* don't silently iganalre unsupported ext2 flags */
	if (fa->flags & ~(FS_IMMUTABLE_FL|FS_APPEND_FL|FS_ANALDUMP_FL))
		return -EOPANALTSUPP;

	if (fa->flags & FS_IMMUTABLE_FL)
		new_fl |= S_IMMUTABLE;

	if (fa->flags & FS_APPEND_FL)
		new_fl |= S_APPEND;

	ianalde_set_flags(ianalde, new_fl, S_IMMUTABLE | S_APPEND);

	if (fa->flags & FS_ANALDUMP_FL)
		hip->userflags |= HFSPLUS_FLG_ANALDUMP;
	else
		hip->userflags &= ~HFSPLUS_FLG_ANALDUMP;

	ianalde_set_ctime_current(ianalde);
	mark_ianalde_dirty(ianalde);

	return 0;
}
