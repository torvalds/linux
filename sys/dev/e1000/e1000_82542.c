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
 * 82542 Gigabit Ethernet Controller
 */

#include "e1000_api.h"

static s32  e1000_init_phy_params_82542(struct e1000_hw *hw);
static s32  e1000_init_nvm_params_82542(struct e1000_hw *hw);
static s32  e1000_init_mac_params_82542(struct e1000_hw *hw);
static s32  e1000_get_bus_info_82542(struct e1000_hw *hw);
static s32  e1000_reset_hw_82542(struct e1000_hw *hw);
static s32  e1000_init_hw_82542(struct e1000_hw *hw);
static s32  e1000_setup_link_82542(struct e1000_hw *hw);
static s32  e1000_led_on_82542(struct e1000_hw *hw);
static s32  e1000_led_off_82542(struct e1000_hw *hw);
static int  e1000_rar_set_82542(struct e1000_hw *hw, u8 *addr, u32 index);
static void e1000_clear_hw_cntrs_82542(struct e1000_hw *hw);
static s32  e1000_read_mac_addr_82542(struct e1000_hw *hw);

/**
 *  e1000_init_phy_params_82542 - Init PHY func ptrs.
 *  @hw: pointer to the HW structure
 **/
static s32 e1000_init_phy_params_82542(struct e1000_hw *hw)
{
	struct e1000_phy_info *phy = &hw->phy;
	s32 ret_val = E1000_SUCCESS;

	DEBUGFUNC("e1000_init_phy_params_82542");

	phy->type = e1000_phy_none;

	return ret_val;
}

/**
 *  e1000_init_nvm_params_82542 - Init NVM func ptrs.
 *  @hw: pointer to the HW structure
 **/
static s32 e1000_init_nvm_params_82542(struct e1000_hw *hw)
{
	struct e1000_nvm_info *nvm = &hw->nvm;

	DEBUGFUNC("e1000_init_nvm_params_82542");

	nvm->address_bits	=  6;
	nvm->delay_usec		= 50;
	nvm->opcode_bits	=  3;
	nvm->type		= e1000_nvm_eeprom_microwire;
	nvm->word_size		= 64;

	/* Function Pointers */
	nvm->ops.read		= e1000_read_nvm_microwire;
	nvm->ops.release	= e1000_stop_nvm;
	nvm->ops.write		= e1000_write_nvm_microwire;
	nvm->ops.update		= e1000_update_nvm_checksum_generic;
	nvm->ops.validate	= e1000_validate_nvm_checksum_generic;

	return E1000_SUCCESS;
}

/**
 *  e1000_init_mac_params_82542 - Init MAC func ptrs.
 *  @hw: pointer to the HW structure
 **/
static s32 e1000_init_mac_params_82542(struct e1000_hw *hw)
{
	struct e1000_mac_info *mac = &hw->mac;

	DEBUGFUNC("e1000_init_mac_params_82542");

	/* Set media type */
	hw->phy.media_type = e1000_media_type_fiber;

	/* Set mta register count */
	mac->mta_reg_count = 128;
	/* Set rar entry count */
	mac->rar_entry_count = E1000_RAR_ENTRIES;

	/* Function pointers */

	/* bus type/speed/width */
	mac->ops.get_bus_info = e1000_get_bus_info_82542;
	/* function id */
	mac->ops.set_lan_id = e1000_set_lan_id_multi_port_pci;
	/* reset */
	mac->ops.reset_hw = e1000_reset_hw_82542;
	/* hw initialization */
	mac->ops.init_hw = e1000_init_hw_82542;
	/* link setup */
	mac->ops.setup_link = e1000_setup_link_82542;
	/* phy/fiber/serdes setup */
	mac->ops.setup_physical_interface =
					e1000_setup_fiber_serdes_link_generic;
	/* check for link */
	mac->ops.check_for_link = e1000_check_for_fiber_link_generic;
	/* multicast address update */
	mac->ops.update_mc_addr_list = e1000_update_mc_addr_list_generic;
	/* writing VFTA */
	mac->ops.write_vfta = e1000_write_vfta_generic;
	/* clearing VFTA */
	mac->ops.clear_vfta = e1000_clear_vfta_generic;
	/* read mac address */
	mac->ops.read_mac_addr = e1000_read_mac_addr_82542;
	/* set RAR */
	mac->ops.rar_set = e1000_rar_set_82542;
	/* turn on/off LED */
	mac->ops.led_on = e1000_led_on_82542;
	mac->ops.led_off = e1000_led_off_82542;
	/* clear hardware counters */
	mac->ops.clear_hw_cntrs = e1000_clear_hw_cntrs_82542;
	/* link info */
	mac->ops.get_link_up_info =
				e1000_get_speed_and_duplex_fiber_serdes_generic;

	return E1000_SUCCESS;
}

