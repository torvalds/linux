// SPDX-License-Identifier: GPL-2.0+
/*
 * AMD Yellow Carp ACP PCI Driver
 *
 * Copyright 2021 Advanced Micro Devices, Inc.
 */

#include <linux/pci.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <sound/pcm_params.h>
#include <linux/pm_runtime.h>

#include "acp6x.h"

struct acp6x_dev_data {
	void __iomem *acp6x_base;
	struct resource *res;
	bool acp6x_audio_mode;
	struct platform_device *pdev[ACP6x_DEVS];
};

static int acp6x_power_on(void __iomem *acp_base)
{
	u32 val;
	int timeout;

	val = acp6x_readl(acp_base + ACP_PGFSM_STATUS);

	if (!val)
		return val;

	if ((val & ACP_PGFSM_STATUS_MASK) != ACP_POWER_ON_IN_PROGRESS)
		acp6x_writel(ACP_PGFSM_CNTL_POWER_ON_MASK, acp_base + ACP_PGFSM_CONTROL);
	timeout = 0;
	while (++timeout < 500) {
		val = acp6x_readl(acp_base + ACP_PGFSM_STATUS);
		if (!val)
			return 0;
		udelay(1);
	}
	return -ETIMEDOUT;
}

static int acp6x_reset(void __iomem *acp_base)
{
	u32 val;
	int timeout;

	acp6x_writel(1, acp_base + ACP_SOFT_RESET);
	timeout = 0;
	while (++timeout < 500) {
		val = acp6x_readl(acp_base + ACP_SOFT_RESET);
		if (val & ACP_SOFT_RESET_SOFTRESET_AUDDONE_MASK)
			break;
		cpu_relax();
	}
	acp6x_writel(0, acp_base + ACP_SOFT_RESET);
	timeout = 0;
	while (++timeout < 500) {
		val = acp6x_readl(acp_base + ACP_SOFT_RESET);
		if (!val)
			return 0;
		cpu_relax();
	}
	return -ETIMEDOUT;
}

static void acp6x_enable_interrupts(void __iomem *acp_base)
{
	acp6x_writel(0x01, acp_base + ACP_EXTERNAL_INTR_ENB);
}

static void acp6x_disable_interrupts(void __iomem *acp_base)
{
	acp6x_writel(ACP_EXT_INTR_STAT_CLEAR_MASK, acp_base +
		     ACP_EXTERNAL_INTR_STAT);
	acp6x_writel(0x00, acp_base + ACP_EXTERNAL_INTR_CNTL);
	acp6x_writel(0x00, acp_base + ACP_EXTERNAL_INTR_ENB);
}

static int acp6x_init(void __iomem *acp_base)
{
	int ret;

	/* power on */
	ret = acp6x_power_on(acp_base);
	if (ret) {
		pr_err("ACP power on failed\n");
		return ret;
	}
	acp6x_writel(0x01, acp_base + ACP_CONTROL);
	/* Reset */
	ret = acp6x_reset(acp_base);
	if (ret) {
		pr_err("ACP reset failed\n");
		return ret;
	}
	acp6x_writel(0x03, acp_base + ACP_CLKMUX_SEL);
	acp6x_enable_interrupts(acp_base);
	return 0;
}

static int acp6x_deinit(void __iomem *acp_base)
{
	int ret;

	acp6x_disable_interrupts(acp_base);
	/* Reset */
	ret = acp6x_reset(acp_base);
	if (ret) {
		pr_err("ACP reset failed\n");
		return ret;
	}
	acp6x_writel(0x00, acp_base + ACP_CLKMUX_SEL);
	acp6x_writel(0x00, acp_base + ACP_CONTROL);
	return 0;
}

static irqreturn_t acp6x_irq_handler(int irq, void *dev_id)
{
	struct acp6x_dev_data *adata;
	struct pdm_dev_data *yc_pdm_data;
	u32 val;

	adata = dev_id;
	if (!adata)
		return IRQ_NONE;

	val = acp6x_readl(adata->acp6x_base + ACP_EXTERNAL_INTR_STAT);
	if (val & BIT(PDM_DMA_STAT)) {
		yc_pdm_data = dev_get_drvdata(&adata->pdev[0]->dev);
		acp6x_writel(BIT(PDM_DMA_STAT), adata->acp6x_base + ACP_EXTERNAL_INTR_STAT);
		if (yc_pdm_data->capture_stream)
			snd_pcm_period_elapsed(yc_pdm_data->capture_stream);
		return IRQ_HANDLED;
	}
	return IRQ_NONE;
}

