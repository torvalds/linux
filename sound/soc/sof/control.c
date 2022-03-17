// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2018 Intel Corporation. All rights reserved.
//
// Author: Liam Girdwood <liam.r.girdwood@linux.intel.com>
//

/* Mixer Controls */

#include <linux/pm_runtime.h>
#include <linux/leds.h>
#include "sof-priv.h"
#include "sof-audio.h"

static void update_mute_led(struct snd_sof_control *scontrol,
			    struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	int temp = 0;
	int mask;
	int i;

	mask = 1U << snd_ctl_get_ioffidx(kcontrol, &ucontrol->id);

	for (i = 0; i < scontrol->num_channels; i++) {
		if (ucontrol->value.integer.value[i]) {
			temp |= mask;
			break;
		}
	}

	if (temp == scontrol->led_ctl.led_value)
		return;

	scontrol->led_ctl.led_value = temp;

#if IS_REACHABLE(CONFIG_LEDS_TRIGGER_AUDIO)
	if (!scontrol->led_ctl.direction)
		ledtrig_audio_set(LED_AUDIO_MUTE, temp ? LED_OFF : LED_ON);
	else
		ledtrig_audio_set(LED_AUDIO_MICMUTE, temp ? LED_OFF : LED_ON);
#endif
}

static void snd_sof_refresh_control(struct snd_sof_control *scontrol)
{
	struct sof_ipc_ctrl_data *cdata = scontrol->ipc_control_data;
	struct snd_soc_component *scomp = scontrol->scomp;
	int ret;

	if (!scontrol->comp_data_dirty)
		return;

	if (!pm_runtime_active(scomp->dev))
		return;

	/* set the ABI header values */
	cdata->data->magic = SOF_ABI_MAGIC;
	cdata->data->abi = SOF_ABI_VERSION;

	/* refresh the component data from DSP */
	scontrol->comp_data_dirty = false;
	ret = snd_sof_ipc_set_get_comp_data(scontrol, false);
	if (ret < 0) {
		dev_err(scomp->dev, "Failed to get control data: %d\n", ret);

		/* Set the flag to re-try next time to get the data */
		scontrol->comp_data_dirty = true;
	}
}

int snd_sof_volume_get(struct snd_kcontrol *kcontrol,
		       struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *sm = (struct soc_mixer_control *)kcontrol->private_value;
	struct snd_sof_control *scontrol = sm->dobj.private;
	struct snd_soc_component *scomp = scontrol->scomp;
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	const struct sof_ipc_tplg_ops *tplg_ops = sdev->ipc->ops->tplg;

	if (tplg_ops->control->volume_get)
		return tplg_ops->control->volume_get(scontrol, ucontrol);

	return 0;
}

int snd_sof_volume_put(struct snd_kcontrol *kcontrol,
		       struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *sm = (struct soc_mixer_control *)kcontrol->private_value;
	struct snd_sof_control *scontrol = sm->dobj.private;
	struct snd_soc_component *scomp = scontrol->scomp;
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	const struct sof_ipc_tplg_ops *tplg_ops = sdev->ipc->ops->tplg;

	if (tplg_ops->control->volume_put)
		return tplg_ops->control->volume_put(scontrol, ucontrol);

	return false;
}

int snd_sof_volume_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	struct soc_mixer_control *sm = (struct soc_mixer_control *)kcontrol->private_value;
	struct snd_sof_control *scontrol = sm->dobj.private;
	unsigned int channels = scontrol->num_channels;
	int platform_max;

	if (!sm->platform_max)
		sm->platform_max = sm->max;
	platform_max = sm->platform_max;

	if (platform_max == 1 && !strstr(kcontrol->id.name, " Volume"))
		uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	else
		uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;

	uinfo->count = channels;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = platform_max - sm->min;
	return 0;
}

int snd_sof_switch_get(struct snd_kcontrol *kcontrol,
		       struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *sm = (struct soc_mixer_control *)kcontrol->private_value;
	struct snd_sof_control *scontrol = sm->dobj.private;
	struct snd_soc_component *scomp = scontrol->scomp;
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	const struct sof_ipc_tplg_ops *tplg_ops = sdev->ipc->ops->tplg;

	if (tplg_ops->control->switch_get)
		return tplg_ops->control->switch_get(scontrol, ucontrol);

	return 0;
}

