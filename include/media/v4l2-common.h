/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
    v4l2 common internal API header

    This header contains internal shared ioctl definitions for use by the
    internal low-level v4l2 drivers.
    Each ioctl begins with VIDIOC_INT_ to clearly mark that it is an internal
    define,

    Copyright (C) 2005  Hans Verkuil <hverkuil@xs4all.nl>

 */

#ifndef V4L2_COMMON_H_
#define V4L2_COMMON_H_

#include <linux/time.h>
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

struct v4l2_device;
struct v4l2_subdev;
struct v4l2_subdev_ops;

/* I2C Helper functions */
#include <linux/i2c.h>

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

#if defined(CONFIG_VIDEO_V4L2_I2C)

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
 * @devname: the name of the device; if NULL, the I²C device drivers's name
 *           will be used
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
 * v4l2_i2c_tuner_addrs - Return a list of I2C tuner addresses to probe.
 *
 * @type: type of the tuner to seek, as defined by
 *	  &enum v4l2_i2c_tuner_type.
 *
 * NOTE: Use only if the tuner addresses are unknown.
 */
const unsigned short *v4l2_i2c_tuner_addrs(enum v4l2_i2c_tuner_type type);

/**
 * v4l2_i2c_subdev_unregister - Unregister a v4l2_subdev
 *
 * @sd: pointer to &struct v4l2_subdev
 */
void v4l2_i2c_subdev_unregister(struct v4l2_subdev *sd);

#else

static inline struct v4l2_subdev *
v4l2_i2c_new_subdev(struct v4l2_device *v4l2_dev,
		    struct i2c_adapter *adapter, const char *client_type,
		    u8 addr, const unsigned short *probe_addrs)
{
	return NULL;
}

static inline struct v4l2_subdev *
v4l2_i2c_new_subdev_board(struct v4l2_device *v4l2_dev,
			  struct i2c_adapter *adapter, struct i2c_board_info *info,
			  const unsigned short *probe_addrs)
{
	return NULL;
}

static inline void
v4l2_i2c_subdev_set_name(struct v4l2_subdev *sd, struct i2c_client *client,
			 const char *devname, const char *postfix)
{}

static inline void
v4l2_i2c_subdev_init(struct v4l2_subdev *sd, struct i2c_client *client,
		     const struct v4l2_subdev_ops *ops)
{}

static inline unsigned short v4l2_i2c_subdev_addr(struct v4l2_subdev *sd)
{
	return I2C_CLIENT_END;
}

static inline const unsigned short *
v4l2_i2c_tuner_addrs(enum v4l2_i2c_tuner_type type)
{
	return NULL;
}

static inline void v4l2_i2c_subdev_unregister(struct v4l2_subdev *sd)
{}

#endif

/* ------------------------------------------------------------------------- */

/* SPI Helper functions */

#include <linux/spi/spi.h>

#if defined(CONFIG_SPI)

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

/**
 * v4l2_spi_subdev_unregister - Unregister a v4l2_subdev
 *
 * @sd: pointer to &struct v4l2_subdev
 */
void v4l2_spi_subdev_unregister(struct v4l2_subdev *sd);

#else

static inline struct v4l2_subdev *
v4l2_spi_new_subdev(struct v4l2_device *v4l2_dev,
		    struct spi_master *master, struct spi_board_info *info)
{
	return NULL;
}

static inline void
v4l2_spi_subdev_init(struct v4l2_subdev *sd, struct spi_device *spi,
		     const struct v4l2_subdev_ops *ops)
{}

static inline void v4l2_spi_subdev_unregister(struct v4l2_subdev *sd)
{}
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

/* ------------------------------------------------------------------------- */

/* Pixel format and FourCC helpers */

/**
 * enum v4l2_pixel_encoding - specifies the pixel encoding value
 *
 * @V4L2_PIXEL_ENC_UNKNOWN:	Pixel encoding is unknown/un-initialized
 * @V4L2_PIXEL_ENC_YUV:		Pixel encoding is YUV
 * @V4L2_PIXEL_ENC_RGB:		Pixel encoding is RGB
 * @V4L2_PIXEL_ENC_BAYER:	Pixel encoding is Bayer
 */
enum v4l2_pixel_encoding {
	V4L2_PIXEL_ENC_UNKNOWN = 0,
	V4L2_PIXEL_ENC_YUV = 1,
	V4L2_PIXEL_ENC_RGB = 2,
	V4L2_PIXEL_ENC_BAYER = 3,
};

