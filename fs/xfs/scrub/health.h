// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2019-2023 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#ifndef __XFS_SCRUB_HEALTH_H__
#define __XFS_SCRUB_HEALTH_H__

unsigned int xchk_health_mask_for_scrub_type(__u32 scrub_type);
void xchk_update_health(struct xfs_scrub *sc);
void xchk_ag_btree_del_cursor_if_sick(struct xfs_scrub *sc,
		struct xfs_btree_cur **curp, unsigned int sm_type);
void xchk_mark_healthy_if_clean(struct xfs_scrub *sc, unsigned int mask);
bool xchk_file_looks_zapped(struct xfs_scrub *sc, unsigned int mask);
int xchk_health_record(struct xfs_scrub *sc);

#endif /* __XFS_SCRUB_HEALTH_H__ */
