// SPDX-License-Identifier: GPL-2.0
/*
 *
 * Copyright (C) 2019-2021 Paragon Software GmbH, All rights reserved.
 *
 * TODO: Merge attr_set_size/attr_data_get_block/attr_allocate_frame?
 */

#include <linux/blkdev.h>
#include <linux/buffer_head.h>
#include <linux/fs.h>
#include <linux/hash.h>
#include <linux/nls.h>
#include <linux/ratelimit.h>
#include <linux/slab.h>

#include "debug.h"
#include "ntfs.h"
#include "ntfs_fs.h"

/*
 * You can set external NTFS_MIN_LOG2_OF_CLUMP/NTFS_MAX_LOG2_OF_CLUMP to manage
 * preallocate algorithm.
 */
#ifndef NTFS_MIN_LOG2_OF_CLUMP
#define NTFS_MIN_LOG2_OF_CLUMP 16
#endif

#ifndef NTFS_MAX_LOG2_OF_CLUMP
#define NTFS_MAX_LOG2_OF_CLUMP 26
#endif

// 16M
#define NTFS_CLUMP_MIN (1 << (NTFS_MIN_LOG2_OF_CLUMP + 8))
// 16G
#define NTFS_CLUMP_MAX (1ull << (NTFS_MAX_LOG2_OF_CLUMP + 8))

static inline u64 get_pre_allocated(u64 size)
{
	u32 clump;
	u8 align_shift;
	u64 ret;

	if (size <= NTFS_CLUMP_MIN) {
		clump = 1 << NTFS_MIN_LOG2_OF_CLUMP;
		align_shift = NTFS_MIN_LOG2_OF_CLUMP;
	} else if (size >= NTFS_CLUMP_MAX) {
		clump = 1 << NTFS_MAX_LOG2_OF_CLUMP;
		align_shift = NTFS_MAX_LOG2_OF_CLUMP;
	} else {
		align_shift = NTFS_MIN_LOG2_OF_CLUMP - 1 +
			      __ffs(size >> (8 + NTFS_MIN_LOG2_OF_CLUMP));
		clump = 1u << align_shift;
	}

	ret = (((size + clump - 1) >> align_shift)) << align_shift;

	return ret;
}

/*
 * attr_must_be_resident
 *
 * Return: True if attribute must be resident.
 */
static inline bool attr_must_be_resident(struct ntfs_sb_info *sbi,
					 enum ATTR_TYPE type)
{
	const struct ATTR_DEF_ENTRY *de;

	switch (type) {
	case ATTR_STD:
	case ATTR_NAME:
	case ATTR_ID:
	case ATTR_LABEL:
	case ATTR_VOL_INFO:
	case ATTR_ROOT:
	case ATTR_EA_INFO:
		return true;
	default:
		de = ntfs_query_def(sbi, type);
		if (de && (de->flags & NTFS_ATTR_MUST_BE_RESIDENT))
			return true;
		return false;
	}
}

/*
 * attr_load_runs - Load all runs stored in @attr.
 */
int attr_load_runs(struct ATTRIB *attr, struct ntfs_inode *ni,
		   struct runs_tree *run, const CLST *vcn)
{
	int err;
	CLST svcn = le64_to_cpu(attr->nres.svcn);
	CLST evcn = le64_to_cpu(attr->nres.evcn);
	u32 asize;
	u16 run_off;

	if (svcn >= evcn + 1 || run_is_mapped_full(run, svcn, evcn))
		return 0;

	if (vcn && (evcn < *vcn || *vcn < svcn))
		return -EINVAL;

	asize = le32_to_cpu(attr->size);
	run_off = le16_to_cpu(attr->nres.run_off);
	err = run_unpack_ex(run, ni->mi.sbi, ni->mi.rno, svcn, evcn,
			    vcn ? *vcn : svcn, Add2Ptr(attr, run_off),
			    asize - run_off);
	if (err < 0)
		return err;

	return 0;
}

/*
 * run_deallocate_ex - Deallocate clusters.
 */
static int run_deallocate_ex(struct ntfs_sb_info *sbi, struct runs_tree *run,
			     CLST vcn, CLST len, CLST *done, bool trim)
{
	int err = 0;
	CLST vcn_next, vcn0 = vcn, lcn, clen, dn = 0;
	size_t idx;

	if (!len)
		goto out;

	if (!run_lookup_entry(run, vcn, &lcn, &clen, &idx)) {
failed:
		run_truncate(run, vcn0);
		err = -EINVAL;
		goto out;
	}

	for (;;) {
		if (clen > len)
			clen = len;

		if (!clen) {
			err = -EINVAL;
			goto out;
		}

		if (lcn != SPARSE_LCN) {
			mark_as_free_ex(sbi, lcn, clen, trim);
			dn += clen;
		}

		len -= clen;
		if (!len)
			break;

		vcn_next = vcn + clen;
		if (!run_get_entry(run, ++idx, &vcn, &lcn, &clen) ||
		    vcn != vcn_next) {
			/* Save memory - don't load entire run. */
			goto failed;
		}
	}

out:
	if (done)
		*done += dn;

	return err;
}

/*
 * attr_allocate_clusters - Find free space, mark it as used and store in @run.
 */
int attr_allocate_clusters(struct ntfs_sb_info *sbi, struct runs_tree *run,
			   CLST vcn, CLST lcn, CLST len, CLST *pre_alloc,
			   enum ALLOCATE_OPT opt, CLST *alen, const size_t fr,
			   CLST *new_lcn)
{
	int err;
	CLST flen, vcn0 = vcn, pre = pre_alloc ? *pre_alloc : 0;
	struct wnd_bitmap *wnd = &sbi->used.bitmap;
	size_t cnt = run->count;

	for (;;) {
		err = ntfs_look_for_free_space(sbi, lcn, len + pre, &lcn, &flen,
					       opt);

		if (err == -ENOSPC && pre) {
			pre = 0;
			if (*pre_alloc)
				*pre_alloc = 0;
			continue;
		}

		if (err)
			goto out;

		if (new_lcn && vcn == vcn0)
			*new_lcn = lcn;

		/* Add new fragment into run storage. */
		if (!run_add_entry(run, vcn, lcn, flen, opt == ALLOCATE_MFT)) {
			down_write_nested(&wnd->rw_lock, BITMAP_MUTEX_CLUSTERS);
			wnd_set_free(wnd, lcn, flen);
			up_write(&wnd->rw_lock);
			err = -ENOMEM;
			goto out;
		}

		vcn += flen;

		if (flen >= len || opt == ALLOCATE_MFT ||
		    (fr && run->count - cnt >= fr)) {
			*alen = vcn - vcn0;
			return 0;
		}

		len -= flen;
	}

out:
	/* Undo 'ntfs_look_for_free_space' */
	if (vcn - vcn0) {
		run_deallocate_ex(sbi, run, vcn0, vcn - vcn0, NULL, false);
		run_truncate(run, vcn0);
	}

	return err;
}

/*
 * attr_make_nonresident
 *
 * If page is not NULL - it is already contains resident data
 * and locked (called from ni_write_frame()).
 */
int attr_make_nonresident(struct ntfs_inode *ni, struct ATTRIB *attr,
			  struct ATTR_LIST_ENTRY *le, struct mft_inode *mi,
			  u64 new_size, struct runs_tree *run,
			  struct ATTRIB **ins_attr, struct page *page)
{
	struct ntfs_sb_info *sbi;
	struct ATTRIB *attr_s;
	struct MFT_REC *rec;
	u32 used, asize, rsize, aoff, align;
	bool is_data;
	CLST len, alen;
	char *next;
	int err;

	if (attr->non_res) {
		*ins_attr = attr;
		return 0;
	}

	sbi = mi->sbi;
	rec = mi->mrec;
	attr_s = NULL;
	used = le32_to_cpu(rec->used);
	asize = le32_to_cpu(attr->size);
	next = Add2Ptr(attr, asize);
	aoff = PtrOffset(rec, attr);
	rsize = le32_to_cpu(attr->res.data_size);
	is_data = attr->type == ATTR_DATA && !attr->name_len;

