// SPDX-License-Identifier: GPL-2.0
/*
 *
 * Copyright (C) 2019-2021 Paragon Software GmbH, All rights reserved.
 *
 */

#include <linux/fs.h>

#include "debug.h"
#include "ntfs.h"
#include "ntfs_fs.h"

/*
 * al_is_valid_le
 *
 * Return: True if @le is valid.
 */
static inline bool al_is_valid_le(const struct ntfs_inode *ni,
				  struct ATTR_LIST_ENTRY *le)
{
	if (!le || !ni->attr_list.le || !ni->attr_list.size)
		return false;

	return PtrOffset(ni->attr_list.le, le) + le16_to_cpu(le->size) <=
	       ni->attr_list.size;
}

void al_destroy(struct ntfs_inode *ni)
{
	run_close(&ni->attr_list.run);
	kfree(ni->attr_list.le);
	ni->attr_list.le = NULL;
	ni->attr_list.size = 0;
	ni->attr_list.dirty = false;
}

/*
 * ntfs_load_attr_list
 *
 * This method makes sure that the ATTRIB list, if present,
 * has been properly set up.
 */
int ntfs_load_attr_list(struct ntfs_inode *ni, struct ATTRIB *attr)
{
	int err;
	size_t lsize;
	void *le = NULL;

	if (ni->attr_list.size)
		return 0;

	if (!attr->non_res) {
		lsize = le32_to_cpu(attr->res.data_size);
		le = kmalloc(al_aligned(lsize), GFP_NOFS);
		if (!le) {
			err = -ENOMEM;
			goto out;
		}
		memcpy(le, resident_data(attr), lsize);
	} else if (attr->nres.svcn) {
		err = -EINVAL;
		goto out;
	} else {
		u16 run_off = le16_to_cpu(attr->nres.run_off);

		lsize = le64_to_cpu(attr->nres.data_size);

		run_init(&ni->attr_list.run);

		err = run_unpack_ex(&ni->attr_list.run, ni->mi.sbi, ni->mi.rno,
				    0, le64_to_cpu(attr->nres.evcn), 0,
				    Add2Ptr(attr, run_off),
				    le32_to_cpu(attr->size) - run_off);
		if (err < 0)
			goto out;

		le = kmalloc(al_aligned(lsize), GFP_NOFS);
		if (!le) {
			err = -ENOMEM;
			goto out;
		}

		err = ntfs_read_run_nb(ni->mi.sbi, &ni->attr_list.run, 0, le,
				       lsize, NULL);
		if (err)
			goto out;
	}

	ni->attr_list.size = lsize;
	ni->attr_list.le = le;

	return 0;

out:
	ni->attr_list.le = le;
	al_destroy(ni);

	return err;
}

/*
 * al_enumerate
 *
 * Return:
 * * The next list le.
 * * If @le is NULL then return the first le.
 */
struct ATTR_LIST_ENTRY *al_enumerate(struct ntfs_inode *ni,
				     struct ATTR_LIST_ENTRY *le)
{
	size_t off;
	u16 sz;

	if (!le) {
		le = ni->attr_list.le;
	} else {
		sz = le16_to_cpu(le->size);
		if (sz < sizeof(struct ATTR_LIST_ENTRY)) {
			/* Impossible 'cause we should not return such le. */
			return NULL;
		}
		le = Add2Ptr(le, sz);
	}

	/* Check boundary. */
	off = PtrOffset(ni->attr_list.le, le);
	if (off + sizeof(struct ATTR_LIST_ENTRY) > ni->attr_list.size) {
		/* The regular end of list. */
		return NULL;
	}

	sz = le16_to_cpu(le->size);

	/* Check le for errors. */
	if (sz < sizeof(struct ATTR_LIST_ENTRY) ||
	    off + sz > ni->attr_list.size ||
	    sz < le->name_off + le->name_len * sizeof(short)) {
		return NULL;
	}

	return le;
}

/*
 * al_find_le
 *
 * Find the first le in the list which matches type, name and VCN.
 *
 * Return: NULL if not found.
 */
