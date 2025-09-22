// SPDX-License-Identifier: MIT
/*
 * Copyright © 2018 Intel Corporation
 */

#include "i915_reg.h"
#include "intel_combo_phy.h"
#include "intel_combo_phy_regs.h"
#include "intel_de.h"
#include "intel_display_types.h"

#define for_each_combo_phy(__dev_priv, __phy) \
	for ((__phy) = PHY_A; (__phy) < I915_MAX_PHYS; (__phy)++)	\
		for_each_if(intel_phy_is_combo(__dev_priv, __phy))

#define for_each_combo_phy_reverse(__dev_priv, __phy) \
	for ((__phy) = I915_MAX_PHYS; (__phy)-- > PHY_A;) \
		for_each_if(intel_phy_is_combo(__dev_priv, __phy))

enum {
	PROCMON_0_85V_DOT_0,
	PROCMON_0_95V_DOT_0,
	PROCMON_0_95V_DOT_1,
	PROCMON_1_05V_DOT_0,
	PROCMON_1_05V_DOT_1,
};

static const struct icl_procmon {
	const char *name;
	u32 dw1, dw9, dw10;
} icl_procmon_values[] = {
	[PROCMON_0_85V_DOT_0] = {
		.name = "0.85V dot0 (low-voltage)",
		.dw1 = 0x00000000, .dw9 = 0x62AB67BB, .dw10 = 0x51914F96,
	},
	[PROCMON_0_95V_DOT_0] = {
		.name = "0.95V dot0",
		.dw1 = 0x00000000, .dw9 = 0x86E172C7, .dw10 = 0x77CA5EAB,
	},
	[PROCMON_0_95V_DOT_1] = {
		.name = "0.95V dot1",
		.dw1 = 0x00000000, .dw9 = 0x93F87FE1, .dw10 = 0x8AE871C5,
	},
	[PROCMON_1_05V_DOT_0] = {
		.name = "1.05V dot0",
		.dw1 = 0x00000000, .dw9 = 0x98FA82DD, .dw10 = 0x89E46DC1,
	},
	[PROCMON_1_05V_DOT_1] = {
		.name = "1.05V dot1",
		.dw1 = 0x00440000, .dw9 = 0x9A00AB25, .dw10 = 0x8AE38FF1,
	},
};

static const struct icl_procmon *
icl_get_procmon_ref_values(struct drm_i915_private *dev_priv, enum phy phy)
{
	u32 val;

	val = intel_de_read(dev_priv, ICL_PORT_COMP_DW3(phy));
	switch (val & (PROCESS_INFO_MASK | VOLTAGE_INFO_MASK)) {
	default:
		MISSING_CASE(val);
		fallthrough;
	case VOLTAGE_INFO_0_85V | PROCESS_INFO_DOT_0:
		return &icl_procmon_values[PROCMON_0_85V_DOT_0];
	case VOLTAGE_INFO_0_95V | PROCESS_INFO_DOT_0:
		return &icl_procmon_values[PROCMON_0_95V_DOT_0];
	case VOLTAGE_INFO_0_95V | PROCESS_INFO_DOT_1:
		return &icl_procmon_values[PROCMON_0_95V_DOT_1];
	case VOLTAGE_INFO_1_05V | PROCESS_INFO_DOT_0:
		return &icl_procmon_values[PROCMON_1_05V_DOT_0];
	case VOLTAGE_INFO_1_05V | PROCESS_INFO_DOT_1:
		return &icl_procmon_values[PROCMON_1_05V_DOT_1];
	}
}

static void icl_set_procmon_ref_values(struct drm_i915_private *dev_priv,
				       enum phy phy)
{
	const struct icl_procmon *procmon;

	procmon = icl_get_procmon_ref_values(dev_priv, phy);

	intel_de_rmw(dev_priv, ICL_PORT_COMP_DW1(phy),
		     (0xff << 16) | 0xff, procmon->dw1);

	intel_de_write(dev_priv, ICL_PORT_COMP_DW9(phy), procmon->dw9);
	intel_de_write(dev_priv, ICL_PORT_COMP_DW10(phy), procmon->dw10);
}

