/**
 * attrib.c - NTFS attribute operations.  Part of the Linux-NTFS project.
 *
 * Copyright (c) 2001-2004 Anton Altaparmakov
 * Copyright (c) 2002 Richard Russon
 *
 * This program/include file is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program/include file is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program (in the main directory of the Linux-NTFS
 * distribution in the file COPYING); if not, write to the Free Software
 * Foundation,Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/buffer_head.h>

#include "attrib.h"
#include "debug.h"
#include "layout.h"
#include "mft.h"
#include "ntfs.h"
#include "types.h"

/**
 * ntfs_map_runlist - map (a part of) a runlist of an ntfs inode
 * @ni:		ntfs inode for which to map (part of) a runlist
 * @vcn:	map runlist part containing this vcn
 *
 * Map the part of a runlist containing the @vcn of the ntfs inode @ni.
 *
 * Return 0 on success and -errno on error.
 *
 * Locking: - The runlist must be unlocked on entry and is unlocked on return.
 *	    - This function takes the lock for writing and modifies the runlist.
 */
int ntfs_map_runlist(ntfs_inode *ni, VCN vcn)
{
	ntfs_inode *base_ni;
	ntfs_attr_search_ctx *ctx;
	MFT_RECORD *mrec;
	int err = 0;

	ntfs_debug("Mapping runlist part containing vcn 0x%llx.",
			(unsigned long long)vcn);

	if (!NInoAttr(ni))
		base_ni = ni;
	else
		base_ni = ni->ext.base_ntfs_ino;

	mrec = map_mft_record(base_ni);
	if (IS_ERR(mrec))
		return PTR_ERR(mrec);
	ctx = ntfs_attr_get_search_ctx(base_ni, mrec);
	if (unlikely(!ctx)) {
		err = -ENOMEM;
		goto err_out;
	}
	err = ntfs_attr_lookup(ni->type, ni->name, ni->name_len,
			CASE_SENSITIVE, vcn, NULL, 0, ctx);
	if (unlikely(err))
		goto put_err_out;

	down_write(&ni->runlist.lock);
	/* Make sure someone else didn't do the work while we were sleeping. */
	if (likely(ntfs_rl_vcn_to_lcn(ni->runlist.rl, vcn) <=
			LCN_RL_NOT_MAPPED)) {
		runlist_element *rl;

		rl = ntfs_mapping_pairs_decompress(ni->vol, ctx->attr,
				ni->runlist.rl);
		if (IS_ERR(rl))
			err = PTR_ERR(rl);
		else
			ni->runlist.rl = rl;
	}
	up_write(&ni->runlist.lock);

put_err_out:
	ntfs_attr_put_search_ctx(ctx);
err_out:
	unmap_mft_record(base_ni);
	return err;
}

/**
 * ntfs_find_vcn - find a vcn in the runlist described by an ntfs inode
 * @ni:		ntfs inode describing the runlist to search
 * @vcn:	vcn to find
 * @need_write:	if false, lock for reading and if true, lock for writing
 *
 * Find the virtual cluster number @vcn in the runlist described by the ntfs
 * inode @ni and return the address of the runlist element containing the @vcn.
 * The runlist is left locked and the caller has to unlock it.  If @need_write
 * is true, the runlist is locked for writing and if @need_write is false, the
 * runlist is locked for reading.  In the error case, the runlist is not left
 * locked.
 *
 * Note you need to distinguish between the lcn of the returned runlist element
 * being >= 0 and LCN_HOLE.  In the later case you have to return zeroes on
 * read and allocate clusters on write.
 *
 * Return the runlist element containing the @vcn on success and
 * ERR_PTR(-errno) on error.  You need to test the return value with IS_ERR()
 * to decide if the return is success or failure and PTR_ERR() to get to the
 * error code if IS_ERR() is true.
 *
 * The possible error return codes are:
 *	-ENOENT - No such vcn in the runlist, i.e. @vcn is out of bounds.
 *	-ENOMEM - Not enough memory to map runlist.
 *	-EIO	- Critical error (runlist/file is corrupt, i/o error, etc).
 *
 * Locking: - The runlist must be unlocked on entry.
 *	    - On failing return, the runlist is unlocked.
 *	    - On successful return, the runlist is locked.  If @need_write us
 *	      true, it is locked for writing.  Otherwise is is locked for
 *	      reading.
 */
runlist_element *ntfs_find_vcn(ntfs_inode *ni, const VCN vcn,
		const BOOL need_write)
{
	runlist_element *rl;
	int err = 0;
	BOOL is_retry = FALSE;

	ntfs_debug("Entering for i_ino 0x%lx, vcn 0x%llx, lock for %sing.",
			ni->mft_no, (unsigned long long)vcn,
			!need_write ? "read" : "writ");
	BUG_ON(!ni);
	BUG_ON(!NInoNonResident(ni));
	BUG_ON(vcn < 0);
lock_retry_remap:
	if (!need_write)
		down_read(&ni->runlist.lock);
	else
		down_write(&ni->runlist.lock);
	rl = ni->runlist.rl;
	if (likely(rl && vcn >= rl[0].vcn)) {
		while (likely(rl->length)) {
			if (likely(vcn < rl[1].vcn)) {
				if (likely(rl->lcn >= LCN_HOLE)) {
					ntfs_debug("Done.");
					return rl;
				}
				break;
			}
			rl++;
		}
		if (likely(rl->lcn != LCN_RL_NOT_MAPPED)) {
			if (likely(rl->lcn == LCN_ENOENT))
				err = -ENOENT;
			else
				err = -EIO;
		}
	}
	if (!need_write)
		up_read(&ni->runlist.lock);
	else
		up_write(&ni->runlist.lock);
	if (!err && !is_retry) {
		/*
		 * The @vcn is in an unmapped region, map the runlist and
		 * retry.
		 */
		err = ntfs_map_runlist(ni, vcn);
		if (likely(!err)) {
			is_retry = TRUE;
			goto lock_retry_remap;
		}
		/*
		 * -EINVAL and -ENOENT coming from a failed mapping attempt are
		 * equivalent to i/o errors for us as they should not happen in
		 * our code paths.
		 */
		if (err == -EINVAL || err == -ENOENT)
			err = -EIO;
	} else if (!err)
		err = -EIO;
	ntfs_error(ni->vol->sb, "Failed with error code %i.", err);
	return ERR_PTR(err);
}

