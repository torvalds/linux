// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2018 Intel Corporation. All rights reserved.
//
// Author: Keyon Jie <yang.jie@linux.intel.com>
//

#include <linux/io-64-nonatomic-lo-hi.h>
#include <linux/platform_device.h>
#include <sound/soc.h>
#include <sound/sof.h>
#include "sof-priv.h"

int sof_bes_setup(struct device *dev, const struct snd_sof_dsp_ops *ops,
		  struct snd_soc_dai_link *links, int link_num,
		  struct snd_soc_card *card)
{
	int i;

	if (!ops || !links || !card)
		return -EINVAL;

	/* set up BE dai_links */
	for (i = 0; i < link_num; i++) {
		links[i].name = devm_kasprintf(dev, GFP_KERNEL,
					       "NoCodec-%d", i);
		if (!links[i].name)
			return -ENOMEM;

		links[i].id = i;
		links[i].no_pcm = 1;
		links[i].cpu_dai_name = ops->drv[i].name;
		links[i].platform_name = "sof-audio";
		links[i].codec_dai_name = "snd-soc-dummy-dai";
		links[i].codec_name = "snd-soc-dummy";
		links[i].dpcm_playback = 1;
		links[i].dpcm_capture = 1;
	}

	card->dai_link = links;
	card->num_links = link_num;

	return 0;
}
EXPORT_SYMBOL(sof_bes_setup);

/* register sof platform device */
int sof_create_platform_device(struct sof_platform_priv *priv)
{
	struct snd_sof_pdata *sof_pdata = priv->sof_pdata;
	struct device *dev = sof_pdata->dev;

	priv->pdev_pcm =
		platform_device_register_data(dev, "sof-audio",
					      PLATFORM_DEVID_NONE,
					      sof_pdata, sizeof(*sof_pdata));
	if (IS_ERR(priv->pdev_pcm)) {
		dev_err(dev, "error: cannot register device sof-audio. Error %ld\n",
			PTR_ERR(priv->pdev_pcm));
		return PTR_ERR(priv->pdev_pcm);
	}

	return 0;
}
EXPORT_SYMBOL(sof_create_platform_device);

/*
 * Register IO
 */

void sof_io_write(struct snd_sof_dev *sdev, void __iomem *addr, u32 value)
{
	writel(value, addr);
}
EXPORT_SYMBOL(sof_io_write);

u32 sof_io_read(struct snd_sof_dev *sdev, void __iomem *addr)
{
	return readl(addr);
}
EXPORT_SYMBOL(sof_io_read);

void sof_io_write64(struct snd_sof_dev *sdev, void __iomem *addr, u64 value)
{
	writeq(value, addr);
}
EXPORT_SYMBOL(sof_io_write64);

u64 sof_io_read64(struct snd_sof_dev *sdev, void __iomem *addr)
{
	return readq(addr);
}
EXPORT_SYMBOL(sof_io_read64);

/*
 * IPC Mailbox IO
 */

void sof_mailbox_write(struct snd_sof_dev *sdev, u32 offset,
		       void *message, size_t bytes)
{
	void __iomem *dest = sdev->bar[sdev->mailbox_bar] + offset;

	memcpy_toio(dest, message, bytes);
}
EXPORT_SYMBOL(sof_mailbox_write);

void sof_mailbox_read(struct snd_sof_dev *sdev, u32 offset,
		      void *message, size_t bytes)
{
	void __iomem *src = sdev->bar[sdev->mailbox_bar] + offset;

	memcpy_fromio(message, src, bytes);
}
EXPORT_SYMBOL(sof_mailbox_read);

/*
 * Memory copy.
 */

void sof_block_write(struct snd_sof_dev *sdev, u32 bar, u32 offset, void *src,
		     size_t size)
{
	void __iomem *dest = sdev->bar[bar] + offset;
	const u8 *src_byte = src;
	u32 affected_mask;
	u32 tmp = 0;
	int m, n;

	m = size / 4;
	n = size % 4;

	/* __iowrite32_copy use 32bit size values so divide by 4 */
	__iowrite32_copy(dest, src, m);

	if (n) {
		affected_mask = (1 << (8 * n)) - 1;

		/* first read the 32bit data of dest, then change affected
		 * bytes, and write back to dest. For unaffected bytes, it
		 * should not be changed
		 */
		tmp = ioread32(dest + m * 4);
		tmp &= ~affected_mask;

		tmp |= *(u32 *)(src_byte + m * 4) & affected_mask;
		iowrite32(tmp, dest + m * 4);
	}
}
EXPORT_SYMBOL(sof_block_write);

void sof_block_read(struct snd_sof_dev *sdev, u32 bar, u32 offset, void *dest,
		    size_t size)
{
	void __iomem *src = sdev->bar[bar] + offset;

	memcpy_fromio(dest, src, size);
}
EXPORT_SYMBOL(sof_block_read);
