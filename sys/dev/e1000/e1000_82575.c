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

/*
 * 82575EB Gigabit Network Connection
 * 82575EB Gigabit Backplane Connection
 * 82575GB Gigabit Network Connection
 * 82576 Gigabit Network Connection
 * 82576 Quad Port Gigabit Mezzanine Adapter
 * 82580 Gigabit Network Connection
 * I350 Gigabit Network Connection
 */

#include "e1000_api.h"
#include "e1000_i210.h"

static s32  e1000_init_phy_params_82575(struct e1000_hw *hw);
static s32  e1000_init_mac_params_82575(struct e1000_hw *hw);
static s32  e1000_acquire_phy_82575(struct e1000_hw *hw);
static void e1000_release_phy_82575(struct e1000_hw *hw);
static s32  e1000_acquire_nvm_82575(struct e1000_hw *hw);
static void e1000_release_nvm_82575(struct e1000_hw *hw);
static s32  e1000_check_for_link_82575(struct e1000_hw *hw);
static s32  e1000_check_for_link_media_swap(struct e1000_hw *hw);
static s32  e1000_get_cfg_done_82575(struct e1000_hw *hw);
static s32  e1000_get_link_up_info_82575(struct e1000_hw *hw, u16 *speed,
					 u16 *duplex);
static s32  e1000_phy_hw_reset_sgmii_82575(struct e1000_hw *hw);
static s32  e1000_read_phy_reg_sgmii_82575(struct e1000_hw *hw, u32 offset,
					   u16 *data);
static s32  e1000_reset_hw_82575(struct e1000_hw *hw);
static s32  e1000_reset_hw_82580(struct e1000_hw *hw);
static s32  e1000_read_phy_reg_82580(struct e1000_hw *hw,
				     u32 offset, u16 *data);
static s32  e1000_write_phy_reg_82580(struct e1000_hw *hw,
				      u32 offset, u16 data);
static s32  e1000_set_d0_lplu_state_82580(struct e1000_hw *hw,
					  bool active);
static s32  e1000_set_d3_lplu_state_82580(struct e1000_hw *hw,
					  bool active);
static s32  e1000_set_d0_lplu_state_82575(struct e1000_hw *hw,
					  bool active);
static s32  e1000_setup_copper_link_82575(struct e1000_hw *hw);
static s32  e1000_setup_serdes_link_82575(struct e1000_hw *hw);
static s32  e1000_get_media_type_82575(struct e1000_hw *hw);
static s32  e1000_set_sfp_media_type_82575(struct e1000_hw *hw);
static s32  e1000_valid_led_default_82575(struct e1000_hw *hw, u16 *data);
static s32  e1000_write_phy_reg_sgmii_82575(struct e1000_hw *hw,
					    u32 offset, u16 data);
static void e1000_clear_hw_cntrs_82575(struct e1000_hw *hw);
static s32  e1000_get_pcs_speed_and_duplex_82575(struct e1000_hw *hw,
						 u16 *speed, u16 *duplex);
static s32  e1000_get_phy_id_82575(struct e1000_hw *hw);
static bool e1000_sgmii_active_82575(struct e1000_hw *hw);
static s32  e1000_reset_init_script_82575(struct e1000_hw *hw);
static s32  e1000_read_mac_addr_82575(struct e1000_hw *hw);
static void e1000_config_collision_dist_82575(struct e1000_hw *hw);
static void e1000_power_down_phy_copper_82575(struct e1000_hw *hw);
static void e1000_shutdown_serdes_link_82575(struct e1000_hw *hw);
static void e1000_power_up_serdes_link_82575(struct e1000_hw *hw);
static s32 e1000_set_pcie_completion_timeout(struct e1000_hw *hw);
static s32 e1000_reset_mdicnfg_82580(struct e1000_hw *hw);
static s32 e1000_validate_nvm_checksum_82580(struct e1000_hw *hw);
static s32 e1000_update_nvm_checksum_82580(struct e1000_hw *hw);
static s32 e1000_update_nvm_checksum_with_offset(struct e1000_hw *hw,
						 u16 offset);
static s32 e1000_validate_nvm_checksum_with_offset(struct e1000_hw *hw,
						   u16 offset);
static s32 e1000_validate_nvm_checksum_i350(struct e1000_hw *hw);
static s32 e1000_update_nvm_checksum_i350(struct e1000_hw *hw);
static void e1000_clear_vfta_i350(struct e1000_hw *hw);

static void e1000_i2c_start(struct e1000_hw *hw);
static void e1000_i2c_stop(struct e1000_hw *hw);
static s32 e1000_clock_in_i2c_byte(struct e1000_hw *hw, u8 *data);
static s32 e1000_clock_out_i2c_byte(struct e1000_hw *hw, u8 data);
static s32 e1000_get_i2c_ack(struct e1000_hw *hw);
static s32 e1000_clock_in_i2c_bit(struct e1000_hw *hw, bool *data);
static s32 e1000_clock_out_i2c_bit(struct e1000_hw *hw, bool data);
static void e1000_raise_i2c_clk(struct e1000_hw *hw, u32 *i2cctl);
static void e1000_lower_i2c_clk(struct e1000_hw *hw, u32 *i2cctl);
static s32 e1000_set_i2c_data(struct e1000_hw *hw, u32 *i2cctl, bool data);
static bool e1000_get_i2c_data(u32 *i2cctl);

static const u16 e1000_82580_rxpbs_table[] = {
	36, 72, 144, 1, 2, 4, 8, 16, 35, 70, 140 };
#define E1000_82580_RXPBS_TABLE_SIZE \
	(sizeof(e1000_82580_rxpbs_table) / \
	 sizeof(e1000_82580_rxpbs_table[0]))


/**
 *  e1000_sgmii_uses_mdio_82575 - Determine if I2C pins are for external MDIO
 *  @hw: pointer to the HW structure
 *
 *  Called to determine if the I2C pins are being used for I2C or as an
 *  external MDIO interface since the two options are mutually exclusive.
 **/
static bool e1000_sgmii_uses_mdio_82575(struct e1000_hw *hw)
{
	u32 reg = 0;
	bool ext_mdio = FALSE;

	DEBUGFUNC("e1000_sgmii_uses_mdio_82575");

	switch (hw->mac.type) {
	case e1000_82575:
	case e1000_82576:
		reg = E1000_READ_REG(hw, E1000_MDIC);
		ext_mdio = !!(reg & E1000_MDIC_DEST);
		break;
	case e1000_82580:
	case e1000_i350:
	case e1000_i354:
	case e1000_i210:
	case e1000_i211:
		reg = E1000_READ_REG(hw, E1000_MDICNFG);
		ext_mdio = !!(reg & E1000_MDICNFG_EXT_MDIO);
		break;
	default:
		break;
	}
	return ext_mdio;
}

/**
 *  e1000_init_phy_params_82575 - Init PHY func ptrs.
 *  @hw: pointer to the HW structure
 **/
static s32 e1000_init_phy_params_82575(struct e1000_hw *hw)
{
	struct e1000_phy_info *phy = &hw->phy;
	s32 ret_val = E1000_SUCCESS;
	u32 ctrl_ext;

	DEBUGFUNC("e1000_init_phy_params_82575");

	phy->ops.read_i2c_byte = e1000_read_i2c_byte_generic;
	phy->ops.write_i2c_byte = e1000_write_i2c_byte_generic;

	if (hw->phy.media_type != e1000_media_type_copper) {
		phy->type = e1000_phy_none;
		goto out;
	}

	phy->ops.power_up   = e1000_power_up_phy_copper;
	phy->ops.power_down = e1000_power_down_phy_copper_82575;

	phy->autoneg_mask	= AUTONEG_ADVERTISE_SPEED_DEFAULT;
	phy->reset_delay_us	= 100;

	phy->ops.acquire	= e1000_acquire_phy_82575;
	phy->ops.check_reset_block = e1000_check_reset_block_generic;
	phy->ops.commit		= e1000_phy_sw_reset_generic;
	phy->ops.get_cfg_done	= e1000_get_cfg_done_82575;
	phy->ops.release	= e1000_release_phy_82575;

	ctrl_ext = E1000_READ_REG(hw, E1000_CTRL_EXT);

	if (e1000_sgmii_active_82575(hw)) {
		phy->ops.reset = e1000_phy_hw_reset_sgmii_82575;
		ctrl_ext |= E1000_CTRL_I2C_ENA;
	} else {
		phy->ops.reset = e1000_phy_hw_reset_generic;
		ctrl_ext &= ~E1000_CTRL_I2C_ENA;
	}

	E1000_WRITE_REG(hw, E1000_CTRL_EXT, ctrl_ext);
	e1000_reset_mdicnfg_82580(hw);

	if (e1000_sgmii_active_82575(hw) && !e1000_sgmii_uses_mdio_82575(hw)) {
		phy->ops.read_reg = e1000_read_phy_reg_sgmii_82575;
		phy->ops.write_reg = e1000_write_phy_reg_sgmii_82575;
	} else {
		switch (hw->mac.type) {
		case e1000_82580:
		case e1000_i350:
		case e1000_i354:
			phy->ops.read_reg = e1000_read_phy_reg_82580;
			phy->ops.write_reg = e1000_write_phy_reg_82580;
			break;
		case e1000_i210:
		case e1000_i211:
			phy->ops.read_reg = e1000_read_phy_reg_gs40g;
			phy->ops.write_reg = e1000_write_phy_reg_gs40g;
			break;
		default:
			phy->ops.read_reg = e1000_read_phy_reg_igp;
			phy->ops.write_reg = e1000_write_phy_reg_igp;
		}
	}

	/* Set phy->phy_addr and phy->id. */
	ret_val = e1000_get_phy_id_82575(hw);

	/* Verify phy id and set remaining function pointers */
	switch (phy->id) {
	case M88E1543_E_PHY_ID:
	case M88E1512_E_PHY_ID:
	case I347AT4_E_PHY_ID:
	case M88E1112_E_PHY_ID:
	case M88E1340M_E_PHY_ID:
	case M88E1111_I_PHY_ID:
		phy->type		= e1000_phy_m88;
		phy->ops.check_polarity	= e1000_check_polarity_m88;
		phy->ops.get_info	= e1000_get_phy_info_m88;
		if (phy->id == I347AT4_E_PHY_ID ||
		    phy->id == M88E1112_E_PHY_ID ||
		    phy->id == M88E1340M_E_PHY_ID)
			phy->ops.get_cable_length =
					 e1000_get_cable_length_m88_gen2;
		else if (phy->id == M88E1543_E_PHY_ID ||
			 phy->id == M88E1512_E_PHY_ID)
			phy->ops.get_cable_length =
					 e1000_get_cable_length_m88_gen2;
		else
			phy->ops.get_cable_length = e1000_get_cable_length_m88;
		phy->ops.force_speed_duplex = e1000_phy_force_speed_duplex_m88;
		/* Check if this PHY is confgured for media swap. */
		if (phy->id == M88E1112_E_PHY_ID) {
			u16 data;

			ret_val = phy->ops.write_reg(hw,
						     E1000_M88E1112_PAGE_ADDR,
						     2);
			if (ret_val)
				goto out;

			ret_val = phy->ops.read_reg(hw,
						    E1000_M88E1112_MAC_CTRL_1,
						    &data);
			if (ret_val)
				goto out;

			data = (data & E1000_M88E1112_MAC_CTRL_1_MODE_MASK) >>
			       E1000_M88E1112_MAC_CTRL_1_MODE_SHIFT;
			if (data == E1000_M88E1112_AUTO_COPPER_SGMII ||
			    data == E1000_M88E1112_AUTO_COPPER_BASEX)
				hw->mac.ops.check_for_link =
						e1000_check_for_link_media_swap;
		}
		if (phy->id == M88E1512_E_PHY_ID) {
			ret_val = e1000_initialize_M88E1512_phy(hw);
			if (ret_val)
				goto out;
		}
		if (phy->id == M88E1543_E_PHY_ID) {
			ret_val = e1000_initialize_M88E1543_phy(hw);
			if (ret_val)
				goto out;
		}
		break;
	case IGP03E1000_E_PHY_ID:
	case IGP04E1000_E_PHY_ID:
		phy->type = e1000_phy_igp_3;
		phy->ops.check_polarity = e1000_check_polarity_igp;
		phy->ops.get_info = e1000_get_phy_info_igp;
		phy->ops.get_cable_length = e1000_get_cable_length_igp_2;
		phy->ops.force_speed_duplex = e1000_phy_force_speed_duplex_igp;
		phy->ops.set_d0_lplu_state = e1000_set_d0_lplu_state_82575;
		phy->ops.set_d3_lplu_state = e1000_set_d3_lplu_state_generic;
		break;
	case I82580_I_PHY_ID:
	case I350_I_PHY_ID:
		phy->type = e1000_phy_82580;
		phy->ops.check_polarity = e1000_check_polarity_82577;
		phy->ops.force_speed_duplex =
					 e1000_phy_force_speed_duplex_82577;
		phy->ops.get_cable_length = e1000_get_cable_length_82577;
		phy->ops.get_info = e1000_get_phy_info_82577;
		phy->ops.set_d0_lplu_state = e1000_set_d0_lplu_state_82580;
		phy->ops.set_d3_lplu_state = e1000_set_d3_lplu_state_82580;
		break;
	case I210_I_PHY_ID:
		phy->type		= e1000_phy_i210;
		phy->ops.check_polarity	= e1000_check_polarity_m88;
		phy->ops.get_info	= e1000_get_phy_info_m88;
		phy->ops.get_cable_length = e1000_get_cable_length_m88_gen2;
		phy->ops.set_d0_lplu_state = e1000_set_d0_lplu_state_82580;
		phy->ops.set_d3_lplu_state = e1000_set_d3_lplu_state_82580;
		phy->ops.force_speed_duplex = e1000_phy_force_speed_duplex_m88;
		break;
	default:
		ret_val = -E1000_ERR_PHY;
		goto out;
	}

out:
	return ret_val;
}

/**
 *  e1000_init_nvm_params_82575 - Init NVM func ptrs.
 *  @hw: pointer to the HW structure
 **/
s32 e1000_init_nvm_params_82575(struct e1000_hw *hw)
{
	struct e1000_nvm_info *nvm = &hw->nvm;
	u32 eecd = E1000_READ_REG(hw, E1000_EECD);
	u16 size;

	DEBUGFUNC("e1000_init_nvm_params_82575");

	size = (u16)((eecd & E1000_EECD_SIZE_EX_MASK) >>
		     E1000_EECD_SIZE_EX_SHIFT);
	/*
	 * Added to a constant, "size" becomes the left-shift value
	 * for setting word_size.
	 */
	size += NVM_WORD_SIZE_BASE_SHIFT;

	/* Just in case size is out of range, cap it to the largest
	 * EEPROM size supported
	 */
	if (size > 15)
		size = 15;

	nvm->word_size = 1 << size;
	if (hw->mac.type < e1000_i210) {
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
			nvm->address_bits = eecd & E1000_EECD_ADDR_BITS ?
					    16 : 8;
			break;
		}
		if (nvm->word_size == (1 << 15))
			nvm->page_size = 128;

		nvm->type = e1000_nvm_eeprom_spi;
	} else {
		nvm->type = e1000_nvm_flash_hw;
	}

	/* Function Pointers */
	nvm->ops.acquire = e1000_acquire_nvm_82575;
	nvm->ops.release = e1000_release_nvm_82575;
	if (nvm->word_size < (1 << 15))
		nvm->ops.read = e1000_read_nvm_eerd;
	else
		nvm->ops.read = e1000_read_nvm_spi;

	nvm->ops.write = e1000_write_nvm_spi;
	nvm->ops.validate = e1000_validate_nvm_checksum_generic;
	nvm->ops.update = e1000_update_nvm_checksum_generic;
	nvm->ops.valid_led_default = e1000_valid_led_default_82575;

	/* override generic family function pointers for specific descendants */
	switch (hw->mac.type) {
	case e1000_82580:
		nvm->ops.validate = e1000_validate_nvm_checksum_82580;
		nvm->ops.update = e1000_update_nvm_checksum_82580;
		break;
	case e1000_i350:
	case e1000_i354:
		nvm->ops.validate = e1000_validate_nvm_checksum_i350;
		nvm->ops.update = e1000_update_nvm_checksum_i350;
		break;
	default:
		break;
	}

	return E1000_SUCCESS;
}

/**
 *  e1000_init_mac_params_82575 - Init MAC func ptrs.
 *  @hw: pointer to the HW structure
 **/
