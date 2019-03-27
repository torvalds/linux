/******************************************************************************
  SPDX-License-Identifier: BSD-3-Clause

  Copyright (c) 2001-2017, Intel Corporation
  All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:

   1. Redistributions of source code must retain the above copyright notice,
      this list of conditions and the following disclaimer.

   2. Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.

   3. Neither the name of the Intel Corporation nor the names of its
      contributors may be used to endorse or promote products derived from
      this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
  POSSIBILITY OF SUCH DAMAGE.

******************************************************************************/
/*$FreeBSD$*/

#include "ixgbe_api.h"
#include "ixgbe_common.h"

#define IXGBE_EMPTY_PARAM

static const u32 ixgbe_mvals_base[IXGBE_MVALS_IDX_LIMIT] = {
	IXGBE_MVALS_INIT(IXGBE_EMPTY_PARAM)
};

static const u32 ixgbe_mvals_X540[IXGBE_MVALS_IDX_LIMIT] = {
	IXGBE_MVALS_INIT(_X540)
};

static const u32 ixgbe_mvals_X550[IXGBE_MVALS_IDX_LIMIT] = {
	IXGBE_MVALS_INIT(_X550)
};

static const u32 ixgbe_mvals_X550EM_x[IXGBE_MVALS_IDX_LIMIT] = {
	IXGBE_MVALS_INIT(_X550EM_x)
};

static const u32 ixgbe_mvals_X550EM_a[IXGBE_MVALS_IDX_LIMIT] = {
	IXGBE_MVALS_INIT(_X550EM_a)
};

/**
 * ixgbe_dcb_get_rtrup2tc - read rtrup2tc reg
 * @hw: pointer to hardware structure
 * @map: pointer to u8 arr for returning map
 *
 * Read the rtrup2tc HW register and resolve its content into map
 **/
void ixgbe_dcb_get_rtrup2tc(struct ixgbe_hw *hw, u8 *map)
{
	if (hw->mac.ops.get_rtrup2tc)
		hw->mac.ops.get_rtrup2tc(hw, map);
}

/**
 *  ixgbe_init_shared_code - Initialize the shared code
 *  @hw: pointer to hardware structure
 *
 *  This will assign function pointers and assign the MAC type and PHY code.
 *  Does not touch the hardware. This function must be called prior to any
 *  other function in the shared code. The ixgbe_hw structure should be
 *  memset to 0 prior to calling this function.  The following fields in
 *  hw structure should be filled in prior to calling this function:
 *  hw_addr, back, device_id, vendor_id, subsystem_device_id,
 *  subsystem_vendor_id, and revision_id
 **/
s32 ixgbe_init_shared_code(struct ixgbe_hw *hw)
{
	s32 status;

	DEBUGFUNC("ixgbe_init_shared_code");

	/*
	 * Set the mac type
	 */
	ixgbe_set_mac_type(hw);

	switch (hw->mac.type) {
	case ixgbe_mac_82598EB:
		status = ixgbe_init_ops_82598(hw);
		break;
	case ixgbe_mac_82599EB:
		status = ixgbe_init_ops_82599(hw);
		break;
	case ixgbe_mac_X540:
		status = ixgbe_init_ops_X540(hw);
		break;
	case ixgbe_mac_X550:
		status = ixgbe_init_ops_X550(hw);
		break;
	case ixgbe_mac_X550EM_x:
		status = ixgbe_init_ops_X550EM_x(hw);
		break;
	case ixgbe_mac_X550EM_a:
		status = ixgbe_init_ops_X550EM_a(hw);
		break;
	default:
		status = IXGBE_ERR_DEVICE_NOT_SUPPORTED;
		break;
	}
	hw->mac.max_link_up_time = IXGBE_LINK_UP_TIME;

	return status;
}

/**
 *  ixgbe_set_mac_type - Sets MAC type
 *  @hw: pointer to the HW structure
 *
 *  This function sets the mac type of the adapter based on the
 *  vendor ID and device ID stored in the hw structure.
 **/
s32 ixgbe_set_mac_type(struct ixgbe_hw *hw)
{
	s32 ret_val = IXGBE_SUCCESS;

	DEBUGFUNC("ixgbe_set_mac_type\n");

	if (hw->vendor_id != IXGBE_INTEL_VENDOR_ID) {
		ERROR_REPORT2(IXGBE_ERROR_UNSUPPORTED,
			     "Unsupported vendor id: %x", hw->vendor_id);
		return IXGBE_ERR_DEVICE_NOT_SUPPORTED;
	}

	hw->mvals = ixgbe_mvals_base;

	switch (hw->device_id) {
	case IXGBE_DEV_ID_82598:
	case IXGBE_DEV_ID_82598_BX:
	case IXGBE_DEV_ID_82598AF_SINGLE_PORT:
	case IXGBE_DEV_ID_82598AF_DUAL_PORT:
	case IXGBE_DEV_ID_82598AT:
	case IXGBE_DEV_ID_82598AT2:
	case IXGBE_DEV_ID_82598EB_CX4:
	case IXGBE_DEV_ID_82598_CX4_DUAL_PORT:
	case IXGBE_DEV_ID_82598_DA_DUAL_PORT:
	case IXGBE_DEV_ID_82598_SR_DUAL_PORT_EM:
	case IXGBE_DEV_ID_82598EB_XF_LR:
	case IXGBE_DEV_ID_82598EB_SFP_LOM:
		hw->mac.type = ixgbe_mac_82598EB;
		break;
	case IXGBE_DEV_ID_82599_KX4:
	case IXGBE_DEV_ID_82599_KX4_MEZZ:
	case IXGBE_DEV_ID_82599_XAUI_LOM:
	case IXGBE_DEV_ID_82599_COMBO_BACKPLANE:
	case IXGBE_DEV_ID_82599_KR:
	case IXGBE_DEV_ID_82599_SFP:
	case IXGBE_DEV_ID_82599_BACKPLANE_FCOE:
	case IXGBE_DEV_ID_82599_SFP_FCOE:
	case IXGBE_DEV_ID_82599_SFP_EM:
	case IXGBE_DEV_ID_82599_SFP_SF2:
	case IXGBE_DEV_ID_82599_SFP_SF_QP:
	case IXGBE_DEV_ID_82599_QSFP_SF_QP:
	case IXGBE_DEV_ID_82599EN_SFP:
	case IXGBE_DEV_ID_82599_CX4:
	case IXGBE_DEV_ID_82599_BYPASS:
	case IXGBE_DEV_ID_82599_T3_LOM:
		hw->mac.type = ixgbe_mac_82599EB;
		break;
	case IXGBE_DEV_ID_X540T:
	case IXGBE_DEV_ID_X540T1:
	case IXGBE_DEV_ID_X540_BYPASS:
		hw->mac.type = ixgbe_mac_X540;
		hw->mvals = ixgbe_mvals_X540;
		break;
	case IXGBE_DEV_ID_X550T:
	case IXGBE_DEV_ID_X550T1:
		hw->mac.type = ixgbe_mac_X550;
		hw->mvals = ixgbe_mvals_X550;
		break;
	case IXGBE_DEV_ID_X550EM_X_KX4:
	case IXGBE_DEV_ID_X550EM_X_KR:
	case IXGBE_DEV_ID_X550EM_X_10G_T:
	case IXGBE_DEV_ID_X550EM_X_1G_T:
	case IXGBE_DEV_ID_X550EM_X_SFP:
	case IXGBE_DEV_ID_X550EM_X_XFI:
		hw->mac.type = ixgbe_mac_X550EM_x;
		hw->mvals = ixgbe_mvals_X550EM_x;
		break;
	case IXGBE_DEV_ID_X550EM_A_KR:
	case IXGBE_DEV_ID_X550EM_A_KR_L:
	case IXGBE_DEV_ID_X550EM_A_SFP_N:
	case IXGBE_DEV_ID_X550EM_A_SGMII:
	case IXGBE_DEV_ID_X550EM_A_SGMII_L:
	case IXGBE_DEV_ID_X550EM_A_1G_T:
	case IXGBE_DEV_ID_X550EM_A_1G_T_L:
	case IXGBE_DEV_ID_X550EM_A_10G_T:
	case IXGBE_DEV_ID_X550EM_A_QSFP:
	case IXGBE_DEV_ID_X550EM_A_QSFP_N:
	case IXGBE_DEV_ID_X550EM_A_SFP:
		hw->mac.type = ixgbe_mac_X550EM_a;
		hw->mvals = ixgbe_mvals_X550EM_a;
		break;
	default:
		ret_val = IXGBE_ERR_DEVICE_NOT_SUPPORTED;
		ERROR_REPORT2(IXGBE_ERROR_UNSUPPORTED,
			     "Unsupported device id: %x",
			     hw->device_id);
		break;
	}

	DEBUGOUT2("ixgbe_set_mac_type found mac: %d, returns: %d\n",
		  hw->mac.type, ret_val);
	return ret_val;
}

