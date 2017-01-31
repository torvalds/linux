/*
 *   intel_hdmi_lpe_audio.h - Intel HDMI LPE audio driver
 *
 *  Copyright (C) 2016 Intel Corp
 *  Authors:	Sailaja Bandarupalli <sailaja.bandarupalli@intel.com>
 *		Ramesh Babu K V <ramesh.babu@intel.com>
 *		Vaibhav Agarwal <vaibhav.agarwal@intel.com>
 *		Jerome Anand <jerome.anand@intel.com>
 *		Aravind Siddappaji <aravindx.siddappaji@intel.com>
 *  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */
#ifndef __INTEL_HDMI_LPE_AUDIO_H
#define __INTEL_HDMI_LPE_AUDIO_H

#include <linux/types.h>
#include <sound/initval.h>
#include <linux/version.h>
#include <linux/pm_runtime.h>
#include <linux/platform_device.h>
#include <sound/asoundef.h>
#include <sound/control.h>
#include <sound/pcm.h>

#define AUD_CONFIG_VALID_BIT			(1<<9)
#define AUD_CONFIG_DP_MODE			(1<<15)
#define AUD_CONFIG_BLOCK_BIT			(1<<7)

#define HMDI_LPE_AUDIO_DRIVER_NAME		"intel-hdmi-lpe-audio"
#define HAD_MAX_DEVICES		1
#define HAD_MIN_CHANNEL		2
#define HAD_MAX_CHANNEL		8
#define HAD_NUM_OF_RING_BUFS	4

/* Assume 192KHz, 8channel, 25msec period */
#define HAD_MAX_BUFFER		(600*1024)
#define HAD_MIN_BUFFER		(32*1024)
#define HAD_MAX_PERIODS		4
#define HAD_MIN_PERIODS		4
#define HAD_MAX_PERIOD_BYTES	(HAD_MAX_BUFFER/HAD_MIN_PERIODS)
#define HAD_MIN_PERIOD_BYTES	256
#define HAD_FIFO_SIZE		0 /* fifo not being used */
#define MAX_SPEAKERS		8

#define AUD_SAMPLE_RATE_32	32000
#define AUD_SAMPLE_RATE_44_1	44100
#define AUD_SAMPLE_RATE_48	48000
#define AUD_SAMPLE_RATE_88_2	88200
#define AUD_SAMPLE_RATE_96	96000
#define AUD_SAMPLE_RATE_176_4	176400
#define AUD_SAMPLE_RATE_192	192000

#define HAD_MIN_RATE		AUD_SAMPLE_RATE_32
#define HAD_MAX_RATE		AUD_SAMPLE_RATE_192

#define DIS_SAMPLE_RATE_25_2	25200
#define DIS_SAMPLE_RATE_27	27000
#define DIS_SAMPLE_RATE_54	54000
#define DIS_SAMPLE_RATE_74_25	74250
#define DIS_SAMPLE_RATE_148_5	148500
#define HAD_REG_WIDTH		0x08
#define HAD_MAX_HW_BUFS		0x04
#define HAD_MAX_DIP_WORDS		16
#define INTEL_HAD		"IntelHdmiLpeAudio"

/* DP Link Rates */
#define DP_2_7_GHZ			270000
#define DP_1_62_GHZ			162000

/* Maud Values */
#define AUD_SAMPLE_RATE_32_DP_2_7_MAUD_VAL		1988
#define AUD_SAMPLE_RATE_44_1_DP_2_7_MAUD_VAL		2740
#define AUD_SAMPLE_RATE_48_DP_2_7_MAUD_VAL		2982
#define AUD_SAMPLE_RATE_88_2_DP_2_7_MAUD_VAL		5480
#define AUD_SAMPLE_RATE_96_DP_2_7_MAUD_VAL		5965
#define AUD_SAMPLE_RATE_176_4_DP_2_7_MAUD_VAL		10961
#define HAD_MAX_RATE_DP_2_7_MAUD_VAL			11930
#define AUD_SAMPLE_RATE_32_DP_1_62_MAUD_VAL		3314
#define AUD_SAMPLE_RATE_44_1_DP_1_62_MAUD_VAL		4567
#define AUD_SAMPLE_RATE_48_DP_1_62_MAUD_VAL		4971
#define AUD_SAMPLE_RATE_88_2_DP_1_62_MAUD_VAL		9134
#define AUD_SAMPLE_RATE_96_DP_1_62_MAUD_VAL		9942
#define AUD_SAMPLE_RATE_176_4_DP_1_62_MAUD_VAL		18268
#define HAD_MAX_RATE_DP_1_62_MAUD_VAL			19884

