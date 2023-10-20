// SPDX-License-Identifier: GPL-2.0
/*
 *
 * Copyright (C) 2019-2021 Paragon Software GmbH, All rights reserved.
 *
 */

#include <linux/fiemap.h>
#include <linux/fs.h>
#include <linux/minmax.h>
#include <linux/vmalloc.h>

#include "debug.h"
#include "ntfs.h"
#include "ntfs_fs.h"
#ifdef CONFIG_NTFS3_LZX_XPRESS
#include "lib/lib.h"
#endif

static struct mft_inode *ni_ins_mi(struct ntfs_inode *ni, struct rb_root *tree,
				   CLST ino, struct rb_node *ins)
{
	struct rb_node **p = &tree->rb_node;
	struct rb_node *pr = NULL;

	while (*p) {
		struct mft_inode *mi;

		pr = *p;
		mi = rb_entry(pr, struct mft_inode, node);
		if (mi->rno > ino)
			p = &pr->rb_left;
		else if (mi->rno < ino)
			p = &pr->rb_right;
		else
			return mi;
	}

	if (!ins)
		return NULL;

	rb_link_node(ins, pr, p);
	rb_insert_color(ins, tree);
	return rb_entry(ins, struct mft_inode, node);
}

/*
 * ni_find_mi - Find mft_inode by record number.
 */
static struct mft_inode *ni_find_mi(struct ntfs_inode *ni, CLST rno)
{
	return ni_ins_mi(ni, &ni->mi_tree, rno, NULL);
}

/*
 * ni_add_mi - Add new mft_inode into ntfs_inode.
 */
static void ni_add_mi(struct ntfs_inode *ni, struct mft_inode *mi)
{
	ni_ins_mi(ni, &ni->mi_tree, mi->rno, &mi->node);
}

/*
 * ni_remove_mi - Remove mft_inode from ntfs_inode.
 */
void ni_remove_mi(struct ntfs_inode *ni, struct mft_inode *mi)
{
	rb_erase(&mi->node, &ni->mi_tree);
}

/*
 * ni_std - Return: Pointer into std_info from primary record.
 */
struct ATTR_STD_INFO *ni_std(struct ntfs_inode *ni)
{
	const struct ATTRIB *attr;

	attr = mi_find_attr(&ni->mi, NULL, ATTR_STD, NULL, 0, NULL);
	return attr ? resident_data_ex(attr, sizeof(struct ATTR_STD_INFO)) :
		      NULL;
}

/*
 * ni_std5
 *
 * Return: Pointer into std_info from primary record.
 */
struct ATTR_STD_INFO5 *ni_std5(struct ntfs_inode *ni)
{
	const struct ATTRIB *attr;

	attr = mi_find_attr(&ni->mi, NULL, ATTR_STD, NULL, 0, NULL);

	return attr ? resident_data_ex(attr, sizeof(struct ATTR_STD_INFO5)) :
		      NULL;
}

/*
 * ni_clear - Clear resources allocated by ntfs_inode.
 */
void ni_clear(struct ntfs_inode *ni)
{
	struct rb_node *node;

	if (!ni->vfs_inode.i_nlink && ni->mi.mrec && is_rec_inuse(ni->mi.mrec))
		ni_delete_all(ni);

	al_destroy(ni);

	for (node = rb_first(&ni->mi_tree); node;) {
		struct rb_node *next = rb_next(node);
		struct mft_inode *mi = rb_entry(node, struct mft_inode, node);

		rb_erase(node, &ni->mi_tree);
		mi_put(mi);
		node = next;
	}

	/* Bad inode always has mode == S_IFREG. */
	if (ni->ni_flags & NI_FLAG_DIR)
		indx_clear(&ni->dir);
	else {
		run_close(&ni->file.run);
#ifdef CONFIG_NTFS3_LZX_XPRESS
		if (ni->file.offs_page) {
			/* On-demand allocated page for offsets. */
			put_page(ni->file.offs_page);
			ni->file.offs_page = NULL;
		}
#endif
	}

	mi_clear(&ni->mi);
}

/*
 * ni_load_mi_ex - Find mft_inode by record number.
 */
int ni_load_mi_ex(struct ntfs_inode *ni, CLST rno, struct mft_inode **mi)
{
	int err;
	struct mft_inode *r;

	r = ni_find_mi(ni, rno);
	if (r)
		goto out;

	err = mi_get(ni->mi.sbi, rno, &r);
	if (err)
		return err;

	ni_add_mi(ni, r);

out:
	if (mi)
		*mi = r;
	return 0;
}

/*
 * ni_load_mi - Load mft_inode corresponded list_entry.
 */
int ni_load_mi(struct ntfs_inode *ni, const struct ATTR_LIST_ENTRY *le,
	       struct mft_inode **mi)
{
	CLST rno;

	if (!le) {
		*mi = &ni->mi;
		return 0;
	}

	rno = ino_get(&le->ref);
	if (rno == ni->mi.rno) {
		*mi = &ni->mi;
		return 0;
	}
	return ni_load_mi_ex(ni, rno, mi);
}

/*
 * ni_find_attr
 *
 * Return: Attribute and record this attribute belongs to.
 */
struct ATTRIB *ni_find_attr(struct ntfs_inode *ni, struct ATTRIB *attr,
			    struct ATTR_LIST_ENTRY **le_o, enum ATTR_TYPE type,
			    const __le16 *name, u8 name_len, const CLST *vcn,
			    struct mft_inode **mi)
{
	struct ATTR_LIST_ENTRY *le;
	struct mft_inode *m;

	if (!ni->attr_list.size ||
	    (!name_len && (type == ATTR_LIST || type == ATTR_STD))) {
		if (le_o)
			*le_o = NULL;
		if (mi)
			*mi = &ni->mi;

		/* Look for required attribute in primary record. */
		return mi_find_attr(&ni->mi, attr, type, name, name_len, NULL);
	}

	/* First look for list entry of required type. */
	le = al_find_ex(ni, le_o ? *le_o : NULL, type, name, name_len, vcn);
	if (!le)
		return NULL;

	if (le_o)
		*le_o = le;

	/* Load record that contains this attribute. */
	if (ni_load_mi(ni, le, &m))
		return NULL;

	/* Look for required attribute. */
	attr = mi_find_attr(m, NULL, type, name, name_len, &le->id);

	if (!attr)
		goto out;

	if (!attr->non_res) {
		if (vcn && *vcn)
			goto out;
	} else if (!vcn) {
		if (attr->nres.svcn)
			goto out;
	} else if (le64_to_cpu(attr->nres.svcn) > *vcn ||
		   *vcn > le64_to_cpu(attr->nres.evcn)) {
		goto out;
	}

	if (mi)
		*mi = m;
	return attr;

out:
	ntfs_inode_err(&ni->vfs_inode, "failed to parse mft record");
	ntfs_set_state(ni->mi.sbi, NTFS_DIRTY_ERROR);
	return NULL;
}

/*
 * ni_enum_attr_ex - Enumerates attributes in ntfs_inode.
 */
struct ATTRIB *ni_enum_attr_ex(struct ntfs_inode *ni, struct ATTRIB *attr,
			       struct ATTR_LIST_ENTRY **le,
			       struct mft_inode **mi)
{
	struct mft_inode *mi2;
	struct ATTR_LIST_ENTRY *le2;

	/* Do we have an attribute list? */
	if (!ni->attr_list.size) {
		*le = NULL;
		if (mi)
			*mi = &ni->mi;
		/* Enum attributes in primary record. */
		return mi_enum_attr(&ni->mi, attr);
	}

	/* Get next list entry. */
	le2 = *le = al_enumerate(ni, attr ? *le : NULL);
	if (!le2)
		return NULL;

	/* Load record that contains the required attribute. */
	if (ni_load_mi(ni, le2, &mi2))
		return NULL;

	if (mi)
		*mi = mi2;

	/* Find attribute in loaded record. */
	return rec_find_attr_le(mi2, le2);
}

/*
 * ni_load_attr - Load attribute that contains given VCN.
 */
struct ATTRIB *ni_load_attr(struct ntfs_inode *ni, enum ATTR_TYPE type,
			    const __le16 *name, u8 name_len, CLST vcn,
			    struct mft_inode **pmi)
{
	struct ATTR_LIST_ENTRY *le;
	struct ATTRIB *attr;
	struct mft_inode *mi;
	struct ATTR_LIST_ENTRY *next;

	if (!ni->attr_list.size) {
		if (pmi)
			*pmi = &ni->mi;
		return mi_find_attr(&ni->mi, NULL, type, name, name_len, NULL);
	}

	le = al_find_ex(ni, NULL, type, name, name_len, NULL);
	if (!le)
		return NULL;

	/*
	 * Unfortunately ATTR_LIST_ENTRY contains only start VCN.
	 * So to find the ATTRIB segment that contains 'vcn' we should
	 * enumerate some entries.
	 */
	if (vcn) {
		for (;; le = next) {
			next = al_find_ex(ni, le, type, name, name_len, NULL);
			if (!next || le64_to_cpu(next->vcn) > vcn)
				break;
		}
	}

	if (ni_load_mi(ni, le, &mi))
		return NULL;

	if (pmi)
		*pmi = mi;

	attr = mi_find_attr(mi, NULL, type, name, name_len, &le->id);
	if (!attr)
		return NULL;

	if (!attr->non_res)
		return attr;

	if (le64_to_cpu(attr->nres.svcn) <= vcn &&
	    vcn <= le64_to_cpu(attr->nres.evcn))
		return attr;

	return NULL;
}

/*
 * ni_load_all_mi - Load all subrecords.
 */
int ni_load_all_mi(struct ntfs_inode *ni)
{
	int err;
	struct ATTR_LIST_ENTRY *le;

	if (!ni->attr_list.size)
		return 0;

	le = NULL;

	while ((le = al_enumerate(ni, le))) {
		CLST rno = ino_get(&le->ref);

		if (rno == ni->mi.rno)
			continue;

		err = ni_load_mi_ex(ni, rno, NULL);
		if (err)
			return err;
	}

	return 0;
}

/*
 * ni_add_subrecord - Allocate + format + attach a new subrecord.
 */
bool ni_add_subrecord(struct ntfs_inode *ni, CLST rno, struct mft_inode **mi)
{
	struct mft_inode *m;

	m = kzalloc(sizeof(struct mft_inode), GFP_NOFS);
	if (!m)
		return false;

	if (mi_format_new(m, ni->mi.sbi, rno, 0, ni->mi.rno == MFT_REC_MFT)) {
		mi_put(m);
		return false;
	}

	mi_get_ref(&ni->mi, &m->mrec->parent_ref);

	ni_add_mi(ni, m);
	*mi = m;
	return true;
}

/*
 * ni_remove_attr - Remove all attributes for the given type/name/id.
 */
int ni_remove_attr(struct ntfs_inode *ni, enum ATTR_TYPE type,
		   const __le16 *name, u8 name_len, bool base_only,
		   const __le16 *id)
{
	int err;
	struct ATTRIB *attr;
	struct ATTR_LIST_ENTRY *le;
	struct mft_inode *mi;
	u32 type_in;
	int diff;

	if (base_only || type == ATTR_LIST || !ni->attr_list.size) {
		attr = mi_find_attr(&ni->mi, NULL, type, name, name_len, id);
		if (!attr)
			return -ENOENT;

		mi_remove_attr(ni, &ni->mi, attr);
		return 0;
	}

	type_in = le32_to_cpu(type);
	le = NULL;

	for (;;) {
		le = al_enumerate(ni, le);
		if (!le)
			return 0;

next_le2:
		diff = le32_to_cpu(le->type) - type_in;
		if (diff < 0)
			continue;

		if (diff > 0)
			return 0;

		if (le->name_len != name_len)
			continue;

		if (name_len &&
		    memcmp(le_name(le), name, name_len * sizeof(short)))
			continue;

		if (id && le->id != *id)
			continue;
		err = ni_load_mi(ni, le, &mi);
		if (err)
			return err;

		al_remove_le(ni, le);

		attr = mi_find_attr(mi, NULL, type, name, name_len, id);
		if (!attr)
			return -ENOENT;

		mi_remove_attr(ni, mi, attr);

		if (PtrOffset(ni->attr_list.le, le) >= ni->attr_list.size)
			return 0;
		goto next_le2;
	}
}

/*
 * ni_ins_new_attr - Insert the attribute into record.
 *
 * Return: Not full constructed attribute or NULL if not possible to create.
 */
