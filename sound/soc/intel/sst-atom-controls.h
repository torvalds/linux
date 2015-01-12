/*
 *  sst-atom-controls.h - Intel MID Platform driver header file
 *
 *  Copyright (C) 2013-14 Intel Corp
 *  Author: Ramesh Babu <ramesh.babu.koul@intel.com>
 *  	Omair M Abdullah <omair.m.abdullah@intel.com>
 *  	Samreen Nilofer <samreen.nilofer@intel.com>
 *  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
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
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 */

#ifndef __SST_ATOM_CONTROLS_H__
#define __SST_ATOM_CONTROLS_H__

#include <sound/soc.h>
#include <sound/tlv.h>

enum {
	MERR_DPCM_AUDIO = 0,
	MERR_DPCM_COMPR,
};

/* define a bit for each mixer input */
#define SST_MIX_IP(x)		(x)

#define SST_IP_CODEC0		SST_MIX_IP(2)
#define SST_IP_CODEC1		SST_MIX_IP(3)
#define SST_IP_LOOP0		SST_MIX_IP(4)
#define SST_IP_LOOP1		SST_MIX_IP(5)
#define SST_IP_LOOP2		SST_MIX_IP(6)
#define SST_IP_PROBE		SST_MIX_IP(7)
#define SST_IP_VOIP		SST_MIX_IP(12)
#define SST_IP_PCM0		SST_MIX_IP(13)
#define SST_IP_PCM1		SST_MIX_IP(14)
#define SST_IP_MEDIA0		SST_MIX_IP(17)
#define SST_IP_MEDIA1		SST_MIX_IP(18)
#define SST_IP_MEDIA2		SST_MIX_IP(19)
#define SST_IP_MEDIA3		SST_MIX_IP(20)

#define SST_IP_LAST		SST_IP_MEDIA3

#define SST_SWM_INPUT_COUNT	(SST_IP_LAST + 1)
#define SST_CMD_SWM_MAX_INPUTS	6

#define SST_PATH_ID_SHIFT	8
#define SST_DEFAULT_LOCATION_ID	0xFFFF
#define SST_DEFAULT_CELL_NBR	0xFF
#define SST_DEFAULT_MODULE_ID	0xFFFF

/*
 * Audio DSP Path Ids. Specified by the audio DSP FW
 */
enum sst_path_index {
	SST_PATH_INDEX_CODEC_OUT0               = (0x02 << SST_PATH_ID_SHIFT),
	SST_PATH_INDEX_CODEC_OUT1               = (0x03 << SST_PATH_ID_SHIFT),

	SST_PATH_INDEX_SPROT_LOOP_OUT           = (0x04 << SST_PATH_ID_SHIFT),
	SST_PATH_INDEX_MEDIA_LOOP1_OUT          = (0x05 << SST_PATH_ID_SHIFT),
	SST_PATH_INDEX_MEDIA_LOOP2_OUT          = (0x06 << SST_PATH_ID_SHIFT),

	SST_PATH_INDEX_VOIP_OUT                 = (0x0C << SST_PATH_ID_SHIFT),
	SST_PATH_INDEX_PCM0_OUT                 = (0x0D << SST_PATH_ID_SHIFT),
	SST_PATH_INDEX_PCM1_OUT                 = (0x0E << SST_PATH_ID_SHIFT),
	SST_PATH_INDEX_PCM2_OUT                 = (0x0F << SST_PATH_ID_SHIFT),

	SST_PATH_INDEX_MEDIA0_OUT               = (0x12 << SST_PATH_ID_SHIFT),
	SST_PATH_INDEX_MEDIA1_OUT               = (0x13 << SST_PATH_ID_SHIFT),


	/* Start of input paths */
	SST_PATH_INDEX_CODEC_IN0                = (0x82 << SST_PATH_ID_SHIFT),
	SST_PATH_INDEX_CODEC_IN1                = (0x83 << SST_PATH_ID_SHIFT),

	SST_PATH_INDEX_SPROT_LOOP_IN            = (0x84 << SST_PATH_ID_SHIFT),
	SST_PATH_INDEX_MEDIA_LOOP1_IN           = (0x85 << SST_PATH_ID_SHIFT),
	SST_PATH_INDEX_MEDIA_LOOP2_IN           = (0x86 << SST_PATH_ID_SHIFT),

	SST_PATH_INDEX_VOIP_IN                  = (0x8C << SST_PATH_ID_SHIFT),

	SST_PATH_INDEX_PCM0_IN                  = (0x8D << SST_PATH_ID_SHIFT),
	SST_PATH_INDEX_PCM1_IN                  = (0x8E << SST_PATH_ID_SHIFT),

	SST_PATH_INDEX_MEDIA0_IN                = (0x8F << SST_PATH_ID_SHIFT),
	SST_PATH_INDEX_MEDIA1_IN                = (0x90 << SST_PATH_ID_SHIFT),
	SST_PATH_INDEX_MEDIA2_IN                = (0x91 << SST_PATH_ID_SHIFT),

	SST_PATH_INDEX_MEDIA3_IN		= (0x9C << SST_PATH_ID_SHIFT),

