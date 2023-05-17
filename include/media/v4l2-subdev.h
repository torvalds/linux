/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  V4L2 sub-device support header.
 *
 *  Copyright (C) 2008  Hans Verkuil <hverkuil@xs4all.nl>
 */

#ifndef _V4L2_SUBDEV_H
#define _V4L2_SUBDEV_H

#include <linux/types.h>
#include <linux/v4l2-subdev.h>
#include <media/media-entity.h>
#include <media/v4l2-async.h>
#include <media/v4l2-common.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-mediabus.h>

/* generic v4l2_device notify callback notification values */
#define V4L2_SUBDEV_IR_RX_NOTIFY		_IOW('v', 0, u32)
#define V4L2_SUBDEV_IR_RX_FIFO_SERVICE_REQ	0x00000001
#define V4L2_SUBDEV_IR_RX_END_OF_RX_DETECTED	0x00000002
#define V4L2_SUBDEV_IR_RX_HW_FIFO_OVERRUN	0x00000004
#define V4L2_SUBDEV_IR_RX_SW_FIFO_OVERRUN	0x00000008

#define V4L2_SUBDEV_IR_TX_NOTIFY		_IOW('v', 1, u32)
#define V4L2_SUBDEV_IR_TX_FIFO_SERVICE_REQ	0x00000001

#define	V4L2_DEVICE_NOTIFY_EVENT		_IOW('v', 2, struct v4l2_event)

struct v4l2_device;
struct v4l2_ctrl_handler;
struct v4l2_event;
struct v4l2_event_subscription;
struct v4l2_fh;
struct v4l2_subdev;
struct v4l2_subdev_fh;
struct tuner_setup;
struct v4l2_mbus_frame_desc;
struct led_classdev;

/**
 * struct v4l2_decode_vbi_line - used to decode_vbi_line
 *
 * @is_second_field: Set to 0 for the first (odd) field;
 *	set to 1 for the second (even) field.
 * @p: Pointer to the sliced VBI data from the decoder. On exit, points to
 *	the start of the payload.
 * @line: Line number of the sliced VBI data (1-23)
 * @type: VBI service type (V4L2_SLICED_*). 0 if no service found
 */
struct v4l2_decode_vbi_line {
	u32 is_second_field;
	u8 *p;
	u32 line;
	u32 type;
};

/*
 * Sub-devices are devices that are connected somehow to the main bridge
 * device. These devices are usually audio/video muxers/encoders/decoders or
 * sensors and webcam controllers.
 *
 * Usually these devices are controlled through an i2c bus, but other buses
 * may also be used.
 *
 * The v4l2_subdev struct provides a way of accessing these devices in a
 * generic manner. Most operations that these sub-devices support fall in
 * a few categories: core ops, audio ops, video ops and tuner ops.
 *
 * More categories can be added if needed, although this should remain a
 * limited set (no more than approx. 8 categories).
 *
 * Each category has its own set of ops that subdev drivers can implement.
 *
 * A subdev driver can leave the pointer to the category ops NULL if
 * it does not implement them (e.g. an audio subdev will generally not
 * implement the video category ops). The exception is the core category:
 * this must always be present.
 *
 * These ops are all used internally so it is no problem to change, remove
 * or add ops or move ops from one to another category. Currently these
 * ops are based on the original ioctls, but since ops are not limited to
 * one argument there is room for improvement here once all i2c subdev
 * drivers are converted to use these ops.
 */

/*
 * Core ops: it is highly recommended to implement at least these ops:
 *
 * log_status
 * g_register
 * s_register
 *
 * This provides basic debugging support.
 *
 * The ioctl ops is meant for generic ioctl-like commands. Depending on
 * the use-case it might be better to use subdev-specific ops (currently
 * not yet implemented) since ops provide proper type-checking.
 */

/**
 * enum v4l2_subdev_io_pin_bits - Subdevice external IO pin configuration
 *	bits
 *
 * @V4L2_SUBDEV_IO_PIN_DISABLE: disables a pin config. ENABLE assumed.
 * @V4L2_SUBDEV_IO_PIN_OUTPUT: set it if pin is an output.
 * @V4L2_SUBDEV_IO_PIN_INPUT: set it if pin is an input.
 * @V4L2_SUBDEV_IO_PIN_SET_VALUE: to set the output value via
 *				  &struct v4l2_subdev_io_pin_config->value.
 * @V4L2_SUBDEV_IO_PIN_ACTIVE_LOW: pin active is bit 0.
 *				   Otherwise, ACTIVE HIGH is assumed.
 */
enum v4l2_subdev_io_pin_bits {
	V4L2_SUBDEV_IO_PIN_DISABLE	= 0,
	V4L2_SUBDEV_IO_PIN_OUTPUT	= 1,
	V4L2_SUBDEV_IO_PIN_INPUT	= 2,
	V4L2_SUBDEV_IO_PIN_SET_VALUE	= 3,
	V4L2_SUBDEV_IO_PIN_ACTIVE_LOW	= 4,
};

/**
 * struct v4l2_subdev_io_pin_config - Subdevice external IO pin configuration
 *
 * @flags: bitmask with flags for this pin's config, whose bits are defined by
 *	   &enum v4l2_subdev_io_pin_bits.
 * @pin: Chip external IO pin to configure
 * @function: Internal signal pad/function to route to IO pin
 * @value: Initial value for pin - e.g. GPIO output value
 * @strength: Pin drive strength
 */
struct v4l2_subdev_io_pin_config {
	u32 flags;
	u8 pin;
	u8 function;
	u8 value;
	u8 strength;
};

/**
 * struct v4l2_subdev_core_ops - Define core ops callbacks for subdevs
 *
 * @log_status: callback for VIDIOC_LOG_STATUS() ioctl handler code.
 *
 * @s_io_pin_config: configure one or more chip I/O pins for chips that
 *	multiplex different internal signal pads out to IO pins.  This function
 *	takes a pointer to an array of 'n' pin configuration entries, one for
 *	each pin being configured.  This function could be called at times
 *	other than just subdevice initialization.
 *
 * @init: initialize the sensor registers to some sort of reasonable default
 *	values. Do not use for new drivers and should be removed in existing
 *	drivers.
 *
 * @load_fw: load firmware.
 *
 * @reset: generic reset command. The argument selects which subsystems to
 *	reset. Passing 0 will always reset the whole chip. Do not use for new
 *	drivers without discussing this first on the linux-media mailinglist.
 *	There should be no reason normally to reset a device.
 *
 * @s_gpio: set GPIO pins. Very simple right now, might need to be extended with
 *	a direction argument if needed.
 *
 * @command: called by in-kernel drivers in order to call functions internal
 *	   to subdev drivers driver that have a separate callback.
 *
 * @ioctl: called at the end of ioctl() syscall handler at the V4L2 core.
 *	   used to provide support for private ioctls used on the driver.
 *
 * @compat_ioctl32: called when a 32 bits application uses a 64 bits Kernel,
 *		    in order to fix data passed from/to userspace.
 *
 * @g_register: callback for VIDIOC_DBG_G_REGISTER() ioctl handler code.
 *
 * @s_register: callback for VIDIOC_DBG_S_REGISTER() ioctl handler code.
 *
 * @s_power: puts subdevice in power saving mode (on == 0) or normal operation
 *	mode (on == 1). DEPRECATED. See
 *	Documentation/driver-api/media/camera-sensor.rst . pre_streamon and
 *	post_streamoff callbacks can be used for e.g. setting the bus to LP-11
 *	mode before s_stream is called.
 *
 * @interrupt_service_routine: Called by the bridge chip's interrupt service
 *	handler, when an interrupt status has be raised due to this subdev,
 *	so that this subdev can handle the details.  It may schedule work to be
 *	performed later.  It must not sleep. **Called from an IRQ context**.
 *
 * @subscribe_event: used by the drivers to request the control framework that
 *		     for it to be warned when the value of a control changes.
 *
 * @unsubscribe_event: remove event subscription from the control framework.
 */
struct v4l2_subdev_core_ops {
	int (*log_status)(struct v4l2_subdev *sd);
	int (*s_io_pin_config)(struct v4l2_subdev *sd, size_t n,
				      struct v4l2_subdev_io_pin_config *pincfg);
	int (*init)(struct v4l2_subdev *sd, u32 val);
	int (*load_fw)(struct v4l2_subdev *sd);
	int (*reset)(struct v4l2_subdev *sd, u32 val);
	int (*s_gpio)(struct v4l2_subdev *sd, u32 val);
	long (*command)(struct v4l2_subdev *sd, unsigned int cmd, void *arg);
	long (*ioctl)(struct v4l2_subdev *sd, unsigned int cmd, void *arg);
#ifdef CONFIG_COMPAT
	long (*compat_ioctl32)(struct v4l2_subdev *sd, unsigned int cmd,
			       unsigned long arg);
#endif
#ifdef CONFIG_VIDEO_ADV_DEBUG
	int (*g_register)(struct v4l2_subdev *sd, struct v4l2_dbg_register *reg);
	int (*s_register)(struct v4l2_subdev *sd, const struct v4l2_dbg_register *reg);
#endif
	int (*s_power)(struct v4l2_subdev *sd, int on);
	int (*interrupt_service_routine)(struct v4l2_subdev *sd,
						u32 status, bool *handled);
	int (*subscribe_event)(struct v4l2_subdev *sd, struct v4l2_fh *fh,
			       struct v4l2_event_subscription *sub);
	int (*unsubscribe_event)(struct v4l2_subdev *sd, struct v4l2_fh *fh,
				 struct v4l2_event_subscription *sub);
};

