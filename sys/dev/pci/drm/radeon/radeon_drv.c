/*
 * \file radeon_drv.c
 * ATI Radeon driver
 *
 * \author Gareth Hughes <gareth@valinux.com>
 */

/*
 * Copyright 2000 VA Linux Systems, Inc., Sunnyvale, California.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */


#include <linux/compat.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/vga_switcheroo.h>
#include <linux/mmu_notifier.h>
#include <linux/pci.h>

#include <drm/drm_aperture.h>
#include <drm/drm_drv.h>
#include <drm/drm_file.h>
#include <drm/drm_gem.h>
#include <drm/drm_ioctl.h>
#include <drm/drm_pciids.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_vblank.h>
#include <drm/radeon_drm.h>

#include "radeon_drv.h"
#include "radeon.h"
#include "radeon_kms.h"
#include "radeon_ttm.h"
#include "radeon_device.h"
#include "radeon_prime.h"

/*
 * KMS wrapper.
 * - 2.0.0 - initial interface
 * - 2.1.0 - add square tiling interface
 * - 2.2.0 - add r6xx/r7xx const buffer support
 * - 2.3.0 - add MSPOS + 3D texture + r500 VAP regs
 * - 2.4.0 - add crtc id query
 * - 2.5.0 - add get accel 2 to work around ddx breakage for evergreen
 * - 2.6.0 - add tiling config query (r6xx+), add initial HiZ support (r300->r500)
 *   2.7.0 - fixups for r600 2D tiling support. (no external ABI change), add eg dyn gpr regs
 *   2.8.0 - pageflip support, r500 US_FORMAT regs. r500 ARGB2101010 colorbuf, r300->r500 CMASK, clock crystal query
 *   2.9.0 - r600 tiling (s3tc,rgtc) working, SET_PREDICATION packet 3 on r600 + eg, backend query
 *   2.10.0 - fusion 2D tiling
 *   2.11.0 - backend map, initial compute support for the CS checker
 *   2.12.0 - RADEON_CS_KEEP_TILING_FLAGS
 *   2.13.0 - virtual memory support, streamout
 *   2.14.0 - add evergreen tiling informations
 *   2.15.0 - add max_pipes query
 *   2.16.0 - fix evergreen 2D tiled surface calculation
 *   2.17.0 - add STRMOUT_BASE_UPDATE for r7xx
 *   2.18.0 - r600-eg: allow "invalid" DB formats
 *   2.19.0 - r600-eg: MSAA textures
 *   2.20.0 - r600-si: RADEON_INFO_TIMESTAMP query
 *   2.21.0 - r600-r700: FMASK and CMASK
 *   2.22.0 - r600 only: RESOLVE_BOX allowed
 *   2.23.0 - allow STRMOUT_BASE_UPDATE on RS780 and RS880
 *   2.24.0 - eg only: allow MIP_ADDRESS=0 for MSAA textures
 *   2.25.0 - eg+: new info request for num SE and num SH
 *   2.26.0 - r600-eg: fix htile size computation
 *   2.27.0 - r600-SI: Add CS ioctl support for async DMA
 *   2.28.0 - r600-eg: Add MEM_WRITE packet support
 *   2.29.0 - R500 FP16 color clear registers
 *   2.30.0 - fix for FMASK texturing
 *   2.31.0 - Add fastfb support for rs690
 *   2.32.0 - new info request for rings working
 *   2.33.0 - Add SI tiling mode array query
 *   2.34.0 - Add CIK tiling mode array query
 *   2.35.0 - Add CIK macrotile mode array query
 *   2.36.0 - Fix CIK DCE tiling setup
 *   2.37.0 - allow GS ring setup on r6xx/r7xx
 *   2.38.0 - RADEON_GEM_OP (GET_INITIAL_DOMAIN, SET_INITIAL_DOMAIN),
 *            CIK: 1D and linear tiling modes contain valid PIPE_CONFIG
 *   2.39.0 - Add INFO query for number of active CUs
 *   2.40.0 - Add RADEON_GEM_GTT_WC/UC, flush HDP cache before submitting
 *            CS to GPU on >= r600
 *   2.41.0 - evergreen/cayman: Add SET_BASE/DRAW_INDIRECT command parsing support
 *   2.42.0 - Add VCE/VUI (Video Usability Information) support
 *   2.43.0 - RADEON_INFO_GPU_RESET_COUNTER
 *   2.44.0 - SET_APPEND_CNT packet3 support
 *   2.45.0 - Allow setting shader registers using DMA/COPY packet3 on SI
 *   2.46.0 - Add PFP_SYNC_ME support on evergreen
 *   2.47.0 - Add UVD_NO_OP register support
 *   2.48.0 - TA_CS_BC_BASE_ADDR allowed on SI
 *   2.49.0 - DRM_RADEON_GEM_INFO ioctl returns correct vram_size/visible values
 *   2.50.0 - Allows unaligned shader loads on CIK. (needed by OpenGL)
 */
#define KMS_DRIVER_MAJOR	2
#define KMS_DRIVER_MINOR	50
#define KMS_DRIVER_PATCHLEVEL	0

int radeon_no_wb;
int radeon_modeset = -1;
int radeon_dynclks = -1;
int radeon_r4xx_atom;
int radeon_agpmode = -1;
int radeon_vram_limit;
int radeon_gart_size = -1; /* auto */
int radeon_benchmarking;
int radeon_testing;
int radeon_connector_table;
int radeon_tv = 1;
int radeon_audio = -1;
int radeon_disp_priority;
int radeon_hw_i2c;
int radeon_pcie_gen2 = -1;
int radeon_msi = -1;
int radeon_lockup_timeout = 10000;
int radeon_fastfb;
int radeon_dpm = -1;
int radeon_aspm = -1;
int radeon_runtime_pm = -1;
int radeon_hard_reset;
int radeon_vm_size = 8;
int radeon_vm_block_size = -1;
int radeon_deep_color;
int radeon_use_pflipirq = 2;
int radeon_bapm = -1;
int radeon_backlight = -1;
int radeon_auxch = -1;
int radeon_uvd = 1;
int radeon_vce = 1;

MODULE_PARM_DESC(no_wb, "Disable AGP writeback for scratch registers");
module_param_named(no_wb, radeon_no_wb, int, 0444);

MODULE_PARM_DESC(modeset, "Disable/Enable modesetting");
module_param_named(modeset, radeon_modeset, int, 0400);

