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

#include "ixgbe_type.h"
#include "ixgbe_82599.h"
#include "ixgbe_api.h"
#include "ixgbe_common.h"
#include "ixgbe_phy.h"

#define IXGBE_82599_MAX_TX_QUEUES 128
#define IXGBE_82599_MAX_RX_QUEUES 128
#define IXGBE_82599_RAR_ENTRIES   128
#define IXGBE_82599_MC_TBL_SIZE   128
#define IXGBE_82599_VFT_TBL_SIZE  128
#define IXGBE_82599_RX_PB_SIZE	  512

static s32 ixgbe_setup_copper_link_82599(struct ixgbe_hw *hw,
					 ixgbe_link_speed speed,
					 bool autoneg_wait_to_complete);
static s32 ixgbe_verify_fw_version_82599(struct ixgbe_hw *hw);
static s32 ixgbe_read_eeprom_82599(struct ixgbe_hw *hw,
				   u16 offset, u16 *data);
static s32 ixgbe_read_eeprom_buffer_82599(struct ixgbe_hw *hw, u16 offset,
					  u16 words, u16 *data);
static s32 ixgbe_read_i2c_byte_82599(struct ixgbe_hw *hw, u8 byte_offset,
					u8 dev_addr, u8 *data);
static s32 ixgbe_write_i2c_byte_82599(struct ixgbe_hw *hw, u8 byte_offset,
					u8 dev_addr, u8 data);

void ixgbe_init_mac_link_ops_82599(struct ixgbe_hw *hw)
{
	struct ixgbe_mac_info *mac = &hw->mac;

	DEBUGFUNC("ixgbe_init_mac_link_ops_82599");

	/*
	 * enable the laser control functions for SFP+ fiber
	 * and MNG not enabled
	 */
	if ((mac->ops.get_media_type(hw) == ixgbe_media_type_fiber) &&
	    !ixgbe_mng_enabled(hw)) {
		mac->ops.disable_tx_laser =
				       ixgbe_disable_tx_laser_multispeed_fiber;
		mac->ops.enable_tx_laser =
					ixgbe_enable_tx_laser_multispeed_fiber;
		mac->ops.flap_tx_laser = ixgbe_flap_tx_laser_multispeed_fiber;

	} else {
		mac->ops.disable_tx_laser = NULL;
		mac->ops.enable_tx_laser = NULL;
		mac->ops.flap_tx_laser = NULL;
	}

	if (hw->phy.multispeed_fiber) {
		/* Set up dual speed SFP+ support */
		mac->ops.setup_link = ixgbe_setup_mac_link_multispeed_fiber;
		mac->ops.setup_mac_link = ixgbe_setup_mac_link_82599;
		mac->ops.set_rate_select_speed =
					       ixgbe_set_hard_rate_select_speed;
		if (ixgbe_get_media_type(hw) == ixgbe_media_type_fiber_fixed)
			mac->ops.set_rate_select_speed =
					       ixgbe_set_soft_rate_select_speed;
	} else {
		if ((ixgbe_get_media_type(hw) == ixgbe_media_type_backplane) &&
		     (hw->phy.smart_speed == ixgbe_smart_speed_auto ||
		      hw->phy.smart_speed == ixgbe_smart_speed_on) &&
		      !ixgbe_verify_lesm_fw_enabled_82599(hw)) {
			mac->ops.setup_link = ixgbe_setup_mac_link_smartspeed;
		} else {
			mac->ops.setup_link = ixgbe_setup_mac_link_82599;
		}
	}
}

/**
 *  ixgbe_init_phy_ops_82599 - PHY/SFP specific init
 *  @hw: pointer to hardware structure
 *
 *  Initialize any function pointers that were not able to be
 *  set during init_shared_code because the PHY/SFP type was
 *  not known.  Perform the SFP init if necessary.
 *
 **/
s32 ixgbe_init_phy_ops_82599(struct ixgbe_hw *hw)
{
	struct ixgbe_mac_info *mac = &hw->mac;
	struct ixgbe_phy_info *phy = &hw->phy;
	s32 ret_val = IXGBE_SUCCESS;
	u32 esdp;

	DEBUGFUNC("ixgbe_init_phy_ops_82599");

	if (hw->device_id == IXGBE_DEV_ID_82599_QSFP_SF_QP) {
		/* Store flag indicating I2C bus access control unit. */
		hw->phy.qsfp_shared_i2c_bus = TRUE;

		/* Initialize access to QSFP+ I2C bus */
		esdp = IXGBE_READ_REG(hw, IXGBE_ESDP);
		esdp |= IXGBE_ESDP_SDP0_DIR;
		esdp &= ~IXGBE_ESDP_SDP1_DIR;
		esdp &= ~IXGBE_ESDP_SDP0;
		esdp &= ~IXGBE_ESDP_SDP0_NATIVE;
		esdp &= ~IXGBE_ESDP_SDP1_NATIVE;
		IXGBE_WRITE_REG(hw, IXGBE_ESDP, esdp);
		IXGBE_WRITE_FLUSH(hw);

		phy->ops.read_i2c_byte = ixgbe_read_i2c_byte_82599;
		phy->ops.write_i2c_byte = ixgbe_write_i2c_byte_82599;
	}
	/* Identify the PHY or SFP module */
	ret_val = phy->ops.identify(hw);
	if (ret_val == IXGBE_ERR_SFP_NOT_SUPPORTED)
		goto init_phy_ops_out;

	/* Setup function pointers based on detected SFP module and speeds */
	ixgbe_init_mac_link_ops_82599(hw);
	if (hw->phy.sfp_type != ixgbe_sfp_type_unknown)
		hw->phy.ops.reset = NULL;

	/* If copper media, overwrite with copper function pointers */
	if (mac->ops.get_media_type(hw) == ixgbe_media_type_copper) {
		mac->ops.setup_link = ixgbe_setup_copper_link_82599;
		mac->ops.get_link_capabilities =
				  ixgbe_get_copper_link_capabilities_generic;
	}

	/* Set necessary function pointers based on PHY type */
	switch (hw->phy.type) {
	case ixgbe_phy_tn:
		phy->ops.setup_link = ixgbe_setup_phy_link_tnx;
		phy->ops.check_link = ixgbe_check_phy_link_tnx;
		phy->ops.get_firmware_version =
			     ixgbe_get_phy_firmware_version_tnx;
		break;
	default:
		break;
	}
init_phy_ops_out:
	return ret_val;
}

s32 ixgbe_setup_sfp_modules_82599(struct ixgbe_hw *hw)
{
	s32 ret_val = IXGBE_SUCCESS;
	u16 list_offset, data_offset, data_value;

	DEBUGFUNC("ixgbe_setup_sfp_modules_82599");

	if (hw->phy.sfp_type != ixgbe_sfp_type_unknown) {
		ixgbe_init_mac_link_ops_82599(hw);

		hw->phy.ops.reset = NULL;

		ret_val = ixgbe_get_sfp_init_sequence_offsets(hw, &list_offset,
							      &data_offset);
		if (ret_val != IXGBE_SUCCESS)
			goto setup_sfp_out;

		/* PHY config will finish before releasing the semaphore */
		ret_val = hw->mac.ops.acquire_swfw_sync(hw,
							IXGBE_GSSR_MAC_CSR_SM);
		if (ret_val != IXGBE_SUCCESS) {
			ret_val = IXGBE_ERR_SWFW_SYNC;
			goto setup_sfp_out;
		}

		if (hw->eeprom.ops.read(hw, ++data_offset, &data_value))
			goto setup_sfp_err;
		while (data_value != 0xffff) {
			IXGBE_WRITE_REG(hw, IXGBE_CORECTL, data_value);
			IXGBE_WRITE_FLUSH(hw);
			if (hw->eeprom.ops.read(hw, ++data_offset, &data_value))
				goto setup_sfp_err;
		}

		/* Release the semaphore */
		hw->mac.ops.release_swfw_sync(hw, IXGBE_GSSR_MAC_CSR_SM);
		/* Delay obtaining semaphore again to allow FW access
		 * prot_autoc_write uses the semaphore too.
		 */
		msec_delay(hw->eeprom.semaphore_delay);

		/* Restart DSP and set SFI mode */
		ret_val = hw->mac.ops.prot_autoc_write(hw,
			hw->mac.orig_autoc | IXGBE_AUTOC_LMS_10G_SERIAL,
			FALSE);

		if (ret_val) {
			DEBUGOUT("sfp module setup not complete\n");
			ret_val = IXGBE_ERR_SFP_SETUP_NOT_COMPLETE;
			goto setup_sfp_out;
		}

	}

setup_sfp_out:
	return ret_val;

setup_sfp_err:
	/* Release the semaphore */
	hw->mac.ops.release_swfw_sync(hw, IXGBE_GSSR_MAC_CSR_SM);
	/* Delay obtaining semaphore again to allow FW access */
	msec_delay(hw->eeprom.semaphore_delay);
	ERROR_REPORT2(IXGBE_ERROR_INVALID_STATE,
		      "eeprom read at offset %d failed", data_offset);
	return IXGBE_ERR_PHY;
}

/**
 *  prot_autoc_read_82599 - Hides MAC differences needed for AUTOC read
 *  @hw: pointer to hardware structure
 *  @locked: Return the if we locked for this read.
 *  @reg_val: Value we read from AUTOC
 *
 *  For this part (82599) we need to wrap read-modify-writes with a possible
 *  FW/SW lock.  It is assumed this lock will be freed with the next
 *  prot_autoc_write_82599().
 */
s32 prot_autoc_read_82599(struct ixgbe_hw *hw, bool *locked, u32 *reg_val)
{
	s32 ret_val;

	*locked = FALSE;
	 /* If LESM is on then we need to hold the SW/FW semaphore. */
	if (ixgbe_verify_lesm_fw_enabled_82599(hw)) {
		ret_val = hw->mac.ops.acquire_swfw_sync(hw,
					IXGBE_GSSR_MAC_CSR_SM);
		if (ret_val != IXGBE_SUCCESS)
			return IXGBE_ERR_SWFW_SYNC;

		*locked = TRUE;
	}

	*reg_val = IXGBE_READ_REG(hw, IXGBE_AUTOC);
	return IXGBE_SUCCESS;
}

/**
 * prot_autoc_write_82599 - Hides MAC differences needed for AUTOC write
 * @hw: pointer to hardware structure
 * @autoc: value to write to AUTOC
 * @locked: bool to indicate whether the SW/FW lock was already taken by
 *           previous proc_autoc_read_82599.
 *
 * This part (82599) may need to hold the SW/FW lock around all writes to
 * AUTOC. Likewise after a write we need to do a pipeline reset.
 */
s32 prot_autoc_write_82599(struct ixgbe_hw *hw, u32 autoc, bool locked)
{
	s32 ret_val = IXGBE_SUCCESS;

	/* Blocked by MNG FW so bail */
	if (ixgbe_check_reset_blocked(hw))
		goto out;

	/* We only need to get the lock if:
	 *  - We didn't do it already (in the read part of a read-modify-write)
	 *  - LESM is enabled.
	 */
	if (!locked && ixgbe_verify_lesm_fw_enabled_82599(hw)) {
		ret_val = hw->mac.ops.acquire_swfw_sync(hw,
					IXGBE_GSSR_MAC_CSR_SM);
		if (ret_val != IXGBE_SUCCESS)
			return IXGBE_ERR_SWFW_SYNC;

		locked = TRUE;
	}

	IXGBE_WRITE_REG(hw, IXGBE_AUTOC, autoc);
	ret_val = ixgbe_reset_pipeline_82599(hw);

out:
	/* Free the SW/FW semaphore as we either grabbed it here or
	 * already had it when this function was called.
	 */
	if (locked)
		hw->mac.ops.release_swfw_sync(hw, IXGBE_GSSR_MAC_CSR_SM);

	return ret_val;
}

/**
 *  ixgbe_init_ops_82599 - Inits func ptrs and MAC type
 *  @hw: pointer to hardware structure
 *
 *  Initialize the function pointers and assign the MAC type for 82599.
 *  Does not touch the hardware.
 **/

