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

#include <linux/pci.h>
#include <sound/control.h>
#include <sound/core.h>
#include <sound/initval.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/tlv.h>
#include "oxygen.h"

MODULE_AUTHOR("Clemens Ladisch <clemens@ladisch.de>");
MODULE_DESCRIPTION("C-Media CMI8788 driver");
MODULE_LICENSE("GPL");
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

static struct pci_device_id oxygen_ids[] __devinitdata = {
	{ OXYGEN_PCI_SUBID(0x10b0, 0x0216) },
	{ OXYGEN_PCI_SUBID(0x10b0, 0x0218) },
	{ OXYGEN_PCI_SUBID(0x10b0, 0x0219) },
	{ OXYGEN_PCI_SUBID(0x13f6, 0x0001) },
	{ OXYGEN_PCI_SUBID(0x13f6, 0x0010) },
	{ OXYGEN_PCI_SUBID(0x13f6, 0x8788) },
	{ OXYGEN_PCI_SUBID(0x147a, 0xa017) },
	{ OXYGEN_PCI_SUBID(0x14c3, 0x1710) },
	{ OXYGEN_PCI_SUBID(0x14c3, 0x1711) },
	{ OXYGEN_PCI_SUBID(0x1a58, 0x0910) },
	{ OXYGEN_PCI_SUBID(0x415a, 0x5431), .driver_data = 1 },
	{ OXYGEN_PCI_SUBID(0x7284, 0x9761) },
	{ }
};
MODULE_DEVICE_TABLE(pci, oxygen_ids);

#define AK4396_WRITE	0x2000

/* register 0 */
#define AK4396_RSTN		0x01
#define AK4396_DIF_24_MSB	0x04
/* register 1 */
#define AK4396_SMUTE		0x01
#define AK4396_DEM_OFF		0x02
#define AK4396_DFS_MASK		0x18
#define AK4396_DFS_NORMAL	0x00
#define AK4396_DFS_DOUBLE	0x08
#define AK4396_DFS_QUAD		0x10

/* register 0 */
#define WM8785_OSR_SINGLE	0x000
#define WM8785_OSR_DOUBLE	0x008
#define WM8785_OSR_QUAD		0x010
#define WM8785_FORMAT_LJUST	0x020
#define WM8785_FORMAT_I2S	0x040
/* register 1 */
#define WM8785_WL_16		0x000
#define WM8785_WL_20		0x001
#define WM8785_WL_24		0x002
#define WM8785_WL_32		0x003

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
	oxygen_write_spi(chip, OXYGEN_SPI_TRIGGER |
			 OXYGEN_SPI_DATA_LENGTH_2 |
			 OXYGEN_SPI_CLOCK_160 |
			 (3 << OXYGEN_SPI_CODEC_SHIFT) |
			 OXYGEN_SPI_CEN_LATCH_CLOCK_LO,
			 (reg << 9) | value);
}

static void ak4396_init(struct oxygen *chip)
{
	unsigned int i;

	chip->ak4396_reg1 = AK4396_DEM_OFF | AK4396_DFS_NORMAL;
	for (i = 0; i < 4; ++i) {
		ak4396_write(chip, i, 0, AK4396_DIF_24_MSB | AK4396_RSTN);
		ak4396_write(chip, i, 1, chip->ak4396_reg1);
		ak4396_write(chip, i, 2, 0);
		ak4396_write(chip, i, 3, 0xff);
		ak4396_write(chip, i, 4, 0xff);
	}
	snd_component_add(chip->card, "AK4396");
}

static void ak5385_init(struct oxygen *chip)
{
	oxygen_set_bits16(chip, OXYGEN_GPIO_CONTROL, 0x0003);
	oxygen_clear_bits16(chip, OXYGEN_GPIO_DATA, 0x0003);
	snd_component_add(chip->card, "AK5385");
}