/**
 *  ixgbe_init_hw - Initialize the hardware
 *  @hw: pointer to hardware structure
 *
 *  Initialize the hardware by resetting and then starting the hardware
 **/
s32 ixgbe_init_hw(struct ixgbe_hw *hw)
{
	return ixgbe_call_func(hw, hw->mac.ops.init_hw, (hw),
			       IXGBE_NOT_IMPLEMENTED);
}

/**
 *  ixgbe_reset_hw - Performs a hardware reset
 *  @hw: pointer to hardware structure
 *
 *  Resets the hardware by resetting the transmit and receive units, masks and
 *  clears all interrupts, performs a PHY reset, and performs a MAC reset
 **/
s32 ixgbe_reset_hw(struct ixgbe_hw *hw)
{
	return ixgbe_call_func(hw, hw->mac.ops.reset_hw, (hw),
			       IXGBE_NOT_IMPLEMENTED);
}

/**
 *  ixgbe_start_hw - Prepares hardware for Rx/Tx
 *  @hw: pointer to hardware structure
 *
 *  Starts the hardware by filling the bus info structure and media type,
 *  clears all on chip counters, initializes receive address registers,
 *  multicast table, VLAN filter table, calls routine to setup link and
 *  flow control settings, and leaves transmit and receive units disabled
 *  and uninitialized.
 **/
s32 ixgbe_start_hw(struct ixgbe_hw *hw)
{
	return ixgbe_call_func(hw, hw->mac.ops.start_hw, (hw),
			       IXGBE_NOT_IMPLEMENTED);
}

/**
 *  ixgbe_enable_relaxed_ordering - Enables tx relaxed ordering,
 *  which is disabled by default in ixgbe_start_hw();
 *
 *  @hw: pointer to hardware structure
 *
 *   Enable relaxed ordering;
 **/
void ixgbe_enable_relaxed_ordering(struct ixgbe_hw *hw)
{
	if (hw->mac.ops.enable_relaxed_ordering)
		hw->mac.ops.enable_relaxed_ordering(hw);
}

/**
 *  ixgbe_clear_hw_cntrs - Clear hardware counters
 *  @hw: pointer to hardware structure
 *
 *  Clears all hardware statistics counters by reading them from the hardware
 *  Statistics counters are clear on read.
 **/
s32 ixgbe_clear_hw_cntrs(struct ixgbe_hw *hw)
{
	return ixgbe_call_func(hw, hw->mac.ops.clear_hw_cntrs, (hw),
			       IXGBE_NOT_IMPLEMENTED);
}

/**
 *  ixgbe_get_media_type - Get media type
 *  @hw: pointer to hardware structure
 *
 *  Returns the media type (fiber, copper, backplane)
 **/
enum ixgbe_media_type ixgbe_get_media_type(struct ixgbe_hw *hw)
{
	return ixgbe_call_func(hw, hw->mac.ops.get_media_type, (hw),
			       ixgbe_media_type_unknown);
}

/**
 *  ixgbe_get_mac_addr - Get MAC address
 *  @hw: pointer to hardware structure
 *  @mac_addr: Adapter MAC address
 *
 *  Reads the adapter's MAC address from the first Receive Address Register
 *  (RAR0) A reset of the adapter must have been performed prior to calling
 *  this function in order for the MAC address to have been loaded from the
 *  EEPROM into RAR0
 **/
s32 ixgbe_get_mac_addr(struct ixgbe_hw *hw, u8 *mac_addr)
{
	return ixgbe_call_func(hw, hw->mac.ops.get_mac_addr,
			       (hw, mac_addr), IXGBE_NOT_IMPLEMENTED);
}

/**
 *  ixgbe_get_san_mac_addr - Get SAN MAC address
 *  @hw: pointer to hardware structure
 *  @san_mac_addr: SAN MAC address
 *
 *  Reads the SAN MAC address from the EEPROM, if it's available.  This is
 *  per-port, so set_lan_id() must be called before reading the addresses.
 **/
s32 ixgbe_get_san_mac_addr(struct ixgbe_hw *hw, u8 *san_mac_addr)
{
	return ixgbe_call_func(hw, hw->mac.ops.get_san_mac_addr,
			       (hw, san_mac_addr), IXGBE_NOT_IMPLEMENTED);
}

/**
 *  ixgbe_set_san_mac_addr - Write a SAN MAC address
 *  @hw: pointer to hardware structure
 *  @san_mac_addr: SAN MAC address
 *
 *  Writes A SAN MAC address to the EEPROM.
 **/
s32 ixgbe_set_san_mac_addr(struct ixgbe_hw *hw, u8 *san_mac_addr)
{
	return ixgbe_call_func(hw, hw->mac.ops.set_san_mac_addr,
			       (hw, san_mac_addr), IXGBE_NOT_IMPLEMENTED);
}

/**
 *  ixgbe_get_device_caps - Get additional device capabilities
 *  @hw: pointer to hardware structure
 *  @device_caps: the EEPROM word for device capabilities
 *
 *  Reads the extra device capabilities from the EEPROM
 **/
s32 ixgbe_get_device_caps(struct ixgbe_hw *hw, u16 *device_caps)
{
	return ixgbe_call_func(hw, hw->mac.ops.get_device_caps,
			       (hw, device_caps), IXGBE_NOT_IMPLEMENTED);
}

/**
 *  ixgbe_get_wwn_prefix - Get alternative WWNN/WWPN prefix from the EEPROM
 *  @hw: pointer to hardware structure
 *  @wwnn_prefix: the alternative WWNN prefix
 *  @wwpn_prefix: the alternative WWPN prefix
 *
 *  This function will read the EEPROM from the alternative SAN MAC address
 *  block to check the support for the alternative WWNN/WWPN prefix support.
 **/
s32 ixgbe_get_wwn_prefix(struct ixgbe_hw *hw, u16 *wwnn_prefix,
			 u16 *wwpn_prefix)
{
	return ixgbe_call_func(hw, hw->mac.ops.get_wwn_prefix,
			       (hw, wwnn_prefix, wwpn_prefix),
			       IXGBE_NOT_IMPLEMENTED);
}

/**
 *  ixgbe_get_fcoe_boot_status -  Get FCOE boot status from EEPROM
 *  @hw: pointer to hardware structure
 *  @bs: the fcoe boot status
 *
 *  This function will read the FCOE boot status from the iSCSI FCOE block
 **/
s32 ixgbe_get_fcoe_boot_status(struct ixgbe_hw *hw, u16 *bs)
{
	return ixgbe_call_func(hw, hw->mac.ops.get_fcoe_boot_status,
			       (hw, bs),
			       IXGBE_NOT_IMPLEMENTED);
}

/**
 *  ixgbe_get_bus_info - Set PCI bus info
 *  @hw: pointer to hardware structure
 *
 *  Sets the PCI bus info (speed, width, type) within the ixgbe_hw structure
 **/
s32 ixgbe_get_bus_info(struct ixgbe_hw *hw)
{
	return ixgbe_call_func(hw, hw->mac.ops.get_bus_info, (hw),
			       IXGBE_NOT_IMPLEMENTED);
}

/**
 *  ixgbe_get_num_of_tx_queues - Get Tx queues
 *  @hw: pointer to hardware structure
 *
 *  Returns the number of transmit queues for the given adapter.
 **/
u32 ixgbe_get_num_of_tx_queues(struct ixgbe_hw *hw)
{
	return hw->mac.max_tx_queues;
}

/**
 *  ixgbe_get_num_of_rx_queues - Get Rx queues
 *  @hw: pointer to hardware structure
 *
 *  Returns the number of receive queues for the given adapter.
 **/
u32 ixgbe_get_num_of_rx_queues(struct ixgbe_hw *hw)
{
	return hw->mac.max_rx_queues;
}

