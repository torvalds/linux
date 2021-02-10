// SPDX-License-Identifier: GPL-2.0-only
/*
 *
 * Implementation of primary ALSA driver code base for NVIDIA Tegra HDA.
 */

#include <linux/clk.h>
#include <linux/clocksource.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/mutex.h>
#include <linux/of_device.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/string.h>
#include <linux/pm_runtime.h>

#include <sound/core.h>
#include <sound/initval.h>

#include <sound/hda_codec.h>
#include "hda_controller.h"

/* Defines for Nvidia Tegra HDA support */
#define HDA_BAR0           0x8000

#define HDA_CFG_CMD        0x1004
#define HDA_CFG_BAR0       0x1010

#define HDA_ENABLE_IO_SPACE       (1 << 0)
#define HDA_ENABLE_MEM_SPACE      (1 << 1)
#define HDA_ENABLE_BUS_MASTER     (1 << 2)
#define HDA_ENABLE_SERR           (1 << 8)
#define HDA_DISABLE_INTR          (1 << 10)
#define HDA_BAR0_INIT_PROGRAM     0xFFFFFFFF
#define HDA_BAR0_FINAL_PROGRAM    (1 << 14)

/* IPFS */
#define HDA_IPFS_CONFIG           0x180
#define HDA_IPFS_EN_FPCI          0x1

#define HDA_IPFS_FPCI_BAR0        0x80
#define HDA_FPCI_BAR0_START       0x40

#define HDA_IPFS_INTR_MASK        0x188
#define HDA_IPFS_EN_INTR          (1 << 16)

/* FPCI */
#define FPCI_DBG_CFG_2		  0x10F4
#define FPCI_GCAP_NSDO_SHIFT	  18
#define FPCI_GCAP_NSDO_MASK	  (0x3 << FPCI_GCAP_NSDO_SHIFT)

/* max number of SDs */
#define NUM_CAPTURE_SD 1
#define NUM_PLAYBACK_SD 1

/*
 * Tegra194 does not reflect correct number of SDO lines. Below macro
 * is used to update the GCAP register to workaround the issue.
 */
#define TEGRA194_NUM_SDO_LINES	  4

struct hda_tegra {
	struct azx chip;
	struct device *dev;
	struct clk *hda_clk;
	struct clk *hda2codec_2x_clk;
	struct clk *hda2hdmi_clk;
	void __iomem *regs;
	struct work_struct probe_work;
};

#ifdef CONFIG_PM
static int power_save = CONFIG_SND_HDA_POWER_SAVE_DEFAULT;
module_param(power_save, bint, 0644);
MODULE_PARM_DESC(power_save,
		 "Automatic power-saving timeout (in seconds, 0 = disable).");
#else
#define power_save	0
#endif

static const struct hda_controller_ops hda_tegra_ops; /* nothing special */

static void hda_tegra_init(struct hda_tegra *hda)
{
	u32 v;

	/* Enable PCI access */
	v = readl(hda->regs + HDA_IPFS_CONFIG);
	v |= HDA_IPFS_EN_FPCI;
	writel(v, hda->regs + HDA_IPFS_CONFIG);

	/* Enable MEM/IO space and bus master */
	v = readl(hda->regs + HDA_CFG_CMD);
	v &= ~HDA_DISABLE_INTR;
	v |= HDA_ENABLE_MEM_SPACE | HDA_ENABLE_IO_SPACE |
		HDA_ENABLE_BUS_MASTER | HDA_ENABLE_SERR;
	writel(v, hda->regs + HDA_CFG_CMD);

	writel(HDA_BAR0_INIT_PROGRAM, hda->regs + HDA_CFG_BAR0);
	writel(HDA_BAR0_FINAL_PROGRAM, hda->regs + HDA_CFG_BAR0);
	writel(HDA_FPCI_BAR0_START, hda->regs + HDA_IPFS_FPCI_BAR0);

	v = readl(hda->regs + HDA_IPFS_INTR_MASK);
	v |= HDA_IPFS_EN_INTR;
	writel(v, hda->regs + HDA_IPFS_INTR_MASK);
}

static int hda_tegra_enable_clocks(struct hda_tegra *data)
{
	int rc;

	rc = clk_prepare_enable(data->hda_clk);
	if (rc)
		return rc;
	rc = clk_prepare_enable(data->hda2codec_2x_clk);
	if (rc)
		goto disable_hda;
	rc = clk_prepare_enable(data->hda2hdmi_clk);
	if (rc)
		goto disable_codec_2x;

	return 0;

disable_codec_2x:
	clk_disable_unprepare(data->hda2codec_2x_clk);
disable_hda:
	clk_disable_unprepare(data->hda_clk);
	return rc;
}