MODULE_PARM_DESC(dynclks, "Disable/Enable dynamic clocks");
module_param_named(dynclks, radeon_dynclks, int, 0444);

MODULE_PARM_DESC(r4xx_atom, "Enable ATOMBIOS modesetting for R4xx");
module_param_named(r4xx_atom, radeon_r4xx_atom, int, 0444);

MODULE_PARM_DESC(vramlimit, "Restrict VRAM for testing, in megabytes");
module_param_named(vramlimit, radeon_vram_limit, int, 0600);

MODULE_PARM_DESC(agpmode, "AGP Mode (-1 == PCI)");
module_param_named(agpmode, radeon_agpmode, int, 0444);

MODULE_PARM_DESC(gartsize, "Size of PCIE/IGP gart to setup in megabytes (32, 64, etc., -1 = auto)");
module_param_named(gartsize, radeon_gart_size, int, 0600);

MODULE_PARM_DESC(benchmark, "Run benchmark");
module_param_named(benchmark, radeon_benchmarking, int, 0444);

MODULE_PARM_DESC(test, "Run tests");
module_param_named(test, radeon_testing, int, 0444);

MODULE_PARM_DESC(connector_table, "Force connector table");
module_param_named(connector_table, radeon_connector_table, int, 0444);

MODULE_PARM_DESC(tv, "TV enable (0 = disable)");
module_param_named(tv, radeon_tv, int, 0444);

MODULE_PARM_DESC(audio, "Audio enable (-1 = auto, 0 = disable, 1 = enable)");
module_param_named(audio, radeon_audio, int, 0444);

MODULE_PARM_DESC(disp_priority, "Display Priority (0 = auto, 1 = normal, 2 = high)");
module_param_named(disp_priority, radeon_disp_priority, int, 0444);

MODULE_PARM_DESC(hw_i2c, "hw i2c engine enable (0 = disable)");
module_param_named(hw_i2c, radeon_hw_i2c, int, 0444);

MODULE_PARM_DESC(pcie_gen2, "PCIE Gen2 mode (-1 = auto, 0 = disable, 1 = enable)");
module_param_named(pcie_gen2, radeon_pcie_gen2, int, 0444);

MODULE_PARM_DESC(msi, "MSI support (1 = enable, 0 = disable, -1 = auto)");
module_param_named(msi, radeon_msi, int, 0444);

MODULE_PARM_DESC(lockup_timeout, "GPU lockup timeout in ms (default 10000 = 10 seconds, 0 = disable)");
module_param_named(lockup_timeout, radeon_lockup_timeout, int, 0444);

MODULE_PARM_DESC(fastfb, "Direct FB access for IGP chips (0 = disable, 1 = enable)");
module_param_named(fastfb, radeon_fastfb, int, 0444);

MODULE_PARM_DESC(dpm, "DPM support (1 = enable, 0 = disable, -1 = auto)");
module_param_named(dpm, radeon_dpm, int, 0444);

MODULE_PARM_DESC(aspm, "ASPM support (1 = enable, 0 = disable, -1 = auto)");
module_param_named(aspm, radeon_aspm, int, 0444);

MODULE_PARM_DESC(runpm, "PX runtime pm (1 = force enable, 0 = disable, -1 = PX only default)");
module_param_named(runpm, radeon_runtime_pm, int, 0444);

MODULE_PARM_DESC(hard_reset, "PCI config reset (1 = force enable, 0 = disable (default))");
module_param_named(hard_reset, radeon_hard_reset, int, 0444);

MODULE_PARM_DESC(vm_size, "VM address space size in gigabytes (default 4GB)");
module_param_named(vm_size, radeon_vm_size, int, 0444);

MODULE_PARM_DESC(vm_block_size, "VM page table size in bits (default depending on vm_size)");
module_param_named(vm_block_size, radeon_vm_block_size, int, 0444);

MODULE_PARM_DESC(deep_color, "Deep Color support (1 = enable, 0 = disable (default))");
module_param_named(deep_color, radeon_deep_color, int, 0444);

MODULE_PARM_DESC(use_pflipirq, "Pflip irqs for pageflip completion (0 = disable, 1 = as fallback, 2 = exclusive (default))");
module_param_named(use_pflipirq, radeon_use_pflipirq, int, 0444);

MODULE_PARM_DESC(bapm, "BAPM support (1 = enable, 0 = disable, -1 = auto)");
module_param_named(bapm, radeon_bapm, int, 0444);

MODULE_PARM_DESC(backlight, "backlight support (1 = enable, 0 = disable, -1 = auto)");
module_param_named(backlight, radeon_backlight, int, 0444);

MODULE_PARM_DESC(auxch, "Use native auxch experimental support (1 = enable, 0 = disable, -1 = auto)");
module_param_named(auxch, radeon_auxch, int, 0444);

MODULE_PARM_DESC(uvd, "uvd enable/disable uvd support (1 = enable, 0 = disable)");
module_param_named(uvd, radeon_uvd, int, 0444);

MODULE_PARM_DESC(vce, "vce enable/disable vce support (1 = enable, 0 = disable)");
module_param_named(vce, radeon_vce, int, 0444);

int radeon_si_support = 1;
MODULE_PARM_DESC(si_support, "SI support (1 = enabled (default), 0 = disabled)");
module_param_named(si_support, radeon_si_support, int, 0444);

int radeon_cik_support = 1;
MODULE_PARM_DESC(cik_support, "CIK support (1 = enabled (default), 0 = disabled)");
module_param_named(cik_support, radeon_cik_support, int, 0444);

static const struct pci_device_id pciidlist[] = {
	radeon_PCI_IDS
};

MODULE_DEVICE_TABLE(pci, pciidlist);

static const struct drm_driver kms_driver;

