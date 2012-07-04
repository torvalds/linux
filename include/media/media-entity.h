/*
 * Media entity
 *
 * Copyright (C) 2010 Nokia Corporation
 *
 * Contacts: Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 *	     Sakari Ailus <sakari.ailus@iki.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef _MEDIA_ENTITY_H
#define _MEDIA_ENTITY_H

#include <linux/list.h>
#include <linux/media.h>

struct media_pipeline {
};

struct media_link {
	struct media_pad *source;	/* Source pad */
	struct media_pad *sink;		/* Sink pad  */
	struct media_link *reverse;	/* Link in the reverse direction */
	unsigned long flags;		/* Link flags (MEDIA_LNK_FL_*) */
};

struct media_pad {
	struct media_entity *entity;	/* Entity this pad belongs to */
	u16 index;			/* Pad index in the entity pads array */
	unsigned long flags;		/* Pad flags (MEDIA_PAD_FL_*) */
};

struct media_entity_operations {
	int (*link_setup)(struct media_entity *entity,
			  const struct media_pad *local,
			  const struct media_pad *remote, u32 flags);
	int (*link_validate)(struct media_link *link);
};

struct media_entity {
	struct list_head list;
	struct media_device *parent;	/* Media device this entity belongs to*/
	u32 id;				/* Entity ID, unique in the parent media
					 * device context */
	const char *name;		/* Entity name */
	u32 type;			/* Entity type (MEDIA_ENT_T_*) */
	u32 revision;			/* Entity revision, driver specific */
	unsigned long flags;		/* Entity flags (MEDIA_ENT_FL_*) */
	u32 group_id;			/* Entity group ID */

	u16 num_pads;			/* Number of sink and source pads */
	u16 num_links;			/* Number of existing links, both
					 * enabled and disabled */
	u16 num_backlinks;		/* Number of backlinks */
	u16 max_links;			/* Maximum number of links */

	struct media_pad *pads;		/* Pads array (num_pads elements) */
	struct media_link *links;	/* Links array (max_links elements)*/

	const struct media_entity_operations *ops;	/* Entity operations */

	/* Reference counts must never be negative, but are signed integers on
	 * purpose: a simple WARN_ON(<0) check can be used to detect reference
	 * count bugs that would make them negative.
	 */
	int stream_count;		/* Stream count for the entity. */
	int use_count;			/* Use count for the entity. */

	struct media_pipeline *pipe;	/* Pipeline this entity belongs to. */

	union {
		/* Node specifications */
		struct {
			u32 major;
			u32 minor;
		} v4l;
		struct {
			u32 major;
			u32 minor;
		} fb;
		struct {
			u32 card;
			u32 device;
			u32 subdevice;
		} alsa;
		int dvb;

		/* Sub-device specifications */
		/* Nothing needed yet */
	} info;
};

static inline u32 media_entity_type(struct media_entity *entity)
{
	return entity->type & MEDIA_ENT_TYPE_MASK;
}

static inline u32 media_entity_subtype(struct media_entity *entity)
{
	return entity->type & MEDIA_ENT_SUBTYPE_MASK;
}

#define MEDIA_ENTITY_ENUM_MAX_DEPTH	16

struct media_entity_graph {
	struct {
		struct media_entity *entity;
		int link;
	} stack[MEDIA_ENTITY_ENUM_MAX_DEPTH];
	int top;
};

int media_entity_init(struct media_entity *entity, u16 num_pads,
		struct media_pad *pads, u16 extra_links);
void media_entity_cleanup(struct media_entity *entity);

int media_entity_create_link(struct media_entity *source, u16 source_pad,
		struct media_entity *sink, u16 sink_pad, u32 flags);
int __media_entity_setup_link(struct media_link *link, u32 flags);
int media_entity_setup_link(struct media_link *link, u32 flags);
struct media_link *media_entity_find_link(struct media_pad *source,
		struct media_pad *sink);
struct media_pad *media_entity_remote_source(struct media_pad *pad);

struct media_entity *media_entity_get(struct media_entity *entity);
void media_entity_put(struct media_entity *entity);

void media_entity_graph_walk_start(struct media_entity_graph *graph,
		struct media_entity *entity);
struct media_entity *
media_entity_graph_walk_next(struct media_entity_graph *graph);
__must_check int media_entity_pipeline_start(struct media_entity *entity,
					     struct media_pipeline *pipe);
void media_entity_pipeline_stop(struct media_entity *entity);

#define media_entity_call(entity, operation, args...)			\
	(((entity)->ops && (entity)->ops->operation) ?			\
	 (entity)->ops->operation((entity) , ##args) : -ENOIOCTLCMD)

#endif
