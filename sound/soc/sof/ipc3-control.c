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
#include "ipc3-ops.h"

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

const struct sof_ipc_tplg_control_ops tplg_ipc3_control_ops = {
	.update = sof_ipc3_control_update,
};
