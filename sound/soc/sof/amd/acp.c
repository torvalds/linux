// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license. When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2021, 2023 Advanced Micro Devices, Inc. All rights reserved.
//
// Authors: Vijendar Mukunda <Vijendar.Mukunda@amd.com>
//	    Ajit Kumar Pandey <AjitKumar.Pandey@amd.com>

/*
 * Hardware interface for generic AMD ACP processor
 */

#include <linux/io.h>
#include <linux/module.h>
#include <linux/pci.h>

#include "../ops.h"
#include "acp.h"
#include "acp-dsp-offset.h"

static bool enable_fw_debug;
module_param(enable_fw_debug, bool, 0444);
MODULE_PARM_DESC(enable_fw_debug, "Enable Firmware debug");

static struct acp_quirk_entry quirk_valve_galileo = {
	.signed_fw_image = true,
	.skip_iram_dram_size_mod = true,
};

const struct dmi_system_id acp_sof_quirk_table[] = {
	{
		/* Steam Deck OLED device */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Valve"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Galileo"),
		},
		.driver_data = &quirk_valve_galileo,
	},
	{}
};
EXPORT_SYMBOL_GPL(acp_sof_quirk_table);

static int smn_write(struct pci_dev *dev, u32 smn_addr, u32 data)
{
	pci_write_config_dword(dev, 0x60, smn_addr);
	pci_write_config_dword(dev, 0x64, data);

	return 0;
}

static int smn_read(struct pci_dev *dev, u32 smn_addr)
{
	u32 data = 0;

	pci_write_config_dword(dev, 0x60, smn_addr);
	pci_read_config_dword(dev, 0x64, &data);

	return data;
}

static void init_dma_descriptor(struct acp_dev_data *adata)
{
	struct snd_sof_dev *sdev = adata->dev;
	const struct sof_amd_acp_desc *desc = get_chip_info(sdev->pdata);
	unsigned int addr;

	addr = desc->sram_pte_offset + sdev->debug_box.offset +
	       offsetof(struct scratch_reg_conf, dma_desc);

	snd_sof_dsp_write(sdev, ACP_DSP_BAR, ACP_DMA_DESC_BASE_ADDR, addr);
	snd_sof_dsp_write(sdev, ACP_DSP_BAR, ACP_DMA_DESC_MAX_NUM_DSCR, ACP_MAX_DESC_CNT);
}

static void configure_dma_descriptor(struct acp_dev_data *adata, unsigned short idx,
				     struct dma_descriptor *dscr_info)
{
	struct snd_sof_dev *sdev = adata->dev;
	unsigned int offset;

	offset = ACP_SCRATCH_REG_0 + sdev->debug_box.offset +
		offsetof(struct scratch_reg_conf, dma_desc) +
		idx * sizeof(struct dma_descriptor);

	snd_sof_dsp_write(sdev, ACP_DSP_BAR, offset, dscr_info->src_addr);
	snd_sof_dsp_write(sdev, ACP_DSP_BAR, offset + 0x4, dscr_info->dest_addr);
	snd_sof_dsp_write(sdev, ACP_DSP_BAR, offset + 0x8, dscr_info->tx_cnt.u32_all);
}

static int config_dma_channel(struct acp_dev_data *adata, unsigned int ch,
			      unsigned int idx, unsigned int dscr_count)
{
	struct snd_sof_dev *sdev = adata->dev;
	unsigned int val, status;
	int ret;

	snd_sof_dsp_write(sdev, ACP_DSP_BAR, ACP_DMA_CNTL_0 + ch * sizeof(u32),
			  ACP_DMA_CH_RST | ACP_DMA_CH_GRACEFUL_RST_EN);

	ret = snd_sof_dsp_read_poll_timeout(sdev, ACP_DSP_BAR, ACP_DMA_CH_RST_STS, val,
					    val & (1 << ch), ACP_REG_POLL_INTERVAL,
					    ACP_REG_POLL_TIMEOUT_US);
	if (ret < 0) {
		status = snd_sof_dsp_read(sdev, ACP_DSP_BAR, ACP_ERROR_STATUS);
		val = snd_sof_dsp_read(sdev, ACP_DSP_BAR, ACP_DMA_ERR_STS_0 + ch * sizeof(u32));

		dev_err(sdev->dev, "ACP_DMA_ERR_STS :0x%x ACP_ERROR_STATUS :0x%x\n", val, status);
		return ret;
	}

	snd_sof_dsp_write(sdev, ACP_DSP_BAR, (ACP_DMA_CNTL_0 + ch * sizeof(u32)), 0);
	snd_sof_dsp_write(sdev, ACP_DSP_BAR, ACP_DMA_DSCR_CNT_0 + ch * sizeof(u32), dscr_count);
	snd_sof_dsp_write(sdev, ACP_DSP_BAR, ACP_DMA_DSCR_STRT_IDX_0 + ch * sizeof(u32), idx);
	snd_sof_dsp_write(sdev, ACP_DSP_BAR, ACP_DMA_PRIO_0 + ch * sizeof(u32), 0);
	snd_sof_dsp_write(sdev, ACP_DSP_BAR, ACP_DMA_CNTL_0 + ch * sizeof(u32), ACP_DMA_CH_RUN);

	return ret;
}

