// SPDX-License-Identifier: GPL-2.0+
/*
 * AMD Pink Sardine ACP PCI Driver
 *
 * Copyright 2022 Advanced Micro Devices, Inc.
 */

#include <linux/pci.h>
#include <linux/bitops.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/acpi.h>
#include <linux/interrupt.h>
#include <sound/pcm_params.h>
#include <linux/pm_runtime.h>
#include <linux/iopoll.h>
#include <linux/soundwire/sdw_amd.h>

#include "acp63.h"

static int acp63_power_on(void __iomem *acp_base)
{
	u32 val;

	val = readl(acp_base + ACP_PGFSM_STATUS);

	if (!val)
		return val;

	if ((val & ACP_PGFSM_STATUS_MASK) != ACP_POWER_ON_IN_PROGRESS)
		writel(ACP_PGFSM_CNTL_POWER_ON_MASK, acp_base + ACP_PGFSM_CONTROL);

	return readl_poll_timeout(acp_base + ACP_PGFSM_STATUS, val, !val, DELAY_US, ACP_TIMEOUT);
}

static int acp63_reset(void __iomem *acp_base)
{
	u32 val;
	int ret;

	writel(1, acp_base + ACP_SOFT_RESET);

	ret = readl_poll_timeout(acp_base + ACP_SOFT_RESET, val,
				 val & ACP_SOFT_RESET_SOFTRESET_AUDDONE_MASK,
				 DELAY_US, ACP_TIMEOUT);
	if (ret)
		return ret;

	writel(0, acp_base + ACP_SOFT_RESET);

	return readl_poll_timeout(acp_base + ACP_SOFT_RESET, val, !val, DELAY_US, ACP_TIMEOUT);
}

static void acp63_enable_interrupts(void __iomem *acp_base)
{
	writel(1, acp_base + ACP_EXTERNAL_INTR_ENB);
	writel(ACP_ERROR_IRQ, acp_base + ACP_EXTERNAL_INTR_CNTL);
}

static void acp63_disable_interrupts(void __iomem *acp_base)
{
	writel(ACP_EXT_INTR_STAT_CLEAR_MASK, acp_base + ACP_EXTERNAL_INTR_STAT);
	writel(0, acp_base + ACP_EXTERNAL_INTR_CNTL);
	writel(0, acp_base + ACP_EXTERNAL_INTR_ENB);
}

static int acp63_init(void __iomem *acp_base, struct device *dev)
{
	int ret;

	ret = acp63_power_on(acp_base);
	if (ret) {
		dev_err(dev, "ACP power on failed\n");
		return ret;
	}
	writel(0x01, acp_base + ACP_CONTROL);
	ret = acp63_reset(acp_base);
	if (ret) {
		dev_err(dev, "ACP reset failed\n");
		return ret;
	}
	acp63_enable_interrupts(acp_base);
	return 0;
}

static int acp63_deinit(void __iomem *acp_base, struct device *dev)
{
	int ret;

	acp63_disable_interrupts(acp_base);
	ret = acp63_reset(acp_base);
	if (ret) {
		dev_err(dev, "ACP reset failed\n");
		return ret;
	}
	writel(0, acp_base + ACP_CONTROL);
	return 0;
}

static irqreturn_t acp63_irq_thread(int irq, void *context)
{
	struct sdw_dma_dev_data *sdw_dma_data;
	struct acp63_dev_data *adata = context;
	u32 stream_index;
	u16 pdev_index;

	pdev_index = adata->sdw_dma_dev_index;
	sdw_dma_data = dev_get_drvdata(&adata->pdev[pdev_index]->dev);

	for (stream_index = 0; stream_index < ACP63_SDW0_DMA_MAX_STREAMS; stream_index++) {
		if (adata->sdw0_dma_intr_stat[stream_index]) {
			if (sdw_dma_data->sdw0_dma_stream[stream_index])
				snd_pcm_period_elapsed(sdw_dma_data->sdw0_dma_stream[stream_index]);
			adata->sdw0_dma_intr_stat[stream_index] = 0;
		}
	}
	for (stream_index = 0; stream_index < ACP63_SDW1_DMA_MAX_STREAMS; stream_index++) {
		if (adata->sdw1_dma_intr_stat[stream_index]) {
			if (sdw_dma_data->sdw1_dma_stream[stream_index])
				snd_pcm_period_elapsed(sdw_dma_data->sdw1_dma_stream[stream_index]);
			adata->sdw1_dma_intr_stat[stream_index] = 0;
		}
	}
	return IRQ_HANDLED;
}

