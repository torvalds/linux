/*
 *  sst-atom-controls.c - Intel MID Platform driver DPCM ALSA controls for Mrfld
 *
 *  Copyright (C) 2013-14 Intel Corp
 *  Author: Omair Mohammed Abdullah <omair.m.abdullah@intel.com>
 *	Vinod Koul <vinod.koul@intel.com>
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
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/slab.h>
#include <sound/soc.h>
#include <sound/tlv.h>
#include "sst-mfld-platform.h"
#include "sst-atom-controls.h"

static int sst_fill_byte_control(struct sst_data *drv,
					 u8 ipc_msg, u8 block,
					 u8 task_id, u8 pipe_id,
					 u16 len, void *cmd_data)
{
	struct snd_sst_bytes_v2 *byte_data = drv->byte_stream;

	byte_data->type = SST_CMD_BYTES_SET;
	byte_data->ipc_msg = ipc_msg;
	byte_data->block = block;
	byte_data->task_id = task_id;
	byte_data->pipe_id = pipe_id;

	if (len > SST_MAX_BIN_BYTES - sizeof(*byte_data)) {
		dev_err(&drv->pdev->dev, "command length too big (%u)", len);
		return -EINVAL;
	}
	byte_data->len = len;
	memcpy(byte_data->bytes, cmd_data, len);
	print_hex_dump_bytes("writing to lpe: ", DUMP_PREFIX_OFFSET,
			     byte_data, len + sizeof(*byte_data));
	return 0;
}

static int sst_fill_and_send_cmd_unlocked(struct sst_data *drv,
				 u8 ipc_msg, u8 block, u8 task_id, u8 pipe_id,
				 void *cmd_data, u16 len)
{
	int ret = 0;

	ret = sst_fill_byte_control(drv, ipc_msg,
				block, task_id, pipe_id, len, cmd_data);
	if (ret < 0)
		return ret;
	return sst->ops->send_byte_stream(sst->dev, drv->byte_stream);
}

/**
 * sst_fill_and_send_cmd - generate the IPC message and send it to the FW
 * @ipc_msg:	type of IPC (CMD, SET_PARAMS, GET_PARAMS)
 * @cmd_data:	the IPC payload
 */
static int sst_fill_and_send_cmd(struct sst_data *drv,
				 u8 ipc_msg, u8 block, u8 task_id, u8 pipe_id,
				 void *cmd_data, u16 len)
{
	int ret;

	mutex_lock(&drv->lock);
	ret = sst_fill_and_send_cmd_unlocked(drv, ipc_msg, block,
					task_id, pipe_id, cmd_data, len);
	mutex_unlock(&drv->lock);

	return ret;
}

static int sst_send_algo_cmd(struct sst_data *drv,
			      struct sst_algo_control *bc)
{
	int len, ret = 0;
	struct sst_cmd_set_params *cmd;

	/*bc->max includes sizeof algos + length field*/
	len = sizeof(cmd->dst) + sizeof(cmd->command_id) + bc->max;

	cmd = kzalloc(len, GFP_KERNEL);
	if (cmd == NULL)
		return -ENOMEM;

	SST_FILL_DESTINATION(2, cmd->dst, bc->pipe_id, bc->module_id);
	cmd->command_id = bc->cmd_id;
	memcpy(cmd->params, bc->params, bc->max);

	ret = sst_fill_and_send_cmd_unlocked(drv, SST_IPC_IA_SET_PARAMS,
				SST_FLAG_BLOCKED, bc->task_id, 0, cmd, len);
	kfree(cmd);
	return ret;
}

static int sst_algo_bytes_ctl_info(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_info *uinfo)
{
	struct sst_algo_control *bc = (void *)kcontrol->private_value;

	uinfo->type = SNDRV_CTL_ELEM_TYPE_BYTES;
	uinfo->count = bc->max;

	return 0;
}

static int sst_algo_control_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct sst_algo_control *bc = (void *)kcontrol->private_value;
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);

	switch (bc->type) {
	case SST_ALGO_PARAMS:
		memcpy(ucontrol->value.bytes.data, bc->params, bc->max);
		break;
	default:
		dev_err(component->dev, "Invalid Input- algo type:%d\n",
				bc->type);
		return -EINVAL;

	}
	return 0;
}

