/* i915_drv.c -- i830,i845,i855,i865,i915 driver -*- linux-c -*-
 */
/*
 *
 * Copyright 2003 Tungsten Graphics, Inc., Cedar Park, Texas.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL TUNGSTEN GRAPHICS AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include <linux/acpi.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/oom.h>
#include <linux/pci.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/string_helpers.h>
#include <linux/vga_switcheroo.h>
#include <linux/vt.h>

#include <drm/drm_aperture.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_ioctl.h>
#include <drm/drm_managed.h>
#include <drm/drm_probe_helper.h>

#include "display/intel_acpi.h"
#include "display/intel_bw.h"
#include "display/intel_cdclk.h"
#include "display/intel_display_driver.h"
#include "display/intel_display.h"
#include "display/intel_dmc.h"
#include "display/intel_dp.h"
#include "display/intel_dpt.h"
#include "display/intel_encoder.h"
#include "display/intel_fbdev.h"
#include "display/intel_hotplug.h"
#include "display/intel_overlay.h"
#include "display/intel_pch_refclk.h"
#include "display/intel_pps.h"
#include "display/intel_sprite.h"
#include "display/skl_watermark.h"

#include "gem/i915_gem_context.h"
#include "gem/i915_gem_create.h"
#include "gem/i915_gem_dmabuf.h"
#include "gem/i915_gem_ioctls.h"
#include "gem/i915_gem_mman.h"
#include "gem/i915_gem_pm.h"
#include "gt/intel_gt.h"
#include "gt/intel_gt_pm.h"
#include "gt/intel_gt_print.h"
#include "gt/intel_rc6.h"

#include "pxp/intel_pxp.h"
#include "pxp/intel_pxp_debugfs.h"
#include "pxp/intel_pxp_pm.h"

#include "soc/intel_dram.h"
#include "soc/intel_gmch.h"

#include "i915_debugfs.h"
#include "i915_driver.h"
#include "i915_drm_client.h"
#include "i915_drv.h"
#include "i915_file_private.h"
#include "i915_getparam.h"
#include "i915_hwmon.h"
#include "i915_ioc32.h"
#include "i915_ioctl.h"
#include "i915_irq.h"
#include "i915_memcpy.h"
#include "i915_perf.h"
#include "i915_query.h"
#include "i915_suspend.h"
#include "i915_switcheroo.h"
#include "i915_sysfs.h"
#include "i915_utils.h"
#include "i915_vgpu.h"
#include "intel_clock_gating.h"
#include "intel_gvt.h"
#include "intel_memory_region.h"
#include "intel_pci_config.h"
#include "intel_pcode.h"
#include "intel_region_ttm.h"
#include "vlv_suspend.h"

static const struct drm_driver i915_drm_driver;

static int i915_workqueues_init(struct drm_i915_private *dev_priv)
{
	/*
	 * The i915 workqueue is primarily used for batched retirement of
	 * requests (and thus managing bo) once the task has been completed
	 * by the GPU. i915_retire_requests() is called directly when we
	 * need high-priority retirement, such as waiting for an explicit
	 * bo.
	 *
	 * It is also used for periodic low-priority events, such as
	 * idle-timers and recording error state.
	 *
	 * All tasks on the workqueue are expected to acquire the dev mutex
	 * so there is no point in running more than one instance of the
	 * workqueue at any time.  Use an ordered one.
	 */
	dev_priv->wq = alloc_ordered_workqueue("i915", 0);
	if (dev_priv->wq == NULL)
		goto out_err;

	dev_priv->display.hotplug.dp_wq = alloc_ordered_workqueue("i915-dp", 0);
	if (dev_priv->display.hotplug.dp_wq == NULL)
		goto out_free_wq;

	/*
	 * The unordered i915 workqueue should be used for all work
	 * scheduling that do not require running in order, which used
	 * to be scheduled on the system_wq before moving to a driver
	 * instance due deprecation of flush_scheduled_work().
	 */
	dev_priv->unordered_wq = alloc_workqueue("i915-unordered", 0, 0);
	if (dev_priv->unordered_wq == NULL)
		goto out_free_dp_wq;

	return 0;

out_free_dp_wq:
	destroy_workqueue(dev_priv->display.hotplug.dp_wq);
out_free_wq:
	destroy_workqueue(dev_priv->wq);
out_err:
	drm_err(&dev_priv->drm, "Failed to allocate workqueues.\n");

	return -ENOMEM;
}

static void i915_workqueues_cleanup(struct drm_i915_private *dev_priv)
{
	destroy_workqueue(dev_priv->unordered_wq);
	destroy_workqueue(dev_priv->display.hotplug.dp_wq);
	destroy_workqueue(dev_priv->wq);
}

/*
 * We don't keep the workarounds for pre-production hardware, so we expect our
 * driver to fail on these machines in one way or another. A little warning on
 * dmesg may help both the user and the bug triagers.
 *
 * Our policy for removing pre-production workarounds is to keep the
 * current gen workarounds as a guide to the bring-up of the next gen
 * (workarounds have a habit of persisting!). Anything older than that
 * should be removed along with the complications they introduce.
 */
static void intel_detect_preproduction_hw(struct drm_i915_private *dev_priv)
{
	bool pre = false;

	pre |= IS_HASWELL_EARLY_SDV(dev_priv);
	pre |= IS_SKYLAKE(dev_priv) && INTEL_REVID(dev_priv) < 0x6;
	pre |= IS_BROXTON(dev_priv) && INTEL_REVID(dev_priv) < 0xA;
	pre |= IS_KABYLAKE(dev_priv) && INTEL_REVID(dev_priv) < 0x1;
	pre |= IS_GEMINILAKE(dev_priv) && INTEL_REVID(dev_priv) < 0x3;
	pre |= IS_ICELAKE(dev_priv) && INTEL_REVID(dev_priv) < 0x7;
	pre |= IS_TIGERLAKE(dev_priv) && INTEL_REVID(dev_priv) < 0x1;
	pre |= IS_DG1(dev_priv) && INTEL_REVID(dev_priv) < 0x1;
	pre |= IS_DG2_G10(dev_priv) && INTEL_REVID(dev_priv) < 0x8;
	pre |= IS_DG2_G11(dev_priv) && INTEL_REVID(dev_priv) < 0x5;
	pre |= IS_DG2_G12(dev_priv) && INTEL_REVID(dev_priv) < 0x1;

	if (pre) {
		drm_err(&dev_priv->drm, "This is a pre-production stepping. "
			  "It may not be fully functional.\n");
		add_taint(TAINT_MACHINE_CHECK, LOCKDEP_STILL_OK);
	}
}

static void sanitize_gpu(struct drm_i915_private *i915)
{
	if (!INTEL_INFO(i915)->gpu_reset_clobbers_display) {
		struct intel_gt *gt;
		unsigned int i;

		for_each_gt(gt, i915, i)
			intel_gt_reset_all_engines(gt);
	}
}

/**
 * i915_driver_early_probe - setup state not requiring device access
 * @dev_priv: device private
 *
 * Initialize everything that is a "SW-only" state, that is state not
 * requiring accessing the device or exposing the driver via kernel internal
 * or userspace interfaces. Example steps belonging here: lock initialization,
 * system memory allocation, setting up device specific attributes and
 * function hooks not requiring accessing the device.
 */
static int i915_driver_early_probe(struct drm_i915_private *dev_priv)
{
	int ret = 0;

	if (i915_inject_probe_failure(dev_priv))
		return -ENODEV;

	intel_device_info_runtime_init_early(dev_priv);

	intel_step_init(dev_priv);

	intel_uncore_mmio_debug_init_early(dev_priv);

	mtx_init(&dev_priv->irq_lock, IPL_TTY);
	mtx_init(&dev_priv->gpu_error.lock, IPL_TTY);

	rw_init(&dev_priv->sb_lock, "sb");
	cpu_latency_qos_add_request(&dev_priv->sb_qos, PM_QOS_DEFAULT_VALUE);

	i915_memcpy_init_early(dev_priv);
	intel_runtime_pm_init_early(&dev_priv->runtime_pm);

	ret = i915_workqueues_init(dev_priv);
	if (ret < 0)
		return ret;

	ret = vlv_suspend_init(dev_priv);
	if (ret < 0)
		goto err_workqueues;

#ifdef __OpenBSD__
	dev_priv->bdev.iot = dev_priv->iot;
	dev_priv->bdev.memt = dev_priv->bst;
	dev_priv->bdev.dmat = dev_priv->dmat;
#endif

	ret = intel_region_ttm_device_init(dev_priv);
	if (ret)
		goto err_ttm;

	ret = intel_root_gt_init_early(dev_priv);
	if (ret < 0)
		goto err_rootgt;

	i915_gem_init_early(dev_priv);

	/* This must be called before any calls to HAS_PCH_* */
	intel_detect_pch(dev_priv);

	intel_irq_init(dev_priv);
	intel_display_driver_early_probe(dev_priv);
	intel_clock_gating_hooks_init(dev_priv);

	intel_detect_preproduction_hw(dev_priv);

	return 0;

err_rootgt:
	intel_region_ttm_device_fini(dev_priv);
err_ttm:
	vlv_suspend_cleanup(dev_priv);
err_workqueues:
	i915_workqueues_cleanup(dev_priv);
	return ret;
}

/**
 * i915_driver_late_release - cleanup the setup done in
 *			       i915_driver_early_probe()
 * @dev_priv: device private
 */
static void i915_driver_late_release(struct drm_i915_private *dev_priv)
{
	intel_irq_fini(dev_priv);
	intel_power_domains_cleanup(dev_priv);
	i915_gem_cleanup_early(dev_priv);
	intel_gt_driver_late_release_all(dev_priv);
	intel_region_ttm_device_fini(dev_priv);
	vlv_suspend_cleanup(dev_priv);
	i915_workqueues_cleanup(dev_priv);

	cpu_latency_qos_remove_request(&dev_priv->sb_qos);
	mutex_destroy(&dev_priv->sb_lock);

	i915_params_free(&dev_priv->params);
}

/**
 * i915_driver_mmio_probe - setup device MMIO
 * @dev_priv: device private
 *
 * Setup minimal device state necessary for MMIO accesses later in the
 * initialization sequence. The setup here should avoid any other device-wide
 * side effects or exposing the driver via kernel internal or user space
 * interfaces.
 */
