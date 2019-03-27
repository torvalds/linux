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
 * 82543GC Gigabit Ethernet Controller (Fiber)
 * 82543GC Gigabit Ethernet Controller (Copper)
 * 82544EI Gigabit Ethernet Controller (Copper)
 * 82544EI Gigabit Ethernet Controller (Fiber)
 * 82544GC Gigabit Ethernet Controller (Copper)
 * 82544GC Gigabit Ethernet Controller (LOM)
 */

#include "e1000_api.h"

static s32  e1000_init_phy_params_82543(struct e1000_hw *hw);
static s32  e1000_init_nvm_params_82543(struct e1000_hw *hw);
static s32  e1000_init_mac_params_82543(struct e1000_hw *hw);
static s32  e1000_read_phy_reg_82543(struct e1000_hw *hw, u32 offset,
				     u16 *data);
static s32  e1000_write_phy_reg_82543(struct e1000_hw *hw, u32 offset,
				      u16 data);
static s32  e1000_phy_force_speed_duplex_82543(struct e1000_hw *hw);
static s32  e1000_phy_hw_reset_82543(struct e1000_hw *hw);
static s32  e1000_reset_hw_82543(struct e1000_hw *hw);
static s32  e1000_init_hw_82543(struct e1000_hw *hw);
static s32  e1000_setup_link_82543(struct e1000_hw *hw);
static s32  e1000_setup_copper_link_82543(struct e1000_hw *hw);
static s32  e1000_setup_fiber_link_82543(struct e1000_hw *hw);
static s32  e1000_check_for_copper_link_82543(struct e1000_hw *hw);
static s32  e1000_check_for_fiber_link_82543(struct e1000_hw *hw);
static s32  e1000_led_on_82543(struct e1000_hw *hw);
static s32  e1000_led_off_82543(struct e1000_hw *hw);
static void e1000_write_vfta_82543(struct e1000_hw *hw, u32 offset,
				   u32 value);
static void e1000_clear_hw_cntrs_82543(struct e1000_hw *hw);
static s32  e1000_config_mac_to_phy_82543(struct e1000_hw *hw);
static bool e1000_init_phy_disabled_82543(struct e1000_hw *hw);
static void e1000_lower_mdi_clk_82543(struct e1000_hw *hw, u32 *ctrl);
static s32  e1000_polarity_reversal_workaround_82543(struct e1000_hw *hw);
static void e1000_raise_mdi_clk_82543(struct e1000_hw *hw, u32 *ctrl);
static u16  e1000_shift_in_mdi_bits_82543(struct e1000_hw *hw);
static void e1000_shift_out_mdi_bits_82543(struct e1000_hw *hw, u32 data,
					   u16 count);
static bool e1000_tbi_compatibility_enabled_82543(struct e1000_hw *hw);
static void e1000_set_tbi_sbp_82543(struct e1000_hw *hw, bool state);
static s32  e1000_read_mac_addr_82543(struct e1000_hw *hw);


/**
 *  e1000_init_phy_params_82543 - Init PHY func ptrs.
 *  @hw: pointer to the HW structure
 **/
static s32 e1000_init_phy_params_82543(struct e1000_hw *hw)
{
	struct e1000_phy_info *phy = &hw->phy;
	s32 ret_val = E1000_SUCCESS;

	DEBUGFUNC("e1000_init_phy_params_82543");

	if (hw->phy.media_type != e1000_media_type_copper) {
		phy->type = e1000_phy_none;
		goto out;
	} else {
		phy->ops.power_up = e1000_power_up_phy_copper;
		phy->ops.power_down = e1000_power_down_phy_copper;
	}

	phy->addr		= 1;
	phy->autoneg_mask	= AUTONEG_ADVERTISE_SPEED_DEFAULT;
	phy->reset_delay_us	= 10000;
	phy->type		= e1000_phy_m88;

	/* Function Pointers */
	phy->ops.check_polarity	= e1000_check_polarity_m88;
	phy->ops.commit		= e1000_phy_sw_reset_generic;
	phy->ops.force_speed_duplex = e1000_phy_force_speed_duplex_82543;
	phy->ops.get_cable_length = e1000_get_cable_length_m88;
	phy->ops.get_cfg_done	= e1000_get_cfg_done_generic;
	phy->ops.read_reg	= (hw->mac.type == e1000_82543)
				  ? e1000_read_phy_reg_82543
				  : e1000_read_phy_reg_m88;
	phy->ops.reset		= (hw->mac.type == e1000_82543)
				  ? e1000_phy_hw_reset_82543
				  : e1000_phy_hw_reset_generic;
	phy->ops.write_reg	= (hw->mac.type == e1000_82543)
				  ? e1000_write_phy_reg_82543
				  : e1000_write_phy_reg_m88;
	phy->ops.get_info	= e1000_get_phy_info_m88;

	/*
	 * The external PHY of the 82543 can be in a funky state.
	 * Resetting helps us read the PHY registers for acquiring
	 * the PHY ID.
	 */
	if (!e1000_init_phy_disabled_82543(hw)) {
		ret_val = phy->ops.reset(hw);
		if (ret_val) {
			DEBUGOUT("Resetting PHY during init failed.\n");
			goto out;
		}
		msec_delay(20);
	}

	ret_val = e1000_get_phy_id(hw);
	if (ret_val)
		goto out;

	/* Verify phy id */
	switch (hw->mac.type) {
	case e1000_82543:
		if (phy->id != M88E1000_E_PHY_ID) {
			ret_val = -E1000_ERR_PHY;
			goto out;
		}
		break;
	case e1000_82544:
		if (phy->id != M88E1000_I_PHY_ID) {
			ret_val = -E1000_ERR_PHY;
			goto out;
		}
		break;
	default:
		ret_val = -E1000_ERR_PHY;
		goto out;
		break;
	}

out:
	return ret_val;
}

/**
 *  e1000_init_nvm_params_82543 - Init NVM func ptrs.
 *  @hw: pointer to the HW structure
 **/
static s32 e1000_init_nvm_params_82543(struct e1000_hw *hw)
{
	struct e1000_nvm_info *nvm = &hw->nvm;

	DEBUGFUNC("e1000_init_nvm_params_82543");

	nvm->type		= e1000_nvm_eeprom_microwire;
	nvm->word_size		= 64;
	nvm->delay_usec		= 50;
	nvm->address_bits	=  6;
	nvm->opcode_bits	=  3;

	/* Function Pointers */
	nvm->ops.read		= e1000_read_nvm_microwire;
	nvm->ops.update		= e1000_update_nvm_checksum_generic;
	nvm->ops.valid_led_default = e1000_valid_led_default_generic;
	nvm->ops.validate	= e1000_validate_nvm_checksum_generic;
	nvm->ops.write		= e1000_write_nvm_microwire;

	return E1000_SUCCESS;
}

/**
 *  e1000_init_mac_params_82543 - Init MAC func ptrs.
 *  @hw: pointer to the HW structure
 **/
