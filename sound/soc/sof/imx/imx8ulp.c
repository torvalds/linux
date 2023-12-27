// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// Copyright 2021-2022 NXP
//
// Author: Peng Zhang <peng.zhang_8@nxp.com>
//
// Hardware interface for audio DSP on i.MX8ULP

#include <linux/arm-smccc.h>
#include <linux/clk.h>
#include <linux/firmware.h>
#include <linux/firmware/imx/dsp.h>
#include <linux/firmware/imx/ipc.h>
#include <linux/firmware/imx/svc/misc.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/of_reserved_mem.h>

#include <sound/sof.h>
#include <sound/sof/xtensa.h>

#include "../ops.h"
#include "../sof-of-dev.h"
#include "imx-common.h"

#define FSL_SIP_HIFI_XRDC	0xc200000e

/* SIM Domain register */
#define SYSCTRL0		0x8
#define EXECUTE_BIT		BIT(13)
#define RESET_BIT		BIT(16)
#define HIFI4_CLK_BIT		BIT(17)
#define PB_CLK_BIT		BIT(18)
#define PLAT_CLK_BIT		BIT(19)
#define DEBUG_LOGIC_BIT		BIT(25)

#define MBOX_OFFSET		0x800000
#define MBOX_SIZE		0x1000

static struct clk_bulk_data imx8ulp_dsp_clks[] = {
	{ .id = "core" },
	{ .id = "ipg" },
	{ .id = "ocram" },
	{ .id = "mu" },
};

struct imx8ulp_priv {
	struct device *dev;
	struct snd_sof_dev *sdev;

	/* DSP IPC handler */
	struct imx_dsp_ipc *dsp_ipc;
	struct platform_device *ipc_dev;

	struct regmap *regmap;
	struct imx_clocks *clks;
};

static void imx8ulp_sim_lpav_start(struct imx8ulp_priv *priv)
{
	/* Controls the HiFi4 DSP Reset: 1 in reset, 0 out of reset */
	regmap_update_bits(priv->regmap, SYSCTRL0, RESET_BIT, 0);

	/* Reset HiFi4 DSP Debug logic: 1 debug reset, 0  out of reset*/
	regmap_update_bits(priv->regmap, SYSCTRL0, DEBUG_LOGIC_BIT, 0);

	/* Stall HIFI4 DSP Execution: 1 stall, 0 run */
	regmap_update_bits(priv->regmap, SYSCTRL0, EXECUTE_BIT, 0);
}

static int imx8ulp_get_mailbox_offset(struct snd_sof_dev *sdev)
{
	return MBOX_OFFSET;
}

static int imx8ulp_get_window_offset(struct snd_sof_dev *sdev, u32 id)
{
	return MBOX_OFFSET;
}

static void imx8ulp_dsp_handle_reply(struct imx_dsp_ipc *ipc)
{
	struct imx8ulp_priv *priv = imx_dsp_get_data(ipc);
	unsigned long flags;

	spin_lock_irqsave(&priv->sdev->ipc_lock, flags);

	snd_sof_ipc_process_reply(priv->sdev, 0);

	spin_unlock_irqrestore(&priv->sdev->ipc_lock, flags);
}

static void imx8ulp_dsp_handle_request(struct imx_dsp_ipc *ipc)
{
	struct imx8ulp_priv *priv = imx_dsp_get_data(ipc);
	u32 p; /* panic code */

	/* Read the message from the debug box. */
	sof_mailbox_read(priv->sdev, priv->sdev->debug_box.offset + 4, &p, sizeof(p));

	/* Check to see if the message is a panic code (0x0dead***) */
	if ((p & SOF_IPC_PANIC_MAGIC_MASK) == SOF_IPC_PANIC_MAGIC)
		snd_sof_dsp_panic(priv->sdev, p, true);
	else
		snd_sof_ipc_msgs_rx(priv->sdev);
}

static struct imx_dsp_ops dsp_ops = {
	.handle_reply		= imx8ulp_dsp_handle_reply,
	.handle_request		= imx8ulp_dsp_handle_request,
};

static int imx8ulp_send_msg(struct snd_sof_dev *sdev, struct snd_sof_ipc_msg *msg)
{
	struct imx8ulp_priv *priv = sdev->pdata->hw_pdata;

	sof_mailbox_write(sdev, sdev->host_box.offset, msg->msg_data,
			  msg->msg_size);
	imx_dsp_ring_doorbell(priv->dsp_ipc, 0);

	return 0;
}

static int imx8ulp_run(struct snd_sof_dev *sdev)
{
	struct imx8ulp_priv *priv = sdev->pdata->hw_pdata;

	imx8ulp_sim_lpav_start(priv);

	return 0;
}

