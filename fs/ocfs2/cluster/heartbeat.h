/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * heartbeat.h
 *
 * Function prototypes
 *
 * Copyright (C) 2004 Oracle.  All rights reserved.
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
 *
 */

#ifndef O2CLUSTER_HEARTBEAT_H
#define O2CLUSTER_HEARTBEAT_H

#include "ocfs2_heartbeat.h"

#define O2HB_REGION_TIMEOUT_MS		2000

#define O2HB_MAX_REGION_NAME_LEN	32

/* number of changes to be seen as live */
#define O2HB_LIVE_THRESHOLD	   2
/* number of equal samples to be seen as dead */
extern unsigned int o2hb_dead_threshold;
#define O2HB_DEFAULT_DEAD_THRESHOLD	   31
/* Otherwise MAX_WRITE_TIMEOUT will be zero... */
#define O2HB_MIN_DEAD_THRESHOLD	  2
#define O2HB_MAX_WRITE_TIMEOUT_MS (O2HB_REGION_TIMEOUT_MS * (o2hb_dead_threshold - 1))

#define O2HB_CB_MAGIC		0x51d1e4ec

/* callback stuff */
enum o2hb_callback_type {
	O2HB_NODE_DOWN_CB = 0,
	O2HB_NODE_UP_CB,
	O2HB_NUM_CB
};

struct o2nm_node;
typedef void (o2hb_cb_func)(struct o2nm_node *, int, void *);

struct o2hb_callback_func {
	u32			hc_magic;
	struct list_head	hc_item;
	o2hb_cb_func		*hc_func;
	void			*hc_data;
	int			hc_priority;
	enum o2hb_callback_type hc_type;
};

struct config_group *o2hb_alloc_hb_set(void);
void o2hb_free_hb_set(struct config_group *group);

void o2hb_setup_callback(struct o2hb_callback_func *hc,
			 enum o2hb_callback_type type,
			 o2hb_cb_func *func,
			 void *data,
			 int priority);
int o2hb_register_callback(const char *region_uuid,
			   struct o2hb_callback_func *hc);
void o2hb_unregister_callback(const char *region_uuid,
			      struct o2hb_callback_func *hc);
void o2hb_fill_node_map(unsigned long *map,
			unsigned bytes);
void o2hb_exit(void);
int o2hb_init(void);
int o2hb_check_node_heartbeating_no_sem(u8 node_num);
int o2hb_check_node_heartbeating_from_callback(u8 node_num);
void o2hb_stop_all_regions(void);
int o2hb_get_all_regions(char *region_uuids, u8 numregions);
int o2hb_global_heartbeat_active(void);

#endif /* O2CLUSTER_HEARTBEAT_H */
