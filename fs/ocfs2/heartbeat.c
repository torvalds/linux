/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * heartbeat.c
 *
 * Register ourselves with the heartbaet service, keep our node maps
 * up to date, and fire off recovery when needed.
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

#include <linux/fs.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/highmem.h>
#include <linux/kmod.h>

#include <dlm/dlmapi.h>

#define MLOG_MASK_PREFIX ML_SUPER
#include <cluster/masklog.h>

#include "ocfs2.h"

#include "alloc.h"
#include "heartbeat.h"
#include "inode.h"
#include "journal.h"

#include "buffer_head_io.h"

static inline void __ocfs2_node_map_set_bit(struct ocfs2_node_map *map,
					    int bit);
static inline void __ocfs2_node_map_clear_bit(struct ocfs2_node_map *map,
					      int bit);
static inline int __ocfs2_node_map_is_empty(struct ocfs2_node_map *map);

/* special case -1 for now
 * TODO: should *really* make sure the calling func never passes -1!!  */
static void ocfs2_node_map_init(struct ocfs2_node_map *map)
{
	map->num_nodes = OCFS2_NODE_MAP_MAX_NODES;
	memset(map->map, 0, BITS_TO_LONGS(OCFS2_NODE_MAP_MAX_NODES) *
	       sizeof(unsigned long));
}

void ocfs2_init_node_maps(struct ocfs2_super *osb)
{
	spin_lock_init(&osb->node_map_lock);
	ocfs2_node_map_init(&osb->recovery_map);
	ocfs2_node_map_init(&osb->osb_recovering_orphan_dirs);
}

static void ocfs2_do_node_down(int node_num,
			       struct ocfs2_super *osb)
{
	BUG_ON(osb->node_num == node_num);

	mlog(0, "ocfs2: node down event for %d\n", node_num);

	if (!osb->dlm) {
		/*
		 * No DLM means we're not even ready to participate yet.
		 * We check the slots after the DLM comes up, so we will
		 * notice the node death then.  We can safely ignore it
		 * here.
		 */
		return;
	}

	ocfs2_recovery_thread(osb, node_num);
}

/* Called from the dlm when it's about to evict a node. We may also
 * get a heartbeat callback later. */
static void ocfs2_dlm_eviction_cb(int node_num,
				  void *data)
{
	struct ocfs2_super *osb = (struct ocfs2_super *) data;
	struct super_block *sb = osb->sb;

	mlog(ML_NOTICE, "device (%u,%u): dlm has evicted node %d\n",
	     MAJOR(sb->s_dev), MINOR(sb->s_dev), node_num);

	ocfs2_do_node_down(node_num, osb);
}

void ocfs2_setup_hb_callbacks(struct ocfs2_super *osb)
{
	/* Not exactly a heartbeat callback, but leads to essentially
	 * the same path so we set it up here. */
	dlm_setup_eviction_cb(&osb->osb_eviction_cb,
			      ocfs2_dlm_eviction_cb,
			      osb);
}

void ocfs2_stop_heartbeat(struct ocfs2_super *osb)
{
	int ret;
	char *argv[5], *envp[3];

	if (ocfs2_mount_local(osb))
		return;

	if (!osb->uuid_str) {
		/* This can happen if we don't get far enough in mount... */
		mlog(0, "No UUID with which to stop heartbeat!\n\n");
		return;
	}

	argv[0] = (char *)o2nm_get_hb_ctl_path();
	argv[1] = "-K";
	argv[2] = "-u";
	argv[3] = osb->uuid_str;
	argv[4] = NULL;

	mlog(0, "Run: %s %s %s %s\n", argv[0], argv[1], argv[2], argv[3]);

	/* minimal command environment taken from cpu_run_sbin_hotplug */
	envp[0] = "HOME=/";
	envp[1] = "PATH=/sbin:/bin:/usr/sbin:/usr/bin";
	envp[2] = NULL;

	ret = call_usermodehelper(argv[0], argv, envp, UMH_WAIT_PROC);
	if (ret < 0)
		mlog_errno(ret);
}

static inline void __ocfs2_node_map_set_bit(struct ocfs2_node_map *map,
					    int bit)
{
	set_bit(bit, map->map);
}

