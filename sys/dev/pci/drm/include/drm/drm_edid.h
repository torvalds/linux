/*
 * Copyright © 2007-2008 Intel Corporation
 *   Jesse Barnes <jesse.barnes@intel.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */
#ifndef __DRM_EDID_H__
#define __DRM_EDID_H__

#include <linux/types.h>

enum hdmi_quantization_range;
struct drm_connector;
struct drm_device;
struct drm_display_mode;
struct drm_edid;
struct drm_printer;
struct hdmi_avi_infoframe;
struct hdmi_vendor_infoframe;
struct i2c_adapter;

#define EDID_LENGTH 128
#define DDC_ADDR 0x50
#define DDC_ADDR2 0x52 /* E-DDC 1.2 - where DisplayID can hide */

#define CEA_EXT	    0x02
#define VTB_EXT	    0x10
#define DI_EXT	    0x40
#define LS_EXT	    0x50
#define MI_EXT	    0x60
#define DISPLAYID_EXT 0x70

struct est_timings {
	u8 t1;
	u8 t2;
	u8 mfg_rsvd;
} __packed;

/* 00=16:10, 01=4:3, 10=5:4, 11=16:9 */
#define EDID_TIMING_ASPECT_SHIFT 6
#define EDID_TIMING_ASPECT_MASK  (0x3 << EDID_TIMING_ASPECT_SHIFT)

/* need to add 60 */
#define EDID_TIMING_VFREQ_SHIFT  0
#define EDID_TIMING_VFREQ_MASK   (0x3f << EDID_TIMING_VFREQ_SHIFT)

struct std_timing {
	u8 hsize; /* need to multiply by 8 then add 248 */
	u8 vfreq_aspect;
} __packed;

#define DRM_EDID_PT_HSYNC_POSITIVE (1 << 1)
#define DRM_EDID_PT_VSYNC_POSITIVE (1 << 2)
#define DRM_EDID_PT_SEPARATE_SYNC  (3 << 3)
#define DRM_EDID_PT_STEREO         (1 << 5)
#define DRM_EDID_PT_INTERLACED     (1 << 7)

/* If detailed data is pixel timing */
struct detailed_pixel_timing {
	u8 hactive_lo;
	u8 hblank_lo;
	u8 hactive_hblank_hi;
	u8 vactive_lo;
	u8 vblank_lo;
	u8 vactive_vblank_hi;
	u8 hsync_offset_lo;
	u8 hsync_pulse_width_lo;
	u8 vsync_offset_pulse_width_lo;
	u8 hsync_vsync_offset_pulse_width_hi;
	u8 width_mm_lo;
	u8 height_mm_lo;
	u8 width_height_mm_hi;
	u8 hborder;
	u8 vborder;
	u8 misc;
} __packed;

/* If it's not pixel timing, it'll be one of the below */
struct detailed_data_string {
	u8 str[13];
} __packed;

#define DRM_EDID_RANGE_OFFSET_MIN_VFREQ (1 << 0) /* 1.4 */
#define DRM_EDID_RANGE_OFFSET_MAX_VFREQ (1 << 1) /* 1.4 */
#define DRM_EDID_RANGE_OFFSET_MIN_HFREQ (1 << 2) /* 1.4 */
#define DRM_EDID_RANGE_OFFSET_MAX_HFREQ (1 << 3) /* 1.4 */

#define DRM_EDID_DEFAULT_GTF_SUPPORT_FLAG   0x00 /* 1.3 */
#define DRM_EDID_RANGE_LIMITS_ONLY_FLAG     0x01 /* 1.4 */
#define DRM_EDID_SECONDARY_GTF_SUPPORT_FLAG 0x02 /* 1.3 */
#define DRM_EDID_CVT_SUPPORT_FLAG           0x04 /* 1.4 */

#define DRM_EDID_CVT_FLAGS_STANDARD_BLANKING (1 << 3)
#define DRM_EDID_CVT_FLAGS_REDUCED_BLANKING  (1 << 4)

