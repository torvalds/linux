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

struct v4l2_device;
struct v4l2_ctrl_handler;
struct v4l2_event_subscription;
struct v4l2_fh;
struct v4l2_subdev;
struct v4l2_subdev_fh;
struct tuner_setup;
struct v4l2_mbus_frame_desc;

/* decode_vbi_line */
struct v4l2_decode_vbi_line {
	u32 is_second_field;	/* Set to 0 for the first (odd) field,
				   set to 1 for the second (even) field. */
	u8 *p; 			/* Pointer to the sliced VBI data from the decoder.
				   On exit points to the start of the payload. */
	u32 line;		/* Line number of the sliced VBI data (1-23) */
	u32 type;		/* VBI service type (V4L2_SLICED_*). 0 if no service found */
};

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

   log_status
   g_register
   s_register

   This provides basic debugging support.

   The ioctl ops is meant for generic ioctl-like commands. Depending on
   the use-case it might be better to use subdev-specific ops (currently
   not yet implemented) since ops provide proper type-checking.
 */

/* Subdevice external IO pin configuration */
#define V4L2_SUBDEV_IO_PIN_DISABLE	(1 << 0) /* ENABLE assumed */
#define V4L2_SUBDEV_IO_PIN_OUTPUT	(1 << 1)
#define V4L2_SUBDEV_IO_PIN_INPUT	(1 << 2)
#define V4L2_SUBDEV_IO_PIN_SET_VALUE	(1 << 3) /* Set output value */
#define V4L2_SUBDEV_IO_PIN_ACTIVE_LOW	(1 << 4) /* ACTIVE HIGH assumed */

struct v4l2_subdev_io_pin_config {
	u32 flags;	/* V4L2_SUBDEV_IO_PIN_* flags for this pin's config */
	u8 pin;		/* Chip external IO pin to configure */
	u8 function;	/* Internal signal pad/function to route to IO pin */
	u8 value;	/* Initial value for pin - e.g. GPIO output value */
	u8 strength;	/* Pin drive strength */
};

/*
   s_io_pin_config: configure one or more chip I/O pins for chips that
	multiplex different internal signal pads out to IO pins.  This function
	takes a pointer to an array of 'n' pin configuration entries, one for
	each pin being configured.  This function could be called at times
	other than just subdevice initialization.

   init: initialize the sensor registers to some sort of reasonable default
	values. Do not use for new drivers and should be removed in existing
	drivers.

   load_fw: load firmware.

   reset: generic reset command. The argument selects which subsystems to
	reset. Passing 0 will always reset the whole chip. Do not use for new
	drivers without discussing this first on the linux-media mailinglist.
	There should be no reason normally to reset a device.

   s_gpio: set GPIO pins. Very simple right now, might need to be extended with
	a direction argument if needed.

   s_power: puts subdevice in power saving mode (on == 0) or normal operation
	mode (on == 1).

   interrupt_service_routine: Called by the bridge chip's interrupt service
	handler, when an interrupt status has be raised due to this subdev,
	so that this subdev can handle the details.  It may schedule work to be
	performed later.  It must not sleep.  *Called from an IRQ context*.
 */
