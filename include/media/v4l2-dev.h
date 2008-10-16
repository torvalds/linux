/*
 *
 *	V 4 L 2   D R I V E R   H E L P E R   A P I
 *
 * Moved from videodev2.h
 *
 *	Some commonly needed functions for drivers (v4l2-common.o module)
 */
#ifndef _V4L2_DEV_H
#define _V4L2_DEV_H

#include <linux/poll.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/mutex.h>
#include <linux/videodev2.h>

#define VIDEO_MAJOR	81

#define VFL_TYPE_GRABBER	0
#define VFL_TYPE_VBI		1
#define VFL_TYPE_RADIO		2
#define VFL_TYPE_VTX		3
#define VFL_TYPE_MAX		4

struct v4l2_ioctl_callbacks;

/*
 * Newer version of video_device, handled by videodev2.c
 * 	This version moves redundant code from video device code to
 *	the common handler
 */

struct video_device
{
	/* device ops */
	const struct file_operations *fops;

	/* sysfs */
	struct device dev;		/* v4l device */
	struct cdev cdev;		/* character device */
	void (*cdev_release)(struct kobject *kobj);
	struct device *parent;		/* device parent */

	/* device info */
	char name[32];
	int vfl_type;
	int minor;
	u16 num;
	/* attribute to differentiate multiple indices on one physical device */
	int index;

	int debug;			/* Activates debug level*/

	/* Video standard vars */
	v4l2_std_id tvnorms;		/* Supported tv norms */
	v4l2_std_id current_norm;	/* Current tvnorm */

	/* callbacks */
	void (*release)(struct video_device *vfd);

	/* ioctl callbacks */
	const struct v4l2_ioctl_ops *ioctl_ops;
};

/* dev to video-device */
#define to_video_device(cd) container_of(cd, struct video_device, dev)

/* Register and unregister devices. Note that if video_register_device fails,
   the release() callback of the video_device structure is *not* called, so
   the caller is responsible for freeing any data. Usually that means that
   you call video_device_release() on failure. */
int __must_check video_register_device(struct video_device *vfd, int type, int nr);
int __must_check video_register_device_index(struct video_device *vfd,
						int type, int nr, int index);
void video_unregister_device(struct video_device *vfd);

/* helper functions to alloc/release struct video_device, the
   latter can also be used for video_device->release(). */
struct video_device * __must_check video_device_alloc(void);

/* this release function frees the vfd pointer */
void video_device_release(struct video_device *vfd);

/* this release function does nothing, use when the video_device is a
   static global struct. Note that having a static video_device is
   a dubious construction at best. */
void video_device_release_empty(struct video_device *vfd);

/* helper functions to access driver private data. */
static inline void *video_get_drvdata(struct video_device *dev)
{
	return dev_get_drvdata(&dev->dev);
}

static inline void video_set_drvdata(struct video_device *dev, void *data)
{
	dev_set_drvdata(&dev->dev, data);
}

struct video_device *video_devdata(struct file *file);

/* Combine video_get_drvdata and video_devdata as this is
   used very often. */
static inline void *video_drvdata(struct file *file)
{
	return video_get_drvdata(video_devdata(file));
}

#endif /* _V4L2_DEV_H */
