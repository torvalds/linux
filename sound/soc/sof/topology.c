// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2018 Intel Corporation. All rights reserved.
//
// Author: Liam Girdwood <liam.r.girdwood@linux.intel.com>
//

#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/firmware.h>
#include <linux/string.h>
#include <sound/soc-topology.h>
#include <sound/soc.h>
#include <uapi/sound/tlv.h>
#include <sound/tlv.h>
#include <uapi/sound/sof-ipc.h>
#include <uapi/sound/sof-topology.h>
#include "sof-priv.h"

#define COMP_ID_UNASSIGNED		0xffffffff
/* Constants used in the computation of linear volume gain from dB gain */
/* 20th root of 10 in Q1.16 fixed-point notation*/
#define VOL_TWENTIETH_ROOT_OF_TEN	73533
/* 40th root of 10 in Q1.16 fixed-point notation*/
#define VOL_FORTIETH_ROOT_OF_TEN	69419
/* Volume fractional word length */
#define VOLUME_FWL	16
/* 0.5 dB step value in topology TLV */
#define VOL_HALF_DB_STEP	50

/* TLV data items */
#define TLV_ITEMS	3
#define TLV_MIN		0
#define TLV_STEP	1
#define TLV_MUTE	2

static inline int get_tlv_data(const int *p, int tlv[TLV_ITEMS])
{
	/* we only support dB scale TLV type at the moment */
	if ((int)p[SNDRV_CTL_TLVO_TYPE] != SNDRV_CTL_TLVT_DB_SCALE)
		return -EINVAL;

	/* min value in topology tlv data is multiplied by 100 */
	tlv[TLV_MIN] = (int)p[SNDRV_CTL_TLVO_DB_SCALE_MIN] / 100;

	/* volume steps */
	tlv[TLV_STEP] = (int)(p[SNDRV_CTL_TLVO_DB_SCALE_MUTE_AND_STEP] &
				TLV_DB_SCALE_MASK);

	/* mute ON/OFF */
	if ((p[SNDRV_CTL_TLVO_DB_SCALE_MUTE_AND_STEP] &
		TLV_DB_SCALE_MUTE) == 0)
		tlv[TLV_MUTE] = 0;
	else
		tlv[TLV_MUTE] = 1;

	return 0;
}

/* Function to truncate an unsigned 64-bit number
 * by x bits and return 32-bit unsigned number
 * This function also takes care of rounding while truncating
 */
static inline u32 vol_shift_64(u64 i, u32 x)
{
	/* do not truncate more than 32 bits */
	if (x > 32)
		x = 32;

	if (x == 0)
		return (u32)i;

	return (u32)(((i >> (x - 1)) + 1) >> 1);
}

/* Function to compute a ^ exp where,
 * a is a fractional number represented by a fixed-point integer
 * with a fractional world length of "fwl"
 * exp is an integer
 * fwl is the fractional word length
 * Return value is a fractional number represented by a fixed-point
 * integer with a fractional word length of "fwl"
 */
static u32 vol_pow32(u32 a, int exp, u32 fwl)
{
	int i, iter;
	u32 power = 1 << fwl;
	u64 numerator;

	/* if exponent is 0, return 1 */
	if (exp == 0)
		return power;

	/* determine the number of iterations based on the exponent */
	if (exp < 0)
		iter = exp * -1;
	else
		iter = exp;

	/* mutiply a "iter" times to compute power */
	for (i = 0; i < iter; i++) {
		/* Product of 2 Qx.fwl fixed-point numbers yields a Q2*x.2*fwl
		 * Truncate product back to fwl fractional bits with rounding
		 */
		power = vol_shift_64((u64)power * a, fwl);
	}

	if (exp > 0) {
		/* if exp is positive, return the result */
		return power;
	}

	/* if exp is negative, return the multiplicative inverse */
	numerator = (u64)1 << (fwl << 1);
	do_div(numerator, power);

	return (u32)numerator;
}

/* Function to calculate volume gain from TLV data
 * This function can only handle gain steps that are multiples of 0.5 dB
 */
static u32 vol_compute_gain(u32 value, int *tlv)
{
	int dB_gain;
	u32 linear_gain;
	int f_step;

	/* mute volume */
	if (value == 0 && tlv[TLV_MUTE])
		return 0;

	/* compute dB gain from tlv
	 * tlv_step in topology is multiplied by 100
	 */
	dB_gain = tlv[TLV_MIN] + (value * tlv[TLV_STEP]) / 100;

	/* compute linear gain
	 * represented by fixed-point int with VOLUME_FWL fractional bits
	 */
	linear_gain = vol_pow32(VOL_TWENTIETH_ROOT_OF_TEN, dB_gain, VOLUME_FWL);

	/* extract the fractional part of volume step */
	f_step = tlv[TLV_STEP] - (tlv[TLV_STEP] / 100);

	/* if volume step is an odd multiple of 0.5 dB */
	if (f_step == VOL_HALF_DB_STEP && (value & 1))
		linear_gain = vol_shift_64((u64)linear_gain *
						  VOL_FORTIETH_ROOT_OF_TEN,
						  VOLUME_FWL);

	return linear_gain;
}

/* Set up volume table for kcontrols from tlv data
 * "size" specifies the number of entries in the table
 */
static int set_up_volume_table(struct snd_sof_control *scontrol,
			       int tlv[TLV_ITEMS], int size)
{
	int j;

	/* init the volume table */
	scontrol->volume_table = kcalloc(size, sizeof(u32), GFP_KERNEL);
	if (!scontrol->volume_table)
		return -ENOMEM;

	/* populate the volume table */
	for (j = 0; j < size ; j++)
		scontrol->volume_table[j] = vol_compute_gain(j, tlv);

	return 0;
}

struct sof_dai_types {
	const char *name;
	enum sof_ipc_dai_type type;
};

static const struct sof_dai_types sof_dais[] = {
	{"SSP", SOF_DAI_INTEL_SSP},
	{"HDA", SOF_DAI_INTEL_HDA},
	{"DMIC", SOF_DAI_INTEL_DMIC},
};

static enum sof_ipc_dai_type find_dai(const char *name)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(sof_dais); i++) {
		if (strcmp(name, sof_dais[i].name) == 0)
			return sof_dais[i].type;
	}

	return SOF_DAI_INTEL_NONE;
}

/*
 * Supported Frame format types and lookup, add new ones to end of list.
 */

struct sof_frame_types {
	const char *name;
	enum sof_ipc_frame frame;
};

static const struct sof_frame_types sof_frames[] = {
	{"s16le", SOF_IPC_FRAME_S16_LE},
	{"s24le", SOF_IPC_FRAME_S24_4LE},
	{"s32le", SOF_IPC_FRAME_S32_LE},
	{"float", SOF_IPC_FRAME_FLOAT},
};

static enum sof_ipc_frame find_format(const char *name)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(sof_frames); i++) {
		if (strcmp(name, sof_frames[i].name) == 0)
			return sof_frames[i].frame;
	}

	/* use s32le if nothing is specified */
	return SOF_IPC_FRAME_S32_LE;
}

struct sof_effect_types {
	const char *name;
	enum sof_ipc_effect_type type;
};

static const struct sof_effect_types sof_effects[] = {
	{"EQFIR", SOF_EFFECT_INTEL_EQFIR},
	{"EQIIR", SOF_EFFECT_INTEL_EQIIR},
};

static enum sof_ipc_effect_type find_effect(const char *name)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(sof_effects); i++) {
		if (strcmp(name, sof_effects[i].name) == 0)
			return sof_effects[i].type;
	}

	return SOF_EFFECT_INTEL_NONE;
}

/*
 * Standard Kcontrols.
 */

static int sof_control_load_volume(struct snd_soc_component *scomp,
				   struct snd_sof_control *scontrol,
				   struct snd_kcontrol_new *kc,
				   struct snd_soc_tplg_ctl_hdr *hdr)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	struct snd_soc_tplg_mixer_control *mc =
		(struct snd_soc_tplg_mixer_control *)hdr;
	struct sof_ipc_ctrl_data *cdata;

	/* validate topology data */
	if (le32_to_cpu(mc->num_channels) > SND_SOC_TPLG_MAX_CHAN)
		return -EINVAL;

	/* init the volume get/put data */
	scontrol->size = sizeof(struct sof_ipc_ctrl_data) +
			 sizeof(struct sof_ipc_ctrl_value_chan) *
			 le32_to_cpu(mc->num_channels);
	scontrol->control_data = kzalloc(scontrol->size, GFP_KERNEL);
	cdata = scontrol->control_data;
	if (!scontrol->control_data)
		return -ENOMEM;

	scontrol->comp_id = sdev->next_comp_id;
	scontrol->num_channels = le32_to_cpu(mc->num_channels);

	dev_dbg(sdev->dev, "tplg: load kcontrol index %d chans %d\n",
		scontrol->comp_id, scontrol->num_channels);

	return 0;
}

static int sof_control_load_bytes(struct snd_soc_component *scomp,
				  struct snd_sof_control *scontrol,
				  struct snd_kcontrol_new *kc,
				  struct snd_soc_tplg_ctl_hdr *hdr)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	struct sof_ipc_ctrl_data *cdata;
	struct snd_soc_tplg_bytes_control *control =
		(struct snd_soc_tplg_bytes_control *)hdr;

	/* init the get/put bytes data */
	scontrol->size = SOF_IPC_MSG_MAX_SIZE;
	scontrol->control_data = kzalloc(scontrol->size, GFP_KERNEL);
	cdata = scontrol->control_data;
	if (!scontrol->control_data)
		return -ENOMEM;

	scontrol->comp_id = sdev->next_comp_id;
	scontrol->cmd = SOF_CTRL_CMD_BINARY;

	dev_dbg(sdev->dev, "tplg: load kcontrol index %d chans %d\n",
		scontrol->comp_id, scontrol->num_channels);

	if (le32_to_cpu(control->priv.size) > SOF_IPC_MSG_MAX_SIZE) {
		dev_warn(sdev->dev, "bytes priv data size %d too big\n",
			 control->priv.size);
		return -EINVAL;
	}

	if (le32_to_cpu(control->priv.size) > 0) {
		memcpy(cdata->data->data, control->priv.data,
		       le32_to_cpu(control->priv.size));
		cdata->data->size = control->priv.size;
		cdata->data->magic = SOF_ABI_MAGIC;
		cdata->data->abi = SOF_ABI_VERSION;
		cdata->data->comp_abi = SOF_ABI_VERSION;
	}

	return 0;
}

/*
 * Topology Token Parsing.
 * New tokens should be added to headers and parsing tables below.
 */

struct sof_topology_token {
	u32 token;
	u32 type;
	int (*get_token)(void *elem, void *object, u32 offset, u32 size);
	u32 offset;
	u32 size;
};

static int get_token_u32(void *elem, void *object, u32 offset, u32 size)
{
	struct snd_soc_tplg_vendor_value_elem *velem = elem;
	u32 *val = object + offset;

	*val = le32_to_cpu(velem->value);
	return 0;
}

