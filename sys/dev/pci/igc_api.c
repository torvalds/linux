/*	$OpenBSD: igc_api.c,v 1.1 2021/10/31 14:52:57 patrick Exp $	*/
/*-
 * Copyright 2021 Intel Corp
 * Copyright 2021 Rubicon Communications, LLC (Netgate)
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <dev/pci/igc_api.h>
#include <dev/pci/igc_hw.h>

/**
 *  igc_init_mac_params - Initialize MAC function pointers
 *  @hw: pointer to the HW structure
 *
 *  This function initializes the function pointers for the MAC
 *  set of functions.  Called by drivers or by igc_setup_init_funcs.
 **/
int
igc_init_mac_params(struct igc_hw *hw)
{
	int ret_val = IGC_SUCCESS;

	if (hw->mac.ops.init_params) {
		ret_val = hw->mac.ops.init_params(hw);
		if (ret_val) {
			DEBUGOUT("MAC Initialization Error\n");
			goto out;
		}
	} else {
		DEBUGOUT("mac.init_mac_params was NULL\n");
		ret_val = -IGC_ERR_CONFIG;
	}
out:
	return ret_val;
}

/**
 *  igc_init_nvm_params - Initialize NVM function pointers
 *  @hw: pointer to the HW structure
 *
 *  This function initializes the function pointers for the NVM
 *  set of functions.  Called by drivers or by igc_setup_init_funcs.
 **/
int
igc_init_nvm_params(struct igc_hw *hw)
{
	int ret_val = IGC_SUCCESS;

	if (hw->nvm.ops.init_params) {
		ret_val = hw->nvm.ops.init_params(hw);
		if (ret_val) {
			DEBUGOUT("NVM Initialization Error\n");
			goto out;
		}
	} else {
		DEBUGOUT("nvm.init_nvm_params was NULL\n");
		ret_val = -IGC_ERR_CONFIG;
	}
out:
	return ret_val;
}

/**
 *  igc_init_phy_params - Initialize PHY function pointers
 *  @hw: pointer to the HW structure
 *
 *  This function initializes the function pointers for the PHY
 *  set of functions.  Called by drivers or by igc_setup_init_funcs.
 **/
int
igc_init_phy_params(struct igc_hw *hw)
{
	int ret_val = IGC_SUCCESS;

	if (hw->phy.ops.init_params) {
		ret_val = hw->phy.ops.init_params(hw);
		if (ret_val) {
			DEBUGOUT("PHY Initialization Error\n");
			goto out;
		}
	} else {
		DEBUGOUT("phy.init_phy_params was NULL\n");
		ret_val =  -IGC_ERR_CONFIG;
	}
out:
	return ret_val;
}

/**
 *  igc_set_mac_type - Sets MAC type
 *  @hw: pointer to the HW structure
 *
 *  This function sets the mac type of the adapter based on the
 *  device ID stored in the hw structure.
 *  MUST BE FIRST FUNCTION CALLED (explicitly or through
 *  igc_setup_init_funcs()).
 **/
int
igc_set_mac_type(struct igc_hw *hw)
{
	struct igc_mac_info *mac = &hw->mac;
	int ret_val = IGC_SUCCESS;

	DEBUGFUNC("igc_set_mac_type");

	switch (hw->device_id) {
	case PCI_PRODUCT_INTEL_I220_V:
	case PCI_PRODUCT_INTEL_I221_V:
	case PCI_PRODUCT_INTEL_I225_BLANK_NVM:
	case PCI_PRODUCT_INTEL_I225_I:
	case PCI_PRODUCT_INTEL_I225_IT:
	case PCI_PRODUCT_INTEL_I225_K:
	case PCI_PRODUCT_INTEL_I225_K2:
	case PCI_PRODUCT_INTEL_I225_LM:
	case PCI_PRODUCT_INTEL_I225_LMVP:
	case PCI_PRODUCT_INTEL_I225_V:
	case PCI_PRODUCT_INTEL_I226_BLANK_NVM:
	case PCI_PRODUCT_INTEL_I226_IT:
	case PCI_PRODUCT_INTEL_I226_LM:
	case PCI_PRODUCT_INTEL_I226_K:
	case PCI_PRODUCT_INTEL_I226_V:
		mac->type = igc_i225;
		break;
	default:
		/* Should never have loaded on this device */
		ret_val = -IGC_ERR_MAC_INIT;
		break;
	}

	return ret_val;
}