/**
 * ntfs_attr_find - find (next) attribute in mft record
 * @type:	attribute type to find
 * @name:	attribute name to find (optional, i.e. NULL means don't care)
 * @name_len:	attribute name length (only needed if @name present)
 * @ic:		IGNORE_CASE or CASE_SENSITIVE (ignored if @name not present)
 * @val:	attribute value to find (optional, resident attributes only)
 * @val_len:	attribute value length
 * @ctx:	search context with mft record and attribute to search from
 *
 * You should not need to call this function directly.  Use ntfs_attr_lookup()
 * instead.
 *
 * ntfs_attr_find() takes a search context @ctx as parameter and searches the
 * mft record specified by @ctx->mrec, beginning at @ctx->attr, for an
 * attribute of @type, optionally @name and @val.
 *
 * If the attribute is found, ntfs_attr_find() returns 0 and @ctx->attr will
 * point to the found attribute.
 *
 * If the attribute is not found, ntfs_attr_find() returns -ENOENT and
 * @ctx->attr will point to the attribute before which the attribute being
 * searched for would need to be inserted if such an action were to be desired.
 *
 * On actual error, ntfs_attr_find() returns -EIO.  In this case @ctx->attr is
 * undefined and in particular do not rely on it not changing.
 *
 * If @ctx->is_first is TRUE, the search begins with @ctx->attr itself.  If it
 * is FALSE, the search begins after @ctx->attr.
 *
 * If @ic is IGNORE_CASE, the @name comparisson is not case sensitive and
 * @ctx->ntfs_ino must be set to the ntfs inode to which the mft record
 * @ctx->mrec belongs.  This is so we can get at the ntfs volume and hence at
 * the upcase table.  If @ic is CASE_SENSITIVE, the comparison is case
 * sensitive.  When @name is present, @name_len is the @name length in Unicode
 * characters.
 *
 * If @name is not present (NULL), we assume that the unnamed attribute is
 * being searched for.
 *
 * Finally, the resident attribute value @val is looked for, if present.  If
 * @val is not present (NULL), @val_len is ignored.
 *
 * ntfs_attr_find() only searches the specified mft record and it ignores the
 * presence of an attribute list attribute (unless it is the one being searched
 * for, obviously).  If you need to take attribute lists into consideration,
 * use ntfs_attr_lookup() instead (see below).  This also means that you cannot
 * use ntfs_attr_find() to search for extent records of non-resident
 * attributes, as extents with lowest_vcn != 0 are usually described by the
 * attribute list attribute only. - Note that it is possible that the first
 * extent is only in the attribute list while the last extent is in the base
 * mft record, so do not rely on being able to find the first extent in the
 * base mft record.
 *
 * Warning: Never use @val when looking for attribute types which can be
 *	    non-resident as this most likely will result in a crash!
 */
static int ntfs_attr_find(const ATTR_TYPE type, const ntfschar *name,
		const u32 name_len, const IGNORE_CASE_BOOL ic,
		const u8 *val, const u32 val_len, ntfs_attr_search_ctx *ctx)
{
	ATTR_RECORD *a;
	ntfs_volume *vol = ctx->ntfs_ino->vol;
	ntfschar *upcase = vol->upcase;
	u32 upcase_len = vol->upcase_len;

	/*
	 * Iterate over attributes in mft record starting at @ctx->attr, or the
	 * attribute following that, if @ctx->is_first is TRUE.
	 */
	if (ctx->is_first) {
		a = ctx->attr;
		ctx->is_first = FALSE;
	} else
		a = (ATTR_RECORD*)((u8*)ctx->attr +
				le32_to_cpu(ctx->attr->length));
	for (;;	a = (ATTR_RECORD*)((u8*)a + le32_to_cpu(a->length))) {
		if ((u8*)a < (u8*)ctx->mrec || (u8*)a > (u8*)ctx->mrec +
				le32_to_cpu(ctx->mrec->bytes_allocated))
			break;
		ctx->attr = a;
		if (unlikely(le32_to_cpu(a->type) > le32_to_cpu(type) ||
				a->type == AT_END))
			return -ENOENT;
		if (unlikely(!a->length))
			break;
		if (a->type != type)
			continue;
		/*
		 * If @name is present, compare the two names.  If @name is
		 * missing, assume we want an unnamed attribute.
		 */
		if (!name) {
			/* The search failed if the found attribute is named. */
			if (a->name_length)
				return -ENOENT;
		} else if (!ntfs_are_names_equal(name, name_len,
			    (ntfschar*)((u8*)a + le16_to_cpu(a->name_offset)),
			    a->name_length, ic, upcase, upcase_len)) {
			register int rc;

			rc = ntfs_collate_names(name, name_len,
					(ntfschar*)((u8*)a +
					le16_to_cpu(a->name_offset)),
					a->name_length, 1, IGNORE_CASE,
					upcase, upcase_len);
			/*
			 * If @name collates before a->name, there is no
			 * matching attribute.
			 */
			if (rc == -1)
				return -ENOENT;
			/* If the strings are not equal, continue search. */
			if (rc)
				continue;
			rc = ntfs_collate_names(name, name_len,
					(ntfschar*)((u8*)a +
					le16_to_cpu(a->name_offset)),
					a->name_length, 1, CASE_SENSITIVE,
					upcase, upcase_len);
			if (rc == -1)
				return -ENOENT;
			if (rc)
				continue;
		}
		/*
		 * The names match or @name not present and attribute is
		 * unnamed.  If no @val specified, we have found the attribute
		 * and are done.
		 */
		if (!val)
			return 0;
		/* @val is present; compare values. */
		else {
			register int rc;

			rc = memcmp(val, (u8*)a + le16_to_cpu(
					a->data.resident.value_offset),
					min_t(u32, val_len, le32_to_cpu(
					a->data.resident.value_length)));
			/*
			 * If @val collates before the current attribute's
			 * value, there is no matching attribute.
			 */
			if (!rc) {
				register u32 avl;

				avl = le32_to_cpu(
						a->data.resident.value_length);
				if (val_len == avl)
					return 0;
				if (val_len < avl)
					return -ENOENT;
			} else if (rc < 0)
				return -ENOENT;
		}
	}
	ntfs_error(vol->sb, "Inode is corrupt.  Run chkdsk.");
	NVolSetErrors(vol);
	return -EIO;
}