	SST_PATH_INDEX_RESERVED                 = (0xFF << SST_PATH_ID_SHIFT),
};

/*
 * path IDs
 */
enum sst_swm_inputs {
	SST_SWM_IN_CODEC0	= (SST_PATH_INDEX_CODEC_IN0	  | SST_DEFAULT_CELL_NBR),
	SST_SWM_IN_CODEC1	= (SST_PATH_INDEX_CODEC_IN1	  | SST_DEFAULT_CELL_NBR),
	SST_SWM_IN_SPROT_LOOP	= (SST_PATH_INDEX_SPROT_LOOP_IN	  | SST_DEFAULT_CELL_NBR),
	SST_SWM_IN_MEDIA_LOOP1	= (SST_PATH_INDEX_MEDIA_LOOP1_IN  | SST_DEFAULT_CELL_NBR),
	SST_SWM_IN_MEDIA_LOOP2	= (SST_PATH_INDEX_MEDIA_LOOP2_IN  | SST_DEFAULT_CELL_NBR),
	SST_SWM_IN_VOIP		= (SST_PATH_INDEX_VOIP_IN	  | SST_DEFAULT_CELL_NBR),
	SST_SWM_IN_PCM0		= (SST_PATH_INDEX_PCM0_IN	  | SST_DEFAULT_CELL_NBR),
	SST_SWM_IN_PCM1		= (SST_PATH_INDEX_PCM1_IN	  | SST_DEFAULT_CELL_NBR),
	SST_SWM_IN_MEDIA0	= (SST_PATH_INDEX_MEDIA0_IN	  | SST_DEFAULT_CELL_NBR), /* Part of Media Mixer */
	SST_SWM_IN_MEDIA1	= (SST_PATH_INDEX_MEDIA1_IN	  | SST_DEFAULT_CELL_NBR), /* Part of Media Mixer */
	SST_SWM_IN_MEDIA2	= (SST_PATH_INDEX_MEDIA2_IN	  | SST_DEFAULT_CELL_NBR), /* Part of Media Mixer */
	SST_SWM_IN_MEDIA3	= (SST_PATH_INDEX_MEDIA3_IN	  | SST_DEFAULT_CELL_NBR), /* Part of Media Mixer */
	SST_SWM_IN_END		= (SST_PATH_INDEX_RESERVED	  | SST_DEFAULT_CELL_NBR)
};

/*
 * path IDs
 */
enum sst_swm_outputs {
	SST_SWM_OUT_CODEC0	= (SST_PATH_INDEX_CODEC_OUT0	  | SST_DEFAULT_CELL_NBR),
	SST_SWM_OUT_CODEC1	= (SST_PATH_INDEX_CODEC_OUT1	  | SST_DEFAULT_CELL_NBR),
	SST_SWM_OUT_SPROT_LOOP	= (SST_PATH_INDEX_SPROT_LOOP_OUT  | SST_DEFAULT_CELL_NBR),
	SST_SWM_OUT_MEDIA_LOOP1	= (SST_PATH_INDEX_MEDIA_LOOP1_OUT | SST_DEFAULT_CELL_NBR),
	SST_SWM_OUT_MEDIA_LOOP2	= (SST_PATH_INDEX_MEDIA_LOOP2_OUT | SST_DEFAULT_CELL_NBR),
	SST_SWM_OUT_VOIP	= (SST_PATH_INDEX_VOIP_OUT	  | SST_DEFAULT_CELL_NBR),
	SST_SWM_OUT_PCM0	= (SST_PATH_INDEX_PCM0_OUT	  | SST_DEFAULT_CELL_NBR),
	SST_SWM_OUT_PCM1	= (SST_PATH_INDEX_PCM1_OUT	  | SST_DEFAULT_CELL_NBR),
	SST_SWM_OUT_PCM2	= (SST_PATH_INDEX_PCM2_OUT	  | SST_DEFAULT_CELL_NBR),
	SST_SWM_OUT_MEDIA0	= (SST_PATH_INDEX_MEDIA0_OUT	  | SST_DEFAULT_CELL_NBR), /* Part of Media Mixer */
	SST_SWM_OUT_MEDIA1	= (SST_PATH_INDEX_MEDIA1_OUT	  | SST_DEFAULT_CELL_NBR), /* Part of Media Mixer */
	SST_SWM_OUT_END		= (SST_PATH_INDEX_RESERVED	  | SST_DEFAULT_CELL_NBR),
};

enum sst_ipc_msg {
	SST_IPC_IA_CMD = 1,
	SST_IPC_IA_SET_PARAMS,
	SST_IPC_IA_GET_PARAMS,
};

enum sst_cmd_type {
	SST_CMD_BYTES_SET = 1,
	SST_CMD_BYTES_GET = 2,
};

enum sst_task {
	SST_TASK_SBA = 1,
	SST_TASK_MMX,
};

enum sst_type {
	SST_TYPE_CMD = 1,
	SST_TYPE_PARAMS,
};

enum sst_flag {
	SST_FLAG_BLOCKED = 1,
	SST_FLAG_NONBLOCK,
};

/*
 * Enumeration for indexing the gain cells in VB_SET_GAIN DSP command
 */