/**
 *  ixgbe_stop_adapter - Disable Rx/Tx units
 *  @hw: pointer to hardware structure
 *
 *  Sets the adapter_stopped flag within ixgbe_hw struct. Clears interrupts,
 *  disables transmit and receive units. The adapter_stopped flag is used by
 *  the shared code and drivers to determine if the adapter is in a stopped
 *  state and should not touch the hardware.
 **/
s32 ixgbe_stop_adapter(struct ixgbe_hw *hw)
{
	return ixgbe_call_func(hw, hw->mac.ops.stop_adapter, (hw),
			       IXGBE_NOT_IMPLEMENTED);
}

/**
 *  ixgbe_read_pba_string - Reads part number string from EEPROM
 *  @hw: pointer to hardware structure
 *  @pba_num: stores the part number string from the EEPROM
 *  @pba_num_size: part number string buffer length
 *
 *  Reads the part number string from the EEPROM.
 **/
s32 ixgbe_read_pba_string(struct ixgbe_hw *hw, u8 *pba_num, u32 pba_num_size)
{
	return ixgbe_read_pba_string_generic(hw, pba_num, pba_num_size);
}

/**
 *  ixgbe_read_pba_num - Reads part number from EEPROM
 *  @hw: pointer to hardware structure
 *  @pba_num: stores the part number from the EEPROM
 *
 *  Reads the part number from the EEPROM.
 **/
s32 ixgbe_read_pba_num(struct ixgbe_hw *hw, u32 *pba_num)
{
	return ixgbe_read_pba_num_generic(hw, pba_num);
}

/**
 *  ixgbe_identify_phy - Get PHY type
 *  @hw: pointer to hardware structure
 *
 *  Determines the physical layer module found on the current adapter.
 **/
s32 ixgbe_identify_phy(struct ixgbe_hw *hw)
{
	s32 status = IXGBE_SUCCESS;

	if (hw->phy.type == ixgbe_phy_unknown) {
		status = ixgbe_call_func(hw, hw->phy.ops.identify, (hw),
					 IXGBE_NOT_IMPLEMENTED);
	}

	return status;
}

/**
 *  ixgbe_reset_phy - Perform a PHY reset
 *  @hw: pointer to hardware structure
 **/
s32 ixgbe_reset_phy(struct ixgbe_hw *hw)
{
	s32 status = IXGBE_SUCCESS;

	if (hw->phy.type == ixgbe_phy_unknown) {
		if (ixgbe_identify_phy(hw) != IXGBE_SUCCESS)
			status = IXGBE_ERR_PHY;
	}

	if (status == IXGBE_SUCCESS) {
		status = ixgbe_call_func(hw, hw->phy.ops.reset, (hw),
					 IXGBE_NOT_IMPLEMENTED);
	}
	return status;
}

/**
 *  ixgbe_get_phy_firmware_version -
 *  @hw: pointer to hardware structure
 *  @firmware_version: pointer to firmware version
 **/
s32 ixgbe_get_phy_firmware_version(struct ixgbe_hw *hw, u16 *firmware_version)
{
	s32 status = IXGBE_SUCCESS;

	status = ixgbe_call_func(hw, hw->phy.ops.get_firmware_version,
				 (hw, firmware_version),
				 IXGBE_NOT_IMPLEMENTED);
	return status;
}

/**
 *  ixgbe_read_phy_reg - Read PHY register
 *  @hw: pointer to hardware structure
 *  @reg_addr: 32 bit address of PHY register to read
 *  @device_type: type of device you want to communicate with
 *  @phy_data: Pointer to read data from PHY register
 *
 *  Reads a value from a specified PHY register
 **/
s32 ixgbe_read_phy_reg(struct ixgbe_hw *hw, u32 reg_addr, u32 device_type,
		       u16 *phy_data)
{
	if (hw->phy.id == 0)
		ixgbe_identify_phy(hw);

	return ixgbe_call_func(hw, hw->phy.ops.read_reg, (hw, reg_addr,
			       device_type, phy_data), IXGBE_NOT_IMPLEMENTED);
}

/**
 *  ixgbe_write_phy_reg - Write PHY register
 *  @hw: pointer to hardware structure
 *  @reg_addr: 32 bit PHY register to write
 *  @device_type: type of device you want to communicate with
 *  @phy_data: Data to write to the PHY register
 *
 *  Writes a value to specified PHY register
 **/
s32 ixgbe_write_phy_reg(struct ixgbe_hw *hw, u32 reg_addr, u32 device_type,
			u16 phy_data)
{
	if (hw->phy.id == 0)
		ixgbe_identify_phy(hw);

	return ixgbe_call_func(hw, hw->phy.ops.write_reg, (hw, reg_addr,
			       device_type, phy_data), IXGBE_NOT_IMPLEMENTED);
}

/**
 *  ixgbe_setup_phy_link - Restart PHY autoneg
 *  @hw: pointer to hardware structure
 *
 *  Restart autonegotiation and PHY and waits for completion.
 **/
s32 ixgbe_setup_phy_link(struct ixgbe_hw *hw)
{
	return ixgbe_call_func(hw, hw->phy.ops.setup_link, (hw),
			       IXGBE_NOT_IMPLEMENTED);
}

/**
 * ixgbe_setup_internal_phy - Configure integrated PHY
 * @hw: pointer to hardware structure
 *
 * Reconfigure the integrated PHY in order to enable talk to the external PHY.
 * Returns success if not implemented, since nothing needs to be done in this
 * case.
 */
s32 ixgbe_setup_internal_phy(struct ixgbe_hw *hw)
{
	return ixgbe_call_func(hw, hw->phy.ops.setup_internal_link, (hw),
			       IXGBE_SUCCESS);
}

/**
 *  ixgbe_check_phy_link - Determine link and speed status
 *  @hw: pointer to hardware structure
 *  @speed: link speed
 *  @link_up: TRUE when link is up
 *
 *  Reads a PHY register to determine if link is up and the current speed for
 *  the PHY.
 **/
s32 ixgbe_check_phy_link(struct ixgbe_hw *hw, ixgbe_link_speed *speed,
			 bool *link_up)
{
	return ixgbe_call_func(hw, hw->phy.ops.check_link, (hw, speed,
			       link_up), IXGBE_NOT_IMPLEMENTED);
}

/**
 *  ixgbe_setup_phy_link_speed - Set auto advertise
 *  @hw: pointer to hardware structure
 *  @speed: new link speed
 *  @autoneg_wait_to_complete: TRUE when waiting for completion is needed
 *
 *  Sets the auto advertised capabilities
 **/
s32 ixgbe_setup_phy_link_speed(struct ixgbe_hw *hw, ixgbe_link_speed speed,
			       bool autoneg_wait_to_complete)
{
	return ixgbe_call_func(hw, hw->phy.ops.setup_link_speed, (hw, speed,
			       autoneg_wait_to_complete),
			       IXGBE_NOT_IMPLEMENTED);
}

/**
 * ixgbe_set_phy_power - Control the phy power state
 * @hw: pointer to hardware structure
 * @on: TRUE for on, FALSE for off
 */
s32 ixgbe_set_phy_power(struct ixgbe_hw *hw, bool on)
{
	return ixgbe_call_func(hw, hw->phy.ops.set_phy_power, (hw, on),
			       IXGBE_NOT_IMPLEMENTED);
}

/**
 *  ixgbe_check_link - Get link and speed status
 *  @hw: pointer to hardware structure
 *  @speed: pointer to link speed
 *  @link_up: TRUE when link is up
 *  @link_up_wait_to_complete: bool used to wait for link up or not
 *
 *  Reads the links register to determine if link is up and the current speed
 **/
s32 ixgbe_check_link(struct ixgbe_hw *hw, ixgbe_link_speed *speed,
		     bool *link_up, bool link_up_wait_to_complete)
{
	return ixgbe_call_func(hw, hw->mac.ops.check_link, (hw, speed,
			       link_up, link_up_wait_to_complete),
			       IXGBE_NOT_IMPLEMENTED);
}

/**
 *  ixgbe_disable_tx_laser - Disable Tx laser
 *  @hw: pointer to hardware structure
 *
 *  If the driver needs to disable the laser on SFI optics.
 **/
void ixgbe_disable_tx_laser(struct ixgbe_hw *hw)
{
	if (hw->mac.ops.disable_tx_laser)
		hw->mac.ops.disable_tx_laser(hw);
}

