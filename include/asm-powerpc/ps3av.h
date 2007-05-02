/*
 * Copyright (C) 2006 Sony Computer Entertainment Inc.
 * Copyright 2006, 2007 Sony Corporation
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published
 * by the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
#ifndef _ASM_POWERPC_PS3AV_H_
#define _ASM_POWERPC_PS3AV_H_

#include <linux/mutex.h>

/** command for ioctl() **/
#define PS3AV_VERSION 0x205	/* version of ps3av command */

#define PS3AV_CID_AV_INIT              0x00000001
#define PS3AV_CID_AV_FIN               0x00000002
#define PS3AV_CID_AV_GET_HW_CONF       0x00000003
#define PS3AV_CID_AV_GET_MONITOR_INFO  0x00000004
#define PS3AV_CID_AV_ENABLE_EVENT      0x00000006
#define PS3AV_CID_AV_DISABLE_EVENT     0x00000007
#define PS3AV_CID_AV_TV_MUTE           0x0000000a

#define PS3AV_CID_AV_VIDEO_CS          0x00010001
#define PS3AV_CID_AV_VIDEO_MUTE        0x00010002
#define PS3AV_CID_AV_VIDEO_DISABLE_SIG 0x00010003
#define PS3AV_CID_AV_AUDIO_PARAM       0x00020001
#define PS3AV_CID_AV_AUDIO_MUTE        0x00020002
#define PS3AV_CID_AV_HDMI_MODE         0x00040001

#define PS3AV_CID_VIDEO_INIT           0x01000001
#define PS3AV_CID_VIDEO_MODE           0x01000002
#define PS3AV_CID_VIDEO_FORMAT         0x01000004
#define PS3AV_CID_VIDEO_PITCH          0x01000005

#define PS3AV_CID_AUDIO_INIT           0x02000001
#define PS3AV_CID_AUDIO_MODE           0x02000002
#define PS3AV_CID_AUDIO_MUTE           0x02000003
#define PS3AV_CID_AUDIO_ACTIVE         0x02000004
#define PS3AV_CID_AUDIO_INACTIVE       0x02000005
#define PS3AV_CID_AUDIO_SPDIF_BIT      0x02000006
#define PS3AV_CID_AUDIO_CTRL           0x02000007

#define PS3AV_CID_EVENT_UNPLUGGED      0x10000001
#define PS3AV_CID_EVENT_PLUGGED        0x10000002
#define PS3AV_CID_EVENT_HDCP_DONE      0x10000003
#define PS3AV_CID_EVENT_HDCP_FAIL      0x10000004
#define PS3AV_CID_EVENT_HDCP_AUTH      0x10000005
#define PS3AV_CID_EVENT_HDCP_ERROR     0x10000006

#define PS3AV_CID_AVB_PARAM            0x04000001

/* max backend ports */
#define PS3AV_HDMI_MAX                 2	/* HDMI_0 HDMI_1 */
#define PS3AV_AVMULTI_MAX              1	/* AVMULTI_0 */
#define PS3AV_AV_PORT_MAX              (PS3AV_HDMI_MAX + PS3AV_AVMULTI_MAX)
#define PS3AV_OPT_PORT_MAX             1	/* SPDIF0 */
#define PS3AV_HEAD_MAX                 2	/* HEAD_A HEAD_B */

/* num of pkt for PS3AV_CID_AVB_PARAM */
#define PS3AV_AVB_NUM_VIDEO            PS3AV_HEAD_MAX
#define PS3AV_AVB_NUM_AUDIO            0	/* not supported */
#define PS3AV_AVB_NUM_AV_VIDEO         PS3AV_AV_PORT_MAX
#define PS3AV_AVB_NUM_AV_AUDIO         PS3AV_HDMI_MAX

#define PS3AV_MUTE_PORT_MAX            1	/* num of ports in mute pkt */

/* event_bit */
#define PS3AV_CMD_EVENT_BIT_UNPLUGGED			(1 << 0)
#define PS3AV_CMD_EVENT_BIT_PLUGGED			(1 << 1)
#define PS3AV_CMD_EVENT_BIT_HDCP_DONE			(1 << 2)
#define PS3AV_CMD_EVENT_BIT_HDCP_FAIL			(1 << 3)
#define PS3AV_CMD_EVENT_BIT_HDCP_REAUTH			(1 << 4)
#define PS3AV_CMD_EVENT_BIT_HDCP_TOPOLOGY		(1 << 5)

