// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * NTFS runlist handling code.
 *
 * Copyright (c) 2001-2007 Anton Altaparmakov
 * Copyright (c) 2002-2005 Richard Russon
 * Copyright (c) 2025 LG Electronics Co., Ltd.
 *
 * Part of this file is based on code from the NTFS-3G.
 * and is copyrighted by the respective authors below:
 * Copyright (c) 2002-2005 Anton Altaparmakov
 * Copyright (c) 2002-2005 Richard Russon
 * Copyright (c) 2002-2008 Szabolcs Szakacsits
 * Copyright (c) 2004 Yura Pakhuchiy
 * Copyright (c) 2007-2022 Jean-Pierre Andre
 */

#include "ntfs.h"
#include "attrib.h"

/*
 * ntfs_rl_mm - runlist memmove
 * @base: base runlist array
 * @dst: destination index in @base
 * @src: source index in @base
 * @size: number of elements to move
 *
 * It is up to the caller to serialize access to the runlist @base.
 */
static inline void ntfs_rl_mm(struct runlist_element *base, int dst, int src, int size)
{
	if (likely((dst != src) && (size > 0)))
		memmove(base + dst, base + src, size * sizeof(*base));
}

/*
 * ntfs_rl_mc - runlist memory copy
 * @dstbase: destination runlist array
 * @dst: destination index in @dstbase
 * @srcbase: source runlist array
 * @src: source index in @srcbase
 * @size: number of elements to copy
 *
 * It is up to the caller to serialize access to the runlists @dstbase and
 * @srcbase.
 */
static inline void ntfs_rl_mc(struct runlist_element *dstbase, int dst,
		struct runlist_element *srcbase, int src, int size)
{
	if (likely(size > 0))
		memcpy(dstbase + dst, srcbase + src, size * sizeof(*dstbase));
}

/*
 * ntfs_rl_realloc - Reallocate memory for runlists
 * @rl:		original runlist
 * @old_size:	number of runlist elements in the original runlist @rl
 * @new_size:	number of runlist elements we need space for
 *
 * As the runlists grow, more memory will be required.  To prevent the
 * kernel having to allocate and reallocate large numbers of small bits of
 * memory, this function returns an entire page of memory.
 *
 * It is up to the caller to serialize access to the runlist @rl.
 *
 * N.B.  If the new allocation doesn't require a different number of pages in
 *       memory, the function will return the original pointer.
 *
 * On success, return a pointer to the newly allocated, or recycled, memory.
 * On error, return -errno.
 */
struct runlist_element *ntfs_rl_realloc(struct runlist_element *rl,
		int old_size, int new_size)
{
	struct runlist_element *new_rl;

	old_size = old_size * sizeof(*rl);
	new_size = new_size * sizeof(*rl);
	if (old_size == new_size)
		return rl;

	new_rl = kvzalloc(new_size, GFP_NOFS);
	if (unlikely(!new_rl))
		return ERR_PTR(-ENOMEM);

	if (likely(rl != NULL)) {
		if (unlikely(old_size > new_size))
			old_size = new_size;
		memcpy(new_rl, rl, old_size);
		kvfree(rl);
	}
	return new_rl;
}

/*
 * ntfs_rl_realloc_nofail - Reallocate memory for runlists
 * @rl:		original runlist
 * @old_size:	number of runlist elements in the original runlist @rl
 * @new_size:	number of runlist elements we need space for
 *
 * As the runlists grow, more memory will be required.  To prevent the
 * kernel having to allocate and reallocate large numbers of small bits of
 * memory, this function returns an entire page of memory.
 *
 * This function guarantees that the allocation will succeed.  It will sleep
 * for as long as it takes to complete the allocation.
 *
 * It is up to the caller to serialize access to the runlist @rl.
 *
 * N.B.  If the new allocation doesn't require a different number of pages in
 *       memory, the function will return the original pointer.
 *
 * On success, return a pointer to the newly allocated, or recycled, memory.
 * On error, return -errno.
 */
static inline struct runlist_element *ntfs_rl_realloc_nofail(struct runlist_element *rl,
		int old_size, int new_size)
{
	struct runlist_element *new_rl;

	old_size = old_size * sizeof(*rl);
	new_size = new_size * sizeof(*rl);
	if (old_size == new_size)
		return rl;

	new_rl = kvmalloc(new_size, GFP_NOFS | __GFP_NOFAIL);
	if (likely(rl != NULL)) {
		if (unlikely(old_size > new_size))
			old_size = new_size;
		memcpy(new_rl, rl, old_size);
		kvfree(rl);
	}
	return new_rl;
}

/*
 * ntfs_are_rl_mergeable - test if two runlists can be joined together
 * @dst:	original runlist
 * @src:	new runlist to test for mergeability with @dst
 *
 * Test if two runlists can be joined together. For this, their VCNs and LCNs
 * must be adjacent.
 *
 * It is up to the caller to serialize access to the runlists @dst and @src.
 *
 * Return: true   Success, the runlists can be merged.
 *	   false  Failure, the runlists cannot be merged.
 */
static inline bool ntfs_are_rl_mergeable(struct runlist_element *dst,
		struct runlist_element *src)
{
	/* We can merge unmapped regions even if they are misaligned. */
	if ((dst->lcn == LCN_RL_NOT_MAPPED) && (src->lcn == LCN_RL_NOT_MAPPED))
		return true;
	/* If the runs are misaligned, we cannot merge them. */
	if ((dst->vcn + dst->length) != src->vcn)
		return false;
	/* If both runs are non-sparse and contiguous, we can merge them. */
	if ((dst->lcn >= 0) && (src->lcn >= 0) &&
			((dst->lcn + dst->length) == src->lcn))
		return true;
	/* If we are merging two holes, we can merge them. */
	if ((dst->lcn == LCN_HOLE) && (src->lcn == LCN_HOLE))
		return true;
	/* If we are merging two dealloc, we can merge them. */
	if ((dst->lcn == LCN_DELALLOC) && (src->lcn == LCN_DELALLOC))
		return true;
	/* Cannot merge. */
	return false;
}

/*
 * __ntfs_rl_merge - merge two runlists without testing if they can be merged
 * @dst:	original, destination runlist
 * @src:	new runlist to merge with @dst
 *
 * Merge the two runlists, writing into the destination runlist @dst. The
 * caller must make sure the runlists can be merged or this will corrupt the
 * destination runlist.
 *
 * It is up to the caller to serialize access to the runlists @dst and @src.
 */
static inline void __ntfs_rl_merge(struct runlist_element *dst, struct runlist_element *src)
{
	dst->length += src->length;
}

/*
 * ntfs_rl_append - append a runlist after a given element
 * @dst: destination runlist to append to
 * @dsize: number of elements in @dst
 * @src: source runlist to append from
 * @ssize: number of elements in @src
 * @loc: index in @dst after which to append @src
 * @new_size: on success, set to the new combined size
 *
 * Append the runlist @src after element @loc in @dst.  Merge the right end of
 * the new runlist, if necessary. Adjust the size of the hole before the
 * appended runlist.
 *
 * It is up to the caller to serialize access to the runlists @dst and @src.
 *
 * On success, return a pointer to the new, combined, runlist. Note, both
 * runlists @dst and @src are deallocated before returning so you cannot use
 * the pointers for anything any more. (Strictly speaking the returned runlist
 * may be the same as @dst but this is irrelevant.)
 *
 * On error, return -errno. Both runlists are left unmodified.
 */
static inline struct runlist_element *ntfs_rl_append(struct runlist_element *dst,
		int dsize, struct runlist_element *src, int ssize, int loc,
		size_t *new_size)
{
	bool right = false;	/* Right end of @src needs merging. */
	int marker;		/* End of the inserted runs. */

	/* First, check if the right hand end needs merging. */
	if ((loc + 1) < dsize)
		right = ntfs_are_rl_mergeable(src + ssize - 1, dst + loc + 1);

	/* Space required: @dst size + @src size, less one if we merged. */
	dst = ntfs_rl_realloc(dst, dsize, dsize + ssize - right);
	if (IS_ERR(dst))
		return dst;

	*new_size = dsize + ssize - right;
	/*
	 * We are guaranteed to succeed from here so can start modifying the
	 * original runlists.
	 */

	/* First, merge the right hand end, if necessary. */
	if (right)
		__ntfs_rl_merge(src + ssize - 1, dst + loc + 1);

	/* First run after the @src runs that have been inserted. */
	marker = loc + ssize + 1;

	/* Move the tail of @dst out of the way, then copy in @src. */
	ntfs_rl_mm(dst, marker, loc + 1 + right, dsize - (loc + 1 + right));
	ntfs_rl_mc(dst, loc + 1, src, 0, ssize);

	/* Adjust the size of the preceding hole. */
	dst[loc].length = dst[loc + 1].vcn - dst[loc].vcn;

	/* We may have changed the length of the file, so fix the end marker */
	if (dst[marker].lcn == LCN_ENOENT)
		dst[marker].vcn = dst[marker - 1].vcn + dst[marker - 1].length;

	return dst;
}

/*
 * ntfs_rl_insert - insert a runlist into another
 * @dst: destination runlist to insert into
 * @dsize: number of elements in @dst
 * @src: source runlist to insert from
 * @ssize: number of elements in @src
 * @loc: index in @dst at which to insert @src
 * @new_size: on success, set to the new combined size
 *
 * Insert the runlist @src before element @loc in the runlist @dst. Merge the
 * left end of the new runlist, if necessary. Adjust the size of the hole
 * after the inserted runlist.
 *
 * It is up to the caller to serialize access to the runlists @dst and @src.
 *
 * On success, return a pointer to the new, combined, runlist. Note, both
 * runlists @dst and @src are deallocated before returning so you cannot use
 * the pointers for anything any more. (Strictly speaking the returned runlist
 * may be the same as @dst but this is irrelevant.)
 *
 * On error, return -errno. Both runlists are left unmodified.
 */
