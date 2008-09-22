/*
 * C-Media CMI8788 driver for the MediaTek/TempoTec HiFier Fantasia
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

#include <linux/delay.h>
#include <linux/pci.h>
#include <sound/control.h>
#include <sound/core.h>
#include <sound/initval.h>
#include <sound/pcm.h>
#include <sound/tlv.h>
#include "oxygen.h"
#include "ak4396.h"

MODULE_AUTHOR("Clemens Ladisch <clemens@ladisch.de>");
MODULE_DESCRIPTION("TempoTec HiFier driver");
MODULE_LICENSE("GPL v2");

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;
static int enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_PNP;

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "card index");
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string");
module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "enable card");

static struct pci_device_id hifier_ids[] __devinitdata = {
	{ OXYGEN_PCI_SUBID(0x14c3, 0x1710) },
	{ OXYGEN_PCI_SUBID(0x14c3, 0x1711) },
	{ }
};
MODULE_DEVICE_TABLE(pci, hifier_ids);

struct hifier_data {
	u8 ak4396_ctl2;
};

static void ak4396_write(struct oxygen *chip, u8 reg, u8 value)
{
	oxygen_write_spi(chip, OXYGEN_SPI_TRIGGER  |
			 OXYGEN_SPI_DATA_LENGTH_2 |
			 OXYGEN_SPI_CLOCK_160 |
			 (0 << OXYGEN_SPI_CODEC_SHIFT) |
			 OXYGEN_SPI_CEN_LATCH_CLOCK_HI,
			 AK4396_WRITE | (reg << 8) | value);
}

static void update_ak4396_volume(struct oxygen *chip)
{
	ak4396_write(chip, AK4396_LCH_ATT, chip->dac_volume[0]);
	ak4396_write(chip, AK4396_RCH_ATT, chip->dac_volume[1]);
}

static void hifier_registers_init(struct oxygen *chip)
{
	struct hifier_data *data = chip->model_data;

	ak4396_write(chip, AK4396_CONTROL_1, AK4396_DIF_24_MSB | AK4396_RSTN);
	ak4396_write(chip, AK4396_CONTROL_2, data->ak4396_ctl2);
	ak4396_write(chip, AK4396_CONTROL_3, AK4396_PCM);
	update_ak4396_volume(chip);
}

static void hifier_init(struct oxygen *chip)
{
	struct hifier_data *data = chip->model_data;

	data->ak4396_ctl2 = AK4396_SMUTE | AK4396_DEM_OFF | AK4396_DFS_NORMAL;
	hifier_registers_init(chip);

	snd_component_add(chip->card, "AK4396");
	snd_component_add(chip->card, "CS5340");
}

static void hifier_cleanup(struct oxygen *chip)
{
}

static void set_ak4396_params(struct oxygen *chip,
			       struct snd_pcm_hw_params *params)
{
	struct hifier_data *data = chip->model_data;
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

	ak4396_write(chip, AK4396_CONTROL_1, AK4396_DIF_24_MSB);
	ak4396_write(chip, AK4396_CONTROL_2, value);
	ak4396_write(chip, AK4396_CONTROL_1, AK4396_DIF_24_MSB | AK4396_RSTN);
}

static void update_ak4396_mute(struct oxygen *chip)
{
	struct hifier_data *data = chip->model_data;
	u8 value;

	value = data->ak4396_ctl2 & ~AK4396_SMUTE;
	if (chip->dac_mute)
		value |= AK4396_SMUTE;
	data->ak4396_ctl2 = value;
	ak4396_write(chip, AK4396_CONTROL_2, value);
}

static void set_cs5340_params(struct oxygen *chip,
			      struct snd_pcm_hw_params *params)
{
}

static const DECLARE_TLV_DB_LINEAR(ak4396_db_scale, TLV_DB_GAIN_MUTE, 0);

static int hifier_control_filter(struct snd_kcontrol_new *template)
{
	if (!strcmp(template->name, "Stereo Upmixing"))
		return 1; /* stereo only - we don't need upmixing */
	return 0;
}

static const struct oxygen_model model_hifier = {
	.shortname = "C-Media CMI8787",
	.longname = "C-Media Oxygen HD Audio",
	.chip = "CMI8788",
	.owner = THIS_MODULE,
	.init = hifier_init,
	.control_filter = hifier_control_filter,
	.cleanup = hifier_cleanup,
	.resume = hifier_registers_init,
	.set_dac_params = set_ak4396_params,
	.set_adc_params = set_cs5340_params,
	.update_dac_volume = update_ak4396_volume,
	.update_dac_mute = update_ak4396_mute,
	.dac_tlv = ak4396_db_scale,
	.model_data_size = sizeof(struct hifier_data),
	.device_config = PLAYBACK_0_TO_I2S |
			 PLAYBACK_1_TO_SPDIF |
			 CAPTURE_0_FROM_I2S_1,
	.dac_channels = 2,
	.dac_volume_min = 0,
	.dac_volume_max = 255,
	.function_flags = OXYGEN_FUNCTION_SPI,
	.dac_i2s_format = OXYGEN_I2S_FORMAT_LJUST,
	.adc_i2s_format = OXYGEN_I2S_FORMAT_LJUST,
};

static int __devinit hifier_probe(struct pci_dev *pci,
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
	err = oxygen_pci_probe(pci, index[dev], id[dev], &model_hifier, 0);
	if (err >= 0)
		++dev;
	return err;
}

static struct pci_driver hifier_driver = {
	.name = "CMI8787HiFier",
	.id_table = hifier_ids,
	.probe = hifier_probe,
	.remove = __devexit_p(oxygen_pci_remove),
#ifdef CONFIG_PM
	.suspend = oxygen_pci_suspend,
	.resume = oxygen_pci_resume,
#endif
};

static int __init alsa_card_hifier_init(void)
{
	return pci_register_driver(&hifier_driver);
}

static void __exit alsa_card_hifier_exit(void)
{
	pci_unregister_driver(&hifier_driver);
}

module_init(alsa_card_hifier_init)
module_exit(alsa_card_hifier_exit)