/* Naud Value */
#define DP_NAUD_VAL					32768

/* _AUD_CONFIG register MASK */
#define AUD_CONFIG_MASK_UNDERRUN	0xC0000000
#define AUD_CONFIG_MASK_SRDBG		0x00000002
#define AUD_CONFIG_MASK_FUNCRST		0x00000001

#define MAX_CNT			0xFF
#define HAD_SUSPEND_DELAY	1000

#define OTM_HDMI_ELD_SIZE 128

union otm_hdmi_eld_t {
	unsigned char eld_data[OTM_HDMI_ELD_SIZE];
	struct {
		/* Byte[0] = ELD Version Number */
		union {
			unsigned char   byte0;
			struct {
				unsigned char reserved:3; /* Reserf */
				unsigned char eld_ver:5; /* ELD Version Number */
				/* 00000b - reserved
				 * 00001b - first rev, obsoleted
				 * 00010b - version 2, supporting CEA version
				 *			861D or below
				 * 00011b:11111b - reserved
				 * for future
				 */
			};
		};

		/* Byte[1] = Vendor Version Field */
		union {
			unsigned char vendor_version;
			struct {
				unsigned char reserved1:3;
				unsigned char veld_ver:5; /* Version number of the ELD
						     * extension. This value is
						     * provisioned and unique to
						     * each vendor.
						     */
			};
		};

		/* Byte[2] = Baseline Length field */
		unsigned char baseline_eld_length; /* Length of the Baseline structure
					      *	divided by Four.
					      */

		/* Byte [3] = Reserved for future use */
		unsigned char byte3;

		/* Starting of the BaseLine EELD structure
		 * Byte[4] = Monitor Name Length
		 */
		union {
			unsigned char byte4;
			struct {
				unsigned char mnl:5;
				unsigned char cea_edid_rev_id:3;
			};
		};

		/* Byte[5] = Capabilities */
		union {
			unsigned char capabilities;
			struct {
				unsigned char hdcp:1; /* HDCP support */
				unsigned char ai_support:1;   /* AI support */
				unsigned char connection_type:2; /* Connection type
							    * 00 - HDMI
							    * 01 - DP
							    * 10 -11  Reserved
							    * for future
							    * connection types
							    */
				unsigned char sadc:4; /* Indicates number of 3 bytes
						 * Short Audio Descriptors.
						 */
			};
		};

		/* Byte[6] = Audio Synch Delay */
		unsigned char audio_synch_delay; /* Amount of time reported by the
					    * sink that the video trails audio
					    * in milliseconds.
					    */

		/* Byte[7] = Speaker Allocation Block */
		union {
			unsigned char speaker_allocation_block;
			struct {
				unsigned char flr:1; /*Front Left and Right channels*/
				unsigned char lfe:1; /*Low Frequency Effect channel*/
				unsigned char fc:1;  /*Center transmission channel*/
				unsigned char rlr:1; /*Rear Left and Right channels*/
				unsigned char rc:1; /*Rear Center channel*/
				unsigned char flrc:1; /*Front left and Right of Center
						 *transmission channels
						 */
				unsigned char rlrc:1; /*Rear left and Right of Center
						 *transmission channels
						 */
				unsigned char reserved3:1; /* Reserved */
			};
		};

		/* Byte[8 - 15] - 8 Byte port identification value */
		unsigned char port_id_value[8];

		/* Byte[16 - 17] - 2 Byte Manufacturer ID */
		unsigned char manufacturer_id[2];

		/* Byte[18 - 19] - 2 Byte Product ID */
		unsigned char product_id[2];