static inline struct runlist_element *ntfs_rl_insert(struct runlist_element *dst,
		int dsize, struct runlist_element *src, int ssize, int loc,
		size_t *new_size)
{
	bool left = false;	/* Left end of @src needs merging. */
	bool disc = false;	/* Discontinuity between @dst and @src. */
	int marker;		/* End of the inserted runs. */

	/*
	 * disc => Discontinuity between the end of @dst and the start of @src.
	 *	   This means we might need to insert a "not mapped" run.
	 */
	if (loc == 0)
		disc = (src[0].vcn > 0);
	else {
		s64 merged_length;

		left = ntfs_are_rl_mergeable(dst + loc - 1, src);

		merged_length = dst[loc - 1].length;
		if (left)
			merged_length += src->length;

		disc = (src[0].vcn > dst[loc - 1].vcn + merged_length);
	}
	/*
	 * Space required: @dst size + @src size, less one if we merged, plus
	 * one if there was a discontinuity.
	 */
	dst = ntfs_rl_realloc(dst, dsize, dsize + ssize - left + disc);
	if (IS_ERR(dst))
		return dst;

	*new_size = dsize + ssize - left + disc;
	/*
	 * We are guaranteed to succeed from here so can start modifying the
	 * original runlist.
	 */
	if (left)
		__ntfs_rl_merge(dst + loc - 1, src);
	/*
	 * First run after the @src runs that have been inserted.
	 * Nominally,  @marker equals @loc + @ssize, i.e. location + number of
	 * runs in @src.  However, if @left, then the first run in @src has
	 * been merged with one in @dst.  And if @disc, then @dst and @src do
	 * not meet and we need an extra run to fill the gap.
	 */
	marker = loc + ssize - left + disc;

	/* Move the tail of @dst out of the way, then copy in @src. */
	ntfs_rl_mm(dst, marker, loc, dsize - loc);
	ntfs_rl_mc(dst, loc + disc, src, left, ssize - left);

	/* Adjust the VCN of the first run after the insertion... */
	dst[marker].vcn = dst[marker - 1].vcn + dst[marker - 1].length;
	/* ... and the length. */
	if (dst[marker].lcn == LCN_HOLE || dst[marker].lcn == LCN_RL_NOT_MAPPED ||
	    dst[marker].lcn == LCN_DELALLOC)
		dst[marker].length = dst[marker + 1].vcn - dst[marker].vcn;

	/* Writing beyond the end of the file and there is a discontinuity. */
	if (disc) {
		if (loc > 0) {
			dst[loc].vcn = dst[loc - 1].vcn + dst[loc - 1].length;
			dst[loc].length = dst[loc + 1].vcn - dst[loc].vcn;
		} else {
			dst[loc].vcn = 0;
			dst[loc].length = dst[loc + 1].vcn;
		}
		dst[loc].lcn = LCN_RL_NOT_MAPPED;
	}
	return dst;
}

/*
 * ntfs_rl_replace - overwrite a runlist element with another runlist
 * @dst: destination runlist to replace in
 * @dsize: number of elements in @dst
 * @src: source runlist to replace with
 * @ssize: number of elements in @src
 * @loc: index in @dst to replace
 * @new_size: on success, set to the new combined size
 *
 * Replace the runlist element @dst at @loc with @src. Merge the left and
 * right ends of the inserted runlist, if necessary.
 *
 * It is up to the caller to serialize access to the runlists @dst and @src.
 *
 * On success, return a pointer to the new, combined, runlist. Note, both
 * runlists @dst and @src are deallocated before returning so you cannot use
 * the pointers for anything any more. (Strictly speaking the returned runlist
 * may be the same as @dst but this is irrelevant.)
 *
 * On error, return -errno. Both runlists are left unmodified.
 */
static inline struct runlist_element *ntfs_rl_replace(struct runlist_element *dst,
		int dsize, struct runlist_element *src, int ssize, int loc,
		size_t *new_size)
{
	int delta;
	bool left = false;	/* Left end of @src needs merging. */
	bool right = false;	/* Right end of @src needs merging. */
	int tail;		/* Start of tail of @dst. */
	int marker;		/* End of the inserted runs. */

	/* First, see if the left and right ends need merging. */
	if ((loc + 1) < dsize)
		right = ntfs_are_rl_mergeable(src + ssize - 1, dst + loc + 1);
	if (loc > 0)
		left = ntfs_are_rl_mergeable(dst + loc - 1, src);
	/*
	 * Allocate some space.  We will need less if the left, right, or both
	 * ends get merged.  The -1 accounts for the run being replaced.
	 */
	delta = ssize - 1 - left - right;
	if (delta > 0) {
		dst = ntfs_rl_realloc(dst, dsize, dsize + delta);
		if (IS_ERR(dst))
			return dst;
	}

	*new_size = dsize + delta;
	/*
	 * We are guaranteed to succeed from here so can start modifying the
	 * original runlists.
	 */

	/* First, merge the left and right ends, if necessary. */
	if (right)
		__ntfs_rl_merge(src + ssize - 1, dst + loc + 1);
	if (left)
		__ntfs_rl_merge(dst + loc - 1, src);
	/*
	 * Offset of the tail of @dst.  This needs to be moved out of the way
	 * to make space for the runs to be copied from @src, i.e. the first
	 * run of the tail of @dst.
	 * Nominally, @tail equals @loc + 1, i.e. location, skipping the
	 * replaced run.  However, if @right, then one of @dst's runs is
	 * already merged into @src.
	 */
	tail = loc + right + 1;
	/*
	 * First run after the @src runs that have been inserted, i.e. where
	 * the tail of @dst needs to be moved to.
	 * Nominally, @marker equals @loc + @ssize, i.e. location + number of
	 * runs in @src.  However, if @left, then the first run in @src has
	 * been merged with one in @dst.
	 */
	marker = loc + ssize - left;

	/* Move the tail of @dst out of the way, then copy in @src. */
	ntfs_rl_mm(dst, marker, tail, dsize - tail);
	ntfs_rl_mc(dst, loc, src, left, ssize - left);

	/* We may have changed the length of the file, so fix the end marker. */
	if (dsize - tail > 0 && dst[marker].lcn == LCN_ENOENT)
		dst[marker].vcn = dst[marker - 1].vcn + dst[marker - 1].length;
	return dst;
}

/*
 * ntfs_rl_split - insert a runlist into the centre of a hole
 * @dst: destination runlist with a hole
 * @dsize: number of elements in @dst
 * @src: source runlist to insert
 * @ssize: number of elements in @src
 * @loc: index in @dst of the hole to split
 * @new_size: on success, set to the new combined size
 *
 * Split the runlist @dst at @loc into two and insert @new in between the two
 * fragments. No merging of runlists is necessary. Adjust the size of the
 * holes either side.
 *
 * It is up to the caller to serialize access to the runlists @dst and @src.
 *
 * On success, return a pointer to the new, combined, runlist. Note, both
 * runlists @dst and @src are deallocated before returning so you cannot use
 * the pointers for anything any more. (Strictly speaking the returned runlist
 * may be the same as @dst but this is irrelevant.)
 *
 * On error, return -errno. Both runlists are left unmodified.
 */
static inline struct runlist_element *ntfs_rl_split(struct runlist_element *dst, int dsize,
		struct runlist_element *src, int ssize, int loc,
		size_t *new_size)
{
	/* Space required: @dst size + @src size + one new hole. */
	dst = ntfs_rl_realloc(dst, dsize, dsize + ssize + 1);
	if (IS_ERR(dst))
		return dst;

	*new_size = dsize + ssize + 1;
	/*
	 * We are guaranteed to succeed from here so can start modifying the
	 * original runlists.
	 */

	/* Move the tail of @dst out of the way, then copy in @src. */
	ntfs_rl_mm(dst, loc + 1 + ssize, loc, dsize - loc);
	ntfs_rl_mc(dst, loc + 1, src, 0, ssize);

	/* Adjust the size of the holes either size of @src. */
	dst[loc].length		= dst[loc+1].vcn       - dst[loc].vcn;
	dst[loc+ssize+1].vcn    = dst[loc+ssize].vcn   + dst[loc+ssize].length;
	dst[loc+ssize+1].length = dst[loc+ssize+2].vcn - dst[loc+ssize+1].vcn;

	return dst;
}

/*
 * ntfs_runlists_merge - merge two runlists into one
 * @d_runlist: destination runlist structure to merge into
 * @srl: source runlist to merge from
 * @s_rl_count: number of elements in @srl (0 to auto-detect)
 * @new_rl_count: on success, set to the new combined runlist size
 *
 * First we sanity check the two runlists @srl and @drl to make sure that they
 * are sensible and can be merged. The runlist @srl must be either after the
 * runlist @drl or completely within a hole (or unmapped region) in @drl.
 *
 * It is up to the caller to serialize access to the runlists @drl and @srl.
 *
 * Merging of runlists is necessary in two cases:
 *   1. When attribute lists are used and a further extent is being mapped.
 *   2. When new clusters are allocated to fill a hole or extend a file.
 *
 * There are four possible ways @srl can be merged. It can:
 *	- be inserted at the beginning of a hole,
 *	- split the hole in two and be inserted between the two fragments,
 *	- be appended at the end of a hole, or it can
 *	- replace the whole hole.
 * It can also be appended to the end of the runlist, which is just a variant
 * of the insert case.
 *
 * On success, return a pointer to the new, combined, runlist. Note, both
 * runlists @drl and @srl are deallocated before returning so you cannot use
 * the pointers for anything any more. (Strictly speaking the returned runlist
 * may be the same as @dst but this is irrelevant.)
 *
 * On error, return -errno. Both runlists are left unmodified.
 */