struct detailed_data_monitor_range {
	u8 min_vfreq;
	u8 max_vfreq;
	u8 min_hfreq_khz;
	u8 max_hfreq_khz;
	u8 pixel_clock_mhz; /* need to multiply by 10 */
	u8 flags;
	union {
		struct {
			u8 reserved;
			u8 hfreq_start_khz; /* need to multiply by 2 */
			u8 c; /* need to divide by 2 */
			__le16 m;
			u8 k;
			u8 j; /* need to divide by 2 */
		} __packed gtf2;
		struct {
			u8 version;
			u8 data1; /* high 6 bits: extra clock resolution */
			u8 data2; /* plus low 2 of above: max hactive */
			u8 supported_aspects;
			u8 flags; /* preferred aspect and blanking support */
			u8 supported_scalings;
			u8 preferred_refresh;
		} __packed cvt;
	} __packed formula;
} __packed;

struct detailed_data_wpindex {
	u8 white_yx_lo; /* Lower 2 bits each */
	u8 white_x_hi;
	u8 white_y_hi;
	u8 gamma; /* need to divide by 100 then add 1 */
} __packed;

struct detailed_data_color_point {
	u8 windex1;
	u8 wpindex1[3];
	u8 windex2;
	u8 wpindex2[3];
} __packed;

struct cvt_timing {
	u8 code[3];
} __packed;

struct detailed_non_pixel {
	u8 pad1;
	u8 type; /* ff=serial, fe=string, fd=monitor range, fc=monitor name
		    fb=color point data, fa=standard timing data,
		    f9=undefined, f8=mfg. reserved */
	u8 pad2;
	union {
		struct detailed_data_string str;
		struct detailed_data_monitor_range range;
		struct detailed_data_wpindex color;
		struct std_timing timings[6];
		struct cvt_timing cvt[4];
	} __packed data;
} __packed;

#define EDID_DETAIL_EST_TIMINGS 0xf7
#define EDID_DETAIL_CVT_3BYTE 0xf8
#define EDID_DETAIL_COLOR_MGMT_DATA 0xf9
#define EDID_DETAIL_STD_MODES 0xfa
#define EDID_DETAIL_MONITOR_CPDATA 0xfb
#define EDID_DETAIL_MONITOR_NAME 0xfc
#define EDID_DETAIL_MONITOR_RANGE 0xfd
#define EDID_DETAIL_MONITOR_STRING 0xfe
#define EDID_DETAIL_MONITOR_SERIAL 0xff

struct detailed_timing {
	__le16 pixel_clock; /* need to multiply by 10 KHz */
	union {
		struct detailed_pixel_timing pixel_data;
		struct detailed_non_pixel other_data;
	} __packed data;
} __packed;

#define DRM_EDID_INPUT_SERRATION_VSYNC (1 << 0)
#define DRM_EDID_INPUT_SYNC_ON_GREEN   (1 << 1)
#define DRM_EDID_INPUT_COMPOSITE_SYNC  (1 << 2)
#define DRM_EDID_INPUT_SEPARATE_SYNCS  (1 << 3)
#define DRM_EDID_INPUT_BLANK_TO_BLACK  (1 << 4)
#define DRM_EDID_INPUT_VIDEO_LEVEL     (3 << 5)
#define DRM_EDID_INPUT_DIGITAL         (1 << 7)
#define DRM_EDID_DIGITAL_DEPTH_MASK    (7 << 4) /* 1.4 */
#define DRM_EDID_DIGITAL_DEPTH_UNDEF   (0 << 4) /* 1.4 */
#define DRM_EDID_DIGITAL_DEPTH_6       (1 << 4) /* 1.4 */
#define DRM_EDID_DIGITAL_DEPTH_8       (2 << 4) /* 1.4 */
#define DRM_EDID_DIGITAL_DEPTH_10      (3 << 4) /* 1.4 */
#define DRM_EDID_DIGITAL_DEPTH_12      (4 << 4) /* 1.4 */
#define DRM_EDID_DIGITAL_DEPTH_14      (5 << 4) /* 1.4 */
#define DRM_EDID_DIGITAL_DEPTH_16      (6 << 4) /* 1.4 */
#define DRM_EDID_DIGITAL_DEPTH_RSVD    (7 << 4) /* 1.4 */
#define DRM_EDID_DIGITAL_TYPE_MASK     (7 << 0) /* 1.4 */
#define DRM_EDID_DIGITAL_TYPE_UNDEF    (0 << 0) /* 1.4 */
#define DRM_EDID_DIGITAL_TYPE_DVI      (1 << 0) /* 1.4 */
#define DRM_EDID_DIGITAL_TYPE_HDMI_A   (2 << 0) /* 1.4 */
#define DRM_EDID_DIGITAL_TYPE_HDMI_B   (3 << 0) /* 1.4 */
#define DRM_EDID_DIGITAL_TYPE_MDDI     (4 << 0) /* 1.4 */
#define DRM_EDID_DIGITAL_TYPE_DP       (5 << 0) /* 1.4 */
#define DRM_EDID_DIGITAL_DFP_1_X       (1 << 0) /* 1.3 */

