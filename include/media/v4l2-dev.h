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

#define OBSOLETE_DEVDATA 1 /* to be removed soon */

#include <linux/poll.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/compiler.h> /* need __user */
#include <linux/videodev2.h>

#define VIDEO_MAJOR	81
/* Minor device allocation */
#define MINOR_VFL_TYPE_GRABBER_MIN   0
#define MINOR_VFL_TYPE_GRABBER_MAX  63
#define MINOR_VFL_TYPE_RADIO_MIN    64
#define MINOR_VFL_TYPE_RADIO_MAX   127
#define MINOR_VFL_TYPE_VTX_MIN     192
#define MINOR_VFL_TYPE_VTX_MAX     223
#define MINOR_VFL_TYPE_VBI_MIN     224
#define MINOR_VFL_TYPE_VBI_MAX     255

#define VFL_TYPE_GRABBER	0
#define VFL_TYPE_VBI		1
#define VFL_TYPE_RADIO		2
#define VFL_TYPE_VTX		3

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
	struct device *parent;		/* device parent */

	/* device info */
	char name[32];
	int vfl_type;
	int minor;
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

#ifdef OBSOLETE_DEVDATA /* to be removed soon */
	/* dev->driver_data will be used instead some day.
	 * Use the video_{get|set}_drvdata() helper functions,
	 * so the switch over will be transparent for you.
	 * Or use {pci|usb}_{get|set}_drvdata() directly. */
	void *priv;
#endif

	/* for videodev.c internal usage -- please don't touch */
	int users;                     /* video_exclusive_{open|close} ... */
	struct mutex lock;             /* ... helper function uses these   */
};

/* Class-dev to video-device */
#define to_video_device(cd) container_of(cd, struct video_device, dev)

/* Version 2 functions */
extern int video_register_device(struct video_device *vfd, int type, int nr);
int video_register_device_index(struct video_device *vfd, int type, int nr,
					int index);
void video_unregister_device(struct video_device *);

/* helper functions to alloc / release struct video_device, the
   later can be used for video_device->release() */
struct video_device *video_device_alloc(void);
void video_device_release(struct video_device *vfd);

#ifdef OBSOLETE_DEVDATA /* to be removed soon */
/* helper functions to access driver private data. */
static inline void *video_get_drvdata(struct video_device *dev)
{
	return dev->priv;
}

static inline void video_set_drvdata(struct video_device *dev, void *data)
{
	dev->priv = data;
}

/* Obsolete stuff - Still needed for radio devices and obsolete drivers */
extern struct video_device* video_devdata(struct file*);
extern int video_exclusive_open(struct inode *inode, struct file *file);
extern int video_exclusive_release(struct inode *inode, struct file *file);
#endif

#endif /* _V4L2_DEV_H */
