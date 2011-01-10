/*
 * C-Media CMI8788 driver for C-Media's reference design and similar models
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
 *  along with this driver; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

/*
 * CMI8788:
 *
 *   SPI 0 -> 1st AK4396 (front)
 *   SPI 1 -> 2nd AK4396 (surround)
 *   SPI 2 -> 3rd AK4396 (center/LFE)
 *   SPI 3 -> WM8785
 *   SPI 4 -> 4th AK4396 (back)
 *
 *   GPIO 0 -> DFS0 of AK5385
 *   GPIO 1 -> DFS1 of AK5385
 *   GPIO 8 -> enable headphone amplifier on HT-Omega models
 *
 * CM9780:
 *
 *   LINE_OUT -> input of ADC
 *
 *   AUX_IN <- aux
 *   CD_IN  <- CD
 *   MIC_IN <- mic
 *
 *   GPO 0 -> route line-in (0) or AC97 output (1) to ADC input
 */

#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <sound/ac97_codec.h>
#include <sound/control.h>
#include <sound/core.h>
#include <sound/info.h>
#include <sound/initval.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/tlv.h>
#include "oxygen.h"
#include "xonar_dg.h"
#include "ak4396.h"
#include "wm8785.h"

MODULE_AUTHOR("Clemens Ladisch <clemens@ladisch.de>");
MODULE_DESCRIPTION("C-Media CMI8788 driver");
MODULE_LICENSE("GPL v2");
MODULE_SUPPORTED_DEVICE("{{C-Media,CMI8786}"
			",{C-Media,CMI8787}"
			",{C-Media,CMI8788}}");

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;
static int enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_PNP;

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "card index");
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string");
module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "enable card");

enum {
	MODEL_CMEDIA_REF,
	MODEL_MERIDIAN,
	MODEL_CLARO,
	MODEL_CLARO_HALO,
	MODEL_FANTASIA,
	MODEL_2CH_OUTPUT,
	MODEL_XONAR_DG,
};

static DEFINE_PCI_DEVICE_TABLE(oxygen_ids) = {
	/* C-Media's reference design */
	{ OXYGEN_PCI_SUBID(0x10b0, 0x0216), .driver_data = MODEL_CMEDIA_REF },
	{ OXYGEN_PCI_SUBID(0x10b0, 0x0217), .driver_data = MODEL_CMEDIA_REF },
	{ OXYGEN_PCI_SUBID(0x10b0, 0x0218), .driver_data = MODEL_CMEDIA_REF },
	{ OXYGEN_PCI_SUBID(0x10b0, 0x0219), .driver_data = MODEL_CMEDIA_REF },
	{ OXYGEN_PCI_SUBID(0x13f6, 0x0001), .driver_data = MODEL_CMEDIA_REF },
	{ OXYGEN_PCI_SUBID(0x13f6, 0x0010), .driver_data = MODEL_CMEDIA_REF },
	{ OXYGEN_PCI_SUBID(0x13f6, 0x8788), .driver_data = MODEL_CMEDIA_REF },
	{ OXYGEN_PCI_SUBID(0x147a, 0xa017), .driver_data = MODEL_CMEDIA_REF },
	{ OXYGEN_PCI_SUBID(0x1a58, 0x0910), .driver_data = MODEL_CMEDIA_REF },
	/* Asus Xonar DG */
	{ OXYGEN_PCI_SUBID(0x1043, 0x8467), .driver_data = MODEL_XONAR_DG },
	/* PCI 2.0 HD Audio */
	{ OXYGEN_PCI_SUBID(0x13f6, 0x8782), .driver_data = MODEL_2CH_OUTPUT },
	/* Kuroutoshikou CMI8787-HG2PCI */
	{ OXYGEN_PCI_SUBID(0x13f6, 0xffff), .driver_data = MODEL_2CH_OUTPUT },
	/* TempoTec HiFier Fantasia */
	{ OXYGEN_PCI_SUBID(0x14c3, 0x1710), .driver_data = MODEL_FANTASIA },
	/* TempoTec HiFier Serenade */
	{ OXYGEN_PCI_SUBID(0x14c3, 0x1711), .driver_data = MODEL_2CH_OUTPUT },
	/* AuzenTech X-Meridian */
	{ OXYGEN_PCI_SUBID(0x415a, 0x5431), .driver_data = MODEL_MERIDIAN },
	/* AuzenTech X-Meridian 2G */
	{ OXYGEN_PCI_SUBID(0x5431, 0x017a), .driver_data = MODEL_MERIDIAN },
	/* HT-Omega Claro */
	{ OXYGEN_PCI_SUBID(0x7284, 0x9761), .driver_data = MODEL_CLARO },
	/* HT-Omega Claro halo */
	{ OXYGEN_PCI_SUBID(0x7284, 0x9781), .driver_data = MODEL_CLARO_HALO },
	{ }
};
MODULE_DEVICE_TABLE(pci, oxygen_ids);


