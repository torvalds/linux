/* Public domain. */

#include <linux/types.h>
#include <linux/ioport.h>
#include <linux/pci.h>
#include <drm/intel/i915_drm.h>
#include "i915_drv.h"

struct resource intel_graphics_stolen_res = DEFINE_RES_MEM(0, 0);

bus_addr_t
gen3_stolen_base(struct drm_i915_private *dev_priv)
{
	uint32_t bsm = pci_conf_read(dev_priv->pc, dev_priv->tag,
	    INTEL_BSM);
	return bsm & INTEL_BSM_MASK;
}

bus_addr_t
gen11_stolen_base(struct drm_i915_private *dev_priv)
{
	uint64_t bsm = pci_conf_read(dev_priv->pc, dev_priv->tag,
	    INTEL_GEN11_BSM_DW0);
	bsm &= INTEL_BSM_MASK;
	bsm |= (uint64_t)pci_conf_read(dev_priv->pc, dev_priv->tag,
	    INTEL_GEN11_BSM_DW1) << 32;
	return bsm;
}

bus_size_t
i830_stolen_size(struct drm_i915_private *dev_priv)
{
	uint16_t gmch_ctl, gms;

	pci_read_config_word(dev_priv->gmch.pdev, I830_GMCH_CTRL,
	    &gmch_ctl);
	gms = gmch_ctl & I830_GMCH_GMS_MASK;

	switch (gms) {
	case I830_GMCH_GMS_STOLEN_512:
		return 512 * 1024;
	case I830_GMCH_GMS_STOLEN_1024:
		return 1 * 1024 * 1024;
	case I830_GMCH_GMS_STOLEN_8192:
		return 8 * 1024 * 1024;
	}

	return 0;
}

bus_size_t
gen3_stolen_size(struct drm_i915_private *dev_priv)
{
	uint16_t gmch_ctl, gms;

	pci_read_config_word(dev_priv->gmch.pdev, I830_GMCH_CTRL,
	    &gmch_ctl);
	gms = gmch_ctl & I855_GMCH_GMS_MASK;

	switch (gms) {
	case I855_GMCH_GMS_STOLEN_1M:
		return 1 * 1024 * 1024;
	case I855_GMCH_GMS_STOLEN_4M:
		return 4 * 1024 * 1024;
	case I855_GMCH_GMS_STOLEN_8M:
		return 8 * 1024 * 1024;
	case I855_GMCH_GMS_STOLEN_16M:
		return 16 * 1024 * 1024;
	case I855_GMCH_GMS_STOLEN_32M:
		return 32 * 1024 * 1024;
	case I915_GMCH_GMS_STOLEN_48M:
		return 48 * 1024 * 1024;
	case I915_GMCH_GMS_STOLEN_64M:
		return 64 * 1024 * 1024;
	case G33_GMCH_GMS_STOLEN_128M:
		return 128 * 1024 * 1024;
	case G33_GMCH_GMS_STOLEN_256M:
		return 256 * 1024 * 1024;
	case INTEL_GMCH_GMS_STOLEN_96M:
		return 96 * 1024 * 1024;
	case INTEL_GMCH_GMS_STOLEN_160M:
		return 160 * 1024 * 1024;
	case INTEL_GMCH_GMS_STOLEN_224M:
		return 224 * 1024 * 1024;
	case INTEL_GMCH_GMS_STOLEN_352M:
		return 352 * 1024 * 1024;
	}

	return 0;
}

bus_size_t
gen6_stolen_size(struct drm_i915_private *dev_priv)
{
	struct pci_dev *pdev = dev_priv->drm.pdev;
	uint16_t gmch_ctl, gms;

	pci_read_config_word(pdev, SNB_GMCH_CTRL, &gmch_ctl);
	gms = (gmch_ctl >> SNB_GMCH_GMS_SHIFT) & SNB_GMCH_GMS_MASK;

	return gms * (32 * 1024 * 1024);
}