	align = sbi->cluster_size;
	if (is_attr_compressed(attr))
		align <<= COMPRESSION_UNIT;
	len = (rsize + align - 1) >> sbi->cluster_bits;

	run_init(run);

	/* Make a copy of original attribute. */
	attr_s = kmemdup(attr, asize, GFP_NOFS);
	if (!attr_s) {
		err = -ENOMEM;
		goto out;
	}

	if (!len) {
		/* Empty resident -> Empty nonresident. */
		alen = 0;
	} else {
		const char *data = resident_data(attr);

		err = attr_allocate_clusters(sbi, run, 0, 0, len, NULL,
					     ALLOCATE_DEF, &alen, 0, NULL);
		if (err)
			goto out1;

		if (!rsize) {
			/* Empty resident -> Non empty nonresident. */
		} else if (!is_data) {
			err = ntfs_sb_write_run(sbi, run, 0, data, rsize);
			if (err)
				goto out2;
		} else if (!page) {
			char *kaddr;

			page = grab_cache_page(ni->vfs_inode.i_mapping, 0);
			if (!page) {
				err = -ENOMEM;
				goto out2;
			}
			kaddr = kmap_atomic(page);
			memcpy(kaddr, data, rsize);
			memset(kaddr + rsize, 0, PAGE_SIZE - rsize);
			kunmap_atomic(kaddr);
			flush_dcache_page(page);
			SetPageUptodate(page);
			set_page_dirty(page);
			unlock_page(page);
			put_page(page);
		}
	}

	/* Remove original attribute. */
	used -= asize;
	memmove(attr, Add2Ptr(attr, asize), used - aoff);
	rec->used = cpu_to_le32(used);
	mi->dirty = true;
	if (le)
		al_remove_le(ni, le);

	err = ni_insert_nonresident(ni, attr_s->type, attr_name(attr_s),
				    attr_s->name_len, run, 0, alen,
				    attr_s->flags, &attr, NULL);
	if (err)
		goto out3;

	kfree(attr_s);
	attr->nres.data_size = cpu_to_le64(rsize);
	attr->nres.valid_size = attr->nres.data_size;

	*ins_attr = attr;

	if (is_data)
		ni->ni_flags &= ~NI_FLAG_RESIDENT;

	/* Resident attribute becomes non resident. */
	return 0;

out3:
	attr = Add2Ptr(rec, aoff);
	memmove(next, attr, used - aoff);
	memcpy(attr, attr_s, asize);
	rec->used = cpu_to_le32(used + asize);
	mi->dirty = true;
out2:
	/* Undo: do not trim new allocated clusters. */
	run_deallocate(sbi, run, false);
	run_close(run);
out1:
	kfree(attr_s);
	/* Reinsert le. */
out:
	return err;
}

/*
 * attr_set_size_res - Helper for attr_set_size().
 */
static int attr_set_size_res(struct ntfs_inode *ni, struct ATTRIB *attr,
			     struct ATTR_LIST_ENTRY *le, struct mft_inode *mi,
			     u64 new_size, struct runs_tree *run,
			     struct ATTRIB **ins_attr)
{
	struct ntfs_sb_info *sbi = mi->sbi;
	struct MFT_REC *rec = mi->mrec;
	u32 used = le32_to_cpu(rec->used);
	u32 asize = le32_to_cpu(attr->size);
	u32 aoff = PtrOffset(rec, attr);
	u32 rsize = le32_to_cpu(attr->res.data_size);
	u32 tail = used - aoff - asize;
	char *next = Add2Ptr(attr, asize);
	s64 dsize = ALIGN(new_size, 8) - ALIGN(rsize, 8);

	if (dsize < 0) {
		memmove(next + dsize, next, tail);
	} else if (dsize > 0) {
		if (used + dsize > sbi->max_bytes_per_attr)
			return attr_make_nonresident(ni, attr, le, mi, new_size,
						     run, ins_attr, NULL);

		memmove(next + dsize, next, tail);
		memset(next, 0, dsize);
	}

	if (new_size > rsize)
		memset(Add2Ptr(resident_data(attr), rsize), 0,
		       new_size - rsize);

	rec->used = cpu_to_le32(used + dsize);
	attr->size = cpu_to_le32(asize + dsize);
	attr->res.data_size = cpu_to_le32(new_size);
	mi->dirty = true;
	*ins_attr = attr;

	return 0;
}

/*
 * attr_set_size - Change the size of attribute.
 *
 * Extend:
 *   - Sparse/compressed: No allocated clusters.
 *   - Normal: Append allocated and preallocated new clusters.
 * Shrink:
 *   - No deallocate if @keep_prealloc is set.
 */
int attr_set_size(struct ntfs_inode *ni, enum ATTR_TYPE type,
		  const __le16 *name, u8 name_len, struct runs_tree *run,
		  u64 new_size, const u64 *new_valid, bool keep_prealloc,
		  struct ATTRIB **ret)
{
	int err = 0;
	struct ntfs_sb_info *sbi = ni->mi.sbi;
	u8 cluster_bits = sbi->cluster_bits;
	bool is_mft =
		ni->mi.rno == MFT_REC_MFT && type == ATTR_DATA && !name_len;
	u64 old_valid, old_size, old_alloc, new_alloc, new_alloc_tmp;
	struct ATTRIB *attr = NULL, *attr_b;
	struct ATTR_LIST_ENTRY *le, *le_b;
	struct mft_inode *mi, *mi_b;
	CLST alen, vcn, lcn, new_alen, old_alen, svcn, evcn;
	CLST next_svcn, pre_alloc = -1, done = 0;
	bool is_ext;
	u32 align;
	struct MFT_REC *rec;

again:
	le_b = NULL;
	attr_b = ni_find_attr(ni, NULL, &le_b, type, name, name_len, NULL,
			      &mi_b);
	if (!attr_b) {
		err = -ENOENT;
		goto out;
	}

	if (!attr_b->non_res) {
		err = attr_set_size_res(ni, attr_b, le_b, mi_b, new_size, run,
					&attr_b);
		if (err || !attr_b->non_res)
			goto out;

		/* Layout of records may be changed, so do a full search. */
		goto again;
	}

	is_ext = is_attr_ext(attr_b);

again_1:
	align = sbi->cluster_size;

	if (is_ext) {
		align <<= attr_b->nres.c_unit;
		if (is_attr_sparsed(attr_b))
			keep_prealloc = false;
	}

	old_valid = le64_to_cpu(attr_b->nres.valid_size);
	old_size = le64_to_cpu(attr_b->nres.data_size);
	old_alloc = le64_to_cpu(attr_b->nres.alloc_size);
	old_alen = old_alloc >> cluster_bits;

	new_alloc = (new_size + align - 1) & ~(u64)(align - 1);
	new_alen = new_alloc >> cluster_bits;

	if (keep_prealloc && is_ext)
		keep_prealloc = false;

	if (keep_prealloc && new_size < old_size) {
		attr_b->nres.data_size = cpu_to_le64(new_size);
		mi_b->dirty = true;
		goto ok;
	}

	vcn = old_alen - 1;

	svcn = le64_to_cpu(attr_b->nres.svcn);
	evcn = le64_to_cpu(attr_b->nres.evcn);

	if (svcn <= vcn && vcn <= evcn) {
		attr = attr_b;
		le = le_b;
		mi = mi_b;
	} else if (!le_b) {
		err = -EINVAL;
		goto out;
	} else {
		le = le_b;
		attr = ni_find_attr(ni, attr_b, &le, type, name, name_len, &vcn,
				    &mi);
		if (!attr) {
			err = -EINVAL;
			goto out;
		}

next_le_1:
		svcn = le64_to_cpu(attr->nres.svcn);
		evcn = le64_to_cpu(attr->nres.evcn);
	}

next_le:
	rec = mi->mrec;

	err = attr_load_runs(attr, ni, run, NULL);
	if (err)
		goto out;

