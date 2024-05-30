// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright(c) 2021-2022 Intel Corporation
//
// Authors: Amadeusz Slawinski <amadeuszx.slawinski@linux.intel.com>
//          Cezary Rojewski <cezary.rojewski@intel.com>
//

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
				if (guid_equal(&mod->template->cfg_ext->type, &AVS_PEAKVOL_MOD_UUID)
				    && mod->template->ctl_id == id) {
					spin_unlock(&adev->path_list_lock);
					return mod;
				}
			}
		}
	}
	spin_unlock(&adev->path_list_lock);

	return NULL;
}

int avs_control_volume_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc = (struct soc_mixer_control *)kcontrol->private_value;
	struct avs_control_data *ctl_data = (struct avs_control_data *)mc->dobj.private;
	struct avs_dev *adev = avs_get_kcontrol_adev(kcontrol);
	struct avs_volume_cfg *dspvols = NULL;
	struct avs_path_module *active_module;
	size_t num_dspvols;
	int ret = 0;

	/* prevent access to modules while path is being constructed */
	mutex_lock(&adev->path_mutex);

	active_module = avs_get_volume_module(adev, ctl_data->id);
	if (active_module) {
		ret = avs_ipc_peakvol_get_volume(adev, active_module->module_id,
						 active_module->instance_id, &dspvols,
						 &num_dspvols);
		if (!ret)
			ucontrol->value.integer.value[0] = dspvols[0].target_volume;

		ret = AVS_IPC_RET(ret);
		kfree(dspvols);
	} else {
		ucontrol->value.integer.value[0] = ctl_data->volume;
	}

	mutex_unlock(&adev->path_mutex);
	return ret;
}

int avs_control_volume_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc = (struct soc_mixer_control *)kcontrol->private_value;
	struct avs_control_data *ctl_data = (struct avs_control_data *)mc->dobj.private;
	struct avs_dev *adev = avs_get_kcontrol_adev(kcontrol);
	long *volume = &ctl_data->volume;
	struct avs_path_module *active_module;
	struct avs_volume_cfg dspvol = {0};
	long ctlvol = ucontrol->value.integer.value[0];
	int ret = 0, changed = 0;

	if (ctlvol < 0 || ctlvol > mc->max)
		return -EINVAL;

	/* prevent access to modules while path is being constructed */
	mutex_lock(&adev->path_mutex);

	if (*volume != ctlvol) {
		*volume = ctlvol;
		changed = 1;
	}

	active_module = avs_get_volume_module(adev, ctl_data->id);
	if (active_module) {
		dspvol.channel_id = AVS_ALL_CHANNELS_MASK;
		dspvol.target_volume = *volume;

		ret = avs_ipc_peakvol_set_volume(adev, active_module->module_id,
						 active_module->instance_id, &dspvol);
		ret = AVS_IPC_RET(ret);
	}

	mutex_unlock(&adev->path_mutex);

	return ret ? ret : changed;
}
