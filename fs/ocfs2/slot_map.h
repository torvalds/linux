/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * slotmap.h
 *
 * description here
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


#ifndef SLOTMAP_H
#define SLOTMAP_H

int ocfs2_init_slot_info(struct ocfs2_super *osb);
void ocfs2_free_slot_info(struct ocfs2_super *osb);

int ocfs2_find_slot(struct ocfs2_super *osb);
void ocfs2_put_slot(struct ocfs2_super *osb);

int ocfs2_refresh_slot_info(struct ocfs2_super *osb);

int ocfs2_node_num_to_slot(struct ocfs2_super *osb, unsigned int node_num);
int ocfs2_slot_to_node_num_locked(struct ocfs2_super *osb, int slot_num,
				  unsigned int *node_num);

int ocfs2_clear_slot(struct ocfs2_super *osb, int slot_num);

#endif