#define DRM_EDID_FEATURE_DEFAULT_GTF      (1 << 0) /* 1.2 */
#define DRM_EDID_FEATURE_CONTINUOUS_FREQ  (1 << 0) /* 1.4 */
#define DRM_EDID_FEATURE_PREFERRED_TIMING (1 << 1)
#define DRM_EDID_FEATURE_STANDARD_COLOR   (1 << 2)
/* If analog */
#define DRM_EDID_FEATURE_DISPLAY_TYPE     (3 << 3) /* 00=mono, 01=rgb, 10=non-rgb, 11=unknown */
/* If digital */
#define DRM_EDID_FEATURE_COLOR_MASK	  (3 << 3)
#define DRM_EDID_FEATURE_RGB		  (0 << 3)
#define DRM_EDID_FEATURE_RGB_YCRCB444	  (1 << 3)
#define DRM_EDID_FEATURE_RGB_YCRCB422	  (2 << 3)
#define DRM_EDID_FEATURE_RGB_YCRCB	  (3 << 3) /* both 4:4:4 and 4:2:2 */

#define DRM_EDID_FEATURE_PM_ACTIVE_OFF    (1 << 5)
#define DRM_EDID_FEATURE_PM_SUSPEND       (1 << 6)
#define DRM_EDID_FEATURE_PM_STANDBY       (1 << 7)

#define DRM_EDID_HDMI_DC_48               (1 << 6)
#define DRM_EDID_HDMI_DC_36               (1 << 5)
#define DRM_EDID_HDMI_DC_30               (1 << 4)
#define DRM_EDID_HDMI_DC_Y444             (1 << 3)

/* YCBCR 420 deep color modes */
#define DRM_EDID_YCBCR420_DC_48		  (1 << 2)
#define DRM_EDID_YCBCR420_DC_36		  (1 << 1)
#define DRM_EDID_YCBCR420_DC_30		  (1 << 0)
#define DRM_EDID_YCBCR420_DC_MASK (DRM_EDID_YCBCR420_DC_48 | \
				    DRM_EDID_YCBCR420_DC_36 | \
				    DRM_EDID_YCBCR420_DC_30)

/* HDMI 2.1 additional fields */
#define DRM_EDID_MAX_FRL_RATE_MASK		0xf0
#define DRM_EDID_FAPA_START_LOCATION		(1 << 0)
#define DRM_EDID_ALLM				(1 << 1)
#define DRM_EDID_FVA				(1 << 2)

/* Deep Color specific */
#define DRM_EDID_DC_30BIT_420			(1 << 0)
#define DRM_EDID_DC_36BIT_420			(1 << 1)
#define DRM_EDID_DC_48BIT_420			(1 << 2)

/* VRR specific */
#define DRM_EDID_CNMVRR				(1 << 3)
#define DRM_EDID_CINEMA_VRR			(1 << 4)
#define DRM_EDID_MDELTA				(1 << 5)
#define DRM_EDID_VRR_MAX_UPPER_MASK		0xc0
#define DRM_EDID_VRR_MAX_LOWER_MASK		0xff
#define DRM_EDID_VRR_MIN_MASK			0x3f

/* DSC specific */
#define DRM_EDID_DSC_10BPC			(1 << 0)
#define DRM_EDID_DSC_12BPC			(1 << 1)
#define DRM_EDID_DSC_16BPC			(1 << 2)
#define DRM_EDID_DSC_ALL_BPP			(1 << 3)
#define DRM_EDID_DSC_NATIVE_420			(1 << 6)
#define DRM_EDID_DSC_1P2			(1 << 7)
#define DRM_EDID_DSC_MAX_FRL_RATE_MASK		0xf0
#define DRM_EDID_DSC_MAX_SLICES			0xf
#define DRM_EDID_DSC_TOTAL_CHUNK_KBYTES		0x3f

