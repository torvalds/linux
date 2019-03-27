/******************************************************************************
  SPDX-License-Identifier: BSD-3-Clause

  Copyright (c) 2001-2015, Intel Corporation 
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

/* 82571EB Gigabit Ethernet Controller
 * 82571EB Gigabit Ethernet Controller (Copper)
 * 82571EB Gigabit Ethernet Controller (Fiber)
 * 82571EB Dual Port Gigabit Mezzanine Adapter
 * 82571EB Quad Port Gigabit Mezzanine Adapter
 * 82571PT Gigabit PT Quad Port Server ExpressModule
 * 82572EI Gigabit Ethernet Controller (Copper)
 * 82572EI Gigabit Ethernet Controller (Fiber)
 * 82572EI Gigabit Ethernet Controller
 * 82573V Gigabit Ethernet Controller (Copper)
 * 82573E Gigabit Ethernet Controller (Copper)
 * 82573L Gigabit Ethernet Controller
 * 82574L Gigabit Network Connection
 * 82583V Gigabit Network Connection
 */

#include "e1000_api.h"

static s32  e1000_acquire_nvm_82571(struct e1000_hw *hw);
static void e1000_release_nvm_82571(struct e1000_hw *hw);
static s32  e1000_write_nvm_82571(struct e1000_hw *hw, u16 offset,
				  u16 words, u16 *data);
static s32  e1000_update_nvm_checksum_82571(struct e1000_hw *hw);
static s32  e1000_validate_nvm_checksum_82571(struct e1000_hw *hw);
static s32  e1000_get_cfg_done_82571(struct e1000_hw *hw);
static s32  e1000_set_d0_lplu_state_82571(struct e1000_hw *hw,
					  bool active);
static s32  e1000_reset_hw_82571(struct e1000_hw *hw);
static s32  e1000_init_hw_82571(struct e1000_hw *hw);
static void e1000_clear_vfta_82571(struct e1000_hw *hw);
static bool e1000_check_mng_mode_82574(struct e1000_hw *hw);
static s32 e1000_led_on_82574(struct e1000_hw *hw);
static s32  e1000_setup_link_82571(struct e1000_hw *hw);
static s32  e1000_setup_copper_link_82571(struct e1000_hw *hw);
static s32  e1000_check_for_serdes_link_82571(struct e1000_hw *hw);
static s32  e1000_setup_fiber_serdes_link_82571(struct e1000_hw *hw);
static s32  e1000_valid_led_default_82571(struct e1000_hw *hw, u16 *data);
static void e1000_clear_hw_cntrs_82571(struct e1000_hw *hw);
static s32  e1000_fix_nvm_checksum_82571(struct e1000_hw *hw);
static s32  e1000_get_phy_id_82571(struct e1000_hw *hw);
static s32  e1000_get_hw_semaphore_82574(struct e1000_hw *hw);
static void e1000_put_hw_semaphore_82574(struct e1000_hw *hw);
static s32  e1000_set_d0_lplu_state_82574(struct e1000_hw *hw,
					  bool active);
static s32  e1000_set_d3_lplu_state_82574(struct e1000_hw *hw,
					  bool active);
static void e1000_initialize_hw_bits_82571(struct e1000_hw *hw);
static s32  e1000_write_nvm_eewr_82571(struct e1000_hw *hw, u16 offset,
				       u16 words, u16 *data);
static s32  e1000_read_mac_addr_82571(struct e1000_hw *hw);
static void e1000_power_down_phy_copper_82571(struct e1000_hw *hw);

/**
 *  e1000_init_phy_params_82571 - Init PHY func ptrs.
 *  @hw: pointer to the HW structure
 **/
static s32 e1000_init_phy_params_82571(struct e1000_hw *hw)
{
	struct e1000_phy_info *phy = &hw->phy;
	s32 ret_val;

	DEBUGFUNC("e1000_init_phy_params_82571");

	if (hw->phy.media_type != e1000_media_type_copper) {
		phy->type = e1000_phy_none;
		return E1000_SUCCESS;
	}

	phy->addr			= 1;
	phy->autoneg_mask		= AUTONEG_ADVERTISE_SPEED_DEFAULT;
	phy->reset_delay_us		= 100;

	phy->ops.check_reset_block	= e1000_check_reset_block_generic;
	phy->ops.reset			= e1000_phy_hw_reset_generic;
	phy->ops.set_d0_lplu_state	= e1000_set_d0_lplu_state_82571;
	phy->ops.set_d3_lplu_state	= e1000_set_d3_lplu_state_generic;
	phy->ops.power_up		= e1000_power_up_phy_copper;
	phy->ops.power_down		= e1000_power_down_phy_copper_82571;

	switch (hw->mac.type) {
	case e1000_82571:
	case e1000_82572:
		phy->type		= e1000_phy_igp_2;
		phy->ops.get_cfg_done	= e1000_get_cfg_done_82571;
		phy->ops.get_info	= e1000_get_phy_info_igp;
		phy->ops.check_polarity	= e1000_check_polarity_igp;
		phy->ops.force_speed_duplex = e1000_phy_force_speed_duplex_igp;
		phy->ops.get_cable_length = e1000_get_cable_length_igp_2;
		phy->ops.read_reg	= e1000_read_phy_reg_igp;
		phy->ops.write_reg	= e1000_write_phy_reg_igp;
		phy->ops.acquire	= e1000_get_hw_semaphore;
		phy->ops.release	= e1000_put_hw_semaphore;
		break;
	case e1000_82573:
		phy->type		= e1000_phy_m88;
		phy->ops.get_cfg_done	= e1000_get_cfg_done_generic;
		phy->ops.get_info	= e1000_get_phy_info_m88;
		phy->ops.check_polarity	= e1000_check_polarity_m88;
		phy->ops.commit		= e1000_phy_sw_reset_generic;
		phy->ops.force_speed_duplex = e1000_phy_force_speed_duplex_m88;
		phy->ops.get_cable_length = e1000_get_cable_length_m88;
		phy->ops.read_reg	= e1000_read_phy_reg_m88;
		phy->ops.write_reg	= e1000_write_phy_reg_m88;
		phy->ops.acquire	= e1000_get_hw_semaphore;
		phy->ops.release	= e1000_put_hw_semaphore;
		break;
	case e1000_82574:
	case e1000_82583:

		phy->type		= e1000_phy_bm;
		phy->ops.get_cfg_done	= e1000_get_cfg_done_generic;
		phy->ops.get_info	= e1000_get_phy_info_m88;
		phy->ops.check_polarity	= e1000_check_polarity_m88;
		phy->ops.commit		= e1000_phy_sw_reset_generic;
		phy->ops.force_speed_duplex = e1000_phy_force_speed_duplex_m88;
		phy->ops.get_cable_length = e1000_get_cable_length_m88;
		phy->ops.read_reg	= e1000_read_phy_reg_bm2;
		phy->ops.write_reg	= e1000_write_phy_reg_bm2;
		phy->ops.acquire	= e1000_get_hw_semaphore_82574;
		phy->ops.release	= e1000_put_hw_semaphore_82574;
		phy->ops.set_d0_lplu_state = e1000_set_d0_lplu_state_82574;
		phy->ops.set_d3_lplu_state = e1000_set_d3_lplu_state_82574;
		break;
	default:
		return -E1000_ERR_PHY;
		break;
	}

	/* This can only be done after all function pointers are setup. */
	ret_val = e1000_get_phy_id_82571(hw);
	if (ret_val) {
		DEBUGOUT("Error getting PHY ID\n");
		return ret_val;
	}

	/* Verify phy id */
	switch (hw->mac.type) {
	case e1000_82571:
	case e1000_82572:
		if (phy->id != IGP01E1000_I_PHY_ID)
			ret_val = -E1000_ERR_PHY;
		break;
	case e1000_82573:
		if (phy->id != M88E1111_I_PHY_ID)
			ret_val = -E1000_ERR_PHY;
		break;
	case e1000_82574:
	case e1000_82583:
		if (phy->id != BME1000_E_PHY_ID_R2)
			ret_val = -E1000_ERR_PHY;
		break;
	default:
		ret_val = -E1000_ERR_PHY;
		break;
	}

	if (ret_val)
		DEBUGOUT1("PHY ID unknown: type = 0x%08x\n", phy->id);

	return ret_val;
}

/**
 *  e1000_init_nvm_params_82571 - Init NVM func ptrs.
 *  @hw: pointer to the HW structure
 **/