#define GPIO_AK5385_DFS_MASK	0x0003
#define GPIO_AK5385_DFS_NORMAL	0x0000
#define GPIO_AK5385_DFS_DOUBLE	0x0001
#define GPIO_AK5385_DFS_QUAD	0x0002

#define GPIO_CLARO_HP		0x0100

struct generic_data {
	unsigned int dacs;
	u8 ak4396_regs[4][5];
	u16 wm8785_regs[3];
};

static void ak4396_write(struct oxygen *chip, unsigned int codec,
			 u8 reg, u8 value)
{
	/* maps ALSA channel pair number to SPI output */
	static const u8 codec_spi_map[4] = {
		0, 1, 2, 4
	};
	struct generic_data *data = chip->model_data;

	oxygen_write_spi(chip, OXYGEN_SPI_TRIGGER |
			 OXYGEN_SPI_DATA_LENGTH_2 |
			 OXYGEN_SPI_CLOCK_160 |
			 (codec_spi_map[codec] << OXYGEN_SPI_CODEC_SHIFT) |
			 OXYGEN_SPI_CEN_LATCH_CLOCK_HI,
			 AK4396_WRITE | (reg << 8) | value);
	data->ak4396_regs[codec][reg] = value;
}

static void ak4396_write_cached(struct oxygen *chip, unsigned int codec,
				u8 reg, u8 value)
{
	struct generic_data *data = chip->model_data;

	if (value != data->ak4396_regs[codec][reg])
		ak4396_write(chip, codec, reg, value);
}

static void wm8785_write(struct oxygen *chip, u8 reg, unsigned int value)
{
	struct generic_data *data = chip->model_data;

	oxygen_write_spi(chip, OXYGEN_SPI_TRIGGER |
			 OXYGEN_SPI_DATA_LENGTH_2 |
			 OXYGEN_SPI_CLOCK_160 |
			 (3 << OXYGEN_SPI_CODEC_SHIFT) |
			 OXYGEN_SPI_CEN_LATCH_CLOCK_LO,
			 (reg << 9) | value);
	if (reg < ARRAY_SIZE(data->wm8785_regs))
		data->wm8785_regs[reg] = value;
}

static void ak4396_registers_init(struct oxygen *chip)
{
	struct generic_data *data = chip->model_data;
	unsigned int i;

	for (i = 0; i < data->dacs; ++i) {
		ak4396_write(chip, i, AK4396_CONTROL_1,
			     AK4396_DIF_24_MSB | AK4396_RSTN);
		ak4396_write(chip, i, AK4396_CONTROL_2,
			     data->ak4396_regs[0][AK4396_CONTROL_2]);
		ak4396_write(chip, i, AK4396_CONTROL_3,
			     AK4396_PCM);
		ak4396_write(chip, i, AK4396_LCH_ATT,
			     chip->dac_volume[i * 2]);
		ak4396_write(chip, i, AK4396_RCH_ATT,
			     chip->dac_volume[i * 2 + 1]);
	}
}

static void ak4396_init(struct oxygen *chip)
{
	struct generic_data *data = chip->model_data;

	data->dacs = chip->model.dac_channels_pcm / 2;
	data->ak4396_regs[0][AK4396_CONTROL_2] =
		AK4396_SMUTE | AK4396_DEM_OFF | AK4396_DFS_NORMAL;
	ak4396_registers_init(chip);
	snd_component_add(chip->card, "AK4396");
}

static void ak5385_init(struct oxygen *chip)
{
	oxygen_set_bits16(chip, OXYGEN_GPIO_CONTROL, GPIO_AK5385_DFS_MASK);
	oxygen_clear_bits16(chip, OXYGEN_GPIO_DATA, GPIO_AK5385_DFS_MASK);
	snd_component_add(chip->card, "AK5385");
}

