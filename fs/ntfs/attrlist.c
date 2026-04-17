// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Attribute list attribute handling code.
 * Part of this file is based on code from the NTFS-3G.
 *
 * Copyright (c) 2004-2005 Anton Altaparmakov
 * Copyright (c) 2004-2005 Yura Pakhuchiy
 * Copyright (c)      2006 Szabolcs Szakacsits
 * Copyright (c) 2025 LG Electronics Co., Ltd.
 */

#include "mft.h"
#include "attrib.h"
#include "attrlist.h"

/*
 * ntfs_attrlist_need - check whether inode need attribute list
 * @ni:	opened ntfs inode for which perform check
 *
 * Check whether all are attributes belong to one MFT record, in that case
 * attribute list is not needed.
 *
 * Return 1 if inode need attribute list, 0 if not, or -errno on error.
 */
int ntfs_attrlist_need(struct ntfs_inode *ni)
{
	struct attr_list_entry *ale;

	if (!ni) {
		ntfs_debug("Invalid arguments.\n");
		return -EINVAL;
	}
	ntfs_debug("Entering for inode 0x%llx.\n", (long long) ni->mft_no);

	if (!NInoAttrList(ni)) {
		ntfs_debug("Inode haven't got attribute list.\n");
		return -EINVAL;
	}

	if (!ni->attr_list) {
		ntfs_debug("Corrupt in-memory struct.\n");
		return -EINVAL;
	}

	ale = (struct attr_list_entry *)ni->attr_list;
	while ((u8 *)ale < ni->attr_list + ni->attr_list_size) {
		if (MREF_LE(ale->mft_reference) != ni->mft_no)
			return 1;
		ale = (struct attr_list_entry *)((u8 *)ale + le16_to_cpu(ale->length));
	}
	return 0;
}

int ntfs_attrlist_update(struct ntfs_inode *base_ni)
{
	struct inode *attr_vi;
	struct ntfs_inode *attr_ni;
	int err;

	attr_vi = ntfs_attr_iget(VFS_I(base_ni), AT_ATTRIBUTE_LIST, AT_UNNAMED, 0);
	if (IS_ERR(attr_vi)) {
		err = PTR_ERR(attr_vi);
		return err;
	}
	attr_ni = NTFS_I(attr_vi);

	err = ntfs_attr_truncate_i(attr_ni, base_ni->attr_list_size, HOLES_NO);
	if (err == -ENOSPC && attr_ni->mft_no == FILE_MFT) {
		err = ntfs_attr_truncate(attr_ni, 0);
		if (err || ntfs_attr_truncate_i(attr_ni, base_ni->attr_list_size, HOLES_NO) != 0) {
			iput(attr_vi);
			ntfs_error(base_ni->vol->sb,
					"Failed to truncate attribute list of inode %#llx",
					(long long)base_ni->mft_no);
			return -EIO;
		}
	} else if (err) {
		iput(attr_vi);
		ntfs_error(base_ni->vol->sb,
			   "Failed to truncate attribute list of inode %#llx",
			   (long long)base_ni->mft_no);
		return -EIO;
	}

	i_size_write(attr_vi, base_ni->attr_list_size);

	if (NInoNonResident(attr_ni) && !NInoAttrListNonResident(base_ni))
		NInoSetAttrListNonResident(base_ni);

	if (ntfs_inode_attr_pwrite(attr_vi, 0, base_ni->attr_list_size,
				   base_ni->attr_list, false) !=
	    base_ni->attr_list_size) {
		iput(attr_vi);
		ntfs_error(base_ni->vol->sb,
			   "Failed to write attribute list of inode %#llx",
			   (long long)base_ni->mft_no);
		return -EIO;
	}

	NInoSetAttrListDirty(base_ni);
	iput(attr_vi);
	return 0;
}

/*
 * ntfs_attrlist_entry_add - add an attribute list attribute entry
 * @ni:	opened ntfs inode, which contains that attribute
 * @attr: attribute record to add to attribute list
 *
 * Return 0 on success and -errno on error.
 */
