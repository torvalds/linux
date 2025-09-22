/*	$OpenBSD: ixgbe.c,v 1.28 2024/10/27 04:44:41 yasuoka Exp $	*/

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
/*$FreeBSD: head/sys/dev/ixgbe/ixgbe_common.c 326022 2017-11-20 19:36:21Z pfg $*/
/*$FreeBSD: head/sys/dev/ixgbe/ixgbe_mbx.c 326022 2017-11-20 19:36:21Z pfg $*/

#include <dev/pci/ixgbe.h>
#include <dev/pci/ixgbe_type.h>

#ifdef __sparc64__
#include <dev/ofw/openfirm.h>
#endif

void ixgbe_set_pci_config_data_generic(struct ixgbe_hw *hw,
				       uint16_t link_status);

int32_t ixgbe_acquire_eeprom(struct ixgbe_hw *hw);
int32_t ixgbe_get_eeprom_semaphore(struct ixgbe_hw *hw);
void ixgbe_release_eeprom_semaphore(struct ixgbe_hw *hw);
int32_t ixgbe_ready_eeprom(struct ixgbe_hw *hw);
void ixgbe_standby_eeprom(struct ixgbe_hw *hw);
void ixgbe_shift_out_eeprom_bits(struct ixgbe_hw *hw, uint16_t data,
				 uint16_t count);
uint16_t ixgbe_shift_in_eeprom_bits(struct ixgbe_hw *hw, uint16_t count);
void ixgbe_raise_eeprom_clk(struct ixgbe_hw *hw, uint32_t *eec);
void ixgbe_lower_eeprom_clk(struct ixgbe_hw *hw, uint32_t *eec);
void ixgbe_release_eeprom(struct ixgbe_hw *hw);

int32_t ixgbe_mta_vector(struct ixgbe_hw *hw, uint8_t *mc_addr);
int32_t ixgbe_fc_autoneg_fiber(struct ixgbe_hw *hw);
int32_t ixgbe_fc_autoneg_backplane(struct ixgbe_hw *hw);
int32_t ixgbe_fc_autoneg_copper(struct ixgbe_hw *hw);
bool ixgbe_device_supports_autoneg_fc(struct ixgbe_hw *hw);

int32_t prot_autoc_read_generic(struct ixgbe_hw *, bool *, uint32_t *);
int32_t prot_autoc_write_generic(struct ixgbe_hw *, uint32_t, bool);

/* MBX */
int32_t ixgbe_poll_for_msg(struct ixgbe_hw *hw, uint16_t mbx_id);
int32_t ixgbe_poll_for_ack(struct ixgbe_hw *hw, uint16_t mbx_id);
int32_t ixgbe_check_for_bit_pf(struct ixgbe_hw *hw, uint32_t mask,
			       int32_t index);
int32_t ixgbe_check_for_msg_pf(struct ixgbe_hw *hw, uint16_t vf_number);
int32_t ixgbe_check_for_ack_pf(struct ixgbe_hw *hw, uint16_t vf_number);
int32_t ixgbe_check_for_rst_pf(struct ixgbe_hw *hw, uint16_t vf_number);
int32_t ixgbe_obtain_mbx_lock_pf(struct ixgbe_hw *hw, uint16_t vf_number);
int32_t ixgbe_write_mbx_pf(struct ixgbe_hw *hw, uint32_t *msg, uint16_t size,
			   uint16_t vf_number);
int32_t ixgbe_read_mbx_pf(struct ixgbe_hw *hw, uint32_t *msg, uint16_t size,
			  uint16_t vf_number);

#define IXGBE_EMPTY_PARAM

static const uint32_t ixgbe_mvals_base[IXGBE_MVALS_IDX_LIMIT] = {
	IXGBE_MVALS_INIT(IXGBE_EMPTY_PARAM)
};

static const uint32_t ixgbe_mvals_X540[IXGBE_MVALS_IDX_LIMIT] = {
	IXGBE_MVALS_INIT(_X540)
};

static const uint32_t ixgbe_mvals_X550[IXGBE_MVALS_IDX_LIMIT] = {
	IXGBE_MVALS_INIT(_X550)
};

static const uint32_t ixgbe_mvals_X550EM_x[IXGBE_MVALS_IDX_LIMIT] = {
	IXGBE_MVALS_INIT(_X550EM_x)
};

static const uint32_t ixgbe_mvals_X550EM_a[IXGBE_MVALS_IDX_LIMIT] = {
	IXGBE_MVALS_INIT(_X550EM_a)
};

/**
 *  ixgbe_init_ops_generic - Inits function ptrs
 *  @hw: pointer to the hardware structure
 *
 *  Initialize the function pointers.
 **/
int32_t ixgbe_init_ops_generic(struct ixgbe_hw *hw)
{
	struct ixgbe_eeprom_info *eeprom = &hw->eeprom;
	struct ixgbe_mac_info *mac = &hw->mac;
	uint32_t eec = IXGBE_READ_REG(hw, IXGBE_EEC_BY_MAC(hw));

	DEBUGFUNC("ixgbe_init_ops_generic");

	/* EEPROM */
	eeprom->ops.init_params = ixgbe_init_eeprom_params_generic;
	/* If EEPROM is valid (bit 8 = 1), use EERD otherwise use bit bang */
	if (eec & IXGBE_EEC_PRES)
		eeprom->ops.read = ixgbe_read_eerd_generic;
	else
		eeprom->ops.read = ixgbe_read_eeprom_bit_bang_generic;
	eeprom->ops.write = ixgbe_write_eeprom_generic;
	eeprom->ops.validate_checksum =
				      ixgbe_validate_eeprom_checksum_generic;
	eeprom->ops.update_checksum = ixgbe_update_eeprom_checksum_generic;
	eeprom->ops.calc_checksum = ixgbe_calc_eeprom_checksum_generic;

	/* MAC */
	mac->ops.init_hw = ixgbe_init_hw_generic;
	mac->ops.reset_hw = NULL;
	mac->ops.start_hw = ixgbe_start_hw_generic;
	mac->ops.clear_hw_cntrs = ixgbe_clear_hw_cntrs_generic;
	mac->ops.get_media_type = NULL;
	mac->ops.get_supported_physical_layer = NULL;
	mac->ops.enable_rx_dma = ixgbe_enable_rx_dma_generic;
	mac->ops.get_mac_addr = ixgbe_get_mac_addr_generic;
	mac->ops.stop_adapter = ixgbe_stop_adapter_generic;
	mac->ops.get_bus_info = ixgbe_get_bus_info_generic;
	mac->ops.set_lan_id = ixgbe_set_lan_id_multi_port_pcie;
	mac->ops.acquire_swfw_sync = ixgbe_acquire_swfw_sync;
	mac->ops.release_swfw_sync = ixgbe_release_swfw_sync;
	mac->ops.prot_autoc_read = prot_autoc_read_generic;
	mac->ops.prot_autoc_write = prot_autoc_write_generic;

	/* LEDs */
	mac->ops.led_on = ixgbe_led_on_generic;
	mac->ops.led_off = ixgbe_led_off_generic;
	mac->ops.blink_led_start = ixgbe_blink_led_start_generic;
	mac->ops.blink_led_stop = ixgbe_blink_led_stop_generic;

	/* RAR, Multicast, VLAN */
	mac->ops.set_rar = ixgbe_set_rar_generic;
	mac->ops.clear_rar = ixgbe_clear_rar_generic;
	mac->ops.insert_mac_addr = NULL;
	mac->ops.set_vmdq = NULL;
	mac->ops.clear_vmdq = NULL;
	mac->ops.init_rx_addrs = ixgbe_init_rx_addrs_generic;
	mac->ops.update_mc_addr_list = ixgbe_update_mc_addr_list_generic;
	mac->ops.enable_mc = ixgbe_enable_mc_generic;
	mac->ops.disable_mc = ixgbe_disable_mc_generic;
	mac->ops.clear_vfta = NULL;
	mac->ops.set_vfta = NULL;
	mac->ops.set_vlvf = NULL;
	mac->ops.init_uta_tables = NULL;
	mac->ops.enable_rx = ixgbe_enable_rx_generic;
	mac->ops.disable_rx = ixgbe_disable_rx_generic;

	/* Flow Control */
	mac->ops.fc_enable = ixgbe_fc_enable_generic;
	mac->ops.setup_fc = ixgbe_setup_fc_generic;
	mac->ops.fc_autoneg = ixgbe_fc_autoneg;

	/* Link */
	mac->ops.get_link_capabilities = NULL;
	mac->ops.setup_link = NULL;
	mac->ops.check_link = NULL;
	mac->ops.dmac_config = NULL;
	mac->ops.dmac_update_tcs = NULL;
	mac->ops.dmac_config_tcs = NULL;

	return IXGBE_SUCCESS;
}

/**
 * ixgbe_device_supports_autoneg_fc - Check if device supports autonegotiation
 * of flow control
 * @hw: pointer to hardware structure
 *
 * This function returns TRUE if the device supports flow control
 * autonegotiation, and FALSE if it does not.
 *
 **/
bool ixgbe_device_supports_autoneg_fc(struct ixgbe_hw *hw)
{
	bool supported = FALSE;
	ixgbe_link_speed speed;
	bool link_up;

	DEBUGFUNC("ixgbe_device_supports_autoneg_fc");

	switch (hw->phy.media_type) {
	case ixgbe_media_type_fiber_fixed:
	case ixgbe_media_type_fiber_qsfp:
	case ixgbe_media_type_fiber:
		/* flow control autoneg black list */
		switch (hw->device_id) {
		case IXGBE_DEV_ID_X550EM_A_SFP:
		case IXGBE_DEV_ID_X550EM_A_SFP_N:
		case IXGBE_DEV_ID_X550EM_A_QSFP:
		case IXGBE_DEV_ID_X550EM_A_QSFP_N:
			supported = FALSE;
			break;
		default:
			hw->mac.ops.check_link(hw, &speed, &link_up, FALSE);
			/* if link is down, assume supported */
			if (link_up)
				supported = speed == IXGBE_LINK_SPEED_1GB_FULL ?
				TRUE : FALSE;
			else
				supported = TRUE;
		}

		break;
	case ixgbe_media_type_backplane:
		if (hw->device_id == IXGBE_DEV_ID_X550EM_X_XFI)
			supported = FALSE;
		else
			supported = TRUE;
		break;
	case ixgbe_media_type_copper:
		/* only some copper devices support flow control autoneg */
		switch (hw->device_id) {
		case IXGBE_DEV_ID_82599_T3_LOM:
		case IXGBE_DEV_ID_X540T:
		case IXGBE_DEV_ID_X540T1:
		case IXGBE_DEV_ID_X540_BYPASS:
		case IXGBE_DEV_ID_X550T:
		case IXGBE_DEV_ID_X550T1:
		case IXGBE_DEV_ID_X550EM_X_10G_T:
		case IXGBE_DEV_ID_X550EM_A_10G_T:
		case IXGBE_DEV_ID_X550EM_A_1G_T:
		case IXGBE_DEV_ID_X550EM_A_1G_T_L:
			supported = TRUE;
			break;
		default:
			supported = FALSE;
		}
	default:
		break;
	}

	if (!supported) {
		ERROR_REPORT2(IXGBE_ERROR_UNSUPPORTED,
		      "Device %x does not support flow control autoneg",
		      hw->device_id);
	}

	return supported;
}

/**
 *  ixgbe_setup_fc_generic - Set up flow control
 *  @hw: pointer to hardware structure
 *
 *  Called at init time to set up flow control.
 **/
int32_t ixgbe_setup_fc_generic(struct ixgbe_hw *hw)
{
	int32_t ret_val = IXGBE_SUCCESS;
	uint32_t reg = 0, reg_bp = 0;
	uint16_t reg_cu = 0;
	bool locked = FALSE;

	DEBUGFUNC("ixgbe_setup_fc");

	/* Validate the requested mode */
	if (hw->fc.strict_ieee && hw->fc.requested_mode == ixgbe_fc_rx_pause) {
		ERROR_REPORT1(IXGBE_ERROR_UNSUPPORTED,
			   "ixgbe_fc_rx_pause not valid in strict IEEE mode\n");
		ret_val = IXGBE_ERR_INVALID_LINK_SETTINGS;
		goto out;
	}

	/*
	 * 10gig parts do not have a word in the EEPROM to determine the
	 * default flow control setting, so we explicitly set it to full.
	 */
	if (hw->fc.requested_mode == ixgbe_fc_default)
		hw->fc.requested_mode = ixgbe_fc_full;

	/*
	 * Set up the 1G and 10G flow control advertisement registers so the
	 * HW will be able to do fc autoneg once the cable is plugged in.  If
	 * we link at 10G, the 1G advertisement is harmless and vice versa.
	 */
	switch (hw->phy.media_type) {
	case ixgbe_media_type_backplane:
		/* some MAC's need RMW protection on AUTOC */
		ret_val = hw->mac.ops.prot_autoc_read(hw, &locked, &reg_bp);
		if (ret_val != IXGBE_SUCCESS)
			goto out;

		/* only backplane uses autoc so fall though */
	case ixgbe_media_type_fiber_fixed:
	case ixgbe_media_type_fiber_qsfp:
	case ixgbe_media_type_fiber:
		reg = IXGBE_READ_REG(hw, IXGBE_PCS1GANA);

		break;
	case ixgbe_media_type_copper:
		hw->phy.ops.read_reg(hw, IXGBE_MDIO_AUTO_NEG_ADVT,
				     IXGBE_MDIO_AUTO_NEG_DEV_TYPE, &reg_cu);
		break;
	default:
		break;
	}

	/*
	 * The possible values of fc.requested_mode are:
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
		reg &= ~(IXGBE_PCS1GANA_SYM_PAUSE | IXGBE_PCS1GANA_ASM_PAUSE);
		if (hw->phy.media_type == ixgbe_media_type_backplane)
			reg_bp &= ~(IXGBE_AUTOC_SYM_PAUSE |
				    IXGBE_AUTOC_ASM_PAUSE);
		else if (hw->phy.media_type == ixgbe_media_type_copper)
			reg_cu &= ~(IXGBE_TAF_SYM_PAUSE | IXGBE_TAF_ASM_PAUSE);
		break;
	case ixgbe_fc_tx_pause:
		/*
		 * Tx Flow control is enabled, and Rx Flow control is
		 * disabled by software override.
		 */
		reg |= IXGBE_PCS1GANA_ASM_PAUSE;
		reg &= ~IXGBE_PCS1GANA_SYM_PAUSE;
		if (hw->phy.media_type == ixgbe_media_type_backplane) {
			reg_bp |= IXGBE_AUTOC_ASM_PAUSE;
			reg_bp &= ~IXGBE_AUTOC_SYM_PAUSE;
		} else if (hw->phy.media_type == ixgbe_media_type_copper) {
			reg_cu |= IXGBE_TAF_ASM_PAUSE;
			reg_cu &= ~IXGBE_TAF_SYM_PAUSE;
		}
		break;
	case ixgbe_fc_rx_pause:
		/*
		 * Rx Flow control is enabled and Tx Flow control is
		 * disabled by software override. Since there really
		 * isn't a way to advertise that we are capable of RX
		 * Pause ONLY, we will advertise that we support both
		 * symmetric and asymmetric Rx PAUSE, as such we fall
		 * through to the fc_full statement.  Later, we will
		 * disable the adapter's ability to send PAUSE frames.
		 */
	case ixgbe_fc_full:
		/* Flow control (both Rx and Tx) is enabled by SW override. */
		reg |= IXGBE_PCS1GANA_SYM_PAUSE | IXGBE_PCS1GANA_ASM_PAUSE;
		if (hw->phy.media_type == ixgbe_media_type_backplane)
			reg_bp |= IXGBE_AUTOC_SYM_PAUSE |
				  IXGBE_AUTOC_ASM_PAUSE;
		else if (hw->phy.media_type == ixgbe_media_type_copper)
			reg_cu |= IXGBE_TAF_SYM_PAUSE | IXGBE_TAF_ASM_PAUSE;
		break;
	default:
		ERROR_REPORT1(IXGBE_ERROR_ARGUMENT,
			     "Flow control param set incorrectly\n");
		ret_val = IXGBE_ERR_CONFIG;
		goto out;
		break;
	}

	if (hw->mac.type < ixgbe_mac_X540) {
		/*
		 * Enable auto-negotiation between the MAC & PHY;
		 * the MAC will advertise clause 37 flow control.
		 */
		IXGBE_WRITE_REG(hw, IXGBE_PCS1GANA, reg);
		reg = IXGBE_READ_REG(hw, IXGBE_PCS1GLCTL);

		/* Disable AN timeout */
		if (hw->fc.strict_ieee)
			reg &= ~IXGBE_PCS1GLCTL_AN_1G_TIMEOUT_EN;

		IXGBE_WRITE_REG(hw, IXGBE_PCS1GLCTL, reg);
		DEBUGOUT1("Set up FC; PCS1GLCTL = 0x%08X\n", reg);
	}

	/*
	 * AUTOC restart handles negotiation of 1G and 10G on backplane
	 * and copper. There is no need to set the PCS1GCTL register.
	 *
	 */
	if (hw->phy.media_type == ixgbe_media_type_backplane) {
		reg_bp |= IXGBE_AUTOC_AN_RESTART;
		ret_val = hw->mac.ops.prot_autoc_write(hw, reg_bp, locked);
		if (ret_val)
			goto out;
	} else if ((hw->phy.media_type == ixgbe_media_type_copper) &&
		    (ixgbe_device_supports_autoneg_fc(hw))) {
		hw->phy.ops.write_reg(hw, IXGBE_MDIO_AUTO_NEG_ADVT,
				      IXGBE_MDIO_AUTO_NEG_DEV_TYPE, reg_cu);
	}

	DEBUGOUT1("Set up FC; PCS1GLCTL = 0x%08X\n", reg);
out:
	return ret_val;
}

/**
 *  ixgbe_start_hw_generic - Prepare hardware for Tx/Rx
 *  @hw: pointer to hardware structure
 *
 *  Starts the hardware by filling the bus info structure and media type, clears
 *  all on chip counters, initializes receive address registers, multicast
 *  table, VLAN filter table, calls routine to set up link and flow control
 *  settings, and leaves transmit and receive units disabled and uninitialized
 **/