static void wm8785_registers_init(struct oxygen *chip)
{
	struct generic_data *data = chip->model_data;

	wm8785_write(chip, WM8785_R7, 0);
	wm8785_write(chip, WM8785_R0, data->wm8785_regs[0]);
	wm8785_write(chip, WM8785_R2, data->wm8785_regs[2]);
}

static void wm8785_init(struct oxygen *chip)
{
	struct generic_data *data = chip->model_data;

	data->wm8785_regs[0] =
		WM8785_MCR_SLAVE | WM8785_OSR_SINGLE | WM8785_FORMAT_LJUST;
	data->wm8785_regs[2] = WM8785_HPFR | WM8785_HPFL;
	wm8785_registers_init(chip);
	snd_component_add(chip->card, "WM8785");
}

static void generic_init(struct oxygen *chip)
{
	ak4396_init(chip);
	wm8785_init(chip);
}

static void meridian_init(struct oxygen *chip)
{
	ak4396_init(chip);
	ak5385_init(chip);
}

static void claro_enable_hp(struct oxygen *chip)
{
	msleep(300);
	oxygen_set_bits16(chip, OXYGEN_GPIO_CONTROL, GPIO_CLARO_HP);
	oxygen_set_bits16(chip, OXYGEN_GPIO_DATA, GPIO_CLARO_HP);
}

static void claro_init(struct oxygen *chip)
{
	ak4396_init(chip);
	wm8785_init(chip);
	claro_enable_hp(chip);
}

static void claro_halo_init(struct oxygen *chip)
{
	ak4396_init(chip);
	ak5385_init(chip);
	claro_enable_hp(chip);
}

static void fantasia_init(struct oxygen *chip)
{
	ak4396_init(chip);
	snd_component_add(chip->card, "CS5340");
}

static void stereo_output_init(struct oxygen *chip)
{
	ak4396_init(chip);
}

static void generic_cleanup(struct oxygen *chip)
{
}

static void claro_disable_hp(struct oxygen *chip)
{
	oxygen_clear_bits16(chip, OXYGEN_GPIO_DATA, GPIO_CLARO_HP);
}

static void claro_cleanup(struct oxygen *chip)
{
	claro_disable_hp(chip);
}

static void claro_suspend(struct oxygen *chip)
{
	claro_disable_hp(chip);
}

static void generic_resume(struct oxygen *chip)
{
	ak4396_registers_init(chip);
	wm8785_registers_init(chip);
}

static void meridian_resume(struct oxygen *chip)
{
	ak4396_registers_init(chip);
}

static void claro_resume(struct oxygen *chip)
{
	ak4396_registers_init(chip);
	claro_enable_hp(chip);
}

static void stereo_resume(struct oxygen *chip)
{
	ak4396_registers_init(chip);
}

static void set_ak4396_params(struct oxygen *chip,
			      struct snd_pcm_hw_params *params)
{
	struct generic_data *data = chip->model_data;
	unsigned int i;
	u8 value;

	value = data->ak4396_regs[0][AK4396_CONTROL_2] & ~AK4396_DFS_MASK;
	if (params_rate(params) <= 54000)
		value |= AK4396_DFS_NORMAL;
	else if (params_rate(params) <= 108000)
		value |= AK4396_DFS_DOUBLE;
	else
		value |= AK4396_DFS_QUAD;

	msleep(1); /* wait for the new MCLK to become stable */

	if (value != data->ak4396_regs[0][AK4396_CONTROL_2]) {
		for (i = 0; i < data->dacs; ++i) {
			ak4396_write(chip, i, AK4396_CONTROL_1,
				     AK4396_DIF_24_MSB);
			ak4396_write(chip, i, AK4396_CONTROL_2, value);
			ak4396_write(chip, i, AK4396_CONTROL_1,
				     AK4396_DIF_24_MSB | AK4396_RSTN);
		}
	}
}

static void update_ak4396_volume(struct oxygen *chip)
{
	struct generic_data *data = chip->model_data;
	unsigned int i;

	for (i = 0; i < data->dacs; ++i) {
		ak4396_write_cached(chip, i, AK4396_LCH_ATT,
				    chip->dac_volume[i * 2]);
		ak4396_write_cached(chip, i, AK4396_RCH_ATT,
				    chip->dac_volume[i * 2 + 1]);
	}
}

