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

/* v4l debugging and diagnostics */

/* Debug bitmask flags to be used on V4L2 */
#define V4L2_DEBUG_IOCTL     0x01
#define V4L2_DEBUG_IOCTL_ARG 0x02

/* Use this macro for non-I2C drivers. Pass the driver name as the first arg. */
#define v4l_print_ioctl(name, cmd)  		 \
	do {  					 \
		printk(KERN_DEBUG "%s: ", name); \
		v4l_printk_ioctl(cmd);		 \
	} while (0)

/* Use this macro in I2C drivers where 'client' is the struct i2c_client
   pointer */
#define v4l_i2c_print_ioctl(client, cmd) 		   \
	do {      					   \
		v4l_client_printk(KERN_DEBUG, client, ""); \
		v4l_printk_ioctl(cmd);			   \
	} while (0)

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