struct ATTR_LIST_ENTRY *al_find_le(struct ntfs_inode *ni,
				   struct ATTR_LIST_ENTRY *le,
				   const struct ATTRIB *attr)
{
	CLST svcn = attr_svcn(attr);

	return al_find_ex(ni, le, attr->type, attr_name(attr), attr->name_len,
			  &svcn);
}

/*
 * al_find_ex
 *
 * Find the first le in the list which matches type, name and VCN.
 *
 * Return: NULL if not found.
 */
struct ATTR_LIST_ENTRY *al_find_ex(struct ntfs_inode *ni,
				   struct ATTR_LIST_ENTRY *le,
				   enum ATTR_TYPE type, const __le16 *name,
				   u8 name_len, const CLST *vcn)
{
	struct ATTR_LIST_ENTRY *ret = NULL;
	u32 type_in = le32_to_cpu(type);

	while ((le = al_enumerate(ni, le))) {
		u64 le_vcn;
		int diff = le32_to_cpu(le->type) - type_in;

		/* List entries are sorted by type, name and VCN. */
		if (diff < 0)
			continue;

		if (diff > 0)
			return ret;

		if (le->name_len != name_len)
			continue;

		le_vcn = le64_to_cpu(le->vcn);
		if (!le_vcn) {
			/*
			 * Compare entry names only for entry with vcn == 0.
			 */
			diff = ntfs_cmp_names(le_name(le), name_len, name,
					      name_len, ni->mi.sbi->upcase,
					      true);
			if (diff < 0)
				continue;

			if (diff > 0)
				return ret;
		}

		if (!vcn)
			return le;

		if (*vcn == le_vcn)
			return le;

		if (*vcn < le_vcn)
			return ret;

		ret = le;
	}

	return ret;
}

/*
 * al_find_le_to_insert
 *
 * Find the first list entry which matches type, name and VCN.
 */
static struct ATTR_LIST_ENTRY *al_find_le_to_insert(struct ntfs_inode *ni,
						    enum ATTR_TYPE type,
						    const __le16 *name,
						    u8 name_len, CLST vcn)
{
	struct ATTR_LIST_ENTRY *le = NULL, *prev;
	u32 type_in = le32_to_cpu(type);

	/* List entries are sorted by type, name and VCN. */
	while ((le = al_enumerate(ni, prev = le))) {
		int diff = le32_to_cpu(le->type) - type_in;

		if (diff < 0)
			continue;

		if (diff > 0)
			return le;

		if (!le->vcn) {
			/*
			 * Compare entry names only for entry with vcn == 0.
			 */
			diff = ntfs_cmp_names(le_name(le), le->name_len, name,
					      name_len, ni->mi.sbi->upcase,
					      true);
			if (diff < 0)
				continue;

			if (diff > 0)
				return le;
		}

		if (le64_to_cpu(le->vcn) >= vcn)
			return le;
	}

	return prev ? Add2Ptr(prev, le16_to_cpu(prev->size)) : ni->attr_list.le;
}

/*
 * al_add_le
 *
 * Add an "attribute list entry" to the list.
 */
int al_add_le(struct ntfs_inode *ni, enum ATTR_TYPE type, const __le16 *name,
	      u8 name_len, CLST svcn, __le16 id, const struct MFT_REF *ref,
	      struct ATTR_LIST_ENTRY **new_le)
{
	int err;
	struct ATTRIB *attr;
	struct ATTR_LIST_ENTRY *le;
	size_t off;
	u16 sz;
	size_t asize, new_asize, old_size;
	u64 new_size;
	typeof(ni->attr_list) *al = &ni->attr_list;

	/*
	 * Compute the size of the new 'le'
	 */
	sz = le_size(name_len);
	old_size = al->size;
	new_size = old_size + sz;
	asize = al_aligned(old_size);
	new_asize = al_aligned(new_size);

	/* Scan forward to the point at which the new 'le' should be inserted. */
	le = al_find_le_to_insert(ni, type, name, name_len, svcn);
	off = PtrOffset(al->le, le);

	if (new_size > asize) {
		void *ptr = kmalloc(new_asize, GFP_NOFS);

		if (!ptr)
			return -ENOMEM;

		memcpy(ptr, al->le, off);
		memcpy(Add2Ptr(ptr, off + sz), le, old_size - off);
		le = Add2Ptr(ptr, off);
		kfree(al->le);
		al->le = ptr;
	} else {
		memmove(Add2Ptr(le, sz), le, old_size - off);
	}
	*new_le = le;

