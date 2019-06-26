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
 */

#ifndef V4L2_EVENT_H
#define V4L2_EVENT_H

#include <linux/types.h>
#include <linux/videodev2.h>
#include <linux/wait.h>

struct v4l2_fh;
struct v4l2_subdev;
struct v4l2_subscribed_event;
struct video_device;

/**
 * struct v4l2_kevent - Internal kernel event struct.
 * @list:	List node for the v4l2_fh->available list.
 * @sev:	Pointer to parent v4l2_subscribed_event.
 * @event:	The event itself.
 * @ts:		The timestamp of the event.
 */
struct v4l2_kevent {
	struct list_head	list;
	struct v4l2_subscribed_event *sev;
	struct v4l2_event	event;
	u64			ts;
};

/**
 * struct v4l2_subscribed_event_ops - Subscribed event operations.
 *
 * @add:	Optional callback, called when a new listener is added
 * @del:	Optional callback, called when a listener stops listening
 * @replace:	Optional callback that can replace event 'old' with event 'new'.
 * @merge:	Optional callback that can merge event 'old' into event 'new'.
 */
struct v4l2_subscribed_event_ops {
	int  (*add)(struct v4l2_subscribed_event *sev, unsigned int elems);
	void (*del)(struct v4l2_subscribed_event *sev);
	void (*replace)(struct v4l2_event *old, const struct v4l2_event *new);
	void (*merge)(const struct v4l2_event *old, struct v4l2_event *new);
};

/**
 * struct v4l2_subscribed_event - Internal struct representing a subscribed
 *		event.
 *
 * @list:	List node for the v4l2_fh->subscribed list.
 * @type:	Event type.
 * @id:	Associated object ID (e.g. control ID). 0 if there isn't any.
 * @flags:	Copy of v4l2_event_subscription->flags.
 * @fh:	Filehandle that subscribed to this event.
 * @node:	List node that hooks into the object's event list
 *		(if there is one).
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
	unsigned int		elems;
	unsigned int		first;
	unsigned int		in_use;
	struct v4l2_kevent	events[];
};

/**
 * v4l2_event_dequeue - Dequeue events from video device.
 *
 * @fh: pointer to struct v4l2_fh
 * @event: pointer to struct v4l2_event
 * @nonblocking: if not zero, waits for an event to arrive
 */
int v4l2_event_dequeue(struct v4l2_fh *fh, struct v4l2_event *event,
		       int nonblocking);

/**
 * v4l2_event_queue - Queue events to video device.
 *
 * @vdev: pointer to &struct video_device
 * @ev: pointer to &struct v4l2_event
 *
 * The event will be queued for all &struct v4l2_fh file handlers.
 *
 * .. note::
 *    The driver's only responsibility is to fill in the type and the data
 *    fields.The other fields will be filled in by  V4L2.
 */
void v4l2_event_queue(struct video_device *vdev, const struct v4l2_event *ev);

/**
 * v4l2_event_queue_fh - Queue events to video device.
 *
 * @fh: pointer to &struct v4l2_fh
 * @ev: pointer to &struct v4l2_event
 *
 *
 * The event will be queued only for the specified &struct v4l2_fh file handler.
 *
 * .. note::
 *    The driver's only responsibility is to fill in the type and the data
 *    fields.The other fields will be filled in by  V4L2.
 */
void v4l2_event_queue_fh(struct v4l2_fh *fh, const struct v4l2_event *ev);

/**
 * v4l2_event_pending - Check if an event is available
 *
 * @fh: pointer to &struct v4l2_fh
 *
 * Returns the number of pending events.
 */
int v4l2_event_pending(struct v4l2_fh *fh);

/**
 * v4l2_event_subscribe - Subscribes to an event
 *
 * @fh: pointer to &struct v4l2_fh
 * @sub: pointer to &struct v4l2_event_subscription
 * @elems: size of the events queue
 * @ops: pointer to &v4l2_subscribed_event_ops
 *
 * .. note::
 *
 *    if @elems is zero, the framework will fill in a default value,
 *    with is currently 1 element.
 */
int v4l2_event_subscribe(struct v4l2_fh *fh,
			 const struct v4l2_event_subscription *sub,
			 unsigned int elems,
			 const struct v4l2_subscribed_event_ops *ops);
/**
 * v4l2_event_unsubscribe - Unsubscribes to an event
 *
 * @fh: pointer to &struct v4l2_fh
 * @sub: pointer to &struct v4l2_event_subscription
 */
int v4l2_event_unsubscribe(struct v4l2_fh *fh,
			   const struct v4l2_event_subscription *sub);
/**
 * v4l2_event_unsubscribe_all - Unsubscribes to all events
 *
 * @fh: pointer to &struct v4l2_fh
 */
void v4l2_event_unsubscribe_all(struct v4l2_fh *fh);

/**
 * v4l2_event_subdev_unsubscribe - Subdev variant of v4l2_event_unsubscribe()
 *
 * @sd: pointer to &struct v4l2_subdev
 * @fh: pointer to &struct v4l2_fh
 * @sub: pointer to &struct v4l2_event_subscription
 *
 * .. note::
 *
 *	This function should be used for the &struct v4l2_subdev_core_ops
 *	%unsubscribe_event field.
 */
int v4l2_event_subdev_unsubscribe(struct v4l2_subdev *sd,
				  struct v4l2_fh *fh,
				  struct v4l2_event_subscription *sub);
/**
 * v4l2_src_change_event_subscribe - helper function that calls
 *	v4l2_event_subscribe() if the event is %V4L2_EVENT_SOURCE_CHANGE.
 *
 * @fh: pointer to struct v4l2_fh
 * @sub: pointer to &struct v4l2_event_subscription
 */
int v4l2_src_change_event_subscribe(struct v4l2_fh *fh,
				    const struct v4l2_event_subscription *sub);
/**
 * v4l2_src_change_event_subdev_subscribe - Variant of v4l2_event_subscribe(),
 *	meant to subscribe only events of the type %V4L2_EVENT_SOURCE_CHANGE.
 *
 * @sd: pointer to &struct v4l2_subdev
 * @fh: pointer to &struct v4l2_fh
 * @sub: pointer to &struct v4l2_event_subscription
 */
int v4l2_src_change_event_subdev_subscribe(struct v4l2_subdev *sd,
					   struct v4l2_fh *fh,
					   struct v4l2_event_subscription *sub);
#endif /* V4L2_EVENT_H */