static int sst_algo_control_set(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	int ret = 0;
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct sst_data *drv = snd_soc_component_get_drvdata(cmpnt);
	struct sst_algo_control *bc = (void *)kcontrol->private_value;

	dev_dbg(cmpnt->dev, "control_name=%s\n", kcontrol->id.name);
	mutex_lock(&drv->lock);
	switch (bc->type) {
	case SST_ALGO_PARAMS:
		memcpy(bc->params, ucontrol->value.bytes.data, bc->max);
		break;
	default:
		mutex_unlock(&drv->lock);
		dev_err(cmpnt->dev, "Invalid Input- algo type:%d\n",
				bc->type);
		return -EINVAL;
	}
	/*if pipe is enabled, need to send the algo params from here*/
	if (bc->w && bc->w->power)
		ret = sst_send_algo_cmd(drv, bc);
	mutex_unlock(&drv->lock);

	return ret;
}

static int sst_gain_ctl_info(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_info *uinfo)
{
	struct sst_gain_mixer_control *mc = (void *)kcontrol->private_value;

	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = mc->stereo ? 2 : 1;
	uinfo->value.integer.min = mc->min;
	uinfo->value.integer.max = mc->max;

	return 0;
}

/**
 * sst_send_gain_cmd - send the gain algorithm IPC to the FW
 * @gv:		the stored value of gain (also contains rampduration)
 * @mute:	flag that indicates whether this was called from the
 *		digital_mute callback or directly. If called from the
 *		digital_mute callback, module will be muted/unmuted based on this
 *		flag. The flag is always 0 if called directly.
 *
 * Called with sst_data.lock held
 *
 * The user-set gain value is sent only if the user-controllable 'mute' control
 * is OFF (indicated by gv->mute). Otherwise, the mute value (MIN value) is
 * sent.
 */
static int sst_send_gain_cmd(struct sst_data *drv, struct sst_gain_value *gv,
			      u16 task_id, u16 loc_id, u16 module_id, int mute)
{
	struct sst_cmd_set_gain_dual cmd;

	dev_dbg(&drv->pdev->dev, "Enter\n");

	cmd.header.command_id = MMX_SET_GAIN;
	SST_FILL_DEFAULT_DESTINATION(cmd.header.dst);
	cmd.gain_cell_num = 1;

	if (mute || gv->mute) {
		cmd.cell_gains[0].cell_gain_left = SST_GAIN_MIN_VALUE;
		cmd.cell_gains[0].cell_gain_right = SST_GAIN_MIN_VALUE;
	} else {
		cmd.cell_gains[0].cell_gain_left = gv->l_gain;
		cmd.cell_gains[0].cell_gain_right = gv->r_gain;
	}

	SST_FILL_DESTINATION(2, cmd.cell_gains[0].dest,
			     loc_id, module_id);
	cmd.cell_gains[0].gain_time_constant = gv->ramp_duration;

	cmd.header.length = sizeof(struct sst_cmd_set_gain_dual)
				- sizeof(struct sst_dsp_header);

	/* we are with lock held, so call the unlocked api  to send */
	return sst_fill_and_send_cmd_unlocked(drv, SST_IPC_IA_SET_PARAMS,
				SST_FLAG_BLOCKED, task_id, 0, &cmd,
			      sizeof(cmd.header) + cmd.header.length);
}

static int sst_gain_get(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct sst_gain_mixer_control *mc = (void *)kcontrol->private_value;
	struct sst_gain_value *gv = mc->gain_val;

	switch (mc->type) {
	case SST_GAIN_TLV:
		ucontrol->value.integer.value[0] = gv->l_gain;
		ucontrol->value.integer.value[1] = gv->r_gain;
		break;

	case SST_GAIN_MUTE:
		ucontrol->value.integer.value[0] = gv->mute ? 1 : 0;
		break;

	case SST_GAIN_RAMP_DURATION:
		ucontrol->value.integer.value[0] = gv->ramp_duration;
		break;

	default:
		dev_err(component->dev, "Invalid Input- gain type:%d\n",
				mc->type);
		return -EINVAL;
	};

	return 0;
}

static int sst_gain_put(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	int ret = 0;
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct sst_data *drv = snd_soc_component_get_drvdata(cmpnt);
	struct sst_gain_mixer_control *mc = (void *)kcontrol->private_value;
	struct sst_gain_value *gv = mc->gain_val;

	mutex_lock(&drv->lock);

	switch (mc->type) {
	case SST_GAIN_TLV:
		gv->l_gain = ucontrol->value.integer.value[0];
		gv->r_gain = ucontrol->value.integer.value[1];
		dev_dbg(cmpnt->dev, "%s: Volume %d, %d\n",
				mc->pname, gv->l_gain, gv->r_gain);
		break;

	case SST_GAIN_MUTE:
		gv->mute = !!ucontrol->value.integer.value[0];
		dev_dbg(cmpnt->dev, "%s: Mute %d\n", mc->pname, gv->mute);
		break;

	case SST_GAIN_RAMP_DURATION:
		gv->ramp_duration = ucontrol->value.integer.value[0];
		dev_dbg(cmpnt->dev, "%s: Ramp Delay%d\n",
					mc->pname, gv->ramp_duration);
		break;

	default:
		mutex_unlock(&drv->lock);
		dev_err(cmpnt->dev, "Invalid Input- gain type:%d\n",
				mc->type);
		return -EINVAL;
	};

	if (mc->w && mc->w->power)
		ret = sst_send_gain_cmd(drv, gv, mc->task_id,
			mc->pipe_id | mc->instance_id, mc->module_id, 0);
	mutex_unlock(&drv->lock);

	return ret;
}