static s32 e1000_init_mac_params_82543(struct e1000_hw *hw)
{
	struct e1000_mac_info *mac = &hw->mac;

	DEBUGFUNC("e1000_init_mac_params_82543");

	/* Set media type */
	switch (hw->device_id) {
	case E1000_DEV_ID_82543GC_FIBER:
	case E1000_DEV_ID_82544EI_FIBER:
		hw->phy.media_type = e1000_media_type_fiber;
		break;
	default:
		hw->phy.media_type = e1000_media_type_copper;
		break;
	}

	/* Set mta register count */
	mac->mta_reg_count = 128;
	/* Set rar entry count */
	mac->rar_entry_count = E1000_RAR_ENTRIES;

	/* Function pointers */

	/* bus type/speed/width */
	mac->ops.get_bus_info = e1000_get_bus_info_pci_generic;
	/* function id */
	mac->ops.set_lan_id = e1000_set_lan_id_multi_port_pci;
	/* reset */
	mac->ops.reset_hw = e1000_reset_hw_82543;
	/* hw initialization */
	mac->ops.init_hw = e1000_init_hw_82543;
	/* link setup */
	mac->ops.setup_link = e1000_setup_link_82543;
	/* physical interface setup */
	mac->ops.setup_physical_interface =
		(hw->phy.media_type == e1000_media_type_copper)
		 ? e1000_setup_copper_link_82543 : e1000_setup_fiber_link_82543;
	/* check for link */
	mac->ops.check_for_link =
		(hw->phy.media_type == e1000_media_type_copper)
		 ? e1000_check_for_copper_link_82543
		 : e1000_check_for_fiber_link_82543;
	/* link info */
	mac->ops.get_link_up_info =
		(hw->phy.media_type == e1000_media_type_copper)
		 ? e1000_get_speed_and_duplex_copper_generic
		 : e1000_get_speed_and_duplex_fiber_serdes_generic;
	/* multicast address update */
	mac->ops.update_mc_addr_list = e1000_update_mc_addr_list_generic;
	/* writing VFTA */
	mac->ops.write_vfta = e1000_write_vfta_82543;
	/* clearing VFTA */
	mac->ops.clear_vfta = e1000_clear_vfta_generic;
	/* read mac address */
	mac->ops.read_mac_addr = e1000_read_mac_addr_82543;
	/* turn on/off LED */
	mac->ops.led_on = e1000_led_on_82543;
	mac->ops.led_off = e1000_led_off_82543;
	/* clear hardware counters */
	mac->ops.clear_hw_cntrs = e1000_clear_hw_cntrs_82543;

	/* Set tbi compatibility */
	if ((hw->mac.type != e1000_82543) ||
	    (hw->phy.media_type == e1000_media_type_fiber))
		e1000_set_tbi_compatibility_82543(hw, FALSE);

	return E1000_SUCCESS;
}

/**
 *  e1000_init_function_pointers_82543 - Init func ptrs.
 *  @hw: pointer to the HW structure
 *
 *  Called to initialize all function pointers and parameters.
 **/
void e1000_init_function_pointers_82543(struct e1000_hw *hw)
{
	DEBUGFUNC("e1000_init_function_pointers_82543");

	hw->mac.ops.init_params = e1000_init_mac_params_82543;
	hw->nvm.ops.init_params = e1000_init_nvm_params_82543;
	hw->phy.ops.init_params = e1000_init_phy_params_82543;
}

/**
 *  e1000_tbi_compatibility_enabled_82543 - Returns TBI compat status
 *  @hw: pointer to the HW structure
 *
 *  Returns the current status of 10-bit Interface (TBI) compatibility
 *  (enabled/disabled).
 **/
static bool e1000_tbi_compatibility_enabled_82543(struct e1000_hw *hw)
{
	struct e1000_dev_spec_82543 *dev_spec = &hw->dev_spec._82543;
	bool state = FALSE;

	DEBUGFUNC("e1000_tbi_compatibility_enabled_82543");

	if (hw->mac.type != e1000_82543) {
		DEBUGOUT("TBI compatibility workaround for 82543 only.\n");
		goto out;
	}

	state = !!(dev_spec->tbi_compatibility & TBI_COMPAT_ENABLED);

out:
	return state;
}

/**
 *  e1000_set_tbi_compatibility_82543 - Set TBI compatibility
 *  @hw: pointer to the HW structure
 *  @state: enable/disable TBI compatibility
 *
 *  Enables or disabled 10-bit Interface (TBI) compatibility.
 **/
void e1000_set_tbi_compatibility_82543(struct e1000_hw *hw, bool state)
{
	struct e1000_dev_spec_82543 *dev_spec = &hw->dev_spec._82543;

	DEBUGFUNC("e1000_set_tbi_compatibility_82543");

	if (hw->mac.type != e1000_82543) {
		DEBUGOUT("TBI compatibility workaround for 82543 only.\n");
		goto out;
	}

	if (state)
		dev_spec->tbi_compatibility |= TBI_COMPAT_ENABLED;
	else
		dev_spec->tbi_compatibility &= ~TBI_COMPAT_ENABLED;

out:
	return;
}

/**
 *  e1000_tbi_sbp_enabled_82543 - Returns TBI SBP status
 *  @hw: pointer to the HW structure
 *
 *  Returns the current status of 10-bit Interface (TBI) store bad packet (SBP)
 *  (enabled/disabled).
 **/
bool e1000_tbi_sbp_enabled_82543(struct e1000_hw *hw)
{
	struct e1000_dev_spec_82543 *dev_spec = &hw->dev_spec._82543;
	bool state = FALSE;

	DEBUGFUNC("e1000_tbi_sbp_enabled_82543");

	if (hw->mac.type != e1000_82543) {
		DEBUGOUT("TBI compatibility workaround for 82543 only.\n");
		goto out;
	}

	state = !!(dev_spec->tbi_compatibility & TBI_SBP_ENABLED);

out:
	return state;
}

/**
 *  e1000_set_tbi_sbp_82543 - Set TBI SBP
 *  @hw: pointer to the HW structure
 *  @state: enable/disable TBI store bad packet
 *
 *  Enables or disabled 10-bit Interface (TBI) store bad packet (SBP).
 **/
static void e1000_set_tbi_sbp_82543(struct e1000_hw *hw, bool state)
{
	struct e1000_dev_spec_82543 *dev_spec = &hw->dev_spec._82543;

	DEBUGFUNC("e1000_set_tbi_sbp_82543");

	if (state && e1000_tbi_compatibility_enabled_82543(hw))
		dev_spec->tbi_compatibility |= TBI_SBP_ENABLED;
	else
		dev_spec->tbi_compatibility &= ~TBI_SBP_ENABLED;

	return;
}

/**
 *  e1000_init_phy_disabled_82543 - Returns init PHY status
 *  @hw: pointer to the HW structure
 *
 *  Returns the current status of whether PHY initialization is disabled.
 *  True if PHY initialization is disabled else FALSE.
 **/
