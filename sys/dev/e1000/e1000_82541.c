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
 * 82541EI Gigabit Ethernet Controller
 * 82541ER Gigabit Ethernet Controller
 * 82541GI Gigabit Ethernet Controller
 * 82541PI Gigabit Ethernet Controller
 * 82547EI Gigabit Ethernet Controller
 * 82547GI Gigabit Ethernet Controller
 */

#include "e1000_api.h"

static s32  e1000_init_phy_params_82541(struct e1000_hw *hw);
static s32  e1000_init_nvm_params_82541(struct e1000_hw *hw);
static s32  e1000_init_mac_params_82541(struct e1000_hw *hw);
static s32  e1000_reset_hw_82541(struct e1000_hw *hw);
static s32  e1000_init_hw_82541(struct e1000_hw *hw);
static s32  e1000_get_link_up_info_82541(struct e1000_hw *hw, u16 *speed,
					 u16 *duplex);
static s32  e1000_phy_hw_reset_82541(struct e1000_hw *hw);
static s32  e1000_setup_copper_link_82541(struct e1000_hw *hw);
static s32  e1000_check_for_link_82541(struct e1000_hw *hw);
static s32  e1000_get_cable_length_igp_82541(struct e1000_hw *hw);
static s32  e1000_set_d3_lplu_state_82541(struct e1000_hw *hw,
					  bool active);
static s32  e1000_setup_led_82541(struct e1000_hw *hw);
static s32  e1000_cleanup_led_82541(struct e1000_hw *hw);
static void e1000_clear_hw_cntrs_82541(struct e1000_hw *hw);
static s32  e1000_read_mac_addr_82541(struct e1000_hw *hw);
static s32  e1000_config_dsp_after_link_change_82541(struct e1000_hw *hw,
						     bool link_up);
static s32  e1000_phy_init_script_82541(struct e1000_hw *hw);
static void e1000_power_down_phy_copper_82541(struct e1000_hw *hw);

static const u16 e1000_igp_cable_length_table[] = {
	5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 10, 10, 10, 10, 10,
	10, 10, 20, 20, 20, 20, 20, 25, 25, 25, 25, 25, 25, 25, 30, 30, 30, 30,
	40, 40, 40, 40, 40, 40, 40, 40, 40, 50, 50, 50, 50, 50, 50, 50, 60, 60,
	60, 60, 60, 60, 60, 60, 60, 70, 70, 70, 70, 70, 70, 80, 80, 80, 80, 80,
	80, 90, 90, 90, 90, 90, 90, 90, 90, 90, 100, 100, 100, 100, 100, 100,
	100, 100, 100, 100, 100, 100, 100, 100, 110, 110, 110, 110, 110, 110,
	110, 110, 110, 110, 110, 110, 110, 110, 110, 110, 110, 110, 120, 120,
	120, 120, 120, 120, 120, 120, 120, 120};
#define IGP01E1000_AGC_LENGTH_TABLE_SIZE \
		(sizeof(e1000_igp_cable_length_table) / \
		 sizeof(e1000_igp_cable_length_table[0]))

/**
 *  e1000_init_phy_params_82541 - Init PHY func ptrs.
 *  @hw: pointer to the HW structure
 **/
static s32 e1000_init_phy_params_82541(struct e1000_hw *hw)
{
	struct e1000_phy_info *phy = &hw->phy;
	s32 ret_val;

	DEBUGFUNC("e1000_init_phy_params_82541");

	phy->addr		= 1;
	phy->autoneg_mask	= AUTONEG_ADVERTISE_SPEED_DEFAULT;
	phy->reset_delay_us	= 10000;
	phy->type		= e1000_phy_igp;

	/* Function Pointers */
	phy->ops.check_polarity	= e1000_check_polarity_igp;
	phy->ops.force_speed_duplex = e1000_phy_force_speed_duplex_igp;
	phy->ops.get_cable_length = e1000_get_cable_length_igp_82541;
	phy->ops.get_cfg_done	= e1000_get_cfg_done_generic;
	phy->ops.get_info	= e1000_get_phy_info_igp;
	phy->ops.read_reg	= e1000_read_phy_reg_igp;
	phy->ops.reset		= e1000_phy_hw_reset_82541;
	phy->ops.set_d3_lplu_state = e1000_set_d3_lplu_state_82541;
	phy->ops.write_reg	= e1000_write_phy_reg_igp;
	phy->ops.power_up	= e1000_power_up_phy_copper;
	phy->ops.power_down	= e1000_power_down_phy_copper_82541;

	ret_val = e1000_get_phy_id(hw);
	if (ret_val)
		goto out;

	/* Verify phy id */
	if (phy->id != IGP01E1000_I_PHY_ID) {
		ret_val = -E1000_ERR_PHY;
		goto out;
	}

out:
	return ret_val;
}

/**
 *  e1000_init_nvm_params_82541 - Init NVM func ptrs.
 *  @hw: pointer to the HW structure
 **/
