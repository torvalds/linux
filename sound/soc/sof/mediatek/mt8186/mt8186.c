// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// Copyright(c) 2022 Mediatek Inc. All rights reserved.
//
// Author: Allen-KH Cheng <allen-kh.cheng@mediatek.com>
//         Tinghan Shen <tinghan.shen@mediatek.com>

/*
 * Hardware interface for audio DSP on mt8186
 */

#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/io.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/of_reserved_mem.h>
#include <linux/module.h>

#include <sound/sof.h>
#include <sound/sof/xtensa.h>
#include "../../ops.h"
#include "../../sof-of-dev.h"
#include "../../sof-audio.h"
#include "../adsp_helper.h"
#include "../mtk-adsp-common.h"
#include "mt8186.h"
#include "mt8186-clk.h"

static int mt8186_get_mailbox_offset(struct snd_sof_dev *sdev)
{
	return MBOX_OFFSET;
}

static int mt8186_get_window_offset(struct snd_sof_dev *sdev, u32 id)
{
	return MBOX_OFFSET;
}

static int mt8186_send_msg(struct snd_sof_dev *sdev,
			   struct snd_sof_ipc_msg *msg)
{
	struct adsp_priv *priv = sdev->pdata->hw_pdata;

	sof_mailbox_write(sdev, sdev->host_box.offset, msg->msg_data,
			  msg->msg_size);

	return mtk_adsp_ipc_send(priv->dsp_ipc, MTK_ADSP_IPC_REQ, MTK_ADSP_IPC_OP_REQ);
}

static void mt8186_dsp_handle_reply(struct mtk_adsp_ipc *ipc)
{
	struct adsp_priv *priv = mtk_adsp_ipc_get_data(ipc);
	unsigned long flags;

	spin_lock_irqsave(&priv->sdev->ipc_lock, flags);
	snd_sof_ipc_process_reply(priv->sdev, 0);
	spin_unlock_irqrestore(&priv->sdev->ipc_lock, flags);
}

static void mt8186_dsp_handle_request(struct mtk_adsp_ipc *ipc)
{
	struct adsp_priv *priv = mtk_adsp_ipc_get_data(ipc);
	u32 p; /* panic code */
	int ret;

	/* Read the message from the debug box. */
	sof_mailbox_read(priv->sdev, priv->sdev->debug_box.offset + 4,
			 &p, sizeof(p));

	/* Check to see if the message is a panic code 0x0dead*** */
	if ((p & SOF_IPC_PANIC_MAGIC_MASK) == SOF_IPC_PANIC_MAGIC) {
		snd_sof_dsp_panic(priv->sdev, p, true);
	} else {
		snd_sof_ipc_msgs_rx(priv->sdev);

		/* tell DSP cmd is done */
		ret = mtk_adsp_ipc_send(priv->dsp_ipc, MTK_ADSP_IPC_RSP, MTK_ADSP_IPC_OP_RSP);
		if (ret)
			dev_err(priv->dev, "request send ipc failed");
	}
}

static struct mtk_adsp_ipc_ops dsp_ops = {
	.handle_reply		= mt8186_dsp_handle_reply,
	.handle_request		= mt8186_dsp_handle_request,
};

