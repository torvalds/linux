/*
 * card driver for the Xonar DG/DGX
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

/*
 * Xonar DG/DGX
 * ------------
 *
 * CS4245 and CS4361 both will mute all outputs if any clock ratio
 * is invalid.
 *
 * CMI8788:
 *
 *   SPI 0 -> CS4245
 *
 *   Playback:
 *   I²S 1 -> CS4245
 *   I²S 2 -> CS4361 (center/LFE)
 *   I²S 3 -> CS4361 (surround)
 *   I²S 4 -> CS4361 (front)
 *   Capture:
 *   I²S ADC 1 <- CS4245
 *
 *   GPIO 3 <- ?
 *   GPIO 4 <- headphone detect
 *   GPIO 5 -> enable ADC analog circuit for the left channel
 *   GPIO 6 -> enable ADC analog circuit for the right channel
 *   GPIO 7 -> switch green rear output jack between CS4245 and and the first
 *             channel of CS4361 (mechanical relay)
 *   GPIO 8 -> enable output to speakers
 *
 * CS4245:
 *
 *   input 0 <- mic
 *   input 1 <- aux
 *   input 2 <- front mic
 *   input 4 <- line
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

int cs4245_write_spi(struct oxygen *chip, u8 reg)
{
	struct dg *data = chip->model_data;
	unsigned int packet;

	packet = reg << 8;
	packet |= (CS4245_SPI_ADDRESS | CS4245_SPI_WRITE) << 16;
	packet |= data->cs4245_shadow[reg];

	return oxygen_write_spi(chip, OXYGEN_SPI_TRIGGER |
				OXYGEN_SPI_DATA_LENGTH_3 |
				OXYGEN_SPI_CLOCK_1280 |
				(0 << OXYGEN_SPI_CODEC_SHIFT) |
				OXYGEN_SPI_CEN_LATCH_CLOCK_HI,
				packet);
}

int cs4245_read_spi(struct oxygen *chip, u8 addr)
{
	struct dg *data = chip->model_data;
	int ret;

	ret = oxygen_write_spi(chip, OXYGEN_SPI_TRIGGER |
		OXYGEN_SPI_DATA_LENGTH_2 |
		OXYGEN_SPI_CEN_LATCH_CLOCK_HI |
		OXYGEN_SPI_CLOCK_1280 | (0 << OXYGEN_SPI_CODEC_SHIFT),
		((CS4245_SPI_ADDRESS | CS4245_SPI_WRITE) << 8) | addr);
	if (ret < 0)
		return ret;

	ret = oxygen_write_spi(chip, OXYGEN_SPI_TRIGGER |
		OXYGEN_SPI_DATA_LENGTH_2 |
		OXYGEN_SPI_CEN_LATCH_CLOCK_HI |
		OXYGEN_SPI_CLOCK_1280 | (0 << OXYGEN_SPI_CODEC_SHIFT),
		(CS4245_SPI_ADDRESS | CS4245_SPI_READ) << 8);
	if (ret < 0)
		return ret;

	data->cs4245_shadow[addr] = oxygen_read8(chip, OXYGEN_SPI_DATA1);

	return 0;
}

int cs4245_shadow_control(struct oxygen *chip, enum cs4245_shadow_operation op)
{
	struct dg *data = chip->model_data;
	unsigned char addr;
	int ret;

	for (addr = 1; addr < ARRAY_SIZE(data->cs4245_shadow); addr++) {
		ret = (op == CS4245_SAVE_TO_SHADOW ?
			cs4245_read_spi(chip, addr) :
			cs4245_write_spi(chip, addr));
		if (ret < 0)
			return ret;
	}
	return 0;
}

static void cs4245_write(struct oxygen *chip, unsigned int reg, u8 value)
{
	struct dg *data = chip->model_data;

	oxygen_write_spi(chip, OXYGEN_SPI_TRIGGER |
			 OXYGEN_SPI_DATA_LENGTH_3 |
			 OXYGEN_SPI_CLOCK_1280 |
			 (0 << OXYGEN_SPI_CODEC_SHIFT) |
			 OXYGEN_SPI_CEN_LATCH_CLOCK_HI,
			 CS4245_SPI_ADDRESS_S |
			 CS4245_SPI_WRITE_S |
			 (reg << 8) | value);
	data->cs4245_shadow[reg] = value;
}

static void cs4245_write_cached(struct oxygen *chip, unsigned int reg, u8 value)
{
	struct dg *data = chip->model_data;

	if (value != data->cs4245_shadow[reg])
		cs4245_write(chip, reg, value);
}

static void cs4245_init(struct oxygen *chip)
{
	struct dg *data = chip->model_data;

	/* save the initial state: codec version, registers */
	cs4245_shadow_control(chip, CS4245_SAVE_TO_SHADOW);

	/*
	 * Power up the CODEC internals, enable soft ramp & zero cross, work in
	 * async. mode, enable aux output from DAC. Invert DAC output as in the
	 * Windows driver.
	 */
	data->cs4245_shadow[CS4245_POWER_CTRL] = 0;
	data->cs4245_shadow[CS4245_SIGNAL_SEL] =
		CS4245_A_OUT_SEL_DAC | CS4245_ASYNCH;
	data->cs4245_shadow[CS4245_DAC_CTRL_1] =
		CS4245_DAC_FM_SINGLE | CS4245_DAC_DIF_LJUST;
	data->cs4245_shadow[CS4245_DAC_CTRL_2] =
		CS4245_DAC_SOFT | CS4245_DAC_ZERO | CS4245_INVERT_DAC;
	data->cs4245_shadow[CS4245_ADC_CTRL] =
		CS4245_ADC_FM_SINGLE | CS4245_ADC_DIF_LJUST;
	data->cs4245_shadow[CS4245_ANALOG_IN] =
		CS4245_PGA_SOFT | CS4245_PGA_ZERO;
	data->cs4245_shadow[CS4245_PGA_B_CTRL] = 0;
	data->cs4245_shadow[CS4245_PGA_A_CTRL] = 0;
	data->cs4245_shadow[CS4245_DAC_A_CTRL] = 4;
	data->cs4245_shadow[CS4245_DAC_B_CTRL] = 4;

	cs4245_shadow_control(chip, CS4245_LOAD_FROM_SHADOW);
	snd_component_add(chip->card, "CS4245");
}