static irqreturn_t acp63_irq_handler(int irq, void *dev_id)
{
	struct acp63_dev_data *adata;
	struct pdm_dev_data *ps_pdm_data;
	struct amd_sdw_manager *amd_manager;
	u32 ext_intr_stat, ext_intr_stat1;
	u32 stream_id = 0;
	u16 irq_flag = 0;
	u16 sdw_dma_irq_flag = 0;
	u16 pdev_index;
	u16 index;

	adata = dev_id;
	if (!adata)
		return IRQ_NONE;
	/* ACP interrupts will be cleared by reading particular bit and writing
	 * same value to the status register. writing zero's doesn't have any
	 * effect.
	 * Bit by bit checking of IRQ field is implemented.
	 */
	ext_intr_stat = readl(adata->acp63_base + ACP_EXTERNAL_INTR_STAT);
	if (ext_intr_stat & ACP_SDW0_STAT) {
		writel(ACP_SDW0_STAT, adata->acp63_base + ACP_EXTERNAL_INTR_STAT);
		pdev_index = adata->sdw0_dev_index;
		amd_manager = dev_get_drvdata(&adata->pdev[pdev_index]->dev);
		if (amd_manager)
			schedule_work(&amd_manager->amd_sdw_irq_thread);
		irq_flag = 1;
	}

	ext_intr_stat1 = readl(adata->acp63_base + ACP_EXTERNAL_INTR_STAT1);
	if (ext_intr_stat1 & ACP_SDW1_STAT) {
		writel(ACP_SDW1_STAT, adata->acp63_base + ACP_EXTERNAL_INTR_STAT1);
		pdev_index = adata->sdw1_dev_index;
		amd_manager = dev_get_drvdata(&adata->pdev[pdev_index]->dev);
		if (amd_manager)
			schedule_work(&amd_manager->amd_sdw_irq_thread);
		irq_flag = 1;
	}

	if (ext_intr_stat & ACP_ERROR_IRQ) {
		writel(ACP_ERROR_IRQ, adata->acp63_base + ACP_EXTERNAL_INTR_STAT);
		/* TODO: Report SoundWire Manager instance errors */
		writel(0, adata->acp63_base + ACP_SW0_I2S_ERROR_REASON);
		writel(0, adata->acp63_base + ACP_SW1_I2S_ERROR_REASON);
		writel(0, adata->acp63_base + ACP_ERROR_STATUS);
		irq_flag = 1;
	}

	if (ext_intr_stat & BIT(PDM_DMA_STAT)) {
		pdev_index = adata->pdm_dev_index;
		ps_pdm_data = dev_get_drvdata(&adata->pdev[pdev_index]->dev);
		writel(BIT(PDM_DMA_STAT), adata->acp63_base + ACP_EXTERNAL_INTR_STAT);
		if (ps_pdm_data->capture_stream)
			snd_pcm_period_elapsed(ps_pdm_data->capture_stream);
		irq_flag = 1;
	}
	if (ext_intr_stat & ACP_SDW_DMA_IRQ_MASK) {
		for (index = ACP_AUDIO2_RX_THRESHOLD; index <= ACP_AUDIO0_TX_THRESHOLD; index++) {
			if (ext_intr_stat & BIT(index)) {
				writel(BIT(index), adata->acp63_base + ACP_EXTERNAL_INTR_STAT);
				switch (index) {
				case ACP_AUDIO0_TX_THRESHOLD:
					stream_id = ACP_SDW0_AUDIO0_TX;
					break;
				case ACP_AUDIO1_TX_THRESHOLD:
					stream_id = ACP_SDW0_AUDIO1_TX;
					break;
				case ACP_AUDIO2_TX_THRESHOLD:
					stream_id = ACP_SDW0_AUDIO2_TX;
					break;
				case ACP_AUDIO0_RX_THRESHOLD:
					stream_id = ACP_SDW0_AUDIO0_RX;
					break;
				case ACP_AUDIO1_RX_THRESHOLD:
					stream_id = ACP_SDW0_AUDIO1_RX;
					break;
				case ACP_AUDIO2_RX_THRESHOLD:
					stream_id = ACP_SDW0_AUDIO2_RX;
					break;
				}

				adata->sdw0_dma_intr_stat[stream_id] = 1;
				sdw_dma_irq_flag = 1;
			}
		}
	}

	if (ext_intr_stat1 & ACP_P1_AUDIO1_RX_THRESHOLD) {
		writel(ACP_P1_AUDIO1_RX_THRESHOLD,
		       adata->acp63_base + ACP_EXTERNAL_INTR_STAT1);
		adata->sdw1_dma_intr_stat[ACP_SDW1_AUDIO1_RX] = 1;
		sdw_dma_irq_flag = 1;
	}

	if (ext_intr_stat1 & ACP_P1_AUDIO1_TX_THRESHOLD) {
		writel(ACP_P1_AUDIO1_TX_THRESHOLD,
		       adata->acp63_base + ACP_EXTERNAL_INTR_STAT1);
		adata->sdw1_dma_intr_stat[ACP_SDW1_AUDIO1_TX] = 1;
		sdw_dma_irq_flag = 1;
	}

	if (sdw_dma_irq_flag)
		return IRQ_WAKE_THREAD;

	if (irq_flag)
		return IRQ_HANDLED;
	else
		return IRQ_NONE;
}