static bool check_phy_reg(struct drm_i915_private *dev_priv,
			  enum phy phy, i915_reg_t reg, u32 mask,
			  u32 expected_val)
{
	u32 val = intel_de_read(dev_priv, reg);

	if ((val & mask) != expected_val) {
		drm_dbg(&dev_priv->drm,
			"Combo PHY %c reg %08x state mismatch: "
			"current %08x mask %08x expected %08x\n",
			phy_name(phy),
			reg.reg, val, mask, expected_val);
		return false;
	}

	return true;
}

static bool icl_verify_procmon_ref_values(struct drm_i915_private *dev_priv,
					  enum phy phy)
{
	const struct icl_procmon *procmon;
	bool ret;

	procmon = icl_get_procmon_ref_values(dev_priv, phy);

	ret = check_phy_reg(dev_priv, phy, ICL_PORT_COMP_DW1(phy),
			    (0xff << 16) | 0xff, procmon->dw1);
	ret &= check_phy_reg(dev_priv, phy, ICL_PORT_COMP_DW9(phy),
			     -1U, procmon->dw9);
	ret &= check_phy_reg(dev_priv, phy, ICL_PORT_COMP_DW10(phy),
			     -1U, procmon->dw10);

	return ret;
}

static bool has_phy_misc(struct drm_i915_private *i915, enum phy phy)
{
	/*
	 * Some platforms only expect PHY_MISC to be programmed for PHY-A and
	 * PHY-B and may not even have instances of the register for the
	 * other combo PHY's.
	 *
	 * ADL-S technically has three instances of PHY_MISC, but only requires
	 * that we program it for PHY A.
	 */

	if (IS_ALDERLAKE_S(i915))
		return phy == PHY_A;
	else if ((IS_JASPERLAKE(i915) || IS_ELKHARTLAKE(i915)) ||
		 IS_ROCKETLAKE(i915) ||
		 IS_DG1(i915))
		return phy < PHY_C;

	return true;
}

static bool icl_combo_phy_enabled(struct drm_i915_private *dev_priv,
				  enum phy phy)
{
	/* The PHY C added by EHL has no PHY_MISC register */
	if (!has_phy_misc(dev_priv, phy))
		return intel_de_read(dev_priv, ICL_PORT_COMP_DW0(phy)) & COMP_INIT;
	else
		return !(intel_de_read(dev_priv, ICL_PHY_MISC(phy)) &
			 ICL_PHY_MISC_DE_IO_COMP_PWR_DOWN) &&
			(intel_de_read(dev_priv, ICL_PORT_COMP_DW0(phy)) & COMP_INIT);
}

static bool ehl_vbt_ddi_d_present(struct drm_i915_private *i915)
{
	struct intel_display *display = &i915->display;

	bool ddi_a_present = intel_bios_is_port_present(display, PORT_A);
	bool ddi_d_present = intel_bios_is_port_present(display, PORT_D);
	bool dsi_present = intel_bios_is_dsi_present(display, NULL);

	/*
	 * VBT's 'dvo port' field for child devices references the DDI, not
	 * the PHY.  So if combo PHY A is wired up to drive an external
	 * display, we should see a child device present on PORT_D and
	 * nothing on PORT_A and no DSI.
	 */
	if (ddi_d_present && !ddi_a_present && !dsi_present)
		return true;

	/*
	 * If we encounter a VBT that claims to have an external display on
	 * DDI-D _and_ an internal display on DDI-A/DSI leave an error message
	 * in the log and let the internal display win.
	 */
	if (ddi_d_present)
		drm_err(&i915->drm,
			"VBT claims to have both internal and external displays on PHY A.  Configuring for internal.\n");

	return false;
}

static bool phy_is_master(struct drm_i915_private *dev_priv, enum phy phy)
{
	/*
	 * Certain PHYs are connected to compensation resistors and act
	 * as masters to other PHYs.
	 *
	 * ICL,TGL:
	 *   A(master) -> B(slave), C(slave)
	 * RKL,DG1:
	 *   A(master) -> B(slave)
	 *   C(master) -> D(slave)
	 * ADL-S:
	 *   A(master) -> B(slave), C(slave)
	 *   D(master) -> E(slave)
	 *
	 * We must set the IREFGEN bit for any PHY acting as a master
	 * to another PHY.
	 */
	if (phy == PHY_A)
		return true;
	else if (IS_ALDERLAKE_S(dev_priv))
		return phy == PHY_D;
	else if (IS_DG1(dev_priv) || IS_ROCKETLAKE(dev_priv))
		return phy == PHY_C;

	return false;
}