/**
 *  ixgbe_enable_tx_laser - Enable Tx laser
 *  @hw: pointer to hardware structure
 *
 *  If the driver needs to enable the laser on SFI optics.
 **/
void ixgbe_enable_tx_laser(struct ixgbe_hw *hw)
{
	if (hw->mac.ops.enable_tx_laser)
		hw->mac.ops.enable_tx_laser(hw);
}

/**
 *  ixgbe_flap_tx_laser - flap Tx laser to start autotry process
 *  @hw: pointer to hardware structure
 *
 *  When the driver changes the link speeds that it can support then
 *  flap the tx laser to alert the link partner to start autotry
 *  process on its end.
 **/
void ixgbe_flap_tx_laser(struct ixgbe_hw *hw)
{
	if (hw->mac.ops.flap_tx_laser)
		hw->mac.ops.flap_tx_laser(hw);
}

/**
 *  ixgbe_setup_link - Set link speed
 *  @hw: pointer to hardware structure
 *  @speed: new link speed
 *  @autoneg_wait_to_complete: TRUE when waiting for completion is needed
 *
 *  Configures link settings.  Restarts the link.
 *  Performs autonegotiation if needed.
 **/
s32 ixgbe_setup_link(struct ixgbe_hw *hw, ixgbe_link_speed speed,
		     bool autoneg_wait_to_complete)
{
	return ixgbe_call_func(hw, hw->mac.ops.setup_link, (hw, speed,
			       autoneg_wait_to_complete),
			       IXGBE_NOT_IMPLEMENTED);
}

/**
 *  ixgbe_setup_mac_link - Set link speed
 *  @hw: pointer to hardware structure
 *  @speed: new link speed
 *  @autoneg_wait_to_complete: TRUE when waiting for completion is needed
 *
 *  Configures link settings.  Restarts the link.
 *  Performs autonegotiation if needed.
 **/
s32 ixgbe_setup_mac_link(struct ixgbe_hw *hw, ixgbe_link_speed speed,
			 bool autoneg_wait_to_complete)
{
	return ixgbe_call_func(hw, hw->mac.ops.setup_mac_link, (hw, speed,
			       autoneg_wait_to_complete),
			       IXGBE_NOT_IMPLEMENTED);
}

/**
 *  ixgbe_get_link_capabilities - Returns link capabilities
 *  @hw: pointer to hardware structure
 *  @speed: link speed capabilities
 *  @autoneg: TRUE when autoneg or autotry is enabled
 *
 *  Determines the link capabilities of the current configuration.
 **/
s32 ixgbe_get_link_capabilities(struct ixgbe_hw *hw, ixgbe_link_speed *speed,
				bool *autoneg)
{
	return ixgbe_call_func(hw, hw->mac.ops.get_link_capabilities, (hw,
			       speed, autoneg), IXGBE_NOT_IMPLEMENTED);
}

/**
 *  ixgbe_led_on - Turn on LEDs
 *  @hw: pointer to hardware structure
 *  @index: led number to turn on
 *
 *  Turns on the software controllable LEDs.
 **/
s32 ixgbe_led_on(struct ixgbe_hw *hw, u32 index)
{
	return ixgbe_call_func(hw, hw->mac.ops.led_on, (hw, index),
			       IXGBE_NOT_IMPLEMENTED);
}

/**
 *  ixgbe_led_off - Turn off LEDs
 *  @hw: pointer to hardware structure
 *  @index: led number to turn off
 *
 *  Turns off the software controllable LEDs.
 **/
s32 ixgbe_led_off(struct ixgbe_hw *hw, u32 index)
{
	return ixgbe_call_func(hw, hw->mac.ops.led_off, (hw, index),
			       IXGBE_NOT_IMPLEMENTED);
}

/**
 *  ixgbe_blink_led_start - Blink LEDs
 *  @hw: pointer to hardware structure
 *  @index: led number to blink
 *
 *  Blink LED based on index.
 **/
s32 ixgbe_blink_led_start(struct ixgbe_hw *hw, u32 index)
{
	return ixgbe_call_func(hw, hw->mac.ops.blink_led_start, (hw, index),
			       IXGBE_NOT_IMPLEMENTED);
}

/**
 *  ixgbe_blink_led_stop - Stop blinking LEDs
 *  @hw: pointer to hardware structure
 *  @index: led number to stop
 *
 *  Stop blinking LED based on index.
 **/
s32 ixgbe_blink_led_stop(struct ixgbe_hw *hw, u32 index)
{
	return ixgbe_call_func(hw, hw->mac.ops.blink_led_stop, (hw, index),
			       IXGBE_NOT_IMPLEMENTED);
}

/**
 *  ixgbe_init_eeprom_params - Initialize EEPROM parameters
 *  @hw: pointer to hardware structure
 *
 *  Initializes the EEPROM parameters ixgbe_eeprom_info within the
 *  ixgbe_hw struct in order to set up EEPROM access.
 **/
s32 ixgbe_init_eeprom_params(struct ixgbe_hw *hw)
{
	return ixgbe_call_func(hw, hw->eeprom.ops.init_params, (hw),
			       IXGBE_NOT_IMPLEMENTED);
}


/**
 *  ixgbe_write_eeprom - Write word to EEPROM
 *  @hw: pointer to hardware structure
 *  @offset: offset within the EEPROM to be written to
 *  @data: 16 bit word to be written to the EEPROM
 *
 *  Writes 16 bit value to EEPROM. If ixgbe_eeprom_update_checksum is not
 *  called after this function, the EEPROM will most likely contain an
 *  invalid checksum.
 **/
s32 ixgbe_write_eeprom(struct ixgbe_hw *hw, u16 offset, u16 data)
{
	return ixgbe_call_func(hw, hw->eeprom.ops.write, (hw, offset, data),
			       IXGBE_NOT_IMPLEMENTED);
}

/**
 *  ixgbe_write_eeprom_buffer - Write word(s) to EEPROM
 *  @hw: pointer to hardware structure
 *  @offset: offset within the EEPROM to be written to
 *  @data: 16 bit word(s) to be written to the EEPROM
 *  @words: number of words
 *
 *  Writes 16 bit word(s) to EEPROM. If ixgbe_eeprom_update_checksum is not
 *  called after this function, the EEPROM will most likely contain an
 *  invalid checksum.
 **/
s32 ixgbe_write_eeprom_buffer(struct ixgbe_hw *hw, u16 offset, u16 words,
			      u16 *data)
{
	return ixgbe_call_func(hw, hw->eeprom.ops.write_buffer,
			       (hw, offset, words, data),
			       IXGBE_NOT_IMPLEMENTED);
}

/**
 *  ixgbe_read_eeprom - Read word from EEPROM
 *  @hw: pointer to hardware structure
 *  @offset: offset within the EEPROM to be read
 *  @data: read 16 bit value from EEPROM
 *
 *  Reads 16 bit value from EEPROM
 **/
s32 ixgbe_read_eeprom(struct ixgbe_hw *hw, u16 offset, u16 *data)
{
	return ixgbe_call_func(hw, hw->eeprom.ops.read, (hw, offset, data),
			       IXGBE_NOT_IMPLEMENTED);
}

/**
 *  ixgbe_read_eeprom_buffer - Read word(s) from EEPROM
 *  @hw: pointer to hardware structure
 *  @offset: offset within the EEPROM to be read
 *  @data: read 16 bit word(s) from EEPROM
 *  @words: number of words
 *
 *  Reads 16 bit word(s) from EEPROM
 **/
s32 ixgbe_read_eeprom_buffer(struct ixgbe_hw *hw, u16 offset,
			     u16 words, u16 *data)
{
	return ixgbe_call_func(hw, hw->eeprom.ops.read_buffer,
			       (hw, offset, words, data),
			       IXGBE_NOT_IMPLEMENTED);
}

/**
 *  ixgbe_validate_eeprom_checksum - Validate EEPROM checksum
 *  @hw: pointer to hardware structure
 *  @checksum_val: calculated checksum
 *
 *  Performs checksum calculation and validates the EEPROM checksum
 **/
s32 ixgbe_validate_eeprom_checksum(struct ixgbe_hw *hw, u16 *checksum_val)
{
	return ixgbe_call_func(hw, hw->eeprom.ops.validate_checksum,
			       (hw, checksum_val), IXGBE_NOT_IMPLEMENTED);
}