static int acpbus_dma_start(struct acp_dev_data *adata, unsigned int ch,
			    unsigned int dscr_count, struct dma_descriptor *dscr_info)
{
	struct snd_sof_dev *sdev = adata->dev;
	int ret;
	u16 dscr;

	if (!dscr_info || !dscr_count)
		return -EINVAL;

	for (dscr = 0; dscr < dscr_count; dscr++)
		configure_dma_descriptor(adata, dscr, dscr_info++);

	ret = config_dma_channel(adata, ch, 0, dscr_count);
	if (ret < 0)
		dev_err(sdev->dev, "config dma ch failed:%d\n", ret);

	return ret;
}

int configure_and_run_dma(struct acp_dev_data *adata, unsigned int src_addr,
			  unsigned int dest_addr, int dsp_data_size)
{
	struct snd_sof_dev *sdev = adata->dev;
	unsigned int desc_count, index;
	int ret;

	for (desc_count = 0; desc_count < ACP_MAX_DESC && dsp_data_size >= 0;
	     desc_count++, dsp_data_size -= ACP_PAGE_SIZE) {
		adata->dscr_info[desc_count].src_addr = src_addr + desc_count * ACP_PAGE_SIZE;
		adata->dscr_info[desc_count].dest_addr = dest_addr + desc_count * ACP_PAGE_SIZE;
		adata->dscr_info[desc_count].tx_cnt.bits.count = ACP_PAGE_SIZE;
		if (dsp_data_size < ACP_PAGE_SIZE)
			adata->dscr_info[desc_count].tx_cnt.bits.count = dsp_data_size;
	}

	ret = acpbus_dma_start(adata, 0, desc_count, adata->dscr_info);
	if (ret)
		dev_err(sdev->dev, "acpbus_dma_start failed\n");

	/* Clear descriptor array */
	for (index = 0; index < desc_count; index++)
		memset(&adata->dscr_info[index], 0x00, sizeof(struct dma_descriptor));

	return ret;
}

/*
 * psp_mbox_ready- function to poll ready bit of psp mbox
 * @adata: acp device data
 * @ack: bool variable to check ready bit status or psp ack
 */

static int psp_mbox_ready(struct acp_dev_data *adata, bool ack)
{
	struct snd_sof_dev *sdev = adata->dev;
	int ret;
	u32 data;

	ret = read_poll_timeout(smn_read, data, data & MBOX_READY_MASK, MBOX_DELAY_US,
				ACP_PSP_TIMEOUT_US, false, adata->smn_dev, MP0_C2PMSG_114_REG);
	if (!ret)
		return 0;

	dev_err(sdev->dev, "PSP error status %x\n", data & MBOX_STATUS_MASK);

	if (ack)
		return -ETIMEDOUT;

	return -EBUSY;
}

/*
 * psp_send_cmd - function to send psp command over mbox
 * @adata: acp device data
 * @cmd: non zero integer value for command type
 */