#ifdef __linux__
static int radeon_pci_probe(struct pci_dev *pdev,
			    const struct pci_device_id *ent)
{
	unsigned long flags = 0;
	struct drm_device *ddev;
	struct radeon_device *rdev;
	int ret;

	if (!ent)
		return -ENODEV; /* Avoid NULL-ptr deref in drm_get_pci_dev */

	flags = ent->driver_data;

	if (!radeon_si_support) {
		switch (flags & RADEON_FAMILY_MASK) {
		case CHIP_TAHITI:
		case CHIP_PITCAIRN:
		case CHIP_VERDE:
		case CHIP_OLAND:
		case CHIP_HAINAN:
			dev_info(&pdev->dev,
				 "SI support disabled by module param\n");
			return -ENODEV;
		}
	}
	if (!radeon_cik_support) {
		switch (flags & RADEON_FAMILY_MASK) {
		case CHIP_KAVERI:
		case CHIP_BONAIRE:
		case CHIP_HAWAII:
		case CHIP_KABINI:
		case CHIP_MULLINS:
			dev_info(&pdev->dev,
				 "CIK support disabled by module param\n");
			return -ENODEV;
		}
	}

	if (vga_switcheroo_client_probe_defer(pdev))
		return -EPROBE_DEFER;

	/* Get rid of things like offb */
	ret = drm_aperture_remove_conflicting_pci_framebuffers(pdev, &kms_driver);
	if (ret)
		return ret;

	rdev = devm_drm_dev_alloc(&pdev->dev, &kms_driver, typeof(*rdev), ddev);
	if (IS_ERR(rdev))
		return PTR_ERR(rdev);

	rdev->dev = &pdev->dev;
	rdev->pdev = pdev;
	ddev = rdev_to_drm(rdev);
	ddev->dev_private = rdev;

	ret = pci_enable_device(pdev);
	if (ret)
		goto err_free;

	pci_set_drvdata(pdev, ddev);

	ret = radeon_driver_load_kms(ddev, flags);
	if (ret)
		goto err_agp;

	ret = drm_dev_register(ddev, flags);
	if (ret)
		goto err_agp;

	radeon_fbdev_setup(ddev->dev_private);

	return 0;

err_agp:
	pci_disable_device(pdev);
err_free:
	drm_dev_put(ddev);
	return ret;
}

static void
radeon_pci_remove(struct pci_dev *pdev)
{
	struct drm_device *dev = pci_get_drvdata(pdev);

	drm_put_dev(dev);
}

static void
radeon_pci_shutdown(struct pci_dev *pdev)
{
	/* if we are running in a VM, make sure the device
	 * torn down properly on reboot/shutdown
	 */
	if (radeon_device_is_virtual())
		radeon_pci_remove(pdev);

#if defined(CONFIG_PPC64) || defined(CONFIG_MACH_LOONGSON64)
	/*
	 * Some adapters need to be suspended before a
	 * shutdown occurs in order to prevent an error
	 * during kexec, shutdown or reboot.
	 * Make this power and Loongson specific because
	 * it breaks some other boards.
	 */
	radeon_suspend_kms(pci_get_drvdata(pdev), true, true, false);
#endif
}

static int radeon_pmops_suspend(struct device *dev)
{
	struct drm_device *drm_dev = dev_get_drvdata(dev);

	return radeon_suspend_kms(drm_dev, true, true, false);
}

static int radeon_pmops_resume(struct device *dev)
{
	struct drm_device *drm_dev = dev_get_drvdata(dev);

	/* GPU comes up enabled by the bios on resume */
	if (radeon_is_px(drm_dev)) {
		pm_runtime_disable(dev);
		pm_runtime_set_active(dev);
		pm_runtime_enable(dev);
	}

	return radeon_resume_kms(drm_dev, true, true);
}

static int radeon_pmops_freeze(struct device *dev)
{
	struct drm_device *drm_dev = dev_get_drvdata(dev);

	return radeon_suspend_kms(drm_dev, false, true, true);
}

static int radeon_pmops_thaw(struct device *dev)
{
	struct drm_device *drm_dev = dev_get_drvdata(dev);

	return radeon_resume_kms(drm_dev, false, true);
}

static int radeon_pmops_runtime_suspend(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct drm_device *drm_dev = pci_get_drvdata(pdev);

	if (!radeon_is_px(drm_dev)) {
		pm_runtime_forbid(dev);
		return -EBUSY;
	}

	drm_dev->switch_power_state = DRM_SWITCH_POWER_CHANGING;
	drm_kms_helper_poll_disable(drm_dev);

	radeon_suspend_kms(drm_dev, false, false, false);
	pci_save_state(pdev);
	pci_disable_device(pdev);
	pci_ignore_hotplug(pdev);
	if (radeon_is_atpx_hybrid())
		pci_set_power_state(pdev, PCI_D3cold);
	else if (!radeon_has_atpx_dgpu_power_cntl())
		pci_set_power_state(pdev, PCI_D3hot);
	drm_dev->switch_power_state = DRM_SWITCH_POWER_DYNAMIC_OFF;

	return 0;
}

static int radeon_pmops_runtime_resume(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct drm_device *drm_dev = pci_get_drvdata(pdev);
	int ret;

	if (!radeon_is_px(drm_dev))
		return -EINVAL;

	drm_dev->switch_power_state = DRM_SWITCH_POWER_CHANGING;

	if (radeon_is_atpx_hybrid() ||
	    !radeon_has_atpx_dgpu_power_cntl())
		pci_set_power_state(pdev, PCI_D0);
	pci_restore_state(pdev);
	ret = pci_enable_device(pdev);
	if (ret)
		return ret;
	pci_set_master(pdev);

	ret = radeon_resume_kms(drm_dev, false, false);
	drm_kms_helper_poll_enable(drm_dev);
	drm_dev->switch_power_state = DRM_SWITCH_POWER_ON;
	return 0;
}

static int radeon_pmops_runtime_idle(struct device *dev)
{
	struct drm_device *drm_dev = dev_get_drvdata(dev);
	struct drm_crtc *crtc;

	if (!radeon_is_px(drm_dev)) {
		pm_runtime_forbid(dev);
		return -EBUSY;
	}

	list_for_each_entry(crtc, &drm_dev->mode_config.crtc_list, head) {
		if (crtc->enabled) {
			DRM_DEBUG_DRIVER("failing to power off - crtc active\n");
			return -EBUSY;
		}
	}

	pm_runtime_mark_last_busy(dev);
	pm_runtime_autosuspend(dev);
	/* we don't want the main rpm_idle to call suspend - we want to autosuspend */
	return 1;
}

long radeon_drm_ioctl(struct file *filp,
		      unsigned int cmd, unsigned long arg)
{
	struct drm_file *file_priv = filp->private_data;
	struct drm_device *dev;
	long ret;

	dev = file_priv->minor->dev;
	ret = pm_runtime_get_sync(dev->dev);
	if (ret < 0) {
		pm_runtime_put_autosuspend(dev->dev);
		return ret;
	}

	ret = drm_ioctl(filp, cmd, arg);

	pm_runtime_mark_last_busy(dev->dev);
	pm_runtime_put_autosuspend(dev->dev);
	return ret;
}

