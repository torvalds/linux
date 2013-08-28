/*
 * ifile.c - NILFS inode file
 *
 * Copyright (C) 2006-2008 Nippon Telegraph and Telephone Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Written by Amagai Yoshiji <amagai@osrg.net>.
 * Revised by Ryusuke Konishi <ryusuke@osrg.net>.
 *
 */

#include <linux/types.h>
#include <linux/buffer_head.h>
#include "nilfs.h"
#include "mdt.h"
#include "alloc.h"
#include "ifile.h"

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
 * Return Value: On success, 0 is returned and the newly allocated inode
 * number is stored in the place pointed by @ino, and buffer_head pointer
 * that contains newly allocated disk inode structure is stored in the
 * place pointed by @out_bh
 * On error, one of the following negative error codes is returned.
 *
 * %-EIO - I/O error.
 *
 * %-ENOMEM - Insufficient amount of memory available.
 *
 * %-ENOSPC - No inode left.
 */
int nilfs_ifile_create_inode(struct inode *ifile, ino_t *out_ino,
			     struct buffer_head **out_bh)
{
	struct nilfs_palloc_req req;
	int ret;

	req.pr_entry_nr = 0;  /* 0 says find free inode from beginning of
				 a group. dull code!! */
	req.pr_entry_bh = NULL;

	ret = nilfs_palloc_prepare_alloc_entry(ifile, &req);
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
 * Return Value: On success, 0 is returned. On error, one of the following
 * negative error codes is returned.
 *
 * %-EIO - I/O error.
 *
 * %-ENOMEM - Insufficient amount of memory available.
 *
 * %-ENOENT - The inode number @ino have not been allocated.
 */
int nilfs_ifile_delete_inode(struct inode *ifile, ino_t ino)
{
	struct nilfs_palloc_req req = {
		.pr_entry_nr = ino, .pr_entry_bh = NULL
	};
	struct nilfs_inode *raw_inode;
	void *kaddr;
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

	kaddr = kmap_atomic(req.pr_entry_bh->b_page);
	raw_inode = nilfs_palloc_block_get_entry(ifile, req.pr_entry_nr,
						 req.pr_entry_bh, kaddr);
	raw_inode->i_flags = 0;
	kunmap_atomic(kaddr);

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
		nilfs_error(sb, __func__, "bad inode number: %lu",
			    (unsigned long) ino);
		return -EINVAL;
	}

	err = nilfs_palloc_get_entry_block(ifile, ino, 0, out_bh);
	if (unlikely(err))
		nilfs_warning(sb, __func__, "unable to read inode: %lu",
			      (unsigned long) ino);
	return err;
}

/**
 * nilfs_ifile_count_free_inodes - calculate free inodes count
 * @ifile: ifile inode
 * @nmaxinodes: current maximum of available inodes count [out]
 * @nfreeinodes: free inodes count [out]
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
 * @inode_size: size of an inode
 * @raw_inode: on-disk ifile inode
 * @inodep: buffer to store the inode
 */
int nilfs_ifile_read(struct super_block *sb, struct nilfs_root *root,
		     size_t inode_size, struct nilfs_inode *raw_inode,
		     struct inode **inodep)
{
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

	err = nilfs_read_inode_common(ifile, raw_inode);
	if (err)
		goto failed;

	unlock_new_inode(ifile);
 out:
	*inodep = ifile;
	return 0;
 failed:
	iget_failed(ifile);
	return err;
}