static struct ATTRIB *
ni_ins_new_attr(struct ntfs_inode *ni, struct mft_inode *mi,
		struct ATTR_LIST_ENTRY *le, enum ATTR_TYPE type,
		const __le16 *name, u8 name_len, u32 asize, u16 name_off,
		CLST svcn, struct ATTR_LIST_ENTRY **ins_le)
{
	int err;
	struct ATTRIB *attr;
	bool le_added = false;
	struct MFT_REF ref;

	mi_get_ref(mi, &ref);

	if (type != ATTR_LIST && !le && ni->attr_list.size) {
		err = al_add_le(ni, type, name, name_len, svcn, cpu_to_le16(-1),
				&ref, &le);
		if (err) {
			/* No memory or no space. */
			return ERR_PTR(err);
		}
		le_added = true;

		/*
		 * al_add_le -> attr_set_size (list) -> ni_expand_list
		 * which moves some attributes out of primary record
		 * this means that name may point into moved memory
		 * reinit 'name' from le.
		 */
		name = le->name;
	}

	attr = mi_insert_attr(mi, type, name, name_len, asize, name_off);
	if (!attr) {
		if (le_added)
			al_remove_le(ni, le);
		return NULL;
	}

	if (type == ATTR_LIST) {
		/* Attr list is not in list entry array. */
		goto out;
	}

	if (!le)
		goto out;

	/* Update ATTRIB Id and record reference. */
	le->id = attr->id;
	ni->attr_list.dirty = true;
	le->ref = ref;

out:
	if (ins_le)
		*ins_le = le;
	return attr;
}

/*
 * ni_repack
 *
 * Random write access to sparsed or compressed file may result to
 * not optimized packed runs.
 * Here is the place to optimize it.
 */
static int ni_repack(struct ntfs_inode *ni)
{
#if 1
	return 0;
#else
	int err = 0;
	struct ntfs_sb_info *sbi = ni->mi.sbi;
	struct mft_inode *mi, *mi_p = NULL;
	struct ATTRIB *attr = NULL, *attr_p;
	struct ATTR_LIST_ENTRY *le = NULL, *le_p;
	CLST alloc = 0;
	u8 cluster_bits = sbi->cluster_bits;
	CLST svcn, evcn = 0, svcn_p, evcn_p, next_svcn;
	u32 roff, rs = sbi->record_size;
	struct runs_tree run;

	run_init(&run);

	while ((attr = ni_enum_attr_ex(ni, attr, &le, &mi))) {
		if (!attr->non_res)
			continue;

		svcn = le64_to_cpu(attr->nres.svcn);
		if (svcn != le64_to_cpu(le->vcn)) {
			err = -EINVAL;
			break;
		}

		if (!svcn) {
			alloc = le64_to_cpu(attr->nres.alloc_size) >>
				cluster_bits;
			mi_p = NULL;
		} else if (svcn != evcn + 1) {
			err = -EINVAL;
			break;
		}

		evcn = le64_to_cpu(attr->nres.evcn);

		if (svcn > evcn + 1) {
			err = -EINVAL;
			break;
		}

		if (!mi_p) {
			/* Do not try if not enough free space. */
			if (le32_to_cpu(mi->mrec->used) + 8 >= rs)
				continue;

			/* Do not try if last attribute segment. */
			if (evcn + 1 == alloc)
				continue;
			run_close(&run);
		}

		roff = le16_to_cpu(attr->nres.run_off);

		if (roff > le32_to_cpu(attr->size)) {
			err = -EINVAL;
			break;
		}

		err = run_unpack(&run, sbi, ni->mi.rno, svcn, evcn, svcn,
				 Add2Ptr(attr, roff),
				 le32_to_cpu(attr->size) - roff);
		if (err < 0)
			break;

		if (!mi_p) {
			mi_p = mi;
			attr_p = attr;
			svcn_p = svcn;
			evcn_p = evcn;
			le_p = le;
			err = 0;
			continue;
		}

		/*
		 * Run contains data from two records: mi_p and mi
		 * Try to pack in one.
		 */
		err = mi_pack_runs(mi_p, attr_p, &run, evcn + 1 - svcn_p);
		if (err)
			break;

		next_svcn = le64_to_cpu(attr_p->nres.evcn) + 1;

		if (next_svcn >= evcn + 1) {
			/* We can remove this attribute segment. */
			al_remove_le(ni, le);
			mi_remove_attr(NULL, mi, attr);
			le = le_p;
			continue;
		}

		attr->nres.svcn = le->vcn = cpu_to_le64(next_svcn);
		mi->dirty = true;
		ni->attr_list.dirty = true;

		if (evcn + 1 == alloc) {
			err = mi_pack_runs(mi, attr, &run,
					   evcn + 1 - next_svcn);
			if (err)
				break;
			mi_p = NULL;
		} else {
			mi_p = mi;
			attr_p = attr;
			svcn_p = next_svcn;
			evcn_p = evcn;
			le_p = le;
			run_truncate_head(&run, next_svcn);
		}
	}

	if (err) {
		ntfs_inode_warn(&ni->vfs_inode, "repack problem");
		ntfs_set_state(sbi, NTFS_DIRTY_ERROR);

		/* Pack loaded but not packed runs. */
		if (mi_p)
			mi_pack_runs(mi_p, attr_p, &run, evcn_p + 1 - svcn_p);
	}

	run_close(&run);
	return err;
#endif
}

/*
 * ni_try_remove_attr_list
 *
 * Can we remove attribute list?
 * Check the case when primary record contains enough space for all attributes.
 */
static int ni_try_remove_attr_list(struct ntfs_inode *ni)
{
	int err = 0;
	struct ntfs_sb_info *sbi = ni->mi.sbi;
	struct ATTRIB *attr, *attr_list, *attr_ins;
	struct ATTR_LIST_ENTRY *le;
	struct mft_inode *mi;
	u32 asize, free;
	struct MFT_REF ref;
	struct MFT_REC *mrec;
	__le16 id;

	if (!ni->attr_list.dirty)
		return 0;

	err = ni_repack(ni);
	if (err)
		return err;

	attr_list = mi_find_attr(&ni->mi, NULL, ATTR_LIST, NULL, 0, NULL);
	if (!attr_list)
		return 0;

	asize = le32_to_cpu(attr_list->size);

	/* Free space in primary record without attribute list. */
	free = sbi->record_size - le32_to_cpu(ni->mi.mrec->used) + asize;
	mi_get_ref(&ni->mi, &ref);

	le = NULL;
	while ((le = al_enumerate(ni, le))) {
		if (!memcmp(&le->ref, &ref, sizeof(ref)))
			continue;

		if (le->vcn)
			return 0;

		mi = ni_find_mi(ni, ino_get(&le->ref));
		if (!mi)
			return 0;

		attr = mi_find_attr(mi, NULL, le->type, le_name(le),
				    le->name_len, &le->id);
		if (!attr)
			return 0;

		asize = le32_to_cpu(attr->size);
		if (asize > free)
			return 0;

		free -= asize;
	}

	/* Make a copy of primary record to restore if error. */
	mrec = kmemdup(ni->mi.mrec, sbi->record_size, GFP_NOFS);
	if (!mrec)
		return 0; /* Not critical. */

	/* It seems that attribute list can be removed from primary record. */
	mi_remove_attr(NULL, &ni->mi, attr_list);

	/*
	 * Repeat the cycle above and copy all attributes to primary record.
	 * Do not remove original attributes from subrecords!
	 * It should be success!
	 */
	le = NULL;
	while ((le = al_enumerate(ni, le))) {
		if (!memcmp(&le->ref, &ref, sizeof(ref)))
			continue;

		mi = ni_find_mi(ni, ino_get(&le->ref));
		if (!mi) {
			/* Should never happened, 'cause already checked. */
			goto out;
		}

		attr = mi_find_attr(mi, NULL, le->type, le_name(le),
				    le->name_len, &le->id);
		if (!attr) {
			/* Should never happened, 'cause already checked. */
			goto out;
		}
		asize = le32_to_cpu(attr->size);

		/* Insert into primary record. */
		attr_ins = mi_insert_attr(&ni->mi, le->type, le_name(le),
					  le->name_len, asize,
					  le16_to_cpu(attr->name_off));
		if (!attr_ins) {
			/*
			 * No space in primary record (already checked).
			 */
			goto out;
		}

		/* Copy all except id. */
		id = attr_ins->id;
		memcpy(attr_ins, attr, asize);
		attr_ins->id = id;
	}

	/*
	 * Repeat the cycle above and remove all attributes from subrecords.
	 */
	le = NULL;
	while ((le = al_enumerate(ni, le))) {
		if (!memcmp(&le->ref, &ref, sizeof(ref)))
			continue;

		mi = ni_find_mi(ni, ino_get(&le->ref));
		if (!mi)
			continue;

		attr = mi_find_attr(mi, NULL, le->type, le_name(le),
				    le->name_len, &le->id);
		if (!attr)
			continue;

		/* Remove from original record. */
		mi_remove_attr(NULL, mi, attr);
	}

	run_deallocate(sbi, &ni->attr_list.run, true);
	run_close(&ni->attr_list.run);
	ni->attr_list.size = 0;
	kfree(ni->attr_list.le);
	ni->attr_list.le = NULL;
	ni->attr_list.dirty = false;

	kfree(mrec);
	return 0;
out:
	/* Restore primary record. */
	swap(mrec, ni->mi.mrec);
	kfree(mrec);
	return 0;
}

/*
 * ni_create_attr_list - Generates an attribute list for this primary record.
 */
int ni_create_attr_list(struct ntfs_inode *ni)
{
	struct ntfs_sb_info *sbi = ni->mi.sbi;
	int err;
	u32 lsize;
	struct ATTRIB *attr;
	struct ATTRIB *arr_move[7];
	struct ATTR_LIST_ENTRY *le, *le_b[7];
	struct MFT_REC *rec;
	bool is_mft;
	CLST rno = 0;
	struct mft_inode *mi;
	u32 free_b, nb, to_free, rs;
	u16 sz;

	is_mft = ni->mi.rno == MFT_REC_MFT;
	rec = ni->mi.mrec;
	rs = sbi->record_size;

	/*
	 * Skip estimating exact memory requirement.
	 * Looks like one record_size is always enough.
	 */
	le = kmalloc(al_aligned(rs), GFP_NOFS);
	if (!le)
		return -ENOMEM;

	mi_get_ref(&ni->mi, &le->ref);
	ni->attr_list.le = le;

	attr = NULL;
	nb = 0;
	free_b = 0;
	attr = NULL;

	for (; (attr = mi_enum_attr(&ni->mi, attr)); le = Add2Ptr(le, sz)) {
		sz = le_size(attr->name_len);
		le->type = attr->type;
		le->size = cpu_to_le16(sz);
		le->name_len = attr->name_len;
		le->name_off = offsetof(struct ATTR_LIST_ENTRY, name);
		le->vcn = 0;
		if (le != ni->attr_list.le)
			le->ref = ni->attr_list.le->ref;
		le->id = attr->id;

		if (attr->name_len)
			memcpy(le->name, attr_name(attr),
			       sizeof(short) * attr->name_len);
		else if (attr->type == ATTR_STD)
			continue;
		else if (attr->type == ATTR_LIST)
			continue;
		else if (is_mft && attr->type == ATTR_DATA)
			continue;

		if (!nb || nb < ARRAY_SIZE(arr_move)) {
			le_b[nb] = le;
			arr_move[nb++] = attr;
			free_b += le32_to_cpu(attr->size);
		}
	}

	lsize = PtrOffset(ni->attr_list.le, le);
	ni->attr_list.size = lsize;

	to_free = le32_to_cpu(rec->used) + lsize + SIZEOF_RESIDENT;
	if (to_free <= rs) {
		to_free = 0;
	} else {
		to_free -= rs;

		if (to_free > free_b) {
			err = -EINVAL;
			goto out;
		}
	}

	/* Allocate child MFT. */
	err = ntfs_look_free_mft(sbi, &rno, is_mft, ni, &mi);
	if (err)
		goto out;

	err = -EINVAL;
	/* Call mi_remove_attr() in reverse order to keep pointers 'arr_move' valid. */
	while (to_free > 0) {
		struct ATTRIB *b = arr_move[--nb];
		u32 asize = le32_to_cpu(b->size);
		u16 name_off = le16_to_cpu(b->name_off);

		attr = mi_insert_attr(mi, b->type, Add2Ptr(b, name_off),
				      b->name_len, asize, name_off);
		if (!attr)
			goto out;

		mi_get_ref(mi, &le_b[nb]->ref);
		le_b[nb]->id = attr->id;

		/* Copy all except id. */
		memcpy(attr, b, asize);
		attr->id = le_b[nb]->id;

		/* Remove from primary record. */
		if (!mi_remove_attr(NULL, &ni->mi, b))
			goto out;

		if (to_free <= asize)
			break;
		to_free -= asize;
		if (!nb)
			goto out;
	}

	attr = mi_insert_attr(&ni->mi, ATTR_LIST, NULL, 0,
			      lsize + SIZEOF_RESIDENT, SIZEOF_RESIDENT);
	if (!attr)
		goto out;

	attr->non_res = 0;
	attr->flags = 0;
	attr->res.data_size = cpu_to_le32(lsize);
	attr->res.data_off = SIZEOF_RESIDENT_LE;
	attr->res.flags = 0;
	attr->res.res = 0;

	memcpy(resident_data_ex(attr, lsize), ni->attr_list.le, lsize);

	ni->attr_list.dirty = false;

	mark_inode_dirty(&ni->vfs_inode);
	return 0;

out:
	kfree(ni->attr_list.le);
	ni->attr_list.le = NULL;
	ni->attr_list.size = 0;
	return err;
}