struct runlist_element *ntfs_runlists_merge(struct runlist *d_runlist,
				     struct runlist_element *srl, size_t s_rl_count,
				     size_t *new_rl_count)
{
	int di, si;		/* Current index into @[ds]rl. */
	int sstart;		/* First index with lcn > LCN_RL_NOT_MAPPED. */
	int dins;		/* Index into @drl at which to insert @srl. */
	int dend, send;		/* Last index into @[ds]rl. */
	int dfinal, sfinal;	/* The last index into @[ds]rl with lcn >= LCN_HOLE. */
	int marker = 0;
	s64 marker_vcn = 0;
	struct runlist_element *drl = d_runlist->rl, *rl;

#ifdef DEBUG
	ntfs_debug("dst:");
	ntfs_debug_dump_runlist(drl);
	ntfs_debug("src:");
	ntfs_debug_dump_runlist(srl);
#endif

	/* Check for silly calling... */
	if (unlikely(!srl))
		return drl;
	if (IS_ERR(srl) || IS_ERR(drl))
		return ERR_PTR(-EINVAL);

	if (s_rl_count == 0) {
		for (; srl[s_rl_count].length; s_rl_count++)
			;
		s_rl_count++;
	}

	/* Check for the case where the first mapping is being done now. */
	if (unlikely(!drl)) {
		drl = srl;
		/* Complete the source runlist if necessary. */
		if (unlikely(drl[0].vcn)) {
			/* Scan to the end of the source runlist. */
			drl = ntfs_rl_realloc(drl, s_rl_count, s_rl_count + 1);
			if (IS_ERR(drl))
				return drl;
			/* Insert start element at the front of the runlist. */
			ntfs_rl_mm(drl, 1, 0, s_rl_count);
			drl[0].vcn = 0;
			drl[0].lcn = LCN_RL_NOT_MAPPED;
			drl[0].length = drl[1].vcn;
			s_rl_count++;
		}

		*new_rl_count = s_rl_count;
		goto finished;
	}

	if (d_runlist->count < 1 || s_rl_count < 2)
		return ERR_PTR(-EINVAL);

	si = di = 0;

	/* Skip any unmapped start element(s) in the source runlist. */
	while (srl[si].length && srl[si].lcn < LCN_HOLE)
		si++;

	/* Can't have an entirely unmapped source runlist. */
	WARN_ON(!srl[si].length);

	/* Record the starting points. */
	sstart = si;

	/*
	 * Skip forward in @drl until we reach the position where @srl needs to
	 * be inserted. If we reach the end of @drl, @srl just needs to be
	 * appended to @drl.
	 */
	rl = __ntfs_attr_find_vcn_nolock(d_runlist, srl[sstart].vcn);
	if (IS_ERR(rl))
		di = (int)d_runlist->count - 1;
	else
		di = (int)(rl - d_runlist->rl);
	dins = di;

	/* Sanity check for illegal overlaps. */
	if ((drl[di].vcn == srl[si].vcn) && (drl[di].lcn >= 0) &&
			(srl[si].lcn >= 0)) {
		ntfs_error(NULL, "Run lists overlap. Cannot merge!");
		return ERR_PTR(-ERANGE);
	}

	/* Scan to the end of both runlists in order to know their sizes. */
	send = (int)s_rl_count - 1;
	dend = (int)d_runlist->count - 1;

	if (srl[send].lcn == LCN_ENOENT)
		marker_vcn = srl[marker = send].vcn;

	/* Scan to the last element with lcn >= LCN_HOLE. */
	for (sfinal = send; sfinal >= 0 && srl[sfinal].lcn < LCN_HOLE; sfinal--)
		;
	for (dfinal = dend; dfinal >= 0 && drl[dfinal].lcn < LCN_HOLE; dfinal--)
		;

	{
	bool start;
	bool finish;
	int ds = dend + 1;		/* Number of elements in drl & srl */
	int ss = sfinal - sstart + 1;

	start  = ((drl[dins].lcn <  LCN_RL_NOT_MAPPED) ||    /* End of file   */
		  (drl[dins].vcn == srl[sstart].vcn));	     /* Start of hole */
	finish = ((drl[dins].lcn >= LCN_RL_NOT_MAPPED) &&    /* End of file   */
		 ((drl[dins].vcn + drl[dins].length) <=      /* End of hole   */
		  (srl[send - 1].vcn + srl[send - 1].length)));

	/* Or we will lose an end marker. */
	if (finish && !drl[dins].length)
		ss++;
	if (marker && (drl[dins].vcn + drl[dins].length > srl[send - 1].vcn))
		finish = false;

	if (start) {
		if (finish)
			drl = ntfs_rl_replace(drl, ds, srl + sstart, ss, dins, new_rl_count);
		else
			drl = ntfs_rl_insert(drl, ds, srl + sstart, ss, dins, new_rl_count);
	} else {
		if (finish)
			drl = ntfs_rl_append(drl, ds, srl + sstart, ss, dins, new_rl_count);
		else
			drl = ntfs_rl_split(drl, ds, srl + sstart, ss, dins, new_rl_count);
	}
	if (IS_ERR(drl)) {
		ntfs_error(NULL, "Merge failed.");
		return drl;
	}
	kvfree(srl);
	if (marker) {
		ntfs_debug("Triggering marker code.");
		for (ds = dend; drl[ds].length; ds++)
			;
		/* We only need to care if @srl ended after @drl. */
		if (drl[ds].vcn <= marker_vcn) {
			int slots = 0;

			if (drl[ds].vcn == marker_vcn) {
				ntfs_debug("Old marker = 0x%llx, replacing with LCN_ENOENT.",
						drl[ds].lcn);
				drl[ds].lcn = LCN_ENOENT;
				goto finished;
			}
			/*
			 * We need to create an unmapped runlist element in
			 * @drl or extend an existing one before adding the
			 * ENOENT terminator.
			 */
			if (drl[ds].lcn == LCN_ENOENT) {
				ds--;
				slots = 1;
			}
			if (drl[ds].lcn != LCN_RL_NOT_MAPPED) {
				/* Add an unmapped runlist element. */
				if (!slots) {
					drl = ntfs_rl_realloc_nofail(drl, ds,
							ds + 2);
					slots = 2;
					*new_rl_count += 2;
				}
				ds++;
				/* Need to set vcn if it isn't set already. */
				if (slots != 1)
					drl[ds].vcn = drl[ds - 1].vcn +
							drl[ds - 1].length;
				drl[ds].lcn = LCN_RL_NOT_MAPPED;
				/* We now used up a slot. */
				slots--;
			}
			drl[ds].length = marker_vcn - drl[ds].vcn;
			/* Finally add the ENOENT terminator. */
			ds++;
			if (!slots) {
				drl = ntfs_rl_realloc_nofail(drl, ds, ds + 1);
				*new_rl_count += 1;
			}
			drl[ds].vcn = marker_vcn;
			drl[ds].lcn = LCN_ENOENT;
			drl[ds].length = (s64)0;
		}
	}
	}

finished:
	/* The merge was completed successfully. */
	ntfs_debug("Merged runlist:");
	ntfs_debug_dump_runlist(drl);
	return drl;
}

/*
 * ntfs_mapping_pairs_decompress - convert mapping pairs array to runlist
 * @vol: ntfs volume
 * @attr: attribute record whose mapping pairs to decompress
 * @old_runlist: optional runlist to merge the decompressed runlist into
 * @new_rl_count: on success, set to the new runlist size
 *
 * It is up to the caller to serialize access to the runlist @old_rl.
 *
 * Decompress the attribute @attr's mapping pairs array into a runlist. On
 * success, return the decompressed runlist.
 *
 * If @old_rl is not NULL, decompressed runlist is inserted into the
 * appropriate place in @old_rl and the resultant, combined runlist is
 * returned. The original @old_rl is deallocated.
 *
 * On error, return -errno. @old_rl is left unmodified in that case.
 */