static int get_token_u16(void *elem, void *object, u32 offset, u32 size)
{
	struct snd_soc_tplg_vendor_value_elem *velem = elem;
	u16 *val = object + offset;

	*val = (u16)le32_to_cpu(velem->value);
	return 0;
}

static int get_token_comp_format(void *elem, void *object, u32 offset, u32 size)
{
	struct snd_soc_tplg_vendor_string_elem *velem = elem;
	u32 *val = object + offset;

	*val = find_format(velem->string);
	return 0;
}

static int get_token_dai_type(void *elem, void *object, u32 offset, u32 size)
{
	struct snd_soc_tplg_vendor_string_elem *velem = elem;
	u32 *val = object + offset;

	*val = find_dai(velem->string);
	return 0;
}

static int get_token_effect_type(void *elem, void *object, u32 offset, u32 size)
{
	struct snd_soc_tplg_vendor_string_elem *velem = elem;
	u32 *val = object + offset;

	*val = find_effect(velem->string);
	return 0;
}

/* Buffers */
static const struct sof_topology_token buffer_tokens[] = {
	{SOF_TKN_BUF_SIZE, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc_buffer, size), 0},
	{SOF_TKN_BUF_CAPS, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc_buffer, caps), 0},
};

/* DAI */
static const struct sof_topology_token dai_tokens[] = {
	{SOF_TKN_DAI_DMAC_CONFIG, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc_comp_dai, dmac_config), 0},
	{SOF_TKN_DAI_TYPE, SND_SOC_TPLG_TUPLE_TYPE_STRING, get_token_dai_type,
		offsetof(struct sof_ipc_comp_dai, type), 0},
	{SOF_TKN_DAI_INDEX, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc_comp_dai, dai_index), 0},
};

/* BE DAI link */
static const struct sof_topology_token dai_link_tokens[] = {
	{SOF_TKN_DAI_TYPE, SND_SOC_TPLG_TUPLE_TYPE_STRING, get_token_dai_type,
		offsetof(struct sof_ipc_dai_config, type), 0},
	{SOF_TKN_DAI_INDEX, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc_dai_config, dai_index), 0},
};

/* scheduling */
static const struct sof_topology_token sched_tokens[] = {
	{SOF_TKN_SCHED_DEADLINE, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc_pipe_new, deadline), 0},
	{SOF_TKN_SCHED_PRIORITY, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc_pipe_new, priority), 0},
	{SOF_TKN_SCHED_MIPS, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc_pipe_new, mips), 0},
	{SOF_TKN_SCHED_CORE, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc_pipe_new, core), 0},
	{SOF_TKN_SCHED_FRAMES, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc_pipe_new, frames_per_sched), 0},
	{SOF_TKN_SCHED_TIMER, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc_pipe_new, timer), 0},
};

/* volume */
static const struct sof_topology_token volume_tokens[] = {
	{SOF_TKN_VOLUME_RAMP_STEP_TYPE, SND_SOC_TPLG_TUPLE_TYPE_WORD,
		get_token_u32, offsetof(struct sof_ipc_comp_volume, ramp), 0},
	{SOF_TKN_VOLUME_RAMP_STEP_MS,
		SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc_comp_volume, initial_ramp), 0},
};

/* SRC */
static const struct sof_topology_token src_tokens[] = {
	{SOF_TKN_SRC_RATE_IN, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc_comp_src, source_rate), 0},
	{SOF_TKN_SRC_RATE_OUT, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc_comp_src, sink_rate), 0},
};

/* Tone */
static const struct sof_topology_token tone_tokens[] = {
};

/* EFFECT */
static const struct sof_topology_token effect_tokens[] = {
	{SOF_TKN_EFFECT_TYPE, SND_SOC_TPLG_TUPLE_TYPE_STRING,
		get_token_effect_type,
		offsetof(struct sof_ipc_comp_effect, type), 0},
};

/* PCM */
static const struct sof_topology_token pcm_tokens[] = {
	{SOF_TKN_PCM_DMAC_CONFIG, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc_comp_host, dmac_config), 0},
};

/* Generic components */
static const struct sof_topology_token comp_tokens[] = {
	{SOF_TKN_COMP_PERIOD_SINK_COUNT,
		SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc_comp_config, periods_sink), 0},
	{SOF_TKN_COMP_PERIOD_SOURCE_COUNT,
		SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc_comp_config, periods_source), 0},
	{SOF_TKN_COMP_FORMAT,
		SND_SOC_TPLG_TUPLE_TYPE_STRING, get_token_comp_format,
		offsetof(struct sof_ipc_comp_config, frame_fmt), 0},
	{SOF_TKN_COMP_PRELOAD_COUNT,
		SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc_comp_config, preload_count), 0},
};

/* SSP */
static const struct sof_topology_token ssp_tokens[] = {
	{SOF_TKN_INTEL_SSP_MCLK_KEEP_ACTIVE,
		SND_SOC_TPLG_TUPLE_TYPE_BOOL, get_token_u32,
		offsetof(struct sof_ipc_dai_ssp_params, mclk_keep_active), 0},
	{SOF_TKN_INTEL_SSP_BCLK_KEEP_ACTIVE,
		SND_SOC_TPLG_TUPLE_TYPE_BOOL, get_token_u32,
		offsetof(struct sof_ipc_dai_ssp_params, bclk_keep_active), 0},
	{SOF_TKN_INTEL_SSP_FS_KEEP_ACTIVE,
		SND_SOC_TPLG_TUPLE_TYPE_BOOL, get_token_u32,
		offsetof(struct sof_ipc_dai_ssp_params, fs_keep_active), 0},
	{SOF_TKN_INTEL_SSP_MCLK_ID,
		SND_SOC_TPLG_TUPLE_TYPE_SHORT, get_token_u16,
		offsetof(struct sof_ipc_dai_ssp_params, mclk_id), 0},
	{SOF_TKN_INTEL_SSP_SAMPLE_BITS, SND_SOC_TPLG_TUPLE_TYPE_WORD,
		get_token_u32,
		offsetof(struct sof_ipc_dai_ssp_params, sample_valid_bits), 0},
	{SOF_TKN_INTEL_SSP_FRAME_PULSE_WIDTH, SND_SOC_TPLG_TUPLE_TYPE_SHORT,
		get_token_u16,
		offsetof(struct sof_ipc_dai_ssp_params, frame_pulse_width), 0},
	{SOF_TKN_INTEL_SSP_QUIRKS, SND_SOC_TPLG_TUPLE_TYPE_WORD,
		get_token_u32,
		offsetof(struct sof_ipc_dai_ssp_params, quirks), 0},
	{SOF_TKN_INTEL_SSP_TDM_PADDING_PER_SLOT, SND_SOC_TPLG_TUPLE_TYPE_BOOL,
		get_token_u16,
		offsetof(struct sof_ipc_dai_ssp_params,
			 tdm_per_slot_padding_flag), 0},

};

/* DMIC */
static const struct sof_topology_token dmic_tokens[] = {
	{SOF_TKN_INTEL_DMIC_DRIVER_VERSION,
		SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc_dai_dmic_params, driver_ipc_version),
		0},
	{SOF_TKN_INTEL_DMIC_CLK_MIN,
		SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc_dai_dmic_params, pdmclk_min), 0},
	{SOF_TKN_INTEL_DMIC_CLK_MAX,
		SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc_dai_dmic_params, pdmclk_max), 0},
	{SOF_TKN_INTEL_DMIC_SAMPLE_RATE,
		SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc_dai_dmic_params, fifo_fs_a), 0},
	{SOF_TKN_INTEL_DMIC_DUTY_MIN,
		SND_SOC_TPLG_TUPLE_TYPE_SHORT, get_token_u16,
		offsetof(struct sof_ipc_dai_dmic_params, duty_min), 0},
	{SOF_TKN_INTEL_DMIC_DUTY_MAX,
		SND_SOC_TPLG_TUPLE_TYPE_SHORT, get_token_u16,
		offsetof(struct sof_ipc_dai_dmic_params, duty_max), 0},
	{SOF_TKN_INTEL_DMIC_NUM_PDM_ACTIVE,
		SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc_dai_dmic_params,
			 num_pdm_active), 0},
	{SOF_TKN_INTEL_DMIC_FIFO_WORD_LENGTH,
		SND_SOC_TPLG_TUPLE_TYPE_SHORT, get_token_u16,
		offsetof(struct sof_ipc_dai_dmic_params, fifo_bits_a), 0},
};

/*
 * DMIC PDM Tokens
 * SOF_TKN_INTEL_DMIC_PDM_CTRL_ID should be the first token
 * as it increments the index while parsing the array of pdm tokens
 * and determines the correct offset
 */
static const struct sof_topology_token dmic_pdm_tokens[] = {
	{SOF_TKN_INTEL_DMIC_PDM_CTRL_ID,
		SND_SOC_TPLG_TUPLE_TYPE_SHORT, get_token_u16,
		offsetof(struct sof_ipc_dai_dmic_pdm_ctrl, id),
		0},
	{SOF_TKN_INTEL_DMIC_PDM_MIC_A_Enable,
		SND_SOC_TPLG_TUPLE_TYPE_SHORT, get_token_u16,
		offsetof(struct sof_ipc_dai_dmic_pdm_ctrl, enable_mic_a),
		0},
	{SOF_TKN_INTEL_DMIC_PDM_MIC_B_Enable,
		SND_SOC_TPLG_TUPLE_TYPE_SHORT, get_token_u16,
		offsetof(struct sof_ipc_dai_dmic_pdm_ctrl, enable_mic_b),
		0},
	{SOF_TKN_INTEL_DMIC_PDM_POLARITY_A,
		SND_SOC_TPLG_TUPLE_TYPE_SHORT, get_token_u16,
		offsetof(struct sof_ipc_dai_dmic_pdm_ctrl, polarity_mic_a),
		0},
	{SOF_TKN_INTEL_DMIC_PDM_POLARITY_B,
		SND_SOC_TPLG_TUPLE_TYPE_SHORT, get_token_u16,
		offsetof(struct sof_ipc_dai_dmic_pdm_ctrl, polarity_mic_b),
		0},
	{SOF_TKN_INTEL_DMIC_PDM_CLK_EDGE,
		SND_SOC_TPLG_TUPLE_TYPE_SHORT, get_token_u16,
		offsetof(struct sof_ipc_dai_dmic_pdm_ctrl, clk_edge),
		0},
	{SOF_TKN_INTEL_DMIC_PDM_SKEW,
		SND_SOC_TPLG_TUPLE_TYPE_SHORT, get_token_u16,
		offsetof(struct sof_ipc_dai_dmic_pdm_ctrl, skew),
		0},
};

/* HDA */
static const struct sof_topology_token hda_tokens[] = {
};

static void sof_parse_uuid_tokens(struct snd_soc_component *scomp,
				  void *object,
				  const struct sof_topology_token *tokens,
				  int count,
				  struct snd_soc_tplg_vendor_array *array)
{
	struct snd_soc_tplg_vendor_uuid_elem *elem;
	int i, j;