static bool e1000_init_phy_disabled_82543(struct e1000_hw *hw)
{
	struct e1000_dev_spec_82543 *dev_spec = &hw->dev_spec._82543;
	bool ret_val;

	DEBUGFUNC("e1000_init_phy_disabled_82543");

	if (hw->mac.type != e1000_82543) {
		ret_val = FALSE;
		goto out;
	}

	ret_val = dev_spec->init_phy_disabled;

out:
	return ret_val;
}

/**
 *  e1000_tbi_adjust_stats_82543 - Adjust stats when TBI enabled
 *  @hw: pointer to the HW structure
 *  @stats: Struct containing statistic register values
 *  @frame_len: The length of the frame in question
 *  @mac_addr: The Ethernet destination address of the frame in question
 *  @max_frame_size: The maximum frame size
 *
 *  Adjusts the statistic counters when a frame is accepted by TBI_ACCEPT
 **/
void e1000_tbi_adjust_stats_82543(struct e1000_hw *hw,
				  struct e1000_hw_stats *stats, u32 frame_len,
				  u8 *mac_addr, u32 max_frame_size)
{
	if (!(e1000_tbi_sbp_enabled_82543(hw)))
		goto out;

	/* First adjust the frame length. */
	frame_len--;
	/*
	 * We need to adjust the statistics counters, since the hardware
	 * counters overcount this packet as a CRC error and undercount
	 * the packet as a good packet
	 */
	/* This packet should not be counted as a CRC error. */
	stats->crcerrs--;
	/* This packet does count as a Good Packet Received. */
	stats->gprc++;

	/* Adjust the Good Octets received counters */
	stats->gorc += frame_len;

	/*
	 * Is this a broadcast or multicast?  Check broadcast first,
	 * since the test for a multicast frame will test positive on
	 * a broadcast frame.
	 */
	if ((mac_addr[0] == 0xff) && (mac_addr[1] == 0xff))
		/* Broadcast packet */
		stats->bprc++;
	else if (*mac_addr & 0x01)
		/* Multicast packet */
		stats->mprc++;

	/*
	 * In this case, the hardware has over counted the number of
	 * oversize frames.
	 */
	if ((frame_len == max_frame_size) && (stats->roc > 0))
		stats->roc--;

	/*
	 * Adjust the bin counters when the extra byte put the frame in the
	 * wrong bin. Remember that the frame_len was adjusted above.
	 */
	if (frame_len == 64) {
		stats->prc64++;
		stats->prc127--;
	} else if (frame_len == 127) {
		stats->prc127++;
		stats->prc255--;
	} else if (frame_len == 255) {
		stats->prc255++;
		stats->prc511--;
	} else if (frame_len == 511) {
		stats->prc511++;
		stats->prc1023--;
	} else if (frame_len == 1023) {
		stats->prc1023++;
		stats->prc1522--;
	} else if (frame_len == 1522) {
		stats->prc1522++;
	}

out:
	return;
}

/**
 *  e1000_read_phy_reg_82543 - Read PHY register
 *  @hw: pointer to the HW structure
 *  @offset: register offset to be read
 *  @data: pointer to the read data
 *
 *  Reads the PHY at offset and stores the information read to data.
 **/
static s32 e1000_read_phy_reg_82543(struct e1000_hw *hw, u32 offset, u16 *data)
{
	u32 mdic;
	s32 ret_val = E1000_SUCCESS;

	DEBUGFUNC("e1000_read_phy_reg_82543");

	if (offset > MAX_PHY_REG_ADDRESS) {
		DEBUGOUT1("PHY Address %d is out of range\n", offset);
		ret_val = -E1000_ERR_PARAM;
		goto out;
	}

	/*
	 * We must first send a preamble through the MDIO pin to signal the
	 * beginning of an MII instruction.  This is done by sending 32
	 * consecutive "1" bits.
	 */
	e1000_shift_out_mdi_bits_82543(hw, PHY_PREAMBLE, PHY_PREAMBLE_SIZE);

	/*
	 * Now combine the next few fields that are required for a read
	 * operation.  We use this method instead of calling the
	 * e1000_shift_out_mdi_bits routine five different times.  The format
	 * of an MII read instruction consists of a shift out of 14 bits and
	 * is defined as follows:
	 *         <Preamble><SOF><Op Code><Phy Addr><Offset>
	 * followed by a shift in of 18 bits.  This first two bits shifted in
	 * are TurnAround bits used to avoid contention on the MDIO pin when a
	 * READ operation is performed.  These two bits are thrown away
	 * followed by a shift in of 16 bits which contains the desired data.
	 */
	mdic = (offset | (hw->phy.addr << 5) |
		(PHY_OP_READ << 10) | (PHY_SOF << 12));

	e1000_shift_out_mdi_bits_82543(hw, mdic, 14);

	/*
	 * Now that we've shifted out the read command to the MII, we need to
	 * "shift in" the 16-bit value (18 total bits) of the requested PHY
	 * register address.
	 */
	*data = e1000_shift_in_mdi_bits_82543(hw);

out:
	return ret_val;
}

/**
 *  e1000_write_phy_reg_82543 - Write PHY register
 *  @hw: pointer to the HW structure
 *  @offset: register offset to be written
 *  @data: pointer to the data to be written at offset
 *
 *  Writes data to the PHY at offset.
 **/
static s32 e1000_write_phy_reg_82543(struct e1000_hw *hw, u32 offset, u16 data)
{
	u32 mdic;
	s32 ret_val = E1000_SUCCESS;

	DEBUGFUNC("e1000_write_phy_reg_82543");

	if (offset > MAX_PHY_REG_ADDRESS) {
		DEBUGOUT1("PHY Address %d is out of range\n", offset);
		ret_val = -E1000_ERR_PARAM;
		goto out;
	}

	/*
	 * We'll need to use the SW defined pins to shift the write command
	 * out to the PHY. We first send a preamble to the PHY to signal the
	 * beginning of the MII instruction.  This is done by sending 32
	 * consecutive "1" bits.
	 */
	e1000_shift_out_mdi_bits_82543(hw, PHY_PREAMBLE, PHY_PREAMBLE_SIZE);

	/*
	 * Now combine the remaining required fields that will indicate a
	 * write operation. We use this method instead of calling the
	 * e1000_shift_out_mdi_bits routine for each field in the command. The
	 * format of a MII write instruction is as follows:
	 * <Preamble><SOF><Op Code><Phy Addr><Reg Addr><Turnaround><Data>.
	 */
	mdic = ((PHY_TURNAROUND) | (offset << 2) | (hw->phy.addr << 7) |
		(PHY_OP_WRITE << 12) | (PHY_SOF << 14));
	mdic <<= 16;
	mdic |= (u32)data;

	e1000_shift_out_mdi_bits_82543(hw, mdic, 32);

out:
	return ret_val;
}

/**
 *  e1000_raise_mdi_clk_82543 - Raise Management Data Input clock
 *  @hw: pointer to the HW structure
 *  @ctrl: pointer to the control register
 *
 *  Raise the management data input clock by setting the MDC bit in the control
 *  register.
 **/
