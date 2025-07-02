// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license. When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2023 Advanced Micro Devices, Inc.
//
// Authors: Syed Saba Kareem <Syed.SabaKareem@amd.com>
//

/*
 * Common file to be used by amd platforms
 */

#include "amd.h"
#include <linux/acpi.h>
#include <linux/pci.h>
#include <linux/export.h>

#include "../mach-config.h"

#define ACP_RENOIR_PDM_ADDR	0x02
#define ACP_REMBRANDT_PDM_ADDR	0x03
#define ACP63_PDM_ADDR		0x02
#define ACP70_PDM_ADDR		0x02

struct acp_resource rn_rsrc = {
	.offset = 20,
	.no_of_ctrls = 1,
	.irqp_used = 0,
	.irq_reg_offset = 0x1800,
	.scratch_reg_offset = 0x12800,
	.sram_pte_offset = 0x02052800,
};
EXPORT_SYMBOL_NS_GPL(rn_rsrc, "SND_SOC_ACP_COMMON");

struct acp_resource rmb_rsrc = {
	.offset = 0,
	.no_of_ctrls = 2,
	.irqp_used = 1,
	.soc_mclk = true,
	.irq_reg_offset = 0x1a00,
	.scratch_reg_offset = 0x12800,
	.sram_pte_offset = 0x03802800,
};
EXPORT_SYMBOL_NS_GPL(rmb_rsrc, "SND_SOC_ACP_COMMON");

struct acp_resource acp63_rsrc = {
	.offset = 0,
	.no_of_ctrls = 2,
	.irqp_used = 1,
	.soc_mclk = true,
	.irq_reg_offset = 0x1a00,
	.scratch_reg_offset = 0x12800,
	.sram_pte_offset = 0x03802800,
};
EXPORT_SYMBOL_NS_GPL(acp63_rsrc, "SND_SOC_ACP_COMMON");

struct acp_resource acp70_rsrc = {
	.offset = 0,
	.no_of_ctrls = 2,
	.irqp_used = 1,
	.soc_mclk = true,
	.irq_reg_offset = 0x1a00,
	.scratch_reg_offset = 0x10000,
	.sram_pte_offset = 0x03800000,
};
EXPORT_SYMBOL_NS_GPL(acp70_rsrc, "SND_SOC_ACP_COMMON");

static const struct snd_acp_hw_ops acp_common_hw_ops = {
	/* ACP hardware initilizations */
	.acp_init = acp_init,
	.acp_deinit = acp_deinit,

	/* ACP Interrupts*/
	.irq = acp_irq_handler,
	.en_interrupts = acp_enable_interrupts,
	.dis_interrupts = acp_disable_interrupts,
};

irqreturn_t acp_irq_handler(int irq, void *data)
{
	struct acp_chip_info *chip = data;
	struct acp_resource *rsrc = chip->rsrc;
	struct acp_stream *stream;
	u16 i2s_flag = 0;
	u32 ext_intr_stat, ext_intr_stat1;

	if (rsrc->no_of_ctrls == 2)
		ext_intr_stat1 = readl(ACP_EXTERNAL_INTR_STAT(chip, (rsrc->irqp_used - 1)));

	ext_intr_stat = readl(ACP_EXTERNAL_INTR_STAT(chip, rsrc->irqp_used));

	spin_lock(&chip->acp_lock);
	list_for_each_entry(stream, &chip->stream_list, list) {
		if (ext_intr_stat & stream->irq_bit) {
			writel(stream->irq_bit,
			       ACP_EXTERNAL_INTR_STAT(chip, rsrc->irqp_used));
			snd_pcm_period_elapsed(stream->substream);
			i2s_flag = 1;
		}
		if (chip->rsrc->no_of_ctrls == 2) {
			if (ext_intr_stat1 & stream->irq_bit) {
				writel(stream->irq_bit, ACP_EXTERNAL_INTR_STAT(chip,
				       (rsrc->irqp_used - 1)));
				snd_pcm_period_elapsed(stream->substream);
				i2s_flag = 1;
			}
		}
	}
	spin_unlock(&chip->acp_lock);
	if (i2s_flag)
		return IRQ_HANDLED;

	return IRQ_NONE;
}

