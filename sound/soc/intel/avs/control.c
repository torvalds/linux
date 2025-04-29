// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright(c) 2021-2022 Intel Corporation
//
// Authors: Amadeusz Slawinski <amadeuszx.slawinski@linux.intel.com>
//          Cezary Rojewski <cezary.rojewski@intel.com>
//

#include <linux/cleanup.h>
#include <sound/soc.h>
#include "avs.h"
#include "control.h"
#include "messages.h"
#include "path.h"

static struct avs_dev *avs_get_kcontrol_adev(struct snd_kcontrol *kcontrol)
{
	struct snd_soc_dapm_widget *w;

	w = snd_soc_dapm_kcontrol_widget(kcontrol);

	return to_avs_dev(w->dapm->component->dev);
}

static struct avs_path_module *avs_get_volume_module(struct avs_dev *adev, u32 id)
{
	struct avs_path *path;
	struct avs_path_pipeline *ppl;
	struct avs_path_module *mod;

	spin_lock(&adev->path_list_lock);
	list_for_each_entry(path, &adev->path_list, node) {
		list_for_each_entry(ppl, &path->ppl_list, node) {
			list_for_each_entry(mod, &ppl->mod_list, node) {
				guid_t *type = &mod->template->cfg_ext->type;

				if ((guid_equal(type, &AVS_PEAKVOL_MOD_UUID) ||
				     guid_equal(type, &AVS_GAIN_MOD_UUID)) &&
				    mod->template->ctl_id == id) {
					spin_unlock(&adev->path_list_lock);
					return mod;
				}
			}
		}
	}
	spin_unlock(&adev->path_list_lock);

	return NULL;
}

int avs_control_volume_get(struct snd_kcontrol *kctl, struct snd_ctl_elem_value *uctl)
{
	struct soc_mixer_control *mc = (struct soc_mixer_control *)kctl->private_value;
	struct avs_control_data *ctl_data = mc->dobj.private;
	struct avs_path_module *active_module;
	struct avs_volume_cfg *dspvols;
	struct avs_dev *adev;
	size_t num_dspvols;
	int ret, i;

	adev = avs_get_kcontrol_adev(kctl);

	/* Prevent access to modules while path is being constructed. */
	guard(mutex)(&adev->path_mutex);

	active_module = avs_get_volume_module(adev, ctl_data->id);
	if (active_module) {
		ret = avs_ipc_peakvol_get_volume(adev, active_module->module_id,
						 active_module->instance_id, &dspvols,
						 &num_dspvols);
		if (ret)
			return AVS_IPC_RET(ret);

		/* Do not copy more than the control can store. */
		num_dspvols = min_t(u32, num_dspvols, SND_SOC_TPLG_MAX_CHAN);
		for (i = 0; i < num_dspvols; i++)
			ctl_data->values[i] = dspvols[i].target_volume;
		kfree(dspvols);
	}

	memcpy(uctl->value.integer.value, ctl_data->values, sizeof(ctl_data->values));
	return 0;
}

int avs_control_volume_put(struct snd_kcontrol *kctl, struct snd_ctl_elem_value *uctl)
{
	struct avs_path_module *active_module;
	struct avs_control_data *ctl_data;
	struct soc_mixer_control *mc;
	struct avs_dev *adev;
	long *input;
	int ret, i;

	mc = (struct soc_mixer_control *)kctl->private_value;
	ctl_data = mc->dobj.private;
	adev = avs_get_kcontrol_adev(kctl);
	input = uctl->value.integer.value;
	i = 0;

	/* mc->num_channels can be 0. */
	do {
		if (input[i] < mc->min || input[i] > mc->max)
			return -EINVAL;
	} while (++i < mc->num_channels);

	if (!memcmp(ctl_data->values, input, sizeof(ctl_data->values)))
		return 0;

	/* Prevent access to modules while path is being constructed. */
	guard(mutex)(&adev->path_mutex);

	active_module = avs_get_volume_module(adev, ctl_data->id);
	if (active_module) {
		ret = avs_peakvol_set_volume(adev, active_module, mc, input);
		if (ret)
			return ret;
	}

	memcpy(ctl_data->values, input, sizeof(ctl_data->values));
	return 1;
}

int avs_control_volume_info(struct snd_kcontrol *kctl, struct snd_ctl_elem_info *uinfo)
{
	struct soc_mixer_control *mc = (struct soc_mixer_control *)kctl->private_value;

	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = max_t(u32, 1, mc->num_channels);
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = mc->max;
	return 0;
}

int avs_control_mute_get(struct snd_kcontrol *kctl, struct snd_ctl_elem_value *uctl)
{
	struct soc_mixer_control *mc = (struct soc_mixer_control *)kctl->private_value;
	struct avs_control_data *ctl_data = mc->dobj.private;
	struct avs_path_module *active_module;
	struct avs_mute_cfg *dspmutes;
	struct avs_dev *adev;
	size_t num_dspmutes;
	int ret, i;

	adev = avs_get_kcontrol_adev(kctl);

	/* Prevent access to modules while path is being constructed. */
	guard(mutex)(&adev->path_mutex);

	active_module = avs_get_volume_module(adev, ctl_data->id);
	if (active_module) {
		ret = avs_ipc_peakvol_get_mute(adev, active_module->module_id,
					       active_module->instance_id, &dspmutes,
					       &num_dspmutes);
		if (ret)
			return AVS_IPC_RET(ret);

		/* Do not copy more than the control can store. */
		num_dspmutes = min_t(u32, num_dspmutes, SND_SOC_TPLG_MAX_CHAN);
		for (i = 0; i < num_dspmutes; i++)
			ctl_data->values[i] = !dspmutes[i].mute;
		kfree(dspmutes);
	}

	memcpy(uctl->value.integer.value, ctl_data->values, sizeof(ctl_data->values));
	return 0;
}

int avs_control_mute_put(struct snd_kcontrol *kctl, struct snd_ctl_elem_value *uctl)
{
	struct avs_path_module *active_module;
	struct avs_control_data *ctl_data;
	struct soc_mixer_control *mc;
	struct avs_dev *adev;
	long *input;
	int ret, i;

	mc = (struct soc_mixer_control *)kctl->private_value;
	ctl_data = mc->dobj.private;
	adev = avs_get_kcontrol_adev(kctl);
	input = uctl->value.integer.value;
	i = 0;

	/* mc->num_channels can be 0. */
	do {
		if (input[i] < mc->min || input[i] > mc->max)
			return -EINVAL;
	} while (++i < mc->num_channels);

	if (!memcmp(ctl_data->values, input, sizeof(ctl_data->values)))
		return 0;

	/* Prevent access to modules while path is being constructed. */
	guard(mutex)(&adev->path_mutex);

	active_module = avs_get_volume_module(adev, ctl_data->id);
	if (active_module) {
		ret = avs_peakvol_set_mute(adev, active_module, mc, input);
		if (ret)
			return ret;
	}

	memcpy(ctl_data->values, input, sizeof(ctl_data->values));
	return 1;
}

int avs_control_mute_info(struct snd_kcontrol *kctl, struct snd_ctl_elem_info *uinfo)
{
	struct soc_mixer_control *mc = (struct soc_mixer_control *)kctl->private_value;

	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = max_t(u32, 1, mc->num_channels);
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = mc->max;
	return 0;
}