static int platform_parse_resource(struct platform_device *pdev, void *data)
{
	struct resource *mmio;
	struct resource res;
	struct device_node *mem_region;
	struct device *dev = &pdev->dev;
	struct mtk_adsp_chip_info *adsp = data;
	int ret;

	ret = of_reserved_mem_device_init(dev);
	if (ret) {
		dev_err(dev, "of_reserved_mem_device_init failed\n");
		return ret;
	}

	mem_region = of_parse_phandle(dev->of_node, "memory-region", 1);
	if (!mem_region) {
		dev_err(dev, "no memory-region sysmem phandle\n");
		return -ENODEV;
	}

	ret = of_address_to_resource(mem_region, 0, &res);
	of_node_put(mem_region);
	if (ret) {
		dev_err(dev, "of_address_to_resource sysmem failed\n");
		return ret;
	}

	adsp->pa_dram = (phys_addr_t)res.start;
	if (adsp->pa_dram & DRAM_REMAP_MASK) {
		dev_err(dev, "adsp memory(%#x) is not 4K-aligned\n",
			(u32)adsp->pa_dram);
		return -EINVAL;
	}

	adsp->dramsize = resource_size(&res);
	if (adsp->dramsize < TOTAL_SIZE_SHARED_DRAM_FROM_TAIL) {
		dev_err(dev, "adsp memory(%#x) is not enough for share\n",
			adsp->dramsize);
		return -EINVAL;
	}

	dev_dbg(dev, "dram pbase=%pa size=%#x\n", &adsp->pa_dram, adsp->dramsize);

	mmio = platform_get_resource_byname(pdev, IORESOURCE_MEM, "cfg");
	if (!mmio) {
		dev_err(dev, "no ADSP-CFG register resource\n");
		return -ENXIO;
	}

	adsp->va_cfgreg = devm_ioremap_resource(dev, mmio);
	if (IS_ERR(adsp->va_cfgreg))
		return PTR_ERR(adsp->va_cfgreg);

	adsp->pa_cfgreg = (phys_addr_t)mmio->start;
	adsp->cfgregsize = resource_size(mmio);

	dev_dbg(dev, "cfgreg pbase=%pa size=%#x\n", &adsp->pa_cfgreg, adsp->cfgregsize);

	mmio = platform_get_resource_byname(pdev, IORESOURCE_MEM, "sram");
	if (!mmio) {
		dev_err(dev, "no SRAM resource\n");
		return -ENXIO;
	}

	adsp->pa_sram = (phys_addr_t)mmio->start;
	adsp->sramsize = resource_size(mmio);

	dev_dbg(dev, "sram pbase=%pa size=%#x\n", &adsp->pa_sram, adsp->sramsize);

	mmio = platform_get_resource_byname(pdev, IORESOURCE_MEM, "sec");
	if (!mmio) {
		dev_err(dev, "no SEC register resource\n");
		return -ENXIO;
	}

	adsp->va_secreg = devm_ioremap_resource(dev, mmio);
	if (IS_ERR(adsp->va_secreg))
		return PTR_ERR(adsp->va_secreg);

	adsp->pa_secreg = (phys_addr_t)mmio->start;
	adsp->secregsize = resource_size(mmio);

	dev_dbg(dev, "secreg pbase=%pa size=%#x\n", &adsp->pa_secreg, adsp->secregsize);

	mmio = platform_get_resource_byname(pdev, IORESOURCE_MEM, "bus");
	if (!mmio) {
		dev_err(dev, "no BUS register resource\n");
		return -ENXIO;
	}

	adsp->va_busreg = devm_ioremap_resource(dev, mmio);
	if (IS_ERR(adsp->va_busreg))
		return PTR_ERR(adsp->va_busreg);

	adsp->pa_busreg = (phys_addr_t)mmio->start;
	adsp->busregsize = resource_size(mmio);

	dev_dbg(dev, "busreg pbase=%pa size=%#x\n", &adsp->pa_busreg, adsp->busregsize);

	return 0;
}

static void adsp_sram_power_on(struct snd_sof_dev *sdev)
{
	snd_sof_dsp_update_bits(sdev, DSP_BUSREG_BAR, ADSP_SRAM_POOL_CON,
				DSP_SRAM_POOL_PD_MASK, 0);
}

static void adsp_sram_power_off(struct snd_sof_dev *sdev)
{
	snd_sof_dsp_update_bits(sdev, DSP_BUSREG_BAR, ADSP_SRAM_POOL_CON,
				DSP_SRAM_POOL_PD_MASK, DSP_SRAM_POOL_PD_MASK);
}