static s32 e1000_init_mac_params_82575(struct e1000_hw *hw)
{
	struct e1000_mac_info *mac = &hw->mac;
	struct e1000_dev_spec_82575 *dev_spec = &hw->dev_spec._82575;

	DEBUGFUNC("e1000_init_mac_params_82575");

	/* Derives media type */
	e1000_get_media_type_82575(hw);
	/* Set mta register count */
	mac->mta_reg_count = 128;
	/* Set uta register count */
	mac->uta_reg_count = (hw->mac.type == e1000_82575) ? 0 : 128;
	/* Set rar entry count */
	mac->rar_entry_count = E1000_RAR_ENTRIES_82575;
	if (mac->type == e1000_82576)
		mac->rar_entry_count = E1000_RAR_ENTRIES_82576;
	if (mac->type == e1000_82580)
		mac->rar_entry_count = E1000_RAR_ENTRIES_82580;
	if (mac->type == e1000_i350 || mac->type == e1000_i354)
		mac->rar_entry_count = E1000_RAR_ENTRIES_I350;

	/* Enable EEE default settings for EEE supported devices */
	if (mac->type >= e1000_i350)
		dev_spec->eee_disable = FALSE;

	/* Allow a single clear of the SW semaphore on I210 and newer */
	if (mac->type >= e1000_i210)
		dev_spec->clear_semaphore_once = TRUE;

	/* Set if part includes ASF firmware */
	mac->asf_firmware_present = TRUE;
	/* FWSM register */
	mac->has_fwsm = TRUE;
	/* ARC supported; valid only if manageability features are enabled. */
	mac->arc_subsystem_valid =
		!!(E1000_READ_REG(hw, E1000_FWSM) & E1000_FWSM_MODE_MASK);

	/* Function pointers */

	/* bus type/speed/width */
	mac->ops.get_bus_info = e1000_get_bus_info_pcie_generic;
	/* reset */
	if (mac->type >= e1000_82580)
		mac->ops.reset_hw = e1000_reset_hw_82580;
	else
	mac->ops.reset_hw = e1000_reset_hw_82575;
	/* hw initialization */
	if ((mac->type == e1000_i210) || (mac->type == e1000_i211))
		mac->ops.init_hw = e1000_init_hw_i210;
	else
	mac->ops.init_hw = e1000_init_hw_82575;
	/* link setup */
	mac->ops.setup_link = e1000_setup_link_generic;
	/* physical interface link setup */
	mac->ops.setup_physical_interface =
		(hw->phy.media_type == e1000_media_type_copper)
		? e1000_setup_copper_link_82575 : e1000_setup_serdes_link_82575;
	/* physical interface shutdown */
	mac->ops.shutdown_serdes = e1000_shutdown_serdes_link_82575;
	/* physical interface power up */
	mac->ops.power_up_serdes = e1000_power_up_serdes_link_82575;
	/* check for link */
	mac->ops.check_for_link = e1000_check_for_link_82575;
	/* read mac address */
	mac->ops.read_mac_addr = e1000_read_mac_addr_82575;
	/* configure collision distance */
	mac->ops.config_collision_dist = e1000_config_collision_dist_82575;
	/* multicast address update */
	mac->ops.update_mc_addr_list = e1000_update_mc_addr_list_generic;
	if (hw->mac.type == e1000_i350 || mac->type == e1000_i354) {
		/* writing VFTA */
		mac->ops.write_vfta = e1000_write_vfta_i350;
		/* clearing VFTA */
		mac->ops.clear_vfta = e1000_clear_vfta_i350;
	} else {
		/* writing VFTA */
		mac->ops.write_vfta = e1000_write_vfta_generic;
		/* clearing VFTA */
		mac->ops.clear_vfta = e1000_clear_vfta_generic;
	}
	if (hw->mac.type >= e1000_82580)
		mac->ops.validate_mdi_setting =
				e1000_validate_mdi_setting_crossover_generic;
	/* ID LED init */
	mac->ops.id_led_init = e1000_id_led_init_generic;
	/* blink LED */
	mac->ops.blink_led = e1000_blink_led_generic;
	/* setup LED */
	mac->ops.setup_led = e1000_setup_led_generic;
	/* cleanup LED */
	mac->ops.cleanup_led = e1000_cleanup_led_generic;
	/* turn on/off LED */
	mac->ops.led_on = e1000_led_on_generic;
	mac->ops.led_off = e1000_led_off_generic;
	/* clear hardware counters */
	mac->ops.clear_hw_cntrs = e1000_clear_hw_cntrs_82575;
	/* link info */
	mac->ops.get_link_up_info = e1000_get_link_up_info_82575;
	/* acquire SW_FW sync */
	mac->ops.acquire_swfw_sync = e1000_acquire_swfw_sync;
	mac->ops.release_swfw_sync = e1000_release_swfw_sync;

	/* set lan id for port to determine which phy lock to use */
	hw->mac.ops.set_lan_id(hw);

	return E1000_SUCCESS;
}

/**
 *  e1000_init_function_pointers_82575 - Init func ptrs.
 *  @hw: pointer to the HW structure
 *
 *  Called to initialize all function pointers and parameters.
 **/
void e1000_init_function_pointers_82575(struct e1000_hw *hw)
{
	DEBUGFUNC("e1000_init_function_pointers_82575");

	hw->mac.ops.init_params = e1000_init_mac_params_82575;
	hw->nvm.ops.init_params = e1000_init_nvm_params_82575;
	hw->phy.ops.init_params = e1000_init_phy_params_82575;
	hw->mbx.ops.init_params = e1000_init_mbx_params_pf;
}

/**
 *  e1000_acquire_phy_82575 - Acquire rights to access PHY
 *  @hw: pointer to the HW structure
 *
 *  Acquire access rights to the correct PHY.
 **/
static s32 e1000_acquire_phy_82575(struct e1000_hw *hw)
{
	u16 mask = E1000_SWFW_PHY0_SM;

	DEBUGFUNC("e1000_acquire_phy_82575");

	if (hw->bus.func == E1000_FUNC_1)
		mask = E1000_SWFW_PHY1_SM;
	else if (hw->bus.func == E1000_FUNC_2)
		mask = E1000_SWFW_PHY2_SM;
	else if (hw->bus.func == E1000_FUNC_3)
		mask = E1000_SWFW_PHY3_SM;

	return hw->mac.ops.acquire_swfw_sync(hw, mask);
}

/**
 *  e1000_release_phy_82575 - Release rights to access PHY
 *  @hw: pointer to the HW structure
 *
 *  A wrapper to release access rights to the correct PHY.
 **/
static void e1000_release_phy_82575(struct e1000_hw *hw)
{
	u16 mask = E1000_SWFW_PHY0_SM;

	DEBUGFUNC("e1000_release_phy_82575");

	if (hw->bus.func == E1000_FUNC_1)
		mask = E1000_SWFW_PHY1_SM;
	else if (hw->bus.func == E1000_FUNC_2)
		mask = E1000_SWFW_PHY2_SM;
	else if (hw->bus.func == E1000_FUNC_3)
		mask = E1000_SWFW_PHY3_SM;

	hw->mac.ops.release_swfw_sync(hw, mask);
}

/**
 *  e1000_read_phy_reg_sgmii_82575 - Read PHY register using sgmii
 *  @hw: pointer to the HW structure
 *  @offset: register offset to be read
 *  @data: pointer to the read data
 *
 *  Reads the PHY register at offset using the serial gigabit media independent
 *  interface and stores the retrieved information in data.
 **/
static s32 e1000_read_phy_reg_sgmii_82575(struct e1000_hw *hw, u32 offset,
					  u16 *data)
{
	s32 ret_val = -E1000_ERR_PARAM;

	DEBUGFUNC("e1000_read_phy_reg_sgmii_82575");

	if (offset > E1000_MAX_SGMII_PHY_REG_ADDR) {
		DEBUGOUT1("PHY Address %u is out of range\n", offset);
		goto out;
	}

	ret_val = hw->phy.ops.acquire(hw);
	if (ret_val)
		goto out;

	ret_val = e1000_read_phy_reg_i2c(hw, offset, data);

	hw->phy.ops.release(hw);

out:
	return ret_val;
}

/**
 *  e1000_write_phy_reg_sgmii_82575 - Write PHY register using sgmii
 *  @hw: pointer to the HW structure
 *  @offset: register offset to write to
 *  @data: data to write at register offset
 *
 *  Writes the data to PHY register at the offset using the serial gigabit
 *  media independent interface.
 **/
static s32 e1000_write_phy_reg_sgmii_82575(struct e1000_hw *hw, u32 offset,
					   u16 data)
{
	s32 ret_val = -E1000_ERR_PARAM;

	DEBUGFUNC("e1000_write_phy_reg_sgmii_82575");

	if (offset > E1000_MAX_SGMII_PHY_REG_ADDR) {
		DEBUGOUT1("PHY Address %d is out of range\n", offset);
		goto out;
	}

	ret_val = hw->phy.ops.acquire(hw);
	if (ret_val)
		goto out;

	ret_val = e1000_write_phy_reg_i2c(hw, offset, data);

	hw->phy.ops.release(hw);

out:
	return ret_val;
}

/**
 *  e1000_get_phy_id_82575 - Retrieve PHY addr and id
 *  @hw: pointer to the HW structure
 *
 *  Retrieves the PHY address and ID for both PHY's which do and do not use
 *  sgmi interface.
 **/
static s32 e1000_get_phy_id_82575(struct e1000_hw *hw)
{
	struct e1000_phy_info *phy = &hw->phy;
	s32  ret_val = E1000_SUCCESS;
	u16 phy_id;
	u32 ctrl_ext;
	u32 mdic;

	DEBUGFUNC("e1000_get_phy_id_82575");

	/* some i354 devices need an extra read for phy id */
	if (hw->mac.type == e1000_i354)
		e1000_get_phy_id(hw);

	/*
	 * For SGMII PHYs, we try the list of possible addresses until
	 * we find one that works.  For non-SGMII PHYs
	 * (e.g. integrated copper PHYs), an address of 1 should
	 * work.  The result of this function should mean phy->phy_addr
	 * and phy->id are set correctly.
	 */
	if (!e1000_sgmii_active_82575(hw)) {
		phy->addr = 1;
		ret_val = e1000_get_phy_id(hw);
		goto out;
	}

	if (e1000_sgmii_uses_mdio_82575(hw)) {
		switch (hw->mac.type) {
		case e1000_82575:
		case e1000_82576:
			mdic = E1000_READ_REG(hw, E1000_MDIC);
			mdic &= E1000_MDIC_PHY_MASK;
			phy->addr = mdic >> E1000_MDIC_PHY_SHIFT;
			break;
		case e1000_82580:
		case e1000_i350:
		case e1000_i354:
		case e1000_i210:
		case e1000_i211:
			mdic = E1000_READ_REG(hw, E1000_MDICNFG);
			mdic &= E1000_MDICNFG_PHY_MASK;
			phy->addr = mdic >> E1000_MDICNFG_PHY_SHIFT;
			break;
		default:
			ret_val = -E1000_ERR_PHY;
			goto out;
			break;
		}
		ret_val = e1000_get_phy_id(hw);
		goto out;
	}

	/* Power on sgmii phy if it is disabled */
	ctrl_ext = E1000_READ_REG(hw, E1000_CTRL_EXT);
	E1000_WRITE_REG(hw, E1000_CTRL_EXT,
			ctrl_ext & ~E1000_CTRL_EXT_SDP3_DATA);
	E1000_WRITE_FLUSH(hw);
	msec_delay(300);

	/*
	 * The address field in the I2CCMD register is 3 bits and 0 is invalid.
	 * Therefore, we need to test 1-7
	 */
	for (phy->addr = 1; phy->addr < 8; phy->addr++) {
		ret_val = e1000_read_phy_reg_sgmii_82575(hw, PHY_ID1, &phy_id);
		if (ret_val == E1000_SUCCESS) {
			DEBUGOUT2("Vendor ID 0x%08X read at address %u\n",
				  phy_id, phy->addr);
			/*
			 * At the time of this writing, The M88 part is
			 * the only supported SGMII PHY product.
			 */
			if (phy_id == M88_VENDOR)
				break;
		} else {
			DEBUGOUT1("PHY address %u was unreadable\n",
				  phy->addr);
		}
	}

	/* A valid PHY type couldn't be found. */
	if (phy->addr == 8) {
		phy->addr = 0;
		ret_val = -E1000_ERR_PHY;
	} else {
		ret_val = e1000_get_phy_id(hw);
	}

	/* restore previous sfp cage power state */
	E1000_WRITE_REG(hw, E1000_CTRL_EXT, ctrl_ext);

out:
	return ret_val;
}

/**
 *  e1000_phy_hw_reset_sgmii_82575 - Performs a PHY reset
 *  @hw: pointer to the HW structure
 *
 *  Resets the PHY using the serial gigabit media independent interface.
 **/
static s32 e1000_phy_hw_reset_sgmii_82575(struct e1000_hw *hw)
{
	s32 ret_val = E1000_SUCCESS;
	struct e1000_phy_info *phy = &hw->phy;

	DEBUGFUNC("e1000_phy_hw_reset_sgmii_82575");

	/*
	 * This isn't a TRUE "hard" reset, but is the only reset
	 * available to us at this time.
	 */

	DEBUGOUT("Soft resetting SGMII attached PHY...\n");

	if (!(hw->phy.ops.write_reg))
		goto out;

	/*
	 * SFP documentation requires the following to configure the SPF module
	 * to work on SGMII.  No further documentation is given.
	 */
	ret_val = hw->phy.ops.write_reg(hw, 0x1B, 0x8084);
	if (ret_val)
		goto out;

	ret_val = hw->phy.ops.commit(hw);
	if (ret_val)
		goto out;

	if (phy->id == M88E1512_E_PHY_ID)
		ret_val = e1000_initialize_M88E1512_phy(hw);
out:
	return ret_val;
}

/**
 *  e1000_set_d0_lplu_state_82575 - Set Low Power Linkup D0 state
 *  @hw: pointer to the HW structure
 *  @active: TRUE to enable LPLU, FALSE to disable
 *
 *  Sets the LPLU D0 state according to the active flag.  When
 *  activating LPLU this function also disables smart speed
 *  and vice versa.  LPLU will not be activated unless the
 *  device autonegotiation advertisement meets standards of
 *  either 10 or 10/100 or 10/100/1000 at all duplexes.
 *  This is a function pointer entry point only called by
 *  PHY setup routines.
 **/
static s32 e1000_set_d0_lplu_state_82575(struct e1000_hw *hw, bool active)
{
	struct e1000_phy_info *phy = &hw->phy;
	s32 ret_val = E1000_SUCCESS;
	u16 data;

	DEBUGFUNC("e1000_set_d0_lplu_state_82575");

	if (!(hw->phy.ops.read_reg))
		goto out;

	ret_val = phy->ops.read_reg(hw, IGP02E1000_PHY_POWER_MGMT, &data);
	if (ret_val)
		goto out;

	if (active) {
		data |= IGP02E1000_PM_D0_LPLU;
		ret_val = phy->ops.write_reg(hw, IGP02E1000_PHY_POWER_MGMT,
					     data);
		if (ret_val)
			goto out;

		/* When LPLU is enabled, we should disable SmartSpeed */
		ret_val = phy->ops.read_reg(hw, IGP01E1000_PHY_PORT_CONFIG,
					    &data);
		data &= ~IGP01E1000_PSCFR_SMART_SPEED;
		ret_val = phy->ops.write_reg(hw, IGP01E1000_PHY_PORT_CONFIG,
					     data);
		if (ret_val)
			goto out;
	} else {
		data &= ~IGP02E1000_PM_D0_LPLU;
		ret_val = phy->ops.write_reg(hw, IGP02E1000_PHY_POWER_MGMT,
					     data);
		/*
		 * LPLU and SmartSpeed are mutually exclusive.  LPLU is used
		 * during Dx states where the power conservation is most
		 * important.  During driver activity we should enable
		 * SmartSpeed, so performance is maintained.
		 */
		if (phy->smart_speed == e1000_smart_speed_on) {
			ret_val = phy->ops.read_reg(hw,
						    IGP01E1000_PHY_PORT_CONFIG,
						    &data);
			if (ret_val)
				goto out;

			data |= IGP01E1000_PSCFR_SMART_SPEED;
			ret_val = phy->ops.write_reg(hw,
						     IGP01E1000_PHY_PORT_CONFIG,
						     data);
			if (ret_val)
				goto out;
		} else if (phy->smart_speed == e1000_smart_speed_off) {
			ret_val = phy->ops.read_reg(hw,
						    IGP01E1000_PHY_PORT_CONFIG,
						    &data);
			if (ret_val)
				goto out;

			data &= ~IGP01E1000_PSCFR_SMART_SPEED;
			ret_val = phy->ops.write_reg(hw,
						     IGP01E1000_PHY_PORT_CONFIG,
						     data);
			if (ret_val)
				goto out;
		}
	}

out:
	return ret_val;
}