/**
 * load_attribute_list - load an attribute list into memory
 * @vol:		ntfs volume from which to read
 * @runlist:		runlist of the attribute list
 * @al_start:		destination buffer
 * @size:		size of the destination buffer in bytes
 * @initialized_size:	initialized size of the attribute list
 *
 * Walk the runlist @runlist and load all clusters from it copying them into
 * the linear buffer @al. The maximum number of bytes copied to @al is @size
 * bytes. Note, @size does not need to be a multiple of the cluster size. If
 * @initialized_size is less than @size, the region in @al between
 * @initialized_size and @size will be zeroed and not read from disk.
 *
 * Return 0 on success or -errno on error.
 */
int load_attribute_list(ntfs_volume *vol, runlist *runlist, u8 *al_start,
		const s64 size, const s64 initialized_size)
{
	LCN lcn;
	u8 *al = al_start;
	u8 *al_end = al + initialized_size;
	runlist_element *rl;
	struct buffer_head *bh;
	struct super_block *sb;
	unsigned long block_size;
	unsigned long block, max_block;
	int err = 0;
	unsigned char block_size_bits;

	ntfs_debug("Entering.");
	if (!vol || !runlist || !al || size <= 0 || initialized_size < 0 ||
			initialized_size > size)
		return -EINVAL;
	if (!initialized_size) {
		memset(al, 0, size);
		return 0;
	}
	sb = vol->sb;
	block_size = sb->s_blocksize;
	block_size_bits = sb->s_blocksize_bits;
	down_read(&runlist->lock);
	rl = runlist->rl;
	/* Read all clusters specified by the runlist one run at a time. */
	while (rl->length) {
		lcn = ntfs_rl_vcn_to_lcn(rl, rl->vcn);
		ntfs_debug("Reading vcn = 0x%llx, lcn = 0x%llx.",
				(unsigned long long)rl->vcn,
				(unsigned long long)lcn);
		/* The attribute list cannot be sparse. */
		if (lcn < 0) {
			ntfs_error(sb, "ntfs_rl_vcn_to_lcn() failed.  Cannot "
					"read attribute list.");
			goto err_out;
		}
		block = lcn << vol->cluster_size_bits >> block_size_bits;
		/* Read the run from device in chunks of block_size bytes. */
		max_block = block + (rl->length << vol->cluster_size_bits >>
				block_size_bits);
		ntfs_debug("max_block = 0x%lx.", max_block);
		do {
			ntfs_debug("Reading block = 0x%lx.", block);
			bh = sb_bread(sb, block);
			if (!bh) {
				ntfs_error(sb, "sb_bread() failed. Cannot "
						"read attribute list.");
				goto err_out;
			}
			if (al + block_size >= al_end)
				goto do_final;
			memcpy(al, bh->b_data, block_size);
			brelse(bh);
			al += block_size;
		} while (++block < max_block);
		rl++;
	}
	if (initialized_size < size) {
initialize:
		memset(al_start + initialized_size, 0, size - initialized_size);
	}
done:
	up_read(&runlist->lock);
	return err;
do_final:
	if (al < al_end) {
		/*
		 * Partial block.
		 *
		 * Note: The attribute list can be smaller than its allocation
		 * by multiple clusters.  This has been encountered by at least
		 * two people running Windows XP, thus we cannot do any
		 * truncation sanity checking here. (AIA)
		 */
		memcpy(al, bh->b_data, al_end - al);
		brelse(bh);
		if (initialized_size < size)
			goto initialize;
		goto done;
	}
	brelse(bh);
	/* Real overflow! */
	ntfs_error(sb, "Attribute list buffer overflow. Read attribute list "
			"is truncated.");
err_out:
	err = -EIO;
	goto done;
}

/**
 * ntfs_external_attr_find - find an attribute in the attribute list of an inode
 * @type:	attribute type to find
 * @name:	attribute name to find (optional, i.e. NULL means don't care)
 * @name_len:	attribute name length (only needed if @name present)
 * @ic:		IGNORE_CASE or CASE_SENSITIVE (ignored if @name not present)
 * @lowest_vcn:	lowest vcn to find (optional, non-resident attributes only)
 * @val:	attribute value to find (optional, resident attributes only)
 * @val_len:	attribute value length
 * @ctx:	search context with mft record and attribute to search from
 *
 * You should not need to call this function directly.  Use ntfs_attr_lookup()
 * instead.
 *
 * Find an attribute by searching the attribute list for the corresponding
 * attribute list entry.  Having found the entry, map the mft record if the
 * attribute is in a different mft record/inode, ntfs_attr_find() the attribute
 * in there and return it.
 *
 * On first search @ctx->ntfs_ino must be the base mft record and @ctx must
 * have been obtained from a call to ntfs_attr_get_search_ctx().  On subsequent
 * calls @ctx->ntfs_ino can be any extent inode, too (@ctx->base_ntfs_ino is
 * then the base inode).
 *
 * After finishing with the attribute/mft record you need to call
 * ntfs_attr_put_search_ctx() to cleanup the search context (unmapping any
 * mapped inodes, etc).
 *
 * If the attribute is found, ntfs_external_attr_find() returns 0 and
 * @ctx->attr will point to the found attribute.  @ctx->mrec will point to the
 * mft record in which @ctx->attr is located and @ctx->al_entry will point to
 * the attribute list entry for the attribute.
 *
 * If the attribute is not found, ntfs_external_attr_find() returns -ENOENT and
 * @ctx->attr will point to the attribute in the base mft record before which
 * the attribute being searched for would need to be inserted if such an action
 * were to be desired.  @ctx->mrec will point to the mft record in which
 * @ctx->attr is located and @ctx->al_entry will point to the attribute list
 * entry of the attribute before which the attribute being searched for would
 * need to be inserted if such an action were to be desired.
 *
 * Thus to insert the not found attribute, one wants to add the attribute to
 * @ctx->mrec (the base mft record) and if there is not enough space, the
 * attribute should be placed in a newly allocated extent mft record.  The
 * attribute list entry for the inserted attribute should be inserted in the
 * attribute list attribute at @ctx->al_entry.
 *
 * On actual error, ntfs_external_attr_find() returns -EIO.  In this case
 * @ctx->attr is undefined and in particular do not rely on it not changing.
 */