/*  Init the basic DSP DRAM address */
static int adsp_memory_remap_init(struct snd_sof_dev *sdev, struct mtk_adsp_chip_info *adsp)
{
	u32 offset;

	offset = adsp->pa_dram - DRAM_PHYS_BASE_FROM_DSP_VIEW;
	adsp->dram_offset = offset;
	offset >>= DRAM_REMAP_SHIFT;

	dev_dbg(sdev->dev, "adsp->pa_dram %pa, offset %#x\n", &adsp->pa_dram, offset);

	snd_sof_dsp_write(sdev, DSP_BUSREG_BAR, DSP_C0_EMI_MAP_ADDR, offset);
	snd_sof_dsp_write(sdev, DSP_BUSREG_BAR, DSP_C0_DMAEMI_MAP_ADDR, offset);

	if (offset != snd_sof_dsp_read(sdev, DSP_BUSREG_BAR, DSP_C0_EMI_MAP_ADDR) ||
	    offset != snd_sof_dsp_read(sdev, DSP_BUSREG_BAR, DSP_C0_DMAEMI_MAP_ADDR)) {
		dev_err(sdev->dev, "emi remap fail\n");
		return -EIO;
	}

	return 0;
}

static int mt8186_run(struct snd_sof_dev *sdev)
{
	u32 adsp_bootup_addr;

	adsp_bootup_addr = SRAM_PHYS_BASE_FROM_DSP_VIEW;
	dev_dbg(sdev->dev, "HIFIxDSP boot from base : 0x%08X\n", adsp_bootup_addr);
	mt8186_sof_hifixdsp_boot_sequence(sdev, adsp_bootup_addr);

	return 0;
}

static int mt8186_dsp_probe(struct snd_sof_dev *sdev)
{
	struct platform_device *pdev = container_of(sdev->dev, struct platform_device, dev);
	struct adsp_priv *priv;
	int ret;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	sdev->pdata->hw_pdata = priv;
	priv->dev = sdev->dev;
	priv->sdev = sdev;

	priv->adsp = devm_kzalloc(&pdev->dev, sizeof(struct mtk_adsp_chip_info), GFP_KERNEL);
	if (!priv->adsp)
		return -ENOMEM;

	ret = platform_parse_resource(pdev, priv->adsp);
	if (ret)
		return ret;

	sdev->bar[SOF_FW_BLK_TYPE_IRAM] = devm_ioremap(sdev->dev,
						       priv->adsp->pa_sram,
						       priv->adsp->sramsize);
	if (!sdev->bar[SOF_FW_BLK_TYPE_IRAM]) {
		dev_err(sdev->dev, "failed to ioremap base %pa size %#x\n",
			&priv->adsp->pa_sram, priv->adsp->sramsize);
		return -ENOMEM;
	}

	priv->adsp->va_sram = sdev->bar[SOF_FW_BLK_TYPE_IRAM];

	sdev->bar[SOF_FW_BLK_TYPE_SRAM] = devm_ioremap(sdev->dev,
						       priv->adsp->pa_dram,
						       priv->adsp->dramsize);

	if (!sdev->bar[SOF_FW_BLK_TYPE_SRAM]) {
		dev_err(sdev->dev, "failed to ioremap base %pa size %#x\n",
			&priv->adsp->pa_dram, priv->adsp->dramsize);
		return -ENOMEM;
	}

	priv->adsp->va_dram = sdev->bar[SOF_FW_BLK_TYPE_SRAM];

	sdev->bar[DSP_REG_BAR] = priv->adsp->va_cfgreg;
	sdev->bar[DSP_SECREG_BAR] = priv->adsp->va_secreg;
	sdev->bar[DSP_BUSREG_BAR] = priv->adsp->va_busreg;

	sdev->mmio_bar = SOF_FW_BLK_TYPE_SRAM;
	sdev->mailbox_bar = SOF_FW_BLK_TYPE_SRAM;

	/* set default mailbox offset for FW ready message */
	sdev->dsp_box.offset = mt8186_get_mailbox_offset(sdev);

	ret = adsp_memory_remap_init(sdev, priv->adsp);
	if (ret) {
		dev_err(sdev->dev, "adsp_memory_remap_init fail!\n");
		return ret;
	}

	/* enable adsp clock before touching registers */
	ret = mt8186_adsp_init_clock(sdev);
	if (ret) {
		dev_err(sdev->dev, "mt8186_adsp_init_clock failed\n");
		return ret;
	}

	ret = mt8186_adsp_clock_on(sdev);
	if (ret) {
		dev_err(sdev->dev, "mt8186_adsp_clock_on fail!\n");
		return ret;
	}

	adsp_sram_power_on(sdev);

	priv->ipc_dev = platform_device_register_data(&pdev->dev, "mtk-adsp-ipc",
						      PLATFORM_DEVID_NONE,
						      pdev, sizeof(*pdev));
	if (IS_ERR(priv->ipc_dev)) {
		ret = PTR_ERR(priv->ipc_dev);
		dev_err(sdev->dev, "failed to create mtk-adsp-ipc device\n");
		goto err_adsp_off;
	}

	priv->dsp_ipc = dev_get_drvdata(&priv->ipc_dev->dev);
	if (!priv->dsp_ipc) {
		ret = -EPROBE_DEFER;
		dev_err(sdev->dev, "failed to get drvdata\n");
		goto exit_pdev_unregister;
	}

	mtk_adsp_ipc_set_data(priv->dsp_ipc, priv);
	priv->dsp_ipc->ops = &dsp_ops;

	return 0;

exit_pdev_unregister:
	platform_device_unregister(priv->ipc_dev);
err_adsp_off:
	adsp_sram_power_off(sdev);
	mt8186_adsp_clock_off(sdev);

	return ret;
}

