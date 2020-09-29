// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright(c) 2020 Intel Corporation. All rights reserved.
//
// Author: Cezary Rojewski <cezary.rojewski@intel.com>
//

#include <linux/dma-mapping.h>
#include <linux/firmware.h>
#include "core.h"
#include "registers.h"

static bool catpt_dma_filter(struct dma_chan *chan, void *param)
{
	return param == chan->device->dev;
}

/*
 * Either engine 0 or 1 can be used for image loading.
 * Align with Windows driver equivalent and stick to engine 1.
 */
#define CATPT_DMA_DEVID		1
#define CATPT_DMA_DSP_ADDR_MASK	GENMASK(31, 20)

struct dma_chan *catpt_dma_request_config_chan(struct catpt_dev *cdev)
{
	struct dma_slave_config config;
	struct dma_chan *chan;
	dma_cap_mask_t mask;
	int ret;

	dma_cap_zero(mask);
	dma_cap_set(DMA_MEMCPY, mask);

	chan = dma_request_channel(mask, catpt_dma_filter, cdev->dev);
	if (!chan) {
		dev_err(cdev->dev, "request channel failed\n");
		return ERR_PTR(-ENODEV);
	}

	memset(&config, 0, sizeof(config));
	config.direction = DMA_MEM_TO_DEV;
	config.src_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	config.dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	config.src_maxburst = 16;
	config.dst_maxburst = 16;

	ret = dmaengine_slave_config(chan, &config);
	if (ret) {
		dev_err(cdev->dev, "slave config failed: %d\n", ret);
		dma_release_channel(chan);
		return ERR_PTR(ret);
	}

	return chan;
}

static int catpt_dma_memcpy(struct catpt_dev *cdev, struct dma_chan *chan,
			    dma_addr_t dst_addr, dma_addr_t src_addr,
			    size_t size)
{
	struct dma_async_tx_descriptor *desc;
	enum dma_status status;

	desc = dmaengine_prep_dma_memcpy(chan, dst_addr, src_addr, size,
					 DMA_CTRL_ACK);
	if (!desc) {
		dev_err(cdev->dev, "prep dma memcpy failed\n");
		return -EIO;
	}

	/* enable demand mode for dma channel */
	catpt_updatel_shim(cdev, HMDC,
			   CATPT_HMDC_HDDA(CATPT_DMA_DEVID, chan->chan_id),
			   CATPT_HMDC_HDDA(CATPT_DMA_DEVID, chan->chan_id));
	dmaengine_submit(desc);
	status = dma_wait_for_async_tx(desc);
	/* regardless of status, disable access to HOST memory in demand mode */
	catpt_updatel_shim(cdev, HMDC,
			   CATPT_HMDC_HDDA(CATPT_DMA_DEVID, chan->chan_id), 0);

	return (status == DMA_COMPLETE) ? 0 : -EPROTO;
}

int catpt_dma_memcpy_todsp(struct catpt_dev *cdev, struct dma_chan *chan,
			   dma_addr_t dst_addr, dma_addr_t src_addr,
			   size_t size)
{
	return catpt_dma_memcpy(cdev, chan, dst_addr | CATPT_DMA_DSP_ADDR_MASK,
				src_addr, size);
}

int catpt_dma_memcpy_fromdsp(struct catpt_dev *cdev, struct dma_chan *chan,
			     dma_addr_t dst_addr, dma_addr_t src_addr,
			     size_t size)
{
	return catpt_dma_memcpy(cdev, chan, dst_addr,
				src_addr | CATPT_DMA_DSP_ADDR_MASK, size);
}

int catpt_dmac_probe(struct catpt_dev *cdev)
{
	struct dw_dma_chip *dmac;
	int ret;

	dmac = devm_kzalloc(cdev->dev, sizeof(*dmac), GFP_KERNEL);
	if (!dmac)
		return -ENOMEM;

	dmac->regs = cdev->lpe_ba + cdev->spec->host_dma_offset[CATPT_DMA_DEVID];
	dmac->dev = cdev->dev;
	dmac->irq = cdev->irq;

	ret = dma_coerce_mask_and_coherent(cdev->dev, DMA_BIT_MASK(31));
	if (ret)
		return ret;
	/*
	 * Caller is responsible for putting device in D0 to allow
	 * for I/O and memory access before probing DW.
	 */
	ret = dw_dma_probe(dmac);
	if (ret)
		return ret;

	cdev->dmac = dmac;
	return 0;
}

void catpt_dmac_remove(struct catpt_dev *cdev)
{
	/*
	 * As do_dma_remove() juggles with pm_runtime_get_xxx() and
	 * pm_runtime_put_xxx() while both ADSP and DW 'devices' are part of
	 * the same module, caller makes sure pm_runtime_disable() is invoked
	 * before removing DW to prevent postmortem resume and suspend.
	 */
	dw_dma_remove(cdev->dmac);
}