static int ntfs_external_attr_find(const ATTR_TYPE type,
		const ntfschar *name, const u32 name_len,
		const IGNORE_CASE_BOOL ic, const VCN lowest_vcn,
		const u8 *val, const u32 val_len, ntfs_attr_search_ctx *ctx)
{
	ntfs_inode *base_ni, *ni;
	ntfs_volume *vol;
	ATTR_LIST_ENTRY *al_entry, *next_al_entry;
	u8 *al_start, *al_end;
	ATTR_RECORD *a;
	ntfschar *al_name;
	u32 al_name_len;
	int err = 0;
	static const char *es = " Unmount and run chkdsk.";

	ni = ctx->ntfs_ino;
	base_ni = ctx->base_ntfs_ino;
	ntfs_debug("Entering for inode 0x%lx, type 0x%x.", ni->mft_no, type);
	if (!base_ni) {
		/* First call happens with the base mft record. */
		base_ni = ctx->base_ntfs_ino = ctx->ntfs_ino;
		ctx->base_mrec = ctx->mrec;
	}
	if (ni == base_ni)
		ctx->base_attr = ctx->attr;
	if (type == AT_END)
		goto not_found;
	vol = base_ni->vol;
	al_start = base_ni->attr_list;
	al_end = al_start + base_ni->attr_list_size;
	if (!ctx->al_entry)
		ctx->al_entry = (ATTR_LIST_ENTRY*)al_start;
	/*
	 * Iterate over entries in attribute list starting at @ctx->al_entry,
	 * or the entry following that, if @ctx->is_first is TRUE.
	 */
	if (ctx->is_first) {
		al_entry = ctx->al_entry;
		ctx->is_first = FALSE;
	} else
		al_entry = (ATTR_LIST_ENTRY*)((u8*)ctx->al_entry +
				le16_to_cpu(ctx->al_entry->length));
	for (;; al_entry = next_al_entry) {
		/* Out of bounds check. */
		if ((u8*)al_entry < base_ni->attr_list ||
				(u8*)al_entry > al_end)
			break;	/* Inode is corrupt. */
		ctx->al_entry = al_entry;
		/* Catch the end of the attribute list. */
		if ((u8*)al_entry == al_end)
			goto not_found;
		if (!al_entry->length)
			break;
		if ((u8*)al_entry + 6 > al_end || (u8*)al_entry +
				le16_to_cpu(al_entry->length) > al_end)
			break;
		next_al_entry = (ATTR_LIST_ENTRY*)((u8*)al_entry +
				le16_to_cpu(al_entry->length));
		if (le32_to_cpu(al_entry->type) > le32_to_cpu(type))
			goto not_found;
		if (type != al_entry->type)
			continue;
		/*
		 * If @name is present, compare the two names.  If @name is
		 * missing, assume we want an unnamed attribute.
		 */
		al_name_len = al_entry->name_length;
		al_name = (ntfschar*)((u8*)al_entry + al_entry->name_offset);
		if (!name) {
			if (al_name_len)
				goto not_found;
		} else if (!ntfs_are_names_equal(al_name, al_name_len, name,
				name_len, ic, vol->upcase, vol->upcase_len)) {
			register int rc;

			rc = ntfs_collate_names(name, name_len, al_name,
					al_name_len, 1, IGNORE_CASE,
					vol->upcase, vol->upcase_len);
			/*
			 * If @name collates before al_name, there is no
			 * matching attribute.
			 */
			if (rc == -1)
				goto not_found;
			/* If the strings are not equal, continue search. */
			if (rc)
				continue;
			/*
			 * FIXME: Reverse engineering showed 0, IGNORE_CASE but
			 * that is inconsistent with ntfs_attr_find().  The
			 * subsequent rc checks were also different.  Perhaps I
			 * made a mistake in one of the two.  Need to recheck
			 * which is correct or at least see what is going on...
			 * (AIA)
			 */
			rc = ntfs_collate_names(name, name_len, al_name,
					al_name_len, 1, CASE_SENSITIVE,
					vol->upcase, vol->upcase_len);
			if (rc == -1)
				goto not_found;
			if (rc)
				continue;
		}
		/*
		 * The names match or @name not present and attribute is
		 * unnamed.  Now check @lowest_vcn.  Continue search if the
		 * next attribute list entry still fits @lowest_vcn.  Otherwise
		 * we have reached the right one or the search has failed.
		 */
		if (lowest_vcn && (u8*)next_al_entry >= al_start	    &&
				(u8*)next_al_entry + 6 < al_end		    &&
				(u8*)next_al_entry + le16_to_cpu(
					next_al_entry->length) <= al_end    &&
				sle64_to_cpu(next_al_entry->lowest_vcn) <=
					lowest_vcn			    &&
				next_al_entry->type == al_entry->type	    &&
				next_al_entry->name_length == al_name_len   &&
				ntfs_are_names_equal((ntfschar*)((u8*)
					next_al_entry +
					next_al_entry->name_offset),
					next_al_entry->name_length,
					al_name, al_name_len, CASE_SENSITIVE,
					vol->upcase, vol->upcase_len))
			continue;
		if (MREF_LE(al_entry->mft_reference) == ni->mft_no) {
			if (MSEQNO_LE(al_entry->mft_reference) != ni->seq_no) {
				ntfs_error(vol->sb, "Found stale mft "
						"reference in attribute list "
						"of base inode 0x%lx.%s",
						base_ni->mft_no, es);
				err = -EIO;
				break;
			}
		} else { /* Mft references do not match. */
			/* If there is a mapped record unmap it first. */
			if (ni != base_ni)
				unmap_extent_mft_record(ni);
			/* Do we want the base record back? */
			if (MREF_LE(al_entry->mft_reference) ==
					base_ni->mft_no) {
				ni = ctx->ntfs_ino = base_ni;
				ctx->mrec = ctx->base_mrec;
			} else {
				/* We want an extent record. */
				ctx->mrec = map_extent_mft_record(base_ni,
						le64_to_cpu(
						al_entry->mft_reference), &ni);
				if (IS_ERR(ctx->mrec)) {
					ntfs_error(vol->sb, "Failed to map "
							"extent mft record "
							"0x%lx of base inode "
							"0x%lx.%s",
							MREF_LE(al_entry->
							mft_reference),
							base_ni->mft_no, es);
					err = PTR_ERR(ctx->mrec);
					if (err == -ENOENT)
						err = -EIO;
					/* Cause @ctx to be sanitized below. */
					ni = NULL;
					break;
				}
				ctx->ntfs_ino = ni;
			}
			ctx->attr = (ATTR_RECORD*)((u8*)ctx->mrec +
					le16_to_cpu(ctx->mrec->attrs_offset));
		}
		/*
		 * ctx->vfs_ino, ctx->mrec, and ctx->attr now point to the
		 * mft record containing the attribute represented by the
		 * current al_entry.
		 */
		/*
		 * We could call into ntfs_attr_find() to find the right
		 * attribute in this mft record but this would be less
		 * efficient and not quite accurate as ntfs_attr_find() ignores
		 * the attribute instance numbers for example which become
		 * important when one plays with attribute lists.  Also,
		 * because a proper match has been found in the attribute list
		 * entry above, the comparison can now be optimized.  So it is
		 * worth re-implementing a simplified ntfs_attr_find() here.
		 */
		a = ctx->attr;
		/*
		 * Use a manual loop so we can still use break and continue
		 * with the same meanings as above.
		 */
do_next_attr_loop:
		if ((u8*)a < (u8*)ctx->mrec || (u8*)a > (u8*)ctx->mrec +
				le32_to_cpu(ctx->mrec->bytes_allocated))
			break;
		if (a->type == AT_END)
			continue;
		if (!a->length)
			break;
		if (al_entry->instance != a->instance)
			goto do_next_attr;
		/*
		 * If the type and/or the name are mismatched between the
		 * attribute list entry and the attribute record, there is
		 * corruption so we break and return error EIO.
		 */
		if (al_entry->type != a->type)
			break;
		if (!ntfs_are_names_equal((ntfschar*)((u8*)a +
				le16_to_cpu(a->name_offset)), a->name_length,
				al_name, al_name_len, CASE_SENSITIVE,
				vol->upcase, vol->upcase_len))
			break;
		ctx->attr = a;
		/*
		 * If no @val specified or @val specified and it matches, we
		 * have found it!
		 */
		if (!val || (!a->non_resident && le32_to_cpu(
				a->data.resident.value_length) == val_len &&
				!memcmp((u8*)a +
				le16_to_cpu(a->data.resident.value_offset),
				val, val_len))) {
			ntfs_debug("Done, found.");
			return 0;
		}
do_next_attr:
		/* Proceed to the next attribute in the current mft record. */
		a = (ATTR_RECORD*)((u8*)a + le32_to_cpu(a->length));
		goto do_next_attr_loop;
	}
	if (!err) {
		ntfs_error(vol->sb, "Base inode 0x%lx contains corrupt "
				"attribute list attribute.%s", base_ni->mft_no,
				es);
		err = -EIO;
	}
	if (ni != base_ni) {
		if (ni)
			unmap_extent_mft_record(ni);
		ctx->ntfs_ino = base_ni;
		ctx->mrec = ctx->base_mrec;
		ctx->attr = ctx->base_attr;
	}
	if (err != -ENOMEM)
		NVolSetErrors(vol);
	return err;
not_found:
	/*
	 * If we were looking for AT_END, we reset the search context @ctx and
	 * use ntfs_attr_find() to seek to the end of the base mft record.
	 */
	if (type == AT_END) {
		ntfs_attr_reinit_search_ctx(ctx);
		return ntfs_attr_find(AT_END, name, name_len, ic, val, val_len,
				ctx);
	}
	/*
	 * The attribute was not found.  Before we return, we want to ensure
	 * @ctx->mrec and @ctx->attr indicate the position at which the
	 * attribute should be inserted in the base mft record.  Since we also
	 * want to preserve @ctx->al_entry we cannot reinitialize the search
	 * context using ntfs_attr_reinit_search_ctx() as this would set
	 * @ctx->al_entry to NULL.  Thus we do the necessary bits manually (see
	 * ntfs_attr_init_search_ctx() below).  Note, we _only_ preserve
	 * @ctx->al_entry as the remaining fields (base_*) are identical to
	 * their non base_ counterparts and we cannot set @ctx->base_attr
	 * correctly yet as we do not know what @ctx->attr will be set to by
	 * the call to ntfs_attr_find() below.
	 */
	if (ni != base_ni)
		unmap_extent_mft_record(ni);
	ctx->mrec = ctx->base_mrec;
	ctx->attr = (ATTR_RECORD*)((u8*)ctx->mrec +
			le16_to_cpu(ctx->mrec->attrs_offset));
	ctx->is_first = TRUE;
	ctx->ntfs_ino = base_ni;
	ctx->base_ntfs_ino = NULL;
	ctx->base_mrec = NULL;
	ctx->base_attr = NULL;
	/*
	 * In case there are multiple matches in the base mft record, need to
	 * keep enumerating until we get an attribute not found response (or
	 * another error), otherwise we would keep returning the same attribute
	 * over and over again and all programs using us for enumeration would
	 * lock up in a tight loop.
	 */
	do {
		err = ntfs_attr_find(type, name, name_len, ic, val, val_len,
				ctx);
	} while (!err);
	ntfs_debug("Done, not found.");
	return err;
}