int acp_enable_interrupts(struct acp_chip_info *chip)
{
	struct acp_resource *rsrc;
	u32 ext_intr_ctrl;

	rsrc = chip->rsrc;
	writel(0x01, ACP_EXTERNAL_INTR_ENB(chip));
	ext_intr_ctrl = readl(ACP_EXTERNAL_INTR_CNTL(chip, rsrc->irqp_used));
	ext_intr_ctrl |= ACP_ERROR_MASK;
	writel(ext_intr_ctrl, ACP_EXTERNAL_INTR_CNTL(chip, rsrc->irqp_used));

	return 0;
}
EXPORT_SYMBOL_NS_GPL(acp_enable_interrupts, "SND_SOC_ACP_COMMON");

int acp_disable_interrupts(struct acp_chip_info *chip)
{
	struct acp_resource *rsrc;

	rsrc = chip->rsrc;
	writel(ACP_EXT_INTR_STAT_CLEAR_MASK, ACP_EXTERNAL_INTR_STAT(chip, rsrc->irqp_used));
	writel(0x00, ACP_EXTERNAL_INTR_ENB(chip));

	return 0;
}
EXPORT_SYMBOL_NS_GPL(acp_disable_interrupts, "SND_SOC_ACP_COMMON");

static void set_acp_pdm_ring_buffer(struct snd_pcm_substream *substream,
				    struct snd_soc_dai *dai)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct acp_stream *stream = runtime->private_data;
	struct device *dev = dai->component->dev;
	struct acp_chip_info *chip = dev_get_platdata(dev);

	u32 physical_addr, pdm_size, period_bytes;

	period_bytes = frames_to_bytes(runtime, runtime->period_size);
	pdm_size = frames_to_bytes(runtime, runtime->buffer_size);
	physical_addr = stream->reg_offset + MEM_WINDOW_START;

	/* Init ACP PDM Ring buffer */
	writel(physical_addr, chip->base + ACP_WOV_RX_RINGBUFADDR);
	writel(pdm_size, chip->base + ACP_WOV_RX_RINGBUFSIZE);
	writel(period_bytes, chip->base + ACP_WOV_RX_INTR_WATERMARK_SIZE);
	writel(0x01, chip->base + ACPAXI2AXI_ATU_CTRL);
}

static void set_acp_pdm_clk(struct snd_pcm_substream *substream,
			    struct snd_soc_dai *dai)
{
	struct device *dev = dai->component->dev;
	struct acp_chip_info *chip = dev_get_platdata(dev);
	unsigned int pdm_ctrl;

	/* Enable default ACP PDM clk */
	writel(PDM_CLK_FREQ_MASK, chip->base + ACP_WOV_CLK_CTRL);
	pdm_ctrl = readl(chip->base + ACP_WOV_MISC_CTRL);
	pdm_ctrl |= PDM_MISC_CTRL_MASK;
	writel(pdm_ctrl, chip->base + ACP_WOV_MISC_CTRL);
	set_acp_pdm_ring_buffer(substream, dai);
}

void restore_acp_pdm_params(struct snd_pcm_substream *substream,
			    struct acp_chip_info *chip)
{
	struct snd_soc_dai *dai;
	struct snd_soc_pcm_runtime *soc_runtime;
	u32 ext_int_ctrl;

	soc_runtime = snd_soc_substream_to_rtd(substream);
	dai = snd_soc_rtd_to_cpu(soc_runtime, 0);

	/* Programming channel mask and sampling rate */
	writel(chip->ch_mask, chip->base + ACP_WOV_PDM_NO_OF_CHANNELS);
	writel(PDM_DEC_64, chip->base + ACP_WOV_PDM_DECIMATION_FACTOR);