enum sst_gain_index {
	/* GAIN IDs for SB task start here */
	SST_GAIN_INDEX_CODEC_OUT0,
	SST_GAIN_INDEX_CODEC_OUT1,
	SST_GAIN_INDEX_CODEC_IN0,
	SST_GAIN_INDEX_CODEC_IN1,

	SST_GAIN_INDEX_SPROT_LOOP_OUT,
	SST_GAIN_INDEX_MEDIA_LOOP1_OUT,
	SST_GAIN_INDEX_MEDIA_LOOP2_OUT,

	SST_GAIN_INDEX_PCM0_IN_LEFT,
	SST_GAIN_INDEX_PCM0_IN_RIGHT,

	SST_GAIN_INDEX_PCM1_OUT_LEFT,
	SST_GAIN_INDEX_PCM1_OUT_RIGHT,
	SST_GAIN_INDEX_PCM1_IN_LEFT,
	SST_GAIN_INDEX_PCM1_IN_RIGHT,
	SST_GAIN_INDEX_PCM2_OUT_LEFT,

	SST_GAIN_INDEX_PCM2_OUT_RIGHT,
	SST_GAIN_INDEX_VOIP_OUT,
	SST_GAIN_INDEX_VOIP_IN,

	/* Gain IDs for MMX task start here */
	SST_GAIN_INDEX_MEDIA0_IN_LEFT,
	SST_GAIN_INDEX_MEDIA0_IN_RIGHT,
	SST_GAIN_INDEX_MEDIA1_IN_LEFT,
	SST_GAIN_INDEX_MEDIA1_IN_RIGHT,

	SST_GAIN_INDEX_MEDIA2_IN_LEFT,
	SST_GAIN_INDEX_MEDIA2_IN_RIGHT,

	SST_GAIN_INDEX_GAIN_END
};

/*
 * Audio DSP module IDs specified by FW spec
 * TODO: Update with all modules
 */
enum sst_module_id {
	SST_MODULE_ID_PCM		  = 0x0001,
	SST_MODULE_ID_MP3		  = 0x0002,
	SST_MODULE_ID_MP24		  = 0x0003,
	SST_MODULE_ID_AAC		  = 0x0004,
	SST_MODULE_ID_AACP		  = 0x0005,
	SST_MODULE_ID_EAACP		  = 0x0006,
	SST_MODULE_ID_WMA9		  = 0x0007,
	SST_MODULE_ID_WMA10		  = 0x0008,
	SST_MODULE_ID_WMA10P		  = 0x0009,
	SST_MODULE_ID_RA		  = 0x000A,
	SST_MODULE_ID_DDAC3		  = 0x000B,
	SST_MODULE_ID_TRUE_HD		  = 0x000C,
	SST_MODULE_ID_HD_PLUS		  = 0x000D,

	SST_MODULE_ID_SRC		  = 0x0064,
	SST_MODULE_ID_DOWNMIX		  = 0x0066,
	SST_MODULE_ID_GAIN_CELL		  = 0x0067,
	SST_MODULE_ID_SPROT		  = 0x006D,
	SST_MODULE_ID_BASS_BOOST	  = 0x006E,
	SST_MODULE_ID_STEREO_WDNG	  = 0x006F,
	SST_MODULE_ID_AV_REMOVAL	  = 0x0070,
	SST_MODULE_ID_MIC_EQ		  = 0x0071,
	SST_MODULE_ID_SPL		  = 0x0072,
	SST_MODULE_ID_ALGO_VTSV           = 0x0073,
	SST_MODULE_ID_NR		  = 0x0076,
	SST_MODULE_ID_BWX		  = 0x0077,
	SST_MODULE_ID_DRP		  = 0x0078,
	SST_MODULE_ID_MDRP		  = 0x0079,

	SST_MODULE_ID_ANA		  = 0x007A,
	SST_MODULE_ID_AEC		  = 0x007B,
	SST_MODULE_ID_NR_SNS		  = 0x007C,
	SST_MODULE_ID_SER		  = 0x007D,
	SST_MODULE_ID_AGC		  = 0x007E,

	SST_MODULE_ID_CNI		  = 0x007F,
	SST_MODULE_ID_CONTEXT_ALGO_AWARE  = 0x0080,
	SST_MODULE_ID_FIR_24		  = 0x0081,
	SST_MODULE_ID_IIR_24		  = 0x0082,

	SST_MODULE_ID_ASRC		  = 0x0083,
	SST_MODULE_ID_TONE_GEN		  = 0x0084,
	SST_MODULE_ID_BMF		  = 0x0086,
	SST_MODULE_ID_EDL		  = 0x0087,
	SST_MODULE_ID_GLC		  = 0x0088,

	SST_MODULE_ID_FIR_16		  = 0x0089,
	SST_MODULE_ID_IIR_16		  = 0x008A,
	SST_MODULE_ID_DNR		  = 0x008B,

	SST_MODULE_ID_VIRTUALIZER	  = 0x008C,
	SST_MODULE_ID_VISUALIZATION	  = 0x008D,
	SST_MODULE_ID_LOUDNESS_OPTIMIZER  = 0x008E,
	SST_MODULE_ID_REVERBERATION	  = 0x008F,

