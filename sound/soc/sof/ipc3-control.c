// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2021 Intel Corporation. All rights reserved.
//
//

#include "sof-priv.h"
#include "sof-audio.h"
#include "ipc3-priv.h"

/* IPC set()/get() for kcontrols. */
static int sof_ipc3_set_get_kcontrol_data(struct snd_sof_control *scontrol,
					  bool set, bool lock)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scontrol->scomp);
	struct sof_ipc_ctrl_data *cdata = scontrol->ipc_control_data;
	const struct sof_ipc_ops *iops = sdev->ipc->ops;
	enum sof_ipc_ctrl_type ctrl_type;
	struct snd_sof_widget *swidget;
	bool widget_found = false;
	u32 ipc_cmd, msg_bytes;
	int ret = 0;

	list_for_each_entry(swidget, &sdev->widget_list, list) {
		if (swidget->comp_id == scontrol->comp_id) {
			widget_found = true;
			break;
		}
	}

	if (!widget_found) {
		dev_err(sdev->dev, "%s: can't find widget with id %d\n", __func__,
			scontrol->comp_id);
		return -EINVAL;
	}

	if (lock)
		mutex_lock(&swidget->setup_mutex);
	else
		lockdep_assert_held(&swidget->setup_mutex);

	/*
	 * Volatile controls should always be part of static pipelines and the
	 * widget use_count would always be > 0 in this case. For the others,
	 * just return the cached value if the widget is not set up.
	 */
	if (!swidget->use_count)
		goto unlock;

	/*
	 * Select the IPC cmd and the ctrl_type based on the ctrl_cmd and the
	 * direction
	 * Note: SOF_CTRL_TYPE_VALUE_COMP_* is not used and supported currently
	 *	 for ctrl_type
	 */
	if (cdata->cmd == SOF_CTRL_CMD_BINARY) {
		ipc_cmd = set ? SOF_IPC_COMP_SET_DATA : SOF_IPC_COMP_GET_DATA;
		ctrl_type = set ? SOF_CTRL_TYPE_DATA_SET : SOF_CTRL_TYPE_DATA_GET;
	} else {
		ipc_cmd = set ? SOF_IPC_COMP_SET_VALUE : SOF_IPC_COMP_GET_VALUE;
		ctrl_type = set ? SOF_CTRL_TYPE_VALUE_CHAN_SET : SOF_CTRL_TYPE_VALUE_CHAN_GET;
	}

	cdata->rhdr.hdr.cmd = SOF_IPC_GLB_COMP_MSG | ipc_cmd;
	cdata->type = ctrl_type;
	cdata->comp_id = scontrol->comp_id;
	cdata->msg_index = 0;

	/* calculate header and data size */
	switch (cdata->type) {
	case SOF_CTRL_TYPE_VALUE_CHAN_GET:
	case SOF_CTRL_TYPE_VALUE_CHAN_SET:
		cdata->num_elems = scontrol->num_channels;

		msg_bytes = scontrol->num_channels *
			    sizeof(struct sof_ipc_ctrl_value_chan);
		msg_bytes += sizeof(struct sof_ipc_ctrl_data);
		break;
	case SOF_CTRL_TYPE_DATA_GET:
	case SOF_CTRL_TYPE_DATA_SET:
		cdata->num_elems = cdata->data->size;

		msg_bytes = cdata->data->size;
		msg_bytes += sizeof(struct sof_ipc_ctrl_data) +
			     sizeof(struct sof_abi_hdr);
		break;
	default:
		ret = -EINVAL;
		goto unlock;
	}

	cdata->rhdr.hdr.size = msg_bytes;
	cdata->elems_remaining = 0;

	ret = iops->set_get_data(sdev, cdata, cdata->rhdr.hdr.size, set);

unlock:
	if (lock)
		mutex_unlock(&swidget->setup_mutex);

	return ret;
}

static void sof_ipc3_refresh_control(struct snd_sof_control *scontrol)
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
	ret = sof_ipc3_set_get_kcontrol_data(scontrol, false, true);
	if (ret < 0) {
		dev_err(scomp->dev, "Failed to get control data: %d\n", ret);

		/* Set the flag to re-try next time to get the data */
		scontrol->comp_data_dirty = true;
	}
}

static int sof_ipc3_volume_get(struct snd_sof_control *scontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct sof_ipc_ctrl_data *cdata = scontrol->ipc_control_data;
	unsigned int channels = scontrol->num_channels;
	unsigned int i;

	sof_ipc3_refresh_control(scontrol);

	/* read back each channel */
	for (i = 0; i < channels; i++)
		ucontrol->value.integer.value[i] = ipc_to_mixer(cdata->chanv[i].value,
								scontrol->volume_table,
								scontrol->max + 1);

	return 0;
}

