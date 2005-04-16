/*
 * runlist.h - Defines for runlist handling in NTFS Linux kernel driver.
 *	       Part of the Linux-NTFS project.
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

#ifndef _LINUX_NTFS_RUNLIST_H
#define _LINUX_NTFS_RUNLIST_H

#include "types.h"
#include "layout.h"
#include "volume.h"

/**
 * runlist_element - in memory vcn to lcn mapping array element
 * @vcn:	starting vcn of the current array element
 * @lcn:	starting lcn of the current array element
 * @length:	length in clusters of the current array element
 *
 * The last vcn (in fact the last vcn + 1) is reached when length == 0.
 *
 * When lcn == -1 this means that the count vcns starting at vcn are not
 * physically allocated (i.e. this is a hole / data is sparse).
 */
typedef struct {	/* In memory vcn to lcn mapping structure element. */
	VCN vcn;	/* vcn = Starting virtual cluster number. */
	LCN lcn;	/* lcn = Starting logical cluster number. */
	s64 length;	/* Run length in clusters. */
} runlist_element;

/**
 * runlist - in memory vcn to lcn mapping array including a read/write lock
 * @rl:		pointer to an array of runlist elements
 * @lock:	read/write spinlock for serializing access to @rl
 *
 */
typedef struct {
	runlist_element *rl;
	struct rw_semaphore lock;
} runlist;

static inline void ntfs_init_runlist(runlist *rl)
{
	rl->rl = NULL;
	init_rwsem(&rl->lock);
}

typedef enum {
	LCN_HOLE		= -1,	/* Keep this as highest value or die! */
	LCN_RL_NOT_MAPPED	= -2,
	LCN_ENOENT		= -3,
} LCN_SPECIAL_VALUES;

extern runlist_element *ntfs_runlists_merge(runlist_element *drl,
		runlist_element *srl);

extern runlist_element *ntfs_mapping_pairs_decompress(const ntfs_volume *vol,
		const ATTR_RECORD *attr, runlist_element *old_rl);

extern LCN ntfs_rl_vcn_to_lcn(const runlist_element *rl, const VCN vcn);

extern int ntfs_get_size_for_mapping_pairs(const ntfs_volume *vol,
		const runlist_element *rl, const VCN start_vcn);

extern int ntfs_mapping_pairs_build(const ntfs_volume *vol, s8 *dst,
		const int dst_len, const runlist_element *rl,
		const VCN start_vcn, VCN *const stop_vcn);

extern int ntfs_rl_truncate_nolock(const ntfs_volume *vol,
		runlist *const runlist, const s64 new_length);

#endif /* _LINUX_NTFS_RUNLIST_H */