static void hda_tegra_disable_clocks(struct hda_tegra *data)
{
	clk_disable_unprepare(data->hda2hdmi_clk);
	clk_disable_unprepare(data->hda2codec_2x_clk);
	clk_disable_unprepare(data->hda_clk);
}

/*
 * power management
 */
static int __maybe_unused hda_tegra_suspend(struct device *dev)
{
	struct snd_card *card = dev_get_drvdata(dev);
	int rc;

	rc = pm_runtime_force_suspend(dev);
	if (rc < 0)
		return rc;
	snd_power_change_state(card, SNDRV_CTL_POWER_D3hot);

	return 0;
}

static int __maybe_unused hda_tegra_resume(struct device *dev)
{
	struct snd_card *card = dev_get_drvdata(dev);
	int rc;

	rc = pm_runtime_force_resume(dev);
	if (rc < 0)
		return rc;
	snd_power_change_state(card, SNDRV_CTL_POWER_D0);

	return 0;
}

static int __maybe_unused hda_tegra_runtime_suspend(struct device *dev)
{
	struct snd_card *card = dev_get_drvdata(dev);
	struct azx *chip = card->private_data;
	struct hda_tegra *hda = container_of(chip, struct hda_tegra, chip);

	if (chip && chip->running) {
		/* enable controller wake up event */
		azx_writew(chip, WAKEEN, azx_readw(chip, WAKEEN) |
			   STATESTS_INT_MASK);

		azx_stop_chip(chip);
		azx_enter_link_reset(chip);
	}
	hda_tegra_disable_clocks(hda);

	return 0;
}

static int __maybe_unused hda_tegra_runtime_resume(struct device *dev)
{
	struct snd_card *card = dev_get_drvdata(dev);
	struct azx *chip = card->private_data;
	struct hda_tegra *hda = container_of(chip, struct hda_tegra, chip);
	int rc;

	rc = hda_tegra_enable_clocks(hda);
	if (rc != 0)
		return rc;
	if (chip && chip->running) {
		hda_tegra_init(hda);
		azx_init_chip(chip, 1);
		/* disable controller wake up event*/
		azx_writew(chip, WAKEEN, azx_readw(chip, WAKEEN) &
			   ~STATESTS_INT_MASK);
	}

	return 0;
}

static const struct dev_pm_ops hda_tegra_pm = {
	SET_SYSTEM_SLEEP_PM_OPS(hda_tegra_suspend, hda_tegra_resume)
	SET_RUNTIME_PM_OPS(hda_tegra_runtime_suspend,
			   hda_tegra_runtime_resume,
			   NULL)
};

static int hda_tegra_dev_disconnect(struct snd_device *device)
{
	struct azx *chip = device->device_data;

	chip->bus.shutdown = 1;
	return 0;
}

/*
 * destructor
 */
static int hda_tegra_dev_free(struct snd_device *device)
{
	struct azx *chip = device->device_data;
	struct hda_tegra *hda = container_of(chip, struct hda_tegra, chip);

	cancel_work_sync(&hda->probe_work);
	if (azx_bus(chip)->chip_init) {
		azx_stop_all_streams(chip);
		azx_stop_chip(chip);
	}

	azx_free_stream_pages(chip);
	azx_free_streams(chip);
	snd_hdac_bus_exit(azx_bus(chip));

	return 0;
}

static int hda_tegra_init_chip(struct azx *chip, struct platform_device *pdev)
{
	struct hda_tegra *hda = container_of(chip, struct hda_tegra, chip);
	struct hdac_bus *bus = azx_bus(chip);
	struct device *dev = hda->dev;
	struct resource *res;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	hda->regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(hda->regs))
		return PTR_ERR(hda->regs);

	bus->remap_addr = hda->regs + HDA_BAR0;
	bus->addr = res->start + HDA_BAR0;

	hda_tegra_init(hda);

	return 0;
}

static int hda_tegra_init_clk(struct hda_tegra *hda)
{
	struct device *dev = hda->dev;

	hda->hda_clk = devm_clk_get(dev, "hda");
	if (IS_ERR(hda->hda_clk)) {
		dev_err(dev, "failed to get hda clock\n");
		return PTR_ERR(hda->hda_clk);
	}
	hda->hda2codec_2x_clk = devm_clk_get(dev, "hda2codec_2x");
	if (IS_ERR(hda->hda2codec_2x_clk)) {
		dev_err(dev, "failed to get hda2codec_2x clock\n");
		return PTR_ERR(hda->hda2codec_2x_clk);
	}
	hda->hda2hdmi_clk = devm_clk_get(dev, "hda2hdmi");
	if (IS_ERR(hda->hda2hdmi_clk)) {
		dev_err(dev, "failed to get hda2hdmi clock\n");
		return PTR_ERR(hda->hda2hdmi_clk);
	}

	return 0;
}

