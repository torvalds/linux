/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2018 Red Hat, Inc.
 * All rights reserved.
 */

#ifndef __LIBXFS_AG_H
#define __LIBXFS_AG_H 1

struct xfs_mount;
struct xfs_trans;

struct aghdr_init_data {
	/* per ag data */
	xfs_agblock_t		agno;		/* ag to init */
	xfs_extlen_t		agsize;		/* new AG size */
	struct list_head	buffer_list;	/* buffer writeback list */
	xfs_rfsblock_t		nfree;		/* cumulative new free space */

	/* per header data */
	xfs_daddr_t		daddr;		/* header location */
	size_t			numblks;	/* size of header */
	xfs_btnum_t		type;		/* type of btree root block */
};

int xfs_ag_init_headers(struct xfs_mount *mp, struct aghdr_init_data *id);
int xfs_ag_extend_space(struct xfs_mount *mp, struct xfs_trans *tp,
			struct aghdr_init_data *id, xfs_extlen_t len);
int xfs_ag_get_geometry(struct xfs_mount *mp, xfs_agnumber_t agno,
			struct xfs_ag_geometry *ageo);

#endif /* __LIBXFS_AG_H */
