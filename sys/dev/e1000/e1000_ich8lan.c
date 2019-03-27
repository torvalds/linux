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

/* 82562G 10/100 Network Connection
 * 82562G-2 10/100 Network Connection
 * 82562GT 10/100 Network Connection
 * 82562GT-2 10/100 Network Connection
 * 82562V 10/100 Network Connection
 * 82562V-2 10/100 Network Connection
 * 82566DC-2 Gigabit Network Connection
 * 82566DC Gigabit Network Connection
 * 82566DM-2 Gigabit Network Connection
 * 82566DM Gigabit Network Connection
 * 82566MC Gigabit Network Connection
 * 82566MM Gigabit Network Connection
 * 82567LM Gigabit Network Connection
 * 82567LF Gigabit Network Connection
 * 82567V Gigabit Network Connection
 * 82567LM-2 Gigabit Network Connection
 * 82567LF-2 Gigabit Network Connection
 * 82567V-2 Gigabit Network Connection
 * 82567LF-3 Gigabit Network Connection
 * 82567LM-3 Gigabit Network Connection
 * 82567LM-4 Gigabit Network Connection
 * 82577LM Gigabit Network Connection
 * 82577LC Gigabit Network Connection
 * 82578DM Gigabit Network Connection
 * 82578DC Gigabit Network Connection
 * 82579LM Gigabit Network Connection
 * 82579V Gigabit Network Connection
 * Ethernet Connection I217-LM
 * Ethernet Connection I217-V
 * Ethernet Connection I218-V
 * Ethernet Connection I218-LM
 * Ethernet Connection (2) I218-LM
 * Ethernet Connection (2) I218-V
 * Ethernet Connection (3) I218-LM
 * Ethernet Connection (3) I218-V
 */

#include "e1000_api.h"

static s32  e1000_acquire_swflag_ich8lan(struct e1000_hw *hw);
static void e1000_release_swflag_ich8lan(struct e1000_hw *hw);
static s32  e1000_acquire_nvm_ich8lan(struct e1000_hw *hw);
static void e1000_release_nvm_ich8lan(struct e1000_hw *hw);
static bool e1000_check_mng_mode_ich8lan(struct e1000_hw *hw);
static bool e1000_check_mng_mode_pchlan(struct e1000_hw *hw);
static int  e1000_rar_set_pch2lan(struct e1000_hw *hw, u8 *addr, u32 index);
static int  e1000_rar_set_pch_lpt(struct e1000_hw *hw, u8 *addr, u32 index);
static s32 e1000_sw_lcd_config_ich8lan(struct e1000_hw *hw);
static void e1000_update_mc_addr_list_pch2lan(struct e1000_hw *hw,
					      u8 *mc_addr_list,
					      u32 mc_addr_count);
static s32  e1000_check_reset_block_ich8lan(struct e1000_hw *hw);
static s32  e1000_phy_hw_reset_ich8lan(struct e1000_hw *hw);
static s32  e1000_set_lplu_state_pchlan(struct e1000_hw *hw, bool active);
static s32  e1000_set_d0_lplu_state_ich8lan(struct e1000_hw *hw,
					    bool active);
static s32  e1000_set_d3_lplu_state_ich8lan(struct e1000_hw *hw,
					    bool active);
static s32  e1000_read_nvm_ich8lan(struct e1000_hw *hw, u16 offset,
				   u16 words, u16 *data);
static s32  e1000_read_nvm_spt(struct e1000_hw *hw, u16 offset, u16 words,
			       u16 *data);
static s32  e1000_write_nvm_ich8lan(struct e1000_hw *hw, u16 offset,
				    u16 words, u16 *data);
static s32  e1000_validate_nvm_checksum_ich8lan(struct e1000_hw *hw);
static s32  e1000_update_nvm_checksum_ich8lan(struct e1000_hw *hw);
static s32  e1000_update_nvm_checksum_spt(struct e1000_hw *hw);
static s32  e1000_valid_led_default_ich8lan(struct e1000_hw *hw,
					    u16 *data);
static s32 e1000_id_led_init_pchlan(struct e1000_hw *hw);
static s32  e1000_get_bus_info_ich8lan(struct e1000_hw *hw);
static s32  e1000_reset_hw_ich8lan(struct e1000_hw *hw);
static s32  e1000_init_hw_ich8lan(struct e1000_hw *hw);
static s32  e1000_setup_link_ich8lan(struct e1000_hw *hw);
static s32  e1000_setup_copper_link_ich8lan(struct e1000_hw *hw);
static s32  e1000_setup_copper_link_pch_lpt(struct e1000_hw *hw);
static s32  e1000_get_link_up_info_ich8lan(struct e1000_hw *hw,
					   u16 *speed, u16 *duplex);
static s32  e1000_cleanup_led_ich8lan(struct e1000_hw *hw);
static s32  e1000_led_on_ich8lan(struct e1000_hw *hw);
static s32  e1000_led_off_ich8lan(struct e1000_hw *hw);
static s32  e1000_k1_gig_workaround_hv(struct e1000_hw *hw, bool link);
static s32  e1000_setup_led_pchlan(struct e1000_hw *hw);
static s32  e1000_cleanup_led_pchlan(struct e1000_hw *hw);
static s32  e1000_led_on_pchlan(struct e1000_hw *hw);
static s32  e1000_led_off_pchlan(struct e1000_hw *hw);
static void e1000_clear_hw_cntrs_ich8lan(struct e1000_hw *hw);
static s32  e1000_erase_flash_bank_ich8lan(struct e1000_hw *hw, u32 bank);
static void e1000_initialize_hw_bits_ich8lan(struct e1000_hw *hw);
static s32  e1000_kmrn_lock_loss_workaround_ich8lan(struct e1000_hw *hw);
static s32  e1000_read_flash_byte_ich8lan(struct e1000_hw *hw,
					  u32 offset, u8 *data);
static s32  e1000_read_flash_data_ich8lan(struct e1000_hw *hw, u32 offset,
					  u8 size, u16 *data);
static s32  e1000_read_flash_data32_ich8lan(struct e1000_hw *hw, u32 offset,
					    u32 *data);
static s32  e1000_read_flash_dword_ich8lan(struct e1000_hw *hw,
					   u32 offset, u32 *data);
static s32  e1000_write_flash_data32_ich8lan(struct e1000_hw *hw,
					     u32 offset, u32 data);
static s32  e1000_retry_write_flash_dword_ich8lan(struct e1000_hw *hw,
						  u32 offset, u32 dword);
static s32  e1000_read_flash_word_ich8lan(struct e1000_hw *hw,
					  u32 offset, u16 *data);
static s32  e1000_retry_write_flash_byte_ich8lan(struct e1000_hw *hw,
						 u32 offset, u8 byte);
static s32 e1000_get_cfg_done_ich8lan(struct e1000_hw *hw);
static void e1000_power_down_phy_copper_ich8lan(struct e1000_hw *hw);
static s32 e1000_check_for_copper_link_ich8lan(struct e1000_hw *hw);
static s32 e1000_set_mdio_slow_mode_hv(struct e1000_hw *hw);
static s32 e1000_k1_workaround_lv(struct e1000_hw *hw);
static void e1000_gate_hw_phy_config_ich8lan(struct e1000_hw *hw, bool gate);
static s32 e1000_set_obff_timer_pch_lpt(struct e1000_hw *hw, u32 itr);

/* ICH GbE Flash Hardware Sequencing Flash Status Register bit breakdown */
/* Offset 04h HSFSTS */
union ich8_hws_flash_status {
	struct ich8_hsfsts {
		u16 flcdone:1; /* bit 0 Flash Cycle Done */
		u16 flcerr:1; /* bit 1 Flash Cycle Error */
		u16 dael:1; /* bit 2 Direct Access error Log */
		u16 berasesz:2; /* bit 4:3 Sector Erase Size */
		u16 flcinprog:1; /* bit 5 flash cycle in Progress */
		u16 reserved1:2; /* bit 13:6 Reserved */
		u16 reserved2:6; /* bit 13:6 Reserved */
		u16 fldesvalid:1; /* bit 14 Flash Descriptor Valid */
		u16 flockdn:1; /* bit 15 Flash Config Lock-Down */
	} hsf_status;
	u16 regval;
};

/* ICH GbE Flash Hardware Sequencing Flash control Register bit breakdown */
/* Offset 06h FLCTL */
union ich8_hws_flash_ctrl {
	struct ich8_hsflctl {
		u16 flcgo:1;   /* 0 Flash Cycle Go */
		u16 flcycle:2;   /* 2:1 Flash Cycle */
		u16 reserved:5;   /* 7:3 Reserved  */
		u16 fldbcount:2;   /* 9:8 Flash Data Byte Count */
		u16 flockdn:6;   /* 15:10 Reserved */
	} hsf_ctrl;
	u16 regval;
};

/* ICH Flash Region Access Permissions */
union ich8_hws_flash_regacc {
	struct ich8_flracc {
		u32 grra:8; /* 0:7 GbE region Read Access */
		u32 grwa:8; /* 8:15 GbE region Write Access */
		u32 gmrag:8; /* 23:16 GbE Master Read Access Grant */
		u32 gmwag:8; /* 31:24 GbE Master Write Access Grant */
	} hsf_flregacc;
	u16 regval;
};

/**
 *  e1000_phy_is_accessible_pchlan - Check if able to access PHY registers
 *  @hw: pointer to the HW structure
 *
 *  Test access to the PHY registers by reading the PHY ID registers.  If
 *  the PHY ID is already known (e.g. resume path) compare it with known ID,
 *  otherwise assume the read PHY ID is correct if it is valid.
 *
 *  Assumes the sw/fw/hw semaphore is already acquired.
 **/
static bool e1000_phy_is_accessible_pchlan(struct e1000_hw *hw)
{
	u16 phy_reg = 0;
	u32 phy_id = 0;
	s32 ret_val = 0;
	u16 retry_count;
	u32 mac_reg = 0;

	for (retry_count = 0; retry_count < 2; retry_count++) {
		ret_val = hw->phy.ops.read_reg_locked(hw, PHY_ID1, &phy_reg);
		if (ret_val || (phy_reg == 0xFFFF))
			continue;
		phy_id = (u32)(phy_reg << 16);

		ret_val = hw->phy.ops.read_reg_locked(hw, PHY_ID2, &phy_reg);
		if (ret_val || (phy_reg == 0xFFFF)) {
			phy_id = 0;
			continue;
		}
		phy_id |= (u32)(phy_reg & PHY_REVISION_MASK);
		break;
	}

	if (hw->phy.id) {
		if  (hw->phy.id == phy_id)
			goto out;
	} else if (phy_id) {
		hw->phy.id = phy_id;
		hw->phy.revision = (u32)(phy_reg & ~PHY_REVISION_MASK);
		goto out;
	}

	/* In case the PHY needs to be in mdio slow mode,
	 * set slow mode and try to get the PHY id again.
	 */
	if (hw->mac.type < e1000_pch_lpt) {
		hw->phy.ops.release(hw);
		ret_val = e1000_set_mdio_slow_mode_hv(hw);
		if (!ret_val)
			ret_val = e1000_get_phy_id(hw);
		hw->phy.ops.acquire(hw);
	}

	if (ret_val)
		return FALSE;
out:
	if (hw->mac.type >= e1000_pch_lpt) {
		/* Only unforce SMBus if ME is not active */
		if (!(E1000_READ_REG(hw, E1000_FWSM) &
		    E1000_ICH_FWSM_FW_VALID)) {
			/* Unforce SMBus mode in PHY */
			hw->phy.ops.read_reg_locked(hw, CV_SMB_CTRL, &phy_reg);
			phy_reg &= ~CV_SMB_CTRL_FORCE_SMBUS;
			hw->phy.ops.write_reg_locked(hw, CV_SMB_CTRL, phy_reg);

			/* Unforce SMBus mode in MAC */
			mac_reg = E1000_READ_REG(hw, E1000_CTRL_EXT);
			mac_reg &= ~E1000_CTRL_EXT_FORCE_SMBUS;
			E1000_WRITE_REG(hw, E1000_CTRL_EXT, mac_reg);
		}
	}

	return TRUE;
}

/**
 *  e1000_toggle_lanphypc_pch_lpt - toggle the LANPHYPC pin value
 *  @hw: pointer to the HW structure
 *
 *  Toggling the LANPHYPC pin value fully power-cycles the PHY and is
 *  used to reset the PHY to a quiescent state when necessary.
 **/
static void e1000_toggle_lanphypc_pch_lpt(struct e1000_hw *hw)
{
	u32 mac_reg;

	DEBUGFUNC("e1000_toggle_lanphypc_pch_lpt");

	/* Set Phy Config Counter to 50msec */
	mac_reg = E1000_READ_REG(hw, E1000_FEXTNVM3);
	mac_reg &= ~E1000_FEXTNVM3_PHY_CFG_COUNTER_MASK;
	mac_reg |= E1000_FEXTNVM3_PHY_CFG_COUNTER_50MSEC;
	E1000_WRITE_REG(hw, E1000_FEXTNVM3, mac_reg);

	/* Toggle LANPHYPC Value bit */
	mac_reg = E1000_READ_REG(hw, E1000_CTRL);
	mac_reg |= E1000_CTRL_LANPHYPC_OVERRIDE;
	mac_reg &= ~E1000_CTRL_LANPHYPC_VALUE;
	E1000_WRITE_REG(hw, E1000_CTRL, mac_reg);
	E1000_WRITE_FLUSH(hw);
	msec_delay(1);
	mac_reg &= ~E1000_CTRL_LANPHYPC_OVERRIDE;
	E1000_WRITE_REG(hw, E1000_CTRL, mac_reg);
	E1000_WRITE_FLUSH(hw);

	if (hw->mac.type < e1000_pch_lpt) {
		msec_delay(50);
	} else {
		u16 count = 20;

		do {
			msec_delay(5);
		} while (!(E1000_READ_REG(hw, E1000_CTRL_EXT) &
			   E1000_CTRL_EXT_LPCD) && count--);

		msec_delay(30);
	}
}

/**
 *  e1000_init_phy_workarounds_pchlan - PHY initialization workarounds
 *  @hw: pointer to the HW structure
 *
 *  Workarounds/flow necessary for PHY initialization during driver load
 *  and resume paths.
 **/
static s32 e1000_init_phy_workarounds_pchlan(struct e1000_hw *hw)
{
	u32 mac_reg, fwsm = E1000_READ_REG(hw, E1000_FWSM);
	s32 ret_val;

	DEBUGFUNC("e1000_init_phy_workarounds_pchlan");

	/* Gate automatic PHY configuration by hardware on managed and
	 * non-managed 82579 and newer adapters.
	 */
	e1000_gate_hw_phy_config_ich8lan(hw, TRUE);

	/* It is not possible to be certain of the current state of ULP
	 * so forcibly disable it.
	 */
	hw->dev_spec.ich8lan.ulp_state = e1000_ulp_state_unknown;
	e1000_disable_ulp_lpt_lp(hw, TRUE);

	ret_val = hw->phy.ops.acquire(hw);
	if (ret_val) {
		DEBUGOUT("Failed to initialize PHY flow\n");
		goto out;
	}

	/* The MAC-PHY interconnect may be in SMBus mode.  If the PHY is
	 * inaccessible and resetting the PHY is not blocked, toggle the
	 * LANPHYPC Value bit to force the interconnect to PCIe mode.
	 */
	switch (hw->mac.type) {
	case e1000_pch_lpt:
	case e1000_pch_spt:
	case e1000_pch_cnp:
		if (e1000_phy_is_accessible_pchlan(hw))
			break;

		/* Before toggling LANPHYPC, see if PHY is accessible by
		 * forcing MAC to SMBus mode first.
		 */
		mac_reg = E1000_READ_REG(hw, E1000_CTRL_EXT);
		mac_reg |= E1000_CTRL_EXT_FORCE_SMBUS;
		E1000_WRITE_REG(hw, E1000_CTRL_EXT, mac_reg);

		/* Wait 50 milliseconds for MAC to finish any retries
		 * that it might be trying to perform from previous
		 * attempts to acknowledge any phy read requests.
		 */
		 msec_delay(50);

		/* fall-through */
	case e1000_pch2lan:
		if (e1000_phy_is_accessible_pchlan(hw))
			break;

		/* fall-through */
	case e1000_pchlan:
		if ((hw->mac.type == e1000_pchlan) &&
		    (fwsm & E1000_ICH_FWSM_FW_VALID))
			break;

		if (hw->phy.ops.check_reset_block(hw)) {
			DEBUGOUT("Required LANPHYPC toggle blocked by ME\n");
			ret_val = -E1000_ERR_PHY;
			break;
		}

		/* Toggle LANPHYPC Value bit */
		e1000_toggle_lanphypc_pch_lpt(hw);
		if (hw->mac.type >= e1000_pch_lpt) {
			if (e1000_phy_is_accessible_pchlan(hw))
				break;

			/* Toggling LANPHYPC brings the PHY out of SMBus mode
			 * so ensure that the MAC is also out of SMBus mode
			 */
			mac_reg = E1000_READ_REG(hw, E1000_CTRL_EXT);
			mac_reg &= ~E1000_CTRL_EXT_FORCE_SMBUS;
			E1000_WRITE_REG(hw, E1000_CTRL_EXT, mac_reg);

			if (e1000_phy_is_accessible_pchlan(hw))
				break;

			ret_val = -E1000_ERR_PHY;
		}
		break;
	default:
		break;
	}

	hw->phy.ops.release(hw);
	if (!ret_val) {

		/* Check to see if able to reset PHY.  Print error if not */
		if (hw->phy.ops.check_reset_block(hw)) {
			ERROR_REPORT("Reset blocked by ME\n");
			goto out;
		}

		/* Reset the PHY before any access to it.  Doing so, ensures
		 * that the PHY is in a known good state before we read/write
		 * PHY registers.  The generic reset is sufficient here,
		 * because we haven't determined the PHY type yet.
		 */
		ret_val = e1000_phy_hw_reset_generic(hw);
		if (ret_val)
			goto out;

		/* On a successful reset, possibly need to wait for the PHY
		 * to quiesce to an accessible state before returning control
		 * to the calling function.  If the PHY does not quiesce, then
		 * return E1000E_BLK_PHY_RESET, as this is the condition that
		 *  the PHY is in.
		 */
		ret_val = hw->phy.ops.check_reset_block(hw);
		if (ret_val)
			ERROR_REPORT("ME blocked access to PHY after reset\n");
	}

out:
	/* Ungate automatic PHY configuration on non-managed 82579 */
	if ((hw->mac.type == e1000_pch2lan) &&
	    !(fwsm & E1000_ICH_FWSM_FW_VALID)) {
		msec_delay(10);
		e1000_gate_hw_phy_config_ich8lan(hw, FALSE);
	}

	return ret_val;
}

/**
 *  e1000_init_phy_params_pchlan - Initialize PHY function pointers
 *  @hw: pointer to the HW structure
 *
 *  Initialize family-specific PHY parameters and function pointers.
 **/
static s32 e1000_init_phy_params_pchlan(struct e1000_hw *hw)
{
	struct e1000_phy_info *phy = &hw->phy;
	s32 ret_val;

	DEBUGFUNC("e1000_init_phy_params_pchlan");

	phy->addr		= 1;
	phy->reset_delay_us	= 100;

	phy->ops.acquire	= e1000_acquire_swflag_ich8lan;
	phy->ops.check_reset_block = e1000_check_reset_block_ich8lan;
	phy->ops.get_cfg_done	= e1000_get_cfg_done_ich8lan;
	phy->ops.set_page	= e1000_set_page_igp;
	phy->ops.read_reg	= e1000_read_phy_reg_hv;
	phy->ops.read_reg_locked = e1000_read_phy_reg_hv_locked;
	phy->ops.read_reg_page	= e1000_read_phy_reg_page_hv;
	phy->ops.release	= e1000_release_swflag_ich8lan;
	phy->ops.reset		= e1000_phy_hw_reset_ich8lan;
	phy->ops.set_d0_lplu_state = e1000_set_lplu_state_pchlan;
	phy->ops.set_d3_lplu_state = e1000_set_lplu_state_pchlan;
	phy->ops.write_reg	= e1000_write_phy_reg_hv;
	phy->ops.write_reg_locked = e1000_write_phy_reg_hv_locked;
	phy->ops.write_reg_page	= e1000_write_phy_reg_page_hv;
	phy->ops.power_up	= e1000_power_up_phy_copper;
	phy->ops.power_down	= e1000_power_down_phy_copper_ich8lan;
	phy->autoneg_mask	= AUTONEG_ADVERTISE_SPEED_DEFAULT;

	phy->id = e1000_phy_unknown;

	ret_val = e1000_init_phy_workarounds_pchlan(hw);
	if (ret_val)
		return ret_val;

	if (phy->id == e1000_phy_unknown)
		switch (hw->mac.type) {
		default:
			ret_val = e1000_get_phy_id(hw);
			if (ret_val)
				return ret_val;
			if ((phy->id != 0) && (phy->id != PHY_REVISION_MASK))
				break;
			/* fall-through */
		case e1000_pch2lan:
		case e1000_pch_lpt:
		case e1000_pch_spt:
		case e1000_pch_cnp:
			/* In case the PHY needs to be in mdio slow mode,
			 * set slow mode and try to get the PHY id again.
			 */
			ret_val = e1000_set_mdio_slow_mode_hv(hw);
			if (ret_val)
				return ret_val;
			ret_val = e1000_get_phy_id(hw);
			if (ret_val)
				return ret_val;
			break;
		}
	phy->type = e1000_get_phy_type_from_id(phy->id);

	switch (phy->type) {
	case e1000_phy_82577:
	case e1000_phy_82579:
	case e1000_phy_i217:
		phy->ops.check_polarity = e1000_check_polarity_82577;
		phy->ops.force_speed_duplex =
			e1000_phy_force_speed_duplex_82577;
		phy->ops.get_cable_length = e1000_get_cable_length_82577;
		phy->ops.get_info = e1000_get_phy_info_82577;
		phy->ops.commit = e1000_phy_sw_reset_generic;
		break;
	case e1000_phy_82578:
		phy->ops.check_polarity = e1000_check_polarity_m88;
		phy->ops.force_speed_duplex = e1000_phy_force_speed_duplex_m88;
		phy->ops.get_cable_length = e1000_get_cable_length_m88;
		phy->ops.get_info = e1000_get_phy_info_m88;
		break;
	default:
		ret_val = -E1000_ERR_PHY;
		break;
	}

	return ret_val;
}

/**
 *  e1000_init_phy_params_ich8lan - Initialize PHY function pointers
 *  @hw: pointer to the HW structure
 *
 *  Initialize family-specific PHY parameters and function pointers.
 **/
static s32 e1000_init_phy_params_ich8lan(struct e1000_hw *hw)
{
	struct e1000_phy_info *phy = &hw->phy;
	s32 ret_val;
	u16 i = 0;

	DEBUGFUNC("e1000_init_phy_params_ich8lan");

	phy->addr		= 1;
	phy->reset_delay_us	= 100;

	phy->ops.acquire	= e1000_acquire_swflag_ich8lan;
	phy->ops.check_reset_block = e1000_check_reset_block_ich8lan;
	phy->ops.get_cable_length = e1000_get_cable_length_igp_2;
	phy->ops.get_cfg_done	= e1000_get_cfg_done_ich8lan;
	phy->ops.read_reg	= e1000_read_phy_reg_igp;
	phy->ops.release	= e1000_release_swflag_ich8lan;
	phy->ops.reset		= e1000_phy_hw_reset_ich8lan;
	phy->ops.set_d0_lplu_state = e1000_set_d0_lplu_state_ich8lan;
	phy->ops.set_d3_lplu_state = e1000_set_d3_lplu_state_ich8lan;
	phy->ops.write_reg	= e1000_write_phy_reg_igp;
	phy->ops.power_up	= e1000_power_up_phy_copper;
	phy->ops.power_down	= e1000_power_down_phy_copper_ich8lan;

	/* We may need to do this twice - once for IGP and if that fails,
	 * we'll set BM func pointers and try again
	 */
	ret_val = e1000_determine_phy_address(hw);
	if (ret_val) {
		phy->ops.write_reg = e1000_write_phy_reg_bm;
		phy->ops.read_reg  = e1000_read_phy_reg_bm;
		ret_val = e1000_determine_phy_address(hw);
		if (ret_val) {
			DEBUGOUT("Cannot determine PHY addr. Erroring out\n");
			return ret_val;
		}
	}

	phy->id = 0;
	while ((e1000_phy_unknown == e1000_get_phy_type_from_id(phy->id)) &&
	       (i++ < 100)) {
		msec_delay(1);
		ret_val = e1000_get_phy_id(hw);
		if (ret_val)
			return ret_val;
	}

	/* Verify phy id */
	switch (phy->id) {
	case IGP03E1000_E_PHY_ID:
		phy->type = e1000_phy_igp_3;
		phy->autoneg_mask = AUTONEG_ADVERTISE_SPEED_DEFAULT;
		phy->ops.read_reg_locked = e1000_read_phy_reg_igp_locked;
		phy->ops.write_reg_locked = e1000_write_phy_reg_igp_locked;
		phy->ops.get_info = e1000_get_phy_info_igp;
		phy->ops.check_polarity = e1000_check_polarity_igp;
		phy->ops.force_speed_duplex = e1000_phy_force_speed_duplex_igp;
		break;
	case IFE_E_PHY_ID:
	case IFE_PLUS_E_PHY_ID:
	case IFE_C_E_PHY_ID:
		phy->type = e1000_phy_ife;
		phy->autoneg_mask = E1000_ALL_NOT_GIG;
		phy->ops.get_info = e1000_get_phy_info_ife;
		phy->ops.check_polarity = e1000_check_polarity_ife;
		phy->ops.force_speed_duplex = e1000_phy_force_speed_duplex_ife;
		break;
	case BME1000_E_PHY_ID:
		phy->type = e1000_phy_bm;
		phy->autoneg_mask = AUTONEG_ADVERTISE_SPEED_DEFAULT;
		phy->ops.read_reg = e1000_read_phy_reg_bm;
		phy->ops.write_reg = e1000_write_phy_reg_bm;
		phy->ops.commit = e1000_phy_sw_reset_generic;
		phy->ops.get_info = e1000_get_phy_info_m88;
		phy->ops.check_polarity = e1000_check_polarity_m88;
		phy->ops.force_speed_duplex = e1000_phy_force_speed_duplex_m88;
		break;
	default:
		return -E1000_ERR_PHY;
		break;
	}

	return E1000_SUCCESS;
}

/**
 *  e1000_init_nvm_params_ich8lan - Initialize NVM function pointers
 *  @hw: pointer to the HW structure
 *
 *  Initialize family-specific NVM parameters and function
 *  pointers.
 **/
static s32 e1000_init_nvm_params_ich8lan(struct e1000_hw *hw)
{
	struct e1000_nvm_info *nvm = &hw->nvm;
	struct e1000_dev_spec_ich8lan *dev_spec = &hw->dev_spec.ich8lan;
	u32 gfpreg, sector_base_addr, sector_end_addr;
	u16 i;
	u32 nvm_size;

	DEBUGFUNC("e1000_init_nvm_params_ich8lan");

	nvm->type = e1000_nvm_flash_sw;

	if (hw->mac.type >= e1000_pch_spt) {
		/* in SPT, gfpreg doesn't exist. NVM size is taken from the
		 * STRAP register. This is because in SPT the GbE Flash region
		 * is no longer accessed through the flash registers. Instead,
		 * the mechanism has changed, and the Flash region access
		 * registers are now implemented in GbE memory space.
		 */
		nvm->flash_base_addr = 0;
		nvm_size =
		    (((E1000_READ_REG(hw, E1000_STRAP) >> 1) & 0x1F) + 1)
		    * NVM_SIZE_MULTIPLIER;
		nvm->flash_bank_size = nvm_size / 2;
		/* Adjust to word count */
		nvm->flash_bank_size /= sizeof(u16);
		/* Set the base address for flash register access */
		hw->flash_address = hw->hw_addr + E1000_FLASH_BASE_ADDR;
	} else {
		/* Can't read flash registers if register set isn't mapped. */
		if (!hw->flash_address) {
			DEBUGOUT("ERROR: Flash registers not mapped\n");
			return -E1000_ERR_CONFIG;
		}

		gfpreg = E1000_READ_FLASH_REG(hw, ICH_FLASH_GFPREG);

		/* sector_X_addr is a "sector"-aligned address (4096 bytes)
		 * Add 1 to sector_end_addr since this sector is included in
		 * the overall size.
		 */
		sector_base_addr = gfpreg & FLASH_GFPREG_BASE_MASK;
		sector_end_addr = ((gfpreg >> 16) & FLASH_GFPREG_BASE_MASK) + 1;

		/* flash_base_addr is byte-aligned */
		nvm->flash_base_addr = sector_base_addr
				       << FLASH_SECTOR_ADDR_SHIFT;

		/* find total size of the NVM, then cut in half since the total
		 * size represents two separate NVM banks.
		 */
		nvm->flash_bank_size = ((sector_end_addr - sector_base_addr)
					<< FLASH_SECTOR_ADDR_SHIFT);
		nvm->flash_bank_size /= 2;
		/* Adjust to word count */
		nvm->flash_bank_size /= sizeof(u16);
	}

	nvm->word_size = E1000_SHADOW_RAM_WORDS;

	/* Clear shadow ram */
	for (i = 0; i < nvm->word_size; i++) {
		dev_spec->shadow_ram[i].modified = FALSE;
		dev_spec->shadow_ram[i].value    = 0xFFFF;
	}

	/* Function Pointers */
	nvm->ops.acquire	= e1000_acquire_nvm_ich8lan;
	nvm->ops.release	= e1000_release_nvm_ich8lan;
	if (hw->mac.type >= e1000_pch_spt) {
		nvm->ops.read	= e1000_read_nvm_spt;
		nvm->ops.update	= e1000_update_nvm_checksum_spt;
	} else {
		nvm->ops.read	= e1000_read_nvm_ich8lan;
		nvm->ops.update	= e1000_update_nvm_checksum_ich8lan;
	}
	nvm->ops.valid_led_default = e1000_valid_led_default_ich8lan;
	nvm->ops.validate	= e1000_validate_nvm_checksum_ich8lan;
	nvm->ops.write		= e1000_write_nvm_ich8lan;

	return E1000_SUCCESS;
}

