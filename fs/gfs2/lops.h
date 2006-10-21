/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2006 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License version 2.
 */

#ifndef __LOPS_DOT_H__
#define __LOPS_DOT_H__

#include <linux/list.h>
#include "incore.h"

extern const struct gfs2_log_operations gfs2_glock_lops;
extern const struct gfs2_log_operations gfs2_buf_lops;
extern const struct gfs2_log_operations gfs2_revoke_lops;
extern const struct gfs2_log_operations gfs2_rg_lops;
extern const struct gfs2_log_operations gfs2_databuf_lops;

extern const struct gfs2_log_operations *gfs2_log_ops[];

static inline void lops_init_le(struct gfs2_log_element *le,
				const struct gfs2_log_operations *lops)
{
	INIT_LIST_HEAD(&le->le_list);
	le->le_ops = lops;
}

static inline void lops_add(struct gfs2_sbd *sdp, struct gfs2_log_element *le)
{
	if (le->le_ops->lo_add)
		le->le_ops->lo_add(sdp, le);
}

static inline void lops_incore_commit(struct gfs2_sbd *sdp,
				      struct gfs2_trans *tr)
{
	int x;
	for (x = 0; gfs2_log_ops[x]; x++)
		if (gfs2_log_ops[x]->lo_incore_commit)
			gfs2_log_ops[x]->lo_incore_commit(sdp, tr);
}

static inline void lops_before_commit(struct gfs2_sbd *sdp)
{
	int x;
	for (x = 0; gfs2_log_ops[x]; x++)
		if (gfs2_log_ops[x]->lo_before_commit)
			gfs2_log_ops[x]->lo_before_commit(sdp);
}

static inline void lops_after_commit(struct gfs2_sbd *sdp, struct gfs2_ail *ai)
{
	int x;
	for (x = 0; gfs2_log_ops[x]; x++)
		if (gfs2_log_ops[x]->lo_after_commit)
			gfs2_log_ops[x]->lo_after_commit(sdp, ai);
}

static inline void lops_before_scan(struct gfs2_jdesc *jd,
				    struct gfs2_log_header *head,
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

