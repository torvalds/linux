/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2005 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License v.2.
 */

#ifndef __LVB_DOT_H__
#define __LVB_DOT_H__

#define GFS2_MIN_LVB_SIZE 32

struct gfs2_quota_lvb {
	uint32_t qb_magic;
	uint32_t __pad;
	uint64_t qb_limit;      /* Hard limit of # blocks to alloc */
	uint64_t qb_warn;       /* Warn user when alloc is above this # */
	int64_t qb_value;       /* Current # blocks allocated */
};

void gfs2_quota_lvb_in(struct gfs2_quota_lvb *qb, char *lvb);
void gfs2_quota_lvb_out(struct gfs2_quota_lvb *qb, char *lvb);
void gfs2_quota_lvb_print(struct gfs2_quota_lvb *qb);

#endif /* __LVB_DOT_H__ */