/*
 * ni_ins_attr_ext - Add an external attribute to the ntfs_inode.
 */
static int ni_ins_attr_ext(struct ntfs_inode *ni, struct ATTR_LIST_ENTRY *le,
			   enum ATTR_TYPE type, const __le16 *name, u8 name_len,
			   u32 asize, CLST svcn, u16 name_off, bool force_ext,
			   struct ATTRIB **ins_attr, struct mft_inode **ins_mi,
			   struct ATTR_LIST_ENTRY **ins_le)
{
	struct ATTRIB *attr;
	struct mft_inode *mi;
	CLST rno;
	u64 vbo;
	struct rb_node *node;
	int err;
	bool is_mft, is_mft_data;
	struct ntfs_sb_info *sbi = ni->mi.sbi;

	is_mft = ni->mi.rno == MFT_REC_MFT;
	is_mft_data = is_mft && type == ATTR_DATA && !name_len;

	if (asize > sbi->max_bytes_per_attr) {
		err = -EINVAL;
		goto out;
	}

	/*
	 * Standard information and attr_list cannot be made external.
	 * The Log File cannot have any external attributes.
	 */
	if (type == ATTR_STD || type == ATTR_LIST ||
	    ni->mi.rno == MFT_REC_LOG) {
		err = -EINVAL;
		goto out;
	}

	/* Create attribute list if it is not already existed. */
	if (!ni->attr_list.size) {
		err = ni_create_attr_list(ni);
		if (err)
			goto out;
	}

	vbo = is_mft_data ? ((u64)svcn << sbi->cluster_bits) : 0;

	if (force_ext)
		goto insert_ext;

	/* Load all subrecords into memory. */
	err = ni_load_all_mi(ni);
	if (err)
		goto out;

	/* Check each of loaded subrecord. */
	for (node = rb_first(&ni->mi_tree); node; node = rb_next(node)) {
		mi = rb_entry(node, struct mft_inode, node);

		if (is_mft_data &&
		    (mi_enum_attr(mi, NULL) ||
		     vbo <= ((u64)mi->rno << sbi->record_bits))) {
			/* We can't accept this record 'cause MFT's bootstrapping. */
			continue;
		}
		if (is_mft &&
		    mi_find_attr(mi, NULL, ATTR_DATA, NULL, 0, NULL)) {
			/*
			 * This child record already has a ATTR_DATA.
			 * So it can't accept any other records.
			 */
			continue;
		}

		if ((type != ATTR_NAME || name_len) &&
		    mi_find_attr(mi, NULL, type, name, name_len, NULL)) {
			/* Only indexed attributes can share same record. */
			continue;
		}

		/*
		 * Do not try to insert this attribute
		 * if there is no room in record.
		 */
		if (le32_to_cpu(mi->mrec->used) + asize > sbi->record_size)
			continue;

		/* Try to insert attribute into this subrecord. */
		attr = ni_ins_new_attr(ni, mi, le, type, name, name_len, asize,
				       name_off, svcn, ins_le);
		if (!attr)
			continue;
		if (IS_ERR(attr))
			return PTR_ERR(attr);

		if (ins_attr)
			*ins_attr = attr;
		if (ins_mi)
			*ins_mi = mi;
		return 0;
	}

insert_ext:
	/* We have to allocate a new child subrecord. */
	err = ntfs_look_free_mft(sbi, &rno, is_mft_data, ni, &mi);
	if (err)
		goto out;

	if (is_mft_data && vbo <= ((u64)rno << sbi->record_bits)) {
		err = -EINVAL;
		goto out1;
	}

	attr = ni_ins_new_attr(ni, mi, le, type, name, name_len, asize,
			       name_off, svcn, ins_le);
	if (!attr) {
		err = -EINVAL;
		goto out2;
	}

	if (IS_ERR(attr)) {
		err = PTR_ERR(attr);
		goto out2;
	}

	if (ins_attr)
		*ins_attr = attr;
	if (ins_mi)
		*ins_mi = mi;

	return 0;

out2:
	ni_remove_mi(ni, mi);
	mi_put(mi);

out1:
	ntfs_mark_rec_free(sbi, rno, is_mft);

out:
	return err;
}

/*
 * ni_insert_attr - Insert an attribute into the file.
 *
 * If the primary record has room, it will just insert the attribute.
 * If not, it may make the attribute external.
 * For $MFT::Data it may make room for the attribute by
 * making other attributes external.
 *
 * NOTE:
 * The ATTR_LIST and ATTR_STD cannot be made external.
 * This function does not fill new attribute full.
 * It only fills 'size'/'type'/'id'/'name_len' fields.
 */
static int ni_insert_attr(struct ntfs_inode *ni, enum ATTR_TYPE type,
			  const __le16 *name, u8 name_len, u32 asize,
			  u16 name_off, CLST svcn, struct ATTRIB **ins_attr,
			  struct mft_inode **ins_mi,
			  struct ATTR_LIST_ENTRY **ins_le)
{
	struct ntfs_sb_info *sbi = ni->mi.sbi;
	int err;
	struct ATTRIB *attr, *eattr;
	struct MFT_REC *rec;
	bool is_mft;
	struct ATTR_LIST_ENTRY *le;
	u32 list_reserve, max_free, free, used, t32;
	__le16 id;
	u16 t16;

	is_mft = ni->mi.rno == MFT_REC_MFT;
	rec = ni->mi.mrec;

	list_reserve = SIZEOF_NONRESIDENT + 3 * (1 + 2 * sizeof(u32));
	used = le32_to_cpu(rec->used);
	free = sbi->record_size - used;

	if (is_mft && type != ATTR_LIST) {
		/* Reserve space for the ATTRIB list. */
		if (free < list_reserve)
			free = 0;
		else
			free -= list_reserve;
	}

	if (asize <= free) {
		attr = ni_ins_new_attr(ni, &ni->mi, NULL, type, name, name_len,
				       asize, name_off, svcn, ins_le);
		if (IS_ERR(attr)) {
			err = PTR_ERR(attr);
			goto out;
		}

		if (attr) {
			if (ins_attr)
				*ins_attr = attr;
			if (ins_mi)
				*ins_mi = &ni->mi;
			err = 0;
			goto out;
		}
	}

	if (!is_mft || type != ATTR_DATA || svcn) {
		/* This ATTRIB will be external. */
		err = ni_ins_attr_ext(ni, NULL, type, name, name_len, asize,
				      svcn, name_off, false, ins_attr, ins_mi,
				      ins_le);
		goto out;
	}

	/*
	 * Here we have: "is_mft && type == ATTR_DATA && !svcn"
	 *
	 * The first chunk of the $MFT::Data ATTRIB must be the base record.
	 * Evict as many other attributes as possible.
	 */
	max_free = free;

	/* Estimate the result of moving all possible attributes away. */
	attr = NULL;

	while ((attr = mi_enum_attr(&ni->mi, attr))) {
		if (attr->type == ATTR_STD)
			continue;
		if (attr->type == ATTR_LIST)
			continue;
		max_free += le32_to_cpu(attr->size);
	}

	if (max_free < asize + list_reserve) {
		/* Impossible to insert this attribute into primary record. */
		err = -EINVAL;
		goto out;
	}

	/* Start real attribute moving. */
	attr = NULL;

	for (;;) {
		attr = mi_enum_attr(&ni->mi, attr);
		if (!attr) {
			/* We should never be here 'cause we have already check this case. */
			err = -EINVAL;
			goto out;
		}

		/* Skip attributes that MUST be primary record. */
		if (attr->type == ATTR_STD || attr->type == ATTR_LIST)
			continue;

		le = NULL;
		if (ni->attr_list.size) {
			le = al_find_le(ni, NULL, attr);
			if (!le) {
				/* Really this is a serious bug. */
				err = -EINVAL;
				goto out;
			}
		}

		t32 = le32_to_cpu(attr->size);
		t16 = le16_to_cpu(attr->name_off);
		err = ni_ins_attr_ext(ni, le, attr->type, Add2Ptr(attr, t16),
				      attr->name_len, t32, attr_svcn(attr), t16,
				      false, &eattr, NULL, NULL);
		if (err)
			return err;

		id = eattr->id;
		memcpy(eattr, attr, t32);
		eattr->id = id;

		/* Remove from primary record. */
		mi_remove_attr(NULL, &ni->mi, attr);

		/* attr now points to next attribute. */
		if (attr->type == ATTR_END)
			goto out;
	}
	while (asize + list_reserve > sbi->record_size - le32_to_cpu(rec->used))
		;

	attr = ni_ins_new_attr(ni, &ni->mi, NULL, type, name, name_len, asize,
			       name_off, svcn, ins_le);
	if (!attr) {
		err = -EINVAL;
		goto out;
	}

	if (IS_ERR(attr)) {
		err = PTR_ERR(attr);
		goto out;
	}

	if (ins_attr)
		*ins_attr = attr;
	if (ins_mi)
		*ins_mi = &ni->mi;

out:
	return err;
}

/* ni_expand_mft_list - Split ATTR_DATA of $MFT. */
static int ni_expand_mft_list(struct ntfs_inode *ni)
{
	int err = 0;
	struct runs_tree *run = &ni->file.run;
	u32 asize, run_size, done = 0;
	struct ATTRIB *attr;
	struct rb_node *node;
	CLST mft_min, mft_new, svcn, evcn, plen;
	struct mft_inode *mi, *mi_min, *mi_new;
	struct ntfs_sb_info *sbi = ni->mi.sbi;

	/* Find the nearest MFT. */
	mft_min = 0;
	mft_new = 0;
	mi_min = NULL;

	for (node = rb_first(&ni->mi_tree); node; node = rb_next(node)) {
		mi = rb_entry(node, struct mft_inode, node);

		attr = mi_enum_attr(mi, NULL);

		if (!attr) {
			mft_min = mi->rno;
			mi_min = mi;
			break;
		}
	}

	if (ntfs_look_free_mft(sbi, &mft_new, true, ni, &mi_new)) {
		mft_new = 0;
		/* Really this is not critical. */
	} else if (mft_min > mft_new) {
		mft_min = mft_new;
		mi_min = mi_new;
	} else {
		ntfs_mark_rec_free(sbi, mft_new, true);
		mft_new = 0;
		ni_remove_mi(ni, mi_new);
	}

	attr = mi_find_attr(&ni->mi, NULL, ATTR_DATA, NULL, 0, NULL);
	if (!attr) {
		err = -EINVAL;
		goto out;
	}

	asize = le32_to_cpu(attr->size);

	evcn = le64_to_cpu(attr->nres.evcn);
	svcn = bytes_to_cluster(sbi, (u64)(mft_min + 1) << sbi->record_bits);
	if (evcn + 1 >= svcn) {
		err = -EINVAL;
		goto out;
	}

	/*
	 * Split primary attribute [0 evcn] in two parts [0 svcn) + [svcn evcn].
	 *
	 * Update first part of ATTR_DATA in 'primary MFT.
	 */
	err = run_pack(run, 0, svcn, Add2Ptr(attr, SIZEOF_NONRESIDENT),
		       asize - SIZEOF_NONRESIDENT, &plen);
	if (err < 0)
		goto out;

	run_size = ALIGN(err, 8);
	err = 0;

	if (plen < svcn) {
		err = -EINVAL;
		goto out;
	}

	attr->nres.evcn = cpu_to_le64(svcn - 1);
	attr->size = cpu_to_le32(run_size + SIZEOF_NONRESIDENT);
	/* 'done' - How many bytes of primary MFT becomes free. */
	done = asize - run_size - SIZEOF_NONRESIDENT;
	le32_sub_cpu(&ni->mi.mrec->used, done);

	/* Estimate packed size (run_buf=NULL). */
	err = run_pack(run, svcn, evcn + 1 - svcn, NULL, sbi->record_size,
		       &plen);
	if (err < 0)
		goto out;

	run_size = ALIGN(err, 8);
	err = 0;

	if (plen < evcn + 1 - svcn) {
		err = -EINVAL;
		goto out;
	}

	/*
	 * This function may implicitly call expand attr_list.
	 * Insert second part of ATTR_DATA in 'mi_min'.
	 */
	attr = ni_ins_new_attr(ni, mi_min, NULL, ATTR_DATA, NULL, 0,
			       SIZEOF_NONRESIDENT + run_size,
			       SIZEOF_NONRESIDENT, svcn, NULL);
	if (!attr) {
		err = -EINVAL;
		goto out;
	}

	if (IS_ERR(attr)) {
		err = PTR_ERR(attr);
		goto out;
	}

	attr->non_res = 1;
	attr->name_off = SIZEOF_NONRESIDENT_LE;
	attr->flags = 0;

	/* This function can't fail - cause already checked above. */
	run_pack(run, svcn, evcn + 1 - svcn, Add2Ptr(attr, SIZEOF_NONRESIDENT),
		 run_size, &plen);

	attr->nres.svcn = cpu_to_le64(svcn);
	attr->nres.evcn = cpu_to_le64(evcn);
	attr->nres.run_off = cpu_to_le16(SIZEOF_NONRESIDENT);

out:
	if (mft_new) {
		ntfs_mark_rec_free(sbi, mft_new, true);
		ni_remove_mi(ni, mi_new);
	}

	return !err && !done ? -EOPNOTSUPP : err;
}