static s32 e1000_init_nvm_params_82571(struct e1000_hw *hw)
{
	struct e1000_nvm_info *nvm = &hw->nvm;
	u32 eecd = E1000_READ_REG(hw, E1000_EECD);
	u16 size;

	DEBUGFUNC("e1000_init_nvm_params_82571");

	nvm->opcode_bits = 8;
	nvm->delay_usec = 1;
	switch (nvm->override) {
	case e1000_nvm_override_spi_large:
		nvm->page_size = 32;
		nvm->address_bits = 16;
		break;
	case e1000_nvm_override_spi_small:
		nvm->page_size = 8;
		nvm->address_bits = 8;
		break;
	default:
		nvm->page_size = eecd & E1000_EECD_ADDR_BITS ? 32 : 8;
		nvm->address_bits = eecd & E1000_EECD_ADDR_BITS ? 16 : 8;
		break;
	}

	switch (hw->mac.type) {
	case e1000_82573:
	case e1000_82574:
	case e1000_82583:
		if (((eecd >> 15) & 0x3) == 0x3) {
			nvm->type = e1000_nvm_flash_hw;
			nvm->word_size = 2048;
			/* Autonomous Flash update bit must be cleared due
			 * to Flash update issue.
			 */
			eecd &= ~E1000_EECD_AUPDEN;
			E1000_WRITE_REG(hw, E1000_EECD, eecd);
			break;
		}
		/* Fall Through */
	default:
		nvm->type = e1000_nvm_eeprom_spi;
		size = (u16)((eecd & E1000_EECD_SIZE_EX_MASK) >>
			     E1000_EECD_SIZE_EX_SHIFT);
		/* Added to a constant, "size" becomes the left-shift value
		 * for setting word_size.
		 */
		size += NVM_WORD_SIZE_BASE_SHIFT;

		/* EEPROM access above 16k is unsupported */
		if (size > 14)
			size = 14;
		nvm->word_size = 1 << size;
		break;
	}

	/* Function Pointers */
	switch (hw->mac.type) {
	case e1000_82574:
	case e1000_82583:
		nvm->ops.acquire = e1000_get_hw_semaphore_82574;
		nvm->ops.release = e1000_put_hw_semaphore_82574;
		break;
	default:
		nvm->ops.acquire = e1000_acquire_nvm_82571;
		nvm->ops.release = e1000_release_nvm_82571;
		break;
	}
	nvm->ops.read = e1000_read_nvm_eerd;
	nvm->ops.update = e1000_update_nvm_checksum_82571;
	nvm->ops.validate = e1000_validate_nvm_checksum_82571;
	nvm->ops.valid_led_default = e1000_valid_led_default_82571;
	nvm->ops.write = e1000_write_nvm_82571;

	return E1000_SUCCESS;
}

/**
 *  e1000_init_mac_params_82571 - Init MAC func ptrs.
 *  @hw: pointer to the HW structure
 **/
static s32 e1000_init_mac_params_82571(struct e1000_hw *hw)
{
	struct e1000_mac_info *mac = &hw->mac;
	u32 swsm = 0;
	u32 swsm2 = 0;
	bool force_clear_smbi = FALSE;

	DEBUGFUNC("e1000_init_mac_params_82571");

	/* Set media type and media-dependent function pointers */
	switch (hw->device_id) {
	case E1000_DEV_ID_82571EB_FIBER:
	case E1000_DEV_ID_82572EI_FIBER:
	case E1000_DEV_ID_82571EB_QUAD_FIBER:
		hw->phy.media_type = e1000_media_type_fiber;
		mac->ops.setup_physical_interface =
			e1000_setup_fiber_serdes_link_82571;
		mac->ops.check_for_link = e1000_check_for_fiber_link_generic;
		mac->ops.get_link_up_info =
			e1000_get_speed_and_duplex_fiber_serdes_generic;
		break;
	case E1000_DEV_ID_82571EB_SERDES:
	case E1000_DEV_ID_82571EB_SERDES_DUAL:
	case E1000_DEV_ID_82571EB_SERDES_QUAD:
	case E1000_DEV_ID_82572EI_SERDES:
		hw->phy.media_type = e1000_media_type_internal_serdes;
		mac->ops.setup_physical_interface =
			e1000_setup_fiber_serdes_link_82571;
		mac->ops.check_for_link = e1000_check_for_serdes_link_82571;
		mac->ops.get_link_up_info =
			e1000_get_speed_and_duplex_fiber_serdes_generic;
		break;
	default:
		hw->phy.media_type = e1000_media_type_copper;
		mac->ops.setup_physical_interface =
			e1000_setup_copper_link_82571;
		mac->ops.check_for_link = e1000_check_for_copper_link_generic;
		mac->ops.get_link_up_info =
			e1000_get_speed_and_duplex_copper_generic;
		break;
	}

	/* Set mta register count */
	mac->mta_reg_count = 128;
	/* Set rar entry count */
	mac->rar_entry_count = E1000_RAR_ENTRIES;
	/* Set if part includes ASF firmware */
	mac->asf_firmware_present = TRUE;
	/* Adaptive IFS supported */
	mac->adaptive_ifs = TRUE;

	/* Function pointers */

	/* bus type/speed/width */
	mac->ops.get_bus_info = e1000_get_bus_info_pcie_generic;
	/* reset */
	mac->ops.reset_hw = e1000_reset_hw_82571;
	/* hw initialization */
	mac->ops.init_hw = e1000_init_hw_82571;
	/* link setup */
	mac->ops.setup_link = e1000_setup_link_82571;
	/* multicast address update */
	mac->ops.update_mc_addr_list = e1000_update_mc_addr_list_generic;
	/* writing VFTA */
	mac->ops.write_vfta = e1000_write_vfta_generic;
	/* clearing VFTA */
	mac->ops.clear_vfta = e1000_clear_vfta_82571;
	/* read mac address */
	mac->ops.read_mac_addr = e1000_read_mac_addr_82571;
	/* ID LED init */
	mac->ops.id_led_init = e1000_id_led_init_generic;
	/* setup LED */
	mac->ops.setup_led = e1000_setup_led_generic;
	/* cleanup LED */
	mac->ops.cleanup_led = e1000_cleanup_led_generic;
	/* turn off LED */
	mac->ops.led_off = e1000_led_off_generic;
	/* clear hardware counters */
	mac->ops.clear_hw_cntrs = e1000_clear_hw_cntrs_82571;

	/* MAC-specific function pointers */
	switch (hw->mac.type) {
	case e1000_82573:
		mac->ops.set_lan_id = e1000_set_lan_id_single_port;
		mac->ops.check_mng_mode = e1000_check_mng_mode_generic;
		mac->ops.led_on = e1000_led_on_generic;
		mac->ops.blink_led = e1000_blink_led_generic;

		/* FWSM register */
		mac->has_fwsm = TRUE;
		/* ARC supported; valid only if manageability features are
		 * enabled.
		 */
		mac->arc_subsystem_valid = !!(E1000_READ_REG(hw, E1000_FWSM) &
					      E1000_FWSM_MODE_MASK);
		break;
	case e1000_82574:
	case e1000_82583:
		mac->ops.set_lan_id = e1000_set_lan_id_single_port;
		mac->ops.check_mng_mode = e1000_check_mng_mode_82574;
		mac->ops.led_on = e1000_led_on_82574;
		break;
	default:
		mac->ops.check_mng_mode = e1000_check_mng_mode_generic;
		mac->ops.led_on = e1000_led_on_generic;
		mac->ops.blink_led = e1000_blink_led_generic;

		/* FWSM register */
		mac->has_fwsm = TRUE;
		break;
	}

	/* Ensure that the inter-port SWSM.SMBI lock bit is clear before
	 * first NVM or PHY access. This should be done for single-port
	 * devices, and for one port only on dual-port devices so that
	 * for those devices we can still use the SMBI lock to synchronize
	 * inter-port accesses to the PHY & NVM.
	 */
	switch (hw->mac.type) {
	case e1000_82571:
	case e1000_82572:
		swsm2 = E1000_READ_REG(hw, E1000_SWSM2);

		if (!(swsm2 & E1000_SWSM2_LOCK)) {
			/* Only do this for the first interface on this card */
			E1000_WRITE_REG(hw, E1000_SWSM2, swsm2 |
					E1000_SWSM2_LOCK);
			force_clear_smbi = TRUE;
		} else {
			force_clear_smbi = FALSE;
		}
		break;
	default:
		force_clear_smbi = TRUE;
		break;
	}

	if (force_clear_smbi) {
		/* Make sure SWSM.SMBI is clear */
		swsm = E1000_READ_REG(hw, E1000_SWSM);
		if (swsm & E1000_SWSM_SMBI) {
			/* This bit should not be set on a first interface, and
			 * indicates that the bootagent or EFI code has
			 * improperly left this bit enabled
			 */
			DEBUGOUT("Please update your 82571 Bootagent\n");
		}
		E1000_WRITE_REG(hw, E1000_SWSM, swsm & ~E1000_SWSM_SMBI);
	}

	/* Initialze device specific counter of SMBI acquisition timeouts. */
	 hw->dev_spec._82571.smb_counter = 0;

	return E1000_SUCCESS;
}

