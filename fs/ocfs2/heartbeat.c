// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * heartbeat.c
 *
 * Register ourselves with the heartbeat service, keep our node maps
 * up to date, and fire off recovery when needed.
 *
 * Copyright (C) 2002, 2004 Oracle.  All rights reserved.
 */

#include <linux/bitmap.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/highmem.h>

#include <cluster/masklog.h>

#include "ocfs2.h"

#include "alloc.h"
#include "heartbeat.h"
#include "inode.h"
#include "journal.h"
#include "ocfs2_trace.h"

#include "buffer_head_io.h"

/* special case -1 for now
 * TODO: should *really* make sure the calling func never passes -1!!  */
static void ocfs2_node_map_init(struct ocfs2_node_map *map)
{
	map->num_nodes = OCFS2_NODE_MAP_MAX_NODES;
	bitmap_zero(map->map, OCFS2_NODE_MAP_MAX_NODES);
}

void ocfs2_init_node_maps(struct ocfs2_super *osb)
{
	spin_lock_init(&osb->node_map_lock);
	ocfs2_node_map_init(&osb->osb_recovering_orphan_dirs);
}

void ocfs2_do_node_down(int node_num, void *data)
{
	struct ocfs2_super *osb = data;

	BUG_ON(osb->node_num == node_num);

	trace_ocfs2_do_node_down(node_num);

	if (!osb->cconn) {
		/*
		 * No cluster connection means we're not even ready to
		 * participate yet.  We check the slots after the cluster
		 * comes up, so we will notice the node death then.  We
		 * can safely ignore it here.
		 */
		return;
	}

	ocfs2_recovery_thread(osb, node_num);
}

void ocfs2_node_map_set_bit(struct ocfs2_super *osb,
			    struct ocfs2_node_map *map,
			    int bit)
{
	if (bit==-1)
		return;
	BUG_ON(bit >= map->num_nodes);
	spin_lock(&osb->node_map_lock);
	set_bit(bit, map->map);
	spin_unlock(&osb->node_map_lock);
}

void ocfs2_node_map_clear_bit(struct ocfs2_super *osb,
			      struct ocfs2_node_map *map,
			      int bit)
{
	if (bit==-1)
		return;
	BUG_ON(bit >= map->num_nodes);
	spin_lock(&osb->node_map_lock);
	clear_bit(bit, map->map);
	spin_unlock(&osb->node_map_lock);
}

int ocfs2_node_map_test_bit(struct ocfs2_super *osb,
			    struct ocfs2_node_map *map,
			    int bit)
{
	int ret;
	if (bit >= map->num_nodes) {
		mlog(ML_ERROR, "bit=%d map->num_nodes=%d\n", bit, map->num_nodes);
		BUG();
	}
	spin_lock(&osb->node_map_lock);
	ret = test_bit(bit, map->map);
	spin_unlock(&osb->node_map_lock);
	return ret;
}