static int imx8ulp_reset(struct snd_sof_dev *sdev)
{
	struct imx8ulp_priv *priv = sdev->pdata->hw_pdata;
	struct arm_smccc_res smc_resource;

	/* HiFi4 Platform Clock Enable: 1 enabled, 0 disabled */
	regmap_update_bits(priv->regmap, SYSCTRL0, PLAT_CLK_BIT, PLAT_CLK_BIT);

	/* HiFi4 PBCLK clock enable: 1 enabled, 0 disabled */
	regmap_update_bits(priv->regmap, SYSCTRL0, PB_CLK_BIT, PB_CLK_BIT);

	/* HiFi4 Clock Enable: 1 enabled, 0 disabled */
	regmap_update_bits(priv->regmap, SYSCTRL0, HIFI4_CLK_BIT, HIFI4_CLK_BIT);

	regmap_update_bits(priv->regmap, SYSCTRL0, RESET_BIT, RESET_BIT);
	usleep_range(1, 2);

	/* Stall HIFI4 DSP Execution: 1 stall, 0 not stall */
	regmap_update_bits(priv->regmap, SYSCTRL0, EXECUTE_BIT, EXECUTE_BIT);
	usleep_range(1, 2);

	arm_smccc_smc(FSL_SIP_HIFI_XRDC, 0, 0, 0, 0, 0, 0, 0, &smc_resource);

	return 0;
}

