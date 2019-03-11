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

/* Common printk constructs for v4l-i2c drivers. These macros create a unique
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
	do {								     \
		if (debug >= (level))					     \
			v4l_client_printk(KERN_DEBUG, client, fmt , ## arg); \
	} while (0)

/* Add a version of v4l_dbg to be used on drivers using dev_foo() macros */
#define dev_dbg_lvl(__dev, __level, __debug, __fmt, __arg...)		\
	do {								\
		if (__debug >= (__level))				\
			dev_printk(KERN_DEBUG, __dev, __fmt, ##__arg);	\
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
	do {								\
		if (debug >= (level))					\
			v4l2_printk(KERN_DEBUG, dev, fmt , ## arg);	\
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
 *	array should be %I2C_CLIENT_END.
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
 *	array should be %I2C_CLIENT_END.
 *
 * returns a &struct v4l2_subdev pointer.
 */
struct v4l2_subdev *v4l2_i2c_new_subdev_board(struct v4l2_device *v4l2_dev,
		struct i2c_adapter *adapter, struct i2c_board_info *info,
		const unsigned short *probe_addrs);

/**
 * v4l2_i2c_subdev_set_name - Set name for an I²C sub-device
 *
 * @sd: pointer to &struct v4l2_subdev
 * @client: pointer to struct i2c_client
 * @devname: the name of the device; if NULL, the I²C device's name will be used
 * @postfix: sub-device specific string to put right after the I²C device name;
 *	     may be NULL
 */
void v4l2_i2c_subdev_set_name(struct v4l2_subdev *sd, struct i2c_client *client,
			      const char *devname, const char *postfix);

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

/**
 * enum v4l2_i2c_tuner_type - specifies the range of tuner address that
 *	should be used when seeking for I2C devices.
 *
 * @ADDRS_RADIO:		Radio tuner addresses.
 *				Represent the following I2C addresses:
 *				0x10 (if compiled with tea5761 support)
 *				and 0x60.
 * @ADDRS_DEMOD:		Demod tuner addresses.
 *				Represent the following I2C addresses:
 *				0x42, 0x43, 0x4a and 0x4b.
 * @ADDRS_TV:			TV tuner addresses.
 *				Represent the following I2C addresses:
 *				0x42, 0x43, 0x4a, 0x4b, 0x60, 0x61, 0x62,
 *				0x63 and 0x64.
 * @ADDRS_TV_WITH_DEMOD:	TV tuner addresses if demod is present, this
 *				excludes addresses used by the demodulator
 *				from the list of candidates.
 *				Represent the following I2C addresses:
 *				0x60, 0x61, 0x62, 0x63 and 0x64.
 *
 * NOTE: All I2C addresses above use the 7-bit notation.
 */
enum v4l2_i2c_tuner_type {
	ADDRS_RADIO,
	ADDRS_DEMOD,
	ADDRS_TV,
	ADDRS_TV_WITH_DEMOD,
};
/**
 * v4l2_i2c_tuner_addrs - Return a list of I2C tuner addresses to probe.
 *
 * @type: type of the tuner to seek, as defined by
 *	  &enum v4l2_i2c_tuner_type.
 *
 * NOTE: Use only if the tuner addresses are unknown.
 */
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

/*
 * FIXME: these remaining ioctls/structs should be removed as well, but they
 * are still used in tuner-simple.c (TUNER_SET_CONFIG) and cx18/ivtv (RESET).
 * To remove these ioctls some more cleanup is needed in those modules.
 *
 * It doesn't make much sense on documenting them, as what we really want is
 * to get rid of them.
 */

/* s_config */
struct v4l2_priv_tun_config {
	int tuner;
	void *priv;
};
#define TUNER_SET_CONFIG           _IOW('d', 92, struct v4l2_priv_tun_config)

#define VIDIOC_INT_RESET		_IOW ('d', 102, u32)

/* ------------------------------------------------------------------------- */

/* Miscellaneous helper functions */

/**
 * v4l_bound_align_image - adjust video dimensions according to
 *	a given constraints.
 *
 * @width:	pointer to width that will be adjusted if needed.
 * @wmin:	minimum width.
 * @wmax:	maximum width.
 * @walign:	least significant bit on width.
 * @height:	pointer to height that will be adjusted if needed.
 * @hmin:	minimum height.
 * @hmax:	maximum height.
 * @halign:	least significant bit on height.
 * @salign:	least significant bit for the image size (e. g.
 *		:math:`width * height`).
 *
 * Clip an image to have @width between @wmin and @wmax, and @height between
 * @hmin and @hmax, inclusive.
 *
 * Additionally, the @width will be a multiple of :math:`2^{walign}`,
 * the @height will be a multiple of :math:`2^{halign}`, and the overall
 * size :math:`width * height` will be a multiple of :math:`2^{salign}`.
 *
 * .. note::
 *
 *    #. The clipping rectangle may be shrunk or enlarged to fit the alignment
 *       constraints.
 *    #. @wmax must not be smaller than @wmin.
 *    #. @hmax must not be smaller than @hmin.
 *    #. The alignments must not be so high there are no possible image
 *       sizes within the allowed bounds.
 *    #. @wmin and @hmin must be at least 1 (don't use 0).
 *    #. For @walign, @halign and @salign, if you don't care about a certain
 *       alignment, specify ``0``, as :math:`2^0 = 1` and one byte alignment
 *       is equivalent to no alignment.
 *    #. If you only want to adjust downward, specify a maximum that's the
 *       same as the initial value.
 */
void v4l_bound_align_image(unsigned int *width, unsigned int wmin,
			   unsigned int wmax, unsigned int walign,
			   unsigned int *height, unsigned int hmin,
			   unsigned int hmax, unsigned int halign,
			   unsigned int salign);

/**
 * v4l2_find_nearest_size - Find the nearest size among a discrete
 *	set of resolutions contained in an array of a driver specific struct.
 *
 * @array: a driver specific array of image sizes
 * @array_size: the length of the driver specific array of image sizes
 * @width_field: the name of the width field in the driver specific struct
 * @height_field: the name of the height field in the driver specific struct
 * @width: desired width.
 * @height: desired height.
 *
 * Finds the closest resolution to minimize the width and height differences
 * between what requested and the supported resolutions. The size of the width
 * and height fields in the driver specific must equal to that of u32, i.e. four
 * bytes.
 *
 * Returns the best match or NULL if the length of the array is zero.
 */
#define v4l2_find_nearest_size(array, array_size, width_field, height_field, \
			       width, height)				\
	({								\
		BUILD_BUG_ON(sizeof((array)->width_field) != sizeof(u32) || \
			     sizeof((array)->height_field) != sizeof(u32)); \
		(typeof(&(array)[0]))__v4l2_find_nearest_size(		\
			(array), array_size, sizeof(*(array)),		\
			offsetof(typeof(*(array)), width_field),	\
			offsetof(typeof(*(array)), height_field),	\
			width, height);					\
	})
const void *
__v4l2_find_nearest_size(const void *array, size_t array_size,
			 size_t entry_size, size_t width_offset,
			 size_t height_offset, s32 width, s32 height);

/**
 * v4l2_g_parm_cap - helper routine for vidioc_g_parm to fill this in by
 *      calling the g_frame_interval op of the given subdev. It only works
 *      for V4L2_BUF_TYPE_VIDEO_CAPTURE(_MPLANE), hence the _cap in the
 *      function name.
 *
 * @vdev: the struct video_device pointer. Used to determine the device caps.
 * @sd: the sub-device pointer.
 * @a: the VIDIOC_G_PARM argument.
 */
int v4l2_g_parm_cap(struct video_device *vdev,
		    struct v4l2_subdev *sd, struct v4l2_streamparm *a);

/**
 * v4l2_s_parm_cap - helper routine for vidioc_s_parm to fill this in by
 *      calling the s_frame_interval op of the given subdev. It only works
 *      for V4L2_BUF_TYPE_VIDEO_CAPTURE(_MPLANE), hence the _cap in the
 *      function name.
 *
 * @vdev: the struct video_device pointer. Used to determine the device caps.
 * @sd: the sub-device pointer.
 * @a: the VIDIOC_S_PARM argument.
 */
int v4l2_s_parm_cap(struct video_device *vdev,
		    struct v4l2_subdev *sd, struct v4l2_streamparm *a);

/* Compare two v4l2_fract structs */
#define V4L2_FRACT_COMPARE(a, OP, b)			\
	((u64)(a).numerator * (b).denominator OP	\
	(u64)(b).numerator * (a).denominator)

#endif /* V4L2_COMMON_H_ */
