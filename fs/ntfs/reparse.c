// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Processing of reparse points
 *
 * Part of this file is based on code from the NTFS-3G.
 *
 * Copyright (c) 2008-2021 Jean-Pierre Andre
 * Copyright (c) 2025 LG Electronics Co., Ltd.
 */

#include "ntfs.h"
#include "layout.h"
#include "attrib.h"
#include "inode.h"
#include "dir.h"
#include "volume.h"
#include "mft.h"
#include "index.h"
#include "lcnalloc.h"
#include "reparse.h"

struct wsl_link_reparse_data {
	__le32	type;
	char	link[];
};

/* Index entry in $Extend/$Reparse */
struct reparse_index {
	struct index_entry_header header;
	struct reparse_index_key key;
	__le32 filling;
};

__le16 reparse_index_name[] = {cpu_to_le16('$'), cpu_to_le16('R'), 0};


/*
 * Check if the reparse point attribute buffer is valid.
 * Returns true if valid, false otherwise.
 */
static bool ntfs_is_valid_reparse_buffer(struct ntfs_inode *ni,
		const struct reparse_point *reparse_attr, size_t size)
{
	size_t expected;

	if (!ni || !reparse_attr)
		return false;

	/* Minimum size must cover reparse_point header */
	if (size < sizeof(struct reparse_point))
		return false;

	/* Reserved zero tag is invalid */
	if (reparse_attr->reparse_tag == IO_REPARSE_TAG_RESERVED_ZERO)
		return false;

	/* Calculate expected total size */
	expected = sizeof(struct reparse_point) +
		le16_to_cpu(reparse_attr->reparse_data_length);

	/* Add GUID size for non-Microsoft tags */
	if (!(reparse_attr->reparse_tag & IO_REPARSE_TAG_IS_MICROSOFT))
		expected += sizeof(struct guid);

	/* Buffer must exactly match the expected size */
	return expected == size;
}

/*
 * Do some sanity checks on reparse data
 *
 * Microsoft reparse points have an 8-byte header whereas
 * non-Microsoft reparse points have a 24-byte header.  In each case,
 * 'reparse_data_length' must equal the number of non-header bytes.
 *
 * If the reparse data looks like a junction point or symbolic
 * link, more checks can be done.
 */
static bool valid_reparse_data(struct ntfs_inode *ni,
		const struct reparse_point *reparse_attr, size_t size)
{
	const struct wsl_link_reparse_data *wsl_reparse_data =
		(const struct wsl_link_reparse_data *)reparse_attr->reparse_data;
	unsigned int data_len = le16_to_cpu(reparse_attr->reparse_data_length);

	if (ntfs_is_valid_reparse_buffer(ni, reparse_attr, size) == false)
		return false;

	switch (reparse_attr->reparse_tag) {
	case IO_REPARSE_TAG_LX_SYMLINK:
		if (data_len <= sizeof(wsl_reparse_data->type) ||
		    wsl_reparse_data->type != cpu_to_le32(2))
			return false;
		break;
	case IO_REPARSE_TAG_AF_UNIX:
	case IO_REPARSE_TAG_LX_FIFO:
	case IO_REPARSE_TAG_LX_CHR:
	case IO_REPARSE_TAG_LX_BLK:
		if (data_len || !(ni->flags & FILE_ATTRIBUTE_RECALL_ON_OPEN))
			return false;
	}

	return true;
}

static unsigned int ntfs_reparse_tag_mode(struct reparse_point *reparse_attr)
{
	unsigned int mode = 0;

	switch (reparse_attr->reparse_tag) {
	case IO_REPARSE_TAG_SYMLINK:
	case IO_REPARSE_TAG_LX_SYMLINK:
		mode = S_IFLNK;
		break;
	case IO_REPARSE_TAG_AF_UNIX:
		mode = S_IFSOCK;
		break;
	case IO_REPARSE_TAG_LX_FIFO:
		mode = S_IFIFO;
		break;
	case IO_REPARSE_TAG_LX_CHR:
		mode = S_IFCHR;
		break;
	case IO_REPARSE_TAG_LX_BLK:
		mode = S_IFBLK;
	}

	return mode;
}

/*
 * Get the target for symbolic link
 */
