// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright(c) 2020 Intel Corporation. All rights reserved.
//
// Author: Cezary Rojewski <cezary.rojewski@intel.com>
//
// Special thanks to:
//    Marcin Barlik <marcin.barlik@intel.com>
//    Piotr Papierkowski <piotr.papierkowski@intel.com>
//
// for sharing LPT-LP and WTP-LP AudioDSP architecture expertise and
// helping backtrack its historical background
//

#include <linux/acpi.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <sound/intel-dsp-config.h>
#include <sound/soc.h>
#include <sound/soc-acpi.h>
#include <sound/soc-acpi-intel-match.h>
#include "core.h"
#include "registers.h"

#define CREATE_TRACE_POINTS
#include "trace.h"

static int __maybe_unused catpt_suspend(struct device *dev)
{
	struct catpt_dev *cdev = dev_get_drvdata(dev);
	struct dma_chan *chan;
	int ret;

	chan = catpt_dma_request_config_chan(cdev);
	if (IS_ERR(chan))
		return PTR_ERR(chan);

	memset(&cdev->dx_ctx, 0, sizeof(cdev->dx_ctx));
	ret = catpt_ipc_enter_dxstate(cdev, CATPT_DX_STATE_D3, &cdev->dx_ctx);
	if (ret) {
		ret = CATPT_IPC_ERROR(ret);
		goto release_dma_chan;
	}

	ret = catpt_dsp_stall(cdev, true);
	if (ret)
		goto release_dma_chan;

	ret = catpt_store_memdumps(cdev, chan);
	if (ret) {
		dev_err(cdev->dev, "store memdumps failed: %d\n", ret);
		goto release_dma_chan;
	}

	ret = catpt_store_module_states(cdev, chan);
	if (ret) {
		dev_err(cdev->dev, "store module states failed: %d\n", ret);
		goto release_dma_chan;
	}

	ret = catpt_store_streams_context(cdev, chan);
	if (ret)
		dev_err(cdev->dev, "store streams ctx failed: %d\n", ret);

release_dma_chan:
	dma_release_channel(chan);
	if (ret)
		return ret;
	return catpt_dsp_power_down(cdev);
}

static int __maybe_unused catpt_resume(struct device *dev)
{
	struct catpt_dev *cdev = dev_get_drvdata(dev);
	int ret, i;

	ret = catpt_dsp_power_up(cdev);
	if (ret)
		return ret;

	if (!try_module_get(dev->driver->owner)) {
		dev_info(dev, "module unloading, skipping fw boot\n");
		return 0;
	}
	module_put(dev->driver->owner);

	ret = catpt_boot_firmware(cdev, true);
	if (ret) {
		dev_err(cdev->dev, "boot firmware failed: %d\n", ret);
		return ret;
	}

	/* reconfigure SSP devices after Dx transition */
	for (i = 0; i < CATPT_SSP_COUNT; i++) {
		if (cdev->devfmt[i].iface == UINT_MAX)
			continue;

		ret = catpt_ipc_set_device_format(cdev, &cdev->devfmt[i]);
		if (ret)
			return CATPT_IPC_ERROR(ret);
	}

	return 0;
}

static int __maybe_unused catpt_runtime_suspend(struct device *dev)
{
	if (!try_module_get(dev->driver->owner)) {
		dev_info(dev, "module unloading, skipping suspend\n");
		return 0;
	}
	module_put(dev->driver->owner);

	return catpt_suspend(dev);
}

static int __maybe_unused catpt_runtime_resume(struct device *dev)
{
	return catpt_resume(dev);
}

static const struct dev_pm_ops catpt_dev_pm = {
	SET_SYSTEM_SLEEP_PM_OPS(catpt_suspend, catpt_resume)
	SET_RUNTIME_PM_OPS(catpt_runtime_suspend, catpt_runtime_resume, NULL)
};

/* machine board owned by CATPT is removed with this hook */
static void board_pdev_unregister(void *data)
{
	platform_device_unregister(data);
}