		/* Byte [20-83] - 64 Bytes of BaseLine Data */
		unsigned char mn_sand_sads[64]; /* This will include
					   * - ASCII string of Monitor name
					   * - List of 3 byte SADs
					   * - Zero padding
					   */

		/* Vendor ELD Block should continue here!
		 * No Vendor ELD block defined as of now.
		 */
	} __packed;
};

/**
 * enum had_status - Audio stream states
 *
 * @STREAM_INIT: Stream initialized
 * @STREAM_RUNNING: Stream running
 * @STREAM_PAUSED: Stream paused
 * @STREAM_DROPPED: Stream dropped
 */
enum had_stream_status {
	STREAM_INIT = 0,
	STREAM_RUNNING = 1,
	STREAM_PAUSED = 2,
	STREAM_DROPPED = 3
};

/**
 * enum had_status_stream - HAD stream states
 */
enum had_status_stream {
	HAD_INIT = 0,
	HAD_RUNNING_STREAM,
};

enum had_drv_status {
	HAD_DRV_CONNECTED,
	HAD_DRV_RUNNING,
	HAD_DRV_DISCONNECTED,
	HAD_DRV_SUSPENDED,
	HAD_DRV_ERR,
};

/* enum intel_had_aud_buf_type - HDMI controller ring buffer types */
enum intel_had_aud_buf_type {
	HAD_BUF_TYPE_A = 0,
	HAD_BUF_TYPE_B = 1,
	HAD_BUF_TYPE_C = 2,
	HAD_BUF_TYPE_D = 3,
};

enum num_aud_ch {
	CH_STEREO = 0,
	CH_THREE_FOUR = 1,
	CH_FIVE_SIX = 2,
	CH_SEVEN_EIGHT = 3
};

/* HDMI Controller register offsets - audio domain common */
/* Base address for below regs = 0x65000 */
enum hdmi_ctrl_reg_offset_common {
	AUDIO_HDMI_CONFIG_A	= 0x000,
	AUDIO_HDMI_CONFIG_B = 0x800,
	AUDIO_HDMI_CONFIG_C = 0x900,
};
/* HDMI controller register offsets */
enum hdmi_ctrl_reg_offset_v1 {
	AUD_CONFIG		= 0x0,
	AUD_CH_STATUS_0		= 0x08,
	AUD_CH_STATUS_1		= 0x0C,
	AUD_HDMI_CTS		= 0x10,
	AUD_N_ENABLE		= 0x14,
	AUD_SAMPLE_RATE		= 0x18,
	AUD_BUF_CONFIG		= 0x20,
	AUD_BUF_CH_SWAP		= 0x24,
	AUD_BUF_A_ADDR		= 0x40,
	AUD_BUF_A_LENGTH	= 0x44,
	AUD_BUF_B_ADDR		= 0x48,
	AUD_BUF_B_LENGTH	= 0x4c,
	AUD_BUF_C_ADDR		= 0x50,
	AUD_BUF_C_LENGTH	= 0x54,
	AUD_BUF_D_ADDR		= 0x58,
	AUD_BUF_D_LENGTH	= 0x5c,
	AUD_CNTL_ST		= 0x60,
	AUD_HDMI_STATUS		= 0x68,
	AUD_HDMIW_INFOFR	= 0x114,
};

/*
 * Delta changes in HDMI controller register offsets
 * compare to v1 version
 */

enum hdmi_ctrl_reg_offset_v2 {
	AUD_HDMI_STATUS_v2	= 0x64,
	AUD_HDMIW_INFOFR_v2	= 0x68,
};

/*
 *	CEA speaker placement:
 *
 *	FL  FLC   FC   FRC   FR
 *
 *						LFE
 *
 *	RL  RLC   RC   RRC   RR
 *
 *	The Left/Right Surround channel _notions_ LS/RS in SMPTE 320M
 *	corresponds to CEA RL/RR; The SMPTE channel _assignment_ C/LFE is
 *	swapped to CEA LFE/FC.
 */
