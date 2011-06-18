/*
 * v4l2-event.h
 *
 * V4L2 events.
 *
 * Copyright (C) 2009--2010 Nokia Corporation.
 *
 * Contact: Sakari Ailus <sakari.ailus@maxwell.research.nokia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#ifndef V4L2_EVENT_H
#define V4L2_EVENT_H

#include <linux/types.h>
#include <linux/videodev2.h>
#include <linux/wait.h>

struct v4l2_fh;
struct v4l2_subscribed_event;
struct video_device;

struct v4l2_kevent {
	/* list node for the v4l2_fh->available list */
	struct list_head	list;
	/* pointer to parent v4l2_subscribed_event */
	struct v4l2_subscribed_event *sev;
	/* event itself */
	struct v4l2_event	event;
};

struct v4l2_subscribed_event {
	/* list node for the v4l2_fh->subscribed list */
	struct list_head	list;
	/* event type */
	u32			type;
	/* associated object ID (e.g. control ID) */
	u32			id;
	/* copy of v4l2_event_subscription->flags */
	u32			flags;
	/* filehandle that subscribed to this event */
	struct v4l2_fh		*fh;
	/* list node that hooks into the object's event list (if there is one) */
	struct list_head	node;
	/* Optional callback that can replace event 'old' with event 'new'. */
	void			(*replace)(struct v4l2_event *old,
					   const struct v4l2_event *new);
	/* Optional callback that can merge event 'old' into event 'new'. */
	void			(*merge)(const struct v4l2_event *old,
					 struct v4l2_event *new);
	/* the number of elements in the events array */
	unsigned		elems;
	/* the index of the events containing the oldest available event */
	unsigned		first;
	/* the number of queued events */
	unsigned		in_use;
	/* an array of elems events */
	struct v4l2_kevent	events[];
};

int v4l2_event_dequeue(struct v4l2_fh *fh, struct v4l2_event *event,
		       int nonblocking);
void v4l2_event_queue(struct video_device *vdev, const struct v4l2_event *ev);
void v4l2_event_queue_fh(struct v4l2_fh *fh, const struct v4l2_event *ev);
int v4l2_event_pending(struct v4l2_fh *fh);
int v4l2_event_subscribe(struct v4l2_fh *fh,
			 struct v4l2_event_subscription *sub, unsigned elems);
int v4l2_event_unsubscribe(struct v4l2_fh *fh,
			   struct v4l2_event_subscription *sub);
void v4l2_event_unsubscribe_all(struct v4l2_fh *fh);

#endif /* V4L2_EVENT_H */