/**
 *  e1000_set_d0_lplu_state_82580 - Set Low Power Linkup D0 state
 *  @hw: pointer to the HW structure
 *  @active: TRUE to enable LPLU, FALSE to disable
 *
 *  Sets the LPLU D0 state according to the active flag.  When
 *  activating LPLU this function also disables smart speed
 *  and vice versa.  LPLU will not be activated unless the
 *  device autonegotiation advertisement meets standards of
 *  either 10 or 10/100 or 10/100/1000 at all duplexes.
 *  This is a function pointer entry point only called by
 *  PHY setup routines.
 **/
static s32 e1000_set_d0_lplu_state_82580(struct e1000_hw *hw, bool active)
{
	struct e1000_phy_info *phy = &hw->phy;
	u32 data;

	DEBUGFUNC("e1000_set_d0_lplu_state_82580");

	data = E1000_READ_REG(hw, E1000_82580_PHY_POWER_MGMT);

	if (active) {
		data |= E1000_82580_PM_D0_LPLU;

		/* When LPLU is enabled, we should disable SmartSpeed */
		data &= ~E1000_82580_PM_SPD;
	} else {
		data &= ~E1000_82580_PM_D0_LPLU;

		/*
		 * LPLU and SmartSpeed are mutually exclusive.  LPLU is used
		 * during Dx states where the power conservation is most
		 * important.  During driver activity we should enable
		 * SmartSpeed, so performance is maintained.
		 */
		if (phy->smart_speed == e1000_smart_speed_on)
			data |= E1000_82580_PM_SPD;
		else if (phy->smart_speed == e1000_smart_speed_off)
			data &= ~E1000_82580_PM_SPD;
	}

	E1000_WRITE_REG(hw, E1000_82580_PHY_POWER_MGMT, data);
	return E1000_SUCCESS;
}

/**
 *  e1000_set_d3_lplu_state_82580 - Sets low power link up state for D3
 *  @hw: pointer to the HW structure
 *  @active: boolean used to enable/disable lplu
 *
 *  Success returns 0, Failure returns 1
 *
 *  The low power link up (lplu) state is set to the power management level D3
 *  and SmartSpeed is disabled when active is TRUE, else clear lplu for D3
 *  and enable Smartspeed.  LPLU and Smartspeed are mutually exclusive.  LPLU
 *  is used during Dx states where the power conservation is most important.
 *  During driver activity, SmartSpeed should be enabled so performance is
 *  maintained.
 **/
s32 e1000_set_d3_lplu_state_82580(struct e1000_hw *hw, bool active)
{
	struct e1000_phy_info *phy = &hw->phy;
	u32 data;

	DEBUGFUNC("e1000_set_d3_lplu_state_82580");

	data = E1000_READ_REG(hw, E1000_82580_PHY_POWER_MGMT);

	if (!active) {
		data &= ~E1000_82580_PM_D3_LPLU;
		/*
		 * LPLU and SmartSpeed are mutually exclusive.  LPLU is used
		 * during Dx states where the power conservation is most
		 * important.  During driver activity we should enable
		 * SmartSpeed, so performance is maintained.
		 */
		if (phy->smart_speed == e1000_smart_speed_on)
			data |= E1000_82580_PM_SPD;
		else if (phy->smart_speed == e1000_smart_speed_off)
			data &= ~E1000_82580_PM_SPD;
	} else if ((phy->autoneg_advertised == E1000_ALL_SPEED_DUPLEX) ||
		   (phy->autoneg_advertised == E1000_ALL_NOT_GIG) ||
		   (phy->autoneg_advertised == E1000_ALL_10_SPEED)) {
		data |= E1000_82580_PM_D3_LPLU;
		/* When LPLU is enabled, we should disable SmartSpeed */
		data &= ~E1000_82580_PM_SPD;
	}

	E1000_WRITE_REG(hw, E1000_82580_PHY_POWER_MGMT, data);
	return E1000_SUCCESS;
}

/**
 *  e1000_acquire_nvm_82575 - Request for access to EEPROM
 *  @hw: pointer to the HW structure
 *
 *  Acquire the necessary semaphores for exclusive access to the EEPROM.
 *  Set the EEPROM access request bit and wait for EEPROM access grant bit.
 *  Return successful if access grant bit set, else clear the request for
 *  EEPROM access and return -E1000_ERR_NVM (-1).
 **/
static s32 e1000_acquire_nvm_82575(struct e1000_hw *hw)
{
	s32 ret_val = E1000_SUCCESS;

	DEBUGFUNC("e1000_acquire_nvm_82575");

	ret_val = e1000_acquire_swfw_sync(hw, E1000_SWFW_EEP_SM);
	if (ret_val)
		goto out;

	/*
	 * Check if there is some access
	 * error this access may hook on
	 */
	if (hw->mac.type == e1000_i350) {
		u32 eecd = E1000_READ_REG(hw, E1000_EECD);
		if (eecd & (E1000_EECD_BLOCKED | E1000_EECD_ABORT |
		    E1000_EECD_TIMEOUT)) {
			/* Clear all access error flags */
			E1000_WRITE_REG(hw, E1000_EECD, eecd |
					E1000_EECD_ERROR_CLR);
			DEBUGOUT("Nvm bit banging access error detected and cleared.\n");
		}
	}

	if (hw->mac.type == e1000_82580) {
		u32 eecd = E1000_READ_REG(hw, E1000_EECD);
		if (eecd & E1000_EECD_BLOCKED) {
			/* Clear access error flag */
			E1000_WRITE_REG(hw, E1000_EECD, eecd |
					E1000_EECD_BLOCKED);
			DEBUGOUT("Nvm bit banging access error detected and cleared.\n");
		}
	}

	ret_val = e1000_acquire_nvm_generic(hw);
	if (ret_val)
		e1000_release_swfw_sync(hw, E1000_SWFW_EEP_SM);

out:
	return ret_val;
}

/**
 *  e1000_release_nvm_82575 - Release exclusive access to EEPROM
 *  @hw: pointer to the HW structure
 *
 *  Stop any current commands to the EEPROM and clear the EEPROM request bit,
 *  then release the semaphores acquired.
 **/
static void e1000_release_nvm_82575(struct e1000_hw *hw)
{
	DEBUGFUNC("e1000_release_nvm_82575");

	e1000_release_nvm_generic(hw);

	e1000_release_swfw_sync(hw, E1000_SWFW_EEP_SM);
}

/**
 *  e1000_get_cfg_done_82575 - Read config done bit
 *  @hw: pointer to the HW structure
 *
 *  Read the management control register for the config done bit for
 *  completion status.  NOTE: silicon which is EEPROM-less will fail trying
 *  to read the config done bit, so an error is *ONLY* logged and returns
 *  E1000_SUCCESS.  If we were to return with error, EEPROM-less silicon
 *  would not be able to be reset or change link.
 **/
static s32 e1000_get_cfg_done_82575(struct e1000_hw *hw)
{
	s32 timeout = PHY_CFG_TIMEOUT;
	u32 mask = E1000_NVM_CFG_DONE_PORT_0;

	DEBUGFUNC("e1000_get_cfg_done_82575");

	if (hw->bus.func == E1000_FUNC_1)
		mask = E1000_NVM_CFG_DONE_PORT_1;
	else if (hw->bus.func == E1000_FUNC_2)
		mask = E1000_NVM_CFG_DONE_PORT_2;
	else if (hw->bus.func == E1000_FUNC_3)
		mask = E1000_NVM_CFG_DONE_PORT_3;
	while (timeout) {
		if (E1000_READ_REG(hw, E1000_EEMNGCTL) & mask)
			break;
		msec_delay(1);
		timeout--;
	}
	if (!timeout)
		DEBUGOUT("MNG configuration cycle has not completed.\n");

	/* If EEPROM is not marked present, init the PHY manually */
	if (!(E1000_READ_REG(hw, E1000_EECD) & E1000_EECD_PRES) &&
	    (hw->phy.type == e1000_phy_igp_3))
		e1000_phy_init_script_igp3(hw);

	return E1000_SUCCESS;
}

/**
 *  e1000_get_link_up_info_82575 - Get link speed/duplex info
 *  @hw: pointer to the HW structure
 *  @speed: stores the current speed
 *  @duplex: stores the current duplex
 *
 *  This is a wrapper function, if using the serial gigabit media independent
 *  interface, use PCS to retrieve the link speed and duplex information.
 *  Otherwise, use the generic function to get the link speed and duplex info.
 **/
static s32 e1000_get_link_up_info_82575(struct e1000_hw *hw, u16 *speed,
					u16 *duplex)
{
	s32 ret_val;

	DEBUGFUNC("e1000_get_link_up_info_82575");

	if (hw->phy.media_type != e1000_media_type_copper)
		ret_val = e1000_get_pcs_speed_and_duplex_82575(hw, speed,
							       duplex);
	else
		ret_val = e1000_get_speed_and_duplex_copper_generic(hw, speed,
								    duplex);

	return ret_val;
}

/**
 *  e1000_check_for_link_82575 - Check for link
 *  @hw: pointer to the HW structure
 *
 *  If sgmii is enabled, then use the pcs register to determine link, otherwise
 *  use the generic interface for determining link.
 **/
static s32 e1000_check_for_link_82575(struct e1000_hw *hw)
{
	s32 ret_val;
	u16 speed, duplex;

	DEBUGFUNC("e1000_check_for_link_82575");

	if (hw->phy.media_type != e1000_media_type_copper) {
		ret_val = e1000_get_pcs_speed_and_duplex_82575(hw, &speed,
							       &duplex);
		/*
		 * Use this flag to determine if link needs to be checked or
		 * not.  If we have link clear the flag so that we do not
		 * continue to check for link.
		 */
		hw->mac.get_link_status = !hw->mac.serdes_has_link;

		/*
		 * Configure Flow Control now that Auto-Neg has completed.
		 * First, we need to restore the desired flow control
		 * settings because we may have had to re-autoneg with a
		 * different link partner.
		 */
		ret_val = e1000_config_fc_after_link_up_generic(hw);
		if (ret_val)
			DEBUGOUT("Error configuring flow control\n");
	} else {
		ret_val = e1000_check_for_copper_link_generic(hw);
	}

	return ret_val;
}

/**
 *  e1000_check_for_link_media_swap - Check which M88E1112 interface linked
 *  @hw: pointer to the HW structure
 *
 *  Poll the M88E1112 interfaces to see which interface achieved link.
 */
static s32 e1000_check_for_link_media_swap(struct e1000_hw *hw)
{
	struct e1000_phy_info *phy = &hw->phy;
	s32 ret_val;
	u16 data;
	u8 port = 0;

	DEBUGFUNC("e1000_check_for_link_media_swap");

	/* Check for copper. */
	ret_val = phy->ops.write_reg(hw, E1000_M88E1112_PAGE_ADDR, 0);
	if (ret_val)
		return ret_val;

	ret_val = phy->ops.read_reg(hw, E1000_M88E1112_STATUS, &data);
	if (ret_val)
		return ret_val;

	if (data & E1000_M88E1112_STATUS_LINK)
		port = E1000_MEDIA_PORT_COPPER;

	/* Check for other. */
	ret_val = phy->ops.write_reg(hw, E1000_M88E1112_PAGE_ADDR, 1);
	if (ret_val)
		return ret_val;

	ret_val = phy->ops.read_reg(hw, E1000_M88E1112_STATUS, &data);
	if (ret_val)
		return ret_val;

	if (data & E1000_M88E1112_STATUS_LINK)
		port = E1000_MEDIA_PORT_OTHER;

	/* Determine if a swap needs to happen. */
	if (port && (hw->dev_spec._82575.media_port != port)) {
		hw->dev_spec._82575.media_port = port;
		hw->dev_spec._82575.media_changed = TRUE;
	}

	if (port == E1000_MEDIA_PORT_COPPER) {
		/* reset page to 0 */
		ret_val = phy->ops.write_reg(hw, E1000_M88E1112_PAGE_ADDR, 0);
		if (ret_val)
			return ret_val;
		e1000_check_for_link_82575(hw);
	} else {
		e1000_check_for_link_82575(hw);
		/* reset page to 0 */
		ret_val = phy->ops.write_reg(hw, E1000_M88E1112_PAGE_ADDR, 0);
		if (ret_val)
			return ret_val;
	}

	return E1000_SUCCESS;
}

/**
 *  e1000_power_up_serdes_link_82575 - Power up the serdes link after shutdown
 *  @hw: pointer to the HW structure
 **/
static void e1000_power_up_serdes_link_82575(struct e1000_hw *hw)
{
	u32 reg;

	DEBUGFUNC("e1000_power_up_serdes_link_82575");

	if ((hw->phy.media_type != e1000_media_type_internal_serdes) &&
	    !e1000_sgmii_active_82575(hw))
		return;

	/* Enable PCS to turn on link */
	reg = E1000_READ_REG(hw, E1000_PCS_CFG0);
	reg |= E1000_PCS_CFG_PCS_EN;
	E1000_WRITE_REG(hw, E1000_PCS_CFG0, reg);

	/* Power up the laser */
	reg = E1000_READ_REG(hw, E1000_CTRL_EXT);
	reg &= ~E1000_CTRL_EXT_SDP3_DATA;
	E1000_WRITE_REG(hw, E1000_CTRL_EXT, reg);

	/* flush the write to verify completion */
	E1000_WRITE_FLUSH(hw);
	msec_delay(1);
}

/**
 *  e1000_get_pcs_speed_and_duplex_82575 - Retrieve current speed/duplex
 *  @hw: pointer to the HW structure
 *  @speed: stores the current speed
 *  @duplex: stores the current duplex
 *
 *  Using the physical coding sub-layer (PCS), retrieve the current speed and
 *  duplex, then store the values in the pointers provided.
 **/
static s32 e1000_get_pcs_speed_and_duplex_82575(struct e1000_hw *hw,
						u16 *speed, u16 *duplex)
{
	struct e1000_mac_info *mac = &hw->mac;
	u32 pcs;
	u32 status;

	DEBUGFUNC("e1000_get_pcs_speed_and_duplex_82575");

	/*
	 * Read the PCS Status register for link state. For non-copper mode,
	 * the status register is not accurate. The PCS status register is
	 * used instead.
	 */
	pcs = E1000_READ_REG(hw, E1000_PCS_LSTAT);

	/*
	 * The link up bit determines when link is up on autoneg.
	 */
	if (pcs & E1000_PCS_LSTS_LINK_OK) {
		mac->serdes_has_link = TRUE;

		/* Detect and store PCS speed */
		if (pcs & E1000_PCS_LSTS_SPEED_1000)
			*speed = SPEED_1000;
		else if (pcs & E1000_PCS_LSTS_SPEED_100)
			*speed = SPEED_100;
		else
			*speed = SPEED_10;

		/* Detect and store PCS duplex */
		if (pcs & E1000_PCS_LSTS_DUPLEX_FULL)
			*duplex = FULL_DUPLEX;
		else
			*duplex = HALF_DUPLEX;

		/* Check if it is an I354 2.5Gb backplane connection. */
		if (mac->type == e1000_i354) {
			status = E1000_READ_REG(hw, E1000_STATUS);
			if ((status & E1000_STATUS_2P5_SKU) &&
			    !(status & E1000_STATUS_2P5_SKU_OVER)) {
				*speed = SPEED_2500;
				*duplex = FULL_DUPLEX;
				DEBUGOUT("2500 Mbs, ");
				DEBUGOUT("Full Duplex\n");
			}
		}

	} else {
		mac->serdes_has_link = FALSE;
		*speed = 0;
		*duplex = 0;
	}

	return E1000_SUCCESS;
}

/**
 *  e1000_shutdown_serdes_link_82575 - Remove link during power down
 *  @hw: pointer to the HW structure
 *
 *  In the case of serdes shut down sfp and PCS on driver unload
 *  when management pass thru is not enabled.
 **/
void e1000_shutdown_serdes_link_82575(struct e1000_hw *hw)
{
	u32 reg;

	DEBUGFUNC("e1000_shutdown_serdes_link_82575");

	if ((hw->phy.media_type != e1000_media_type_internal_serdes) &&
	    !e1000_sgmii_active_82575(hw))
		return;

	if (!e1000_enable_mng_pass_thru(hw)) {
		/* Disable PCS to turn off link */
		reg = E1000_READ_REG(hw, E1000_PCS_CFG0);
		reg &= ~E1000_PCS_CFG_PCS_EN;
		E1000_WRITE_REG(hw, E1000_PCS_CFG0, reg);

		/* shutdown the laser */
		reg = E1000_READ_REG(hw, E1000_CTRL_EXT);
		reg |= E1000_CTRL_EXT_SDP3_DATA;
		E1000_WRITE_REG(hw, E1000_CTRL_EXT, reg);

		/* flush the write to verify completion */
		E1000_WRITE_FLUSH(hw);
		msec_delay(1);
	}

	return;
}

