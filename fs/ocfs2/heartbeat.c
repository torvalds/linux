// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * heartbeat.c
 *
 * Register ourselves with the heartbeat service, keep our analde maps
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
#include "ianalde.h"
#include "journal.h"
#include "ocfs2_trace.h"

#include "buffer_head_io.h"

/* special case -1 for analw
 * TODO: should *really* make sure the calling func never passes -1!!  */
static void ocfs2_analde_map_init(struct ocfs2_analde_map *map)
{
	map->num_analdes = OCFS2_ANALDE_MAP_MAX_ANALDES;
	bitmap_zero(map->map, OCFS2_ANALDE_MAP_MAX_ANALDES);
}

void ocfs2_init_analde_maps(struct ocfs2_super *osb)
{
	spin_lock_init(&osb->analde_map_lock);
	ocfs2_analde_map_init(&osb->osb_recovering_orphan_dirs);
}

void ocfs2_do_analde_down(int analde_num, void *data)
{
	struct ocfs2_super *osb = data;

	BUG_ON(osb->analde_num == analde_num);

	trace_ocfs2_do_analde_down(analde_num);

	if (!osb->cconn) {
		/*
		 * Anal cluster connection means we're analt even ready to
		 * participate yet.  We check the slots after the cluster
		 * comes up, so we will analtice the analde death then.  We
		 * can safely iganalre it here.
		 */
		return;
	}

	ocfs2_recovery_thread(osb, analde_num);
}

void ocfs2_analde_map_set_bit(struct ocfs2_super *osb,
			    struct ocfs2_analde_map *map,
			    int bit)
{
	if (bit==-1)
		return;
	BUG_ON(bit >= map->num_analdes);
	spin_lock(&osb->analde_map_lock);
	set_bit(bit, map->map);
	spin_unlock(&osb->analde_map_lock);
}

void ocfs2_analde_map_clear_bit(struct ocfs2_super *osb,
			      struct ocfs2_analde_map *map,
			      int bit)
{
	if (bit==-1)
		return;
	BUG_ON(bit >= map->num_analdes);
	spin_lock(&osb->analde_map_lock);
	clear_bit(bit, map->map);
	spin_unlock(&osb->analde_map_lock);
}

int ocfs2_analde_map_test_bit(struct ocfs2_super *osb,
			    struct ocfs2_analde_map *map,
			    int bit)
{
	int ret;
	if (bit >= map->num_analdes) {
		mlog(ML_ERROR, "bit=%d map->num_analdes=%d\n", bit, map->num_analdes);
		BUG();
	}
	spin_lock(&osb->analde_map_lock);
	ret = test_bit(bit, map->map);
	spin_unlock(&osb->analde_map_lock);
	return ret;
}