static int psp_send_cmd(struct acp_dev_data *adata, int cmd)
{
	struct snd_sof_dev *sdev = adata->dev;
	int ret;
	u32 data;

	if (!cmd)
		return -EINVAL;

	/* Get a non-zero Doorbell value from PSP */
	ret = read_poll_timeout(smn_read, data, data, MBOX_DELAY_US, ACP_PSP_TIMEOUT_US, false,
				adata->smn_dev, MP0_C2PMSG_73_REG);

	if (ret) {
		dev_err(sdev->dev, "Failed to get Doorbell from MBOX %x\n", MP0_C2PMSG_73_REG);
		return ret;
	}

	/* Check if PSP is ready for new command */
	ret = psp_mbox_ready(adata, 0);
	if (ret)
		return ret;

	smn_write(adata->smn_dev, MP0_C2PMSG_114_REG, cmd);

	/* Ring the Doorbell for PSP */
	smn_write(adata->smn_dev, MP0_C2PMSG_73_REG, data);

	/* Check MBOX ready as PSP ack */
	ret = psp_mbox_ready(adata, 1);

	return ret;
}

int configure_and_run_sha_dma(struct acp_dev_data *adata, void *image_addr,
			      unsigned int start_addr, unsigned int dest_addr,
			      unsigned int image_length)
{
	struct snd_sof_dev *sdev = adata->dev;
	const struct sof_amd_acp_desc *desc = get_chip_info(sdev->pdata);
	unsigned int tx_count, fw_qualifier, val;
	int ret;

	if (!image_addr) {
		dev_err(sdev->dev, "SHA DMA image address is NULL\n");
		return -EINVAL;
	}

	val = snd_sof_dsp_read(sdev, ACP_DSP_BAR, ACP_SHA_DMA_CMD);
	if (val & ACP_SHA_RUN) {
		snd_sof_dsp_write(sdev, ACP_DSP_BAR, ACP_SHA_DMA_CMD, ACP_SHA_RESET);
		ret = snd_sof_dsp_read_poll_timeout(sdev, ACP_DSP_BAR, ACP_SHA_DMA_CMD_STS,
						    val, val & ACP_SHA_RESET,
						    ACP_REG_POLL_INTERVAL,
						    ACP_REG_POLL_TIMEOUT_US);
		if (ret < 0) {
			dev_err(sdev->dev, "SHA DMA Failed to Reset\n");
			return ret;
		}
	}

	if (adata->quirks && adata->quirks->signed_fw_image)
		snd_sof_dsp_write(sdev, ACP_DSP_BAR, ACP_SHA_DMA_INCLUDE_HDR, ACP_SHA_HEADER);

	snd_sof_dsp_write(sdev, ACP_DSP_BAR, ACP_SHA_DMA_STRT_ADDR, start_addr);
	snd_sof_dsp_write(sdev, ACP_DSP_BAR, ACP_SHA_DMA_DESTINATION_ADDR, dest_addr);
	snd_sof_dsp_write(sdev, ACP_DSP_BAR, ACP_SHA_MSG_LENGTH, image_length);
	snd_sof_dsp_write(sdev, ACP_DSP_BAR, ACP_SHA_DMA_CMD, ACP_SHA_RUN);

	ret = snd_sof_dsp_read_poll_timeout(sdev, ACP_DSP_BAR, ACP_SHA_TRANSFER_BYTE_CNT,
					    tx_count, tx_count == image_length,
					    ACP_REG_POLL_INTERVAL, ACP_DMA_COMPLETE_TIMEOUT_US);
	if (ret < 0) {
		dev_err(sdev->dev, "SHA DMA Failed to Transfer Length %x\n", tx_count);
		return ret;
	}

	/* psp_send_cmd only required for renoir platform (rev - 3) */
	if (desc->rev == 3) {
		ret = psp_send_cmd(adata, MBOX_ACP_SHA_DMA_COMMAND);
		if (ret)
			return ret;
	}

	/* psp_send_cmd only required for vangogh platform (rev - 5) */
	if (desc->rev == 5 && !(adata->quirks && adata->quirks->skip_iram_dram_size_mod)) {
		/* Modify IRAM and DRAM size */
		ret = psp_send_cmd(adata, MBOX_ACP_IRAM_DRAM_FENCE_COMMAND | IRAM_DRAM_FENCE_2);
		if (ret)
			return ret;
		ret = psp_send_cmd(adata, MBOX_ACP_IRAM_DRAM_FENCE_COMMAND | MBOX_ISREADY_FLAG);
		if (ret)
			return ret;
	}

	ret = snd_sof_dsp_read_poll_timeout(sdev, ACP_DSP_BAR, ACP_SHA_DSP_FW_QUALIFIER,
					    fw_qualifier, fw_qualifier & DSP_FW_RUN_ENABLE,
					    ACP_REG_POLL_INTERVAL, ACP_DMA_COMPLETE_TIMEOUT_US);
	if (ret < 0) {
		dev_err(sdev->dev, "PSP validation failed\n");
		return ret;
	}

	return 0;
}