/**
 *  e1000_reset_hw_82575 - Reset hardware
 *  @hw: pointer to the HW structure
 *
 *  This resets the hardware into a known state.
 **/
static s32 e1000_reset_hw_82575(struct e1000_hw *hw)
{
	u32 ctrl;
	s32 ret_val;

	DEBUGFUNC("e1000_reset_hw_82575");

	/*
	 * Prevent the PCI-E bus from sticking if there is no TLP connection
	 * on the last TLP read/write transaction when MAC is reset.
	 */
	ret_val = e1000_disable_pcie_master_generic(hw);
	if (ret_val)
		DEBUGOUT("PCI-E Master disable polling has failed.\n");

	/* set the completion timeout for interface */
	ret_val = e1000_set_pcie_completion_timeout(hw);
	if (ret_val)
		DEBUGOUT("PCI-E Set completion timeout has failed.\n");

	DEBUGOUT("Masking off all interrupts\n");
	E1000_WRITE_REG(hw, E1000_IMC, 0xffffffff);

	E1000_WRITE_REG(hw, E1000_RCTL, 0);
	E1000_WRITE_REG(hw, E1000_TCTL, E1000_TCTL_PSP);
	E1000_WRITE_FLUSH(hw);

	msec_delay(10);

	ctrl = E1000_READ_REG(hw, E1000_CTRL);

	DEBUGOUT("Issuing a global reset to MAC\n");
	E1000_WRITE_REG(hw, E1000_CTRL, ctrl | E1000_CTRL_RST);

	ret_val = e1000_get_auto_rd_done_generic(hw);
	if (ret_val) {
		/*
		 * When auto config read does not complete, do not
		 * return with an error. This can happen in situations
		 * where there is no eeprom and prevents getting link.
		 */
		DEBUGOUT("Auto Read Done did not complete\n");
	}

	/* If EEPROM is not present, run manual init scripts */
	if (!(E1000_READ_REG(hw, E1000_EECD) & E1000_EECD_PRES))
		e1000_reset_init_script_82575(hw);

	/* Clear any pending interrupt events. */
	E1000_WRITE_REG(hw, E1000_IMC, 0xffffffff);
	E1000_READ_REG(hw, E1000_ICR);

	/* Install any alternate MAC address into RAR0 */
	ret_val = e1000_check_alt_mac_addr_generic(hw);

	return ret_val;
}

/**
 *  e1000_init_hw_82575 - Initialize hardware
 *  @hw: pointer to the HW structure
 *
 *  This inits the hardware readying it for operation.
 **/
s32 e1000_init_hw_82575(struct e1000_hw *hw)
{
	struct e1000_mac_info *mac = &hw->mac;
	s32 ret_val;
	u16 i, rar_count = mac->rar_entry_count;

	DEBUGFUNC("e1000_init_hw_82575");

	/* Initialize identification LED */
	ret_val = mac->ops.id_led_init(hw);
	if (ret_val) {
		DEBUGOUT("Error initializing identification LED\n");
		/* This is not fatal and we should not stop init due to this */
	}

	/* Disabling VLAN filtering */
	DEBUGOUT("Initializing the IEEE VLAN\n");
	mac->ops.clear_vfta(hw);

	/* Setup the receive address */
	e1000_init_rx_addrs_generic(hw, rar_count);

	/* Zero out the Multicast HASH table */
	DEBUGOUT("Zeroing the MTA\n");
	for (i = 0; i < mac->mta_reg_count; i++)
		E1000_WRITE_REG_ARRAY(hw, E1000_MTA, i, 0);

	/* Zero out the Unicast HASH table */
	DEBUGOUT("Zeroing the UTA\n");
	for (i = 0; i < mac->uta_reg_count; i++)
		E1000_WRITE_REG_ARRAY(hw, E1000_UTA, i, 0);

	/* Setup link and flow control */
	ret_val = mac->ops.setup_link(hw);

	/* Set the default MTU size */
	hw->dev_spec._82575.mtu = 1500;

	/*
	 * Clear all of the statistics registers (clear on read).  It is
	 * important that we do this after we have tried to establish link
	 * because the symbol error count will increment wildly if there
	 * is no link.
	 */
	e1000_clear_hw_cntrs_82575(hw);

	return ret_val;
}

/**
 *  e1000_setup_copper_link_82575 - Configure copper link settings
 *  @hw: pointer to the HW structure
 *
 *  Configures the link for auto-neg or forced speed and duplex.  Then we check
 *  for link, once link is established calls to configure collision distance
 *  and flow control are called.
 **/
static s32 e1000_setup_copper_link_82575(struct e1000_hw *hw)
{
	u32 ctrl;
	s32 ret_val;
	u32 phpm_reg;

	DEBUGFUNC("e1000_setup_copper_link_82575");

	ctrl = E1000_READ_REG(hw, E1000_CTRL);
	ctrl |= E1000_CTRL_SLU;
	ctrl &= ~(E1000_CTRL_FRCSPD | E1000_CTRL_FRCDPX);
	E1000_WRITE_REG(hw, E1000_CTRL, ctrl);

	/* Clear Go Link Disconnect bit on supported devices */
	switch (hw->mac.type) {
	case e1000_82580:
	case e1000_i350:
	case e1000_i210:
	case e1000_i211:
		phpm_reg = E1000_READ_REG(hw, E1000_82580_PHY_POWER_MGMT);
		phpm_reg &= ~E1000_82580_PM_GO_LINKD;
		E1000_WRITE_REG(hw, E1000_82580_PHY_POWER_MGMT, phpm_reg);
		break;
	default:
		break;
	}

	ret_val = e1000_setup_serdes_link_82575(hw);
	if (ret_val)
		goto out;

	if (e1000_sgmii_active_82575(hw)) {
		/* allow time for SFP cage time to power up phy */
		msec_delay(300);

		ret_val = hw->phy.ops.reset(hw);
		if (ret_val) {
			DEBUGOUT("Error resetting the PHY.\n");
			goto out;
		}
	}
	switch (hw->phy.type) {
	case e1000_phy_i210:
	case e1000_phy_m88:
		switch (hw->phy.id) {
		case I347AT4_E_PHY_ID:
		case M88E1112_E_PHY_ID:
		case M88E1340M_E_PHY_ID:
		case M88E1543_E_PHY_ID:
		case M88E1512_E_PHY_ID:
		case I210_I_PHY_ID:
			ret_val = e1000_copper_link_setup_m88_gen2(hw);
			break;
		default:
			ret_val = e1000_copper_link_setup_m88(hw);
			break;
		}
		break;
	case e1000_phy_igp_3:
		ret_val = e1000_copper_link_setup_igp(hw);
		break;
	case e1000_phy_82580:
		ret_val = e1000_copper_link_setup_82577(hw);
		break;
	default:
		ret_val = -E1000_ERR_PHY;
		break;
	}

	if (ret_val)
		goto out;

	ret_val = e1000_setup_copper_link_generic(hw);
out:
	return ret_val;
}

/**
 *  e1000_setup_serdes_link_82575 - Setup link for serdes
 *  @hw: pointer to the HW structure
 *
 *  Configure the physical coding sub-layer (PCS) link.  The PCS link is
 *  used on copper connections where the serialized gigabit media independent
 *  interface (sgmii), or serdes fiber is being used.  Configures the link
 *  for auto-negotiation or forces speed/duplex.
 **/
static s32 e1000_setup_serdes_link_82575(struct e1000_hw *hw)
{
	u32 ctrl_ext, ctrl_reg, reg, anadv_reg;
	bool pcs_autoneg;
	s32 ret_val = E1000_SUCCESS;
	u16 data;

	DEBUGFUNC("e1000_setup_serdes_link_82575");

	if ((hw->phy.media_type != e1000_media_type_internal_serdes) &&
	    !e1000_sgmii_active_82575(hw))
		return ret_val;

	/*
	 * On the 82575, SerDes loopback mode persists until it is
	 * explicitly turned off or a power cycle is performed.  A read to
	 * the register does not indicate its status.  Therefore, we ensure
	 * loopback mode is disabled during initialization.
	 */
	E1000_WRITE_REG(hw, E1000_SCTL, E1000_SCTL_DISABLE_SERDES_LOOPBACK);

	/* power on the sfp cage if present */
	ctrl_ext = E1000_READ_REG(hw, E1000_CTRL_EXT);
	ctrl_ext &= ~E1000_CTRL_EXT_SDP3_DATA;
	E1000_WRITE_REG(hw, E1000_CTRL_EXT, ctrl_ext);

	ctrl_reg = E1000_READ_REG(hw, E1000_CTRL);
	ctrl_reg |= E1000_CTRL_SLU;

	/* set both sw defined pins on 82575/82576*/
	if (hw->mac.type == e1000_82575 || hw->mac.type == e1000_82576)
		ctrl_reg |= E1000_CTRL_SWDPIN0 | E1000_CTRL_SWDPIN1;

	reg = E1000_READ_REG(hw, E1000_PCS_LCTL);

	/* default pcs_autoneg to the same setting as mac autoneg */
	pcs_autoneg = hw->mac.autoneg;

	switch (ctrl_ext & E1000_CTRL_EXT_LINK_MODE_MASK) {
	case E1000_CTRL_EXT_LINK_MODE_SGMII:
		/* sgmii mode lets the phy handle forcing speed/duplex */
		pcs_autoneg = TRUE;
		/* autoneg time out should be disabled for SGMII mode */
		reg &= ~(E1000_PCS_LCTL_AN_TIMEOUT);
		break;
	case E1000_CTRL_EXT_LINK_MODE_1000BASE_KX:
		/* disable PCS autoneg and support parallel detect only */
		pcs_autoneg = FALSE;
		/* FALLTHROUGH */
	default:
		if (hw->mac.type == e1000_82575 ||
		    hw->mac.type == e1000_82576) {
			ret_val = hw->nvm.ops.read(hw, NVM_COMPAT, 1, &data);
			if (ret_val) {
				DEBUGOUT("NVM Read Error\n");
				return ret_val;
			}

			if (data & E1000_EEPROM_PCS_AUTONEG_DISABLE_BIT)
				pcs_autoneg = FALSE;
		}

		/*
		 * non-SGMII modes only supports a speed of 1000/Full for the
		 * link so it is best to just force the MAC and let the pcs
		 * link either autoneg or be forced to 1000/Full
		 */
		ctrl_reg |= E1000_CTRL_SPD_1000 | E1000_CTRL_FRCSPD |
			    E1000_CTRL_FD | E1000_CTRL_FRCDPX;

		/* set speed of 1000/Full if speed/duplex is forced */
		reg |= E1000_PCS_LCTL_FSV_1000 | E1000_PCS_LCTL_FDV_FULL;
		break;
	}

	E1000_WRITE_REG(hw, E1000_CTRL, ctrl_reg);

	/*
	 * New SerDes mode allows for forcing speed or autonegotiating speed
	 * at 1gb. Autoneg should be default set by most drivers. This is the
	 * mode that will be compatible with older link partners and switches.
	 * However, both are supported by the hardware and some drivers/tools.
	 */
	reg &= ~(E1000_PCS_LCTL_AN_ENABLE | E1000_PCS_LCTL_FLV_LINK_UP |
		 E1000_PCS_LCTL_FSD | E1000_PCS_LCTL_FORCE_LINK);

	if (pcs_autoneg) {
		/* Set PCS register for autoneg */
		reg |= E1000_PCS_LCTL_AN_ENABLE | /* Enable Autoneg */
		       E1000_PCS_LCTL_AN_RESTART; /* Restart autoneg */

		/* Disable force flow control for autoneg */
		reg &= ~E1000_PCS_LCTL_FORCE_FCTRL;

		/* Configure flow control advertisement for autoneg */
		anadv_reg = E1000_READ_REG(hw, E1000_PCS_ANADV);
		anadv_reg &= ~(E1000_TXCW_ASM_DIR | E1000_TXCW_PAUSE);

		switch (hw->fc.requested_mode) {
		case e1000_fc_full:
		case e1000_fc_rx_pause:
			anadv_reg |= E1000_TXCW_ASM_DIR;
			anadv_reg |= E1000_TXCW_PAUSE;
			break;
		case e1000_fc_tx_pause:
			anadv_reg |= E1000_TXCW_ASM_DIR;
			break;
		default:
			break;
		}

		E1000_WRITE_REG(hw, E1000_PCS_ANADV, anadv_reg);

		DEBUGOUT1("Configuring Autoneg:PCS_LCTL=0x%08X\n", reg);
	} else {
		/* Set PCS register for forced link */
		reg |= E1000_PCS_LCTL_FSD;	/* Force Speed */

		/* Force flow control for forced link */
		reg |= E1000_PCS_LCTL_FORCE_FCTRL;

		DEBUGOUT1("Configuring Forced Link:PCS_LCTL=0x%08X\n", reg);
	}

	E1000_WRITE_REG(hw, E1000_PCS_LCTL, reg);

	if (!pcs_autoneg && !e1000_sgmii_active_82575(hw))
		e1000_force_mac_fc_generic(hw);

	return ret_val;
}

/**
 *  e1000_get_media_type_82575 - derives current media type.
 *  @hw: pointer to the HW structure
 *
 *  The media type is chosen reflecting few settings.
 *  The following are taken into account:
 *  - link mode set in the current port Init Control Word #3
 *  - current link mode settings in CSR register
 *  - MDIO vs. I2C PHY control interface chosen
 *  - SFP module media type
 **/
static s32 e1000_get_media_type_82575(struct e1000_hw *hw)
{
	struct e1000_dev_spec_82575 *dev_spec = &hw->dev_spec._82575;
	s32 ret_val = E1000_SUCCESS;
	u32 ctrl_ext = 0;
	u32 link_mode = 0;

	/* Set internal phy as default */
	dev_spec->sgmii_active = FALSE;
	dev_spec->module_plugged = FALSE;

	/* Get CSR setting */
	ctrl_ext = E1000_READ_REG(hw, E1000_CTRL_EXT);

	/* extract link mode setting */
	link_mode = ctrl_ext & E1000_CTRL_EXT_LINK_MODE_MASK;

	switch (link_mode) {
	case E1000_CTRL_EXT_LINK_MODE_1000BASE_KX:
		hw->phy.media_type = e1000_media_type_internal_serdes;
		break;
	case E1000_CTRL_EXT_LINK_MODE_GMII:
		hw->phy.media_type = e1000_media_type_copper;
		break;
	case E1000_CTRL_EXT_LINK_MODE_SGMII:
		/* Get phy control interface type set (MDIO vs. I2C)*/
		if (e1000_sgmii_uses_mdio_82575(hw)) {
			hw->phy.media_type = e1000_media_type_copper;
			dev_spec->sgmii_active = TRUE;
			break;
		}
		/* fall through for I2C based SGMII */
		/* FALLTHROUGH */
	case E1000_CTRL_EXT_LINK_MODE_PCIE_SERDES:
		/* read media type from SFP EEPROM */
		ret_val = e1000_set_sfp_media_type_82575(hw);
		if ((ret_val != E1000_SUCCESS) ||
		    (hw->phy.media_type == e1000_media_type_unknown)) {
			/*
			 * If media type was not identified then return media
			 * type defined by the CTRL_EXT settings.
			 */
			hw->phy.media_type = e1000_media_type_internal_serdes;

			if (link_mode == E1000_CTRL_EXT_LINK_MODE_SGMII) {
				hw->phy.media_type = e1000_media_type_copper;
				dev_spec->sgmii_active = TRUE;
			}

			break;
		}

		/* do not change link mode for 100BaseFX */
		if (dev_spec->eth_flags.e100_base_fx)
			break;

		/* change current link mode setting */
		ctrl_ext &= ~E1000_CTRL_EXT_LINK_MODE_MASK;

		if (hw->phy.media_type == e1000_media_type_copper)
			ctrl_ext |= E1000_CTRL_EXT_LINK_MODE_SGMII;
		else
			ctrl_ext |= E1000_CTRL_EXT_LINK_MODE_PCIE_SERDES;

		E1000_WRITE_REG(hw, E1000_CTRL_EXT, ctrl_ext);

		break;
	}

	return ret_val;
}

/**
 *  e1000_set_sfp_media_type_82575 - derives SFP module media type.
 *  @hw: pointer to the HW structure
 *
 *  The media type is chosen based on SFP module.
 *  compatibility flags retrieved from SFP ID EEPROM.
 **/