s32 ixgbe_init_ops_82599(struct ixgbe_hw *hw)
{
	struct ixgbe_mac_info *mac = &hw->mac;
	struct ixgbe_phy_info *phy = &hw->phy;
	struct ixgbe_eeprom_info *eeprom = &hw->eeprom;
	s32 ret_val;

	DEBUGFUNC("ixgbe_init_ops_82599");

	ixgbe_init_phy_ops_generic(hw);
	ret_val = ixgbe_init_ops_generic(hw);

	/* PHY */
	phy->ops.identify = ixgbe_identify_phy_82599;
	phy->ops.init = ixgbe_init_phy_ops_82599;

	/* MAC */
	mac->ops.reset_hw = ixgbe_reset_hw_82599;
	mac->ops.enable_relaxed_ordering = ixgbe_enable_relaxed_ordering_gen2;
	mac->ops.get_media_type = ixgbe_get_media_type_82599;
	mac->ops.get_supported_physical_layer =
				    ixgbe_get_supported_physical_layer_82599;
	mac->ops.disable_sec_rx_path = ixgbe_disable_sec_rx_path_generic;
	mac->ops.enable_sec_rx_path = ixgbe_enable_sec_rx_path_generic;
	mac->ops.enable_rx_dma = ixgbe_enable_rx_dma_82599;
	mac->ops.read_analog_reg8 = ixgbe_read_analog_reg8_82599;
	mac->ops.write_analog_reg8 = ixgbe_write_analog_reg8_82599;
	mac->ops.start_hw = ixgbe_start_hw_82599;
	mac->ops.get_san_mac_addr = ixgbe_get_san_mac_addr_generic;
	mac->ops.set_san_mac_addr = ixgbe_set_san_mac_addr_generic;
	mac->ops.get_device_caps = ixgbe_get_device_caps_generic;
	mac->ops.get_wwn_prefix = ixgbe_get_wwn_prefix_generic;
	mac->ops.get_fcoe_boot_status = ixgbe_get_fcoe_boot_status_generic;
	mac->ops.prot_autoc_read = prot_autoc_read_82599;
	mac->ops.prot_autoc_write = prot_autoc_write_82599;

	/* RAR, Multicast, VLAN */
	mac->ops.set_vmdq = ixgbe_set_vmdq_generic;
	mac->ops.set_vmdq_san_mac = ixgbe_set_vmdq_san_mac_generic;
	mac->ops.clear_vmdq = ixgbe_clear_vmdq_generic;
	mac->ops.insert_mac_addr = ixgbe_insert_mac_addr_generic;
	mac->rar_highwater = 1;
	mac->ops.set_vfta = ixgbe_set_vfta_generic;
	mac->ops.set_vlvf = ixgbe_set_vlvf_generic;
	mac->ops.clear_vfta = ixgbe_clear_vfta_generic;
	mac->ops.init_uta_tables = ixgbe_init_uta_tables_generic;
	mac->ops.setup_sfp = ixgbe_setup_sfp_modules_82599;
	mac->ops.set_mac_anti_spoofing = ixgbe_set_mac_anti_spoofing;
	mac->ops.set_vlan_anti_spoofing = ixgbe_set_vlan_anti_spoofing;

	/* Link */
	mac->ops.get_link_capabilities = ixgbe_get_link_capabilities_82599;
	mac->ops.check_link = ixgbe_check_mac_link_generic;
	mac->ops.setup_rxpba = ixgbe_set_rxpba_generic;
	ixgbe_init_mac_link_ops_82599(hw);

	mac->mcft_size		= IXGBE_82599_MC_TBL_SIZE;
	mac->vft_size		= IXGBE_82599_VFT_TBL_SIZE;
	mac->num_rar_entries	= IXGBE_82599_RAR_ENTRIES;
	mac->rx_pb_size		= IXGBE_82599_RX_PB_SIZE;
	mac->max_rx_queues	= IXGBE_82599_MAX_RX_QUEUES;
	mac->max_tx_queues	= IXGBE_82599_MAX_TX_QUEUES;
	mac->max_msix_vectors	= ixgbe_get_pcie_msix_count_generic(hw);

	mac->arc_subsystem_valid = !!(IXGBE_READ_REG(hw, IXGBE_FWSM_BY_MAC(hw))
				      & IXGBE_FWSM_MODE_MASK);

	hw->mbx.ops.init_params = ixgbe_init_mbx_params_pf;

	/* EEPROM */
	eeprom->ops.read = ixgbe_read_eeprom_82599;
	eeprom->ops.read_buffer = ixgbe_read_eeprom_buffer_82599;

	/* Manageability interface */
	mac->ops.set_fw_drv_ver = ixgbe_set_fw_drv_ver_generic;

	mac->ops.bypass_rw = ixgbe_bypass_rw_generic;
	mac->ops.bypass_valid_rd = ixgbe_bypass_valid_rd_generic;
	mac->ops.bypass_set = ixgbe_bypass_set_generic;
	mac->ops.bypass_rd_eep = ixgbe_bypass_rd_eep_generic;

	mac->ops.get_rtrup2tc = ixgbe_dcb_get_rtrup2tc_generic;

	return ret_val;
}

/**
 *  ixgbe_get_link_capabilities_82599 - Determines link capabilities
 *  @hw: pointer to hardware structure
 *  @speed: pointer to link speed
 *  @autoneg: TRUE when autoneg or autotry is enabled
 *
 *  Determines the link capabilities by reading the AUTOC register.
 **/
s32 ixgbe_get_link_capabilities_82599(struct ixgbe_hw *hw,
				      ixgbe_link_speed *speed,
				      bool *autoneg)
{
	s32 status = IXGBE_SUCCESS;
	u32 autoc = 0;

	DEBUGFUNC("ixgbe_get_link_capabilities_82599");


	/* Check if 1G SFP module. */
	if (hw->phy.sfp_type == ixgbe_sfp_type_1g_cu_core0 ||
	    hw->phy.sfp_type == ixgbe_sfp_type_1g_cu_core1 ||
	    hw->phy.sfp_type == ixgbe_sfp_type_1g_lx_core0 ||
	    hw->phy.sfp_type == ixgbe_sfp_type_1g_lx_core1 ||
	    hw->phy.sfp_type == ixgbe_sfp_type_1g_sx_core0 ||
	    hw->phy.sfp_type == ixgbe_sfp_type_1g_sx_core1) {
		*speed = IXGBE_LINK_SPEED_1GB_FULL;
		*autoneg = TRUE;
		goto out;
	}

	/*
	 * Determine link capabilities based on the stored value of AUTOC,
	 * which represents EEPROM defaults.  If AUTOC value has not
	 * been stored, use the current register values.
	 */
	if (hw->mac.orig_link_settings_stored)
		autoc = hw->mac.orig_autoc;
	else
		autoc = IXGBE_READ_REG(hw, IXGBE_AUTOC);

	switch (autoc & IXGBE_AUTOC_LMS_MASK) {
	case IXGBE_AUTOC_LMS_1G_LINK_NO_AN:
		*speed = IXGBE_LINK_SPEED_1GB_FULL;
		*autoneg = FALSE;
		break;

	case IXGBE_AUTOC_LMS_10G_LINK_NO_AN:
		*speed = IXGBE_LINK_SPEED_10GB_FULL;
		*autoneg = FALSE;
		break;

	case IXGBE_AUTOC_LMS_1G_AN:
		*speed = IXGBE_LINK_SPEED_1GB_FULL;
		*autoneg = TRUE;
		break;

	case IXGBE_AUTOC_LMS_10G_SERIAL:
		*speed = IXGBE_LINK_SPEED_10GB_FULL;
		*autoneg = FALSE;
		break;

	case IXGBE_AUTOC_LMS_KX4_KX_KR:
	case IXGBE_AUTOC_LMS_KX4_KX_KR_1G_AN:
		*speed = IXGBE_LINK_SPEED_UNKNOWN;
		if (autoc & IXGBE_AUTOC_KR_SUPP)
			*speed |= IXGBE_LINK_SPEED_10GB_FULL;
		if (autoc & IXGBE_AUTOC_KX4_SUPP)
			*speed |= IXGBE_LINK_SPEED_10GB_FULL;
		if (autoc & IXGBE_AUTOC_KX_SUPP)
			*speed |= IXGBE_LINK_SPEED_1GB_FULL;
		*autoneg = TRUE;
		break;

	case IXGBE_AUTOC_LMS_KX4_KX_KR_SGMII:
		*speed = IXGBE_LINK_SPEED_100_FULL;
		if (autoc & IXGBE_AUTOC_KR_SUPP)
			*speed |= IXGBE_LINK_SPEED_10GB_FULL;
		if (autoc & IXGBE_AUTOC_KX4_SUPP)
			*speed |= IXGBE_LINK_SPEED_10GB_FULL;
		if (autoc & IXGBE_AUTOC_KX_SUPP)
			*speed |= IXGBE_LINK_SPEED_1GB_FULL;
		*autoneg = TRUE;
		break;

	case IXGBE_AUTOC_LMS_SGMII_1G_100M:
		*speed = IXGBE_LINK_SPEED_1GB_FULL | IXGBE_LINK_SPEED_100_FULL;
		*autoneg = FALSE;
		break;

	default:
		status = IXGBE_ERR_LINK_SETUP;
		goto out;
		break;
	}

	if (hw->phy.multispeed_fiber) {
		*speed |= IXGBE_LINK_SPEED_10GB_FULL |
			  IXGBE_LINK_SPEED_1GB_FULL;

		/* QSFP must not enable full auto-negotiation
		 * Limited autoneg is enabled at 1G
		 */
		if (hw->phy.media_type == ixgbe_media_type_fiber_qsfp)
			*autoneg = FALSE;
		else
			*autoneg = TRUE;
	}

out:
	return status;
}

/**
 *  ixgbe_get_media_type_82599 - Get media type
 *  @hw: pointer to hardware structure
 *
 *  Returns the media type (fiber, copper, backplane)
 **/
enum ixgbe_media_type ixgbe_get_media_type_82599(struct ixgbe_hw *hw)
{
	enum ixgbe_media_type media_type;

	DEBUGFUNC("ixgbe_get_media_type_82599");

	/* Detect if there is a copper PHY attached. */
	switch (hw->phy.type) {
	case ixgbe_phy_cu_unknown:
	case ixgbe_phy_tn:
		media_type = ixgbe_media_type_copper;
		goto out;
	default:
		break;
	}

	switch (hw->device_id) {
	case IXGBE_DEV_ID_82599_KX4:
	case IXGBE_DEV_ID_82599_KX4_MEZZ:
	case IXGBE_DEV_ID_82599_COMBO_BACKPLANE:
	case IXGBE_DEV_ID_82599_KR:
	case IXGBE_DEV_ID_82599_BACKPLANE_FCOE:
	case IXGBE_DEV_ID_82599_XAUI_LOM:
		/* Default device ID is mezzanine card KX/KX4 */
		media_type = ixgbe_media_type_backplane;
		break;
	case IXGBE_DEV_ID_82599_SFP:
	case IXGBE_DEV_ID_82599_SFP_FCOE:
	case IXGBE_DEV_ID_82599_SFP_EM:
	case IXGBE_DEV_ID_82599_SFP_SF2:
	case IXGBE_DEV_ID_82599_SFP_SF_QP:
	case IXGBE_DEV_ID_82599EN_SFP:
		media_type = ixgbe_media_type_fiber;
		break;
	case IXGBE_DEV_ID_82599_CX4:
		media_type = ixgbe_media_type_cx4;
		break;
	case IXGBE_DEV_ID_82599_T3_LOM:
		media_type = ixgbe_media_type_copper;
		break;
	case IXGBE_DEV_ID_82599_QSFP_SF_QP:
		media_type = ixgbe_media_type_fiber_qsfp;
		break;
	case IXGBE_DEV_ID_82599_BYPASS:
		media_type = ixgbe_media_type_fiber_fixed;
		hw->phy.multispeed_fiber = TRUE;
		break;
	default:
		media_type = ixgbe_media_type_unknown;
		break;
	}
out:
	return media_type;
}

/**
 *  ixgbe_stop_mac_link_on_d3_82599 - Disables link on D3
 *  @hw: pointer to hardware structure
 *
 *  Disables link during D3 power down sequence.
 *
 **/
void ixgbe_stop_mac_link_on_d3_82599(struct ixgbe_hw *hw)
{
	u32 autoc2_reg;
	u16 ee_ctrl_2 = 0;

	DEBUGFUNC("ixgbe_stop_mac_link_on_d3_82599");
	ixgbe_read_eeprom(hw, IXGBE_EEPROM_CTRL_2, &ee_ctrl_2);

	if (!ixgbe_mng_present(hw) && !hw->wol_enabled &&
	    ee_ctrl_2 & IXGBE_EEPROM_CCD_BIT) {
		autoc2_reg = IXGBE_READ_REG(hw, IXGBE_AUTOC2);
		autoc2_reg |= IXGBE_AUTOC2_LINK_DISABLE_ON_D3_MASK;
		IXGBE_WRITE_REG(hw, IXGBE_AUTOC2, autoc2_reg);
	}
}

/**
 *  ixgbe_start_mac_link_82599 - Setup MAC link settings
 *  @hw: pointer to hardware structure
 *  @autoneg_wait_to_complete: TRUE when waiting for completion is needed
 *
 *  Configures link settings based on values in the ixgbe_hw struct.
 *  Restarts the link.  Performs autonegotiation if needed.
 **/
s32 ixgbe_start_mac_link_82599(struct ixgbe_hw *hw,
			       bool autoneg_wait_to_complete)
{
	u32 autoc_reg;
	u32 links_reg;
	u32 i;
	s32 status = IXGBE_SUCCESS;
	bool got_lock = FALSE;

	DEBUGFUNC("ixgbe_start_mac_link_82599");


	/*  reset_pipeline requires us to hold this lock as it writes to
	 *  AUTOC.
	 */
	if (ixgbe_verify_lesm_fw_enabled_82599(hw)) {
		status = hw->mac.ops.acquire_swfw_sync(hw,
						       IXGBE_GSSR_MAC_CSR_SM);
		if (status != IXGBE_SUCCESS)
			goto out;

		got_lock = TRUE;
	}

	/* Restart link */
	ixgbe_reset_pipeline_82599(hw);

	if (got_lock)
		hw->mac.ops.release_swfw_sync(hw, IXGBE_GSSR_MAC_CSR_SM);

	/* Only poll for autoneg to complete if specified to do so */
	if (autoneg_wait_to_complete) {
		autoc_reg = IXGBE_READ_REG(hw, IXGBE_AUTOC);
		if ((autoc_reg & IXGBE_AUTOC_LMS_MASK) ==
		     IXGBE_AUTOC_LMS_KX4_KX_KR ||
		    (autoc_reg & IXGBE_AUTOC_LMS_MASK) ==
		     IXGBE_AUTOC_LMS_KX4_KX_KR_1G_AN ||
		    (autoc_reg & IXGBE_AUTOC_LMS_MASK) ==
		     IXGBE_AUTOC_LMS_KX4_KX_KR_SGMII) {
			links_reg = 0; /* Just in case Autoneg time = 0 */
			for (i = 0; i < IXGBE_AUTO_NEG_TIME; i++) {
				links_reg = IXGBE_READ_REG(hw, IXGBE_LINKS);
				if (links_reg & IXGBE_LINKS_KX_AN_COMP)
					break;
				msec_delay(100);
			}
			if (!(links_reg & IXGBE_LINKS_KX_AN_COMP)) {
				status = IXGBE_ERR_AUTONEG_NOT_COMPLETE;
				DEBUGOUT("Autoneg did not complete.\n");
			}
		}
	}

	/* Add delay to filter out noises during initial link setup */
	msec_delay(50);

out:
	return status;
}