/*
 * ni_expand_list - Move all possible attributes out of primary record.
 */
int ni_expand_list(struct ntfs_inode *ni)
{
	int err = 0;
	u32 asize, done = 0;
	struct ATTRIB *attr, *ins_attr;
	struct ATTR_LIST_ENTRY *le;
	bool is_mft = ni->mi.rno == MFT_REC_MFT;
	struct MFT_REF ref;

	mi_get_ref(&ni->mi, &ref);
	le = NULL;

	while ((le = al_enumerate(ni, le))) {
		if (le->type == ATTR_STD)
			continue;

		if (memcmp(&ref, &le->ref, sizeof(struct MFT_REF)))
			continue;

		if (is_mft && le->type == ATTR_DATA)
			continue;

		/* Find attribute in primary record. */
		attr = rec_find_attr_le(&ni->mi, le);
		if (!attr) {
			err = -EINVAL;
			goto out;
		}

		asize = le32_to_cpu(attr->size);

		/* Always insert into new record to avoid collisions (deep recursive). */
		err = ni_ins_attr_ext(ni, le, attr->type, attr_name(attr),
				      attr->name_len, asize, attr_svcn(attr),
				      le16_to_cpu(attr->name_off), true,
				      &ins_attr, NULL, NULL);

		if (err)
			goto out;

		memcpy(ins_attr, attr, asize);
		ins_attr->id = le->id;
		/* Remove from primary record. */
		mi_remove_attr(NULL, &ni->mi, attr);

		done += asize;
		goto out;
	}

	if (!is_mft) {
		err = -EFBIG; /* Attr list is too big(?) */
		goto out;
	}

	/* Split MFT data as much as possible. */
	err = ni_expand_mft_list(ni);

out:
	return !err && !done ? -EOPNOTSUPP : err;
}

/*
 * ni_insert_nonresident - Insert new nonresident attribute.
 */
int ni_insert_nonresident(struct ntfs_inode *ni, enum ATTR_TYPE type,
			  const __le16 *name, u8 name_len,
			  const struct runs_tree *run, CLST svcn, CLST len,
			  __le16 flags, struct ATTRIB **new_attr,
			  struct mft_inode **mi, struct ATTR_LIST_ENTRY **le)
{
	int err;
	CLST plen;
	struct ATTRIB *attr;
	bool is_ext = (flags & (ATTR_FLAG_SPARSED | ATTR_FLAG_COMPRESSED)) &&
		      !svcn;
	u32 name_size = ALIGN(name_len * sizeof(short), 8);
	u32 name_off = is_ext ? SIZEOF_NONRESIDENT_EX : SIZEOF_NONRESIDENT;
	u32 run_off = name_off + name_size;
	u32 run_size, asize;
	struct ntfs_sb_info *sbi = ni->mi.sbi;

	/* Estimate packed size (run_buf=NULL). */
	err = run_pack(run, svcn, len, NULL, sbi->max_bytes_per_attr - run_off,
		       &plen);
	if (err < 0)
		goto out;

	run_size = ALIGN(err, 8);

	if (plen < len) {
		err = -EINVAL;
		goto out;
	}

	asize = run_off + run_size;

	if (asize > sbi->max_bytes_per_attr) {
		err = -EINVAL;
		goto out;
	}

	err = ni_insert_attr(ni, type, name, name_len, asize, name_off, svcn,
			     &attr, mi, le);

	if (err)
		goto out;

	attr->non_res = 1;
	attr->name_off = cpu_to_le16(name_off);
	attr->flags = flags;

	/* This function can't fail - cause already checked above. */
	run_pack(run, svcn, len, Add2Ptr(attr, run_off), run_size, &plen);

	attr->nres.svcn = cpu_to_le64(svcn);
	attr->nres.evcn = cpu_to_le64((u64)svcn + len - 1);

	if (new_attr)
		*new_attr = attr;

	*(__le64 *)&attr->nres.run_off = cpu_to_le64(run_off);

	attr->nres.alloc_size =
		svcn ? 0 : cpu_to_le64((u64)len << ni->mi.sbi->cluster_bits);
	attr->nres.data_size = attr->nres.alloc_size;
	attr->nres.valid_size = attr->nres.alloc_size;

	if (is_ext) {
		if (flags & ATTR_FLAG_COMPRESSED)
			attr->nres.c_unit = COMPRESSION_UNIT;
		attr->nres.total_size = attr->nres.alloc_size;
	}

out:
	return err;
}

/*
 * ni_insert_resident - Inserts new resident attribute.
 */
int ni_insert_resident(struct ntfs_inode *ni, u32 data_size,
		       enum ATTR_TYPE type, const __le16 *name, u8 name_len,
		       struct ATTRIB **new_attr, struct mft_inode **mi,
		       struct ATTR_LIST_ENTRY **le)
{
	int err;
	u32 name_size = ALIGN(name_len * sizeof(short), 8);
	u32 asize = SIZEOF_RESIDENT + name_size + ALIGN(data_size, 8);
	struct ATTRIB *attr;

	err = ni_insert_attr(ni, type, name, name_len, asize, SIZEOF_RESIDENT,
			     0, &attr, mi, le);
	if (err)
		return err;

	attr->non_res = 0;
	attr->flags = 0;

	attr->res.data_size = cpu_to_le32(data_size);
	attr->res.data_off = cpu_to_le16(SIZEOF_RESIDENT + name_size);
	if (type == ATTR_NAME) {
		attr->res.flags = RESIDENT_FLAG_INDEXED;

		/* is_attr_indexed(attr)) == true */
		le16_add_cpu(&ni->mi.mrec->hard_links, 1);
		ni->mi.dirty = true;
	}
	attr->res.res = 0;

	if (new_attr)
		*new_attr = attr;

	return 0;
}

/*
 * ni_remove_attr_le - Remove attribute from record.
 */
void ni_remove_attr_le(struct ntfs_inode *ni, struct ATTRIB *attr,
		       struct mft_inode *mi, struct ATTR_LIST_ENTRY *le)
{
	mi_remove_attr(ni, mi, attr);

	if (le)
		al_remove_le(ni, le);
}

/*
 * ni_delete_all - Remove all attributes and frees allocates space.
 *
 * ntfs_evict_inode->ntfs_clear_inode->ni_delete_all (if no links).
 */
int ni_delete_all(struct ntfs_inode *ni)
{
	int err;
	struct ATTR_LIST_ENTRY *le = NULL;
	struct ATTRIB *attr = NULL;
	struct rb_node *node;
	u16 roff;
	u32 asize;
	CLST svcn, evcn;
	struct ntfs_sb_info *sbi = ni->mi.sbi;
	bool nt3 = is_ntfs3(sbi);
	struct MFT_REF ref;

	while ((attr = ni_enum_attr_ex(ni, attr, &le, NULL))) {
		if (!nt3 || attr->name_len) {
			;
		} else if (attr->type == ATTR_REPARSE) {
			mi_get_ref(&ni->mi, &ref);
			ntfs_remove_reparse(sbi, 0, &ref);
		} else if (attr->type == ATTR_ID && !attr->non_res &&
			   le32_to_cpu(attr->res.data_size) >=
				   sizeof(struct GUID)) {
			ntfs_objid_remove(sbi, resident_data(attr));
		}

		if (!attr->non_res)
			continue;

		svcn = le64_to_cpu(attr->nres.svcn);
		evcn = le64_to_cpu(attr->nres.evcn);

		if (evcn + 1 <= svcn)
			continue;

		asize = le32_to_cpu(attr->size);
		roff = le16_to_cpu(attr->nres.run_off);

		if (roff > asize)
			return -EINVAL;

		/* run==1 means unpack and deallocate. */
		run_unpack_ex(RUN_DEALLOCATE, sbi, ni->mi.rno, svcn, evcn, svcn,
			      Add2Ptr(attr, roff), asize - roff);
	}

	if (ni->attr_list.size) {
		run_deallocate(ni->mi.sbi, &ni->attr_list.run, true);
		al_destroy(ni);
	}

	/* Free all subrecords. */
	for (node = rb_first(&ni->mi_tree); node;) {
		struct rb_node *next = rb_next(node);
		struct mft_inode *mi = rb_entry(node, struct mft_inode, node);

		clear_rec_inuse(mi->mrec);
		mi->dirty = true;
		mi_write(mi, 0);

		ntfs_mark_rec_free(sbi, mi->rno, false);
		ni_remove_mi(ni, mi);
		mi_put(mi);
		node = next;
	}

	/* Free base record. */
	clear_rec_inuse(ni->mi.mrec);
	ni->mi.dirty = true;
	err = mi_write(&ni->mi, 0);

	ntfs_mark_rec_free(sbi, ni->mi.rno, false);

	return err;
}

/* ni_fname_name
 *
 * Return: File name attribute by its value.
 */
struct ATTR_FILE_NAME *ni_fname_name(struct ntfs_inode *ni,
				     const struct le_str *uni,
				     const struct MFT_REF *home_dir,
				     struct mft_inode **mi,
				     struct ATTR_LIST_ENTRY **le)
{
	struct ATTRIB *attr = NULL;
	struct ATTR_FILE_NAME *fname;

	if (le)
		*le = NULL;

	/* Enumerate all names. */
next:
	attr = ni_find_attr(ni, attr, le, ATTR_NAME, NULL, 0, NULL, mi);
	if (!attr)
		return NULL;

	fname = resident_data_ex(attr, SIZEOF_ATTRIBUTE_FILENAME);
	if (!fname)
		goto next;

	if (home_dir && memcmp(home_dir, &fname->home, sizeof(*home_dir)))
		goto next;

	if (!uni)
		return fname;

	if (uni->len != fname->name_len)
		goto next;

	if (ntfs_cmp_names(uni->name, uni->len, fname->name, uni->len, NULL,
			   false))
		goto next;
	return fname;
}

/*
 * ni_fname_type
 *
 * Return: File name attribute with given type.
 */
struct ATTR_FILE_NAME *ni_fname_type(struct ntfs_inode *ni, u8 name_type,
				     struct mft_inode **mi,
				     struct ATTR_LIST_ENTRY **le)
{
	struct ATTRIB *attr = NULL;
	struct ATTR_FILE_NAME *fname;

	*le = NULL;

	if (name_type == FILE_NAME_POSIX)
		return NULL;

	/* Enumerate all names. */
	for (;;) {
		attr = ni_find_attr(ni, attr, le, ATTR_NAME, NULL, 0, NULL, mi);
		if (!attr)
			return NULL;

		fname = resident_data_ex(attr, SIZEOF_ATTRIBUTE_FILENAME);
		if (fname && name_type == fname->type)
			return fname;
	}
}

/*
 * ni_new_attr_flags
 *
 * Process compressed/sparsed in special way.
 * NOTE: You need to set ni->std_fa = new_fa
 * after this function to keep internal structures in consistency.
 */
int ni_new_attr_flags(struct ntfs_inode *ni, enum FILE_ATTRIBUTE new_fa)
{
	struct ATTRIB *attr;
	struct mft_inode *mi;
	__le16 new_aflags;
	u32 new_asize;

	attr = ni_find_attr(ni, NULL, NULL, ATTR_DATA, NULL, 0, NULL, &mi);
	if (!attr)
		return -EINVAL;

	new_aflags = attr->flags;

	if (new_fa & FILE_ATTRIBUTE_SPARSE_FILE)
		new_aflags |= ATTR_FLAG_SPARSED;
	else
		new_aflags &= ~ATTR_FLAG_SPARSED;

	if (new_fa & FILE_ATTRIBUTE_COMPRESSED)
		new_aflags |= ATTR_FLAG_COMPRESSED;
	else
		new_aflags &= ~ATTR_FLAG_COMPRESSED;

	if (new_aflags == attr->flags)
		return 0;

	if ((new_aflags & (ATTR_FLAG_COMPRESSED | ATTR_FLAG_SPARSED)) ==
	    (ATTR_FLAG_COMPRESSED | ATTR_FLAG_SPARSED)) {
		ntfs_inode_warn(&ni->vfs_inode,
				"file can't be sparsed and compressed");
		return -EOPNOTSUPP;
	}

	if (!attr->non_res)
		goto out;

	if (attr->nres.data_size) {
		ntfs_inode_warn(
			&ni->vfs_inode,
			"one can change sparsed/compressed only for empty files");
		return -EOPNOTSUPP;
	}

	/* Resize nonresident empty attribute in-place only. */
	new_asize = (new_aflags & (ATTR_FLAG_COMPRESSED | ATTR_FLAG_SPARSED)) ?
			    (SIZEOF_NONRESIDENT_EX + 8) :
			    (SIZEOF_NONRESIDENT + 8);

	if (!mi_resize_attr(mi, attr, new_asize - le32_to_cpu(attr->size)))
		return -EOPNOTSUPP;

	if (new_aflags & ATTR_FLAG_SPARSED) {
		attr->name_off = SIZEOF_NONRESIDENT_EX_LE;
		/* Windows uses 16 clusters per frame but supports one cluster per frame too. */
		attr->nres.c_unit = 0;
		ni->vfs_inode.i_mapping->a_ops = &ntfs_aops;
	} else if (new_aflags & ATTR_FLAG_COMPRESSED) {
		attr->name_off = SIZEOF_NONRESIDENT_EX_LE;
		/* The only allowed: 16 clusters per frame. */
		attr->nres.c_unit = NTFS_LZNT_CUNIT;
		ni->vfs_inode.i_mapping->a_ops = &ntfs_aops_cmpr;
	} else {
		attr->name_off = SIZEOF_NONRESIDENT_LE;
		/* Normal files. */
		attr->nres.c_unit = 0;
		ni->vfs_inode.i_mapping->a_ops = &ntfs_aops;
	}
	attr->nres.run_off = attr->name_off;
out:
	attr->flags = new_aflags;
	mi->dirty = true;

	return 0;
}