/**
 * struct v4l2_subdev_tuner_ops - Callbacks used when v4l device was opened
 *	in radio mode.
 *
 * @standby: puts the tuner in standby mode. It will be woken up
 *	     automatically the next time it is used.
 *
 * @s_radio: callback that switches the tuner to radio mode.
 *	     drivers should explicitly call it when a tuner ops should
 *	     operate on radio mode, before being able to handle it.
 *	     Used on devices that have both AM/FM radio receiver and TV.
 *
 * @s_frequency: callback for VIDIOC_S_FREQUENCY() ioctl handler code.
 *
 * @g_frequency: callback for VIDIOC_G_FREQUENCY() ioctl handler code.
 *		 freq->type must be filled in. Normally done by video_ioctl2()
 *		 or the bridge driver.
 *
 * @enum_freq_bands: callback for VIDIOC_ENUM_FREQ_BANDS() ioctl handler code.
 *
 * @g_tuner: callback for VIDIOC_G_TUNER() ioctl handler code.
 *
 * @s_tuner: callback for VIDIOC_S_TUNER() ioctl handler code. @vt->type must be
 *	     filled in. Normally done by video_ioctl2 or the
 *	     bridge driver.
 *
 * @g_modulator: callback for VIDIOC_G_MODULATOR() ioctl handler code.
 *
 * @s_modulator: callback for VIDIOC_S_MODULATOR() ioctl handler code.
 *
 * @s_type_addr: sets tuner type and its I2C addr.
 *
 * @s_config: sets tda9887 specific stuff, like port1, port2 and qss
 *
 * .. note::
 *
 *	On devices that have both AM/FM and TV, it is up to the driver
 *	to explicitly call s_radio when the tuner should be switched to
 *	radio mode, before handling other &struct v4l2_subdev_tuner_ops
 *	that would require it. An example of such usage is::
 *
 *	  static void s_frequency(void *priv, const struct v4l2_frequency *f)
 *	  {
 *		...
 *		if (f.type == V4L2_TUNER_RADIO)
 *			v4l2_device_call_all(v4l2_dev, 0, tuner, s_radio);
 *		...
 *		v4l2_device_call_all(v4l2_dev, 0, tuner, s_frequency);
 *	  }
 */
struct v4l2_subdev_tuner_ops {
	int (*standby)(struct v4l2_subdev *sd);
	int (*s_radio)(struct v4l2_subdev *sd);
	int (*s_frequency)(struct v4l2_subdev *sd, const struct v4l2_frequency *freq);
	int (*g_frequency)(struct v4l2_subdev *sd, struct v4l2_frequency *freq);
	int (*enum_freq_bands)(struct v4l2_subdev *sd, struct v4l2_frequency_band *band);
	int (*g_tuner)(struct v4l2_subdev *sd, struct v4l2_tuner *vt);
	int (*s_tuner)(struct v4l2_subdev *sd, const struct v4l2_tuner *vt);
	int (*g_modulator)(struct v4l2_subdev *sd, struct v4l2_modulator *vm);
	int (*s_modulator)(struct v4l2_subdev *sd, const struct v4l2_modulator *vm);
	int (*s_type_addr)(struct v4l2_subdev *sd, struct tuner_setup *type);
	int (*s_config)(struct v4l2_subdev *sd, const struct v4l2_priv_tun_config *config);
};

/**
 * struct v4l2_subdev_audio_ops - Callbacks used for audio-related settings
 *
 * @s_clock_freq: set the frequency (in Hz) of the audio clock output.
 *	Used to slave an audio processor to the video decoder, ensuring that
 *	audio and video remain synchronized. Usual values for the frequency
 *	are 48000, 44100 or 32000 Hz. If the frequency is not supported, then
 *	-EINVAL is returned.
 *
 * @s_i2s_clock_freq: sets I2S speed in bps. This is used to provide a standard
 *	way to select I2S clock used by driving digital audio streams at some
 *	board designs. Usual values for the frequency are 1024000 and 2048000.
 *	If the frequency is not supported, then %-EINVAL is returned.
 *
 * @s_routing: used to define the input and/or output pins of an audio chip,
 *	and any additional configuration data.
 *	Never attempt to use user-level input IDs (e.g. Composite, S-Video,
 *	Tuner) at this level. An i2c device shouldn't know about whether an
 *	input pin is connected to a Composite connector, become on another
 *	board or platform it might be connected to something else entirely.
 *	The calling driver is responsible for mapping a user-level input to
 *	the right pins on the i2c device.
 *
 * @s_stream: used to notify the audio code that stream will start or has
 *	stopped.
 */
struct v4l2_subdev_audio_ops {
	int (*s_clock_freq)(struct v4l2_subdev *sd, u32 freq);
	int (*s_i2s_clock_freq)(struct v4l2_subdev *sd, u32 freq);
	int (*s_routing)(struct v4l2_subdev *sd, u32 input, u32 output, u32 config);
	int (*s_stream)(struct v4l2_subdev *sd, int enable);
};

/**
 * struct v4l2_mbus_frame_desc_entry_csi2
 *
 * @vc: CSI-2 virtual channel
 * @dt: CSI-2 data type ID
 */
struct v4l2_mbus_frame_desc_entry_csi2 {
	u8 vc;
	u8 dt;
};

/**
 * enum v4l2_mbus_frame_desc_flags - media bus frame description flags
 *
 * @V4L2_MBUS_FRAME_DESC_FL_LEN_MAX:
 *	Indicates that &struct v4l2_mbus_frame_desc_entry->length field
 *	specifies maximum data length.
 * @V4L2_MBUS_FRAME_DESC_FL_BLOB:
 *	Indicates that the format does not have line offsets, i.e.
 *	the receiver should use 1D DMA.
 */
enum v4l2_mbus_frame_desc_flags {
	V4L2_MBUS_FRAME_DESC_FL_LEN_MAX	= BIT(0),
	V4L2_MBUS_FRAME_DESC_FL_BLOB	= BIT(1),
};

/**
 * struct v4l2_mbus_frame_desc_entry - media bus frame description structure
 *
 * @flags:	bitmask flags, as defined by &enum v4l2_mbus_frame_desc_flags.
 * @stream:	stream in routing configuration
 * @pixelcode:	media bus pixel code, valid if @flags
 *		%FRAME_DESC_FL_BLOB is not set.
 * @length:	number of octets per frame, valid if @flags
 *		%V4L2_MBUS_FRAME_DESC_FL_LEN_MAX is set.
 * @bus:	Bus-specific frame descriptor parameters
 * @bus.csi2:	CSI-2-specific bus configuration
 */
struct v4l2_mbus_frame_desc_entry {
	enum v4l2_mbus_frame_desc_flags flags;
	u32 stream;
	u32 pixelcode;
	u32 length;
	union {
		struct v4l2_mbus_frame_desc_entry_csi2 csi2;
	} bus;
};

 /*
  * If this number is too small, it should be dropped altogether and the
  * API switched to a dynamic number of frame descriptor entries.
  */
#define V4L2_FRAME_DESC_ENTRY_MAX	8

/**
 * enum v4l2_mbus_frame_desc_type - media bus frame description type
 *
 * @V4L2_MBUS_FRAME_DESC_TYPE_UNDEFINED:
 *	Undefined frame desc type. Drivers should not use this, it is
 *	for backwards compatibility.
 * @V4L2_MBUS_FRAME_DESC_TYPE_PARALLEL:
 *	Parallel media bus.
 * @V4L2_MBUS_FRAME_DESC_TYPE_CSI2:
 *	CSI-2 media bus. Frame desc parameters must be set in
 *	&struct v4l2_mbus_frame_desc_entry->csi2.
 */
enum v4l2_mbus_frame_desc_type {
	V4L2_MBUS_FRAME_DESC_TYPE_UNDEFINED = 0,
	V4L2_MBUS_FRAME_DESC_TYPE_PARALLEL,
	V4L2_MBUS_FRAME_DESC_TYPE_CSI2,
};

/**
 * struct v4l2_mbus_frame_desc - media bus data frame description
 * @type: type of the bus (enum v4l2_mbus_frame_desc_type)
 * @entry: frame descriptors array
 * @num_entries: number of entries in @entry array
 */
struct v4l2_mbus_frame_desc {
	enum v4l2_mbus_frame_desc_type type;
	struct v4l2_mbus_frame_desc_entry entry[V4L2_FRAME_DESC_ENTRY_MAX];
	unsigned short num_entries;
};

/**
 * enum v4l2_subdev_pre_streamon_flags - Flags for pre_streamon subdev core op
 *
 * @V4L2_SUBDEV_PRE_STREAMON_FL_MANUAL_LP: Set the transmitter to either LP-11
 *	or LP-111 mode before call to s_stream().
 */
enum v4l2_subdev_pre_streamon_flags {
	V4L2_SUBDEV_PRE_STREAMON_FL_MANUAL_LP = BIT(0),
};

/**
 * struct v4l2_subdev_video_ops - Callbacks used when v4l device was opened
 *				  in video mode.
 *
 * @s_routing: see s_routing in audio_ops, except this version is for video
 *	devices.
 *
 * @s_crystal_freq: sets the frequency of the crystal used to generate the
 *	clocks in Hz. An extra flags field allows device specific configuration
 *	regarding clock frequency dividers, etc. If not used, then set flags
 *	to 0. If the frequency is not supported, then -EINVAL is returned.
 *
 * @g_std: callback for VIDIOC_G_STD() ioctl handler code.
 *
 * @s_std: callback for VIDIOC_S_STD() ioctl handler code.
 *
 * @s_std_output: set v4l2_std_id for video OUTPUT devices. This is ignored by
 *	video input devices.
 *
 * @g_std_output: get current standard for video OUTPUT devices. This is ignored
 *	by video input devices.
 *
 * @querystd: callback for VIDIOC_QUERYSTD() ioctl handler code.
 *
 * @g_tvnorms: get &v4l2_std_id with all standards supported by the video
 *	CAPTURE device. This is ignored by video output devices.
 *
 * @g_tvnorms_output: get v4l2_std_id with all standards supported by the video
 *	OUTPUT device. This is ignored by video capture devices.
 *
 * @g_input_status: get input status. Same as the status field in the
 *	&struct v4l2_input
 *
 * @s_stream: start (enabled == 1) or stop (enabled == 0) streaming on the
 *	sub-device. Failure on stop will remove any resources acquired in
 *	streaming start, while the error code is still returned by the driver.
 *	Also see call_s_stream wrapper in v4l2-subdev.c.
 *
 * @g_pixelaspect: callback to return the pixelaspect ratio.
 *
 * @g_frame_interval: callback for VIDIOC_SUBDEV_G_FRAME_INTERVAL()
 *		      ioctl handler code.
 *
 * @s_frame_interval: callback for VIDIOC_SUBDEV_S_FRAME_INTERVAL()
 *		      ioctl handler code.
 *
 * @s_dv_timings: Set custom dv timings in the sub device. This is used
 *	when sub device is capable of setting detailed timing information
 *	in the hardware to generate/detect the video signal.
 *
 * @g_dv_timings: Get custom dv timings in the sub device.
 *
 * @query_dv_timings: callback for VIDIOC_QUERY_DV_TIMINGS() ioctl handler code.
 *
 * @s_rx_buffer: set a host allocated memory buffer for the subdev. The subdev
 *	can adjust @size to a lower value and must not write more data to the
 *	buffer starting at @data than the original value of @size.
 *
 * @pre_streamon: May be called before streaming is actually started, to help
 *	initialising the bus. Current usage is to set a CSI-2 transmitter to
 *	LP-11 or LP-111 mode before streaming. See &enum
 *	v4l2_subdev_pre_streamon_flags.
 *
 *	pre_streamon shall return error if it cannot perform the operation as
 *	indicated by the flags argument. In particular, -EACCES indicates lack
 *	of support for the operation. The caller shall call post_streamoff for
 *	each successful call of pre_streamon.
 *
 * @post_streamoff: Called after streaming is stopped, but if and only if
 *	pre_streamon was called earlier.
 */