	/* parse element by element */
	for (i = 0; i < le32_to_cpu(array->num_elems); i++) {
		elem = &array->uuid[i];

		/* search for token */
		for (j = 0; j < count; j++) {
			/* match token type */
			if (tokens[j].type != SND_SOC_TPLG_TUPLE_TYPE_UUID)
				continue;

			/* match token id */
			if (tokens[j].token != le32_to_cpu(elem->token))
				continue;

			/* matched - now load token */
			tokens[j].get_token(elem, object, tokens[j].offset,
					    tokens[j].size);
		}
	}
}

static void sof_parse_string_tokens(struct snd_soc_component *scomp,
				    void *object,
				    const struct sof_topology_token *tokens,
				    int count,
				    struct snd_soc_tplg_vendor_array *array)
{
	struct snd_soc_tplg_vendor_string_elem *elem;
	int i, j;

	/* parse element by element */
	for (i = 0; i < le32_to_cpu(array->num_elems); i++) {
		elem = &array->string[i];

		/* search for token */
		for (j = 0; j < count; j++) {
			/* match token type */
			if (tokens[j].type != SND_SOC_TPLG_TUPLE_TYPE_STRING)
				continue;

			/* match token id */
			if (tokens[j].token != le32_to_cpu(elem->token))
				continue;

			/* matched - now load token */
			tokens[j].get_token(elem, object, tokens[j].offset,
					    tokens[j].size);
		}
	}
}

static void sof_parse_word_tokens(struct snd_soc_component *scomp,
				  void *object,
				  const struct sof_topology_token *tokens,
				  int count,
				  struct snd_soc_tplg_vendor_array *array)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	struct snd_soc_tplg_vendor_value_elem *elem;
	size_t size = sizeof(struct sof_ipc_dai_dmic_pdm_ctrl);
	int i, j;
	u32 offset;
	u32 *index = NULL;

	/* parse element by element */
	for (i = 0; i < le32_to_cpu(array->num_elems); i++) {
		elem = &array->value[i];

		/* search for token */
		for (j = 0; j < count; j++) {
			/* match token type */
			if (!(tokens[j].type == SND_SOC_TPLG_TUPLE_TYPE_WORD ||
			      tokens[j].type == SND_SOC_TPLG_TUPLE_TYPE_SHORT))
				continue;

			/* match token id */
			if (tokens[j].token != le32_to_cpu(elem->token))
				continue;

			/* pdm config array index */
			if (sdev->private)
				index = (u32 *)sdev->private;

			/* matched - determine offset */
			switch (tokens[j].token) {
			case SOF_TKN_INTEL_DMIC_PDM_CTRL_ID:

				/* inc number of pdm array index */
				if (index)
					(*index)++;
				/* fallthrough */
			case SOF_TKN_INTEL_DMIC_PDM_MIC_A_Enable:
			case SOF_TKN_INTEL_DMIC_PDM_MIC_B_Enable:
			case SOF_TKN_INTEL_DMIC_PDM_POLARITY_A:
			case SOF_TKN_INTEL_DMIC_PDM_POLARITY_B:
			case SOF_TKN_INTEL_DMIC_PDM_CLK_EDGE:
			case SOF_TKN_INTEL_DMIC_PDM_SKEW:

				/* check if array index is valid */
				if (!index || *index == 0) {
					dev_err(sdev->dev,
						"error: invalid array offset\n");
					continue;
				} else {
					/* offset within the pdm config array */
					offset = size * (*index - 1);
				}
				break;
			default:
				offset = 0;
				break;
			}

			/* load token */
			tokens[j].get_token(elem, object,
					    offset + tokens[j].offset,
					    tokens[j].size);
		}
	}
}

static int sof_parse_tokens(struct snd_soc_component *scomp,
			    void *object,
			    const struct sof_topology_token *tokens,
			    int count,
			    struct snd_soc_tplg_vendor_array *array,
			    int priv_size)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	int asize;

	while (priv_size > 0) {
		asize = le32_to_cpu(array->size);

		/* validate asize */
		if (asize < 0) { /* FIXME: A zero-size array makes no sense */
			dev_err(sdev->dev, "error: invalid array size 0x%x\n",
				asize);
			return -EINVAL;
		}

		/* make sure there is enough data before parsing */
		priv_size -= asize;
		if (priv_size < 0) {
			dev_err(sdev->dev, "error: invalid array size 0x%x\n",
				asize);
			return -EINVAL;
		}

		/* call correct parser depending on type */
		switch (le32_to_cpu(array->type)) {
		case SND_SOC_TPLG_TUPLE_TYPE_UUID:
			sof_parse_uuid_tokens(scomp, object, tokens, count,
					      array);
			break;
		case SND_SOC_TPLG_TUPLE_TYPE_STRING:
			sof_parse_string_tokens(scomp, object, tokens, count,
						array);
			break;
		case SND_SOC_TPLG_TUPLE_TYPE_BOOL:
		case SND_SOC_TPLG_TUPLE_TYPE_BYTE:
		case SND_SOC_TPLG_TUPLE_TYPE_WORD:
		case SND_SOC_TPLG_TUPLE_TYPE_SHORT:
			sof_parse_word_tokens(scomp, object, tokens, count,
					      array);
			break;
		default:
			dev_err(sdev->dev, "error: unknown token type %d\n",
				array->type);
			return -EINVAL;
		}

		/* next array */
		array = (void *)array + asize;
	}
	return 0;
}

static void sof_dbg_comp_config(struct snd_soc_component *scomp,
				struct sof_ipc_comp_config *config)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);

	dev_dbg(sdev->dev, " config: periods snk %d src %d fmt %d\n",
		config->periods_sink, config->periods_source,
		config->frame_fmt);
}

/* external kcontrol init - used for any driver specific init */
static int sof_control_load(struct snd_soc_component *scomp, int index,
			    struct snd_kcontrol_new *kc,
			    struct snd_soc_tplg_ctl_hdr *hdr)
{
	struct soc_mixer_control *sm;
	struct soc_bytes_ext  *sbe;
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	struct snd_soc_dobj *dobj = NULL;
	struct snd_sof_control *scontrol;
	int ret = -EINVAL;

	dev_dbg(sdev->dev, "tplg: load control type %d name : %s\n",
		hdr->type, hdr->name);

	scontrol = kzalloc(sizeof(*scontrol), GFP_KERNEL);
	if (!scontrol)
		return -ENOMEM;

	scontrol->sdev = sdev;
	mutex_init(&scontrol->mutex);

	switch (le32_to_cpu(hdr->ops.info)) {
	case SND_SOC_TPLG_CTL_VOLSW:
	case SND_SOC_TPLG_CTL_VOLSW_SX:
	case SND_SOC_TPLG_CTL_VOLSW_XR_SX:
		sm = (struct soc_mixer_control *)kc->private_value;
		dobj = &sm->dobj;
		ret = sof_control_load_volume(scomp, scontrol, kc, hdr);
		break;
	case SND_SOC_TPLG_CTL_BYTES:
		sbe = (struct soc_bytes_ext *)kc->private_value;
		dobj = &sbe->dobj;
		ret = sof_control_load_bytes(scomp, scontrol, kc, hdr);
		break;
	case SND_SOC_TPLG_CTL_ENUM:
	case SND_SOC_TPLG_CTL_ENUM_VALUE:
	case SND_SOC_TPLG_CTL_RANGE:
	case SND_SOC_TPLG_CTL_STROBE:
	case SND_SOC_TPLG_DAPM_CTL_VOLSW:
	case SND_SOC_TPLG_DAPM_CTL_ENUM_DOUBLE:
	case SND_SOC_TPLG_DAPM_CTL_ENUM_VIRT:
	case SND_SOC_TPLG_DAPM_CTL_ENUM_VALUE:
	case SND_SOC_TPLG_DAPM_CTL_PIN:
	default:
		dev_warn(sdev->dev, "control type not supported %d:%d:%d\n",
			 hdr->ops.get, hdr->ops.put, hdr->ops.info);
		kfree(scontrol);
		return 0;
	}

	dobj->private = scontrol;
	list_add(&scontrol->list, &sdev->kcontrol_list);
	return ret;
}

static int sof_control_unload(struct snd_soc_component *scomp,
			      struct snd_soc_dobj *dobj)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	struct sof_ipc_free fcomp;
	struct snd_sof_control *scontrol = dobj->private;

	dev_dbg(sdev->dev, "tplg: unload control name : %s\n", scomp->name);

	fcomp.hdr.cmd = SOF_IPC_GLB_TPLG_MSG | SOF_IPC_TPLG_COMP_FREE;
	fcomp.hdr.size = sizeof(fcomp);
	fcomp.id = scontrol->comp_id;

	/* send IPC to the DSP */
	return sof_ipc_tx_message(sdev->ipc,
				  fcomp.hdr.cmd, &fcomp, sizeof(fcomp),
				  NULL, 0);
}

/*
 * DAI Topology
 */

static int sof_connect_dai_widget(struct snd_soc_component *scomp,
				  struct snd_soc_dapm_widget *w,
				  struct snd_soc_tplg_dapm_widget *tw,
				  struct snd_sof_dai *dai)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	struct snd_soc_card *card = scomp->card;
	struct snd_soc_pcm_runtime *rtd;

	list_for_each_entry(rtd, &card->rtd_list, list) {
		dev_dbg(sdev->dev, "tplg: check widget: %s stream: %s dai stream: %s\n",
			w->name,  w->sname, rtd->dai_link->stream_name);

		if (!w->sname || !rtd->dai_link->stream_name)
			continue;

		/* does stream match DAI link ? */
		if (strcmp(w->sname, rtd->dai_link->stream_name))
			continue;

		switch (w->id) {
		case snd_soc_dapm_dai_out:
			rtd->cpu_dai->capture_widget = w;
			if (dai)
				dai->name = rtd->dai_link->name;
			dev_dbg(sdev->dev, "tplg: connected widget %s -> DAI link %s\n",
				w->name, rtd->dai_link->name);
			break;
		case snd_soc_dapm_dai_in:
			rtd->cpu_dai->playback_widget = w;
			if (dai)
				dai->name = rtd->dai_link->name;
			dev_dbg(sdev->dev, "tplg: connected widget %s -> DAI link %s\n",
				w->name, rtd->dai_link->name);
			break;
		default:
			break;
		}
	}

	/* check we have a connection */
	if (!dai->name) {
		dev_err(sdev->dev, "error: can't connect DAI %s stream %s\n",
			w->name, w->sname);
		return -EINVAL;
	}

	return 0;
}

