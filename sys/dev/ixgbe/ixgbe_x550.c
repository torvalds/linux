/******************************************************************************

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

#include "ixgbe_x550.h"
#include "ixgbe_x540.h"
#include "ixgbe_type.h"
#include "ixgbe_api.h"
#include "ixgbe_common.h"
#include "ixgbe_phy.h"

static s32 ixgbe_setup_ixfi_x550em(struct ixgbe_hw *hw, ixgbe_link_speed *speed);
static s32 ixgbe_acquire_swfw_sync_X550a(struct ixgbe_hw *, u32 mask);
static void ixgbe_release_swfw_sync_X550a(struct ixgbe_hw *, u32 mask);
static s32 ixgbe_read_mng_if_sel_x550em(struct ixgbe_hw *hw);

/**
 *  ixgbe_init_ops_X550 - Inits func ptrs and MAC type
 *  @hw: pointer to hardware structure
 *
 *  Initialize the function pointers and assign the MAC type for X550.
 *  Does not touch the hardware.
 **/
s32 ixgbe_init_ops_X550(struct ixgbe_hw *hw)
{
	struct ixgbe_mac_info *mac = &hw->mac;
	struct ixgbe_eeprom_info *eeprom = &hw->eeprom;
	s32 ret_val;

	DEBUGFUNC("ixgbe_init_ops_X550");

	ret_val = ixgbe_init_ops_X540(hw);
	mac->ops.dmac_config = ixgbe_dmac_config_X550;
	mac->ops.dmac_config_tcs = ixgbe_dmac_config_tcs_X550;
	mac->ops.dmac_update_tcs = ixgbe_dmac_update_tcs_X550;
	mac->ops.setup_eee = NULL;
	mac->ops.set_source_address_pruning =
			ixgbe_set_source_address_pruning_X550;
	mac->ops.set_ethertype_anti_spoofing =
			ixgbe_set_ethertype_anti_spoofing_X550;

	mac->ops.get_rtrup2tc = ixgbe_dcb_get_rtrup2tc_generic;
	eeprom->ops.init_params = ixgbe_init_eeprom_params_X550;
	eeprom->ops.calc_checksum = ixgbe_calc_eeprom_checksum_X550;
	eeprom->ops.read = ixgbe_read_ee_hostif_X550;
	eeprom->ops.read_buffer = ixgbe_read_ee_hostif_buffer_X550;
	eeprom->ops.write = ixgbe_write_ee_hostif_X550;
	eeprom->ops.write_buffer = ixgbe_write_ee_hostif_buffer_X550;
	eeprom->ops.update_checksum = ixgbe_update_eeprom_checksum_X550;
	eeprom->ops.validate_checksum = ixgbe_validate_eeprom_checksum_X550;

	mac->ops.disable_mdd = ixgbe_disable_mdd_X550;
	mac->ops.enable_mdd = ixgbe_enable_mdd_X550;
	mac->ops.mdd_event = ixgbe_mdd_event_X550;
	mac->ops.restore_mdd_vf = ixgbe_restore_mdd_vf_X550;
	mac->ops.disable_rx = ixgbe_disable_rx_x550;
	/* Manageability interface */
	mac->ops.set_fw_drv_ver = ixgbe_set_fw_drv_ver_x550;
	switch (hw->device_id) {
	case IXGBE_DEV_ID_X550EM_X_1G_T:
		hw->mac.ops.led_on = NULL;
		hw->mac.ops.led_off = NULL;
		break;
	case IXGBE_DEV_ID_X550EM_X_10G_T:
	case IXGBE_DEV_ID_X550EM_A_10G_T:
		hw->mac.ops.led_on = ixgbe_led_on_t_X550em;
		hw->mac.ops.led_off = ixgbe_led_off_t_X550em;
		break;
	default:
		break;
	}
	return ret_val;
}

/**
 * ixgbe_read_cs4227 - Read CS4227 register
 * @hw: pointer to hardware structure
 * @reg: register number to write
 * @value: pointer to receive value read
 *
 * Returns status code
 **/
static s32 ixgbe_read_cs4227(struct ixgbe_hw *hw, u16 reg, u16 *value)
{
	return hw->link.ops.read_link_unlocked(hw, hw->link.addr, reg, value);
}

/**
 * ixgbe_write_cs4227 - Write CS4227 register
 * @hw: pointer to hardware structure
 * @reg: register number to write
 * @value: value to write to register
 *
 * Returns status code
 **/
static s32 ixgbe_write_cs4227(struct ixgbe_hw *hw, u16 reg, u16 value)
{
	return hw->link.ops.write_link_unlocked(hw, hw->link.addr, reg, value);
}

/**
 * ixgbe_read_pe - Read register from port expander
 * @hw: pointer to hardware structure
 * @reg: register number to read
 * @value: pointer to receive read value
 *
 * Returns status code
 **/
static s32 ixgbe_read_pe(struct ixgbe_hw *hw, u8 reg, u8 *value)
{
	s32 status;

	status = ixgbe_read_i2c_byte_unlocked(hw, reg, IXGBE_PE, value);
	if (status != IXGBE_SUCCESS)
		ERROR_REPORT2(IXGBE_ERROR_CAUTION,
			      "port expander access failed with %d\n", status);
	return status;
}

/**
 * ixgbe_write_pe - Write register to port expander
 * @hw: pointer to hardware structure
 * @reg: register number to write
 * @value: value to write
 *
 * Returns status code
 **/
static s32 ixgbe_write_pe(struct ixgbe_hw *hw, u8 reg, u8 value)
{
	s32 status;

	status = ixgbe_write_i2c_byte_unlocked(hw, reg, IXGBE_PE, value);
	if (status != IXGBE_SUCCESS)
		ERROR_REPORT2(IXGBE_ERROR_CAUTION,
			      "port expander access failed with %d\n", status);
	return status;
}

/**
 * ixgbe_reset_cs4227 - Reset CS4227 using port expander
 * @hw: pointer to hardware structure
 *
 * This function assumes that the caller has acquired the proper semaphore.
 * Returns error code
 **/
static s32 ixgbe_reset_cs4227(struct ixgbe_hw *hw)
{
	s32 status;
	u32 retry;
	u16 value;
	u8 reg;

	/* Trigger hard reset. */
	status = ixgbe_read_pe(hw, IXGBE_PE_OUTPUT, &reg);
	if (status != IXGBE_SUCCESS)
		return status;
	reg |= IXGBE_PE_BIT1;
	status = ixgbe_write_pe(hw, IXGBE_PE_OUTPUT, reg);
	if (status != IXGBE_SUCCESS)
		return status;

	status = ixgbe_read_pe(hw, IXGBE_PE_CONFIG, &reg);
	if (status != IXGBE_SUCCESS)
		return status;
	reg &= ~IXGBE_PE_BIT1;
	status = ixgbe_write_pe(hw, IXGBE_PE_CONFIG, reg);
	if (status != IXGBE_SUCCESS)
		return status;

	status = ixgbe_read_pe(hw, IXGBE_PE_OUTPUT, &reg);
	if (status != IXGBE_SUCCESS)
		return status;
	reg &= ~IXGBE_PE_BIT1;
	status = ixgbe_write_pe(hw, IXGBE_PE_OUTPUT, reg);
	if (status != IXGBE_SUCCESS)
		return status;

	usec_delay(IXGBE_CS4227_RESET_HOLD);

	status = ixgbe_read_pe(hw, IXGBE_PE_OUTPUT, &reg);
	if (status != IXGBE_SUCCESS)
		return status;
	reg |= IXGBE_PE_BIT1;
	status = ixgbe_write_pe(hw, IXGBE_PE_OUTPUT, reg);
	if (status != IXGBE_SUCCESS)
		return status;

	/* Wait for the reset to complete. */
	msec_delay(IXGBE_CS4227_RESET_DELAY);
	for (retry = 0; retry < IXGBE_CS4227_RETRIES; retry++) {
		status = ixgbe_read_cs4227(hw, IXGBE_CS4227_EFUSE_STATUS,
					   &value);
		if (status == IXGBE_SUCCESS &&
		    value == IXGBE_CS4227_EEPROM_LOAD_OK)
			break;
		msec_delay(IXGBE_CS4227_CHECK_DELAY);
	}
	if (retry == IXGBE_CS4227_RETRIES) {
		ERROR_REPORT1(IXGBE_ERROR_INVALID_STATE,
			"CS4227 reset did not complete.");
		return IXGBE_ERR_PHY;
	}

	status = ixgbe_read_cs4227(hw, IXGBE_CS4227_EEPROM_STATUS, &value);
	if (status != IXGBE_SUCCESS ||
	    !(value & IXGBE_CS4227_EEPROM_LOAD_OK)) {
		ERROR_REPORT1(IXGBE_ERROR_INVALID_STATE,
			"CS4227 EEPROM did not load successfully.");
		return IXGBE_ERR_PHY;
	}

	return IXGBE_SUCCESS;
}

/**
 * ixgbe_check_cs4227 - Check CS4227 and reset as needed
 * @hw: pointer to hardware structure
 **/
static void ixgbe_check_cs4227(struct ixgbe_hw *hw)
{
	s32 status = IXGBE_SUCCESS;
	u32 swfw_mask = hw->phy.phy_semaphore_mask;
	u16 value = 0;
	u8 retry;

	for (retry = 0; retry < IXGBE_CS4227_RETRIES; retry++) {
		status = hw->mac.ops.acquire_swfw_sync(hw, swfw_mask);
		if (status != IXGBE_SUCCESS) {
			ERROR_REPORT2(IXGBE_ERROR_CAUTION,
				"semaphore failed with %d", status);
			msec_delay(IXGBE_CS4227_CHECK_DELAY);
			continue;
		}

		/* Get status of reset flow. */
		status = ixgbe_read_cs4227(hw, IXGBE_CS4227_SCRATCH, &value);

		if (status == IXGBE_SUCCESS &&
		    value == IXGBE_CS4227_RESET_COMPLETE)
			goto out;

		if (status != IXGBE_SUCCESS ||
		    value != IXGBE_CS4227_RESET_PENDING)
			break;

		/* Reset is pending. Wait and check again. */
		hw->mac.ops.release_swfw_sync(hw, swfw_mask);
		msec_delay(IXGBE_CS4227_CHECK_DELAY);
	}

	/* If still pending, assume other instance failed. */
	if (retry == IXGBE_CS4227_RETRIES) {
		status = hw->mac.ops.acquire_swfw_sync(hw, swfw_mask);
		if (status != IXGBE_SUCCESS) {
			ERROR_REPORT2(IXGBE_ERROR_CAUTION,
				      "semaphore failed with %d", status);
			return;
		}
	}

	/* Reset the CS4227. */
	status = ixgbe_reset_cs4227(hw);
	if (status != IXGBE_SUCCESS) {
		ERROR_REPORT2(IXGBE_ERROR_INVALID_STATE,
			"CS4227 reset failed: %d", status);
		goto out;
	}

	/* Reset takes so long, temporarily release semaphore in case the
	 * other driver instance is waiting for the reset indication.
	 */
	ixgbe_write_cs4227(hw, IXGBE_CS4227_SCRATCH,
			   IXGBE_CS4227_RESET_PENDING);
	hw->mac.ops.release_swfw_sync(hw, swfw_mask);
	msec_delay(10);
	status = hw->mac.ops.acquire_swfw_sync(hw, swfw_mask);
	if (status != IXGBE_SUCCESS) {
		ERROR_REPORT2(IXGBE_ERROR_CAUTION,
			"semaphore failed with %d", status);
		return;
	}

	/* Record completion for next time. */
	status = ixgbe_write_cs4227(hw, IXGBE_CS4227_SCRATCH,
		IXGBE_CS4227_RESET_COMPLETE);

out:
	hw->mac.ops.release_swfw_sync(hw, swfw_mask);
	msec_delay(hw->eeprom.semaphore_delay);
}

/**
 * ixgbe_setup_mux_ctl - Setup ESDP register for I2C mux control
 * @hw: pointer to hardware structure
 **/
static void ixgbe_setup_mux_ctl(struct ixgbe_hw *hw)
{
	u32 esdp = IXGBE_READ_REG(hw, IXGBE_ESDP);

	if (hw->bus.lan_id) {
		esdp &= ~(IXGBE_ESDP_SDP1_NATIVE | IXGBE_ESDP_SDP1);
		esdp |= IXGBE_ESDP_SDP1_DIR;
	}
	esdp &= ~(IXGBE_ESDP_SDP0_NATIVE | IXGBE_ESDP_SDP0_DIR);
	IXGBE_WRITE_REG(hw, IXGBE_ESDP, esdp);
	IXGBE_WRITE_FLUSH(hw);
}

/**
 * ixgbe_identify_phy_x550em - Get PHY type based on device id
 * @hw: pointer to hardware structure
 *
 * Returns error code
 */
static s32 ixgbe_identify_phy_x550em(struct ixgbe_hw *hw)
{
	hw->mac.ops.set_lan_id(hw);

	ixgbe_read_mng_if_sel_x550em(hw);

	switch (hw->device_id) {
	case IXGBE_DEV_ID_X550EM_A_SFP:
		return ixgbe_identify_module_generic(hw);
	case IXGBE_DEV_ID_X550EM_X_SFP:
		/* set up for CS4227 usage */
		ixgbe_setup_mux_ctl(hw);
		ixgbe_check_cs4227(hw);
		/* Fallthrough */

	case IXGBE_DEV_ID_X550EM_A_SFP_N:
		return ixgbe_identify_module_generic(hw);
		break;
	case IXGBE_DEV_ID_X550EM_X_KX4:
		hw->phy.type = ixgbe_phy_x550em_kx4;
		break;
	case IXGBE_DEV_ID_X550EM_X_XFI:
		hw->phy.type = ixgbe_phy_x550em_xfi;
		break;
	case IXGBE_DEV_ID_X550EM_X_KR:
	case IXGBE_DEV_ID_X550EM_A_KR:
	case IXGBE_DEV_ID_X550EM_A_KR_L:
		hw->phy.type = ixgbe_phy_x550em_kr;
		break;
	case IXGBE_DEV_ID_X550EM_A_10G_T:
	case IXGBE_DEV_ID_X550EM_X_10G_T:
		return ixgbe_identify_phy_generic(hw);
	case IXGBE_DEV_ID_X550EM_X_1G_T:
		hw->phy.type = ixgbe_phy_ext_1g_t;
		break;
	case IXGBE_DEV_ID_X550EM_A_1G_T:
	case IXGBE_DEV_ID_X550EM_A_1G_T_L:
		hw->phy.type = ixgbe_phy_fw;
		if (hw->bus.lan_id)
			hw->phy.phy_semaphore_mask |= IXGBE_GSSR_PHY1_SM;
		else
			hw->phy.phy_semaphore_mask |= IXGBE_GSSR_PHY0_SM;
		break;
	default:
		break;
	}
	return IXGBE_SUCCESS;
}

/**
 * ixgbe_fw_phy_activity - Perform an activity on a PHY
 * @hw: pointer to hardware structure
 * @activity: activity to perform
 * @data: Pointer to 4 32-bit words of data
 */
s32 ixgbe_fw_phy_activity(struct ixgbe_hw *hw, u16 activity,
			  u32 (*data)[FW_PHY_ACT_DATA_COUNT])
{
	union {
		struct ixgbe_hic_phy_activity_req cmd;
		struct ixgbe_hic_phy_activity_resp rsp;
	} hic;
	u16 retries = FW_PHY_ACT_RETRIES;
	s32 rc;
	u16 i;

	do {
		memset(&hic, 0, sizeof(hic));
		hic.cmd.hdr.cmd = FW_PHY_ACT_REQ_CMD;
		hic.cmd.hdr.buf_len = FW_PHY_ACT_REQ_LEN;
		hic.cmd.hdr.checksum = FW_DEFAULT_CHECKSUM;
		hic.cmd.port_number = hw->bus.lan_id;
		hic.cmd.activity_id = IXGBE_CPU_TO_LE16(activity);
		for (i = 0; i < FW_PHY_ACT_DATA_COUNT; ++i)
			hic.cmd.data[i] = IXGBE_CPU_TO_BE32((*data)[i]);

		rc = ixgbe_host_interface_command(hw, (u32 *)&hic.cmd,
						  sizeof(hic.cmd),
						  IXGBE_HI_COMMAND_TIMEOUT,
						  TRUE);
		if (rc != IXGBE_SUCCESS)
			return rc;
		if (hic.rsp.hdr.cmd_or_resp.ret_status ==
		    FW_CEM_RESP_STATUS_SUCCESS) {
			for (i = 0; i < FW_PHY_ACT_DATA_COUNT; ++i)
				(*data)[i] = IXGBE_BE32_TO_CPU(hic.rsp.data[i]);
			return IXGBE_SUCCESS;
		}
		usec_delay(20);
		--retries;
	} while (retries > 0);

	return IXGBE_ERR_HOST_INTERFACE_COMMAND;
}

static const struct {
	u16 fw_speed;
	ixgbe_link_speed phy_speed;
} ixgbe_fw_map[] = {
	{ FW_PHY_ACT_LINK_SPEED_10, IXGBE_LINK_SPEED_10_FULL },
	{ FW_PHY_ACT_LINK_SPEED_100, IXGBE_LINK_SPEED_100_FULL },
	{ FW_PHY_ACT_LINK_SPEED_1G, IXGBE_LINK_SPEED_1GB_FULL },
	{ FW_PHY_ACT_LINK_SPEED_2_5G, IXGBE_LINK_SPEED_2_5GB_FULL },
	{ FW_PHY_ACT_LINK_SPEED_5G, IXGBE_LINK_SPEED_5GB_FULL },
	{ FW_PHY_ACT_LINK_SPEED_10G, IXGBE_LINK_SPEED_10GB_FULL },
};

/**
 * ixgbe_get_phy_id_fw - Get the phy ID via firmware command
 * @hw: pointer to hardware structure
 *
 * Returns error code
 */
static s32 ixgbe_get_phy_id_fw(struct ixgbe_hw *hw)
{
	u32 info[FW_PHY_ACT_DATA_COUNT] = { 0 };
	u16 phy_speeds;
	u16 phy_id_lo;
	s32 rc;
	u16 i;

	rc = ixgbe_fw_phy_activity(hw, FW_PHY_ACT_GET_PHY_INFO, &info);
	if (rc)
		return rc;

	hw->phy.speeds_supported = 0;
	phy_speeds = info[0] & FW_PHY_INFO_SPEED_MASK;
	for (i = 0; i < sizeof(ixgbe_fw_map) / sizeof(ixgbe_fw_map[0]); ++i) {
		if (phy_speeds & ixgbe_fw_map[i].fw_speed)
			hw->phy.speeds_supported |= ixgbe_fw_map[i].phy_speed;
	}
	if (!hw->phy.autoneg_advertised)
		hw->phy.autoneg_advertised = hw->phy.speeds_supported;

	hw->phy.id = info[0] & FW_PHY_INFO_ID_HI_MASK;
	phy_id_lo = info[1] & FW_PHY_INFO_ID_LO_MASK;
	hw->phy.id |= phy_id_lo & IXGBE_PHY_REVISION_MASK;
	hw->phy.revision = phy_id_lo & ~IXGBE_PHY_REVISION_MASK;
	if (!hw->phy.id || hw->phy.id == IXGBE_PHY_REVISION_MASK)
		return IXGBE_ERR_PHY_ADDR_INVALID;
	return IXGBE_SUCCESS;
}

/**
 * ixgbe_identify_phy_fw - Get PHY type based on firmware command
 * @hw: pointer to hardware structure
 *
 * Returns error code
 */
static s32 ixgbe_identify_phy_fw(struct ixgbe_hw *hw)
{
	if (hw->bus.lan_id)
		hw->phy.phy_semaphore_mask = IXGBE_GSSR_PHY1_SM;
	else
		hw->phy.phy_semaphore_mask = IXGBE_GSSR_PHY0_SM;

	hw->phy.type = ixgbe_phy_fw;
	hw->phy.ops.read_reg = NULL;
	hw->phy.ops.write_reg = NULL;
	return ixgbe_get_phy_id_fw(hw);
}

/**
 * ixgbe_shutdown_fw_phy - Shutdown a firmware-controlled PHY
 * @hw: pointer to hardware structure
 *
 * Returns error code
 */
s32 ixgbe_shutdown_fw_phy(struct ixgbe_hw *hw)
{
	u32 setup[FW_PHY_ACT_DATA_COUNT] = { 0 };

	setup[0] = FW_PHY_ACT_FORCE_LINK_DOWN_OFF;
	return ixgbe_fw_phy_activity(hw, FW_PHY_ACT_FORCE_LINK_DOWN, &setup);
}

static s32 ixgbe_read_phy_reg_x550em(struct ixgbe_hw *hw, u32 reg_addr,
				     u32 device_type, u16 *phy_data)
{
	UNREFERENCED_4PARAMETER(*hw, reg_addr, device_type, *phy_data);
	return IXGBE_NOT_IMPLEMENTED;
}

static s32 ixgbe_write_phy_reg_x550em(struct ixgbe_hw *hw, u32 reg_addr,
				      u32 device_type, u16 phy_data)
{
	UNREFERENCED_4PARAMETER(*hw, reg_addr, device_type, phy_data);
	return IXGBE_NOT_IMPLEMENTED;
}

/**
 * ixgbe_read_i2c_combined_generic - Perform I2C read combined operation
 * @hw: pointer to the hardware structure
 * @addr: I2C bus address to read from
 * @reg: I2C device register to read from
 * @val: pointer to location to receive read value
 *
 * Returns an error code on error.
 **/
static s32 ixgbe_read_i2c_combined_generic(struct ixgbe_hw *hw, u8 addr,
					   u16 reg, u16 *val)
{
	return ixgbe_read_i2c_combined_generic_int(hw, addr, reg, val, TRUE);
}

/**
 * ixgbe_read_i2c_combined_generic_unlocked - Do I2C read combined operation
 * @hw: pointer to the hardware structure
 * @addr: I2C bus address to read from
 * @reg: I2C device register to read from
 * @val: pointer to location to receive read value
 *
 * Returns an error code on error.
 **/
static s32
ixgbe_read_i2c_combined_generic_unlocked(struct ixgbe_hw *hw, u8 addr,
					 u16 reg, u16 *val)
{
	return ixgbe_read_i2c_combined_generic_int(hw, addr, reg, val, FALSE);
}

/**
 * ixgbe_write_i2c_combined_generic - Perform I2C write combined operation
 * @hw: pointer to the hardware structure
 * @addr: I2C bus address to write to
 * @reg: I2C device register to write to
 * @val: value to write
 *
 * Returns an error code on error.
 **/
static s32 ixgbe_write_i2c_combined_generic(struct ixgbe_hw *hw,
					    u8 addr, u16 reg, u16 val)
{
	return ixgbe_write_i2c_combined_generic_int(hw, addr, reg, val, TRUE);
}

/**
 * ixgbe_write_i2c_combined_generic_unlocked - Do I2C write combined operation
 * @hw: pointer to the hardware structure
 * @addr: I2C bus address to write to
 * @reg: I2C device register to write to
 * @val: value to write
 *
 * Returns an error code on error.
 **/
static s32
ixgbe_write_i2c_combined_generic_unlocked(struct ixgbe_hw *hw,
					  u8 addr, u16 reg, u16 val)
{
	return ixgbe_write_i2c_combined_generic_int(hw, addr, reg, val, FALSE);
}