	if (new_size > old_size) {
		CLST to_allocate;
		size_t free;

		if (new_alloc <= old_alloc) {
			attr_b->nres.data_size = cpu_to_le64(new_size);
			mi_b->dirty = true;
			goto ok;
		}

		to_allocate = new_alen - old_alen;
add_alloc_in_same_attr_seg:
		lcn = 0;
		if (is_mft) {
			/* MFT allocates clusters from MFT zone. */
			pre_alloc = 0;
		} else if (is_ext) {
			/* No preallocate for sparse/compress. */
			pre_alloc = 0;
		} else if (pre_alloc == -1) {
			pre_alloc = 0;
			if (type == ATTR_DATA && !name_len &&
			    sbi->options.prealloc) {
				CLST new_alen2 = bytes_to_cluster(
					sbi, get_pre_allocated(new_size));
				pre_alloc = new_alen2 - new_alen;
			}

			/* Get the last LCN to allocate from. */
			if (old_alen &&
			    !run_lookup_entry(run, vcn, &lcn, NULL, NULL)) {
				lcn = SPARSE_LCN;
			}

			if (lcn == SPARSE_LCN)
				lcn = 0;
			else if (lcn)
				lcn += 1;

			free = wnd_zeroes(&sbi->used.bitmap);
			if (to_allocate > free) {
				err = -ENOSPC;
				goto out;
			}

			if (pre_alloc && to_allocate + pre_alloc > free)
				pre_alloc = 0;
		}

		vcn = old_alen;

		if (is_ext) {
			if (!run_add_entry(run, vcn, SPARSE_LCN, to_allocate,
					   false)) {
				err = -ENOMEM;
				goto out;
			}
			alen = to_allocate;
		} else {
			/* ~3 bytes per fragment. */
			err = attr_allocate_clusters(
				sbi, run, vcn, lcn, to_allocate, &pre_alloc,
				is_mft ? ALLOCATE_MFT : 0, &alen,
				is_mft ? 0
				       : (sbi->record_size -
					  le32_to_cpu(rec->used) + 8) /
							 3 +
						 1,
				NULL);
			if (err)
				goto out;
		}

		done += alen;
		vcn += alen;
		if (to_allocate > alen)
			to_allocate -= alen;
		else
			to_allocate = 0;

pack_runs:
		err = mi_pack_runs(mi, attr, run, vcn - svcn);
		if (err)
			goto out;

		next_svcn = le64_to_cpu(attr->nres.evcn) + 1;
		new_alloc_tmp = (u64)next_svcn << cluster_bits;
		attr_b->nres.alloc_size = cpu_to_le64(new_alloc_tmp);
		mi_b->dirty = true;

		if (next_svcn >= vcn && !to_allocate) {
			/* Normal way. Update attribute and exit. */
			attr_b->nres.data_size = cpu_to_le64(new_size);
			goto ok;
		}

		/* At least two MFT to avoid recursive loop. */
		if (is_mft && next_svcn == vcn &&
		    ((u64)done << sbi->cluster_bits) >= 2 * sbi->record_size) {
			new_size = new_alloc_tmp;
			attr_b->nres.data_size = attr_b->nres.alloc_size;
			goto ok;
		}

		if (le32_to_cpu(rec->used) < sbi->record_size) {
			old_alen = next_svcn;
			evcn = old_alen - 1;
			goto add_alloc_in_same_attr_seg;
		}

		attr_b->nres.data_size = attr_b->nres.alloc_size;
		if (new_alloc_tmp < old_valid)
			attr_b->nres.valid_size = attr_b->nres.data_size;

		if (type == ATTR_LIST) {
			err = ni_expand_list(ni);
			if (err)
				goto out;
			if (next_svcn < vcn)
				goto pack_runs;

			/* Layout of records is changed. */
			goto again;
		}

		if (!ni->attr_list.size) {
			err = ni_create_attr_list(ni);
			if (err)
				goto out;
			/* Layout of records is changed. */
		}

		if (next_svcn >= vcn) {
			/* This is MFT data, repeat. */
			goto again;
		}

		/* Insert new attribute segment. */
		err = ni_insert_nonresident(ni, type, name, name_len, run,
					    next_svcn, vcn - next_svcn,
					    attr_b->flags, &attr, &mi);
		if (err)
			goto out;

		if (!is_mft)
			run_truncate_head(run, evcn + 1);

		svcn = le64_to_cpu(attr->nres.svcn);
		evcn = le64_to_cpu(attr->nres.evcn);

		le_b = NULL;
		/*
		 * Layout of records maybe changed.
		 * Find base attribute to update.
		 */
		attr_b = ni_find_attr(ni, NULL, &le_b, type, name, name_len,
				      NULL, &mi_b);
		if (!attr_b) {
			err = -ENOENT;
			goto out;
		}

		attr_b->nres.alloc_size = cpu_to_le64((u64)vcn << cluster_bits);
		attr_b->nres.data_size = attr_b->nres.alloc_size;
		attr_b->nres.valid_size = attr_b->nres.alloc_size;
		mi_b->dirty = true;
		goto again_1;
	}

	if (new_size != old_size ||
	    (new_alloc != old_alloc && !keep_prealloc)) {
		vcn = max(svcn, new_alen);
		new_alloc_tmp = (u64)vcn << cluster_bits;

		alen = 0;
		err = run_deallocate_ex(sbi, run, vcn, evcn - vcn + 1, &alen,
					true);
		if (err)
			goto out;

		run_truncate(run, vcn);

		if (vcn > svcn) {
			err = mi_pack_runs(mi, attr, run, vcn - svcn);
			if (err)
				goto out;
		} else if (le && le->vcn) {
			u16 le_sz = le16_to_cpu(le->size);

			/*
			 * NOTE: List entries for one attribute are always
			 * the same size. We deal with last entry (vcn==0)
			 * and it is not first in entries array
			 * (list entry for std attribute always first).
			 * So it is safe to step back.
			 */
			mi_remove_attr(NULL, mi, attr);

			if (!al_remove_le(ni, le)) {
				err = -EINVAL;
				goto out;
			}

			le = (struct ATTR_LIST_ENTRY *)((u8 *)le - le_sz);
		} else {
			attr->nres.evcn = cpu_to_le64((u64)vcn - 1);
			mi->dirty = true;
		}

		attr_b->nres.alloc_size = cpu_to_le64(new_alloc_tmp);

		if (vcn == new_alen) {
			attr_b->nres.data_size = cpu_to_le64(new_size);
			if (new_size < old_valid)
				attr_b->nres.valid_size =
					attr_b->nres.data_size;
		} else {
			if (new_alloc_tmp <=
			    le64_to_cpu(attr_b->nres.data_size))
				attr_b->nres.data_size =
					attr_b->nres.alloc_size;
			if (new_alloc_tmp <
			    le64_to_cpu(attr_b->nres.valid_size))
				attr_b->nres.valid_size =
					attr_b->nres.alloc_size;
		}

		if (is_ext)
			le64_sub_cpu(&attr_b->nres.total_size,
				     ((u64)alen << cluster_bits));

		mi_b->dirty = true;

		if (new_alloc_tmp <= new_alloc)
			goto ok;

		old_size = new_alloc_tmp;
		vcn = svcn - 1;

		if (le == le_b) {
			attr = attr_b;
			mi = mi_b;
			evcn = svcn - 1;
			svcn = 0;
			goto next_le;
		}

		if (le->type != type || le->name_len != name_len ||
		    memcmp(le_name(le), name, name_len * sizeof(short))) {
			err = -EINVAL;
			goto out;
		}

		err = ni_load_mi(ni, le, &mi);
		if (err)
			goto out;

		attr = mi_find_attr(mi, NULL, type, name, name_len, &le->id);
		if (!attr) {
			err = -EINVAL;
			goto out;
		}
		goto next_le_1;
	}

ok:
	if (new_valid) {
		__le64 valid = cpu_to_le64(min(*new_valid, new_size));

		if (attr_b->nres.valid_size != valid) {
			attr_b->nres.valid_size = valid;
			mi_b->dirty = true;
		}
	}

out:
	if (!err && attr_b && ret)
		*ret = attr_b;