struct runlist_element *ntfs_mapping_pairs_decompress(const struct ntfs_volume *vol,
		const struct attr_record *attr, struct runlist *old_runlist,
		size_t *new_rl_count)
{
	s64 vcn;		/* Current vcn. */
	s64 lcn;		/* Current lcn. */
	s64 deltaxcn;		/* Change in [vl]cn. */
	struct runlist_element *rl, *new_rl;	/* The output runlist. */
	u8 *buf;		/* Current position in mapping pairs array. */
	u8 *attr_end;		/* End of attribute. */
	int rlsize;		/* Size of runlist buffer. */
	u16 rlpos;		/* Current runlist position in units of struct runlist_elements. */
	u8 b;			/* Current byte offset in buf. */

#ifdef DEBUG
	/* Make sure attr exists and is non-resident. */
	if (!attr || !attr->non_resident) {
		ntfs_error(vol->sb, "Invalid arguments.");
		return ERR_PTR(-EINVAL);
	}
#endif
	/* Start at vcn = lowest_vcn and lcn 0. */
	vcn = le64_to_cpu(attr->data.non_resident.lowest_vcn);
	lcn = 0;
	/* Get start of the mapping pairs array. */
	buf = (u8 *)attr +
		le16_to_cpu(attr->data.non_resident.mapping_pairs_offset);
	attr_end = (u8 *)attr + le32_to_cpu(attr->length);
	if (unlikely(buf < (u8 *)attr || buf > attr_end)) {
		ntfs_error(vol->sb, "Corrupt attribute.");
		return ERR_PTR(-EIO);
	}

	/* Current position in runlist array. */
	rlpos = 0;
	/* Allocate first page and set current runlist size to one page. */
	rl = kvzalloc(rlsize = PAGE_SIZE, GFP_NOFS);
	if (unlikely(!rl))
		return ERR_PTR(-ENOMEM);
	/* Insert unmapped starting element if necessary. */
	if (vcn) {
		rl->vcn = 0;
		rl->lcn = LCN_RL_NOT_MAPPED;
		rl->length = vcn;
		rlpos++;
	}
	while (buf < attr_end && *buf) {
		/*
		 * Allocate more memory if needed, including space for the
		 * not-mapped and terminator elements. kvzalloc()
		 * operates on whole pages only.
		 */
		if (((rlpos + 3) * sizeof(*rl)) > rlsize) {
			struct runlist_element *rl2;

			rl2 = kvzalloc(rlsize + PAGE_SIZE, GFP_NOFS);
			if (unlikely(!rl2)) {
				kvfree(rl);
				return ERR_PTR(-ENOMEM);
			}
			memcpy(rl2, rl, rlsize);
			kvfree(rl);
			rl = rl2;
			rlsize += PAGE_SIZE;
		}
		/* Enter the current vcn into the current runlist element. */
		rl[rlpos].vcn = vcn;
		/*
		 * Get the change in vcn, i.e. the run length in clusters.
		 * Doing it this way ensures that we signextend negative values.
		 * A negative run length doesn't make any sense, but hey, I
		 * didn't make up the NTFS specs and Windows NT4 treats the run
		 * length as a signed value so that's how it is...
		 */
		b = *buf & 0xf;
		if (b) {
			if (unlikely(buf + b > attr_end))
				goto io_error;
			for (deltaxcn = (s8)buf[b--]; b; b--)
				deltaxcn = (deltaxcn << 8) + buf[b];
		} else { /* The length entry is compulsory. */
			ntfs_error(vol->sb, "Missing length entry in mapping pairs array.");
			deltaxcn = (s64)-1;
		}
		/*
		 * Assume a negative length to indicate data corruption and
		 * hence clean-up and return NULL.
		 */
		if (unlikely(deltaxcn < 0)) {
			ntfs_error(vol->sb, "Invalid length in mapping pairs array.");
			goto err_out;
		}
		/*
		 * Enter the current run length into the current runlist
		 * element.
		 */
		rl[rlpos].length = deltaxcn;
		/* Increment the current vcn by the current run length. */
		vcn += deltaxcn;
		/*
		 * There might be no lcn change at all, as is the case for
		 * sparse clusters on NTFS 3.0+, in which case we set the lcn
		 * to LCN_HOLE.
		 */
		if (!(*buf & 0xf0))
			rl[rlpos].lcn = LCN_HOLE;
		else {
			/* Get the lcn change which really can be negative. */
			u8 b2 = *buf & 0xf;

			b = b2 + ((*buf >> 4) & 0xf);
			if (buf + b > attr_end)
				goto io_error;
			for (deltaxcn = (s8)buf[b--]; b > b2; b--)
				deltaxcn = (deltaxcn << 8) + buf[b];
			/* Change the current lcn to its new value. */
			lcn += deltaxcn;
#ifdef DEBUG
			/*
			 * On NTFS 1.2-, apparently can have lcn == -1 to
			 * indicate a hole. But we haven't verified ourselves
			 * whether it is really the lcn or the deltaxcn that is
			 * -1. So if either is found give us a message so we
			 * can investigate it further!
			 */
			if (vol->major_ver < 3) {
				if (unlikely(deltaxcn == -1))
					ntfs_error(vol->sb, "lcn delta == -1");
				if (unlikely(lcn == -1))
					ntfs_error(vol->sb, "lcn == -1");
			}
#endif
			/* Check lcn is not below -1. */
			if (unlikely(lcn < -1)) {
				ntfs_error(vol->sb, "Invalid s64 < -1 in mapping pairs array.");
				goto err_out;
			}

			/* chkdsk accepts zero-sized runs only for holes */
			if ((lcn != -1) && !rl[rlpos].length) {
				ntfs_error(vol->sb,
					   "Invalid zero-sized data run(lcn : %lld).\n",
					   lcn);
				goto err_out;
			}

			/* Enter the current lcn into the runlist element. */
			rl[rlpos].lcn = lcn;
		}
		/* Get to the next runlist element, skipping zero-sized holes */
		if (rl[rlpos].length)
			rlpos++;
		/* Increment the buffer position to the next mapping pair. */
		buf += (*buf & 0xf) + ((*buf >> 4) & 0xf) + 1;
	}
	if (unlikely(buf >= attr_end))
		goto io_error;
	/*
	 * If there is a highest_vcn specified, it must be equal to the final
	 * vcn in the runlist - 1, or something has gone badly wrong.
	 */
	deltaxcn = le64_to_cpu(attr->data.non_resident.highest_vcn);
	if (unlikely(deltaxcn && vcn - 1 != deltaxcn)) {
mpa_err:
		ntfs_error(vol->sb, "Corrupt mapping pairs array in non-resident attribute.");
		goto err_out;
	}
	/* Setup not mapped runlist element if this is the base extent. */
	if (!attr->data.non_resident.lowest_vcn) {
		s64 max_cluster;

		max_cluster = ((le64_to_cpu(attr->data.non_resident.allocated_size) +
				vol->cluster_size - 1) >>
				vol->cluster_size_bits) - 1;
		/*
		 * A highest_vcn of zero means this is a single extent
		 * attribute so simply terminate the runlist with LCN_ENOENT).
		 */
		if (deltaxcn) {
			/*
			 * If there is a difference between the highest_vcn and
			 * the highest cluster, the runlist is either corrupt
			 * or, more likely, there are more extents following
			 * this one.
			 */
			if (deltaxcn < max_cluster) {
				ntfs_debug("More extents to follow; deltaxcn = 0x%llx, max_cluster = 0x%llx",
						deltaxcn, max_cluster);
				rl[rlpos].vcn = vcn;
				vcn += rl[rlpos].length = max_cluster -
						deltaxcn;
				rl[rlpos].lcn = LCN_RL_NOT_MAPPED;
				rlpos++;
			} else if (unlikely(deltaxcn > max_cluster)) {
				ntfs_error(vol->sb,
					   "Corrupt attribute. deltaxcn = 0x%llx, max_cluster = 0x%llx",
					   deltaxcn, max_cluster);
				goto mpa_err;
			}
		}
		rl[rlpos].lcn = LCN_ENOENT;
	} else /* Not the base extent. There may be more extents to follow. */
		rl[rlpos].lcn = LCN_RL_NOT_MAPPED;

	/* Setup terminating runlist element. */
	rl[rlpos].vcn = vcn;
	rl[rlpos].length = (s64)0;
	/* If no existing runlist was specified, we are done. */
	if (!old_runlist || !old_runlist->rl) {
		*new_rl_count = rlpos + 1;
		ntfs_debug("Mapping pairs array successfully decompressed:");
		ntfs_debug_dump_runlist(rl);
		return rl;
	}
	/* Now combine the new and old runlists checking for overlaps. */
	new_rl = ntfs_runlists_merge(old_runlist, rl, rlpos + 1, new_rl_count);
	if (!IS_ERR(new_rl))
		return new_rl;
	kvfree(rl);
	ntfs_error(vol->sb, "Failed to merge runlists.");
	return new_rl;
io_error:
	ntfs_error(vol->sb, "Corrupt attribute.");
err_out:
	kvfree(rl);
	return ERR_PTR(-EIO);
}

/*
 * ntfs_rl_vcn_to_lcn - convert a vcn into a lcn given a runlist
 * @rl:		runlist to use for conversion
 * @vcn:	vcn to convert
 *
 * Convert the virtual cluster number @vcn of an attribute into a logical
 * cluster number (lcn) of a device using the runlist @rl to map vcns to their
 * corresponding lcns.
 *
 * It is up to the caller to serialize access to the runlist @rl.
 *
 * Since lcns must be >= 0, we use negative return codes with special meaning:
 *
 * Return code		Meaning / Description
 * ==================================================
 *  LCN_HOLE		Hole / not allocated on disk.
 *  LCN_RL_NOT_MAPPED	This is part of the runlist which has not been
 *			inserted into the runlist yet.
 *  LCN_ENOENT		There is no such vcn in the attribute.
 *
 * Locking: - The caller must have locked the runlist (for reading or writing).
 *	    - This function does not touch the lock, nor does it modify the
 *	      runlist.
 */
s64 ntfs_rl_vcn_to_lcn(const struct runlist_element *rl, const s64 vcn)
{
	int i;

	/*
	 * If rl is NULL, assume that we have found an unmapped runlist. The
	 * caller can then attempt to map it and fail appropriately if
	 * necessary.
	 */
	if (unlikely(!rl))
		return LCN_RL_NOT_MAPPED;

	/* Catch out of lower bounds vcn. */
	if (unlikely(vcn < rl[0].vcn))
		return LCN_ENOENT;

	for (i = 0; likely(rl[i].length); i++) {
		if (vcn < rl[i+1].vcn) {
			if (likely(rl[i].lcn >= 0))
				return rl[i].lcn + (vcn - rl[i].vcn);
			return rl[i].lcn;
		}
	}
	/*
	 * The terminator element is setup to the correct value, i.e. one of
	 * LCN_HOLE, LCN_RL_NOT_MAPPED, or LCN_ENOENT.
	 */
	if (likely(rl[i].lcn < 0))
		return rl[i].lcn;
	/* Just in case... We could replace this with BUG() some day. */
	return LCN_ENOENT;
}

/*
 * ntfs_rl_find_vcn_nolock - find a vcn in a runlist
 * @rl:		runlist to search
 * @vcn:	vcn to find
 *
 * Find the virtual cluster number @vcn in the runlist @rl and return the
 * address of the runlist element containing the @vcn on success.
 *
 * Return NULL if @rl is NULL or @vcn is in an unmapped part/out of bounds of
 * the runlist.
 *
 * Locking: The runlist must be locked on entry.
 */
struct runlist_element *ntfs_rl_find_vcn_nolock(struct runlist_element *rl, const s64 vcn)
{
	if (unlikely(!rl || vcn < rl[0].vcn))
		return NULL;
	while (likely(rl->length)) {
		if (unlikely(vcn < rl[1].vcn)) {
			if (likely(rl->lcn >= LCN_HOLE))
				return rl;
			return NULL;
		}
		rl++;
	}
	if (likely(rl->lcn == LCN_ENOENT))
		return rl;
	return NULL;
}

/*
 * ntfs_get_nr_significant_bytes - get number of bytes needed to store a number
 * @n:		number for which to get the number of bytes for
 *
 * Return the number of bytes required to store @n unambiguously as
 * a signed number.
 *
 * This is used in the context of the mapping pairs array to determine how
 * many bytes will be needed in the array to store a given logical cluster
 * number (lcn) or a specific run length.
 *
 * Return the number of bytes written.  This function cannot fail.
 */
