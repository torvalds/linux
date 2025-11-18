/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * v4l2-fh.h
 *
 * V4L2 file handle. Store per file handle data for the V4L2
 * framework. Using file handles is mandatory for the drivers.
 *
 * Copyright (C) 2009--2010 Nokia Corporation.
 *
 * Contact: Sakari Ailus <sakari.ailus@iki.fi>
 */

#ifndef V4L2_FH_H
#define V4L2_FH_H

#include <linux/fs.h>
#include <linux/kconfig.h>
#include <linux/list.h>
#include <linux/videodev2.h>

struct video_device;
struct v4l2_ctrl_handler;

/**
 * struct v4l2_fh - Describes a V4L2 file handler
 *
 * @list: list of file handlers
 * @vdev: pointer to &struct video_device
 * @ctrl_handler: pointer to &struct v4l2_ctrl_handler
 * @prio: priority of the file handler, as defined by &enum v4l2_priority
 *
 * @wait: event' s wait queue
 * @subscribe_lock: serialise changes to the subscribed list; guarantee that
 *		    the add and del event callbacks are orderly called
 * @subscribed: list of subscribed events
 * @available: list of events waiting to be dequeued
 * @navailable: number of available events at @available list
 * @sequence: event sequence number
 *
 * @m2m_ctx: pointer to &struct v4l2_m2m_ctx
 */
struct v4l2_fh {
	struct list_head	list;
	struct video_device	*vdev;
	struct v4l2_ctrl_handler *ctrl_handler;
	enum v4l2_priority	prio;

	/* Events */
	wait_queue_head_t	wait;
	struct mutex		subscribe_lock;
	struct list_head	subscribed;
	struct list_head	available;
	unsigned int		navailable;
	u32			sequence;

	struct v4l2_m2m_ctx	*m2m_ctx;
};

/**
 * file_to_v4l2_fh - Return the v4l2_fh associated with a struct file
 *
 * @filp: pointer to &struct file
 *
 * This function should be used by drivers to retrieve the &struct v4l2_fh
 * instance pointer stored in the file private_data instead of accessing the
 * private_data field directly.
 */
static inline struct v4l2_fh *file_to_v4l2_fh(struct file *filp)
{
	return filp->private_data;
}

/**
 * v4l2_fh_init - Initialise the file handle.
 *
 * @fh: pointer to &struct v4l2_fh
 * @vdev: pointer to &struct video_device
 *
 * Parts of the V4L2 framework using the
 * file handles should be initialised in this function. Must be called
 * from driver's v4l2_file_operations->open\(\) handler if the driver
 * uses &struct v4l2_fh.
 */
void v4l2_fh_init(struct v4l2_fh *fh, struct video_device *vdev);

/**
 * v4l2_fh_add - Add the fh to the list of file handles on a video_device.
 *
 * @fh: pointer to &struct v4l2_fh
 * @filp: pointer to &struct file associated with @fh
 *
 * The function sets filp->private_data to point to @fh.
 *
 * .. note::
 *    The @fh file handle must be initialised first.
 */
void v4l2_fh_add(struct v4l2_fh *fh, struct file *filp);

/**
 * v4l2_fh_open - Ancillary routine that can be used as the open\(\) op
 *	of v4l2_file_operations.
 *
 * @filp: pointer to struct file
 *
 * It allocates a v4l2_fh and inits and adds it to the &struct video_device
 * associated with the file pointer.
 *
 * On error filp->private_data will be %NULL, otherwise it will point to
 * the &struct v4l2_fh.
 */
int v4l2_fh_open(struct file *filp);

/**
 * v4l2_fh_del - Remove file handle from the list of file handles.
 *
 * @fh: pointer to &struct v4l2_fh
 * @filp: pointer to &struct file associated with @fh
 *
 * The function resets filp->private_data to NULL.
 *
 * .. note::
 *    Must be called in v4l2_file_operations->release\(\) handler if the driver
 *    uses &struct v4l2_fh.
 */
void v4l2_fh_del(struct v4l2_fh *fh, struct file *filp);

/**
 * v4l2_fh_exit - Release resources related to a file handle.
 *
 * @fh: pointer to &struct v4l2_fh
 *
 * Parts of the V4L2 framework using the v4l2_fh must release their
 * resources here, too.
 *
 * .. note::
 *    Must be called in v4l2_file_operations->release\(\) handler if the
 *    driver uses &struct v4l2_fh.
 */
void v4l2_fh_exit(struct v4l2_fh *fh);

/**
 * v4l2_fh_release - Ancillary routine that can be used as the release\(\) op
 *	of v4l2_file_operations.
 *
 * @filp: pointer to struct file
 *
 * It deletes and exits the v4l2_fh associated with the file pointer and
 * frees it. It will do nothing if filp->private_data (the pointer to the
 * v4l2_fh struct) is %NULL.
 *
 * This function always returns 0.
 */
int v4l2_fh_release(struct file *filp);

/**
 * v4l2_fh_is_singular - Returns 1 if this filehandle is the only filehandle
 *	 opened for the associated video_device.
 *
 * @fh: pointer to &struct v4l2_fh
 *
 * If @fh is NULL, then it returns 0.
 */
int v4l2_fh_is_singular(struct v4l2_fh *fh);

/**
 * v4l2_fh_is_singular_file - Returns 1 if this filehandle is the only
 *	filehandle opened for the associated video_device.
 *
 * @filp: pointer to struct file
 *
 * This is a helper function variant of v4l2_fh_is_singular() with uses
 * struct file as argument.
 *
 * If filp->private_data is %NULL, then it will return 0.
 */
static inline int v4l2_fh_is_singular_file(struct file *filp)
{
	return v4l2_fh_is_singular(filp->private_data);
}

#endif /* V4L2_EVENT_H */
