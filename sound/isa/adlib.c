/*
 * AdLib FM card driver.
 */

#include <sound/driver.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <sound/core.h>
#include <sound/initval.h>
#include <sound/opl3.h>

#define CRD_NAME "AdLib FM"
#define DRV_NAME "snd_adlib"

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

static struct platform_device *devices[SNDRV_CARDS];

static void snd_adlib_free(struct snd_card *card)
{
	release_and_free_resource(card->private_data);
}

static int __devinit snd_adlib_probe(struct platform_device *device)
{
	struct snd_card *card;
	struct snd_opl3 *opl3;

	int error;
	int i = device->id;

	if (port[i] == SNDRV_AUTO_PORT) {
		snd_printk(KERN_ERR DRV_NAME ": please specify port\n");
		error = -EINVAL;
		goto out0;
	}

	card = snd_card_new(index[i], id[i], THIS_MODULE, 0);
	if (!card) {
		snd_printk(KERN_ERR DRV_NAME ": could not create card\n");
		error = -EINVAL;
		goto out0;
	}

	card->private_data = request_region(port[i], 4, CRD_NAME);
	if (!card->private_data) {
		snd_printk(KERN_ERR DRV_NAME ": could not grab ports\n");
		error = -EBUSY;
		goto out1;
	}
	card->private_free = snd_adlib_free;

	error = snd_opl3_create(card, port[i], port[i] + 2, OPL3_HW_AUTO, 1, &opl3);
	if (error < 0) {
		snd_printk(KERN_ERR DRV_NAME ": could not create OPL\n");
		goto out1;
	}

	error = snd_opl3_hwdep_new(opl3, 0, 0, NULL);
	if (error < 0) {
		snd_printk(KERN_ERR DRV_NAME ": could not create FM\n");
		goto out1;
	}

	strcpy(card->driver, DRV_NAME);
	strcpy(card->shortname, CRD_NAME);
	sprintf(card->longname, CRD_NAME " at %#lx", port[i]);

	snd_card_set_dev(card, &device->dev);

	error = snd_card_register(card);
	if (error < 0) {
		snd_printk(KERN_ERR DRV_NAME ": could not register card\n");
		goto out1;
	}

	platform_set_drvdata(device, card);
	return 0;

out1:	snd_card_free(card);
 out0:	error = -EINVAL; /* FIXME: should be the original error code */
	return error;
}

static int __devexit snd_adlib_remove(struct platform_device *device)
{
	snd_card_free(platform_get_drvdata(device));
	platform_set_drvdata(device, NULL);
	return 0;
}

static struct platform_driver snd_adlib_driver = {
	.probe		= snd_adlib_probe,
	.remove		= __devexit_p(snd_adlib_remove),

	.driver		= {
		.name	= DRV_NAME
	}
};

static int __init alsa_card_adlib_init(void)
{
	int i, cards;

	if (platform_driver_register(&snd_adlib_driver) < 0) {
		snd_printk(KERN_ERR DRV_NAME ": could not register driver\n");
		return -ENODEV;
	}

	for (cards = 0, i = 0; i < SNDRV_CARDS; i++) {
		struct platform_device *device;

		if (!enable[i])
			continue;

		device = platform_device_register_simple(DRV_NAME, i, NULL, 0);
		if (IS_ERR(device))
			continue;

		devices[i] = device;
		cards++;
	}

	if (!cards) {
#ifdef MODULE
		printk(KERN_ERR CRD_NAME " soundcard not found or device busy\n");
#endif
		platform_driver_unregister(&snd_adlib_driver);
		return -ENODEV;
	}
	return 0;
}

static void __exit alsa_card_adlib_exit(void)
{
	int i;

	for (i = 0; i < SNDRV_CARDS; i++)
		platform_device_unregister(devices[i]);
	platform_driver_unregister(&snd_adlib_driver);
}

module_init(alsa_card_adlib_init);
module_exit(alsa_card_adlib_exit);