struct v4l2_subdev_core_ops {
	int (*log_status)(struct v4l2_subdev *sd);
	int (*s_io_pin_config)(struct v4l2_subdev *sd, size_t n,
				      struct v4l2_subdev_io_pin_config *pincfg);
	int (*init)(struct v4l2_subdev *sd, u32 val);
	int (*load_fw)(struct v4l2_subdev *sd);
	int (*reset)(struct v4l2_subdev *sd, u32 val);
	int (*s_gpio)(struct v4l2_subdev *sd, u32 val);
	int (*queryctrl)(struct v4l2_subdev *sd, struct v4l2_queryctrl *qc);
	int (*g_ctrl)(struct v4l2_subdev *sd, struct v4l2_control *ctrl);
	int (*s_ctrl)(struct v4l2_subdev *sd, struct v4l2_control *ctrl);
	int (*g_ext_ctrls)(struct v4l2_subdev *sd, struct v4l2_ext_controls *ctrls);
	int (*s_ext_ctrls)(struct v4l2_subdev *sd, struct v4l2_ext_controls *ctrls);
	int (*try_ext_ctrls)(struct v4l2_subdev *sd, struct v4l2_ext_controls *ctrls);
	int (*querymenu)(struct v4l2_subdev *sd, struct v4l2_querymenu *qm);
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

/* s_radio: v4l device was opened in radio mode.

   g_frequency: freq->type must be filled in. Normally done by video_ioctl2
	or the bridge driver.

   g_tuner:
   s_tuner: vt->type must be filled in. Normally done by video_ioctl2 or the
	bridge driver.

   s_type_addr: sets tuner type and its I2C addr.

   s_config: sets tda9887 specific stuff, like port1, port2 and qss
 */
struct v4l2_subdev_tuner_ops {
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

/* s_clock_freq: set the frequency (in Hz) of the audio clock output.
	Used to slave an audio processor to the video decoder, ensuring that
	audio and video remain synchronized. Usual values for the frequency
	are 48000, 44100 or 32000 Hz. If the frequency is not supported, then
	-EINVAL is returned.

   s_i2s_clock_freq: sets I2S speed in bps. This is used to provide a standard
	way to select I2S clock used by driving digital audio streams at some
	board designs. Usual values for the frequency are 1024000 and 2048000.
	If the frequency is not supported, then -EINVAL is returned.

   s_routing: used to define the input and/or output pins of an audio chip,
	and any additional configuration data.
	Never attempt to use user-level input IDs (e.g. Composite, S-Video,
	Tuner) at this level. An i2c device shouldn't know about whether an
	input pin is connected to a Composite connector, become on another
	board or platform it might be connected to something else entirely.
	The calling driver is responsible for mapping a user-level input to
	the right pins on the i2c device.
 */
struct v4l2_subdev_audio_ops {
	int (*s_clock_freq)(struct v4l2_subdev *sd, u32 freq);
	int (*s_i2s_clock_freq)(struct v4l2_subdev *sd, u32 freq);
	int (*s_routing)(struct v4l2_subdev *sd, u32 input, u32 output, u32 config);
	int (*s_stream)(struct v4l2_subdev *sd, int enable);
};

/* Indicates the @length field specifies maximum data length. */
#define V4L2_MBUS_FRAME_DESC_FL_LEN_MAX		(1U << 0)
/*
 * Indicates that the format does not have line offsets, i.e. the
 * receiver should use 1D DMA.
 */
#define V4L2_MBUS_FRAME_DESC_FL_BLOB		(1U << 1)

/**
 * struct v4l2_mbus_frame_desc_entry - media bus frame description structure
 * @flags: V4L2_MBUS_FRAME_DESC_FL_* flags
 * @pixelcode: media bus pixel code, valid if FRAME_DESC_FL_BLOB is not set
 * @length: number of octets per frame, valid if V4L2_MBUS_FRAME_DESC_FL_BLOB
 *	    is set
 */
struct v4l2_mbus_frame_desc_entry {
	u16 flags;
	u32 pixelcode;
	u32 length;
};

#define V4L2_FRAME_DESC_ENTRY_MAX	4

/**
 * struct v4l2_mbus_frame_desc - media bus data frame description
 * @entry: frame descriptors array
 * @num_entries: number of entries in @entry array
 */
struct v4l2_mbus_frame_desc {
	struct v4l2_mbus_frame_desc_entry entry[V4L2_FRAME_DESC_ENTRY_MAX];
	unsigned short num_entries;
};

/*
   s_std_output: set v4l2_std_id for video OUTPUT devices. This is ignored by
	video input devices.

   g_std_output: get current standard for video OUTPUT devices. This is ignored
	by video input devices.

   g_tvnorms: get v4l2_std_id with all standards supported by the video
	CAPTURE device. This is ignored by video output devices.

   g_tvnorms_output: get v4l2_std_id with all standards supported by the video
	OUTPUT device. This is ignored by video capture devices.

   s_crystal_freq: sets the frequency of the crystal used to generate the
	clocks in Hz. An extra flags field allows device specific configuration
	regarding clock frequency dividers, etc. If not used, then set flags
	to 0. If the frequency is not supported, then -EINVAL is returned.

   g_input_status: get input status. Same as the status field in the v4l2_input
	struct.

   s_routing: see s_routing in audio_ops, except this version is for video
	devices.

   s_dv_timings(): Set custom dv timings in the sub device. This is used
	when sub device is capable of setting detailed timing information
	in the hardware to generate/detect the video signal.

   g_dv_timings(): Get custom dv timings in the sub device.

   enum_mbus_fmt: enumerate pixel formats, provided by a video data source

   g_mbus_fmt: get the current pixel format, provided by a video data source

   try_mbus_fmt: try to set a pixel format on a video data source

   s_mbus_fmt: set a pixel format on a video data source

   g_mbus_config: get supported mediabus configurations

   s_mbus_config: set a certain mediabus configuration. This operation is added
	for compatibility with soc-camera drivers and should not be used by new
	software.

   s_rx_buffer: set a host allocated memory buffer for the subdev. The subdev
	can adjust @size to a lower value and must not write more data to the
	buffer starting at @data than the original value of @size.
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
	int (*cropcap)(struct v4l2_subdev *sd, struct v4l2_cropcap *cc);
	int (*g_crop)(struct v4l2_subdev *sd, struct v4l2_crop *crop);
	int (*s_crop)(struct v4l2_subdev *sd, const struct v4l2_crop *crop);
	int (*g_parm)(struct v4l2_subdev *sd, struct v4l2_streamparm *param);
	int (*s_parm)(struct v4l2_subdev *sd, struct v4l2_streamparm *param);
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
	int (*enum_mbus_fmt)(struct v4l2_subdev *sd, unsigned int index,
			     u32 *code);
	int (*g_mbus_fmt)(struct v4l2_subdev *sd,
			  struct v4l2_mbus_framefmt *fmt);
	int (*try_mbus_fmt)(struct v4l2_subdev *sd,
			    struct v4l2_mbus_framefmt *fmt);
	int (*s_mbus_fmt)(struct v4l2_subdev *sd,
			  struct v4l2_mbus_framefmt *fmt);
	int (*g_mbus_config)(struct v4l2_subdev *sd,
			     struct v4l2_mbus_config *cfg);
	int (*s_mbus_config)(struct v4l2_subdev *sd,
			     const struct v4l2_mbus_config *cfg);
	int (*s_rx_buffer)(struct v4l2_subdev *sd, void *buf,
			   unsigned int *size);
};

/*
   decode_vbi_line: video decoders that support sliced VBI need to implement
	this ioctl. Field p of the v4l2_sliced_vbi_line struct is set to the
	start of the VBI data that was generated by the decoder. The driver
	then parses the sliced VBI data and sets the other fields in the
	struct accordingly. The pointer p is updated to point to the start of
	the payload which can be copied verbatim into the data field of the
	v4l2_sliced_vbi_data struct. If no valid VBI data was found, then the
	type field is set to 0 on return.

   s_vbi_data: used to generate VBI signals on a video signal.
	v4l2_sliced_vbi_data is filled with the data packets that should be
	output. Note that if you set the line field to 0, then that VBI signal
	is disabled. If no valid VBI data was found, then the type field is
	set to 0 on return.

   g_vbi_data: used to obtain the sliced VBI packet from a readback register.
	Not all video decoders support this. If no data is available because
	the readback register contains invalid or erroneous data -EIO is
	returned. Note that you must fill in the 'id' member and the 'field'
	member (to determine whether CC data from the first or second field
	should be obtained).

   s_raw_fmt: setup the video encoder/decoder for raw VBI.

   g_sliced_fmt: retrieve the current sliced VBI settings.

   s_sliced_fmt: setup the sliced VBI settings.
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

/*
   [rt]x_g_parameters: Get the current operating parameters and state of the
	the IR receiver or transmitter.

   [rt]x_s_parameters: Set the current operating parameters and state of the
	the IR receiver or transmitter.  It is recommended to call
	[rt]x_g_parameters first to fill out the current state, and only change
	the fields that need to be changed.  Upon return, the actual device
	operating parameters and state will be returned.  Note that hardware
	limitations may prevent the actual settings from matching the requested
	settings - e.g. an actual carrier setting of 35,904 Hz when 36,000 Hz
	was requested.  An exception is when the shutdown parameter is true.
	The last used operational parameters will be returned, but the actual
	state of the hardware be different to minimize power consumption and
	processing when shutdown is true.

   rx_read: Reads received codes or pulse width data.
	The semantics are similar to a non-blocking read() call.

   tx_write: Writes codes or pulse width data for transmission.
	The semantics are similar to a non-blocking write() call.
 */

enum v4l2_subdev_ir_mode {
	V4L2_SUBDEV_IR_MODE_PULSE_WIDTH, /* uses struct ir_raw_event records */
};

struct v4l2_subdev_ir_parameters {
	/* Either Rx or Tx */
	unsigned int bytes_per_data_element; /* of data in read or write call */
	enum v4l2_subdev_ir_mode mode;