	SST_MODULE_ID_CNI_TX		  = 0x0090,
	SST_MODULE_ID_REF_LINE		  = 0x0091,
	SST_MODULE_ID_VOLUME		  = 0x0092,
	SST_MODULE_ID_FILT_DCR		  = 0x0094,
	SST_MODULE_ID_SLV		  = 0x009A,
	SST_MODULE_ID_NLF		  = 0x009B,
	SST_MODULE_ID_TNR		  = 0x009C,
	SST_MODULE_ID_WNR		  = 0x009D,

	SST_MODULE_ID_LOG		  = 0xFF00,

	SST_MODULE_ID_TASK		  = 0xFFFF,
};

enum sst_cmd {
	SBA_IDLE		= 14,
	SBA_VB_SET_SPEECH_PATH	= 26,
	MMX_SET_GAIN		= 33,
	SBA_VB_SET_GAIN		= 33,
	FBA_VB_RX_CNI		= 35,
	MMX_SET_GAIN_TIMECONST	= 36,
	SBA_VB_SET_TIMECONST	= 36,
	SBA_VB_START		= 85,
	SBA_SET_SWM		= 114,
	SBA_SET_MDRP            = 116,
	SBA_HW_SET_SSP		= 117,
	SBA_SET_MEDIA_LOOP_MAP	= 118,
	SBA_SET_MEDIA_PATH	= 119,
	MMX_SET_MEDIA_PATH	= 119,
	SBA_VB_LPRO             = 126,
	SBA_VB_SET_FIR          = 128,
	SBA_VB_SET_IIR          = 129,
	SBA_SET_SSP_SLOT_MAP	= 130,
};

enum sst_dsp_switch {
	SST_SWITCH_OFF = 0,
	SST_SWITCH_ON = 3,
};

enum sst_path_switch {
	SST_PATH_OFF = 0,
	SST_PATH_ON = 1,
};

enum sst_swm_state {
	SST_SWM_OFF = 0,
	SST_SWM_ON = 3,
};

#define SST_FILL_LOCATION_IDS(dst, cell_idx, pipe_id)		do {	\
		dst.location_id.p.cell_nbr_idx = (cell_idx);		\
		dst.location_id.p.path_id = (pipe_id);			\
	} while (0)
#define SST_FILL_LOCATION_ID(dst, loc_id)				(\
	dst.location_id.f = (loc_id))
#define SST_FILL_MODULE_ID(dst, mod_id)					(\
	dst.module_id = (mod_id))

#define SST_FILL_DESTINATION1(dst, id)				do {	\
		SST_FILL_LOCATION_ID(dst, (id) & 0xFFFF);		\
		SST_FILL_MODULE_ID(dst, ((id) & 0xFFFF0000) >> 16);	\
	} while (0)
#define SST_FILL_DESTINATION2(dst, loc_id, mod_id)		do {	\
		SST_FILL_LOCATION_ID(dst, loc_id);			\
		SST_FILL_MODULE_ID(dst, mod_id);			\
	} while (0)
#define SST_FILL_DESTINATION3(dst, cell_idx, path_id, mod_id)	do {	\
		SST_FILL_LOCATION_IDS(dst, cell_idx, path_id);		\
		SST_FILL_MODULE_ID(dst, mod_id);			\
	} while (0)

#define SST_FILL_DESTINATION(level, dst, ...)				\
	SST_FILL_DESTINATION##level(dst, __VA_ARGS__)
#define SST_FILL_DEFAULT_DESTINATION(dst)				\
	SST_FILL_DESTINATION(2, dst, SST_DEFAULT_LOCATION_ID, SST_DEFAULT_MODULE_ID)

struct sst_destination_id {
	union sst_location_id {
		struct {
			u8 cell_nbr_idx;	/* module index */
			u8 path_id;		/* pipe_id */
		} __packed	p;		/* part */
		u16		f;		/* full */
	} __packed location_id;
	u16	   module_id;
} __packed;
struct sst_dsp_header {
	struct sst_destination_id dst;
	u16 command_id;
	u16 length;
} __packed;

/*
 *
 * Common Commands
 *
 */
struct sst_cmd_generic {
	struct sst_dsp_header header;
} __packed;

struct swm_input_ids {
	struct sst_destination_id input_id;
} __packed;

struct sst_cmd_set_swm {
	struct sst_dsp_header header;
	struct sst_destination_id output_id;
	u16    switch_state;
	u16    nb_inputs;
	struct swm_input_ids input[SST_CMD_SWM_MAX_INPUTS];
} __packed;

struct sst_cmd_set_media_path {
	struct sst_dsp_header header;
	u16    switch_state;
} __packed;

struct pcm_cfg {
		u8 s_length:2;
		u8 rate:3;
		u8 format:3;
} __packed;

struct sst_cmd_set_speech_path {
	struct sst_dsp_header header;
	u16    switch_state;
	struct {
		u16 rsvd:8;
		struct pcm_cfg cfg;
	} config;
} __packed;

struct gain_cell {
	struct sst_destination_id dest;
	s16 cell_gain_left;
	s16 cell_gain_right;
	u16 gain_time_constant;
} __packed;