/**
 *  e1000_init_function_pointers_82571 - Init func ptrs.
 *  @hw: pointer to the HW structure
 *
 *  Called to initialize all function pointers and parameters.
 **/
void e1000_init_function_pointers_82571(struct e1000_hw *hw)
{
	DEBUGFUNC("e1000_init_function_pointers_82571");

	hw->mac.ops.init_params = e1000_init_mac_params_82571;
	hw->nvm.ops.init_params = e1000_init_nvm_params_82571;
	hw->phy.ops.init_params = e1000_init_phy_params_82571;
}

/**
 *  e1000_get_phy_id_82571 - Retrieve the PHY ID and revision
 *  @hw: pointer to the HW structure
 *
 *  Reads the PHY registers and stores the PHY ID and possibly the PHY
 *  revision in the hardware structure.
 **/
static s32 e1000_get_phy_id_82571(struct e1000_hw *hw)
{
	struct e1000_phy_info *phy = &hw->phy;
	s32 ret_val;
	u16 phy_id = 0;

	DEBUGFUNC("e1000_get_phy_id_82571");

	switch (hw->mac.type) {
	case e1000_82571:
	case e1000_82572:
		/* The 82571 firmware may still be configuring the PHY.
		 * In this case, we cannot access the PHY until the
		 * configuration is done.  So we explicitly set the
		 * PHY ID.
		 */
		phy->id = IGP01E1000_I_PHY_ID;
		break;
	case e1000_82573:
		return e1000_get_phy_id(hw);
		break;
	case e1000_82574:
	case e1000_82583:
		ret_val = phy->ops.read_reg(hw, PHY_ID1, &phy_id);
		if (ret_val)
			return ret_val;

		phy->id = (u32)(phy_id << 16);
		usec_delay(20);
		ret_val = phy->ops.read_reg(hw, PHY_ID2, &phy_id);
		if (ret_val)
			return ret_val;

		phy->id |= (u32)(phy_id);
		phy->revision = (u32)(phy_id & ~PHY_REVISION_MASK);
		break;
	default:
		return -E1000_ERR_PHY;
		break;
	}

	return E1000_SUCCESS;
}

/**
 *  e1000_get_hw_semaphore_82574 - Acquire hardware semaphore
 *  @hw: pointer to the HW structure
 *
 *  Acquire the HW semaphore during reset.
 *
 **/
static s32
e1000_get_hw_semaphore_82574(struct e1000_hw *hw)
{
	u32 extcnf_ctrl;
	s32 i = 0;
	/* XXX assert that mutex is held */
	DEBUGFUNC("e1000_get_hw_semaphore_82573");

	ASSERT_CTX_LOCK_HELD(hw);
	extcnf_ctrl = E1000_READ_REG(hw, E1000_EXTCNF_CTRL);
	do {
		extcnf_ctrl |= E1000_EXTCNF_CTRL_MDIO_SW_OWNERSHIP;
		E1000_WRITE_REG(hw, E1000_EXTCNF_CTRL, extcnf_ctrl);
		extcnf_ctrl = E1000_READ_REG(hw, E1000_EXTCNF_CTRL);

		if (extcnf_ctrl & E1000_EXTCNF_CTRL_MDIO_SW_OWNERSHIP)
			break;

		msec_delay(2);
		i++;
	} while (i < MDIO_OWNERSHIP_TIMEOUT);

	if (i == MDIO_OWNERSHIP_TIMEOUT) {
		/* Release semaphores */
		e1000_put_hw_semaphore_82574(hw);
		DEBUGOUT("Driver can't access the PHY\n");
		return -E1000_ERR_PHY;
	}

	return E1000_SUCCESS;
}

/**
 *  e1000_put_hw_semaphore_82574 - Release hardware semaphore
 *  @hw: pointer to the HW structure
 *
 *  Release hardware semaphore used during reset.
 *
 **/
static void
e1000_put_hw_semaphore_82574(struct e1000_hw *hw)
{
	u32 extcnf_ctrl;

	DEBUGFUNC("e1000_put_hw_semaphore_82574");

	extcnf_ctrl = E1000_READ_REG(hw, E1000_EXTCNF_CTRL);
	extcnf_ctrl &= ~E1000_EXTCNF_CTRL_MDIO_SW_OWNERSHIP;
	E1000_WRITE_REG(hw, E1000_EXTCNF_CTRL, extcnf_ctrl);
}

/**
 *  e1000_set_d0_lplu_state_82574 - Set Low Power Linkup D0 state
 *  @hw: pointer to the HW structure
 *  @active: TRUE to enable LPLU, FALSE to disable
 *
 *  Sets the LPLU D0 state according to the active flag.
 *  LPLU will not be activated unless the
 *  device autonegotiation advertisement meets standards of
 *  either 10 or 10/100 or 10/100/1000 at all duplexes.
 *  This is a function pointer entry point only called by
 *  PHY setup routines.
 **/
static s32 e1000_set_d0_lplu_state_82574(struct e1000_hw *hw, bool active)
{
	u32 data = E1000_READ_REG(hw, E1000_POEMB);

	DEBUGFUNC("e1000_set_d0_lplu_state_82574");

	if (active)
		data |= E1000_PHY_CTRL_D0A_LPLU;
	else
		data &= ~E1000_PHY_CTRL_D0A_LPLU;

	E1000_WRITE_REG(hw, E1000_POEMB, data);
	return E1000_SUCCESS;
}

/**
 *  e1000_set_d3_lplu_state_82574 - Sets low power link up state for D3
 *  @hw: pointer to the HW structure
 *  @active: boolean used to enable/disable lplu
 *
 *  The low power link up (lplu) state is set to the power management level D3
 *  when active is TRUE, else clear lplu for D3. LPLU
 *  is used during Dx states where the power conservation is most important.
 *  During driver activity, SmartSpeed should be enabled so performance is
 *  maintained.
 **/
static s32 e1000_set_d3_lplu_state_82574(struct e1000_hw *hw, bool active)
{
	u32 data = E1000_READ_REG(hw, E1000_POEMB);

	DEBUGFUNC("e1000_set_d3_lplu_state_82574");

	if (!active) {
		data &= ~E1000_PHY_CTRL_NOND0A_LPLU;
	} else if ((hw->phy.autoneg_advertised == E1000_ALL_SPEED_DUPLEX) ||
		   (hw->phy.autoneg_advertised == E1000_ALL_NOT_GIG) ||
		   (hw->phy.autoneg_advertised == E1000_ALL_10_SPEED)) {
		data |= E1000_PHY_CTRL_NOND0A_LPLU;
	}

	E1000_WRITE_REG(hw, E1000_POEMB, data);
	return E1000_SUCCESS;
}

/**
 *  e1000_acquire_nvm_82571 - Request for access to the EEPROM
 *  @hw: pointer to the HW structure
 *
 *  To gain access to the EEPROM, first we must obtain a hardware semaphore.
 *  Then for non-82573 hardware, set the EEPROM access request bit and wait
 *  for EEPROM access grant bit.  If the access grant bit is not set, release
 *  hardware semaphore.
 **/
static s32 e1000_acquire_nvm_82571(struct e1000_hw *hw)
{
	s32 ret_val;

	DEBUGFUNC("e1000_acquire_nvm_82571");

	ret_val = e1000_get_hw_semaphore(hw);
	if (ret_val)
		return ret_val;

	switch (hw->mac.type) {
	case e1000_82573:
		break;
	default:
		ret_val = e1000_acquire_nvm_generic(hw);
		break;
	}

	if (ret_val)
		e1000_put_hw_semaphore(hw);

	return ret_val;
}

/**
 *  e1000_release_nvm_82571 - Release exclusive access to EEPROM
 *  @hw: pointer to the HW structure
 *
 *  Stop any current commands to the EEPROM and clear the EEPROM request bit.
 **/
static void e1000_release_nvm_82571(struct e1000_hw *hw)
{
	DEBUGFUNC("e1000_release_nvm_82571");

	e1000_release_nvm_generic(hw);
	e1000_put_hw_semaphore(hw);
}

/**
 *  e1000_write_nvm_82571 - Write to EEPROM using appropriate interface
 *  @hw: pointer to the HW structure
 *  @offset: offset within the EEPROM to be written to
 *  @words: number of words to write
 *  @data: 16 bit word(s) to be written to the EEPROM
 *
 *  For non-82573 silicon, write data to EEPROM at offset using SPI interface.
 *
 *  If e1000_update_nvm_checksum is not called after this function, the
 *  EEPROM will most likely contain an invalid checksum.
 **/
