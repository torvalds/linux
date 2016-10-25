/*
 * Copyright (C) 2012 Avionic Design GmbH
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#ifndef __LINUX_HDMI_H_
#define __LINUX_HDMI_H_

#include <linux/types.h>
#include <linux/device.h>

enum hdmi_infoframe_type {
	HDMI_INFOFRAME_TYPE_VENDOR = 0x81,
	HDMI_INFOFRAME_TYPE_AVI = 0x82,
	HDMI_INFOFRAME_TYPE_SPD = 0x83,
	HDMI_INFOFRAME_TYPE_AUDIO = 0x84,
};

#define HDMI_IEEE_OUI 0x000c03
#define HDMI_INFOFRAME_HEADER_SIZE  4
#define HDMI_AVI_INFOFRAME_SIZE    13
#define HDMI_SPD_INFOFRAME_SIZE    25
#define HDMI_AUDIO_INFOFRAME_SIZE  10

#define HDMI_INFOFRAME_SIZE(type)	\
	(HDMI_INFOFRAME_HEADER_SIZE + HDMI_ ## type ## _INFOFRAME_SIZE)

struct hdmi_any_infoframe {
	enum hdmi_infoframe_type type;
	unsigned char version;
	unsigned char length;
};

enum hdmi_colorspace {
	HDMI_COLORSPACE_RGB,
	HDMI_COLORSPACE_YUV422,
	HDMI_COLORSPACE_YUV444,
	HDMI_COLORSPACE_YUV420,
	HDMI_COLORSPACE_RESERVED4,
	HDMI_COLORSPACE_RESERVED5,
	HDMI_COLORSPACE_RESERVED6,
	HDMI_COLORSPACE_IDO_DEFINED,
};

enum hdmi_scan_mode {
	HDMI_SCAN_MODE_NONE,
	HDMI_SCAN_MODE_OVERSCAN,
	HDMI_SCAN_MODE_UNDERSCAN,
	HDMI_SCAN_MODE_RESERVED,
};

enum hdmi_colorimetry {
	HDMI_COLORIMETRY_NONE,
	HDMI_COLORIMETRY_ITU_601,
	HDMI_COLORIMETRY_ITU_709,
	HDMI_COLORIMETRY_EXTENDED,
};

enum hdmi_picture_aspect {
	HDMI_PICTURE_ASPECT_NONE,
	HDMI_PICTURE_ASPECT_4_3,
	HDMI_PICTURE_ASPECT_16_9,
	HDMI_PICTURE_ASPECT_64_27,
	HDMI_PICTURE_ASPECT_256_135,
	HDMI_PICTURE_ASPECT_RESERVED,
};

enum hdmi_active_aspect {
	HDMI_ACTIVE_ASPECT_16_9_TOP = 2,
	HDMI_ACTIVE_ASPECT_14_9_TOP = 3,
	HDMI_ACTIVE_ASPECT_16_9_CENTER = 4,
	HDMI_ACTIVE_ASPECT_PICTURE = 8,
	HDMI_ACTIVE_ASPECT_4_3 = 9,
	HDMI_ACTIVE_ASPECT_16_9 = 10,
	HDMI_ACTIVE_ASPECT_14_9 = 11,
	HDMI_ACTIVE_ASPECT_4_3_SP_14_9 = 13,
	HDMI_ACTIVE_ASPECT_16_9_SP_14_9 = 14,
	HDMI_ACTIVE_ASPECT_16_9_SP_4_3 = 15,
};

enum hdmi_extended_colorimetry {
	HDMI_EXTENDED_COLORIMETRY_XV_YCC_601,
	HDMI_EXTENDED_COLORIMETRY_XV_YCC_709,
	HDMI_EXTENDED_COLORIMETRY_S_YCC_601,
	HDMI_EXTENDED_COLORIMETRY_ADOBE_YCC_601,
	HDMI_EXTENDED_COLORIMETRY_ADOBE_RGB,

	/* The following EC values are only defined in CEA-861-F. */
	HDMI_EXTENDED_COLORIMETRY_BT2020_CONST_LUM,
	HDMI_EXTENDED_COLORIMETRY_BT2020,
	HDMI_EXTENDED_COLORIMETRY_RESERVED,
};

enum hdmi_quantization_range {
	HDMI_QUANTIZATION_RANGE_DEFAULT,
	HDMI_QUANTIZATION_RANGE_LIMITED,
	HDMI_QUANTIZATION_RANGE_FULL,
	HDMI_QUANTIZATION_RANGE_RESERVED,
};

/* non-uniform picture scaling */
enum hdmi_nups {
	HDMI_NUPS_UNKNOWN,
	HDMI_NUPS_HORIZONTAL,
	HDMI_NUPS_VERTICAL,
	HDMI_NUPS_BOTH,
};

