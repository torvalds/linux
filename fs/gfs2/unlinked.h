/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2005 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License v.2.
 */

#ifndef __UNLINKED_DOT_H__
#define __UNLINKED_DOT_H__

int gfs2_unlinked_get(struct gfs2_sbd *sdp, struct gfs2_unlinked **ul);
void gfs2_unlinked_put(struct gfs2_sbd *sdp, struct gfs2_unlinked *ul);

int gfs2_unlinked_ondisk_add(struct gfs2_sbd *sdp, struct gfs2_unlinked *ul);
int gfs2_unlinked_ondisk_munge(struct gfs2_sbd *sdp, struct gfs2_unlinked *ul);
int gfs2_unlinked_ondisk_rm(struct gfs2_sbd *sdp, struct gfs2_unlinked *ul);

int gfs2_unlinked_dealloc(struct gfs2_sbd *sdp);

int gfs2_unlinked_init(struct gfs2_sbd *sdp);
void gfs2_unlinked_cleanup(struct gfs2_sbd *sdp);

#endif /* __UNLINKED_DOT_H__ */
