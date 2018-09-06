// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2018 Intel Corporation. All rights reserved.
//
// Author: Liam Girdwood <liam.r.girdwood@linux.intel.com>
//

/* Mixer Controls */

#include <linux/slab.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <sound/soc-topology.h>
#include <sound/soc.h>
#include <sound/control.h>
#include <uapi/sound/sof-ipc.h>
#include "sof-priv.h"

static inline u32 mixer_to_ipc(unsigned int value, u32 *volume_map, int size)
{
	if (value >= size)
		return volume_map[size - 1];
	else
		return volume_map[value];
}

static inline u32 ipc_to_mixer(u32 value, u32 *volume_map, int size)
{
	int i;

	for (i = 0; i < size; i++) {
		if (volume_map[i] >= value)
			return i;
	}

	return i - 1;
}

int snd_sof_volume_get(struct snd_kcontrol *kcontrol,
		       struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *sm =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct snd_sof_control *scontrol = sm->dobj.private;
	struct snd_sof_dev *sdev = scontrol->sdev;
	struct sof_ipc_ctrl_data *cdata = scontrol->control_data;
	unsigned int i, channels = scontrol->num_channels;
	int err, ret;

	ret = pm_runtime_get_sync(sdev->dev);
	if (ret < 0) {
		dev_err(sdev->dev, "error: volume get failed to resume %d\n",
			ret);
		return ret;
	}

	/* get all the mixer data from DSP */
	snd_sof_ipc_get_comp_data(sdev->ipc, scontrol, SOF_IPC_COMP_GET_VALUE,
				  SOF_CTRL_TYPE_VALUE_CHAN_GET,
				  SOF_CTRL_CMD_VOLUME);

	/* read back each channel */
	for (i = 0; i < channels; i++)
		ucontrol->value.integer.value[i] =
			ipc_to_mixer(cdata->chanv[i].value,
				     scontrol->volume_table, sm->max + 1);

	pm_runtime_mark_last_busy(sdev->dev);
	err = pm_runtime_put_autosuspend(sdev->dev);
	if (err < 0)
		dev_err(sdev->dev, "error: volume get failed to idle %d\n",
			err);
	return 0;
}

int snd_sof_volume_put(struct snd_kcontrol *kcontrol,
		       struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *sm =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct snd_sof_control *scontrol = sm->dobj.private;
	struct snd_sof_dev *sdev = scontrol->sdev;
	struct sof_ipc_ctrl_data *cdata = scontrol->control_data;
	unsigned int i, channels = scontrol->num_channels;
	int ret, err;

	ret = pm_runtime_get_sync(sdev->dev);
	if (ret < 0) {
		dev_err(sdev->dev, "error: volume put failed to resume %d\n",
			ret);
		return ret;
	}

	/* update each channel */
	for (i = 0; i < channels; i++) {
		cdata->chanv[i].value =
			mixer_to_ipc(ucontrol->value.integer.value[i],
				     scontrol->volume_table, sm->max + 1);
		cdata->chanv[i].channel = i;
	}

	/* notify DSP of mixer updates */
	snd_sof_ipc_set_comp_data(sdev->ipc, scontrol, SOF_IPC_COMP_SET_VALUE,
				  SOF_CTRL_TYPE_VALUE_CHAN_GET,
				  SOF_CTRL_CMD_VOLUME);

	pm_runtime_mark_last_busy(sdev->dev);
	err = pm_runtime_put_autosuspend(sdev->dev);
	if (err < 0)
		dev_err(sdev->dev, "error: volume put failed to idle %d\n",
			err);
	return 0;
}

int snd_sof_enum_get(struct snd_kcontrol *kcontrol,
		     struct snd_ctl_elem_value *ucontrol)
{
	struct soc_enum *se =
		(struct soc_enum *)kcontrol->private_value;
	struct snd_sof_control *scontrol = se->dobj.private;
	struct snd_sof_dev *sdev = scontrol->sdev;
	struct sof_ipc_ctrl_data *cdata = scontrol->control_data;
	unsigned int i, channels = scontrol->num_channels;
	int err, ret;

	ret = pm_runtime_get_sync(sdev->dev);
	if (ret < 0) {
		dev_err(sdev->dev, "error: enum get failed to resume %d\n",
			ret);
		return ret;
	}

	/* get all the mixer data from DSP */
	snd_sof_ipc_get_comp_data(sdev->ipc, scontrol, SOF_IPC_COMP_GET_VALUE,
				  SOF_CTRL_TYPE_VALUE_CHAN_GET,
				  SOF_CTRL_CMD_ENUM);