int acp_dma_status(struct acp_dev_data *adata, unsigned char ch)
{
	struct snd_sof_dev *sdev = adata->dev;
	unsigned int val;
	int ret = 0;

	val = snd_sof_dsp_read(sdev, ACP_DSP_BAR, ACP_DMA_CNTL_0 + ch * sizeof(u32));
	if (val & ACP_DMA_CH_RUN) {
		ret = snd_sof_dsp_read_poll_timeout(sdev, ACP_DSP_BAR, ACP_DMA_CH_STS, val, !val,
						    ACP_REG_POLL_INTERVAL,
						    ACP_DMA_COMPLETE_TIMEOUT_US);
		if (ret < 0)
			dev_err(sdev->dev, "DMA_CHANNEL %d status timeout\n", ch);
	}

	return ret;
}

void memcpy_from_scratch(struct snd_sof_dev *sdev, u32 offset, unsigned int *dst, size_t bytes)
{
	unsigned int reg_offset = offset + ACP_SCRATCH_REG_0;
	int i, j;

	for (i = 0, j = 0; i < bytes; i = i + 4, j++)
		dst[j] = snd_sof_dsp_read(sdev, ACP_DSP_BAR, reg_offset + i);
}

void memcpy_to_scratch(struct snd_sof_dev *sdev, u32 offset, unsigned int *src, size_t bytes)
{
	unsigned int reg_offset = offset + ACP_SCRATCH_REG_0;
	int i, j;

	for (i = 0, j = 0; i < bytes; i = i + 4, j++)
		snd_sof_dsp_write(sdev, ACP_DSP_BAR, reg_offset + i, src[j]);
}

static int acp_memory_init(struct snd_sof_dev *sdev)
{
	struct acp_dev_data *adata = sdev->pdata->hw_pdata;
	const struct sof_amd_acp_desc *desc = get_chip_info(sdev->pdata);

	snd_sof_dsp_update_bits(sdev, ACP_DSP_BAR, desc->dsp_intr_base + DSP_SW_INTR_CNTL_OFFSET,
				ACP_DSP_INTR_EN_MASK, ACP_DSP_INTR_EN_MASK);
	init_dma_descriptor(adata);

	return 0;
}

static irqreturn_t acp_irq_thread(int irq, void *context)
{
	struct snd_sof_dev *sdev = context;
	const struct sof_amd_acp_desc *desc = get_chip_info(sdev->pdata);
	unsigned int count = ACP_HW_SEM_RETRY_COUNT;

	spin_lock_irq(&sdev->ipc_lock);
	/* Wait until acquired HW Semaphore lock or timeout */
	while (snd_sof_dsp_read(sdev, ACP_DSP_BAR, desc->hw_semaphore_offset) && --count)
		;
	spin_unlock_irq(&sdev->ipc_lock);

	if (!count) {
		dev_err(sdev->dev, "%s: Failed to acquire HW lock\n", __func__);
		return IRQ_NONE;
	}

	sof_ops(sdev)->irq_thread(irq, sdev);
	/* Unlock or Release HW Semaphore */
	snd_sof_dsp_write(sdev, ACP_DSP_BAR, desc->hw_semaphore_offset, 0x0);

	return IRQ_HANDLED;
};