static s32 e1000_init_nvm_params_82541(struct e1000_hw *hw)
{
	struct e1000_nvm_info *nvm = &hw->nvm;
	s32 ret_val = E1000_SUCCESS;
	u32 eecd = E1000_READ_REG(hw, E1000_EECD);
	u16 size;

	DEBUGFUNC("e1000_init_nvm_params_82541");

	switch (nvm->override) {
	case e1000_nvm_override_spi_large:
		nvm->type = e1000_nvm_eeprom_spi;
		eecd |= E1000_EECD_ADDR_BITS;
		break;
	case e1000_nvm_override_spi_small:
		nvm->type = e1000_nvm_eeprom_spi;
		eecd &= ~E1000_EECD_ADDR_BITS;
		break;
	case e1000_nvm_override_microwire_large:
		nvm->type = e1000_nvm_eeprom_microwire;
		eecd |= E1000_EECD_SIZE;
		break;
	case e1000_nvm_override_microwire_small:
		nvm->type = e1000_nvm_eeprom_microwire;
		eecd &= ~E1000_EECD_SIZE;
		break;
	default:
		nvm->type = eecd & E1000_EECD_TYPE ? e1000_nvm_eeprom_spi
			    : e1000_nvm_eeprom_microwire;
		break;
	}

	if (nvm->type == e1000_nvm_eeprom_spi) {
		nvm->address_bits = (eecd & E1000_EECD_ADDR_BITS) ? 16 : 8;
		nvm->delay_usec = 1;
		nvm->opcode_bits = 8;
		nvm->page_size = (eecd & E1000_EECD_ADDR_BITS) ? 32 : 8;

		/* Function Pointers */
		nvm->ops.acquire	= e1000_acquire_nvm_generic;
		nvm->ops.read		= e1000_read_nvm_spi;
		nvm->ops.release	= e1000_release_nvm_generic;
		nvm->ops.update		= e1000_update_nvm_checksum_generic;
		nvm->ops.valid_led_default = e1000_valid_led_default_generic;
		nvm->ops.validate	= e1000_validate_nvm_checksum_generic;
		nvm->ops.write		= e1000_write_nvm_spi;

		/*
		 * nvm->word_size must be discovered after the pointers
		 * are set so we can verify the size from the nvm image
		 * itself.  Temporarily set it to a dummy value so the
		 * read will work.
		 */
		nvm->word_size = 64;
		ret_val = nvm->ops.read(hw, NVM_CFG, 1, &size);
		if (ret_val)
			goto out;
		size = (size & NVM_SIZE_MASK) >> NVM_SIZE_SHIFT;
		/*
		 * if size != 0, it can be added to a constant and become
		 * the left-shift value to set the word_size.  Otherwise,
		 * word_size stays at 64.
		 */
		if (size) {
			size += NVM_WORD_SIZE_BASE_SHIFT_82541;
			nvm->word_size = 1 << size;
		}
	} else {
		nvm->address_bits = (eecd & E1000_EECD_ADDR_BITS) ? 8 : 6;
		nvm->delay_usec = 50;
		nvm->opcode_bits = 3;
		nvm->word_size = (eecd & E1000_EECD_ADDR_BITS) ? 256 : 64;

		/* Function Pointers */
		nvm->ops.acquire	= e1000_acquire_nvm_generic;
		nvm->ops.read		= e1000_read_nvm_microwire;
		nvm->ops.release	= e1000_release_nvm_generic;
		nvm->ops.update		= e1000_update_nvm_checksum_generic;
		nvm->ops.valid_led_default = e1000_valid_led_default_generic;
		nvm->ops.validate	= e1000_validate_nvm_checksum_generic;
		nvm->ops.write		= e1000_write_nvm_microwire;
	}

out:
	return ret_val;
}

/**
 *  e1000_init_mac_params_82541 - Init MAC func ptrs.
 *  @hw: pointer to the HW structure
 **/
static s32 e1000_init_mac_params_82541(struct e1000_hw *hw)
{
	struct e1000_mac_info *mac = &hw->mac;

	DEBUGFUNC("e1000_init_mac_params_82541");

	/* Set media type */
	hw->phy.media_type = e1000_media_type_copper;
	/* Set mta register count */
	mac->mta_reg_count = 128;
	/* Set rar entry count */
	mac->rar_entry_count = E1000_RAR_ENTRIES;
	/* Set if part includes ASF firmware */
	mac->asf_firmware_present = TRUE;

	/* Function Pointers */

	/* bus type/speed/width */
	mac->ops.get_bus_info = e1000_get_bus_info_pci_generic;
	/* function id */
	mac->ops.set_lan_id = e1000_set_lan_id_single_port;
	/* reset */
	mac->ops.reset_hw = e1000_reset_hw_82541;
	/* hw initialization */
	mac->ops.init_hw = e1000_init_hw_82541;
	/* link setup */
	mac->ops.setup_link = e1000_setup_link_generic;
	/* physical interface link setup */
	mac->ops.setup_physical_interface = e1000_setup_copper_link_82541;
	/* check for link */
	mac->ops.check_for_link = e1000_check_for_link_82541;
	/* link info */
	mac->ops.get_link_up_info = e1000_get_link_up_info_82541;
	/* multicast address update */
	mac->ops.update_mc_addr_list = e1000_update_mc_addr_list_generic;
	/* writing VFTA */
	mac->ops.write_vfta = e1000_write_vfta_generic;
	/* clearing VFTA */
	mac->ops.clear_vfta = e1000_clear_vfta_generic;
	/* read mac address */
	mac->ops.read_mac_addr = e1000_read_mac_addr_82541;
	/* ID LED init */
	mac->ops.id_led_init = e1000_id_led_init_generic;
	/* setup LED */
	mac->ops.setup_led = e1000_setup_led_82541;
	/* cleanup LED */
	mac->ops.cleanup_led = e1000_cleanup_led_82541;
	/* turn on/off LED */
	mac->ops.led_on = e1000_led_on_generic;
	mac->ops.led_off = e1000_led_off_generic;
	/* clear hardware counters */
	mac->ops.clear_hw_cntrs = e1000_clear_hw_cntrs_82541;

	return E1000_SUCCESS;
}

/**
 *  e1000_init_function_pointers_82541 - Init func ptrs.
 *  @hw: pointer to the HW structure
 *
 *  Called to initialize all function pointers and parameters.
 **/
