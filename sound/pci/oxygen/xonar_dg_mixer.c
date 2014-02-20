/*
 * Mixer controls for the Xonar DG/DGX
 *
 * Copyright (c) Clemens Ladisch <clemens@ladisch.de>
 * Copyright (c) Roman Volkov <v1ron@mail.ru>
 *
 *  This driver is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License, version 2.
 *
 *  This driver is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this driver; if not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/pci.h>
#include <linux/delay.h>
#include <sound/control.h>
#include <sound/core.h>
#include <sound/info.h>
#include <sound/pcm.h>
#include <sound/tlv.h>
#include "oxygen.h"
#include "xonar_dg.h"
#include "cs4245.h"

/* analog output select */

static int output_select_apply(struct oxygen *chip)
{
	struct dg *data = chip->model_data;

	data->cs4245_shadow[CS4245_SIGNAL_SEL] &= ~CS4245_A_OUT_SEL_MASK;
	if (data->output_sel == PLAYBACK_DST_HP) {
		/* mute FP (aux output) amplifier, switch rear jack to CS4245 */
		oxygen_set_bits8(chip, OXYGEN_GPIO_DATA, GPIO_HP_REAR);
	} else if (data->output_sel == PLAYBACK_DST_HP_FP) {
		/*
		 * Unmute FP amplifier, switch rear jack to CS4361;
		 * I2S channels 2,3,4 should be inactive.
		 */
		oxygen_clear_bits8(chip, OXYGEN_GPIO_DATA, GPIO_HP_REAR);
		data->cs4245_shadow[CS4245_SIGNAL_SEL] |= CS4245_A_OUT_SEL_DAC;
	} else {
		/*
		 * 2.0, 4.0, 5.1: switch to CS4361, mute FP amp.,
		 * and change playback routing.
		 */
		oxygen_clear_bits8(chip, OXYGEN_GPIO_DATA, GPIO_HP_REAR);
	}
	return cs4245_write_spi(chip, CS4245_SIGNAL_SEL);
}

static int output_select_info(struct snd_kcontrol *ctl,
			      struct snd_ctl_elem_info *info)
{
	static const char *const names[3] = {
		"Stereo Headphones",
		"Stereo Headphones FP",
		"Multichannel",
	};

	return snd_ctl_enum_info(info, 1, 3, names);
}

static int output_select_get(struct snd_kcontrol *ctl,
			     struct snd_ctl_elem_value *value)
{
	struct oxygen *chip = ctl->private_data;
	struct dg *data = chip->model_data;

	mutex_lock(&chip->mutex);
	value->value.enumerated.item[0] = data->output_sel;
	mutex_unlock(&chip->mutex);
	return 0;
}

static int output_select_put(struct snd_kcontrol *ctl,
			     struct snd_ctl_elem_value *value)
{
	struct oxygen *chip = ctl->private_data;
	struct dg *data = chip->model_data;
	unsigned int new = value->value.enumerated.item[0];
	int changed = 0;
	int ret;

	mutex_lock(&chip->mutex);
	if (data->output_sel != new) {
		data->output_sel = new;
		ret = output_select_apply(chip);
		changed = ret >= 0 ? 1 : ret;
		oxygen_update_dac_routing(chip);
	}
	mutex_unlock(&chip->mutex);

	return changed;
}

/* CS4245 Headphone Channels A&B Volume Control */

static int hp_stereo_volume_info(struct snd_kcontrol *ctl,
				struct snd_ctl_elem_info *info)
{
	info->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	info->count = 2;
	info->value.integer.min = 0;
	info->value.integer.max = 255;
	return 0;
}

static int hp_stereo_volume_get(struct snd_kcontrol *ctl,
				struct snd_ctl_elem_value *val)
{
	struct oxygen *chip = ctl->private_data;
	struct dg *data = chip->model_data;
	unsigned int tmp;

	mutex_lock(&chip->mutex);
	tmp = (~data->cs4245_shadow[CS4245_DAC_A_CTRL]) & 255;
	val->value.integer.value[0] = tmp;
	tmp = (~data->cs4245_shadow[CS4245_DAC_B_CTRL]) & 255;
	val->value.integer.value[1] = tmp;
	mutex_unlock(&chip->mutex);
	return 0;
}