#define NUM_GAIN_CELLS 1
struct sst_cmd_set_gain_dual {
	struct sst_dsp_header header;
	u16    gain_cell_num;
	struct gain_cell cell_gains[NUM_GAIN_CELLS];
} __packed;
struct sst_cmd_set_params {
	struct sst_destination_id dst;
	u16 command_id;
	char params[0];
} __packed;


struct sst_cmd_sba_vb_start {
	struct sst_dsp_header header;
} __packed;

union sba_media_loop_params {
	struct {
		u16 rsvd:8;
		struct pcm_cfg cfg;
	} part;
	u16 full;
} __packed;

struct sst_cmd_sba_set_media_loop_map {
	struct	sst_dsp_header header;
	u16	switch_state;
	union	sba_media_loop_params param;
	u16	map;
} __packed;

struct sst_cmd_tone_stop {
	struct	sst_dsp_header header;
	u16	switch_state;
} __packed;

enum sst_ssp_mode {
	SSP_MODE_MASTER = 0,
	SSP_MODE_SLAVE = 1,
};

enum sst_ssp_pcm_mode {
	SSP_PCM_MODE_NORMAL = 0,
	SSP_PCM_MODE_NETWORK = 1,
};

enum sst_ssp_duplex {
	SSP_DUPLEX = 0,
	SSP_RX = 1,
	SSP_TX = 2,
};

enum sst_ssp_fs_frequency {
	SSP_FS_8_KHZ = 0,
	SSP_FS_16_KHZ = 1,
	SSP_FS_44_1_KHZ = 2,
	SSP_FS_48_KHZ = 3,
};

enum sst_ssp_fs_polarity {
	SSP_FS_ACTIVE_LOW = 0,
	SSP_FS_ACTIVE_HIGH = 1,
};

enum sst_ssp_protocol {
	SSP_MODE_PCM = 0,
	SSP_MODE_I2S = 1,
};

enum sst_ssp_port_id {
	SSP_MODEM = 0,
	SSP_BT = 1,
	SSP_FM = 2,
	SSP_CODEC = 3,
};

struct sst_cmd_sba_hw_set_ssp {
	struct sst_dsp_header header;
	u16 selection;			/* 0:SSP0(def), 1:SSP1, 2:SSP2 */

	u16 switch_state;

	u16 nb_bits_per_slots:6;        /* 0-32 bits, 24 (def) */
	u16 nb_slots:4;			/* 0-8: slots per frame  */
	u16 mode:3;			/* 0:Master, 1: Slave  */
	u16 duplex:3;

	u16 active_tx_slot_map:8;       /* Bit map, 0:off, 1:on */
	u16 reserved1:8;

	u16 active_rx_slot_map:8;       /* Bit map 0: Off, 1:On */
	u16 reserved2:8;

	u16 frame_sync_frequency;

	u16 frame_sync_polarity:8;
	u16 data_polarity:8;

	u16 frame_sync_width;           /* 1 to N clocks */
	u16 ssp_protocol:8;
	u16 start_delay:8;		/* Start delay in terms of clock ticks */
} __packed;

#define SST_MAX_TDM_SLOTS 8

struct sst_param_sba_ssp_slot_map {
	struct sst_dsp_header header;

	u16 param_id;
	u16 param_len;
	u16 ssp_index;

	u8 rx_slot_map[SST_MAX_TDM_SLOTS];
	u8 tx_slot_map[SST_MAX_TDM_SLOTS];
} __packed;

enum {
	SST_PROBE_EXTRACTOR = 0,
	SST_PROBE_INJECTOR = 1,
};

/**** widget defines *****/

#define SST_MODULE_GAIN 1
#define SST_MODULE_ALGO 2

#define SST_FMT_MONO 0
#define SST_FMT_STEREO 3

/* physical SSP numbers */
enum {
	SST_SSP0 = 0,
	SST_SSP1,
	SST_SSP2,
	SST_SSP_LAST = SST_SSP2,
};

#define SST_NUM_SSPS		(SST_SSP_LAST + 1)	/* physical SSPs */
#define SST_MAX_SSP_MUX		2			/* single SSP muxed between pipes */
#define SST_MAX_SSP_DOMAINS	2			/* domains present in each pipe */

struct sst_module {
	struct snd_kcontrol *kctl;
	struct list_head node;
};

struct sst_ssp_config {
	u8 ssp_id;
	u8 bits_per_slot;
	u8 slots;
	u8 ssp_mode;
	u8 pcm_mode;
	u8 duplex;
	u8 ssp_protocol;
	u8 fs_frequency;
	u8 active_slot_map;
	u8 start_delay;
	u16 fs_width;
};

struct sst_ssp_cfg {
	const u8 ssp_number;
	const int *mux_shift;
	const int (*domain_shift)[SST_MAX_SSP_MUX];
	const struct sst_ssp_config (*ssp_config)[SST_MAX_SSP_MUX][SST_MAX_SSP_DOMAINS];
};