static inline int ntfs_get_nr_significant_bytes(const s64 n)
{
	s64 l = n;
	int i;
	s8 j;

	i = 0;
	do {
		l >>= 8;
		i++;
	} while (l != 0 && l != -1);
	j = (n >> 8 * (i - 1)) & 0xff;
	/* If the sign bit is wrong, we need an extra byte. */
	if ((n < 0 && j >= 0) || (n > 0 && j < 0))
		i++;
	return i;
}

/*
 * ntfs_get_size_for_mapping_pairs - get bytes needed for mapping pairs array
 * @vol: ntfs volume
 * @rl: runlist to calculate the mapping pairs array size for
 * @first_vcn: first vcn which to include in the mapping pairs array
 * @last_vcn: last vcn which to include in the mapping pairs array
 * @max_mp_size: maximum size to return (0 or less means unlimited)
 *
 * Walk the locked runlist @rl and calculate the size in bytes of the mapping
 * pairs array corresponding to the runlist @rl, starting at vcn @first_vcn and
 * finishing with vcn @last_vcn.
 *
 * A @last_vcn of -1 means end of runlist and in that case the size of the
 * mapping pairs array corresponding to the runlist starting at vcn @first_vcn
 * and finishing at the end of the runlist is determined.
 *
 * This for example allows us to allocate a buffer of the right size when
 * building the mapping pairs array.
 *
 * If @rl is NULL, just return 1 (for the single terminator byte).
 *
 * Return the calculated size in bytes on success.  On error, return -errno.
 */
int ntfs_get_size_for_mapping_pairs(const struct ntfs_volume *vol,
		const struct runlist_element *rl, const s64 first_vcn,
		const s64 last_vcn, int max_mp_size)
{
	s64 prev_lcn;
	int rls;
	bool the_end = false;

	if (first_vcn < 0 || last_vcn < -1)
		return -EINVAL;

	if (last_vcn >= 0 && first_vcn > last_vcn)
		return -EINVAL;

	if (!rl) {
		WARN_ON(first_vcn);
		WARN_ON(last_vcn > 0);
		return 1;
	}
	if (max_mp_size <= 0)
		max_mp_size = INT_MAX;
	/* Skip to runlist element containing @first_vcn. */
	while (rl->length && first_vcn >= rl[1].vcn)
		rl++;
	if (unlikely((!rl->length && first_vcn > rl->vcn) ||
			first_vcn < rl->vcn))
		return -EINVAL;
	prev_lcn = 0;
	/* Always need the termining zero byte. */
	rls = 1;
	/* Do the first partial run if present. */
	if (first_vcn > rl->vcn) {
		s64 delta, length = rl->length;

		/* We know rl->length != 0 already. */
		if (unlikely(length < 0 || rl->lcn < LCN_HOLE))
			goto err_out;
		/*
		 * If @stop_vcn is given and finishes inside this run, cap the
		 * run length.
		 */
		if (unlikely(last_vcn >= 0 && rl[1].vcn > last_vcn)) {
			s64 s1 = last_vcn + 1;

			if (unlikely(rl[1].vcn > s1))
				length = s1 - rl->vcn;
			the_end = true;
		}
		delta = first_vcn - rl->vcn;
		/* Header byte + length. */
		rls += 1 + ntfs_get_nr_significant_bytes(length - delta);
		/*
		 * If the logical cluster number (lcn) denotes a hole and we
		 * are on NTFS 3.0+, we don't store it at all, i.e. we need
		 * zero space.  On earlier NTFS versions we just store the lcn.
		 * Note: this assumes that on NTFS 1.2-, holes are stored with
		 * an lcn of -1 and not a delta_lcn of -1 (unless both are -1).
		 */
		if (likely(rl->lcn >= 0 || vol->major_ver < 3)) {
			prev_lcn = rl->lcn;
			if (likely(rl->lcn >= 0))
				prev_lcn += delta;
			/* Change in lcn. */
			rls += ntfs_get_nr_significant_bytes(prev_lcn);
		}
		/* Go to next runlist element. */
		rl++;
	}
	/* Do the full runs. */
	for (; rl->length && !the_end; rl++) {
		s64 length = rl->length;

		if (unlikely(length < 0 || rl->lcn < LCN_HOLE))
			goto err_out;
		/*
		 * If @stop_vcn is given and finishes inside this run, cap the
		 * run length.
		 */
		if (unlikely(last_vcn >= 0 && rl[1].vcn > last_vcn)) {
			s64 s1 = last_vcn + 1;

			if (unlikely(rl[1].vcn > s1))
				length = s1 - rl->vcn;
			the_end = true;
		}
		/* Header byte + length. */
		rls += 1 + ntfs_get_nr_significant_bytes(length);
		/*
		 * If the logical cluster number (lcn) denotes a hole and we
		 * are on NTFS 3.0+, we don't store it at all, i.e. we need
		 * zero space.  On earlier NTFS versions we just store the lcn.
		 * Note: this assumes that on NTFS 1.2-, holes are stored with
		 * an lcn of -1 and not a delta_lcn of -1 (unless both are -1).
		 */
		if (likely(rl->lcn >= 0 || vol->major_ver < 3)) {
			/* Change in lcn. */
			rls += ntfs_get_nr_significant_bytes(rl->lcn -
					prev_lcn);
			prev_lcn = rl->lcn;
		}

		if (rls > max_mp_size)
			break;
	}
	return rls;
err_out:
	if (rl->lcn == LCN_RL_NOT_MAPPED)
		rls = -EINVAL;
	else
		rls = -EIO;
	return rls;
}

/*
 * ntfs_write_significant_bytes - write the significant bytes of a number
 * @dst:	destination buffer to write to
 * @dst_max:	pointer to last byte of destination buffer for bounds checking
 * @n:		number whose significant bytes to write
 *
 * Store in @dst, the minimum bytes of the number @n which are required to
 * identify @n unambiguously as a signed number, taking care not to exceed
 * @dest_max, the maximum position within @dst to which we are allowed to
 * write.
 *
 * This is used when building the mapping pairs array of a runlist to compress
 * a given logical cluster number (lcn) or a specific run length to the minimum
 * size possible.
 *
 * Return the number of bytes written on success.  On error, i.e. the
 * destination buffer @dst is too small, return -ENOSPC.
 */
static inline int ntfs_write_significant_bytes(s8 *dst, const s8 *dst_max,
		const s64 n)
{
	s64 l = n;
	int i;
	s8 j;

	i = 0;
	do {
		if (unlikely(dst > dst_max))
			goto err_out;
		*dst++ = l & 0xffll;
		l >>= 8;
		i++;
	} while (l != 0 && l != -1);
	j = (n >> 8 * (i - 1)) & 0xff;
	/* If the sign bit is wrong, we need an extra byte. */
	if (n < 0 && j >= 0) {
		if (unlikely(dst > dst_max))
			goto err_out;
		i++;
		*dst = (s8)-1;
	} else if (n > 0 && j < 0) {
		if (unlikely(dst > dst_max))
			goto err_out;
		i++;
		*dst = (s8)0;
	}
	return i;
err_out:
	return -ENOSPC;
}

/*
 * ntfs_mapping_pairs_build - build the mapping pairs array from a runlist
 * @vol: ntfs volume
 * @dst: destination buffer to build mapping pairs array into
 * @dst_len: size of @dst in bytes
 * @rl: runlist to build the mapping pairs array from
 * @first_vcn: first vcn which to include in the mapping pairs array
 * @last_vcn: last vcn which to include in the mapping pairs array
 * @stop_vcn: on return, set to the first vcn outside the destination buffer
 * @stop_rl: on return, set to the runlist element where encoding stopped
 * @de_cluster_count: on return, set to the number of clusters encoded
 *
 * Create the mapping pairs array from the locked runlist @rl, starting at vcn
 * @first_vcn and finishing with vcn @last_vcn and save the array in @dst.
 * @dst_len is the size of @dst in bytes and it should be at least equal to the
 * value obtained by calling ntfs_get_size_for_mapping_pairs().
 *
 * A @last_vcn of -1 means end of runlist and in that case the mapping pairs
 * array corresponding to the runlist starting at vcn @first_vcn and finishing
 * at the end of the runlist is created.
 *
 * If @rl is NULL, just write a single terminator byte to @dst.
 *
 * On success or -ENOSPC error, if @stop_vcn is not NULL, *@stop_vcn is set to
 * the first vcn outside the destination buffer.  Note that on error, @dst has
 * been filled with all the mapping pairs that will fit, thus it can be treated
 * as partial success, in that a new attribute extent needs to be created or
 * the next extent has to be used and the mapping pairs build has to be
 * continued with @first_vcn set to *@stop_vcn.
 *
 * Return 0 on success and -errno on error.  The following error codes are
 * defined:
 *	-EINVAL	- Run list contains unmapped elements.  Make sure to only pass
 *		  fully mapped runlists to this function.
 *	-EIO	- The runlist is corrupt.
 *	-ENOSPC	- The destination buffer is too small.
 *
 * Locking: @rl must be locked on entry (either for reading or writing), it
 *	    remains locked throughout, and is left locked upon return.
 */
int ntfs_mapping_pairs_build(const struct ntfs_volume *vol, s8 *dst,
		const int dst_len, const struct runlist_element *rl,
		const s64 first_vcn, const s64 last_vcn, s64 *const stop_vcn,
		struct runlist_element **stop_rl, unsigned int *de_cluster_count)
{
	s64 prev_lcn;
	s8 *dst_max, *dst_next;
	int err = -ENOSPC;
	bool the_end = false;
	s8 len_len, lcn_len;
	unsigned int de_cnt = 0;

	if (first_vcn < 0 || last_vcn < -1 || dst_len < 1)
		return -EINVAL;
	if (last_vcn >= 0 && first_vcn > last_vcn)
		return -EINVAL;