static int sdw_amd_scan_controller(struct device *dev)
{
	struct acp63_dev_data *acp_data;
	struct fwnode_handle *link;
	char name[32];
	u32 sdw_manager_bitmap;
	u8 count = 0;
	u32 acp_sdw_power_mode = 0;
	int index;
	int ret;

	acp_data = dev_get_drvdata(dev);
	/*
	 * Current implementation is based on MIPI DisCo 2.0 spec.
	 * Found controller, find links supported.
	 */
	ret = fwnode_property_read_u32_array((acp_data->sdw_fw_node), "mipi-sdw-manager-list",
					     &sdw_manager_bitmap, 1);

	if (ret) {
		dev_dbg(dev, "Failed to read mipi-sdw-manager-list: %d\n", ret);
		return -EINVAL;
	}
	count = hweight32(sdw_manager_bitmap);
	/* Check count is within bounds */
	if (count > AMD_SDW_MAX_MANAGERS) {
		dev_err(dev, "Manager count %d exceeds max %d\n", count, AMD_SDW_MAX_MANAGERS);
		return -EINVAL;
	}

	if (!count) {
		dev_dbg(dev, "No SoundWire Managers detected\n");
		return -EINVAL;
	}
	dev_dbg(dev, "ACPI reports %d SoundWire Manager devices\n", count);
	acp_data->sdw_manager_count = count;
	for (index = 0; index < count; index++) {
		scnprintf(name, sizeof(name), "mipi-sdw-link-%d-subproperties", index);
		link = fwnode_get_named_child_node(acp_data->sdw_fw_node, name);
		if (!link) {
			dev_err(dev, "Manager node %s not found\n", name);
			return -EIO;
		}

		ret = fwnode_property_read_u32(link, "amd-sdw-power-mode", &acp_sdw_power_mode);
		if (ret)
			return ret;
		/*
		 * when SoundWire configuration is selected from acp pin config,
		 * based on manager instances count, acp init/de-init sequence should be
		 * executed as part of PM ops only when Bus reset is applied for the active
		 * SoundWire manager instances.
		 */
		if (acp_sdw_power_mode != AMD_SDW_POWER_OFF_MODE) {
			acp_data->acp_reset = false;
			return 0;
		}
	}
	return 0;
}

