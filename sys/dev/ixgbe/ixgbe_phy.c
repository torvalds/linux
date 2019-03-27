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
#include "ixgbe_phy.h"

static void ixgbe_i2c_start(struct ixgbe_hw *hw);
static void ixgbe_i2c_stop(struct ixgbe_hw *hw);
static s32 ixgbe_clock_in_i2c_byte(struct ixgbe_hw *hw, u8 *data);
static s32 ixgbe_clock_out_i2c_byte(struct ixgbe_hw *hw, u8 data);
static s32 ixgbe_get_i2c_ack(struct ixgbe_hw *hw);
static s32 ixgbe_clock_in_i2c_bit(struct ixgbe_hw *hw, bool *data);
static s32 ixgbe_clock_out_i2c_bit(struct ixgbe_hw *hw, bool data);
static void ixgbe_raise_i2c_clk(struct ixgbe_hw *hw, u32 *i2cctl);
static void ixgbe_lower_i2c_clk(struct ixgbe_hw *hw, u32 *i2cctl);
static s32 ixgbe_set_i2c_data(struct ixgbe_hw *hw, u32 *i2cctl, bool data);
static bool ixgbe_get_i2c_data(struct ixgbe_hw *hw, u32 *i2cctl);
static s32 ixgbe_read_i2c_sff8472_generic(struct ixgbe_hw *hw, u8 byte_offset,
					  u8 *sff8472_data);

/**
 * ixgbe_out_i2c_byte_ack - Send I2C byte with ack
 * @hw: pointer to the hardware structure
 * @byte: byte to send
 *
 * Returns an error code on error.
 */
static s32 ixgbe_out_i2c_byte_ack(struct ixgbe_hw *hw, u8 byte)
{
	s32 status;

	status = ixgbe_clock_out_i2c_byte(hw, byte);
	if (status)
		return status;
	return ixgbe_get_i2c_ack(hw);
}

/**
 * ixgbe_in_i2c_byte_ack - Receive an I2C byte and send ack
 * @hw: pointer to the hardware structure
 * @byte: pointer to a u8 to receive the byte
 *
 * Returns an error code on error.
 */
static s32 ixgbe_in_i2c_byte_ack(struct ixgbe_hw *hw, u8 *byte)
{
	s32 status;

	status = ixgbe_clock_in_i2c_byte(hw, byte);
	if (status)
		return status;
	/* ACK */
	return ixgbe_clock_out_i2c_bit(hw, FALSE);
}

/**
 * ixgbe_ones_comp_byte_add - Perform one's complement addition
 * @add1: addend 1
 * @add2: addend 2
 *
 * Returns one's complement 8-bit sum.
 */
static u8 ixgbe_ones_comp_byte_add(u8 add1, u8 add2)
{
	u16 sum = add1 + add2;

	sum = (sum & 0xFF) + (sum >> 8);
	return sum & 0xFF;
}

/**
 * ixgbe_read_i2c_combined_generic_int - Perform I2C read combined operation
 * @hw: pointer to the hardware structure
 * @addr: I2C bus address to read from
 * @reg: I2C device register to read from
 * @val: pointer to location to receive read value
 * @lock: TRUE if to take and release semaphore
 *
 * Returns an error code on error.
 */
s32 ixgbe_read_i2c_combined_generic_int(struct ixgbe_hw *hw, u8 addr, u16 reg,
					u16 *val, bool lock)
{
	u32 swfw_mask = hw->phy.phy_semaphore_mask;
	int max_retry = 3;
	int retry = 0;
	u8 csum_byte;
	u8 high_bits;
	u8 low_bits;
	u8 reg_high;
	u8 csum;

	reg_high = ((reg >> 7) & 0xFE) | 1;	/* Indicate read combined */
	csum = ixgbe_ones_comp_byte_add(reg_high, reg & 0xFF);
	csum = ~csum;
	do {
		if (lock && hw->mac.ops.acquire_swfw_sync(hw, swfw_mask))
			return IXGBE_ERR_SWFW_SYNC;
		ixgbe_i2c_start(hw);
		/* Device Address and write indication */
		if (ixgbe_out_i2c_byte_ack(hw, addr))
			goto fail;
		/* Write bits 14:8 */
		if (ixgbe_out_i2c_byte_ack(hw, reg_high))
			goto fail;
		/* Write bits 7:0 */
		if (ixgbe_out_i2c_byte_ack(hw, reg & 0xFF))
			goto fail;
		/* Write csum */
		if (ixgbe_out_i2c_byte_ack(hw, csum))
			goto fail;
		/* Re-start condition */
		ixgbe_i2c_start(hw);
		/* Device Address and read indication */
		if (ixgbe_out_i2c_byte_ack(hw, addr | 1))
			goto fail;
		/* Get upper bits */
		if (ixgbe_in_i2c_byte_ack(hw, &high_bits))
			goto fail;
		/* Get low bits */
		if (ixgbe_in_i2c_byte_ack(hw, &low_bits))
			goto fail;
		/* Get csum */
		if (ixgbe_clock_in_i2c_byte(hw, &csum_byte))
			goto fail;
		/* NACK */
		if (ixgbe_clock_out_i2c_bit(hw, FALSE))
			goto fail;
		ixgbe_i2c_stop(hw);
		if (lock)
			hw->mac.ops.release_swfw_sync(hw, swfw_mask);
		*val = (high_bits << 8) | low_bits;
		return 0;

fail:
		ixgbe_i2c_bus_clear(hw);
		if (lock)
			hw->mac.ops.release_swfw_sync(hw, swfw_mask);
		retry++;
		if (retry < max_retry)
			DEBUGOUT("I2C byte read combined error - Retrying.\n");
		else
			DEBUGOUT("I2C byte read combined error.\n");
	} while (retry < max_retry);

	return IXGBE_ERR_I2C;
}

/**
 * ixgbe_write_i2c_combined_generic_int - Perform I2C write combined operation
 * @hw: pointer to the hardware structure
 * @addr: I2C bus address to write to
 * @reg: I2C device register to write to
 * @val: value to write
 * @lock: TRUE if to take and release semaphore
 *
 * Returns an error code on error.
 */
s32 ixgbe_write_i2c_combined_generic_int(struct ixgbe_hw *hw, u8 addr, u16 reg,
					 u16 val, bool lock)
{
	u32 swfw_mask = hw->phy.phy_semaphore_mask;
	int max_retry = 1;
	int retry = 0;
	u8 reg_high;
	u8 csum;

	reg_high = (reg >> 7) & 0xFE;	/* Indicate write combined */
	csum = ixgbe_ones_comp_byte_add(reg_high, reg & 0xFF);
	csum = ixgbe_ones_comp_byte_add(csum, val >> 8);
	csum = ixgbe_ones_comp_byte_add(csum, val & 0xFF);
	csum = ~csum;
	do {
		if (lock && hw->mac.ops.acquire_swfw_sync(hw, swfw_mask))
			return IXGBE_ERR_SWFW_SYNC;
		ixgbe_i2c_start(hw);
		/* Device Address and write indication */
		if (ixgbe_out_i2c_byte_ack(hw, addr))
			goto fail;
		/* Write bits 14:8 */
		if (ixgbe_out_i2c_byte_ack(hw, reg_high))
			goto fail;
		/* Write bits 7:0 */
		if (ixgbe_out_i2c_byte_ack(hw, reg & 0xFF))
			goto fail;
		/* Write data 15:8 */
		if (ixgbe_out_i2c_byte_ack(hw, val >> 8))
			goto fail;
		/* Write data 7:0 */
		if (ixgbe_out_i2c_byte_ack(hw, val & 0xFF))
			goto fail;
		/* Write csum */
		if (ixgbe_out_i2c_byte_ack(hw, csum))
			goto fail;
		ixgbe_i2c_stop(hw);
		if (lock)
			hw->mac.ops.release_swfw_sync(hw, swfw_mask);
		return 0;

fail:
		ixgbe_i2c_bus_clear(hw);
		if (lock)
			hw->mac.ops.release_swfw_sync(hw, swfw_mask);
		retry++;
		if (retry < max_retry)
			DEBUGOUT("I2C byte write combined error - Retrying.\n");
		else
			DEBUGOUT("I2C byte write combined error.\n");
	} while (retry < max_retry);

	return IXGBE_ERR_I2C;
}

/**
 *  ixgbe_init_phy_ops_generic - Inits PHY function ptrs
 *  @hw: pointer to the hardware structure
 *
 *  Initialize the function pointers.
 **/
s32 ixgbe_init_phy_ops_generic(struct ixgbe_hw *hw)
{
	struct ixgbe_phy_info *phy = &hw->phy;

	DEBUGFUNC("ixgbe_init_phy_ops_generic");

	/* PHY */
	phy->ops.identify = ixgbe_identify_phy_generic;
	phy->ops.reset = ixgbe_reset_phy_generic;
	phy->ops.read_reg = ixgbe_read_phy_reg_generic;
	phy->ops.write_reg = ixgbe_write_phy_reg_generic;
	phy->ops.read_reg_mdi = ixgbe_read_phy_reg_mdi;
	phy->ops.write_reg_mdi = ixgbe_write_phy_reg_mdi;
	phy->ops.setup_link = ixgbe_setup_phy_link_generic;
	phy->ops.setup_link_speed = ixgbe_setup_phy_link_speed_generic;
	phy->ops.check_link = NULL;
	phy->ops.get_firmware_version = ixgbe_get_phy_firmware_version_generic;
	phy->ops.read_i2c_byte = ixgbe_read_i2c_byte_generic;
	phy->ops.write_i2c_byte = ixgbe_write_i2c_byte_generic;
	phy->ops.read_i2c_sff8472 = ixgbe_read_i2c_sff8472_generic;
	phy->ops.read_i2c_eeprom = ixgbe_read_i2c_eeprom_generic;
	phy->ops.write_i2c_eeprom = ixgbe_write_i2c_eeprom_generic;
	phy->ops.i2c_bus_clear = ixgbe_i2c_bus_clear;
	phy->ops.identify_sfp = ixgbe_identify_module_generic;
	phy->sfp_type = ixgbe_sfp_type_unknown;
	phy->ops.read_i2c_byte_unlocked = ixgbe_read_i2c_byte_generic_unlocked;
	phy->ops.write_i2c_byte_unlocked =
				ixgbe_write_i2c_byte_generic_unlocked;
	phy->ops.check_overtemp = ixgbe_tn_check_overtemp;
	return IXGBE_SUCCESS;
}

/**
 * ixgbe_probe_phy - Probe a single address for a PHY
 * @hw: pointer to hardware structure
 * @phy_addr: PHY address to probe
 *
 * Returns TRUE if PHY found
 */
static bool ixgbe_probe_phy(struct ixgbe_hw *hw, u16 phy_addr)
{
	u16 ext_ability = 0;

	if (!ixgbe_validate_phy_addr(hw, phy_addr)) {
		DEBUGOUT1("Unable to validate PHY address 0x%04X\n",
			phy_addr);
		return FALSE;
	}

	if (ixgbe_get_phy_id(hw))
		return FALSE;

	hw->phy.type = ixgbe_get_phy_type_from_id(hw->phy.id);

	if (hw->phy.type == ixgbe_phy_unknown) {
		hw->phy.ops.read_reg(hw, IXGBE_MDIO_PHY_EXT_ABILITY,
				     IXGBE_MDIO_PMA_PMD_DEV_TYPE, &ext_ability);
		if (ext_ability &
		    (IXGBE_MDIO_PHY_10GBASET_ABILITY |
		     IXGBE_MDIO_PHY_1000BASET_ABILITY))
			hw->phy.type = ixgbe_phy_cu_unknown;
		else
			hw->phy.type = ixgbe_phy_generic;
	}

	return TRUE;
}

/**
 *  ixgbe_identify_phy_generic - Get physical layer module
 *  @hw: pointer to hardware structure
 *
 *  Determines the physical layer module found on the current adapter.
 **/
s32 ixgbe_identify_phy_generic(struct ixgbe_hw *hw)
{
	s32 status = IXGBE_ERR_PHY_ADDR_INVALID;
	u16 phy_addr;

	DEBUGFUNC("ixgbe_identify_phy_generic");

	if (!hw->phy.phy_semaphore_mask) {
		if (hw->bus.lan_id)
			hw->phy.phy_semaphore_mask = IXGBE_GSSR_PHY1_SM;
		else
			hw->phy.phy_semaphore_mask = IXGBE_GSSR_PHY0_SM;
	}

	if (hw->phy.type != ixgbe_phy_unknown)
		return IXGBE_SUCCESS;

	if (hw->phy.nw_mng_if_sel) {
		phy_addr = (hw->phy.nw_mng_if_sel &
			    IXGBE_NW_MNG_IF_SEL_MDIO_PHY_ADD) >>
			   IXGBE_NW_MNG_IF_SEL_MDIO_PHY_ADD_SHIFT;
		if (ixgbe_probe_phy(hw, phy_addr))
			return IXGBE_SUCCESS;
		else
			return IXGBE_ERR_PHY_ADDR_INVALID;
	}

	for (phy_addr = 0; phy_addr < IXGBE_MAX_PHY_ADDR; phy_addr++) {
		if (ixgbe_probe_phy(hw, phy_addr)) {
			status = IXGBE_SUCCESS;
			break;
		}
	}

	/* Certain media types do not have a phy so an address will not
	 * be found and the code will take this path.  Caller has to
	 * decide if it is an error or not.
	 */
	if (status != IXGBE_SUCCESS)
		hw->phy.addr = 0;

	return status;
}