static int hp_stereo_volume_put(struct snd_kcontrol *ctl,
				struct snd_ctl_elem_value *val)
{
	struct oxygen *chip = ctl->private_data;
	struct dg *data = chip->model_data;
	int ret;
	int changed = 0;
	long new1 = val->value.integer.value[0];
	long new2 = val->value.integer.value[1];

	if ((new1 > 255) || (new1 < 0) || (new2 > 255) || (new2 < 0))
		return -EINVAL;

	mutex_lock(&chip->mutex);
	if ((data->cs4245_shadow[CS4245_DAC_A_CTRL] != ~new1) ||
	    (data->cs4245_shadow[CS4245_DAC_B_CTRL] != ~new2)) {
		data->cs4245_shadow[CS4245_DAC_A_CTRL] = ~new1;
		data->cs4245_shadow[CS4245_DAC_B_CTRL] = ~new2;
		ret = cs4245_write_spi(chip, CS4245_DAC_A_CTRL);
		if (ret >= 0)
			ret = cs4245_write_spi(chip, CS4245_DAC_B_CTRL);
		changed = ret >= 0 ? 1 : ret;
	}
	mutex_unlock(&chip->mutex);

	return changed;
}

/* Headphone Mute */

static int hp_mute_get(struct snd_kcontrol *ctl,
			struct snd_ctl_elem_value *val)
{
	struct oxygen *chip = ctl->private_data;
	struct dg *data = chip->model_data;

	mutex_lock(&chip->mutex);
	val->value.integer.value[0] =
		!(data->cs4245_shadow[CS4245_DAC_CTRL_1] & CS4245_MUTE_DAC);
	mutex_unlock(&chip->mutex);
	return 0;
}

static int hp_mute_put(struct snd_kcontrol *ctl,
			struct snd_ctl_elem_value *val)
{
	struct oxygen *chip = ctl->private_data;
	struct dg *data = chip->model_data;
	int ret;
	int changed;

	if (val->value.integer.value[0] > 1)
		return -EINVAL;
	mutex_lock(&chip->mutex);
	data->cs4245_shadow[CS4245_DAC_CTRL_1] &= ~CS4245_MUTE_DAC;
	data->cs4245_shadow[CS4245_DAC_CTRL_1] |=
		(~val->value.integer.value[0] << 2) & CS4245_MUTE_DAC;
	ret = cs4245_write_spi(chip, CS4245_DAC_CTRL_1);
	changed = ret >= 0 ? 1 : ret;
	mutex_unlock(&chip->mutex);
	return changed;
}

/* capture volume for all sources */

static int input_volume_apply(struct oxygen *chip, char left, char right)
{
	struct dg *data = chip->model_data;
	int ret;

	data->cs4245_shadow[CS4245_PGA_A_CTRL] = left;
	data->cs4245_shadow[CS4245_PGA_B_CTRL] = right;
	ret = cs4245_write_spi(chip, CS4245_PGA_A_CTRL);
	if (ret < 0)
		return ret;
	return cs4245_write_spi(chip, CS4245_PGA_B_CTRL);
}

static int input_vol_info(struct snd_kcontrol *ctl,
			  struct snd_ctl_elem_info *info)
{
	info->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	info->count = 2;
	info->value.integer.min = 2 * -12;
	info->value.integer.max = 2 * 12;
	return 0;
}

static int input_vol_get(struct snd_kcontrol *ctl,
			 struct snd_ctl_elem_value *value)
{
	struct oxygen *chip = ctl->private_data;
	struct dg *data = chip->model_data;
	unsigned int idx = ctl->private_value;

	mutex_lock(&chip->mutex);
	value->value.integer.value[0] = data->input_vol[idx][0];
	value->value.integer.value[1] = data->input_vol[idx][1];
	mutex_unlock(&chip->mutex);
	return 0;
}

static int input_vol_put(struct snd_kcontrol *ctl,
			 struct snd_ctl_elem_value *value)
{
	struct oxygen *chip = ctl->private_data;
	struct dg *data = chip->model_data;
	unsigned int idx = ctl->private_value;
	int changed = 0;
	int ret = 0;