static s32 e1000_write_nvm_82571(struct e1000_hw *hw, u16 offset, u16 words,
				 u16 *data)
{
	s32 ret_val;

	DEBUGFUNC("e1000_write_nvm_82571");

	switch (hw->mac.type) {
	case e1000_82573:
	case e1000_82574:
	case e1000_82583:
		ret_val = e1000_write_nvm_eewr_82571(hw, offset, words, data);
		break;
	case e1000_82571:
	case e1000_82572:
		ret_val = e1000_write_nvm_spi(hw, offset, words, data);
		break;
	default:
		ret_val = -E1000_ERR_NVM;
		break;
	}

	return ret_val;
}

/**
 *  e1000_update_nvm_checksum_82571 - Update EEPROM checksum
 *  @hw: pointer to the HW structure
 *
 *  Updates the EEPROM checksum by reading/adding each word of the EEPROM
 *  up to the checksum.  Then calculates the EEPROM checksum and writes the
 *  value to the EEPROM.
 **/
static s32 e1000_update_nvm_checksum_82571(struct e1000_hw *hw)
{
	u32 eecd;
	s32 ret_val;
	u16 i;

	DEBUGFUNC("e1000_update_nvm_checksum_82571");

	ret_val = e1000_update_nvm_checksum_generic(hw);
	if (ret_val)
		return ret_val;

	/* If our nvm is an EEPROM, then we're done
	 * otherwise, commit the checksum to the flash NVM.
	 */
	if (hw->nvm.type != e1000_nvm_flash_hw)
		return E1000_SUCCESS;

	/* Check for pending operations. */
	for (i = 0; i < E1000_FLASH_UPDATES; i++) {
		msec_delay(1);
		if (!(E1000_READ_REG(hw, E1000_EECD) & E1000_EECD_FLUPD))
			break;
	}

	if (i == E1000_FLASH_UPDATES)
		return -E1000_ERR_NVM;

	/* Reset the firmware if using STM opcode. */
	if ((E1000_READ_REG(hw, E1000_FLOP) & 0xFF00) == E1000_STM_OPCODE) {
		/* The enabling of and the actual reset must be done
		 * in two write cycles.
		 */
		E1000_WRITE_REG(hw, E1000_HICR, E1000_HICR_FW_RESET_ENABLE);
		E1000_WRITE_FLUSH(hw);
		E1000_WRITE_REG(hw, E1000_HICR, E1000_HICR_FW_RESET);
	}

	/* Commit the write to flash */
	eecd = E1000_READ_REG(hw, E1000_EECD) | E1000_EECD_FLUPD;
	E1000_WRITE_REG(hw, E1000_EECD, eecd);

	for (i = 0; i < E1000_FLASH_UPDATES; i++) {
		msec_delay(1);
		if (!(E1000_READ_REG(hw, E1000_EECD) & E1000_EECD_FLUPD))
			break;
	}

	if (i == E1000_FLASH_UPDATES)
		return -E1000_ERR_NVM;

	return E1000_SUCCESS;
}

/**
 *  e1000_validate_nvm_checksum_82571 - Validate EEPROM checksum
 *  @hw: pointer to the HW structure
 *
 *  Calculates the EEPROM checksum by reading/adding each word of the EEPROM
 *  and then verifies that the sum of the EEPROM is equal to 0xBABA.
 **/
static s32 e1000_validate_nvm_checksum_82571(struct e1000_hw *hw)
{
	DEBUGFUNC("e1000_validate_nvm_checksum_82571");

	if (hw->nvm.type == e1000_nvm_flash_hw)
		e1000_fix_nvm_checksum_82571(hw);

	return e1000_validate_nvm_checksum_generic(hw);
}

/**
 *  e1000_write_nvm_eewr_82571 - Write to EEPROM for 82573 silicon
 *  @hw: pointer to the HW structure
 *  @offset: offset within the EEPROM to be written to
 *  @words: number of words to write
 *  @data: 16 bit word(s) to be written to the EEPROM
 *
 *  After checking for invalid values, poll the EEPROM to ensure the previous
 *  command has completed before trying to write the next word.  After write
 *  poll for completion.
 *
 *  If e1000_update_nvm_checksum is not called after this function, the
 *  EEPROM will most likely contain an invalid checksum.
 **/
static s32 e1000_write_nvm_eewr_82571(struct e1000_hw *hw, u16 offset,
				      u16 words, u16 *data)
{
	struct e1000_nvm_info *nvm = &hw->nvm;
	u32 i, eewr = 0;
	s32 ret_val = E1000_SUCCESS;

	DEBUGFUNC("e1000_write_nvm_eewr_82571");

	/* A check for invalid values:  offset too large, too many words,
	 * and not enough words.
	 */
	if ((offset >= nvm->word_size) || (words > (nvm->word_size - offset)) ||
	    (words == 0)) {
		DEBUGOUT("nvm parameter(s) out of bounds\n");
		return -E1000_ERR_NVM;
	}

	for (i = 0; i < words; i++) {
		eewr = ((data[i] << E1000_NVM_RW_REG_DATA) |
			((offset + i) << E1000_NVM_RW_ADDR_SHIFT) |
			E1000_NVM_RW_REG_START);

		ret_val = e1000_poll_eerd_eewr_done(hw, E1000_NVM_POLL_WRITE);
		if (ret_val)
			break;

		E1000_WRITE_REG(hw, E1000_EEWR, eewr);

		ret_val = e1000_poll_eerd_eewr_done(hw, E1000_NVM_POLL_WRITE);
		if (ret_val)
			break;
	}

	return ret_val;
}

/**
 *  e1000_get_cfg_done_82571 - Poll for configuration done
 *  @hw: pointer to the HW structure
 *
 *  Reads the management control register for the config done bit to be set.
 **/
static s32 e1000_get_cfg_done_82571(struct e1000_hw *hw)
{
	s32 timeout = PHY_CFG_TIMEOUT;

	DEBUGFUNC("e1000_get_cfg_done_82571");

	while (timeout) {
		if (E1000_READ_REG(hw, E1000_EEMNGCTL) &
		    E1000_NVM_CFG_DONE_PORT_0)
			break;
		msec_delay(1);
		timeout--;
	}
	if (!timeout) {
		DEBUGOUT("MNG configuration cycle has not completed.\n");
		return -E1000_ERR_RESET;
	}

	return E1000_SUCCESS;
}

/**
 *  e1000_set_d0_lplu_state_82571 - Set Low Power Linkup D0 state
 *  @hw: pointer to the HW structure
 *  @active: TRUE to enable LPLU, FALSE to disable
 *
 *  Sets the LPLU D0 state according to the active flag.  When activating LPLU
 *  this function also disables smart speed and vice versa.  LPLU will not be
 *  activated unless the device autonegotiation advertisement meets standards
 *  of either 10 or 10/100 or 10/100/1000 at all duplexes.  This is a function
 *  pointer entry point only called by PHY setup routines.
 **/
static s32 e1000_set_d0_lplu_state_82571(struct e1000_hw *hw, bool active)
{
	struct e1000_phy_info *phy = &hw->phy;
	s32 ret_val;
	u16 data;

	DEBUGFUNC("e1000_set_d0_lplu_state_82571");

	if (!(phy->ops.read_reg))
		return E1000_SUCCESS;

	ret_val = phy->ops.read_reg(hw, IGP02E1000_PHY_POWER_MGMT, &data);
	if (ret_val)
		return ret_val;

	if (active) {
		data |= IGP02E1000_PM_D0_LPLU;
		ret_val = phy->ops.write_reg(hw, IGP02E1000_PHY_POWER_MGMT,
					     data);
		if (ret_val)
			return ret_val;

		/* When LPLU is enabled, we should disable SmartSpeed */
		ret_val = phy->ops.read_reg(hw, IGP01E1000_PHY_PORT_CONFIG,
					    &data);
		if (ret_val)
			return ret_val;
		data &= ~IGP01E1000_PSCFR_SMART_SPEED;
		ret_val = phy->ops.write_reg(hw, IGP01E1000_PHY_PORT_CONFIG,
					     data);
		if (ret_val)
			return ret_val;
	} else {
		data &= ~IGP02E1000_PM_D0_LPLU;
		ret_val = phy->ops.write_reg(hw, IGP02E1000_PHY_POWER_MGMT,
					     data);
		/* LPLU and SmartSpeed are mutually exclusive.  LPLU is used
		 * during Dx states where the power conservation is most
		 * important.  During driver activity we should enable
		 * SmartSpeed, so performance is maintained.
		 */
		if (phy->smart_speed == e1000_smart_speed_on) {
			ret_val = phy->ops.read_reg(hw,
						    IGP01E1000_PHY_PORT_CONFIG,
						    &data);
			if (ret_val)
				return ret_val;

			data |= IGP01E1000_PSCFR_SMART_SPEED;
			ret_val = phy->ops.write_reg(hw,
						     IGP01E1000_PHY_PORT_CONFIG,
						     data);
			if (ret_val)
				return ret_val;
		} else if (phy->smart_speed == e1000_smart_speed_off) {
			ret_val = phy->ops.read_reg(hw,
						    IGP01E1000_PHY_PORT_CONFIG,
						    &data);
			if (ret_val)
				return ret_val;

			data &= ~IGP01E1000_PSCFR_SMART_SPEED;
			ret_val = phy->ops.write_reg(hw,
						     IGP01E1000_PHY_PORT_CONFIG,
						     data);
			if (ret_val)
				return ret_val;
		}
	}

	return E1000_SUCCESS;
}

