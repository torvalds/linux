/*
 *
 * Implementation of primary ALSA driver code base for NVIDIA Tegra HDA.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
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

#include <sound/core.h>
#include <sound/initval.h>

#include "hda_codec.h"
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

/* max number of SDs */
#define NUM_CAPTURE_SD 1
#define NUM_PLAYBACK_SD 1

struct hda_tegra {
	struct azx chip;
	struct device *dev;
	struct clk *hda_clk;
	struct clk *hda2codec_2x_clk;
	struct clk *hda2hdmi_clk;
	void __iomem *regs;
};

#ifdef CONFIG_PM
static int power_save = CONFIG_SND_HDA_POWER_SAVE_DEFAULT;
module_param(power_save, bint, 0644);
MODULE_PARM_DESC(power_save,
		 "Automatic power-saving timeout (in seconds, 0 = disable).");
#else
#define power_save	0
#endif

/*
 * DMA page allocation ops.
 */
static int dma_alloc_pages(struct azx *chip, int type, size_t size,
			   struct snd_dma_buffer *buf)
{
	return snd_dma_alloc_pages(type, chip->card->dev, size, buf);
}

static void dma_free_pages(struct azx *chip, struct snd_dma_buffer *buf)
{
	snd_dma_free_pages(buf);
}

static int substream_alloc_pages(struct azx *chip,
				 struct snd_pcm_substream *substream,
				 size_t size)
{
	struct azx_dev *azx_dev = get_azx_dev(substream);

	azx_dev->bufsize = 0;
	azx_dev->period_bytes = 0;
	azx_dev->format_val = 0;
	return snd_pcm_lib_malloc_pages(substream, size);
}

static int substream_free_pages(struct azx *chip,
				struct snd_pcm_substream *substream)
{
	return snd_pcm_lib_free_pages(substream);
}

/*
 * Register access ops. Tegra HDA register access is DWORD only.
 */
static void hda_tegra_writel(u32 value, u32 *addr)
{
	writel(value, addr);
}

static u32 hda_tegra_readl(u32 *addr)
{
	return readl(addr);
}

static void hda_tegra_writew(u16 value, u16 *addr)
{
	unsigned int shift = ((unsigned long)(addr) & 0x3) << 3;
	void *dword_addr = (void *)((unsigned long)(addr) & ~0x3);
	u32 v;

	v = readl(dword_addr);
	v &= ~(0xffff << shift);
	v |= value << shift;
	writel(v, dword_addr);
}

static u16 hda_tegra_readw(u16 *addr)
{
	unsigned int shift = ((unsigned long)(addr) & 0x3) << 3;
	void *dword_addr = (void *)((unsigned long)(addr) & ~0x3);
	u32 v;

	v = readl(dword_addr);
	return (v >> shift) & 0xffff;
}

static void hda_tegra_writeb(u8 value, u8 *addr)
{
	unsigned int shift = ((unsigned long)(addr) & 0x3) << 3;
	void *dword_addr = (void *)((unsigned long)(addr) & ~0x3);
	u32 v;

	v = readl(dword_addr);
	v &= ~(0xff << shift);
	v |= value << shift;
	writel(v, dword_addr);
}

static u8 hda_tegra_readb(u8 *addr)
{
	unsigned int shift = ((unsigned long)(addr) & 0x3) << 3;
	void *dword_addr = (void *)((unsigned long)(addr) & ~0x3);
	u32 v;

	v = readl(dword_addr);
	return (v >> shift) & 0xff;
}

static const struct hda_controller_ops hda_tegra_ops = {
	.reg_writel = hda_tegra_writel,
	.reg_readl = hda_tegra_readl,
	.reg_writew = hda_tegra_writew,
	.reg_readw = hda_tegra_readw,
	.reg_writeb = hda_tegra_writeb,
	.reg_readb = hda_tegra_readb,
	.dma_alloc_pages = dma_alloc_pages,
	.dma_free_pages = dma_free_pages,
	.substream_alloc_pages = substream_alloc_pages,
	.substream_free_pages = substream_free_pages,
};

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

#ifdef CONFIG_PM_SLEEP
static void hda_tegra_disable_clocks(struct hda_tegra *data)
{
	clk_disable_unprepare(data->hda2hdmi_clk);
	clk_disable_unprepare(data->hda2codec_2x_clk);
	clk_disable_unprepare(data->hda_clk);
}

/*
 * power management
 */
static int hda_tegra_suspend(struct device *dev)
{
	struct snd_card *card = dev_get_drvdata(dev);
	struct azx *chip = card->private_data;
	struct hda_tegra *hda = container_of(chip, struct hda_tegra, chip);

	snd_power_change_state(card, SNDRV_CTL_POWER_D3hot);

	azx_stop_chip(chip);
	azx_enter_link_reset(chip);
	hda_tegra_disable_clocks(hda);

	return 0;
}

static int hda_tegra_resume(struct device *dev)
{
	struct snd_card *card = dev_get_drvdata(dev);
	struct azx *chip = card->private_data;
	struct hda_tegra *hda = container_of(chip, struct hda_tegra, chip);

	hda_tegra_enable_clocks(hda);

	hda_tegra_init(hda);

	azx_init_chip(chip, 1);

	snd_power_change_state(card, SNDRV_CTL_POWER_D0);

	return 0;
}
#endif /* CONFIG_PM_SLEEP */

static const struct dev_pm_ops hda_tegra_pm = {
	SET_SYSTEM_SLEEP_PM_OPS(hda_tegra_suspend, hda_tegra_resume)
};

/*
 * destructor
 */
