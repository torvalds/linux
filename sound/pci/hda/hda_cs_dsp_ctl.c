// SPDX-License-Identifier: GPL-2.0
//
// HDA DSP ALSA Control Driver
//
// Copyright 2022 Cirrus Logic, Inc.
//
// Author: Stefan Binding <sbinding@opensource.cirrus.com>

#include <linux/module.h>
#include <sound/soc.h>
#include <linux/cleanup.h>
#include <linux/firmware/cirrus/cs_dsp.h>
#include <linux/firmware/cirrus/wmfw.h>
#include "hda_cs_dsp_ctl.h"

#define ADSP_MAX_STD_CTRL_SIZE               512

struct hda_cs_dsp_coeff_ctl {
	struct cs_dsp_coeff_ctl *cs_ctl;
	struct snd_card *card;
	struct snd_kcontrol *kctl;
};

static const char * const hda_cs_dsp_fw_text[HDA_CS_DSP_NUM_FW] = {
	[HDA_CS_DSP_FW_SPK_PROT] = "Prot",
	[HDA_CS_DSP_FW_SPK_CALI] = "Cali",
	[HDA_CS_DSP_FW_SPK_DIAG] = "Diag",
	[HDA_CS_DSP_FW_MISC] =     "Misc",
};

const char * const hda_cs_dsp_fw_ids[HDA_CS_DSP_NUM_FW] = {
	[HDA_CS_DSP_FW_SPK_PROT] = "spk-prot",
	[HDA_CS_DSP_FW_SPK_CALI] = "spk-cali",
	[HDA_CS_DSP_FW_SPK_DIAG] = "spk-diag",
	[HDA_CS_DSP_FW_MISC] =     "misc",
};
EXPORT_SYMBOL_NS_GPL(hda_cs_dsp_fw_ids, SND_HDA_CS_DSP_CONTROLS);

static int hda_cs_dsp_coeff_info(struct snd_kcontrol *kctl, struct snd_ctl_elem_info *uinfo)
{
	struct hda_cs_dsp_coeff_ctl *ctl = (struct hda_cs_dsp_coeff_ctl *)snd_kcontrol_chip(kctl);
	struct cs_dsp_coeff_ctl *cs_ctl = ctl->cs_ctl;

	uinfo->type = SNDRV_CTL_ELEM_TYPE_BYTES;
	uinfo->count = cs_ctl->len;

	return 0;
}

static int hda_cs_dsp_coeff_put(struct snd_kcontrol *kctl, struct snd_ctl_elem_value *ucontrol)
{
	struct hda_cs_dsp_coeff_ctl *ctl = (struct hda_cs_dsp_coeff_ctl *)snd_kcontrol_chip(kctl);
	struct cs_dsp_coeff_ctl *cs_ctl = ctl->cs_ctl;
	char *p = ucontrol->value.bytes.data;

	return cs_dsp_coeff_lock_and_write_ctrl(cs_ctl, 0, p, cs_ctl->len);
}

static int hda_cs_dsp_coeff_get(struct snd_kcontrol *kctl, struct snd_ctl_elem_value *ucontrol)
{
	struct hda_cs_dsp_coeff_ctl *ctl = (struct hda_cs_dsp_coeff_ctl *)snd_kcontrol_chip(kctl);
	struct cs_dsp_coeff_ctl *cs_ctl = ctl->cs_ctl;
	char *p = ucontrol->value.bytes.data;

	return cs_dsp_coeff_lock_and_read_ctrl(cs_ctl, 0, p, cs_ctl->len);
}

static unsigned int wmfw_convert_flags(unsigned int in)
{
	unsigned int out, rd, wr, vol;

	rd = SNDRV_CTL_ELEM_ACCESS_READ;
	wr = SNDRV_CTL_ELEM_ACCESS_WRITE;
	vol = SNDRV_CTL_ELEM_ACCESS_VOLATILE;

	out = 0;

	if (in) {
		out |= rd;
		if (in & WMFW_CTL_FLAG_WRITEABLE)
			out |= wr;
		if (in & WMFW_CTL_FLAG_VOLATILE)
			out |= vol;
	} else {
		out |= rd | wr | vol;
	}

	return out;
}

static void hda_cs_dsp_free_kcontrol(struct snd_kcontrol *kctl)
{
	struct hda_cs_dsp_coeff_ctl *ctl = (struct hda_cs_dsp_coeff_ctl *)snd_kcontrol_chip(kctl);
	struct cs_dsp_coeff_ctl *cs_ctl = ctl->cs_ctl;

	/* NULL priv to prevent a double-free in hda_cs_dsp_control_remove() */
	cs_ctl->priv = NULL;
	kfree(ctl);
}

