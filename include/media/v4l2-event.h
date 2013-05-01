/*
 * v4l2-event.h
 *
 * V4L2 events.
 *
 * Copyright (C) 2009--2010 Nokia Corporation.
 *
 * Contact: Sakari Ailus <sakari.ailus@iki.fi>
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

/*
 * Overview:
 *
 * Events are subscribed per-filehandle. An event specification consists of a
 * type and is optionally associated with an object identified through the
 * 'id' field. So an event is uniquely identified by the (type, id) tuple.
 *
 * The v4l2-fh struct has a list of subscribed events. The v4l2_subscribed_event
 * struct is added to that list, one for every subscribed event.
 *
 * Each v4l2_subscribed_event struct ends with an array of v4l2_kevent structs.
 * This array (ringbuffer, really) is used to store any events raised by the
 * driver. The v4l2_kevent struct links into the 'available' list of the
 * v4l2_fh struct so VIDIOC_DQEVENT will know which event to dequeue first.
 *
 * Finally, if the event subscription is associated with a particular object
 * such as a V4L2 control, then that object needs to know about that as well
 * so that an event can be raised by that object. So the 'node' field can
 * be used to link the v4l2_subscribed_event struct into a list of that
 * object.
 *
 * So to summarize:
 *
 * struct v4l2_fh has two lists: one of the subscribed events, and one of the
 * pending events.
 *
 * struct v4l2_subscribed_event has a ringbuffer of raised (pending) events of
 * that particular type.
 *
 * If struct v4l2_subscribed_event is associated with a specific object, then
 * that object will have an internal list of struct v4l2_subscribed_event so
 * it knows who subscribed an event to that object.
 */

struct v4l2_fh;
struct v4l2_subdev;
struct v4l2_subscribed_event;
struct video_device;

/** struct v4l2_kevent - Internal kernel event struct.
  * @list:	List node for the v4l2_fh->available list.
  * @sev:	Pointer to parent v4l2_subscribed_event.
  * @event:	The event itself.
  */
struct v4l2_kevent {
	struct list_head	list;
	struct v4l2_subscribed_event *sev;
	struct v4l2_event	event;
};

/** struct v4l2_subscribed_event_ops - Subscribed event operations.
  * @add:	Optional callback, called when a new listener is added
  * @del:	Optional callback, called when a listener stops listening
  * @replace:	Optional callback that can replace event 'old' with event 'new'.
  * @merge:	Optional callback that can merge event 'old' into event 'new'.
  */
struct v4l2_subscribed_event_ops {
	int  (*add)(struct v4l2_subscribed_event *sev, unsigned elems);
	void (*del)(struct v4l2_subscribed_event *sev);
	void (*replace)(struct v4l2_event *old, const struct v4l2_event *new);
	void (*merge)(const struct v4l2_event *old, struct v4l2_event *new);
};

/** struct v4l2_subscribed_event - Internal struct representing a subscribed event.
  * @list:	List node for the v4l2_fh->subscribed list.
  * @type:	Event type.
  * @id:	Associated object ID (e.g. control ID). 0 if there isn't any.
  * @flags:	Copy of v4l2_event_subscription->flags.
  * @fh:	Filehandle that subscribed to this event.
  * @node:	List node that hooks into the object's event list (if there is one).
  * @ops:	v4l2_subscribed_event_ops
  * @elems:	The number of elements in the events array.
  * @first:	The index of the events containing the oldest available event.
  * @in_use:	The number of queued events.
  * @events:	An array of @elems events.
  */
struct v4l2_subscribed_event {
	struct list_head	list;
	u32			type;
	u32			id;
	u32			flags;
	struct v4l2_fh		*fh;
	struct list_head	node;
	const struct v4l2_subscribed_event_ops *ops;
	unsigned		elems;
	unsigned		first;
	unsigned		in_use;
	struct v4l2_kevent	events[];
};

int v4l2_event_dequeue(struct v4l2_fh *fh, struct v4l2_event *event,
		       int nonblocking);
void v4l2_event_queue(struct video_device *vdev, const struct v4l2_event *ev);
void v4l2_event_queue_fh(struct v4l2_fh *fh, const struct v4l2_event *ev);
int v4l2_event_pending(struct v4l2_fh *fh);
int v4l2_event_subscribe(struct v4l2_fh *fh,
			 const struct v4l2_event_subscription *sub, unsigned elems,
			 const struct v4l2_subscribed_event_ops *ops);
int v4l2_event_unsubscribe(struct v4l2_fh *fh,
			   const struct v4l2_event_subscription *sub);
void v4l2_event_unsubscribe_all(struct v4l2_fh *fh);
int v4l2_event_subdev_unsubscribe(struct v4l2_subdev *sd, struct v4l2_fh *fh,
				  struct v4l2_event_subscription *sub);
#endif /* V4L2_EVENT_H */
