/* SPDX-License-Identifier: GPL-2.0-only
 *
 * HDA audio driver for Texas Instruments TAS2781 smart amp
 *
 * Copyright (C) 2025 Texas Instruments, Inc.
 */
#ifndef __TAS2781_HDA_H__
#define __TAS2781_HDA_H__

#include <sound/asound.h>

/* Flag of calibration registers address. */
#define TASDEV_UEFI_CALI_REG_ADDR_FLG	BIT(7)

#define TASDEV_CALIB_N			5

/*
 * No standard control callbacks for SNDRV_CTL_ELEM_IFACE_CARD
 * Define two controls, one is Volume control callbacks, the other is
 * flag setting control callbacks.
 */

/* Volume control callbacks for tas2781 */
#define ACARD_SINGLE_RANGE_EXT_TLV(xname, xreg, xshift, xmin, xmax, xinvert, \
	xhandler_get, xhandler_put, tlv_array) { \
	.iface = SNDRV_CTL_ELEM_IFACE_CARD, .name = (xname), \
	.access = SNDRV_CTL_ELEM_ACCESS_TLV_READ | \
		SNDRV_CTL_ELEM_ACCESS_READWRITE, \
	.tlv.p = (tlv_array), \
	.info = snd_soc_info_volsw, \
	.get = xhandler_get, .put = xhandler_put, \
	.private_value = (unsigned long)&(struct soc_mixer_control) { \
		.reg = xreg, .rreg = xreg, \
		.shift = xshift, .rshift = xshift,\
		.min = xmin, .max = xmax, .invert = xinvert, \
	} \
}

/* Flag control callbacks for tas2781 */
#define ACARD_SINGLE_BOOL_EXT(xname, xdata, xhandler_get, xhandler_put) { \
	.iface = SNDRV_CTL_ELEM_IFACE_CARD, \
	.name = xname, \
	.info = snd_ctl_boolean_mono_info, \
	.get = xhandler_get, \
	.put = xhandler_put, \
	.private_value = xdata, \
}

enum device_catlog_id {
	DELL = 0,
	HP,
	LENOVO,
	OTHERS
};

struct tas2781_hda {
	struct device *dev;
	struct tasdevice_priv *priv;
	struct snd_kcontrol *dsp_prog_ctl;
	struct snd_kcontrol *dsp_conf_ctl;
	struct snd_kcontrol *prof_ctl;
	enum device_catlog_id catlog_id;
	void *hda_priv;
};

extern const efi_guid_t tasdev_fct_efi_guid[];

int tas2781_save_calibration(struct tas2781_hda *p);
void tas2781_hda_remove(struct device *dev,
	const struct component_ops *ops);
int tasdevice_info_profile(struct snd_kcontrol *kctl,
	struct snd_ctl_elem_info *uctl);
int tasdevice_info_programs(struct snd_kcontrol *kctl,
	struct snd_ctl_elem_info *uctl);
int tasdevice_info_config(struct snd_kcontrol *kctl,
	struct snd_ctl_elem_info *uctl);
int tasdevice_set_profile_id(struct snd_kcontrol *kctl,
	struct snd_ctl_elem_value *uctl);
int tasdevice_get_profile_id(struct snd_kcontrol *kctl,
	struct snd_ctl_elem_value *uctl);
int tasdevice_program_get(struct snd_kcontrol *kctl,
	struct snd_ctl_elem_value *uctl);
int tasdevice_program_put(struct snd_kcontrol *kctl,
	struct snd_ctl_elem_value *uctl);
int tasdevice_config_put(struct snd_kcontrol *kctl,
	struct snd_ctl_elem_value *uctl);
int tasdevice_config_get(struct snd_kcontrol *kctl,
	struct snd_ctl_elem_value *uctl);

#endif