	bool enable;
	bool interrupt_enable;
	bool shutdown; /* true: set hardware to low/no power, false: normal */

	bool modulation;           /* true: uses carrier, false: baseband */
	u32 max_pulse_width;       /* ns,      valid only for baseband signal */
	unsigned int carrier_freq; /* Hz,      valid only for modulated signal*/
	unsigned int duty_cycle;   /* percent, valid only for modulated signal*/
	bool invert_level;	   /* invert signal level */

	/* Tx only */
	bool invert_carrier_sense; /* Send 0/space as a carrier burst */

	/* Rx only */
	u32 noise_filter_min_width;       /* ns, min time of a valid pulse */
	unsigned int carrier_range_lower; /* Hz, valid only for modulated sig */
	unsigned int carrier_range_upper; /* Hz, valid only for modulated sig */
	u32 resolution;                   /* ns */
};

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

/*
 * Used for storing subdev pad information. This structure only needs
 * to be passed to the pad op if the 'which' field of the main argument
 * is set to V4L2_SUBDEV_FORMAT_TRY. For V4L2_SUBDEV_FORMAT_ACTIVE it is
 * safe to pass NULL.
 */
struct v4l2_subdev_pad_config {
	struct v4l2_mbus_framefmt try_fmt;
	struct v4l2_rect try_crop;
	struct v4l2_rect try_compose;
};

/**
 * struct v4l2_subdev_pad_ops - v4l2-subdev pad level operations
 * @get_frame_desc: get the current low level media bus frame parameters.
 * @get_frame_desc: set the low level media bus frame parameters, @fd array
 *                  may be adjusted by the subdev driver to device capabilities.
 */
struct v4l2_subdev_pad_ops {
	int (*enum_mbus_code)(struct v4l2_subdev *sd,
			      struct v4l2_subdev_pad_config *cfg,
			      struct v4l2_subdev_mbus_code_enum *code);
	int (*enum_frame_size)(struct v4l2_subdev *sd,
			       struct v4l2_subdev_pad_config *cfg,
			       struct v4l2_subdev_frame_size_enum *fse);
	int (*enum_frame_interval)(struct v4l2_subdev *sd,
				   struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_subdev_frame_interval_enum *fie);
	int (*get_fmt)(struct v4l2_subdev *sd,
		       struct v4l2_subdev_pad_config *cfg,
		       struct v4l2_subdev_format *format);
	int (*set_fmt)(struct v4l2_subdev *sd,
		       struct v4l2_subdev_pad_config *cfg,
		       struct v4l2_subdev_format *format);
	int (*get_selection)(struct v4l2_subdev *sd,
			     struct v4l2_subdev_pad_config *cfg,
			     struct v4l2_subdev_selection *sel);
	int (*set_selection)(struct v4l2_subdev *sd,
			     struct v4l2_subdev_pad_config *cfg,
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
};

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

/*
 * Internal ops. Never call this from drivers, only the v4l2 framework can call
 * these ops.
 *
 * registered: called when this subdev is registered. When called the v4l2_dev
 *	field is set to the correct v4l2_device.
 *
 * unregistered: called when this subdev is unregistered. When called the
 *	v4l2_dev field is still set to the correct v4l2_device.
 *
 * open: called when the subdev device node is opened by an application.
 *
 * close: called when the subdev device node is closed.
 */
struct v4l2_subdev_internal_ops {
	int (*registered)(struct v4l2_subdev *sd);
	void (*unregistered)(struct v4l2_subdev *sd);
	int (*open)(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh);
	int (*close)(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh);
};

#define V4L2_SUBDEV_NAME_SIZE 32

/* Set this flag if this subdev is a i2c device. */
#define V4L2_SUBDEV_FL_IS_I2C			(1U << 0)
/* Set this flag if this subdev is a spi device. */
#define V4L2_SUBDEV_FL_IS_SPI			(1U << 1)
/* Set this flag if this subdev needs a device node. */
#define V4L2_SUBDEV_FL_HAS_DEVNODE		(1U << 2)
/* Set this flag if this subdev generates events. */
#define V4L2_SUBDEV_FL_HAS_EVENTS		(1U << 3)

struct regulator_bulk_data;

struct v4l2_subdev_platform_data {
	/* Optional regulators uset to power on/off the subdevice */
	struct regulator_bulk_data *regulators;
	int num_regulators;

