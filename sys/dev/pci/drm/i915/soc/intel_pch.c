// SPDX-License-Identifier: MIT
/*
 * Copyright 2019 Intel Corporation.
 */

#include "i915_drv.h"
#include "i915_utils.h"
#include "intel_pch.h"

/* Map PCH device id to PCH type, or PCH_NONE if unknown. */
static enum intel_pch
intel_pch_type(const struct drm_i915_private *dev_priv, unsigned short id)
{
	switch (id) {
	case INTEL_PCH_IBX_DEVICE_ID_TYPE:
		drm_dbg_kms(&dev_priv->drm, "Found Ibex Peak PCH\n");
		drm_WARN_ON(&dev_priv->drm, GRAPHICS_VER(dev_priv) != 5);
		return PCH_IBX;
	case INTEL_PCH_CPT_DEVICE_ID_TYPE:
		drm_dbg_kms(&dev_priv->drm, "Found CougarPoint PCH\n");
		drm_WARN_ON(&dev_priv->drm,
			    GRAPHICS_VER(dev_priv) != 6 && !IS_IVYBRIDGE(dev_priv));
		return PCH_CPT;
	case INTEL_PCH_PPT_DEVICE_ID_TYPE:
		drm_dbg_kms(&dev_priv->drm, "Found PantherPoint PCH\n");
		drm_WARN_ON(&dev_priv->drm,
			    GRAPHICS_VER(dev_priv) != 6 && !IS_IVYBRIDGE(dev_priv));
		/* PPT is CPT compatible */
		return PCH_CPT;
	case INTEL_PCH_LPT_DEVICE_ID_TYPE:
		drm_dbg_kms(&dev_priv->drm, "Found LynxPoint PCH\n");
		drm_WARN_ON(&dev_priv->drm,
			    !IS_HASWELL(dev_priv) && !IS_BROADWELL(dev_priv));
		drm_WARN_ON(&dev_priv->drm,
			    IS_HASWELL_ULT(dev_priv) || IS_BROADWELL_ULT(dev_priv));
		return PCH_LPT;
	case INTEL_PCH_LPT_LP_DEVICE_ID_TYPE:
		drm_dbg_kms(&dev_priv->drm, "Found LynxPoint LP PCH\n");
		drm_WARN_ON(&dev_priv->drm,
			    !IS_HASWELL(dev_priv) && !IS_BROADWELL(dev_priv));
		drm_WARN_ON(&dev_priv->drm,
			    !IS_HASWELL_ULT(dev_priv) && !IS_BROADWELL_ULT(dev_priv));
		return PCH_LPT;
	case INTEL_PCH_WPT_DEVICE_ID_TYPE:
		drm_dbg_kms(&dev_priv->drm, "Found WildcatPoint PCH\n");
		drm_WARN_ON(&dev_priv->drm,
			    !IS_HASWELL(dev_priv) && !IS_BROADWELL(dev_priv));
		drm_WARN_ON(&dev_priv->drm,
			    IS_HASWELL_ULT(dev_priv) || IS_BROADWELL_ULT(dev_priv));
		/* WPT is LPT compatible */
		return PCH_LPT;
	case INTEL_PCH_WPT_LP_DEVICE_ID_TYPE:
		drm_dbg_kms(&dev_priv->drm, "Found WildcatPoint LP PCH\n");
		drm_WARN_ON(&dev_priv->drm,
			    !IS_HASWELL(dev_priv) && !IS_BROADWELL(dev_priv));
		drm_WARN_ON(&dev_priv->drm,
			    !IS_HASWELL_ULT(dev_priv) && !IS_BROADWELL_ULT(dev_priv));
		/* WPT is LPT compatible */
		return PCH_LPT;
	case INTEL_PCH_SPT_DEVICE_ID_TYPE:
		drm_dbg_kms(&dev_priv->drm, "Found SunrisePoint PCH\n");
		drm_WARN_ON(&dev_priv->drm,
			    !IS_SKYLAKE(dev_priv) && !IS_KABYLAKE(dev_priv));
		return PCH_SPT;
	case INTEL_PCH_SPT_LP_DEVICE_ID_TYPE:
		drm_dbg_kms(&dev_priv->drm, "Found SunrisePoint LP PCH\n");
		drm_WARN_ON(&dev_priv->drm,
			    !IS_SKYLAKE(dev_priv) &&
			    !IS_KABYLAKE(dev_priv) &&
			    !IS_COFFEELAKE(dev_priv) &&
			    !IS_COMETLAKE(dev_priv));
		return PCH_SPT;
	case INTEL_PCH_KBP_DEVICE_ID_TYPE:
		drm_dbg_kms(&dev_priv->drm, "Found Kaby Lake PCH (KBP)\n");
		drm_WARN_ON(&dev_priv->drm,
			    !IS_SKYLAKE(dev_priv) &&
			    !IS_KABYLAKE(dev_priv) &&
			    !IS_COFFEELAKE(dev_priv) &&
			    !IS_COMETLAKE(dev_priv));
		/* KBP is SPT compatible */
		return PCH_SPT;
	case INTEL_PCH_CNP_DEVICE_ID_TYPE:
		drm_dbg_kms(&dev_priv->drm, "Found Cannon Lake PCH (CNP)\n");
		drm_WARN_ON(&dev_priv->drm,
			    !IS_COFFEELAKE(dev_priv) &&
			    !IS_COMETLAKE(dev_priv));
		return PCH_CNP;
	case INTEL_PCH_CNP_LP_DEVICE_ID_TYPE:
		drm_dbg_kms(&dev_priv->drm,
			    "Found Cannon Lake LP PCH (CNP-LP)\n");
		drm_WARN_ON(&dev_priv->drm,
			    !IS_COFFEELAKE(dev_priv) &&
			    !IS_COMETLAKE(dev_priv));
		return PCH_CNP;
	case INTEL_PCH_CMP_DEVICE_ID_TYPE:
	case INTEL_PCH_CMP2_DEVICE_ID_TYPE:
		drm_dbg_kms(&dev_priv->drm, "Found Comet Lake PCH (CMP)\n");
		drm_WARN_ON(&dev_priv->drm,
			    !IS_COFFEELAKE(dev_priv) &&
			    !IS_COMETLAKE(dev_priv) &&
			    !IS_ROCKETLAKE(dev_priv));
		/* CMP is CNP compatible */
		return PCH_CNP;
	case INTEL_PCH_CMP_V_DEVICE_ID_TYPE:
		drm_dbg_kms(&dev_priv->drm, "Found Comet Lake V PCH (CMP-V)\n");
		drm_WARN_ON(&dev_priv->drm,
			    !IS_COFFEELAKE(dev_priv) &&
			    !IS_COMETLAKE(dev_priv));
		/* CMP-V is based on KBP, which is SPT compatible */
		return PCH_SPT;
	case INTEL_PCH_ICP_DEVICE_ID_TYPE:
	case INTEL_PCH_ICP2_DEVICE_ID_TYPE:
		drm_dbg_kms(&dev_priv->drm, "Found Ice Lake PCH\n");
		drm_WARN_ON(&dev_priv->drm, !IS_ICELAKE(dev_priv));
		return PCH_ICP;
	case INTEL_PCH_MCC_DEVICE_ID_TYPE:
		drm_dbg_kms(&dev_priv->drm, "Found Mule Creek Canyon PCH\n");
		drm_WARN_ON(&dev_priv->drm, !(IS_JASPERLAKE(dev_priv) ||
					      IS_ELKHARTLAKE(dev_priv)));
		/* MCC is TGP compatible */
		return PCH_TGP;
	case INTEL_PCH_TGP_DEVICE_ID_TYPE:
	case INTEL_PCH_TGP2_DEVICE_ID_TYPE:
		drm_dbg_kms(&dev_priv->drm, "Found Tiger Lake LP PCH\n");
		drm_WARN_ON(&dev_priv->drm, !IS_TIGERLAKE(dev_priv) &&
			    !IS_ROCKETLAKE(dev_priv) &&
			    !IS_GEN9_BC(dev_priv));
		return PCH_TGP;
	case INTEL_PCH_JSP_DEVICE_ID_TYPE:
		drm_dbg_kms(&dev_priv->drm, "Found Jasper Lake PCH\n");
		drm_WARN_ON(&dev_priv->drm, !(IS_JASPERLAKE(dev_priv) ||
					      IS_ELKHARTLAKE(dev_priv)));
		/* JSP is ICP compatible */
		return PCH_ICP;
	case INTEL_PCH_ADP_DEVICE_ID_TYPE:
	case INTEL_PCH_ADP2_DEVICE_ID_TYPE:
	case INTEL_PCH_ADP3_DEVICE_ID_TYPE:
	case INTEL_PCH_ADP4_DEVICE_ID_TYPE:
		drm_dbg_kms(&dev_priv->drm, "Found Alder Lake PCH\n");
		drm_WARN_ON(&dev_priv->drm, !IS_ALDERLAKE_S(dev_priv) &&
			    !IS_ALDERLAKE_P(dev_priv));
		return PCH_ADP;
	default:
		return PCH_NONE;
	}
}

