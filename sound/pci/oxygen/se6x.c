// SPDX-License-Identifier: GPL-2.0-only
/*
 * C-Media CMI8787 driver for the Studio Evolution SE6X
 *
 * Copyright (c) Clemens Ladisch <clemens@ladisch.de>
 */

/*
 * CMI8787:
 *
 *   SPI    -> microcontroller (not actually used)
 *   GPIO 0 -> do.
 *   GPIO 2 -> do.
 *
 *   DAC0   -> both PCM1792A (L+R, each in mono mode)
 *   ADC1  <-  1st PCM1804
 *   ADC2  <-  2nd PCM1804
 *   ADC3  <-  3rd PCM1804
 */

#include <linux/pci.h>
#include <linux/module.h>
#include <sound/core.h>
#include <sound/control.h>
#include <sound/initval.h>
#include <sound/pcm.h>
#include "oxygen.h"

MODULE_AUTHOR("Clemens Ladisch <clemens@ladisch.de>");
MODULE_DESCRIPTION("Studio Evolution SE6X driver");
MODULE_LICENSE("GPL v2");

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;
static bool enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_PNP;

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "card index");
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string");
module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "enable card");

static const struct pci_device_id se6x_ids[] = {
	{ OXYGEN_PCI_SUBID(0x13f6, 0x8788) },
	{ }
};
MODULE_DEVICE_TABLE(pci, se6x_ids);

static void se6x_init(struct oxygen *chip)
{
	oxygen_set_bits16(chip, OXYGEN_GPIO_CONTROL, 0x005);

	snd_component_add(chip->card, "PCM1792A");
	snd_component_add(chip->card, "PCM1804");
}

static int se6x_control_filter(struct snd_kcontrol_new *template)
{
	/* no DAC volume/mute */
	if (!strncmp(template->name, "Master Playback ", 16))
		return 1;
	return 0;
}

static void se6x_cleanup(struct oxygen *chip)
{
}

static void set_pcm1792a_params(struct oxygen *chip,
				struct snd_pcm_hw_params *params)
{
	/* nothing to do (the microcontroller monitors DAC_LRCK) */
}

static void set_pcm1804_params(struct oxygen *chip,
			       struct snd_pcm_hw_params *params)
{
}

static unsigned int se6x_adjust_dac_routing(struct oxygen *chip,
					    unsigned int play_routing)
{
	/* route the same stereo pair to DAC0 and DAC1 */
	return ( play_routing       & OXYGEN_PLAY_DAC0_SOURCE_MASK) |
	       ((play_routing << 2) & OXYGEN_PLAY_DAC1_SOURCE_MASK);
}

static const struct oxygen_model model_se6x = {
	.shortname = "Studio Evolution SE6X",
	.longname = "C-Media Oxygen HD Audio",
	.chip = "CMI8787",
	.init = se6x_init,
	.control_filter = se6x_control_filter,
	.cleanup = se6x_cleanup,
	.set_dac_params = set_pcm1792a_params,
	.set_adc_params = set_pcm1804_params,
	.adjust_dac_routing = se6x_adjust_dac_routing,
	.device_config = PLAYBACK_0_TO_I2S |
			 CAPTURE_0_FROM_I2S_1 |
			 CAPTURE_2_FROM_I2S_2 |
			 CAPTURE_3_FROM_I2S_3,
	.dac_channels_pcm = 2,
	.function_flags = OXYGEN_FUNCTION_SPI,
	.dac_mclks = OXYGEN_MCLKS(256, 128, 128),
	.adc_mclks = OXYGEN_MCLKS(256, 256, 128),
	.dac_i2s_format = OXYGEN_I2S_FORMAT_LJUST,
	.adc_i2s_format = OXYGEN_I2S_FORMAT_I2S,
};

static int se6x_get_model(struct oxygen *chip,
			  const struct pci_device_id *pci_id)
{
	chip->model = model_se6x;
	return 0;
}

static int se6x_probe(struct pci_dev *pci, const struct pci_device_id *pci_id)
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
			       se6x_ids, se6x_get_model);
	if (err >= 0)
		++dev;
	return err;
}

static struct pci_driver se6x_driver = {
	.name = KBUILD_MODNAME,
	.id_table = se6x_ids,
	.probe = se6x_probe,
#ifdef CONFIG_PM_SLEEP
	.driver = {
		.pm = &oxygen_pci_pm,
	},
#endif
	.shutdown = oxygen_pci_shutdown,
};

module_pci_driver(se6x_driver);