	/* Per-subdevice data, specific for a certain video host device */
	void *host_priv;
};

/* Each instance of a subdev driver should create this struct, either
   stand-alone or embedded in a larger struct.
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
	/* Never call these internal ops from within a driver! */
	const struct v4l2_subdev_internal_ops *internal_ops;
	/* The control handler of this subdev. May be NULL. */
	struct v4l2_ctrl_handler *ctrl_handler;
	/* name must be unique */
	char name[V4L2_SUBDEV_NAME_SIZE];
	/* can be used to group similar subdevs, value is driver-specific */
	u32 grp_id;
	/* pointer to private data */
	void *dev_priv;
	void *host_priv;
	/* subdev device node */
	struct video_device *devnode;
	/* pointer to the physical device, if any */
	struct device *dev;
	/* Links this subdev to a global subdev_list or @notifier->done list. */
	struct list_head async_list;
	/* Pointer to respective struct v4l2_async_subdev. */
	struct v4l2_async_subdev *asd;
	/* Pointer to the managing notifier. */
	struct v4l2_async_notifier *notifier;
	/* common part of subdevice platform data */
	struct v4l2_subdev_platform_data *pdata;
};

#define media_entity_to_v4l2_subdev(ent) \
	container_of(ent, struct v4l2_subdev, entity)
#define vdev_to_v4l2_subdev(vdev) \
	((struct v4l2_subdev *)video_get_drvdata(vdev))

/*
 * Used for storing subdev information per file handle
 */
struct v4l2_subdev_fh {
	struct v4l2_fh vfh;
#if defined(CONFIG_VIDEO_V4L2_SUBDEV_API)
	struct v4l2_subdev_pad_config *pad;
#endif
};

#define to_v4l2_subdev_fh(fh)	\
	container_of(fh, struct v4l2_subdev_fh, vfh)

#if defined(CONFIG_VIDEO_V4L2_SUBDEV_API)
#define __V4L2_SUBDEV_MK_GET_TRY(rtype, fun_name, field_name)		\
	static inline struct rtype *					\
	fun_name(struct v4l2_subdev *sd,				\
		 struct v4l2_subdev_pad_config *cfg,			\
		 unsigned int pad)					\
	{								\
		BUG_ON(pad >= sd->entity.num_pads);			\
		return &cfg[pad].field_name;				\
	}

__V4L2_SUBDEV_MK_GET_TRY(v4l2_mbus_framefmt, v4l2_subdev_get_try_format, try_fmt)
__V4L2_SUBDEV_MK_GET_TRY(v4l2_rect, v4l2_subdev_get_try_crop, try_crop)
__V4L2_SUBDEV_MK_GET_TRY(v4l2_rect, v4l2_subdev_get_try_compose, try_compose)
#endif

extern const struct v4l2_file_operations v4l2_subdev_fops;

static inline void v4l2_set_subdevdata(struct v4l2_subdev *sd, void *p)
{
	sd->dev_priv = p;
}

static inline void *v4l2_get_subdevdata(const struct v4l2_subdev *sd)
{
	return sd->dev_priv;
}

static inline void v4l2_set_subdev_hostdata(struct v4l2_subdev *sd, void *p)
{
	sd->host_priv = p;
}

static inline void *v4l2_get_subdev_hostdata(const struct v4l2_subdev *sd)
{
	return sd->host_priv;
}

#ifdef CONFIG_MEDIA_CONTROLLER
int v4l2_subdev_link_validate_default(struct v4l2_subdev *sd,
				      struct media_link *link,
				      struct v4l2_subdev_format *source_fmt,
				      struct v4l2_subdev_format *sink_fmt);
int v4l2_subdev_link_validate(struct media_link *link);
#endif /* CONFIG_MEDIA_CONTROLLER */
void v4l2_subdev_init(struct v4l2_subdev *sd,
		      const struct v4l2_subdev_ops *ops);

/* Call an ops of a v4l2_subdev, doing the right checks against
   NULL pointers.

   Example: err = v4l2_subdev_call(sd, video, s_std, norm);
 */
#define v4l2_subdev_call(sd, o, f, args...)				\
	(!(sd) ? -ENODEV : (((sd)->ops->o && (sd)->ops->o->f) ?	\
		(sd)->ops->o->f((sd) , ##args) : -ENOIOCTLCMD))

#define v4l2_subdev_has_op(sd, o, f) \
	((sd)->ops->o && (sd)->ops->o->f)

#endif
