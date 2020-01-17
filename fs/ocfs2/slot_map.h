/* SPDX-License-Identifier: GPL-2.0-or-later */
/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: yesexpandtab sw=8 ts=8 sts=0:
 *
 * slotmap.h
 *
 * description here
 *
 * Copyright (C) 2002, 2004 Oracle.  All rights reserved.
 */


#ifndef SLOTMAP_H
#define SLOTMAP_H

int ocfs2_init_slot_info(struct ocfs2_super *osb);
void ocfs2_free_slot_info(struct ocfs2_super *osb);

int ocfs2_find_slot(struct ocfs2_super *osb);
void ocfs2_put_slot(struct ocfs2_super *osb);

int ocfs2_refresh_slot_info(struct ocfs2_super *osb);

int ocfs2_yesde_num_to_slot(struct ocfs2_super *osb, unsigned int yesde_num);
int ocfs2_slot_to_yesde_num_locked(struct ocfs2_super *osb, int slot_num,
				  unsigned int *yesde_num);

int ocfs2_clear_slot(struct ocfs2_super *osb, int slot_num);

#endif
