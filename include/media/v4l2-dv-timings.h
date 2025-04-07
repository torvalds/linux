/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * v4l2-dv-timings - Internal header with dv-timings helper functions
 *
 * Copyright 2013 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
 */

#ifndef __V4L2_DV_TIMINGS_H
#define __V4L2_DV_TIMINGS_H

#include <linux/debugfs.h>
#include <linux/videodev2.h>

/**
 * v4l2_calc_timeperframe - helper function to calculate timeperframe based
 *	v4l2_dv_timings fields.
 * @t: Timings for the video mode.
 *
 * Calculates the expected timeperframe using the pixel clock value and
 * horizontal/vertical measures. This means that v4l2_dv_timings structure
 * must be correctly and fully filled.
 */
struct v4l2_fract v4l2_calc_timeperframe(const struct v4l2_dv_timings *t);

/*
 * v4l2_dv_timings_presets: list of all dv_timings presets.
 */
extern const struct v4l2_dv_timings v4l2_dv_timings_presets[];

/**
 * typedef v4l2_check_dv_timings_fnc - timings check callback
 *
 * @t: the v4l2_dv_timings struct.
 * @handle: a handle from the driver.
 *
 * Returns true if the given timings are valid.
 */
typedef bool v4l2_check_dv_timings_fnc(const struct v4l2_dv_timings *t, void *handle);

/**
 * v4l2_valid_dv_timings() - are these timings valid?
 *
 * @t:	  the v4l2_dv_timings struct.
 * @cap: the v4l2_dv_timings_cap capabilities.
 * @fnc: callback to check if this timing is OK. May be NULL.
 * @fnc_handle: a handle that is passed on to @fnc.
 *
 * Returns true if the given dv_timings struct is supported by the
 * hardware capabilities and the callback function (if non-NULL), returns
 * false otherwise.
 */
bool v4l2_valid_dv_timings(const struct v4l2_dv_timings *t,
			   const struct v4l2_dv_timings_cap *cap,
			   v4l2_check_dv_timings_fnc fnc,
			   void *fnc_handle);

/**
 * v4l2_enum_dv_timings_cap() - Helper function to enumerate possible DV
 *	 timings based on capabilities
 *
 * @t:	  the v4l2_enum_dv_timings struct.
 * @cap: the v4l2_dv_timings_cap capabilities.
 * @fnc: callback to check if this timing is OK. May be NULL.
 * @fnc_handle: a handle that is passed on to @fnc.
 *
 * This enumerates dv_timings using the full list of possible CEA-861 and DMT
 * timings, filtering out any timings that are not supported based on the
 * hardware capabilities and the callback function (if non-NULL).
 *
 * If a valid timing for the given index is found, it will fill in @t and
 * return 0, otherwise it returns -EINVAL.
 */
int v4l2_enum_dv_timings_cap(struct v4l2_enum_dv_timings *t,
			     const struct v4l2_dv_timings_cap *cap,
			     v4l2_check_dv_timings_fnc fnc,
			     void *fnc_handle);

/**
 * v4l2_find_dv_timings_cap() - Find the closest timings struct
 *
 * @t:	  the v4l2_enum_dv_timings struct.
 * @cap: the v4l2_dv_timings_cap capabilities.
 * @pclock_delta: maximum delta between t->pixelclock and the timing struct
 *		under consideration.
 * @fnc: callback to check if a given timings struct is OK. May be NULL.
 * @fnc_handle: a handle that is passed on to @fnc.
 *
 * This function tries to map the given timings to an entry in the
 * full list of possible CEA-861 and DMT timings, filtering out any timings
 * that are not supported based on the hardware capabilities and the callback
 * function (if non-NULL).
 *
 * On success it will fill in @t with the found timings and it returns true.
 * On failure it will return false.
 */
bool v4l2_find_dv_timings_cap(struct v4l2_dv_timings *t,
			      const struct v4l2_dv_timings_cap *cap,
			      unsigned pclock_delta,
			      v4l2_check_dv_timings_fnc fnc,
			      void *fnc_handle);

/**
 * v4l2_find_dv_timings_cea861_vic() - find timings based on CEA-861 VIC
 * @t:		the timings data.
 * @vic:	CEA-861 VIC code
 *
 * On success it will fill in @t with the found timings and it returns true.
 * On failure it will return false.
 */