int snd_sof_switch_put(struct snd_kcontrol *kcontrol,
		       struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *sm = (struct soc_mixer_control *)kcontrol->private_value;
	struct snd_sof_control *scontrol = sm->dobj.private;
	struct snd_soc_component *scomp = scontrol->scomp;
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	const struct sof_ipc_tplg_ops *tplg_ops = sdev->ipc->ops->tplg;

	if (scontrol->led_ctl.use_led)
		update_mute_led(scontrol, kcontrol, ucontrol);

	if (tplg_ops->control->switch_put)
		return tplg_ops->control->switch_put(scontrol, ucontrol);

	return false;
}

int snd_sof_enum_get(struct snd_kcontrol *kcontrol,
		     struct snd_ctl_elem_value *ucontrol)
{
	struct soc_enum *se = (struct soc_enum *)kcontrol->private_value;
	struct snd_sof_control *scontrol = se->dobj.private;
	struct snd_soc_component *scomp = scontrol->scomp;
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	const struct sof_ipc_tplg_ops *tplg_ops = sdev->ipc->ops->tplg;

	if (tplg_ops->control->enum_get)
		return tplg_ops->control->enum_get(scontrol, ucontrol);

	return 0;
}

int snd_sof_enum_put(struct snd_kcontrol *kcontrol,
		     struct snd_ctl_elem_value *ucontrol)
{
	struct soc_enum *se = (struct soc_enum *)kcontrol->private_value;
	struct snd_sof_control *scontrol = se->dobj.private;
	struct snd_soc_component *scomp = scontrol->scomp;
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	const struct sof_ipc_tplg_ops *tplg_ops = sdev->ipc->ops->tplg;

	if (tplg_ops->control->enum_put)
		return tplg_ops->control->enum_put(scontrol, ucontrol);

	return false;
}

int snd_sof_bytes_get(struct snd_kcontrol *kcontrol,
		      struct snd_ctl_elem_value *ucontrol)
{
	struct soc_bytes_ext *be =
		(struct soc_bytes_ext *)kcontrol->private_value;
	struct snd_sof_control *scontrol = be->dobj.private;
	struct snd_soc_component *scomp = scontrol->scomp;
	struct sof_ipc_ctrl_data *cdata = scontrol->ipc_control_data;
	struct sof_abi_hdr *data = cdata->data;
	size_t size;

	snd_sof_refresh_control(scontrol);

	if (be->max > sizeof(ucontrol->value.bytes.data)) {
		dev_err_ratelimited(scomp->dev,
				    "error: data max %d exceeds ucontrol data array size\n",
				    be->max);
		return -EINVAL;
	}

	/* be->max has been verified to be >= sizeof(struct sof_abi_hdr) */
	if (data->size > be->max - sizeof(*data)) {
		dev_err_ratelimited(scomp->dev,
				    "error: %u bytes of control data is invalid, max is %zu\n",
				    data->size, be->max - sizeof(*data));
		return -EINVAL;
	}

	size = data->size + sizeof(*data);

	/* copy back to kcontrol */
	memcpy(ucontrol->value.bytes.data, data, size);

	return 0;
}

int snd_sof_bytes_put(struct snd_kcontrol *kcontrol,
		      struct snd_ctl_elem_value *ucontrol)
{
	struct soc_bytes_ext *be =
		(struct soc_bytes_ext *)kcontrol->private_value;
	struct snd_sof_control *scontrol = be->dobj.private;
	struct snd_soc_component *scomp = scontrol->scomp;
	struct sof_ipc_ctrl_data *cdata = scontrol->ipc_control_data;
	struct sof_abi_hdr *data = cdata->data;
	size_t size;

	if (be->max > sizeof(ucontrol->value.bytes.data)) {
		dev_err_ratelimited(scomp->dev,
				    "error: data max %d exceeds ucontrol data array size\n",
				    be->max);
		return -EINVAL;
	}