/**
 *  e1000_init_function_pointers_82542 - Init func ptrs.
 *  @hw: pointer to the HW structure
 *
 *  Called to initialize all function pointers and parameters.
 **/
void e1000_init_function_pointers_82542(struct e1000_hw *hw)
{
	DEBUGFUNC("e1000_init_function_pointers_82542");

	hw->mac.ops.init_params = e1000_init_mac_params_82542;
	hw->nvm.ops.init_params = e1000_init_nvm_params_82542;
	hw->phy.ops.init_params = e1000_init_phy_params_82542;
}

/**
 *  e1000_get_bus_info_82542 - Obtain bus information for adapter
 *  @hw: pointer to the HW structure
 *
 *  This will obtain information about the HW bus for which the
 *  adapter is attached and stores it in the hw structure.
 **/
static s32 e1000_get_bus_info_82542(struct e1000_hw *hw)
{
	DEBUGFUNC("e1000_get_bus_info_82542");

	hw->bus.type = e1000_bus_type_pci;
	hw->bus.speed = e1000_bus_speed_unknown;
	hw->bus.width = e1000_bus_width_unknown;

	return E1000_SUCCESS;
}

/**
 *  e1000_reset_hw_82542 - Reset hardware
 *  @hw: pointer to the HW structure
 *
 *  This resets the hardware into a known state.
 **/
static s32 e1000_reset_hw_82542(struct e1000_hw *hw)
{
	struct e1000_bus_info *bus = &hw->bus;
	s32 ret_val = E1000_SUCCESS;
	u32 ctrl;

	DEBUGFUNC("e1000_reset_hw_82542");

	if (hw->revision_id == E1000_REVISION_2) {
		DEBUGOUT("Disabling MWI on 82542 rev 2\n");
		e1000_pci_clear_mwi(hw);
	}

	DEBUGOUT("Masking off all interrupts\n");
	E1000_WRITE_REG(hw, E1000_IMC, 0xffffffff);

	E1000_WRITE_REG(hw, E1000_RCTL, 0);
	E1000_WRITE_REG(hw, E1000_TCTL, E1000_TCTL_PSP);
	E1000_WRITE_FLUSH(hw);

	/*
	 * Delay to allow any outstanding PCI transactions to complete before
	 * resetting the device
	 */
	msec_delay(10);

	ctrl = E1000_READ_REG(hw, E1000_CTRL);

	DEBUGOUT("Issuing a global reset to 82542/82543 MAC\n");
	E1000_WRITE_REG(hw, E1000_CTRL, ctrl | E1000_CTRL_RST);

	hw->nvm.ops.reload(hw);
	msec_delay(2);

	E1000_WRITE_REG(hw, E1000_IMC, 0xffffffff);
	E1000_READ_REG(hw, E1000_ICR);

	if (hw->revision_id == E1000_REVISION_2) {
		if (bus->pci_cmd_word & CMD_MEM_WRT_INVALIDATE)
			e1000_pci_set_mwi(hw);
	}

	return ret_val;
}

/**
 *  e1000_init_hw_82542 - Initialize hardware
 *  @hw: pointer to the HW structure
 *
 *  This inits the hardware readying it for operation.
 **/
