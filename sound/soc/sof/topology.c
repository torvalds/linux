// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2018 Intel Corporation. All rights reserved.
//
// Author: Liam Girdwood <liam.r.girdwood@linux.intel.com>
//

#include <linux/firmware.h>
#include <sound/tlv.h>
#include <sound/pcm_params.h>
#include <uapi/sound/sof/tokens.h>
#include "sof-priv.h"
#include "ops.h"

#define COMP_ID_UNASSIGNED		0xffffffff
/*
 * Constants used in the computation of linear volume gain
 * from dB gain 20th root of 10 in Q1.16 fixed-point notation
 */
#define VOL_TWENTIETH_ROOT_OF_TEN	73533
/* 40th root of 10 in Q1.16 fixed-point notation*/
#define VOL_FORTIETH_ROOT_OF_TEN	69419
/*
 * Volume fractional word length define to 16 sets
 * the volume linear gain value to use Qx.16 format
 */
#define VOLUME_FWL	16
/* 0.5 dB step value in topology TLV */
#define VOL_HALF_DB_STEP	50
/* Full volume for default values */
#define VOL_ZERO_DB	BIT(VOLUME_FWL)

/* TLV data items */
#define TLV_ITEMS	3
#define TLV_MIN		0
#define TLV_STEP	1
#define TLV_MUTE	2

/* size of tplg abi in byte */
#define SOF_TPLG_ABI_SIZE 3

/* send pcm params ipc */
static int ipc_pcm_params(struct snd_sof_widget *swidget, int dir)
{
	struct sof_ipc_pcm_params_reply ipc_params_reply;
	struct snd_sof_dev *sdev = swidget->sdev;
	struct sof_ipc_pcm_params pcm;
	struct snd_pcm_hw_params *params;
	struct snd_sof_pcm *spcm;
	int ret = 0;

	memset(&pcm, 0, sizeof(pcm));

	/* get runtime PCM params using widget's stream name */
	spcm = snd_sof_find_spcm_name(sdev, swidget->widget->sname);
	if (!spcm) {
		dev_err(sdev->dev, "error: cannot find PCM for %s\n",
			swidget->widget->name);
		return -EINVAL;
	}

	params = &spcm->params[dir];

	/* set IPC PCM params */
	pcm.hdr.size = sizeof(pcm);
	pcm.hdr.cmd = SOF_IPC_GLB_STREAM_MSG | SOF_IPC_STREAM_PCM_PARAMS;
	pcm.comp_id = swidget->comp_id;
	pcm.params.hdr.size = sizeof(pcm.params);
	pcm.params.direction = dir;
	pcm.params.sample_valid_bytes = params_width(params) >> 3;
	pcm.params.buffer_fmt = SOF_IPC_BUFFER_INTERLEAVED;
	pcm.params.rate = params_rate(params);
	pcm.params.channels = params_channels(params);
	pcm.params.host_period_bytes = params_period_bytes(params);

	/* set format */
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16:
		pcm.params.frame_fmt = SOF_IPC_FRAME_S16_LE;
		break;
	case SNDRV_PCM_FORMAT_S24:
		pcm.params.frame_fmt = SOF_IPC_FRAME_S24_4LE;
		break;
	case SNDRV_PCM_FORMAT_S32:
		pcm.params.frame_fmt = SOF_IPC_FRAME_S32_LE;
		break;
	default:
		return -EINVAL;
	}

	/* send IPC to the DSP */
	ret = sof_ipc_tx_message(sdev->ipc, pcm.hdr.cmd, &pcm, sizeof(pcm),
				 &ipc_params_reply, sizeof(ipc_params_reply));
	if (ret < 0)
		dev_err(sdev->dev, "error: pcm params failed for %s\n",
			swidget->widget->name);

	return ret;
}

 /* send stream trigger ipc */
static int ipc_trigger(struct snd_sof_widget *swidget, int cmd)
{
	struct snd_sof_dev *sdev = swidget->sdev;
	struct sof_ipc_stream stream;
	struct sof_ipc_reply reply;
	int ret = 0;

	/* set IPC stream params */
	stream.hdr.size = sizeof(stream);
	stream.hdr.cmd = SOF_IPC_GLB_STREAM_MSG | cmd;
	stream.comp_id = swidget->comp_id;

	/* send IPC to the DSP */
	ret = sof_ipc_tx_message(sdev->ipc, stream.hdr.cmd, &stream,
				 sizeof(stream), &reply, sizeof(reply));
	if (ret < 0)
		dev_err(sdev->dev, "error: failed to trigger %s\n",
			swidget->widget->name);

	return ret;
}

static int sof_keyword_dapm_event(struct snd_soc_dapm_widget *w,
				  struct snd_kcontrol *k, int event)
{
	struct snd_sof_widget *swidget = w->dobj.private;
	struct snd_sof_dev *sdev;
	int ret = 0;

	if (!swidget)
		return 0;

	sdev = swidget->sdev;

	dev_dbg(sdev->dev, "received event %d for widget %s\n",
		event, w->name);

	/* process events */
	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		/* set pcm params */
		ret = ipc_pcm_params(swidget, SOF_IPC_STREAM_CAPTURE);
		if (ret < 0) {
			dev_err(sdev->dev,
				"error: failed to set pcm params for widget %s\n",
				swidget->widget->name);
			break;
		}

		/* start trigger */
		ret = ipc_trigger(swidget, SOF_IPC_STREAM_TRIG_START);
		if (ret < 0)
			dev_err(sdev->dev,
				"error: failed to trigger widget %s\n",
				swidget->widget->name);
		break;
	case SND_SOC_DAPM_POST_PMD:
		/* stop trigger */
		ret = ipc_trigger(swidget, SOF_IPC_STREAM_TRIG_STOP);
		if (ret < 0)
			dev_err(sdev->dev,
				"error: failed to trigger widget %s\n",
				swidget->widget->name);

		/* pcm free */
		ret = ipc_trigger(swidget, SOF_IPC_STREAM_PCM_FREE);
		if (ret < 0)
			dev_err(sdev->dev,
				"error: failed to trigger widget %s\n",
				swidget->widget->name);
		break;
	default:
		break;
	}

	return ret;
}

/* event handlers for keyword detect component */
static const struct snd_soc_tplg_widget_events sof_kwd_events[] = {
	{SOF_KEYWORD_DETECT_DAPM_EVENT, sof_keyword_dapm_event},
};

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