struct v4l2_subdev_video_ops {
	int (*s_routing)(struct v4l2_subdev *sd, u32 input, u32 output, u32 config);
	int (*s_crystal_freq)(struct v4l2_subdev *sd, u32 freq, u32 flags);
	int (*g_std)(struct v4l2_subdev *sd, v4l2_std_id *norm);
	int (*s_std)(struct v4l2_subdev *sd, v4l2_std_id norm);
	int (*s_std_output)(struct v4l2_subdev *sd, v4l2_std_id std);
	int (*g_std_output)(struct v4l2_subdev *sd, v4l2_std_id *std);
	int (*querystd)(struct v4l2_subdev *sd, v4l2_std_id *std);
	int (*g_tvnorms)(struct v4l2_subdev *sd, v4l2_std_id *std);
	int (*g_tvnorms_output)(struct v4l2_subdev *sd, v4l2_std_id *std);
	int (*g_input_status)(struct v4l2_subdev *sd, u32 *status);
	int (*s_stream)(struct v4l2_subdev *sd, int enable);
	int (*g_pixelaspect)(struct v4l2_subdev *sd, struct v4l2_fract *aspect);
	int (*g_frame_interval)(struct v4l2_subdev *sd,
				struct v4l2_subdev_frame_interval *interval);
	int (*s_frame_interval)(struct v4l2_subdev *sd,
				struct v4l2_subdev_frame_interval *interval);
	int (*s_dv_timings)(struct v4l2_subdev *sd,
			struct v4l2_dv_timings *timings);
	int (*g_dv_timings)(struct v4l2_subdev *sd,
			struct v4l2_dv_timings *timings);
	int (*query_dv_timings)(struct v4l2_subdev *sd,
			struct v4l2_dv_timings *timings);
	int (*s_rx_buffer)(struct v4l2_subdev *sd, void *buf,
			   unsigned int *size);
	int (*pre_streamon)(struct v4l2_subdev *sd, u32 flags);
	int (*post_streamoff)(struct v4l2_subdev *sd);
};

/**
 * struct v4l2_subdev_vbi_ops - Callbacks used when v4l device was opened
 *				  in video mode via the vbi device node.
 *
 *  @decode_vbi_line: video decoders that support sliced VBI need to implement
 *	this ioctl. Field p of the &struct v4l2_decode_vbi_line is set to the
 *	start of the VBI data that was generated by the decoder. The driver
 *	then parses the sliced VBI data and sets the other fields in the
 *	struct accordingly. The pointer p is updated to point to the start of
 *	the payload which can be copied verbatim into the data field of the
 *	&struct v4l2_sliced_vbi_data. If no valid VBI data was found, then the
 *	type field is set to 0 on return.
 *
 * @s_vbi_data: used to generate VBI signals on a video signal.
 *	&struct v4l2_sliced_vbi_data is filled with the data packets that
 *	should be output. Note that if you set the line field to 0, then that
 *	VBI signal is disabled. If no valid VBI data was found, then the type
 *	field is set to 0 on return.
 *
 * @g_vbi_data: used to obtain the sliced VBI packet from a readback register.
 *	Not all video decoders support this. If no data is available because
 *	the readback register contains invalid or erroneous data %-EIO is
 *	returned. Note that you must fill in the 'id' member and the 'field'
 *	member (to determine whether CC data from the first or second field
 *	should be obtained).
 *
 * @g_sliced_vbi_cap: callback for VIDIOC_G_SLICED_VBI_CAP() ioctl handler
 *		      code.
 *
 * @s_raw_fmt: setup the video encoder/decoder for raw VBI.
 *
 * @g_sliced_fmt: retrieve the current sliced VBI settings.
 *
 * @s_sliced_fmt: setup the sliced VBI settings.
 */
struct v4l2_subdev_vbi_ops {
	int (*decode_vbi_line)(struct v4l2_subdev *sd, struct v4l2_decode_vbi_line *vbi_line);
	int (*s_vbi_data)(struct v4l2_subdev *sd, const struct v4l2_sliced_vbi_data *vbi_data);
	int (*g_vbi_data)(struct v4l2_subdev *sd, struct v4l2_sliced_vbi_data *vbi_data);
	int (*g_sliced_vbi_cap)(struct v4l2_subdev *sd, struct v4l2_sliced_vbi_cap *cap);
	int (*s_raw_fmt)(struct v4l2_subdev *sd, struct v4l2_vbi_format *fmt);
	int (*g_sliced_fmt)(struct v4l2_subdev *sd, struct v4l2_sliced_vbi_format *fmt);
	int (*s_sliced_fmt)(struct v4l2_subdev *sd, struct v4l2_sliced_vbi_format *fmt);
};

/**
 * struct v4l2_subdev_sensor_ops - v4l2-subdev sensor operations
 * @g_skip_top_lines: number of lines at the top of the image to be skipped.
 *		      This is needed for some sensors, which always corrupt
 *		      several top lines of the output image, or which send their
 *		      metadata in them.
 * @g_skip_frames: number of frames to skip at stream start. This is needed for
 *		   buggy sensors that generate faulty frames when they are
 *		   turned on.
 */
struct v4l2_subdev_sensor_ops {
	int (*g_skip_top_lines)(struct v4l2_subdev *sd, u32 *lines);
	int (*g_skip_frames)(struct v4l2_subdev *sd, u32 *frames);
};

/**
 * enum v4l2_subdev_ir_mode- describes the type of IR supported
 *
 * @V4L2_SUBDEV_IR_MODE_PULSE_WIDTH: IR uses struct ir_raw_event records
 */
enum v4l2_subdev_ir_mode {
	V4L2_SUBDEV_IR_MODE_PULSE_WIDTH,
};

/**
 * struct v4l2_subdev_ir_parameters - Parameters for IR TX or TX
 *
 * @bytes_per_data_element: bytes per data element of data in read or
 *	write call.
 * @mode: IR mode as defined by &enum v4l2_subdev_ir_mode.
 * @enable: device is active if true
 * @interrupt_enable: IR interrupts are enabled if true
 * @shutdown: if true: set hardware to low/no power, false: normal mode
 *
 * @modulation: if true, it uses carrier, if false: baseband
 * @max_pulse_width:  maximum pulse width in ns, valid only for baseband signal
 * @carrier_freq: carrier frequency in Hz, valid only for modulated signal
 * @duty_cycle: duty cycle percentage, valid only for modulated signal
 * @invert_level: invert signal level
 *
 * @invert_carrier_sense: Send 0/space as a carrier burst. used only in TX.
 *
 * @noise_filter_min_width: min time of a valid pulse, in ns. Used only for RX.
 * @carrier_range_lower: Lower carrier range, in Hz, valid only for modulated
 *	signal. Used only for RX.
 * @carrier_range_upper: Upper carrier range, in Hz, valid only for modulated
 *	signal. Used only for RX.
 * @resolution: The receive resolution, in ns . Used only for RX.
 */
struct v4l2_subdev_ir_parameters {
	unsigned int bytes_per_data_element;
	enum v4l2_subdev_ir_mode mode;

	bool enable;
	bool interrupt_enable;
	bool shutdown;

	bool modulation;
	u32 max_pulse_width;
	unsigned int carrier_freq;
	unsigned int duty_cycle;
	bool invert_level;

	/* Tx only */
	bool invert_carrier_sense;

	/* Rx only */
	u32 noise_filter_min_width;
	unsigned int carrier_range_lower;
	unsigned int carrier_range_upper;
	u32 resolution;
};

/**
 * struct v4l2_subdev_ir_ops - operations for IR subdevices
 *
 * @rx_read: Reads received codes or pulse width data.
 *	The semantics are similar to a non-blocking read() call.
 * @rx_g_parameters: Get the current operating parameters and state of
 *	the IR receiver.
 * @rx_s_parameters: Set the current operating parameters and state of
 *	the IR receiver.  It is recommended to call
 *	[rt]x_g_parameters first to fill out the current state, and only change
 *	the fields that need to be changed.  Upon return, the actual device
 *	operating parameters and state will be returned.  Note that hardware
 *	limitations may prevent the actual settings from matching the requested
 *	settings - e.g. an actual carrier setting of 35,904 Hz when 36,000 Hz
 *	was requested.  An exception is when the shutdown parameter is true.
 *	The last used operational parameters will be returned, but the actual
 *	state of the hardware be different to minimize power consumption and
 *	processing when shutdown is true.
 *
 * @tx_write: Writes codes or pulse width data for transmission.
 *	The semantics are similar to a non-blocking write() call.
 * @tx_g_parameters: Get the current operating parameters and state of
 *	the IR transmitter.
 * @tx_s_parameters: Set the current operating parameters and state of
 *	the IR transmitter.  It is recommended to call
 *	[rt]x_g_parameters first to fill out the current state, and only change
 *	the fields that need to be changed.  Upon return, the actual device
 *	operating parameters and state will be returned.  Note that hardware
 *	limitations may prevent the actual settings from matching the requested
 *	settings - e.g. an actual carrier setting of 35,904 Hz when 36,000 Hz
 *	was requested.  An exception is when the shutdown parameter is true.
 *	The last used operational parameters will be returned, but the actual
 *	state of the hardware be different to minimize power consumption and
 *	processing when shutdown is true.
 */