static void update_ak4396_mute(struct oxygen *chip)
{
	struct generic_data *data = chip->model_data;
	unsigned int i;
	u8 value;

	value = data->ak4396_regs[0][AK4396_CONTROL_2] & ~AK4396_SMUTE;
	if (chip->dac_mute)
		value |= AK4396_SMUTE;
	for (i = 0; i < data->dacs; ++i)
		ak4396_write_cached(chip, i, AK4396_CONTROL_2, value);
}

static void set_wm8785_params(struct oxygen *chip,
			      struct snd_pcm_hw_params *params)
{
	struct generic_data *data = chip->model_data;
	unsigned int value;

	value = WM8785_MCR_SLAVE | WM8785_FORMAT_LJUST;
	if (params_rate(params) <= 48000)
		value |= WM8785_OSR_SINGLE;
	else if (params_rate(params) <= 96000)
		value |= WM8785_OSR_DOUBLE;
	else
		value |= WM8785_OSR_QUAD;
	if (value != data->wm8785_regs[0]) {
		wm8785_write(chip, WM8785_R7, 0);
		wm8785_write(chip, WM8785_R0, value);
		wm8785_write(chip, WM8785_R2, data->wm8785_regs[2]);
	}
}

static void set_ak5385_params(struct oxygen *chip,
			      struct snd_pcm_hw_params *params)
{
	unsigned int value;

	if (params_rate(params) <= 54000)
		value = GPIO_AK5385_DFS_NORMAL;
	else if (params_rate(params) <= 108000)
		value = GPIO_AK5385_DFS_DOUBLE;
	else
		value = GPIO_AK5385_DFS_QUAD;
	oxygen_write16_masked(chip, OXYGEN_GPIO_DATA,
			      value, GPIO_AK5385_DFS_MASK);
}

static void set_no_params(struct oxygen *chip, struct snd_pcm_hw_params *params)
{
}

static int rolloff_info(struct snd_kcontrol *ctl,
			struct snd_ctl_elem_info *info)
{
	static const char *const names[2] = {
		"Sharp Roll-off", "Slow Roll-off"
	};

	info->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	info->count = 1;
	info->value.enumerated.items = 2;
	if (info->value.enumerated.item >= 2)
		info->value.enumerated.item = 1;
	strcpy(info->value.enumerated.name, names[info->value.enumerated.item]);
	return 0;
}

static int rolloff_get(struct snd_kcontrol *ctl,
		       struct snd_ctl_elem_value *value)
{
	struct oxygen *chip = ctl->private_data;
	struct generic_data *data = chip->model_data;

	value->value.enumerated.item[0] =
		(data->ak4396_regs[0][AK4396_CONTROL_2] & AK4396_SLOW) != 0;
	return 0;
}

static int rolloff_put(struct snd_kcontrol *ctl,
		       struct snd_ctl_elem_value *value)
{
	struct oxygen *chip = ctl->private_data;
	struct generic_data *data = chip->model_data;
	unsigned int i;
	int changed;
	u8 reg;

	mutex_lock(&chip->mutex);
	reg = data->ak4396_regs[0][AK4396_CONTROL_2];
	if (value->value.enumerated.item[0])
		reg |= AK4396_SLOW;
	else
		reg &= ~AK4396_SLOW;
	changed = reg != data->ak4396_regs[0][AK4396_CONTROL_2];
	if (changed) {
		for (i = 0; i < data->dacs; ++i)
			ak4396_write(chip, i, AK4396_CONTROL_2, reg);
	}
	mutex_unlock(&chip->mutex);
	return changed;
}

static const struct snd_kcontrol_new rolloff_control = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "DAC Filter Playback Enum",
	.info = rolloff_info,
	.get = rolloff_get,
	.put = rolloff_put,
};

static int hpf_info(struct snd_kcontrol *ctl, struct snd_ctl_elem_info *info)
{
	static const char *const names[2] = {
		"None", "High-pass Filter"
	};

	info->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	info->count = 1;
	info->value.enumerated.items = 2;
	if (info->value.enumerated.item >= 2)
		info->value.enumerated.item = 1;
	strcpy(info->value.enumerated.name, names[info->value.enumerated.item]);
	return 0;
}