/*
 * Function to truncate an unsigned 64-bit number
 * by x bits and return 32-bit unsigned number. This
 * function also takes care of rounding while truncating
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

/*
 * Function to compute a ^ exp where,
 * a is a fractional number represented by a fixed-point
 * integer with a fractional world length of "fwl"
 * exp is an integer
 * fwl is the fractional word length
 * Return value is a fractional number represented by a
 * fixed-point integer with a fractional word length of "fwl"
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
		/*
		 * Product of 2 Qx.fwl fixed-point numbers yields a Q2*x.2*fwl
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

/*
 * Function to calculate volume gain from TLV data.
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

	/*
	 * compute dB gain from tlv. tlv_step
	 * in topology is multiplied by 100
	 */
	dB_gain = tlv[TLV_MIN] + (value * tlv[TLV_STEP]) / 100;

	/*
	 * compute linear gain represented by fixed-point
	 * int with VOLUME_FWL fractional bits
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

/*
 * Set up volume table for kcontrols from tlv data
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

struct sof_process_types {
	const char *name;
	enum sof_ipc_process_type type;
	enum sof_comp_type comp_type;
};

static const struct sof_process_types sof_process[] = {
	{"EQFIR", SOF_PROCESS_EQFIR, SOF_COMP_EQ_FIR},
	{"EQIIR", SOF_PROCESS_EQIIR, SOF_COMP_EQ_IIR},
	{"KEYWORD_DETECT", SOF_PROCESS_KEYWORD_DETECT, SOF_COMP_KEYWORD_DETECT},
	{"KPB", SOF_PROCESS_KPB, SOF_COMP_KPB},
	{"CHAN_SELECTOR", SOF_PROCESS_CHAN_SELECTOR, SOF_COMP_SELECTOR},
	{"MUX", SOF_PROCESS_MUX, SOF_COMP_MUX},
	{"DEMUX", SOF_PROCESS_DEMUX, SOF_COMP_DEMUX},
};

static enum sof_ipc_process_type find_process(const char *name)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(sof_process); i++) {
		if (strcmp(name, sof_process[i].name) == 0)
			return sof_process[i].type;
	}

	return SOF_PROCESS_NONE;
}

static enum sof_comp_type find_process_comp_type(enum sof_ipc_process_type type)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(sof_process); i++) {
		if (sof_process[i].type == type)
			return sof_process[i].comp_type;
	}

	return SOF_COMP_NONE;
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
		container_of(hdr, struct snd_soc_tplg_mixer_control, hdr);
	struct sof_ipc_ctrl_data *cdata;
	int tlv[TLV_ITEMS];
	unsigned int i;
	int ret;

	/* validate topology data */
	if (le32_to_cpu(mc->num_channels) > SND_SOC_TPLG_MAX_CHAN)
		return -EINVAL;

	/* init the volume get/put data */
	scontrol->size = struct_size(scontrol->control_data, chanv,
				     le32_to_cpu(mc->num_channels));
	scontrol->control_data = kzalloc(scontrol->size, GFP_KERNEL);
	if (!scontrol->control_data)
		return -ENOMEM;

	scontrol->comp_id = sdev->next_comp_id;
	scontrol->min_volume_step = le32_to_cpu(mc->min);
	scontrol->max_volume_step = le32_to_cpu(mc->max);
	scontrol->num_channels = le32_to_cpu(mc->num_channels);

	/* set cmd for mixer control */
	if (le32_to_cpu(mc->max) == 1) {
		scontrol->cmd = SOF_CTRL_CMD_SWITCH;
		goto out;
	}

	scontrol->cmd = SOF_CTRL_CMD_VOLUME;

	/* extract tlv data */
	if (get_tlv_data(kc->tlv.p, tlv) < 0) {
		dev_err(sdev->dev, "error: invalid TLV data\n");
		return -EINVAL;
	}

	/* set up volume table */
	ret = set_up_volume_table(scontrol, tlv, le32_to_cpu(mc->max) + 1);
	if (ret < 0) {
		dev_err(sdev->dev, "error: setting up volume table\n");
		return ret;
	}

	/* set default volume values to 0dB in control */
	cdata = scontrol->control_data;
	for (i = 0; i < scontrol->num_channels; i++) {
		cdata->chanv[i].channel = i;
		cdata->chanv[i].value = VOL_ZERO_DB;
	}

out:
	dev_dbg(sdev->dev, "tplg: load kcontrol index %d chans %d\n",
		scontrol->comp_id, scontrol->num_channels);

	return 0;
}

static int sof_control_load_enum(struct snd_soc_component *scomp,
				 struct snd_sof_control *scontrol,
				 struct snd_kcontrol_new *kc,
				 struct snd_soc_tplg_ctl_hdr *hdr)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	struct snd_soc_tplg_enum_control *ec =
		container_of(hdr, struct snd_soc_tplg_enum_control, hdr);

	/* validate topology data */
	if (le32_to_cpu(ec->num_channels) > SND_SOC_TPLG_MAX_CHAN)
		return -EINVAL;

	/* init the enum get/put data */
	scontrol->size = struct_size(scontrol->control_data, chanv,
				     le32_to_cpu(ec->num_channels));
	scontrol->control_data = kzalloc(scontrol->size, GFP_KERNEL);
	if (!scontrol->control_data)
		return -ENOMEM;

	scontrol->comp_id = sdev->next_comp_id;
	scontrol->num_channels = le32_to_cpu(ec->num_channels);

	scontrol->cmd = SOF_CTRL_CMD_ENUM;

	dev_dbg(sdev->dev, "tplg: load kcontrol index %d chans %d comp_id %d\n",
		scontrol->comp_id, scontrol->num_channels, scontrol->comp_id);

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
		container_of(hdr, struct snd_soc_tplg_bytes_control, hdr);
	struct soc_bytes_ext *sbe = (struct soc_bytes_ext *)kc->private_value;
	int max_size = sbe->max;

	if (le32_to_cpu(control->priv.size) > max_size) {
		dev_err(sdev->dev, "err: bytes data size %d exceeds max %d.\n",
			control->priv.size, max_size);
		return -EINVAL;
	}

	/* init the get/put bytes data */
	scontrol->size = sizeof(struct sof_ipc_ctrl_data) +
		le32_to_cpu(control->priv.size);
	scontrol->control_data = kzalloc(max_size, GFP_KERNEL);
	cdata = scontrol->control_data;
	if (!scontrol->control_data)
		return -ENOMEM;

	scontrol->comp_id = sdev->next_comp_id;
	scontrol->cmd = SOF_CTRL_CMD_BINARY;

	dev_dbg(sdev->dev, "tplg: load kcontrol index %d chans %d\n",
		scontrol->comp_id, scontrol->num_channels);

	if (le32_to_cpu(control->priv.size) > 0) {
		memcpy(cdata->data, control->priv.data,
		       le32_to_cpu(control->priv.size));

		if (cdata->data->magic != SOF_ABI_MAGIC) {
			dev_err(sdev->dev, "error: Wrong ABI magic 0x%08x.\n",
				cdata->data->magic);
			return -EINVAL;
		}
		if (SOF_ABI_VERSION_INCOMPATIBLE(SOF_ABI_VERSION,
						 cdata->data->abi)) {
			dev_err(sdev->dev,
				"error: Incompatible ABI version 0x%08x.\n",
				cdata->data->abi);
			return -EINVAL;
		}
		if (cdata->data->size + sizeof(const struct sof_abi_hdr) !=
		    le32_to_cpu(control->priv.size)) {
			dev_err(sdev->dev,
				"error: Conflict in bytes vs. priv size.\n");
			return -EINVAL;
		}
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
	u32 *val = (u32 *)((u8 *)object + offset);

	*val = le32_to_cpu(velem->value);
	return 0;
}

static int get_token_u16(void *elem, void *object, u32 offset, u32 size)
{
	struct snd_soc_tplg_vendor_value_elem *velem = elem;
	u16 *val = (u16 *)((u8 *)object + offset);

	*val = (u16)le32_to_cpu(velem->value);
	return 0;
}

static int get_token_comp_format(void *elem, void *object, u32 offset, u32 size)
{
	struct snd_soc_tplg_vendor_string_elem *velem = elem;
	u32 *val = (u32 *)((u8 *)object + offset);

	*val = find_format(velem->string);
	return 0;
}

