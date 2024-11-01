/* SPDX-License-Identifier: GPL-2.0-only */
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
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */
#ifndef __INTEL_HDMI_LPE_AUDIO_H
#define __INTEL_HDMI_LPE_AUDIO_H

#define HAD_MIN_CHANNEL		2
#define HAD_MAX_CHANNEL		8
#define HAD_NUM_OF_RING_BUFS	4

/* max 20bit address, aligned to 64 */
#define HAD_MAX_BUFFER		((1024 * 1024 - 1) & ~0x3f)
#define HAD_DEFAULT_BUFFER	(600 * 1024) /* default prealloc size */
#define HAD_MAX_PERIODS		256	/* arbitrary, but should suffice */
#define HAD_MIN_PERIODS		1
#define HAD_MAX_PERIOD_BYTES	((HAD_MAX_BUFFER / HAD_MIN_PERIODS) & ~0x3f)
#define HAD_MIN_PERIOD_BYTES	1024	/* might be smaller */
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
#define HAD_MAX_DIP_WORDS		16

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

/* Audio configuration */
union aud_cfg {
	struct {
		u32 aud_en:1;
		u32 layout:1;		/* LAYOUT[01], see below */
		u32 fmt:2;
		u32 num_ch:3;
		u32 set:1;
		u32 flat:1;
		u32 val_bit:1;
		u32 user_bit:1;
		u32 underrun:1;		/* 0: send null packets,
					 * 1: send silence stream
					 */
		u32 packet_mode:1;	/* 0: 32bit container, 1: 16bit */
		u32 left_align:1;	/* 0: MSB bits 0-23, 1: bits 8-31 */
		u32 bogus_sample:1;	/* bogus sample for odd channels */
		u32 dp_modei:1;		/* 0: HDMI, 1: DP */
		u32 rsvd:16;
	} regx;
	u32 regval;
};

#define AUD_CONFIG_VALID_BIT			(1 << 9)
#define AUD_CONFIG_DP_MODE			(1 << 15)
#define AUD_CONFIG_CH_MASK	0x70
#define LAYOUT0			0		/* interleaved stereo */
#define LAYOUT1			1		/* for channels > 2 */

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
		u32 samp_freq:4;	/* CH_STATUS_MAP_XXX */
		u32 clk_acc:2;
		u32 rsvd:2;
	} regx;
	u32 regval;
};

/* samp_freq values - Sampling rate as per IEC60958 Ver 3 */
#define CH_STATUS_MAP_32KHZ	0x3
#define CH_STATUS_MAP_44KHZ	0x0
#define CH_STATUS_MAP_48KHZ	0x2
#define CH_STATUS_MAP_88KHZ	0x8
#define CH_STATUS_MAP_96KHZ	0xA
#define CH_STATUS_MAP_176KHZ	0xC
#define CH_STATUS_MAP_192KHZ	0xE

/* Audio Channel Status 1 Attributes */
union aud_ch_status_1 {
	struct {
		u32 max_wrd_len:1;
		u32 wrd_len:3;
		u32 rsvd:28;
	} regx;
	u32 regval;
};

#define MAX_SMPL_WIDTH_20	0x0
#define MAX_SMPL_WIDTH_24	0x1
#define SMPL_WIDTH_16BITS	0x1
#define SMPL_WIDTH_24BITS	0x5

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

#define FIFO_THRESHOLD		0xFE
#define DMA_FIFO_THRESHOLD	0x7

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

#define SWAP_LFE_CENTER		0x00fac4c8	/* octal 76543210 */

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

#define AUD_BUF_VALID		(1U << 0)
#define AUD_BUF_INTR_EN		(1U << 1)

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

#define HDMI_INFO_FRAME_WORD1	0x000a0184
#define DP_INFO_FRAME_WORD1	0x00441b84

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

#define VALID_DIP_WORDS		3

/* AUD_HDMI_STATUS bits */
#define HDMI_AUDIO_UNDERRUN		(1U << 31)
#define HDMI_AUDIO_BUFFER_DONE		(1U << 29)

/* AUD_HDMI_STATUS register mask */
#define AUD_HDMI_STATUS_MASK_UNDERRUN	0xC0000000
#define AUD_HDMI_STATUS_MASK_SRDBG	0x00000002
#define AUD_HDMI_STATUSG_MASK_FUNCRST	0x00000001

#endif