static bool icl_combo_phy_verify_state(struct drm_i915_private *dev_priv,
				       enum phy phy)
{
	bool ret = true;
	u32 expected_val = 0;

	if (!icl_combo_phy_enabled(dev_priv, phy))
		return false;

	if (DISPLAY_VER(dev_priv) >= 12) {
		ret &= check_phy_reg(dev_priv, phy, ICL_PORT_TX_DW8_LN(0, phy),
				     ICL_PORT_TX_DW8_ODCC_CLK_SEL |
				     ICL_PORT_TX_DW8_ODCC_CLK_DIV_SEL_MASK,
				     ICL_PORT_TX_DW8_ODCC_CLK_SEL |
				     ICL_PORT_TX_DW8_ODCC_CLK_DIV_SEL_DIV2);

		ret &= check_phy_reg(dev_priv, phy, ICL_PORT_PCS_DW1_LN(0, phy),
				     DCC_MODE_SELECT_MASK, RUN_DCC_ONCE);
	}

	ret &= icl_verify_procmon_ref_values(dev_priv, phy);

	if (phy_is_master(dev_priv, phy)) {
		ret &= check_phy_reg(dev_priv, phy, ICL_PORT_COMP_DW8(phy),
				     IREFGEN, IREFGEN);

		if (IS_JASPERLAKE(dev_priv) || IS_ELKHARTLAKE(dev_priv)) {
			if (ehl_vbt_ddi_d_present(dev_priv))
				expected_val = ICL_PHY_MISC_MUX_DDID;

			ret &= check_phy_reg(dev_priv, phy, ICL_PHY_MISC(phy),
					     ICL_PHY_MISC_MUX_DDID,
					     expected_val);
		}
	}

	ret &= check_phy_reg(dev_priv, phy, ICL_PORT_CL_DW5(phy),
			     CL_POWER_DOWN_ENABLE, CL_POWER_DOWN_ENABLE);

	return ret;
}

void intel_combo_phy_power_up_lanes(struct drm_i915_private *dev_priv,
				    enum phy phy, bool is_dsi,
				    int lane_count, bool lane_reversal)
{
	u8 lane_mask;

	if (is_dsi) {
		drm_WARN_ON(&dev_priv->drm, lane_reversal);

		switch (lane_count) {
		case 1:
			lane_mask = PWR_DOWN_LN_3_1_0;
			break;
		case 2:
			lane_mask = PWR_DOWN_LN_3_1;
			break;
		case 3:
			lane_mask = PWR_DOWN_LN_3;
			break;
		default:
			MISSING_CASE(lane_count);
			fallthrough;
		case 4:
			lane_mask = PWR_UP_ALL_LANES;
			break;
		}
	} else {
		switch (lane_count) {
		case 1:
			lane_mask = lane_reversal ? PWR_DOWN_LN_2_1_0 :
						    PWR_DOWN_LN_3_2_1;
			break;
		case 2:
			lane_mask = lane_reversal ? PWR_DOWN_LN_1_0 :
						    PWR_DOWN_LN_3_2;
			break;
		default:
			MISSING_CASE(lane_count);
			fallthrough;
		case 4:
			lane_mask = PWR_UP_ALL_LANES;
			break;
		}
	}

	intel_de_rmw(dev_priv, ICL_PORT_CL_DW10(phy),
		     PWR_DOWN_LN_MASK, lane_mask);
}

