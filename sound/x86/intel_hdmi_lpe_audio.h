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

/* enum intel_had_aud_buf_type - HDMI controller ring buffer types */
enum intel_had_aud_buf_type {
	HAD_BUF_TYPE_A = 0,
	HAD_BUF_TYPE_B = 1,
	HAD_BUF_TYPE_C = 2,
	HAD_BUF_TYPE_D = 3,
};

/* HDMI Controller register offsets - audio domain common */
/* Base address for below regs = 0x65000 */
enum hdmi_ctrl_reg_offset_common {
	AUDIO_HDMI_CONFIG_A = 0x000,
	AUDIO_HDMI_CONFIG_B = 0x800,
	AUDIO_HDMI_CONFIG_C = 0x900,
};
/* HDMI controller register offsets */
enum hdmi_ctrl_reg_offset {
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
	AUD_HDMI_STATUS		= 0x64, /* v2 */
	AUD_HDMIW_INFOFR	= 0x68, /* v2 */
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

/* Audio configuration */
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
	} regx;
	u32 regval;
};

#define AUD_CONFIG_BLOCK_BIT			(1 << 7)
#define AUD_CONFIG_VALID_BIT			(1 << 9)
#define AUD_CONFIG_DP_MODE			(1 << 15)

/* Audio Channel Status 0 Attributes */
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
	} regx;
	u32 regval;
};

/* Audio Channel Status 1 Attributes */
union aud_ch_status_1 {
	struct {
		u32 max_wrd_len:1;
		u32 wrd_len:3;
		u32 rsvd:28;
	} regx;
	u32 regval;
};

/* CTS register */
union aud_hdmi_cts {
	struct {
		u32 cts_val:24;
		u32 en_cts_prog:1;
		u32 rsvd:7;
	} regx;
	u32 regval;
};

/* N register */
union aud_hdmi_n_enable {
	struct {
		u32 n_val:24;
		u32 en_n_prog:1;
		u32 rsvd:7;
	} regx;
	u32 regval;
};

/* Audio Buffer configurations */
union aud_buf_config {
	struct {
		u32 audio_fifo_watermark:8;
		u32 dma_fifo_watermark:3;
		u32 rsvd0:5;
		u32 aud_delay:8;
		u32 rsvd1:8;
	} regx;
	u32 regval;
};

/* Audio Sample Swapping offset */
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
	} regx;
	u32 regval;
};

/* Address for Audio Buffer */
union aud_buf_addr {
	struct {
		u32 valid:1;
		u32 intr_en:1;
		u32 rsvd:4;
		u32 addr:26;
	} regx;
	u32 regval;
};

/* Length of Audio Buffer */
union aud_buf_len {
	struct {
		u32 buf_len:20;
		u32 rsvd:12;
	} regx;
	u32 regval;
};

/* Audio Control State Register offset */
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
	} regx;
	u32 regval;
};

/* Audio HDMI Widget Data Island Packet offset */
union aud_info_frame1 {
	struct {
		u32 pkt_type:8;
		u32 ver_num:8;
		u32 len:5;
		u32 rsvd:11;
	} regx;
	u32 regval;
};

/* DIP frame 2 */
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
	} regx;
	u32 regval;
};

/* DIP frame 3 */
union aud_info_frame3 {
	struct {
		u32 chnl_alloc:8;
		u32 rsvd0:3;
		u32 lsv:4;
		u32 dm_inh:1;
		u32 rsvd1:16;
	} regx;
	u32 regval;
};

/* AUD_HDMI_STATUS bits */
#define HDMI_AUDIO_UNDERRUN		(1U << 31)
#define HDMI_AUDIO_BUFFER_DONE		(1U << 29)

/* AUD_HDMI_STATUS register mask */
#define AUD_CONFIG_MASK_UNDERRUN	0xC0000000
#define AUD_CONFIG_MASK_SRDBG		0x00000002
#define AUD_CONFIG_MASK_FUNCRST		0x00000001

#endif
