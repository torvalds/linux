// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2022 Intel Corporation. All rights reserved.
//
//

#include "sof-priv.h"
#include "sof-audio.h"
#include "ipc4-priv.h"
#include "ipc4-topology.h"

static int sof_ipc4_set_get_kcontrol_data(struct snd_sof_control *scontrol, bool set)
{
	struct sof_ipc4_control_data *cdata = scontrol->ipc_control_data;
	struct snd_soc_component *scomp = scontrol->scomp;
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	const struct sof_ipc_ops *iops = sdev->ipc->ops;
	struct sof_ipc4_msg *msg = &cdata->msg;
	struct snd_sof_widget *swidget;
	bool widget_found = false;

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

	/*
	 * Volatile controls should always be part of static pipelines and the widget use_count
	 * would always be > 0 in this case. For the others, just return the cached value if the
	 * widget is not set up.
	 */
	if (!swidget->use_count)
		return 0;

	msg->primary &= ~SOF_IPC4_MOD_INSTANCE_MASK;
	msg->primary |= SOF_IPC4_MOD_INSTANCE(swidget->instance_id);

	return iops->set_get_data(sdev, msg, msg->data_size, set);
}

static int
sof_ipc4_set_volume_data(struct snd_sof_dev *sdev, struct snd_sof_widget *swidget,
			 struct snd_sof_control *scontrol)
{
	struct sof_ipc4_control_data *cdata = scontrol->ipc_control_data;
	struct sof_ipc4_gain *gain = swidget->private;
	struct sof_ipc4_msg *msg = &cdata->msg;
	struct sof_ipc4_gain_data data;
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
			data.channels = SOF_IPC4_GAIN_ALL_CHANNELS_MASK;
			data.init_val = cdata->chanv[0].value;
		} else {
			data.channels = cdata->chanv[i].channel;
			data.init_val = cdata->chanv[i].value;
		}

		/* set curve type and duration from topology */
		data.curve_duration_l = gain->data.curve_duration_l;
		data.curve_duration_h = gain->data.curve_duration_h;
		data.curve_type = gain->data.curve_type;

		msg->data_ptr = &data;
		msg->data_size = sizeof(data);

		ret = sof_ipc4_set_get_kcontrol_data(scontrol, true);
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

	ret = sof_ipc4_set_volume_data(sdev, swidget, scontrol);
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

/* set up all controls for the widget */
static int sof_ipc4_widget_kcontrol_setup(struct snd_sof_dev *sdev, struct snd_sof_widget *swidget)
{
	struct snd_sof_control *scontrol;
	int ret;

	list_for_each_entry(scontrol, &sdev->kcontrol_list, list)
		if (scontrol->comp_id == swidget->comp_id) {
			ret = sof_ipc4_set_volume_data(sdev, swidget, scontrol);
			if (ret < 0) {
				dev_err(sdev->dev, "%s: kcontrol %d set up failed for widget %s\n",
					__func__, scontrol->comp_id, swidget->widget->name);
				return ret;
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
	.widget_kcontrol_setup = sof_ipc4_widget_kcontrol_setup,
	.set_up_volume_table = sof_ipc4_set_up_volume_table,
};