static void icl_combo_phys_init(struct drm_i915_private *dev_priv)
{
	enum phy phy;

	for_each_combo_phy(dev_priv, phy) {
		const struct icl_procmon *procmon;
		u32 val;

		if (icl_combo_phy_verify_state(dev_priv, phy))
			continue;

		procmon = icl_get_procmon_ref_values(dev_priv, phy);

		drm_dbg(&dev_priv->drm,
			"Initializing combo PHY %c (Voltage/Process Info : %s)\n",
			phy_name(phy), procmon->name);

		if (!has_phy_misc(dev_priv, phy))
			goto skip_phy_misc;

		/*
		 * EHL's combo PHY A can be hooked up to either an external
		 * display (via DDI-D) or an internal display (via DDI-A or
		 * the DSI DPHY).  This is a motherboard design decision that
		 * can't be changed on the fly, so initialize the PHY's mux
		 * based on whether our VBT indicates the presence of any
		 * "internal" child devices.
		 */
		val = intel_de_read(dev_priv, ICL_PHY_MISC(phy));
		if ((IS_JASPERLAKE(dev_priv) || IS_ELKHARTLAKE(dev_priv)) &&
		    phy == PHY_A) {
			val &= ~ICL_PHY_MISC_MUX_DDID;

			if (ehl_vbt_ddi_d_present(dev_priv))
				val |= ICL_PHY_MISC_MUX_DDID;
		}

		val &= ~ICL_PHY_MISC_DE_IO_COMP_PWR_DOWN;
		intel_de_write(dev_priv, ICL_PHY_MISC(phy), val);

skip_phy_misc:
		if (DISPLAY_VER(dev_priv) >= 12) {
			val = intel_de_read(dev_priv, ICL_PORT_TX_DW8_LN(0, phy));
			val &= ~ICL_PORT_TX_DW8_ODCC_CLK_DIV_SEL_MASK;
			val |= ICL_PORT_TX_DW8_ODCC_CLK_SEL;
			val |= ICL_PORT_TX_DW8_ODCC_CLK_DIV_SEL_DIV2;
			intel_de_write(dev_priv, ICL_PORT_TX_DW8_GRP(phy), val);

			val = intel_de_read(dev_priv, ICL_PORT_PCS_DW1_LN(0, phy));
			val &= ~DCC_MODE_SELECT_MASK;
			val |= RUN_DCC_ONCE;
			intel_de_write(dev_priv, ICL_PORT_PCS_DW1_GRP(phy), val);
		}

		icl_set_procmon_ref_values(dev_priv, phy);

		if (phy_is_master(dev_priv, phy))
			intel_de_rmw(dev_priv, ICL_PORT_COMP_DW8(phy),
				     0, IREFGEN);

		intel_de_rmw(dev_priv, ICL_PORT_COMP_DW0(phy), 0, COMP_INIT);
		intel_de_rmw(dev_priv, ICL_PORT_CL_DW5(phy),
			     0, CL_POWER_DOWN_ENABLE);
	}
}

static void icl_combo_phys_uninit(struct drm_i915_private *dev_priv)
{
	enum phy phy;

	for_each_combo_phy_reverse(dev_priv, phy) {
		if (phy == PHY_A &&
		    !icl_combo_phy_verify_state(dev_priv, phy)) {
			if (IS_TIGERLAKE(dev_priv) || IS_DG1(dev_priv)) {
				/*
				 * A known problem with old ifwi:
				 * https://gitlab.freedesktop.org/drm/intel/-/issues/2411
				 * Suppress the warning for CI. Remove ASAP!
				 */
				drm_dbg_kms(&dev_priv->drm,
					    "Combo PHY %c HW state changed unexpectedly\n",
					    phy_name(phy));
			} else {
				drm_warn(&dev_priv->drm,
					 "Combo PHY %c HW state changed unexpectedly\n",
					 phy_name(phy));
			}
		}

		if (!has_phy_misc(dev_priv, phy))
			goto skip_phy_misc;

		intel_de_rmw(dev_priv, ICL_PHY_MISC(phy), 0,
			     ICL_PHY_MISC_DE_IO_COMP_PWR_DOWN);

skip_phy_misc:
		intel_de_rmw(dev_priv, ICL_PORT_COMP_DW0(phy), COMP_INIT, 0);
	}
}

void intel_combo_phy_init(struct drm_i915_private *i915)
{
	icl_combo_phys_init(i915);
}

void intel_combo_phy_uninit(struct drm_i915_private *i915)
{
	icl_combo_phys_uninit(i915);
}