	/* be->max has been verified to be >= sizeof(struct sof_abi_hdr) */
	if (data->size > be->max - sizeof(*data)) {
		dev_err_ratelimited(scomp->dev,
				    "error: data size too big %u bytes max is %zu\n",
				    data->size, be->max - sizeof(*data));
		return -EINVAL;
	}

	size = data->size + sizeof(*data);

	/* copy from kcontrol */
	memcpy(data, ucontrol->value.bytes.data, size);

	/* notify DSP of byte control updates */
	if (pm_runtime_active(scomp->dev))
		snd_sof_ipc_set_get_comp_data(scontrol, true);

	return 0;
}

int snd_sof_bytes_ext_put(struct snd_kcontrol *kcontrol,
			  const unsigned int __user *binary_data,
			  unsigned int size)
{
	struct soc_bytes_ext *be =
		(struct soc_bytes_ext *)kcontrol->private_value;
	struct snd_sof_control *scontrol = be->dobj.private;
	struct snd_soc_component *scomp = scontrol->scomp;
	struct sof_ipc_ctrl_data *cdata = scontrol->ipc_control_data;
	struct snd_ctl_tlv header;
	const struct snd_ctl_tlv __user *tlvd =
		(const struct snd_ctl_tlv __user *)binary_data;

	/* make sure we have at least a header */
	if (size < sizeof(struct snd_ctl_tlv))
		return -EINVAL;

	/*
	 * The beginning of bytes data contains a header from where
	 * the length (as bytes) is needed to know the correct copy
	 * length of data from tlvd->tlv.
	 */
	if (copy_from_user(&header, tlvd, sizeof(struct snd_ctl_tlv)))
		return -EFAULT;

	/* make sure TLV info is consistent */
	if (header.length + sizeof(struct snd_ctl_tlv) > size) {
		dev_err_ratelimited(scomp->dev, "error: inconsistent TLV, data %d + header %zu > %d\n",
				    header.length, sizeof(struct snd_ctl_tlv), size);
		return -EINVAL;
	}

	/* be->max is coming from topology */
	if (header.length > be->max) {
		dev_err_ratelimited(scomp->dev, "error: Bytes data size %d exceeds max %d.\n",
				    header.length, be->max);
		return -EINVAL;
	}

	/* Check that header id matches the command */
	if (header.numid != cdata->cmd) {
		dev_err_ratelimited(scomp->dev,
				    "error: incorrect numid %d\n",
				    header.numid);
		return -EINVAL;
	}

	if (copy_from_user(cdata->data, tlvd->tlv, header.length))
		return -EFAULT;

	if (cdata->data->magic != SOF_ABI_MAGIC) {
		dev_err_ratelimited(scomp->dev,
				    "error: Wrong ABI magic 0x%08x.\n",
				    cdata->data->magic);
		return -EINVAL;
	}

	if (SOF_ABI_VERSION_INCOMPATIBLE(SOF_ABI_VERSION, cdata->data->abi)) {
		dev_err_ratelimited(scomp->dev, "error: Incompatible ABI version 0x%08x.\n",
				    cdata->data->abi);
		return -EINVAL;
	}

	/* be->max has been verified to be >= sizeof(struct sof_abi_hdr) */
	if (cdata->data->size > be->max - sizeof(struct sof_abi_hdr)) {
		dev_err_ratelimited(scomp->dev, "error: Mismatch in ABI data size (truncated?).\n");
		return -EINVAL;
	}

	/* notify DSP of byte control updates */
	if (pm_runtime_active(scomp->dev))
		snd_sof_ipc_set_get_comp_data(scontrol, true);

	return 0;
}

