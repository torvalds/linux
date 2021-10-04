// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// Copyright 2020 NXP
//
// Author: Daniel Baluta <daniel.baluta@nxp.com>
//
// Hardware interface for audio DSP on i.MX8M

#include <linux/firmware.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>

#include <linux/module.h>
#include <sound/sof.h>
#include <sound/sof/xtensa.h>
#include <linux/firmware/imx/dsp.h>

#include "../ops.h"
#include "imx-common.h"

#define MBOX_OFFSET	0x800000
#define MBOX_SIZE	0x1000

struct imx8m_priv {
	struct device *dev;
	struct snd_sof_dev *sdev;

	/* DSP IPC handler */
	struct imx_dsp_ipc *dsp_ipc;
	struct platform_device *ipc_dev;
};

static void imx8m_get_reply(struct snd_sof_dev *sdev)
{
	struct snd_sof_ipc_msg *msg = sdev->msg;
	struct sof_ipc_reply reply;
	int ret = 0;

	if (!msg) {
		dev_warn(sdev->dev, "unexpected ipc interrupt\n");
		return;
	}

	/* get reply */
	sof_mailbox_read(sdev, sdev->host_box.offset, &reply, sizeof(reply));

	if (reply.error < 0) {
		memcpy(msg->reply_data, &reply, sizeof(reply));
		ret = reply.error;
	} else {
		/* reply has correct size? */
		if (reply.hdr.size != msg->reply_size) {
			dev_err(sdev->dev, "error: reply expected %zu got %u bytes\n",
				msg->reply_size, reply.hdr.size);
			ret = -EINVAL;
		}

		/* read the message */
		if (msg->reply_size > 0)
			sof_mailbox_read(sdev, sdev->host_box.offset,
					 msg->reply_data, msg->reply_size);
	}

	msg->reply_error = ret;
}

static int imx8m_get_mailbox_offset(struct snd_sof_dev *sdev)
{
	return MBOX_OFFSET;
}

static int imx8m_get_window_offset(struct snd_sof_dev *sdev, u32 id)
{
	return MBOX_OFFSET;
}

static void imx8m_dsp_handle_reply(struct imx_dsp_ipc *ipc)
{
	struct imx8m_priv *priv = imx_dsp_get_data(ipc);
	unsigned long flags;

	spin_lock_irqsave(&priv->sdev->ipc_lock, flags);
	imx8m_get_reply(priv->sdev);
	snd_sof_ipc_reply(priv->sdev, 0);
	spin_unlock_irqrestore(&priv->sdev->ipc_lock, flags);
}

static void imx8m_dsp_handle_request(struct imx_dsp_ipc *ipc)
{
	struct imx8m_priv *priv = imx_dsp_get_data(ipc);
	u32 p; /* Panic code */

	/* Read the message from the debug box. */
	sof_mailbox_read(priv->sdev, priv->sdev->debug_box.offset + 4, &p, sizeof(p));

	/* Check to see if the message is a panic code (0x0dead***) */
	if ((p & SOF_IPC_PANIC_MAGIC_MASK) == SOF_IPC_PANIC_MAGIC)
		snd_sof_dsp_panic(priv->sdev, p);
	else
		snd_sof_ipc_msgs_rx(priv->sdev);
}

static struct imx_dsp_ops imx8m_dsp_ops = {
	.handle_reply		= imx8m_dsp_handle_reply,
	.handle_request		= imx8m_dsp_handle_request,
};

static int imx8m_send_msg(struct snd_sof_dev *sdev, struct snd_sof_ipc_msg *msg)
{
	struct imx8m_priv *priv = sdev->pdata->hw_pdata;

	sof_mailbox_write(sdev, sdev->host_box.offset, msg->msg_data,
			  msg->msg_size);
	imx_dsp_ring_doorbell(priv->dsp_ipc, 0);

	return 0;
}

/*
 * DSP control.
 */
static int imx8m_run(struct snd_sof_dev *sdev)
{
	/* TODO: start DSP using Audio MIX bits */
	return 0;
}

