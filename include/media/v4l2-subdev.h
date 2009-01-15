/*
    V4L2 sub-device support header.

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

#ifndef _V4L2_SUBDEV_H
#define _V4L2_SUBDEV_H

#include <media/v4l2-common.h>

struct v4l2_device;
struct v4l2_subdev;
struct tuner_setup;

/* Sub-devices are devices that are connected somehow to the main bridge
   device. These devices are usually audio/video muxers/encoders/decoders or
   sensors and webcam controllers.

   Usually these devices are controlled through an i2c bus, but other busses
   may also be used.

   The v4l2_subdev struct provides a way of accessing these devices in a
   generic manner. Most operations that these sub-devices support fall in
   a few categories: core ops, audio ops, video ops and tuner ops.

   More categories can be added if needed, although this should remain a
   limited set (no more than approx. 8 categories).

   Each category has its own set of ops that subdev drivers can implement.

   A subdev driver can leave the pointer to the category ops NULL if
   it does not implement them (e.g. an audio subdev will generally not
   implement the video category ops). The exception is the core category:
   this must always be present.

   These ops are all used internally so it is no problem to change, remove
   or add ops or move ops from one to another category. Currently these
   ops are based on the original ioctls, but since ops are not limited to
   one argument there is room for improvement here once all i2c subdev
   drivers are converted to use these ops.
 */

/* Core ops: it is highly recommended to implement at least these ops:

   g_chip_ident
   log_status
   g_register
   s_register

   This provides basic debugging support.

   The ioctl ops is meant for generic ioctl-like commands. Depending on
   the use-case it might be better to use subdev-specific ops (currently
   not yet implemented) since ops provide proper type-checking.
 */
struct v4l2_subdev_core_ops {
	int (*g_chip_ident)(struct v4l2_subdev *sd, struct v4l2_dbg_chip_ident *chip);
	int (*log_status)(struct v4l2_subdev *sd);
	int (*init)(struct v4l2_subdev *sd, u32 val);
	int (*s_standby)(struct v4l2_subdev *sd, u32 standby);
	int (*reset)(struct v4l2_subdev *sd, u32 val);
	int (*s_gpio)(struct v4l2_subdev *sd, u32 val);
	int (*queryctrl)(struct v4l2_subdev *sd, struct v4l2_queryctrl *qc);
	int (*g_ctrl)(struct v4l2_subdev *sd, struct v4l2_control *ctrl);
	int (*s_ctrl)(struct v4l2_subdev *sd, struct v4l2_control *ctrl);
	int (*querymenu)(struct v4l2_subdev *sd, struct v4l2_querymenu *qm);
	long (*ioctl)(struct v4l2_subdev *sd, unsigned int cmd, void *arg);
#ifdef CONFIG_VIDEO_ADV_DEBUG
	int (*g_register)(struct v4l2_subdev *sd, struct v4l2_dbg_register *reg);
	int (*s_register)(struct v4l2_subdev *sd, struct v4l2_dbg_register *reg);
#endif
};

struct v4l2_subdev_tuner_ops {
	int (*s_mode)(struct v4l2_subdev *sd, enum v4l2_tuner_type);
	int (*s_radio)(struct v4l2_subdev *sd);
	int (*s_frequency)(struct v4l2_subdev *sd, struct v4l2_frequency *freq);
	int (*g_frequency)(struct v4l2_subdev *sd, struct v4l2_frequency *freq);
	int (*g_tuner)(struct v4l2_subdev *sd, struct v4l2_tuner *vt);
	int (*s_tuner)(struct v4l2_subdev *sd, struct v4l2_tuner *vt);
	int (*s_std)(struct v4l2_subdev *sd, v4l2_std_id norm);
	int (*s_type_addr)(struct v4l2_subdev *sd, struct tuner_setup *type);
	int (*s_config)(struct v4l2_subdev *sd, const struct v4l2_priv_tun_config *config);
};

