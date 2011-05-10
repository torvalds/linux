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

#include <media/media-device.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-dev.h>

/* Each instance of a V4L2 device should create the v4l2_device struct,
   either stand-alone or embedded in a larger struct.

   It allows easy access to sub-devices (see v4l2-subdev.h) and provides
   basic V4L2 device-level support.
 */

#define V4L2_DEVICE_NAME_SIZE (20 + 16)

struct v4l2_ctrl_handler;

struct v4l2_device {
	/* dev->driver_data points to this struct.
	   Note: dev might be NULL if there is no parent device
	   as is the case with e.g. ISA devices. */
	struct device *dev;
#if defined(CONFIG_MEDIA_CONTROLLER)
	struct media_device *mdev;
#endif
	/* used to keep track of the registered subdevs */
	struct list_head subdevs;
	/* lock this struct; can be used by the driver as well if this
	   struct is embedded into a larger struct. */
	spinlock_t lock;
	/* unique device name, by default the driver name + bus ID */
	char name[V4L2_DEVICE_NAME_SIZE];
	/* notify callback called by some sub-devices. */
	void (*notify)(struct v4l2_subdev *sd,
			unsigned int notification, void *arg);
	/* The control handler. May be NULL. */
	struct v4l2_ctrl_handler *ctrl_handler;
	/* Device's priority state */
	struct v4l2_prio_state prio;
	/* BKL replacement mutex. Temporary solution only. */
	struct mutex ioctl_lock;
	/* Keep track of the references to this struct. */
	struct kref ref;
	/* Release function that is called when the ref count goes to 0. */
	void (*release)(struct v4l2_device *v4l2_dev);
};

static inline void v4l2_device_get(struct v4l2_device *v4l2_dev)
{
	kref_get(&v4l2_dev->ref);
}

int v4l2_device_put(struct v4l2_device *v4l2_dev);

/* Initialize v4l2_dev and make dev->driver_data point to v4l2_dev.
   dev may be NULL in rare cases (ISA devices). In that case you
   must fill in the v4l2_dev->name field before calling this function. */
int __must_check v4l2_device_register(struct device *dev, struct v4l2_device *v4l2_dev);

/* Optional function to initialize the name field of struct v4l2_device using
   the driver name and a driver-global atomic_t instance.
   This function will increment the instance counter and returns the instance
   value used in the name.

   Example:

   static atomic_t drv_instance = ATOMIC_INIT(0);

   ...

   instance = v4l2_device_set_name(&v4l2_dev, "foo", &drv_instance);

   The first time this is called the name field will be set to foo0 and
   this function returns 0. If the name ends with a digit (e.g. cx18),
   then the name will be set to cx18-0 since cx180 looks really odd. */
int v4l2_device_set_name(struct v4l2_device *v4l2_dev, const char *basename,
						atomic_t *instance);

/* Set v4l2_dev->dev to NULL. Call when the USB parent disconnects.
   Since the parent disappears this ensures that v4l2_dev doesn't have an
   invalid parent pointer. */
void v4l2_device_disconnect(struct v4l2_device *v4l2_dev);

/* Unregister all sub-devices and any other resources related to v4l2_dev. */
void v4l2_device_unregister(struct v4l2_device *v4l2_dev);

/* Register a subdev with a v4l2 device. While registered the subdev module
   is marked as in-use. An error is returned if the module is no longer
   loaded when you attempt to register it. */
int __must_check v4l2_device_register_subdev(struct v4l2_device *v4l2_dev,
						struct v4l2_subdev *sd);
/* Unregister a subdev with a v4l2 device. Can also be called if the subdev
   wasn't registered. In that case it will do nothing. */
void v4l2_device_unregister_subdev(struct v4l2_subdev *sd);

/* Register device nodes for all subdev of the v4l2 device that are marked with
 * the V4L2_SUBDEV_FL_HAS_DEVNODE flag.
 */
int __must_check
v4l2_device_register_subdev_nodes(struct v4l2_device *v4l2_dev);

/* Iterate over all subdevs. */
#define v4l2_device_for_each_subdev(sd, v4l2_dev)			\
	list_for_each_entry(sd, &(v4l2_dev)->subdevs, list)

/* Call the specified callback for all subdevs matching the condition.
   Ignore any errors. Note that you cannot add or delete a subdev
   while walking the subdevs list. */
#define __v4l2_device_call_subdevs_p(v4l2_dev, sd, cond, o, f, args...)	\
	do { 								\
		list_for_each_entry((sd), &(v4l2_dev)->subdevs, list)	\
			if ((cond) && (sd)->ops->o && (sd)->ops->o->f)	\
				(sd)->ops->o->f((sd) , ##args);		\
	} while (0)

#define __v4l2_device_call_subdevs(v4l2_dev, cond, o, f, args...)	\
	do {								\
		struct v4l2_subdev *__sd;				\
									\
		__v4l2_device_call_subdevs_p(v4l2_dev, __sd, cond, o,	\
						f , ##args);		\
	} while (0)

/* Call the specified callback for all subdevs matching the condition.
   If the callback returns an error other than 0 or -ENOIOCTLCMD, then
   return with that error code. Note that you cannot add or delete a
   subdev while walking the subdevs list. */
#define __v4l2_device_call_subdevs_until_err_p(v4l2_dev, sd, cond, o, f, args...) \
({ 									\
	long __err = 0;							\
									\
	list_for_each_entry((sd), &(v4l2_dev)->subdevs, list) {		\
		if ((cond) && (sd)->ops->o && (sd)->ops->o->f)		\
			__err = (sd)->ops->o->f((sd) , ##args);		\
		if (__err && __err != -ENOIOCTLCMD)			\
			break; 						\
	} 								\
	(__err == -ENOIOCTLCMD) ? 0 : __err;				\
})

#define __v4l2_device_call_subdevs_until_err(v4l2_dev, cond, o, f, args...) \
({									\
	struct v4l2_subdev *__sd;					\
	__v4l2_device_call_subdevs_until_err_p(v4l2_dev, __sd, cond, o,	\
						f , ##args);		\
})

/* Call the specified callback for all subdevs matching grp_id (if 0, then
   match them all). Ignore any errors. Note that you cannot add or delete
   a subdev while walking the subdevs list. */
#define v4l2_device_call_all(v4l2_dev, grpid, o, f, args...)		\
	do {								\
		struct v4l2_subdev *__sd;				\
									\
		__v4l2_device_call_subdevs_p(v4l2_dev, __sd,		\
			!(grpid) || __sd->grp_id == (grpid), o, f ,	\
			##args);					\
	} while (0)

/* Call the specified callback for all subdevs matching grp_id (if 0, then
   match them all). If the callback returns an error other than 0 or
   -ENOIOCTLCMD, then return with that error code. Note that you cannot
   add or delete a subdev while walking the subdevs list. */
#define v4l2_device_call_until_err(v4l2_dev, grpid, o, f, args...) 	\
({									\
	struct v4l2_subdev *__sd;					\
	__v4l2_device_call_subdevs_until_err_p(v4l2_dev, __sd,		\
			!(grpid) || __sd->grp_id == (grpid), o, f ,	\
			##args);					\
})

#endif