static int snd_acp6x_probe(struct pci_dev *pci,
			   const struct pci_device_id *pci_id)
{
	struct acp6x_dev_data *adata;
	struct platform_device_info pdevinfo[ACP6x_DEVS];
	int index = 0;
	int val = 0x00;
	u32 addr;
	unsigned int irqflags;
	int ret;

	irqflags = IRQF_SHARED;
	/* Yellow Carp device check */
	if (pci->revision != 0x60)
		return -ENODEV;

	if (pci_enable_device(pci)) {
		dev_err(&pci->dev, "pci_enable_device failed\n");
		return -ENODEV;
	}

	ret = pci_request_regions(pci, "AMD ACP3x audio");
	if (ret < 0) {
		dev_err(&pci->dev, "pci_request_regions failed\n");
		goto disable_pci;
	}

	adata = devm_kzalloc(&pci->dev, sizeof(struct acp6x_dev_data),
			     GFP_KERNEL);
	if (!adata) {
		ret = -ENOMEM;
		goto release_regions;
	}

	addr = pci_resource_start(pci, 0);
	adata->acp6x_base = devm_ioremap(&pci->dev, addr,
					 pci_resource_len(pci, 0));
	if (!adata->acp6x_base) {
		ret = -ENOMEM;
		goto release_regions;
	}
	pci_set_master(pci);
	pci_set_drvdata(pci, adata);
	ret = acp6x_init(adata->acp6x_base);
	if (ret)
		goto release_regions;
	val = acp6x_readl(adata->acp6x_base + ACP_PIN_CONFIG);
	switch (val) {
	case ACP_CONFIG_0:
	case ACP_CONFIG_1:
	case ACP_CONFIG_2:
	case ACP_CONFIG_3:
	case ACP_CONFIG_9:
	case ACP_CONFIG_15:
		dev_info(&pci->dev, "Audio Mode %d\n", val);
		break;
	default:
		adata->res = devm_kzalloc(&pci->dev,
					  sizeof(struct resource),
					  GFP_KERNEL);
		if (!adata->res) {
			ret = -ENOMEM;
			goto de_init;
		}

		adata->res->name = "acp_iomem";
		adata->res->flags = IORESOURCE_MEM;
		adata->res->start = addr;
		adata->res->end = addr + (ACP6x_REG_END - ACP6x_REG_START);

		adata->acp6x_audio_mode = ACP6x_PDM_MODE;

		memset(&pdevinfo, 0, sizeof(pdevinfo));
		pdevinfo[0].name = "acp_yc_pdm_dma";
		pdevinfo[0].id = 0;
		pdevinfo[0].parent = &pci->dev;
		pdevinfo[0].num_res = 1;
		pdevinfo[0].res = adata->res;

		pdevinfo[1].name = "dmic-codec";
		pdevinfo[1].id = 0;
		pdevinfo[1].parent = &pci->dev;

		pdevinfo[2].name = "acp_yc_mach";
		pdevinfo[2].id = 0;
		pdevinfo[2].parent = &pci->dev;

		for (index = 0; index < ACP6x_DEVS; index++) {
			adata->pdev[index] =
				platform_device_register_full(&pdevinfo[index]);
			if (IS_ERR(adata->pdev[index])) {
				dev_err(&pci->dev, "cannot register %s device\n",
					pdevinfo[index].name);
				ret = PTR_ERR(adata->pdev[index]);
				goto unregister_devs;
			}
		}
		break;
	}
	ret = devm_request_irq(&pci->dev, pci->irq, acp6x_irq_handler,
			       irqflags, "ACP_PCI_IRQ", adata);
	if (ret) {
		dev_err(&pci->dev, "ACP PCI IRQ request failed\n");
		goto unregister_devs;
	}
	pm_runtime_set_autosuspend_delay(&pci->dev, ACP_SUSPEND_DELAY_MS);
	pm_runtime_use_autosuspend(&pci->dev);
	pm_runtime_put_noidle(&pci->dev);
	pm_runtime_allow(&pci->dev);

	return 0;
unregister_devs:
	for (--index; index >= 0; index--)
		platform_device_unregister(adata->pdev[index]);
de_init:
	if (acp6x_deinit(adata->acp6x_base))
		dev_err(&pci->dev, "ACP de-init failed\n");
release_regions:
	pci_release_regions(pci);
disable_pci:
	pci_disable_device(pci);

	return ret;
}

static int __maybe_unused snd_acp6x_suspend(struct device *dev)
{
	struct acp6x_dev_data *adata;
	int ret;

	adata = dev_get_drvdata(dev);
	ret = acp6x_deinit(adata->acp6x_base);
	if (ret)
		dev_err(dev, "ACP de-init failed\n");
	return ret;
}

static int __maybe_unused snd_acp6x_resume(struct device *dev)
{
	struct acp6x_dev_data *adata;
	int ret;

	adata = dev_get_drvdata(dev);
	ret = acp6x_init(adata->acp6x_base);
	if (ret)
		dev_err(dev, "ACP init failed\n");
	return ret;
}

static const struct dev_pm_ops acp6x_pm = {
	SET_RUNTIME_PM_OPS(snd_acp6x_suspend, snd_acp6x_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(snd_acp6x_suspend, snd_acp6x_resume)
};

static void snd_acp6x_remove(struct pci_dev *pci)
{
	struct acp6x_dev_data *adata;
	int ret, index;

	adata = pci_get_drvdata(pci);
	if (adata->acp6x_audio_mode == ACP6x_PDM_MODE) {
		for (index = 0; index < ACP6x_DEVS; index++)
			platform_device_unregister(adata->pdev[index]);
	}
	ret = acp6x_deinit(adata->acp6x_base);
	if (ret)
		dev_err(&pci->dev, "ACP de-init failed\n");
	pm_runtime_forbid(&pci->dev);
	pm_runtime_get_noresume(&pci->dev);
	pci_release_regions(pci);
	pci_disable_device(pci);
}

static const struct pci_device_id snd_acp6x_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_AMD, ACP_DEVICE_ID),
	.class = PCI_CLASS_MULTIMEDIA_OTHER << 8,
	.class_mask = 0xffffff },
	{ 0, },
};
MODULE_DEVICE_TABLE(pci, snd_acp6x_ids);

static struct pci_driver yc_acp6x_driver  = {
	.name = KBUILD_MODNAME,
	.id_table = snd_acp6x_ids,
	.probe = snd_acp6x_probe,
	.remove = snd_acp6x_remove,
	.driver = {
		.pm = &acp6x_pm,
	}
};

module_pci_driver(yc_acp6x_driver);

MODULE_AUTHOR("Vijendar.Mukunda@amd.com");
MODULE_DESCRIPTION("AMD ACP Yellow Carp PCI driver");
MODULE_LICENSE("GPL v2");