	/* Enabling ACP Pdm interuppts */
	ext_int_ctrl = readl(ACP_EXTERNAL_INTR_CNTL(chip, 0));
	ext_int_ctrl |= PDM_DMA_INTR_MASK;
	writel(ext_int_ctrl, ACP_EXTERNAL_INTR_CNTL(chip, 0));
	set_acp_pdm_clk(substream, dai);
}
EXPORT_SYMBOL_NS_GPL(restore_acp_pdm_params, "SND_SOC_ACP_COMMON");

static int set_acp_i2s_dma_fifo(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct device *dev = dai->component->dev;
	struct acp_chip_info *chip = dev_get_platdata(dev);
	struct acp_resource *rsrc = chip->rsrc;
	struct acp_stream *stream = substream->runtime->private_data;
	u32 reg_dma_size, reg_fifo_size, reg_fifo_addr;
	u32 phy_addr, acp_fifo_addr, ext_int_ctrl;
	unsigned int dir = substream->stream;

	switch (dai->driver->id) {
	case I2S_SP_INSTANCE:
		if (dir == SNDRV_PCM_STREAM_PLAYBACK) {
			reg_dma_size = ACP_I2S_TX_DMA_SIZE(chip);
			acp_fifo_addr = rsrc->sram_pte_offset +
					SP_PB_FIFO_ADDR_OFFSET;
			reg_fifo_addr = ACP_I2S_TX_FIFOADDR(chip);
			reg_fifo_size = ACP_I2S_TX_FIFOSIZE(chip);
			phy_addr = I2S_SP_TX_MEM_WINDOW_START + stream->reg_offset;
			writel(phy_addr, chip->base + ACP_I2S_TX_RINGBUFADDR(chip));
		} else {
			reg_dma_size = ACP_I2S_RX_DMA_SIZE(chip);
			acp_fifo_addr = rsrc->sram_pte_offset +
					SP_CAPT_FIFO_ADDR_OFFSET;
			reg_fifo_addr = ACP_I2S_RX_FIFOADDR(chip);
			reg_fifo_size = ACP_I2S_RX_FIFOSIZE(chip);
			phy_addr = I2S_SP_RX_MEM_WINDOW_START + stream->reg_offset;
			writel(phy_addr, chip->base + ACP_I2S_RX_RINGBUFADDR(chip));
		}
		break;
	case I2S_BT_INSTANCE:
		if (dir == SNDRV_PCM_STREAM_PLAYBACK) {
			reg_dma_size = ACP_BT_TX_DMA_SIZE(chip);
			acp_fifo_addr = rsrc->sram_pte_offset +
					BT_PB_FIFO_ADDR_OFFSET;
			reg_fifo_addr = ACP_BT_TX_FIFOADDR(chip);
			reg_fifo_size = ACP_BT_TX_FIFOSIZE(chip);
			phy_addr = I2S_BT_TX_MEM_WINDOW_START + stream->reg_offset;
			writel(phy_addr, chip->base + ACP_BT_TX_RINGBUFADDR(chip));
		} else {
			reg_dma_size = ACP_BT_RX_DMA_SIZE(chip);
			acp_fifo_addr = rsrc->sram_pte_offset +
					BT_CAPT_FIFO_ADDR_OFFSET;
			reg_fifo_addr = ACP_BT_RX_FIFOADDR(chip);
			reg_fifo_size = ACP_BT_RX_FIFOSIZE(chip);
			phy_addr = I2S_BT_TX_MEM_WINDOW_START + stream->reg_offset;
			writel(phy_addr, chip->base + ACP_BT_RX_RINGBUFADDR(chip));
		}
		break;
	case I2S_HS_INSTANCE:
		if (dir == SNDRV_PCM_STREAM_PLAYBACK) {
			reg_dma_size = ACP_HS_TX_DMA_SIZE;
			acp_fifo_addr = rsrc->sram_pte_offset +
					HS_PB_FIFO_ADDR_OFFSET;
			reg_fifo_addr = ACP_HS_TX_FIFOADDR;
			reg_fifo_size = ACP_HS_TX_FIFOSIZE;
			phy_addr = I2S_HS_TX_MEM_WINDOW_START + stream->reg_offset;
			writel(phy_addr, chip->base + ACP_HS_TX_RINGBUFADDR);
		} else {
			reg_dma_size = ACP_HS_RX_DMA_SIZE;
			acp_fifo_addr = rsrc->sram_pte_offset +
					HS_CAPT_FIFO_ADDR_OFFSET;
			reg_fifo_addr = ACP_HS_RX_FIFOADDR;
			reg_fifo_size = ACP_HS_RX_FIFOSIZE;
			phy_addr = I2S_HS_RX_MEM_WINDOW_START + stream->reg_offset;
			writel(phy_addr, chip->base + ACP_HS_RX_RINGBUFADDR);
		}
		break;
	default:
		dev_err(dev, "Invalid dai id %x\n", dai->driver->id);
		return -EINVAL;
	}

	writel(DMA_SIZE, chip->base + reg_dma_size);
	writel(acp_fifo_addr, chip->base + reg_fifo_addr);
	writel(FIFO_SIZE, chip->base + reg_fifo_size);

	ext_int_ctrl = readl(ACP_EXTERNAL_INTR_CNTL(chip, rsrc->irqp_used));
	ext_int_ctrl |= BIT(I2S_RX_THRESHOLD(rsrc->offset)) |
			BIT(BT_RX_THRESHOLD(rsrc->offset)) |
			BIT(I2S_TX_THRESHOLD(rsrc->offset)) |
			BIT(BT_TX_THRESHOLD(rsrc->offset)) |
			BIT(HS_RX_THRESHOLD(rsrc->offset)) |
			BIT(HS_TX_THRESHOLD(rsrc->offset));

	writel(ext_int_ctrl, ACP_EXTERNAL_INTR_CNTL(chip, rsrc->irqp_used));
	return 0;
}

