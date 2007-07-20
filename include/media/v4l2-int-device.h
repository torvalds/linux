/*
 * include/media/v4l2-int-device.h
 *
 * V4L2 internal ioctl interface.
 *
 * Copyright (C) 2007 Nokia Corporation.
 *
 * Contact: Sakari Ailus <sakari.ailus@nokia.com>
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

#ifndef V4L2_INT_DEVICE_H
#define V4L2_INT_DEVICE_H

#include <linux/module.h>
#include <media/v4l2-common.h>

#define V4L2NAMESIZE 32

enum v4l2_int_type {
	v4l2_int_type_master = 1,
	v4l2_int_type_slave
};

enum v4l2_int_ioctl_num {
	/*
	 *
	 * "Proper" V4L ioctls, as in struct video_device.
	 *
	 */
	vidioc_int_enum_fmt_cap_num = 1,
	vidioc_int_g_fmt_cap_num,
	vidioc_int_s_fmt_cap_num,
	vidioc_int_try_fmt_cap_num,
	vidioc_int_queryctrl_num,
	vidioc_int_g_ctrl_num,
	vidioc_int_s_ctrl_num,
	vidioc_int_g_parm_num,
	vidioc_int_s_parm_num,

	/*
	 *
	 * Strictly internal ioctls.
	 *
	 */
	/* Initialise the device when slave attaches to the master. */
	vidioc_int_dev_init_num = 1000,
	/* Delinitialise the device at slave detach. */
	vidioc_int_dev_exit_num,
	/* Set device power state: 0 is off, non-zero is on. */
	vidioc_int_s_power_num,
	/* Get parallel interface clock speed for current settings. */
	vidioc_int_g_ext_clk_num,
	/*
	 * Tell what the parallel interface clock speed actually is.
	 */
	vidioc_int_s_ext_clk_num,
	/* Does the slave need to be reset after VIDIOC_DQBUF? */
	vidioc_int_g_needs_reset_num,

	/*
	 *
	 * VIDIOC_INT_* ioctls.
	 *
	 */
	/* VIDIOC_INT_RESET */
	vidioc_int_reset_num,
	/* VIDIOC_INT_INIT */
	vidioc_int_init_num,
	/* VIDIOC_INT_G_CHIP_IDENT */
	vidioc_int_g_chip_ident_num,

	/*
	 *
	 * Start of private ioctls.
	 *
	 */
	vidioc_int_priv_start_num = 2000,
};

struct v4l2_int_device;

struct v4l2_int_master {
	int (*attach)(struct v4l2_int_device *master,
		      struct v4l2_int_device *slave);
	void (*detach)(struct v4l2_int_device *master);
};

typedef int (v4l2_int_ioctl_func)(struct v4l2_int_device *);
typedef int (v4l2_int_ioctl_func_0)(struct v4l2_int_device *);
typedef int (v4l2_int_ioctl_func_1)(struct v4l2_int_device *, void *);

struct v4l2_int_ioctl_desc {
	int num;
	v4l2_int_ioctl_func *func;
};

struct v4l2_int_slave {
	/* Don't touch master. */
	struct v4l2_int_device *master;

	char attach_to[V4L2NAMESIZE];

	int num_ioctls;
	struct v4l2_int_ioctl_desc *ioctls;
};

struct v4l2_int_device {
	/* Don't touch head. */
	struct list_head head;

	struct module *module;

	char name[V4L2NAMESIZE];

	enum v4l2_int_type type;
	union {
		struct v4l2_int_master *master;
		struct v4l2_int_slave *slave;
	} u;

	void *priv;
};

int v4l2_int_device_register(struct v4l2_int_device *d);
void v4l2_int_device_unregister(struct v4l2_int_device *d);

int v4l2_int_ioctl_0(struct v4l2_int_device *d, int cmd);
int v4l2_int_ioctl_1(struct v4l2_int_device *d, int cmd, void *arg);

/*
 *
 * IOCTL wrapper functions for better type checking.
 *
 */

#define V4L2_INT_WRAPPER_0(name)					\
	static inline int vidioc_int_##name(struct v4l2_int_device *d)	\
	{								\
		return v4l2_int_ioctl_0(d, vidioc_int_##name##_num);	\
	}								\
									\
	static inline struct v4l2_int_ioctl_desc			\
	vidioc_int_##name##_cb(int (*func)				\
			       (struct v4l2_int_device *))		\
	{								\
		struct v4l2_int_ioctl_desc desc;			\
									\
		desc.num = vidioc_int_##name##_num;			\
		desc.func = (v4l2_int_ioctl_func *)func;		\
									\
		return desc;						\
	}

#define V4L2_INT_WRAPPER_1(name, arg_type, asterisk)			\
	static inline int vidioc_int_##name(struct v4l2_int_device *d,	\
					    arg_type asterisk arg)	\
	{								\
		return v4l2_int_ioctl_1(d, vidioc_int_##name##_num,	\
					(void *)(unsigned long)arg);	\
	}								\
									\
	static inline struct v4l2_int_ioctl_desc			\
	vidioc_int_##name##_cb(int (*func)				\
			       (struct v4l2_int_device *,		\
				arg_type asterisk))			\
	{								\
		struct v4l2_int_ioctl_desc desc;			\
									\
		desc.num = vidioc_int_##name##_num;			\
		desc.func = (v4l2_int_ioctl_func *)func;		\
									\
		return desc;						\
	}

V4L2_INT_WRAPPER_1(enum_fmt_cap, struct v4l2_fmtdesc, *);
V4L2_INT_WRAPPER_1(g_fmt_cap, struct v4l2_format, *);
V4L2_INT_WRAPPER_1(s_fmt_cap, struct v4l2_format, *);
V4L2_INT_WRAPPER_1(try_fmt_cap, struct v4l2_format, *);
V4L2_INT_WRAPPER_1(queryctrl, struct v4l2_queryctrl, *);
V4L2_INT_WRAPPER_1(g_ctrl, struct v4l2_control, *);
V4L2_INT_WRAPPER_1(s_ctrl, struct v4l2_control, *);
V4L2_INT_WRAPPER_1(g_parm, struct v4l2_streamparm, *);
V4L2_INT_WRAPPER_1(s_parm, struct v4l2_streamparm, *);

V4L2_INT_WRAPPER_0(dev_init);
V4L2_INT_WRAPPER_0(dev_exit);
V4L2_INT_WRAPPER_1(s_power, int, );
V4L2_INT_WRAPPER_1(s_ext_clk, u32, );
V4L2_INT_WRAPPER_1(g_ext_clk, u32, *);
V4L2_INT_WRAPPER_1(g_needs_reset, void, *);

V4L2_INT_WRAPPER_0(reset);
V4L2_INT_WRAPPER_0(init);
V4L2_INT_WRAPPER_1(g_chip_ident, int, *);

#endif