static s32 e1000_init_hw_82542(struct e1000_hw *hw)
{
	struct e1000_mac_info *mac = &hw->mac;
	struct e1000_dev_spec_82542 *dev_spec = &hw->dev_spec._82542;
	s32 ret_val = E1000_SUCCESS;
	u32 ctrl;
	u16 i;

	DEBUGFUNC("e1000_init_hw_82542");

	/* Disabling VLAN filtering */
	E1000_WRITE_REG(hw, E1000_VET, 0);
	mac->ops.clear_vfta(hw);

	/* For 82542 (rev 2.0), disable MWI and put the receiver into reset */
	if (hw->revision_id == E1000_REVISION_2) {
		DEBUGOUT("Disabling MWI on 82542 rev 2.0\n");
		e1000_pci_clear_mwi(hw);
		E1000_WRITE_REG(hw, E1000_RCTL, E1000_RCTL_RST);
		E1000_WRITE_FLUSH(hw);
		msec_delay(5);
	}

	/* Setup the receive address. */
	e1000_init_rx_addrs_generic(hw, mac->rar_entry_count);

	/* For 82542 (rev 2.0), take the receiver out of reset and enable MWI */
	if (hw->revision_id == E1000_REVISION_2) {
		E1000_WRITE_REG(hw, E1000_RCTL, 0);
		E1000_WRITE_FLUSH(hw);
		msec_delay(1);
		if (hw->bus.pci_cmd_word & CMD_MEM_WRT_INVALIDATE)
			e1000_pci_set_mwi(hw);
	}

	/* Zero out the Multicast HASH table */
	DEBUGOUT("Zeroing the MTA\n");
	for (i = 0; i < mac->mta_reg_count; i++)
		E1000_WRITE_REG_ARRAY(hw, E1000_MTA, i, 0);

	/*
	 * Set the PCI priority bit correctly in the CTRL register.  This
	 * determines if the adapter gives priority to receives, or if it
	 * gives equal priority to transmits and receives.
	 */
	if (dev_spec->dma_fairness) {
		ctrl = E1000_READ_REG(hw, E1000_CTRL);
		E1000_WRITE_REG(hw, E1000_CTRL, ctrl | E1000_CTRL_PRIOR);
	}

	/* Setup link and flow control */
	ret_val = e1000_setup_link_82542(hw);

	/*
	 * Clear all of the statistics registers (clear on read).  It is
	 * important that we do this after we have tried to establish link
	 * because the symbol error count will increment wildly if there
	 * is no link.
	 */
	e1000_clear_hw_cntrs_82542(hw);

	return ret_val;
}

/**
 *  e1000_setup_link_82542 - Setup flow control and link settings
 *  @hw: pointer to the HW structure
 *
 *  Determines which flow control settings to use, then configures flow
 *  control.  Calls the appropriate media-specific link configuration
 *  function.  Assuming the adapter has a valid link partner, a valid link
 *  should be established.  Assumes the hardware has previously been reset
 *  and the transmitter and receiver are not enabled.
 **/
static s32 e1000_setup_link_82542(struct e1000_hw *hw)
{
	struct e1000_mac_info *mac = &hw->mac;
	s32 ret_val;

	DEBUGFUNC("e1000_setup_link_82542");

	ret_val = e1000_set_default_fc_generic(hw);
	if (ret_val)
		goto out;

	hw->fc.requested_mode &= ~e1000_fc_tx_pause;

	if (mac->report_tx_early)
		hw->fc.requested_mode &= ~e1000_fc_rx_pause;

	/*
	 * Save off the requested flow control mode for use later.  Depending
	 * on the link partner's capabilities, we may or may not use this mode.
	 */
	hw->fc.current_mode = hw->fc.requested_mode;

	DEBUGOUT1("After fix-ups FlowControl is now = %x\n",
		  hw->fc.current_mode);

	/* Call the necessary subroutine to configure the link. */
	ret_val = mac->ops.setup_physical_interface(hw);
	if (ret_val)
		goto out;

	/*
	 * Initialize the flow control address, type, and PAUSE timer
	 * registers to their default values.  This is done even if flow
	 * control is disabled, because it does not hurt anything to
	 * initialize these registers.
	 */
	DEBUGOUT("Initializing Flow Control address, type and timer regs\n");

	E1000_WRITE_REG(hw, E1000_FCAL, FLOW_CONTROL_ADDRESS_LOW);
	E1000_WRITE_REG(hw, E1000_FCAH, FLOW_CONTROL_ADDRESS_HIGH);
	E1000_WRITE_REG(hw, E1000_FCT, FLOW_CONTROL_TYPE);

	E1000_WRITE_REG(hw, E1000_FCTTV, hw->fc.pause_time);

	ret_val = e1000_set_fc_watermarks_generic(hw);

out:
	return ret_val;
}

/**
 *  e1000_led_on_82542 - Turn on SW controllable LED
 *  @hw: pointer to the HW structure
 *
 *  Turns the SW defined LED on.
 **/
static s32 e1000_led_on_82542(struct e1000_hw *hw)
{
	u32 ctrl = E1000_READ_REG(hw, E1000_CTRL);

	DEBUGFUNC("e1000_led_on_82542");

	ctrl |= E1000_CTRL_SWDPIN0;
	ctrl |= E1000_CTRL_SWDPIO0;
	E1000_WRITE_REG(hw, E1000_CTRL, ctrl);

	return E1000_SUCCESS;
}

/**
 *  e1000_led_off_82542 - Turn off SW controllable LED
 *  @hw: pointer to the HW structure
 *
 *  Turns the SW defined LED off.
 **/