/**
 *  ixgbe_eeprom_update_checksum - Updates the EEPROM checksum
 *  @hw: pointer to hardware structure
 **/
s32 ixgbe_update_eeprom_checksum(struct ixgbe_hw *hw)
{
	return ixgbe_call_func(hw, hw->eeprom.ops.update_checksum, (hw),
			       IXGBE_NOT_IMPLEMENTED);
}

/**
 *  ixgbe_insert_mac_addr - Find a RAR for this mac address
 *  @hw: pointer to hardware structure
 *  @addr: Address to put into receive address register
 *  @vmdq: VMDq pool to assign
 *
 *  Puts an ethernet address into a receive address register, or
 *  finds the rar that it is already in; adds to the pool list
 **/
s32 ixgbe_insert_mac_addr(struct ixgbe_hw *hw, u8 *addr, u32 vmdq)
{
	return ixgbe_call_func(hw, hw->mac.ops.insert_mac_addr,
			       (hw, addr, vmdq),
			       IXGBE_NOT_IMPLEMENTED);
}

/**
 *  ixgbe_set_rar - Set Rx address register
 *  @hw: pointer to hardware structure
 *  @index: Receive address register to write
 *  @addr: Address to put into receive address register
 *  @vmdq: VMDq "set"
 *  @enable_addr: set flag that address is active
 *
 *  Puts an ethernet address into a receive address register.
 **/
s32 ixgbe_set_rar(struct ixgbe_hw *hw, u32 index, u8 *addr, u32 vmdq,
		  u32 enable_addr)
{
	return ixgbe_call_func(hw, hw->mac.ops.set_rar, (hw, index, addr, vmdq,
			       enable_addr), IXGBE_NOT_IMPLEMENTED);
}

/**
 *  ixgbe_clear_rar - Clear Rx address register
 *  @hw: pointer to hardware structure
 *  @index: Receive address register to write
 *
 *  Puts an ethernet address into a receive address register.
 **/
s32 ixgbe_clear_rar(struct ixgbe_hw *hw, u32 index)
{
	return ixgbe_call_func(hw, hw->mac.ops.clear_rar, (hw, index),
			       IXGBE_NOT_IMPLEMENTED);
}

/**
 *  ixgbe_set_vmdq - Associate a VMDq index with a receive address
 *  @hw: pointer to hardware structure
 *  @rar: receive address register index to associate with VMDq index
 *  @vmdq: VMDq set or pool index
 **/
s32 ixgbe_set_vmdq(struct ixgbe_hw *hw, u32 rar, u32 vmdq)
{
	return ixgbe_call_func(hw, hw->mac.ops.set_vmdq, (hw, rar, vmdq),
			       IXGBE_NOT_IMPLEMENTED);

}

/**
 *  ixgbe_set_vmdq_san_mac - Associate VMDq index 127 with a receive address
 *  @hw: pointer to hardware structure
 *  @vmdq: VMDq default pool index
 **/
s32 ixgbe_set_vmdq_san_mac(struct ixgbe_hw *hw, u32 vmdq)
{
	return ixgbe_call_func(hw, hw->mac.ops.set_vmdq_san_mac,
			       (hw, vmdq), IXGBE_NOT_IMPLEMENTED);
}

/**
 *  ixgbe_clear_vmdq - Disassociate a VMDq index from a receive address
 *  @hw: pointer to hardware structure
 *  @rar: receive address register index to disassociate with VMDq index
 *  @vmdq: VMDq set or pool index
 **/
s32 ixgbe_clear_vmdq(struct ixgbe_hw *hw, u32 rar, u32 vmdq)
{
	return ixgbe_call_func(hw, hw->mac.ops.clear_vmdq, (hw, rar, vmdq),
			       IXGBE_NOT_IMPLEMENTED);
}

/**
 *  ixgbe_init_rx_addrs - Initializes receive address filters.
 *  @hw: pointer to hardware structure
 *
 *  Places the MAC address in receive address register 0 and clears the rest
 *  of the receive address registers. Clears the multicast table. Assumes
 *  the receiver is in reset when the routine is called.
 **/
s32 ixgbe_init_rx_addrs(struct ixgbe_hw *hw)
{
	return ixgbe_call_func(hw, hw->mac.ops.init_rx_addrs, (hw),
			       IXGBE_NOT_IMPLEMENTED);
}

/**
 *  ixgbe_get_num_rx_addrs - Returns the number of RAR entries.
 *  @hw: pointer to hardware structure
 **/
u32 ixgbe_get_num_rx_addrs(struct ixgbe_hw *hw)
{
	return hw->mac.num_rar_entries;
}

/**
 *  ixgbe_update_uc_addr_list - Updates the MAC's list of secondary addresses
 *  @hw: pointer to hardware structure
 *  @addr_list: the list of new multicast addresses
 *  @addr_count: number of addresses
 *  @func: iterator function to walk the multicast address list
 *
 *  The given list replaces any existing list. Clears the secondary addrs from
 *  receive address registers. Uses unused receive address registers for the
 *  first secondary addresses, and falls back to promiscuous mode as needed.
 **/
s32 ixgbe_update_uc_addr_list(struct ixgbe_hw *hw, u8 *addr_list,
			      u32 addr_count, ixgbe_mc_addr_itr func)
{
	return ixgbe_call_func(hw, hw->mac.ops.update_uc_addr_list, (hw,
			       addr_list, addr_count, func),
			       IXGBE_NOT_IMPLEMENTED);
}

/**
 *  ixgbe_update_mc_addr_list - Updates the MAC's list of multicast addresses
 *  @hw: pointer to hardware structure
 *  @mc_addr_list: the list of new multicast addresses
 *  @mc_addr_count: number of addresses
 *  @func: iterator function to walk the multicast address list
 *  @clear: flag, when set clears the table beforehand
 *
 *  The given list replaces any existing list. Clears the MC addrs from receive
 *  address registers and the multicast table. Uses unused receive address
 *  registers for the first multicast addresses, and hashes the rest into the
 *  multicast table.
 **/
s32 ixgbe_update_mc_addr_list(struct ixgbe_hw *hw, u8 *mc_addr_list,
			      u32 mc_addr_count, ixgbe_mc_addr_itr func,
			      bool clear)
{
	return ixgbe_call_func(hw, hw->mac.ops.update_mc_addr_list, (hw,
			       mc_addr_list, mc_addr_count, func, clear),
			       IXGBE_NOT_IMPLEMENTED);
}

/**
 *  ixgbe_enable_mc - Enable multicast address in RAR
 *  @hw: pointer to hardware structure
 *
 *  Enables multicast address in RAR and the use of the multicast hash table.
 **/
s32 ixgbe_enable_mc(struct ixgbe_hw *hw)
{
	return ixgbe_call_func(hw, hw->mac.ops.enable_mc, (hw),
			       IXGBE_NOT_IMPLEMENTED);
}

/**
 *  ixgbe_disable_mc - Disable multicast address in RAR
 *  @hw: pointer to hardware structure
 *
 *  Disables multicast address in RAR and the use of the multicast hash table.
 **/
s32 ixgbe_disable_mc(struct ixgbe_hw *hw)
{
	return ixgbe_call_func(hw, hw->mac.ops.disable_mc, (hw),
			       IXGBE_NOT_IMPLEMENTED);
}

/**
 *  ixgbe_clear_vfta - Clear VLAN filter table
 *  @hw: pointer to hardware structure
 *
 *  Clears the VLAN filer table, and the VMDq index associated with the filter
 **/
s32 ixgbe_clear_vfta(struct ixgbe_hw *hw)
{
	return ixgbe_call_func(hw, hw->mac.ops.clear_vfta, (hw),
			       IXGBE_NOT_IMPLEMENTED);
}

/**
 *  ixgbe_set_vfta - Set VLAN filter table
 *  @hw: pointer to hardware structure
 *  @vlan: VLAN id to write to VLAN filter
 *  @vind: VMDq output index that maps queue to VLAN id in VLVFB
 *  @vlan_on: boolean flag to turn on/off VLAN
 *  @vlvf_bypass: boolean flag indicating updating the default pool is okay
 *
 *  Turn on/off specified VLAN in the VLAN filter table.
 **/
s32 ixgbe_set_vfta(struct ixgbe_hw *hw, u32 vlan, u32 vind, bool vlan_on,
		   bool vlvf_bypass)
{
	return ixgbe_call_func(hw, hw->mac.ops.set_vfta, (hw, vlan, vind,
			       vlan_on, vlvf_bypass), IXGBE_NOT_IMPLEMENTED);
}