static irqreturn_t acp_irq_handler(int irq, void *dev_id)
{
	struct amd_sdw_manager *amd_manager;
	struct snd_sof_dev *sdev = dev_id;
	const struct sof_amd_acp_desc *desc = get_chip_info(sdev->pdata);
	struct acp_dev_data *adata = sdev->pdata->hw_pdata;
	unsigned int base = desc->dsp_intr_base;
	unsigned int val;
	int irq_flag = 0;

	val = snd_sof_dsp_read(sdev, ACP_DSP_BAR, base + DSP_SW_INTR_STAT_OFFSET);
	if (val & ACP_DSP_TO_HOST_IRQ) {
		snd_sof_dsp_write(sdev, ACP_DSP_BAR, base + DSP_SW_INTR_STAT_OFFSET,
				  ACP_DSP_TO_HOST_IRQ);
		return IRQ_WAKE_THREAD;
	}

	val = snd_sof_dsp_read(sdev, ACP_DSP_BAR, desc->ext_intr_stat);
	if (val & ACP_SDW0_IRQ_MASK) {
		amd_manager = dev_get_drvdata(&adata->sdw->pdev[0]->dev);
		snd_sof_dsp_write(sdev, ACP_DSP_BAR, desc->ext_intr_stat, ACP_SDW0_IRQ_MASK);
		if (amd_manager)
			schedule_work(&amd_manager->amd_sdw_irq_thread);
		irq_flag = 1;
	}

	if (val & ACP_ERROR_IRQ_MASK) {
		snd_sof_dsp_write(sdev, ACP_DSP_BAR, desc->ext_intr_stat, ACP_ERROR_IRQ_MASK);
		snd_sof_dsp_write(sdev, ACP_DSP_BAR, base + ACP_SW0_I2S_ERROR_REASON, 0);
		snd_sof_dsp_write(sdev, ACP_DSP_BAR, base + ACP_SW1_I2S_ERROR_REASON, 0);
		snd_sof_dsp_write(sdev, ACP_DSP_BAR, base + ACP_ERROR_STATUS, 0);
		irq_flag = 1;
	}

	if (desc->ext_intr_stat1) {
		val = snd_sof_dsp_read(sdev, ACP_DSP_BAR, desc->ext_intr_stat1);
		if (val & ACP_SDW1_IRQ_MASK) {
			amd_manager = dev_get_drvdata(&adata->sdw->pdev[1]->dev);
			snd_sof_dsp_write(sdev, ACP_DSP_BAR, desc->ext_intr_stat1,
					  ACP_SDW1_IRQ_MASK);
			if (amd_manager)
				schedule_work(&amd_manager->amd_sdw_irq_thread);
			irq_flag = 1;
		}
	}
	if (irq_flag)
		return IRQ_HANDLED;
	else
		return IRQ_NONE;
}

static int acp_power_on(struct snd_sof_dev *sdev)
{
	const struct sof_amd_acp_desc *desc = get_chip_info(sdev->pdata);
	unsigned int base = desc->pgfsm_base;
	unsigned int val;
	int ret;

	val = snd_sof_dsp_read(sdev, ACP_DSP_BAR, base + PGFSM_STATUS_OFFSET);

	if (val == ACP_POWERED_ON)
		return 0;

	if (val & ACP_PGFSM_STATUS_MASK)
		snd_sof_dsp_write(sdev, ACP_DSP_BAR, base + PGFSM_CONTROL_OFFSET,
				  ACP_PGFSM_CNTL_POWER_ON_MASK);

	ret = snd_sof_dsp_read_poll_timeout(sdev, ACP_DSP_BAR, base + PGFSM_STATUS_OFFSET, val,
					    !val, ACP_REG_POLL_INTERVAL, ACP_REG_POLL_TIMEOUT_US);
	if (ret < 0)
		dev_err(sdev->dev, "timeout in ACP_PGFSM_STATUS read\n");

	return ret;
}

static int acp_reset(struct snd_sof_dev *sdev)
{
	const struct sof_amd_acp_desc *desc = get_chip_info(sdev->pdata);
	unsigned int val;
	int ret;

	snd_sof_dsp_write(sdev, ACP_DSP_BAR, ACP_SOFT_RESET, ACP_ASSERT_RESET);

	ret = snd_sof_dsp_read_poll_timeout(sdev, ACP_DSP_BAR, ACP_SOFT_RESET, val,
					    val & ACP_SOFT_RESET_DONE_MASK,
					    ACP_REG_POLL_INTERVAL, ACP_REG_POLL_TIMEOUT_US);
	if (ret < 0) {
		dev_err(sdev->dev, "timeout asserting reset\n");
		return ret;
	}

	snd_sof_dsp_write(sdev, ACP_DSP_BAR, ACP_SOFT_RESET, ACP_RELEASE_RESET);

	ret = snd_sof_dsp_read_poll_timeout(sdev, ACP_DSP_BAR, ACP_SOFT_RESET, val, !val,
					    ACP_REG_POLL_INTERVAL, ACP_REG_POLL_TIMEOUT_US);
	if (ret < 0)
		dev_err(sdev->dev, "timeout in releasing reset\n");

	if (desc->acp_clkmux_sel)
		snd_sof_dsp_write(sdev, ACP_DSP_BAR, desc->acp_clkmux_sel, ACP_CLOCK_ACLK);

	if (desc->ext_intr_enb)
		snd_sof_dsp_write(sdev, ACP_DSP_BAR, desc->ext_intr_enb, 0x01);

	if (desc->ext_intr_cntl)
		snd_sof_dsp_write(sdev, ACP_DSP_BAR, desc->ext_intr_cntl, ACP_ERROR_IRQ_MASK);
	return ret;
}

