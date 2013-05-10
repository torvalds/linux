/*
 * lcnalloc.h - Exports for NTFS kernel cluster (de)allocation.  Part of the
 *		Linux-NTFS project.
 *
 * Copyright (c) 2004-2005 Anton Altaparmakov
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

#ifndef _LINUX_NTFS_LCNALLOC_H
#define _LINUX_NTFS_LCNALLOC_H

#ifdef NTFS_RW

#include <linux/fs.h>

#include "attrib.h"
#include "types.h"
#include "inode.h"
#include "runlist.h"
#include "volume.h"

typedef enum {
	FIRST_ZONE	= 0,	/* For sanity checking. */
	MFT_ZONE	= 0,	/* Allocate from $MFT zone. */
	DATA_ZONE	= 1,	/* Allocate from $DATA zone. */
	LAST_ZONE	= 1,	/* For sanity checking. */
} NTFS_CLUSTER_ALLOCATION_ZONES;

extern runlist_element *ntfs_cluster_alloc(ntfs_volume *vol,
		const VCN start_vcn, const s64 count, const LCN start_lcn,
		const NTFS_CLUSTER_ALLOCATION_ZONES zone,
		const bool is_extension);

extern s64 __ntfs_cluster_free(ntfs_inode *ni, const VCN start_vcn,
		s64 count, ntfs_attr_search_ctx *ctx, const bool is_rollback);

/**
 * ntfs_cluster_free - free clusters on an ntfs volume
 * @ni:		ntfs inode whose runlist describes the clusters to free
 * @start_vcn:	vcn in the runlist of @ni at which to start freeing clusters
 * @count:	number of clusters to free or -1 for all clusters
 * @ctx:	active attribute search context if present or NULL if not
 *
 * Free @count clusters starting at the cluster @start_vcn in the runlist
 * described by the ntfs inode @ni.
 *
 * If @count is -1, all clusters from @start_vcn to the end of the runlist are
 * deallocated.  Thus, to completely free all clusters in a runlist, use
 * @start_vcn = 0 and @count = -1.
 *
 * If @ctx is specified, it is an active search context of @ni and its base mft
 * record.  This is needed when ntfs_cluster_free() encounters unmapped runlist
 * fragments and allows their mapping.  If you do not have the mft record
 * mapped, you can specify @ctx as NULL and ntfs_cluster_free() will perform
 * the necessary mapping and unmapping.
 *
 * Note, ntfs_cluster_free() saves the state of @ctx on entry and restores it
 * before returning.  Thus, @ctx will be left pointing to the same attribute on
 * return as on entry.  However, the actual pointers in @ctx may point to
 * different memory locations on return, so you must remember to reset any
 * cached pointers from the @ctx, i.e. after the call to ntfs_cluster_free(),
 * you will probably want to do:
 *	m = ctx->mrec;
 *	a = ctx->attr;
 * Assuming you cache ctx->attr in a variable @a of type ATTR_RECORD * and that
 * you cache ctx->mrec in a variable @m of type MFT_RECORD *.
 *
 * Note, ntfs_cluster_free() does not modify the runlist, so you have to remove
 * from the runlist or mark sparse the freed runs later.
 *
 * Return the number of deallocated clusters (not counting sparse ones) on
 * success and -errno on error.
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
 *	    - The volume lcn bitmap must be unlocked on entry and is unlocked
 *	      on return.
 *	    - This function takes the volume lcn bitmap lock for writing and
 *	      modifies the bitmap contents.
 *	    - If @ctx is NULL, the base mft record of @ni must not be mapped on
 *	      entry and it will be left unmapped on return.
 *	    - If @ctx is not NULL, the base mft record must be mapped on entry
 *	      and it will be left mapped on return.
 */
static inline s64 ntfs_cluster_free(ntfs_inode *ni, const VCN start_vcn,
		s64 count, ntfs_attr_search_ctx *ctx)
{
	return __ntfs_cluster_free(ni, start_vcn, count, ctx, false);
}

extern int ntfs_cluster_free_from_rl_nolock(ntfs_volume *vol,
		const runlist_element *rl);

/**
 * ntfs_cluster_free_from_rl - free clusters from runlist
 * @vol:	mounted ntfs volume on which to free the clusters
 * @rl:		runlist describing the clusters to free
 *
 * Free all the clusters described by the runlist @rl on the volume @vol.  In
 * the case of an error being returned, at least some of the clusters were not
 * freed.
 *
 * Return 0 on success and -errno on error.
 *
 * Locking: - This function takes the volume lcn bitmap lock for writing and
 *	      modifies the bitmap contents.
 *	    - The caller must have locked the runlist @rl for reading or
 *	      writing.
 */
static inline int ntfs_cluster_free_from_rl(ntfs_volume *vol,
		const runlist_element *rl)
{
	int ret;

	down_write(&vol->lcnbmp_lock);
	ret = ntfs_cluster_free_from_rl_nolock(vol, rl);
	up_write(&vol->lcnbmp_lock);
	return ret;
}

#endif /* NTFS_RW */

#endif /* defined _LINUX_NTFS_LCNALLOC_H */
