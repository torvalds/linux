/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Defines for runlist handling in NTFS Linux kernel driver.
 *
 * Copyright (c) 2001-2005 Anton Altaparmakov
 * Copyright (c) 2002 Richard Russon
 * Copyright (c) 2025 LG Electronics Co., Ltd.
 */

#ifndef _LINUX_NTFS_RUNLIST_H
#define _LINUX_NTFS_RUNLIST_H

#include "volume.h"

/*
 * runlist_element - in memory vcn to lcn mapping array element
 * @vcn:	starting vcn of the current array element
 * @lcn:	starting lcn of the current array element
 * @length:	length in clusters of the current array element
 *
 * The last vcn (in fact the last vcn + 1) is reached when length == 0.
 *
 * When lcn == -1 this means that the count vcns starting at vcn are not
 * physically allocated (i.e. this is a hole / data is sparse).
 *
 * In memory vcn to lcn mapping structure element.
 * @vcn: vcn = Starting virtual cluster number.
 * @lcn: lcn = Starting logical cluster number.
 * @length: Run length in clusters.
 */
struct runlist_element {
	s64 vcn;
	s64 lcn;
	s64 length;
};

/*
 * runlist - in memory vcn to lcn mapping array including a read/write lock
 * @rl:		pointer to an array of runlist elements
 * @lock:	read/write spinlock for serializing access to @rl
 * @rl_hint:	hint/cache pointing to the last accessed runlist element
 */
struct runlist {
	struct runlist_element *rl;
	struct rw_semaphore lock;
	size_t count;
	int rl_hint;
};

static inline void ntfs_init_runlist(struct runlist *rl)
{
	rl->rl = NULL;
	init_rwsem(&rl->lock);
	rl->count = 0;
	rl->rl_hint = -1;
}

enum {
	LCN_DELALLOC		= -1,
	LCN_HOLE		= -2,
	LCN_RL_NOT_MAPPED	= -3,
	LCN_ENOENT		= -4,
	LCN_ENOMEM		= -5,
	LCN_EIO			= -6,
	LCN_EINVAL		= -7,
};

struct runlist_element *ntfs_runlists_merge(struct runlist *d_runlist,
		struct runlist_element *srl, size_t s_rl_count,
		size_t *new_rl_count);
struct runlist_element *ntfs_mapping_pairs_decompress(const struct ntfs_volume *vol,
		const struct attr_record *attr, struct runlist *old_runlist,
		size_t *new_rl_count);
s64 ntfs_rl_vcn_to_lcn(const struct runlist_element *rl, const s64 vcn);
struct runlist_element *ntfs_rl_find_vcn_nolock(struct runlist_element *rl, const s64 vcn);
int ntfs_get_size_for_mapping_pairs(const struct ntfs_volume *vol,
		const struct runlist_element *rl, const s64 first_vcn,
		const s64 last_vcn, int max_mp_size);
int ntfs_mapping_pairs_build(const struct ntfs_volume *vol, s8 *dst,
		const int dst_len, const struct runlist_element *rl,
		const s64 first_vcn, const s64 last_vcn, s64 *const stop_vcn,
		struct runlist_element **stop_rl, unsigned int *de_cluster_count);
int ntfs_rl_truncate_nolock(const struct ntfs_volume *vol,
		struct runlist *const runlist, const s64 new_length);
int ntfs_rl_sparse(struct runlist_element *rl);
s64 ntfs_rl_get_compressed_size(struct ntfs_volume *vol, struct runlist_element *rl);
struct runlist_element *ntfs_rl_insert_range(struct runlist_element *dst_rl, int dst_cnt,
		struct runlist_element *src_rl, int src_cnt, size_t *new_cnt);
struct runlist_element *ntfs_rl_punch_hole(struct runlist_element *dst_rl, int dst_cnt,
		s64 start_vcn, s64 len, struct runlist_element **punch_rl,
		size_t *new_rl_cnt);
struct runlist_element *ntfs_rl_collapse_range(struct runlist_element *dst_rl, int dst_cnt,
		s64 start_vcn, s64 len, struct runlist_element **punch_rl,
		size_t *new_rl_cnt);
struct runlist_element *ntfs_rl_realloc(struct runlist_element *rl, int old_size,
		int new_size);
#endif /* _LINUX_NTFS_RUNLIST_H */