static void e1000_raise_mdi_clk_82543(struct e1000_hw *hw, u32 *ctrl)
{
	/*
	 * Raise the clock input to the Management Data Clock (by setting the
	 * MDC bit), and then delay a sufficient amount of time.
	 */
	E1000_WRITE_REG(hw, E1000_CTRL, (*ctrl | E1000_CTRL_MDC));
	E1000_WRITE_FLUSH(hw);
	usec_delay(10);
}

/**
 *  e1000_lower_mdi_clk_82543 - Lower Management Data Input clock
 *  @hw: pointer to the HW structure
 *  @ctrl: pointer to the control register
 *
 *  Lower the management data input clock by clearing the MDC bit in the
 *  control register.
 **/
static void e1000_lower_mdi_clk_82543(struct e1000_hw *hw, u32 *ctrl)
{
	/*
	 * Lower the clock input to the Management Data Clock (by clearing the
	 * MDC bit), and then delay a sufficient amount of time.
	 */
	E1000_WRITE_REG(hw, E1000_CTRL, (*ctrl & ~E1000_CTRL_MDC));
	E1000_WRITE_FLUSH(hw);
	usec_delay(10);
}

/**
 *  e1000_shift_out_mdi_bits_82543 - Shift data bits our to the PHY
 *  @hw: pointer to the HW structure
 *  @data: data to send to the PHY
 *  @count: number of bits to shift out
 *
 *  We need to shift 'count' bits out to the PHY.  So, the value in the
 *  "data" parameter will be shifted out to the PHY one bit at a time.
 *  In order to do this, "data" must be broken down into bits.
 **/
static void e1000_shift_out_mdi_bits_82543(struct e1000_hw *hw, u32 data,
					   u16 count)
{
	u32 ctrl, mask;

	/*
	 * We need to shift "count" number of bits out to the PHY.  So, the
	 * value in the "data" parameter will be shifted out to the PHY one
	 * bit at a time.  In order to do this, "data" must be broken down
	 * into bits.
	 */
	mask = 0x01;
	mask <<= (count - 1);

	ctrl = E1000_READ_REG(hw, E1000_CTRL);

	/* Set MDIO_DIR and MDC_DIR direction bits to be used as output pins. */
	ctrl |= (E1000_CTRL_MDIO_DIR | E1000_CTRL_MDC_DIR);

	while (mask) {
		/*
		 * A "1" is shifted out to the PHY by setting the MDIO bit to
		 * "1" and then raising and lowering the Management Data Clock.
		 * A "0" is shifted out to the PHY by setting the MDIO bit to
		 * "0" and then raising and lowering the clock.
		 */
		if (data & mask)
			ctrl |= E1000_CTRL_MDIO;
		else
			ctrl &= ~E1000_CTRL_MDIO;

		E1000_WRITE_REG(hw, E1000_CTRL, ctrl);
		E1000_WRITE_FLUSH(hw);

		usec_delay(10);

		e1000_raise_mdi_clk_82543(hw, &ctrl);
		e1000_lower_mdi_clk_82543(hw, &ctrl);

		mask >>= 1;
	}
}

/**
 *  e1000_shift_in_mdi_bits_82543 - Shift data bits in from the PHY
 *  @hw: pointer to the HW structure
 *
 *  In order to read a register from the PHY, we need to shift 18 bits
 *  in from the PHY.  Bits are "shifted in" by raising the clock input to
 *  the PHY (setting the MDC bit), and then reading the value of the data out
 *  MDIO bit.
 **/
static u16 e1000_shift_in_mdi_bits_82543(struct e1000_hw *hw)
{
	u32 ctrl;
	u16 data = 0;
	u8 i;

	/*
	 * In order to read a register from the PHY, we need to shift in a
	 * total of 18 bits from the PHY.  The first two bit (turnaround)
	 * times are used to avoid contention on the MDIO pin when a read
	 * operation is performed.  These two bits are ignored by us and
	 * thrown away.  Bits are "shifted in" by raising the input to the
	 * Management Data Clock (setting the MDC bit) and then reading the
	 * value of the MDIO bit.
	 */
	ctrl = E1000_READ_REG(hw, E1000_CTRL);

	/*
	 * Clear MDIO_DIR (SWDPIO1) to indicate this bit is to be used as
	 * input.
	 */
	ctrl &= ~E1000_CTRL_MDIO_DIR;
	ctrl &= ~E1000_CTRL_MDIO;

	E1000_WRITE_REG(hw, E1000_CTRL, ctrl);
	E1000_WRITE_FLUSH(hw);

	/*
	 * Raise and lower the clock before reading in the data.  This accounts
	 * for the turnaround bits.  The first clock occurred when we clocked
	 * out the last bit of the Register Address.
	 */
	e1000_raise_mdi_clk_82543(hw, &ctrl);
	e1000_lower_mdi_clk_82543(hw, &ctrl);

	for (data = 0, i = 0; i < 16; i++) {
		data <<= 1;
		e1000_raise_mdi_clk_82543(hw, &ctrl);
		ctrl = E1000_READ_REG(hw, E1000_CTRL);
		/* Check to see if we shifted in a "1". */
		if (ctrl & E1000_CTRL_MDIO)
			data |= 1;
		e1000_lower_mdi_clk_82543(hw, &ctrl);
	}

	e1000_raise_mdi_clk_82543(hw, &ctrl);
	e1000_lower_mdi_clk_82543(hw, &ctrl);

	return data;
}

/**
 *  e1000_phy_force_speed_duplex_82543 - Force speed/duplex for PHY
 *  @hw: pointer to the HW structure
 *
 *  Calls the function to force speed and duplex for the m88 PHY, and
 *  if the PHY is not auto-negotiating and the speed is forced to 10Mbit,
 *  then call the function for polarity reversal workaround.
 **/
static s32 e1000_phy_force_speed_duplex_82543(struct e1000_hw *hw)
{
	s32 ret_val;

	DEBUGFUNC("e1000_phy_force_speed_duplex_82543");

	ret_val = e1000_phy_force_speed_duplex_m88(hw);
	if (ret_val)
		goto out;

	if (!hw->mac.autoneg && (hw->mac.forced_speed_duplex &
	    E1000_ALL_10_SPEED))
		ret_val = e1000_polarity_reversal_workaround_82543(hw);

out:
	return ret_val;
}

/**
 *  e1000_polarity_reversal_workaround_82543 - Workaround polarity reversal
 *  @hw: pointer to the HW structure
 *
 *  When forcing link to 10 Full or 10 Half, the PHY can reverse the polarity
 *  inadvertently.  To workaround the issue, we disable the transmitter on
 *  the PHY until we have established the link partner's link parameters.
 **/
