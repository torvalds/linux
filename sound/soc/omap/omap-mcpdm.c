/*
 * omap-mcpdm.c  --  OMAP ALSA SoC DAI driver using McPDM port
 *
 * Copyright (C) 2009 - 2011 Texas Instruments
 *
 * Author: Misael Lopez Cruz <misael.lopez@ti.com>
 * Contact: Jorge Eduardo Candelaria <x0107209@ti.com>
 *          Margarita Olaya <magi.olaya@ti.com>
 *          Peter Ujfalusi <peter.ujfalusi@ti.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/slab.h>
#include <linux/pm_runtime.h>
#include <linux/of_device.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/dmaengine_pcm.h>

#include "omap-mcpdm.h"
#include "sdma-pcm.h"

struct mcpdm_link_config {
	u32 link_mask; /* channel mask for the direction */
	u32 threshold; /* FIFO threshold */
};

struct omap_mcpdm {
	struct device *dev;
	unsigned long phys_base;
	void __iomem *io_base;
	int irq;

	struct mutex mutex;

	/* Playback/Capture configuration */
	struct mcpdm_link_config config[2];

	/* McPDM dn offsets for rx1, and 2 channels */
	u32 dn_rx_offset;

	/* McPDM needs to be restarted due to runtime reconfiguration */
	bool restart;

	/* pm state for suspend/resume handling */
	int pm_active_count;

	struct snd_dmaengine_dai_dma_data dma_data[2];
};

/*
 * Stream DMA parameters
 */

static inline void omap_mcpdm_write(struct omap_mcpdm *mcpdm, u16 reg, u32 val)
{
	writel_relaxed(val, mcpdm->io_base + reg);
}

static inline int omap_mcpdm_read(struct omap_mcpdm *mcpdm, u16 reg)
{
	return readl_relaxed(mcpdm->io_base + reg);
}

#ifdef DEBUG
static void omap_mcpdm_reg_dump(struct omap_mcpdm *mcpdm)
{
	dev_dbg(mcpdm->dev, "***********************\n");
	dev_dbg(mcpdm->dev, "IRQSTATUS_RAW:  0x%04x\n",
			omap_mcpdm_read(mcpdm, MCPDM_REG_IRQSTATUS_RAW));
	dev_dbg(mcpdm->dev, "IRQSTATUS:  0x%04x\n",
			omap_mcpdm_read(mcpdm, MCPDM_REG_IRQSTATUS));
	dev_dbg(mcpdm->dev, "IRQENABLE_SET:  0x%04x\n",
			omap_mcpdm_read(mcpdm, MCPDM_REG_IRQENABLE_SET));
	dev_dbg(mcpdm->dev, "IRQENABLE_CLR:  0x%04x\n",
			omap_mcpdm_read(mcpdm, MCPDM_REG_IRQENABLE_CLR));
	dev_dbg(mcpdm->dev, "IRQWAKE_EN: 0x%04x\n",
			omap_mcpdm_read(mcpdm, MCPDM_REG_IRQWAKE_EN));
	dev_dbg(mcpdm->dev, "DMAENABLE_SET: 0x%04x\n",
			omap_mcpdm_read(mcpdm, MCPDM_REG_DMAENABLE_SET));
	dev_dbg(mcpdm->dev, "DMAENABLE_CLR:  0x%04x\n",
			omap_mcpdm_read(mcpdm, MCPDM_REG_DMAENABLE_CLR));
	dev_dbg(mcpdm->dev, "DMAWAKEEN:  0x%04x\n",
			omap_mcpdm_read(mcpdm, MCPDM_REG_DMAWAKEEN));
	dev_dbg(mcpdm->dev, "CTRL:  0x%04x\n",
			omap_mcpdm_read(mcpdm, MCPDM_REG_CTRL));
	dev_dbg(mcpdm->dev, "DN_DATA:  0x%04x\n",
			omap_mcpdm_read(mcpdm, MCPDM_REG_DN_DATA));
	dev_dbg(mcpdm->dev, "UP_DATA: 0x%04x\n",
			omap_mcpdm_read(mcpdm, MCPDM_REG_UP_DATA));
	dev_dbg(mcpdm->dev, "FIFO_CTRL_DN: 0x%04x\n",
			omap_mcpdm_read(mcpdm, MCPDM_REG_FIFO_CTRL_DN));
	dev_dbg(mcpdm->dev, "FIFO_CTRL_UP:  0x%04x\n",
			omap_mcpdm_read(mcpdm, MCPDM_REG_FIFO_CTRL_UP));
	dev_dbg(mcpdm->dev, "***********************\n");
}
#else
static void omap_mcpdm_reg_dump(struct omap_mcpdm *mcpdm) {}
#endif