	/* Update inode_set_bytes. */
	if (!err && ((type == ATTR_DATA && !name_len) ||
		     (type == ATTR_ALLOC && name == I30_NAME))) {
		bool dirty = false;

		if (ni->vfs_inode.i_size != new_size) {
			ni->vfs_inode.i_size = new_size;
			dirty = true;
		}

		if (attr_b && attr_b->non_res) {
			new_alloc = le64_to_cpu(attr_b->nres.alloc_size);
			if (inode_get_bytes(&ni->vfs_inode) != new_alloc) {
				inode_set_bytes(&ni->vfs_inode, new_alloc);
				dirty = true;
			}
		}

		if (dirty) {
			ni->ni_flags |= NI_FLAG_UPDATE_PARENT;
			mark_inode_dirty(&ni->vfs_inode);
		}
	}

	return err;
}

int attr_data_get_block(struct ntfs_inode *ni, CLST vcn, CLST clen, CLST *lcn,
			CLST *len, bool *new)
{
	int err = 0;
	struct runs_tree *run = &ni->file.run;
	struct ntfs_sb_info *sbi;
	u8 cluster_bits;
	struct ATTRIB *attr = NULL, *attr_b;
	struct ATTR_LIST_ENTRY *le, *le_b;
	struct mft_inode *mi, *mi_b;
	CLST hint, svcn, to_alloc, evcn1, next_svcn, asize, end;
	u64 total_size;
	u32 clst_per_frame;
	bool ok;

	if (new)
		*new = false;

	down_read(&ni->file.run_lock);
	ok = run_lookup_entry(run, vcn, lcn, len, NULL);
	up_read(&ni->file.run_lock);

	if (ok && (*lcn != SPARSE_LCN || !new)) {
		/* Normal way. */
		return 0;
	}

	if (!clen)
		clen = 1;

	if (ok && clen > *len)
		clen = *len;

	sbi = ni->mi.sbi;
	cluster_bits = sbi->cluster_bits;

	ni_lock(ni);
	down_write(&ni->file.run_lock);

	le_b = NULL;
	attr_b = ni_find_attr(ni, NULL, &le_b, ATTR_DATA, NULL, 0, NULL, &mi_b);
	if (!attr_b) {
		err = -ENOENT;
		goto out;
	}

	if (!attr_b->non_res) {
		*lcn = RESIDENT_LCN;
		*len = 1;
		goto out;
	}

	asize = le64_to_cpu(attr_b->nres.alloc_size) >> sbi->cluster_bits;
	if (vcn >= asize) {
		err = -EINVAL;
		goto out;
	}

	clst_per_frame = 1u << attr_b->nres.c_unit;
	to_alloc = (clen + clst_per_frame - 1) & ~(clst_per_frame - 1);

	if (vcn + to_alloc > asize)
		to_alloc = asize - vcn;

	svcn = le64_to_cpu(attr_b->nres.svcn);
	evcn1 = le64_to_cpu(attr_b->nres.evcn) + 1;

	attr = attr_b;
	le = le_b;
	mi = mi_b;

	if (le_b && (vcn < svcn || evcn1 <= vcn)) {
		attr = ni_find_attr(ni, attr_b, &le, ATTR_DATA, NULL, 0, &vcn,
				    &mi);
		if (!attr) {
			err = -EINVAL;
			goto out;
		}
		svcn = le64_to_cpu(attr->nres.svcn);
		evcn1 = le64_to_cpu(attr->nres.evcn) + 1;
	}

	err = attr_load_runs(attr, ni, run, NULL);
	if (err)
		goto out;

	if (!ok) {
		ok = run_lookup_entry(run, vcn, lcn, len, NULL);
		if (ok && (*lcn != SPARSE_LCN || !new)) {
			/* Normal way. */
			err = 0;
			goto ok;
		}

		if (!ok && !new) {
			*len = 0;
			err = 0;
			goto ok;
		}

		if (ok && clen > *len) {
			clen = *len;
			to_alloc = (clen + clst_per_frame - 1) &
				   ~(clst_per_frame - 1);
		}
	}

	if (!is_attr_ext(attr_b)) {
		err = -EINVAL;
		goto out;
	}

	/* Get the last LCN to allocate from. */
	hint = 0;

	if (vcn > evcn1) {
		if (!run_add_entry(run, evcn1, SPARSE_LCN, vcn - evcn1,
				   false)) {
			err = -ENOMEM;
			goto out;
		}
	} else if (vcn && !run_lookup_entry(run, vcn - 1, &hint, NULL, NULL)) {
		hint = -1;
	}

	err = attr_allocate_clusters(
		sbi, run, vcn, hint + 1, to_alloc, NULL, 0, len,
		(sbi->record_size - le32_to_cpu(mi->mrec->used) + 8) / 3 + 1,
		lcn);
	if (err)
		goto out;
	*new = true;

	end = vcn + *len;

	total_size = le64_to_cpu(attr_b->nres.total_size) +
		     ((u64)*len << cluster_bits);

repack:
	err = mi_pack_runs(mi, attr, run, max(end, evcn1) - svcn);
	if (err)
		goto out;

	attr_b->nres.total_size = cpu_to_le64(total_size);
	inode_set_bytes(&ni->vfs_inode, total_size);
	ni->ni_flags |= NI_FLAG_UPDATE_PARENT;

	mi_b->dirty = true;
	mark_inode_dirty(&ni->vfs_inode);

	/* Stored [vcn : next_svcn) from [vcn : end). */
	next_svcn = le64_to_cpu(attr->nres.evcn) + 1;

	if (end <= evcn1) {
		if (next_svcn == evcn1) {
			/* Normal way. Update attribute and exit. */
			goto ok;
		}
		/* Add new segment [next_svcn : evcn1 - next_svcn). */
		if (!ni->attr_list.size) {
			err = ni_create_attr_list(ni);
			if (err)
				goto out;
			/* Layout of records is changed. */
			le_b = NULL;
			attr_b = ni_find_attr(ni, NULL, &le_b, ATTR_DATA, NULL,
					      0, NULL, &mi_b);
			if (!attr_b) {
				err = -ENOENT;
				goto out;
			}

			attr = attr_b;
			le = le_b;
			mi = mi_b;
			goto repack;
		}
	}

	svcn = evcn1;

	/* Estimate next attribute. */
	attr = ni_find_attr(ni, attr, &le, ATTR_DATA, NULL, 0, &svcn, &mi);

	if (attr) {
		CLST alloc = bytes_to_cluster(
			sbi, le64_to_cpu(attr_b->nres.alloc_size));
		CLST evcn = le64_to_cpu(attr->nres.evcn);

		if (end < next_svcn)
			end = next_svcn;
		while (end > evcn) {
			/* Remove segment [svcn : evcn). */
			mi_remove_attr(NULL, mi, attr);

			if (!al_remove_le(ni, le)) {
				err = -EINVAL;
				goto out;
			}

			if (evcn + 1 >= alloc) {
				/* Last attribute segment. */
				evcn1 = evcn + 1;
				goto ins_ext;
			}

			if (ni_load_mi(ni, le, &mi)) {
				attr = NULL;
				goto out;
			}

			attr = mi_find_attr(mi, NULL, ATTR_DATA, NULL, 0,
					    &le->id);
			if (!attr) {
				err = -EINVAL;
				goto out;
			}
			svcn = le64_to_cpu(attr->nres.svcn);
			evcn = le64_to_cpu(attr->nres.evcn);
		}

		if (end < svcn)
			end = svcn;

		err = attr_load_runs(attr, ni, run, &end);
		if (err)
			goto out;

		evcn1 = evcn + 1;
		attr->nres.svcn = cpu_to_le64(next_svcn);
		err = mi_pack_runs(mi, attr, run, evcn1 - next_svcn);
		if (err)
			goto out;

		le->vcn = cpu_to_le64(next_svcn);
		ni->attr_list.dirty = true;
		mi->dirty = true;

		next_svcn = le64_to_cpu(attr->nres.evcn) + 1;
	}
ins_ext:
	if (evcn1 > next_svcn) {
		err = ni_insert_nonresident(ni, ATTR_DATA, NULL, 0, run,
					    next_svcn, evcn1 - next_svcn,
					    attr_b->flags, &attr, &mi);
		if (err)
			goto out;
	}
ok:
	run_truncate_around(run, vcn);
out:
	up_write(&ni->file.run_lock);
	ni_unlock(ni);

	return err;
}