void e1000_init_function_pointers_82541(struct e1000_hw *hw)
{
	DEBUGFUNC("e1000_init_function_pointers_82541");

	hw->mac.ops.init_params = e1000_init_mac_params_82541;
	hw->nvm.ops.init_params = e1000_init_nvm_params_82541;
	hw->phy.ops.init_params = e1000_init_phy_params_82541;
}

/**
 *  e1000_reset_hw_82541 - Reset hardware
 *  @hw: pointer to the HW structure
 *
 *  This resets the hardware into a known state.
 **/
static s32 e1000_reset_hw_82541(struct e1000_hw *hw)
{
	u32 ledctl, ctrl, manc;

	DEBUGFUNC("e1000_reset_hw_82541");

	DEBUGOUT("Masking off all interrupts\n");
	E1000_WRITE_REG(hw, E1000_IMC, 0xFFFFFFFF);

	E1000_WRITE_REG(hw, E1000_RCTL, 0);
	E1000_WRITE_REG(hw, E1000_TCTL, E1000_TCTL_PSP);
	E1000_WRITE_FLUSH(hw);

	/*
	 * Delay to allow any outstanding PCI transactions to complete
	 * before resetting the device.
	 */
	msec_delay(10);

	ctrl = E1000_READ_REG(hw, E1000_CTRL);

	/* Must reset the Phy before resetting the MAC */
	if ((hw->mac.type == e1000_82541) || (hw->mac.type == e1000_82547)) {
		E1000_WRITE_REG(hw, E1000_CTRL, (ctrl | E1000_CTRL_PHY_RST));
		E1000_WRITE_FLUSH(hw);
		msec_delay(5);
	}

	DEBUGOUT("Issuing a global reset to 82541/82547 MAC\n");
	switch (hw->mac.type) {
	case e1000_82541:
	case e1000_82541_rev_2:
		/*
		 * These controllers can't ack the 64-bit write when
		 * issuing the reset, so we use IO-mapping as a
		 * workaround to issue the reset.
		 */
		E1000_WRITE_REG_IO(hw, E1000_CTRL, ctrl | E1000_CTRL_RST);
		break;
	default:
		E1000_WRITE_REG(hw, E1000_CTRL, ctrl | E1000_CTRL_RST);
		break;
	}

	/* Wait for NVM reload */
	msec_delay(20);

	/* Disable HW ARPs on ASF enabled adapters */
	manc = E1000_READ_REG(hw, E1000_MANC);
	manc &= ~E1000_MANC_ARP_EN;
	E1000_WRITE_REG(hw, E1000_MANC, manc);

	if ((hw->mac.type == e1000_82541) || (hw->mac.type == e1000_82547)) {
		e1000_phy_init_script_82541(hw);

		/* Configure activity LED after Phy reset */
		ledctl = E1000_READ_REG(hw, E1000_LEDCTL);
		ledctl &= IGP_ACTIVITY_LED_MASK;
		ledctl |= (IGP_ACTIVITY_LED_ENABLE | IGP_LED3_MODE);
		E1000_WRITE_REG(hw, E1000_LEDCTL, ledctl);
	}

	/* Once again, mask the interrupts */
	DEBUGOUT("Masking off all interrupts\n");
	E1000_WRITE_REG(hw, E1000_IMC, 0xFFFFFFFF);

	/* Clear any pending interrupt events. */
	E1000_READ_REG(hw, E1000_ICR);

	return E1000_SUCCESS;
}

/**
 *  e1000_init_hw_82541 - Initialize hardware
 *  @hw: pointer to the HW structure
 *
 *  This inits the hardware readying it for operation.
 **/
static s32 e1000_init_hw_82541(struct e1000_hw *hw)
{
	struct e1000_mac_info *mac = &hw->mac;
	struct e1000_dev_spec_82541 *dev_spec = &hw->dev_spec._82541;
	u32 i, txdctl;
	s32 ret_val;

	DEBUGFUNC("e1000_init_hw_82541");

	/* Initialize identification LED */
	ret_val = mac->ops.id_led_init(hw);
	if (ret_val) {
		DEBUGOUT("Error initializing identification LED\n");
		/* This is not fatal and we should not stop init due to this */
	}

	/* Storing the Speed Power Down  value for later use */
	ret_val = hw->phy.ops.read_reg(hw, IGP01E1000_GMII_FIFO,
				       &dev_spec->spd_default);
	if (ret_val)
		goto out;

	/* Disabling VLAN filtering */
	DEBUGOUT("Initializing the IEEE VLAN\n");
	mac->ops.clear_vfta(hw);

	/* Setup the receive address. */
	e1000_init_rx_addrs_generic(hw, mac->rar_entry_count);

	/* Zero out the Multicast HASH table */
	DEBUGOUT("Zeroing the MTA\n");
	for (i = 0; i < mac->mta_reg_count; i++) {
		E1000_WRITE_REG_ARRAY(hw, E1000_MTA, i, 0);
		/*
		 * Avoid back to back register writes by adding the register
		 * read (flush).  This is to protect against some strange
		 * bridge configurations that may issue Memory Write Block
		 * (MWB) to our register space.
		 */
		E1000_WRITE_FLUSH(hw);
	}

	/* Setup link and flow control */
	ret_val = mac->ops.setup_link(hw);

	txdctl = E1000_READ_REG(hw, E1000_TXDCTL(0));
	txdctl = (txdctl & ~E1000_TXDCTL_WTHRESH) |
		  E1000_TXDCTL_FULL_TX_DESC_WB;
	E1000_WRITE_REG(hw, E1000_TXDCTL(0), txdctl);

	/*
	 * Clear all of the statistics registers (clear on read).  It is
	 * important that we do this after we have tried to establish link
	 * because the symbol error count will increment wildly if there
	 * is no link.
	 */
	e1000_clear_hw_cntrs_82541(hw);

out:
	return ret_val;
}