/**
*  ixgbe_init_ops_X550EM - Inits func ptrs and MAC type
*  @hw: pointer to hardware structure
*
*  Initialize the function pointers and for MAC type X550EM.
*  Does not touch the hardware.
**/
s32 ixgbe_init_ops_X550EM(struct ixgbe_hw *hw)
{
	struct ixgbe_mac_info *mac = &hw->mac;
	struct ixgbe_eeprom_info *eeprom = &hw->eeprom;
	struct ixgbe_phy_info *phy = &hw->phy;
	s32 ret_val;

	DEBUGFUNC("ixgbe_init_ops_X550EM");

	/* Similar to X550 so start there. */
	ret_val = ixgbe_init_ops_X550(hw);

	/* Since this function eventually calls
	 * ixgbe_init_ops_540 by design, we are setting
	 * the pointers to NULL explicitly here to overwrite
	 * the values being set in the x540 function.
	 */

	/* Bypass not supported in x550EM */
	mac->ops.bypass_rw = NULL;
	mac->ops.bypass_valid_rd = NULL;
	mac->ops.bypass_set = NULL;
	mac->ops.bypass_rd_eep = NULL;

	/* FCOE not supported in x550EM */
	mac->ops.get_san_mac_addr = NULL;
	mac->ops.set_san_mac_addr = NULL;
	mac->ops.get_wwn_prefix = NULL;
	mac->ops.get_fcoe_boot_status = NULL;

	/* IPsec not supported in x550EM */
	mac->ops.disable_sec_rx_path = NULL;
	mac->ops.enable_sec_rx_path = NULL;

	/* AUTOC register is not present in x550EM. */
	mac->ops.prot_autoc_read = NULL;
	mac->ops.prot_autoc_write = NULL;

	/* X550EM bus type is internal*/
	hw->bus.type = ixgbe_bus_type_internal;
	mac->ops.get_bus_info = ixgbe_get_bus_info_X550em;


	mac->ops.get_media_type = ixgbe_get_media_type_X550em;
	mac->ops.setup_sfp = ixgbe_setup_sfp_modules_X550em;
	mac->ops.get_link_capabilities = ixgbe_get_link_capabilities_X550em;
	mac->ops.reset_hw = ixgbe_reset_hw_X550em;
	mac->ops.get_supported_physical_layer =
				    ixgbe_get_supported_physical_layer_X550em;

	if (mac->ops.get_media_type(hw) == ixgbe_media_type_copper)
		mac->ops.setup_fc = ixgbe_setup_fc_generic;
	else
		mac->ops.setup_fc = ixgbe_setup_fc_X550em;

	/* PHY */
	phy->ops.init = ixgbe_init_phy_ops_X550em;
	switch (hw->device_id) {
	case IXGBE_DEV_ID_X550EM_A_1G_T:
	case IXGBE_DEV_ID_X550EM_A_1G_T_L:
		mac->ops.setup_fc = NULL;
		phy->ops.identify = ixgbe_identify_phy_fw;
		phy->ops.set_phy_power = NULL;
		phy->ops.get_firmware_version = NULL;
		break;
	case IXGBE_DEV_ID_X550EM_X_1G_T:
		mac->ops.setup_fc = NULL;
		phy->ops.identify = ixgbe_identify_phy_x550em;
		phy->ops.set_phy_power = NULL;
		break;
	default:
		phy->ops.identify = ixgbe_identify_phy_x550em;
	}

	if (mac->ops.get_media_type(hw) != ixgbe_media_type_copper)
		phy->ops.set_phy_power = NULL;


	/* EEPROM */
	eeprom->ops.init_params = ixgbe_init_eeprom_params_X540;
	eeprom->ops.read = ixgbe_read_ee_hostif_X550;
	eeprom->ops.read_buffer = ixgbe_read_ee_hostif_buffer_X550;
	eeprom->ops.write = ixgbe_write_ee_hostif_X550;
	eeprom->ops.write_buffer = ixgbe_write_ee_hostif_buffer_X550;
	eeprom->ops.update_checksum = ixgbe_update_eeprom_checksum_X550;
	eeprom->ops.validate_checksum = ixgbe_validate_eeprom_checksum_X550;
	eeprom->ops.calc_checksum = ixgbe_calc_eeprom_checksum_X550;

	return ret_val;
}

/**
 * ixgbe_setup_fw_link - Setup firmware-controlled PHYs
 * @hw: pointer to hardware structure
 */
static s32 ixgbe_setup_fw_link(struct ixgbe_hw *hw)
{
	u32 setup[FW_PHY_ACT_DATA_COUNT] = { 0 };
	s32 rc;
	u16 i;

	if (hw->phy.reset_disable || ixgbe_check_reset_blocked(hw))
		return 0;

	if (hw->fc.strict_ieee && hw->fc.requested_mode == ixgbe_fc_rx_pause) {
		ERROR_REPORT1(IXGBE_ERROR_UNSUPPORTED,
			      "ixgbe_fc_rx_pause not valid in strict IEEE mode\n");
		return IXGBE_ERR_INVALID_LINK_SETTINGS;
	}

	switch (hw->fc.requested_mode) {
	case ixgbe_fc_full:
		setup[0] |= FW_PHY_ACT_SETUP_LINK_PAUSE_RXTX <<
			    FW_PHY_ACT_SETUP_LINK_PAUSE_SHIFT;
		break;
	case ixgbe_fc_rx_pause:
		setup[0] |= FW_PHY_ACT_SETUP_LINK_PAUSE_RX <<
			    FW_PHY_ACT_SETUP_LINK_PAUSE_SHIFT;
		break;
	case ixgbe_fc_tx_pause:
		setup[0] |= FW_PHY_ACT_SETUP_LINK_PAUSE_TX <<
			    FW_PHY_ACT_SETUP_LINK_PAUSE_SHIFT;
		break;
	default:
		break;
	}

	for (i = 0; i < sizeof(ixgbe_fw_map) / sizeof(ixgbe_fw_map[0]); ++i) {
		if (hw->phy.autoneg_advertised & ixgbe_fw_map[i].phy_speed)
			setup[0] |= ixgbe_fw_map[i].fw_speed;
	}
	setup[0] |= FW_PHY_ACT_SETUP_LINK_HP | FW_PHY_ACT_SETUP_LINK_AN;

	if (hw->phy.eee_speeds_advertised)
		setup[0] |= FW_PHY_ACT_SETUP_LINK_EEE;

	rc = ixgbe_fw_phy_activity(hw, FW_PHY_ACT_SETUP_LINK, &setup);
	if (rc)
		return rc;
	if (setup[0] == FW_PHY_ACT_SETUP_LINK_RSP_DOWN)
		return IXGBE_ERR_OVERTEMP;
	return IXGBE_SUCCESS;
}

/**
 * ixgbe_fc_autoneg_fw _ Set up flow control for FW-controlled PHYs
 * @hw: pointer to hardware structure
 *
 *  Called at init time to set up flow control.
 */
static s32 ixgbe_fc_autoneg_fw(struct ixgbe_hw *hw)
{
	if (hw->fc.requested_mode == ixgbe_fc_default)
		hw->fc.requested_mode = ixgbe_fc_full;

	return ixgbe_setup_fw_link(hw);
}

/**
 * ixgbe_setup_eee_fw - Enable/disable EEE support
 * @hw: pointer to the HW structure
 * @enable_eee: boolean flag to enable EEE
 *
 * Enable/disable EEE based on enable_eee flag.
 * This function controls EEE for firmware-based PHY implementations.
 */
static s32 ixgbe_setup_eee_fw(struct ixgbe_hw *hw, bool enable_eee)
{
	if (!!hw->phy.eee_speeds_advertised == enable_eee)
		return IXGBE_SUCCESS;
	if (enable_eee)
		hw->phy.eee_speeds_advertised = hw->phy.eee_speeds_supported;
	else
		hw->phy.eee_speeds_advertised = 0;
	return hw->phy.ops.setup_link(hw);
}

/**
*  ixgbe_init_ops_X550EM_a - Inits func ptrs and MAC type
*  @hw: pointer to hardware structure
*
*  Initialize the function pointers and for MAC type X550EM_a.
*  Does not touch the hardware.
**/
s32 ixgbe_init_ops_X550EM_a(struct ixgbe_hw *hw)
{
	struct ixgbe_mac_info *mac = &hw->mac;
	s32 ret_val;

	DEBUGFUNC("ixgbe_init_ops_X550EM_a");

	/* Start with generic X550EM init */
	ret_val = ixgbe_init_ops_X550EM(hw);

	if (hw->device_id == IXGBE_DEV_ID_X550EM_A_SGMII ||
	    hw->device_id == IXGBE_DEV_ID_X550EM_A_SGMII_L) {
		mac->ops.read_iosf_sb_reg = ixgbe_read_iosf_sb_reg_x550;
		mac->ops.write_iosf_sb_reg = ixgbe_write_iosf_sb_reg_x550;
	} else {
		mac->ops.read_iosf_sb_reg = ixgbe_read_iosf_sb_reg_x550a;
		mac->ops.write_iosf_sb_reg = ixgbe_write_iosf_sb_reg_x550a;
	}
	mac->ops.acquire_swfw_sync = ixgbe_acquire_swfw_sync_X550a;
	mac->ops.release_swfw_sync = ixgbe_release_swfw_sync_X550a;

	switch (mac->ops.get_media_type(hw)) {
	case ixgbe_media_type_fiber:
		mac->ops.setup_fc = NULL;
		mac->ops.fc_autoneg = ixgbe_fc_autoneg_fiber_x550em_a;
		break;
	case ixgbe_media_type_backplane:
		mac->ops.fc_autoneg = ixgbe_fc_autoneg_backplane_x550em_a;
		mac->ops.setup_fc = ixgbe_setup_fc_backplane_x550em_a;
		break;
	default:
		break;
	}

	switch (hw->device_id) {
	case IXGBE_DEV_ID_X550EM_A_1G_T:
	case IXGBE_DEV_ID_X550EM_A_1G_T_L:
		mac->ops.fc_autoneg = ixgbe_fc_autoneg_sgmii_x550em_a;
		mac->ops.setup_fc = ixgbe_fc_autoneg_fw;
		mac->ops.setup_eee = ixgbe_setup_eee_fw;
		hw->phy.eee_speeds_supported = IXGBE_LINK_SPEED_100_FULL |
					       IXGBE_LINK_SPEED_1GB_FULL;
		hw->phy.eee_speeds_advertised = hw->phy.eee_speeds_supported;
		break;
	default:
		break;
	}

	return ret_val;
}

/**
*  ixgbe_init_ops_X550EM_x - Inits func ptrs and MAC type
*  @hw: pointer to hardware structure
*
*  Initialize the function pointers and for MAC type X550EM_x.
*  Does not touch the hardware.
**/
s32 ixgbe_init_ops_X550EM_x(struct ixgbe_hw *hw)
{
	struct ixgbe_mac_info *mac = &hw->mac;
	struct ixgbe_link_info *link = &hw->link;
	s32 ret_val;

	DEBUGFUNC("ixgbe_init_ops_X550EM_x");

	/* Start with generic X550EM init */
	ret_val = ixgbe_init_ops_X550EM(hw);

	mac->ops.read_iosf_sb_reg = ixgbe_read_iosf_sb_reg_x550;
	mac->ops.write_iosf_sb_reg = ixgbe_write_iosf_sb_reg_x550;
	mac->ops.acquire_swfw_sync = ixgbe_acquire_swfw_sync_X550em;
	mac->ops.release_swfw_sync = ixgbe_release_swfw_sync_X550em;
	link->ops.read_link = ixgbe_read_i2c_combined_generic;
	link->ops.read_link_unlocked = ixgbe_read_i2c_combined_generic_unlocked;
	link->ops.write_link = ixgbe_write_i2c_combined_generic;
	link->ops.write_link_unlocked =
				      ixgbe_write_i2c_combined_generic_unlocked;
	link->addr = IXGBE_CS4227;

	if (hw->device_id == IXGBE_DEV_ID_X550EM_X_1G_T) {
		mac->ops.setup_fc = NULL;
		mac->ops.setup_eee = NULL;
		mac->ops.init_led_link_act = NULL;
	}

	return ret_val;
}

/**
 *  ixgbe_dmac_config_X550
 *  @hw: pointer to hardware structure
 *
 *  Configure DMA coalescing. If enabling dmac, dmac is activated.
 *  When disabling dmac, dmac enable dmac bit is cleared.
 **/
