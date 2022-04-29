// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * attrib.c - NTFS attribute operations.  Part of the Linux-NTFS project.
 *
 * Copyright (c) 2001-2012 Anton Altaparmakov and Tuxera Inc.
 * Copyright (c) 2002 Richard Russon
 */

#include <linux/buffer_head.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/swap.h>
#include <linux/writeback.h>

#include "attrib.h"
#include "debug.h"
#include "layout.h"
#include "lcnalloc.h"
#include "malloc.h"
#include "mft.h"
#include "ntfs.h"
#include "types.h"

/**
 * ntfs_map_runlist_nolock - map (a part of) a runlist of an ntfs inode
 * @ni:		ntfs inode for which to map (part of) a runlist
 * @vcn:	map runlist part containing this vcn
 * @ctx:	active attribute search context if present or NULL if not
 *
 * Map the part of a runlist containing the @vcn of the ntfs inode @ni.
 *
 * If @ctx is specified, it is an active search context of @ni and its base mft
 * record.  This is needed when ntfs_map_runlist_nolock() encounters unmapped
 * runlist fragments and allows their mapping.  If you do not have the mft
 * record mapped, you can specify @ctx as NULL and ntfs_map_runlist_nolock()
 * will perform the necessary mapping and unmapping.
 *
 * Note, ntfs_map_runlist_nolock() saves the state of @ctx on entry and
 * restores it before returning.  Thus, @ctx will be left pointing to the same
 * attribute on return as on entry.  However, the actual pointers in @ctx may
 * point to different memory locations on return, so you must remember to reset
 * any cached pointers from the @ctx, i.e. after the call to
 * ntfs_map_runlist_nolock(), you will probably want to do:
 *	m = ctx->mrec;
 *	a = ctx->attr;
 * Assuming you cache ctx->attr in a variable @a of type ATTR_RECORD * and that
 * you cache ctx->mrec in a variable @m of type MFT_RECORD *.
 *
 * Return 0 on success and -errno on error.  There is one special error code
 * which is not an error as such.  This is -ENOENT.  It means that @vcn is out
 * of bounds of the runlist.
 *
 * Note the runlist can be NULL after this function returns if @vcn is zero and
 * the attribute has zero allocated size, i.e. there simply is no runlist.
 *
 * WARNING: If @ctx is supplied, regardless of whether success or failure is
 *	    returned, you need to check IS_ERR(@ctx->mrec) and if 'true' the @ctx
 *	    is no longer valid, i.e. you need to either call
 *	    ntfs_attr_reinit_search_ctx() or ntfs_attr_put_search_ctx() on it.
 *	    In that case PTR_ERR(@ctx->mrec) will give you the error code for
 *	    why the mapping of the old inode failed.
 *
 * Locking: - The runlist described by @ni must be locked for writing on entry
 *	      and is locked on return.  Note the runlist will be modified.
 *	    - If @ctx is NULL, the base mft record of @ni must not be mapped on
 *	      entry and it will be left unmapped on return.
 *	    - If @ctx is not NULL, the base mft record must be mapped on entry
 *	      and it will be left mapped on return.
 */
int ntfs_map_runlist_nolock(ntfs_inode *ni, VCN vcn, ntfs_attr_search_ctx *ctx)
{
	VCN end_vcn;
	unsigned long flags;
	ntfs_inode *base_ni;
	MFT_RECORD *m;
	ATTR_RECORD *a;
	runlist_element *rl;
	struct page *put_this_page = NULL;
	int err = 0;
	bool ctx_is_temporary, ctx_needs_reset;
	ntfs_attr_search_ctx old_ctx = { NULL, };

	ntfs_debug("Mapping runlist part containing vcn 0x%llx.",
			(unsigned long long)vcn);
	if (!NInoAttr(ni))
		base_ni = ni;
	else
		base_ni = ni->ext.base_ntfs_ino;
	if (!ctx) {
		ctx_is_temporary = ctx_needs_reset = true;
		m = map_mft_record(base_ni);
		if (IS_ERR(m))
			return PTR_ERR(m);
		ctx = ntfs_attr_get_search_ctx(base_ni, m);
		if (unlikely(!ctx)) {
			err = -ENOMEM;
			goto err_out;
		}
	} else {
		VCN allocated_size_vcn;

		BUG_ON(IS_ERR(ctx->mrec));
		a = ctx->attr;
		BUG_ON(!a->non_resident);
		ctx_is_temporary = false;
		end_vcn = sle64_to_cpu(a->data.non_resident.highest_vcn);
		read_lock_irqsave(&ni->size_lock, flags);
		allocated_size_vcn = ni->allocated_size >>
				ni->vol->cluster_size_bits;
		read_unlock_irqrestore(&ni->size_lock, flags);
		if (!a->data.non_resident.lowest_vcn && end_vcn <= 0)
			end_vcn = allocated_size_vcn - 1;
		/*
		 * If we already have the attribute extent containing @vcn in
		 * @ctx, no need to look it up again.  We slightly cheat in
		 * that if vcn exceeds the allocated size, we will refuse to
		 * map the runlist below, so there is definitely no need to get
		 * the right attribute extent.
		 */
		if (vcn >= allocated_size_vcn || (a->type == ni->type &&
				a->name_length == ni->name_len &&
				!memcmp((u8*)a + le16_to_cpu(a->name_offset),
				ni->name, ni->name_len) &&
				sle64_to_cpu(a->data.non_resident.lowest_vcn)
				<= vcn && end_vcn >= vcn))
			ctx_needs_reset = false;
		else {
			/* Save the old search context. */
			old_ctx = *ctx;
			/*
			 * If the currently mapped (extent) inode is not the
			 * base inode we will unmap it when we reinitialize the
			 * search context which means we need to get a
			 * reference to the page containing the mapped mft
			 * record so we do not accidentally drop changes to the
			 * mft record when it has not been marked dirty yet.
			 */
			if (old_ctx.base_ntfs_ino && old_ctx.ntfs_ino !=
					old_ctx.base_ntfs_ino) {
				put_this_page = old_ctx.ntfs_ino->page;
				get_page(put_this_page);
			}
			/*
			 * Reinitialize the search context so we can lookup the
			 * needed attribute extent.
			 */
			ntfs_attr_reinit_search_ctx(ctx);
			ctx_needs_reset = true;
		}
	}
	if (ctx_needs_reset) {
		err = ntfs_attr_lookup(ni->type, ni->name, ni->name_len,
				CASE_SENSITIVE, vcn, NULL, 0, ctx);
		if (unlikely(err)) {
			if (err == -ENOENT)
				err = -EIO;
			goto err_out;
		}
		BUG_ON(!ctx->attr->non_resident);
	}
	a = ctx->attr;
	/*
	 * Only decompress the mapping pairs if @vcn is inside it.  Otherwise
	 * we get into problems when we try to map an out of bounds vcn because
	 * we then try to map the already mapped runlist fragment and
	 * ntfs_mapping_pairs_decompress() fails.
	 */
	end_vcn = sle64_to_cpu(a->data.non_resident.highest_vcn) + 1;
	if (unlikely(vcn && vcn >= end_vcn)) {
		err = -ENOENT;
		goto err_out;
	}
	rl = ntfs_mapping_pairs_decompress(ni->vol, a, ni->runlist.rl);
	if (IS_ERR(rl))
		err = PTR_ERR(rl);
	else
		ni->runlist.rl = rl;
err_out:
	if (ctx_is_temporary) {
		if (likely(ctx))
			ntfs_attr_put_search_ctx(ctx);
		unmap_mft_record(base_ni);
	} else if (ctx_needs_reset) {
		/*
		 * If there is no attribute list, restoring the search context
		 * is accomplished simply by copying the saved context back over
		 * the caller supplied context.  If there is an attribute list,
		 * things are more complicated as we need to deal with mapping
		 * of mft records and resulting potential changes in pointers.
		 */
		if (NInoAttrList(base_ni)) {
			/*
			 * If the currently mapped (extent) inode is not the
			 * one we had before, we need to unmap it and map the
			 * old one.
			 */
			if (ctx->ntfs_ino != old_ctx.ntfs_ino) {
				/*
				 * If the currently mapped inode is not the
				 * base inode, unmap it.
				 */
				if (ctx->base_ntfs_ino && ctx->ntfs_ino !=
						ctx->base_ntfs_ino) {
					unmap_extent_mft_record(ctx->ntfs_ino);
					ctx->mrec = ctx->base_mrec;
					BUG_ON(!ctx->mrec);
				}
				/*
				 * If the old mapped inode is not the base
				 * inode, map it.
				 */
				if (old_ctx.base_ntfs_ino &&
						old_ctx.ntfs_ino !=
						old_ctx.base_ntfs_ino) {
retry_map:
					ctx->mrec = map_mft_record(
							old_ctx.ntfs_ino);
					/*
					 * Something bad has happened.  If out
					 * of memory retry till it succeeds.
					 * Any other errors are fatal and we
					 * return the error code in ctx->mrec.
					 * Let the caller deal with it...  We
					 * just need to fudge things so the
					 * caller can reinit and/or put the
					 * search context safely.
					 */
					if (IS_ERR(ctx->mrec)) {
						if (PTR_ERR(ctx->mrec) ==
								-ENOMEM) {
							schedule();
							goto retry_map;
						} else
							old_ctx.ntfs_ino =
								old_ctx.
								base_ntfs_ino;
					}
				}
			}
			/* Update the changed pointers in the saved context. */
			if (ctx->mrec != old_ctx.mrec) {
				if (!IS_ERR(ctx->mrec))
					old_ctx.attr = (ATTR_RECORD*)(
							(u8*)ctx->mrec +
							((u8*)old_ctx.attr -
							(u8*)old_ctx.mrec));
				old_ctx.mrec = ctx->mrec;
			}
		}
		/* Restore the search context to the saved one. */
		*ctx = old_ctx;
		/*
		 * We drop the reference on the page we took earlier.  In the
		 * case that IS_ERR(ctx->mrec) is true this means we might lose
		 * some changes to the mft record that had been made between
		 * the last time it was marked dirty/written out and now.  This
		 * at this stage is not a problem as the mapping error is fatal
		 * enough that the mft record cannot be written out anyway and
		 * the caller is very likely to shutdown the whole inode
		 * immediately and mark the volume dirty for chkdsk to pick up
		 * the pieces anyway.
		 */
		if (put_this_page)
			put_page(put_this_page);
	}
	return err;
}