struct drm_edid_product_id {
	__be16 manufacturer_name;
	__le16 product_code;
	__le32 serial_number;
	u8 week_of_manufacture;
	u8 year_of_manufacture;
} __packed;

struct edid {
	u8 header[8];
	/* Vendor & product info */
	union {
		struct drm_edid_product_id product_id;
		struct {
			u8 mfg_id[2];
			u8 prod_code[2];
			u32 serial; /* FIXME: byte order */
			u8 mfg_week;
			u8 mfg_year;
		} __packed;
	} __packed;
	/* EDID version */
	u8 version;
	u8 revision;
	/* Display info: */
	u8 input;
	u8 width_cm;
	u8 height_cm;
	u8 gamma;
	u8 features;
	/* Color characteristics */
	u8 red_green_lo;
	u8 blue_white_lo;
	u8 red_x;
	u8 red_y;
	u8 green_x;
	u8 green_y;
	u8 blue_x;
	u8 blue_y;
	u8 white_x;
	u8 white_y;
	/* Est. timings and mfg rsvd timings*/
	struct est_timings established_timings;
	/* Standard timings 1-8*/
	struct std_timing standard_timings[8];
	/* Detailing timings 1-4 */
	struct detailed_timing detailed_timings[4];
	/* Number of 128 byte ext. blocks */
	u8 extensions;
	/* Checksum */
	u8 checksum;
} __packed;

/* EDID matching */
struct drm_edid_ident {
	/* ID encoded by drm_edid_encode_panel_id() */
	u32 panel_id;
	const char *name;
};

#define EDID_PRODUCT_ID(e) ((e)->prod_code[0] | ((e)->prod_code[1] << 8))

/* Short Audio Descriptor */
struct cea_sad {
	u8 format;
	u8 channels; /* max number of channels - 1 */
	u8 freq;
	u8 byte2; /* meaning depends on format */
};

int drm_edid_to_sad(const struct edid *edid, struct cea_sad **sads);
int drm_edid_to_speaker_allocation(const struct edid *edid, u8 **sadb);
int drm_av_sync_delay(struct drm_connector *connector,
		      const struct drm_display_mode *mode);

int
drm_hdmi_avi_infoframe_from_display_mode(struct hdmi_avi_infoframe *frame,
					 const struct drm_connector *connector,
					 const struct drm_display_mode *mode);
int
drm_hdmi_vendor_infoframe_from_display_mode(struct hdmi_vendor_infoframe *frame,
					    const struct drm_connector *connector,
					    const struct drm_display_mode *mode);

void
drm_hdmi_avi_infoframe_quant_range(struct hdmi_avi_infoframe *frame,
				   const struct drm_connector *connector,
				   const struct drm_display_mode *mode,
				   enum hdmi_quantization_range rgb_quant_range);

/**
 * drm_edid_decode_mfg_id - Decode the manufacturer ID
 * @mfg_id: The manufacturer ID
 * @vend: A 4-byte buffer to store the 3-letter vendor string plus a '\0'
 *	  termination
 */
static inline const char *drm_edid_decode_mfg_id(u16 mfg_id, char vend[4])
{
	vend[0] = '@' + ((mfg_id >> 10) & 0x1f);
	vend[1] = '@' + ((mfg_id >> 5) & 0x1f);
	vend[2] = '@' + ((mfg_id >> 0) & 0x1f);
	vend[3] = '\0';

	return vend;
}

/**
 * drm_edid_encode_panel_id - Encode an ID for matching against drm_edid_get_panel_id()
 * @vend_chr_0: First character of the vendor string.
 * @vend_chr_1: Second character of the vendor string.
 * @vend_chr_2: Third character of the vendor string.
 * @product_id: The 16-bit product ID.
 *
 * This is a macro so that it can be calculated at compile time and used
 * as an initializer.
 *
 * For instance:
 *   drm_edid_encode_panel_id('B', 'O', 'E', 0x2d08) => 0x09e52d08
 *
 * Return: a 32-bit ID per panel.
 */