struct sst_ids {
	u16 location_id;
	u16 module_id;
	u8  task_id;
	u8  format;
	u8  reg;
	const char *parent_wname;
	struct snd_soc_dapm_widget *parent_w;
	struct list_head algo_list;
	struct list_head gain_list;
	const struct sst_pcm_format *pcm_fmt;
};


#define SST_AIF_IN(wname, wevent)							\
{	.id = snd_soc_dapm_aif_in, .name = wname, .sname = NULL,			\
	.reg = SND_SOC_NOPM, .shift = 0,					\
	.on_val = 1, .off_val = 0,							\
	.event = wevent, .event_flags = SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD,	\
	.priv = (void *)&(struct sst_ids) { .task_id = 0, .location_id = 0 }		\
}

#define SST_AIF_OUT(wname, wevent)							\
{	.id = snd_soc_dapm_aif_out, .name = wname, .sname = NULL,			\
	.reg = SND_SOC_NOPM, .shift = 0,						\
	.on_val = 1, .off_val = 0,							\
	.event = wevent, .event_flags = SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD,	\
	.priv = (void *)&(struct sst_ids) { .task_id = 0, .location_id = 0 }		\
}

#define SST_INPUT(wname, wevent)							\
{	.id = snd_soc_dapm_input, .name = wname, .sname = NULL,				\
	.reg = SND_SOC_NOPM, .shift = 0,						\
	.on_val = 1, .off_val = 0,							\
	.event = wevent, .event_flags = SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD,	\
	.priv = (void *)&(struct sst_ids) { .task_id = 0, .location_id = 0 }		\
}

#define SST_OUTPUT(wname, wevent)							\
{	.id = snd_soc_dapm_output, .name = wname, .sname = NULL,			\
	.reg = SND_SOC_NOPM, .shift = 0,						\
	.on_val = 1, .off_val = 0,							\
	.event = wevent, .event_flags = SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD,	\
	.priv = (void *)&(struct sst_ids) { .task_id = 0, .location_id = 0 }		\
}

#define SST_DAPM_OUTPUT(wname, wloc_id, wtask_id, wformat, wevent)                      \
{	.id = snd_soc_dapm_output, .name = wname, .sname = NULL,                        \
	.reg = SND_SOC_NOPM, .shift = 0,						\
	.on_val = 1, .off_val = 0,							\
	.event = wevent, .event_flags = SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD,   \
	.priv = (void *)&(struct sst_ids) { .location_id = wloc_id, .task_id = wtask_id,\
						.pcm_fmt = wformat, }			\
}

#define SST_PATH(wname, wtask, wloc_id, wevent, wflags)					\
{	.id = snd_soc_dapm_pga, .name = wname, .reg = SND_SOC_NOPM, .shift = 0,		\
	.kcontrol_news = NULL, .num_kcontrols = 0,				\
	.on_val = 1, .off_val = 0,							\
	.event = wevent, .event_flags = wflags,						\
	.priv = (void *)&(struct sst_ids) { .task_id = wtask, .location_id = wloc_id, }	\
}

#define SST_LINKED_PATH(wname, wtask, wloc_id, linked_wname, wevent, wflags)		\
{	.id = snd_soc_dapm_pga, .name = wname, .reg = SND_SOC_NOPM, .shift = 0,		\
	.kcontrol_news = NULL, .num_kcontrols = 0,				\
	.on_val = 1, .off_val = 0,							\
	.event = wevent, .event_flags = wflags,						\
	.priv = (void *)&(struct sst_ids) { .task_id = wtask, .location_id = wloc_id,	\
					.parent_wname = linked_wname}			\
}

#define SST_PATH_MEDIA_LOOP(wname, wtask, wloc_id, wformat, wevent, wflags)             \
{	.id = snd_soc_dapm_pga, .name = wname, .reg = SND_SOC_NOPM, .shift = 0,         \
	.kcontrol_news = NULL, .num_kcontrols = 0,                         \
	.event = wevent, .event_flags = wflags,                                         \
	.priv = (void *)&(struct sst_ids) { .task_id = wtask, .location_id = wloc_id,	\
					    .format = wformat,}				\
}

/* output is triggered before input */
#define SST_PATH_INPUT(name, task_id, loc_id, event)					\
	SST_PATH(name, task_id, loc_id, event, SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD)

#define SST_PATH_LINKED_INPUT(name, task_id, loc_id, linked_wname, event)		\
	SST_LINKED_PATH(name, task_id, loc_id, linked_wname, event,			\
					SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD)

#define SST_PATH_OUTPUT(name, task_id, loc_id, event)					\
	SST_PATH(name, task_id, loc_id, event, SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD)

#define SST_PATH_LINKED_OUTPUT(name, task_id, loc_id, linked_wname, event)		\
	SST_LINKED_PATH(name, task_id, loc_id, linked_wname, event,			\
					SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD)

#define SST_PATH_MEDIA_LOOP_OUTPUT(name, task_id, loc_id, format, event)		\
	SST_PATH_MEDIA_LOOP(name, task_id, loc_id, format, event, SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD)


