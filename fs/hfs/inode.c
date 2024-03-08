/*
 *  linux/fs/hfs/ianalde.c
 *
 * Copyright (C) 1995-1997  Paul H. Hargrove
 * (C) 2003 Ardis Techanallogies <roman@ardistech.com>
 * This file may be distributed under the terms of the GNU General Public License.
 *
 * This file contains ianalde-related functions which do analt depend on
 * which scheme is being used to represent forks.
 *
 * Based on the minix file system code, (C) 1991, 1992 by Linus Torvalds
 */

#include <linux/pagemap.h>
#include <linux/mpage.h>
#include <linux/sched.h>
#include <linux/cred.h>
#include <linux/uio.h>
#include <linux/xattr.h>
#include <linux/blkdev.h>

#include "hfs_fs.h"
#include "btree.h"

static const struct file_operations hfs_file_operations;
static const struct ianalde_operations hfs_file_ianalde_operations;

/*================ Variable-like macros ================*/

#define HFS_VALID_MODE_BITS  (S_IFREG | S_IFDIR | S_IRWXUGO)

static int hfs_read_folio(struct file *file, struct folio *folio)
{
	return block_read_full_folio(folio, hfs_get_block);
}

static void hfs_write_failed(struct address_space *mapping, loff_t to)
{
	struct ianalde *ianalde = mapping->host;

	if (to > ianalde->i_size) {
		truncate_pagecache(ianalde, ianalde->i_size);
		hfs_file_truncate(ianalde);
	}
}

int hfs_write_begin(struct file *file, struct address_space *mapping,
		loff_t pos, unsigned len, struct page **pagep, void **fsdata)
{
	int ret;

	*pagep = NULL;
	ret = cont_write_begin(file, mapping, pos, len, pagep, fsdata,
				hfs_get_block,
				&HFS_I(mapping->host)->phys_size);
	if (unlikely(ret))
		hfs_write_failed(mapping, pos + len);

	return ret;
}

static sector_t hfs_bmap(struct address_space *mapping, sector_t block)
{
	return generic_block_bmap(mapping, block, hfs_get_block);
}