/**
 *  e1000_reset_hw_82571 - Reset hardware
 *  @hw: pointer to the HW structure
 *
 *  This resets the hardware into a known state.
 **/
static s32 e1000_reset_hw_82571(struct e1000_hw *hw)
{
	u32 ctrl, ctrl_ext, eecd, tctl;
	s32 ret_val;

	DEBUGFUNC("e1000_reset_hw_82571");

	/* Prevent the PCI-E bus from sticking if there is no TLP connection
	 * on the last TLP read/write transaction when MAC is reset.
	 */
	ret_val = e1000_disable_pcie_master_generic(hw);
	if (ret_val)
		DEBUGOUT("PCI-E Master disable polling has failed.\n");

	DEBUGOUT("Masking off all interrupts\n");
	E1000_WRITE_REG(hw, E1000_IMC, 0xffffffff);

	E1000_WRITE_REG(hw, E1000_RCTL, 0);
	tctl = E1000_READ_REG(hw, E1000_TCTL);
	tctl &= ~E1000_TCTL_EN;
	E1000_WRITE_REG(hw, E1000_TCTL, tctl);
	E1000_WRITE_FLUSH(hw);

	msec_delay(10);

	/* Must acquire the MDIO ownership before MAC reset.
	 * Ownership defaults to firmware after a reset.
	 */
	switch (hw->mac.type) {
	case e1000_82573:
	case e1000_82574:
	case e1000_82583:
		ret_val = e1000_get_hw_semaphore_82574(hw);
		break;
	default:
		break;
	}

	ctrl = E1000_READ_REG(hw, E1000_CTRL);

	DEBUGOUT("Issuing a global reset to MAC\n");
	E1000_WRITE_REG(hw, E1000_CTRL, ctrl | E1000_CTRL_RST);

	/* Must release MDIO ownership and mutex after MAC reset. */
	switch (hw->mac.type) {
	case e1000_82573:
	case e1000_82574:
	case e1000_82583:
		/* Release mutex only if the hw semaphore is acquired */
		if (!ret_val)
			e1000_put_hw_semaphore_82574(hw);
		break;
	default:
		/* we didn't get the semaphore no need to put it */
		break;
	}

	if (hw->nvm.type == e1000_nvm_flash_hw) {
		usec_delay(10);
		ctrl_ext = E1000_READ_REG(hw, E1000_CTRL_EXT);
		ctrl_ext |= E1000_CTRL_EXT_EE_RST;
		E1000_WRITE_REG(hw, E1000_CTRL_EXT, ctrl_ext);
		E1000_WRITE_FLUSH(hw);
	}

	ret_val = e1000_get_auto_rd_done_generic(hw);
	if (ret_val)
		/* We don't want to continue accessing MAC registers. */
		return ret_val;

	/* Phy configuration from NVM just starts after EECD_AUTO_RD is set.
	 * Need to wait for Phy configuration completion before accessing
	 * NVM and Phy.
	 */

	switch (hw->mac.type) {
	case e1000_82571:
	case e1000_82572:
		/* REQ and GNT bits need to be cleared when using AUTO_RD
		 * to access the EEPROM.
		 */
		eecd = E1000_READ_REG(hw, E1000_EECD);
		eecd &= ~(E1000_EECD_REQ | E1000_EECD_GNT);
		E1000_WRITE_REG(hw, E1000_EECD, eecd);
		break;
	case e1000_82573:
	case e1000_82574:
	case e1000_82583:
		msec_delay(25);
		break;
	default:
		break;
	}

	/* Clear any pending interrupt events. */
	E1000_WRITE_REG(hw, E1000_IMC, 0xffffffff);
	E1000_READ_REG(hw, E1000_ICR);

	if (hw->mac.type == e1000_82571) {
		/* Install any alternate MAC address into RAR0 */
		ret_val = e1000_check_alt_mac_addr_generic(hw);
		if (ret_val)
			return ret_val;

		e1000_set_laa_state_82571(hw, TRUE);
	}

	/* Reinitialize the 82571 serdes link state machine */
	if (hw->phy.media_type == e1000_media_type_internal_serdes)
		hw->mac.serdes_link_state = e1000_serdes_link_down;

	return E1000_SUCCESS;
}

/**
 *  e1000_init_hw_82571 - Initialize hardware
 *  @hw: pointer to the HW structure
 *
 *  This inits the hardware readying it for operation.
 **/
static s32 e1000_init_hw_82571(struct e1000_hw *hw)
{
	struct e1000_mac_info *mac = &hw->mac;
	u32 reg_data;
	s32 ret_val;
	u16 i, rar_count = mac->rar_entry_count;

	DEBUGFUNC("e1000_init_hw_82571");

	e1000_initialize_hw_bits_82571(hw);

	/* Initialize identification LED */
	ret_val = mac->ops.id_led_init(hw);
	/* An error is not fatal and we should not stop init due to this */
	if (ret_val)
		DEBUGOUT("Error initializing identification LED\n");

	/* Disabling VLAN filtering */
	DEBUGOUT("Initializing the IEEE VLAN\n");
	mac->ops.clear_vfta(hw);

	/* Setup the receive address.
	 * If, however, a locally administered address was assigned to the
	 * 82571, we must reserve a RAR for it to work around an issue where
	 * resetting one port will reload the MAC on the other port.
	 */
	if (e1000_get_laa_state_82571(hw))
		rar_count--;
	e1000_init_rx_addrs_generic(hw, rar_count);

	/* Zero out the Multicast HASH table */
	DEBUGOUT("Zeroing the MTA\n");
	for (i = 0; i < mac->mta_reg_count; i++)
		E1000_WRITE_REG_ARRAY(hw, E1000_MTA, i, 0);

	/* Setup link and flow control */
	ret_val = mac->ops.setup_link(hw);

	/* Set the transmit descriptor write-back policy */
	reg_data = E1000_READ_REG(hw, E1000_TXDCTL(0));
	reg_data = ((reg_data & ~E1000_TXDCTL_WTHRESH) |
		    E1000_TXDCTL_FULL_TX_DESC_WB | E1000_TXDCTL_COUNT_DESC);
	E1000_WRITE_REG(hw, E1000_TXDCTL(0), reg_data);

	/* ...for both queues. */
	switch (mac->type) {
	case e1000_82573:
		e1000_enable_tx_pkt_filtering_generic(hw);
		/* fall through */
	case e1000_82574:
	case e1000_82583:
		reg_data = E1000_READ_REG(hw, E1000_GCR);
		reg_data |= E1000_GCR_L1_ACT_WITHOUT_L0S_RX;
		E1000_WRITE_REG(hw, E1000_GCR, reg_data);
		break;
	default:
		reg_data = E1000_READ_REG(hw, E1000_TXDCTL(1));
		reg_data = ((reg_data & ~E1000_TXDCTL_WTHRESH) |
			    E1000_TXDCTL_FULL_TX_DESC_WB |
			    E1000_TXDCTL_COUNT_DESC);
		E1000_WRITE_REG(hw, E1000_TXDCTL(1), reg_data);
		break;
	}

	/* Clear all of the statistics registers (clear on read).  It is
	 * important that we do this after we have tried to establish link
	 * because the symbol error count will increment wildly if there
	 * is no link.
	 */
	e1000_clear_hw_cntrs_82571(hw);

	return ret_val;
}

/**
 *  e1000_initialize_hw_bits_82571 - Initialize hardware-dependent bits
 *  @hw: pointer to the HW structure
 *
 *  Initializes required hardware-dependent bits needed for normal operation.
 **/