#define drm_edid_encode_panel_id(vend_chr_0, vend_chr_1, vend_chr_2, product_id) \
	((((u32)(vend_chr_0) - '@') & 0x1f) << 26 | \
	 (((u32)(vend_chr_1) - '@') & 0x1f) << 21 | \
	 (((u32)(vend_chr_2) - '@') & 0x1f) << 16 | \
	 ((product_id) & 0xffff))

/**
 * drm_edid_decode_panel_id - Decode a panel ID from drm_edid_encode_panel_id()
 * @panel_id: The panel ID to decode.
 * @vend: A 4-byte buffer to store the 3-letter vendor string plus a '\0'
 *	  termination
 * @product_id: The product ID will be returned here.
 *
 * For instance, after:
 *   drm_edid_decode_panel_id(0x09e52d08, vend, &product_id)
 * These will be true:
 *   vend[0] = 'B'
 *   vend[1] = 'O'
 *   vend[2] = 'E'
 *   vend[3] = '\0'
 *   product_id = 0x2d08
 */
static inline void drm_edid_decode_panel_id(u32 panel_id, char vend[4], u16 *product_id)
{
	*product_id = (u16)(panel_id & 0xffff);
	drm_edid_decode_mfg_id(panel_id >> 16, vend);
}

bool drm_probe_ddc(struct i2c_adapter *adapter);
struct edid *drm_get_edid(struct drm_connector *connector,
			  struct i2c_adapter *adapter);
struct edid *drm_get_edid_switcheroo(struct drm_connector *connector,
				     struct i2c_adapter *adapter);
struct edid *drm_edid_duplicate(const struct edid *edid);
int drm_add_edid_modes(struct drm_connector *connector, struct edid *edid);
int drm_edid_override_connector_update(struct drm_connector *connector);

u8 drm_match_cea_mode(const struct drm_display_mode *to_match);
bool drm_detect_hdmi_monitor(const struct edid *edid);
bool drm_detect_monitor_audio(const struct edid *edid);
enum hdmi_quantization_range
drm_default_rgb_quant_range(const struct drm_display_mode *mode);
int drm_add_modes_noedid(struct drm_connector *connector,
			 int hdisplay, int vdisplay);

int drm_edid_header_is_valid(const void *edid);
bool drm_edid_is_valid(struct edid *edid);
void drm_edid_get_monitor_name(const struct edid *edid, char *name,
			       int buflen);
struct drm_display_mode *drm_mode_find_dmt(struct drm_device *dev,
					   int hsize, int vsize, int fresh,
					   bool rb);
struct drm_display_mode *
drm_display_mode_from_cea_vic(struct drm_device *dev,
			      u8 video_code);

/* Interface based on struct drm_edid */
const struct drm_edid *drm_edid_alloc(const void *edid, size_t size);
const struct drm_edid *drm_edid_dup(const struct drm_edid *drm_edid);
void drm_edid_free(const struct drm_edid *drm_edid);
bool drm_edid_valid(const struct drm_edid *drm_edid);
const struct edid *drm_edid_raw(const struct drm_edid *drm_edid);
const struct drm_edid *drm_edid_read(struct drm_connector *connector);
const struct drm_edid *drm_edid_read_ddc(struct drm_connector *connector,
					 struct i2c_adapter *adapter);
const struct drm_edid *drm_edid_read_custom(struct drm_connector *connector,
					    int (*read_block)(void *context, u8 *buf, unsigned int block, size_t len),
					    void *context);
const struct drm_edid *drm_edid_read_base_block(struct i2c_adapter *adapter);
const struct drm_edid *drm_edid_read_switcheroo(struct drm_connector *connector,
						struct i2c_adapter *adapter);
int drm_edid_connector_update(struct drm_connector *connector,
			      const struct drm_edid *edid);
int drm_edid_connector_add_modes(struct drm_connector *connector);
bool drm_edid_is_digital(const struct drm_edid *drm_edid);
void drm_edid_get_product_id(const struct drm_edid *drm_edid,
			     struct drm_edid_product_id *id);
void drm_edid_print_product_id(struct drm_printer *p,
			       const struct drm_edid_product_id *id, bool raw);
u32 drm_edid_get_panel_id(const struct drm_edid *drm_edid);
bool drm_edid_match(const struct drm_edid *drm_edid,
		    const struct drm_edid_ident *ident);

#endif /* __DRM_EDID_H__ */