struct v4l2_subdev_ir_ops {
	/* Receiver */
	int (*rx_read)(struct v4l2_subdev *sd, u8 *buf, size_t count,
				ssize_t *num);

	int (*rx_g_parameters)(struct v4l2_subdev *sd,
				struct v4l2_subdev_ir_parameters *params);
	int (*rx_s_parameters)(struct v4l2_subdev *sd,
				struct v4l2_subdev_ir_parameters *params);

	/* Transmitter */
	int (*tx_write)(struct v4l2_subdev *sd, u8 *buf, size_t count,
				ssize_t *num);

	int (*tx_g_parameters)(struct v4l2_subdev *sd,
				struct v4l2_subdev_ir_parameters *params);
	int (*tx_s_parameters)(struct v4l2_subdev *sd,
				struct v4l2_subdev_ir_parameters *params);
};

/**
 * struct v4l2_subdev_pad_config - Used for storing subdev pad information.
 *
 * @try_fmt: &struct v4l2_mbus_framefmt
 * @try_crop: &struct v4l2_rect to be used for crop
 * @try_compose: &struct v4l2_rect to be used for compose
 *
 * This structure only needs to be passed to the pad op if the 'which' field
 * of the main argument is set to %V4L2_SUBDEV_FORMAT_TRY. For
 * %V4L2_SUBDEV_FORMAT_ACTIVE it is safe to pass %NULL.
 *
 * Note: This struct is also used in active state, and the 'try' prefix is
 * historical and to be removed.
 */
struct v4l2_subdev_pad_config {
	struct v4l2_mbus_framefmt try_fmt;
	struct v4l2_rect try_crop;
	struct v4l2_rect try_compose;
};

/**
 * struct v4l2_subdev_stream_config - Used for storing stream configuration.
 *
 * @pad: pad number
 * @stream: stream number
 * @enabled: has the stream been enabled with v4l2_subdev_enable_stream()
 * @fmt: &struct v4l2_mbus_framefmt
 * @crop: &struct v4l2_rect to be used for crop
 * @compose: &struct v4l2_rect to be used for compose
 *
 * This structure stores configuration for a stream.
 */
struct v4l2_subdev_stream_config {
	u32 pad;
	u32 stream;
	bool enabled;

	struct v4l2_mbus_framefmt fmt;
	struct v4l2_rect crop;
	struct v4l2_rect compose;
};

/**
 * struct v4l2_subdev_stream_configs - A collection of stream configs.
 *
 * @num_configs: number of entries in @config.
 * @configs: an array of &struct v4l2_subdev_stream_configs.
 */
struct v4l2_subdev_stream_configs {
	u32 num_configs;
	struct v4l2_subdev_stream_config *configs;
};

/**
 * struct v4l2_subdev_krouting - subdev routing table
 *
 * @num_routes: number of routes
 * @routes: &struct v4l2_subdev_route
 *
 * This structure contains the routing table for a subdev.
 */
struct v4l2_subdev_krouting {
	unsigned int num_routes;
	struct v4l2_subdev_route *routes;
};

/**
 * struct v4l2_subdev_state - Used for storing subdev state information.
 *
 * @_lock: default for 'lock'
 * @lock: mutex for the state. May be replaced by the user.
 * @pads: &struct v4l2_subdev_pad_config array
 * @routing: routing table for the subdev
 * @stream_configs: stream configurations (only for V4L2_SUBDEV_FL_STREAMS)
 *
 * This structure only needs to be passed to the pad op if the 'which' field
 * of the main argument is set to %V4L2_SUBDEV_FORMAT_TRY. For
 * %V4L2_SUBDEV_FORMAT_ACTIVE it is safe to pass %NULL.
 */
struct v4l2_subdev_state {
	/* lock for the struct v4l2_subdev_state fields */
	struct mutex _lock;
	struct mutex *lock;
	struct v4l2_subdev_pad_config *pads;
	struct v4l2_subdev_krouting routing;
	struct v4l2_subdev_stream_configs stream_configs;
};

/**
 * struct v4l2_subdev_pad_ops - v4l2-subdev pad level operations
 *
 * @init_cfg: initialize the pad config to default values
 * @enum_mbus_code: callback for VIDIOC_SUBDEV_ENUM_MBUS_CODE() ioctl handler
 *		    code.
 * @enum_frame_size: callback for VIDIOC_SUBDEV_ENUM_FRAME_SIZE() ioctl handler
 *		     code.
 *
 * @enum_frame_interval: callback for VIDIOC_SUBDEV_ENUM_FRAME_INTERVAL() ioctl
 *			 handler code.
 *
 * @get_fmt: callback for VIDIOC_SUBDEV_G_FMT() ioctl handler code.
 *
 * @set_fmt: callback for VIDIOC_SUBDEV_S_FMT() ioctl handler code.
 *
 * @get_selection: callback for VIDIOC_SUBDEV_G_SELECTION() ioctl handler code.
 *
 * @set_selection: callback for VIDIOC_SUBDEV_S_SELECTION() ioctl handler code.
 *
 * @get_edid: callback for VIDIOC_SUBDEV_G_EDID() ioctl handler code.
 *
 * @set_edid: callback for VIDIOC_SUBDEV_S_EDID() ioctl handler code.
 *
 * @dv_timings_cap: callback for VIDIOC_SUBDEV_DV_TIMINGS_CAP() ioctl handler
 *		    code.
 *
 * @enum_dv_timings: callback for VIDIOC_SUBDEV_ENUM_DV_TIMINGS() ioctl handler
 *		     code.
 *
 * @link_validate: used by the media controller code to check if the links
 *		   that belongs to a pipeline can be used for stream.
 *
 * @get_frame_desc: get the current low level media bus frame parameters.
 *
 * @set_frame_desc: set the low level media bus frame parameters, @fd array
 *                  may be adjusted by the subdev driver to device capabilities.
 *
 * @get_mbus_config: get the media bus configuration of a remote sub-device.
 *		     The media bus configuration is usually retrieved from the
 *		     firmware interface at sub-device probe time, immediately
 *		     applied to the hardware and eventually adjusted by the
 *		     driver. Remote sub-devices (usually video receivers) shall
 *		     use this operation to query the transmitting end bus
 *		     configuration in order to adjust their own one accordingly.
 *		     Callers should make sure they get the most up-to-date as
 *		     possible configuration from the remote end, likely calling
 *		     this operation as close as possible to stream on time. The
 *		     operation shall fail if the pad index it has been called on
 *		     is not valid or in case of unrecoverable failures.
 *
 * @set_routing: enable or disable data connection routes described in the
 *		 subdevice routing table.
 *
 * @enable_streams: Enable the streams defined in streams_mask on the given
 *	source pad. Subdevs that implement this operation must use the active
 *	state management provided by the subdev core (enabled through a call to
 *	v4l2_subdev_init_finalize() at initialization time). Do not call
 *	directly, use v4l2_subdev_enable_streams() instead.
 *
 * @disable_streams: Disable the streams defined in streams_mask on the given
 *	source pad. Subdevs that implement this operation must use the active
 *	state management provided by the subdev core (enabled through a call to
 *	v4l2_subdev_init_finalize() at initialization time). Do not call
 *	directly, use v4l2_subdev_disable_streams() instead.
 */
struct v4l2_subdev_pad_ops {
	int (*init_cfg)(struct v4l2_subdev *sd,
			struct v4l2_subdev_state *state);
	int (*enum_mbus_code)(struct v4l2_subdev *sd,
			      struct v4l2_subdev_state *state,
			      struct v4l2_subdev_mbus_code_enum *code);
	int (*enum_frame_size)(struct v4l2_subdev *sd,
			       struct v4l2_subdev_state *state,
			       struct v4l2_subdev_frame_size_enum *fse);
	int (*enum_frame_interval)(struct v4l2_subdev *sd,
				   struct v4l2_subdev_state *state,
				   struct v4l2_subdev_frame_interval_enum *fie);
	int (*get_fmt)(struct v4l2_subdev *sd,
		       struct v4l2_subdev_state *state,
		       struct v4l2_subdev_format *format);
	int (*set_fmt)(struct v4l2_subdev *sd,
		       struct v4l2_subdev_state *state,
		       struct v4l2_subdev_format *format);
	int (*get_selection)(struct v4l2_subdev *sd,
			     struct v4l2_subdev_state *state,
			     struct v4l2_subdev_selection *sel);
	int (*set_selection)(struct v4l2_subdev *sd,
			     struct v4l2_subdev_state *state,
			     struct v4l2_subdev_selection *sel);
	int (*get_edid)(struct v4l2_subdev *sd, struct v4l2_edid *edid);
	int (*set_edid)(struct v4l2_subdev *sd, struct v4l2_edid *edid);
	int (*dv_timings_cap)(struct v4l2_subdev *sd,
			      struct v4l2_dv_timings_cap *cap);
	int (*enum_dv_timings)(struct v4l2_subdev *sd,
			       struct v4l2_enum_dv_timings *timings);
#ifdef CONFIG_MEDIA_CONTROLLER
	int (*link_validate)(struct v4l2_subdev *sd, struct media_link *link,
			     struct v4l2_subdev_format *source_fmt,
			     struct v4l2_subdev_format *sink_fmt);
#endif /* CONFIG_MEDIA_CONTROLLER */
	int (*get_frame_desc)(struct v4l2_subdev *sd, unsigned int pad,
			      struct v4l2_mbus_frame_desc *fd);
	int (*set_frame_desc)(struct v4l2_subdev *sd, unsigned int pad,
			      struct v4l2_mbus_frame_desc *fd);
	int (*get_mbus_config)(struct v4l2_subdev *sd, unsigned int pad,
			       struct v4l2_mbus_config *config);
	int (*set_routing)(struct v4l2_subdev *sd,
			   struct v4l2_subdev_state *state,
			   enum v4l2_subdev_format_whence which,
			   struct v4l2_subdev_krouting *route);
	int (*enable_streams)(struct v4l2_subdev *sd,
			      struct v4l2_subdev_state *state, u32 pad,
			      u64 streams_mask);
	int (*disable_streams)(struct v4l2_subdev *sd,
			       struct v4l2_subdev_state *state, u32 pad,
			       u64 streams_mask);
};