static void e1000_initialize_hw_bits_82571(struct e1000_hw *hw)
{
	u32 reg;

	DEBUGFUNC("e1000_initialize_hw_bits_82571");

	/* Transmit Descriptor Control 0 */
	reg = E1000_READ_REG(hw, E1000_TXDCTL(0));
	reg |= (1 << 22);
	E1000_WRITE_REG(hw, E1000_TXDCTL(0), reg);

	/* Transmit Descriptor Control 1 */
	reg = E1000_READ_REG(hw, E1000_TXDCTL(1));
	reg |= (1 << 22);
	E1000_WRITE_REG(hw, E1000_TXDCTL(1), reg);

	/* Transmit Arbitration Control 0 */
	reg = E1000_READ_REG(hw, E1000_TARC(0));
	reg &= ~(0xF << 27); /* 30:27 */
	switch (hw->mac.type) {
	case e1000_82571:
	case e1000_82572:
		reg |= (1 << 23) | (1 << 24) | (1 << 25) | (1 << 26);
		break;
	case e1000_82574:
	case e1000_82583:
		reg |= (1 << 26);
		break;
	default:
		break;
	}
	E1000_WRITE_REG(hw, E1000_TARC(0), reg);

	/* Transmit Arbitration Control 1 */
	reg = E1000_READ_REG(hw, E1000_TARC(1));
	switch (hw->mac.type) {
	case e1000_82571:
	case e1000_82572:
		reg &= ~((1 << 29) | (1 << 30));
		reg |= (1 << 22) | (1 << 24) | (1 << 25) | (1 << 26);
		if (E1000_READ_REG(hw, E1000_TCTL) & E1000_TCTL_MULR)
			reg &= ~(1 << 28);
		else
			reg |= (1 << 28);
		E1000_WRITE_REG(hw, E1000_TARC(1), reg);
		break;
	default:
		break;
	}

	/* Device Control */
	switch (hw->mac.type) {
	case e1000_82573:
	case e1000_82574:
	case e1000_82583:
		reg = E1000_READ_REG(hw, E1000_CTRL);
		reg &= ~(1 << 29);
		E1000_WRITE_REG(hw, E1000_CTRL, reg);
		break;
	default:
		break;
	}

	/* Extended Device Control */
	switch (hw->mac.type) {
	case e1000_82573:
	case e1000_82574:
	case e1000_82583:
		reg = E1000_READ_REG(hw, E1000_CTRL_EXT);
		reg &= ~(1 << 23);
		reg |= (1 << 22);
		E1000_WRITE_REG(hw, E1000_CTRL_EXT, reg);
		break;
	default:
		break;
	}

	if (hw->mac.type == e1000_82571) {
		reg = E1000_READ_REG(hw, E1000_PBA_ECC);
		reg |= E1000_PBA_ECC_CORR_EN;
		E1000_WRITE_REG(hw, E1000_PBA_ECC, reg);
	}

	/* Workaround for hardware errata.
	 * Ensure that DMA Dynamic Clock gating is disabled on 82571 and 82572
	 */
	if ((hw->mac.type == e1000_82571) ||
	   (hw->mac.type == e1000_82572)) {
		reg = E1000_READ_REG(hw, E1000_CTRL_EXT);
		reg &= ~E1000_CTRL_EXT_DMA_DYN_CLK_EN;
		E1000_WRITE_REG(hw, E1000_CTRL_EXT, reg);
	}

	/* Disable IPv6 extension header parsing because some malformed
	 * IPv6 headers can hang the Rx.
	 */
	if (hw->mac.type <= e1000_82573) {
		reg = E1000_READ_REG(hw, E1000_RFCTL);
		reg |= (E1000_RFCTL_IPV6_EX_DIS | E1000_RFCTL_NEW_IPV6_EXT_DIS);
		E1000_WRITE_REG(hw, E1000_RFCTL, reg);
	}

	/* PCI-Ex Control Registers */
	switch (hw->mac.type) {
	case e1000_82574:
	case e1000_82583:
		reg = E1000_READ_REG(hw, E1000_GCR);
		reg |= (1 << 22);
		E1000_WRITE_REG(hw, E1000_GCR, reg);

		/* Workaround for hardware errata.
		 * apply workaround for hardware errata documented in errata
		 * docs Fixes issue where some error prone or unreliable PCIe
		 * completions are occurring, particularly with ASPM enabled.
		 * Without fix, issue can cause Tx timeouts.
		 */
		reg = E1000_READ_REG(hw, E1000_GCR2);
		reg |= 1;
		E1000_WRITE_REG(hw, E1000_GCR2, reg);
		break;
	default:
		break;
	}

	return;
}

/**
 *  e1000_clear_vfta_82571 - Clear VLAN filter table
 *  @hw: pointer to the HW structure
 *
 *  Clears the register array which contains the VLAN filter table by
 *  setting all the values to 0.
 **/
static void e1000_clear_vfta_82571(struct e1000_hw *hw)
{
	u32 offset;
	u32 vfta_value = 0;
	u32 vfta_offset = 0;
	u32 vfta_bit_in_reg = 0;

	DEBUGFUNC("e1000_clear_vfta_82571");

	switch (hw->mac.type) {
	case e1000_82573:
	case e1000_82574:
	case e1000_82583:
		if (hw->mng_cookie.vlan_id != 0) {
			/* The VFTA is a 4096b bit-field, each identifying
			 * a single VLAN ID.  The following operations
			 * determine which 32b entry (i.e. offset) into the
			 * array we want to set the VLAN ID (i.e. bit) of
			 * the manageability unit.
			 */
			vfta_offset = (hw->mng_cookie.vlan_id >>
				       E1000_VFTA_ENTRY_SHIFT) &
			    E1000_VFTA_ENTRY_MASK;
			vfta_bit_in_reg =
			    1 << (hw->mng_cookie.vlan_id &
				  E1000_VFTA_ENTRY_BIT_SHIFT_MASK);
		}
		break;
	default:
		break;
	}
	for (offset = 0; offset < E1000_VLAN_FILTER_TBL_SIZE; offset++) {
		/* If the offset we want to clear is the same offset of the
		 * manageability VLAN ID, then clear all bits except that of
		 * the manageability unit.
		 */
		vfta_value = (offset == vfta_offset) ? vfta_bit_in_reg : 0;
		E1000_WRITE_REG_ARRAY(hw, E1000_VFTA, offset, vfta_value);
		E1000_WRITE_FLUSH(hw);
	}
}

/**
 *  e1000_check_mng_mode_82574 - Check manageability is enabled
 *  @hw: pointer to the HW structure
 *
 *  Reads the NVM Initialization Control Word 2 and returns TRUE
 *  (>0) if any manageability is enabled, else FALSE (0).
 **/
static bool e1000_check_mng_mode_82574(struct e1000_hw *hw)
{
	u16 data;
	s32 ret_val;

	DEBUGFUNC("e1000_check_mng_mode_82574");

	ret_val = hw->nvm.ops.read(hw, NVM_INIT_CONTROL2_REG, 1, &data);
	if (ret_val)
		return FALSE;

	return (data & E1000_NVM_INIT_CTRL2_MNGM) != 0;
}

/**
 *  e1000_led_on_82574 - Turn LED on
 *  @hw: pointer to the HW structure
 *
 *  Turn LED on.
 **/
static s32 e1000_led_on_82574(struct e1000_hw *hw)
{
	u32 ctrl;
	u32 i;

	DEBUGFUNC("e1000_led_on_82574");

	ctrl = hw->mac.ledctl_mode2;
	if (!(E1000_STATUS_LU & E1000_READ_REG(hw, E1000_STATUS))) {
		/* If no link, then turn LED on by setting the invert bit
		 * for each LED that's "on" (0x0E) in ledctl_mode2.
		 */
		for (i = 0; i < 4; i++)
			if (((hw->mac.ledctl_mode2 >> (i * 8)) & 0xFF) ==
			    E1000_LEDCTL_MODE_LED_ON)
				ctrl |= (E1000_LEDCTL_LED0_IVRT << (i * 8));
	}
	E1000_WRITE_REG(hw, E1000_LEDCTL, ctrl);

	return E1000_SUCCESS;
}

/**
 *  e1000_check_phy_82574 - check 82574 phy hung state
 *  @hw: pointer to the HW structure
 *
 *  Returns whether phy is hung or not
 **/
bool e1000_check_phy_82574(struct e1000_hw *hw)
{
	u16 status_1kbt = 0;
	u16 receive_errors = 0;
	s32 ret_val;

	DEBUGFUNC("e1000_check_phy_82574");

	/* Read PHY Receive Error counter first, if its is max - all F's then
	 * read the Base1000T status register If both are max then PHY is hung.
	 */
	ret_val = hw->phy.ops.read_reg(hw, E1000_RECEIVE_ERROR_COUNTER,
				       &receive_errors);
	if (ret_val)
		return FALSE;
	if (receive_errors == E1000_RECEIVE_ERROR_MAX) {
		ret_val = hw->phy.ops.read_reg(hw, E1000_BASE1000T_STATUS,
					       &status_1kbt);
		if (ret_val)
			return FALSE;
		if ((status_1kbt & E1000_IDLE_ERROR_COUNT_MASK) ==
		    E1000_IDLE_ERROR_COUNT_MASK)
			return TRUE;
	}

	return FALSE;
}


/**
 *  e1000_setup_link_82571 - Setup flow control and link settings
 *  @hw: pointer to the HW structure
 *
 *  Determines which flow control settings to use, then configures flow
 *  control.  Calls the appropriate media-specific link configuration
 *  function.  Assuming the adapter has a valid link partner, a valid link
 *  should be established.  Assumes the hardware has previously been reset
 *  and the transmitter and receiver are not enabled.
 **/
