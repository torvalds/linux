/* SPDX-License-Identifier: GPL-2.0-or-later */
/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * heartbeat.h
 *
 * Function prototypes
 *
 * Copyright (C) 2002, 2004 Oracle.  All rights reserved.
 */

#ifndef OCFS2_HEARTBEAT_H
#define OCFS2_HEARTBEAT_H

void ocfs2_init_node_maps(struct ocfs2_super *osb);

void ocfs2_do_node_down(int node_num, void *data);

/* node map functions - used to keep track of mounted and in-recovery
 * nodes. */
void ocfs2_node_map_set_bit(struct ocfs2_super *osb,
			    struct ocfs2_node_map *map,
			    int bit);
void ocfs2_node_map_clear_bit(struct ocfs2_super *osb,
			      struct ocfs2_node_map *map,
			      int bit);
int ocfs2_node_map_test_bit(struct ocfs2_super *osb,
			    struct ocfs2_node_map *map,
			    int bit);

#endif /* OCFS2_HEARTBEAT_H */
