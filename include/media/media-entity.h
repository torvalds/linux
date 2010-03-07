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

#define MEDIA_ENT_TYPE_SHIFT		16
#define MEDIA_ENT_TYPE_MASK		0x00ff0000
#define MEDIA_ENT_SUBTYPE_MASK		0x0000ffff

#define MEDIA_ENT_T_DEVNODE		(1 << MEDIA_ENT_TYPE_SHIFT)
#define MEDIA_ENT_T_DEVNODE_V4L		(MEDIA_ENT_T_DEVNODE + 1)
#define MEDIA_ENT_T_DEVNODE_FB		(MEDIA_ENT_T_DEVNODE + 2)
#define MEDIA_ENT_T_DEVNODE_ALSA	(MEDIA_ENT_T_DEVNODE + 3)
#define MEDIA_ENT_T_DEVNODE_DVB		(MEDIA_ENT_T_DEVNODE + 4)

#define MEDIA_ENT_T_V4L2_SUBDEV		(2 << MEDIA_ENT_TYPE_SHIFT)
#define MEDIA_ENT_T_V4L2_SUBDEV_SENSOR	(MEDIA_ENT_T_V4L2_SUBDEV + 1)
#define MEDIA_ENT_T_V4L2_SUBDEV_FLASH	(MEDIA_ENT_T_V4L2_SUBDEV + 2)
#define MEDIA_ENT_T_V4L2_SUBDEV_LENS	(MEDIA_ENT_T_V4L2_SUBDEV + 3)

#define MEDIA_ENT_FL_DEFAULT		(1 << 0)

#define MEDIA_LNK_FL_ENABLED		(1 << 0)
#define MEDIA_LNK_FL_IMMUTABLE		(1 << 1)

#define MEDIA_PAD_FL_SINK		(1 << 0)
#define MEDIA_PAD_FL_SOURCE		(1 << 1)

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
	};
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

void media_entity_graph_walk_start(struct media_entity_graph *graph,
		struct media_entity *entity);
struct media_entity *
media_entity_graph_walk_next(struct media_entity_graph *graph);

#endif
