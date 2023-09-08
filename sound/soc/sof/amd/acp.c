// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license. When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2021 Advanced Micro Devices, Inc. All rights reserved.
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

static int smn_write(struct pci_dev *dev, u32 smn_addr, u32 data)
{
	pci_write_config_dword(dev, 0x60, smn_addr);
	pci_write_config_dword(dev, 0x64, data);

	return 0;
}

static int smn_read(struct pci_dev *dev, u32 smn_addr, u32 *data)
{
	pci_write_config_dword(dev, 0x60, smn_addr);
	pci_read_config_dword(dev, 0x64, data);

	return 0;
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
	int timeout;
	u32 data;

	for (timeout = ACP_PSP_TIMEOUT_COUNTER; timeout > 0; timeout--) {
		msleep(20);
		smn_read(adata->smn_dev, MP0_C2PMSG_114_REG, &data);
		if (data & MBOX_READY_MASK)
			return 0;
	}

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
	int ret, timeout;
	u32 data;

	if (!cmd)
		return -EINVAL;

	/* Get a non-zero Doorbell value from PSP */
	for (timeout = ACP_PSP_TIMEOUT_COUNTER; timeout > 0; timeout--) {
		msleep(MBOX_DELAY);
		smn_read(adata->smn_dev, MP0_C2PMSG_73_REG, &data);
		if (data)
			break;
	}

	if (!timeout) {
		dev_err(sdev->dev, "Failed to get Doorbell from MBOX %x\n", MP0_C2PMSG_73_REG);
		return -EINVAL;
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

	ret = psp_send_cmd(adata, MBOX_ACP_SHA_DMA_COMMAND);
	if (ret)
		return ret;

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
	unsigned int val, count = ACP_HW_SEM_RETRY_COUNT;

	val = snd_sof_dsp_read(sdev, ACP_DSP_BAR, desc->ext_intr_stat);
	if (val & ACP_SHA_STAT) {
		/* Clear SHA interrupt raised by PSP */
		snd_sof_dsp_write(sdev, ACP_DSP_BAR, desc->ext_intr_stat, val);
		return IRQ_HANDLED;
	}

	while (snd_sof_dsp_read(sdev, ACP_DSP_BAR, desc->hw_semaphore_offset)) {
		/* Wait until acquired HW Semaphore lock or timeout */
		count--;
		if (!count) {
			dev_err(sdev->dev, "%s: Failed to acquire HW lock\n", __func__);
			return IRQ_NONE;
		}
	}

	sof_ops(sdev)->irq_thread(irq, sdev);
	/* Unlock or Release HW Semaphore */
	snd_sof_dsp_write(sdev, ACP_DSP_BAR, desc->hw_semaphore_offset, 0x0);

	return IRQ_HANDLED;
};

static irqreturn_t acp_irq_handler(int irq, void *dev_id)
{
	struct snd_sof_dev *sdev = dev_id;
	const struct sof_amd_acp_desc *desc = get_chip_info(sdev->pdata);
	unsigned int base = desc->dsp_intr_base;
	unsigned int val;

	val = snd_sof_dsp_read(sdev, ACP_DSP_BAR, base + DSP_SW_INTR_STAT_OFFSET);
	if (val) {
		val |= ACP_DSP_TO_HOST_IRQ;
		snd_sof_dsp_write(sdev, ACP_DSP_BAR, base + DSP_SW_INTR_STAT_OFFSET, val);
		return IRQ_WAKE_THREAD;
	}

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

	snd_sof_dsp_write(sdev, ACP_DSP_BAR, desc->acp_clkmux_sel, ACP_CLOCK_ACLK);
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

int amd_sof_acp_suspend(struct snd_sof_dev *sdev, u32 target_state)
{
	int ret;

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
	const struct sof_amd_acp_desc *desc = get_chip_info(sdev->pdata);
	int ret;

	ret = acp_init(sdev);
	if (ret) {
		dev_err(sdev->dev, "ACP Init failed\n");
		return ret;
	}

	snd_sof_dsp_write(sdev, ACP_DSP_BAR, desc->acp_clkmux_sel, ACP_CLOCK_ACLK);

	ret = acp_memory_init(sdev);

	return ret;
}
EXPORT_SYMBOL_NS(amd_sof_acp_resume, SND_SOC_SOF_AMD_COMMON);

int amd_sof_acp_probe(struct snd_sof_dev *sdev)
{
	struct pci_dev *pci = to_pci_dev(sdev->dev);
	struct acp_dev_data *adata;
	const struct sof_amd_acp_desc *chip;
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

	sdev->dsp_box.offset = 0;
	sdev->dsp_box.size = BOX_SIZE_512;

	sdev->host_box.offset = sdev->dsp_box.offset + sdev->dsp_box.size;
	sdev->host_box.size = BOX_SIZE_512;

	sdev->debug_box.offset = sdev->host_box.offset + sdev->host_box.size;
	sdev->debug_box.size = BOX_SIZE_1024;

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

int amd_sof_acp_remove(struct snd_sof_dev *sdev)
{
	struct acp_dev_data *adata = sdev->pdata->hw_pdata;

	if (adata->smn_dev)
		pci_dev_put(adata->smn_dev);

	if (sdev->ipc_irq)
		free_irq(sdev->ipc_irq, sdev);

	if (adata->dmic_dev)
		platform_device_unregister(adata->dmic_dev);

	return acp_reset(sdev);
}
EXPORT_SYMBOL_NS(amd_sof_acp_remove, SND_SOC_SOF_AMD_COMMON);

MODULE_DESCRIPTION("AMD ACP sof driver");
MODULE_LICENSE("Dual BSD/GPL");