/* common params */
/* mute */
#define PS3AV_CMD_MUTE_OFF				0x0000
#define PS3AV_CMD_MUTE_ON				0x0001
/* avport */
#define PS3AV_CMD_AVPORT_HDMI_0				0x0000
#define PS3AV_CMD_AVPORT_HDMI_1				0x0001
#define PS3AV_CMD_AVPORT_AVMULTI_0			0x0010
#define PS3AV_CMD_AVPORT_SPDIF_0			0x0020
#define PS3AV_CMD_AVPORT_SPDIF_1			0x0021

/* for av backend */
/* av_mclk */
#define PS3AV_CMD_AV_MCLK_128				0x0000
#define PS3AV_CMD_AV_MCLK_256				0x0001
#define PS3AV_CMD_AV_MCLK_512				0x0003
/* av_inputlen */
#define PS3AV_CMD_AV_INPUTLEN_16			0x02
#define PS3AV_CMD_AV_INPUTLEN_20			0x0a
#define PS3AV_CMD_AV_INPUTLEN_24			0x0b
/* alayout */
#define PS3AV_CMD_AV_LAYOUT_32				(1 << 0)
#define PS3AV_CMD_AV_LAYOUT_44				(1 << 1)
#define PS3AV_CMD_AV_LAYOUT_48				(1 << 2)
#define PS3AV_CMD_AV_LAYOUT_88				(1 << 3)
#define PS3AV_CMD_AV_LAYOUT_96				(1 << 4)
#define PS3AV_CMD_AV_LAYOUT_176				(1 << 5)
#define PS3AV_CMD_AV_LAYOUT_192				(1 << 6)
/* hdmi_mode */
#define PS3AV_CMD_AV_HDMI_MODE_NORMAL			0xff
#define PS3AV_CMD_AV_HDMI_HDCP_OFF			0x01
#define PS3AV_CMD_AV_HDMI_EDID_PASS			0x80
#define PS3AV_CMD_AV_HDMI_DVI				0x40

/* for video module */
/* video_head */
#define PS3AV_CMD_VIDEO_HEAD_A				0x0000
#define PS3AV_CMD_VIDEO_HEAD_B				0x0001
/* video_cs_out video_cs_in */
#define PS3AV_CMD_VIDEO_CS_NONE				0x0000
#define PS3AV_CMD_VIDEO_CS_RGB_8			0x0001
#define PS3AV_CMD_VIDEO_CS_YUV444_8			0x0002
#define PS3AV_CMD_VIDEO_CS_YUV422_8			0x0003
#define PS3AV_CMD_VIDEO_CS_XVYCC_8			0x0004
#define PS3AV_CMD_VIDEO_CS_RGB_10			0x0005
#define PS3AV_CMD_VIDEO_CS_YUV444_10			0x0006
#define PS3AV_CMD_VIDEO_CS_YUV422_10			0x0007
#define PS3AV_CMD_VIDEO_CS_XVYCC_10			0x0008
#define PS3AV_CMD_VIDEO_CS_RGB_12			0x0009
#define PS3AV_CMD_VIDEO_CS_YUV444_12			0x000a
#define PS3AV_CMD_VIDEO_CS_YUV422_12			0x000b
#define PS3AV_CMD_VIDEO_CS_XVYCC_12			0x000c
/* video_vid */
#define PS3AV_CMD_VIDEO_VID_NONE			0x0000
#define PS3AV_CMD_VIDEO_VID_480I			0x0001
#define PS3AV_CMD_VIDEO_VID_576I			0x0003
#define PS3AV_CMD_VIDEO_VID_480P			0x0005
#define PS3AV_CMD_VIDEO_VID_576P			0x0006
#define PS3AV_CMD_VIDEO_VID_1080I_60HZ			0x0007
#define PS3AV_CMD_VIDEO_VID_1080I_50HZ			0x0008
#define PS3AV_CMD_VIDEO_VID_720P_60HZ			0x0009
#define PS3AV_CMD_VIDEO_VID_720P_50HZ			0x000a
#define PS3AV_CMD_VIDEO_VID_1080P_60HZ			0x000b
#define PS3AV_CMD_VIDEO_VID_1080P_50HZ			0x000c
#define PS3AV_CMD_VIDEO_VID_WXGA			0x000d
#define PS3AV_CMD_VIDEO_VID_SXGA			0x000e
#define PS3AV_CMD_VIDEO_VID_WUXGA			0x000f
#define PS3AV_CMD_VIDEO_VID_480I_A			0x0010
/* video_format */
#define PS3AV_CMD_VIDEO_FORMAT_BLACK			0x0000
#define PS3AV_CMD_VIDEO_FORMAT_ARGB_8BIT		0x0007
/* video_order */
#define PS3AV_CMD_VIDEO_ORDER_RGB			0x0000
#define PS3AV_CMD_VIDEO_ORDER_BGR			0x0001
/* video_fmt */
#define PS3AV_CMD_VIDEO_FMT_X8R8G8B8			0x0000
/* video_out_format */
#define PS3AV_CMD_VIDEO_OUT_FORMAT_RGB_12BIT		0x0000
/* video_sync */
#define PS3AV_CMD_VIDEO_SYNC_VSYNC			0x0001
#define PS3AV_CMD_VIDEO_SYNC_CSYNC			0x0004
#define PS3AV_CMD_VIDEO_SYNC_HSYNC			0x0010