static s32 e1000_polarity_reversal_workaround_82543(struct e1000_hw *hw)
{
	s32 ret_val = E1000_SUCCESS;
	u16 mii_status_reg;
	u16 i;
	bool link;

	if (!(hw->phy.ops.write_reg))
		goto out;

	/* Polarity reversal workaround for forced 10F/10H links. */

	/* Disable the transmitter on the PHY */

	ret_val = hw->phy.ops.write_reg(hw, M88E1000_PHY_PAGE_SELECT, 0x0019);
	if (ret_val)
		goto out;
	ret_val = hw->phy.ops.write_reg(hw, M88E1000_PHY_GEN_CONTROL, 0xFFFF);
	if (ret_val)
		goto out;

	ret_val = hw->phy.ops.write_reg(hw, M88E1000_PHY_PAGE_SELECT, 0x0000);
	if (ret_val)
		goto out;

	/*
	 * This loop will early-out if the NO link condition has been met.
	 * In other words, DO NOT use e1000_phy_has_link_generic() here.
	 */
	for (i = PHY_FORCE_TIME; i > 0; i--) {
		/*
		 * Read the MII Status Register and wait for Link Status bit
		 * to be clear.
		 */

		ret_val = hw->phy.ops.read_reg(hw, PHY_STATUS, &mii_status_reg);
		if (ret_val)
			goto out;

		ret_val = hw->phy.ops.read_reg(hw, PHY_STATUS, &mii_status_reg);
		if (ret_val)
			goto out;

		if (!(mii_status_reg & ~MII_SR_LINK_STATUS))
			break;
		msec_delay_irq(100);
	}

	/* Recommended delay time after link has been lost */
	msec_delay_irq(1000);

	/* Now we will re-enable the transmitter on the PHY */

	ret_val = hw->phy.ops.write_reg(hw, M88E1000_PHY_PAGE_SELECT, 0x0019);
	if (ret_val)
		goto out;
	msec_delay_irq(50);
	ret_val = hw->phy.ops.write_reg(hw, M88E1000_PHY_GEN_CONTROL, 0xFFF0);
	if (ret_val)
		goto out;
	msec_delay_irq(50);
	ret_val = hw->phy.ops.write_reg(hw, M88E1000_PHY_GEN_CONTROL, 0xFF00);
	if (ret_val)
		goto out;
	msec_delay_irq(50);
	ret_val = hw->phy.ops.write_reg(hw, M88E1000_PHY_GEN_CONTROL, 0x0000);
	if (ret_val)
		goto out;

	ret_val = hw->phy.ops.write_reg(hw, M88E1000_PHY_PAGE_SELECT, 0x0000);
	if (ret_val)
		goto out;

	/*
	 * Read the MII Status Register and wait for Link Status bit
	 * to be set.
	 */
	ret_val = e1000_phy_has_link_generic(hw, PHY_FORCE_TIME, 100000, &link);
	if (ret_val)
		goto out;

out:
	return ret_val;
}

/**
 *  e1000_phy_hw_reset_82543 - PHY hardware reset
 *  @hw: pointer to the HW structure
 *
 *  Sets the PHY_RESET_DIR bit in the extended device control register
 *  to put the PHY into a reset and waits for completion.  Once the reset
 *  has been accomplished, clear the PHY_RESET_DIR bit to take the PHY out
 *  of reset.
 **/
static s32 e1000_phy_hw_reset_82543(struct e1000_hw *hw)
{
	u32 ctrl_ext;
	s32 ret_val;

	DEBUGFUNC("e1000_phy_hw_reset_82543");

	/*
	 * Read the Extended Device Control Register, assert the PHY_RESET_DIR
	 * bit to put the PHY into reset...
	 */
	ctrl_ext = E1000_READ_REG(hw, E1000_CTRL_EXT);
	ctrl_ext |= E1000_CTRL_EXT_SDP4_DIR;
	ctrl_ext &= ~E1000_CTRL_EXT_SDP4_DATA;
	E1000_WRITE_REG(hw, E1000_CTRL_EXT, ctrl_ext);
	E1000_WRITE_FLUSH(hw);

	msec_delay(10);

	/* ...then take it out of reset. */
	ctrl_ext |= E1000_CTRL_EXT_SDP4_DATA;
	E1000_WRITE_REG(hw, E1000_CTRL_EXT, ctrl_ext);
	E1000_WRITE_FLUSH(hw);

	usec_delay(150);

	if (!(hw->phy.ops.get_cfg_done))
		return E1000_SUCCESS;

	ret_val = hw->phy.ops.get_cfg_done(hw);

	return ret_val;
}

/**
 *  e1000_reset_hw_82543 - Reset hardware
 *  @hw: pointer to the HW structure
 *
 *  This resets the hardware into a known state.
 **/
static s32 e1000_reset_hw_82543(struct e1000_hw *hw)
{
	u32 ctrl;
	s32 ret_val = E1000_SUCCESS;

	DEBUGFUNC("e1000_reset_hw_82543");

	DEBUGOUT("Masking off all interrupts\n");
	E1000_WRITE_REG(hw, E1000_IMC, 0xffffffff);

	E1000_WRITE_REG(hw, E1000_RCTL, 0);
	E1000_WRITE_REG(hw, E1000_TCTL, E1000_TCTL_PSP);
	E1000_WRITE_FLUSH(hw);

	e1000_set_tbi_sbp_82543(hw, FALSE);

	/*
	 * Delay to allow any outstanding PCI transactions to complete before
	 * resetting the device
	 */
	msec_delay(10);

	ctrl = E1000_READ_REG(hw, E1000_CTRL);

	DEBUGOUT("Issuing a global reset to 82543/82544 MAC\n");
	if (hw->mac.type == e1000_82543) {
		E1000_WRITE_REG(hw, E1000_CTRL, ctrl | E1000_CTRL_RST);
	} else {
		/*
		 * The 82544 can't ACK the 64-bit write when issuing the
		 * reset, so use IO-mapping as a workaround.
		 */
		E1000_WRITE_REG_IO(hw, E1000_CTRL, ctrl | E1000_CTRL_RST);
	}

	/*
	 * After MAC reset, force reload of NVM to restore power-on
	 * settings to device.
	 */
	hw->nvm.ops.reload(hw);
	msec_delay(2);

	/* Masking off and clearing any pending interrupts */
	E1000_WRITE_REG(hw, E1000_IMC, 0xffffffff);
	E1000_READ_REG(hw, E1000_ICR);

	return ret_val;
}

/**
 *  e1000_init_hw_82543 - Initialize hardware
 *  @hw: pointer to the HW structure
 *
 *  This inits the hardware readying it for operation.
 **/
static s32 e1000_init_hw_82543(struct e1000_hw *hw)
{
	struct e1000_mac_info *mac = &hw->mac;
	struct e1000_dev_spec_82543 *dev_spec = &hw->dev_spec._82543;
	u32 ctrl;
	s32 ret_val;
	u16 i;

	DEBUGFUNC("e1000_init_hw_82543");

	/* Disabling VLAN filtering */
	E1000_WRITE_REG(hw, E1000_VET, 0);
	mac->ops.clear_vfta(hw);

	/* Setup the receive address. */
	e1000_init_rx_addrs_generic(hw, mac->rar_entry_count);

	/* Zero out the Multicast HASH table */
	DEBUGOUT("Zeroing the MTA\n");
	for (i = 0; i < mac->mta_reg_count; i++) {
		E1000_WRITE_REG_ARRAY(hw, E1000_MTA, i, 0);
		E1000_WRITE_FLUSH(hw);
	}

	/*
	 * Set the PCI priority bit correctly in the CTRL register.  This
	 * determines if the adapter gives priority to receives, or if it
	 * gives equal priority to transmits and receives.
	 */
	if (hw->mac.type == e1000_82543 && dev_spec->dma_fairness) {
		ctrl = E1000_READ_REG(hw, E1000_CTRL);
		E1000_WRITE_REG(hw, E1000_CTRL, ctrl | E1000_CTRL_PRIOR);
	}

	e1000_pcix_mmrbc_workaround_generic(hw);

	/* Setup link and flow control */
	ret_val = mac->ops.setup_link(hw);

	/*
	 * Clear all of the statistics registers (clear on read).  It is
	 * important that we do this after we have tried to establish link
	 * because the symbol error count will increment wildly if there
	 * is no link.
	 */
	e1000_clear_hw_cntrs_82543(hw);

	return ret_val;
}