static bool sof_ipc3_volume_put(struct snd_sof_control *scontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct sof_ipc_ctrl_data *cdata = scontrol->ipc_control_data;
	struct snd_soc_component *scomp = scontrol->scomp;
	unsigned int channels = scontrol->num_channels;
	unsigned int i;
	bool change = false;

	/* update each channel */
	for (i = 0; i < channels; i++) {
		u32 value = mixer_to_ipc(ucontrol->value.integer.value[i],
					 scontrol->volume_table, scontrol->max + 1);

		change = change || (value != cdata->chanv[i].value);
		cdata->chanv[i].channel = i;
		cdata->chanv[i].value = value;
	}

	/* notify DSP of mixer updates */
	if (pm_runtime_active(scomp->dev)) {
		int ret = sof_ipc3_set_get_kcontrol_data(scontrol, true, true);

		if (ret < 0) {
			dev_err(scomp->dev, "Failed to set mixer updates for %s\n",
				scontrol->name);
			return false;
		}
	}

	return change;
}

static int sof_ipc3_switch_get(struct snd_sof_control *scontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct sof_ipc_ctrl_data *cdata = scontrol->ipc_control_data;
	unsigned int channels = scontrol->num_channels;
	unsigned int i;

	sof_ipc3_refresh_control(scontrol);

	/* read back each channel */
	for (i = 0; i < channels; i++)
		ucontrol->value.integer.value[i] = cdata->chanv[i].value;

	return 0;
}

static bool sof_ipc3_switch_put(struct snd_sof_control *scontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct sof_ipc_ctrl_data *cdata = scontrol->ipc_control_data;
	struct snd_soc_component *scomp = scontrol->scomp;
	unsigned int channels = scontrol->num_channels;
	unsigned int i;
	bool change = false;
	u32 value;

	/* update each channel */
	for (i = 0; i < channels; i++) {
		value = ucontrol->value.integer.value[i];
		change = change || (value != cdata->chanv[i].value);
		cdata->chanv[i].channel = i;
		cdata->chanv[i].value = value;
	}

	/* notify DSP of mixer updates */
	if (pm_runtime_active(scomp->dev)) {
		int ret = sof_ipc3_set_get_kcontrol_data(scontrol, true, true);

		if (ret < 0) {
			dev_err(scomp->dev, "Failed to set mixer updates for %s\n",
				scontrol->name);
			return false;
		}
	}

	return change;
}

static int sof_ipc3_enum_get(struct snd_sof_control *scontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct sof_ipc_ctrl_data *cdata = scontrol->ipc_control_data;
	unsigned int channels = scontrol->num_channels;
	unsigned int i;

	sof_ipc3_refresh_control(scontrol);

	/* read back each channel */
	for (i = 0; i < channels; i++)
		ucontrol->value.enumerated.item[i] = cdata->chanv[i].value;

	return 0;
}

static bool sof_ipc3_enum_put(struct snd_sof_control *scontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct sof_ipc_ctrl_data *cdata = scontrol->ipc_control_data;
	struct snd_soc_component *scomp = scontrol->scomp;
	unsigned int channels = scontrol->num_channels;
	unsigned int i;
	bool change = false;
	u32 value;

	/* update each channel */
	for (i = 0; i < channels; i++) {
		value = ucontrol->value.enumerated.item[i];
		change = change || (value != cdata->chanv[i].value);
		cdata->chanv[i].channel = i;
		cdata->chanv[i].value = value;
	}

	/* notify DSP of enum updates */
	if (pm_runtime_active(scomp->dev)) {
		int ret = sof_ipc3_set_get_kcontrol_data(scontrol, true, true);

		if (ret < 0) {
			dev_err(scomp->dev, "Failed to set enum updates for %s\n",
				scontrol->name);
			return false;
		}
	}

	return change;
}

static int sof_ipc3_bytes_get(struct snd_sof_control *scontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct sof_ipc_ctrl_data *cdata = scontrol->ipc_control_data;
	struct snd_soc_component *scomp = scontrol->scomp;
	struct sof_abi_hdr *data = cdata->data;
	size_t size;

	sof_ipc3_refresh_control(scontrol);

	if (scontrol->max_size > sizeof(ucontrol->value.bytes.data)) {
		dev_err_ratelimited(scomp->dev, "data max %zu exceeds ucontrol data array size\n",
				    scontrol->max_size);
		return -EINVAL;
	}

	/* be->max has been verified to be >= sizeof(struct sof_abi_hdr) */
	if (data->size > scontrol->max_size - sizeof(*data)) {
		dev_err_ratelimited(scomp->dev,
				    "%u bytes of control data is invalid, max is %zu\n",
				    data->size, scontrol->max_size - sizeof(*data));
		return -EINVAL;
	}

	size = data->size + sizeof(*data);

	/* copy back to kcontrol */
	memcpy(ucontrol->value.bytes.data, data, size);

	return 0;
}