/* for audio module */
/* num_of_ch */
#define PS3AV_CMD_AUDIO_NUM_OF_CH_2			0x0000
#define PS3AV_CMD_AUDIO_NUM_OF_CH_3			0x0001
#define PS3AV_CMD_AUDIO_NUM_OF_CH_4			0x0002
#define PS3AV_CMD_AUDIO_NUM_OF_CH_5			0x0003
#define PS3AV_CMD_AUDIO_NUM_OF_CH_6			0x0004
#define PS3AV_CMD_AUDIO_NUM_OF_CH_7			0x0005
#define PS3AV_CMD_AUDIO_NUM_OF_CH_8			0x0006
/* audio_fs */
#define PS3AV_CMD_AUDIO_FS_32K				0x0001
#define PS3AV_CMD_AUDIO_FS_44K				0x0002
#define PS3AV_CMD_AUDIO_FS_48K				0x0003
#define PS3AV_CMD_AUDIO_FS_88K				0x0004
#define PS3AV_CMD_AUDIO_FS_96K				0x0005
#define PS3AV_CMD_AUDIO_FS_176K				0x0006
#define PS3AV_CMD_AUDIO_FS_192K				0x0007
/* audio_word_bits */
#define PS3AV_CMD_AUDIO_WORD_BITS_16			0x0001
#define PS3AV_CMD_AUDIO_WORD_BITS_20			0x0002
#define PS3AV_CMD_AUDIO_WORD_BITS_24			0x0003
/* audio_format */
#define PS3AV_CMD_AUDIO_FORMAT_PCM			0x0001
#define PS3AV_CMD_AUDIO_FORMAT_BITSTREAM		0x00ff
/* audio_source */
#define PS3AV_CMD_AUDIO_SOURCE_SERIAL			0x0000
#define PS3AV_CMD_AUDIO_SOURCE_SPDIF			0x0001
/* audio_swap */
#define PS3AV_CMD_AUDIO_SWAP_0				0x0000
#define PS3AV_CMD_AUDIO_SWAP_1				0x0000
/* audio_map */
#define PS3AV_CMD_AUDIO_MAP_OUTPUT_0			0x0000
#define PS3AV_CMD_AUDIO_MAP_OUTPUT_1			0x0001
#define PS3AV_CMD_AUDIO_MAP_OUTPUT_2			0x0002
#define PS3AV_CMD_AUDIO_MAP_OUTPUT_3			0x0003
/* audio_layout */
#define PS3AV_CMD_AUDIO_LAYOUT_2CH			0x0000
#define PS3AV_CMD_AUDIO_LAYOUT_6CH			0x000b	/* LREClr */
#define PS3AV_CMD_AUDIO_LAYOUT_8CH			0x001f	/* LREClrXY */
/* audio_downmix */
#define PS3AV_CMD_AUDIO_DOWNMIX_PERMITTED		0x0000
#define PS3AV_CMD_AUDIO_DOWNMIX_PROHIBITED		0x0001

/* audio_port */
#define PS3AV_CMD_AUDIO_PORT_HDMI_0			( 1 << 0 )
#define PS3AV_CMD_AUDIO_PORT_HDMI_1			( 1 << 1 )
#define PS3AV_CMD_AUDIO_PORT_AVMULTI_0			( 1 << 10 )
#define PS3AV_CMD_AUDIO_PORT_SPDIF_0			( 1 << 20 )
#define PS3AV_CMD_AUDIO_PORT_SPDIF_1			( 1 << 21 )