static int i915_driver_mmio_probe(struct drm_i915_private *dev_priv)
{
	struct intel_gt *gt;
	int ret, i;

	if (i915_inject_probe_failure(dev_priv))
		return -ENODEV;

	ret = intel_gmch_bridge_setup(dev_priv);
	if (ret < 0)
		return ret;

	for_each_gt(gt, dev_priv, i) {
		ret = intel_uncore_init_mmio(gt->uncore);
		if (ret)
			return ret;

		ret = drmm_add_action_or_reset(&dev_priv->drm,
					       intel_uncore_fini_mmio,
					       gt->uncore);
		if (ret)
			return ret;
	}

	/* Try to make sure MCHBAR is enabled before poking at it */
	intel_gmch_bar_setup(dev_priv);
	intel_device_info_runtime_init(dev_priv);
	intel_display_device_info_runtime_init(dev_priv);

	for_each_gt(gt, dev_priv, i) {
		ret = intel_gt_init_mmio(gt);
		if (ret)
			goto err_uncore;
	}

	/* As early as possible, scrub existing GPU state before clobbering */
	sanitize_gpu(dev_priv);

	return 0;

err_uncore:
	intel_gmch_bar_teardown(dev_priv);

	return ret;
}

/**
 * i915_driver_mmio_release - cleanup the setup done in i915_driver_mmio_probe()
 * @dev_priv: device private
 */
static void i915_driver_mmio_release(struct drm_i915_private *dev_priv)
{
	intel_gmch_bar_teardown(dev_priv);
}

/**
 * i915_set_dma_info - set all relevant PCI dma info as configured for the
 * platform
 * @i915: valid i915 instance
 *
 * Set the dma max segment size, device and coherent masks.  The dma mask set
 * needs to occur before i915_ggtt_probe_hw.
 *
 * A couple of platforms have special needs.  Address them as well.
 *
 */
static int i915_set_dma_info(struct drm_i915_private *i915)
{
	unsigned int mask_size = INTEL_INFO(i915)->dma_mask_size;
	int ret;

	GEM_BUG_ON(!mask_size);

	/*
	 * We don't have a max segment size, so set it to the max so sg's
	 * debugging layer doesn't complain
	 */
	dma_set_max_seg_size(i915->drm.dev, UINT_MAX);

	ret = dma_set_mask(i915->drm.dev, DMA_BIT_MASK(mask_size));
	if (ret)
		goto mask_err;

	/* overlay on gen2 is broken and can't address above 1G */
	if (GRAPHICS_VER(i915) == 2)
		mask_size = 30;

	/*
	 * 965GM sometimes incorrectly writes to hardware status page (HWS)
	 * using 32bit addressing, overwriting memory if HWS is located
	 * above 4GB.
	 *
	 * The documentation also mentions an issue with undefined
	 * behaviour if any general state is accessed within a page above 4GB,
	 * which also needs to be handled carefully.
	 */
	if (IS_I965G(i915) || IS_I965GM(i915))
		mask_size = 32;

	ret = dma_set_coherent_mask(i915->drm.dev, DMA_BIT_MASK(mask_size));
	if (ret)
		goto mask_err;

	return 0;

mask_err:
	drm_err(&i915->drm, "Can't set DMA mask/consistent mask (%d)\n", ret);
	return ret;
}

static int i915_pcode_init(struct drm_i915_private *i915)
{
	struct intel_gt *gt;
	int id, ret;

	for_each_gt(gt, i915, id) {
		ret = intel_pcode_init(gt->uncore);
		if (ret) {
			gt_err(gt, "intel_pcode_init failed %d\n", ret);
			return ret;
		}
	}

	return 0;
}

/**
 * i915_driver_hw_probe - setup state requiring device access
 * @dev_priv: device private
 *
 * Setup state that requires accessing the device, but doesn't require
 * exposing the driver via kernel internal or userspace interfaces.
 */
static int i915_driver_hw_probe(struct drm_i915_private *dev_priv)
{
	struct intel_display *display = &dev_priv->display;
	struct pci_dev *pdev = dev_priv->drm.pdev;
	int ret;

	if (i915_inject_probe_failure(dev_priv))
		return -ENODEV;

	if (HAS_PPGTT(dev_priv)) {
		if (intel_vgpu_active(dev_priv) &&
		    !intel_vgpu_has_full_ppgtt(dev_priv)) {
			drm_err(&dev_priv->drm,
				"incompatible vGPU found, support for isolated ppGTT required\n");
			return -ENXIO;
		}
	}

	if (HAS_EXECLISTS(dev_priv)) {
		/*
		 * Older GVT emulation depends upon intercepting CSB mmio,
		 * which we no longer use, preferring to use the HWSP cache
		 * instead.
		 */
		if (intel_vgpu_active(dev_priv) &&
		    !intel_vgpu_has_hwsp_emulation(dev_priv)) {
			drm_err(&dev_priv->drm,
				"old vGPU host found, support for HWSP emulation required\n");
			return -ENXIO;
		}
	}

	/* needs to be done before ggtt probe */
	intel_dram_edram_detect(dev_priv);

	ret = i915_set_dma_info(dev_priv);
	if (ret)
		return ret;

	ret = i915_perf_init(dev_priv);
	if (ret)
		return ret;

	ret = i915_ggtt_probe_hw(dev_priv);
	if (ret)
		goto err_perf;

	ret = drm_aperture_remove_conflicting_pci_framebuffers(pdev, dev_priv->drm.driver);
	if (ret)
		goto err_ggtt;

	ret = i915_ggtt_init_hw(dev_priv);
	if (ret)
		goto err_ggtt;

	/*
	 * Make sure we probe lmem before we probe stolen-lmem. The BAR size
	 * might be different due to bar resizing.
	 */
	ret = intel_gt_tiles_init(dev_priv);
	if (ret)
		goto err_ggtt;

	ret = intel_memory_regions_hw_probe(dev_priv);
	if (ret)
		goto err_ggtt;

	ret = i915_ggtt_enable_hw(dev_priv);
	if (ret) {
		drm_err(&dev_priv->drm, "failed to enable GGTT\n");
		goto err_mem_regions;
	}

	pci_set_master(pdev);

	/* On the 945G/GM, the chipset reports the MSI capability on the
	 * integrated graphics even though the support isn't actually there
	 * according to the published specs.  It doesn't appear to function
	 * correctly in testing on 945G.
	 * This may be a side effect of MSI having been made available for PEG
	 * and the registers being closely associated.
	 *
	 * According to chipset errata, on the 965GM, MSI interrupts may
	 * be lost or delayed, and was defeatured. MSI interrupts seem to
	 * get lost on g4x as well, and interrupt delivery seems to stay
	 * properly dead afterwards. So we'll just disable them for all
	 * pre-gen5 chipsets.
	 *
	 * dp aux and gmbus irq on gen4 seems to be able to generate legacy
	 * interrupts even when in MSI mode. This results in spurious
	 * interrupt warnings if the legacy irq no. is shared with another
	 * device. The kernel then disables that interrupt source and so
	 * prevents the other device from working properly.
	 */
	if (GRAPHICS_VER(dev_priv) >= 5) {
		if (pci_enable_msi(pdev) < 0)
			drm_dbg(&dev_priv->drm, "can't enable MSI");
	}

	ret = intel_gvt_init(dev_priv);
	if (ret)
		goto err_msi;

	intel_opregion_setup(display);

	ret = i915_pcode_init(dev_priv);
	if (ret)
		goto err_opregion;

	/*
	 * Fill the dram structure to get the system dram info. This will be
	 * used for memory latency calculation.
	 */
	intel_dram_detect(dev_priv);

	intel_bw_init_hw(dev_priv);

	return 0;

err_opregion:
	intel_opregion_cleanup(display);
err_msi:
	if (pdev->msi_enabled)
		pci_disable_msi(pdev);
err_mem_regions:
	intel_memory_regions_driver_release(dev_priv);
err_ggtt:
	i915_ggtt_driver_release(dev_priv);
	i915_gem_drain_freed_objects(dev_priv);
	i915_ggtt_driver_late_release(dev_priv);
err_perf:
	i915_perf_fini(dev_priv);
	return ret;
}

/**
 * i915_driver_hw_remove - cleanup the setup done in i915_driver_hw_probe()
 * @dev_priv: device private
 */
static void i915_driver_hw_remove(struct drm_i915_private *dev_priv)
{
	struct intel_display *display = &dev_priv->display;
	struct pci_dev *pdev = dev_priv->drm.pdev;

	i915_perf_fini(dev_priv);

	intel_opregion_cleanup(display);

	if (pdev->msi_enabled)
		pci_disable_msi(pdev);
}

/**
 * i915_driver_register - register the driver with the rest of the system
 * @dev_priv: device private
 *
 * Perform any steps necessary to make the driver available via kernel
 * internal or userspace interfaces.
 */
static void i915_driver_register(struct drm_i915_private *dev_priv)
{
	struct intel_gt *gt;
	unsigned int i;

	i915_gem_driver_register(dev_priv);
	i915_pmu_register(dev_priv);

	intel_vgpu_register(dev_priv);

	/* Reveal our presence to userspace */
	if (drm_dev_register(&dev_priv->drm, 0)) {
		drm_err(&dev_priv->drm,
			"Failed to register driver for userspace access!\n");
		return;
	}

	i915_debugfs_register(dev_priv);
	i915_setup_sysfs(dev_priv);

	/* Depends on sysfs having been initialized */
	i915_perf_register(dev_priv);

	for_each_gt(gt, dev_priv, i)
		intel_gt_driver_register(gt);

	intel_pxp_debugfs_register(dev_priv->pxp);

	i915_hwmon_register(dev_priv);

	intel_display_driver_register(dev_priv);

	intel_power_domains_enable(dev_priv);
	intel_runtime_pm_enable(&dev_priv->runtime_pm);

	intel_register_dsm_handler();

	if (i915_switcheroo_register(dev_priv))
		drm_err(&dev_priv->drm, "Failed to register vga switcheroo!\n");
}

/**
 * i915_driver_unregister - cleanup the registration done in i915_driver_regiser()
 * @dev_priv: device private
 */
static void i915_driver_unregister(struct drm_i915_private *dev_priv)
{
	struct intel_gt *gt;
	unsigned int i;

	i915_switcheroo_unregister(dev_priv);

	intel_unregister_dsm_handler();

	intel_runtime_pm_disable(&dev_priv->runtime_pm);
	intel_power_domains_disable(dev_priv);

	intel_display_driver_unregister(dev_priv);

	intel_pxp_fini(dev_priv);

	for_each_gt(gt, dev_priv, i)
		intel_gt_driver_unregister(gt);

	i915_hwmon_unregister(dev_priv);

	i915_perf_unregister(dev_priv);
	i915_pmu_unregister(dev_priv);

	i915_teardown_sysfs(dev_priv);
	drm_dev_unplug(&dev_priv->drm);

	i915_gem_driver_unregister(dev_priv);
}

void
i915_print_iommu_status(struct drm_i915_private *i915, struct drm_printer *p)
{
	drm_printf(p, "iommu: %s\n",
		   str_enabled_disabled(i915_vtd_active(i915)));
}