/*
 * ni_parse_reparse
 *
 * buffer - memory for reparse buffer header
 */
enum REPARSE_SIGN ni_parse_reparse(struct ntfs_inode *ni, struct ATTRIB *attr,
				   struct REPARSE_DATA_BUFFER *buffer)
{
	const struct REPARSE_DATA_BUFFER *rp = NULL;
	u8 bits;
	u16 len;
	typeof(rp->CompressReparseBuffer) *cmpr;

	/* Try to estimate reparse point. */
	if (!attr->non_res) {
		rp = resident_data_ex(attr, sizeof(struct REPARSE_DATA_BUFFER));
	} else if (le64_to_cpu(attr->nres.data_size) >=
		   sizeof(struct REPARSE_DATA_BUFFER)) {
		struct runs_tree run;

		run_init(&run);

		if (!attr_load_runs_vcn(ni, ATTR_REPARSE, NULL, 0, &run, 0) &&
		    !ntfs_read_run_nb(ni->mi.sbi, &run, 0, buffer,
				      sizeof(struct REPARSE_DATA_BUFFER),
				      NULL)) {
			rp = buffer;
		}

		run_close(&run);
	}

	if (!rp)
		return REPARSE_NONE;

	len = le16_to_cpu(rp->ReparseDataLength);
	switch (rp->ReparseTag) {
	case (IO_REPARSE_TAG_MICROSOFT | IO_REPARSE_TAG_SYMBOLIC_LINK):
		break; /* Symbolic link. */
	case IO_REPARSE_TAG_MOUNT_POINT:
		break; /* Mount points and junctions. */
	case IO_REPARSE_TAG_SYMLINK:
		break;
	case IO_REPARSE_TAG_COMPRESS:
		/*
		 * WOF - Windows Overlay Filter - Used to compress files with
		 * LZX/Xpress.
		 *
		 * Unlike native NTFS file compression, the Windows
		 * Overlay Filter supports only read operations. This means
		 * that it doesn't need to sector-align each compressed chunk,
		 * so the compressed data can be packed more tightly together.
		 * If you open the file for writing, the WOF just decompresses
		 * the entire file, turning it back into a plain file.
		 *
		 * Ntfs3 driver decompresses the entire file only on write or
		 * change size requests.
		 */

		cmpr = &rp->CompressReparseBuffer;
		if (len < sizeof(*cmpr) ||
		    cmpr->WofVersion != WOF_CURRENT_VERSION ||
		    cmpr->WofProvider != WOF_PROVIDER_SYSTEM ||
		    cmpr->ProviderVer != WOF_PROVIDER_CURRENT_VERSION) {
			return REPARSE_NONE;
		}

		switch (cmpr->CompressionFormat) {
		case WOF_COMPRESSION_XPRESS4K:
			bits = 0xc; // 4k
			break;
		case WOF_COMPRESSION_XPRESS8K:
			bits = 0xd; // 8k
			break;
		case WOF_COMPRESSION_XPRESS16K:
			bits = 0xe; // 16k
			break;
		case WOF_COMPRESSION_LZX32K:
			bits = 0xf; // 32k
			break;
		default:
			bits = 0x10; // 64k
			break;
		}
		ni_set_ext_compress_bits(ni, bits);
		return REPARSE_COMPRESSED;

	case IO_REPARSE_TAG_DEDUP:
		ni->ni_flags |= NI_FLAG_DEDUPLICATED;
		return REPARSE_DEDUPLICATED;

	default:
		if (rp->ReparseTag & IO_REPARSE_TAG_NAME_SURROGATE)
			break;

		return REPARSE_NONE;
	}

	if (buffer != rp)
		memcpy(buffer, rp, sizeof(struct REPARSE_DATA_BUFFER));

	/* Looks like normal symlink. */
	return REPARSE_LINK;
}

/*
 * ni_fiemap - Helper for file_fiemap().
 *
 * Assumed ni_lock.
 * TODO: Less aggressive locks.
 */
int ni_fiemap(struct ntfs_inode *ni, struct fiemap_extent_info *fieinfo,
	      __u64 vbo, __u64 len)
{
	int err = 0;
	struct ntfs_sb_info *sbi = ni->mi.sbi;
	u8 cluster_bits = sbi->cluster_bits;
	struct runs_tree *run;
	struct rw_semaphore *run_lock;
	struct ATTRIB *attr;
	CLST vcn = vbo >> cluster_bits;
	CLST lcn, clen;
	u64 valid = ni->i_valid;
	u64 lbo, bytes;
	u64 end, alloc_size;
	size_t idx = -1;
	u32 flags;
	bool ok;

	if (S_ISDIR(ni->vfs_inode.i_mode)) {
		run = &ni->dir.alloc_run;
		attr = ni_find_attr(ni, NULL, NULL, ATTR_ALLOC, I30_NAME,
				    ARRAY_SIZE(I30_NAME), NULL, NULL);
		run_lock = &ni->dir.run_lock;
	} else {
		run = &ni->file.run;
		attr = ni_find_attr(ni, NULL, NULL, ATTR_DATA, NULL, 0, NULL,
				    NULL);
		if (!attr) {
			err = -EINVAL;
			goto out;
		}
		if (is_attr_compressed(attr)) {
			/* Unfortunately cp -r incorrectly treats compressed clusters. */
			err = -EOPNOTSUPP;
			ntfs_inode_warn(
				&ni->vfs_inode,
				"fiemap is not supported for compressed file (cp -r)");
			goto out;
		}
		run_lock = &ni->file.run_lock;
	}

	if (!attr || !attr->non_res) {
		err = fiemap_fill_next_extent(
			fieinfo, 0, 0,
			attr ? le32_to_cpu(attr->res.data_size) : 0,
			FIEMAP_EXTENT_DATA_INLINE | FIEMAP_EXTENT_LAST |
				FIEMAP_EXTENT_MERGED);
		goto out;
	}

	end = vbo + len;
	alloc_size = le64_to_cpu(attr->nres.alloc_size);
	if (end > alloc_size)
		end = alloc_size;

	down_read(run_lock);

	while (vbo < end) {
		if (idx == -1) {
			ok = run_lookup_entry(run, vcn, &lcn, &clen, &idx);
		} else {
			CLST vcn_next = vcn;

			ok = run_get_entry(run, ++idx, &vcn, &lcn, &clen) &&
			     vcn == vcn_next;
			if (!ok)
				vcn = vcn_next;
		}

		if (!ok) {
			up_read(run_lock);
			down_write(run_lock);

			err = attr_load_runs_vcn(ni, attr->type,
						 attr_name(attr),
						 attr->name_len, run, vcn);

			up_write(run_lock);
			down_read(run_lock);

			if (err)
				break;

			ok = run_lookup_entry(run, vcn, &lcn, &clen, &idx);

			if (!ok) {
				err = -EINVAL;
				break;
			}
		}

		if (!clen) {
			err = -EINVAL; // ?
			break;
		}

		if (lcn == SPARSE_LCN) {
			vcn += clen;
			vbo = (u64)vcn << cluster_bits;
			continue;
		}

		flags = FIEMAP_EXTENT_MERGED;
		if (S_ISDIR(ni->vfs_inode.i_mode)) {
			;
		} else if (is_attr_compressed(attr)) {
			CLST clst_data;

			err = attr_is_frame_compressed(
				ni, attr, vcn >> attr->nres.c_unit, &clst_data);
			if (err)
				break;
			if (clst_data < NTFS_LZNT_CLUSTERS)
				flags |= FIEMAP_EXTENT_ENCODED;
		} else if (is_attr_encrypted(attr)) {
			flags |= FIEMAP_EXTENT_DATA_ENCRYPTED;
		}

		vbo = (u64)vcn << cluster_bits;
		bytes = (u64)clen << cluster_bits;
		lbo = (u64)lcn << cluster_bits;

		vcn += clen;

		if (vbo + bytes >= end)
			bytes = end - vbo;

		if (vbo + bytes <= valid) {
			;
		} else if (vbo >= valid) {
			flags |= FIEMAP_EXTENT_UNWRITTEN;
		} else {
			/* vbo < valid && valid < vbo + bytes */
			u64 dlen = valid - vbo;

			if (vbo + dlen >= end)
				flags |= FIEMAP_EXTENT_LAST;

			err = fiemap_fill_next_extent(fieinfo, vbo, lbo, dlen,
						      flags);
			if (err < 0)
				break;
			if (err == 1) {
				err = 0;
				break;
			}

			vbo = valid;
			bytes -= dlen;
			if (!bytes)
				continue;

			lbo += dlen;
			flags |= FIEMAP_EXTENT_UNWRITTEN;
		}

		if (vbo + bytes >= end)
			flags |= FIEMAP_EXTENT_LAST;

		err = fiemap_fill_next_extent(fieinfo, vbo, lbo, bytes, flags);
		if (err < 0)
			break;
		if (err == 1) {
			err = 0;
			break;
		}

		vbo += bytes;
	}

	up_read(run_lock);

out:
	return err;
}

/*
 * ni_readpage_cmpr
 *
 * When decompressing, we typically obtain more than one page per reference.
 * We inject the additional pages into the page cache.
 */
int ni_readpage_cmpr(struct ntfs_inode *ni, struct page *page)
{
	int err;
	struct ntfs_sb_info *sbi = ni->mi.sbi;
	struct address_space *mapping = page->mapping;
	pgoff_t index = page->index;
	u64 frame_vbo, vbo = (u64)index << PAGE_SHIFT;
	struct page **pages = NULL; /* Array of at most 16 pages. stack? */
	u8 frame_bits;
	CLST frame;
	u32 i, idx, frame_size, pages_per_frame;
	gfp_t gfp_mask;
	struct page *pg;

	if (vbo >= ni->vfs_inode.i_size) {
		SetPageUptodate(page);
		err = 0;
		goto out;
	}

	if (ni->ni_flags & NI_FLAG_COMPRESSED_MASK) {
		/* Xpress or LZX. */
		frame_bits = ni_ext_compress_bits(ni);
	} else {
		/* LZNT compression. */
		frame_bits = NTFS_LZNT_CUNIT + sbi->cluster_bits;
	}
	frame_size = 1u << frame_bits;
	frame = vbo >> frame_bits;
	frame_vbo = (u64)frame << frame_bits;
	idx = (vbo - frame_vbo) >> PAGE_SHIFT;

	pages_per_frame = frame_size >> PAGE_SHIFT;
	pages = kcalloc(pages_per_frame, sizeof(struct page *), GFP_NOFS);
	if (!pages) {
		err = -ENOMEM;
		goto out;
	}

	pages[idx] = page;
	index = frame_vbo >> PAGE_SHIFT;
	gfp_mask = mapping_gfp_mask(mapping);

	for (i = 0; i < pages_per_frame; i++, index++) {
		if (i == idx)
			continue;

		pg = find_or_create_page(mapping, index, gfp_mask);
		if (!pg) {
			err = -ENOMEM;
			goto out1;
		}
		pages[i] = pg;
	}

	err = ni_read_frame(ni, frame_vbo, pages, pages_per_frame);

out1:
	if (err)
		SetPageError(page);

	for (i = 0; i < pages_per_frame; i++) {
		pg = pages[i];
		if (i == idx || !pg)
			continue;
		unlock_page(pg);
		put_page(pg);
	}

out:
	/* At this point, err contains 0 or -EIO depending on the "critical" page. */
	kfree(pages);
	unlock_page(page);

	return err;
}