	if (value->value.integer.value[0] < 2 * -12 ||
	    value->value.integer.value[0] > 2 * 12 ||
	    value->value.integer.value[1] < 2 * -12 ||
	    value->value.integer.value[1] > 2 * 12)
		return -EINVAL;
	mutex_lock(&chip->mutex);
	changed = data->input_vol[idx][0] != value->value.integer.value[0] ||
		  data->input_vol[idx][1] != value->value.integer.value[1];
	if (changed) {
		data->input_vol[idx][0] = value->value.integer.value[0];
		data->input_vol[idx][1] = value->value.integer.value[1];
		if (idx == data->input_sel) {
			ret = input_volume_apply(chip,
				data->input_vol[idx][0],
				data->input_vol[idx][1]);
		}
		changed = ret >= 0 ? 1 : ret;
	}
	mutex_unlock(&chip->mutex);
	return changed;
}

/* Capture Source */

static int input_source_apply(struct oxygen *chip)
{
	struct dg *data = chip->model_data;

	data->cs4245_shadow[CS4245_ANALOG_IN] &= ~CS4245_SEL_MASK;
	if (data->input_sel == CAPTURE_SRC_FP_MIC)
		data->cs4245_shadow[CS4245_ANALOG_IN] |= CS4245_SEL_INPUT_2;
	else if (data->input_sel == CAPTURE_SRC_LINE)
		data->cs4245_shadow[CS4245_ANALOG_IN] |= CS4245_SEL_INPUT_4;
	else if (data->input_sel != CAPTURE_SRC_MIC)
		data->cs4245_shadow[CS4245_ANALOG_IN] |= CS4245_SEL_INPUT_1;
	return cs4245_write_spi(chip, CS4245_ANALOG_IN);
}

static int input_sel_info(struct snd_kcontrol *ctl,
			  struct snd_ctl_elem_info *info)
{
	static const char *const names[4] = {
		"Mic", "Front Mic", "Line", "Aux"
	};

	return snd_ctl_enum_info(info, 1, 4, names);
}

static int input_sel_get(struct snd_kcontrol *ctl,
			 struct snd_ctl_elem_value *value)
{
	struct oxygen *chip = ctl->private_data;
	struct dg *data = chip->model_data;

	mutex_lock(&chip->mutex);
	value->value.enumerated.item[0] = data->input_sel;
	mutex_unlock(&chip->mutex);
	return 0;
}

static int input_sel_put(struct snd_kcontrol *ctl,
			 struct snd_ctl_elem_value *value)
{
	struct oxygen *chip = ctl->private_data;
	struct dg *data = chip->model_data;
	int changed;
	int ret;

	if (value->value.enumerated.item[0] > 3)
		return -EINVAL;

	mutex_lock(&chip->mutex);
	changed = value->value.enumerated.item[0] != data->input_sel;
	if (changed) {
		data->input_sel = value->value.enumerated.item[0];

		ret = input_source_apply(chip);
		if (ret >= 0)
			ret = input_volume_apply(chip,
				data->input_vol[data->input_sel][0],
				data->input_vol[data->input_sel][1]);
		changed = ret >= 0 ? 1 : ret;
	}
	mutex_unlock(&chip->mutex);
	return changed;
}

/* ADC high-pass filter */

static int hpf_info(struct snd_kcontrol *ctl, struct snd_ctl_elem_info *info)
{
	static const char *const names[2] = { "Active", "Frozen" };

	return snd_ctl_enum_info(info, 1, 2, names);
}

static int hpf_get(struct snd_kcontrol *ctl, struct snd_ctl_elem_value *value)
{
	struct oxygen *chip = ctl->private_data;
	struct dg *data = chip->model_data;

	value->value.enumerated.item[0] =
		!!(data->cs4245_shadow[CS4245_ADC_CTRL] & CS4245_HPF_FREEZE);
	return 0;
}

static int hpf_put(struct snd_kcontrol *ctl, struct snd_ctl_elem_value *value)
{
	struct oxygen *chip = ctl->private_data;
	struct dg *data = chip->model_data;
	u8 reg;
	int changed;

	mutex_lock(&chip->mutex);
	reg = data->cs4245_shadow[CS4245_ADC_CTRL] & ~CS4245_HPF_FREEZE;
	if (value->value.enumerated.item[0])
		reg |= CS4245_HPF_FREEZE;
	changed = reg != data->cs4245_shadow[CS4245_ADC_CTRL];
	if (changed) {
		data->cs4245_shadow[CS4245_ADC_CTRL] = reg;
		cs4245_write_spi(chip, CS4245_ADC_CTRL);
	}
	mutex_unlock(&chip->mutex);
	return changed;
}