/**
 *  ixgbe_disable_tx_laser_multispeed_fiber - Disable Tx laser
 *  @hw: pointer to hardware structure
 *
 *  The base drivers may require better control over SFP+ module
 *  PHY states.  This includes selectively shutting down the Tx
 *  laser on the PHY, effectively halting physical link.
 **/
void ixgbe_disable_tx_laser_multispeed_fiber(struct ixgbe_hw *hw)
{
	u32 esdp_reg = IXGBE_READ_REG(hw, IXGBE_ESDP);

	/* Blocked by MNG FW so bail */
	if (ixgbe_check_reset_blocked(hw))
		return;

	/* Disable Tx laser; allow 100us to go dark per spec */
	esdp_reg |= IXGBE_ESDP_SDP3;
	IXGBE_WRITE_REG(hw, IXGBE_ESDP, esdp_reg);
	IXGBE_WRITE_FLUSH(hw);
	usec_delay(100);
}

/**
 *  ixgbe_enable_tx_laser_multispeed_fiber - Enable Tx laser
 *  @hw: pointer to hardware structure
 *
 *  The base drivers may require better control over SFP+ module
 *  PHY states.  This includes selectively turning on the Tx
 *  laser on the PHY, effectively starting physical link.
 **/
void ixgbe_enable_tx_laser_multispeed_fiber(struct ixgbe_hw *hw)
{
	u32 esdp_reg = IXGBE_READ_REG(hw, IXGBE_ESDP);

	/* Enable Tx laser; allow 100ms to light up */
	esdp_reg &= ~IXGBE_ESDP_SDP3;
	IXGBE_WRITE_REG(hw, IXGBE_ESDP, esdp_reg);
	IXGBE_WRITE_FLUSH(hw);
	msec_delay(100);
}

/**
 *  ixgbe_flap_tx_laser_multispeed_fiber - Flap Tx laser
 *  @hw: pointer to hardware structure
 *
 *  When the driver changes the link speeds that it can support,
 *  it sets autotry_restart to TRUE to indicate that we need to
 *  initiate a new autotry session with the link partner.  To do
 *  so, we set the speed then disable and re-enable the Tx laser, to
 *  alert the link partner that it also needs to restart autotry on its
 *  end.  This is consistent with TRUE clause 37 autoneg, which also
 *  involves a loss of signal.
 **/
void ixgbe_flap_tx_laser_multispeed_fiber(struct ixgbe_hw *hw)
{
	DEBUGFUNC("ixgbe_flap_tx_laser_multispeed_fiber");

	/* Blocked by MNG FW so bail */
	if (ixgbe_check_reset_blocked(hw))
		return;

	if (hw->mac.autotry_restart) {
		ixgbe_disable_tx_laser_multispeed_fiber(hw);
		ixgbe_enable_tx_laser_multispeed_fiber(hw);
		hw->mac.autotry_restart = FALSE;
	}
}

/**
 *  ixgbe_set_hard_rate_select_speed - Set module link speed
 *  @hw: pointer to hardware structure
 *  @speed: link speed to set
 *
 *  Set module link speed via RS0/RS1 rate select pins.
 */
void ixgbe_set_hard_rate_select_speed(struct ixgbe_hw *hw,
					ixgbe_link_speed speed)
{
	u32 esdp_reg = IXGBE_READ_REG(hw, IXGBE_ESDP);

	switch (speed) {
	case IXGBE_LINK_SPEED_10GB_FULL:
		esdp_reg |= (IXGBE_ESDP_SDP5_DIR | IXGBE_ESDP_SDP5);
		break;
	case IXGBE_LINK_SPEED_1GB_FULL:
		esdp_reg &= ~IXGBE_ESDP_SDP5;
		esdp_reg |= IXGBE_ESDP_SDP5_DIR;
		break;
	default:
		DEBUGOUT("Invalid fixed module speed\n");
		return;
	}

	IXGBE_WRITE_REG(hw, IXGBE_ESDP, esdp_reg);
	IXGBE_WRITE_FLUSH(hw);
}

/**
 *  ixgbe_setup_mac_link_smartspeed - Set MAC link speed using SmartSpeed
 *  @hw: pointer to hardware structure
 *  @speed: new link speed
 *  @autoneg_wait_to_complete: TRUE when waiting for completion is needed
 *
 *  Implements the Intel SmartSpeed algorithm.
 **/
s32 ixgbe_setup_mac_link_smartspeed(struct ixgbe_hw *hw,
				    ixgbe_link_speed speed,
				    bool autoneg_wait_to_complete)
{
	s32 status = IXGBE_SUCCESS;
	ixgbe_link_speed link_speed = IXGBE_LINK_SPEED_UNKNOWN;
	s32 i, j;
	bool link_up = FALSE;
	u32 autoc_reg = IXGBE_READ_REG(hw, IXGBE_AUTOC);

	DEBUGFUNC("ixgbe_setup_mac_link_smartspeed");

	 /* Set autoneg_advertised value based on input link speed */
	hw->phy.autoneg_advertised = 0;

	if (speed & IXGBE_LINK_SPEED_10GB_FULL)
		hw->phy.autoneg_advertised |= IXGBE_LINK_SPEED_10GB_FULL;

	if (speed & IXGBE_LINK_SPEED_1GB_FULL)
		hw->phy.autoneg_advertised |= IXGBE_LINK_SPEED_1GB_FULL;

	if (speed & IXGBE_LINK_SPEED_100_FULL)
		hw->phy.autoneg_advertised |= IXGBE_LINK_SPEED_100_FULL;

	/*
	 * Implement Intel SmartSpeed algorithm.  SmartSpeed will reduce the
	 * autoneg advertisement if link is unable to be established at the
	 * highest negotiated rate.  This can sometimes happen due to integrity
	 * issues with the physical media connection.
	 */

	/* First, try to get link with full advertisement */
	hw->phy.smart_speed_active = FALSE;
	for (j = 0; j < IXGBE_SMARTSPEED_MAX_RETRIES; j++) {
		status = ixgbe_setup_mac_link_82599(hw, speed,
						    autoneg_wait_to_complete);
		if (status != IXGBE_SUCCESS)
			goto out;

		/*
		 * Wait for the controller to acquire link.  Per IEEE 802.3ap,
		 * Section 73.10.2, we may have to wait up to 500ms if KR is
		 * attempted, or 200ms if KX/KX4/BX/BX4 is attempted, per
		 * Table 9 in the AN MAS.
		 */
		for (i = 0; i < 5; i++) {
			msec_delay(100);

			/* If we have link, just jump out */
			status = ixgbe_check_link(hw, &link_speed, &link_up,
						  FALSE);
			if (status != IXGBE_SUCCESS)
				goto out;

			if (link_up)
				goto out;
		}
	}

	/*
	 * We didn't get link.  If we advertised KR plus one of KX4/KX
	 * (or BX4/BX), then disable KR and try again.
	 */
	if (((autoc_reg & IXGBE_AUTOC_KR_SUPP) == 0) ||
	    ((autoc_reg & IXGBE_AUTOC_KX4_KX_SUPP_MASK) == 0))
		goto out;

	/* Turn SmartSpeed on to disable KR support */
	hw->phy.smart_speed_active = TRUE;
	status = ixgbe_setup_mac_link_82599(hw, speed,
					    autoneg_wait_to_complete);
	if (status != IXGBE_SUCCESS)
		goto out;

	/*
	 * Wait for the controller to acquire link.  600ms will allow for
	 * the AN link_fail_inhibit_timer as well for multiple cycles of
	 * parallel detect, both 10g and 1g. This allows for the maximum
	 * connect attempts as defined in the AN MAS table 73-7.
	 */
	for (i = 0; i < 6; i++) {
		msec_delay(100);

		/* If we have link, just jump out */
		status = ixgbe_check_link(hw, &link_speed, &link_up, FALSE);
		if (status != IXGBE_SUCCESS)
			goto out;

		if (link_up)
			goto out;
	}

	/* We didn't get link.  Turn SmartSpeed back off. */
	hw->phy.smart_speed_active = FALSE;
	status = ixgbe_setup_mac_link_82599(hw, speed,
					    autoneg_wait_to_complete);

out:
	if (link_up && (link_speed == IXGBE_LINK_SPEED_1GB_FULL))
		DEBUGOUT("Smartspeed has downgraded the link speed "
		"from the maximum advertised\n");
	return status;
}

/**
 *  ixgbe_setup_mac_link_82599 - Set MAC link speed
 *  @hw: pointer to hardware structure
 *  @speed: new link speed
 *  @autoneg_wait_to_complete: TRUE when waiting for completion is needed
 *
 *  Set the link speed in the AUTOC register and restarts link.
 **/
s32 ixgbe_setup_mac_link_82599(struct ixgbe_hw *hw,
			       ixgbe_link_speed speed,
			       bool autoneg_wait_to_complete)
{
	bool autoneg = FALSE;
	s32 status = IXGBE_SUCCESS;
	u32 pma_pmd_1g, link_mode;
	u32 current_autoc = IXGBE_READ_REG(hw, IXGBE_AUTOC); /* holds the value of AUTOC register at this current point in time */
	u32 orig_autoc = 0; /* holds the cached value of AUTOC register */
	u32 autoc = current_autoc; /* Temporary variable used for comparison purposes */
	u32 autoc2 = IXGBE_READ_REG(hw, IXGBE_AUTOC2);
	u32 pma_pmd_10g_serial = autoc2 & IXGBE_AUTOC2_10G_SERIAL_PMA_PMD_MASK;
	u32 links_reg;
	u32 i;
	ixgbe_link_speed link_capabilities = IXGBE_LINK_SPEED_UNKNOWN;

	DEBUGFUNC("ixgbe_setup_mac_link_82599");

	/* Check to see if speed passed in is supported. */
	status = ixgbe_get_link_capabilities(hw, &link_capabilities, &autoneg);
	if (status)
		goto out;

	speed &= link_capabilities;

	if (speed == IXGBE_LINK_SPEED_UNKNOWN) {
		status = IXGBE_ERR_LINK_SETUP;
		goto out;
	}

	/* Use stored value (EEPROM defaults) of AUTOC to find KR/KX4 support*/
	if (hw->mac.orig_link_settings_stored)
		orig_autoc = hw->mac.orig_autoc;
	else
		orig_autoc = autoc;

	link_mode = autoc & IXGBE_AUTOC_LMS_MASK;
	pma_pmd_1g = autoc & IXGBE_AUTOC_1G_PMA_PMD_MASK;

	if (link_mode == IXGBE_AUTOC_LMS_KX4_KX_KR ||
	    link_mode == IXGBE_AUTOC_LMS_KX4_KX_KR_1G_AN ||
	    link_mode == IXGBE_AUTOC_LMS_KX4_KX_KR_SGMII) {
		/* Set KX4/KX/KR support according to speed requested */
		autoc &= ~(IXGBE_AUTOC_KX4_KX_SUPP_MASK | IXGBE_AUTOC_KR_SUPP);
		if (speed & IXGBE_LINK_SPEED_10GB_FULL) {
			if (orig_autoc & IXGBE_AUTOC_KX4_SUPP)
				autoc |= IXGBE_AUTOC_KX4_SUPP;
			if ((orig_autoc & IXGBE_AUTOC_KR_SUPP) &&
			    (hw->phy.smart_speed_active == FALSE))
				autoc |= IXGBE_AUTOC_KR_SUPP;
		}
		if (speed & IXGBE_LINK_SPEED_1GB_FULL)
			autoc |= IXGBE_AUTOC_KX_SUPP;
	} else if ((pma_pmd_1g == IXGBE_AUTOC_1G_SFI) &&
		   (link_mode == IXGBE_AUTOC_LMS_1G_LINK_NO_AN ||
		    link_mode == IXGBE_AUTOC_LMS_1G_AN)) {
		/* Switch from 1G SFI to 10G SFI if requested */
		if ((speed == IXGBE_LINK_SPEED_10GB_FULL) &&
		    (pma_pmd_10g_serial == IXGBE_AUTOC2_10G_SFI)) {
			autoc &= ~IXGBE_AUTOC_LMS_MASK;
			autoc |= IXGBE_AUTOC_LMS_10G_SERIAL;
		}
	} else if ((pma_pmd_10g_serial == IXGBE_AUTOC2_10G_SFI) &&
		   (link_mode == IXGBE_AUTOC_LMS_10G_SERIAL)) {
		/* Switch from 10G SFI to 1G SFI if requested */
		if ((speed == IXGBE_LINK_SPEED_1GB_FULL) &&
		    (pma_pmd_1g == IXGBE_AUTOC_1G_SFI)) {
			autoc &= ~IXGBE_AUTOC_LMS_MASK;
			if (autoneg || hw->phy.type == ixgbe_phy_qsfp_intel)
				autoc |= IXGBE_AUTOC_LMS_1G_AN;
			else
				autoc |= IXGBE_AUTOC_LMS_1G_LINK_NO_AN;
		}
	}

	if (autoc != current_autoc) {
		/* Restart link */
		status = hw->mac.ops.prot_autoc_write(hw, autoc, FALSE);
		if (status != IXGBE_SUCCESS)
			goto out;

		/* Only poll for autoneg to complete if specified to do so */
		if (autoneg_wait_to_complete) {
			if (link_mode == IXGBE_AUTOC_LMS_KX4_KX_KR ||
			    link_mode == IXGBE_AUTOC_LMS_KX4_KX_KR_1G_AN ||
			    link_mode == IXGBE_AUTOC_LMS_KX4_KX_KR_SGMII) {
				links_reg = 0; /*Just in case Autoneg time=0*/
				for (i = 0; i < IXGBE_AUTO_NEG_TIME; i++) {
					links_reg =
					       IXGBE_READ_REG(hw, IXGBE_LINKS);
					if (links_reg & IXGBE_LINKS_KX_AN_COMP)
						break;
					msec_delay(100);
				}
				if (!(links_reg & IXGBE_LINKS_KX_AN_COMP)) {
					status =
						IXGBE_ERR_AUTONEG_NOT_COMPLETE;
					DEBUGOUT("Autoneg did not complete.\n");
				}
			}
		}

		/* Add delay to filter out noises during initial link setup */
		msec_delay(50);
	}

out:
	return status;
}