static int imx8m_probe(struct snd_sof_dev *sdev)
{
	struct platform_device *pdev =
		container_of(sdev->dev, struct platform_device, dev);
	struct device_node *np = pdev->dev.of_node;
	struct device_node *res_node;
	struct resource *mmio;
	struct imx8m_priv *priv;
	struct resource res;
	u32 base, size;
	int ret = 0;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	sdev->pdata->hw_pdata = priv;
	priv->dev = sdev->dev;
	priv->sdev = sdev;

	priv->ipc_dev = platform_device_register_data(sdev->dev, "imx-dsp",
						      PLATFORM_DEVID_NONE,
						      pdev, sizeof(*pdev));
	if (IS_ERR(priv->ipc_dev))
		return PTR_ERR(priv->ipc_dev);

	priv->dsp_ipc = dev_get_drvdata(&priv->ipc_dev->dev);
	if (!priv->dsp_ipc) {
		/* DSP IPC driver not probed yet, try later */
		ret = -EPROBE_DEFER;
		dev_err(sdev->dev, "Failed to get drvdata\n");
		goto exit_pdev_unregister;
	}

	imx_dsp_set_data(priv->dsp_ipc, priv);
	priv->dsp_ipc->ops = &imx8m_dsp_ops;

	/* DSP base */
	mmio = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (mmio) {
		base = mmio->start;
		size = resource_size(mmio);
	} else {
		dev_err(sdev->dev, "error: failed to get DSP base at idx 0\n");
		ret = -EINVAL;
		goto exit_pdev_unregister;
	}

	sdev->bar[SOF_FW_BLK_TYPE_IRAM] = devm_ioremap(sdev->dev, base, size);
	if (!sdev->bar[SOF_FW_BLK_TYPE_IRAM]) {
		dev_err(sdev->dev, "failed to ioremap base 0x%x size 0x%x\n",
			base, size);
		ret = -ENODEV;
		goto exit_pdev_unregister;
	}
	sdev->mmio_bar = SOF_FW_BLK_TYPE_IRAM;

	res_node = of_parse_phandle(np, "memory-region", 0);
	if (!res_node) {
		dev_err(&pdev->dev, "failed to get memory region node\n");
		ret = -ENODEV;
		goto exit_pdev_unregister;
	}

	ret = of_address_to_resource(res_node, 0, &res);
	if (ret) {
		dev_err(&pdev->dev, "failed to get reserved region address\n");
		goto exit_pdev_unregister;
	}

	sdev->bar[SOF_FW_BLK_TYPE_SRAM] = devm_ioremap_wc(sdev->dev, res.start,
							  resource_size(&res));
	if (!sdev->bar[SOF_FW_BLK_TYPE_SRAM]) {
		dev_err(sdev->dev, "failed to ioremap mem 0x%x size 0x%x\n",
			base, size);
		ret = -ENOMEM;
		goto exit_pdev_unregister;
	}
	sdev->mailbox_bar = SOF_FW_BLK_TYPE_SRAM;

	/* set default mailbox offset for FW ready message */
	sdev->dsp_box.offset = MBOX_OFFSET;

	return 0;

exit_pdev_unregister:
	platform_device_unregister(priv->ipc_dev);
	return ret;
}

static int imx8m_remove(struct snd_sof_dev *sdev)
{
	struct imx8m_priv *priv = sdev->pdata->hw_pdata;

	platform_device_unregister(priv->ipc_dev);

	return 0;
}

/* on i.MX8 there is 1 to 1 match between type and BAR idx */
static int imx8m_get_bar_index(struct snd_sof_dev *sdev, u32 type)
{
	/* Only IRAM and SRAM bars are valid */
	switch (type) {
	case SOF_FW_BLK_TYPE_IRAM:
	case SOF_FW_BLK_TYPE_SRAM:
		return type;
	default:
		return -EINVAL;
	}
}

static void imx8m_ipc_msg_data(struct snd_sof_dev *sdev,
			       struct snd_pcm_substream *substream,
			       void *p, size_t sz)
{
	sof_mailbox_read(sdev, sdev->dsp_box.offset, p, sz);
}

static int imx8m_ipc_pcm_params(struct snd_sof_dev *sdev,
				struct snd_pcm_substream *substream,
				const struct sof_ipc_pcm_params_reply *reply)
{
	return 0;
}

static struct snd_soc_dai_driver imx8m_dai[] = {
{
	.name = "sai3",
	.playback = {
		.channels_min = 1,
		.channels_max = 32,
	},
	.capture = {
		.channels_min = 1,
		.channels_max = 32,
	},
},
};

/* i.MX8 ops */
struct snd_sof_dsp_ops sof_imx8m_ops = {
	/* probe and remove */
	.probe		= imx8m_probe,
	.remove		= imx8m_remove,
	/* DSP core boot */
	.run		= imx8m_run,

	/* Block IO */
	.block_read	= sof_block_read,
	.block_write	= sof_block_write,

	/* Module IO */
	.read64	= sof_io_read64,

	/* ipc */
	.send_msg	= imx8m_send_msg,
	.fw_ready	= sof_fw_ready,
	.get_mailbox_offset	= imx8m_get_mailbox_offset,
	.get_window_offset	= imx8m_get_window_offset,

	.ipc_msg_data	= imx8m_ipc_msg_data,
	.ipc_pcm_params	= imx8m_ipc_pcm_params,

	/* module loading */
	.load_module	= snd_sof_parse_module_memcpy,
	.get_bar_index	= imx8m_get_bar_index,
	/* firmware loading */
	.load_firmware	= snd_sof_load_firmware_memcpy,

	/* Debug information */
	.dbg_dump = imx8_dump,

	/* Firmware ops */
	.arch_ops = &sof_xtensa_arch_ops,

	/* DAI drivers */
	.drv = imx8m_dai,
	.num_drv = ARRAY_SIZE(imx8m_dai),

	.hw_info = SNDRV_PCM_INFO_MMAP |
		SNDRV_PCM_INFO_MMAP_VALID |
		SNDRV_PCM_INFO_INTERLEAVED |
		SNDRV_PCM_INFO_PAUSE |
		SNDRV_PCM_INFO_NO_PERIOD_WAKEUP,
};
EXPORT_SYMBOL(sof_imx8m_ops);

MODULE_IMPORT_NS(SND_SOC_SOF_XTENSA);
MODULE_LICENSE("Dual BSD/GPL");
