/* SPDX-License-Identifier: GPL-2.0 */
//
// ALSA SoC Texas Instruments TAS2563/TAS2781 Audio Smart Amplifier
//
// Copyright (C) 2025 Texas Instruments Incorporated
// https://www.ti.com
//
// The TAS2563/TAS2781 driver implements a flexible and configurable
// algo coefficient setting for one, two, or even multiple
// TAS2563/TAS2781 chips.
//
// Author: Shenghao Ding <shenghao-ding@ti.com>
//

#ifndef __TAS2781_COMLIB_I2C_H__
#define __TAS2781_COMLIB_I2C_H__

void tasdevice_reset(struct tasdevice_priv *tas_dev);
int tascodec_init(struct tasdevice_priv *tas_priv, void *codec,
	struct module *module,
	void (*cont)(const struct firmware *fw, void *context));
struct tasdevice_priv *tasdevice_kzalloc(struct i2c_client *i2c);
int tasdevice_init(struct tasdevice_priv *tas_priv);
int tasdev_chn_switch(struct tasdevice_priv *tas_priv,
	unsigned short chn);
int tasdevice_dev_update_bits(
	struct tasdevice_priv *tasdevice, unsigned short chn,
	unsigned int reg, unsigned int mask, unsigned int value);
int tasdevice_amp_putvol(struct tasdevice_priv *tas_priv,
	struct snd_ctl_elem_value *ucontrol, struct soc_mixer_control *mc);
int tasdevice_amp_getvol(struct tasdevice_priv *tas_priv,
	struct snd_ctl_elem_value *ucontrol, struct soc_mixer_control *mc);
int tasdevice_digital_getvol(struct tasdevice_priv *tas_priv,
	struct snd_ctl_elem_value *ucontrol, struct soc_mixer_control *mc);
int tasdevice_digital_putvol(struct tasdevice_priv *tas_priv,
	struct snd_ctl_elem_value *ucontrol, struct soc_mixer_control *mc);
#endif /* __TAS2781_COMLIB_I2C_H__ */