static void i915_welcome_messages(struct drm_i915_private *dev_priv)
{
	if (drm_debug_enabled(DRM_UT_DRIVER)) {
		struct drm_printer p = drm_dbg_printer(&dev_priv->drm, DRM_UT_DRIVER,
						       "device info:");
		struct intel_gt *gt;
		unsigned int i;

		drm_printf(&p, "pciid=0x%04x rev=0x%02x platform=%s (subplatform=0x%x) gen=%i\n",
			   INTEL_DEVID(dev_priv),
			   INTEL_REVID(dev_priv),
			   intel_platform_name(INTEL_INFO(dev_priv)->platform),
			   intel_subplatform(RUNTIME_INFO(dev_priv),
					     INTEL_INFO(dev_priv)->platform),
			   GRAPHICS_VER(dev_priv));

		intel_device_info_print(INTEL_INFO(dev_priv),
					RUNTIME_INFO(dev_priv), &p);
		i915_print_iommu_status(dev_priv, &p);
		for_each_gt(gt, dev_priv, i)
			intel_gt_info_print(&gt->info, &p);
	}

	if (IS_ENABLED(CONFIG_DRM_I915_DEBUG))
		drm_info(&dev_priv->drm, "DRM_I915_DEBUG enabled\n");
	if (IS_ENABLED(CONFIG_DRM_I915_DEBUG_GEM))
		drm_info(&dev_priv->drm, "DRM_I915_DEBUG_GEM enabled\n");
	if (IS_ENABLED(CONFIG_DRM_I915_DEBUG_RUNTIME_PM))
		drm_info(&dev_priv->drm,
			 "DRM_I915_DEBUG_RUNTIME_PM enabled\n");
}

#ifdef __linux__

static struct drm_i915_private *
i915_driver_create(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	const struct intel_device_info *match_info =
		(struct intel_device_info *)ent->driver_data;
	struct drm_i915_private *i915;

	i915 = devm_drm_dev_alloc(&pdev->dev, &i915_drm_driver,
				  struct drm_i915_private, drm);
	if (IS_ERR(i915))
		return i915;

	pci_set_drvdata(pdev, &i915->drm);

	/* Device parameters start as a copy of module parameters. */
	i915_params_copy(&i915->params, &i915_modparams);

	/* Set up device info and initial runtime info. */
	intel_device_info_driver_create(i915, pdev->device, match_info);

	intel_display_device_probe(i915);

	return i915;
}

#endif

void inteldrm_init_backlight(struct drm_i915_private *);

/**
 * i915_driver_probe - setup chip and create an initial config
 * @pdev: PCI device
 * @ent: matching PCI ID entry
 *
 * The driver probe routine has to do several things:
 *   - drive output discovery via intel_display_driver_probe()
 *   - initialize the memory manager
 *   - allocate initial config memory
 *   - setup the DRM framebuffer with the allocated memory
 */
int i915_driver_probe(struct drm_i915_private *i915, const struct pci_device_id *ent)
{
#ifdef __linux__
	struct drm_i915_private *i915;
	int ret;

	ret = pci_enable_device(pdev);
	if (ret) {
		pr_err("Failed to enable graphics device: %pe\n", ERR_PTR(ret));
		return ret;
	}

	i915 = i915_driver_create(pdev, ent);
	if (IS_ERR(i915)) {
		pci_disable_device(pdev);
		return PTR_ERR(i915);
	}
#else
	struct pci_dev *pdev = i915->drm.pdev;
	int ret;
#endif

	ret = i915_driver_early_probe(i915);
	if (ret < 0)
		goto out_pci_disable;

	disable_rpm_wakeref_asserts(&i915->runtime_pm);

	intel_vgpu_detect(i915);

	ret = intel_gt_probe_all(i915);
	if (ret < 0)
		goto out_runtime_pm_put;

	ret = i915_driver_mmio_probe(i915);
	if (ret < 0)
		goto out_runtime_pm_put;

	ret = i915_driver_hw_probe(i915);
	if (ret < 0)
		goto out_cleanup_mmio;

	ret = intel_display_driver_probe_noirq(i915);
	if (ret < 0)
		goto out_cleanup_hw;

	ret = intel_irq_install(i915);
	if (ret)
		goto out_cleanup_modeset;

	ret = intel_display_driver_probe_nogem(i915);
	if (ret)
		goto out_cleanup_irq;

	ret = i915_gem_init(i915);
	if (ret)
		goto out_cleanup_modeset2;

	ret = intel_pxp_init(i915);
	if (ret && ret != -ENODEV)
		drm_dbg(&i915->drm, "pxp init failed with %d\n", ret);

	ret = intel_display_driver_probe(i915);
	if (ret)
		goto out_cleanup_gem;

	i915_driver_register(i915);

#ifdef __OpenBSD__
	inteldrm_init_backlight(i915);
#endif

	enable_rpm_wakeref_asserts(&i915->runtime_pm);

	i915_welcome_messages(i915);

	i915->do_release = true;

	return 0;

out_cleanup_gem:
	i915_gem_suspend(i915);
	i915_gem_driver_remove(i915);
	i915_gem_driver_release(i915);
out_cleanup_modeset2:
	/* FIXME clean up the error path */
	intel_display_driver_remove(i915);
	intel_irq_uninstall(i915);
	intel_display_driver_remove_noirq(i915);
	goto out_cleanup_modeset;
out_cleanup_irq:
	intel_irq_uninstall(i915);
out_cleanup_modeset:
	intel_display_driver_remove_nogem(i915);
out_cleanup_hw:
	i915_driver_hw_remove(i915);
	intel_memory_regions_driver_release(i915);
	i915_ggtt_driver_release(i915);
	i915_gem_drain_freed_objects(i915);
	i915_ggtt_driver_late_release(i915);
out_cleanup_mmio:
	i915_driver_mmio_release(i915);
out_runtime_pm_put:
	enable_rpm_wakeref_asserts(&i915->runtime_pm);
	i915_driver_late_release(i915);
out_pci_disable:
	pci_disable_device(pdev);
	i915_probe_error(i915, "Device initialization failed (%d)\n", ret);
	return ret;
}

void i915_driver_remove(struct drm_i915_private *i915)
{
	intel_wakeref_t wakeref;

	wakeref = intel_runtime_pm_get(&i915->runtime_pm);

	i915_driver_unregister(i915);

	/* Flush any external code that still may be under the RCU lock */
	synchronize_rcu();

	i915_gem_suspend(i915);

	intel_gvt_driver_remove(i915);

	intel_display_driver_remove(i915);

	intel_irq_uninstall(i915);

	intel_display_driver_remove_noirq(i915);

	i915_reset_error_state(i915);
	i915_gem_driver_remove(i915);

	intel_display_driver_remove_nogem(i915);

	i915_driver_hw_remove(i915);

	intel_runtime_pm_put(&i915->runtime_pm, wakeref);
}

static void i915_driver_release(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_runtime_pm *rpm = &dev_priv->runtime_pm;
	intel_wakeref_t wakeref;

	if (!dev_priv->do_release)
		return;

	wakeref = intel_runtime_pm_get(rpm);

	i915_gem_driver_release(dev_priv);

	intel_memory_regions_driver_release(dev_priv);
	i915_ggtt_driver_release(dev_priv);
	i915_gem_drain_freed_objects(dev_priv);
	i915_ggtt_driver_late_release(dev_priv);

	i915_driver_mmio_release(dev_priv);

	intel_runtime_pm_put(rpm, wakeref);

	intel_runtime_pm_driver_release(rpm);

	i915_driver_late_release(dev_priv);

	intel_display_device_remove(dev_priv);
}

static int i915_driver_open(struct drm_device *dev, struct drm_file *file)
{
	struct drm_i915_private *i915 = to_i915(dev);
	int ret;

	ret = i915_gem_open(i915, file);
	if (ret)
		return ret;

	return 0;
}

static void i915_driver_postclose(struct drm_device *dev, struct drm_file *file)
{
	struct drm_i915_file_private *file_priv = file->driver_priv;

	i915_gem_context_close(file);
	i915_drm_client_put(file_priv->client);

	kfree_rcu(file_priv, rcu);

	/* Catch up with all the deferred frees from "this" client */
	i915_gem_flush_free_objects(to_i915(dev));
}

void i915_driver_shutdown(struct drm_i915_private *i915)
{
	disable_rpm_wakeref_asserts(&i915->runtime_pm);
	intel_runtime_pm_disable(&i915->runtime_pm);
	intel_power_domains_disable(i915);

	intel_fbdev_set_suspend(&i915->drm, FBINFO_STATE_SUSPENDED, true);
	if (HAS_DISPLAY(i915)) {
		drm_kms_helper_poll_disable(&i915->drm);
		intel_display_driver_disable_user_access(i915);

		drm_atomic_helper_shutdown(&i915->drm);
	}

	intel_dp_mst_suspend(i915);

	intel_runtime_pm_disable_interrupts(i915);
	intel_hpd_cancel_work(i915);

	if (HAS_DISPLAY(i915))
		intel_display_driver_suspend_access(i915);

	intel_encoder_suspend_all(&i915->display);
	intel_encoder_shutdown_all(&i915->display);

	intel_dmc_suspend(i915);

	i915_gem_suspend(i915);

	/*
	 * The only requirement is to reboot with display DC states disabled,
	 * for now leaving all display power wells in the INIT power domain
	 * enabled.
	 *
	 * TODO:
	 * - unify the pci_driver::shutdown sequence here with the
	 *   pci_driver.driver.pm.poweroff,poweroff_late sequence.
	 * - unify the driver remove and system/runtime suspend sequences with
	 *   the above unified shutdown/poweroff sequence.
	 */
	intel_power_domains_driver_remove(i915);
	enable_rpm_wakeref_asserts(&i915->runtime_pm);

	intel_runtime_pm_driver_last_release(&i915->runtime_pm);
}

static bool suspend_to_idle(struct drm_i915_private *dev_priv)
{
#if IS_ENABLED(CONFIG_ACPI_SLEEP)
	if (acpi_target_system_state() < ACPI_STATE_S3)
		return true;
#endif
	return false;
}

static void i915_drm_complete(struct drm_device *dev)
{
	struct drm_i915_private *i915 = to_i915(dev);

	intel_pxp_resume_complete(i915->pxp);
}

static int i915_drm_prepare(struct drm_device *dev)
{
	struct drm_i915_private *i915 = to_i915(dev);

	intel_pxp_suspend_prepare(i915->pxp);

	/*
	 * NB intel_display_driver_suspend() may issue new requests after we've
	 * ostensibly marked the GPU as ready-to-sleep here. We need to
	 * split out that work and pull it forward so that after point,
	 * the GPU is not woken again.
	 */
	return i915_gem_backup_suspend(i915);
}

