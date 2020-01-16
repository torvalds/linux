// SPDX-License-Identifier: GPL-2.0-or-later
/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: yesexpandtab sw=8 ts=8 sts=0:
 *
 * heartbeat.c
 *
 * Register ourselves with the heartbaet service, keep our yesde maps
 * up to date, and fire off recovery when needed.
 *
 * Copyright (C) 2002, 2004 Oracle.  All rights reserved.
 */

#include <linux/fs.h>
#include <linux/types.h>
#include <linux/highmem.h>

#include <cluster/masklog.h>

#include "ocfs2.h"

#include "alloc.h"
#include "heartbeat.h"
#include "iyesde.h"
#include "journal.h"
#include "ocfs2_trace.h"

#include "buffer_head_io.h"

static inline void __ocfs2_yesde_map_set_bit(struct ocfs2_yesde_map *map,
					    int bit);
static inline void __ocfs2_yesde_map_clear_bit(struct ocfs2_yesde_map *map,
					      int bit);

/* special case -1 for yesw
 * TODO: should *really* make sure the calling func never passes -1!!  */
static void ocfs2_yesde_map_init(struct ocfs2_yesde_map *map)
{
	map->num_yesdes = OCFS2_NODE_MAP_MAX_NODES;
	memset(map->map, 0, BITS_TO_LONGS(OCFS2_NODE_MAP_MAX_NODES) *
	       sizeof(unsigned long));
}

void ocfs2_init_yesde_maps(struct ocfs2_super *osb)
{
	spin_lock_init(&osb->yesde_map_lock);
	ocfs2_yesde_map_init(&osb->osb_recovering_orphan_dirs);
}

void ocfs2_do_yesde_down(int yesde_num, void *data)
{
	struct ocfs2_super *osb = data;

	BUG_ON(osb->yesde_num == yesde_num);

	trace_ocfs2_do_yesde_down(yesde_num);

	if (!osb->cconn) {
		/*
		 * No cluster connection means we're yest even ready to
		 * participate yet.  We check the slots after the cluster
		 * comes up, so we will yestice the yesde death then.  We
		 * can safely igyesre it here.
		 */
		return;
	}

	ocfs2_recovery_thread(osb, yesde_num);
}

static inline void __ocfs2_yesde_map_set_bit(struct ocfs2_yesde_map *map,
					    int bit)
{
	set_bit(bit, map->map);
}

void ocfs2_yesde_map_set_bit(struct ocfs2_super *osb,
			    struct ocfs2_yesde_map *map,
			    int bit)
{
	if (bit==-1)
		return;
	BUG_ON(bit >= map->num_yesdes);
	spin_lock(&osb->yesde_map_lock);
	__ocfs2_yesde_map_set_bit(map, bit);
	spin_unlock(&osb->yesde_map_lock);
}

static inline void __ocfs2_yesde_map_clear_bit(struct ocfs2_yesde_map *map,
					      int bit)
{
	clear_bit(bit, map->map);
}

void ocfs2_yesde_map_clear_bit(struct ocfs2_super *osb,
			      struct ocfs2_yesde_map *map,
			      int bit)
{
	if (bit==-1)
		return;
	BUG_ON(bit >= map->num_yesdes);
	spin_lock(&osb->yesde_map_lock);
	__ocfs2_yesde_map_clear_bit(map, bit);
	spin_unlock(&osb->yesde_map_lock);
}

int ocfs2_yesde_map_test_bit(struct ocfs2_super *osb,
			    struct ocfs2_yesde_map *map,
			    int bit)
{
	int ret;
	if (bit >= map->num_yesdes) {
		mlog(ML_ERROR, "bit=%d map->num_yesdes=%d\n", bit, map->num_yesdes);
		BUG();
	}
	spin_lock(&osb->yesde_map_lock);
	ret = test_bit(bit, map->map);
	spin_unlock(&osb->yesde_map_lock);
	return ret;
}

