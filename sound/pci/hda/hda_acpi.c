// SPDX-License-Identifier: GPL-2.0-only
/*
 * ALSA driver for ACPI-based HDA Controllers.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/acpi.h>

#include <sound/hda_codec.h>

#include "hda_controller.h"

struct hda_acpi {
	struct azx azx;
	struct snd_card *card;
	struct platform_device *pdev;
	void __iomem *regs;
	struct work_struct probe_work;
	const struct hda_data *data;
};

/**
 * struct hda_data - Optional device-specific data
 * @short_name: Used for the ALSA card name; defaults to KBUILD_MODNAME
 * @long_name:  Used for longer description; defaults to short_name
 * @flags:      Passed to &azx->driver_caps
 *
 * A pointer to a record of this type may be stored in the
 * &acpi_device_id->driver_data field of an ACPI match table entry in order to
 * customize the naming and behavior of a particular device. All fields are
 * optional and sensible defaults will be selected in their absence.
 */
struct hda_data {
	const char *short_name;
	const char *long_name;
	unsigned long flags;
};

static int hda_acpi_dev_disconnect(struct snd_device *device)
{
	struct azx *chip = device->device_data;

	chip->bus.shutdown = 1;
	return 0;
}

static int hda_acpi_dev_free(struct snd_device *device)
{
	struct azx *azx = device->device_data;
	struct hda_acpi *hda = container_of(azx, struct hda_acpi, azx);

	cancel_work_sync(&hda->probe_work);
	if (azx_bus(azx)->chip_init) {
		azx_stop_all_streams(azx);
		azx_stop_chip(azx);
	}

	azx_free_stream_pages(azx);
	azx_free_streams(azx);
	snd_hdac_bus_exit(azx_bus(azx));

	return 0;
}

static int hda_acpi_init(struct hda_acpi *hda)
{
	struct hdac_bus *bus = azx_bus(&hda->azx);
	struct snd_card *card = hda->azx.card;
	struct device *dev = &hda->pdev->dev;
	struct azx *azx = &hda->azx;
	struct resource *res;
	unsigned short gcap;
	const char *sname, *lname;
	int err, irq;

	/* The base address for the HDA registers and the interrupt are wrapped
	 * in an ACPI _CRS object which can be parsed by platform_get_irq() and
	 * devm_platform_get_and_ioremap_resource()
	 */

	irq = platform_get_irq(hda->pdev, 0);
	if (irq < 0)
		return irq;

	hda->regs = devm_platform_get_and_ioremap_resource(hda->pdev, 0, &res);
	if (IS_ERR(hda->regs))
		return PTR_ERR(hda->regs);

	bus->remap_addr = hda->regs;
	bus->addr = res->start;

	err = devm_request_irq(dev, irq, azx_interrupt,
			       IRQF_SHARED, KBUILD_MODNAME, azx);
	if (err) {
		dev_err(dev, "unable to request IRQ %d, disabling device\n",
			irq);
		return err;
	}
	bus->irq = irq;
	bus->dma_stop_delay = 100;
	card->sync_irq = bus->irq;

	gcap = azx_readw(azx, GCAP);
	dev_dbg(dev, "chipset global capabilities = 0x%x\n", gcap);

	azx->align_buffer_size = 1;

	azx->capture_streams = (gcap >> 8) & 0x0f;
	azx->playback_streams = (gcap >> 12) & 0x0f;

	azx->capture_index_offset = 0;
	azx->playback_index_offset = azx->capture_streams;
	azx->num_streams = azx->playback_streams + azx->capture_streams;

	err = azx_init_streams(azx);
	if (err < 0) {
		dev_err(dev, "failed to initialize streams: %d\n", err);
		return err;
	}

	err = azx_alloc_stream_pages(azx);
	if (err < 0) {
		dev_err(dev, "failed to allocate stream pages: %d\n", err);
		return err;
	}

	azx_init_chip(azx, 1);

	if (!bus->codec_mask) {
		dev_err(dev, "no codecs found!\n");
		return -ENODEV;
	}

	strscpy(card->driver, "hda-acpi");

	sname = hda->data->short_name ? hda->data->short_name : KBUILD_MODNAME;

	if (strlen(sname) > sizeof(card->shortname))
		dev_info(dev, "truncating shortname for card %s\n", sname);
	strscpy(card->shortname, sname);

	lname = hda->data->long_name ? hda->data->long_name : sname;

	snprintf(card->longname, sizeof(card->longname),
		 "%s at 0x%lx irq %i", lname, bus->addr, bus->irq);

	return 0;
}

static void hda_acpi_probe_work(struct work_struct *work)
{
	struct hda_acpi *hda = container_of(work, struct hda_acpi, probe_work);
	struct azx *chip = &hda->azx;
	int err;

	err = hda_acpi_init(hda);
	if (err < 0)
		return;

	err = azx_probe_codecs(chip, 8);
	if (err < 0)
		return;

	err = azx_codec_configure(chip);
	if (err < 0)
		return;

	err = snd_card_register(chip->card);
	if (err < 0)
		return;

	chip->running = 1;
}