#ifdef CONFIG_NTFS3_LZX_XPRESS
/*
 * ni_decompress_file - Decompress LZX/Xpress compressed file.
 *
 * Remove ATTR_DATA::WofCompressedData.
 * Remove ATTR_REPARSE.
 */
int ni_decompress_file(struct ntfs_inode *ni)
{
	struct ntfs_sb_info *sbi = ni->mi.sbi;
	struct inode *inode = &ni->vfs_inode;
	loff_t i_size = inode->i_size;
	struct address_space *mapping = inode->i_mapping;
	gfp_t gfp_mask = mapping_gfp_mask(mapping);
	struct page **pages = NULL;
	struct ATTR_LIST_ENTRY *le;
	struct ATTRIB *attr;
	CLST vcn, cend, lcn, clen, end;
	pgoff_t index;
	u64 vbo;
	u8 frame_bits;
	u32 i, frame_size, pages_per_frame, bytes;
	struct mft_inode *mi;
	int err;

	/* Clusters for decompressed data. */
	cend = bytes_to_cluster(sbi, i_size);

	if (!i_size)
		goto remove_wof;

	/* Check in advance. */
	if (cend > wnd_zeroes(&sbi->used.bitmap)) {
		err = -ENOSPC;
		goto out;
	}

	frame_bits = ni_ext_compress_bits(ni);
	frame_size = 1u << frame_bits;
	pages_per_frame = frame_size >> PAGE_SHIFT;
	pages = kcalloc(pages_per_frame, sizeof(struct page *), GFP_NOFS);
	if (!pages) {
		err = -ENOMEM;
		goto out;
	}

	/*
	 * Step 1: Decompress data and copy to new allocated clusters.
	 */
	index = 0;
	for (vbo = 0; vbo < i_size; vbo += bytes) {
		u32 nr_pages;
		bool new;

		if (vbo + frame_size > i_size) {
			bytes = i_size - vbo;
			nr_pages = (bytes + PAGE_SIZE - 1) >> PAGE_SHIFT;
		} else {
			nr_pages = pages_per_frame;
			bytes = frame_size;
		}

		end = bytes_to_cluster(sbi, vbo + bytes);

		for (vcn = vbo >> sbi->cluster_bits; vcn < end; vcn += clen) {
			err = attr_data_get_block(ni, vcn, cend - vcn, &lcn,
						  &clen, &new, false);
			if (err)
				goto out;
		}

		for (i = 0; i < pages_per_frame; i++, index++) {
			struct page *pg;

			pg = find_or_create_page(mapping, index, gfp_mask);
			if (!pg) {
				while (i--) {
					unlock_page(pages[i]);
					put_page(pages[i]);
				}
				err = -ENOMEM;
				goto out;
			}
			pages[i] = pg;
		}

		err = ni_read_frame(ni, vbo, pages, pages_per_frame);

		if (!err) {
			down_read(&ni->file.run_lock);
			err = ntfs_bio_pages(sbi, &ni->file.run, pages,
					     nr_pages, vbo, bytes,
					     REQ_OP_WRITE);
			up_read(&ni->file.run_lock);
		}

		for (i = 0; i < pages_per_frame; i++) {
			unlock_page(pages[i]);
			put_page(pages[i]);
		}

		if (err)
			goto out;

		cond_resched();
	}

remove_wof:
	/*
	 * Step 2: Deallocate attributes ATTR_DATA::WofCompressedData
	 * and ATTR_REPARSE.
	 */
	attr = NULL;
	le = NULL;
	while ((attr = ni_enum_attr_ex(ni, attr, &le, NULL))) {
		CLST svcn, evcn;
		u32 asize, roff;

		if (attr->type == ATTR_REPARSE) {
			struct MFT_REF ref;

			mi_get_ref(&ni->mi, &ref);
			ntfs_remove_reparse(sbi, 0, &ref);
		}

		if (!attr->non_res)
			continue;

		if (attr->type != ATTR_REPARSE &&
		    (attr->type != ATTR_DATA ||
		     attr->name_len != ARRAY_SIZE(WOF_NAME) ||
		     memcmp(attr_name(attr), WOF_NAME, sizeof(WOF_NAME))))
			continue;

		svcn = le64_to_cpu(attr->nres.svcn);
		evcn = le64_to_cpu(attr->nres.evcn);

		if (evcn + 1 <= svcn)
			continue;

		asize = le32_to_cpu(attr->size);
		roff = le16_to_cpu(attr->nres.run_off);

		if (roff > asize) {
			err = -EINVAL;
			goto out;
		}

		/*run==1  Means unpack and deallocate. */
		run_unpack_ex(RUN_DEALLOCATE, sbi, ni->mi.rno, svcn, evcn, svcn,
			      Add2Ptr(attr, roff), asize - roff);
	}

	/*
	 * Step 3: Remove attribute ATTR_DATA::WofCompressedData.
	 */
	err = ni_remove_attr(ni, ATTR_DATA, WOF_NAME, ARRAY_SIZE(WOF_NAME),
			     false, NULL);
	if (err)
		goto out;

	/*
	 * Step 4: Remove ATTR_REPARSE.
	 */
	err = ni_remove_attr(ni, ATTR_REPARSE, NULL, 0, false, NULL);
	if (err)
		goto out;

	/*
	 * Step 5: Remove sparse flag from data attribute.
	 */
	attr = ni_find_attr(ni, NULL, NULL, ATTR_DATA, NULL, 0, NULL, &mi);
	if (!attr) {
		err = -EINVAL;
		goto out;
	}

	if (attr->non_res && is_attr_sparsed(attr)) {
		/* Sparsed attribute header is 8 bytes bigger than normal. */
		struct MFT_REC *rec = mi->mrec;
		u32 used = le32_to_cpu(rec->used);
		u32 asize = le32_to_cpu(attr->size);
		u16 roff = le16_to_cpu(attr->nres.run_off);
		char *rbuf = Add2Ptr(attr, roff);

		memmove(rbuf - 8, rbuf, used - PtrOffset(rec, rbuf));
		attr->size = cpu_to_le32(asize - 8);
		attr->flags &= ~ATTR_FLAG_SPARSED;
		attr->nres.run_off = cpu_to_le16(roff - 8);
		attr->nres.c_unit = 0;
		rec->used = cpu_to_le32(used - 8);
		mi->dirty = true;
		ni->std_fa &= ~(FILE_ATTRIBUTE_SPARSE_FILE |
				FILE_ATTRIBUTE_REPARSE_POINT);

		mark_inode_dirty(inode);
	}

	/* Clear cached flag. */
	ni->ni_flags &= ~NI_FLAG_COMPRESSED_MASK;
	if (ni->file.offs_page) {
		put_page(ni->file.offs_page);
		ni->file.offs_page = NULL;
	}
	mapping->a_ops = &ntfs_aops;

out:
	kfree(pages);
	if (err)
		_ntfs_bad_inode(inode);

	return err;
}

/*
 * decompress_lzx_xpress - External compression LZX/Xpress.
 */
static int decompress_lzx_xpress(struct ntfs_sb_info *sbi, const char *cmpr,
				 size_t cmpr_size, void *unc, size_t unc_size,
				 u32 frame_size)
{
	int err;
	void *ctx;

	if (cmpr_size == unc_size) {
		/* Frame not compressed. */
		memcpy(unc, cmpr, unc_size);
		return 0;
	}

	err = 0;
	if (frame_size == 0x8000) {
		mutex_lock(&sbi->compress.mtx_lzx);
		/* LZX: Frame compressed. */
		ctx = sbi->compress.lzx;
		if (!ctx) {
			/* Lazy initialize LZX decompress context. */
			ctx = lzx_allocate_decompressor();
			if (!ctx) {
				err = -ENOMEM;
				goto out1;
			}

			sbi->compress.lzx = ctx;
		}

		if (lzx_decompress(ctx, cmpr, cmpr_size, unc, unc_size)) {
			/* Treat all errors as "invalid argument". */
			err = -EINVAL;
		}
out1:
		mutex_unlock(&sbi->compress.mtx_lzx);
	} else {
		/* XPRESS: Frame compressed. */
		mutex_lock(&sbi->compress.mtx_xpress);
		ctx = sbi->compress.xpress;
		if (!ctx) {
			/* Lazy initialize Xpress decompress context. */
			ctx = xpress_allocate_decompressor();
			if (!ctx) {
				err = -ENOMEM;
				goto out2;
			}

			sbi->compress.xpress = ctx;
		}

		if (xpress_decompress(ctx, cmpr, cmpr_size, unc, unc_size)) {
			/* Treat all errors as "invalid argument". */
			err = -EINVAL;
		}
out2:
		mutex_unlock(&sbi->compress.mtx_xpress);
	}
	return err;
}
#endif

/*
 * ni_read_frame
 *
 * Pages - Array of locked pages.
 */