/**
 *  ixgbe_set_vlvf - Set VLAN Pool Filter
 *  @hw: pointer to hardware structure
 *  @vlan: VLAN id to write to VLAN filter
 *  @vind: VMDq output index that maps queue to VLAN id in VLVFB
 *  @vlan_on: boolean flag to turn on/off VLAN in VLVF
 *  @vfta_delta: pointer to the difference between the current value of VFTA
 *		 and the desired value
 *  @vfta: the desired value of the VFTA
 *  @vlvf_bypass: boolean flag indicating updating the default pool is okay
 *
 *  Turn on/off specified bit in VLVF table.
 **/
s32 ixgbe_set_vlvf(struct ixgbe_hw *hw, u32 vlan, u32 vind, bool vlan_on,
		   u32 *vfta_delta, u32 vfta, bool vlvf_bypass)
{
	return ixgbe_call_func(hw, hw->mac.ops.set_vlvf, (hw, vlan, vind,
			       vlan_on, vfta_delta, vfta, vlvf_bypass),
			       IXGBE_NOT_IMPLEMENTED);
}

/**
 *  ixgbe_fc_enable - Enable flow control
 *  @hw: pointer to hardware structure
 *
 *  Configures the flow control settings based on SW configuration.
 **/
s32 ixgbe_fc_enable(struct ixgbe_hw *hw)
{
	return ixgbe_call_func(hw, hw->mac.ops.fc_enable, (hw),
			       IXGBE_NOT_IMPLEMENTED);
}

/**
 *  ixgbe_setup_fc - Set up flow control
 *  @hw: pointer to hardware structure
 *
 *  Called at init time to set up flow control.
 **/
s32 ixgbe_setup_fc(struct ixgbe_hw *hw)
{
	return ixgbe_call_func(hw, hw->mac.ops.setup_fc, (hw),
		IXGBE_NOT_IMPLEMENTED);
}

/**
 * ixgbe_set_fw_drv_ver - Try to send the driver version number FW
 * @hw: pointer to hardware structure
 * @maj: driver major number to be sent to firmware
 * @min: driver minor number to be sent to firmware
 * @build: driver build number to be sent to firmware
 * @ver: driver version number to be sent to firmware
 * @len: length of driver_ver string
 * @driver_ver: driver string
 **/
s32 ixgbe_set_fw_drv_ver(struct ixgbe_hw *hw, u8 maj, u8 min, u8 build,
			 u8 ver, u16 len, char *driver_ver)
{
	return ixgbe_call_func(hw, hw->mac.ops.set_fw_drv_ver, (hw, maj, min,
			       build, ver, len, driver_ver),
			       IXGBE_NOT_IMPLEMENTED);
}



/**
 *  ixgbe_dmac_config - Configure DMA Coalescing registers.
 *  @hw: pointer to hardware structure
 *
 *  Configure DMA coalescing. If enabling dmac, dmac is activated.
 *  When disabling dmac, dmac enable dmac bit is cleared.
 **/
s32 ixgbe_dmac_config(struct ixgbe_hw *hw)
{
	return ixgbe_call_func(hw, hw->mac.ops.dmac_config, (hw),
				IXGBE_NOT_IMPLEMENTED);
}

/**
 *  ixgbe_dmac_update_tcs - Configure DMA Coalescing registers.
 *  @hw: pointer to hardware structure
 *
 *  Disables dmac, updates per TC settings, and then enable dmac.
 **/
s32 ixgbe_dmac_update_tcs(struct ixgbe_hw *hw)
{
	return ixgbe_call_func(hw, hw->mac.ops.dmac_update_tcs, (hw),
				IXGBE_NOT_IMPLEMENTED);
}

/**
 *  ixgbe_dmac_config_tcs - Configure DMA Coalescing registers.
 *  @hw: pointer to hardware structure
 *
 *  Configure DMA coalescing threshold per TC and set high priority bit for
 *  FCOE TC. The dmac enable bit must be cleared before configuring.
 **/
s32 ixgbe_dmac_config_tcs(struct ixgbe_hw *hw)
{
	return ixgbe_call_func(hw, hw->mac.ops.dmac_config_tcs, (hw),
				IXGBE_NOT_IMPLEMENTED);
}

/**
 *  ixgbe_setup_eee - Enable/disable EEE support
 *  @hw: pointer to the HW structure
 *  @enable_eee: boolean flag to enable EEE
 *
 *  Enable/disable EEE based on enable_ee flag.
 *  Auto-negotiation must be started after BASE-T EEE bits in PHY register 7.3C
 *  are modified.
 *
 **/
s32 ixgbe_setup_eee(struct ixgbe_hw *hw, bool enable_eee)
{
	return ixgbe_call_func(hw, hw->mac.ops.setup_eee, (hw, enable_eee),
			IXGBE_NOT_IMPLEMENTED);
}

/**
 * ixgbe_set_source_address_pruning - Enable/Disable source address pruning
 * @hw: pointer to hardware structure
 * @enable: enable or disable source address pruning
 * @pool: Rx pool - Rx pool to toggle source address pruning
 **/
void ixgbe_set_source_address_pruning(struct ixgbe_hw *hw, bool enable,
				      unsigned int pool)
{
	if (hw->mac.ops.set_source_address_pruning)
		hw->mac.ops.set_source_address_pruning(hw, enable, pool);
}

/**
 *  ixgbe_set_ethertype_anti_spoofing - Enable/Disable Ethertype anti-spoofing
 *  @hw: pointer to hardware structure
 *  @enable: enable or disable switch for Ethertype anti-spoofing
 *  @vf: Virtual Function pool - VF Pool to set for Ethertype anti-spoofing
 *
 **/
void ixgbe_set_ethertype_anti_spoofing(struct ixgbe_hw *hw, bool enable, int vf)
{
	if (hw->mac.ops.set_ethertype_anti_spoofing)
		hw->mac.ops.set_ethertype_anti_spoofing(hw, enable, vf);
}

/**
 *  ixgbe_read_iosf_sb_reg - Read 32 bit PHY register
 *  @hw: pointer to hardware structure
 *  @reg_addr: 32 bit address of PHY register to read
 *  @device_type: type of device you want to communicate with
 *  @phy_data: Pointer to read data from PHY register
 *
 *  Reads a value from a specified PHY register
 **/
s32 ixgbe_read_iosf_sb_reg(struct ixgbe_hw *hw, u32 reg_addr,
			   u32 device_type, u32 *phy_data)
{
	return ixgbe_call_func(hw, hw->mac.ops.read_iosf_sb_reg, (hw, reg_addr,
			       device_type, phy_data), IXGBE_NOT_IMPLEMENTED);
}

/**
 *  ixgbe_write_iosf_sb_reg - Write 32 bit register through IOSF Sideband
 *  @hw: pointer to hardware structure
 *  @reg_addr: 32 bit PHY register to write
 *  @device_type: type of device you want to communicate with
 *  @phy_data: Data to write to the PHY register
 *
 *  Writes a value to specified PHY register
 **/
s32 ixgbe_write_iosf_sb_reg(struct ixgbe_hw *hw, u32 reg_addr,
			    u32 device_type, u32 phy_data)
{
	return ixgbe_call_func(hw, hw->mac.ops.write_iosf_sb_reg, (hw, reg_addr,
			       device_type, phy_data), IXGBE_NOT_IMPLEMENTED);
}

/**
 *  ixgbe_disable_mdd - Disable malicious driver detection
 *  @hw: pointer to hardware structure
 *
 **/
void ixgbe_disable_mdd(struct ixgbe_hw *hw)
{
	if (hw->mac.ops.disable_mdd)
		hw->mac.ops.disable_mdd(hw);
}

/**
 *  ixgbe_enable_mdd - Enable malicious driver detection
 *  @hw: pointer to hardware structure
 *
 **/
void ixgbe_enable_mdd(struct ixgbe_hw *hw)
{
	if (hw->mac.ops.enable_mdd)
		hw->mac.ops.enable_mdd(hw);
}

/**
 *  ixgbe_mdd_event - Handle malicious driver detection event
 *  @hw: pointer to hardware structure
 *  @vf_bitmap: vf bitmap of malicious vfs
 *
 **/