/**
 * e1000_get_link_up_info_82541 - Report speed and duplex
 * @hw: pointer to the HW structure
 * @speed: pointer to speed buffer
 * @duplex: pointer to duplex buffer
 *
 * Retrieve the current speed and duplex configuration.
 **/
static s32 e1000_get_link_up_info_82541(struct e1000_hw *hw, u16 *speed,
					u16 *duplex)
{
	struct e1000_phy_info *phy = &hw->phy;
	s32 ret_val;
	u16 data;

	DEBUGFUNC("e1000_get_link_up_info_82541");

	ret_val = e1000_get_speed_and_duplex_copper_generic(hw, speed, duplex);
	if (ret_val)
		goto out;

	if (!phy->speed_downgraded)
		goto out;

	/*
	 * IGP01 PHY may advertise full duplex operation after speed
	 * downgrade even if it is operating at half duplex.
	 * Here we set the duplex settings to match the duplex in the
	 * link partner's capabilities.
	 */
	ret_val = phy->ops.read_reg(hw, PHY_AUTONEG_EXP, &data);
	if (ret_val)
		goto out;

	if (!(data & NWAY_ER_LP_NWAY_CAPS)) {
		*duplex = HALF_DUPLEX;
	} else {
		ret_val = phy->ops.read_reg(hw, PHY_LP_ABILITY, &data);
		if (ret_val)
			goto out;

		if (*speed == SPEED_100) {
			if (!(data & NWAY_LPAR_100TX_FD_CAPS))
				*duplex = HALF_DUPLEX;
		} else if (*speed == SPEED_10) {
			if (!(data & NWAY_LPAR_10T_FD_CAPS))
				*duplex = HALF_DUPLEX;
		}
	}

out:
	return ret_val;
}

/**
 *  e1000_phy_hw_reset_82541 - PHY hardware reset
 *  @hw: pointer to the HW structure
 *
 *  Verify the reset block is not blocking us from resetting.  Acquire
 *  semaphore (if necessary) and read/set/write the device control reset
 *  bit in the PHY.  Wait the appropriate delay time for the device to
 *  reset and release the semaphore (if necessary).
 **/
static s32 e1000_phy_hw_reset_82541(struct e1000_hw *hw)
{
	s32 ret_val;
	u32 ledctl;

	DEBUGFUNC("e1000_phy_hw_reset_82541");

	ret_val = e1000_phy_hw_reset_generic(hw);
	if (ret_val)
		goto out;

	e1000_phy_init_script_82541(hw);

	if ((hw->mac.type == e1000_82541) || (hw->mac.type == e1000_82547)) {
		/* Configure activity LED after PHY reset */
		ledctl = E1000_READ_REG(hw, E1000_LEDCTL);
		ledctl &= IGP_ACTIVITY_LED_MASK;
		ledctl |= (IGP_ACTIVITY_LED_ENABLE | IGP_LED3_MODE);
		E1000_WRITE_REG(hw, E1000_LEDCTL, ledctl);
	}

out:
	return ret_val;
}

/**
 *  e1000_setup_copper_link_82541 - Configure copper link settings
 *  @hw: pointer to the HW structure
 *
 *  Calls the appropriate function to configure the link for auto-neg or forced
 *  speed and duplex.  Then we check for link, once link is established calls
 *  to configure collision distance and flow control are called.  If link is
 *  not established, we return -E1000_ERR_PHY (-2).
 **/
static s32 e1000_setup_copper_link_82541(struct e1000_hw *hw)
{
	struct e1000_phy_info *phy = &hw->phy;
	struct e1000_dev_spec_82541 *dev_spec = &hw->dev_spec._82541;
	s32  ret_val;
	u32 ctrl, ledctl;

	DEBUGFUNC("e1000_setup_copper_link_82541");

	ctrl = E1000_READ_REG(hw, E1000_CTRL);
	ctrl |= E1000_CTRL_SLU;
	ctrl &= ~(E1000_CTRL_FRCSPD | E1000_CTRL_FRCDPX);
	E1000_WRITE_REG(hw, E1000_CTRL, ctrl);


	/* Earlier revs of the IGP phy require us to force MDI. */
	if (hw->mac.type == e1000_82541 || hw->mac.type == e1000_82547) {
		dev_spec->dsp_config = e1000_dsp_config_disabled;
		phy->mdix = 1;
	} else {
		dev_spec->dsp_config = e1000_dsp_config_enabled;
	}

	ret_val = e1000_copper_link_setup_igp(hw);
	if (ret_val)
		goto out;

	if (hw->mac.autoneg) {
		if (dev_spec->ffe_config == e1000_ffe_config_active)
			dev_spec->ffe_config = e1000_ffe_config_enabled;
	}

	/* Configure activity LED after Phy reset */
	ledctl = E1000_READ_REG(hw, E1000_LEDCTL);
	ledctl &= IGP_ACTIVITY_LED_MASK;
	ledctl |= (IGP_ACTIVITY_LED_ENABLE | IGP_LED3_MODE);
	E1000_WRITE_REG(hw, E1000_LEDCTL, ledctl);

	ret_val = e1000_setup_copper_link_generic(hw);

out:
	return ret_val;
}

/**
 *  e1000_check_for_link_82541 - Check/Store link connection
 *  @hw: pointer to the HW structure
 *
 *  This checks the link condition of the adapter and stores the
 *  results in the hw->mac structure.
 **/