static int sof_ipc3_bytes_put(struct snd_sof_control *scontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct sof_ipc_ctrl_data *cdata = scontrol->ipc_control_data;
	struct snd_soc_component *scomp = scontrol->scomp;
	struct sof_abi_hdr *data = cdata->data;
	size_t size;

	if (scontrol->max_size > sizeof(ucontrol->value.bytes.data)) {
		dev_err_ratelimited(scomp->dev, "data max %zu exceeds ucontrol data array size\n",
				    scontrol->max_size);
		return -EINVAL;
	}

	/* scontrol->max_size has been verified to be >= sizeof(struct sof_abi_hdr) */
	if (data->size > scontrol->max_size - sizeof(*data)) {
		dev_err_ratelimited(scomp->dev, "data size too big %u bytes max is %zu\n",
				    data->size, scontrol->max_size - sizeof(*data));
		return -EINVAL;
	}

	size = data->size + sizeof(*data);

	/* copy from kcontrol */
	memcpy(data, ucontrol->value.bytes.data, size);

	/* notify DSP of byte control updates */
	if (pm_runtime_active(scomp->dev))
		return sof_ipc3_set_get_kcontrol_data(scontrol, true, true);

	return 0;
}

static int sof_ipc3_bytes_ext_put(struct snd_sof_control *scontrol,
				  const unsigned int __user *binary_data,
				  unsigned int size)
{
	const struct snd_ctl_tlv __user *tlvd = (const struct snd_ctl_tlv __user *)binary_data;
	struct sof_ipc_ctrl_data *cdata = scontrol->ipc_control_data;
	struct snd_soc_component *scomp = scontrol->scomp;
	struct snd_ctl_tlv header;

	/*
	 * The beginning of bytes data contains a header from where
	 * the length (as bytes) is needed to know the correct copy
	 * length of data from tlvd->tlv.
	 */
	if (copy_from_user(&header, tlvd, sizeof(struct snd_ctl_tlv)))
		return -EFAULT;

	/* make sure TLV info is consistent */
	if (header.length + sizeof(struct snd_ctl_tlv) > size) {
		dev_err_ratelimited(scomp->dev, "Inconsistent TLV, data %d + header %zu > %d\n",
				    header.length, sizeof(struct snd_ctl_tlv), size);
		return -EINVAL;
	}

	/* be->max is coming from topology */
	if (header.length > scontrol->max_size) {
		dev_err_ratelimited(scomp->dev, "Bytes data size %d exceeds max %zu\n",
				    header.length, scontrol->max_size);
		return -EINVAL;
	}

	/* Check that header id matches the command */
	if (header.numid != cdata->cmd) {
		dev_err_ratelimited(scomp->dev, "Incorrect command for bytes put %d\n",
				    header.numid);
		return -EINVAL;
	}

	if (copy_from_user(cdata->data, tlvd->tlv, header.length))
		return -EFAULT;

	if (cdata->data->magic != SOF_ABI_MAGIC) {
		dev_err_ratelimited(scomp->dev, "Wrong ABI magic 0x%08x\n", cdata->data->magic);
		return -EINVAL;
	}

	if (SOF_ABI_VERSION_INCOMPATIBLE(SOF_ABI_VERSION, cdata->data->abi)) {
		dev_err_ratelimited(scomp->dev, "Incompatible ABI version 0x%08x\n",
				    cdata->data->abi);
		return -EINVAL;
	}

	/* be->max has been verified to be >= sizeof(struct sof_abi_hdr) */
	if (cdata->data->size > scontrol->max_size - sizeof(struct sof_abi_hdr)) {
		dev_err_ratelimited(scomp->dev, "Mismatch in ABI data size (truncated?)\n");
		return -EINVAL;
	}

	/* notify DSP of byte control updates */
	if (pm_runtime_active(scomp->dev))
		return sof_ipc3_set_get_kcontrol_data(scontrol, true, true);

	return 0;
}

