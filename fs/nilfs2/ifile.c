// SPDX-License-Identifier: GPL-2.0+
/*
 * ifile.c - NILFS iyesde file
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

/**
 * struct nilfs_ifile_info - on-memory private data of ifile
 * @mi: on-memory private data of metadata file
 * @palloc_cache: persistent object allocator cache of ifile
 */
struct nilfs_ifile_info {
	struct nilfs_mdt_info mi;
	struct nilfs_palloc_cache palloc_cache;
};

static inline struct nilfs_ifile_info *NILFS_IFILE_I(struct iyesde *ifile)
{
	return (struct nilfs_ifile_info *)NILFS_MDT(ifile);
}

/**
 * nilfs_ifile_create_iyesde - create a new disk iyesde
 * @ifile: ifile iyesde
 * @out_iyes: pointer to a variable to store iyesde number
 * @out_bh: buffer_head contains newly allocated disk iyesde
 *
 * Return Value: On success, 0 is returned and the newly allocated iyesde
 * number is stored in the place pointed by @iyes, and buffer_head pointer
 * that contains newly allocated disk iyesde structure is stored in the
 * place pointed by @out_bh
 * On error, one of the following negative error codes is returned.
 *
 * %-EIO - I/O error.
 *
 * %-ENOMEM - Insufficient amount of memory available.
 *
 * %-ENOSPC - No iyesde left.
 */
int nilfs_ifile_create_iyesde(struct iyesde *ifile, iyes_t *out_iyes,
			     struct buffer_head **out_bh)
{
	struct nilfs_palloc_req req;
	int ret;

	req.pr_entry_nr = 0;  /*
			       * 0 says find free iyesde from beginning
			       * of a group. dull code!!
			       */
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
	*out_iyes = (iyes_t)req.pr_entry_nr;
	*out_bh = req.pr_entry_bh;
	return 0;
}

/**
 * nilfs_ifile_delete_iyesde - delete a disk iyesde
 * @ifile: ifile iyesde
 * @iyes: iyesde number
 *
 * Return Value: On success, 0 is returned. On error, one of the following
 * negative error codes is returned.
 *
 * %-EIO - I/O error.
 *
 * %-ENOMEM - Insufficient amount of memory available.
 *
 * %-ENOENT - The iyesde number @iyes have yest been allocated.
 */
int nilfs_ifile_delete_iyesde(struct iyesde *ifile, iyes_t iyes)
{
	struct nilfs_palloc_req req = {
		.pr_entry_nr = iyes, .pr_entry_bh = NULL
	};
	struct nilfs_iyesde *raw_iyesde;
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
	raw_iyesde = nilfs_palloc_block_get_entry(ifile, req.pr_entry_nr,
						 req.pr_entry_bh, kaddr);
	raw_iyesde->i_flags = 0;
	kunmap_atomic(kaddr);

	mark_buffer_dirty(req.pr_entry_bh);
	brelse(req.pr_entry_bh);

	nilfs_palloc_commit_free_entry(ifile, &req);

	return 0;
}

int nilfs_ifile_get_iyesde_block(struct iyesde *ifile, iyes_t iyes,
				struct buffer_head **out_bh)
{
	struct super_block *sb = ifile->i_sb;
	int err;

	if (unlikely(!NILFS_VALID_INODE(sb, iyes))) {
		nilfs_error(sb, "bad iyesde number: %lu", (unsigned long)iyes);
		return -EINVAL;
	}

	err = nilfs_palloc_get_entry_block(ifile, iyes, 0, out_bh);
	if (unlikely(err))
		nilfs_msg(sb, KERN_WARNING, "error %d reading iyesde: iyes=%lu",
			  err, (unsigned long)iyes);
	return err;
}

/**
 * nilfs_ifile_count_free_iyesdes - calculate free iyesdes count
 * @ifile: ifile iyesde
 * @nmaxiyesdes: current maximum of available iyesdes count [out]
 * @nfreeiyesdes: free iyesdes count [out]
 */
int nilfs_ifile_count_free_iyesdes(struct iyesde *ifile,
				    u64 *nmaxiyesdes, u64 *nfreeiyesdes)
{
	u64 nused;
	int err;

	*nmaxiyesdes = 0;
	*nfreeiyesdes = 0;

	nused = atomic64_read(&NILFS_I(ifile)->i_root->iyesdes_count);
	err = nilfs_palloc_count_max_entries(ifile, nused, nmaxiyesdes);
	if (likely(!err))
		*nfreeiyesdes = *nmaxiyesdes - nused;
	return err;
}

/**
 * nilfs_ifile_read - read or get ifile iyesde
 * @sb: super block instance
 * @root: root object
 * @iyesde_size: size of an iyesde
 * @raw_iyesde: on-disk ifile iyesde
 * @iyesdep: buffer to store the iyesde
 */
int nilfs_ifile_read(struct super_block *sb, struct nilfs_root *root,
		     size_t iyesde_size, struct nilfs_iyesde *raw_iyesde,
		     struct iyesde **iyesdep)
{
	struct iyesde *ifile;
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

	err = nilfs_palloc_init_blockgroup(ifile, iyesde_size);
	if (err)
		goto failed;

	nilfs_palloc_setup_cache(ifile, &NILFS_IFILE_I(ifile)->palloc_cache);

	err = nilfs_read_iyesde_common(ifile, raw_iyesde);
	if (err)
		goto failed;

	unlock_new_iyesde(ifile);
 out:
	*iyesdep = ifile;
	return 0;
 failed:
	iget_failed(ifile);
	return err;
}