int ni_read_frame(struct ntfs_inode *ni, u64 frame_vbo, struct page **pages,
		  u32 pages_per_frame)
{
	int err;
	struct ntfs_sb_info *sbi = ni->mi.sbi;
	u8 cluster_bits = sbi->cluster_bits;
	char *frame_ondisk = NULL;
	char *frame_mem = NULL;
	struct page **pages_disk = NULL;
	struct ATTR_LIST_ENTRY *le = NULL;
	struct runs_tree *run = &ni->file.run;
	u64 valid_size = ni->i_valid;
	u64 vbo_disk;
	size_t unc_size;
	u32 frame_size, i, npages_disk, ondisk_size;
	struct page *pg;
	struct ATTRIB *attr;
	CLST frame, clst_data;

	/*
	 * To simplify decompress algorithm do vmap for source
	 * and target pages.
	 */
	for (i = 0; i < pages_per_frame; i++)
		kmap(pages[i]);

	frame_size = pages_per_frame << PAGE_SHIFT;
	frame_mem = vmap(pages, pages_per_frame, VM_MAP, PAGE_KERNEL);
	if (!frame_mem) {
		err = -ENOMEM;
		goto out;
	}

	attr = ni_find_attr(ni, NULL, &le, ATTR_DATA, NULL, 0, NULL, NULL);
	if (!attr) {
		err = -ENOENT;
		goto out1;
	}

	if (!attr->non_res) {
		u32 data_size = le32_to_cpu(attr->res.data_size);

		memset(frame_mem, 0, frame_size);
		if (frame_vbo < data_size) {
			ondisk_size = data_size - frame_vbo;
			memcpy(frame_mem, resident_data(attr) + frame_vbo,
			       min(ondisk_size, frame_size));
		}
		err = 0;
		goto out1;
	}

	if (frame_vbo >= valid_size) {
		memset(frame_mem, 0, frame_size);
		err = 0;
		goto out1;
	}

	if (ni->ni_flags & NI_FLAG_COMPRESSED_MASK) {
#ifndef CONFIG_NTFS3_LZX_XPRESS
		err = -EOPNOTSUPP;
		goto out1;
#else
		u32 frame_bits = ni_ext_compress_bits(ni);
		u64 frame64 = frame_vbo >> frame_bits;
		u64 frames, vbo_data;

		if (frame_size != (1u << frame_bits)) {
			err = -EINVAL;
			goto out1;
		}
		switch (frame_size) {
		case 0x1000:
		case 0x2000:
		case 0x4000:
		case 0x8000:
			break;
		default:
			/* Unknown compression. */
			err = -EOPNOTSUPP;
			goto out1;
		}

		attr = ni_find_attr(ni, attr, &le, ATTR_DATA, WOF_NAME,
				    ARRAY_SIZE(WOF_NAME), NULL, NULL);
		if (!attr) {
			ntfs_inode_err(
				&ni->vfs_inode,
				"external compressed file should contains data attribute \"WofCompressedData\"");
			err = -EINVAL;
			goto out1;
		}

		if (!attr->non_res) {
			run = NULL;
		} else {
			run = run_alloc();
			if (!run) {
				err = -ENOMEM;
				goto out1;
			}
		}

		frames = (ni->vfs_inode.i_size - 1) >> frame_bits;

		err = attr_wof_frame_info(ni, attr, run, frame64, frames,
					  frame_bits, &ondisk_size, &vbo_data);
		if (err)
			goto out2;

		if (frame64 == frames) {
			unc_size = 1 + ((ni->vfs_inode.i_size - 1) &
					(frame_size - 1));
			ondisk_size = attr_size(attr) - vbo_data;
		} else {
			unc_size = frame_size;
		}

		if (ondisk_size > frame_size) {
			err = -EINVAL;
			goto out2;
		}

		if (!attr->non_res) {
			if (vbo_data + ondisk_size >
			    le32_to_cpu(attr->res.data_size)) {
				err = -EINVAL;
				goto out1;
			}

			err = decompress_lzx_xpress(
				sbi, Add2Ptr(resident_data(attr), vbo_data),
				ondisk_size, frame_mem, unc_size, frame_size);
			goto out1;
		}
		vbo_disk = vbo_data;
		/* Load all runs to read [vbo_disk-vbo_to). */
		err = attr_load_runs_range(ni, ATTR_DATA, WOF_NAME,
					   ARRAY_SIZE(WOF_NAME), run, vbo_disk,
					   vbo_data + ondisk_size);
		if (err)
			goto out2;
		npages_disk = (ondisk_size + (vbo_disk & (PAGE_SIZE - 1)) +
			       PAGE_SIZE - 1) >>
			      PAGE_SHIFT;
#endif
	} else if (is_attr_compressed(attr)) {
		/* LZNT compression. */
		if (sbi->cluster_size > NTFS_LZNT_MAX_CLUSTER) {
			err = -EOPNOTSUPP;
			goto out1;
		}

		if (attr->nres.c_unit != NTFS_LZNT_CUNIT) {
			err = -EOPNOTSUPP;
			goto out1;
		}

		down_write(&ni->file.run_lock);
		run_truncate_around(run, le64_to_cpu(attr->nres.svcn));
		frame = frame_vbo >> (cluster_bits + NTFS_LZNT_CUNIT);
		err = attr_is_frame_compressed(ni, attr, frame, &clst_data);
		up_write(&ni->file.run_lock);
		if (err)
			goto out1;

		if (!clst_data) {
			memset(frame_mem, 0, frame_size);
			goto out1;
		}

		frame_size = sbi->cluster_size << NTFS_LZNT_CUNIT;
		ondisk_size = clst_data << cluster_bits;

		if (clst_data >= NTFS_LZNT_CLUSTERS) {
			/* Frame is not compressed. */
			down_read(&ni->file.run_lock);
			err = ntfs_bio_pages(sbi, run, pages, pages_per_frame,
					     frame_vbo, ondisk_size,
					     REQ_OP_READ);
			up_read(&ni->file.run_lock);
			goto out1;
		}
		vbo_disk = frame_vbo;
		npages_disk = (ondisk_size + PAGE_SIZE - 1) >> PAGE_SHIFT;
	} else {
		__builtin_unreachable();
		err = -EINVAL;
		goto out1;
	}

	pages_disk = kzalloc(npages_disk * sizeof(struct page *), GFP_NOFS);
	if (!pages_disk) {
		err = -ENOMEM;
		goto out2;
	}

	for (i = 0; i < npages_disk; i++) {
		pg = alloc_page(GFP_KERNEL);
		if (!pg) {
			err = -ENOMEM;
			goto out3;
		}
		pages_disk[i] = pg;
		lock_page(pg);
		kmap(pg);
	}

	/* Read 'ondisk_size' bytes from disk. */
	down_read(&ni->file.run_lock);
	err = ntfs_bio_pages(sbi, run, pages_disk, npages_disk, vbo_disk,
			     ondisk_size, REQ_OP_READ);
	up_read(&ni->file.run_lock);
	if (err)
		goto out3;

	/*
	 * To simplify decompress algorithm do vmap for source and target pages.
	 */
	frame_ondisk = vmap(pages_disk, npages_disk, VM_MAP, PAGE_KERNEL_RO);
	if (!frame_ondisk) {
		err = -ENOMEM;
		goto out3;
	}

	/* Decompress: Frame_ondisk -> frame_mem. */
#ifdef CONFIG_NTFS3_LZX_XPRESS
	if (run != &ni->file.run) {
		/* LZX or XPRESS */
		err = decompress_lzx_xpress(
			sbi, frame_ondisk + (vbo_disk & (PAGE_SIZE - 1)),
			ondisk_size, frame_mem, unc_size, frame_size);
	} else
#endif
	{
		/* LZNT - Native NTFS compression. */
		unc_size = decompress_lznt(frame_ondisk, ondisk_size, frame_mem,
					   frame_size);
		if ((ssize_t)unc_size < 0)
			err = unc_size;
		else if (!unc_size || unc_size > frame_size)
			err = -EINVAL;
	}
	if (!err && valid_size < frame_vbo + frame_size) {
		size_t ok = valid_size - frame_vbo;

		memset(frame_mem + ok, 0, frame_size - ok);
	}

	vunmap(frame_ondisk);

out3:
	for (i = 0; i < npages_disk; i++) {
		pg = pages_disk[i];
		if (pg) {
			kunmap(pg);
			unlock_page(pg);
			put_page(pg);
		}
	}
	kfree(pages_disk);

out2:
#ifdef CONFIG_NTFS3_LZX_XPRESS
	if (run != &ni->file.run)
		run_free(run);
#endif
out1:
	vunmap(frame_mem);
out:
	for (i = 0; i < pages_per_frame; i++) {
		pg = pages[i];
		kunmap(pg);
		ClearPageError(pg);
		SetPageUptodate(pg);
	}

	return err;
}

/*
 * ni_write_frame
 *
 * Pages - Array of locked pages.
 */
int ni_write_frame(struct ntfs_inode *ni, struct page **pages,
		   u32 pages_per_frame)
{
	int err;
	struct ntfs_sb_info *sbi = ni->mi.sbi;
	u8 frame_bits = NTFS_LZNT_CUNIT + sbi->cluster_bits;
	u32 frame_size = sbi->cluster_size << NTFS_LZNT_CUNIT;
	u64 frame_vbo = (u64)pages[0]->index << PAGE_SHIFT;
	CLST frame = frame_vbo >> frame_bits;
	char *frame_ondisk = NULL;
	struct page **pages_disk = NULL;
	struct ATTR_LIST_ENTRY *le = NULL;
	char *frame_mem;
	struct ATTRIB *attr;
	struct mft_inode *mi;
	u32 i;
	struct page *pg;
	size_t compr_size, ondisk_size;
	struct lznt *lznt;

	attr = ni_find_attr(ni, NULL, &le, ATTR_DATA, NULL, 0, NULL, &mi);
	if (!attr) {
		err = -ENOENT;
		goto out;
	}

	if (WARN_ON(!is_attr_compressed(attr))) {
		err = -EINVAL;
		goto out;
	}

	if (sbi->cluster_size > NTFS_LZNT_MAX_CLUSTER) {
		err = -EOPNOTSUPP;
		goto out;
	}

	if (!attr->non_res) {
		down_write(&ni->file.run_lock);
		err = attr_make_nonresident(ni, attr, le, mi,
					    le32_to_cpu(attr->res.data_size),
					    &ni->file.run, &attr, pages[0]);
		up_write(&ni->file.run_lock);
		if (err)
			goto out;
	}

	if (attr->nres.c_unit != NTFS_LZNT_CUNIT) {
		err = -EOPNOTSUPP;
		goto out;
	}

	pages_disk = kcalloc(pages_per_frame, sizeof(struct page *), GFP_NOFS);
	if (!pages_disk) {
		err = -ENOMEM;
		goto out;
	}

	for (i = 0; i < pages_per_frame; i++) {
		pg = alloc_page(GFP_KERNEL);
		if (!pg) {
			err = -ENOMEM;
			goto out1;
		}
		pages_disk[i] = pg;
		lock_page(pg);
		kmap(pg);
	}

	/* To simplify compress algorithm do vmap for source and target pages. */
	frame_ondisk = vmap(pages_disk, pages_per_frame, VM_MAP, PAGE_KERNEL);
	if (!frame_ondisk) {
		err = -ENOMEM;
		goto out1;
	}

	for (i = 0; i < pages_per_frame; i++)
		kmap(pages[i]);

	/* Map in-memory frame for read-only. */
	frame_mem = vmap(pages, pages_per_frame, VM_MAP, PAGE_KERNEL_RO);
	if (!frame_mem) {
		err = -ENOMEM;
		goto out2;
	}

	mutex_lock(&sbi->compress.mtx_lznt);
	lznt = NULL;
	if (!sbi->compress.lznt) {
		/*
		 * LZNT implements two levels of compression:
		 * 0 - Standard compression
		 * 1 - Best compression, requires a lot of cpu
		 * use mount option?
		 */
		lznt = get_lznt_ctx(0);
		if (!lznt) {
			mutex_unlock(&sbi->compress.mtx_lznt);
			err = -ENOMEM;
			goto out3;
		}

		sbi->compress.lznt = lznt;
		lznt = NULL;
	}

	/* Compress: frame_mem -> frame_ondisk */
	compr_size = compress_lznt(frame_mem, frame_size, frame_ondisk,
				   frame_size, sbi->compress.lznt);
	mutex_unlock(&sbi->compress.mtx_lznt);
	kfree(lznt);

	if (compr_size + sbi->cluster_size > frame_size) {
		/* Frame is not compressed. */
		compr_size = frame_size;
		ondisk_size = frame_size;
	} else if (compr_size) {
		/* Frame is compressed. */
		ondisk_size = ntfs_up_cluster(sbi, compr_size);
		memset(frame_ondisk + compr_size, 0, ondisk_size - compr_size);
	} else {
		/* Frame is sparsed. */
		ondisk_size = 0;
	}

	down_write(&ni->file.run_lock);
	run_truncate_around(&ni->file.run, le64_to_cpu(attr->nres.svcn));
	err = attr_allocate_frame(ni, frame, compr_size, ni->i_valid);
	up_write(&ni->file.run_lock);
	if (err)
		goto out2;

	if (!ondisk_size)
		goto out2;

	down_read(&ni->file.run_lock);
	err = ntfs_bio_pages(sbi, &ni->file.run,
			     ondisk_size < frame_size ? pages_disk : pages,
			     pages_per_frame, frame_vbo, ondisk_size,
			     REQ_OP_WRITE);
	up_read(&ni->file.run_lock);

out3:
	vunmap(frame_mem);

out2:
	for (i = 0; i < pages_per_frame; i++)
		kunmap(pages[i]);

	vunmap(frame_ondisk);
out1:
	for (i = 0; i < pages_per_frame; i++) {
		pg = pages_disk[i];
		if (pg) {
			kunmap(pg);
			unlock_page(pg);
			put_page(pg);
		}
	}
	kfree(pages_disk);
out:
	return err;
}

/*
 * ni_remove_name - Removes name 'de' from MFT and from directory.
 * 'de2' and 'undo_step' are used to restore MFT/dir, if error occurs.
 */
int ni_remove_name(struct ntfs_inode *dir_ni, struct ntfs_inode *ni,
		   struct NTFS_DE *de, struct NTFS_DE **de2, int *undo_step)
{
	int err;
	struct ntfs_sb_info *sbi = ni->mi.sbi;
	struct ATTR_FILE_NAME *de_name = (struct ATTR_FILE_NAME *)(de + 1);
	struct ATTR_FILE_NAME *fname;
	struct ATTR_LIST_ENTRY *le;
	struct mft_inode *mi;
	u16 de_key_size = le16_to_cpu(de->key_size);
	u8 name_type;

	*undo_step = 0;

	/* Find name in record. */
	mi_get_ref(&dir_ni->mi, &de_name->home);

	fname = ni_fname_name(ni, (struct le_str *)&de_name->name_len,
			      &de_name->home, &mi, &le);
	if (!fname)
		return -ENOENT;

	memcpy(&de_name->dup, &fname->dup, sizeof(struct NTFS_DUP_INFO));
	name_type = paired_name(fname->type);

	/* Mark ntfs as dirty. It will be cleared at umount. */
	ntfs_set_state(sbi, NTFS_DIRTY_DIRTY);

	/* Step 1: Remove name from directory. */
	err = indx_delete_entry(&dir_ni->dir, dir_ni, fname, de_key_size, sbi);
	if (err)
		return err;

	/* Step 2: Remove name from MFT. */
	ni_remove_attr_le(ni, attr_from_name(fname), mi, le);

	*undo_step = 2;

	/* Get paired name. */
	fname = ni_fname_type(ni, name_type, &mi, &le);
	if (fname) {
		u16 de2_key_size = fname_full_size(fname);

		*de2 = Add2Ptr(de, 1024);
		(*de2)->key_size = cpu_to_le16(de2_key_size);

		memcpy(*de2 + 1, fname, de2_key_size);

		/* Step 3: Remove paired name from directory. */
		err = indx_delete_entry(&dir_ni->dir, dir_ni, fname,
					de2_key_size, sbi);
		if (err)
			return err;

		/* Step 4: Remove paired name from MFT. */
		ni_remove_attr_le(ni, attr_from_name(fname), mi, le);

		*undo_step = 4;
	}
	return 0;
}

/*
 * ni_remove_name_undo - Paired function for ni_remove_name.
 *
 * Return: True if ok
 */
bool ni_remove_name_undo(struct ntfs_inode *dir_ni, struct ntfs_inode *ni,
			 struct NTFS_DE *de, struct NTFS_DE *de2, int undo_step)
{
	struct ntfs_sb_info *sbi = ni->mi.sbi;
	struct ATTRIB *attr;
	u16 de_key_size;

