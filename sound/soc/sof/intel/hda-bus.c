// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * Copyright(c) 2018 Intel Corporation. All rights reserved.
 *
 * Authors: Jeeja KP <jeeja.kp@intel.com>
 *	Keyon Jie <yang.jie@linux.intel.com>
 */

#include <sound/hdaudio.h>

#if IS_ENABLED(CONFIG_SND_SOC_SOF_HDA)

static const struct hdac_bus_ops bus_ops = {
	.command = snd_hdac_bus_send_cmd,
	.get_response = snd_hdac_bus_get_response,
};

#endif

static void sof_hda_writel(u32 value, u32 __iomem *addr)
{
	writel(value, addr);
}

static u32 sof_hda_readl(u32 __iomem *addr)
{
	return readl(addr);
}

static void sof_hda_writew(u16 value, u16 __iomem *addr)
{
	writew(value, addr);
}

static u16 sof_hda_readw(u16 __iomem *addr)
{
	return readw(addr);
}

static void sof_hda_writeb(u8 value, u8 __iomem *addr)
{
	writeb(value, addr);
}

static u8 sof_hda_readb(u8 __iomem *addr)
{
	return readb(addr);
}

static int sof_hda_dma_alloc_pages(struct hdac_bus *bus, int type,
			   size_t size, struct snd_dma_buffer *buf)
{
	return snd_dma_alloc_pages(type, bus->dev, size, buf);
}

static void sof_hda_dma_free_pages(struct hdac_bus *bus,
				    struct snd_dma_buffer *buf)
{
	snd_dma_free_pages(buf);
}

static const struct hdac_io_ops io_ops = {
	.reg_writel = sof_hda_writel,
	.reg_readl = sof_hda_readl,
	.reg_writew = sof_hda_writew,
	.reg_readw = sof_hda_readw,
	.reg_writeb = sof_hda_writeb,
	.reg_readb = sof_hda_readb,
	.dma_alloc_pages = sof_hda_dma_alloc_pages,
	.dma_free_pages = sof_hda_dma_free_pages,
};

/*
 * This can be used for both with/without hda link support.
 * Returns 0 if successful, or a negative error code.
 */
int sof_hda_bus_init(struct hdac_bus *bus, struct device *dev,
			const struct hdac_ext_bus_ops *ext_ops)
{
	static int idx;

	memset(bus, 0, sizeof(*bus));
	bus->dev = dev;

	bus->io_ops = &io_ops;
	INIT_LIST_HEAD(&bus->stream_list);
	INIT_LIST_HEAD(&bus->codec_list);

#if IS_ENABLED(CONFIG_SND_SOC_SOF_HDA)
	bus->ops = &bus_ops;
	INIT_WORK(&bus->unsol_work, snd_hdac_bus_process_unsol_events);
#endif
	spin_lock_init(&bus->reg_lock);
	mutex_init(&bus->cmd_mutex);
	bus->irq = -1;

	bus->ext_ops = ext_ops;
	INIT_LIST_HEAD(&bus->hlink_list);
	bus->idx = idx++;

	mutex_init(&bus->lock);
	bus->cmd_dma_state = true;

	return 0;
}
