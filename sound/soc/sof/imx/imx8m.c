// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// Copyright 2020 NXP
//
// Author: Daniel Baluta <daniel.baluta@nxp.com>
//
// Hardware interface for audio DSP on i.MX8M

#include <linux/bits.h>
#include <linux/firmware.h>
#include <linux/mfd/syscon.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/regmap.h>

#include <linux/module.h>
#include <sound/sof.h>
#include <sound/sof/xtensa.h>
#include <linux/firmware/imx/dsp.h>

#include "../ops.h"
#include "../sof-of-dev.h"
#include "imx-common.h"

#define MBOX_OFFSET	0x800000
#define MBOX_SIZE	0x1000

static struct clk_bulk_data imx8m_dsp_clks[] = {
	{ .id = "ipg" },
	{ .id = "ocram" },
	{ .id = "core" },
};

/* DAP registers */
#define IMX8M_DAP_DEBUG                0x28800000
#define IMX8M_DAP_DEBUG_SIZE   (64 * 1024)
#define IMX8M_DAP_PWRCTL       (0x4000 + 0x3020)
#define IMX8M_PWRCTL_CORERESET         BIT(16)

/* DSP audio mix registers */
#define AudioDSP_REG0	0x100
#define AudioDSP_REG1	0x104
#define AudioDSP_REG2	0x108
#define AudioDSP_REG3	0x10c

#define AudioDSP_REG2_RUNSTALL	BIT(5)

struct imx8m_priv {
	struct device *dev;
	struct snd_sof_dev *sdev;

	/* DSP IPC handler */
	struct imx_dsp_ipc *dsp_ipc;
	struct platform_device *ipc_dev;

	struct imx_clocks *clks;

	void __iomem *dap;
	struct regmap *regmap;
};

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
	snd_sof_ipc_process_reply(priv->sdev, 0);
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
		snd_sof_dsp_panic(priv->sdev, p, true);
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
	struct imx8m_priv *priv = (struct imx8m_priv *)sdev->pdata->hw_pdata;

	regmap_update_bits(priv->regmap, AudioDSP_REG2, AudioDSP_REG2_RUNSTALL, 0);

	return 0;
}