static bool intel_is_virt_pch(unsigned short id,
			      unsigned short svendor, unsigned short sdevice)
{
	return (id == INTEL_PCH_P2X_DEVICE_ID_TYPE ||
		id == INTEL_PCH_P3X_DEVICE_ID_TYPE ||
		(id == INTEL_PCH_QEMU_DEVICE_ID_TYPE &&
		 svendor == PCI_SUBVENDOR_ID_REDHAT_QUMRANET &&
		 sdevice == PCI_SUBDEVICE_ID_QEMU));
}

static void
intel_virt_detect_pch(const struct drm_i915_private *dev_priv,
		      unsigned short *pch_id, enum intel_pch *pch_type)
{
	unsigned short id = 0;

	/*
	 * In a virtualized passthrough environment we can be in a
	 * setup where the ISA bridge is not able to be passed through.
	 * In this case, a south bridge can be emulated and we have to
	 * make an educated guess as to which PCH is really there.
	 */

	if (IS_ALDERLAKE_S(dev_priv) || IS_ALDERLAKE_P(dev_priv))
		id = INTEL_PCH_ADP_DEVICE_ID_TYPE;
	else if (IS_TIGERLAKE(dev_priv) || IS_ROCKETLAKE(dev_priv))
		id = INTEL_PCH_TGP_DEVICE_ID_TYPE;
	else if (IS_JASPERLAKE(dev_priv) || IS_ELKHARTLAKE(dev_priv))
		id = INTEL_PCH_MCC_DEVICE_ID_TYPE;
	else if (IS_ICELAKE(dev_priv))
		id = INTEL_PCH_ICP_DEVICE_ID_TYPE;
	else if (IS_COFFEELAKE(dev_priv) ||
		 IS_COMETLAKE(dev_priv))
		id = INTEL_PCH_CNP_DEVICE_ID_TYPE;
	else if (IS_KABYLAKE(dev_priv) || IS_SKYLAKE(dev_priv))
		id = INTEL_PCH_SPT_DEVICE_ID_TYPE;
	else if (IS_HASWELL_ULT(dev_priv) || IS_BROADWELL_ULT(dev_priv))
		id = INTEL_PCH_LPT_LP_DEVICE_ID_TYPE;
	else if (IS_HASWELL(dev_priv) || IS_BROADWELL(dev_priv))
		id = INTEL_PCH_LPT_DEVICE_ID_TYPE;
	else if (GRAPHICS_VER(dev_priv) == 6 || IS_IVYBRIDGE(dev_priv))
		id = INTEL_PCH_CPT_DEVICE_ID_TYPE;
	else if (GRAPHICS_VER(dev_priv) == 5)
		id = INTEL_PCH_IBX_DEVICE_ID_TYPE;

	if (id)
		drm_dbg_kms(&dev_priv->drm, "Assuming PCH ID %04x\n", id);
	else
		drm_dbg_kms(&dev_priv->drm, "Assuming no PCH\n");

	*pch_type = intel_pch_type(dev_priv, id);

	/* Sanity check virtual PCH id */
	if (drm_WARN_ON(&dev_priv->drm,
			id && *pch_type == PCH_NONE))
		id = 0;

	*pch_id = id;
}