/**
 * struct v4l2_subdev_ops - Subdev operations
 *
 * @core: pointer to &struct v4l2_subdev_core_ops. Can be %NULL
 * @tuner: pointer to &struct v4l2_subdev_tuner_ops. Can be %NULL
 * @audio: pointer to &struct v4l2_subdev_audio_ops. Can be %NULL
 * @video: pointer to &struct v4l2_subdev_video_ops. Can be %NULL
 * @vbi: pointer to &struct v4l2_subdev_vbi_ops. Can be %NULL
 * @ir: pointer to &struct v4l2_subdev_ir_ops. Can be %NULL
 * @sensor: pointer to &struct v4l2_subdev_sensor_ops. Can be %NULL
 * @pad: pointer to &struct v4l2_subdev_pad_ops. Can be %NULL
 */
struct v4l2_subdev_ops {
	const struct v4l2_subdev_core_ops	*core;
	const struct v4l2_subdev_tuner_ops	*tuner;
	const struct v4l2_subdev_audio_ops	*audio;
	const struct v4l2_subdev_video_ops	*video;
	const struct v4l2_subdev_vbi_ops	*vbi;
	const struct v4l2_subdev_ir_ops		*ir;
	const struct v4l2_subdev_sensor_ops	*sensor;
	const struct v4l2_subdev_pad_ops	*pad;
};

/**
 * struct v4l2_subdev_internal_ops - V4L2 subdev internal ops
 *
 * @registered: called when this subdev is registered. When called the v4l2_dev
 *	field is set to the correct v4l2_device.
 *
 * @unregistered: called when this subdev is unregistered. When called the
 *	v4l2_dev field is still set to the correct v4l2_device.
 *
 * @open: called when the subdev device node is opened by an application.
 *
 * @close: called when the subdev device node is closed. Please note that
 *	it is possible for @close to be called after @unregistered!
 *
 * @release: called when the last user of the subdev device is gone. This
 *	happens after the @unregistered callback and when the last open
 *	filehandle to the v4l-subdevX device node was closed. If no device
 *	node was created for this sub-device, then the @release callback
 *	is called right after the @unregistered callback.
 *	The @release callback is typically used to free the memory containing
 *	the v4l2_subdev structure. It is almost certainly required for any
 *	sub-device that sets the V4L2_SUBDEV_FL_HAS_DEVNODE flag.
 *
 * .. note::
 *	Never call this from drivers, only the v4l2 framework can call
 *	these ops.
 */
struct v4l2_subdev_internal_ops {
	int (*registered)(struct v4l2_subdev *sd);
	void (*unregistered)(struct v4l2_subdev *sd);
	int (*open)(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh);
	int (*close)(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh);
	void (*release)(struct v4l2_subdev *sd);
};

#define V4L2_SUBDEV_NAME_SIZE 32

/* Set this flag if this subdev is a i2c device. */
#define V4L2_SUBDEV_FL_IS_I2C			(1U << 0)
/* Set this flag if this subdev is a spi device. */
#define V4L2_SUBDEV_FL_IS_SPI			(1U << 1)
/* Set this flag if this subdev needs a device node. */
#define V4L2_SUBDEV_FL_HAS_DEVNODE		(1U << 2)
/*
 * Set this flag if this subdev generates events.
 * Note controls can send events, thus drivers exposing controls
 * should set this flag.
 */
#define V4L2_SUBDEV_FL_HAS_EVENTS		(1U << 3)
/*
 * Set this flag if this subdev supports multiplexed streams. This means
 * that the driver supports routing and handles the stream parameter in its
 * v4l2_subdev_pad_ops handlers. More specifically, this means:
 *
 * - Centrally managed subdev active state is enabled
 * - Legacy pad config is _not_ supported (state->pads is NULL)
 * - Routing ioctls are available
 * - Multiple streams per pad are supported
 */
#define V4L2_SUBDEV_FL_STREAMS			(1U << 4)

struct regulator_bulk_data;

/**
 * struct v4l2_subdev_platform_data - regulators config struct
 *
 * @regulators: Optional regulators used to power on/off the subdevice
 * @num_regulators: Number of regululators
 * @host_priv: Per-subdevice data, specific for a certain video host device
 */
struct v4l2_subdev_platform_data {
	struct regulator_bulk_data *regulators;
	int num_regulators;

	void *host_priv;
};

/**
 * struct v4l2_subdev - describes a V4L2 sub-device
 *
 * @entity: pointer to &struct media_entity
 * @list: List of sub-devices
 * @owner: The owner is the same as the driver's &struct device owner.
 * @owner_v4l2_dev: true if the &sd->owner matches the owner of @v4l2_dev->dev
 *	owner. Initialized by v4l2_device_register_subdev().
 * @flags: subdev flags. Can be:
 *   %V4L2_SUBDEV_FL_IS_I2C - Set this flag if this subdev is a i2c device;
 *   %V4L2_SUBDEV_FL_IS_SPI - Set this flag if this subdev is a spi device;
 *   %V4L2_SUBDEV_FL_HAS_DEVNODE - Set this flag if this subdev needs a
 *   device node;
 *   %V4L2_SUBDEV_FL_HAS_EVENTS -  Set this flag if this subdev generates
 *   events.
 *
 * @v4l2_dev: pointer to struct &v4l2_device
 * @ops: pointer to struct &v4l2_subdev_ops
 * @internal_ops: pointer to struct &v4l2_subdev_internal_ops.
 *	Never call these internal ops from within a driver!
 * @ctrl_handler: The control handler of this subdev. May be NULL.
 * @name: Name of the sub-device. Please notice that the name must be unique.
 * @grp_id: can be used to group similar subdevs. Value is driver-specific
 * @dev_priv: pointer to private data
 * @host_priv: pointer to private data used by the device where the subdev
 *	is attached.
 * @devnode: subdev device node
 * @dev: pointer to the physical device, if any
 * @fwnode: The fwnode_handle of the subdev, usually the same as
 *	    either dev->of_node->fwnode or dev->fwnode (whichever is non-NULL).
 * @async_list: Links this subdev to a global subdev_list or @notifier->done
 *	list.
 * @asd: Pointer to respective &struct v4l2_async_subdev.
 * @notifier: Pointer to the managing notifier.
 * @subdev_notifier: A sub-device notifier implicitly registered for the sub-
 *		     device using v4l2_async_register_subdev_sensor().
 * @pdata: common part of subdevice platform data
 * @state_lock: A pointer to a lock used for all the subdev's states, set by the
 *		driver. This is	optional. If NULL, each state instance will get
 *		a lock of its own.
 * @privacy_led: Optional pointer to a LED classdev for the privacy LED for sensors.
 * @active_state: Active state for the subdev (NULL for subdevs tracking the
 *		  state internally). Initialized by calling
 *		  v4l2_subdev_init_finalize().
 * @enabled_streams: Bitmask of enabled streams used by
 *		     v4l2_subdev_enable_streams() and
 *		     v4l2_subdev_disable_streams() helper functions for fallback
 *		     cases.
 *
 * Each instance of a subdev driver should create this struct, either
 * stand-alone or embedded in a larger struct.
 *
 * This structure should be initialized by v4l2_subdev_init() or one of
 * its variants: v4l2_spi_subdev_init(), v4l2_i2c_subdev_init().
 */
struct v4l2_subdev {
#if defined(CONFIG_MEDIA_CONTROLLER)
	struct media_entity entity;
#endif
	struct list_head list;
	struct module *owner;
	bool owner_v4l2_dev;
	u32 flags;
	struct v4l2_device *v4l2_dev;
	const struct v4l2_subdev_ops *ops;
	const struct v4l2_subdev_internal_ops *internal_ops;
	struct v4l2_ctrl_handler *ctrl_handler;
	char name[V4L2_SUBDEV_NAME_SIZE];
	u32 grp_id;
	void *dev_priv;
	void *host_priv;
	struct video_device *devnode;
	struct device *dev;
	struct fwnode_handle *fwnode;
	struct list_head async_list;
	struct v4l2_async_subdev *asd;
	struct v4l2_async_notifier *notifier;
	struct v4l2_async_notifier *subdev_notifier;
	struct v4l2_subdev_platform_data *pdata;
	struct mutex *state_lock;

	/*
	 * The fields below are private, and should only be accessed via
	 * appropriate functions.
	 */

	struct led_classdev *privacy_led;

	/*
	 * TODO: active_state should most likely be changed from a pointer to an
	 * embedded field. For the time being it's kept as a pointer to more
	 * easily catch uses of active_state in the cases where the driver
	 * doesn't support it.
	 */
	struct v4l2_subdev_state *active_state;
	u64 enabled_streams;
};


/**
 * media_entity_to_v4l2_subdev - Returns a &struct v4l2_subdev from
 *    the &struct media_entity embedded in it.
 *
 * @ent: pointer to &struct media_entity.
 */
#define media_entity_to_v4l2_subdev(ent)				\
({									\
	typeof(ent) __me_sd_ent = (ent);				\
									\
	__me_sd_ent ?							\
		container_of(__me_sd_ent, struct v4l2_subdev, entity) :	\
		NULL;							\
})

/**
 * vdev_to_v4l2_subdev - Returns a &struct v4l2_subdev from
 *	the &struct video_device embedded on it.
 *
 * @vdev: pointer to &struct video_device
 */
#define vdev_to_v4l2_subdev(vdev) \
	((struct v4l2_subdev *)video_get_drvdata(vdev))

/**
 * struct v4l2_subdev_fh - Used for storing subdev information per file handle
 *
 * @vfh: pointer to &struct v4l2_fh
 * @state: pointer to &struct v4l2_subdev_state
 * @owner: module pointer to the owner of this file handle
 */