static void dg_init(struct oxygen *chip)
{
	struct dg *data = chip->model_data;

	data->output_sel = 0;
	data->input_sel = 3;
	data->hp_vol_att = 2 * 16;

	cs4245_init(chip);
	oxygen_write16(chip, OXYGEN_GPIO_CONTROL,
		       GPIO_OUTPUT_ENABLE | GPIO_HP_REAR | GPIO_INPUT_ROUTE);
	oxygen_write16(chip, OXYGEN_GPIO_DATA, GPIO_INPUT_ROUTE);
	msleep(2500); /* anti-pop delay */
	oxygen_write16(chip, OXYGEN_GPIO_DATA,
		       GPIO_OUTPUT_ENABLE | GPIO_INPUT_ROUTE);
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
	cs4245_shadow_control(chip, CS4245_LOAD_FROM_SHADOW);
	msleep(2500);
	oxygen_set_bits16(chip, OXYGEN_GPIO_DATA, GPIO_OUTPUT_ENABLE);
}

static void set_cs4245_dac_params(struct oxygen *chip,
				  struct snd_pcm_hw_params *params)
{
	struct dg *data = chip->model_data;
	unsigned char dac_ctrl;
	unsigned char mclk_freq;

	dac_ctrl = data->cs4245_shadow[CS4245_DAC_CTRL_1] & ~CS4245_DAC_FM_MASK;
	mclk_freq = data->cs4245_shadow[CS4245_MCLK_FREQ] & ~CS4245_MCLK1_MASK;
	if (params_rate(params) <= 50000) {
		dac_ctrl |= CS4245_DAC_FM_SINGLE;
		mclk_freq |= CS4245_MCLK_1 << CS4245_MCLK1_SHIFT;
	} else if (params_rate(params) <= 100000) {
		dac_ctrl |= CS4245_DAC_FM_DOUBLE;
		mclk_freq |= CS4245_MCLK_1 << CS4245_MCLK1_SHIFT;
	} else {
		dac_ctrl |= CS4245_DAC_FM_QUAD;
		mclk_freq |= CS4245_MCLK_2 << CS4245_MCLK1_SHIFT;
	}
	data->cs4245_shadow[CS4245_DAC_CTRL_1] = dac_ctrl;
	data->cs4245_shadow[CS4245_MCLK_FREQ] = mclk_freq;
	cs4245_write_spi(chip, CS4245_DAC_CTRL_1);
	cs4245_write_spi(chip, CS4245_MCLK_FREQ);
}

