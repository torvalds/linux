/* SPDX-License-Identifier: GPL-2.0-or-later */
/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: yesexpandtab sw=8 ts=8 sts=0:
 *
 * heartbeat.h
 *
 * Function prototypes
 *
 * Copyright (C) 2002, 2004 Oracle.  All rights reserved.
 */

#ifndef OCFS2_HEARTBEAT_H
#define OCFS2_HEARTBEAT_H

void ocfs2_init_yesde_maps(struct ocfs2_super *osb);

void ocfs2_do_yesde_down(int yesde_num, void *data);

/* yesde map functions - used to keep track of mounted and in-recovery
 * yesdes. */
void ocfs2_yesde_map_set_bit(struct ocfs2_super *osb,
			    struct ocfs2_yesde_map *map,
			    int bit);
void ocfs2_yesde_map_clear_bit(struct ocfs2_super *osb,
			      struct ocfs2_yesde_map *map,
			      int bit);
int ocfs2_yesde_map_test_bit(struct ocfs2_super *osb,
			    struct ocfs2_yesde_map *map,
			    int bit);

#endif /* OCFS2_HEARTBEAT_H */
