// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Pocessing of object ids
 *
 * Part of this file is based on code from the NTFS-3G.
 *
 * Copyright (c) 2009-2019 Jean-Pierre Andre
 * Copyright (c) 2026 LG Electronics Co., Ltd.
 */

#include "ntfs.h"
#include "index.h"
#include "object_id.h"

struct object_id_index_key {
	union {
		u32 alignment;
		struct guid guid;
	} object_id;
} __packed;

struct object_id_index_data {
	__le64 file_id;
	struct guid birth_volume_id;
	struct guid birth_object_id;
	struct guid domain_id;
} __packed;

/* Index entry in $Extend/$ObjId */
struct object_id_index {
	struct index_entry_header header;
	struct object_id_index_key key;
	struct object_id_index_data data;
} __packed;

__le16 objid_index_name[] = {cpu_to_le16('$'), cpu_to_le16('O'), 0};

/*
 * open_object_id_index - Open the $Extend/$ObjId file and its index
 * @vol: NTFS volume structure
 *
 * Opens the $ObjId system file and retrieves its index context.
 *
 * Return: The index context if opened successfully, or NULL if an error
 *	   occurred.
 */
static struct ntfs_index_context *open_object_id_index(struct ntfs_volume *vol)
{
	struct inode *dir_vi, *vi;
	struct ntfs_inode *dir_ni;
	struct ntfs_index_context *xo = NULL;
	struct ntfs_name *name = NULL;
	u64 mref;
	int uname_len;
	__le16 *uname;

	uname_len = ntfs_nlstoucs(vol, "$ObjId", 6, &uname,
			NTFS_MAX_NAME_LEN);
	if (uname_len < 0)
		return NULL;

	/* do not use path_name_to inode - could reopen root */
	dir_vi = ntfs_iget(vol->sb, FILE_Extend);
	if (IS_ERR(dir_vi)) {
		kmem_cache_free(ntfs_name_cache, uname);
		return NULL;
	}
	dir_ni = NTFS_I(dir_vi);

	mutex_lock_nested(&dir_ni->mrec_lock, NTFS_EXTEND_MUTEX_PARENT);
	mref = ntfs_lookup_inode_by_name(dir_ni, uname, uname_len, &name);
	mutex_unlock(&dir_ni->mrec_lock);
	kfree(name);
	kmem_cache_free(ntfs_name_cache, uname);
	if (IS_ERR_MREF(mref))
		goto put_dir_vi;

	vi = ntfs_iget(vol->sb, MREF(mref));
	if (IS_ERR(vi))
		goto put_dir_vi;

	xo = ntfs_index_ctx_get(NTFS_I(vi), objid_index_name, 2);
	if (!xo)
		iput(vi);
put_dir_vi:
	iput(dir_vi);
	return xo;
}


/*
 * remove_object_id_index - Remove an object id index entry if attribute present
 * @ni: NTFS inode structure containing the attribute
 * @xo:	Index context for the object id index
 *
 * Reads the existing object ID attribute and removes it from the index.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
static int remove_object_id_index(struct ntfs_inode *ni, struct ntfs_index_context *xo)
{
	struct object_id_index_key key = {0};
	s64 size;

	if (ni->data_size == 0)
		return -ENODATA;

	/* read the existing object id attribute */
	size = ntfs_inode_attr_pread(VFS_I(ni), 0, sizeof(struct guid),
				     (char *)&key);
	if (size != sizeof(struct guid))
		return -ENODATA;

	if (!ntfs_index_lookup(&key, sizeof(struct object_id_index_key), xo))
		return ntfs_index_rm(xo);

	return 0;
}

/*
 * ntfs_delete_object_id_index - Delete an object_id index entry
 * @ni:	NTFS inode structure
 *
 * Opens the object ID index and removes the entry corresponding to the inode.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
int ntfs_delete_object_id_index(struct ntfs_inode *ni)
{
	struct ntfs_index_context *xo;
	struct ntfs_inode *xoni;
	struct inode *attr_vi;
	int ret = 0;

	attr_vi = ntfs_attr_iget(VFS_I(ni), AT_OBJECT_ID, AT_UNNAMED, 0);
	if (IS_ERR(attr_vi))
		return PTR_ERR(attr_vi);

	/*
	 * read the existing object id and un-index it
	 */
	xo = open_object_id_index(ni->vol);
	if (xo) {
		xoni = xo->idx_ni;
		mutex_lock_nested(&xoni->mrec_lock, NTFS_EXTEND_MUTEX_PARENT);
		ret = remove_object_id_index(NTFS_I(attr_vi), xo);
		if (!ret) {
			ntfs_index_entry_mark_dirty(xo);
			mark_mft_record_dirty(xoni);
		}
		ntfs_index_ctx_put(xo);
		mutex_unlock(&xoni->mrec_lock);
		iput(VFS_I(xoni));
	}

	iput(attr_vi);
	return ret;
}