static int catpt_register_board(struct catpt_dev *cdev)
{
	const struct catpt_spec *spec = cdev->spec;
	struct snd_soc_acpi_mach *mach;
	struct platform_device *board;

	mach = snd_soc_acpi_find_machine(spec->machines);
	if (!mach) {
		dev_info(cdev->dev, "no machines present\n");
		return 0;
	}

	mach->mach_params.platform = "catpt-platform";
	board = platform_device_register_data(NULL, mach->drv_name,
					PLATFORM_DEVID_NONE,
					(const void *)mach, sizeof(*mach));
	if (IS_ERR(board)) {
		dev_err(cdev->dev, "board register failed\n");
		return PTR_ERR(board);
	}

	return devm_add_action_or_reset(cdev->dev, board_pdev_unregister,
					board);
}

static int catpt_probe_components(struct catpt_dev *cdev)
{
	int ret;

	ret = catpt_dsp_power_up(cdev);
	if (ret)
		return ret;

	ret = catpt_dmac_probe(cdev);
	if (ret) {
		dev_err(cdev->dev, "DMAC probe failed: %d\n", ret);
		goto err_dmac_probe;
	}

	ret = catpt_first_boot_firmware(cdev);
	if (ret) {
		dev_err(cdev->dev, "first fw boot failed: %d\n", ret);
		goto err_boot_fw;
	}

	ret = catpt_register_plat_component(cdev);
	if (ret) {
		dev_err(cdev->dev, "register plat comp failed: %d\n", ret);
		goto err_boot_fw;
	}

	ret = catpt_register_board(cdev);
	if (ret) {
		dev_err(cdev->dev, "register board failed: %d\n", ret);
		goto err_reg_board;
	}

	/* reflect actual ADSP state in pm_runtime */
	pm_runtime_set_active(cdev->dev);

	pm_runtime_set_autosuspend_delay(cdev->dev, 2000);
	pm_runtime_use_autosuspend(cdev->dev);
	pm_runtime_mark_last_busy(cdev->dev);
	pm_runtime_enable(cdev->dev);
	return 0;

err_reg_board:
	snd_soc_unregister_component(cdev->dev);
err_boot_fw:
	catpt_dmac_remove(cdev);
err_dmac_probe:
	catpt_dsp_power_down(cdev);

	return ret;
}

static void catpt_dev_init(struct catpt_dev *cdev, struct device *dev,
			   const struct catpt_spec *spec)
{
	cdev->dev = dev;
	cdev->spec = spec;
	init_completion(&cdev->fw_ready);
	INIT_LIST_HEAD(&cdev->stream_list);
	spin_lock_init(&cdev->list_lock);
	mutex_init(&cdev->clk_mutex);

	/*
	 * Mark both device formats as uninitialized. Once corresponding
	 * cpu_dai's pcm is created, proper values are assigned.
	 */
	cdev->devfmt[CATPT_SSP_IFACE_0].iface = UINT_MAX;
	cdev->devfmt[CATPT_SSP_IFACE_1].iface = UINT_MAX;

	catpt_ipc_init(&cdev->ipc, dev);

	catpt_sram_init(&cdev->dram, spec->host_dram_offset,
			catpt_dram_size(cdev));
	catpt_sram_init(&cdev->iram, spec->host_iram_offset,
			catpt_iram_size(cdev));
}