/**
 *  e1000_init_mac_params_ich8lan - Initialize MAC function pointers
 *  @hw: pointer to the HW structure
 *
 *  Initialize family-specific MAC parameters and function
 *  pointers.
 **/
static s32 e1000_init_mac_params_ich8lan(struct e1000_hw *hw)
{
	struct e1000_mac_info *mac = &hw->mac;

	DEBUGFUNC("e1000_init_mac_params_ich8lan");

	/* Set media type function pointer */
	hw->phy.media_type = e1000_media_type_copper;

	/* Set mta register count */
	mac->mta_reg_count = 32;
	/* Set rar entry count */
	mac->rar_entry_count = E1000_ICH_RAR_ENTRIES;
	if (mac->type == e1000_ich8lan)
		mac->rar_entry_count--;
	/* Set if part includes ASF firmware */
	mac->asf_firmware_present = TRUE;
	/* FWSM register */
	mac->has_fwsm = TRUE;
	/* ARC subsystem not supported */
	mac->arc_subsystem_valid = FALSE;
	/* Adaptive IFS supported */
	mac->adaptive_ifs = TRUE;

	/* Function pointers */

	/* bus type/speed/width */
	mac->ops.get_bus_info = e1000_get_bus_info_ich8lan;
	/* function id */
	mac->ops.set_lan_id = e1000_set_lan_id_single_port;
	/* reset */
	mac->ops.reset_hw = e1000_reset_hw_ich8lan;
	/* hw initialization */
	mac->ops.init_hw = e1000_init_hw_ich8lan;
	/* link setup */
	mac->ops.setup_link = e1000_setup_link_ich8lan;
	/* physical interface setup */
	mac->ops.setup_physical_interface = e1000_setup_copper_link_ich8lan;
	/* check for link */
	mac->ops.check_for_link = e1000_check_for_copper_link_ich8lan;
	/* link info */
	mac->ops.get_link_up_info = e1000_get_link_up_info_ich8lan;
	/* multicast address update */
	mac->ops.update_mc_addr_list = e1000_update_mc_addr_list_generic;
	/* clear hardware counters */
	mac->ops.clear_hw_cntrs = e1000_clear_hw_cntrs_ich8lan;

	/* LED and other operations */
	switch (mac->type) {
	case e1000_ich8lan:
	case e1000_ich9lan:
	case e1000_ich10lan:
		/* check management mode */
		mac->ops.check_mng_mode = e1000_check_mng_mode_ich8lan;
		/* ID LED init */
		mac->ops.id_led_init = e1000_id_led_init_generic;
		/* blink LED */
		mac->ops.blink_led = e1000_blink_led_generic;
		/* setup LED */
		mac->ops.setup_led = e1000_setup_led_generic;
		/* cleanup LED */
		mac->ops.cleanup_led = e1000_cleanup_led_ich8lan;
		/* turn on/off LED */
		mac->ops.led_on = e1000_led_on_ich8lan;
		mac->ops.led_off = e1000_led_off_ich8lan;
		break;
	case e1000_pch2lan:
		mac->rar_entry_count = E1000_PCH2_RAR_ENTRIES;
		mac->ops.rar_set = e1000_rar_set_pch2lan;
		/* fall-through */
	case e1000_pch_lpt:
	case e1000_pch_spt:
	case e1000_pch_cnp:
		/* multicast address update for pch2 */
		mac->ops.update_mc_addr_list =
			e1000_update_mc_addr_list_pch2lan;
		/* fall-through */
	case e1000_pchlan:
		/* check management mode */
		mac->ops.check_mng_mode = e1000_check_mng_mode_pchlan;
		/* ID LED init */
		mac->ops.id_led_init = e1000_id_led_init_pchlan;
		/* setup LED */
		mac->ops.setup_led = e1000_setup_led_pchlan;
		/* cleanup LED */
		mac->ops.cleanup_led = e1000_cleanup_led_pchlan;
		/* turn on/off LED */
		mac->ops.led_on = e1000_led_on_pchlan;
		mac->ops.led_off = e1000_led_off_pchlan;
		break;
	default:
		break;
	}

	if (mac->type >= e1000_pch_lpt) {
		mac->rar_entry_count = E1000_PCH_LPT_RAR_ENTRIES;
		mac->ops.rar_set = e1000_rar_set_pch_lpt;
		mac->ops.setup_physical_interface = e1000_setup_copper_link_pch_lpt;
		mac->ops.set_obff_timer = e1000_set_obff_timer_pch_lpt;
	}

	/* Enable PCS Lock-loss workaround for ICH8 */
	if (mac->type == e1000_ich8lan)
		e1000_set_kmrn_lock_loss_workaround_ich8lan(hw, TRUE);

	return E1000_SUCCESS;
}

/**
 *  __e1000_access_emi_reg_locked - Read/write EMI register
 *  @hw: pointer to the HW structure
 *  @addr: EMI address to program
 *  @data: pointer to value to read/write from/to the EMI address
 *  @read: boolean flag to indicate read or write
 *
 *  This helper function assumes the SW/FW/HW Semaphore is already acquired.
 **/
static s32 __e1000_access_emi_reg_locked(struct e1000_hw *hw, u16 address,
					 u16 *data, bool read)
{
	s32 ret_val;

	DEBUGFUNC("__e1000_access_emi_reg_locked");

	ret_val = hw->phy.ops.write_reg_locked(hw, I82579_EMI_ADDR, address);
	if (ret_val)
		return ret_val;

	if (read)
		ret_val = hw->phy.ops.read_reg_locked(hw, I82579_EMI_DATA,
						      data);
	else
		ret_val = hw->phy.ops.write_reg_locked(hw, I82579_EMI_DATA,
						       *data);

	return ret_val;
}

/**
 *  e1000_read_emi_reg_locked - Read Extended Management Interface register
 *  @hw: pointer to the HW structure
 *  @addr: EMI address to program
 *  @data: value to be read from the EMI address
 *
 *  Assumes the SW/FW/HW Semaphore is already acquired.
 **/
s32 e1000_read_emi_reg_locked(struct e1000_hw *hw, u16 addr, u16 *data)
{
	DEBUGFUNC("e1000_read_emi_reg_locked");

	return __e1000_access_emi_reg_locked(hw, addr, data, TRUE);
}

/**
 *  e1000_write_emi_reg_locked - Write Extended Management Interface register
 *  @hw: pointer to the HW structure
 *  @addr: EMI address to program
 *  @data: value to be written to the EMI address
 *
 *  Assumes the SW/FW/HW Semaphore is already acquired.
 **/
s32 e1000_write_emi_reg_locked(struct e1000_hw *hw, u16 addr, u16 data)
{
	DEBUGFUNC("e1000_read_emi_reg_locked");

	return __e1000_access_emi_reg_locked(hw, addr, &data, FALSE);
}

/**
 *  e1000_set_eee_pchlan - Enable/disable EEE support
 *  @hw: pointer to the HW structure
 *
 *  Enable/disable EEE based on setting in dev_spec structure, the duplex of
 *  the link and the EEE capabilities of the link partner.  The LPI Control
 *  register bits will remain set only if/when link is up.
 *
 *  EEE LPI must not be asserted earlier than one second after link is up.
 *  On 82579, EEE LPI should not be enabled until such time otherwise there
 *  can be link issues with some switches.  Other devices can have EEE LPI
 *  enabled immediately upon link up since they have a timer in hardware which
 *  prevents LPI from being asserted too early.
 **/
s32 e1000_set_eee_pchlan(struct e1000_hw *hw)
{
	struct e1000_dev_spec_ich8lan *dev_spec = &hw->dev_spec.ich8lan;
	s32 ret_val;
	u16 lpa, pcs_status, adv, adv_addr, lpi_ctrl, data;

	DEBUGFUNC("e1000_set_eee_pchlan");

	switch (hw->phy.type) {
	case e1000_phy_82579:
		lpa = I82579_EEE_LP_ABILITY;
		pcs_status = I82579_EEE_PCS_STATUS;
		adv_addr = I82579_EEE_ADVERTISEMENT;
		break;
	case e1000_phy_i217:
		lpa = I217_EEE_LP_ABILITY;
		pcs_status = I217_EEE_PCS_STATUS;
		adv_addr = I217_EEE_ADVERTISEMENT;
		break;
	default:
		return E1000_SUCCESS;
	}

	ret_val = hw->phy.ops.acquire(hw);
	if (ret_val)
		return ret_val;

	ret_val = hw->phy.ops.read_reg_locked(hw, I82579_LPI_CTRL, &lpi_ctrl);
	if (ret_val)
		goto release;

	/* Clear bits that enable EEE in various speeds */
	lpi_ctrl &= ~I82579_LPI_CTRL_ENABLE_MASK;

	/* Enable EEE if not disabled by user */
	if (!dev_spec->eee_disable) {
		/* Save off link partner's EEE ability */
		ret_val = e1000_read_emi_reg_locked(hw, lpa,
						    &dev_spec->eee_lp_ability);
		if (ret_val)
			goto release;

		/* Read EEE advertisement */
		ret_val = e1000_read_emi_reg_locked(hw, adv_addr, &adv);
		if (ret_val)
			goto release;

		/* Enable EEE only for speeds in which the link partner is
		 * EEE capable and for which we advertise EEE.
		 */
		if (adv & dev_spec->eee_lp_ability & I82579_EEE_1000_SUPPORTED)
			lpi_ctrl |= I82579_LPI_CTRL_1000_ENABLE;

		if (adv & dev_spec->eee_lp_ability & I82579_EEE_100_SUPPORTED) {
			hw->phy.ops.read_reg_locked(hw, PHY_LP_ABILITY, &data);
			if (data & NWAY_LPAR_100TX_FD_CAPS)
				lpi_ctrl |= I82579_LPI_CTRL_100_ENABLE;
			else
				/* EEE is not supported in 100Half, so ignore
				 * partner's EEE in 100 ability if full-duplex
				 * is not advertised.
				 */
				dev_spec->eee_lp_ability &=
				    ~I82579_EEE_100_SUPPORTED;
		}
	}

	if (hw->phy.type == e1000_phy_82579) {
		ret_val = e1000_read_emi_reg_locked(hw, I82579_LPI_PLL_SHUT,
						    &data);
		if (ret_val)
			goto release;

		data &= ~I82579_LPI_100_PLL_SHUT;
		ret_val = e1000_write_emi_reg_locked(hw, I82579_LPI_PLL_SHUT,
						     data);
	}

	/* R/Clr IEEE MMD 3.1 bits 11:10 - Tx/Rx LPI Received */
	ret_val = e1000_read_emi_reg_locked(hw, pcs_status, &data);
	if (ret_val)
		goto release;

	ret_val = hw->phy.ops.write_reg_locked(hw, I82579_LPI_CTRL, lpi_ctrl);
release:
	hw->phy.ops.release(hw);

	return ret_val;
}

/**
 *  e1000_k1_workaround_lpt_lp - K1 workaround on Lynxpoint-LP
 *  @hw:   pointer to the HW structure
 *  @link: link up bool flag
 *
 *  When K1 is enabled for 1Gbps, the MAC can miss 2 DMA completion indications
 *  preventing further DMA write requests.  Workaround the issue by disabling
 *  the de-assertion of the clock request when in 1Gpbs mode.
 *  Also, set appropriate Tx re-transmission timeouts for 10 and 100Half link
 *  speeds in order to avoid Tx hangs.
 **/
static s32 e1000_k1_workaround_lpt_lp(struct e1000_hw *hw, bool link)
{
	u32 fextnvm6 = E1000_READ_REG(hw, E1000_FEXTNVM6);
	u32 status = E1000_READ_REG(hw, E1000_STATUS);
	s32 ret_val = E1000_SUCCESS;
	u16 reg;

	if (link && (status & E1000_STATUS_SPEED_1000)) {
		ret_val = hw->phy.ops.acquire(hw);
		if (ret_val)
			return ret_val;

		ret_val =
		    e1000_read_kmrn_reg_locked(hw, E1000_KMRNCTRLSTA_K1_CONFIG,
					       &reg);
		if (ret_val)
			goto release;

		ret_val =
		    e1000_write_kmrn_reg_locked(hw,
						E1000_KMRNCTRLSTA_K1_CONFIG,
						reg &
						~E1000_KMRNCTRLSTA_K1_ENABLE);
		if (ret_val)
			goto release;

		usec_delay(10);

		E1000_WRITE_REG(hw, E1000_FEXTNVM6,
				fextnvm6 | E1000_FEXTNVM6_REQ_PLL_CLK);

		ret_val =
		    e1000_write_kmrn_reg_locked(hw,
						E1000_KMRNCTRLSTA_K1_CONFIG,
						reg);
release:
		hw->phy.ops.release(hw);
	} else {
		/* clear FEXTNVM6 bit 8 on link down or 10/100 */
		fextnvm6 &= ~E1000_FEXTNVM6_REQ_PLL_CLK;

		if ((hw->phy.revision > 5) || !link ||
		    ((status & E1000_STATUS_SPEED_100) &&
		     (status & E1000_STATUS_FD)))
			goto update_fextnvm6;

		ret_val = hw->phy.ops.read_reg(hw, I217_INBAND_CTRL, &reg);
		if (ret_val)
			return ret_val;

		/* Clear link status transmit timeout */
		reg &= ~I217_INBAND_CTRL_LINK_STAT_TX_TIMEOUT_MASK;

		if (status & E1000_STATUS_SPEED_100) {
			/* Set inband Tx timeout to 5x10us for 100Half */
			reg |= 5 << I217_INBAND_CTRL_LINK_STAT_TX_TIMEOUT_SHIFT;

			/* Do not extend the K1 entry latency for 100Half */
			fextnvm6 &= ~E1000_FEXTNVM6_ENABLE_K1_ENTRY_CONDITION;
		} else {
			/* Set inband Tx timeout to 50x10us for 10Full/Half */
			reg |= 50 <<
			       I217_INBAND_CTRL_LINK_STAT_TX_TIMEOUT_SHIFT;

			/* Extend the K1 entry latency for 10 Mbps */
			fextnvm6 |= E1000_FEXTNVM6_ENABLE_K1_ENTRY_CONDITION;
		}

		ret_val = hw->phy.ops.write_reg(hw, I217_INBAND_CTRL, reg);
		if (ret_val)
			return ret_val;

update_fextnvm6:
		E1000_WRITE_REG(hw, E1000_FEXTNVM6, fextnvm6);
	}

	return ret_val;
}

static u64 e1000_ltr2ns(u16 ltr)
{
	u32 value, scale;

	/* Determine the latency in nsec based on the LTR value & scale */
	value = ltr & E1000_LTRV_VALUE_MASK;
	scale = (ltr & E1000_LTRV_SCALE_MASK) >> E1000_LTRV_SCALE_SHIFT;

	return value * (1 << (scale * E1000_LTRV_SCALE_FACTOR));
}

/**
 *  e1000_platform_pm_pch_lpt - Set platform power management values
 *  @hw: pointer to the HW structure
 *  @link: bool indicating link status
 *
 *  Set the Latency Tolerance Reporting (LTR) values for the "PCIe-like"
 *  GbE MAC in the Lynx Point PCH based on Rx buffer size and link speed
 *  when link is up (which must not exceed the maximum latency supported
 *  by the platform), otherwise specify there is no LTR requirement.
 *  Unlike TRUE-PCIe devices which set the LTR maximum snoop/no-snoop
 *  latencies in the LTR Extended Capability Structure in the PCIe Extended
 *  Capability register set, on this device LTR is set by writing the
 *  equivalent snoop/no-snoop latencies in the LTRV register in the MAC and
 *  set the SEND bit to send an Intel On-chip System Fabric sideband (IOSF-SB)
 *  message to the PMC.
 *
 *  Use the LTR value to calculate the Optimized Buffer Flush/Fill (OBFF)
 *  high-water mark.
 **/
static s32 e1000_platform_pm_pch_lpt(struct e1000_hw *hw, bool link)
{
	u32 reg = link << (E1000_LTRV_REQ_SHIFT + E1000_LTRV_NOSNOOP_SHIFT) |
		  link << E1000_LTRV_REQ_SHIFT | E1000_LTRV_SEND;
	u16 lat_enc = 0;	/* latency encoded */
	s32 obff_hwm = 0;

	DEBUGFUNC("e1000_platform_pm_pch_lpt");

	if (link) {
		u16 speed, duplex, scale = 0;
		u16 max_snoop, max_nosnoop;
		u16 max_ltr_enc;	/* max LTR latency encoded */
		s64 lat_ns;
		s64 value;
		u32 rxa;

		if (!hw->mac.max_frame_size) {
			DEBUGOUT("max_frame_size not set.\n");
			return -E1000_ERR_CONFIG;
		}

		hw->mac.ops.get_link_up_info(hw, &speed, &duplex);
		if (!speed) {
			DEBUGOUT("Speed not set.\n");
			return -E1000_ERR_CONFIG;
		}

		/* Rx Packet Buffer Allocation size (KB) */
		rxa = E1000_READ_REG(hw, E1000_PBA) & E1000_PBA_RXA_MASK;

		/* Determine the maximum latency tolerated by the device.
		 *
		 * Per the PCIe spec, the tolerated latencies are encoded as
		 * a 3-bit encoded scale (only 0-5 are valid) multiplied by
		 * a 10-bit value (0-1023) to provide a range from 1 ns to
		 * 2^25*(2^10-1) ns.  The scale is encoded as 0=2^0ns,
		 * 1=2^5ns, 2=2^10ns,...5=2^25ns.
		 */
		lat_ns = ((s64)rxa * 1024 -
			  (2 * (s64)hw->mac.max_frame_size)) * 8 * 1000;
		if (lat_ns < 0)
			lat_ns = 0;
		else
			lat_ns /= speed;
		value = lat_ns;

		while (value > E1000_LTRV_VALUE_MASK) {
			scale++;
			value = E1000_DIVIDE_ROUND_UP(value, (1 << 5));
		}
		if (scale > E1000_LTRV_SCALE_MAX) {
			DEBUGOUT1("Invalid LTR latency scale %d\n", scale);
			return -E1000_ERR_CONFIG;
		}
		lat_enc = (u16)((scale << E1000_LTRV_SCALE_SHIFT) | value);

		/* Determine the maximum latency tolerated by the platform */
		e1000_read_pci_cfg(hw, E1000_PCI_LTR_CAP_LPT, &max_snoop);
		e1000_read_pci_cfg(hw, E1000_PCI_LTR_CAP_LPT + 2, &max_nosnoop);
		max_ltr_enc = E1000_MAX(max_snoop, max_nosnoop);

		if (lat_enc > max_ltr_enc) {
			lat_enc = max_ltr_enc;
			lat_ns = e1000_ltr2ns(max_ltr_enc);
		}

		if (lat_ns) {
			lat_ns *= speed * 1000;
			lat_ns /= 8;
			lat_ns /= 1000000000;
			obff_hwm = (s32)(rxa - lat_ns);
		}
		if ((obff_hwm < 0) || (obff_hwm > E1000_SVT_OFF_HWM_MASK)) {
			DEBUGOUT1("Invalid high water mark %d\n", obff_hwm);
			return -E1000_ERR_CONFIG;
		}
	}

	/* Set Snoop and No-Snoop latencies the same */
	reg |= lat_enc | (lat_enc << E1000_LTRV_NOSNOOP_SHIFT);
	E1000_WRITE_REG(hw, E1000_LTRV, reg);

	/* Set OBFF high water mark */
	reg = E1000_READ_REG(hw, E1000_SVT) & ~E1000_SVT_OFF_HWM_MASK;
	reg |= obff_hwm;
	E1000_WRITE_REG(hw, E1000_SVT, reg);

	/* Enable OBFF */
	reg = E1000_READ_REG(hw, E1000_SVCR);
	reg |= E1000_SVCR_OFF_EN;
	/* Always unblock interrupts to the CPU even when the system is
	 * in OBFF mode. This ensures that small round-robin traffic
	 * (like ping) does not get dropped or experience long latency.
	 */
	reg |= E1000_SVCR_OFF_MASKINT;
	E1000_WRITE_REG(hw, E1000_SVCR, reg);

	return E1000_SUCCESS;
}

/**
 *  e1000_set_obff_timer_pch_lpt - Update Optimized Buffer Flush/Fill timer
 *  @hw: pointer to the HW structure
 *  @itr: interrupt throttling rate
 *
 *  Configure OBFF with the updated interrupt rate.
 **/
static s32 e1000_set_obff_timer_pch_lpt(struct e1000_hw *hw, u32 itr)
{
	u32 svcr;
	s32 timer;

	DEBUGFUNC("e1000_set_obff_timer_pch_lpt");

	/* Convert ITR value into microseconds for OBFF timer */
	timer = itr & E1000_ITR_MASK;
	timer = (timer * E1000_ITR_MULT) / 1000;

	if ((timer < 0) || (timer > E1000_ITR_MASK)) {
		DEBUGOUT1("Invalid OBFF timer %d\n", timer);
		return -E1000_ERR_CONFIG;
	}

	svcr = E1000_READ_REG(hw, E1000_SVCR);
	svcr &= ~E1000_SVCR_OFF_TIMER_MASK;
	svcr |= timer << E1000_SVCR_OFF_TIMER_SHIFT;
	E1000_WRITE_REG(hw, E1000_SVCR, svcr);

	return E1000_SUCCESS;
}

/**
 *  e1000_enable_ulp_lpt_lp - configure Ultra Low Power mode for LynxPoint-LP
 *  @hw: pointer to the HW structure
 *  @to_sx: boolean indicating a system power state transition to Sx
 *
 *  When link is down, configure ULP mode to significantly reduce the power
 *  to the PHY.  If on a Manageability Engine (ME) enabled system, tell the
 *  ME firmware to start the ULP configuration.  If not on an ME enabled
 *  system, configure the ULP mode by software.
 */
s32 e1000_enable_ulp_lpt_lp(struct e1000_hw *hw, bool to_sx)
{
	u32 mac_reg;
	s32 ret_val = E1000_SUCCESS;
	u16 phy_reg;
	u16 oem_reg = 0;

	if ((hw->mac.type < e1000_pch_lpt) ||
	    (hw->device_id == E1000_DEV_ID_PCH_LPT_I217_LM) ||
	    (hw->device_id == E1000_DEV_ID_PCH_LPT_I217_V) ||
	    (hw->device_id == E1000_DEV_ID_PCH_I218_LM2) ||
	    (hw->device_id == E1000_DEV_ID_PCH_I218_V2) ||
	    (hw->dev_spec.ich8lan.ulp_state == e1000_ulp_state_on))
		return 0;

	if (E1000_READ_REG(hw, E1000_FWSM) & E1000_ICH_FWSM_FW_VALID) {
		/* Request ME configure ULP mode in the PHY */
		mac_reg = E1000_READ_REG(hw, E1000_H2ME);
		mac_reg |= E1000_H2ME_ULP | E1000_H2ME_ENFORCE_SETTINGS;
		E1000_WRITE_REG(hw, E1000_H2ME, mac_reg);

		goto out;
	}

	if (!to_sx) {
		int i = 0;

		/* Poll up to 5 seconds for Cable Disconnected indication */
		while (!(E1000_READ_REG(hw, E1000_FEXT) &
			 E1000_FEXT_PHY_CABLE_DISCONNECTED)) {
			/* Bail if link is re-acquired */
			if (E1000_READ_REG(hw, E1000_STATUS) & E1000_STATUS_LU)
				return -E1000_ERR_PHY;

			if (i++ == 100)
				break;

			msec_delay(50);
		}
		DEBUGOUT2("CABLE_DISCONNECTED %s set after %dmsec\n",
			 (E1000_READ_REG(hw, E1000_FEXT) &
			  E1000_FEXT_PHY_CABLE_DISCONNECTED) ? "" : "not",
			 i * 50);
	}

	ret_val = hw->phy.ops.acquire(hw);
	if (ret_val)
		goto out;

	/* Force SMBus mode in PHY */
	ret_val = e1000_read_phy_reg_hv_locked(hw, CV_SMB_CTRL, &phy_reg);
	if (ret_val)
		goto release;
	phy_reg |= CV_SMB_CTRL_FORCE_SMBUS;
	e1000_write_phy_reg_hv_locked(hw, CV_SMB_CTRL, phy_reg);

	/* Force SMBus mode in MAC */
	mac_reg = E1000_READ_REG(hw, E1000_CTRL_EXT);
	mac_reg |= E1000_CTRL_EXT_FORCE_SMBUS;
	E1000_WRITE_REG(hw, E1000_CTRL_EXT, mac_reg);

	/* Si workaround for ULP entry flow on i127/rev6 h/w.  Enable
	 * LPLU and disable Gig speed when entering ULP
	 */
	if ((hw->phy.type == e1000_phy_i217) && (hw->phy.revision == 6)) {
		ret_val = e1000_read_phy_reg_hv_locked(hw, HV_OEM_BITS,
						       &oem_reg);
		if (ret_val)
			goto release;

		phy_reg = oem_reg;
		phy_reg |= HV_OEM_BITS_LPLU | HV_OEM_BITS_GBE_DIS;

		ret_val = e1000_write_phy_reg_hv_locked(hw, HV_OEM_BITS,
							phy_reg);

		if (ret_val)
			goto release;
	}

	/* Set Inband ULP Exit, Reset to SMBus mode and
	 * Disable SMBus Release on PERST# in PHY
	 */
	ret_val = e1000_read_phy_reg_hv_locked(hw, I218_ULP_CONFIG1, &phy_reg);
	if (ret_val)
		goto release;
	phy_reg |= (I218_ULP_CONFIG1_RESET_TO_SMBUS |
		    I218_ULP_CONFIG1_DISABLE_SMB_PERST);
	if (to_sx) {
		if (E1000_READ_REG(hw, E1000_WUFC) & E1000_WUFC_LNKC)
			phy_reg |= I218_ULP_CONFIG1_WOL_HOST;
		else
			phy_reg &= ~I218_ULP_CONFIG1_WOL_HOST;

		phy_reg |= I218_ULP_CONFIG1_STICKY_ULP;
		phy_reg &= ~I218_ULP_CONFIG1_INBAND_EXIT;
	} else {
		phy_reg |= I218_ULP_CONFIG1_INBAND_EXIT;
		phy_reg &= ~I218_ULP_CONFIG1_STICKY_ULP;
		phy_reg &= ~I218_ULP_CONFIG1_WOL_HOST;
	}
	e1000_write_phy_reg_hv_locked(hw, I218_ULP_CONFIG1, phy_reg);

	/* Set Disable SMBus Release on PERST# in MAC */
	mac_reg = E1000_READ_REG(hw, E1000_FEXTNVM7);
	mac_reg |= E1000_FEXTNVM7_DISABLE_SMB_PERST;
	E1000_WRITE_REG(hw, E1000_FEXTNVM7, mac_reg);

	/* Commit ULP changes in PHY by starting auto ULP configuration */
	phy_reg |= I218_ULP_CONFIG1_START;
	e1000_write_phy_reg_hv_locked(hw, I218_ULP_CONFIG1, phy_reg);

	if ((hw->phy.type == e1000_phy_i217) && (hw->phy.revision == 6) &&
	    to_sx && (E1000_READ_REG(hw, E1000_STATUS) & E1000_STATUS_LU)) {
		ret_val = e1000_write_phy_reg_hv_locked(hw, HV_OEM_BITS,
							oem_reg);
		if (ret_val)
			goto release;
	}

release:
	hw->phy.ops.release(hw);
out:
	if (ret_val)
		DEBUGOUT1("Error in ULP enable flow: %d\n", ret_val);
	else
		hw->dev_spec.ich8lan.ulp_state = e1000_ulp_state_on;

	return ret_val;
}