struct v4l2_subdev_fh {
	struct v4l2_fh vfh;
	struct module *owner;
#if defined(CONFIG_VIDEO_V4L2_SUBDEV_API)
	struct v4l2_subdev_state *state;
	u64 client_caps;
#endif
};

/**
 * to_v4l2_subdev_fh - Returns a &struct v4l2_subdev_fh from
 *	the &struct v4l2_fh embedded on it.
 *
 * @fh: pointer to &struct v4l2_fh
 */
#define to_v4l2_subdev_fh(fh)	\
	container_of(fh, struct v4l2_subdev_fh, vfh)

#if defined(CONFIG_VIDEO_V4L2_SUBDEV_API)

/**
 * v4l2_subdev_get_pad_format - ancillary routine to call
 *	&struct v4l2_subdev_pad_config->try_fmt
 *
 * @sd: pointer to &struct v4l2_subdev
 * @state: pointer to &struct v4l2_subdev_state
 * @pad: index of the pad in the &struct v4l2_subdev_state->pads array
 */
static inline struct v4l2_mbus_framefmt *
v4l2_subdev_get_pad_format(struct v4l2_subdev *sd,
			   struct v4l2_subdev_state *state,
			   unsigned int pad)
{
	if (WARN_ON(!state))
		return NULL;
	if (WARN_ON(pad >= sd->entity.num_pads))
		pad = 0;
	return &state->pads[pad].try_fmt;
}

/**
 * v4l2_subdev_get_pad_crop - ancillary routine to call
 *	&struct v4l2_subdev_pad_config->try_crop
 *
 * @sd: pointer to &struct v4l2_subdev
 * @state: pointer to &struct v4l2_subdev_state.
 * @pad: index of the pad in the &struct v4l2_subdev_state->pads array.
 */
static inline struct v4l2_rect *
v4l2_subdev_get_pad_crop(struct v4l2_subdev *sd,
			 struct v4l2_subdev_state *state,
			 unsigned int pad)
{
	if (WARN_ON(!state))
		return NULL;
	if (WARN_ON(pad >= sd->entity.num_pads))
		pad = 0;
	return &state->pads[pad].try_crop;
}

/**
 * v4l2_subdev_get_pad_compose - ancillary routine to call
 *	&struct v4l2_subdev_pad_config->try_compose
 *
 * @sd: pointer to &struct v4l2_subdev
 * @state: pointer to &struct v4l2_subdev_state.
 * @pad: index of the pad in the &struct v4l2_subdev_state->pads array.
 */
static inline struct v4l2_rect *
v4l2_subdev_get_pad_compose(struct v4l2_subdev *sd,
			    struct v4l2_subdev_state *state,
			    unsigned int pad)
{
	if (WARN_ON(!state))
		return NULL;
	if (WARN_ON(pad >= sd->entity.num_pads))
		pad = 0;
	return &state->pads[pad].try_compose;
}

/*
 * Temprary helpers until uses of v4l2_subdev_get_try_* functions have been
 * renamed
 */
#define v4l2_subdev_get_try_format(sd, state, pad) \
	v4l2_subdev_get_pad_format(sd, state, pad)

#define v4l2_subdev_get_try_crop(sd, state, pad) \
	v4l2_subdev_get_pad_crop(sd, state, pad)

#define v4l2_subdev_get_try_compose(sd, state, pad) \
	v4l2_subdev_get_pad_compose(sd, state, pad)

#endif /* CONFIG_VIDEO_V4L2_SUBDEV_API */

extern const struct v4l2_file_operations v4l2_subdev_fops;

/**
 * v4l2_set_subdevdata - Sets V4L2 dev private device data
 *
 * @sd: pointer to &struct v4l2_subdev
 * @p: pointer to the private device data to be stored.
 */
static inline void v4l2_set_subdevdata(struct v4l2_subdev *sd, void *p)
{
	sd->dev_priv = p;
}

/**
 * v4l2_get_subdevdata - Gets V4L2 dev private device data
 *
 * @sd: pointer to &struct v4l2_subdev
 *
 * Returns the pointer to the private device data to be stored.
 */
static inline void *v4l2_get_subdevdata(const struct v4l2_subdev *sd)
{
	return sd->dev_priv;
}

/**
 * v4l2_set_subdev_hostdata - Sets V4L2 dev private host data
 *
 * @sd: pointer to &struct v4l2_subdev
 * @p: pointer to the private data to be stored.
 */
static inline void v4l2_set_subdev_hostdata(struct v4l2_subdev *sd, void *p)
{
	sd->host_priv = p;
}

/**
 * v4l2_get_subdev_hostdata - Gets V4L2 dev private data
 *
 * @sd: pointer to &struct v4l2_subdev
 *
 * Returns the pointer to the private host data to be stored.
 */
static inline void *v4l2_get_subdev_hostdata(const struct v4l2_subdev *sd)
{
	return sd->host_priv;
}

#ifdef CONFIG_MEDIA_CONTROLLER

/**
 * v4l2_subdev_get_fwnode_pad_1_to_1 - Get pad number from a subdev fwnode
 *                                     endpoint, assuming 1:1 port:pad
 *
 * @entity: Pointer to the subdev entity
 * @endpoint: Pointer to a parsed fwnode endpoint
 *
 * This function can be used as the .get_fwnode_pad operation for
 * subdevices that map port numbers and pad indexes 1:1. If the endpoint
 * is owned by the subdevice, the function returns the endpoint port
 * number.
 *
 * Returns the endpoint port number on success or a negative error code.
 */
int v4l2_subdev_get_fwnode_pad_1_to_1(struct media_entity *entity,
				      struct fwnode_endpoint *endpoint);

/**
 * v4l2_subdev_link_validate_default - validates a media link
 *
 * @sd: pointer to &struct v4l2_subdev
 * @link: pointer to &struct media_link
 * @source_fmt: pointer to &struct v4l2_subdev_format
 * @sink_fmt: pointer to &struct v4l2_subdev_format
 *
 * This function ensures that width, height and the media bus pixel
 * code are equal on both source and sink of the link.
 */
int v4l2_subdev_link_validate_default(struct v4l2_subdev *sd,
				      struct media_link *link,
				      struct v4l2_subdev_format *source_fmt,
				      struct v4l2_subdev_format *sink_fmt);

/**
 * v4l2_subdev_link_validate - validates a media link
 *
 * @link: pointer to &struct media_link
 *
 * This function calls the subdev's link_validate ops to validate
 * if a media link is valid for streaming. It also internally
 * calls v4l2_subdev_link_validate_default() to ensure that
 * width, height and the media bus pixel code are equal on both
 * source and sink of the link.
 */
int v4l2_subdev_link_validate(struct media_link *link);

/**
 * v4l2_subdev_has_pad_interdep - MC has_pad_interdep implementation for subdevs
 *
 * @entity: pointer to &struct media_entity
 * @pad0: pad number for the first pad
 * @pad1: pad number for the second pad
 *
 * This function is an implementation of the
 * media_entity_operations.has_pad_interdep operation for subdevs that
 * implement the multiplexed streams API (as indicated by the
 * V4L2_SUBDEV_FL_STREAMS subdev flag).
 *
 * It considers two pads interdependent if there is an active route between pad0
 * and pad1.
 */
bool v4l2_subdev_has_pad_interdep(struct media_entity *entity,
				  unsigned int pad0, unsigned int pad1);

/**
 * __v4l2_subdev_state_alloc - allocate v4l2_subdev_state
 *
 * @sd: pointer to &struct v4l2_subdev for which the state is being allocated.
 * @lock_name: name of the state lock
 * @key: lock_class_key for the lock
 *
 * Must call __v4l2_subdev_state_free() when state is no longer needed.
 *
 * Not to be called directly by the drivers.
 */
struct v4l2_subdev_state *__v4l2_subdev_state_alloc(struct v4l2_subdev *sd,
						    const char *lock_name,
						    struct lock_class_key *key);

/**
 * __v4l2_subdev_state_free - free a v4l2_subdev_state
 *
 * @state: v4l2_subdev_state to be freed.
 *
 * Not to be called directly by the drivers.
 */
void __v4l2_subdev_state_free(struct v4l2_subdev_state *state);

/**
 * v4l2_subdev_init_finalize() - Finalizes the initialization of the subdevice
 * @sd: The subdev
 *
 * This function finalizes the initialization of the subdev, including
 * allocation of the active state for the subdev.
 *
 * This function must be called by the subdev drivers that use the centralized
 * active state, after the subdev struct has been initialized and
 * media_entity_pads_init() has been called, but before registering the
 * subdev.
 *
 * The user must call v4l2_subdev_cleanup() when the subdev is being removed.
 */
#define v4l2_subdev_init_finalize(sd)                                          \
	({                                                                     \
		static struct lock_class_key __key;                            \
		const char *name = KBUILD_BASENAME                             \
			":" __stringify(__LINE__) ":sd->active_state->lock";   \
		__v4l2_subdev_init_finalize(sd, name, &__key);                 \
	})

int __v4l2_subdev_init_finalize(struct v4l2_subdev *sd, const char *name,
				struct lock_class_key *key);

/**
 * v4l2_subdev_cleanup() - Releases the resources allocated by the subdevice
 * @sd: The subdevice
 *
 * This function will release the resources allocated in
 * v4l2_subdev_init_finalize.
 */
void v4l2_subdev_cleanup(struct v4l2_subdev *sd);

/**
 * v4l2_subdev_lock_state() - Locks the subdev state
 * @state: The subdevice state
 *
 * Locks the given subdev state.
 *
 * The state must be unlocked with v4l2_subdev_unlock_state() after use.
 */
static inline void v4l2_subdev_lock_state(struct v4l2_subdev_state *state)
{
	mutex_lock(state->lock);
}

/**
 * v4l2_subdev_unlock_state() - Unlocks the subdev state
 * @state: The subdevice state
 *
 * Unlocks the given subdev state.
 */
static inline void v4l2_subdev_unlock_state(struct v4l2_subdev_state *state)
{
	mutex_unlock(state->lock);
}

/**
 * v4l2_subdev_get_unlocked_active_state() - Checks that the active subdev state
 *					     is unlocked and returns it
 * @sd: The subdevice
 *
 * Returns the active state for the subdevice, or NULL if the subdev does not
 * support active state. If the state is not NULL, calls
 * lockdep_assert_not_held() to issue a warning if the state is locked.
 *
 * This function is to be used e.g. when getting the active state for the sole
 * purpose of passing it forward, without accessing the state fields.
 */