/**
 * ixgbe_check_reset_blocked - check status of MNG FW veto bit
 * @hw: pointer to the hardware structure
 *
 * This function checks the MMNGC.MNG_VETO bit to see if there are
 * any constraints on link from manageability.  For MAC's that don't
 * have this bit just return faluse since the link can not be blocked
 * via this method.
 **/
s32 ixgbe_check_reset_blocked(struct ixgbe_hw *hw)
{
	u32 mmngc;

	DEBUGFUNC("ixgbe_check_reset_blocked");

	/* If we don't have this bit, it can't be blocking */
	if (hw->mac.type == ixgbe_mac_82598EB)
		return FALSE;

	mmngc = IXGBE_READ_REG(hw, IXGBE_MMNGC);
	if (mmngc & IXGBE_MMNGC_MNG_VETO) {
		ERROR_REPORT1(IXGBE_ERROR_SOFTWARE,
			      "MNG_VETO bit detected.\n");
		return TRUE;
	}

	return FALSE;
}

/**
 *  ixgbe_validate_phy_addr - Determines phy address is valid
 *  @hw: pointer to hardware structure
 *  @phy_addr: PHY address
 *
 **/
bool ixgbe_validate_phy_addr(struct ixgbe_hw *hw, u32 phy_addr)
{
	u16 phy_id = 0;
	bool valid = FALSE;

	DEBUGFUNC("ixgbe_validate_phy_addr");

	hw->phy.addr = phy_addr;
	hw->phy.ops.read_reg(hw, IXGBE_MDIO_PHY_ID_HIGH,
			     IXGBE_MDIO_PMA_PMD_DEV_TYPE, &phy_id);

	if (phy_id != 0xFFFF && phy_id != 0x0)
		valid = TRUE;

	DEBUGOUT1("PHY ID HIGH is 0x%04X\n", phy_id);

	return valid;
}

/**
 *  ixgbe_get_phy_id - Get the phy type
 *  @hw: pointer to hardware structure
 *
 **/
s32 ixgbe_get_phy_id(struct ixgbe_hw *hw)
{
	u32 status;
	u16 phy_id_high = 0;
	u16 phy_id_low = 0;

	DEBUGFUNC("ixgbe_get_phy_id");

	status = hw->phy.ops.read_reg(hw, IXGBE_MDIO_PHY_ID_HIGH,
				      IXGBE_MDIO_PMA_PMD_DEV_TYPE,
				      &phy_id_high);

	if (status == IXGBE_SUCCESS) {
		hw->phy.id = (u32)(phy_id_high << 16);
		status = hw->phy.ops.read_reg(hw, IXGBE_MDIO_PHY_ID_LOW,
					      IXGBE_MDIO_PMA_PMD_DEV_TYPE,
					      &phy_id_low);
		hw->phy.id |= (u32)(phy_id_low & IXGBE_PHY_REVISION_MASK);
		hw->phy.revision = (u32)(phy_id_low & ~IXGBE_PHY_REVISION_MASK);
	}
	DEBUGOUT2("PHY_ID_HIGH 0x%04X, PHY_ID_LOW 0x%04X\n",
		  phy_id_high, phy_id_low);

	return status;
}

/**
 *  ixgbe_get_phy_type_from_id - Get the phy type
 *  @phy_id: PHY ID information
 *
 **/
enum ixgbe_phy_type ixgbe_get_phy_type_from_id(u32 phy_id)
{
	enum ixgbe_phy_type phy_type;

	DEBUGFUNC("ixgbe_get_phy_type_from_id");

	switch (phy_id) {
	case TN1010_PHY_ID:
		phy_type = ixgbe_phy_tn;
		break;
	case X550_PHY_ID2:
	case X550_PHY_ID3:
	case X540_PHY_ID:
		phy_type = ixgbe_phy_aq;
		break;
	case QT2022_PHY_ID:
		phy_type = ixgbe_phy_qt;
		break;
	case ATH_PHY_ID:
		phy_type = ixgbe_phy_nl;
		break;
	case X557_PHY_ID:
	case X557_PHY_ID2:
		phy_type = ixgbe_phy_x550em_ext_t;
		break;
	case IXGBE_M88E1500_E_PHY_ID:
	case IXGBE_M88E1543_E_PHY_ID:
		phy_type = ixgbe_phy_ext_1g_t;
		break;
	default:
		phy_type = ixgbe_phy_unknown;
		break;
	}
	return phy_type;
}

/**
 *  ixgbe_reset_phy_generic - Performs a PHY reset
 *  @hw: pointer to hardware structure
 **/
s32 ixgbe_reset_phy_generic(struct ixgbe_hw *hw)
{
	u32 i;
	u16 ctrl = 0;
	s32 status = IXGBE_SUCCESS;

	DEBUGFUNC("ixgbe_reset_phy_generic");

	if (hw->phy.type == ixgbe_phy_unknown)
		status = ixgbe_identify_phy_generic(hw);

	if (status != IXGBE_SUCCESS || hw->phy.type == ixgbe_phy_none)
		goto out;

	/* Don't reset PHY if it's shut down due to overtemp. */
	if (!hw->phy.reset_if_overtemp &&
	    (IXGBE_ERR_OVERTEMP == hw->phy.ops.check_overtemp(hw)))
		goto out;

	/* Blocked by MNG FW so bail */
	if (ixgbe_check_reset_blocked(hw))
		goto out;

	/*
	 * Perform soft PHY reset to the PHY_XS.
	 * This will cause a soft reset to the PHY
	 */
	hw->phy.ops.write_reg(hw, IXGBE_MDIO_PHY_XS_CONTROL,
			      IXGBE_MDIO_PHY_XS_DEV_TYPE,
			      IXGBE_MDIO_PHY_XS_RESET);

	/*
	 * Poll for reset bit to self-clear indicating reset is complete.
	 * Some PHYs could take up to 3 seconds to complete and need about
	 * 1.7 usec delay after the reset is complete.
	 */
	for (i = 0; i < 30; i++) {
		msec_delay(100);
		if (hw->phy.type == ixgbe_phy_x550em_ext_t) {
			status = hw->phy.ops.read_reg(hw,
						  IXGBE_MDIO_TX_VENDOR_ALARMS_3,
						  IXGBE_MDIO_PMA_PMD_DEV_TYPE,
						  &ctrl);
			if (status != IXGBE_SUCCESS)
				return status;

			if (ctrl & IXGBE_MDIO_TX_VENDOR_ALARMS_3_RST_MASK) {
				usec_delay(2);
				break;
			}
		} else {
			status = hw->phy.ops.read_reg(hw,
						     IXGBE_MDIO_PHY_XS_CONTROL,
						     IXGBE_MDIO_PHY_XS_DEV_TYPE,
						     &ctrl);
			if (status != IXGBE_SUCCESS)
				return status;

			if (!(ctrl & IXGBE_MDIO_PHY_XS_RESET)) {
				usec_delay(2);
				break;
			}
		}
	}

	if (ctrl & IXGBE_MDIO_PHY_XS_RESET) {
		status = IXGBE_ERR_RESET_FAILED;
		ERROR_REPORT1(IXGBE_ERROR_POLLING,
			     "PHY reset polling failed to complete.\n");
	}

out:
	return status;
}

/**
 *  ixgbe_read_phy_mdi - Reads a value from a specified PHY register without
 *  the SWFW lock
 *  @hw: pointer to hardware structure
 *  @reg_addr: 32 bit address of PHY register to read
 *  @device_type: 5 bit device type
 *  @phy_data: Pointer to read data from PHY register
 **/