/**
 * ntfs_map_runlist - map (a part of) a runlist of an ntfs inode
 * @ni:		ntfs inode for which to map (part of) a runlist
 * @vcn:	map runlist part containing this vcn
 *
 * Map the part of a runlist containing the @vcn of the ntfs inode @ni.
 *
 * Return 0 on success and -errno on error.  There is one special error code
 * which is not an error as such.  This is -ENOENT.  It means that @vcn is out
 * of bounds of the runlist.
 *
 * Locking: - The runlist must be unlocked on entry and is unlocked on return.
 *	    - This function takes the runlist lock for writing and may modify
 *	      the runlist.
 */
int ntfs_map_runlist(ntfs_inode *ni, VCN vcn)
{
	int err = 0;

	down_write(&ni->runlist.lock);
	/* Make sure someone else didn't do the work while we were sleeping. */
	if (likely(ntfs_rl_vcn_to_lcn(ni->runlist.rl, vcn) <=
			LCN_RL_NOT_MAPPED))
		err = ntfs_map_runlist_nolock(ni, vcn, NULL);
	up_write(&ni->runlist.lock);
	return err;
}

/**
 * ntfs_attr_vcn_to_lcn_nolock - convert a vcn into a lcn given an ntfs inode
 * @ni:			ntfs inode of the attribute whose runlist to search
 * @vcn:		vcn to convert
 * @write_locked:	true if the runlist is locked for writing
 *
 * Find the virtual cluster number @vcn in the runlist of the ntfs attribute
 * described by the ntfs inode @ni and return the corresponding logical cluster
 * number (lcn).
 *
 * If the @vcn is not mapped yet, the attempt is made to map the attribute
 * extent containing the @vcn and the vcn to lcn conversion is retried.
 *
 * If @write_locked is true the caller has locked the runlist for writing and
 * if false for reading.
 *
 * Since lcns must be >= 0, we use negative return codes with special meaning:
 *
 * Return code	Meaning / Description
 * ==========================================
 *  LCN_HOLE	Hole / not allocated on disk.
 *  LCN_ENOENT	There is no such vcn in the runlist, i.e. @vcn is out of bounds.
 *  LCN_ENOMEM	Not enough memory to map runlist.
 *  LCN_EIO	Critical error (runlist/file is corrupt, i/o error, etc).
 *
 * Locking: - The runlist must be locked on entry and is left locked on return.
 *	    - If @write_locked is 'false', i.e. the runlist is locked for reading,
 *	      the lock may be dropped inside the function so you cannot rely on
 *	      the runlist still being the same when this function returns.
 */
LCN ntfs_attr_vcn_to_lcn_nolock(ntfs_inode *ni, const VCN vcn,
		const bool write_locked)
{
	LCN lcn;
	unsigned long flags;
	bool is_retry = false;

	BUG_ON(!ni);
	ntfs_debug("Entering for i_ino 0x%lx, vcn 0x%llx, %s_locked.",
			ni->mft_no, (unsigned long long)vcn,
			write_locked ? "write" : "read");
	BUG_ON(!NInoNonResident(ni));
	BUG_ON(vcn < 0);
	if (!ni->runlist.rl) {
		read_lock_irqsave(&ni->size_lock, flags);
		if (!ni->allocated_size) {
			read_unlock_irqrestore(&ni->size_lock, flags);
			return LCN_ENOENT;
		}
		read_unlock_irqrestore(&ni->size_lock, flags);
	}
retry_remap:
	/* Convert vcn to lcn.  If that fails map the runlist and retry once. */
	lcn = ntfs_rl_vcn_to_lcn(ni->runlist.rl, vcn);
	if (likely(lcn >= LCN_HOLE)) {
		ntfs_debug("Done, lcn 0x%llx.", (long long)lcn);
		return lcn;
	}
	if (lcn != LCN_RL_NOT_MAPPED) {
		if (lcn != LCN_ENOENT)
			lcn = LCN_EIO;
	} else if (!is_retry) {
		int err;

		if (!write_locked) {
			up_read(&ni->runlist.lock);
			down_write(&ni->runlist.lock);
			if (unlikely(ntfs_rl_vcn_to_lcn(ni->runlist.rl, vcn) !=
					LCN_RL_NOT_MAPPED)) {
				up_write(&ni->runlist.lock);
				down_read(&ni->runlist.lock);
				goto retry_remap;
			}
		}
		err = ntfs_map_runlist_nolock(ni, vcn, NULL);
		if (!write_locked) {
			up_write(&ni->runlist.lock);
			down_read(&ni->runlist.lock);
		}
		if (likely(!err)) {
			is_retry = true;
			goto retry_remap;
		}
		if (err == -ENOENT)
			lcn = LCN_ENOENT;
		else if (err == -ENOMEM)
			lcn = LCN_ENOMEM;
		else
			lcn = LCN_EIO;
	}
	if (lcn != LCN_ENOENT)
		ntfs_error(ni->vol->sb, "Failed with error code %lli.",
				(long long)lcn);
	return lcn;
}

/**
 * ntfs_attr_find_vcn_nolock - find a vcn in the runlist of an ntfs inode
 * @ni:		ntfs inode describing the runlist to search
 * @vcn:	vcn to find
 * @ctx:	active attribute search context if present or NULL if not
 *
 * Find the virtual cluster number @vcn in the runlist described by the ntfs
 * inode @ni and return the address of the runlist element containing the @vcn.
 *
 * If the @vcn is not mapped yet, the attempt is made to map the attribute
 * extent containing the @vcn and the vcn to lcn conversion is retried.
 *
 * If @ctx is specified, it is an active search context of @ni and its base mft
 * record.  This is needed when ntfs_attr_find_vcn_nolock() encounters unmapped
 * runlist fragments and allows their mapping.  If you do not have the mft
 * record mapped, you can specify @ctx as NULL and ntfs_attr_find_vcn_nolock()
 * will perform the necessary mapping and unmapping.
 *
 * Note, ntfs_attr_find_vcn_nolock() saves the state of @ctx on entry and
 * restores it before returning.  Thus, @ctx will be left pointing to the same
 * attribute on return as on entry.  However, the actual pointers in @ctx may
 * point to different memory locations on return, so you must remember to reset
 * any cached pointers from the @ctx, i.e. after the call to
 * ntfs_attr_find_vcn_nolock(), you will probably want to do:
 *	m = ctx->mrec;
 *	a = ctx->attr;
 * Assuming you cache ctx->attr in a variable @a of type ATTR_RECORD * and that
 * you cache ctx->mrec in a variable @m of type MFT_RECORD *.
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
 * WARNING: If @ctx is supplied, regardless of whether success or failure is
 *	    returned, you need to check IS_ERR(@ctx->mrec) and if 'true' the @ctx
 *	    is no longer valid, i.e. you need to either call
 *	    ntfs_attr_reinit_search_ctx() or ntfs_attr_put_search_ctx() on it.
 *	    In that case PTR_ERR(@ctx->mrec) will give you the error code for
 *	    why the mapping of the old inode failed.
 *
 * Locking: - The runlist described by @ni must be locked for writing on entry
 *	      and is locked on return.  Note the runlist may be modified when
 *	      needed runlist fragments need to be mapped.
 *	    - If @ctx is NULL, the base mft record of @ni must not be mapped on
 *	      entry and it will be left unmapped on return.
 *	    - If @ctx is not NULL, the base mft record must be mapped on entry
 *	      and it will be left mapped on return.
 */
