/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * heartbeat.h
 *
 * Function prototypes
 *
 * Copyright (C) 2002, 2004 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 */

#ifndef OCFS2_HEARTBEAT_H
#define OCFS2_HEARTBEAT_H

void ocfs2_init_node_maps(struct ocfs2_super *osb);

void ocfs2_setup_hb_callbacks(struct ocfs2_super *osb);
void ocfs2_stop_heartbeat(struct ocfs2_super *osb);

/* node map functions - used to keep track of mounted and in-recovery
 * nodes. */
int ocfs2_node_map_is_empty(struct ocfs2_super *osb,
			    struct ocfs2_node_map *map);
void ocfs2_node_map_set_bit(struct ocfs2_super *osb,
			    struct ocfs2_node_map *map,
			    int bit);
void ocfs2_node_map_clear_bit(struct ocfs2_super *osb,
			      struct ocfs2_node_map *map,
			      int bit);
int ocfs2_node_map_test_bit(struct ocfs2_super *osb,
			    struct ocfs2_node_map *map,
			    int bit);
int ocfs2_node_map_iterate(struct ocfs2_super *osb,
			   struct ocfs2_node_map *map,
			   int idx);
static inline int ocfs2_node_map_first_set_bit(struct ocfs2_super *osb,
					       struct ocfs2_node_map *map)
{
	return ocfs2_node_map_iterate(osb, map, 0);
}
int ocfs2_recovery_map_set(struct ocfs2_super *osb,
			   int num);
void ocfs2_recovery_map_clear(struct ocfs2_super *osb,
			      int num);

#endif /* OCFS2_HEARTBEAT_H */