static int get_token_dai_type(void *elem, void *object, u32 offset, u32 size)
{
	struct snd_soc_tplg_vendor_string_elem *velem = elem;
	u32 *val = (u32 *)((u8 *)object + offset);

	*val = find_dai(velem->string);
	return 0;
}

static int get_token_process_type(void *elem, void *object, u32 offset,
				  u32 size)
{
	struct snd_soc_tplg_vendor_string_elem *velem = elem;
	u32 *val = (u32 *)((u8 *)object + offset);

	*val = find_process(velem->string);
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
	{SOF_TKN_DAI_TYPE, SND_SOC_TPLG_TUPLE_TYPE_STRING, get_token_dai_type,
		offsetof(struct sof_ipc_comp_dai, type), 0},
	{SOF_TKN_DAI_INDEX, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc_comp_dai, dai_index), 0},
	{SOF_TKN_DAI_DIRECTION, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc_comp_dai, direction), 0},
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
	{SOF_TKN_SCHED_PERIOD, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc_pipe_new, period), 0},
	{SOF_TKN_SCHED_PRIORITY, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc_pipe_new, priority), 0},
	{SOF_TKN_SCHED_MIPS, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc_pipe_new, period_mips), 0},
	{SOF_TKN_SCHED_CORE, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc_pipe_new, core), 0},
	{SOF_TKN_SCHED_FRAMES, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc_pipe_new, frames_per_sched), 0},
	{SOF_TKN_SCHED_TIME_DOMAIN, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc_pipe_new, time_domain), 0},
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
static const struct sof_topology_token process_tokens[] = {
	{SOF_TKN_PROCESS_TYPE, SND_SOC_TPLG_TUPLE_TYPE_STRING,
		get_token_process_type,
		offsetof(struct sof_ipc_comp_process, type), 0},
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
};

/* SSP */
static const struct sof_topology_token ssp_tokens[] = {
	{SOF_TKN_INTEL_SSP_CLKS_CONTROL,
		SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc_dai_ssp_params, clks_control), 0},
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
	{SOF_TKN_INTEL_SSP_BCLK_DELAY, SND_SOC_TPLG_TUPLE_TYPE_WORD,
		get_token_u32,
		offsetof(struct sof_ipc_dai_ssp_params, bclk_delay), 0},

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
		offsetof(struct sof_ipc_dai_dmic_params, fifo_fs), 0},
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
		offsetof(struct sof_ipc_dai_dmic_params, fifo_bits), 0},
	{SOF_TKN_INTEL_DMIC_UNMUTE_RAMP_TIME_MS,
		SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc_dai_dmic_params, unmute_ramp_time), 0},

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
				index = sdev->private;

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
		array = (struct snd_soc_tplg_vendor_array *)((u8 *)array
			+ asize);
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
	struct soc_bytes_ext *sbe;
	struct soc_enum *se;
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	struct snd_soc_dobj *dobj;
	struct snd_sof_control *scontrol;
	int ret = -EINVAL;

	dev_dbg(sdev->dev, "tplg: load control type %d name : %s\n",
		hdr->type, hdr->name);

	scontrol = kzalloc(sizeof(*scontrol), GFP_KERNEL);
	if (!scontrol)
		return -ENOMEM;

	scontrol->sdev = sdev;

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
		se = (struct soc_enum *)kc->private_value;
		dobj = &se->dobj;
		ret = sof_control_load_enum(scomp, scontrol, kc, hdr);
		break;
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

	kfree(scontrol->control_data);
	list_del(&scontrol->list);
	kfree(scontrol);
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
		dev_vdbg(sdev->dev, "tplg: check widget: %s stream: %s dai stream: %s\n",
			 w->name,  w->sname, rtd->dai_link->stream_name);

		if (!w->sname || !rtd->dai_link->stream_name)
			continue;

		/* does stream match DAI link ? */
		if (strcmp(w->sname, rtd->dai_link->stream_name))
			continue;

		switch (w->id) {
		case snd_soc_dapm_dai_out:
			rtd->cpu_dai->capture_widget = w;
			dai->name = rtd->dai_link->name;
			dev_dbg(sdev->dev, "tplg: connected widget %s -> DAI link %s\n",
				w->name, rtd->dai_link->name);
			break;
		case snd_soc_dapm_dai_in:
			rtd->cpu_dai->playback_widget = w;
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
	comp_dai.config.hdr.size = sizeof(comp_dai.config);

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

	swidget->private = buffer;

	ret = sof_ipc_tx_message(sdev->ipc, buffer->comp.hdr.cmd, buffer,
				 sizeof(*buffer), r, sizeof(*r));
	if (ret < 0) {
		dev_err(sdev->dev, "error: buffer %s load failed\n",
			swidget->widget->name);
		kfree(buffer);
	}

	return ret;
}

/* bind PCM ID to host component ID */
static int spcm_bind(struct snd_sof_dev *sdev, struct snd_sof_pcm *spcm,
		     int dir)
{
	struct snd_sof_widget *host_widget;

	host_widget = snd_sof_find_swidget_sname(sdev,
						 spcm->pcm.caps[dir].name,
						 dir);
	if (!host_widget) {
		dev_err(sdev->dev, "can't find host comp to bind pcm\n");
		return -EINVAL;
	}

	spcm->stream[dir].comp_id = host_widget->comp_id;

	return 0;
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

	/* configure host comp IPC message */
	host->comp.hdr.size = sizeof(*host);
	host->comp.hdr.cmd = SOF_IPC_GLB_TPLG_MSG | SOF_IPC_TPLG_COMP_NEW;
	host->comp.id = swidget->comp_id;
	host->comp.type = SOF_COMP_HOST;
	host->comp.pipeline_id = index;
	host->direction = dir;
	host->config.hdr.size = sizeof(host->config);

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

	swidget->private = host;

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
int sof_load_pipeline_ipc(struct snd_sof_dev *sdev,
			  struct sof_ipc_pipe_new *pipeline,
			  struct sof_ipc_comp_reply *r)
{
	struct sof_ipc_pm_core_config pm_core_config;
	int ret;

	ret = sof_ipc_tx_message(sdev->ipc, pipeline->hdr.cmd, pipeline,
				 sizeof(*pipeline), r, sizeof(*r));
	if (ret < 0) {
		dev_err(sdev->dev, "error: load pipeline ipc failure\n");
		return ret;
	}

	/* power up the core that this pipeline is scheduled on */
	ret = snd_sof_dsp_core_power_up(sdev, 1 << pipeline->core);
	if (ret < 0) {
		dev_err(sdev->dev, "error: powering up pipeline schedule core %d\n",
			pipeline->core);
		return ret;
	}

	/* update enabled cores mask */
	sdev->enabled_cores_mask |= 1 << pipeline->core;

	/*
	 * Now notify DSP that the core that this pipeline is scheduled on
	 * has been powered up
	 */
	memset(&pm_core_config, 0, sizeof(pm_core_config));
	pm_core_config.enable_mask = sdev->enabled_cores_mask;

	/* configure CORE_ENABLE ipc message */
	pm_core_config.hdr.size = sizeof(pm_core_config);
	pm_core_config.hdr.cmd = SOF_IPC_GLB_PM_MSG | SOF_IPC_PM_CORE_ENABLE;

	/* send ipc */
	ret = sof_ipc_tx_message(sdev->ipc, pm_core_config.hdr.cmd,
				 &pm_core_config, sizeof(pm_core_config),
				 &pm_core_config, sizeof(pm_core_config));
	if (ret < 0)
		dev_err(sdev->dev, "error: core enable ipc failure\n");

	return ret;
}

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

	dev_dbg(sdev->dev, "pipeline %s: period %d pri %d mips %d core %d frames %d\n",
		swidget->widget->name, pipeline->period, pipeline->priority,
		pipeline->period_mips, pipeline->core, pipeline->frames_per_sched);

	swidget->private = pipeline;

	/* send ipc's to create pipeline comp and power up schedule core */
	ret = sof_load_pipeline_ipc(sdev, pipeline, r);
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
	mixer->config.hdr.size = sizeof(mixer->config);

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

	swidget->private = mixer;

	ret = sof_ipc_tx_message(sdev->ipc, mixer->comp.hdr.cmd, mixer,
				 sizeof(*mixer), r, sizeof(*r));
	if (ret < 0)
		kfree(mixer);

	return ret;
}

