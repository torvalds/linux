/*
 * C-Media CMI8788 driver for C-Media's reference design and for the X-Meridian
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
 * SPI 0 -> 1st AK4396 (front)
 * SPI 1 -> 2nd AK4396 (surround)
 * SPI 2 -> 3rd AK4396 (center/LFE)
 * SPI 3 -> WM8785
 * SPI 4 -> 4th AK4396 (back)
 *
 * GPIO 0 -> DFS0 of AK5385
 * GPIO 1 -> DFS1 of AK5385
 */

#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <sound/ac97_codec.h>
#include <sound/control.h>
#include <sound/core.h>
#include <sound/initval.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/tlv.h>
#include "oxygen.h"
#include "ak4396.h"
#include "wm8785.h"

MODULE_AUTHOR("Clemens Ladisch <clemens@ladisch.de>");
MODULE_DESCRIPTION("C-Media CMI8788 driver");
MODULE_LICENSE("GPL v2");
MODULE_SUPPORTED_DEVICE("{{C-Media,CMI8788}}");

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
	MODEL_CMEDIA_REF,	/* C-Media's reference design */
	MODEL_MERIDIAN,		/* AuzenTech X-Meridian */
	MODEL_HALO,		/* HT-Omega Claro halo */
};

static struct pci_device_id oxygen_ids[] __devinitdata = {
	{ OXYGEN_PCI_SUBID(0x10b0, 0x0216), .driver_data = MODEL_CMEDIA_REF },
	{ OXYGEN_PCI_SUBID(0x10b0, 0x0218), .driver_data = MODEL_CMEDIA_REF },
	{ OXYGEN_PCI_SUBID(0x10b0, 0x0219), .driver_data = MODEL_CMEDIA_REF },
	{ OXYGEN_PCI_SUBID(0x13f6, 0x0001), .driver_data = MODEL_CMEDIA_REF },
	{ OXYGEN_PCI_SUBID(0x13f6, 0x0010), .driver_data = MODEL_CMEDIA_REF },
	{ OXYGEN_PCI_SUBID(0x13f6, 0x8788), .driver_data = MODEL_CMEDIA_REF },
	{ OXYGEN_PCI_SUBID(0x147a, 0xa017), .driver_data = MODEL_CMEDIA_REF },
	{ OXYGEN_PCI_SUBID(0x1a58, 0x0910), .driver_data = MODEL_CMEDIA_REF },
	{ OXYGEN_PCI_SUBID(0x415a, 0x5431), .driver_data = MODEL_MERIDIAN },
	{ OXYGEN_PCI_SUBID(0x7284, 0x9761), .driver_data = MODEL_CMEDIA_REF },
	{ OXYGEN_PCI_SUBID(0x7284, 0x9781), .driver_data = MODEL_HALO },
	{ }
};
MODULE_DEVICE_TABLE(pci, oxygen_ids);


#define GPIO_AK5385_DFS_MASK	0x0003
#define GPIO_AK5385_DFS_NORMAL	0x0000
#define GPIO_AK5385_DFS_DOUBLE	0x0001
#define GPIO_AK5385_DFS_QUAD	0x0002

struct generic_data {
	u8 ak4396_ctl2;
	u16 saved_wm8785_registers[2];
};

static void ak4396_write(struct oxygen *chip, unsigned int codec,
			 u8 reg, u8 value)
{
	/* maps ALSA channel pair number to SPI output */
	static const u8 codec_spi_map[4] = {
		0, 1, 2, 4
	};
	oxygen_write_spi(chip, OXYGEN_SPI_TRIGGER |
			 OXYGEN_SPI_DATA_LENGTH_2 |
			 OXYGEN_SPI_CLOCK_160 |
			 (codec_spi_map[codec] << OXYGEN_SPI_CODEC_SHIFT) |
			 OXYGEN_SPI_CEN_LATCH_CLOCK_HI,
			 AK4396_WRITE | (reg << 8) | value);
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
	if (reg < ARRAY_SIZE(data->saved_wm8785_registers))
		data->saved_wm8785_registers[reg] = value;
}

static void update_ak4396_volume(struct oxygen *chip)
{
	unsigned int i;

	for (i = 0; i < 4; ++i) {
		ak4396_write(chip, i,
			     AK4396_LCH_ATT, chip->dac_volume[i * 2]);
		ak4396_write(chip, i,
			     AK4396_RCH_ATT, chip->dac_volume[i * 2 + 1]);
	}
}

static void ak4396_registers_init(struct oxygen *chip)
{
	struct generic_data *data = chip->model_data;
	unsigned int i;

	for (i = 0; i < 4; ++i) {
		ak4396_write(chip, i,
			     AK4396_CONTROL_1, AK4396_DIF_24_MSB | AK4396_RSTN);
		ak4396_write(chip, i,
			     AK4396_CONTROL_2, data->ak4396_ctl2);
		ak4396_write(chip, i,
			     AK4396_CONTROL_3, AK4396_PCM);
	}
	update_ak4396_volume(chip);
}

static void ak4396_init(struct oxygen *chip)
{
	struct generic_data *data = chip->model_data;

	data->ak4396_ctl2 = AK4396_SMUTE | AK4396_DEM_OFF | AK4396_DFS_NORMAL;
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
	wm8785_write(chip, WM8785_R0, data->saved_wm8785_registers[0]);
	wm8785_write(chip, WM8785_R1, data->saved_wm8785_registers[1]);
}