static int catpt_acpi_probe(struct platform_device *pdev)
{
	const struct catpt_spec *spec;
	struct catpt_dev *cdev;
	struct device *dev = &pdev->dev;
	const struct acpi_device_id *id;
	struct resource *res;
	int ret;

	id = acpi_match_device(dev->driver->acpi_match_table, dev);
	if (!id)
		return -ENODEV;

	ret = snd_intel_acpi_dsp_driver_probe(dev, id->id);
	if (ret != SND_INTEL_DSP_DRIVER_ANY && ret != SND_INTEL_DSP_DRIVER_SST) {
		dev_dbg(dev, "CATPT ACPI driver not selected, aborting probe\n");
		return -ENODEV;
	}

	spec = device_get_match_data(dev);
	if (!spec)
		return -ENODEV;

	cdev = devm_kzalloc(dev, sizeof(*cdev), GFP_KERNEL);
	if (!cdev)
		return -ENOMEM;

	catpt_dev_init(cdev, dev, spec);

	/* map DSP bar address */
	cdev->lpe_ba = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(cdev->lpe_ba))
		return PTR_ERR(cdev->lpe_ba);
	cdev->lpe_base = res->start;

	/* map PCI bar address */
	cdev->pci_ba = devm_platform_ioremap_resource(pdev, 1);
	if (IS_ERR(cdev->pci_ba))
		return PTR_ERR(cdev->pci_ba);

	/* alloc buffer for storing DRAM context during dx transitions */
	cdev->dxbuf_vaddr = dmam_alloc_coherent(dev, catpt_dram_size(cdev),
						&cdev->dxbuf_paddr, GFP_KERNEL);
	if (!cdev->dxbuf_vaddr)
		return -ENOMEM;

	ret = platform_get_irq(pdev, 0);
	if (ret < 0)
		return ret;
	cdev->irq = ret;

	platform_set_drvdata(pdev, cdev);

	ret = devm_request_threaded_irq(dev, cdev->irq, catpt_dsp_irq_handler,
					catpt_dsp_irq_thread,
					IRQF_SHARED, "AudioDSP", cdev);
	if (ret)
		return ret;

	return catpt_probe_components(cdev);
}

static int catpt_acpi_remove(struct platform_device *pdev)
{
	struct catpt_dev *cdev = platform_get_drvdata(pdev);

	pm_runtime_disable(cdev->dev);

	snd_soc_unregister_component(cdev->dev);
	catpt_dmac_remove(cdev);
	catpt_dsp_power_down(cdev);

	catpt_sram_free(&cdev->iram);
	catpt_sram_free(&cdev->dram);

	return 0;
}

static struct catpt_spec lpt_desc = {
	.machines = snd_soc_acpi_intel_haswell_machines,
	.core_id = 0x01,
	.host_dram_offset = 0x000000,
	.host_iram_offset = 0x080000,
	.host_shim_offset = 0x0E7000,
	.host_dma_offset = { 0x0F0000, 0x0F8000 },
	.host_ssp_offset = { 0x0E8000, 0x0E9000 },
	.dram_mask = LPT_VDRTCTL0_DSRAMPGE_MASK,
	.iram_mask = LPT_VDRTCTL0_ISRAMPGE_MASK,
	.d3srampgd_bit = LPT_VDRTCTL0_D3SRAMPGD,
	.d3pgd_bit = LPT_VDRTCTL0_D3PGD,
	.pll_shutdown = lpt_dsp_pll_shutdown,
};

static struct catpt_spec wpt_desc = {
	.machines = snd_soc_acpi_intel_broadwell_machines,
	.core_id = 0x02,
	.host_dram_offset = 0x000000,
	.host_iram_offset = 0x0A0000,
	.host_shim_offset = 0x0FB000,
	.host_dma_offset = { 0x0FE000, 0x0FF000 },
	.host_ssp_offset = { 0x0FC000, 0x0FD000 },
	.dram_mask = WPT_VDRTCTL0_DSRAMPGE_MASK,
	.iram_mask = WPT_VDRTCTL0_ISRAMPGE_MASK,
	.d3srampgd_bit = WPT_VDRTCTL0_D3SRAMPGD,
	.d3pgd_bit = WPT_VDRTCTL0_D3PGD,
	.pll_shutdown = wpt_dsp_pll_shutdown,
};

static const struct acpi_device_id catpt_ids[] = {
	{ "INT33C8", (unsigned long)&lpt_desc },
	{ "INT3438", (unsigned long)&wpt_desc },
	{ }
};
MODULE_DEVICE_TABLE(acpi, catpt_ids);

static struct platform_driver catpt_acpi_driver = {
	.probe = catpt_acpi_probe,
	.remove = catpt_acpi_remove,
	.driver = {
		.name = "intel_catpt",
		.acpi_match_table = catpt_ids,
		.pm = &catpt_dev_pm,
		.dev_groups = catpt_attr_groups,
	},
};
module_platform_driver(catpt_acpi_driver);

MODULE_AUTHOR("Cezary Rojewski <cezary.rojewski@intel.com>");
MODULE_DESCRIPTION("Intel LPT/WPT AudioDSP driver");
MODULE_LICENSE("GPL v2");