/* audio_ctrl_id */
#define PS3AV_CMD_AUDIO_CTRL_ID_DAC_RESET		0x0000
#define PS3AV_CMD_AUDIO_CTRL_ID_DAC_DE_EMPHASIS		0x0001
#define PS3AV_CMD_AUDIO_CTRL_ID_AVCLK			0x0002
/* audio_ctrl_data[0] reset */
#define PS3AV_CMD_AUDIO_CTRL_RESET_NEGATE		0x0000
#define PS3AV_CMD_AUDIO_CTRL_RESET_ASSERT		0x0001
/* audio_ctrl_data[0] de-emphasis */
#define PS3AV_CMD_AUDIO_CTRL_DE_EMPHASIS_OFF		0x0000
#define PS3AV_CMD_AUDIO_CTRL_DE_EMPHASIS_ON		0x0001
/* audio_ctrl_data[0] avclk */
#define PS3AV_CMD_AUDIO_CTRL_AVCLK_22			0x0000
#define PS3AV_CMD_AUDIO_CTRL_AVCLK_18			0x0001

/* av_vid */
/* do not use these params directly, use vid_video2av */
#define PS3AV_CMD_AV_VID_480I				0x0000
#define PS3AV_CMD_AV_VID_480P				0x0001
#define PS3AV_CMD_AV_VID_720P_60HZ			0x0002
#define PS3AV_CMD_AV_VID_1080I_60HZ			0x0003
#define PS3AV_CMD_AV_VID_1080P_60HZ			0x0004
#define PS3AV_CMD_AV_VID_576I				0x0005
#define PS3AV_CMD_AV_VID_576P				0x0006
#define PS3AV_CMD_AV_VID_720P_50HZ			0x0007
#define PS3AV_CMD_AV_VID_1080I_50HZ			0x0008
#define PS3AV_CMD_AV_VID_1080P_50HZ			0x0009
#define PS3AV_CMD_AV_VID_WXGA				0x000a
#define PS3AV_CMD_AV_VID_SXGA				0x000b
#define PS3AV_CMD_AV_VID_WUXGA				0x000c
/* av_cs_out av_cs_in */
/* use cs_video2av() */
#define PS3AV_CMD_AV_CS_RGB_8				0x0000
#define PS3AV_CMD_AV_CS_YUV444_8			0x0001
#define PS3AV_CMD_AV_CS_YUV422_8			0x0002
#define PS3AV_CMD_AV_CS_XVYCC_8				0x0003
#define PS3AV_CMD_AV_CS_RGB_10				0x0004
#define PS3AV_CMD_AV_CS_YUV444_10			0x0005
#define PS3AV_CMD_AV_CS_YUV422_10			0x0006
#define PS3AV_CMD_AV_CS_XVYCC_10			0x0007
#define PS3AV_CMD_AV_CS_RGB_12				0x0008
#define PS3AV_CMD_AV_CS_YUV444_12			0x0009
#define PS3AV_CMD_AV_CS_YUV422_12			0x000a
#define PS3AV_CMD_AV_CS_XVYCC_12			0x000b
#define PS3AV_CMD_AV_CS_8				0x0000
#define PS3AV_CMD_AV_CS_10				0x0001
#define PS3AV_CMD_AV_CS_12				0x0002
/* dither */
#define PS3AV_CMD_AV_DITHER_OFF				0x0000
#define PS3AV_CMD_AV_DITHER_ON				0x0001
#define PS3AV_CMD_AV_DITHER_8BIT			0x0000
#define PS3AV_CMD_AV_DITHER_10BIT			0x0002
#define PS3AV_CMD_AV_DITHER_12BIT			0x0004
/* super_white */
#define PS3AV_CMD_AV_SUPER_WHITE_OFF			0x0000
#define PS3AV_CMD_AV_SUPER_WHITE_ON			0x0001
/* aspect */
#define PS3AV_CMD_AV_ASPECT_16_9			0x0000
#define PS3AV_CMD_AV_ASPECT_4_3				0x0001
/* video_cs_cnv() */
#define PS3AV_CMD_VIDEO_CS_RGB				0x0001
#define PS3AV_CMD_VIDEO_CS_YUV422			0x0002
#define PS3AV_CMD_VIDEO_CS_YUV444			0x0003

/* for automode */
#define PS3AV_RESBIT_720x480P			0x0003	/* 0x0001 | 0x0002 */
#define PS3AV_RESBIT_720x576P			0x0003	/* 0x0001 | 0x0002 */
#define PS3AV_RESBIT_1280x720P			0x0004
#define PS3AV_RESBIT_1920x1080I			0x0008
#define PS3AV_RESBIT_1920x1080P			0x4000
#define PS3AV_RES_MASK_60			(PS3AV_RESBIT_720x480P \
						| PS3AV_RESBIT_1280x720P \
						| PS3AV_RESBIT_1920x1080I \
						| PS3AV_RESBIT_1920x1080P)
