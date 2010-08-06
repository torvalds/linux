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
struct video_device;

struct v4l2_kevent {
	struct list_head	list;
	struct v4l2_event	event;
};

struct v4l2_subscribed_event {
	struct list_head	list;
	u32			type;
};

struct v4l2_events {
	wait_queue_head_t	wait;
	struct list_head	subscribed; /* Subscribed events */
	struct list_head	free; /* Events ready for use */
	struct list_head	available; /* Dequeueable event */
	unsigned int		navailable;
	unsigned int		nallocated; /* Number of allocated events */
	u32			sequence;
};

int v4l2_event_init(struct v4l2_fh *fh);
int v4l2_event_alloc(struct v4l2_fh *fh, unsigned int n);
void v4l2_event_free(struct v4l2_fh *fh);
int v4l2_event_dequeue(struct v4l2_fh *fh, struct v4l2_event *event,
		       int nonblocking);
void v4l2_event_queue(struct video_device *vdev, const struct v4l2_event *ev);
int v4l2_event_pending(struct v4l2_fh *fh);
int v4l2_event_subscribe(struct v4l2_fh *fh,
			 struct v4l2_event_subscription *sub);
int v4l2_event_unsubscribe(struct v4l2_fh *fh,
			   struct v4l2_event_subscription *sub);

#endif /* V4L2_EVENT_H */