enum hdmi_ycc_quantization_range {
	HDMI_YCC_QUANTIZATION_RANGE_LIMITED,
	HDMI_YCC_QUANTIZATION_RANGE_FULL,
};

enum hdmi_content_type {
	HDMI_CONTENT_TYPE_GRAPHICS,
	HDMI_CONTENT_TYPE_PHOTO,
	HDMI_CONTENT_TYPE_CINEMA,
	HDMI_CONTENT_TYPE_GAME,
};

struct hdmi_avi_infoframe {
	enum hdmi_infoframe_type type;
	unsigned char version;
	unsigned char length;
	enum hdmi_colorspace colorspace;
	enum hdmi_scan_mode scan_mode;
	enum hdmi_colorimetry colorimetry;
	enum hdmi_picture_aspect picture_aspect;
	enum hdmi_active_aspect active_aspect;
	bool itc;
	enum hdmi_extended_colorimetry extended_colorimetry;
	enum hdmi_quantization_range quantization_range;
	enum hdmi_nups nups;
	unsigned char video_code;
	enum hdmi_ycc_quantization_range ycc_quantization_range;
	enum hdmi_content_type content_type;
	unsigned char pixel_repeat;
	unsigned short top_bar;
	unsigned short bottom_bar;
	unsigned short left_bar;
	unsigned short right_bar;
};

int hdmi_avi_infoframe_init(struct hdmi_avi_infoframe *frame);
ssize_t hdmi_avi_infoframe_pack(struct hdmi_avi_infoframe *frame, void *buffer,
				size_t size);

enum hdmi_spd_sdi {
	HDMI_SPD_SDI_UNKNOWN,
	HDMI_SPD_SDI_DSTB,
	HDMI_SPD_SDI_DVDP,
	HDMI_SPD_SDI_DVHS,
	HDMI_SPD_SDI_HDDVR,
	HDMI_SPD_SDI_DVC,
	HDMI_SPD_SDI_DSC,
	HDMI_SPD_SDI_VCD,
	HDMI_SPD_SDI_GAME,
	HDMI_SPD_SDI_PC,
	HDMI_SPD_SDI_BD,
	HDMI_SPD_SDI_SACD,
	HDMI_SPD_SDI_HDDVD,
	HDMI_SPD_SDI_PMP,
};

struct hdmi_spd_infoframe {
	enum hdmi_infoframe_type type;
	unsigned char version;
	unsigned char length;
	char vendor[8];
	char product[16];
	enum hdmi_spd_sdi sdi;
};

int hdmi_spd_infoframe_init(struct hdmi_spd_infoframe *frame,
			    const char *vendor, const char *product);
ssize_t hdmi_spd_infoframe_pack(struct hdmi_spd_infoframe *frame, void *buffer,
				size_t size);

enum hdmi_audio_coding_type {
	HDMI_AUDIO_CODING_TYPE_STREAM,
	HDMI_AUDIO_CODING_TYPE_PCM,
	HDMI_AUDIO_CODING_TYPE_AC3,
	HDMI_AUDIO_CODING_TYPE_MPEG1,
	HDMI_AUDIO_CODING_TYPE_MP3,
	HDMI_AUDIO_CODING_TYPE_MPEG2,
	HDMI_AUDIO_CODING_TYPE_AAC_LC,
	HDMI_AUDIO_CODING_TYPE_DTS,
	HDMI_AUDIO_CODING_TYPE_ATRAC,
	HDMI_AUDIO_CODING_TYPE_DSD,
	HDMI_AUDIO_CODING_TYPE_EAC3,
	HDMI_AUDIO_CODING_TYPE_DTS_HD,
	HDMI_AUDIO_CODING_TYPE_MLP,
	HDMI_AUDIO_CODING_TYPE_DST,
	HDMI_AUDIO_CODING_TYPE_WMA_PRO,
	HDMI_AUDIO_CODING_TYPE_CXT,
};

enum hdmi_audio_sample_size {
	HDMI_AUDIO_SAMPLE_SIZE_STREAM,
	HDMI_AUDIO_SAMPLE_SIZE_16,
	HDMI_AUDIO_SAMPLE_SIZE_20,
	HDMI_AUDIO_SAMPLE_SIZE_24,
};

enum hdmi_audio_sample_frequency {
	HDMI_AUDIO_SAMPLE_FREQUENCY_STREAM,
	HDMI_AUDIO_SAMPLE_FREQUENCY_32000,
	HDMI_AUDIO_SAMPLE_FREQUENCY_44100,
	HDMI_AUDIO_SAMPLE_FREQUENCY_48000,
	HDMI_AUDIO_SAMPLE_FREQUENCY_88200,
	HDMI_AUDIO_SAMPLE_FREQUENCY_96000,
	HDMI_AUDIO_SAMPLE_FREQUENCY_176400,
	HDMI_AUDIO_SAMPLE_FREQUENCY_192000,
};