static const DECLARE_TLV_DB_SCALE(sst_gain_tlv_common, SST_GAIN_MIN_VALUE * 10, 10, 0);

/* Gain helper with min/max set */
#define SST_GAIN(name, path_id, task_id, instance, gain_var)				\
	SST_GAIN_KCONTROLS(name, "Gain", SST_GAIN_MIN_VALUE, SST_GAIN_MAX_VALUE,	\
		SST_GAIN_TC_MIN, SST_GAIN_TC_MAX,					\
		sst_gain_get, sst_gain_put,						\
		SST_MODULE_ID_GAIN_CELL, path_id, instance, task_id,			\
		sst_gain_tlv_common, gain_var)

#define SST_VOLUME(name, path_id, task_id, instance, gain_var)				\
	SST_GAIN_KCONTROLS(name, "Volume", SST_GAIN_MIN_VALUE, SST_GAIN_MAX_VALUE,	\
		SST_GAIN_TC_MIN, SST_GAIN_TC_MAX,					\
		sst_gain_get, sst_gain_put,						\
		SST_MODULE_ID_VOLUME, path_id, instance, task_id,			\
		sst_gain_tlv_common, gain_var)

static struct sst_gain_value sst_gains[];

static const struct snd_kcontrol_new sst_gain_controls[] = {
	SST_GAIN("media0_in", SST_PATH_INDEX_MEDIA0_IN, SST_TASK_MMX, 0, &sst_gains[0]),
	SST_GAIN("media1_in", SST_PATH_INDEX_MEDIA1_IN, SST_TASK_MMX, 0, &sst_gains[1]),
	SST_GAIN("media2_in", SST_PATH_INDEX_MEDIA2_IN, SST_TASK_MMX, 0, &sst_gains[2]),
	SST_GAIN("media3_in", SST_PATH_INDEX_MEDIA3_IN, SST_TASK_MMX, 0, &sst_gains[3]),

	SST_GAIN("pcm0_in", SST_PATH_INDEX_PCM0_IN, SST_TASK_SBA, 0, &sst_gains[4]),
	SST_GAIN("pcm1_in", SST_PATH_INDEX_PCM1_IN, SST_TASK_SBA, 0, &sst_gains[5]),
	SST_GAIN("pcm1_out", SST_PATH_INDEX_PCM1_OUT, SST_TASK_SBA, 0, &sst_gains[6]),
	SST_GAIN("pcm2_out", SST_PATH_INDEX_PCM2_OUT, SST_TASK_SBA, 0, &sst_gains[7]),

	SST_GAIN("codec_in0", SST_PATH_INDEX_CODEC_IN0, SST_TASK_SBA, 0, &sst_gains[8]),
	SST_GAIN("codec_in1", SST_PATH_INDEX_CODEC_IN1, SST_TASK_SBA, 0, &sst_gains[9]),
	SST_GAIN("codec_out0", SST_PATH_INDEX_CODEC_OUT0, SST_TASK_SBA, 0, &sst_gains[10]),
	SST_GAIN("codec_out1", SST_PATH_INDEX_CODEC_OUT1, SST_TASK_SBA, 0, &sst_gains[11]),
	SST_GAIN("media_loop1_out", SST_PATH_INDEX_MEDIA_LOOP1_OUT, SST_TASK_SBA, 0, &sst_gains[12]),
	SST_GAIN("media_loop2_out", SST_PATH_INDEX_MEDIA_LOOP2_OUT, SST_TASK_SBA, 0, &sst_gains[13]),
	SST_GAIN("sprot_loop_out", SST_PATH_INDEX_SPROT_LOOP_OUT, SST_TASK_SBA, 0, &sst_gains[14]),
	SST_VOLUME("media0_in", SST_PATH_INDEX_MEDIA0_IN, SST_TASK_MMX, 0, &sst_gains[15]),
};

#define SST_GAIN_NUM_CONTROLS 3
/* the SST_GAIN macro above will create three alsa controls for each
 * instance invoked, gain, mute and ramp duration, which use the same gain
 * cell sst_gain to keep track of data
 * To calculate number of gain cell instances we need to device by 3 in
 * below caulcation for gain cell memory.
 * This gets rid of static number and issues while adding new controls
 */