#define PS3AV_RES_MASK_50			(PS3AV_RESBIT_720x576P \
						| PS3AV_RESBIT_1280x720P \
						| PS3AV_RESBIT_1920x1080I \
						| PS3AV_RESBIT_1920x1080P)

#define PS3AV_MONITOR_TYPE_HDMI			1	/* HDMI */
#define PS3AV_MONITOR_TYPE_DVI			2	/* DVI */
#define PS3AV_DEFAULT_HDMI_VID_REG_60		PS3AV_CMD_VIDEO_VID_480P
#define PS3AV_DEFAULT_AVMULTI_VID_REG_60	PS3AV_CMD_VIDEO_VID_480I
#define PS3AV_DEFAULT_HDMI_VID_REG_50		PS3AV_CMD_VIDEO_VID_576P
#define PS3AV_DEFAULT_AVMULTI_VID_REG_50	PS3AV_CMD_VIDEO_VID_576I
#define PS3AV_DEFAULT_DVI_VID			PS3AV_CMD_VIDEO_VID_480P

#define PS3AV_REGION_60				0x01
#define PS3AV_REGION_50				0x02
#define PS3AV_REGION_RGB			0x10

#define get_status(buf)				(((__u32 *)buf)[2])
#define PS3AV_HDR_SIZE				4	/* version + size */

/* for video mode */
#define PS3AV_MODE_MASK				0x000F
#define PS3AV_MODE_HDCP_OFF			0x1000	/* Retail PS3 product doesn't support this */
#define PS3AV_MODE_DITHER			0x0800
#define PS3AV_MODE_FULL				0x0080
#define PS3AV_MODE_DVI				0x0040
#define PS3AV_MODE_RGB				0x0020


/** command packet structure **/
struct ps3av_send_hdr {
	u16 version;
	u16 size;		/* size of command packet */
	u32 cid;		/* command id */
};

struct ps3av_reply_hdr {
	u16 version;
	u16 size;
	u32 cid;
	u32 status;
};

/* backend: initialization */
struct ps3av_pkt_av_init {
	struct ps3av_send_hdr send_hdr;
	u32 event_bit;
};

/* backend: finalize */
struct ps3av_pkt_av_fin {
	struct ps3av_send_hdr send_hdr;
	/* recv */
	u32 reserved;
};

/* backend: get port */
struct ps3av_pkt_av_get_hw_conf {
	struct ps3av_send_hdr send_hdr;
	/* recv */
	u32 status;
	u16 num_of_hdmi;	/* out: number of hdmi */
	u16 num_of_avmulti;	/* out: number of avmulti */
	u16 num_of_spdif;	/* out: number of hdmi */
	u16 reserved;
};

/* backend: get monitor info */
struct ps3av_info_resolution {
	u32 res_bits;
	u32 native;
};

struct ps3av_info_cs {
	u8 rgb;
	u8 yuv444;
	u8 yuv422;
	u8 reserved;
};

struct ps3av_info_color {
	u16 red_x;
	u16 red_y;
	u16 green_x;
	u16 green_y;
	u16 blue_x;
	u16 blue_y;
	u16 white_x;
	u16 white_y;
	u32 gamma;
};

struct ps3av_info_audio {
	u8 type;
	u8 max_num_of_ch;
	u8 fs;
	u8 sbit;
};

struct ps3av_info_monitor {
	u8 avport;
	u8 monitor_id[10];
	u8 monitor_type;
	u8 monitor_name[16];
	struct ps3av_info_resolution res_60;
	struct ps3av_info_resolution res_50;
	struct ps3av_info_resolution res_other;
	struct ps3av_info_resolution res_vesa;
	struct ps3av_info_cs cs;
	struct ps3av_info_color color;
	u8 supported_ai;
	u8 speaker_info;
	u8 num_of_audio_block;
	struct ps3av_info_audio audio[0];	/* 0 or more audio blocks */
	u8 reserved[169];
} __attribute__ ((packed));

struct ps3av_pkt_av_get_monitor_info {
	struct ps3av_send_hdr send_hdr;
	u16 avport;		/* in: avport */
	u16 reserved;
	/* recv */
	struct ps3av_info_monitor info;	/* out: monitor info */
};

/* backend: enable/disable event */
struct ps3av_pkt_av_event {
	struct ps3av_send_hdr send_hdr;
	u32 event_bit;		/* in */
};

/* backend: video cs param */
struct ps3av_pkt_av_video_cs {
	struct ps3av_send_hdr send_hdr;
	u16 avport;		/* in: avport */
	u16 av_vid;		/* in: video resolution */
	u16 av_cs_out;		/* in: output color space */
	u16 av_cs_in;		/* in: input color space */
	u8 dither;		/* in: dither bit length */
	u8 bitlen_out;		/* in: bit length */
	u8 super_white;		/* in: super white */
	u8 aspect;		/* in: aspect ratio */
};