static int
intel_pch_match(struct pci_attach_args *pa)
{
	if (PCI_VENDOR(pa->pa_id) == PCI_VENDOR_INTEL &&
	    PCI_CLASS(pa->pa_class) == PCI_CLASS_BRIDGE &&
	    PCI_SUBCLASS(pa->pa_class) == PCI_SUBCLASS_BRIDGE_ISA)
		return 1;
	return 0;
}

void intel_detect_pch(struct drm_i915_private *dev_priv)
{
	struct pci_attach_args pa;
	unsigned short id;
	enum intel_pch pch_type;
	pcireg_t subsys;

	/*
	 * South display engine on the same PCI device: just assign the fake
	 * PCH.
	 */
	if (DISPLAY_VER(dev_priv) >= 20) {
		dev_priv->pch_type = PCH_LNL;
		return;
	} else if (IS_BATTLEMAGE(dev_priv) || IS_METEORLAKE(dev_priv)) {
		/*
		 * Both north display and south display are on the SoC die.
		 * The real PCH (if it even exists) is uninvolved in display.
		 */
		dev_priv->pch_type = PCH_MTL;
		return;
	} else if (IS_DG2(dev_priv)) {
		dev_priv->pch_type = PCH_DG2;
		return;
	} else if (IS_DG1(dev_priv)) {
		dev_priv->pch_type = PCH_DG1;
		return;
	}

	/*
	 * The reason to probe ISA bridge instead of Dev31:Fun0 is to
	 * make graphics device passthrough work easy for VMM, that only
	 * need to expose ISA bridge to let driver know the real hardware
	 * underneath. This is a requirement from virtualization team.
	 *
	 * In some virtualized environments (e.g. XEN), there is irrelevant
	 * ISA bridge in the system. To work reliably, we should scan trhough
	 * all the ISA bridge devices and check for the first match, instead
	 * of only checking the first one.
	 */
	if (pci_find_device(&pa, intel_pch_match)) {
		id = PCI_PRODUCT(pa.pa_id) & INTEL_PCH_DEVICE_ID_MASK;
		subsys = pci_conf_read(pa.pa_pc, pa.pa_tag, PCI_SUBSYS_ID_REG);

		pch_type = intel_pch_type(dev_priv, id);
		if (pch_type != PCH_NONE) {
			dev_priv->pch_type = pch_type;
			dev_priv->pch_id = id;
		} else if (intel_is_virt_pch(id, PCI_VENDOR(subsys),
					     PCI_PRODUCT(subsys))) {
			intel_virt_detect_pch(dev_priv, &id, &pch_type);
			dev_priv->pch_type = pch_type;
			dev_priv->pch_id = id;
		}
	}

	/*
	 * Use PCH_NOP (PCH but no South Display) for PCH platforms without
	 * display.
	 */
	if (pci_find_device(&pa, intel_pch_match) && !HAS_DISPLAY(dev_priv)) {
		drm_dbg_kms(&dev_priv->drm,
			    "Display disabled, reverting to NOP PCH\n");
		dev_priv->pch_type = PCH_NOP;
		dev_priv->pch_id = 0;
	} else if (!pci_find_device(&pa, intel_pch_match)) {
		if (i915_run_as_guest() && HAS_DISPLAY(dev_priv)) {
			intel_virt_detect_pch(dev_priv, &id, &pch_type);
			dev_priv->pch_type = pch_type;
			dev_priv->pch_id = id;
		} else {
			drm_dbg_kms(&dev_priv->drm, "No PCH found.\n");
		}
	}

	pci_dev_put(pch);
}