static int hpf_get(struct snd_kcontrol *ctl, struct snd_ctl_elem_value *value)
{
	struct oxygen *chip = ctl->private_data;
	struct generic_data *data = chip->model_data;

	value->value.enumerated.item[0] =
		(data->wm8785_regs[WM8785_R2] & WM8785_HPFR) != 0;
	return 0;
}

static int hpf_put(struct snd_kcontrol *ctl, struct snd_ctl_elem_value *value)
{
	struct oxygen *chip = ctl->private_data;
	struct generic_data *data = chip->model_data;
	unsigned int reg;
	int changed;

	mutex_lock(&chip->mutex);
	reg = data->wm8785_regs[WM8785_R2] & ~(WM8785_HPFR | WM8785_HPFL);
	if (value->value.enumerated.item[0])
		reg |= WM8785_HPFR | WM8785_HPFL;
	changed = reg != data->wm8785_regs[WM8785_R2];
	if (changed)
		wm8785_write(chip, WM8785_R2, reg);
	mutex_unlock(&chip->mutex);
	return changed;
}

static const struct snd_kcontrol_new hpf_control = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "ADC Filter Capture Enum",
	.info = hpf_info,
	.get = hpf_get,
	.put = hpf_put,
};

static int generic_mixer_init(struct oxygen *chip)
{
	return snd_ctl_add(chip->card, snd_ctl_new1(&rolloff_control, chip));
}

static int generic_wm8785_mixer_init(struct oxygen *chip)
{
	int err;

	err = generic_mixer_init(chip);
	if (err < 0)
		return err;
	err = snd_ctl_add(chip->card, snd_ctl_new1(&hpf_control, chip));
	if (err < 0)
		return err;
	return 0;
}

static void dump_ak4396_registers(struct oxygen *chip,
				  struct snd_info_buffer *buffer)
{
	struct generic_data *data = chip->model_data;
	unsigned int dac, i;

	for (dac = 0; dac < data->dacs; ++dac) {
		snd_iprintf(buffer, "\nAK4396 %u:", dac + 1);
		for (i = 0; i < 5; ++i)
			snd_iprintf(buffer, " %02x", data->ak4396_regs[dac][i]);
	}
	snd_iprintf(buffer, "\n");
}

static void dump_wm8785_registers(struct oxygen *chip,
				  struct snd_info_buffer *buffer)
{
	struct generic_data *data = chip->model_data;
	unsigned int i;

	snd_iprintf(buffer, "\nWM8785:");
	for (i = 0; i < 3; ++i)
		snd_iprintf(buffer, " %03x", data->wm8785_regs[i]);
	snd_iprintf(buffer, "\n");
}

static void dump_oxygen_registers(struct oxygen *chip,
				  struct snd_info_buffer *buffer)
{
	dump_ak4396_registers(chip, buffer);
	dump_wm8785_registers(chip, buffer);
}

static const DECLARE_TLV_DB_LINEAR(ak4396_db_scale, TLV_DB_GAIN_MUTE, 0);

static const struct oxygen_model model_generic = {
	.shortname = "C-Media CMI8788",
	.longname = "C-Media Oxygen HD Audio",
	.chip = "CMI8788",
	.init = generic_init,
	.mixer_init = generic_wm8785_mixer_init,
	.cleanup = generic_cleanup,
	.resume = generic_resume,
	.set_dac_params = set_ak4396_params,
	.set_adc_params = set_wm8785_params,
	.update_dac_volume = update_ak4396_volume,
	.update_dac_mute = update_ak4396_mute,
	.dump_registers = dump_oxygen_registers,
	.dac_tlv = ak4396_db_scale,
	.model_data_size = sizeof(struct generic_data),
	.device_config = PLAYBACK_0_TO_I2S |
			 PLAYBACK_1_TO_SPDIF |
			 PLAYBACK_2_TO_AC97_1 |
			 CAPTURE_0_FROM_I2S_1 |
			 CAPTURE_1_FROM_SPDIF |
			 CAPTURE_2_FROM_AC97_1 |
			 AC97_CD_INPUT,
	.dac_channels_pcm = 8,
	.dac_channels_mixer = 8,
	.dac_volume_min = 0,
	.dac_volume_max = 255,
	.function_flags = OXYGEN_FUNCTION_SPI |
			  OXYGEN_FUNCTION_ENABLE_SPI_4_5,
	.dac_mclks = OXYGEN_MCLKS(256, 128, 128),
	.adc_mclks = OXYGEN_MCLKS(256, 256, 128),
	.dac_i2s_format = OXYGEN_I2S_FORMAT_LJUST,
	.adc_i2s_format = OXYGEN_I2S_FORMAT_LJUST,
};