static s32 e1000_check_for_link_82541(struct e1000_hw *hw)
{
	struct e1000_mac_info *mac = &hw->mac;
	s32 ret_val;
	bool link;

	DEBUGFUNC("e1000_check_for_link_82541");

	/*
	 * We only want to go out to the PHY registers to see if Auto-Neg
	 * has completed and/or if our link status has changed.  The
	 * get_link_status flag is set upon receiving a Link Status
	 * Change or Rx Sequence Error interrupt.
	 */
	if (!mac->get_link_status) {
		ret_val = E1000_SUCCESS;
		goto out;
	}

	/*
	 * First we want to see if the MII Status Register reports
	 * link.  If so, then we want to get the current speed/duplex
	 * of the PHY.
	 */
	ret_val = e1000_phy_has_link_generic(hw, 1, 0, &link);
	if (ret_val)
		goto out;

	if (!link) {
		ret_val = e1000_config_dsp_after_link_change_82541(hw, FALSE);
		goto out; /* No link detected */
	}

	mac->get_link_status = FALSE;

	/*
	 * Check if there was DownShift, must be checked
	 * immediately after link-up
	 */
	e1000_check_downshift_generic(hw);

	/*
	 * If we are forcing speed/duplex, then we simply return since
	 * we have already determined whether we have link or not.
	 */
	if (!mac->autoneg) {
		ret_val = -E1000_ERR_CONFIG;
		goto out;
	}

	ret_val = e1000_config_dsp_after_link_change_82541(hw, TRUE);

	/*
	 * Auto-Neg is enabled.  Auto Speed Detection takes care
	 * of MAC speed/duplex configuration.  So we only need to
	 * configure Collision Distance in the MAC.
	 */
	mac->ops.config_collision_dist(hw);

	/*
	 * Configure Flow Control now that Auto-Neg has completed.
	 * First, we need to restore the desired flow control
	 * settings because we may have had to re-autoneg with a
	 * different link partner.
	 */
	ret_val = e1000_config_fc_after_link_up_generic(hw);
	if (ret_val)
		DEBUGOUT("Error configuring flow control\n");

out:
	return ret_val;
}

/**
 *  e1000_config_dsp_after_link_change_82541 - Config DSP after link
 *  @hw: pointer to the HW structure
 *  @link_up: boolean flag for link up status
 *
 *  Return E1000_ERR_PHY when failing to read/write the PHY, else E1000_SUCCESS
 *  at any other case.
 *
 *  82541_rev_2 & 82547_rev_2 have the capability to configure the DSP when a
 *  gigabit link is achieved to improve link quality.
 **/
