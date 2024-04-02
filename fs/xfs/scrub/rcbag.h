// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2022-2024 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#ifndef __XFS_SCRUB_RCBAG_H__
#define __XFS_SCRUB_RCBAG_H__

struct xfs_mount;
struct rcbag;
struct xfs_buftarg;

int rcbag_init(struct xfs_mount *mp, struct xfs_buftarg *btp,
		struct rcbag **bagp);
void rcbag_free(struct rcbag **bagp);
int rcbag_add(struct rcbag *bag, struct xfs_trans *tp,
		const struct xfs_rmap_irec *rmap);
uint64_t rcbag_count(const struct rcbag *bag);

int rcbag_next_edge(struct rcbag *bag, struct xfs_trans *tp,
		const struct xfs_rmap_irec *next_rmap, bool next_valid,
		uint32_t *next_bnop);
int rcbag_remove_ending_at(struct rcbag *bag, struct xfs_trans *tp,
		uint32_t next_bno);

void rcbag_dump(struct rcbag *bag, struct xfs_trans *tp);

#endif /* __XFS_SCRUB_RCBAG_H__ */