void ixgbe_mdd_event(struct ixgbe_hw *hw, u32 *vf_bitmap)
{
	if (hw->mac.ops.mdd_event)
		hw->mac.ops.mdd_event(hw, vf_bitmap);
}

/**
 *  ixgbe_restore_mdd_vf - Restore VF that was disabled during malicious driver
 *  detection event
 *  @hw: pointer to hardware structure
 *  @vf: vf index
 *
 **/
void ixgbe_restore_mdd_vf(struct ixgbe_hw *hw, u32 vf)
{
	if (hw->mac.ops.restore_mdd_vf)
		hw->mac.ops.restore_mdd_vf(hw, vf);
}

/**
 *  ixgbe_enter_lplu - Transition to low power states
 *  @hw: pointer to hardware structure
 *
 * Configures Low Power Link Up on transition to low power states
 * (from D0 to non-D0).
 **/
s32 ixgbe_enter_lplu(struct ixgbe_hw *hw)
{
	return ixgbe_call_func(hw, hw->phy.ops.enter_lplu, (hw),
				IXGBE_NOT_IMPLEMENTED);
}

/**
 * ixgbe_handle_lasi - Handle external Base T PHY interrupt
 * @hw: pointer to hardware structure
 *
 * Handle external Base T PHY interrupt. If high temperature
 * failure alarm then return error, else if link status change
 * then setup internal/external PHY link
 *
 * Return IXGBE_ERR_OVERTEMP if interrupt is high temperature
 * failure alarm, else return PHY access status.
 */
s32 ixgbe_handle_lasi(struct ixgbe_hw *hw)
{
	return ixgbe_call_func(hw, hw->phy.ops.handle_lasi, (hw),
				IXGBE_NOT_IMPLEMENTED);
}

/**
 *  ixgbe_bypass_rw - Bit bang data into by_pass FW
 *  @hw: pointer to hardware structure
 *  @cmd: Command we send to the FW
 *  @status: The reply from the FW
 *
 *  Bit-bangs the cmd to the by_pass FW status points to what is returned.
 **/
s32 ixgbe_bypass_rw(struct ixgbe_hw *hw, u32 cmd, u32 *status)
{
	return ixgbe_call_func(hw, hw->mac.ops.bypass_rw, (hw, cmd, status),
				IXGBE_NOT_IMPLEMENTED);
}

/**
 * ixgbe_bypass_valid_rd - Verify valid return from bit-bang.
 *
 * If we send a write we can't be sure it took until we can read back
 * that same register.  It can be a problem as some of the feilds may
 * for valid reasons change inbetween the time wrote the register and
 * we read it again to verify.  So this function check everything we
 * can check and then assumes it worked.
 *
 * @u32 in_reg - The register cmd for the bit-bang read.
 * @u32 out_reg - The register returned from a bit-bang read.
 **/
bool ixgbe_bypass_valid_rd(struct ixgbe_hw *hw, u32 in_reg, u32 out_reg)
{
	return ixgbe_call_func(hw, hw->mac.ops.bypass_valid_rd,
			       (in_reg, out_reg), IXGBE_NOT_IMPLEMENTED);
}

/**
 *  ixgbe_bypass_set - Set a bypass field in the FW CTRL Regiter.
 *  @hw: pointer to hardware structure
 *  @cmd: The control word we are setting.
 *  @event: The event we are setting in the FW.  This also happens to
 *          be the mask for the event we are setting (handy)
 *  @action: The action we set the event to in the FW. This is in a
 *           bit field that happens to be what we want to put in
 *           the event spot (also handy)
 *
 *  Writes to the cmd control the bits in actions.
 **/
s32 ixgbe_bypass_set(struct ixgbe_hw *hw, u32 cmd, u32 event, u32 action)
{
	return ixgbe_call_func(hw, hw->mac.ops.bypass_set,
			       (hw, cmd, event, action),
				IXGBE_NOT_IMPLEMENTED);
}

/**
 *  ixgbe_bypass_rd_eep - Read the bypass FW eeprom address
 *  @hw: pointer to hardware structure
 *  @addr: The bypass eeprom address to read.
 *  @value: The 8b of data at the address above.
 **/
s32 ixgbe_bypass_rd_eep(struct ixgbe_hw *hw, u32 addr, u8 *value)
{
	return ixgbe_call_func(hw, hw->mac.ops.bypass_rd_eep,
			       (hw, addr, value), IXGBE_NOT_IMPLEMENTED);
}

/**
 *  ixgbe_read_analog_reg8 - Reads 8 bit analog register
 *  @hw: pointer to hardware structure
 *  @reg: analog register to read
 *  @val: read value
 *
 *  Performs write operation to analog register specified.
 **/
s32 ixgbe_read_analog_reg8(struct ixgbe_hw *hw, u32 reg, u8 *val)
{
	return ixgbe_call_func(hw, hw->mac.ops.read_analog_reg8, (hw, reg,
			       val), IXGBE_NOT_IMPLEMENTED);
}

/**
 *  ixgbe_write_analog_reg8 - Writes 8 bit analog register
 *  @hw: pointer to hardware structure
 *  @reg: analog register to write
 *  @val: value to write
 *
 *  Performs write operation to Atlas analog register specified.
 **/
s32 ixgbe_write_analog_reg8(struct ixgbe_hw *hw, u32 reg, u8 val)
{
	return ixgbe_call_func(hw, hw->mac.ops.write_analog_reg8, (hw, reg,
			       val), IXGBE_NOT_IMPLEMENTED);
}

/**
 *  ixgbe_init_uta_tables - Initializes Unicast Table Arrays.
 *  @hw: pointer to hardware structure
 *
 *  Initializes the Unicast Table Arrays to zero on device load.  This
 *  is part of the Rx init addr execution path.
 **/
s32 ixgbe_init_uta_tables(struct ixgbe_hw *hw)
{
	return ixgbe_call_func(hw, hw->mac.ops.init_uta_tables, (hw),
			       IXGBE_NOT_IMPLEMENTED);
}

/**
 *  ixgbe_read_i2c_byte - Reads 8 bit word over I2C at specified device address
 *  @hw: pointer to hardware structure
 *  @byte_offset: byte offset to read
 *  @dev_addr: I2C bus address to read from
 *  @data: value read
 *
 *  Performs byte read operation to SFP module's EEPROM over I2C interface.
 **/
s32 ixgbe_read_i2c_byte(struct ixgbe_hw *hw, u8 byte_offset, u8 dev_addr,
			u8 *data)
{
	return ixgbe_call_func(hw, hw->phy.ops.read_i2c_byte, (hw, byte_offset,
			       dev_addr, data), IXGBE_NOT_IMPLEMENTED);
}

/**
 *  ixgbe_read_i2c_byte_unlocked - Reads 8 bit word via I2C from device address
 *  @hw: pointer to hardware structure
 *  @byte_offset: byte offset to read
 *  @dev_addr: I2C bus address to read from
 *  @data: value read
 *
 *  Performs byte read operation to SFP module's EEPROM over I2C interface.
 **/
s32 ixgbe_read_i2c_byte_unlocked(struct ixgbe_hw *hw, u8 byte_offset,
				 u8 dev_addr, u8 *data)
{
	return ixgbe_call_func(hw, hw->phy.ops.read_i2c_byte_unlocked,
			       (hw, byte_offset, dev_addr, data),
			       IXGBE_NOT_IMPLEMENTED);
}

/**
 * ixgbe_read_link - Perform read operation on link device
 * @hw: pointer to the hardware structure
 * @addr: bus address to read from
 * @reg: device register to read from
 * @val: pointer to location to receive read value
 *
 * Returns an error code on error.
 */
s32 ixgbe_read_link(struct ixgbe_hw *hw, u8 addr, u16 reg, u16 *val)
{
	return ixgbe_call_func(hw, hw->link.ops.read_link, (hw, addr,
			       reg, val), IXGBE_NOT_IMPLEMENTED);
}

/**
 * ixgbe_read_link_unlocked - Perform read operation on link device
 * @hw: pointer to the hardware structure
 * @addr: bus address to read from
 * @reg: device register to read from
 * @val: pointer to location to receive read value
 *
 * Returns an error code on error.
 **/
s32 ixgbe_read_link_unlocked(struct ixgbe_hw *hw, u8 addr, u16 reg, u16 *val)
{
	return ixgbe_call_func(hw, hw->link.ops.read_link_unlocked,
			       (hw, addr, reg, val), IXGBE_NOT_IMPLEMENTED);
}