static int sof_widget_load_dai(struct snd_soc_component *scomp, int index,
			       struct snd_sof_widget *swidget,
			       struct snd_soc_tplg_dapm_widget *tw,
			       struct sof_ipc_comp_reply *r,
			       struct snd_sof_dai *dai)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	struct snd_soc_tplg_private *private = &tw->priv;
	struct sof_ipc_comp_dai comp_dai;
	int ret;

	/* configure dai IPC message */
	memset(&comp_dai, 0, sizeof(comp_dai));
	comp_dai.comp.hdr.size = sizeof(comp_dai);
	comp_dai.comp.hdr.cmd = SOF_IPC_GLB_TPLG_MSG | SOF_IPC_TPLG_COMP_NEW;
	comp_dai.comp.id = swidget->comp_id;
	comp_dai.comp.type = SOF_COMP_DAI;
	comp_dai.comp.pipeline_id = index;

	ret = sof_parse_tokens(scomp, &comp_dai, dai_tokens,
			       ARRAY_SIZE(dai_tokens), private->array,
			       le32_to_cpu(private->size));
	if (ret != 0) {
		dev_err(sdev->dev, "error: parse dai tokens failed %d\n",
			le32_to_cpu(private->size));
		return ret;
	}

	ret = sof_parse_tokens(scomp, &comp_dai.config, comp_tokens,
			       ARRAY_SIZE(comp_tokens), private->array,
			       le32_to_cpu(private->size));
	if (ret != 0) {
		dev_err(sdev->dev, "error: parse dai.cfg tokens failed %d\n",
			private->size);
		return ret;
	}

	dev_dbg(sdev->dev, "dai %s: type %d index %d\n",
		swidget->widget->name, comp_dai.type, comp_dai.dai_index);
	sof_dbg_comp_config(scomp, &comp_dai.config);

	ret = sof_ipc_tx_message(sdev->ipc, comp_dai.comp.hdr.cmd,
				 &comp_dai, sizeof(comp_dai), r, sizeof(*r));

	if (ret == 0 && dai) {
		dai->sdev = sdev;
		memcpy(&dai->comp_dai, &comp_dai, sizeof(comp_dai));
	}

	return ret;
}

/*
 * Buffer topology
 */

static int sof_widget_load_buffer(struct snd_soc_component *scomp, int index,
				  struct snd_sof_widget *swidget,
				  struct snd_soc_tplg_dapm_widget *tw,
				  struct sof_ipc_comp_reply *r)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	struct snd_soc_tplg_private *private = &tw->priv;
	struct sof_ipc_buffer *buffer;
	int ret;

	buffer = kzalloc(sizeof(*buffer), GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	/* configure dai IPC message */
	buffer->comp.hdr.size = sizeof(*buffer);
	buffer->comp.hdr.cmd = SOF_IPC_GLB_TPLG_MSG | SOF_IPC_TPLG_BUFFER_NEW;
	buffer->comp.id = swidget->comp_id;
	buffer->comp.type = SOF_COMP_BUFFER;
	buffer->comp.pipeline_id = index;

	ret = sof_parse_tokens(scomp, buffer, buffer_tokens,
			       ARRAY_SIZE(buffer_tokens), private->array,
			       le32_to_cpu(private->size));
	if (ret != 0) {
		dev_err(sdev->dev, "error: parse buffer tokens failed %d\n",
			private->size);
		kfree(buffer);
		return ret;
	}

	dev_dbg(sdev->dev, "buffer %s: size %d caps 0x%x\n",
		swidget->widget->name, buffer->size, buffer->caps);

	swidget->private = (void *)buffer;

	ret = sof_ipc_tx_message(sdev->ipc, buffer->comp.hdr.cmd, buffer,
				 sizeof(*buffer), r, sizeof(*r));
	if (ret < 0) {
		dev_err(sdev->dev, "buffer %s load failed\n",
			swidget->widget->name);
		kfree(buffer);
	}

	return ret;
}

/*
 * PCM Topology
 */

static int sof_widget_load_pcm(struct snd_soc_component *scomp, int index,
			       struct snd_sof_widget *swidget,
			       enum sof_ipc_stream_direction dir,
			       struct snd_soc_tplg_dapm_widget *tw,
			       struct sof_ipc_comp_reply *r)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	struct snd_soc_tplg_private *private = &tw->priv;
	struct sof_ipc_comp_host *host;
	int ret;

	host = kzalloc(sizeof(*host), GFP_KERNEL);
	if (!host)
		return -ENOMEM;

	/* configure mixer IPC message */
	host->comp.hdr.size = sizeof(*host);
	host->comp.hdr.cmd = SOF_IPC_GLB_TPLG_MSG | SOF_IPC_TPLG_COMP_NEW;
	host->comp.id = swidget->comp_id;
	host->comp.type = SOF_COMP_HOST;
	host->comp.pipeline_id = index;
	host->direction = dir;

	ret = sof_parse_tokens(scomp, host, pcm_tokens,
			       ARRAY_SIZE(pcm_tokens), private->array,
			       le32_to_cpu(private->size));
	if (ret != 0) {
		dev_err(sdev->dev, "error: parse host tokens failed %d\n",
			private->size);
		goto err;
	}

	ret = sof_parse_tokens(scomp, &host->config, comp_tokens,
			       ARRAY_SIZE(comp_tokens), private->array,
			       le32_to_cpu(private->size));
	if (ret != 0) {
		dev_err(sdev->dev, "error: parse host.cfg tokens failed %d\n",
			le32_to_cpu(private->size));
		goto err;
	}

	dev_dbg(sdev->dev, "loaded host %s\n", swidget->widget->name);
	sof_dbg_comp_config(scomp, &host->config);

	swidget->private = (void *)host;

	ret = sof_ipc_tx_message(sdev->ipc, host->comp.hdr.cmd, host,
				 sizeof(*host), r, sizeof(*r));
	if (ret >= 0)
		return ret;
err:
	kfree(host);
	return ret;
}

/*
 * Pipeline Topology
 */

static int sof_widget_load_pipeline(struct snd_soc_component *scomp,
				    int index, struct snd_sof_widget *swidget,
				    struct snd_soc_tplg_dapm_widget *tw,
				    struct sof_ipc_comp_reply *r)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	struct snd_soc_tplg_private *private = &tw->priv;
	struct sof_ipc_pipe_new *pipeline;
	struct snd_sof_widget *comp_swidget;
	int ret;

	pipeline = kzalloc(sizeof(*pipeline), GFP_KERNEL);
	if (!pipeline)
		return -ENOMEM;

	/* configure dai IPC message */
	pipeline->hdr.size = sizeof(*pipeline);
	pipeline->hdr.cmd = SOF_IPC_GLB_TPLG_MSG | SOF_IPC_TPLG_PIPE_NEW;
	pipeline->pipeline_id = index;
	pipeline->comp_id = swidget->comp_id;

	/* component at start of pipeline is our stream id */
	comp_swidget = snd_sof_find_swidget(sdev, tw->sname);
	if (!comp_swidget) {
		dev_err(sdev->dev, "error: widget %s refers to non existent widget %s\n",
			tw->name, tw->sname);
		ret = -EINVAL;
		goto err;
	}

	pipeline->sched_id = comp_swidget->comp_id;

	dev_dbg(sdev->dev, "tplg: pipeline id %d comp %d scheduling comp id %d\n",
		pipeline->pipeline_id, pipeline->comp_id, pipeline->sched_id);

	ret = sof_parse_tokens(scomp, pipeline, sched_tokens,
			       ARRAY_SIZE(sched_tokens), private->array,
			       le32_to_cpu(private->size));
	if (ret != 0) {
		dev_err(sdev->dev, "error: parse pipeline tokens failed %d\n",
			private->size);
		goto err;
	}

	dev_dbg(sdev->dev, "pipeline %s: deadline %d pri %d mips %d core %d frames %d\n",
		swidget->widget->name, pipeline->deadline, pipeline->priority,
		pipeline->mips, pipeline->core, pipeline->frames_per_sched);

	swidget->private = (void *)pipeline;

	ret = sof_ipc_tx_message(sdev->ipc, pipeline->hdr.cmd, pipeline,
				 sizeof(*pipeline), r, sizeof(*r));
	if (ret >= 0)
		return ret;
err:
	kfree(pipeline);
	return ret;
}

/*
 * Mixer topology
 */

static int sof_widget_load_mixer(struct snd_soc_component *scomp, int index,
				 struct snd_sof_widget *swidget,
				 struct snd_soc_tplg_dapm_widget *tw,
				 struct sof_ipc_comp_reply *r)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	struct snd_soc_tplg_private *private = &tw->priv;
	struct sof_ipc_comp_mixer *mixer;
	int ret;

	mixer = kzalloc(sizeof(*mixer), GFP_KERNEL);
	if (!mixer)
		return -ENOMEM;

	/* configure mixer IPC message */
	mixer->comp.hdr.size = sizeof(*mixer);
	mixer->comp.hdr.cmd = SOF_IPC_GLB_TPLG_MSG | SOF_IPC_TPLG_COMP_NEW;
	mixer->comp.id = swidget->comp_id;
	mixer->comp.type = SOF_COMP_MIXER;
	mixer->comp.pipeline_id = index;

	ret = sof_parse_tokens(scomp, &mixer->config, comp_tokens,
			       ARRAY_SIZE(comp_tokens), private->array,
			       le32_to_cpu(private->size));
	if (ret != 0) {
		dev_err(sdev->dev, "error: parse mixer.cfg tokens failed %d\n",
			private->size);
		kfree(mixer);
		return ret;
	}

	sof_dbg_comp_config(scomp, &mixer->config);

	swidget->private = (void *)mixer;

	ret = sof_ipc_tx_message(sdev->ipc, mixer->comp.hdr.cmd, mixer,
				 sizeof(*mixer), r, sizeof(*r));
	if (ret < 0)
		kfree(mixer);

	return ret;
}

/*
 * PGA Topology
 */

static int sof_widget_load_pga(struct snd_soc_component *scomp, int index,
			       struct snd_sof_widget *swidget,
			       struct snd_soc_tplg_dapm_widget *tw,
			       struct sof_ipc_comp_reply *r)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	struct snd_soc_tplg_private *private = &tw->priv;
	struct sof_ipc_comp_volume *volume;
	struct snd_soc_dapm_widget *widget = swidget->widget;
	const struct snd_kcontrol_new *kc = NULL;
	struct soc_mixer_control *sm;
	struct snd_sof_control *scontrol;
	const unsigned int *p;
	int ret, tlv[TLV_ITEMS];

	volume = kzalloc(sizeof(*volume), GFP_KERNEL);
	if (!volume)
		return -ENOMEM;

	if (le32_to_cpu(tw->num_kcontrols) != 1) {
		dev_err(sdev->dev, "error: invalid kcontrol count %d for volume\n",
			tw->num_kcontrols);
		ret = -EINVAL;
		goto err;
	}

	/* set up volume gain tables for kcontrol */
	kc = &widget->kcontrol_news[0];
	sm = (struct soc_mixer_control *)kc->private_value;

	/* get volume control */
	scontrol = sm->dobj.private;

	/* set cmd for pga kcontrol */
	scontrol->cmd = SOF_CTRL_CMD_VOLUME;

	/* get topology tlv data */
	p = kc->tlv.p;

	/* extract tlv data */
	if (get_tlv_data(p, tlv) < 0) {
		dev_err(sdev->dev, "error: invalid TLV data\n");
		ret = -EINVAL;
		goto err;
	}

	/* set up volume table */
	ret = set_up_volume_table(scontrol, tlv, sm->max + 1);
	if (ret < 0) {
		dev_err(sdev->dev, "error: setting up volume table\n");
		goto err;
	}

	/* configure dai IPC message */
	volume->comp.hdr.size = sizeof(*volume);
	volume->comp.hdr.cmd = SOF_IPC_GLB_TPLG_MSG | SOF_IPC_TPLG_COMP_NEW;
	volume->comp.id = swidget->comp_id;
	volume->comp.type = SOF_COMP_VOLUME;
	volume->comp.pipeline_id = index;

	ret = sof_parse_tokens(scomp, volume, volume_tokens,
			       ARRAY_SIZE(volume_tokens), private->array,
			       le32_to_cpu(private->size));
	if (ret != 0) {
		dev_err(sdev->dev, "error: parse volume tokens failed %d\n",
			private->size);
		goto err;
	}
	ret = sof_parse_tokens(scomp, &volume->config, comp_tokens,
			       ARRAY_SIZE(comp_tokens), private->array,
			       le32_to_cpu(private->size));
	if (ret != 0) {
		dev_err(sdev->dev, "error: parse volume.cfg tokens failed %d\n",
			le32_to_cpu(private->size));
		goto err;
	}

	sof_dbg_comp_config(scomp, &volume->config);

	swidget->private = (void *)volume;

	ret = sof_ipc_tx_message(sdev->ipc, volume->comp.hdr.cmd, volume,
				 sizeof(*volume), r, sizeof(*r));
	if (ret >= 0)
		return ret;