int restore_acp_i2s_params(struct snd_pcm_substream *substream,
			   struct acp_chip_info *chip,
			   struct acp_stream *stream)
{
	struct snd_soc_dai *dai;
	struct snd_soc_pcm_runtime *soc_runtime;
	u32 tdm_fmt, reg_val, fmt_reg, val;

	soc_runtime = snd_soc_substream_to_rtd(substream);
	dai = snd_soc_rtd_to_cpu(soc_runtime, 0);
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		tdm_fmt = chip->tdm_tx_fmt[stream->dai_id - 1];
		switch (stream->dai_id) {
		case I2S_BT_INSTANCE:
			reg_val = ACP_BTTDM_ITER;
			fmt_reg = ACP_BTTDM_TXFRMT;
			break;
		case I2S_SP_INSTANCE:
			reg_val = ACP_I2STDM_ITER;
			fmt_reg = ACP_I2STDM_TXFRMT;
			break;
		case I2S_HS_INSTANCE:
			reg_val = ACP_HSTDM_ITER;
			fmt_reg = ACP_HSTDM_TXFRMT;
			break;
		default:
			pr_err("Invalid dai id %x\n", stream->dai_id);
			return -EINVAL;
		}
		val = chip->xfer_tx_resolution[stream->dai_id - 1] << 3;
	} else {
		tdm_fmt = chip->tdm_rx_fmt[stream->dai_id - 1];
		switch (stream->dai_id) {
		case I2S_BT_INSTANCE:
			reg_val = ACP_BTTDM_IRER;
			fmt_reg = ACP_BTTDM_RXFRMT;
			break;
		case I2S_SP_INSTANCE:
			reg_val = ACP_I2STDM_IRER;
			fmt_reg = ACP_I2STDM_RXFRMT;
			break;
		case I2S_HS_INSTANCE:
			reg_val = ACP_HSTDM_IRER;
			fmt_reg = ACP_HSTDM_RXFRMT;
			break;
		default:
			pr_err("Invalid dai id %x\n", stream->dai_id);
			return -EINVAL;
		}
		val = chip->xfer_rx_resolution[stream->dai_id - 1] << 3;
	}
	writel(val, chip->base + reg_val);
	if (chip->tdm_mode == TDM_ENABLE) {
		writel(tdm_fmt, chip->base + fmt_reg);
		val = readl(chip->base + reg_val);
		writel(val | 0x2, chip->base + reg_val);
	}
	return set_acp_i2s_dma_fifo(substream, dai);
}
EXPORT_SYMBOL_NS_GPL(restore_acp_i2s_params, "SND_SOC_ACP_COMMON");