unsigned int ntfs_make_symlink(struct ntfs_inode *ni)
{
	s64 attr_size = 0;
	unsigned int lth;
	struct reparse_point *reparse_attr;
	struct wsl_link_reparse_data *wsl_link_data;
	unsigned int mode = 0;

	reparse_attr = ntfs_attr_readall(ni, AT_REPARSE_POINT, NULL, 0,
					 &attr_size);
	if (reparse_attr && attr_size &&
	    valid_reparse_data(ni, reparse_attr, attr_size)) {
		switch (reparse_attr->reparse_tag) {
		case IO_REPARSE_TAG_LX_SYMLINK:
			wsl_link_data =
				(struct wsl_link_reparse_data *)reparse_attr->reparse_data;
			if (wsl_link_data->type == cpu_to_le32(2)) {
				lth = le16_to_cpu(reparse_attr->reparse_data_length) -
						  sizeof(wsl_link_data->type);
				ni->target = kvzalloc(lth + 1, GFP_NOFS);
				if (ni->target) {
					memcpy(ni->target, wsl_link_data->link, lth);
					ni->target[lth] = 0;
					mode = ntfs_reparse_tag_mode(reparse_attr);
				}
			}
			break;
		default:
			mode = ntfs_reparse_tag_mode(reparse_attr);
		}
	} else
		ni->flags &= ~FILE_ATTR_REPARSE_POINT;

	if (reparse_attr)
		kvfree(reparse_attr);

	return mode;
}

unsigned int ntfs_reparse_tag_dt_types(struct ntfs_volume *vol, unsigned long mref)
{
	s64 attr_size = 0;
	struct reparse_point *reparse_attr;
	unsigned int dt_type = DT_UNKNOWN;
	struct inode *vi;

	vi = ntfs_iget(vol->sb, mref);
	if (IS_ERR(vi))
		return PTR_ERR(vi);

	reparse_attr = (struct reparse_point *)ntfs_attr_readall(NTFS_I(vi),
			AT_REPARSE_POINT, NULL, 0, &attr_size);

	if (reparse_attr && attr_size) {
		switch (reparse_attr->reparse_tag) {
		case IO_REPARSE_TAG_SYMLINK:
		case IO_REPARSE_TAG_LX_SYMLINK:
			dt_type = DT_LNK;
			break;
		case IO_REPARSE_TAG_AF_UNIX:
			dt_type = DT_SOCK;
			break;
		case IO_REPARSE_TAG_LX_FIFO:
			dt_type = DT_FIFO;
			break;
		case IO_REPARSE_TAG_LX_CHR:
			dt_type = DT_CHR;
			break;
		case IO_REPARSE_TAG_LX_BLK:
			dt_type = DT_BLK;
		}
	}

	if (reparse_attr)
		kvfree(reparse_attr);

	iput(vi);
	return dt_type;
}

/*
 * Set the index for new reparse data
 */
static int set_reparse_index(struct ntfs_inode *ni, struct ntfs_index_context *xr,
		__le32 reparse_tag)
{
	struct reparse_index indx;
	u64 file_id_cpu;
	__le64 file_id;

	file_id_cpu = MK_MREF(ni->mft_no, ni->seq_no);
	file_id = cpu_to_le64(file_id_cpu);
	indx.header.data.vi.data_offset =
		cpu_to_le16(sizeof(struct index_entry_header) + sizeof(struct reparse_index_key));
	indx.header.data.vi.data_length = 0;
	indx.header.data.vi.reservedV = 0;
	indx.header.length = cpu_to_le16(sizeof(struct reparse_index));
	indx.header.key_length = cpu_to_le16(sizeof(struct reparse_index_key));
	indx.header.flags = 0;
	indx.header.reserved = 0;
	indx.key.reparse_tag = reparse_tag;
	/* danger on processors which require proper alignment! */
	memcpy(&indx.key.file_id, &file_id, 8);
	indx.filling = 0;
	ntfs_index_ctx_reinit(xr);

	return ntfs_ie_add(xr, (struct index_entry *)&indx);
}

/*
 * Remove a reparse data index entry if attribute present
 */
static int remove_reparse_index(struct inode *rp, struct ntfs_index_context *xr,
				__le32 *preparse_tag)
{
	struct reparse_index_key key;
	u64 file_id_cpu;
	__le64 file_id;
	s64 size;
	struct ntfs_inode *ni = NTFS_I(rp);
	int err = 0, ret = ni->data_size;

	if (ni->data_size == 0)
		return 0;

	/* read the existing reparse_tag */
	size = ntfs_inode_attr_pread(rp, 0, 4, (char *)preparse_tag);
	if (size != 4)
		return -ENODATA;

	file_id_cpu = MK_MREF(ni->mft_no, ni->seq_no);
	file_id = cpu_to_le64(file_id_cpu);
	key.reparse_tag = *preparse_tag;
	/* danger on processors which require proper alignment! */
	memcpy(&key.file_id, &file_id, 8);
	if (!ntfs_index_lookup(&key, sizeof(struct reparse_index_key), xr)) {
		err = ntfs_index_rm(xr);
		if (err)
			ret = err;
	}
	return ret;
}

/*
 * Open the $Extend/$Reparse file and its index
 */
static struct ntfs_index_context *open_reparse_index(struct ntfs_volume *vol)
{
	struct ntfs_index_context *xr = NULL;
	u64 mref;
	__le16 *uname;
	struct ntfs_name *name = NULL;
	int uname_len;
	struct inode *vi, *dir_vi;