/**
 *  ixgbe_setup_copper_link_82599 - Set the PHY autoneg advertised field
 *  @hw: pointer to hardware structure
 *  @speed: new link speed
 *  @autoneg_wait_to_complete: TRUE if waiting is needed to complete
 *
 *  Restarts link on PHY and MAC based on settings passed in.
 **/
static s32 ixgbe_setup_copper_link_82599(struct ixgbe_hw *hw,
					 ixgbe_link_speed speed,
					 bool autoneg_wait_to_complete)
{
	s32 status;

	DEBUGFUNC("ixgbe_setup_copper_link_82599");

	/* Setup the PHY according to input speed */
	status = hw->phy.ops.setup_link_speed(hw, speed,
					      autoneg_wait_to_complete);
	/* Set up MAC */
	ixgbe_start_mac_link_82599(hw, autoneg_wait_to_complete);

	return status;
}

/**
 *  ixgbe_reset_hw_82599 - Perform hardware reset
 *  @hw: pointer to hardware structure
 *
 *  Resets the hardware by resetting the transmit and receive units, masks
 *  and clears all interrupts, perform a PHY reset, and perform a link (MAC)
 *  reset.
 **/
s32 ixgbe_reset_hw_82599(struct ixgbe_hw *hw)
{
	ixgbe_link_speed link_speed;
	s32 status;
	u32 ctrl = 0;
	u32 i, autoc, autoc2;
	u32 curr_lms;
	bool link_up = FALSE;

	DEBUGFUNC("ixgbe_reset_hw_82599");

	/* Call adapter stop to disable tx/rx and clear interrupts */
	status = hw->mac.ops.stop_adapter(hw);
	if (status != IXGBE_SUCCESS)
		goto reset_hw_out;

	/* flush pending Tx transactions */
	ixgbe_clear_tx_pending(hw);

	/* PHY ops must be identified and initialized prior to reset */

	/* Identify PHY and related function pointers */
	status = hw->phy.ops.init(hw);

	if (status == IXGBE_ERR_SFP_NOT_SUPPORTED)
		goto reset_hw_out;

	/* Setup SFP module if there is one present. */
	if (hw->phy.sfp_setup_needed) {
		status = hw->mac.ops.setup_sfp(hw);
		hw->phy.sfp_setup_needed = FALSE;
	}

	if (status == IXGBE_ERR_SFP_NOT_SUPPORTED)
		goto reset_hw_out;

	/* Reset PHY */
	if (hw->phy.reset_disable == FALSE && hw->phy.ops.reset != NULL)
		hw->phy.ops.reset(hw);

	/* remember AUTOC from before we reset */
	curr_lms = IXGBE_READ_REG(hw, IXGBE_AUTOC) & IXGBE_AUTOC_LMS_MASK;

mac_reset_top:
	/*
	 * Issue global reset to the MAC.  Needs to be SW reset if link is up.
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

	ctrl |= IXGBE_READ_REG(hw, IXGBE_CTRL);
	IXGBE_WRITE_REG(hw, IXGBE_CTRL, ctrl);
	IXGBE_WRITE_FLUSH(hw);

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

	/*
	 * Double resets are required for recovery from certain error
	 * conditions.  Between resets, it is necessary to stall to
	 * allow time for any pending HW events to complete.
	 */
	if (hw->mac.flags & IXGBE_FLAGS_DOUBLE_RESET_REQUIRED) {
		hw->mac.flags &= ~IXGBE_FLAGS_DOUBLE_RESET_REQUIRED;
		goto mac_reset_top;
	}

	/*
	 * Store the original AUTOC/AUTOC2 values if they have not been
	 * stored off yet.  Otherwise restore the stored original
	 * values since the reset operation sets back to defaults.
	 */
	autoc = IXGBE_READ_REG(hw, IXGBE_AUTOC);
	autoc2 = IXGBE_READ_REG(hw, IXGBE_AUTOC2);

	/* Enable link if disabled in NVM */
	if (autoc2 & IXGBE_AUTOC2_LINK_DISABLE_MASK) {
		autoc2 &= ~IXGBE_AUTOC2_LINK_DISABLE_MASK;
		IXGBE_WRITE_REG(hw, IXGBE_AUTOC2, autoc2);
		IXGBE_WRITE_FLUSH(hw);
	}

	if (hw->mac.orig_link_settings_stored == FALSE) {
		hw->mac.orig_autoc = autoc;
		hw->mac.orig_autoc2 = autoc2;
		hw->mac.orig_link_settings_stored = TRUE;
	} else {

		/* If MNG FW is running on a multi-speed device that
		 * doesn't autoneg with out driver support we need to
		 * leave LMS in the state it was before we MAC reset.
		 * Likewise if we support WoL we don't want change the
		 * LMS state.
		 */
		if ((hw->phy.multispeed_fiber && ixgbe_mng_enabled(hw)) ||
		    hw->wol_enabled)
			hw->mac.orig_autoc =
				(hw->mac.orig_autoc & ~IXGBE_AUTOC_LMS_MASK) |
				curr_lms;

		if (autoc != hw->mac.orig_autoc) {
			status = hw->mac.ops.prot_autoc_write(hw,
							hw->mac.orig_autoc,
							FALSE);
			if (status != IXGBE_SUCCESS)
				goto reset_hw_out;
		}

		if ((autoc2 & IXGBE_AUTOC2_UPPER_MASK) !=
		    (hw->mac.orig_autoc2 & IXGBE_AUTOC2_UPPER_MASK)) {
			autoc2 &= ~IXGBE_AUTOC2_UPPER_MASK;
			autoc2 |= (hw->mac.orig_autoc2 &
				   IXGBE_AUTOC2_UPPER_MASK);
			IXGBE_WRITE_REG(hw, IXGBE_AUTOC2, autoc2);
		}
	}

	/* Store the permanent mac address */
	hw->mac.ops.get_mac_addr(hw, hw->mac.perm_addr);

	/*
	 * Store MAC address from RAR0, clear receive address registers, and
	 * clear the multicast table.  Also reset num_rar_entries to 128,
	 * since we modify this value when programming the SAN MAC address.
	 */
	hw->mac.num_rar_entries = 128;
	hw->mac.ops.init_rx_addrs(hw);

	/* Store the permanent SAN mac address */
	hw->mac.ops.get_san_mac_addr(hw, hw->mac.san_addr);

	/* Add the SAN MAC address to the RAR only if it's a valid address */
	if (ixgbe_validate_mac_addr(hw->mac.san_addr) == 0) {
		/* Save the SAN MAC RAR index */
		hw->mac.san_mac_rar_index = hw->mac.num_rar_entries - 1;

		hw->mac.ops.set_rar(hw, hw->mac.san_mac_rar_index,
				    hw->mac.san_addr, 0, IXGBE_RAH_AV);

		/* clear VMDq pool/queue selection for this RAR */
		hw->mac.ops.clear_vmdq(hw, hw->mac.san_mac_rar_index,
				       IXGBE_CLEAR_VMDQ_ALL);

		/* Reserve the last RAR for the SAN MAC address */
		hw->mac.num_rar_entries--;
	}

	/* Store the alternative WWNN/WWPN prefix */
	hw->mac.ops.get_wwn_prefix(hw, &hw->mac.wwnn_prefix,
				   &hw->mac.wwpn_prefix);

reset_hw_out:
	return status;
}

/**
 * ixgbe_fdir_check_cmd_complete - poll to check whether FDIRCMD is complete
 * @hw: pointer to hardware structure
 * @fdircmd: current value of FDIRCMD register
 */
static s32 ixgbe_fdir_check_cmd_complete(struct ixgbe_hw *hw, u32 *fdircmd)
{
	int i;

	for (i = 0; i < IXGBE_FDIRCMD_CMD_POLL; i++) {
		*fdircmd = IXGBE_READ_REG(hw, IXGBE_FDIRCMD);
		if (!(*fdircmd & IXGBE_FDIRCMD_CMD_MASK))
			return IXGBE_SUCCESS;
		usec_delay(10);
	}

	return IXGBE_ERR_FDIR_CMD_INCOMPLETE;
}

/**
 *  ixgbe_reinit_fdir_tables_82599 - Reinitialize Flow Director tables.
 *  @hw: pointer to hardware structure
 **/
