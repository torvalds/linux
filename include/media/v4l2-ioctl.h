/*
 *
 *	V 4 L 2   D R I V E R   H E L P E R   A P I
 *
 * Moved from videodev2.h
 *
 *	Some commonly needed functions for drivers (v4l2-common.o module)
 */
#ifndef _V4L2_IOCTL_H
#define _V4L2_IOCTL_H

#include <linux/poll.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/compiler.h> /* need __user */
#ifdef CONFIG_VIDEO_V4L1_COMPAT
#include <linux/videodev.h>
#else
#include <linux/videodev2.h>
#endif

/*  Video standard functions  */
extern const char *v4l2_norm_to_name(v4l2_std_id id);
extern int v4l2_video_std_construct(struct v4l2_standard *vs,
				    int id, const char *name);
/* Prints the ioctl in a human-readable format */
extern void v4l_printk_ioctl(unsigned int cmd);

/* names for fancy debug output */
extern const char *v4l2_field_names[];
extern const char *v4l2_type_names[];

/*  Compatibility layer interface  --  v4l1-compat module */
typedef int (*v4l2_kioctl)(struct inode *inode, struct file *file,
			   unsigned int cmd, void *arg);
#ifdef CONFIG_VIDEO_V4L1_COMPAT
int v4l_compat_translate_ioctl(struct inode *inode, struct file *file,
			       int cmd, void *arg, v4l2_kioctl driver_ioctl);
#else
#define v4l_compat_translate_ioctl(inode, file, cmd, arg, ioctl) (-EINVAL)
#endif

/* 32 Bits compatibility layer for 64 bits processors */
extern long v4l_compat_ioctl32(struct file *file, unsigned int cmd,
				unsigned long arg);

extern int video_ioctl2(struct inode *inode, struct file *file,
			  unsigned int cmd, unsigned long arg);

/* Include support for obsoleted stuff */
extern int video_usercopy(struct inode *inode, struct file *file,
			  unsigned int cmd, unsigned long arg,
			  int (*func)(struct inode *inode, struct file *file,
				      unsigned int cmd, void *arg));

#endif /* _V4L2_IOCTL_H */