void ocfs2_node_map_set_bit(struct ocfs2_super *osb,
			    struct ocfs2_node_map *map,
			    int bit)
{
	if (bit==-1)
		return;
	BUG_ON(bit >= map->num_nodes);
	spin_lock(&osb->node_map_lock);
	__ocfs2_node_map_set_bit(map, bit);
	spin_unlock(&osb->node_map_lock);
}

static inline void __ocfs2_node_map_clear_bit(struct ocfs2_node_map *map,
					      int bit)
{
	clear_bit(bit, map->map);
}

void ocfs2_node_map_clear_bit(struct ocfs2_super *osb,
			      struct ocfs2_node_map *map,
			      int bit)
{
	if (bit==-1)
		return;
	BUG_ON(bit >= map->num_nodes);
	spin_lock(&osb->node_map_lock);
	__ocfs2_node_map_clear_bit(map, bit);
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

static inline int __ocfs2_node_map_is_empty(struct ocfs2_node_map *map)
{
	int bit;
	bit = find_next_bit(map->map, map->num_nodes, 0);
	if (bit < map->num_nodes)
		return 0;
	return 1;
}

int ocfs2_node_map_is_empty(struct ocfs2_super *osb,
			    struct ocfs2_node_map *map)
{
	int ret;
	BUG_ON(map->num_nodes == 0);
	spin_lock(&osb->node_map_lock);
	ret = __ocfs2_node_map_is_empty(map);
	spin_unlock(&osb->node_map_lock);
	return ret;
}

#if 0

static void __ocfs2_node_map_dup(struct ocfs2_node_map *target,
				 struct ocfs2_node_map *from)
{
	BUG_ON(from->num_nodes == 0);
	ocfs2_node_map_init(target);
	__ocfs2_node_map_set(target, from);
}

/* returns 1 if bit is the only bit set in target, 0 otherwise */
int ocfs2_node_map_is_only(struct ocfs2_super *osb,
			   struct ocfs2_node_map *target,
			   int bit)
{
	struct ocfs2_node_map temp;
	int ret;

	spin_lock(&osb->node_map_lock);
	__ocfs2_node_map_dup(&temp, target);
	__ocfs2_node_map_clear_bit(&temp, bit);
	ret = __ocfs2_node_map_is_empty(&temp);
	spin_unlock(&osb->node_map_lock);

	return ret;
}

static void __ocfs2_node_map_set(struct ocfs2_node_map *target,
				 struct ocfs2_node_map *from)
{
	int num_longs, i;

	BUG_ON(target->num_nodes != from->num_nodes);
	BUG_ON(target->num_nodes == 0);

	num_longs = BITS_TO_LONGS(target->num_nodes);
	for (i = 0; i < num_longs; i++)
		target->map[i] = from->map[i];
}

#endif  /*  0  */

/* Returns whether the recovery bit was actually set - it may not be
 * if a node is still marked as needing recovery */
int ocfs2_recovery_map_set(struct ocfs2_super *osb,
			   int num)
{
	int set = 0;

	spin_lock(&osb->node_map_lock);

	if (!test_bit(num, osb->recovery_map.map)) {
	    __ocfs2_node_map_set_bit(&osb->recovery_map, num);
	    set = 1;
	}

	spin_unlock(&osb->node_map_lock);

	return set;
}

void ocfs2_recovery_map_clear(struct ocfs2_super *osb,
			      int num)
{
	ocfs2_node_map_clear_bit(osb, &osb->recovery_map, num);
}

int ocfs2_node_map_iterate(struct ocfs2_super *osb,
			   struct ocfs2_node_map *map,
			   int idx)
{
	int i = idx;

	idx = O2NM_INVALID_NODE_NUM;
	spin_lock(&osb->node_map_lock);
	if ((i != O2NM_INVALID_NODE_NUM) &&
	    (i >= 0) &&
	    (i < map->num_nodes)) {
		while(i < map->num_nodes) {
			if (test_bit(i, map->map)) {
				idx = i;
				break;
			}
			i++;
		}
	}
	spin_unlock(&osb->node_map_lock);
	return idx;
}