static void set_cs4245_adc_params(struct oxygen *chip,
				  struct snd_pcm_hw_params *params)
{
	struct dg *data = chip->model_data;
	unsigned char adc_ctrl;
	unsigned char mclk_freq;

	adc_ctrl = data->cs4245_shadow[CS4245_ADC_CTRL] & ~CS4245_ADC_FM_MASK;
	mclk_freq = data->cs4245_shadow[CS4245_MCLK_FREQ] & ~CS4245_MCLK2_MASK;
	if (params_rate(params) <= 50000) {
		adc_ctrl |= CS4245_ADC_FM_SINGLE;
		mclk_freq |= CS4245_MCLK_1 << CS4245_MCLK2_SHIFT;
	} else if (params_rate(params) <= 100000) {
		adc_ctrl |= CS4245_ADC_FM_DOUBLE;
		mclk_freq |= CS4245_MCLK_1 << CS4245_MCLK2_SHIFT;
	} else {
		adc_ctrl |= CS4245_ADC_FM_QUAD;
		mclk_freq |= CS4245_MCLK_2 << CS4245_MCLK2_SHIFT;
	}
	data->cs4245_shadow[CS4245_ADC_CTRL] = adc_ctrl;
	data->cs4245_shadow[CS4245_MCLK_FREQ] = mclk_freq;
	cs4245_write_spi(chip, CS4245_ADC_CTRL);
	cs4245_write_spi(chip, CS4245_MCLK_FREQ);
}

static unsigned int adjust_dg_dac_routing(struct oxygen *chip,
					  unsigned int play_routing)
{
	struct dg *data = chip->model_data;
	unsigned int routing = 0;

	switch (data->pcm_output) {
	case PLAYBACK_DST_HP:
	case PLAYBACK_DST_HP_FP:
		oxygen_write8_masked(chip, OXYGEN_PLAY_ROUTING,
			OXYGEN_PLAY_MUTE23 | OXYGEN_PLAY_MUTE45 |
			OXYGEN_PLAY_MUTE67, OXYGEN_PLAY_MUTE_MASK);
		break;
	case PLAYBACK_DST_MULTICH:
		routing = (0 << OXYGEN_PLAY_DAC0_SOURCE_SHIFT) |
			  (2 << OXYGEN_PLAY_DAC1_SOURCE_SHIFT) |
			  (1 << OXYGEN_PLAY_DAC2_SOURCE_SHIFT) |
			  (0 << OXYGEN_PLAY_DAC3_SOURCE_SHIFT);
		oxygen_write8_masked(chip, OXYGEN_PLAY_ROUTING,
			OXYGEN_PLAY_MUTE01, OXYGEN_PLAY_MUTE_MASK);
		break;
	}
	return routing;
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

		reg = data->cs4245_shadow[CS4245_SIGNAL_SEL] &
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
			     (data->cs4245_shadow[CS4245_ANALOG_IN] &
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
	unsigned int addr;

	snd_iprintf(buffer, "\nCS4245:");
	cs4245_read_spi(chip, CS4245_INT_STATUS);
	for (addr = 1; addr < ARRAY_SIZE(data->cs4245_shadow); addr++)
		snd_iprintf(buffer, " %02x", data->cs4245_shadow[addr]);
	snd_iprintf(buffer, "\n");
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
			 CAPTURE_0_FROM_I2S_2 |
			 CAPTURE_1_FROM_SPDIF,
	.dac_channels_pcm = 6,
	.dac_channels_mixer = 0,
	.function_flags = OXYGEN_FUNCTION_SPI,
	.dac_mclks = OXYGEN_MCLKS(256, 128, 128),
	.adc_mclks = OXYGEN_MCLKS(256, 128, 128),
	.dac_i2s_format = OXYGEN_I2S_FORMAT_LJUST,
	.adc_i2s_format = OXYGEN_I2S_FORMAT_LJUST,
};
