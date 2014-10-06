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
struct sst_cmd_set_params {
	struct sst_destination_id dst;
	u16 command_id;
	char params[0];
} __packed;
#define SST_CONTROL_NAME(xpname, xmname, xinstance, xtype) \
	xpname " " xmname " " #xinstance " " xtype

#define SST_COMBO_CONTROL_NAME(xpname, xmname, xinstance, xtype, xsubmodule) \
	xpname " " xmname " " #xinstance " " xtype " " xsubmodule
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

#endif