/**
 * ntfs_attr_lookup - find an attribute in an ntfs inode
 * @type:	attribute type to find
 * @name:	attribute name to find (optional, i.e. NULL means don't care)
 * @name_len:	attribute name length (only needed if @name present)
 * @ic:		IGNORE_CASE or CASE_SENSITIVE (ignored if @name not present)
 * @lowest_vcn:	lowest vcn to find (optional, non-resident attributes only)
 * @val:	attribute value to find (optional, resident attributes only)
 * @val_len:	attribute value length
 * @ctx:	search context with mft record and attribute to search from
 *
 * Find an attribute in an ntfs inode.  On first search @ctx->ntfs_ino must
 * be the base mft record and @ctx must have been obtained from a call to
 * ntfs_attr_get_search_ctx().
 *
 * This function transparently handles attribute lists and @ctx is used to
 * continue searches where they were left off at.
 *
 * After finishing with the attribute/mft record you need to call
 * ntfs_attr_put_search_ctx() to cleanup the search context (unmapping any
 * mapped inodes, etc).
 *
 * Return 0 if the search was successful and -errno if not.
 *
 * When 0, @ctx->attr is the found attribute and it is in mft record
 * @ctx->mrec.  If an attribute list attribute is present, @ctx->al_entry is
 * the attribute list entry of the found attribute.
 *
 * When -ENOENT, @ctx->attr is the attribute which collates just after the
 * attribute being searched for, i.e. if one wants to add the attribute to the
 * mft record this is the correct place to insert it into.  If an attribute
 * list attribute is present, @ctx->al_entry is the attribute list entry which
 * collates just after the attribute list entry of the attribute being searched
 * for, i.e. if one wants to add the attribute to the mft record this is the
 * correct place to insert its attribute list entry into.
 *
 * When -errno != -ENOENT, an error occured during the lookup.  @ctx->attr is
 * then undefined and in particular you should not rely on it not changing.
 */