static int get_acp63_device_config(u32 config, struct pci_dev *pci, struct acp63_dev_data *acp_data)
{
	struct acpi_device *dmic_dev;
	struct acpi_device *sdw_dev;
	const union acpi_object *obj;
	bool is_dmic_dev = false;
	bool is_sdw_dev = false;
	int ret;

	dmic_dev = acpi_find_child_device(ACPI_COMPANION(&pci->dev), ACP63_DMIC_ADDR, 0);
	if (dmic_dev) {
		/* is_dmic_dev flag will be set when ACP PDM controller device exists */
		if (!acpi_dev_get_property(dmic_dev, "acp-audio-device-type",
					   ACPI_TYPE_INTEGER, &obj) &&
					   obj->integer.value == ACP_DMIC_DEV)
			is_dmic_dev = true;
	}

	sdw_dev = acpi_find_child_device(ACPI_COMPANION(&pci->dev), ACP63_SDW_ADDR, 0);
	if (sdw_dev) {
		acp_data->sdw_fw_node = acpi_fwnode_handle(sdw_dev);
		ret = sdw_amd_scan_controller(&pci->dev);
		/* is_sdw_dev flag will be set when SoundWire Manager device exists */
		if (!ret)
			is_sdw_dev = true;
	}
	if (!is_dmic_dev && !is_sdw_dev)
		return -ENODEV;
	dev_dbg(&pci->dev, "Audio Mode %d\n", config);
	switch (config) {
	case ACP_CONFIG_4:
	case ACP_CONFIG_5:
	case ACP_CONFIG_10:
	case ACP_CONFIG_11:
		if (is_dmic_dev) {
			acp_data->pdev_config = ACP63_PDM_DEV_CONFIG;
			acp_data->pdev_count = ACP63_PDM_MODE_DEVS;
		}
		break;
	case ACP_CONFIG_2:
	case ACP_CONFIG_3:
		if (is_sdw_dev) {
			switch (acp_data->sdw_manager_count) {
			case 1:
				acp_data->pdev_config = ACP63_SDW_DEV_CONFIG;
				acp_data->pdev_count = ACP63_SDW0_MODE_DEVS;
				break;
			case 2:
				acp_data->pdev_config = ACP63_SDW_DEV_CONFIG;
				acp_data->pdev_count = ACP63_SDW0_SDW1_MODE_DEVS;
				break;
			default:
				return -EINVAL;
			}
		}
		break;
	case ACP_CONFIG_6:
	case ACP_CONFIG_7:
	case ACP_CONFIG_12:
	case ACP_CONFIG_8:
	case ACP_CONFIG_13:
	case ACP_CONFIG_14:
		if (is_dmic_dev && is_sdw_dev) {
			switch (acp_data->sdw_manager_count) {
			case 1:
				acp_data->pdev_config = ACP63_SDW_PDM_DEV_CONFIG;
				acp_data->pdev_count = ACP63_SDW0_PDM_MODE_DEVS;
				break;
			case 2:
				acp_data->pdev_config = ACP63_SDW_PDM_DEV_CONFIG;
				acp_data->pdev_count = ACP63_SDW0_SDW1_PDM_MODE_DEVS;
				break;
			default:
				return -EINVAL;
			}
		} else if (is_dmic_dev) {
			acp_data->pdev_config = ACP63_PDM_DEV_CONFIG;
			acp_data->pdev_count = ACP63_PDM_MODE_DEVS;
		} else if (is_sdw_dev) {
			switch (acp_data->sdw_manager_count) {
			case 1:
				acp_data->pdev_config = ACP63_SDW_DEV_CONFIG;
				acp_data->pdev_count = ACP63_SDW0_MODE_DEVS;
				break;
			case 2:
				acp_data->pdev_config = ACP63_SDW_DEV_CONFIG;
				acp_data->pdev_count = ACP63_SDW0_SDW1_MODE_DEVS;
				break;
			default:
				return -EINVAL;
			}
		}
		break;
	default:
		break;
	}
	return 0;
}

static void acp63_fill_platform_dev_info(struct platform_device_info *pdevinfo,
					 struct device *parent,
					 struct fwnode_handle *fw_node,
					 char *name, unsigned int id,
					 const struct resource *res,
					 unsigned int num_res,
					 const void *data,
					 size_t size_data)
{
	pdevinfo->name = name;
	pdevinfo->id = id;
	pdevinfo->parent = parent;
	pdevinfo->num_res = num_res;
	pdevinfo->res = res;
	pdevinfo->data = data;
	pdevinfo->size_data = size_data;
	pdevinfo->fwnode = fw_node;
}