/**
 *  igc_setup_init_funcs - Initializes function pointers
 *  @hw: pointer to the HW structure
 *  @init_device: true will initialize the rest of the function pointers
 *		  getting the device ready for use.  FALSE will only set
 *		  MAC type and the function pointers for the other init
 *		  functions.  Passing FALSE will not generate any hardware
 *		  reads or writes.
 *
 *  This function must be called by a driver in order to use the rest
 *  of the 'shared' code files. Called by drivers only.
 **/
int
igc_setup_init_funcs(struct igc_hw *hw, bool init_device)
{
	int ret_val;

	/* Can't do much good without knowing the MAC type. */
	ret_val = igc_set_mac_type(hw);
	if (ret_val) {
		DEBUGOUT("ERROR: MAC type could not be set properly.\n");
		goto out;
	}

	if (!hw->hw_addr) {
		DEBUGOUT("ERROR: Registers not mapped\n");
		ret_val = -IGC_ERR_CONFIG;
		goto out;
	}

	/*
	 * Init function pointers to generic implementations. We do this first
	 * allowing a driver module to override it afterward.
	 */
	igc_init_mac_ops_generic(hw);
	igc_init_phy_ops_generic(hw);
	igc_init_nvm_ops_generic(hw);

	/*
	 * Set up the init function pointers. These are functions within the
	 * adapter family file that sets up function pointers for the rest of
	 * the functions in that family.
	 */
	switch (hw->mac.type) {
	case igc_i225:
		igc_init_function_pointers_i225(hw);
		break;
	default:
		DEBUGOUT("Hardware not supported\n");
		ret_val = -IGC_ERR_CONFIG;
		break;
	}

	/*
	 * Initialize the rest of the function pointers. These require some
	 * register reads/writes in some cases.
	 */
	if (!(ret_val) && init_device) {
		ret_val = igc_init_mac_params(hw);
		if (ret_val)
			goto out;

		ret_val = igc_init_nvm_params(hw);
		if (ret_val)
			goto out;

		ret_val = igc_init_phy_params(hw);
		if (ret_val)
			goto out;
	}
out:
	return ret_val;
}

/**
 *  igc_update_mc_addr_list - Update Multicast addresses
 *  @hw: pointer to the HW structure
 *  @mc_addr_list: array of multicast addresses to program
 *  @mc_addr_count: number of multicast addresses to program
 *
 *  Updates the Multicast Table Array.
 *  The caller must have a packed mc_addr_list of multicast addresses.
 **/
void
igc_update_mc_addr_list(struct igc_hw *hw, uint8_t *mc_addr_list,
    uint32_t mc_addr_count)
{
	if (hw->mac.ops.update_mc_addr_list)
		hw->mac.ops.update_mc_addr_list(hw, mc_addr_list,
		    mc_addr_count);
}

/**
 *  igc_check_for_link - Check/Store link connection
 *  @hw: pointer to the HW structure
 *
 *  This checks the link condition of the adapter and stores the
 *  results in the hw->mac structure. This is a function pointer entry
 *  point called by drivers.
 **/
int
igc_check_for_link(struct igc_hw *hw)
{
	if (hw->mac.ops.check_for_link)
		return hw->mac.ops.check_for_link(hw);

	return -IGC_ERR_CONFIG;
}

/**
 *  igc_reset_hw - Reset hardware
 *  @hw: pointer to the HW structure
 *
 *  This resets the hardware into a known state. This is a function pointer
 *  entry point called by drivers.
 **/
int
igc_reset_hw(struct igc_hw *hw)
{
	if (hw->mac.ops.reset_hw)
		return hw->mac.ops.reset_hw(hw);

	return -IGC_ERR_CONFIG;
}

