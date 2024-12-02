/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * localalloc.h
 *
 * Function prototypes
 *
 * Copyright (C) 2002, 2004 Oracle.  All rights reserved.
 */

#ifndef OCFS2_LOCALALLOC_H
#define OCFS2_LOCALALLOC_H

int ocfs2_load_local_alloc(struct ocfs2_super *osb);

void ocfs2_shutdown_local_alloc(struct ocfs2_super *osb);

void ocfs2_la_set_sizes(struct ocfs2_super *osb, int requested_mb);
unsigned int ocfs2_la_default_mb(struct ocfs2_super *osb);

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
				 u32 bits_wanted,
				 u32 *bit_off,
				 u32 *num_bits);

int ocfs2_free_local_alloc_bits(struct ocfs2_super *osb,
				handle_t *handle,
				struct ocfs2_alloc_context *ac,
				u32 bit_off,
				u32 num_bits);

void ocfs2_local_alloc_seen_free_bits(struct ocfs2_super *osb,
				      unsigned int num_clusters);
void ocfs2_la_enable_worker(struct work_struct *work);

#endif /* OCFS2_LOCALALLOC_H */