static int hda_acpi_create(struct hda_acpi *hda)
{
	static const struct snd_device_ops ops = {
		.dev_disconnect = hda_acpi_dev_disconnect,
		.dev_free = hda_acpi_dev_free,
	};
	static const struct hda_controller_ops null_ops;
	struct azx *azx = &hda->azx;
	int err;

	mutex_init(&azx->open_mutex);
	azx->card = hda->card;
	INIT_LIST_HEAD(&azx->pcm_list);

	azx->ops = &null_ops;
	azx->driver_caps = hda->data->flags;
	azx->driver_type = hda->data->flags & 0xff;
	azx->codec_probe_mask = -1;

	err = azx_bus_init(azx, NULL);
	if (err < 0)
		return err;

	err = snd_device_new(hda->card, SNDRV_DEV_LOWLEVEL, &hda->azx, &ops);
	if (err < 0) {
		dev_err(&hda->pdev->dev, "Error creating device\n");
		return err;
	}

	return 0;
}

static int hda_acpi_probe(struct platform_device *pdev)
{
	struct hda_acpi *hda;
	int err;

	hda = devm_kzalloc(&pdev->dev, sizeof(*hda), GFP_KERNEL);
	if (!hda)
		return -ENOMEM;

	hda->pdev = pdev;
	hda->data = acpi_device_get_match_data(&pdev->dev);

	/* Fall back to defaults if the table didn't have a *struct hda_data */
	if (!hda->data)
		hda->data = devm_kzalloc(&pdev->dev, sizeof(*hda->data),
					 GFP_KERNEL);
	if (!hda->data)
		return -ENOMEM;

	err = snd_card_new(&pdev->dev, SNDRV_DEFAULT_IDX1, SNDRV_DEFAULT_STR1,
			   THIS_MODULE, 0, &hda->card);
	if (err < 0) {
		dev_err(&pdev->dev, "Error creating card!\n");
		return err;
	}

	INIT_WORK(&hda->probe_work, hda_acpi_probe_work);

	err = hda_acpi_create(hda);
	if (err < 0)
		goto out_free;
	hda->card->private_data = &hda->azx;

	dev_set_drvdata(&pdev->dev, hda->card);

	schedule_work(&hda->probe_work);

	return 0;

out_free:
	snd_card_free(hda->card);
	return err;
}

static void hda_acpi_remove(struct platform_device *pdev)
{
	snd_card_free(dev_get_drvdata(&pdev->dev));
}

static void hda_acpi_shutdown(struct platform_device *pdev)
{
	struct snd_card *card = dev_get_drvdata(&pdev->dev);
	struct azx *chip;

	if (!card)
		return;
	chip = card->private_data;
	if (chip && chip->running)
		azx_stop_chip(chip);
}

static int hda_acpi_suspend(struct device *dev)
{
	struct snd_card *card = dev_get_drvdata(dev);
	int rc;

	rc = pm_runtime_force_suspend(dev);
	if (rc < 0)
		return rc;
	snd_power_change_state(card, SNDRV_CTL_POWER_D3hot);

	return 0;
}

static int hda_acpi_resume(struct device *dev)
{
	struct snd_card *card = dev_get_drvdata(dev);
	int rc;

	rc = pm_runtime_force_resume(dev);
	if (rc < 0)
		return rc;
	snd_power_change_state(card, SNDRV_CTL_POWER_D0);

	return 0;
}

static const struct dev_pm_ops hda_acpi_pm = {
	SYSTEM_SLEEP_PM_OPS(hda_acpi_suspend, hda_acpi_resume)
};

static const struct hda_data nvidia_hda_data = {
	.short_name = "NVIDIA",
	.long_name = "NVIDIA HDA Controller",
	.flags = AZX_DCAPS_CORBRP_SELF_CLEAR,
};

static const struct acpi_device_id hda_acpi_match[] = {
	{ .id = "NVDA2014", .driver_data = (uintptr_t) &nvidia_hda_data },
	{ .id = "NVDA2015", .driver_data = (uintptr_t) &nvidia_hda_data },
	{},
};
MODULE_DEVICE_TABLE(acpi, hda_acpi_match);

static struct platform_driver hda_acpi_platform_driver = {
	.driver = {
		.name = KBUILD_MODNAME,
		.pm = &hda_acpi_pm,
		.acpi_match_table = hda_acpi_match,
	},
	.probe = hda_acpi_probe,
	.remove = hda_acpi_remove,
	.shutdown = hda_acpi_shutdown,
};
module_platform_driver(hda_acpi_platform_driver);

MODULE_DESCRIPTION("Driver for ACPI-based HDA Controllers");
MODULE_LICENSE("GPL");