int attr_data_read_resident(struct ntfs_inode *ni, struct page *page)
{
	u64 vbo;
	struct ATTRIB *attr;
	u32 data_size;

	attr = ni_find_attr(ni, NULL, NULL, ATTR_DATA, NULL, 0, NULL, NULL);
	if (!attr)
		return -EINVAL;

	if (attr->non_res)
		return E_NTFS_NONRESIDENT;

	vbo = page->index << PAGE_SHIFT;
	data_size = le32_to_cpu(attr->res.data_size);
	if (vbo < data_size) {
		const char *data = resident_data(attr);
		char *kaddr = kmap_atomic(page);
		u32 use = data_size - vbo;

		if (use > PAGE_SIZE)
			use = PAGE_SIZE;

		memcpy(kaddr, data + vbo, use);
		memset(kaddr + use, 0, PAGE_SIZE - use);
		kunmap_atomic(kaddr);
		flush_dcache_page(page);
		SetPageUptodate(page);
	} else if (!PageUptodate(page)) {
		zero_user_segment(page, 0, PAGE_SIZE);
		SetPageUptodate(page);
	}

	return 0;
}

int attr_data_write_resident(struct ntfs_inode *ni, struct page *page)
{
	u64 vbo;
	struct mft_inode *mi;
	struct ATTRIB *attr;
	u32 data_size;

	attr = ni_find_attr(ni, NULL, NULL, ATTR_DATA, NULL, 0, NULL, &mi);
	if (!attr)
		return -EINVAL;

	if (attr->non_res) {
		/* Return special error code to check this case. */
		return E_NTFS_NONRESIDENT;
	}

	vbo = page->index << PAGE_SHIFT;
	data_size = le32_to_cpu(attr->res.data_size);
	if (vbo < data_size) {
		char *data = resident_data(attr);
		char *kaddr = kmap_atomic(page);
		u32 use = data_size - vbo;

		if (use > PAGE_SIZE)
			use = PAGE_SIZE;
		memcpy(data + vbo, kaddr, use);
		kunmap_atomic(kaddr);
		mi->dirty = true;
	}
	ni->i_valid = data_size;

	return 0;
}

/*
 * attr_load_runs_vcn - Load runs with VCN.
 */
int attr_load_runs_vcn(struct ntfs_inode *ni, enum ATTR_TYPE type,
		       const __le16 *name, u8 name_len, struct runs_tree *run,
		       CLST vcn)
{
	struct ATTRIB *attr;
	int err;
	CLST svcn, evcn;
	u16 ro;

	attr = ni_find_attr(ni, NULL, NULL, type, name, name_len, &vcn, NULL);
	if (!attr)
		return -ENOENT;

	svcn = le64_to_cpu(attr->nres.svcn);
	evcn = le64_to_cpu(attr->nres.evcn);

	if (evcn < vcn || vcn < svcn)
		return -EINVAL;

	ro = le16_to_cpu(attr->nres.run_off);
	err = run_unpack_ex(run, ni->mi.sbi, ni->mi.rno, svcn, evcn, svcn,
			    Add2Ptr(attr, ro), le32_to_cpu(attr->size) - ro);
	if (err < 0)
		return err;
	return 0;
}

/*
 * attr_wof_load_runs_range - Load runs for given range [from to).
 */
int attr_load_runs_range(struct ntfs_inode *ni, enum ATTR_TYPE type,
			 const __le16 *name, u8 name_len, struct runs_tree *run,
			 u64 from, u64 to)
{
	struct ntfs_sb_info *sbi = ni->mi.sbi;
	u8 cluster_bits = sbi->cluster_bits;
	CLST vcn = from >> cluster_bits;
	CLST vcn_last = (to - 1) >> cluster_bits;
	CLST lcn, clen;
	int err;

	for (vcn = from >> cluster_bits; vcn <= vcn_last; vcn += clen) {
		if (!run_lookup_entry(run, vcn, &lcn, &clen, NULL)) {
			err = attr_load_runs_vcn(ni, type, name, name_len, run,
						 vcn);
			if (err)
				return err;
			clen = 0; /* Next run_lookup_entry(vcn) must be success. */
		}
	}

	return 0;
}

#ifdef CONFIG_NTFS3_LZX_XPRESS
/*
 * attr_wof_frame_info
 *
 * Read header of Xpress/LZX file to get info about frame.
 */
int attr_wof_frame_info(struct ntfs_inode *ni, struct ATTRIB *attr,
			struct runs_tree *run, u64 frame, u64 frames,
			u8 frame_bits, u32 *ondisk_size, u64 *vbo_data)
{
	struct ntfs_sb_info *sbi = ni->mi.sbi;
	u64 vbo[2], off[2], wof_size;
	u32 voff;
	u8 bytes_per_off;
	char *addr;
	struct page *page;
	int i, err;
	__le32 *off32;
	__le64 *off64;

	if (ni->vfs_inode.i_size < 0x100000000ull) {
		/* File starts with array of 32 bit offsets. */
		bytes_per_off = sizeof(__le32);
		vbo[1] = frame << 2;
		*vbo_data = frames << 2;
	} else {
		/* File starts with array of 64 bit offsets. */
		bytes_per_off = sizeof(__le64);
		vbo[1] = frame << 3;
		*vbo_data = frames << 3;
	}

	/*
	 * Read 4/8 bytes at [vbo - 4(8)] == offset where compressed frame starts.
	 * Read 4/8 bytes at [vbo] == offset where compressed frame ends.
	 */
	if (!attr->non_res) {
		if (vbo[1] + bytes_per_off > le32_to_cpu(attr->res.data_size)) {
			ntfs_inode_err(&ni->vfs_inode, "is corrupted");
			return -EINVAL;
		}
		addr = resident_data(attr);

		if (bytes_per_off == sizeof(__le32)) {
			off32 = Add2Ptr(addr, vbo[1]);
			off[0] = vbo[1] ? le32_to_cpu(off32[-1]) : 0;
			off[1] = le32_to_cpu(off32[0]);
		} else {
			off64 = Add2Ptr(addr, vbo[1]);
			off[0] = vbo[1] ? le64_to_cpu(off64[-1]) : 0;
			off[1] = le64_to_cpu(off64[0]);
		}

		*vbo_data += off[0];
		*ondisk_size = off[1] - off[0];
		return 0;
	}

	wof_size = le64_to_cpu(attr->nres.data_size);
	down_write(&ni->file.run_lock);
	page = ni->file.offs_page;
	if (!page) {
		page = alloc_page(GFP_KERNEL);
		if (!page) {
			err = -ENOMEM;
			goto out;
		}
		page->index = -1;
		ni->file.offs_page = page;
	}
	lock_page(page);
	addr = page_address(page);

	if (vbo[1]) {
		voff = vbo[1] & (PAGE_SIZE - 1);
		vbo[0] = vbo[1] - bytes_per_off;
		i = 0;
	} else {
		voff = 0;
		vbo[0] = 0;
		off[0] = 0;
		i = 1;
	}

	do {
		pgoff_t index = vbo[i] >> PAGE_SHIFT;

		if (index != page->index) {
			u64 from = vbo[i] & ~(u64)(PAGE_SIZE - 1);
			u64 to = min(from + PAGE_SIZE, wof_size);

			err = attr_load_runs_range(ni, ATTR_DATA, WOF_NAME,
						   ARRAY_SIZE(WOF_NAME), run,
						   from, to);
			if (err)
				goto out1;

			err = ntfs_bio_pages(sbi, run, &page, 1, from,
					     to - from, REQ_OP_READ);
			if (err) {
				page->index = -1;
				goto out1;
			}
			page->index = index;
		}

		if (i) {
			if (bytes_per_off == sizeof(__le32)) {
				off32 = Add2Ptr(addr, voff);
				off[1] = le32_to_cpu(*off32);
			} else {
				off64 = Add2Ptr(addr, voff);
				off[1] = le64_to_cpu(*off64);
			}
		} else if (!voff) {
			if (bytes_per_off == sizeof(__le32)) {
				off32 = Add2Ptr(addr, PAGE_SIZE - sizeof(u32));
				off[0] = le32_to_cpu(*off32);
			} else {
				off64 = Add2Ptr(addr, PAGE_SIZE - sizeof(u64));
				off[0] = le64_to_cpu(*off64);
			}
		} else {
			/* Two values in one page. */
			if (bytes_per_off == sizeof(__le32)) {
				off32 = Add2Ptr(addr, voff);
				off[0] = le32_to_cpu(off32[-1]);
				off[1] = le32_to_cpu(off32[0]);
			} else {
				off64 = Add2Ptr(addr, voff);
				off[0] = le64_to_cpu(off64[-1]);
				off[1] = le64_to_cpu(off64[0]);
			}
			break;
		}
	} while (++i < 2);

	*vbo_data += off[0];
	*ondisk_size = off[1] - off[0];

out1:
	unlock_page(page);
out:
	up_write(&ni->file.run_lock);
	return err;
}
#endif