/**
 *  e1000_disable_ulp_lpt_lp - unconfigure Ultra Low Power mode for LynxPoint-LP
 *  @hw: pointer to the HW structure
 *  @force: boolean indicating whether or not to force disabling ULP
 *
 *  Un-configure ULP mode when link is up, the system is transitioned from
 *  Sx or the driver is unloaded.  If on a Manageability Engine (ME) enabled
 *  system, poll for an indication from ME that ULP has been un-configured.
 *  If not on an ME enabled system, un-configure the ULP mode by software.
 *
 *  During nominal operation, this function is called when link is acquired
 *  to disable ULP mode (force=FALSE); otherwise, for example when unloading
 *  the driver or during Sx->S0 transitions, this is called with force=TRUE
 *  to forcibly disable ULP.
 */
s32 e1000_disable_ulp_lpt_lp(struct e1000_hw *hw, bool force)
{
	s32 ret_val = E1000_SUCCESS;
	u32 mac_reg;
	u16 phy_reg;
	int i = 0;

	if ((hw->mac.type < e1000_pch_lpt) ||
	    (hw->device_id == E1000_DEV_ID_PCH_LPT_I217_LM) ||
	    (hw->device_id == E1000_DEV_ID_PCH_LPT_I217_V) ||
	    (hw->device_id == E1000_DEV_ID_PCH_I218_LM2) ||
	    (hw->device_id == E1000_DEV_ID_PCH_I218_V2) ||
	    (hw->dev_spec.ich8lan.ulp_state == e1000_ulp_state_off))
		return 0;

	if (E1000_READ_REG(hw, E1000_FWSM) & E1000_ICH_FWSM_FW_VALID) {
		if (force) {
			/* Request ME un-configure ULP mode in the PHY */
			mac_reg = E1000_READ_REG(hw, E1000_H2ME);
			mac_reg &= ~E1000_H2ME_ULP;
			mac_reg |= E1000_H2ME_ENFORCE_SETTINGS;
			E1000_WRITE_REG(hw, E1000_H2ME, mac_reg);
		}

		/* Poll up to 300msec for ME to clear ULP_CFG_DONE. */
		while (E1000_READ_REG(hw, E1000_FWSM) &
		       E1000_FWSM_ULP_CFG_DONE) {
			if (i++ == 30) {
				ret_val = -E1000_ERR_PHY;
				goto out;
			}

			msec_delay(10);
		}
		DEBUGOUT1("ULP_CONFIG_DONE cleared after %dmsec\n", i * 10);

		if (force) {
			mac_reg = E1000_READ_REG(hw, E1000_H2ME);
			mac_reg &= ~E1000_H2ME_ENFORCE_SETTINGS;
			E1000_WRITE_REG(hw, E1000_H2ME, mac_reg);
		} else {
			/* Clear H2ME.ULP after ME ULP configuration */
			mac_reg = E1000_READ_REG(hw, E1000_H2ME);
			mac_reg &= ~E1000_H2ME_ULP;
			E1000_WRITE_REG(hw, E1000_H2ME, mac_reg);
		}

		goto out;
	}

	ret_val = hw->phy.ops.acquire(hw);
	if (ret_val)
		goto out;

	if (force)
		/* Toggle LANPHYPC Value bit */
		e1000_toggle_lanphypc_pch_lpt(hw);

	/* Unforce SMBus mode in PHY */
	ret_val = e1000_read_phy_reg_hv_locked(hw, CV_SMB_CTRL, &phy_reg);
	if (ret_val) {
		/* The MAC might be in PCIe mode, so temporarily force to
		 * SMBus mode in order to access the PHY.
		 */
		mac_reg = E1000_READ_REG(hw, E1000_CTRL_EXT);
		mac_reg |= E1000_CTRL_EXT_FORCE_SMBUS;
		E1000_WRITE_REG(hw, E1000_CTRL_EXT, mac_reg);

		msec_delay(50);

		ret_val = e1000_read_phy_reg_hv_locked(hw, CV_SMB_CTRL,
						       &phy_reg);
		if (ret_val)
			goto release;
	}
	phy_reg &= ~CV_SMB_CTRL_FORCE_SMBUS;
	e1000_write_phy_reg_hv_locked(hw, CV_SMB_CTRL, phy_reg);

	/* Unforce SMBus mode in MAC */
	mac_reg = E1000_READ_REG(hw, E1000_CTRL_EXT);
	mac_reg &= ~E1000_CTRL_EXT_FORCE_SMBUS;
	E1000_WRITE_REG(hw, E1000_CTRL_EXT, mac_reg);

	/* When ULP mode was previously entered, K1 was disabled by the
	 * hardware.  Re-Enable K1 in the PHY when exiting ULP.
	 */
	ret_val = e1000_read_phy_reg_hv_locked(hw, HV_PM_CTRL, &phy_reg);
	if (ret_val)
		goto release;
	phy_reg |= HV_PM_CTRL_K1_ENABLE;
	e1000_write_phy_reg_hv_locked(hw, HV_PM_CTRL, phy_reg);

	/* Clear ULP enabled configuration */
	ret_val = e1000_read_phy_reg_hv_locked(hw, I218_ULP_CONFIG1, &phy_reg);
	if (ret_val)
		goto release;
	phy_reg &= ~(I218_ULP_CONFIG1_IND |
		     I218_ULP_CONFIG1_STICKY_ULP |
		     I218_ULP_CONFIG1_RESET_TO_SMBUS |
		     I218_ULP_CONFIG1_WOL_HOST |
		     I218_ULP_CONFIG1_INBAND_EXIT |
		     I218_ULP_CONFIG1_EN_ULP_LANPHYPC |
		     I218_ULP_CONFIG1_DIS_CLR_STICKY_ON_PERST |
		     I218_ULP_CONFIG1_DISABLE_SMB_PERST);
	e1000_write_phy_reg_hv_locked(hw, I218_ULP_CONFIG1, phy_reg);

	/* Commit ULP changes by starting auto ULP configuration */
	phy_reg |= I218_ULP_CONFIG1_START;
	e1000_write_phy_reg_hv_locked(hw, I218_ULP_CONFIG1, phy_reg);

	/* Clear Disable SMBus Release on PERST# in MAC */
	mac_reg = E1000_READ_REG(hw, E1000_FEXTNVM7);
	mac_reg &= ~E1000_FEXTNVM7_DISABLE_SMB_PERST;
	E1000_WRITE_REG(hw, E1000_FEXTNVM7, mac_reg);

release:
	hw->phy.ops.release(hw);
	if (force) {
		hw->phy.ops.reset(hw);
		msec_delay(50);
	}
out:
	if (ret_val)
		DEBUGOUT1("Error in ULP disable flow: %d\n", ret_val);
	else
		hw->dev_spec.ich8lan.ulp_state = e1000_ulp_state_off;

	return ret_val;
}

/**
 *  e1000_check_for_copper_link_ich8lan - Check for link (Copper)
 *  @hw: pointer to the HW structure
 *
 *  Checks to see of the link status of the hardware has changed.  If a
 *  change in link status has been detected, then we read the PHY registers
 *  to get the current speed/duplex if link exists.
 **/
static s32 e1000_check_for_copper_link_ich8lan(struct e1000_hw *hw)
{
	struct e1000_mac_info *mac = &hw->mac;
	s32 ret_val, tipg_reg = 0;
	u16 emi_addr, emi_val = 0;
	bool link;
	u16 phy_reg;

	DEBUGFUNC("e1000_check_for_copper_link_ich8lan");

	/* We only want to go out to the PHY registers to see if Auto-Neg
	 * has completed and/or if our link status has changed.  The
	 * get_link_status flag is set upon receiving a Link Status
	 * Change or Rx Sequence Error interrupt.
	 */
	if (!mac->get_link_status)
		return E1000_SUCCESS;

	/* First we want to see if the MII Status Register reports
	 * link.  If so, then we want to get the current speed/duplex
	 * of the PHY.
	 */
	ret_val = e1000_phy_has_link_generic(hw, 1, 0, &link);
	if (ret_val)
		return ret_val;

	if (hw->mac.type == e1000_pchlan) {
		ret_val = e1000_k1_gig_workaround_hv(hw, link);
		if (ret_val)
			return ret_val;
	}

	/* When connected at 10Mbps half-duplex, some parts are excessively
	 * aggressive resulting in many collisions. To avoid this, increase
	 * the IPG and reduce Rx latency in the PHY.
	 */
	if ((hw->mac.type >= e1000_pch2lan) && link) {
		u16 speed, duplex;

		e1000_get_speed_and_duplex_copper_generic(hw, &speed, &duplex);
		tipg_reg = E1000_READ_REG(hw, E1000_TIPG);
		tipg_reg &= ~E1000_TIPG_IPGT_MASK;

		if (duplex == HALF_DUPLEX && speed == SPEED_10) {
			tipg_reg |= 0xFF;
			/* Reduce Rx latency in analog PHY */
			emi_val = 0;
		} else if (hw->mac.type >= e1000_pch_spt &&
			   duplex == FULL_DUPLEX && speed != SPEED_1000) {
			tipg_reg |= 0xC;
			emi_val = 1;
		} else {
			/* Roll back the default values */
			tipg_reg |= 0x08;
			emi_val = 1;
		}

		E1000_WRITE_REG(hw, E1000_TIPG, tipg_reg);

		ret_val = hw->phy.ops.acquire(hw);
		if (ret_val)
			return ret_val;

		if (hw->mac.type == e1000_pch2lan)
			emi_addr = I82579_RX_CONFIG;
		else
			emi_addr = I217_RX_CONFIG;
		ret_val = e1000_write_emi_reg_locked(hw, emi_addr, emi_val);


		if (hw->mac.type >= e1000_pch_lpt) {
			u16 phy_reg;

			hw->phy.ops.read_reg_locked(hw, I217_PLL_CLOCK_GATE_REG,
						    &phy_reg);
			phy_reg &= ~I217_PLL_CLOCK_GATE_MASK;
			if (speed == SPEED_100 || speed == SPEED_10)
				phy_reg |= 0x3E8;
			else
				phy_reg |= 0xFA;
			hw->phy.ops.write_reg_locked(hw,
						     I217_PLL_CLOCK_GATE_REG,
						     phy_reg);

			if (speed == SPEED_1000) {
				hw->phy.ops.read_reg_locked(hw, HV_PM_CTRL,
							    &phy_reg);

				phy_reg |= HV_PM_CTRL_K1_CLK_REQ;

				hw->phy.ops.write_reg_locked(hw, HV_PM_CTRL,
							     phy_reg);
				}
		 }
		hw->phy.ops.release(hw);

		if (ret_val)
			return ret_val;

		if (hw->mac.type >= e1000_pch_spt) {
			u16 data;
			u16 ptr_gap;

			if (speed == SPEED_1000) {
				ret_val = hw->phy.ops.acquire(hw);
				if (ret_val)
					return ret_val;

				ret_val = hw->phy.ops.read_reg_locked(hw,
							      PHY_REG(776, 20),
							      &data);
				if (ret_val) {
					hw->phy.ops.release(hw);
					return ret_val;
				}

				ptr_gap = (data & (0x3FF << 2)) >> 2;
				if (ptr_gap < 0x18) {
					data &= ~(0x3FF << 2);
					data |= (0x18 << 2);
					ret_val =
						hw->phy.ops.write_reg_locked(hw,
							PHY_REG(776, 20), data);
				}
				hw->phy.ops.release(hw);
				if (ret_val)
					return ret_val;
			} else {
				ret_val = hw->phy.ops.acquire(hw);
				if (ret_val)
					return ret_val;

				ret_val = hw->phy.ops.write_reg_locked(hw,
							     PHY_REG(776, 20),
							     0xC023);
				hw->phy.ops.release(hw);
				if (ret_val)
					return ret_val;

			}
		}
	}

	/* I217 Packet Loss issue:
	 * ensure that FEXTNVM4 Beacon Duration is set correctly
	 * on power up.
	 * Set the Beacon Duration for I217 to 8 usec
	 */
	if (hw->mac.type >= e1000_pch_lpt) {
		u32 mac_reg;

		mac_reg = E1000_READ_REG(hw, E1000_FEXTNVM4);
		mac_reg &= ~E1000_FEXTNVM4_BEACON_DURATION_MASK;
		mac_reg |= E1000_FEXTNVM4_BEACON_DURATION_8USEC;
		E1000_WRITE_REG(hw, E1000_FEXTNVM4, mac_reg);
	}

	/* Work-around I218 hang issue */
	if ((hw->device_id == E1000_DEV_ID_PCH_LPTLP_I218_LM) ||
	    (hw->device_id == E1000_DEV_ID_PCH_LPTLP_I218_V) ||
	    (hw->device_id == E1000_DEV_ID_PCH_I218_LM3) ||
	    (hw->device_id == E1000_DEV_ID_PCH_I218_V3)) {
		ret_val = e1000_k1_workaround_lpt_lp(hw, link);
		if (ret_val)
			return ret_val;
	}
	if (hw->mac.type >= e1000_pch_lpt) {
		/* Set platform power management values for
		 * Latency Tolerance Reporting (LTR)
		 * Optimized Buffer Flush/Fill (OBFF)
		 */
		ret_val = e1000_platform_pm_pch_lpt(hw, link);
		if (ret_val)
			return ret_val;
	}

	/* Clear link partner's EEE ability */
	hw->dev_spec.ich8lan.eee_lp_ability = 0;

	if (hw->mac.type >= e1000_pch_lpt) {
		u32 fextnvm6 = E1000_READ_REG(hw, E1000_FEXTNVM6);

		if (hw->mac.type == e1000_pch_spt) {
			/* FEXTNVM6 K1-off workaround - for SPT only */
			u32 pcieanacfg = E1000_READ_REG(hw, E1000_PCIEANACFG);

			if (pcieanacfg & E1000_FEXTNVM6_K1_OFF_ENABLE)
				fextnvm6 |= E1000_FEXTNVM6_K1_OFF_ENABLE;
			else
				fextnvm6 &= ~E1000_FEXTNVM6_K1_OFF_ENABLE;
		}

		if (hw->dev_spec.ich8lan.disable_k1_off == TRUE)
			fextnvm6 &= ~E1000_FEXTNVM6_K1_OFF_ENABLE;

		E1000_WRITE_REG(hw, E1000_FEXTNVM6, fextnvm6);
	}

	if (!link)
		return E1000_SUCCESS; /* No link detected */

	mac->get_link_status = FALSE;

	switch (hw->mac.type) {
	case e1000_pch2lan:
		ret_val = e1000_k1_workaround_lv(hw);
		if (ret_val)
			return ret_val;
		/* fall-thru */
	case e1000_pchlan:
		if (hw->phy.type == e1000_phy_82578) {
			ret_val = e1000_link_stall_workaround_hv(hw);
			if (ret_val)
				return ret_val;
		}

		/* Workaround for PCHx parts in half-duplex:
		 * Set the number of preambles removed from the packet
		 * when it is passed from the PHY to the MAC to prevent
		 * the MAC from misinterpreting the packet type.
		 */
		hw->phy.ops.read_reg(hw, HV_KMRN_FIFO_CTRLSTA, &phy_reg);
		phy_reg &= ~HV_KMRN_FIFO_CTRLSTA_PREAMBLE_MASK;

		if ((E1000_READ_REG(hw, E1000_STATUS) & E1000_STATUS_FD) !=
		    E1000_STATUS_FD)
			phy_reg |= (1 << HV_KMRN_FIFO_CTRLSTA_PREAMBLE_SHIFT);

		hw->phy.ops.write_reg(hw, HV_KMRN_FIFO_CTRLSTA, phy_reg);
		break;
	default:
		break;
	}

	/* Check if there was DownShift, must be checked
	 * immediately after link-up
	 */
	e1000_check_downshift_generic(hw);

	/* Enable/Disable EEE after link up */
	if (hw->phy.type > e1000_phy_82579) {
		ret_val = e1000_set_eee_pchlan(hw);
		if (ret_val)
			return ret_val;
	}

	/* If we are forcing speed/duplex, then we simply return since
	 * we have already determined whether we have link or not.
	 */
	if (!mac->autoneg)
		return -E1000_ERR_CONFIG;

	/* Auto-Neg is enabled.  Auto Speed Detection takes care
	 * of MAC speed/duplex configuration.  So we only need to
	 * configure Collision Distance in the MAC.
	 */
	mac->ops.config_collision_dist(hw);

	/* Configure Flow Control now that Auto-Neg has completed.
	 * First, we need to restore the desired flow control
	 * settings because we may have had to re-autoneg with a
	 * different link partner.
	 */
	ret_val = e1000_config_fc_after_link_up_generic(hw);
	if (ret_val)
		DEBUGOUT("Error configuring flow control\n");

	return ret_val;
}

/**
 *  e1000_init_function_pointers_ich8lan - Initialize ICH8 function pointers
 *  @hw: pointer to the HW structure
 *
 *  Initialize family-specific function pointers for PHY, MAC, and NVM.
 **/
void e1000_init_function_pointers_ich8lan(struct e1000_hw *hw)
{
	DEBUGFUNC("e1000_init_function_pointers_ich8lan");

	hw->mac.ops.init_params = e1000_init_mac_params_ich8lan;
	hw->nvm.ops.init_params = e1000_init_nvm_params_ich8lan;
	switch (hw->mac.type) {
	case e1000_ich8lan:
	case e1000_ich9lan:
	case e1000_ich10lan:
		hw->phy.ops.init_params = e1000_init_phy_params_ich8lan;
		break;
	case e1000_pchlan:
	case e1000_pch2lan:
	case e1000_pch_lpt:
	case e1000_pch_spt:
	case e1000_pch_cnp:
		hw->phy.ops.init_params = e1000_init_phy_params_pchlan;
		break;
	default:
		break;
	}
}

/**
 *  e1000_acquire_nvm_ich8lan - Acquire NVM mutex
 *  @hw: pointer to the HW structure
 *
 *  Acquires the mutex for performing NVM operations.
 **/
static s32 e1000_acquire_nvm_ich8lan(struct e1000_hw *hw)
{
	DEBUGFUNC("e1000_acquire_nvm_ich8lan");

	ASSERT_CTX_LOCK_HELD(hw);

	return E1000_SUCCESS;
}

/**
 *  e1000_release_nvm_ich8lan - Release NVM mutex
 *  @hw: pointer to the HW structure
 *
 *  Releases the mutex used while performing NVM operations.
 **/
static void e1000_release_nvm_ich8lan(struct e1000_hw *hw)
{
	DEBUGFUNC("e1000_release_nvm_ich8lan");

	ASSERT_CTX_LOCK_HELD(hw);
}

/**
 *  e1000_acquire_swflag_ich8lan - Acquire software control flag
 *  @hw: pointer to the HW structure
 *
 *  Acquires the software control flag for performing PHY and select
 *  MAC CSR accesses.
 **/
static s32 e1000_acquire_swflag_ich8lan(struct e1000_hw *hw)
{
	u32 extcnf_ctrl, timeout = PHY_CFG_TIMEOUT;
	s32 ret_val = E1000_SUCCESS;

	DEBUGFUNC("e1000_acquire_swflag_ich8lan");

	ASSERT_CTX_LOCK_HELD(hw);

	while (timeout) {
		extcnf_ctrl = E1000_READ_REG(hw, E1000_EXTCNF_CTRL);
		if (!(extcnf_ctrl & E1000_EXTCNF_CTRL_SWFLAG))
			break;

		msec_delay_irq(1);
		timeout--;
	}

	if (!timeout) {
		DEBUGOUT("SW has already locked the resource.\n");
		ret_val = -E1000_ERR_CONFIG;
		goto out;
	}

	timeout = SW_FLAG_TIMEOUT;

	extcnf_ctrl |= E1000_EXTCNF_CTRL_SWFLAG;
	E1000_WRITE_REG(hw, E1000_EXTCNF_CTRL, extcnf_ctrl);

	while (timeout) {
		extcnf_ctrl = E1000_READ_REG(hw, E1000_EXTCNF_CTRL);
		if (extcnf_ctrl & E1000_EXTCNF_CTRL_SWFLAG)
			break;

		msec_delay_irq(1);
		timeout--;
	}

	if (!timeout) {
		DEBUGOUT2("Failed to acquire the semaphore, FW or HW has it: FWSM=0x%8.8x EXTCNF_CTRL=0x%8.8x)\n",
			  E1000_READ_REG(hw, E1000_FWSM), extcnf_ctrl);
		extcnf_ctrl &= ~E1000_EXTCNF_CTRL_SWFLAG;
		E1000_WRITE_REG(hw, E1000_EXTCNF_CTRL, extcnf_ctrl);
		ret_val = -E1000_ERR_CONFIG;
		goto out;
	}

out:
	return ret_val;
}

/**
 *  e1000_release_swflag_ich8lan - Release software control flag
 *  @hw: pointer to the HW structure
 *
 *  Releases the software control flag for performing PHY and select
 *  MAC CSR accesses.
 **/
static void e1000_release_swflag_ich8lan(struct e1000_hw *hw)
{
	u32 extcnf_ctrl;

	DEBUGFUNC("e1000_release_swflag_ich8lan");

	extcnf_ctrl = E1000_READ_REG(hw, E1000_EXTCNF_CTRL);

	if (extcnf_ctrl & E1000_EXTCNF_CTRL_SWFLAG) {
		extcnf_ctrl &= ~E1000_EXTCNF_CTRL_SWFLAG;
		E1000_WRITE_REG(hw, E1000_EXTCNF_CTRL, extcnf_ctrl);
	} else {
		DEBUGOUT("Semaphore unexpectedly released by sw/fw/hw\n");
	}
}

/**
 *  e1000_check_mng_mode_ich8lan - Checks management mode
 *  @hw: pointer to the HW structure
 *
 *  This checks if the adapter has any manageability enabled.
 *  This is a function pointer entry point only called by read/write
 *  routines for the PHY and NVM parts.
 **/
static bool e1000_check_mng_mode_ich8lan(struct e1000_hw *hw)
{
	u32 fwsm;

	DEBUGFUNC("e1000_check_mng_mode_ich8lan");

	fwsm = E1000_READ_REG(hw, E1000_FWSM);

	return (fwsm & E1000_ICH_FWSM_FW_VALID) &&
	       ((fwsm & E1000_FWSM_MODE_MASK) ==
		(E1000_ICH_MNG_IAMT_MODE << E1000_FWSM_MODE_SHIFT));
}

/**
 *  e1000_check_mng_mode_pchlan - Checks management mode
 *  @hw: pointer to the HW structure
 *
 *  This checks if the adapter has iAMT enabled.
 *  This is a function pointer entry point only called by read/write
 *  routines for the PHY and NVM parts.
 **/
static bool e1000_check_mng_mode_pchlan(struct e1000_hw *hw)
{
	u32 fwsm;

	DEBUGFUNC("e1000_check_mng_mode_pchlan");

	fwsm = E1000_READ_REG(hw, E1000_FWSM);

	return (fwsm & E1000_ICH_FWSM_FW_VALID) &&
	       (fwsm & (E1000_ICH_MNG_IAMT_MODE << E1000_FWSM_MODE_SHIFT));
}

/**
 *  e1000_rar_set_pch2lan - Set receive address register
 *  @hw: pointer to the HW structure
 *  @addr: pointer to the receive address
 *  @index: receive address array register
 *
 *  Sets the receive address array register at index to the address passed
 *  in by addr.  For 82579, RAR[0] is the base address register that is to
 *  contain the MAC address but RAR[1-6] are reserved for manageability (ME).
 *  Use SHRA[0-3] in place of those reserved for ME.
 **/
static int e1000_rar_set_pch2lan(struct e1000_hw *hw, u8 *addr, u32 index)
{
	u32 rar_low, rar_high;

	DEBUGFUNC("e1000_rar_set_pch2lan");

	/* HW expects these in little endian so we reverse the byte order
	 * from network order (big endian) to little endian
	 */
	rar_low = ((u32) addr[0] |
		   ((u32) addr[1] << 8) |
		   ((u32) addr[2] << 16) | ((u32) addr[3] << 24));

	rar_high = ((u32) addr[4] | ((u32) addr[5] << 8));

	/* If MAC address zero, no need to set the AV bit */
	if (rar_low || rar_high)
		rar_high |= E1000_RAH_AV;

	if (index == 0) {
		E1000_WRITE_REG(hw, E1000_RAL(index), rar_low);
		E1000_WRITE_FLUSH(hw);
		E1000_WRITE_REG(hw, E1000_RAH(index), rar_high);
		E1000_WRITE_FLUSH(hw);
		return E1000_SUCCESS;
	}

	/* RAR[1-6] are owned by manageability.  Skip those and program the
	 * next address into the SHRA register array.
	 */
	if (index < (u32) (hw->mac.rar_entry_count)) {
		s32 ret_val;

		ret_val = e1000_acquire_swflag_ich8lan(hw);
		if (ret_val)
			goto out;

		E1000_WRITE_REG(hw, E1000_SHRAL(index - 1), rar_low);
		E1000_WRITE_FLUSH(hw);
		E1000_WRITE_REG(hw, E1000_SHRAH(index - 1), rar_high);
		E1000_WRITE_FLUSH(hw);

		e1000_release_swflag_ich8lan(hw);

		/* verify the register updates */
		if ((E1000_READ_REG(hw, E1000_SHRAL(index - 1)) == rar_low) &&
		    (E1000_READ_REG(hw, E1000_SHRAH(index - 1)) == rar_high))
			return E1000_SUCCESS;

		DEBUGOUT2("SHRA[%d] might be locked by ME - FWSM=0x%8.8x\n",
			 (index - 1), E1000_READ_REG(hw, E1000_FWSM));
	}

out:
	DEBUGOUT1("Failed to write receive address at index %d\n", index);
	return -E1000_ERR_CONFIG;
}

/**
 *  e1000_rar_set_pch_lpt - Set receive address registers
 *  @hw: pointer to the HW structure
 *  @addr: pointer to the receive address
 *  @index: receive address array register
 *
 *  Sets the receive address register array at index to the address passed
 *  in by addr. For LPT, RAR[0] is the base address register that is to
 *  contain the MAC address. SHRA[0-10] are the shared receive address
 *  registers that are shared between the Host and manageability engine (ME).
 **/
static int e1000_rar_set_pch_lpt(struct e1000_hw *hw, u8 *addr, u32 index)
{
	u32 rar_low, rar_high;
	u32 wlock_mac;

	DEBUGFUNC("e1000_rar_set_pch_lpt");

	/* HW expects these in little endian so we reverse the byte order
	 * from network order (big endian) to little endian
	 */
	rar_low = ((u32) addr[0] | ((u32) addr[1] << 8) |
		   ((u32) addr[2] << 16) | ((u32) addr[3] << 24));

	rar_high = ((u32) addr[4] | ((u32) addr[5] << 8));

	/* If MAC address zero, no need to set the AV bit */
	if (rar_low || rar_high)
		rar_high |= E1000_RAH_AV;

	if (index == 0) {
		E1000_WRITE_REG(hw, E1000_RAL(index), rar_low);
		E1000_WRITE_FLUSH(hw);
		E1000_WRITE_REG(hw, E1000_RAH(index), rar_high);
		E1000_WRITE_FLUSH(hw);
		return E1000_SUCCESS;
	}

	/* The manageability engine (ME) can lock certain SHRAR registers that
	 * it is using - those registers are unavailable for use.
	 */
	if (index < hw->mac.rar_entry_count) {
		wlock_mac = E1000_READ_REG(hw, E1000_FWSM) &
			    E1000_FWSM_WLOCK_MAC_MASK;
		wlock_mac >>= E1000_FWSM_WLOCK_MAC_SHIFT;

		/* Check if all SHRAR registers are locked */
		if (wlock_mac == 1)
			goto out;

		if ((wlock_mac == 0) || (index <= wlock_mac)) {
			s32 ret_val;

			ret_val = e1000_acquire_swflag_ich8lan(hw);

			if (ret_val)
				goto out;

			E1000_WRITE_REG(hw, E1000_SHRAL_PCH_LPT(index - 1),
					rar_low);
			E1000_WRITE_FLUSH(hw);
			E1000_WRITE_REG(hw, E1000_SHRAH_PCH_LPT(index - 1),
					rar_high);
			E1000_WRITE_FLUSH(hw);

			e1000_release_swflag_ich8lan(hw);

			/* verify the register updates */
			if ((E1000_READ_REG(hw, E1000_SHRAL_PCH_LPT(index - 1)) == rar_low) &&
			    (E1000_READ_REG(hw, E1000_SHRAH_PCH_LPT(index - 1)) == rar_high))
				return E1000_SUCCESS;
		}
	}

out:
	DEBUGOUT1("Failed to write receive address at index %d\n", index);
	return -E1000_ERR_CONFIG;
}

/**
 *  e1000_update_mc_addr_list_pch2lan - Update Multicast addresses
 *  @hw: pointer to the HW structure
 *  @mc_addr_list: array of multicast addresses to program
 *  @mc_addr_count: number of multicast addresses to program
 *
 *  Updates entire Multicast Table Array of the PCH2 MAC and PHY.
 *  The caller must have a packed mc_addr_list of multicast addresses.
 **/