int ntfs_attr_lookup(const ATTR_TYPE type, const ntfschar *name,
		const u32 name_len, const IGNORE_CASE_BOOL ic,
		const VCN lowest_vcn, const u8 *val, const u32 val_len,
		ntfs_attr_search_ctx *ctx)
{
	ntfs_inode *base_ni;

	ntfs_debug("Entering.");
	if (ctx->base_ntfs_ino)
		base_ni = ctx->base_ntfs_ino;
	else
		base_ni = ctx->ntfs_ino;
	/* Sanity check, just for debugging really. */
	BUG_ON(!base_ni);
	if (!NInoAttrList(base_ni) || type == AT_ATTRIBUTE_LIST)
		return ntfs_attr_find(type, name, name_len, ic, val, val_len,
				ctx);
	return ntfs_external_attr_find(type, name, name_len, ic, lowest_vcn,
			val, val_len, ctx);
}

/**
 * ntfs_attr_init_search_ctx - initialize an attribute search context
 * @ctx:	attribute search context to initialize
 * @ni:		ntfs inode with which to initialize the search context
 * @mrec:	mft record with which to initialize the search context
 *
 * Initialize the attribute search context @ctx with @ni and @mrec.
 */
static inline void ntfs_attr_init_search_ctx(ntfs_attr_search_ctx *ctx,
		ntfs_inode *ni, MFT_RECORD *mrec)
{
	ctx->mrec = mrec;
	/* Sanity checks are performed elsewhere. */
	ctx->attr = (ATTR_RECORD*)((u8*)mrec + le16_to_cpu(mrec->attrs_offset));
	ctx->is_first = TRUE;
	ctx->ntfs_ino = ni;
	ctx->al_entry = NULL;
	ctx->base_ntfs_ino = NULL;
	ctx->base_mrec = NULL;
	ctx->base_attr = NULL;
}

/**
 * ntfs_attr_reinit_search_ctx - reinitialize an attribute search context
 * @ctx:	attribute search context to reinitialize
 *
 * Reinitialize the attribute search context @ctx, unmapping an associated
 * extent mft record if present, and initialize the search context again.
 *
 * This is used when a search for a new attribute is being started to reset
 * the search context to the beginning.
 */
void ntfs_attr_reinit_search_ctx(ntfs_attr_search_ctx *ctx)
{
	if (likely(!ctx->base_ntfs_ino)) {
		/* No attribute list. */
		ctx->is_first = TRUE;
		/* Sanity checks are performed elsewhere. */
		ctx->attr = (ATTR_RECORD*)((u8*)ctx->mrec +
				le16_to_cpu(ctx->mrec->attrs_offset));
		/*
		 * This needs resetting due to ntfs_external_attr_find() which
		 * can leave it set despite having zeroed ctx->base_ntfs_ino.
		 */
		ctx->al_entry = NULL;
		return;
	} /* Attribute list. */
	if (ctx->ntfs_ino != ctx->base_ntfs_ino)
		unmap_extent_mft_record(ctx->ntfs_ino);
	ntfs_attr_init_search_ctx(ctx, ctx->base_ntfs_ino, ctx->base_mrec);
	return;
}

/**
 * ntfs_attr_get_search_ctx - allocate/initialize a new attribute search context
 * @ni:		ntfs inode with which to initialize the search context
 * @mrec:	mft record with which to initialize the search context
 *
 * Allocate a new attribute search context, initialize it with @ni and @mrec,
 * and return it. Return NULL if allocation failed.
 */
ntfs_attr_search_ctx *ntfs_attr_get_search_ctx(ntfs_inode *ni, MFT_RECORD *mrec)
{
	ntfs_attr_search_ctx *ctx;

	ctx = kmem_cache_alloc(ntfs_attr_ctx_cache, SLAB_NOFS);
	if (ctx)
		ntfs_attr_init_search_ctx(ctx, ni, mrec);
	return ctx;
}

/**
 * ntfs_attr_put_search_ctx - release an attribute search context
 * @ctx:	attribute search context to free
 *
 * Release the attribute search context @ctx, unmapping an associated extent
 * mft record if present.
 */
void ntfs_attr_put_search_ctx(ntfs_attr_search_ctx *ctx)
{
	if (ctx->base_ntfs_ino && ctx->ntfs_ino != ctx->base_ntfs_ino)
		unmap_extent_mft_record(ctx->ntfs_ino);
	kmem_cache_free(ntfs_attr_ctx_cache, ctx);
	return;
}

