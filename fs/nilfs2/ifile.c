// SPDX-License-Identifier: GPL-2.0+
/*
 * NILFS inode file
 *
 * Copyright (C) 2006-2008 Nippon Telegraph and Telephone Corporation.
 *
 * Written by Amagai Yoshiji.
 * Revised by Ryusuke Konishi.
 *
 */

#include <linux/types.h>
#include <linux/buffer_head.h>
#include "nilfs.h"
#include "mdt.h"
#include "alloc.h"
#include "ifile.h"
#include "cpfile.h"

/**
 * struct nilfs_ifile_info - on-memory private data of ifile
 * @mi: on-memory private data of metadata file
 * @palloc_cache: persistent object allocator cache of ifile
 */
struct nilfs_ifile_info {
	struct nilfs_mdt_info mi;
	struct nilfs_palloc_cache palloc_cache;
};

static inline struct nilfs_ifile_info *NILFS_IFILE_I(struct inode *ifile)
{
	return (struct nilfs_ifile_info *)NILFS_MDT(ifile);
}

/**
 * nilfs_ifile_create_inode - create a new disk inode
 * @ifile: ifile inode
 * @out_ino: pointer to a variable to store inode number
 * @out_bh: buffer_head contains newly allocated disk inode
 *
 * nilfs_ifile_create_inode() allocates a new inode in the ifile metadata
 * file and stores the inode number in the variable pointed to by @out_ino,
 * as well as storing the ifile's buffer with the disk inode in the location
 * pointed to by @out_bh.
 *
 * Return: 0 on success, or one of the following negative error codes on
 * failure:
 * * %-EIO	- I/O error (including metadata corruption).
 * * %-ENOMEM	- Insufficient memory available.
 * * %-ENOSPC	- No inode left.
 */
int nilfs_ifile_create_inode(struct inode *ifile, ino_t *out_ino,
			     struct buffer_head **out_bh)
{
	struct nilfs_palloc_req req;
	int ret;

	req.pr_entry_nr = NILFS_FIRST_INO(ifile->i_sb);
	req.pr_entry_bh = NULL;

	ret = nilfs_palloc_prepare_alloc_entry(ifile, &req, false);
	if (!ret) {
		ret = nilfs_palloc_get_entry_block(ifile, req.pr_entry_nr, 1,
						   &req.pr_entry_bh);
		if (ret < 0)
			nilfs_palloc_abort_alloc_entry(ifile, &req);
	}
	if (ret < 0) {
		brelse(req.pr_entry_bh);
		return ret;
	}
	nilfs_palloc_commit_alloc_entry(ifile, &req);
	mark_buffer_dirty(req.pr_entry_bh);
	nilfs_mdt_mark_dirty(ifile);
	*out_ino = (ino_t)req.pr_entry_nr;
	*out_bh = req.pr_entry_bh;
	return 0;
}

/**
 * nilfs_ifile_delete_inode - delete a disk inode
 * @ifile: ifile inode
 * @ino: inode number
 *
 * Return: 0 on success, or one of the following negative error codes on
 * failure:
 * * %-EIO	- I/O error (including metadata corruption).
 * * %-ENOENT	- Inode number unallocated.
 * * %-ENOMEM	- Insufficient memory available.
 */
int nilfs_ifile_delete_inode(struct inode *ifile, ino_t ino)
{
	struct nilfs_palloc_req req = {
		.pr_entry_nr = ino, .pr_entry_bh = NULL
	};
	struct nilfs_inode *raw_inode;
	size_t offset;
	int ret;

	ret = nilfs_palloc_prepare_free_entry(ifile, &req);
	if (!ret) {
		ret = nilfs_palloc_get_entry_block(ifile, req.pr_entry_nr, 0,
						   &req.pr_entry_bh);
		if (ret < 0)
			nilfs_palloc_abort_free_entry(ifile, &req);
	}
	if (ret < 0) {
		brelse(req.pr_entry_bh);
		return ret;
	}

	offset = nilfs_palloc_entry_offset(ifile, req.pr_entry_nr,
					   req.pr_entry_bh);
	raw_inode = kmap_local_folio(req.pr_entry_bh->b_folio, offset);
	raw_inode->i_flags = 0;
	kunmap_local(raw_inode);

	mark_buffer_dirty(req.pr_entry_bh);
	brelse(req.pr_entry_bh);

	nilfs_palloc_commit_free_entry(ifile, &req);

	return 0;
}

int nilfs_ifile_get_inode_block(struct inode *ifile, ino_t ino,
				struct buffer_head **out_bh)
{
	struct super_block *sb = ifile->i_sb;
	int err;

	if (unlikely(!NILFS_VALID_INODE(sb, ino))) {
		nilfs_error(sb, "bad inode number: %lu", (unsigned long)ino);
		return -EINVAL;
	}

	err = nilfs_palloc_get_entry_block(ifile, ino, 0, out_bh);
	if (unlikely(err))
		nilfs_warn(sb, "error %d reading inode: ino=%lu",
			   err, (unsigned long)ino);
	return err;
}

/**
 * nilfs_ifile_count_free_inodes - calculate free inodes count
 * @ifile: ifile inode
 * @nmaxinodes: current maximum of available inodes count [out]
 * @nfreeinodes: free inodes count [out]
 *
 * Return: 0 on success, or a negative error code on failure.
 */
int nilfs_ifile_count_free_inodes(struct inode *ifile,
				    u64 *nmaxinodes, u64 *nfreeinodes)
{
	u64 nused;
	int err;

	*nmaxinodes = 0;
	*nfreeinodes = 0;

	nused = atomic64_read(&NILFS_I(ifile)->i_root->inodes_count);
	err = nilfs_palloc_count_max_entries(ifile, nused, nmaxinodes);
	if (likely(!err))
		*nfreeinodes = *nmaxinodes - nused;
	return err;
}

/**
 * nilfs_ifile_read - read or get ifile inode
 * @sb: super block instance
 * @root: root object
 * @cno: number of checkpoint entry to read
 * @inode_size: size of an inode
 *
 * Return: 0 on success, or one of the following negative error codes on
 * failure:
 * * %-EINVAL	- Invalid checkpoint.
 * * %-ENOMEM	- Insufficient memory available.
 * * %-EIO	- I/O error (including metadata corruption).
 */
int nilfs_ifile_read(struct super_block *sb, struct nilfs_root *root,
		     __u64 cno, size_t inode_size)
{
	struct the_nilfs *nilfs;
	struct inode *ifile;
	int err;

	ifile = nilfs_iget_locked(sb, root, NILFS_IFILE_INO);
	if (unlikely(!ifile))
		return -ENOMEM;
	if (!(ifile->i_state & I_NEW))
		goto out;

	err = nilfs_mdt_init(ifile, NILFS_MDT_GFP,
			     sizeof(struct nilfs_ifile_info));
	if (err)
		goto failed;

	err = nilfs_palloc_init_blockgroup(ifile, inode_size);
	if (err)
		goto failed;

	nilfs_palloc_setup_cache(ifile, &NILFS_IFILE_I(ifile)->palloc_cache);

	nilfs = sb->s_fs_info;
	err = nilfs_cpfile_read_checkpoint(nilfs->ns_cpfile, cno, root, ifile);
	if (err)
		goto failed;

	unlock_new_inode(ifile);
 out:
	return 0;
 failed:
	iget_failed(ifile);
	return err;
}