s32 ixgbe_reinit_fdir_tables_82599(struct ixgbe_hw *hw)
{
	s32 err;
	int i;
	u32 fdirctrl = IXGBE_READ_REG(hw, IXGBE_FDIRCTRL);
	u32 fdircmd;
	fdirctrl &= ~IXGBE_FDIRCTRL_INIT_DONE;

	DEBUGFUNC("ixgbe_reinit_fdir_tables_82599");

	/*
	 * Before starting reinitialization process,
	 * FDIRCMD.CMD must be zero.
	 */
	err = ixgbe_fdir_check_cmd_complete(hw, &fdircmd);
	if (err) {
		DEBUGOUT("Flow Director previous command did not complete, aborting table re-initialization.\n");
		return err;
	}

	IXGBE_WRITE_REG(hw, IXGBE_FDIRFREE, 0);
	IXGBE_WRITE_FLUSH(hw);
	/*
	 * 82599 adapters flow director init flow cannot be restarted,
	 * Workaround 82599 silicon errata by performing the following steps
	 * before re-writing the FDIRCTRL control register with the same value.
	 * - write 1 to bit 8 of FDIRCMD register &
	 * - write 0 to bit 8 of FDIRCMD register
	 */
	IXGBE_WRITE_REG(hw, IXGBE_FDIRCMD,
			(IXGBE_READ_REG(hw, IXGBE_FDIRCMD) |
			 IXGBE_FDIRCMD_CLEARHT));
	IXGBE_WRITE_FLUSH(hw);
	IXGBE_WRITE_REG(hw, IXGBE_FDIRCMD,
			(IXGBE_READ_REG(hw, IXGBE_FDIRCMD) &
			 ~IXGBE_FDIRCMD_CLEARHT));
	IXGBE_WRITE_FLUSH(hw);
	/*
	 * Clear FDIR Hash register to clear any leftover hashes
	 * waiting to be programmed.
	 */
	IXGBE_WRITE_REG(hw, IXGBE_FDIRHASH, 0x00);
	IXGBE_WRITE_FLUSH(hw);

	IXGBE_WRITE_REG(hw, IXGBE_FDIRCTRL, fdirctrl);
	IXGBE_WRITE_FLUSH(hw);

	/* Poll init-done after we write FDIRCTRL register */
	for (i = 0; i < IXGBE_FDIR_INIT_DONE_POLL; i++) {
		if (IXGBE_READ_REG(hw, IXGBE_FDIRCTRL) &
				   IXGBE_FDIRCTRL_INIT_DONE)
			break;
		msec_delay(1);
	}
	if (i >= IXGBE_FDIR_INIT_DONE_POLL) {
		DEBUGOUT("Flow Director Signature poll time exceeded!\n");
		return IXGBE_ERR_FDIR_REINIT_FAILED;
	}

	/* Clear FDIR statistics registers (read to clear) */
	IXGBE_READ_REG(hw, IXGBE_FDIRUSTAT);
	IXGBE_READ_REG(hw, IXGBE_FDIRFSTAT);
	IXGBE_READ_REG(hw, IXGBE_FDIRMATCH);
	IXGBE_READ_REG(hw, IXGBE_FDIRMISS);
	IXGBE_READ_REG(hw, IXGBE_FDIRLEN);

	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_fdir_enable_82599 - Initialize Flow Director control registers
 *  @hw: pointer to hardware structure
 *  @fdirctrl: value to write to flow director control register
 **/
static void ixgbe_fdir_enable_82599(struct ixgbe_hw *hw, u32 fdirctrl)
{
	int i;

	DEBUGFUNC("ixgbe_fdir_enable_82599");

	/* Prime the keys for hashing */
	IXGBE_WRITE_REG(hw, IXGBE_FDIRHKEY, IXGBE_ATR_BUCKET_HASH_KEY);
	IXGBE_WRITE_REG(hw, IXGBE_FDIRSKEY, IXGBE_ATR_SIGNATURE_HASH_KEY);

	/*
	 * Poll init-done after we write the register.  Estimated times:
	 *      10G: PBALLOC = 11b, timing is 60us
	 *       1G: PBALLOC = 11b, timing is 600us
	 *     100M: PBALLOC = 11b, timing is 6ms
	 *
	 *     Multiple these timings by 4 if under full Rx load
	 *
	 * So we'll poll for IXGBE_FDIR_INIT_DONE_POLL times, sleeping for
	 * 1 msec per poll time.  If we're at line rate and drop to 100M, then
	 * this might not finish in our poll time, but we can live with that
	 * for now.
	 */
	IXGBE_WRITE_REG(hw, IXGBE_FDIRCTRL, fdirctrl);
	IXGBE_WRITE_FLUSH(hw);
	for (i = 0; i < IXGBE_FDIR_INIT_DONE_POLL; i++) {
		if (IXGBE_READ_REG(hw, IXGBE_FDIRCTRL) &
				   IXGBE_FDIRCTRL_INIT_DONE)
			break;
		msec_delay(1);
	}

	if (i >= IXGBE_FDIR_INIT_DONE_POLL)
		DEBUGOUT("Flow Director poll time exceeded!\n");
}

/**
 *  ixgbe_init_fdir_signature_82599 - Initialize Flow Director signature filters
 *  @hw: pointer to hardware structure
 *  @fdirctrl: value to write to flow director control register, initially
 *	     contains just the value of the Rx packet buffer allocation
 **/
s32 ixgbe_init_fdir_signature_82599(struct ixgbe_hw *hw, u32 fdirctrl)
{
	DEBUGFUNC("ixgbe_init_fdir_signature_82599");

	/*
	 * Continue setup of fdirctrl register bits:
	 *  Move the flexible bytes to use the ethertype - shift 6 words
	 *  Set the maximum length per hash bucket to 0xA filters
	 *  Send interrupt when 64 filters are left
	 */
	fdirctrl |= (0x6 << IXGBE_FDIRCTRL_FLEX_SHIFT) |
		    (0xA << IXGBE_FDIRCTRL_MAX_LENGTH_SHIFT) |
		    (4 << IXGBE_FDIRCTRL_FULL_THRESH_SHIFT);

	/* write hashes and fdirctrl register, poll for completion */
	ixgbe_fdir_enable_82599(hw, fdirctrl);

	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_init_fdir_perfect_82599 - Initialize Flow Director perfect filters
 *  @hw: pointer to hardware structure
 *  @fdirctrl: value to write to flow director control register, initially
 *	     contains just the value of the Rx packet buffer allocation
 *  @cloud_mode: TRUE - cloud mode, FALSE - other mode
 **/
s32 ixgbe_init_fdir_perfect_82599(struct ixgbe_hw *hw, u32 fdirctrl,
			bool cloud_mode)
{
	UNREFERENCED_1PARAMETER(cloud_mode);
	DEBUGFUNC("ixgbe_init_fdir_perfect_82599");

	/*
	 * Continue setup of fdirctrl register bits:
	 *  Turn perfect match filtering on
	 *  Report hash in RSS field of Rx wb descriptor
	 *  Initialize the drop queue to queue 127
	 *  Move the flexible bytes to use the ethertype - shift 6 words
	 *  Set the maximum length per hash bucket to 0xA filters
	 *  Send interrupt when 64 (0x4 * 16) filters are left
	 */
	fdirctrl |= IXGBE_FDIRCTRL_PERFECT_MATCH |
		    IXGBE_FDIRCTRL_REPORT_STATUS |
		    (IXGBE_FDIR_DROP_QUEUE << IXGBE_FDIRCTRL_DROP_Q_SHIFT) |
		    (0x6 << IXGBE_FDIRCTRL_FLEX_SHIFT) |
		    (0xA << IXGBE_FDIRCTRL_MAX_LENGTH_SHIFT) |
		    (4 << IXGBE_FDIRCTRL_FULL_THRESH_SHIFT);

	if (cloud_mode)
		fdirctrl |=(IXGBE_FDIRCTRL_FILTERMODE_CLOUD <<
					IXGBE_FDIRCTRL_FILTERMODE_SHIFT);

	/* write hashes and fdirctrl register, poll for completion */
	ixgbe_fdir_enable_82599(hw, fdirctrl);

	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_set_fdir_drop_queue_82599 - Set Flow Director drop queue
 *  @hw: pointer to hardware structure
 *  @dropqueue: Rx queue index used for the dropped packets
 **/
void ixgbe_set_fdir_drop_queue_82599(struct ixgbe_hw *hw, u8 dropqueue)
{
	u32 fdirctrl;

	DEBUGFUNC("ixgbe_set_fdir_drop_queue_82599");
	/* Clear init done bit and drop queue field */
	fdirctrl = IXGBE_READ_REG(hw, IXGBE_FDIRCTRL);
	fdirctrl &= ~(IXGBE_FDIRCTRL_DROP_Q_MASK | IXGBE_FDIRCTRL_INIT_DONE);

	/* Set drop queue */
	fdirctrl |= (dropqueue << IXGBE_FDIRCTRL_DROP_Q_SHIFT);
	if ((hw->mac.type == ixgbe_mac_X550) ||
	    (hw->mac.type == ixgbe_mac_X550EM_x) ||
	    (hw->mac.type == ixgbe_mac_X550EM_a))
		fdirctrl |= IXGBE_FDIRCTRL_DROP_NO_MATCH;

	IXGBE_WRITE_REG(hw, IXGBE_FDIRCMD,
			(IXGBE_READ_REG(hw, IXGBE_FDIRCMD) |
			 IXGBE_FDIRCMD_CLEARHT));
	IXGBE_WRITE_FLUSH(hw);
	IXGBE_WRITE_REG(hw, IXGBE_FDIRCMD,
			(IXGBE_READ_REG(hw, IXGBE_FDIRCMD) &
			 ~IXGBE_FDIRCMD_CLEARHT));
	IXGBE_WRITE_FLUSH(hw);

	/* write hashes and fdirctrl register, poll for completion */
	ixgbe_fdir_enable_82599(hw, fdirctrl);
}

/*
 * These defines allow us to quickly generate all of the necessary instructions
 * in the function below by simply calling out IXGBE_COMPUTE_SIG_HASH_ITERATION
 * for values 0 through 15
 */
#define IXGBE_ATR_COMMON_HASH_KEY \
		(IXGBE_ATR_BUCKET_HASH_KEY & IXGBE_ATR_SIGNATURE_HASH_KEY)
#define IXGBE_COMPUTE_SIG_HASH_ITERATION(_n) \
do { \
	u32 n = (_n); \
	if (IXGBE_ATR_COMMON_HASH_KEY & (0x01 << n)) \
		common_hash ^= lo_hash_dword >> n; \
	else if (IXGBE_ATR_BUCKET_HASH_KEY & (0x01 << n)) \
		bucket_hash ^= lo_hash_dword >> n; \
	else if (IXGBE_ATR_SIGNATURE_HASH_KEY & (0x01 << n)) \
		sig_hash ^= lo_hash_dword << (16 - n); \
	if (IXGBE_ATR_COMMON_HASH_KEY & (0x01 << (n + 16))) \
		common_hash ^= hi_hash_dword >> n; \
	else if (IXGBE_ATR_BUCKET_HASH_KEY & (0x01 << (n + 16))) \
		bucket_hash ^= hi_hash_dword >> n; \
	else if (IXGBE_ATR_SIGNATURE_HASH_KEY & (0x01 << (n + 16))) \
		sig_hash ^= hi_hash_dword << (16 - n); \
} while (0)

/**
 *  ixgbe_atr_compute_sig_hash_82599 - Compute the signature hash
 *  @input: input bitstream to compute the hash on
 *  @common: compressed common input dword
 *
 *  This function is almost identical to the function above but contains
 *  several optimizations such as unwinding all of the loops, letting the
 *  compiler work out all of the conditional ifs since the keys are static
 *  defines, and computing two keys at once since the hashed dword stream
 *  will be the same for both keys.
 **/
u32 ixgbe_atr_compute_sig_hash_82599(union ixgbe_atr_hash_dword input,
				     union ixgbe_atr_hash_dword common)
{
	u32 hi_hash_dword, lo_hash_dword, flow_vm_vlan;
	u32 sig_hash = 0, bucket_hash = 0, common_hash = 0;

	/* record the flow_vm_vlan bits as they are a key part to the hash */
	flow_vm_vlan = IXGBE_NTOHL(input.dword);

	/* generate common hash dword */
	hi_hash_dword = IXGBE_NTOHL(common.dword);

	/* low dword is word swapped version of common */
	lo_hash_dword = (hi_hash_dword >> 16) | (hi_hash_dword << 16);

	/* apply flow ID/VM pool/VLAN ID bits to hash words */
	hi_hash_dword ^= flow_vm_vlan ^ (flow_vm_vlan >> 16);

	/* Process bits 0 and 16 */
	IXGBE_COMPUTE_SIG_HASH_ITERATION(0);

	/*
	 * apply flow ID/VM pool/VLAN ID bits to lo hash dword, we had to
	 * delay this because bit 0 of the stream should not be processed
	 * so we do not add the VLAN until after bit 0 was processed
	 */
	lo_hash_dword ^= flow_vm_vlan ^ (flow_vm_vlan << 16);

	/* Process remaining 30 bit of the key */
	IXGBE_COMPUTE_SIG_HASH_ITERATION(1);
	IXGBE_COMPUTE_SIG_HASH_ITERATION(2);
	IXGBE_COMPUTE_SIG_HASH_ITERATION(3);
	IXGBE_COMPUTE_SIG_HASH_ITERATION(4);
	IXGBE_COMPUTE_SIG_HASH_ITERATION(5);
	IXGBE_COMPUTE_SIG_HASH_ITERATION(6);
	IXGBE_COMPUTE_SIG_HASH_ITERATION(7);
	IXGBE_COMPUTE_SIG_HASH_ITERATION(8);
	IXGBE_COMPUTE_SIG_HASH_ITERATION(9);
	IXGBE_COMPUTE_SIG_HASH_ITERATION(10);
	IXGBE_COMPUTE_SIG_HASH_ITERATION(11);
	IXGBE_COMPUTE_SIG_HASH_ITERATION(12);
	IXGBE_COMPUTE_SIG_HASH_ITERATION(13);
	IXGBE_COMPUTE_SIG_HASH_ITERATION(14);
	IXGBE_COMPUTE_SIG_HASH_ITERATION(15);

	/* combine common_hash result with signature and bucket hashes */
	bucket_hash ^= common_hash;
	bucket_hash &= IXGBE_ATR_HASH_MASK;

	sig_hash ^= common_hash << 16;
	sig_hash &= IXGBE_ATR_HASH_MASK << 16;

	/* return completed signature hash */
	return sig_hash ^ bucket_hash;
}

/**
 *  ixgbe_atr_add_signature_filter_82599 - Adds a signature hash filter
 *  @hw: pointer to hardware structure
 *  @input: unique input dword
 *  @common: compressed common input dword
 *  @queue: queue index to direct traffic to
 *
 * Note that the tunnel bit in input must not be set when the hardware
 * tunneling support does not exist.
 **/
void ixgbe_fdir_add_signature_filter_82599(struct ixgbe_hw *hw,
					   union ixgbe_atr_hash_dword input,
					   union ixgbe_atr_hash_dword common,
					   u8 queue)
{
	u64 fdirhashcmd;
	u8 flow_type;
	bool tunnel;
	u32 fdircmd;

	DEBUGFUNC("ixgbe_fdir_add_signature_filter_82599");

	/*
	 * Get the flow_type in order to program FDIRCMD properly
	 * lowest 2 bits are FDIRCMD.L4TYPE, third lowest bit is FDIRCMD.IPV6
	 * fifth is FDIRCMD.TUNNEL_FILTER
	 */
	tunnel = !!(input.formatted.flow_type & IXGBE_ATR_L4TYPE_TUNNEL_MASK);
	flow_type = input.formatted.flow_type &
		    (IXGBE_ATR_L4TYPE_TUNNEL_MASK - 1);
	switch (flow_type) {
	case IXGBE_ATR_FLOW_TYPE_TCPV4:
	case IXGBE_ATR_FLOW_TYPE_UDPV4:
	case IXGBE_ATR_FLOW_TYPE_SCTPV4:
	case IXGBE_ATR_FLOW_TYPE_TCPV6:
	case IXGBE_ATR_FLOW_TYPE_UDPV6:
	case IXGBE_ATR_FLOW_TYPE_SCTPV6:
		break;
	default:
		DEBUGOUT(" Error on flow type input\n");
		return;
	}

	/* configure FDIRCMD register */
	fdircmd = IXGBE_FDIRCMD_CMD_ADD_FLOW | IXGBE_FDIRCMD_FILTER_UPDATE |
		  IXGBE_FDIRCMD_LAST | IXGBE_FDIRCMD_QUEUE_EN;
	fdircmd |= (u32)flow_type << IXGBE_FDIRCMD_FLOW_TYPE_SHIFT;
	fdircmd |= (u32)queue << IXGBE_FDIRCMD_RX_QUEUE_SHIFT;
	if (tunnel)
		fdircmd |= IXGBE_FDIRCMD_TUNNEL_FILTER;

	/*
	 * The lower 32-bits of fdirhashcmd is for FDIRHASH, the upper 32-bits
	 * is for FDIRCMD.  Then do a 64-bit register write from FDIRHASH.
	 */
	fdirhashcmd = (u64)fdircmd << 32;
	fdirhashcmd |= ixgbe_atr_compute_sig_hash_82599(input, common);
	IXGBE_WRITE_REG64(hw, IXGBE_FDIRHASH, fdirhashcmd);

	DEBUGOUT2("Tx Queue=%x hash=%x\n", queue, (u32)fdirhashcmd);

	return;
}

#define IXGBE_COMPUTE_BKT_HASH_ITERATION(_n) \
do { \
	u32 n = (_n); \
	if (IXGBE_ATR_BUCKET_HASH_KEY & (0x01 << n)) \
		bucket_hash ^= lo_hash_dword >> n; \
	if (IXGBE_ATR_BUCKET_HASH_KEY & (0x01 << (n + 16))) \
		bucket_hash ^= hi_hash_dword >> n; \
} while (0)

/**
 *  ixgbe_atr_compute_perfect_hash_82599 - Compute the perfect filter hash
 *  @input: input bitstream to compute the hash on
 *  @input_mask: mask for the input bitstream
 *
 *  This function serves two main purposes.  First it applies the input_mask
 *  to the atr_input resulting in a cleaned up atr_input data stream.
 *  Secondly it computes the hash and stores it in the bkt_hash field at
 *  the end of the input byte stream.  This way it will be available for
 *  future use without needing to recompute the hash.
 **/
void ixgbe_atr_compute_perfect_hash_82599(union ixgbe_atr_input *input,
					  union ixgbe_atr_input *input_mask)
{

	u32 hi_hash_dword, lo_hash_dword, flow_vm_vlan;
	u32 bucket_hash = 0;
	u32 hi_dword = 0;
	u32 i = 0;

	/* Apply masks to input data */
	for (i = 0; i < 14; i++)
		input->dword_stream[i]  &= input_mask->dword_stream[i];

	/* record the flow_vm_vlan bits as they are a key part to the hash */
	flow_vm_vlan = IXGBE_NTOHL(input->dword_stream[0]);

	/* generate common hash dword */
	for (i = 1; i <= 13; i++)
		hi_dword ^= input->dword_stream[i];
	hi_hash_dword = IXGBE_NTOHL(hi_dword);

	/* low dword is word swapped version of common */
	lo_hash_dword = (hi_hash_dword >> 16) | (hi_hash_dword << 16);

	/* apply flow ID/VM pool/VLAN ID bits to hash words */
	hi_hash_dword ^= flow_vm_vlan ^ (flow_vm_vlan >> 16);

	/* Process bits 0 and 16 */
	IXGBE_COMPUTE_BKT_HASH_ITERATION(0);

	/*
	 * apply flow ID/VM pool/VLAN ID bits to lo hash dword, we had to
	 * delay this because bit 0 of the stream should not be processed
	 * so we do not add the VLAN until after bit 0 was processed
	 */
	lo_hash_dword ^= flow_vm_vlan ^ (flow_vm_vlan << 16);

	/* Process remaining 30 bit of the key */
	for (i = 1; i <= 15; i++)
		IXGBE_COMPUTE_BKT_HASH_ITERATION(i);

	/*
	 * Limit hash to 13 bits since max bucket count is 8K.
	 * Store result at the end of the input stream.
	 */
	input->formatted.bkt_hash = bucket_hash & 0x1FFF;
}

/**
 *  ixgbe_get_fdirtcpm_82599 - generate a TCP port from atr_input_masks
 *  @input_mask: mask to be bit swapped
 *
 *  The source and destination port masks for flow director are bit swapped
 *  in that bit 15 effects bit 0, 14 effects 1, 13, 2 etc.  In order to
 *  generate a correctly swapped value we need to bit swap the mask and that
 *  is what is accomplished by this function.
 **/
static u32 ixgbe_get_fdirtcpm_82599(union ixgbe_atr_input *input_mask)
{
	u32 mask = IXGBE_NTOHS(input_mask->formatted.dst_port);
	mask <<= IXGBE_FDIRTCPM_DPORTM_SHIFT;
	mask |= IXGBE_NTOHS(input_mask->formatted.src_port);
	mask = ((mask & 0x55555555) << 1) | ((mask & 0xAAAAAAAA) >> 1);
	mask = ((mask & 0x33333333) << 2) | ((mask & 0xCCCCCCCC) >> 2);
	mask = ((mask & 0x0F0F0F0F) << 4) | ((mask & 0xF0F0F0F0) >> 4);
	return ((mask & 0x00FF00FF) << 8) | ((mask & 0xFF00FF00) >> 8);
}

/*
 * These two macros are meant to address the fact that we have registers
 * that are either all or in part big-endian.  As a result on big-endian
 * systems we will end up byte swapping the value to little-endian before
 * it is byte swapped again and written to the hardware in the original
 * big-endian format.
 */
#define IXGBE_STORE_AS_BE32(_value) \
	(((u32)(_value) >> 24) | (((u32)(_value) & 0x00FF0000) >> 8) | \
	 (((u32)(_value) & 0x0000FF00) << 8) | ((u32)(_value) << 24))

#define IXGBE_WRITE_REG_BE32(a, reg, value) \
	IXGBE_WRITE_REG((a), (reg), IXGBE_STORE_AS_BE32(IXGBE_NTOHL(value)))

#define IXGBE_STORE_AS_BE16(_value) \
	IXGBE_NTOHS(((u16)(_value) >> 8) | ((u16)(_value) << 8))

s32 ixgbe_fdir_set_input_mask_82599(struct ixgbe_hw *hw,
				    union ixgbe_atr_input *input_mask, bool cloud_mode)
{
	/* mask IPv6 since it is currently not supported */
	u32 fdirm = IXGBE_FDIRM_DIPv6;
	u32 fdirtcpm;
	u32 fdirip6m;
	UNREFERENCED_1PARAMETER(cloud_mode);
	DEBUGFUNC("ixgbe_fdir_set_atr_input_mask_82599");

	/*
	 * Program the relevant mask registers.  If src/dst_port or src/dst_addr
	 * are zero, then assume a full mask for that field.  Also assume that
	 * a VLAN of 0 is unspecified, so mask that out as well.  L4type
	 * cannot be masked out in this implementation.
	 *
	 * This also assumes IPv4 only.  IPv6 masking isn't supported at this
	 * point in time.
	 */

	/* verify bucket hash is cleared on hash generation */
	if (input_mask->formatted.bkt_hash)
		DEBUGOUT(" bucket hash should always be 0 in mask\n");

	/* Program FDIRM and verify partial masks */
	switch (input_mask->formatted.vm_pool & 0x7F) {
	case 0x0:
		fdirm |= IXGBE_FDIRM_POOL;
	case 0x7F:
		break;
	default:
		DEBUGOUT(" Error on vm pool mask\n");
		return IXGBE_ERR_CONFIG;
	}

	switch (input_mask->formatted.flow_type & IXGBE_ATR_L4TYPE_MASK) {
	case 0x0:
		fdirm |= IXGBE_FDIRM_L4P;
		if (input_mask->formatted.dst_port ||
		    input_mask->formatted.src_port) {
			DEBUGOUT(" Error on src/dst port mask\n");
			return IXGBE_ERR_CONFIG;
		}
	case IXGBE_ATR_L4TYPE_MASK:
		break;
	default:
		DEBUGOUT(" Error on flow type mask\n");
		return IXGBE_ERR_CONFIG;
	}

	switch (IXGBE_NTOHS(input_mask->formatted.vlan_id) & 0xEFFF) {
	case 0x0000:
		/* mask VLAN ID */
		fdirm |= IXGBE_FDIRM_VLANID;
		/* FALLTHROUGH */
	case 0x0FFF:
		/* mask VLAN priority */
		fdirm |= IXGBE_FDIRM_VLANP;
		break;
	case 0xE000:
		/* mask VLAN ID only */
		fdirm |= IXGBE_FDIRM_VLANID;
		/* fall through */
	case 0xEFFF:
		/* no VLAN fields masked */
		break;
	default:
		DEBUGOUT(" Error on VLAN mask\n");
		return IXGBE_ERR_CONFIG;
	}

	switch (input_mask->formatted.flex_bytes & 0xFFFF) {
	case 0x0000:
		/* Mask Flex Bytes */
		fdirm |= IXGBE_FDIRM_FLEX;
		/* fall through */
	case 0xFFFF:
		break;
	default:
		DEBUGOUT(" Error on flexible byte mask\n");
		return IXGBE_ERR_CONFIG;
	}

	if (cloud_mode) {
		fdirm |= IXGBE_FDIRM_L3P;
		fdirip6m = ((u32) 0xFFFFU << IXGBE_FDIRIP6M_DIPM_SHIFT);
		fdirip6m |= IXGBE_FDIRIP6M_ALWAYS_MASK;

		switch (input_mask->formatted.inner_mac[0] & 0xFF) {
		case 0x00:
			/* Mask inner MAC, fall through */
			fdirip6m |= IXGBE_FDIRIP6M_INNER_MAC;
		case 0xFF:
			break;
		default:
			DEBUGOUT(" Error on inner_mac byte mask\n");
			return IXGBE_ERR_CONFIG;
		}

		switch (input_mask->formatted.tni_vni & 0xFFFFFFFF) {
		case 0x0:
			/* Mask vxlan id */
			fdirip6m |= IXGBE_FDIRIP6M_TNI_VNI;
			break;
		case 0x00FFFFFF:
			fdirip6m |= IXGBE_FDIRIP6M_TNI_VNI_24;
			break;
		case 0xFFFFFFFF:
			break;
		default:
			DEBUGOUT(" Error on TNI/VNI byte mask\n");
			return IXGBE_ERR_CONFIG;
		}

		switch (input_mask->formatted.tunnel_type & 0xFFFF) {
		case 0x0:
			/* Mask turnnel type, fall through */
			fdirip6m |= IXGBE_FDIRIP6M_TUNNEL_TYPE;
		case 0xFFFF:
			break;
		default:
			DEBUGOUT(" Error on tunnel type byte mask\n");
			return IXGBE_ERR_CONFIG;
		}
		IXGBE_WRITE_REG_BE32(hw, IXGBE_FDIRIP6M, fdirip6m);

		/* Set all bits in FDIRTCPM, FDIRUDPM, FDIRSCTPM,
		 * FDIRSIP4M and FDIRDIP4M in cloud mode to allow
		 * L3/L3 packets to tunnel.
		 */
		IXGBE_WRITE_REG(hw, IXGBE_FDIRTCPM, 0xFFFFFFFF);
		IXGBE_WRITE_REG(hw, IXGBE_FDIRUDPM, 0xFFFFFFFF);
		IXGBE_WRITE_REG_BE32(hw, IXGBE_FDIRDIP4M, 0xFFFFFFFF);
		IXGBE_WRITE_REG_BE32(hw, IXGBE_FDIRSIP4M, 0xFFFFFFFF);
		switch (hw->mac.type) {
		case ixgbe_mac_X550:
		case ixgbe_mac_X550EM_x:
		case ixgbe_mac_X550EM_a:
			IXGBE_WRITE_REG(hw, IXGBE_FDIRSCTPM, 0xFFFFFFFF);
			break;
		default:
			break;
		}
	}

	/* Now mask VM pool and destination IPv6 - bits 5 and 2 */
	IXGBE_WRITE_REG(hw, IXGBE_FDIRM, fdirm);

	if (!cloud_mode) {
		/* store the TCP/UDP port masks, bit reversed from port
		 * layout */
		fdirtcpm = ixgbe_get_fdirtcpm_82599(input_mask);

		/* write both the same so that UDP and TCP use the same mask */
		IXGBE_WRITE_REG(hw, IXGBE_FDIRTCPM, ~fdirtcpm);
		IXGBE_WRITE_REG(hw, IXGBE_FDIRUDPM, ~fdirtcpm);
		/* also use it for SCTP */
		switch (hw->mac.type) {
		case ixgbe_mac_X550:
		case ixgbe_mac_X550EM_x:
		case ixgbe_mac_X550EM_a:
			IXGBE_WRITE_REG(hw, IXGBE_FDIRSCTPM, ~fdirtcpm);
			break;
		default:
			break;
		}

		/* store source and destination IP masks (big-enian) */
		IXGBE_WRITE_REG_BE32(hw, IXGBE_FDIRSIP4M,
				     ~input_mask->formatted.src_ip[0]);
		IXGBE_WRITE_REG_BE32(hw, IXGBE_FDIRDIP4M,
				     ~input_mask->formatted.dst_ip[0]);
	}
	return IXGBE_SUCCESS;
}

s32 ixgbe_fdir_write_perfect_filter_82599(struct ixgbe_hw *hw,
					  union ixgbe_atr_input *input,
					  u16 soft_id, u8 queue, bool cloud_mode)
{
	u32 fdirport, fdirvlan, fdirhash, fdircmd;
	u32 addr_low, addr_high;
	u32 cloud_type = 0;
	s32 err;
	UNREFERENCED_1PARAMETER(cloud_mode);

	DEBUGFUNC("ixgbe_fdir_write_perfect_filter_82599");
	if (!cloud_mode) {
		/* currently IPv6 is not supported, must be programmed with 0 */
		IXGBE_WRITE_REG_BE32(hw, IXGBE_FDIRSIPv6(0),
				     input->formatted.src_ip[0]);
		IXGBE_WRITE_REG_BE32(hw, IXGBE_FDIRSIPv6(1),
				     input->formatted.src_ip[1]);
		IXGBE_WRITE_REG_BE32(hw, IXGBE_FDIRSIPv6(2),
				     input->formatted.src_ip[2]);

		/* record the source address (big-endian) */
		IXGBE_WRITE_REG_BE32(hw, IXGBE_FDIRIPSA,
			input->formatted.src_ip[0]);

		/* record the first 32 bits of the destination address
		 * (big-endian) */
		IXGBE_WRITE_REG_BE32(hw, IXGBE_FDIRIPDA,
			input->formatted.dst_ip[0]);

		/* record source and destination port (little-endian)*/
		fdirport = IXGBE_NTOHS(input->formatted.dst_port);
		fdirport <<= IXGBE_FDIRPORT_DESTINATION_SHIFT;
		fdirport |= IXGBE_NTOHS(input->formatted.src_port);
		IXGBE_WRITE_REG(hw, IXGBE_FDIRPORT, fdirport);
	}

	/* record VLAN (little-endian) and flex_bytes(big-endian) */
	fdirvlan = IXGBE_STORE_AS_BE16(input->formatted.flex_bytes);
	fdirvlan <<= IXGBE_FDIRVLAN_FLEX_SHIFT;
	fdirvlan |= IXGBE_NTOHS(input->formatted.vlan_id);
	IXGBE_WRITE_REG(hw, IXGBE_FDIRVLAN, fdirvlan);

	if (cloud_mode) {
		if (input->formatted.tunnel_type != 0)
			cloud_type = 0x80000000;

		addr_low = ((u32)input->formatted.inner_mac[0] |
				((u32)input->formatted.inner_mac[1] << 8) |
				((u32)input->formatted.inner_mac[2] << 16) |
				((u32)input->formatted.inner_mac[3] << 24));
		addr_high = ((u32)input->formatted.inner_mac[4] |
				((u32)input->formatted.inner_mac[5] << 8));
		cloud_type |= addr_high;
		IXGBE_WRITE_REG_BE32(hw, IXGBE_FDIRSIPv6(0), addr_low);
		IXGBE_WRITE_REG_BE32(hw, IXGBE_FDIRSIPv6(1), cloud_type);
		IXGBE_WRITE_REG_BE32(hw, IXGBE_FDIRSIPv6(2), input->formatted.tni_vni);
	}

	/* configure FDIRHASH register */
	fdirhash = input->formatted.bkt_hash;
	fdirhash |= soft_id << IXGBE_FDIRHASH_SIG_SW_INDEX_SHIFT;
	IXGBE_WRITE_REG(hw, IXGBE_FDIRHASH, fdirhash);

	/*
	 * flush all previous writes to make certain registers are
	 * programmed prior to issuing the command
	 */
	IXGBE_WRITE_FLUSH(hw);

	/* configure FDIRCMD register */
	fdircmd = IXGBE_FDIRCMD_CMD_ADD_FLOW | IXGBE_FDIRCMD_FILTER_UPDATE |
		  IXGBE_FDIRCMD_LAST | IXGBE_FDIRCMD_QUEUE_EN;
	if (queue == IXGBE_FDIR_DROP_QUEUE)
		fdircmd |= IXGBE_FDIRCMD_DROP;
	if (input->formatted.flow_type & IXGBE_ATR_L4TYPE_TUNNEL_MASK)
		fdircmd |= IXGBE_FDIRCMD_TUNNEL_FILTER;
	fdircmd |= input->formatted.flow_type << IXGBE_FDIRCMD_FLOW_TYPE_SHIFT;
	fdircmd |= (u32)queue << IXGBE_FDIRCMD_RX_QUEUE_SHIFT;
	fdircmd |= (u32)input->formatted.vm_pool << IXGBE_FDIRCMD_VT_POOL_SHIFT;

	IXGBE_WRITE_REG(hw, IXGBE_FDIRCMD, fdircmd);
	err = ixgbe_fdir_check_cmd_complete(hw, &fdircmd);
	if (err) {
		DEBUGOUT("Flow Director command did not complete!\n");
		return err;
	}

	return IXGBE_SUCCESS;
}

s32 ixgbe_fdir_erase_perfect_filter_82599(struct ixgbe_hw *hw,
					  union ixgbe_atr_input *input,
					  u16 soft_id)
{
	u32 fdirhash;
	u32 fdircmd;
	s32 err;

	/* configure FDIRHASH register */
	fdirhash = input->formatted.bkt_hash;
	fdirhash |= soft_id << IXGBE_FDIRHASH_SIG_SW_INDEX_SHIFT;
	IXGBE_WRITE_REG(hw, IXGBE_FDIRHASH, fdirhash);

	/* flush hash to HW */
	IXGBE_WRITE_FLUSH(hw);

	/* Query if filter is present */
	IXGBE_WRITE_REG(hw, IXGBE_FDIRCMD, IXGBE_FDIRCMD_CMD_QUERY_REM_FILT);

	err = ixgbe_fdir_check_cmd_complete(hw, &fdircmd);
	if (err) {
		DEBUGOUT("Flow Director command did not complete!\n");
		return err;
	}

	/* if filter exists in hardware then remove it */
	if (fdircmd & IXGBE_FDIRCMD_FILTER_VALID) {
		IXGBE_WRITE_REG(hw, IXGBE_FDIRHASH, fdirhash);
		IXGBE_WRITE_FLUSH(hw);
		IXGBE_WRITE_REG(hw, IXGBE_FDIRCMD,
				IXGBE_FDIRCMD_CMD_REMOVE_FLOW);
	}

	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_fdir_add_perfect_filter_82599 - Adds a perfect filter
 *  @hw: pointer to hardware structure
 *  @input: input bitstream
 *  @input_mask: mask for the input bitstream
 *  @soft_id: software index for the filters
 *  @queue: queue index to direct traffic to
 *  @cloud_mode: unused
 *
 *  Note that the caller to this function must lock before calling, since the
 *  hardware writes must be protected from one another.
 **/
s32 ixgbe_fdir_add_perfect_filter_82599(struct ixgbe_hw *hw,
					union ixgbe_atr_input *input,
					union ixgbe_atr_input *input_mask,
					u16 soft_id, u8 queue, bool cloud_mode)
{
	s32 err = IXGBE_ERR_CONFIG;
	UNREFERENCED_1PARAMETER(cloud_mode);

	DEBUGFUNC("ixgbe_fdir_add_perfect_filter_82599");

	/*
	 * Check flow_type formatting, and bail out before we touch the hardware
	 * if there's a configuration issue
	 */
	switch (input->formatted.flow_type) {
	case IXGBE_ATR_FLOW_TYPE_IPV4:
	case IXGBE_ATR_FLOW_TYPE_TUNNELED_IPV4:
		input_mask->formatted.flow_type = IXGBE_ATR_L4TYPE_IPV6_MASK;
		if (input->formatted.dst_port || input->formatted.src_port) {
			DEBUGOUT(" Error on src/dst port\n");
			return IXGBE_ERR_CONFIG;
		}
		break;
	case IXGBE_ATR_FLOW_TYPE_SCTPV4:
	case IXGBE_ATR_FLOW_TYPE_TUNNELED_SCTPV4:
		if (input->formatted.dst_port || input->formatted.src_port) {
			DEBUGOUT(" Error on src/dst port\n");
			return IXGBE_ERR_CONFIG;
		}
		/* FALLTHROUGH */
	case IXGBE_ATR_FLOW_TYPE_TCPV4:
	case IXGBE_ATR_FLOW_TYPE_TUNNELED_TCPV4:
	case IXGBE_ATR_FLOW_TYPE_UDPV4:
	case IXGBE_ATR_FLOW_TYPE_TUNNELED_UDPV4:
		input_mask->formatted.flow_type = IXGBE_ATR_L4TYPE_IPV6_MASK |
						  IXGBE_ATR_L4TYPE_MASK;
		break;
	default:
		DEBUGOUT(" Error on flow type input\n");
		return err;
	}

	/* program input mask into the HW */
	err = ixgbe_fdir_set_input_mask_82599(hw, input_mask, cloud_mode);
	if (err)
		return err;

	/* apply mask and compute/store hash */
	ixgbe_atr_compute_perfect_hash_82599(input, input_mask);

	/* program filters to filter memory */
	return ixgbe_fdir_write_perfect_filter_82599(hw, input,
						     soft_id, queue, cloud_mode);
}

/**
 *  ixgbe_read_analog_reg8_82599 - Reads 8 bit Omer analog register
 *  @hw: pointer to hardware structure
 *  @reg: analog register to read
 *  @val: read value
 *
 *  Performs read operation to Omer analog register specified.
 **/
s32 ixgbe_read_analog_reg8_82599(struct ixgbe_hw *hw, u32 reg, u8 *val)
{
	u32  core_ctl;

	DEBUGFUNC("ixgbe_read_analog_reg8_82599");

	IXGBE_WRITE_REG(hw, IXGBE_CORECTL, IXGBE_CORECTL_WRITE_CMD |
			(reg << 8));
	IXGBE_WRITE_FLUSH(hw);
	usec_delay(10);
	core_ctl = IXGBE_READ_REG(hw, IXGBE_CORECTL);
	*val = (u8)core_ctl;

	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_write_analog_reg8_82599 - Writes 8 bit Omer analog register
 *  @hw: pointer to hardware structure
 *  @reg: atlas register to write
 *  @val: value to write
 *
 *  Performs write operation to Omer analog register specified.
 **/
s32 ixgbe_write_analog_reg8_82599(struct ixgbe_hw *hw, u32 reg, u8 val)
{
	u32  core_ctl;

	DEBUGFUNC("ixgbe_write_analog_reg8_82599");

	core_ctl = (reg << 8) | val;
	IXGBE_WRITE_REG(hw, IXGBE_CORECTL, core_ctl);
	IXGBE_WRITE_FLUSH(hw);
	usec_delay(10);

	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_start_hw_82599 - Prepare hardware for Tx/Rx
 *  @hw: pointer to hardware structure
 *
 *  Starts the hardware using the generic start_hw function
 *  and the generation start_hw function.
 *  Then performs revision-specific operations, if any.
 **/
s32 ixgbe_start_hw_82599(struct ixgbe_hw *hw)
{
	s32 ret_val = IXGBE_SUCCESS;

	DEBUGFUNC("ixgbe_start_hw_82599");

	ret_val = ixgbe_start_hw_generic(hw);
	if (ret_val != IXGBE_SUCCESS)
		goto out;

	ret_val = ixgbe_start_hw_gen2(hw);
	if (ret_val != IXGBE_SUCCESS)
		goto out;

	/* We need to run link autotry after the driver loads */
	hw->mac.autotry_restart = TRUE;

	if (ret_val == IXGBE_SUCCESS)
		ret_val = ixgbe_verify_fw_version_82599(hw);
out:
	return ret_val;
}

/**
 *  ixgbe_identify_phy_82599 - Get physical layer module
 *  @hw: pointer to hardware structure
 *
 *  Determines the physical layer module found on the current adapter.
 *  If PHY already detected, maintains current PHY type in hw struct,
 *  otherwise executes the PHY detection routine.
 **/
s32 ixgbe_identify_phy_82599(struct ixgbe_hw *hw)
{
	s32 status;

	DEBUGFUNC("ixgbe_identify_phy_82599");

	/* Detect PHY if not unknown - returns success if already detected. */
	status = ixgbe_identify_phy_generic(hw);
	if (status != IXGBE_SUCCESS) {
		/* 82599 10GBASE-T requires an external PHY */
		if (hw->mac.ops.get_media_type(hw) == ixgbe_media_type_copper)
			return status;
		else
			status = ixgbe_identify_module_generic(hw);
	}

	/* Set PHY type none if no PHY detected */
	if (hw->phy.type == ixgbe_phy_unknown) {
		hw->phy.type = ixgbe_phy_none;
		return IXGBE_SUCCESS;
	}

	/* Return error if SFP module has been detected but is not supported */
	if (hw->phy.type == ixgbe_phy_sfp_unsupported)
		return IXGBE_ERR_SFP_NOT_SUPPORTED;

	return status;
}

/**
 *  ixgbe_get_supported_physical_layer_82599 - Returns physical layer type
 *  @hw: pointer to hardware structure
 *
 *  Determines physical layer capabilities of the current configuration.
 **/
u64 ixgbe_get_supported_physical_layer_82599(struct ixgbe_hw *hw)
{
	u64 physical_layer = IXGBE_PHYSICAL_LAYER_UNKNOWN;
	u32 autoc = IXGBE_READ_REG(hw, IXGBE_AUTOC);
	u32 autoc2 = IXGBE_READ_REG(hw, IXGBE_AUTOC2);
	u32 pma_pmd_10g_serial = autoc2 & IXGBE_AUTOC2_10G_SERIAL_PMA_PMD_MASK;
	u32 pma_pmd_10g_parallel = autoc & IXGBE_AUTOC_10G_PMA_PMD_MASK;
	u32 pma_pmd_1g = autoc & IXGBE_AUTOC_1G_PMA_PMD_MASK;
	u16 ext_ability = 0;

	DEBUGFUNC("ixgbe_get_support_physical_layer_82599");

	hw->phy.ops.identify(hw);

	switch (hw->phy.type) {
	case ixgbe_phy_tn:
	case ixgbe_phy_cu_unknown:
		hw->phy.ops.read_reg(hw, IXGBE_MDIO_PHY_EXT_ABILITY,
		IXGBE_MDIO_PMA_PMD_DEV_TYPE, &ext_ability);
		if (ext_ability & IXGBE_MDIO_PHY_10GBASET_ABILITY)
			physical_layer |= IXGBE_PHYSICAL_LAYER_10GBASE_T;
		if (ext_ability & IXGBE_MDIO_PHY_1000BASET_ABILITY)
			physical_layer |= IXGBE_PHYSICAL_LAYER_1000BASE_T;
		if (ext_ability & IXGBE_MDIO_PHY_100BASETX_ABILITY)
			physical_layer |= IXGBE_PHYSICAL_LAYER_100BASE_TX;
		goto out;
	default:
		break;
	}

	switch (autoc & IXGBE_AUTOC_LMS_MASK) {
	case IXGBE_AUTOC_LMS_1G_AN:
	case IXGBE_AUTOC_LMS_1G_LINK_NO_AN:
		if (pma_pmd_1g == IXGBE_AUTOC_1G_KX_BX) {
			physical_layer = IXGBE_PHYSICAL_LAYER_1000BASE_KX |
			    IXGBE_PHYSICAL_LAYER_1000BASE_BX;
			goto out;
		} else
			/* SFI mode so read SFP module */
			goto sfp_check;
		break;
	case IXGBE_AUTOC_LMS_10G_LINK_NO_AN:
		if (pma_pmd_10g_parallel == IXGBE_AUTOC_10G_CX4)
			physical_layer = IXGBE_PHYSICAL_LAYER_10GBASE_CX4;
		else if (pma_pmd_10g_parallel == IXGBE_AUTOC_10G_KX4)
			physical_layer = IXGBE_PHYSICAL_LAYER_10GBASE_KX4;
		else if (pma_pmd_10g_parallel == IXGBE_AUTOC_10G_XAUI)
			physical_layer = IXGBE_PHYSICAL_LAYER_10GBASE_XAUI;
		goto out;
		break;
	case IXGBE_AUTOC_LMS_10G_SERIAL:
		if (pma_pmd_10g_serial == IXGBE_AUTOC2_10G_KR) {
			physical_layer = IXGBE_PHYSICAL_LAYER_10GBASE_KR;
			goto out;
		} else if (pma_pmd_10g_serial == IXGBE_AUTOC2_10G_SFI)
			goto sfp_check;
		break;
	case IXGBE_AUTOC_LMS_KX4_KX_KR:
	case IXGBE_AUTOC_LMS_KX4_KX_KR_1G_AN:
		if (autoc & IXGBE_AUTOC_KX_SUPP)
			physical_layer |= IXGBE_PHYSICAL_LAYER_1000BASE_KX;
		if (autoc & IXGBE_AUTOC_KX4_SUPP)
			physical_layer |= IXGBE_PHYSICAL_LAYER_10GBASE_KX4;
		if (autoc & IXGBE_AUTOC_KR_SUPP)
			physical_layer |= IXGBE_PHYSICAL_LAYER_10GBASE_KR;
		goto out;
		break;
	default:
		goto out;
		break;
	}

sfp_check:
	/* SFP check must be done last since DA modules are sometimes used to
	 * test KR mode -  we need to id KR mode correctly before SFP module.
	 * Call identify_sfp because the pluggable module may have changed */
	physical_layer = ixgbe_get_supported_phy_sfp_layer_generic(hw);
out:
	return physical_layer;
}

/**
 *  ixgbe_enable_rx_dma_82599 - Enable the Rx DMA unit on 82599
 *  @hw: pointer to hardware structure
 *  @regval: register value to write to RXCTRL
 *
 *  Enables the Rx DMA unit for 82599
 **/
s32 ixgbe_enable_rx_dma_82599(struct ixgbe_hw *hw, u32 regval)
{

	DEBUGFUNC("ixgbe_enable_rx_dma_82599");

	/*
	 * Workaround for 82599 silicon errata when enabling the Rx datapath.
	 * If traffic is incoming before we enable the Rx unit, it could hang
	 * the Rx DMA unit.  Therefore, make sure the security engine is
	 * completely disabled prior to enabling the Rx unit.
	 */

	hw->mac.ops.disable_sec_rx_path(hw);

	if (regval & IXGBE_RXCTRL_RXEN)
		ixgbe_enable_rx(hw);
	else
		ixgbe_disable_rx(hw);

	hw->mac.ops.enable_sec_rx_path(hw);

	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_verify_fw_version_82599 - verify FW version for 82599
 *  @hw: pointer to hardware structure
 *
 *  Verifies that installed the firmware version is 0.6 or higher
 *  for SFI devices. All 82599 SFI devices should have version 0.6 or higher.
 *
 *  Returns IXGBE_ERR_EEPROM_VERSION if the FW is not present or
 *  if the FW version is not supported.
 **/
static s32 ixgbe_verify_fw_version_82599(struct ixgbe_hw *hw)
{
	s32 status = IXGBE_ERR_EEPROM_VERSION;
	u16 fw_offset, fw_ptp_cfg_offset;
	u16 fw_version;

	DEBUGFUNC("ixgbe_verify_fw_version_82599");

	/* firmware check is only necessary for SFI devices */
	if (hw->phy.media_type != ixgbe_media_type_fiber) {
		status = IXGBE_SUCCESS;
		goto fw_version_out;
	}

	/* get the offset to the Firmware Module block */
	if (hw->eeprom.ops.read(hw, IXGBE_FW_PTR, &fw_offset)) {
		ERROR_REPORT2(IXGBE_ERROR_INVALID_STATE,
			      "eeprom read at offset %d failed", IXGBE_FW_PTR);
		return IXGBE_ERR_EEPROM_VERSION;
	}

	if ((fw_offset == 0) || (fw_offset == 0xFFFF))
		goto fw_version_out;

	/* get the offset to the Pass Through Patch Configuration block */
	if (hw->eeprom.ops.read(hw, (fw_offset +
				 IXGBE_FW_PASSTHROUGH_PATCH_CONFIG_PTR),
				 &fw_ptp_cfg_offset)) {
		ERROR_REPORT2(IXGBE_ERROR_INVALID_STATE,
			      "eeprom read at offset %d failed",
			      fw_offset +
			      IXGBE_FW_PASSTHROUGH_PATCH_CONFIG_PTR);
		return IXGBE_ERR_EEPROM_VERSION;
	}

	if ((fw_ptp_cfg_offset == 0) || (fw_ptp_cfg_offset == 0xFFFF))
		goto fw_version_out;

	/* get the firmware version */
	if (hw->eeprom.ops.read(hw, (fw_ptp_cfg_offset +
			    IXGBE_FW_PATCH_VERSION_4), &fw_version)) {
		ERROR_REPORT2(IXGBE_ERROR_INVALID_STATE,
			      "eeprom read at offset %d failed",
			      fw_ptp_cfg_offset + IXGBE_FW_PATCH_VERSION_4);
		return IXGBE_ERR_EEPROM_VERSION;
	}

	if (fw_version > 0x5)
		status = IXGBE_SUCCESS;

fw_version_out:
	return status;
}

/**
 *  ixgbe_verify_lesm_fw_enabled_82599 - Checks LESM FW module state.
 *  @hw: pointer to hardware structure
 *
 *  Returns TRUE if the LESM FW module is present and enabled. Otherwise
 *  returns FALSE. Smart Speed must be disabled if LESM FW module is enabled.
 **/
bool ixgbe_verify_lesm_fw_enabled_82599(struct ixgbe_hw *hw)
{
	bool lesm_enabled = FALSE;
	u16 fw_offset, fw_lesm_param_offset, fw_lesm_state;
	s32 status;

	DEBUGFUNC("ixgbe_verify_lesm_fw_enabled_82599");

	/* get the offset to the Firmware Module block */
	status = hw->eeprom.ops.read(hw, IXGBE_FW_PTR, &fw_offset);

	if ((status != IXGBE_SUCCESS) ||
	    (fw_offset == 0) || (fw_offset == 0xFFFF))
		goto out;

	/* get the offset to the LESM Parameters block */
	status = hw->eeprom.ops.read(hw, (fw_offset +
				     IXGBE_FW_LESM_PARAMETERS_PTR),
				     &fw_lesm_param_offset);

	if ((status != IXGBE_SUCCESS) ||
	    (fw_lesm_param_offset == 0) || (fw_lesm_param_offset == 0xFFFF))
		goto out;

	/* get the LESM state word */
	status = hw->eeprom.ops.read(hw, (fw_lesm_param_offset +
				     IXGBE_FW_LESM_STATE_1),
				     &fw_lesm_state);

	if ((status == IXGBE_SUCCESS) &&
	    (fw_lesm_state & IXGBE_FW_LESM_STATE_ENABLED))
		lesm_enabled = TRUE;

out:
	return lesm_enabled;
}

/**
 *  ixgbe_read_eeprom_buffer_82599 - Read EEPROM word(s) using
 *  fastest available method
 *
 *  @hw: pointer to hardware structure
 *  @offset: offset of  word in EEPROM to read
 *  @words: number of words
 *  @data: word(s) read from the EEPROM
 *
 *  Retrieves 16 bit word(s) read from EEPROM
 **/
static s32 ixgbe_read_eeprom_buffer_82599(struct ixgbe_hw *hw, u16 offset,
					  u16 words, u16 *data)
{
	struct ixgbe_eeprom_info *eeprom = &hw->eeprom;
	s32 ret_val = IXGBE_ERR_CONFIG;

	DEBUGFUNC("ixgbe_read_eeprom_buffer_82599");

	/*
	 * If EEPROM is detected and can be addressed using 14 bits,
	 * use EERD otherwise use bit bang
	 */
	if ((eeprom->type == ixgbe_eeprom_spi) &&
	    (offset + (words - 1) <= IXGBE_EERD_MAX_ADDR))
		ret_val = ixgbe_read_eerd_buffer_generic(hw, offset, words,
							 data);
	else
		ret_val = ixgbe_read_eeprom_buffer_bit_bang_generic(hw, offset,
								    words,
								    data);

	return ret_val;
}

/**
 *  ixgbe_read_eeprom_82599 - Read EEPROM word using
 *  fastest available method
 *
 *  @hw: pointer to hardware structure
 *  @offset: offset of  word in the EEPROM to read
 *  @data: word read from the EEPROM
 *
 *  Reads a 16 bit word from the EEPROM
 **/
static s32 ixgbe_read_eeprom_82599(struct ixgbe_hw *hw,
				   u16 offset, u16 *data)
{
	struct ixgbe_eeprom_info *eeprom = &hw->eeprom;
	s32 ret_val = IXGBE_ERR_CONFIG;

	DEBUGFUNC("ixgbe_read_eeprom_82599");

	/*
	 * If EEPROM is detected and can be addressed using 14 bits,
	 * use EERD otherwise use bit bang
	 */
	if ((eeprom->type == ixgbe_eeprom_spi) &&
	    (offset <= IXGBE_EERD_MAX_ADDR))
		ret_val = ixgbe_read_eerd_generic(hw, offset, data);
	else
		ret_val = ixgbe_read_eeprom_bit_bang_generic(hw, offset, data);

	return ret_val;
}

/**
 * ixgbe_reset_pipeline_82599 - perform pipeline reset
 *
 *  @hw: pointer to hardware structure
 *
 * Reset pipeline by asserting Restart_AN together with LMS change to ensure
 * full pipeline reset.  This function assumes the SW/FW lock is held.
 **/
s32 ixgbe_reset_pipeline_82599(struct ixgbe_hw *hw)
{
	s32 ret_val;
	u32 anlp1_reg = 0;
	u32 i, autoc_reg, autoc2_reg;

	/* Enable link if disabled in NVM */
	autoc2_reg = IXGBE_READ_REG(hw, IXGBE_AUTOC2);
	if (autoc2_reg & IXGBE_AUTOC2_LINK_DISABLE_MASK) {
		autoc2_reg &= ~IXGBE_AUTOC2_LINK_DISABLE_MASK;
		IXGBE_WRITE_REG(hw, IXGBE_AUTOC2, autoc2_reg);
		IXGBE_WRITE_FLUSH(hw);
	}

	autoc_reg = IXGBE_READ_REG(hw, IXGBE_AUTOC);
	autoc_reg |= IXGBE_AUTOC_AN_RESTART;
	/* Write AUTOC register with toggled LMS[2] bit and Restart_AN */
	IXGBE_WRITE_REG(hw, IXGBE_AUTOC,
			autoc_reg ^ (0x4 << IXGBE_AUTOC_LMS_SHIFT));
	/* Wait for AN to leave state 0 */
	for (i = 0; i < 10; i++) {
		msec_delay(4);
		anlp1_reg = IXGBE_READ_REG(hw, IXGBE_ANLP1);
		if (anlp1_reg & IXGBE_ANLP1_AN_STATE_MASK)
			break;
	}

	if (!(anlp1_reg & IXGBE_ANLP1_AN_STATE_MASK)) {
		DEBUGOUT("auto negotiation not completed\n");
		ret_val = IXGBE_ERR_RESET_FAILED;
		goto reset_pipeline_out;
	}

	ret_val = IXGBE_SUCCESS;

reset_pipeline_out:
	/* Write AUTOC register with original LMS field and Restart_AN */
	IXGBE_WRITE_REG(hw, IXGBE_AUTOC, autoc_reg);
	IXGBE_WRITE_FLUSH(hw);

	return ret_val;
}

/**
 *  ixgbe_read_i2c_byte_82599 - Reads 8 bit word over I2C
 *  @hw: pointer to hardware structure
 *  @byte_offset: byte offset to read
 *  @dev_addr: address to read from
 *  @data: value read
 *
 *  Performs byte read operation to SFP module's EEPROM over I2C interface at
 *  a specified device address.
 **/
static s32 ixgbe_read_i2c_byte_82599(struct ixgbe_hw *hw, u8 byte_offset,
				u8 dev_addr, u8 *data)
{
	u32 esdp;
	s32 status;
	s32 timeout = 200;

	DEBUGFUNC("ixgbe_read_i2c_byte_82599");

	if (hw->phy.qsfp_shared_i2c_bus == TRUE) {
		/* Acquire I2C bus ownership. */
		esdp = IXGBE_READ_REG(hw, IXGBE_ESDP);
		esdp |= IXGBE_ESDP_SDP0;
		IXGBE_WRITE_REG(hw, IXGBE_ESDP, esdp);
		IXGBE_WRITE_FLUSH(hw);

		while (timeout) {
			esdp = IXGBE_READ_REG(hw, IXGBE_ESDP);
			if (esdp & IXGBE_ESDP_SDP1)
				break;

			msec_delay(5);
			timeout--;
		}

		if (!timeout) {
			DEBUGOUT("Driver can't access resource,"
				 " acquiring I2C bus timeout.\n");
			status = IXGBE_ERR_I2C;
			goto release_i2c_access;
		}
	}

	status = ixgbe_read_i2c_byte_generic(hw, byte_offset, dev_addr, data);

release_i2c_access:

	if (hw->phy.qsfp_shared_i2c_bus == TRUE) {
		/* Release I2C bus ownership. */
		esdp = IXGBE_READ_REG(hw, IXGBE_ESDP);
		esdp &= ~IXGBE_ESDP_SDP0;
		IXGBE_WRITE_REG(hw, IXGBE_ESDP, esdp);
		IXGBE_WRITE_FLUSH(hw);
	}

	return status;
}

/**
 *  ixgbe_write_i2c_byte_82599 - Writes 8 bit word over I2C
 *  @hw: pointer to hardware structure
 *  @byte_offset: byte offset to write
 *  @dev_addr: address to read from
 *  @data: value to write
 *
 *  Performs byte write operation to SFP module's EEPROM over I2C interface at
 *  a specified device address.
 **/
static s32 ixgbe_write_i2c_byte_82599(struct ixgbe_hw *hw, u8 byte_offset,
				 u8 dev_addr, u8 data)
{
	u32 esdp;
	s32 status;
	s32 timeout = 200;

	DEBUGFUNC("ixgbe_write_i2c_byte_82599");

	if (hw->phy.qsfp_shared_i2c_bus == TRUE) {
		/* Acquire I2C bus ownership. */
		esdp = IXGBE_READ_REG(hw, IXGBE_ESDP);
		esdp |= IXGBE_ESDP_SDP0;
		IXGBE_WRITE_REG(hw, IXGBE_ESDP, esdp);
		IXGBE_WRITE_FLUSH(hw);

		while (timeout) {
			esdp = IXGBE_READ_REG(hw, IXGBE_ESDP);
			if (esdp & IXGBE_ESDP_SDP1)
				break;

			msec_delay(5);
			timeout--;
		}

		if (!timeout) {
			DEBUGOUT("Driver can't access resource,"
				 " acquiring I2C bus timeout.\n");
			status = IXGBE_ERR_I2C;
			goto release_i2c_access;
		}
	}

	status = ixgbe_write_i2c_byte_generic(hw, byte_offset, dev_addr, data);

release_i2c_access:

	if (hw->phy.qsfp_shared_i2c_bus == TRUE) {
		/* Release I2C bus ownership. */
		esdp = IXGBE_READ_REG(hw, IXGBE_ESDP);
		esdp &= ~IXGBE_ESDP_SDP0;
		IXGBE_WRITE_REG(hw, IXGBE_ESDP, esdp);
		IXGBE_WRITE_FLUSH(hw);
	}

	return status;
}