static int acp_power_on(struct acp_chip_info *chip)
{
	u32 val, acp_pgfsm_stat_reg, acp_pgfsm_ctrl_reg;
	void __iomem *base;

	base = chip->base;
	switch (chip->acp_rev) {
	case ACP_RN_PCI_ID:
		acp_pgfsm_stat_reg = ACP_PGFSM_STATUS;
		acp_pgfsm_ctrl_reg = ACP_PGFSM_CONTROL;
		break;
	case ACP_RMB_PCI_ID:
		acp_pgfsm_stat_reg = ACP6X_PGFSM_STATUS;
		acp_pgfsm_ctrl_reg = ACP6X_PGFSM_CONTROL;
		break;
	case ACP63_PCI_ID:
		acp_pgfsm_stat_reg = ACP63_PGFSM_STATUS;
		acp_pgfsm_ctrl_reg = ACP63_PGFSM_CONTROL;
		break;
	case ACP70_PCI_ID:
	case ACP71_PCI_ID:
	case ACP72_PCI_ID:
		acp_pgfsm_stat_reg = ACP70_PGFSM_STATUS;
		acp_pgfsm_ctrl_reg = ACP70_PGFSM_CONTROL;
		break;
	default:
		return -EINVAL;
	}

	val = readl(base + acp_pgfsm_stat_reg);
	if (val == ACP_POWERED_ON)
		return 0;

	if ((val & ACP_PGFSM_STATUS_MASK) != ACP_POWER_ON_IN_PROGRESS)
		writel(ACP_PGFSM_CNTL_POWER_ON_MASK, base + acp_pgfsm_ctrl_reg);

	return readl_poll_timeout(base + acp_pgfsm_stat_reg, val,
				  !val, DELAY_US, ACP_TIMEOUT);
}

static int acp_reset(void __iomem *base)
{
	u32 val;
	int ret;

	writel(1, base + ACP_SOFT_RESET);
	ret = readl_poll_timeout(base + ACP_SOFT_RESET, val, val & ACP_SOFT_RST_DONE_MASK,
				 DELAY_US, ACP_TIMEOUT);
	if (ret)
		return ret;

	writel(0, base + ACP_SOFT_RESET);
	return readl_poll_timeout(base + ACP_SOFT_RESET, val, !val, DELAY_US, ACP_TIMEOUT);
}

int acp_init(struct acp_chip_info *chip)
{
	int ret;

	/* power on */
	ret = acp_power_on(chip);
	if (ret) {
		pr_err("ACP power on failed\n");
		return ret;
	}
	writel(0x01, chip->base + ACP_CONTROL);

	/* Reset */
	ret = acp_reset(chip->base);
	if (ret) {
		pr_err("ACP reset failed\n");
		return ret;
	}
	if (chip->acp_rev >= ACP70_PCI_ID)
		writel(0, chip->base + ACP_ZSC_DSP_CTRL);
	return 0;
}
EXPORT_SYMBOL_NS_GPL(acp_init, "SND_SOC_ACP_COMMON");