static s32 e1000_config_dsp_after_link_change_82541(struct e1000_hw *hw,
						    bool link_up)
{
	struct e1000_phy_info *phy = &hw->phy;
	struct e1000_dev_spec_82541 *dev_spec = &hw->dev_spec._82541;
	s32 ret_val;
	u32 idle_errs = 0;
	u16 phy_data, phy_saved_data, speed, duplex, i;
	u16 ffe_idle_err_timeout = FFE_IDLE_ERR_COUNT_TIMEOUT_20;
	u16 dsp_reg_array[IGP01E1000_PHY_CHANNEL_NUM] = {
						IGP01E1000_PHY_AGC_PARAM_A,
						IGP01E1000_PHY_AGC_PARAM_B,
						IGP01E1000_PHY_AGC_PARAM_C,
						IGP01E1000_PHY_AGC_PARAM_D};

	DEBUGFUNC("e1000_config_dsp_after_link_change_82541");

	if (link_up) {
		ret_val = hw->mac.ops.get_link_up_info(hw, &speed, &duplex);
		if (ret_val) {
			DEBUGOUT("Error getting link speed and duplex\n");
			goto out;
		}

		if (speed != SPEED_1000) {
			ret_val = E1000_SUCCESS;
			goto out;
		}

		ret_val = phy->ops.get_cable_length(hw);
		if (ret_val)
			goto out;

		if ((dev_spec->dsp_config == e1000_dsp_config_enabled) &&
		    phy->min_cable_length >= 50) {

			for (i = 0; i < IGP01E1000_PHY_CHANNEL_NUM; i++) {
				ret_val = phy->ops.read_reg(hw,
							    dsp_reg_array[i],
							    &phy_data);
				if (ret_val)
					goto out;

				phy_data &= ~IGP01E1000_PHY_EDAC_MU_INDEX;

				ret_val = phy->ops.write_reg(hw,
							     dsp_reg_array[i],
							     phy_data);
				if (ret_val)
					goto out;
			}
			dev_spec->dsp_config = e1000_dsp_config_activated;
		}

		if ((dev_spec->ffe_config != e1000_ffe_config_enabled) ||
		    (phy->min_cable_length >= 50)) {
			ret_val = E1000_SUCCESS;
			goto out;
		}

		/* clear previous idle error counts */
		ret_val = phy->ops.read_reg(hw, PHY_1000T_STATUS, &phy_data);
		if (ret_val)
			goto out;

		for (i = 0; i < ffe_idle_err_timeout; i++) {
			usec_delay(1000);
			ret_val = phy->ops.read_reg(hw, PHY_1000T_STATUS,
						    &phy_data);
			if (ret_val)
				goto out;

			idle_errs += (phy_data & SR_1000T_IDLE_ERROR_CNT);
			if (idle_errs > SR_1000T_PHY_EXCESSIVE_IDLE_ERR_COUNT) {
				dev_spec->ffe_config = e1000_ffe_config_active;

				ret_val = phy->ops.write_reg(hw,
						  IGP01E1000_PHY_DSP_FFE,
						  IGP01E1000_PHY_DSP_FFE_CM_CP);
				if (ret_val)
					goto out;
				break;
			}

			if (idle_errs)
				ffe_idle_err_timeout =
						 FFE_IDLE_ERR_COUNT_TIMEOUT_100;
		}
	} else {
		if (dev_spec->dsp_config == e1000_dsp_config_activated) {
			/*
			 * Save off the current value of register 0x2F5B
			 * to be restored at the end of the routines.
			 */
			ret_val = phy->ops.read_reg(hw, 0x2F5B,
						    &phy_saved_data);
			if (ret_val)
				goto out;

			/* Disable the PHY transmitter */
			ret_val = phy->ops.write_reg(hw, 0x2F5B, 0x0003);
			if (ret_val)
				goto out;

			msec_delay_irq(20);

			ret_val = phy->ops.write_reg(hw, 0x0000,
						     IGP01E1000_IEEE_FORCE_GIG);
			if (ret_val)
				goto out;
			for (i = 0; i < IGP01E1000_PHY_CHANNEL_NUM; i++) {
				ret_val = phy->ops.read_reg(hw,
							    dsp_reg_array[i],
							    &phy_data);
				if (ret_val)
					goto out;

				phy_data &= ~IGP01E1000_PHY_EDAC_MU_INDEX;
				phy_data |= IGP01E1000_PHY_EDAC_SIGN_EXT_9_BITS;

				ret_val = phy->ops.write_reg(hw,
							     dsp_reg_array[i],
							     phy_data);
				if (ret_val)
					goto out;
			}

			ret_val = phy->ops.write_reg(hw, 0x0000,
					       IGP01E1000_IEEE_RESTART_AUTONEG);
			if (ret_val)
				goto out;

			msec_delay_irq(20);

			/* Now enable the transmitter */
			ret_val = phy->ops.write_reg(hw, 0x2F5B,
						     phy_saved_data);
			if (ret_val)
				goto out;

			dev_spec->dsp_config = e1000_dsp_config_enabled;
		}

		if (dev_spec->ffe_config != e1000_ffe_config_active) {
			ret_val = E1000_SUCCESS;
			goto out;
		}

		/*
		 * Save off the current value of register 0x2F5B
		 * to be restored at the end of the routines.
		 */
		ret_val = phy->ops.read_reg(hw, 0x2F5B, &phy_saved_data);
		if (ret_val)
			goto out;

		/* Disable the PHY transmitter */
		ret_val = phy->ops.write_reg(hw, 0x2F5B, 0x0003);
		if (ret_val)
			goto out;

		msec_delay_irq(20);

		ret_val = phy->ops.write_reg(hw, 0x0000,
					     IGP01E1000_IEEE_FORCE_GIG);
		if (ret_val)
			goto out;

		ret_val = phy->ops.write_reg(hw, IGP01E1000_PHY_DSP_FFE,
					     IGP01E1000_PHY_DSP_FFE_DEFAULT);
		if (ret_val)
			goto out;

		ret_val = phy->ops.write_reg(hw, 0x0000,
					     IGP01E1000_IEEE_RESTART_AUTONEG);
		if (ret_val)
			goto out;

		msec_delay_irq(20);

		/* Now enable the transmitter */
		ret_val = phy->ops.write_reg(hw, 0x2F5B, phy_saved_data);

		if (ret_val)
			goto out;

		dev_spec->ffe_config = e1000_ffe_config_enabled;
	}

out:
	return ret_val;
}

/**
 *  e1000_get_cable_length_igp_82541 - Determine cable length for igp PHY
 *  @hw: pointer to the HW structure
 *
 *  The automatic gain control (agc) normalizes the amplitude of the
 *  received signal, adjusting for the attenuation produced by the
 *  cable.  By reading the AGC registers, which represent the
 *  combination of coarse and fine gain value, the value can be put
 *  into a lookup table to obtain the approximate cable length
 *  for each channel.
 **/
static s32 e1000_get_cable_length_igp_82541(struct e1000_hw *hw)
{
	struct e1000_phy_info *phy = &hw->phy;
	s32 ret_val = E1000_SUCCESS;
	u16 i, data;
	u16 cur_agc_value, agc_value = 0;
	u16 min_agc_value = IGP01E1000_AGC_LENGTH_TABLE_SIZE;
	u16 agc_reg_array[IGP01E1000_PHY_CHANNEL_NUM] = {IGP01E1000_PHY_AGC_A,
							 IGP01E1000_PHY_AGC_B,
							 IGP01E1000_PHY_AGC_C,
							 IGP01E1000_PHY_AGC_D};

	DEBUGFUNC("e1000_get_cable_length_igp_82541");

	/* Read the AGC registers for all channels */
	for (i = 0; i < IGP01E1000_PHY_CHANNEL_NUM; i++) {
		ret_val = phy->ops.read_reg(hw, agc_reg_array[i], &data);
		if (ret_val)
			goto out;

		cur_agc_value = data >> IGP01E1000_AGC_LENGTH_SHIFT;

		/* Bounds checking */
		if ((cur_agc_value >= IGP01E1000_AGC_LENGTH_TABLE_SIZE - 1) ||
		    (cur_agc_value == 0)) {
			ret_val = -E1000_ERR_PHY;
			goto out;
		}

		agc_value += cur_agc_value;

		if (min_agc_value > cur_agc_value)
			min_agc_value = cur_agc_value;
	}

	/* Remove the minimal AGC result for length < 50m */
	if (agc_value < IGP01E1000_PHY_CHANNEL_NUM * 50) {
		agc_value -= min_agc_value;
		/* Average the three remaining channels for the length. */
		agc_value /= (IGP01E1000_PHY_CHANNEL_NUM - 1);
	} else {
		/* Average the channels for the length. */
		agc_value /= IGP01E1000_PHY_CHANNEL_NUM;
	}

	phy->min_cable_length = (e1000_igp_cable_length_table[agc_value] >
				 IGP01E1000_AGC_RANGE)
				? (e1000_igp_cable_length_table[agc_value] -
				   IGP01E1000_AGC_RANGE)
				: 0;
	phy->max_cable_length = e1000_igp_cable_length_table[agc_value] +
				IGP01E1000_AGC_RANGE;

	phy->cable_length = (phy->min_cable_length + phy->max_cable_length) / 2;

out:
	return ret_val;
}