static void mt8186_dsp_remove(struct snd_sof_dev *sdev)
{
	struct adsp_priv *priv = sdev->pdata->hw_pdata;

	platform_device_unregister(priv->ipc_dev);
	mt8186_sof_hifixdsp_shutdown(sdev);
	adsp_sram_power_off(sdev);
	mt8186_adsp_clock_off(sdev);
}

static int mt8186_dsp_shutdown(struct snd_sof_dev *sdev)
{
	return snd_sof_suspend(sdev->dev);
}

static int mt8186_dsp_suspend(struct snd_sof_dev *sdev, u32 target_state)
{
	mt8186_sof_hifixdsp_shutdown(sdev);
	adsp_sram_power_off(sdev);
	mt8186_adsp_clock_off(sdev);

	return 0;
}

static int mt8186_dsp_resume(struct snd_sof_dev *sdev)
{
	int ret;

	ret = mt8186_adsp_clock_on(sdev);
	if (ret) {
		dev_err(sdev->dev, "mt8186_adsp_clock_on fail!\n");
		return ret;
	}

	adsp_sram_power_on(sdev);

	return ret;
}

/* on mt8186 there is 1 to 1 match between type and BAR idx */
static int mt8186_get_bar_index(struct snd_sof_dev *sdev, u32 type)
{
	return type;
}

static int mt8186_pcm_hw_params(struct snd_sof_dev *sdev,
				struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params,
				struct snd_sof_platform_stream_params *platform_params)
{
	platform_params->cont_update_posn = 1;

	return 0;
}

static snd_pcm_uframes_t mt8186_pcm_pointer(struct snd_sof_dev *sdev,
					    struct snd_pcm_substream *substream)
{
	int ret;
	snd_pcm_uframes_t pos;
	struct snd_sof_pcm *spcm;
	struct sof_ipc_stream_posn posn;
	struct snd_sof_pcm_stream *stream;
	struct snd_soc_component *scomp = sdev->component;
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);

	spcm = snd_sof_find_spcm_dai(scomp, rtd);
	if (!spcm) {
		dev_warn_ratelimited(sdev->dev, "warn: can't find PCM with DAI ID %d\n",
				     rtd->dai_link->id);
		return 0;
	}

	stream = &spcm->stream[substream->stream];
	ret = snd_sof_ipc_msg_data(sdev, stream, &posn, sizeof(posn));
	if (ret < 0) {
		dev_warn(sdev->dev, "failed to read stream position: %d\n", ret);
		return 0;
	}

	memcpy(&stream->posn, &posn, sizeof(posn));
	pos = spcm->stream[substream->stream].posn.host_posn;
	pos = bytes_to_frames(substream->runtime, pos);

	return pos;
}