/*
 * Enables the transfer through the PDM interface to/from the Phoenix
 * codec by enabling the corresponding UP or DN channels.
 */
static void omap_mcpdm_start(struct omap_mcpdm *mcpdm)
{
	u32 ctrl = omap_mcpdm_read(mcpdm, MCPDM_REG_CTRL);
	u32 link_mask = mcpdm->config[0].link_mask | mcpdm->config[1].link_mask;

	ctrl |= (MCPDM_SW_DN_RST | MCPDM_SW_UP_RST);
	omap_mcpdm_write(mcpdm, MCPDM_REG_CTRL, ctrl);

	ctrl |= link_mask;
	omap_mcpdm_write(mcpdm, MCPDM_REG_CTRL, ctrl);

	ctrl &= ~(MCPDM_SW_DN_RST | MCPDM_SW_UP_RST);
	omap_mcpdm_write(mcpdm, MCPDM_REG_CTRL, ctrl);
}

/*
 * Disables the transfer through the PDM interface to/from the Phoenix
 * codec by disabling the corresponding UP or DN channels.
 */
static void omap_mcpdm_stop(struct omap_mcpdm *mcpdm)
{
	u32 ctrl = omap_mcpdm_read(mcpdm, MCPDM_REG_CTRL);
	u32 link_mask = MCPDM_PDM_DN_MASK | MCPDM_PDM_UP_MASK;

	ctrl |= (MCPDM_SW_DN_RST | MCPDM_SW_UP_RST);
	omap_mcpdm_write(mcpdm, MCPDM_REG_CTRL, ctrl);

	ctrl &= ~(link_mask);
	omap_mcpdm_write(mcpdm, MCPDM_REG_CTRL, ctrl);

	ctrl &= ~(MCPDM_SW_DN_RST | MCPDM_SW_UP_RST);
	omap_mcpdm_write(mcpdm, MCPDM_REG_CTRL, ctrl);

}

/*
 * Is the physical McPDM interface active.
 */
static inline int omap_mcpdm_active(struct omap_mcpdm *mcpdm)
{
	return omap_mcpdm_read(mcpdm, MCPDM_REG_CTRL) &
					(MCPDM_PDM_DN_MASK | MCPDM_PDM_UP_MASK);
}

/*
 * Configures McPDM uplink, and downlink for audio.
 * This function should be called before omap_mcpdm_start.
 */
static void omap_mcpdm_open_streams(struct omap_mcpdm *mcpdm)
{
	u32 ctrl = omap_mcpdm_read(mcpdm, MCPDM_REG_CTRL);

	omap_mcpdm_write(mcpdm, MCPDM_REG_CTRL, ctrl | MCPDM_WD_EN);

	omap_mcpdm_write(mcpdm, MCPDM_REG_IRQENABLE_SET,
			MCPDM_DN_IRQ_EMPTY | MCPDM_DN_IRQ_FULL |
			MCPDM_UP_IRQ_EMPTY | MCPDM_UP_IRQ_FULL);

	/* Enable DN RX1/2 offset cancellation feature, if configured */
	if (mcpdm->dn_rx_offset) {
		u32 dn_offset = mcpdm->dn_rx_offset;

		omap_mcpdm_write(mcpdm, MCPDM_REG_DN_OFFSET, dn_offset);
		dn_offset |= (MCPDM_DN_OFST_RX1_EN | MCPDM_DN_OFST_RX2_EN);
		omap_mcpdm_write(mcpdm, MCPDM_REG_DN_OFFSET, dn_offset);
	}

	omap_mcpdm_write(mcpdm, MCPDM_REG_FIFO_CTRL_DN,
			 mcpdm->config[SNDRV_PCM_STREAM_PLAYBACK].threshold);
	omap_mcpdm_write(mcpdm, MCPDM_REG_FIFO_CTRL_UP,
			 mcpdm->config[SNDRV_PCM_STREAM_CAPTURE].threshold);

	omap_mcpdm_write(mcpdm, MCPDM_REG_DMAENABLE_SET,
			MCPDM_DMA_DN_ENABLE | MCPDM_DMA_UP_ENABLE);
}

/*
 * Cleans McPDM uplink, and downlink configuration.
 * This function should be called when the stream is closed.
 */
