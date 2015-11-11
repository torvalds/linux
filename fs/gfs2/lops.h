/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2008 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License version 2.
 */

#ifndef __LOPS_DOT_H__
#define __LOPS_DOT_H__

#include <linux/list.h>
#include "incore.h"

#define BUF_OFFSET \
	((sizeof(struct gfs2_log_descriptor) + sizeof(__be64) - 1) & \
	 ~(sizeof(__be64) - 1))
#define DATABUF_OFFSET \
	((sizeof(struct gfs2_log_descriptor) + (2 * sizeof(__be64) - 1)) & \
	 ~(2 * sizeof(__be64) - 1))

extern const struct gfs2_log_operations gfs2_glock_lops;
extern const struct gfs2_log_operations gfs2_buf_lops;
extern const struct gfs2_log_operations gfs2_revoke_lops;
extern const struct gfs2_log_operations gfs2_databuf_lops;

extern const struct gfs2_log_operations *gfs2_log_ops[];
extern void gfs2_log_write_page(struct gfs2_sbd *sdp, struct page *page);
extern void gfs2_log_flush_bio(struct gfs2_sbd *sdp, int rw);
extern void gfs2_pin(struct gfs2_sbd *sdp, struct buffer_head *bh);

static inline unsigned int buf_limit(struct gfs2_sbd *sdp)
{
	unsigned int limit;

	limit = (sdp->sd_sb.sb_bsize - BUF_OFFSET) / sizeof(__be64);
	return limit;
}

static inline unsigned int databuf_limit(struct gfs2_sbd *sdp)
{
	unsigned int limit;

	limit = (sdp->sd_sb.sb_bsize - DATABUF_OFFSET) / (2 * sizeof(__be64));
	return limit;
}

static inline void lops_before_commit(struct gfs2_sbd *sdp,
				      struct gfs2_trans *tr)
{
	int x;
	for (x = 0; gfs2_log_ops[x]; x++)
		if (gfs2_log_ops[x]->lo_before_commit)
			gfs2_log_ops[x]->lo_before_commit(sdp, tr);
}

static inline void lops_after_commit(struct gfs2_sbd *sdp,
				     struct gfs2_trans *tr)
{
	int x;
	for (x = 0; gfs2_log_ops[x]; x++)
		if (gfs2_log_ops[x]->lo_after_commit)
			gfs2_log_ops[x]->lo_after_commit(sdp, tr);
}

static inline void lops_before_scan(struct gfs2_jdesc *jd,
				    struct gfs2_log_header_host *head,
				    unsigned int pass)
{
	int x;
	for (x = 0; gfs2_log_ops[x]; x++)
		if (gfs2_log_ops[x]->lo_before_scan)
			gfs2_log_ops[x]->lo_before_scan(jd, head, pass);
}

static inline int lops_scan_elements(struct gfs2_jdesc *jd, unsigned int start,
				     struct gfs2_log_descriptor *ld,
				     __be64 *ptr,
				     unsigned int pass)
{
	int x, error;
	for (x = 0; gfs2_log_ops[x]; x++)
		if (gfs2_log_ops[x]->lo_scan_elements) {
			error = gfs2_log_ops[x]->lo_scan_elements(jd, start,
								  ld, ptr, pass);
			if (error)
				return error;
		}

	return 0;
}

static inline void lops_after_scan(struct gfs2_jdesc *jd, int error,
				   unsigned int pass)
{
	int x;
	for (x = 0; gfs2_log_ops[x]; x++)
		if (gfs2_log_ops[x]->lo_before_scan)
			gfs2_log_ops[x]->lo_after_scan(jd, error, pass);
}

#endif /* __LOPS_DOT_H__ */

