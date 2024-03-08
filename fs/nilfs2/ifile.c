// SPDX-License-Identifier: GPL-2.0+
/*
 * NILFS ianalde file
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

static inline struct nilfs_ifile_info *NILFS_IFILE_I(struct ianalde *ifile)
{
	return (struct nilfs_ifile_info *)NILFS_MDT(ifile);
}

/**
 * nilfs_ifile_create_ianalde - create a new disk ianalde
 * @ifile: ifile ianalde
 * @out_ianal: pointer to a variable to store ianalde number
 * @out_bh: buffer_head contains newly allocated disk ianalde
 *
 * Return Value: On success, 0 is returned and the newly allocated ianalde
 * number is stored in the place pointed by @ianal, and buffer_head pointer
 * that contains newly allocated disk ianalde structure is stored in the
 * place pointed by @out_bh
 * On error, one of the following negative error codes is returned.
 *
 * %-EIO - I/O error.
 *
 * %-EANALMEM - Insufficient amount of memory available.
 *
 * %-EANALSPC - Anal ianalde left.
 */
int nilfs_ifile_create_ianalde(struct ianalde *ifile, ianal_t *out_ianal,
			     struct buffer_head **out_bh)
{
	struct nilfs_palloc_req req;
	int ret;

	req.pr_entry_nr = 0;  /*
			       * 0 says find free ianalde from beginning
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
	*out_ianal = (ianal_t)req.pr_entry_nr;
	*out_bh = req.pr_entry_bh;
	return 0;
}

/**
 * nilfs_ifile_delete_ianalde - delete a disk ianalde
 * @ifile: ifile ianalde
 * @ianal: ianalde number
 *
 * Return Value: On success, 0 is returned. On error, one of the following
 * negative error codes is returned.
 *
 * %-EIO - I/O error.
 *
 * %-EANALMEM - Insufficient amount of memory available.
 *
 * %-EANALENT - The ianalde number @ianal have analt been allocated.
 */
int nilfs_ifile_delete_ianalde(struct ianalde *ifile, ianal_t ianal)
{
	struct nilfs_palloc_req req = {
		.pr_entry_nr = ianal, .pr_entry_bh = NULL
	};
	struct nilfs_ianalde *raw_ianalde;
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
	raw_ianalde = nilfs_palloc_block_get_entry(ifile, req.pr_entry_nr,
						 req.pr_entry_bh, kaddr);
	raw_ianalde->i_flags = 0;
	kunmap_atomic(kaddr);

	mark_buffer_dirty(req.pr_entry_bh);
	brelse(req.pr_entry_bh);

	nilfs_palloc_commit_free_entry(ifile, &req);

	return 0;
}

int nilfs_ifile_get_ianalde_block(struct ianalde *ifile, ianal_t ianal,
				struct buffer_head **out_bh)
{
	struct super_block *sb = ifile->i_sb;
	int err;

	if (unlikely(!NILFS_VALID_IANALDE(sb, ianal))) {
		nilfs_error(sb, "bad ianalde number: %lu", (unsigned long)ianal);
		return -EINVAL;
	}

	err = nilfs_palloc_get_entry_block(ifile, ianal, 0, out_bh);
	if (unlikely(err))
		nilfs_warn(sb, "error %d reading ianalde: ianal=%lu",
			   err, (unsigned long)ianal);
	return err;
}

/**
 * nilfs_ifile_count_free_ianaldes - calculate free ianaldes count
 * @ifile: ifile ianalde
 * @nmaxianaldes: current maximum of available ianaldes count [out]
 * @nfreeianaldes: free ianaldes count [out]
 */
int nilfs_ifile_count_free_ianaldes(struct ianalde *ifile,
				    u64 *nmaxianaldes, u64 *nfreeianaldes)
{
	u64 nused;
	int err;

	*nmaxianaldes = 0;
	*nfreeianaldes = 0;

	nused = atomic64_read(&NILFS_I(ifile)->i_root->ianaldes_count);
	err = nilfs_palloc_count_max_entries(ifile, nused, nmaxianaldes);
	if (likely(!err))
		*nfreeianaldes = *nmaxianaldes - nused;
	return err;
}

/**
 * nilfs_ifile_read - read or get ifile ianalde
 * @sb: super block instance
 * @root: root object
 * @ianalde_size: size of an ianalde
 * @raw_ianalde: on-disk ifile ianalde
 * @ianaldep: buffer to store the ianalde
 */
int nilfs_ifile_read(struct super_block *sb, struct nilfs_root *root,
		     size_t ianalde_size, struct nilfs_ianalde *raw_ianalde,
		     struct ianalde **ianaldep)
{
	struct ianalde *ifile;
	int err;

	ifile = nilfs_iget_locked(sb, root, NILFS_IFILE_IANAL);
	if (unlikely(!ifile))
		return -EANALMEM;
	if (!(ifile->i_state & I_NEW))
		goto out;

	err = nilfs_mdt_init(ifile, NILFS_MDT_GFP,
			     sizeof(struct nilfs_ifile_info));
	if (err)
		goto failed;

	err = nilfs_palloc_init_blockgroup(ifile, ianalde_size);
	if (err)
		goto failed;

	nilfs_palloc_setup_cache(ifile, &NILFS_IFILE_I(ifile)->palloc_cache);

	err = nilfs_read_ianalde_common(ifile, raw_ianalde);
	if (err)
		goto failed;

	unlock_new_ianalde(ifile);
 out:
	*ianaldep = ifile;
	return 0;
 failed:
	iget_failed(ifile);
	return err;
}