int acp_deinit(struct acp_chip_info *chip)
{
	int ret;

	/* Reset */
	ret = acp_reset(chip->base);
	if (ret)
		return ret;

	if (chip->acp_rev < ACP70_PCI_ID)
		writel(0, chip->base + ACP_CONTROL);
	else
		writel(0x01, chip->base + ACP_ZSC_DSP_CTRL);
	return 0;
}
EXPORT_SYMBOL_NS_GPL(acp_deinit, "SND_SOC_ACP_COMMON");
int acp_machine_select(struct acp_chip_info *chip)
{
	struct snd_soc_acpi_mach *mach;
	int size, platform;

	if (chip->flag == FLAG_AMD_LEGACY_ONLY_DMIC && chip->is_pdm_dev) {
		platform = chip->acp_rev;
		chip->mach_dev = platform_device_register_data(chip->dev, "acp-pdm-mach",
							       PLATFORM_DEVID_NONE, &platform,
							       sizeof(platform));
	} else {
		size = sizeof(*chip->machines);
		mach = snd_soc_acpi_find_machine(chip->machines);
		if (!mach) {
			dev_err(chip->dev, "warning: No matching ASoC machine driver found\n");
			return -EINVAL;
		}
		mach->mach_params.subsystem_rev = chip->acp_rev;
		chip->mach_dev = platform_device_register_data(chip->dev, mach->drv_name,
							       PLATFORM_DEVID_NONE, mach, size);
	}
	if (IS_ERR(chip->mach_dev))
		dev_warn(chip->dev, "Unable to register Machine device\n");
	return 0;
}
EXPORT_SYMBOL_NS_GPL(acp_machine_select, "SND_SOC_ACP_COMMON");

static void check_acp3x_config(struct acp_chip_info *chip)
{
	u32 val;

	val = readl(chip->base + ACP3X_PIN_CONFIG);
	switch (val) {
	case ACP_CONFIG_4:
		chip->is_i2s_config = true;
		chip->is_pdm_config = true;
		break;
	default:
		chip->is_pdm_config = true;
		break;
	}
}

static void check_acp6x_config(struct acp_chip_info *chip)
{
	u32 val;

	val = readl(chip->base + ACP_PIN_CONFIG);
	switch (val) {
	case ACP_CONFIG_4:
	case ACP_CONFIG_5:
	case ACP_CONFIG_6:
	case ACP_CONFIG_7:
	case ACP_CONFIG_8:
	case ACP_CONFIG_11:
	case ACP_CONFIG_14:
		chip->is_pdm_config = true;
		break;
	case ACP_CONFIG_9:
		chip->is_i2s_config = true;
		break;
	case ACP_CONFIG_10:
	case ACP_CONFIG_12:
	case ACP_CONFIG_13:
		chip->is_i2s_config = true;
		chip->is_pdm_config = true;
		break;
	default:
		break;
	}
}

static void check_acp70_config(struct acp_chip_info *chip)
{
	u32 val;

	val = readl(chip->base + ACP_PIN_CONFIG);
	switch (val) {
	case ACP_CONFIG_4:
	case ACP_CONFIG_5:
	case ACP_CONFIG_6:
	case ACP_CONFIG_7:
	case ACP_CONFIG_8:
	case ACP_CONFIG_11:
	case ACP_CONFIG_14:
	case ACP_CONFIG_17:
	case ACP_CONFIG_18:
		chip->is_pdm_config = true;
		break;
	case ACP_CONFIG_9:
		chip->is_i2s_config = true;
		break;
	case ACP_CONFIG_10:
	case ACP_CONFIG_12:
	case ACP_CONFIG_13:
	case ACP_CONFIG_19:
	case ACP_CONFIG_20:
		chip->is_i2s_config = true;
		chip->is_pdm_config = true;
		break;
	default:
		break;
	}
}