static int imx8ulp_probe(struct snd_sof_dev *sdev)
{
	struct platform_device *pdev =
		container_of(sdev->dev, struct platform_device, dev);
	struct device_node *np = pdev->dev.of_node;
	struct device_node *res_node;
	struct resource *mmio;
	struct imx8ulp_priv *priv;
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

	/* System integration module(SIM) control dsp configuration */
	priv->regmap = syscon_regmap_lookup_by_phandle(np, "fsl,dsp-ctrl");
	if (IS_ERR(priv->regmap))
		return PTR_ERR(priv->regmap);

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
	priv->dsp_ipc->ops = &dsp_ops;

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

	res_node = of_parse_phandle(np, "memory-reserved", 0);
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

	ret = of_reserved_mem_device_init(sdev->dev);
	if (ret) {
		dev_err(&pdev->dev, "failed to init reserved memory region %d\n", ret);
		goto exit_pdev_unregister;
	}

	priv->clks->dsp_clks = imx8ulp_dsp_clks;
	priv->clks->num_dsp_clks = ARRAY_SIZE(imx8ulp_dsp_clks);

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

static void imx8ulp_remove(struct snd_sof_dev *sdev)
{
	struct imx8ulp_priv *priv = sdev->pdata->hw_pdata;

	imx8_disable_clocks(sdev, priv->clks);
	platform_device_unregister(priv->ipc_dev);
}

/* on i.MX8 there is 1 to 1 match between type and BAR idx */
static int imx8ulp_get_bar_index(struct snd_sof_dev *sdev, u32 type)
{
	return type;
}

static int imx8ulp_suspend(struct snd_sof_dev *sdev)
{
	int i;
	struct imx8ulp_priv *priv = (struct imx8ulp_priv *)sdev->pdata->hw_pdata;

	/*Stall DSP,  release in .run() */
	regmap_update_bits(priv->regmap, SYSCTRL0, EXECUTE_BIT, EXECUTE_BIT);

	for (i = 0; i < DSP_MU_CHAN_NUM; i++)
		imx_dsp_free_channel(priv->dsp_ipc, i);

	imx8_disable_clocks(sdev, priv->clks);

	return 0;
}

static int imx8ulp_resume(struct snd_sof_dev *sdev)
{
	struct imx8ulp_priv *priv = (struct imx8ulp_priv *)sdev->pdata->hw_pdata;
	int i;

	imx8_enable_clocks(sdev, priv->clks);

	for (i = 0; i < DSP_MU_CHAN_NUM; i++)
		imx_dsp_request_channel(priv->dsp_ipc, i);

	return 0;
}

static int imx8ulp_dsp_runtime_resume(struct snd_sof_dev *sdev)
{
	const struct sof_dsp_power_state target_dsp_state = {
		.state = SOF_DSP_PM_D0,
		.substate = 0,
	};

	imx8ulp_resume(sdev);

	return snd_sof_dsp_set_power_state(sdev, &target_dsp_state);
}

static int imx8ulp_dsp_runtime_suspend(struct snd_sof_dev *sdev)
{
	const struct sof_dsp_power_state target_dsp_state = {
		.state = SOF_DSP_PM_D3,
		.substate = 0,
	};

	imx8ulp_suspend(sdev);

	return snd_sof_dsp_set_power_state(sdev, &target_dsp_state);
}

static int imx8ulp_dsp_suspend(struct snd_sof_dev *sdev, unsigned int target_state)
{
	const struct sof_dsp_power_state target_dsp_state = {
		.state = target_state,
		.substate = 0,
	};

	if (!pm_runtime_suspended(sdev->dev))
		imx8ulp_suspend(sdev);

	return snd_sof_dsp_set_power_state(sdev, &target_dsp_state);
}

static int imx8ulp_dsp_resume(struct snd_sof_dev *sdev)
{
	const struct sof_dsp_power_state target_dsp_state = {
		.state = SOF_DSP_PM_D0,
		.substate = 0,
	};

	imx8ulp_resume(sdev);

	if (pm_runtime_suspended(sdev->dev)) {
		pm_runtime_disable(sdev->dev);
		pm_runtime_set_active(sdev->dev);
		pm_runtime_mark_last_busy(sdev->dev);
		pm_runtime_enable(sdev->dev);
		pm_runtime_idle(sdev->dev);
	}

	return snd_sof_dsp_set_power_state(sdev, &target_dsp_state);
}

static struct snd_soc_dai_driver imx8ulp_dai[] = {
	{
		.name = "sai5",
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
		.name = "sai6",
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

static int imx8ulp_dsp_set_power_state(struct snd_sof_dev *sdev,
				       const struct sof_dsp_power_state *target_state)
{
	sdev->dsp_power_state = *target_state;

	return 0;
}

/* i.MX8 ops */
static struct snd_sof_dsp_ops sof_imx8ulp_ops = {
	/* probe and remove */
	.probe		= imx8ulp_probe,
	.remove		= imx8ulp_remove,
	/* DSP core boot */
	.run		= imx8ulp_run,
	.reset		= imx8ulp_reset,

	/* Block IO */
	.block_read	= sof_block_read,
	.block_write	= sof_block_write,

	/* Module IO */
	.read64		= sof_io_read64,

	/* Mailbox IO */
	.mailbox_read	= sof_mailbox_read,
	.mailbox_write	= sof_mailbox_write,

	/* ipc */
	.send_msg	= imx8ulp_send_msg,
	.get_mailbox_offset	= imx8ulp_get_mailbox_offset,
	.get_window_offset	= imx8ulp_get_window_offset,

	.ipc_msg_data	= sof_ipc_msg_data,
	.set_stream_data_offset = sof_set_stream_data_offset,

	/* stream callbacks */
	.pcm_open	= sof_stream_pcm_open,
	.pcm_close	= sof_stream_pcm_close,

	/* module loading */
	.get_bar_index	= imx8ulp_get_bar_index,
	/* firmware loading */
	.load_firmware	= snd_sof_load_firmware_memcpy,

	/* Debug information */
	.dbg_dump	= imx8_dump,

	/* Firmware ops */
	.dsp_arch_ops	= &sof_xtensa_arch_ops,

	/* DAI drivers */
	.drv		= imx8ulp_dai,
	.num_drv	= ARRAY_SIZE(imx8ulp_dai),

	/* ALSA HW info flags */
	.hw_info	= SNDRV_PCM_INFO_MMAP |
			SNDRV_PCM_INFO_MMAP_VALID |
			SNDRV_PCM_INFO_INTERLEAVED |
			SNDRV_PCM_INFO_PAUSE |
			SNDRV_PCM_INFO_BATCH |
			SNDRV_PCM_INFO_NO_PERIOD_WAKEUP,

	/* PM */
	.runtime_suspend	= imx8ulp_dsp_runtime_suspend,
	.runtime_resume		= imx8ulp_dsp_runtime_resume,

	.suspend	= imx8ulp_dsp_suspend,
	.resume		= imx8ulp_dsp_resume,

	.set_power_state	= imx8ulp_dsp_set_power_state,
};

static struct sof_dev_desc sof_of_imx8ulp_desc = {
	.ipc_supported_mask     = BIT(SOF_IPC_TYPE_3),
	.ipc_default            = SOF_IPC_TYPE_3,
	.default_fw_path = {
		[SOF_IPC_TYPE_3] = "imx/sof",
	},
	.default_tplg_path = {
		[SOF_IPC_TYPE_3] = "imx/sof-tplg",
	},
	.default_fw_filename = {
		[SOF_IPC_TYPE_3] = "sof-imx8ulp.ri",
	},
	.nocodec_tplg_filename = "sof-imx8ulp-nocodec.tplg",
	.ops = &sof_imx8ulp_ops,
};

static const struct of_device_id sof_of_imx8ulp_ids[] = {
	{ .compatible = "fsl,imx8ulp-dsp", .data = &sof_of_imx8ulp_desc},
	{ }
};
MODULE_DEVICE_TABLE(of, sof_of_imx8ulp_ids);

/* DT driver definition */
static struct platform_driver snd_sof_of_imx8ulp_driver = {
	.probe = sof_of_probe,
	.remove_new = sof_of_remove,
	.driver = {
		.name = "sof-audio-of-imx8ulp",
		.pm = &sof_of_pm,
		.of_match_table = sof_of_imx8ulp_ids,
	},
};
module_platform_driver(snd_sof_of_imx8ulp_driver);

MODULE_IMPORT_NS(SND_SOC_SOF_XTENSA);
MODULE_LICENSE("Dual BSD/GPL");
