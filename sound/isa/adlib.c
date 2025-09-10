// SPDX-License-Identifier: GPL-2.0-only
/*
 * AdLib FM card driver.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/isa.h>
#include <sound/core.h>
#include <sound/initval.h>
#include <sound/opl3.h>

#define CRD_NAME "AdLib FM"
#define DEV_NAME "adlib"

MODULE_DESCRIPTION(CRD_NAME);
MODULE_AUTHOR("Rene Herman");
MODULE_LICENSE("GPL");

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;
static bool enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE;
static long port[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for " CRD_NAME " soundcard.");
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string for " CRD_NAME " soundcard.");
module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "Enable " CRD_NAME " soundcard.");
module_param_hw_array(port, long, ioport, NULL, 0444);
MODULE_PARM_DESC(port, "Port # for " CRD_NAME " driver.");

static int snd_adlib_match(struct device *dev, unsigned int n)
{
	if (!enable[n])
		return 0;

	if (port[n] == SNDRV_AUTO_PORT) {
		dev_err(dev, "please specify port\n");
		return 0;
	}
	return 1;
}

static int snd_adlib_probe(struct device *dev, unsigned int n)
{
	struct snd_card *card;
	struct snd_opl3 *opl3;
	int error;

	error = snd_devm_card_new(dev, index[n], id[n], THIS_MODULE, 0, &card);
	if (error < 0) {
		dev_err(dev, "could not create card\n");
		return error;
	}

	card->private_data = devm_request_region(dev, port[n], 4, CRD_NAME);
	if (!card->private_data) {
		dev_err(dev, "could not grab ports\n");
		return -EBUSY;
	}

	strscpy(card->driver, DEV_NAME);
	strscpy(card->shortname, CRD_NAME);
	sprintf(card->longname, CRD_NAME " at %#lx", port[n]);

	error = snd_opl3_create(card, port[n], port[n] + 2, OPL3_HW_AUTO, 1, &opl3);
	if (error < 0) {
		dev_err(dev, "could not create OPL\n");
		return error;
	}

	error = snd_opl3_hwdep_new(opl3, 0, 0, NULL);
	if (error < 0) {
		dev_err(dev, "could not create FM\n");
		return error;
	}

	error = snd_card_register(card);
	if (error < 0) {
		dev_err(dev, "could not register card\n");
		return error;
	}

	dev_set_drvdata(dev, card);
	return 0;
}

static struct isa_driver snd_adlib_driver = {
	.match		= snd_adlib_match,
	.probe		= snd_adlib_probe,

	.driver		= {
		.name	= DEV_NAME
	}
};

module_isa_driver(snd_adlib_driver, SNDRV_CARDS);