static int _sof_ipc3_bytes_ext_get(struct snd_sof_control *scontrol,
				   const unsigned int __user *binary_data,
				   unsigned int size, bool from_dsp)
{
	struct snd_ctl_tlv __user *tlvd = (struct snd_ctl_tlv __user *)binary_data;
	struct sof_ipc_ctrl_data *cdata = scontrol->ipc_control_data;
	struct snd_soc_component *scomp = scontrol->scomp;
	struct snd_ctl_tlv header;
	size_t data_size;

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

	/* get all the component data from DSP */
	if (from_dsp) {
		int ret = sof_ipc3_set_get_kcontrol_data(scontrol, false, true);

		if (ret < 0)
			return ret;
	}

	/* check data size doesn't exceed max coming from topology */
	if (cdata->data->size > scontrol->max_size - sizeof(struct sof_abi_hdr)) {
		dev_err_ratelimited(scomp->dev, "User data size %d exceeds max size %zu\n",
				    cdata->data->size,
				    scontrol->max_size - sizeof(struct sof_abi_hdr));
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

static int sof_ipc3_bytes_ext_get(struct snd_sof_control *scontrol,
				  const unsigned int __user *binary_data, unsigned int size)
{
	return _sof_ipc3_bytes_ext_get(scontrol, binary_data, size, false);
}

static int sof_ipc3_bytes_ext_volatile_get(struct snd_sof_control *scontrol,
					   const unsigned int __user *binary_data,
					   unsigned int size)
{
	return _sof_ipc3_bytes_ext_get(scontrol, binary_data, size, true);
}

static void snd_sof_update_control(struct snd_sof_control *scontrol,
				   struct sof_ipc_ctrl_data *cdata)
{
	struct snd_soc_component *scomp = scontrol->scomp;
	struct sof_ipc_ctrl_data *local_cdata;
	int i;

	local_cdata = scontrol->ipc_control_data;

	if (cdata->cmd == SOF_CTRL_CMD_BINARY) {
		if (cdata->num_elems != local_cdata->data->size) {
			dev_err(scomp->dev, "cdata binary size mismatch %u - %u\n",
				cdata->num_elems, local_cdata->data->size);
			return;
		}

		/* copy the new binary data */
		memcpy(local_cdata->data, cdata->data, cdata->num_elems);
	} else if (cdata->num_elems != scontrol->num_channels) {
		dev_err(scomp->dev, "cdata channel count mismatch %u - %d\n",
			cdata->num_elems, scontrol->num_channels);
	} else {
		/* copy the new values */
		for (i = 0; i < cdata->num_elems; i++)
			local_cdata->chanv[i].value = cdata->chanv[i].value;
	}
}

static void sof_ipc3_control_update(struct snd_sof_dev *sdev, void *ipc_control_message)
{
	struct sof_ipc_ctrl_data *cdata = ipc_control_message;
	struct snd_soc_dapm_widget *widget;
	struct snd_sof_control *scontrol;
	struct snd_sof_widget *swidget;
	struct snd_kcontrol *kc = NULL;
	struct soc_mixer_control *sm;
	struct soc_bytes_ext *be;
	size_t expected_size;
	struct soc_enum *se;
	bool found = false;
	int i, type;

	if (cdata->type == SOF_CTRL_TYPE_VALUE_COMP_GET ||
	    cdata->type == SOF_CTRL_TYPE_VALUE_COMP_SET) {
		dev_err(sdev->dev, "Component data is not supported in control notification\n");
		return;
	}

	/* Find the swidget first */
	list_for_each_entry(swidget, &sdev->widget_list, list) {
		if (swidget->comp_id == cdata->comp_id) {
			found = true;
			break;
		}
	}

	if (!found)
		return;

	/* Translate SOF cmd to TPLG type */
	switch (cdata->cmd) {
	case SOF_CTRL_CMD_VOLUME:
	case SOF_CTRL_CMD_SWITCH:
		type = SND_SOC_TPLG_TYPE_MIXER;
		break;
	case SOF_CTRL_CMD_BINARY:
		type = SND_SOC_TPLG_TYPE_BYTES;
		break;
	case SOF_CTRL_CMD_ENUM:
		type = SND_SOC_TPLG_TYPE_ENUM;
		break;
	default:
		dev_err(sdev->dev, "Unknown cmd %u in %s\n", cdata->cmd, __func__);
		return;
	}

	widget = swidget->widget;
	for (i = 0; i < widget->num_kcontrols; i++) {
		/* skip non matching types or non matching indexes within type */
		if (widget->dobj.widget.kcontrol_type[i] == type &&
		    widget->kcontrol_news[i].index == cdata->index) {
			kc = widget->kcontrols[i];
			break;
		}
	}

	if (!kc)
		return;

	switch (cdata->cmd) {
	case SOF_CTRL_CMD_VOLUME:
	case SOF_CTRL_CMD_SWITCH:
		sm = (struct soc_mixer_control *)kc->private_value;
		scontrol = sm->dobj.private;
		break;
	case SOF_CTRL_CMD_BINARY:
		be = (struct soc_bytes_ext *)kc->private_value;
		scontrol = be->dobj.private;
		break;
	case SOF_CTRL_CMD_ENUM:
		se = (struct soc_enum *)kc->private_value;
		scontrol = se->dobj.private;
		break;
	default:
		return;
	}

	expected_size = sizeof(struct sof_ipc_ctrl_data);
	switch (cdata->type) {
	case SOF_CTRL_TYPE_VALUE_CHAN_GET:
	case SOF_CTRL_TYPE_VALUE_CHAN_SET:
		expected_size += cdata->num_elems *
				 sizeof(struct sof_ipc_ctrl_value_chan);
		break;
	case SOF_CTRL_TYPE_DATA_GET:
	case SOF_CTRL_TYPE_DATA_SET:
		expected_size += cdata->num_elems + sizeof(struct sof_abi_hdr);
		break;
	default:
		return;
	}

	if (cdata->rhdr.hdr.size != expected_size) {
		dev_err(sdev->dev, "Component notification size mismatch\n");
		return;
	}

	if (cdata->num_elems)
		/*
		 * The message includes the updated value/data, update the
		 * control's local cache using the received notification
		 */
		snd_sof_update_control(scontrol, cdata);
	else
		/* Mark the scontrol that the value/data is changed in SOF */
		scontrol->comp_data_dirty = true;

	snd_ctl_notify_one(swidget->scomp->card->snd_card, SNDRV_CTL_EVENT_MASK_VALUE, kc, 0);
}

static int sof_ipc3_widget_kcontrol_setup(struct snd_sof_dev *sdev,
					  struct snd_sof_widget *swidget)
{
	struct snd_sof_control *scontrol;
	int ret;

	/* set up all controls for the widget */
	list_for_each_entry(scontrol, &sdev->kcontrol_list, list)
		if (scontrol->comp_id == swidget->comp_id) {
			/* set kcontrol data in DSP */
			ret = sof_ipc3_set_get_kcontrol_data(scontrol, true, false);
			if (ret < 0) {
				dev_err(sdev->dev,
					"kcontrol %d set up failed for widget %s\n",
					scontrol->comp_id, swidget->widget->name);
				return ret;
			}

			/*
			 * Read back the data from the DSP for static widgets.
			 * This is particularly useful for binary kcontrols
			 * associated with static pipeline widgets to initialize
			 * the data size to match that in the DSP.
			 */
			if (swidget->dynamic_pipeline_widget)
				continue;

			ret = sof_ipc3_set_get_kcontrol_data(scontrol, false, false);
			if (ret < 0)
				dev_warn(sdev->dev,
					 "kcontrol %d read failed for widget %s\n",
					 scontrol->comp_id, swidget->widget->name);
		}

	return 0;
}

static int
sof_ipc3_set_up_volume_table(struct snd_sof_control *scontrol, int tlv[SOF_TLV_ITEMS], int size)
{
	int i;

	/* init the volume table */
	scontrol->volume_table = kcalloc(size, sizeof(u32), GFP_KERNEL);
	if (!scontrol->volume_table)
		return -ENOMEM;

	/* populate the volume table */
	for (i = 0; i < size ; i++)
		scontrol->volume_table[i] = vol_compute_gain(i, tlv);

	return 0;
}

const struct sof_ipc_tplg_control_ops tplg_ipc3_control_ops = {
	.volume_put = sof_ipc3_volume_put,
	.volume_get = sof_ipc3_volume_get,
	.switch_put = sof_ipc3_switch_put,
	.switch_get = sof_ipc3_switch_get,
	.enum_put = sof_ipc3_enum_put,
	.enum_get = sof_ipc3_enum_get,
	.bytes_put = sof_ipc3_bytes_put,
	.bytes_get = sof_ipc3_bytes_get,
	.bytes_ext_put = sof_ipc3_bytes_ext_put,
	.bytes_ext_get = sof_ipc3_bytes_ext_get,
	.bytes_ext_volatile_get = sof_ipc3_bytes_ext_volatile_get,
	.update = sof_ipc3_control_update,
	.widget_kcontrol_setup = sof_ipc3_widget_kcontrol_setup,
	.set_up_volume_table = sof_ipc3_set_up_volume_table,
};