static s32 e1000_led_off_82542(struct e1000_hw *hw)
{
	u32 ctrl = E1000_READ_REG(hw, E1000_CTRL);

	DEBUGFUNC("e1000_led_off_82542");

	ctrl &= ~E1000_CTRL_SWDPIN0;
	ctrl |= E1000_CTRL_SWDPIO0;
	E1000_WRITE_REG(hw, E1000_CTRL, ctrl);

	return E1000_SUCCESS;
}

/**
 *  e1000_rar_set_82542 - Set receive address register
 *  @hw: pointer to the HW structure
 *  @addr: pointer to the receive address
 *  @index: receive address array register
 *
 *  Sets the receive address array register at index to the address passed
 *  in by addr.
 **/
static int e1000_rar_set_82542(struct e1000_hw *hw, u8 *addr, u32 index)
{
	u32 rar_low, rar_high;

	DEBUGFUNC("e1000_rar_set_82542");

	/*
	 * HW expects these in little endian so we reverse the byte order
	 * from network order (big endian) to little endian
	 */
	rar_low = ((u32) addr[0] | ((u32) addr[1] << 8) |
		   ((u32) addr[2] << 16) | ((u32) addr[3] << 24));

	rar_high = ((u32) addr[4] | ((u32) addr[5] << 8));

	/* If MAC address zero, no need to set the AV bit */
	if (rar_low || rar_high)
		rar_high |= E1000_RAH_AV;

	E1000_WRITE_REG_ARRAY(hw, E1000_RA, (index << 1), rar_low);
	E1000_WRITE_REG_ARRAY(hw, E1000_RA, ((index << 1) + 1), rar_high);

	return E1000_SUCCESS;
}

/**
 *  e1000_translate_register_82542 - Translate the proper register offset
 *  @reg: e1000 register to be read
 *
 *  Registers in 82542 are located in different offsets than other adapters
 *  even though they function in the same manner.  This function takes in
 *  the name of the register to read and returns the correct offset for
 *  82542 silicon.
 **/
u32 e1000_translate_register_82542(u32 reg)
{
	/*
	 * Some of the 82542 registers are located at different
	 * offsets than they are in newer adapters.
	 * Despite the difference in location, the registers
	 * function in the same manner.
	 */
	switch (reg) {
	case E1000_RA:
		reg = 0x00040;
		break;
	case E1000_RDTR:
		reg = 0x00108;
		break;
	case E1000_RDBAL(0):
		reg = 0x00110;
		break;
	case E1000_RDBAH(0):
		reg = 0x00114;
		break;
	case E1000_RDLEN(0):
		reg = 0x00118;
		break;
	case E1000_RDH(0):
		reg = 0x00120;
		break;
	case E1000_RDT(0):
		reg = 0x00128;
		break;
	case E1000_RDBAL(1):
		reg = 0x00138;
		break;
	case E1000_RDBAH(1):
		reg = 0x0013C;
		break;
	case E1000_RDLEN(1):
		reg = 0x00140;
		break;
	case E1000_RDH(1):
		reg = 0x00148;
		break;
	case E1000_RDT(1):
		reg = 0x00150;
		break;
	case E1000_FCRTH:
		reg = 0x00160;
		break;
	case E1000_FCRTL:
		reg = 0x00168;
		break;
	case E1000_MTA:
		reg = 0x00200;
		break;
	case E1000_TDBAL(0):
		reg = 0x00420;
		break;
	case E1000_TDBAH(0):
		reg = 0x00424;
		break;
	case E1000_TDLEN(0):
		reg = 0x00428;
		break;
	case E1000_TDH(0):
		reg = 0x00430;
		break;
	case E1000_TDT(0):
		reg = 0x00438;
		break;
	case E1000_TIDV:
		reg = 0x00440;
		break;
	case E1000_VFTA:
		reg = 0x00600;
		break;
	case E1000_TDFH:
		reg = 0x08010;
		break;
	case E1000_TDFT:
		reg = 0x08018;
		break;
	default:
		break;
	}

	return reg;
}

/**
 *  e1000_clear_hw_cntrs_82542 - Clear device specific hardware counters
 *  @hw: pointer to the HW structure
 *
 *  Clears the hardware counters by reading the counter registers.
 **/
static void e1000_clear_hw_cntrs_82542(struct e1000_hw *hw)
{
	DEBUGFUNC("e1000_clear_hw_cntrs_82542");

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
}

/**
 *  e1000_read_mac_addr_82542 - Read device MAC address
 *  @hw: pointer to the HW structure
 *
 *  Reads the device MAC address from the EEPROM and stores the value.
 **/
s32 e1000_read_mac_addr_82542(struct e1000_hw *hw)
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