	/* do not use path_name_to inode - could reopen root */
	dir_vi = ntfs_iget(vol->sb, FILE_Extend);
	if (IS_ERR(dir_vi))
		return NULL;

	uname_len = ntfs_nlstoucs(vol, "$Reparse", 8, &uname,
				  NTFS_MAX_NAME_LEN);
	if (uname_len < 0) {
		iput(dir_vi);
		return NULL;
	}

	mutex_lock_nested(&NTFS_I(dir_vi)->mrec_lock, NTFS_EXTEND_MUTEX_PARENT);
	mref = ntfs_lookup_inode_by_name(NTFS_I(dir_vi), uname, uname_len,
					 &name);
	mutex_unlock(&NTFS_I(dir_vi)->mrec_lock);
	kfree(name);
	kmem_cache_free(ntfs_name_cache, uname);
	if (IS_ERR_MREF(mref))
		goto put_dir_vi;

	vi = ntfs_iget(vol->sb, MREF(mref));
	if (IS_ERR(vi))
		goto put_dir_vi;

	xr = ntfs_index_ctx_get(NTFS_I(vi), reparse_index_name, 2);
	if (!xr)
		iput(vi);
put_dir_vi:
	iput(dir_vi);
	return xr;
}


/*
 * Update the reparse data and index
 *
 * The reparse data attribute should have been created, and
 * an existing index is expected if there is an existing value.
 *
 */
static int update_reparse_data(struct ntfs_inode *ni, struct ntfs_index_context *xr,
		char *value, size_t size)
{
	struct inode *rp_inode;
	int err = 0;
	s64 written;
	int oldsize;
	__le32 reparse_tag;
	struct ntfs_inode *rp_ni;

	rp_inode = ntfs_attr_iget(VFS_I(ni), AT_REPARSE_POINT, AT_UNNAMED, 0);
	if (IS_ERR(rp_inode))
		return -EINVAL;
	rp_ni = NTFS_I(rp_inode);

	/* remove the existing reparse data */
	oldsize = remove_reparse_index(rp_inode, xr, &reparse_tag);
	if (oldsize < 0) {
		err = oldsize;
		goto put_rp_inode;
	}

	/* overwrite value if any */
	written = ntfs_inode_attr_pwrite(rp_inode, 0, size, value, false);
	if (written != size) {
		ntfs_error(ni->vol->sb, "Failed to update reparse data\n");
		err = -EIO;
		goto put_rp_inode;
	}

	if (set_reparse_index(ni, xr, ((const struct reparse_point *)value)->reparse_tag) &&
	    oldsize > 0) {
		/*
		 * If cannot index, try to remove the reparse
		 * data and log the error. There will be an
		 * inconsistency if removal fails.
		 */
		ntfs_attr_rm(rp_ni);
		ntfs_error(ni->vol->sb,
			   "Failed to index reparse data. Possible corruption.\n");
	}

	mark_mft_record_dirty(ni);
put_rp_inode:
	iput(rp_inode);

	return err;
}

/*
 * Delete a reparse index entry
 */
int ntfs_delete_reparse_index(struct ntfs_inode *ni)
{
	struct inode *vi;
	struct ntfs_index_context *xr;
	struct ntfs_inode *xrni;
	__le32 reparse_tag;
	int err = 0;

	if (!(ni->flags & FILE_ATTR_REPARSE_POINT))
		return 0;

	vi = ntfs_attr_iget(VFS_I(ni), AT_REPARSE_POINT, AT_UNNAMED, 0);
	if (IS_ERR(vi))
		return PTR_ERR(vi);

	/*
	 * read the existing reparse data (the tag is enough)
	 * and un-index it
	 */
	xr = open_reparse_index(ni->vol);
	if (xr) {
		xrni = xr->idx_ni;
		mutex_lock_nested(&xrni->mrec_lock, NTFS_EXTEND_MUTEX_PARENT);
		err = remove_reparse_index(vi, xr, &reparse_tag);
		if (err < 0) {
			ntfs_index_ctx_put(xr);
			mutex_unlock(&xrni->mrec_lock);
			iput(VFS_I(xrni));
			goto out;
		}
		mark_mft_record_dirty(xrni);
		ntfs_index_ctx_put(xr);
		mutex_unlock(&xrni->mrec_lock);
		iput(VFS_I(xrni));
	}

	ni->flags &= ~FILE_ATTR_REPARSE_POINT;
	NInoSetFileNameDirty(ni);
	mark_mft_record_dirty(ni);

out:
	iput(vi);
	return err;
}

/*
 * Set the reparse data from an extended attribute
 */