static s32 e1000_setup_link_82571(struct e1000_hw *hw)
{
	DEBUGFUNC("e1000_setup_link_82571");

	/* 82573 does not have a word in the NVM to determine
	 * the default flow control setting, so we explicitly
	 * set it to full.
	 */
	switch (hw->mac.type) {
	case e1000_82573:
	case e1000_82574:
	case e1000_82583:
		if (hw->fc.requested_mode == e1000_fc_default)
			hw->fc.requested_mode = e1000_fc_full;
		break;
	default:
		break;
	}

	return e1000_setup_link_generic(hw);
}

/**
 *  e1000_setup_copper_link_82571 - Configure copper link settings
 *  @hw: pointer to the HW structure
 *
 *  Configures the link for auto-neg or forced speed and duplex.  Then we check
 *  for link, once link is established calls to configure collision distance
 *  and flow control are called.
 **/
static s32 e1000_setup_copper_link_82571(struct e1000_hw *hw)
{
	u32 ctrl;
	s32 ret_val;

	DEBUGFUNC("e1000_setup_copper_link_82571");

	ctrl = E1000_READ_REG(hw, E1000_CTRL);
	ctrl |= E1000_CTRL_SLU;
	ctrl &= ~(E1000_CTRL_FRCSPD | E1000_CTRL_FRCDPX);
	E1000_WRITE_REG(hw, E1000_CTRL, ctrl);

	switch (hw->phy.type) {
	case e1000_phy_m88:
	case e1000_phy_bm:
		ret_val = e1000_copper_link_setup_m88(hw);
		break;
	case e1000_phy_igp_2:
		ret_val = e1000_copper_link_setup_igp(hw);
		break;
	default:
		return -E1000_ERR_PHY;
		break;
	}

	if (ret_val)
		return ret_val;

	return e1000_setup_copper_link_generic(hw);
}

/**
 *  e1000_setup_fiber_serdes_link_82571 - Setup link for fiber/serdes
 *  @hw: pointer to the HW structure
 *
 *  Configures collision distance and flow control for fiber and serdes links.
 *  Upon successful setup, poll for link.
 **/
static s32 e1000_setup_fiber_serdes_link_82571(struct e1000_hw *hw)
{
	DEBUGFUNC("e1000_setup_fiber_serdes_link_82571");

	switch (hw->mac.type) {
	case e1000_82571:
	case e1000_82572:
		/* If SerDes loopback mode is entered, there is no form
		 * of reset to take the adapter out of that mode.  So we
		 * have to explicitly take the adapter out of loopback
		 * mode.  This prevents drivers from twiddling their thumbs
		 * if another tool failed to take it out of loopback mode.
		 */
		E1000_WRITE_REG(hw, E1000_SCTL,
				E1000_SCTL_DISABLE_SERDES_LOOPBACK);
		break;
	default:
		break;
	}

	return e1000_setup_fiber_serdes_link_generic(hw);
}

/**
 *  e1000_check_for_serdes_link_82571 - Check for link (Serdes)
 *  @hw: pointer to the HW structure
 *
 *  Reports the link state as up or down.
 *
 *  If autonegotiation is supported by the link partner, the link state is
 *  determined by the result of autonegotiation. This is the most likely case.
 *  If autonegotiation is not supported by the link partner, and the link
 *  has a valid signal, force the link up.
 *
 *  The link state is represented internally here by 4 states:
 *
 *  1) down
 *  2) autoneg_progress
 *  3) autoneg_complete (the link successfully autonegotiated)
 *  4) forced_up (the link has been forced up, it did not autonegotiate)
 *
 **/
static s32 e1000_check_for_serdes_link_82571(struct e1000_hw *hw)
{
	struct e1000_mac_info *mac = &hw->mac;
	u32 rxcw;
	u32 ctrl;
	u32 status;
	u32 txcw;
	u32 i;
	s32 ret_val = E1000_SUCCESS;

	DEBUGFUNC("e1000_check_for_serdes_link_82571");

	ctrl = E1000_READ_REG(hw, E1000_CTRL);
	status = E1000_READ_REG(hw, E1000_STATUS);
	E1000_READ_REG(hw, E1000_RXCW);
	/* SYNCH bit and IV bit are sticky */
	usec_delay(10);
	rxcw = E1000_READ_REG(hw, E1000_RXCW);

	if ((rxcw & E1000_RXCW_SYNCH) && !(rxcw & E1000_RXCW_IV)) {
		/* Receiver is synchronized with no invalid bits.  */
		switch (mac->serdes_link_state) {
		case e1000_serdes_link_autoneg_complete:
			if (!(status & E1000_STATUS_LU)) {
				/* We have lost link, retry autoneg before
				 * reporting link failure
				 */
				mac->serdes_link_state =
				    e1000_serdes_link_autoneg_progress;
				mac->serdes_has_link = FALSE;
				DEBUGOUT("AN_UP     -> AN_PROG\n");
			} else {
				mac->serdes_has_link = TRUE;
			}
			break;

		case e1000_serdes_link_forced_up:
			/* If we are receiving /C/ ordered sets, re-enable
			 * auto-negotiation in the TXCW register and disable
			 * forced link in the Device Control register in an
			 * attempt to auto-negotiate with our link partner.
			 */
			if (rxcw & E1000_RXCW_C) {
				/* Enable autoneg, and unforce link up */
				E1000_WRITE_REG(hw, E1000_TXCW, mac->txcw);
				E1000_WRITE_REG(hw, E1000_CTRL,
				    (ctrl & ~E1000_CTRL_SLU));
				mac->serdes_link_state =
				    e1000_serdes_link_autoneg_progress;
				mac->serdes_has_link = FALSE;
				DEBUGOUT("FORCED_UP -> AN_PROG\n");
			} else {
				mac->serdes_has_link = TRUE;
			}
			break;

		case e1000_serdes_link_autoneg_progress:
			if (rxcw & E1000_RXCW_C) {
				/* We received /C/ ordered sets, meaning the
				 * link partner has autonegotiated, and we can
				 * trust the Link Up (LU) status bit.
				 */
				if (status & E1000_STATUS_LU) {
					mac->serdes_link_state =
					    e1000_serdes_link_autoneg_complete;
					DEBUGOUT("AN_PROG   -> AN_UP\n");
					mac->serdes_has_link = TRUE;
				} else {
					/* Autoneg completed, but failed. */
					mac->serdes_link_state =
					    e1000_serdes_link_down;
					DEBUGOUT("AN_PROG   -> DOWN\n");
				}
			} else {
				/* The link partner did not autoneg.
				 * Force link up and full duplex, and change
				 * state to forced.
				 */
				E1000_WRITE_REG(hw, E1000_TXCW,
				(mac->txcw & ~E1000_TXCW_ANE));
				ctrl |= (E1000_CTRL_SLU | E1000_CTRL_FD);
				E1000_WRITE_REG(hw, E1000_CTRL, ctrl);

				/* Configure Flow Control after link up. */
				ret_val =
				    e1000_config_fc_after_link_up_generic(hw);
				if (ret_val) {
					DEBUGOUT("Error config flow control\n");
					break;
				}
				mac->serdes_link_state =
						e1000_serdes_link_forced_up;
				mac->serdes_has_link = TRUE;
				DEBUGOUT("AN_PROG   -> FORCED_UP\n");
			}
			break;

		case e1000_serdes_link_down:
		default:
			/* The link was down but the receiver has now gained
			 * valid sync, so lets see if we can bring the link
			 * up.
			 */
			E1000_WRITE_REG(hw, E1000_TXCW, mac->txcw);
			E1000_WRITE_REG(hw, E1000_CTRL, (ctrl &
					~E1000_CTRL_SLU));
			mac->serdes_link_state =
					e1000_serdes_link_autoneg_progress;
			mac->serdes_has_link = FALSE;
			DEBUGOUT("DOWN      -> AN_PROG\n");
			break;
		}
	} else {
		if (!(rxcw & E1000_RXCW_SYNCH)) {
			mac->serdes_has_link = FALSE;
			mac->serdes_link_state = e1000_serdes_link_down;
			DEBUGOUT("ANYSTATE  -> DOWN\n");
		} else {
			/* Check several times, if SYNCH bit and CONFIG
			 * bit both are consistently 1 then simply ignore
			 * the IV bit and restart Autoneg
			 */
			for (i = 0; i < AN_RETRY_COUNT; i++) {
				usec_delay(10);
				rxcw = E1000_READ_REG(hw, E1000_RXCW);
				if ((rxcw & E1000_RXCW_SYNCH) &&
				    (rxcw & E1000_RXCW_C))
					continue;

				if (rxcw & E1000_RXCW_IV) {
					mac->serdes_has_link = FALSE;
					mac->serdes_link_state =
							e1000_serdes_link_down;
					DEBUGOUT("ANYSTATE  -> DOWN\n");
					break;
				}
			}

			if (i == AN_RETRY_COUNT) {
				txcw = E1000_READ_REG(hw, E1000_TXCW);
				txcw |= E1000_TXCW_ANE;
				E1000_WRITE_REG(hw, E1000_TXCW, txcw);
				mac->serdes_link_state =
					e1000_serdes_link_autoneg_progress;
				mac->serdes_has_link = FALSE;
				DEBUGOUT("ANYSTATE  -> AN_PROG\n");
			}
		}
	}

	return ret_val;
}