static void wm8785_init(struct oxygen *chip)
{
	struct generic_data *data = chip->model_data;

	data->saved_wm8785_registers[0] = WM8785_MCR_SLAVE |
		WM8785_OSR_SINGLE | WM8785_FORMAT_LJUST;
	data->saved_wm8785_registers[1] = WM8785_WL_24;
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

static void generic_cleanup(struct oxygen *chip)
{
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

static void set_ak4396_params(struct oxygen *chip,
			      struct snd_pcm_hw_params *params)
{
	struct generic_data *data = chip->model_data;
	unsigned int i;
	u8 value;

	value = data->ak4396_ctl2 & ~AK4396_DFS_MASK;
	if (params_rate(params) <= 54000)
		value |= AK4396_DFS_NORMAL;
	else if (params_rate(params) <= 108000)
		value |= AK4396_DFS_DOUBLE;
	else
		value |= AK4396_DFS_QUAD;
	data->ak4396_ctl2 = value;

	msleep(1); /* wait for the new MCLK to become stable */

	for (i = 0; i < 4; ++i) {
		ak4396_write(chip, i,
			     AK4396_CONTROL_1, AK4396_DIF_24_MSB);
		ak4396_write(chip, i,
			     AK4396_CONTROL_2, value);
		ak4396_write(chip, i,
			     AK4396_CONTROL_1, AK4396_DIF_24_MSB | AK4396_RSTN);
	}
}

static void update_ak4396_mute(struct oxygen *chip)
{
	struct generic_data *data = chip->model_data;
	unsigned int i;
	u8 value;

	value = data->ak4396_ctl2 & ~AK4396_SMUTE;
	if (chip->dac_mute)
		value |= AK4396_SMUTE;
	data->ak4396_ctl2 = value;
	for (i = 0; i < 4; ++i)
		ak4396_write(chip, i, AK4396_CONTROL_2, value);
}

static void set_wm8785_params(struct oxygen *chip,
			      struct snd_pcm_hw_params *params)
{
	unsigned int value;

	wm8785_write(chip, WM8785_R7, 0);

	value = WM8785_MCR_SLAVE | WM8785_FORMAT_LJUST;
	if (params_rate(params) <= 48000)
		value |= WM8785_OSR_SINGLE;
	else if (params_rate(params) <= 96000)
		value |= WM8785_OSR_DOUBLE;
	else
		value |= WM8785_OSR_QUAD;
	wm8785_write(chip, WM8785_R0, value);

	if (snd_pcm_format_width(params_format(params)) <= 16)
		value = WM8785_WL_16;
	else
		value = WM8785_WL_24;
	wm8785_write(chip, WM8785_R1, value);
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

static const DECLARE_TLV_DB_LINEAR(ak4396_db_scale, TLV_DB_GAIN_MUTE, 0);

static int generic_probe(struct oxygen *chip, unsigned long driver_data)
{
	if (driver_data == MODEL_MERIDIAN) {
		chip->model.init = meridian_init;
		chip->model.resume = meridian_resume;
		chip->model.set_adc_params = set_ak5385_params;
		chip->model.device_config = PLAYBACK_0_TO_I2S |
					    PLAYBACK_1_TO_SPDIF |
					    CAPTURE_0_FROM_I2S_2 |
					    CAPTURE_1_FROM_SPDIF;
	}
	if (driver_data == MODEL_MERIDIAN || driver_data == MODEL_HALO) {
		chip->model.misc_flags = OXYGEN_MISC_MIDI;
		chip->model.device_config |= MIDI_OUTPUT | MIDI_INPUT;
	}
	return 0;
}

static const struct oxygen_model model_generic = {
	.shortname = "C-Media CMI8788",
	.longname = "C-Media Oxygen HD Audio",
	.chip = "CMI8788",
	.owner = THIS_MODULE,
	.probe = generic_probe,
	.init = generic_init,
	.cleanup = generic_cleanup,
	.resume = generic_resume,
	.set_dac_params = set_ak4396_params,
	.set_adc_params = set_wm8785_params,
	.update_dac_volume = update_ak4396_volume,
	.update_dac_mute = update_ak4396_mute,
	.dac_tlv = ak4396_db_scale,
	.model_data_size = sizeof(struct generic_data),
	.device_config = PLAYBACK_0_TO_I2S |
			 PLAYBACK_1_TO_SPDIF |
			 PLAYBACK_2_TO_AC97_1 |
			 CAPTURE_0_FROM_I2S_1 |
			 CAPTURE_1_FROM_SPDIF |
			 CAPTURE_2_FROM_AC97_1,
	.dac_channels = 8,
	.dac_volume_min = 0,
	.dac_volume_max = 255,
	.function_flags = OXYGEN_FUNCTION_SPI |
			  OXYGEN_FUNCTION_ENABLE_SPI_4_5,
	.dac_i2s_format = OXYGEN_I2S_FORMAT_LJUST,
	.adc_i2s_format = OXYGEN_I2S_FORMAT_LJUST,
};

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
	err = oxygen_pci_probe(pci, index[dev], id[dev],
			       &model_generic, pci_id->driver_data);
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