	switch (undo_step) {
	case 4:
		de_key_size = le16_to_cpu(de2->key_size);
		if (ni_insert_resident(ni, de_key_size, ATTR_NAME, NULL, 0,
				       &attr, NULL, NULL))
			return false;
		memcpy(Add2Ptr(attr, SIZEOF_RESIDENT), de2 + 1, de_key_size);

		mi_get_ref(&ni->mi, &de2->ref);
		de2->size = cpu_to_le16(ALIGN(de_key_size, 8) +
					sizeof(struct NTFS_DE));
		de2->flags = 0;
		de2->res = 0;

		if (indx_insert_entry(&dir_ni->dir, dir_ni, de2, sbi, NULL, 1))
			return false;
		fallthrough;

	case 2:
		de_key_size = le16_to_cpu(de->key_size);

		if (ni_insert_resident(ni, de_key_size, ATTR_NAME, NULL, 0,
				       &attr, NULL, NULL))
			return false;

		memcpy(Add2Ptr(attr, SIZEOF_RESIDENT), de + 1, de_key_size);
		mi_get_ref(&ni->mi, &de->ref);

		if (indx_insert_entry(&dir_ni->dir, dir_ni, de, sbi, NULL, 1))
			return false;
	}

	return true;
}

/*
 * ni_add_name - Add new name into MFT and into directory.
 */
int ni_add_name(struct ntfs_inode *dir_ni, struct ntfs_inode *ni,
		struct NTFS_DE *de)
{
	int err;
	struct ntfs_sb_info *sbi = ni->mi.sbi;
	struct ATTRIB *attr;
	struct ATTR_LIST_ENTRY *le;
	struct mft_inode *mi;
	struct ATTR_FILE_NAME *fname;
	struct ATTR_FILE_NAME *de_name = (struct ATTR_FILE_NAME *)(de + 1);
	u16 de_key_size = le16_to_cpu(de->key_size);

	if (sbi->options->windows_names &&
	    !valid_windows_name(sbi, (struct le_str *)&de_name->name_len))
		return -EINVAL;

	/* If option "hide_dot_files" then set hidden attribute for dot files. */
	if (ni->mi.sbi->options->hide_dot_files) {
		if (de_name->name_len > 0 &&
		    le16_to_cpu(de_name->name[0]) == '.')
			ni->std_fa |= FILE_ATTRIBUTE_HIDDEN;
		else
			ni->std_fa &= ~FILE_ATTRIBUTE_HIDDEN;
	}

	mi_get_ref(&ni->mi, &de->ref);
	mi_get_ref(&dir_ni->mi, &de_name->home);

	/* Fill duplicate from any ATTR_NAME. */
	fname = ni_fname_name(ni, NULL, NULL, NULL, NULL);
	if (fname)
		memcpy(&de_name->dup, &fname->dup, sizeof(fname->dup));
	de_name->dup.fa = ni->std_fa;

	/* Insert new name into MFT. */
	err = ni_insert_resident(ni, de_key_size, ATTR_NAME, NULL, 0, &attr,
				 &mi, &le);
	if (err)
		return err;

	memcpy(Add2Ptr(attr, SIZEOF_RESIDENT), de_name, de_key_size);

	/* Insert new name into directory. */
	err = indx_insert_entry(&dir_ni->dir, dir_ni, de, sbi, NULL, 0);
	if (err)
		ni_remove_attr_le(ni, attr, mi, le);

	return err;
}

/*
 * ni_rename - Remove one name and insert new name.
 */
int ni_rename(struct ntfs_inode *dir_ni, struct ntfs_inode *new_dir_ni,
	      struct ntfs_inode *ni, struct NTFS_DE *de, struct NTFS_DE *new_de,
	      bool *is_bad)
{
	int err;
	struct NTFS_DE *de2 = NULL;
	int undo = 0;

	/*
	 * There are two possible ways to rename:
	 * 1) Add new name and remove old name.
	 * 2) Remove old name and add new name.
	 *
	 * In most cases (not all!) adding new name into MFT and into directory can
	 * allocate additional cluster(s).
	 * Second way may result to bad inode if we can't add new name
	 * and then can't restore (add) old name.
	 */

	/*
	 * Way 1 - Add new + remove old.
	 */
	err = ni_add_name(new_dir_ni, ni, new_de);
	if (!err) {
		err = ni_remove_name(dir_ni, ni, de, &de2, &undo);
		if (err && ni_remove_name(new_dir_ni, ni, new_de, &de2, &undo))
			*is_bad = true;
	}

	/*
	 * Way 2 - Remove old + add new.
	 */
	/*
	 *	err = ni_remove_name(dir_ni, ni, de, &de2, &undo);
	 *	if (!err) {
	 *		err = ni_add_name(new_dir_ni, ni, new_de);
	 *		if (err && !ni_remove_name_undo(dir_ni, ni, de, de2, undo))
	 *			*is_bad = true;
	 *	}
	 */

	return err;
}

/*
 * ni_is_dirty - Return: True if 'ni' requires ni_write_inode.
 */
bool ni_is_dirty(struct inode *inode)
{
	struct ntfs_inode *ni = ntfs_i(inode);
	struct rb_node *node;

	if (ni->mi.dirty || ni->attr_list.dirty ||
	    (ni->ni_flags & NI_FLAG_UPDATE_PARENT))
		return true;

	for (node = rb_first(&ni->mi_tree); node; node = rb_next(node)) {
		if (rb_entry(node, struct mft_inode, node)->dirty)
			return true;
	}

	return false;
}

/*
 * ni_update_parent
 *
 * Update duplicate info of ATTR_FILE_NAME in MFT and in parent directories.
 */
static bool ni_update_parent(struct ntfs_inode *ni, struct NTFS_DUP_INFO *dup,
			     int sync)
{
	struct ATTRIB *attr;
	struct mft_inode *mi;
	struct ATTR_LIST_ENTRY *le = NULL;
	struct ntfs_sb_info *sbi = ni->mi.sbi;
	struct super_block *sb = sbi->sb;
	bool re_dirty = false;

	if (ni->mi.mrec->flags & RECORD_FLAG_DIR) {
		dup->fa |= FILE_ATTRIBUTE_DIRECTORY;
		attr = NULL;
		dup->alloc_size = 0;
		dup->data_size = 0;
	} else {
		dup->fa &= ~FILE_ATTRIBUTE_DIRECTORY;

		attr = ni_find_attr(ni, NULL, &le, ATTR_DATA, NULL, 0, NULL,
				    &mi);
		if (!attr) {
			dup->alloc_size = dup->data_size = 0;
		} else if (!attr->non_res) {
			u32 data_size = le32_to_cpu(attr->res.data_size);

			dup->alloc_size = cpu_to_le64(ALIGN(data_size, 8));
			dup->data_size = cpu_to_le64(data_size);
		} else {
			u64 new_valid = ni->i_valid;
			u64 data_size = le64_to_cpu(attr->nres.data_size);
			__le64 valid_le;

			dup->alloc_size = is_attr_ext(attr) ?
						  attr->nres.total_size :
						  attr->nres.alloc_size;
			dup->data_size = attr->nres.data_size;

			if (new_valid > data_size)
				new_valid = data_size;

			valid_le = cpu_to_le64(new_valid);
			if (valid_le != attr->nres.valid_size) {
				attr->nres.valid_size = valid_le;
				mi->dirty = true;
			}
		}
	}

	/* TODO: Fill reparse info. */
	dup->reparse = 0;
	dup->ea_size = 0;

	if (ni->ni_flags & NI_FLAG_EA) {
		attr = ni_find_attr(ni, attr, &le, ATTR_EA_INFO, NULL, 0, NULL,
				    NULL);
		if (attr) {
			const struct EA_INFO *info;

			info = resident_data_ex(attr, sizeof(struct EA_INFO));
			/* If ATTR_EA_INFO exists 'info' can't be NULL. */
			if (info)
				dup->ea_size = info->size_pack;
		}
	}

	attr = NULL;
	le = NULL;

	while ((attr = ni_find_attr(ni, attr, &le, ATTR_NAME, NULL, 0, NULL,
				    &mi))) {
		struct inode *dir;
		struct ATTR_FILE_NAME *fname;

		fname = resident_data_ex(attr, SIZEOF_ATTRIBUTE_FILENAME);
		if (!fname || !memcmp(&fname->dup, dup, sizeof(fname->dup)))
			continue;

		/* Check simple case when parent inode equals current inode. */
		if (ino_get(&fname->home) == ni->vfs_inode.i_ino) {
			ntfs_set_state(sbi, NTFS_DIRTY_ERROR);
			continue;
		}

		/* ntfs_iget5 may sleep. */
		dir = ntfs_iget5(sb, &fname->home, NULL);
		if (IS_ERR(dir)) {
			ntfs_inode_warn(
				&ni->vfs_inode,
				"failed to open parent directory r=%lx to update",
				(long)ino_get(&fname->home));
			continue;
		}

		if (!is_bad_inode(dir)) {
			struct ntfs_inode *dir_ni = ntfs_i(dir);

			if (!ni_trylock(dir_ni)) {
				re_dirty = true;
			} else {
				indx_update_dup(dir_ni, sbi, fname, dup, sync);
				ni_unlock(dir_ni);
				memcpy(&fname->dup, dup, sizeof(fname->dup));
				mi->dirty = true;
			}
		}
		iput(dir);
	}

	return re_dirty;
}

/*
 * ni_write_inode - Write MFT base record and all subrecords to disk.
 */
int ni_write_inode(struct inode *inode, int sync, const char *hint)
{
	int err = 0, err2;
	struct ntfs_inode *ni = ntfs_i(inode);
	struct super_block *sb = inode->i_sb;
	struct ntfs_sb_info *sbi = sb->s_fs_info;
	bool re_dirty = false;
	struct ATTR_STD_INFO *std;
	struct rb_node *node, *next;
	struct NTFS_DUP_INFO dup;

	if (is_bad_inode(inode) || sb_rdonly(sb))
		return 0;

	if (!ni_trylock(ni)) {
		/* 'ni' is under modification, skip for now. */
		mark_inode_dirty_sync(inode);
		return 0;
	}

	if (!ni->mi.mrec)
		goto out;

	if (is_rec_inuse(ni->mi.mrec) &&
	    !(sbi->flags & NTFS_FLAGS_LOG_REPLAYING) && inode->i_nlink) {
		bool modified = false;
		struct timespec64 ctime = inode_get_ctime(inode);

		/* Update times in standard attribute. */
		std = ni_std(ni);
		if (!std) {
			err = -EINVAL;
			goto out;
		}

		/* Update the access times if they have changed. */
		dup.m_time = kernel2nt(&inode->i_mtime);
		if (std->m_time != dup.m_time) {
			std->m_time = dup.m_time;
			modified = true;
		}

		dup.c_time = kernel2nt(&ctime);
		if (std->c_time != dup.c_time) {
			std->c_time = dup.c_time;
			modified = true;
		}

		dup.a_time = kernel2nt(&inode->i_atime);
		if (std->a_time != dup.a_time) {
			std->a_time = dup.a_time;
			modified = true;
		}

		dup.fa = ni->std_fa;
		if (std->fa != dup.fa) {
			std->fa = dup.fa;
			modified = true;
		}

		/* std attribute is always in primary MFT record. */
		if (modified)
			ni->mi.dirty = true;

		if (!ntfs_is_meta_file(sbi, inode->i_ino) &&
		    (modified || (ni->ni_flags & NI_FLAG_UPDATE_PARENT))
		    /* Avoid __wait_on_freeing_inode(inode). */
		    && (sb->s_flags & SB_ACTIVE)) {
			dup.cr_time = std->cr_time;
			/* Not critical if this function fail. */
			re_dirty = ni_update_parent(ni, &dup, sync);

			if (re_dirty)
				ni->ni_flags |= NI_FLAG_UPDATE_PARENT;
			else
				ni->ni_flags &= ~NI_FLAG_UPDATE_PARENT;
		}

		/* Update attribute list. */
		if (ni->attr_list.size && ni->attr_list.dirty) {
			if (inode->i_ino != MFT_REC_MFT || sync) {
				err = ni_try_remove_attr_list(ni);
				if (err)
					goto out;
			}

			err = al_update(ni, sync);
			if (err)
				goto out;
		}
	}

	for (node = rb_first(&ni->mi_tree); node; node = next) {
		struct mft_inode *mi = rb_entry(node, struct mft_inode, node);
		bool is_empty;

		next = rb_next(node);

		if (!mi->dirty)
			continue;

		is_empty = !mi_enum_attr(mi, NULL);

		if (is_empty)
			clear_rec_inuse(mi->mrec);

		err2 = mi_write(mi, sync);
		if (!err && err2)
			err = err2;

		if (is_empty) {
			ntfs_mark_rec_free(sbi, mi->rno, false);
			rb_erase(node, &ni->mi_tree);
			mi_put(mi);
		}
	}

	if (ni->mi.dirty) {
		err2 = mi_write(&ni->mi, sync);
		if (!err && err2)
			err = err2;
	}
out:
	ni_unlock(ni);

	if (err) {
		ntfs_inode_err(inode, "%s failed, %d.", hint, err);
		ntfs_set_state(sbi, NTFS_DIRTY_ERROR);
		return err;
	}

	if (re_dirty)
		mark_inode_dirty_sync(inode);

	return 0;
}