/**
 *  e1000_valid_led_default_82571 - Verify a valid default LED config
 *  @hw: pointer to the HW structure
 *  @data: pointer to the NVM (EEPROM)
 *
 *  Read the EEPROM for the current default LED configuration.  If the
 *  LED configuration is not valid, set to a valid LED configuration.
 **/
static s32 e1000_valid_led_default_82571(struct e1000_hw *hw, u16 *data)
{
	s32 ret_val;

	DEBUGFUNC("e1000_valid_led_default_82571");

	ret_val = hw->nvm.ops.read(hw, NVM_ID_LED_SETTINGS, 1, data);
	if (ret_val) {
		DEBUGOUT("NVM Read Error\n");
		return ret_val;
	}

	switch (hw->mac.type) {
	case e1000_82573:
	case e1000_82574:
	case e1000_82583:
		if (*data == ID_LED_RESERVED_F746)
			*data = ID_LED_DEFAULT_82573;
		break;
	default:
		if (*data == ID_LED_RESERVED_0000 ||
		    *data == ID_LED_RESERVED_FFFF)
			*data = ID_LED_DEFAULT;
		break;
	}

	return E1000_SUCCESS;
}

/**
 *  e1000_get_laa_state_82571 - Get locally administered address state
 *  @hw: pointer to the HW structure
 *
 *  Retrieve and return the current locally administered address state.
 **/
bool e1000_get_laa_state_82571(struct e1000_hw *hw)
{
	DEBUGFUNC("e1000_get_laa_state_82571");

	if (hw->mac.type != e1000_82571)
		return FALSE;

	return hw->dev_spec._82571.laa_is_present;
}

/**
 *  e1000_set_laa_state_82571 - Set locally administered address state
 *  @hw: pointer to the HW structure
 *  @state: enable/disable locally administered address
 *
 *  Enable/Disable the current locally administered address state.
 **/
void e1000_set_laa_state_82571(struct e1000_hw *hw, bool state)
{
	DEBUGFUNC("e1000_set_laa_state_82571");

	if (hw->mac.type != e1000_82571)
		return;

	hw->dev_spec._82571.laa_is_present = state;

	/* If workaround is activated... */
	if (state)
		/* Hold a copy of the LAA in RAR[14] This is done so that
		 * between the time RAR[0] gets clobbered and the time it
		 * gets fixed, the actual LAA is in one of the RARs and no
		 * incoming packets directed to this port are dropped.
		 * Eventually the LAA will be in RAR[0] and RAR[14].
		 */
		hw->mac.ops.rar_set(hw, hw->mac.addr,
				    hw->mac.rar_entry_count - 1);
	return;
}

/**
 *  e1000_fix_nvm_checksum_82571 - Fix EEPROM checksum
 *  @hw: pointer to the HW structure
 *
 *  Verifies that the EEPROM has completed the update.  After updating the
 *  EEPROM, we need to check bit 15 in work 0x23 for the checksum fix.  If
 *  the checksum fix is not implemented, we need to set the bit and update
 *  the checksum.  Otherwise, if bit 15 is set and the checksum is incorrect,
 *  we need to return bad checksum.
 **/
static s32 e1000_fix_nvm_checksum_82571(struct e1000_hw *hw)
{
	struct e1000_nvm_info *nvm = &hw->nvm;
	s32 ret_val;
	u16 data;

	DEBUGFUNC("e1000_fix_nvm_checksum_82571");

	if (nvm->type != e1000_nvm_flash_hw)
		return E1000_SUCCESS;

	/* Check bit 4 of word 10h.  If it is 0, firmware is done updating
	 * 10h-12h.  Checksum may need to be fixed.
	 */
	ret_val = nvm->ops.read(hw, 0x10, 1, &data);
	if (ret_val)
		return ret_val;

	if (!(data & 0x10)) {
		/* Read 0x23 and check bit 15.  This bit is a 1
		 * when the checksum has already been fixed.  If
		 * the checksum is still wrong and this bit is a
		 * 1, we need to return bad checksum.  Otherwise,
		 * we need to set this bit to a 1 and update the
		 * checksum.
		 */
		ret_val = nvm->ops.read(hw, 0x23, 1, &data);
		if (ret_val)
			return ret_val;

		if (!(data & 0x8000)) {
			data |= 0x8000;
			ret_val = nvm->ops.write(hw, 0x23, 1, &data);
			if (ret_val)
				return ret_val;
			ret_val = nvm->ops.update(hw);
			if (ret_val)
				return ret_val;
		}
	}

	return E1000_SUCCESS;
}


/**
 *  e1000_read_mac_addr_82571 - Read device MAC address
 *  @hw: pointer to the HW structure
 **/
static s32 e1000_read_mac_addr_82571(struct e1000_hw *hw)
{
	DEBUGFUNC("e1000_read_mac_addr_82571");

	if (hw->mac.type == e1000_82571) {
		s32 ret_val;

		/* If there's an alternate MAC address place it in RAR0
		 * so that it will override the Si installed default perm
		 * address.
		 */
		ret_val = e1000_check_alt_mac_addr_generic(hw);
		if (ret_val)
			return ret_val;
	}

	return e1000_read_mac_addr_generic(hw);
}

/**
 * e1000_power_down_phy_copper_82571 - Remove link during PHY power down
 * @hw: pointer to the HW structure
 *
 * In the case of a PHY power down to save power, or to turn off link during a
 * driver unload, or wake on lan is not enabled, remove the link.
 **/
static void e1000_power_down_phy_copper_82571(struct e1000_hw *hw)
{
	struct e1000_phy_info *phy = &hw->phy;
	struct e1000_mac_info *mac = &hw->mac;

	if (!phy->ops.check_reset_block)
		return;

	/* If the management interface is not enabled, then power down */
	if (!(mac->ops.check_mng_mode(hw) || phy->ops.check_reset_block(hw)))
		e1000_power_down_phy_copper(hw);

	return;
}

/**
 *  e1000_clear_hw_cntrs_82571 - Clear device specific hardware counters
 *  @hw: pointer to the HW structure
 *
 *  Clears the hardware counters by reading the counter registers.
 **/
static void e1000_clear_hw_cntrs_82571(struct e1000_hw *hw)
{
	DEBUGFUNC("e1000_clear_hw_cntrs_82571");

	e1000_clear_hw_cntrs_base_generic(hw);

	E1000_READ_REG(hw, E1000_PRC64);
	E1000_READ_REG(hw, E1000_PRC127);
	E1000_READ_REG(hw, E1000_PRC255);
	E1000_READ_REG(hw, E1000_PRC511);
	E1000_READ_REG(hw, E1000_PRC1023);
	E1000_READ_REG(hw, E1000_PRC1522);
	E1000_READ_REG(hw, E1000_PTC64);
	E1000_READ_REG(hw, E1000_PTC127);
	E1000_READ_REG(hw, E1000_PTC255);
	E1000_READ_REG(hw, E1000_PTC511);
	E1000_READ_REG(hw, E1000_PTC1023);
	E1000_READ_REG(hw, E1000_PTC1522);

	E1000_READ_REG(hw, E1000_ALGNERRC);
	E1000_READ_REG(hw, E1000_RXERRC);
	E1000_READ_REG(hw, E1000_TNCRS);
	E1000_READ_REG(hw, E1000_CEXTERR);
	E1000_READ_REG(hw, E1000_TSCTC);
	E1000_READ_REG(hw, E1000_TSCTFC);

	E1000_READ_REG(hw, E1000_MGTPRC);
	E1000_READ_REG(hw, E1000_MGTPDC);
	E1000_READ_REG(hw, E1000_MGTPTC);

	E1000_READ_REG(hw, E1000_IAC);
	E1000_READ_REG(hw, E1000_ICRXOC);

	E1000_READ_REG(hw, E1000_ICRXPTC);
	E1000_READ_REG(hw, E1000_ICRXATC);
	E1000_READ_REG(hw, E1000_ICTXPTC);
	E1000_READ_REG(hw, E1000_ICTXATC);
	E1000_READ_REG(hw, E1000_ICTXQEC);
	E1000_READ_REG(hw, E1000_ICTXQMTC);
	E1000_READ_REG(hw, E1000_ICRXDMTC);
}