/* backend: video mute */
struct ps3av_av_mute {
	u16 avport;		/* in: avport */
	u16 mute;		/* in: mute on/off */
};

struct ps3av_pkt_av_video_mute {
	struct ps3av_send_hdr send_hdr;
	struct ps3av_av_mute mute[PS3AV_MUTE_PORT_MAX];
};

/* backend: video disable signal */
struct ps3av_pkt_av_video_disable_sig {
	struct ps3av_send_hdr send_hdr;
	u16 avport;		/* in: avport */
	u16 reserved;
};

/* backend: audio param */
struct ps3av_audio_info_frame {
	struct pb1_bit {
		u8 ct:4;
		u8 rsv:1;
		u8 cc:3;
	} pb1;
	struct pb2_bit {
		u8 rsv:3;
		u8 sf:3;
		u8 ss:2;
	} pb2;
	u8 pb3;
	u8 pb4;
	struct pb5_bit {
		u8 dm:1;
		u8 lsv:4;
		u8 rsv:3;
	} pb5;
};

struct ps3av_pkt_av_audio_param {
	struct ps3av_send_hdr send_hdr;
	u16 avport;		/* in: avport */
	u16 reserved;
	u8 mclk;		/* in: audio mclk */
	u8 ns[3];		/* in: audio ns val */
	u8 enable;		/* in: audio enable */
	u8 swaplr;		/* in: audio swap */
	u8 fifomap;		/* in: audio fifomap */
	u8 inputctrl;		/* in: audio input ctrl */
	u8 inputlen;		/* in: sample bit size */
	u8 layout;		/* in: speaker layout param */
	struct ps3av_audio_info_frame info;	/* in: info */
	u8 chstat[5];		/* in: ch stat */
};

/* backend: audio_mute */
struct ps3av_pkt_av_audio_mute {
	struct ps3av_send_hdr send_hdr;
	struct ps3av_av_mute mute[PS3AV_MUTE_PORT_MAX];
};

/* backend: hdmi_mode */
struct ps3av_pkt_av_hdmi_mode {
	struct ps3av_send_hdr send_hdr;
	u8 mode;		/* in: hdmi_mode */
	u8 reserved0;
	u8 reserved1;
	u8 reserved2;
};

/* backend: tv_mute */
struct ps3av_pkt_av_tv_mute {
	struct ps3av_send_hdr send_hdr;
	u16 avport;		/* in: avport HDMI only */
	u16 mute;		/* in: mute */
};

/* video: initialize */
struct ps3av_pkt_video_init {
	struct ps3av_send_hdr send_hdr;
	/* recv */
	u32 reserved;
};

/* video: mode setting */
struct ps3av_pkt_video_mode {
	struct ps3av_send_hdr send_hdr;
	u32 video_head;		/* in: head */
	u32 reserved;
	u32 video_vid;		/* in: video resolution */
	u16 reserved1;
	u16 width;		/* in: width in pixel */
	u16 reserved2;
	u16 height;		/* in: height in pixel */
	u32 pitch;		/* in: line size in byte */
	u32 video_out_format;	/* in: out format */
	u32 video_format;	/* in: input frame buffer format */
	u8 reserved3;
	u8 reserved4;
	u16 video_order;	/* in: input RGB order */
	u32 reserved5;
};

/* video: format */
struct ps3av_pkt_video_format {
	struct ps3av_send_hdr send_hdr;
	u32 video_head;		/* in: head */
	u32 video_format;	/* in: frame buffer format */
	u16 reserved;
	u16 video_order;	/* in: input RGB order */
};

/* video: pitch */
struct ps3av_pkt_video_pitch {
	u16 version;
	u16 size;		/* size of command packet */
	u32 cid;		/* command id */
	u32 video_head;		/* in: head */
	u32 pitch;		/* in: line size in byte */
};

/* audio: initialize */
struct ps3av_pkt_audio_init {
	struct ps3av_send_hdr send_hdr;
	/* recv */
	u32 reserved;
};