int32_t ixgbe_start_hw_generic(struct ixgbe_hw *hw)
{
	int32_t ret_val;
	uint32_t ctrl_ext;
	uint16_t device_caps;

	DEBUGFUNC("ixgbe_start_hw_generic");

	/* Set the media type */
	hw->phy.media_type = hw->mac.ops.get_media_type(hw);

	/* PHY ops initialization must be done in reset_hw() */

	/* Clear the VLAN filter table */
	hw->mac.ops.clear_vfta(hw);

	/* Clear statistics registers */
	hw->mac.ops.clear_hw_cntrs(hw);

	/* Set No Snoop Disable */
	ctrl_ext = IXGBE_READ_REG(hw, IXGBE_CTRL_EXT);
	ctrl_ext |= IXGBE_CTRL_EXT_NS_DIS;
	IXGBE_WRITE_REG(hw, IXGBE_CTRL_EXT, ctrl_ext);
	IXGBE_WRITE_FLUSH(hw);

	/* Setup flow control */
	if (hw->mac.ops.setup_fc) {
		ret_val = hw->mac.ops.setup_fc(hw);
		if (ret_val != IXGBE_SUCCESS) {
			DEBUGOUT1("Flow control setup failed, returning %d\n", ret_val);
			return ret_val;
		}
	}

	/* Cache bit indicating need for crosstalk fix */
	switch (hw->mac.type) {
	case ixgbe_mac_82599EB:
	case ixgbe_mac_X550EM_x:
	case ixgbe_mac_X550EM_a:
		hw->mac.ops.get_device_caps(hw, &device_caps);
		if (device_caps & IXGBE_DEVICE_CAPS_NO_CROSSTALK_WR)
			hw->need_crosstalk_fix = FALSE;
		else
			hw->need_crosstalk_fix = TRUE;
		break;
	default:
		hw->need_crosstalk_fix = FALSE;
		break;
	}

	/* Clear adapter stopped flag */
	hw->adapter_stopped = FALSE;

	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_start_hw_gen2 - Init sequence for common device family
 *  @hw: pointer to hw structure
 *
 * Performs the init sequence common to the second generation
 * of 10 GbE devices.
 * Devices in the second generation:
 *     82599
 *     X540
 **/
int32_t ixgbe_start_hw_gen2(struct ixgbe_hw *hw)
{
	uint32_t i;
	uint32_t regval;

	/* Clear the rate limiters */
	for (i = 0; i < hw->mac.max_tx_queues; i++) {
		IXGBE_WRITE_REG(hw, IXGBE_RTTDQSEL, i);
		IXGBE_WRITE_REG(hw, IXGBE_RTTBCNRC, 0);
	}
	IXGBE_WRITE_FLUSH(hw);

	/* Disable relaxed ordering */
	for (i = 0; i < hw->mac.max_tx_queues; i++) {
		regval = IXGBE_READ_REG(hw, IXGBE_DCA_TXCTRL_82599(i));
		regval &= ~IXGBE_DCA_TXCTRL_DESC_WRO_EN;
		IXGBE_WRITE_REG(hw, IXGBE_DCA_TXCTRL_82599(i), regval);
	}

	for (i = 0; i < hw->mac.max_rx_queues; i++) {
		regval = IXGBE_READ_REG(hw, IXGBE_DCA_RXCTRL(i));
		regval &= ~(IXGBE_DCA_RXCTRL_DATA_WRO_EN |
			    IXGBE_DCA_RXCTRL_HEAD_WRO_EN);
		IXGBE_WRITE_REG(hw, IXGBE_DCA_RXCTRL(i), regval);
	}

	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_init_hw_generic - Generic hardware initialization
 *  @hw: pointer to hardware structure
 *
 *  Initialize the hardware by resetting the hardware, filling the bus info
 *  structure and media type, clears all on chip counters, initializes receive
 *  address registers, multicast table, VLAN filter table, calls routine to set
 *  up link and flow control settings, and leaves transmit and receive units
 *  disabled and uninitialized
 **/
int32_t ixgbe_init_hw_generic(struct ixgbe_hw *hw)
{
	int32_t status;

	DEBUGFUNC("ixgbe_init_hw_generic");

	/* Reset the hardware */
	status = hw->mac.ops.reset_hw(hw);

	if (status == IXGBE_SUCCESS || status == IXGBE_ERR_SFP_NOT_PRESENT) {
		/* Start the HW */
		status = hw->mac.ops.start_hw(hw);
	}

	if (status != IXGBE_SUCCESS)
		DEBUGOUT1("Failed to initialize HW, STATUS = %d\n", status);

	return status;
}

/**
 *  ixgbe_clear_hw_cntrs_generic - Generic clear hardware counters
 *  @hw: pointer to hardware structure
 *
 *  Clears all hardware statistics counters by reading them from the hardware
 *  Statistics counters are clear on read.
 **/
int32_t ixgbe_clear_hw_cntrs_generic(struct ixgbe_hw *hw)
{
	uint16_t i = 0;

	DEBUGFUNC("ixgbe_clear_hw_cntrs_generic");

	IXGBE_READ_REG(hw, IXGBE_CRCERRS);
	IXGBE_READ_REG(hw, IXGBE_ILLERRC);
	IXGBE_READ_REG(hw, IXGBE_ERRBC);
	IXGBE_READ_REG(hw, IXGBE_MSPDC);
	for (i = 0; i < 8; i++)
		IXGBE_READ_REG(hw, IXGBE_MPC(i));

	IXGBE_READ_REG(hw, IXGBE_MLFC);
	IXGBE_READ_REG(hw, IXGBE_MRFC);
	IXGBE_READ_REG(hw, IXGBE_RLEC);
	IXGBE_READ_REG(hw, IXGBE_LXONTXC);
	IXGBE_READ_REG(hw, IXGBE_LXOFFTXC);
	if (hw->mac.type >= ixgbe_mac_82599EB) {
		IXGBE_READ_REG(hw, IXGBE_LXONRXCNT);
		IXGBE_READ_REG(hw, IXGBE_LXOFFRXCNT);
	} else {
		IXGBE_READ_REG(hw, IXGBE_LXONRXC);
		IXGBE_READ_REG(hw, IXGBE_LXOFFRXC);
	}

	for (i = 0; i < 8; i++) {
		IXGBE_READ_REG(hw, IXGBE_PXONTXC(i));
		IXGBE_READ_REG(hw, IXGBE_PXOFFTXC(i));
		if (hw->mac.type >= ixgbe_mac_82599EB) {
			IXGBE_READ_REG(hw, IXGBE_PXONRXCNT(i));
			IXGBE_READ_REG(hw, IXGBE_PXOFFRXCNT(i));
		} else {
			IXGBE_READ_REG(hw, IXGBE_PXONRXC(i));
			IXGBE_READ_REG(hw, IXGBE_PXOFFRXC(i));
		}
	}
	if (hw->mac.type >= ixgbe_mac_82599EB)
		for (i = 0; i < 8; i++)
			IXGBE_READ_REG(hw, IXGBE_PXON2OFFCNT(i));
	IXGBE_READ_REG(hw, IXGBE_PRC64);
	IXGBE_READ_REG(hw, IXGBE_PRC127);
	IXGBE_READ_REG(hw, IXGBE_PRC255);
	IXGBE_READ_REG(hw, IXGBE_PRC511);
	IXGBE_READ_REG(hw, IXGBE_PRC1023);
	IXGBE_READ_REG(hw, IXGBE_PRC1522);
	IXGBE_READ_REG(hw, IXGBE_GPRC);
	IXGBE_READ_REG(hw, IXGBE_BPRC);
	IXGBE_READ_REG(hw, IXGBE_MPRC);
	IXGBE_READ_REG(hw, IXGBE_GPTC);
	IXGBE_READ_REG(hw, IXGBE_GORCL);
	IXGBE_READ_REG(hw, IXGBE_GORCH);
	IXGBE_READ_REG(hw, IXGBE_GOTCL);
	IXGBE_READ_REG(hw, IXGBE_GOTCH);
	if (hw->mac.type == ixgbe_mac_82598EB)
		for (i = 0; i < 8; i++)
			IXGBE_READ_REG(hw, IXGBE_RNBC(i));
	IXGBE_READ_REG(hw, IXGBE_RUC);
	IXGBE_READ_REG(hw, IXGBE_RFC);
	IXGBE_READ_REG(hw, IXGBE_ROC);
	IXGBE_READ_REG(hw, IXGBE_RJC);
	IXGBE_READ_REG(hw, IXGBE_MNGPRC);
	IXGBE_READ_REG(hw, IXGBE_MNGPDC);
	IXGBE_READ_REG(hw, IXGBE_MNGPTC);
	IXGBE_READ_REG(hw, IXGBE_TORL);
	IXGBE_READ_REG(hw, IXGBE_TORH);
	IXGBE_READ_REG(hw, IXGBE_TPR);
	IXGBE_READ_REG(hw, IXGBE_TPT);
	IXGBE_READ_REG(hw, IXGBE_PTC64);
	IXGBE_READ_REG(hw, IXGBE_PTC127);
	IXGBE_READ_REG(hw, IXGBE_PTC255);
	IXGBE_READ_REG(hw, IXGBE_PTC511);
	IXGBE_READ_REG(hw, IXGBE_PTC1023);
	IXGBE_READ_REG(hw, IXGBE_PTC1522);
	IXGBE_READ_REG(hw, IXGBE_MPTC);
	IXGBE_READ_REG(hw, IXGBE_BPTC);
	for (i = 0; i < 16; i++) {
		IXGBE_READ_REG(hw, IXGBE_QPRC(i));
		IXGBE_READ_REG(hw, IXGBE_QPTC(i));
		if (hw->mac.type >= ixgbe_mac_82599EB) {
			IXGBE_READ_REG(hw, IXGBE_QBRC_L(i));
			IXGBE_READ_REG(hw, IXGBE_QBRC_H(i));
			IXGBE_READ_REG(hw, IXGBE_QBTC_L(i));
			IXGBE_READ_REG(hw, IXGBE_QBTC_H(i));
			IXGBE_READ_REG(hw, IXGBE_QPRDC(i));
		} else {
			IXGBE_READ_REG(hw, IXGBE_QBRC(i));
			IXGBE_READ_REG(hw, IXGBE_QBTC(i));
		}
	}

	if (hw->mac.type == ixgbe_mac_X550 || hw->mac.type == ixgbe_mac_X540) {
		if (hw->phy.id == 0)
			ixgbe_identify_phy(hw);
		hw->phy.ops.read_reg(hw, IXGBE_PCRC8ECL,
				     IXGBE_MDIO_PCS_DEV_TYPE, &i);
		hw->phy.ops.read_reg(hw, IXGBE_PCRC8ECH,
				     IXGBE_MDIO_PCS_DEV_TYPE, &i);
		hw->phy.ops.read_reg(hw, IXGBE_LDPCECL,
				     IXGBE_MDIO_PCS_DEV_TYPE, &i);
		hw->phy.ops.read_reg(hw, IXGBE_LDPCECH,
				     IXGBE_MDIO_PCS_DEV_TYPE, &i);
	}

	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_get_mac_addr_generic - Generic get MAC address
 *  @hw: pointer to hardware structure
 *  @mac_addr: Adapter MAC address
 *
 *  Reads the adapter's MAC address from first Receive Address Register (RAR0)
 *  A reset of the adapter must be performed prior to calling this function
 *  in order for the MAC address to have been loaded from the EEPROM into RAR0
 **/
int32_t ixgbe_get_mac_addr_generic(struct ixgbe_hw *hw, uint8_t *mac_addr)
{
	uint32_t rar_high;
	uint32_t rar_low;
	uint16_t i;

	DEBUGFUNC("ixgbe_get_mac_addr_generic");

#ifdef __sparc64__
	struct ixgbe_osdep *os = hw->back;
 
	if (OF_getprop(PCITAG_NODE(os->os_pa.pa_tag), "local-mac-address",
	    mac_addr, ETHER_ADDR_LEN) == ETHER_ADDR_LEN)
		return IXGBE_SUCCESS;
#endif

	rar_high = IXGBE_READ_REG(hw, IXGBE_RAH(0));
	rar_low = IXGBE_READ_REG(hw, IXGBE_RAL(0));

	for (i = 0; i < 4; i++)
		mac_addr[i] = (uint8_t)(rar_low >> (i*8));

	for (i = 0; i < 2; i++)
		mac_addr[i+4] = (uint8_t)(rar_high >> (i*8));

	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_set_pci_config_data_generic - Generic store PCI bus info
 *  @hw: pointer to hardware structure
 *  @link_status: the link status returned by the PCI config space
 *
 *  Stores the PCI bus info (speed, width, type) within the ixgbe_hw structure
 **/
void ixgbe_set_pci_config_data_generic(struct ixgbe_hw *hw,
				       uint16_t link_status)
{
	struct ixgbe_mac_info *mac = &hw->mac;

	if (hw->bus.type == ixgbe_bus_type_unknown)
		hw->bus.type = ixgbe_bus_type_pci_express;

	switch (link_status & IXGBE_PCI_LINK_WIDTH) {
	case IXGBE_PCI_LINK_WIDTH_1:
		hw->bus.width = ixgbe_bus_width_pcie_x1;
		break;
	case IXGBE_PCI_LINK_WIDTH_2:
		hw->bus.width = ixgbe_bus_width_pcie_x2;
		break;
	case IXGBE_PCI_LINK_WIDTH_4:
		hw->bus.width = ixgbe_bus_width_pcie_x4;
		break;
	case IXGBE_PCI_LINK_WIDTH_8:
		hw->bus.width = ixgbe_bus_width_pcie_x8;
		break;
	default:
		hw->bus.width = ixgbe_bus_width_unknown;
		break;
	}

	switch (link_status & IXGBE_PCI_LINK_SPEED) {
	case IXGBE_PCI_LINK_SPEED_2500:
		hw->bus.speed = ixgbe_bus_speed_2500;
		break;
	case IXGBE_PCI_LINK_SPEED_5000:
		hw->bus.speed = ixgbe_bus_speed_5000;
		break;
	case IXGBE_PCI_LINK_SPEED_8000:
		hw->bus.speed = ixgbe_bus_speed_8000;
		break;
	default:
		hw->bus.speed = ixgbe_bus_speed_unknown;
		break;
	}

	mac->ops.set_lan_id(hw);
}

/**
 *  ixgbe_get_bus_info_generic - Generic set PCI bus info
 *  @hw: pointer to hardware structure
 *
 *  Gets the PCI bus info (speed, width, type) then calls helper function to
 *  store this data within the ixgbe_hw structure.
 **/
int32_t ixgbe_get_bus_info_generic(struct ixgbe_hw *hw)
{
	uint16_t link_status;

	DEBUGFUNC("ixgbe_get_bus_info_generic");

	/* Get the negotiated link width and speed from PCI config space */
	link_status = IXGBE_READ_PCIE_WORD(hw, IXGBE_PCI_LINK_STATUS);

	ixgbe_set_pci_config_data_generic(hw, link_status);

	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_set_lan_id_multi_port_pcie - Set LAN id for PCIe multiple port devices
 *  @hw: pointer to the HW structure
 *
 *  Determines the LAN function id by reading memory-mapped registers and swaps
 *  the port value if requested, and set MAC instance for devices that share
 *  CS4227.
 **/
void ixgbe_set_lan_id_multi_port_pcie(struct ixgbe_hw *hw)
{
	struct ixgbe_bus_info *bus = &hw->bus;
	uint32_t reg;
	uint16_t ee_ctrl_4;

	DEBUGFUNC("ixgbe_set_lan_id_multi_port_pcie");

	reg = IXGBE_READ_REG(hw, IXGBE_STATUS);
	bus->func = (reg & IXGBE_STATUS_LAN_ID) >> IXGBE_STATUS_LAN_ID_SHIFT;
	bus->lan_id = bus->func;

	/* check for a port swap */
	reg = IXGBE_READ_REG(hw, IXGBE_FACTPS_BY_MAC(hw));
	if (reg & IXGBE_FACTPS_LFS)
		bus->func ^= 0x1;

	/* Get MAC instance from EEPROM for configuring CS4227 */
	if (hw->device_id == IXGBE_DEV_ID_X550EM_A_SFP) {
		hw->eeprom.ops.read(hw, IXGBE_EEPROM_CTRL_4, &ee_ctrl_4);
		bus->instance_id = (ee_ctrl_4 & IXGBE_EE_CTRL_4_INST_ID) >>
				   IXGBE_EE_CTRL_4_INST_ID_SHIFT;
	}
}

/**
 *  ixgbe_stop_adapter_generic - Generic stop Tx/Rx units
 *  @hw: pointer to hardware structure
 *
 *  Sets the adapter_stopped flag within ixgbe_hw struct. Clears interrupts,
 *  disables transmit and receive units. The adapter_stopped flag is used by
 *  the shared code and drivers to determine if the adapter is in a stopped
 *  state and should not touch the hardware.
 **/
int32_t ixgbe_stop_adapter_generic(struct ixgbe_hw *hw)
{
	uint32_t reg_val;
	uint16_t i;

	DEBUGFUNC("ixgbe_stop_adapter_generic");

	/*
	 * Set the adapter_stopped flag so other driver functions stop touching
	 * the hardware
	 */
	hw->adapter_stopped = TRUE;

	/* Disable the receive unit */
	ixgbe_disable_rx(hw);

	/* Clear interrupt mask to stop interrupts from being generated */
	IXGBE_WRITE_REG(hw, IXGBE_EIMC, IXGBE_IRQ_CLEAR_MASK);

	/* Clear any pending interrupts, flush previous writes */
	IXGBE_READ_REG(hw, IXGBE_EICR);

	/* Disable the transmit unit.  Each queue must be disabled. */
	for (i = 0; i < hw->mac.max_tx_queues; i++)
		IXGBE_WRITE_REG(hw, IXGBE_TXDCTL(i), IXGBE_TXDCTL_SWFLSH);

	/* Disable the receive unit by stopping each queue */
	for (i = 0; i < hw->mac.max_rx_queues; i++) {
		reg_val = IXGBE_READ_REG(hw, IXGBE_RXDCTL(i));
		reg_val &= ~IXGBE_RXDCTL_ENABLE;
		reg_val |= IXGBE_RXDCTL_SWFLSH;
		IXGBE_WRITE_REG(hw, IXGBE_RXDCTL(i), reg_val);
	}

	/* flush all queues disables */
	IXGBE_WRITE_FLUSH(hw);
	msec_delay(2);

	/*
	 * Prevent the PCI-E bus from hanging by disabling PCI-E master
	 * access and verify no pending requests
	 */
	return ixgbe_disable_pcie_master(hw);
}

/**
 *  ixgbe_led_on_generic - Turns on the software controllable LEDs.
 *  @hw: pointer to hardware structure
 *  @index: led number to turn on
 **/
int32_t ixgbe_led_on_generic(struct ixgbe_hw *hw, uint32_t index)
{
	uint32_t led_reg = IXGBE_READ_REG(hw, IXGBE_LEDCTL);

	DEBUGFUNC("ixgbe_led_on_generic");

	if (index > 3)
		return IXGBE_ERR_PARAM;

	/* To turn on the LED, set mode to ON. */
	led_reg &= ~IXGBE_LED_MODE_MASK(index);
	led_reg |= IXGBE_LED_ON << IXGBE_LED_MODE_SHIFT(index);
	IXGBE_WRITE_REG(hw, IXGBE_LEDCTL, led_reg);
	IXGBE_WRITE_FLUSH(hw);

	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_led_off_generic - Turns off the software controllable LEDs.
 *  @hw: pointer to hardware structure
 *  @index: led number to turn off
 **/
int32_t ixgbe_led_off_generic(struct ixgbe_hw *hw, uint32_t index)
{
	uint32_t led_reg = IXGBE_READ_REG(hw, IXGBE_LEDCTL);

	DEBUGFUNC("ixgbe_led_off_generic");

	if (index > 3)
		return IXGBE_ERR_PARAM;

	/* To turn off the LED, set mode to OFF. */
	led_reg &= ~IXGBE_LED_MODE_MASK(index);
	led_reg |= IXGBE_LED_OFF << IXGBE_LED_MODE_SHIFT(index);
	IXGBE_WRITE_REG(hw, IXGBE_LEDCTL, led_reg);
	IXGBE_WRITE_FLUSH(hw);

	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_init_eeprom_params_generic - Initialize EEPROM params
 *  @hw: pointer to hardware structure
 *
 *  Initializes the EEPROM parameters ixgbe_eeprom_info within the
 *  ixgbe_hw struct in order to set up EEPROM access.
 **/
int32_t ixgbe_init_eeprom_params_generic(struct ixgbe_hw *hw)
{
	struct ixgbe_eeprom_info *eeprom = &hw->eeprom;
	uint32_t eec;
	uint16_t eeprom_size;

	DEBUGFUNC("ixgbe_init_eeprom_params_generic");

	if (eeprom->type == ixgbe_eeprom_uninitialized) {
		eeprom->type = ixgbe_eeprom_none;
		/* Set default semaphore delay to 10ms which is a well
		 * tested value */
		eeprom->semaphore_delay = 10;
		/* Clear EEPROM page size, it will be initialized as needed */
		eeprom->word_page_size = 0;

		/*
		 * Check for EEPROM present first.
		 * If not present leave as none
		 */
		eec = IXGBE_READ_REG(hw, IXGBE_EEC_BY_MAC(hw));
		if (eec & IXGBE_EEC_PRES) {
			eeprom->type = ixgbe_eeprom_spi;

			/*
			 * SPI EEPROM is assumed here.  This code would need to
			 * change if a future EEPROM is not SPI.
			 */
			eeprom_size = (uint16_t)((eec & IXGBE_EEC_SIZE) >>
					    IXGBE_EEC_SIZE_SHIFT);
			eeprom->word_size = 1 << (eeprom_size +
					     IXGBE_EEPROM_WORD_SIZE_SHIFT);
		}

		if (eec & IXGBE_EEC_ADDR_SIZE)
			eeprom->address_bits = 16;
		else
			eeprom->address_bits = 8;
		DEBUGOUT3("Eeprom params: type = %d, size = %d, address bits: "
			  "%d\n", eeprom->type, eeprom->word_size,
			  eeprom->address_bits);
	}

	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_write_eeprom_buffer_bit_bang - Writes 16 bit word(s) to EEPROM
 *  @hw: pointer to hardware structure
 *  @offset: offset within the EEPROM to be written to
 *  @words: number of word(s)
 *  @data: 16 bit word(s) to be written to the EEPROM
 *
 *  If ixgbe_eeprom_update_checksum is not called after this function, the
 *  EEPROM will most likely contain an invalid checksum.
 **/
static int32_t ixgbe_write_eeprom_buffer_bit_bang(struct ixgbe_hw *hw, uint16_t offset,
					      uint16_t words, uint16_t *data)
{
	int32_t status;
	uint16_t word;
	uint16_t page_size;
	uint16_t i;
	uint8_t write_opcode = IXGBE_EEPROM_WRITE_OPCODE_SPI;

	DEBUGFUNC("ixgbe_write_eeprom_buffer_bit_bang");

	/* Prepare the EEPROM for writing  */
	status = ixgbe_acquire_eeprom(hw);

	if (status == IXGBE_SUCCESS) {
		if (ixgbe_ready_eeprom(hw) != IXGBE_SUCCESS) {
			ixgbe_release_eeprom(hw);
			status = IXGBE_ERR_EEPROM;
		}
	}

	if (status == IXGBE_SUCCESS) {
		for (i = 0; i < words; i++) {
			ixgbe_standby_eeprom(hw);

			/*  Send the WRITE ENABLE command (8 bit opcode )  */
			ixgbe_shift_out_eeprom_bits(hw,
						   IXGBE_EEPROM_WREN_OPCODE_SPI,
						   IXGBE_EEPROM_OPCODE_BITS);

			ixgbe_standby_eeprom(hw);

			/*
			 * Some SPI eeproms use the 8th address bit embedded
			 * in the opcode
			 */
			if ((hw->eeprom.address_bits == 8) &&
			    ((offset + i) >= 128))
				write_opcode |= IXGBE_EEPROM_A8_OPCODE_SPI;

			/* Send the Write command (8-bit opcode + addr) */
			ixgbe_shift_out_eeprom_bits(hw, write_opcode,
						    IXGBE_EEPROM_OPCODE_BITS);
			ixgbe_shift_out_eeprom_bits(hw, (uint16_t)((offset + i) * 2),
						    hw->eeprom.address_bits);

			page_size = hw->eeprom.word_page_size;

			/* Send the data in burst via SPI*/
			do {
				word = data[i];
				word = (word >> 8) | (word << 8);
				ixgbe_shift_out_eeprom_bits(hw, word, 16);

				if (page_size == 0)
					break;

				/* do not wrap around page */
				if (((offset + i) & (page_size - 1)) ==
				    (page_size - 1))
					break;
			} while (++i < words);

			ixgbe_standby_eeprom(hw);
			msec_delay(10);
		}
		/* Done with writing - release the EEPROM */
		ixgbe_release_eeprom(hw);
	}

	return status;
}

/**
 *  ixgbe_write_eeprom_generic - Writes 16 bit value to EEPROM
 *  @hw: pointer to hardware structure
 *  @offset: offset within the EEPROM to be written to
 *  @data: 16 bit word to be written to the EEPROM
 *
 *  If ixgbe_eeprom_update_checksum is not called after this function, the
 *  EEPROM will most likely contain an invalid checksum.
 **/
int32_t ixgbe_write_eeprom_generic(struct ixgbe_hw *hw, uint16_t offset, uint16_t data)
{
	int32_t status;

	DEBUGFUNC("ixgbe_write_eeprom_generic");

	hw->eeprom.ops.init_params(hw);

	if (offset >= hw->eeprom.word_size) {
		status = IXGBE_ERR_EEPROM;
		goto out;
	}

	status = ixgbe_write_eeprom_buffer_bit_bang(hw, offset, 1, &data);

out:
	return status;
}

/**
 *  ixgbe_read_eeprom_buffer_bit_bang - Read EEPROM using bit-bang
 *  @hw: pointer to hardware structure
 *  @offset: offset within the EEPROM to be read
 *  @words: number of word(s)
 *  @data: read 16 bit word(s) from EEPROM
 *
 *  Reads 16 bit word(s) from EEPROM through bit-bang method
 **/
static int32_t ixgbe_read_eeprom_buffer_bit_bang(struct ixgbe_hw *hw, uint16_t offset,
					     uint16_t words, uint16_t *data)
{
	int32_t status;
	uint16_t word_in;
	uint8_t read_opcode = IXGBE_EEPROM_READ_OPCODE_SPI;
	uint16_t i;

	DEBUGFUNC("ixgbe_read_eeprom_buffer_bit_bang");

	/* Prepare the EEPROM for reading  */
	status = ixgbe_acquire_eeprom(hw);

	if (status == IXGBE_SUCCESS) {
		if (ixgbe_ready_eeprom(hw) != IXGBE_SUCCESS) {
			ixgbe_release_eeprom(hw);
			status = IXGBE_ERR_EEPROM;
		}
	}

	if (status == IXGBE_SUCCESS) {
		for (i = 0; i < words; i++) {
			ixgbe_standby_eeprom(hw);
			/*
			 * Some SPI eeproms use the 8th address bit embedded
			 * in the opcode
			 */
			if ((hw->eeprom.address_bits == 8) &&
			    ((offset + i) >= 128))
				read_opcode |= IXGBE_EEPROM_A8_OPCODE_SPI;

			/* Send the READ command (opcode + addr) */
			ixgbe_shift_out_eeprom_bits(hw, read_opcode,
						    IXGBE_EEPROM_OPCODE_BITS);
			ixgbe_shift_out_eeprom_bits(hw, (uint16_t)((offset + i) * 2),
						    hw->eeprom.address_bits);

			/* Read the data. */
			word_in = ixgbe_shift_in_eeprom_bits(hw, 16);
			data[i] = (word_in >> 8) | (word_in << 8);
		}

		/* End this read operation */
		ixgbe_release_eeprom(hw);
	}

	return status;
}

/**
 *  ixgbe_read_eeprom_bit_bang_generic - Read EEPROM word using bit-bang
 *  @hw: pointer to hardware structure
 *  @offset: offset within the EEPROM to be read
 *  @data: read 16 bit value from EEPROM
 *
 *  Reads 16 bit value from EEPROM through bit-bang method
 **/
int32_t ixgbe_read_eeprom_bit_bang_generic(struct ixgbe_hw *hw, uint16_t offset,
				       uint16_t *data)
{
	int32_t status;

	DEBUGFUNC("ixgbe_read_eeprom_bit_bang_generic");

	hw->eeprom.ops.init_params(hw);

	if (offset >= hw->eeprom.word_size) {
		status = IXGBE_ERR_EEPROM;
		goto out;
	}

	status = ixgbe_read_eeprom_buffer_bit_bang(hw, offset, 1, data);

out:
	return status;
}

/**
 *  ixgbe_read_eerd_buffer_generic - Read EEPROM word(s) using EERD
 *  @hw: pointer to hardware structure
 *  @offset: offset of word in the EEPROM to read
 *  @words: number of word(s)
 *  @data: 16 bit word(s) from the EEPROM
 *
 *  Reads a 16 bit word(s) from the EEPROM using the EERD register.
 **/
int32_t ixgbe_read_eerd_buffer_generic(struct ixgbe_hw *hw, uint16_t offset,
				   uint16_t words, uint16_t *data)
{
	uint32_t eerd;
	int32_t status = IXGBE_SUCCESS;
	uint32_t i;

	DEBUGFUNC("ixgbe_read_eerd_buffer_generic");

	hw->eeprom.ops.init_params(hw);

	if (words == 0) {
		status = IXGBE_ERR_INVALID_ARGUMENT;
		ERROR_REPORT1(IXGBE_ERROR_ARGUMENT, "Invalid EEPROM words");
		goto out;
	}

	if (offset >= hw->eeprom.word_size) {
		status = IXGBE_ERR_EEPROM;
		ERROR_REPORT1(IXGBE_ERROR_ARGUMENT, "Invalid EEPROM offset");
		goto out;
	}

	for (i = 0; i < words; i++) {
		eerd = ((offset + i) << IXGBE_EEPROM_RW_ADDR_SHIFT) |
		       IXGBE_EEPROM_RW_REG_START;

		IXGBE_WRITE_REG(hw, IXGBE_EERD, eerd);
		status = ixgbe_poll_eerd_eewr_done(hw, IXGBE_NVM_POLL_READ);

		if (status == IXGBE_SUCCESS) {
			data[i] = (IXGBE_READ_REG(hw, IXGBE_EERD) >>
				   IXGBE_EEPROM_RW_REG_DATA);
		} else {
			DEBUGOUT("Eeprom read timed out\n");
			goto out;
		}
	}
out:
	return status;
}

/**
 *  ixgbe_read_eerd_generic - Read EEPROM word using EERD
 *  @hw: pointer to hardware structure
 *  @offset: offset of  word in the EEPROM to read
 *  @data: word read from the EEPROM
 *
 *  Reads a 16 bit word from the EEPROM using the EERD register.
 **/
int32_t ixgbe_read_eerd_generic(struct ixgbe_hw *hw, uint16_t offset, uint16_t *data)
{
	return ixgbe_read_eerd_buffer_generic(hw, offset, 1, data);
}

/**
 *  ixgbe_write_eewr_buffer_generic - Write EEPROM word(s) using EEWR
 *  @hw: pointer to hardware structure
 *  @offset: offset of  word in the EEPROM to write
 *  @words: number of word(s)
 *  @data: word(s) write to the EEPROM
 *
 *  Write a 16 bit word(s) to the EEPROM using the EEWR register.
 **/
int32_t ixgbe_write_eewr_buffer_generic(struct ixgbe_hw *hw, uint16_t offset,
				    uint16_t words, uint16_t *data)
{
	uint32_t eewr;
	int32_t status = IXGBE_SUCCESS;
	uint16_t i;

	DEBUGFUNC("ixgbe_write_eewr_generic");

	hw->eeprom.ops.init_params(hw);

	if (words == 0) {
		status = IXGBE_ERR_INVALID_ARGUMENT;
		ERROR_REPORT1(IXGBE_ERROR_ARGUMENT, "Invalid EEPROM words");
		goto out;
	}

	if (offset >= hw->eeprom.word_size) {
		status = IXGBE_ERR_EEPROM;
		ERROR_REPORT1(IXGBE_ERROR_ARGUMENT, "Invalid EEPROM offset");
		goto out;
	}

	for (i = 0; i < words; i++) {
		eewr = ((offset + i) << IXGBE_EEPROM_RW_ADDR_SHIFT) |
			(data[i] << IXGBE_EEPROM_RW_REG_DATA) |
			IXGBE_EEPROM_RW_REG_START;

		status = ixgbe_poll_eerd_eewr_done(hw, IXGBE_NVM_POLL_WRITE);
		if (status != IXGBE_SUCCESS) {
			DEBUGOUT("Eeprom write EEWR timed out\n");
			goto out;
		}

		IXGBE_WRITE_REG(hw, IXGBE_EEWR, eewr);

		status = ixgbe_poll_eerd_eewr_done(hw, IXGBE_NVM_POLL_WRITE);
		if (status != IXGBE_SUCCESS) {
			DEBUGOUT("Eeprom write EEWR timed out\n");
			goto out;
		}
	}

out:
	return status;
}

/**
 *  ixgbe_write_eewr_generic - Write EEPROM word using EEWR
 *  @hw: pointer to hardware structure
 *  @offset: offset of  word in the EEPROM to write
 *  @data: word write to the EEPROM
 *
 *  Write a 16 bit word to the EEPROM using the EEWR register.
 **/
int32_t ixgbe_write_eewr_generic(struct ixgbe_hw *hw, uint16_t offset, uint16_t data)
{
	return ixgbe_write_eewr_buffer_generic(hw, offset, 1, &data);
}

/**
 *  ixgbe_poll_eerd_eewr_done - Poll EERD read or EEWR write status
 *  @hw: pointer to hardware structure
 *  @ee_reg: EEPROM flag for polling
 *
 *  Polls the status bit (bit 1) of the EERD or EEWR to determine when the
 *  read or write is done respectively.
 **/
int32_t ixgbe_poll_eerd_eewr_done(struct ixgbe_hw *hw, uint32_t ee_reg)
{
	uint32_t i;
	uint32_t reg;
	int32_t status = IXGBE_ERR_EEPROM;

	DEBUGFUNC("ixgbe_poll_eerd_eewr_done");

	for (i = 0; i < IXGBE_EERD_EEWR_ATTEMPTS; i++) {
		if (ee_reg == IXGBE_NVM_POLL_READ)
			reg = IXGBE_READ_REG(hw, IXGBE_EERD);
		else
			reg = IXGBE_READ_REG(hw, IXGBE_EEWR);

		if (reg & IXGBE_EEPROM_RW_REG_DONE) {
			status = IXGBE_SUCCESS;
			break;
		}
		usec_delay(5);
	}

	if (i == IXGBE_EERD_EEWR_ATTEMPTS)
		ERROR_REPORT1(IXGBE_ERROR_POLLING,
			     "EEPROM read/write done polling timed out");

	return status;
}

/**
 *  ixgbe_acquire_eeprom - Acquire EEPROM using bit-bang
 *  @hw: pointer to hardware structure
 *
 *  Prepares EEPROM for access using bit-bang method. This function should
 *  be called before issuing a command to the EEPROM.
 **/
int32_t ixgbe_acquire_eeprom(struct ixgbe_hw *hw)
{
	int32_t status = IXGBE_SUCCESS;
	uint32_t eec;
	uint32_t i;

	DEBUGFUNC("ixgbe_acquire_eeprom");

	if (hw->mac.ops.acquire_swfw_sync(hw, IXGBE_GSSR_EEP_SM)
	    != IXGBE_SUCCESS)
		status = IXGBE_ERR_SWFW_SYNC;

	if (status == IXGBE_SUCCESS) {
		eec = IXGBE_READ_REG(hw, IXGBE_EEC_BY_MAC(hw));

		/* Request EEPROM Access */
		eec |= IXGBE_EEC_REQ;
		IXGBE_WRITE_REG(hw, IXGBE_EEC_BY_MAC(hw), eec);

		for (i = 0; i < IXGBE_EEPROM_GRANT_ATTEMPTS; i++) {
			eec = IXGBE_READ_REG(hw, IXGBE_EEC_BY_MAC(hw));
			if (eec & IXGBE_EEC_GNT)
				break;
			usec_delay(5);
		}

		/* Release if grant not acquired */
		if (!(eec & IXGBE_EEC_GNT)) {
			eec &= ~IXGBE_EEC_REQ;
			IXGBE_WRITE_REG(hw, IXGBE_EEC_BY_MAC(hw), eec);
			DEBUGOUT("Could not acquire EEPROM grant\n");

			hw->mac.ops.release_swfw_sync(hw, IXGBE_GSSR_EEP_SM);
			status = IXGBE_ERR_EEPROM;
		}

		/* Setup EEPROM for Read/Write */
		if (status == IXGBE_SUCCESS) {
			/* Clear CS and SK */
			eec &= ~(IXGBE_EEC_CS | IXGBE_EEC_SK);
			IXGBE_WRITE_REG(hw, IXGBE_EEC_BY_MAC(hw), eec);
			IXGBE_WRITE_FLUSH(hw);
			usec_delay(1);
		}
	}
	return status;
}

/**
 *  ixgbe_get_eeprom_semaphore - Get hardware semaphore
 *  @hw: pointer to hardware structure
 *
 *  Sets the hardware semaphores so EEPROM access can occur for bit-bang method
 **/
int32_t ixgbe_get_eeprom_semaphore(struct ixgbe_hw *hw)
{
	int32_t status = IXGBE_ERR_EEPROM;
	uint32_t timeout = 2000;
	uint32_t i;
	uint32_t swsm;

	DEBUGFUNC("ixgbe_get_eeprom_semaphore");


	/* Get SMBI software semaphore between device drivers first */
	for (i = 0; i < timeout; i++) {
		/*
		 * If the SMBI bit is 0 when we read it, then the bit will be
		 * set and we have the semaphore
		 */
		swsm = IXGBE_READ_REG(hw, IXGBE_SWSM_BY_MAC(hw));
		if (!(swsm & IXGBE_SWSM_SMBI)) {
			status = IXGBE_SUCCESS;
			break;
		}
		usec_delay(50);
	}

	if (i == timeout) {
		DEBUGOUT("Driver can't access the Eeprom - SMBI Semaphore "
			 "not granted.\n");
		/*
		 * this release is particularly important because our attempts
		 * above to get the semaphore may have succeeded, and if there
		 * was a timeout, we should unconditionally clear the semaphore
		 * bits to free the driver to make progress
		 */
		ixgbe_release_eeprom_semaphore(hw);

		usec_delay(50);
		/*
		 * one last try
		 * If the SMBI bit is 0 when we read it, then the bit will be
		 * set and we have the semaphore
		 */
		swsm = IXGBE_READ_REG(hw, IXGBE_SWSM_BY_MAC(hw));
		if (!(swsm & IXGBE_SWSM_SMBI))
			status = IXGBE_SUCCESS;
	}

	/* Now get the semaphore between SW/FW through the SWESMBI bit */
	if (status == IXGBE_SUCCESS) {
		for (i = 0; i < timeout; i++) {
			swsm = IXGBE_READ_REG(hw, IXGBE_SWSM_BY_MAC(hw));

			/* Set the SW EEPROM semaphore bit to request access */
			swsm |= IXGBE_SWSM_SWESMBI;
			IXGBE_WRITE_REG(hw, IXGBE_SWSM_BY_MAC(hw), swsm);

			/*
			 * If we set the bit successfully then we got the
			 * semaphore.
			 */
			swsm = IXGBE_READ_REG(hw, IXGBE_SWSM_BY_MAC(hw));
			if (swsm & IXGBE_SWSM_SWESMBI)
				break;

			usec_delay(50);
		}

		/*
		 * Release semaphores and return error if SW EEPROM semaphore
		 * was not granted because we don't have access to the EEPROM
		 */
		if (i >= timeout) {
			ERROR_REPORT1(IXGBE_ERROR_POLLING,
			    "SWESMBI Software EEPROM semaphore not granted.\n");
			ixgbe_release_eeprom_semaphore(hw);
			status = IXGBE_ERR_EEPROM;
		}
	} else {
		ERROR_REPORT1(IXGBE_ERROR_POLLING,
			     "Software semaphore SMBI between device drivers "
			     "not granted.\n");
	}

	return status;
}

/**
 *  ixgbe_release_eeprom_semaphore - Release hardware semaphore
 *  @hw: pointer to hardware structure
 *
 *  This function clears hardware semaphore bits.
 **/
void ixgbe_release_eeprom_semaphore(struct ixgbe_hw *hw)
{
	uint32_t swsm;

	DEBUGFUNC("ixgbe_release_eeprom_semaphore");

	swsm = IXGBE_READ_REG(hw, IXGBE_SWSM);

	/* Release both semaphores by writing 0 to the bits SWESMBI and SMBI */
	swsm &= ~(IXGBE_SWSM_SWESMBI | IXGBE_SWSM_SMBI);
	IXGBE_WRITE_REG(hw, IXGBE_SWSM, swsm);
	IXGBE_WRITE_FLUSH(hw);
}

/**
 *  ixgbe_ready_eeprom - Polls for EEPROM ready
 *  @hw: pointer to hardware structure
 **/
int32_t ixgbe_ready_eeprom(struct ixgbe_hw *hw)
{
	int32_t status = IXGBE_SUCCESS;
	uint16_t i;
	uint8_t spi_stat_reg;

	DEBUGFUNC("ixgbe_ready_eeprom");

	/*
	 * Read "Status Register" repeatedly until the LSB is cleared.  The
	 * EEPROM will signal that the command has been completed by clearing
	 * bit 0 of the internal status register.  If it's not cleared within
	 * 5 milliseconds, then error out.
	 */
	for (i = 0; i < IXGBE_EEPROM_MAX_RETRY_SPI; i += 5) {
		ixgbe_shift_out_eeprom_bits(hw, IXGBE_EEPROM_RDSR_OPCODE_SPI,
					    IXGBE_EEPROM_OPCODE_BITS);
		spi_stat_reg = (uint8_t)ixgbe_shift_in_eeprom_bits(hw, 8);
		if (!(spi_stat_reg & IXGBE_EEPROM_STATUS_RDY_SPI))
			break;

		usec_delay(5);
		ixgbe_standby_eeprom(hw);
	}

	/*
	 * On some parts, SPI write time could vary from 0-20mSec on 3.3V
	 * devices (and only 0-5mSec on 5V devices)
	 */
	if (i >= IXGBE_EEPROM_MAX_RETRY_SPI) {
		DEBUGOUT("SPI EEPROM Status error\n");
		status = IXGBE_ERR_EEPROM;
	}

	return status;
}

/**
 *  ixgbe_standby_eeprom - Returns EEPROM to a "standby" state
 *  @hw: pointer to hardware structure
 **/
void ixgbe_standby_eeprom(struct ixgbe_hw *hw)
{
	uint32_t eec;

	DEBUGFUNC("ixgbe_standby_eeprom");

	eec = IXGBE_READ_REG(hw, IXGBE_EEC_BY_MAC(hw));

	/* Toggle CS to flush commands */
	eec |= IXGBE_EEC_CS;
	IXGBE_WRITE_REG(hw, IXGBE_EEC_BY_MAC(hw), eec);
	IXGBE_WRITE_FLUSH(hw);
	usec_delay(1);
	eec &= ~IXGBE_EEC_CS;
	IXGBE_WRITE_REG(hw, IXGBE_EEC_BY_MAC(hw), eec);
	IXGBE_WRITE_FLUSH(hw);
	usec_delay(1);
}

/**
 *  ixgbe_shift_out_eeprom_bits - Shift data bits out to the EEPROM.
 *  @hw: pointer to hardware structure
 *  @data: data to send to the EEPROM
 *  @count: number of bits to shift out
 **/
void ixgbe_shift_out_eeprom_bits(struct ixgbe_hw *hw, uint16_t data,
				 uint16_t count)
{
	uint32_t eec;
	uint32_t mask;
	uint32_t i;

	DEBUGFUNC("ixgbe_shift_out_eeprom_bits");

	eec = IXGBE_READ_REG(hw, IXGBE_EEC_BY_MAC(hw));

	/*
	 * Mask is used to shift "count" bits of "data" out to the EEPROM
	 * one bit at a time.  Determine the starting bit based on count
	 */
	mask = 0x01 << (count - 1);

	for (i = 0; i < count; i++) {
		/*
		 * A "1" is shifted out to the EEPROM by setting bit "DI" to a
		 * "1", and then raising and then lowering the clock (the SK
		 * bit controls the clock input to the EEPROM).  A "0" is
		 * shifted out to the EEPROM by setting "DI" to "0" and then
		 * raising and then lowering the clock.
		 */
		if (data & mask)
			eec |= IXGBE_EEC_DI;
		else
			eec &= ~IXGBE_EEC_DI;

		IXGBE_WRITE_REG(hw, IXGBE_EEC_BY_MAC(hw), eec);
		IXGBE_WRITE_FLUSH(hw);

		usec_delay(1);

		ixgbe_raise_eeprom_clk(hw, &eec);
		ixgbe_lower_eeprom_clk(hw, &eec);

		/*
		 * Shift mask to signify next bit of data to shift in to the
		 * EEPROM
		 */
		mask = mask >> 1;
	}

	/* We leave the "DI" bit set to "0" when we leave this routine. */
	eec &= ~IXGBE_EEC_DI;
	IXGBE_WRITE_REG(hw, IXGBE_EEC_BY_MAC(hw), eec);
	IXGBE_WRITE_FLUSH(hw);
}

/**
 *  ixgbe_shift_in_eeprom_bits - Shift data bits in from the EEPROM
 *  @hw: pointer to hardware structure
 *  @count: number of bits to shift
 **/
uint16_t ixgbe_shift_in_eeprom_bits(struct ixgbe_hw *hw, uint16_t count)
{
	uint32_t eec;
	uint32_t i;
	uint16_t data = 0;

	DEBUGFUNC("ixgbe_shift_in_eeprom_bits");

	/*
	 * In order to read a register from the EEPROM, we need to shift
	 * 'count' bits in from the EEPROM. Bits are "shifted in" by raising
	 * the clock input to the EEPROM (setting the SK bit), and then reading
	 * the value of the "DO" bit.  During this "shifting in" process the
	 * "DI" bit should always be clear.
	 */
	eec = IXGBE_READ_REG(hw, IXGBE_EEC_BY_MAC(hw));

	eec &= ~(IXGBE_EEC_DO | IXGBE_EEC_DI);

	for (i = 0; i < count; i++) {
		data = data << 1;
		ixgbe_raise_eeprom_clk(hw, &eec);

		eec = IXGBE_READ_REG(hw, IXGBE_EEC_BY_MAC(hw));

		eec &= ~(IXGBE_EEC_DI);
		if (eec & IXGBE_EEC_DO)
			data |= 1;

		ixgbe_lower_eeprom_clk(hw, &eec);
	}

	return data;
}

/**
 *  ixgbe_raise_eeprom_clk - Raises the EEPROM's clock input.
 *  @hw: pointer to hardware structure
 *  @eec: EEC register's current value
 **/
void ixgbe_raise_eeprom_clk(struct ixgbe_hw *hw, uint32_t *eec)
{
	DEBUGFUNC("ixgbe_raise_eeprom_clk");

	/*
	 * Raise the clock input to the EEPROM
	 * (setting the SK bit), then delay
	 */
	*eec = *eec | IXGBE_EEC_SK;
	IXGBE_WRITE_REG(hw, IXGBE_EEC_BY_MAC(hw), *eec);
	IXGBE_WRITE_FLUSH(hw);
	usec_delay(1);
}

/**
 *  ixgbe_lower_eeprom_clk - Lowers the EEPROM's clock input.
 *  @hw: pointer to hardware structure
 *  @eec: EEC's current value
 **/
void ixgbe_lower_eeprom_clk(struct ixgbe_hw *hw, uint32_t *eec)
{
	DEBUGFUNC("ixgbe_lower_eeprom_clk");

	/*
	 * Lower the clock input to the EEPROM (clearing the SK bit), then
	 * delay
	 */
	*eec = *eec & ~IXGBE_EEC_SK;
	IXGBE_WRITE_REG(hw, IXGBE_EEC_BY_MAC(hw), *eec);
	IXGBE_WRITE_FLUSH(hw);
	usec_delay(1);
}

/**
 *  ixgbe_release_eeprom - Release EEPROM, release semaphores
 *  @hw: pointer to hardware structure
 **/
void ixgbe_release_eeprom(struct ixgbe_hw *hw)
{
	uint32_t eec;

	DEBUGFUNC("ixgbe_release_eeprom");

	eec = IXGBE_READ_REG(hw, IXGBE_EEC_BY_MAC(hw));

	eec |= IXGBE_EEC_CS;  /* Pull CS high */
	eec &= ~IXGBE_EEC_SK; /* Lower SCK */

	IXGBE_WRITE_REG(hw, IXGBE_EEC_BY_MAC(hw), eec);
	IXGBE_WRITE_FLUSH(hw);

	usec_delay(1);

	/* Stop requesting EEPROM access */
	eec &= ~IXGBE_EEC_REQ;
	IXGBE_WRITE_REG(hw, IXGBE_EEC_BY_MAC(hw), eec);

	hw->mac.ops.release_swfw_sync(hw, IXGBE_GSSR_EEP_SM);

	/* Delay before attempt to obtain semaphore again to allow FW access */
	msec_delay(hw->eeprom.semaphore_delay);
}

/**
 *  ixgbe_calc_eeprom_checksum_generic - Calculates and returns the checksum
 *  @hw: pointer to hardware structure
 *
 *  Returns a negative error code on error, or the 16-bit checksum
 **/
int32_t ixgbe_calc_eeprom_checksum_generic(struct ixgbe_hw *hw)
{
	uint16_t i;
	uint16_t j;
	uint16_t checksum = 0;
	uint16_t length = 0;
	uint16_t pointer = 0;
	uint16_t word = 0;

	DEBUGFUNC("ixgbe_calc_eeprom_checksum_generic");

	/* Include 0x0-0x3F in the checksum */
	for (i = 0; i < IXGBE_EEPROM_CHECKSUM; i++) {
		if (hw->eeprom.ops.read(hw, i, &word)) {
			DEBUGOUT("EEPROM read failed\n");
			return IXGBE_ERR_EEPROM;
		}
		checksum += word;
	}

	/* Include all data from pointers except for the fw pointer */
	for (i = IXGBE_PCIE_ANALOG_PTR; i < IXGBE_FW_PTR; i++) {
		if (hw->eeprom.ops.read(hw, i, &pointer)) {
			DEBUGOUT("EEPROM read failed\n");
			return IXGBE_ERR_EEPROM;
		}

		/* If the pointer seems invalid */
		if (pointer == 0xFFFF || pointer == 0)
			continue;

		if (hw->eeprom.ops.read(hw, pointer, &length)) {
			DEBUGOUT("EEPROM read failed\n");
			return IXGBE_ERR_EEPROM;
		}

		if (length == 0xFFFF || length == 0)
			continue;

		for (j = pointer + 1; j <= pointer + length; j++) {
			if (hw->eeprom.ops.read(hw, j, &word)) {
				DEBUGOUT("EEPROM read failed\n");
				return IXGBE_ERR_EEPROM;
			}
			checksum += word;
		}
	}

	checksum = (uint16_t)IXGBE_EEPROM_SUM - checksum;

	return (int32_t)checksum;
}

/**
 *  ixgbe_validate_eeprom_checksum_generic - Validate EEPROM checksum
 *  @hw: pointer to hardware structure
 *  @checksum_val: calculated checksum
 *
 *  Performs checksum calculation and validates the EEPROM checksum.  If the
 *  caller does not need checksum_val, the value can be NULL.
 **/
int32_t ixgbe_validate_eeprom_checksum_generic(struct ixgbe_hw *hw,
					       uint16_t *checksum_val)
{
	int32_t status;
	uint16_t checksum;
	uint16_t read_checksum = 0;

	DEBUGFUNC("ixgbe_validate_eeprom_checksum_generic");

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

	checksum = (uint16_t)(status & 0xffff);

	status = hw->eeprom.ops.read(hw, IXGBE_EEPROM_CHECKSUM, &read_checksum);
	if (status) {
		DEBUGOUT("EEPROM read failed\n");
		return status;
	}

	/* Verify read checksum from EEPROM is the same as
	 * calculated checksum
	 */
	if (read_checksum != checksum)
		status = IXGBE_ERR_EEPROM_CHECKSUM;

	/* If the user cares, return the calculated checksum */
	if (checksum_val)
		*checksum_val = checksum;

	return status;
}

/**
 *  ixgbe_update_eeprom_checksum_generic - Updates the EEPROM checksum
 *  @hw: pointer to hardware structure
 **/
int32_t ixgbe_update_eeprom_checksum_generic(struct ixgbe_hw *hw)
{
	int32_t status;
	uint16_t checksum;

	DEBUGFUNC("ixgbe_update_eeprom_checksum_generic");

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

	checksum = (uint16_t)(status & 0xffff);

	status = hw->eeprom.ops.write(hw, IXGBE_EEPROM_CHECKSUM, checksum);

	return status;
}

/**
 *  ixgbe_validate_mac_addr - Validate MAC address
 *  @mac_addr: pointer to MAC address.
 *
 *  Tests a MAC address to ensure it is a valid Individual Address
 **/
int32_t ixgbe_validate_mac_addr(uint8_t *mac_addr)
{
	int32_t status = IXGBE_SUCCESS;

	DEBUGFUNC("ixgbe_validate_mac_addr");

	/* Make sure it is not a multicast address */
	if (IXGBE_IS_MULTICAST(mac_addr)) {
		DEBUGOUT("MAC address is multicast\n");
		status = IXGBE_ERR_INVALID_MAC_ADDR;
	/* Not a broadcast address */
	} else if (IXGBE_IS_BROADCAST(mac_addr)) {
		DEBUGOUT("MAC address is broadcast\n");
		status = IXGBE_ERR_INVALID_MAC_ADDR;
	/* Reject the zero address */
	} else if (mac_addr[0] == 0 && mac_addr[1] == 0 && mac_addr[2] == 0 &&
		   mac_addr[3] == 0 && mac_addr[4] == 0 && mac_addr[5] == 0) {
		DEBUGOUT("MAC address is all zeros\n");
		status = IXGBE_ERR_INVALID_MAC_ADDR;
	}
	return status;
}

/**
 *  ixgbe_set_rar_generic - Set Rx address register
 *  @hw: pointer to hardware structure
 *  @index: Receive address register to write
 *  @addr: Address to put into receive address register
 *  @vmdq: VMDq "set" or "pool" index
 *  @enable_addr: set flag that address is active
 *
 *  Puts an ethernet address into a receive address register.
 **/
int32_t ixgbe_set_rar_generic(struct ixgbe_hw *hw, uint32_t index, uint8_t *addr,
			      uint32_t vmdq, uint32_t enable_addr)
{
	uint32_t rar_low, rar_high;
	uint32_t rar_entries = hw->mac.num_rar_entries;

	DEBUGFUNC("ixgbe_set_rar_generic");

	/* Make sure we are using a valid rar index range */
	if (index >= rar_entries) {
		ERROR_REPORT2(IXGBE_ERROR_ARGUMENT,
			     "RAR index %d is out of range.\n", index);
		return IXGBE_ERR_INVALID_ARGUMENT;
	}

	/* setup VMDq pool selection before this RAR gets enabled */
	hw->mac.ops.set_vmdq(hw, index, vmdq);

	/*
	 * HW expects these in little endian so we reverse the byte
	 * order from network order (big endian) to little endian
	 */
	rar_low = ((uint32_t)addr[0] |
		   ((uint32_t)addr[1] << 8) |
		   ((uint32_t)addr[2] << 16) |
		   ((uint32_t)addr[3] << 24));
	/*
	 * Some parts put the VMDq setting in the extra RAH bits,
	 * so save everything except the lower 16 bits that hold part
	 * of the address and the address valid bit.
	 */
	rar_high = IXGBE_READ_REG(hw, IXGBE_RAH(index));
	rar_high &= ~(0x0000FFFF | IXGBE_RAH_AV);
	rar_high |= ((uint32_t)addr[4] | ((uint32_t)addr[5] << 8));

	if (enable_addr != 0)
		rar_high |= IXGBE_RAH_AV;

	IXGBE_WRITE_REG(hw, IXGBE_RAL(index), rar_low);
	IXGBE_WRITE_REG(hw, IXGBE_RAH(index), rar_high);

	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_clear_rar_generic - Remove Rx address register
 *  @hw: pointer to hardware structure
 *  @index: Receive address register to write
 *
 *  Clears an ethernet address from a receive address register.
 **/
int32_t ixgbe_clear_rar_generic(struct ixgbe_hw *hw, uint32_t index)
{
	uint32_t rar_high;
	uint32_t rar_entries = hw->mac.num_rar_entries;

	DEBUGFUNC("ixgbe_clear_rar_generic");

	/* Make sure we are using a valid rar index range */
	if (index >= rar_entries) {
		ERROR_REPORT2(IXGBE_ERROR_ARGUMENT,
			     "RAR index %d is out of range.\n", index);
		return IXGBE_ERR_INVALID_ARGUMENT;
	}

	/*
	 * Some parts put the VMDq setting in the extra RAH bits,
	 * so save everything except the lower 16 bits that hold part
	 * of the address and the address valid bit.
	 */
	rar_high = IXGBE_READ_REG(hw, IXGBE_RAH(index));
	rar_high &= ~(0x0000FFFF | IXGBE_RAH_AV);

	IXGBE_WRITE_REG(hw, IXGBE_RAL(index), 0);
	IXGBE_WRITE_REG(hw, IXGBE_RAH(index), rar_high);

	/* clear VMDq pool/queue selection for this RAR */
	hw->mac.ops.clear_vmdq(hw, index, IXGBE_CLEAR_VMDQ_ALL);

	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_init_rx_addrs_generic - Initializes receive address filters.
 *  @hw: pointer to hardware structure
 *
 *  Places the MAC address in receive address register 0 and clears the rest
 *  of the receive address registers. Clears the multicast table. Assumes
 *  the receiver is in reset when the routine is called.
 **/
int32_t ixgbe_init_rx_addrs_generic(struct ixgbe_hw *hw)
{
	uint32_t i;
	uint32_t rar_entries = hw->mac.num_rar_entries;

	DEBUGFUNC("ixgbe_init_rx_addrs_generic");

	/*
	 * If the current mac address is valid, assume it is a software override
	 * to the permanent address.
	 * Otherwise, use the permanent address from the eeprom.
	 */
	if (ixgbe_validate_mac_addr(hw->mac.addr) ==
	    IXGBE_ERR_INVALID_MAC_ADDR) {
		/* Get the MAC address from the RAR0 for later reference */
		hw->mac.ops.get_mac_addr(hw, hw->mac.addr);

		DEBUGOUT3(" Keeping Current RAR0 Addr =%.2X %.2X %.2X ",
			  hw->mac.addr[0], hw->mac.addr[1],
			  hw->mac.addr[2]);
		DEBUGOUT3("%.2X %.2X %.2X\n", hw->mac.addr[3],
			  hw->mac.addr[4], hw->mac.addr[5]);
	} else {
		/* Setup the receive address. */
		DEBUGOUT("Overriding MAC Address in RAR[0]\n");
		DEBUGOUT3(" New MAC Addr =%.2X %.2X %.2X ",
			  hw->mac.addr[0], hw->mac.addr[1],
			  hw->mac.addr[2]);
		DEBUGOUT3("%.2X %.2X %.2X\n", hw->mac.addr[3],
			  hw->mac.addr[4], hw->mac.addr[5]);

		hw->mac.ops.set_rar(hw, 0, hw->mac.addr, 0, IXGBE_RAH_AV);
	}

	/* clear VMDq pool/queue selection for RAR 0 */
	hw->mac.ops.clear_vmdq(hw, 0, IXGBE_CLEAR_VMDQ_ALL);

	hw->addr_ctrl.overflow_promisc = 0;

	hw->addr_ctrl.rar_used_count = 1;

	/* Zero out the other receive addresses. */
	DEBUGOUT1("Clearing RAR[1-%d]\n", rar_entries - 1);
	for (i = 1; i < rar_entries; i++) {
		IXGBE_WRITE_REG(hw, IXGBE_RAL(i), 0);
		IXGBE_WRITE_REG(hw, IXGBE_RAH(i), 0);
	}

	/* Clear the MTA */
	hw->addr_ctrl.mta_in_use = 0;
	IXGBE_WRITE_REG(hw, IXGBE_MCSTCTRL, hw->mac.mc_filter_type);

	DEBUGOUT(" Clearing MTA\n");
	for (i = 0; i < hw->mac.mcft_size; i++)
		IXGBE_WRITE_REG(hw, IXGBE_MTA(i), 0);

	ixgbe_init_uta_tables(hw);

	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_add_uc_addr - Adds a secondary unicast address.
 *  @hw: pointer to hardware structure
 *  @addr: new address
 *  @vmdq: VMDq "set" or "pool" index
 *
 *  Adds it to unused receive address register or goes into promiscuous mode.
 **/
void ixgbe_add_uc_addr(struct ixgbe_hw *hw, uint8_t *addr, uint32_t vmdq)
{
	uint32_t rar_entries = hw->mac.num_rar_entries;
	uint32_t rar;

	DEBUGFUNC("ixgbe_add_uc_addr");

	DEBUGOUT6(" UC Addr = %.2X %.2X %.2X %.2X %.2X %.2X\n",
		  addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);

	/*
	 * Place this address in the RAR if there is room,
	 * else put the controller into promiscuous mode
	 */
	if (hw->addr_ctrl.rar_used_count < rar_entries) {
		rar = hw->addr_ctrl.rar_used_count;
		hw->mac.ops.set_rar(hw, rar, addr, vmdq, IXGBE_RAH_AV);
		DEBUGOUT1("Added a secondary address to RAR[%d]\n", rar);
		hw->addr_ctrl.rar_used_count++;
	} else {
		hw->addr_ctrl.overflow_promisc++;
	}

	DEBUGOUT("ixgbe_add_uc_addr Complete\n");
}

/**
 *  ixgbe_mta_vector - Determines bit-vector in multicast table to set
 *  @hw: pointer to hardware structure
 *  @mc_addr: the multicast address
 *
 *  Extracts the 12 bits, from a multicast address, to determine which
 *  bit-vector to set in the multicast table. The hardware uses 12 bits, from
 *  incoming rx multicast addresses, to determine the bit-vector to check in
 *  the MTA. Which of the 4 combination, of 12-bits, the hardware uses is set
 *  by the MO field of the MCSTCTRL. The MO field is set during initialization
 *  to mc_filter_type.
 **/
int32_t ixgbe_mta_vector(struct ixgbe_hw *hw, uint8_t *mc_addr)
{
	uint32_t vector = 0;

	DEBUGFUNC("ixgbe_mta_vector");

	switch (hw->mac.mc_filter_type) {
	case 0:   /* use bits [47:36] of the address */
		vector = ((mc_addr[4] >> 4) | (((uint16_t)mc_addr[5]) << 4));
		break;
	case 1:   /* use bits [46:35] of the address */
		vector = ((mc_addr[4] >> 3) | (((uint16_t)mc_addr[5]) << 5));
		break;
	case 2:   /* use bits [45:34] of the address */
		vector = ((mc_addr[4] >> 2) | (((uint16_t)mc_addr[5]) << 6));
		break;
	case 3:   /* use bits [43:32] of the address */
		vector = ((mc_addr[4]) | (((uint16_t)mc_addr[5]) << 8));
		break;
	default:  /* Invalid mc_filter_type */
		DEBUGOUT("MC filter type param set incorrectly\n");
		panic("incorrect multicast filter type");
		break;
	}

	/* vector can only be 12-bits or boundary will be exceeded */
	vector &= 0xFFF;
	return vector;
}

/**
 *  ixgbe_set_mta - Set bit-vector in multicast table
 *  @hw: pointer to hardware structure
 *  @mc_addr: Multicast address
 *
 *  Sets the bit-vector in the multicast table.
 **/
void ixgbe_set_mta(struct ixgbe_hw *hw, uint8_t *mc_addr)
{
	uint32_t vector;
	uint32_t vector_bit;
	uint32_t vector_reg;

	DEBUGFUNC("ixgbe_set_mta");

	hw->addr_ctrl.mta_in_use++;

	vector = ixgbe_mta_vector(hw, mc_addr);
	DEBUGOUT1(" bit-vector = 0x%03X\n", vector);

	/*
	 * The MTA is a register array of 128 32-bit registers. It is treated
	 * like an array of 4096 bits.  We want to set bit
	 * BitArray[vector_value]. So we figure out what register the bit is
	 * in, read it, OR in the new bit, then write back the new value.  The
	 * register is determined by the upper 7 bits of the vector value and
	 * the bit within that register are determined by the lower 5 bits of
	 * the value.
	 */
	vector_reg = (vector >> 5) & 0x7F;
	vector_bit = vector & 0x1F;
	hw->mac.mta_shadow[vector_reg] |= (1 << vector_bit);
}

/**
 *  ixgbe_update_mc_addr_list_generic - Updates MAC list of multicast addresses
 *  @hw: pointer to hardware structure
 *  @mc_addr_list: the list of new multicast addresses
 *  @mc_addr_count: number of addresses
 *  @next: iterator function to walk the multicast address list
 *  @clear: flag, when set clears the table beforehand
 *
 *  When the clear flag is set, the given list replaces any existing list.
 *  Hashes the given addresses into the multicast table.
 **/
int32_t ixgbe_update_mc_addr_list_generic(struct ixgbe_hw *hw, uint8_t *mc_addr_list,
					  uint32_t mc_addr_count, ixgbe_mc_addr_itr next,
					  bool clear)
{
	uint32_t i;
	uint32_t vmdq;

	DEBUGFUNC("ixgbe_update_mc_addr_list_generic");

	/*
	 * Set the new number of MC addresses that we are being requested to
	 * use.
	 */
	hw->addr_ctrl.num_mc_addrs = mc_addr_count;
	hw->addr_ctrl.mta_in_use = 0;

	/* Clear mta_shadow */
	if (clear) {
		DEBUGOUT(" Clearing MTA\n");
		memset(&hw->mac.mta_shadow, 0, sizeof(hw->mac.mta_shadow));
	}

	/* Update mta_shadow */
	for (i = 0; i < mc_addr_count; i++) {
		DEBUGOUT(" Adding the multicast addresses:\n");
		ixgbe_set_mta(hw, next(hw, &mc_addr_list, &vmdq));
	}

	/* Enable mta */
	for (i = 0; i < hw->mac.mcft_size; i++)
		IXGBE_WRITE_REG_ARRAY(hw, IXGBE_MTA(0), i,
				      hw->mac.mta_shadow[i]);

	if (hw->addr_ctrl.mta_in_use > 0)
		IXGBE_WRITE_REG(hw, IXGBE_MCSTCTRL,
				IXGBE_MCSTCTRL_MFE | hw->mac.mc_filter_type);

	DEBUGOUT("ixgbe_update_mc_addr_list_generic Complete\n");
	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_enable_mc_generic - Enable multicast address in RAR
 *  @hw: pointer to hardware structure
 *
 *  Enables multicast address in RAR and the use of the multicast hash table.
 **/
int32_t ixgbe_enable_mc_generic(struct ixgbe_hw *hw)
{
	struct ixgbe_addr_filter_info *a = &hw->addr_ctrl;

	DEBUGFUNC("ixgbe_enable_mc_generic");

	if (a->mta_in_use > 0)
		IXGBE_WRITE_REG(hw, IXGBE_MCSTCTRL, IXGBE_MCSTCTRL_MFE |
				hw->mac.mc_filter_type);

	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_disable_mc_generic - Disable multicast address in RAR
 *  @hw: pointer to hardware structure
 *
 *  Disables multicast address in RAR and the use of the multicast hash table.
 **/
int32_t ixgbe_disable_mc_generic(struct ixgbe_hw *hw)
{
	struct ixgbe_addr_filter_info *a = &hw->addr_ctrl;

	DEBUGFUNC("ixgbe_disable_mc_generic");

	if (a->mta_in_use > 0)
		IXGBE_WRITE_REG(hw, IXGBE_MCSTCTRL, hw->mac.mc_filter_type);

	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_fc_enable_generic - Enable flow control
 *  @hw: pointer to hardware structure
 *
 *  Enable flow control according to the current settings.
 **/
int32_t ixgbe_fc_enable_generic(struct ixgbe_hw *hw)
{
	int32_t ret_val = IXGBE_SUCCESS;
	uint32_t mflcn_reg, fccfg_reg;
	uint32_t reg;
	uint32_t fcrtl, fcrth;
	int i;

	DEBUGFUNC("ixgbe_fc_enable_generic");

	/* Validate the water mark configuration */
	if (!hw->fc.pause_time) {
		ret_val = IXGBE_ERR_INVALID_LINK_SETTINGS;
		goto out;
	}

	/* Low water mark of zero causes XOFF floods */
	for (i = 0; i < IXGBE_DCB_MAX_TRAFFIC_CLASS; i++) {
		if ((hw->fc.current_mode & ixgbe_fc_tx_pause) &&
		    hw->fc.high_water[i]) {
			if (!hw->fc.low_water[i] ||
			    hw->fc.low_water[i] >= hw->fc.high_water[i]) {
				DEBUGOUT("Invalid water mark configuration\n");
				ret_val = IXGBE_ERR_INVALID_LINK_SETTINGS;
				goto out;
			}
		}
	}

	/* Negotiate the fc mode to use */
	hw->mac.ops.fc_autoneg(hw);

	/* Disable any previous flow control settings */
	mflcn_reg = IXGBE_READ_REG(hw, IXGBE_MFLCN);
	mflcn_reg &= ~(IXGBE_MFLCN_RPFCE_MASK | IXGBE_MFLCN_RFCE);

	fccfg_reg = IXGBE_READ_REG(hw, IXGBE_FCCFG);
	fccfg_reg &= ~(IXGBE_FCCFG_TFCE_802_3X | IXGBE_FCCFG_TFCE_PRIORITY);

	/*
	 * The possible values of fc.current_mode are:
	 * 0: Flow control is completely disabled
	 * 1: Rx flow control is enabled (we can receive pause frames,
	 *    but not send pause frames).
	 * 2: Tx flow control is enabled (we can send pause frames but
	 *    we do not support receiving pause frames).
	 * 3: Both Rx and Tx flow control (symmetric) are enabled.
	 * other: Invalid.
	 */
	switch (hw->fc.current_mode) {
	case ixgbe_fc_none:
		/*
		 * Flow control is disabled by software override or autoneg.
		 * The code below will actually disable it in the HW.
		 */
		break;
	case ixgbe_fc_rx_pause:
		/*
		 * Rx Flow control is enabled and Tx Flow control is
		 * disabled by software override. Since there really
		 * isn't a way to advertise that we are capable of RX
		 * Pause ONLY, we will advertise that we support both
		 * symmetric and asymmetric Rx PAUSE.  Later, we will
		 * disable the adapter's ability to send PAUSE frames.
		 */
		mflcn_reg |= IXGBE_MFLCN_RFCE;
		break;
	case ixgbe_fc_tx_pause:
		/*
		 * Tx Flow control is enabled, and Rx Flow control is
		 * disabled by software override.
		 */
		fccfg_reg |= IXGBE_FCCFG_TFCE_802_3X;
		break;
	case ixgbe_fc_full:
		/* Flow control (both Rx and Tx) is enabled by SW override. */
		mflcn_reg |= IXGBE_MFLCN_RFCE;
		fccfg_reg |= IXGBE_FCCFG_TFCE_802_3X;
		break;
	default:
		ERROR_REPORT1(IXGBE_ERROR_ARGUMENT,
			     "Flow control param set incorrectly\n");
		ret_val = IXGBE_ERR_CONFIG;
		goto out;
		break;
	}

	/* Set 802.3x based flow control settings. */
	mflcn_reg |= IXGBE_MFLCN_DPF;
	IXGBE_WRITE_REG(hw, IXGBE_MFLCN, mflcn_reg);
	IXGBE_WRITE_REG(hw, IXGBE_FCCFG, fccfg_reg);


	/* Set up and enable Rx high/low water mark thresholds, enable XON. */
	for (i = 0; i < IXGBE_DCB_MAX_TRAFFIC_CLASS; i++) {
		if ((hw->fc.current_mode & ixgbe_fc_tx_pause) &&
		    hw->fc.high_water[i]) {
			fcrtl = (hw->fc.low_water[i] << 10) | IXGBE_FCRTL_XONE;
			IXGBE_WRITE_REG(hw, IXGBE_FCRTL_82599(i), fcrtl);
			fcrth = (hw->fc.high_water[i] << 10) | IXGBE_FCRTH_FCEN;
		} else {
			IXGBE_WRITE_REG(hw, IXGBE_FCRTL_82599(i), 0);
			/*
			 * In order to prevent Tx hangs when the internal Tx
			 * switch is enabled we must set the high water mark
			 * to the Rx packet buffer size - 24KB.  This allows
			 * the Tx switch to function even under heavy Rx
			 * workloads.
			 */
			fcrth = IXGBE_READ_REG(hw, IXGBE_RXPBSIZE(i)) - 0x6000;
		}

		IXGBE_WRITE_REG(hw, IXGBE_FCRTH_82599(i), fcrth);
	}

	/* Configure pause time (2 TCs per register) */
	reg = (uint32_t)hw->fc.pause_time * 0x00010001;
	for (i = 0; i < (IXGBE_DCB_MAX_TRAFFIC_CLASS / 2); i++)
		IXGBE_WRITE_REG(hw, IXGBE_FCTTV(i), reg);

	/* Configure flow control refresh threshold value */
	IXGBE_WRITE_REG(hw, IXGBE_FCRTV, hw->fc.pause_time / 2);

out:
	return ret_val;
}

/**
 *  ixgbe_negotiate_fc - Negotiate flow control
 *  @hw: pointer to hardware structure
 *  @adv_reg: flow control advertised settings
 *  @lp_reg: link partner's flow control settings
 *  @adv_sym: symmetric pause bit in advertisement
 *  @adv_asm: asymmetric pause bit in advertisement
 *  @lp_sym: symmetric pause bit in link partner advertisement
 *  @lp_asm: asymmetric pause bit in link partner advertisement
 *
 *  Find the intersection between advertised settings and link partner's
 *  advertised settings
 **/
int32_t ixgbe_negotiate_fc(struct ixgbe_hw *hw, uint32_t adv_reg,
			   uint32_t lp_reg, uint32_t adv_sym,
			   uint32_t adv_asm, uint32_t lp_sym,
			   uint32_t lp_asm)
{
	if ((!(adv_reg)) ||  (!(lp_reg))) {
		ERROR_REPORT3(IXGBE_ERROR_UNSUPPORTED,
			     "Local or link partner's advertised flow control "
			     "settings are NULL. Local: %x, link partner: %x\n",
			     adv_reg, lp_reg);
		return IXGBE_ERR_FC_NOT_NEGOTIATED;
	}

	if ((adv_reg & adv_sym) && (lp_reg & lp_sym)) {
		/*
		 * Now we need to check if the user selected Rx ONLY
		 * of pause frames.  In this case, we had to advertise
		 * FULL flow control because we could not advertise RX
		 * ONLY. Hence, we must now check to see if we need to
		 * turn OFF the TRANSMISSION of PAUSE frames.
		 */
		if (hw->fc.requested_mode == ixgbe_fc_full) {
			hw->fc.current_mode = ixgbe_fc_full;
			DEBUGOUT("Flow Control = FULL.\n");
		} else {
			hw->fc.current_mode = ixgbe_fc_rx_pause;
			DEBUGOUT("Flow Control=RX PAUSE frames only\n");
		}
	} else if (!(adv_reg & adv_sym) && (adv_reg & adv_asm) &&
		   (lp_reg & lp_sym) && (lp_reg & lp_asm)) {
		hw->fc.current_mode = ixgbe_fc_tx_pause;
		DEBUGOUT("Flow Control = TX PAUSE frames only.\n");
	} else if ((adv_reg & adv_sym) && (adv_reg & adv_asm) &&
		   !(lp_reg & lp_sym) && (lp_reg & lp_asm)) {
		hw->fc.current_mode = ixgbe_fc_rx_pause;
		DEBUGOUT("Flow Control = RX PAUSE frames only.\n");
	} else {
		hw->fc.current_mode = ixgbe_fc_none;
		DEBUGOUT("Flow Control = NONE.\n");
	}
	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_fc_autoneg_fiber - Enable flow control on 1 gig fiber
 *  @hw: pointer to hardware structure
 *
 *  Enable flow control according on 1 gig fiber.
 **/
int32_t ixgbe_fc_autoneg_fiber(struct ixgbe_hw *hw)
{
	uint32_t pcs_anadv_reg, pcs_lpab_reg, linkstat;
	int32_t ret_val = IXGBE_ERR_FC_NOT_NEGOTIATED;

	/*
	 * On multispeed fiber at 1g, bail out if
	 * - link is up but AN did not complete, or if
	 * - link is up and AN completed but timed out
	 */

	linkstat = IXGBE_READ_REG(hw, IXGBE_PCS1GLSTA);
	if ((!!(linkstat & IXGBE_PCS1GLSTA_AN_COMPLETE) == 0) ||
	    (!!(linkstat & IXGBE_PCS1GLSTA_AN_TIMED_OUT) == 1)) {
		DEBUGOUT("Auto-Negotiation did not complete or timed out\n");
		goto out;
	}

	pcs_anadv_reg = IXGBE_READ_REG(hw, IXGBE_PCS1GANA);
	pcs_lpab_reg = IXGBE_READ_REG(hw, IXGBE_PCS1GANLP);

	ret_val =  ixgbe_negotiate_fc(hw, pcs_anadv_reg,
				      pcs_lpab_reg, IXGBE_PCS1GANA_SYM_PAUSE,
				      IXGBE_PCS1GANA_ASM_PAUSE,
				      IXGBE_PCS1GANA_SYM_PAUSE,
				      IXGBE_PCS1GANA_ASM_PAUSE);

out:
	return ret_val;
}

/**
 *  ixgbe_fc_autoneg_backplane - Enable flow control IEEE clause 37
 *  @hw: pointer to hardware structure
 *
 *  Enable flow control according to IEEE clause 37.
 **/
int32_t ixgbe_fc_autoneg_backplane(struct ixgbe_hw *hw)
{
	uint32_t links2, anlp1_reg, autoc_reg, links;
	int32_t ret_val = IXGBE_ERR_FC_NOT_NEGOTIATED;

	/*
	 * On backplane, bail out if
	 * - backplane autoneg was not completed, or if
	 * - we are 82599 and link partner is not AN enabled
	 */
	links = IXGBE_READ_REG(hw, IXGBE_LINKS);
	if ((links & IXGBE_LINKS_KX_AN_COMP) == 0) {
		DEBUGOUT("Auto-Negotiation did not complete\n");
		goto out;
	}

	if (hw->mac.type == ixgbe_mac_82599EB) {
		links2 = IXGBE_READ_REG(hw, IXGBE_LINKS2);
		if ((links2 & IXGBE_LINKS2_AN_SUPPORTED) == 0) {
			DEBUGOUT("Link partner is not AN enabled\n");
			goto out;
		}
	}
	/*
	 * Read the 10g AN autoc and LP ability registers and resolve
	 * local flow control settings accordingly
	 */
	autoc_reg = IXGBE_READ_REG(hw, IXGBE_AUTOC);
	anlp1_reg = IXGBE_READ_REG(hw, IXGBE_ANLP1);

	ret_val = ixgbe_negotiate_fc(hw, autoc_reg,
		anlp1_reg, IXGBE_AUTOC_SYM_PAUSE, IXGBE_AUTOC_ASM_PAUSE,
		IXGBE_ANLP1_SYM_PAUSE, IXGBE_ANLP1_ASM_PAUSE);

out:
	return ret_val;
}

/**
 *  ixgbe_fc_autoneg_copper - Enable flow control IEEE clause 37
 *  @hw: pointer to hardware structure
 *
 *  Enable flow control according to IEEE clause 37.
 **/
int32_t ixgbe_fc_autoneg_copper(struct ixgbe_hw *hw)
{
	uint16_t technology_ability_reg = 0;
	uint16_t lp_technology_ability_reg = 0;

	hw->phy.ops.read_reg(hw, IXGBE_MDIO_AUTO_NEG_ADVT,
			     IXGBE_MDIO_AUTO_NEG_DEV_TYPE,
			     &technology_ability_reg);
	hw->phy.ops.read_reg(hw, IXGBE_MDIO_AUTO_NEG_LP,
			     IXGBE_MDIO_AUTO_NEG_DEV_TYPE,
			     &lp_technology_ability_reg);

	return ixgbe_negotiate_fc(hw, (uint32_t)technology_ability_reg,
				  (uint32_t)lp_technology_ability_reg,
				  IXGBE_TAF_SYM_PAUSE, IXGBE_TAF_ASM_PAUSE,
				  IXGBE_TAF_SYM_PAUSE, IXGBE_TAF_ASM_PAUSE);
}

/**
 *  ixgbe_fc_autoneg - Configure flow control
 *  @hw: pointer to hardware structure
 *
 *  Compares our advertised flow control capabilities to those advertised by
 *  our link partner, and determines the proper flow control mode to use.
 **/
void ixgbe_fc_autoneg(struct ixgbe_hw *hw)
{
	int32_t ret_val = IXGBE_ERR_FC_NOT_NEGOTIATED;
	ixgbe_link_speed speed;
	bool link_up;

	DEBUGFUNC("ixgbe_fc_autoneg");

	/*
	 * AN should have completed when the cable was plugged in.
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

	switch (hw->phy.media_type) {
	/* Autoneg flow control on fiber adapters */
	case ixgbe_media_type_fiber_fixed:
	case ixgbe_media_type_fiber_qsfp:
	case ixgbe_media_type_fiber:
		if (speed == IXGBE_LINK_SPEED_1GB_FULL)
			ret_val = ixgbe_fc_autoneg_fiber(hw);
		break;

	/* Autoneg flow control on backplane adapters */
	case ixgbe_media_type_backplane:
		ret_val = ixgbe_fc_autoneg_backplane(hw);
		break;

	/* Autoneg flow control on copper adapters */
	case ixgbe_media_type_copper:
		if (ixgbe_device_supports_autoneg_fc(hw))
			ret_val = ixgbe_fc_autoneg_copper(hw);
		break;

	default:
		break;
	}

out:
	if (ret_val == IXGBE_SUCCESS) {
		hw->fc.fc_was_autonegged = TRUE;
	} else {
		hw->fc.fc_was_autonegged = FALSE;
		hw->fc.current_mode = hw->fc.requested_mode;
	}
}

/*
 * ixgbe_pcie_timeout_poll - Return number of times to poll for completion
 * @hw: pointer to hardware structure
 *
 * System-wide timeout range is encoded in PCIe Device Control2 register.
 *
 * Add 10% to specified maximum and return the number of times to poll for
 * completion timeout, in units of 100 microsec.  Never return less than
 * 800 = 80 millisec.
 */
static uint32_t ixgbe_pcie_timeout_poll(struct ixgbe_hw *hw)
{
	int16_t devctl2;
	uint32_t pollcnt;

	devctl2 = IXGBE_READ_PCIE_WORD(hw, IXGBE_PCI_DEVICE_CONTROL2);
	devctl2 &= IXGBE_PCIDEVCTRL2_TIMEO_MASK;

	switch (devctl2) {
	case IXGBE_PCIDEVCTRL2_65_130ms:
		pollcnt = 1300;		/* 130 millisec */
		break;
	case IXGBE_PCIDEVCTRL2_260_520ms:
		pollcnt = 5200;		/* 520 millisec */
		break;
	case IXGBE_PCIDEVCTRL2_1_2s:
		pollcnt = 20000;	/* 2 sec */
		break;
	case IXGBE_PCIDEVCTRL2_4_8s:
		pollcnt = 80000;	/* 8 sec */
		break;
	case IXGBE_PCIDEVCTRL2_17_34s:
		pollcnt = 34000;	/* 34 sec */
		break;
	case IXGBE_PCIDEVCTRL2_50_100us:	/* 100 microsecs */
	case IXGBE_PCIDEVCTRL2_1_2ms:		/* 2 millisecs */
	case IXGBE_PCIDEVCTRL2_16_32ms:		/* 32 millisec */
	case IXGBE_PCIDEVCTRL2_16_32ms_def:	/* 32 millisec default */
	default:
		pollcnt = 800;		/* 80 millisec minimum */
		break;
	}

	/* add 10% to spec maximum */
	return (pollcnt * 11) / 10;
}

/**
 *  ixgbe_disable_pcie_master - Disable PCI-express master access
 *  @hw: pointer to hardware structure
 *
 *  Disables PCI-Express master access and verifies there are no pending
 *  requests. IXGBE_ERR_MASTER_REQUESTS_PENDING is returned if master disable
 *  bit hasn't caused the master requests to be disabled, else IXGBE_SUCCESS
 *  is returned signifying master requests disabled.
 **/
int32_t ixgbe_disable_pcie_master(struct ixgbe_hw *hw)
{
	int32_t status = IXGBE_SUCCESS;
	uint32_t i, poll;
	uint16_t value;

	DEBUGFUNC("ixgbe_disable_pcie_master");

	/* Always set this bit to ensure any future transactions are blocked */
	IXGBE_WRITE_REG(hw, IXGBE_CTRL, IXGBE_CTRL_GIO_DIS);

	/* Exit if master requests are blocked */
	if (!(IXGBE_READ_REG(hw, IXGBE_STATUS) & IXGBE_STATUS_GIO) ||
	    IXGBE_REMOVED(hw->hw_addr))
		goto out;

	/* Poll for master request bit to clear */
	for (i = 0; i < IXGBE_PCI_MASTER_DISABLE_TIMEOUT; i++) {
		usec_delay(100);
		if (!(IXGBE_READ_REG(hw, IXGBE_STATUS) & IXGBE_STATUS_GIO))
			goto out;
	}

	/*
	 * Two consecutive resets are required via CTRL.RST per datasheet
	 * 5.2.5.3.2 Master Disable.  We set a flag to inform the reset routine
	 * of this need.  The first reset prevents new master requests from
	 * being issued by our device.  We then must wait 1usec or more for any
	 * remaining completions from the PCIe bus to trickle in, and then reset
	 * again to clear out any effects they may have had on our device.
	 */
	DEBUGOUT("GIO Master Disable bit didn't clear - requesting resets\n");
	hw->mac.flags |= IXGBE_FLAGS_DOUBLE_RESET_REQUIRED;

	if (hw->mac.type >= ixgbe_mac_X550)
		goto out;

	/*
	 * Before proceeding, make sure that the PCIe block does not have
	 * transactions pending.
	 */
	poll = ixgbe_pcie_timeout_poll(hw);
	for (i = 0; i < poll; i++) {
		usec_delay(100);
		value = IXGBE_READ_PCIE_WORD(hw, IXGBE_PCI_DEVICE_STATUS);
		if (IXGBE_REMOVED(hw->hw_addr))
			goto out;
		if (!(value & IXGBE_PCI_DEVICE_STATUS_TRANSACTION_PENDING))
			goto out;
	}

	ERROR_REPORT1(IXGBE_ERROR_POLLING,
		     "PCIe transaction pending bit also did not clear.\n");
	status = IXGBE_ERR_MASTER_REQUESTS_PENDING;

out:
	return status;
}

/**
 *  ixgbe_acquire_swfw_sync - Acquire SWFW semaphore
 *  @hw: pointer to hardware structure
 *  @mask: Mask to specify which semaphore to acquire
 *
 *  Acquires the SWFW semaphore through the GSSR register for the specified
 *  function (CSR, PHY0, PHY1, EEPROM, Flash)
 **/
int32_t ixgbe_acquire_swfw_sync(struct ixgbe_hw *hw, uint32_t mask)
{
	uint32_t gssr = 0;
	uint32_t swmask = mask;
	uint32_t fwmask = mask << 5;
	uint32_t timeout = 200;
	uint32_t i;

	DEBUGFUNC("ixgbe_acquire_swfw_sync");

	for (i = 0; i < timeout; i++) {
		/*
		 * SW NVM semaphore bit is used for access to all
		 * SW_FW_SYNC bits (not just NVM)
		 */
		if (ixgbe_get_eeprom_semaphore(hw))
			return IXGBE_ERR_SWFW_SYNC;

		gssr = IXGBE_READ_REG(hw, IXGBE_GSSR);
		if (!(gssr & (fwmask | swmask))) {
			gssr |= swmask;
			IXGBE_WRITE_REG(hw, IXGBE_GSSR, gssr);
			ixgbe_release_eeprom_semaphore(hw);
			return IXGBE_SUCCESS;
		} else {
			/* Resource is currently in use by FW or SW */
			ixgbe_release_eeprom_semaphore(hw);
			msec_delay(5);
		}
	}

	/* If time expired clear the bits holding the lock and retry */
	if (gssr & (fwmask | swmask))
		ixgbe_release_swfw_sync(hw, gssr & (fwmask | swmask));

	msec_delay(5);
	return IXGBE_ERR_SWFW_SYNC;
}

/**
 *  ixgbe_release_swfw_sync - Release SWFW semaphore
 *  @hw: pointer to hardware structure
 *  @mask: Mask to specify which semaphore to release
 *
 *  Releases the SWFW semaphore through the GSSR register for the specified
 *  function (CSR, PHY0, PHY1, EEPROM, Flash)
 **/
void ixgbe_release_swfw_sync(struct ixgbe_hw *hw, uint32_t mask)
{
	uint32_t gssr;
	uint32_t swmask = mask;

	DEBUGFUNC("ixgbe_release_swfw_sync");

	ixgbe_get_eeprom_semaphore(hw);

	gssr = IXGBE_READ_REG(hw, IXGBE_GSSR);
	gssr &= ~swmask;
	IXGBE_WRITE_REG(hw, IXGBE_GSSR, gssr);

	ixgbe_release_eeprom_semaphore(hw);
}

/**
 *  ixgbe_disable_sec_rx_path_generic - Stops the receive data path
 *  @hw: pointer to hardware structure
 *
 *  Stops the receive data path and waits for the HW to internally empty
 *  the Rx security block
 **/
int32_t ixgbe_disable_sec_rx_path_generic(struct ixgbe_hw *hw)
{
#define IXGBE_MAX_SECRX_POLL 40

	int i;
	int secrxreg;

	DEBUGFUNC("ixgbe_disable_sec_rx_path_generic");


	secrxreg = IXGBE_READ_REG(hw, IXGBE_SECRXCTRL);
	secrxreg |= IXGBE_SECRXCTRL_RX_DIS;
	IXGBE_WRITE_REG(hw, IXGBE_SECRXCTRL, secrxreg);
	for (i = 0; i < IXGBE_MAX_SECRX_POLL; i++) {
		secrxreg = IXGBE_READ_REG(hw, IXGBE_SECRXSTAT);
		if (secrxreg & IXGBE_SECRXSTAT_SECRX_RDY)
			break;
		else
			/* Use interrupt-safe sleep just in case */
			usec_delay(1000);
	}

	/* For informational purposes only */
	if (i >= IXGBE_MAX_SECRX_POLL)
		DEBUGOUT("Rx unit being enabled before security "
			 "path fully disabled.  Continuing with init.\n");

	return IXGBE_SUCCESS;
}

/**
 *  prot_autoc_read_generic - Hides MAC differences needed for AUTOC read
 *  @hw: pointer to hardware structure
 *  @locked: bool to indicate whether the SW/FW lock was taken
 *  @reg_val: Value we read from AUTOC
 *
 *  The default case requires no protection so just to the register read.
 */
int32_t prot_autoc_read_generic(struct ixgbe_hw *hw, bool *locked,
				uint32_t *reg_val)
{
	*locked = FALSE;
	*reg_val = IXGBE_READ_REG(hw, IXGBE_AUTOC);
	return IXGBE_SUCCESS;
}

/**
 * prot_autoc_write_generic - Hides MAC differences needed for AUTOC write
 * @hw: pointer to hardware structure
 * @reg_val: value to write to AUTOC
 * @locked: bool to indicate whether the SW/FW lock was already taken by
 *           previous read.
 *
 * The default case requires no protection so just to the register write.
 */
int32_t prot_autoc_write_generic(struct ixgbe_hw *hw, uint32_t reg_val,
				 bool locked)
{
	IXGBE_WRITE_REG(hw, IXGBE_AUTOC, reg_val);
	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_enable_sec_rx_path_generic - Enables the receive data path
 *  @hw: pointer to hardware structure
 *
 *  Enables the receive data path.
 **/
int32_t ixgbe_enable_sec_rx_path_generic(struct ixgbe_hw *hw)
{
	uint32_t secrxreg;

	DEBUGFUNC("ixgbe_enable_sec_rx_path_generic");

	secrxreg = IXGBE_READ_REG(hw, IXGBE_SECRXCTRL);
	secrxreg &= ~IXGBE_SECRXCTRL_RX_DIS;
	IXGBE_WRITE_REG(hw, IXGBE_SECRXCTRL, secrxreg);
	IXGBE_WRITE_FLUSH(hw);

	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_enable_rx_dma_generic - Enable the Rx DMA unit
 *  @hw: pointer to hardware structure
 *  @regval: register value to write to RXCTRL
 *
 *  Enables the Rx DMA unit
 **/
int32_t ixgbe_enable_rx_dma_generic(struct ixgbe_hw *hw, uint32_t regval)
{
	DEBUGFUNC("ixgbe_enable_rx_dma_generic");

	if (regval & IXGBE_RXCTRL_RXEN)
		ixgbe_enable_rx(hw);
	else
		ixgbe_disable_rx(hw);

	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_blink_led_start_generic - Blink LED based on index.
 *  @hw: pointer to hardware structure
 *  @index: led number to blink
 **/
int32_t ixgbe_blink_led_start_generic(struct ixgbe_hw *hw, uint32_t index)
{
	ixgbe_link_speed speed = 0;
	bool link_up = 0;
	uint32_t autoc_reg = 0;
	uint32_t led_reg = IXGBE_READ_REG(hw, IXGBE_LEDCTL);
	int32_t ret_val = IXGBE_SUCCESS;
	bool locked = FALSE;

	DEBUGFUNC("ixgbe_blink_led_start_generic");

	if (index > 3)
		return IXGBE_ERR_PARAM;

	/*
	 * Link must be up to auto-blink the LEDs;
	 * Force it if link is down.
	 */
	hw->mac.ops.check_link(hw, &speed, &link_up, FALSE);

	if (!link_up) {
		ret_val = hw->mac.ops.prot_autoc_read(hw, &locked, &autoc_reg);
		if (ret_val != IXGBE_SUCCESS)
			goto out;

		autoc_reg |= IXGBE_AUTOC_AN_RESTART;
		autoc_reg |= IXGBE_AUTOC_FLU;

		ret_val = hw->mac.ops.prot_autoc_write(hw, autoc_reg, locked);
		if (ret_val != IXGBE_SUCCESS)
			goto out;

		IXGBE_WRITE_FLUSH(hw);
		msec_delay(10);
	}

	led_reg &= ~IXGBE_LED_MODE_MASK(index);
	led_reg |= IXGBE_LED_BLINK(index);
	IXGBE_WRITE_REG(hw, IXGBE_LEDCTL, led_reg);
	IXGBE_WRITE_FLUSH(hw);

out:
	return ret_val;
}

/**
 *  ixgbe_blink_led_stop_generic - Stop blinking LED based on index.
 *  @hw: pointer to hardware structure
 *  @index: led number to stop blinking
 **/
int32_t ixgbe_blink_led_stop_generic(struct ixgbe_hw *hw, uint32_t index)
{
	uint32_t autoc_reg = 0;
	uint32_t led_reg = IXGBE_READ_REG(hw, IXGBE_LEDCTL);
	int32_t ret_val = IXGBE_SUCCESS;
	bool locked = FALSE;

	DEBUGFUNC("ixgbe_blink_led_stop_generic");

	if (index > 3)
		return IXGBE_ERR_PARAM;

	ret_val = hw->mac.ops.prot_autoc_read(hw, &locked, &autoc_reg);
	if (ret_val != IXGBE_SUCCESS)
		goto out;

	autoc_reg &= ~IXGBE_AUTOC_FLU;
	autoc_reg |= IXGBE_AUTOC_AN_RESTART;

	ret_val = hw->mac.ops.prot_autoc_write(hw, autoc_reg, locked);
	if (ret_val != IXGBE_SUCCESS)
		goto out;

	led_reg &= ~IXGBE_LED_MODE_MASK(index);
	led_reg &= ~IXGBE_LED_BLINK(index);
	led_reg |= IXGBE_LED_LINK_ACTIVE << IXGBE_LED_MODE_SHIFT(index);
	IXGBE_WRITE_REG(hw, IXGBE_LEDCTL, led_reg);
	IXGBE_WRITE_FLUSH(hw);

out:
	return ret_val;
}

/**
 *  ixgbe_get_pcie_msix_count_generic - Gets MSI-X vector count
 *  @hw: pointer to hardware structure
 *
 *  Read PCIe configuration space, and get the MSI-X vector count from
 *  the capabilities table.
 **/
uint16_t ixgbe_get_pcie_msix_count_generic(struct ixgbe_hw *hw)
{
	uint16_t msix_count = 1;
	uint16_t max_msix_count;
	uint16_t pcie_offset;

	switch (hw->mac.type) {
	case ixgbe_mac_82598EB:
		pcie_offset = IXGBE_PCIE_MSIX_82598_CAPS;
		max_msix_count = IXGBE_MAX_MSIX_VECTORS_82598;
		break;
	case ixgbe_mac_82599EB:
	case ixgbe_mac_X540:
	case ixgbe_mac_X550:
	case ixgbe_mac_X550EM_x:
	case ixgbe_mac_X550EM_a:
		pcie_offset = IXGBE_PCIE_MSIX_82599_CAPS;
		max_msix_count = IXGBE_MAX_MSIX_VECTORS_82599;
		break;
	default:
		return msix_count;
	}

	DEBUGFUNC("ixgbe_get_pcie_msix_count_generic");
	msix_count = IXGBE_READ_PCIE_WORD(hw, pcie_offset);
	if (IXGBE_REMOVED(hw->hw_addr))
		msix_count = 0;
	msix_count &= IXGBE_PCIE_MSIX_TBL_SZ_MASK;

	/* MSI-X count is zero-based in HW */
	msix_count++;

	if (msix_count > max_msix_count)
		msix_count = max_msix_count;

	return msix_count;
}

/**
 *  ixgbe_insert_mac_addr_generic - Find a RAR for this mac address
 *  @hw: pointer to hardware structure
 *  @addr: Address to put into receive address register
 *  @vmdq: VMDq pool to assign
 *
 *  Puts an ethernet address into a receive address register, or
 *  finds the rar that it is already in; adds to the pool list
 **/
int32_t ixgbe_insert_mac_addr_generic(struct ixgbe_hw *hw, uint8_t *addr, uint32_t vmdq)
{
	static const uint32_t NO_EMPTY_RAR_FOUND = 0xFFFFFFFF;
	uint32_t first_empty_rar = NO_EMPTY_RAR_FOUND;
	uint32_t rar;
	uint32_t rar_low, rar_high;
	uint32_t addr_low, addr_high;

	DEBUGFUNC("ixgbe_insert_mac_addr_generic");

	/* swap bytes for HW little endian */
	addr_low  = addr[0] | (addr[1] << 8)
			    | (addr[2] << 16)
			    | (addr[3] << 24);
	addr_high = addr[4] | (addr[5] << 8);

	/*
	 * Either find the mac_id in rar or find the first empty space.
	 * rar_highwater points to just after the highest currently used
	 * rar in order to shorten the search.  It grows when we add a new
	 * rar to the top.
	 */
	for (rar = 0; rar < hw->mac.rar_highwater; rar++) {
		rar_high = IXGBE_READ_REG(hw, IXGBE_RAH(rar));

		if (((IXGBE_RAH_AV & rar_high) == 0)
		    && first_empty_rar == NO_EMPTY_RAR_FOUND) {
			first_empty_rar = rar;
		} else if ((rar_high & 0xFFFF) == addr_high) {
			rar_low = IXGBE_READ_REG(hw, IXGBE_RAL(rar));
			if (rar_low == addr_low)
				break;    /* found it already in the rars */
		}
	}

	if (rar < hw->mac.rar_highwater) {
		/* already there so just add to the pool bits */
		ixgbe_set_vmdq(hw, rar, vmdq);
	} else if (first_empty_rar != NO_EMPTY_RAR_FOUND) {
		/* stick it into first empty RAR slot we found */
		rar = first_empty_rar;
		ixgbe_set_rar(hw, rar, addr, vmdq, IXGBE_RAH_AV);
	} else if (rar == hw->mac.rar_highwater) {
		/* add it to the top of the list and inc the highwater mark */
		ixgbe_set_rar(hw, rar, addr, vmdq, IXGBE_RAH_AV);
		hw->mac.rar_highwater++;
	} else if (rar >= hw->mac.num_rar_entries) {
		return IXGBE_ERR_INVALID_MAC_ADDR;
	}

	/*
	 * If we found rar[0], make sure the default pool bit (we use pool 0)
	 * remains cleared to be sure default pool packets will get delivered
	 */
	if (rar == 0)
		ixgbe_clear_vmdq(hw, rar, 0);

	return rar;
}

/**
 *  ixgbe_clear_vmdq_generic - Disassociate a VMDq pool index from a rx address
 *  @hw: pointer to hardware struct
 *  @rar: receive address register index to disassociate
 *  @vmdq: VMDq pool index to remove from the rar
 **/
int32_t ixgbe_clear_vmdq_generic(struct ixgbe_hw *hw, uint32_t rar, uint32_t vmdq)
{
	uint32_t mpsar_lo, mpsar_hi;
	uint32_t rar_entries = hw->mac.num_rar_entries;

	DEBUGFUNC("ixgbe_clear_vmdq_generic");

	/* Make sure we are using a valid rar index range */
	if (rar >= rar_entries) {
		ERROR_REPORT2(IXGBE_ERROR_ARGUMENT,
			     "RAR index %d is out of range.\n", rar);
		return IXGBE_ERR_INVALID_ARGUMENT;
	}

	mpsar_lo = IXGBE_READ_REG(hw, IXGBE_MPSAR_LO(rar));
	mpsar_hi = IXGBE_READ_REG(hw, IXGBE_MPSAR_HI(rar));

	if (IXGBE_REMOVED(hw->hw_addr))
		goto done;

	if (!mpsar_lo && !mpsar_hi)
		goto done;

	if (vmdq == IXGBE_CLEAR_VMDQ_ALL) {
		if (mpsar_lo) {
			IXGBE_WRITE_REG(hw, IXGBE_MPSAR_LO(rar), 0);
			mpsar_lo = 0;
		}
		if (mpsar_hi) {
			IXGBE_WRITE_REG(hw, IXGBE_MPSAR_HI(rar), 0);
			mpsar_hi = 0;
		}
	} else if (vmdq < 32) {
		mpsar_lo &= ~(1 << vmdq);
		IXGBE_WRITE_REG(hw, IXGBE_MPSAR_LO(rar), mpsar_lo);
	} else {
		mpsar_hi &= ~(1 << (vmdq - 32));
		IXGBE_WRITE_REG(hw, IXGBE_MPSAR_HI(rar), mpsar_hi);
	}

	/* was that the last pool using this rar? */
	if (mpsar_lo == 0 && mpsar_hi == 0 && rar != 0)
		hw->mac.ops.clear_rar(hw, rar);
done:
	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_set_vmdq_generic - Associate a VMDq pool index with a rx address
 *  @hw: pointer to hardware struct
 *  @rar: receive address register index to associate with a VMDq index
 *  @vmdq: VMDq pool index
 **/
int32_t ixgbe_set_vmdq_generic(struct ixgbe_hw *hw, uint32_t rar, uint32_t vmdq)
{
	uint32_t mpsar;
	uint32_t rar_entries = hw->mac.num_rar_entries;

	DEBUGFUNC("ixgbe_set_vmdq_generic");

	/* Make sure we are using a valid rar index range */
	if (rar >= rar_entries) {
		ERROR_REPORT2(IXGBE_ERROR_ARGUMENT,
			     "RAR index %d is out of range.\n", rar);
		return IXGBE_ERR_INVALID_ARGUMENT;
	}

	if (vmdq < 32) {
		mpsar = IXGBE_READ_REG(hw, IXGBE_MPSAR_LO(rar));
		mpsar |= 1 << vmdq;
		IXGBE_WRITE_REG(hw, IXGBE_MPSAR_LO(rar), mpsar);
	} else {
		mpsar = IXGBE_READ_REG(hw, IXGBE_MPSAR_HI(rar));
		mpsar |= 1 << (vmdq - 32);
		IXGBE_WRITE_REG(hw, IXGBE_MPSAR_HI(rar), mpsar);
	}
	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_init_uta_tables_generic - Initialize the Unicast Table Array
 *  @hw: pointer to hardware structure
 **/
int32_t ixgbe_init_uta_tables_generic(struct ixgbe_hw *hw)
{
	int i;

	DEBUGFUNC("ixgbe_init_uta_tables_generic");
	DEBUGOUT(" Clearing UTA\n");

	for (i = 0; i < 128; i++)
		IXGBE_WRITE_REG(hw, IXGBE_UTA(i), 0);

	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_find_vlvf_slot - find the vlanid or the first empty slot
 *  @hw: pointer to hardware structure
 *  @vlan: VLAN id to write to VLAN filter
 *  @vlvf_bypass: TRUE to find vlanid only, FALSE returns first empty slot if
 *		  vlanid not found
 *
 *
 *  return the VLVF index where this VLAN id should be placed
 *
 **/
int32_t ixgbe_find_vlvf_slot(struct ixgbe_hw *hw, uint32_t vlan, bool vlvf_bypass)
{
	int32_t regindex, first_empty_slot;
	uint32_t bits;

	/* short cut the special case */
	if (vlan == 0)
		return 0;

	/* if vlvf_bypass is set we don't want to use an empty slot, we
	 * will simply bypass the VLVF if there are no entries present in the
	 * VLVF that contain our VLAN
	 */
	first_empty_slot = vlvf_bypass ? IXGBE_ERR_NO_SPACE : 0;

	/* add VLAN enable bit for comparison */
	vlan |= IXGBE_VLVF_VIEN;

	/* Search for the vlan id in the VLVF entries. Save off the first empty
	 * slot found along the way.
	 *
	 * pre-decrement loop covering (IXGBE_VLVF_ENTRIES - 1) .. 1
	 */
	for (regindex = IXGBE_VLVF_ENTRIES; --regindex;) {
		bits = IXGBE_READ_REG(hw, IXGBE_VLVF(regindex));
		if (bits == vlan)
			return regindex;
		if (!first_empty_slot && !bits)
			first_empty_slot = regindex;
	}

	/* If we are here then we didn't find the VLAN.  Return first empty
	 * slot we found during our search, else error.
	 */
	if (!first_empty_slot)
		ERROR_REPORT1(IXGBE_ERROR_SOFTWARE, "No space in VLVF.\n");

	return first_empty_slot ? first_empty_slot : IXGBE_ERR_NO_SPACE;
}

/**
 *  ixgbe_set_vfta_generic - Set VLAN filter table
 *  @hw: pointer to hardware structure
 *  @vlan: VLAN id to write to VLAN filter
 *  @vind: VMDq output index that maps queue to VLAN id in VLVFB
 *  @vlan_on: boolean flag to turn on/off VLAN
 *  @vlvf_bypass: boolean flag indicating updating default pool is okay
 *
 *  Turn on/off specified VLAN in the VLAN filter table.
 **/
int32_t ixgbe_set_vfta_generic(struct ixgbe_hw *hw, uint32_t vlan, uint32_t vind,
			       bool vlan_on, bool vlvf_bypass)
{
	uint32_t regidx, vfta_delta, vfta;
	int32_t ret_val;

	DEBUGFUNC("ixgbe_set_vfta_generic");

	if (vlan > 4095 || vind > 63)
		return IXGBE_ERR_PARAM;

	/*
	 * this is a 2 part operation - first the VFTA, then the
	 * VLVF and VLVFB if VT Mode is set
	 * We don't write the VFTA until we know the VLVF part succeeded.
	 */

	/* Part 1
	 * The VFTA is a bitstring made up of 128 32-bit registers
	 * that enable the particular VLAN id, much like the MTA:
	 *    bits[11-5]: which register
	 *    bits[4-0]:  which bit in the register
	 */
	regidx = vlan / 32;
	vfta_delta = 1 << (vlan % 32);
	vfta = IXGBE_READ_REG(hw, IXGBE_VFTA(regidx));

	/*
	 * vfta_delta represents the difference between the current value
	 * of vfta and the value we want in the register.  Since the diff
	 * is an XOR mask we can just update the vfta using an XOR
	 */
	vfta_delta &= vlan_on ? ~vfta : vfta;
	vfta ^= vfta_delta;

	/* Part 2
	 * Call ixgbe_set_vlvf_generic to set VLVFB and VLVF
	 */
	ret_val = ixgbe_set_vlvf_generic(hw, vlan, vind, vlan_on, &vfta_delta,
					 vfta, vlvf_bypass);
	if (ret_val != IXGBE_SUCCESS) {
		if (vlvf_bypass)
			goto vfta_update;
		return ret_val;
	}

vfta_update:
	/* Update VFTA now that we are ready for traffic */
	if (vfta_delta)
		IXGBE_WRITE_REG(hw, IXGBE_VFTA(regidx), vfta);

	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_set_vlvf_generic - Set VLAN Pool Filter
 *  @hw: pointer to hardware structure
 *  @vlan: VLAN id to write to VLAN filter
 *  @vind: VMDq output index that maps queue to VLAN id in VLVFB
 *  @vlan_on: boolean flag to turn on/off VLAN in VLVF
 *  @vfta_delta: pointer to the difference between the current value of VFTA
 *		 and the desired value
 *  @vfta: the desired value of the VFTA
 *  @vlvf_bypass: boolean flag indicating updating default pool is okay
 *
 *  Turn on/off specified bit in VLVF table.
 **/
int32_t ixgbe_set_vlvf_generic(struct ixgbe_hw *hw, uint32_t vlan, uint32_t vind,
			       bool vlan_on, uint32_t *vfta_delta, uint32_t vfta,
			       bool vlvf_bypass)
{
	uint32_t bits;
	int32_t vlvf_index;

	DEBUGFUNC("ixgbe_set_vlvf_generic");

	if (vlan > 4095 || vind > 63)
		return IXGBE_ERR_PARAM;

	/* If VT Mode is set
	 *   Either vlan_on
	 *     make sure the vlan is in VLVF
	 *     set the vind bit in the matching VLVFB
	 *   Or !vlan_on
	 *     clear the pool bit and possibly the vind
	 */
	if (!(IXGBE_READ_REG(hw, IXGBE_VT_CTL) & IXGBE_VT_CTL_VT_ENABLE))
		return IXGBE_SUCCESS;

	vlvf_index = ixgbe_find_vlvf_slot(hw, vlan, vlvf_bypass);
	if (vlvf_index < 0)
		return vlvf_index;

	bits = IXGBE_READ_REG(hw, IXGBE_VLVFB(vlvf_index * 2 + vind / 32));

	/* set the pool bit */
	bits |= 1 << (vind % 32);
	if (vlan_on)
		goto vlvf_update;

	/* clear the pool bit */
	bits ^= 1 << (vind % 32);

	if (!bits &&
	    !IXGBE_READ_REG(hw, IXGBE_VLVFB(vlvf_index * 2 + 1 - vind / 32))) {
		/* Clear VFTA first, then disable VLVF.  Otherwise
		 * we run the risk of stray packets leaking into
		 * the PF via the default pool
		 */
		if (*vfta_delta)
			IXGBE_WRITE_REG(hw, IXGBE_VFTA(vlan / 32), vfta);

		/* disable VLVF and clear remaining bit from pool */
		IXGBE_WRITE_REG(hw, IXGBE_VLVF(vlvf_index), 0);
		IXGBE_WRITE_REG(hw, IXGBE_VLVFB(vlvf_index * 2 + vind / 32), 0);

		return IXGBE_SUCCESS;
	}

	/* If there are still bits set in the VLVFB registers
	 * for the VLAN ID indicated we need to see if the
	 * caller is requesting that we clear the VFTA entry bit.
	 * If the caller has requested that we clear the VFTA
	 * entry bit but there are still pools/VFs using this VLAN
	 * ID entry then ignore the request.  We're not worried
	 * about the case where we're turning the VFTA VLAN ID
	 * entry bit on, only when requested to turn it off as
	 * there may be multiple pools and/or VFs using the
	 * VLAN ID entry.  In that case we cannot clear the
	 * VFTA bit until all pools/VFs using that VLAN ID have also
	 * been cleared.  This will be indicated by "bits" being
	 * zero.
	 */
	*vfta_delta = 0;

vlvf_update:
	/* record pool change and enable VLAN ID if not already enabled */
	IXGBE_WRITE_REG(hw, IXGBE_VLVFB(vlvf_index * 2 + vind / 32), bits);
	IXGBE_WRITE_REG(hw, IXGBE_VLVF(vlvf_index), IXGBE_VLVF_VIEN | vlan);

	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_clear_vfta_generic - Clear VLAN filter table
 *  @hw: pointer to hardware structure
 *
 *  Clears the VLAN filer table, and the VMDq index associated with the filter
 **/
int32_t ixgbe_clear_vfta_generic(struct ixgbe_hw *hw)
{
	uint32_t offset;

	DEBUGFUNC("ixgbe_clear_vfta_generic");

	for (offset = 0; offset < hw->mac.vft_size; offset++)
		IXGBE_WRITE_REG(hw, IXGBE_VFTA(offset), 0);

	for (offset = 0; offset < IXGBE_VLVF_ENTRIES; offset++) {
		IXGBE_WRITE_REG(hw, IXGBE_VLVF(offset), 0);
		IXGBE_WRITE_REG(hw, IXGBE_VLVFB(offset * 2), 0);
		IXGBE_WRITE_REG(hw, IXGBE_VLVFB((offset * 2) + 1), 0);
	}

	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_need_crosstalk_fix - Determine if we need to do cross talk fix
 *  @hw: pointer to hardware structure
 *
 *  Contains the logic to identify if we need to verify link for the
 *  crosstalk fix
 **/
bool ixgbe_need_crosstalk_fix(struct ixgbe_hw *hw)
{

	/* Does FW say we need the fix */
	if (!hw->need_crosstalk_fix)
		return FALSE;

	/* Only consider SFP+ PHYs i.e. media type fiber */
	switch (hw->mac.ops.get_media_type(hw)) {
	case ixgbe_media_type_fiber:
	case ixgbe_media_type_fiber_qsfp:
		break;
	default:
		return FALSE;
	}

	return TRUE;
}

/**
 *  ixgbe_check_mac_link_generic - Determine link and speed status
 *  @hw: pointer to hardware structure
 *  @speed: pointer to link speed
 *  @link_up: TRUE when link is up
 *  @link_up_wait_to_complete: bool used to wait for link up or not
 *
 *  Reads the links register to determine if link is up and the current speed
 **/
int32_t ixgbe_check_mac_link_generic(struct ixgbe_hw *hw, ixgbe_link_speed *speed,
				     bool *link_up, bool link_up_wait_to_complete)
{
	uint32_t links_reg, links_orig;
	uint32_t i;

	DEBUGFUNC("ixgbe_check_mac_link_generic");

	/* If Crosstalk fix enabled do the sanity check of making sure
	 * the SFP+ cage is full.
	 */
	if (ixgbe_need_crosstalk_fix(hw)) {
		uint32_t sfp_cage_full;

		switch (hw->mac.type) {
		case ixgbe_mac_82599EB:
			sfp_cage_full = IXGBE_READ_REG(hw, IXGBE_ESDP) &
					IXGBE_ESDP_SDP2;
			break;
		case ixgbe_mac_X550EM_x:
		case ixgbe_mac_X550EM_a:
			sfp_cage_full = IXGBE_READ_REG(hw, IXGBE_ESDP) &
					IXGBE_ESDP_SDP0;
			break;
		default:
			/* sanity check - No SFP+ devices here */
			sfp_cage_full = FALSE;
			break;
		}

		if (!sfp_cage_full) {
			*link_up = FALSE;
			*speed = IXGBE_LINK_SPEED_UNKNOWN;
			return IXGBE_SUCCESS;
		}
	}

	/* clear the old state */
	links_orig = IXGBE_READ_REG(hw, IXGBE_LINKS);

	links_reg = IXGBE_READ_REG(hw, IXGBE_LINKS);

	if (links_orig != links_reg) {
		DEBUGOUT2("LINKS changed from %08X to %08X\n",
			  links_orig, links_reg);
	}

	if (link_up_wait_to_complete) {
		for (i = 0; i < hw->mac.max_link_up_time; i++) {
			if (links_reg & IXGBE_LINKS_UP) {
				*link_up = TRUE;
				break;
			} else {
				*link_up = FALSE;
			}
			msec_delay(100);
			links_reg = IXGBE_READ_REG(hw, IXGBE_LINKS);
		}
	} else {
		if (links_reg & IXGBE_LINKS_UP)
			*link_up = TRUE;
		else
			*link_up = FALSE;
	}

	switch (links_reg & IXGBE_LINKS_SPEED_82599) {
	case IXGBE_LINKS_SPEED_10G_82599:
		*speed = IXGBE_LINK_SPEED_10GB_FULL;
		if (hw->mac.type >= ixgbe_mac_X550) {
			if (links_reg & IXGBE_LINKS_SPEED_NON_STD)
				*speed = IXGBE_LINK_SPEED_2_5GB_FULL;
		}
		break;
	case IXGBE_LINKS_SPEED_1G_82599:
		*speed = IXGBE_LINK_SPEED_1GB_FULL;
		break;
	case IXGBE_LINKS_SPEED_100_82599:
		*speed = IXGBE_LINK_SPEED_100_FULL;
		if (hw->mac.type == ixgbe_mac_X550) {
			if (links_reg & IXGBE_LINKS_SPEED_NON_STD)
				*speed = IXGBE_LINK_SPEED_5GB_FULL;
		}
		break;
	case IXGBE_LINKS_SPEED_10_X550EM_A:
		*speed = IXGBE_LINK_SPEED_UNKNOWN;
		if (hw->device_id == IXGBE_DEV_ID_X550EM_A_1G_T ||
		    hw->device_id == IXGBE_DEV_ID_X550EM_A_1G_T_L)
			*speed = IXGBE_LINK_SPEED_10_FULL;
		break;
	default:
		*speed = IXGBE_LINK_SPEED_UNKNOWN;
	}

	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_get_device_caps_generic - Get additional device capabilities
 *  @hw: pointer to hardware structure
 *  @device_caps: the EEPROM word with the extra device capabilities
 *
 *  This function will read the EEPROM location for the device capabilities,
 *  and return the word through device_caps.
 **/
int32_t ixgbe_get_device_caps_generic(struct ixgbe_hw *hw, uint16_t *device_caps)
{
	DEBUGFUNC("ixgbe_get_device_caps_generic");

	hw->eeprom.ops.read(hw, IXGBE_DEVICE_CAPS, device_caps);

	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_calculate_checksum - Calculate checksum for buffer
 *  @buffer: pointer to EEPROM
 *  @length: size of EEPROM to calculate a checksum for
 *  Calculates the checksum for some buffer on a specified length.  The
 *  checksum calculated is returned.
 **/
uint8_t ixgbe_calculate_checksum(uint8_t *buffer, uint32_t length)
{
	uint32_t i;
	uint8_t sum = 0;

	DEBUGFUNC("ixgbe_calculate_checksum");

	if (!buffer)
		return 0;

	for (i = 0; i < length; i++)
		sum += buffer[i];

	return (uint8_t) (0 - sum);
}

/**
 *  ixgbe_hic_unlocked - Issue command to manageability block unlocked
 *  @hw: pointer to the HW structure
 *  @buffer: command to write and where the return status will be placed
 *  @length: length of buffer, must be multiple of 4 bytes
 *  @timeout: time in ms to wait for command completion
 *
 *  Communicates with the manageability block. On success return IXGBE_SUCCESS
 *  else returns semaphore error when encountering an error acquiring
 *  semaphore or IXGBE_ERR_HOST_INTERFACE_COMMAND when command fails.
 *
 *  This function assumes that the IXGBE_GSSR_SW_MNG_SM semaphore is held
 *  by the caller.
 **/
int32_t ixgbe_hic_unlocked(struct ixgbe_hw *hw, uint32_t *buffer, uint32_t length,
		       uint32_t timeout)
{
	uint32_t hicr, i, fwsts;
	uint16_t dword_len;

	DEBUGFUNC("ixgbe_hic_unlocked");

	if (!length || length > IXGBE_HI_MAX_BLOCK_BYTE_LENGTH) {
		DEBUGOUT1("Buffer length failure buffersize=%d.\n", length);
		return IXGBE_ERR_HOST_INTERFACE_COMMAND;
	}

	/* Set bit 9 of FWSTS clearing FW reset indication */
	fwsts = IXGBE_READ_REG(hw, IXGBE_FWSTS);
	IXGBE_WRITE_REG(hw, IXGBE_FWSTS, fwsts | IXGBE_FWSTS_FWRI);

	/* Check that the host interface is enabled. */
	hicr = IXGBE_READ_REG(hw, IXGBE_HICR);
	if (!(hicr & IXGBE_HICR_EN)) {
		DEBUGOUT("IXGBE_HOST_EN bit disabled.\n");
		return IXGBE_ERR_HOST_INTERFACE_COMMAND;
	}

	/* Calculate length in DWORDs. We must be DWORD aligned */
	if (length % sizeof(uint32_t)) {
		DEBUGOUT("Buffer length failure, not aligned to dword");
		return IXGBE_ERR_INVALID_ARGUMENT;
	}

	dword_len = length >> 2;

	/* The device driver writes the relevant command block
	 * into the ram area.
	 */
	for (i = 0; i < dword_len; i++)
		IXGBE_WRITE_REG_ARRAY(hw, IXGBE_FLEX_MNG,
				      i, htole32(buffer[i]));

	/* Setting this bit tells the ARC that a new command is pending. */
	IXGBE_WRITE_REG(hw, IXGBE_HICR, hicr | IXGBE_HICR_C);

	for (i = 0; i < timeout; i++) {
		hicr = IXGBE_READ_REG(hw, IXGBE_HICR);
		if (!(hicr & IXGBE_HICR_C))
			break;
		msec_delay(1);
	}

	/* Check command completion */
	if ((timeout && i == timeout) ||
	    !(IXGBE_READ_REG(hw, IXGBE_HICR) & IXGBE_HICR_SV)) {
		ERROR_REPORT1(IXGBE_ERROR_CAUTION,
			     "Command has failed with no status valid.\n");
		return IXGBE_ERR_HOST_INTERFACE_COMMAND;
	}

	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_host_interface_command - Issue command to manageability block
 *  @hw: pointer to the HW structure
 *  @buffer: contains the command to write and where the return status will
 *   be placed
 *  @length: length of buffer, must be multiple of 4 bytes
 *  @timeout: time in ms to wait for command completion
 *  @return_data: read and return data from the buffer (TRUE) or not (FALSE)
 *   Needed because FW structures are big endian and decoding of
 *   these fields can be 8 bit or 16 bit based on command. Decoding
 *   is not easily understood without making a table of commands.
 *   So we will leave this up to the caller to read back the data
 *   in these cases.
 *
 *  Communicates with the manageability block. On success return IXGBE_SUCCESS
 *  else returns semaphore error when encountering an error acquiring
 *  semaphore or IXGBE_ERR_HOST_INTERFACE_COMMAND when command fails.
 **/
int32_t ixgbe_host_interface_command(struct ixgbe_hw *hw, uint32_t *buffer,
				 uint32_t length, uint32_t timeout, bool return_data)
{
	uint32_t hdr_size = sizeof(struct ixgbe_hic_hdr);
	struct ixgbe_hic_hdr *resp = (struct ixgbe_hic_hdr *)buffer;
	uint16_t buf_len;
	int32_t status;
	uint32_t bi;
	uint32_t dword_len;

	DEBUGFUNC("ixgbe_host_interface_command");

	if (length == 0 || length > IXGBE_HI_MAX_BLOCK_BYTE_LENGTH) {
		DEBUGOUT1("Buffer length failure buffersize=%d.\n", length);
		return IXGBE_ERR_HOST_INTERFACE_COMMAND;
	}

	/* Take management host interface semaphore */
	status = hw->mac.ops.acquire_swfw_sync(hw, IXGBE_GSSR_SW_MNG_SM);
	if (status)
		return status;

	status = ixgbe_hic_unlocked(hw, buffer, length, timeout);
	if (status)
		goto rel_out;

	if (!return_data)
		goto rel_out;

	/* Calculate length in DWORDs */
	dword_len = hdr_size >> 2;

	/* first pull in the header so we know the buffer length */
	for (bi = 0; bi < dword_len; bi++) {
		buffer[bi] = letoh32(IXGBE_READ_REG_ARRAY(hw,
		    IXGBE_FLEX_MNG, bi));
	}

	/*
	 * If there is any thing in data position pull it in
	 * Read Flash command requires reading buffer length from
	 * two byes instead of one byte
	 */
	if (resp->cmd == 0x30) {
		for (; bi < dword_len + 2; bi++) {
			buffer[bi] = letoh32(IXGBE_READ_REG_ARRAY(hw,
			    IXGBE_FLEX_MNG, bi));
		}
		buf_len = (((uint16_t)(resp->cmd_or_resp.ret_status) << 3)
				  & 0xF00) | resp->buf_len;
		hdr_size += (2 << 2);
	} else {
		buf_len = resp->buf_len;
	}
	if (!buf_len)
		goto rel_out;

	if (length < buf_len + hdr_size) {
		DEBUGOUT("Buffer not large enough for reply message.\n");
		status = IXGBE_ERR_HOST_INTERFACE_COMMAND;
		goto rel_out;
	}

	/* Calculate length in DWORDs, add 3 for odd lengths */
	dword_len = (buf_len + 3) >> 2;

	/* Pull in the rest of the buffer (bi is where we left off) */
	for (; bi <= dword_len; bi++) {
		buffer[bi] = letoh32(IXGBE_READ_REG_ARRAY(hw,
		    IXGBE_FLEX_MNG, bi));
	}

rel_out:
	hw->mac.ops.release_swfw_sync(hw, IXGBE_GSSR_SW_MNG_SM);

	return status;
}

/**
 * ixgbe_clear_tx_pending - Clear pending TX work from the PCIe fifo
 * @hw: pointer to the hardware structure
 *
 * The 82599 and x540 MACs can experience issues if TX work is still pending
 * when a reset occurs.  This function prevents this by flushing the PCIe
 * buffers on the system.
 **/
void ixgbe_clear_tx_pending(struct ixgbe_hw *hw)
{
	uint32_t gcr_ext, hlreg0, i, poll;
	uint16_t value;

	/*
	 * If double reset is not requested then all transactions should
	 * already be clear and as such there is no work to do
	 */
	if (!(hw->mac.flags & IXGBE_FLAGS_DOUBLE_RESET_REQUIRED))
		return;

	/*
	 * Set loopback enable to prevent any transmits from being sent
	 * should the link come up.  This assumes that the RXCTRL.RXEN bit
	 * has already been cleared.
	 */
	hlreg0 = IXGBE_READ_REG(hw, IXGBE_HLREG0);
	IXGBE_WRITE_REG(hw, IXGBE_HLREG0, hlreg0 | IXGBE_HLREG0_LPBK);

	/* Wait for a last completion before clearing buffers */
	IXGBE_WRITE_FLUSH(hw);
	msec_delay(3);

	/*
	 * Before proceeding, make sure that the PCIe block does not have
	 * transactions pending.
	 */
	poll = ixgbe_pcie_timeout_poll(hw);
	for (i = 0; i < poll; i++) {
		usec_delay(100);
		value = IXGBE_READ_PCIE_WORD(hw, IXGBE_PCI_DEVICE_STATUS);
		if (IXGBE_REMOVED(hw->hw_addr))
			goto out;
		if (!(value & IXGBE_PCI_DEVICE_STATUS_TRANSACTION_PENDING))
			goto out;
	}

out:
	/* initiate cleaning flow for buffers in the PCIe transaction layer */
	gcr_ext = IXGBE_READ_REG(hw, IXGBE_GCR_EXT);
	IXGBE_WRITE_REG(hw, IXGBE_GCR_EXT,
			gcr_ext | IXGBE_GCR_EXT_BUFFERS_CLEAR);

	/* Flush all writes and allow 20usec for all transactions to clear */
	IXGBE_WRITE_FLUSH(hw);
	usec_delay(20);

	/* restore previous register values */
	IXGBE_WRITE_REG(hw, IXGBE_GCR_EXT, gcr_ext);
	IXGBE_WRITE_REG(hw, IXGBE_HLREG0, hlreg0);
}

void ixgbe_disable_rx_generic(struct ixgbe_hw *hw)
{
	uint32_t pfdtxgswc;
	uint32_t rxctrl;

	rxctrl = IXGBE_READ_REG(hw, IXGBE_RXCTRL);
	if (rxctrl & IXGBE_RXCTRL_RXEN) {
		if (hw->mac.type != ixgbe_mac_82598EB) {
			pfdtxgswc = IXGBE_READ_REG(hw, IXGBE_PFDTXGSWC);
			if (pfdtxgswc & IXGBE_PFDTXGSWC_VT_LBEN) {
				pfdtxgswc &= ~IXGBE_PFDTXGSWC_VT_LBEN;
				IXGBE_WRITE_REG(hw, IXGBE_PFDTXGSWC, pfdtxgswc);
				hw->mac.set_lben = TRUE;
			} else {
				hw->mac.set_lben = FALSE;
			}
		}
		rxctrl &= ~IXGBE_RXCTRL_RXEN;
		IXGBE_WRITE_REG(hw, IXGBE_RXCTRL, rxctrl);
	}
}

void ixgbe_enable_rx_generic(struct ixgbe_hw *hw)
{
	uint32_t pfdtxgswc;
	uint32_t rxctrl;

	rxctrl = IXGBE_READ_REG(hw, IXGBE_RXCTRL);
	IXGBE_WRITE_REG(hw, IXGBE_RXCTRL, (rxctrl | IXGBE_RXCTRL_RXEN));

	if (hw->mac.type != ixgbe_mac_82598EB) {
		if (hw->mac.set_lben) {
			pfdtxgswc = IXGBE_READ_REG(hw, IXGBE_PFDTXGSWC);
			pfdtxgswc |= IXGBE_PFDTXGSWC_VT_LBEN;
			IXGBE_WRITE_REG(hw, IXGBE_PFDTXGSWC, pfdtxgswc);
			hw->mac.set_lben = FALSE;
		}
	}
}

/**
 * ixgbe_mng_present - returns TRUE when management capability is present
 * @hw: pointer to hardware structure
 */
bool ixgbe_mng_present(struct ixgbe_hw *hw)
{
	uint32_t fwsm;

	if (hw->mac.type < ixgbe_mac_82599EB)
		return FALSE;

	fwsm = IXGBE_READ_REG(hw, IXGBE_FWSM_BY_MAC(hw));

	return !!(fwsm & IXGBE_FWSM_FW_MODE_PT);
}

/**
 * ixgbe_mng_enabled - Is the manageability engine enabled?
 * @hw: pointer to hardware structure
 *
 * Returns TRUE if the manageability engine is enabled.
 **/
bool ixgbe_mng_enabled(struct ixgbe_hw *hw)
{
	uint32_t fwsm, manc, factps;

	fwsm = IXGBE_READ_REG(hw, IXGBE_FWSM_BY_MAC(hw));
	if ((fwsm & IXGBE_FWSM_MODE_MASK) != IXGBE_FWSM_FW_MODE_PT)
		return FALSE;

	manc = IXGBE_READ_REG(hw, IXGBE_MANC);
	if (!(manc & IXGBE_MANC_RCV_TCO_EN))
		return FALSE;

	if (hw->mac.type <= ixgbe_mac_X540) {
		factps = IXGBE_READ_REG(hw, IXGBE_FACTPS_BY_MAC(hw));
		if (factps & IXGBE_FACTPS_MNGCG)
			return FALSE;
	}

	return TRUE;
}

/**
 *  ixgbe_setup_mac_link_multispeed_fiber - Set MAC link speed
 *  @hw: pointer to hardware structure
 *  @speed: new link speed
 *  @autoneg_wait_to_complete: TRUE when waiting for completion is needed
 *
 *  Set the link speed in the MAC and/or PHY register and restarts link.
 **/
int32_t ixgbe_setup_mac_link_multispeed_fiber(struct ixgbe_hw *hw,
					      ixgbe_link_speed speed,
					      bool autoneg_wait_to_complete)
{
	ixgbe_link_speed link_speed = IXGBE_LINK_SPEED_UNKNOWN;
	ixgbe_link_speed highest_link_speed = IXGBE_LINK_SPEED_UNKNOWN;
	int32_t status = IXGBE_SUCCESS;
	uint32_t speedcnt = 0;
	uint32_t i = 0;
	bool autoneg, link_up = FALSE;

	DEBUGFUNC("ixgbe_setup_mac_link_multispeed_fiber");

	/* Mask off requested but non-supported speeds */
	status = hw->mac.ops.get_link_capabilities(hw, &link_speed, &autoneg);
	if (status != IXGBE_SUCCESS)
		return status;

	speed &= link_speed;

	/* Try each speed one by one, highest priority first.  We do this in
	 * software because 10Gb fiber doesn't support speed autonegotiation.
	 */
	if (speed & IXGBE_LINK_SPEED_10GB_FULL) {
		speedcnt++;
		highest_link_speed = IXGBE_LINK_SPEED_10GB_FULL;

		/* Set the module link speed */
		switch (hw->phy.media_type) {
		case ixgbe_media_type_fiber_fixed:
		case ixgbe_media_type_fiber:
			if (hw->mac.ops.set_rate_select_speed)
				hw->mac.ops.set_rate_select_speed(hw,
				    IXGBE_LINK_SPEED_10GB_FULL);
			break;
		case ixgbe_media_type_fiber_qsfp:
			/* QSFP module automatically detects MAC link speed */
			break;
		default:
			DEBUGOUT("Unexpected media type.\n");
			break;
		}

		/* Allow module to change analog characteristics (1G->10G) */
		msec_delay(40);

		if (!hw->mac.ops.setup_mac_link)
			return IXGBE_NOT_IMPLEMENTED;
		status = hw->mac.ops.setup_mac_link(hw,
						    IXGBE_LINK_SPEED_10GB_FULL,
						    autoneg_wait_to_complete);
		if (status != IXGBE_SUCCESS)
			return status;

		/* Flap the Tx laser if it has not already been done */
		ixgbe_flap_tx_laser(hw);

		/* Wait for the controller to acquire link.  Per IEEE 802.3ap,
		 * Section 73.10.2, we may have to wait up to 500ms if KR is
		 * attempted.  82599 uses the same timing for 10g SFI.
		 */
		for (i = 0; i < 5; i++) {
			/* Wait for the link partner to also set speed */
			msec_delay(100);

			/* If we have link, just jump out */
			status = ixgbe_check_link(hw, &link_speed,
						  &link_up, FALSE);
			if (status != IXGBE_SUCCESS)
				return status;

			if (link_up)
				goto out;
		}
	}

	if (speed & IXGBE_LINK_SPEED_1GB_FULL) {
		speedcnt++;
		if (highest_link_speed == IXGBE_LINK_SPEED_UNKNOWN)
			highest_link_speed = IXGBE_LINK_SPEED_1GB_FULL;

		/* Set the module link speed */
		switch (hw->phy.media_type) {
		case ixgbe_media_type_fiber_fixed:
		case ixgbe_media_type_fiber:
			if (hw->mac.ops.set_rate_select_speed)
				hw->mac.ops.set_rate_select_speed(hw,
				    IXGBE_LINK_SPEED_1GB_FULL);
			break;
		case ixgbe_media_type_fiber_qsfp:
			/* QSFP module automatically detects link speed */
			break;
		default:
			DEBUGOUT("Unexpected media type.\n");
			break;
		}

		/* Allow module to change analog characteristics (10G->1G) */
		msec_delay(40);

		if (!hw->mac.ops.setup_mac_link)
			return IXGBE_NOT_IMPLEMENTED;
		status = hw->mac.ops.setup_mac_link(hw,
						    IXGBE_LINK_SPEED_1GB_FULL,
						    autoneg_wait_to_complete);
		if (status != IXGBE_SUCCESS)
			return status;

		/* Flap the Tx laser if it has not already been done */
		ixgbe_flap_tx_laser(hw);

		/* Wait for the link partner to also set speed */
		msec_delay(100);

		/* If we have link, just jump out */
		status = ixgbe_check_link(hw, &link_speed, &link_up, FALSE);
		if (status != IXGBE_SUCCESS)
			return status;

		if (link_up)
			goto out;
	}

	/* We didn't get link.  Configure back to the highest speed we tried,
	 * (if there was more than one).  We call ourselves back with just the
	 * single highest speed that the user requested.
	 */
	if (speedcnt > 1)
		status = ixgbe_setup_mac_link_multispeed_fiber(hw,
						      highest_link_speed,
						      autoneg_wait_to_complete);

out:
	/* Set autoneg_advertised value based on input link speed */
	hw->phy.autoneg_advertised = 0;

	if (speed & IXGBE_LINK_SPEED_10GB_FULL)
		hw->phy.autoneg_advertised |= IXGBE_LINK_SPEED_10GB_FULL;

	if (speed & IXGBE_LINK_SPEED_1GB_FULL)
		hw->phy.autoneg_advertised |= IXGBE_LINK_SPEED_1GB_FULL;

	return status;
}

/**
 *  ixgbe_set_soft_rate_select_speed - Set module link speed
 *  @hw: pointer to hardware structure
 *  @speed: link speed to set
 *
 *  Set module link speed via the soft rate select.
 */
void ixgbe_set_soft_rate_select_speed(struct ixgbe_hw *hw,
					ixgbe_link_speed speed)
{
	int32_t status;
	uint8_t rs, eeprom_data;

	switch (speed) {
	case IXGBE_LINK_SPEED_10GB_FULL:
		/* one bit mask same as setting on */
		rs = IXGBE_SFF_SOFT_RS_SELECT_10G;
		break;
	case IXGBE_LINK_SPEED_1GB_FULL:
		rs = IXGBE_SFF_SOFT_RS_SELECT_1G;
		break;
	default:
		DEBUGOUT("Invalid fixed module speed\n");
		return;
	}

	/* Set RS0 */
	status = hw->phy.ops.read_i2c_byte(hw, IXGBE_SFF_SFF_8472_OSCB,
					   IXGBE_I2C_EEPROM_DEV_ADDR2,
					   &eeprom_data);
	if (status) {
		DEBUGOUT("Failed to read Rx Rate Select RS0\n");
		goto out;
	}

	eeprom_data = (eeprom_data & ~IXGBE_SFF_SOFT_RS_SELECT_MASK) | rs;

	status = hw->phy.ops.write_i2c_byte(hw, IXGBE_SFF_SFF_8472_OSCB,
					    IXGBE_I2C_EEPROM_DEV_ADDR2,
					    eeprom_data);
	if (status) {
		DEBUGOUT("Failed to write Rx Rate Select RS0\n");
		goto out;
	}

	/* Set RS1 */
	status = hw->phy.ops.read_i2c_byte(hw, IXGBE_SFF_SFF_8472_ESCB,
					   IXGBE_I2C_EEPROM_DEV_ADDR2,
					   &eeprom_data);
	if (status) {
		DEBUGOUT("Failed to read Rx Rate Select RS1\n");
		goto out;
	}

	eeprom_data = (eeprom_data & ~IXGBE_SFF_SOFT_RS_SELECT_MASK) | rs;

	status = hw->phy.ops.write_i2c_byte(hw, IXGBE_SFF_SFF_8472_ESCB,
					    IXGBE_I2C_EEPROM_DEV_ADDR2,
					    eeprom_data);
	if (status) {
		DEBUGOUT("Failed to write Rx Rate Select RS1\n");
		goto out;
	}
out:
	return;
}

/* MAC Operations */

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
int32_t ixgbe_init_shared_code(struct ixgbe_hw *hw)
{
	int32_t status;

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
int32_t ixgbe_set_mac_type(struct ixgbe_hw *hw)
{
	int32_t ret_val = IXGBE_SUCCESS;

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
	case IXGBE_DEV_ID_82598AT_DUAL_PORT:
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
int32_t ixgbe_init_hw(struct ixgbe_hw *hw)
{
	if (hw->mac.ops.init_hw)
		return hw->mac.ops.init_hw(hw);
	else
		return IXGBE_NOT_IMPLEMENTED;
}

/**
 *  ixgbe_get_media_type - Get media type
 *  @hw: pointer to hardware structure
 *
 *  Returns the media type (fiber, copper, backplane)
 **/
enum ixgbe_media_type ixgbe_get_media_type(struct ixgbe_hw *hw)
{
	if (hw->mac.ops.get_media_type)
		return hw->mac.ops.get_media_type(hw);
	else
		return ixgbe_media_type_unknown;
}

/**
 *  ixgbe_identify_phy - Get PHY type
 *  @hw: pointer to hardware structure
 *
 *  Determines the physical layer module found on the current adapter.
 **/
int32_t ixgbe_identify_phy(struct ixgbe_hw *hw)
{
	int32_t status = IXGBE_SUCCESS;

	if (hw->phy.type == ixgbe_phy_unknown) {
		if (hw->phy.ops.identify)
			status = hw->phy.ops.identify(hw);
		else
			status = IXGBE_NOT_IMPLEMENTED;
	}

	return status;
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
int32_t ixgbe_check_link(struct ixgbe_hw *hw, ixgbe_link_speed *speed,
			 bool *link_up, bool link_up_wait_to_complete)
{
	if (hw->mac.ops.check_link)
		return hw->mac.ops.check_link(hw, speed, link_up,
					      link_up_wait_to_complete);
	else
		return IXGBE_NOT_IMPLEMENTED;
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
 *  ixgbe_set_rar - Set Rx address register
 *  @hw: pointer to hardware structure
 *  @index: Receive address register to write
 *  @addr: Address to put into receive address register
 *  @vmdq: VMDq "set"
 *  @enable_addr: set flag that address is active
 *
 *  Puts an ethernet address into a receive address register.
 **/
int32_t ixgbe_set_rar(struct ixgbe_hw *hw, uint32_t index, uint8_t *addr,
		      uint32_t vmdq, uint32_t enable_addr)
{
	if (hw->mac.ops.set_rar)
		return hw->mac.ops.set_rar(hw, index, addr, vmdq, enable_addr);
	else
		return IXGBE_NOT_IMPLEMENTED;
}

/**
 *  ixgbe_set_vmdq - Associate a VMDq index with a receive address
 *  @hw: pointer to hardware structure
 *  @rar: receive address register index to associate with VMDq index
 *  @vmdq: VMDq set or pool index
 **/
int32_t ixgbe_set_vmdq(struct ixgbe_hw *hw, uint32_t rar, uint32_t vmdq)
{
	if (hw->mac.ops.set_vmdq)
		return hw->mac.ops.set_vmdq(hw, rar, vmdq);
	else
		return IXGBE_NOT_IMPLEMENTED;
}

/**
 *  ixgbe_clear_vmdq - Disassociate a VMDq index from a receive address
 *  @hw: pointer to hardware structure
 *  @rar: receive address register index to disassociate with VMDq index
 *  @vmdq: VMDq set or pool index
 **/
int32_t ixgbe_clear_vmdq(struct ixgbe_hw *hw, uint32_t rar, uint32_t vmdq)
{
	if (hw->mac.ops.clear_vmdq)
		return hw->mac.ops.clear_vmdq(hw, rar, vmdq);
	else
		return IXGBE_NOT_IMPLEMENTED;
}

/**
 *  ixgbe_init_uta_tables - Initializes Unicast Table Arrays.
 *  @hw: pointer to hardware structure
 *
 *  Initializes the Unicast Table Arrays to zero on device load.  This
 *  is part of the Rx init addr execution path.
 **/
int32_t ixgbe_init_uta_tables(struct ixgbe_hw *hw)
{
	if (hw->mac.ops.init_uta_tables)
		return hw->mac.ops.init_uta_tables(hw);
	else
		return IXGBE_NOT_IMPLEMENTED;
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

/*
 * MBX: Mailbox handling
 */

/**
 *  ixgbe_read_mbx - Reads a message from the mailbox
 *  @hw: pointer to the HW structure
 *  @msg: The message buffer
 *  @size: Length of buffer
 *  @mbx_id: id of mailbox to read
 *
 *  returns SUCCESS if it successfully read message from buffer
 **/
int32_t ixgbe_read_mbx(struct ixgbe_hw *hw, uint32_t *msg, uint16_t size, uint16_t mbx_id)
{
	struct ixgbe_mbx_info *mbx = &hw->mbx;

	DEBUGFUNC("ixgbe_read_mbx");

	/* limit read to size of mailbox */
	if (size > mbx->size)
		size = mbx->size;

	if (mbx->ops.read)
		return mbx->ops.read(hw, msg, size, mbx_id);

	return IXGBE_ERR_CONFIG;
}

/**
 * ixgbe_poll_mbx - Wait for message and read it from the mailbox
 * @hw: pointer to the HW structure
 * @msg: The message buffer
 * @size: Length of buffer
 * @mbx_id: id of mailbox to read
 *
 * returns SUCCESS if it successfully read message from buffer
 **/
int32_t ixgbe_poll_mbx(struct ixgbe_hw *hw, uint32_t *msg, uint16_t size,
		       uint16_t mbx_id)
{
	struct ixgbe_mbx_info *mbx = &hw->mbx;
	int32_t ret_val;

	DEBUGFUNC("ixgbe_poll_mbx");

	if (!mbx->ops.read || !mbx->ops.check_for_msg ||
	    !mbx->timeout)
		return IXGBE_ERR_CONFIG;

	/* limit read to size of mailbox */
	if (size > mbx->size)
		size = mbx->size;

	ret_val = ixgbe_poll_for_msg(hw, mbx_id);
	/* if ack received read message, otherwise we timed out */
	if (!ret_val)
		return mbx->ops.read(hw, msg, size, mbx_id);

	return ret_val;
}

/**
 *  ixgbe_write_mbx - Write a message to the mailbox
 *  @hw: pointer to the HW structure
 *  @msg: The message buffer
 *  @size: Length of buffer
 *  @mbx_id: id of mailbox to write
 *
 *  returns SUCCESS if it successfully copied message into the buffer
 **/
int32_t ixgbe_write_mbx(struct ixgbe_hw *hw, uint32_t *msg, uint16_t size, uint16_t mbx_id)
{
	struct ixgbe_mbx_info *mbx = &hw->mbx;
	int32_t ret_val = IXGBE_ERR_MBX;

	DEBUGFUNC("ixgbe_write_mbx");

	/*
	 * exit if either we can't write, release
	 * or there is no timeout defined
	 */
	if (!mbx->ops.write || !mbx->ops.check_for_ack ||
	    !mbx->ops.release || !mbx->timeout)
		return IXGBE_ERR_CONFIG;

	if (size > mbx->size) {
		ret_val = IXGBE_ERR_PARAM;
		ERROR_REPORT2(IXGBE_ERROR_ARGUMENT,
			     "Invalid mailbox message size %u", size);
	} else {
		ret_val = mbx->ops.write(hw, msg, size, mbx_id);
	}

	return ret_val;
}

/**
 *  ixgbe_check_for_msg - checks to see if someone sent us mail
 *  @hw: pointer to the HW structure
 *  @mbx_id: id of mailbox to check
 *
 *  returns SUCCESS if the Status bit was found or else ERR_MBX
 **/
int32_t ixgbe_check_for_msg(struct ixgbe_hw *hw, uint16_t mbx_id)
{
	struct ixgbe_mbx_info *mbx = &hw->mbx;
	int32_t ret_val = IXGBE_ERR_MBX;

	DEBUGFUNC("ixgbe_check_for_msg");

	if (mbx->ops.check_for_msg)
		ret_val = mbx->ops.check_for_msg(hw, mbx_id);

	return ret_val;
}

/**
 *  ixgbe_check_for_ack - checks to see if someone sent us ACK
 *  @hw: pointer to the HW structure
 *  @mbx_id: id of mailbox to check
 *
 *  returns SUCCESS if the Status bit was found or else ERR_MBX
 **/
int32_t ixgbe_check_for_ack(struct ixgbe_hw *hw, uint16_t mbx_id)
{
	struct ixgbe_mbx_info *mbx = &hw->mbx;
	int32_t ret_val = IXGBE_ERR_MBX;

	DEBUGFUNC("ixgbe_check_for_ack");

	if (mbx->ops.check_for_ack)
		ret_val = mbx->ops.check_for_ack(hw, mbx_id);

	return ret_val;
}

/**
 *  ixgbe_check_for_rst - checks to see if other side has reset
 *  @hw: pointer to the HW structure
 *  @mbx_id: id of mailbox to check
 *
 *  returns SUCCESS if the Status bit was found or else ERR_MBX
 **/
int32_t ixgbe_check_for_rst(struct ixgbe_hw *hw, uint16_t mbx_id)
{
	struct ixgbe_mbx_info *mbx = &hw->mbx;
	int32_t ret_val = IXGBE_ERR_MBX;

	DEBUGFUNC("ixgbe_check_for_rst");

	if (mbx->ops.check_for_rst)
		ret_val = mbx->ops.check_for_rst(hw, mbx_id);

	return ret_val;
}

/**
 *  ixgbe_poll_for_msg - Wait for message notification
 *  @hw: pointer to the HW structure
 *  @mbx_id: id of mailbox to write
 *
 *  returns SUCCESS if it successfully received a message notification
 **/
int32_t ixgbe_poll_for_msg(struct ixgbe_hw *hw, uint16_t mbx_id)
{
	struct ixgbe_mbx_info *mbx = &hw->mbx;
	int countdown = mbx->timeout;

	DEBUGFUNC("ixgbe_poll_for_msg");

	if (!countdown || !mbx->ops.check_for_msg)
		return IXGBE_ERR_CONFIG;

	while (countdown && mbx->ops.check_for_msg(hw, mbx_id)) {
		countdown--;
		if (!countdown)
			break;
		usec_delay(mbx->usec_delay);
	}

	if (countdown == 0) {
		ERROR_REPORT2(IXGBE_ERROR_POLLING,
			   "Polling for VF%u mailbox message timedout", mbx_id);
		return IXGBE_ERR_TIMEOUT;
	}

	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_poll_for_ack - Wait for message acknowledgement
 *  @hw: pointer to the HW structure
 *  @mbx_id: id of mailbox to write
 *
 *  returns SUCCESS if it successfully received a message acknowledgement
 **/
int32_t ixgbe_poll_for_ack(struct ixgbe_hw *hw, uint16_t mbx_id)
{
	struct ixgbe_mbx_info *mbx = &hw->mbx;
	int countdown = mbx->timeout;

	DEBUGFUNC("ixgbe_poll_for_ack");

	if (!countdown || !mbx->ops.check_for_ack)
		return IXGBE_ERR_CONFIG;

	while (countdown && mbx->ops.check_for_ack(hw, mbx_id)) {
		countdown--;
		if (!countdown)
			break;
		usec_delay(mbx->usec_delay);
	}

	if (countdown == 0) {
		ERROR_REPORT2(IXGBE_ERROR_POLLING,
			     "Polling for VF%u mailbox ack timedout", mbx_id);
		return IXGBE_ERR_TIMEOUT;
	}

	return IXGBE_SUCCESS;
}

/**
 * ixgbe_read_mailbox_vf - read VF's mailbox register
 * @hw: pointer to the HW structure
 *
 * This function is used to read the mailbox register dedicated for VF without
 * losing the read to clear status bits.
 **/
static uint32_t ixgbe_read_mailbox_vf(struct ixgbe_hw *hw)
{
	uint32_t vf_mailbox = IXGBE_READ_REG(hw, IXGBE_VFMAILBOX);

	vf_mailbox |= hw->mbx.vf_mailbox;
	hw->mbx.vf_mailbox |= vf_mailbox & IXGBE_VFMAILBOX_R2C_BITS;

	return vf_mailbox;
}

static void ixgbe_clear_msg_vf(struct ixgbe_hw *hw)
{
	uint32_t vf_mailbox = ixgbe_read_mailbox_vf(hw);

	if (vf_mailbox & IXGBE_VFMAILBOX_PFSTS) {
		hw->mbx.stats.reqs++;
		hw->mbx.vf_mailbox &= ~IXGBE_VFMAILBOX_PFSTS;
	}
}

static void ixgbe_clear_ack_vf(struct ixgbe_hw *hw)
{
	uint32_t vf_mailbox = ixgbe_read_mailbox_vf(hw);

	if (vf_mailbox & IXGBE_VFMAILBOX_PFACK) {
		hw->mbx.stats.acks++;
		hw->mbx.vf_mailbox &= ~IXGBE_VFMAILBOX_PFACK;
	}
}

static void ixgbe_clear_rst_vf(struct ixgbe_hw *hw)
{
	uint32_t vf_mailbox = ixgbe_read_mailbox_vf(hw);

	if (vf_mailbox & (IXGBE_VFMAILBOX_RSTI | IXGBE_VFMAILBOX_RSTD)) {
		hw->mbx.stats.rsts++;
		hw->mbx.vf_mailbox &= ~(IXGBE_VFMAILBOX_RSTI |
					IXGBE_VFMAILBOX_RSTD);
	}
}

/**
 * ixgbe_check_for_bit_vf - Determine if a status bit was set
 * @hw: pointer to the HW structure
 * @mask: bitmask for bits to be tested and cleared
 *
 * This function is used to check for the read to clear bits within
 * the V2P mailbox.
 **/
static int32_t ixgbe_check_for_bit_vf(struct ixgbe_hw *hw, uint32_t mask)
{
	uint32_t vf_mailbox = ixgbe_read_mailbox_vf(hw);

	if (vf_mailbox & mask)
		return IXGBE_SUCCESS;

	return IXGBE_ERR_MBX;
}

/**
 * ixgbe_check_for_msg_vf - checks to see if the PF has sent mail
 * @hw: pointer to the HW structure
 * @mbx_id: id of mailbox to check
 *
 * returns SUCCESS if the PF has set the Status bit or else ERR_MBX
 **/
static int32_t ixgbe_check_for_msg_vf(struct ixgbe_hw *hw, uint16_t mbx_id)
{
	DEBUGFUNC("ixgbe_check_for_msg_vf");

	if (!ixgbe_check_for_bit_vf(hw, IXGBE_VFMAILBOX_PFSTS))
		return IXGBE_SUCCESS;

	return IXGBE_ERR_MBX;
}

/**
 * ixgbe_check_for_ack_vf - checks to see if the PF has ACK'd
 * @hw: pointer to the HW structure
 * @mbx_id: id of mailbox to check
 *
 * returns SUCCESS if the PF has set the ACK bit or else ERR_MBX
 **/
static int32_t ixgbe_check_for_ack_vf(struct ixgbe_hw *hw, uint16_t mbx_id)
{
	DEBUGFUNC("ixgbe_check_for_ack_vf");

	if (!ixgbe_check_for_bit_vf(hw, IXGBE_VFMAILBOX_PFACK)) {
		/* TODO: should this be autocleared? */
		ixgbe_clear_ack_vf(hw);
		return IXGBE_SUCCESS;
	}

	return IXGBE_ERR_MBX;
}

/**
 * ixgbe_check_for_rst_vf - checks to see if the PF has reset
 * @hw: pointer to the HW structure
 * @mbx_id: id of mailbox to check
 *
 * returns TRUE if the PF has set the reset done bit or else FALSE
 **/
static int32_t ixgbe_check_for_rst_vf(struct ixgbe_hw *hw, uint16_t mbx_id)
{
	DEBUGFUNC("ixgbe_check_for_rst_vf");

	if (!ixgbe_check_for_bit_vf(hw, IXGBE_VFMAILBOX_RSTI |
					  IXGBE_VFMAILBOX_RSTD)) {
		/* TODO: should this be autocleared? */
		ixgbe_clear_rst_vf(hw);
		return IXGBE_SUCCESS;
	}

	return IXGBE_ERR_MBX;
}

/**
 * ixgbe_obtain_mbx_lock_vf - obtain mailbox lock
 * @hw: pointer to the HW structure
 *
 * return SUCCESS if we obtained the mailbox lock
 **/
static int32_t ixgbe_obtain_mbx_lock_vf(struct ixgbe_hw *hw)
{
	struct ixgbe_mbx_info *mbx = &hw->mbx;
	int countdown = mbx->timeout;
	int32_t ret_val = IXGBE_ERR_MBX;
	uint32_t vf_mailbox;

	DEBUGFUNC("ixgbe_obtain_mbx_lock_vf");

	if (!mbx->timeout)
		return IXGBE_ERR_CONFIG;

	while (countdown--) {
		/* Reserve mailbox for VF use */
		vf_mailbox = ixgbe_read_mailbox_vf(hw);
		vf_mailbox |= IXGBE_VFMAILBOX_VFU;
		IXGBE_WRITE_REG(hw, IXGBE_VFMAILBOX, vf_mailbox);

		/* Verify that VF is the owner of the lock */
		if (ixgbe_read_mailbox_vf(hw) & IXGBE_VFMAILBOX_VFU) {
			ret_val = IXGBE_SUCCESS;
			break;
		}

		/* Wait a bit before trying again */
		usec_delay(mbx->usec_delay);
	}

	if (ret_val != IXGBE_SUCCESS) {
		ERROR_REPORT1(IXGBE_ERROR_INVALID_STATE,
				"Failed to obtain mailbox lock");
		ret_val = IXGBE_ERR_TIMEOUT;
	}

	return ret_val;
}

/**
 *  ixgbe_read_posted_mbx - Wait for message notification and receive message
 *  @hw: pointer to the HW structure
 *  @msg: The message buffer
 *  @size: Length of buffer
 *  @mbx_id: id of mailbox to write
 *
 *  returns SUCCESS if it successfully received a message notification and
 *  copied it into the receive buffer.
 **/
int32_t ixgbe_read_posted_mbx(struct ixgbe_hw *hw, uint32_t *msg, uint16_t size, uint16_t mbx_id)
{
	struct ixgbe_mbx_info *mbx = &hw->mbx;
	int32_t ret_val = IXGBE_ERR_MBX;

	DEBUGFUNC("ixgbe_read_posted_mbx");

	if (!mbx->ops.read)
		goto out;

	ret_val = ixgbe_poll_for_msg(hw, mbx_id);

	/* if ack received read message, otherwise we timed out */
	if (!ret_val)
		ret_val = mbx->ops.read(hw, msg, size, mbx_id);
out:
	return ret_val;
}

/**
 *  ixgbe_write_posted_mbx - Write a message to the mailbox, wait for ack
 *  @hw: pointer to the HW structure
 *  @msg: The message buffer
 *  @size: Length of buffer
 *  @mbx_id: id of mailbox to write
 *
 *  returns SUCCESS if it successfully copied message into the buffer and
 *  received an ack to that message within delay * timeout period
 **/
int32_t ixgbe_write_posted_mbx(struct ixgbe_hw *hw, uint32_t *msg, uint16_t size,
			   uint16_t mbx_id)
{
	struct ixgbe_mbx_info *mbx = &hw->mbx;
	int32_t ret_val = IXGBE_ERR_MBX;

	DEBUGFUNC("ixgbe_write_posted_mbx");

	/* exit if either we can't write or there isn't a defined timeout */
	if (!mbx->ops.write || !mbx->timeout)
		goto out;

	/* send msg */
	ret_val = mbx->ops.write(hw, msg, size, mbx_id);

	/* if msg sent wait until we receive an ack */
	if (!ret_val)
		ret_val = ixgbe_poll_for_ack(hw, mbx_id);
out:
	return ret_val;
}

/**
 *  ixgbe_init_mbx_ops_generic - Initialize MB function pointers
 *  @hw: pointer to the HW structure
 *
 *  Setups up the mailbox read and write message function pointers
 **/
void ixgbe_init_mbx_ops_generic(struct ixgbe_hw *hw)
{
	struct ixgbe_mbx_info *mbx = &hw->mbx;

	mbx->ops.read_posted = ixgbe_read_posted_mbx;
	mbx->ops.write_posted = ixgbe_write_posted_mbx;
}

int32_t ixgbe_check_for_bit_pf(struct ixgbe_hw *hw, uint32_t mask, int32_t index)
{
	uint32_t pfmbicr = IXGBE_READ_REG(hw, IXGBE_PFMBICR(index));

	if (pfmbicr & mask)
		return IXGBE_SUCCESS;

	return IXGBE_ERR_MBX;
}

/**
 *  ixgbe_check_for_msg_pf - checks to see if the VF has sent mail
 *  @hw: pointer to the HW structure
 *  @vf_number: the VF index
 *
 *  returns SUCCESS if the VF has set the Status bit or else ERR_MBX
 **/
int32_t ixgbe_check_for_msg_pf(struct ixgbe_hw *hw, uint16_t vf_id)
{
	uint32_t vf_shift = IXGBE_PFMBICR_SHIFT(vf_id);
	int32_t index = IXGBE_PFMBICR_INDEX(vf_id);
	DEBUGFUNC("ixgbe_check_for_msg_pf");

	if (!ixgbe_check_for_bit_pf(hw, IXGBE_PFMBICR_VFREQ_VF1 << vf_shift,
				    index))
		return IXGBE_SUCCESS;

	return IXGBE_ERR_MBX;
}

static void ixgbe_clear_msg_pf(struct ixgbe_hw *hw, uint16_t vf_id)
{
	uint32_t vf_shift = IXGBE_PFMBICR_SHIFT(vf_id);
	int32_t index = IXGBE_PFMBICR_INDEX(vf_id);
	uint32_t pfmbicr;

	pfmbicr = IXGBE_READ_REG(hw, IXGBE_PFMBICR(index));

	if (pfmbicr & (IXGBE_PFMBICR_VFREQ_VF1 << vf_shift))
		hw->mbx.stats.reqs++;

	IXGBE_WRITE_REG(hw, IXGBE_PFMBICR(index),
			IXGBE_PFMBICR_VFREQ_VF1 << vf_shift);
}

static void ixgbe_clear_ack_pf(struct ixgbe_hw *hw, uint16_t vf_id)
{
	uint32_t vf_shift = IXGBE_PFMBICR_SHIFT(vf_id);
	int32_t index = IXGBE_PFMBICR_INDEX(vf_id);
	uint32_t pfmbicr;

	pfmbicr = IXGBE_READ_REG(hw, IXGBE_PFMBICR(index));

	if (pfmbicr & (IXGBE_PFMBICR_VFACK_VF1 << vf_shift))
		hw->mbx.stats.acks++;

	IXGBE_WRITE_REG(hw, IXGBE_PFMBICR(index),
			IXGBE_PFMBICR_VFACK_VF1 << vf_shift);
}

/**
 *  ixgbe_check_for_ack_pf - checks to see if the VF has ACKed
 *  @hw: pointer to the HW structure
 *  @vf_number: the VF index
 *
 *  returns SUCCESS if the VF has set the Status bit or else ERR_MBX
 **/
int32_t ixgbe_check_for_ack_pf(struct ixgbe_hw *hw, uint16_t vf_id)
{
	uint32_t vf_shift = IXGBE_PFMBICR_SHIFT(vf_id);
	int32_t index = IXGBE_PFMBICR_INDEX(vf_id);
	int32_t ret_val = IXGBE_ERR_MBX;

	DEBUGFUNC("ixgbe_check_for_ack_pf");

	if (!ixgbe_check_for_bit_pf(hw, IXGBE_PFMBICR_VFACK_VF1 << vf_shift,
				    index)) {
		ret_val = IXGBE_SUCCESS;
		/* TODO: should this be autocleared? */
		ixgbe_clear_ack_pf(hw, vf_id);
	}

	return ret_val;
}

/**
 *  ixgbe_check_for_rst_pf - checks to see if the VF has reset
 *  @hw: pointer to the HW structure
 *  @vf_number: the VF index
 *
 *  returns SUCCESS if the VF has set the Status bit or else ERR_MBX
 **/
int32_t ixgbe_check_for_rst_pf(struct ixgbe_hw *hw, uint16_t vf_id)
{
	uint32_t vf_shift = IXGBE_PFVFLRE_SHIFT(vf_id);
	uint32_t index = IXGBE_PFVFLRE_INDEX(vf_id);
	int32_t ret_val = IXGBE_ERR_MBX;
	uint32_t vflre = 0;

	DEBUGFUNC("ixgbe_check_for_rst_pf");

	switch (hw->mac.type) {
	case ixgbe_mac_82599EB:
		vflre = IXGBE_READ_REG(hw, IXGBE_PFVFLRE(index));
		break;
	case ixgbe_mac_X550:
	case ixgbe_mac_X550EM_x:
	case ixgbe_mac_X550EM_a:
	case ixgbe_mac_X540:
		vflre = IXGBE_READ_REG(hw, IXGBE_PFVFLREC(index));
		break;
	default:
		break;
	}

	if (vflre & (1 << vf_shift)) {
		ret_val = IXGBE_SUCCESS;
		IXGBE_WRITE_REG(hw, IXGBE_PFVFLREC(index), (1 << vf_shift));
		hw->mbx.stats.rsts++;
	}

	return ret_val;
}

/**
 *  ixgbe_obtain_mbx_lock_pf - obtain mailbox lock
 *  @hw: pointer to the HW structure
 *  @vf_number: the VF index
 *
 *  return SUCCESS if we obtained the mailbox lock
 **/
int32_t ixgbe_obtain_mbx_lock_pf(struct ixgbe_hw *hw, uint16_t vf_id)
{
	struct ixgbe_mbx_info *mbx = &hw->mbx;
	int countdown = mbx->timeout;
	int32_t ret_val = IXGBE_ERR_MBX;
	uint32_t pf_mailbox;

	DEBUGFUNC("ixgbe_obtain_mbx_lock_pf");

	if (!mbx->timeout)
		return IXGBE_ERR_CONFIG;

	while (countdown--) {
		/* Reserve mailbox for PF use */
		pf_mailbox = IXGBE_READ_REG(hw, IXGBE_PFMAILBOX(vf_id));
		pf_mailbox |= IXGBE_PFMAILBOX_PFU;
		IXGBE_WRITE_REG(hw, IXGBE_PFMAILBOX(vf_id), pf_mailbox);

		/* Verify that PF is the owner of the lock */
		pf_mailbox = IXGBE_READ_REG(hw, IXGBE_PFMAILBOX(vf_id));
		if (pf_mailbox & IXGBE_PFMAILBOX_PFU) {
			ret_val = IXGBE_SUCCESS;
			break;
		}

		/* Wait a bit before trying again */
		usec_delay(mbx->usec_delay);
	}

	if (ret_val != IXGBE_SUCCESS) {
		ERROR_REPORT1(IXGBE_ERROR_INVALID_STATE,
			      "Failed to obtain mailbox lock");
		ret_val = IXGBE_ERR_TIMEOUT;
	}

	return ret_val;
}

/**
 *  ixgbe_write_mbx_pf - Places a message in the mailbox
 *  @hw: pointer to the HW structure
 *  @msg: The message buffer
 *  @size: Length of buffer
 *  @vf_number: the VF index
 *
 *  returns SUCCESS if it successfully copied message into the buffer
 **/
int32_t ixgbe_write_mbx_pf(struct ixgbe_hw *hw, uint32_t *msg, uint16_t size,
			   uint16_t vf_id)
{
	uint32_t pf_mailbox;
	int32_t ret_val;
	uint16_t i;

	DEBUGFUNC("ixgbe_write_mbx_pf");

	/* lock the mailbox to prevent pf/vf race condition */
	ret_val = ixgbe_obtain_mbx_lock_pf(hw, vf_id);
	if (ret_val)
		goto out;

	/* flush msg and acks as we are overwriting the message buffer */
	ixgbe_clear_msg_pf(hw, vf_id);
	ixgbe_clear_ack_pf(hw, vf_id);

	/* copy the caller specified message to the mailbox memory buffer */
	for (i = 0; i < size; i++)
		IXGBE_WRITE_REG_ARRAY(hw, IXGBE_PFMBMEM(vf_id), i, msg[i]);

	/* Interrupt VF to tell it a message has been sent */
	pf_mailbox = IXGBE_READ_REG(hw, IXGBE_PFMAILBOX(vf_id));
	pf_mailbox |= IXGBE_PFMAILBOX_STS;
	IXGBE_WRITE_REG(hw, IXGBE_PFMAILBOX(vf_id), pf_mailbox);

	/* if msg sent wait until we receive an ack */
	ixgbe_poll_for_ack(hw, vf_id);

	/* update stats */
	hw->mbx.stats.msgs_tx++;

out:
	hw->mbx.ops.release(hw, vf_id);

	return ret_val;
}

/**
 * ixgbe_read_mbx_pf_legacy - Read a message from the mailbox
 * @hw: pointer to the HW structure
 * @msg: The message buffer
 * @size: Length of buffer
 * @vf_id: the VF index
 *
 * This function copies a message from the mailbox buffer to the caller's
 * memory buffer.  The presumption is that the caller knows that there was
 * a message due to a VF request so no polling for message is needed.
 **/
static int32_t ixgbe_read_mbx_pf_legacy(struct ixgbe_hw *hw, uint32_t *msg,
					uint16_t size, uint16_t vf_id)
{
	int32_t ret_val;
	uint16_t i;

	DEBUGFUNC("ixgbe_read_mbx_pf_legacy");

	/* lock the mailbox to prevent pf/vf race condition */
	ret_val = ixgbe_obtain_mbx_lock_pf(hw, vf_id);
	if (ret_val != IXGBE_SUCCESS)
		return ret_val;

	/* copy the message to the mailbox memory buffer */
	for (i = 0; i < size; i++)
		msg[i] = IXGBE_READ_REG_ARRAY(hw, IXGBE_PFMBMEM(vf_id), i);

	/* Acknowledge the message and release buffer */
	IXGBE_WRITE_REG(hw, IXGBE_PFMAILBOX(vf_id), IXGBE_PFMAILBOX_ACK);

	/* update stats */
	hw->mbx.stats.msgs_rx++;

	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_read_mbx_pf - Read a message from the mailbox
 *  @hw: pointer to the HW structure
 *  @msg: The message buffer
 *  @size: Length of buffer
 *  @vf_number: the VF index
 *
 *  This function copies a message from the mailbox buffer to the caller's
 *  memory buffer.  The presumption is that the caller knows that there was
 *  a message due to a VF request so no polling for message is needed.
 **/
int32_t ixgbe_read_mbx_pf(struct ixgbe_hw *hw, uint32_t *msg, uint16_t size,
			  uint16_t vf_id)
{
	uint32_t pf_mailbox;
	int32_t ret_val;
	uint16_t i;

	DEBUGFUNC("ixgbe_read_mbx_pf");

	/* check if there is a message from VF */
	ret_val = ixgbe_check_for_msg_pf(hw, vf_id);
	if (ret_val != IXGBE_SUCCESS)
		return IXGBE_ERR_MBX_NOMSG;

	ixgbe_clear_msg_pf(hw, vf_id);

	/* copy the message to the mailbox memory buffer */
	for (i = 0; i < size; i++)
		msg[i] = IXGBE_READ_REG_ARRAY(hw, IXGBE_PFMBMEM(vf_id), i);

	/* Acknowledge the message and release buffer */
	pf_mailbox = IXGBE_READ_REG(hw, IXGBE_PFMAILBOX(vf_id));
	pf_mailbox |= IXGBE_PFMAILBOX_ACK;
	IXGBE_WRITE_REG(hw, IXGBE_PFMAILBOX(vf_id), pf_mailbox);

	/* update stats */
	hw->mbx.stats.msgs_rx++;

	return IXGBE_SUCCESS;
}

/**
 * ixgbe_release_mbx_lock_dummy - release mailbox lock
 * @hw: pointer to the HW structure
 * @mbx_id: id of mailbox to read
 **/
static void ixgbe_release_mbx_lock_dummy(struct ixgbe_hw *hw, uint16_t mbx_id)
{
	DEBUGFUNC("ixgbe_release_mbx_lock_dummy");
}

/**
 * ixgbe_write_mbx_vf_legacy - Write a message to the mailbox
 * @hw: pointer to the HW structure
 * @msg: The message buffer
 * @size: Length of buffer
 * @mbx_id: id of mailbox to write
 *
 * returns SUCCESS if it successfully copied message into the buffer
 **/
static int32_t ixgbe_write_mbx_vf_legacy(struct ixgbe_hw *hw, uint32_t *msg,
					 uint16_t size, uint16_t mbx_id)
{
	int32_t ret_val;
	uint16_t i;

	DEBUGFUNC("ixgbe_write_mbx_vf_legacy");

	/* lock the mailbox to prevent pf/vf race condition */
	ret_val = ixgbe_obtain_mbx_lock_vf(hw);
	if (ret_val)
		return ret_val;

	/* flush msg and acks as we are overwriting the message buffer */
	ixgbe_check_for_msg_vf(hw, 0);
	ixgbe_clear_msg_vf(hw);
	ixgbe_check_for_ack_vf(hw, 0);
	ixgbe_clear_ack_vf(hw);

	/* copy the caller specified message to the mailbox memory buffer */
	for (i = 0; i < size; i++)
		IXGBE_WRITE_REG_ARRAY(hw, IXGBE_VFMBMEM, i, msg[i]);

	/* update stats */
	hw->mbx.stats.msgs_tx++;

	/* interrupt the PF to tell it a message has been sent */
	IXGBE_WRITE_REG(hw, IXGBE_VFMAILBOX, IXGBE_VFMAILBOX_REQ);

	return IXGBE_SUCCESS;
}

/**
 * ixgbe_read_mbx_vf_legacy - Reads a message from the inbox intended for vf
 * @hw: pointer to the HW structure
 * @msg: The message buffer
 * @size: Length of buffer
 * @mbx_id: id of mailbox to read
 *
 * returns SUCCESS if it successfully read message from buffer
 **/
static int32_t ixgbe_read_mbx_vf_legacy(struct ixgbe_hw *hw, uint32_t *msg,
					uint16_t size, uint16_t mbx_id)
{
	int32_t ret_val;
	uint16_t i;

	DEBUGFUNC("ixgbe_read_mbx_vf_legacy");

	/* lock the mailbox to prevent pf/vf race condition */
	ret_val = ixgbe_obtain_mbx_lock_vf(hw);
	if (ret_val)
		return ret_val;

	/* copy the message from the mailbox memory buffer */
	for (i = 0; i < size; i++)
		msg[i] = IXGBE_READ_REG_ARRAY(hw, IXGBE_VFMBMEM, i);

	/* Acknowledge receipt and release mailbox, then we're done */
	IXGBE_WRITE_REG(hw, IXGBE_VFMAILBOX, IXGBE_VFMAILBOX_ACK);

	/* update stats */
	hw->mbx.stats.msgs_rx++;

	return IXGBE_SUCCESS;
}

/**
 * ixgbe_init_mbx_params_vf - set initial values for vf mailbox
 * @hw: pointer to the HW structure
 *
 * Initializes single set the hw->mbx struct to correct values for vf mailbox
 * Set of legacy functions is being used here
 */
void ixgbe_init_mbx_params_vf(struct ixgbe_hw *hw)
{
	struct ixgbe_mbx_info *mbx = &hw->mbx;

	mbx->timeout = IXGBE_VF_MBX_INIT_TIMEOUT;
	mbx->usec_delay = IXGBE_VF_MBX_INIT_DELAY;

	mbx->size = IXGBE_VFMAILBOX_SIZE;

	mbx->ops.release = ixgbe_release_mbx_lock_dummy;
	mbx->ops.read = ixgbe_read_mbx_vf_legacy;
	mbx->ops.write = ixgbe_write_mbx_vf_legacy;
	mbx->ops.check_for_msg = ixgbe_check_for_msg_vf;
	mbx->ops.check_for_ack = ixgbe_check_for_ack_vf;
	mbx->ops.check_for_rst = ixgbe_check_for_rst_vf;
	mbx->ops.clear = NULL;

	mbx->stats.msgs_tx = 0;
	mbx->stats.msgs_rx = 0;
	mbx->stats.reqs = 0;
	mbx->stats.acks = 0;
	mbx->stats.rsts = 0;
}

/**
 * ixgbe_write_mbx_pf_legacy - Places a message in the mailbox
 * @hw: pointer to the HW structure
 * @msg: The message buffer
 * @size: Length of buffer
 * @vf_id: the VF index
 *
 * returns SUCCESS if it successfully copied message into the buffer
 **/
static int32_t ixgbe_write_mbx_pf_legacy(struct ixgbe_hw *hw, uint32_t *msg,
					 uint16_t size, uint16_t vf_id)
{
	int32_t ret_val;
	uint16_t i;

	DEBUGFUNC("ixgbe_write_mbx_pf_legacy");

	/* lock the mailbox to prevent pf/vf race condition */
	ret_val = ixgbe_obtain_mbx_lock_pf(hw, vf_id);
	if (ret_val)
		return ret_val;

	/* flush msg and acks as we are overwriting the message buffer */
	ixgbe_check_for_msg_pf(hw, vf_id);
	ixgbe_clear_msg_pf(hw, vf_id);
	ixgbe_check_for_ack_pf(hw, vf_id);
	ixgbe_clear_ack_pf(hw, vf_id);

	/* copy the caller specified message to the mailbox memory buffer */
	for (i = 0; i < size; i++)
		IXGBE_WRITE_REG_ARRAY(hw, IXGBE_PFMBMEM(vf_id), i, msg[i]);

	/* Interrupt VF to tell it a message has been sent and release buffer*/
	IXGBE_WRITE_REG(hw, IXGBE_PFMAILBOX(vf_id), IXGBE_PFMAILBOX_STS);

	/* update stats */
	hw->mbx.stats.msgs_tx++;

	return IXGBE_SUCCESS;
}

/**
 * ixgbe_clear_mbx_pf - Clear Mailbox Memory
 * @hw: pointer to the HW structure
 * @vf_id: the VF index
 *
 * Set VFMBMEM of given VF to 0x0.
 **/
static int32_t ixgbe_clear_mbx_pf(struct ixgbe_hw *hw, uint16_t vf_id)
{
	uint16_t mbx_size = hw->mbx.size;
	uint16_t i;

	if (vf_id > 63)
		return IXGBE_ERR_PARAM;

	for (i = 0; i < mbx_size; ++i)
		IXGBE_WRITE_REG_ARRAY(hw, IXGBE_PFMBMEM(vf_id), i, 0x0);

	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_init_mbx_params_pf - set initial values for pf mailbox
 *  @hw: pointer to the HW structure
 *
 *  Initializes the hw->mbx struct to correct values for pf mailbox
 */
void ixgbe_init_mbx_params_pf(struct ixgbe_hw *hw)
{
	struct ixgbe_mbx_info *mbx = &hw->mbx;

	if (hw->mac.type != ixgbe_mac_82599EB &&
	    hw->mac.type != ixgbe_mac_X550 &&
	    hw->mac.type != ixgbe_mac_X550EM_x &&
	    hw->mac.type != ixgbe_mac_X550EM_a &&
	    hw->mac.type != ixgbe_mac_X540)
		return;

	/* Initialize common mailbox settings */
	mbx->timeout = IXGBE_VF_MBX_INIT_TIMEOUT;
	mbx->usec_delay = IXGBE_VF_MBX_INIT_DELAY;
	mbx->size = IXGBE_VFMAILBOX_SIZE;

	/* Initialize counters with zeroes */
	mbx->stats.msgs_tx = 0;
	mbx->stats.msgs_rx = 0;
	mbx->stats.reqs = 0;
	mbx->stats.acks = 0;
	mbx->stats.rsts = 0;

	/* Initialize mailbox operations */
	mbx->ops.release = ixgbe_release_mbx_lock_dummy;
	mbx->ops.read = ixgbe_read_mbx_pf_legacy;
	mbx->ops.write = ixgbe_write_mbx_pf_legacy;
	mbx->ops.check_for_msg = ixgbe_check_for_msg_pf;
	mbx->ops.check_for_ack = ixgbe_check_for_ack_pf;
	mbx->ops.check_for_rst = ixgbe_check_for_rst_pf;
	mbx->ops.clear = ixgbe_clear_mbx_pf;
}