err:
	kfree(volume);
	return ret;
}

/*
 * SRC Topology
 */

static int sof_widget_load_src(struct snd_soc_component *scomp, int index,
			       struct snd_sof_widget *swidget,
			       struct snd_soc_tplg_dapm_widget *tw,
			       struct sof_ipc_comp_reply *r)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	struct snd_soc_tplg_private *private = &tw->priv;
	struct sof_ipc_comp_src *src;
	int ret;

	src = kzalloc(sizeof(*src), GFP_KERNEL);
	if (!src)
		return -ENOMEM;

	/* configure mixer IPC message */
	src->comp.hdr.size = sizeof(*src);
	src->comp.hdr.cmd = SOF_IPC_GLB_TPLG_MSG | SOF_IPC_TPLG_COMP_NEW;
	src->comp.id = swidget->comp_id;
	src->comp.type = SOF_COMP_SRC;
	src->comp.pipeline_id = index;

	ret = sof_parse_tokens(scomp, src, src_tokens,
			       ARRAY_SIZE(src_tokens), private->array,
			       le32_to_cpu(private->size));
	if (ret != 0) {
		dev_err(sdev->dev, "error: parse src tokens failed %d\n",
			private->size);
		goto err;
	}

	ret = sof_parse_tokens(scomp, &src->config, comp_tokens,
			       ARRAY_SIZE(comp_tokens), private->array,
			       le32_to_cpu(private->size));
	if (ret != 0) {
		dev_err(sdev->dev, "error: parse src.cfg tokens failed %d\n",
			le32_to_cpu(private->size));
		goto err;
	}

	dev_dbg(sdev->dev, "src %s: source rate %d sink rate %d\n",
		swidget->widget->name, src->source_rate, src->sink_rate);
	sof_dbg_comp_config(scomp, &src->config);

	swidget->private = (void *)src;

	ret = sof_ipc_tx_message(sdev->ipc, src->comp.hdr.cmd, src,
				 sizeof(*src), r, sizeof(*r));
	if (ret >= 0)
		return ret;
err:
	kfree(src);
	return ret;
}

/*
 * Signal Generator Topology
 */

static int sof_widget_load_siggen(struct snd_soc_component *scomp, int index,
				  struct snd_sof_widget *swidget,
				  struct snd_soc_tplg_dapm_widget *tw,
				  struct sof_ipc_comp_reply *r)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	struct snd_soc_tplg_private *private = &tw->priv;
	struct sof_ipc_comp_tone *tone;
	int ret;

	tone = kzalloc(sizeof(*tone), GFP_KERNEL);
	if (!tone)
		return -ENOMEM;

	/* configure mixer IPC message */
	tone->comp.hdr.size = sizeof(*tone);
	tone->comp.hdr.cmd = SOF_IPC_GLB_TPLG_MSG | SOF_IPC_TPLG_COMP_NEW;
	tone->comp.id = swidget->comp_id;
	tone->comp.type = SOF_COMP_TONE;
	tone->comp.pipeline_id = index;

	ret = sof_parse_tokens(scomp, tone, tone_tokens,
			       ARRAY_SIZE(tone_tokens), private->array,
			       le32_to_cpu(private->size));
	if (ret != 0) {
		dev_err(sdev->dev, "error: parse tone tokens failed %d\n",
			le32_to_cpu(private->size));
		goto err;
	}

	ret = sof_parse_tokens(scomp, &tone->config, comp_tokens,
			       ARRAY_SIZE(comp_tokens), private->array,
			       le32_to_cpu(private->size));
	if (ret != 0) {
		dev_err(sdev->dev, "error: parse tone.cfg tokens failed %d\n",
			le32_to_cpu(private->size));
		goto err;
	}

	dev_dbg(sdev->dev, "tone %s: frequency %d amplitude %d\n",
		swidget->widget->name, tone->frequency, tone->amplitude);
	sof_dbg_comp_config(scomp, &tone->config);

	swidget->private = (void *)tone;

	ret = sof_ipc_tx_message(sdev->ipc, tone->comp.hdr.cmd, tone,
				 sizeof(*tone), r, sizeof(*r));
	if (ret >= 0)
		return ret;
err:
	kfree(tone);
	return ret;
}

static int sof_effect_fir_load(struct snd_soc_component *scomp, int index,
			       struct snd_sof_widget *swidget,
			       struct snd_soc_tplg_dapm_widget *tw,
			       struct sof_ipc_comp_reply *r)

{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	struct snd_soc_tplg_private *private = &tw->priv;
	struct snd_sof_control *scontrol = NULL;
	struct snd_soc_dapm_widget *widget = swidget->widget;
	const struct snd_kcontrol_new *kc = NULL;
	struct soc_bytes_ext *sbe;
	struct sof_abi_hdr *pdata = NULL;
	struct sof_ipc_comp_eq_fir *fir;
	size_t ipc_size = 0, fir_data_size = 0;
	int ret;

	/* get possible eq controls */
	kc = &widget->kcontrol_news[0];
	if (kc) {
		sbe = (struct soc_bytes_ext *)kc->private_value;
		scontrol = sbe->dobj.private;
	}

	/*
	 * Check if there's eq parameters in control's private member and set
	 * data size accordingly. If there's no parameters eq will use defaults
	 * in firmware (which in this case is passthrough).
	 */
	if (scontrol && scontrol->cmd == SOF_CTRL_CMD_BINARY) {
		pdata = scontrol->control_data->data;
		if (pdata->size > 0 && pdata->magic == SOF_ABI_MAGIC)
			fir_data_size = pdata->size;
	}

	ipc_size = sizeof(struct sof_ipc_comp_eq_fir) +
		le32_to_cpu(private->size) +
		fir_data_size;

	fir = kzalloc(ipc_size, GFP_KERNEL);
	if (!fir)
		return -ENOMEM;

	/* configure fir IPC message */
	fir->comp.hdr.size = ipc_size;
	fir->comp.hdr.cmd = SOF_IPC_GLB_TPLG_MSG | SOF_IPC_TPLG_COMP_NEW;
	fir->comp.id = swidget->comp_id;
	fir->comp.type = SOF_COMP_EQ_FIR;
	fir->comp.pipeline_id = index;

	ret = sof_parse_tokens(scomp, &fir->config, comp_tokens,
			       ARRAY_SIZE(comp_tokens), private->array,
			       le32_to_cpu(private->size));
	if (ret != 0) {
		dev_err(sdev->dev, "error: parse fir.cfg tokens failed %d\n",
			le32_to_cpu(private->size));
		goto err;
	}

	sof_dbg_comp_config(scomp, &fir->config);

	/* we have a private data found in control, so copy it */
	if (fir_data_size > 0) {
		memcpy(&fir->data, pdata->data, pdata->size);
		fir->size = fir_data_size;
	}

	swidget->private = (void *)fir;

	ret = sof_ipc_tx_message(sdev->ipc, fir->comp.hdr.cmd, fir,
				 ipc_size, r, sizeof(*r));
	if (ret >= 0)
		return ret;
err:
	kfree(fir);
	return ret;
}

static int sof_effect_iir_load(struct snd_soc_component *scomp, int index,
			       struct snd_sof_widget *swidget,
			       struct snd_soc_tplg_dapm_widget *tw,
			       struct sof_ipc_comp_reply *r)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	struct snd_soc_tplg_private *private = &tw->priv;
	struct snd_soc_dapm_widget *widget = swidget->widget;
	const struct snd_kcontrol_new *kc = NULL;
	struct soc_bytes_ext *sbe;
	struct snd_sof_control *scontrol = NULL;
	struct sof_abi_hdr *pdata = NULL;
	struct sof_ipc_comp_eq_iir *iir;
	size_t ipc_size = 0, iir_data_size = 0;
	int ret;

	/* get possible eq controls */
	kc = &widget->kcontrol_news[0];
	if (kc) {
		sbe = (struct soc_bytes_ext *)kc->private_value;
		scontrol = sbe->dobj.private;
	}

	/*
	 * Check if there's eq parameters in control's private member and set
	 * data size accordingly. If there's no parameters eq will use defaults
	 * in firmware (which in this case is passthrough).
	 */
	if (scontrol && scontrol->cmd == SOF_CTRL_CMD_BINARY) {
		pdata = scontrol->control_data->data;
		if (pdata->size > 0 && pdata->magic == SOF_ABI_MAGIC)
			iir_data_size = pdata->size;
	}

	ipc_size = sizeof(struct sof_ipc_comp_eq_iir) +
		le32_to_cpu(private->size) +
		iir_data_size;

	iir = kzalloc(ipc_size, GFP_KERNEL);
	if (!iir)
		return -ENOMEM;

	/* configure iir IPC message */
	iir->comp.hdr.size = ipc_size;
	iir->comp.hdr.cmd = SOF_IPC_GLB_TPLG_MSG | SOF_IPC_TPLG_COMP_NEW;
	iir->comp.id = swidget->comp_id;
	iir->comp.type = SOF_COMP_EQ_IIR;
	iir->comp.pipeline_id = index;

	ret = sof_parse_tokens(scomp, &iir->config, comp_tokens,
			       ARRAY_SIZE(comp_tokens), private->array,
			       le32_to_cpu(private->size));
	if (ret != 0) {
		dev_err(sdev->dev, "error: parse iir.cfg tokens failed %d\n",
			le32_to_cpu(private->size));
		goto err;
	}

	sof_dbg_comp_config(scomp, &iir->config);

	/* we have a private data found in control, so copy it */
	if (iir_data_size > 0) {
		memcpy(&iir->data, pdata->data, pdata->size);
		iir->size = iir_data_size;
	}

	swidget->private = (void *)iir;

	ret = sof_ipc_tx_message(sdev->ipc, iir->comp.hdr.cmd, iir,
				 ipc_size, r, sizeof(*r));
	if (ret >= 0)
		return ret;