void check_acp_config(struct pci_dev *pci, struct acp_chip_info *chip)
{
	struct acpi_device *pdm_dev;
	const union acpi_object *obj;
	acpi_handle handle;
	acpi_integer dmic_status;
	u32 pdm_addr, ret;

	switch (chip->acp_rev) {
	case ACP_RN_PCI_ID:
		pdm_addr = ACP_RENOIR_PDM_ADDR;
		check_acp3x_config(chip);
		break;
	case ACP_RMB_PCI_ID:
		pdm_addr = ACP_REMBRANDT_PDM_ADDR;
		check_acp6x_config(chip);
		break;
	case ACP63_PCI_ID:
		pdm_addr = ACP63_PDM_ADDR;
		check_acp6x_config(chip);
		break;
	case ACP70_PCI_ID:
	case ACP71_PCI_ID:
	case ACP72_PCI_ID:
		pdm_addr = ACP70_PDM_ADDR;
		check_acp70_config(chip);
		break;
	default:
		break;
	}

	if (chip->is_pdm_config) {
		pdm_dev = acpi_find_child_device(ACPI_COMPANION(&pci->dev), pdm_addr, 0);
		if (pdm_dev) {
			if (!acpi_dev_get_property(pdm_dev, "acp-audio-device-type",
						   ACPI_TYPE_INTEGER, &obj) &&
						   obj->integer.value == pdm_addr)
				chip->is_pdm_dev = true;
		}

		handle = ACPI_HANDLE(&pci->dev);
		ret = acpi_evaluate_integer(handle, "_WOV", NULL, &dmic_status);
		if (!ACPI_FAILURE(ret))
			chip->is_pdm_dev = dmic_status;
	}
}
EXPORT_SYMBOL_NS_GPL(check_acp_config, "SND_SOC_ACP_COMMON");

struct snd_acp_hw_ops acp31_common_hw_ops;
EXPORT_SYMBOL_NS_GPL(acp31_common_hw_ops, "SND_SOC_ACP_COMMON");
int acp31_hw_ops_init(struct acp_chip_info *chip)
{
	memcpy(&acp31_common_hw_ops, &acp_common_hw_ops, sizeof(acp_common_hw_ops));
	chip->acp_hw_ops = &acp31_common_hw_ops;

	return 0;
}
EXPORT_SYMBOL_NS_GPL(acp31_hw_ops_init, "SND_SOC_ACP_COMMON");

struct snd_acp_hw_ops acp6x_common_hw_ops;
EXPORT_SYMBOL_NS_GPL(acp6x_common_hw_ops, "SND_SOC_ACP_COMMON");
int acp6x_hw_ops_init(struct acp_chip_info *chip)
{
	memcpy(&acp6x_common_hw_ops, &acp_common_hw_ops, sizeof(acp_common_hw_ops));
	chip->acp_hw_ops = &acp6x_common_hw_ops;

	return 0;
}
EXPORT_SYMBOL_NS_GPL(acp6x_hw_ops_init, "SND_SOC_ACP_COMMON");

struct snd_acp_hw_ops acp63_common_hw_ops;
EXPORT_SYMBOL_NS_GPL(acp63_common_hw_ops, "SND_SOC_ACP_COMMON");
int acp63_hw_ops_init(struct acp_chip_info *chip)
{
	memcpy(&acp63_common_hw_ops, &acp_common_hw_ops, sizeof(acp_common_hw_ops));
	chip->acp_hw_ops = &acp63_common_hw_ops;

	return 0;
}
EXPORT_SYMBOL_NS_GPL(acp63_hw_ops_init, "SND_SOC_ACP_COMMON");

struct snd_acp_hw_ops acp70_common_hw_ops;
EXPORT_SYMBOL_NS_GPL(acp70_common_hw_ops, "SND_SOC_ACP_COMMON");
int acp70_hw_ops_init(struct acp_chip_info *chip)
{
	memcpy(&acp70_common_hw_ops, &acp_common_hw_ops, sizeof(acp_common_hw_ops));
	chip->acp_hw_ops = &acp70_common_hw_ops;

	return 0;
}
EXPORT_SYMBOL_NS_GPL(acp70_hw_ops_init, "SND_SOC_ACP_COMMON");

MODULE_DESCRIPTION("AMD ACP legacy common features");
MODULE_LICENSE("Dual BSD/GPL");