#define INPUT_VOLUME(xname, index) { \
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
	.name = xname, \
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE | \
		  SNDRV_CTL_ELEM_ACCESS_TLV_READ, \
	.info = input_vol_info, \
	.get = input_vol_get, \
	.put = input_vol_put, \
	.tlv = { .p = pga_db_scale }, \
	.private_value = index, \
}
static const DECLARE_TLV_DB_MINMAX(hp_db_scale, -12550, 0);
static const DECLARE_TLV_DB_MINMAX(pga_db_scale, -1200, 1200);
static const struct snd_kcontrol_new dg_controls[] = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Analog Output Playback Enum",
		.info = output_select_info,
		.get = output_select_get,
		.put = output_select_put,
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Headphone Playback Volume",
		.access = SNDRV_CTL_ELEM_ACCESS_READWRITE |
			  SNDRV_CTL_ELEM_ACCESS_TLV_READ,
		.info = hp_stereo_volume_info,
		.get = hp_stereo_volume_get,
		.put = hp_stereo_volume_put,
		.tlv = { .p = hp_db_scale, },
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Headphone Playback Switch",
		.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
		.info = snd_ctl_boolean_mono_info,
		.get = hp_mute_get,
		.put = hp_mute_put,
	},
	INPUT_VOLUME("Mic Capture Volume", CAPTURE_SRC_MIC),
	INPUT_VOLUME("Front Mic Capture Volume", CAPTURE_SRC_FP_MIC),
	INPUT_VOLUME("Line Capture Volume", CAPTURE_SRC_LINE),
	INPUT_VOLUME("Aux Capture Volume", CAPTURE_SRC_AUX),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Capture Source",
		.info = input_sel_info,
		.get = input_sel_get,
		.put = input_sel_put,
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "ADC High-pass Filter Capture Enum",
		.info = hpf_info,
		.get = hpf_get,
		.put = hpf_put,
	},
};

static int dg_control_filter(struct snd_kcontrol_new *template)
{
	if (!strncmp(template->name, "Master Playback ", 16))
		return 1;
	return 0;
}

static int dg_mixer_init(struct oxygen *chip)
{
	unsigned int i;
	int err;

	output_select_apply(chip);
	input_source_apply(chip);
	oxygen_update_dac_routing(chip);

	for (i = 0; i < ARRAY_SIZE(dg_controls); ++i) {
		err = snd_ctl_add(chip->card,
				  snd_ctl_new1(&dg_controls[i], chip));
		if (err < 0)
			return err;
	}

	return 0;
}

struct oxygen_model model_xonar_dg = {
	.longname = "C-Media Oxygen HD Audio",
	.chip = "CMI8786",
	.init = dg_init,
	.control_filter = dg_control_filter,
	.mixer_init = dg_mixer_init,
	.cleanup = dg_cleanup,
	.suspend = dg_suspend,
	.resume = dg_resume,
	.set_dac_params = set_cs4245_dac_params,
	.set_adc_params = set_cs4245_adc_params,
	.adjust_dac_routing = adjust_dg_dac_routing,
	.dump_registers = dump_cs4245_registers,
	.model_data_size = sizeof(struct dg),
	.device_config = PLAYBACK_0_TO_I2S |
			 PLAYBACK_1_TO_SPDIF |
			 CAPTURE_0_FROM_I2S_1 |
			 CAPTURE_1_FROM_SPDIF,
	.dac_channels_pcm = 6,
	.dac_channels_mixer = 0,
	.function_flags = OXYGEN_FUNCTION_SPI,
	.dac_mclks = OXYGEN_MCLKS(256, 128, 128),
	.adc_mclks = OXYGEN_MCLKS(256, 128, 128),
	.dac_i2s_format = OXYGEN_I2S_FORMAT_LJUST,
	.adc_i2s_format = OXYGEN_I2S_FORMAT_LJUST,
};