/**
 *  e1000_set_d3_lplu_state_82541 - Sets low power link up state for D3
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
static s32 e1000_set_d3_lplu_state_82541(struct e1000_hw *hw, bool active)
{
	struct e1000_phy_info *phy = &hw->phy;
	s32 ret_val;
	u16 data;

	DEBUGFUNC("e1000_set_d3_lplu_state_82541");

	switch (hw->mac.type) {
	case e1000_82541_rev_2:
	case e1000_82547_rev_2:
		break;
	default:
		ret_val = e1000_set_d3_lplu_state_generic(hw, active);
		goto out;
		break;
	}

	ret_val = phy->ops.read_reg(hw, IGP01E1000_GMII_FIFO, &data);
	if (ret_val)
		goto out;

	if (!active) {
		data &= ~IGP01E1000_GMII_FLEX_SPD;
		ret_val = phy->ops.write_reg(hw, IGP01E1000_GMII_FIFO, data);
		if (ret_val)
			goto out;

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
	} else if ((phy->autoneg_advertised == E1000_ALL_SPEED_DUPLEX) ||
		   (phy->autoneg_advertised == E1000_ALL_NOT_GIG) ||
		   (phy->autoneg_advertised == E1000_ALL_10_SPEED)) {
		data |= IGP01E1000_GMII_FLEX_SPD;
		ret_val = phy->ops.write_reg(hw, IGP01E1000_GMII_FIFO, data);
		if (ret_val)
			goto out;

		/* When LPLU is enabled, we should disable SmartSpeed */
		ret_val = phy->ops.read_reg(hw, IGP01E1000_PHY_PORT_CONFIG,
					    &data);
		if (ret_val)
			goto out;

		data &= ~IGP01E1000_PSCFR_SMART_SPEED;
		ret_val = phy->ops.write_reg(hw, IGP01E1000_PHY_PORT_CONFIG,
					     data);
	}

out:
	return ret_val;
}

/**
 *  e1000_setup_led_82541 - Configures SW controllable LED
 *  @hw: pointer to the HW structure
 *
 *  This prepares the SW controllable LED for use and saves the current state
 *  of the LED so it can be later restored.
 **/
static s32 e1000_setup_led_82541(struct e1000_hw *hw)
{
	struct e1000_dev_spec_82541 *dev_spec = &hw->dev_spec._82541;
	s32 ret_val;

	DEBUGFUNC("e1000_setup_led_82541");

	ret_val = hw->phy.ops.read_reg(hw, IGP01E1000_GMII_FIFO,
				       &dev_spec->spd_default);
	if (ret_val)
		goto out;

	ret_val = hw->phy.ops.write_reg(hw, IGP01E1000_GMII_FIFO,
					(u16)(dev_spec->spd_default &
					~IGP01E1000_GMII_SPD));
	if (ret_val)
		goto out;

	E1000_WRITE_REG(hw, E1000_LEDCTL, hw->mac.ledctl_mode1);

out:
	return ret_val;
}

/**
 *  e1000_cleanup_led_82541 - Set LED config to default operation
 *  @hw: pointer to the HW structure
 *
 *  Remove the current LED configuration and set the LED configuration
 *  to the default value, saved from the EEPROM.
 **/
static s32 e1000_cleanup_led_82541(struct e1000_hw *hw)
{
	struct e1000_dev_spec_82541 *dev_spec = &hw->dev_spec._82541;
	s32 ret_val;

	DEBUGFUNC("e1000_cleanup_led_82541");

	ret_val = hw->phy.ops.write_reg(hw, IGP01E1000_GMII_FIFO,
					dev_spec->spd_default);
	if (ret_val)
		goto out;

	E1000_WRITE_REG(hw, E1000_LEDCTL, hw->mac.ledctl_default);

out:
	return ret_val;
}

/**
 *  e1000_phy_init_script_82541 - Initialize GbE PHY
 *  @hw: pointer to the HW structure
 *
 *  Initializes the IGP PHY.
 **/