#define SST_SWM_MIXER(wname, wreg, wtask, wloc_id, wcontrols, wevent)			\
{	.id = snd_soc_dapm_mixer, .name = wname, .reg = SND_SOC_NOPM, .shift = 0,	\
	.kcontrol_news = wcontrols, .num_kcontrols = ARRAY_SIZE(wcontrols),\
	.event = wevent, .event_flags = SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD |	\
					SND_SOC_DAPM_POST_REG,				\
	.priv = (void *)&(struct sst_ids) { .task_id = wtask, .location_id = wloc_id,	\
					    .reg = wreg }				\
}

enum sst_gain_kcontrol_type {
	SST_GAIN_TLV,
	SST_GAIN_MUTE,
	SST_GAIN_RAMP_DURATION,
};

struct sst_gain_mixer_control {
	bool stereo;
	enum sst_gain_kcontrol_type type;
	struct sst_gain_value *gain_val;
	int max;
	int min;
	u16 instance_id;
	u16 module_id;
	u16 pipe_id;
	u16 task_id;
	char pname[44];
	struct snd_soc_dapm_widget *w;
};

struct sst_gain_value {
	u16 ramp_duration;
	s16 l_gain;
	s16 r_gain;
	bool mute;
};
#define SST_GAIN_VOLUME_DEFAULT		(-1440)
#define SST_GAIN_RAMP_DURATION_DEFAULT	5 /* timeconstant */
#define SST_GAIN_MUTE_DEFAULT		true

#define SST_GAIN_KCONTROL_TLV(xname, xhandler_get, xhandler_put, \
			      xmod, xpipe, xinstance, xtask, tlv_array, xgain_val, \
			      xmin, xmax, xpname) \
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, \
	.access = SNDRV_CTL_ELEM_ACCESS_TLV_READ | \
		  SNDRV_CTL_ELEM_ACCESS_READWRITE, \
	.tlv.p = (tlv_array), \
	.info = sst_gain_ctl_info,\
	.get = xhandler_get, .put = xhandler_put, \
	.private_value = (unsigned long)&(struct sst_gain_mixer_control) \
	{ .stereo = true, .max = xmax, .min = xmin, .type = SST_GAIN_TLV, \
	  .module_id = xmod, .pipe_id = xpipe, .task_id = xtask,\
	  .instance_id = xinstance, .gain_val = xgain_val, .pname = xpname}

#define SST_GAIN_KCONTROL_INT(xname, xhandler_get, xhandler_put, \
			      xmod, xpipe, xinstance, xtask, xtype, xgain_val, \
			      xmin, xmax, xpname) \
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, \
	.info = sst_gain_ctl_info, \
	.get = xhandler_get, .put = xhandler_put, \
	.private_value = (unsigned long)&(struct sst_gain_mixer_control) \
	{ .stereo = false, .max = xmax, .min = xmin, .type = xtype, \
	  .module_id = xmod, .pipe_id = xpipe, .task_id = xtask,\
	  .instance_id = xinstance, .gain_val = xgain_val, .pname =  xpname}

#define SST_GAIN_KCONTROL_BOOL(xname, xhandler_get, xhandler_put,\
			       xmod, xpipe, xinstance, xtask, xgain_val, xpname) \
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, \
	.info = snd_soc_info_bool_ext, \
	.get = xhandler_get, .put = xhandler_put, \
	.private_value = (unsigned long)&(struct sst_gain_mixer_control) \
	{ .stereo = false, .type = SST_GAIN_MUTE, \
	  .module_id = xmod, .pipe_id = xpipe, .task_id = xtask,\
	  .instance_id = xinstance, .gain_val = xgain_val, .pname = xpname}
#define SST_CONTROL_NAME(xpname, xmname, xinstance, xtype) \
	xpname " " xmname " " #xinstance " " xtype

#define SST_COMBO_CONTROL_NAME(xpname, xmname, xinstance, xtype, xsubmodule) \
	xpname " " xmname " " #xinstance " " xtype " " xsubmodule

/*
 * 3 Controls for each Gain module
 * e.g.	- pcm0_in Gain 0 Volume
 *	- pcm0_in Gain 0 Ramp Delay
 *	- pcm0_in Gain 0 Switch
 */
#define SST_GAIN_KCONTROLS(xpname, xmname, xmin_gain, xmax_gain, xmin_tc, xmax_tc, \
			   xhandler_get, xhandler_put, \
			   xmod, xpipe, xinstance, xtask, tlv_array, xgain_val) \
	{ SST_GAIN_KCONTROL_INT(SST_CONTROL_NAME(xpname, xmname, xinstance, "Ramp Delay"), \
		xhandler_get, xhandler_put, xmod, xpipe, xinstance, xtask, SST_GAIN_RAMP_DURATION, \
		xgain_val, xmin_tc, xmax_tc, xpname) }, \
	{ SST_GAIN_KCONTROL_BOOL(SST_CONTROL_NAME(xpname, xmname, xinstance, "Switch"), \
		xhandler_get, xhandler_put, xmod, xpipe, xinstance, xtask, \
		xgain_val, xpname) } ,\
	{ SST_GAIN_KCONTROL_TLV(SST_CONTROL_NAME(xpname, xmname, xinstance, "Volume"), \
		xhandler_get, xhandler_put, xmod, xpipe, xinstance, xtask, tlv_array, \
		xgain_val, xmin_gain, xmax_gain, xpname) }

