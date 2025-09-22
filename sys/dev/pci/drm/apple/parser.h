// SPDX-License-Identifier: GPL-2.0-only OR MIT
/* Copyright 2021 Alyssa Rosenzweig <alyssa@rosenzweig.io> */

#ifndef __APPLE_DCP_PARSER_H__
#define __APPLE_DCP_PARSER_H__

/* For mode parsing */
#include <drm/drm_modes.h>

struct apple_dcp;

struct dcp_parse_ctx {
	struct apple_dcp *dcp;
	const void *blob;
	u32 pos, len;
};

enum dcp_color_eotf {
	DCP_EOTF_SDR_GAMMA = 0, // "SDR gamma"
	DCP_EOTF_HDR_GAMMA = 1, // "HDR gamma"
	DCP_EOTF_ST_2084   = 2, // "ST 2084 (PQ)"
	DCP_EOTF_BT_2100   = 3, // "BT.2100 (HLG)"
	DCP_EOTF_COUNT
};

enum dcp_color_format {
	DCP_COLOR_FORMAT_RGB                 =  0, // "RGB"
	DCP_COLOR_FORMAT_YCBCR420            =  1, // "YUV 4:2:0"
	DCP_COLOR_FORMAT_YCBCR422            =  3, // "YUV 4:2:2"
	DCP_COLOR_FORMAT_YCBCR444            =  2, // "YUV 4:4:4"
	DCP_COLOR_FORMAT_DV_NATIVE           =  4, // "DolbyVision (native)"
	DCP_COLOR_FORMAT_DV_HDMI             =  5, // "DolbyVision (HDMI)"
	DCP_COLOR_FORMAT_YCBCR422_DP         =  6, // "YCbCr 4:2:2 (DP tunnel)"
	DCP_COLOR_FORMAT_YCBCR422_HDMI       =  7, // "YCbCr 4:2:2 (HDMI tunnel)"
	DCP_COLOR_FORMAT_DV_LL_YCBCR422      =  8, // "DolbyVision LL YCbCr 4:2:2"
	DCP_COLOR_FORMAT_DV_LL_YCBCR422_DP   =  9, // "DolbyVision LL YCbCr 4:2:2 (DP)"
	DCP_COLOR_FORMAT_DV_LL_YCBCR422_HDMI = 10, // "DolbyVision LL YCbCr 4:2:2 (HDMI)"
	DCP_COLOR_FORMAT_DV_LL_YCBCR444      = 11, // "DolbyVision LL YCbCr 4:4:4"
	DCP_COLOR_FORMAT_DV_LL_RGB422        = 12, // "DolbyVision LL RGB 4:2:2"
	DCP_COLOR_FORMAT_GRGB_BLUE_422       = 13, // "GRGB as YCbCr422 (Even line blue)"
	DCP_COLOR_FORMAT_GRGB_RED_422        = 14, // "GRGB as YCbCr422 (Even line red)"
	DCP_COLOR_FORMAT_COUNT
};

enum dcp_colorimetry {
	DCP_COLORIMETRY_BT601              =  0, // "SMPTE 170M/BT.601"
	DCP_COLORIMETRY_BT709              =  1, // "BT.701"
	DCP_COLORIMETRY_XVYCC_601          =  2, // "xvYCC601"
	DCP_COLORIMETRY_XVYCC_709          =  3, // "xvYCC709"
	DCP_COLORIMETRY_SYCC_601           =  4, // "sYCC601"
	DCP_COLORIMETRY_ADOBE_YCC_601      =  5, // "AdobeYCC601"
	DCP_COLORIMETRY_BT2020_CYCC        =  6, // "BT.2020 (c)"
	DCP_COLORIMETRY_BT2020_YCC         =  7, // "BT.2020 (nc)"
	DCP_COLORIMETRY_VSVDB              =  8, // "DolbyVision VSVDB"
	DCP_COLORIMETRY_BT2020_RGB         =  9, // "BT.2020 (RGB)"
	DCP_COLORIMETRY_SRGB               = 10, // "sRGB"
	DCP_COLORIMETRY_SCRGB              = 11, // "scRGB"
	DCP_COLORIMETRY_SCRGB_FIXED        = 12, // "scRGBfixed"
	DCP_COLORIMETRY_ADOBE_RGB          = 13, // "AdobeRGB"
	DCP_COLORIMETRY_DCI_P3_RGB_D65     = 14, // "DCI-P3 (D65)"
	DCP_COLORIMETRY_DCI_P3_RGB_THEATER = 15, // "DCI-P3 (Theater)"
	DCP_COLORIMETRY_RGB                = 16, // "Default RGB"
	DCP_COLORIMETRY_COUNT
};

enum dcp_color_range {
	DCP_COLOR_YCBCR_RANGE_FULL    = 0,
	DCP_COLOR_YCBCR_RANGE_LIMITED = 1,
	DCP_COLOR_YCBCR_RANGE_COUNT
};

struct dcp_color_mode {
	s64 score;
	u32 id;
	enum dcp_color_eotf eotf;
	enum dcp_color_format format;
	enum dcp_colorimetry colorimetry;
	enum dcp_color_range range;
	u8 depth;
};

/*
 * Represents a single display mode. These mode objects are populated at
 * runtime based on the TimingElements dictionary sent by the DCP.
 */
struct dcp_display_mode {
	struct drm_display_mode mode;
	u32 color_mode_id;
	u32 timing_mode_id;
	struct dcp_color_mode sdr_rgb;
	struct dcp_color_mode sdr_444;
	struct dcp_color_mode sdr;
	struct dcp_color_mode best;
};

struct dimension {
	s64 total, front_porch, sync_width, active;
	s64 precise_sync_rate;
};

int parse(const void *blob, size_t size, struct dcp_parse_ctx *ctx);
struct dcp_display_mode *enumerate_modes(struct dcp_parse_ctx *handle,
					 unsigned int *count, int width_mm,
					 int height_mm, unsigned notch_height);
int parse_display_attributes(struct dcp_parse_ctx *handle, int *width_mm,
			     int *height_mm);
int parse_epic_service_init(struct dcp_parse_ctx *handle, const char **name,
			    const char **class, s64 *unit);

struct dcp_sound_format_mask {
	u64 formats;			/* SNDRV_PCM_FMTBIT_* */
	unsigned int rates;		/* SNDRV_PCM_RATE_* */
	unsigned int nchans;
};

struct dcp_sound_cookie {
	u8 data[24];
};

struct snd_pcm_chmap_elem;
int parse_sound_constraints(struct dcp_parse_ctx *handle,
			    struct dcp_sound_format_mask *sieve,
			    struct dcp_sound_format_mask *hits);
int parse_sound_mode(struct dcp_parse_ctx *handle,
		     struct dcp_sound_format_mask *sieve,
		     struct snd_pcm_chmap_elem *chmap,
		     struct dcp_sound_cookie *cookie);

struct dcp_system_ev_mnits {
	u32 timestamp;
	u32 millinits;
	u32 idac;
};

int parse_system_log_mnits(struct dcp_parse_ctx *handle,
			   struct dcp_system_ev_mnits *entry);

#endif