static s32 e1000_phy_init_script_82541(struct e1000_hw *hw)
{
	struct e1000_dev_spec_82541 *dev_spec = &hw->dev_spec._82541;
	u32 ret_val;
	u16 phy_saved_data;

	DEBUGFUNC("e1000_phy_init_script_82541");

	if (!dev_spec->phy_init_script) {
		ret_val = E1000_SUCCESS;
		goto out;
	}

	/* Delay after phy reset to enable NVM configuration to load */
	msec_delay(20);

	/*
	 * Save off the current value of register 0x2F5B to be restored at
	 * the end of this routine.
	 */
	ret_val = hw->phy.ops.read_reg(hw, 0x2F5B, &phy_saved_data);

	/* Disabled the PHY transmitter */
	hw->phy.ops.write_reg(hw, 0x2F5B, 0x0003);

	msec_delay(20);

	hw->phy.ops.write_reg(hw, 0x0000, 0x0140);

	msec_delay(5);

	switch (hw->mac.type) {
	case e1000_82541:
	case e1000_82547:
		hw->phy.ops.write_reg(hw, 0x1F95, 0x0001);

		hw->phy.ops.write_reg(hw, 0x1F71, 0xBD21);

		hw->phy.ops.write_reg(hw, 0x1F79, 0x0018);

		hw->phy.ops.write_reg(hw, 0x1F30, 0x1600);

		hw->phy.ops.write_reg(hw, 0x1F31, 0x0014);

		hw->phy.ops.write_reg(hw, 0x1F32, 0x161C);

		hw->phy.ops.write_reg(hw, 0x1F94, 0x0003);

		hw->phy.ops.write_reg(hw, 0x1F96, 0x003F);

		hw->phy.ops.write_reg(hw, 0x2010, 0x0008);
		break;
	case e1000_82541_rev_2:
	case e1000_82547_rev_2:
		hw->phy.ops.write_reg(hw, 0x1F73, 0x0099);
		break;
	default:
		break;
	}

	hw->phy.ops.write_reg(hw, 0x0000, 0x3300);

	msec_delay(20);

	/* Now enable the transmitter */
	hw->phy.ops.write_reg(hw, 0x2F5B, phy_saved_data);

	if (hw->mac.type == e1000_82547) {
		u16 fused, fine, coarse;

		/* Move to analog registers page */
		hw->phy.ops.read_reg(hw, IGP01E1000_ANALOG_SPARE_FUSE_STATUS,
				     &fused);

		if (!(fused & IGP01E1000_ANALOG_SPARE_FUSE_ENABLED)) {
			hw->phy.ops.read_reg(hw, IGP01E1000_ANALOG_FUSE_STATUS,
					     &fused);

			fine = fused & IGP01E1000_ANALOG_FUSE_FINE_MASK;
			coarse = fused & IGP01E1000_ANALOG_FUSE_COARSE_MASK;

			if (coarse > IGP01E1000_ANALOG_FUSE_COARSE_THRESH) {
				coarse -= IGP01E1000_ANALOG_FUSE_COARSE_10;
				fine -= IGP01E1000_ANALOG_FUSE_FINE_1;
			} else if (coarse ==
				   IGP01E1000_ANALOG_FUSE_COARSE_THRESH)
				fine -= IGP01E1000_ANALOG_FUSE_FINE_10;

			fused = (fused & IGP01E1000_ANALOG_FUSE_POLY_MASK) |
				(fine & IGP01E1000_ANALOG_FUSE_FINE_MASK) |
				(coarse & IGP01E1000_ANALOG_FUSE_COARSE_MASK);

			hw->phy.ops.write_reg(hw,
					      IGP01E1000_ANALOG_FUSE_CONTROL,
					      fused);
			hw->phy.ops.write_reg(hw,
				      IGP01E1000_ANALOG_FUSE_BYPASS,
				      IGP01E1000_ANALOG_FUSE_ENABLE_SW_CONTROL);
		}
	}

out:
	return ret_val;
}

/**
 *  e1000_init_script_state_82541 - Enable/Disable PHY init script
 *  @hw: pointer to the HW structure
 *  @state: boolean value used to enable/disable PHY init script
 *
 *  Allows the driver to enable/disable the PHY init script, if the PHY is an
 *  IGP PHY.
 **/
void e1000_init_script_state_82541(struct e1000_hw *hw, bool state)
{
	struct e1000_dev_spec_82541 *dev_spec = &hw->dev_spec._82541;

	DEBUGFUNC("e1000_init_script_state_82541");

	if (hw->phy.type != e1000_phy_igp) {
		DEBUGOUT("Initialization script not necessary.\n");
		goto out;
	}

	dev_spec->phy_init_script = state;

out:
	return;
}

/**
 * e1000_power_down_phy_copper_82541 - Remove link in case of PHY power down
 * @hw: pointer to the HW structure
 *
 * In the case of a PHY power down to save power, or to turn off link during a
 * driver unload, or wake on lan is not enabled, remove the link.
 **/
static void e1000_power_down_phy_copper_82541(struct e1000_hw *hw)
{
	/* If the management interface is not enabled, then power down */
	if (!(E1000_READ_REG(hw, E1000_MANC) & E1000_MANC_SMBUS_EN))
		e1000_power_down_phy_copper(hw);

	return;
}

/**
 *  e1000_clear_hw_cntrs_82541 - Clear device specific hardware counters
 *  @hw: pointer to the HW structure
 *
 *  Clears the hardware counters by reading the counter registers.
 **/
static void e1000_clear_hw_cntrs_82541(struct e1000_hw *hw)
{
	DEBUGFUNC("e1000_clear_hw_cntrs_82541");

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
}

/**
 *  e1000_read_mac_addr_82541 - Read device MAC address
 *  @hw: pointer to the HW structure
 *
 *  Reads the device MAC address from the EEPROM and stores the value.
 **/
static s32 e1000_read_mac_addr_82541(struct e1000_hw *hw)
{
	s32  ret_val = E1000_SUCCESS;
	u16 offset, nvm_data, i;

	DEBUGFUNC("e1000_read_mac_addr");

	for (i = 0; i < ETH_ADDR_LEN; i += 2) {
		offset = i >> 1;
		ret_val = hw->nvm.ops.read(hw, offset, 1, &nvm_data);
		if (ret_val) {
			DEBUGOUT("NVM Read Error\n");
			goto out;
		}
		hw->mac.perm_addr[i] = (u8)(nvm_data & 0xFF);
		hw->mac.perm_addr[i+1] = (u8)(nvm_data >> 8);
	}

	for (i = 0; i < ETH_ADDR_LEN; i++)
		hw->mac.addr[i] = hw->mac.perm_addr[i];

out:
	return ret_val;
}