/*
 * Mux topology
 */
static int sof_widget_load_mux(struct snd_soc_component *scomp, int index,
			       struct snd_sof_widget *swidget,
			       struct snd_soc_tplg_dapm_widget *tw,
			       struct sof_ipc_comp_reply *r)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	struct snd_soc_tplg_private *private = &tw->priv;
	struct sof_ipc_comp_mux *mux;
	int ret;

	mux = kzalloc(sizeof(*mux), GFP_KERNEL);
	if (!mux)
		return -ENOMEM;

	/* configure mux IPC message */
	mux->comp.hdr.size = sizeof(*mux);
	mux->comp.hdr.cmd = SOF_IPC_GLB_TPLG_MSG | SOF_IPC_TPLG_COMP_NEW;
	mux->comp.id = swidget->comp_id;
	mux->comp.type = SOF_COMP_MUX;
	mux->comp.pipeline_id = index;
	mux->config.hdr.size = sizeof(mux->config);

	ret = sof_parse_tokens(scomp, &mux->config, comp_tokens,
			       ARRAY_SIZE(comp_tokens), private->array,
			       le32_to_cpu(private->size));
	if (ret != 0) {
		dev_err(sdev->dev, "error: parse mux.cfg tokens failed %d\n",
			private->size);
		kfree(mux);
		return ret;
	}

	sof_dbg_comp_config(scomp, &mux->config);

	swidget->private = mux;

	ret = sof_ipc_tx_message(sdev->ipc, mux->comp.hdr.cmd, mux,
				 sizeof(*mux), r, sizeof(*r));
	if (ret < 0)
		kfree(mux);

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
	struct snd_sof_control *scontrol;
	int min_step;
	int max_step;
	int ret;

	volume = kzalloc(sizeof(*volume), GFP_KERNEL);
	if (!volume)
		return -ENOMEM;

	if (le32_to_cpu(tw->num_kcontrols) != 1) {
		dev_err(sdev->dev, "error: invalid kcontrol count %d for volume\n",
			tw->num_kcontrols);
		ret = -EINVAL;
		goto err;
	}

	/* configure volume IPC message */
	volume->comp.hdr.size = sizeof(*volume);
	volume->comp.hdr.cmd = SOF_IPC_GLB_TPLG_MSG | SOF_IPC_TPLG_COMP_NEW;
	volume->comp.id = swidget->comp_id;
	volume->comp.type = SOF_COMP_VOLUME;
	volume->comp.pipeline_id = index;
	volume->config.hdr.size = sizeof(volume->config);

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

	swidget->private = volume;

	list_for_each_entry(scontrol, &sdev->kcontrol_list, list) {
		if (scontrol->comp_id == swidget->comp_id) {
			min_step = scontrol->min_volume_step;
			max_step = scontrol->max_volume_step;
			volume->min_value = scontrol->volume_table[min_step];
			volume->max_value = scontrol->volume_table[max_step];
			volume->channels = scontrol->num_channels;
			break;
		}
	}

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

	/* configure src IPC message */
	src->comp.hdr.size = sizeof(*src);
	src->comp.hdr.cmd = SOF_IPC_GLB_TPLG_MSG | SOF_IPC_TPLG_COMP_NEW;
	src->comp.id = swidget->comp_id;
	src->comp.type = SOF_COMP_SRC;
	src->comp.pipeline_id = index;
	src->config.hdr.size = sizeof(src->config);

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

	swidget->private = src;

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

	/* configure siggen IPC message */
	tone->comp.hdr.size = sizeof(*tone);
	tone->comp.hdr.cmd = SOF_IPC_GLB_TPLG_MSG | SOF_IPC_TPLG_COMP_NEW;
	tone->comp.id = swidget->comp_id;
	tone->comp.type = SOF_COMP_TONE;
	tone->comp.pipeline_id = index;
	tone->config.hdr.size = sizeof(tone->config);

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

	swidget->private = tone;

	ret = sof_ipc_tx_message(sdev->ipc, tone->comp.hdr.cmd, tone,
				 sizeof(*tone), r, sizeof(*r));
	if (ret >= 0)
		return ret;
err:
	kfree(tone);
	return ret;
}