err:
	kfree(iir);
	return ret;
}

/*
 * Effect Topology
 */

static int sof_widget_load_effect(struct snd_soc_component *scomp, int index,
				  struct snd_sof_widget *swidget,
				  struct snd_soc_tplg_dapm_widget *tw,
				  struct sof_ipc_comp_reply *r)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	struct snd_soc_tplg_private *private = &tw->priv;
	struct sof_ipc_comp_effect config;
	int ret;

	/* check we have some tokens - we need at least effect type */
	if (le32_to_cpu(private->size) == 0) {
		dev_err(sdev->dev, "error: effect tokens not found\n");
		return -EINVAL;
	}

	memset(&config, 0, sizeof(config));

	/* get the effect token */
	ret = sof_parse_tokens(scomp, &config, effect_tokens,
			       ARRAY_SIZE(effect_tokens), private->array,
			       le32_to_cpu(private->size));
	if (ret != 0) {
		dev_err(sdev->dev, "error: parse effect tokens failed %d\n",
			le32_to_cpu(private->size));
		return ret;
	}

	/* now load effect specific data and send IPC */
	switch (config.type) {
	case SOF_EFFECT_INTEL_EQFIR:
		ret = sof_effect_fir_load(scomp, index, swidget, tw, r);
		break;
	case SOF_EFFECT_INTEL_EQIIR:
		ret = sof_effect_iir_load(scomp, index, swidget, tw, r);
		break;
	default:
		dev_err(sdev->dev, "error: invalid effect type %d\n",
			config.type);
		ret = -EINVAL;
		break;
	}

	if (ret < 0) {
		dev_err(sdev->dev, "error: effect loading failed\n");
		return ret;
	}

	return 0;
}

/*
 * Generic widget loader.
 */

static int sof_widget_load(struct snd_soc_component *scomp, int index,
			   struct snd_soc_dapm_widget *w,
			   struct snd_soc_tplg_dapm_widget *tw)
{
	/* nothing todo atm */
	return 0;
}

/* external widget init - used for any driver specific init */
static int sof_widget_ready(struct snd_soc_component *scomp, int index,
			    struct snd_soc_dapm_widget *w,
			    struct snd_soc_tplg_dapm_widget *tw)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	struct snd_sof_widget *swidget;
	struct snd_sof_dai *dai;
	struct sof_ipc_comp_reply reply;
	struct snd_sof_control *scontrol = NULL;
	int ret = 0;

	swidget = kzalloc(sizeof(*swidget), GFP_KERNEL);
	if (!swidget)
		return -ENOMEM;

	swidget->sdev = sdev;
	swidget->widget = w;
	swidget->comp_id = sdev->next_comp_id++;
	swidget->complete = 0;
	swidget->id = w->id;
	swidget->pipeline_id = index;
	swidget->private = NULL;
	memset(&reply, 0, sizeof(reply));

	dev_dbg(sdev->dev, "tplg: ready widget id %d pipe %d type %d name : %s stream %s\n",
		swidget->comp_id, index, swidget->id, tw->name,
		strnlen(tw->sname, SNDRV_CTL_ELEM_ID_NAME_MAXLEN) > 0
			? tw->sname : "none");

	/* handle any special case widgets */
	switch (w->id) {
	case snd_soc_dapm_dai_in:
	case snd_soc_dapm_dai_out:
		dai = kzalloc(sizeof(*dai), GFP_KERNEL);
		if (!dai) {
			kfree(swidget);
			return -ENOMEM;
		}

		ret = sof_widget_load_dai(scomp, index, swidget, tw, &reply,
					  dai);
		if (ret == 0) {
			sof_connect_dai_widget(scomp, w, tw, dai);
			list_add(&dai->list, &sdev->dai_list);
			swidget->private = dai;
		} else {
			kfree(dai);
		}
		break;
	case snd_soc_dapm_mixer:
		ret = sof_widget_load_mixer(scomp, index, swidget, tw, &reply);
		break;
	case snd_soc_dapm_pga:
		ret = sof_widget_load_pga(scomp, index, swidget, tw, &reply);
		/* Find scontrol for this pga and set readback offset*/
		list_for_each_entry(scontrol, &sdev->kcontrol_list, list) {
			if (scontrol->comp_id == swidget->comp_id) {
				scontrol->readback_offset = reply.offset;
				break;
			}
		}
		break;
	case snd_soc_dapm_buffer:
		ret = sof_widget_load_buffer(scomp, index, swidget, tw, &reply);
		break;
	case snd_soc_dapm_scheduler:
		ret = sof_widget_load_pipeline(scomp, index, swidget, tw,
					       &reply);
		break;
	case snd_soc_dapm_aif_out:
		ret = sof_widget_load_pcm(scomp, index, swidget,
					  SOF_IPC_STREAM_CAPTURE, tw, &reply);
		break;
	case snd_soc_dapm_aif_in:
		ret = sof_widget_load_pcm(scomp, index, swidget,
					  SOF_IPC_STREAM_PLAYBACK, tw, &reply);
		break;
	case snd_soc_dapm_src:
		ret = sof_widget_load_src(scomp, index, swidget, tw, &reply);
		break;
	case snd_soc_dapm_siggen:
		ret = sof_widget_load_siggen(scomp, index, swidget, tw, &reply);
		break;
	case snd_soc_dapm_effect:
		ret = sof_widget_load_effect(scomp, index, swidget, tw, &reply);
		break;
	case snd_soc_dapm_mux:
	case snd_soc_dapm_demux:
	case snd_soc_dapm_switch:
	case snd_soc_dapm_dai_link:
	case snd_soc_dapm_kcontrol:
	default:
		dev_warn(sdev->dev, "warning: widget type %d name %s not handled\n",
			 swidget->id, tw->name);
		break;
	}

	/* check IPC reply */
	if (ret < 0 || reply.rhdr.error < 0) {
		dev_err(sdev->dev,
			"error: DSP failed to add widget id %d type %d name : %s stream %s reply %d\n",
			tw->shift, swidget->id, tw->name,
			strnlen(tw->sname, SNDRV_CTL_ELEM_ID_NAME_MAXLEN) > 0
				? tw->sname : "none", reply.rhdr.error);
		kfree(swidget);
		return ret;
	}

	w->dobj.private = swidget;
	mutex_init(&swidget->mutex);
	list_add(&swidget->list, &sdev->widget_list);
	return ret;
}

static int sof_widget_unload(struct snd_soc_component *scomp,
			     struct snd_soc_dobj *dobj)
{
	const struct snd_kcontrol_new *kc = NULL;
	struct snd_soc_dapm_widget *widget;
	struct snd_sof_control *scontrol;
	struct snd_sof_widget *swidget;
	struct soc_mixer_control *sm;
	struct snd_sof_dai *dai;

	swidget = dobj->private;
	if (!swidget)
		return 0;

	widget = swidget->widget;

	switch (swidget->id) {
	case snd_soc_dapm_dai_in:
	case snd_soc_dapm_dai_out:
		dai = swidget->private;

		/* remove and free dai object */
		if (dai) {
			list_del(&dai->list);
			kfree(dai);
		}
		break;
	case snd_soc_dapm_pga:

		/* get volume kcontrol */
		kc = &widget->kcontrol_news[0];
		sm = (struct soc_mixer_control *)kc->private_value;
		scontrol = sm->dobj.private;

		/* free volume table */
		kfree(scontrol->volume_table);

		/* fallthrough */
	default:
		/* free private value */
		kfree(swidget->private);
		break;
	}

	/* remove and free swidget object */
	list_del(&swidget->list);
	kfree(swidget);

	return 0;
}

/*
 * DAI HW configuration.
 */

/* FE DAI - used for any driver specific init */
static int sof_dai_load(struct snd_soc_component *scomp, int index,
			struct snd_soc_dai_driver *dai_drv,
			struct snd_soc_tplg_pcm *pcm, struct snd_soc_dai *dai)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	struct snd_sof_pcm *spcm;

	/* don't need to do anything for BEs atm */
	if (!pcm)
		return 0;

	spcm = kzalloc(sizeof(*spcm), GFP_KERNEL);
	if (!spcm)
		return -ENOMEM;

	spcm->sdev = sdev;
	spcm->stream[SNDRV_PCM_STREAM_PLAYBACK].comp_id = COMP_ID_UNASSIGNED;
	spcm->stream[SNDRV_PCM_STREAM_CAPTURE].comp_id = COMP_ID_UNASSIGNED;
	if (pcm) {
		spcm->pcm = *pcm;
		dev_dbg(sdev->dev, "tplg: load pcm %s\n", pcm->dai_name);
	}
	dai_drv->dobj.private = spcm;
	mutex_init(&spcm->mutex);
	list_add(&spcm->list, &sdev->pcm_list);

	return 0;
}

static int sof_dai_unload(struct snd_soc_component *scomp,
			  struct snd_soc_dobj *dobj)
{
	struct snd_sof_pcm *spcm = dobj->private;

	list_del(&spcm->list);
	kfree(spcm);

	return 0;
}

static void sof_dai_set_format(struct snd_soc_tplg_hw_config *hw_config,
			       struct sof_ipc_dai_config *config)
{
	/* clock directions wrt codec */
	if (hw_config->bclk_master == SND_SOC_TPLG_BCLK_CM) {
		/* codec is bclk master */
		if (hw_config->fsync_master == SND_SOC_TPLG_FSYNC_CM)
			config->format |= SOF_DAI_FMT_CBM_CFM;
		else
			config->format |= SOF_DAI_FMT_CBM_CFS;
	} else {
		/* codec is bclk slave */
		if (hw_config->fsync_master == SND_SOC_TPLG_FSYNC_CM)
			config->format |= SOF_DAI_FMT_CBS_CFM;
		else
			config->format |= SOF_DAI_FMT_CBS_CFS;
	}

	/* inverted clocks ? */
	if (hw_config->invert_bclk) {
		if (hw_config->invert_fsync)
			config->format |= SOF_DAI_FMT_IB_IF;
		else
			config->format |= SOF_DAI_FMT_IB_NF;
	} else {
		if (hw_config->invert_fsync)
			config->format |= SOF_DAI_FMT_NB_IF;
		else
			config->format |= SOF_DAI_FMT_NB_NF;
	}
}