/**
 *  igc_init_hw - Initialize hardware
 *  @hw: pointer to the HW structure
 *
 *  This inits the hardware readying it for operation. This is a function
 *  pointer entry point called by drivers.
 **/
int
igc_init_hw(struct igc_hw *hw)
{
	if (hw->mac.ops.init_hw)
		return hw->mac.ops.init_hw(hw);

	return -IGC_ERR_CONFIG;
}

/**
 *  igc_get_speed_and_duplex - Returns current speed and duplex
 *  @hw: pointer to the HW structure
 *  @speed: pointer to a 16-bit value to store the speed
 *  @duplex: pointer to a 16-bit value to store the duplex.
 *
 *  This returns the speed and duplex of the adapter in the two 'out'
 *  variables passed in. This is a function pointer entry point called
 *  by drivers.
 **/
int
igc_get_speed_and_duplex(struct igc_hw *hw, uint16_t *speed, uint16_t *duplex)
{
	if (hw->mac.ops.get_link_up_info)
		return hw->mac.ops.get_link_up_info(hw, speed, duplex);

	return -IGC_ERR_CONFIG;
}

/**
 *  igc_rar_set - Sets a receive address register
 *  @hw: pointer to the HW structure
 *  @addr: address to set the RAR to
 *  @index: the RAR to set
 *
 *  Sets a Receive Address Register (RAR) to the specified address.
 **/
int
igc_rar_set(struct igc_hw *hw, uint8_t *addr, uint32_t index)
{
	if (hw->mac.ops.rar_set)
		return hw->mac.ops.rar_set(hw, addr, index);

	return IGC_SUCCESS;
}

/**
 *  igc_check_reset_block - Verifies PHY can be reset
 *  @hw: pointer to the HW structure
 *
 *  Checks if the PHY is in a state that can be reset or if manageability
 *  has it tied up. This is a function pointer entry point called by drivers.
 **/
int
igc_check_reset_block(struct igc_hw *hw)
{
	if (hw->phy.ops.check_reset_block)
		return hw->phy.ops.check_reset_block(hw);

	return IGC_SUCCESS;
}

/**
 *  igc_get_phy_info - Retrieves PHY information from registers
 *  @hw: pointer to the HW structure
 *
 *  This function gets some information from various PHY registers and
 *  populates hw->phy values with it. This is a function pointer entry
 *  point called by drivers.
 **/
int
igc_get_phy_info(struct igc_hw *hw)
{
	if (hw->phy.ops.get_info)
		return hw->phy.ops.get_info(hw);

	return IGC_SUCCESS;
}

/**
 *  igc_phy_hw_reset - Hard PHY reset
 *  @hw: pointer to the HW structure
 *
 *  Performs a hard PHY reset. This is a function pointer entry point called
 *  by drivers.
 **/
int
igc_phy_hw_reset(struct igc_hw *hw)
{
	if (hw->phy.ops.reset)
		return hw->phy.ops.reset(hw);

	return IGC_SUCCESS;
}

/**
 *  igc_read_mac_addr - Reads MAC address
 *  @hw: pointer to the HW structure
 *
 *  Reads the MAC address out of the adapter and stores it in the HW structure.
 *  Currently no func pointer exists and all implementations are handled in the
 *  generic version of this function.
 **/
int
igc_read_mac_addr(struct igc_hw *hw)
{
	if (hw->mac.ops.read_mac_addr)
		return hw->mac.ops.read_mac_addr(hw);

	return igc_read_mac_addr_generic(hw);
}

/**
 *  igc_validate_nvm_checksum - Verifies NVM (EEPROM) checksum
 *  @hw: pointer to the HW structure
 *
 *  Validates the NVM checksum is correct. This is a function pointer entry
 *  point called by drivers.
 **/
int
igc_validate_nvm_checksum(struct igc_hw *hw)
{
	if (hw->nvm.ops.validate)
		return hw->nvm.ops.validate(hw);

	return -IGC_ERR_CONFIG;
}