static int hda_tegra_first_init(struct azx *chip, struct platform_device *pdev)
{
	struct hda_tegra *hda = container_of(chip, struct hda_tegra, chip);
	struct hdac_bus *bus = azx_bus(chip);
	struct snd_card *card = chip->card;
	int err;
	unsigned short gcap;
	int irq_id = platform_get_irq(pdev, 0);
	const char *sname, *drv_name = "tegra-hda";
	struct device_node *np = pdev->dev.of_node;

	err = hda_tegra_init_chip(chip, pdev);
	if (err)
		return err;

	err = devm_request_irq(chip->card->dev, irq_id, azx_interrupt,
			     IRQF_SHARED, KBUILD_MODNAME, chip);
	if (err) {
		dev_err(chip->card->dev,
			"unable to request IRQ %d, disabling device\n",
			irq_id);
		return err;
	}
	bus->irq = irq_id;
	bus->dma_stop_delay = 100;
	card->sync_irq = bus->irq;

	/*
	 * Tegra194 has 4 SDO lines and the STRIPE can be used to
	 * indicate how many of the SDO lines the stream should be
	 * striped. But GCAP register does not reflect the true
	 * capability of HW. Below workaround helps to fix this.
	 *
	 * GCAP_NSDO is bits 19:18 in T_AZA_DBG_CFG_2,
	 * 0 for 1 SDO, 1 for 2 SDO, 2 for 4 SDO lines.
	 */
	if (of_device_is_compatible(np, "nvidia,tegra194-hda")) {
		u32 val;

		dev_info(card->dev, "Override SDO lines to %u\n",
			 TEGRA194_NUM_SDO_LINES);

		val = readl(hda->regs + FPCI_DBG_CFG_2) & ~FPCI_GCAP_NSDO_MASK;
		val |= (TEGRA194_NUM_SDO_LINES >> 1) << FPCI_GCAP_NSDO_SHIFT;
		writel(val, hda->regs + FPCI_DBG_CFG_2);
	}

	gcap = azx_readw(chip, GCAP);
	dev_dbg(card->dev, "chipset global capabilities = 0x%x\n", gcap);

	chip->align_buffer_size = 1;

	/* read number of streams from GCAP register instead of using
	 * hardcoded value
	 */
	chip->capture_streams = (gcap >> 8) & 0x0f;
	chip->playback_streams = (gcap >> 12) & 0x0f;
	if (!chip->playback_streams && !chip->capture_streams) {
		/* gcap didn't give any info, switching to old method */
		chip->playback_streams = NUM_PLAYBACK_SD;
		chip->capture_streams = NUM_CAPTURE_SD;
	}
	chip->capture_index_offset = 0;
	chip->playback_index_offset = chip->capture_streams;
	chip->num_streams = chip->playback_streams + chip->capture_streams;

	/* initialize streams */
	err = azx_init_streams(chip);
	if (err < 0) {
		dev_err(card->dev, "failed to initialize streams: %d\n", err);
		return err;
	}

	err = azx_alloc_stream_pages(chip);
	if (err < 0) {
		dev_err(card->dev, "failed to allocate stream pages: %d\n",
			err);
		return err;
	}

	/* initialize chip */
	azx_init_chip(chip, 1);

	/*
	 * Playback (for 44.1K/48K, 2-channel, 16-bps) fails with
	 * 4 SDO lines due to legacy design limitation. Following
	 * is, from HD Audio Specification (Revision 1.0a), used to
	 * control striping of the stream across multiple SDO lines
	 * for sample rates <= 48K.
	 *
	 * { ((num_channels * bits_per_sample) / number of SDOs) >= 8 }
	 *
	 * Due to legacy design issue it is recommended that above
	 * ratio must be greater than 8. Since number of SDO lines is
	 * in powers of 2, next available ratio is 16 which can be
	 * used as a limiting factor here.
	 */
	if (of_device_is_compatible(np, "nvidia,tegra30-hda"))
		chip->bus.core.sdo_limit = 16;

	/* codec detection */
	if (!bus->codec_mask) {
		dev_err(card->dev, "no codecs found!\n");
		return -ENODEV;
	}

	/* driver name */
	strncpy(card->driver, drv_name, sizeof(card->driver));
	/* shortname for card */
	sname = of_get_property(np, "nvidia,model", NULL);
	if (!sname)
		sname = drv_name;
	if (strlen(sname) > sizeof(card->shortname))
		dev_info(card->dev, "truncating shortname for card\n");
	strncpy(card->shortname, sname, sizeof(card->shortname));

	/* longname for card */
	snprintf(card->longname, sizeof(card->longname),
		 "%s at 0x%lx irq %i",
		 card->shortname, bus->addr, bus->irq);

	return 0;
}

/*
 * constructor
 */

static void hda_tegra_probe_work(struct work_struct *work);