enum hdmi_audio_coding_type_ext {
	/* Refer to Audio Coding Type (CT) field in Data Byte 1 */
	HDMI_AUDIO_CODING_TYPE_EXT_CT,

	/*
	 * The next three CXT values are defined in CEA-861-E only.
	 * They do not exist in older versions, and in CEA-861-F they are
	 * defined as 'Not in use'.
	 */
	HDMI_AUDIO_CODING_TYPE_EXT_HE_AAC,
	HDMI_AUDIO_CODING_TYPE_EXT_HE_AAC_V2,
	HDMI_AUDIO_CODING_TYPE_EXT_MPEG_SURROUND,

	/* The following CXT values are only defined in CEA-861-F. */
	HDMI_AUDIO_CODING_TYPE_EXT_MPEG4_HE_AAC,
	HDMI_AUDIO_CODING_TYPE_EXT_MPEG4_HE_AAC_V2,
	HDMI_AUDIO_CODING_TYPE_EXT_MPEG4_AAC_LC,
	HDMI_AUDIO_CODING_TYPE_EXT_DRA,
	HDMI_AUDIO_CODING_TYPE_EXT_MPEG4_HE_AAC_SURROUND,
	HDMI_AUDIO_CODING_TYPE_EXT_MPEG4_AAC_LC_SURROUND = 10,
};

struct hdmi_audio_infoframe {
	enum hdmi_infoframe_type type;
	unsigned char version;
	unsigned char length;
	unsigned char channels;
	enum hdmi_audio_coding_type coding_type;
	enum hdmi_audio_sample_size sample_size;
	enum hdmi_audio_sample_frequency sample_frequency;
	enum hdmi_audio_coding_type_ext coding_type_ext;
	unsigned char channel_allocation;
	unsigned char level_shift_value;
	bool downmix_inhibit;

};

int hdmi_audio_infoframe_init(struct hdmi_audio_infoframe *frame);
ssize_t hdmi_audio_infoframe_pack(struct hdmi_audio_infoframe *frame,
				  void *buffer, size_t size);

enum hdmi_3d_structure {
	HDMI_3D_STRUCTURE_INVALID = -1,
	HDMI_3D_STRUCTURE_FRAME_PACKING = 0,
	HDMI_3D_STRUCTURE_FIELD_ALTERNATIVE,
	HDMI_3D_STRUCTURE_LINE_ALTERNATIVE,
	HDMI_3D_STRUCTURE_SIDE_BY_SIDE_FULL,
	HDMI_3D_STRUCTURE_L_DEPTH,
	HDMI_3D_STRUCTURE_L_DEPTH_GFX_GFX_DEPTH,
	HDMI_3D_STRUCTURE_TOP_AND_BOTTOM,
	HDMI_3D_STRUCTURE_SIDE_BY_SIDE_HALF = 8,
};


struct hdmi_vendor_infoframe {
	enum hdmi_infoframe_type type;
	unsigned char version;
	unsigned char length;
	unsigned int oui;
	u8 vic;
	enum hdmi_3d_structure s3d_struct;
	unsigned int s3d_ext_data;
};

int hdmi_vendor_infoframe_init(struct hdmi_vendor_infoframe *frame);
ssize_t hdmi_vendor_infoframe_pack(struct hdmi_vendor_infoframe *frame,
				   void *buffer, size_t size);

union hdmi_vendor_any_infoframe {
	struct {
		enum hdmi_infoframe_type type;
		unsigned char version;
		unsigned char length;
		unsigned int oui;
	} any;
	struct hdmi_vendor_infoframe hdmi;
};

/**
 * union hdmi_infoframe - overall union of all abstract infoframe representations
 * @any: generic infoframe
 * @avi: avi infoframe
 * @spd: spd infoframe
 * @vendor: union of all vendor infoframes
 * @audio: audio infoframe
 *
 * This is used by the generic pack function. This works since all infoframes
 * have the same header which also indicates which type of infoframe should be
 * packed.
 */
union hdmi_infoframe {
	struct hdmi_any_infoframe any;
	struct hdmi_avi_infoframe avi;
	struct hdmi_spd_infoframe spd;
	union hdmi_vendor_any_infoframe vendor;
	struct hdmi_audio_infoframe audio;
};

ssize_t
hdmi_infoframe_pack(union hdmi_infoframe *frame, void *buffer, size_t size);
int hdmi_infoframe_unpack(union hdmi_infoframe *frame, void *buffer);
void hdmi_infoframe_log(const char *level, struct device *dev,
			union hdmi_infoframe *frame);

#endif /* _DRM_HDMI_H */