static int __devinit get_oxygen_model(struct oxygen *chip,
				      const struct pci_device_id *id)
{
	chip->model = model_generic;
	switch (id->driver_data) {
	case MODEL_MERIDIAN:
		chip->model.init = meridian_init;
		chip->model.mixer_init = generic_mixer_init;
		chip->model.resume = meridian_resume;
		chip->model.set_adc_params = set_ak5385_params;
		chip->model.dump_registers = dump_ak4396_registers;
		chip->model.device_config = PLAYBACK_0_TO_I2S |
					    PLAYBACK_1_TO_SPDIF |
					    CAPTURE_0_FROM_I2S_2 |
					    CAPTURE_1_FROM_SPDIF;
		break;
	case MODEL_CLARO:
		chip->model.init = claro_init;
		chip->model.cleanup = claro_cleanup;
		chip->model.suspend = claro_suspend;
		chip->model.resume = claro_resume;
		break;
	case MODEL_CLARO_HALO:
		chip->model.init = claro_halo_init;
		chip->model.mixer_init = generic_mixer_init;
		chip->model.cleanup = claro_cleanup;
		chip->model.suspend = claro_suspend;
		chip->model.resume = claro_resume;
		chip->model.set_adc_params = set_ak5385_params;
		chip->model.dump_registers = dump_ak4396_registers;
		chip->model.device_config = PLAYBACK_0_TO_I2S |
					    PLAYBACK_1_TO_SPDIF |
					    CAPTURE_0_FROM_I2S_2 |
					    CAPTURE_1_FROM_SPDIF;
		break;
	case MODEL_FANTASIA:
	case MODEL_2CH_OUTPUT:
		chip->model.shortname = "C-Media CMI8787";
		chip->model.chip = "CMI8787";
		if (id->driver_data == MODEL_FANTASIA)
			chip->model.init = fantasia_init;
		else
			chip->model.init = stereo_output_init;
		chip->model.resume = stereo_resume;
		chip->model.mixer_init = generic_mixer_init;
		chip->model.set_adc_params = set_no_params;
		chip->model.dump_registers = dump_ak4396_registers;
		chip->model.device_config = PLAYBACK_0_TO_I2S |
					    PLAYBACK_1_TO_SPDIF;
		if (id->driver_data == MODEL_FANTASIA) {
			chip->model.device_config |= CAPTURE_0_FROM_I2S_1;
			chip->model.adc_mclks = OXYGEN_MCLKS(256, 128, 128);
		}
		chip->model.dac_channels_pcm = 2;
		chip->model.dac_channels_mixer = 2;
		break;
	case MODEL_XONAR_DG:
		chip->model = model_xonar_dg;
		break;
	}
	if (id->driver_data == MODEL_MERIDIAN ||
	    id->driver_data == MODEL_CLARO_HALO) {
		chip->model.misc_flags = OXYGEN_MISC_MIDI;
		chip->model.device_config |= MIDI_OUTPUT | MIDI_INPUT;
	}
	return 0;
}

static int __devinit generic_oxygen_probe(struct pci_dev *pci,
					  const struct pci_device_id *pci_id)
{
	static int dev;
	int err;

	if (dev >= SNDRV_CARDS)
		return -ENODEV;
	if (!enable[dev]) {
		++dev;
		return -ENOENT;
	}
	err = oxygen_pci_probe(pci, index[dev], id[dev], THIS_MODULE,
			       oxygen_ids, get_oxygen_model);
	if (err >= 0)
		++dev;
	return err;
}

static struct pci_driver oxygen_driver = {
	.name = "CMI8788",
	.id_table = oxygen_ids,
	.probe = generic_oxygen_probe,
	.remove = __devexit_p(oxygen_pci_remove),
#ifdef CONFIG_PM
	.suspend = oxygen_pci_suspend,
	.resume = oxygen_pci_resume,
#endif
};

static int __init alsa_card_oxygen_init(void)
{
	return pci_register_driver(&oxygen_driver);
}

static void __exit alsa_card_oxygen_exit(void)
{
	pci_unregister_driver(&oxygen_driver);
}

module_init(alsa_card_oxygen_init)
module_exit(alsa_card_oxygen_exit)