/**
 * ntfs_attr_find_in_attrdef - find an attribute in the $AttrDef system file
 * @vol:	ntfs volume to which the attribute belongs
 * @type:	attribute type which to find
 *
 * Search for the attribute definition record corresponding to the attribute
 * @type in the $AttrDef system file.
 *
 * Return the attribute type definition record if found and NULL if not found.
 */
static ATTR_DEF *ntfs_attr_find_in_attrdef(const ntfs_volume *vol,
		const ATTR_TYPE type)
{
	ATTR_DEF *ad;

	BUG_ON(!vol->attrdef);
	BUG_ON(!type);
	for (ad = vol->attrdef; (u8*)ad - (u8*)vol->attrdef <
			vol->attrdef_size && ad->type; ++ad) {
		/* We have not found it yet, carry on searching. */
		if (likely(le32_to_cpu(ad->type) < le32_to_cpu(type)))
			continue;
		/* We found the attribute; return it. */
		if (likely(ad->type == type))
			return ad;
		/* We have gone too far already.  No point in continuing. */
		break;
	}
	/* Attribute not found. */
	ntfs_debug("Attribute type 0x%x not found in $AttrDef.",
			le32_to_cpu(type));
	return NULL;
}

/**
 * ntfs_attr_size_bounds_check - check a size of an attribute type for validity
 * @vol:	ntfs volume to which the attribute belongs
 * @type:	attribute type which to check
 * @size:	size which to check
 *
 * Check whether the @size in bytes is valid for an attribute of @type on the
 * ntfs volume @vol.  This information is obtained from $AttrDef system file.
 *
 * Return 0 if valid, -ERANGE if not valid, or -ENOENT if the attribute is not
 * listed in $AttrDef.
 */
int ntfs_attr_size_bounds_check(const ntfs_volume *vol, const ATTR_TYPE type,
		const s64 size)
{
	ATTR_DEF *ad;

	BUG_ON(size < 0);
	/*
	 * $ATTRIBUTE_LIST has a maximum size of 256kiB, but this is not
	 * listed in $AttrDef.
	 */
	if (unlikely(type == AT_ATTRIBUTE_LIST && size > 256 * 1024))
		return -ERANGE;
	/* Get the $AttrDef entry for the attribute @type. */
	ad = ntfs_attr_find_in_attrdef(vol, type);
	if (unlikely(!ad))
		return -ENOENT;
	/* Do the bounds check. */
	if (((sle64_to_cpu(ad->min_size) > 0) &&
			size < sle64_to_cpu(ad->min_size)) ||
			((sle64_to_cpu(ad->max_size) > 0) && size >
			sle64_to_cpu(ad->max_size)))
		return -ERANGE;
	return 0;
}

/**
 * ntfs_attr_can_be_non_resident - check if an attribute can be non-resident
 * @vol:	ntfs volume to which the attribute belongs
 * @type:	attribute type which to check
 *
 * Check whether the attribute of @type on the ntfs volume @vol is allowed to
 * be non-resident.  This information is obtained from $AttrDef system file.
 *
 * Return 0 if the attribute is allowed to be non-resident, -EPERM if not, or
 * -ENOENT if the attribute is not listed in $AttrDef.
 */
int ntfs_attr_can_be_non_resident(const ntfs_volume *vol, const ATTR_TYPE type)
{
	ATTR_DEF *ad;

	/*
	 * $DATA is always allowed to be non-resident even if $AttrDef does not
	 * specify this in the flags of the $DATA attribute definition record.
	 */
	if (type == AT_DATA)
		return 0;
	/* Find the attribute definition record in $AttrDef. */
	ad = ntfs_attr_find_in_attrdef(vol, type);
	if (unlikely(!ad))
		return -ENOENT;
	/* Check the flags and return the result. */
	if (ad->flags & CAN_BE_NON_RESIDENT)
		return 0;
	return -EPERM;
}

/**
 * ntfs_attr_can_be_resident - check if an attribute can be resident
 * @vol:	ntfs volume to which the attribute belongs
 * @type:	attribute type which to check
 *
 * Check whether the attribute of @type on the ntfs volume @vol is allowed to
 * be resident.  This information is derived from our ntfs knowledge and may
 * not be completely accurate, especially when user defined attributes are
 * present.  Basically we allow everything to be resident except for index
 * allocation and $EA attributes.
 *
 * Return 0 if the attribute is allowed to be non-resident and -EPERM if not.
 *
 * Warning: In the system file $MFT the attribute $Bitmap must be non-resident
 *	    otherwise windows will not boot (blue screen of death)!  We cannot
 *	    check for this here as we do not know which inode's $Bitmap is
 *	    being asked about so the caller needs to special case this.
 */
int ntfs_attr_can_be_resident(const ntfs_volume *vol, const ATTR_TYPE type)
{
	if (type != AT_INDEX_ALLOCATION && type != AT_EA)
		return 0;
	return -EPERM;
}

/**
 * ntfs_attr_record_resize - resize an attribute record
 * @m:		mft record containing attribute record
 * @a:		attribute record to resize
 * @new_size:	new size in bytes to which to resize the attribute record @a
 *
 * Resize the attribute record @a, i.e. the resident part of the attribute, in
 * the mft record @m to @new_size bytes.
 *
 * Return 0 on success and -errno on error.  The following error codes are
 * defined:
 *	-ENOSPC	- Not enough space in the mft record @m to perform the resize.
 *
 * Note: On error, no modifications have been performed whatsoever.
 *
 * Warning: If you make a record smaller without having copied all the data you
 *	    are interested in the data may be overwritten.
 */
