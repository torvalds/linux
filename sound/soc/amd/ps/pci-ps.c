// SPDX-License-Identifier: GPL-2.0-only
/*
 * AMD common ACP PCI driver for ACP6.3, ACP7.0 & ACP7.1 platforms.
 *
 * Copyright 2022, 2025 Advanced Micro Devices, Inc.
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
#include "../mach-config.h"

#include "acp63.h"

static void handle_acp70_sdw_wake_event(struct acp63_dev_data *adata)
{
	struct amd_sdw_manager *amd_manager;

	if (adata->acp70_sdw0_wake_event) {
		amd_manager = dev_get_drvdata(&adata->sdw->pdev[0]->dev);
		if (amd_manager)
			pm_request_resume(amd_manager->dev);
		adata->acp70_sdw0_wake_event = 0;
	}

	if (adata->acp70_sdw1_wake_event) {
		amd_manager = dev_get_drvdata(&adata->sdw->pdev[1]->dev);
		if (amd_manager)
			pm_request_resume(amd_manager->dev);
		adata->acp70_sdw1_wake_event = 0;
	}
}

static short int check_and_handle_acp70_sdw_wake_irq(struct acp63_dev_data *adata)
{
	u32 ext_intr_stat1;
	int irq_flag = 0;
	bool sdw_wake_irq = false;

	ext_intr_stat1 = readl(adata->acp63_base + ACP_EXTERNAL_INTR_STAT1);
	if (ext_intr_stat1 & ACP70_SDW0_HOST_WAKE_STAT) {
		writel(ACP70_SDW0_HOST_WAKE_STAT, adata->acp63_base + ACP_EXTERNAL_INTR_STAT1);
		adata->acp70_sdw0_wake_event = true;
		sdw_wake_irq = true;
	}

	if (ext_intr_stat1 & ACP70_SDW1_HOST_WAKE_STAT) {
		writel(ACP70_SDW1_HOST_WAKE_STAT, adata->acp63_base + ACP_EXTERNAL_INTR_STAT1);
		adata->acp70_sdw1_wake_event = true;
		sdw_wake_irq = true;
	}

	if (ext_intr_stat1 & ACP70_SDW0_PME_STAT) {
		writel(0, adata->acp63_base + ACP_SW0_WAKE_EN);
		writel(ACP70_SDW0_PME_STAT, adata->acp63_base + ACP_EXTERNAL_INTR_STAT1);
		adata->acp70_sdw0_wake_event = true;
		sdw_wake_irq = true;
	}

	if (ext_intr_stat1 & ACP70_SDW1_PME_STAT) {
		writel(0, adata->acp63_base + ACP_SW1_WAKE_EN);
		writel(ACP70_SDW1_PME_STAT, adata->acp63_base + ACP_EXTERNAL_INTR_STAT1);
		adata->acp70_sdw1_wake_event = true;
		sdw_wake_irq = true;
	}

	if (sdw_wake_irq) {
		handle_acp70_sdw_wake_event(adata);
		irq_flag = 1;
	}
	return irq_flag;
}

static short int check_and_handle_sdw_dma_irq(struct acp63_dev_data *adata, u32 ext_intr_stat,
					      u32 ext_intr_stat1)
{
	u32 stream_id = 0;
	u16 sdw_dma_irq_flag = 0;
	u16 index;

	if (ext_intr_stat & ACP63_SDW_DMA_IRQ_MASK) {
		for (index = ACP_AUDIO2_RX_THRESHOLD; index <= ACP_AUDIO0_TX_THRESHOLD; index++) {
			if (ext_intr_stat & BIT(index)) {
				writel(BIT(index), adata->acp63_base + ACP_EXTERNAL_INTR_STAT);
				switch (index) {
				case ACP_AUDIO0_TX_THRESHOLD:
					stream_id = ACP63_SDW0_AUDIO0_TX;
					break;
				case ACP_AUDIO1_TX_THRESHOLD:
					stream_id = ACP63_SDW0_AUDIO1_TX;
					break;
				case ACP_AUDIO2_TX_THRESHOLD:
					stream_id = ACP63_SDW0_AUDIO2_TX;
					break;
				case ACP_AUDIO0_RX_THRESHOLD:
					stream_id = ACP63_SDW0_AUDIO0_RX;
					break;
				case ACP_AUDIO1_RX_THRESHOLD:
					stream_id = ACP63_SDW0_AUDIO1_RX;
					break;
				case ACP_AUDIO2_RX_THRESHOLD:
					stream_id = ACP63_SDW0_AUDIO2_RX;
					break;
				}
				switch (adata->acp_rev) {
				case ACP63_PCI_REV:
					adata->acp63_sdw0_dma_intr_stat[stream_id] = 1;
					break;
				case ACP70_PCI_REV:
				case ACP71_PCI_REV:
					adata->acp70_sdw0_dma_intr_stat[stream_id] = 1;
					break;
				}
				sdw_dma_irq_flag = 1;
			}
		}
	}
	switch (adata->acp_rev) {
	case ACP63_PCI_REV:
		if (ext_intr_stat1 & ACP63_P1_AUDIO1_RX_THRESHOLD) {
			writel(ACP63_P1_AUDIO1_RX_THRESHOLD,
			       adata->acp63_base + ACP_EXTERNAL_INTR_STAT1);
			adata->acp63_sdw1_dma_intr_stat[ACP63_SDW1_AUDIO1_RX] = 1;
			sdw_dma_irq_flag = 1;
		}
		if (ext_intr_stat1 & ACP63_P1_AUDIO1_TX_THRESHOLD) {
			writel(ACP63_P1_AUDIO1_TX_THRESHOLD,
			       adata->acp63_base + ACP_EXTERNAL_INTR_STAT1);
			adata->acp63_sdw1_dma_intr_stat[ACP63_SDW1_AUDIO1_TX] = 1;
			sdw_dma_irq_flag = 1;
		}
		break;
	case ACP70_PCI_REV:
	case ACP71_PCI_REV:
		if (ext_intr_stat1 & ACP70_P1_SDW_DMA_IRQ_MASK) {
			for (index = ACP70_P1_AUDIO2_RX_THRESHOLD;
			     index <= ACP70_P1_AUDIO0_TX_THRESHOLD; index++) {
				if (ext_intr_stat1 & BIT(index)) {
					writel(BIT(index),
					       adata->acp63_base + ACP_EXTERNAL_INTR_STAT1);
					switch (index) {
					case ACP70_P1_AUDIO0_TX_THRESHOLD:
						stream_id = ACP70_SDW_AUDIO0_TX;
						break;
					case ACP70_P1_AUDIO1_TX_THRESHOLD:
						stream_id = ACP70_SDW_AUDIO1_TX;
						break;
					case ACP70_P1_AUDIO2_TX_THRESHOLD:
						stream_id = ACP70_SDW_AUDIO2_TX;
						break;
					case ACP70_P1_AUDIO0_RX_THRESHOLD:
						stream_id = ACP70_SDW_AUDIO0_RX;
						break;
					case ACP70_P1_AUDIO1_RX_THRESHOLD:
						 stream_id = ACP70_SDW_AUDIO1_RX;
						break;
					case ACP70_P1_AUDIO2_RX_THRESHOLD:
						stream_id = ACP70_SDW_AUDIO2_RX;
						break;
					}

					adata->acp70_sdw1_dma_intr_stat[stream_id] = 1;
					sdw_dma_irq_flag = 1;
				}
			}
		}
		break;
	}
	return sdw_dma_irq_flag;
}

static irqreturn_t acp63_irq_thread(int irq, void *context)
{
	struct acp63_dev_data *adata = context;

	acp_hw_sdw_dma_irq_thread(adata);
	return IRQ_HANDLED;
}

static irqreturn_t acp63_irq_handler(int irq, void *dev_id)
{
	struct acp63_dev_data *adata;
	struct pdm_dev_data *ps_pdm_data;
	struct amd_sdw_manager *amd_manager;
	u32 ext_intr_stat, ext_intr_stat1;
	u16 irq_flag = 0;
	u16 wake_irq_flag = 0;
	u16 sdw_dma_irq_flag = 0;

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
		amd_manager = dev_get_drvdata(&adata->sdw->pdev[0]->dev);
		if (amd_manager)
			schedule_work(&amd_manager->amd_sdw_irq_thread);
		irq_flag = 1;
	}

	ext_intr_stat1 = readl(adata->acp63_base + ACP_EXTERNAL_INTR_STAT1);
	if (ext_intr_stat1 & ACP_SDW1_STAT) {
		writel(ACP_SDW1_STAT, adata->acp63_base + ACP_EXTERNAL_INTR_STAT1);
		amd_manager = dev_get_drvdata(&adata->sdw->pdev[1]->dev);
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

	if (adata->acp_rev >= ACP70_PCI_REV)
		wake_irq_flag = check_and_handle_acp70_sdw_wake_irq(adata);

	if (ext_intr_stat & BIT(PDM_DMA_STAT)) {
		ps_pdm_data = dev_get_drvdata(&adata->pdm_dev->dev);
		writel(BIT(PDM_DMA_STAT), adata->acp63_base + ACP_EXTERNAL_INTR_STAT);
		if (ps_pdm_data->capture_stream)
			snd_pcm_period_elapsed(ps_pdm_data->capture_stream);
		irq_flag = 1;
	}

	sdw_dma_irq_flag = check_and_handle_sdw_dma_irq(adata, ext_intr_stat, ext_intr_stat1);
	if (sdw_dma_irq_flag)
		return IRQ_WAKE_THREAD;

	if (irq_flag | wake_irq_flag)
		return IRQ_HANDLED;
	else
		return IRQ_NONE;
}

#if IS_ENABLED(CONFIG_SND_SOC_AMD_SOUNDWIRE)
static int acp_scan_sdw_devices(struct device *dev, u64 addr)
{
	struct acpi_device *sdw_dev;
	struct acp63_dev_data *acp_data;

	acp_data = dev_get_drvdata(dev);
	if (!addr)
		return -ENODEV;

	sdw_dev = acpi_find_child_device(ACPI_COMPANION(dev), addr, 0);
	if (!sdw_dev)
		return -ENODEV;

	acp_data->info.handle = sdw_dev->handle;
	acp_data->info.count = AMD_SDW_MAX_MANAGERS;
	return amd_sdw_scan_controller(&acp_data->info);
}

static int amd_sdw_probe(struct device *dev)
{
	struct acp63_dev_data *acp_data;
	struct sdw_amd_res sdw_res;
	int ret;

	acp_data = dev_get_drvdata(dev);
	memset(&sdw_res, 0, sizeof(sdw_res));
	sdw_res.addr = acp_data->addr;
	sdw_res.reg_range = acp_data->reg_range;
	sdw_res.handle = acp_data->info.handle;
	sdw_res.parent = dev;
	sdw_res.dev = dev;
	sdw_res.acp_lock = &acp_data->acp_lock;
	sdw_res.count = acp_data->info.count;
	sdw_res.mmio_base = acp_data->acp63_base;
	sdw_res.acp_rev = acp_data->acp_rev;
	sdw_res.link_mask = acp_data->info.link_mask;
	ret = sdw_amd_probe(&sdw_res, &acp_data->sdw);
	if (ret)
		dev_err(dev, "error: SoundWire probe failed\n");
	return ret;
}

static int amd_sdw_exit(struct acp63_dev_data *acp_data)
{
	if (acp_data->sdw)
		sdw_amd_exit(acp_data->sdw);
	acp_data->sdw = NULL;

	return 0;
}

static struct snd_soc_acpi_mach *acp63_sdw_machine_select(struct device *dev)
{
	struct snd_soc_acpi_mach *mach;
	const struct snd_soc_acpi_link_adr *link;
	struct acp63_dev_data *acp_data = dev_get_drvdata(dev);
	int ret, i;

	if (acp_data->info.count) {
		ret = sdw_amd_get_slave_info(acp_data->sdw);
		if (ret) {
			dev_dbg(dev, "failed to read slave information\n");
			return NULL;
		}
		for (mach = acp_data->machines; mach; mach++) {
			if (!mach->links)
				break;
			link = mach->links;
			for (i = 0; i < acp_data->info.count && link->num_adr; link++, i++) {
				if (!snd_soc_acpi_sdw_link_slaves_found(dev, link,
									acp_data->sdw->peripherals))
					break;
			}
			if (i == acp_data->info.count || !link->num_adr)
				break;
		}
		if (mach && mach->link_mask) {
			mach->mach_params.links = mach->links;
			mach->mach_params.link_mask = mach->link_mask;
			mach->mach_params.subsystem_rev = acp_data->acp_rev;
			return mach;
		}
	}
	dev_dbg(dev, "No SoundWire machine driver found\n");
	return NULL;
}
#else
static int acp_scan_sdw_devices(struct device *dev, u64 addr)
{
	return 0;
}

static int amd_sdw_probe(struct device *dev)
{
	return 0;
}

static int amd_sdw_exit(struct acp63_dev_data *acp_data)
{
	return 0;
}

static struct snd_soc_acpi_mach *acp63_sdw_machine_select(struct device *dev)
{
	return NULL;
}
#endif

static int acp63_machine_register(struct device *dev)
{
	struct snd_soc_acpi_mach *mach;
	struct acp63_dev_data *adata = dev_get_drvdata(dev);
	int size;

	if (adata->is_sdw_dev && adata->is_sdw_config) {
		size = sizeof(*adata->machines);
		mach = acp63_sdw_machine_select(dev);
		if (mach) {
			adata->mach_dev = platform_device_register_data(dev, mach->drv_name,
									PLATFORM_DEVID_NONE, mach,
									size);
			if (IS_ERR(adata->mach_dev)) {
				dev_err(dev,
					"cannot register Machine device for SoundWire Interface\n");
				return PTR_ERR(adata->mach_dev);
			}
		}

	} else if (adata->is_pdm_dev && !adata->is_sdw_dev && adata->is_pdm_config) {
		adata->mach_dev = platform_device_register_data(dev, "acp_ps_mach",
								PLATFORM_DEVID_NONE, NULL, 0);
		if (IS_ERR(adata->mach_dev)) {
			dev_err(dev, "cannot register amd_ps_mach device\n");
			return PTR_ERR(adata->mach_dev);
		}
	}
	return 0;
}

static int get_acp63_device_config(struct pci_dev *pci, struct acp63_dev_data *acp_data)
{
	struct acpi_device *pdm_dev;
	const union acpi_object *obj;
	acpi_handle handle;
	acpi_integer dmic_status;
	bool is_dmic_dev = false;
	bool is_sdw_dev = false;
	bool wov_en, dmic_en;
	int ret;

	/* IF WOV entry not found, enable dmic based on acp-audio-device-type entry*/
	wov_en = true;
	dmic_en = false;

	acp_hw_get_config(pci, acp_data);

	if (acp_data->is_pdm_config) {
		pdm_dev = acpi_find_child_device(ACPI_COMPANION(&pci->dev), ACP63_DMIC_ADDR, 0);
		if (pdm_dev) {
			/* is_dmic_dev flag will be set when ACP PDM controller device exists */
			if (!acpi_dev_get_property(pdm_dev, "acp-audio-device-type",
						   ACPI_TYPE_INTEGER, &obj) &&
						   obj->integer.value == ACP_DMIC_DEV)
				dmic_en = true;
		}

		handle = ACPI_HANDLE(&pci->dev);
		ret = acpi_evaluate_integer(handle, "_WOV", NULL, &dmic_status);
		if (!ACPI_FAILURE(ret))
			wov_en = dmic_status;
	}

	if (dmic_en && wov_en)
		is_dmic_dev = true;

	if (acp_data->is_sdw_config) {
		ret = acp_scan_sdw_devices(&pci->dev, ACP63_SDW_ADDR);
		if (!ret && acp_data->info.link_mask)
			is_sdw_dev = true;
	}

	acp_data->is_pdm_dev = is_dmic_dev;
	acp_data->is_sdw_dev = is_sdw_dev;
	if (!is_dmic_dev && !is_sdw_dev) {
		dev_dbg(&pci->dev, "No PDM or SoundWire manager devices found\n");
		return -ENODEV;
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
	struct platform_device_info pdevinfo;
	struct device *parent;
	int ret;

	parent = &pci->dev;

	if (adata->is_sdw_dev || adata->is_pdm_dev) {
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

	if (adata->is_pdm_dev && adata->is_pdm_config) {
		acp63_fill_platform_dev_info(&pdevinfo, parent, NULL, "acp_ps_pdm_dma",
					     0, adata->res, 1, NULL, 0);

		adata->pdm_dev = platform_device_register_full(&pdevinfo);
		if (IS_ERR(adata->pdm_dev)) {
			dev_err(&pci->dev,
				"cannot register %s device\n", pdevinfo.name);
			ret = PTR_ERR(adata->pdm_dev);
			goto de_init;
		}
		memset(&pdevinfo, 0, sizeof(pdevinfo));
		acp63_fill_platform_dev_info(&pdevinfo, parent, NULL, "dmic-codec",
					     0, NULL, 0, NULL, 0);
		adata->dmic_codec_dev = platform_device_register_full(&pdevinfo);
		if (IS_ERR(adata->dmic_codec_dev)) {
			dev_err(&pci->dev,
				"cannot register %s device\n", pdevinfo.name);
			ret = PTR_ERR(adata->dmic_codec_dev);
			goto unregister_pdm_dev;
		}
	}
	if (adata->is_sdw_dev && adata->is_sdw_config) {
		ret = amd_sdw_probe(&pci->dev);
		if (ret) {
			if (adata->is_pdm_dev)
				goto unregister_dmic_codec_dev;
			else
				goto de_init;
		}
		memset(&pdevinfo, 0, sizeof(pdevinfo));
		acp63_fill_platform_dev_info(&pdevinfo, parent, NULL, "amd_ps_sdw_dma",
					     0, adata->res, 1, NULL, 0);

		adata->sdw_dma_dev = platform_device_register_full(&pdevinfo);
		if (IS_ERR(adata->sdw_dma_dev)) {
			dev_err(&pci->dev,
				"cannot register %s device\n", pdevinfo.name);
			ret = PTR_ERR(adata->sdw_dma_dev);
			if (adata->is_pdm_dev)
				goto unregister_dmic_codec_dev;
			else
				goto de_init;
		}
	}

	return 0;
unregister_dmic_codec_dev:
		platform_device_unregister(adata->dmic_codec_dev);
unregister_pdm_dev:
		platform_device_unregister(adata->pdm_dev);
de_init:
	if (acp_hw_deinit(adata, &pci->dev))
		dev_err(&pci->dev, "ACP de-init failed\n");
	return ret;
}

static int acp_hw_init_ops(struct acp63_dev_data *adata, struct pci_dev *pci)
{
	adata->hw_ops = devm_kzalloc(&pci->dev, sizeof(struct acp_hw_ops),
				     GFP_KERNEL);
	if (!adata->hw_ops)
		return -ENOMEM;

	switch (adata->acp_rev) {
	case ACP63_PCI_REV:
		acp63_hw_init_ops(adata->hw_ops);
		break;
	case ACP70_PCI_REV:
	case ACP71_PCI_REV:
		acp70_hw_init_ops(adata->hw_ops);
		break;
	default:
		dev_err(&pci->dev, "ACP device not found\n");
		return -ENODEV;
	}
	return 0;
}

static int snd_acp63_probe(struct pci_dev *pci,
			   const struct pci_device_id *pci_id)
{
	struct acp63_dev_data *adata;
	u32 addr;
	u32 irqflags, flag;
	int ret;

	irqflags = IRQF_SHARED;

	/* Return if acp config flag is defined */
	flag = snd_amd_acp_find_config(pci);
	if (flag)
		return -ENODEV;

	/* ACP PCI revision id check for ACP6.3, ACP7.0 & ACP7.1 platforms */
	switch (pci->revision) {
	case ACP63_PCI_REV:
	case ACP70_PCI_REV:
	case ACP71_PCI_REV:
		break;
	default:
		dev_dbg(&pci->dev, "acp63/acp70/acp71 pci device not found\n");
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
	adata->addr = addr;
	adata->reg_range = ACP63_REG_END - ACP63_REG_START;
	adata->acp_rev = pci->revision;
	pci_set_master(pci);
	pci_set_drvdata(pci, adata);
	mutex_init(&adata->acp_lock);
	ret = acp_hw_init_ops(adata, pci);
	if (ret) {
		dev_err(&pci->dev, "ACP hw ops init failed\n");
		goto release_regions;
	}
	ret = acp_hw_init(adata, &pci->dev);
	if (ret)
		goto release_regions;
	ret = devm_request_threaded_irq(&pci->dev, pci->irq, acp63_irq_handler,
					acp63_irq_thread, irqflags, "ACP_PCI_IRQ", adata);
	if (ret) {
		dev_err(&pci->dev, "ACP PCI IRQ request failed\n");
		goto de_init;
	}
	ret = get_acp63_device_config(pci, adata);
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
	if (adata->acp_rev >= ACP70_PCI_REV)
		adata->machines = snd_soc_acpi_amd_acp70_sdw_machines;
	else
		adata->machines = snd_soc_acpi_amd_acp63_sdw_machines;

	ret = acp63_machine_register(&pci->dev);
	if (ret) {
		dev_err(&pci->dev, "ACP machine register failed\n");
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
	if (acp_hw_deinit(adata, &pci->dev))
		dev_err(&pci->dev, "ACP de-init failed\n");
release_regions:
	pci_release_regions(pci);
disable_pci:
	pci_disable_device(pci);

	return ret;
}

static int snd_acp_suspend(struct device *dev)
{
	return acp_hw_suspend(dev);
}

static int snd_acp_runtime_resume(struct device *dev)
{
	return acp_hw_runtime_resume(dev);
}

static int snd_acp_resume(struct device *dev)
{
	return acp_hw_resume(dev);
}

static const struct dev_pm_ops acp63_pm_ops = {
	RUNTIME_PM_OPS(snd_acp_suspend, snd_acp_runtime_resume, NULL)
	SYSTEM_SLEEP_PM_OPS(snd_acp_suspend, snd_acp_resume)
};

static void snd_acp63_remove(struct pci_dev *pci)
{
	struct acp63_dev_data *adata;
	int ret;

	adata = pci_get_drvdata(pci);
	if (adata->sdw) {
		amd_sdw_exit(adata);
		platform_device_unregister(adata->sdw_dma_dev);
	}
	if (adata->is_pdm_dev) {
		platform_device_unregister(adata->pdm_dev);
		platform_device_unregister(adata->dmic_codec_dev);
	}
	if (adata->mach_dev)
		platform_device_unregister(adata->mach_dev);
	ret = acp_hw_deinit(adata, &pci->dev);
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
		.pm = pm_ptr(&acp63_pm_ops),
	}
};

module_pci_driver(ps_acp63_driver);

MODULE_AUTHOR("Vijendar.Mukunda@amd.com");
MODULE_AUTHOR("Syed.SabaKareem@amd.com");
MODULE_DESCRIPTION("AMD common ACP PCI driver for ACP6.3, ACP7.0 & ACP7.1 platforms");
MODULE_IMPORT_NS("SOUNDWIRE_AMD_INIT");
MODULE_IMPORT_NS("SND_AMD_SOUNDWIRE_ACPI");
MODULE_LICENSE("GPL v2");