static s32 e1000_set_sfp_media_type_82575(struct e1000_hw *hw)
{
	s32 ret_val = E1000_ERR_CONFIG;
	u32 ctrl_ext = 0;
	struct e1000_dev_spec_82575 *dev_spec = &hw->dev_spec._82575;
	struct sfp_e1000_flags *eth_flags = &dev_spec->eth_flags;
	u8 tranceiver_type = 0;
	s32 timeout = 3;

	/* Turn I2C interface ON and power on sfp cage */
	ctrl_ext = E1000_READ_REG(hw, E1000_CTRL_EXT);
	ctrl_ext &= ~E1000_CTRL_EXT_SDP3_DATA;
	E1000_WRITE_REG(hw, E1000_CTRL_EXT, ctrl_ext | E1000_CTRL_I2C_ENA);

	E1000_WRITE_FLUSH(hw);

	/* Read SFP module data */
	while (timeout) {
		ret_val = e1000_read_sfp_data_byte(hw,
			E1000_I2CCMD_SFP_DATA_ADDR(E1000_SFF_IDENTIFIER_OFFSET),
			&tranceiver_type);
		if (ret_val == E1000_SUCCESS)
			break;
		msec_delay(100);
		timeout--;
	}
	if (ret_val != E1000_SUCCESS)
		goto out;

	ret_val = e1000_read_sfp_data_byte(hw,
			E1000_I2CCMD_SFP_DATA_ADDR(E1000_SFF_ETH_FLAGS_OFFSET),
			(u8 *)eth_flags);
	if (ret_val != E1000_SUCCESS)
		goto out;

	/* Check if there is some SFP module plugged and powered */
	if ((tranceiver_type == E1000_SFF_IDENTIFIER_SFP) ||
	    (tranceiver_type == E1000_SFF_IDENTIFIER_SFF)) {
		dev_spec->module_plugged = TRUE;
		if (eth_flags->e1000_base_lx || eth_flags->e1000_base_sx) {
			hw->phy.media_type = e1000_media_type_internal_serdes;
		} else if (eth_flags->e100_base_fx) {
			dev_spec->sgmii_active = TRUE;
			hw->phy.media_type = e1000_media_type_internal_serdes;
		} else if (eth_flags->e1000_base_t) {
			dev_spec->sgmii_active = TRUE;
			hw->phy.media_type = e1000_media_type_copper;
		} else {
			hw->phy.media_type = e1000_media_type_unknown;
			DEBUGOUT("PHY module has not been recognized\n");
			goto out;
		}
	} else {
		hw->phy.media_type = e1000_media_type_unknown;
	}
	ret_val = E1000_SUCCESS;
out:
	/* Restore I2C interface setting */
	E1000_WRITE_REG(hw, E1000_CTRL_EXT, ctrl_ext);
	return ret_val;
}

/**
 *  e1000_valid_led_default_82575 - Verify a valid default LED config
 *  @hw: pointer to the HW structure
 *  @data: pointer to the NVM (EEPROM)
 *
 *  Read the EEPROM for the current default LED configuration.  If the
 *  LED configuration is not valid, set to a valid LED configuration.
 **/
static s32 e1000_valid_led_default_82575(struct e1000_hw *hw, u16 *data)
{
	s32 ret_val;

	DEBUGFUNC("e1000_valid_led_default_82575");

	ret_val = hw->nvm.ops.read(hw, NVM_ID_LED_SETTINGS, 1, data);
	if (ret_val) {
		DEBUGOUT("NVM Read Error\n");
		goto out;
	}

	if (*data == ID_LED_RESERVED_0000 || *data == ID_LED_RESERVED_FFFF) {
		switch (hw->phy.media_type) {
		case e1000_media_type_internal_serdes:
			*data = ID_LED_DEFAULT_82575_SERDES;
			break;
		case e1000_media_type_copper:
		default:
			*data = ID_LED_DEFAULT;
			break;
		}
	}
out:
	return ret_val;
}

/**
 *  e1000_sgmii_active_82575 - Return sgmii state
 *  @hw: pointer to the HW structure
 *
 *  82575 silicon has a serialized gigabit media independent interface (sgmii)
 *  which can be enabled for use in the embedded applications.  Simply
 *  return the current state of the sgmii interface.
 **/
static bool e1000_sgmii_active_82575(struct e1000_hw *hw)
{
	struct e1000_dev_spec_82575 *dev_spec = &hw->dev_spec._82575;
	return dev_spec->sgmii_active;
}

/**
 *  e1000_reset_init_script_82575 - Inits HW defaults after reset
 *  @hw: pointer to the HW structure
 *
 *  Inits recommended HW defaults after a reset when there is no EEPROM
 *  detected. This is only for the 82575.
 **/
static s32 e1000_reset_init_script_82575(struct e1000_hw *hw)
{
	DEBUGFUNC("e1000_reset_init_script_82575");

	if (hw->mac.type == e1000_82575) {
		DEBUGOUT("Running reset init script for 82575\n");
		/* SerDes configuration via SERDESCTRL */
		e1000_write_8bit_ctrl_reg_generic(hw, E1000_SCTL, 0x00, 0x0C);
		e1000_write_8bit_ctrl_reg_generic(hw, E1000_SCTL, 0x01, 0x78);
		e1000_write_8bit_ctrl_reg_generic(hw, E1000_SCTL, 0x1B, 0x23);
		e1000_write_8bit_ctrl_reg_generic(hw, E1000_SCTL, 0x23, 0x15);

		/* CCM configuration via CCMCTL register */
		e1000_write_8bit_ctrl_reg_generic(hw, E1000_CCMCTL, 0x14, 0x00);
		e1000_write_8bit_ctrl_reg_generic(hw, E1000_CCMCTL, 0x10, 0x00);

		/* PCIe lanes configuration */
		e1000_write_8bit_ctrl_reg_generic(hw, E1000_GIOCTL, 0x00, 0xEC);
		e1000_write_8bit_ctrl_reg_generic(hw, E1000_GIOCTL, 0x61, 0xDF);
		e1000_write_8bit_ctrl_reg_generic(hw, E1000_GIOCTL, 0x34, 0x05);
		e1000_write_8bit_ctrl_reg_generic(hw, E1000_GIOCTL, 0x2F, 0x81);

		/* PCIe PLL Configuration */
		e1000_write_8bit_ctrl_reg_generic(hw, E1000_SCCTL, 0x02, 0x47);
		e1000_write_8bit_ctrl_reg_generic(hw, E1000_SCCTL, 0x14, 0x00);
		e1000_write_8bit_ctrl_reg_generic(hw, E1000_SCCTL, 0x10, 0x00);
	}

	return E1000_SUCCESS;
}

/**
 *  e1000_read_mac_addr_82575 - Read device MAC address
 *  @hw: pointer to the HW structure
 **/
static s32 e1000_read_mac_addr_82575(struct e1000_hw *hw)
{
	s32 ret_val;

	DEBUGFUNC("e1000_read_mac_addr_82575");

	/*
	 * If there's an alternate MAC address place it in RAR0
	 * so that it will override the Si installed default perm
	 * address.
	 */
	ret_val = e1000_check_alt_mac_addr_generic(hw);
	if (ret_val)
		goto out;

	ret_val = e1000_read_mac_addr_generic(hw);

out:
	return ret_val;
}

/**
 *  e1000_config_collision_dist_82575 - Configure collision distance
 *  @hw: pointer to the HW structure
 *
 *  Configures the collision distance to the default value and is used
 *  during link setup.
 **/
static void e1000_config_collision_dist_82575(struct e1000_hw *hw)
{
	u32 tctl_ext;

	DEBUGFUNC("e1000_config_collision_dist_82575");

	tctl_ext = E1000_READ_REG(hw, E1000_TCTL_EXT);

	tctl_ext &= ~E1000_TCTL_EXT_COLD;
	tctl_ext |= E1000_COLLISION_DISTANCE << E1000_TCTL_EXT_COLD_SHIFT;

	E1000_WRITE_REG(hw, E1000_TCTL_EXT, tctl_ext);
	E1000_WRITE_FLUSH(hw);
}

/**
 * e1000_power_down_phy_copper_82575 - Remove link during PHY power down
 * @hw: pointer to the HW structure
 *
 * In the case of a PHY power down to save power, or to turn off link during a
 * driver unload, or wake on lan is not enabled, remove the link.
 **/
static void e1000_power_down_phy_copper_82575(struct e1000_hw *hw)
{
	struct e1000_phy_info *phy = &hw->phy;

	if (!(phy->ops.check_reset_block))
		return;

	/* If the management interface is not enabled, then power down */
	if (!(e1000_enable_mng_pass_thru(hw) || phy->ops.check_reset_block(hw)))
		e1000_power_down_phy_copper(hw);

	return;
}

/**
 *  e1000_clear_hw_cntrs_82575 - Clear device specific hardware counters
 *  @hw: pointer to the HW structure
 *
 *  Clears the hardware counters by reading the counter registers.
 **/
static void e1000_clear_hw_cntrs_82575(struct e1000_hw *hw)
{
	DEBUGFUNC("e1000_clear_hw_cntrs_82575");

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

	E1000_READ_REG(hw, E1000_CBTMPC);
	E1000_READ_REG(hw, E1000_HTDPMC);
	E1000_READ_REG(hw, E1000_CBRMPC);
	E1000_READ_REG(hw, E1000_RPTHC);
	E1000_READ_REG(hw, E1000_HGPTC);
	E1000_READ_REG(hw, E1000_HTCBDPC);
	E1000_READ_REG(hw, E1000_HGORCL);
	E1000_READ_REG(hw, E1000_HGORCH);
	E1000_READ_REG(hw, E1000_HGOTCL);
	E1000_READ_REG(hw, E1000_HGOTCH);
	E1000_READ_REG(hw, E1000_LENERRS);

	/* This register should not be read in copper configurations */
	if ((hw->phy.media_type == e1000_media_type_internal_serdes) ||
	    e1000_sgmii_active_82575(hw))
		E1000_READ_REG(hw, E1000_SCVPC);
}

/**
 *  e1000_rx_fifo_flush_82575 - Clean rx fifo after Rx enable
 *  @hw: pointer to the HW structure
 *
 *  After Rx enable, if manageability is enabled then there is likely some
 *  bad data at the start of the fifo and possibly in the DMA fifo.  This
 *  function clears the fifos and flushes any packets that came in as rx was
 *  being enabled.
 **/
void e1000_rx_fifo_flush_82575(struct e1000_hw *hw)
{
	u32 rctl, rlpml, rxdctl[4], rfctl, temp_rctl, rx_enabled;
	int i, ms_wait;

	DEBUGFUNC("e1000_rx_fifo_flush_82575");

	/* disable IPv6 options as per hardware errata */
	rfctl = E1000_READ_REG(hw, E1000_RFCTL);
	rfctl |= E1000_RFCTL_IPV6_EX_DIS;
	E1000_WRITE_REG(hw, E1000_RFCTL, rfctl);

	if (hw->mac.type != e1000_82575 ||
	    !(E1000_READ_REG(hw, E1000_MANC) & E1000_MANC_RCV_TCO_EN))
		return;

	/* Disable all Rx queues */
	for (i = 0; i < 4; i++) {
		rxdctl[i] = E1000_READ_REG(hw, E1000_RXDCTL(i));
		E1000_WRITE_REG(hw, E1000_RXDCTL(i),
				rxdctl[i] & ~E1000_RXDCTL_QUEUE_ENABLE);
	}
	/* Poll all queues to verify they have shut down */
	for (ms_wait = 0; ms_wait < 10; ms_wait++) {
		msec_delay(1);
		rx_enabled = 0;
		for (i = 0; i < 4; i++)
			rx_enabled |= E1000_READ_REG(hw, E1000_RXDCTL(i));
		if (!(rx_enabled & E1000_RXDCTL_QUEUE_ENABLE))
			break;
	}

	if (ms_wait == 10)
		DEBUGOUT("Queue disable timed out after 10ms\n");

	/* Clear RLPML, RCTL.SBP, RFCTL.LEF, and set RCTL.LPE so that all
	 * incoming packets are rejected.  Set enable and wait 2ms so that
	 * any packet that was coming in as RCTL.EN was set is flushed
	 */
	E1000_WRITE_REG(hw, E1000_RFCTL, rfctl & ~E1000_RFCTL_LEF);

	rlpml = E1000_READ_REG(hw, E1000_RLPML);
	E1000_WRITE_REG(hw, E1000_RLPML, 0);

	rctl = E1000_READ_REG(hw, E1000_RCTL);
	temp_rctl = rctl & ~(E1000_RCTL_EN | E1000_RCTL_SBP);
	temp_rctl |= E1000_RCTL_LPE;

	E1000_WRITE_REG(hw, E1000_RCTL, temp_rctl);
	E1000_WRITE_REG(hw, E1000_RCTL, temp_rctl | E1000_RCTL_EN);
	E1000_WRITE_FLUSH(hw);
	msec_delay(2);

	/* Enable Rx queues that were previously enabled and restore our
	 * previous state
	 */
	for (i = 0; i < 4; i++)
		E1000_WRITE_REG(hw, E1000_RXDCTL(i), rxdctl[i]);
	E1000_WRITE_REG(hw, E1000_RCTL, rctl);
	E1000_WRITE_FLUSH(hw);

	E1000_WRITE_REG(hw, E1000_RLPML, rlpml);
	E1000_WRITE_REG(hw, E1000_RFCTL, rfctl);

	/* Flush receive errors generated by workaround */
	E1000_READ_REG(hw, E1000_ROC);
	E1000_READ_REG(hw, E1000_RNBC);
	E1000_READ_REG(hw, E1000_MPC);
}

/**
 *  e1000_set_pcie_completion_timeout - set pci-e completion timeout
 *  @hw: pointer to the HW structure
 *
 *  The defaults for 82575 and 82576 should be in the range of 50us to 50ms,
 *  however the hardware default for these parts is 500us to 1ms which is less
 *  than the 10ms recommended by the pci-e spec.  To address this we need to
 *  increase the value to either 10ms to 200ms for capability version 1 config,
 *  or 16ms to 55ms for version 2.
 **/
static s32 e1000_set_pcie_completion_timeout(struct e1000_hw *hw)
{
	u32 gcr = E1000_READ_REG(hw, E1000_GCR);
	s32 ret_val = E1000_SUCCESS;
	u16 pcie_devctl2;

	/* only take action if timeout value is defaulted to 0 */
	if (gcr & E1000_GCR_CMPL_TMOUT_MASK)
		goto out;

	/*
	 * if capababilities version is type 1 we can write the
	 * timeout of 10ms to 200ms through the GCR register
	 */
	if (!(gcr & E1000_GCR_CAP_VER2)) {
		gcr |= E1000_GCR_CMPL_TMOUT_10ms;
		goto out;
	}

	/*
	 * for version 2 capabilities we need to write the config space
	 * directly in order to set the completion timeout value for
	 * 16ms to 55ms
	 */
	ret_val = e1000_read_pcie_cap_reg(hw, PCIE_DEVICE_CONTROL2,
					  &pcie_devctl2);
	if (ret_val)
		goto out;

	pcie_devctl2 |= PCIE_DEVICE_CONTROL2_16ms;

	ret_val = e1000_write_pcie_cap_reg(hw, PCIE_DEVICE_CONTROL2,
					   &pcie_devctl2);
out:
	/* disable completion timeout resend */
	gcr &= ~E1000_GCR_CMPL_TMOUT_RESEND;

	E1000_WRITE_REG(hw, E1000_GCR, gcr);
	return ret_val;
}

/**
 *  e1000_vmdq_set_anti_spoofing_pf - enable or disable anti-spoofing
 *  @hw: pointer to the hardware struct
 *  @enable: state to enter, either enabled or disabled
 *  @pf: Physical Function pool - do not set anti-spoofing for the PF
 *
 *  enables/disables L2 switch anti-spoofing functionality.
 **/
void e1000_vmdq_set_anti_spoofing_pf(struct e1000_hw *hw, bool enable, int pf)
{
	u32 reg_val, reg_offset;

	switch (hw->mac.type) {
	case e1000_82576:
		reg_offset = E1000_DTXSWC;
		break;
	case e1000_i350:
	case e1000_i354:
		reg_offset = E1000_TXSWC;
		break;
	default:
		return;
	}

	reg_val = E1000_READ_REG(hw, reg_offset);
	if (enable) {
		reg_val |= (E1000_DTXSWC_MAC_SPOOF_MASK |
			     E1000_DTXSWC_VLAN_SPOOF_MASK);
		/* The PF can spoof - it has to in order to
		 * support emulation mode NICs
		 */
		reg_val ^= (1 << pf | 1 << (pf + MAX_NUM_VFS));
	} else {
		reg_val &= ~(E1000_DTXSWC_MAC_SPOOF_MASK |
			     E1000_DTXSWC_VLAN_SPOOF_MASK);
	}
	E1000_WRITE_REG(hw, reg_offset, reg_val);
}