	/* read back each channel */
	for (i = 0; i < channels; i++)
		ucontrol->value.integer.value[i] = cdata->chanv[i].value;

	pm_runtime_mark_last_busy(sdev->dev);
	err = pm_runtime_put_autosuspend(sdev->dev);
	if (err < 0)
		dev_err(sdev->dev, "error: enum get failed to idle %d\n",
			ret);
	return 0;
}

int snd_sof_enum_put(struct snd_kcontrol *kcontrol,
		     struct snd_ctl_elem_value *ucontrol)
{
	struct soc_enum *se =
		(struct soc_enum *)kcontrol->private_value;
	struct snd_sof_control *scontrol = se->dobj.private;
	struct snd_sof_dev *sdev = scontrol->sdev;
	struct sof_ipc_ctrl_data *cdata = scontrol->control_data;
	unsigned int i, channels = scontrol->num_channels;
	int ret, err;

	ret = pm_runtime_get_sync(sdev->dev);
	if (ret < 0) {
		dev_err(sdev->dev, "error: enum put failed to resume %d\n",
			ret);
		return ret;
	}

	/* update each channel */
	for (i = 0; i < channels; i++)
		cdata->chanv[i].value = ucontrol->value.integer.value[i];

	/* notify DSP of mixer updates */
	snd_sof_ipc_set_comp_data(sdev->ipc, scontrol, SOF_IPC_COMP_SET_VALUE,
				  SOF_CTRL_TYPE_VALUE_CHAN_SET,
				  SOF_CTRL_CMD_ENUM);

	pm_runtime_mark_last_busy(sdev->dev);
	err = pm_runtime_put_autosuspend(sdev->dev);
	if (err < 0)
		dev_err(sdev->dev, "error: enum put failed to idle %d\n",
			err);
	return 0;
}

int snd_sof_bytes_get(struct snd_kcontrol *kcontrol,
		      struct snd_ctl_elem_value *ucontrol)
{
	struct soc_bytes_ext *be =
		(struct soc_bytes_ext *)kcontrol->private_value;
	struct snd_sof_control *scontrol = be->dobj.private;
	struct snd_sof_dev *sdev = scontrol->sdev;
	struct sof_ipc_ctrl_data *cdata = scontrol->control_data;
	struct sof_abi_hdr *data = cdata->data;
	size_t size;
	int ret, err;

	ret = pm_runtime_get_sync(sdev->dev);
	if (ret < 0) {
		dev_err(sdev->dev, "error: bytes get failed to resume %d\n",
			ret);
		return ret;
	}

	/* get all the mixer data from DSP */
	snd_sof_ipc_get_comp_data(sdev->ipc, scontrol, SOF_IPC_COMP_GET_DATA,
				  SOF_CTRL_TYPE_DATA_GET, scontrol->cmd);
	size = data->size + sizeof(*data);
	if (size > be->max) {
		dev_err(sdev->dev, "error: DSP sent %zu bytes max is %d\n",
			size, be->max);
		ret = -EINVAL;
		goto out;
	}

	/* copy back to kcontrol */
	memcpy(ucontrol->value.bytes.data, data, size);

out:
	pm_runtime_mark_last_busy(sdev->dev);
	err = pm_runtime_put_autosuspend(sdev->dev);
	if (err < 0)
		dev_err(sdev->dev, "error: bytes get failed to idle %d\n",
			err);
	return ret;
}

int snd_sof_bytes_put(struct snd_kcontrol *kcontrol,
		      struct snd_ctl_elem_value *ucontrol)
{
	struct soc_bytes_ext *be =
		(struct soc_bytes_ext *)kcontrol->private_value;
	struct snd_sof_control *scontrol = be->dobj.private;
	struct snd_sof_dev *sdev = scontrol->sdev;
	struct sof_ipc_ctrl_data *cdata = scontrol->control_data;
	struct sof_abi_hdr *data = cdata->data;
	int ret, err;

	ret = pm_runtime_get_sync(sdev->dev);
	if (ret < 0) {
		dev_err(sdev->dev, "error: bytes put failed to resume %d\n",
			ret);
		return ret;
	}

	if (data->size > be->max) {
		dev_err(sdev->dev, "error: size too big %d bytes max is %d\n",
			data->size, be->max);
		ret = -EINVAL;
		goto out;
	}

