/*
 * v4l2-fh.h
 *
 * V4L2 file handle. Store per file handle data for the V4L2
 * framework. Using file handles is optional for the drivers.
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

#ifndef V4L2_FH_H
#define V4L2_FH_H

#include <linux/list.h>

struct video_device;
struct v4l2_events;
struct v4l2_ctrl_handler;

struct v4l2_fh {
	struct list_head	list;
	struct video_device	*vdev;
	struct v4l2_events      *events; /* events, pending and subscribed */
	struct v4l2_ctrl_handler *ctrl_handler;
	enum v4l2_priority	prio;
};

/*
 * Initialise the file handle. Parts of the V4L2 framework using the
 * file handles should be initialised in this function. Must be called
 * from driver's v4l2_file_operations->open() handler if the driver
 * uses v4l2_fh.
 */
int v4l2_fh_init(struct v4l2_fh *fh, struct video_device *vdev);
/*
 * Add the fh to the list of file handles on a video_device. The file
 * handle must be initialised first.
 */
void v4l2_fh_add(struct v4l2_fh *fh);
/*
 * Can be used as the open() op of v4l2_file_operations.
 * It allocates a v4l2_fh and inits and adds it to the video_device associated
 * with the file pointer.
 */
int v4l2_fh_open(struct file *filp);
/*
 * Remove file handle from the list of file handles. Must be called in
 * v4l2_file_operations->release() handler if the driver uses v4l2_fh.
 * On error filp->private_data will be NULL, otherwise it will point to
 * the v4l2_fh struct.
 */
void v4l2_fh_del(struct v4l2_fh *fh);
/*
 * Release resources related to a file handle. Parts of the V4L2
 * framework using the v4l2_fh must release their resources here, too.
 * Must be called in v4l2_file_operations->release() handler if the
 * driver uses v4l2_fh.
 */
void v4l2_fh_exit(struct v4l2_fh *fh);
/*
 * Can be used as the release() op of v4l2_file_operations.
 * It deletes and exits the v4l2_fh associated with the file pointer and
 * frees it. It will do nothing if filp->private_data (the pointer to the
 * v4l2_fh struct) is NULL. This function always returns 0.
 */
int v4l2_fh_release(struct file *filp);
/*
 * Returns 1 if this filehandle is the only filehandle opened for the
 * associated video_device. If fh is NULL, then it returns 0.
 */
int v4l2_fh_is_singular(struct v4l2_fh *fh);
/*
 * Helper function with struct file as argument. If filp->private_data is
 * NULL, then it will return 0.
 */
static inline int v4l2_fh_is_singular_file(struct file *filp)
{
	return v4l2_fh_is_singular(filp->private_data);
}

#endif /* V4L2_EVENT_H */