static int i915_drm_suspend(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_display *display = &dev_priv->display;
	struct pci_dev *pdev = dev_priv->drm.pdev;
	pci_power_t opregion_target_state;

	disable_rpm_wakeref_asserts(&dev_priv->runtime_pm);

	/* We do a lot of poking in a lot of registers, make sure they work
	 * properly. */
	intel_power_domains_disable(dev_priv);
	intel_fbdev_set_suspend(dev, FBINFO_STATE_SUSPENDED, true);
	if (HAS_DISPLAY(dev_priv)) {
		drm_kms_helper_poll_disable(dev);
		intel_display_driver_disable_user_access(dev_priv);
	}

	pci_save_state(pdev);

	intel_display_driver_suspend(dev_priv);

	intel_dp_mst_suspend(dev_priv);

	intel_runtime_pm_disable_interrupts(dev_priv);
	intel_hpd_cancel_work(dev_priv);

	if (HAS_DISPLAY(dev_priv))
		intel_display_driver_suspend_access(dev_priv);

	intel_encoder_suspend_all(&dev_priv->display);

	/* Must be called before GGTT is suspended. */
	intel_dpt_suspend(dev_priv);
	i915_ggtt_suspend(to_gt(dev_priv)->ggtt);

	i915_save_display(dev_priv);

	opregion_target_state = suspend_to_idle(dev_priv) ? PCI_D1 : PCI_D3cold;
	intel_opregion_suspend(display, opregion_target_state);

	dev_priv->suspend_count++;

	intel_dmc_suspend(dev_priv);

	enable_rpm_wakeref_asserts(&dev_priv->runtime_pm);

	i915_gem_drain_freed_objects(dev_priv);

	return 0;
}

static int i915_drm_suspend_late(struct drm_device *dev, bool hibernation)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct pci_dev *pdev = dev_priv->drm.pdev;
	struct intel_runtime_pm *rpm = &dev_priv->runtime_pm;
	struct intel_gt *gt;
	int ret, i;
	bool s2idle = !hibernation && suspend_to_idle(dev_priv);

	disable_rpm_wakeref_asserts(rpm);

	intel_pxp_suspend(dev_priv->pxp);

	i915_gem_suspend_late(dev_priv);

	for_each_gt(gt, dev_priv, i)
		intel_uncore_suspend(gt->uncore);

	intel_power_domains_suspend(dev_priv, s2idle);

	intel_display_power_suspend_late(dev_priv);

	ret = vlv_suspend_complete(dev_priv);
	if (ret) {
		drm_err(&dev_priv->drm, "Suspend complete failed: %d\n", ret);
		intel_power_domains_resume(dev_priv);

		goto out;
	}

	pci_disable_device(pdev);
	/*
	 * During hibernation on some platforms the BIOS may try to access
	 * the device even though it's already in D3 and hang the machine. So
	 * leave the device in D0 on those platforms and hope the BIOS will
	 * power down the device properly. The issue was seen on multiple old
	 * GENs with different BIOS vendors, so having an explicit blacklist
	 * is inpractical; apply the workaround on everything pre GEN6. The
	 * platforms where the issue was seen:
	 * Lenovo Thinkpad X301, X61s, X60, T60, X41
	 * Fujitsu FSC S7110
	 * Acer Aspire 1830T
	 */
	if (!(hibernation && GRAPHICS_VER(dev_priv) < 6))
		pci_set_power_state(pdev, PCI_D3hot);

out:
	enable_rpm_wakeref_asserts(rpm);
	if (!dev_priv->uncore.user_forcewake_count)
		intel_runtime_pm_driver_release(rpm);

	return ret;
}

#ifdef __linux__
int i915_driver_suspend_switcheroo(struct drm_i915_private *i915,
				   pm_message_t state)
{
	int error;

	if (drm_WARN_ON_ONCE(&i915->drm, state.event != PM_EVENT_SUSPEND &&
			     state.event != PM_EVENT_FREEZE))
		return -EINVAL;

	if (i915->drm.switch_power_state == DRM_SWITCH_POWER_OFF)
		return 0;

	error = i915_drm_suspend(&i915->drm);
	if (error)
		return error;

	return i915_drm_suspend_late(&i915->drm, false);
}
#endif

static int i915_drm_resume(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_display *display = &dev_priv->display;
	struct intel_gt *gt;
	int ret, i;

	disable_rpm_wakeref_asserts(&dev_priv->runtime_pm);

	ret = i915_pcode_init(dev_priv);
	if (ret)
		return ret;

	sanitize_gpu(dev_priv);

	ret = i915_ggtt_enable_hw(dev_priv);
	if (ret)
		drm_err(&dev_priv->drm, "failed to re-enable GGTT\n");

	i915_ggtt_resume(to_gt(dev_priv)->ggtt);

	for_each_gt(gt, dev_priv, i)
		if (GRAPHICS_VER(gt->i915) >= 8)
			setup_private_pat(gt);

	/* Must be called after GGTT is resumed. */
	intel_dpt_resume(dev_priv);

	intel_dmc_resume(dev_priv);

	i915_restore_display(dev_priv);
	intel_pps_unlock_regs_wa(display);

	intel_init_pch_refclk(dev_priv);

	/*
	 * Interrupts have to be enabled before any batches are run. If not the
	 * GPU will hang. i915_gem_init_hw() will initiate batches to
	 * update/restore the context.
	 *
	 * drm_mode_config_reset() needs AUX interrupts.
	 *
	 * Modeset enabling in intel_display_driver_init_hw() also needs working
	 * interrupts.
	 */
	intel_runtime_pm_enable_interrupts(dev_priv);

	if (HAS_DISPLAY(dev_priv))
		drm_mode_config_reset(dev);

	i915_gem_resume(dev_priv);

	intel_display_driver_init_hw(dev_priv);

	intel_clock_gating_init(dev_priv);

	if (HAS_DISPLAY(dev_priv))
		intel_display_driver_resume_access(dev_priv);

	intel_hpd_init(dev_priv);

	/* MST sideband requires HPD interrupts enabled */
	intel_dp_mst_resume(dev_priv);
	intel_display_driver_resume(dev_priv);

	if (HAS_DISPLAY(dev_priv)) {
		intel_display_driver_enable_user_access(dev_priv);
		drm_kms_helper_poll_enable(dev);
	}
	intel_hpd_poll_disable(dev_priv);

	intel_opregion_resume(display);

	intel_fbdev_set_suspend(dev, FBINFO_STATE_RUNNING, false);

	intel_power_domains_enable(dev_priv);

	intel_gvt_resume(dev_priv);

	enable_rpm_wakeref_asserts(&dev_priv->runtime_pm);

	return 0;
}

static int i915_drm_resume_early(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct pci_dev *pdev = dev_priv->drm.pdev;
	struct intel_gt *gt;
	int ret, i;

	/*
	 * We have a resume ordering issue with the snd-hda driver also
	 * requiring our device to be power up. Due to the lack of a
	 * parent/child relationship we currently solve this with an early
	 * resume hook.
	 *
	 * FIXME: This should be solved with a special hdmi sink device or
	 * similar so that power domains can be employed.
	 */

	/*
	 * Note that we need to set the power state explicitly, since we
	 * powered off the device during freeze and the PCI core won't power
	 * it back up for us during thaw. Powering off the device during
	 * freeze is not a hard requirement though, and during the
	 * suspend/resume phases the PCI core makes sure we get here with the
	 * device powered on. So in case we change our freeze logic and keep
	 * the device powered we can also remove the following set power state
	 * call.
	 */
	ret = pci_set_power_state(pdev, PCI_D0);
	if (ret) {
		drm_err(&dev_priv->drm,
			"failed to set PCI D0 power state (%d)\n", ret);
		return ret;
	}

	/*
	 * Note that pci_enable_device() first enables any parent bridge
	 * device and only then sets the power state for this device. The
	 * bridge enabling is a nop though, since bridge devices are resumed
	 * first. The order of enabling power and enabling the device is
	 * imposed by the PCI core as described above, so here we preserve the
	 * same order for the freeze/thaw phases.
	 *
	 * TODO: eventually we should remove pci_disable_device() /
	 * pci_enable_enable_device() from suspend/resume. Due to how they
	 * depend on the device enable refcount we can't anyway depend on them
	 * disabling/enabling the device.
	 */
	if (pci_enable_device(pdev))
		return -EIO;

	pci_set_master(pdev);

	disable_rpm_wakeref_asserts(&dev_priv->runtime_pm);

	ret = vlv_resume_prepare(dev_priv, false);
	if (ret)
		drm_err(&dev_priv->drm,
			"Resume prepare failed: %d, continuing anyway\n", ret);

	for_each_gt(gt, dev_priv, i)
		intel_gt_resume_early(gt);

	intel_display_power_resume_early(dev_priv);

	intel_power_domains_resume(dev_priv);

	enable_rpm_wakeref_asserts(&dev_priv->runtime_pm);

	return ret;
}

int i915_driver_resume_switcheroo(struct drm_i915_private *i915)
{
	int ret;

	if (i915->drm.switch_power_state == DRM_SWITCH_POWER_OFF)
		return 0;

	ret = i915_drm_resume_early(&i915->drm);
	if (ret)
		return ret;

	return i915_drm_resume(&i915->drm);
}

#ifdef __linux__

static int i915_pm_prepare(struct device *kdev)
{
	struct drm_i915_private *i915 = kdev_to_i915(kdev);

	if (!i915) {
		dev_err(kdev, "DRM not initialized, aborting suspend.\n");
		return -ENODEV;
	}

	if (i915->drm.switch_power_state == DRM_SWITCH_POWER_OFF)
		return 0;

	return i915_drm_prepare(&i915->drm);
}

static int i915_pm_suspend(struct device *kdev)
{
	struct drm_i915_private *i915 = kdev_to_i915(kdev);

	if (!i915) {
		dev_err(kdev, "DRM not initialized, aborting suspend.\n");
		return -ENODEV;
	}

	if (i915->drm.switch_power_state == DRM_SWITCH_POWER_OFF)
		return 0;

	return i915_drm_suspend(&i915->drm);
}

static int i915_pm_suspend_late(struct device *kdev)
{
	struct drm_i915_private *i915 = kdev_to_i915(kdev);

	/*
	 * We have a suspend ordering issue with the snd-hda driver also
	 * requiring our device to be power up. Due to the lack of a
	 * parent/child relationship we currently solve this with an late
	 * suspend hook.
	 *
	 * FIXME: This should be solved with a special hdmi sink device or
	 * similar so that power domains can be employed.
	 */
	if (i915->drm.switch_power_state == DRM_SWITCH_POWER_OFF)
		return 0;

	return i915_drm_suspend_late(&i915->drm, false);
}

static int i915_pm_poweroff_late(struct device *kdev)
{
	struct drm_i915_private *i915 = kdev_to_i915(kdev);

	if (i915->drm.switch_power_state == DRM_SWITCH_POWER_OFF)
		return 0;

	return i915_drm_suspend_late(&i915->drm, true);
}

static int i915_pm_resume_early(struct device *kdev)
{
	struct drm_i915_private *i915 = kdev_to_i915(kdev);

	if (i915->drm.switch_power_state == DRM_SWITCH_POWER_OFF)
		return 0;

	return i915_drm_resume_early(&i915->drm);
}