/**
 *  ixgbe_write_i2c_byte - Writes 8 bit word over I2C
 *  @hw: pointer to hardware structure
 *  @byte_offset: byte offset to write
 *  @dev_addr: I2C bus address to write to
 *  @data: value to write
 *
 *  Performs byte write operation to SFP module's EEPROM over I2C interface
 *  at a specified device address.
 **/
s32 ixgbe_write_i2c_byte(struct ixgbe_hw *hw, u8 byte_offset, u8 dev_addr,
			 u8 data)
{
	return ixgbe_call_func(hw, hw->phy.ops.write_i2c_byte, (hw, byte_offset,
			       dev_addr, data), IXGBE_NOT_IMPLEMENTED);
}

/**
 *  ixgbe_write_i2c_byte_unlocked - Writes 8 bit word over I2C
 *  @hw: pointer to hardware structure
 *  @byte_offset: byte offset to write
 *  @dev_addr: I2C bus address to write to
 *  @data: value to write
 *
 *  Performs byte write operation to SFP module's EEPROM over I2C interface
 *  at a specified device address.
 **/
s32 ixgbe_write_i2c_byte_unlocked(struct ixgbe_hw *hw, u8 byte_offset,
				  u8 dev_addr, u8 data)
{
	return ixgbe_call_func(hw, hw->phy.ops.write_i2c_byte_unlocked,
			       (hw, byte_offset, dev_addr, data),
			       IXGBE_NOT_IMPLEMENTED);
}

/**
 * ixgbe_write_link - Perform write operation on link device
 * @hw: pointer to the hardware structure
 * @addr: bus address to write to
 * @reg: device register to write to
 * @val: value to write
 *
 * Returns an error code on error.
 */
s32 ixgbe_write_link(struct ixgbe_hw *hw, u8 addr, u16 reg, u16 val)
{
	return ixgbe_call_func(hw, hw->link.ops.write_link,
			       (hw, addr, reg, val), IXGBE_NOT_IMPLEMENTED);
}

/**
 * ixgbe_write_link_unlocked - Perform write operation on link device
 * @hw: pointer to the hardware structure
 * @addr: bus address to write to
 * @reg: device register to write to
 * @val: value to write
 *
 * Returns an error code on error.
 **/
s32 ixgbe_write_link_unlocked(struct ixgbe_hw *hw, u8 addr, u16 reg, u16 val)
{
	return ixgbe_call_func(hw, hw->link.ops.write_link_unlocked,
			       (hw, addr, reg, val), IXGBE_NOT_IMPLEMENTED);
}

/**
 *  ixgbe_write_i2c_eeprom - Writes 8 bit EEPROM word over I2C interface
 *  @hw: pointer to hardware structure
 *  @byte_offset: EEPROM byte offset to write
 *  @eeprom_data: value to write
 *
 *  Performs byte write operation to SFP module's EEPROM over I2C interface.
 **/
s32 ixgbe_write_i2c_eeprom(struct ixgbe_hw *hw,
			   u8 byte_offset, u8 eeprom_data)
{
	return ixgbe_call_func(hw, hw->phy.ops.write_i2c_eeprom,
			       (hw, byte_offset, eeprom_data),
			       IXGBE_NOT_IMPLEMENTED);
}

/**
 *  ixgbe_read_i2c_eeprom - Reads 8 bit EEPROM word over I2C interface
 *  @hw: pointer to hardware structure
 *  @byte_offset: EEPROM byte offset to read
 *  @eeprom_data: value read
 *
 *  Performs byte read operation to SFP module's EEPROM over I2C interface.
 **/
s32 ixgbe_read_i2c_eeprom(struct ixgbe_hw *hw, u8 byte_offset, u8 *eeprom_data)
{
	return ixgbe_call_func(hw, hw->phy.ops.read_i2c_eeprom,
			      (hw, byte_offset, eeprom_data),
			      IXGBE_NOT_IMPLEMENTED);
}

/**
 *  ixgbe_get_supported_physical_layer - Returns physical layer type
 *  @hw: pointer to hardware structure
 *
 *  Determines physical layer capabilities of the current configuration.
 **/
u64 ixgbe_get_supported_physical_layer(struct ixgbe_hw *hw)
{
	return ixgbe_call_func(hw, hw->mac.ops.get_supported_physical_layer,
			       (hw), IXGBE_PHYSICAL_LAYER_UNKNOWN);
}

/**
 *  ixgbe_enable_rx_dma - Enables Rx DMA unit, dependent on device specifics
 *  @hw: pointer to hardware structure
 *  @regval: bitfield to write to the Rx DMA register
 *
 *  Enables the Rx DMA unit of the device.
 **/
s32 ixgbe_enable_rx_dma(struct ixgbe_hw *hw, u32 regval)
{
	return ixgbe_call_func(hw, hw->mac.ops.enable_rx_dma,
			       (hw, regval), IXGBE_NOT_IMPLEMENTED);
}

/**
 *  ixgbe_disable_sec_rx_path - Stops the receive data path
 *  @hw: pointer to hardware structure
 *
 *  Stops the receive data path.
 **/
s32 ixgbe_disable_sec_rx_path(struct ixgbe_hw *hw)
{
	return ixgbe_call_func(hw, hw->mac.ops.disable_sec_rx_path,
				(hw), IXGBE_NOT_IMPLEMENTED);
}

/**
 *  ixgbe_enable_sec_rx_path - Enables the receive data path
 *  @hw: pointer to hardware structure
 *
 *  Enables the receive data path.
 **/
s32 ixgbe_enable_sec_rx_path(struct ixgbe_hw *hw)
{
	return ixgbe_call_func(hw, hw->mac.ops.enable_sec_rx_path,
				(hw), IXGBE_NOT_IMPLEMENTED);
}

/**
 *  ixgbe_acquire_swfw_semaphore - Acquire SWFW semaphore
 *  @hw: pointer to hardware structure
 *  @mask: Mask to specify which semaphore to acquire
 *
 *  Acquires the SWFW semaphore through SW_FW_SYNC register for the specified
 *  function (CSR, PHY0, PHY1, EEPROM, Flash)
 **/
s32 ixgbe_acquire_swfw_semaphore(struct ixgbe_hw *hw, u32 mask)
{
	return ixgbe_call_func(hw, hw->mac.ops.acquire_swfw_sync,
			       (hw, mask), IXGBE_NOT_IMPLEMENTED);
}

/**
 *  ixgbe_release_swfw_semaphore - Release SWFW semaphore
 *  @hw: pointer to hardware structure
 *  @mask: Mask to specify which semaphore to release
 *
 *  Releases the SWFW semaphore through SW_FW_SYNC register for the specified
 *  function (CSR, PHY0, PHY1, EEPROM, Flash)
 **/
void ixgbe_release_swfw_semaphore(struct ixgbe_hw *hw, u32 mask)
{
	if (hw->mac.ops.release_swfw_sync)
		hw->mac.ops.release_swfw_sync(hw, mask);
}

/**
 *  ixgbe_init_swfw_semaphore - Clean up SWFW semaphore
 *  @hw: pointer to hardware structure
 *
 *  Attempts to acquire the SWFW semaphore through SW_FW_SYNC register.
 *  Regardless of whether is succeeds or not it then release the semaphore.
 *  This is function is called to recover from catastrophic failures that
 *  may have left the semaphore locked.
 **/
void ixgbe_init_swfw_semaphore(struct ixgbe_hw *hw)
{
	if (hw->mac.ops.init_swfw_sync)
		hw->mac.ops.init_swfw_sync(hw);
}


void ixgbe_disable_rx(struct ixgbe_hw *hw)
{
	if (hw->mac.ops.disable_rx)
		hw->mac.ops.disable_rx(hw);
}

void ixgbe_enable_rx(struct ixgbe_hw *hw)
{
	if (hw->mac.ops.enable_rx)
		hw->mac.ops.enable_rx(hw);
}

/**
 *  ixgbe_set_rate_select_speed - Set module link speed
 *  @hw: pointer to hardware structure
 *  @speed: link speed to set
 *
 *  Set module link speed via the rate select.
 */
void ixgbe_set_rate_select_speed(struct ixgbe_hw *hw, ixgbe_link_speed speed)
{
	if (hw->mac.ops.set_rate_select_speed)
		hw->mac.ops.set_rate_select_speed(hw, speed);
}