static int ntfs_set_ntfs_reparse_data(struct ntfs_inode *ni, char *value, size_t size)
{
	int err = 0;
	struct ntfs_inode *xrni;
	struct ntfs_index_context *xr;

	if (!ni)
		return -EINVAL;

	/*
	 * reparse data compatibily with EA is not checked
	 * any more, it is required by Windows 10, but may
	 * lead to problems with earlier versions.
	 */
	if (valid_reparse_data(ni, (const struct reparse_point *)value, size) == false)
		return -EINVAL;

	xr = open_reparse_index(ni->vol);
	if (!xr)
		return -EINVAL;
	xrni = xr->idx_ni;

	if (!ntfs_attr_exist(ni, AT_REPARSE_POINT, AT_UNNAMED, 0)) {
		struct reparse_point rp = {0, };

		/*
		 * no reparse data attribute : add one,
		 * apparently, this does not feed the new value in
		 * Note : NTFS version must be >= 3
		 */
		if (ni->vol->major_ver < 3) {
			err = -EOPNOTSUPP;
			ntfs_index_ctx_put(xr);
			goto out;
		}

		err = ntfs_attr_add(ni, AT_REPARSE_POINT, AT_UNNAMED, 0, (u8 *)&rp, sizeof(rp));
		if (err) {
			ntfs_index_ctx_put(xr);
			goto out;
		}
		ni->flags |= FILE_ATTR_REPARSE_POINT;
		NInoSetFileNameDirty(ni);
		mark_mft_record_dirty(ni);
	}

	/* update value and index */
	mutex_lock_nested(&xrni->mrec_lock, NTFS_EXTEND_MUTEX_PARENT);
	err = update_reparse_data(ni, xr, value, size);
	if (err) {
		ni->flags &= ~FILE_ATTR_REPARSE_POINT;
		NInoSetFileNameDirty(ni);
		mark_mft_record_dirty(ni);
	}
	ntfs_index_ctx_put(xr);
	mutex_unlock(&xrni->mrec_lock);

out:
	if (!err)
		mark_mft_record_dirty(xrni);
	iput(VFS_I(xrni));

	return err;
}

/*
 * Set reparse data for a WSL type symlink
 */
int ntfs_reparse_set_wsl_symlink(struct ntfs_inode *ni,
		const __le16 *target, int target_len)
{
	int err = 0;
	int len;
	int reparse_len;
	unsigned char *utarget = NULL;
	struct reparse_point *reparse;
	struct wsl_link_reparse_data *data;

	utarget = (char *)NULL;
	len = ntfs_ucstonls(ni->vol, target, target_len, &utarget, 0);
	if (len <= 0)
		return -EINVAL;

	reparse_len = sizeof(struct reparse_point) + sizeof(data->type) + len;
	reparse = kvzalloc(reparse_len, GFP_NOFS);
	if (!reparse) {
		err = -ENOMEM;
		kvfree(utarget);
	} else {
		data = (struct wsl_link_reparse_data *)reparse->reparse_data;
		reparse->reparse_tag = IO_REPARSE_TAG_LX_SYMLINK;
		reparse->reparse_data_length =
			cpu_to_le16(sizeof(data->type) + len);
		reparse->reserved = 0;
		data->type = cpu_to_le32(2);
		memcpy(data->link, utarget, len);
		err = ntfs_set_ntfs_reparse_data(ni,
				(char *)reparse, reparse_len);
		kvfree(reparse);
		if (!err)
			ni->target = utarget;
	}
	return err;
}

/*
 * Set reparse data for a WSL special file other than a symlink
 * (socket, fifo, character or block device)
 */
int ntfs_reparse_set_wsl_not_symlink(struct ntfs_inode *ni, mode_t mode)
{
	int err;
	int len;
	int reparse_len;
	__le32 reparse_tag;
	struct reparse_point *reparse;

	len = 0;
	if (S_ISSOCK(mode))
		reparse_tag = IO_REPARSE_TAG_AF_UNIX;
	else if (S_ISFIFO(mode))
		reparse_tag = IO_REPARSE_TAG_LX_FIFO;
	else if (S_ISCHR(mode))
		reparse_tag = IO_REPARSE_TAG_LX_CHR;
	else if (S_ISBLK(mode))
		reparse_tag = IO_REPARSE_TAG_LX_BLK;
	else
		return -EOPNOTSUPP;

	reparse_len = sizeof(struct reparse_point) + len;
	reparse = kvzalloc(reparse_len, GFP_NOFS);
	if (!reparse)
		err = -ENOMEM;
	else {
		reparse->reparse_tag = reparse_tag;
		reparse->reparse_data_length = cpu_to_le16(len);
		reparse->reserved = cpu_to_le16(0);
		err = ntfs_set_ntfs_reparse_data(ni, (char *)reparse,
						 reparse_len);
		kvfree(reparse);
	}

	return err;
}
