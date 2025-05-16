// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2022 Intel Corporation
//
//

#include "sof-priv.h"
#include "sof-audio.h"
#include "ipc4-priv.h"
#include "ipc4-topology.h"

static int sof_ipc4_set_get_kcontrol_data(struct snd_sof_control *scontrol,
					  bool set, bool lock)
{
	struct sof_ipc4_control_data *cdata = scontrol->ipc_control_data;
	struct snd_soc_component *scomp = scontrol->scomp;
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	const struct sof_ipc_ops *iops = sdev->ipc->ops;
	struct sof_ipc4_msg *msg = &cdata->msg;
	struct snd_sof_widget *swidget;
	bool widget_found = false;
	int ret = 0;

	/* find widget associated with the control */
	list_for_each_entry(swidget, &sdev->widget_list, list) {
		if (swidget->comp_id == scontrol->comp_id) {
			widget_found = true;
			break;
		}
	}

	if (!widget_found) {
		dev_err(scomp->dev, "Failed to find widget for kcontrol %s\n", scontrol->name);
		return -ENOENT;
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

	msg->primary &= ~SOF_IPC4_MOD_INSTANCE_MASK;
	msg->primary |= SOF_IPC4_MOD_INSTANCE(swidget->instance_id);

	ret = iops->set_get_data(sdev, msg, msg->data_size, set);
	if (!set)
		goto unlock;

	/* It is a set-data operation, and we have a valid backup that we can restore */
	if (ret < 0) {
		if (!scontrol->old_ipc_control_data)
			goto unlock;
		/*
		 * Current ipc_control_data is not valid, we use the last known good
		 * configuration
		 */
		memcpy(scontrol->ipc_control_data, scontrol->old_ipc_control_data,
		       scontrol->max_size);
		kfree(scontrol->old_ipc_control_data);
		scontrol->old_ipc_control_data = NULL;
		/* Send the last known good configuration to firmware */
		ret = iops->set_get_data(sdev, msg, msg->data_size, set);
		if (ret < 0)
			goto unlock;
	}

unlock:
	if (lock)
		mutex_unlock(&swidget->setup_mutex);

	return ret;
}

static int
sof_ipc4_set_volume_data(struct snd_sof_dev *sdev, struct snd_sof_widget *swidget,
			 struct snd_sof_control *scontrol, bool lock)
{
	struct sof_ipc4_control_data *cdata = scontrol->ipc_control_data;
	struct sof_ipc4_gain *gain = swidget->private;
	struct sof_ipc4_msg *msg = &cdata->msg;
	struct sof_ipc4_gain_params params;
	bool all_channels_equal = true;
	u32 value;
	int ret, i;

	/* check if all channel values are equal */
	value = cdata->chanv[0].value;
	for (i = 1; i < scontrol->num_channels; i++) {
		if (cdata->chanv[i].value != value) {
			all_channels_equal = false;
			break;
		}
	}

	/*
	 * notify DSP with a single IPC message if all channel values are equal. Otherwise send
	 * a separate IPC for each channel.
	 */
	for (i = 0; i < scontrol->num_channels; i++) {
		if (all_channels_equal) {
			params.channels = SOF_IPC4_GAIN_ALL_CHANNELS_MASK;
			params.init_val = cdata->chanv[0].value;
		} else {
			params.channels = cdata->chanv[i].channel;
			params.init_val = cdata->chanv[i].value;
		}

		/* set curve type and duration from topology */
		params.curve_duration_l = gain->data.params.curve_duration_l;
		params.curve_duration_h = gain->data.params.curve_duration_h;
		params.curve_type = gain->data.params.curve_type;

		msg->data_ptr = &params;
		msg->data_size = sizeof(params);

		ret = sof_ipc4_set_get_kcontrol_data(scontrol, true, lock);
		msg->data_ptr = NULL;
		msg->data_size = 0;
		if (ret < 0) {
			dev_err(sdev->dev, "Failed to set volume update for %s\n",
				scontrol->name);
			return ret;
		}

		if (all_channels_equal)
			break;
	}

	return 0;
}

static bool sof_ipc4_volume_put(struct snd_sof_control *scontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct sof_ipc4_control_data *cdata = scontrol->ipc_control_data;
	struct snd_soc_component *scomp = scontrol->scomp;
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	unsigned int channels = scontrol->num_channels;
	struct snd_sof_widget *swidget;
	bool widget_found = false;
	bool change = false;
	unsigned int i;
	int ret;

	/* update each channel */
	for (i = 0; i < channels; i++) {
		u32 value = mixer_to_ipc(ucontrol->value.integer.value[i],
					 scontrol->volume_table, scontrol->max + 1);

		change = change || (value != cdata->chanv[i].value);
		cdata->chanv[i].channel = i;
		cdata->chanv[i].value = value;
	}

	if (!pm_runtime_active(scomp->dev))
		return change;

	/* find widget associated with the control */
	list_for_each_entry(swidget, &sdev->widget_list, list) {
		if (swidget->comp_id == scontrol->comp_id) {
			widget_found = true;
			break;
		}
	}

	if (!widget_found) {
		dev_err(scomp->dev, "Failed to find widget for kcontrol %s\n", scontrol->name);
		return false;
	}

	ret = sof_ipc4_set_volume_data(sdev, swidget, scontrol, true);
	if (ret < 0)
		return false;

	return change;
}

static int sof_ipc4_volume_get(struct snd_sof_control *scontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct sof_ipc4_control_data *cdata = scontrol->ipc_control_data;
	unsigned int channels = scontrol->num_channels;
	unsigned int i;

	for (i = 0; i < channels; i++)
		ucontrol->value.integer.value[i] = ipc_to_mixer(cdata->chanv[i].value,
								scontrol->volume_table,
								scontrol->max + 1);

	return 0;
}

static int
sof_ipc4_set_generic_control_data(struct snd_sof_dev *sdev,
				  struct snd_sof_widget *swidget,
				  struct snd_sof_control *scontrol, bool lock)
{
	struct sof_ipc4_control_data *cdata = scontrol->ipc_control_data;
	struct sof_ipc4_control_msg_payload *data;
	struct sof_ipc4_msg *msg = &cdata->msg;
	size_t data_size;
	unsigned int i;
	int ret;

	data_size = struct_size(data, chanv, scontrol->num_channels);
	data = kzalloc(data_size, GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->id = cdata->index;
	data->num_elems = scontrol->num_channels;
	for (i = 0; i < scontrol->num_channels; i++) {
		data->chanv[i].channel = cdata->chanv[i].channel;
		data->chanv[i].value = cdata->chanv[i].value;
	}

	msg->data_ptr = data;
	msg->data_size = data_size;

	ret = sof_ipc4_set_get_kcontrol_data(scontrol, true, lock);
	msg->data_ptr = NULL;
	msg->data_size = 0;
	if (ret < 0)
		dev_err(sdev->dev, "Failed to set control update for %s\n",
			scontrol->name);

	kfree(data);

	return ret;
}

static void sof_ipc4_refresh_generic_control(struct snd_sof_control *scontrol)
{
	struct sof_ipc4_control_data *cdata = scontrol->ipc_control_data;
	struct snd_soc_component *scomp = scontrol->scomp;
	struct sof_ipc4_control_msg_payload *data;
	struct sof_ipc4_msg *msg = &cdata->msg;
	size_t data_size;
	unsigned int i;
	int ret;

	if (!scontrol->comp_data_dirty)
		return;

	if (!pm_runtime_active(scomp->dev))
		return;

	data_size = struct_size(data, chanv, scontrol->num_channels);
	data = kmalloc(data_size, GFP_KERNEL);
	if (!data)
		return;

	data->id = cdata->index;
	data->num_elems = scontrol->num_channels;
	msg->data_ptr = data;
	msg->data_size = data_size;

	scontrol->comp_data_dirty = false;
	ret = sof_ipc4_set_get_kcontrol_data(scontrol, false, true);
	msg->data_ptr = NULL;
	msg->data_size = 0;
	if (!ret) {
		for (i = 0; i < scontrol->num_channels; i++) {
			cdata->chanv[i].channel = data->chanv[i].channel;
			cdata->chanv[i].value = data->chanv[i].value;
		}
	} else {
		dev_err(scomp->dev, "Failed to read control data for %s\n",
			scontrol->name);
		scontrol->comp_data_dirty = true;
	}

	kfree(data);
}

static bool sof_ipc4_switch_put(struct snd_sof_control *scontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct sof_ipc4_control_data *cdata = scontrol->ipc_control_data;
	struct snd_soc_component *scomp = scontrol->scomp;
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	struct snd_sof_widget *swidget;
	bool widget_found = false;
	bool change = false;
	unsigned int i;
	u32 value;
	int ret;

	/* update each channel */
	for (i = 0; i < scontrol->num_channels; i++) {
		value = ucontrol->value.integer.value[i];
		change = change || (value != cdata->chanv[i].value);
		cdata->chanv[i].channel = i;
		cdata->chanv[i].value = value;
	}

	if (!pm_runtime_active(scomp->dev))
		return change;

	/* find widget associated with the control */
	list_for_each_entry(swidget, &sdev->widget_list, list) {
		if (swidget->comp_id == scontrol->comp_id) {
			widget_found = true;
			break;
		}
	}

	if (!widget_found) {
		dev_err(scomp->dev, "Failed to find widget for kcontrol %s\n", scontrol->name);
		return false;
	}

	ret = sof_ipc4_set_generic_control_data(sdev, swidget, scontrol, true);
	if (ret < 0)
		return false;

	return change;
}

static int sof_ipc4_switch_get(struct snd_sof_control *scontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct sof_ipc4_control_data *cdata = scontrol->ipc_control_data;
	unsigned int i;

	sof_ipc4_refresh_generic_control(scontrol);

	/* read back each channel */
	for (i = 0; i < scontrol->num_channels; i++)
		ucontrol->value.integer.value[i] = cdata->chanv[i].value;

	return 0;
}

static bool sof_ipc4_enum_put(struct snd_sof_control *scontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct sof_ipc4_control_data *cdata = scontrol->ipc_control_data;
	struct snd_soc_component *scomp = scontrol->scomp;
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	struct snd_sof_widget *swidget;
	bool widget_found = false;
	bool change = false;
	unsigned int i;
	u32 value;
	int ret;

	/* update each channel */
	for (i = 0; i < scontrol->num_channels; i++) {
		value = ucontrol->value.enumerated.item[i];
		change = change || (value != cdata->chanv[i].value);
		cdata->chanv[i].channel = i;
		cdata->chanv[i].value = value;
	}

	if (!pm_runtime_active(scomp->dev))
		return change;

	/* find widget associated with the control */
	list_for_each_entry(swidget, &sdev->widget_list, list) {
		if (swidget->comp_id == scontrol->comp_id) {
			widget_found = true;
			break;
		}
	}

	if (!widget_found) {
		dev_err(scomp->dev, "Failed to find widget for kcontrol %s\n", scontrol->name);
		return false;
	}

	ret = sof_ipc4_set_generic_control_data(sdev, swidget, scontrol, true);
	if (ret < 0)
		return false;

	return change;
}

static int sof_ipc4_enum_get(struct snd_sof_control *scontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct sof_ipc4_control_data *cdata = scontrol->ipc_control_data;
	unsigned int i;

	sof_ipc4_refresh_generic_control(scontrol);

	/* read back each channel */
	for (i = 0; i < scontrol->num_channels; i++)
		ucontrol->value.enumerated.item[i] = cdata->chanv[i].value;

	return 0;
}

static int sof_ipc4_set_get_bytes_data(struct snd_sof_dev *sdev,
				       struct snd_sof_control *scontrol,
				       bool set, bool lock)
{
	struct sof_ipc4_control_data *cdata = scontrol->ipc_control_data;
	struct sof_abi_hdr *data = cdata->data;
	struct sof_ipc4_msg *msg = &cdata->msg;
	int ret = 0;

	/* Send the new data to the firmware only if it is powered up */
	if (set && !pm_runtime_active(sdev->dev))
		return 0;

	msg->extension = SOF_IPC4_MOD_EXT_MSG_PARAM_ID(data->type);

	msg->data_ptr = data->data;
	msg->data_size = data->size;

	ret = sof_ipc4_set_get_kcontrol_data(scontrol, set, lock);
	if (ret < 0)
		dev_err(sdev->dev, "Failed to %s for %s\n",
			set ? "set bytes update" : "get bytes",
			scontrol->name);

	msg->data_ptr = NULL;
	msg->data_size = 0;

	return ret;
}

static int sof_ipc4_bytes_put(struct snd_sof_control *scontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct sof_ipc4_control_data *cdata = scontrol->ipc_control_data;
	struct snd_soc_component *scomp = scontrol->scomp;
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	struct sof_abi_hdr *data = cdata->data;
	size_t size;

	if (scontrol->max_size > sizeof(ucontrol->value.bytes.data)) {
		dev_err_ratelimited(scomp->dev,
				    "data max %zu exceeds ucontrol data array size\n",
				    scontrol->max_size);
		return -EINVAL;
	}

	/* scontrol->max_size has been verified to be >= sizeof(struct sof_abi_hdr) */
	if (data->size > scontrol->max_size - sizeof(*data)) {
		dev_err_ratelimited(scomp->dev,
				    "data size too big %u bytes max is %zu\n",
				    data->size, scontrol->max_size - sizeof(*data));
		return -EINVAL;
	}

	size = data->size + sizeof(*data);

	/* copy from kcontrol */
	memcpy(data, ucontrol->value.bytes.data, size);

	sof_ipc4_set_get_bytes_data(sdev, scontrol, true, true);

	return 0;
}

static int sof_ipc4_bytes_get(struct snd_sof_control *scontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct sof_ipc4_control_data *cdata = scontrol->ipc_control_data;
	struct snd_soc_component *scomp = scontrol->scomp;
	struct sof_abi_hdr *data = cdata->data;
	size_t size;

	if (scontrol->max_size > sizeof(ucontrol->value.bytes.data)) {
		dev_err_ratelimited(scomp->dev, "data max %zu exceeds ucontrol data array size\n",
				    scontrol->max_size);
		return -EINVAL;
	}

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

static int sof_ipc4_bytes_ext_put(struct snd_sof_control *scontrol,
				  const unsigned int __user *binary_data,
				  unsigned int size)
{
	struct snd_ctl_tlv __user *tlvd = (struct snd_ctl_tlv __user *)binary_data;
	struct sof_ipc4_control_data *cdata = scontrol->ipc_control_data;
	struct snd_soc_component *scomp = scontrol->scomp;
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	struct sof_abi_hdr *data = cdata->data;
	struct sof_abi_hdr abi_hdr;
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
		dev_err_ratelimited(scomp->dev,
				    "Inconsistent TLV, data %d + header %zu > %d\n",
				    header.length, sizeof(struct snd_ctl_tlv), size);
		return -EINVAL;
	}

	/* be->max is coming from topology */
	if (header.length > scontrol->max_size) {
		dev_err_ratelimited(scomp->dev,
				    "Bytes data size %d exceeds max %zu\n",
				    header.length, scontrol->max_size);
		return -EINVAL;
	}

	/* Check header id */
	if (header.numid != SOF_CTRL_CMD_BINARY) {
		dev_err_ratelimited(scomp->dev,
				    "Incorrect numid for bytes put %d\n",
				    header.numid);
		return -EINVAL;
	}

	/* Verify the ABI header first */
	if (copy_from_user(&abi_hdr, tlvd->tlv, sizeof(abi_hdr)))
		return -EFAULT;

	if (abi_hdr.magic != SOF_IPC4_ABI_MAGIC) {
		dev_err_ratelimited(scomp->dev, "Wrong ABI magic 0x%08x\n",
				    abi_hdr.magic);
		return -EINVAL;
	}

	if (abi_hdr.size > scontrol->max_size - sizeof(abi_hdr)) {
		dev_err_ratelimited(scomp->dev,
				    "%u bytes of control data is invalid, max is %zu\n",
				    abi_hdr.size, scontrol->max_size - sizeof(abi_hdr));
		return -EINVAL;
	}

	if (!scontrol->old_ipc_control_data) {
		/* Create a backup of the current, valid bytes control */
		scontrol->old_ipc_control_data = kmemdup(scontrol->ipc_control_data,
							 scontrol->max_size, GFP_KERNEL);
		if (!scontrol->old_ipc_control_data)
			return -ENOMEM;
	}

	/* Copy the whole binary data which includes the ABI header and the payload */
	if (copy_from_user(data, tlvd->tlv, header.length)) {
		memcpy(scontrol->ipc_control_data, scontrol->old_ipc_control_data,
		       scontrol->max_size);
		kfree(scontrol->old_ipc_control_data);
		scontrol->old_ipc_control_data = NULL;
		return -EFAULT;
	}

	return sof_ipc4_set_get_bytes_data(sdev, scontrol, true, true);
}

static int _sof_ipc4_bytes_ext_get(struct snd_sof_control *scontrol,
				   const unsigned int __user *binary_data,
				   unsigned int size, bool from_dsp)
{
	struct snd_ctl_tlv __user *tlvd = (struct snd_ctl_tlv __user *)binary_data;
	struct sof_ipc4_control_data *cdata = scontrol->ipc_control_data;
	struct snd_soc_component *scomp = scontrol->scomp;
	struct sof_abi_hdr *data = cdata->data;
	struct snd_ctl_tlv header;
	size_t data_size;

	/*
	 * Decrement the limit by ext bytes header size to ensure the user space
	 * buffer is not exceeded.
	 */
	if (size < sizeof(struct snd_ctl_tlv))
		return -ENOSPC;

	size -= sizeof(struct snd_ctl_tlv);

	/* get all the component data from DSP */
	if (from_dsp) {
		struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
		int ret = sof_ipc4_set_get_bytes_data(sdev, scontrol, false, true);

		if (ret < 0)
			return ret;

		/* Set the ABI magic (if the control is not initialized) */
		data->magic = SOF_IPC4_ABI_MAGIC;
	}

	if (data->size > scontrol->max_size - sizeof(*data)) {
		dev_err_ratelimited(scomp->dev,
				    "%u bytes of control data is invalid, max is %zu\n",
				    data->size, scontrol->max_size - sizeof(*data));
		return -EINVAL;
	}

	data_size = data->size + sizeof(struct sof_abi_hdr);

	/* make sure we don't exceed size provided by user space for data */
	if (data_size > size)
		return -ENOSPC;

	/* Set header id and length */
	header.numid = SOF_CTRL_CMD_BINARY;
	header.length = data_size;

	if (copy_to_user(tlvd, &header, sizeof(struct snd_ctl_tlv)))
		return -EFAULT;

	if (copy_to_user(tlvd->tlv, data, data_size))
		return -EFAULT;

	return 0;
}

static int sof_ipc4_bytes_ext_get(struct snd_sof_control *scontrol,
				  const unsigned int __user *binary_data,
				  unsigned int size)
{
	return _sof_ipc4_bytes_ext_get(scontrol, binary_data, size, false);
}

static int sof_ipc4_bytes_ext_volatile_get(struct snd_sof_control *scontrol,
					   const unsigned int __user *binary_data,
					   unsigned int size)
{
	return _sof_ipc4_bytes_ext_get(scontrol, binary_data, size, true);
}

static int
sof_ipc4_volsw_setup(struct snd_sof_dev *sdev, struct snd_sof_widget *swidget,
		     struct snd_sof_control *scontrol)
{
	if (scontrol->max == 1)
		return sof_ipc4_set_generic_control_data(sdev, swidget, scontrol, false);

	return sof_ipc4_set_volume_data(sdev, swidget, scontrol, false);
}

#define PARAM_ID_FROM_EXTENSION(_ext)	(((_ext) & SOF_IPC4_MOD_EXT_MSG_PARAM_ID_MASK)	\
					 >> SOF_IPC4_MOD_EXT_MSG_PARAM_ID_SHIFT)

static void sof_ipc4_control_update(struct snd_sof_dev *sdev, void *ipc_message)
{
	struct sof_ipc4_msg *ipc4_msg = ipc_message;
	struct sof_ipc4_notify_module_data *ndata = ipc4_msg->data_ptr;
	struct sof_ipc4_control_msg_payload *msg_data;
	struct sof_ipc4_control_data *cdata;
	struct snd_soc_dapm_widget *widget;
	struct snd_sof_control *scontrol;
	struct snd_sof_widget *swidget;
	struct snd_kcontrol *kc = NULL;
	bool scontrol_found = false;
	u32 event_param_id;
	int i, type;

	if (ndata->event_data_size < sizeof(*msg_data)) {
		dev_err(sdev->dev,
			"%s: Invalid event data size for module %u.%u: %u\n",
			__func__, ndata->module_id, ndata->instance_id,
			ndata->event_data_size);
		return;
	}

	event_param_id = ndata->event_id & SOF_IPC4_NOTIFY_MODULE_EVENTID_ALSA_PARAMID_MASK;
	switch (event_param_id) {
	case SOF_IPC4_SWITCH_CONTROL_PARAM_ID:
		type = SND_SOC_TPLG_TYPE_MIXER;
		break;
	case SOF_IPC4_ENUM_CONTROL_PARAM_ID:
		type = SND_SOC_TPLG_TYPE_ENUM;
		break;
	default:
		dev_err(sdev->dev,
			"%s: Invalid control type for module %u.%u: %u\n",
			__func__, ndata->module_id, ndata->instance_id,
			event_param_id);
		return;
	}

	/* Find the swidget based on ndata->module_id and ndata->instance_id */
	swidget = sof_ipc4_find_swidget_by_ids(sdev, ndata->module_id,
					       ndata->instance_id);
	if (!swidget) {
		dev_err(sdev->dev, "%s: Failed to find widget for module %u.%u\n",
			__func__, ndata->module_id, ndata->instance_id);
		return;
	}

	/* Find the scontrol which is the source of the notification */
	msg_data = (struct sof_ipc4_control_msg_payload *)ndata->event_data;
	list_for_each_entry(scontrol, &sdev->kcontrol_list, list) {
		if (scontrol->comp_id == swidget->comp_id) {
			u32 local_param_id;

			cdata = scontrol->ipc_control_data;
			/*
			 * The scontrol's param_id is stored in the IPC message
			 * template's extension
			 */
			local_param_id = PARAM_ID_FROM_EXTENSION(cdata->msg.extension);
			if (local_param_id == event_param_id &&
			    msg_data->id == cdata->index) {
				scontrol_found = true;
				break;
			}
		}
	}

	if (!scontrol_found) {
		dev_err(sdev->dev,
			"%s: Failed to find control on widget %s: %u:%u\n",
			__func__, swidget->widget->name, ndata->event_id & 0xffff,
			msg_data->id);
		return;
	}

	if (msg_data->num_elems) {
		/*
		 * The message includes the updated value/data, update the
		 * control's local cache using the received notification
		 */
		for (i = 0; i < msg_data->num_elems; i++) {
			u32 channel = msg_data->chanv[i].channel;

			if (channel >= scontrol->num_channels) {
				dev_warn(sdev->dev,
					 "Invalid channel index for %s: %u\n",
					 scontrol->name, i);

				/*
				 * Mark the scontrol as dirty to force a refresh
				 * on next read
				 */
				scontrol->comp_data_dirty = true;
				break;
			}

			cdata->chanv[channel].value = msg_data->chanv[i].value;
		}
	} else {
		/*
		 * Mark the scontrol as dirty because the value/data is changed
		 * in firmware, forcing a refresh on next read access
		 */
		scontrol->comp_data_dirty = true;
	}

	/*
	 * Look up the ALSA kcontrol of the scontrol to be able to send a
	 * notification to user space
	 */
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

	snd_ctl_notify_one(swidget->scomp->card->snd_card,
			   SNDRV_CTL_EVENT_MASK_VALUE, kc, 0);
}

/* set up all controls for the widget */
static int sof_ipc4_widget_kcontrol_setup(struct snd_sof_dev *sdev, struct snd_sof_widget *swidget)
{
	struct snd_sof_control *scontrol;
	int ret = 0;

	list_for_each_entry(scontrol, &sdev->kcontrol_list, list) {
		if (scontrol->comp_id == swidget->comp_id) {
			switch (scontrol->info_type) {
			case SND_SOC_TPLG_CTL_VOLSW:
			case SND_SOC_TPLG_CTL_VOLSW_SX:
			case SND_SOC_TPLG_CTL_VOLSW_XR_SX:
				ret = sof_ipc4_volsw_setup(sdev, swidget, scontrol);
				break;
			case SND_SOC_TPLG_CTL_BYTES:
				ret = sof_ipc4_set_get_bytes_data(sdev, scontrol,
								  true, false);
				break;
			case SND_SOC_TPLG_CTL_ENUM:
			case SND_SOC_TPLG_CTL_ENUM_VALUE:
				ret = sof_ipc4_set_generic_control_data(sdev, swidget,
									scontrol, false);
				break;
			default:
				break;
			}

			if (ret < 0) {
				dev_err(sdev->dev,
					"kcontrol %d set up failed for widget %s\n",
					scontrol->comp_id, swidget->widget->name);
				return ret;
			}
		}
	}

	return 0;
}

static int
sof_ipc4_set_up_volume_table(struct snd_sof_control *scontrol, int tlv[SOF_TLV_ITEMS], int size)
{
	int i;

	/* init the volume table */
	scontrol->volume_table = kcalloc(size, sizeof(u32), GFP_KERNEL);
	if (!scontrol->volume_table)
		return -ENOMEM;

	/* populate the volume table */
	for (i = 0; i < size ; i++) {
		u32 val = vol_compute_gain(i, tlv);
		u64 q31val = ((u64)val) << 15; /* Can be over Q1.31, need to saturate */

		scontrol->volume_table[i] = q31val > SOF_IPC4_VOL_ZERO_DB ?
						SOF_IPC4_VOL_ZERO_DB : q31val;
	}

	return 0;
}

const struct sof_ipc_tplg_control_ops tplg_ipc4_control_ops = {
	.volume_put = sof_ipc4_volume_put,
	.volume_get = sof_ipc4_volume_get,
	.switch_put = sof_ipc4_switch_put,
	.switch_get = sof_ipc4_switch_get,
	.enum_put = sof_ipc4_enum_put,
	.enum_get = sof_ipc4_enum_get,
	.bytes_put = sof_ipc4_bytes_put,
	.bytes_get = sof_ipc4_bytes_get,
	.bytes_ext_put = sof_ipc4_bytes_ext_put,
	.bytes_ext_get = sof_ipc4_bytes_ext_get,
	.bytes_ext_volatile_get = sof_ipc4_bytes_ext_volatile_get,
	.update = sof_ipc4_control_update,
	.widget_kcontrol_setup = sof_ipc4_widget_kcontrol_setup,
	.set_up_volume_table = sof_ipc4_set_up_volume_table,
};