static void hda_cs_dsp_add_kcontrol(struct cs_dsp_coeff_ctl *cs_ctl,
				    const struct hda_cs_dsp_ctl_info *info,
				    const char *name)
{
	struct snd_kcontrol_new kcontrol = {0};
	struct snd_kcontrol *kctl;
	struct hda_cs_dsp_coeff_ctl *ctl __free(kfree) = NULL;
	int ret = 0;

	if (cs_ctl->len > ADSP_MAX_STD_CTRL_SIZE) {
		dev_err(cs_ctl->dsp->dev, "KControl %s: length %zu exceeds maximum %d\n", name,
			cs_ctl->len, ADSP_MAX_STD_CTRL_SIZE);
		return;
	}

	ctl = kzalloc(sizeof(*ctl), GFP_KERNEL);
	if (!ctl)
		return;

	ctl->cs_ctl = cs_ctl;
	ctl->card = info->card;

	kcontrol.name = name;
	kcontrol.info = hda_cs_dsp_coeff_info;
	kcontrol.iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	kcontrol.access = wmfw_convert_flags(cs_ctl->flags);
	kcontrol.get = hda_cs_dsp_coeff_get;
	kcontrol.put = hda_cs_dsp_coeff_put;

	kctl = snd_ctl_new1(&kcontrol, (void *)ctl);
	if (!kctl)
		return;

	kctl->private_free = hda_cs_dsp_free_kcontrol;
	ctl->kctl = kctl;

	/* snd_ctl_add() calls our private_free on error, which will kfree(ctl) */
	cs_ctl->priv = no_free_ptr(ctl);
	ret = snd_ctl_add(info->card, kctl);
	if (ret) {
		dev_err(cs_ctl->dsp->dev, "Failed to add KControl %s = %d\n", kcontrol.name, ret);
		return;
	}

	dev_dbg(cs_ctl->dsp->dev, "Added KControl: %s\n", kcontrol.name);
}

static void hda_cs_dsp_control_add(struct cs_dsp_coeff_ctl *cs_ctl,
				   const struct hda_cs_dsp_ctl_info *info)
{
	struct cs_dsp *cs_dsp = cs_ctl->dsp;
	char name[SNDRV_CTL_ELEM_ID_NAME_MAXLEN];
	const char *region_name;
	int ret;

	region_name = cs_dsp_mem_region_name(cs_ctl->alg_region.type);
	if (!region_name) {
		dev_warn(cs_dsp->dev, "Unknown region type: %d\n", cs_ctl->alg_region.type);
		return;
	}

	ret = scnprintf(name, SNDRV_CTL_ELEM_ID_NAME_MAXLEN, "%s %s %.12s %x", info->device_name,
			cs_dsp->name, hda_cs_dsp_fw_text[info->fw_type], cs_ctl->alg_region.alg);

	if (cs_ctl->subname) {
		int avail = SNDRV_CTL_ELEM_ID_NAME_MAXLEN - ret - 2;
		int skip = 0;

		/* Truncate the subname from the start if it is too long */
		if (cs_ctl->subname_len > avail)
			skip = cs_ctl->subname_len - avail;

		snprintf(name + ret, SNDRV_CTL_ELEM_ID_NAME_MAXLEN - ret,
			 " %.*s", cs_ctl->subname_len - skip, cs_ctl->subname + skip);
	}

	hda_cs_dsp_add_kcontrol(cs_ctl, info, name);
}

void hda_cs_dsp_add_controls(struct cs_dsp *dsp, const struct hda_cs_dsp_ctl_info *info)
{
	struct cs_dsp_coeff_ctl *cs_ctl;

	/*
	 * pwr_lock would cause mutex inversion with ALSA control lock compared
	 * to the get/put functions.
	 * It is safe to walk the list without holding a mutex because entries
	 * are persistent and only cs_dsp_power_up() or cs_dsp_remove() can
	 * change the list.
	 */
	lockdep_assert_not_held(&dsp->pwr_lock);

	list_for_each_entry(cs_ctl, &dsp->ctl_list, list) {
		if (cs_ctl->flags & WMFW_CTL_FLAG_SYS)
			continue;

		if (cs_ctl->priv)
			continue;

		hda_cs_dsp_control_add(cs_ctl, info);
	}
}
EXPORT_SYMBOL_NS_GPL(hda_cs_dsp_add_controls, SND_HDA_CS_DSP_CONTROLS);

void hda_cs_dsp_control_remove(struct cs_dsp_coeff_ctl *cs_ctl)
{
	struct hda_cs_dsp_coeff_ctl *ctl = cs_ctl->priv;

	/* ctl and kctl may already have been removed by ALSA private_free */
	if (ctl)
		snd_ctl_remove(ctl->card, ctl->kctl);
}
EXPORT_SYMBOL_NS_GPL(hda_cs_dsp_control_remove, SND_HDA_CS_DSP_CONTROLS);

int hda_cs_dsp_write_ctl(struct cs_dsp *dsp, const char *name, int type,
			 unsigned int alg, const void *buf, size_t len)
{
	struct cs_dsp_coeff_ctl *cs_ctl;
	int ret;

	mutex_lock(&dsp->pwr_lock);
	cs_ctl = cs_dsp_get_ctl(dsp, name, type, alg);
	ret = cs_dsp_coeff_write_ctrl(cs_ctl, 0, buf, len);
	mutex_unlock(&dsp->pwr_lock);
	if (ret < 0)
		return ret;

	return 0;
}
EXPORT_SYMBOL_NS_GPL(hda_cs_dsp_write_ctl, SND_HDA_CS_DSP_CONTROLS);

int hda_cs_dsp_read_ctl(struct cs_dsp *dsp, const char *name, int type,
			unsigned int alg, void *buf, size_t len)
{
	int ret;

	mutex_lock(&dsp->pwr_lock);
	ret = cs_dsp_coeff_read_ctrl(cs_dsp_get_ctl(dsp, name, type, alg), 0, buf, len);
	mutex_unlock(&dsp->pwr_lock);

	return ret;

}
EXPORT_SYMBOL_NS_GPL(hda_cs_dsp_read_ctl, SND_HDA_CS_DSP_CONTROLS);

MODULE_DESCRIPTION("CS_DSP ALSA Control HDA Library");
MODULE_AUTHOR("Stefan Binding, <sbinding@opensource.cirrus.com>");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS(FW_CS_DSP);