/**
 * struct v4l2_format_info - information about a V4L2 format
 * @format: 4CC format identifier (V4L2_PIX_FMT_*)
 * @pixel_enc: Pixel encoding (see enum v4l2_pixel_encoding above)
 * @mem_planes: Number of memory planes, which includes the alpha plane (1 to 4).
 * @comp_planes: Number of component planes, which includes the alpha plane (1 to 4).
 * @bpp: Array of per-plane bytes per pixel
 * @hdiv: Horizontal chroma subsampling factor
 * @vdiv: Vertical chroma subsampling factor
 * @block_w: Per-plane macroblock pixel width (optional)
 * @block_h: Per-plane macroblock pixel height (optional)
 */
struct v4l2_format_info {
	u32 format;
	u8 pixel_enc;
	u8 mem_planes;
	u8 comp_planes;
	u8 bpp[4];
	u8 hdiv;
	u8 vdiv;
	u8 block_w[4];
	u8 block_h[4];
};

static inline bool v4l2_is_format_rgb(const struct v4l2_format_info *f)
{
	return f && f->pixel_enc == V4L2_PIXEL_ENC_RGB;
}

static inline bool v4l2_is_format_yuv(const struct v4l2_format_info *f)
{
	return f && f->pixel_enc == V4L2_PIXEL_ENC_YUV;
}

static inline bool v4l2_is_format_bayer(const struct v4l2_format_info *f)
{
	return f && f->pixel_enc == V4L2_PIXEL_ENC_BAYER;
}

const struct v4l2_format_info *v4l2_format_info(u32 format);
void v4l2_apply_frmsize_constraints(u32 *width, u32 *height,
				    const struct v4l2_frmsize_stepwise *frmsize);
int v4l2_fill_pixfmt(struct v4l2_pix_format *pixfmt, u32 pixelformat,
		     u32 width, u32 height);
int v4l2_fill_pixfmt_mp(struct v4l2_pix_format_mplane *pixfmt, u32 pixelformat,
			u32 width, u32 height);

/**
 * v4l2_get_link_freq - Get link rate from transmitter
 *
 * @handler: The transmitter's control handler
 * @mul: The multiplier between pixel rate and link frequency. Bits per pixel on
 *	 D-PHY, samples per clock on parallel. 0 otherwise.
 * @div: The divisor between pixel rate and link frequency. Number of data lanes
 *	 times two on D-PHY, 1 on parallel. 0 otherwise.
 *
 * This function is intended for obtaining the link frequency from the
 * transmitter sub-devices. It returns the link rate, either from the
 * V4L2_CID_LINK_FREQ control implemented by the transmitter, or value
 * calculated based on the V4L2_CID_PIXEL_RATE implemented by the transmitter.
 *
 * Returns link frequency on success, otherwise a negative error code:
 *	-ENOENT: Link frequency or pixel rate control not found
 *	-EINVAL: Invalid link frequency value
 */
s64 v4l2_get_link_freq(struct v4l2_ctrl_handler *handler, unsigned int mul,
		       unsigned int div);

static inline u64 v4l2_buffer_get_timestamp(const struct v4l2_buffer *buf)
{
	/*
	 * When the timestamp comes from 32-bit user space, there may be
	 * uninitialized data in tv_usec, so cast it to u32.
	 * Otherwise allow invalid input for backwards compatibility.
	 */
	return buf->timestamp.tv_sec * NSEC_PER_SEC +
		(u32)buf->timestamp.tv_usec * NSEC_PER_USEC;
}

static inline void v4l2_buffer_set_timestamp(struct v4l2_buffer *buf,
					     u64 timestamp)
{
	struct timespec64 ts = ns_to_timespec64(timestamp);

	buf->timestamp.tv_sec  = ts.tv_sec;
	buf->timestamp.tv_usec = ts.tv_nsec / NSEC_PER_USEC;
}

static inline bool v4l2_is_colorspace_valid(__u32 colorspace)
{
	return colorspace > V4L2_COLORSPACE_DEFAULT &&
	       colorspace <= V4L2_COLORSPACE_DCI_P3;
}

static inline bool v4l2_is_xfer_func_valid(__u32 xfer_func)
{
	return xfer_func > V4L2_XFER_FUNC_DEFAULT &&
	       xfer_func <= V4L2_XFER_FUNC_SMPTE2084;
}

static inline bool v4l2_is_ycbcr_enc_valid(__u8 ycbcr_enc)
{
	return ycbcr_enc > V4L2_YCBCR_ENC_DEFAULT &&
	       ycbcr_enc <= V4L2_YCBCR_ENC_SMPTE240M;
}

static inline bool v4l2_is_hsv_enc_valid(__u8 hsv_enc)
{
	return hsv_enc == V4L2_HSV_ENC_180 || hsv_enc == V4L2_HSV_ENC_256;
}

static inline bool v4l2_is_quant_valid(__u8 quantization)
{
	return quantization == V4L2_QUANTIZATION_FULL_RANGE ||
	       quantization == V4L2_QUANTIZATION_LIM_RANGE;
}

#endif /* V4L2_COMMON_H_ */