static int acp_dsp_reset(struct snd_sof_dev *sdev)
{
	unsigned int val;
	int ret;

	snd_sof_dsp_write(sdev, ACP_DSP_BAR, ACP_SOFT_RESET, ACP_DSP_ASSERT_RESET);

	ret = snd_sof_dsp_read_poll_timeout(sdev, ACP_DSP_BAR, ACP_SOFT_RESET, val,
					    val & ACP_DSP_SOFT_RESET_DONE_MASK,
					    ACP_REG_POLL_INTERVAL, ACP_REG_POLL_TIMEOUT_US);
	if (ret < 0) {
		dev_err(sdev->dev, "timeout asserting reset\n");
		return ret;
	}

	snd_sof_dsp_write(sdev, ACP_DSP_BAR, ACP_SOFT_RESET, ACP_DSP_RELEASE_RESET);

	ret = snd_sof_dsp_read_poll_timeout(sdev, ACP_DSP_BAR, ACP_SOFT_RESET, val, !val,
					    ACP_REG_POLL_INTERVAL, ACP_REG_POLL_TIMEOUT_US);
	if (ret < 0)
		dev_err(sdev->dev, "timeout in releasing reset\n");

	return ret;
}

static int acp_init(struct snd_sof_dev *sdev)
{
	int ret;

	/* power on */
	ret = acp_power_on(sdev);
	if (ret) {
		dev_err(sdev->dev, "ACP power on failed\n");
		return ret;
	}

	snd_sof_dsp_write(sdev, ACP_DSP_BAR, ACP_CONTROL, 0x01);
	/* Reset */
	return acp_reset(sdev);
}

static bool check_acp_sdw_enable_status(struct snd_sof_dev *sdev)
{
	struct acp_dev_data *acp_data;
	u32 sdw0_en, sdw1_en;

	acp_data = sdev->pdata->hw_pdata;
	if (!acp_data->sdw)
		return false;

	sdw0_en = snd_sof_dsp_read(sdev, ACP_DSP_BAR, ACP_SW0_EN);
	sdw1_en = snd_sof_dsp_read(sdev, ACP_DSP_BAR, ACP_SW1_EN);
	acp_data->sdw_en_stat = sdw0_en || sdw1_en;
	return acp_data->sdw_en_stat;
}

int amd_sof_acp_suspend(struct snd_sof_dev *sdev, u32 target_state)
{
	int ret;

	/* When acp_reset() function is invoked, it will apply ACP SOFT reset and
	 * DSP reset. ACP Soft reset sequence will cause all ACP IP registers will
	 * be reset to default values which will break the ClockStop Mode functionality.
	 * Add a condition check to apply DSP reset when SoundWire ClockStop mode
	 * is selected. For the rest of the scenarios, apply acp reset sequence.
	 */
	if (check_acp_sdw_enable_status(sdev))
		return acp_dsp_reset(sdev);

	ret = acp_reset(sdev);
	if (ret) {
		dev_err(sdev->dev, "ACP Reset failed\n");
		return ret;
	}

	snd_sof_dsp_write(sdev, ACP_DSP_BAR, ACP_CONTROL, 0x00);

	return 0;
}
EXPORT_SYMBOL_NS(amd_sof_acp_suspend, SND_SOC_SOF_AMD_COMMON);