static int create_acp63_platform_devs(struct pci_dev *pci, struct acp63_dev_data *adata, u32 addr)
{
	struct acp_sdw_pdata *sdw_pdata;
	struct platform_device_info pdevinfo[ACP63_DEVS];
	struct device *parent;
	int index;
	int ret;

	parent = &pci->dev;
	dev_dbg(&pci->dev,
		"%s pdev_config:0x%x pdev_count:0x%x\n", __func__, adata->pdev_config,
		adata->pdev_count);
	if (adata->pdev_config) {
		adata->res = devm_kzalloc(&pci->dev, sizeof(struct resource), GFP_KERNEL);
		if (!adata->res) {
			ret = -ENOMEM;
			goto de_init;
		}
		adata->res->flags = IORESOURCE_MEM;
		adata->res->start = addr;
		adata->res->end = addr + (ACP63_REG_END - ACP63_REG_START);
		memset(&pdevinfo, 0, sizeof(pdevinfo));
	}

	switch (adata->pdev_config) {
	case ACP63_PDM_DEV_CONFIG:
		adata->pdm_dev_index  = 0;
		acp63_fill_platform_dev_info(&pdevinfo[0], parent, NULL, "acp_ps_pdm_dma",
					     0, adata->res, 1, NULL, 0);
		acp63_fill_platform_dev_info(&pdevinfo[1], parent, NULL, "dmic-codec",
					     0, NULL, 0, NULL, 0);
		acp63_fill_platform_dev_info(&pdevinfo[2], parent, NULL, "acp_ps_mach",
					     0, NULL, 0, NULL, 0);
		break;
	case ACP63_SDW_DEV_CONFIG:
		if (adata->pdev_count == ACP63_SDW0_MODE_DEVS) {
			sdw_pdata = devm_kzalloc(&pci->dev, sizeof(struct acp_sdw_pdata),
						 GFP_KERNEL);
			if (!sdw_pdata) {
				ret = -ENOMEM;
				goto de_init;
			}

			sdw_pdata->instance = 0;
			sdw_pdata->acp_sdw_lock = &adata->acp_lock;
			adata->sdw0_dev_index = 0;
			adata->sdw_dma_dev_index = 1;
			acp63_fill_platform_dev_info(&pdevinfo[0], parent, adata->sdw_fw_node,
						     "amd_sdw_manager", 0, adata->res, 1,
						     sdw_pdata, sizeof(struct acp_sdw_pdata));
			acp63_fill_platform_dev_info(&pdevinfo[1], parent, NULL, "amd_ps_sdw_dma",
						     0, adata->res, 1, NULL, 0);
		} else if (adata->pdev_count == ACP63_SDW0_SDW1_MODE_DEVS) {
			sdw_pdata = devm_kzalloc(&pci->dev, sizeof(struct acp_sdw_pdata) * 2,
						 GFP_KERNEL);
			if (!sdw_pdata) {
				ret = -ENOMEM;
				goto de_init;
			}

			sdw_pdata[0].instance = 0;
			sdw_pdata[1].instance = 1;
			sdw_pdata[0].acp_sdw_lock = &adata->acp_lock;
			sdw_pdata[1].acp_sdw_lock = &adata->acp_lock;
			sdw_pdata->acp_sdw_lock = &adata->acp_lock;
			adata->sdw0_dev_index = 0;
			adata->sdw1_dev_index = 1;
			adata->sdw_dma_dev_index = 2;
			acp63_fill_platform_dev_info(&pdevinfo[0], parent, adata->sdw_fw_node,
						     "amd_sdw_manager", 0, adata->res, 1,
						     &sdw_pdata[0], sizeof(struct acp_sdw_pdata));
			acp63_fill_platform_dev_info(&pdevinfo[1], parent, adata->sdw_fw_node,
						     "amd_sdw_manager", 1, adata->res, 1,
						     &sdw_pdata[1], sizeof(struct acp_sdw_pdata));
			acp63_fill_platform_dev_info(&pdevinfo[2], parent, NULL, "amd_ps_sdw_dma",
						     0, adata->res, 1, NULL, 0);
		}
		break;
	case ACP63_SDW_PDM_DEV_CONFIG:
		if (adata->pdev_count == ACP63_SDW0_PDM_MODE_DEVS) {
			sdw_pdata = devm_kzalloc(&pci->dev, sizeof(struct acp_sdw_pdata),
						 GFP_KERNEL);
			if (!sdw_pdata) {
				ret = -ENOMEM;
				goto de_init;
			}

			sdw_pdata->instance = 0;
			sdw_pdata->acp_sdw_lock = &adata->acp_lock;
			adata->pdm_dev_index = 0;
			adata->sdw0_dev_index = 1;
			adata->sdw_dma_dev_index = 2;
			acp63_fill_platform_dev_info(&pdevinfo[0], parent, NULL, "acp_ps_pdm_dma",
						     0, adata->res, 1, NULL, 0);
			acp63_fill_platform_dev_info(&pdevinfo[1], parent, adata->sdw_fw_node,
						     "amd_sdw_manager", 0, adata->res, 1,
						     sdw_pdata, sizeof(struct acp_sdw_pdata));
			acp63_fill_platform_dev_info(&pdevinfo[2], parent, NULL, "amd_ps_sdw_dma",
						     0, adata->res, 1, NULL, 0);
			acp63_fill_platform_dev_info(&pdevinfo[3], parent, NULL, "dmic-codec",
						     0, NULL, 0, NULL, 0);
		} else if (adata->pdev_count == ACP63_SDW0_SDW1_PDM_MODE_DEVS) {
			sdw_pdata = devm_kzalloc(&pci->dev, sizeof(struct acp_sdw_pdata) * 2,
						 GFP_KERNEL);
			if (!sdw_pdata) {
				ret = -ENOMEM;
				goto de_init;
			}
			sdw_pdata[0].instance = 0;
			sdw_pdata[1].instance = 1;
			sdw_pdata[0].acp_sdw_lock = &adata->acp_lock;
			sdw_pdata[1].acp_sdw_lock = &adata->acp_lock;
			adata->pdm_dev_index = 0;
			adata->sdw0_dev_index = 1;
			adata->sdw1_dev_index = 2;
			adata->sdw_dma_dev_index = 3;
			acp63_fill_platform_dev_info(&pdevinfo[0], parent, NULL, "acp_ps_pdm_dma",
						     0, adata->res, 1, NULL, 0);
			acp63_fill_platform_dev_info(&pdevinfo[1], parent, adata->sdw_fw_node,
						     "amd_sdw_manager", 0, adata->res, 1,
						     &sdw_pdata[0], sizeof(struct acp_sdw_pdata));
			acp63_fill_platform_dev_info(&pdevinfo[2], parent, adata->sdw_fw_node,
						     "amd_sdw_manager", 1, adata->res, 1,
						     &sdw_pdata[1], sizeof(struct acp_sdw_pdata));
			acp63_fill_platform_dev_info(&pdevinfo[3], parent, NULL, "amd_ps_sdw_dma",
						     0, adata->res, 1, NULL, 0);
			acp63_fill_platform_dev_info(&pdevinfo[4], parent, NULL, "dmic-codec",
						     0, NULL, 0, NULL, 0);
		}
		break;
	default:
		dev_dbg(&pci->dev, "No PDM or SoundWire manager devices found\n");
		return 0;
	}

	for (index = 0; index < adata->pdev_count; index++) {
		adata->pdev[index] = platform_device_register_full(&pdevinfo[index]);
		if (IS_ERR(adata->pdev[index])) {
			dev_err(&pci->dev,
				"cannot register %s device\n", pdevinfo[index].name);
			ret = PTR_ERR(adata->pdev[index]);
			goto unregister_devs;
		}
	}
	return 0;
unregister_devs:
	for (--index; index >= 0; index--)
		platform_device_unregister(adata->pdev[index]);
de_init:
	if (acp63_deinit(adata->acp63_base, &pci->dev))
		dev_err(&pci->dev, "ACP de-init failed\n");
	return ret;
}