static int i915_pm_resume(struct device *kdev)
{
	struct drm_i915_private *i915 = kdev_to_i915(kdev);

	if (i915->drm.switch_power_state == DRM_SWITCH_POWER_OFF)
		return 0;

	return i915_drm_resume(&i915->drm);
}

static void i915_pm_complete(struct device *kdev)
{
	struct drm_i915_private *i915 = kdev_to_i915(kdev);

	if (i915->drm.switch_power_state == DRM_SWITCH_POWER_OFF)
		return;

	i915_drm_complete(&i915->drm);
}

/* freeze: before creating the hibernation_image */
static int i915_pm_freeze(struct device *kdev)
{
	struct drm_i915_private *i915 = kdev_to_i915(kdev);
	int ret;

	if (i915->drm.switch_power_state != DRM_SWITCH_POWER_OFF) {
		ret = i915_drm_suspend(&i915->drm);
		if (ret)
			return ret;
	}

	ret = i915_gem_freeze(i915);
	if (ret)
		return ret;

	return 0;
}

static int i915_pm_freeze_late(struct device *kdev)
{
	struct drm_i915_private *i915 = kdev_to_i915(kdev);
	int ret;

	if (i915->drm.switch_power_state != DRM_SWITCH_POWER_OFF) {
		ret = i915_drm_suspend_late(&i915->drm, true);
		if (ret)
			return ret;
	}

	ret = i915_gem_freeze_late(i915);
	if (ret)
		return ret;

	return 0;
}

/* thaw: called after creating the hibernation image, but before turning off. */
static int i915_pm_thaw_early(struct device *kdev)
{
	return i915_pm_resume_early(kdev);
}

static int i915_pm_thaw(struct device *kdev)
{
	return i915_pm_resume(kdev);
}

/* restore: called after loading the hibernation image. */
static int i915_pm_restore_early(struct device *kdev)
{
	return i915_pm_resume_early(kdev);
}

static int i915_pm_restore(struct device *kdev)
{
	return i915_pm_resume(kdev);
}

static int intel_runtime_suspend(struct device *kdev)
{
	struct drm_i915_private *dev_priv = kdev_to_i915(kdev);
	struct intel_display *display = &dev_priv->display;
	struct intel_runtime_pm *rpm = &dev_priv->runtime_pm;
	struct pci_dev *pdev = to_pci_dev(dev_priv->drm.dev);
	struct pci_dev *root_pdev;
	struct intel_gt *gt;
	int ret, i;

	if (drm_WARN_ON_ONCE(&dev_priv->drm, !HAS_RUNTIME_PM(dev_priv)))
		return -ENODEV;

	drm_dbg(&dev_priv->drm, "Suspending device\n");

	disable_rpm_wakeref_asserts(rpm);

	/*
	 * We are safe here against re-faults, since the fault handler takes
	 * an RPM reference.
	 */
	i915_gem_runtime_suspend(dev_priv);

	intel_pxp_runtime_suspend(dev_priv->pxp);

	for_each_gt(gt, dev_priv, i)
		intel_gt_runtime_suspend(gt);

	intel_runtime_pm_disable_interrupts(dev_priv);

	for_each_gt(gt, dev_priv, i)
		intel_uncore_suspend(gt->uncore);

	intel_display_power_suspend(dev_priv);

	ret = vlv_suspend_complete(dev_priv);
	if (ret) {
		drm_err(&dev_priv->drm,
			"Runtime suspend failed, disabling it (%d)\n", ret);
		intel_uncore_runtime_resume(&dev_priv->uncore);

		intel_runtime_pm_enable_interrupts(dev_priv);

		for_each_gt(gt, dev_priv, i)
			intel_gt_runtime_resume(gt);

		enable_rpm_wakeref_asserts(rpm);

		return ret;
	}

	enable_rpm_wakeref_asserts(rpm);
	intel_runtime_pm_driver_release(rpm);

	if (intel_uncore_arm_unclaimed_mmio_detection(&dev_priv->uncore))
		drm_err(&dev_priv->drm,
			"Unclaimed access detected prior to suspending\n");

	/*
	 * FIXME: Temporary hammer to avoid freezing the machine on our DGFX
	 * This should be totally removed when we handle the pci states properly
	 * on runtime PM.
	 */
	root_pdev = pcie_find_root_port(pdev);
	if (root_pdev)
		pci_d3cold_disable(root_pdev);

	/*
	 * FIXME: We really should find a document that references the arguments
	 * used below!
	 */
	if (IS_BROADWELL(dev_priv)) {
		/*
		 * On Broadwell, if we use PCI_D1 the PCH DDI ports will stop
		 * being detected, and the call we do at intel_runtime_resume()
		 * won't be able to restore them. Since PCI_D3hot matches the
		 * actual specification and appears to be working, use it.
		 */
		intel_opregion_notify_adapter(display, PCI_D3hot);
	} else {
		/*
		 * current versions of firmware which depend on this opregion
		 * notification have repurposed the D1 definition to mean
		 * "runtime suspended" vs. what you would normally expect (D3)
		 * to distinguish it from notifications that might be sent via
		 * the suspend path.
		 */
		intel_opregion_notify_adapter(display, PCI_D1);
	}

	assert_forcewakes_inactive(&dev_priv->uncore);

	if (!IS_VALLEYVIEW(dev_priv) && !IS_CHERRYVIEW(dev_priv))
		intel_hpd_poll_enable(dev_priv);

	drm_dbg(&dev_priv->drm, "Device suspended\n");
	return 0;
}

static int intel_runtime_resume(struct device *kdev)
{
	struct drm_i915_private *dev_priv = kdev_to_i915(kdev);
	struct intel_display *display = &dev_priv->display;
	struct intel_runtime_pm *rpm = &dev_priv->runtime_pm;
	struct pci_dev *pdev = to_pci_dev(dev_priv->drm.dev);
	struct pci_dev *root_pdev;
	struct intel_gt *gt;
	int ret, i;

	if (drm_WARN_ON_ONCE(&dev_priv->drm, !HAS_RUNTIME_PM(dev_priv)))
		return -ENODEV;

	drm_dbg(&dev_priv->drm, "Resuming device\n");

	drm_WARN_ON_ONCE(&dev_priv->drm, atomic_read(&rpm->wakeref_count));
	disable_rpm_wakeref_asserts(rpm);

	intel_opregion_notify_adapter(display, PCI_D0);

	root_pdev = pcie_find_root_port(pdev);
	if (root_pdev)
		pci_d3cold_enable(root_pdev);

	if (intel_uncore_unclaimed_mmio(&dev_priv->uncore))
		drm_dbg(&dev_priv->drm,
			"Unclaimed access during suspend, bios?\n");

	intel_display_power_resume(dev_priv);

	ret = vlv_resume_prepare(dev_priv, true);

	for_each_gt(gt, dev_priv, i)
		intel_uncore_runtime_resume(gt->uncore);

	intel_runtime_pm_enable_interrupts(dev_priv);

	/*
	 * No point of rolling back things in case of an error, as the best
	 * we can do is to hope that things will still work (and disable RPM).
	 */
	for_each_gt(gt, dev_priv, i)
		intel_gt_runtime_resume(gt);

	intel_pxp_runtime_resume(dev_priv->pxp);

	/*
	 * On VLV/CHV display interrupts are part of the display
	 * power well, so hpd is reinitialized from there. For
	 * everyone else do it here.
	 */
	if (!IS_VALLEYVIEW(dev_priv) && !IS_CHERRYVIEW(dev_priv)) {
		intel_hpd_init(dev_priv);
		intel_hpd_poll_disable(dev_priv);
	}

	skl_watermark_ipc_update(dev_priv);

	enable_rpm_wakeref_asserts(rpm);

	if (ret)
		drm_err(&dev_priv->drm,
			"Runtime resume failed, disabling it (%d)\n", ret);
	else
		drm_dbg(&dev_priv->drm, "Device resumed\n");

	return ret;
}

const struct dev_pm_ops i915_pm_ops = {
	/*
	 * S0ix (via system suspend) and S3 event handlers [PMSG_SUSPEND,
	 * PMSG_RESUME]
	 */
	.prepare = i915_pm_prepare,
	.suspend = i915_pm_suspend,
	.suspend_late = i915_pm_suspend_late,
	.resume_early = i915_pm_resume_early,
	.resume = i915_pm_resume,
	.complete = i915_pm_complete,

	/*
	 * S4 event handlers
	 * @freeze, @freeze_late    : called (1) before creating the
	 *                            hibernation image [PMSG_FREEZE] and
	 *                            (2) after rebooting, before restoring
	 *                            the image [PMSG_QUIESCE]
	 * @thaw, @thaw_early       : called (1) after creating the hibernation
	 *                            image, before writing it [PMSG_THAW]
	 *                            and (2) after failing to create or
	 *                            restore the image [PMSG_RECOVER]
	 * @poweroff, @poweroff_late: called after writing the hibernation
	 *                            image, before rebooting [PMSG_HIBERNATE]
	 * @restore, @restore_early : called after rebooting and restoring the
	 *                            hibernation image [PMSG_RESTORE]
	 */
	.freeze = i915_pm_freeze,
	.freeze_late = i915_pm_freeze_late,
	.thaw_early = i915_pm_thaw_early,
	.thaw = i915_pm_thaw,
	.poweroff = i915_pm_suspend,
	.poweroff_late = i915_pm_poweroff_late,
	.restore_early = i915_pm_restore_early,
	.restore = i915_pm_restore,

	/* S0ix (via runtime suspend) event handlers */
	.runtime_suspend = intel_runtime_suspend,
	.runtime_resume = intel_runtime_resume,
};

static const struct file_operations i915_driver_fops = {
	.owner = THIS_MODULE,
	.open = drm_open,
	.release = drm_release_noglobal,
	.unlocked_ioctl = drm_ioctl,
	.mmap = i915_gem_mmap,
	.poll = drm_poll,
	.read = drm_read,
	.compat_ioctl = i915_ioc32_compat_ioctl,
	.llseek = noop_llseek,
#ifdef CONFIG_PROC_FS
	.show_fdinfo = drm_show_fdinfo,
#endif
	.fop_flags = FOP_UNSIGNED_OFFSET,
};

#endif /* __linux__ */

static int
i915_gem_reject_pin_ioctl(struct drm_device *dev, void *data,
			  struct drm_file *file)
{
	return -ENODEV;
}

