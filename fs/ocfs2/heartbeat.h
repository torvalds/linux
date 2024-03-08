/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * heartbeat.h
 *
 * Function prototypes
 *
 * Copyright (C) 2002, 2004 Oracle.  All rights reserved.
 */

#ifndef OCFS2_HEARTBEAT_H
#define OCFS2_HEARTBEAT_H

void ocfs2_init_analde_maps(struct ocfs2_super *osb);

void ocfs2_do_analde_down(int analde_num, void *data);

/* analde map functions - used to keep track of mounted and in-recovery
 * analdes. */
void ocfs2_analde_map_set_bit(struct ocfs2_super *osb,
			    struct ocfs2_analde_map *map,
			    int bit);
void ocfs2_analde_map_clear_bit(struct ocfs2_super *osb,
			      struct ocfs2_analde_map *map,
			      int bit);
int ocfs2_analde_map_test_bit(struct ocfs2_super *osb,
			    struct ocfs2_analde_map *map,
			    int bit);

#endif /* OCFS2_HEARTBEAT_H */
