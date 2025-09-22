/*	$OpenBSD: igc_base.c,v 1.2 2024/05/24 06:02:57 jsg Exp $	*/
/*-
 * Copyright 2021 Intel Corp
 * Copyright 2021 Rubicon Communications, LLC (Netgate)
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <dev/pci/igc_hw.h>
#include <dev/pci/if_igc.h>
#include <dev/pci/igc_mac.h>
#include <dev/pci/igc_base.h>

/**
 *  igc_acquire_phy_base - Acquire rights to access PHY
 *  @hw: pointer to the HW structure
 *
 *  Acquire access rights to the correct PHY.
 **/
int
igc_acquire_phy_base(struct igc_hw *hw)
{
	uint16_t mask = IGC_SWFW_PHY0_SM;

	DEBUGFUNC("igc_acquire_phy_base");

	if (hw->bus.func == IGC_FUNC_1)
		mask = IGC_SWFW_PHY1_SM;

	return hw->mac.ops.acquire_swfw_sync(hw, mask);
}

/**
 *  igc_release_phy_base - Release rights to access PHY
 *  @hw: pointer to the HW structure
 *
 *  A wrapper to release access rights to the correct PHY.
 **/
void
igc_release_phy_base(struct igc_hw *hw)
{
	uint16_t mask = IGC_SWFW_PHY0_SM;

	DEBUGFUNC("igc_release_phy_base");

	if (hw->bus.func == IGC_FUNC_1)
		mask = IGC_SWFW_PHY1_SM;

	hw->mac.ops.release_swfw_sync(hw, mask);
}

/**
 *  igc_init_hw_base - Initialize hardware
 *  @hw: pointer to the HW structure
 *
 *  This inits the hardware readying it for operation.
 **/
int
igc_init_hw_base(struct igc_hw *hw)
{
	struct igc_mac_info *mac = &hw->mac;
	uint16_t i, rar_count = mac->rar_entry_count;
	int ret_val;

	DEBUGFUNC("igc_init_hw_base");

	/* Setup the receive address */
	igc_init_rx_addrs_generic(hw, rar_count);

	/* Zero out the Multicast HASH table */
	DEBUGOUT("Zeroing the MTA\n");
	for (i = 0; i < mac->mta_reg_count; i++)
		IGC_WRITE_REG_ARRAY(hw, IGC_MTA, i, 0);

	/* Zero out the Unicast HASH table */
	DEBUGOUT("Zeroing the UTA\n");
	for (i = 0; i < mac->uta_reg_count; i++)
		IGC_WRITE_REG_ARRAY(hw, IGC_UTA, i, 0);

	/* Setup link and flow control */
	ret_val = mac->ops.setup_link(hw);
	/*
	 * Clear all of the statistics registers (clear on read).  It is
	 * important that we do this after we have tried to establish link
	 * because the symbol error count will increment wildly if there
	 * is no link.
	 */
	igc_clear_hw_cntrs_base_generic(hw);

	return ret_val;
}

/**
 * igc_power_down_phy_copper_base - Remove link during PHY power down
 * @hw: pointer to the HW structure
 *
 * In the case of a PHY power down to save power, or to turn off link during a
 * driver unload, or wake on lan is not enabled, remove the link.
 **/
void
igc_power_down_phy_copper_base(struct igc_hw *hw)
{
	struct igc_phy_info *phy = &hw->phy;

	if (!(phy->ops.check_reset_block))
		return;

	/* If the management interface is not enabled, then power down */
	if (phy->ops.check_reset_block(hw))
		igc_power_down_phy_copper(hw);

	return;
}