static inline struct v4l2_subdev_state *
v4l2_subdev_get_unlocked_active_state(struct v4l2_subdev *sd)
{
	if (sd->active_state)
		lockdep_assert_not_held(sd->active_state->lock);
	return sd->active_state;
}

/**
 * v4l2_subdev_get_locked_active_state() - Checks that the active subdev state
 *					   is locked and returns it
 *
 * @sd: The subdevice
 *
 * Returns the active state for the subdevice, or NULL if the subdev does not
 * support active state. If the state is not NULL, calls lockdep_assert_held()
 * to issue a warning if the state is not locked.
 *
 * This function is to be used when the caller knows that the active state is
 * already locked.
 */
static inline struct v4l2_subdev_state *
v4l2_subdev_get_locked_active_state(struct v4l2_subdev *sd)
{
	if (sd->active_state)
		lockdep_assert_held(sd->active_state->lock);
	return sd->active_state;
}

/**
 * v4l2_subdev_lock_and_get_active_state() - Locks and returns the active subdev
 *					     state for the subdevice
 * @sd: The subdevice
 *
 * Returns the locked active state for the subdevice, or NULL if the subdev
 * does not support active state.
 *
 * The state must be unlocked with v4l2_subdev_unlock_state() after use.
 */
static inline struct v4l2_subdev_state *
v4l2_subdev_lock_and_get_active_state(struct v4l2_subdev *sd)
{
	if (sd->active_state)
		v4l2_subdev_lock_state(sd->active_state);
	return sd->active_state;
}

#if defined(CONFIG_VIDEO_V4L2_SUBDEV_API)

/**
 * v4l2_subdev_get_fmt() - Fill format based on state
 * @sd: subdevice
 * @state: subdevice state
 * @format: pointer to &struct v4l2_subdev_format
 *
 * Fill @format->format field based on the information in the @format struct.
 *
 * This function can be used by the subdev drivers which support active state to
 * implement v4l2_subdev_pad_ops.get_fmt if the subdev driver does not need to
 * do anything special in their get_fmt op.
 *
 * Returns 0 on success, error value otherwise.
 */
int v4l2_subdev_get_fmt(struct v4l2_subdev *sd, struct v4l2_subdev_state *state,
			struct v4l2_subdev_format *format);

/**
 * v4l2_subdev_set_routing() - Set given routing to subdev state
 * @sd: The subdevice
 * @state: The subdevice state
 * @routing: Routing that will be copied to subdev state
 *
 * This will release old routing table (if any) from the state, allocate
 * enough space for the given routing, and copy the routing.
 *
 * This can be used from the subdev driver's set_routing op, after validating
 * the routing.
 */
int v4l2_subdev_set_routing(struct v4l2_subdev *sd,
			    struct v4l2_subdev_state *state,
			    const struct v4l2_subdev_krouting *routing);

struct v4l2_subdev_route *
__v4l2_subdev_next_active_route(const struct v4l2_subdev_krouting *routing,
				struct v4l2_subdev_route *route);

/**
 * for_each_active_route - iterate on all active routes of a routing table
 * @routing: The routing table
 * @route: The route iterator
 */
#define for_each_active_route(routing, route) \
	for ((route) = NULL;                  \
	     ((route) = __v4l2_subdev_next_active_route((routing), (route)));)

/**
 * v4l2_subdev_set_routing_with_fmt() - Set given routing and format to subdev
 *					state
 * @sd: The subdevice
 * @state: The subdevice state
 * @routing: Routing that will be copied to subdev state
 * @fmt: Format used to initialize all the streams
 *
 * This is the same as v4l2_subdev_set_routing, but additionally initializes
 * all the streams using the given format.
 */
int v4l2_subdev_set_routing_with_fmt(struct v4l2_subdev *sd,
				     struct v4l2_subdev_state *state,
				     struct v4l2_subdev_krouting *routing,
				     const struct v4l2_mbus_framefmt *fmt);

/**
 * v4l2_subdev_state_get_stream_format() - Get pointer to a stream format
 * @state: subdevice state
 * @pad: pad id
 * @stream: stream id
 *
 * This returns a pointer to &struct v4l2_mbus_framefmt for the given pad +
 * stream in the subdev state.
 *
 * If the state does not contain the given pad + stream, NULL is returned.
 */
struct v4l2_mbus_framefmt *
v4l2_subdev_state_get_stream_format(struct v4l2_subdev_state *state,
				    unsigned int pad, u32 stream);

/**
 * v4l2_subdev_state_get_stream_crop() - Get pointer to a stream crop rectangle
 * @state: subdevice state
 * @pad: pad id
 * @stream: stream id
 *
 * This returns a pointer to crop rectangle for the given pad + stream in the
 * subdev state.
 *
 * If the state does not contain the given pad + stream, NULL is returned.
 */
struct v4l2_rect *
v4l2_subdev_state_get_stream_crop(struct v4l2_subdev_state *state,
				  unsigned int pad, u32 stream);

/**
 * v4l2_subdev_state_get_stream_compose() - Get pointer to a stream compose
 *					    rectangle
 * @state: subdevice state
 * @pad: pad id
 * @stream: stream id
 *
 * This returns a pointer to compose rectangle for the given pad + stream in the
 * subdev state.
 *
 * If the state does not contain the given pad + stream, NULL is returned.
 */
struct v4l2_rect *
v4l2_subdev_state_get_stream_compose(struct v4l2_subdev_state *state,
				     unsigned int pad, u32 stream);

/**
 * v4l2_subdev_routing_find_opposite_end() - Find the opposite stream
 * @routing: routing used to find the opposite side
 * @pad: pad id
 * @stream: stream id
 * @other_pad: pointer used to return the opposite pad
 * @other_stream: pointer used to return the opposite stream
 *
 * This function uses the routing table to find the pad + stream which is
 * opposite the given pad + stream.
 *
 * @other_pad and/or @other_stream can be NULL if the caller does not need the
 * value.
 *
 * Returns 0 on success, or -EINVAL if no matching route is found.
 */
int v4l2_subdev_routing_find_opposite_end(const struct v4l2_subdev_krouting *routing,
					  u32 pad, u32 stream, u32 *other_pad,
					  u32 *other_stream);

/**
 * v4l2_subdev_state_get_opposite_stream_format() - Get pointer to opposite
 *                                                  stream format
 * @state: subdevice state
 * @pad: pad id
 * @stream: stream id
 *
 * This returns a pointer to &struct v4l2_mbus_framefmt for the pad + stream
 * that is opposite the given pad + stream in the subdev state.
 *
 * If the state does not contain the given pad + stream, NULL is returned.
 */
struct v4l2_mbus_framefmt *
v4l2_subdev_state_get_opposite_stream_format(struct v4l2_subdev_state *state,
					     u32 pad, u32 stream);

/**
 * v4l2_subdev_state_xlate_streams() - Translate streams from one pad to another
 *
 * @state: Subdevice state
 * @pad0: The first pad
 * @pad1: The second pad
 * @streams: Streams bitmask on the first pad
 *
 * Streams on sink pads of a subdev are routed to source pads as expressed in
 * the subdev state routing table. Stream numbers don't necessarily match on
 * the sink and source side of a route. This function translates stream numbers
 * on @pad0, expressed as a bitmask in @streams, to the corresponding streams
 * on @pad1 using the routing table from the @state. It returns the stream mask
 * on @pad1, and updates @streams with the streams that have been found in the
 * routing table.
 *
 * @pad0 and @pad1 must be a sink and a source, in any order.
 *
 * Return: The bitmask of streams of @pad1 that are routed to @streams on @pad0.
 */
u64 v4l2_subdev_state_xlate_streams(const struct v4l2_subdev_state *state,
				    u32 pad0, u32 pad1, u64 *streams);

/**
 * enum v4l2_subdev_routing_restriction - Subdevice internal routing restrictions
 *
 * @V4L2_SUBDEV_ROUTING_NO_1_TO_N:
 *	an input stream shall not be routed to multiple output streams (stream
 *	duplication)
 * @V4L2_SUBDEV_ROUTING_NO_N_TO_1:
 *	multiple input streams shall not be routed to the same output stream
 *	(stream merging)
 * @V4L2_SUBDEV_ROUTING_NO_SINK_STREAM_MIX:
 *	all streams from a sink pad must be routed to a single source pad
 * @V4L2_SUBDEV_ROUTING_NO_SOURCE_STREAM_MIX:
 *	all streams on a source pad must originate from a single sink pad
 * @V4L2_SUBDEV_ROUTING_NO_SOURCE_MULTIPLEXING:
 *	source pads shall not contain multiplexed streams
 * @V4L2_SUBDEV_ROUTING_NO_SINK_MULTIPLEXING:
 *	sink pads shall not contain multiplexed streams
 * @V4L2_SUBDEV_ROUTING_ONLY_1_TO_1:
 *	only non-overlapping 1-to-1 stream routing is allowed (a combination of
 *	@V4L2_SUBDEV_ROUTING_NO_1_TO_N and @V4L2_SUBDEV_ROUTING_NO_N_TO_1)
 * @V4L2_SUBDEV_ROUTING_NO_STREAM_MIX:
 *	all streams from a sink pad must be routed to a single source pad, and
 *	that source pad shall not get routes from any other sink pad
 *	(a combination of @V4L2_SUBDEV_ROUTING_NO_SINK_STREAM_MIX and
 *	@V4L2_SUBDEV_ROUTING_NO_SOURCE_STREAM_MIX)
 * @V4L2_SUBDEV_ROUTING_NO_MULTIPLEXING:
 *	no multiplexed streams allowed on either source or sink sides.
 */