/**
 *  e1000_vmdq_set_loopback_pf - enable or disable vmdq loopback
 *  @hw: pointer to the hardware struct
 *  @enable: state to enter, either enabled or disabled
 *
 *  enables/disables L2 switch loopback functionality.
 **/
void e1000_vmdq_set_loopback_pf(struct e1000_hw *hw, bool enable)
{
	u32 dtxswc;

	switch (hw->mac.type) {
	case e1000_82576:
		dtxswc = E1000_READ_REG(hw, E1000_DTXSWC);
		if (enable)
			dtxswc |= E1000_DTXSWC_VMDQ_LOOPBACK_EN;
		else
			dtxswc &= ~E1000_DTXSWC_VMDQ_LOOPBACK_EN;
		E1000_WRITE_REG(hw, E1000_DTXSWC, dtxswc);
		break;
	case e1000_i350:
	case e1000_i354:
		dtxswc = E1000_READ_REG(hw, E1000_TXSWC);
		if (enable)
			dtxswc |= E1000_DTXSWC_VMDQ_LOOPBACK_EN;
		else
			dtxswc &= ~E1000_DTXSWC_VMDQ_LOOPBACK_EN;
		E1000_WRITE_REG(hw, E1000_TXSWC, dtxswc);
		break;
	default:
		/* Currently no other hardware supports loopback */
		break;
	}


}

/**
 *  e1000_vmdq_set_replication_pf - enable or disable vmdq replication
 *  @hw: pointer to the hardware struct
 *  @enable: state to enter, either enabled or disabled
 *
 *  enables/disables replication of packets across multiple pools.
 **/
void e1000_vmdq_set_replication_pf(struct e1000_hw *hw, bool enable)
{
	u32 vt_ctl = E1000_READ_REG(hw, E1000_VT_CTL);

	if (enable)
		vt_ctl |= E1000_VT_CTL_VM_REPL_EN;
	else
		vt_ctl &= ~E1000_VT_CTL_VM_REPL_EN;

	E1000_WRITE_REG(hw, E1000_VT_CTL, vt_ctl);
}

/**
 *  e1000_read_phy_reg_82580 - Read 82580 MDI control register
 *  @hw: pointer to the HW structure
 *  @offset: register offset to be read
 *  @data: pointer to the read data
 *
 *  Reads the MDI control register in the PHY at offset and stores the
 *  information read to data.
 **/
static s32 e1000_read_phy_reg_82580(struct e1000_hw *hw, u32 offset, u16 *data)
{
	s32 ret_val;

	DEBUGFUNC("e1000_read_phy_reg_82580");

	ret_val = hw->phy.ops.acquire(hw);
	if (ret_val)
		goto out;

	ret_val = e1000_read_phy_reg_mdic(hw, offset, data);

	hw->phy.ops.release(hw);

out:
	return ret_val;
}

/**
 *  e1000_write_phy_reg_82580 - Write 82580 MDI control register
 *  @hw: pointer to the HW structure
 *  @offset: register offset to write to
 *  @data: data to write to register at offset
 *
 *  Writes data to MDI control register in the PHY at offset.
 **/
static s32 e1000_write_phy_reg_82580(struct e1000_hw *hw, u32 offset, u16 data)
{
	s32 ret_val;

	DEBUGFUNC("e1000_write_phy_reg_82580");

	ret_val = hw->phy.ops.acquire(hw);
	if (ret_val)
		goto out;

	ret_val = e1000_write_phy_reg_mdic(hw, offset, data);

	hw->phy.ops.release(hw);

out:
	return ret_val;
}

/**
 *  e1000_reset_mdicnfg_82580 - Reset MDICNFG destination and com_mdio bits
 *  @hw: pointer to the HW structure
 *
 *  This resets the MDICNFG.Destination and MDICNFG.Com_MDIO bits based on
 *  the values found in the EEPROM.  This addresses an issue in which these
 *  bits are not restored from EEPROM after reset.
 **/
static s32 e1000_reset_mdicnfg_82580(struct e1000_hw *hw)
{
	s32 ret_val = E1000_SUCCESS;
	u32 mdicnfg;
	u16 nvm_data = 0;

	DEBUGFUNC("e1000_reset_mdicnfg_82580");

	if (hw->mac.type != e1000_82580)
		goto out;
	if (!e1000_sgmii_active_82575(hw))
		goto out;

	ret_val = hw->nvm.ops.read(hw, NVM_INIT_CONTROL3_PORT_A +
				   NVM_82580_LAN_FUNC_OFFSET(hw->bus.func), 1,
				   &nvm_data);
	if (ret_val) {
		DEBUGOUT("NVM Read Error\n");
		goto out;
	}

	mdicnfg = E1000_READ_REG(hw, E1000_MDICNFG);
	if (nvm_data & NVM_WORD24_EXT_MDIO)
		mdicnfg |= E1000_MDICNFG_EXT_MDIO;
	if (nvm_data & NVM_WORD24_COM_MDIO)
		mdicnfg |= E1000_MDICNFG_COM_MDIO;
	E1000_WRITE_REG(hw, E1000_MDICNFG, mdicnfg);
out:
	return ret_val;
}

/**
 *  e1000_reset_hw_82580 - Reset hardware
 *  @hw: pointer to the HW structure
 *
 *  This resets function or entire device (all ports, etc.)
 *  to a known state.
 **/
static s32 e1000_reset_hw_82580(struct e1000_hw *hw)
{
	s32 ret_val = E1000_SUCCESS;
	/* BH SW mailbox bit in SW_FW_SYNC */
	u16 swmbsw_mask = E1000_SW_SYNCH_MB;
	u32 ctrl;
	bool global_device_reset = hw->dev_spec._82575.global_device_reset;

	DEBUGFUNC("e1000_reset_hw_82580");

	hw->dev_spec._82575.global_device_reset = FALSE;

	/* 82580 does not reliably do global_device_reset due to hw errata */
	if (hw->mac.type == e1000_82580)
		global_device_reset = FALSE;

	/* Get current control state. */
	ctrl = E1000_READ_REG(hw, E1000_CTRL);

	/*
	 * Prevent the PCI-E bus from sticking if there is no TLP connection
	 * on the last TLP read/write transaction when MAC is reset.
	 */
	ret_val = e1000_disable_pcie_master_generic(hw);
	if (ret_val)
		DEBUGOUT("PCI-E Master disable polling has failed.\n");

	DEBUGOUT("Masking off all interrupts\n");
	E1000_WRITE_REG(hw, E1000_IMC, 0xffffffff);
	E1000_WRITE_REG(hw, E1000_RCTL, 0);
	E1000_WRITE_REG(hw, E1000_TCTL, E1000_TCTL_PSP);
	E1000_WRITE_FLUSH(hw);

	msec_delay(10);

	/* Determine whether or not a global dev reset is requested */
	if (global_device_reset && hw->mac.ops.acquire_swfw_sync(hw,
	    swmbsw_mask))
			global_device_reset = FALSE;

	if (global_device_reset && !(E1000_READ_REG(hw, E1000_STATUS) &
	    E1000_STAT_DEV_RST_SET))
		ctrl |= E1000_CTRL_DEV_RST;
	else
		ctrl |= E1000_CTRL_RST;

	E1000_WRITE_REG(hw, E1000_CTRL, ctrl);

	switch (hw->device_id) {
	case E1000_DEV_ID_DH89XXCC_SGMII:
		break;
	default:
		E1000_WRITE_FLUSH(hw);
		break;
	}

	/* Add delay to insure DEV_RST or RST has time to complete */
	msec_delay(5);

	ret_val = e1000_get_auto_rd_done_generic(hw);
	if (ret_val) {
		/*
		 * When auto config read does not complete, do not
		 * return with an error. This can happen in situations
		 * where there is no eeprom and prevents getting link.
		 */
		DEBUGOUT("Auto Read Done did not complete\n");
	}

	/* clear global device reset status bit */
	E1000_WRITE_REG(hw, E1000_STATUS, E1000_STAT_DEV_RST_SET);

	/* Clear any pending interrupt events. */
	E1000_WRITE_REG(hw, E1000_IMC, 0xffffffff);
	E1000_READ_REG(hw, E1000_ICR);

	ret_val = e1000_reset_mdicnfg_82580(hw);
	if (ret_val)
		DEBUGOUT("Could not reset MDICNFG based on EEPROM\n");

	/* Install any alternate MAC address into RAR0 */
	ret_val = e1000_check_alt_mac_addr_generic(hw);

	/* Release semaphore */
	if (global_device_reset)
		hw->mac.ops.release_swfw_sync(hw, swmbsw_mask);

	return ret_val;
}

/**
 *  e1000_rxpbs_adjust_82580 - adjust RXPBS value to reflect actual Rx PBA size
 *  @data: data received by reading RXPBS register
 *
 *  The 82580 uses a table based approach for packet buffer allocation sizes.
 *  This function converts the retrieved value into the correct table value
 *     0x0 0x1 0x2 0x3 0x4 0x5 0x6 0x7
 *  0x0 36  72 144   1   2   4   8  16
 *  0x8 35  70 140 rsv rsv rsv rsv rsv
 */
u16 e1000_rxpbs_adjust_82580(u32 data)
{
	u16 ret_val = 0;

	if (data < E1000_82580_RXPBS_TABLE_SIZE)
		ret_val = e1000_82580_rxpbs_table[data];

	return ret_val;
}

/**
 *  e1000_validate_nvm_checksum_with_offset - Validate EEPROM
 *  checksum
 *  @hw: pointer to the HW structure
 *  @offset: offset in words of the checksum protected region
 *
 *  Calculates the EEPROM checksum by reading/adding each word of the EEPROM
 *  and then verifies that the sum of the EEPROM is equal to 0xBABA.
 **/
s32 e1000_validate_nvm_checksum_with_offset(struct e1000_hw *hw, u16 offset)
{
	s32 ret_val = E1000_SUCCESS;
	u16 checksum = 0;
	u16 i, nvm_data;

	DEBUGFUNC("e1000_validate_nvm_checksum_with_offset");

	for (i = offset; i < ((NVM_CHECKSUM_REG + offset) + 1); i++) {
		ret_val = hw->nvm.ops.read(hw, i, 1, &nvm_data);
		if (ret_val) {
			DEBUGOUT("NVM Read Error\n");
			goto out;
		}
		checksum += nvm_data;
	}

	if (checksum != (u16) NVM_SUM) {
		DEBUGOUT("NVM Checksum Invalid\n");
		ret_val = -E1000_ERR_NVM;
		goto out;
	}

out:
	return ret_val;
}

/**
 *  e1000_update_nvm_checksum_with_offset - Update EEPROM
 *  checksum
 *  @hw: pointer to the HW structure
 *  @offset: offset in words of the checksum protected region
 *
 *  Updates the EEPROM checksum by reading/adding each word of the EEPROM
 *  up to the checksum.  Then calculates the EEPROM checksum and writes the
 *  value to the EEPROM.
 **/
s32 e1000_update_nvm_checksum_with_offset(struct e1000_hw *hw, u16 offset)
{
	s32 ret_val;
	u16 checksum = 0;
	u16 i, nvm_data;

	DEBUGFUNC("e1000_update_nvm_checksum_with_offset");

	for (i = offset; i < (NVM_CHECKSUM_REG + offset); i++) {
		ret_val = hw->nvm.ops.read(hw, i, 1, &nvm_data);
		if (ret_val) {
			DEBUGOUT("NVM Read Error while updating checksum.\n");
			goto out;
		}
		checksum += nvm_data;
	}
	checksum = (u16) NVM_SUM - checksum;
	ret_val = hw->nvm.ops.write(hw, (NVM_CHECKSUM_REG + offset), 1,
				    &checksum);
	if (ret_val)
		DEBUGOUT("NVM Write Error while updating checksum.\n");

out:
	return ret_val;
}

/**
 *  e1000_validate_nvm_checksum_82580 - Validate EEPROM checksum
 *  @hw: pointer to the HW structure
 *
 *  Calculates the EEPROM section checksum by reading/adding each word of
 *  the EEPROM and then verifies that the sum of the EEPROM is
 *  equal to 0xBABA.
 **/
static s32 e1000_validate_nvm_checksum_82580(struct e1000_hw *hw)
{
	s32 ret_val;
	u16 eeprom_regions_count = 1;
	u16 j, nvm_data;
	u16 nvm_offset;

	DEBUGFUNC("e1000_validate_nvm_checksum_82580");

	ret_val = hw->nvm.ops.read(hw, NVM_COMPATIBILITY_REG_3, 1, &nvm_data);
	if (ret_val) {
		DEBUGOUT("NVM Read Error\n");
		goto out;
	}

	if (nvm_data & NVM_COMPATIBILITY_BIT_MASK) {
		/* if chekcsums compatibility bit is set validate checksums
		 * for all 4 ports. */
		eeprom_regions_count = 4;
	}

	for (j = 0; j < eeprom_regions_count; j++) {
		nvm_offset = NVM_82580_LAN_FUNC_OFFSET(j);
		ret_val = e1000_validate_nvm_checksum_with_offset(hw,
								  nvm_offset);
		if (ret_val != E1000_SUCCESS)
			goto out;
	}

out:
	return ret_val;
}

/**
 *  e1000_update_nvm_checksum_82580 - Update EEPROM checksum
 *  @hw: pointer to the HW structure
 *
 *  Updates the EEPROM section checksums for all 4 ports by reading/adding
 *  each word of the EEPROM up to the checksum.  Then calculates the EEPROM
 *  checksum and writes the value to the EEPROM.
 **/
static s32 e1000_update_nvm_checksum_82580(struct e1000_hw *hw)
{
	s32 ret_val;
	u16 j, nvm_data;
	u16 nvm_offset;

	DEBUGFUNC("e1000_update_nvm_checksum_82580");

	ret_val = hw->nvm.ops.read(hw, NVM_COMPATIBILITY_REG_3, 1, &nvm_data);
	if (ret_val) {
		DEBUGOUT("NVM Read Error while updating checksum compatibility bit.\n");
		goto out;
	}

	if (!(nvm_data & NVM_COMPATIBILITY_BIT_MASK)) {
		/* set compatibility bit to validate checksums appropriately */
		nvm_data = nvm_data | NVM_COMPATIBILITY_BIT_MASK;
		ret_val = hw->nvm.ops.write(hw, NVM_COMPATIBILITY_REG_3, 1,
					    &nvm_data);
		if (ret_val) {
			DEBUGOUT("NVM Write Error while updating checksum compatibility bit.\n");
			goto out;
		}
	}

	for (j = 0; j < 4; j++) {
		nvm_offset = NVM_82580_LAN_FUNC_OFFSET(j);
		ret_val = e1000_update_nvm_checksum_with_offset(hw, nvm_offset);
		if (ret_val)
			goto out;
	}

out:
	return ret_val;
}

/**
 *  e1000_validate_nvm_checksum_i350 - Validate EEPROM checksum
 *  @hw: pointer to the HW structure
 *
 *  Calculates the EEPROM section checksum by reading/adding each word of
 *  the EEPROM and then verifies that the sum of the EEPROM is
 *  equal to 0xBABA.
 **/
static s32 e1000_validate_nvm_checksum_i350(struct e1000_hw *hw)
{
	s32 ret_val = E1000_SUCCESS;
	u16 j;
	u16 nvm_offset;

	DEBUGFUNC("e1000_validate_nvm_checksum_i350");

	for (j = 0; j < 4; j++) {
		nvm_offset = NVM_82580_LAN_FUNC_OFFSET(j);
		ret_val = e1000_validate_nvm_checksum_with_offset(hw,
								  nvm_offset);
		if (ret_val != E1000_SUCCESS)
			goto out;
	}

out:
	return ret_val;
}

/**
 *  e1000_update_nvm_checksum_i350 - Update EEPROM checksum
 *  @hw: pointer to the HW structure
 *
 *  Updates the EEPROM section checksums for all 4 ports by reading/adding
 *  each word of the EEPROM up to the checksum.  Then calculates the EEPROM
 *  checksum and writes the value to the EEPROM.
 **/
static s32 e1000_update_nvm_checksum_i350(struct e1000_hw *hw)
{
	s32 ret_val = E1000_SUCCESS;
	u16 j;
	u16 nvm_offset;

	DEBUGFUNC("e1000_update_nvm_checksum_i350");

	for (j = 0; j < 4; j++) {
		nvm_offset = NVM_82580_LAN_FUNC_OFFSET(j);
		ret_val = e1000_update_nvm_checksum_with_offset(hw, nvm_offset);
		if (ret_val != E1000_SUCCESS)
			goto out;
	}

out:
	return ret_val;
}

/**
 *  __e1000_access_emi_reg - Read/write EMI register
 *  @hw: pointer to the HW structure
 *  @addr: EMI address to program
 *  @data: pointer to value to read/write from/to the EMI address
 *  @read: boolean flag to indicate read or write
 **/
