/*
 *  linux/fs/hfs/inode.c
 *
 * Copyright (C) 1995-1997  Paul H. Hargrove
 * (C) 2003 Ardis Technologies <roman@ardistech.com>
 * This file may be distributed under the terms of the GNU General Public License.
 *
 * This file contains inode-related functions which do not depend on
 * which scheme is being used to represent forks.
 *
 * Based on the minix file system code, (C) 1991, 1992 by Linus Torvalds
 */

#include <linux/pagemap.h>
#include <linux/mpage.h>
#include <linux/sched.h>
#include <linux/uio.h>
#include <linux/xattr.h>

#include "hfs_fs.h"
#include "btree.h"

static const struct file_operations hfs_file_operations;
static const struct inode_operations hfs_file_inode_operations;

/*================ Variable-like macros ================*/

#define HFS_VALID_MODE_BITS  (S_IFREG | S_IFDIR | S_IRWXUGO)

static int hfs_writepage(struct page *page, struct writeback_control *wbc)
{
	return block_write_full_page(page, hfs_get_block, wbc);
}

static int hfs_readpage(struct file *file, struct page *page)
{
	return block_read_full_page(page, hfs_get_block);
}

static void hfs_write_failed(struct address_space *mapping, loff_t to)
{
	struct inode *inode = mapping->host;

	if (to > inode->i_size) {
		truncate_pagecache(inode, inode->i_size);
		hfs_file_truncate(inode);
	}
}

static int hfs_write_begin(struct file *file, struct address_space *mapping,
			loff_t pos, unsigned len, unsigned flags,
			struct page **pagep, void **fsdata)
{
	int ret;