static void mt8186_adsp_dump(struct snd_sof_dev *sdev, u32 flags)
{
	u32 dbg_pc, dbg_data, dbg_inst, dbg_ls0stat, dbg_status, faultinfo;

	/* dump debug registers */
	dbg_pc = snd_sof_dsp_read(sdev, DSP_REG_BAR, DSP_PDEBUGPC);
	dbg_data = snd_sof_dsp_read(sdev, DSP_REG_BAR, DSP_PDEBUGDATA);
	dbg_inst = snd_sof_dsp_read(sdev, DSP_REG_BAR, DSP_PDEBUGINST);
	dbg_ls0stat = snd_sof_dsp_read(sdev, DSP_REG_BAR, DSP_PDEBUGLS0STAT);
	dbg_status = snd_sof_dsp_read(sdev, DSP_REG_BAR, DSP_PDEBUGSTATUS);
	faultinfo = snd_sof_dsp_read(sdev, DSP_REG_BAR, DSP_PFAULTINFO);

	dev_info(sdev->dev, "adsp dump : pc %#x, data %#x, dbg_inst %#x,",
		 dbg_pc, dbg_data, dbg_inst);
	dev_info(sdev->dev, "ls0stat %#x, status %#x, faultinfo %#x",
		 dbg_ls0stat, dbg_status, faultinfo);

	mtk_adsp_dump(sdev, flags);
}

static struct snd_soc_dai_driver mt8186_dai[] = {
{
	.name = "SOF_DL1",
	.playback = {
		.channels_min = 1,
		.channels_max = 2,
	},
},
{
	.name = "SOF_DL2",
	.playback = {
		.channels_min = 1,
		.channels_max = 2,
	},
},
{
	.name = "SOF_UL1",
	.capture = {
		.channels_min = 1,
		.channels_max = 2,
	},
},
{
	.name = "SOF_UL2",
	.capture = {
		.channels_min = 1,
		.channels_max = 2,
	},
},
};

/* mt8186 ops */
static const struct snd_sof_dsp_ops sof_mt8186_ops = {
	/* probe and remove */
	.probe		= mt8186_dsp_probe,
	.remove		= mt8186_dsp_remove,
	.shutdown	= mt8186_dsp_shutdown,

	/* DSP core boot */
	.run		= mt8186_run,

	/* Block IO */
	.block_read	= sof_block_read,
	.block_write	= sof_block_write,

	/* Mailbox IO */
	.mailbox_read	= sof_mailbox_read,
	.mailbox_write	= sof_mailbox_write,

	/* Register IO */
	.write		= sof_io_write,
	.read		= sof_io_read,
	.write64	= sof_io_write64,
	.read64		= sof_io_read64,

	/* ipc */
	.send_msg		= mt8186_send_msg,
	.get_mailbox_offset	= mt8186_get_mailbox_offset,
	.get_window_offset	= mt8186_get_window_offset,
	.ipc_msg_data		= sof_ipc_msg_data,
	.set_stream_data_offset = sof_set_stream_data_offset,

	/* misc */
	.get_bar_index	= mt8186_get_bar_index,

	/* stream callbacks */
	.pcm_open	= sof_stream_pcm_open,
	.pcm_hw_params	= mt8186_pcm_hw_params,
	.pcm_pointer	= mt8186_pcm_pointer,
	.pcm_close	= sof_stream_pcm_close,

	/* firmware loading */
	.load_firmware	= snd_sof_load_firmware_memcpy,

	/* Firmware ops */
	.dsp_arch_ops = &sof_xtensa_arch_ops,

	/* DAI drivers */
	.drv		= mt8186_dai,
	.num_drv	= ARRAY_SIZE(mt8186_dai),

	/* Debug information */
	.dbg_dump = mt8186_adsp_dump,
	.debugfs_add_region_item = snd_sof_debugfs_add_region_item_iomem,

	/* PM */
	.suspend	= mt8186_dsp_suspend,
	.resume		= mt8186_dsp_resume,

	/* ALSA HW info flags */
	.hw_info =	SNDRV_PCM_INFO_MMAP |
			SNDRV_PCM_INFO_MMAP_VALID |
			SNDRV_PCM_INFO_INTERLEAVED |
			SNDRV_PCM_INFO_PAUSE |
			SNDRV_PCM_INFO_NO_PERIOD_WAKEUP,
};