/**
 *  e1000_setup_link_82543 - Setup flow control and link settings
 *  @hw: pointer to the HW structure
 *
 *  Read the EEPROM to determine the initial polarity value and write the
 *  extended device control register with the information before calling
 *  the generic setup link function, which does the following:
 *  Determines which flow control settings to use, then configures flow
 *  control.  Calls the appropriate media-specific link configuration
 *  function.  Assuming the adapter has a valid link partner, a valid link
 *  should be established.  Assumes the hardware has previously been reset
 *  and the transmitter and receiver are not enabled.
 **/
static s32 e1000_setup_link_82543(struct e1000_hw *hw)
{
	u32 ctrl_ext;
	s32  ret_val;
	u16 data;

	DEBUGFUNC("e1000_setup_link_82543");

	/*
	 * Take the 4 bits from NVM word 0xF that determine the initial
	 * polarity value for the SW controlled pins, and setup the
	 * Extended Device Control reg with that info.
	 * This is needed because one of the SW controlled pins is used for
	 * signal detection.  So this should be done before phy setup.
	 */
	if (hw->mac.type == e1000_82543) {
		ret_val = hw->nvm.ops.read(hw, NVM_INIT_CONTROL2_REG, 1, &data);
		if (ret_val) {
			DEBUGOUT("NVM Read Error\n");
			ret_val = -E1000_ERR_NVM;
			goto out;
		}
		ctrl_ext = ((data & NVM_WORD0F_SWPDIO_EXT_MASK) <<
			    NVM_SWDPIO_EXT_SHIFT);
		E1000_WRITE_REG(hw, E1000_CTRL_EXT, ctrl_ext);
	}

	ret_val = e1000_setup_link_generic(hw);

out:
	return ret_val;
}

/**
 *  e1000_setup_copper_link_82543 - Configure copper link settings
 *  @hw: pointer to the HW structure
 *
 *  Configures the link for auto-neg or forced speed and duplex.  Then we check
 *  for link, once link is established calls to configure collision distance
 *  and flow control are called.
 **/
static s32 e1000_setup_copper_link_82543(struct e1000_hw *hw)
{
	u32 ctrl;
	s32 ret_val;
	bool link;

	DEBUGFUNC("e1000_setup_copper_link_82543");

	ctrl = E1000_READ_REG(hw, E1000_CTRL) | E1000_CTRL_SLU;
	/*
	 * With 82543, we need to force speed and duplex on the MAC
	 * equal to what the PHY speed and duplex configuration is.
	 * In addition, we need to perform a hardware reset on the
	 * PHY to take it out of reset.
	 */
	if (hw->mac.type == e1000_82543) {
		ctrl |= (E1000_CTRL_FRCSPD | E1000_CTRL_FRCDPX);
		E1000_WRITE_REG(hw, E1000_CTRL, ctrl);
		ret_val = hw->phy.ops.reset(hw);
		if (ret_val)
			goto out;
	} else {
		ctrl &= ~(E1000_CTRL_FRCSPD | E1000_CTRL_FRCDPX);
		E1000_WRITE_REG(hw, E1000_CTRL, ctrl);
	}

	/* Set MDI/MDI-X, Polarity Reversal, and downshift settings */
	ret_val = e1000_copper_link_setup_m88(hw);
	if (ret_val)
		goto out;

	if (hw->mac.autoneg) {
		/*
		 * Setup autoneg and flow control advertisement and perform
		 * autonegotiation.
		 */
		ret_val = e1000_copper_link_autoneg(hw);
		if (ret_val)
			goto out;
	} else {
		/*
		 * PHY will be set to 10H, 10F, 100H or 100F
		 * depending on user settings.
		 */
		DEBUGOUT("Forcing Speed and Duplex\n");
		ret_val = e1000_phy_force_speed_duplex_82543(hw);
		if (ret_val) {
			DEBUGOUT("Error Forcing Speed and Duplex\n");
			goto out;
		}
	}

	/*
	 * Check link status. Wait up to 100 microseconds for link to become
	 * valid.
	 */
	ret_val = e1000_phy_has_link_generic(hw, COPPER_LINK_UP_LIMIT, 10,
					     &link);
	if (ret_val)
		goto out;


	if (link) {
		DEBUGOUT("Valid link established!!!\n");
		/* Config the MAC and PHY after link is up */
		if (hw->mac.type == e1000_82544) {
			hw->mac.ops.config_collision_dist(hw);
		} else {
			ret_val = e1000_config_mac_to_phy_82543(hw);
			if (ret_val)
				goto out;
		}
		ret_val = e1000_config_fc_after_link_up_generic(hw);
	} else {
		DEBUGOUT("Unable to establish link!!!\n");
	}

out:
	return ret_val;
}

/**
 *  e1000_setup_fiber_link_82543 - Setup link for fiber
 *  @hw: pointer to the HW structure
 *
 *  Configures collision distance and flow control for fiber links.  Upon
 *  successful setup, poll for link.
 **/
static s32 e1000_setup_fiber_link_82543(struct e1000_hw *hw)
{
	u32 ctrl;
	s32 ret_val;

	DEBUGFUNC("e1000_setup_fiber_link_82543");

	ctrl = E1000_READ_REG(hw, E1000_CTRL);

	/* Take the link out of reset */
	ctrl &= ~E1000_CTRL_LRST;

	hw->mac.ops.config_collision_dist(hw);

	ret_val = e1000_commit_fc_settings_generic(hw);
	if (ret_val)
		goto out;

	DEBUGOUT("Auto-negotiation enabled\n");

	E1000_WRITE_REG(hw, E1000_CTRL, ctrl);
	E1000_WRITE_FLUSH(hw);
	msec_delay(1);

	/*
	 * For these adapters, the SW definable pin 1 is cleared when the
	 * optics detect a signal.  If we have a signal, then poll for a
	 * "Link-Up" indication.
	 */
	if (!(E1000_READ_REG(hw, E1000_CTRL) & E1000_CTRL_SWDPIN1))
		ret_val = e1000_poll_fiber_serdes_link_generic(hw);
	else
		DEBUGOUT("No signal detected\n");

out:
	return ret_val;
}