struct v4l2_subdev_audio_ops {
	int (*s_clock_freq)(struct v4l2_subdev *sd, u32 freq);
	int (*s_i2s_clock_freq)(struct v4l2_subdev *sd, u32 freq);
	int (*s_routing)(struct v4l2_subdev *sd, const struct v4l2_routing *route);
};

struct v4l2_subdev_video_ops {
	int (*s_routing)(struct v4l2_subdev *sd, const struct v4l2_routing *route);
	int (*s_crystal_freq)(struct v4l2_subdev *sd, struct v4l2_crystal_freq *freq);
	int (*decode_vbi_line)(struct v4l2_subdev *sd, struct v4l2_decode_vbi_line *vbi_line);
	int (*s_vbi_data)(struct v4l2_subdev *sd, const struct v4l2_sliced_vbi_data *vbi_data);
	int (*g_vbi_data)(struct v4l2_subdev *sd, struct v4l2_sliced_vbi_data *vbi_data);
	int (*g_sliced_vbi_cap)(struct v4l2_subdev *sd, struct v4l2_sliced_vbi_cap *cap);
	int (*s_std_output)(struct v4l2_subdev *sd, v4l2_std_id std);
	int (*s_stream)(struct v4l2_subdev *sd, int enable);
	int (*s_fmt)(struct v4l2_subdev *sd, struct v4l2_format *fmt);
	int (*g_fmt)(struct v4l2_subdev *sd, struct v4l2_format *fmt);
};

struct v4l2_subdev_ops {
	const struct v4l2_subdev_core_ops  *core;
	const struct v4l2_subdev_tuner_ops *tuner;
	const struct v4l2_subdev_audio_ops *audio;
	const struct v4l2_subdev_video_ops *video;
};

#define V4L2_SUBDEV_NAME_SIZE 32

/* Each instance of a subdev driver should create this struct, either
   stand-alone or embedded in a larger struct.
 */
struct v4l2_subdev {
	struct list_head list;
	struct module *owner;
	struct v4l2_device *dev;
	const struct v4l2_subdev_ops *ops;
	/* name must be unique */
	char name[V4L2_SUBDEV_NAME_SIZE];
	/* can be used to group similar subdevs, value is driver-specific */
	u32 grp_id;
	/* pointer to private data */
	void *priv;
};

static inline void v4l2_set_subdevdata(struct v4l2_subdev *sd, void *p)
{
	sd->priv = p;
}

static inline void *v4l2_get_subdevdata(const struct v4l2_subdev *sd)
{
	return sd->priv;
}

/* Convert an ioctl-type command to the proper v4l2_subdev_ops function call.
   This is used by subdev modules that can be called by both old-style ioctl
   commands and through the v4l2_subdev_ops.

   The ioctl API of the subdev driver can call this function to call the
   right ops based on the ioctl cmd and arg.

   Once all subdev drivers have been converted and all drivers no longer
   use the ioctl interface, then this function can be removed.
 */
int v4l2_subdev_command(struct v4l2_subdev *sd, unsigned cmd, void *arg);

static inline void v4l2_subdev_init(struct v4l2_subdev *sd,
					const struct v4l2_subdev_ops *ops)
{
	INIT_LIST_HEAD(&sd->list);
	/* ops->core MUST be set */
	BUG_ON(!ops || !ops->core);
	sd->ops = ops;
	sd->dev = NULL;
	sd->name[0] = '\0';
	sd->grp_id = 0;
	sd->priv = NULL;
}

/* Call an ops of a v4l2_subdev, doing the right checks against
   NULL pointers.

   Example: err = v4l2_subdev_call(sd, core, g_chip_ident, &chip);
 */
#define v4l2_subdev_call(sd, o, f, args...)				\
	(!(sd) ? -ENODEV : (((sd) && (sd)->ops->o && (sd)->ops->o->f) ?	\
		(sd)->ops->o->f((sd) , ##args) : -ENOIOCTLCMD))

#endif