#ifdef CONFIG_COMPAT
static long radeon_kms_compat_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	unsigned int nr = DRM_IOCTL_NR(cmd);

	if (nr < DRM_COMMAND_BASE)
		return drm_compat_ioctl(filp, cmd, arg);

	return radeon_drm_ioctl(filp, cmd, arg);
}
#endif

static const struct dev_pm_ops radeon_pm_ops = {
	.suspend = radeon_pmops_suspend,
	.resume = radeon_pmops_resume,
	.freeze = radeon_pmops_freeze,
	.thaw = radeon_pmops_thaw,
	.poweroff = radeon_pmops_freeze,
	.restore = radeon_pmops_resume,
	.runtime_suspend = radeon_pmops_runtime_suspend,
	.runtime_resume = radeon_pmops_runtime_resume,
	.runtime_idle = radeon_pmops_runtime_idle,
};

static const struct file_operations radeon_driver_kms_fops = {
	.owner = THIS_MODULE,
	.open = drm_open,
	.release = drm_release,
	.unlocked_ioctl = radeon_drm_ioctl,
	.mmap = drm_gem_mmap,
	.poll = drm_poll,
	.read = drm_read,
#ifdef CONFIG_COMPAT
	.compat_ioctl = radeon_kms_compat_ioctl,
#endif
	.fop_flags = FOP_UNSIGNED_OFFSET,
};

#endif /* __linux__ */

