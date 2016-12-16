/*
    v4l2 common internal API header

    This header contains internal shared ioctl definitions for use by the
    internal low-level v4l2 drivers.
    Each ioctl begins with VIDIOC_INT_ to clearly mark that it is an internal
    define,

    Copyright (C) 2005  Hans Verkuil <hverkuil@xs4all.nl>

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

#ifndef V4L2_COMMON_H_
#define V4L2_COMMON_H_

#include <media/v4l2-dev.h>

/* Common printk constucts for v4l-i2c drivers. These macros create a unique
   prefix consisting of the driver name, the adapter number and the i2c
   address. */
#define v4l_printk(level, name, adapter, addr, fmt, arg...) \
	printk(level "%s %d-%04x: " fmt, name, i2c_adapter_id(adapter), addr , ## arg)

#define v4l_client_printk(level, client, fmt, arg...)			    \
	v4l_printk(level, (client)->dev.driver->name, (client)->adapter, \
		   (client)->addr, fmt , ## arg)

#define v4l_err(client, fmt, arg...) \
	v4l_client_printk(KERN_ERR, client, fmt , ## arg)

#define v4l_warn(client, fmt, arg...) \
	v4l_client_printk(KERN_WARNING, client, fmt , ## arg)

#define v4l_info(client, fmt, arg...) \
	v4l_client_printk(KERN_INFO, client, fmt , ## arg)

/* These three macros assume that the debug level is set with a module
   parameter called 'debug'. */
#define v4l_dbg(level, debug, client, fmt, arg...)			     \
	do { 								     \
		if (debug >= (level))					     \
			v4l_client_printk(KERN_DEBUG, client, fmt , ## arg); \
	} while (0)

/* ------------------------------------------------------------------------- */

/* These printk constructs can be used with v4l2_device and v4l2_subdev */
#define v4l2_printk(level, dev, fmt, arg...) \
	printk(level "%s: " fmt, (dev)->name , ## arg)

#define v4l2_err(dev, fmt, arg...) \
	v4l2_printk(KERN_ERR, dev, fmt , ## arg)

#define v4l2_warn(dev, fmt, arg...) \
	v4l2_printk(KERN_WARNING, dev, fmt , ## arg)

#define v4l2_info(dev, fmt, arg...) \
	v4l2_printk(KERN_INFO, dev, fmt , ## arg)

/* These three macros assume that the debug level is set with a module
   parameter called 'debug'. */
#define v4l2_dbg(level, debug, dev, fmt, arg...)			\
	do { 								\
		if (debug >= (level))					\
			v4l2_printk(KERN_DEBUG, dev, fmt , ## arg); 	\
	} while (0)

/**
 * v4l2_ctrl_query_fill- Fill in a struct v4l2_queryctrl
 *
 * @qctrl: pointer to the &struct v4l2_queryctrl to be filled
 * @min: minimum value for the control
 * @max: maximum value for the control
 * @step: control step
 * @def: default value for the control
 *
 * Fills the &struct v4l2_queryctrl fields for the query control.
 *
 * .. note::
 *
 *    This function assumes that the @qctrl->id field is filled.
 *
 * Returns -EINVAL if the control is not known by the V4L2 core, 0 on success.
 */

int v4l2_ctrl_query_fill(struct v4l2_queryctrl *qctrl,
			 s32 min, s32 max, s32 step, s32 def);

/* ------------------------------------------------------------------------- */

/* I2C Helper functions */

struct i2c_driver;
struct i2c_adapter;
struct i2c_client;
struct i2c_device_id;
struct v4l2_device;
struct v4l2_subdev;
struct v4l2_subdev_ops;

/**
 * v4l2_i2c_new_subdev - Load an i2c module and return an initialized
 *	&struct v4l2_subdev.
 *
 * @v4l2_dev: pointer to &struct v4l2_device
 * @adapter: pointer to struct i2c_adapter
 * @client_type:  name of the chip that's on the adapter.
 * @addr: I2C address. If zero, it will use @probe_addrs
 * @probe_addrs: array with a list of address. The last entry at such
 * 	array should be %I2C_CLIENT_END.
 *
 * returns a &struct v4l2_subdev pointer.
 */
struct v4l2_subdev *v4l2_i2c_new_subdev(struct v4l2_device *v4l2_dev,
		struct i2c_adapter *adapter, const char *client_type,
		u8 addr, const unsigned short *probe_addrs);

struct i2c_board_info;

/**
 * v4l2_i2c_new_subdev_board - Load an i2c module and return an initialized
 *	&struct v4l2_subdev.
 *
 * @v4l2_dev: pointer to &struct v4l2_device
 * @adapter: pointer to struct i2c_adapter
 * @info: pointer to struct i2c_board_info used to replace the irq,
 *	 platform_data and addr arguments.
 * @probe_addrs: array with a list of address. The last entry at such
 * 	array should be %I2C_CLIENT_END.
 *
 * returns a &struct v4l2_subdev pointer.
 */
struct v4l2_subdev *v4l2_i2c_new_subdev_board(struct v4l2_device *v4l2_dev,
		struct i2c_adapter *adapter, struct i2c_board_info *info,
		const unsigned short *probe_addrs);

/**
 * v4l2_i2c_subdev_init - Initializes a &struct v4l2_subdev with data from
 *	an i2c_client struct.
 *
 * @sd: pointer to &struct v4l2_subdev
 * @client: pointer to struct i2c_client
 * @ops: pointer to &struct v4l2_subdev_ops
 */
void v4l2_i2c_subdev_init(struct v4l2_subdev *sd, struct i2c_client *client,
		const struct v4l2_subdev_ops *ops);

/**
 * v4l2_i2c_subdev_addr - returns i2c client address of &struct v4l2_subdev.
 *
 * @sd: pointer to &struct v4l2_subdev
 *
 * Returns the address of an I2C sub-device
 */
unsigned short v4l2_i2c_subdev_addr(struct v4l2_subdev *sd);

enum v4l2_i2c_tuner_type {
	ADDRS_RADIO,	/* Radio tuner addresses */
	ADDRS_DEMOD,	/* Demod tuner addresses */
	ADDRS_TV,	/* TV tuner addresses */
	/* TV tuner addresses if demod is present, this excludes
	   addresses used by the demodulator from the list of
	   candidates. */
	ADDRS_TV_WITH_DEMOD,
};
/* Return a list of I2C tuner addresses to probe. Use only if the tuner
   addresses are unknown. */
const unsigned short *v4l2_i2c_tuner_addrs(enum v4l2_i2c_tuner_type type);

/* ------------------------------------------------------------------------- */

/* SPI Helper functions */
#if defined(CONFIG_SPI)

#include <linux/spi/spi.h>

struct spi_device;

/**
 *  v4l2_spi_new_subdev - Load an spi module and return an initialized
 *	&struct v4l2_subdev.
 *
 *
 * @v4l2_dev: pointer to &struct v4l2_device.
 * @master: pointer to struct spi_master.
 * @info: pointer to struct spi_board_info.
 *
 * returns a &struct v4l2_subdev pointer.
 */
struct v4l2_subdev *v4l2_spi_new_subdev(struct v4l2_device *v4l2_dev,
		struct spi_master *master, struct spi_board_info *info);

/**
 * v4l2_spi_subdev_init - Initialize a v4l2_subdev with data from an
 *	spi_device struct.
 *
 * @sd: pointer to &struct v4l2_subdev
 * @spi: pointer to struct spi_device.
 * @ops: pointer to &struct v4l2_subdev_ops
 */
void v4l2_spi_subdev_init(struct v4l2_subdev *sd, struct spi_device *spi,
		const struct v4l2_subdev_ops *ops);
#endif

/* ------------------------------------------------------------------------- */

/* Note: these remaining ioctls/structs should be removed as well, but they are
   still used in tuner-simple.c (TUNER_SET_CONFIG), cx18/ivtv (RESET) and
   v4l2-int-device.h (v4l2_routing). To remove these ioctls some more cleanup
   is needed in those modules. */

/* s_config */
struct v4l2_priv_tun_config {
	int tuner;
	void *priv;
};
#define TUNER_SET_CONFIG           _IOW('d', 92, struct v4l2_priv_tun_config)

#define VIDIOC_INT_RESET            	_IOW ('d', 102, u32)

struct v4l2_routing {
	u32 input;
	u32 output;
};

/* ------------------------------------------------------------------------- */

/* Miscellaneous helper functions */

void v4l_bound_align_image(unsigned int *w, unsigned int wmin,
			   unsigned int wmax, unsigned int walign,
			   unsigned int *h, unsigned int hmin,
			   unsigned int hmax, unsigned int halign,
			   unsigned int salign);

struct v4l2_discrete_probe {
	const struct v4l2_frmsize_discrete	*sizes;
	int					num_sizes;
};

const struct v4l2_frmsize_discrete *v4l2_find_nearest_format(
		const struct v4l2_discrete_probe *probe,
		s32 width, s32 height);

void v4l2_get_timestamp(struct timeval *tv);

#endif /* V4L2_COMMON_H_ */