/*
 * attr_is_frame_compressed - Used to detect compressed frame.
 */
int attr_is_frame_compressed(struct ntfs_inode *ni, struct ATTRIB *attr,
			     CLST frame, CLST *clst_data)
{
	int err;
	u32 clst_frame;
	CLST clen, lcn, vcn, alen, slen, vcn_next;
	size_t idx;
	struct runs_tree *run;

	*clst_data = 0;

	if (!is_attr_compressed(attr))
		return 0;

	if (!attr->non_res)
		return 0;

	clst_frame = 1u << attr->nres.c_unit;
	vcn = frame * clst_frame;
	run = &ni->file.run;

	if (!run_lookup_entry(run, vcn, &lcn, &clen, &idx)) {
		err = attr_load_runs_vcn(ni, attr->type, attr_name(attr),
					 attr->name_len, run, vcn);
		if (err)
			return err;

		if (!run_lookup_entry(run, vcn, &lcn, &clen, &idx))
			return -EINVAL;
	}

	if (lcn == SPARSE_LCN) {
		/* Sparsed frame. */
		return 0;
	}

	if (clen >= clst_frame) {
		/*
		 * The frame is not compressed 'cause
		 * it does not contain any sparse clusters.
		 */
		*clst_data = clst_frame;
		return 0;
	}

	alen = bytes_to_cluster(ni->mi.sbi, le64_to_cpu(attr->nres.alloc_size));
	slen = 0;
	*clst_data = clen;

	/*
	 * The frame is compressed if *clst_data + slen >= clst_frame.
	 * Check next fragments.
	 */
	while ((vcn += clen) < alen) {
		vcn_next = vcn;

		if (!run_get_entry(run, ++idx, &vcn, &lcn, &clen) ||
		    vcn_next != vcn) {
			err = attr_load_runs_vcn(ni, attr->type,
						 attr_name(attr),
						 attr->name_len, run, vcn_next);
			if (err)
				return err;
			vcn = vcn_next;

			if (!run_lookup_entry(run, vcn, &lcn, &clen, &idx))
				return -EINVAL;
		}

		if (lcn == SPARSE_LCN) {
			slen += clen;
		} else {
			if (slen) {
				/*
				 * Data_clusters + sparse_clusters =
				 * not enough for frame.
				 */
				return -EINVAL;
			}
			*clst_data += clen;
		}

		if (*clst_data + slen >= clst_frame) {
			if (!slen) {
				/*
				 * There is no sparsed clusters in this frame
				 * so it is not compressed.
				 */
				*clst_data = clst_frame;
			} else {
				/* Frame is compressed. */
			}
			break;
		}
	}

	return 0;
}

/*
 * attr_allocate_frame - Allocate/free clusters for @frame.
 *
 * Assumed: down_write(&ni->file.run_lock);
 */
int attr_allocate_frame(struct ntfs_inode *ni, CLST frame, size_t compr_size,
			u64 new_valid)
{
	int err = 0;
	struct runs_tree *run = &ni->file.run;
	struct ntfs_sb_info *sbi = ni->mi.sbi;
	struct ATTRIB *attr = NULL, *attr_b;
	struct ATTR_LIST_ENTRY *le, *le_b;
	struct mft_inode *mi, *mi_b;
	CLST svcn, evcn1, next_svcn, lcn, len;
	CLST vcn, end, clst_data;
	u64 total_size, valid_size, data_size;

	le_b = NULL;
	attr_b = ni_find_attr(ni, NULL, &le_b, ATTR_DATA, NULL, 0, NULL, &mi_b);
	if (!attr_b)
		return -ENOENT;

	if (!is_attr_ext(attr_b))
		return -EINVAL;

	vcn = frame << NTFS_LZNT_CUNIT;
	total_size = le64_to_cpu(attr_b->nres.total_size);

	svcn = le64_to_cpu(attr_b->nres.svcn);
	evcn1 = le64_to_cpu(attr_b->nres.evcn) + 1;
	data_size = le64_to_cpu(attr_b->nres.data_size);

	if (svcn <= vcn && vcn < evcn1) {
		attr = attr_b;
		le = le_b;
		mi = mi_b;
	} else if (!le_b) {
		err = -EINVAL;
		goto out;
	} else {
		le = le_b;
		attr = ni_find_attr(ni, attr_b, &le, ATTR_DATA, NULL, 0, &vcn,
				    &mi);
		if (!attr) {
			err = -EINVAL;
			goto out;
		}
		svcn = le64_to_cpu(attr->nres.svcn);
		evcn1 = le64_to_cpu(attr->nres.evcn) + 1;
	}

	err = attr_load_runs(attr, ni, run, NULL);
	if (err)
		goto out;

	err = attr_is_frame_compressed(ni, attr_b, frame, &clst_data);
	if (err)
		goto out;

	total_size -= (u64)clst_data << sbi->cluster_bits;

	len = bytes_to_cluster(sbi, compr_size);

	if (len == clst_data)
		goto out;

	if (len < clst_data) {
		err = run_deallocate_ex(sbi, run, vcn + len, clst_data - len,
					NULL, true);
		if (err)
			goto out;

		if (!run_add_entry(run, vcn + len, SPARSE_LCN, clst_data - len,
				   false)) {
			err = -ENOMEM;
			goto out;
		}
		end = vcn + clst_data;
		/* Run contains updated range [vcn + len : end). */
	} else {
		CLST alen, hint = 0;
		/* Get the last LCN to allocate from. */
		if (vcn + clst_data &&
		    !run_lookup_entry(run, vcn + clst_data - 1, &hint, NULL,
				      NULL)) {
			hint = -1;
		}

		err = attr_allocate_clusters(sbi, run, vcn + clst_data,
					     hint + 1, len - clst_data, NULL, 0,
					     &alen, 0, &lcn);
		if (err)
			goto out;

		end = vcn + len;
		/* Run contains updated range [vcn + clst_data : end). */
	}

	total_size += (u64)len << sbi->cluster_bits;

repack:
	err = mi_pack_runs(mi, attr, run, max(end, evcn1) - svcn);
	if (err)
		goto out;

	attr_b->nres.total_size = cpu_to_le64(total_size);
	inode_set_bytes(&ni->vfs_inode, total_size);

	mi_b->dirty = true;
	mark_inode_dirty(&ni->vfs_inode);

	/* Stored [vcn : next_svcn) from [vcn : end). */
	next_svcn = le64_to_cpu(attr->nres.evcn) + 1;

	if (end <= evcn1) {
		if (next_svcn == evcn1) {
			/* Normal way. Update attribute and exit. */
			goto ok;
		}
		/* Add new segment [next_svcn : evcn1 - next_svcn). */
		if (!ni->attr_list.size) {
			err = ni_create_attr_list(ni);
			if (err)
				goto out;
			/* Layout of records is changed. */
			le_b = NULL;
			attr_b = ni_find_attr(ni, NULL, &le_b, ATTR_DATA, NULL,
					      0, NULL, &mi_b);
			if (!attr_b) {
				err = -ENOENT;
				goto out;
			}

			attr = attr_b;
			le = le_b;
			mi = mi_b;
			goto repack;
		}
	}

	svcn = evcn1;

	/* Estimate next attribute. */
	attr = ni_find_attr(ni, attr, &le, ATTR_DATA, NULL, 0, &svcn, &mi);

	if (attr) {
		CLST alloc = bytes_to_cluster(
			sbi, le64_to_cpu(attr_b->nres.alloc_size));
		CLST evcn = le64_to_cpu(attr->nres.evcn);

		if (end < next_svcn)
			end = next_svcn;
		while (end > evcn) {
			/* Remove segment [svcn : evcn). */
			mi_remove_attr(NULL, mi, attr);

			if (!al_remove_le(ni, le)) {
				err = -EINVAL;
				goto out;
			}

			if (evcn + 1 >= alloc) {
				/* Last attribute segment. */
				evcn1 = evcn + 1;
				goto ins_ext;
			}

			if (ni_load_mi(ni, le, &mi)) {
				attr = NULL;
				goto out;
			}

			attr = mi_find_attr(mi, NULL, ATTR_DATA, NULL, 0,
					    &le->id);
			if (!attr) {
				err = -EINVAL;
				goto out;
			}
			svcn = le64_to_cpu(attr->nres.svcn);
			evcn = le64_to_cpu(attr->nres.evcn);
		}

		if (end < svcn)
			end = svcn;

		err = attr_load_runs(attr, ni, run, &end);
		if (err)
			goto out;

		evcn1 = evcn + 1;
		attr->nres.svcn = cpu_to_le64(next_svcn);
		err = mi_pack_runs(mi, attr, run, evcn1 - next_svcn);
		if (err)
			goto out;

		le->vcn = cpu_to_le64(next_svcn);
		ni->attr_list.dirty = true;
		mi->dirty = true;

		next_svcn = le64_to_cpu(attr->nres.evcn) + 1;
	}
ins_ext:
	if (evcn1 > next_svcn) {
		err = ni_insert_nonresident(ni, ATTR_DATA, NULL, 0, run,
					    next_svcn, evcn1 - next_svcn,
					    attr_b->flags, &attr, &mi);
		if (err)
			goto out;
	}
ok:
	run_truncate_around(run, vcn);
out:
	if (new_valid > data_size)
		new_valid = data_size;

	valid_size = le64_to_cpu(attr_b->nres.valid_size);
	if (new_valid != valid_size) {
		attr_b->nres.valid_size = cpu_to_le64(valid_size);
		mi_b->dirty = true;
	}

	return err;
}