bool v4l2_find_dv_timings_cea861_vic(struct v4l2_dv_timings *t, u8 vic);

/**
 * v4l2_match_dv_timings() - do two timings match?
 *
 * @measured:	  the measured timings data.
 * @standard:	  the timings according to the standard.
 * @pclock_delta: maximum delta in Hz between standard->pixelclock and
 *		the measured timings.
 * @match_reduced_fps: if true, then fail if V4L2_DV_FL_REDUCED_FPS does not
 * match.
 *
 * Returns true if the two timings match, returns false otherwise.
 */
bool v4l2_match_dv_timings(const struct v4l2_dv_timings *measured,
			   const struct v4l2_dv_timings *standard,
			   unsigned pclock_delta, bool match_reduced_fps);

/**
 * v4l2_print_dv_timings() - log the contents of a dv_timings struct
 * @dev_prefix:device prefix for each log line.
 * @prefix:	additional prefix for each log line, may be NULL.
 * @t:		the timings data.
 * @detailed:	if true, give a detailed log.
 */
void v4l2_print_dv_timings(const char *dev_prefix, const char *prefix,
			   const struct v4l2_dv_timings *t, bool detailed);

/**
 * v4l2_detect_cvt - detect if the given timings follow the CVT standard
 *
 * @frame_height: the total height of the frame (including blanking) in lines.
 * @hfreq: the horizontal frequency in Hz.
 * @vsync: the height of the vertical sync in lines.
 * @active_width: active width of image (does not include blanking). This
 * information is needed only in case of version 2 of reduced blanking.
 * In other cases, this parameter does not have any effect on timings.
 * @polarities: the horizontal and vertical polarities (same as struct
 *		v4l2_bt_timings polarities).
 * @interlaced: if this flag is true, it indicates interlaced format
 * @cap: the v4l2_dv_timings_cap capabilities.
 * @fmt: the resulting timings.
 *
 * This function will attempt to detect if the given values correspond to a
 * valid CVT format. If so, then it will return true, and fmt will be filled
 * in with the found CVT timings.
 */
bool v4l2_detect_cvt(unsigned int frame_height, unsigned int hfreq,
		     unsigned int vsync, unsigned int active_width,
		     u32 polarities, bool interlaced,
		     const struct v4l2_dv_timings_cap *cap,
		     struct v4l2_dv_timings *fmt);

/**
 * v4l2_detect_gtf - detect if the given timings follow the GTF standard
 *
 * @frame_height: the total height of the frame (including blanking) in lines.
 * @hfreq: the horizontal frequency in Hz.
 * @vsync: the height of the vertical sync in lines.
 * @polarities: the horizontal and vertical polarities (same as struct
 *		v4l2_bt_timings polarities).
 * @interlaced: if this flag is true, it indicates interlaced format
 * @aspect: preferred aspect ratio. GTF has no method of determining the
 *		aspect ratio in order to derive the image width from the
 *		image height, so it has to be passed explicitly. Usually
 *		the native screen aspect ratio is used for this. If it
 *		is not filled in correctly, then 16:9 will be assumed.
 * @cap: the v4l2_dv_timings_cap capabilities.
 * @fmt: the resulting timings.
 *
 * This function will attempt to detect if the given values correspond to a
 * valid GTF format. If so, then it will return true, and fmt will be filled
 * in with the found GTF timings.
 */
bool v4l2_detect_gtf(unsigned int frame_height, unsigned int hfreq,
		     unsigned int vsync, u32 polarities, bool interlaced,
		     struct v4l2_fract aspect,
		     const struct v4l2_dv_timings_cap *cap,
		     struct v4l2_dv_timings *fmt);

/**
 * v4l2_calc_aspect_ratio - calculate the aspect ratio based on bytes
 *	0x15 and 0x16 from the EDID.
 *
 * @hor_landscape: byte 0x15 from the EDID.
 * @vert_portrait: byte 0x16 from the EDID.
 *
 * Determines the aspect ratio from the EDID.
 * See VESA Enhanced EDID standard, release A, rev 2, section 3.6.2:
 * "Horizontal and Vertical Screen Size or Aspect Ratio"
 */
struct v4l2_fract v4l2_calc_aspect_ratio(u8 hor_landscape, u8 vert_portrait);