/* audio: mode setting */
struct ps3av_pkt_audio_mode {
	struct ps3av_send_hdr send_hdr;
	u8 avport;		/* in: avport */
	u8 reserved0[3];
	u32 mask;		/* in: mask */
	u32 audio_num_of_ch;	/* in: number of ch */
	u32 audio_fs;		/* in: sampling freq */
	u32 audio_word_bits;	/* in: sample bit size */
	u32 audio_format;	/* in: audio output format */
	u32 audio_source;	/* in: audio source */
	u8 audio_enable[4];	/* in: audio enable */
	u8 audio_swap[4];	/* in: audio swap */
	u8 audio_map[4];	/* in: audio map */
	u32 audio_layout;	/* in: speaker layout */
	u32 audio_downmix;	/* in: audio downmix permission */
	u32 audio_downmix_level;
	u8 audio_cs_info[8];	/* in: IEC channel status */
};

/* audio: mute */
struct ps3av_audio_mute {
	u8 avport;		/* in: opt_port optical */
	u8 reserved[3];
	u32 mute;		/* in: mute */
};

struct ps3av_pkt_audio_mute {
	struct ps3av_send_hdr send_hdr;
	struct ps3av_audio_mute mute[PS3AV_OPT_PORT_MAX];
};

/* audio: active/inactive */
struct ps3av_pkt_audio_active {
	struct ps3av_send_hdr send_hdr;
	u32 audio_port;		/* in: audio active/inactive port */
};

/* audio: SPDIF user bit */
struct ps3av_pkt_audio_spdif_bit {
	u16 version;
	u16 size;		/* size of command packet */
	u32 cid;		/* command id */
	u8 avport;		/* in: avport SPDIF only */
	u8 reserved[3];
	u32 audio_port;		/* in: SPDIF only */
	u32 spdif_bit_data[12];	/* in: user bit data */
};

/* audio: audio control */
struct ps3av_pkt_audio_ctrl {
	u16 version;
	u16 size;		/* size of command packet */
	u32 cid;		/* command id */
	u32 audio_ctrl_id;	/* in: control id */
	u32 audio_ctrl_data[4];	/* in: control data */
};

/* avb:param */
#define PS3AV_PKT_AVB_PARAM_MAX_BUF_SIZE	\
	(PS3AV_AVB_NUM_VIDEO*sizeof(struct ps3av_pkt_video_mode) + \
	 PS3AV_AVB_NUM_AUDIO*sizeof(struct ps3av_pkt_audio_mode) + \
	 PS3AV_AVB_NUM_AV_VIDEO*sizeof(struct ps3av_pkt_av_video_cs) + \
	 PS3AV_AVB_NUM_AV_AUDIO*sizeof(struct ps3av_pkt_av_audio_param))

struct ps3av_pkt_avb_param {
	struct ps3av_send_hdr send_hdr;
	u16 num_of_video_pkt;
	u16 num_of_audio_pkt;
	u16 num_of_av_video_pkt;
	u16 num_of_av_audio_pkt;
	/*
	 * The actual buffer layout depends on the fields above:
	 *
	 * struct ps3av_pkt_video_mode video[num_of_video_pkt];
	 * struct ps3av_pkt_audio_mode audio[num_of_audio_pkt];
	 * struct ps3av_pkt_av_video_cs av_video[num_of_av_video_pkt];
	 * struct ps3av_pkt_av_audio_param av_audio[num_of_av_audio_pkt];
	 */
	u8 buf[PS3AV_PKT_AVB_PARAM_MAX_BUF_SIZE];
};

struct ps3av {
	int available;
	struct semaphore sem;
	struct work_struct work;
	struct completion done;
	struct workqueue_struct *wq;
	struct mutex mutex;
	int open_count;
	struct ps3_vuart_port_device *dev;

	int region;
	struct ps3av_pkt_av_get_hw_conf av_hw_conf;
	u32 av_port[PS3AV_AV_PORT_MAX + PS3AV_OPT_PORT_MAX];
	u32 opt_port[PS3AV_OPT_PORT_MAX];
	u32 head[PS3AV_HEAD_MAX];
	u32 audio_port;
	int ps3av_mode;
	int ps3av_mode_old;
};

