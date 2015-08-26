/*
 * Copyright 2009-2015 Freescale Semiconductor, Inc. All Rights Reserved.
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

/*!
 * @defgroup Framebuffer Framebuffer Driver for SDC and ADC.
 */

/*!
 * @file mxc_edid.h
 *
 * @brief MXC EDID tools
 *
 * @ingroup Framebuffer
 */

#ifndef MXC_EDID_H
#define MXC_EDID_H

#include <linux/fb.h>

#define FB_VMODE_ASPECT_4_3	0x10
#define FB_VMODE_ASPECT_16_9	0x20
#define FB_VMODE_ASPECT_MASK	(FB_VMODE_ASPECT_4_3 | FB_VMODE_ASPECT_16_9)

enum cea_audio_coding_types {
	AUDIO_CODING_TYPE_REF_STREAM_HEADER	=  0,
	AUDIO_CODING_TYPE_LPCM			=  1,
	AUDIO_CODING_TYPE_AC3			=  2,
	AUDIO_CODING_TYPE_MPEG1			=  3,
	AUDIO_CODING_TYPE_MP3			=  4,
	AUDIO_CODING_TYPE_MPEG2			=  5,
	AUDIO_CODING_TYPE_AACLC			=  6,
	AUDIO_CODING_TYPE_DTS			=  7,
	AUDIO_CODING_TYPE_ATRAC			=  8,
	AUDIO_CODING_TYPE_SACD			=  9,
	AUDIO_CODING_TYPE_EAC3			= 10,
	AUDIO_CODING_TYPE_DTS_HD		= 11,
	AUDIO_CODING_TYPE_MLP			= 12,
	AUDIO_CODING_TYPE_DST			= 13,
	AUDIO_CODING_TYPE_WMAPRO		= 14,
	AUDIO_CODING_TYPE_RESERVED		= 15,
};

struct mxc_hdmi_3d_format {
	unsigned char vic_order_2d;
	unsigned char struct_3d;
	unsigned char detail_3d;
	unsigned char reserved;
};

struct mxc_edid_cfg {
	bool cea_underscan;
	bool cea_basicaudio;
	bool cea_ycbcr444;
	bool cea_ycbcr422;
	bool hdmi_cap;

	/*VSD*/
	bool vsd_support_ai;
	bool vsd_dc_48bit;
	bool vsd_dc_36bit;
	bool vsd_dc_30bit;
	bool vsd_dc_y444;
	bool vsd_dvi_dual;

	bool vsd_cnc0;
	bool vsd_cnc1;
	bool vsd_cnc2;
	bool vsd_cnc3;

	u8 vsd_video_latency;
	u8 vsd_audio_latency;
	u8 vsd_I_video_latency;
	u8 vsd_I_audio_latency;

	u8 physical_address[4];
	u8 hdmi_vic[64];
	struct mxc_hdmi_3d_format hdmi_3d_format[64];
	u16 hdmi_3d_mask_all;
	u16 hdmi_3d_struct_all;
	u32 vsd_max_tmdsclk_rate;

	u8 max_channels;
	u8 sample_sizes;
	u8 sample_rates;
	u8 speaker_alloc;
};

int mxc_edid_var_to_vic(struct fb_var_screeninfo *var);
int mxc_edid_mode_to_vic(const struct fb_videomode *mode);
int mxc_edid_read(struct i2c_adapter *adp, unsigned short addr,
	unsigned char *edid, struct mxc_edid_cfg *cfg, struct fb_info *fbi);
int mxc_edid_parse_ext_blk(unsigned char *edid, struct mxc_edid_cfg *cfg,
	struct fb_monspecs *specs);
#endif