static s32 __e1000_access_emi_reg(struct e1000_hw *hw, u16 address,
				  u16 *data, bool read)
{
	s32 ret_val;

	DEBUGFUNC("__e1000_access_emi_reg");

	ret_val = hw->phy.ops.write_reg(hw, E1000_EMIADD, address);
	if (ret_val)
		return ret_val;

	if (read)
		ret_val = hw->phy.ops.read_reg(hw, E1000_EMIDATA, data);
	else
		ret_val = hw->phy.ops.write_reg(hw, E1000_EMIDATA, *data);

	return ret_val;
}

/**
 *  e1000_read_emi_reg - Read Extended Management Interface register
 *  @hw: pointer to the HW structure
 *  @addr: EMI address to program
 *  @data: value to be read from the EMI address
 **/
s32 e1000_read_emi_reg(struct e1000_hw *hw, u16 addr, u16 *data)
{
	DEBUGFUNC("e1000_read_emi_reg");

	return __e1000_access_emi_reg(hw, addr, data, TRUE);
}

/**
 *  e1000_initialize_M88E1512_phy - Initialize M88E1512 PHY
 *  @hw: pointer to the HW structure
 *
 *  Initialize Marvell 1512 to work correctly with Avoton.
 **/
s32 e1000_initialize_M88E1512_phy(struct e1000_hw *hw)
{
	struct e1000_phy_info *phy = &hw->phy;
	s32 ret_val = E1000_SUCCESS;

	DEBUGFUNC("e1000_initialize_M88E1512_phy");

	/* Check if this is correct PHY. */
	if (phy->id != M88E1512_E_PHY_ID)
		goto out;

	/* Switch to PHY page 0xFF. */
	ret_val = phy->ops.write_reg(hw, E1000_M88E1543_PAGE_ADDR, 0x00FF);
	if (ret_val)
		goto out;

	ret_val = phy->ops.write_reg(hw, E1000_M88E1512_CFG_REG_2, 0x214B);
	if (ret_val)
		goto out;

	ret_val = phy->ops.write_reg(hw, E1000_M88E1512_CFG_REG_1, 0x2144);
	if (ret_val)
		goto out;

	ret_val = phy->ops.write_reg(hw, E1000_M88E1512_CFG_REG_2, 0x0C28);
	if (ret_val)
		goto out;

	ret_val = phy->ops.write_reg(hw, E1000_M88E1512_CFG_REG_1, 0x2146);
	if (ret_val)
		goto out;

	ret_val = phy->ops.write_reg(hw, E1000_M88E1512_CFG_REG_2, 0xB233);
	if (ret_val)
		goto out;

	ret_val = phy->ops.write_reg(hw, E1000_M88E1512_CFG_REG_1, 0x214D);
	if (ret_val)
		goto out;

	ret_val = phy->ops.write_reg(hw, E1000_M88E1512_CFG_REG_2, 0xCC0C);
	if (ret_val)
		goto out;

	ret_val = phy->ops.write_reg(hw, E1000_M88E1512_CFG_REG_1, 0x2159);
	if (ret_val)
		goto out;

	/* Switch to PHY page 0xFB. */
	ret_val = phy->ops.write_reg(hw, E1000_M88E1543_PAGE_ADDR, 0x00FB);
	if (ret_val)
		goto out;

	ret_val = phy->ops.write_reg(hw, E1000_M88E1512_CFG_REG_3, 0x000D);
	if (ret_val)
		goto out;

	/* Switch to PHY page 0x12. */
	ret_val = phy->ops.write_reg(hw, E1000_M88E1543_PAGE_ADDR, 0x12);
	if (ret_val)
		goto out;

	/* Change mode to SGMII-to-Copper */
	ret_val = phy->ops.write_reg(hw, E1000_M88E1512_MODE, 0x8001);
	if (ret_val)
		goto out;

	/* Return the PHY to page 0. */
	ret_val = phy->ops.write_reg(hw, E1000_M88E1543_PAGE_ADDR, 0);
	if (ret_val)
		goto out;

	ret_val = phy->ops.commit(hw);
	if (ret_val) {
		DEBUGOUT("Error committing the PHY changes\n");
		return ret_val;
	}

	msec_delay(1000);
out:
	return ret_val;
}

/**
 *  e1000_initialize_M88E1543_phy - Initialize M88E1543 PHY
 *  @hw: pointer to the HW structure
 *
 *  Initialize Marvell 1543 to work correctly with Avoton.
 **/
s32 e1000_initialize_M88E1543_phy(struct e1000_hw *hw)
{
	struct e1000_phy_info *phy = &hw->phy;
	s32 ret_val = E1000_SUCCESS;

	DEBUGFUNC("e1000_initialize_M88E1543_phy");

	/* Check if this is correct PHY. */
	if (phy->id != M88E1543_E_PHY_ID)
		goto out;

	/* Switch to PHY page 0xFF. */
	ret_val = phy->ops.write_reg(hw, E1000_M88E1543_PAGE_ADDR, 0x00FF);
	if (ret_val)
		goto out;

	ret_val = phy->ops.write_reg(hw, E1000_M88E1512_CFG_REG_2, 0x214B);
	if (ret_val)
		goto out;

	ret_val = phy->ops.write_reg(hw, E1000_M88E1512_CFG_REG_1, 0x2144);
	if (ret_val)
		goto out;

	ret_val = phy->ops.write_reg(hw, E1000_M88E1512_CFG_REG_2, 0x0C28);
	if (ret_val)
		goto out;

	ret_val = phy->ops.write_reg(hw, E1000_M88E1512_CFG_REG_1, 0x2146);
	if (ret_val)
		goto out;

	ret_val = phy->ops.write_reg(hw, E1000_M88E1512_CFG_REG_2, 0xB233);
	if (ret_val)
		goto out;

	ret_val = phy->ops.write_reg(hw, E1000_M88E1512_CFG_REG_1, 0x214D);
	if (ret_val)
		goto out;

	ret_val = phy->ops.write_reg(hw, E1000_M88E1512_CFG_REG_2, 0xDC0C);
	if (ret_val)
		goto out;

	ret_val = phy->ops.write_reg(hw, E1000_M88E1512_CFG_REG_1, 0x2159);
	if (ret_val)
		goto out;

	/* Switch to PHY page 0xFB. */
	ret_val = phy->ops.write_reg(hw, E1000_M88E1543_PAGE_ADDR, 0x00FB);
	if (ret_val)
		goto out;

	ret_val = phy->ops.write_reg(hw, E1000_M88E1512_CFG_REG_3, 0xC00D);
	if (ret_val)
		goto out;

	/* Switch to PHY page 0x12. */
	ret_val = phy->ops.write_reg(hw, E1000_M88E1543_PAGE_ADDR, 0x12);
	if (ret_val)
		goto out;

	/* Change mode to SGMII-to-Copper */
	ret_val = phy->ops.write_reg(hw, E1000_M88E1512_MODE, 0x8001);
	if (ret_val)
		goto out;

	/* Switch to PHY page 1. */
	ret_val = phy->ops.write_reg(hw, E1000_M88E1543_PAGE_ADDR, 0x1);
	if (ret_val)
		goto out;

	/* Change mode to 1000BASE-X/SGMII and autoneg enable; reset */
	ret_val = phy->ops.write_reg(hw, E1000_M88E1543_FIBER_CTRL, 0x9140);
	if (ret_val)
		goto out;

	/* Return the PHY to page 0. */
	ret_val = phy->ops.write_reg(hw, E1000_M88E1543_PAGE_ADDR, 0);
	if (ret_val)
		goto out;

	ret_val = phy->ops.commit(hw);
	if (ret_val) {
		DEBUGOUT("Error committing the PHY changes\n");
		return ret_val;
	}

	msec_delay(1000);
out:
	return ret_val;
}

/**
 *  e1000_set_eee_i350 - Enable/disable EEE support
 *  @hw: pointer to the HW structure
 *  @adv1g: boolean flag enabling 1G EEE advertisement
 *  @adv100m: boolean flag enabling 100M EEE advertisement
 *
 *  Enable/disable EEE based on setting in dev_spec structure.
 *
 **/
s32 e1000_set_eee_i350(struct e1000_hw *hw, bool adv1G, bool adv100M)
{
	u32 ipcnfg, eeer;

	DEBUGFUNC("e1000_set_eee_i350");

	if ((hw->mac.type < e1000_i350) ||
	    (hw->phy.media_type != e1000_media_type_copper))
		goto out;
	ipcnfg = E1000_READ_REG(hw, E1000_IPCNFG);
	eeer = E1000_READ_REG(hw, E1000_EEER);

	/* enable or disable per user setting */
	if (!(hw->dev_spec._82575.eee_disable)) {
		u32 eee_su = E1000_READ_REG(hw, E1000_EEE_SU);

		if (adv100M)
			ipcnfg |= E1000_IPCNFG_EEE_100M_AN;
		else
			ipcnfg &= ~E1000_IPCNFG_EEE_100M_AN;

		if (adv1G)
			ipcnfg |= E1000_IPCNFG_EEE_1G_AN;
		else
			ipcnfg &= ~E1000_IPCNFG_EEE_1G_AN;

		eeer |= (E1000_EEER_TX_LPI_EN | E1000_EEER_RX_LPI_EN |
			 E1000_EEER_LPI_FC);

		/* This bit should not be set in normal operation. */
		if (eee_su & E1000_EEE_SU_LPI_CLK_STP)
			DEBUGOUT("LPI Clock Stop Bit should not be set!\n");
	} else {
		ipcnfg &= ~(E1000_IPCNFG_EEE_1G_AN | E1000_IPCNFG_EEE_100M_AN);
		eeer &= ~(E1000_EEER_TX_LPI_EN | E1000_EEER_RX_LPI_EN |
			  E1000_EEER_LPI_FC);
	}
	E1000_WRITE_REG(hw, E1000_IPCNFG, ipcnfg);
	E1000_WRITE_REG(hw, E1000_EEER, eeer);
	E1000_READ_REG(hw, E1000_IPCNFG);
	E1000_READ_REG(hw, E1000_EEER);
out:

	return E1000_SUCCESS;
}

/**
 *  e1000_set_eee_i354 - Enable/disable EEE support
 *  @hw: pointer to the HW structure
 *  @adv1g: boolean flag enabling 1G EEE advertisement
 *  @adv100m: boolean flag enabling 100M EEE advertisement
 *
 *  Enable/disable EEE legacy mode based on setting in dev_spec structure.
 *
 **/
s32 e1000_set_eee_i354(struct e1000_hw *hw, bool adv1G, bool adv100M)
{
	struct e1000_phy_info *phy = &hw->phy;
	s32 ret_val = E1000_SUCCESS;
	u16 phy_data;

	DEBUGFUNC("e1000_set_eee_i354");

	if ((hw->phy.media_type != e1000_media_type_copper) ||
	    ((phy->id != M88E1543_E_PHY_ID) &&
	    (phy->id != M88E1512_E_PHY_ID)))
		goto out;

	if (!hw->dev_spec._82575.eee_disable) {
		/* Switch to PHY page 18. */
		ret_val = phy->ops.write_reg(hw, E1000_M88E1543_PAGE_ADDR, 18);
		if (ret_val)
			goto out;

		ret_val = phy->ops.read_reg(hw, E1000_M88E1543_EEE_CTRL_1,
					    &phy_data);
		if (ret_val)
			goto out;

		phy_data |= E1000_M88E1543_EEE_CTRL_1_MS;
		ret_val = phy->ops.write_reg(hw, E1000_M88E1543_EEE_CTRL_1,
					     phy_data);
		if (ret_val)
			goto out;

		/* Return the PHY to page 0. */
		ret_val = phy->ops.write_reg(hw, E1000_M88E1543_PAGE_ADDR, 0);
		if (ret_val)
			goto out;

		/* Turn on EEE advertisement. */
		ret_val = e1000_read_xmdio_reg(hw, E1000_EEE_ADV_ADDR_I354,
					       E1000_EEE_ADV_DEV_I354,
					       &phy_data);
		if (ret_val)
			goto out;

		if (adv100M)
			phy_data |= E1000_EEE_ADV_100_SUPPORTED;
		else
			phy_data &= ~E1000_EEE_ADV_100_SUPPORTED;

		if (adv1G)
			phy_data |= E1000_EEE_ADV_1000_SUPPORTED;
		else
			phy_data &= ~E1000_EEE_ADV_1000_SUPPORTED;

		ret_val = e1000_write_xmdio_reg(hw, E1000_EEE_ADV_ADDR_I354,
						E1000_EEE_ADV_DEV_I354,
						phy_data);
	} else {
		/* Turn off EEE advertisement. */
		ret_val = e1000_read_xmdio_reg(hw, E1000_EEE_ADV_ADDR_I354,
					       E1000_EEE_ADV_DEV_I354,
					       &phy_data);
		if (ret_val)
			goto out;

		phy_data &= ~(E1000_EEE_ADV_100_SUPPORTED |
			      E1000_EEE_ADV_1000_SUPPORTED);
		ret_val = e1000_write_xmdio_reg(hw, E1000_EEE_ADV_ADDR_I354,
						E1000_EEE_ADV_DEV_I354,
						phy_data);
	}

out:
	return ret_val;
}

/**
 *  e1000_get_eee_status_i354 - Get EEE status
 *  @hw: pointer to the HW structure
 *  @status: EEE status
 *
 *  Get EEE status by guessing based on whether Tx or Rx LPI indications have
 *  been received.
 **/
s32 e1000_get_eee_status_i354(struct e1000_hw *hw, bool *status)
{
	struct e1000_phy_info *phy = &hw->phy;
	s32 ret_val = E1000_SUCCESS;
	u16 phy_data;

	DEBUGFUNC("e1000_get_eee_status_i354");

	/* Check if EEE is supported on this device. */
	if ((hw->phy.media_type != e1000_media_type_copper) ||
	    ((phy->id != M88E1543_E_PHY_ID) &&
	    (phy->id != M88E1512_E_PHY_ID)))
		goto out;

	ret_val = e1000_read_xmdio_reg(hw, E1000_PCS_STATUS_ADDR_I354,
				       E1000_PCS_STATUS_DEV_I354,
				       &phy_data);
	if (ret_val)
		goto out;

	*status = phy_data & (E1000_PCS_STATUS_TX_LPI_RCVD |
			      E1000_PCS_STATUS_RX_LPI_RCVD) ? TRUE : FALSE;

out:
	return ret_val;
}

/* Due to a hw errata, if the host tries to  configure the VFTA register
 * while performing queries from the BMC or DMA, then the VFTA in some
 * cases won't be written.
 */

/**
 *  e1000_clear_vfta_i350 - Clear VLAN filter table
 *  @hw: pointer to the HW structure
 *
 *  Clears the register array which contains the VLAN filter table by
 *  setting all the values to 0.
 **/
void e1000_clear_vfta_i350(struct e1000_hw *hw)
{
	u32 offset;
	int i;

	DEBUGFUNC("e1000_clear_vfta_350");

	for (offset = 0; offset < E1000_VLAN_FILTER_TBL_SIZE; offset++) {
		for (i = 0; i < 10; i++)
			E1000_WRITE_REG_ARRAY(hw, E1000_VFTA, offset, 0);

		E1000_WRITE_FLUSH(hw);
	}
}

/**
 *  e1000_write_vfta_i350 - Write value to VLAN filter table
 *  @hw: pointer to the HW structure
 *  @offset: register offset in VLAN filter table
 *  @value: register value written to VLAN filter table
 *
 *  Writes value at the given offset in the register array which stores
 *  the VLAN filter table.
 **/
void e1000_write_vfta_i350(struct e1000_hw *hw, u32 offset, u32 value)
{
	int i;

	DEBUGFUNC("e1000_write_vfta_350");

	for (i = 0; i < 10; i++)
		E1000_WRITE_REG_ARRAY(hw, E1000_VFTA, offset, value);

	E1000_WRITE_FLUSH(hw);
}


/**
 *  e1000_set_i2c_bb - Enable I2C bit-bang
 *  @hw: pointer to the HW structure
 *
 *  Enable I2C bit-bang interface
 *
 **/
s32 e1000_set_i2c_bb(struct e1000_hw *hw)
{
	s32 ret_val = E1000_SUCCESS;
	u32 ctrl_ext, i2cparams;

	DEBUGFUNC("e1000_set_i2c_bb");

	ctrl_ext = E1000_READ_REG(hw, E1000_CTRL_EXT);
	ctrl_ext |= E1000_CTRL_I2C_ENA;
	E1000_WRITE_REG(hw, E1000_CTRL_EXT, ctrl_ext);
	E1000_WRITE_FLUSH(hw);

	i2cparams = E1000_READ_REG(hw, E1000_I2CPARAMS);
	i2cparams |= E1000_I2CBB_EN;
	i2cparams |= E1000_I2C_DATA_OE_N;
	i2cparams |= E1000_I2C_CLK_OE_N;
	E1000_WRITE_REG(hw, E1000_I2CPARAMS, i2cparams);
	E1000_WRITE_FLUSH(hw);

	return ret_val;
}