static int sof_link_ssp_load(struct snd_soc_component *scomp, int index,
			     struct snd_soc_dai_link *link,
			     struct snd_soc_tplg_link_config *cfg,
			     struct snd_soc_tplg_hw_config *hw_config,
			     struct sof_ipc_dai_config *config)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	struct snd_soc_tplg_private *private = &cfg->priv;
	struct sof_ipc_reply reply;
	u32 size = sizeof(*config);
	int ret;

	/* handle master/slave and inverted clocks */
	sof_dai_set_format(hw_config, config);

	/* init IPC */
	memset(&config->ssp, 0, sizeof(struct sof_ipc_dai_ssp_params));
	config->hdr.size = size;

	ret = sof_parse_tokens(scomp, &config->ssp, ssp_tokens,
			       ARRAY_SIZE(ssp_tokens), private->array,
			       le32_to_cpu(private->size));
	if (ret != 0) {
		dev_err(sdev->dev, "error: parse ssp tokens failed %d\n",
			le32_to_cpu(private->size));
		return ret;
	}

	config->ssp.mclk_rate = le32_to_cpu(hw_config->mclk_rate);
	config->ssp.bclk_rate = le32_to_cpu(hw_config->bclk_rate);
	config->ssp.fsync_rate = le32_to_cpu(hw_config->fsync_rate);
	config->ssp.tdm_slots = le32_to_cpu(hw_config->tdm_slots);
	config->ssp.tdm_slot_width = le32_to_cpu(hw_config->tdm_slot_width);
	config->ssp.mclk_direction = hw_config->mclk_direction;
	config->ssp.rx_slots = le32_to_cpu(hw_config->rx_slots);
	config->ssp.tx_slots = le32_to_cpu(hw_config->tx_slots);

	dev_dbg(sdev->dev, "tplg: config SSP%d fmt 0x%x mclk %d bclk %d fclk %d width (%d)%d slots %d mclk id %d\n",
		config->dai_index, config->format,
		config->ssp.mclk_rate, config->ssp.bclk_rate,
		config->ssp.fsync_rate, config->ssp.sample_valid_bits,
		config->ssp.tdm_slot_width, config->ssp.tdm_slots,
		config->ssp.mclk_id);

	/* send message to DSP */
	ret = sof_ipc_tx_message(sdev->ipc,
				 config->hdr.cmd, config, size, &reply,
				 sizeof(reply));

	if (ret < 0)
		dev_err(sdev->dev, "error: failed to set DAI config for SSP%d\n",
			config->dai_index);

	return ret;
}

static int sof_link_dmic_load(struct snd_soc_component *scomp, int index,
			      struct snd_soc_dai_link *link,
			      struct snd_soc_tplg_link_config *cfg,
			      struct snd_soc_tplg_hw_config *hw_config,
			      struct sof_ipc_dai_config *config)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	struct snd_soc_tplg_private *private = &cfg->priv;
	struct sof_ipc_dai_config *ipc_config;
	struct sof_ipc_reply reply;
	u32 size;
	int ret, j;

	memset(&config->dmic, 0, sizeof(struct sof_ipc_dai_dmic_params));

	/* get DMIC tokens */
	ret = sof_parse_tokens(scomp, &config->dmic, dmic_tokens,
			       ARRAY_SIZE(dmic_tokens), private->array,
			       le32_to_cpu(private->size));
	if (ret != 0) {
		dev_err(sdev->dev, "error: parse dmic tokens failed %d\n",
			le32_to_cpu(private->size));
		return ret;
	}

	/*
	 * allocate memory for common dai params, dmic params
	 * and dmic pdm controller params
	 */
	ipc_config = kzalloc(sizeof(*config) +
				sizeof(struct sof_ipc_dai_dmic_pdm_ctrl) *
				config->dmic.num_pdm_active,
			     GFP_KERNEL);
	if (!ipc_config) {
		dev_err(sdev->dev, "error: allocating memory for config\n");
		return -ENOMEM;
	}

	/* copy the common dai config and dmic params */
	memcpy(ipc_config, config, sizeof(*config));

	/*
	 * alloc memory for private member
	 * Used to track the pdm config array index currently being parsed
	 */
	sdev->private = kzalloc(sizeof(u32), GFP_KERNEL);

	/* get DMIC PDM tokens */
	ret = sof_parse_tokens(scomp, &ipc_config->dmic.pdm[0], dmic_pdm_tokens,
			       ARRAY_SIZE(dmic_pdm_tokens), private->array,
			       le32_to_cpu(private->size));
	if (ret != 0) {
		dev_err(sdev->dev, "error: parse dmic pdm tokens failed %d\n",
			le32_to_cpu(private->size));
		kfree(ipc_config);
		return ret;
	}

	/* set IPC header size */
	size = sizeof(*ipc_config);
	ipc_config->hdr.size = size;

	/* debug messages */
	dev_dbg(sdev->dev, "tplg: config DMIC%d driver version %d\n",
		ipc_config->dai_index, ipc_config->dmic.driver_ipc_version);
	dev_dbg(sdev->dev, "pdmclk_min %d pdm_clkmax %d duty_min %hd\n",
		ipc_config->dmic.pdmclk_min, ipc_config->dmic.pdmclk_max,
		ipc_config->dmic.duty_min);
	dev_dbg(sdev->dev, "duty_max %hd fifo_fs %d num_pdms active %d\n",
		ipc_config->dmic.duty_max, ipc_config->dmic.fifo_fs_a,
		ipc_config->dmic.num_pdm_active);
	dev_dbg(sdev->dev, "fifo word length %hd\n",
		ipc_config->dmic.fifo_bits_a);

	for (j = 0; j < ipc_config->dmic.num_pdm_active; j++) {
		dev_dbg(sdev->dev, "pdm %hd mic a %hd mic b %hd\n",
			ipc_config->dmic.pdm[j].id,
			ipc_config->dmic.pdm[j].enable_mic_a,
			ipc_config->dmic.pdm[j].enable_mic_b);
		dev_dbg(sdev->dev, "pdm %hd polarity a %hd polarity b %hd\n",
			ipc_config->dmic.pdm[j].id,
			ipc_config->dmic.pdm[j].polarity_mic_a,
			ipc_config->dmic.pdm[j].polarity_mic_b);
		dev_dbg(sdev->dev, "pdm %hd clk_edge %hd skew %hd\n",
			ipc_config->dmic.pdm[j].id,
			ipc_config->dmic.pdm[j].clk_edge,
			ipc_config->dmic.pdm[j].skew);
	}

	/* TODO: check if fifo_b word length is needed */
	ipc_config->dmic.fifo_bits_b = ipc_config->dmic.fifo_bits_a;

	/* send message to DSP */
	ret = sof_ipc_tx_message(sdev->ipc,
				 ipc_config->hdr.cmd, ipc_config, size, &reply,
				 sizeof(reply));

	if (ret < 0)
		dev_err(sdev->dev, "error: failed to set DAI config for DMIC%d\n",
			config->dai_index);

	/* update config with pdm config */
	memcpy(config, ipc_config, sizeof(*ipc_config));

	kfree(sdev->private);
	kfree(ipc_config);

	return ret;
}

static int sof_link_hda_load(struct snd_soc_component *scomp, int index,
			     struct snd_soc_dai_link *link,
			     struct snd_soc_tplg_link_config *cfg,
			     struct snd_soc_tplg_hw_config *hw_config,
			     struct sof_ipc_dai_config *config)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	struct snd_soc_tplg_private *private = &cfg->priv;
	struct sof_ipc_reply reply;
	u32 size = sizeof(*config);
	int ret;

	/* init IPC */
	memset(&config->hda, 0, sizeof(struct sof_ipc_dai_hda_params));
	config->hdr.size = size;

	/* get any bespoke DAI tokens */
	ret = sof_parse_tokens(scomp, config, hda_tokens,
			       ARRAY_SIZE(hda_tokens), private->array,
			       le32_to_cpu(private->size));
	if (ret != 0) {
		dev_err(sdev->dev, "error: parse hda tokens failed %d\n",
			le32_to_cpu(private->size));
		return ret;
	}

	dev_dbg(sdev->dev, "tplg: config HDA%d fmt 0x%x\n",
		config->dai_index, config->format);

	/* send message to DSP */
	ret = sof_ipc_tx_message(sdev->ipc,
				 config->hdr.cmd, config, size, &reply,
				 sizeof(reply));

	if (ret < 0)
		dev_err(sdev->dev, "error: failed to set DAI config for HDA%d\n",
			config->dai_index);

	return ret;
}

/* DAI link - used for any driver specific init */
static int sof_link_load(struct snd_soc_component *scomp, int index,
			 struct snd_soc_dai_link *link,
			 struct snd_soc_tplg_link_config *cfg)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	struct snd_soc_tplg_private *private = &cfg->priv;
	struct sof_ipc_dai_config config;
	struct snd_soc_tplg_hw_config *hw_config;
	struct snd_sof_dai *dai;
	int ret = 0;

	link->platform_name = "sof-audio";
	link->nonatomic = true;

	/* send BE configurations to DSP */
	if (!link->no_pcm)
		return 0;

	/* usually we use 1 config, but for HDA it may be 0 ATM */
	if (le32_to_cpu(cfg->num_hw_configs) != 1)
		dev_warn(sdev->dev, "warn: unexpected DAI config count %d!\n",
			le32_to_cpu(cfg->num_hw_configs));

	/* check we have some tokens - we need at least DAI type */
	if (le32_to_cpu(private->size) == 0) {
		dev_err(sdev->dev, "error: expected tokens for DAI, none found\n");
		return -EINVAL;
	}

	memset(&config, 0, sizeof(config));

	/* get any common DAI tokens */
	ret = sof_parse_tokens(scomp, &config, dai_link_tokens,
			       ARRAY_SIZE(dai_link_tokens), private->array,
			       le32_to_cpu(private->size));
	if (ret != 0) {
		dev_err(sdev->dev, "error: parse link tokens failed %d\n",
			le32_to_cpu(private->size));
		return ret;
	}

	/* configure dai IPC message */
	hw_config = &cfg->hw_config[0];

	config.hdr.cmd = SOF_IPC_GLB_DAI_MSG | SOF_IPC_DAI_CONFIG;
	config.format = le32_to_cpu(hw_config->fmt);

	/* now load DAI specific data and send IPC - type comes from token */
	switch (config.type) {
	case SOF_DAI_INTEL_SSP:
		ret = sof_link_ssp_load(scomp, index, link, cfg, hw_config,
					&config);
		break;
	case SOF_DAI_INTEL_DMIC:
		ret = sof_link_dmic_load(scomp, index, link, cfg, hw_config,
					 &config);
		break;
	case SOF_DAI_INTEL_HDA:
		ret = sof_link_hda_load(scomp, index, link, cfg, hw_config,
					&config);
		break;
	default:
		dev_err(sdev->dev, "error: invalid DAI type %d\n", config.type);
		ret = -EINVAL;
		break;
	}
	if (ret < 0)
		return ret;

	/* set config for all DAI's with name matching the link name */
	list_for_each_entry(dai, &sdev->dai_list, list) {
		if (!dai->name)
			continue;

		if (strcmp(link->name, dai->name) == 0)
			memcpy(&dai->dai_config, &config,
			       sizeof(struct sof_ipc_dai_config));
	}

	return 0;
}

static int sof_link_unload(struct snd_soc_component *scomp,
			   struct snd_soc_dobj *dobj)
{
	return 0;
}