enum cea_speaker_placement {
	FL  = (1 <<  0),        /* Front Left           */
	FC  = (1 <<  1),        /* Front Center         */
	FR  = (1 <<  2),        /* Front Right          */
	FLC = (1 <<  3),        /* Front Left Center    */
	FRC = (1 <<  4),        /* Front Right Center   */
	RL  = (1 <<  5),        /* Rear Left            */
	RC  = (1 <<  6),        /* Rear Center          */
	RR  = (1 <<  7),        /* Rear Right           */
	RLC = (1 <<  8),        /* Rear Left Center     */
	RRC = (1 <<  9),        /* Rear Right Center    */
	LFE = (1 << 10),        /* Low Frequency Effect */
};

struct cea_channel_speaker_allocation {
	int ca_index;
	int speakers[8];

	/* derived values, just for convenience */
	int channels;
	int spk_mask;
};

struct channel_map_table {
	unsigned char map;              /* ALSA API channel map position */
	unsigned char cea_slot;         /* CEA slot value */
	int spk_mask;                   /* speaker position bit mask */
};

/**
 * union aud_cfg - Audio configuration
 *
 * @cfg_regx: individual register bits
 * @cfg_regval: full register value
 *
 */
union aud_cfg {
	struct {
		u32 aud_en:1;
		u32 layout:1;
		u32 fmt:2;
		u32 num_ch:3;
		u32 set:1;
		u32 flat:1;
		u32 val_bit:1;
		u32 user_bit:1;
		u32 underrun:1;
		u32 packet_mode:1;
		u32 left_align:1;
		u32 bogus_sample:1;
		u32 dp_modei:1;
		u32 rsvd:16;
	} cfg_regx_v2;
	u32 cfg_regval;
};

/**
 * union aud_ch_status_0 - Audio Channel Status 0 Attributes
 *
 * @status_0_regx:individual register bits
 * @status_0_regval:full register value
 *
 */
union aud_ch_status_0 {
	struct {
		u32 ch_status:1;
		u32 lpcm_id:1;
		u32 cp_info:1;
		u32 format:3;
		u32 mode:2;
		u32 ctg_code:8;
		u32 src_num:4;
		u32 ch_num:4;
		u32 samp_freq:4;
		u32 clk_acc:2;
		u32 rsvd:2;
	} status_0_regx;
	u32 status_0_regval;
};

/**
 * union aud_ch_status_1 - Audio Channel Status 1 Attributes
 *
 * @status_1_regx: individual register bits
 * @status_1_regval: full register value
 *
 */
union aud_ch_status_1 {
	struct {
		u32 max_wrd_len:1;
		u32 wrd_len:3;
		u32 rsvd:28;
		} status_1_regx;
	u32 status_1_regval;
};

/**
 * union aud_hdmi_cts - CTS register
 *
 * @cts_regx: individual register bits
 * @cts_regval: full register value
 *
 */
union aud_hdmi_cts {
	struct {
		u32 cts_val:24;
		u32 en_cts_prog:1;
		u32 rsvd:7;
	} cts_regx_v2;
	u32 cts_regval;
};

/**
 * union aud_hdmi_n_enable - N register
 *
 * @n_regx: individual register bits
 * @n_regval: full register value
 *
 */
union aud_hdmi_n_enable {
	struct {
		u32 n_val:24;
		u32 en_n_prog:1;
		u32 rsvd:7;
	} n_regx_v2;
	u32 n_regval;
};

/**
 * union aud_buf_config -  Audio Buffer configurations
 *
 * @buf_cfg_regx: individual register bits
 * @buf_cfgval: full register value
 *
 */
union aud_buf_config {
	struct {
		u32 audio_fifo_watermark:8;
		u32 dma_fifo_watermark:3;
		u32 rsvd0:5;
		u32 aud_delay:8;
		u32 rsvd1:8;
	} buf_cfg_regx_v2;
	u32 buf_cfgval;
};

/**
 * union aud_buf_ch_swap - Audio Sample Swapping offset
 *
 * @buf_ch_swap_regx: individual register bits
 * @buf_ch_swap_val: full register value
 *
 */
union aud_buf_ch_swap {
	struct {
		u32 first_0:3;
		u32 second_0:3;
		u32 first_1:3;
		u32 second_1:3;
		u32 first_2:3;
		u32 second_2:3;
		u32 first_3:3;
		u32 second_3:3;
		u32 rsvd:8;
	} buf_ch_swap_regx;
	u32 buf_ch_swap_val;
};