/**
 *  e1000_check_for_copper_link_82543 - Check for link (Copper)
 *  @hw: pointer to the HW structure
 *
 *  Checks the phy for link, if link exists, do the following:
 *   - check for downshift
 *   - do polarity workaround (if necessary)
 *   - configure collision distance
 *   - configure flow control after link up
 *   - configure tbi compatibility
 **/
static s32 e1000_check_for_copper_link_82543(struct e1000_hw *hw)
{
	struct e1000_mac_info *mac = &hw->mac;
	u32 icr, rctl;
	s32 ret_val;
	u16 speed, duplex;
	bool link;

	DEBUGFUNC("e1000_check_for_copper_link_82543");

	if (!mac->get_link_status) {
		ret_val = E1000_SUCCESS;
		goto out;
	}

	ret_val = e1000_phy_has_link_generic(hw, 1, 0, &link);
	if (ret_val)
		goto out;

	if (!link)
		goto out; /* No link detected */

	mac->get_link_status = FALSE;

	e1000_check_downshift_generic(hw);

	/*
	 * If we are forcing speed/duplex, then we can return since
	 * we have already determined whether we have link or not.
	 */
	if (!mac->autoneg) {
		/*
		 * If speed and duplex are forced to 10H or 10F, then we will
		 * implement the polarity reversal workaround.  We disable
		 * interrupts first, and upon returning, place the devices
		 * interrupt state to its previous value except for the link
		 * status change interrupt which will happened due to the
		 * execution of this workaround.
		 */
		if (mac->forced_speed_duplex & E1000_ALL_10_SPEED) {
			E1000_WRITE_REG(hw, E1000_IMC, 0xFFFFFFFF);
			ret_val = e1000_polarity_reversal_workaround_82543(hw);
			icr = E1000_READ_REG(hw, E1000_ICR);
			E1000_WRITE_REG(hw, E1000_ICS, (icr & ~E1000_ICS_LSC));
			E1000_WRITE_REG(hw, E1000_IMS, IMS_ENABLE_MASK);
		}

		ret_val = -E1000_ERR_CONFIG;
		goto out;
	}

	/*
	 * We have a M88E1000 PHY and Auto-Neg is enabled.  If we
	 * have Si on board that is 82544 or newer, Auto
	 * Speed Detection takes care of MAC speed/duplex
	 * configuration.  So we only need to configure Collision
	 * Distance in the MAC.  Otherwise, we need to force
	 * speed/duplex on the MAC to the current PHY speed/duplex
	 * settings.
	 */
	if (mac->type == e1000_82544)
		hw->mac.ops.config_collision_dist(hw);
	else {
		ret_val = e1000_config_mac_to_phy_82543(hw);
		if (ret_val) {
			DEBUGOUT("Error configuring MAC to PHY settings\n");
			goto out;
		}
	}

	/*
	 * Configure Flow Control now that Auto-Neg has completed.
	 * First, we need to restore the desired flow control
	 * settings because we may have had to re-autoneg with a
	 * different link partner.
	 */
	ret_val = e1000_config_fc_after_link_up_generic(hw);
	if (ret_val)
		DEBUGOUT("Error configuring flow control\n");

	/*
	 * At this point we know that we are on copper and we have
	 * auto-negotiated link.  These are conditions for checking the link
	 * partner capability register.  We use the link speed to determine if
	 * TBI compatibility needs to be turned on or off.  If the link is not
	 * at gigabit speed, then TBI compatibility is not needed.  If we are
	 * at gigabit speed, we turn on TBI compatibility.
	 */
	if (e1000_tbi_compatibility_enabled_82543(hw)) {
		ret_val = mac->ops.get_link_up_info(hw, &speed, &duplex);
		if (ret_val) {
			DEBUGOUT("Error getting link speed and duplex\n");
			return ret_val;
		}
		if (speed != SPEED_1000) {
			/*
			 * If link speed is not set to gigabit speed,
			 * we do not need to enable TBI compatibility.
			 */
			if (e1000_tbi_sbp_enabled_82543(hw)) {
				/*
				 * If we previously were in the mode,
				 * turn it off.
				 */
				e1000_set_tbi_sbp_82543(hw, FALSE);
				rctl = E1000_READ_REG(hw, E1000_RCTL);
				rctl &= ~E1000_RCTL_SBP;
				E1000_WRITE_REG(hw, E1000_RCTL, rctl);
			}
		} else {
			/*
			 * If TBI compatibility is was previously off,
			 * turn it on. For compatibility with a TBI link
			 * partner, we will store bad packets. Some
			 * frames have an additional byte on the end and
			 * will look like CRC errors to the hardware.
			 */
			if (!e1000_tbi_sbp_enabled_82543(hw)) {
				e1000_set_tbi_sbp_82543(hw, TRUE);
				rctl = E1000_READ_REG(hw, E1000_RCTL);
				rctl |= E1000_RCTL_SBP;
				E1000_WRITE_REG(hw, E1000_RCTL, rctl);
			}
		}
	}
out:
	return ret_val;
}

/**
 *  e1000_check_for_fiber_link_82543 - Check for link (Fiber)
 *  @hw: pointer to the HW structure
 *
 *  Checks for link up on the hardware.  If link is not up and we have
 *  a signal, then we need to force link up.
 **/
static s32 e1000_check_for_fiber_link_82543(struct e1000_hw *hw)
{
	struct e1000_mac_info *mac = &hw->mac;
	u32 rxcw, ctrl, status;
	s32 ret_val = E1000_SUCCESS;

	DEBUGFUNC("e1000_check_for_fiber_link_82543");

	ctrl = E1000_READ_REG(hw, E1000_CTRL);
	status = E1000_READ_REG(hw, E1000_STATUS);
	rxcw = E1000_READ_REG(hw, E1000_RXCW);

	/*
	 * If we don't have link (auto-negotiation failed or link partner
	 * cannot auto-negotiate), the cable is plugged in (we have signal),
	 * and our link partner is not trying to auto-negotiate with us (we
	 * are receiving idles or data), we need to force link up. We also
	 * need to give auto-negotiation time to complete, in case the cable
	 * was just plugged in. The autoneg_failed flag does this.
	 */
	/* (ctrl & E1000_CTRL_SWDPIN1) == 0 == have signal */
	if ((!(ctrl & E1000_CTRL_SWDPIN1)) &&
	    (!(status & E1000_STATUS_LU)) &&
	    (!(rxcw & E1000_RXCW_C))) {
		if (!mac->autoneg_failed) {
			mac->autoneg_failed = TRUE;
			ret_val = 0;
			goto out;
		}
		DEBUGOUT("NOT RXing /C/, disable AutoNeg and force link.\n");

		/* Disable auto-negotiation in the TXCW register */
		E1000_WRITE_REG(hw, E1000_TXCW, (mac->txcw & ~E1000_TXCW_ANE));

		/* Force link-up and also force full-duplex. */
		ctrl = E1000_READ_REG(hw, E1000_CTRL);
		ctrl |= (E1000_CTRL_SLU | E1000_CTRL_FD);
		E1000_WRITE_REG(hw, E1000_CTRL, ctrl);

		/* Configure Flow Control after forcing link up. */
		ret_val = e1000_config_fc_after_link_up_generic(hw);
		if (ret_val) {
			DEBUGOUT("Error configuring flow control\n");
			goto out;
		}
	} else if ((ctrl & E1000_CTRL_SLU) && (rxcw & E1000_RXCW_C)) {
		/*
		 * If we are forcing link and we are receiving /C/ ordered
		 * sets, re-enable auto-negotiation in the TXCW register
		 * and disable forced link in the Device Control register
		 * in an attempt to auto-negotiate with our link partner.
		 */
		DEBUGOUT("RXing /C/, enable AutoNeg and stop forcing link.\n");
		E1000_WRITE_REG(hw, E1000_TXCW, mac->txcw);
		E1000_WRITE_REG(hw, E1000_CTRL, (ctrl & ~E1000_CTRL_SLU));

		mac->serdes_has_link = TRUE;
	}

out:
	return ret_val;
}

