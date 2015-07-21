/*
 *  skl-message.c - HDA DSP interface for FW registration, Pipe and Module
 *  configurations
 *
 *  Copyright (C) 2015 Intel Corp
 *  Author:Rafal Redzimski <rafal.f.redzimski@intel.com>
 *	   Jeeja KP <jeeja.kp@intel.com>
 *  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#include <linux/slab.h>
#include <linux/pci.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include "skl-sst-dsp.h"
#include "skl-sst-ipc.h"
#include "skl.h"
#include "../common/sst-dsp.h"
#include "../common/sst-dsp-priv.h"

static int skl_alloc_dma_buf(struct device *dev,
		struct snd_dma_buffer *dmab, size_t size)
{
	struct hdac_ext_bus *ebus = dev_get_drvdata(dev);
	struct hdac_bus *bus = ebus_to_hbus(ebus);

	if (!bus)
		return -ENODEV;

	return  bus->io_ops->dma_alloc_pages(bus, SNDRV_DMA_TYPE_DEV, size, dmab);
}

static int skl_free_dma_buf(struct device *dev, struct snd_dma_buffer *dmab)
{
	struct hdac_ext_bus *ebus = dev_get_drvdata(dev);
	struct hdac_bus *bus = ebus_to_hbus(ebus);

	if (!bus)
		return -ENODEV;

	bus->io_ops->dma_free_pages(bus, dmab);

	return 0;
}

int skl_init_dsp(struct skl *skl)
{
	void __iomem *mmio_base;
	struct hdac_ext_bus *ebus = &skl->ebus;
	struct hdac_bus *bus = ebus_to_hbus(ebus);
	int irq = bus->irq;
	struct skl_dsp_loader_ops loader_ops;
	int ret;

	loader_ops.alloc_dma_buf = skl_alloc_dma_buf;
	loader_ops.free_dma_buf = skl_free_dma_buf;

	/* enable ppcap interrupt */
	snd_hdac_ext_bus_ppcap_enable(&skl->ebus, true);
	snd_hdac_ext_bus_ppcap_int_enable(&skl->ebus, true);

	/* read the BAR of the ADSP MMIO */
	mmio_base = pci_ioremap_bar(skl->pci, 4);
	if (mmio_base == NULL) {
		dev_err(bus->dev, "ioremap error\n");
		return -ENXIO;
	}

	ret = skl_sst_dsp_init(bus->dev, mmio_base, irq,
			loader_ops, &skl->skl_sst);

	dev_dbg(bus->dev, "dsp registration status=%d\n", ret);

	return ret;
}

void skl_free_dsp(struct skl *skl)
{
	struct hdac_ext_bus *ebus = &skl->ebus;
	struct hdac_bus *bus = ebus_to_hbus(ebus);
	struct skl_sst *ctx =  skl->skl_sst;

	/* disable  ppcap interrupt */
	snd_hdac_ext_bus_ppcap_int_enable(&skl->ebus, false);

	skl_sst_dsp_cleanup(bus->dev, ctx);
	if (ctx->dsp->addr.lpe)
		iounmap(ctx->dsp->addr.lpe);
}

int skl_suspend_dsp(struct skl *skl)
{
	struct skl_sst *ctx = skl->skl_sst;
	int ret;

	/* if ppcap is not supported return 0 */
	if (!skl->ebus.ppcap)
		return 0;

	ret = skl_dsp_sleep(ctx->dsp);
	if (ret < 0)
		return ret;

	/* disable ppcap interrupt */
	snd_hdac_ext_bus_ppcap_int_enable(&skl->ebus, false);
	snd_hdac_ext_bus_ppcap_enable(&skl->ebus, false);

	return 0;
}

int skl_resume_dsp(struct skl *skl)
{
	struct skl_sst *ctx = skl->skl_sst;

	/* if ppcap is not supported return 0 */
	if (!skl->ebus.ppcap)
		return 0;

	/* enable ppcap interrupt */
	snd_hdac_ext_bus_ppcap_enable(&skl->ebus, true);
	snd_hdac_ext_bus_ppcap_int_enable(&skl->ebus, true);

	return skl_dsp_wake(ctx->dsp);
}