	*pagep = NULL;
	ret = cont_write_begin(file, mapping, pos, len, flags, pagep, fsdata,
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

static int hfs_releasepage(struct page *page, gfp_t mask)
{
	struct inode *inode = page->mapping->host;
	struct super_block *sb = inode->i_sb;
	struct hfs_btree *tree;
	struct hfs_bnode *node;
	u32 nidx;
	int i, res = 1;

	switch (inode->i_ino) {
	case HFS_EXT_CNID:
		tree = HFS_SB(sb)->ext_tree;
		break;
	case HFS_CAT_CNID:
		tree = HFS_SB(sb)->cat_tree;
		break;
	default:
		BUG();
		return 0;
	}

	if (!tree)
		return 0;

	if (tree->node_size >= PAGE_SIZE) {
		nidx = page->index >> (tree->node_size_shift - PAGE_SHIFT);
		spin_lock(&tree->hash_lock);
		node = hfs_bnode_findhash(tree, nidx);
		if (!node)
			;
		else if (atomic_read(&node->refcnt))
			res = 0;
		if (res && node) {
			hfs_bnode_unhash(node);
			hfs_bnode_free(node);
		}
		spin_unlock(&tree->hash_lock);
	} else {
		nidx = page->index << (PAGE_SHIFT - tree->node_size_shift);
		i = 1 << (PAGE_SHIFT - tree->node_size_shift);
		spin_lock(&tree->hash_lock);
		do {
			node = hfs_bnode_findhash(tree, nidx++);
			if (!node)
				continue;
			if (atomic_read(&node->refcnt)) {
				res = 0;
				break;
			}
			hfs_bnode_unhash(node);
			hfs_bnode_free(node);
		} while (--i && nidx < tree->node_count);
		spin_unlock(&tree->hash_lock);
	}
	return res ? try_to_free_buffers(page) : 0;
}

static ssize_t hfs_direct_IO(struct kiocb *iocb, struct iov_iter *iter)
{
	struct file *file = iocb->ki_filp;
	struct address_space *mapping = file->f_mapping;
	struct inode *inode = mapping->host;
	size_t count = iov_iter_count(iter);
	ssize_t ret;

	ret = blockdev_direct_IO(iocb, inode, iter, hfs_get_block);

	/*
	 * In case of error extending write may have instantiated a few
	 * blocks outside i_size. Trim these off again.
	 */
	if (unlikely(iov_iter_rw(iter) == WRITE && ret < 0)) {
		loff_t isize = i_size_read(inode);
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
	.readpage	= hfs_readpage,
	.writepage	= hfs_writepage,
	.write_begin	= hfs_write_begin,
	.write_end	= generic_write_end,
	.bmap		= hfs_bmap,
	.releasepage	= hfs_releasepage,
};

const struct address_space_operations hfs_aops = {
	.readpage	= hfs_readpage,
	.writepage	= hfs_writepage,
	.write_begin	= hfs_write_begin,
	.write_end	= generic_write_end,
	.bmap		= hfs_bmap,
	.direct_IO	= hfs_direct_IO,
	.writepages	= hfs_writepages,
};

/*
 * hfs_new_inode
 */
struct inode *hfs_new_inode(struct inode *dir, const struct qstr *name, umode_t mode)
{
	struct super_block *sb = dir->i_sb;
	struct inode *inode = new_inode(sb);
	if (!inode)
		return NULL;

	mutex_init(&HFS_I(inode)->extents_lock);
	INIT_LIST_HEAD(&HFS_I(inode)->open_dir_list);
	spin_lock_init(&HFS_I(inode)->open_dir_lock);
	hfs_cat_build_key(sb, (btree_key *)&HFS_I(inode)->cat_key, dir->i_ino, name);
	inode->i_ino = HFS_SB(sb)->next_id++;
	inode->i_mode = mode;
	inode->i_uid = current_fsuid();
	inode->i_gid = current_fsgid();
	set_nlink(inode, 1);
	inode->i_mtime = inode->i_atime = inode->i_ctime = current_time(inode);
	HFS_I(inode)->flags = 0;
	HFS_I(inode)->rsrc_inode = NULL;
	HFS_I(inode)->fs_blocks = 0;
	if (S_ISDIR(mode)) {
		inode->i_size = 2;
		HFS_SB(sb)->folder_count++;
		if (dir->i_ino == HFS_ROOT_CNID)
			HFS_SB(sb)->root_dirs++;
		inode->i_op = &hfs_dir_inode_operations;
		inode->i_fop = &hfs_dir_operations;
		inode->i_mode |= S_IRWXUGO;
		inode->i_mode &= ~HFS_SB(inode->i_sb)->s_dir_umask;
	} else if (S_ISREG(mode)) {
		HFS_I(inode)->clump_blocks = HFS_SB(sb)->clumpablks;
		HFS_SB(sb)->file_count++;
		if (dir->i_ino == HFS_ROOT_CNID)
			HFS_SB(sb)->root_files++;
		inode->i_op = &hfs_file_inode_operations;
		inode->i_fop = &hfs_file_operations;
		inode->i_mapping->a_ops = &hfs_aops;
		inode->i_mode |= S_IRUGO|S_IXUGO;
		if (mode & S_IWUSR)
			inode->i_mode |= S_IWUGO;
		inode->i_mode &= ~HFS_SB(inode->i_sb)->s_file_umask;
		HFS_I(inode)->phys_size = 0;
		HFS_I(inode)->alloc_blocks = 0;
		HFS_I(inode)->first_blocks = 0;
		HFS_I(inode)->cached_start = 0;
		HFS_I(inode)->cached_blocks = 0;
		memset(HFS_I(inode)->first_extents, 0, sizeof(hfs_extent_rec));
		memset(HFS_I(inode)->cached_extents, 0, sizeof(hfs_extent_rec));
	}
	insert_inode_hash(inode);
	mark_inode_dirty(inode);
	set_bit(HFS_FLG_MDB_DIRTY, &HFS_SB(sb)->flags);
	hfs_mark_mdb_dirty(sb);

	return inode;
}

void hfs_delete_inode(struct inode *inode)
{
	struct super_block *sb = inode->i_sb;

	hfs_dbg(INODE, "delete_inode: %lu\n", inode->i_ino);
	if (S_ISDIR(inode->i_mode)) {
		HFS_SB(sb)->folder_count--;
		if (HFS_I(inode)->cat_key.ParID == cpu_to_be32(HFS_ROOT_CNID))
			HFS_SB(sb)->root_dirs--;
		set_bit(HFS_FLG_MDB_DIRTY, &HFS_SB(sb)->flags);
		hfs_mark_mdb_dirty(sb);
		return;
	}
	HFS_SB(sb)->file_count--;
	if (HFS_I(inode)->cat_key.ParID == cpu_to_be32(HFS_ROOT_CNID))
		HFS_SB(sb)->root_files--;
	if (S_ISREG(inode->i_mode)) {
		if (!inode->i_nlink) {
			inode->i_size = 0;
			hfs_file_truncate(inode);
		}
	}
	set_bit(HFS_FLG_MDB_DIRTY, &HFS_SB(sb)->flags);
	hfs_mark_mdb_dirty(sb);
}

void hfs_inode_read_fork(struct inode *inode, struct hfs_extent *ext,
			 __be32 __log_size, __be32 phys_size, u32 clump_size)
{
	struct super_block *sb = inode->i_sb;
	u32 log_size = be32_to_cpu(__log_size);
	u16 count;
	int i;

	memcpy(HFS_I(inode)->first_extents, ext, sizeof(hfs_extent_rec));
	for (count = 0, i = 0; i < 3; i++)
		count += be16_to_cpu(ext[i].count);
	HFS_I(inode)->first_blocks = count;

	inode->i_size = HFS_I(inode)->phys_size = log_size;
	HFS_I(inode)->fs_blocks = (log_size + sb->s_blocksize - 1) >> sb->s_blocksize_bits;
	inode_set_bytes(inode, HFS_I(inode)->fs_blocks << sb->s_blocksize_bits);
	HFS_I(inode)->alloc_blocks = be32_to_cpu(phys_size) /
				     HFS_SB(sb)->alloc_blksz;
	HFS_I(inode)->clump_blocks = clump_size / HFS_SB(sb)->alloc_blksz;
	if (!HFS_I(inode)->clump_blocks)
		HFS_I(inode)->clump_blocks = HFS_SB(sb)->clumpablks;
}

struct hfs_iget_data {
	struct hfs_cat_key *key;
	hfs_cat_rec *rec;
};

static int hfs_test_inode(struct inode *inode, void *data)
{
	struct hfs_iget_data *idata = data;
	hfs_cat_rec *rec;

	rec = idata->rec;
	switch (rec->type) {
	case HFS_CDR_DIR:
		return inode->i_ino == be32_to_cpu(rec->dir.DirID);
	case HFS_CDR_FIL:
		return inode->i_ino == be32_to_cpu(rec->file.FlNum);
	default:
		BUG();
		return 1;
	}
}

/*
 * hfs_read_inode
 */
static int hfs_read_inode(struct inode *inode, void *data)
{
	struct hfs_iget_data *idata = data;
	struct hfs_sb_info *hsb = HFS_SB(inode->i_sb);
	hfs_cat_rec *rec;

	HFS_I(inode)->flags = 0;
	HFS_I(inode)->rsrc_inode = NULL;
	mutex_init(&HFS_I(inode)->extents_lock);
	INIT_LIST_HEAD(&HFS_I(inode)->open_dir_list);
	spin_lock_init(&HFS_I(inode)->open_dir_lock);

	/* Initialize the inode */
	inode->i_uid = hsb->s_uid;
	inode->i_gid = hsb->s_gid;
	set_nlink(inode, 1);

	if (idata->key)
		HFS_I(inode)->cat_key = *idata->key;
	else
		HFS_I(inode)->flags |= HFS_FLG_RSRC;
	HFS_I(inode)->tz_secondswest = sys_tz.tz_minuteswest * 60;

	rec = idata->rec;
	switch (rec->type) {
	case HFS_CDR_FIL:
		if (!HFS_IS_RSRC(inode)) {
			hfs_inode_read_fork(inode, rec->file.ExtRec, rec->file.LgLen,
					    rec->file.PyLen, be16_to_cpu(rec->file.ClpSize));
		} else {
			hfs_inode_read_fork(inode, rec->file.RExtRec, rec->file.RLgLen,
					    rec->file.RPyLen, be16_to_cpu(rec->file.ClpSize));
		}

		inode->i_ino = be32_to_cpu(rec->file.FlNum);
		inode->i_mode = S_IRUGO | S_IXUGO;
		if (!(rec->file.Flags & HFS_FIL_LOCK))
			inode->i_mode |= S_IWUGO;
		inode->i_mode &= ~hsb->s_file_umask;
		inode->i_mode |= S_IFREG;
		inode->i_ctime = inode->i_atime = inode->i_mtime =
				hfs_m_to_utime(rec->file.MdDat);
		inode->i_op = &hfs_file_inode_operations;
		inode->i_fop = &hfs_file_operations;
		inode->i_mapping->a_ops = &hfs_aops;
		break;
	case HFS_CDR_DIR:
		inode->i_ino = be32_to_cpu(rec->dir.DirID);
		inode->i_size = be16_to_cpu(rec->dir.Val) + 2;
		HFS_I(inode)->fs_blocks = 0;
		inode->i_mode = S_IFDIR | (S_IRWXUGO & ~hsb->s_dir_umask);
		inode->i_ctime = inode->i_atime = inode->i_mtime =
				hfs_m_to_utime(rec->dir.MdDat);
		inode->i_op = &hfs_dir_inode_operations;
		inode->i_fop = &hfs_dir_operations;
		break;
	default:
		make_bad_inode(inode);
	}
	return 0;
}

/*
 * __hfs_iget()
 *
 * Given the MDB for a HFS filesystem, a 'key' and an 'entry' in
 * the catalog B-tree and the 'type' of the desired file return the
 * inode for that file/directory or NULL.  Note that 'type' indicates
 * whether we want the actual file or directory, or the corresponding
 * metadata (AppleDouble header file or CAP metadata file).
 */
struct inode *hfs_iget(struct super_block *sb, struct hfs_cat_key *key, hfs_cat_rec *rec)
{
	struct hfs_iget_data data = { key, rec };
	struct inode *inode;
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
	inode = iget5_locked(sb, cnid, hfs_test_inode, hfs_read_inode, &data);
	if (inode && (inode->i_state & I_NEW))
		unlock_new_inode(inode);
	return inode;
}

void hfs_inode_write_fork(struct inode *inode, struct hfs_extent *ext,
			  __be32 *log_size, __be32 *phys_size)
{
	memcpy(ext, HFS_I(inode)->first_extents, sizeof(hfs_extent_rec));

	if (log_size)
		*log_size = cpu_to_be32(inode->i_size);
	if (phys_size)
		*phys_size = cpu_to_be32(HFS_I(inode)->alloc_blocks *
					 HFS_SB(inode->i_sb)->alloc_blksz);
}

int hfs_write_inode(struct inode *inode, struct writeback_control *wbc)
{
	struct inode *main_inode = inode;
	struct hfs_find_data fd;
	hfs_cat_rec rec;
	int res;

	hfs_dbg(INODE, "hfs_write_inode: %lu\n", inode->i_ino);
	res = hfs_ext_write_extent(inode);
	if (res)
		return res;

	if (inode->i_ino < HFS_FIRSTUSER_CNID) {
		switch (inode->i_ino) {
		case HFS_ROOT_CNID:
			break;
		case HFS_EXT_CNID:
			hfs_btree_write(HFS_SB(inode->i_sb)->ext_tree);
			return 0;
		case HFS_CAT_CNID:
			hfs_btree_write(HFS_SB(inode->i_sb)->cat_tree);
			return 0;
		default:
			BUG();
			return -EIO;
		}
	}

	if (HFS_IS_RSRC(inode))
		main_inode = HFS_I(inode)->rsrc_inode;

	if (!main_inode->i_nlink)
		return 0;

	if (hfs_find_init(HFS_SB(main_inode->i_sb)->cat_tree, &fd))
		/* panic? */
		return -EIO;

	fd.search_key->cat = HFS_I(main_inode)->cat_key;
	if (hfs_brec_find(&fd))
		/* panic? */
		goto out;

	if (S_ISDIR(main_inode->i_mode)) {
		if (fd.entrylength < sizeof(struct hfs_cat_dir))
			/* panic? */;
		hfs_bnode_read(fd.bnode, &rec, fd.entryoffset,
			   sizeof(struct hfs_cat_dir));
		if (rec.type != HFS_CDR_DIR ||
		    be32_to_cpu(rec.dir.DirID) != inode->i_ino) {
		}

		rec.dir.MdDat = hfs_u_to_mtime(inode->i_mtime);
		rec.dir.Val = cpu_to_be16(inode->i_size - 2);

		hfs_bnode_write(fd.bnode, &rec, fd.entryoffset,
			    sizeof(struct hfs_cat_dir));
	} else if (HFS_IS_RSRC(inode)) {
		hfs_bnode_read(fd.bnode, &rec, fd.entryoffset,
			       sizeof(struct hfs_cat_file));
		hfs_inode_write_fork(inode, rec.file.RExtRec,
				     &rec.file.RLgLen, &rec.file.RPyLen);
		hfs_bnode_write(fd.bnode, &rec, fd.entryoffset,
				sizeof(struct hfs_cat_file));
	} else {
		if (fd.entrylength < sizeof(struct hfs_cat_file))
			/* panic? */;
		hfs_bnode_read(fd.bnode, &rec, fd.entryoffset,
			   sizeof(struct hfs_cat_file));
		if (rec.type != HFS_CDR_FIL ||
		    be32_to_cpu(rec.file.FlNum) != inode->i_ino) {
		}

		if (inode->i_mode & S_IWUSR)
			rec.file.Flags &= ~HFS_FIL_LOCK;
		else
			rec.file.Flags |= HFS_FIL_LOCK;
		hfs_inode_write_fork(inode, rec.file.ExtRec, &rec.file.LgLen, &rec.file.PyLen);
		rec.file.MdDat = hfs_u_to_mtime(inode->i_mtime);

		hfs_bnode_write(fd.bnode, &rec, fd.entryoffset,
			    sizeof(struct hfs_cat_file));
	}
out:
	hfs_find_exit(&fd);
	return 0;
}

static struct dentry *hfs_file_lookup(struct inode *dir, struct dentry *dentry,
				      unsigned int flags)
{
	struct inode *inode = NULL;
	hfs_cat_rec rec;
	struct hfs_find_data fd;
	int res;

	if (HFS_IS_RSRC(dir) || strcmp(dentry->d_name.name, "rsrc"))
		goto out;

	inode = HFS_I(dir)->rsrc_inode;
	if (inode)
		goto out;

	inode = new_inode(dir->i_sb);
	if (!inode)
		return ERR_PTR(-ENOMEM);

	res = hfs_find_init(HFS_SB(dir->i_sb)->cat_tree, &fd);
	if (res) {
		iput(inode);
		return ERR_PTR(res);
	}
	fd.search_key->cat = HFS_I(dir)->cat_key;
	res = hfs_brec_read(&fd, &rec, sizeof(rec));
	if (!res) {
		struct hfs_iget_data idata = { NULL, &rec };
		hfs_read_inode(inode, &idata);
	}
	hfs_find_exit(&fd);
	if (res) {
		iput(inode);
		return ERR_PTR(res);
	}
	HFS_I(inode)->rsrc_inode = dir;
	HFS_I(dir)->rsrc_inode = inode;
	igrab(dir);
	hlist_add_fake(&inode->i_hash);
	mark_inode_dirty(inode);
out:
	d_add(dentry, inode);
	return NULL;
}

void hfs_evict_inode(struct inode *inode)
{
	truncate_inode_pages_final(&inode->i_data);
	clear_inode(inode);
	if (HFS_IS_RSRC(inode) && HFS_I(inode)->rsrc_inode) {
		HFS_I(HFS_I(inode)->rsrc_inode)->rsrc_inode = NULL;
		iput(HFS_I(inode)->rsrc_inode);
	}
}

static int hfs_file_open(struct inode *inode, struct file *file)
{
	if (HFS_IS_RSRC(inode))
		inode = HFS_I(inode)->rsrc_inode;
	atomic_inc(&HFS_I(inode)->opencnt);
	return 0;
}

static int hfs_file_release(struct inode *inode, struct file *file)
{
	//struct super_block *sb = inode->i_sb;

	if (HFS_IS_RSRC(inode))
		inode = HFS_I(inode)->rsrc_inode;
	if (atomic_dec_and_test(&HFS_I(inode)->opencnt)) {
		inode_lock(inode);
		hfs_file_truncate(inode);
		//if (inode->i_flags & S_DEAD) {
		//	hfs_delete_cat(inode->i_ino, HFSPLUS_SB(sb).hidden_dir, NULL);
		//	hfs_delete_inode(inode);
		//}
		inode_unlock(inode);
	}
	return 0;
}

/*
 * hfs_notify_change()
 *
 * Based very closely on fs/msdos/inode.c by Werner Almesberger
 *
 * This is the notify_change() field in the super_operations structure
 * for HFS file systems.  The purpose is to take that changes made to
 * an inode and apply then in a filesystem-dependent manner.  In this
 * case the process has a few of tasks to do:
 *  1) prevent changes to the i_uid and i_gid fields.
 *  2) map file permissions to the closest allowable permissions
 *  3) Since multiple Linux files can share the same on-disk inode under
 *     HFS (for instance the data and resource forks of a file) a change
 *     to permissions must be applied to all other in-core inodes which
 *     correspond to the same HFS file.
 */

int hfs_inode_setattr(struct dentry *dentry, struct iattr * attr)
{
	struct inode *inode = d_inode(dentry);
	struct hfs_sb_info *hsb = HFS_SB(inode->i_sb);
	int error;

	error = setattr_prepare(dentry, attr); /* basic permission checks */
	if (error)
		return error;

	/* no uig/gid changes and limit which mode bits can be set */
	if (((attr->ia_valid & ATTR_UID) &&
	     (!uid_eq(attr->ia_uid, hsb->s_uid))) ||
	    ((attr->ia_valid & ATTR_GID) &&
	     (!gid_eq(attr->ia_gid, hsb->s_gid))) ||
	    ((attr->ia_valid & ATTR_MODE) &&
	     ((S_ISDIR(inode->i_mode) &&
	       (attr->ia_mode != inode->i_mode)) ||
	      (attr->ia_mode & ~HFS_VALID_MODE_BITS)))) {
		return hsb->s_quiet ? 0 : error;
	}

	if (attr->ia_valid & ATTR_MODE) {
		/* Only the 'w' bits can ever change and only all together. */
		if (attr->ia_mode & S_IWUSR)
			attr->ia_mode = inode->i_mode | S_IWUGO;
		else
			attr->ia_mode = inode->i_mode & ~S_IWUGO;
		attr->ia_mode &= S_ISDIR(inode->i_mode) ? ~hsb->s_dir_umask: ~hsb->s_file_umask;
	}

	if ((attr->ia_valid & ATTR_SIZE) &&
	    attr->ia_size != i_size_read(inode)) {
		inode_dio_wait(inode);

		error = inode_newsize_ok(inode, attr->ia_size);
		if (error)
			return error;

		truncate_setsize(inode, attr->ia_size);
		hfs_file_truncate(inode);
	}

	setattr_copy(inode, attr);
	mark_inode_dirty(inode);
	return 0;
}

static int hfs_file_fsync(struct file *filp, loff_t start, loff_t end,
			  int datasync)
{
	struct inode *inode = filp->f_mapping->host;
	struct super_block * sb;
	int ret, err;

	ret = filemap_write_and_wait_range(inode->i_mapping, start, end);
	if (ret)
		return ret;
	inode_lock(inode);

	/* sync the inode to buffers */
	ret = write_inode_now(inode, 0);

	/* sync the superblock to buffers */
	sb = inode->i_sb;
	flush_delayed_work(&HFS_SB(sb)->mdb_work);
	/* .. finally sync the buffers to disk */
	err = sync_blockdev(sb->s_bdev);
	if (!ret)
		ret = err;
	inode_unlock(inode);
	return ret;
}

static const struct file_operations hfs_file_operations = {
	.llseek		= generic_file_llseek,
	.read_iter	= generic_file_read_iter,
	.write_iter	= generic_file_write_iter,
	.mmap		= generic_file_mmap,
	.splice_read	= generic_file_splice_read,
	.fsync		= hfs_file_fsync,
	.open		= hfs_file_open,
	.release	= hfs_file_release,
};

static const struct inode_operations hfs_file_inode_operations = {
	.lookup		= hfs_file_lookup,
	.setattr	= hfs_inode_setattr,
	.listxattr	= generic_listxattr,
};