static int sof_process_load(struct snd_soc_component *scomp, int index,
			    struct snd_sof_widget *swidget,
			    struct snd_soc_tplg_dapm_widget *tw,
			    struct sof_ipc_comp_reply *r,
			    int type)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	struct snd_soc_tplg_private *private = &tw->priv;
	struct snd_soc_dapm_widget *widget = swidget->widget;
	const struct snd_kcontrol_new *kc;
	struct soc_bytes_ext *sbe;
	struct soc_mixer_control *sm;
	struct soc_enum *se;
	struct snd_sof_control *scontrol = NULL;
	struct sof_abi_hdr *pdata = NULL;
	struct sof_ipc_comp_process *process;
	size_t ipc_size, ipc_data_size = 0;
	int ret, i, offset = 0;

	if (type == SOF_COMP_NONE) {
		dev_err(sdev->dev, "error: invalid process comp type %d\n",
			type);
		return -EINVAL;
	}

	/*
	 * get possible component controls - get size of all pdata,
	 * then memcpy with headers
	 */
	for (i = 0; i < widget->num_kcontrols; i++) {

		kc = &widget->kcontrol_news[i];

		switch (widget->dobj.widget.kcontrol_type) {
		case SND_SOC_TPLG_TYPE_MIXER:
			sm = (struct soc_mixer_control *)kc->private_value;
			scontrol = sm->dobj.private;
			break;
		case SND_SOC_TPLG_TYPE_BYTES:
			sbe = (struct soc_bytes_ext *)kc->private_value;
			scontrol = sbe->dobj.private;
			break;
		case SND_SOC_TPLG_TYPE_ENUM:
			se = (struct soc_enum *)kc->private_value;
			scontrol = se->dobj.private;
			break;
		default:
			dev_err(sdev->dev, "error: unknown kcontrol type %d in widget %s\n",
				widget->dobj.widget.kcontrol_type,
				widget->name);
			return -EINVAL;
		}

		if (!scontrol) {
			dev_err(sdev->dev, "error: no scontrol for widget %s\n",
				widget->name);
			return -EINVAL;
		}

		/* don't include if no private data */
		pdata = scontrol->control_data->data;
		if (!pdata)
			continue;

		/* make sure data is valid - data can be updated at runtime */
		if (pdata->magic != SOF_ABI_MAGIC)
			continue;

		ipc_data_size += pdata->size;
	}

	ipc_size = sizeof(struct sof_ipc_comp_process) +
		le32_to_cpu(private->size) +
		ipc_data_size;

	process = kzalloc(ipc_size, GFP_KERNEL);
	if (!process)
		return -ENOMEM;

	/* configure iir IPC message */
	process->comp.hdr.size = ipc_size;
	process->comp.hdr.cmd = SOF_IPC_GLB_TPLG_MSG | SOF_IPC_TPLG_COMP_NEW;
	process->comp.id = swidget->comp_id;
	process->comp.type = type;
	process->comp.pipeline_id = index;
	process->config.hdr.size = sizeof(process->config);

	ret = sof_parse_tokens(scomp, &process->config, comp_tokens,
			       ARRAY_SIZE(comp_tokens), private->array,
			       le32_to_cpu(private->size));
	if (ret != 0) {
		dev_err(sdev->dev, "error: parse process.cfg tokens failed %d\n",
			le32_to_cpu(private->size));
		goto err;
	}

	sof_dbg_comp_config(scomp, &process->config);

	/*
	 * found private data in control, so copy it.
	 * get possible component controls - get size of all pdata,
	 * then memcpy with headers
	 */
	for (i = 0; i < widget->num_kcontrols; i++) {
		kc = &widget->kcontrol_news[i];

		switch (widget->dobj.widget.kcontrol_type) {
		case SND_SOC_TPLG_TYPE_MIXER:
			sm = (struct soc_mixer_control *)kc->private_value;
			scontrol = sm->dobj.private;
			break;
		case SND_SOC_TPLG_TYPE_BYTES:
			sbe = (struct soc_bytes_ext *)kc->private_value;
			scontrol = sbe->dobj.private;
			break;
		case SND_SOC_TPLG_TYPE_ENUM:
			se = (struct soc_enum *)kc->private_value;
			scontrol = se->dobj.private;
			break;
		default:
			dev_err(sdev->dev, "error: unknown kcontrol type %d in widget %s\n",
				widget->dobj.widget.kcontrol_type,
				widget->name);
			return -EINVAL;
		}

		/* don't include if no private data */
		pdata = scontrol->control_data->data;
		if (!pdata)
			continue;

		/* make sure data is valid - data can be updated at runtime */
		if (pdata->magic != SOF_ABI_MAGIC)
			continue;

		memcpy(&process->data + offset, pdata->data, pdata->size);
		offset += pdata->size;
	}

	process->size = ipc_data_size;
	swidget->private = process;

	ret = sof_ipc_tx_message(sdev->ipc, process->comp.hdr.cmd, process,
				 ipc_size, r, sizeof(*r));
	if (ret >= 0)
		return ret;
err:
	kfree(process);
	return ret;
}

/*
 * Processing Component Topology - can be "effect", "codec", or general
 * "processing".
 */

static int sof_widget_load_process(struct snd_soc_component *scomp, int index,
				   struct snd_sof_widget *swidget,
				   struct snd_soc_tplg_dapm_widget *tw,
				   struct sof_ipc_comp_reply *r)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	struct snd_soc_tplg_private *private = &tw->priv;
	struct sof_ipc_comp_process config;
	int ret;

	/* check we have some tokens - we need at least process type */
	if (le32_to_cpu(private->size) == 0) {
		dev_err(sdev->dev, "error: process tokens not found\n");
		return -EINVAL;
	}

	memset(&config, 0, sizeof(config));

	/* get the process token */
	ret = sof_parse_tokens(scomp, &config, process_tokens,
			       ARRAY_SIZE(process_tokens), private->array,
			       le32_to_cpu(private->size));
	if (ret != 0) {
		dev_err(sdev->dev, "error: parse process tokens failed %d\n",
			le32_to_cpu(private->size));
		return ret;
	}

	/* now load process specific data and send IPC */
	ret = sof_process_load(scomp, index, swidget, tw, r,
			       find_process_comp_type(config.type));
	if (ret < 0) {
		dev_err(sdev->dev, "error: process loading failed\n");
		return ret;
	}

	return 0;
}

static int sof_widget_bind_event(struct snd_sof_dev *sdev,
				 struct snd_sof_widget *swidget,
				 u16 event_type)
{
	struct sof_ipc_comp *ipc_comp;

	/* validate widget event type */
	switch (event_type) {
	case SOF_KEYWORD_DETECT_DAPM_EVENT:
		/* only KEYWORD_DETECT comps should handle this */
		if (swidget->id != snd_soc_dapm_effect)
			break;

		ipc_comp = swidget->private;
		if (ipc_comp && ipc_comp->type != SOF_COMP_KEYWORD_DETECT)
			break;

		/* bind event to keyword detect comp */
		return snd_soc_tplg_widget_bind_event(swidget->widget,
						      sof_kwd_events,
						      ARRAY_SIZE(sof_kwd_events),
						      event_type);
	default:
		break;
	}

	dev_err(sdev->dev,
		"error: invalid event type %d for widget %s\n",
		event_type, swidget->widget->name);
	return -EINVAL;
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
	struct snd_sof_control *scontrol;
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
		ret = sof_widget_load_process(scomp, index, swidget, tw,
					      &reply);
		break;
	case snd_soc_dapm_mux:
	case snd_soc_dapm_demux:
		ret = sof_widget_load_mux(scomp, index, swidget, tw, &reply);
		break;
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

	/* bind widget to external event */
	if (tw->event_type) {
		ret = sof_widget_bind_event(sdev, swidget,
					    le16_to_cpu(tw->event_type));
		if (ret) {
			dev_err(sdev->dev, "error: widget event binding failed\n");
			kfree(swidget->private);
			kfree(swidget);
			return ret;
		}
	}

	w->dobj.private = swidget;
	list_add(&swidget->list, &sdev->widget_list);
	return ret;
}

static int sof_route_unload(struct snd_soc_component *scomp,
			    struct snd_soc_dobj *dobj)
{
	struct snd_sof_route *sroute;

	sroute = dobj->private;
	if (!sroute)
		return 0;

	/* free sroute and its private data */
	kfree(sroute->private);
	list_del(&sroute->list);
	kfree(sroute);

	return 0;
}

static int sof_widget_unload(struct snd_soc_component *scomp,
			     struct snd_soc_dobj *dobj)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	const struct snd_kcontrol_new *kc;
	struct snd_soc_dapm_widget *widget;
	struct sof_ipc_pipe_new *pipeline;
	struct snd_sof_control *scontrol;
	struct snd_sof_widget *swidget;
	struct soc_mixer_control *sm;
	struct soc_bytes_ext *sbe;
	struct snd_sof_dai *dai;
	struct soc_enum *se;
	int ret = 0;
	int i;

	swidget = dobj->private;
	if (!swidget)
		return 0;

	widget = swidget->widget;

	switch (swidget->id) {
	case snd_soc_dapm_dai_in:
	case snd_soc_dapm_dai_out:
		dai = swidget->private;

		if (dai) {
			/* free dai config */
			kfree(dai->dai_config);
			list_del(&dai->list);
		}
		break;
	case snd_soc_dapm_scheduler:

		/* power down the pipeline schedule core */
		pipeline = swidget->private;
		ret = snd_sof_dsp_core_power_down(sdev, 1 << pipeline->core);
		if (ret < 0)
			dev_err(sdev->dev, "error: powering down pipeline schedule core %d\n",
				pipeline->core);

		/* update enabled cores mask */
		sdev->enabled_cores_mask &= ~(1 << pipeline->core);

		break;
	default:
		break;
	}
	for (i = 0; i < widget->num_kcontrols; i++) {
		kc = &widget->kcontrol_news[i];
		switch (dobj->widget.kcontrol_type) {
		case SND_SOC_TPLG_TYPE_MIXER:
			sm = (struct soc_mixer_control *)kc->private_value;
			scontrol = sm->dobj.private;
			if (sm->max > 1)
				kfree(scontrol->volume_table);
			break;
		case SND_SOC_TPLG_TYPE_ENUM:
			se = (struct soc_enum *)kc->private_value;
			scontrol = se->dobj.private;
			break;
		case SND_SOC_TPLG_TYPE_BYTES:
			sbe = (struct soc_bytes_ext *)kc->private_value;
			scontrol = sbe->dobj.private;
			break;
		default:
			dev_warn(sdev->dev, "unsupported kcontrol_type\n");
			goto out;
		}
		kfree(scontrol->control_data);
		list_del(&scontrol->list);
		kfree(scontrol);
	}

