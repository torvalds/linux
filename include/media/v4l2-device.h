/*
    V4L2 device support header.

    Copyright (C) 2008  Hans Verkuil <hverkuil@xs4all.nl>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef _V4L2_DEVICE_H
#define _V4L2_DEVICE_H

#include <media/v4l2-subdev.h>

/* Each instance of a V4L2 device should create the v4l2_device struct,
   either stand-alone or embedded in a larger struct.

   It allows easy access to sub-devices (see v4l2-subdev.h) and provides
   basic V4L2 device-level support.
 */

#define V4L2_DEVICE_NAME_SIZE (BUS_ID_SIZE + 16)

struct v4l2_device {
	/* dev->driver_data points to this struct */
	struct device *dev;
	/* used to keep track of the registered subdevs */
	struct list_head subdevs;
	/* lock this struct; can be used by the driver as well if this
	   struct is embedded into a larger struct. */
	spinlock_t lock;
	/* unique device name, by default the driver name + bus ID */
	char name[V4L2_DEVICE_NAME_SIZE];
};

/* Initialize v4l2_dev and make dev->driver_data point to v4l2_dev */
int __must_check v4l2_device_register(struct device *dev, struct v4l2_device *v4l2_dev);
/* Set v4l2_dev->dev->driver_data to NULL and unregister all sub-devices */
void v4l2_device_unregister(struct v4l2_device *v4l2_dev);

/* Register a subdev with a v4l2 device. While registered the subdev module
   is marked as in-use. An error is returned if the module is no longer
   loaded when you attempt to register it. */
int __must_check v4l2_device_register_subdev(struct v4l2_device *dev, struct v4l2_subdev *sd);
/* Unregister a subdev with a v4l2 device. Can also be called if the subdev
   wasn't registered. In that case it will do nothing. */
void v4l2_device_unregister_subdev(struct v4l2_subdev *sd);

/* Iterate over all subdevs. */
#define v4l2_device_for_each_subdev(sd, dev)				\
	list_for_each_entry(sd, &(dev)->subdevs, list)

/* Call the specified callback for all subdevs matching the condition.
   Ignore any errors. Note that you cannot add or delete a subdev
   while walking the subdevs list. */
#define __v4l2_device_call_subdevs(dev, cond, o, f, args...) 		\
	do { 								\
		struct v4l2_subdev *sd; 				\
									\
		list_for_each_entry(sd, &(dev)->subdevs, list)   	\
			if ((cond) && sd->ops->o && sd->ops->o->f) 	\
				sd->ops->o->f(sd , ##args); 		\
	} while (0)

/* Call the specified callback for all subdevs matching the condition.
   If the callback returns an error other than 0 or -ENOIOCTLCMD, then
   return with that error code. Note that you cannot add or delete a
   subdev while walking the subdevs list. */
#define __v4l2_device_call_subdevs_until_err(dev, cond, o, f, args...)  \
({ 									\
	struct v4l2_subdev *sd; 					\
	long err = 0; 							\
									\
	list_for_each_entry(sd, &(dev)->subdevs, list) { 		\
		if ((cond) && sd->ops->o && sd->ops->o->f) 		\
			err = sd->ops->o->f(sd , ##args); 		\
		if (err && err != -ENOIOCTLCMD)				\
			break; 						\
	} 								\
	(err == -ENOIOCTLCMD) ? 0 : err; 				\
})

/* Call the specified callback for all subdevs matching grp_id (if 0, then
   match them all). Ignore any errors. Note that you cannot add or delete
   a subdev while walking the subdevs list. */
#define v4l2_device_call_all(dev, grp_id, o, f, args...) 		\
	__v4l2_device_call_subdevs(dev, 				\
			!(grp_id) || sd->grp_id == (grp_id), o, f , ##args)

/* Call the specified callback for all subdevs matching grp_id (if 0, then
   match them all). If the callback returns an error other than 0 or
   -ENOIOCTLCMD, then return with that error code. Note that you cannot
   add or delete a subdev while walking the subdevs list. */
#define v4l2_device_call_until_err(dev, grp_id, o, f, args...) 		\
	__v4l2_device_call_subdevs_until_err(dev,			\
		       !(grp_id) || sd->grp_id == (grp_id), o, f , ##args)

#endif