s32 ixgbe_dmac_config_X550(struct ixgbe_hw *hw)
{
	u32 reg, high_pri_tc;

	DEBUGFUNC("ixgbe_dmac_config_X550");

	/* Disable DMA coalescing before configuring */
	reg = IXGBE_READ_REG(hw, IXGBE_DMACR);
	reg &= ~IXGBE_DMACR_DMAC_EN;
	IXGBE_WRITE_REG(hw, IXGBE_DMACR, reg);

	/* Disable DMA Coalescing if the watchdog timer is 0 */
	if (!hw->mac.dmac_config.watchdog_timer)
		goto out;

	ixgbe_dmac_config_tcs_X550(hw);

	/* Configure DMA Coalescing Control Register */
	reg = IXGBE_READ_REG(hw, IXGBE_DMACR);

	/* Set the watchdog timer in units of 40.96 usec */
	reg &= ~IXGBE_DMACR_DMACWT_MASK;
	reg |= (hw->mac.dmac_config.watchdog_timer * 100) / 4096;

	reg &= ~IXGBE_DMACR_HIGH_PRI_TC_MASK;
	/* If fcoe is enabled, set high priority traffic class */
	if (hw->mac.dmac_config.fcoe_en) {
		high_pri_tc = 1 << hw->mac.dmac_config.fcoe_tc;
		reg |= ((high_pri_tc << IXGBE_DMACR_HIGH_PRI_TC_SHIFT) &
			IXGBE_DMACR_HIGH_PRI_TC_MASK);
	}
	reg |= IXGBE_DMACR_EN_MNG_IND;

	/* Enable DMA coalescing after configuration */
	reg |= IXGBE_DMACR_DMAC_EN;
	IXGBE_WRITE_REG(hw, IXGBE_DMACR, reg);

out:
	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_dmac_config_tcs_X550
 *  @hw: pointer to hardware structure
 *
 *  Configure DMA coalescing threshold per TC. The dmac enable bit must
 *  be cleared before configuring.
 **/
s32 ixgbe_dmac_config_tcs_X550(struct ixgbe_hw *hw)
{
	u32 tc, reg, pb_headroom, rx_pb_size, maxframe_size_kb;

	DEBUGFUNC("ixgbe_dmac_config_tcs_X550");

	/* Configure DMA coalescing enabled */
	switch (hw->mac.dmac_config.link_speed) {
	case IXGBE_LINK_SPEED_10_FULL:
	case IXGBE_LINK_SPEED_100_FULL:
		pb_headroom = IXGBE_DMACRXT_100M;
		break;
	case IXGBE_LINK_SPEED_1GB_FULL:
		pb_headroom = IXGBE_DMACRXT_1G;
		break;
	default:
		pb_headroom = IXGBE_DMACRXT_10G;
		break;
	}

	maxframe_size_kb = ((IXGBE_READ_REG(hw, IXGBE_MAXFRS) >>
			     IXGBE_MHADD_MFS_SHIFT) / 1024);

	/* Set the per Rx packet buffer receive threshold */
	for (tc = 0; tc < IXGBE_DCB_MAX_TRAFFIC_CLASS; tc++) {
		reg = IXGBE_READ_REG(hw, IXGBE_DMCTH(tc));
		reg &= ~IXGBE_DMCTH_DMACRXT_MASK;

		if (tc < hw->mac.dmac_config.num_tcs) {
			/* Get Rx PB size */
			rx_pb_size = IXGBE_READ_REG(hw, IXGBE_RXPBSIZE(tc));
			rx_pb_size = (rx_pb_size & IXGBE_RXPBSIZE_MASK) >>
				IXGBE_RXPBSIZE_SHIFT;

			/* Calculate receive buffer threshold in kilobytes */
			if (rx_pb_size > pb_headroom)
				rx_pb_size = rx_pb_size - pb_headroom;
			else
				rx_pb_size = 0;

			/* Minimum of MFS shall be set for DMCTH */
			reg |= (rx_pb_size > maxframe_size_kb) ?
				rx_pb_size : maxframe_size_kb;
		}
		IXGBE_WRITE_REG(hw, IXGBE_DMCTH(tc), reg);
	}
	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_dmac_update_tcs_X550
 *  @hw: pointer to hardware structure
 *
 *  Disables dmac, updates per TC settings, and then enables dmac.
 **/
s32 ixgbe_dmac_update_tcs_X550(struct ixgbe_hw *hw)
{
	u32 reg;

	DEBUGFUNC("ixgbe_dmac_update_tcs_X550");

	/* Disable DMA coalescing before configuring */
	reg = IXGBE_READ_REG(hw, IXGBE_DMACR);
	reg &= ~IXGBE_DMACR_DMAC_EN;
	IXGBE_WRITE_REG(hw, IXGBE_DMACR, reg);

	ixgbe_dmac_config_tcs_X550(hw);

	/* Enable DMA coalescing after configuration */
	reg = IXGBE_READ_REG(hw, IXGBE_DMACR);
	reg |= IXGBE_DMACR_DMAC_EN;
	IXGBE_WRITE_REG(hw, IXGBE_DMACR, reg);

	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_init_eeprom_params_X550 - Initialize EEPROM params
 *  @hw: pointer to hardware structure
 *
 *  Initializes the EEPROM parameters ixgbe_eeprom_info within the
 *  ixgbe_hw struct in order to set up EEPROM access.
 **/
s32 ixgbe_init_eeprom_params_X550(struct ixgbe_hw *hw)
{
	struct ixgbe_eeprom_info *eeprom = &hw->eeprom;
	u32 eec;
	u16 eeprom_size;

	DEBUGFUNC("ixgbe_init_eeprom_params_X550");

	if (eeprom->type == ixgbe_eeprom_uninitialized) {
		eeprom->semaphore_delay = 10;
		eeprom->type = ixgbe_flash;

		eec = IXGBE_READ_REG(hw, IXGBE_EEC);
		eeprom_size = (u16)((eec & IXGBE_EEC_SIZE) >>
				    IXGBE_EEC_SIZE_SHIFT);
		eeprom->word_size = 1 << (eeprom_size +
					  IXGBE_EEPROM_WORD_SIZE_SHIFT);

		DEBUGOUT2("Eeprom params: type = %d, size = %d\n",
			  eeprom->type, eeprom->word_size);
	}

	return IXGBE_SUCCESS;
}

/**
 * ixgbe_set_source_address_pruning_X550 - Enable/Disbale source address pruning
 * @hw: pointer to hardware structure
 * @enable: enable or disable source address pruning
 * @pool: Rx pool to set source address pruning for
 **/
void ixgbe_set_source_address_pruning_X550(struct ixgbe_hw *hw, bool enable,
					   unsigned int pool)
{
	u64 pfflp;

	/* max rx pool is 63 */
	if (pool > 63)
		return;

	pfflp = (u64)IXGBE_READ_REG(hw, IXGBE_PFFLPL);
	pfflp |= (u64)IXGBE_READ_REG(hw, IXGBE_PFFLPH) << 32;

	if (enable)
		pfflp |= (1ULL << pool);
	else
		pfflp &= ~(1ULL << pool);

	IXGBE_WRITE_REG(hw, IXGBE_PFFLPL, (u32)pfflp);
	IXGBE_WRITE_REG(hw, IXGBE_PFFLPH, (u32)(pfflp >> 32));
}

/**
 *  ixgbe_set_ethertype_anti_spoofing_X550 - Enable/Disable Ethertype anti-spoofing
 *  @hw: pointer to hardware structure
 *  @enable: enable or disable switch for Ethertype anti-spoofing
 *  @vf: Virtual Function pool - VF Pool to set for Ethertype anti-spoofing
 *
 **/
void ixgbe_set_ethertype_anti_spoofing_X550(struct ixgbe_hw *hw,
		bool enable, int vf)
{
	int vf_target_reg = vf >> 3;
	int vf_target_shift = vf % 8 + IXGBE_SPOOF_ETHERTYPEAS_SHIFT;
	u32 pfvfspoof;

	DEBUGFUNC("ixgbe_set_ethertype_anti_spoofing_X550");

	pfvfspoof = IXGBE_READ_REG(hw, IXGBE_PFVFSPOOF(vf_target_reg));
	if (enable)
		pfvfspoof |= (1 << vf_target_shift);
	else
		pfvfspoof &= ~(1 << vf_target_shift);

	IXGBE_WRITE_REG(hw, IXGBE_PFVFSPOOF(vf_target_reg), pfvfspoof);
}

/**
 * ixgbe_iosf_wait - Wait for IOSF command completion
 * @hw: pointer to hardware structure
 * @ctrl: pointer to location to receive final IOSF control value
 *
 * Returns failing status on timeout
 *
 * Note: ctrl can be NULL if the IOSF control register value is not needed
 **/
static s32 ixgbe_iosf_wait(struct ixgbe_hw *hw, u32 *ctrl)
{
	u32 i, command = 0;

	/* Check every 10 usec to see if the address cycle completed.
	 * The SB IOSF BUSY bit will clear when the operation is
	 * complete
	 */
	for (i = 0; i < IXGBE_MDIO_COMMAND_TIMEOUT; i++) {
		command = IXGBE_READ_REG(hw, IXGBE_SB_IOSF_INDIRECT_CTRL);
		if ((command & IXGBE_SB_IOSF_CTRL_BUSY) == 0)
			break;
		usec_delay(10);
	}
	if (ctrl)
		*ctrl = command;
	if (i == IXGBE_MDIO_COMMAND_TIMEOUT) {
		ERROR_REPORT1(IXGBE_ERROR_POLLING, "Wait timed out\n");
		return IXGBE_ERR_PHY;
	}

	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_write_iosf_sb_reg_x550 - Writes a value to specified register
 *  of the IOSF device
 *  @hw: pointer to hardware structure
 *  @reg_addr: 32 bit PHY register to write
 *  @device_type: 3 bit device type
 *  @data: Data to write to the register
 **/
s32 ixgbe_write_iosf_sb_reg_x550(struct ixgbe_hw *hw, u32 reg_addr,
			    u32 device_type, u32 data)
{
	u32 gssr = IXGBE_GSSR_PHY1_SM | IXGBE_GSSR_PHY0_SM;
	u32 command, error __unused;
	s32 ret;

	ret = ixgbe_acquire_swfw_semaphore(hw, gssr);
	if (ret != IXGBE_SUCCESS)
		return ret;

	ret = ixgbe_iosf_wait(hw, NULL);
	if (ret != IXGBE_SUCCESS)
		goto out;

	command = ((reg_addr << IXGBE_SB_IOSF_CTRL_ADDR_SHIFT) |
		   (device_type << IXGBE_SB_IOSF_CTRL_TARGET_SELECT_SHIFT));

	/* Write IOSF control register */
	IXGBE_WRITE_REG(hw, IXGBE_SB_IOSF_INDIRECT_CTRL, command);

	/* Write IOSF data register */
	IXGBE_WRITE_REG(hw, IXGBE_SB_IOSF_INDIRECT_DATA, data);

	ret = ixgbe_iosf_wait(hw, &command);

	if ((command & IXGBE_SB_IOSF_CTRL_RESP_STAT_MASK) != 0) {
		error = (command & IXGBE_SB_IOSF_CTRL_CMPL_ERR_MASK) >>
			 IXGBE_SB_IOSF_CTRL_CMPL_ERR_SHIFT;
		ERROR_REPORT2(IXGBE_ERROR_POLLING,
			      "Failed to write, error %x\n", error);
		ret = IXGBE_ERR_PHY;
	}

out:
	ixgbe_release_swfw_semaphore(hw, gssr);
	return ret;
}

/**
 *  ixgbe_read_iosf_sb_reg_x550 - Reads specified register of the IOSF device
 *  @hw: pointer to hardware structure
 *  @reg_addr: 32 bit PHY register to write
 *  @device_type: 3 bit device type
 *  @data: Pointer to read data from the register
 **/
s32 ixgbe_read_iosf_sb_reg_x550(struct ixgbe_hw *hw, u32 reg_addr,
			   u32 device_type, u32 *data)
{
	u32 gssr = IXGBE_GSSR_PHY1_SM | IXGBE_GSSR_PHY0_SM;
	u32 command, error __unused;
	s32 ret;

	ret = ixgbe_acquire_swfw_semaphore(hw, gssr);
	if (ret != IXGBE_SUCCESS)
		return ret;

	ret = ixgbe_iosf_wait(hw, NULL);
	if (ret != IXGBE_SUCCESS)
		goto out;

	command = ((reg_addr << IXGBE_SB_IOSF_CTRL_ADDR_SHIFT) |
		   (device_type << IXGBE_SB_IOSF_CTRL_TARGET_SELECT_SHIFT));

	/* Write IOSF control register */
	IXGBE_WRITE_REG(hw, IXGBE_SB_IOSF_INDIRECT_CTRL, command);

	ret = ixgbe_iosf_wait(hw, &command);

	if ((command & IXGBE_SB_IOSF_CTRL_RESP_STAT_MASK) != 0) {
		error = (command & IXGBE_SB_IOSF_CTRL_CMPL_ERR_MASK) >>
			 IXGBE_SB_IOSF_CTRL_CMPL_ERR_SHIFT;
		ERROR_REPORT2(IXGBE_ERROR_POLLING,
				"Failed to read, error %x\n", error);
		ret = IXGBE_ERR_PHY;
	}

	if (ret == IXGBE_SUCCESS)
		*data = IXGBE_READ_REG(hw, IXGBE_SB_IOSF_INDIRECT_DATA);

out:
	ixgbe_release_swfw_semaphore(hw, gssr);
	return ret;
}

/**
 * ixgbe_get_phy_token - Get the token for shared phy access
 * @hw: Pointer to hardware structure
 */

s32 ixgbe_get_phy_token(struct ixgbe_hw *hw)
{
	struct ixgbe_hic_phy_token_req token_cmd;
	s32 status;

	token_cmd.hdr.cmd = FW_PHY_TOKEN_REQ_CMD;
	token_cmd.hdr.buf_len = FW_PHY_TOKEN_REQ_LEN;
	token_cmd.hdr.cmd_or_resp.cmd_resv = 0;
	token_cmd.hdr.checksum = FW_DEFAULT_CHECKSUM;
	token_cmd.port_number = hw->bus.lan_id;
	token_cmd.command_type = FW_PHY_TOKEN_REQ;
	token_cmd.pad = 0;
	status = ixgbe_host_interface_command(hw, (u32 *)&token_cmd,
					      sizeof(token_cmd),
					      IXGBE_HI_COMMAND_TIMEOUT,
					      TRUE);
	if (status) {
		DEBUGOUT1("Issuing host interface command failed with Status = %d\n",
			  status);
		return status;
	}
	if (token_cmd.hdr.cmd_or_resp.ret_status == FW_PHY_TOKEN_OK)
		return IXGBE_SUCCESS;
	if (token_cmd.hdr.cmd_or_resp.ret_status != FW_PHY_TOKEN_RETRY) {
		DEBUGOUT1("Host interface command returned 0x%08x , returning IXGBE_ERR_FW_RESP_INVALID\n",
			  token_cmd.hdr.cmd_or_resp.ret_status);
		return IXGBE_ERR_FW_RESP_INVALID;
	}

	DEBUGOUT("Returning  IXGBE_ERR_TOKEN_RETRY\n");
	return IXGBE_ERR_TOKEN_RETRY;
}

/**
 * ixgbe_put_phy_token - Put the token for shared phy access
 * @hw: Pointer to hardware structure
 */

s32 ixgbe_put_phy_token(struct ixgbe_hw *hw)
{
	struct ixgbe_hic_phy_token_req token_cmd;
	s32 status;

	token_cmd.hdr.cmd = FW_PHY_TOKEN_REQ_CMD;
	token_cmd.hdr.buf_len = FW_PHY_TOKEN_REQ_LEN;
	token_cmd.hdr.cmd_or_resp.cmd_resv = 0;
	token_cmd.hdr.checksum = FW_DEFAULT_CHECKSUM;
	token_cmd.port_number = hw->bus.lan_id;
	token_cmd.command_type = FW_PHY_TOKEN_REL;
	token_cmd.pad = 0;
	status = ixgbe_host_interface_command(hw, (u32 *)&token_cmd,
					      sizeof(token_cmd),
					      IXGBE_HI_COMMAND_TIMEOUT,
					      TRUE);
	if (status)
		return status;
	if (token_cmd.hdr.cmd_or_resp.ret_status == FW_PHY_TOKEN_OK)
		return IXGBE_SUCCESS;

	DEBUGOUT("Put PHY Token host interface command failed");
	return IXGBE_ERR_FW_RESP_INVALID;
}

/**
 *  ixgbe_write_iosf_sb_reg_x550a - Writes a value to specified register
 *  of the IOSF device
 *  @hw: pointer to hardware structure
 *  @reg_addr: 32 bit PHY register to write
 *  @device_type: 3 bit device type
 *  @data: Data to write to the register
 **/
s32 ixgbe_write_iosf_sb_reg_x550a(struct ixgbe_hw *hw, u32 reg_addr,
				  u32 device_type, u32 data)
{
	struct ixgbe_hic_internal_phy_req write_cmd;
	s32 status;
	UNREFERENCED_1PARAMETER(device_type);

	memset(&write_cmd, 0, sizeof(write_cmd));
	write_cmd.hdr.cmd = FW_INT_PHY_REQ_CMD;
	write_cmd.hdr.buf_len = FW_INT_PHY_REQ_LEN;
	write_cmd.hdr.checksum = FW_DEFAULT_CHECKSUM;
	write_cmd.port_number = hw->bus.lan_id;
	write_cmd.command_type = FW_INT_PHY_REQ_WRITE;
	write_cmd.address = IXGBE_CPU_TO_BE16(reg_addr);
	write_cmd.write_data = IXGBE_CPU_TO_BE32(data);

	status = ixgbe_host_interface_command(hw, (u32 *)&write_cmd,
					      sizeof(write_cmd),
					      IXGBE_HI_COMMAND_TIMEOUT, FALSE);

	return status;
}

/**
 *  ixgbe_read_iosf_sb_reg_x550a - Reads specified register of the IOSF device
 *  @hw: pointer to hardware structure
 *  @reg_addr: 32 bit PHY register to write
 *  @device_type: 3 bit device type
 *  @data: Pointer to read data from the register
 **/
s32 ixgbe_read_iosf_sb_reg_x550a(struct ixgbe_hw *hw, u32 reg_addr,
				 u32 device_type, u32 *data)
{
	union {
		struct ixgbe_hic_internal_phy_req cmd;
		struct ixgbe_hic_internal_phy_resp rsp;
	} hic;
	s32 status;
	UNREFERENCED_1PARAMETER(device_type);

	memset(&hic, 0, sizeof(hic));
	hic.cmd.hdr.cmd = FW_INT_PHY_REQ_CMD;
	hic.cmd.hdr.buf_len = FW_INT_PHY_REQ_LEN;
	hic.cmd.hdr.checksum = FW_DEFAULT_CHECKSUM;
	hic.cmd.port_number = hw->bus.lan_id;
	hic.cmd.command_type = FW_INT_PHY_REQ_READ;
	hic.cmd.address = IXGBE_CPU_TO_BE16(reg_addr);

	status = ixgbe_host_interface_command(hw, (u32 *)&hic.cmd,
					      sizeof(hic.cmd),
					      IXGBE_HI_COMMAND_TIMEOUT, TRUE);

	/* Extract the register value from the response. */
	*data = IXGBE_BE32_TO_CPU(hic.rsp.read_data);

	return status;
}

/**
 *  ixgbe_disable_mdd_X550
 *  @hw: pointer to hardware structure
 *
 *  Disable malicious driver detection
 **/
void ixgbe_disable_mdd_X550(struct ixgbe_hw *hw)
{
	u32 reg;

	DEBUGFUNC("ixgbe_disable_mdd_X550");

	/* Disable MDD for TX DMA and interrupt */
	reg = IXGBE_READ_REG(hw, IXGBE_DMATXCTL);
	reg &= ~(IXGBE_DMATXCTL_MDP_EN | IXGBE_DMATXCTL_MBINTEN);
	IXGBE_WRITE_REG(hw, IXGBE_DMATXCTL, reg);

	/* Disable MDD for RX and interrupt */
	reg = IXGBE_READ_REG(hw, IXGBE_RDRXCTL);
	reg &= ~(IXGBE_RDRXCTL_MDP_EN | IXGBE_RDRXCTL_MBINTEN);
	IXGBE_WRITE_REG(hw, IXGBE_RDRXCTL, reg);
}

/**
 *  ixgbe_enable_mdd_X550
 *  @hw: pointer to hardware structure
 *
 *  Enable malicious driver detection
 **/
void ixgbe_enable_mdd_X550(struct ixgbe_hw *hw)
{
	u32 reg;

	DEBUGFUNC("ixgbe_enable_mdd_X550");

	/* Enable MDD for TX DMA and interrupt */
	reg = IXGBE_READ_REG(hw, IXGBE_DMATXCTL);
	reg |= (IXGBE_DMATXCTL_MDP_EN | IXGBE_DMATXCTL_MBINTEN);
	IXGBE_WRITE_REG(hw, IXGBE_DMATXCTL, reg);

	/* Enable MDD for RX and interrupt */
	reg = IXGBE_READ_REG(hw, IXGBE_RDRXCTL);
	reg |= (IXGBE_RDRXCTL_MDP_EN | IXGBE_RDRXCTL_MBINTEN);
	IXGBE_WRITE_REG(hw, IXGBE_RDRXCTL, reg);
}

/**
 *  ixgbe_restore_mdd_vf_X550
 *  @hw: pointer to hardware structure
 *  @vf: vf index
 *
 *  Restore VF that was disabled during malicious driver detection event
 **/
void ixgbe_restore_mdd_vf_X550(struct ixgbe_hw *hw, u32 vf)
{
	u32 idx, reg, num_qs, start_q, bitmask;

	DEBUGFUNC("ixgbe_restore_mdd_vf_X550");

	/* Map VF to queues */
	reg = IXGBE_READ_REG(hw, IXGBE_MRQC);
	switch (reg & IXGBE_MRQC_MRQE_MASK) {
	case IXGBE_MRQC_VMDQRT8TCEN:
		num_qs = 8;  /* 16 VFs / pools */
		bitmask = 0x000000FF;
		break;
	case IXGBE_MRQC_VMDQRSS32EN:
	case IXGBE_MRQC_VMDQRT4TCEN:
		num_qs = 4;  /* 32 VFs / pools */
		bitmask = 0x0000000F;
		break;
	default:            /* 64 VFs / pools */
		num_qs = 2;
		bitmask = 0x00000003;
		break;
	}
	start_q = vf * num_qs;

	/* Release vf's queues by clearing WQBR_TX and WQBR_RX (RW1C) */
	idx = start_q / 32;
	reg = 0;
	reg |= (bitmask << (start_q % 32));
	IXGBE_WRITE_REG(hw, IXGBE_WQBR_TX(idx), reg);
	IXGBE_WRITE_REG(hw, IXGBE_WQBR_RX(idx), reg);
}

/**
 *  ixgbe_mdd_event_X550
 *  @hw: pointer to hardware structure
 *  @vf_bitmap: vf bitmap of malicious vfs
 *
 *  Handle malicious driver detection event.
 **/
void ixgbe_mdd_event_X550(struct ixgbe_hw *hw, u32 *vf_bitmap)
{
	u32 wqbr;
	u32 i, j, reg, q, shift, vf, idx;

	DEBUGFUNC("ixgbe_mdd_event_X550");

	/* figure out pool size for mapping to vf's */
	reg = IXGBE_READ_REG(hw, IXGBE_MRQC);
	switch (reg & IXGBE_MRQC_MRQE_MASK) {
	case IXGBE_MRQC_VMDQRT8TCEN:
		shift = 3;  /* 16 VFs / pools */
		break;
	case IXGBE_MRQC_VMDQRSS32EN:
	case IXGBE_MRQC_VMDQRT4TCEN:
		shift = 2;  /* 32 VFs / pools */
		break;
	default:
		shift = 1;  /* 64 VFs / pools */
		break;
	}

	/* Read WQBR_TX and WQBR_RX and check for malicious queues */
	for (i = 0; i < 4; i++) {
		wqbr = IXGBE_READ_REG(hw, IXGBE_WQBR_TX(i));
		wqbr |= IXGBE_READ_REG(hw, IXGBE_WQBR_RX(i));

		if (!wqbr)
			continue;

		/* Get malicious queue */
		for (j = 0; j < 32 && wqbr; j++) {

			if (!(wqbr & (1 << j)))
				continue;

			/* Get queue from bitmask */
			q = j + (i * 32);

			/* Map queue to vf */
			vf = (q >> shift);

			/* Set vf bit in vf_bitmap */
			idx = vf / 32;
			vf_bitmap[idx] |= (1 << (vf % 32));
			wqbr &= ~(1 << j);
		}
	}
}

/**
 *  ixgbe_get_media_type_X550em - Get media type
 *  @hw: pointer to hardware structure
 *
 *  Returns the media type (fiber, copper, backplane)
 */
enum ixgbe_media_type ixgbe_get_media_type_X550em(struct ixgbe_hw *hw)
{
	enum ixgbe_media_type media_type;

	DEBUGFUNC("ixgbe_get_media_type_X550em");

	/* Detect if there is a copper PHY attached. */
	switch (hw->device_id) {
	case IXGBE_DEV_ID_X550EM_X_KR:
	case IXGBE_DEV_ID_X550EM_X_KX4:
	case IXGBE_DEV_ID_X550EM_X_XFI:
	case IXGBE_DEV_ID_X550EM_A_KR:
	case IXGBE_DEV_ID_X550EM_A_KR_L:
		media_type = ixgbe_media_type_backplane;
		break;
	case IXGBE_DEV_ID_X550EM_X_SFP:
	case IXGBE_DEV_ID_X550EM_A_SFP:
	case IXGBE_DEV_ID_X550EM_A_SFP_N:
	case IXGBE_DEV_ID_X550EM_A_QSFP:
	case IXGBE_DEV_ID_X550EM_A_QSFP_N:
		media_type = ixgbe_media_type_fiber;
		break;
	case IXGBE_DEV_ID_X550EM_X_1G_T:
	case IXGBE_DEV_ID_X550EM_X_10G_T:
	case IXGBE_DEV_ID_X550EM_A_10G_T:
		media_type = ixgbe_media_type_copper;
		break;
	case IXGBE_DEV_ID_X550EM_A_SGMII:
	case IXGBE_DEV_ID_X550EM_A_SGMII_L:
		media_type = ixgbe_media_type_backplane;
		hw->phy.type = ixgbe_phy_sgmii;
		break;
	case IXGBE_DEV_ID_X550EM_A_1G_T:
	case IXGBE_DEV_ID_X550EM_A_1G_T_L:
		media_type = ixgbe_media_type_copper;
		break;
	default:
		media_type = ixgbe_media_type_unknown;
		break;
	}
	return media_type;
}

/**
 *  ixgbe_supported_sfp_modules_X550em - Check if SFP module type is supported
 *  @hw: pointer to hardware structure
 *  @linear: TRUE if SFP module is linear
 */
static s32 ixgbe_supported_sfp_modules_X550em(struct ixgbe_hw *hw, bool *linear)
{
	DEBUGFUNC("ixgbe_supported_sfp_modules_X550em");

	switch (hw->phy.sfp_type) {
	case ixgbe_sfp_type_not_present:
		return IXGBE_ERR_SFP_NOT_PRESENT;
	case ixgbe_sfp_type_da_cu_core0:
	case ixgbe_sfp_type_da_cu_core1:
		*linear = TRUE;
		break;
	case ixgbe_sfp_type_srlr_core0:
	case ixgbe_sfp_type_srlr_core1:
	case ixgbe_sfp_type_da_act_lmt_core0:
	case ixgbe_sfp_type_da_act_lmt_core1:
	case ixgbe_sfp_type_1g_sx_core0:
	case ixgbe_sfp_type_1g_sx_core1:
	case ixgbe_sfp_type_1g_lx_core0:
	case ixgbe_sfp_type_1g_lx_core1:
		*linear = FALSE;
		break;
	case ixgbe_sfp_type_unknown:
	case ixgbe_sfp_type_1g_cu_core0:
	case ixgbe_sfp_type_1g_cu_core1:
	default:
		return IXGBE_ERR_SFP_NOT_SUPPORTED;
	}

	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_identify_sfp_module_X550em - Identifies SFP modules
 *  @hw: pointer to hardware structure
 *
 *  Searches for and identifies the SFP module and assigns appropriate PHY type.
 **/
s32 ixgbe_identify_sfp_module_X550em(struct ixgbe_hw *hw)
{
	s32 status;
	bool linear;

	DEBUGFUNC("ixgbe_identify_sfp_module_X550em");

	status = ixgbe_identify_module_generic(hw);

	if (status != IXGBE_SUCCESS)
		return status;

	/* Check if SFP module is supported */
	status = ixgbe_supported_sfp_modules_X550em(hw, &linear);

	return status;
}

/**
 *  ixgbe_setup_sfp_modules_X550em - Setup MAC link ops
 *  @hw: pointer to hardware structure
 */
s32 ixgbe_setup_sfp_modules_X550em(struct ixgbe_hw *hw)
{
	s32 status;
	bool linear;

	DEBUGFUNC("ixgbe_setup_sfp_modules_X550em");

	/* Check if SFP module is supported */
	status = ixgbe_supported_sfp_modules_X550em(hw, &linear);

	if (status != IXGBE_SUCCESS)
		return status;

	ixgbe_init_mac_link_ops_X550em(hw);
	hw->phy.ops.reset = NULL;

	return IXGBE_SUCCESS;
}

/**
*  ixgbe_restart_an_internal_phy_x550em - restart autonegotiation for the
*  internal PHY
*  @hw: pointer to hardware structure
**/
static s32 ixgbe_restart_an_internal_phy_x550em(struct ixgbe_hw *hw)
{
	s32 status;
	u32 link_ctrl;

	/* Restart auto-negotiation. */
	status = hw->mac.ops.read_iosf_sb_reg(hw,
				       IXGBE_KRM_LINK_CTRL_1(hw->bus.lan_id),
				       IXGBE_SB_IOSF_TARGET_KR_PHY, &link_ctrl);

	if (status) {
		DEBUGOUT("Auto-negotiation did not complete\n");
		return status;
	}

	link_ctrl |= IXGBE_KRM_LINK_CTRL_1_TETH_AN_RESTART;
	status = hw->mac.ops.write_iosf_sb_reg(hw,
					IXGBE_KRM_LINK_CTRL_1(hw->bus.lan_id),
					IXGBE_SB_IOSF_TARGET_KR_PHY, link_ctrl);

	if (hw->mac.type == ixgbe_mac_X550EM_a) {
		u32 flx_mask_st20;

		/* Indicate to FW that AN restart has been asserted */
		status = hw->mac.ops.read_iosf_sb_reg(hw,
				IXGBE_KRM_PMD_FLX_MASK_ST20(hw->bus.lan_id),
				IXGBE_SB_IOSF_TARGET_KR_PHY, &flx_mask_st20);

		if (status) {
			DEBUGOUT("Auto-negotiation did not complete\n");
			return status;
		}

		flx_mask_st20 |= IXGBE_KRM_PMD_FLX_MASK_ST20_FW_AN_RESTART;
		status = hw->mac.ops.write_iosf_sb_reg(hw,
				IXGBE_KRM_PMD_FLX_MASK_ST20(hw->bus.lan_id),
				IXGBE_SB_IOSF_TARGET_KR_PHY, flx_mask_st20);
	}

	return status;
}

/**
 * ixgbe_setup_sgmii - Set up link for sgmii
 * @hw: pointer to hardware structure
 * @speed: new link speed
 * @autoneg_wait: TRUE when waiting for completion is needed
 */
static s32 ixgbe_setup_sgmii(struct ixgbe_hw *hw, ixgbe_link_speed speed,
			     bool autoneg_wait)
{
	struct ixgbe_mac_info *mac = &hw->mac;
	u32 lval, sval, flx_val;
	s32 rc;

	rc = mac->ops.read_iosf_sb_reg(hw,
				       IXGBE_KRM_LINK_CTRL_1(hw->bus.lan_id),
				       IXGBE_SB_IOSF_TARGET_KR_PHY, &lval);
	if (rc)
		return rc;

	lval &= ~IXGBE_KRM_LINK_CTRL_1_TETH_AN_ENABLE;
	lval &= ~IXGBE_KRM_LINK_CTRL_1_TETH_FORCE_SPEED_MASK;
	lval |= IXGBE_KRM_LINK_CTRL_1_TETH_AN_SGMII_EN;
	lval |= IXGBE_KRM_LINK_CTRL_1_TETH_AN_CLAUSE_37_EN;
	lval |= IXGBE_KRM_LINK_CTRL_1_TETH_FORCE_SPEED_1G;
	rc = mac->ops.write_iosf_sb_reg(hw,
					IXGBE_KRM_LINK_CTRL_1(hw->bus.lan_id),
					IXGBE_SB_IOSF_TARGET_KR_PHY, lval);
	if (rc)
		return rc;

	rc = mac->ops.read_iosf_sb_reg(hw,
				       IXGBE_KRM_SGMII_CTRL(hw->bus.lan_id),
				       IXGBE_SB_IOSF_TARGET_KR_PHY, &sval);
	if (rc)
		return rc;

	sval |= IXGBE_KRM_SGMII_CTRL_MAC_TAR_FORCE_10_D;
	sval |= IXGBE_KRM_SGMII_CTRL_MAC_TAR_FORCE_100_D;
	rc = mac->ops.write_iosf_sb_reg(hw,
					IXGBE_KRM_SGMII_CTRL(hw->bus.lan_id),
					IXGBE_SB_IOSF_TARGET_KR_PHY, sval);
	if (rc)
		return rc;

	rc = mac->ops.read_iosf_sb_reg(hw,
				    IXGBE_KRM_PMD_FLX_MASK_ST20(hw->bus.lan_id),
				    IXGBE_SB_IOSF_TARGET_KR_PHY, &flx_val);
	if (rc)
		return rc;

	flx_val &= ~IXGBE_KRM_PMD_FLX_MASK_ST20_SPEED_MASK;
	flx_val |= IXGBE_KRM_PMD_FLX_MASK_ST20_SPEED_1G;
	flx_val &= ~IXGBE_KRM_PMD_FLX_MASK_ST20_AN_EN;
	flx_val |= IXGBE_KRM_PMD_FLX_MASK_ST20_SGMII_EN;
	flx_val |= IXGBE_KRM_PMD_FLX_MASK_ST20_AN37_EN;

	rc = mac->ops.write_iosf_sb_reg(hw,
				    IXGBE_KRM_PMD_FLX_MASK_ST20(hw->bus.lan_id),
				    IXGBE_SB_IOSF_TARGET_KR_PHY, flx_val);
	if (rc)
		return rc;

	rc = ixgbe_restart_an_internal_phy_x550em(hw);
	if (rc)
		return rc;

	return hw->phy.ops.setup_link_speed(hw, speed, autoneg_wait);
}

/**
 * ixgbe_setup_sgmii_fw - Set up link for internal PHY SGMII auto-negotiation
 * @hw: pointer to hardware structure
 * @speed: new link speed
 * @autoneg_wait: TRUE when waiting for completion is needed
 */
static s32 ixgbe_setup_sgmii_fw(struct ixgbe_hw *hw, ixgbe_link_speed speed,
				bool autoneg_wait)
{
	struct ixgbe_mac_info *mac = &hw->mac;
	u32 lval, sval, flx_val;
	s32 rc;

	rc = mac->ops.read_iosf_sb_reg(hw,
				       IXGBE_KRM_LINK_CTRL_1(hw->bus.lan_id),
				       IXGBE_SB_IOSF_TARGET_KR_PHY, &lval);
	if (rc)
		return rc;

	lval &= ~IXGBE_KRM_LINK_CTRL_1_TETH_AN_ENABLE;
	lval &= ~IXGBE_KRM_LINK_CTRL_1_TETH_FORCE_SPEED_MASK;
	lval |= IXGBE_KRM_LINK_CTRL_1_TETH_AN_SGMII_EN;
	lval |= IXGBE_KRM_LINK_CTRL_1_TETH_AN_CLAUSE_37_EN;
	lval &= ~IXGBE_KRM_LINK_CTRL_1_TETH_FORCE_SPEED_1G;
	rc = mac->ops.write_iosf_sb_reg(hw,
					IXGBE_KRM_LINK_CTRL_1(hw->bus.lan_id),
					IXGBE_SB_IOSF_TARGET_KR_PHY, lval);
	if (rc)
		return rc;

	rc = mac->ops.read_iosf_sb_reg(hw,
				       IXGBE_KRM_SGMII_CTRL(hw->bus.lan_id),
				       IXGBE_SB_IOSF_TARGET_KR_PHY, &sval);
	if (rc)
		return rc;

	sval &= ~IXGBE_KRM_SGMII_CTRL_MAC_TAR_FORCE_10_D;
	sval &= ~IXGBE_KRM_SGMII_CTRL_MAC_TAR_FORCE_100_D;
	rc = mac->ops.write_iosf_sb_reg(hw,
					IXGBE_KRM_SGMII_CTRL(hw->bus.lan_id),
					IXGBE_SB_IOSF_TARGET_KR_PHY, sval);
	if (rc)
		return rc;

	rc = mac->ops.write_iosf_sb_reg(hw,
					IXGBE_KRM_LINK_CTRL_1(hw->bus.lan_id),
					IXGBE_SB_IOSF_TARGET_KR_PHY, lval);
	if (rc)
		return rc;

	rc = mac->ops.read_iosf_sb_reg(hw,
				    IXGBE_KRM_PMD_FLX_MASK_ST20(hw->bus.lan_id),
				    IXGBE_SB_IOSF_TARGET_KR_PHY, &flx_val);
	if (rc)
		return rc;

	flx_val &= ~IXGBE_KRM_PMD_FLX_MASK_ST20_SPEED_MASK;
	flx_val |= IXGBE_KRM_PMD_FLX_MASK_ST20_SPEED_AN;
	flx_val &= ~IXGBE_KRM_PMD_FLX_MASK_ST20_AN_EN;
	flx_val |= IXGBE_KRM_PMD_FLX_MASK_ST20_SGMII_EN;
	flx_val |= IXGBE_KRM_PMD_FLX_MASK_ST20_AN37_EN;

	rc = mac->ops.write_iosf_sb_reg(hw,
				    IXGBE_KRM_PMD_FLX_MASK_ST20(hw->bus.lan_id),
				    IXGBE_SB_IOSF_TARGET_KR_PHY, flx_val);
	if (rc)
		return rc;

	rc = ixgbe_restart_an_internal_phy_x550em(hw);

	return hw->phy.ops.setup_link_speed(hw, speed, autoneg_wait);
}

/**
 *  ixgbe_init_mac_link_ops_X550em - init mac link function pointers
 *  @hw: pointer to hardware structure
 */
void ixgbe_init_mac_link_ops_X550em(struct ixgbe_hw *hw)
{
	struct ixgbe_mac_info *mac = &hw->mac;

	DEBUGFUNC("ixgbe_init_mac_link_ops_X550em");

	switch (hw->mac.ops.get_media_type(hw)) {
	case ixgbe_media_type_fiber:
		/* CS4227 does not support autoneg, so disable the laser control
		 * functions for SFP+ fiber
		 */
		mac->ops.disable_tx_laser = NULL;
		mac->ops.enable_tx_laser = NULL;
		mac->ops.flap_tx_laser = NULL;
		mac->ops.setup_link = ixgbe_setup_mac_link_multispeed_fiber;
		mac->ops.set_rate_select_speed =
					ixgbe_set_soft_rate_select_speed;

		if ((hw->device_id == IXGBE_DEV_ID_X550EM_A_SFP_N) ||
		    (hw->device_id == IXGBE_DEV_ID_X550EM_A_SFP))
			mac->ops.setup_mac_link =
						ixgbe_setup_mac_link_sfp_x550a;
		else
			mac->ops.setup_mac_link =
						ixgbe_setup_mac_link_sfp_x550em;
		break;
	case ixgbe_media_type_copper:
		if (hw->device_id == IXGBE_DEV_ID_X550EM_X_1G_T)
			break;
		if (hw->mac.type == ixgbe_mac_X550EM_a) {
			if (hw->device_id == IXGBE_DEV_ID_X550EM_A_1G_T ||
			    hw->device_id == IXGBE_DEV_ID_X550EM_A_1G_T_L) {
				mac->ops.setup_link = ixgbe_setup_sgmii_fw;
				mac->ops.check_link =
						   ixgbe_check_mac_link_generic;
			} else {
				mac->ops.setup_link =
						  ixgbe_setup_mac_link_t_X550em;
			}
		} else {
			mac->ops.setup_link = ixgbe_setup_mac_link_t_X550em;
			mac->ops.check_link = ixgbe_check_link_t_X550em;
		}
		break;
	case ixgbe_media_type_backplane:
		if (hw->device_id == IXGBE_DEV_ID_X550EM_A_SGMII ||
		    hw->device_id == IXGBE_DEV_ID_X550EM_A_SGMII_L)
			mac->ops.setup_link = ixgbe_setup_sgmii;
		break;
	default:
		break;
	}
}

/**
 *  ixgbe_get_link_capabilities_x550em - Determines link capabilities
 *  @hw: pointer to hardware structure
 *  @speed: pointer to link speed
 *  @autoneg: TRUE when autoneg or autotry is enabled
 */
s32 ixgbe_get_link_capabilities_X550em(struct ixgbe_hw *hw,
				       ixgbe_link_speed *speed,
				       bool *autoneg)
{
	DEBUGFUNC("ixgbe_get_link_capabilities_X550em");


	if (hw->phy.type == ixgbe_phy_fw) {
		*autoneg = TRUE;
		*speed = hw->phy.speeds_supported;
		return 0;
	}

	/* SFP */
	if (hw->phy.media_type == ixgbe_media_type_fiber) {

		/* CS4227 SFP must not enable auto-negotiation */
		*autoneg = FALSE;

		/* Check if 1G SFP module. */
		if (hw->phy.sfp_type == ixgbe_sfp_type_1g_sx_core0 ||
		    hw->phy.sfp_type == ixgbe_sfp_type_1g_sx_core1
		    || hw->phy.sfp_type == ixgbe_sfp_type_1g_lx_core0 ||
		    hw->phy.sfp_type == ixgbe_sfp_type_1g_lx_core1) {
			*speed = IXGBE_LINK_SPEED_1GB_FULL;
			return IXGBE_SUCCESS;
		}

		/* Link capabilities are based on SFP */
		if (hw->phy.multispeed_fiber)
			*speed = IXGBE_LINK_SPEED_10GB_FULL |
				 IXGBE_LINK_SPEED_1GB_FULL;
		else
			*speed = IXGBE_LINK_SPEED_10GB_FULL;
	} else {
		switch (hw->phy.type) {
		case ixgbe_phy_ext_1g_t:
		case ixgbe_phy_sgmii:
			*speed = IXGBE_LINK_SPEED_1GB_FULL;
			break;
		case ixgbe_phy_x550em_kr:
			if (hw->mac.type == ixgbe_mac_X550EM_a) {
				/* check different backplane modes */
				if (hw->phy.nw_mng_if_sel &
					   IXGBE_NW_MNG_IF_SEL_PHY_SPEED_2_5G) {
					*speed = IXGBE_LINK_SPEED_2_5GB_FULL;
					break;
				} else if (hw->device_id ==
						   IXGBE_DEV_ID_X550EM_A_KR_L) {
					*speed = IXGBE_LINK_SPEED_1GB_FULL;
					break;
				}
			}
			/* fall through */
		default:
			*speed = IXGBE_LINK_SPEED_10GB_FULL |
				 IXGBE_LINK_SPEED_1GB_FULL;
			break;
		}
		*autoneg = TRUE;
	}

	return IXGBE_SUCCESS;
}

/**
 * ixgbe_get_lasi_ext_t_x550em - Determime external Base T PHY interrupt cause
 * @hw: pointer to hardware structure
 * @lsc: pointer to boolean flag which indicates whether external Base T
 *       PHY interrupt is lsc
 *
 * Determime if external Base T PHY interrupt cause is high temperature
 * failure alarm or link status change.
 *
 * Return IXGBE_ERR_OVERTEMP if interrupt is high temperature
 * failure alarm, else return PHY access status.
 */
static s32 ixgbe_get_lasi_ext_t_x550em(struct ixgbe_hw *hw, bool *lsc)
{
	u32 status;
	u16 reg;

	*lsc = FALSE;

	/* Vendor alarm triggered */
	status = hw->phy.ops.read_reg(hw, IXGBE_MDIO_GLOBAL_CHIP_STD_INT_FLAG,
				      IXGBE_MDIO_VENDOR_SPECIFIC_1_DEV_TYPE,
				      &reg);

	if (status != IXGBE_SUCCESS ||
	    !(reg & IXGBE_MDIO_GLOBAL_VEN_ALM_INT_EN))
		return status;

	/* Vendor Auto-Neg alarm triggered or Global alarm 1 triggered */
	status = hw->phy.ops.read_reg(hw, IXGBE_MDIO_GLOBAL_INT_CHIP_VEN_FLAG,
				      IXGBE_MDIO_VENDOR_SPECIFIC_1_DEV_TYPE,
				      &reg);

	if (status != IXGBE_SUCCESS ||
	    !(reg & (IXGBE_MDIO_GLOBAL_AN_VEN_ALM_INT_EN |
	    IXGBE_MDIO_GLOBAL_ALARM_1_INT)))
		return status;

	/* Global alarm triggered */
	status = hw->phy.ops.read_reg(hw, IXGBE_MDIO_GLOBAL_ALARM_1,
				      IXGBE_MDIO_VENDOR_SPECIFIC_1_DEV_TYPE,
				      &reg);

	if (status != IXGBE_SUCCESS)
		return status;

	/* If high temperature failure, then return over temp error and exit */
	if (reg & IXGBE_MDIO_GLOBAL_ALM_1_HI_TMP_FAIL) {
		/* power down the PHY in case the PHY FW didn't already */
		ixgbe_set_copper_phy_power(hw, FALSE);
		return IXGBE_ERR_OVERTEMP;
	} else if (reg & IXGBE_MDIO_GLOBAL_ALM_1_DEV_FAULT) {
		/*  device fault alarm triggered */
		status = hw->phy.ops.read_reg(hw, IXGBE_MDIO_GLOBAL_FAULT_MSG,
					  IXGBE_MDIO_VENDOR_SPECIFIC_1_DEV_TYPE,
					  &reg);

		if (status != IXGBE_SUCCESS)
			return status;

		/* if device fault was due to high temp alarm handle and exit */
		if (reg == IXGBE_MDIO_GLOBAL_FAULT_MSG_HI_TMP) {
			/* power down the PHY in case the PHY FW didn't */
			ixgbe_set_copper_phy_power(hw, FALSE);
			return IXGBE_ERR_OVERTEMP;
		}
	}

	/* Vendor alarm 2 triggered */
	status = hw->phy.ops.read_reg(hw, IXGBE_MDIO_GLOBAL_CHIP_STD_INT_FLAG,
				      IXGBE_MDIO_AUTO_NEG_DEV_TYPE, &reg);

	if (status != IXGBE_SUCCESS ||
	    !(reg & IXGBE_MDIO_GLOBAL_STD_ALM2_INT))
		return status;

	/* link connect/disconnect event occurred */
	status = hw->phy.ops.read_reg(hw, IXGBE_MDIO_AUTO_NEG_VENDOR_TX_ALARM2,
				      IXGBE_MDIO_AUTO_NEG_DEV_TYPE, &reg);

	if (status != IXGBE_SUCCESS)
		return status;

	/* Indicate LSC */
	if (reg & IXGBE_MDIO_AUTO_NEG_VEN_LSC)
		*lsc = TRUE;

	return IXGBE_SUCCESS;
}

/**
 * ixgbe_enable_lasi_ext_t_x550em - Enable external Base T PHY interrupts
 * @hw: pointer to hardware structure
 *
 * Enable link status change and temperature failure alarm for the external
 * Base T PHY
 *
 * Returns PHY access status
 */
static s32 ixgbe_enable_lasi_ext_t_x550em(struct ixgbe_hw *hw)
{
	u32 status;
	u16 reg;
	bool lsc;

	/* Clear interrupt flags */
	status = ixgbe_get_lasi_ext_t_x550em(hw, &lsc);

	/* Enable link status change alarm */

	/* Enable the LASI interrupts on X552 devices to receive notifications
	 * of the link configurations of the external PHY and correspondingly
	 * support the configuration of the internal iXFI link, since iXFI does
	 * not support auto-negotiation. This is not required for X553 devices
	 * having KR support, which performs auto-negotiations and which is used
	 * as the internal link to the external PHY. Hence adding a check here
	 * to avoid enabling LASI interrupts for X553 devices.
	 */
	if (hw->mac.type != ixgbe_mac_X550EM_a) {
		status = hw->phy.ops.read_reg(hw,
					IXGBE_MDIO_PMA_TX_VEN_LASI_INT_MASK,
					IXGBE_MDIO_AUTO_NEG_DEV_TYPE, &reg);

		if (status != IXGBE_SUCCESS)
			return status;

		reg |= IXGBE_MDIO_PMA_TX_VEN_LASI_INT_EN;

		status = hw->phy.ops.write_reg(hw,
					IXGBE_MDIO_PMA_TX_VEN_LASI_INT_MASK,
					IXGBE_MDIO_AUTO_NEG_DEV_TYPE, reg);

		if (status != IXGBE_SUCCESS)
			return status;
	}

	/* Enable high temperature failure and global fault alarms */
	status = hw->phy.ops.read_reg(hw, IXGBE_MDIO_GLOBAL_INT_MASK,
				      IXGBE_MDIO_VENDOR_SPECIFIC_1_DEV_TYPE,
				      &reg);

	if (status != IXGBE_SUCCESS)
		return status;

	reg |= (IXGBE_MDIO_GLOBAL_INT_HI_TEMP_EN |
		IXGBE_MDIO_GLOBAL_INT_DEV_FAULT_EN);

	status = hw->phy.ops.write_reg(hw, IXGBE_MDIO_GLOBAL_INT_MASK,
				       IXGBE_MDIO_VENDOR_SPECIFIC_1_DEV_TYPE,
				       reg);

	if (status != IXGBE_SUCCESS)
		return status;

	/* Enable vendor Auto-Neg alarm and Global Interrupt Mask 1 alarm */
	status = hw->phy.ops.read_reg(hw, IXGBE_MDIO_GLOBAL_INT_CHIP_VEN_MASK,
				      IXGBE_MDIO_VENDOR_SPECIFIC_1_DEV_TYPE,
				      &reg);

	if (status != IXGBE_SUCCESS)
		return status;

	reg |= (IXGBE_MDIO_GLOBAL_AN_VEN_ALM_INT_EN |
		IXGBE_MDIO_GLOBAL_ALARM_1_INT);

	status = hw->phy.ops.write_reg(hw, IXGBE_MDIO_GLOBAL_INT_CHIP_VEN_MASK,
				       IXGBE_MDIO_VENDOR_SPECIFIC_1_DEV_TYPE,
				       reg);

	if (status != IXGBE_SUCCESS)
		return status;

	/* Enable chip-wide vendor alarm */
	status = hw->phy.ops.read_reg(hw, IXGBE_MDIO_GLOBAL_INT_CHIP_STD_MASK,
				      IXGBE_MDIO_VENDOR_SPECIFIC_1_DEV_TYPE,
				      &reg);

	if (status != IXGBE_SUCCESS)
		return status;

	reg |= IXGBE_MDIO_GLOBAL_VEN_ALM_INT_EN;

	status = hw->phy.ops.write_reg(hw, IXGBE_MDIO_GLOBAL_INT_CHIP_STD_MASK,
				       IXGBE_MDIO_VENDOR_SPECIFIC_1_DEV_TYPE,
				       reg);

	return status;
}

/**
 *  ixgbe_setup_kr_speed_x550em - Configure the KR PHY for link speed.
 *  @hw: pointer to hardware structure
 *  @speed: link speed
 *
 *  Configures the integrated KR PHY.
 **/
static s32 ixgbe_setup_kr_speed_x550em(struct ixgbe_hw *hw,
				       ixgbe_link_speed speed)
{
	s32 status;
	u32 reg_val;

	status = hw->mac.ops.read_iosf_sb_reg(hw,
					IXGBE_KRM_LINK_CTRL_1(hw->bus.lan_id),
					IXGBE_SB_IOSF_TARGET_KR_PHY, &reg_val);
	if (status)
		return status;

	reg_val |= IXGBE_KRM_LINK_CTRL_1_TETH_AN_ENABLE;
	reg_val &= ~(IXGBE_KRM_LINK_CTRL_1_TETH_AN_CAP_KR |
		     IXGBE_KRM_LINK_CTRL_1_TETH_AN_CAP_KX);

	/* Advertise 10G support. */
	if (speed & IXGBE_LINK_SPEED_10GB_FULL)
		reg_val |= IXGBE_KRM_LINK_CTRL_1_TETH_AN_CAP_KR;

	/* Advertise 1G support. */
	if (speed & IXGBE_LINK_SPEED_1GB_FULL)
		reg_val |= IXGBE_KRM_LINK_CTRL_1_TETH_AN_CAP_KX;

	status = hw->mac.ops.write_iosf_sb_reg(hw,
					IXGBE_KRM_LINK_CTRL_1(hw->bus.lan_id),
					IXGBE_SB_IOSF_TARGET_KR_PHY, reg_val);

	if (hw->mac.type == ixgbe_mac_X550EM_a) {
		/* Set lane mode  to KR auto negotiation */
		status = hw->mac.ops.read_iosf_sb_reg(hw,
				    IXGBE_KRM_PMD_FLX_MASK_ST20(hw->bus.lan_id),
				    IXGBE_SB_IOSF_TARGET_KR_PHY, &reg_val);

		if (status)
			return status;

		reg_val &= ~IXGBE_KRM_PMD_FLX_MASK_ST20_SPEED_MASK;
		reg_val |= IXGBE_KRM_PMD_FLX_MASK_ST20_SPEED_AN;
		reg_val |= IXGBE_KRM_PMD_FLX_MASK_ST20_AN_EN;
		reg_val &= ~IXGBE_KRM_PMD_FLX_MASK_ST20_AN37_EN;
		reg_val &= ~IXGBE_KRM_PMD_FLX_MASK_ST20_SGMII_EN;

		status = hw->mac.ops.write_iosf_sb_reg(hw,
				    IXGBE_KRM_PMD_FLX_MASK_ST20(hw->bus.lan_id),
				    IXGBE_SB_IOSF_TARGET_KR_PHY, reg_val);
	}

	return ixgbe_restart_an_internal_phy_x550em(hw);
}

/**
 * ixgbe_reset_phy_fw - Reset firmware-controlled PHYs
 * @hw: pointer to hardware structure
 */
static s32 ixgbe_reset_phy_fw(struct ixgbe_hw *hw)
{
	u32 store[FW_PHY_ACT_DATA_COUNT] = { 0 };
	s32 rc;

	if (hw->phy.reset_disable || ixgbe_check_reset_blocked(hw))
		return IXGBE_SUCCESS;

	rc = ixgbe_fw_phy_activity(hw, FW_PHY_ACT_PHY_SW_RESET, &store);
	if (rc)
		return rc;
	memset(store, 0, sizeof(store));

	rc = ixgbe_fw_phy_activity(hw, FW_PHY_ACT_INIT_PHY, &store);
	if (rc)
		return rc;

	return ixgbe_setup_fw_link(hw);
}

/**
 * ixgbe_check_overtemp_fw - Check firmware-controlled PHYs for overtemp
 * @hw: pointer to hardware structure
 */
static s32 ixgbe_check_overtemp_fw(struct ixgbe_hw *hw)
{
	u32 store[FW_PHY_ACT_DATA_COUNT] = { 0 };
	s32 rc;

	rc = ixgbe_fw_phy_activity(hw, FW_PHY_ACT_GET_LINK_INFO, &store);
	if (rc)
		return rc;

	if (store[0] & FW_PHY_ACT_GET_LINK_INFO_TEMP) {
		ixgbe_shutdown_fw_phy(hw);
		return IXGBE_ERR_OVERTEMP;
	}
	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_read_mng_if_sel_x550em - Read NW_MNG_IF_SEL register
 *  @hw: pointer to hardware structure
 *
 *  Read NW_MNG_IF_SEL register and save field values, and check for valid field
 *  values.
 **/
static s32 ixgbe_read_mng_if_sel_x550em(struct ixgbe_hw *hw)
{
	/* Save NW management interface connected on board. This is used
	 * to determine internal PHY mode.
	 */
	hw->phy.nw_mng_if_sel = IXGBE_READ_REG(hw, IXGBE_NW_MNG_IF_SEL);

	/* If X552 (X550EM_a) and MDIO is connected to external PHY, then set
	 * PHY address. This register field was has only been used for X552.
	 */
	if (hw->mac.type == ixgbe_mac_X550EM_a &&
	    hw->phy.nw_mng_if_sel & IXGBE_NW_MNG_IF_SEL_MDIO_ACT) {
		hw->phy.addr = (hw->phy.nw_mng_if_sel &
				IXGBE_NW_MNG_IF_SEL_MDIO_PHY_ADD) >>
			       IXGBE_NW_MNG_IF_SEL_MDIO_PHY_ADD_SHIFT;
	}

	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_init_phy_ops_X550em - PHY/SFP specific init
 *  @hw: pointer to hardware structure
 *
 *  Initialize any function pointers that were not able to be
 *  set during init_shared_code because the PHY/SFP type was
 *  not known.  Perform the SFP init if necessary.
 */
s32 ixgbe_init_phy_ops_X550em(struct ixgbe_hw *hw)
{
	struct ixgbe_phy_info *phy = &hw->phy;
	s32 ret_val;

	DEBUGFUNC("ixgbe_init_phy_ops_X550em");

	hw->mac.ops.set_lan_id(hw);
	ixgbe_read_mng_if_sel_x550em(hw);

	if (hw->mac.ops.get_media_type(hw) == ixgbe_media_type_fiber) {
		phy->phy_semaphore_mask = IXGBE_GSSR_SHARED_I2C_SM;
		ixgbe_setup_mux_ctl(hw);
		phy->ops.identify_sfp = ixgbe_identify_sfp_module_X550em;
	}

	switch (hw->device_id) {
	case IXGBE_DEV_ID_X550EM_A_1G_T:
	case IXGBE_DEV_ID_X550EM_A_1G_T_L:
		phy->ops.read_reg_mdi = NULL;
		phy->ops.write_reg_mdi = NULL;
		hw->phy.ops.read_reg = NULL;
		hw->phy.ops.write_reg = NULL;
		phy->ops.check_overtemp = ixgbe_check_overtemp_fw;
		if (hw->bus.lan_id)
			hw->phy.phy_semaphore_mask |= IXGBE_GSSR_PHY1_SM;
		else
			hw->phy.phy_semaphore_mask |= IXGBE_GSSR_PHY0_SM;

		break;
	case IXGBE_DEV_ID_X550EM_A_10G_T:
	case IXGBE_DEV_ID_X550EM_A_SFP:
		hw->phy.ops.read_reg = ixgbe_read_phy_reg_x550a;
		hw->phy.ops.write_reg = ixgbe_write_phy_reg_x550a;
		if (hw->bus.lan_id)
			hw->phy.phy_semaphore_mask |= IXGBE_GSSR_PHY1_SM;
		else
			hw->phy.phy_semaphore_mask |= IXGBE_GSSR_PHY0_SM;
		break;
	case IXGBE_DEV_ID_X550EM_X_SFP:
		/* set up for CS4227 usage */
		hw->phy.phy_semaphore_mask = IXGBE_GSSR_SHARED_I2C_SM;
		break;
	case IXGBE_DEV_ID_X550EM_X_1G_T:
		phy->ops.read_reg_mdi = NULL;
		phy->ops.write_reg_mdi = NULL;
	default:
		break;
	}

	/* Identify the PHY or SFP module */
	ret_val = phy->ops.identify(hw);
	if (ret_val == IXGBE_ERR_SFP_NOT_SUPPORTED ||
	    ret_val == IXGBE_ERR_PHY_ADDR_INVALID)
		return ret_val;

	/* Setup function pointers based on detected hardware */
	ixgbe_init_mac_link_ops_X550em(hw);
	if (phy->sfp_type != ixgbe_sfp_type_unknown)
		phy->ops.reset = NULL;

	/* Set functions pointers based on phy type */
	switch (hw->phy.type) {
	case ixgbe_phy_x550em_kx4:
		phy->ops.setup_link = NULL;
		phy->ops.read_reg = ixgbe_read_phy_reg_x550em;
		phy->ops.write_reg = ixgbe_write_phy_reg_x550em;
		break;
	case ixgbe_phy_x550em_kr:
		phy->ops.setup_link = ixgbe_setup_kr_x550em;
		phy->ops.read_reg = ixgbe_read_phy_reg_x550em;
		phy->ops.write_reg = ixgbe_write_phy_reg_x550em;
		break;
	case ixgbe_phy_ext_1g_t:
		/* link is managed by FW */
		phy->ops.setup_link = NULL;
		phy->ops.reset = NULL;
		break;
	case ixgbe_phy_x550em_xfi:
		/* link is managed by HW */
		phy->ops.setup_link = NULL;
		phy->ops.read_reg = ixgbe_read_phy_reg_x550em;
		phy->ops.write_reg = ixgbe_write_phy_reg_x550em;
		break;
	case ixgbe_phy_x550em_ext_t:
		/* If internal link mode is XFI, then setup iXFI internal link,
		 * else setup KR now.
		 */
		phy->ops.setup_internal_link =
					      ixgbe_setup_internal_phy_t_x550em;

		/* setup SW LPLU only for first revision of X550EM_x */
		if ((hw->mac.type == ixgbe_mac_X550EM_x) &&
		    !(IXGBE_FUSES0_REV_MASK &
		      IXGBE_READ_REG(hw, IXGBE_FUSES0_GROUP(0))))
			phy->ops.enter_lplu = ixgbe_enter_lplu_t_x550em;

		phy->ops.handle_lasi = ixgbe_handle_lasi_ext_t_x550em;
		phy->ops.reset = ixgbe_reset_phy_t_X550em;
		break;
	case ixgbe_phy_sgmii:
		phy->ops.setup_link = NULL;
		break;
	case ixgbe_phy_fw:
		phy->ops.setup_link = ixgbe_setup_fw_link;
		phy->ops.reset = ixgbe_reset_phy_fw;
		break;
	default:
		break;
	}
	return ret_val;
}

/**
 * ixgbe_set_mdio_speed - Set MDIO clock speed
 *  @hw: pointer to hardware structure
 */
static void ixgbe_set_mdio_speed(struct ixgbe_hw *hw)
{
	u32 hlreg0;

	switch (hw->device_id) {
	case IXGBE_DEV_ID_X550EM_X_10G_T:
	case IXGBE_DEV_ID_X550EM_A_SGMII:
	case IXGBE_DEV_ID_X550EM_A_SGMII_L:
	case IXGBE_DEV_ID_X550EM_A_10G_T:
	case IXGBE_DEV_ID_X550EM_A_SFP:
	case IXGBE_DEV_ID_X550EM_A_QSFP:
		/* Config MDIO clock speed before the first MDIO PHY access */
		hlreg0 = IXGBE_READ_REG(hw, IXGBE_HLREG0);
		hlreg0 &= ~IXGBE_HLREG0_MDCSPD;
		IXGBE_WRITE_REG(hw, IXGBE_HLREG0, hlreg0);
		break;
	case IXGBE_DEV_ID_X550EM_A_1G_T:
	case IXGBE_DEV_ID_X550EM_A_1G_T_L:
		/* Select fast MDIO clock speed for these devices */
		hlreg0 = IXGBE_READ_REG(hw, IXGBE_HLREG0);
		hlreg0 |= IXGBE_HLREG0_MDCSPD;
		IXGBE_WRITE_REG(hw, IXGBE_HLREG0, hlreg0);
		break;
	default:
		break;
	}
}

/**
 *  ixgbe_reset_hw_X550em - Perform hardware reset
 *  @hw: pointer to hardware structure
 *
 *  Resets the hardware by resetting the transmit and receive units, masks
 *  and clears all interrupts, perform a PHY reset, and perform a link (MAC)
 *  reset.
 */
s32 ixgbe_reset_hw_X550em(struct ixgbe_hw *hw)
{
	ixgbe_link_speed link_speed;
	s32 status;
	u32 ctrl = 0;
	u32 i;
	bool link_up = FALSE;
	u32 swfw_mask = hw->phy.phy_semaphore_mask;

	DEBUGFUNC("ixgbe_reset_hw_X550em");

	/* Call adapter stop to disable Tx/Rx and clear interrupts */
	status = hw->mac.ops.stop_adapter(hw);
	if (status != IXGBE_SUCCESS) {
		DEBUGOUT1("Failed to stop adapter, STATUS = %d\n", status);
		return status;
	}
	/* flush pending Tx transactions */
	ixgbe_clear_tx_pending(hw);

	ixgbe_set_mdio_speed(hw);

	/* PHY ops must be identified and initialized prior to reset */
	status = hw->phy.ops.init(hw);

	if (status)
		DEBUGOUT1("Failed to initialize PHY ops, STATUS = %d\n",
			  status);

	if (status == IXGBE_ERR_SFP_NOT_SUPPORTED ||
	    status == IXGBE_ERR_PHY_ADDR_INVALID) {
		DEBUGOUT("Returning from reset HW due to PHY init failure\n");
		return status;
	}

	/* start the external PHY */
	if (hw->phy.type == ixgbe_phy_x550em_ext_t) {
		status = ixgbe_init_ext_t_x550em(hw);
		if (status) {
			DEBUGOUT1("Failed to start the external PHY, STATUS = %d\n",
				  status);
			return status;
		}
	}

	/* Setup SFP module if there is one present. */
	if (hw->phy.sfp_setup_needed) {
		status = hw->mac.ops.setup_sfp(hw);
		hw->phy.sfp_setup_needed = FALSE;
	}

	if (status == IXGBE_ERR_SFP_NOT_SUPPORTED)
		return status;

	/* Reset PHY */
	if (!hw->phy.reset_disable && hw->phy.ops.reset) {
		if (hw->phy.ops.reset(hw) == IXGBE_ERR_OVERTEMP)
			return IXGBE_ERR_OVERTEMP;
	}

mac_reset_top:
	/* Issue global reset to the MAC.  Needs to be SW reset if link is up.
	 * If link reset is used when link is up, it might reset the PHY when
	 * mng is using it.  If link is down or the flag to force full link
	 * reset is set, then perform link reset.
	 */
	ctrl = IXGBE_CTRL_LNK_RST;
	if (!hw->force_full_reset) {
		hw->mac.ops.check_link(hw, &link_speed, &link_up, FALSE);
		if (link_up)
			ctrl = IXGBE_CTRL_RST;
	}

	status = hw->mac.ops.acquire_swfw_sync(hw, swfw_mask);
	if (status != IXGBE_SUCCESS) {
		ERROR_REPORT2(IXGBE_ERROR_CAUTION,
			"semaphore failed with %d", status);
		return IXGBE_ERR_SWFW_SYNC;
	}
	ctrl |= IXGBE_READ_REG(hw, IXGBE_CTRL);
	IXGBE_WRITE_REG(hw, IXGBE_CTRL, ctrl);
	IXGBE_WRITE_FLUSH(hw);
	hw->mac.ops.release_swfw_sync(hw, swfw_mask);

	/* Poll for reset bit to self-clear meaning reset is complete */
	for (i = 0; i < 10; i++) {
		usec_delay(1);
		ctrl = IXGBE_READ_REG(hw, IXGBE_CTRL);
		if (!(ctrl & IXGBE_CTRL_RST_MASK))
			break;
	}

	if (ctrl & IXGBE_CTRL_RST_MASK) {
		status = IXGBE_ERR_RESET_FAILED;
		DEBUGOUT("Reset polling failed to complete.\n");
	}

	msec_delay(50);

	/* Double resets are required for recovery from certain error
	 * conditions.  Between resets, it is necessary to stall to
	 * allow time for any pending HW events to complete.
	 */
	if (hw->mac.flags & IXGBE_FLAGS_DOUBLE_RESET_REQUIRED) {
		hw->mac.flags &= ~IXGBE_FLAGS_DOUBLE_RESET_REQUIRED;
		goto mac_reset_top;
	}

	/* Store the permanent mac address */
	hw->mac.ops.get_mac_addr(hw, hw->mac.perm_addr);

	/* Store MAC address from RAR0, clear receive address registers, and
	 * clear the multicast table.  Also reset num_rar_entries to 128,
	 * since we modify this value when programming the SAN MAC address.
	 */
	hw->mac.num_rar_entries = 128;
	hw->mac.ops.init_rx_addrs(hw);

	ixgbe_set_mdio_speed(hw);

	if (hw->device_id == IXGBE_DEV_ID_X550EM_X_SFP)
		ixgbe_setup_mux_ctl(hw);

	if (status != IXGBE_SUCCESS)
		DEBUGOUT1("Reset HW failed, STATUS = %d\n", status);

	return status;
}

/**
 * ixgbe_init_ext_t_x550em - Start (unstall) the external Base T PHY.
 * @hw: pointer to hardware structure
 */
s32 ixgbe_init_ext_t_x550em(struct ixgbe_hw *hw)
{
	u32 status;
	u16 reg;

	status = hw->phy.ops.read_reg(hw,
				      IXGBE_MDIO_TX_VENDOR_ALARMS_3,
				      IXGBE_MDIO_PMA_PMD_DEV_TYPE,
				      &reg);

	if (status != IXGBE_SUCCESS)
		return status;

	/* If PHY FW reset completed bit is set then this is the first
	 * SW instance after a power on so the PHY FW must be un-stalled.
	 */
	if (reg & IXGBE_MDIO_TX_VENDOR_ALARMS_3_RST_MASK) {
		status = hw->phy.ops.read_reg(hw,
					IXGBE_MDIO_GLOBAL_RES_PR_10,
					IXGBE_MDIO_VENDOR_SPECIFIC_1_DEV_TYPE,
					&reg);

		if (status != IXGBE_SUCCESS)
			return status;

		reg &= ~IXGBE_MDIO_POWER_UP_STALL;

		status = hw->phy.ops.write_reg(hw,
					IXGBE_MDIO_GLOBAL_RES_PR_10,
					IXGBE_MDIO_VENDOR_SPECIFIC_1_DEV_TYPE,
					reg);

		if (status != IXGBE_SUCCESS)
			return status;
	}

	return status;
}

/**
 *  ixgbe_setup_kr_x550em - Configure the KR PHY.
 *  @hw: pointer to hardware structure
 **/
s32 ixgbe_setup_kr_x550em(struct ixgbe_hw *hw)
{
	/* leave link alone for 2.5G */
	if (hw->phy.autoneg_advertised & IXGBE_LINK_SPEED_2_5GB_FULL)
		return IXGBE_SUCCESS;

	if (ixgbe_check_reset_blocked(hw))
		return 0;

	return ixgbe_setup_kr_speed_x550em(hw, hw->phy.autoneg_advertised);
}

/**
 *  ixgbe_setup_mac_link_sfp_x550em - Setup internal/external the PHY for SFP
 *  @hw: pointer to hardware structure
 *  @speed: new link speed
 *  @autoneg_wait_to_complete: unused
 *
 *  Configure the external PHY and the integrated KR PHY for SFP support.
 **/
s32 ixgbe_setup_mac_link_sfp_x550em(struct ixgbe_hw *hw,
				    ixgbe_link_speed speed,
				    bool autoneg_wait_to_complete)
{
	s32 ret_val;
	u16 reg_slice, reg_val;
	bool setup_linear = FALSE;
	UNREFERENCED_1PARAMETER(autoneg_wait_to_complete);

	/* Check if SFP module is supported and linear */
	ret_val = ixgbe_supported_sfp_modules_X550em(hw, &setup_linear);

	/* If no SFP module present, then return success. Return success since
	 * there is no reason to configure CS4227 and SFP not present error is
	 * not excepted in the setup MAC link flow.
	 */
	if (ret_val == IXGBE_ERR_SFP_NOT_PRESENT)
		return IXGBE_SUCCESS;

	if (ret_val != IXGBE_SUCCESS)
		return ret_val;

	/* Configure internal PHY for KR/KX. */
	ixgbe_setup_kr_speed_x550em(hw, speed);

	/* Configure CS4227 LINE side to proper mode. */
	reg_slice = IXGBE_CS4227_LINE_SPARE24_LSB +
		    (hw->bus.lan_id << 12);
	if (setup_linear)
		reg_val = (IXGBE_CS4227_EDC_MODE_CX1 << 1) | 0x1;
	else
		reg_val = (IXGBE_CS4227_EDC_MODE_SR << 1) | 0x1;
	ret_val = hw->link.ops.write_link(hw, hw->link.addr, reg_slice,
					  reg_val);
	return ret_val;
}

/**
 *  ixgbe_setup_sfi_x550a - Configure the internal PHY for native SFI mode
 *  @hw: pointer to hardware structure
 *  @speed: the link speed to force
 *
 *  Configures the integrated PHY for native SFI mode. Used to connect the
 *  internal PHY directly to an SFP cage, without autonegotiation.
 **/
static s32 ixgbe_setup_sfi_x550a(struct ixgbe_hw *hw, ixgbe_link_speed *speed)
{
	struct ixgbe_mac_info *mac = &hw->mac;
	s32 status;
	u32 reg_val;

	/* Disable all AN and force speed to 10G Serial. */
	status = mac->ops.read_iosf_sb_reg(hw,
				IXGBE_KRM_PMD_FLX_MASK_ST20(hw->bus.lan_id),
				IXGBE_SB_IOSF_TARGET_KR_PHY, &reg_val);
	if (status != IXGBE_SUCCESS)
		return status;

	reg_val &= ~IXGBE_KRM_PMD_FLX_MASK_ST20_AN_EN;
	reg_val &= ~IXGBE_KRM_PMD_FLX_MASK_ST20_AN37_EN;
	reg_val &= ~IXGBE_KRM_PMD_FLX_MASK_ST20_SGMII_EN;
	reg_val &= ~IXGBE_KRM_PMD_FLX_MASK_ST20_SPEED_MASK;

	/* Select forced link speed for internal PHY. */
	switch (*speed) {
	case IXGBE_LINK_SPEED_10GB_FULL:
		reg_val |= IXGBE_KRM_PMD_FLX_MASK_ST20_SPEED_10G;
		break;
	case IXGBE_LINK_SPEED_1GB_FULL:
		reg_val |= IXGBE_KRM_PMD_FLX_MASK_ST20_SPEED_1G;
		break;
	default:
		/* Other link speeds are not supported by internal PHY. */
		return IXGBE_ERR_LINK_SETUP;
	}

	status = mac->ops.write_iosf_sb_reg(hw,
				IXGBE_KRM_PMD_FLX_MASK_ST20(hw->bus.lan_id),
				IXGBE_SB_IOSF_TARGET_KR_PHY, reg_val);

	/* Toggle port SW reset by AN reset. */
	status = ixgbe_restart_an_internal_phy_x550em(hw);

	return status;
}

/**
 *  ixgbe_setup_mac_link_sfp_x550a - Setup internal PHY for SFP
 *  @hw: pointer to hardware structure
 *  @speed: new link speed
 *  @autoneg_wait_to_complete: unused
 *
 *  Configure the the integrated PHY for SFP support.
 **/
s32 ixgbe_setup_mac_link_sfp_x550a(struct ixgbe_hw *hw,
				    ixgbe_link_speed speed,
				    bool autoneg_wait_to_complete)
{
	s32 ret_val;
	u16 reg_phy_ext;
	bool setup_linear = FALSE;
	u32 reg_slice, reg_phy_int, slice_offset;

	UNREFERENCED_1PARAMETER(autoneg_wait_to_complete);

	/* Check if SFP module is supported and linear */
	ret_val = ixgbe_supported_sfp_modules_X550em(hw, &setup_linear);

	/* If no SFP module present, then return success. Return success since
	 * SFP not present error is not excepted in the setup MAC link flow.
	 */
	if (ret_val == IXGBE_ERR_SFP_NOT_PRESENT)
		return IXGBE_SUCCESS;

	if (ret_val != IXGBE_SUCCESS)
		return ret_val;

	if (hw->device_id == IXGBE_DEV_ID_X550EM_A_SFP_N) {
		/* Configure internal PHY for native SFI based on module type */
		ret_val = hw->mac.ops.read_iosf_sb_reg(hw,
				   IXGBE_KRM_PMD_FLX_MASK_ST20(hw->bus.lan_id),
				   IXGBE_SB_IOSF_TARGET_KR_PHY, &reg_phy_int);

		if (ret_val != IXGBE_SUCCESS)
			return ret_val;

		reg_phy_int &= IXGBE_KRM_PMD_FLX_MASK_ST20_SFI_10G_DA;
		if (!setup_linear)
			reg_phy_int |= IXGBE_KRM_PMD_FLX_MASK_ST20_SFI_10G_SR;

		ret_val = hw->mac.ops.write_iosf_sb_reg(hw,
				   IXGBE_KRM_PMD_FLX_MASK_ST20(hw->bus.lan_id),
				   IXGBE_SB_IOSF_TARGET_KR_PHY, reg_phy_int);

		if (ret_val != IXGBE_SUCCESS)
			return ret_val;

		/* Setup SFI internal link. */
		ret_val = ixgbe_setup_sfi_x550a(hw, &speed);
	} else {
		/* Configure internal PHY for KR/KX. */
		ixgbe_setup_kr_speed_x550em(hw, speed);

		if (hw->phy.addr == 0x0 || hw->phy.addr == 0xFFFF) {
			/* Find Address */
			DEBUGOUT("Invalid NW_MNG_IF_SEL.MDIO_PHY_ADD value\n");
			return IXGBE_ERR_PHY_ADDR_INVALID;
		}

		/* Get external PHY SKU id */
		ret_val = hw->phy.ops.read_reg(hw, IXGBE_CS4227_EFUSE_PDF_SKU,
					IXGBE_MDIO_ZERO_DEV_TYPE, &reg_phy_ext);

		if (ret_val != IXGBE_SUCCESS)
			return ret_val;

		/* When configuring quad port CS4223, the MAC instance is part
		 * of the slice offset.
		 */
		if (reg_phy_ext == IXGBE_CS4223_SKU_ID)
			slice_offset = (hw->bus.lan_id +
					(hw->bus.instance_id << 1)) << 12;
		else
			slice_offset = hw->bus.lan_id << 12;

		/* Configure CS4227/CS4223 LINE side to proper mode. */
		reg_slice = IXGBE_CS4227_LINE_SPARE24_LSB + slice_offset;

		ret_val = hw->phy.ops.read_reg(hw, reg_slice,
					IXGBE_MDIO_ZERO_DEV_TYPE, &reg_phy_ext);

		if (ret_val != IXGBE_SUCCESS)
			return ret_val;

		reg_phy_ext &= ~((IXGBE_CS4227_EDC_MODE_CX1 << 1) |
				 (IXGBE_CS4227_EDC_MODE_SR << 1));

		if (setup_linear)
			reg_phy_ext = (IXGBE_CS4227_EDC_MODE_CX1 << 1) | 0x1;
		else
			reg_phy_ext = (IXGBE_CS4227_EDC_MODE_SR << 1) | 0x1;
		ret_val = hw->phy.ops.write_reg(hw, reg_slice,
					 IXGBE_MDIO_ZERO_DEV_TYPE, reg_phy_ext);

		/* Flush previous write with a read */
		ret_val = hw->phy.ops.read_reg(hw, reg_slice,
					IXGBE_MDIO_ZERO_DEV_TYPE, &reg_phy_ext);
	}
	return ret_val;
}

/**
 *  ixgbe_setup_ixfi_x550em_x - MAC specific iXFI configuration
 *  @hw: pointer to hardware structure
 *
 *  iXfI configuration needed for ixgbe_mac_X550EM_x devices.
 **/
static s32 ixgbe_setup_ixfi_x550em_x(struct ixgbe_hw *hw)
{
	struct ixgbe_mac_info *mac = &hw->mac;
	s32 status;
	u32 reg_val;

	/* Disable training protocol FSM. */
	status = mac->ops.read_iosf_sb_reg(hw,
				IXGBE_KRM_RX_TRN_LINKUP_CTRL(hw->bus.lan_id),
				IXGBE_SB_IOSF_TARGET_KR_PHY, &reg_val);
	if (status != IXGBE_SUCCESS)
		return status;
	reg_val |= IXGBE_KRM_RX_TRN_LINKUP_CTRL_CONV_WO_PROTOCOL;
	status = mac->ops.write_iosf_sb_reg(hw,
				IXGBE_KRM_RX_TRN_LINKUP_CTRL(hw->bus.lan_id),
				IXGBE_SB_IOSF_TARGET_KR_PHY, reg_val);
	if (status != IXGBE_SUCCESS)
		return status;

	/* Disable Flex from training TXFFE. */
	status = mac->ops.read_iosf_sb_reg(hw,
				IXGBE_KRM_DSP_TXFFE_STATE_4(hw->bus.lan_id),
				IXGBE_SB_IOSF_TARGET_KR_PHY, &reg_val);
	if (status != IXGBE_SUCCESS)
		return status;
	reg_val &= ~IXGBE_KRM_DSP_TXFFE_STATE_C0_EN;
	reg_val &= ~IXGBE_KRM_DSP_TXFFE_STATE_CP1_CN1_EN;
	reg_val &= ~IXGBE_KRM_DSP_TXFFE_STATE_CO_ADAPT_EN;
	status = mac->ops.write_iosf_sb_reg(hw,
				IXGBE_KRM_DSP_TXFFE_STATE_4(hw->bus.lan_id),
				IXGBE_SB_IOSF_TARGET_KR_PHY, reg_val);
	if (status != IXGBE_SUCCESS)
		return status;
	status = mac->ops.read_iosf_sb_reg(hw,
				IXGBE_KRM_DSP_TXFFE_STATE_5(hw->bus.lan_id),
				IXGBE_SB_IOSF_TARGET_KR_PHY, &reg_val);
	if (status != IXGBE_SUCCESS)
		return status;
	reg_val &= ~IXGBE_KRM_DSP_TXFFE_STATE_C0_EN;
	reg_val &= ~IXGBE_KRM_DSP_TXFFE_STATE_CP1_CN1_EN;
	reg_val &= ~IXGBE_KRM_DSP_TXFFE_STATE_CO_ADAPT_EN;
	status = mac->ops.write_iosf_sb_reg(hw,
				IXGBE_KRM_DSP_TXFFE_STATE_5(hw->bus.lan_id),
				IXGBE_SB_IOSF_TARGET_KR_PHY, reg_val);
	if (status != IXGBE_SUCCESS)
		return status;

	/* Enable override for coefficients. */
	status = mac->ops.read_iosf_sb_reg(hw,
				IXGBE_KRM_TX_COEFF_CTRL_1(hw->bus.lan_id),
				IXGBE_SB_IOSF_TARGET_KR_PHY, &reg_val);
	if (status != IXGBE_SUCCESS)
		return status;
	reg_val |= IXGBE_KRM_TX_COEFF_CTRL_1_OVRRD_EN;
	reg_val |= IXGBE_KRM_TX_COEFF_CTRL_1_CZERO_EN;
	reg_val |= IXGBE_KRM_TX_COEFF_CTRL_1_CPLUS1_OVRRD_EN;
	reg_val |= IXGBE_KRM_TX_COEFF_CTRL_1_CMINUS1_OVRRD_EN;
	status = mac->ops.write_iosf_sb_reg(hw,
				IXGBE_KRM_TX_COEFF_CTRL_1(hw->bus.lan_id),
				IXGBE_SB_IOSF_TARGET_KR_PHY, reg_val);
	return status;
}

/**
 *  ixgbe_setup_ixfi_x550em - Configure the KR PHY for iXFI mode.
 *  @hw: pointer to hardware structure
 *  @speed: the link speed to force
 *
 *  Configures the integrated KR PHY to use iXFI mode. Used to connect an
 *  internal and external PHY at a specific speed, without autonegotiation.
 **/
static s32 ixgbe_setup_ixfi_x550em(struct ixgbe_hw *hw, ixgbe_link_speed *speed)
{
	struct ixgbe_mac_info *mac = &hw->mac;
	s32 status;
	u32 reg_val;

	/* iXFI is only supported with X552 */
	if (mac->type != ixgbe_mac_X550EM_x)
		return IXGBE_ERR_LINK_SETUP;

	/* Disable AN and force speed to 10G Serial. */
	status = mac->ops.read_iosf_sb_reg(hw,
					IXGBE_KRM_LINK_CTRL_1(hw->bus.lan_id),
					IXGBE_SB_IOSF_TARGET_KR_PHY, &reg_val);
	if (status != IXGBE_SUCCESS)
		return status;

	reg_val &= ~IXGBE_KRM_LINK_CTRL_1_TETH_AN_ENABLE;
	reg_val &= ~IXGBE_KRM_LINK_CTRL_1_TETH_FORCE_SPEED_MASK;

	/* Select forced link speed for internal PHY. */
	switch (*speed) {
	case IXGBE_LINK_SPEED_10GB_FULL:
		reg_val |= IXGBE_KRM_LINK_CTRL_1_TETH_FORCE_SPEED_10G;
		break;
	case IXGBE_LINK_SPEED_1GB_FULL:
		reg_val |= IXGBE_KRM_LINK_CTRL_1_TETH_FORCE_SPEED_1G;
		break;
	default:
		/* Other link speeds are not supported by internal KR PHY. */
		return IXGBE_ERR_LINK_SETUP;
	}

	status = mac->ops.write_iosf_sb_reg(hw,
					IXGBE_KRM_LINK_CTRL_1(hw->bus.lan_id),
					IXGBE_SB_IOSF_TARGET_KR_PHY, reg_val);
	if (status != IXGBE_SUCCESS)
		return status;

	/* Additional configuration needed for x550em_x */
	if (hw->mac.type == ixgbe_mac_X550EM_x) {
		status = ixgbe_setup_ixfi_x550em_x(hw);
		if (status != IXGBE_SUCCESS)
			return status;
	}

	/* Toggle port SW reset by AN reset. */
	status = ixgbe_restart_an_internal_phy_x550em(hw);

	return status;
}

/**
 * ixgbe_ext_phy_t_x550em_get_link - Get ext phy link status
 * @hw: address of hardware structure
 * @link_up: address of boolean to indicate link status
 *
 * Returns error code if unable to get link status.
 */
static s32 ixgbe_ext_phy_t_x550em_get_link(struct ixgbe_hw *hw, bool *link_up)
{
	u32 ret;
	u16 autoneg_status;

	*link_up = FALSE;

	/* read this twice back to back to indicate current status */
	ret = hw->phy.ops.read_reg(hw, IXGBE_MDIO_AUTO_NEG_STATUS,
				   IXGBE_MDIO_AUTO_NEG_DEV_TYPE,
				   &autoneg_status);
	if (ret != IXGBE_SUCCESS)
		return ret;

	ret = hw->phy.ops.read_reg(hw, IXGBE_MDIO_AUTO_NEG_STATUS,
				   IXGBE_MDIO_AUTO_NEG_DEV_TYPE,
				   &autoneg_status);
	if (ret != IXGBE_SUCCESS)
		return ret;

	*link_up = !!(autoneg_status & IXGBE_MDIO_AUTO_NEG_LINK_STATUS);

	return IXGBE_SUCCESS;
}

/**
 * ixgbe_setup_internal_phy_t_x550em - Configure KR PHY to X557 link
 * @hw: point to hardware structure
 *
 * Configures the link between the integrated KR PHY and the external X557 PHY
 * The driver will call this function when it gets a link status change
 * interrupt from the X557 PHY. This function configures the link speed
 * between the PHYs to match the link speed of the BASE-T link.
 *
 * A return of a non-zero value indicates an error, and the base driver should
 * not report link up.
 */
s32 ixgbe_setup_internal_phy_t_x550em(struct ixgbe_hw *hw)
{
	ixgbe_link_speed force_speed;
	bool link_up;
	u32 status;
	u16 speed;

	if (hw->mac.ops.get_media_type(hw) != ixgbe_media_type_copper)
		return IXGBE_ERR_CONFIG;

	if (hw->mac.type == ixgbe_mac_X550EM_x &&
	    !(hw->phy.nw_mng_if_sel & IXGBE_NW_MNG_IF_SEL_INT_PHY_MODE)) {
		/* If link is down, there is no setup necessary so return  */
		status = ixgbe_ext_phy_t_x550em_get_link(hw, &link_up);
		if (status != IXGBE_SUCCESS)
			return status;

		if (!link_up)
			return IXGBE_SUCCESS;

		status = hw->phy.ops.read_reg(hw,
					      IXGBE_MDIO_AUTO_NEG_VENDOR_STAT,
					      IXGBE_MDIO_AUTO_NEG_DEV_TYPE,
					      &speed);
		if (status != IXGBE_SUCCESS)
			return status;

		/* If link is still down - no setup is required so return */
		status = ixgbe_ext_phy_t_x550em_get_link(hw, &link_up);
		if (status != IXGBE_SUCCESS)
			return status;
		if (!link_up)
			return IXGBE_SUCCESS;

		/* clear everything but the speed and duplex bits */
		speed &= IXGBE_MDIO_AUTO_NEG_VENDOR_STATUS_MASK;

		switch (speed) {
		case IXGBE_MDIO_AUTO_NEG_VENDOR_STATUS_10GB_FULL:
			force_speed = IXGBE_LINK_SPEED_10GB_FULL;
			break;
		case IXGBE_MDIO_AUTO_NEG_VENDOR_STATUS_1GB_FULL:
			force_speed = IXGBE_LINK_SPEED_1GB_FULL;
			break;
		default:
			/* Internal PHY does not support anything else */
			return IXGBE_ERR_INVALID_LINK_SETTINGS;
		}

		return ixgbe_setup_ixfi_x550em(hw, &force_speed);
	} else {
		speed = IXGBE_LINK_SPEED_10GB_FULL |
			IXGBE_LINK_SPEED_1GB_FULL;
		return ixgbe_setup_kr_speed_x550em(hw, speed);
	}
}

/**
 *  ixgbe_setup_phy_loopback_x550em - Configure the KR PHY for loopback.
 *  @hw: pointer to hardware structure
 *
 *  Configures the integrated KR PHY to use internal loopback mode.
 **/
s32 ixgbe_setup_phy_loopback_x550em(struct ixgbe_hw *hw)
{
	s32 status;
	u32 reg_val;

	/* Disable AN and force speed to 10G Serial. */
	status = hw->mac.ops.read_iosf_sb_reg(hw,
					IXGBE_KRM_LINK_CTRL_1(hw->bus.lan_id),
					IXGBE_SB_IOSF_TARGET_KR_PHY, &reg_val);
	if (status != IXGBE_SUCCESS)
		return status;
	reg_val &= ~IXGBE_KRM_LINK_CTRL_1_TETH_AN_ENABLE;
	reg_val &= ~IXGBE_KRM_LINK_CTRL_1_TETH_FORCE_SPEED_MASK;
	reg_val |= IXGBE_KRM_LINK_CTRL_1_TETH_FORCE_SPEED_10G;
	status = hw->mac.ops.write_iosf_sb_reg(hw,
					IXGBE_KRM_LINK_CTRL_1(hw->bus.lan_id),
					IXGBE_SB_IOSF_TARGET_KR_PHY, reg_val);
	if (status != IXGBE_SUCCESS)
		return status;

	/* Set near-end loopback clocks. */
	status = hw->mac.ops.read_iosf_sb_reg(hw,
				IXGBE_KRM_PORT_CAR_GEN_CTRL(hw->bus.lan_id),
				IXGBE_SB_IOSF_TARGET_KR_PHY, &reg_val);
	if (status != IXGBE_SUCCESS)
		return status;
	reg_val |= IXGBE_KRM_PORT_CAR_GEN_CTRL_NELB_32B;
	reg_val |= IXGBE_KRM_PORT_CAR_GEN_CTRL_NELB_KRPCS;
	status = hw->mac.ops.write_iosf_sb_reg(hw,
				IXGBE_KRM_PORT_CAR_GEN_CTRL(hw->bus.lan_id),
				IXGBE_SB_IOSF_TARGET_KR_PHY, reg_val);
	if (status != IXGBE_SUCCESS)
		return status;

	/* Set loopback enable. */
	status = hw->mac.ops.read_iosf_sb_reg(hw,
				IXGBE_KRM_PMD_DFX_BURNIN(hw->bus.lan_id),
				IXGBE_SB_IOSF_TARGET_KR_PHY, &reg_val);
	if (status != IXGBE_SUCCESS)
		return status;
	reg_val |= IXGBE_KRM_PMD_DFX_BURNIN_TX_RX_KR_LB_MASK;
	status = hw->mac.ops.write_iosf_sb_reg(hw,
				IXGBE_KRM_PMD_DFX_BURNIN(hw->bus.lan_id),
				IXGBE_SB_IOSF_TARGET_KR_PHY, reg_val);
	if (status != IXGBE_SUCCESS)
		return status;

	/* Training bypass. */
	status = hw->mac.ops.read_iosf_sb_reg(hw,
				IXGBE_KRM_RX_TRN_LINKUP_CTRL(hw->bus.lan_id),
				IXGBE_SB_IOSF_TARGET_KR_PHY, &reg_val);
	if (status != IXGBE_SUCCESS)
		return status;
	reg_val |= IXGBE_KRM_RX_TRN_LINKUP_CTRL_PROTOCOL_BYPASS;
	status = hw->mac.ops.write_iosf_sb_reg(hw,
				IXGBE_KRM_RX_TRN_LINKUP_CTRL(hw->bus.lan_id),
				IXGBE_SB_IOSF_TARGET_KR_PHY, reg_val);

	return status;
}

/**
 *  ixgbe_read_ee_hostif_X550 - Read EEPROM word using a host interface command
 *  assuming that the semaphore is already obtained.
 *  @hw: pointer to hardware structure
 *  @offset: offset of  word in the EEPROM to read
 *  @data: word read from the EEPROM
 *
 *  Reads a 16 bit word from the EEPROM using the hostif.
 **/
s32 ixgbe_read_ee_hostif_X550(struct ixgbe_hw *hw, u16 offset, u16 *data)
{
	const u32 mask = IXGBE_GSSR_SW_MNG_SM | IXGBE_GSSR_EEP_SM;
	struct ixgbe_hic_read_shadow_ram buffer;
	s32 status;

	DEBUGFUNC("ixgbe_read_ee_hostif_X550");
	buffer.hdr.req.cmd = FW_READ_SHADOW_RAM_CMD;
	buffer.hdr.req.buf_lenh = 0;
	buffer.hdr.req.buf_lenl = FW_READ_SHADOW_RAM_LEN;
	buffer.hdr.req.checksum = FW_DEFAULT_CHECKSUM;

	/* convert offset from words to bytes */
	buffer.address = IXGBE_CPU_TO_BE32(offset * 2);
	/* one word */
	buffer.length = IXGBE_CPU_TO_BE16(sizeof(u16));
	buffer.pad2 = 0;
	buffer.pad3 = 0;

	status = hw->mac.ops.acquire_swfw_sync(hw, mask);
	if (status)
		return status;

	status = ixgbe_hic_unlocked(hw, (u32 *)&buffer, sizeof(buffer),
				    IXGBE_HI_COMMAND_TIMEOUT);
	if (!status) {
		*data = (u16)IXGBE_READ_REG_ARRAY(hw, IXGBE_FLEX_MNG,
						  FW_NVM_DATA_OFFSET);
	}

	hw->mac.ops.release_swfw_sync(hw, mask);
	return status;
}

/**
 *  ixgbe_read_ee_hostif_buffer_X550- Read EEPROM word(s) using hostif
 *  @hw: pointer to hardware structure
 *  @offset: offset of  word in the EEPROM to read
 *  @words: number of words
 *  @data: word(s) read from the EEPROM
 *
 *  Reads a 16 bit word(s) from the EEPROM using the hostif.
 **/
s32 ixgbe_read_ee_hostif_buffer_X550(struct ixgbe_hw *hw,
				     u16 offset, u16 words, u16 *data)
{
	const u32 mask = IXGBE_GSSR_SW_MNG_SM | IXGBE_GSSR_EEP_SM;
	struct ixgbe_hic_read_shadow_ram buffer;
	u32 current_word = 0;
	u16 words_to_read;
	s32 status;
	u32 i;

	DEBUGFUNC("ixgbe_read_ee_hostif_buffer_X550");

	/* Take semaphore for the entire operation. */
	status = hw->mac.ops.acquire_swfw_sync(hw, mask);
	if (status) {
		DEBUGOUT("EEPROM read buffer - semaphore failed\n");
		return status;
	}

	while (words) {
		if (words > FW_MAX_READ_BUFFER_SIZE / 2)
			words_to_read = FW_MAX_READ_BUFFER_SIZE / 2;
		else
			words_to_read = words;

		buffer.hdr.req.cmd = FW_READ_SHADOW_RAM_CMD;
		buffer.hdr.req.buf_lenh = 0;
		buffer.hdr.req.buf_lenl = FW_READ_SHADOW_RAM_LEN;
		buffer.hdr.req.checksum = FW_DEFAULT_CHECKSUM;

		/* convert offset from words to bytes */
		buffer.address = IXGBE_CPU_TO_BE32((offset + current_word) * 2);
		buffer.length = IXGBE_CPU_TO_BE16(words_to_read * 2);
		buffer.pad2 = 0;
		buffer.pad3 = 0;

		status = ixgbe_hic_unlocked(hw, (u32 *)&buffer, sizeof(buffer),
					    IXGBE_HI_COMMAND_TIMEOUT);

		if (status) {
			DEBUGOUT("Host interface command failed\n");
			goto out;
		}

		for (i = 0; i < words_to_read; i++) {
			u32 reg = IXGBE_FLEX_MNG + (FW_NVM_DATA_OFFSET << 2) +
				  2 * i;
			u32 value = IXGBE_READ_REG(hw, reg);

			data[current_word] = (u16)(value & 0xffff);
			current_word++;
			i++;
			if (i < words_to_read) {
				value >>= 16;
				data[current_word] = (u16)(value & 0xffff);
				current_word++;
			}
		}
		words -= words_to_read;
	}

out:
	hw->mac.ops.release_swfw_sync(hw, mask);
	return status;
}

/**
 *  ixgbe_write_ee_hostif_X550 - Write EEPROM word using hostif
 *  @hw: pointer to hardware structure
 *  @offset: offset of  word in the EEPROM to write
 *  @data: word write to the EEPROM
 *
 *  Write a 16 bit word to the EEPROM using the hostif.
 **/
s32 ixgbe_write_ee_hostif_data_X550(struct ixgbe_hw *hw, u16 offset,
				    u16 data)
{
	s32 status;
	struct ixgbe_hic_write_shadow_ram buffer;

	DEBUGFUNC("ixgbe_write_ee_hostif_data_X550");

	buffer.hdr.req.cmd = FW_WRITE_SHADOW_RAM_CMD;
	buffer.hdr.req.buf_lenh = 0;
	buffer.hdr.req.buf_lenl = FW_WRITE_SHADOW_RAM_LEN;
	buffer.hdr.req.checksum = FW_DEFAULT_CHECKSUM;

	 /* one word */
	buffer.length = IXGBE_CPU_TO_BE16(sizeof(u16));
	buffer.data = data;
	buffer.address = IXGBE_CPU_TO_BE32(offset * 2);

	status = ixgbe_host_interface_command(hw, (u32 *)&buffer,
					      sizeof(buffer),
					      IXGBE_HI_COMMAND_TIMEOUT, FALSE);

	return status;
}

/**
 *  ixgbe_write_ee_hostif_X550 - Write EEPROM word using hostif
 *  @hw: pointer to hardware structure
 *  @offset: offset of  word in the EEPROM to write
 *  @data: word write to the EEPROM
 *
 *  Write a 16 bit word to the EEPROM using the hostif.
 **/
s32 ixgbe_write_ee_hostif_X550(struct ixgbe_hw *hw, u16 offset,
			       u16 data)
{
	s32 status = IXGBE_SUCCESS;

	DEBUGFUNC("ixgbe_write_ee_hostif_X550");

	if (hw->mac.ops.acquire_swfw_sync(hw, IXGBE_GSSR_EEP_SM) ==
	    IXGBE_SUCCESS) {
		status = ixgbe_write_ee_hostif_data_X550(hw, offset, data);
		hw->mac.ops.release_swfw_sync(hw, IXGBE_GSSR_EEP_SM);
	} else {
		DEBUGOUT("write ee hostif failed to get semaphore");
		status = IXGBE_ERR_SWFW_SYNC;
	}

	return status;
}

/**
 *  ixgbe_write_ee_hostif_buffer_X550 - Write EEPROM word(s) using hostif
 *  @hw: pointer to hardware structure
 *  @offset: offset of  word in the EEPROM to write
 *  @words: number of words
 *  @data: word(s) write to the EEPROM
 *
 *  Write a 16 bit word(s) to the EEPROM using the hostif.
 **/
s32 ixgbe_write_ee_hostif_buffer_X550(struct ixgbe_hw *hw,
				      u16 offset, u16 words, u16 *data)
{
	s32 status = IXGBE_SUCCESS;
	u32 i = 0;

	DEBUGFUNC("ixgbe_write_ee_hostif_buffer_X550");

	/* Take semaphore for the entire operation. */
	status = hw->mac.ops.acquire_swfw_sync(hw, IXGBE_GSSR_EEP_SM);
	if (status != IXGBE_SUCCESS) {
		DEBUGOUT("EEPROM write buffer - semaphore failed\n");
		goto out;
	}

	for (i = 0; i < words; i++) {
		status = ixgbe_write_ee_hostif_data_X550(hw, offset + i,
							 data[i]);

		if (status != IXGBE_SUCCESS) {
			DEBUGOUT("Eeprom buffered write failed\n");
			break;
		}
	}

	hw->mac.ops.release_swfw_sync(hw, IXGBE_GSSR_EEP_SM);
out:

	return status;
}

/**
 * ixgbe_checksum_ptr_x550 - Checksum one pointer region
 * @hw: pointer to hardware structure
 * @ptr: pointer offset in eeprom
 * @size: size of section pointed by ptr, if 0 first word will be used as size
 * @csum: address of checksum to update
 * @buffer: pointer to buffer containing calculated checksum
 * @buffer_size: size of buffer
 *
 * Returns error status for any failure
 */
static s32 ixgbe_checksum_ptr_x550(struct ixgbe_hw *hw, u16 ptr,
				   u16 size, u16 *csum, u16 *buffer,
				   u32 buffer_size)
{
	u16 buf[256];
	s32 status;
	u16 length, bufsz, i, start;
	u16 *local_buffer;

	bufsz = sizeof(buf) / sizeof(buf[0]);

	/* Read a chunk at the pointer location */
	if (!buffer) {
		status = ixgbe_read_ee_hostif_buffer_X550(hw, ptr, bufsz, buf);
		if (status) {
			DEBUGOUT("Failed to read EEPROM image\n");
			return status;
		}
		local_buffer = buf;
	} else {
		if (buffer_size < ptr)
			return  IXGBE_ERR_PARAM;
		local_buffer = &buffer[ptr];
	}

	if (size) {
		start = 0;
		length = size;
	} else {
		start = 1;
		length = local_buffer[0];

		/* Skip pointer section if length is invalid. */
		if (length == 0xFFFF || length == 0 ||
		    (ptr + length) >= hw->eeprom.word_size)
			return IXGBE_SUCCESS;
	}

	if (buffer && ((u32)start + (u32)length > buffer_size))
		return IXGBE_ERR_PARAM;

	for (i = start; length; i++, length--) {
		if (i == bufsz && !buffer) {
			ptr += bufsz;
			i = 0;
			if (length < bufsz)
				bufsz = length;

			/* Read a chunk at the pointer location */
			status = ixgbe_read_ee_hostif_buffer_X550(hw, ptr,
								  bufsz, buf);
			if (status) {
				DEBUGOUT("Failed to read EEPROM image\n");
				return status;
			}
		}
		*csum += local_buffer[i];
	}
	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_calc_checksum_X550 - Calculates and returns the checksum
 *  @hw: pointer to hardware structure
 *  @buffer: pointer to buffer containing calculated checksum
 *  @buffer_size: size of buffer
 *
 *  Returns a negative error code on error, or the 16-bit checksum
 **/
s32 ixgbe_calc_checksum_X550(struct ixgbe_hw *hw, u16 *buffer, u32 buffer_size)
{
	u16 eeprom_ptrs[IXGBE_EEPROM_LAST_WORD + 1];
	u16 *local_buffer;
	s32 status;
	u16 checksum = 0;
	u16 pointer, i, size;

	DEBUGFUNC("ixgbe_calc_eeprom_checksum_X550");

	hw->eeprom.ops.init_params(hw);

	if (!buffer) {
		/* Read pointer area */
		status = ixgbe_read_ee_hostif_buffer_X550(hw, 0,
						     IXGBE_EEPROM_LAST_WORD + 1,
						     eeprom_ptrs);
		if (status) {
			DEBUGOUT("Failed to read EEPROM image\n");
			return status;
		}
		local_buffer = eeprom_ptrs;
	} else {
		if (buffer_size < IXGBE_EEPROM_LAST_WORD)
			return IXGBE_ERR_PARAM;
		local_buffer = buffer;
	}

	/*
	 * For X550 hardware include 0x0-0x41 in the checksum, skip the
	 * checksum word itself
	 */
	for (i = 0; i <= IXGBE_EEPROM_LAST_WORD; i++)
		if (i != IXGBE_EEPROM_CHECKSUM)
			checksum += local_buffer[i];

	/*
	 * Include all data from pointers 0x3, 0x6-0xE.  This excludes the
	 * FW, PHY module, and PCIe Expansion/Option ROM pointers.
	 */
	for (i = IXGBE_PCIE_ANALOG_PTR_X550; i < IXGBE_FW_PTR; i++) {
		if (i == IXGBE_PHY_PTR || i == IXGBE_OPTION_ROM_PTR)
			continue;

		pointer = local_buffer[i];

		/* Skip pointer section if the pointer is invalid. */
		if (pointer == 0xFFFF || pointer == 0 ||
		    pointer >= hw->eeprom.word_size)
			continue;

		switch (i) {
		case IXGBE_PCIE_GENERAL_PTR:
			size = IXGBE_IXGBE_PCIE_GENERAL_SIZE;
			break;
		case IXGBE_PCIE_CONFIG0_PTR:
		case IXGBE_PCIE_CONFIG1_PTR:
			size = IXGBE_PCIE_CONFIG_SIZE;
			break;
		default:
			size = 0;
			break;
		}

		status = ixgbe_checksum_ptr_x550(hw, pointer, size, &checksum,
						buffer, buffer_size);
		if (status)
			return status;
	}

	checksum = (u16)IXGBE_EEPROM_SUM - checksum;

	return (s32)checksum;
}

/**
 *  ixgbe_calc_eeprom_checksum_X550 - Calculates and returns the checksum
 *  @hw: pointer to hardware structure
 *
 *  Returns a negative error code on error, or the 16-bit checksum
 **/
s32 ixgbe_calc_eeprom_checksum_X550(struct ixgbe_hw *hw)
{
	return ixgbe_calc_checksum_X550(hw, NULL, 0);
}

/**
 *  ixgbe_validate_eeprom_checksum_X550 - Validate EEPROM checksum
 *  @hw: pointer to hardware structure
 *  @checksum_val: calculated checksum
 *
 *  Performs checksum calculation and validates the EEPROM checksum.  If the
 *  caller does not need checksum_val, the value can be NULL.
 **/
s32 ixgbe_validate_eeprom_checksum_X550(struct ixgbe_hw *hw, u16 *checksum_val)
{
	s32 status;
	u16 checksum;
	u16 read_checksum = 0;

	DEBUGFUNC("ixgbe_validate_eeprom_checksum_X550");

	/* Read the first word from the EEPROM. If this times out or fails, do
	 * not continue or we could be in for a very long wait while every
	 * EEPROM read fails
	 */
	status = hw->eeprom.ops.read(hw, 0, &checksum);
	if (status) {
		DEBUGOUT("EEPROM read failed\n");
		return status;
	}

	status = hw->eeprom.ops.calc_checksum(hw);
	if (status < 0)
		return status;

	checksum = (u16)(status & 0xffff);

	status = ixgbe_read_ee_hostif_X550(hw, IXGBE_EEPROM_CHECKSUM,
					   &read_checksum);
	if (status)
		return status;

	/* Verify read checksum from EEPROM is the same as
	 * calculated checksum
	 */
	if (read_checksum != checksum) {
		status = IXGBE_ERR_EEPROM_CHECKSUM;
		ERROR_REPORT1(IXGBE_ERROR_INVALID_STATE,
			     "Invalid EEPROM checksum");
	}

	/* If the user cares, return the calculated checksum */
	if (checksum_val)
		*checksum_val = checksum;

	return status;
}

/**
 * ixgbe_update_eeprom_checksum_X550 - Updates the EEPROM checksum and flash
 * @hw: pointer to hardware structure
 *
 * After writing EEPROM to shadow RAM using EEWR register, software calculates
 * checksum and updates the EEPROM and instructs the hardware to update
 * the flash.
 **/
s32 ixgbe_update_eeprom_checksum_X550(struct ixgbe_hw *hw)
{
	s32 status;
	u16 checksum = 0;

	DEBUGFUNC("ixgbe_update_eeprom_checksum_X550");

	/* Read the first word from the EEPROM. If this times out or fails, do
	 * not continue or we could be in for a very long wait while every
	 * EEPROM read fails
	 */
	status = ixgbe_read_ee_hostif_X550(hw, 0, &checksum);
	if (status) {
		DEBUGOUT("EEPROM read failed\n");
		return status;
	}

	status = ixgbe_calc_eeprom_checksum_X550(hw);
	if (status < 0)
		return status;

	checksum = (u16)(status & 0xffff);

	status = ixgbe_write_ee_hostif_X550(hw, IXGBE_EEPROM_CHECKSUM,
					    checksum);
	if (status)
		return status;

	status = ixgbe_update_flash_X550(hw);

	return status;
}

/**
 *  ixgbe_update_flash_X550 - Instruct HW to copy EEPROM to Flash device
 *  @hw: pointer to hardware structure
 *
 *  Issue a shadow RAM dump to FW to copy EEPROM from shadow RAM to the flash.
 **/
s32 ixgbe_update_flash_X550(struct ixgbe_hw *hw)
{
	s32 status = IXGBE_SUCCESS;
	union ixgbe_hic_hdr2 buffer;

	DEBUGFUNC("ixgbe_update_flash_X550");

	buffer.req.cmd = FW_SHADOW_RAM_DUMP_CMD;
	buffer.req.buf_lenh = 0;
	buffer.req.buf_lenl = FW_SHADOW_RAM_DUMP_LEN;
	buffer.req.checksum = FW_DEFAULT_CHECKSUM;

	status = ixgbe_host_interface_command(hw, (u32 *)&buffer,
					      sizeof(buffer),
					      IXGBE_HI_COMMAND_TIMEOUT, FALSE);

	return status;
}

/**
 *  ixgbe_get_supported_physical_layer_X550em - Returns physical layer type
 *  @hw: pointer to hardware structure
 *
 *  Determines physical layer capabilities of the current configuration.
 **/
u64 ixgbe_get_supported_physical_layer_X550em(struct ixgbe_hw *hw)
{
	u64 physical_layer = IXGBE_PHYSICAL_LAYER_UNKNOWN;
	u16 ext_ability = 0;

	DEBUGFUNC("ixgbe_get_supported_physical_layer_X550em");

	hw->phy.ops.identify(hw);

	switch (hw->phy.type) {
	case ixgbe_phy_x550em_kr:
		if (hw->mac.type == ixgbe_mac_X550EM_a) {
			if (hw->phy.nw_mng_if_sel &
			    IXGBE_NW_MNG_IF_SEL_PHY_SPEED_2_5G) {
				physical_layer =
					IXGBE_PHYSICAL_LAYER_2500BASE_KX;
				break;
			} else if (hw->device_id ==
				   IXGBE_DEV_ID_X550EM_A_KR_L) {
				physical_layer =
					IXGBE_PHYSICAL_LAYER_1000BASE_KX;
				break;
			}
		}
		/* fall through */
	case ixgbe_phy_x550em_xfi:
		physical_layer = IXGBE_PHYSICAL_LAYER_10GBASE_KR |
				 IXGBE_PHYSICAL_LAYER_1000BASE_KX;
		break;
	case ixgbe_phy_x550em_kx4:
		physical_layer = IXGBE_PHYSICAL_LAYER_10GBASE_KX4 |
				 IXGBE_PHYSICAL_LAYER_1000BASE_KX;
		break;
	case ixgbe_phy_x550em_ext_t:
		hw->phy.ops.read_reg(hw, IXGBE_MDIO_PHY_EXT_ABILITY,
				     IXGBE_MDIO_PMA_PMD_DEV_TYPE,
				     &ext_ability);
		if (ext_ability & IXGBE_MDIO_PHY_10GBASET_ABILITY)
			physical_layer |= IXGBE_PHYSICAL_LAYER_10GBASE_T;
		if (ext_ability & IXGBE_MDIO_PHY_1000BASET_ABILITY)
			physical_layer |= IXGBE_PHYSICAL_LAYER_1000BASE_T;
		break;
	case ixgbe_phy_fw:
		if (hw->phy.speeds_supported & IXGBE_LINK_SPEED_1GB_FULL)
			physical_layer |= IXGBE_PHYSICAL_LAYER_1000BASE_T;
		if (hw->phy.speeds_supported & IXGBE_LINK_SPEED_100_FULL)
			physical_layer |= IXGBE_PHYSICAL_LAYER_100BASE_TX;
		if (hw->phy.speeds_supported & IXGBE_LINK_SPEED_10_FULL)
			physical_layer |= IXGBE_PHYSICAL_LAYER_10BASE_T;
		break;
	case ixgbe_phy_sgmii:
		physical_layer = IXGBE_PHYSICAL_LAYER_1000BASE_KX;
		break;
	case ixgbe_phy_ext_1g_t:
		physical_layer = IXGBE_PHYSICAL_LAYER_1000BASE_T;
		break;
	default:
		break;
	}

	if (hw->mac.ops.get_media_type(hw) == ixgbe_media_type_fiber)
		physical_layer = ixgbe_get_supported_phy_sfp_layer_generic(hw);

	return physical_layer;
}

/**
 * ixgbe_get_bus_info_x550em - Set PCI bus info
 * @hw: pointer to hardware structure
 *
 * Sets bus link width and speed to unknown because X550em is
 * not a PCI device.
 **/
s32 ixgbe_get_bus_info_X550em(struct ixgbe_hw *hw)
{

	DEBUGFUNC("ixgbe_get_bus_info_x550em");

	hw->bus.width = ixgbe_bus_width_unknown;
	hw->bus.speed = ixgbe_bus_speed_unknown;

	hw->mac.ops.set_lan_id(hw);

	return IXGBE_SUCCESS;
}

/**
 * ixgbe_disable_rx_x550 - Disable RX unit
 * @hw: pointer to hardware structure
 *
 * Enables the Rx DMA unit for x550
 **/
void ixgbe_disable_rx_x550(struct ixgbe_hw *hw)
{
	u32 rxctrl, pfdtxgswc;
	s32 status;
	struct ixgbe_hic_disable_rxen fw_cmd;

	DEBUGFUNC("ixgbe_enable_rx_dma_x550");

	rxctrl = IXGBE_READ_REG(hw, IXGBE_RXCTRL);
	if (rxctrl & IXGBE_RXCTRL_RXEN) {
		pfdtxgswc = IXGBE_READ_REG(hw, IXGBE_PFDTXGSWC);
		if (pfdtxgswc & IXGBE_PFDTXGSWC_VT_LBEN) {
			pfdtxgswc &= ~IXGBE_PFDTXGSWC_VT_LBEN;
			IXGBE_WRITE_REG(hw, IXGBE_PFDTXGSWC, pfdtxgswc);
			hw->mac.set_lben = TRUE;
		} else {
			hw->mac.set_lben = FALSE;
		}

		fw_cmd.hdr.cmd = FW_DISABLE_RXEN_CMD;
		fw_cmd.hdr.buf_len = FW_DISABLE_RXEN_LEN;
		fw_cmd.hdr.checksum = FW_DEFAULT_CHECKSUM;
		fw_cmd.port_number = (u8)hw->bus.lan_id;

		status = ixgbe_host_interface_command(hw, (u32 *)&fw_cmd,
					sizeof(struct ixgbe_hic_disable_rxen),
					IXGBE_HI_COMMAND_TIMEOUT, TRUE);

		/* If we fail - disable RX using register write */
		if (status) {
			rxctrl = IXGBE_READ_REG(hw, IXGBE_RXCTRL);
			if (rxctrl & IXGBE_RXCTRL_RXEN) {
				rxctrl &= ~IXGBE_RXCTRL_RXEN;
				IXGBE_WRITE_REG(hw, IXGBE_RXCTRL, rxctrl);
			}
		}
	}
}

/**
 * ixgbe_enter_lplu_x550em - Transition to low power states
 *  @hw: pointer to hardware structure
 *
 * Configures Low Power Link Up on transition to low power states
 * (from D0 to non-D0). Link is required to enter LPLU so avoid resetting the
 * X557 PHY immediately prior to entering LPLU.
 **/
s32 ixgbe_enter_lplu_t_x550em(struct ixgbe_hw *hw)
{
	u16 an_10g_cntl_reg, autoneg_reg, speed;
	s32 status;
	ixgbe_link_speed lcd_speed;
	u32 save_autoneg;
	bool link_up;

	/* SW LPLU not required on later HW revisions. */
	if ((hw->mac.type == ixgbe_mac_X550EM_x) &&
	    (IXGBE_FUSES0_REV_MASK &
	     IXGBE_READ_REG(hw, IXGBE_FUSES0_GROUP(0))))
		return IXGBE_SUCCESS;

	/* If blocked by MNG FW, then don't restart AN */
	if (ixgbe_check_reset_blocked(hw))
		return IXGBE_SUCCESS;

	status = ixgbe_ext_phy_t_x550em_get_link(hw, &link_up);
	if (status != IXGBE_SUCCESS)
		return status;

	status = ixgbe_read_eeprom(hw, NVM_INIT_CTRL_3, &hw->eeprom.ctrl_word_3);

	if (status != IXGBE_SUCCESS)
		return status;

	/* If link is down, LPLU disabled in NVM, WoL disabled, or manageability
	 * disabled, then force link down by entering low power mode.
	 */
	if (!link_up || !(hw->eeprom.ctrl_word_3 & NVM_INIT_CTRL_3_LPLU) ||
	    !(hw->wol_enabled || ixgbe_mng_present(hw)))
		return ixgbe_set_copper_phy_power(hw, FALSE);

	/* Determine LCD */
	status = ixgbe_get_lcd_t_x550em(hw, &lcd_speed);

	if (status != IXGBE_SUCCESS)
		return status;

	/* If no valid LCD link speed, then force link down and exit. */
	if (lcd_speed == IXGBE_LINK_SPEED_UNKNOWN)
		return ixgbe_set_copper_phy_power(hw, FALSE);

	status = hw->phy.ops.read_reg(hw, IXGBE_MDIO_AUTO_NEG_VENDOR_STAT,
				      IXGBE_MDIO_AUTO_NEG_DEV_TYPE,
				      &speed);

	if (status != IXGBE_SUCCESS)
		return status;

	/* If no link now, speed is invalid so take link down */
	status = ixgbe_ext_phy_t_x550em_get_link(hw, &link_up);
	if (status != IXGBE_SUCCESS)
		return ixgbe_set_copper_phy_power(hw, FALSE);

	/* clear everything but the speed bits */
	speed &= IXGBE_MDIO_AUTO_NEG_VEN_STAT_SPEED_MASK;

	/* If current speed is already LCD, then exit. */
	if (((speed == IXGBE_MDIO_AUTO_NEG_VENDOR_STATUS_1GB) &&
	     (lcd_speed == IXGBE_LINK_SPEED_1GB_FULL)) ||
	    ((speed == IXGBE_MDIO_AUTO_NEG_VENDOR_STATUS_10GB) &&
	     (lcd_speed == IXGBE_LINK_SPEED_10GB_FULL)))
		return status;

	/* Clear AN completed indication */
	status = hw->phy.ops.read_reg(hw, IXGBE_MDIO_AUTO_NEG_VENDOR_TX_ALARM,
				      IXGBE_MDIO_AUTO_NEG_DEV_TYPE,
				      &autoneg_reg);

	if (status != IXGBE_SUCCESS)
		return status;

	status = hw->phy.ops.read_reg(hw, IXGBE_MII_10GBASE_T_AUTONEG_CTRL_REG,
			     IXGBE_MDIO_AUTO_NEG_DEV_TYPE,
			     &an_10g_cntl_reg);

	if (status != IXGBE_SUCCESS)
		return status;

	status = hw->phy.ops.read_reg(hw,
			     IXGBE_MII_AUTONEG_VENDOR_PROVISION_1_REG,
			     IXGBE_MDIO_AUTO_NEG_DEV_TYPE,
			     &autoneg_reg);

	if (status != IXGBE_SUCCESS)
		return status;

	save_autoneg = hw->phy.autoneg_advertised;

	/* Setup link at least common link speed */
	status = hw->mac.ops.setup_link(hw, lcd_speed, FALSE);

	/* restore autoneg from before setting lplu speed */
	hw->phy.autoneg_advertised = save_autoneg;

	return status;
}

/**
 * ixgbe_get_lcd_x550em - Determine lowest common denominator
 *  @hw: pointer to hardware structure
 *  @lcd_speed: pointer to lowest common link speed
 *
 * Determine lowest common link speed with link partner.
 **/
s32 ixgbe_get_lcd_t_x550em(struct ixgbe_hw *hw, ixgbe_link_speed *lcd_speed)
{
	u16 an_lp_status;
	s32 status;
	u16 word = hw->eeprom.ctrl_word_3;

	*lcd_speed = IXGBE_LINK_SPEED_UNKNOWN;

	status = hw->phy.ops.read_reg(hw, IXGBE_AUTO_NEG_LP_STATUS,
				      IXGBE_MDIO_AUTO_NEG_DEV_TYPE,
				      &an_lp_status);

	if (status != IXGBE_SUCCESS)
		return status;

	/* If link partner advertised 1G, return 1G */
	if (an_lp_status & IXGBE_AUTO_NEG_LP_1000BASE_CAP) {
		*lcd_speed = IXGBE_LINK_SPEED_1GB_FULL;
		return status;
	}

	/* If 10G disabled for LPLU via NVM D10GMP, then return no valid LCD */
	if ((hw->bus.lan_id && (word & NVM_INIT_CTRL_3_D10GMP_PORT1)) ||
	    (word & NVM_INIT_CTRL_3_D10GMP_PORT0))
		return status;

	/* Link partner not capable of lower speeds, return 10G */
	*lcd_speed = IXGBE_LINK_SPEED_10GB_FULL;
	return status;
}

/**
 *  ixgbe_setup_fc_X550em - Set up flow control
 *  @hw: pointer to hardware structure
 *
 *  Called at init time to set up flow control.
 **/
s32 ixgbe_setup_fc_X550em(struct ixgbe_hw *hw)
{
	s32 ret_val = IXGBE_SUCCESS;
	u32 pause, asm_dir, reg_val;

	DEBUGFUNC("ixgbe_setup_fc_X550em");

	/* Validate the requested mode */
	if (hw->fc.strict_ieee && hw->fc.requested_mode == ixgbe_fc_rx_pause) {
		ERROR_REPORT1(IXGBE_ERROR_UNSUPPORTED,
			"ixgbe_fc_rx_pause not valid in strict IEEE mode\n");
		ret_val = IXGBE_ERR_INVALID_LINK_SETTINGS;
		goto out;
	}

	/* 10gig parts do not have a word in the EEPROM to determine the
	 * default flow control setting, so we explicitly set it to full.
	 */
	if (hw->fc.requested_mode == ixgbe_fc_default)
		hw->fc.requested_mode = ixgbe_fc_full;

	/* Determine PAUSE and ASM_DIR bits. */
	switch (hw->fc.requested_mode) {
	case ixgbe_fc_none:
		pause = 0;
		asm_dir = 0;
		break;
	case ixgbe_fc_tx_pause:
		pause = 0;
		asm_dir = 1;
		break;
	case ixgbe_fc_rx_pause:
		/* Rx Flow control is enabled and Tx Flow control is
		 * disabled by software override. Since there really
		 * isn't a way to advertise that we are capable of RX
		 * Pause ONLY, we will advertise that we support both
		 * symmetric and asymmetric Rx PAUSE, as such we fall
		 * through to the fc_full statement.  Later, we will
		 * disable the adapter's ability to send PAUSE frames.
		 */
	case ixgbe_fc_full:
		pause = 1;
		asm_dir = 1;
		break;
	default:
		ERROR_REPORT1(IXGBE_ERROR_ARGUMENT,
			"Flow control param set incorrectly\n");
		ret_val = IXGBE_ERR_CONFIG;
		goto out;
	}

	switch (hw->device_id) {
	case IXGBE_DEV_ID_X550EM_X_KR:
	case IXGBE_DEV_ID_X550EM_A_KR:
	case IXGBE_DEV_ID_X550EM_A_KR_L:
		ret_val = hw->mac.ops.read_iosf_sb_reg(hw,
					IXGBE_KRM_AN_CNTL_1(hw->bus.lan_id),
					IXGBE_SB_IOSF_TARGET_KR_PHY, &reg_val);
		if (ret_val != IXGBE_SUCCESS)
			goto out;
		reg_val &= ~(IXGBE_KRM_AN_CNTL_1_SYM_PAUSE |
			IXGBE_KRM_AN_CNTL_1_ASM_PAUSE);
		if (pause)
			reg_val |= IXGBE_KRM_AN_CNTL_1_SYM_PAUSE;
		if (asm_dir)
			reg_val |= IXGBE_KRM_AN_CNTL_1_ASM_PAUSE;
		ret_val = hw->mac.ops.write_iosf_sb_reg(hw,
					IXGBE_KRM_AN_CNTL_1(hw->bus.lan_id),
					IXGBE_SB_IOSF_TARGET_KR_PHY, reg_val);

		/* This device does not fully support AN. */
		hw->fc.disable_fc_autoneg = TRUE;
		break;
	case IXGBE_DEV_ID_X550EM_X_XFI:
		hw->fc.disable_fc_autoneg = TRUE;
		break;
	default:
		break;
	}

out:
	return ret_val;
}

/**
 *  ixgbe_fc_autoneg_backplane_x550em_a - Enable flow control IEEE clause 37
 *  @hw: pointer to hardware structure
 *
 *  Enable flow control according to IEEE clause 37.
 **/
void ixgbe_fc_autoneg_backplane_x550em_a(struct ixgbe_hw *hw)
{
	u32 link_s1, lp_an_page_low, an_cntl_1;
	s32 status = IXGBE_ERR_FC_NOT_NEGOTIATED;
	ixgbe_link_speed speed;
	bool link_up;

	/* AN should have completed when the cable was plugged in.
	 * Look for reasons to bail out.  Bail out if:
	 * - FC autoneg is disabled, or if
	 * - link is not up.
	 */
	if (hw->fc.disable_fc_autoneg) {
		ERROR_REPORT1(IXGBE_ERROR_UNSUPPORTED,
			     "Flow control autoneg is disabled");
		goto out;
	}

	hw->mac.ops.check_link(hw, &speed, &link_up, FALSE);
	if (!link_up) {
		ERROR_REPORT1(IXGBE_ERROR_SOFTWARE, "The link is down");
		goto out;
	}

	/* Check at auto-negotiation has completed */
	status = hw->mac.ops.read_iosf_sb_reg(hw,
					IXGBE_KRM_LINK_S1(hw->bus.lan_id),
					IXGBE_SB_IOSF_TARGET_KR_PHY, &link_s1);

	if (status != IXGBE_SUCCESS ||
	    (link_s1 & IXGBE_KRM_LINK_S1_MAC_AN_COMPLETE) == 0) {
		DEBUGOUT("Auto-Negotiation did not complete\n");
		status = IXGBE_ERR_FC_NOT_NEGOTIATED;
		goto out;
	}

	/* Read the 10g AN autoc and LP ability registers and resolve
	 * local flow control settings accordingly
	 */
	status = hw->mac.ops.read_iosf_sb_reg(hw,
				IXGBE_KRM_AN_CNTL_1(hw->bus.lan_id),
				IXGBE_SB_IOSF_TARGET_KR_PHY, &an_cntl_1);

	if (status != IXGBE_SUCCESS) {
		DEBUGOUT("Auto-Negotiation did not complete\n");
		goto out;
	}

	status = hw->mac.ops.read_iosf_sb_reg(hw,
				IXGBE_KRM_LP_BASE_PAGE_HIGH(hw->bus.lan_id),
				IXGBE_SB_IOSF_TARGET_KR_PHY, &lp_an_page_low);

	if (status != IXGBE_SUCCESS) {
		DEBUGOUT("Auto-Negotiation did not complete\n");
		goto out;
	}

	status = ixgbe_negotiate_fc(hw, an_cntl_1, lp_an_page_low,
				    IXGBE_KRM_AN_CNTL_1_SYM_PAUSE,
				    IXGBE_KRM_AN_CNTL_1_ASM_PAUSE,
				    IXGBE_KRM_LP_BASE_PAGE_HIGH_SYM_PAUSE,
				    IXGBE_KRM_LP_BASE_PAGE_HIGH_ASM_PAUSE);

out:
	if (status == IXGBE_SUCCESS) {
		hw->fc.fc_was_autonegged = TRUE;
	} else {
		hw->fc.fc_was_autonegged = FALSE;
		hw->fc.current_mode = hw->fc.requested_mode;
	}
}

/**
 *  ixgbe_fc_autoneg_fiber_x550em_a - passthrough FC settings
 *  @hw: pointer to hardware structure
 *
 **/
void ixgbe_fc_autoneg_fiber_x550em_a(struct ixgbe_hw *hw)
{
	hw->fc.fc_was_autonegged = FALSE;
	hw->fc.current_mode = hw->fc.requested_mode;
}

/**
 *  ixgbe_fc_autoneg_sgmii_x550em_a - Enable flow control IEEE clause 37
 *  @hw: pointer to hardware structure
 *
 *  Enable flow control according to IEEE clause 37.
 **/
void ixgbe_fc_autoneg_sgmii_x550em_a(struct ixgbe_hw *hw)
{
	s32 status = IXGBE_ERR_FC_NOT_NEGOTIATED;
	u32 info[FW_PHY_ACT_DATA_COUNT] = { 0 };
	ixgbe_link_speed speed;
	bool link_up;

	/* AN should have completed when the cable was plugged in.
	 * Look for reasons to bail out.  Bail out if:
	 * - FC autoneg is disabled, or if
	 * - link is not up.
	 */
	if (hw->fc.disable_fc_autoneg) {
		ERROR_REPORT1(IXGBE_ERROR_UNSUPPORTED,
			     "Flow control autoneg is disabled");
		goto out;
	}

	hw->mac.ops.check_link(hw, &speed, &link_up, FALSE);
	if (!link_up) {
		ERROR_REPORT1(IXGBE_ERROR_SOFTWARE, "The link is down");
		goto out;
	}

	/* Check if auto-negotiation has completed */
	status = ixgbe_fw_phy_activity(hw, FW_PHY_ACT_GET_LINK_INFO, &info);
	if (status != IXGBE_SUCCESS ||
	    !(info[0] & FW_PHY_ACT_GET_LINK_INFO_AN_COMPLETE)) {
		DEBUGOUT("Auto-Negotiation did not complete\n");
		status = IXGBE_ERR_FC_NOT_NEGOTIATED;
		goto out;
	}

	/* Negotiate the flow control */
	status = ixgbe_negotiate_fc(hw, info[0], info[0],
				    FW_PHY_ACT_GET_LINK_INFO_FC_RX,
				    FW_PHY_ACT_GET_LINK_INFO_FC_TX,
				    FW_PHY_ACT_GET_LINK_INFO_LP_FC_RX,
				    FW_PHY_ACT_GET_LINK_INFO_LP_FC_TX);

out:
	if (status == IXGBE_SUCCESS) {
		hw->fc.fc_was_autonegged = TRUE;
	} else {
		hw->fc.fc_was_autonegged = FALSE;
		hw->fc.current_mode = hw->fc.requested_mode;
	}
}

/**
 *  ixgbe_setup_fc_backplane_x550em_a - Set up flow control
 *  @hw: pointer to hardware structure
 *
 *  Called at init time to set up flow control.
 **/
s32 ixgbe_setup_fc_backplane_x550em_a(struct ixgbe_hw *hw)
{
	s32 status = IXGBE_SUCCESS;
	u32 an_cntl = 0;

	DEBUGFUNC("ixgbe_setup_fc_backplane_x550em_a");

	/* Validate the requested mode */
	if (hw->fc.strict_ieee && hw->fc.requested_mode == ixgbe_fc_rx_pause) {
		ERROR_REPORT1(IXGBE_ERROR_UNSUPPORTED,
			      "ixgbe_fc_rx_pause not valid in strict IEEE mode\n");
		return IXGBE_ERR_INVALID_LINK_SETTINGS;
	}

	if (hw->fc.requested_mode == ixgbe_fc_default)
		hw->fc.requested_mode = ixgbe_fc_full;

	/* Set up the 1G and 10G flow control advertisement registers so the
	 * HW will be able to do FC autoneg once the cable is plugged in.  If
	 * we link at 10G, the 1G advertisement is harmless and vice versa.
	 */
	status = hw->mac.ops.read_iosf_sb_reg(hw,
					IXGBE_KRM_AN_CNTL_1(hw->bus.lan_id),
					IXGBE_SB_IOSF_TARGET_KR_PHY, &an_cntl);

	if (status != IXGBE_SUCCESS) {
		DEBUGOUT("Auto-Negotiation did not complete\n");
		return status;
	}

	/* The possible values of fc.requested_mode are:
	 * 0: Flow control is completely disabled
	 * 1: Rx flow control is enabled (we can receive pause frames,
	 *    but not send pause frames).
	 * 2: Tx flow control is enabled (we can send pause frames but
	 *    we do not support receiving pause frames).
	 * 3: Both Rx and Tx flow control (symmetric) are enabled.
	 * other: Invalid.
	 */
	switch (hw->fc.requested_mode) {
	case ixgbe_fc_none:
		/* Flow control completely disabled by software override. */
		an_cntl &= ~(IXGBE_KRM_AN_CNTL_1_SYM_PAUSE |
			     IXGBE_KRM_AN_CNTL_1_ASM_PAUSE);
		break;
	case ixgbe_fc_tx_pause:
		/* Tx Flow control is enabled, and Rx Flow control is
		 * disabled by software override.
		 */
		an_cntl |= IXGBE_KRM_AN_CNTL_1_ASM_PAUSE;
		an_cntl &= ~IXGBE_KRM_AN_CNTL_1_SYM_PAUSE;
		break;
	case ixgbe_fc_rx_pause:
		/* Rx Flow control is enabled and Tx Flow control is
		 * disabled by software override. Since there really
		 * isn't a way to advertise that we are capable of RX
		 * Pause ONLY, we will advertise that we support both
		 * symmetric and asymmetric Rx PAUSE, as such we fall
		 * through to the fc_full statement.  Later, we will
		 * disable the adapter's ability to send PAUSE frames.
		 */
	case ixgbe_fc_full:
		/* Flow control (both Rx and Tx) is enabled by SW override. */
		an_cntl |= IXGBE_KRM_AN_CNTL_1_SYM_PAUSE |
			   IXGBE_KRM_AN_CNTL_1_ASM_PAUSE;
		break;
	default:
		ERROR_REPORT1(IXGBE_ERROR_ARGUMENT,
			      "Flow control param set incorrectly\n");
		return IXGBE_ERR_CONFIG;
	}

	status = hw->mac.ops.write_iosf_sb_reg(hw,
					IXGBE_KRM_AN_CNTL_1(hw->bus.lan_id),
					IXGBE_SB_IOSF_TARGET_KR_PHY, an_cntl);

	/* Restart auto-negotiation. */
	status = ixgbe_restart_an_internal_phy_x550em(hw);

	return status;
}

/**
 * ixgbe_set_mux - Set mux for port 1 access with CS4227
 * @hw: pointer to hardware structure
 * @state: set mux if 1, clear if 0
 */
static void ixgbe_set_mux(struct ixgbe_hw *hw, u8 state)
{
	u32 esdp;

	if (!hw->bus.lan_id)
		return;
	esdp = IXGBE_READ_REG(hw, IXGBE_ESDP);
	if (state)
		esdp |= IXGBE_ESDP_SDP1;
	else
		esdp &= ~IXGBE_ESDP_SDP1;
	IXGBE_WRITE_REG(hw, IXGBE_ESDP, esdp);
	IXGBE_WRITE_FLUSH(hw);
}

/**
 *  ixgbe_acquire_swfw_sync_X550em - Acquire SWFW semaphore
 *  @hw: pointer to hardware structure
 *  @mask: Mask to specify which semaphore to acquire
 *
 *  Acquires the SWFW semaphore and sets the I2C MUX
 **/
s32 ixgbe_acquire_swfw_sync_X550em(struct ixgbe_hw *hw, u32 mask)
{
	s32 status;

	DEBUGFUNC("ixgbe_acquire_swfw_sync_X550em");

	status = ixgbe_acquire_swfw_sync_X540(hw, mask);
	if (status)
		return status;

	if (mask & IXGBE_GSSR_I2C_MASK)
		ixgbe_set_mux(hw, 1);

	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_release_swfw_sync_X550em - Release SWFW semaphore
 *  @hw: pointer to hardware structure
 *  @mask: Mask to specify which semaphore to release
 *
 *  Releases the SWFW semaphore and sets the I2C MUX
 **/
void ixgbe_release_swfw_sync_X550em(struct ixgbe_hw *hw, u32 mask)
{
	DEBUGFUNC("ixgbe_release_swfw_sync_X550em");

	if (mask & IXGBE_GSSR_I2C_MASK)
		ixgbe_set_mux(hw, 0);

	ixgbe_release_swfw_sync_X540(hw, mask);
}

/**
 *  ixgbe_acquire_swfw_sync_X550a - Acquire SWFW semaphore
 *  @hw: pointer to hardware structure
 *  @mask: Mask to specify which semaphore to acquire
 *
 *  Acquires the SWFW semaphore and get the shared phy token as needed
 */
static s32 ixgbe_acquire_swfw_sync_X550a(struct ixgbe_hw *hw, u32 mask)
{
	u32 hmask = mask & ~IXGBE_GSSR_TOKEN_SM;
	int retries = FW_PHY_TOKEN_RETRIES;
	s32 status = IXGBE_SUCCESS;

	DEBUGFUNC("ixgbe_acquire_swfw_sync_X550a");

	while (--retries) {
		status = IXGBE_SUCCESS;
		if (hmask)
			status = ixgbe_acquire_swfw_sync_X540(hw, hmask);
		if (status) {
			DEBUGOUT1("Could not acquire SWFW semaphore, Status = %d\n",
				  status);
			return status;
		}
		if (!(mask & IXGBE_GSSR_TOKEN_SM))
			return IXGBE_SUCCESS;

		status = ixgbe_get_phy_token(hw);
		if (status == IXGBE_ERR_TOKEN_RETRY)
			DEBUGOUT1("Could not acquire PHY token, Status = %d\n",
				  status);

		if (status == IXGBE_SUCCESS)
			return IXGBE_SUCCESS;

		if (hmask)
			ixgbe_release_swfw_sync_X540(hw, hmask);

		if (status != IXGBE_ERR_TOKEN_RETRY) {
			DEBUGOUT1("Unable to retry acquiring the PHY token, Status = %d\n",
				  status);
			return status;
		}
	}

	DEBUGOUT1("Semaphore acquisition retries failed!: PHY ID = 0x%08X\n",
		  hw->phy.id);
	return status;
}

/**
 *  ixgbe_release_swfw_sync_X550a - Release SWFW semaphore
 *  @hw: pointer to hardware structure
 *  @mask: Mask to specify which semaphore to release
 *
 *  Releases the SWFW semaphore and puts the shared phy token as needed
 */
static void ixgbe_release_swfw_sync_X550a(struct ixgbe_hw *hw, u32 mask)
{
	u32 hmask = mask & ~IXGBE_GSSR_TOKEN_SM;

	DEBUGFUNC("ixgbe_release_swfw_sync_X550a");

	if (mask & IXGBE_GSSR_TOKEN_SM)
		ixgbe_put_phy_token(hw);

	if (hmask)
		ixgbe_release_swfw_sync_X540(hw, hmask);
}

/**
 *  ixgbe_read_phy_reg_x550a  - Reads specified PHY register
 *  @hw: pointer to hardware structure
 *  @reg_addr: 32 bit address of PHY register to read
 *  @device_type: 5 bit device type
 *  @phy_data: Pointer to read data from PHY register
 *
 *  Reads a value from a specified PHY register using the SWFW lock and PHY
 *  Token. The PHY Token is needed since the MDIO is shared between to MAC
 *  instances.
 **/
s32 ixgbe_read_phy_reg_x550a(struct ixgbe_hw *hw, u32 reg_addr,
			       u32 device_type, u16 *phy_data)
{
	s32 status;
	u32 mask = hw->phy.phy_semaphore_mask | IXGBE_GSSR_TOKEN_SM;

	DEBUGFUNC("ixgbe_read_phy_reg_x550a");

	if (hw->mac.ops.acquire_swfw_sync(hw, mask))
		return IXGBE_ERR_SWFW_SYNC;

	status = hw->phy.ops.read_reg_mdi(hw, reg_addr, device_type, phy_data);

	hw->mac.ops.release_swfw_sync(hw, mask);

	return status;
}

/**
 *  ixgbe_write_phy_reg_x550a - Writes specified PHY register
 *  @hw: pointer to hardware structure
 *  @reg_addr: 32 bit PHY register to write
 *  @device_type: 5 bit device type
 *  @phy_data: Data to write to the PHY register
 *
 *  Writes a value to specified PHY register using the SWFW lock and PHY Token.
 *  The PHY Token is needed since the MDIO is shared between to MAC instances.
 **/
s32 ixgbe_write_phy_reg_x550a(struct ixgbe_hw *hw, u32 reg_addr,
				u32 device_type, u16 phy_data)
{
	s32 status;
	u32 mask = hw->phy.phy_semaphore_mask | IXGBE_GSSR_TOKEN_SM;

	DEBUGFUNC("ixgbe_write_phy_reg_x550a");

	if (hw->mac.ops.acquire_swfw_sync(hw, mask) == IXGBE_SUCCESS) {
		status = hw->phy.ops.write_reg_mdi(hw, reg_addr, device_type,
						 phy_data);
		hw->mac.ops.release_swfw_sync(hw, mask);
	} else {
		status = IXGBE_ERR_SWFW_SYNC;
	}

	return status;
}

/**
 * ixgbe_handle_lasi_ext_t_x550em - Handle external Base T PHY interrupt
 * @hw: pointer to hardware structure
 *
 * Handle external Base T PHY interrupt. If high temperature
 * failure alarm then return error, else if link status change
 * then setup internal/external PHY link
 *
 * Return IXGBE_ERR_OVERTEMP if interrupt is high temperature
 * failure alarm, else return PHY access status.
 */
s32 ixgbe_handle_lasi_ext_t_x550em(struct ixgbe_hw *hw)
{
	bool lsc;
	u32 status;

	status = ixgbe_get_lasi_ext_t_x550em(hw, &lsc);

	if (status != IXGBE_SUCCESS)
		return status;

	if (lsc)
		return ixgbe_setup_internal_phy(hw);

	return IXGBE_SUCCESS;
}

/**
 * ixgbe_setup_mac_link_t_X550em - Sets the auto advertised link speed
 * @hw: pointer to hardware structure
 * @speed: new link speed
 * @autoneg_wait_to_complete: TRUE when waiting for completion is needed
 *
 * Setup internal/external PHY link speed based on link speed, then set
 * external PHY auto advertised link speed.
 *
 * Returns error status for any failure
 **/
s32 ixgbe_setup_mac_link_t_X550em(struct ixgbe_hw *hw,
				  ixgbe_link_speed speed,
				  bool autoneg_wait_to_complete)
{
	s32 status;
	ixgbe_link_speed force_speed;

	DEBUGFUNC("ixgbe_setup_mac_link_t_X550em");

	/* Setup internal/external PHY link speed to iXFI (10G), unless
	 * only 1G is auto advertised then setup KX link.
	 */
	if (speed & IXGBE_LINK_SPEED_10GB_FULL)
		force_speed = IXGBE_LINK_SPEED_10GB_FULL;
	else
		force_speed = IXGBE_LINK_SPEED_1GB_FULL;

	/* If X552 and internal link mode is XFI, then setup XFI internal link.
	 */
	if (hw->mac.type == ixgbe_mac_X550EM_x &&
	    !(hw->phy.nw_mng_if_sel & IXGBE_NW_MNG_IF_SEL_INT_PHY_MODE)) {
		status = ixgbe_setup_ixfi_x550em(hw, &force_speed);

		if (status != IXGBE_SUCCESS)
			return status;
	}

	return hw->phy.ops.setup_link_speed(hw, speed, autoneg_wait_to_complete);
}

/**
 * ixgbe_check_link_t_X550em - Determine link and speed status
 * @hw: pointer to hardware structure
 * @speed: pointer to link speed
 * @link_up: TRUE when link is up
 * @link_up_wait_to_complete: bool used to wait for link up or not
 *
 * Check that both the MAC and X557 external PHY have link.
 **/
s32 ixgbe_check_link_t_X550em(struct ixgbe_hw *hw, ixgbe_link_speed *speed,
			      bool *link_up, bool link_up_wait_to_complete)
{
	u32 status;
	u16 i, autoneg_status = 0;

	if (hw->mac.ops.get_media_type(hw) != ixgbe_media_type_copper)
		return IXGBE_ERR_CONFIG;

	status = ixgbe_check_mac_link_generic(hw, speed, link_up,
					      link_up_wait_to_complete);

	/* If check link fails or MAC link is not up, then return */
	if (status != IXGBE_SUCCESS || !(*link_up))
		return status;

	/* MAC link is up, so check external PHY link.
	 * X557 PHY. Link status is latching low, and can only be used to detect
	 * link drop, and not the current status of the link without performing
	 * back-to-back reads.
	 */
	for (i = 0; i < 2; i++) {
		status = hw->phy.ops.read_reg(hw, IXGBE_MDIO_AUTO_NEG_STATUS,
					      IXGBE_MDIO_AUTO_NEG_DEV_TYPE,
					      &autoneg_status);

		if (status != IXGBE_SUCCESS)
			return status;
	}

	/* If external PHY link is not up, then indicate link not up */
	if (!(autoneg_status & IXGBE_MDIO_AUTO_NEG_LINK_STATUS))
		*link_up = FALSE;

	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_reset_phy_t_X550em - Performs X557 PHY reset and enables LASI
 *  @hw: pointer to hardware structure
 **/
s32 ixgbe_reset_phy_t_X550em(struct ixgbe_hw *hw)
{
	s32 status;

	status = ixgbe_reset_phy_generic(hw);

	if (status != IXGBE_SUCCESS)
		return status;

	/* Configure Link Status Alarm and Temperature Threshold interrupts */
	return ixgbe_enable_lasi_ext_t_x550em(hw);
}

/**
 *  ixgbe_led_on_t_X550em - Turns on the software controllable LEDs.
 *  @hw: pointer to hardware structure
 *  @led_idx: led number to turn on
 **/
s32 ixgbe_led_on_t_X550em(struct ixgbe_hw *hw, u32 led_idx)
{
	u16 phy_data;

	DEBUGFUNC("ixgbe_led_on_t_X550em");

	if (led_idx >= IXGBE_X557_MAX_LED_INDEX)
		return IXGBE_ERR_PARAM;

	/* To turn on the LED, set mode to ON. */
	ixgbe_read_phy_reg(hw, IXGBE_X557_LED_PROVISIONING + led_idx,
			   IXGBE_MDIO_VENDOR_SPECIFIC_1_DEV_TYPE, &phy_data);
	phy_data |= IXGBE_X557_LED_MANUAL_SET_MASK;
	ixgbe_write_phy_reg(hw, IXGBE_X557_LED_PROVISIONING + led_idx,
			    IXGBE_MDIO_VENDOR_SPECIFIC_1_DEV_TYPE, phy_data);

	/* Some designs have the LEDs wired to the MAC */
	return ixgbe_led_on_generic(hw, led_idx);
}

/**
 *  ixgbe_led_off_t_X550em - Turns off the software controllable LEDs.
 *  @hw: pointer to hardware structure
 *  @led_idx: led number to turn off
 **/
s32 ixgbe_led_off_t_X550em(struct ixgbe_hw *hw, u32 led_idx)
{
	u16 phy_data;

	DEBUGFUNC("ixgbe_led_off_t_X550em");

	if (led_idx >= IXGBE_X557_MAX_LED_INDEX)
		return IXGBE_ERR_PARAM;

	/* To turn on the LED, set mode to ON. */
	ixgbe_read_phy_reg(hw, IXGBE_X557_LED_PROVISIONING + led_idx,
			   IXGBE_MDIO_VENDOR_SPECIFIC_1_DEV_TYPE, &phy_data);
	phy_data &= ~IXGBE_X557_LED_MANUAL_SET_MASK;
	ixgbe_write_phy_reg(hw, IXGBE_X557_LED_PROVISIONING + led_idx,
			    IXGBE_MDIO_VENDOR_SPECIFIC_1_DEV_TYPE, phy_data);

	/* Some designs have the LEDs wired to the MAC */
	return ixgbe_led_off_generic(hw, led_idx);
}

/**
 *  ixgbe_set_fw_drv_ver_x550 - Sends driver version to firmware
 *  @hw: pointer to the HW structure
 *  @maj: driver version major number
 *  @min: driver version minor number
 *  @build: driver version build number
 *  @sub: driver version sub build number
 *  @len: length of driver_ver string
 *  @driver_ver: driver string
 *
 *  Sends driver version number to firmware through the manageability
 *  block.  On success return IXGBE_SUCCESS
 *  else returns IXGBE_ERR_SWFW_SYNC when encountering an error acquiring
 *  semaphore or IXGBE_ERR_HOST_INTERFACE_COMMAND when command fails.
 **/
s32 ixgbe_set_fw_drv_ver_x550(struct ixgbe_hw *hw, u8 maj, u8 min,
			      u8 build, u8 sub, u16 len, const char *driver_ver)
{
	struct ixgbe_hic_drv_info2 fw_cmd;
	s32 ret_val = IXGBE_SUCCESS;
	int i;

	DEBUGFUNC("ixgbe_set_fw_drv_ver_x550");

	if ((len == 0) || (driver_ver == NULL) ||
	   (len > sizeof(fw_cmd.driver_string)))
		return IXGBE_ERR_INVALID_ARGUMENT;

	fw_cmd.hdr.cmd = FW_CEM_CMD_DRIVER_INFO;
	fw_cmd.hdr.buf_len = FW_CEM_CMD_DRIVER_INFO_LEN + len;
	fw_cmd.hdr.cmd_or_resp.cmd_resv = FW_CEM_CMD_RESERVED;
	fw_cmd.port_num = (u8)hw->bus.func;
	fw_cmd.ver_maj = maj;
	fw_cmd.ver_min = min;
	fw_cmd.ver_build = build;
	fw_cmd.ver_sub = sub;
	fw_cmd.hdr.checksum = 0;
	memcpy(fw_cmd.driver_string, driver_ver, len);
	fw_cmd.hdr.checksum = ixgbe_calculate_checksum((u8 *)&fw_cmd,
				(FW_CEM_HDR_LEN + fw_cmd.hdr.buf_len));

	for (i = 0; i <= FW_CEM_MAX_RETRIES; i++) {
		ret_val = ixgbe_host_interface_command(hw, (u32 *)&fw_cmd,
						       sizeof(fw_cmd),
						       IXGBE_HI_COMMAND_TIMEOUT,
						       TRUE);
		if (ret_val != IXGBE_SUCCESS)
			continue;

		if (fw_cmd.hdr.cmd_or_resp.ret_status ==
		    FW_CEM_RESP_STATUS_SUCCESS)
			ret_val = IXGBE_SUCCESS;
		else
			ret_val = IXGBE_ERR_HOST_INTERFACE_COMMAND;

		break;
	}

	return ret_val;
}