	/* copy from kcontrol */
	memcpy(data, ucontrol->value.bytes.data, data->size);

	/* notify DSP of mixer updates */
	snd_sof_ipc_set_comp_data(sdev->ipc, scontrol, SOF_IPC_COMP_SET_DATA,
				  SOF_CTRL_TYPE_DATA_SET, scontrol->cmd);

out:
	pm_runtime_mark_last_busy(sdev->dev);
	err = pm_runtime_put_autosuspend(sdev->dev);
	if (err < 0)
		dev_err(sdev->dev, "error: volume get failed to idle %d\n",
			err);
	return ret;
}

int snd_sof_bytes_ext_put(struct snd_kcontrol *kcontrol,
			  const unsigned int __user *binary_data,
			  unsigned int size)
{
	struct soc_bytes_ext *be =
		(struct soc_bytes_ext *)kcontrol->private_value;
	struct snd_sof_control *scontrol = be->dobj.private;
	struct snd_sof_dev *sdev = scontrol->sdev;
	struct sof_ipc_ctrl_data *cdata = scontrol->control_data;
	u32 header[2];
	int ret;
	int err;
	int length;
	const int max_length = SOF_IPC_MSG_MAX_SIZE -
		sizeof(struct sof_ipc_ctrl_data) -
		sizeof(struct sof_abi_hdr);

	ret = pm_runtime_get_sync(sdev->dev);
	if (ret < 0) {
		dev_err(sdev->dev, "error: bytes put failed to resume %d\n",
			ret);
		return ret;
	}

	/* the first two ints of the bytes data contain a dummy tag
	 * and the size, so copy from the 3rd int
	 * TODO: or should we send the size as well so the firmware
	 * can allocate memory accordingly?
	 */
	if (copy_from_user(&header, binary_data, 2*sizeof(u32)))
		return -EFAULT;

	length = header[1];
	dev_dbg(sdev->dev, "size of bytes put data is %d\n", length);
	if (length > max_length) {
		dev_err(sdev->dev, "error: size is too large, bytes max is %d\n",
			max_length);
		ret = -EINVAL;
		goto out;
	}

	if (copy_from_user(cdata->data->data, binary_data + 2, length))
		return -EFAULT;

	/* set the ABI header values */
	cdata->data->magic = SOF_ABI_MAGIC;
	cdata->data->abi = SOF_ABI_VERSION;
	cdata->data->comp_abi = SOF_ABI_VERSION;

	/* notify DSP of mixer updates */
	snd_sof_ipc_set_comp_data(sdev->ipc, scontrol, SOF_IPC_COMP_SET_DATA,
				  SOF_CTRL_TYPE_DATA_SET, scontrol->cmd);


out:
	pm_runtime_mark_last_busy(sdev->dev);
	err = pm_runtime_put_autosuspend(sdev->dev);
	if (err < 0)
		dev_err(sdev->dev, "error: failed to idle %d\n", err);

	return ret;
}

int snd_sof_bytes_ext_get(struct snd_kcontrol *kcontrol,
			  unsigned int __user *binary_data,
			  unsigned int size)
{
	struct soc_bytes_ext *be =
		(struct soc_bytes_ext *)kcontrol->private_value;
	struct snd_sof_control *scontrol = be->dobj.private;
	struct snd_sof_dev *sdev = scontrol->sdev;
	struct sof_ipc_ctrl_data *cdata = scontrol->control_data;
	unsigned int tag = 0;

	pm_runtime_get_sync(sdev->dev);

	dev_dbg(sdev->dev, "getting data and command is %d\n", scontrol->cmd);
	/* get all the mixer data from DSP */
	snd_sof_ipc_get_comp_data(sdev->ipc, scontrol, SOF_IPC_COMP_GET_DATA,
				  SOF_CTRL_TYPE_DATA_GET, scontrol->cmd);

	/* TODO: replace 252 with actual size */
	if (copy_to_user(binary_data, &tag, sizeof(u32)))
		return -EFAULT;
	if (copy_to_user(binary_data + 1, &size, sizeof(u32)))
		return -EFAULT;
	if (copy_to_user(binary_data + 2, cdata->data->data,
			 SOF_IPC_MSG_MAX_SIZE - sizeof(struct sof_ipc_ctrl_data)))
		return -EFAULT;

	pm_runtime_mark_last_busy(sdev->dev);
	pm_runtime_put_autosuspend(sdev->dev);
	return 0;
}
