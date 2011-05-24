/*
 * card driver for the Xonar DG
 *
 * Copyright (c) Clemens Ladisch <clemens@ladisch.de>
 *
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

/*
 * Xonar DG
 * --------
 *
 * CMI8788:
 *
 *   SPI 0 -> CS4245
 *
 *   I²S 1 -> CS4245
 *   I²S 2 -> CS4361 (center/LFE)
 *   I²S 3 -> CS4361 (surround)
 *   I²S 4 -> CS4361 (front)
 *
 *   GPIO 3 <- ?
 *   GPIO 4 <- headphone detect
 *   GPIO 5 -> route input jack to line-in (0) or mic-in (1)
 *   GPIO 6 -> route input jack to line-in (0) or mic-in (1)
 *   GPIO 7 -> enable rear headphone amp
 *   GPIO 8 -> enable output to speakers
 *
 * CS4245:
 *
 *   input 1 <- aux
 *   input 2 <- front mic
 *   input 4 <- line/mic
 *   DAC out -> headphones
 *   aux out -> front panel headphones
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

#define GPIO_MAGIC		0x0008
#define GPIO_HP_DETECT		0x0010
#define GPIO_INPUT_ROUTE	0x0060
#define GPIO_HP_REAR		0x0080
#define GPIO_OUTPUT_ENABLE	0x0100

struct dg {
	unsigned int output_sel;
	s8 input_vol[4][2];
	unsigned int input_sel;
	u8 hp_vol_att;
	u8 cs4245_regs[0x11];
};

static void cs4245_write(struct oxygen *chip, unsigned int reg, u8 value)
{
	struct dg *data = chip->model_data;

	oxygen_write_spi(chip, OXYGEN_SPI_TRIGGER |
			 OXYGEN_SPI_DATA_LENGTH_3 |
			 OXYGEN_SPI_CLOCK_1280 |
			 (0 << OXYGEN_SPI_CODEC_SHIFT) |
			 OXYGEN_SPI_CEN_LATCH_CLOCK_HI,
			 CS4245_SPI_ADDRESS |
			 CS4245_SPI_WRITE |
			 (reg << 8) | value);
	data->cs4245_regs[reg] = value;
}

static void cs4245_write_cached(struct oxygen *chip, unsigned int reg, u8 value)
{
	struct dg *data = chip->model_data;

	if (value != data->cs4245_regs[reg])
		cs4245_write(chip, reg, value);
}

static void cs4245_registers_init(struct oxygen *chip)
{
	struct dg *data = chip->model_data;

	cs4245_write(chip, CS4245_POWER_CTRL, CS4245_PDN);
	cs4245_write(chip, CS4245_DAC_CTRL_1,
		     data->cs4245_regs[CS4245_DAC_CTRL_1]);
	cs4245_write(chip, CS4245_ADC_CTRL,
		     data->cs4245_regs[CS4245_ADC_CTRL]);
	cs4245_write(chip, CS4245_SIGNAL_SEL,
		     data->cs4245_regs[CS4245_SIGNAL_SEL]);
	cs4245_write(chip, CS4245_PGA_B_CTRL,
		     data->cs4245_regs[CS4245_PGA_B_CTRL]);
	cs4245_write(chip, CS4245_PGA_A_CTRL,
		     data->cs4245_regs[CS4245_PGA_A_CTRL]);
	cs4245_write(chip, CS4245_ANALOG_IN,
		     data->cs4245_regs[CS4245_ANALOG_IN]);
	cs4245_write(chip, CS4245_DAC_A_CTRL,
		     data->cs4245_regs[CS4245_DAC_A_CTRL]);
	cs4245_write(chip, CS4245_DAC_B_CTRL,
		     data->cs4245_regs[CS4245_DAC_B_CTRL]);
	cs4245_write(chip, CS4245_DAC_CTRL_2,
		     CS4245_DAC_SOFT | CS4245_DAC_ZERO | CS4245_INVERT_DAC);
	cs4245_write(chip, CS4245_INT_MASK, 0);
	cs4245_write(chip, CS4245_POWER_CTRL, 0);
}

static void cs4245_init(struct oxygen *chip)
{
	struct dg *data = chip->model_data;

	data->cs4245_regs[CS4245_DAC_CTRL_1] =
		CS4245_DAC_FM_SINGLE | CS4245_DAC_DIF_LJUST;
	data->cs4245_regs[CS4245_ADC_CTRL] =
		CS4245_ADC_FM_SINGLE | CS4245_ADC_DIF_LJUST;
	data->cs4245_regs[CS4245_SIGNAL_SEL] =
		CS4245_A_OUT_SEL_HIZ | CS4245_ASYNCH;
	data->cs4245_regs[CS4245_PGA_B_CTRL] = 0;
	data->cs4245_regs[CS4245_PGA_A_CTRL] = 0;
	data->cs4245_regs[CS4245_ANALOG_IN] =
		CS4245_PGA_SOFT | CS4245_PGA_ZERO | CS4245_SEL_INPUT_4;
	data->cs4245_regs[CS4245_DAC_A_CTRL] = 0;
	data->cs4245_regs[CS4245_DAC_B_CTRL] = 0;
	cs4245_registers_init(chip);
	snd_component_add(chip->card, "CS4245");
}

static void dg_output_enable(struct oxygen *chip)
{
	msleep(2500);
	oxygen_set_bits16(chip, OXYGEN_GPIO_DATA, GPIO_OUTPUT_ENABLE);
}

static void dg_init(struct oxygen *chip)
{
	struct dg *data = chip->model_data;

	data->output_sel = 0;
	data->input_sel = 3;
	data->hp_vol_att = 2 * 16;

	cs4245_init(chip);

	oxygen_clear_bits16(chip, OXYGEN_GPIO_CONTROL,
			    GPIO_MAGIC | GPIO_HP_DETECT);
	oxygen_set_bits16(chip, OXYGEN_GPIO_CONTROL,
			  GPIO_INPUT_ROUTE | GPIO_HP_REAR | GPIO_OUTPUT_ENABLE);
	oxygen_clear_bits16(chip, OXYGEN_GPIO_DATA,
			    GPIO_INPUT_ROUTE | GPIO_HP_REAR);
	dg_output_enable(chip);
}

static void dg_cleanup(struct oxygen *chip)
{
	oxygen_clear_bits16(chip, OXYGEN_GPIO_DATA, GPIO_OUTPUT_ENABLE);
}

static void dg_suspend(struct oxygen *chip)
{
	dg_cleanup(chip);
}

static void dg_resume(struct oxygen *chip)
{
	cs4245_registers_init(chip);
	dg_output_enable(chip);
}

static void set_cs4245_dac_params(struct oxygen *chip,
				  struct snd_pcm_hw_params *params)
{
	struct dg *data = chip->model_data;
	u8 value;

	value = data->cs4245_regs[CS4245_DAC_CTRL_1] & ~CS4245_DAC_FM_MASK;
	if (params_rate(params) <= 50000)
		value |= CS4245_DAC_FM_SINGLE;
	else if (params_rate(params) <= 100000)
		value |= CS4245_DAC_FM_DOUBLE;
	else
		value |= CS4245_DAC_FM_QUAD;
	cs4245_write_cached(chip, CS4245_DAC_CTRL_1, value);
}

static void set_cs4245_adc_params(struct oxygen *chip,
				  struct snd_pcm_hw_params *params)
{
	struct dg *data = chip->model_data;
	u8 value;

	value = data->cs4245_regs[CS4245_ADC_CTRL] & ~CS4245_ADC_FM_MASK;
	if (params_rate(params) <= 50000)
		value |= CS4245_ADC_FM_SINGLE;
	else if (params_rate(params) <= 100000)
		value |= CS4245_ADC_FM_DOUBLE;
	else
		value |= CS4245_ADC_FM_QUAD;
	cs4245_write_cached(chip, CS4245_ADC_CTRL, value);
}

static inline unsigned int shift_bits(unsigned int value,
				      unsigned int shift_from,
				      unsigned int shift_to,
				      unsigned int mask)
{
	if (shift_from < shift_to)
		return (value << (shift_to - shift_from)) & mask;
	else
		return (value >> (shift_from - shift_to)) & mask;
}

static unsigned int adjust_dg_dac_routing(struct oxygen *chip,
					  unsigned int play_routing)
{
	return (play_routing & OXYGEN_PLAY_DAC0_SOURCE_MASK) |
	       shift_bits(play_routing,
			  OXYGEN_PLAY_DAC2_SOURCE_SHIFT,
			  OXYGEN_PLAY_DAC1_SOURCE_SHIFT,
			  OXYGEN_PLAY_DAC1_SOURCE_MASK) |
	       shift_bits(play_routing,
			  OXYGEN_PLAY_DAC1_SOURCE_SHIFT,
			  OXYGEN_PLAY_DAC2_SOURCE_SHIFT,
			  OXYGEN_PLAY_DAC2_SOURCE_MASK) |
	       shift_bits(play_routing,
			  OXYGEN_PLAY_DAC0_SOURCE_SHIFT,
			  OXYGEN_PLAY_DAC3_SOURCE_SHIFT,
			  OXYGEN_PLAY_DAC3_SOURCE_MASK);
}

static int output_switch_info(struct snd_kcontrol *ctl,
			      struct snd_ctl_elem_info *info)
{
	static const char *const names[3] = {
		"Speakers", "Headphones", "FP Headphones"
	};

	return snd_ctl_enum_info(info, 1, 3, names);
}

static int output_switch_get(struct snd_kcontrol *ctl,
			     struct snd_ctl_elem_value *value)
{
	struct oxygen *chip = ctl->private_data;
	struct dg *data = chip->model_data;

	mutex_lock(&chip->mutex);
	value->value.enumerated.item[0] = data->output_sel;
	mutex_unlock(&chip->mutex);
	return 0;
}

static int output_switch_put(struct snd_kcontrol *ctl,
			     struct snd_ctl_elem_value *value)
{
	struct oxygen *chip = ctl->private_data;
	struct dg *data = chip->model_data;
	u8 reg;
	int changed;

	if (value->value.enumerated.item[0] > 2)
		return -EINVAL;

	mutex_lock(&chip->mutex);
	changed = value->value.enumerated.item[0] != data->output_sel;
	if (changed) {
		data->output_sel = value->value.enumerated.item[0];

		reg = data->cs4245_regs[CS4245_SIGNAL_SEL] &
						~CS4245_A_OUT_SEL_MASK;
		reg |= data->output_sel == 2 ?
				CS4245_A_OUT_SEL_DAC : CS4245_A_OUT_SEL_HIZ;
		cs4245_write_cached(chip, CS4245_SIGNAL_SEL, reg);

		cs4245_write_cached(chip, CS4245_DAC_A_CTRL,
				    data->output_sel ? data->hp_vol_att : 0);
		cs4245_write_cached(chip, CS4245_DAC_B_CTRL,
				    data->output_sel ? data->hp_vol_att : 0);

		oxygen_write16_masked(chip, OXYGEN_GPIO_DATA,
				      data->output_sel == 1 ? GPIO_HP_REAR : 0,
				      GPIO_HP_REAR);
	}
	mutex_unlock(&chip->mutex);
	return changed;
}

static int hp_volume_offset_info(struct snd_kcontrol *ctl,
				 struct snd_ctl_elem_info *info)
{
	static const char *const names[3] = {
		"< 64 ohms", "64-150 ohms", "150-300 ohms"
	};

	return snd_ctl_enum_info(info, 1, 3, names);
}

static int hp_volume_offset_get(struct snd_kcontrol *ctl,
				struct snd_ctl_elem_value *value)
{
	struct oxygen *chip = ctl->private_data;
	struct dg *data = chip->model_data;

	mutex_lock(&chip->mutex);
	if (data->hp_vol_att > 2 * 7)
		value->value.enumerated.item[0] = 0;
	else if (data->hp_vol_att > 0)
		value->value.enumerated.item[0] = 1;
	else
		value->value.enumerated.item[0] = 2;
	mutex_unlock(&chip->mutex);
	return 0;
}

static int hp_volume_offset_put(struct snd_kcontrol *ctl,
				struct snd_ctl_elem_value *value)
{
	static const s8 atts[3] = { 2 * 16, 2 * 7, 0 };
	struct oxygen *chip = ctl->private_data;
	struct dg *data = chip->model_data;
	s8 att;
	int changed;

	if (value->value.enumerated.item[0] > 2)
		return -EINVAL;
	att = atts[value->value.enumerated.item[0]];
	mutex_lock(&chip->mutex);
	changed = att != data->hp_vol_att;
	if (changed) {
		data->hp_vol_att = att;
		if (data->output_sel) {
			cs4245_write_cached(chip, CS4245_DAC_A_CTRL, att);
			cs4245_write_cached(chip, CS4245_DAC_B_CTRL, att);
		}
	}
	mutex_unlock(&chip->mutex);
	return changed;
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
			cs4245_write_cached(chip, CS4245_PGA_A_CTRL,
					    data->input_vol[idx][0]);
			cs4245_write_cached(chip, CS4245_PGA_B_CTRL,
					    data->input_vol[idx][1]);
		}
	}
	mutex_unlock(&chip->mutex);
	return changed;
}

static DECLARE_TLV_DB_SCALE(cs4245_pga_db_scale, -1200, 50, 0);

static int input_sel_info(struct snd_kcontrol *ctl,
			  struct snd_ctl_elem_info *info)
{
	static const char *const names[4] = {
		"Mic", "Aux", "Front Mic", "Line"
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
	static const u8 sel_values[4] = {
		CS4245_SEL_MIC,
		CS4245_SEL_INPUT_1,
		CS4245_SEL_INPUT_2,
		CS4245_SEL_INPUT_4
	};
	struct oxygen *chip = ctl->private_data;
	struct dg *data = chip->model_data;
	int changed;

	if (value->value.enumerated.item[0] > 3)
		return -EINVAL;

	mutex_lock(&chip->mutex);
	changed = value->value.enumerated.item[0] != data->input_sel;
	if (changed) {
		data->input_sel = value->value.enumerated.item[0];

		cs4245_write(chip, CS4245_ANALOG_IN,
			     (data->cs4245_regs[CS4245_ANALOG_IN] &
							~CS4245_SEL_MASK) |
			     sel_values[data->input_sel]);

		cs4245_write_cached(chip, CS4245_PGA_A_CTRL,
				    data->input_vol[data->input_sel][0]);
		cs4245_write_cached(chip, CS4245_PGA_B_CTRL,
				    data->input_vol[data->input_sel][1]);

		oxygen_write16_masked(chip, OXYGEN_GPIO_DATA,
				      data->input_sel ? 0 : GPIO_INPUT_ROUTE,
				      GPIO_INPUT_ROUTE);
	}
	mutex_unlock(&chip->mutex);
	return changed;
}

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
		!!(data->cs4245_regs[CS4245_ADC_CTRL] & CS4245_HPF_FREEZE);
	return 0;
}

static int hpf_put(struct snd_kcontrol *ctl, struct snd_ctl_elem_value *value)
{
	struct oxygen *chip = ctl->private_data;
	struct dg *data = chip->model_data;
	u8 reg;
	int changed;

	mutex_lock(&chip->mutex);
	reg = data->cs4245_regs[CS4245_ADC_CTRL] & ~CS4245_HPF_FREEZE;
	if (value->value.enumerated.item[0])
		reg |= CS4245_HPF_FREEZE;
	changed = reg != data->cs4245_regs[CS4245_ADC_CTRL];
	if (changed)
		cs4245_write(chip, CS4245_ADC_CTRL, reg);
	mutex_unlock(&chip->mutex);
	return changed;
}

#define INPUT_VOLUME(xname, index) { \
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
	.name = xname, \
	.info = input_vol_info, \
	.get = input_vol_get, \
	.put = input_vol_put, \
	.tlv = { .p = cs4245_pga_db_scale }, \
	.private_value = index, \
}
static const struct snd_kcontrol_new dg_controls[] = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Analog Output Playback Enum",
		.info = output_switch_info,
		.get = output_switch_get,
		.put = output_switch_put,
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Headphones Impedance Playback Enum",
		.info = hp_volume_offset_info,
		.get = hp_volume_offset_get,
		.put = hp_volume_offset_put,
	},
	INPUT_VOLUME("Mic Capture Volume", 0),
	INPUT_VOLUME("Aux Capture Volume", 1),
	INPUT_VOLUME("Front Mic Capture Volume", 2),
	INPUT_VOLUME("Line Capture Volume", 3),
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

	for (i = 0; i < ARRAY_SIZE(dg_controls); ++i) {
		err = snd_ctl_add(chip->card,
				  snd_ctl_new1(&dg_controls[i], chip));
		if (err < 0)
			return err;
	}
	return 0;
}

static void dump_cs4245_registers(struct oxygen *chip,
				  struct snd_info_buffer *buffer)
{
	struct dg *data = chip->model_data;
	unsigned int i;

	snd_iprintf(buffer, "\nCS4245:");
	for (i = 1; i <= 0x10; ++i)
		snd_iprintf(buffer, " %02x", data->cs4245_regs[i]);
	snd_iprintf(buffer, "\n");
}

struct oxygen_model model_xonar_dg = {
	.shortname = "Xonar DG",
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
			 CAPTURE_0_FROM_I2S_2,
	.dac_channels_pcm = 6,
	.dac_channels_mixer = 0,
	.function_flags = OXYGEN_FUNCTION_SPI,
	.dac_mclks = OXYGEN_MCLKS(256, 128, 128),
	.adc_mclks = OXYGEN_MCLKS(256, 128, 128),
	.dac_i2s_format = OXYGEN_I2S_FORMAT_LJUST,
	.adc_i2s_format = OXYGEN_I2S_FORMAT_LJUST,
};