/*
 * attr_collapse_range - Collapse range in file.
 */
int attr_collapse_range(struct ntfs_inode *ni, u64 vbo, u64 bytes)
{
	int err = 0;
	struct runs_tree *run = &ni->file.run;
	struct ntfs_sb_info *sbi = ni->mi.sbi;
	struct ATTRIB *attr = NULL, *attr_b;
	struct ATTR_LIST_ENTRY *le, *le_b;
	struct mft_inode *mi, *mi_b;
	CLST svcn, evcn1, len, dealloc, alen;
	CLST vcn, end;
	u64 valid_size, data_size, alloc_size, total_size;
	u32 mask;
	__le16 a_flags;

	if (!bytes)
		return 0;

	le_b = NULL;
	attr_b = ni_find_attr(ni, NULL, &le_b, ATTR_DATA, NULL, 0, NULL, &mi_b);
	if (!attr_b)
		return -ENOENT;

	if (!attr_b->non_res) {
		/* Attribute is resident. Nothing to do? */
		return 0;
	}

	data_size = le64_to_cpu(attr_b->nres.data_size);
	alloc_size = le64_to_cpu(attr_b->nres.alloc_size);
	a_flags = attr_b->flags;

	if (is_attr_ext(attr_b)) {
		total_size = le64_to_cpu(attr_b->nres.total_size);
		mask = (sbi->cluster_size << attr_b->nres.c_unit) - 1;
	} else {
		total_size = alloc_size;
		mask = sbi->cluster_mask;
	}

	if ((vbo & mask) || (bytes & mask)) {
		/* Allow to collapse only cluster aligned ranges. */
		return -EINVAL;
	}

	if (vbo > data_size)
		return -EINVAL;

	down_write(&ni->file.run_lock);

	if (vbo + bytes >= data_size) {
		u64 new_valid = min(ni->i_valid, vbo);

		/* Simple truncate file at 'vbo'. */
		truncate_setsize(&ni->vfs_inode, vbo);
		err = attr_set_size(ni, ATTR_DATA, NULL, 0, &ni->file.run, vbo,
				    &new_valid, true, NULL);

		if (!err && new_valid < ni->i_valid)
			ni->i_valid = new_valid;

		goto out;
	}

	/*
	 * Enumerate all attribute segments and collapse.
	 */
	alen = alloc_size >> sbi->cluster_bits;
	vcn = vbo >> sbi->cluster_bits;
	len = bytes >> sbi->cluster_bits;
	end = vcn + len;
	dealloc = 0;

	svcn = le64_to_cpu(attr_b->nres.svcn);
	evcn1 = le64_to_cpu(attr_b->nres.evcn) + 1;

	if (svcn <= vcn && vcn < evcn1) {
		attr = attr_b;
		le = le_b;
		mi = mi_b;
	} else if (!le_b) {
		err = -EINVAL;
		goto out;
	} else {
		le = le_b;
		attr = ni_find_attr(ni, attr_b, &le, ATTR_DATA, NULL, 0, &vcn,
				    &mi);
		if (!attr) {
			err = -EINVAL;
			goto out;
		}

		svcn = le64_to_cpu(attr->nres.svcn);
		evcn1 = le64_to_cpu(attr->nres.evcn) + 1;
	}

	for (;;) {
		if (svcn >= end) {
			/* Shift VCN- */
			attr->nres.svcn = cpu_to_le64(svcn - len);
			attr->nres.evcn = cpu_to_le64(evcn1 - 1 - len);
			if (le) {
				le->vcn = attr->nres.svcn;
				ni->attr_list.dirty = true;
			}
			mi->dirty = true;
		} else if (svcn < vcn || end < evcn1) {
			CLST vcn1, eat, next_svcn;

			/* Collapse a part of this attribute segment. */
			err = attr_load_runs(attr, ni, run, &svcn);
			if (err)
				goto out;
			vcn1 = max(vcn, svcn);
			eat = min(end, evcn1) - vcn1;

			err = run_deallocate_ex(sbi, run, vcn1, eat, &dealloc,
						true);
			if (err)
				goto out;

			if (!run_collapse_range(run, vcn1, eat)) {
				err = -ENOMEM;
				goto out;
			}

			if (svcn >= vcn) {
				/* Shift VCN */
				attr->nres.svcn = cpu_to_le64(vcn);
				if (le) {
					le->vcn = attr->nres.svcn;
					ni->attr_list.dirty = true;
				}
			}

			err = mi_pack_runs(mi, attr, run, evcn1 - svcn - eat);
			if (err)
				goto out;

			next_svcn = le64_to_cpu(attr->nres.evcn) + 1;
			if (next_svcn + eat < evcn1) {
				err = ni_insert_nonresident(
					ni, ATTR_DATA, NULL, 0, run, next_svcn,
					evcn1 - eat - next_svcn, a_flags, &attr,
					&mi);
				if (err)
					goto out;

				/* Layout of records maybe changed. */
				attr_b = NULL;
				le = al_find_ex(ni, NULL, ATTR_DATA, NULL, 0,
						&next_svcn);
				if (!le) {
					err = -EINVAL;
					goto out;
				}
			}

			/* Free all allocated memory. */
			run_truncate(run, 0);
		} else {
			u16 le_sz;
			u16 roff = le16_to_cpu(attr->nres.run_off);

			run_unpack_ex(RUN_DEALLOCATE, sbi, ni->mi.rno, svcn,
				      evcn1 - 1, svcn, Add2Ptr(attr, roff),
				      le32_to_cpu(attr->size) - roff);

			/* Delete this attribute segment. */
			mi_remove_attr(NULL, mi, attr);
			if (!le)
				break;

			le_sz = le16_to_cpu(le->size);
			if (!al_remove_le(ni, le)) {
				err = -EINVAL;
				goto out;
			}

			if (evcn1 >= alen)
				break;

			if (!svcn) {
				/* Load next record that contains this attribute. */
				if (ni_load_mi(ni, le, &mi)) {
					err = -EINVAL;
					goto out;
				}

				/* Look for required attribute. */
				attr = mi_find_attr(mi, NULL, ATTR_DATA, NULL,
						    0, &le->id);
				if (!attr) {
					err = -EINVAL;
					goto out;
				}
				goto next_attr;
			}
			le = (struct ATTR_LIST_ENTRY *)((u8 *)le - le_sz);
		}

		if (evcn1 >= alen)
			break;

		attr = ni_enum_attr_ex(ni, attr, &le, &mi);
		if (!attr) {
			err = -EINVAL;
			goto out;
		}

next_attr:
		svcn = le64_to_cpu(attr->nres.svcn);
		evcn1 = le64_to_cpu(attr->nres.evcn) + 1;
	}

	if (!attr_b) {
		le_b = NULL;
		attr_b = ni_find_attr(ni, NULL, &le_b, ATTR_DATA, NULL, 0, NULL,
				      &mi_b);
		if (!attr_b) {
			err = -ENOENT;
			goto out;
		}
	}

	data_size -= bytes;
	valid_size = ni->i_valid;
	if (vbo + bytes <= valid_size)
		valid_size -= bytes;
	else if (vbo < valid_size)
		valid_size = vbo;

	attr_b->nres.alloc_size = cpu_to_le64(alloc_size - bytes);
	attr_b->nres.data_size = cpu_to_le64(data_size);
	attr_b->nres.valid_size = cpu_to_le64(min(valid_size, data_size));
	total_size -= (u64)dealloc << sbi->cluster_bits;
	if (is_attr_ext(attr_b))
		attr_b->nres.total_size = cpu_to_le64(total_size);
	mi_b->dirty = true;

	/* Update inode size. */
	ni->i_valid = valid_size;
	ni->vfs_inode.i_size = data_size;
	inode_set_bytes(&ni->vfs_inode, total_size);
	ni->ni_flags |= NI_FLAG_UPDATE_PARENT;
	mark_inode_dirty(&ni->vfs_inode);

out:
	up_write(&ni->file.run_lock);
	if (err)
		make_bad_inode(&ni->vfs_inode);

	return err;
}