int snd_sof_bytes_ext_volatile_get(struct snd_kcontrol *kcontrol, unsigned int __user *binary_data,
				   unsigned int size)
{
	struct soc_bytes_ext *be = (struct soc_bytes_ext *)kcontrol->private_value;
	struct snd_sof_control *scontrol = be->dobj.private;
	struct snd_soc_component *scomp = scontrol->scomp;
	struct sof_ipc_ctrl_data *cdata = scontrol->ipc_control_data;
	struct snd_ctl_tlv header;
	struct snd_ctl_tlv __user *tlvd = (struct snd_ctl_tlv __user *)binary_data;
	size_t data_size;
	int ret;
	int err;

	/*
	 * Decrement the limit by ext bytes header size to
	 * ensure the user space buffer is not exceeded.
	 */
	if (size < sizeof(struct snd_ctl_tlv))
		return -ENOSPC;
	size -= sizeof(struct snd_ctl_tlv);

	ret = pm_runtime_get_sync(scomp->dev);
	if (ret < 0 && ret != -EACCES) {
		dev_err_ratelimited(scomp->dev, "error: bytes_ext get failed to resume %d\n", ret);
		pm_runtime_put_noidle(scomp->dev);
		return ret;
	}

	/* set the ABI header values */
	cdata->data->magic = SOF_ABI_MAGIC;
	cdata->data->abi = SOF_ABI_VERSION;
	/* get all the component data from DSP */
	ret = snd_sof_ipc_set_get_comp_data(scontrol, false);
	if (ret < 0)
		goto out;

	/* check data size doesn't exceed max coming from topology */
	if (cdata->data->size > be->max - sizeof(struct sof_abi_hdr)) {
		dev_err_ratelimited(scomp->dev, "error: user data size %d exceeds max size %zu.\n",
				    cdata->data->size,
				    be->max - sizeof(struct sof_abi_hdr));
		ret = -EINVAL;
		goto out;
	}

	data_size = cdata->data->size + sizeof(struct sof_abi_hdr);

	/* make sure we don't exceed size provided by user space for data */
	if (data_size > size) {
		ret = -ENOSPC;
		goto out;
	}

	header.numid = cdata->cmd;
	header.length = data_size;
	if (copy_to_user(tlvd, &header, sizeof(struct snd_ctl_tlv))) {
		ret = -EFAULT;
		goto out;
	}

	if (copy_to_user(tlvd->tlv, cdata->data, data_size))
		ret = -EFAULT;
out:
	pm_runtime_mark_last_busy(scomp->dev);
	err = pm_runtime_put_autosuspend(scomp->dev);
	if (err < 0)
		dev_err_ratelimited(scomp->dev, "error: bytes_ext get failed to idle %d\n", err);

	return ret;
}

int snd_sof_bytes_ext_get(struct snd_kcontrol *kcontrol,
			  unsigned int __user *binary_data,
			  unsigned int size)
{
	struct soc_bytes_ext *be =
		(struct soc_bytes_ext *)kcontrol->private_value;
	struct snd_sof_control *scontrol = be->dobj.private;
	struct snd_soc_component *scomp = scontrol->scomp;
	struct sof_ipc_ctrl_data *cdata = scontrol->ipc_control_data;
	struct snd_ctl_tlv header;
	struct snd_ctl_tlv __user *tlvd =
		(struct snd_ctl_tlv __user *)binary_data;
	size_t data_size;

	snd_sof_refresh_control(scontrol);

	/*
	 * Decrement the limit by ext bytes header size to
	 * ensure the user space buffer is not exceeded.
	 */
	if (size < sizeof(struct snd_ctl_tlv))
		return -ENOSPC;
	size -= sizeof(struct snd_ctl_tlv);

	/* set the ABI header values */
	cdata->data->magic = SOF_ABI_MAGIC;
	cdata->data->abi = SOF_ABI_VERSION;

	/* check data size doesn't exceed max coming from topology */
	if (cdata->data->size > be->max - sizeof(struct sof_abi_hdr)) {
		dev_err_ratelimited(scomp->dev, "error: user data size %d exceeds max size %zu.\n",
				    cdata->data->size,
				    be->max - sizeof(struct sof_abi_hdr));
		return -EINVAL;
	}

	data_size = cdata->data->size + sizeof(struct sof_abi_hdr);

	/* make sure we don't exceed size provided by user space for data */
	if (data_size > size)
		return -ENOSPC;

	header.numid = cdata->cmd;
	header.length = data_size;
	if (copy_to_user(tlvd, &header, sizeof(struct snd_ctl_tlv)))
		return -EFAULT;

	if (copy_to_user(tlvd->tlv, cdata->data, data_size))
		return -EFAULT;

	return 0;
}