/**
 *  e1000_config_mac_to_phy_82543 - Configure MAC to PHY settings
 *  @hw: pointer to the HW structure
 *
 *  For the 82543 silicon, we need to set the MAC to match the settings
 *  of the PHY, even if the PHY is auto-negotiating.
 **/
static s32 e1000_config_mac_to_phy_82543(struct e1000_hw *hw)
{
	u32 ctrl;
	s32 ret_val = E1000_SUCCESS;
	u16 phy_data;

	DEBUGFUNC("e1000_config_mac_to_phy_82543");

	if (!(hw->phy.ops.read_reg))
		goto out;

	/* Set the bits to force speed and duplex */
	ctrl = E1000_READ_REG(hw, E1000_CTRL);
	ctrl |= (E1000_CTRL_FRCSPD | E1000_CTRL_FRCDPX);
	ctrl &= ~(E1000_CTRL_SPD_SEL | E1000_CTRL_ILOS);

	/*
	 * Set up duplex in the Device Control and Transmit Control
	 * registers depending on negotiated values.
	 */
	ret_val = hw->phy.ops.read_reg(hw, M88E1000_PHY_SPEC_STATUS, &phy_data);
	if (ret_val)
		goto out;

	ctrl &= ~E1000_CTRL_FD;
	if (phy_data & M88E1000_PSSR_DPLX)
		ctrl |= E1000_CTRL_FD;

	hw->mac.ops.config_collision_dist(hw);

	/*
	 * Set up speed in the Device Control register depending on
	 * negotiated values.
	 */
	if ((phy_data & M88E1000_PSSR_SPEED) == M88E1000_PSSR_1000MBS)
		ctrl |= E1000_CTRL_SPD_1000;
	else if ((phy_data & M88E1000_PSSR_SPEED) == M88E1000_PSSR_100MBS)
		ctrl |= E1000_CTRL_SPD_100;

	E1000_WRITE_REG(hw, E1000_CTRL, ctrl);

out:
	return ret_val;
}

/**
 *  e1000_write_vfta_82543 - Write value to VLAN filter table
 *  @hw: pointer to the HW structure
 *  @offset: the 32-bit offset in which to write the value to.
 *  @value: the 32-bit value to write at location offset.
 *
 *  This writes a 32-bit value to a 32-bit offset in the VLAN filter
 *  table.
 **/
static void e1000_write_vfta_82543(struct e1000_hw *hw, u32 offset, u32 value)
{
	u32 temp;

	DEBUGFUNC("e1000_write_vfta_82543");

	if ((hw->mac.type == e1000_82544) && (offset & 1)) {
		temp = E1000_READ_REG_ARRAY(hw, E1000_VFTA, offset - 1);
		E1000_WRITE_REG_ARRAY(hw, E1000_VFTA, offset, value);
		E1000_WRITE_FLUSH(hw);
		E1000_WRITE_REG_ARRAY(hw, E1000_VFTA, offset - 1, temp);
		E1000_WRITE_FLUSH(hw);
	} else {
		e1000_write_vfta_generic(hw, offset, value);
	}
}

/**
 *  e1000_led_on_82543 - Turn on SW controllable LED
 *  @hw: pointer to the HW structure
 *
 *  Turns the SW defined LED on.
 **/
static s32 e1000_led_on_82543(struct e1000_hw *hw)
{
	u32 ctrl = E1000_READ_REG(hw, E1000_CTRL);

	DEBUGFUNC("e1000_led_on_82543");

	if (hw->mac.type == e1000_82544 &&
	    hw->phy.media_type == e1000_media_type_copper) {
		/* Clear SW-definable Pin 0 to turn on the LED */
		ctrl &= ~E1000_CTRL_SWDPIN0;
		ctrl |= E1000_CTRL_SWDPIO0;
	} else {
		/* Fiber 82544 and all 82543 use this method */
		ctrl |= E1000_CTRL_SWDPIN0;
		ctrl |= E1000_CTRL_SWDPIO0;
	}
	E1000_WRITE_REG(hw, E1000_CTRL, ctrl);

	return E1000_SUCCESS;
}

/**
 *  e1000_led_off_82543 - Turn off SW controllable LED
 *  @hw: pointer to the HW structure
 *
 *  Turns the SW defined LED off.
 **/
static s32 e1000_led_off_82543(struct e1000_hw *hw)
{
	u32 ctrl = E1000_READ_REG(hw, E1000_CTRL);

	DEBUGFUNC("e1000_led_off_82543");

	if (hw->mac.type == e1000_82544 &&
	    hw->phy.media_type == e1000_media_type_copper) {
		/* Set SW-definable Pin 0 to turn off the LED */
		ctrl |= E1000_CTRL_SWDPIN0;
		ctrl |= E1000_CTRL_SWDPIO0;
	} else {
		ctrl &= ~E1000_CTRL_SWDPIN0;
		ctrl |= E1000_CTRL_SWDPIO0;
	}
	E1000_WRITE_REG(hw, E1000_CTRL, ctrl);

	return E1000_SUCCESS;
}

/**
 *  e1000_clear_hw_cntrs_82543 - Clear device specific hardware counters
 *  @hw: pointer to the HW structure
 *
 *  Clears the hardware counters by reading the counter registers.
 **/
static void e1000_clear_hw_cntrs_82543(struct e1000_hw *hw)
{
	DEBUGFUNC("e1000_clear_hw_cntrs_82543");

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
}

/**
 *  e1000_read_mac_addr_82543 - Read device MAC address
 *  @hw: pointer to the HW structure
 *
 *  Reads the device MAC address from the EEPROM and stores the value.
 *  Since devices with two ports use the same EEPROM, we increment the
 *  last bit in the MAC address for the second port.
 *
 **/
s32 e1000_read_mac_addr_82543(struct e1000_hw *hw)
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

	/* Flip last bit of mac address if we're on second port */
	if (hw->bus.func == E1000_FUNC_1)
		hw->mac.perm_addr[5] ^= 1;

	for (i = 0; i < ETH_ADDR_LEN; i++)
		hw->mac.addr[i] = hw->mac.perm_addr[i];

out:
	return ret_val;
}
