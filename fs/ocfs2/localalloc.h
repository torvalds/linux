/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * localalloc.h
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

#ifndef OCFS2_LOCALALLOC_H
#define OCFS2_LOCALALLOC_H

int ocfs2_load_local_alloc(struct ocfs2_super *osb);

void ocfs2_shutdown_local_alloc(struct ocfs2_super *osb);

int ocfs2_begin_local_alloc_recovery(struct ocfs2_super *osb,
				     int node_num,
				     struct ocfs2_dinode **alloc_copy);

int ocfs2_complete_local_alloc_recovery(struct ocfs2_super *osb,
					struct ocfs2_dinode *alloc);

int ocfs2_alloc_should_use_local(struct ocfs2_super *osb,
				 u64 bits);

struct ocfs2_alloc_context;
int ocfs2_reserve_local_alloc_bits(struct ocfs2_super *osb,
				   u32 bits_wanted,
				   struct ocfs2_alloc_context *ac);

int ocfs2_claim_local_alloc_bits(struct ocfs2_super *osb,
				 handle_t *handle,
				 struct ocfs2_alloc_context *ac,
				 u32 min_bits,
				 u32 *bit_off,
				 u32 *num_bits);

#endif /* OCFS2_LOCALALLOC_H */