static void omap_mcpdm_close_streams(struct omap_mcpdm *mcpdm)
{
	/* Disable irq request generation for downlink */
	omap_mcpdm_write(mcpdm, MCPDM_REG_IRQENABLE_CLR,
			MCPDM_DN_IRQ_EMPTY | MCPDM_DN_IRQ_FULL);

	/* Disable DMA request generation for downlink */
	omap_mcpdm_write(mcpdm, MCPDM_REG_DMAENABLE_CLR, MCPDM_DMA_DN_ENABLE);

	/* Disable irq request generation for uplink */
	omap_mcpdm_write(mcpdm, MCPDM_REG_IRQENABLE_CLR,
			MCPDM_UP_IRQ_EMPTY | MCPDM_UP_IRQ_FULL);

	/* Disable DMA request generation for uplink */
	omap_mcpdm_write(mcpdm, MCPDM_REG_DMAENABLE_CLR, MCPDM_DMA_UP_ENABLE);

	/* Disable RX1/2 offset cancellation */
	if (mcpdm->dn_rx_offset)
		omap_mcpdm_write(mcpdm, MCPDM_REG_DN_OFFSET, 0);
}

static irqreturn_t omap_mcpdm_irq_handler(int irq, void *dev_id)
{
	struct omap_mcpdm *mcpdm = dev_id;
	int irq_status;

	irq_status = omap_mcpdm_read(mcpdm, MCPDM_REG_IRQSTATUS);

	/* Acknowledge irq event */
	omap_mcpdm_write(mcpdm, MCPDM_REG_IRQSTATUS, irq_status);

	if (irq_status & MCPDM_DN_IRQ_FULL)
		dev_dbg(mcpdm->dev, "DN (playback) FIFO Full\n");

	if (irq_status & MCPDM_DN_IRQ_EMPTY)
		dev_dbg(mcpdm->dev, "DN (playback) FIFO Empty\n");

	if (irq_status & MCPDM_DN_IRQ)
		dev_dbg(mcpdm->dev, "DN (playback) write request\n");

	if (irq_status & MCPDM_UP_IRQ_FULL)
		dev_dbg(mcpdm->dev, "UP (capture) FIFO Full\n");

	if (irq_status & MCPDM_UP_IRQ_EMPTY)
		dev_dbg(mcpdm->dev, "UP (capture) FIFO Empty\n");

	if (irq_status & MCPDM_UP_IRQ)
		dev_dbg(mcpdm->dev, "UP (capture) write request\n");

	return IRQ_HANDLED;
}

static int omap_mcpdm_dai_startup(struct snd_pcm_substream *substream,
				  struct snd_soc_dai *dai)
{
	struct omap_mcpdm *mcpdm = snd_soc_dai_get_drvdata(dai);

	mutex_lock(&mcpdm->mutex);

	if (!dai->active)
		omap_mcpdm_open_streams(mcpdm);

	mutex_unlock(&mcpdm->mutex);

	return 0;
}

static void omap_mcpdm_dai_shutdown(struct snd_pcm_substream *substream,
				  struct snd_soc_dai *dai)
{
	struct omap_mcpdm *mcpdm = snd_soc_dai_get_drvdata(dai);

	mutex_lock(&mcpdm->mutex);

	if (!dai->active) {
		if (omap_mcpdm_active(mcpdm)) {
			omap_mcpdm_stop(mcpdm);
			omap_mcpdm_close_streams(mcpdm);
			mcpdm->config[0].link_mask = 0;
			mcpdm->config[1].link_mask = 0;
		}
	}

	mutex_unlock(&mcpdm->mutex);
}

static int omap_mcpdm_dai_hw_params(struct snd_pcm_substream *substream,
				    struct snd_pcm_hw_params *params,
				    struct snd_soc_dai *dai)
{
	struct omap_mcpdm *mcpdm = snd_soc_dai_get_drvdata(dai);
	int stream = substream->stream;
	struct snd_dmaengine_dai_dma_data *dma_data;
	u32 threshold;
	int channels;
	int link_mask = 0;

	channels = params_channels(params);
	switch (channels) {
	case 5:
		if (stream == SNDRV_PCM_STREAM_CAPTURE)
			/* up to 3 channels for capture */
			return -EINVAL;
		link_mask |= 1 << 4;
		/* fall through */
	case 4:
		if (stream == SNDRV_PCM_STREAM_CAPTURE)
			/* up to 3 channels for capture */
			return -EINVAL;
		link_mask |= 1 << 3;
		/* fall through */
	case 3:
		link_mask |= 1 << 2;
		/* fall through */
	case 2:
		link_mask |= 1 << 1;
		/* fall through */
	case 1:
		link_mask |= 1 << 0;
		break;
	default:
		/* unsupported number of channels */
		return -EINVAL;
	}