	if (!rl) {
		WARN_ON(first_vcn || last_vcn > 0);
		if (stop_vcn)
			*stop_vcn = 0;
		/* Terminator byte. */
		*dst = 0;
		return 0;
	}
	/* Skip to runlist element containing @first_vcn. */
	while (rl->length && first_vcn >= rl[1].vcn)
		rl++;
	if (unlikely((!rl->length && first_vcn > rl->vcn) ||
			first_vcn < rl->vcn))
		return -EINVAL;
	/*
	 * @dst_max is used for bounds checking in
	 * ntfs_write_significant_bytes().
	 */
	dst_max = dst + dst_len - 1;
	prev_lcn = 0;
	/* Do the first partial run if present. */
	if (first_vcn > rl->vcn) {
		s64 delta, length = rl->length;

		/* We know rl->length != 0 already. */
		if (unlikely(length < 0 || rl->lcn < LCN_HOLE))
			goto err_out;
		/*
		 * If @stop_vcn is given and finishes inside this run, cap the
		 * run length.
		 */
		if (unlikely(last_vcn >= 0 && rl[1].vcn > last_vcn)) {
			s64 s1 = last_vcn + 1;

			if (unlikely(rl[1].vcn > s1))
				length = s1 - rl->vcn;
			the_end = true;
		}
		delta = first_vcn - rl->vcn;
		/* Write length. */
		len_len = ntfs_write_significant_bytes(dst + 1, dst_max,
				length - delta);
		if (unlikely(len_len < 0))
			goto size_err;
		/*
		 * If the logical cluster number (lcn) denotes a hole and we
		 * are on NTFS 3.0+, we don't store it at all, i.e. we need
		 * zero space.  On earlier NTFS versions we just write the lcn
		 * change.
		 */
		if (likely(rl->lcn >= 0 || vol->major_ver < 3)) {
			prev_lcn = rl->lcn;
			if (likely(rl->lcn >= 0))
				prev_lcn += delta;
			/* Write change in lcn. */
			lcn_len = ntfs_write_significant_bytes(dst + 1 +
					len_len, dst_max, prev_lcn);
			if (unlikely(lcn_len < 0))
				goto size_err;
		} else
			lcn_len = 0;
		dst_next = dst + len_len + lcn_len + 1;
		if (unlikely(dst_next > dst_max))
			goto size_err;
		/* Update header byte. */
		*dst = lcn_len << 4 | len_len;
		/* Position at next mapping pairs array element. */
		dst = dst_next;
		/* Go to next runlist element. */
		rl++;
	}
	/* Do the full runs. */
	for (; rl->length && !the_end; rl++) {
		s64 length = rl->length;

		if (unlikely(length < 0 || rl->lcn < LCN_HOLE))
			goto err_out;
		/*
		 * If @stop_vcn is given and finishes inside this run, cap the
		 * run length.
		 */
		if (unlikely(last_vcn >= 0 && rl[1].vcn > last_vcn)) {
			s64 s1 = last_vcn + 1;

			if (unlikely(rl[1].vcn > s1))
				length = s1 - rl->vcn;
			the_end = true;
		}
		/* Write length. */
		len_len = ntfs_write_significant_bytes(dst + 1, dst_max,
				length);
		if (unlikely(len_len < 0))
			goto size_err;
		/*
		 * If the logical cluster number (lcn) denotes a hole and we
		 * are on NTFS 3.0+, we don't store it at all, i.e. we need
		 * zero space.  On earlier NTFS versions we just write the lcn
		 * change.
		 */
		if (likely(rl->lcn >= 0 || vol->major_ver < 3)) {
			/* Write change in lcn. */
			lcn_len = ntfs_write_significant_bytes(dst + 1 +
					len_len, dst_max, rl->lcn - prev_lcn);
			if (unlikely(lcn_len < 0))
				goto size_err;
			prev_lcn = rl->lcn;
		} else {
			if (rl->lcn == LCN_DELALLOC)
				de_cnt += rl->length;
			lcn_len = 0;
		}
		dst_next = dst + len_len + lcn_len + 1;
		if (unlikely(dst_next > dst_max))
			goto size_err;
		/* Update header byte. */
		*dst = lcn_len << 4 | len_len;
		/* Position at next mapping pairs array element. */
		dst = dst_next;
	}
	/* Success. */
	if (de_cluster_count)
		*de_cluster_count = de_cnt;
	err = 0;
size_err:
	/* Set stop vcn. */
	if (stop_vcn)
		*stop_vcn = rl->vcn;
	if (stop_rl)
		*stop_rl = (struct runlist_element *)rl;
	/* Add terminator byte. */
	*dst = 0;
	return err;
err_out:
	if (rl->lcn == LCN_RL_NOT_MAPPED)
		err = -EINVAL;
	else
		err = -EIO;
	return err;
}

/*
 * ntfs_rl_truncate_nolock - truncate a runlist starting at a specified vcn
 * @vol:	ntfs volume (needed for error output)
 * @runlist:	runlist to truncate
 * @new_length:	the new length of the runlist in VCNs
 *
 * Truncate the runlist described by @runlist as well as the memory buffer
 * holding the runlist elements to a length of @new_length VCNs.
 *
 * If @new_length lies within the runlist, the runlist elements with VCNs of
 * @new_length and above are discarded.  As a special case if @new_length is
 * zero, the runlist is discarded and set to NULL.
 *
 * If @new_length lies beyond the runlist, a sparse runlist element is added to
 * the end of the runlist @runlist or if the last runlist element is a sparse
 * one already, this is extended.
 *
 * Note, no checking is done for unmapped runlist elements.  It is assumed that
 * the caller has mapped any elements that need to be mapped already.
 *
 * Return 0 on success and -errno on error.
 *
 * Locking: The caller must hold @runlist->lock for writing.
 */
int ntfs_rl_truncate_nolock(const struct ntfs_volume *vol, struct runlist *const runlist,
		const s64 new_length)
{
	struct runlist_element *rl;
	int old_size;

	ntfs_debug("Entering for new_length 0x%llx.", (long long)new_length);

	if (!runlist || new_length < 0)
		return -EINVAL;

	rl = runlist->rl;
	if (new_length < rl->vcn)
		return -EINVAL;

	/* Find @new_length in the runlist. */
	while (likely(rl->length && new_length >= rl[1].vcn))
		rl++;
	/*
	 * If not at the end of the runlist we need to shrink it.
	 * If at the end of the runlist we need to expand it.
	 */
	if (rl->length) {
		struct runlist_element *trl;
		bool is_end;

		ntfs_debug("Shrinking runlist.");
		/* Determine the runlist size. */
		trl = rl + 1;
		while (likely(trl->length))
			trl++;
		old_size = trl - runlist->rl + 1;
		/* Truncate the run. */
		rl->length = new_length - rl->vcn;
		/*
		 * If a run was partially truncated, make the following runlist
		 * element a terminator.
		 */
		is_end = false;
		if (rl->length) {
			rl++;
			if (!rl->length)
				is_end = true;
			rl->vcn = new_length;
			rl->length = 0;
		}
		rl->lcn = LCN_ENOENT;
		runlist->count = rl - runlist->rl + 1;
		/* Reallocate memory if necessary. */
		if (!is_end) {
			int new_size = rl - runlist->rl + 1;

			rl = ntfs_rl_realloc(runlist->rl, old_size, new_size);
			if (IS_ERR(rl))
				ntfs_warning(vol->sb,
					"Failed to shrink runlist buffer.  This just wastes a bit of memory temporarily so we ignore it and return success.");
			else
				runlist->rl = rl;
		}
	} else if (likely(/* !rl->length && */ new_length > rl->vcn)) {
		ntfs_debug("Expanding runlist.");
		/*
		 * If there is a previous runlist element and it is a sparse
		 * one, extend it.  Otherwise need to add a new, sparse runlist
		 * element.
		 */
		if ((rl > runlist->rl) && ((rl - 1)->lcn == LCN_HOLE))
			(rl - 1)->length = new_length - (rl - 1)->vcn;
		else {
			/* Determine the runlist size. */
			old_size = rl - runlist->rl + 1;
			/* Reallocate memory if necessary. */
			rl = ntfs_rl_realloc(runlist->rl, old_size,
					old_size + 1);
			if (IS_ERR(rl)) {
				ntfs_error(vol->sb, "Failed to expand runlist buffer, aborting.");
				return PTR_ERR(rl);
			}
			runlist->rl = rl;
			/*
			 * Set @rl to the same runlist element in the new
			 * runlist as before in the old runlist.
			 */
			rl += old_size - 1;
			/* Add a new, sparse runlist element. */
			rl->lcn = LCN_HOLE;
			rl->length = new_length - rl->vcn;
			/* Add a new terminator runlist element. */
			rl++;
			rl->length = 0;
			runlist->count = old_size + 1;
		}
		rl->vcn = new_length;
		rl->lcn = LCN_ENOENT;
	} else /* if (unlikely(!rl->length && new_length == rl->vcn)) */ {
		/* Runlist already has same size as requested. */
		rl->lcn = LCN_ENOENT;
	}
	ntfs_debug("Done.");
	return 0;
}

/*
 * ntfs_rl_sparse - check whether runlist have sparse regions or not.
 * @rl:         runlist to check
 *
 * Return 1 if have, 0 if not, -errno on error.
 */
int ntfs_rl_sparse(struct runlist_element *rl)
{
	struct runlist_element *rlc;

	if (!rl)
		return -EINVAL;

	for (rlc = rl; rlc->length; rlc++)
		if (rlc->lcn < 0) {
			if (rlc->lcn != LCN_HOLE && rlc->lcn != LCN_DELALLOC) {
				pr_err("%s: bad runlist\n", __func__);
				return -EINVAL;
			}
			return 1;
		}
	return 0;
}

/*
 * ntfs_rl_get_compressed_size - calculate length of non sparse regions
 * @vol:        ntfs volume (need for cluster size)
 * @rl:         runlist to calculate for
 *
 * Return compressed size or -errno on error.
 */