out:
	/* free private value */
	kfree(swidget->private);

	/* remove and free swidget object */
	list_del(&swidget->list);
	kfree(swidget);

	return ret;
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
	struct snd_soc_tplg_stream_caps *caps;
	struct snd_sof_pcm *spcm;
	int stream = SNDRV_PCM_STREAM_PLAYBACK;
	int ret = 0;

	/* nothing to do for BEs atm */
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
	list_add(&spcm->list, &sdev->pcm_list);

	/* do we need to allocate playback PCM DMA pages */
	if (!spcm->pcm.playback)
		goto capture;

	caps = &spcm->pcm.caps[stream];

	/* allocate playback page table buffer */
	ret = snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV, sdev->dev,
				  PAGE_SIZE, &spcm->stream[stream].page_table);
	if (ret < 0) {
		dev_err(sdev->dev, "error: can't alloc page table for %s %d\n",
			caps->name, ret);

		return ret;
	}

	/* bind pcm to host comp */
	ret = spcm_bind(sdev, spcm, stream);
	if (ret) {
		dev_err(sdev->dev,
			"error: can't bind pcm to host\n");
		goto free_playback_tables;
	}

capture:
	stream = SNDRV_PCM_STREAM_CAPTURE;

	/* do we need to allocate capture PCM DMA pages */
	if (!spcm->pcm.capture)
		return ret;

	caps = &spcm->pcm.caps[stream];

	/* allocate capture page table buffer */
	ret = snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV, sdev->dev,
				  PAGE_SIZE, &spcm->stream[stream].page_table);
	if (ret < 0) {
		dev_err(sdev->dev, "error: can't alloc page table for %s %d\n",
			caps->name, ret);
		goto free_playback_tables;
	}

	/* bind pcm to host comp */
	ret = spcm_bind(sdev, spcm, stream);
	if (ret) {
		dev_err(sdev->dev,
			"error: can't bind pcm to host\n");
		snd_dma_free_pages(&spcm->stream[stream].page_table);
		goto free_playback_tables;
	}

	return ret;

free_playback_tables:
	if (spcm->pcm.playback)
		snd_dma_free_pages(&spcm->stream[SNDRV_PCM_STREAM_PLAYBACK].page_table);

	return ret;
}

static int sof_dai_unload(struct snd_soc_component *scomp,
			  struct snd_soc_dobj *dobj)
{
	struct snd_sof_pcm *spcm = dobj->private;

	/* free PCM DMA pages */
	if (spcm->pcm.playback)
		snd_dma_free_pages(&spcm->stream[SNDRV_PCM_STREAM_PLAYBACK].page_table);

	if (spcm->pcm.capture)
		snd_dma_free_pages(&spcm->stream[SNDRV_PCM_STREAM_CAPTURE].page_table);

	/* remove from list and free spcm */
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

/* set config for all DAI's with name matching the link name */
static int sof_set_dai_config(struct snd_sof_dev *sdev, u32 size,
			      struct snd_soc_dai_link *link,
			      struct sof_ipc_dai_config *config)
{
	struct snd_sof_dai *dai;
	int found = 0;

	list_for_each_entry(dai, &sdev->dai_list, list) {
		if (!dai->name)
			continue;

		if (strcmp(link->name, dai->name) == 0) {
			dai->dai_config = kmemdup(config, size, GFP_KERNEL);
			if (!dai->dai_config)
				return -ENOMEM;

			/* set cpu_dai_name */
			dai->cpu_dai_name = link->cpus->dai_name;

			found = 1;
		}
	}

	/*
	 * machine driver may define a dai link with playback and capture
	 * dai enabled, but the dai link in topology would support both, one
	 * or none of them. Here print a warning message to notify user
	 */
	if (!found) {
		dev_warn(sdev->dev, "warning: failed to find dai for dai link %s",
			 link->name);
	}

	return 0;
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

	dev_dbg(sdev->dev, "tplg: config SSP%d fmt 0x%x mclk %d bclk %d fclk %d width (%d)%d slots %d mclk id %d quirks %d\n",
		config->dai_index, config->format,
		config->ssp.mclk_rate, config->ssp.bclk_rate,
		config->ssp.fsync_rate, config->ssp.sample_valid_bits,
		config->ssp.tdm_slot_width, config->ssp.tdm_slots,
		config->ssp.mclk_id, config->ssp.quirks);

	/* validate SSP fsync rate and channel count */
	if (config->ssp.fsync_rate < 8000 || config->ssp.fsync_rate > 192000) {
		dev_err(sdev->dev, "error: invalid fsync rate for SSP%d\n",
			config->dai_index);
		return -EINVAL;
	}

	if (config->ssp.tdm_slots < 1 || config->ssp.tdm_slots > 8) {
		dev_err(sdev->dev, "error: invalid channel count for SSP%d\n",
			config->dai_index);
		return -EINVAL;
	}

	/* send message to DSP */
	ret = sof_ipc_tx_message(sdev->ipc,
				 config->hdr.cmd, config, size, &reply,
				 sizeof(reply));

	if (ret < 0) {
		dev_err(sdev->dev, "error: failed to set DAI config for SSP%d\n",
			config->dai_index);
		return ret;
	}