	dma_data = snd_soc_dai_get_dma_data(dai, substream);

	threshold = mcpdm->config[stream].threshold;
	/* Configure McPDM channels, and DMA packet size */
	if (stream == SNDRV_PCM_STREAM_PLAYBACK) {
		link_mask <<= 3;

		/* If capture is not running assume a stereo stream to come */
		if (!mcpdm->config[!stream].link_mask)
			mcpdm->config[!stream].link_mask = 0x3;

		dma_data->maxburst =
				(MCPDM_DN_THRES_MAX - threshold) * channels;
	} else {
		/* If playback is not running assume a stereo stream to come */
		if (!mcpdm->config[!stream].link_mask)
			mcpdm->config[!stream].link_mask = (0x3 << 3);

		dma_data->maxburst = threshold * channels;
	}

	/* Check if we need to restart McPDM with this stream */
	if (mcpdm->config[stream].link_mask &&
	    mcpdm->config[stream].link_mask != link_mask)
		mcpdm->restart = true;

	mcpdm->config[stream].link_mask = link_mask;

	return 0;
}

static int omap_mcpdm_prepare(struct snd_pcm_substream *substream,
				  struct snd_soc_dai *dai)
{
	struct omap_mcpdm *mcpdm = snd_soc_dai_get_drvdata(dai);

	if (!omap_mcpdm_active(mcpdm)) {
		omap_mcpdm_start(mcpdm);
		omap_mcpdm_reg_dump(mcpdm);
	} else if (mcpdm->restart) {
		omap_mcpdm_stop(mcpdm);
		omap_mcpdm_start(mcpdm);
		mcpdm->restart = false;
		omap_mcpdm_reg_dump(mcpdm);
	}

	return 0;
}

static const struct snd_soc_dai_ops omap_mcpdm_dai_ops = {
	.startup	= omap_mcpdm_dai_startup,
	.shutdown	= omap_mcpdm_dai_shutdown,
	.hw_params	= omap_mcpdm_dai_hw_params,
	.prepare	= omap_mcpdm_prepare,
};

static int omap_mcpdm_probe(struct snd_soc_dai *dai)
{
	struct omap_mcpdm *mcpdm = snd_soc_dai_get_drvdata(dai);
	int ret;

	pm_runtime_enable(mcpdm->dev);

	/* Disable lines while request is ongoing */
	pm_runtime_get_sync(mcpdm->dev);
	omap_mcpdm_write(mcpdm, MCPDM_REG_CTRL, 0x00);

	ret = request_irq(mcpdm->irq, omap_mcpdm_irq_handler, 0, "McPDM",
			  (void *)mcpdm);

	pm_runtime_put_sync(mcpdm->dev);

	if (ret) {
		dev_err(mcpdm->dev, "Request for IRQ failed\n");
		pm_runtime_disable(mcpdm->dev);
	}

	/* Configure McPDM threshold values */
	mcpdm->config[SNDRV_PCM_STREAM_PLAYBACK].threshold = 2;
	mcpdm->config[SNDRV_PCM_STREAM_CAPTURE].threshold =
							MCPDM_UP_THRES_MAX - 3;

	snd_soc_dai_init_dma_data(dai,
				  &mcpdm->dma_data[SNDRV_PCM_STREAM_PLAYBACK],
				  &mcpdm->dma_data[SNDRV_PCM_STREAM_CAPTURE]);

	return ret;
}