s64 ntfs_rl_get_compressed_size(struct ntfs_volume *vol, struct runlist_element *rl)
{
	struct runlist_element *rlc;
	s64 ret = 0;

	if (!rl)
		return -EINVAL;

	for (rlc = rl; rlc->length; rlc++) {
		if (rlc->lcn < 0) {
			if (rlc->lcn != LCN_HOLE && rlc->lcn != LCN_DELALLOC) {
				ntfs_error(vol->sb, "%s: bad runlist, rlc->lcn : %lld",
						__func__, rlc->lcn);
				return -EINVAL;
			}
		} else
			ret += rlc->length;
	}
	return NTFS_CLU_TO_B(vol, ret);
}

static inline bool ntfs_rle_lcn_contiguous(struct runlist_element *left_rle,
					   struct runlist_element *right_rle)
{
	if (left_rle->lcn > LCN_HOLE &&
	    left_rle->lcn + left_rle->length == right_rle->lcn)
		return true;
	else if (left_rle->lcn == LCN_HOLE && right_rle->lcn == LCN_HOLE)
		return true;
	else
		return false;
}

static inline bool ntfs_rle_contain(struct runlist_element *rle, s64 vcn)
{
	if (rle->length > 0 &&
	    vcn >= rle->vcn && vcn < rle->vcn + rle->length)
		return true;
	else
		return false;
}

struct runlist_element *ntfs_rl_insert_range(struct runlist_element *dst_rl, int dst_cnt,
				      struct runlist_element *src_rl, int src_cnt,
				      size_t *new_rl_cnt)
{
	struct runlist_element *i_rl, *new_rl, *src_rl_origin = src_rl;
	struct runlist_element dst_rl_split;
	s64 start_vcn;
	int new_1st_cnt, new_2nd_cnt, new_3rd_cnt, new_cnt;

	if (!dst_rl || !src_rl || !new_rl_cnt)
		return ERR_PTR(-EINVAL);
	if (dst_cnt <= 0 || src_cnt <= 0)
		return ERR_PTR(-EINVAL);
	if (!(dst_rl[dst_cnt - 1].lcn == LCN_ENOENT &&
	      dst_rl[dst_cnt - 1].length == 0) ||
	    src_rl[src_cnt - 1].lcn < LCN_HOLE)
		return ERR_PTR(-EINVAL);

	start_vcn = src_rl[0].vcn;

	i_rl = ntfs_rl_find_vcn_nolock(dst_rl, start_vcn);
	if (!i_rl ||
	    (i_rl->lcn == LCN_ENOENT && i_rl->vcn != start_vcn) ||
	    (i_rl->lcn != LCN_ENOENT && !ntfs_rle_contain(i_rl, start_vcn)))
		return ERR_PTR(-EINVAL);

	new_1st_cnt = (int)(i_rl - dst_rl);
	if (new_1st_cnt > dst_cnt)
		return ERR_PTR(-EINVAL);
	new_3rd_cnt = dst_cnt - new_1st_cnt;
	if (new_3rd_cnt < 1)
		return ERR_PTR(-EINVAL);

	if (i_rl[0].vcn != start_vcn) {
		if (i_rl[0].lcn == LCN_HOLE && src_rl[0].lcn == LCN_HOLE)
			goto merge_src_rle;

		/* split @i_rl[0] and create @dst_rl_split */
		dst_rl_split.vcn = i_rl[0].vcn;
		dst_rl_split.length = start_vcn - i_rl[0].vcn;
		dst_rl_split.lcn = i_rl[0].lcn;

		i_rl[0].vcn = start_vcn;
		i_rl[0].length -= dst_rl_split.length;
		i_rl[0].lcn += dst_rl_split.length;
	} else {
		struct runlist_element *dst_rle, *src_rle;
merge_src_rle:

		/* not split @i_rl[0] */
		dst_rl_split.lcn = LCN_ENOENT;

		/* merge @src_rl's first run and @i_rl[0]'s left run if possible */
		dst_rle = &dst_rl[new_1st_cnt - 1];
		src_rle = &src_rl[0];
		if (new_1st_cnt > 0 && ntfs_rle_lcn_contiguous(dst_rle, src_rle)) {
			WARN_ON(dst_rle->vcn + dst_rle->length != src_rle->vcn);
			dst_rle->length += src_rle->length;
			src_rl++;
			src_cnt--;
		} else {
			/* merge @src_rl's last run and @i_rl[0]'s right if possible */
			dst_rle = &dst_rl[new_1st_cnt];
			src_rle = &src_rl[src_cnt - 1];

			if (ntfs_rle_lcn_contiguous(dst_rle, src_rle)) {
				dst_rle->length += src_rle->length;
				src_cnt--;
			}
		}
	}

	new_2nd_cnt = src_cnt;
	new_cnt = new_1st_cnt + new_2nd_cnt + new_3rd_cnt;
	new_cnt += dst_rl_split.lcn >= LCN_HOLE ? 1 : 0;
	new_rl = kvcalloc(new_cnt, sizeof(*new_rl), GFP_NOFS);
	if (!new_rl)
		return ERR_PTR(-ENOMEM);

	/* Copy the @dst_rl's first half to @new_rl */
	ntfs_rl_mc(new_rl, 0, dst_rl, 0, new_1st_cnt);
	if (dst_rl_split.lcn >= LCN_HOLE) {
		ntfs_rl_mc(new_rl, new_1st_cnt, &dst_rl_split, 0, 1);
		new_1st_cnt++;
	}
	/* Copy the @src_rl to @new_rl */
	ntfs_rl_mc(new_rl, new_1st_cnt, src_rl, 0, new_2nd_cnt);
	/* Copy the @dst_rl's second half to @new_rl */
	if (new_3rd_cnt >= 1) {
		struct runlist_element *rl, *rl_3rd;
		int dst_1st_cnt = dst_rl_split.lcn >= LCN_HOLE ?
			new_1st_cnt - 1 : new_1st_cnt;

		ntfs_rl_mc(new_rl, new_1st_cnt + new_2nd_cnt,
			   dst_rl, dst_1st_cnt, new_3rd_cnt);
		/* Update vcn of the @dst_rl's second half runs to reflect
		 * appended @src_rl.
		 */
		if (new_1st_cnt + new_2nd_cnt == 0) {
			rl_3rd = &new_rl[new_1st_cnt + new_2nd_cnt + 1];
			rl = &new_rl[new_1st_cnt + new_2nd_cnt];
		} else {
			rl_3rd = &new_rl[new_1st_cnt + new_2nd_cnt];
			rl = &new_rl[new_1st_cnt + new_2nd_cnt - 1];
		}
		do {
			rl_3rd->vcn = rl->vcn + rl->length;
			if (rl_3rd->length <= 0)
				break;
			rl = rl_3rd;
			rl_3rd++;
		} while (1);
	}
	*new_rl_cnt = new_1st_cnt + new_2nd_cnt + new_3rd_cnt;

	kvfree(dst_rl);
	kvfree(src_rl_origin);
	return new_rl;
}

