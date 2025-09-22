// SPDX-License-Identifier: MIT
/*
 * Copyright © 2019 Intel Corporation
 */

#include <linux/delay.h>
#include <linux/vgaarb.h>

#include <video/vga.h>
#include "soc/intel_gmch.h"

#include "i915_drv.h"
#include "i915_reg.h"
#include "intel_de.h"
#include "intel_vga.h"

static i915_reg_t intel_vga_cntrl_reg(struct drm_i915_private *i915)
{
	if (IS_VALLEYVIEW(i915) || IS_CHERRYVIEW(i915))
		return VLV_VGACNTRL;
	else if (DISPLAY_VER(i915) >= 5)
		return CPU_VGACNTRL;
	else
		return VGACNTRL;
}

/* Disable the VGA plane that we never use */
void intel_vga_disable(struct drm_i915_private *dev_priv)
{
	struct pci_dev *pdev = dev_priv->drm.pdev;
	i915_reg_t vga_reg = intel_vga_cntrl_reg(dev_priv);
	u8 sr1;

	if (intel_de_read(dev_priv, vga_reg) & VGA_DISP_DISABLE)
		return;

	/* WaEnableVGAAccessThroughIOPort:ctg,elk,ilk,snb,ivb,vlv,hsw */
	vga_get_uninterruptible(pdev, VGA_RSRC_LEGACY_IO);
#ifdef __linux__
	outb(0x01, VGA_SEQ_I);
	sr1 = inb(VGA_SEQ_D);
	outb(sr1 | VGA_SR01_SCREEN_OFF, VGA_SEQ_D);
#else
	outb(VGA_SEQ_I, 0x01);
	sr1 = inb(VGA_SEQ_D);
	outb(VGA_SEQ_D, sr1 | VGA_SR01_SCREEN_OFF);
#endif
	vga_put(pdev, VGA_RSRC_LEGACY_IO);
	udelay(300);

	intel_de_write(dev_priv, vga_reg, VGA_DISP_DISABLE);
	intel_de_posting_read(dev_priv, vga_reg);
}

void intel_vga_redisable_power_on(struct drm_i915_private *dev_priv)
{
	i915_reg_t vga_reg = intel_vga_cntrl_reg(dev_priv);

	if (!(intel_de_read(dev_priv, vga_reg) & VGA_DISP_DISABLE)) {
		drm_dbg_kms(&dev_priv->drm,
			    "Something enabled VGA plane, disabling it\n");
		intel_vga_disable(dev_priv);
	}
}

void intel_vga_redisable(struct drm_i915_private *i915)
{
	intel_wakeref_t wakeref;

	/*
	 * This function can be called both from intel_modeset_setup_hw_state or
	 * at a very early point in our resume sequence, where the power well
	 * structures are not yet restored. Since this function is at a very
	 * paranoid "someone might have enabled VGA while we were not looking"
	 * level, just check if the power well is enabled instead of trying to
	 * follow the "don't touch the power well if we don't need it" policy
	 * the rest of the driver uses.
	 */
	wakeref = intel_display_power_get_if_enabled(i915, POWER_DOMAIN_VGA);
	if (!wakeref)
		return;

	intel_vga_redisable_power_on(i915);

	intel_display_power_put(i915, POWER_DOMAIN_VGA, wakeref);
}

void intel_vga_reset_io_mem(struct drm_i915_private *i915)
{
	struct pci_dev *pdev = i915->drm.pdev;

	/*
	 * After we re-enable the power well, if we touch VGA register 0x3d5
	 * we'll get unclaimed register interrupts. This stops after we write
	 * anything to the VGA MSR register. The vgacon module uses this
	 * register all the time, so if we unbind our driver and, as a
	 * consequence, bind vgacon, we'll get stuck in an infinite loop at
	 * console_unlock(). So make here we touch the VGA MSR register, making
	 * sure vgacon can keep working normally without triggering interrupts
	 * and error messages.
	 */
	vga_get_uninterruptible(pdev, VGA_RSRC_LEGACY_IO);
#ifdef __linux__
	outb(inb(VGA_MIS_R), VGA_MIS_W);
#else
	outb(VGA_MIS_W, inb(VGA_MIS_R));
#endif
	vga_put(pdev, VGA_RSRC_LEGACY_IO);
}

int intel_vga_register(struct drm_i915_private *i915)
{

	struct pci_dev *pdev = i915->drm.pdev;
	int ret;

	/*
	 * If we have > 1 VGA cards, then we need to arbitrate access to the
	 * common VGA resources.
	 *
	 * If we are a secondary display controller (!PCI_DISPLAY_CLASS_VGA),
	 * then we do not take part in VGA arbitration and the
	 * vga_client_register() fails with -ENODEV.
	 */
	ret = vga_client_register(pdev, intel_gmch_vga_set_decode);
	if (ret && ret != -ENODEV)
		return ret;

	return 0;
}

void intel_vga_unregister(struct drm_i915_private *i915)
{
	struct pci_dev *pdev = i915->drm.pdev;

	vga_client_unregister(pdev);
}