runlist_element *ntfs_attr_find_vcn_nolock(ntfs_inode *ni, const VCN vcn,
		ntfs_attr_search_ctx *ctx)
{
	unsigned long flags;
	runlist_element *rl;
	int err = 0;
	bool is_retry = false;

	BUG_ON(!ni);
	ntfs_debug("Entering for i_ino 0x%lx, vcn 0x%llx, with%s ctx.",
			ni->mft_no, (unsigned long long)vcn, ctx ? "" : "out");
	BUG_ON(!NInoNonResident(ni));
	BUG_ON(vcn < 0);
	if (!ni->runlist.rl) {
		read_lock_irqsave(&ni->size_lock, flags);
		if (!ni->allocated_size) {
			read_unlock_irqrestore(&ni->size_lock, flags);
			return ERR_PTR(-ENOENT);
		}
		read_unlock_irqrestore(&ni->size_lock, flags);
	}
retry_remap:
	rl = ni->runlist.rl;
	if (likely(rl && vcn >= rl[0].vcn)) {
		while (likely(rl->length)) {
			if (unlikely(vcn < rl[1].vcn)) {
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
	if (!err && !is_retry) {
		/*
		 * If the search context is invalid we cannot map the unmapped
		 * region.
		 */
		if (IS_ERR(ctx->mrec))
			err = PTR_ERR(ctx->mrec);
		else {
			/*
			 * The @vcn is in an unmapped region, map the runlist
			 * and retry.
			 */
			err = ntfs_map_runlist_nolock(ni, vcn, ctx);
			if (likely(!err)) {
				is_retry = true;
				goto retry_remap;
			}
		}
		if (err == -EINVAL)
			err = -EIO;
	} else if (!err)
		err = -EIO;
	if (err != -ENOENT)
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
 * If @ctx->is_first is 'true', the search begins with @ctx->attr itself.  If it
 * is 'false', the search begins after @ctx->attr.
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
	 * attribute following that, if @ctx->is_first is 'true'.
	 */
	if (ctx->is_first) {
		a = ctx->attr;
		ctx->is_first = false;
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
	if (!rl) {
		ntfs_error(sb, "Cannot read attribute list since runlist is "
				"missing.");
		goto err_out;	
	}
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
	 * or the entry following that, if @ctx->is_first is 'true'.
	 */
	if (ctx->is_first) {
		al_entry = ctx->al_entry;
		ctx->is_first = false;
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
			break;
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
	ctx->is_first = true;
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
 * When -errno != -ENOENT, an error occurred during the lookup.  @ctx->attr is
 * then undefined and in particular you should not rely on it not changing.
 */
int ntfs_attr_lookup(const ATTR_TYPE type, const ntfschar *name,
		const u32 name_len, const IGNORE_CASE_BOOL ic,
		const VCN lowest_vcn, const u8 *val, const u32 val_len,
		ntfs_attr_search_ctx *ctx)
{
	ntfs_inode *base_ni;

	ntfs_debug("Entering.");
	BUG_ON(IS_ERR(ctx->mrec));
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
	*ctx = (ntfs_attr_search_ctx) {
		.mrec = mrec,
		/* Sanity checks are performed elsewhere. */
		.attr = (ATTR_RECORD*)((u8*)mrec +
				le16_to_cpu(mrec->attrs_offset)),
		.is_first = true,
		.ntfs_ino = ni,
	};
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
		ctx->is_first = true;
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

	ctx = kmem_cache_alloc(ntfs_attr_ctx_cache, GFP_NOFS);
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

#ifdef NTFS_RW

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
 * Return 0 if the attribute is allowed to be non-resident, -EPERM if not, and
 * -ENOENT if the attribute is not listed in $AttrDef.
 */
int ntfs_attr_can_be_non_resident(const ntfs_volume *vol, const ATTR_TYPE type)
{
	ATTR_DEF *ad;

	/* Find the attribute definition record in $AttrDef. */
	ad = ntfs_attr_find_in_attrdef(vol, type);
	if (unlikely(!ad))
		return -ENOENT;
	/* Check the flags and return the result. */
	if (ad->flags & ATTR_DEF_RESIDENT)
		return -EPERM;
	return 0;
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
	if (type == AT_INDEX_ALLOCATION)
		return -EPERM;
	return 0;
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
 * ntfs_resident_attr_value_resize - resize the value of a resident attribute
 * @m:		mft record containing attribute record
 * @a:		attribute record whose value to resize
 * @new_size:	new size in bytes to which to resize the attribute value of @a
 *
 * Resize the value of the attribute @a in the mft record @m to @new_size bytes.
 * If the value is made bigger, the newly allocated space is cleared.
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
int ntfs_resident_attr_value_resize(MFT_RECORD *m, ATTR_RECORD *a,
		const u32 new_size)
{
	u32 old_size;

	/* Resize the resident part of the attribute record. */
	if (ntfs_attr_record_resize(m, a,
			le16_to_cpu(a->data.resident.value_offset) + new_size))
		return -ENOSPC;
	/*
	 * The resize succeeded!  If we made the attribute value bigger, clear
	 * the area between the old size and @new_size.
	 */
	old_size = le32_to_cpu(a->data.resident.value_length);
	if (new_size > old_size)
		memset((u8*)a + le16_to_cpu(a->data.resident.value_offset) +
				old_size, 0, new_size - old_size);
	/* Finally update the length of the attribute value. */
	a->data.resident.value_length = cpu_to_le32(new_size);
	return 0;
}

/**
 * ntfs_attr_make_non_resident - convert a resident to a non-resident attribute
 * @ni:		ntfs inode describing the attribute to convert
 * @data_size:	size of the resident data to copy to the non-resident attribute
 *
 * Convert the resident ntfs attribute described by the ntfs inode @ni to a
 * non-resident one.
 *
 * @data_size must be equal to the attribute value size.  This is needed since
 * we need to know the size before we can map the mft record and our callers
 * always know it.  The reason we cannot simply read the size from the vfs
 * inode i_size is that this is not necessarily uptodate.  This happens when
 * ntfs_attr_make_non_resident() is called in the ->truncate call path(s).
 *
 * Return 0 on success and -errno on error.  The following error return codes
 * are defined:
 *	-EPERM	- The attribute is not allowed to be non-resident.
 *	-ENOMEM	- Not enough memory.
 *	-ENOSPC	- Not enough disk space.
 *	-EINVAL	- Attribute not defined on the volume.
 *	-EIO	- I/o error or other error.
 * Note that -ENOSPC is also returned in the case that there is not enough
 * space in the mft record to do the conversion.  This can happen when the mft
 * record is already very full.  The caller is responsible for trying to make
 * space in the mft record and trying again.  FIXME: Do we need a separate
 * error return code for this kind of -ENOSPC or is it always worth trying
 * again in case the attribute may then fit in a resident state so no need to
 * make it non-resident at all?  Ho-hum...  (AIA)
 *
 * NOTE to self: No changes in the attribute list are required to move from
 *		 a resident to a non-resident attribute.
 *
 * Locking: - The caller must hold i_mutex on the inode.
 */
int ntfs_attr_make_non_resident(ntfs_inode *ni, const u32 data_size)
{
	s64 new_size;
	struct inode *vi = VFS_I(ni);
	ntfs_volume *vol = ni->vol;
	ntfs_inode *base_ni;
	MFT_RECORD *m;
	ATTR_RECORD *a;
	ntfs_attr_search_ctx *ctx;
	struct page *page;
	runlist_element *rl;
	u8 *kaddr;
	unsigned long flags;
	int mp_size, mp_ofs, name_ofs, arec_size, err, err2;
	u32 attr_size;
	u8 old_res_attr_flags;

	/* Check that the attribute is allowed to be non-resident. */
	err = ntfs_attr_can_be_non_resident(vol, ni->type);
	if (unlikely(err)) {
		if (err == -EPERM)
			ntfs_debug("Attribute is not allowed to be "
					"non-resident.");
		else
			ntfs_debug("Attribute not defined on the NTFS "
					"volume!");
		return err;
	}
	/*
	 * FIXME: Compressed and encrypted attributes are not supported when
	 * writing and we should never have gotten here for them.
	 */
	BUG_ON(NInoCompressed(ni));
	BUG_ON(NInoEncrypted(ni));
	/*
	 * The size needs to be aligned to a cluster boundary for allocation
	 * purposes.
	 */
	new_size = (data_size + vol->cluster_size - 1) &
			~(vol->cluster_size - 1);
	if (new_size > 0) {
		/*
		 * Will need the page later and since the page lock nests
		 * outside all ntfs locks, we need to get the page now.
		 */
		page = find_or_create_page(vi->i_mapping, 0,
				mapping_gfp_mask(vi->i_mapping));
		if (unlikely(!page))
			return -ENOMEM;
		/* Start by allocating clusters to hold the attribute value. */
		rl = ntfs_cluster_alloc(vol, 0, new_size >>
				vol->cluster_size_bits, -1, DATA_ZONE, true);
		if (IS_ERR(rl)) {
			err = PTR_ERR(rl);
			ntfs_debug("Failed to allocate cluster%s, error code "
					"%i.", (new_size >>
					vol->cluster_size_bits) > 1 ? "s" : "",
					err);
			goto page_err_out;
		}
	} else {
		rl = NULL;
		page = NULL;
	}
	/* Determine the size of the mapping pairs array. */
	mp_size = ntfs_get_size_for_mapping_pairs(vol, rl, 0, -1);
	if (unlikely(mp_size < 0)) {
		err = mp_size;
		ntfs_debug("Failed to get size for mapping pairs array, error "
				"code %i.", err);
		goto rl_err_out;
	}
	down_write(&ni->runlist.lock);
	if (!NInoAttr(ni))
		base_ni = ni;
	else
		base_ni = ni->ext.base_ntfs_ino;
	m = map_mft_record(base_ni);
	if (IS_ERR(m)) {
		err = PTR_ERR(m);
		m = NULL;
		ctx = NULL;
		goto err_out;
	}
	ctx = ntfs_attr_get_search_ctx(base_ni, m);
	if (unlikely(!ctx)) {
		err = -ENOMEM;
		goto err_out;
	}
	err = ntfs_attr_lookup(ni->type, ni->name, ni->name_len,
			CASE_SENSITIVE, 0, NULL, 0, ctx);
	if (unlikely(err)) {
		if (err == -ENOENT)
			err = -EIO;
		goto err_out;
	}
	m = ctx->mrec;
	a = ctx->attr;
	BUG_ON(NInoNonResident(ni));
	BUG_ON(a->non_resident);
	/*
	 * Calculate new offsets for the name and the mapping pairs array.
	 */
	if (NInoSparse(ni) || NInoCompressed(ni))
		name_ofs = (offsetof(ATTR_REC,
				data.non_resident.compressed_size) +
				sizeof(a->data.non_resident.compressed_size) +
				7) & ~7;
	else
		name_ofs = (offsetof(ATTR_REC,
				data.non_resident.compressed_size) + 7) & ~7;
	mp_ofs = (name_ofs + a->name_length * sizeof(ntfschar) + 7) & ~7;
	/*
	 * Determine the size of the resident part of the now non-resident
	 * attribute record.
	 */
	arec_size = (mp_ofs + mp_size + 7) & ~7;
	/*
	 * If the page is not uptodate bring it uptodate by copying from the
	 * attribute value.
	 */
	attr_size = le32_to_cpu(a->data.resident.value_length);
	BUG_ON(attr_size != data_size);
	if (page && !PageUptodate(page)) {
		kaddr = kmap_atomic(page);
		memcpy(kaddr, (u8*)a +
				le16_to_cpu(a->data.resident.value_offset),
				attr_size);
		memset(kaddr + attr_size, 0, PAGE_SIZE - attr_size);
		kunmap_atomic(kaddr);
		flush_dcache_page(page);
		SetPageUptodate(page);
	}
	/* Backup the attribute flag. */
	old_res_attr_flags = a->data.resident.flags;
	/* Resize the resident part of the attribute record. */
	err = ntfs_attr_record_resize(m, a, arec_size);
	if (unlikely(err))
		goto err_out;
	/*
	 * Convert the resident part of the attribute record to describe a
	 * non-resident attribute.
	 */
	a->non_resident = 1;
	/* Move the attribute name if it exists and update the offset. */
	if (a->name_length)
		memmove((u8*)a + name_ofs, (u8*)a + le16_to_cpu(a->name_offset),
				a->name_length * sizeof(ntfschar));
	a->name_offset = cpu_to_le16(name_ofs);
	/* Setup the fields specific to non-resident attributes. */
	a->data.non_resident.lowest_vcn = 0;
	a->data.non_resident.highest_vcn = cpu_to_sle64((new_size - 1) >>
			vol->cluster_size_bits);
	a->data.non_resident.mapping_pairs_offset = cpu_to_le16(mp_ofs);
	memset(&a->data.non_resident.reserved, 0,
			sizeof(a->data.non_resident.reserved));
	a->data.non_resident.allocated_size = cpu_to_sle64(new_size);
	a->data.non_resident.data_size =
			a->data.non_resident.initialized_size =
			cpu_to_sle64(attr_size);
	if (NInoSparse(ni) || NInoCompressed(ni)) {
		a->data.non_resident.compression_unit = 0;
		if (NInoCompressed(ni) || vol->major_ver < 3)
			a->data.non_resident.compression_unit = 4;
		a->data.non_resident.compressed_size =
				a->data.non_resident.allocated_size;
	} else
		a->data.non_resident.compression_unit = 0;
	/* Generate the mapping pairs array into the attribute record. */
	err = ntfs_mapping_pairs_build(vol, (u8*)a + mp_ofs,
			arec_size - mp_ofs, rl, 0, -1, NULL);
	if (unlikely(err)) {
		ntfs_debug("Failed to build mapping pairs, error code %i.",
				err);
		goto undo_err_out;
	}
	/* Setup the in-memory attribute structure to be non-resident. */
	ni->runlist.rl = rl;
	write_lock_irqsave(&ni->size_lock, flags);
	ni->allocated_size = new_size;
	if (NInoSparse(ni) || NInoCompressed(ni)) {
		ni->itype.compressed.size = ni->allocated_size;
		if (a->data.non_resident.compression_unit) {
			ni->itype.compressed.block_size = 1U << (a->data.
					non_resident.compression_unit +
					vol->cluster_size_bits);
			ni->itype.compressed.block_size_bits =
					ffs(ni->itype.compressed.block_size) -
					1;
			ni->itype.compressed.block_clusters = 1U <<
					a->data.non_resident.compression_unit;
		} else {
			ni->itype.compressed.block_size = 0;
			ni->itype.compressed.block_size_bits = 0;
			ni->itype.compressed.block_clusters = 0;
		}
		vi->i_blocks = ni->itype.compressed.size >> 9;
	} else
		vi->i_blocks = ni->allocated_size >> 9;
	write_unlock_irqrestore(&ni->size_lock, flags);
	/*
	 * This needs to be last since the address space operations ->read_folio
	 * and ->writepage can run concurrently with us as they are not
	 * serialized on i_mutex.  Note, we are not allowed to fail once we flip
	 * this switch, which is another reason to do this last.
	 */
	NInoSetNonResident(ni);
	/* Mark the mft record dirty, so it gets written back. */
	flush_dcache_mft_record_page(ctx->ntfs_ino);
	mark_mft_record_dirty(ctx->ntfs_ino);
	ntfs_attr_put_search_ctx(ctx);
	unmap_mft_record(base_ni);
	up_write(&ni->runlist.lock);
	if (page) {
		set_page_dirty(page);
		unlock_page(page);
		put_page(page);
	}
	ntfs_debug("Done.");
	return 0;
undo_err_out:
	/* Convert the attribute back into a resident attribute. */
	a->non_resident = 0;
	/* Move the attribute name if it exists and update the offset. */
	name_ofs = (offsetof(ATTR_RECORD, data.resident.reserved) +
			sizeof(a->data.resident.reserved) + 7) & ~7;
	if (a->name_length)
		memmove((u8*)a + name_ofs, (u8*)a + le16_to_cpu(a->name_offset),
				a->name_length * sizeof(ntfschar));
	mp_ofs = (name_ofs + a->name_length * sizeof(ntfschar) + 7) & ~7;
	a->name_offset = cpu_to_le16(name_ofs);
	arec_size = (mp_ofs + attr_size + 7) & ~7;
	/* Resize the resident part of the attribute record. */
	err2 = ntfs_attr_record_resize(m, a, arec_size);
	if (unlikely(err2)) {
		/*
		 * This cannot happen (well if memory corruption is at work it
		 * could happen in theory), but deal with it as well as we can.
		 * If the old size is too small, truncate the attribute,
		 * otherwise simply give it a larger allocated size.
		 * FIXME: Should check whether chkdsk complains when the
		 * allocated size is much bigger than the resident value size.
		 */
		arec_size = le32_to_cpu(a->length);
		if ((mp_ofs + attr_size) > arec_size) {
			err2 = attr_size;
			attr_size = arec_size - mp_ofs;
			ntfs_error(vol->sb, "Failed to undo partial resident "
					"to non-resident attribute "
					"conversion.  Truncating inode 0x%lx, "
					"attribute type 0x%x from %i bytes to "
					"%i bytes to maintain metadata "
					"consistency.  THIS MEANS YOU ARE "
					"LOSING %i BYTES DATA FROM THIS %s.",
					vi->i_ino,
					(unsigned)le32_to_cpu(ni->type),
					err2, attr_size, err2 - attr_size,
					((ni->type == AT_DATA) &&
					!ni->name_len) ? "FILE": "ATTRIBUTE");
			write_lock_irqsave(&ni->size_lock, flags);
			ni->initialized_size = attr_size;
			i_size_write(vi, attr_size);
			write_unlock_irqrestore(&ni->size_lock, flags);
		}
	}
	/* Setup the fields specific to resident attributes. */
	a->data.resident.value_length = cpu_to_le32(attr_size);
	a->data.resident.value_offset = cpu_to_le16(mp_ofs);
	a->data.resident.flags = old_res_attr_flags;
	memset(&a->data.resident.reserved, 0,
			sizeof(a->data.resident.reserved));
	/* Copy the data from the page back to the attribute value. */
	if (page) {
		kaddr = kmap_atomic(page);
		memcpy((u8*)a + mp_ofs, kaddr, attr_size);
		kunmap_atomic(kaddr);
	}
	/* Setup the allocated size in the ntfs inode in case it changed. */
	write_lock_irqsave(&ni->size_lock, flags);
	ni->allocated_size = arec_size - mp_ofs;
	write_unlock_irqrestore(&ni->size_lock, flags);
	/* Mark the mft record dirty, so it gets written back. */
	flush_dcache_mft_record_page(ctx->ntfs_ino);
	mark_mft_record_dirty(ctx->ntfs_ino);
err_out:
	if (ctx)
		ntfs_attr_put_search_ctx(ctx);
	if (m)
		unmap_mft_record(base_ni);
	ni->runlist.rl = NULL;
	up_write(&ni->runlist.lock);
rl_err_out:
	if (rl) {
		if (ntfs_cluster_free_from_rl(vol, rl) < 0) {
			ntfs_error(vol->sb, "Failed to release allocated "
					"cluster(s) in error code path.  Run "
					"chkdsk to recover the lost "
					"cluster(s).");
			NVolSetErrors(vol);
		}
		ntfs_free(rl);
page_err_out:
		unlock_page(page);
		put_page(page);
	}
	if (err == -EINVAL)
		err = -EIO;
	return err;
}

/**
 * ntfs_attr_extend_allocation - extend the allocated space of an attribute
 * @ni:			ntfs inode of the attribute whose allocation to extend
 * @new_alloc_size:	new size in bytes to which to extend the allocation to
 * @new_data_size:	new size in bytes to which to extend the data to
 * @data_start:		beginning of region which is required to be non-sparse
 *
 * Extend the allocated space of an attribute described by the ntfs inode @ni
 * to @new_alloc_size bytes.  If @data_start is -1, the whole extension may be
 * implemented as a hole in the file (as long as both the volume and the ntfs
 * inode @ni have sparse support enabled).  If @data_start is >= 0, then the
 * region between the old allocated size and @data_start - 1 may be made sparse
 * but the regions between @data_start and @new_alloc_size must be backed by
 * actual clusters.
 *
 * If @new_data_size is -1, it is ignored.  If it is >= 0, then the data size
 * of the attribute is extended to @new_data_size.  Note that the i_size of the
 * vfs inode is not updated.  Only the data size in the base attribute record
 * is updated.  The caller has to update i_size separately if this is required.
 * WARNING: It is a BUG() for @new_data_size to be smaller than the old data
 * size as well as for @new_data_size to be greater than @new_alloc_size.
 *
 * For resident attributes this involves resizing the attribute record and if
 * necessary moving it and/or other attributes into extent mft records and/or
 * converting the attribute to a non-resident attribute which in turn involves
 * extending the allocation of a non-resident attribute as described below.
 *
 * For non-resident attributes this involves allocating clusters in the data
 * zone on the volume (except for regions that are being made sparse) and
 * extending the run list to describe the allocated clusters as well as
 * updating the mapping pairs array of the attribute.  This in turn involves
 * resizing the attribute record and if necessary moving it and/or other
 * attributes into extent mft records and/or splitting the attribute record
 * into multiple extent attribute records.
 *
 * Also, the attribute list attribute is updated if present and in some of the
 * above cases (the ones where extent mft records/attributes come into play),
 * an attribute list attribute is created if not already present.
 *
 * Return the new allocated size on success and -errno on error.  In the case
 * that an error is encountered but a partial extension at least up to
 * @data_start (if present) is possible, the allocation is partially extended
 * and this is returned.  This means the caller must check the returned size to
 * determine if the extension was partial.  If @data_start is -1 then partial
 * allocations are not performed.
 *
 * WARNING: Do not call ntfs_attr_extend_allocation() for $MFT/$DATA.
 *
 * Locking: This function takes the runlist lock of @ni for writing as well as
 * locking the mft record of the base ntfs inode.  These locks are maintained
 * throughout execution of the function.  These locks are required so that the
 * attribute can be resized safely and so that it can for example be converted
 * from resident to non-resident safely.
 *
 * TODO: At present attribute list attribute handling is not implemented.
 *
 * TODO: At present it is not safe to call this function for anything other
 * than the $DATA attribute(s) of an uncompressed and unencrypted file.
 */
s64 ntfs_attr_extend_allocation(ntfs_inode *ni, s64 new_alloc_size,
		const s64 new_data_size, const s64 data_start)
{
	VCN vcn;
	s64 ll, allocated_size, start = data_start;
	struct inode *vi = VFS_I(ni);
	ntfs_volume *vol = ni->vol;
	ntfs_inode *base_ni;
	MFT_RECORD *m;
	ATTR_RECORD *a;
	ntfs_attr_search_ctx *ctx;
	runlist_element *rl, *rl2;
	unsigned long flags;
	int err, mp_size;
	u32 attr_len = 0; /* Silence stupid gcc warning. */
	bool mp_rebuilt;

#ifdef DEBUG
	read_lock_irqsave(&ni->size_lock, flags);
	allocated_size = ni->allocated_size;
	read_unlock_irqrestore(&ni->size_lock, flags);
	ntfs_debug("Entering for i_ino 0x%lx, attribute type 0x%x, "
			"old_allocated_size 0x%llx, "
			"new_allocated_size 0x%llx, new_data_size 0x%llx, "
			"data_start 0x%llx.", vi->i_ino,
			(unsigned)le32_to_cpu(ni->type),
			(unsigned long long)allocated_size,
			(unsigned long long)new_alloc_size,
			(unsigned long long)new_data_size,
			(unsigned long long)start);
#endif
retry_extend:
	/*
	 * For non-resident attributes, @start and @new_size need to be aligned
	 * to cluster boundaries for allocation purposes.
	 */
	if (NInoNonResident(ni)) {
		if (start > 0)
			start &= ~(s64)vol->cluster_size_mask;
		new_alloc_size = (new_alloc_size + vol->cluster_size - 1) &
				~(s64)vol->cluster_size_mask;
	}
	BUG_ON(new_data_size >= 0 && new_data_size > new_alloc_size);
	/* Check if new size is allowed in $AttrDef. */
	err = ntfs_attr_size_bounds_check(vol, ni->type, new_alloc_size);
	if (unlikely(err)) {
		/* Only emit errors when the write will fail completely. */
		read_lock_irqsave(&ni->size_lock, flags);
		allocated_size = ni->allocated_size;
		read_unlock_irqrestore(&ni->size_lock, flags);
		if (start < 0 || start >= allocated_size) {
			if (err == -ERANGE) {
				ntfs_error(vol->sb, "Cannot extend allocation "
						"of inode 0x%lx, attribute "
						"type 0x%x, because the new "
						"allocation would exceed the "
						"maximum allowed size for "
						"this attribute type.",
						vi->i_ino, (unsigned)
						le32_to_cpu(ni->type));
			} else {
				ntfs_error(vol->sb, "Cannot extend allocation "
						"of inode 0x%lx, attribute "
						"type 0x%x, because this "
						"attribute type is not "
						"defined on the NTFS volume.  "
						"Possible corruption!  You "
						"should run chkdsk!",
						vi->i_ino, (unsigned)
						le32_to_cpu(ni->type));
			}
		}
		/* Translate error code to be POSIX conformant for write(2). */
		if (err == -ERANGE)
			err = -EFBIG;
		else
			err = -EIO;
		return err;
	}
	if (!NInoAttr(ni))
		base_ni = ni;
	else
		base_ni = ni->ext.base_ntfs_ino;
	/*
	 * We will be modifying both the runlist (if non-resident) and the mft
	 * record so lock them both down.
	 */
	down_write(&ni->runlist.lock);
	m = map_mft_record(base_ni);
	if (IS_ERR(m)) {
		err = PTR_ERR(m);
		m = NULL;
		ctx = NULL;
		goto err_out;
	}
	ctx = ntfs_attr_get_search_ctx(base_ni, m);
	if (unlikely(!ctx)) {
		err = -ENOMEM;
		goto err_out;
	}
	read_lock_irqsave(&ni->size_lock, flags);
	allocated_size = ni->allocated_size;
	read_unlock_irqrestore(&ni->size_lock, flags);
	/*
	 * If non-resident, seek to the last extent.  If resident, there is
	 * only one extent, so seek to that.
	 */
	vcn = NInoNonResident(ni) ? allocated_size >> vol->cluster_size_bits :
			0;
	/*
	 * Abort if someone did the work whilst we waited for the locks.  If we
	 * just converted the attribute from resident to non-resident it is
	 * likely that exactly this has happened already.  We cannot quite
	 * abort if we need to update the data size.
	 */
	if (unlikely(new_alloc_size <= allocated_size)) {
		ntfs_debug("Allocated size already exceeds requested size.");
		new_alloc_size = allocated_size;
		if (new_data_size < 0)
			goto done;
		/*
		 * We want the first attribute extent so that we can update the
		 * data size.
		 */
		vcn = 0;
	}
	err = ntfs_attr_lookup(ni->type, ni->name, ni->name_len,
			CASE_SENSITIVE, vcn, NULL, 0, ctx);
	if (unlikely(err)) {
		if (err == -ENOENT)
			err = -EIO;
		goto err_out;
	}
	m = ctx->mrec;
	a = ctx->attr;
	/* Use goto to reduce indentation. */
	if (a->non_resident)
		goto do_non_resident_extend;
	BUG_ON(NInoNonResident(ni));
	/* The total length of the attribute value. */
	attr_len = le32_to_cpu(a->data.resident.value_length);
	/*
	 * Extend the attribute record to be able to store the new attribute
	 * size.  ntfs_attr_record_resize() will not do anything if the size is
	 * not changing.
	 */
	if (new_alloc_size < vol->mft_record_size &&
			!ntfs_attr_record_resize(m, a,
			le16_to_cpu(a->data.resident.value_offset) +
			new_alloc_size)) {
		/* The resize succeeded! */
		write_lock_irqsave(&ni->size_lock, flags);
		ni->allocated_size = le32_to_cpu(a->length) -
				le16_to_cpu(a->data.resident.value_offset);
		write_unlock_irqrestore(&ni->size_lock, flags);
		if (new_data_size >= 0) {
			BUG_ON(new_data_size < attr_len);
			a->data.resident.value_length =
					cpu_to_le32((u32)new_data_size);
		}
		goto flush_done;
	}
	/*
	 * We have to drop all the locks so we can call
	 * ntfs_attr_make_non_resident().  This could be optimised by try-
	 * locking the first page cache page and only if that fails dropping
	 * the locks, locking the page, and redoing all the locking and
	 * lookups.  While this would be a huge optimisation, it is not worth
	 * it as this is definitely a slow code path.
	 */
	ntfs_attr_put_search_ctx(ctx);
	unmap_mft_record(base_ni);
	up_write(&ni->runlist.lock);
	/*
	 * Not enough space in the mft record, try to make the attribute
	 * non-resident and if successful restart the extension process.
	 */
	err = ntfs_attr_make_non_resident(ni, attr_len);
	if (likely(!err))
		goto retry_extend;
	/*
	 * Could not make non-resident.  If this is due to this not being
	 * permitted for this attribute type or there not being enough space,
	 * try to make other attributes non-resident.  Otherwise fail.
	 */
	if (unlikely(err != -EPERM && err != -ENOSPC)) {
		/* Only emit errors when the write will fail completely. */
		read_lock_irqsave(&ni->size_lock, flags);
		allocated_size = ni->allocated_size;
		read_unlock_irqrestore(&ni->size_lock, flags);
		if (start < 0 || start >= allocated_size)
			ntfs_error(vol->sb, "Cannot extend allocation of "
					"inode 0x%lx, attribute type 0x%x, "
					"because the conversion from resident "
					"to non-resident attribute failed "
					"with error code %i.", vi->i_ino,
					(unsigned)le32_to_cpu(ni->type), err);
		if (err != -ENOMEM)
			err = -EIO;
		goto conv_err_out;
	}
	/* TODO: Not implemented from here, abort. */
	read_lock_irqsave(&ni->size_lock, flags);
	allocated_size = ni->allocated_size;
	read_unlock_irqrestore(&ni->size_lock, flags);
	if (start < 0 || start >= allocated_size) {
		if (err == -ENOSPC)
			ntfs_error(vol->sb, "Not enough space in the mft "
					"record/on disk for the non-resident "
					"attribute value.  This case is not "
					"implemented yet.");
		else /* if (err == -EPERM) */
			ntfs_error(vol->sb, "This attribute type may not be "
					"non-resident.  This case is not "
					"implemented yet.");
	}
	err = -EOPNOTSUPP;
	goto conv_err_out;
#if 0
	// TODO: Attempt to make other attributes non-resident.
	if (!err)
		goto do_resident_extend;
	/*
	 * Both the attribute list attribute and the standard information
	 * attribute must remain in the base inode.  Thus, if this is one of
	 * these attributes, we have to try to move other attributes out into
	 * extent mft records instead.
	 */
	if (ni->type == AT_ATTRIBUTE_LIST ||
			ni->type == AT_STANDARD_INFORMATION) {
		// TODO: Attempt to move other attributes into extent mft
		// records.
		err = -EOPNOTSUPP;
		if (!err)
			goto do_resident_extend;
		goto err_out;
	}
	// TODO: Attempt to move this attribute to an extent mft record, but
	// only if it is not already the only attribute in an mft record in
	// which case there would be nothing to gain.
	err = -EOPNOTSUPP;
	if (!err)
		goto do_resident_extend;
	/* There is nothing we can do to make enough space. )-: */
	goto err_out;
#endif
do_non_resident_extend:
	BUG_ON(!NInoNonResident(ni));
	if (new_alloc_size == allocated_size) {
		BUG_ON(vcn);
		goto alloc_done;
	}
	/*
	 * If the data starts after the end of the old allocation, this is a
	 * $DATA attribute and sparse attributes are enabled on the volume and
	 * for this inode, then create a sparse region between the old
	 * allocated size and the start of the data.  Otherwise simply proceed
	 * with filling the whole space between the old allocated size and the
	 * new allocated size with clusters.
	 */
	if ((start >= 0 && start <= allocated_size) || ni->type != AT_DATA ||
			!NVolSparseEnabled(vol) || NInoSparseDisabled(ni))
		goto skip_sparse;
	// TODO: This is not implemented yet.  We just fill in with real
	// clusters for now...
	ntfs_debug("Inserting holes is not-implemented yet.  Falling back to "
			"allocating real clusters instead.");
skip_sparse:
	rl = ni->runlist.rl;
	if (likely(rl)) {
		/* Seek to the end of the runlist. */
		while (rl->length)
			rl++;
	}
	/* If this attribute extent is not mapped, map it now. */
	if (unlikely(!rl || rl->lcn == LCN_RL_NOT_MAPPED ||
			(rl->lcn == LCN_ENOENT && rl > ni->runlist.rl &&
			(rl-1)->lcn == LCN_RL_NOT_MAPPED))) {
		if (!rl && !allocated_size)
			goto first_alloc;
		rl = ntfs_mapping_pairs_decompress(vol, a, ni->runlist.rl);
		if (IS_ERR(rl)) {
			err = PTR_ERR(rl);
			if (start < 0 || start >= allocated_size)
				ntfs_error(vol->sb, "Cannot extend allocation "
						"of inode 0x%lx, attribute "
						"type 0x%x, because the "
						"mapping of a runlist "
						"fragment failed with error "
						"code %i.", vi->i_ino,
						(unsigned)le32_to_cpu(ni->type),
						err);
			if (err != -ENOMEM)
				err = -EIO;
			goto err_out;
		}
		ni->runlist.rl = rl;
		/* Seek to the end of the runlist. */
		while (rl->length)
			rl++;
	}
	/*
	 * We now know the runlist of the last extent is mapped and @rl is at
	 * the end of the runlist.  We want to begin allocating clusters
	 * starting at the last allocated cluster to reduce fragmentation.  If
	 * there are no valid LCNs in the attribute we let the cluster
	 * allocator choose the starting cluster.
	 */
	/* If the last LCN is a hole or simillar seek back to last real LCN. */
	while (rl->lcn < 0 && rl > ni->runlist.rl)
		rl--;
first_alloc:
	// FIXME: Need to implement partial allocations so at least part of the
	// write can be performed when start >= 0.  (Needed for POSIX write(2)
	// conformance.)
	rl2 = ntfs_cluster_alloc(vol, allocated_size >> vol->cluster_size_bits,
			(new_alloc_size - allocated_size) >>
			vol->cluster_size_bits, (rl && (rl->lcn >= 0)) ?
			rl->lcn + rl->length : -1, DATA_ZONE, true);
	if (IS_ERR(rl2)) {
		err = PTR_ERR(rl2);
		if (start < 0 || start >= allocated_size)
			ntfs_error(vol->sb, "Cannot extend allocation of "
					"inode 0x%lx, attribute type 0x%x, "
					"because the allocation of clusters "
					"failed with error code %i.", vi->i_ino,
					(unsigned)le32_to_cpu(ni->type), err);
		if (err != -ENOMEM && err != -ENOSPC)
			err = -EIO;
		goto err_out;
	}
	rl = ntfs_runlists_merge(ni->runlist.rl, rl2);
	if (IS_ERR(rl)) {
		err = PTR_ERR(rl);
		if (start < 0 || start >= allocated_size)
			ntfs_error(vol->sb, "Cannot extend allocation of "
					"inode 0x%lx, attribute type 0x%x, "
					"because the runlist merge failed "
					"with error code %i.", vi->i_ino,
					(unsigned)le32_to_cpu(ni->type), err);
		if (err != -ENOMEM)
			err = -EIO;
		if (ntfs_cluster_free_from_rl(vol, rl2)) {
			ntfs_error(vol->sb, "Failed to release allocated "
					"cluster(s) in error code path.  Run "
					"chkdsk to recover the lost "
					"cluster(s).");
			NVolSetErrors(vol);
		}
		ntfs_free(rl2);
		goto err_out;
	}
	ni->runlist.rl = rl;
	ntfs_debug("Allocated 0x%llx clusters.", (long long)(new_alloc_size -
			allocated_size) >> vol->cluster_size_bits);
	/* Find the runlist element with which the attribute extent starts. */
	ll = sle64_to_cpu(a->data.non_resident.lowest_vcn);
	rl2 = ntfs_rl_find_vcn_nolock(rl, ll);
	BUG_ON(!rl2);
	BUG_ON(!rl2->length);
	BUG_ON(rl2->lcn < LCN_HOLE);
	mp_rebuilt = false;
	/* Get the size for the new mapping pairs array for this extent. */
	mp_size = ntfs_get_size_for_mapping_pairs(vol, rl2, ll, -1);
	if (unlikely(mp_size <= 0)) {
		err = mp_size;
		if (start < 0 || start >= allocated_size)
			ntfs_error(vol->sb, "Cannot extend allocation of "
					"inode 0x%lx, attribute type 0x%x, "
					"because determining the size for the "
					"mapping pairs failed with error code "
					"%i.", vi->i_ino,
					(unsigned)le32_to_cpu(ni->type), err);
		err = -EIO;
		goto undo_alloc;
	}
	/* Extend the attribute record to fit the bigger mapping pairs array. */
	attr_len = le32_to_cpu(a->length);
	err = ntfs_attr_record_resize(m, a, mp_size +
			le16_to_cpu(a->data.non_resident.mapping_pairs_offset));
	if (unlikely(err)) {
		BUG_ON(err != -ENOSPC);
		// TODO: Deal with this by moving this extent to a new mft
		// record or by starting a new extent in a new mft record,
		// possibly by extending this extent partially and filling it
		// and creating a new extent for the remainder, or by making
		// other attributes non-resident and/or by moving other
		// attributes out of this mft record.
		if (start < 0 || start >= allocated_size)
			ntfs_error(vol->sb, "Not enough space in the mft "
					"record for the extended attribute "
					"record.  This case is not "
					"implemented yet.");
		err = -EOPNOTSUPP;
		goto undo_alloc;
	}
	mp_rebuilt = true;
	/* Generate the mapping pairs array directly into the attr record. */
	err = ntfs_mapping_pairs_build(vol, (u8*)a +
			le16_to_cpu(a->data.non_resident.mapping_pairs_offset),
			mp_size, rl2, ll, -1, NULL);
	if (unlikely(err)) {
		if (start < 0 || start >= allocated_size)
			ntfs_error(vol->sb, "Cannot extend allocation of "
					"inode 0x%lx, attribute type 0x%x, "
					"because building the mapping pairs "
					"failed with error code %i.", vi->i_ino,
					(unsigned)le32_to_cpu(ni->type), err);
		err = -EIO;
		goto undo_alloc;
	}
	/* Update the highest_vcn. */
	a->data.non_resident.highest_vcn = cpu_to_sle64((new_alloc_size >>
			vol->cluster_size_bits) - 1);
	/*
	 * We now have extended the allocated size of the attribute.  Reflect
	 * this in the ntfs_inode structure and the attribute record.
	 */
	if (a->data.non_resident.lowest_vcn) {
		/*
		 * We are not in the first attribute extent, switch to it, but
		 * first ensure the changes will make it to disk later.
		 */
		flush_dcache_mft_record_page(ctx->ntfs_ino);
		mark_mft_record_dirty(ctx->ntfs_ino);
		ntfs_attr_reinit_search_ctx(ctx);
		err = ntfs_attr_lookup(ni->type, ni->name, ni->name_len,
				CASE_SENSITIVE, 0, NULL, 0, ctx);
		if (unlikely(err))
			goto restore_undo_alloc;
		/* @m is not used any more so no need to set it. */
		a = ctx->attr;
	}
	write_lock_irqsave(&ni->size_lock, flags);
	ni->allocated_size = new_alloc_size;
	a->data.non_resident.allocated_size = cpu_to_sle64(new_alloc_size);
	/*
	 * FIXME: This would fail if @ni is a directory, $MFT, or an index,
	 * since those can have sparse/compressed set.  For example can be
	 * set compressed even though it is not compressed itself and in that
	 * case the bit means that files are to be created compressed in the
	 * directory...  At present this is ok as this code is only called for
	 * regular files, and only for their $DATA attribute(s).
	 * FIXME: The calculation is wrong if we created a hole above.  For now
	 * it does not matter as we never create holes.
	 */
	if (NInoSparse(ni) || NInoCompressed(ni)) {
		ni->itype.compressed.size += new_alloc_size - allocated_size;
		a->data.non_resident.compressed_size =
				cpu_to_sle64(ni->itype.compressed.size);
		vi->i_blocks = ni->itype.compressed.size >> 9;
	} else
		vi->i_blocks = new_alloc_size >> 9;
	write_unlock_irqrestore(&ni->size_lock, flags);
alloc_done:
	if (new_data_size >= 0) {
		BUG_ON(new_data_size <
				sle64_to_cpu(a->data.non_resident.data_size));
		a->data.non_resident.data_size = cpu_to_sle64(new_data_size);
	}
flush_done:
	/* Ensure the changes make it to disk. */
	flush_dcache_mft_record_page(ctx->ntfs_ino);
	mark_mft_record_dirty(ctx->ntfs_ino);
done:
	ntfs_attr_put_search_ctx(ctx);
	unmap_mft_record(base_ni);
	up_write(&ni->runlist.lock);
	ntfs_debug("Done, new_allocated_size 0x%llx.",
			(unsigned long long)new_alloc_size);
	return new_alloc_size;
restore_undo_alloc:
	if (start < 0 || start >= allocated_size)
		ntfs_error(vol->sb, "Cannot complete extension of allocation "
				"of inode 0x%lx, attribute type 0x%x, because "
				"lookup of first attribute extent failed with "
				"error code %i.", vi->i_ino,
				(unsigned)le32_to_cpu(ni->type), err);
	if (err == -ENOENT)
		err = -EIO;
	ntfs_attr_reinit_search_ctx(ctx);
	if (ntfs_attr_lookup(ni->type, ni->name, ni->name_len, CASE_SENSITIVE,
			allocated_size >> vol->cluster_size_bits, NULL, 0,
			ctx)) {
		ntfs_error(vol->sb, "Failed to find last attribute extent of "
				"attribute in error code path.  Run chkdsk to "
				"recover.");
		write_lock_irqsave(&ni->size_lock, flags);
		ni->allocated_size = new_alloc_size;
		/*
		 * FIXME: This would fail if @ni is a directory...  See above.
		 * FIXME: The calculation is wrong if we created a hole above.
		 * For now it does not matter as we never create holes.
		 */
		if (NInoSparse(ni) || NInoCompressed(ni)) {
			ni->itype.compressed.size += new_alloc_size -
					allocated_size;
			vi->i_blocks = ni->itype.compressed.size >> 9;
		} else
			vi->i_blocks = new_alloc_size >> 9;
		write_unlock_irqrestore(&ni->size_lock, flags);
		ntfs_attr_put_search_ctx(ctx);
		unmap_mft_record(base_ni);
		up_write(&ni->runlist.lock);
		/*
		 * The only thing that is now wrong is the allocated size of the
		 * base attribute extent which chkdsk should be able to fix.
		 */
		NVolSetErrors(vol);
		return err;
	}
	ctx->attr->data.non_resident.highest_vcn = cpu_to_sle64(
			(allocated_size >> vol->cluster_size_bits) - 1);
undo_alloc:
	ll = allocated_size >> vol->cluster_size_bits;
	if (ntfs_cluster_free(ni, ll, -1, ctx) < 0) {
		ntfs_error(vol->sb, "Failed to release allocated cluster(s) "
				"in error code path.  Run chkdsk to recover "
				"the lost cluster(s).");
		NVolSetErrors(vol);
	}
	m = ctx->mrec;
	a = ctx->attr;
	/*
	 * If the runlist truncation fails and/or the search context is no
	 * longer valid, we cannot resize the attribute record or build the
	 * mapping pairs array thus we mark the inode bad so that no access to
	 * the freed clusters can happen.
	 */
	if (ntfs_rl_truncate_nolock(vol, &ni->runlist, ll) || IS_ERR(m)) {
		ntfs_error(vol->sb, "Failed to %s in error code path.  Run "
				"chkdsk to recover.", IS_ERR(m) ?
				"restore attribute search context" :
				"truncate attribute runlist");
		NVolSetErrors(vol);
	} else if (mp_rebuilt) {
		if (ntfs_attr_record_resize(m, a, attr_len)) {
			ntfs_error(vol->sb, "Failed to restore attribute "
					"record in error code path.  Run "
					"chkdsk to recover.");
			NVolSetErrors(vol);
		} else /* if (success) */ {
			if (ntfs_mapping_pairs_build(vol, (u8*)a + le16_to_cpu(
					a->data.non_resident.
					mapping_pairs_offset), attr_len -
					le16_to_cpu(a->data.non_resident.
					mapping_pairs_offset), rl2, ll, -1,
					NULL)) {
				ntfs_error(vol->sb, "Failed to restore "
						"mapping pairs array in error "
						"code path.  Run chkdsk to "
						"recover.");
				NVolSetErrors(vol);
			}
			flush_dcache_mft_record_page(ctx->ntfs_ino);
			mark_mft_record_dirty(ctx->ntfs_ino);
		}
	}
err_out:
	if (ctx)
		ntfs_attr_put_search_ctx(ctx);
	if (m)
		unmap_mft_record(base_ni);
	up_write(&ni->runlist.lock);
conv_err_out:
	ntfs_debug("Failed.  Returning error code %i.", err);
	return err;
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
 * Note thie function actually only operates on the page cache pages belonging
 * to the ntfs attribute and it marks them dirty after doing the memset().
 * Thus it relies on the vm dirty page write code paths to cause the modified
 * pages to be written to the mft record/disk.
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
	unsigned start_ofs, end_ofs, size;

	ntfs_debug("Entering for ofs 0x%llx, cnt 0x%llx, val 0x%hx.",
			(long long)ofs, (long long)cnt, val);
	BUG_ON(ofs < 0);
	BUG_ON(cnt < 0);
	if (!cnt)
		goto done;
	/*
	 * FIXME: Compressed and encrypted attributes are not supported when
	 * writing and we should never have gotten here for them.
	 */
	BUG_ON(NInoCompressed(ni));
	BUG_ON(NInoEncrypted(ni));
	mapping = VFS_I(ni)->i_mapping;
	/* Work out the starting index and page offset. */
	idx = ofs >> PAGE_SHIFT;
	start_ofs = ofs & ~PAGE_MASK;
	/* Work out the ending index and page offset. */
	end = ofs + cnt;
	end_ofs = end & ~PAGE_MASK;
	/* If the end is outside the inode size return -ESPIPE. */
	if (unlikely(end > i_size_read(VFS_I(ni)))) {
		ntfs_error(vol->sb, "Request exceeds end of attribute.");
		return -ESPIPE;
	}
	end >>= PAGE_SHIFT;
	/* If there is a first partial page, need to do it the slow way. */
	if (start_ofs) {
		page = read_mapping_page(mapping, idx, NULL);
		if (IS_ERR(page)) {
			ntfs_error(vol->sb, "Failed to read first partial "
					"page (error, index 0x%lx).", idx);
			return PTR_ERR(page);
		}
		/*
		 * If the last page is the same as the first page, need to
		 * limit the write to the end offset.
		 */
		size = PAGE_SIZE;
		if (idx == end)
			size = end_ofs;
		kaddr = kmap_atomic(page);
		memset(kaddr + start_ofs, val, size - start_ofs);
		flush_dcache_page(page);
		kunmap_atomic(kaddr);
		set_page_dirty(page);
		put_page(page);
		balance_dirty_pages_ratelimited(mapping);
		cond_resched();
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
		kaddr = kmap_atomic(page);
		memset(kaddr, val, PAGE_SIZE);
		flush_dcache_page(page);
		kunmap_atomic(kaddr);
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
		put_page(page);
		balance_dirty_pages_ratelimited(mapping);
		cond_resched();
	}
	/* If there is a last partial page, need to do it the slow way. */
	if (end_ofs) {
		page = read_mapping_page(mapping, idx, NULL);
		if (IS_ERR(page)) {
			ntfs_error(vol->sb, "Failed to read last partial page "
					"(error, index 0x%lx).", idx);
			return PTR_ERR(page);
		}
		kaddr = kmap_atomic(page);
		memset(kaddr, val, end_ofs);
		flush_dcache_page(page);
		kunmap_atomic(kaddr);
		set_page_dirty(page);
		put_page(page);
		balance_dirty_pages_ratelimited(mapping);
		cond_resched();
	}
done:
	ntfs_debug("Done.");
	return 0;
}

#endif /* NTFS_RW */