static int omap_mcpdm_remove(struct snd_soc_dai *dai)
{
	struct omap_mcpdm *mcpdm = snd_soc_dai_get_drvdata(dai);

	free_irq(mcpdm->irq, (void *)mcpdm);
	pm_runtime_disable(mcpdm->dev);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int omap_mcpdm_suspend(struct snd_soc_dai *dai)
{
	struct omap_mcpdm *mcpdm = snd_soc_dai_get_drvdata(dai);

	if (dai->active) {
		omap_mcpdm_stop(mcpdm);
		omap_mcpdm_close_streams(mcpdm);
	}

	mcpdm->pm_active_count = 0;
	while (pm_runtime_active(mcpdm->dev)) {
		pm_runtime_put_sync(mcpdm->dev);
		mcpdm->pm_active_count++;
	}

	return 0;
}

static int omap_mcpdm_resume(struct snd_soc_dai *dai)
{
	struct omap_mcpdm *mcpdm = snd_soc_dai_get_drvdata(dai);

	if (mcpdm->pm_active_count) {
		while (mcpdm->pm_active_count--)
			pm_runtime_get_sync(mcpdm->dev);

		if (dai->active) {
			omap_mcpdm_open_streams(mcpdm);
			omap_mcpdm_start(mcpdm);
		}
	}


	return 0;
}
#else
#define omap_mcpdm_suspend NULL
#define omap_mcpdm_resume NULL
#endif

#define OMAP_MCPDM_RATES	(SNDRV_PCM_RATE_88200 | SNDRV_PCM_RATE_96000)
#define OMAP_MCPDM_FORMATS	SNDRV_PCM_FMTBIT_S32_LE

static struct snd_soc_dai_driver omap_mcpdm_dai = {
	.probe = omap_mcpdm_probe,
	.remove = omap_mcpdm_remove,
	.suspend = omap_mcpdm_suspend,
	.resume = omap_mcpdm_resume,
	.probe_order = SND_SOC_COMP_ORDER_LATE,
	.remove_order = SND_SOC_COMP_ORDER_EARLY,
	.playback = {
		.channels_min = 1,
		.channels_max = 5,
		.rates = OMAP_MCPDM_RATES,
		.formats = OMAP_MCPDM_FORMATS,
		.sig_bits = 24,
	},
	.capture = {
		.channels_min = 1,
		.channels_max = 3,
		.rates = OMAP_MCPDM_RATES,
		.formats = OMAP_MCPDM_FORMATS,
		.sig_bits = 24,
	},
	.ops = &omap_mcpdm_dai_ops,
};

static const struct snd_soc_component_driver omap_mcpdm_component = {
	.name		= "omap-mcpdm",
};

void omap_mcpdm_configure_dn_offsets(struct snd_soc_pcm_runtime *rtd,
				    u8 rx1, u8 rx2)
{
	struct omap_mcpdm *mcpdm = snd_soc_dai_get_drvdata(rtd->cpu_dai);

	mcpdm->dn_rx_offset = MCPDM_DNOFST_RX1(rx1) | MCPDM_DNOFST_RX2(rx2);
}
EXPORT_SYMBOL_GPL(omap_mcpdm_configure_dn_offsets);

static int asoc_mcpdm_probe(struct platform_device *pdev)
{
	struct omap_mcpdm *mcpdm;
	struct resource *res;
	int ret;

	mcpdm = devm_kzalloc(&pdev->dev, sizeof(struct omap_mcpdm), GFP_KERNEL);
	if (!mcpdm)
		return -ENOMEM;

	platform_set_drvdata(pdev, mcpdm);

	mutex_init(&mcpdm->mutex);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "dma");
	if (res == NULL)
		return -ENOMEM;

	mcpdm->dma_data[0].addr = res->start + MCPDM_REG_DN_DATA;
	mcpdm->dma_data[1].addr = res->start + MCPDM_REG_UP_DATA;

	mcpdm->dma_data[0].filter_data = "dn_link";
	mcpdm->dma_data[1].filter_data = "up_link";

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "mpu");
	mcpdm->io_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(mcpdm->io_base))
		return PTR_ERR(mcpdm->io_base);

	mcpdm->irq = platform_get_irq(pdev, 0);
	if (mcpdm->irq < 0)
		return mcpdm->irq;

	mcpdm->dev = &pdev->dev;

	ret =  devm_snd_soc_register_component(&pdev->dev,
					       &omap_mcpdm_component,
					       &omap_mcpdm_dai, 1);
	if (ret)
		return ret;

	return sdma_pcm_platform_register(&pdev->dev, "dn_link", "up_link");
}

static const struct of_device_id omap_mcpdm_of_match[] = {
	{ .compatible = "ti,omap4-mcpdm", },
	{ }
};
MODULE_DEVICE_TABLE(of, omap_mcpdm_of_match);

static struct platform_driver asoc_mcpdm_driver = {
	.driver = {
		.name	= "omap-mcpdm",
		.of_match_table = omap_mcpdm_of_match,
	},

	.probe	= asoc_mcpdm_probe,
};

module_platform_driver(asoc_mcpdm_driver);

MODULE_ALIAS("platform:omap-mcpdm");
MODULE_AUTHOR("Misael Lopez Cruz <misael.lopez@ti.com>");
MODULE_DESCRIPTION("OMAP PDM SoC Interface");
MODULE_LICENSE("GPL");