struct runlist_element *ntfs_rl_punch_hole(struct runlist_element *dst_rl, int dst_cnt,
				    s64 start_vcn, s64 len,
				    struct runlist_element **punch_rl,
				    size_t *new_rl_cnt)
{
	struct runlist_element *s_rl, *e_rl, *new_rl, *dst_3rd_rl, hole_rl[1];
	s64 end_vcn;
	int new_1st_cnt, dst_3rd_cnt, new_cnt, punch_cnt, merge_cnt;
	bool begin_split, end_split, one_split_3;

	if (dst_cnt < 2 ||
	    !(dst_rl[dst_cnt - 1].lcn == LCN_ENOENT &&
	      dst_rl[dst_cnt - 1].length == 0))
		return ERR_PTR(-EINVAL);

	end_vcn = min(start_vcn + len - 1,
		      dst_rl[dst_cnt - 2].vcn + dst_rl[dst_cnt - 2].length - 1);

	s_rl = ntfs_rl_find_vcn_nolock(dst_rl, start_vcn);
	if (!s_rl ||
	    s_rl->lcn <= LCN_ENOENT ||
	    !ntfs_rle_contain(s_rl, start_vcn))
		return ERR_PTR(-EINVAL);

	begin_split = s_rl->vcn != start_vcn ? true : false;

	e_rl = ntfs_rl_find_vcn_nolock(dst_rl, end_vcn);
	if (!e_rl ||
	    e_rl->lcn <= LCN_ENOENT ||
	    !ntfs_rle_contain(e_rl, end_vcn))
		return ERR_PTR(-EINVAL);

	end_split = e_rl->vcn + e_rl->length - 1 != end_vcn ? true : false;

	/* @s_rl has to be split into left, punched hole, and right */
	one_split_3 = e_rl == s_rl && begin_split && end_split ? true : false;

	punch_cnt = (int)(e_rl - s_rl) + 1;

	*punch_rl = kvcalloc(punch_cnt + 1, sizeof(struct runlist_element),
			GFP_NOFS);
	if (!*punch_rl)
		return ERR_PTR(-ENOMEM);

	new_cnt = dst_cnt - (int)(e_rl - s_rl + 1) + 3;
	new_rl = kvcalloc(new_cnt, sizeof(struct runlist_element), GFP_NOFS);
	if (!new_rl) {
		kvfree(*punch_rl);
		*punch_rl = NULL;
		return ERR_PTR(-ENOMEM);
	}

	new_1st_cnt = (int)(s_rl - dst_rl) + 1;
	ntfs_rl_mc(*punch_rl, 0, dst_rl, new_1st_cnt - 1, punch_cnt);

	(*punch_rl)[punch_cnt].lcn = LCN_ENOENT;
	(*punch_rl)[punch_cnt].length = 0;

	if (!begin_split)
		new_1st_cnt--;
	dst_3rd_rl = e_rl;
	dst_3rd_cnt = (int)(&dst_rl[dst_cnt - 1] - e_rl) + 1;
	if (!end_split) {
		dst_3rd_rl++;
		dst_3rd_cnt--;
	}

	/* Copy the 1st part of @dst_rl into @new_rl */
	ntfs_rl_mc(new_rl, 0, dst_rl, 0, new_1st_cnt);
	if (begin_split) {
		/* the @e_rl has to be splited and copied into the last of @new_rl
		 * and the first of @punch_rl
		 */
		s64 first_cnt = start_vcn - dst_rl[new_1st_cnt - 1].vcn;

		if (new_1st_cnt)
			new_rl[new_1st_cnt - 1].length = first_cnt;

		(*punch_rl)[0].vcn = start_vcn;
		(*punch_rl)[0].length -= first_cnt;
		if ((*punch_rl)[0].lcn > LCN_HOLE)
			(*punch_rl)[0].lcn += first_cnt;
	}

	/* Copy a hole into @new_rl */
	hole_rl[0].vcn = start_vcn;
	hole_rl[0].length = (s64)len;
	hole_rl[0].lcn = LCN_HOLE;
	ntfs_rl_mc(new_rl, new_1st_cnt, hole_rl, 0, 1);

	/* Copy the 3rd part of @dst_rl into @new_rl */
	ntfs_rl_mc(new_rl, new_1st_cnt + 1, dst_3rd_rl, 0, dst_3rd_cnt);
	if (end_split) {
		/* the @e_rl has to be splited and copied into the first of
		 * @new_rl and the last of @punch_rl
		 */
		s64 first_cnt = end_vcn - dst_3rd_rl[0].vcn + 1;

		new_rl[new_1st_cnt + 1].vcn = end_vcn + 1;
		new_rl[new_1st_cnt + 1].length -= first_cnt;
		if (new_rl[new_1st_cnt + 1].lcn > LCN_HOLE)
			new_rl[new_1st_cnt + 1].lcn += first_cnt;

		if (one_split_3)
			(*punch_rl)[punch_cnt - 1].length -=
				new_rl[new_1st_cnt + 1].length;
		else
			(*punch_rl)[punch_cnt - 1].length = first_cnt;
	}

	/* Merge left and hole, or hole and right in @new_rl, if left or right
	 * consists of holes.
	 */
	merge_cnt = 0;
	if (new_1st_cnt > 0 && new_rl[new_1st_cnt - 1].lcn == LCN_HOLE) {
		/* Merge right and hole */
		s_rl =  &new_rl[new_1st_cnt - 1];
		s_rl->length += s_rl[1].length;
		merge_cnt = 1;
		/* Merge left and right */
		if (new_1st_cnt + 1 < new_cnt &&
		    new_rl[new_1st_cnt + 1].lcn == LCN_HOLE) {
			s_rl->length += s_rl[2].length;
			merge_cnt++;
		}
	} else if (new_1st_cnt + 1 < new_cnt &&
		   new_rl[new_1st_cnt + 1].lcn == LCN_HOLE) {
		/* Merge left and hole */
		s_rl = &new_rl[new_1st_cnt];
		s_rl->length += s_rl[1].length;
		merge_cnt = 1;
	}
	if (merge_cnt) {
		struct runlist_element *d_rl, *src_rl;

		d_rl = s_rl + 1;
		src_rl = s_rl + 1 + merge_cnt;
		ntfs_rl_mm(new_rl, (int)(d_rl - new_rl), (int)(src_rl - new_rl),
			   (int)(&new_rl[new_cnt - 1] - src_rl) + 1);
	}

	(*punch_rl)[punch_cnt].vcn = (*punch_rl)[punch_cnt - 1].vcn +
		(*punch_rl)[punch_cnt - 1].length;

	/* punch_cnt elements of dst are replaced with one hole */
	*new_rl_cnt = dst_cnt - (punch_cnt - (int)begin_split - (int)end_split) +
		1 - merge_cnt;
	kvfree(dst_rl);
	return new_rl;
}

struct runlist_element *ntfs_rl_collapse_range(struct runlist_element *dst_rl, int dst_cnt,
					s64 start_vcn, s64 len,
					struct runlist_element **punch_rl,
					size_t *new_rl_cnt)
{
	struct runlist_element *s_rl, *e_rl, *new_rl, *dst_3rd_rl;
	s64 end_vcn;
	int new_1st_cnt, dst_3rd_cnt, new_cnt, punch_cnt, merge_cnt, i;
	bool begin_split, end_split, one_split_3;

	if (dst_cnt < 2 ||
	    !(dst_rl[dst_cnt - 1].lcn == LCN_ENOENT &&
	      dst_rl[dst_cnt - 1].length == 0))
		return ERR_PTR(-EINVAL);

	end_vcn = min(start_vcn + len - 1,
			dst_rl[dst_cnt - 1].vcn - 1);

	s_rl = ntfs_rl_find_vcn_nolock(dst_rl, start_vcn);
	if (!s_rl ||
	    s_rl->lcn <= LCN_ENOENT ||
	    !ntfs_rle_contain(s_rl, start_vcn))
		return ERR_PTR(-EINVAL);

	begin_split = s_rl->vcn != start_vcn ? true : false;

	e_rl = ntfs_rl_find_vcn_nolock(dst_rl, end_vcn);
	if (!e_rl ||
	    e_rl->lcn <= LCN_ENOENT ||
	    !ntfs_rle_contain(e_rl, end_vcn))
		return ERR_PTR(-EINVAL);

	end_split = e_rl->vcn + e_rl->length - 1 != end_vcn ? true : false;

	/* @s_rl has to be split into left, collapsed, and right */
	one_split_3 = e_rl == s_rl && begin_split && end_split ? true : false;

	punch_cnt = (int)(e_rl - s_rl) + 1;
	*punch_rl = kvcalloc(punch_cnt + 1, sizeof(struct runlist_element),
			GFP_NOFS);
	if (!*punch_rl)
		return ERR_PTR(-ENOMEM);

	new_cnt = dst_cnt - (int)(e_rl - s_rl + 1) + 3;
	new_rl = kvcalloc(new_cnt, sizeof(struct runlist_element), GFP_NOFS);
	if (!new_rl) {
		kvfree(*punch_rl);
		*punch_rl = NULL;
		return ERR_PTR(-ENOMEM);
	}

	new_1st_cnt = (int)(s_rl - dst_rl) + 1;
	ntfs_rl_mc(*punch_rl, 0, dst_rl, new_1st_cnt - 1, punch_cnt);
	(*punch_rl)[punch_cnt].lcn = LCN_ENOENT;
	(*punch_rl)[punch_cnt].length = 0;

	if (!begin_split)
		new_1st_cnt--;
	dst_3rd_rl = e_rl;
	dst_3rd_cnt = (int)(&dst_rl[dst_cnt - 1] - e_rl) + 1;
	if (!end_split) {
		dst_3rd_rl++;
		dst_3rd_cnt--;
	}

	/* Copy the 1st part of @dst_rl into @new_rl */
	ntfs_rl_mc(new_rl, 0, dst_rl, 0, new_1st_cnt);
	if (begin_split) {
		/* the @e_rl has to be splited and copied into the last of @new_rl
		 * and the first of @punch_rl
		 */
		s64 first_cnt = start_vcn - dst_rl[new_1st_cnt - 1].vcn;

		new_rl[new_1st_cnt - 1].length = first_cnt;

		(*punch_rl)[0].vcn = start_vcn;
		(*punch_rl)[0].length -= first_cnt;
		if ((*punch_rl)[0].lcn > LCN_HOLE)
			(*punch_rl)[0].lcn += first_cnt;
	}

	/* Copy the 3rd part of @dst_rl into @new_rl */
	ntfs_rl_mc(new_rl, new_1st_cnt, dst_3rd_rl, 0, dst_3rd_cnt);
	if (end_split) {
		/* the @e_rl has to be splited and copied into the first of
		 * @new_rl and the last of @punch_rl
		 */
		s64 first_cnt = end_vcn - dst_3rd_rl[0].vcn + 1;

		new_rl[new_1st_cnt].vcn = end_vcn + 1;
		new_rl[new_1st_cnt].length -= first_cnt;
		if (new_rl[new_1st_cnt].lcn > LCN_HOLE)
			new_rl[new_1st_cnt].lcn += first_cnt;

		if (one_split_3)
			(*punch_rl)[punch_cnt - 1].length -=
				new_rl[new_1st_cnt].length;
		else
			(*punch_rl)[punch_cnt - 1].length = first_cnt;
	}

	/* Adjust vcn */
	if (new_1st_cnt == 0)
		new_rl[new_1st_cnt].vcn = 0;
	for (i = new_1st_cnt == 0 ? 1 : new_1st_cnt; new_rl[i].length; i++)
		new_rl[i].vcn = new_rl[i - 1].vcn + new_rl[i - 1].length;
	new_rl[i].vcn = new_rl[i - 1].vcn + new_rl[i - 1].length;

	/* Merge left and hole, or hole and right in @new_rl, if left or right
	 * consists of holes.
	 */
	merge_cnt = 0;
	i = new_1st_cnt == 0 ? 1 : new_1st_cnt;
	if (ntfs_rle_lcn_contiguous(&new_rl[i - 1], &new_rl[i])) {
		/* Merge right and left */
		s_rl =  &new_rl[new_1st_cnt - 1];
		s_rl->length += s_rl[1].length;
		merge_cnt = 1;
	}
	if (merge_cnt) {
		struct runlist_element *d_rl, *src_rl;

		d_rl = s_rl + 1;
		src_rl = s_rl + 1 + merge_cnt;
		ntfs_rl_mm(new_rl, (int)(d_rl - new_rl), (int)(src_rl - new_rl),
			   (int)(&new_rl[new_cnt - 1] - src_rl) + 1);
	}

	(*punch_rl)[punch_cnt].vcn = (*punch_rl)[punch_cnt - 1].vcn +
		(*punch_rl)[punch_cnt - 1].length;

	/* punch_cnt elements of dst are extracted */
	*new_rl_cnt = dst_cnt - (punch_cnt - (int)begin_split - (int)end_split) -
		merge_cnt;

	kvfree(dst_rl);
	return new_rl;
}