static struct sst_gain_value sst_gains[ARRAY_SIZE(sst_gain_controls)/SST_GAIN_NUM_CONTROLS];

static const struct snd_kcontrol_new sst_algo_controls[] = {
	SST_ALGO_KCONTROL_BYTES("media_loop1_out", "fir", 272, SST_MODULE_ID_FIR_24,
		 SST_PATH_INDEX_MEDIA_LOOP1_OUT, 0, SST_TASK_SBA, SBA_VB_SET_FIR),
	SST_ALGO_KCONTROL_BYTES("media_loop1_out", "iir", 300, SST_MODULE_ID_IIR_24,
		SST_PATH_INDEX_MEDIA_LOOP1_OUT, 0, SST_TASK_SBA, SBA_VB_SET_IIR),
	SST_ALGO_KCONTROL_BYTES("media_loop1_out", "mdrp", 286, SST_MODULE_ID_MDRP,
		SST_PATH_INDEX_MEDIA_LOOP1_OUT, 0, SST_TASK_SBA, SBA_SET_MDRP),
	SST_ALGO_KCONTROL_BYTES("media_loop2_out", "fir", 272, SST_MODULE_ID_FIR_24,
		SST_PATH_INDEX_MEDIA_LOOP2_OUT, 0, SST_TASK_SBA, SBA_VB_SET_FIR),
	SST_ALGO_KCONTROL_BYTES("media_loop2_out", "iir", 300, SST_MODULE_ID_IIR_24,
		SST_PATH_INDEX_MEDIA_LOOP2_OUT, 0, SST_TASK_SBA, SBA_VB_SET_IIR),
	SST_ALGO_KCONTROL_BYTES("media_loop2_out", "mdrp", 286, SST_MODULE_ID_MDRP,
		SST_PATH_INDEX_MEDIA_LOOP2_OUT, 0, SST_TASK_SBA, SBA_SET_MDRP),
	SST_ALGO_KCONTROL_BYTES("sprot_loop_out", "lpro", 192, SST_MODULE_ID_SPROT,
		SST_PATH_INDEX_SPROT_LOOP_OUT, 0, SST_TASK_SBA, SBA_VB_LPRO),
	SST_ALGO_KCONTROL_BYTES("codec_in0", "dcr", 52, SST_MODULE_ID_FILT_DCR,
		SST_PATH_INDEX_CODEC_IN0, 0, SST_TASK_SBA, SBA_VB_SET_IIR),
	SST_ALGO_KCONTROL_BYTES("codec_in1", "dcr", 52, SST_MODULE_ID_FILT_DCR,
		SST_PATH_INDEX_CODEC_IN1, 0, SST_TASK_SBA, SBA_VB_SET_IIR),

};

static int sst_algo_control_init(struct device *dev)
{
	int i = 0;
	struct sst_algo_control *bc;
	/*allocate space to cache the algo parameters in the driver*/
	for (i = 0; i < ARRAY_SIZE(sst_algo_controls); i++) {
		bc = (struct sst_algo_control *)sst_algo_controls[i].private_value;
		bc->params = devm_kzalloc(dev, bc->max, GFP_KERNEL);
		if (bc->params == NULL)
			return -ENOMEM;
	}
	return 0;
}

int sst_dsp_init_v2_dpcm(struct snd_soc_platform *platform)
{
	int i, ret = 0;
	struct sst_data *drv = snd_soc_platform_get_drvdata(platform);
	unsigned int gains = ARRAY_SIZE(sst_gain_controls)/3;

	drv->byte_stream = devm_kzalloc(platform->dev,
					SST_MAX_BIN_BYTES, GFP_KERNEL);
	if (!drv->byte_stream)
		return -ENOMEM;

	for (i = 0; i < gains; i++) {
		sst_gains[i].mute = SST_GAIN_MUTE_DEFAULT;
		sst_gains[i].l_gain = SST_GAIN_VOLUME_DEFAULT;
		sst_gains[i].r_gain = SST_GAIN_VOLUME_DEFAULT;
		sst_gains[i].ramp_duration = SST_GAIN_RAMP_DURATION_DEFAULT;
	}

	ret = snd_soc_add_platform_controls(platform, sst_gain_controls,
			ARRAY_SIZE(sst_gain_controls));
	if (ret)
		return ret;

	/* Initialize algo control params */
	ret = sst_algo_control_init(platform->dev);
	if (ret)
		return ret;
	ret = snd_soc_add_platform_controls(platform, sst_algo_controls,
			ARRAY_SIZE(sst_algo_controls));

	return ret;
}