int amd_sof_acp_resume(struct snd_sof_dev *sdev)
{
	int ret;
	struct acp_dev_data *acp_data;

	acp_data = sdev->pdata->hw_pdata;
	if (!acp_data->sdw_en_stat) {
		ret = acp_init(sdev);
		if (ret) {
			dev_err(sdev->dev, "ACP Init failed\n");
			return ret;
		}
		return acp_memory_init(sdev);
	} else {
		return acp_dsp_reset(sdev);
	}
}
EXPORT_SYMBOL_NS(amd_sof_acp_resume, SND_SOC_SOF_AMD_COMMON);

#if IS_ENABLED(CONFIG_SND_SOC_SOF_AMD_SOUNDWIRE)
static int acp_sof_scan_sdw_devices(struct snd_sof_dev *sdev, u64 addr)
{
	struct acpi_device *sdw_dev;
	struct acp_dev_data *acp_data;
	const struct sof_amd_acp_desc *desc = get_chip_info(sdev->pdata);

	if (!addr)
		return -ENODEV;

	acp_data = sdev->pdata->hw_pdata;
	sdw_dev = acpi_find_child_device(ACPI_COMPANION(sdev->dev), addr, 0);
	if (!sdw_dev)
		return -ENODEV;

	acp_data->info.handle = sdw_dev->handle;
	acp_data->info.count = desc->sdw_max_link_count;

	return amd_sdw_scan_controller(&acp_data->info);
}

static int amd_sof_sdw_probe(struct snd_sof_dev *sdev)
{
	struct acp_dev_data *acp_data;
	struct sdw_amd_res sdw_res;
	int ret;

	acp_data = sdev->pdata->hw_pdata;

	memset(&sdw_res, 0, sizeof(sdw_res));
	sdw_res.addr = acp_data->addr;
	sdw_res.reg_range = acp_data->reg_range;
	sdw_res.handle = acp_data->info.handle;
	sdw_res.parent = sdev->dev;
	sdw_res.dev = sdev->dev;
	sdw_res.acp_lock = &acp_data->acp_lock;
	sdw_res.count = acp_data->info.count;
	sdw_res.link_mask = acp_data->info.link_mask;
	sdw_res.mmio_base = sdev->bar[ACP_DSP_BAR];

	ret = sdw_amd_probe(&sdw_res, &acp_data->sdw);
	if (ret)
		dev_err(sdev->dev, "SoundWire probe failed\n");
	return ret;
}

static int amd_sof_sdw_exit(struct snd_sof_dev *sdev)
{
	struct acp_dev_data *acp_data;

	acp_data = sdev->pdata->hw_pdata;
	if (acp_data->sdw)
		sdw_amd_exit(acp_data->sdw);
	acp_data->sdw = NULL;

	return 0;
}

#else
static int acp_sof_scan_sdw_devices(struct snd_sof_dev *sdev, u64 addr)
{
	return 0;
}

static int amd_sof_sdw_probe(struct snd_sof_dev *sdev)
{
	return 0;
}

static int amd_sof_sdw_exit(struct snd_sof_dev *sdev)
{
	return 0;
}
#endif