static int snd_acp63_probe(struct pci_dev *pci,
			   const struct pci_device_id *pci_id)
{
	struct acp63_dev_data *adata;
	u32 addr;
	u32 irqflags, flag;
	int val;
	int ret;

	irqflags = IRQF_SHARED;

	/* Return if acp config flag is defined */
	flag = snd_amd_acp_find_config(pci);
	if (flag)
		return -ENODEV;

	/* Pink Sardine device check */
	switch (pci->revision) {
	case 0x63:
		break;
	default:
		dev_dbg(&pci->dev, "acp63 pci device not found\n");
		return -ENODEV;
	}
	if (pci_enable_device(pci)) {
		dev_err(&pci->dev, "pci_enable_device failed\n");
		return -ENODEV;
	}

	ret = pci_request_regions(pci, "AMD ACP6.2 audio");
	if (ret < 0) {
		dev_err(&pci->dev, "pci_request_regions failed\n");
		goto disable_pci;
	}
	adata = devm_kzalloc(&pci->dev, sizeof(struct acp63_dev_data),
			     GFP_KERNEL);
	if (!adata) {
		ret = -ENOMEM;
		goto release_regions;
	}

	addr = pci_resource_start(pci, 0);
	adata->acp63_base = devm_ioremap(&pci->dev, addr,
					 pci_resource_len(pci, 0));
	if (!adata->acp63_base) {
		ret = -ENOMEM;
		goto release_regions;
	}
	/*
	 * By default acp_reset flag is set to true. i.e acp_deinit() and acp_init()
	 * will be invoked for all ACP configurations during suspend/resume callbacks.
	 * This flag should be set to false only when SoundWire manager power mode
	 * set to ClockStopMode.
	 */
	adata->acp_reset = true;
	pci_set_master(pci);
	pci_set_drvdata(pci, adata);
	mutex_init(&adata->acp_lock);
	ret = acp63_init(adata->acp63_base, &pci->dev);
	if (ret)
		goto release_regions;
	ret = devm_request_threaded_irq(&pci->dev, pci->irq, acp63_irq_handler,
					acp63_irq_thread, irqflags, "ACP_PCI_IRQ", adata);
	if (ret) {
		dev_err(&pci->dev, "ACP PCI IRQ request failed\n");
		goto de_init;
	}
	val = readl(adata->acp63_base + ACP_PIN_CONFIG);
	ret = get_acp63_device_config(val, pci, adata);
	/* ACP PCI driver probe should be continued even PDM or SoundWire Devices are not found */
	if (ret) {
		dev_dbg(&pci->dev, "get acp device config failed:%d\n", ret);
		goto skip_pdev_creation;
	}
	ret = create_acp63_platform_devs(pci, adata, addr);
	if (ret < 0) {
		dev_err(&pci->dev, "ACP platform devices creation failed\n");
		goto de_init;
	}
skip_pdev_creation:
	device_set_wakeup_enable(&pci->dev, true);
	pm_runtime_set_autosuspend_delay(&pci->dev, ACP_SUSPEND_DELAY_MS);
	pm_runtime_use_autosuspend(&pci->dev);
	pm_runtime_put_noidle(&pci->dev);
	pm_runtime_allow(&pci->dev);
	return 0;
de_init:
	if (acp63_deinit(adata->acp63_base, &pci->dev))
		dev_err(&pci->dev, "ACP de-init failed\n");
release_regions:
	pci_release_regions(pci);
disable_pci:
	pci_disable_device(pci);

	return ret;
}