/*
 * attr_punch_hole
 *
 * Not for normal files.
 */
int attr_punch_hole(struct ntfs_inode *ni, u64 vbo, u64 bytes, u32 *frame_size)
{
	int err = 0;
	struct runs_tree *run = &ni->file.run;
	struct ntfs_sb_info *sbi = ni->mi.sbi;
	struct ATTRIB *attr = NULL, *attr_b;
	struct ATTR_LIST_ENTRY *le, *le_b;
	struct mft_inode *mi, *mi_b;
	CLST svcn, evcn1, vcn, len, end, alen, dealloc;
	u64 total_size, alloc_size;
	u32 mask;

	if (!bytes)
		return 0;

	le_b = NULL;
	attr_b = ni_find_attr(ni, NULL, &le_b, ATTR_DATA, NULL, 0, NULL, &mi_b);
	if (!attr_b)
		return -ENOENT;

	if (!attr_b->non_res) {
		u32 data_size = le32_to_cpu(attr->res.data_size);
		u32 from, to;

		if (vbo > data_size)
			return 0;

		from = vbo;
		to = (vbo + bytes) < data_size ? (vbo + bytes) : data_size;
		memset(Add2Ptr(resident_data(attr_b), from), 0, to - from);
		return 0;
	}

	if (!is_attr_ext(attr_b))
		return -EOPNOTSUPP;

	alloc_size = le64_to_cpu(attr_b->nres.alloc_size);
	total_size = le64_to_cpu(attr_b->nres.total_size);

	if (vbo >= alloc_size) {
		// NOTE: It is allowed.
		return 0;
	}

	mask = (sbi->cluster_size << attr_b->nres.c_unit) - 1;

	bytes += vbo;
	if (bytes > alloc_size)
		bytes = alloc_size;
	bytes -= vbo;

	if ((vbo & mask) || (bytes & mask)) {
		/* We have to zero a range(s)*/
		if (frame_size == NULL) {
			/* Caller insists range is aligned */
			return -EINVAL;
		}
		*frame_size = mask + 1;
		return E_NTFS_NOTALIGNED;
	}

	down_write(&ni->file.run_lock);
	/*
	 * Enumerate all attribute segments and punch hole where necessary.
	 */
	alen = alloc_size >> sbi->cluster_bits;
	vcn = vbo >> sbi->cluster_bits;
	len = bytes >> sbi->cluster_bits;
	end = vcn + len;
	dealloc = 0;

	svcn = le64_to_cpu(attr_b->nres.svcn);
	evcn1 = le64_to_cpu(attr_b->nres.evcn) + 1;

	if (svcn <= vcn && vcn < evcn1) {
		attr = attr_b;
		le = le_b;
		mi = mi_b;
	} else if (!le_b) {
		err = -EINVAL;
		goto out;
	} else {
		le = le_b;
		attr = ni_find_attr(ni, attr_b, &le, ATTR_DATA, NULL, 0, &vcn,
				    &mi);
		if (!attr) {
			err = -EINVAL;
			goto out;
		}

		svcn = le64_to_cpu(attr->nres.svcn);
		evcn1 = le64_to_cpu(attr->nres.evcn) + 1;
	}

	while (svcn < end) {
		CLST vcn1, zero, dealloc2;

		err = attr_load_runs(attr, ni, run, &svcn);
		if (err)
			goto out;
		vcn1 = max(vcn, svcn);
		zero = min(end, evcn1) - vcn1;

		dealloc2 = dealloc;
		err = run_deallocate_ex(sbi, run, vcn1, zero, &dealloc, true);
		if (err)
			goto out;

		if (dealloc2 == dealloc) {
			/* Looks like the required range is already sparsed. */
		} else {
			if (!run_add_entry(run, vcn1, SPARSE_LCN, zero,
					   false)) {
				err = -ENOMEM;
				goto out;
			}

			err = mi_pack_runs(mi, attr, run, evcn1 - svcn);
			if (err)
				goto out;
		}
		/* Free all allocated memory. */
		run_truncate(run, 0);

		if (evcn1 >= alen)
			break;

		attr = ni_enum_attr_ex(ni, attr, &le, &mi);
		if (!attr) {
			err = -EINVAL;
			goto out;
		}

		svcn = le64_to_cpu(attr->nres.svcn);
		evcn1 = le64_to_cpu(attr->nres.evcn) + 1;
	}

	total_size -= (u64)dealloc << sbi->cluster_bits;
	attr_b->nres.total_size = cpu_to_le64(total_size);
	mi_b->dirty = true;

	/* Update inode size. */
	inode_set_bytes(&ni->vfs_inode, total_size);
	ni->ni_flags |= NI_FLAG_UPDATE_PARENT;
	mark_inode_dirty(&ni->vfs_inode);

out:
	up_write(&ni->file.run_lock);
	if (err)
		make_bad_inode(&ni->vfs_inode);

	return err;
}