static const struct drm_ioctl_desc radeon_ioctls_kms[] = {
	DRM_IOCTL_DEF_DRV(RADEON_CP_INIT, drm_invalid_op, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF_DRV(RADEON_CP_START, drm_invalid_op, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF_DRV(RADEON_CP_STOP, drm_invalid_op, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF_DRV(RADEON_CP_RESET, drm_invalid_op, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF_DRV(RADEON_CP_IDLE, drm_invalid_op, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(RADEON_CP_RESUME, drm_invalid_op, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(RADEON_RESET, drm_invalid_op, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(RADEON_FULLSCREEN, drm_invalid_op, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(RADEON_SWAP, drm_invalid_op, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(RADEON_CLEAR, drm_invalid_op, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(RADEON_VERTEX, drm_invalid_op, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(RADEON_INDICES, drm_invalid_op, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(RADEON_TEXTURE, drm_invalid_op, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(RADEON_STIPPLE, drm_invalid_op, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(RADEON_INDIRECT, drm_invalid_op, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF_DRV(RADEON_VERTEX2, drm_invalid_op, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(RADEON_CMDBUF, drm_invalid_op, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(RADEON_GETPARAM, drm_invalid_op, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(RADEON_FLIP, drm_invalid_op, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(RADEON_ALLOC, drm_invalid_op, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(RADEON_FREE, drm_invalid_op, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(RADEON_INIT_HEAP, drm_invalid_op, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF_DRV(RADEON_IRQ_EMIT, drm_invalid_op, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(RADEON_IRQ_WAIT, drm_invalid_op, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(RADEON_SETPARAM, drm_invalid_op, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(RADEON_SURF_ALLOC, drm_invalid_op, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(RADEON_SURF_FREE, drm_invalid_op, DRM_AUTH),
	/* KMS */
	DRM_IOCTL_DEF_DRV(RADEON_GEM_INFO, radeon_gem_info_ioctl, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(RADEON_GEM_CREATE, radeon_gem_create_ioctl, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(RADEON_GEM_MMAP, radeon_gem_mmap_ioctl, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(RADEON_GEM_SET_DOMAIN, radeon_gem_set_domain_ioctl, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(RADEON_GEM_WAIT_IDLE, radeon_gem_wait_idle_ioctl, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(RADEON_CS, radeon_cs_ioctl, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(RADEON_INFO, radeon_info_ioctl, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(RADEON_GEM_SET_TILING, radeon_gem_set_tiling_ioctl, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(RADEON_GEM_GET_TILING, radeon_gem_get_tiling_ioctl, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(RADEON_GEM_BUSY, radeon_gem_busy_ioctl, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(RADEON_GEM_VA, radeon_gem_va_ioctl, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(RADEON_GEM_OP, radeon_gem_op_ioctl, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(RADEON_GEM_USERPTR, radeon_gem_userptr_ioctl, DRM_AUTH|DRM_RENDER_ALLOW),
};

static const struct drm_driver kms_driver = {
	.driver_features =
	    DRIVER_GEM | DRIVER_RENDER | DRIVER_MODESET,
	.open = radeon_driver_open_kms,
#ifdef __OpenBSD__
	.mmap = drm_gem_mmap,
	.gem_size = sizeof(struct radeon_bo),
#endif
	.postclose = radeon_driver_postclose_kms,
#ifdef notyet
	.unload = radeon_driver_unload_kms,
#endif
	.ioctls = radeon_ioctls_kms,
	.num_ioctls = ARRAY_SIZE(radeon_ioctls_kms),
	.dumb_create = radeon_mode_dumb_create,
	.dumb_map_offset = radeon_mode_dumb_mmap,
#ifdef __linux__
	.fops = &radeon_driver_kms_fops,

	.gem_prime_import_sg_table = radeon_gem_prime_import_sg_table,
#endif

	.name = DRIVER_NAME,
	.desc = DRIVER_DESC,
	.date = DRIVER_DATE,
	.major = KMS_DRIVER_MAJOR,
	.minor = KMS_DRIVER_MINOR,
	.patchlevel = KMS_DRIVER_PATCHLEVEL,
};

#ifdef __linux__
static struct pci_driver radeon_kms_pci_driver = {
	.name = DRIVER_NAME,
	.id_table = pciidlist,
	.probe = radeon_pci_probe,
	.remove = radeon_pci_remove,
	.shutdown = radeon_pci_shutdown,
	.driver.pm = &radeon_pm_ops,
};
#endif

#ifdef notyet
static int __init radeon_module_init(void)
{
	if (drm_firmware_drivers_only() && radeon_modeset == -1)
		radeon_modeset = 0;

	if (radeon_modeset == 0)
		return -EINVAL;

	DRM_INFO("radeon kernel modesetting enabled.\n");
	radeon_register_atpx_handler();

	return pci_register_driver(&radeon_kms_pci_driver);
}

static void __exit radeon_module_exit(void)
{
	pci_unregister_driver(&radeon_kms_pci_driver);
	radeon_unregister_atpx_handler();
	mmu_notifier_synchronize();
}
#endif /* notyet */

module_init(radeon_module_init);
module_exit(radeon_module_exit);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL and additional rights");

#if defined(CONFIG_VGA_SWITCHEROO)
bool radeon_has_atpx(void);
#else
static inline bool radeon_has_atpx(void) { return false; }
#endif

#include <drm/drm_fb_helper.h>
#include "vga.h"

#if NVGA > 0
#include <dev/ic/mc6845reg.h>
#include <dev/ic/pcdisplayvar.h>
#include <dev/ic/vgareg.h>
#include <dev/ic/vgavar.h>

extern int vga_console_attached;
#endif

#ifdef __amd64__
#include "efifb.h"
#include <machine/biosvar.h>
#endif

#if NEFIFB > 0
#include <machine/efifbvar.h>
#endif

int	radeondrm_probe(struct device *, void *, void *);
void	radeondrm_attach_kms(struct device *, struct device *, void *);
int	radeondrm_detach_kms(struct device *, int);
int	radeondrm_activate_kms(struct device *, int);
void	radeondrm_attachhook(struct device *);
int	radeondrm_forcedetach(struct radeon_device *);

bool		radeon_msi_ok(struct radeon_device *);
irqreturn_t	radeon_driver_irq_handler_kms(void *);

/*
 * set if the mountroot hook has a fatal error
 * such as not being able to find the firmware on newer cards
 */
int radeon_fatal_error;

const struct cfattach radeondrm_ca = {
        sizeof (struct radeon_device), radeondrm_probe, radeondrm_attach_kms,
        radeondrm_detach_kms, radeondrm_activate_kms
};

struct cfdriver radeondrm_cd = { 
	NULL, "radeondrm", DV_DULL
};

int
radeondrm_probe(struct device *parent, void *match, void *aux)
{
	if (radeon_fatal_error)
		return 0;
	if (drm_pciprobe(aux, pciidlist))
		return 20;
	return 0;
}

int
radeondrm_detach_kms(struct device *self, int flags)
{
	struct radeon_device *rdev = (struct radeon_device *)self;

	if (rdev == NULL)
		return 0;

	pci_intr_disestablish(rdev->pc, rdev->irqh);

#ifdef notyet
	pm_runtime_get_sync(dev->dev);

	radeon_kfd_device_fini(rdev);
#endif

	radeon_acpi_fini(rdev);
	
	radeon_modeset_fini(rdev);
	radeon_device_fini(rdev);

	config_detach(rdev_to_drm(rdev)->dev, flags);

	return 0;
}

void radeondrm_burner(void *, u_int, u_int);
int radeondrm_wsioctl(void *, u_long, caddr_t, int, struct proc *);
paddr_t radeondrm_wsmmap(void *, off_t, int);
int radeondrm_alloc_screen(void *, const struct wsscreen_descr *,
    void **, int *, int *, uint32_t *);
void radeondrm_free_screen(void *, void *);
int radeondrm_show_screen(void *, void *, int,
    void (*)(void *, int, int), void *);
void radeondrm_doswitch(void *);
void radeondrm_enter_ddb(void *, void *);
#ifdef __sparc64__
void radeondrm_setcolor(void *, u_int, u_int8_t, u_int8_t, u_int8_t);
#endif
void radeondrm_setpal(struct radeon_device *, struct rasops_info *);

struct wsscreen_descr radeondrm_stdscreen = {
	"std",
	0, 0,
	0,
	0, 0,
	WSSCREEN_UNDERLINE | WSSCREEN_HILIT |
	WSSCREEN_REVERSE | WSSCREEN_WSCOLORS
};

const struct wsscreen_descr *radeondrm_scrlist[] = {
	&radeondrm_stdscreen,
};

struct wsscreen_list radeondrm_screenlist = {
	nitems(radeondrm_scrlist), radeondrm_scrlist
};

struct wsdisplay_accessops radeondrm_accessops = {
	.ioctl = radeondrm_wsioctl,
	.mmap = radeondrm_wsmmap,
	.alloc_screen = radeondrm_alloc_screen,
	.free_screen = radeondrm_free_screen,
	.show_screen = radeondrm_show_screen,
	.enter_ddb = radeondrm_enter_ddb,
	.getchar = rasops_getchar,
	.load_font = rasops_load_font,
	.list_font = rasops_list_font,
	.scrollback = rasops_scrollback,
	.burn_screen = radeondrm_burner
};

int
radeondrm_wsioctl(void *v, u_long cmd, caddr_t data, int flag, struct proc *p)
{
	struct rasops_info *ri = v;
	struct wsdisplay_fbinfo *wdf;
	struct wsdisplay_param *dp = (struct wsdisplay_param *)data;

	switch (cmd) {
	case WSDISPLAYIO_GTYPE:
		*(u_int *)data = WSDISPLAY_TYPE_RADEONDRM;
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
		if (ws_get_param == NULL)
			return 0;
		return ws_get_param(dp);
	case WSDISPLAYIO_SETPARAM:
		if (ws_set_param == NULL)
			return 0;
		return ws_set_param(dp);
	case WSDISPLAYIO_SVIDEO:
	case WSDISPLAYIO_GVIDEO:
		return 0;
	default:
		return -1;
	}
}

paddr_t
radeondrm_wsmmap(void *v, off_t off, int prot)
{
	return (-1);
}

int
radeondrm_alloc_screen(void *v, const struct wsscreen_descr *type,
    void **cookiep, int *curxp, int *curyp, uint32_t *attrp)
{
	return rasops_alloc_screen(v, cookiep, curxp, curyp, attrp);
}

void
radeondrm_free_screen(void *v, void *cookie)
{
	return rasops_free_screen(v, cookie);
}

int
radeondrm_show_screen(void *v, void *cookie, int waitok,
    void (*cb)(void *, int, int), void *cbarg)
{
	struct rasops_info *ri = v;
	struct radeon_device *rdev = ri->ri_hw;

	if (cookie == ri->ri_active)
		return (0);

	rdev->switchcb = cb;
	rdev->switchcbarg = cbarg;
	rdev->switchcookie = cookie;
	if (cb) {
		task_add(systq, &rdev->switchtask);
		return (EAGAIN);
	}

	radeondrm_doswitch(v);

	return (0);
}

void
radeondrm_doswitch(void *v)
{
	struct rasops_info *ri = v;
	struct radeon_device *rdev = ri->ri_hw;

	rasops_show_screen(ri, rdev->switchcookie, 0, NULL, NULL);
#ifdef __sparc64__
	fbwscons_setcolormap(&rdev->sf, radeondrm_setcolor);
#else
	radeondrm_setpal(rdev, ri);
#endif
	drm_fb_helper_restore_fbdev_mode_unlocked(rdev_to_drm(rdev)->fb_helper);

	if (rdev->switchcb)
		(rdev->switchcb)(rdev->switchcbarg, 0, 0);
}

void
radeondrm_enter_ddb(void *v, void *cookie)
{
	struct rasops_info *ri = v;
	struct radeon_device *rdev = ri->ri_hw;
	struct drm_fb_helper *fb_helper = rdev_to_drm(rdev)->fb_helper;

	if (cookie == ri->ri_active)
		return;

	rasops_show_screen(ri, cookie, 0, NULL, NULL);
	drm_fb_helper_debug_enter(fb_helper->info);
}

#ifdef __sparc64__
void
radeondrm_setcolor(void *v, u_int index, u_int8_t r, u_int8_t g, u_int8_t b)
{
	struct sunfb *sf = v;
	struct radeon_device *rdev = sf->sf_ro.ri_hw;

	/* see legacy_crtc_load_lut() */
	if (rdev->family < CHIP_RS600) {
		WREG8(RADEON_PALETTE_INDEX, index);
		WREG32(RADEON_PALETTE_30_DATA,
		    (r << 22) | (g << 12) | (b << 2));
	} else {
		printf("%s: setcolor family %d not handled\n",
		    rdev->self.dv_xname, rdev->family);
	}
}
#endif

void
radeondrm_setpal(struct radeon_device *rdev, struct rasops_info *ri)
{
	struct drm_device *dev = rdev_to_drm(rdev);
	struct drm_crtc *crtc;
	uint16_t *r_base, *g_base, *b_base;
	int i, index, ret = 0;
	const u_char *p;

	if (ri->ri_depth != 8)
		return;

	for (i = 0; i < rdev->num_crtc; i++) {
		struct drm_modeset_acquire_ctx ctx;
		crtc = &rdev->mode_info.crtcs[i]->base;

		r_base = crtc->gamma_store;
		g_base = r_base + crtc->gamma_size;
		b_base = g_base + crtc->gamma_size;

		DRM_MODESET_LOCK_ALL_BEGIN(dev, ctx, 0, ret);

		p = rasops_cmap;
		for (index = 0; index < 256; index++) {
			r_base[index] = *p++ << 8;
			g_base[index] = *p++ << 8;
			b_base[index] = *p++ << 8;
		}

		crtc->funcs->gamma_set(crtc, NULL, NULL, NULL, 0, NULL);

		DRM_MODESET_LOCK_ALL_END(dev, ctx, ret);
	}
}

void
radeondrm_attach_kms(struct device *parent, struct device *self, void *aux)
{
	struct radeon_device	*rdev = (struct radeon_device *)self;
	struct drm_device	*dev;
	struct pci_attach_args	*pa = aux;
	const struct pci_device_id *id_entry;
	int			 is_agp;
	pcireg_t		 type;
	int			 i;
	uint8_t			 rmmio_bar;
	paddr_t			 fb_aper;
	pcireg_t		 addr, mask;
	int			 s;

#if defined(__sparc64__) || defined(__macppc__)
	extern int fbnode;
#endif

	id_entry = drm_find_description(PCI_VENDOR(pa->pa_id),
	    PCI_PRODUCT(pa->pa_id), pciidlist);
	rdev->flags = id_entry->driver_data;
	rdev->family = rdev->flags & RADEON_FAMILY_MASK;
	rdev->pc = pa->pa_pc;
	rdev->pa_tag = pa->pa_tag;
	rdev->iot = pa->pa_iot;
	rdev->memt = pa->pa_memt;
	rdev->dmat = pa->pa_dmat;

#if defined(__sparc64__) || defined(__macppc__)
	if (fbnode == PCITAG_NODE(rdev->pa_tag))
		rdev->console = rdev->primary = 1;
#else
	if (PCI_CLASS(pa->pa_class) == PCI_CLASS_DISPLAY &&
	    PCI_SUBCLASS(pa->pa_class) == PCI_SUBCLASS_DISPLAY_VGA &&
	    (pci_conf_read(pa->pa_pc, pa->pa_tag, PCI_COMMAND_STATUS_REG)
	    & (PCI_COMMAND_IO_ENABLE | PCI_COMMAND_MEM_ENABLE))
	    == (PCI_COMMAND_IO_ENABLE | PCI_COMMAND_MEM_ENABLE)) {
		rdev->primary = 1;
#if NVGA > 0
		rdev->console = vga_is_console(pa->pa_iot, -1);
		vga_console_attached = 1;
#endif
	}

#if NEFIFB > 0
	if (efifb_is_primary(pa)) {
		rdev->primary = 1;
		rdev->console = efifb_is_console(pa);
		efifb_detach();
	}
#endif
#endif

#define RADEON_PCI_MEM		0x10

	type = pci_mapreg_type(pa->pa_pc, pa->pa_tag, RADEON_PCI_MEM);
	if (PCI_MAPREG_TYPE(type) != PCI_MAPREG_TYPE_MEM ||
	    pci_mapreg_info(pa->pa_pc, pa->pa_tag, RADEON_PCI_MEM,
	    type, &rdev->fb_aper_offset, &rdev->fb_aper_size, NULL)) {
		printf(": can't get framebuffer info\n");
		return;
	}
	if (rdev->fb_aper_offset == 0) {
		bus_size_t start, end;
		bus_addr_t base;

		KASSERT(pa->pa_memex != NULL);

		start = max(PCI_MEM_START, pa->pa_memex->ex_start);
		end = min(PCI_MEM_END, pa->pa_memex->ex_end);
		if (extent_alloc_subregion(pa->pa_memex, start, end,
		    rdev->fb_aper_size, rdev->fb_aper_size, 0, 0, 0, &base)) {
			printf(": can't reserve framebuffer space\n");
			return;
		}
		pci_conf_write(pa->pa_pc, pa->pa_tag, RADEON_PCI_MEM, base);
		if (PCI_MAPREG_MEM_TYPE(type) == PCI_MAPREG_MEM_TYPE_64BIT)
			pci_conf_write(pa->pa_pc, pa->pa_tag,
			    RADEON_PCI_MEM + 4, (uint64_t)base >> 32);
		rdev->fb_aper_offset = base;
	}

	for (i = PCI_MAPREG_START; i < PCI_MAPREG_END; i += 4) {
		type = pci_mapreg_type(pa->pa_pc, pa->pa_tag, i);
		if (type == PCI_MAPREG_TYPE_IO) {
			pci_mapreg_map(pa, i, type, 0, NULL,
			    &rdev->rio_mem, NULL, &rdev->rio_mem_size, 0);
			break;
		}
		if (type == PCI_MAPREG_MEM_TYPE_64BIT)
			i += 4;
	}

	if (rdev->family >= CHIP_BONAIRE) {
		type = pci_mapreg_type(pa->pa_pc, pa->pa_tag, 0x18);
		if (PCI_MAPREG_TYPE(type) != PCI_MAPREG_TYPE_MEM ||
		    pci_mapreg_map(pa, 0x18, type, BUS_SPACE_MAP_LINEAR, NULL,
		    &rdev->doorbell.bsh, &rdev->doorbell.base,
		    &rdev->doorbell.size, 0)) {
			printf(": can't map doorbell space\n");
			return;
		}
		rdev->doorbell.ptr = bus_space_vaddr(rdev->memt,
		    rdev->doorbell.bsh);
	}

	if (rdev->family >= CHIP_BONAIRE)
		rmmio_bar = 0x24;
	else
		rmmio_bar = 0x18;

	type = pci_mapreg_type(pa->pa_pc, pa->pa_tag, rmmio_bar);
	if (PCI_MAPREG_TYPE(type) != PCI_MAPREG_TYPE_MEM ||
	    pci_mapreg_map(pa, rmmio_bar, type, BUS_SPACE_MAP_LINEAR, NULL,
	    &rdev->rmmio_bsh, &rdev->rmmio_base, &rdev->rmmio_size, 0)) {
		printf(": can't map rmmio space\n");
		return;
	}
	rdev->rmmio = bus_space_vaddr(rdev->memt, rdev->rmmio_bsh);

	/*
	 * Make sure we have a base address for the ROM such that we
	 * can map it later.
	 */
	s = splhigh();
	addr = pci_conf_read(pa->pa_pc, pa->pa_tag, PCI_ROM_REG);
	pci_conf_write(pa->pa_pc, pa->pa_tag, PCI_ROM_REG, ~PCI_ROM_ENABLE);
	mask = pci_conf_read(pa->pa_pc, pa->pa_tag, PCI_ROM_REG);
	pci_conf_write(pa->pa_pc, pa->pa_tag, PCI_ROM_REG, addr);
	splx(s);

	if (addr == 0 && PCI_ROM_SIZE(mask) != 0 && pa->pa_memex) {
		bus_size_t size, start, end;
		bus_addr_t base;

		size = PCI_ROM_SIZE(mask);
		start = max(PCI_MEM_START, pa->pa_memex->ex_start);
		end = min(PCI_MEM_END, pa->pa_memex->ex_end);
		if (extent_alloc_subregion(pa->pa_memex, start, end, size,
		    size, 0, 0, 0, &base) == 0)
			pci_conf_write(pa->pa_pc, pa->pa_tag, PCI_ROM_REG, base);
	}

	/* update BUS flag */
	if (pci_get_capability(pa->pa_pc, pa->pa_tag, PCI_CAP_AGP, NULL, NULL)) {
		rdev->flags |= RADEON_IS_AGP;
	} else if (pci_get_capability(pa->pa_pc, pa->pa_tag,
	    PCI_CAP_PCIEXPRESS, NULL, NULL)) {
		rdev->flags |= RADEON_IS_PCIE;
	} else {
		rdev->flags |= RADEON_IS_PCI;
	}

	if ((radeon_runtime_pm != 0) &&
	    radeon_has_atpx() &&
	    ((rdev->flags & RADEON_IS_IGP) == 0))
		rdev->flags |= RADEON_IS_PX;

	DRM_DEBUG("%s card detected\n",
		 ((rdev->flags & RADEON_IS_AGP) ? "AGP" :
		 (((rdev->flags & RADEON_IS_PCIE) ? "PCIE" : "PCI"))));

	is_agp = pci_get_capability(pa->pa_pc, pa->pa_tag, PCI_CAP_AGP,
	    NULL, NULL);

	printf("\n");

	dev = drm_attach_pci(&kms_driver, pa, is_agp, rdev->primary,
	    self, &rdev->ddev);
	if (dev == NULL) {
		printf("%s: drm attach failed\n", rdev->self.dv_xname);
		return;
	}
	rdev->pdev = dev->pdev;

	if (!radeon_msi_ok(rdev))
		pa->pa_flags &= ~PCI_FLAGS_MSI_ENABLED;

	rdev->msi_enabled = 0;
	if (pci_intr_map_msi(pa, &rdev->intrh) == 0)
		rdev->msi_enabled = 1;
	else if (pci_intr_map(pa, &rdev->intrh) != 0) {
		printf("%s: couldn't map interrupt\n", rdev->self.dv_xname);
		return;
	}
	printf("%s: %s\n", rdev->self.dv_xname,
	    pci_intr_string(pa->pa_pc, rdev->intrh));

	rdev->irqh = pci_intr_establish(pa->pa_pc, rdev->intrh, IPL_TTY,
	    radeon_driver_irq_handler_kms, rdev_to_drm(rdev), rdev->self.dv_xname);
	if (rdev->irqh == NULL) {
		printf("%s: couldn't establish interrupt\n",
		    rdev->self.dv_xname);
		return;
	}
	rdev->pdev->irq = -1;

#ifdef __sparc64__
{
	struct rasops_info *ri;
	int node, console;

	node = PCITAG_NODE(pa->pa_tag);
	console = (fbnode == node);

	fb_setsize(&rdev->sf, 8, 1152, 900, node, 0);

	/*
	 * The firmware sets up the framebuffer such that it starts at
	 * an offset from the start of video memory.
	 */
	rdev->fb_offset =
	    bus_space_read_4(rdev->memt, rdev->rmmio_bsh, RADEON_CRTC_OFFSET);
	if (bus_space_map(rdev->memt, rdev->fb_aper_offset + rdev->fb_offset,
	    rdev->sf.sf_fbsize, BUS_SPACE_MAP_LINEAR, &rdev->memh)) {
		printf("%s: can't map video memory\n", rdev->self.dv_xname);
		return;
	}

	ri = &rdev->sf.sf_ro;
	ri->ri_bits = bus_space_vaddr(rdev->memt, rdev->memh);
	ri->ri_hw = rdev;
	ri->ri_updatecursor = NULL;

	fbwscons_init(&rdev->sf, RI_VCONS | RI_WRONLY | RI_BSWAP, console);
	if (console)
		fbwscons_console_init(&rdev->sf, -1);
}
#endif

	fb_aper = bus_space_mmap(rdev->memt, rdev->fb_aper_offset, 0, 0, 0);
	if (fb_aper != -1)
		rasops_claim_framebuffer(fb_aper, rdev->fb_aper_size, self);

	rdev->shutdown = true;
	config_mountroot(self, radeondrm_attachhook);
}

int
radeondrm_forcedetach(struct radeon_device *rdev)
{
	struct pci_softc	*sc = (struct pci_softc *)rdev->self.dv_parent;
	pcitag_t		 tag = rdev->pa_tag;

#if NVGA > 0
	if (rdev->primary)
		vga_console_attached = 0;
#endif

	/* reprobe pci device for non efi systems */
#if NEFIFB > 0
	if (bios_efiinfo == NULL && !efifb_cb_found()) {
#endif
		config_detach(&rdev->self, 0);
		return pci_probe_device(sc, tag, NULL, NULL);
#if NEFIFB > 0
	} else if (rdev->primary) {
		efifb_reattach();
	}
#endif

	return 0;
}

void
radeondrm_attachhook(struct device *self)
{
	struct radeon_device *rdev = (struct radeon_device *)self;
	struct drm_device *dev = rdev_to_drm(rdev);
	int r, acpi_status;

	/* radeon_device_init should report only fatal error
	 * like memory allocation failure or iomapping failure,
	 * or memory manager initialization failure, it must
	 * properly initialize the GPU MC controller and permit
	 * VRAM allocation
	 */
	r = radeon_device_init(rdev, dev, dev->pdev, rdev->flags);
	if (r) {
		dev_err(&dev->pdev->dev, "Fatal error during GPU init\n");
		radeon_fatal_error = 1;
		radeondrm_forcedetach(rdev);
		return;
	}

	/* Again modeset_init should fail only on fatal error
	 * otherwise it should provide enough functionalities
	 * for shadowfb to run
	 */
	r = radeon_modeset_init(rdev);
	if (r)
		dev_err(&dev->pdev->dev, "Fatal error during modeset init\n");

	/* Call ACPI methods: require modeset init
	 * but failure is not fatal
	 */
	if (!r) {
		acpi_status = radeon_acpi_init(rdev);
		if (acpi_status)
			DRM_DEBUG("Error during ACPI methods call\n");
	}

#ifdef notyet
	radeon_kfd_device_probe(rdev);
	radeon_kfd_device_init(rdev);
#endif

	if (radeon_is_px(rdev_to_drm(rdev))) {
		pm_runtime_use_autosuspend(dev->dev);
		pm_runtime_set_autosuspend_delay(dev->dev, 5000);
		pm_runtime_set_active(dev->dev);
		pm_runtime_allow(dev->dev);
		pm_runtime_mark_last_busy(dev->dev);
		pm_runtime_put_autosuspend(dev->dev);
	}

{
	struct wsemuldisplaydev_attach_args aa;
	struct rasops_info *ri = &rdev->ro;

	task_set(&rdev->switchtask, radeondrm_doswitch, ri);

	/* from linux radeon_pci_probe() */

	pci_set_drvdata(dev->pdev, dev);

	drm_dev_register(dev, rdev->flags);

	radeon_fbdev_setup(rdev);

	if (ri->ri_bits == NULL)
		return;

#ifdef __sparc64__
	fbwscons_setcolormap(&rdev->sf, radeondrm_setcolor);
	ri = &rdev->sf.sf_ro;
#else
	radeondrm_setpal(rdev, ri);
	ri->ri_flg = RI_CENTER | RI_VCONS | RI_WRONLY;
	rasops_init(ri, 160, 160);

	ri->ri_hw = rdev;
#endif

	radeondrm_stdscreen.capabilities = ri->ri_caps;
	radeondrm_stdscreen.nrows = ri->ri_rows;
	radeondrm_stdscreen.ncols = ri->ri_cols;
	radeondrm_stdscreen.textops = &ri->ri_ops;
	radeondrm_stdscreen.fontwidth = ri->ri_font->fontwidth;
	radeondrm_stdscreen.fontheight = ri->ri_font->fontheight;

	aa.console = rdev->console;
	aa.primary = rdev->primary;
	aa.scrdata = &radeondrm_screenlist;
	aa.accessops = &radeondrm_accessops;
	aa.accesscookie = ri;
	aa.defaultscreens = 0;

	if (rdev->console) {
		uint32_t defattr;

		ri->ri_ops.pack_attr(ri->ri_active, 0, 0, 0, &defattr);
		wsdisplay_cnattach(&radeondrm_stdscreen, ri->ri_active,
		    ri->ri_ccol, ri->ri_crow, defattr);
	}

	/*
	 * Now that we've taken over the console, disable decoding of
	 * VGA legacy addresses, and opt out of arbitration.
	 */
	radeon_vga_set_state(rdev, false);
	pci_disable_legacy_vga(&rdev->self);

	printf("%s: %dx%d, %dbpp\n", rdev->self.dv_xname,
	    ri->ri_width, ri->ri_height, ri->ri_depth);

	config_found_sm(&rdev->self, &aa, wsemuldisplaydevprint,
	    wsemuldisplaydevsubmatch);
}
}

int
radeondrm_activate_kms(struct device *self, int act)
{
	struct radeon_device *rdev = (struct radeon_device *)self;
	struct drm_device *ddev = rdev_to_drm(rdev);
	int rv = 0;

	if (ddev == NULL || radeon_fatal_error)
		return (0);

	switch (act) {
	case DVACT_QUIESCE:
		rv = config_activate_children(self, act);
		radeon_suspend_kms(ddev, true, true, false);
		break;
	case DVACT_SUSPEND:
		break;
	case DVACT_RESUME:
		break;
	case DVACT_WAKEUP:
		radeon_resume_kms(ddev, true, true);
		rv = config_activate_children(self, act);
		break;
	}

	return (rv);
}