static int __maybe_unused snd_acp63_suspend(struct device *dev)
{
	struct acp63_dev_data *adata;
	int ret = 0;

	adata = dev_get_drvdata(dev);
	if (adata->acp_reset) {
		ret = acp63_deinit(adata->acp63_base, dev);
		if (ret)
			dev_err(dev, "ACP de-init failed\n");
	}
	return ret;
}

static int __maybe_unused snd_acp63_resume(struct device *dev)
{
	struct acp63_dev_data *adata;
	int ret = 0;

	adata = dev_get_drvdata(dev);
	if (adata->acp_reset) {
		ret = acp63_init(adata->acp63_base, dev);
		if (ret)
			dev_err(dev, "ACP init failed\n");
	}
	return ret;
}

static const struct dev_pm_ops acp63_pm_ops = {
	SET_RUNTIME_PM_OPS(snd_acp63_suspend, snd_acp63_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(snd_acp63_suspend, snd_acp63_resume)
};

static void snd_acp63_remove(struct pci_dev *pci)
{
	struct acp63_dev_data *adata;
	int ret, index;

	adata = pci_get_drvdata(pci);
	for (index = 0; index < adata->pdev_count; index++)
		platform_device_unregister(adata->pdev[index]);
	ret = acp63_deinit(adata->acp63_base, &pci->dev);
	if (ret)
		dev_err(&pci->dev, "ACP de-init failed\n");
	pm_runtime_forbid(&pci->dev);
	pm_runtime_get_noresume(&pci->dev);
	pci_release_regions(pci);
	pci_disable_device(pci);
}

static const struct pci_device_id snd_acp63_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_AMD, ACP_DEVICE_ID),
	.class = PCI_CLASS_MULTIMEDIA_OTHER << 8,
	.class_mask = 0xffffff },
	{ 0, },
};
MODULE_DEVICE_TABLE(pci, snd_acp63_ids);

static struct pci_driver ps_acp63_driver  = {
	.name = KBUILD_MODNAME,
	.id_table = snd_acp63_ids,
	.probe = snd_acp63_probe,
	.remove = snd_acp63_remove,
	.driver = {
		.pm = &acp63_pm_ops,
	}
};

module_pci_driver(ps_acp63_driver);

MODULE_AUTHOR("Vijendar.Mukunda@amd.com");
MODULE_AUTHOR("Syed.SabaKareem@amd.com");
MODULE_DESCRIPTION("AMD ACP Pink Sardine PCI driver");
MODULE_LICENSE("GPL v2");