static const struct drm_ioctl_desc i915_ioctls[] = {
	DRM_IOCTL_DEF_DRV(I915_INIT, drm_noop, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF_DRV(I915_FLUSH, drm_noop, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(I915_FLIP, drm_noop, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(I915_BATCHBUFFER, drm_noop, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(I915_IRQ_EMIT, drm_noop, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(I915_IRQ_WAIT, drm_noop, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(I915_GETPARAM, i915_getparam_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_SETPARAM, drm_noop, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF_DRV(I915_ALLOC, drm_noop, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(I915_FREE, drm_noop, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(I915_INIT_HEAP, drm_noop, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF_DRV(I915_CMDBUFFER, drm_noop, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(I915_DESTROY_HEAP, drm_noop, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF_DRV(I915_SET_VBLANK_PIPE, drm_noop, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF_DRV(I915_GET_VBLANK_PIPE, drm_noop, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(I915_VBLANK_SWAP, drm_noop, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(I915_HWS_ADDR, drm_noop, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF_DRV(I915_GEM_INIT, drm_noop, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF_DRV(I915_GEM_EXECBUFFER, drm_invalid_op, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(I915_GEM_EXECBUFFER2_WR, i915_gem_execbuffer2_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GEM_PIN, i915_gem_reject_pin_ioctl, DRM_AUTH|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF_DRV(I915_GEM_UNPIN, i915_gem_reject_pin_ioctl, DRM_AUTH|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF_DRV(I915_GEM_BUSY, i915_gem_busy_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GEM_SET_CACHING, i915_gem_set_caching_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GEM_GET_CACHING, i915_gem_get_caching_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GEM_THROTTLE, i915_gem_throttle_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GEM_ENTERVT, drm_noop, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF_DRV(I915_GEM_LEAVEVT, drm_noop, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF_DRV(I915_GEM_CREATE, i915_gem_create_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GEM_CREATE_EXT, i915_gem_create_ext_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GEM_PREAD, i915_gem_pread_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GEM_PWRITE, i915_gem_pwrite_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GEM_MMAP, i915_gem_mmap_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GEM_MMAP_OFFSET, i915_gem_mmap_offset_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GEM_SET_DOMAIN, i915_gem_set_domain_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GEM_SW_FINISH, i915_gem_sw_finish_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GEM_SET_TILING, i915_gem_set_tiling_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GEM_GET_TILING, i915_gem_get_tiling_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GEM_GET_APERTURE, i915_gem_get_aperture_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GET_PIPE_FROM_CRTC_ID, intel_get_pipe_from_crtc_id_ioctl, 0),
	DRM_IOCTL_DEF_DRV(I915_GEM_MADVISE, i915_gem_madvise_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_OVERLAY_PUT_IMAGE, intel_overlay_put_image_ioctl, DRM_MASTER),
	DRM_IOCTL_DEF_DRV(I915_OVERLAY_ATTRS, intel_overlay_attrs_ioctl, DRM_MASTER),
	DRM_IOCTL_DEF_DRV(I915_SET_SPRITE_COLORKEY, intel_sprite_set_colorkey_ioctl, DRM_MASTER),
	DRM_IOCTL_DEF_DRV(I915_GET_SPRITE_COLORKEY, drm_noop, DRM_MASTER),
	DRM_IOCTL_DEF_DRV(I915_GEM_WAIT, i915_gem_wait_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GEM_CONTEXT_CREATE_EXT, i915_gem_context_create_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GEM_CONTEXT_DESTROY, i915_gem_context_destroy_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_REG_READ, i915_reg_read_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GET_RESET_STATS, i915_gem_context_reset_stats_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GEM_USERPTR, i915_gem_userptr_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GEM_CONTEXT_GETPARAM, i915_gem_context_getparam_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GEM_CONTEXT_SETPARAM, i915_gem_context_setparam_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_PERF_OPEN, i915_perf_open_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_PERF_ADD_CONFIG, i915_perf_add_config_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_PERF_REMOVE_CONFIG, i915_perf_remove_config_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_QUERY, i915_query_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GEM_VM_CREATE, i915_gem_vm_create_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GEM_VM_DESTROY, i915_gem_vm_destroy_ioctl, DRM_RENDER_ALLOW),
};

/*
 * Interface history:
 *
 * 1.1: Original.
 * 1.2: Add Power Management
 * 1.3: Add vblank support
 * 1.4: Fix cmdbuffer path, add heap destroy
 * 1.5: Add vblank pipe configuration
 * 1.6: - New ioctl for scheduling buffer swaps on vertical blank
 *      - Support vertical blank on secondary display pipe
 */
#define DRIVER_MAJOR		1
#define DRIVER_MINOR		6
#define DRIVER_PATCHLEVEL	0

static const struct drm_driver i915_drm_driver = {
	/* Don't use MTRRs here; the Xserver or userspace app should
	 * deal with them for Intel hardware.
	 */
	.driver_features =
	    DRIVER_GEM |
	    DRIVER_RENDER | DRIVER_MODESET | DRIVER_ATOMIC | DRIVER_SYNCOBJ |
	    DRIVER_SYNCOBJ_TIMELINE,
	.release = i915_driver_release,
	.open = i915_driver_open,
	.postclose = i915_driver_postclose,
	.show_fdinfo = PTR_IF(IS_ENABLED(CONFIG_PROC_FS), i915_drm_client_fdinfo),

	.gem_prime_import = i915_gem_prime_import,

	.dumb_create = i915_gem_dumb_create,
	.dumb_map_offset = i915_gem_dumb_mmap_offset,

#ifdef __OpenBSD__
	.mmap = i915_gem_mmap,
	.gem_fault = i915_gem_fault,
#endif

	.ioctls = i915_ioctls,
	.num_ioctls = ARRAY_SIZE(i915_ioctls),
#ifdef __linux__
	.fops = &i915_driver_fops,
#endif
	.name = DRIVER_NAME,
	.desc = DRIVER_DESC,
	.date = DRIVER_DATE,
	.major = DRIVER_MAJOR,
	.minor = DRIVER_MINOR,
	.patchlevel = DRIVER_PATCHLEVEL,
};

#ifdef __OpenBSD__

#include <ddb/db_var.h>

#include <drm/drm_device.h> /* for agp */
#include <drm/drm_utils.h>
#include <drm/drm_fb_helper.h>
#include "display/intel_display_types.h"

#ifdef __amd64__
#include "efifb.h"
#include <machine/biosvar.h>
#endif

#if NEFIFB > 0
#include <machine/efifbvar.h>
#endif

#include "intagp.h"

/*
 * some functions are only called once on init regardless of how many times
 * inteldrm attaches in linux this is handled via module_init()/module_exit()
 */
int inteldrm_refcnt;

#if NINTAGP > 0
int	intagpsubmatch(struct device *, void *, void *);
int	intagp_print(void *, const char *);

int
intagpsubmatch(struct device *parent, void *match, void *aux)
{
	extern struct cfdriver intagp_cd;
	struct cfdata *cf = match;

	/* only allow intagp to attach */
	if (cf->cf_driver == &intagp_cd)
		return ((*cf->cf_attach->ca_match)(parent, match, aux));
	return (0);
}

int
intagp_print(void *vaa, const char *pnp)
{
	if (pnp)
		printf("intagp at %s", pnp);
	return (UNCONF);
}
#endif

int	inteldrm_wsioctl(void *, u_long, caddr_t, int, struct proc *);
paddr_t	inteldrm_wsmmap(void *, off_t, int);
int	inteldrm_alloc_screen(void *, const struct wsscreen_descr *,
	    void **, int *, int *, uint32_t *);
void	inteldrm_free_screen(void *, void *);
int	inteldrm_show_screen(void *, void *, int,
	    void (*)(void *, int, int), void *);
void	inteldrm_doswitch(void *);
void	inteldrm_enter_ddb(void *, void *);
int	inteldrm_load_font(void *, void *, struct wsdisplay_font *);
int	inteldrm_list_font(void *, struct wsdisplay_font *);
int	inteldrm_getchar(void *, int, int, struct wsdisplay_charcell *);
void	inteldrm_burner(void *, u_int, u_int);
void	inteldrm_burner_cb(void *);
void	inteldrm_scrollback(void *, void *, int lines);
extern const struct pci_device_id pciidlist[];

struct wsscreen_descr inteldrm_stdscreen = {
	"std",
	0, 0,
	0,
	0, 0,
	WSSCREEN_UNDERLINE | WSSCREEN_HILIT |
	WSSCREEN_REVERSE | WSSCREEN_WSCOLORS
};

const struct wsscreen_descr *inteldrm_scrlist[] = {
	&inteldrm_stdscreen,
};

struct wsscreen_list inteldrm_screenlist = {
	nitems(inteldrm_scrlist), inteldrm_scrlist
};

struct wsdisplay_accessops inteldrm_accessops = {
	.ioctl = inteldrm_wsioctl,
	.mmap = inteldrm_wsmmap,
	.alloc_screen = inteldrm_alloc_screen,
	.free_screen = inteldrm_free_screen,
	.show_screen = inteldrm_show_screen,
	.enter_ddb = inteldrm_enter_ddb,
	.getchar = inteldrm_getchar,
	.load_font = inteldrm_load_font,
	.list_font = inteldrm_list_font,
	.scrollback = inteldrm_scrollback,
	.burn_screen = inteldrm_burner
};

int
inteldrm_wsioctl(void *v, u_long cmd, caddr_t data, int flag, struct proc *p)
{
	struct drm_i915_private *dev_priv = v;
	struct backlight_device *bd = dev_priv->backlight;
	struct rasops_info *ri = &dev_priv->ro;
	struct wsdisplay_fbinfo *wdf;
	struct wsdisplay_param *dp = (struct wsdisplay_param *)data;

	switch (cmd) {
	case WSDISPLAYIO_GTYPE:
		*(u_int *)data = WSDISPLAY_TYPE_INTELDRM;
		return 0;
	case WSDISPLAYIO_GINFO:
		wdf = (struct wsdisplay_fbinfo *)data;
		wdf->width = ri->ri_width;
		wdf->height = ri->ri_height;
		wdf->depth = ri->ri_depth;
		wdf->stride = ri->ri_stride;
		wdf->offset = 0;
		wdf->cmsize = 0;
		return 0;
	case WSDISPLAYIO_GETPARAM:
		if (ws_get_param && ws_get_param(dp) == 0)
			return 0;

		if (bd == NULL)
			return -1;

		switch (dp->param) {
		case WSDISPLAYIO_PARAM_BRIGHTNESS:
			dp->min = 0;
			dp->max = bd->props.max_brightness;
			dp->curval = bd->ops->get_brightness(bd);
			return (dp->max > dp->min) ? 0 : -1;
		}
		break;
	case WSDISPLAYIO_SETPARAM:
		if (ws_set_param && ws_set_param(dp) == 0)
			return 0;

		if (bd == NULL || dp->curval > bd->props.max_brightness)
			return -1;

		switch (dp->param) {
		case WSDISPLAYIO_PARAM_BRIGHTNESS:
			bd->props.brightness = dp->curval;
			backlight_update_status(bd);
			knote_locked(&dev_priv->drm.note, NOTE_CHANGE);
			return 0;
		}
		break;
	case WSDISPLAYIO_SVIDEO:
	case WSDISPLAYIO_GVIDEO:
		return 0;
	}

	return (-1);
}

paddr_t
inteldrm_wsmmap(void *v, off_t off, int prot)
{
	return (-1);
}

int
inteldrm_alloc_screen(void *v, const struct wsscreen_descr *type,
    void **cookiep, int *curxp, int *curyp, uint32_t *attrp)
{
	struct drm_i915_private *dev_priv = v;
	struct rasops_info *ri = &dev_priv->ro;

	return rasops_alloc_screen(ri, cookiep, curxp, curyp, attrp);
}

void
inteldrm_free_screen(void *v, void *cookie)
{
	struct drm_i915_private *dev_priv = v;
	struct rasops_info *ri = &dev_priv->ro;

	return rasops_free_screen(ri, cookie);
}

int
inteldrm_show_screen(void *v, void *cookie, int waitok,
    void (*cb)(void *, int, int), void *cbarg)
{
	struct drm_i915_private *dev_priv = v;
	struct rasops_info *ri = &dev_priv->ro;

	if (cookie == ri->ri_active)
		return (0);

	dev_priv->switchcb = cb;
	dev_priv->switchcbarg = cbarg;
	dev_priv->switchcookie = cookie;
	if (cb) {
		task_add(systq, &dev_priv->switchtask);
		return (EAGAIN);
	}

	inteldrm_doswitch(v);

	return (0);
}

void
inteldrm_doswitch(void *v)
{
	struct drm_i915_private *dev_priv = v;
	struct drm_device *dev = &dev_priv->drm;
	struct rasops_info *ri = &dev_priv->ro;

	rasops_show_screen(ri, dev_priv->switchcookie, 0, NULL, NULL);
	drm_client_dev_restore(dev);

	if (dev_priv->switchcb)
		(*dev_priv->switchcb)(dev_priv->switchcbarg, 0, 0);
}

void
inteldrm_enter_ddb(void *v, void *cookie)
{
	struct drm_i915_private *dev_priv = v;
	struct drm_device *dev = &dev_priv->drm;
	struct rasops_info *ri = &dev_priv->ro;

	if (cookie == ri->ri_active)
		return;

	rasops_show_screen(ri, cookie, 0, NULL, NULL);
	drm_client_dev_restore(dev);
}

int
inteldrm_getchar(void *v, int row, int col, struct wsdisplay_charcell *cell)
{
	struct drm_i915_private *dev_priv = v;
	struct rasops_info *ri = &dev_priv->ro;

	return rasops_getchar(ri, row, col, cell);
}

int
inteldrm_load_font(void *v, void *cookie, struct wsdisplay_font *font)
{
	struct drm_i915_private *dev_priv = v;
	struct rasops_info *ri = &dev_priv->ro;

	return rasops_load_font(ri, cookie, font);
}

int
inteldrm_list_font(void *v, struct wsdisplay_font *font)
{
	struct drm_i915_private *dev_priv = v;
	struct rasops_info *ri = &dev_priv->ro;

	return rasops_list_font(ri, font);
}

void
inteldrm_burner(void *v, u_int on, u_int flags)
{
	struct drm_i915_private *dev_priv = v;

	task_del(systq, &dev_priv->burner_task);

	if (on)
		dev_priv->burner_fblank = FB_BLANK_UNBLANK;
	else {
		if (flags & WSDISPLAY_BURN_VBLANK)
			dev_priv->burner_fblank = FB_BLANK_VSYNC_SUSPEND;
		else
			dev_priv->burner_fblank = FB_BLANK_NORMAL;
	}

	/*
	 * Setting the DPMS mode may sleep while waiting for the display
	 * to come back on so hand things off to a taskq.
	 */
	task_add(systq, &dev_priv->burner_task);
}

void
inteldrm_burner_cb(void *arg1)
{
	struct drm_i915_private *dev_priv = arg1;
	struct drm_device *dev = &dev_priv->drm;
	struct drm_fb_helper *helper = dev->fb_helper;

	drm_fb_helper_blank(dev_priv->burner_fblank, helper->info);
}

int
inteldrm_backlight_update_status(struct backlight_device *bd)
{
	struct wsdisplay_param dp;

	dp.param = WSDISPLAYIO_PARAM_BRIGHTNESS;
	dp.curval = bd->props.brightness;
	ws_set_param(&dp);
	return 0;
}

int
inteldrm_backlight_get_brightness(struct backlight_device *bd)
{
	struct wsdisplay_param dp;

	dp.param = WSDISPLAYIO_PARAM_BRIGHTNESS;
	ws_get_param(&dp);
	return dp.curval;
}

const struct backlight_ops inteldrm_backlight_ops = {
	.update_status = inteldrm_backlight_update_status,
	.get_brightness = inteldrm_backlight_get_brightness
};

void
inteldrm_scrollback(void *v, void *cookie, int lines)
{
	struct drm_i915_private *dev_priv = v;
	struct rasops_info *ri = &dev_priv->ro;

	rasops_scrollback(ri, cookie, lines);
}

int	inteldrm_match(struct device *, void *, void *);
void	inteldrm_attach(struct device *, struct device *, void *);
int	inteldrm_detach(struct device *, int);
int	inteldrm_activate(struct device *, int);
void	inteldrm_attachhook(struct device *);

const struct cfattach inteldrm_ca = {
	sizeof(struct drm_i915_private), inteldrm_match, inteldrm_attach,
	inteldrm_detach, inteldrm_activate
};

struct cfdriver inteldrm_cd = {
	NULL, "inteldrm", DV_DULL
};

int	inteldrm_intr(void *);

/*
 * Set if the mountroot hook has a fatal error.
 */
int	inteldrm_fatal_error;

int
inteldrm_match(struct device *parent, void *match, void *aux)
{
	struct pci_attach_args *pa = aux;
	const struct pci_device_id *id;
	struct intel_device_info *info;

	if (inteldrm_fatal_error)
		return 0;

	id = drm_find_description(PCI_VENDOR(pa->pa_id),
	    PCI_PRODUCT(pa->pa_id), pciidlist);
	if (id != NULL) {
		info = (struct intel_device_info *)id->driver_data;
		if (info->require_force_probe == 0 &&
		    pa->pa_function == 0)
			return 20;
	}

	return 0;
}

int drm_gem_init(struct drm_device *);
void intel_init_stolen_res(struct drm_i915_private *);

void
inteldrm_attach(struct device *parent, struct device *self, void *aux)
{
	struct drm_i915_private *dev_priv = (struct drm_i915_private *)self;
	struct drm_device *dev;
	struct pci_attach_args *pa = aux;
	const struct pci_device_id *id;
	struct intel_device_info *info;
	extern int vga_console_attached;
	int mmio_bar, mmio_size, mmio_type;

	dev_priv->pa = pa;
	dev_priv->pc = pa->pa_pc;
	dev_priv->tag = pa->pa_tag;
	dev_priv->iot = pa->pa_iot;
	dev_priv->dmat = pa->pa_dmat;
	dev_priv->bst = pa->pa_memt;
	dev_priv->memex = pa->pa_memex;
	dev_priv->vga_regs = &dev_priv->bar;

	id = drm_find_description(PCI_VENDOR(pa->pa_id),
	    PCI_PRODUCT(pa->pa_id), pciidlist);
	dev_priv->id = id;
	info = (struct intel_device_info *)id->driver_data;

	if (PCI_CLASS(pa->pa_class) == PCI_CLASS_DISPLAY &&
	    PCI_SUBCLASS(pa->pa_class) == PCI_SUBCLASS_DISPLAY_VGA &&
	    (pci_conf_read(pa->pa_pc, pa->pa_tag, PCI_COMMAND_STATUS_REG)
	    & (PCI_COMMAND_IO_ENABLE | PCI_COMMAND_MEM_ENABLE))
	    == (PCI_COMMAND_IO_ENABLE | PCI_COMMAND_MEM_ENABLE)) {
		dev_priv->primary = 1;
		dev_priv->console = vga_is_console(pa->pa_iot, -1);
		vga_console_attached = 1;
	}

#if NEFIFB > 0
	if (efifb_is_primary(pa)) {
		dev_priv->primary = 1;
		dev_priv->console = efifb_is_console(pa);
		efifb_detach();
	}
#endif

	/*
	 * Meteor Lake GOP framebuffer doesn't pass efifb pci bar tests
	 * too early for IS_METEORLAKE which uses runtime info
	 */
	if (info->platform == INTEL_METEORLAKE) {
		dev_priv->primary = 1;
		dev_priv->console = 1;
#if NEFIFB > 0
		efifb_detach();
#endif
	}

	printf("\n");

	dev = drm_attach_pci(&i915_drm_driver, pa, 0, dev_priv->primary,
	    self, &dev_priv->drm);
	if (dev == NULL) {
		printf("%s: drm attach failed\n", dev_priv->sc_dev.dv_xname);
		return;
	}

	pci_set_drvdata(dev->pdev, &dev_priv->drm);

	/* Device parameters start as a copy of module parameters. */
	i915_params_copy(&dev_priv->params, &i915_modparams);
	dev_priv->params.request_timeout_ms = 0;

	/* Set up device info and initial runtime info. */
	intel_device_info_driver_create(dev_priv, dev->pdev->device, info);

	intel_display_device_probe(dev_priv);

	/* with GuC submission, init sometimes fails on Alder Lake-P */
	if (IS_ALDERLAKE_P(dev_priv))
		dev_priv->params.enable_guc = ENABLE_GUC_LOAD_HUC;

	mmio_bar = (GRAPHICS_VER(dev_priv) == 2) ? 0x14 : 0x10;

	/* from intel_uncore_setup_mmio() */

	/*
	 * Before gen4, the registers and the GTT are behind different BARs.
	 * However, from gen4 onwards, the registers and the GTT are shared
	 * in the same BAR, so we want to restrict this ioremap from
	 * clobbering the GTT which we want ioremap_wc instead. Fortunately,
	 * the register BAR remains the same size for all the earlier
	 * generations up to Ironlake.
	 * For dgfx chips register range is expanded to 4MB, and this larger
	 * range is also used for integrated gpus beginning with Meteor Lake.
	 */
	if (IS_DGFX(dev_priv) || GRAPHICS_VER_FULL(dev_priv) >= IP_VER(12, 70))
		mmio_size = 4 * 1024 * 1024;
	else if (GRAPHICS_VER(dev_priv) >= 5)
		mmio_size = 2 * 1024 * 1024;
	else
		mmio_size = 512 * 1024;

	mmio_type = pci_mapreg_type(pa->pa_pc, pa->pa_tag, mmio_bar);
	if (pci_mapreg_map(pa, mmio_bar, mmio_type, BUS_SPACE_MAP_LINEAR,
	    &dev_priv->vga_regs->bst, &dev_priv->vga_regs->bsh,
	    &dev_priv->vga_regs->base, &dev_priv->vga_regs->size, mmio_size)) {
		printf("%s: can't map registers\n",
		    dev_priv->sc_dev.dv_xname);
		return;
	}
	dev_priv->uncore.regs = bus_space_vaddr(dev_priv->vga_regs->bst,
	     dev_priv->vga_regs->bsh);
	if (dev_priv->uncore.regs == NULL) {
		printf("%s: bus_space_vaddr registers failed\n",
		    dev_priv->sc_dev.dv_xname);
		return;
	}

#if NINTAGP > 0
	if (GRAPHICS_VER(dev_priv) <= 5) {
		config_found_sm(self, aux, intagp_print, intagpsubmatch);
		dev->agp = drm_legacy_agp_init(dev);
		if (dev->agp) {
			if (drm_mtrr_add(dev->agp->info.ai_aperture_base,
			    dev->agp->info.ai_aperture_size, DRM_MTRR_WC) == 0)
				dev->agp->mtrr = 1;
		}
	}
#endif

	if (GRAPHICS_VER(dev_priv) < 5)
		pa->pa_flags &= ~PCI_FLAGS_MSI_ENABLED;

	if (pci_intr_map_msi(pa, &dev_priv->ih) != 0 &&
	    pci_intr_map(pa, &dev_priv->ih) != 0) {
		printf("%s: couldn't map interrupt\n",
		    dev_priv->sc_dev.dv_xname);
		return;
	}

	printf("%s: %s, %s, gen %d\n", dev_priv->sc_dev.dv_xname,
	    pci_intr_string(dev_priv->pc, dev_priv->ih),
	    intel_platform_name(INTEL_INFO(dev_priv)->platform),
	    GRAPHICS_VER(dev_priv));

	dev_priv->irqh = pci_intr_establish(dev_priv->pc, dev_priv->ih,
	    IPL_TTY, inteldrm_intr, dev_priv, dev_priv->sc_dev.dv_xname);
	if (dev_priv->irqh == NULL) {
		printf("%s: couldn't establish interrupt\n",
		    dev_priv->sc_dev.dv_xname);
		return;
	}
	dev->pdev->irq = -1;
	intel_gmch_bridge_setup(dev_priv);
	intel_init_stolen_res(dev_priv);

	config_mountroot(self, inteldrm_attachhook);
}

void
inteldrm_forcedetach(struct drm_i915_private *dev_priv)
{
#ifdef notyet
	struct pci_softc *psc = (struct pci_softc *)dev_priv->sc_dev.dv_parent;
	pcitag_t tag = dev_priv->tag;
#endif
	extern int vga_console_attached;

	if (dev_priv->primary) {
		vga_console_attached = 0;
#if NEFIFB > 0
		efifb_reattach();
#endif
	}

#ifdef notyet
	config_detach(&dev_priv->sc_dev, 0);
	pci_probe_device(psc, tag, NULL, NULL);
#endif
}

extern int __init i915_init(void);

void
inteldrm_attachhook(struct device *self)
{
	struct drm_i915_private *dev_priv = (struct drm_i915_private *)self;
	struct rasops_info *ri = &dev_priv->ro;
	struct wsemuldisplaydev_attach_args aa;
	const struct pci_device_id *id = dev_priv->id;
	int orientation_quirk;

	if (inteldrm_refcnt == 0) {
		i915_init();
	}
	inteldrm_refcnt++;

	if (i915_driver_probe(dev_priv, id))
		goto fail;

	if (ri->ri_bits == NULL)
		goto fail;

	printf("%s: %dx%d, %dbpp\n", dev_priv->sc_dev.dv_xname,
	    ri->ri_width, ri->ri_height, ri->ri_depth);

	ri->ri_flg = RI_CENTER | RI_WRONLY | RI_VCONS | RI_CLEAR;

	orientation_quirk = drm_get_panel_orientation_quirk(ri->ri_width,
	    ri->ri_height);
	if (orientation_quirk == DRM_MODE_PANEL_ORIENTATION_LEFT_UP)
		ri->ri_flg |= RI_ROTATE_CCW;
	else if (orientation_quirk == DRM_MODE_PANEL_ORIENTATION_RIGHT_UP)
		ri->ri_flg |= RI_ROTATE_CW;

	ri->ri_hw = dev_priv;
	rasops_init(ri, 160, 160);

	task_set(&dev_priv->switchtask, inteldrm_doswitch, dev_priv);
	task_set(&dev_priv->burner_task, inteldrm_burner_cb, dev_priv);

	inteldrm_stdscreen.capabilities = ri->ri_caps;
	inteldrm_stdscreen.nrows = ri->ri_rows;
	inteldrm_stdscreen.ncols = ri->ri_cols;
	inteldrm_stdscreen.textops = &ri->ri_ops;
	inteldrm_stdscreen.fontwidth = ri->ri_font->fontwidth;
	inteldrm_stdscreen.fontheight = ri->ri_font->fontheight;

	aa.console = dev_priv->console;
	aa.primary = dev_priv->primary;
	aa.scrdata = &inteldrm_screenlist;
	aa.accessops = &inteldrm_accessops;
	aa.accesscookie = dev_priv;
	aa.defaultscreens = 0;

	if (dev_priv->console) {
		uint32_t defattr;

		/*
		 * Clear the entire screen if we're doing rotation to
		 * make sure no unrotated content survives.
		 */
		if (ri->ri_flg & (RI_ROTATE_CW | RI_ROTATE_CCW))
			memset(ri->ri_bits, 0, ri->ri_height * ri->ri_stride);

		ri->ri_ops.pack_attr(ri->ri_active, 0, 0, 0, &defattr);
		wsdisplay_cnattach(&inteldrm_stdscreen, ri->ri_active,
		    0, 0, defattr);
	}

	config_found_sm(self, &aa, wsemuldisplaydevprint,
	    wsemuldisplaydevsubmatch);
	return;

fail:
	inteldrm_fatal_error = 1;
	inteldrm_forcedetach(dev_priv);
}

int
inteldrm_detach(struct device *self, int flags)
{
	return 0;
}

int
inteldrm_activate(struct device *self, int act)
{
	struct drm_i915_private *dev_priv = (struct drm_i915_private *)self;
	struct drm_device *dev = &dev_priv->drm;
	int rv = 0;

	if (dev->dev == NULL || inteldrm_fatal_error)
		return (0);

	/*
	 * On hibernate resume activate is called before inteldrm_attachhook().
	 * Do not try to call i915_drm_suspend() when
	 * i915_load_modeset_init()/i915_gem_init() have not been called.
	 */
	if (dev_priv->display.wq.modeset == NULL)
		return 0;

#ifdef DDB
	if (db_suspend)
		return config_suspend(dev->dev, act);
#endif

	switch (act) {
	case DVACT_QUIESCE:
		rv = config_suspend(dev->dev, act);
		i915_drm_prepare(dev);
		i915_drm_suspend(dev);
		i915_drm_suspend_late(dev, false);
		break;
	case DVACT_SUSPEND:
		if (dev->agp)
			config_suspend(dev->agp->agpdev->sc_chipc, act);
		break;
	case DVACT_RESUME:
		if (dev->agp)
			config_suspend(dev->agp->agpdev->sc_chipc, act);
		break;
	case DVACT_WAKEUP:
		i915_drm_resume_early(dev);
		i915_drm_resume(dev);
		drm_client_dev_restore(dev);
		rv = config_suspend(dev->dev, act);
		break;
	}

	return (rv);
}

void
inteldrm_native_backlight(struct drm_i915_private *dev_priv)
{
	struct drm_device *dev = &dev_priv->drm;
	struct drm_connector_list_iter conn_iter;
	struct drm_connector *connector;

	drm_connector_list_iter_begin(dev, &conn_iter);
	drm_for_each_connector_iter(connector, &conn_iter) {
		struct intel_connector *intel_connector;
		struct intel_panel *panel;
		struct backlight_device *bd;

		if (connector->registration_state != DRM_CONNECTOR_REGISTERED)
			continue;

		intel_connector = to_intel_connector(connector);
		panel = &intel_connector->panel;
		bd = panel->backlight.device;

		if (!panel->backlight.present || bd == NULL)
			continue;

		dev->registered = false;
		connector->registration_state = DRM_CONNECTOR_UNREGISTERED;

		connector->backlight_device = bd;
		connector->backlight_property = drm_property_create_range(dev,
		    0, "Backlight", 0, bd->props.max_brightness);
		drm_object_attach_property(&connector->base,
		    connector->backlight_property, bd->props.brightness);

		connector->registration_state = DRM_CONNECTOR_REGISTERED;
		dev->registered = true;

		/*
		 * Use backlight from the first connector that has one
		 * for wscons(4).
		 */
		if (dev_priv->backlight == NULL)
			dev_priv->backlight = bd;
	}
	drm_connector_list_iter_end(&conn_iter);
}

void
inteldrm_firmware_backlight(struct drm_i915_private *dev_priv,
    struct wsdisplay_param *dp)
{
	struct drm_device *dev = &dev_priv->drm;
	struct drm_connector_list_iter conn_iter;
	struct drm_connector *connector;
	struct backlight_properties props;
	struct backlight_device *bd;

	memset(&props, 0, sizeof(props));
	props.type = BACKLIGHT_FIRMWARE;
	props.brightness = dp->curval;
	bd = backlight_device_register(dev->dev->dv_xname, NULL, NULL,
	    &inteldrm_backlight_ops, &props);

	drm_connector_list_iter_begin(dev, &conn_iter);
	drm_for_each_connector_iter(connector, &conn_iter) {
		if (connector->connector_type != DRM_MODE_CONNECTOR_LVDS &&
		    connector->connector_type != DRM_MODE_CONNECTOR_eDP &&
		    connector->connector_type != DRM_MODE_CONNECTOR_DSI)
			continue;

		if (connector->registration_state != DRM_CONNECTOR_REGISTERED)
			continue;

		dev->registered = false;
		connector->registration_state = DRM_CONNECTOR_UNREGISTERED;

		connector->backlight_device = bd;
		connector->backlight_property = drm_property_create_range(dev,
		    0, "Backlight", dp->min, dp->max);
		drm_object_attach_property(&connector->base,
		    connector->backlight_property, dp->curval);

		connector->registration_state = DRM_CONNECTOR_REGISTERED;
		dev->registered = true;
	}
	drm_connector_list_iter_end(&conn_iter);
}

void
inteldrm_init_backlight(struct drm_i915_private *dev_priv)
{
	struct wsdisplay_param dp;

	dp.param = WSDISPLAYIO_PARAM_BRIGHTNESS;
	if (ws_get_param && ws_get_param(&dp) == 0)
		inteldrm_firmware_backlight(dev_priv, &dp);
	else
		inteldrm_native_backlight(dev_priv);
}

int
inteldrm_intr(void *arg)
{
	struct drm_i915_private *dev_priv = arg;

	if (dev_priv->irq_handler)
		return dev_priv->irq_handler(0, dev_priv);

	return 0;
}

#endif