static int hda_tegra_create(struct snd_card *card,
			    unsigned int driver_caps,
			    struct hda_tegra *hda)
{
	static const struct snd_device_ops ops = {
		.dev_disconnect = hda_tegra_dev_disconnect,
		.dev_free = hda_tegra_dev_free,
	};
	struct azx *chip;
	int err;

	chip = &hda->chip;

	mutex_init(&chip->open_mutex);
	chip->card = card;
	chip->ops = &hda_tegra_ops;
	chip->driver_caps = driver_caps;
	chip->driver_type = driver_caps & 0xff;
	chip->dev_index = 0;
	INIT_LIST_HEAD(&chip->pcm_list);

	chip->codec_probe_mask = -1;

	chip->single_cmd = false;
	chip->snoop = true;

	INIT_WORK(&hda->probe_work, hda_tegra_probe_work);

	err = azx_bus_init(chip, NULL);
	if (err < 0)
		return err;

	chip->bus.core.sync_write = 0;
	chip->bus.core.needs_damn_long_delay = 1;
	chip->bus.core.aligned_mmio = 1;

	err = snd_device_new(card, SNDRV_DEV_LOWLEVEL, chip, &ops);
	if (err < 0) {
		dev_err(card->dev, "Error creating device\n");
		return err;
	}

	return 0;
}

static const struct of_device_id hda_tegra_match[] = {
	{ .compatible = "nvidia,tegra30-hda" },
	{ .compatible = "nvidia,tegra194-hda" },
	{},
};
MODULE_DEVICE_TABLE(of, hda_tegra_match);

static int hda_tegra_probe(struct platform_device *pdev)
{
	const unsigned int driver_flags = AZX_DCAPS_CORBRP_SELF_CLEAR |
					  AZX_DCAPS_PM_RUNTIME;
	struct snd_card *card;
	struct azx *chip;
	struct hda_tegra *hda;
	int err;

	hda = devm_kzalloc(&pdev->dev, sizeof(*hda), GFP_KERNEL);
	if (!hda)
		return -ENOMEM;
	hda->dev = &pdev->dev;
	chip = &hda->chip;

	err = snd_card_new(&pdev->dev, SNDRV_DEFAULT_IDX1, SNDRV_DEFAULT_STR1,
			   THIS_MODULE, 0, &card);
	if (err < 0) {
		dev_err(&pdev->dev, "Error creating card!\n");
		return err;
	}

	err = hda_tegra_init_clk(hda);
	if (err < 0)
		goto out_free;

	err = hda_tegra_create(card, driver_flags, hda);
	if (err < 0)
		goto out_free;
	card->private_data = chip;

	dev_set_drvdata(&pdev->dev, card);

	pm_runtime_enable(hda->dev);
	if (!azx_has_pm_runtime(chip))
		pm_runtime_forbid(hda->dev);

	schedule_work(&hda->probe_work);

	return 0;

out_free:
	snd_card_free(card);
	return err;
}

static void hda_tegra_probe_work(struct work_struct *work)
{
	struct hda_tegra *hda = container_of(work, struct hda_tegra, probe_work);
	struct azx *chip = &hda->chip;
	struct platform_device *pdev = to_platform_device(hda->dev);
	int err;

	pm_runtime_get_sync(hda->dev);
	err = hda_tegra_first_init(chip, pdev);
	if (err < 0)
		goto out_free;

	/* create codec instances */
	err = azx_probe_codecs(chip, 8);
	if (err < 0)
		goto out_free;

	err = azx_codec_configure(chip);
	if (err < 0)
		goto out_free;

	err = snd_card_register(chip->card);
	if (err < 0)
		goto out_free;

	chip->running = 1;
	snd_hda_set_power_save(&chip->bus, power_save * 1000);

 out_free:
	pm_runtime_put(hda->dev);
	return; /* no error return from async probe */
}

static int hda_tegra_remove(struct platform_device *pdev)
{
	int ret;

	ret = snd_card_free(dev_get_drvdata(&pdev->dev));
	pm_runtime_disable(&pdev->dev);

	return ret;
}

static void hda_tegra_shutdown(struct platform_device *pdev)
{
	struct snd_card *card = dev_get_drvdata(&pdev->dev);
	struct azx *chip;

	if (!card)
		return;
	chip = card->private_data;
	if (chip && chip->running)
		azx_stop_chip(chip);
}

static struct platform_driver tegra_platform_hda = {
	.driver = {
		.name = "tegra-hda",
		.pm = &hda_tegra_pm,
		.of_match_table = hda_tegra_match,
	},
	.probe = hda_tegra_probe,
	.remove = hda_tegra_remove,
	.shutdown = hda_tegra_shutdown,
};
module_platform_driver(tegra_platform_hda);

MODULE_DESCRIPTION("Tegra HDA bus driver");
MODULE_LICENSE("GPL v2");