#define SST_GAIN_TC_MIN		5
#define SST_GAIN_TC_MAX		5000
#define SST_GAIN_MIN_VALUE	-1440 /* in 0.1 DB units */
#define SST_GAIN_MAX_VALUE	360

enum sst_algo_kcontrol_type {
	SST_ALGO_PARAMS,
	SST_ALGO_BYPASS,
};

struct sst_algo_control {
	enum sst_algo_kcontrol_type type;
	int max;
	u16 module_id;
	u16 pipe_id;
	u16 task_id;
	u16 cmd_id;
	bool bypass;
	unsigned char *params;
	struct snd_soc_dapm_widget *w;
};

/* size of the control = size of params + size of length field */
#define SST_ALGO_CTL_VALUE(xcount, xtype, xpipe, xmod, xtask, xcmd)			\
	(struct sst_algo_control){							\
		.max = xcount + sizeof(u16), .type = xtype, .module_id = xmod,			\
		.pipe_id = xpipe, .task_id = xtask, .cmd_id = xcmd,			\
	}

#define SST_ALGO_KCONTROL(xname, xcount, xmod, xpipe,					\
			  xtask, xcmd, xtype, xinfo, xget, xput)			\
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,						\
	.name =  xname,									\
	.info = xinfo, .get = xget, .put = xput,					\
	.private_value = (unsigned long)&						\
			SST_ALGO_CTL_VALUE(xcount, xtype, xpipe,			\
					   xmod, xtask, xcmd),				\
}

#define SST_ALGO_KCONTROL_BYTES(xpname, xmname, xcount, xmod,				\
				xpipe, xinstance, xtask, xcmd)				\
	SST_ALGO_KCONTROL(SST_CONTROL_NAME(xpname, xmname, xinstance, "params"),	\
			  xcount, xmod, xpipe, xtask, xcmd, SST_ALGO_PARAMS,		\
			  sst_algo_bytes_ctl_info,					\
			  sst_algo_control_get, sst_algo_control_set)

#define SST_ALGO_KCONTROL_BOOL(xpname, xmname, xmod, xpipe, xinstance, xtask)		\
	SST_ALGO_KCONTROL(SST_CONTROL_NAME(xpname, xmname, xinstance, "bypass"),	\
			  0, xmod, xpipe, xtask, 0, SST_ALGO_BYPASS,			\
			  snd_soc_info_bool_ext,					\
			  sst_algo_control_get, sst_algo_control_set)

#define SST_ALGO_BYPASS_PARAMS(xpname, xmname, xcount, xmod, xpipe,			\
				xinstance, xtask, xcmd)					\
	SST_ALGO_KCONTROL_BOOL(xpname, xmname, xmod, xpipe, xinstance, xtask),		\
	SST_ALGO_KCONTROL_BYTES(xpname, xmname, xcount, xmod, xpipe, xinstance, xtask, xcmd)

#define SST_COMBO_ALGO_KCONTROL_BYTES(xpname, xmname, xsubmod, xcount, xmod,		\
				      xpipe, xinstance, xtask, xcmd)			\
	SST_ALGO_KCONTROL(SST_COMBO_CONTROL_NAME(xpname, xmname, xinstance, "params",	\
						 xsubmod),				\
			  xcount, xmod, xpipe, xtask, xcmd, SST_ALGO_PARAMS,		\
			  sst_algo_bytes_ctl_info,					\
			  sst_algo_control_get, sst_algo_control_set)


struct sst_enum {
	bool tx;
	unsigned short reg;
	unsigned int max;
	const char * const *texts;
	struct snd_soc_dapm_widget *w;
};

/* only 4 slots/channels supported atm */
#define SST_SSP_SLOT_ENUM(s_ch_no, is_tx, xtexts) \
	(struct sst_enum){ .reg = s_ch_no, .tx = is_tx, .max = 4+1, .texts = xtexts, }

#define SST_SLOT_CTL_NAME(xpname, xmname, s_ch_name) \
	xpname " " xmname " " s_ch_name

#define SST_SSP_SLOT_CTL(xpname, xmname, s_ch_name, s_ch_no, is_tx, xtexts, xget, xput) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
	.name = SST_SLOT_CTL_NAME(xpname, xmname, s_ch_name), \
	.info = sst_slot_enum_info, \
	.get = xget, .put = xput, \
	.private_value = (unsigned long)&SST_SSP_SLOT_ENUM(s_ch_no, is_tx, xtexts), \
}

#define SST_MUX_CTL_NAME(xpname, xinstance) \
	xpname " " #xinstance

#define SST_SSP_MUX_ENUM(xreg, xshift, xtexts) \
	(struct soc_enum) SOC_ENUM_DOUBLE(xreg, xshift, xshift, ARRAY_SIZE(xtexts), xtexts)

#define SST_SSP_MUX_CTL(xpname, xinstance, xreg, xshift, xtexts) \
	SOC_DAPM_ENUM(SST_MUX_CTL_NAME(xpname, xinstance), \
			  SST_SSP_MUX_ENUM(xreg, xshift, xtexts))

#endif