s32 ixgbe_read_phy_reg_mdi(struct ixgbe_hw *hw, u32 reg_addr, u32 device_type,
			   u16 *phy_data)
{
	u32 i, data, command;

	/* Setup and write the address cycle command */
	command = ((reg_addr << IXGBE_MSCA_NP_ADDR_SHIFT)  |
		   (device_type << IXGBE_MSCA_DEV_TYPE_SHIFT) |
		   (hw->phy.addr << IXGBE_MSCA_PHY_ADDR_SHIFT) |
		   (IXGBE_MSCA_ADDR_CYCLE | IXGBE_MSCA_MDI_COMMAND));

	IXGBE_WRITE_REG(hw, IXGBE_MSCA, command);

	/*
	 * Check every 10 usec to see if the address cycle completed.
	 * The MDI Command bit will clear when the operation is
	 * complete
	 */
	for (i = 0; i < IXGBE_MDIO_COMMAND_TIMEOUT; i++) {
		usec_delay(10);

		command = IXGBE_READ_REG(hw, IXGBE_MSCA);
		if ((command & IXGBE_MSCA_MDI_COMMAND) == 0)
			break;
	}


	if ((command & IXGBE_MSCA_MDI_COMMAND) != 0) {
		ERROR_REPORT1(IXGBE_ERROR_POLLING, "PHY address command did not complete.\n");
		DEBUGOUT("PHY address command did not complete, returning IXGBE_ERR_PHY\n");
		return IXGBE_ERR_PHY;
	}

	/*
	 * Address cycle complete, setup and write the read
	 * command
	 */
	command = ((reg_addr << IXGBE_MSCA_NP_ADDR_SHIFT)  |
		   (device_type << IXGBE_MSCA_DEV_TYPE_SHIFT) |
		   (hw->phy.addr << IXGBE_MSCA_PHY_ADDR_SHIFT) |
		   (IXGBE_MSCA_READ | IXGBE_MSCA_MDI_COMMAND));

	IXGBE_WRITE_REG(hw, IXGBE_MSCA, command);

	/*
	 * Check every 10 usec to see if the address cycle
	 * completed. The MDI Command bit will clear when the
	 * operation is complete
	 */
	for (i = 0; i < IXGBE_MDIO_COMMAND_TIMEOUT; i++) {
		usec_delay(10);

		command = IXGBE_READ_REG(hw, IXGBE_MSCA);
		if ((command & IXGBE_MSCA_MDI_COMMAND) == 0)
			break;
	}

	if ((command & IXGBE_MSCA_MDI_COMMAND) != 0) {
		ERROR_REPORT1(IXGBE_ERROR_POLLING, "PHY read command didn't complete\n");
		DEBUGOUT("PHY read command didn't complete, returning IXGBE_ERR_PHY\n");
		return IXGBE_ERR_PHY;
	}

	/*
	 * Read operation is complete.  Get the data
	 * from MSRWD
	 */
	data = IXGBE_READ_REG(hw, IXGBE_MSRWD);
	data >>= IXGBE_MSRWD_READ_DATA_SHIFT;
	*phy_data = (u16)(data);

	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_read_phy_reg_generic - Reads a value from a specified PHY register
 *  using the SWFW lock - this function is needed in most cases
 *  @hw: pointer to hardware structure
 *  @reg_addr: 32 bit address of PHY register to read
 *  @device_type: 5 bit device type
 *  @phy_data: Pointer to read data from PHY register
 **/
s32 ixgbe_read_phy_reg_generic(struct ixgbe_hw *hw, u32 reg_addr,
			       u32 device_type, u16 *phy_data)
{
	s32 status;
	u32 gssr = hw->phy.phy_semaphore_mask;

	DEBUGFUNC("ixgbe_read_phy_reg_generic");

	if (hw->mac.ops.acquire_swfw_sync(hw, gssr))
		return IXGBE_ERR_SWFW_SYNC;

	status = hw->phy.ops.read_reg_mdi(hw, reg_addr, device_type, phy_data);

	hw->mac.ops.release_swfw_sync(hw, gssr);

	return status;
}

/**
 *  ixgbe_write_phy_reg_mdi - Writes a value to specified PHY register
 *  without SWFW lock
 *  @hw: pointer to hardware structure
 *  @reg_addr: 32 bit PHY register to write
 *  @device_type: 5 bit device type
 *  @phy_data: Data to write to the PHY register
 **/
s32 ixgbe_write_phy_reg_mdi(struct ixgbe_hw *hw, u32 reg_addr,
				u32 device_type, u16 phy_data)
{
	u32 i, command;

	/* Put the data in the MDI single read and write data register*/
	IXGBE_WRITE_REG(hw, IXGBE_MSRWD, (u32)phy_data);

	/* Setup and write the address cycle command */
	command = ((reg_addr << IXGBE_MSCA_NP_ADDR_SHIFT)  |
		   (device_type << IXGBE_MSCA_DEV_TYPE_SHIFT) |
		   (hw->phy.addr << IXGBE_MSCA_PHY_ADDR_SHIFT) |
		   (IXGBE_MSCA_ADDR_CYCLE | IXGBE_MSCA_MDI_COMMAND));

	IXGBE_WRITE_REG(hw, IXGBE_MSCA, command);

	/*
	 * Check every 10 usec to see if the address cycle completed.
	 * The MDI Command bit will clear when the operation is
	 * complete
	 */
	for (i = 0; i < IXGBE_MDIO_COMMAND_TIMEOUT; i++) {
		usec_delay(10);

		command = IXGBE_READ_REG(hw, IXGBE_MSCA);
		if ((command & IXGBE_MSCA_MDI_COMMAND) == 0)
			break;
	}

	if ((command & IXGBE_MSCA_MDI_COMMAND) != 0) {
		ERROR_REPORT1(IXGBE_ERROR_POLLING, "PHY address cmd didn't complete\n");
		return IXGBE_ERR_PHY;
	}

	/*
	 * Address cycle complete, setup and write the write
	 * command
	 */
	command = ((reg_addr << IXGBE_MSCA_NP_ADDR_SHIFT)  |
		   (device_type << IXGBE_MSCA_DEV_TYPE_SHIFT) |
		   (hw->phy.addr << IXGBE_MSCA_PHY_ADDR_SHIFT) |
		   (IXGBE_MSCA_WRITE | IXGBE_MSCA_MDI_COMMAND));

	IXGBE_WRITE_REG(hw, IXGBE_MSCA, command);

	/*
	 * Check every 10 usec to see if the address cycle
	 * completed. The MDI Command bit will clear when the
	 * operation is complete
	 */
	for (i = 0; i < IXGBE_MDIO_COMMAND_TIMEOUT; i++) {
		usec_delay(10);

		command = IXGBE_READ_REG(hw, IXGBE_MSCA);
		if ((command & IXGBE_MSCA_MDI_COMMAND) == 0)
			break;
	}

	if ((command & IXGBE_MSCA_MDI_COMMAND) != 0) {
		ERROR_REPORT1(IXGBE_ERROR_POLLING, "PHY write cmd didn't complete\n");
		return IXGBE_ERR_PHY;
	}

	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_write_phy_reg_generic - Writes a value to specified PHY register
 *  using SWFW lock- this function is needed in most cases
 *  @hw: pointer to hardware structure
 *  @reg_addr: 32 bit PHY register to write
 *  @device_type: 5 bit device type
 *  @phy_data: Data to write to the PHY register
 **/
s32 ixgbe_write_phy_reg_generic(struct ixgbe_hw *hw, u32 reg_addr,
				u32 device_type, u16 phy_data)
{
	s32 status;
	u32 gssr = hw->phy.phy_semaphore_mask;

	DEBUGFUNC("ixgbe_write_phy_reg_generic");

	if (hw->mac.ops.acquire_swfw_sync(hw, gssr) == IXGBE_SUCCESS) {
		status = hw->phy.ops.write_reg_mdi(hw, reg_addr, device_type,
						 phy_data);
		hw->mac.ops.release_swfw_sync(hw, gssr);
	} else {
		status = IXGBE_ERR_SWFW_SYNC;
	}

	return status;
}

/**
 *  ixgbe_setup_phy_link_generic - Set and restart auto-neg
 *  @hw: pointer to hardware structure
 *
 *  Restart auto-negotiation and PHY and waits for completion.
 **/
s32 ixgbe_setup_phy_link_generic(struct ixgbe_hw *hw)
{
	s32 status = IXGBE_SUCCESS;
	u16 autoneg_reg = IXGBE_MII_AUTONEG_REG;
	bool autoneg = FALSE;
	ixgbe_link_speed speed;

	DEBUGFUNC("ixgbe_setup_phy_link_generic");

	ixgbe_get_copper_link_capabilities_generic(hw, &speed, &autoneg);

	/* Set or unset auto-negotiation 10G advertisement */
	hw->phy.ops.read_reg(hw, IXGBE_MII_10GBASE_T_AUTONEG_CTRL_REG,
			     IXGBE_MDIO_AUTO_NEG_DEV_TYPE,
			     &autoneg_reg);

	autoneg_reg &= ~IXGBE_MII_10GBASE_T_ADVERTISE;
	if ((hw->phy.autoneg_advertised & IXGBE_LINK_SPEED_10GB_FULL) &&
	    (speed & IXGBE_LINK_SPEED_10GB_FULL))
		autoneg_reg |= IXGBE_MII_10GBASE_T_ADVERTISE;

	hw->phy.ops.write_reg(hw, IXGBE_MII_10GBASE_T_AUTONEG_CTRL_REG,
			      IXGBE_MDIO_AUTO_NEG_DEV_TYPE,
			      autoneg_reg);

	hw->phy.ops.read_reg(hw, IXGBE_MII_AUTONEG_VENDOR_PROVISION_1_REG,
			     IXGBE_MDIO_AUTO_NEG_DEV_TYPE,
			     &autoneg_reg);

	if (hw->mac.type == ixgbe_mac_X550) {
		/* Set or unset auto-negotiation 5G advertisement */
		autoneg_reg &= ~IXGBE_MII_5GBASE_T_ADVERTISE;
		if ((hw->phy.autoneg_advertised & IXGBE_LINK_SPEED_5GB_FULL) &&
		    (speed & IXGBE_LINK_SPEED_5GB_FULL))
			autoneg_reg |= IXGBE_MII_5GBASE_T_ADVERTISE;

		/* Set or unset auto-negotiation 2.5G advertisement */
		autoneg_reg &= ~IXGBE_MII_2_5GBASE_T_ADVERTISE;
		if ((hw->phy.autoneg_advertised &
		     IXGBE_LINK_SPEED_2_5GB_FULL) &&
		    (speed & IXGBE_LINK_SPEED_2_5GB_FULL))
			autoneg_reg |= IXGBE_MII_2_5GBASE_T_ADVERTISE;
	}

	/* Set or unset auto-negotiation 1G advertisement */
	autoneg_reg &= ~IXGBE_MII_1GBASE_T_ADVERTISE;
	if ((hw->phy.autoneg_advertised & IXGBE_LINK_SPEED_1GB_FULL) &&
	    (speed & IXGBE_LINK_SPEED_1GB_FULL))
		autoneg_reg |= IXGBE_MII_1GBASE_T_ADVERTISE;

	hw->phy.ops.write_reg(hw, IXGBE_MII_AUTONEG_VENDOR_PROVISION_1_REG,
			      IXGBE_MDIO_AUTO_NEG_DEV_TYPE,
			      autoneg_reg);

	/* Set or unset auto-negotiation 100M advertisement */
	hw->phy.ops.read_reg(hw, IXGBE_MII_AUTONEG_ADVERTISE_REG,
			     IXGBE_MDIO_AUTO_NEG_DEV_TYPE,
			     &autoneg_reg);

	autoneg_reg &= ~(IXGBE_MII_100BASE_T_ADVERTISE |
			 IXGBE_MII_100BASE_T_ADVERTISE_HALF);
	if ((hw->phy.autoneg_advertised & IXGBE_LINK_SPEED_100_FULL) &&
	    (speed & IXGBE_LINK_SPEED_100_FULL))
		autoneg_reg |= IXGBE_MII_100BASE_T_ADVERTISE;

	hw->phy.ops.write_reg(hw, IXGBE_MII_AUTONEG_ADVERTISE_REG,
			      IXGBE_MDIO_AUTO_NEG_DEV_TYPE,
			      autoneg_reg);

	/* Blocked by MNG FW so don't reset PHY */
	if (ixgbe_check_reset_blocked(hw))
		return status;

	/* Restart PHY auto-negotiation. */
	hw->phy.ops.read_reg(hw, IXGBE_MDIO_AUTO_NEG_CONTROL,
			     IXGBE_MDIO_AUTO_NEG_DEV_TYPE, &autoneg_reg);

	autoneg_reg |= IXGBE_MII_RESTART;

	hw->phy.ops.write_reg(hw, IXGBE_MDIO_AUTO_NEG_CONTROL,
			      IXGBE_MDIO_AUTO_NEG_DEV_TYPE, autoneg_reg);

	return status;
}

/**
 *  ixgbe_setup_phy_link_speed_generic - Sets the auto advertised capabilities
 *  @hw: pointer to hardware structure
 *  @speed: new link speed
 *  @autoneg_wait_to_complete: unused
 **/
s32 ixgbe_setup_phy_link_speed_generic(struct ixgbe_hw *hw,
				       ixgbe_link_speed speed,
				       bool autoneg_wait_to_complete)
{
	UNREFERENCED_1PARAMETER(autoneg_wait_to_complete);

	DEBUGFUNC("ixgbe_setup_phy_link_speed_generic");

	/*
	 * Clear autoneg_advertised and set new values based on input link
	 * speed.
	 */
	hw->phy.autoneg_advertised = 0;

	if (speed & IXGBE_LINK_SPEED_10GB_FULL)
		hw->phy.autoneg_advertised |= IXGBE_LINK_SPEED_10GB_FULL;

	if (speed & IXGBE_LINK_SPEED_5GB_FULL)
		hw->phy.autoneg_advertised |= IXGBE_LINK_SPEED_5GB_FULL;

	if (speed & IXGBE_LINK_SPEED_2_5GB_FULL)
		hw->phy.autoneg_advertised |= IXGBE_LINK_SPEED_2_5GB_FULL;

	if (speed & IXGBE_LINK_SPEED_1GB_FULL)
		hw->phy.autoneg_advertised |= IXGBE_LINK_SPEED_1GB_FULL;

	if (speed & IXGBE_LINK_SPEED_100_FULL)
		hw->phy.autoneg_advertised |= IXGBE_LINK_SPEED_100_FULL;

	if (speed & IXGBE_LINK_SPEED_10_FULL)
		hw->phy.autoneg_advertised |= IXGBE_LINK_SPEED_10_FULL;

	/* Setup link based on the new speed settings */
	ixgbe_setup_phy_link(hw);

	return IXGBE_SUCCESS;
}

/**
 * ixgbe_get_copper_speeds_supported - Get copper link speeds from phy
 * @hw: pointer to hardware structure
 *
 * Determines the supported link capabilities by reading the PHY auto
 * negotiation register.
 **/
static s32 ixgbe_get_copper_speeds_supported(struct ixgbe_hw *hw)
{
	s32 status;
	u16 speed_ability;

	status = hw->phy.ops.read_reg(hw, IXGBE_MDIO_PHY_SPEED_ABILITY,
				      IXGBE_MDIO_PMA_PMD_DEV_TYPE,
				      &speed_ability);
	if (status)
		return status;

	if (speed_ability & IXGBE_MDIO_PHY_SPEED_10G)
		hw->phy.speeds_supported |= IXGBE_LINK_SPEED_10GB_FULL;
	if (speed_ability & IXGBE_MDIO_PHY_SPEED_1G)
		hw->phy.speeds_supported |= IXGBE_LINK_SPEED_1GB_FULL;
	if (speed_ability & IXGBE_MDIO_PHY_SPEED_100M)
		hw->phy.speeds_supported |= IXGBE_LINK_SPEED_100_FULL;

	switch (hw->mac.type) {
	case ixgbe_mac_X550:
		hw->phy.speeds_supported |= IXGBE_LINK_SPEED_2_5GB_FULL;
		hw->phy.speeds_supported |= IXGBE_LINK_SPEED_5GB_FULL;
		break;
	case ixgbe_mac_X550EM_x:
	case ixgbe_mac_X550EM_a:
		hw->phy.speeds_supported &= ~IXGBE_LINK_SPEED_100_FULL;
		break;
	default:
		break;
	}

	return status;
}

/**
 *  ixgbe_get_copper_link_capabilities_generic - Determines link capabilities
 *  @hw: pointer to hardware structure
 *  @speed: pointer to link speed
 *  @autoneg: boolean auto-negotiation value
 **/
s32 ixgbe_get_copper_link_capabilities_generic(struct ixgbe_hw *hw,
					       ixgbe_link_speed *speed,
					       bool *autoneg)
{
	s32 status = IXGBE_SUCCESS;

	DEBUGFUNC("ixgbe_get_copper_link_capabilities_generic");

	*autoneg = TRUE;
	if (!hw->phy.speeds_supported)
		status = ixgbe_get_copper_speeds_supported(hw);

	*speed = hw->phy.speeds_supported;
	return status;
}

/**
 *  ixgbe_check_phy_link_tnx - Determine link and speed status
 *  @hw: pointer to hardware structure
 *  @speed: current link speed
 *  @link_up: TRUE is link is up, FALSE otherwise
 *
 *  Reads the VS1 register to determine if link is up and the current speed for
 *  the PHY.
 **/
s32 ixgbe_check_phy_link_tnx(struct ixgbe_hw *hw, ixgbe_link_speed *speed,
			     bool *link_up)
{
	s32 status = IXGBE_SUCCESS;
	u32 time_out;
	u32 max_time_out = 10;
	u16 phy_link = 0;
	u16 phy_speed = 0;
	u16 phy_data = 0;

	DEBUGFUNC("ixgbe_check_phy_link_tnx");

	/* Initialize speed and link to default case */
	*link_up = FALSE;
	*speed = IXGBE_LINK_SPEED_10GB_FULL;

	/*
	 * Check current speed and link status of the PHY register.
	 * This is a vendor specific register and may have to
	 * be changed for other copper PHYs.
	 */
	for (time_out = 0; time_out < max_time_out; time_out++) {
		usec_delay(10);
		status = hw->phy.ops.read_reg(hw,
					IXGBE_MDIO_VENDOR_SPECIFIC_1_STATUS,
					IXGBE_MDIO_VENDOR_SPECIFIC_1_DEV_TYPE,
					&phy_data);
		phy_link = phy_data & IXGBE_MDIO_VENDOR_SPECIFIC_1_LINK_STATUS;
		phy_speed = phy_data &
				 IXGBE_MDIO_VENDOR_SPECIFIC_1_SPEED_STATUS;
		if (phy_link == IXGBE_MDIO_VENDOR_SPECIFIC_1_LINK_STATUS) {
			*link_up = TRUE;
			if (phy_speed ==
			    IXGBE_MDIO_VENDOR_SPECIFIC_1_SPEED_STATUS)
				*speed = IXGBE_LINK_SPEED_1GB_FULL;
			break;
		}
	}

	return status;
}

/**
 *	ixgbe_setup_phy_link_tnx - Set and restart auto-neg
 *	@hw: pointer to hardware structure
 *
 *	Restart auto-negotiation and PHY and waits for completion.
 **/
s32 ixgbe_setup_phy_link_tnx(struct ixgbe_hw *hw)
{
	s32 status = IXGBE_SUCCESS;
	u16 autoneg_reg = IXGBE_MII_AUTONEG_REG;
	bool autoneg = FALSE;
	ixgbe_link_speed speed;

	DEBUGFUNC("ixgbe_setup_phy_link_tnx");

	ixgbe_get_copper_link_capabilities_generic(hw, &speed, &autoneg);

	if (speed & IXGBE_LINK_SPEED_10GB_FULL) {
		/* Set or unset auto-negotiation 10G advertisement */
		hw->phy.ops.read_reg(hw, IXGBE_MII_10GBASE_T_AUTONEG_CTRL_REG,
				     IXGBE_MDIO_AUTO_NEG_DEV_TYPE,
				     &autoneg_reg);

		autoneg_reg &= ~IXGBE_MII_10GBASE_T_ADVERTISE;
		if (hw->phy.autoneg_advertised & IXGBE_LINK_SPEED_10GB_FULL)
			autoneg_reg |= IXGBE_MII_10GBASE_T_ADVERTISE;

		hw->phy.ops.write_reg(hw, IXGBE_MII_10GBASE_T_AUTONEG_CTRL_REG,
				      IXGBE_MDIO_AUTO_NEG_DEV_TYPE,
				      autoneg_reg);
	}

	if (speed & IXGBE_LINK_SPEED_1GB_FULL) {
		/* Set or unset auto-negotiation 1G advertisement */
		hw->phy.ops.read_reg(hw, IXGBE_MII_AUTONEG_XNP_TX_REG,
				     IXGBE_MDIO_AUTO_NEG_DEV_TYPE,
				     &autoneg_reg);

		autoneg_reg &= ~IXGBE_MII_1GBASE_T_ADVERTISE_XNP_TX;
		if (hw->phy.autoneg_advertised & IXGBE_LINK_SPEED_1GB_FULL)
			autoneg_reg |= IXGBE_MII_1GBASE_T_ADVERTISE_XNP_TX;

		hw->phy.ops.write_reg(hw, IXGBE_MII_AUTONEG_XNP_TX_REG,
				      IXGBE_MDIO_AUTO_NEG_DEV_TYPE,
				      autoneg_reg);
	}

	if (speed & IXGBE_LINK_SPEED_100_FULL) {
		/* Set or unset auto-negotiation 100M advertisement */
		hw->phy.ops.read_reg(hw, IXGBE_MII_AUTONEG_ADVERTISE_REG,
				     IXGBE_MDIO_AUTO_NEG_DEV_TYPE,
				     &autoneg_reg);

		autoneg_reg &= ~IXGBE_MII_100BASE_T_ADVERTISE;
		if (hw->phy.autoneg_advertised & IXGBE_LINK_SPEED_100_FULL)
			autoneg_reg |= IXGBE_MII_100BASE_T_ADVERTISE;

		hw->phy.ops.write_reg(hw, IXGBE_MII_AUTONEG_ADVERTISE_REG,
				      IXGBE_MDIO_AUTO_NEG_DEV_TYPE,
				      autoneg_reg);
	}

	/* Blocked by MNG FW so don't reset PHY */
	if (ixgbe_check_reset_blocked(hw))
		return status;

	/* Restart PHY auto-negotiation. */
	hw->phy.ops.read_reg(hw, IXGBE_MDIO_AUTO_NEG_CONTROL,
			     IXGBE_MDIO_AUTO_NEG_DEV_TYPE, &autoneg_reg);

	autoneg_reg |= IXGBE_MII_RESTART;

	hw->phy.ops.write_reg(hw, IXGBE_MDIO_AUTO_NEG_CONTROL,
			      IXGBE_MDIO_AUTO_NEG_DEV_TYPE, autoneg_reg);

	return status;
}

/**
 *  ixgbe_get_phy_firmware_version_tnx - Gets the PHY Firmware Version
 *  @hw: pointer to hardware structure
 *  @firmware_version: pointer to the PHY Firmware Version
 **/
s32 ixgbe_get_phy_firmware_version_tnx(struct ixgbe_hw *hw,
				       u16 *firmware_version)
{
	s32 status;

	DEBUGFUNC("ixgbe_get_phy_firmware_version_tnx");

	status = hw->phy.ops.read_reg(hw, TNX_FW_REV,
				      IXGBE_MDIO_VENDOR_SPECIFIC_1_DEV_TYPE,
				      firmware_version);

	return status;
}

/**
 *  ixgbe_get_phy_firmware_version_generic - Gets the PHY Firmware Version
 *  @hw: pointer to hardware structure
 *  @firmware_version: pointer to the PHY Firmware Version
 **/
s32 ixgbe_get_phy_firmware_version_generic(struct ixgbe_hw *hw,
					   u16 *firmware_version)
{
	s32 status;

	DEBUGFUNC("ixgbe_get_phy_firmware_version_generic");

	status = hw->phy.ops.read_reg(hw, AQ_FW_REV,
				      IXGBE_MDIO_VENDOR_SPECIFIC_1_DEV_TYPE,
				      firmware_version);

	return status;
}

/**
 *  ixgbe_reset_phy_nl - Performs a PHY reset
 *  @hw: pointer to hardware structure
 **/
s32 ixgbe_reset_phy_nl(struct ixgbe_hw *hw)
{
	u16 phy_offset, control, eword, edata, block_crc;
	bool end_data = FALSE;
	u16 list_offset, data_offset;
	u16 phy_data = 0;
	s32 ret_val = IXGBE_SUCCESS;
	u32 i;

	DEBUGFUNC("ixgbe_reset_phy_nl");

	/* Blocked by MNG FW so bail */
	if (ixgbe_check_reset_blocked(hw))
		goto out;

	hw->phy.ops.read_reg(hw, IXGBE_MDIO_PHY_XS_CONTROL,
			     IXGBE_MDIO_PHY_XS_DEV_TYPE, &phy_data);

	/* reset the PHY and poll for completion */
	hw->phy.ops.write_reg(hw, IXGBE_MDIO_PHY_XS_CONTROL,
			      IXGBE_MDIO_PHY_XS_DEV_TYPE,
			      (phy_data | IXGBE_MDIO_PHY_XS_RESET));

	for (i = 0; i < 100; i++) {
		hw->phy.ops.read_reg(hw, IXGBE_MDIO_PHY_XS_CONTROL,
				     IXGBE_MDIO_PHY_XS_DEV_TYPE, &phy_data);
		if ((phy_data & IXGBE_MDIO_PHY_XS_RESET) == 0)
			break;
		msec_delay(10);
	}

	if ((phy_data & IXGBE_MDIO_PHY_XS_RESET) != 0) {
		DEBUGOUT("PHY reset did not complete.\n");
		ret_val = IXGBE_ERR_PHY;
		goto out;
	}

	/* Get init offsets */
	ret_val = ixgbe_get_sfp_init_sequence_offsets(hw, &list_offset,
						      &data_offset);
	if (ret_val != IXGBE_SUCCESS)
		goto out;

	ret_val = hw->eeprom.ops.read(hw, data_offset, &block_crc);
	data_offset++;
	while (!end_data) {
		/*
		 * Read control word from PHY init contents offset
		 */
		ret_val = hw->eeprom.ops.read(hw, data_offset, &eword);
		if (ret_val)
			goto err_eeprom;
		control = (eword & IXGBE_CONTROL_MASK_NL) >>
			   IXGBE_CONTROL_SHIFT_NL;
		edata = eword & IXGBE_DATA_MASK_NL;
		switch (control) {
		case IXGBE_DELAY_NL:
			data_offset++;
			DEBUGOUT1("DELAY: %d MS\n", edata);
			msec_delay(edata);
			break;
		case IXGBE_DATA_NL:
			DEBUGOUT("DATA:\n");
			data_offset++;
			ret_val = hw->eeprom.ops.read(hw, data_offset,
						      &phy_offset);
			if (ret_val)
				goto err_eeprom;
			data_offset++;
			for (i = 0; i < edata; i++) {
				ret_val = hw->eeprom.ops.read(hw, data_offset,
							      &eword);
				if (ret_val)
					goto err_eeprom;
				hw->phy.ops.write_reg(hw, phy_offset,
						      IXGBE_TWINAX_DEV, eword);
				DEBUGOUT2("Wrote %4.4x to %4.4x\n", eword,
					  phy_offset);
				data_offset++;
				phy_offset++;
			}
			break;
		case IXGBE_CONTROL_NL:
			data_offset++;
			DEBUGOUT("CONTROL:\n");
			if (edata == IXGBE_CONTROL_EOL_NL) {
				DEBUGOUT("EOL\n");
				end_data = TRUE;
			} else if (edata == IXGBE_CONTROL_SOL_NL) {
				DEBUGOUT("SOL\n");
			} else {
				DEBUGOUT("Bad control value\n");
				ret_val = IXGBE_ERR_PHY;
				goto out;
			}
			break;
		default:
			DEBUGOUT("Bad control type\n");
			ret_val = IXGBE_ERR_PHY;
			goto out;
		}
	}

out:
	return ret_val;

err_eeprom:
	ERROR_REPORT2(IXGBE_ERROR_INVALID_STATE,
		      "eeprom read at offset %d failed", data_offset);
	return IXGBE_ERR_PHY;
}

/**
 *  ixgbe_identify_module_generic - Identifies module type
 *  @hw: pointer to hardware structure
 *
 *  Determines HW type and calls appropriate function.
 **/
s32 ixgbe_identify_module_generic(struct ixgbe_hw *hw)
{
	s32 status = IXGBE_ERR_SFP_NOT_PRESENT;

	DEBUGFUNC("ixgbe_identify_module_generic");

	switch (hw->mac.ops.get_media_type(hw)) {
	case ixgbe_media_type_fiber:
		status = ixgbe_identify_sfp_module_generic(hw);
		break;

	case ixgbe_media_type_fiber_qsfp:
		status = ixgbe_identify_qsfp_module_generic(hw);
		break;

	default:
		hw->phy.sfp_type = ixgbe_sfp_type_not_present;
		status = IXGBE_ERR_SFP_NOT_PRESENT;
		break;
	}

	return status;
}

/**
 *  ixgbe_identify_sfp_module_generic - Identifies SFP modules
 *  @hw: pointer to hardware structure
 *
 *  Searches for and identifies the SFP module and assigns appropriate PHY type.
 **/
s32 ixgbe_identify_sfp_module_generic(struct ixgbe_hw *hw)
{
	s32 status = IXGBE_ERR_PHY_ADDR_INVALID;
	u32 vendor_oui = 0;
	enum ixgbe_sfp_type stored_sfp_type = hw->phy.sfp_type;
	u8 identifier = 0;
	u8 comp_codes_1g = 0;
	u8 comp_codes_10g = 0;
	u8 oui_bytes[3] = {0, 0, 0};
	u8 cable_tech = 0;
	u8 cable_spec = 0;
	u16 enforce_sfp = 0;

	DEBUGFUNC("ixgbe_identify_sfp_module_generic");

	if (hw->mac.ops.get_media_type(hw) != ixgbe_media_type_fiber) {
		hw->phy.sfp_type = ixgbe_sfp_type_not_present;
		status = IXGBE_ERR_SFP_NOT_PRESENT;
		goto out;
	}

	/* LAN ID is needed for I2C access */
	hw->mac.ops.set_lan_id(hw);

	status = hw->phy.ops.read_i2c_eeprom(hw,
					     IXGBE_SFF_IDENTIFIER,
					     &identifier);

	if (status != IXGBE_SUCCESS)
		goto err_read_i2c_eeprom;

	if (identifier != IXGBE_SFF_IDENTIFIER_SFP) {
		hw->phy.type = ixgbe_phy_sfp_unsupported;
		status = IXGBE_ERR_SFP_NOT_SUPPORTED;
	} else {
		status = hw->phy.ops.read_i2c_eeprom(hw,
						     IXGBE_SFF_1GBE_COMP_CODES,
						     &comp_codes_1g);

		if (status != IXGBE_SUCCESS)
			goto err_read_i2c_eeprom;

		status = hw->phy.ops.read_i2c_eeprom(hw,
						     IXGBE_SFF_10GBE_COMP_CODES,
						     &comp_codes_10g);

		if (status != IXGBE_SUCCESS)
			goto err_read_i2c_eeprom;
		status = hw->phy.ops.read_i2c_eeprom(hw,
						     IXGBE_SFF_CABLE_TECHNOLOGY,
						     &cable_tech);

		if (status != IXGBE_SUCCESS)
			goto err_read_i2c_eeprom;

		 /* ID Module
		  * =========
		  * 0   SFP_DA_CU
		  * 1   SFP_SR
		  * 2   SFP_LR
		  * 3   SFP_DA_CORE0 - 82599-specific
		  * 4   SFP_DA_CORE1 - 82599-specific
		  * 5   SFP_SR/LR_CORE0 - 82599-specific
		  * 6   SFP_SR/LR_CORE1 - 82599-specific
		  * 7   SFP_act_lmt_DA_CORE0 - 82599-specific
		  * 8   SFP_act_lmt_DA_CORE1 - 82599-specific
		  * 9   SFP_1g_cu_CORE0 - 82599-specific
		  * 10  SFP_1g_cu_CORE1 - 82599-specific
		  * 11  SFP_1g_sx_CORE0 - 82599-specific
		  * 12  SFP_1g_sx_CORE1 - 82599-specific
		  */
		if (hw->mac.type == ixgbe_mac_82598EB) {
			if (cable_tech & IXGBE_SFF_DA_PASSIVE_CABLE)
				hw->phy.sfp_type = ixgbe_sfp_type_da_cu;
			else if (comp_codes_10g & IXGBE_SFF_10GBASESR_CAPABLE)
				hw->phy.sfp_type = ixgbe_sfp_type_sr;
			else if (comp_codes_10g & IXGBE_SFF_10GBASELR_CAPABLE)
				hw->phy.sfp_type = ixgbe_sfp_type_lr;
			else
				hw->phy.sfp_type = ixgbe_sfp_type_unknown;
		} else {
			if (cable_tech & IXGBE_SFF_DA_PASSIVE_CABLE) {
				if (hw->bus.lan_id == 0)
					hw->phy.sfp_type =
						     ixgbe_sfp_type_da_cu_core0;
				else
					hw->phy.sfp_type =
						     ixgbe_sfp_type_da_cu_core1;
			} else if (cable_tech & IXGBE_SFF_DA_ACTIVE_CABLE) {
				hw->phy.ops.read_i2c_eeprom(
						hw, IXGBE_SFF_CABLE_SPEC_COMP,
						&cable_spec);
				if (cable_spec &
				    IXGBE_SFF_DA_SPEC_ACTIVE_LIMITING) {
					if (hw->bus.lan_id == 0)
						hw->phy.sfp_type =
						ixgbe_sfp_type_da_act_lmt_core0;
					else
						hw->phy.sfp_type =
						ixgbe_sfp_type_da_act_lmt_core1;
				} else {
					hw->phy.sfp_type =
							ixgbe_sfp_type_unknown;
				}
			} else if (comp_codes_10g &
				   (IXGBE_SFF_10GBASESR_CAPABLE |
				    IXGBE_SFF_10GBASELR_CAPABLE)) {
				if (hw->bus.lan_id == 0)
					hw->phy.sfp_type =
						      ixgbe_sfp_type_srlr_core0;
				else
					hw->phy.sfp_type =
						      ixgbe_sfp_type_srlr_core1;
			} else if (comp_codes_1g & IXGBE_SFF_1GBASET_CAPABLE) {
				if (hw->bus.lan_id == 0)
					hw->phy.sfp_type =
						ixgbe_sfp_type_1g_cu_core0;
				else
					hw->phy.sfp_type =
						ixgbe_sfp_type_1g_cu_core1;
			} else if (comp_codes_1g & IXGBE_SFF_1GBASESX_CAPABLE) {
				if (hw->bus.lan_id == 0)
					hw->phy.sfp_type =
						ixgbe_sfp_type_1g_sx_core0;
				else
					hw->phy.sfp_type =
						ixgbe_sfp_type_1g_sx_core1;
			} else if (comp_codes_1g & IXGBE_SFF_1GBASELX_CAPABLE) {
				if (hw->bus.lan_id == 0)
					hw->phy.sfp_type =
						ixgbe_sfp_type_1g_lx_core0;
				else
					hw->phy.sfp_type =
						ixgbe_sfp_type_1g_lx_core1;
			} else {
				hw->phy.sfp_type = ixgbe_sfp_type_unknown;
			}
		}

		if (hw->phy.sfp_type != stored_sfp_type)
			hw->phy.sfp_setup_needed = TRUE;

		/* Determine if the SFP+ PHY is dual speed or not. */
		hw->phy.multispeed_fiber = FALSE;
		if (((comp_codes_1g & IXGBE_SFF_1GBASESX_CAPABLE) &&
		   (comp_codes_10g & IXGBE_SFF_10GBASESR_CAPABLE)) ||
		   ((comp_codes_1g & IXGBE_SFF_1GBASELX_CAPABLE) &&
		   (comp_codes_10g & IXGBE_SFF_10GBASELR_CAPABLE)))
			hw->phy.multispeed_fiber = TRUE;

		/* Determine PHY vendor */
		if (hw->phy.type != ixgbe_phy_nl) {
			hw->phy.id = identifier;
			status = hw->phy.ops.read_i2c_eeprom(hw,
						    IXGBE_SFF_VENDOR_OUI_BYTE0,
						    &oui_bytes[0]);

			if (status != IXGBE_SUCCESS)
				goto err_read_i2c_eeprom;

			status = hw->phy.ops.read_i2c_eeprom(hw,
						    IXGBE_SFF_VENDOR_OUI_BYTE1,
						    &oui_bytes[1]);

			if (status != IXGBE_SUCCESS)
				goto err_read_i2c_eeprom;

			status = hw->phy.ops.read_i2c_eeprom(hw,
						    IXGBE_SFF_VENDOR_OUI_BYTE2,
						    &oui_bytes[2]);

			if (status != IXGBE_SUCCESS)
				goto err_read_i2c_eeprom;

			vendor_oui =
			  ((oui_bytes[0] << IXGBE_SFF_VENDOR_OUI_BYTE0_SHIFT) |
			   (oui_bytes[1] << IXGBE_SFF_VENDOR_OUI_BYTE1_SHIFT) |
			   (oui_bytes[2] << IXGBE_SFF_VENDOR_OUI_BYTE2_SHIFT));

			switch (vendor_oui) {
			case IXGBE_SFF_VENDOR_OUI_TYCO:
				if (cable_tech & IXGBE_SFF_DA_PASSIVE_CABLE)
					hw->phy.type =
						    ixgbe_phy_sfp_passive_tyco;
				break;
			case IXGBE_SFF_VENDOR_OUI_FTL:
				if (cable_tech & IXGBE_SFF_DA_ACTIVE_CABLE)
					hw->phy.type = ixgbe_phy_sfp_ftl_active;
				else
					hw->phy.type = ixgbe_phy_sfp_ftl;
				break;
			case IXGBE_SFF_VENDOR_OUI_AVAGO:
				hw->phy.type = ixgbe_phy_sfp_avago;
				break;
			case IXGBE_SFF_VENDOR_OUI_INTEL:
				hw->phy.type = ixgbe_phy_sfp_intel;
				break;
			default:
				hw->phy.type = ixgbe_phy_sfp_unknown;
				break;
			}
		}

		/* Allow any DA cable vendor */
		if (cable_tech & (IXGBE_SFF_DA_PASSIVE_CABLE |
			IXGBE_SFF_DA_ACTIVE_CABLE)) {
			if (cable_tech & IXGBE_SFF_DA_PASSIVE_CABLE)
				hw->phy.type = ixgbe_phy_sfp_passive_unknown;
			else if (cable_tech & IXGBE_SFF_DA_ACTIVE_CABLE)
				hw->phy.type = ixgbe_phy_sfp_active_unknown;
			status = IXGBE_SUCCESS;
			goto out;
		}

		/* Verify supported 1G SFP modules */
		if (comp_codes_10g == 0 &&
		    !(hw->phy.sfp_type == ixgbe_sfp_type_1g_cu_core1 ||
		      hw->phy.sfp_type == ixgbe_sfp_type_1g_cu_core0 ||
		      hw->phy.sfp_type == ixgbe_sfp_type_1g_lx_core0 ||
		      hw->phy.sfp_type == ixgbe_sfp_type_1g_lx_core1 ||
		      hw->phy.sfp_type == ixgbe_sfp_type_1g_sx_core0 ||
		      hw->phy.sfp_type == ixgbe_sfp_type_1g_sx_core1)) {
			hw->phy.type = ixgbe_phy_sfp_unsupported;
			status = IXGBE_ERR_SFP_NOT_SUPPORTED;
			goto out;
		}

		/* Anything else 82598-based is supported */
		if (hw->mac.type == ixgbe_mac_82598EB) {
			status = IXGBE_SUCCESS;
			goto out;
		}

		ixgbe_get_device_caps(hw, &enforce_sfp);
		if (!(enforce_sfp & IXGBE_DEVICE_CAPS_ALLOW_ANY_SFP) &&
		    !(hw->phy.sfp_type == ixgbe_sfp_type_1g_cu_core0 ||
		      hw->phy.sfp_type == ixgbe_sfp_type_1g_cu_core1 ||
		      hw->phy.sfp_type == ixgbe_sfp_type_1g_lx_core0 ||
		      hw->phy.sfp_type == ixgbe_sfp_type_1g_lx_core1 ||
		      hw->phy.sfp_type == ixgbe_sfp_type_1g_sx_core0 ||
		      hw->phy.sfp_type == ixgbe_sfp_type_1g_sx_core1)) {
			/* Make sure we're a supported PHY type */
			if (hw->phy.type == ixgbe_phy_sfp_intel) {
				status = IXGBE_SUCCESS;
			} else {
				if (hw->allow_unsupported_sfp == TRUE) {
					EWARN(hw, "WARNING: Intel (R) Network Connections are quality tested using Intel (R) Ethernet Optics. Using untested modules is not supported and may cause unstable operation or damage to the module or the adapter. Intel Corporation is not responsible for any harm caused by using untested modules.\n");
					status = IXGBE_SUCCESS;
				} else {
					DEBUGOUT("SFP+ module not supported\n");
					hw->phy.type =
						ixgbe_phy_sfp_unsupported;
					status = IXGBE_ERR_SFP_NOT_SUPPORTED;
				}
			}
		} else {
			status = IXGBE_SUCCESS;
		}
	}

out:
	return status;

err_read_i2c_eeprom:
	hw->phy.sfp_type = ixgbe_sfp_type_not_present;
	if (hw->phy.type != ixgbe_phy_nl) {
		hw->phy.id = 0;
		hw->phy.type = ixgbe_phy_unknown;
	}
	return IXGBE_ERR_SFP_NOT_PRESENT;
}

/**
 *  ixgbe_get_supported_phy_sfp_layer_generic - Returns physical layer type
 *  @hw: pointer to hardware structure
 *
 *  Determines physical layer capabilities of the current SFP.
 */
u64 ixgbe_get_supported_phy_sfp_layer_generic(struct ixgbe_hw *hw)
{
	u64 physical_layer = IXGBE_PHYSICAL_LAYER_UNKNOWN;
	u8 comp_codes_10g = 0;
	u8 comp_codes_1g = 0;

	DEBUGFUNC("ixgbe_get_supported_phy_sfp_layer_generic");

	hw->phy.ops.identify_sfp(hw);
	if (hw->phy.sfp_type == ixgbe_sfp_type_not_present)
		return physical_layer;

	switch (hw->phy.type) {
	case ixgbe_phy_sfp_passive_tyco:
	case ixgbe_phy_sfp_passive_unknown:
	case ixgbe_phy_qsfp_passive_unknown:
		physical_layer = IXGBE_PHYSICAL_LAYER_SFP_PLUS_CU;
		break;
	case ixgbe_phy_sfp_ftl_active:
	case ixgbe_phy_sfp_active_unknown:
	case ixgbe_phy_qsfp_active_unknown:
		physical_layer = IXGBE_PHYSICAL_LAYER_SFP_ACTIVE_DA;
		break;
	case ixgbe_phy_sfp_avago:
	case ixgbe_phy_sfp_ftl:
	case ixgbe_phy_sfp_intel:
	case ixgbe_phy_sfp_unknown:
		hw->phy.ops.read_i2c_eeprom(hw,
		      IXGBE_SFF_1GBE_COMP_CODES, &comp_codes_1g);
		hw->phy.ops.read_i2c_eeprom(hw,
		      IXGBE_SFF_10GBE_COMP_CODES, &comp_codes_10g);
		if (comp_codes_10g & IXGBE_SFF_10GBASESR_CAPABLE)
			physical_layer = IXGBE_PHYSICAL_LAYER_10GBASE_SR;
		else if (comp_codes_10g & IXGBE_SFF_10GBASELR_CAPABLE)
			physical_layer = IXGBE_PHYSICAL_LAYER_10GBASE_LR;
		else if (comp_codes_1g & IXGBE_SFF_1GBASET_CAPABLE)
			physical_layer = IXGBE_PHYSICAL_LAYER_1000BASE_T;
		else if (comp_codes_1g & IXGBE_SFF_1GBASESX_CAPABLE)
			physical_layer = IXGBE_PHYSICAL_LAYER_1000BASE_SX;
		break;
	case ixgbe_phy_qsfp_intel:
	case ixgbe_phy_qsfp_unknown:
		hw->phy.ops.read_i2c_eeprom(hw,
		      IXGBE_SFF_QSFP_10GBE_COMP, &comp_codes_10g);
		if (comp_codes_10g & IXGBE_SFF_10GBASESR_CAPABLE)
			physical_layer = IXGBE_PHYSICAL_LAYER_10GBASE_SR;
		else if (comp_codes_10g & IXGBE_SFF_10GBASELR_CAPABLE)
			physical_layer = IXGBE_PHYSICAL_LAYER_10GBASE_LR;
		break;
	default:
		break;
	}

	return physical_layer;
}

/**
 *  ixgbe_identify_qsfp_module_generic - Identifies QSFP modules
 *  @hw: pointer to hardware structure
 *
 *  Searches for and identifies the QSFP module and assigns appropriate PHY type
 **/
s32 ixgbe_identify_qsfp_module_generic(struct ixgbe_hw *hw)
{
	s32 status = IXGBE_ERR_PHY_ADDR_INVALID;
	u32 vendor_oui = 0;
	enum ixgbe_sfp_type stored_sfp_type = hw->phy.sfp_type;
	u8 identifier = 0;
	u8 comp_codes_1g = 0;
	u8 comp_codes_10g = 0;
	u8 oui_bytes[3] = {0, 0, 0};
	u16 enforce_sfp = 0;
	u8 connector = 0;
	u8 cable_length = 0;
	u8 device_tech = 0;
	bool active_cable = FALSE;

	DEBUGFUNC("ixgbe_identify_qsfp_module_generic");

	if (hw->mac.ops.get_media_type(hw) != ixgbe_media_type_fiber_qsfp) {
		hw->phy.sfp_type = ixgbe_sfp_type_not_present;
		status = IXGBE_ERR_SFP_NOT_PRESENT;
		goto out;
	}

	/* LAN ID is needed for I2C access */
	hw->mac.ops.set_lan_id(hw);

	status = hw->phy.ops.read_i2c_eeprom(hw, IXGBE_SFF_IDENTIFIER,
					     &identifier);

	if (status != IXGBE_SUCCESS)
		goto err_read_i2c_eeprom;

	if (identifier != IXGBE_SFF_IDENTIFIER_QSFP_PLUS) {
		hw->phy.type = ixgbe_phy_sfp_unsupported;
		status = IXGBE_ERR_SFP_NOT_SUPPORTED;
		goto out;
	}

	hw->phy.id = identifier;

	status = hw->phy.ops.read_i2c_eeprom(hw, IXGBE_SFF_QSFP_10GBE_COMP,
					     &comp_codes_10g);

	if (status != IXGBE_SUCCESS)
		goto err_read_i2c_eeprom;

	status = hw->phy.ops.read_i2c_eeprom(hw, IXGBE_SFF_QSFP_1GBE_COMP,
					     &comp_codes_1g);

	if (status != IXGBE_SUCCESS)
		goto err_read_i2c_eeprom;

	if (comp_codes_10g & IXGBE_SFF_QSFP_DA_PASSIVE_CABLE) {
		hw->phy.type = ixgbe_phy_qsfp_passive_unknown;
		if (hw->bus.lan_id == 0)
			hw->phy.sfp_type = ixgbe_sfp_type_da_cu_core0;
		else
			hw->phy.sfp_type = ixgbe_sfp_type_da_cu_core1;
	} else if (comp_codes_10g & (IXGBE_SFF_10GBASESR_CAPABLE |
				     IXGBE_SFF_10GBASELR_CAPABLE)) {
		if (hw->bus.lan_id == 0)
			hw->phy.sfp_type = ixgbe_sfp_type_srlr_core0;
		else
			hw->phy.sfp_type = ixgbe_sfp_type_srlr_core1;
	} else {
		if (comp_codes_10g & IXGBE_SFF_QSFP_DA_ACTIVE_CABLE)
			active_cable = TRUE;

		if (!active_cable) {
			/* check for active DA cables that pre-date
			 * SFF-8436 v3.6 */
			hw->phy.ops.read_i2c_eeprom(hw,
					IXGBE_SFF_QSFP_CONNECTOR,
					&connector);

			hw->phy.ops.read_i2c_eeprom(hw,
					IXGBE_SFF_QSFP_CABLE_LENGTH,
					&cable_length);

			hw->phy.ops.read_i2c_eeprom(hw,
					IXGBE_SFF_QSFP_DEVICE_TECH,
					&device_tech);

			if ((connector ==
				     IXGBE_SFF_QSFP_CONNECTOR_NOT_SEPARABLE) &&
			    (cable_length > 0) &&
			    ((device_tech >> 4) ==
				     IXGBE_SFF_QSFP_TRANSMITER_850NM_VCSEL))
				active_cable = TRUE;
		}

		if (active_cable) {
			hw->phy.type = ixgbe_phy_qsfp_active_unknown;
			if (hw->bus.lan_id == 0)
				hw->phy.sfp_type =
						ixgbe_sfp_type_da_act_lmt_core0;
			else
				hw->phy.sfp_type =
						ixgbe_sfp_type_da_act_lmt_core1;
		} else {
			/* unsupported module type */
			hw->phy.type = ixgbe_phy_sfp_unsupported;
			status = IXGBE_ERR_SFP_NOT_SUPPORTED;
			goto out;
		}
	}

	if (hw->phy.sfp_type != stored_sfp_type)
		hw->phy.sfp_setup_needed = TRUE;

	/* Determine if the QSFP+ PHY is dual speed or not. */
	hw->phy.multispeed_fiber = FALSE;
	if (((comp_codes_1g & IXGBE_SFF_1GBASESX_CAPABLE) &&
	   (comp_codes_10g & IXGBE_SFF_10GBASESR_CAPABLE)) ||
	   ((comp_codes_1g & IXGBE_SFF_1GBASELX_CAPABLE) &&
	   (comp_codes_10g & IXGBE_SFF_10GBASELR_CAPABLE)))
		hw->phy.multispeed_fiber = TRUE;

	/* Determine PHY vendor for optical modules */
	if (comp_codes_10g & (IXGBE_SFF_10GBASESR_CAPABLE |
			      IXGBE_SFF_10GBASELR_CAPABLE))  {
		status = hw->phy.ops.read_i2c_eeprom(hw,
					    IXGBE_SFF_QSFP_VENDOR_OUI_BYTE0,
					    &oui_bytes[0]);

		if (status != IXGBE_SUCCESS)
			goto err_read_i2c_eeprom;

		status = hw->phy.ops.read_i2c_eeprom(hw,
					    IXGBE_SFF_QSFP_VENDOR_OUI_BYTE1,
					    &oui_bytes[1]);

		if (status != IXGBE_SUCCESS)
			goto err_read_i2c_eeprom;

		status = hw->phy.ops.read_i2c_eeprom(hw,
					    IXGBE_SFF_QSFP_VENDOR_OUI_BYTE2,
					    &oui_bytes[2]);

		if (status != IXGBE_SUCCESS)
			goto err_read_i2c_eeprom;

		vendor_oui =
		  ((oui_bytes[0] << IXGBE_SFF_VENDOR_OUI_BYTE0_SHIFT) |
		   (oui_bytes[1] << IXGBE_SFF_VENDOR_OUI_BYTE1_SHIFT) |
		   (oui_bytes[2] << IXGBE_SFF_VENDOR_OUI_BYTE2_SHIFT));

		if (vendor_oui == IXGBE_SFF_VENDOR_OUI_INTEL)
			hw->phy.type = ixgbe_phy_qsfp_intel;
		else
			hw->phy.type = ixgbe_phy_qsfp_unknown;

		ixgbe_get_device_caps(hw, &enforce_sfp);
		if (!(enforce_sfp & IXGBE_DEVICE_CAPS_ALLOW_ANY_SFP)) {
			/* Make sure we're a supported PHY type */
			if (hw->phy.type == ixgbe_phy_qsfp_intel) {
				status = IXGBE_SUCCESS;
			} else {
				if (hw->allow_unsupported_sfp == TRUE) {
					EWARN(hw, "WARNING: Intel (R) Network Connections are quality tested using Intel (R) Ethernet Optics. Using untested modules is not supported and may cause unstable operation or damage to the module or the adapter. Intel Corporation is not responsible for any harm caused by using untested modules.\n");
					status = IXGBE_SUCCESS;
				} else {
					DEBUGOUT("QSFP module not supported\n");
					hw->phy.type =
						ixgbe_phy_sfp_unsupported;
					status = IXGBE_ERR_SFP_NOT_SUPPORTED;
				}
			}
		} else {
			status = IXGBE_SUCCESS;
		}
	}

out:
	return status;

err_read_i2c_eeprom:
	hw->phy.sfp_type = ixgbe_sfp_type_not_present;
	hw->phy.id = 0;
	hw->phy.type = ixgbe_phy_unknown;

	return IXGBE_ERR_SFP_NOT_PRESENT;
}

/**
 *  ixgbe_get_sfp_init_sequence_offsets - Provides offset of PHY init sequence
 *  @hw: pointer to hardware structure
 *  @list_offset: offset to the SFP ID list
 *  @data_offset: offset to the SFP data block
 *
 *  Checks the MAC's EEPROM to see if it supports a given SFP+ module type, if
 *  so it returns the offsets to the phy init sequence block.
 **/
s32 ixgbe_get_sfp_init_sequence_offsets(struct ixgbe_hw *hw,
					u16 *list_offset,
					u16 *data_offset)
{
	u16 sfp_id;
	u16 sfp_type = hw->phy.sfp_type;

	DEBUGFUNC("ixgbe_get_sfp_init_sequence_offsets");

	if (hw->phy.sfp_type == ixgbe_sfp_type_unknown)
		return IXGBE_ERR_SFP_NOT_SUPPORTED;

	if (hw->phy.sfp_type == ixgbe_sfp_type_not_present)
		return IXGBE_ERR_SFP_NOT_PRESENT;

	if ((hw->device_id == IXGBE_DEV_ID_82598_SR_DUAL_PORT_EM) &&
	    (hw->phy.sfp_type == ixgbe_sfp_type_da_cu))
		return IXGBE_ERR_SFP_NOT_SUPPORTED;

	/*
	 * Limiting active cables and 1G Phys must be initialized as
	 * SR modules
	 */
	if (sfp_type == ixgbe_sfp_type_da_act_lmt_core0 ||
	    sfp_type == ixgbe_sfp_type_1g_lx_core0 ||
	    sfp_type == ixgbe_sfp_type_1g_cu_core0 ||
	    sfp_type == ixgbe_sfp_type_1g_sx_core0)
		sfp_type = ixgbe_sfp_type_srlr_core0;
	else if (sfp_type == ixgbe_sfp_type_da_act_lmt_core1 ||
		 sfp_type == ixgbe_sfp_type_1g_lx_core1 ||
		 sfp_type == ixgbe_sfp_type_1g_cu_core1 ||
		 sfp_type == ixgbe_sfp_type_1g_sx_core1)
		sfp_type = ixgbe_sfp_type_srlr_core1;

	/* Read offset to PHY init contents */
	if (hw->eeprom.ops.read(hw, IXGBE_PHY_INIT_OFFSET_NL, list_offset)) {
		ERROR_REPORT2(IXGBE_ERROR_INVALID_STATE,
			      "eeprom read at offset %d failed",
			      IXGBE_PHY_INIT_OFFSET_NL);
		return IXGBE_ERR_SFP_NO_INIT_SEQ_PRESENT;
	}

	if ((!*list_offset) || (*list_offset == 0xFFFF))
		return IXGBE_ERR_SFP_NO_INIT_SEQ_PRESENT;

	/* Shift offset to first ID word */
	(*list_offset)++;

	/*
	 * Find the matching SFP ID in the EEPROM
	 * and program the init sequence
	 */
	if (hw->eeprom.ops.read(hw, *list_offset, &sfp_id))
		goto err_phy;

	while (sfp_id != IXGBE_PHY_INIT_END_NL) {
		if (sfp_id == sfp_type) {
			(*list_offset)++;
			if (hw->eeprom.ops.read(hw, *list_offset, data_offset))
				goto err_phy;
			if ((!*data_offset) || (*data_offset == 0xFFFF)) {
				DEBUGOUT("SFP+ module not supported\n");
				return IXGBE_ERR_SFP_NOT_SUPPORTED;
			} else {
				break;
			}
		} else {
			(*list_offset) += 2;
			if (hw->eeprom.ops.read(hw, *list_offset, &sfp_id))
				goto err_phy;
		}
	}

	if (sfp_id == IXGBE_PHY_INIT_END_NL) {
		DEBUGOUT("No matching SFP+ module found\n");
		return IXGBE_ERR_SFP_NOT_SUPPORTED;
	}

	return IXGBE_SUCCESS;

err_phy:
	ERROR_REPORT2(IXGBE_ERROR_INVALID_STATE,
		      "eeprom read at offset %d failed", *list_offset);
	return IXGBE_ERR_PHY;
}

/**
 *  ixgbe_read_i2c_eeprom_generic - Reads 8 bit EEPROM word over I2C interface
 *  @hw: pointer to hardware structure
 *  @byte_offset: EEPROM byte offset to read
 *  @eeprom_data: value read
 *
 *  Performs byte read operation to SFP module's EEPROM over I2C interface.
 **/
s32 ixgbe_read_i2c_eeprom_generic(struct ixgbe_hw *hw, u8 byte_offset,
				  u8 *eeprom_data)
{
	DEBUGFUNC("ixgbe_read_i2c_eeprom_generic");

	return hw->phy.ops.read_i2c_byte(hw, byte_offset,
					 IXGBE_I2C_EEPROM_DEV_ADDR,
					 eeprom_data);
}

/**
 *  ixgbe_read_i2c_sff8472_generic - Reads 8 bit word over I2C interface
 *  @hw: pointer to hardware structure
 *  @byte_offset: byte offset at address 0xA2
 *  @sff8472_data: value read
 *
 *  Performs byte read operation to SFP module's SFF-8472 data over I2C
 **/
static s32 ixgbe_read_i2c_sff8472_generic(struct ixgbe_hw *hw, u8 byte_offset,
					  u8 *sff8472_data)
{
	return hw->phy.ops.read_i2c_byte(hw, byte_offset,
					 IXGBE_I2C_EEPROM_DEV_ADDR2,
					 sff8472_data);
}

/**
 *  ixgbe_write_i2c_eeprom_generic - Writes 8 bit EEPROM word over I2C interface
 *  @hw: pointer to hardware structure
 *  @byte_offset: EEPROM byte offset to write
 *  @eeprom_data: value to write
 *
 *  Performs byte write operation to SFP module's EEPROM over I2C interface.
 **/
s32 ixgbe_write_i2c_eeprom_generic(struct ixgbe_hw *hw, u8 byte_offset,
				   u8 eeprom_data)
{
	DEBUGFUNC("ixgbe_write_i2c_eeprom_generic");

	return hw->phy.ops.write_i2c_byte(hw, byte_offset,
					  IXGBE_I2C_EEPROM_DEV_ADDR,
					  eeprom_data);
}

/**
 * ixgbe_is_sfp_probe - Returns TRUE if SFP is being detected
 * @hw: pointer to hardware structure
 * @offset: eeprom offset to be read
 * @addr: I2C address to be read
 */
static bool ixgbe_is_sfp_probe(struct ixgbe_hw *hw, u8 offset, u8 addr)
{
	if (addr == IXGBE_I2C_EEPROM_DEV_ADDR &&
	    offset == IXGBE_SFF_IDENTIFIER &&
	    hw->phy.sfp_type == ixgbe_sfp_type_not_present)
		return TRUE;
	return FALSE;
}

/**
 *  ixgbe_read_i2c_byte_generic_int - Reads 8 bit word over I2C
 *  @hw: pointer to hardware structure
 *  @byte_offset: byte offset to read
 *  @dev_addr: address to read from
 *  @data: value read
 *  @lock: TRUE if to take and release semaphore
 *
 *  Performs byte read operation to SFP module's EEPROM over I2C interface at
 *  a specified device address.
 **/
static s32 ixgbe_read_i2c_byte_generic_int(struct ixgbe_hw *hw, u8 byte_offset,
					   u8 dev_addr, u8 *data, bool lock)
{
	s32 status;
	u32 max_retry = 10;
	u32 retry = 0;
	u32 swfw_mask = hw->phy.phy_semaphore_mask;
	bool nack = 1;
	*data = 0;

	DEBUGFUNC("ixgbe_read_i2c_byte_generic");

	if (hw->mac.type >= ixgbe_mac_X550)
		max_retry = 3;
	if (ixgbe_is_sfp_probe(hw, byte_offset, dev_addr))
		max_retry = IXGBE_SFP_DETECT_RETRIES;

	do {
		if (lock && hw->mac.ops.acquire_swfw_sync(hw, swfw_mask))
			return IXGBE_ERR_SWFW_SYNC;

		ixgbe_i2c_start(hw);

		/* Device Address and write indication */
		status = ixgbe_clock_out_i2c_byte(hw, dev_addr);
		if (status != IXGBE_SUCCESS)
			goto fail;

		status = ixgbe_get_i2c_ack(hw);
		if (status != IXGBE_SUCCESS)
			goto fail;

		status = ixgbe_clock_out_i2c_byte(hw, byte_offset);
		if (status != IXGBE_SUCCESS)
			goto fail;

		status = ixgbe_get_i2c_ack(hw);
		if (status != IXGBE_SUCCESS)
			goto fail;

		ixgbe_i2c_start(hw);

		/* Device Address and read indication */
		status = ixgbe_clock_out_i2c_byte(hw, (dev_addr | 0x1));
		if (status != IXGBE_SUCCESS)
			goto fail;

		status = ixgbe_get_i2c_ack(hw);
		if (status != IXGBE_SUCCESS)
			goto fail;

		status = ixgbe_clock_in_i2c_byte(hw, data);
		if (status != IXGBE_SUCCESS)
			goto fail;

		status = ixgbe_clock_out_i2c_bit(hw, nack);
		if (status != IXGBE_SUCCESS)
			goto fail;

		ixgbe_i2c_stop(hw);
		if (lock)
			hw->mac.ops.release_swfw_sync(hw, swfw_mask);
		return IXGBE_SUCCESS;

fail:
		ixgbe_i2c_bus_clear(hw);
		if (lock) {
			hw->mac.ops.release_swfw_sync(hw, swfw_mask);
			msec_delay(100);
		}
		retry++;
		if (retry < max_retry)
			DEBUGOUT("I2C byte read error - Retrying.\n");
		else
			DEBUGOUT("I2C byte read error.\n");

	} while (retry < max_retry);

	return status;
}

/**
 *  ixgbe_read_i2c_byte_generic - Reads 8 bit word over I2C
 *  @hw: pointer to hardware structure
 *  @byte_offset: byte offset to read
 *  @dev_addr: address to read from
 *  @data: value read
 *
 *  Performs byte read operation to SFP module's EEPROM over I2C interface at
 *  a specified device address.
 **/
s32 ixgbe_read_i2c_byte_generic(struct ixgbe_hw *hw, u8 byte_offset,
				u8 dev_addr, u8 *data)
{
	return ixgbe_read_i2c_byte_generic_int(hw, byte_offset, dev_addr,
					       data, TRUE);
}

/**
 *  ixgbe_read_i2c_byte_generic_unlocked - Reads 8 bit word over I2C
 *  @hw: pointer to hardware structure
 *  @byte_offset: byte offset to read
 *  @dev_addr: address to read from
 *  @data: value read
 *
 *  Performs byte read operation to SFP module's EEPROM over I2C interface at
 *  a specified device address.
 **/
s32 ixgbe_read_i2c_byte_generic_unlocked(struct ixgbe_hw *hw, u8 byte_offset,
					 u8 dev_addr, u8 *data)
{
	return ixgbe_read_i2c_byte_generic_int(hw, byte_offset, dev_addr,
					       data, FALSE);
}

/**
 *  ixgbe_write_i2c_byte_generic_int - Writes 8 bit word over I2C
 *  @hw: pointer to hardware structure
 *  @byte_offset: byte offset to write
 *  @dev_addr: address to write to
 *  @data: value to write
 *  @lock: TRUE if to take and release semaphore
 *
 *  Performs byte write operation to SFP module's EEPROM over I2C interface at
 *  a specified device address.
 **/
static s32 ixgbe_write_i2c_byte_generic_int(struct ixgbe_hw *hw, u8 byte_offset,
					    u8 dev_addr, u8 data, bool lock)
{
	s32 status;
	u32 max_retry = 1;
	u32 retry = 0;
	u32 swfw_mask = hw->phy.phy_semaphore_mask;

	DEBUGFUNC("ixgbe_write_i2c_byte_generic");

	if (lock && hw->mac.ops.acquire_swfw_sync(hw, swfw_mask) !=
	    IXGBE_SUCCESS)
		return IXGBE_ERR_SWFW_SYNC;

	do {
		ixgbe_i2c_start(hw);

		status = ixgbe_clock_out_i2c_byte(hw, dev_addr);
		if (status != IXGBE_SUCCESS)
			goto fail;

		status = ixgbe_get_i2c_ack(hw);
		if (status != IXGBE_SUCCESS)
			goto fail;

		status = ixgbe_clock_out_i2c_byte(hw, byte_offset);
		if (status != IXGBE_SUCCESS)
			goto fail;

		status = ixgbe_get_i2c_ack(hw);
		if (status != IXGBE_SUCCESS)
			goto fail;

		status = ixgbe_clock_out_i2c_byte(hw, data);
		if (status != IXGBE_SUCCESS)
			goto fail;

		status = ixgbe_get_i2c_ack(hw);
		if (status != IXGBE_SUCCESS)
			goto fail;

		ixgbe_i2c_stop(hw);
		if (lock)
			hw->mac.ops.release_swfw_sync(hw, swfw_mask);
		return IXGBE_SUCCESS;

fail:
		ixgbe_i2c_bus_clear(hw);
		retry++;
		if (retry < max_retry)
			DEBUGOUT("I2C byte write error - Retrying.\n");
		else
			DEBUGOUT("I2C byte write error.\n");
	} while (retry < max_retry);

	if (lock)
		hw->mac.ops.release_swfw_sync(hw, swfw_mask);

	return status;
}

/**
 *  ixgbe_write_i2c_byte_generic - Writes 8 bit word over I2C
 *  @hw: pointer to hardware structure
 *  @byte_offset: byte offset to write
 *  @dev_addr: address to write to
 *  @data: value to write
 *
 *  Performs byte write operation to SFP module's EEPROM over I2C interface at
 *  a specified device address.
 **/
s32 ixgbe_write_i2c_byte_generic(struct ixgbe_hw *hw, u8 byte_offset,
				 u8 dev_addr, u8 data)
{
	return ixgbe_write_i2c_byte_generic_int(hw, byte_offset, dev_addr,
						data, TRUE);
}

/**
 *  ixgbe_write_i2c_byte_generic_unlocked - Writes 8 bit word over I2C
 *  @hw: pointer to hardware structure
 *  @byte_offset: byte offset to write
 *  @dev_addr: address to write to
 *  @data: value to write
 *
 *  Performs byte write operation to SFP module's EEPROM over I2C interface at
 *  a specified device address.
 **/
s32 ixgbe_write_i2c_byte_generic_unlocked(struct ixgbe_hw *hw, u8 byte_offset,
					  u8 dev_addr, u8 data)
{
	return ixgbe_write_i2c_byte_generic_int(hw, byte_offset, dev_addr,
						data, FALSE);
}

/**
 *  ixgbe_i2c_start - Sets I2C start condition
 *  @hw: pointer to hardware structure
 *
 *  Sets I2C start condition (High -> Low on SDA while SCL is High)
 *  Set bit-bang mode on X550 hardware.
 **/
static void ixgbe_i2c_start(struct ixgbe_hw *hw)
{
	u32 i2cctl = IXGBE_READ_REG(hw, IXGBE_I2CCTL_BY_MAC(hw));

	DEBUGFUNC("ixgbe_i2c_start");

	i2cctl |= IXGBE_I2C_BB_EN_BY_MAC(hw);

	/* Start condition must begin with data and clock high */
	ixgbe_set_i2c_data(hw, &i2cctl, 1);
	ixgbe_raise_i2c_clk(hw, &i2cctl);

	/* Setup time for start condition (4.7us) */
	usec_delay(IXGBE_I2C_T_SU_STA);

	ixgbe_set_i2c_data(hw, &i2cctl, 0);

	/* Hold time for start condition (4us) */
	usec_delay(IXGBE_I2C_T_HD_STA);

	ixgbe_lower_i2c_clk(hw, &i2cctl);

	/* Minimum low period of clock is 4.7 us */
	usec_delay(IXGBE_I2C_T_LOW);

}

/**
 *  ixgbe_i2c_stop - Sets I2C stop condition
 *  @hw: pointer to hardware structure
 *
 *  Sets I2C stop condition (Low -> High on SDA while SCL is High)
 *  Disables bit-bang mode and negates data output enable on X550
 *  hardware.
 **/
static void ixgbe_i2c_stop(struct ixgbe_hw *hw)
{
	u32 i2cctl = IXGBE_READ_REG(hw, IXGBE_I2CCTL_BY_MAC(hw));
	u32 data_oe_bit = IXGBE_I2C_DATA_OE_N_EN_BY_MAC(hw);
	u32 clk_oe_bit = IXGBE_I2C_CLK_OE_N_EN_BY_MAC(hw);
	u32 bb_en_bit = IXGBE_I2C_BB_EN_BY_MAC(hw);

	DEBUGFUNC("ixgbe_i2c_stop");

	/* Stop condition must begin with data low and clock high */
	ixgbe_set_i2c_data(hw, &i2cctl, 0);
	ixgbe_raise_i2c_clk(hw, &i2cctl);

	/* Setup time for stop condition (4us) */
	usec_delay(IXGBE_I2C_T_SU_STO);

	ixgbe_set_i2c_data(hw, &i2cctl, 1);

	/* bus free time between stop and start (4.7us)*/
	usec_delay(IXGBE_I2C_T_BUF);

	if (bb_en_bit || data_oe_bit || clk_oe_bit) {
		i2cctl &= ~bb_en_bit;
		i2cctl |= data_oe_bit | clk_oe_bit;
		IXGBE_WRITE_REG(hw, IXGBE_I2CCTL_BY_MAC(hw), i2cctl);
		IXGBE_WRITE_FLUSH(hw);
	}
}

/**
 *  ixgbe_clock_in_i2c_byte - Clocks in one byte via I2C
 *  @hw: pointer to hardware structure
 *  @data: data byte to clock in
 *
 *  Clocks in one byte data via I2C data/clock
 **/
static s32 ixgbe_clock_in_i2c_byte(struct ixgbe_hw *hw, u8 *data)
{
	s32 i;
	bool bit = 0;

	DEBUGFUNC("ixgbe_clock_in_i2c_byte");

	*data = 0;
	for (i = 7; i >= 0; i--) {
		ixgbe_clock_in_i2c_bit(hw, &bit);
		*data |= bit << i;
	}

	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_clock_out_i2c_byte - Clocks out one byte via I2C
 *  @hw: pointer to hardware structure
 *  @data: data byte clocked out
 *
 *  Clocks out one byte data via I2C data/clock
 **/
static s32 ixgbe_clock_out_i2c_byte(struct ixgbe_hw *hw, u8 data)
{
	s32 status = IXGBE_SUCCESS;
	s32 i;
	u32 i2cctl;
	bool bit;

	DEBUGFUNC("ixgbe_clock_out_i2c_byte");

	for (i = 7; i >= 0; i--) {
		bit = (data >> i) & 0x1;
		status = ixgbe_clock_out_i2c_bit(hw, bit);

		if (status != IXGBE_SUCCESS)
			break;
	}

	/* Release SDA line (set high) */
	i2cctl = IXGBE_READ_REG(hw, IXGBE_I2CCTL_BY_MAC(hw));
	i2cctl |= IXGBE_I2C_DATA_OUT_BY_MAC(hw);
	i2cctl |= IXGBE_I2C_DATA_OE_N_EN_BY_MAC(hw);
	IXGBE_WRITE_REG(hw, IXGBE_I2CCTL_BY_MAC(hw), i2cctl);
	IXGBE_WRITE_FLUSH(hw);

	return status;
}

/**
 *  ixgbe_get_i2c_ack - Polls for I2C ACK
 *  @hw: pointer to hardware structure
 *
 *  Clocks in/out one bit via I2C data/clock
 **/
static s32 ixgbe_get_i2c_ack(struct ixgbe_hw *hw)
{
	u32 data_oe_bit = IXGBE_I2C_DATA_OE_N_EN_BY_MAC(hw);
	s32 status = IXGBE_SUCCESS;
	u32 i = 0;
	u32 i2cctl = IXGBE_READ_REG(hw, IXGBE_I2CCTL_BY_MAC(hw));
	u32 timeout = 10;
	bool ack = 1;

	DEBUGFUNC("ixgbe_get_i2c_ack");

	if (data_oe_bit) {
		i2cctl |= IXGBE_I2C_DATA_OUT_BY_MAC(hw);
		i2cctl |= data_oe_bit;
		IXGBE_WRITE_REG(hw, IXGBE_I2CCTL_BY_MAC(hw), i2cctl);
		IXGBE_WRITE_FLUSH(hw);
	}
	ixgbe_raise_i2c_clk(hw, &i2cctl);

	/* Minimum high period of clock is 4us */
	usec_delay(IXGBE_I2C_T_HIGH);

	/* Poll for ACK.  Note that ACK in I2C spec is
	 * transition from 1 to 0 */
	for (i = 0; i < timeout; i++) {
		i2cctl = IXGBE_READ_REG(hw, IXGBE_I2CCTL_BY_MAC(hw));
		ack = ixgbe_get_i2c_data(hw, &i2cctl);

		usec_delay(1);
		if (!ack)
			break;
	}

	if (ack) {
		DEBUGOUT("I2C ack was not received.\n");
		status = IXGBE_ERR_I2C;
	}

	ixgbe_lower_i2c_clk(hw, &i2cctl);

	/* Minimum low period of clock is 4.7 us */
	usec_delay(IXGBE_I2C_T_LOW);

	return status;
}

/**
 *  ixgbe_clock_in_i2c_bit - Clocks in one bit via I2C data/clock
 *  @hw: pointer to hardware structure
 *  @data: read data value
 *
 *  Clocks in one bit via I2C data/clock
 **/
static s32 ixgbe_clock_in_i2c_bit(struct ixgbe_hw *hw, bool *data)
{
	u32 i2cctl = IXGBE_READ_REG(hw, IXGBE_I2CCTL_BY_MAC(hw));
	u32 data_oe_bit = IXGBE_I2C_DATA_OE_N_EN_BY_MAC(hw);

	DEBUGFUNC("ixgbe_clock_in_i2c_bit");

	if (data_oe_bit) {
		i2cctl |= IXGBE_I2C_DATA_OUT_BY_MAC(hw);
		i2cctl |= data_oe_bit;
		IXGBE_WRITE_REG(hw, IXGBE_I2CCTL_BY_MAC(hw), i2cctl);
		IXGBE_WRITE_FLUSH(hw);
	}
	ixgbe_raise_i2c_clk(hw, &i2cctl);

	/* Minimum high period of clock is 4us */
	usec_delay(IXGBE_I2C_T_HIGH);

	i2cctl = IXGBE_READ_REG(hw, IXGBE_I2CCTL_BY_MAC(hw));
	*data = ixgbe_get_i2c_data(hw, &i2cctl);

	ixgbe_lower_i2c_clk(hw, &i2cctl);

	/* Minimum low period of clock is 4.7 us */
	usec_delay(IXGBE_I2C_T_LOW);

	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_clock_out_i2c_bit - Clocks in/out one bit via I2C data/clock
 *  @hw: pointer to hardware structure
 *  @data: data value to write
 *
 *  Clocks out one bit via I2C data/clock
 **/
static s32 ixgbe_clock_out_i2c_bit(struct ixgbe_hw *hw, bool data)
{
	s32 status;
	u32 i2cctl = IXGBE_READ_REG(hw, IXGBE_I2CCTL_BY_MAC(hw));

	DEBUGFUNC("ixgbe_clock_out_i2c_bit");

	status = ixgbe_set_i2c_data(hw, &i2cctl, data);
	if (status == IXGBE_SUCCESS) {
		ixgbe_raise_i2c_clk(hw, &i2cctl);

		/* Minimum high period of clock is 4us */
		usec_delay(IXGBE_I2C_T_HIGH);

		ixgbe_lower_i2c_clk(hw, &i2cctl);

		/* Minimum low period of clock is 4.7 us.
		 * This also takes care of the data hold time.
		 */
		usec_delay(IXGBE_I2C_T_LOW);
	} else {
		status = IXGBE_ERR_I2C;
		ERROR_REPORT2(IXGBE_ERROR_INVALID_STATE,
			     "I2C data was not set to %X\n", data);
	}

	return status;
}

/**
 *  ixgbe_raise_i2c_clk - Raises the I2C SCL clock
 *  @hw: pointer to hardware structure
 *  @i2cctl: Current value of I2CCTL register
 *
 *  Raises the I2C clock line '0'->'1'
 *  Negates the I2C clock output enable on X550 hardware.
 **/
static void ixgbe_raise_i2c_clk(struct ixgbe_hw *hw, u32 *i2cctl)
{
	u32 clk_oe_bit = IXGBE_I2C_CLK_OE_N_EN_BY_MAC(hw);
	u32 i = 0;
	u32 timeout = IXGBE_I2C_CLOCK_STRETCHING_TIMEOUT;
	u32 i2cctl_r = 0;

	DEBUGFUNC("ixgbe_raise_i2c_clk");

	if (clk_oe_bit) {
		*i2cctl |= clk_oe_bit;
		IXGBE_WRITE_REG(hw, IXGBE_I2CCTL_BY_MAC(hw), *i2cctl);
	}

	for (i = 0; i < timeout; i++) {
		*i2cctl |= IXGBE_I2C_CLK_OUT_BY_MAC(hw);

		IXGBE_WRITE_REG(hw, IXGBE_I2CCTL_BY_MAC(hw), *i2cctl);
		IXGBE_WRITE_FLUSH(hw);
		/* SCL rise time (1000ns) */
		usec_delay(IXGBE_I2C_T_RISE);

		i2cctl_r = IXGBE_READ_REG(hw, IXGBE_I2CCTL_BY_MAC(hw));
		if (i2cctl_r & IXGBE_I2C_CLK_IN_BY_MAC(hw))
			break;
	}
}

/**
 *  ixgbe_lower_i2c_clk - Lowers the I2C SCL clock
 *  @hw: pointer to hardware structure
 *  @i2cctl: Current value of I2CCTL register
 *
 *  Lowers the I2C clock line '1'->'0'
 *  Asserts the I2C clock output enable on X550 hardware.
 **/
static void ixgbe_lower_i2c_clk(struct ixgbe_hw *hw, u32 *i2cctl)
{
	DEBUGFUNC("ixgbe_lower_i2c_clk");

	*i2cctl &= ~(IXGBE_I2C_CLK_OUT_BY_MAC(hw));
	*i2cctl &= ~IXGBE_I2C_CLK_OE_N_EN_BY_MAC(hw);

	IXGBE_WRITE_REG(hw, IXGBE_I2CCTL_BY_MAC(hw), *i2cctl);
	IXGBE_WRITE_FLUSH(hw);

	/* SCL fall time (300ns) */
	usec_delay(IXGBE_I2C_T_FALL);
}

/**
 *  ixgbe_set_i2c_data - Sets the I2C data bit
 *  @hw: pointer to hardware structure
 *  @i2cctl: Current value of I2CCTL register
 *  @data: I2C data value (0 or 1) to set
 *
 *  Sets the I2C data bit
 *  Asserts the I2C data output enable on X550 hardware.
 **/
static s32 ixgbe_set_i2c_data(struct ixgbe_hw *hw, u32 *i2cctl, bool data)
{
	u32 data_oe_bit = IXGBE_I2C_DATA_OE_N_EN_BY_MAC(hw);
	s32 status = IXGBE_SUCCESS;

	DEBUGFUNC("ixgbe_set_i2c_data");

	if (data)
		*i2cctl |= IXGBE_I2C_DATA_OUT_BY_MAC(hw);
	else
		*i2cctl &= ~(IXGBE_I2C_DATA_OUT_BY_MAC(hw));
	*i2cctl &= ~data_oe_bit;

	IXGBE_WRITE_REG(hw, IXGBE_I2CCTL_BY_MAC(hw), *i2cctl);
	IXGBE_WRITE_FLUSH(hw);

	/* Data rise/fall (1000ns/300ns) and set-up time (250ns) */
	usec_delay(IXGBE_I2C_T_RISE + IXGBE_I2C_T_FALL + IXGBE_I2C_T_SU_DATA);

	if (!data)	/* Can't verify data in this case */
		return IXGBE_SUCCESS;
	if (data_oe_bit) {
		*i2cctl |= data_oe_bit;
		IXGBE_WRITE_REG(hw, IXGBE_I2CCTL_BY_MAC(hw), *i2cctl);
		IXGBE_WRITE_FLUSH(hw);
	}

	/* Verify data was set correctly */
	*i2cctl = IXGBE_READ_REG(hw, IXGBE_I2CCTL_BY_MAC(hw));
	if (data != ixgbe_get_i2c_data(hw, i2cctl)) {
		status = IXGBE_ERR_I2C;
		ERROR_REPORT2(IXGBE_ERROR_INVALID_STATE,
			     "Error - I2C data was not set to %X.\n",
			     data);
	}

	return status;
}

/**
 *  ixgbe_get_i2c_data - Reads the I2C SDA data bit
 *  @hw: pointer to hardware structure
 *  @i2cctl: Current value of I2CCTL register
 *
 *  Returns the I2C data bit value
 *  Negates the I2C data output enable on X550 hardware.
 **/
static bool ixgbe_get_i2c_data(struct ixgbe_hw *hw, u32 *i2cctl)
{
	u32 data_oe_bit = IXGBE_I2C_DATA_OE_N_EN_BY_MAC(hw);
	bool data;
	UNREFERENCED_1PARAMETER(hw);

	DEBUGFUNC("ixgbe_get_i2c_data");

	if (data_oe_bit) {
		*i2cctl |= data_oe_bit;
		IXGBE_WRITE_REG(hw, IXGBE_I2CCTL_BY_MAC(hw), *i2cctl);
		IXGBE_WRITE_FLUSH(hw);
		usec_delay(IXGBE_I2C_T_FALL);
	}

	if (*i2cctl & IXGBE_I2C_DATA_IN_BY_MAC(hw))
		data = 1;
	else
		data = 0;

	return data;
}

/**
 *  ixgbe_i2c_bus_clear - Clears the I2C bus
 *  @hw: pointer to hardware structure
 *
 *  Clears the I2C bus by sending nine clock pulses.
 *  Used when data line is stuck low.
 **/
void ixgbe_i2c_bus_clear(struct ixgbe_hw *hw)
{
	u32 i2cctl;
	u32 i;

	DEBUGFUNC("ixgbe_i2c_bus_clear");

	ixgbe_i2c_start(hw);
	i2cctl = IXGBE_READ_REG(hw, IXGBE_I2CCTL_BY_MAC(hw));

	ixgbe_set_i2c_data(hw, &i2cctl, 1);

	for (i = 0; i < 9; i++) {
		ixgbe_raise_i2c_clk(hw, &i2cctl);

		/* Min high period of clock is 4us */
		usec_delay(IXGBE_I2C_T_HIGH);

		ixgbe_lower_i2c_clk(hw, &i2cctl);

		/* Min low period of clock is 4.7us*/
		usec_delay(IXGBE_I2C_T_LOW);
	}

	ixgbe_i2c_start(hw);

	/* Put the i2c bus back to default state */
	ixgbe_i2c_stop(hw);
}

/**
 *  ixgbe_tn_check_overtemp - Checks if an overtemp occurred.
 *  @hw: pointer to hardware structure
 *
 *  Checks if the LASI temp alarm status was triggered due to overtemp
 **/
s32 ixgbe_tn_check_overtemp(struct ixgbe_hw *hw)
{
	s32 status = IXGBE_SUCCESS;
	u16 phy_data = 0;

	DEBUGFUNC("ixgbe_tn_check_overtemp");

	if (hw->device_id != IXGBE_DEV_ID_82599_T3_LOM)
		goto out;

	/* Check that the LASI temp alarm status was triggered */
	hw->phy.ops.read_reg(hw, IXGBE_TN_LASI_STATUS_REG,
			     IXGBE_MDIO_PMA_PMD_DEV_TYPE, &phy_data);

	if (!(phy_data & IXGBE_TN_LASI_STATUS_TEMP_ALARM))
		goto out;

	status = IXGBE_ERR_OVERTEMP;
	ERROR_REPORT1(IXGBE_ERROR_CAUTION, "Device over temperature");
out:
	return status;
}

/**
 * ixgbe_set_copper_phy_power - Control power for copper phy
 * @hw: pointer to hardware structure
 * @on: TRUE for on, FALSE for off
 */
s32 ixgbe_set_copper_phy_power(struct ixgbe_hw *hw, bool on)
{
	u32 status;
	u16 reg;

	if (!on && ixgbe_mng_present(hw))
		return 0;

	status = hw->phy.ops.read_reg(hw, IXGBE_MDIO_VENDOR_SPECIFIC_1_CONTROL,
				      IXGBE_MDIO_VENDOR_SPECIFIC_1_DEV_TYPE,
				      &reg);
	if (status)
		return status;

	if (on) {
		reg &= ~IXGBE_MDIO_PHY_SET_LOW_POWER_MODE;
	} else {
		if (ixgbe_check_reset_blocked(hw))
			return 0;
		reg |= IXGBE_MDIO_PHY_SET_LOW_POWER_MODE;
	}

	status = hw->phy.ops.write_reg(hw, IXGBE_MDIO_VENDOR_SPECIFIC_1_CONTROL,
				       IXGBE_MDIO_VENDOR_SPECIFIC_1_DEV_TYPE,
				       reg);
	return status;
}