/* bind PCM ID to host component ID */
static int spcm_bind(struct snd_soc_component *scomp, struct snd_sof_pcm *spcm,
		     const char *host)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	struct snd_sof_widget *host_widget;

	host_widget = snd_sof_find_swidget(sdev, (char *)host);
	if (!host_widget) {
		dev_err(sdev->dev, "error: can't find host component %s\n",
			host);
		return -ENODEV;
	}

	switch (host_widget->id) {
	case snd_soc_dapm_aif_in:
		spcm->stream[SNDRV_PCM_STREAM_PLAYBACK].comp_id =
			host_widget->comp_id;
		break;
	case snd_soc_dapm_aif_out:
		spcm->stream[SNDRV_PCM_STREAM_CAPTURE].comp_id =
			host_widget->comp_id;
		break;
	default:
		dev_err(sdev->dev, "error: host is wrong type %d\n",
			host_widget->id);
		return -EINVAL;
	}

	return 0;
}

/* Used for free route in topology free stage */
static void sof_route_remove(struct snd_soc_dapm_route *route)
{
	if (!route)
		return;

	kfree(route->source);
	kfree(route->sink);
	kfree(route->control);
}

/* DAI link - used for any driver specific init */
static int sof_route_load(struct snd_soc_component *scomp, int index,
			  struct snd_soc_dapm_route *route)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	struct sof_ipc_pipe_comp_connect *connect;
	struct snd_sof_widget *source_swidget, *sink_swidget;
	struct snd_sof_pcm *spcm;
	struct snd_sof_route *sroute;
	struct sof_ipc_reply reply;
	int ret = 0;

	/* allocate memory for sroute and connect */
	sroute = kzalloc(sizeof(*sroute), GFP_KERNEL);
	if (!sroute)
		return -ENOMEM;

	sroute->sdev = sdev;

	connect = kzalloc(sizeof(*connect), GFP_KERNEL);
	if (!connect) {
		kfree(sroute);
		return -ENOMEM;
	}

	connect->hdr.size = sizeof(*connect);
	connect->hdr.cmd = SOF_IPC_GLB_TPLG_MSG | SOF_IPC_TPLG_COMP_CONNECT;

	dev_dbg(sdev->dev, "sink %s control %s source %s\n",
		route->sink, route->control ? route->control : "none",
		route->source);

	/* source component */
	source_swidget = snd_sof_find_swidget(sdev, (char *)route->source);
	if (!source_swidget) {
		/* don't send any routes to DSP that include a driver PCM */
		spcm = snd_sof_find_spcm_name(sdev, (char *)route->source);
		if (spcm) {
			ret = spcm_bind(scomp, spcm, route->sink);
			if (ret < 0)
				goto err;
			return 0;
		}

		dev_err(sdev->dev, "error: source %s not found\n",
			route->source);
		ret = -EINVAL;
		goto err;
	}

	connect->source_id = source_swidget->comp_id;

	/* sink component */
	sink_swidget = snd_sof_find_swidget(sdev, (char *)route->sink);
	if (!sink_swidget) {
		/* don't send any routes to DSP that include a driver PCM */
		spcm = snd_sof_find_spcm_name(sdev, (char *)route->sink);
		if (spcm) {
			ret = spcm_bind(scomp, spcm, route->source);
			if (ret < 0)
				goto err;
			return 0;
		}

		dev_err(sdev->dev, "error: sink %s not found\n",
			route->sink);
		ret = -EINVAL;
		goto err;
	}

	connect->sink_id = sink_swidget->comp_id;

	/* Some virtual routes and widgets may been added in topology for
	 * compatibility. For virtual routes, both sink and source are not
	 * buffer. Since only buffer linked to component is supported by
	 * FW, others are reported as error, add check in route function,
	 * do not send it to FW when both source and sink are not buffer
	 */
	if (source_swidget->id != snd_soc_dapm_buffer &&
	    sink_swidget->id != snd_soc_dapm_buffer) {
		dev_dbg(sdev->dev, "warning: neither Linked source component %s nor sink component %s is of buffer type, ignoring link\n",
			route->source, route->sink);
		ret = 0;
		goto err;
	} else {
		ret = sof_ipc_tx_message(sdev->ipc,
					 connect->hdr.cmd,
					 connect, sizeof(*connect),
					 &reply, sizeof(reply));

		/* check IPC return value */
		if (ret < 0) {
			dev_err(sdev->dev, "error: failed to add route sink %s control %s source %s\n",
				route->sink,
				route->control ? route->control : "none",
				route->source);
			goto err;
		}

		/* check IPC reply */
		if (reply.error < 0) {
			dev_err(sdev->dev, "error: DSP failed to add route sink %s control %s source %s result %d\n",
				route->sink,
				route->control ? route->control : "none",
				route->source, reply.error);
			ret = reply.error;
			goto err;
		}

		sroute->route.source = kstrdup(route->source, GFP_KERNEL);
		if (!sroute->route.source)
			goto err;

		sroute->route.sink = kstrdup(route->sink, GFP_KERNEL);
		if (!sroute->route.sink) {
			kfree(sroute->route.source);
			goto err;
		}

		if (route->control) {
			sroute->route.control = kstrdup(route->control, GFP_KERNEL);
			if (!sroute->route.control) {
				kfree(sroute->route.source);
				kfree(sroute->route.sink);
				goto err;
			}
		}

		sroute->private = connect;

		/* add route to route list */
		list_add(&sroute->list, &sdev->route_list);
	}

	return ret;

err:
	kfree(connect);
	kfree(sroute);
	return ret;
}

static int sof_route_unload(struct snd_soc_component *scomp,
			    struct snd_soc_dobj *dobj)
{
	/* TODO: unload routes when topology is changed */
	return 0;
}

int snd_sof_complete_pipeline(struct snd_sof_dev *sdev,
			      struct snd_sof_widget *swidget)
{
	struct sof_ipc_pipe_ready ready;
	struct sof_ipc_reply reply;
	int ret;

	dev_dbg(sdev->dev, "tplg: complete pipeline %s id %d\n",
		swidget->widget->name, swidget->comp_id);

	memset(&ready, 0, sizeof(ready));
	ready.hdr.size = sizeof(ready);
	ready.hdr.cmd = SOF_IPC_GLB_TPLG_MSG | SOF_IPC_TPLG_PIPE_COMPLETE;
	ready.comp_id = swidget->comp_id;

	ret = sof_ipc_tx_message(sdev->ipc,
				 ready.hdr.cmd, &ready, sizeof(ready), &reply,
				 sizeof(reply));
	if (ret < 0)
		return ret;
	return 1;
}

/* completion - called at completion of firmware loading */
static void sof_complete(struct snd_soc_component *scomp)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	struct snd_sof_widget *swidget;

	/* some widget types require completion notificattion */
	list_for_each_entry(swidget, &sdev->widget_list, list) {
		if (swidget->complete)
			continue;

		switch (swidget->id) {
		case snd_soc_dapm_scheduler:
			swidget->complete =
				snd_sof_complete_pipeline(sdev, swidget);
			break;
		default:
			break;
		}
	}
}

/* manifest - optional to inform component of manifest */
static int sof_manifest(struct snd_soc_component *scomp, int index,
			struct snd_soc_tplg_manifest *man)
{
	/* not currently parsed */
	return 0;
}

/* vendor specific kcontrol handlers available for binding */
static const struct snd_soc_tplg_kcontrol_ops sof_io_ops[] = {
	{SOF_TPLG_KCTL_VOL_ID, snd_sof_volume_get, snd_sof_volume_put},
	{SOF_TPLG_KCTL_ENUM_ID, snd_sof_enum_get, snd_sof_enum_put},
	{SOF_TPLG_KCTL_BYTES_ID, snd_sof_bytes_get, snd_sof_bytes_put},
};

/* vendor specific bytes ext handlers available for binding */
static const struct snd_soc_tplg_bytes_ext_ops sof_bytes_ext_ops[] = {
	{SOF_TPLG_KCTL_BYTES_ID, snd_sof_bytes_ext_get, snd_sof_bytes_ext_put},
};

static struct snd_soc_tplg_ops sof_tplg_ops = {
	/* external kcontrol init - used for any driver specific init */
	.control_load	= sof_control_load,
	.control_unload	= sof_control_unload,

	/* external kcontrol init - used for any driver specific init */
	.dapm_route_load	= sof_route_load,
	.dapm_route_unload	= sof_route_unload,

	/* external widget init - used for any driver specific init */
	.widget_load	= sof_widget_load,
	.widget_ready	= sof_widget_ready,
	.widget_unload	= sof_widget_unload,

	/* FE DAI - used for any driver specific init */
	.dai_load	= sof_dai_load,
	.dai_unload	= sof_dai_unload,

	/* DAI link - used for any driver specific init */
	.link_load	= sof_link_load,
	.link_unload	= sof_link_unload,

	/* completion - called at completion of firmware loading */
	.complete	= sof_complete,

	/* manifest - optional to inform component of manifest */
	.manifest	= sof_manifest,

	/* vendor specific kcontrol handlers available for binding */
	.io_ops		= sof_io_ops,
	.io_ops_count	= ARRAY_SIZE(sof_io_ops),

	/* vendor specific bytes ext handlers available for binding */
	.bytes_ext_ops	= sof_bytes_ext_ops,
	.bytes_ext_ops_count	= ARRAY_SIZE(sof_bytes_ext_ops),
};

int snd_sof_init_topology(struct snd_sof_dev *sdev,
			  struct snd_soc_tplg_ops *ops)
{
	/* TODO: support linked list of topologies */
	sdev->tplg_ops = ops;
	return 0;
}
EXPORT_SYMBOL(snd_sof_init_topology);

int snd_sof_load_topology(struct snd_sof_dev *sdev, const char *file)
{
	const struct firmware *fw;
	struct snd_soc_tplg_hdr *hdr;
	int ret;

	dev_dbg(sdev->dev, "loading topology:%s\n", file);

	ret = request_firmware(&fw, file, sdev->dev);
	if (ret < 0) {
		dev_err(sdev->dev, "error: tplg %s load failed with %d\n",
			file, ret);
		return ret;
	}

	hdr = (struct snd_soc_tplg_hdr *)fw->data;
	ret = snd_soc_tplg_component_load(sdev->component,
					  &sof_tplg_ops, fw,
					  SND_SOC_TPLG_INDEX_ALL);
	if (ret < 0) {
		dev_err(sdev->dev, "error: tplg component load failed %d\n",
			ret);
		ret = -EINVAL;
	}

	release_firmware(fw);
	return ret;
}
EXPORT_SYMBOL(snd_sof_load_topology);

void snd_sof_free_topology(struct snd_sof_dev *sdev)
{
	struct snd_soc_dapm_context *dapm =
		snd_soc_component_get_dapm(sdev->component);
	struct snd_sof_route *sroute, *temp;
	int ret;

	dev_dbg(sdev->dev, "free topology...\n");

	/* remove routes */
	list_for_each_entry_safe(sroute, temp, &sdev->route_list, list) {

		/* delete dapm route */
		snd_soc_dapm_del_routes(dapm, &sroute->route, 1);

		sof_route_remove(&sroute->route);

		/* free sroute and its private data */
		kfree(sroute->private);
		kfree(sroute);
	}

	ret = snd_soc_tplg_component_remove(sdev->component,
					    SND_SOC_TPLG_INDEX_ALL);
	if (ret < 0)
		dev_err(sdev->dev,
			"error: tplg component free failed %d\n", ret);
}
EXPORT_SYMBOL(snd_sof_free_topology);