static struct snd_sof_of_mach sof_mt8186_machs[] = {
	{
		.compatible = "mediatek,mt8186",
		.sof_tplg_filename = "sof-mt8186.tplg",
	},
	{}
};

static const struct sof_dev_desc sof_of_mt8186_desc = {
	.of_machines = sof_mt8186_machs,
	.ipc_supported_mask	= BIT(SOF_IPC_TYPE_3),
	.ipc_default		= SOF_IPC_TYPE_3,
	.default_fw_path = {
		[SOF_IPC_TYPE_3] = "mediatek/sof",
	},
	.default_tplg_path = {
		[SOF_IPC_TYPE_3] = "mediatek/sof-tplg",
	},
	.default_fw_filename = {
		[SOF_IPC_TYPE_3] = "sof-mt8186.ri",
	},
	.nocodec_tplg_filename = "sof-mt8186-nocodec.tplg",
	.ops = &sof_mt8186_ops,
};

/*
 * DL2, DL3, UL4, UL5 are registered as SOF FE, so creating the corresponding
 * SOF BE to complete the pipeline.
 */
static struct snd_soc_dai_driver mt8188_dai[] = {
{
	.name = "SOF_DL2",
	.playback = {
		.channels_min = 1,
		.channels_max = 2,
	},
},
{
	.name = "SOF_DL3",
	.playback = {
		.channels_min = 1,
		.channels_max = 2,
	},
},
{
	.name = "SOF_UL4",
	.capture = {
		.channels_min = 1,
		.channels_max = 2,
	},
},
{
	.name = "SOF_UL5",
	.capture = {
		.channels_min = 1,
		.channels_max = 2,
	},
},
};

/* mt8188 ops */
static struct snd_sof_dsp_ops sof_mt8188_ops;

static int sof_mt8188_ops_init(struct snd_sof_dev *sdev)
{
	/* common defaults */
	memcpy(&sof_mt8188_ops, &sof_mt8186_ops, sizeof(sof_mt8188_ops));

	sof_mt8188_ops.drv = mt8188_dai;
	sof_mt8188_ops.num_drv = ARRAY_SIZE(mt8188_dai);

	return 0;
}

static struct snd_sof_of_mach sof_mt8188_machs[] = {
	{
		.compatible = "mediatek,mt8188",
		.sof_tplg_filename = "sof-mt8188.tplg",
	},
	{}
};

static const struct sof_dev_desc sof_of_mt8188_desc = {
	.of_machines = sof_mt8188_machs,
	.ipc_supported_mask	= BIT(SOF_IPC_TYPE_3),
	.ipc_default		= SOF_IPC_TYPE_3,
	.default_fw_path = {
		[SOF_IPC_TYPE_3] = "mediatek/sof",
	},
	.default_tplg_path = {
		[SOF_IPC_TYPE_3] = "mediatek/sof-tplg",
	},
	.default_fw_filename = {
		[SOF_IPC_TYPE_3] = "sof-mt8188.ri",
	},
	.nocodec_tplg_filename = "sof-mt8188-nocodec.tplg",
	.ops = &sof_mt8188_ops,
	.ops_init = sof_mt8188_ops_init,
};

static const struct of_device_id sof_of_mt8186_ids[] = {
	{ .compatible = "mediatek,mt8186-dsp", .data = &sof_of_mt8186_desc},
	{ .compatible = "mediatek,mt8188-dsp", .data = &sof_of_mt8188_desc},
	{ }
};
MODULE_DEVICE_TABLE(of, sof_of_mt8186_ids);

/* DT driver definition */
static struct platform_driver snd_sof_of_mt8186_driver = {
	.probe = sof_of_probe,
	.remove_new = sof_of_remove,
	.shutdown = sof_of_shutdown,
	.driver = {
	.name = "sof-audio-of-mt8186",
		.pm = &sof_of_pm,
		.of_match_table = sof_of_mt8186_ids,
	},
};
module_platform_driver(snd_sof_of_mt8186_driver);

MODULE_IMPORT_NS(SND_SOC_SOF_XTENSA);
MODULE_IMPORT_NS(SND_SOC_SOF_MTK_COMMON);
MODULE_LICENSE("Dual BSD/GPL");