static void e1000_update_mc_addr_list_pch2lan(struct e1000_hw *hw,
					      u8 *mc_addr_list,
					      u32 mc_addr_count)
{
	u16 phy_reg = 0;
	int i;
	s32 ret_val;

	DEBUGFUNC("e1000_update_mc_addr_list_pch2lan");

	e1000_update_mc_addr_list_generic(hw, mc_addr_list, mc_addr_count);

	ret_val = hw->phy.ops.acquire(hw);
	if (ret_val)
		return;

	ret_val = e1000_enable_phy_wakeup_reg_access_bm(hw, &phy_reg);
	if (ret_val)
		goto release;

	for (i = 0; i < hw->mac.mta_reg_count; i++) {
		hw->phy.ops.write_reg_page(hw, BM_MTA(i),
					   (u16)(hw->mac.mta_shadow[i] &
						 0xFFFF));
		hw->phy.ops.write_reg_page(hw, (BM_MTA(i) + 1),
					   (u16)((hw->mac.mta_shadow[i] >> 16) &
						 0xFFFF));
	}

	e1000_disable_phy_wakeup_reg_access_bm(hw, &phy_reg);

release:
	hw->phy.ops.release(hw);
}

/**
 *  e1000_check_reset_block_ich8lan - Check if PHY reset is blocked
 *  @hw: pointer to the HW structure
 *
 *  Checks if firmware is blocking the reset of the PHY.
 *  This is a function pointer entry point only called by
 *  reset routines.
 **/
static s32 e1000_check_reset_block_ich8lan(struct e1000_hw *hw)
{
	u32 fwsm;
	bool blocked = FALSE;
	int i = 0;

	DEBUGFUNC("e1000_check_reset_block_ich8lan");

	do {
		fwsm = E1000_READ_REG(hw, E1000_FWSM);
		if (!(fwsm & E1000_ICH_FWSM_RSPCIPHY)) {
			blocked = TRUE;
			msec_delay(10);
			continue;
		}
		blocked = FALSE;
	} while (blocked && (i++ < 30));
	return blocked ? E1000_BLK_PHY_RESET : E1000_SUCCESS;
}

/**
 *  e1000_write_smbus_addr - Write SMBus address to PHY needed during Sx states
 *  @hw: pointer to the HW structure
 *
 *  Assumes semaphore already acquired.
 *
 **/
static s32 e1000_write_smbus_addr(struct e1000_hw *hw)
{
	u16 phy_data;
	u32 strap = E1000_READ_REG(hw, E1000_STRAP);
	u32 freq = (strap & E1000_STRAP_SMT_FREQ_MASK) >>
		E1000_STRAP_SMT_FREQ_SHIFT;
	s32 ret_val;

	strap &= E1000_STRAP_SMBUS_ADDRESS_MASK;

	ret_val = e1000_read_phy_reg_hv_locked(hw, HV_SMB_ADDR, &phy_data);
	if (ret_val)
		return ret_val;

	phy_data &= ~HV_SMB_ADDR_MASK;
	phy_data |= (strap >> E1000_STRAP_SMBUS_ADDRESS_SHIFT);
	phy_data |= HV_SMB_ADDR_PEC_EN | HV_SMB_ADDR_VALID;

	if (hw->phy.type == e1000_phy_i217) {
		/* Restore SMBus frequency */
		if (freq--) {
			phy_data &= ~HV_SMB_ADDR_FREQ_MASK;
			phy_data |= (freq & (1 << 0)) <<
				HV_SMB_ADDR_FREQ_LOW_SHIFT;
			phy_data |= (freq & (1 << 1)) <<
				(HV_SMB_ADDR_FREQ_HIGH_SHIFT - 1);
		} else {
			DEBUGOUT("Unsupported SMB frequency in PHY\n");
		}
	}

	return e1000_write_phy_reg_hv_locked(hw, HV_SMB_ADDR, phy_data);
}

/**
 *  e1000_sw_lcd_config_ich8lan - SW-based LCD Configuration
 *  @hw:   pointer to the HW structure
 *
 *  SW should configure the LCD from the NVM extended configuration region
 *  as a workaround for certain parts.
 **/
static s32 e1000_sw_lcd_config_ich8lan(struct e1000_hw *hw)
{
	struct e1000_phy_info *phy = &hw->phy;
	u32 i, data, cnf_size, cnf_base_addr, sw_cfg_mask;
	s32 ret_val = E1000_SUCCESS;
	u16 word_addr, reg_data, reg_addr, phy_page = 0;

	DEBUGFUNC("e1000_sw_lcd_config_ich8lan");

	/* Initialize the PHY from the NVM on ICH platforms.  This
	 * is needed due to an issue where the NVM configuration is
	 * not properly autoloaded after power transitions.
	 * Therefore, after each PHY reset, we will load the
	 * configuration data out of the NVM manually.
	 */
	switch (hw->mac.type) {
	case e1000_ich8lan:
		if (phy->type != e1000_phy_igp_3)
			return ret_val;

		if ((hw->device_id == E1000_DEV_ID_ICH8_IGP_AMT) ||
		    (hw->device_id == E1000_DEV_ID_ICH8_IGP_C)) {
			sw_cfg_mask = E1000_FEXTNVM_SW_CONFIG;
			break;
		}
		/* Fall-thru */
	case e1000_pchlan:
	case e1000_pch2lan:
	case e1000_pch_lpt:
	case e1000_pch_spt:
	case e1000_pch_cnp:
		sw_cfg_mask = E1000_FEXTNVM_SW_CONFIG_ICH8M;
		break;
	default:
		return ret_val;
	}

	ret_val = hw->phy.ops.acquire(hw);
	if (ret_val)
		return ret_val;

	data = E1000_READ_REG(hw, E1000_FEXTNVM);
	if (!(data & sw_cfg_mask))
		goto release;

	/* Make sure HW does not configure LCD from PHY
	 * extended configuration before SW configuration
	 */
	data = E1000_READ_REG(hw, E1000_EXTCNF_CTRL);
	if ((hw->mac.type < e1000_pch2lan) &&
	    (data & E1000_EXTCNF_CTRL_LCD_WRITE_ENABLE))
			goto release;

	cnf_size = E1000_READ_REG(hw, E1000_EXTCNF_SIZE);
	cnf_size &= E1000_EXTCNF_SIZE_EXT_PCIE_LENGTH_MASK;
	cnf_size >>= E1000_EXTCNF_SIZE_EXT_PCIE_LENGTH_SHIFT;
	if (!cnf_size)
		goto release;

	cnf_base_addr = data & E1000_EXTCNF_CTRL_EXT_CNF_POINTER_MASK;
	cnf_base_addr >>= E1000_EXTCNF_CTRL_EXT_CNF_POINTER_SHIFT;

	if (((hw->mac.type == e1000_pchlan) &&
	     !(data & E1000_EXTCNF_CTRL_OEM_WRITE_ENABLE)) ||
	    (hw->mac.type > e1000_pchlan)) {
		/* HW configures the SMBus address and LEDs when the
		 * OEM and LCD Write Enable bits are set in the NVM.
		 * When both NVM bits are cleared, SW will configure
		 * them instead.
		 */
		ret_val = e1000_write_smbus_addr(hw);
		if (ret_val)
			goto release;

		data = E1000_READ_REG(hw, E1000_LEDCTL);
		ret_val = e1000_write_phy_reg_hv_locked(hw, HV_LED_CONFIG,
							(u16)data);
		if (ret_val)
			goto release;
	}

	/* Configure LCD from extended configuration region. */

	/* cnf_base_addr is in DWORD */
	word_addr = (u16)(cnf_base_addr << 1);

	for (i = 0; i < cnf_size; i++) {
		ret_val = hw->nvm.ops.read(hw, (word_addr + i * 2), 1,
					   &reg_data);
		if (ret_val)
			goto release;

		ret_val = hw->nvm.ops.read(hw, (word_addr + i * 2 + 1),
					   1, &reg_addr);
		if (ret_val)
			goto release;

		/* Save off the PHY page for future writes. */
		if (reg_addr == IGP01E1000_PHY_PAGE_SELECT) {
			phy_page = reg_data;
			continue;
		}

		reg_addr &= PHY_REG_MASK;
		reg_addr |= phy_page;

		ret_val = phy->ops.write_reg_locked(hw, (u32)reg_addr,
						    reg_data);
		if (ret_val)
			goto release;
	}

release:
	hw->phy.ops.release(hw);
	return ret_val;
}

/**
 *  e1000_k1_gig_workaround_hv - K1 Si workaround
 *  @hw:   pointer to the HW structure
 *  @link: link up bool flag
 *
 *  If K1 is enabled for 1Gbps, the MAC might stall when transitioning
 *  from a lower speed.  This workaround disables K1 whenever link is at 1Gig
 *  If link is down, the function will restore the default K1 setting located
 *  in the NVM.
 **/
static s32 e1000_k1_gig_workaround_hv(struct e1000_hw *hw, bool link)
{
	s32 ret_val = E1000_SUCCESS;
	u16 status_reg = 0;
	bool k1_enable = hw->dev_spec.ich8lan.nvm_k1_enabled;

	DEBUGFUNC("e1000_k1_gig_workaround_hv");

	if (hw->mac.type != e1000_pchlan)
		return E1000_SUCCESS;

	/* Wrap the whole flow with the sw flag */
	ret_val = hw->phy.ops.acquire(hw);
	if (ret_val)
		return ret_val;

	/* Disable K1 when link is 1Gbps, otherwise use the NVM setting */
	if (link) {
		if (hw->phy.type == e1000_phy_82578) {
			ret_val = hw->phy.ops.read_reg_locked(hw, BM_CS_STATUS,
							      &status_reg);
			if (ret_val)
				goto release;

			status_reg &= (BM_CS_STATUS_LINK_UP |
				       BM_CS_STATUS_RESOLVED |
				       BM_CS_STATUS_SPEED_MASK);

			if (status_reg == (BM_CS_STATUS_LINK_UP |
					   BM_CS_STATUS_RESOLVED |
					   BM_CS_STATUS_SPEED_1000))
				k1_enable = FALSE;
		}

		if (hw->phy.type == e1000_phy_82577) {
			ret_val = hw->phy.ops.read_reg_locked(hw, HV_M_STATUS,
							      &status_reg);
			if (ret_val)
				goto release;

			status_reg &= (HV_M_STATUS_LINK_UP |
				       HV_M_STATUS_AUTONEG_COMPLETE |
				       HV_M_STATUS_SPEED_MASK);

			if (status_reg == (HV_M_STATUS_LINK_UP |
					   HV_M_STATUS_AUTONEG_COMPLETE |
					   HV_M_STATUS_SPEED_1000))
				k1_enable = FALSE;
		}

		/* Link stall fix for link up */
		ret_val = hw->phy.ops.write_reg_locked(hw, PHY_REG(770, 19),
						       0x0100);
		if (ret_val)
			goto release;

	} else {
		/* Link stall fix for link down */
		ret_val = hw->phy.ops.write_reg_locked(hw, PHY_REG(770, 19),
						       0x4100);
		if (ret_val)
			goto release;
	}

	ret_val = e1000_configure_k1_ich8lan(hw, k1_enable);

release:
	hw->phy.ops.release(hw);

	return ret_val;
}

/**
 *  e1000_configure_k1_ich8lan - Configure K1 power state
 *  @hw: pointer to the HW structure
 *  @enable: K1 state to configure
 *
 *  Configure the K1 power state based on the provided parameter.
 *  Assumes semaphore already acquired.
 *
 *  Success returns 0, Failure returns -E1000_ERR_PHY (-2)
 **/
s32 e1000_configure_k1_ich8lan(struct e1000_hw *hw, bool k1_enable)
{
	s32 ret_val;
	u32 ctrl_reg = 0;
	u32 ctrl_ext = 0;
	u32 reg = 0;
	u16 kmrn_reg = 0;

	DEBUGFUNC("e1000_configure_k1_ich8lan");

	ret_val = e1000_read_kmrn_reg_locked(hw, E1000_KMRNCTRLSTA_K1_CONFIG,
					     &kmrn_reg);
	if (ret_val)
		return ret_val;

	if (k1_enable)
		kmrn_reg |= E1000_KMRNCTRLSTA_K1_ENABLE;
	else
		kmrn_reg &= ~E1000_KMRNCTRLSTA_K1_ENABLE;

	ret_val = e1000_write_kmrn_reg_locked(hw, E1000_KMRNCTRLSTA_K1_CONFIG,
					      kmrn_reg);
	if (ret_val)
		return ret_val;

	usec_delay(20);
	ctrl_ext = E1000_READ_REG(hw, E1000_CTRL_EXT);
	ctrl_reg = E1000_READ_REG(hw, E1000_CTRL);

	reg = ctrl_reg & ~(E1000_CTRL_SPD_1000 | E1000_CTRL_SPD_100);
	reg |= E1000_CTRL_FRCSPD;
	E1000_WRITE_REG(hw, E1000_CTRL, reg);

	E1000_WRITE_REG(hw, E1000_CTRL_EXT, ctrl_ext | E1000_CTRL_EXT_SPD_BYPS);
	E1000_WRITE_FLUSH(hw);
	usec_delay(20);
	E1000_WRITE_REG(hw, E1000_CTRL, ctrl_reg);
	E1000_WRITE_REG(hw, E1000_CTRL_EXT, ctrl_ext);
	E1000_WRITE_FLUSH(hw);
	usec_delay(20);

	return E1000_SUCCESS;
}

/**
 *  e1000_oem_bits_config_ich8lan - SW-based LCD Configuration
 *  @hw:       pointer to the HW structure
 *  @d0_state: boolean if entering d0 or d3 device state
 *
 *  SW will configure Gbe Disable and LPLU based on the NVM. The four bits are
 *  collectively called OEM bits.  The OEM Write Enable bit and SW Config bit
 *  in NVM determines whether HW should configure LPLU and Gbe Disable.
 **/
static s32 e1000_oem_bits_config_ich8lan(struct e1000_hw *hw, bool d0_state)
{
	s32 ret_val = 0;
	u32 mac_reg;
	u16 oem_reg;

	DEBUGFUNC("e1000_oem_bits_config_ich8lan");

	if (hw->mac.type < e1000_pchlan)
		return ret_val;

	ret_val = hw->phy.ops.acquire(hw);
	if (ret_val)
		return ret_val;

	if (hw->mac.type == e1000_pchlan) {
		mac_reg = E1000_READ_REG(hw, E1000_EXTCNF_CTRL);
		if (mac_reg & E1000_EXTCNF_CTRL_OEM_WRITE_ENABLE)
			goto release;
	}

	mac_reg = E1000_READ_REG(hw, E1000_FEXTNVM);
	if (!(mac_reg & E1000_FEXTNVM_SW_CONFIG_ICH8M))
		goto release;

	mac_reg = E1000_READ_REG(hw, E1000_PHY_CTRL);

	ret_val = hw->phy.ops.read_reg_locked(hw, HV_OEM_BITS, &oem_reg);
	if (ret_val)
		goto release;

	oem_reg &= ~(HV_OEM_BITS_GBE_DIS | HV_OEM_BITS_LPLU);

	if (d0_state) {
		if (mac_reg & E1000_PHY_CTRL_GBE_DISABLE)
			oem_reg |= HV_OEM_BITS_GBE_DIS;

		if (mac_reg & E1000_PHY_CTRL_D0A_LPLU)
			oem_reg |= HV_OEM_BITS_LPLU;
	} else {
		if (mac_reg & (E1000_PHY_CTRL_GBE_DISABLE |
		    E1000_PHY_CTRL_NOND0A_GBE_DISABLE))
			oem_reg |= HV_OEM_BITS_GBE_DIS;

		if (mac_reg & (E1000_PHY_CTRL_D0A_LPLU |
		    E1000_PHY_CTRL_NOND0A_LPLU))
			oem_reg |= HV_OEM_BITS_LPLU;
	}

	/* Set Restart auto-neg to activate the bits */
	if ((d0_state || (hw->mac.type != e1000_pchlan)) &&
	    !hw->phy.ops.check_reset_block(hw))
		oem_reg |= HV_OEM_BITS_RESTART_AN;

	ret_val = hw->phy.ops.write_reg_locked(hw, HV_OEM_BITS, oem_reg);

release:
	hw->phy.ops.release(hw);

	return ret_val;
}


/**
 *  e1000_set_mdio_slow_mode_hv - Set slow MDIO access mode
 *  @hw:   pointer to the HW structure
 **/
static s32 e1000_set_mdio_slow_mode_hv(struct e1000_hw *hw)
{
	s32 ret_val;
	u16 data;

	DEBUGFUNC("e1000_set_mdio_slow_mode_hv");

	ret_val = hw->phy.ops.read_reg(hw, HV_KMRN_MODE_CTRL, &data);
	if (ret_val)
		return ret_val;

	data |= HV_KMRN_MDIO_SLOW;

	ret_val = hw->phy.ops.write_reg(hw, HV_KMRN_MODE_CTRL, data);

	return ret_val;
}

/**
 *  e1000_hv_phy_workarounds_ich8lan - A series of Phy workarounds to be
 *  done after every PHY reset.
 **/
static s32 e1000_hv_phy_workarounds_ich8lan(struct e1000_hw *hw)
{
	s32 ret_val = E1000_SUCCESS;
	u16 phy_data;

	DEBUGFUNC("e1000_hv_phy_workarounds_ich8lan");

	if (hw->mac.type != e1000_pchlan)
		return E1000_SUCCESS;

	/* Set MDIO slow mode before any other MDIO access */
	if (hw->phy.type == e1000_phy_82577) {
		ret_val = e1000_set_mdio_slow_mode_hv(hw);
		if (ret_val)
			return ret_val;
	}

	if (((hw->phy.type == e1000_phy_82577) &&
	     ((hw->phy.revision == 1) || (hw->phy.revision == 2))) ||
	    ((hw->phy.type == e1000_phy_82578) && (hw->phy.revision == 1))) {
		/* Disable generation of early preamble */
		ret_val = hw->phy.ops.write_reg(hw, PHY_REG(769, 25), 0x4431);
		if (ret_val)
			return ret_val;

		/* Preamble tuning for SSC */
		ret_val = hw->phy.ops.write_reg(hw, HV_KMRN_FIFO_CTRLSTA,
						0xA204);
		if (ret_val)
			return ret_val;
	}

	if (hw->phy.type == e1000_phy_82578) {
		/* Return registers to default by doing a soft reset then
		 * writing 0x3140 to the control register.
		 */
		if (hw->phy.revision < 2) {
			e1000_phy_sw_reset_generic(hw);
			ret_val = hw->phy.ops.write_reg(hw, PHY_CONTROL,
							0x3140);
			if (ret_val)
				return ret_val;
		}
	}

	/* Select page 0 */
	ret_val = hw->phy.ops.acquire(hw);
	if (ret_val)
		return ret_val;

	hw->phy.addr = 1;
	ret_val = e1000_write_phy_reg_mdic(hw, IGP01E1000_PHY_PAGE_SELECT, 0);
	hw->phy.ops.release(hw);
	if (ret_val)
		return ret_val;

	/* Configure the K1 Si workaround during phy reset assuming there is
	 * link so that it disables K1 if link is in 1Gbps.
	 */
	ret_val = e1000_k1_gig_workaround_hv(hw, TRUE);
	if (ret_val)
		return ret_val;

	/* Workaround for link disconnects on a busy hub in half duplex */
	ret_val = hw->phy.ops.acquire(hw);
	if (ret_val)
		return ret_val;
	ret_val = hw->phy.ops.read_reg_locked(hw, BM_PORT_GEN_CFG, &phy_data);
	if (ret_val)
		goto release;
	ret_val = hw->phy.ops.write_reg_locked(hw, BM_PORT_GEN_CFG,
					       phy_data & 0x00FF);
	if (ret_val)
		goto release;

	/* set MSE higher to enable link to stay up when noise is high */
	ret_val = e1000_write_emi_reg_locked(hw, I82577_MSE_THRESHOLD, 0x0034);
release:
	hw->phy.ops.release(hw);

	return ret_val;
}

/**
 *  e1000_copy_rx_addrs_to_phy_ich8lan - Copy Rx addresses from MAC to PHY
 *  @hw:   pointer to the HW structure
 **/
void e1000_copy_rx_addrs_to_phy_ich8lan(struct e1000_hw *hw)
{
	u32 mac_reg;
	u16 i, phy_reg = 0;
	s32 ret_val;

	DEBUGFUNC("e1000_copy_rx_addrs_to_phy_ich8lan");

	ret_val = hw->phy.ops.acquire(hw);
	if (ret_val)
		return;
	ret_val = e1000_enable_phy_wakeup_reg_access_bm(hw, &phy_reg);
	if (ret_val)
		goto release;

	/* Copy both RAL/H (rar_entry_count) and SHRAL/H to PHY */
	for (i = 0; i < (hw->mac.rar_entry_count); i++) {
		mac_reg = E1000_READ_REG(hw, E1000_RAL(i));
		hw->phy.ops.write_reg_page(hw, BM_RAR_L(i),
					   (u16)(mac_reg & 0xFFFF));
		hw->phy.ops.write_reg_page(hw, BM_RAR_M(i),
					   (u16)((mac_reg >> 16) & 0xFFFF));

		mac_reg = E1000_READ_REG(hw, E1000_RAH(i));
		hw->phy.ops.write_reg_page(hw, BM_RAR_H(i),
					   (u16)(mac_reg & 0xFFFF));
		hw->phy.ops.write_reg_page(hw, BM_RAR_CTRL(i),
					   (u16)((mac_reg & E1000_RAH_AV)
						 >> 16));
	}

	e1000_disable_phy_wakeup_reg_access_bm(hw, &phy_reg);

release:
	hw->phy.ops.release(hw);
}

static u32 e1000_calc_rx_da_crc(u8 mac[])
{
	u32 poly = 0xEDB88320;	/* Polynomial for 802.3 CRC calculation */
	u32 i, j, mask, crc;

	DEBUGFUNC("e1000_calc_rx_da_crc");

	crc = 0xffffffff;
	for (i = 0; i < 6; i++) {
		crc = crc ^ mac[i];
		for (j = 8; j > 0; j--) {
			mask = (crc & 1) * (-1);
			crc = (crc >> 1) ^ (poly & mask);
		}
	}
	return ~crc;
}

/**
 *  e1000_lv_jumbo_workaround_ich8lan - required for jumbo frame operation
 *  with 82579 PHY
 *  @hw: pointer to the HW structure
 *  @enable: flag to enable/disable workaround when enabling/disabling jumbos
 **/
s32 e1000_lv_jumbo_workaround_ich8lan(struct e1000_hw *hw, bool enable)
{
	s32 ret_val = E1000_SUCCESS;
	u16 phy_reg, data;
	u32 mac_reg;
	u16 i;

	DEBUGFUNC("e1000_lv_jumbo_workaround_ich8lan");

	if (hw->mac.type < e1000_pch2lan)
		return E1000_SUCCESS;

	/* disable Rx path while enabling/disabling workaround */
	hw->phy.ops.read_reg(hw, PHY_REG(769, 20), &phy_reg);
	ret_val = hw->phy.ops.write_reg(hw, PHY_REG(769, 20),
					phy_reg | (1 << 14));
	if (ret_val)
		return ret_val;

	if (enable) {
		/* Write Rx addresses (rar_entry_count for RAL/H, and
		 * SHRAL/H) and initial CRC values to the MAC
		 */
		for (i = 0; i < hw->mac.rar_entry_count; i++) {
			u8 mac_addr[ETH_ADDR_LEN] = {0};
			u32 addr_high, addr_low;

			addr_high = E1000_READ_REG(hw, E1000_RAH(i));
			if (!(addr_high & E1000_RAH_AV))
				continue;
			addr_low = E1000_READ_REG(hw, E1000_RAL(i));
			mac_addr[0] = (addr_low & 0xFF);
			mac_addr[1] = ((addr_low >> 8) & 0xFF);
			mac_addr[2] = ((addr_low >> 16) & 0xFF);
			mac_addr[3] = ((addr_low >> 24) & 0xFF);
			mac_addr[4] = (addr_high & 0xFF);
			mac_addr[5] = ((addr_high >> 8) & 0xFF);

			E1000_WRITE_REG(hw, E1000_PCH_RAICC(i),
					e1000_calc_rx_da_crc(mac_addr));
		}

		/* Write Rx addresses to the PHY */
		e1000_copy_rx_addrs_to_phy_ich8lan(hw);

		/* Enable jumbo frame workaround in the MAC */
		mac_reg = E1000_READ_REG(hw, E1000_FFLT_DBG);
		mac_reg &= ~(1 << 14);
		mac_reg |= (7 << 15);
		E1000_WRITE_REG(hw, E1000_FFLT_DBG, mac_reg);

		mac_reg = E1000_READ_REG(hw, E1000_RCTL);
		mac_reg |= E1000_RCTL_SECRC;
		E1000_WRITE_REG(hw, E1000_RCTL, mac_reg);

		ret_val = e1000_read_kmrn_reg_generic(hw,
						E1000_KMRNCTRLSTA_CTRL_OFFSET,
						&data);
		if (ret_val)
			return ret_val;
		ret_val = e1000_write_kmrn_reg_generic(hw,
						E1000_KMRNCTRLSTA_CTRL_OFFSET,
						data | (1 << 0));
		if (ret_val)
			return ret_val;
		ret_val = e1000_read_kmrn_reg_generic(hw,
						E1000_KMRNCTRLSTA_HD_CTRL,
						&data);
		if (ret_val)
			return ret_val;
		data &= ~(0xF << 8);
		data |= (0xB << 8);
		ret_val = e1000_write_kmrn_reg_generic(hw,
						E1000_KMRNCTRLSTA_HD_CTRL,
						data);
		if (ret_val)
			return ret_val;

		/* Enable jumbo frame workaround in the PHY */
		hw->phy.ops.read_reg(hw, PHY_REG(769, 23), &data);
		data &= ~(0x7F << 5);
		data |= (0x37 << 5);
		ret_val = hw->phy.ops.write_reg(hw, PHY_REG(769, 23), data);
		if (ret_val)
			return ret_val;
		hw->phy.ops.read_reg(hw, PHY_REG(769, 16), &data);
		data &= ~(1 << 13);
		ret_val = hw->phy.ops.write_reg(hw, PHY_REG(769, 16), data);
		if (ret_val)
			return ret_val;
		hw->phy.ops.read_reg(hw, PHY_REG(776, 20), &data);
		data &= ~(0x3FF << 2);
		data |= (E1000_TX_PTR_GAP << 2);
		ret_val = hw->phy.ops.write_reg(hw, PHY_REG(776, 20), data);
		if (ret_val)
			return ret_val;
		ret_val = hw->phy.ops.write_reg(hw, PHY_REG(776, 23), 0xF100);
		if (ret_val)
			return ret_val;
		hw->phy.ops.read_reg(hw, HV_PM_CTRL, &data);
		ret_val = hw->phy.ops.write_reg(hw, HV_PM_CTRL, data |
						(1 << 10));
		if (ret_val)
			return ret_val;
	} else {
		/* Write MAC register values back to h/w defaults */
		mac_reg = E1000_READ_REG(hw, E1000_FFLT_DBG);
		mac_reg &= ~(0xF << 14);
		E1000_WRITE_REG(hw, E1000_FFLT_DBG, mac_reg);

		mac_reg = E1000_READ_REG(hw, E1000_RCTL);
		mac_reg &= ~E1000_RCTL_SECRC;
		E1000_WRITE_REG(hw, E1000_RCTL, mac_reg);

		ret_val = e1000_read_kmrn_reg_generic(hw,
						E1000_KMRNCTRLSTA_CTRL_OFFSET,
						&data);
		if (ret_val)
			return ret_val;
		ret_val = e1000_write_kmrn_reg_generic(hw,
						E1000_KMRNCTRLSTA_CTRL_OFFSET,
						data & ~(1 << 0));
		if (ret_val)
			return ret_val;
		ret_val = e1000_read_kmrn_reg_generic(hw,
						E1000_KMRNCTRLSTA_HD_CTRL,
						&data);
		if (ret_val)
			return ret_val;
		data &= ~(0xF << 8);
		data |= (0xB << 8);
		ret_val = e1000_write_kmrn_reg_generic(hw,
						E1000_KMRNCTRLSTA_HD_CTRL,
						data);
		if (ret_val)
			return ret_val;

		/* Write PHY register values back to h/w defaults */
		hw->phy.ops.read_reg(hw, PHY_REG(769, 23), &data);
		data &= ~(0x7F << 5);
		ret_val = hw->phy.ops.write_reg(hw, PHY_REG(769, 23), data);
		if (ret_val)
			return ret_val;
		hw->phy.ops.read_reg(hw, PHY_REG(769, 16), &data);
		data |= (1 << 13);
		ret_val = hw->phy.ops.write_reg(hw, PHY_REG(769, 16), data);
		if (ret_val)
			return ret_val;
		hw->phy.ops.read_reg(hw, PHY_REG(776, 20), &data);
		data &= ~(0x3FF << 2);
		data |= (0x8 << 2);
		ret_val = hw->phy.ops.write_reg(hw, PHY_REG(776, 20), data);
		if (ret_val)
			return ret_val;
		ret_val = hw->phy.ops.write_reg(hw, PHY_REG(776, 23), 0x7E00);
		if (ret_val)
			return ret_val;
		hw->phy.ops.read_reg(hw, HV_PM_CTRL, &data);
		ret_val = hw->phy.ops.write_reg(hw, HV_PM_CTRL, data &
						~(1 << 10));
		if (ret_val)
			return ret_val;
	}

	/* re-enable Rx path after enabling/disabling workaround */
	return hw->phy.ops.write_reg(hw, PHY_REG(769, 20), phy_reg &
				     ~(1 << 14));
}