	/* set config for all DAI's with name matching the link name */
	ret = sof_set_dai_config(sdev, size, link, config);
	if (ret < 0)
		dev_err(sdev->dev, "error: failed to save DAI config for SSP%d\n",
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
	struct sof_ipc_fw_ready *ready = &sdev->fw_ready;
	struct sof_ipc_fw_version *v = &ready->version;
	u32 size;
	int ret, j;

	/*
	 * config is only used for the common params in dmic_params structure
	 * that does not include the PDM controller config array
	 * Set the common params to 0.
	 */
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
	 * allocate memory for dmic dai config accounting for the
	 * variable number of active pdm controllers
	 * This will be the ipc payload for setting dai config
	 */
	size = sizeof(*config) + sizeof(struct sof_ipc_dai_dmic_pdm_ctrl) *
					config->dmic.num_pdm_active;

	ipc_config = kzalloc(size, GFP_KERNEL);
	if (!ipc_config)
		return -ENOMEM;

	/* copy the common dai config and dmic params */
	memcpy(ipc_config, config, sizeof(*config));

	/*
	 * alloc memory for private member
	 * Used to track the pdm config array index currently being parsed
	 */
	sdev->private = kzalloc(sizeof(u32), GFP_KERNEL);
	if (!sdev->private) {
		kfree(ipc_config);
		return -ENOMEM;
	}

	/* get DMIC PDM tokens */
	ret = sof_parse_tokens(scomp, &ipc_config->dmic.pdm[0], dmic_pdm_tokens,
			       ARRAY_SIZE(dmic_pdm_tokens), private->array,
			       le32_to_cpu(private->size));
	if (ret != 0) {
		dev_err(sdev->dev, "error: parse dmic pdm tokens failed %d\n",
			le32_to_cpu(private->size));
		goto err;
	}

	/* set IPC header size */
	ipc_config->hdr.size = size;

	/* debug messages */
	dev_dbg(sdev->dev, "tplg: config DMIC%d driver version %d\n",
		ipc_config->dai_index, ipc_config->dmic.driver_ipc_version);
	dev_dbg(sdev->dev, "pdmclk_min %d pdm_clkmax %d duty_min %hd\n",
		ipc_config->dmic.pdmclk_min, ipc_config->dmic.pdmclk_max,
		ipc_config->dmic.duty_min);
	dev_dbg(sdev->dev, "duty_max %hd fifo_fs %d num_pdms active %d\n",
		ipc_config->dmic.duty_max, ipc_config->dmic.fifo_fs,
		ipc_config->dmic.num_pdm_active);
	dev_dbg(sdev->dev, "fifo word length %hd\n",
		ipc_config->dmic.fifo_bits);

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

	if (SOF_ABI_VER(v->major, v->minor, v->micro) < SOF_ABI_VER(3, 0, 1)) {
		/* this takes care of backwards compatible handling of fifo_bits_b */
		ipc_config->dmic.reserved_2 = ipc_config->dmic.fifo_bits;
	}

	/* send message to DSP */
	ret = sof_ipc_tx_message(sdev->ipc,
				 ipc_config->hdr.cmd, ipc_config, size, &reply,
				 sizeof(reply));

	if (ret < 0) {
		dev_err(sdev->dev,
			"error: failed to set DAI config for DMIC%d\n",
			config->dai_index);
		goto err;
	}

	/* set config for all DAI's with name matching the link name */
	ret = sof_set_dai_config(sdev, size, link, ipc_config);
	if (ret < 0)
		dev_err(sdev->dev, "error: failed to save DAI config for DMIC%d\n",
			config->dai_index);

err:
	kfree(sdev->private);
	kfree(ipc_config);

	return ret;
}

/*
 * for hda link, playback and capture are supported by different dai
 * in FW. Here get the dai_index, set dma channel of each dai
 * and send config to FW. In FW, each dai sets config by dai_index
 */
static int sof_link_hda_process(struct snd_sof_dev *sdev,
				struct snd_soc_dai_link *link,
				struct sof_ipc_dai_config *config)
{
	struct sof_ipc_reply reply;
	u32 size = sizeof(*config);
	struct snd_sof_dai *sof_dai;
	int found = 0;
	int ret;

	list_for_each_entry(sof_dai, &sdev->dai_list, list) {
		if (!sof_dai->name)
			continue;

		if (strcmp(link->name, sof_dai->name) == 0) {
			config->dai_index = sof_dai->comp_dai.dai_index;
			found = 1;

			config->hda.link_dma_ch = DMA_CHAN_INVALID;

			/* save config in dai component */
			sof_dai->dai_config = kmemdup(config, size, GFP_KERNEL);
			if (!sof_dai->dai_config)
				return -ENOMEM;

			sof_dai->cpu_dai_name = link->cpus->dai_name;

			/* send message to DSP */
			ret = sof_ipc_tx_message(sdev->ipc,
						 config->hdr.cmd, config, size,
						 &reply, sizeof(reply));

			if (ret < 0) {
				dev_err(sdev->dev, "error: failed to set DAI config for direction:%d of HDA dai %d\n",
					sof_dai->comp_dai.direction,
					config->dai_index);

				return ret;
			}
		}
	}

	/*
	 * machine driver may define a dai link with playback and capture
	 * dai enabled, but the dai link in topology would support both, one
	 * or none of them. Here print a warning message to notify user
	 */
	if (!found) {
		dev_warn(sdev->dev, "warning: failed to find dai for dai link %s",
			 link->name);
	}

	return 0;
}

static int sof_link_hda_load(struct snd_soc_component *scomp, int index,
			     struct snd_soc_dai_link *link,
			     struct snd_soc_tplg_link_config *cfg,
			     struct snd_soc_tplg_hw_config *hw_config,
			     struct sof_ipc_dai_config *config)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	struct snd_soc_tplg_private *private = &cfg->priv;
	struct snd_soc_dai *dai;
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

	dai = snd_soc_find_dai(link->cpus);
	if (!dai) {
		dev_err(sdev->dev, "error: failed to find dai %s in %s",
			link->cpus->dai_name, __func__);
		return -EINVAL;
	}

	ret = sof_link_hda_process(sdev, link, config);
	if (ret < 0)
		dev_err(sdev->dev, "error: failed to process hda dai link %s",
			link->name);

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
	int num_hw_configs;
	int ret;
	int i = 0;

	if (!link->platforms) {
		dev_err(sdev->dev, "error: no platforms\n");
		return -EINVAL;
	}
	link->platforms->name = dev_name(sdev->dev);

	/*
	 * Set nonatomic property for FE dai links as their trigger action
	 * involves IPC's.
	 */
	if (!link->no_pcm) {
		link->nonatomic = true;

		/* nothing more to do for FE dai links */
		return 0;
	}

	/* check we have some tokens - we need at least DAI type */
	if (le32_to_cpu(private->size) == 0) {
		dev_err(sdev->dev, "error: expected tokens for DAI, none found\n");
		return -EINVAL;
	}

	/* Send BE DAI link configurations to DSP */
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

	/*
	 * DAI links are expected to have at least 1 hw_config.
	 * But some older topologies might have no hw_config for HDA dai links.
	 */
	num_hw_configs = le32_to_cpu(cfg->num_hw_configs);
	if (!num_hw_configs) {
		if (config.type != SOF_DAI_INTEL_HDA) {
			dev_err(sdev->dev, "error: unexpected DAI config count %d!\n",
				le32_to_cpu(cfg->num_hw_configs));
			return -EINVAL;
		}
	} else {
		dev_dbg(sdev->dev, "tplg: %d hw_configs found, default id: %d!\n",
			cfg->num_hw_configs, le32_to_cpu(cfg->default_hw_config_id));

		for (i = 0; i < num_hw_configs; i++) {
			if (cfg->hw_config[i].id == cfg->default_hw_config_id)
				break;
		}

		if (i == num_hw_configs) {
			dev_err(sdev->dev, "error: default hw_config id: %d not found!\n",
				le32_to_cpu(cfg->default_hw_config_id));
			return -EINVAL;
		}
	}

	/* configure dai IPC message */
	hw_config = &cfg->hw_config[i];

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

	return 0;
}

static int sof_link_hda_unload(struct snd_sof_dev *sdev,
			       struct snd_soc_dai_link *link)
{
	struct snd_soc_dai *dai;
	int ret = 0;

	dai = snd_soc_find_dai(link->cpus);
	if (!dai) {
		dev_err(sdev->dev, "error: failed to find dai %s in %s",
			link->cpus->dai_name, __func__);
		return -EINVAL;
	}