bus_size_t
chv_stolen_size(struct drm_i915_private *dev_priv)
{
	struct pci_dev *pdev = dev_priv->drm.pdev;
	uint16_t gmch_ctl, gms;

	pci_read_config_word(pdev, SNB_GMCH_CTRL, &gmch_ctl);
	gms = (gmch_ctl >> SNB_GMCH_GMS_SHIFT) & SNB_GMCH_GMS_MASK;

	if (gms < 0x11)
		return gms * (32 * 1024 * 1024);
	else if (gms < 0x17)
		return (gms - 0x11) * (4 * 1024 * 1024) + (8 * 1024 * 1024);
	else
		return (gms - 0x17) + (4 * 1024 * 1024) + (36 * 1024 * 1024);
}

bus_size_t
gen8_stolen_size(struct drm_i915_private *dev_priv)
{
	struct pci_dev *pdev = dev_priv->drm.pdev;
	uint16_t gmch_ctl, gms;

	pci_read_config_word(pdev, SNB_GMCH_CTRL, &gmch_ctl);
	gms = (gmch_ctl >> BDW_GMCH_GMS_SHIFT) & BDW_GMCH_GMS_MASK;

	return gms * (32 * 1024 * 1024);
}

bus_size_t
gen9_stolen_size(struct drm_i915_private *dev_priv)
{
	struct pci_dev *pdev = dev_priv->drm.pdev;
	uint16_t gmch_ctl, gms;

	pci_read_config_word(pdev, SNB_GMCH_CTRL, &gmch_ctl);
	gms = (gmch_ctl >> BDW_GMCH_GMS_SHIFT) & BDW_GMCH_GMS_MASK;
	if (gms < 0xf0)
		return gms * (32 * 1024 * 1024);
	else
		return (gms - 0xf0) * (4 * 1024 * 1024) + (4 * 1024 * 1024);
}

void
intel_init_stolen_res(struct drm_i915_private *dev_priv)
{
	bus_addr_t stolen_base = 0;
	bus_size_t stolen_size = 0;

#ifdef notyet
	if (IS_I830(dev_priv))
		stolen_base  = i830_stolen_base(dev_priv);
	else if (IS_I845G(dev_priv))
		stolen_base  = i845_stolen_base(dev_priv);
	else if (IS_I85X(dev_priv))
		stolen_base  = i85x_stolen_base(dev_priv);
	else if (IS_I865G(dev_priv))
		stolen_base  = i865_stolen_base(dev_priv);
#endif

	if (GRAPHICS_VER(dev_priv) >= 3 && GRAPHICS_VER(dev_priv) < 11)
		stolen_base  = gen3_stolen_base(dev_priv);
	else if (GRAPHICS_VER(dev_priv) == 11 || GRAPHICS_VER(dev_priv) == 12)
		stolen_base = gen11_stolen_base(dev_priv);

	if (IS_I830(dev_priv) || IS_I845G(dev_priv))
		stolen_size = i830_stolen_size(dev_priv);
	else if (IS_I85X(dev_priv) || IS_I865G(dev_priv) ||
	    (GRAPHICS_VER(dev_priv) >= 3 && GRAPHICS_VER(dev_priv) <= 5))
		stolen_size = gen3_stolen_size(dev_priv);
	else if (IS_CHERRYVIEW(dev_priv))
		stolen_size = chv_stolen_size(dev_priv);
	else if (GRAPHICS_VER(dev_priv) >= 6 && GRAPHICS_VER(dev_priv) < 8)
		stolen_size = gen6_stolen_size(dev_priv);
	else if (GRAPHICS_VER(dev_priv) == 8)
		stolen_size = gen8_stolen_size(dev_priv);
	else if (GRAPHICS_VER(dev_priv) >= 9 && GRAPHICS_VER(dev_priv) <= 12)
		stolen_size = gen9_stolen_size(dev_priv);

	if (stolen_base == 0 || stolen_size == 0)
		return;

	intel_graphics_stolen_res.start = stolen_base;
	intel_graphics_stolen_res.end = stolen_base + stolen_size - 1;
}