enum v4l2_subdev_routing_restriction {
	V4L2_SUBDEV_ROUTING_NO_1_TO_N = BIT(0),
	V4L2_SUBDEV_ROUTING_NO_N_TO_1 = BIT(1),
	V4L2_SUBDEV_ROUTING_NO_SINK_STREAM_MIX = BIT(2),
	V4L2_SUBDEV_ROUTING_NO_SOURCE_STREAM_MIX = BIT(3),
	V4L2_SUBDEV_ROUTING_NO_SINK_MULTIPLEXING = BIT(4),
	V4L2_SUBDEV_ROUTING_NO_SOURCE_MULTIPLEXING = BIT(5),
	V4L2_SUBDEV_ROUTING_ONLY_1_TO_1 =
		V4L2_SUBDEV_ROUTING_NO_1_TO_N |
		V4L2_SUBDEV_ROUTING_NO_N_TO_1,
	V4L2_SUBDEV_ROUTING_NO_STREAM_MIX =
		V4L2_SUBDEV_ROUTING_NO_SINK_STREAM_MIX |
		V4L2_SUBDEV_ROUTING_NO_SOURCE_STREAM_MIX,
	V4L2_SUBDEV_ROUTING_NO_MULTIPLEXING =
		V4L2_SUBDEV_ROUTING_NO_SINK_MULTIPLEXING |
		V4L2_SUBDEV_ROUTING_NO_SOURCE_MULTIPLEXING,
};

/**
 * v4l2_subdev_routing_validate() - Verify that routes comply with driver
 *				    constraints
 * @sd: The subdevice
 * @routing: Routing to verify
 * @disallow: Restrictions on routes
 *
 * This verifies that the given routing complies with the @disallow constraints.
 *
 * Returns 0 on success, error value otherwise.
 */
int v4l2_subdev_routing_validate(struct v4l2_subdev *sd,
				 const struct v4l2_subdev_krouting *routing,
				 enum v4l2_subdev_routing_restriction disallow);

/**
 * v4l2_subdev_enable_streams() - Enable streams on a pad
 * @sd: The subdevice
 * @pad: The pad
 * @streams_mask: Bitmask of streams to enable
 *
 * This function enables streams on a source @pad of a subdevice. The pad is
 * identified by its index, while the streams are identified by the
 * @streams_mask bitmask. This allows enabling multiple streams on a pad at
 * once.
 *
 * Enabling a stream that is already enabled isn't allowed. If @streams_mask
 * contains an already enabled stream, this function returns -EALREADY without
 * performing any operation.
 *
 * Per-stream enable is only available for subdevs that implement the
 * .enable_streams() and .disable_streams() operations. For other subdevs, this
 * function implements a best-effort compatibility by calling the .s_stream()
 * operation, limited to subdevs that have a single source pad.
 *
 * Return:
 * * 0: Success
 * * -EALREADY: One of the streams in streams_mask is already enabled
 * * -EINVAL: The pad index is invalid, or doesn't correspond to a source pad
 * * -EOPNOTSUPP: Falling back to the legacy .s_stream() operation is
 *   impossible because the subdev has multiple source pads
 */
int v4l2_subdev_enable_streams(struct v4l2_subdev *sd, u32 pad,
			       u64 streams_mask);

/**
 * v4l2_subdev_disable_streams() - Disable streams on a pad
 * @sd: The subdevice
 * @pad: The pad
 * @streams_mask: Bitmask of streams to disable
 *
 * This function disables streams on a source @pad of a subdevice. The pad is
 * identified by its index, while the streams are identified by the
 * @streams_mask bitmask. This allows disabling multiple streams on a pad at
 * once.
 *
 * Disabling a streams that is not enabled isn't allowed. If @streams_mask
 * contains a disabled stream, this function returns -EALREADY without
 * performing any operation.
 *
 * Per-stream disable is only available for subdevs that implement the
 * .enable_streams() and .disable_streams() operations. For other subdevs, this
 * function implements a best-effort compatibility by calling the .s_stream()
 * operation, limited to subdevs that have a single source pad.
 *
 * Return:
 * * 0: Success
 * * -EALREADY: One of the streams in streams_mask is not enabled
 * * -EINVAL: The pad index is invalid, or doesn't correspond to a source pad
 * * -EOPNOTSUPP: Falling back to the legacy .s_stream() operation is
 *   impossible because the subdev has multiple source pads
 */
int v4l2_subdev_disable_streams(struct v4l2_subdev *sd, u32 pad,
				u64 streams_mask);

/**
 * v4l2_subdev_s_stream_helper() - Helper to implement the subdev s_stream
 *	operation using enable_streams and disable_streams
 * @sd: The subdevice
 * @enable: Enable or disable streaming
 *
 * Subdevice drivers that implement the streams-aware
 * &v4l2_subdev_pad_ops.enable_streams and &v4l2_subdev_pad_ops.disable_streams
 * operations can use this helper to implement the legacy
 * &v4l2_subdev_video_ops.s_stream operation.
 *
 * This helper can only be used by subdevs that have a single source pad.
 *
 * Return: 0 on success, or a negative error code otherwise.
 */
int v4l2_subdev_s_stream_helper(struct v4l2_subdev *sd, int enable);

#endif /* CONFIG_VIDEO_V4L2_SUBDEV_API */

#endif /* CONFIG_MEDIA_CONTROLLER */

/**
 * v4l2_subdev_init - initializes the sub-device struct
 *
 * @sd: pointer to the &struct v4l2_subdev to be initialized
 * @ops: pointer to &struct v4l2_subdev_ops.
 */
void v4l2_subdev_init(struct v4l2_subdev *sd,
		      const struct v4l2_subdev_ops *ops);

extern const struct v4l2_subdev_ops v4l2_subdev_call_wrappers;

/**
 * v4l2_subdev_call - call an operation of a v4l2_subdev.
 *
 * @sd: pointer to the &struct v4l2_subdev
 * @o: name of the element at &struct v4l2_subdev_ops that contains @f.
 *     Each element there groups a set of callbacks functions.
 * @f: callback function to be called.
 *     The callback functions are defined in groups, according to
 *     each element at &struct v4l2_subdev_ops.
 * @args: arguments for @f.
 *
 * Example: err = v4l2_subdev_call(sd, video, s_std, norm);
 */
#define v4l2_subdev_call(sd, o, f, args...)				\
	({								\
		struct v4l2_subdev *__sd = (sd);			\
		int __result;						\
		if (!__sd)						\
			__result = -ENODEV;				\
		else if (!(__sd->ops->o && __sd->ops->o->f))		\
			__result = -ENOIOCTLCMD;			\
		else if (v4l2_subdev_call_wrappers.o &&			\
			 v4l2_subdev_call_wrappers.o->f)		\
			__result = v4l2_subdev_call_wrappers.o->f(	\
							__sd, ##args);	\
		else							\
			__result = __sd->ops->o->f(__sd, ##args);	\
		__result;						\
	})

/**
 * v4l2_subdev_call_state_active - call an operation of a v4l2_subdev which
 *				   takes state as a parameter, passing the
 *				   subdev its active state.
 *
 * @sd: pointer to the &struct v4l2_subdev
 * @o: name of the element at &struct v4l2_subdev_ops that contains @f.
 *     Each element there groups a set of callbacks functions.
 * @f: callback function to be called.
 *     The callback functions are defined in groups, according to
 *     each element at &struct v4l2_subdev_ops.
 * @args: arguments for @f.
 *
 * This is similar to v4l2_subdev_call(), except that this version can only be
 * used for ops that take a subdev state as a parameter. The macro will get the
 * active state, lock it before calling the op and unlock it after the call.
 */
#define v4l2_subdev_call_state_active(sd, o, f, args...)		\
	({								\
		int __result;						\
		struct v4l2_subdev_state *state;			\
		state = v4l2_subdev_get_unlocked_active_state(sd);	\
		if (state)						\
			v4l2_subdev_lock_state(state);			\
		__result = v4l2_subdev_call(sd, o, f, state, ##args);	\
		if (state)						\
			v4l2_subdev_unlock_state(state);		\
		__result;						\
	})

/**
 * v4l2_subdev_call_state_try - call an operation of a v4l2_subdev which
 *				takes state as a parameter, passing the
 *				subdev a newly allocated try state.
 *
 * @sd: pointer to the &struct v4l2_subdev
 * @o: name of the element at &struct v4l2_subdev_ops that contains @f.
 *     Each element there groups a set of callbacks functions.
 * @f: callback function to be called.
 *     The callback functions are defined in groups, according to
 *     each element at &struct v4l2_subdev_ops.
 * @args: arguments for @f.
 *
 * This is similar to v4l2_subdev_call_state_active(), except that as this
 * version allocates a new state, this is only usable for
 * V4L2_SUBDEV_FORMAT_TRY use cases.
 *
 * Note: only legacy non-MC drivers may need this macro.
 */
#define v4l2_subdev_call_state_try(sd, o, f, args...)                 \
	({                                                            \
		int __result;                                         \
		static struct lock_class_key __key;                   \
		const char *name = KBUILD_BASENAME                    \
			":" __stringify(__LINE__) ":state->lock";     \
		struct v4l2_subdev_state *state =                     \
			__v4l2_subdev_state_alloc(sd, name, &__key);  \
		v4l2_subdev_lock_state(state);                        \
		__result = v4l2_subdev_call(sd, o, f, state, ##args); \
		v4l2_subdev_unlock_state(state);                      \
		__v4l2_subdev_state_free(state);                      \
		__result;                                             \
	})

/**
 * v4l2_subdev_has_op - Checks if a subdev defines a certain operation.
 *
 * @sd: pointer to the &struct v4l2_subdev
 * @o: The group of callback functions in &struct v4l2_subdev_ops
 * which @f is a part of.
 * @f: callback function to be checked for its existence.
 */
#define v4l2_subdev_has_op(sd, o, f) \
	((sd)->ops->o && (sd)->ops->o->f)

/**
 * v4l2_subdev_notify_event() - Delivers event notification for subdevice
 * @sd: The subdev for which to deliver the event
 * @ev: The event to deliver
 *
 * Will deliver the specified event to all userspace event listeners which are
 * subscribed to the v42l subdev event queue as well as to the bridge driver
 * using the notify callback. The notification type for the notify callback
 * will be %V4L2_DEVICE_NOTIFY_EVENT.
 */
void v4l2_subdev_notify_event(struct v4l2_subdev *sd,
			      const struct v4l2_event *ev);

#endif /* _V4L2_SUBDEV_H */