/**
 * union aud_buf_addr - Address for Audio Buffer
 *
 * @buf_addr_regx: individual register bits
 * @buf_addr_val: full register value
 *
 */
union aud_buf_addr {
	struct {
		u32 valid:1;
		u32 intr_en:1;
		u32 rsvd:4;
		u32 addr:26;
	} buf_addr_regx;
	u32 buf_addr_val;
};

/**
 * union aud_buf_len - Length of Audio Buffer
 *
 * @buf_len_regx: individual register bits
 * @buf_len_val: full register value
 *
 */
union aud_buf_len {
	struct {
		u32 buf_len:20;
		u32 rsvd:12;
	} buf_len_regx;
	u32 buf_len_val;
};

/**
 * union aud_ctrl_st - Audio Control State Register offset
 *
 * @ctrl_regx: individual register bits
 * @ctrl_val: full register value
 *
 */
union aud_ctrl_st {
	struct {
		u32 ram_addr:4;
		u32 eld_ack:1;
		u32 eld_addr:4;
		u32 eld_buf_size:5;
		u32 eld_valid:1;
		u32 cp_ready:1;
		u32 dip_freq:2;
		u32 dip_idx:3;
		u32 dip_en_sta:4;
		u32 rsvd:7;
	} ctrl_regx;
	u32 ctrl_val;
};

/**
 * union aud_info_frame1 - Audio HDMI Widget Data Island Packet offset
 *
 * @fr1_regx: individual register bits
 * @fr1_val: full register value
 *
 */
union aud_info_frame1 {
	struct {
		u32 pkt_type:8;
		u32 ver_num:8;
		u32 len:5;
		u32 rsvd:11;
	} fr1_regx;
	u32 fr1_val;
};

/**
 * union aud_info_frame2 - DIP frame 2
 *
 * @fr2_regx: individual register bits
 * @fr2_val: full register value
 *
 */
union aud_info_frame2 {
	struct {
		u32 chksum:8;
		u32 chnl_cnt:3;
		u32 rsvd0:1;
		u32 coding_type:4;
		u32 smpl_size:2;
		u32 smpl_freq:3;
		u32 rsvd1:3;
		u32 format:8;
	} fr2_regx;
	u32 fr2_val;
};

/**
 * union aud_info_frame3 - DIP frame 3
 *
 * @fr3_regx: individual register bits
 * @fr3_val: full register value
 *
 */
union aud_info_frame3 {
	struct {
		u32 chnl_alloc:8;
		u32 rsvd0:3;
		u32 lsv:4;
		u32 dm_inh:1;
		u32 rsvd1:16;
	} fr3_regx;
	u32 fr3_val;
};

enum hdmi_connector_status {
	hdmi_connector_status_connected = 1,
	hdmi_connector_status_disconnected = 2,
	hdmi_connector_status_unknown = 3,
};

#define HDMI_AUDIO_UNDERRUN     (1UL<<31)
#define HDMI_AUDIO_BUFFER_DONE  (1UL<<29)


#define PORT_ENABLE			(1 << 31)
#define SDVO_AUDIO_ENABLE	(1 << 6)

enum had_caps_list {
	HAD_GET_ELD = 1,
	HAD_GET_DISPLAY_RATE,
	HAD_GET_DP_OUTPUT,
	HAD_GET_LINK_RATE,
	HAD_SET_ENABLE_AUDIO,
	HAD_SET_DISABLE_AUDIO,
	HAD_SET_ENABLE_AUDIO_INT,
	HAD_SET_DISABLE_AUDIO_INT,
};

enum had_event_type {
	HAD_EVENT_HOT_PLUG = 1,
	HAD_EVENT_HOT_UNPLUG,
	HAD_EVENT_MODE_CHANGING,
	HAD_EVENT_AUDIO_BUFFER_DONE,
	HAD_EVENT_AUDIO_BUFFER_UNDERRUN,
	HAD_EVENT_QUERY_IS_AUDIO_BUSY,
	HAD_EVENT_QUERY_IS_AUDIO_SUSPENDED,
};

#endif