	al->size = new_size;

	le->type = type;
	le->size = cpu_to_le16(sz);
	le->name_len = name_len;
	le->name_off = offsetof(struct ATTR_LIST_ENTRY, name);
	le->vcn = cpu_to_le64(svcn);
	le->ref = *ref;
	le->id = id;
	memcpy(le->name, name, sizeof(short) * name_len);

	err = attr_set_size(ni, ATTR_LIST, NULL, 0, &al->run, new_size,
			    &new_size, true, &attr);
	if (err) {
		/* Undo memmove above. */
		memmove(le, Add2Ptr(le, sz), old_size - off);
		al->size = old_size;
		return err;
	}

	al->dirty = true;

	if (attr && attr->non_res) {
		err = ntfs_sb_write_run(ni->mi.sbi, &al->run, 0, al->le,
					al->size);
		if (err)
			return err;
		al->dirty = false;
	}

	return 0;
}

/*
 * al_remove_le - Remove @le from attribute list.
 */
bool al_remove_le(struct ntfs_inode *ni, struct ATTR_LIST_ENTRY *le)
{
	u16 size;
	size_t off;
	typeof(ni->attr_list) *al = &ni->attr_list;

	if (!al_is_valid_le(ni, le))
		return false;

	/* Save on stack the size of 'le' */
	size = le16_to_cpu(le->size);
	off = PtrOffset(al->le, le);

	memmove(le, Add2Ptr(le, size), al->size - (off + size));

	al->size -= size;
	al->dirty = true;

	return true;
}

/*
 * al_delete_le - Delete first le from the list which matches its parameters.
 */
bool al_delete_le(struct ntfs_inode *ni, enum ATTR_TYPE type, CLST vcn,
		  const __le16 *name, size_t name_len,
		  const struct MFT_REF *ref)
{
	u16 size;
	struct ATTR_LIST_ENTRY *le;
	size_t off;
	typeof(ni->attr_list) *al = &ni->attr_list;

	/* Scan forward to the first le that matches the input. */
	le = al_find_ex(ni, NULL, type, name, name_len, &vcn);
	if (!le)
		return false;

	off = PtrOffset(al->le, le);

next:
	if (off >= al->size)
		return false;
	if (le->type != type)
		return false;
	if (le->name_len != name_len)
		return false;
	if (name_len && ntfs_cmp_names(le_name(le), name_len, name, name_len,
				       ni->mi.sbi->upcase, true))
		return false;
	if (le64_to_cpu(le->vcn) != vcn)
		return false;

	/*
	 * The caller specified a segment reference, so we have to
	 * scan through the matching entries until we find that segment
	 * reference or we run of matching entries.
	 */
	if (ref && memcmp(ref, &le->ref, sizeof(*ref))) {
		off += le16_to_cpu(le->size);
		le = Add2Ptr(al->le, off);
		goto next;
	}

	/* Save on stack the size of 'le'. */
	size = le16_to_cpu(le->size);
	/* Delete the le. */
	memmove(le, Add2Ptr(le, size), al->size - (off + size));

	al->size -= size;
	al->dirty = true;

	return true;
}

int al_update(struct ntfs_inode *ni)
{
	int err;
	struct ATTRIB *attr;
	typeof(ni->attr_list) *al = &ni->attr_list;

	if (!al->dirty || !al->size)
		return 0;

	/*
	 * Attribute list increased on demand in al_add_le.
	 * Attribute list decreased here.
	 */
	err = attr_set_size(ni, ATTR_LIST, NULL, 0, &al->run, al->size, NULL,
			    false, &attr);
	if (err)
		goto out;

	if (!attr->non_res) {
		memcpy(resident_data(attr), al->le, al->size);
	} else {
		err = ntfs_sb_write_run(ni->mi.sbi, &al->run, 0, al->le,
					al->size);
		if (err)
			goto out;

		attr->nres.valid_size = attr->nres.data_size;
	}

	ni->mi.dirty = true;
	al->dirty = false;

out:
	return err;
}