	return ret;
}

static int sof_link_unload(struct snd_soc_component *scomp,
			   struct snd_soc_dobj *dobj)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	struct snd_soc_dai_link *link =
		container_of(dobj, struct snd_soc_dai_link, dobj);

	struct snd_sof_dai *sof_dai;
	int ret = 0;

	/* only BE link is loaded by sof */
	if (!link->no_pcm)
		return 0;

	list_for_each_entry(sof_dai, &sdev->dai_list, list) {
		if (!sof_dai->name)
			continue;

		if (strcmp(link->name, sof_dai->name) == 0)
			goto found;
	}

	dev_err(sdev->dev, "error: failed to find dai %s in %s",
		link->name, __func__);
	return -EINVAL;
found:

	switch (sof_dai->dai_config->type) {
	case SOF_DAI_INTEL_SSP:
	case SOF_DAI_INTEL_DMIC:
		/* no resource needs to be released for SSP and DMIC */
		break;
	case SOF_DAI_INTEL_HDA:
		ret = sof_link_hda_unload(sdev, link);
		break;
	default:
		dev_err(sdev->dev, "error: invalid DAI type %d\n",
			sof_dai->dai_config->type);
		ret = -EINVAL;
		break;
	}

	return ret;
}

/* DAI link - used for any driver specific init */
static int sof_route_load(struct snd_soc_component *scomp, int index,
			  struct snd_soc_dapm_route *route)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	struct sof_ipc_pipe_comp_connect *connect;
	struct snd_sof_widget *source_swidget, *sink_swidget;
	struct snd_soc_dobj *dobj = &route->dobj;
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
		dev_err(sdev->dev, "error: source %s not found\n",
			route->source);
		ret = -EINVAL;
		goto err;
	}

	/*
	 * Virtual widgets of type output/out_drv may be added in topology
	 * for compatibility. These are not handled by the FW.
	 * So, don't send routes whose source/sink widget is of such types
	 * to the DSP.
	 */
	if (source_swidget->id == snd_soc_dapm_out_drv ||
	    source_swidget->id == snd_soc_dapm_output)
		goto err;

	connect->source_id = source_swidget->comp_id;

	/* sink component */
	sink_swidget = snd_sof_find_swidget(sdev, (char *)route->sink);
	if (!sink_swidget) {
		dev_err(sdev->dev, "error: sink %s not found\n",
			route->sink);
		ret = -EINVAL;
		goto err;
	}

	/*
	 * Don't send routes whose sink widget is of type
	 * output or out_drv to the DSP
	 */
	if (sink_swidget->id == snd_soc_dapm_out_drv ||
	    sink_swidget->id == snd_soc_dapm_output)
		goto err;

	connect->sink_id = sink_swidget->comp_id;

	/*
	 * For virtual routes, both sink and source are not
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

		sroute->route = route;
		dobj->private = sroute;
		sroute->private = connect;

		/* add route to route list */
		list_add(&sroute->list, &sdev->route_list);

		return ret;
	}

err:
	kfree(connect);
	kfree(sroute);
	return ret;
}

/* Function to set the initial value of SOF kcontrols.
 * The value will be stored in scontrol->control_data
 */
static int snd_sof_cache_kcontrol_val(struct snd_sof_dev *sdev)
{
	struct snd_sof_control *scontrol = NULL;
	int ipc_cmd, ctrl_type;
	int ret = 0;

	list_for_each_entry(scontrol, &sdev->kcontrol_list, list) {

		/* notify DSP of kcontrol values */
		switch (scontrol->cmd) {
		case SOF_CTRL_CMD_VOLUME:
		case SOF_CTRL_CMD_ENUM:
		case SOF_CTRL_CMD_SWITCH:
			ipc_cmd = SOF_IPC_COMP_GET_VALUE;
			ctrl_type = SOF_CTRL_TYPE_VALUE_CHAN_GET;
			break;
		case SOF_CTRL_CMD_BINARY:
			ipc_cmd = SOF_IPC_COMP_GET_DATA;
			ctrl_type = SOF_CTRL_TYPE_DATA_GET;
			break;
		default:
			dev_err(sdev->dev,
				"error: Invalid scontrol->cmd: %d\n",
				scontrol->cmd);
			return -EINVAL;
		}
		ret = snd_sof_ipc_set_get_comp_data(sdev->ipc, scontrol,
						    ipc_cmd, ctrl_type,
						    scontrol->cmd,
						    false);
		if (ret < 0) {
			dev_warn(sdev->dev,
				"error: kcontrol value get for widget: %d\n",
				scontrol->comp_id);
		}
	}

	return ret;
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
	/*
	 * cache initial values of SOF kcontrols by reading DSP value over
	 * IPC. It may be overwritten by alsa-mixer after booting up
	 */
	snd_sof_cache_kcontrol_val(sdev);
}

/* manifest - optional to inform component of manifest */
static int sof_manifest(struct snd_soc_component *scomp, int index,
			struct snd_soc_tplg_manifest *man)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	u32 size;
	u32 abi_version;

	size = le32_to_cpu(man->priv.size);

	/* backward compatible with tplg without ABI info */
	if (!size) {
		dev_dbg(sdev->dev, "No topology ABI info\n");
		return 0;
	}

	if (size != SOF_TPLG_ABI_SIZE) {
		dev_err(sdev->dev, "error: invalid topology ABI size\n");
		return -EINVAL;
	}

	dev_info(sdev->dev,
		 "Topology: ABI %d:%d:%d Kernel ABI %d:%d:%d\n",
		 man->priv.data[0], man->priv.data[1],
		 man->priv.data[2], SOF_ABI_MAJOR, SOF_ABI_MINOR,
		 SOF_ABI_PATCH);

	abi_version = SOF_ABI_VER(man->priv.data[0],
				  man->priv.data[1],
				  man->priv.data[2]);

	if (SOF_ABI_VERSION_INCOMPATIBLE(SOF_ABI_VERSION, abi_version)) {
		dev_err(sdev->dev, "error: incompatible topology ABI version\n");
		return -EINVAL;
	}

	if (abi_version > SOF_ABI_VERSION) {
		if (!IS_ENABLED(CONFIG_SND_SOC_SOF_STRICT_ABI_CHECKS)) {
			dev_warn(sdev->dev, "warn: topology ABI is more recent than kernel\n");
		} else {
			dev_err(sdev->dev, "error: topology ABI is more recent than kernel\n");
			return -EINVAL;
		}
	}

	return 0;
}

/* vendor specific kcontrol handlers available for binding */
static const struct snd_soc_tplg_kcontrol_ops sof_io_ops[] = {
	{SOF_TPLG_KCTL_VOL_ID, snd_sof_volume_get, snd_sof_volume_put},
	{SOF_TPLG_KCTL_BYTES_ID, snd_sof_bytes_get, snd_sof_bytes_put},
	{SOF_TPLG_KCTL_ENUM_ID, snd_sof_enum_get, snd_sof_enum_put},
	{SOF_TPLG_KCTL_SWITCH_ID, snd_sof_switch_get, snd_sof_switch_put},
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
	/* .widget_load is not currently used */
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
	int ret;

	dev_dbg(sdev->dev, "loading topology:%s\n", file);

	ret = request_firmware(&fw, file, sdev->dev);
	if (ret < 0) {
		dev_err(sdev->dev, "error: tplg request firmware %s failed err: %d\n",
			file, ret);
		return ret;
	}

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
