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
static int enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE;
static long port[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for " CRD_NAME " soundcard.");
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string for " CRD_NAME " soundcard.");
module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "Enable " CRD_NAME " soundcard.");
module_param_array(port, long, NULL, 0444);
MODULE_PARM_DESC(port, "Port # for " CRD_NAME " driver.");

static int __devinit snd_adlib_match(struct device *dev, unsigned int n)
{
	if (!enable[n])
		return 0;

	if (port[n] == SNDRV_AUTO_PORT) {
		snd_printk(KERN_ERR "%s: please specify port\n", dev->bus_id);
		return 0;
	}
	return 1;
}

static void snd_adlib_free(struct snd_card *card)
{
	release_and_free_resource(card->private_data);
}

static int __devinit snd_adlib_probe(struct device *dev, unsigned int n)
{
	struct snd_card *card;
	struct snd_opl3 *opl3;
	int error;

	card = snd_card_new(index[n], id[n], THIS_MODULE, 0);
	if (!card) {
		snd_printk(KERN_ERR "%s: could not create card\n", dev->bus_id);
		return -EINVAL;
	}

	card->private_data = request_region(port[n], 4, CRD_NAME);
	if (!card->private_data) {
		snd_printk(KERN_ERR "%s: could not grab ports\n", dev->bus_id);
		error = -EBUSY;
		goto out;
	}
	card->private_free = snd_adlib_free;

	strcpy(card->driver, DEV_NAME);
	strcpy(card->shortname, CRD_NAME);
	sprintf(card->longname, CRD_NAME " at %#lx", port[n]);

	error = snd_opl3_create(card, port[n], port[n] + 2, OPL3_HW_AUTO, 1, &opl3);
	if (error < 0) {
		snd_printk(KERN_ERR "%s: could not create OPL\n", dev->bus_id);
		goto out;
	}

	error = snd_opl3_hwdep_new(opl3, 0, 0, NULL);
	if (error < 0) {
		snd_printk(KERN_ERR "%s: could not create FM\n", dev->bus_id);
		goto out;
	}

	snd_card_set_dev(card, dev);

	error = snd_card_register(card);
	if (error < 0) {
		snd_printk(KERN_ERR "%s: could not register card\n", dev->bus_id);
		goto out;
	}

	dev_set_drvdata(dev, card);
	return 0;

out:	snd_card_free(card);
	return error;
}

static int __devexit snd_adlib_remove(struct device *dev, unsigned int n)
{
	snd_card_free(dev_get_drvdata(dev));
	dev_set_drvdata(dev, NULL);
	return 0;
}

static struct isa_driver snd_adlib_driver = {
	.match		= snd_adlib_match,
	.probe		= snd_adlib_probe,
	.remove		= __devexit_p(snd_adlib_remove),

	.driver		= {
		.name	= DEV_NAME
	}
};

static int __init alsa_card_adlib_init(void)
{
	return isa_register_driver(&snd_adlib_driver, SNDRV_CARDS);
}

static void __exit alsa_card_adlib_exit(void)
{
	isa_unregister_driver(&snd_adlib_driver);
}

module_init(alsa_card_adlib_init);
module_exit(alsa_card_adlib_exit);