static int hda_tegra_dev_free(struct snd_device *device)
{
	int i;
	struct azx *chip = device->device_data;

	azx_notifier_unregister(chip);

	if (chip->initialized) {
		for (i = 0; i < chip->num_streams; i++)
			azx_stream_stop(chip, &chip->azx_dev[i]);
		azx_stop_chip(chip);
	}

	azx_free_stream_pages(chip);

	return 0;
}

static int hda_tegra_init_chip(struct azx *chip, struct platform_device *pdev)
{
	struct hda_tegra *hda = container_of(chip, struct hda_tegra, chip);
	struct device *dev = hda->dev;
	struct resource *res;
	int err;

	hda->hda_clk = devm_clk_get(dev, "hda");
	if (IS_ERR(hda->hda_clk))
		return PTR_ERR(hda->hda_clk);
	hda->hda2codec_2x_clk = devm_clk_get(dev, "hda2codec_2x");
	if (IS_ERR(hda->hda2codec_2x_clk))
		return PTR_ERR(hda->hda2codec_2x_clk);
	hda->hda2hdmi_clk = devm_clk_get(dev, "hda2hdmi");
	if (IS_ERR(hda->hda2hdmi_clk))
		return PTR_ERR(hda->hda2hdmi_clk);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	hda->regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(hda->regs))
		return PTR_ERR(hda->regs);

	chip->remap_addr = hda->regs + HDA_BAR0;
	chip->addr = res->start + HDA_BAR0;

	err = hda_tegra_enable_clocks(hda);
	if (err)
		return err;

	hda_tegra_init(hda);

	return 0;
}

static int hda_tegra_first_init(struct azx *chip, struct platform_device *pdev)
{
	struct snd_card *card = chip->card;
	int err;
	unsigned short gcap;
	int irq_id = platform_get_irq(pdev, 0);

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
	chip->irq = irq_id;

	synchronize_irq(chip->irq);

	gcap = azx_readw(chip, GCAP);
	dev_dbg(card->dev, "chipset global capabilities = 0x%x\n", gcap);

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
	chip->azx_dev = devm_kcalloc(card->dev, chip->num_streams,
				     sizeof(*chip->azx_dev), GFP_KERNEL);
	if (!chip->azx_dev)
		return -ENOMEM;

	err = azx_alloc_stream_pages(chip);
	if (err < 0)
		return err;

	/* initialize streams */
	azx_init_stream(chip);

	/* initialize chip */
	azx_init_chip(chip, 1);

	/* codec detection */
	if (!chip->codec_mask) {
		dev_err(card->dev, "no codecs found!\n");
		return -ENODEV;
	}

	strcpy(card->driver, "tegra-hda");
	strcpy(card->shortname, "tegra-hda");
	snprintf(card->longname, sizeof(card->longname),
		 "%s at 0x%lx irq %i",
		 card->shortname, chip->addr, chip->irq);

	return 0;
}

/*
 * constructor
 */
static int hda_tegra_create(struct snd_card *card,
			    unsigned int driver_caps,
			    const struct hda_controller_ops *hda_ops,
			    struct hda_tegra *hda)
{
	static struct snd_device_ops ops = {
		.dev_free = hda_tegra_dev_free,
	};
	struct azx *chip;
	int err;

	chip = &hda->chip;

	spin_lock_init(&chip->reg_lock);
	mutex_init(&chip->open_mutex);
	chip->card = card;
	chip->ops = hda_ops;
	chip->irq = -1;
	chip->driver_caps = driver_caps;
	chip->driver_type = driver_caps & 0xff;
	chip->dev_index = 0;
	INIT_LIST_HEAD(&chip->pcm_list);

	chip->codec_probe_mask = -1;

	chip->single_cmd = false;
	chip->snoop = true;

	err = snd_device_new(card, SNDRV_DEV_LOWLEVEL, chip, &ops);
	if (err < 0) {
		dev_err(card->dev, "Error creating device\n");
		return err;
	}

	return 0;
}

static const struct of_device_id hda_tegra_match[] = {
	{ .compatible = "nvidia,tegra30-hda" },
	{},
};
MODULE_DEVICE_TABLE(of, hda_tegra_match);

static int hda_tegra_probe(struct platform_device *pdev)
{
	struct snd_card *card;
	struct azx *chip;
	struct hda_tegra *hda;
	int err;
	const unsigned int driver_flags = AZX_DCAPS_RIRB_DELAY;

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

	err = hda_tegra_create(card, driver_flags, &hda_tegra_ops, hda);
	if (err < 0)
		goto out_free;
	card->private_data = chip;

	dev_set_drvdata(&pdev->dev, card);

	err = hda_tegra_first_init(chip, pdev);
	if (err < 0)
		goto out_free;

	/* create codec instances */
	err = azx_bus_create(chip, NULL);
	if (err < 0)
		goto out_free;

	err = azx_probe_codecs(chip, 0);
	if (err < 0)
		goto out_free;

	err = azx_codec_configure(chip);
	if (err < 0)
		goto out_free;

	err = snd_card_register(chip->card);
	if (err < 0)
		goto out_free;

	chip->running = 1;
	azx_notifier_register(chip);
	snd_hda_set_power_save(chip->bus, power_save * 1000);

	return 0;

out_free:
	snd_card_free(card);
	return err;
}

static int hda_tegra_remove(struct platform_device *pdev)
{
	return snd_card_free(dev_get_drvdata(&pdev->dev));
}

static struct platform_driver tegra_platform_hda = {
	.driver = {
		.name = "tegra-hda",
		.pm = &hda_tegra_pm,
		.of_match_table = hda_tegra_match,
	},
	.probe = hda_tegra_probe,
	.remove = hda_tegra_remove,
};
module_platform_driver(tegra_platform_hda);

MODULE_DESCRIPTION("Tegra HDA bus driver");
MODULE_LICENSE("GPL v2");