int ntfs_attrlist_entry_add(struct ntfs_inode *ni, struct attr_record *attr)
{
	struct attr_list_entry *ale;
	__le64 mref;
	struct ntfs_attr_search_ctx *ctx;
	u8 *new_al;
	int entry_len, entry_offset, err;
	struct mft_record *ni_mrec;
	u8 *old_al;

	ntfs_debug("Entering for inode 0x%llx, attr 0x%x.\n",
			(long long) ni->mft_no,
			(unsigned int) le32_to_cpu(attr->type));

	if (!ni || !attr) {
		ntfs_debug("Invalid arguments.\n");
		return -EINVAL;
	}

	ni_mrec = map_mft_record(ni);
	if (IS_ERR(ni_mrec)) {
		ntfs_debug("Invalid arguments.\n");
		return -EIO;
	}

	mref = MK_LE_MREF(ni->mft_no, le16_to_cpu(ni_mrec->sequence_number));
	unmap_mft_record(ni);

	if (ni->nr_extents == -1)
		ni = ni->ext.base_ntfs_ino;

	if (!NInoAttrList(ni)) {
		ntfs_debug("Attribute list isn't present.\n");
		return -ENOENT;
	}

	/* Determine size and allocate memory for new attribute list. */
	entry_len = (sizeof(struct attr_list_entry) + sizeof(__le16) *
			attr->name_length + 7) & ~7;
	new_al = kvzalloc(ni->attr_list_size + entry_len, GFP_NOFS);
	if (!new_al)
		return -ENOMEM;

	/* Find place for the new entry. */
	ctx = ntfs_attr_get_search_ctx(ni, NULL);
	if (!ctx) {
		err = -ENOMEM;
		ntfs_error(ni->vol->sb, "Failed to get search context");
		goto err_out;
	}

	err = ntfs_attr_lookup(attr->type, (attr->name_length) ? (__le16 *)
			((u8 *)attr + le16_to_cpu(attr->name_offset)) :
			AT_UNNAMED, attr->name_length, CASE_SENSITIVE,
			(attr->non_resident) ? le64_to_cpu(attr->data.non_resident.lowest_vcn) :
			0, (attr->non_resident) ? NULL : ((u8 *)attr +
			le16_to_cpu(attr->data.resident.value_offset)), (attr->non_resident) ?
			0 : le32_to_cpu(attr->data.resident.value_length), ctx);
	if (!err) {
		/* Found some extent, check it to be before new extent. */
		if (ctx->al_entry->lowest_vcn == attr->data.non_resident.lowest_vcn) {
			err = -EEXIST;
			ntfs_debug("Such attribute already present in the attribute list.\n");
			ntfs_attr_put_search_ctx(ctx);
			goto err_out;
		}
		/* Add new entry after this extent. */
		ale = (struct attr_list_entry *)((u8 *)ctx->al_entry +
				le16_to_cpu(ctx->al_entry->length));
	} else {
		/* Check for real errors. */
		if (err != -ENOENT) {
			ntfs_debug("Attribute lookup failed.\n");
			ntfs_attr_put_search_ctx(ctx);
			goto err_out;
		}
		/* No previous extents found. */
		ale = ctx->al_entry;
	}
	/* Don't need it anymore, @ctx->al_entry points to @ni->attr_list. */
	ntfs_attr_put_search_ctx(ctx);

	/* Determine new entry offset. */
	entry_offset = ((u8 *)ale - ni->attr_list);
	/* Set pointer to new entry. */
	ale = (struct attr_list_entry *)(new_al + entry_offset);
	memset(ale, 0, entry_len);
	/* Form new entry. */
	ale->type = attr->type;
	ale->length = cpu_to_le16(entry_len);
	ale->name_length = attr->name_length;
	ale->name_offset = offsetof(struct attr_list_entry, name);
	if (attr->non_resident)
		ale->lowest_vcn = attr->data.non_resident.lowest_vcn;
	else
		ale->lowest_vcn = 0;
	ale->mft_reference = mref;
	ale->instance = attr->instance;
	memcpy(ale->name, (u8 *)attr + le16_to_cpu(attr->name_offset),
			attr->name_length * sizeof(__le16));

	/* Copy entries from old attribute list to new. */
	memcpy(new_al, ni->attr_list, entry_offset);
	memcpy(new_al + entry_offset + entry_len, ni->attr_list +
			entry_offset, ni->attr_list_size - entry_offset);

	/* Set new runlist. */
	old_al = ni->attr_list;
	ni->attr_list = new_al;
	ni->attr_list_size = ni->attr_list_size + entry_len;

	err = ntfs_attrlist_update(ni);
	if (err) {
		ni->attr_list = old_al;
		ni->attr_list_size -= entry_len;
		goto err_out;
	}
	kvfree(old_al);
	return 0;
err_out:
	kvfree(new_al);
	return err;
}

/*
 * ntfs_attrlist_entry_rm - remove an attribute list attribute entry
 * @ctx:	attribute search context describing the attribute list entry
 *
 * Remove the attribute list entry @ctx->al_entry from the attribute list.
 *
 * Return 0 on success and -errno on error.
 */
int ntfs_attrlist_entry_rm(struct ntfs_attr_search_ctx *ctx)
{
	u8 *new_al;
	int new_al_len;
	struct ntfs_inode *base_ni;
	struct attr_list_entry *ale;

	if (!ctx || !ctx->ntfs_ino || !ctx->al_entry) {
		ntfs_debug("Invalid arguments.\n");
		return -EINVAL;
	}

	if (ctx->base_ntfs_ino)
		base_ni = ctx->base_ntfs_ino;
	else
		base_ni = ctx->ntfs_ino;
	ale = ctx->al_entry;

	ntfs_debug("Entering for inode 0x%llx, attr 0x%x, lowest_vcn %lld.\n",
			(long long)ctx->ntfs_ino->mft_no,
			(unsigned int)le32_to_cpu(ctx->al_entry->type),
			(long long)le64_to_cpu(ctx->al_entry->lowest_vcn));

	if (!NInoAttrList(base_ni)) {
		ntfs_debug("Attribute list isn't present.\n");
		return -ENOENT;
	}

	/* Allocate memory for new attribute list. */
	new_al_len = base_ni->attr_list_size - le16_to_cpu(ale->length);
	new_al = kvzalloc(new_al_len, GFP_NOFS);
	if (!new_al)
		return -ENOMEM;

	/* Copy entries from old attribute list to new. */
	memcpy(new_al, base_ni->attr_list, (u8 *)ale - base_ni->attr_list);
	memcpy(new_al + ((u8 *)ale - base_ni->attr_list), (u8 *)ale + le16_to_cpu(
				ale->length), new_al_len - ((u8 *)ale - base_ni->attr_list));

	/* Set new runlist. */
	kvfree(base_ni->attr_list);
	base_ni->attr_list = new_al;
	base_ni->attr_list_size = new_al_len;

	return ntfs_attrlist_update(base_ni);
}