/**
 * v4l2_dv_timings_aspect_ratio - calculate the aspect ratio based on the
 *	v4l2_dv_timings information.
 *
 * @t: the timings data.
 */
struct v4l2_fract v4l2_dv_timings_aspect_ratio(const struct v4l2_dv_timings *t);

/**
 * can_reduce_fps - check if conditions for reduced fps are true.
 * @bt: v4l2 timing structure
 *
 * For different timings reduced fps is allowed if the following conditions
 * are met:
 *
 *   - For CVT timings: if reduced blanking v2 (vsync == 8) is true.
 *   - For CEA861 timings: if %V4L2_DV_FL_CAN_REDUCE_FPS flag is true.
 */
static inline  bool can_reduce_fps(struct v4l2_bt_timings *bt)
{
	if ((bt->standards & V4L2_DV_BT_STD_CVT) && (bt->vsync == 8))
		return true;

	if ((bt->standards & V4L2_DV_BT_STD_CEA861) &&
	    (bt->flags & V4L2_DV_FL_CAN_REDUCE_FPS))
		return true;

	return false;
}

/**
 * struct v4l2_hdmi_colorimetry - describes the HDMI colorimetry information
 * @colorspace:		enum v4l2_colorspace, the colorspace
 * @ycbcr_enc:		enum v4l2_ycbcr_encoding, Y'CbCr encoding
 * @quantization:	enum v4l2_quantization, colorspace quantization
 * @xfer_func:		enum v4l2_xfer_func, colorspace transfer function
 */
struct v4l2_hdmi_colorimetry {
	enum v4l2_colorspace colorspace;
	enum v4l2_ycbcr_encoding ycbcr_enc;
	enum v4l2_quantization quantization;
	enum v4l2_xfer_func xfer_func;
};

struct hdmi_avi_infoframe;
struct hdmi_vendor_infoframe;

struct v4l2_hdmi_colorimetry
v4l2_hdmi_rx_colorimetry(const struct hdmi_avi_infoframe *avi,
			 const struct hdmi_vendor_infoframe *hdmi,
			 unsigned int height);

unsigned int v4l2_num_edid_blocks(const u8 *edid, unsigned int max_blocks);
u16 v4l2_get_edid_phys_addr(const u8 *edid, unsigned int size,
			    unsigned int *offset);
void v4l2_set_edid_phys_addr(u8 *edid, unsigned int size, u16 phys_addr);
u16 v4l2_phys_addr_for_input(u16 phys_addr, u8 input);
int v4l2_phys_addr_validate(u16 phys_addr, u16 *parent, u16 *port);

/* Add support for exporting InfoFrames to debugfs */

/*
 * HDMI InfoFrames start with a 3 byte header, then a checksum,
 * followed by the actual IF payload.
 *
 * The payload length is limited to 30 bytes according to the HDMI spec,
 * but since the length is encoded in 5 bits, it can be 31 bytes theoretically.
 * So set the max length as 31 + 3 (header) + 1 (checksum) = 35.
 */
#define V4L2_DEBUGFS_IF_MAX_LEN (35)

#define V4L2_DEBUGFS_IF_AVI	BIT(0)
#define V4L2_DEBUGFS_IF_AUDIO	BIT(1)
#define V4L2_DEBUGFS_IF_SPD	BIT(2)
#define V4L2_DEBUGFS_IF_HDMI	BIT(3)

typedef ssize_t (*v4l2_debugfs_if_read_t)(u32 type, void *priv,
					  struct file *filp, char __user *ubuf,
					  size_t count, loff_t *ppos);

struct v4l2_debugfs_if {
	struct dentry *if_dir;
	void *priv;

	v4l2_debugfs_if_read_t if_read;
};

#ifdef CONFIG_DEBUG_FS
struct v4l2_debugfs_if *v4l2_debugfs_if_alloc(struct dentry *root, u32 if_types,
					      void *priv,
					      v4l2_debugfs_if_read_t if_read);
void v4l2_debugfs_if_free(struct v4l2_debugfs_if *infoframes);
#else
static inline
struct v4l2_debugfs_if *v4l2_debugfs_if_alloc(struct dentry *root, u32 if_types,
					      void *priv,
					      v4l2_debugfs_if_read_t if_read)
{
	return NULL;
}

static inline void v4l2_debugfs_if_free(struct v4l2_debugfs_if *infoframes)
{
}
#endif

#endif