/** command status **/
#define PS3AV_STATUS_SUCCESS			0x0000	/* success */
#define PS3AV_STATUS_RECEIVE_VUART_ERROR	0x0001	/* receive vuart error */
#define PS3AV_STATUS_SYSCON_COMMUNICATE_FAIL	0x0002	/* syscon communication error */
#define PS3AV_STATUS_INVALID_COMMAND		0x0003	/* obsolete invalid CID */
#define PS3AV_STATUS_INVALID_PORT		0x0004	/* invalid port number */
#define PS3AV_STATUS_INVALID_VID		0x0005	/* invalid video format */
#define PS3AV_STATUS_INVALID_COLOR_SPACE	0x0006	/* invalid video colose space */
#define PS3AV_STATUS_INVALID_FS			0x0007	/* invalid audio sampling freq */
#define PS3AV_STATUS_INVALID_AUDIO_CH		0x0008	/* invalid audio channel number */
#define PS3AV_STATUS_UNSUPPORTED_VERSION	0x0009	/* version mismatch  */
#define PS3AV_STATUS_INVALID_SAMPLE_SIZE	0x000a	/* invalid audio sample bit size */
#define PS3AV_STATUS_FAILURE			0x000b	/* other failures */
#define PS3AV_STATUS_UNSUPPORTED_COMMAND	0x000c	/* unsupported cid */
#define PS3AV_STATUS_BUFFER_OVERFLOW		0x000d	/* write buffer overflow */
#define PS3AV_STATUS_INVALID_VIDEO_PARAM	0x000e	/* invalid video param */
#define PS3AV_STATUS_NO_SEL			0x000f	/* not exist selector */
#define PS3AV_STATUS_INVALID_AV_PARAM		0x0010	/* invalid backend param */
#define PS3AV_STATUS_INVALID_AUDIO_PARAM	0x0011	/* invalid audio param */
#define PS3AV_STATUS_UNSUPPORTED_HDMI_MODE	0x0012	/* unsupported hdmi mode */
#define PS3AV_STATUS_NO_SYNC_HEAD		0x0013	/* sync head failed */

extern void ps3av_set_hdr(u32, u16, struct ps3av_send_hdr *);
extern int ps3av_do_pkt(u32, u16, size_t, struct ps3av_send_hdr *);

extern int ps3av_cmd_init(void);
extern int ps3av_cmd_fin(void);
extern int ps3av_cmd_av_video_mute(int, u32 *, u32);
extern int ps3av_cmd_av_video_disable_sig(u32);
extern int ps3av_cmd_av_tv_mute(u32, u32);
extern int ps3av_cmd_enable_event(void);
extern int ps3av_cmd_av_hdmi_mode(u8);
extern u32 ps3av_cmd_set_av_video_cs(void *, u32, int, int, int, u32);
extern u32 ps3av_cmd_set_video_mode(void *, u32, int, int, u32);
extern int ps3av_cmd_video_format_black(u32, u32, u32);
extern int ps3av_cmd_av_audio_mute(int, u32 *, u32);
extern u32 ps3av_cmd_set_av_audio_param(void *, u32,
					const struct ps3av_pkt_audio_mode *,
					u32);
extern void ps3av_cmd_set_audio_mode(struct ps3av_pkt_audio_mode *, u32, u32,
				     u32, u32, u32, u32);
extern int ps3av_cmd_audio_mode(struct ps3av_pkt_audio_mode *);
extern int ps3av_cmd_audio_mute(int, u32 *, u32);
extern int ps3av_cmd_audio_active(int, u32);
extern int ps3av_cmd_avb_param(struct ps3av_pkt_avb_param *, u32);
extern int ps3av_cmd_av_get_hw_conf(struct ps3av_pkt_av_get_hw_conf *);
#ifdef PS3AV_DEBUG
extern void ps3av_cmd_av_hw_conf_dump(const struct ps3av_pkt_av_get_hw_conf *);
extern void ps3av_cmd_av_monitor_info_dump(const struct ps3av_pkt_av_get_monitor_info *);
#else
static inline void ps3av_cmd_av_hw_conf_dump(const struct ps3av_pkt_av_get_hw_conf *hw_conf) {}
static inline void ps3av_cmd_av_monitor_info_dump(const struct ps3av_pkt_av_get_monitor_info *monitor_info) {}
#endif
extern int ps3av_cmd_video_get_monitor_info(struct ps3av_pkt_av_get_monitor_info *,
					    u32);

extern int ps3av_vuart_write(struct ps3_vuart_port_device *dev,
			     const void *buf, unsigned long size);
extern int ps3av_vuart_read(struct ps3_vuart_port_device *dev, void *buf,
			    unsigned long size, int timeout);

extern int ps3av_set_video_mode(u32, int);
extern int ps3av_set_audio_mode(u32, u32, u32, u32, u32);
extern int ps3av_set_mode(u32, int);
extern int ps3av_get_mode(void);
extern int ps3av_get_scanmode(int);
extern int ps3av_get_refresh_rate(int);
extern int ps3av_video_mode2res(u32, u32 *, u32 *);
extern int ps3av_video_mute(int);
extern int ps3av_audio_mute(int);
extern int ps3av_dev_open(void);
extern int ps3av_dev_close(void);

#endif	/* _ASM_POWERPC_PS3AV_H_ */
