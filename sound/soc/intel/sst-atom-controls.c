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
	int ret = 0;
	struct sst_data *drv = snd_soc_platform_get_drvdata(platform);

	drv->byte_stream = devm_kzalloc(platform->dev,
					SST_MAX_BIN_BYTES, GFP_KERNEL);
	if (!drv->byte_stream)
		return -ENOMEM;

	/*Initialize algo control params*/
	ret = sst_algo_control_init(platform->dev);
	if (ret)
		return ret;
	ret = snd_soc_add_platform_controls(platform, sst_algo_controls,
			ARRAY_SIZE(sst_algo_controls));
	return ret;
}