static void wm8785_init(struct oxygen *chip)
{
	wm8785_write(chip, 7, 0);
	wm8785_write(chip, 0, WM8785_FORMAT_LJUST | WM8785_OSR_SINGLE);
	wm8785_write(chip, 1, WM8785_WL_24);
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

static void generic_pcm_hardware_filter(unsigned int channel,
					struct snd_pcm_hardware *hardware)
{
	if (channel == PCM_A) {
		hardware->rates = SNDRV_PCM_RATE_44100 |
				  SNDRV_PCM_RATE_48000 |
				  SNDRV_PCM_RATE_96000 |
				  SNDRV_PCM_RATE_192000;
		hardware->rate_min = 44100;
	}
}

static void set_ak4396_params(struct oxygen *chip,
			      struct snd_pcm_hw_params *params)
{
	unsigned int i;
	u8 value;

	value = chip->ak4396_reg1 & ~AK4396_DFS_MASK;
	if (params_rate(params) <= 54000)
		value |= AK4396_DFS_NORMAL;
	else if (params_rate(params) < 120000)
		value |= AK4396_DFS_DOUBLE;
	else
		value |= AK4396_DFS_QUAD;
	chip->ak4396_reg1 = value;
	for (i = 0; i < 4; ++i) {
		ak4396_write(chip, i, 0, AK4396_DIF_24_MSB);
		ak4396_write(chip, i, 1, value);
		ak4396_write(chip, i, 0, AK4396_DIF_24_MSB | AK4396_RSTN);
	}
}

static void update_ak4396_volume(struct oxygen *chip)
{
	unsigned int i;

	for (i = 0; i < 4; ++i) {
		ak4396_write(chip, i, 3, chip->dac_volume[i * 2]);
		ak4396_write(chip, i, 4, chip->dac_volume[i * 2 + 1]);
	}
}

static void update_ak4396_mute(struct oxygen *chip)
{
	unsigned int i;
	u8 value;

	value = chip->ak4396_reg1 & ~AK4396_SMUTE;
	if (chip->dac_mute)
		value |= AK4396_SMUTE;
	for (i = 0; i < 4; ++i)
		ak4396_write(chip, i, 1, value);
}

static void set_wm8785_params(struct oxygen *chip,
			      struct snd_pcm_hw_params *params)
{
	unsigned int value;

	wm8785_write(chip, 7, 0);

	value = WM8785_FORMAT_LJUST;
	if (params_rate(params) == 96000)
		value |= WM8785_OSR_DOUBLE;
	else if (params_rate(params) == 192000)
		value |= WM8785_OSR_QUAD;
	else
		value |= WM8785_OSR_SINGLE;
	wm8785_write(chip, 0, value);

	if (snd_pcm_format_width(params_format(params)) <= 16)
		value = WM8785_WL_16;
	else
		value = WM8785_WL_24;
	wm8785_write(chip, 1, value);
}

static void set_ak5385_params(struct oxygen *chip,
			      struct snd_pcm_hw_params *params)
{
	unsigned int value;

	if (params_rate(params) <= 54000)
		value = 0;
	else if (params_rate(params) <= 108000)
		value = 1;
	else
		value = 2;
	oxygen_write16_masked(chip, OXYGEN_GPIO_DATA, value, 0x0003);
}

static const DECLARE_TLV_DB_LINEAR(ak4396_db_scale, TLV_DB_GAIN_MUTE, 0);

static int ak4396_control_filter(struct snd_kcontrol_new *template)
{
	if (!strcmp(template->name, "Master Playback Volume")) {
		template->access |= SNDRV_CTL_ELEM_ACCESS_TLV_READ;
		template->tlv.p = ak4396_db_scale;
	}
	return 0;
}

static const struct oxygen_model model_generic = {
	.shortname = "C-Media CMI8788",
	.longname = "C-Media Oxygen HD Audio",
	.chip = "CMI8788",
	.owner = THIS_MODULE,
	.init = generic_init,
	.control_filter = ak4396_control_filter,
	.cleanup = generic_cleanup,
	.pcm_hardware_filter = generic_pcm_hardware_filter,
	.set_dac_params = set_ak4396_params,
	.set_adc_params = set_wm8785_params,
	.update_dac_volume = update_ak4396_volume,
	.update_dac_mute = update_ak4396_mute,
	.used_channels = OXYGEN_CHANNEL_A |
			 OXYGEN_CHANNEL_C |
			 OXYGEN_CHANNEL_SPDIF |
			 OXYGEN_CHANNEL_MULTICH |
			 OXYGEN_CHANNEL_AC97,
	.function_flags = OXYGEN_FUNCTION_ENABLE_SPI_4_5,
	.dac_i2s_format = OXYGEN_I2S_FORMAT_LJUST,
	.adc_i2s_format = OXYGEN_I2S_FORMAT_LJUST,
};
static const struct oxygen_model model_meridian = {
	.shortname = "C-Media CMI8788",
	.longname = "C-Media Oxygen HD Audio",
	.chip = "CMI8788",
	.owner = THIS_MODULE,
	.init = meridian_init,
	.control_filter = ak4396_control_filter,
	.cleanup = generic_cleanup,
	.set_dac_params = set_ak4396_params,
	.set_adc_params = set_ak5385_params,
	.update_dac_volume = update_ak4396_volume,
	.update_dac_mute = update_ak4396_mute,
	.used_channels = OXYGEN_CHANNEL_B |
			 OXYGEN_CHANNEL_C |
			 OXYGEN_CHANNEL_SPDIF |
			 OXYGEN_CHANNEL_MULTICH |
			 OXYGEN_CHANNEL_AC97,
	.function_flags = OXYGEN_FUNCTION_ENABLE_SPI_4_5,
	.dac_i2s_format = OXYGEN_I2S_FORMAT_LJUST,
	.adc_i2s_format = OXYGEN_I2S_FORMAT_LJUST,
};

static int __devinit generic_oxygen_probe(struct pci_dev *pci,
					  const struct pci_device_id *pci_id)
{
	static int dev;
	const struct oxygen_model *model;
	int err;

	if (dev >= SNDRV_CARDS)
		return -ENODEV;
	if (!enable[dev]) {
		++dev;
		return -ENOENT;
	}
	model = pci_id->driver_data ? &model_meridian : &model_generic;
	err = oxygen_pci_probe(pci, index[dev], id[dev], 1, model);
	if (err >= 0)
		++dev;
	return err;
}

static struct pci_driver oxygen_driver = {
	.name = "CMI8788",
	.id_table = oxygen_ids,
	.probe = generic_oxygen_probe,
	.remove = __devexit_p(oxygen_pci_remove),
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