/**
 *  e1000_read_i2c_byte_generic - Reads 8 bit word over I2C
 *  @hw: pointer to hardware structure
 *  @byte_offset: byte offset to read
 *  @dev_addr: device address
 *  @data: value read
 *
 *  Performs byte read operation over I2C interface at
 *  a specified device address.
 **/
s32 e1000_read_i2c_byte_generic(struct e1000_hw *hw, u8 byte_offset,
				u8 dev_addr, u8 *data)
{
	s32 status = E1000_SUCCESS;
	u32 max_retry = 10;
	u32 retry = 1;
	u16 swfw_mask = 0;

	bool nack = TRUE;

	DEBUGFUNC("e1000_read_i2c_byte_generic");

	swfw_mask = E1000_SWFW_PHY0_SM;

	do {
		if (hw->mac.ops.acquire_swfw_sync(hw, swfw_mask)
		    != E1000_SUCCESS) {
			status = E1000_ERR_SWFW_SYNC;
			goto read_byte_out;
		}

		e1000_i2c_start(hw);

		/* Device Address and write indication */
		status = e1000_clock_out_i2c_byte(hw, dev_addr);
		if (status != E1000_SUCCESS)
			goto fail;

		status = e1000_get_i2c_ack(hw);
		if (status != E1000_SUCCESS)
			goto fail;

		status = e1000_clock_out_i2c_byte(hw, byte_offset);
		if (status != E1000_SUCCESS)
			goto fail;

		status = e1000_get_i2c_ack(hw);
		if (status != E1000_SUCCESS)
			goto fail;

		e1000_i2c_start(hw);

		/* Device Address and read indication */
		status = e1000_clock_out_i2c_byte(hw, (dev_addr | 0x1));
		if (status != E1000_SUCCESS)
			goto fail;

		status = e1000_get_i2c_ack(hw);
		if (status != E1000_SUCCESS)
			goto fail;

		status = e1000_clock_in_i2c_byte(hw, data);
		if (status != E1000_SUCCESS)
			goto fail;

		status = e1000_clock_out_i2c_bit(hw, nack);
		if (status != E1000_SUCCESS)
			goto fail;

		e1000_i2c_stop(hw);
		break;

fail:
		hw->mac.ops.release_swfw_sync(hw, swfw_mask);
		msec_delay(100);
		e1000_i2c_bus_clear(hw);
		retry++;
		if (retry < max_retry)
			DEBUGOUT("I2C byte read error - Retrying.\n");
		else
			DEBUGOUT("I2C byte read error.\n");

	} while (retry < max_retry);

	hw->mac.ops.release_swfw_sync(hw, swfw_mask);

read_byte_out:

	return status;
}

/**
 *  e1000_write_i2c_byte_generic - Writes 8 bit word over I2C
 *  @hw: pointer to hardware structure
 *  @byte_offset: byte offset to write
 *  @dev_addr: device address
 *  @data: value to write
 *
 *  Performs byte write operation over I2C interface at
 *  a specified device address.
 **/
s32 e1000_write_i2c_byte_generic(struct e1000_hw *hw, u8 byte_offset,
				 u8 dev_addr, u8 data)
{
	s32 status = E1000_SUCCESS;
	u32 max_retry = 1;
	u32 retry = 0;
	u16 swfw_mask = 0;

	DEBUGFUNC("e1000_write_i2c_byte_generic");

	swfw_mask = E1000_SWFW_PHY0_SM;

	if (hw->mac.ops.acquire_swfw_sync(hw, swfw_mask) != E1000_SUCCESS) {
		status = E1000_ERR_SWFW_SYNC;
		goto write_byte_out;
	}

	do {
		e1000_i2c_start(hw);

		status = e1000_clock_out_i2c_byte(hw, dev_addr);
		if (status != E1000_SUCCESS)
			goto fail;

		status = e1000_get_i2c_ack(hw);
		if (status != E1000_SUCCESS)
			goto fail;

		status = e1000_clock_out_i2c_byte(hw, byte_offset);
		if (status != E1000_SUCCESS)
			goto fail;

		status = e1000_get_i2c_ack(hw);
		if (status != E1000_SUCCESS)
			goto fail;

		status = e1000_clock_out_i2c_byte(hw, data);
		if (status != E1000_SUCCESS)
			goto fail;

		status = e1000_get_i2c_ack(hw);
		if (status != E1000_SUCCESS)
			goto fail;

		e1000_i2c_stop(hw);
		break;

fail:
		e1000_i2c_bus_clear(hw);
		retry++;
		if (retry < max_retry)
			DEBUGOUT("I2C byte write error - Retrying.\n");
		else
			DEBUGOUT("I2C byte write error.\n");
	} while (retry < max_retry);

	hw->mac.ops.release_swfw_sync(hw, swfw_mask);

write_byte_out:

	return status;
}

/**
 *  e1000_i2c_start - Sets I2C start condition
 *  @hw: pointer to hardware structure
 *
 *  Sets I2C start condition (High -> Low on SDA while SCL is High)
 **/
static void e1000_i2c_start(struct e1000_hw *hw)
{
	u32 i2cctl = E1000_READ_REG(hw, E1000_I2CPARAMS);

	DEBUGFUNC("e1000_i2c_start");

	/* Start condition must begin with data and clock high */
	e1000_set_i2c_data(hw, &i2cctl, 1);
	e1000_raise_i2c_clk(hw, &i2cctl);

	/* Setup time for start condition (4.7us) */
	usec_delay(E1000_I2C_T_SU_STA);

	e1000_set_i2c_data(hw, &i2cctl, 0);

	/* Hold time for start condition (4us) */
	usec_delay(E1000_I2C_T_HD_STA);

	e1000_lower_i2c_clk(hw, &i2cctl);

	/* Minimum low period of clock is 4.7 us */
	usec_delay(E1000_I2C_T_LOW);

}

/**
 *  e1000_i2c_stop - Sets I2C stop condition
 *  @hw: pointer to hardware structure
 *
 *  Sets I2C stop condition (Low -> High on SDA while SCL is High)
 **/
static void e1000_i2c_stop(struct e1000_hw *hw)
{
	u32 i2cctl = E1000_READ_REG(hw, E1000_I2CPARAMS);

	DEBUGFUNC("e1000_i2c_stop");

	/* Stop condition must begin with data low and clock high */
	e1000_set_i2c_data(hw, &i2cctl, 0);
	e1000_raise_i2c_clk(hw, &i2cctl);

	/* Setup time for stop condition (4us) */
	usec_delay(E1000_I2C_T_SU_STO);

	e1000_set_i2c_data(hw, &i2cctl, 1);

	/* bus free time between stop and start (4.7us)*/
	usec_delay(E1000_I2C_T_BUF);
}

/**
 *  e1000_clock_in_i2c_byte - Clocks in one byte via I2C
 *  @hw: pointer to hardware structure
 *  @data: data byte to clock in
 *
 *  Clocks in one byte data via I2C data/clock
 **/
static s32 e1000_clock_in_i2c_byte(struct e1000_hw *hw, u8 *data)
{
	s32 i;
	bool bit = 0;

	DEBUGFUNC("e1000_clock_in_i2c_byte");

	*data = 0;
	for (i = 7; i >= 0; i--) {
		e1000_clock_in_i2c_bit(hw, &bit);
		*data |= bit << i;
	}

	return E1000_SUCCESS;
}

/**
 *  e1000_clock_out_i2c_byte - Clocks out one byte via I2C
 *  @hw: pointer to hardware structure
 *  @data: data byte clocked out
 *
 *  Clocks out one byte data via I2C data/clock
 **/
static s32 e1000_clock_out_i2c_byte(struct e1000_hw *hw, u8 data)
{
	s32 status = E1000_SUCCESS;
	s32 i;
	u32 i2cctl;
	bool bit = 0;

	DEBUGFUNC("e1000_clock_out_i2c_byte");

	for (i = 7; i >= 0; i--) {
		bit = (data >> i) & 0x1;
		status = e1000_clock_out_i2c_bit(hw, bit);

		if (status != E1000_SUCCESS)
			break;
	}

	/* Release SDA line (set high) */
	i2cctl = E1000_READ_REG(hw, E1000_I2CPARAMS);

	i2cctl |= E1000_I2C_DATA_OE_N;
	E1000_WRITE_REG(hw, E1000_I2CPARAMS, i2cctl);
	E1000_WRITE_FLUSH(hw);

	return status;
}

/**
 *  e1000_get_i2c_ack - Polls for I2C ACK
 *  @hw: pointer to hardware structure
 *
 *  Clocks in/out one bit via I2C data/clock
 **/
static s32 e1000_get_i2c_ack(struct e1000_hw *hw)
{
	s32 status = E1000_SUCCESS;
	u32 i = 0;
	u32 i2cctl = E1000_READ_REG(hw, E1000_I2CPARAMS);
	u32 timeout = 10;
	bool ack = TRUE;

	DEBUGFUNC("e1000_get_i2c_ack");

	e1000_raise_i2c_clk(hw, &i2cctl);

	/* Minimum high period of clock is 4us */
	usec_delay(E1000_I2C_T_HIGH);

	/* Wait until SCL returns high */
	for (i = 0; i < timeout; i++) {
		usec_delay(1);
		i2cctl = E1000_READ_REG(hw, E1000_I2CPARAMS);
		if (i2cctl & E1000_I2C_CLK_IN)
			break;
	}
	if (!(i2cctl & E1000_I2C_CLK_IN))
		return E1000_ERR_I2C;

	ack = e1000_get_i2c_data(&i2cctl);
	if (ack) {
		DEBUGOUT("I2C ack was not received.\n");
		status = E1000_ERR_I2C;
	}

	e1000_lower_i2c_clk(hw, &i2cctl);

	/* Minimum low period of clock is 4.7 us */
	usec_delay(E1000_I2C_T_LOW);

	return status;
}

/**
 *  e1000_clock_in_i2c_bit - Clocks in one bit via I2C data/clock
 *  @hw: pointer to hardware structure
 *  @data: read data value
 *
 *  Clocks in one bit via I2C data/clock
 **/
static s32 e1000_clock_in_i2c_bit(struct e1000_hw *hw, bool *data)
{
	u32 i2cctl = E1000_READ_REG(hw, E1000_I2CPARAMS);

	DEBUGFUNC("e1000_clock_in_i2c_bit");

	e1000_raise_i2c_clk(hw, &i2cctl);

	/* Minimum high period of clock is 4us */
	usec_delay(E1000_I2C_T_HIGH);

	i2cctl = E1000_READ_REG(hw, E1000_I2CPARAMS);
	*data = e1000_get_i2c_data(&i2cctl);

	e1000_lower_i2c_clk(hw, &i2cctl);

	/* Minimum low period of clock is 4.7 us */
	usec_delay(E1000_I2C_T_LOW);

	return E1000_SUCCESS;
}

/**
 *  e1000_clock_out_i2c_bit - Clocks in/out one bit via I2C data/clock
 *  @hw: pointer to hardware structure
 *  @data: data value to write
 *
 *  Clocks out one bit via I2C data/clock
 **/
static s32 e1000_clock_out_i2c_bit(struct e1000_hw *hw, bool data)
{
	s32 status;
	u32 i2cctl = E1000_READ_REG(hw, E1000_I2CPARAMS);

	DEBUGFUNC("e1000_clock_out_i2c_bit");

	status = e1000_set_i2c_data(hw, &i2cctl, data);
	if (status == E1000_SUCCESS) {
		e1000_raise_i2c_clk(hw, &i2cctl);

		/* Minimum high period of clock is 4us */
		usec_delay(E1000_I2C_T_HIGH);

		e1000_lower_i2c_clk(hw, &i2cctl);

		/* Minimum low period of clock is 4.7 us.
		 * This also takes care of the data hold time.
		 */
		usec_delay(E1000_I2C_T_LOW);
	} else {
		status = E1000_ERR_I2C;
		DEBUGOUT1("I2C data was not set to %X\n", data);
	}

	return status;
}
/**
 *  e1000_raise_i2c_clk - Raises the I2C SCL clock
 *  @hw: pointer to hardware structure
 *  @i2cctl: Current value of I2CCTL register
 *
 *  Raises the I2C clock line '0'->'1'
 **/
static void e1000_raise_i2c_clk(struct e1000_hw *hw, u32 *i2cctl)
{
	DEBUGFUNC("e1000_raise_i2c_clk");

	*i2cctl |= E1000_I2C_CLK_OUT;
	*i2cctl &= ~E1000_I2C_CLK_OE_N;
	E1000_WRITE_REG(hw, E1000_I2CPARAMS, *i2cctl);
	E1000_WRITE_FLUSH(hw);

	/* SCL rise time (1000ns) */
	usec_delay(E1000_I2C_T_RISE);
}

/**
 *  e1000_lower_i2c_clk - Lowers the I2C SCL clock
 *  @hw: pointer to hardware structure
 *  @i2cctl: Current value of I2CCTL register
 *
 *  Lowers the I2C clock line '1'->'0'
 **/
static void e1000_lower_i2c_clk(struct e1000_hw *hw, u32 *i2cctl)
{

	DEBUGFUNC("e1000_lower_i2c_clk");

	*i2cctl &= ~E1000_I2C_CLK_OUT;
	*i2cctl &= ~E1000_I2C_CLK_OE_N;
	E1000_WRITE_REG(hw, E1000_I2CPARAMS, *i2cctl);
	E1000_WRITE_FLUSH(hw);

	/* SCL fall time (300ns) */
	usec_delay(E1000_I2C_T_FALL);
}

/**
 *  e1000_set_i2c_data - Sets the I2C data bit
 *  @hw: pointer to hardware structure
 *  @i2cctl: Current value of I2CCTL register
 *  @data: I2C data value (0 or 1) to set
 *
 *  Sets the I2C data bit
 **/
static s32 e1000_set_i2c_data(struct e1000_hw *hw, u32 *i2cctl, bool data)
{
	s32 status = E1000_SUCCESS;

	DEBUGFUNC("e1000_set_i2c_data");

	if (data)
		*i2cctl |= E1000_I2C_DATA_OUT;
	else
		*i2cctl &= ~E1000_I2C_DATA_OUT;

	*i2cctl &= ~E1000_I2C_DATA_OE_N;
	*i2cctl |= E1000_I2C_CLK_OE_N;
	E1000_WRITE_REG(hw, E1000_I2CPARAMS, *i2cctl);
	E1000_WRITE_FLUSH(hw);

	/* Data rise/fall (1000ns/300ns) and set-up time (250ns) */
	usec_delay(E1000_I2C_T_RISE + E1000_I2C_T_FALL + E1000_I2C_T_SU_DATA);

	*i2cctl = E1000_READ_REG(hw, E1000_I2CPARAMS);
	if (data != e1000_get_i2c_data(i2cctl)) {
		status = E1000_ERR_I2C;
		DEBUGOUT1("Error - I2C data was not set to %X.\n", data);
	}

	return status;
}

/**
 *  e1000_get_i2c_data - Reads the I2C SDA data bit
 *  @hw: pointer to hardware structure
 *  @i2cctl: Current value of I2CCTL register
 *
 *  Returns the I2C data bit value
 **/
static bool e1000_get_i2c_data(u32 *i2cctl)
{
	bool data;

	DEBUGFUNC("e1000_get_i2c_data");

	if (*i2cctl & E1000_I2C_DATA_IN)
		data = 1;
	else
		data = 0;

	return data;
}

/**
 *  e1000_i2c_bus_clear - Clears the I2C bus
 *  @hw: pointer to hardware structure
 *
 *  Clears the I2C bus by sending nine clock pulses.
 *  Used when data line is stuck low.
 **/
void e1000_i2c_bus_clear(struct e1000_hw *hw)
{
	u32 i2cctl = E1000_READ_REG(hw, E1000_I2CPARAMS);
	u32 i;

	DEBUGFUNC("e1000_i2c_bus_clear");

	e1000_i2c_start(hw);

	e1000_set_i2c_data(hw, &i2cctl, 1);

	for (i = 0; i < 9; i++) {
		e1000_raise_i2c_clk(hw, &i2cctl);

		/* Min high period of clock is 4us */
		usec_delay(E1000_I2C_T_HIGH);

		e1000_lower_i2c_clk(hw, &i2cctl);

		/* Min low period of clock is 4.7us*/
		usec_delay(E1000_I2C_T_LOW);
	}

	e1000_i2c_start(hw);

	/* Put the i2c bus back to default state */
	e1000_i2c_stop(hw);
}