int ntfs_attr_record_resize(MFT_RECORD *m, ATTR_RECORD *a, u32 new_size)
{
	ntfs_debug("Entering for new_size %u.", new_size);
	/* Align to 8 bytes if it is not already done. */
	if (new_size & 7)
		new_size = (new_size + 7) & ~7;
	/* If the actual attribute length has changed, move things around. */
	if (new_size != le32_to_cpu(a->length)) {
		u32 new_muse = le32_to_cpu(m->bytes_in_use) -
				le32_to_cpu(a->length) + new_size;
		/* Not enough space in this mft record. */
		if (new_muse > le32_to_cpu(m->bytes_allocated))
			return -ENOSPC;
		/* Move attributes following @a to their new location. */
		memmove((u8*)a + new_size, (u8*)a + le32_to_cpu(a->length),
				le32_to_cpu(m->bytes_in_use) - ((u8*)a -
				(u8*)m) - le32_to_cpu(a->length));
		/* Adjust @m to reflect the change in used space. */
		m->bytes_in_use = cpu_to_le32(new_muse);
		/* Adjust @a to reflect the new size. */
		if (new_size >= offsetof(ATTR_REC, length) + sizeof(a->length))
			a->length = cpu_to_le32(new_size);
	}
	return 0;
}

/**
 * ntfs_attr_set - fill (a part of) an attribute with a byte
 * @ni:		ntfs inode describing the attribute to fill
 * @ofs:	offset inside the attribute at which to start to fill
 * @cnt:	number of bytes to fill
 * @val:	the unsigned 8-bit value with which to fill the attribute
 *
 * Fill @cnt bytes of the attribute described by the ntfs inode @ni starting at
 * byte offset @ofs inside the attribute with the constant byte @val.
 *
 * This function is effectively like memset() applied to an ntfs attribute.
 *
 * Return 0 on success and -errno on error.  An error code of -ESPIPE means
 * that @ofs + @cnt were outside the end of the attribute and no write was
 * performed.
 */
int ntfs_attr_set(ntfs_inode *ni, const s64 ofs, const s64 cnt, const u8 val)
{
	ntfs_volume *vol = ni->vol;
	struct address_space *mapping;
	struct page *page;
	u8 *kaddr;
	pgoff_t idx, end;
	unsigned int start_ofs, end_ofs, size;

	ntfs_debug("Entering for ofs 0x%llx, cnt 0x%llx, val 0x%hx.",
			(long long)ofs, (long long)cnt, val);
	BUG_ON(ofs < 0);
	BUG_ON(cnt < 0);
	if (!cnt)
		goto done;
	mapping = VFS_I(ni)->i_mapping;
	/* Work out the starting index and page offset. */
	idx = ofs >> PAGE_CACHE_SHIFT;
	start_ofs = ofs & ~PAGE_CACHE_MASK;
	/* Work out the ending index and page offset. */
	end = ofs + cnt;
	end_ofs = end & ~PAGE_CACHE_MASK;
	/* If the end is outside the inode size return -ESPIPE. */
	if (unlikely(end > VFS_I(ni)->i_size)) {
		ntfs_error(vol->sb, "Request exceeds end of attribute.");
		return -ESPIPE;
	}
	end >>= PAGE_CACHE_SHIFT;
	/* If there is a first partial page, need to do it the slow way. */
	if (start_ofs) {
		page = read_cache_page(mapping, idx,
				(filler_t*)mapping->a_ops->readpage, NULL);
		if (IS_ERR(page)) {
			ntfs_error(vol->sb, "Failed to read first partial "
					"page (sync error, index 0x%lx).", idx);
			return PTR_ERR(page);
		}
		wait_on_page_locked(page);
		if (unlikely(!PageUptodate(page))) {
			ntfs_error(vol->sb, "Failed to read first partial page "
					"(async error, index 0x%lx).", idx);
			page_cache_release(page);
			return PTR_ERR(page);
		}
		/*
		 * If the last page is the same as the first page, need to
		 * limit the write to the end offset.
		 */
		size = PAGE_CACHE_SIZE;
		if (idx == end)
			size = end_ofs;
		kaddr = kmap_atomic(page, KM_USER0);
		memset(kaddr + start_ofs, val, size - start_ofs);
		flush_dcache_page(page);
		kunmap_atomic(kaddr, KM_USER0);
		set_page_dirty(page);
		page_cache_release(page);
		if (idx == end)
			goto done;
		idx++;
	}
	/* Do the whole pages the fast way. */
	for (; idx < end; idx++) {
		/* Find or create the current page.  (The page is locked.) */
		page = grab_cache_page(mapping, idx);
		if (unlikely(!page)) {
			ntfs_error(vol->sb, "Insufficient memory to grab "
					"page (index 0x%lx).", idx);
			return -ENOMEM;
		}
		kaddr = kmap_atomic(page, KM_USER0);
		memset(kaddr, val, PAGE_CACHE_SIZE);
		flush_dcache_page(page);
		kunmap_atomic(kaddr, KM_USER0);
		/*
		 * If the page has buffers, mark them uptodate since buffer
		 * state and not page state is definitive in 2.6 kernels.
		 */
		if (page_has_buffers(page)) {
			struct buffer_head *bh, *head;

			bh = head = page_buffers(page);
			do {
				set_buffer_uptodate(bh);
			} while ((bh = bh->b_this_page) != head);
		}
		/* Now that buffers are uptodate, set the page uptodate, too. */
		SetPageUptodate(page);
		/*
		 * Set the page and all its buffers dirty and mark the inode
		 * dirty, too.  The VM will write the page later on.
		 */
		set_page_dirty(page);
		/* Finally unlock and release the page. */
		unlock_page(page);
		page_cache_release(page);
	}
	/* If there is a last partial page, need to do it the slow way. */
	if (end_ofs) {
		page = read_cache_page(mapping, idx,
				(filler_t*)mapping->a_ops->readpage, NULL);
		if (IS_ERR(page)) {
			ntfs_error(vol->sb, "Failed to read last partial page "
					"(sync error, index 0x%lx).", idx);
			return PTR_ERR(page);
		}
		wait_on_page_locked(page);
		if (unlikely(!PageUptodate(page))) {
			ntfs_error(vol->sb, "Failed to read last partial page "
					"(async error, index 0x%lx).", idx);
			page_cache_release(page);
			return PTR_ERR(page);
		}
		kaddr = kmap_atomic(page, KM_USER0);
		memset(kaddr, val, end_ofs);
		flush_dcache_page(page);
		kunmap_atomic(kaddr, KM_USER0);
		set_page_dirty(page);
		page_cache_release(page);
	}
done:
	ntfs_debug("Done.");
	return 0;
}