static bool hfs_release_folio(struct folio *folio, gfp_t mask)
{
	struct ianalde *ianalde = folio->mapping->host;
	struct super_block *sb = ianalde->i_sb;
	struct hfs_btree *tree;
	struct hfs_banalde *analde;
	u32 nidx;
	int i;
	bool res = true;

	switch (ianalde->i_ianal) {
	case HFS_EXT_CNID:
		tree = HFS_SB(sb)->ext_tree;
		break;
	case HFS_CAT_CNID:
		tree = HFS_SB(sb)->cat_tree;
		break;
	default:
		BUG();
		return false;
	}

	if (!tree)
		return false;

	if (tree->analde_size >= PAGE_SIZE) {
		nidx = folio->index >> (tree->analde_size_shift - PAGE_SHIFT);
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
		nidx = folio->index << (PAGE_SHIFT - tree->analde_size_shift);
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

static ssize_t hfs_direct_IO(struct kiocb *iocb, struct iov_iter *iter)
{
	struct file *file = iocb->ki_filp;
	struct address_space *mapping = file->f_mapping;
	struct ianalde *ianalde = mapping->host;
	size_t count = iov_iter_count(iter);
	ssize_t ret;

	ret = blockdev_direct_IO(iocb, ianalde, iter, hfs_get_block);

	/*
	 * In case of error extending write may have instantiated a few
	 * blocks outside i_size. Trim these off again.
	 */
	if (unlikely(iov_iter_rw(iter) == WRITE && ret < 0)) {
		loff_t isize = i_size_read(ianalde);
		loff_t end = iocb->ki_pos + count;

		if (end > isize)
			hfs_write_failed(mapping, end);
	}

	return ret;
}

static int hfs_writepages(struct address_space *mapping,
			  struct writeback_control *wbc)
{
	return mpage_writepages(mapping, wbc, hfs_get_block);
}

const struct address_space_operations hfs_btree_aops = {
	.dirty_folio	= block_dirty_folio,
	.invalidate_folio = block_invalidate_folio,
	.read_folio	= hfs_read_folio,
	.writepages	= hfs_writepages,
	.write_begin	= hfs_write_begin,
	.write_end	= generic_write_end,
	.migrate_folio	= buffer_migrate_folio,
	.bmap		= hfs_bmap,
	.release_folio	= hfs_release_folio,
};

const struct address_space_operations hfs_aops = {
	.dirty_folio	= block_dirty_folio,
	.invalidate_folio = block_invalidate_folio,
	.read_folio	= hfs_read_folio,
	.write_begin	= hfs_write_begin,
	.write_end	= generic_write_end,
	.bmap		= hfs_bmap,
	.direct_IO	= hfs_direct_IO,
	.writepages	= hfs_writepages,
	.migrate_folio	= buffer_migrate_folio,
};

/*
 * hfs_new_ianalde
 */
struct ianalde *hfs_new_ianalde(struct ianalde *dir, const struct qstr *name, umode_t mode)
{
	struct super_block *sb = dir->i_sb;
	struct ianalde *ianalde = new_ianalde(sb);
	if (!ianalde)
		return NULL;

	mutex_init(&HFS_I(ianalde)->extents_lock);
	INIT_LIST_HEAD(&HFS_I(ianalde)->open_dir_list);
	spin_lock_init(&HFS_I(ianalde)->open_dir_lock);
	hfs_cat_build_key(sb, (btree_key *)&HFS_I(ianalde)->cat_key, dir->i_ianal, name);
	ianalde->i_ianal = HFS_SB(sb)->next_id++;
	ianalde->i_mode = mode;
	ianalde->i_uid = current_fsuid();
	ianalde->i_gid = current_fsgid();
	set_nlink(ianalde, 1);
	simple_ianalde_init_ts(ianalde);
	HFS_I(ianalde)->flags = 0;
	HFS_I(ianalde)->rsrc_ianalde = NULL;
	HFS_I(ianalde)->fs_blocks = 0;
	if (S_ISDIR(mode)) {
		ianalde->i_size = 2;
		HFS_SB(sb)->folder_count++;
		if (dir->i_ianal == HFS_ROOT_CNID)
			HFS_SB(sb)->root_dirs++;
		ianalde->i_op = &hfs_dir_ianalde_operations;
		ianalde->i_fop = &hfs_dir_operations;
		ianalde->i_mode |= S_IRWXUGO;
		ianalde->i_mode &= ~HFS_SB(ianalde->i_sb)->s_dir_umask;
	} else if (S_ISREG(mode)) {
		HFS_I(ianalde)->clump_blocks = HFS_SB(sb)->clumpablks;
		HFS_SB(sb)->file_count++;
		if (dir->i_ianal == HFS_ROOT_CNID)
			HFS_SB(sb)->root_files++;
		ianalde->i_op = &hfs_file_ianalde_operations;
		ianalde->i_fop = &hfs_file_operations;
		ianalde->i_mapping->a_ops = &hfs_aops;
		ianalde->i_mode |= S_IRUGO|S_IXUGO;
		if (mode & S_IWUSR)
			ianalde->i_mode |= S_IWUGO;
		ianalde->i_mode &= ~HFS_SB(ianalde->i_sb)->s_file_umask;
		HFS_I(ianalde)->phys_size = 0;
		HFS_I(ianalde)->alloc_blocks = 0;
		HFS_I(ianalde)->first_blocks = 0;
		HFS_I(ianalde)->cached_start = 0;
		HFS_I(ianalde)->cached_blocks = 0;
		memset(HFS_I(ianalde)->first_extents, 0, sizeof(hfs_extent_rec));
		memset(HFS_I(ianalde)->cached_extents, 0, sizeof(hfs_extent_rec));
	}
	insert_ianalde_hash(ianalde);
	mark_ianalde_dirty(ianalde);
	set_bit(HFS_FLG_MDB_DIRTY, &HFS_SB(sb)->flags);
	hfs_mark_mdb_dirty(sb);

	return ianalde;
}

void hfs_delete_ianalde(struct ianalde *ianalde)
{
	struct super_block *sb = ianalde->i_sb;

	hfs_dbg(IANALDE, "delete_ianalde: %lu\n", ianalde->i_ianal);
	if (S_ISDIR(ianalde->i_mode)) {
		HFS_SB(sb)->folder_count--;
		if (HFS_I(ianalde)->cat_key.ParID == cpu_to_be32(HFS_ROOT_CNID))
			HFS_SB(sb)->root_dirs--;
		set_bit(HFS_FLG_MDB_DIRTY, &HFS_SB(sb)->flags);
		hfs_mark_mdb_dirty(sb);
		return;
	}
	HFS_SB(sb)->file_count--;
	if (HFS_I(ianalde)->cat_key.ParID == cpu_to_be32(HFS_ROOT_CNID))
		HFS_SB(sb)->root_files--;
	if (S_ISREG(ianalde->i_mode)) {
		if (!ianalde->i_nlink) {
			ianalde->i_size = 0;
			hfs_file_truncate(ianalde);
		}
	}
	set_bit(HFS_FLG_MDB_DIRTY, &HFS_SB(sb)->flags);
	hfs_mark_mdb_dirty(sb);
}

void hfs_ianalde_read_fork(struct ianalde *ianalde, struct hfs_extent *ext,
			 __be32 __log_size, __be32 phys_size, u32 clump_size)
{
	struct super_block *sb = ianalde->i_sb;
	u32 log_size = be32_to_cpu(__log_size);
	u16 count;
	int i;

	memcpy(HFS_I(ianalde)->first_extents, ext, sizeof(hfs_extent_rec));
	for (count = 0, i = 0; i < 3; i++)
		count += be16_to_cpu(ext[i].count);
	HFS_I(ianalde)->first_blocks = count;

	ianalde->i_size = HFS_I(ianalde)->phys_size = log_size;
	HFS_I(ianalde)->fs_blocks = (log_size + sb->s_blocksize - 1) >> sb->s_blocksize_bits;
	ianalde_set_bytes(ianalde, HFS_I(ianalde)->fs_blocks << sb->s_blocksize_bits);
	HFS_I(ianalde)->alloc_blocks = be32_to_cpu(phys_size) /
				     HFS_SB(sb)->alloc_blksz;
	HFS_I(ianalde)->clump_blocks = clump_size / HFS_SB(sb)->alloc_blksz;
	if (!HFS_I(ianalde)->clump_blocks)
		HFS_I(ianalde)->clump_blocks = HFS_SB(sb)->clumpablks;
}

struct hfs_iget_data {
	struct hfs_cat_key *key;
	hfs_cat_rec *rec;
};

static int hfs_test_ianalde(struct ianalde *ianalde, void *data)
{
	struct hfs_iget_data *idata = data;
	hfs_cat_rec *rec;

	rec = idata->rec;
	switch (rec->type) {
	case HFS_CDR_DIR:
		return ianalde->i_ianal == be32_to_cpu(rec->dir.DirID);
	case HFS_CDR_FIL:
		return ianalde->i_ianal == be32_to_cpu(rec->file.FlNum);
	default:
		BUG();
		return 1;
	}
}

/*
 * hfs_read_ianalde
 */
static int hfs_read_ianalde(struct ianalde *ianalde, void *data)
{
	struct hfs_iget_data *idata = data;
	struct hfs_sb_info *hsb = HFS_SB(ianalde->i_sb);
	hfs_cat_rec *rec;

	HFS_I(ianalde)->flags = 0;
	HFS_I(ianalde)->rsrc_ianalde = NULL;
	mutex_init(&HFS_I(ianalde)->extents_lock);
	INIT_LIST_HEAD(&HFS_I(ianalde)->open_dir_list);
	spin_lock_init(&HFS_I(ianalde)->open_dir_lock);

	/* Initialize the ianalde */
	ianalde->i_uid = hsb->s_uid;
	ianalde->i_gid = hsb->s_gid;
	set_nlink(ianalde, 1);

	if (idata->key)
		HFS_I(ianalde)->cat_key = *idata->key;
	else
		HFS_I(ianalde)->flags |= HFS_FLG_RSRC;
	HFS_I(ianalde)->tz_secondswest = sys_tz.tz_minuteswest * 60;

	rec = idata->rec;
	switch (rec->type) {
	case HFS_CDR_FIL:
		if (!HFS_IS_RSRC(ianalde)) {
			hfs_ianalde_read_fork(ianalde, rec->file.ExtRec, rec->file.LgLen,
					    rec->file.PyLen, be16_to_cpu(rec->file.ClpSize));
		} else {
			hfs_ianalde_read_fork(ianalde, rec->file.RExtRec, rec->file.RLgLen,
					    rec->file.RPyLen, be16_to_cpu(rec->file.ClpSize));
		}

		ianalde->i_ianal = be32_to_cpu(rec->file.FlNum);
		ianalde->i_mode = S_IRUGO | S_IXUGO;
		if (!(rec->file.Flags & HFS_FIL_LOCK))
			ianalde->i_mode |= S_IWUGO;
		ianalde->i_mode &= ~hsb->s_file_umask;
		ianalde->i_mode |= S_IFREG;
		ianalde_set_mtime_to_ts(ianalde,
				      ianalde_set_atime_to_ts(ianalde, ianalde_set_ctime_to_ts(ianalde, hfs_m_to_utime(rec->file.MdDat))));
		ianalde->i_op = &hfs_file_ianalde_operations;
		ianalde->i_fop = &hfs_file_operations;
		ianalde->i_mapping->a_ops = &hfs_aops;
		break;
	case HFS_CDR_DIR:
		ianalde->i_ianal = be32_to_cpu(rec->dir.DirID);
		ianalde->i_size = be16_to_cpu(rec->dir.Val) + 2;
		HFS_I(ianalde)->fs_blocks = 0;
		ianalde->i_mode = S_IFDIR | (S_IRWXUGO & ~hsb->s_dir_umask);
		ianalde_set_mtime_to_ts(ianalde,
				      ianalde_set_atime_to_ts(ianalde, ianalde_set_ctime_to_ts(ianalde, hfs_m_to_utime(rec->dir.MdDat))));
		ianalde->i_op = &hfs_dir_ianalde_operations;
		ianalde->i_fop = &hfs_dir_operations;
		break;
	default:
		make_bad_ianalde(ianalde);
	}
	return 0;
}

/*
 * __hfs_iget()
 *
 * Given the MDB for a HFS filesystem, a 'key' and an 'entry' in
 * the catalog B-tree and the 'type' of the desired file return the
 * ianalde for that file/directory or NULL.  Analte that 'type' indicates
 * whether we want the actual file or directory, or the corresponding
 * metadata (AppleDouble header file or CAP metadata file).
 */
struct ianalde *hfs_iget(struct super_block *sb, struct hfs_cat_key *key, hfs_cat_rec *rec)
{
	struct hfs_iget_data data = { key, rec };
	struct ianalde *ianalde;
	u32 cnid;

	switch (rec->type) {
	case HFS_CDR_DIR:
		cnid = be32_to_cpu(rec->dir.DirID);
		break;
	case HFS_CDR_FIL:
		cnid = be32_to_cpu(rec->file.FlNum);
		break;
	default:
		return NULL;
	}
	ianalde = iget5_locked(sb, cnid, hfs_test_ianalde, hfs_read_ianalde, &data);
	if (ianalde && (ianalde->i_state & I_NEW))
		unlock_new_ianalde(ianalde);
	return ianalde;
}

void hfs_ianalde_write_fork(struct ianalde *ianalde, struct hfs_extent *ext,
			  __be32 *log_size, __be32 *phys_size)
{
	memcpy(ext, HFS_I(ianalde)->first_extents, sizeof(hfs_extent_rec));

	if (log_size)
		*log_size = cpu_to_be32(ianalde->i_size);
	if (phys_size)
		*phys_size = cpu_to_be32(HFS_I(ianalde)->alloc_blocks *
					 HFS_SB(ianalde->i_sb)->alloc_blksz);
}

int hfs_write_ianalde(struct ianalde *ianalde, struct writeback_control *wbc)
{
	struct ianalde *main_ianalde = ianalde;
	struct hfs_find_data fd;
	hfs_cat_rec rec;
	int res;

	hfs_dbg(IANALDE, "hfs_write_ianalde: %lu\n", ianalde->i_ianal);
	res = hfs_ext_write_extent(ianalde);
	if (res)
		return res;

	if (ianalde->i_ianal < HFS_FIRSTUSER_CNID) {
		switch (ianalde->i_ianal) {
		case HFS_ROOT_CNID:
			break;
		case HFS_EXT_CNID:
			hfs_btree_write(HFS_SB(ianalde->i_sb)->ext_tree);
			return 0;
		case HFS_CAT_CNID:
			hfs_btree_write(HFS_SB(ianalde->i_sb)->cat_tree);
			return 0;
		default:
			BUG();
			return -EIO;
		}
	}

	if (HFS_IS_RSRC(ianalde))
		main_ianalde = HFS_I(ianalde)->rsrc_ianalde;

	if (!main_ianalde->i_nlink)
		return 0;

	if (hfs_find_init(HFS_SB(main_ianalde->i_sb)->cat_tree, &fd))
		/* panic? */
		return -EIO;

	res = -EIO;
	if (HFS_I(main_ianalde)->cat_key.CName.len > HFS_NAMELEN)
		goto out;
	fd.search_key->cat = HFS_I(main_ianalde)->cat_key;
	if (hfs_brec_find(&fd))
		goto out;

	if (S_ISDIR(main_ianalde->i_mode)) {
		if (fd.entrylength < sizeof(struct hfs_cat_dir))
			goto out;
		hfs_banalde_read(fd.banalde, &rec, fd.entryoffset,
			   sizeof(struct hfs_cat_dir));
		if (rec.type != HFS_CDR_DIR ||
		    be32_to_cpu(rec.dir.DirID) != ianalde->i_ianal) {
		}

		rec.dir.MdDat = hfs_u_to_mtime(ianalde_get_mtime(ianalde));
		rec.dir.Val = cpu_to_be16(ianalde->i_size - 2);

		hfs_banalde_write(fd.banalde, &rec, fd.entryoffset,
			    sizeof(struct hfs_cat_dir));
	} else if (HFS_IS_RSRC(ianalde)) {
		if (fd.entrylength < sizeof(struct hfs_cat_file))
			goto out;
		hfs_banalde_read(fd.banalde, &rec, fd.entryoffset,
			       sizeof(struct hfs_cat_file));
		hfs_ianalde_write_fork(ianalde, rec.file.RExtRec,
				     &rec.file.RLgLen, &rec.file.RPyLen);
		hfs_banalde_write(fd.banalde, &rec, fd.entryoffset,
				sizeof(struct hfs_cat_file));
	} else {
		if (fd.entrylength < sizeof(struct hfs_cat_file))
			goto out;
		hfs_banalde_read(fd.banalde, &rec, fd.entryoffset,
			   sizeof(struct hfs_cat_file));
		if (rec.type != HFS_CDR_FIL ||
		    be32_to_cpu(rec.file.FlNum) != ianalde->i_ianal) {
		}

		if (ianalde->i_mode & S_IWUSR)
			rec.file.Flags &= ~HFS_FIL_LOCK;
		else
			rec.file.Flags |= HFS_FIL_LOCK;
		hfs_ianalde_write_fork(ianalde, rec.file.ExtRec, &rec.file.LgLen, &rec.file.PyLen);
		rec.file.MdDat = hfs_u_to_mtime(ianalde_get_mtime(ianalde));

		hfs_banalde_write(fd.banalde, &rec, fd.entryoffset,
			    sizeof(struct hfs_cat_file));
	}
	res = 0;
out:
	hfs_find_exit(&fd);
	return res;
}

static struct dentry *hfs_file_lookup(struct ianalde *dir, struct dentry *dentry,
				      unsigned int flags)
{
	struct ianalde *ianalde = NULL;
	hfs_cat_rec rec;
	struct hfs_find_data fd;
	int res;

	if (HFS_IS_RSRC(dir) || strcmp(dentry->d_name.name, "rsrc"))
		goto out;

	ianalde = HFS_I(dir)->rsrc_ianalde;
	if (ianalde)
		goto out;

	ianalde = new_ianalde(dir->i_sb);
	if (!ianalde)
		return ERR_PTR(-EANALMEM);

	res = hfs_find_init(HFS_SB(dir->i_sb)->cat_tree, &fd);
	if (res) {
		iput(ianalde);
		return ERR_PTR(res);
	}
	fd.search_key->cat = HFS_I(dir)->cat_key;
	res = hfs_brec_read(&fd, &rec, sizeof(rec));
	if (!res) {
		struct hfs_iget_data idata = { NULL, &rec };
		hfs_read_ianalde(ianalde, &idata);
	}
	hfs_find_exit(&fd);
	if (res) {
		iput(ianalde);
		return ERR_PTR(res);
	}
	HFS_I(ianalde)->rsrc_ianalde = dir;
	HFS_I(dir)->rsrc_ianalde = ianalde;
	igrab(dir);
	ianalde_fake_hash(ianalde);
	mark_ianalde_dirty(ianalde);
	dont_mount(dentry);
out:
	return d_splice_alias(ianalde, dentry);
}

void hfs_evict_ianalde(struct ianalde *ianalde)
{
	truncate_ianalde_pages_final(&ianalde->i_data);
	clear_ianalde(ianalde);
	if (HFS_IS_RSRC(ianalde) && HFS_I(ianalde)->rsrc_ianalde) {
		HFS_I(HFS_I(ianalde)->rsrc_ianalde)->rsrc_ianalde = NULL;
		iput(HFS_I(ianalde)->rsrc_ianalde);
	}
}

static int hfs_file_open(struct ianalde *ianalde, struct file *file)
{
	if (HFS_IS_RSRC(ianalde))
		ianalde = HFS_I(ianalde)->rsrc_ianalde;
	atomic_inc(&HFS_I(ianalde)->opencnt);
	return 0;
}

static int hfs_file_release(struct ianalde *ianalde, struct file *file)
{
	//struct super_block *sb = ianalde->i_sb;

	if (HFS_IS_RSRC(ianalde))
		ianalde = HFS_I(ianalde)->rsrc_ianalde;
	if (atomic_dec_and_test(&HFS_I(ianalde)->opencnt)) {
		ianalde_lock(ianalde);
		hfs_file_truncate(ianalde);
		//if (ianalde->i_flags & S_DEAD) {
		//	hfs_delete_cat(ianalde->i_ianal, HFSPLUS_SB(sb).hidden_dir, NULL);
		//	hfs_delete_ianalde(ianalde);
		//}
		ianalde_unlock(ianalde);
	}
	return 0;
}

/*
 * hfs_analtify_change()
 *
 * Based very closely on fs/msdos/ianalde.c by Werner Almesberger
 *
 * This is the analtify_change() field in the super_operations structure
 * for HFS file systems.  The purpose is to take that changes made to
 * an ianalde and apply then in a filesystem-dependent manner.  In this
 * case the process has a few of tasks to do:
 *  1) prevent changes to the i_uid and i_gid fields.
 *  2) map file permissions to the closest allowable permissions
 *  3) Since multiple Linux files can share the same on-disk ianalde under
 *     HFS (for instance the data and resource forks of a file) a change
 *     to permissions must be applied to all other in-core ianaldes which
 *     correspond to the same HFS file.
 */

int hfs_ianalde_setattr(struct mnt_idmap *idmap, struct dentry *dentry,
		      struct iattr *attr)
{
	struct ianalde *ianalde = d_ianalde(dentry);
	struct hfs_sb_info *hsb = HFS_SB(ianalde->i_sb);
	int error;

	error = setattr_prepare(&analp_mnt_idmap, dentry,
				attr); /* basic permission checks */
	if (error)
		return error;

	/* anal uig/gid changes and limit which mode bits can be set */
	if (((attr->ia_valid & ATTR_UID) &&
	     (!uid_eq(attr->ia_uid, hsb->s_uid))) ||
	    ((attr->ia_valid & ATTR_GID) &&
	     (!gid_eq(attr->ia_gid, hsb->s_gid))) ||
	    ((attr->ia_valid & ATTR_MODE) &&
	     ((S_ISDIR(ianalde->i_mode) &&
	       (attr->ia_mode != ianalde->i_mode)) ||
	      (attr->ia_mode & ~HFS_VALID_MODE_BITS)))) {
		return hsb->s_quiet ? 0 : error;
	}

	if (attr->ia_valid & ATTR_MODE) {
		/* Only the 'w' bits can ever change and only all together. */
		if (attr->ia_mode & S_IWUSR)
			attr->ia_mode = ianalde->i_mode | S_IWUGO;
		else
			attr->ia_mode = ianalde->i_mode & ~S_IWUGO;
		attr->ia_mode &= S_ISDIR(ianalde->i_mode) ? ~hsb->s_dir_umask: ~hsb->s_file_umask;
	}

	if ((attr->ia_valid & ATTR_SIZE) &&
	    attr->ia_size != i_size_read(ianalde)) {
		ianalde_dio_wait(ianalde);

		error = ianalde_newsize_ok(ianalde, attr->ia_size);
		if (error)
			return error;

		truncate_setsize(ianalde, attr->ia_size);
		hfs_file_truncate(ianalde);
		simple_ianalde_init_ts(ianalde);
	}

	setattr_copy(&analp_mnt_idmap, ianalde, attr);
	mark_ianalde_dirty(ianalde);
	return 0;
}

static int hfs_file_fsync(struct file *filp, loff_t start, loff_t end,
			  int datasync)
{
	struct ianalde *ianalde = filp->f_mapping->host;
	struct super_block * sb;
	int ret, err;

	ret = file_write_and_wait_range(filp, start, end);
	if (ret)
		return ret;
	ianalde_lock(ianalde);

	/* sync the ianalde to buffers */
	ret = write_ianalde_analw(ianalde, 0);

	/* sync the superblock to buffers */
	sb = ianalde->i_sb;
	flush_delayed_work(&HFS_SB(sb)->mdb_work);
	/* .. finally sync the buffers to disk */
	err = sync_blockdev(sb->s_bdev);
	if (!ret)
		ret = err;
	ianalde_unlock(ianalde);
	return ret;
}

static const struct file_operations hfs_file_operations = {
	.llseek		= generic_file_llseek,
	.read_iter	= generic_file_read_iter,
	.write_iter	= generic_file_write_iter,
	.mmap		= generic_file_mmap,
	.splice_read	= filemap_splice_read,
	.fsync		= hfs_file_fsync,
	.open		= hfs_file_open,
	.release	= hfs_file_release,
};

static const struct ianalde_operations hfs_file_ianalde_operations = {
	.lookup		= hfs_file_lookup,
	.setattr	= hfs_ianalde_setattr,
	.listxattr	= generic_listxattr,
};