static int imx8m_reset(struct snd_sof_dev *sdev)
{
	struct imx8m_priv *priv = (struct imx8m_priv *)sdev->pdata->hw_pdata;
	u32 pwrctl;

	/* put DSP into reset and stall */
	pwrctl = readl(priv->dap + IMX8M_DAP_PWRCTL);
	pwrctl |= IMX8M_PWRCTL_CORERESET;
	writel(pwrctl, priv->dap + IMX8M_DAP_PWRCTL);

	/* keep reset asserted for 10 cycles */
	usleep_range(1, 2);

	regmap_update_bits(priv->regmap, AudioDSP_REG2,
			   AudioDSP_REG2_RUNSTALL, AudioDSP_REG2_RUNSTALL);

	/* take the DSP out of reset and keep stalled for FW loading */
	pwrctl = readl(priv->dap + IMX8M_DAP_PWRCTL);
	pwrctl &= ~IMX8M_PWRCTL_CORERESET;
	writel(pwrctl, priv->dap + IMX8M_DAP_PWRCTL);

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

	priv->clks = devm_kzalloc(&pdev->dev, sizeof(*priv->clks), GFP_KERNEL);
	if (!priv->clks)
		return -ENOMEM;

	sdev->num_cores = 1;
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

	priv->dap = devm_ioremap(sdev->dev, IMX8M_DAP_DEBUG, IMX8M_DAP_DEBUG_SIZE);
	if (!priv->dap) {
		dev_err(sdev->dev, "error: failed to map DAP debug memory area");
		ret = -ENODEV;
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
	of_node_put(res_node);
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

	priv->regmap = syscon_regmap_lookup_by_compatible("fsl,dsp-ctrl");
	if (IS_ERR(priv->regmap)) {
		dev_err(sdev->dev, "cannot find dsp-ctrl registers");
		ret = PTR_ERR(priv->regmap);
		goto exit_pdev_unregister;
	}

	/* init clocks info */
	priv->clks->dsp_clks = imx8m_dsp_clks;
	priv->clks->num_dsp_clks = ARRAY_SIZE(imx8m_dsp_clks);

	ret = imx8_parse_clocks(sdev, priv->clks);
	if (ret < 0)
		goto exit_pdev_unregister;

	ret = imx8_enable_clocks(sdev, priv->clks);
	if (ret < 0)
		goto exit_pdev_unregister;

	return 0;

exit_pdev_unregister:
	platform_device_unregister(priv->ipc_dev);
	return ret;
}

static int imx8m_remove(struct snd_sof_dev *sdev)
{
	struct imx8m_priv *priv = sdev->pdata->hw_pdata;

	imx8_disable_clocks(sdev, priv->clks);
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

static struct snd_soc_dai_driver imx8m_dai[] = {
{
	.name = "sai1",
	.playback = {
		.channels_min = 1,
		.channels_max = 32,
	},
	.capture = {
		.channels_min = 1,
		.channels_max = 32,
	},
},
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

static int imx8m_dsp_set_power_state(struct snd_sof_dev *sdev,
				     const struct sof_dsp_power_state *target_state)
{
	sdev->dsp_power_state = *target_state;

	return 0;
}

static int imx8m_resume(struct snd_sof_dev *sdev)
{
	struct imx8m_priv *priv = (struct imx8m_priv *)sdev->pdata->hw_pdata;
	int ret;
	int i;

	ret = imx8_enable_clocks(sdev, priv->clks);
	if (ret < 0)
		return ret;

	for (i = 0; i < DSP_MU_CHAN_NUM; i++)
		imx_dsp_request_channel(priv->dsp_ipc, i);

	return 0;
}

static void imx8m_suspend(struct snd_sof_dev *sdev)
{
	struct imx8m_priv *priv = (struct imx8m_priv *)sdev->pdata->hw_pdata;
	int i;

	for (i = 0; i < DSP_MU_CHAN_NUM; i++)
		imx_dsp_free_channel(priv->dsp_ipc, i);

	imx8_disable_clocks(sdev, priv->clks);
}

static int imx8m_dsp_runtime_resume(struct snd_sof_dev *sdev)
{
	int ret;
	const struct sof_dsp_power_state target_dsp_state = {
		.state = SOF_DSP_PM_D0,
	};

	ret = imx8m_resume(sdev);
	if (ret < 0)
		return ret;

	return snd_sof_dsp_set_power_state(sdev, &target_dsp_state);
}

static int imx8m_dsp_runtime_suspend(struct snd_sof_dev *sdev)
{
	const struct sof_dsp_power_state target_dsp_state = {
		.state = SOF_DSP_PM_D3,
	};

	imx8m_suspend(sdev);

	return snd_sof_dsp_set_power_state(sdev, &target_dsp_state);
}

static int imx8m_dsp_resume(struct snd_sof_dev *sdev)
{
	int ret;
	const struct sof_dsp_power_state target_dsp_state = {
		.state = SOF_DSP_PM_D0,
	};

	ret = imx8m_resume(sdev);
	if (ret < 0)
		return ret;

	if (pm_runtime_suspended(sdev->dev)) {
		pm_runtime_disable(sdev->dev);
		pm_runtime_set_active(sdev->dev);
		pm_runtime_mark_last_busy(sdev->dev);
		pm_runtime_enable(sdev->dev);
		pm_runtime_idle(sdev->dev);
	}

	return snd_sof_dsp_set_power_state(sdev, &target_dsp_state);
}

static int imx8m_dsp_suspend(struct snd_sof_dev *sdev, unsigned int target_state)
{
	const struct sof_dsp_power_state target_dsp_state = {
		.state = target_state,
	};

	if (!pm_runtime_suspended(sdev->dev))
		imx8m_suspend(sdev);

	return snd_sof_dsp_set_power_state(sdev, &target_dsp_state);
}

/* i.MX8 ops */
static struct snd_sof_dsp_ops sof_imx8m_ops = {
	/* probe and remove */
	.probe		= imx8m_probe,
	.remove		= imx8m_remove,
	/* DSP core boot */
	.run		= imx8m_run,
	.reset		= imx8m_reset,

	/* Block IO */
	.block_read	= sof_block_read,
	.block_write	= sof_block_write,

	/* Mailbox IO */
	.mailbox_read	= sof_mailbox_read,
	.mailbox_write	= sof_mailbox_write,

	/* ipc */
	.send_msg	= imx8m_send_msg,
	.fw_ready	= sof_fw_ready,
	.get_mailbox_offset	= imx8m_get_mailbox_offset,
	.get_window_offset	= imx8m_get_window_offset,

	.ipc_msg_data	= sof_ipc_msg_data,
	.set_stream_data_offset = sof_set_stream_data_offset,

	/* module loading */
	.load_module	= snd_sof_parse_module_memcpy,
	.get_bar_index	= imx8m_get_bar_index,
	/* firmware loading */
	.load_firmware	= snd_sof_load_firmware_memcpy,

	/* Debug information */
	.dbg_dump = imx8_dump,
	.debugfs_add_region_item = snd_sof_debugfs_add_region_item_iomem,

	/* stream callbacks */
	.pcm_open	= sof_stream_pcm_open,
	.pcm_close	= sof_stream_pcm_close,
	/* Firmware ops */
	.dsp_arch_ops = &sof_xtensa_arch_ops,

	/* DAI drivers */
	.drv = imx8m_dai,
	.num_drv = ARRAY_SIZE(imx8m_dai),

	.suspend	= imx8m_dsp_suspend,
	.resume		= imx8m_dsp_resume,

	.runtime_suspend = imx8m_dsp_runtime_suspend,
	.runtime_resume = imx8m_dsp_runtime_resume,

	.set_power_state = imx8m_dsp_set_power_state,

	.hw_info = SNDRV_PCM_INFO_MMAP |
		SNDRV_PCM_INFO_MMAP_VALID |
		SNDRV_PCM_INFO_INTERLEAVED |
		SNDRV_PCM_INFO_PAUSE |
		SNDRV_PCM_INFO_NO_PERIOD_WAKEUP,
};

static struct sof_dev_desc sof_of_imx8mp_desc = {
	.ipc_supported_mask	= BIT(SOF_IPC),
	.ipc_default		= SOF_IPC,
	.default_fw_path = {
		[SOF_IPC] = "imx/sof",
	},
	.default_tplg_path = {
		[SOF_IPC] = "imx/sof-tplg",
	},
	.default_fw_filename = {
		[SOF_IPC] = "sof-imx8m.ri",
	},
	.nocodec_tplg_filename = "sof-imx8-nocodec.tplg",
	.ops = &sof_imx8m_ops,
};

static const struct of_device_id sof_of_imx8m_ids[] = {
	{ .compatible = "fsl,imx8mp-dsp", .data = &sof_of_imx8mp_desc},
	{ }
};
MODULE_DEVICE_TABLE(of, sof_of_imx8m_ids);

/* DT driver definition */
static struct platform_driver snd_sof_of_imx8m_driver = {
	.probe = sof_of_probe,
	.remove = sof_of_remove,
	.driver = {
		.name = "sof-audio-of-imx8m",
		.pm = &sof_of_pm,
		.of_match_table = sof_of_imx8m_ids,
	},
};
module_platform_driver(snd_sof_of_imx8m_driver);

MODULE_IMPORT_NS(SND_SOC_SOF_XTENSA);
MODULE_LICENSE("Dual BSD/GPL");