int amd_sof_acp_probe(struct snd_sof_dev *sdev)
{
	struct pci_dev *pci = to_pci_dev(sdev->dev);
	struct acp_dev_data *adata;
	const struct sof_amd_acp_desc *chip;
	const struct dmi_system_id *dmi_id;
	unsigned int addr;
	int ret;

	chip = get_chip_info(sdev->pdata);
	if (!chip) {
		dev_err(sdev->dev, "no such device supported, chip id:%x\n", pci->device);
		return -EIO;
	}
	adata = devm_kzalloc(sdev->dev, sizeof(struct acp_dev_data),
			     GFP_KERNEL);
	if (!adata)
		return -ENOMEM;

	adata->dev = sdev;
	adata->dmic_dev = platform_device_register_data(sdev->dev, "dmic-codec",
							PLATFORM_DEVID_NONE, NULL, 0);
	if (IS_ERR(adata->dmic_dev)) {
		dev_err(sdev->dev, "failed to register platform for dmic codec\n");
		return PTR_ERR(adata->dmic_dev);
	}
	addr = pci_resource_start(pci, ACP_DSP_BAR);
	sdev->bar[ACP_DSP_BAR] = devm_ioremap(sdev->dev, addr, pci_resource_len(pci, ACP_DSP_BAR));
	if (!sdev->bar[ACP_DSP_BAR]) {
		dev_err(sdev->dev, "ioremap error\n");
		ret = -ENXIO;
		goto unregister_dev;
	}

	pci_set_master(pci);
	adata->addr = addr;
	adata->reg_range = chip->reg_end_addr - chip->reg_start_addr;
	mutex_init(&adata->acp_lock);
	sdev->pdata->hw_pdata = adata;
	adata->smn_dev = pci_get_device(PCI_VENDOR_ID_AMD, chip->host_bridge_id, NULL);
	if (!adata->smn_dev) {
		dev_err(sdev->dev, "Failed to get host bridge device\n");
		ret = -ENODEV;
		goto unregister_dev;
	}

	sdev->ipc_irq = pci->irq;
	ret = request_threaded_irq(sdev->ipc_irq, acp_irq_handler, acp_irq_thread,
				   IRQF_SHARED, "AudioDSP", sdev);
	if (ret < 0) {
		dev_err(sdev->dev, "failed to register IRQ %d\n",
			sdev->ipc_irq);
		goto free_smn_dev;
	}

	ret = acp_init(sdev);
	if (ret < 0)
		goto free_ipc_irq;

	/* scan SoundWire capabilities exposed by DSDT */
	ret = acp_sof_scan_sdw_devices(sdev, chip->sdw_acpi_dev_addr);
	if (ret < 0) {
		dev_dbg(sdev->dev, "skipping SoundWire, not detected with ACPI scan\n");
		goto skip_soundwire;
	}
	ret = amd_sof_sdw_probe(sdev);
	if (ret < 0) {
		dev_err(sdev->dev, "error: SoundWire probe error\n");
		free_irq(sdev->ipc_irq, sdev);
		pci_dev_put(adata->smn_dev);
		return ret;
	}

skip_soundwire:
	sdev->dsp_box.offset = 0;
	sdev->dsp_box.size = BOX_SIZE_512;

	sdev->host_box.offset = sdev->dsp_box.offset + sdev->dsp_box.size;
	sdev->host_box.size = BOX_SIZE_512;

	sdev->debug_box.offset = sdev->host_box.offset + sdev->host_box.size;
	sdev->debug_box.size = BOX_SIZE_1024;

	dmi_id = dmi_first_match(acp_sof_quirk_table);
	if (dmi_id) {
		adata->quirks = dmi_id->driver_data;

		if (adata->quirks->signed_fw_image) {
			adata->fw_code_bin = devm_kasprintf(sdev->dev, GFP_KERNEL,
							    "sof-%s-code.bin",
							    chip->name);
			if (!adata->fw_code_bin) {
				ret = -ENOMEM;
				goto free_ipc_irq;
			}

			adata->fw_data_bin = devm_kasprintf(sdev->dev, GFP_KERNEL,
							    "sof-%s-data.bin",
							    chip->name);
			if (!adata->fw_data_bin) {
				ret = -ENOMEM;
				goto free_ipc_irq;
			}
		}
	}

	adata->enable_fw_debug = enable_fw_debug;
	acp_memory_init(sdev);

	acp_dsp_stream_init(sdev);

	return 0;

free_ipc_irq:
	free_irq(sdev->ipc_irq, sdev);
free_smn_dev:
	pci_dev_put(adata->smn_dev);
unregister_dev:
	platform_device_unregister(adata->dmic_dev);
	return ret;
}
EXPORT_SYMBOL_NS(amd_sof_acp_probe, SND_SOC_SOF_AMD_COMMON);

void amd_sof_acp_remove(struct snd_sof_dev *sdev)
{
	struct acp_dev_data *adata = sdev->pdata->hw_pdata;

	if (adata->smn_dev)
		pci_dev_put(adata->smn_dev);

	if (adata->sdw)
		amd_sof_sdw_exit(sdev);

	if (sdev->ipc_irq)
		free_irq(sdev->ipc_irq, sdev);

	if (adata->dmic_dev)
		platform_device_unregister(adata->dmic_dev);

	acp_reset(sdev);
}
EXPORT_SYMBOL_NS(amd_sof_acp_remove, SND_SOC_SOF_AMD_COMMON);

MODULE_DESCRIPTION("AMD ACP sof driver");
MODULE_IMPORT_NS(SOUNDWIRE_AMD_INIT);
MODULE_IMPORT_NS(SND_AMD_SOUNDWIRE_ACPI);
MODULE_LICENSE("Dual BSD/GPL");