/**
 *  e1000_lv_phy_workarounds_ich8lan - A series of Phy workarounds to be
 *  done after every PHY reset.
 **/
static s32 e1000_lv_phy_workarounds_ich8lan(struct e1000_hw *hw)
{
	s32 ret_val = E1000_SUCCESS;

	DEBUGFUNC("e1000_lv_phy_workarounds_ich8lan");

	if (hw->mac.type != e1000_pch2lan)
		return E1000_SUCCESS;

	/* Set MDIO slow mode before any other MDIO access */
	ret_val = e1000_set_mdio_slow_mode_hv(hw);
	if (ret_val)
		return ret_val;

	ret_val = hw->phy.ops.acquire(hw);
	if (ret_val)
		return ret_val;
	/* set MSE higher to enable link to stay up when noise is high */
	ret_val = e1000_write_emi_reg_locked(hw, I82579_MSE_THRESHOLD, 0x0034);
	if (ret_val)
		goto release;
	/* drop link after 5 times MSE threshold was reached */
	ret_val = e1000_write_emi_reg_locked(hw, I82579_MSE_LINK_DOWN, 0x0005);
release:
	hw->phy.ops.release(hw);

	return ret_val;
}

/**
 *  e1000_k1_gig_workaround_lv - K1 Si workaround
 *  @hw:   pointer to the HW structure
 *
 *  Workaround to set the K1 beacon duration for 82579 parts in 10Mbps
 *  Disable K1 for 1000 and 100 speeds
 **/
static s32 e1000_k1_workaround_lv(struct e1000_hw *hw)
{
	s32 ret_val = E1000_SUCCESS;
	u16 status_reg = 0;

	DEBUGFUNC("e1000_k1_workaround_lv");

	if (hw->mac.type != e1000_pch2lan)
		return E1000_SUCCESS;

	/* Set K1 beacon duration based on 10Mbs speed */
	ret_val = hw->phy.ops.read_reg(hw, HV_M_STATUS, &status_reg);
	if (ret_val)
		return ret_val;

	if ((status_reg & (HV_M_STATUS_LINK_UP | HV_M_STATUS_AUTONEG_COMPLETE))
	    == (HV_M_STATUS_LINK_UP | HV_M_STATUS_AUTONEG_COMPLETE)) {
		if (status_reg &
		    (HV_M_STATUS_SPEED_1000 | HV_M_STATUS_SPEED_100)) {
			u16 pm_phy_reg;

			/* LV 1G/100 Packet drop issue wa  */
			ret_val = hw->phy.ops.read_reg(hw, HV_PM_CTRL,
						       &pm_phy_reg);
			if (ret_val)
				return ret_val;
			pm_phy_reg &= ~HV_PM_CTRL_K1_ENABLE;
			ret_val = hw->phy.ops.write_reg(hw, HV_PM_CTRL,
							pm_phy_reg);
			if (ret_val)
				return ret_val;
		} else {
			u32 mac_reg;
			mac_reg = E1000_READ_REG(hw, E1000_FEXTNVM4);
			mac_reg &= ~E1000_FEXTNVM4_BEACON_DURATION_MASK;
			mac_reg |= E1000_FEXTNVM4_BEACON_DURATION_16USEC;
			E1000_WRITE_REG(hw, E1000_FEXTNVM4, mac_reg);
		}
	}

	return ret_val;
}

/**
 *  e1000_gate_hw_phy_config_ich8lan - disable PHY config via hardware
 *  @hw:   pointer to the HW structure
 *  @gate: boolean set to TRUE to gate, FALSE to ungate
 *
 *  Gate/ungate the automatic PHY configuration via hardware; perform
 *  the configuration via software instead.
 **/
static void e1000_gate_hw_phy_config_ich8lan(struct e1000_hw *hw, bool gate)
{
	u32 extcnf_ctrl;

	DEBUGFUNC("e1000_gate_hw_phy_config_ich8lan");

	if (hw->mac.type < e1000_pch2lan)
		return;

	extcnf_ctrl = E1000_READ_REG(hw, E1000_EXTCNF_CTRL);

	if (gate)
		extcnf_ctrl |= E1000_EXTCNF_CTRL_GATE_PHY_CFG;
	else
		extcnf_ctrl &= ~E1000_EXTCNF_CTRL_GATE_PHY_CFG;

	E1000_WRITE_REG(hw, E1000_EXTCNF_CTRL, extcnf_ctrl);
}

/**
 *  e1000_lan_init_done_ich8lan - Check for PHY config completion
 *  @hw: pointer to the HW structure
 *
 *  Check the appropriate indication the MAC has finished configuring the
 *  PHY after a software reset.
 **/
static void e1000_lan_init_done_ich8lan(struct e1000_hw *hw)
{
	u32 data, loop = E1000_ICH8_LAN_INIT_TIMEOUT;

	DEBUGFUNC("e1000_lan_init_done_ich8lan");

	/* Wait for basic configuration completes before proceeding */
	do {
		data = E1000_READ_REG(hw, E1000_STATUS);
		data &= E1000_STATUS_LAN_INIT_DONE;
		usec_delay(100);
	} while ((!data) && --loop);

	/* If basic configuration is incomplete before the above loop
	 * count reaches 0, loading the configuration from NVM will
	 * leave the PHY in a bad state possibly resulting in no link.
	 */
	if (loop == 0)
		DEBUGOUT("LAN_INIT_DONE not set, increase timeout\n");

	/* Clear the Init Done bit for the next init event */
	data = E1000_READ_REG(hw, E1000_STATUS);
	data &= ~E1000_STATUS_LAN_INIT_DONE;
	E1000_WRITE_REG(hw, E1000_STATUS, data);
}

/**
 *  e1000_post_phy_reset_ich8lan - Perform steps required after a PHY reset
 *  @hw: pointer to the HW structure
 **/
static s32 e1000_post_phy_reset_ich8lan(struct e1000_hw *hw)
{
	s32 ret_val = E1000_SUCCESS;
	u16 reg;

	DEBUGFUNC("e1000_post_phy_reset_ich8lan");

	if (hw->phy.ops.check_reset_block(hw))
		return E1000_SUCCESS;

	/* Allow time for h/w to get to quiescent state after reset */
	msec_delay(10);

	/* Perform any necessary post-reset workarounds */
	switch (hw->mac.type) {
	case e1000_pchlan:
		ret_val = e1000_hv_phy_workarounds_ich8lan(hw);
		if (ret_val)
			return ret_val;
		break;
	case e1000_pch2lan:
		ret_val = e1000_lv_phy_workarounds_ich8lan(hw);
		if (ret_val)
			return ret_val;
		break;
	default:
		break;
	}

	/* Clear the host wakeup bit after lcd reset */
	if (hw->mac.type >= e1000_pchlan) {
		hw->phy.ops.read_reg(hw, BM_PORT_GEN_CFG, &reg);
		reg &= ~BM_WUC_HOST_WU_BIT;
		hw->phy.ops.write_reg(hw, BM_PORT_GEN_CFG, reg);
	}

	/* Configure the LCD with the extended configuration region in NVM */
	ret_val = e1000_sw_lcd_config_ich8lan(hw);
	if (ret_val)
		return ret_val;

	/* Configure the LCD with the OEM bits in NVM */
	ret_val = e1000_oem_bits_config_ich8lan(hw, TRUE);

	if (hw->mac.type == e1000_pch2lan) {
		/* Ungate automatic PHY configuration on non-managed 82579 */
		if (!(E1000_READ_REG(hw, E1000_FWSM) &
		    E1000_ICH_FWSM_FW_VALID)) {
			msec_delay(10);
			e1000_gate_hw_phy_config_ich8lan(hw, FALSE);
		}

		/* Set EEE LPI Update Timer to 200usec */
		ret_val = hw->phy.ops.acquire(hw);
		if (ret_val)
			return ret_val;
		ret_val = e1000_write_emi_reg_locked(hw,
						     I82579_LPI_UPDATE_TIMER,
						     0x1387);
		hw->phy.ops.release(hw);
	}

	return ret_val;
}

/**
 *  e1000_phy_hw_reset_ich8lan - Performs a PHY reset
 *  @hw: pointer to the HW structure
 *
 *  Resets the PHY
 *  This is a function pointer entry point called by drivers
 *  or other shared routines.
 **/
static s32 e1000_phy_hw_reset_ich8lan(struct e1000_hw *hw)
{
	s32 ret_val = E1000_SUCCESS;

	DEBUGFUNC("e1000_phy_hw_reset_ich8lan");

	/* Gate automatic PHY configuration by hardware on non-managed 82579 */
	if ((hw->mac.type == e1000_pch2lan) &&
	    !(E1000_READ_REG(hw, E1000_FWSM) & E1000_ICH_FWSM_FW_VALID))
		e1000_gate_hw_phy_config_ich8lan(hw, TRUE);

	ret_val = e1000_phy_hw_reset_generic(hw);
	if (ret_val)
		return ret_val;

	return e1000_post_phy_reset_ich8lan(hw);
}

/**
 *  e1000_set_lplu_state_pchlan - Set Low Power Link Up state
 *  @hw: pointer to the HW structure
 *  @active: TRUE to enable LPLU, FALSE to disable
 *
 *  Sets the LPLU state according to the active flag.  For PCH, if OEM write
 *  bit are disabled in the NVM, writing the LPLU bits in the MAC will not set
 *  the phy speed. This function will manually set the LPLU bit and restart
 *  auto-neg as hw would do. D3 and D0 LPLU will call the same function
 *  since it configures the same bit.
 **/
static s32 e1000_set_lplu_state_pchlan(struct e1000_hw *hw, bool active)
{
	s32 ret_val;
	u16 oem_reg;

	DEBUGFUNC("e1000_set_lplu_state_pchlan");
	ret_val = hw->phy.ops.read_reg(hw, HV_OEM_BITS, &oem_reg);
	if (ret_val)
		return ret_val;

	if (active)
		oem_reg |= HV_OEM_BITS_LPLU;
	else
		oem_reg &= ~HV_OEM_BITS_LPLU;

	if (!hw->phy.ops.check_reset_block(hw))
		oem_reg |= HV_OEM_BITS_RESTART_AN;

	return hw->phy.ops.write_reg(hw, HV_OEM_BITS, oem_reg);
}

/**
 *  e1000_set_d0_lplu_state_ich8lan - Set Low Power Linkup D0 state
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
static s32 e1000_set_d0_lplu_state_ich8lan(struct e1000_hw *hw, bool active)
{
	struct e1000_phy_info *phy = &hw->phy;
	u32 phy_ctrl;
	s32 ret_val = E1000_SUCCESS;
	u16 data;

	DEBUGFUNC("e1000_set_d0_lplu_state_ich8lan");

	if (phy->type == e1000_phy_ife)
		return E1000_SUCCESS;

	phy_ctrl = E1000_READ_REG(hw, E1000_PHY_CTRL);

	if (active) {
		phy_ctrl |= E1000_PHY_CTRL_D0A_LPLU;
		E1000_WRITE_REG(hw, E1000_PHY_CTRL, phy_ctrl);

		if (phy->type != e1000_phy_igp_3)
			return E1000_SUCCESS;

		/* Call gig speed drop workaround on LPLU before accessing
		 * any PHY registers
		 */
		if (hw->mac.type == e1000_ich8lan)
			e1000_gig_downshift_workaround_ich8lan(hw);

		/* When LPLU is enabled, we should disable SmartSpeed */
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
	} else {
		phy_ctrl &= ~E1000_PHY_CTRL_D0A_LPLU;
		E1000_WRITE_REG(hw, E1000_PHY_CTRL, phy_ctrl);

		if (phy->type != e1000_phy_igp_3)
			return E1000_SUCCESS;

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
 *  e1000_set_d3_lplu_state_ich8lan - Set Low Power Linkup D3 state
 *  @hw: pointer to the HW structure
 *  @active: TRUE to enable LPLU, FALSE to disable
 *
 *  Sets the LPLU D3 state according to the active flag.  When
 *  activating LPLU this function also disables smart speed
 *  and vice versa.  LPLU will not be activated unless the
 *  device autonegotiation advertisement meets standards of
 *  either 10 or 10/100 or 10/100/1000 at all duplexes.
 *  This is a function pointer entry point only called by
 *  PHY setup routines.
 **/
static s32 e1000_set_d3_lplu_state_ich8lan(struct e1000_hw *hw, bool active)
{
	struct e1000_phy_info *phy = &hw->phy;
	u32 phy_ctrl;
	s32 ret_val = E1000_SUCCESS;
	u16 data;

	DEBUGFUNC("e1000_set_d3_lplu_state_ich8lan");

	phy_ctrl = E1000_READ_REG(hw, E1000_PHY_CTRL);

	if (!active) {
		phy_ctrl &= ~E1000_PHY_CTRL_NOND0A_LPLU;
		E1000_WRITE_REG(hw, E1000_PHY_CTRL, phy_ctrl);

		if (phy->type != e1000_phy_igp_3)
			return E1000_SUCCESS;

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
	} else if ((phy->autoneg_advertised == E1000_ALL_SPEED_DUPLEX) ||
		   (phy->autoneg_advertised == E1000_ALL_NOT_GIG) ||
		   (phy->autoneg_advertised == E1000_ALL_10_SPEED)) {
		phy_ctrl |= E1000_PHY_CTRL_NOND0A_LPLU;
		E1000_WRITE_REG(hw, E1000_PHY_CTRL, phy_ctrl);

		if (phy->type != e1000_phy_igp_3)
			return E1000_SUCCESS;

		/* Call gig speed drop workaround on LPLU before accessing
		 * any PHY registers
		 */
		if (hw->mac.type == e1000_ich8lan)
			e1000_gig_downshift_workaround_ich8lan(hw);

		/* When LPLU is enabled, we should disable SmartSpeed */
		ret_val = phy->ops.read_reg(hw,
					    IGP01E1000_PHY_PORT_CONFIG,
					    &data);
		if (ret_val)
			return ret_val;

		data &= ~IGP01E1000_PSCFR_SMART_SPEED;
		ret_val = phy->ops.write_reg(hw,
					     IGP01E1000_PHY_PORT_CONFIG,
					     data);
	}

	return ret_val;
}

/**
 *  e1000_valid_nvm_bank_detect_ich8lan - finds out the valid bank 0 or 1
 *  @hw: pointer to the HW structure
 *  @bank:  pointer to the variable that returns the active bank
 *
 *  Reads signature byte from the NVM using the flash access registers.
 *  Word 0x13 bits 15:14 = 10b indicate a valid signature for that bank.
 **/
static s32 e1000_valid_nvm_bank_detect_ich8lan(struct e1000_hw *hw, u32 *bank)
{
	u32 eecd;
	struct e1000_nvm_info *nvm = &hw->nvm;
	u32 bank1_offset = nvm->flash_bank_size * sizeof(u16);
	u32 act_offset = E1000_ICH_NVM_SIG_WORD * 2 + 1;
	u32 nvm_dword = 0;
	u8 sig_byte = 0;
	s32 ret_val;

	DEBUGFUNC("e1000_valid_nvm_bank_detect_ich8lan");

	switch (hw->mac.type) {
	case e1000_pch_spt:
	case e1000_pch_cnp:
		bank1_offset = nvm->flash_bank_size;
		act_offset = E1000_ICH_NVM_SIG_WORD;

		/* set bank to 0 in case flash read fails */
		*bank = 0;

		/* Check bank 0 */
		ret_val = e1000_read_flash_dword_ich8lan(hw, act_offset,
							 &nvm_dword);
		if (ret_val)
			return ret_val;
		sig_byte = (u8)((nvm_dword & 0xFF00) >> 8);
		if ((sig_byte & E1000_ICH_NVM_VALID_SIG_MASK) ==
		    E1000_ICH_NVM_SIG_VALUE) {
			*bank = 0;
			return E1000_SUCCESS;
		}

		/* Check bank 1 */
		ret_val = e1000_read_flash_dword_ich8lan(hw, act_offset +
							 bank1_offset,
							 &nvm_dword);
		if (ret_val)
			return ret_val;
		sig_byte = (u8)((nvm_dword & 0xFF00) >> 8);
		if ((sig_byte & E1000_ICH_NVM_VALID_SIG_MASK) ==
		    E1000_ICH_NVM_SIG_VALUE) {
			*bank = 1;
			return E1000_SUCCESS;
		}

		DEBUGOUT("ERROR: No valid NVM bank present\n");
		return -E1000_ERR_NVM;
	case e1000_ich8lan:
	case e1000_ich9lan:
		eecd = E1000_READ_REG(hw, E1000_EECD);
		if ((eecd & E1000_EECD_SEC1VAL_VALID_MASK) ==
		    E1000_EECD_SEC1VAL_VALID_MASK) {
			if (eecd & E1000_EECD_SEC1VAL)
				*bank = 1;
			else
				*bank = 0;

			return E1000_SUCCESS;
		}
		DEBUGOUT("Unable to determine valid NVM bank via EEC - reading flash signature\n");
		/* fall-thru */
	default:
		/* set bank to 0 in case flash read fails */
		*bank = 0;

		/* Check bank 0 */
		ret_val = e1000_read_flash_byte_ich8lan(hw, act_offset,
							&sig_byte);
		if (ret_val)
			return ret_val;
		if ((sig_byte & E1000_ICH_NVM_VALID_SIG_MASK) ==
		    E1000_ICH_NVM_SIG_VALUE) {
			*bank = 0;
			return E1000_SUCCESS;
		}

		/* Check bank 1 */
		ret_val = e1000_read_flash_byte_ich8lan(hw, act_offset +
							bank1_offset,
							&sig_byte);
		if (ret_val)
			return ret_val;
		if ((sig_byte & E1000_ICH_NVM_VALID_SIG_MASK) ==
		    E1000_ICH_NVM_SIG_VALUE) {
			*bank = 1;
			return E1000_SUCCESS;
		}

		DEBUGOUT("ERROR: No valid NVM bank present\n");
		return -E1000_ERR_NVM;
	}
}

/**
 *  e1000_read_nvm_spt - NVM access for SPT
 *  @hw: pointer to the HW structure
 *  @offset: The offset (in bytes) of the word(s) to read.
 *  @words: Size of data to read in words.
 *  @data: pointer to the word(s) to read at offset.
 *
 *  Reads a word(s) from the NVM
 **/
static s32 e1000_read_nvm_spt(struct e1000_hw *hw, u16 offset, u16 words,
			      u16 *data)
{
	struct e1000_nvm_info *nvm = &hw->nvm;
	struct e1000_dev_spec_ich8lan *dev_spec = &hw->dev_spec.ich8lan;
	u32 act_offset;
	s32 ret_val = E1000_SUCCESS;
	u32 bank = 0;
	u32 dword = 0;
	u16 offset_to_read;
	u16 i;

	DEBUGFUNC("e1000_read_nvm_spt");

	if ((offset >= nvm->word_size) || (words > nvm->word_size - offset) ||
	    (words == 0)) {
		DEBUGOUT("nvm parameter(s) out of bounds\n");
		ret_val = -E1000_ERR_NVM;
		goto out;
	}

	nvm->ops.acquire(hw);

	ret_val = e1000_valid_nvm_bank_detect_ich8lan(hw, &bank);
	if (ret_val != E1000_SUCCESS) {
		DEBUGOUT("Could not detect valid bank, assuming bank 0\n");
		bank = 0;
	}

	act_offset = (bank) ? nvm->flash_bank_size : 0;
	act_offset += offset;

	ret_val = E1000_SUCCESS;

	for (i = 0; i < words; i += 2) {
		if (words - i == 1) {
			if (dev_spec->shadow_ram[offset+i].modified) {
				data[i] = dev_spec->shadow_ram[offset+i].value;
			} else {
				offset_to_read = act_offset + i -
						 ((act_offset + i) % 2);
				ret_val =
				   e1000_read_flash_dword_ich8lan(hw,
								 offset_to_read,
								 &dword);
				if (ret_val)
					break;
				if ((act_offset + i) % 2 == 0)
					data[i] = (u16)(dword & 0xFFFF);
				else
					data[i] = (u16)((dword >> 16) & 0xFFFF);
			}
		} else {
			offset_to_read = act_offset + i;
			if (!(dev_spec->shadow_ram[offset+i].modified) ||
			    !(dev_spec->shadow_ram[offset+i+1].modified)) {
				ret_val =
				   e1000_read_flash_dword_ich8lan(hw,
								 offset_to_read,
								 &dword);
				if (ret_val)
					break;
			}
			if (dev_spec->shadow_ram[offset+i].modified)
				data[i] = dev_spec->shadow_ram[offset+i].value;
			else
				data[i] = (u16) (dword & 0xFFFF);
			if (dev_spec->shadow_ram[offset+i].modified)
				data[i+1] =
				   dev_spec->shadow_ram[offset+i+1].value;
			else
				data[i+1] = (u16) (dword >> 16 & 0xFFFF);
		}
	}

	nvm->ops.release(hw);

out:
	if (ret_val)
		DEBUGOUT1("NVM read error: %d\n", ret_val);

	return ret_val;
}

/**
 *  e1000_read_nvm_ich8lan - Read word(s) from the NVM
 *  @hw: pointer to the HW structure
 *  @offset: The offset (in bytes) of the word(s) to read.
 *  @words: Size of data to read in words
 *  @data: Pointer to the word(s) to read at offset.
 *
 *  Reads a word(s) from the NVM using the flash access registers.
 **/
static s32 e1000_read_nvm_ich8lan(struct e1000_hw *hw, u16 offset, u16 words,
				  u16 *data)
{
	struct e1000_nvm_info *nvm = &hw->nvm;
	struct e1000_dev_spec_ich8lan *dev_spec = &hw->dev_spec.ich8lan;
	u32 act_offset;
	s32 ret_val = E1000_SUCCESS;
	u32 bank = 0;
	u16 i, word;

	DEBUGFUNC("e1000_read_nvm_ich8lan");

	if ((offset >= nvm->word_size) || (words > nvm->word_size - offset) ||
	    (words == 0)) {
		DEBUGOUT("nvm parameter(s) out of bounds\n");
		ret_val = -E1000_ERR_NVM;
		goto out;
	}

	nvm->ops.acquire(hw);

	ret_val = e1000_valid_nvm_bank_detect_ich8lan(hw, &bank);
	if (ret_val != E1000_SUCCESS) {
		DEBUGOUT("Could not detect valid bank, assuming bank 0\n");
		bank = 0;
	}

	act_offset = (bank) ? nvm->flash_bank_size : 0;
	act_offset += offset;

	ret_val = E1000_SUCCESS;
	for (i = 0; i < words; i++) {
		if (dev_spec->shadow_ram[offset+i].modified) {
			data[i] = dev_spec->shadow_ram[offset+i].value;
		} else {
			ret_val = e1000_read_flash_word_ich8lan(hw,
								act_offset + i,
								&word);
			if (ret_val)
				break;
			data[i] = word;
		}
	}

	nvm->ops.release(hw);

out:
	if (ret_val)
		DEBUGOUT1("NVM read error: %d\n", ret_val);

	return ret_val;
}

/**
 *  e1000_flash_cycle_init_ich8lan - Initialize flash
 *  @hw: pointer to the HW structure
 *
 *  This function does initial flash setup so that a new read/write/erase cycle
 *  can be started.
 **/
static s32 e1000_flash_cycle_init_ich8lan(struct e1000_hw *hw)
{
	union ich8_hws_flash_status hsfsts;
	s32 ret_val = -E1000_ERR_NVM;

	DEBUGFUNC("e1000_flash_cycle_init_ich8lan");

	hsfsts.regval = E1000_READ_FLASH_REG16(hw, ICH_FLASH_HSFSTS);

	/* Check if the flash descriptor is valid */
	if (!hsfsts.hsf_status.fldesvalid) {
		DEBUGOUT("Flash descriptor invalid.  SW Sequencing must be used.\n");
		return -E1000_ERR_NVM;
	}

	/* Clear FCERR and DAEL in hw status by writing 1 */
	hsfsts.hsf_status.flcerr = 1;
	hsfsts.hsf_status.dael = 1;
	if (hw->mac.type >= e1000_pch_spt)
		E1000_WRITE_FLASH_REG(hw, ICH_FLASH_HSFSTS,
				      hsfsts.regval & 0xFFFF);
	else
		E1000_WRITE_FLASH_REG16(hw, ICH_FLASH_HSFSTS, hsfsts.regval);

	/* Either we should have a hardware SPI cycle in progress
	 * bit to check against, in order to start a new cycle or
	 * FDONE bit should be changed in the hardware so that it
	 * is 1 after hardware reset, which can then be used as an
	 * indication whether a cycle is in progress or has been
	 * completed.
	 */

	if (!hsfsts.hsf_status.flcinprog) {
		/* There is no cycle running at present,
		 * so we can start a cycle.
		 * Begin by setting Flash Cycle Done.
		 */
		hsfsts.hsf_status.flcdone = 1;
		if (hw->mac.type >= e1000_pch_spt)
			E1000_WRITE_FLASH_REG(hw, ICH_FLASH_HSFSTS,
					      hsfsts.regval & 0xFFFF);
		else
			E1000_WRITE_FLASH_REG16(hw, ICH_FLASH_HSFSTS,
						hsfsts.regval);
		ret_val = E1000_SUCCESS;
	} else {
		s32 i;

		/* Otherwise poll for sometime so the current
		 * cycle has a chance to end before giving up.
		 */
		for (i = 0; i < ICH_FLASH_READ_COMMAND_TIMEOUT; i++) {
			hsfsts.regval = E1000_READ_FLASH_REG16(hw,
							      ICH_FLASH_HSFSTS);
			if (!hsfsts.hsf_status.flcinprog) {
				ret_val = E1000_SUCCESS;
				break;
			}
			usec_delay(1);
		}
		if (ret_val == E1000_SUCCESS) {
			/* Successful in waiting for previous cycle to timeout,
			 * now set the Flash Cycle Done.
			 */
			hsfsts.hsf_status.flcdone = 1;
			if (hw->mac.type >= e1000_pch_spt)
				E1000_WRITE_FLASH_REG(hw, ICH_FLASH_HSFSTS,
						      hsfsts.regval & 0xFFFF);
			else
				E1000_WRITE_FLASH_REG16(hw, ICH_FLASH_HSFSTS,
							hsfsts.regval);
		} else {
			DEBUGOUT("Flash controller busy, cannot get access\n");
		}
	}

	return ret_val;
}

/**
 *  e1000_flash_cycle_ich8lan - Starts flash cycle (read/write/erase)
 *  @hw: pointer to the HW structure
 *  @timeout: maximum time to wait for completion
 *
 *  This function starts a flash cycle and waits for its completion.
 **/
static s32 e1000_flash_cycle_ich8lan(struct e1000_hw *hw, u32 timeout)
{
	union ich8_hws_flash_ctrl hsflctl;
	union ich8_hws_flash_status hsfsts;
	u32 i = 0;

	DEBUGFUNC("e1000_flash_cycle_ich8lan");

	/* Start a cycle by writing 1 in Flash Cycle Go in Hw Flash Control */
	if (hw->mac.type >= e1000_pch_spt)
		hsflctl.regval = E1000_READ_FLASH_REG(hw, ICH_FLASH_HSFSTS)>>16;
	else
		hsflctl.regval = E1000_READ_FLASH_REG16(hw, ICH_FLASH_HSFCTL);
	hsflctl.hsf_ctrl.flcgo = 1;

	if (hw->mac.type >= e1000_pch_spt)
		E1000_WRITE_FLASH_REG(hw, ICH_FLASH_HSFSTS,
				      hsflctl.regval << 16);
	else
		E1000_WRITE_FLASH_REG16(hw, ICH_FLASH_HSFCTL, hsflctl.regval);

	/* wait till FDONE bit is set to 1 */
	do {
		hsfsts.regval = E1000_READ_FLASH_REG16(hw, ICH_FLASH_HSFSTS);
		if (hsfsts.hsf_status.flcdone)
			break;
		usec_delay(1);
	} while (i++ < timeout);

	if (hsfsts.hsf_status.flcdone && !hsfsts.hsf_status.flcerr)
		return E1000_SUCCESS;

	return -E1000_ERR_NVM;
}

/**
 *  e1000_read_flash_dword_ich8lan - Read dword from flash
 *  @hw: pointer to the HW structure
 *  @offset: offset to data location
 *  @data: pointer to the location for storing the data
 *
 *  Reads the flash dword at offset into data.  Offset is converted
 *  to bytes before read.
 **/
static s32 e1000_read_flash_dword_ich8lan(struct e1000_hw *hw, u32 offset,
					  u32 *data)
{
	DEBUGFUNC("e1000_read_flash_dword_ich8lan");

	if (!data)
		return -E1000_ERR_NVM;

	/* Must convert word offset into bytes. */
	offset <<= 1;

	return e1000_read_flash_data32_ich8lan(hw, offset, data);
}

/**
 *  e1000_read_flash_word_ich8lan - Read word from flash
 *  @hw: pointer to the HW structure
 *  @offset: offset to data location
 *  @data: pointer to the location for storing the data
 *
 *  Reads the flash word at offset into data.  Offset is converted
 *  to bytes before read.
 **/
static s32 e1000_read_flash_word_ich8lan(struct e1000_hw *hw, u32 offset,
					 u16 *data)
{
	DEBUGFUNC("e1000_read_flash_word_ich8lan");

	if (!data)
		return -E1000_ERR_NVM;

	/* Must convert offset into bytes. */
	offset <<= 1;

	return e1000_read_flash_data_ich8lan(hw, offset, 2, data);
}

/**
 *  e1000_read_flash_byte_ich8lan - Read byte from flash
 *  @hw: pointer to the HW structure
 *  @offset: The offset of the byte to read.
 *  @data: Pointer to a byte to store the value read.
 *
 *  Reads a single byte from the NVM using the flash access registers.
 **/
static s32 e1000_read_flash_byte_ich8lan(struct e1000_hw *hw, u32 offset,
					 u8 *data)
{
	s32 ret_val;
	u16 word = 0;

	/* In SPT, only 32 bits access is supported,
	 * so this function should not be called.
	 */
	if (hw->mac.type >= e1000_pch_spt)
		return -E1000_ERR_NVM;
	else
		ret_val = e1000_read_flash_data_ich8lan(hw, offset, 1, &word);

	if (ret_val)
		return ret_val;

	*data = (u8)word;

	return E1000_SUCCESS;
}

/**
 *  e1000_read_flash_data_ich8lan - Read byte or word from NVM
 *  @hw: pointer to the HW structure
 *  @offset: The offset (in bytes) of the byte or word to read.
 *  @size: Size of data to read, 1=byte 2=word
 *  @data: Pointer to the word to store the value read.
 *
 *  Reads a byte or word from the NVM using the flash access registers.
 **/
static s32 e1000_read_flash_data_ich8lan(struct e1000_hw *hw, u32 offset,
					 u8 size, u16 *data)
{
	union ich8_hws_flash_status hsfsts;
	union ich8_hws_flash_ctrl hsflctl;
	u32 flash_linear_addr;
	u32 flash_data = 0;
	s32 ret_val = -E1000_ERR_NVM;
	u8 count = 0;

	DEBUGFUNC("e1000_read_flash_data_ich8lan");

	if (size < 1 || size > 2 || offset > ICH_FLASH_LINEAR_ADDR_MASK)
		return -E1000_ERR_NVM;
	flash_linear_addr = ((ICH_FLASH_LINEAR_ADDR_MASK & offset) +
			     hw->nvm.flash_base_addr);

	do {
		usec_delay(1);
		/* Steps */
		ret_val = e1000_flash_cycle_init_ich8lan(hw);
		if (ret_val != E1000_SUCCESS)
			break;
		hsflctl.regval = E1000_READ_FLASH_REG16(hw, ICH_FLASH_HSFCTL);

		/* 0b/1b corresponds to 1 or 2 byte size, respectively. */
		hsflctl.hsf_ctrl.fldbcount = size - 1;
		hsflctl.hsf_ctrl.flcycle = ICH_CYCLE_READ;
		E1000_WRITE_FLASH_REG16(hw, ICH_FLASH_HSFCTL, hsflctl.regval);
		E1000_WRITE_FLASH_REG(hw, ICH_FLASH_FADDR, flash_linear_addr);

		ret_val = e1000_flash_cycle_ich8lan(hw,
						ICH_FLASH_READ_COMMAND_TIMEOUT);

		/* Check if FCERR is set to 1, if set to 1, clear it
		 * and try the whole sequence a few more times, else
		 * read in (shift in) the Flash Data0, the order is
		 * least significant byte first msb to lsb
		 */
		if (ret_val == E1000_SUCCESS) {
			flash_data = E1000_READ_FLASH_REG(hw, ICH_FLASH_FDATA0);
			if (size == 1)
				*data = (u8)(flash_data & 0x000000FF);
			else if (size == 2)
				*data = (u16)(flash_data & 0x0000FFFF);
			break;
		} else {
			/* If we've gotten here, then things are probably
			 * completely hosed, but if the error condition is
			 * detected, it won't hurt to give it another try...
			 * ICH_FLASH_CYCLE_REPEAT_COUNT times.
			 */
			hsfsts.regval = E1000_READ_FLASH_REG16(hw,
							      ICH_FLASH_HSFSTS);
			if (hsfsts.hsf_status.flcerr) {
				/* Repeat for some time before giving up. */
				continue;
			} else if (!hsfsts.hsf_status.flcdone) {
				DEBUGOUT("Timeout error - flash cycle did not complete.\n");
				break;
			}
		}
	} while (count++ < ICH_FLASH_CYCLE_REPEAT_COUNT);

	return ret_val;
}

/**
 *  e1000_read_flash_data32_ich8lan - Read dword from NVM
 *  @hw: pointer to the HW structure
 *  @offset: The offset (in bytes) of the dword to read.
 *  @data: Pointer to the dword to store the value read.
 *
 *  Reads a byte or word from the NVM using the flash access registers.
 **/
static s32 e1000_read_flash_data32_ich8lan(struct e1000_hw *hw, u32 offset,
					   u32 *data)
{
	union ich8_hws_flash_status hsfsts;
	union ich8_hws_flash_ctrl hsflctl;
	u32 flash_linear_addr;
	s32 ret_val = -E1000_ERR_NVM;
	u8 count = 0;

	DEBUGFUNC("e1000_read_flash_data_ich8lan");

		if (offset > ICH_FLASH_LINEAR_ADDR_MASK ||
		    hw->mac.type < e1000_pch_spt)
			return -E1000_ERR_NVM;
	flash_linear_addr = ((ICH_FLASH_LINEAR_ADDR_MASK & offset) +
			     hw->nvm.flash_base_addr);

	do {
		usec_delay(1);
		/* Steps */
		ret_val = e1000_flash_cycle_init_ich8lan(hw);
		if (ret_val != E1000_SUCCESS)
			break;
		/* In SPT, This register is in Lan memory space, not flash.
		 * Therefore, only 32 bit access is supported
		 */
		hsflctl.regval = E1000_READ_FLASH_REG(hw, ICH_FLASH_HSFSTS)>>16;

		/* 0b/1b corresponds to 1 or 2 byte size, respectively. */
		hsflctl.hsf_ctrl.fldbcount = sizeof(u32) - 1;
		hsflctl.hsf_ctrl.flcycle = ICH_CYCLE_READ;
		/* In SPT, This register is in Lan memory space, not flash.
		 * Therefore, only 32 bit access is supported
		 */
		E1000_WRITE_FLASH_REG(hw, ICH_FLASH_HSFSTS,
				      (u32)hsflctl.regval << 16);
		E1000_WRITE_FLASH_REG(hw, ICH_FLASH_FADDR, flash_linear_addr);

		ret_val = e1000_flash_cycle_ich8lan(hw,
						ICH_FLASH_READ_COMMAND_TIMEOUT);

		/* Check if FCERR is set to 1, if set to 1, clear it
		 * and try the whole sequence a few more times, else
		 * read in (shift in) the Flash Data0, the order is
		 * least significant byte first msb to lsb
		 */
		if (ret_val == E1000_SUCCESS) {
			*data = E1000_READ_FLASH_REG(hw, ICH_FLASH_FDATA0);
			break;
		} else {
			/* If we've gotten here, then things are probably
			 * completely hosed, but if the error condition is
			 * detected, it won't hurt to give it another try...
			 * ICH_FLASH_CYCLE_REPEAT_COUNT times.
			 */
			hsfsts.regval = E1000_READ_FLASH_REG16(hw,
							      ICH_FLASH_HSFSTS);
			if (hsfsts.hsf_status.flcerr) {
				/* Repeat for some time before giving up. */
				continue;
			} else if (!hsfsts.hsf_status.flcdone) {
				DEBUGOUT("Timeout error - flash cycle did not complete.\n");
				break;
			}
		}
	} while (count++ < ICH_FLASH_CYCLE_REPEAT_COUNT);

	return ret_val;
}

/**
 *  e1000_write_nvm_ich8lan - Write word(s) to the NVM
 *  @hw: pointer to the HW structure
 *  @offset: The offset (in bytes) of the word(s) to write.
 *  @words: Size of data to write in words
 *  @data: Pointer to the word(s) to write at offset.
 *
 *  Writes a byte or word to the NVM using the flash access registers.
 **/
static s32 e1000_write_nvm_ich8lan(struct e1000_hw *hw, u16 offset, u16 words,
				   u16 *data)
{
	struct e1000_nvm_info *nvm = &hw->nvm;
	struct e1000_dev_spec_ich8lan *dev_spec = &hw->dev_spec.ich8lan;
	u16 i;

	DEBUGFUNC("e1000_write_nvm_ich8lan");

	if ((offset >= nvm->word_size) || (words > nvm->word_size - offset) ||
	    (words == 0)) {
		DEBUGOUT("nvm parameter(s) out of bounds\n");
		return -E1000_ERR_NVM;
	}

	nvm->ops.acquire(hw);

	for (i = 0; i < words; i++) {
		dev_spec->shadow_ram[offset+i].modified = TRUE;
		dev_spec->shadow_ram[offset+i].value = data[i];
	}

	nvm->ops.release(hw);

	return E1000_SUCCESS;
}

/**
 *  e1000_update_nvm_checksum_spt - Update the checksum for NVM
 *  @hw: pointer to the HW structure
 *
 *  The NVM checksum is updated by calling the generic update_nvm_checksum,
 *  which writes the checksum to the shadow ram.  The changes in the shadow
 *  ram are then committed to the EEPROM by processing each bank at a time
 *  checking for the modified bit and writing only the pending changes.
 *  After a successful commit, the shadow ram is cleared and is ready for
 *  future writes.
 **/
static s32 e1000_update_nvm_checksum_spt(struct e1000_hw *hw)
{
	struct e1000_nvm_info *nvm = &hw->nvm;
	struct e1000_dev_spec_ich8lan *dev_spec = &hw->dev_spec.ich8lan;
	u32 i, act_offset, new_bank_offset, old_bank_offset, bank;
	s32 ret_val;
	u32 dword = 0;

	DEBUGFUNC("e1000_update_nvm_checksum_spt");

	ret_val = e1000_update_nvm_checksum_generic(hw);
	if (ret_val)
		goto out;

	if (nvm->type != e1000_nvm_flash_sw)
		goto out;

	nvm->ops.acquire(hw);

	/* We're writing to the opposite bank so if we're on bank 1,
	 * write to bank 0 etc.  We also need to erase the segment that
	 * is going to be written
	 */
	ret_val =  e1000_valid_nvm_bank_detect_ich8lan(hw, &bank);
	if (ret_val != E1000_SUCCESS) {
		DEBUGOUT("Could not detect valid bank, assuming bank 0\n");
		bank = 0;
	}

	if (bank == 0) {
		new_bank_offset = nvm->flash_bank_size;
		old_bank_offset = 0;
		ret_val = e1000_erase_flash_bank_ich8lan(hw, 1);
		if (ret_val)
			goto release;
	} else {
		old_bank_offset = nvm->flash_bank_size;
		new_bank_offset = 0;
		ret_val = e1000_erase_flash_bank_ich8lan(hw, 0);
		if (ret_val)
			goto release;
	}
	for (i = 0; i < E1000_SHADOW_RAM_WORDS; i += 2) {
		/* Determine whether to write the value stored
		 * in the other NVM bank or a modified value stored
		 * in the shadow RAM
		 */
		ret_val = e1000_read_flash_dword_ich8lan(hw,
							 i + old_bank_offset,
							 &dword);

		if (dev_spec->shadow_ram[i].modified) {
			dword &= 0xffff0000;
			dword |= (dev_spec->shadow_ram[i].value & 0xffff);
		}
		if (dev_spec->shadow_ram[i + 1].modified) {
			dword &= 0x0000ffff;
			dword |= ((dev_spec->shadow_ram[i + 1].value & 0xffff)
				  << 16);
		}
		if (ret_val)
			break;

		/* If the word is 0x13, then make sure the signature bits
		 * (15:14) are 11b until the commit has completed.
		 * This will allow us to write 10b which indicates the
		 * signature is valid.  We want to do this after the write
		 * has completed so that we don't mark the segment valid
		 * while the write is still in progress
		 */
		if (i == E1000_ICH_NVM_SIG_WORD - 1)
			dword |= E1000_ICH_NVM_SIG_MASK << 16;

		/* Convert offset to bytes. */
		act_offset = (i + new_bank_offset) << 1;

		usec_delay(100);

		/* Write the data to the new bank. Offset in words*/
		act_offset = i + new_bank_offset;
		ret_val = e1000_retry_write_flash_dword_ich8lan(hw, act_offset,
								dword);
		if (ret_val)
			break;
	 }

	/* Don't bother writing the segment valid bits if sector
	 * programming failed.
	 */
	if (ret_val) {
		DEBUGOUT("Flash commit failed.\n");
		goto release;
	}

	/* Finally validate the new segment by setting bit 15:14
	 * to 10b in word 0x13 , this can be done without an
	 * erase as well since these bits are 11 to start with
	 * and we need to change bit 14 to 0b
	 */
	act_offset = new_bank_offset + E1000_ICH_NVM_SIG_WORD;

	/*offset in words but we read dword*/
	--act_offset;
	ret_val = e1000_read_flash_dword_ich8lan(hw, act_offset, &dword);

	if (ret_val)
		goto release;

	dword &= 0xBFFFFFFF;
	ret_val = e1000_retry_write_flash_dword_ich8lan(hw, act_offset, dword);

	if (ret_val)
		goto release;

	/* And invalidate the previously valid segment by setting
	 * its signature word (0x13) high_byte to 0b. This can be
	 * done without an erase because flash erase sets all bits
	 * to 1's. We can write 1's to 0's without an erase
	 */
	act_offset = (old_bank_offset + E1000_ICH_NVM_SIG_WORD) * 2 + 1;

	/* offset in words but we read dword*/
	act_offset = old_bank_offset + E1000_ICH_NVM_SIG_WORD - 1;
	ret_val = e1000_read_flash_dword_ich8lan(hw, act_offset, &dword);

	if (ret_val)
		goto release;

	dword &= 0x00FFFFFF;
	ret_val = e1000_retry_write_flash_dword_ich8lan(hw, act_offset, dword);

	if (ret_val)
		goto release;

	/* Great!  Everything worked, we can now clear the cached entries. */
	for (i = 0; i < E1000_SHADOW_RAM_WORDS; i++) {
		dev_spec->shadow_ram[i].modified = FALSE;
		dev_spec->shadow_ram[i].value = 0xFFFF;
	}

release:
	nvm->ops.release(hw);

	/* Reload the EEPROM, or else modifications will not appear
	 * until after the next adapter reset.
	 */
	if (!ret_val) {
		nvm->ops.reload(hw);
		msec_delay(10);
	}

out:
	if (ret_val)
		DEBUGOUT1("NVM update error: %d\n", ret_val);

	return ret_val;
}

/**
 *  e1000_update_nvm_checksum_ich8lan - Update the checksum for NVM
 *  @hw: pointer to the HW structure
 *
 *  The NVM checksum is updated by calling the generic update_nvm_checksum,
 *  which writes the checksum to the shadow ram.  The changes in the shadow
 *  ram are then committed to the EEPROM by processing each bank at a time
 *  checking for the modified bit and writing only the pending changes.
 *  After a successful commit, the shadow ram is cleared and is ready for
 *  future writes.
 **/
static s32 e1000_update_nvm_checksum_ich8lan(struct e1000_hw *hw)
{
	struct e1000_nvm_info *nvm = &hw->nvm;
	struct e1000_dev_spec_ich8lan *dev_spec = &hw->dev_spec.ich8lan;
	u32 i, act_offset, new_bank_offset, old_bank_offset, bank;
	s32 ret_val;
	u16 data = 0;

	DEBUGFUNC("e1000_update_nvm_checksum_ich8lan");

	ret_val = e1000_update_nvm_checksum_generic(hw);
	if (ret_val)
		goto out;

	if (nvm->type != e1000_nvm_flash_sw)
		goto out;

	nvm->ops.acquire(hw);

	/* We're writing to the opposite bank so if we're on bank 1,
	 * write to bank 0 etc.  We also need to erase the segment that
	 * is going to be written
	 */
	ret_val =  e1000_valid_nvm_bank_detect_ich8lan(hw, &bank);
	if (ret_val != E1000_SUCCESS) {
		DEBUGOUT("Could not detect valid bank, assuming bank 0\n");
		bank = 0;
	}

	if (bank == 0) {
		new_bank_offset = nvm->flash_bank_size;
		old_bank_offset = 0;
		ret_val = e1000_erase_flash_bank_ich8lan(hw, 1);
		if (ret_val)
			goto release;
	} else {
		old_bank_offset = nvm->flash_bank_size;
		new_bank_offset = 0;
		ret_val = e1000_erase_flash_bank_ich8lan(hw, 0);
		if (ret_val)
			goto release;
	}
	for (i = 0; i < E1000_SHADOW_RAM_WORDS; i++) {
		if (dev_spec->shadow_ram[i].modified) {
			data = dev_spec->shadow_ram[i].value;
		} else {
			ret_val = e1000_read_flash_word_ich8lan(hw, i +
								old_bank_offset,
								&data);
			if (ret_val)
				break;
		}
		/* If the word is 0x13, then make sure the signature bits
		 * (15:14) are 11b until the commit has completed.
		 * This will allow us to write 10b which indicates the
		 * signature is valid.  We want to do this after the write
		 * has completed so that we don't mark the segment valid
		 * while the write is still in progress
		 */
		if (i == E1000_ICH_NVM_SIG_WORD)
			data |= E1000_ICH_NVM_SIG_MASK;

		/* Convert offset to bytes. */
		act_offset = (i + new_bank_offset) << 1;

		usec_delay(100);

		/* Write the bytes to the new bank. */
		ret_val = e1000_retry_write_flash_byte_ich8lan(hw,
							       act_offset,
							       (u8)data);
		if (ret_val)
			break;

		usec_delay(100);
		ret_val = e1000_retry_write_flash_byte_ich8lan(hw,
							  act_offset + 1,
							  (u8)(data >> 8));
		if (ret_val)
			break;
	 }

	/* Don't bother writing the segment valid bits if sector
	 * programming failed.
	 */
	if (ret_val) {
		DEBUGOUT("Flash commit failed.\n");
		goto release;
	}

	/* Finally validate the new segment by setting bit 15:14
	 * to 10b in word 0x13 , this can be done without an
	 * erase as well since these bits are 11 to start with
	 * and we need to change bit 14 to 0b
	 */
	act_offset = new_bank_offset + E1000_ICH_NVM_SIG_WORD;
	ret_val = e1000_read_flash_word_ich8lan(hw, act_offset, &data);
	if (ret_val)
		goto release;

	data &= 0xBFFF;
	ret_val = e1000_retry_write_flash_byte_ich8lan(hw, act_offset * 2 + 1,
						       (u8)(data >> 8));
	if (ret_val)
		goto release;

	/* And invalidate the previously valid segment by setting
	 * its signature word (0x13) high_byte to 0b. This can be
	 * done without an erase because flash erase sets all bits
	 * to 1's. We can write 1's to 0's without an erase
	 */
	act_offset = (old_bank_offset + E1000_ICH_NVM_SIG_WORD) * 2 + 1;

	ret_val = e1000_retry_write_flash_byte_ich8lan(hw, act_offset, 0);

	if (ret_val)
		goto release;

	/* Great!  Everything worked, we can now clear the cached entries. */
	for (i = 0; i < E1000_SHADOW_RAM_WORDS; i++) {
		dev_spec->shadow_ram[i].modified = FALSE;
		dev_spec->shadow_ram[i].value = 0xFFFF;
	}

release:
	nvm->ops.release(hw);

	/* Reload the EEPROM, or else modifications will not appear
	 * until after the next adapter reset.
	 */
	if (!ret_val) {
		nvm->ops.reload(hw);
		msec_delay(10);
	}

out:
	if (ret_val)
		DEBUGOUT1("NVM update error: %d\n", ret_val);

	return ret_val;
}

/**
 *  e1000_validate_nvm_checksum_ich8lan - Validate EEPROM checksum
 *  @hw: pointer to the HW structure
 *
 *  Check to see if checksum needs to be fixed by reading bit 6 in word 0x19.
 *  If the bit is 0, that the EEPROM had been modified, but the checksum was not
 *  calculated, in which case we need to calculate the checksum and set bit 6.
 **/
static s32 e1000_validate_nvm_checksum_ich8lan(struct e1000_hw *hw)
{
	s32 ret_val;
	u16 data;
	u16 word;
	u16 valid_csum_mask;

	DEBUGFUNC("e1000_validate_nvm_checksum_ich8lan");

	/* Read NVM and check Invalid Image CSUM bit.  If this bit is 0,
	 * the checksum needs to be fixed.  This bit is an indication that
	 * the NVM was prepared by OEM software and did not calculate
	 * the checksum...a likely scenario.
	 */
	switch (hw->mac.type) {
	case e1000_pch_lpt:
	case e1000_pch_spt:
	case e1000_pch_cnp:
		word = NVM_COMPAT;
		valid_csum_mask = NVM_COMPAT_VALID_CSUM;
		break;
	default:
		word = NVM_FUTURE_INIT_WORD1;
		valid_csum_mask = NVM_FUTURE_INIT_WORD1_VALID_CSUM;
		break;
	}

	ret_val = hw->nvm.ops.read(hw, word, 1, &data);
	if (ret_val)
		return ret_val;

	if (!(data & valid_csum_mask)) {
		data |= valid_csum_mask;
		ret_val = hw->nvm.ops.write(hw, word, 1, &data);
		if (ret_val)
			return ret_val;
		ret_val = hw->nvm.ops.update(hw);
		if (ret_val)
			return ret_val;
	}

	return e1000_validate_nvm_checksum_generic(hw);
}

/**
 *  e1000_write_flash_data_ich8lan - Writes bytes to the NVM
 *  @hw: pointer to the HW structure
 *  @offset: The offset (in bytes) of the byte/word to read.
 *  @size: Size of data to read, 1=byte 2=word
 *  @data: The byte(s) to write to the NVM.
 *
 *  Writes one/two bytes to the NVM using the flash access registers.
 **/
static s32 e1000_write_flash_data_ich8lan(struct e1000_hw *hw, u32 offset,
					  u8 size, u16 data)
{
	union ich8_hws_flash_status hsfsts;
	union ich8_hws_flash_ctrl hsflctl;
	u32 flash_linear_addr;
	u32 flash_data = 0;
	s32 ret_val;
	u8 count = 0;

	DEBUGFUNC("e1000_write_ich8_data");

	if (hw->mac.type >= e1000_pch_spt) {
		if (size != 4 || offset > ICH_FLASH_LINEAR_ADDR_MASK)
			return -E1000_ERR_NVM;
	} else {
		if (size < 1 || size > 2 || offset > ICH_FLASH_LINEAR_ADDR_MASK)
			return -E1000_ERR_NVM;
	}

	flash_linear_addr = ((ICH_FLASH_LINEAR_ADDR_MASK & offset) +
			     hw->nvm.flash_base_addr);

	do {
		usec_delay(1);
		/* Steps */
		ret_val = e1000_flash_cycle_init_ich8lan(hw);
		if (ret_val != E1000_SUCCESS)
			break;
		/* In SPT, This register is in Lan memory space, not
		 * flash.  Therefore, only 32 bit access is supported
		 */
		if (hw->mac.type >= e1000_pch_spt)
			hsflctl.regval =
			    E1000_READ_FLASH_REG(hw, ICH_FLASH_HSFSTS)>>16;
		else
			hsflctl.regval =
			    E1000_READ_FLASH_REG16(hw, ICH_FLASH_HSFCTL);

		/* 0b/1b corresponds to 1 or 2 byte size, respectively. */
		hsflctl.hsf_ctrl.fldbcount = size - 1;
		hsflctl.hsf_ctrl.flcycle = ICH_CYCLE_WRITE;
		/* In SPT, This register is in Lan memory space,
		 * not flash.  Therefore, only 32 bit access is
		 * supported
		 */
		if (hw->mac.type >= e1000_pch_spt)
			E1000_WRITE_FLASH_REG(hw, ICH_FLASH_HSFSTS,
					      hsflctl.regval << 16);
		else
			E1000_WRITE_FLASH_REG16(hw, ICH_FLASH_HSFCTL,
						hsflctl.regval);

		E1000_WRITE_FLASH_REG(hw, ICH_FLASH_FADDR, flash_linear_addr);

		if (size == 1)
			flash_data = (u32)data & 0x00FF;
		else
			flash_data = (u32)data;

		E1000_WRITE_FLASH_REG(hw, ICH_FLASH_FDATA0, flash_data);

		/* check if FCERR is set to 1 , if set to 1, clear it
		 * and try the whole sequence a few more times else done
		 */
		ret_val =
		    e1000_flash_cycle_ich8lan(hw,
					      ICH_FLASH_WRITE_COMMAND_TIMEOUT);
		if (ret_val == E1000_SUCCESS)
			break;

		/* If we're here, then things are most likely
		 * completely hosed, but if the error condition
		 * is detected, it won't hurt to give it another
		 * try...ICH_FLASH_CYCLE_REPEAT_COUNT times.
		 */
		hsfsts.regval = E1000_READ_FLASH_REG16(hw, ICH_FLASH_HSFSTS);
		if (hsfsts.hsf_status.flcerr)
			/* Repeat for some time before giving up. */
			continue;
		if (!hsfsts.hsf_status.flcdone) {
			DEBUGOUT("Timeout error - flash cycle did not complete.\n");
			break;
		}
	} while (count++ < ICH_FLASH_CYCLE_REPEAT_COUNT);

	return ret_val;
}

/**
*  e1000_write_flash_data32_ich8lan - Writes 4 bytes to the NVM
*  @hw: pointer to the HW structure
*  @offset: The offset (in bytes) of the dwords to read.
*  @data: The 4 bytes to write to the NVM.
*
*  Writes one/two/four bytes to the NVM using the flash access registers.
**/
static s32 e1000_write_flash_data32_ich8lan(struct e1000_hw *hw, u32 offset,
					    u32 data)
{
	union ich8_hws_flash_status hsfsts;
	union ich8_hws_flash_ctrl hsflctl;
	u32 flash_linear_addr;
	s32 ret_val;
	u8 count = 0;

	DEBUGFUNC("e1000_write_flash_data32_ich8lan");

	if (hw->mac.type >= e1000_pch_spt) {
		if (offset > ICH_FLASH_LINEAR_ADDR_MASK)
			return -E1000_ERR_NVM;
	}
	flash_linear_addr = ((ICH_FLASH_LINEAR_ADDR_MASK & offset) +
			     hw->nvm.flash_base_addr);
	do {
		usec_delay(1);
		/* Steps */
		ret_val = e1000_flash_cycle_init_ich8lan(hw);
		if (ret_val != E1000_SUCCESS)
			break;

		/* In SPT, This register is in Lan memory space, not
		 * flash.  Therefore, only 32 bit access is supported
		 */
		if (hw->mac.type >= e1000_pch_spt)
			hsflctl.regval = E1000_READ_FLASH_REG(hw,
							      ICH_FLASH_HSFSTS)
					 >> 16;
		else
			hsflctl.regval = E1000_READ_FLASH_REG16(hw,
							      ICH_FLASH_HSFCTL);

		hsflctl.hsf_ctrl.fldbcount = sizeof(u32) - 1;
		hsflctl.hsf_ctrl.flcycle = ICH_CYCLE_WRITE;

		/* In SPT, This register is in Lan memory space,
		 * not flash.  Therefore, only 32 bit access is
		 * supported
		 */
		if (hw->mac.type >= e1000_pch_spt)
			E1000_WRITE_FLASH_REG(hw, ICH_FLASH_HSFSTS,
					      hsflctl.regval << 16);
		else
			E1000_WRITE_FLASH_REG16(hw, ICH_FLASH_HSFCTL,
						hsflctl.regval);

		E1000_WRITE_FLASH_REG(hw, ICH_FLASH_FADDR, flash_linear_addr);

		E1000_WRITE_FLASH_REG(hw, ICH_FLASH_FDATA0, data);

		/* check if FCERR is set to 1 , if set to 1, clear it
		 * and try the whole sequence a few more times else done
		 */
		ret_val = e1000_flash_cycle_ich8lan(hw,
					       ICH_FLASH_WRITE_COMMAND_TIMEOUT);

		if (ret_val == E1000_SUCCESS)
			break;

		/* If we're here, then things are most likely
		 * completely hosed, but if the error condition
		 * is detected, it won't hurt to give it another
		 * try...ICH_FLASH_CYCLE_REPEAT_COUNT times.
		 */
		hsfsts.regval = E1000_READ_FLASH_REG16(hw, ICH_FLASH_HSFSTS);

		if (hsfsts.hsf_status.flcerr)
			/* Repeat for some time before giving up. */
			continue;
		if (!hsfsts.hsf_status.flcdone) {
			DEBUGOUT("Timeout error - flash cycle did not complete.\n");
			break;
		}
	} while (count++ < ICH_FLASH_CYCLE_REPEAT_COUNT);

	return ret_val;
}

/**
 *  e1000_write_flash_byte_ich8lan - Write a single byte to NVM
 *  @hw: pointer to the HW structure
 *  @offset: The index of the byte to read.
 *  @data: The byte to write to the NVM.
 *
 *  Writes a single byte to the NVM using the flash access registers.
 **/
static s32 e1000_write_flash_byte_ich8lan(struct e1000_hw *hw, u32 offset,
					  u8 data)
{
	u16 word = (u16)data;

	DEBUGFUNC("e1000_write_flash_byte_ich8lan");

	return e1000_write_flash_data_ich8lan(hw, offset, 1, word);
}

/**
*  e1000_retry_write_flash_dword_ich8lan - Writes a dword to NVM
*  @hw: pointer to the HW structure
*  @offset: The offset of the word to write.
*  @dword: The dword to write to the NVM.
*
*  Writes a single dword to the NVM using the flash access registers.
*  Goes through a retry algorithm before giving up.
**/
static s32 e1000_retry_write_flash_dword_ich8lan(struct e1000_hw *hw,
						 u32 offset, u32 dword)
{
	s32 ret_val;
	u16 program_retries;

	DEBUGFUNC("e1000_retry_write_flash_dword_ich8lan");

	/* Must convert word offset into bytes. */
	offset <<= 1;

	ret_val = e1000_write_flash_data32_ich8lan(hw, offset, dword);

	if (!ret_val)
		return ret_val;
	for (program_retries = 0; program_retries < 100; program_retries++) {
		DEBUGOUT2("Retrying Byte %8.8X at offset %u\n", dword, offset);
		usec_delay(100);
		ret_val = e1000_write_flash_data32_ich8lan(hw, offset, dword);
		if (ret_val == E1000_SUCCESS)
			break;
	}
	if (program_retries == 100)
		return -E1000_ERR_NVM;

	return E1000_SUCCESS;
}

/**
 *  e1000_retry_write_flash_byte_ich8lan - Writes a single byte to NVM
 *  @hw: pointer to the HW structure
 *  @offset: The offset of the byte to write.
 *  @byte: The byte to write to the NVM.
 *
 *  Writes a single byte to the NVM using the flash access registers.
 *  Goes through a retry algorithm before giving up.
 **/
static s32 e1000_retry_write_flash_byte_ich8lan(struct e1000_hw *hw,
						u32 offset, u8 byte)
{
	s32 ret_val;
	u16 program_retries;

	DEBUGFUNC("e1000_retry_write_flash_byte_ich8lan");

	ret_val = e1000_write_flash_byte_ich8lan(hw, offset, byte);
	if (!ret_val)
		return ret_val;

	for (program_retries = 0; program_retries < 100; program_retries++) {
		DEBUGOUT2("Retrying Byte %2.2X at offset %u\n", byte, offset);
		usec_delay(100);
		ret_val = e1000_write_flash_byte_ich8lan(hw, offset, byte);
		if (ret_val == E1000_SUCCESS)
			break;
	}
	if (program_retries == 100)
		return -E1000_ERR_NVM;

	return E1000_SUCCESS;
}

/**
 *  e1000_erase_flash_bank_ich8lan - Erase a bank (4k) from NVM
 *  @hw: pointer to the HW structure
 *  @bank: 0 for first bank, 1 for second bank, etc.
 *
 *  Erases the bank specified. Each bank is a 4k block. Banks are 0 based.
 *  bank N is 4096 * N + flash_reg_addr.
 **/
static s32 e1000_erase_flash_bank_ich8lan(struct e1000_hw *hw, u32 bank)
{
	struct e1000_nvm_info *nvm = &hw->nvm;
	union ich8_hws_flash_status hsfsts;
	union ich8_hws_flash_ctrl hsflctl;
	u32 flash_linear_addr;
	/* bank size is in 16bit words - adjust to bytes */
	u32 flash_bank_size = nvm->flash_bank_size * 2;
	s32 ret_val;
	s32 count = 0;
	s32 j, iteration, sector_size;

	DEBUGFUNC("e1000_erase_flash_bank_ich8lan");

	hsfsts.regval = E1000_READ_FLASH_REG16(hw, ICH_FLASH_HSFSTS);

	/* Determine HW Sector size: Read BERASE bits of hw flash status
	 * register
	 * 00: The Hw sector is 256 bytes, hence we need to erase 16
	 *     consecutive sectors.  The start index for the nth Hw sector
	 *     can be calculated as = bank * 4096 + n * 256
	 * 01: The Hw sector is 4K bytes, hence we need to erase 1 sector.
	 *     The start index for the nth Hw sector can be calculated
	 *     as = bank * 4096
	 * 10: The Hw sector is 8K bytes, nth sector = bank * 8192
	 *     (ich9 only, otherwise error condition)
	 * 11: The Hw sector is 64K bytes, nth sector = bank * 65536
	 */
	switch (hsfsts.hsf_status.berasesz) {
	case 0:
		/* Hw sector size 256 */
		sector_size = ICH_FLASH_SEG_SIZE_256;
		iteration = flash_bank_size / ICH_FLASH_SEG_SIZE_256;
		break;
	case 1:
		sector_size = ICH_FLASH_SEG_SIZE_4K;
		iteration = 1;
		break;
	case 2:
		sector_size = ICH_FLASH_SEG_SIZE_8K;
		iteration = 1;
		break;
	case 3:
		sector_size = ICH_FLASH_SEG_SIZE_64K;
		iteration = 1;
		break;
	default:
		return -E1000_ERR_NVM;
	}

	/* Start with the base address, then add the sector offset. */
	flash_linear_addr = hw->nvm.flash_base_addr;
	flash_linear_addr += (bank) ? flash_bank_size : 0;

	for (j = 0; j < iteration; j++) {
		do {
			u32 timeout = ICH_FLASH_ERASE_COMMAND_TIMEOUT;

			/* Steps */
			ret_val = e1000_flash_cycle_init_ich8lan(hw);
			if (ret_val)
				return ret_val;

			/* Write a value 11 (block Erase) in Flash
			 * Cycle field in hw flash control
			 */
			if (hw->mac.type >= e1000_pch_spt)
				hsflctl.regval =
				    E1000_READ_FLASH_REG(hw,
							 ICH_FLASH_HSFSTS)>>16;
			else
				hsflctl.regval =
				    E1000_READ_FLASH_REG16(hw,
							   ICH_FLASH_HSFCTL);

			hsflctl.hsf_ctrl.flcycle = ICH_CYCLE_ERASE;
			if (hw->mac.type >= e1000_pch_spt)
				E1000_WRITE_FLASH_REG(hw, ICH_FLASH_HSFSTS,
						      hsflctl.regval << 16);
			else
				E1000_WRITE_FLASH_REG16(hw, ICH_FLASH_HSFCTL,
							hsflctl.regval);

			/* Write the last 24 bits of an index within the
			 * block into Flash Linear address field in Flash
			 * Address.
			 */
			flash_linear_addr += (j * sector_size);
			E1000_WRITE_FLASH_REG(hw, ICH_FLASH_FADDR,
					      flash_linear_addr);

			ret_val = e1000_flash_cycle_ich8lan(hw, timeout);
			if (ret_val == E1000_SUCCESS)
				break;

			/* Check if FCERR is set to 1.  If 1,
			 * clear it and try the whole sequence
			 * a few more times else Done
			 */
			hsfsts.regval = E1000_READ_FLASH_REG16(hw,
						      ICH_FLASH_HSFSTS);
			if (hsfsts.hsf_status.flcerr)
				/* repeat for some time before giving up */
				continue;
			else if (!hsfsts.hsf_status.flcdone)
				return ret_val;
		} while (++count < ICH_FLASH_CYCLE_REPEAT_COUNT);
	}

	return E1000_SUCCESS;
}

/**
 *  e1000_valid_led_default_ich8lan - Set the default LED settings
 *  @hw: pointer to the HW structure
 *  @data: Pointer to the LED settings
 *
 *  Reads the LED default settings from the NVM to data.  If the NVM LED
 *  settings is all 0's or F's, set the LED default to a valid LED default
 *  setting.
 **/
static s32 e1000_valid_led_default_ich8lan(struct e1000_hw *hw, u16 *data)
{
	s32 ret_val;

	DEBUGFUNC("e1000_valid_led_default_ich8lan");

	ret_val = hw->nvm.ops.read(hw, NVM_ID_LED_SETTINGS, 1, data);
	if (ret_val) {
		DEBUGOUT("NVM Read Error\n");
		return ret_val;
	}

	if (*data == ID_LED_RESERVED_0000 || *data == ID_LED_RESERVED_FFFF)
		*data = ID_LED_DEFAULT_ICH8LAN;

	return E1000_SUCCESS;
}

/**
 *  e1000_id_led_init_pchlan - store LED configurations
 *  @hw: pointer to the HW structure
 *
 *  PCH does not control LEDs via the LEDCTL register, rather it uses
 *  the PHY LED configuration register.
 *
 *  PCH also does not have an "always on" or "always off" mode which
 *  complicates the ID feature.  Instead of using the "on" mode to indicate
 *  in ledctl_mode2 the LEDs to use for ID (see e1000_id_led_init_generic()),
 *  use "link_up" mode.  The LEDs will still ID on request if there is no
 *  link based on logic in e1000_led_[on|off]_pchlan().
 **/
static s32 e1000_id_led_init_pchlan(struct e1000_hw *hw)
{
	struct e1000_mac_info *mac = &hw->mac;
	s32 ret_val;
	const u32 ledctl_on = E1000_LEDCTL_MODE_LINK_UP;
	const u32 ledctl_off = E1000_LEDCTL_MODE_LINK_UP | E1000_PHY_LED0_IVRT;
	u16 data, i, temp, shift;

	DEBUGFUNC("e1000_id_led_init_pchlan");

	/* Get default ID LED modes */
	ret_val = hw->nvm.ops.valid_led_default(hw, &data);
	if (ret_val)
		return ret_val;

	mac->ledctl_default = E1000_READ_REG(hw, E1000_LEDCTL);
	mac->ledctl_mode1 = mac->ledctl_default;
	mac->ledctl_mode2 = mac->ledctl_default;

	for (i = 0; i < 4; i++) {
		temp = (data >> (i << 2)) & E1000_LEDCTL_LED0_MODE_MASK;
		shift = (i * 5);
		switch (temp) {
		case ID_LED_ON1_DEF2:
		case ID_LED_ON1_ON2:
		case ID_LED_ON1_OFF2:
			mac->ledctl_mode1 &= ~(E1000_PHY_LED0_MASK << shift);
			mac->ledctl_mode1 |= (ledctl_on << shift);
			break;
		case ID_LED_OFF1_DEF2:
		case ID_LED_OFF1_ON2:
		case ID_LED_OFF1_OFF2:
			mac->ledctl_mode1 &= ~(E1000_PHY_LED0_MASK << shift);
			mac->ledctl_mode1 |= (ledctl_off << shift);
			break;
		default:
			/* Do nothing */
			break;
		}
		switch (temp) {
		case ID_LED_DEF1_ON2:
		case ID_LED_ON1_ON2:
		case ID_LED_OFF1_ON2:
			mac->ledctl_mode2 &= ~(E1000_PHY_LED0_MASK << shift);
			mac->ledctl_mode2 |= (ledctl_on << shift);
			break;
		case ID_LED_DEF1_OFF2:
		case ID_LED_ON1_OFF2:
		case ID_LED_OFF1_OFF2:
			mac->ledctl_mode2 &= ~(E1000_PHY_LED0_MASK << shift);
			mac->ledctl_mode2 |= (ledctl_off << shift);
			break;
		default:
			/* Do nothing */
			break;
		}
	}

	return E1000_SUCCESS;
}

/**
 *  e1000_get_bus_info_ich8lan - Get/Set the bus type and width
 *  @hw: pointer to the HW structure
 *
 *  ICH8 use the PCI Express bus, but does not contain a PCI Express Capability
 *  register, so the bus width is hard coded.
 **/
static s32 e1000_get_bus_info_ich8lan(struct e1000_hw *hw)
{
	struct e1000_bus_info *bus = &hw->bus;
	s32 ret_val;

	DEBUGFUNC("e1000_get_bus_info_ich8lan");

	ret_val = e1000_get_bus_info_pcie_generic(hw);

	/* ICH devices are "PCI Express"-ish.  They have
	 * a configuration space, but do not contain
	 * PCI Express Capability registers, so bus width
	 * must be hardcoded.
	 */
	if (bus->width == e1000_bus_width_unknown)
		bus->width = e1000_bus_width_pcie_x1;

	return ret_val;
}

/**
 *  e1000_reset_hw_ich8lan - Reset the hardware
 *  @hw: pointer to the HW structure
 *
 *  Does a full reset of the hardware which includes a reset of the PHY and
 *  MAC.
 **/
static s32 e1000_reset_hw_ich8lan(struct e1000_hw *hw)
{
	struct e1000_dev_spec_ich8lan *dev_spec = &hw->dev_spec.ich8lan;
	u16 kum_cfg;
	u32 ctrl, reg;
	s32 ret_val;

	DEBUGFUNC("e1000_reset_hw_ich8lan");

	/* Prevent the PCI-E bus from sticking if there is no TLP connection
	 * on the last TLP read/write transaction when MAC is reset.
	 */
	ret_val = e1000_disable_pcie_master_generic(hw);
	if (ret_val)
		DEBUGOUT("PCI-E Master disable polling has failed.\n");

	DEBUGOUT("Masking off all interrupts\n");
	E1000_WRITE_REG(hw, E1000_IMC, 0xffffffff);

	/* Disable the Transmit and Receive units.  Then delay to allow
	 * any pending transactions to complete before we hit the MAC
	 * with the global reset.
	 */
	E1000_WRITE_REG(hw, E1000_RCTL, 0);
	E1000_WRITE_REG(hw, E1000_TCTL, E1000_TCTL_PSP);
	E1000_WRITE_FLUSH(hw);

	msec_delay(10);

	/* Workaround for ICH8 bit corruption issue in FIFO memory */
	if (hw->mac.type == e1000_ich8lan) {
		/* Set Tx and Rx buffer allocation to 8k apiece. */
		E1000_WRITE_REG(hw, E1000_PBA, E1000_PBA_8K);
		/* Set Packet Buffer Size to 16k. */
		E1000_WRITE_REG(hw, E1000_PBS, E1000_PBS_16K);
	}

	if (hw->mac.type == e1000_pchlan) {
		/* Save the NVM K1 bit setting*/
		ret_val = e1000_read_nvm(hw, E1000_NVM_K1_CONFIG, 1, &kum_cfg);
		if (ret_val)
			return ret_val;

		if (kum_cfg & E1000_NVM_K1_ENABLE)
			dev_spec->nvm_k1_enabled = TRUE;
		else
			dev_spec->nvm_k1_enabled = FALSE;
	}

	ctrl = E1000_READ_REG(hw, E1000_CTRL);

	if (!hw->phy.ops.check_reset_block(hw)) {
		/* Full-chip reset requires MAC and PHY reset at the same
		 * time to make sure the interface between MAC and the
		 * external PHY is reset.
		 */
		ctrl |= E1000_CTRL_PHY_RST;

		/* Gate automatic PHY configuration by hardware on
		 * non-managed 82579
		 */
		if ((hw->mac.type == e1000_pch2lan) &&
		    !(E1000_READ_REG(hw, E1000_FWSM) & E1000_ICH_FWSM_FW_VALID))
			e1000_gate_hw_phy_config_ich8lan(hw, TRUE);
	}
	ret_val = e1000_acquire_swflag_ich8lan(hw);
	DEBUGOUT("Issuing a global reset to ich8lan\n");
	E1000_WRITE_REG(hw, E1000_CTRL, (ctrl | E1000_CTRL_RST));
	/* cannot issue a flush here because it hangs the hardware */
	msec_delay(20);

	/* Set Phy Config Counter to 50msec */
	if (hw->mac.type == e1000_pch2lan) {
		reg = E1000_READ_REG(hw, E1000_FEXTNVM3);
		reg &= ~E1000_FEXTNVM3_PHY_CFG_COUNTER_MASK;
		reg |= E1000_FEXTNVM3_PHY_CFG_COUNTER_50MSEC;
		E1000_WRITE_REG(hw, E1000_FEXTNVM3, reg);
	}


	if (ctrl & E1000_CTRL_PHY_RST) {
		ret_val = hw->phy.ops.get_cfg_done(hw);
		if (ret_val)
			return ret_val;

		ret_val = e1000_post_phy_reset_ich8lan(hw);
		if (ret_val)
			return ret_val;
	}

	/* For PCH, this write will make sure that any noise
	 * will be detected as a CRC error and be dropped rather than show up
	 * as a bad packet to the DMA engine.
	 */
	if (hw->mac.type == e1000_pchlan)
		E1000_WRITE_REG(hw, E1000_CRC_OFFSET, 0x65656565);

	E1000_WRITE_REG(hw, E1000_IMC, 0xffffffff);
	E1000_READ_REG(hw, E1000_ICR);

	reg = E1000_READ_REG(hw, E1000_KABGTXD);
	reg |= E1000_KABGTXD_BGSQLBIAS;
	E1000_WRITE_REG(hw, E1000_KABGTXD, reg);

	return E1000_SUCCESS;
}

/**
 *  e1000_init_hw_ich8lan - Initialize the hardware
 *  @hw: pointer to the HW structure
 *
 *  Prepares the hardware for transmit and receive by doing the following:
 *   - initialize hardware bits
 *   - initialize LED identification
 *   - setup receive address registers
 *   - setup flow control
 *   - setup transmit descriptors
 *   - clear statistics
 **/
static s32 e1000_init_hw_ich8lan(struct e1000_hw *hw)
{
	struct e1000_mac_info *mac = &hw->mac;
	u32 ctrl_ext, txdctl, snoop;
	s32 ret_val;
	u16 i;

	DEBUGFUNC("e1000_init_hw_ich8lan");

	e1000_initialize_hw_bits_ich8lan(hw);

	/* Initialize identification LED */
	ret_val = mac->ops.id_led_init(hw);
	/* An error is not fatal and we should not stop init due to this */
	if (ret_val)
		DEBUGOUT("Error initializing identification LED\n");

	/* Setup the receive address. */
	e1000_init_rx_addrs_generic(hw, mac->rar_entry_count);

	/* Zero out the Multicast HASH table */
	DEBUGOUT("Zeroing the MTA\n");
	for (i = 0; i < mac->mta_reg_count; i++)
		E1000_WRITE_REG_ARRAY(hw, E1000_MTA, i, 0);

	/* The 82578 Rx buffer will stall if wakeup is enabled in host and
	 * the ME.  Disable wakeup by clearing the host wakeup bit.
	 * Reset the phy after disabling host wakeup to reset the Rx buffer.
	 */
	if (hw->phy.type == e1000_phy_82578) {
		hw->phy.ops.read_reg(hw, BM_PORT_GEN_CFG, &i);
		i &= ~BM_WUC_HOST_WU_BIT;
		hw->phy.ops.write_reg(hw, BM_PORT_GEN_CFG, i);
		ret_val = e1000_phy_hw_reset_ich8lan(hw);
		if (ret_val)
			return ret_val;
	}

	/* Setup link and flow control */
	ret_val = mac->ops.setup_link(hw);

	/* Set the transmit descriptor write-back policy for both queues */
	txdctl = E1000_READ_REG(hw, E1000_TXDCTL(0));
	txdctl = ((txdctl & ~E1000_TXDCTL_WTHRESH) |
		  E1000_TXDCTL_FULL_TX_DESC_WB);
	txdctl = ((txdctl & ~E1000_TXDCTL_PTHRESH) |
		  E1000_TXDCTL_MAX_TX_DESC_PREFETCH);
	E1000_WRITE_REG(hw, E1000_TXDCTL(0), txdctl);
	txdctl = E1000_READ_REG(hw, E1000_TXDCTL(1));
	txdctl = ((txdctl & ~E1000_TXDCTL_WTHRESH) |
		  E1000_TXDCTL_FULL_TX_DESC_WB);
	txdctl = ((txdctl & ~E1000_TXDCTL_PTHRESH) |
		  E1000_TXDCTL_MAX_TX_DESC_PREFETCH);
	E1000_WRITE_REG(hw, E1000_TXDCTL(1), txdctl);

	/* ICH8 has opposite polarity of no_snoop bits.
	 * By default, we should use snoop behavior.
	 */
	if (mac->type == e1000_ich8lan)
		snoop = PCIE_ICH8_SNOOP_ALL;
	else
		snoop = (u32) ~(PCIE_NO_SNOOP_ALL);
	e1000_set_pcie_no_snoop_generic(hw, snoop);

	ctrl_ext = E1000_READ_REG(hw, E1000_CTRL_EXT);
	ctrl_ext |= E1000_CTRL_EXT_RO_DIS;
	E1000_WRITE_REG(hw, E1000_CTRL_EXT, ctrl_ext);

	/* Clear all of the statistics registers (clear on read).  It is
	 * important that we do this after we have tried to establish link
	 * because the symbol error count will increment wildly if there
	 * is no link.
	 */
	e1000_clear_hw_cntrs_ich8lan(hw);

	return ret_val;
}

/**
 *  e1000_initialize_hw_bits_ich8lan - Initialize required hardware bits
 *  @hw: pointer to the HW structure
 *
 *  Sets/Clears required hardware bits necessary for correctly setting up the
 *  hardware for transmit and receive.
 **/
static void e1000_initialize_hw_bits_ich8lan(struct e1000_hw *hw)
{
	u32 reg;

	DEBUGFUNC("e1000_initialize_hw_bits_ich8lan");

	/* Extended Device Control */
	reg = E1000_READ_REG(hw, E1000_CTRL_EXT);
	reg |= (1 << 22);
	/* Enable PHY low-power state when MAC is at D3 w/o WoL */
	if (hw->mac.type >= e1000_pchlan)
		reg |= E1000_CTRL_EXT_PHYPDEN;
	E1000_WRITE_REG(hw, E1000_CTRL_EXT, reg);

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
	if (hw->mac.type == e1000_ich8lan)
		reg |= (1 << 28) | (1 << 29);
	reg |= (1 << 23) | (1 << 24) | (1 << 26) | (1 << 27);
	E1000_WRITE_REG(hw, E1000_TARC(0), reg);

	/* Transmit Arbitration Control 1 */
	reg = E1000_READ_REG(hw, E1000_TARC(1));
	if (E1000_READ_REG(hw, E1000_TCTL) & E1000_TCTL_MULR)
		reg &= ~(1 << 28);
	else
		reg |= (1 << 28);
	reg |= (1 << 24) | (1 << 26) | (1 << 30);
	E1000_WRITE_REG(hw, E1000_TARC(1), reg);

	/* Device Status */
	if (hw->mac.type == e1000_ich8lan) {
		reg = E1000_READ_REG(hw, E1000_STATUS);
		reg &= ~(1U << 31);
		E1000_WRITE_REG(hw, E1000_STATUS, reg);
	}

	/* work-around descriptor data corruption issue during nfs v2 udp
	 * traffic, just disable the nfs filtering capability
	 */
	reg = E1000_READ_REG(hw, E1000_RFCTL);
	reg |= (E1000_RFCTL_NFSW_DIS | E1000_RFCTL_NFSR_DIS);

	/* Disable IPv6 extension header parsing because some malformed
	 * IPv6 headers can hang the Rx.
	 */
	if (hw->mac.type == e1000_ich8lan)
		reg |= (E1000_RFCTL_IPV6_EX_DIS | E1000_RFCTL_NEW_IPV6_EXT_DIS);
	E1000_WRITE_REG(hw, E1000_RFCTL, reg);

	/* Enable ECC on Lynxpoint */
	if (hw->mac.type >= e1000_pch_lpt) {
		reg = E1000_READ_REG(hw, E1000_PBECCSTS);
		reg |= E1000_PBECCSTS_ECC_ENABLE;
		E1000_WRITE_REG(hw, E1000_PBECCSTS, reg);

		reg = E1000_READ_REG(hw, E1000_CTRL);
		reg |= E1000_CTRL_MEHE;
		E1000_WRITE_REG(hw, E1000_CTRL, reg);
	}

	return;
}

/**
 *  e1000_setup_link_ich8lan - Setup flow control and link settings
 *  @hw: pointer to the HW structure
 *
 *  Determines which flow control settings to use, then configures flow
 *  control.  Calls the appropriate media-specific link configuration
 *  function.  Assuming the adapter has a valid link partner, a valid link
 *  should be established.  Assumes the hardware has previously been reset
 *  and the transmitter and receiver are not enabled.
 **/
static s32 e1000_setup_link_ich8lan(struct e1000_hw *hw)
{
	s32 ret_val;

	DEBUGFUNC("e1000_setup_link_ich8lan");

	if (hw->phy.ops.check_reset_block(hw))
		return E1000_SUCCESS;

	/* ICH parts do not have a word in the NVM to determine
	 * the default flow control setting, so we explicitly
	 * set it to full.
	 */
	if (hw->fc.requested_mode == e1000_fc_default)
		hw->fc.requested_mode = e1000_fc_full;

	/* Save off the requested flow control mode for use later.  Depending
	 * on the link partner's capabilities, we may or may not use this mode.
	 */
	hw->fc.current_mode = hw->fc.requested_mode;

	DEBUGOUT1("After fix-ups FlowControl is now = %x\n",
		hw->fc.current_mode);

	/* Continue to configure the copper link. */
	ret_val = hw->mac.ops.setup_physical_interface(hw);
	if (ret_val)
		return ret_val;

	E1000_WRITE_REG(hw, E1000_FCTTV, hw->fc.pause_time);
	if ((hw->phy.type == e1000_phy_82578) ||
	    (hw->phy.type == e1000_phy_82579) ||
	    (hw->phy.type == e1000_phy_i217) ||
	    (hw->phy.type == e1000_phy_82577)) {
		E1000_WRITE_REG(hw, E1000_FCRTV_PCH, hw->fc.refresh_time);

		ret_val = hw->phy.ops.write_reg(hw,
					     PHY_REG(BM_PORT_CTRL_PAGE, 27),
					     hw->fc.pause_time);
		if (ret_val)
			return ret_val;
	}

	return e1000_set_fc_watermarks_generic(hw);
}

/**
 *  e1000_setup_copper_link_ich8lan - Configure MAC/PHY interface
 *  @hw: pointer to the HW structure
 *
 *  Configures the kumeran interface to the PHY to wait the appropriate time
 *  when polling the PHY, then call the generic setup_copper_link to finish
 *  configuring the copper link.
 **/
static s32 e1000_setup_copper_link_ich8lan(struct e1000_hw *hw)
{
	u32 ctrl;
	s32 ret_val;
	u16 reg_data;

	DEBUGFUNC("e1000_setup_copper_link_ich8lan");

	ctrl = E1000_READ_REG(hw, E1000_CTRL);
	ctrl |= E1000_CTRL_SLU;
	ctrl &= ~(E1000_CTRL_FRCSPD | E1000_CTRL_FRCDPX);
	E1000_WRITE_REG(hw, E1000_CTRL, ctrl);

	/* Set the mac to wait the maximum time between each iteration
	 * and increase the max iterations when polling the phy;
	 * this fixes erroneous timeouts at 10Mbps.
	 */
	ret_val = e1000_write_kmrn_reg_generic(hw, E1000_KMRNCTRLSTA_TIMEOUTS,
					       0xFFFF);
	if (ret_val)
		return ret_val;
	ret_val = e1000_read_kmrn_reg_generic(hw,
					      E1000_KMRNCTRLSTA_INBAND_PARAM,
					      &reg_data);
	if (ret_val)
		return ret_val;
	reg_data |= 0x3F;
	ret_val = e1000_write_kmrn_reg_generic(hw,
					       E1000_KMRNCTRLSTA_INBAND_PARAM,
					       reg_data);
	if (ret_val)
		return ret_val;

	switch (hw->phy.type) {
	case e1000_phy_igp_3:
		ret_val = e1000_copper_link_setup_igp(hw);
		if (ret_val)
			return ret_val;
		break;
	case e1000_phy_bm:
	case e1000_phy_82578:
		ret_val = e1000_copper_link_setup_m88(hw);
		if (ret_val)
			return ret_val;
		break;
	case e1000_phy_82577:
	case e1000_phy_82579:
		ret_val = e1000_copper_link_setup_82577(hw);
		if (ret_val)
			return ret_val;
		break;
	case e1000_phy_ife:
		ret_val = hw->phy.ops.read_reg(hw, IFE_PHY_MDIX_CONTROL,
					       &reg_data);
		if (ret_val)
			return ret_val;

		reg_data &= ~IFE_PMC_AUTO_MDIX;

		switch (hw->phy.mdix) {
		case 1:
			reg_data &= ~IFE_PMC_FORCE_MDIX;
			break;
		case 2:
			reg_data |= IFE_PMC_FORCE_MDIX;
			break;
		case 0:
		default:
			reg_data |= IFE_PMC_AUTO_MDIX;
			break;
		}
		ret_val = hw->phy.ops.write_reg(hw, IFE_PHY_MDIX_CONTROL,
						reg_data);
		if (ret_val)
			return ret_val;
		break;
	default:
		break;
	}

	return e1000_setup_copper_link_generic(hw);
}

/**
 *  e1000_setup_copper_link_pch_lpt - Configure MAC/PHY interface
 *  @hw: pointer to the HW structure
 *
 *  Calls the PHY specific link setup function and then calls the
 *  generic setup_copper_link to finish configuring the link for
 *  Lynxpoint PCH devices
 **/
static s32 e1000_setup_copper_link_pch_lpt(struct e1000_hw *hw)
{
	u32 ctrl;
	s32 ret_val;

	DEBUGFUNC("e1000_setup_copper_link_pch_lpt");

	ctrl = E1000_READ_REG(hw, E1000_CTRL);
	ctrl |= E1000_CTRL_SLU;
	ctrl &= ~(E1000_CTRL_FRCSPD | E1000_CTRL_FRCDPX);
	E1000_WRITE_REG(hw, E1000_CTRL, ctrl);

	ret_val = e1000_copper_link_setup_82577(hw);
	if (ret_val)
		return ret_val;

	return e1000_setup_copper_link_generic(hw);
}

/**
 *  e1000_get_link_up_info_ich8lan - Get current link speed and duplex
 *  @hw: pointer to the HW structure
 *  @speed: pointer to store current link speed
 *  @duplex: pointer to store the current link duplex
 *
 *  Calls the generic get_speed_and_duplex to retrieve the current link
 *  information and then calls the Kumeran lock loss workaround for links at
 *  gigabit speeds.
 **/
static s32 e1000_get_link_up_info_ich8lan(struct e1000_hw *hw, u16 *speed,
					  u16 *duplex)
{
	s32 ret_val;

	DEBUGFUNC("e1000_get_link_up_info_ich8lan");

	ret_val = e1000_get_speed_and_duplex_copper_generic(hw, speed, duplex);
	if (ret_val)
		return ret_val;

	if ((hw->mac.type == e1000_ich8lan) &&
	    (hw->phy.type == e1000_phy_igp_3) &&
	    (*speed == SPEED_1000)) {
		ret_val = e1000_kmrn_lock_loss_workaround_ich8lan(hw);
	}

	return ret_val;
}

/**
 *  e1000_kmrn_lock_loss_workaround_ich8lan - Kumeran workaround
 *  @hw: pointer to the HW structure
 *
 *  Work-around for 82566 Kumeran PCS lock loss:
 *  On link status change (i.e. PCI reset, speed change) and link is up and
 *  speed is gigabit-
 *    0) if workaround is optionally disabled do nothing
 *    1) wait 1ms for Kumeran link to come up
 *    2) check Kumeran Diagnostic register PCS lock loss bit
 *    3) if not set the link is locked (all is good), otherwise...
 *    4) reset the PHY
 *    5) repeat up to 10 times
 *  Note: this is only called for IGP3 copper when speed is 1gb.
 **/
static s32 e1000_kmrn_lock_loss_workaround_ich8lan(struct e1000_hw *hw)
{
	struct e1000_dev_spec_ich8lan *dev_spec = &hw->dev_spec.ich8lan;
	u32 phy_ctrl;
	s32 ret_val;
	u16 i, data;
	bool link;

	DEBUGFUNC("e1000_kmrn_lock_loss_workaround_ich8lan");

	if (!dev_spec->kmrn_lock_loss_workaround_enabled)
		return E1000_SUCCESS;

	/* Make sure link is up before proceeding.  If not just return.
	 * Attempting this while link is negotiating fouled up link
	 * stability
	 */
	ret_val = e1000_phy_has_link_generic(hw, 1, 0, &link);
	if (!link)
		return E1000_SUCCESS;

	for (i = 0; i < 10; i++) {
		/* read once to clear */
		ret_val = hw->phy.ops.read_reg(hw, IGP3_KMRN_DIAG, &data);
		if (ret_val)
			return ret_val;
		/* and again to get new status */
		ret_val = hw->phy.ops.read_reg(hw, IGP3_KMRN_DIAG, &data);
		if (ret_val)
			return ret_val;

		/* check for PCS lock */
		if (!(data & IGP3_KMRN_DIAG_PCS_LOCK_LOSS))
			return E1000_SUCCESS;

		/* Issue PHY reset */
		hw->phy.ops.reset(hw);
		msec_delay_irq(5);
	}
	/* Disable GigE link negotiation */
	phy_ctrl = E1000_READ_REG(hw, E1000_PHY_CTRL);
	phy_ctrl |= (E1000_PHY_CTRL_GBE_DISABLE |
		     E1000_PHY_CTRL_NOND0A_GBE_DISABLE);
	E1000_WRITE_REG(hw, E1000_PHY_CTRL, phy_ctrl);

	/* Call gig speed drop workaround on Gig disable before accessing
	 * any PHY registers
	 */
	e1000_gig_downshift_workaround_ich8lan(hw);

	/* unable to acquire PCS lock */
	return -E1000_ERR_PHY;
}

/**
 *  e1000_set_kmrn_lock_loss_workaround_ich8lan - Set Kumeran workaround state
 *  @hw: pointer to the HW structure
 *  @state: boolean value used to set the current Kumeran workaround state
 *
 *  If ICH8, set the current Kumeran workaround state (enabled - TRUE
 *  /disabled - FALSE).
 **/
void e1000_set_kmrn_lock_loss_workaround_ich8lan(struct e1000_hw *hw,
						 bool state)
{
	struct e1000_dev_spec_ich8lan *dev_spec = &hw->dev_spec.ich8lan;

	DEBUGFUNC("e1000_set_kmrn_lock_loss_workaround_ich8lan");

	if (hw->mac.type != e1000_ich8lan) {
		DEBUGOUT("Workaround applies to ICH8 only.\n");
		return;
	}

	dev_spec->kmrn_lock_loss_workaround_enabled = state;

	return;
}

/**
 *  e1000_ipg3_phy_powerdown_workaround_ich8lan - Power down workaround on D3
 *  @hw: pointer to the HW structure
 *
 *  Workaround for 82566 power-down on D3 entry:
 *    1) disable gigabit link
 *    2) write VR power-down enable
 *    3) read it back
 *  Continue if successful, else issue LCD reset and repeat
 **/
void e1000_igp3_phy_powerdown_workaround_ich8lan(struct e1000_hw *hw)
{
	u32 reg;
	u16 data;
	u8  retry = 0;

	DEBUGFUNC("e1000_igp3_phy_powerdown_workaround_ich8lan");

	if (hw->phy.type != e1000_phy_igp_3)
		return;

	/* Try the workaround twice (if needed) */
	do {
		/* Disable link */
		reg = E1000_READ_REG(hw, E1000_PHY_CTRL);
		reg |= (E1000_PHY_CTRL_GBE_DISABLE |
			E1000_PHY_CTRL_NOND0A_GBE_DISABLE);
		E1000_WRITE_REG(hw, E1000_PHY_CTRL, reg);

		/* Call gig speed drop workaround on Gig disable before
		 * accessing any PHY registers
		 */
		if (hw->mac.type == e1000_ich8lan)
			e1000_gig_downshift_workaround_ich8lan(hw);

		/* Write VR power-down enable */
		hw->phy.ops.read_reg(hw, IGP3_VR_CTRL, &data);
		data &= ~IGP3_VR_CTRL_DEV_POWERDOWN_MODE_MASK;
		hw->phy.ops.write_reg(hw, IGP3_VR_CTRL,
				      data | IGP3_VR_CTRL_MODE_SHUTDOWN);

		/* Read it back and test */
		hw->phy.ops.read_reg(hw, IGP3_VR_CTRL, &data);
		data &= IGP3_VR_CTRL_DEV_POWERDOWN_MODE_MASK;
		if ((data == IGP3_VR_CTRL_MODE_SHUTDOWN) || retry)
			break;

		/* Issue PHY reset and repeat at most one more time */
		reg = E1000_READ_REG(hw, E1000_CTRL);
		E1000_WRITE_REG(hw, E1000_CTRL, reg | E1000_CTRL_PHY_RST);
		retry++;
	} while (retry);
}

/**
 *  e1000_gig_downshift_workaround_ich8lan - WoL from S5 stops working
 *  @hw: pointer to the HW structure
 *
 *  Steps to take when dropping from 1Gb/s (eg. link cable removal (LSC),
 *  LPLU, Gig disable, MDIC PHY reset):
 *    1) Set Kumeran Near-end loopback
 *    2) Clear Kumeran Near-end loopback
 *  Should only be called for ICH8[m] devices with any 1G Phy.
 **/
void e1000_gig_downshift_workaround_ich8lan(struct e1000_hw *hw)
{
	s32 ret_val;
	u16 reg_data;

	DEBUGFUNC("e1000_gig_downshift_workaround_ich8lan");

	if ((hw->mac.type != e1000_ich8lan) ||
	    (hw->phy.type == e1000_phy_ife))
		return;

	ret_val = e1000_read_kmrn_reg_generic(hw, E1000_KMRNCTRLSTA_DIAG_OFFSET,
					      &reg_data);
	if (ret_val)
		return;
	reg_data |= E1000_KMRNCTRLSTA_DIAG_NELPBK;
	ret_val = e1000_write_kmrn_reg_generic(hw,
					       E1000_KMRNCTRLSTA_DIAG_OFFSET,
					       reg_data);
	if (ret_val)
		return;
	reg_data &= ~E1000_KMRNCTRLSTA_DIAG_NELPBK;
	e1000_write_kmrn_reg_generic(hw, E1000_KMRNCTRLSTA_DIAG_OFFSET,
				     reg_data);
}

/**
 *  e1000_suspend_workarounds_ich8lan - workarounds needed during S0->Sx
 *  @hw: pointer to the HW structure
 *
 *  During S0 to Sx transition, it is possible the link remains at gig
 *  instead of negotiating to a lower speed.  Before going to Sx, set
 *  'Gig Disable' to force link speed negotiation to a lower speed based on
 *  the LPLU setting in the NVM or custom setting.  For PCH and newer parts,
 *  the OEM bits PHY register (LED, GbE disable and LPLU configurations) also
 *  needs to be written.
 *  Parts that support (and are linked to a partner which support) EEE in
 *  100Mbps should disable LPLU since 100Mbps w/ EEE requires less power
 *  than 10Mbps w/o EEE.
 **/
void e1000_suspend_workarounds_ich8lan(struct e1000_hw *hw)
{
	struct e1000_dev_spec_ich8lan *dev_spec = &hw->dev_spec.ich8lan;
	u32 phy_ctrl;
	s32 ret_val;

	DEBUGFUNC("e1000_suspend_workarounds_ich8lan");

	phy_ctrl = E1000_READ_REG(hw, E1000_PHY_CTRL);
	phy_ctrl |= E1000_PHY_CTRL_GBE_DISABLE;

	if (hw->phy.type == e1000_phy_i217) {
		u16 phy_reg, device_id = hw->device_id;

		if ((device_id == E1000_DEV_ID_PCH_LPTLP_I218_LM) ||
		    (device_id == E1000_DEV_ID_PCH_LPTLP_I218_V) ||
		    (device_id == E1000_DEV_ID_PCH_I218_LM3) ||
		    (device_id == E1000_DEV_ID_PCH_I218_V3) ||
		    (hw->mac.type >= e1000_pch_spt)) {
			u32 fextnvm6 = E1000_READ_REG(hw, E1000_FEXTNVM6);

			E1000_WRITE_REG(hw, E1000_FEXTNVM6,
					fextnvm6 & ~E1000_FEXTNVM6_REQ_PLL_CLK);
		}

		ret_val = hw->phy.ops.acquire(hw);
		if (ret_val)
			goto out;

		if (!dev_spec->eee_disable) {
			u16 eee_advert;

			ret_val =
			    e1000_read_emi_reg_locked(hw,
						      I217_EEE_ADVERTISEMENT,
						      &eee_advert);
			if (ret_val)
				goto release;

			/* Disable LPLU if both link partners support 100BaseT
			 * EEE and 100Full is advertised on both ends of the
			 * link, and enable Auto Enable LPI since there will
			 * be no driver to enable LPI while in Sx.
			 */
			if ((eee_advert & I82579_EEE_100_SUPPORTED) &&
			    (dev_spec->eee_lp_ability &
			     I82579_EEE_100_SUPPORTED) &&
			    (hw->phy.autoneg_advertised & ADVERTISE_100_FULL)) {
				phy_ctrl &= ~(E1000_PHY_CTRL_D0A_LPLU |
					      E1000_PHY_CTRL_NOND0A_LPLU);

				/* Set Auto Enable LPI after link up */
				hw->phy.ops.read_reg_locked(hw,
							    I217_LPI_GPIO_CTRL,
							    &phy_reg);
				phy_reg |= I217_LPI_GPIO_CTRL_AUTO_EN_LPI;
				hw->phy.ops.write_reg_locked(hw,
							     I217_LPI_GPIO_CTRL,
							     phy_reg);
			}
		}

		/* For i217 Intel Rapid Start Technology support,
		 * when the system is going into Sx and no manageability engine
		 * is present, the driver must configure proxy to reset only on
		 * power good.  LPI (Low Power Idle) state must also reset only
		 * on power good, as well as the MTA (Multicast table array).
		 * The SMBus release must also be disabled on LCD reset.
		 */
		if (!(E1000_READ_REG(hw, E1000_FWSM) &
		      E1000_ICH_FWSM_FW_VALID)) {
			/* Enable proxy to reset only on power good. */
			hw->phy.ops.read_reg_locked(hw, I217_PROXY_CTRL,
						    &phy_reg);
			phy_reg |= I217_PROXY_CTRL_AUTO_DISABLE;
			hw->phy.ops.write_reg_locked(hw, I217_PROXY_CTRL,
						     phy_reg);

			/* Set bit enable LPI (EEE) to reset only on
			 * power good.
			*/
			hw->phy.ops.read_reg_locked(hw, I217_SxCTRL, &phy_reg);
			phy_reg |= I217_SxCTRL_ENABLE_LPI_RESET;
			hw->phy.ops.write_reg_locked(hw, I217_SxCTRL, phy_reg);

			/* Disable the SMB release on LCD reset. */
			hw->phy.ops.read_reg_locked(hw, I217_MEMPWR, &phy_reg);
			phy_reg &= ~I217_MEMPWR_DISABLE_SMB_RELEASE;
			hw->phy.ops.write_reg_locked(hw, I217_MEMPWR, phy_reg);
		}

		/* Enable MTA to reset for Intel Rapid Start Technology
		 * Support
		 */
		hw->phy.ops.read_reg_locked(hw, I217_CGFREG, &phy_reg);
		phy_reg |= I217_CGFREG_ENABLE_MTA_RESET;
		hw->phy.ops.write_reg_locked(hw, I217_CGFREG, phy_reg);

release:
		hw->phy.ops.release(hw);
	}
out:
	E1000_WRITE_REG(hw, E1000_PHY_CTRL, phy_ctrl);

	if (hw->mac.type == e1000_ich8lan)
		e1000_gig_downshift_workaround_ich8lan(hw);

	if (hw->mac.type >= e1000_pchlan) {
		e1000_oem_bits_config_ich8lan(hw, FALSE);

		/* Reset PHY to activate OEM bits on 82577/8 */
		if (hw->mac.type == e1000_pchlan)
			e1000_phy_hw_reset_generic(hw);

		ret_val = hw->phy.ops.acquire(hw);
		if (ret_val)
			return;
		e1000_write_smbus_addr(hw);
		hw->phy.ops.release(hw);
	}

	return;
}

/**
 *  e1000_resume_workarounds_pchlan - workarounds needed during Sx->S0
 *  @hw: pointer to the HW structure
 *
 *  During Sx to S0 transitions on non-managed devices or managed devices
 *  on which PHY resets are not blocked, if the PHY registers cannot be
 *  accessed properly by the s/w toggle the LANPHYPC value to power cycle
 *  the PHY.
 *  On i217, setup Intel Rapid Start Technology.
 **/
u32 e1000_resume_workarounds_pchlan(struct e1000_hw *hw)
{
	s32 ret_val;

	DEBUGFUNC("e1000_resume_workarounds_pchlan");
	if (hw->mac.type < e1000_pch2lan)
		return E1000_SUCCESS;

	ret_val = e1000_init_phy_workarounds_pchlan(hw);
	if (ret_val) {
		DEBUGOUT1("Failed to init PHY flow ret_val=%d\n", ret_val);
		return ret_val;
	}

	/* For i217 Intel Rapid Start Technology support when the system
	 * is transitioning from Sx and no manageability engine is present
	 * configure SMBus to restore on reset, disable proxy, and enable
	 * the reset on MTA (Multicast table array).
	 */
	if (hw->phy.type == e1000_phy_i217) {
		u16 phy_reg;

		ret_val = hw->phy.ops.acquire(hw);
		if (ret_val) {
			DEBUGOUT("Failed to setup iRST\n");
			return ret_val;
		}

		/* Clear Auto Enable LPI after link up */
		hw->phy.ops.read_reg_locked(hw, I217_LPI_GPIO_CTRL, &phy_reg);
		phy_reg &= ~I217_LPI_GPIO_CTRL_AUTO_EN_LPI;
		hw->phy.ops.write_reg_locked(hw, I217_LPI_GPIO_CTRL, phy_reg);

		if (!(E1000_READ_REG(hw, E1000_FWSM) &
		    E1000_ICH_FWSM_FW_VALID)) {
			/* Restore clear on SMB if no manageability engine
			 * is present
			 */
			ret_val = hw->phy.ops.read_reg_locked(hw, I217_MEMPWR,
							      &phy_reg);
			if (ret_val)
				goto release;
			phy_reg |= I217_MEMPWR_DISABLE_SMB_RELEASE;
			hw->phy.ops.write_reg_locked(hw, I217_MEMPWR, phy_reg);

			/* Disable Proxy */
			hw->phy.ops.write_reg_locked(hw, I217_PROXY_CTRL, 0);
		}
		/* Enable reset on MTA */
		ret_val = hw->phy.ops.read_reg_locked(hw, I217_CGFREG,
						      &phy_reg);
		if (ret_val)
			goto release;
		phy_reg &= ~I217_CGFREG_ENABLE_MTA_RESET;
		hw->phy.ops.write_reg_locked(hw, I217_CGFREG, phy_reg);
release:
		if (ret_val)
			DEBUGOUT1("Error %d in resume workarounds\n", ret_val);
		hw->phy.ops.release(hw);
		return ret_val;
	}
	return E1000_SUCCESS;
}

/**
 *  e1000_cleanup_led_ich8lan - Restore the default LED operation
 *  @hw: pointer to the HW structure
 *
 *  Return the LED back to the default configuration.
 **/
static s32 e1000_cleanup_led_ich8lan(struct e1000_hw *hw)
{
	DEBUGFUNC("e1000_cleanup_led_ich8lan");

	if (hw->phy.type == e1000_phy_ife)
		return hw->phy.ops.write_reg(hw, IFE_PHY_SPECIAL_CONTROL_LED,
					     0);

	E1000_WRITE_REG(hw, E1000_LEDCTL, hw->mac.ledctl_default);
	return E1000_SUCCESS;
}

/**
 *  e1000_led_on_ich8lan - Turn LEDs on
 *  @hw: pointer to the HW structure
 *
 *  Turn on the LEDs.
 **/
static s32 e1000_led_on_ich8lan(struct e1000_hw *hw)
{
	DEBUGFUNC("e1000_led_on_ich8lan");

	if (hw->phy.type == e1000_phy_ife)
		return hw->phy.ops.write_reg(hw, IFE_PHY_SPECIAL_CONTROL_LED,
				(IFE_PSCL_PROBE_MODE | IFE_PSCL_PROBE_LEDS_ON));

	E1000_WRITE_REG(hw, E1000_LEDCTL, hw->mac.ledctl_mode2);
	return E1000_SUCCESS;
}

/**
 *  e1000_led_off_ich8lan - Turn LEDs off
 *  @hw: pointer to the HW structure
 *
 *  Turn off the LEDs.
 **/
static s32 e1000_led_off_ich8lan(struct e1000_hw *hw)
{
	DEBUGFUNC("e1000_led_off_ich8lan");

	if (hw->phy.type == e1000_phy_ife)
		return hw->phy.ops.write_reg(hw, IFE_PHY_SPECIAL_CONTROL_LED,
			       (IFE_PSCL_PROBE_MODE | IFE_PSCL_PROBE_LEDS_OFF));

	E1000_WRITE_REG(hw, E1000_LEDCTL, hw->mac.ledctl_mode1);
	return E1000_SUCCESS;
}

/**
 *  e1000_setup_led_pchlan - Configures SW controllable LED
 *  @hw: pointer to the HW structure
 *
 *  This prepares the SW controllable LED for use.
 **/
static s32 e1000_setup_led_pchlan(struct e1000_hw *hw)
{
	DEBUGFUNC("e1000_setup_led_pchlan");

	return hw->phy.ops.write_reg(hw, HV_LED_CONFIG,
				     (u16)hw->mac.ledctl_mode1);
}

/**
 *  e1000_cleanup_led_pchlan - Restore the default LED operation
 *  @hw: pointer to the HW structure
 *
 *  Return the LED back to the default configuration.
 **/
static s32 e1000_cleanup_led_pchlan(struct e1000_hw *hw)
{
	DEBUGFUNC("e1000_cleanup_led_pchlan");

	return hw->phy.ops.write_reg(hw, HV_LED_CONFIG,
				     (u16)hw->mac.ledctl_default);
}

/**
 *  e1000_led_on_pchlan - Turn LEDs on
 *  @hw: pointer to the HW structure
 *
 *  Turn on the LEDs.
 **/
static s32 e1000_led_on_pchlan(struct e1000_hw *hw)
{
	u16 data = (u16)hw->mac.ledctl_mode2;
	u32 i, led;

	DEBUGFUNC("e1000_led_on_pchlan");

	/* If no link, then turn LED on by setting the invert bit
	 * for each LED that's mode is "link_up" in ledctl_mode2.
	 */
	if (!(E1000_READ_REG(hw, E1000_STATUS) & E1000_STATUS_LU)) {
		for (i = 0; i < 3; i++) {
			led = (data >> (i * 5)) & E1000_PHY_LED0_MASK;
			if ((led & E1000_PHY_LED0_MODE_MASK) !=
			    E1000_LEDCTL_MODE_LINK_UP)
				continue;
			if (led & E1000_PHY_LED0_IVRT)
				data &= ~(E1000_PHY_LED0_IVRT << (i * 5));
			else
				data |= (E1000_PHY_LED0_IVRT << (i * 5));
		}
	}

	return hw->phy.ops.write_reg(hw, HV_LED_CONFIG, data);
}

/**
 *  e1000_led_off_pchlan - Turn LEDs off
 *  @hw: pointer to the HW structure
 *
 *  Turn off the LEDs.
 **/
static s32 e1000_led_off_pchlan(struct e1000_hw *hw)
{
	u16 data = (u16)hw->mac.ledctl_mode1;
	u32 i, led;

	DEBUGFUNC("e1000_led_off_pchlan");

	/* If no link, then turn LED off by clearing the invert bit
	 * for each LED that's mode is "link_up" in ledctl_mode1.
	 */
	if (!(E1000_READ_REG(hw, E1000_STATUS) & E1000_STATUS_LU)) {
		for (i = 0; i < 3; i++) {
			led = (data >> (i * 5)) & E1000_PHY_LED0_MASK;
			if ((led & E1000_PHY_LED0_MODE_MASK) !=
			    E1000_LEDCTL_MODE_LINK_UP)
				continue;
			if (led & E1000_PHY_LED0_IVRT)
				data &= ~(E1000_PHY_LED0_IVRT << (i * 5));
			else
				data |= (E1000_PHY_LED0_IVRT << (i * 5));
		}
	}

	return hw->phy.ops.write_reg(hw, HV_LED_CONFIG, data);
}

/**
 *  e1000_get_cfg_done_ich8lan - Read config done bit after Full or PHY reset
 *  @hw: pointer to the HW structure
 *
 *  Read appropriate register for the config done bit for completion status
 *  and configure the PHY through s/w for EEPROM-less parts.
 *
 *  NOTE: some silicon which is EEPROM-less will fail trying to read the
 *  config done bit, so only an error is logged and continues.  If we were
 *  to return with error, EEPROM-less silicon would not be able to be reset
 *  or change link.
 **/
static s32 e1000_get_cfg_done_ich8lan(struct e1000_hw *hw)
{
	s32 ret_val = E1000_SUCCESS;
	u32 bank = 0;
	u32 status;

	DEBUGFUNC("e1000_get_cfg_done_ich8lan");

	e1000_get_cfg_done_generic(hw);

	/* Wait for indication from h/w that it has completed basic config */
	if (hw->mac.type >= e1000_ich10lan) {
		e1000_lan_init_done_ich8lan(hw);
	} else {
		ret_val = e1000_get_auto_rd_done_generic(hw);
		if (ret_val) {
			/* When auto config read does not complete, do not
			 * return with an error. This can happen in situations
			 * where there is no eeprom and prevents getting link.
			 */
			DEBUGOUT("Auto Read Done did not complete\n");
			ret_val = E1000_SUCCESS;
		}
	}

	/* Clear PHY Reset Asserted bit */
	status = E1000_READ_REG(hw, E1000_STATUS);
	if (status & E1000_STATUS_PHYRA)
		E1000_WRITE_REG(hw, E1000_STATUS, status & ~E1000_STATUS_PHYRA);
	else
		DEBUGOUT("PHY Reset Asserted not set - needs delay\n");

	/* If EEPROM is not marked present, init the IGP 3 PHY manually */
	if (hw->mac.type <= e1000_ich9lan) {
		if (!(E1000_READ_REG(hw, E1000_EECD) & E1000_EECD_PRES) &&
		    (hw->phy.type == e1000_phy_igp_3)) {
			e1000_phy_init_script_igp3(hw);
		}
	} else {
		if (e1000_valid_nvm_bank_detect_ich8lan(hw, &bank)) {
			/* Maybe we should do a basic PHY config */
			DEBUGOUT("EEPROM not present\n");
			ret_val = -E1000_ERR_CONFIG;
		}
	}

	return ret_val;
}

/**
 * e1000_power_down_phy_copper_ich8lan - Remove link during PHY power down
 * @hw: pointer to the HW structure
 *
 * In the case of a PHY power down to save power, or to turn off link during a
 * driver unload, or wake on lan is not enabled, remove the link.
 **/
static void e1000_power_down_phy_copper_ich8lan(struct e1000_hw *hw)
{
	/* If the management interface is not enabled, then power down */
	if (!(hw->mac.ops.check_mng_mode(hw) ||
	      hw->phy.ops.check_reset_block(hw)))
		e1000_power_down_phy_copper(hw);

	return;
}

/**
 *  e1000_clear_hw_cntrs_ich8lan - Clear statistical counters
 *  @hw: pointer to the HW structure
 *
 *  Clears hardware counters specific to the silicon family and calls
 *  clear_hw_cntrs_generic to clear all general purpose counters.
 **/
static void e1000_clear_hw_cntrs_ich8lan(struct e1000_hw *hw)
{
	u16 phy_data;
	s32 ret_val;

	DEBUGFUNC("e1000_clear_hw_cntrs_ich8lan");

	e1000_clear_hw_cntrs_base_generic(hw);

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

	/* Clear PHY statistics registers */
	if ((hw->phy.type == e1000_phy_82578) ||
	    (hw->phy.type == e1000_phy_82579) ||
	    (hw->phy.type == e1000_phy_i217) ||
	    (hw->phy.type == e1000_phy_82577)) {
		ret_val = hw->phy.ops.acquire(hw);
		if (ret_val)
			return;
		ret_val = hw->phy.ops.set_page(hw,
					       HV_STATS_PAGE << IGP_PAGE_SHIFT);
		if (ret_val)
			goto release;
		hw->phy.ops.read_reg_page(hw, HV_SCC_UPPER, &phy_data);
		hw->phy.ops.read_reg_page(hw, HV_SCC_LOWER, &phy_data);
		hw->phy.ops.read_reg_page(hw, HV_ECOL_UPPER, &phy_data);
		hw->phy.ops.read_reg_page(hw, HV_ECOL_LOWER, &phy_data);
		hw->phy.ops.read_reg_page(hw, HV_MCC_UPPER, &phy_data);
		hw->phy.ops.read_reg_page(hw, HV_MCC_LOWER, &phy_data);
		hw->phy.ops.read_reg_page(hw, HV_LATECOL_UPPER, &phy_data);
		hw->phy.ops.read_reg_page(hw, HV_LATECOL_LOWER, &phy_data);
		hw->phy.ops.read_reg_page(hw, HV_COLC_UPPER, &phy_data);
		hw->phy.ops.read_reg_page(hw, HV_COLC_LOWER, &phy_data);
		hw->phy.ops.read_reg_page(hw, HV_DC_UPPER, &phy_data);
		hw->phy.ops.read_reg_page(hw, HV_DC_LOWER, &phy_data);
		hw->phy.ops.read_reg_page(hw, HV_TNCRS_UPPER, &phy_data);
		hw->phy.ops.read_reg_page(hw, HV_TNCRS_LOWER, &phy_data);
release:
		hw->phy.ops.release(hw);
	}
}

