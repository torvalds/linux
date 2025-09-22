/*	$OpenBSD: ixgbe_vf.c,v 1.1 2024/11/02 04:37:20 yasuoka Exp $	*/

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

#include <dev/pci/ixgbe.h>
#include <dev/pci/ixgbe_type.h>

#ifndef IXGBE_VFWRITE_REG
#define IXGBE_VFWRITE_REG IXGBE_WRITE_REG
#endif
#ifndef IXGBE_VFREAD_REG
#define IXGBE_VFREAD_REG IXGBE_READ_REG
#endif

/**
   Dummy handlers.
   They are called from ix driver code,
   and there is nothing to do for VF.
 */
static uint64_t
ixgbe_dummy_uint64_handler_vf(struct ixgbe_hw *hw)
{
	return 0;
}

static int32_t
ixgbe_dummy_handler_vf(struct ixgbe_hw *hw)
{
	return 0;
}

static void
ixgbe_dummy_void_handler_vf(struct ixgbe_hw *hw)
{
	return;
}

/**
 * ixgbe_init_ops_vf - Initialize the pointers for vf
 * @hw: pointer to hardware structure
 *
 * This will assign function pointers, adapter-specific functions can
 * override the assignment of generic function pointers by assigning
 * their own adapter-specific function pointers.
 * Does not touch the hardware.
 **/
int32_t ixgbe_init_ops_vf(struct ixgbe_hw *hw)
{
	/* MAC */
	hw->mac.ops.init_hw = ixgbe_init_hw_vf;
	hw->mac.ops.reset_hw = ixgbe_reset_hw_vf;
	hw->mac.ops.start_hw = ixgbe_start_hw_vf;
	/* Cannot clear stats on VF */
	hw->mac.ops.clear_hw_cntrs = NULL;
	hw->mac.ops.get_media_type = NULL;
	hw->mac.ops.get_supported_physical_layer =
		ixgbe_dummy_uint64_handler_vf;
	hw->mac.ops.get_mac_addr = ixgbe_get_mac_addr_vf;
	hw->mac.ops.stop_adapter = ixgbe_stop_adapter_vf;
	hw->mac.ops.get_bus_info = NULL;
	hw->mac.ops.negotiate_api_version = ixgbevf_negotiate_api_version;

	/* Link */
	hw->mac.ops.setup_link = ixgbe_setup_mac_link_vf;
	hw->mac.ops.check_link = ixgbe_check_mac_link_vf;
	hw->mac.ops.get_link_capabilities = NULL;

	/* RAR, Multicast, VLAN */
	hw->mac.ops.set_rar = ixgbe_set_rar_vf;
	hw->mac.ops.set_uc_addr = ixgbevf_set_uc_addr_vf;
	hw->mac.ops.init_rx_addrs = NULL;
	hw->mac.ops.update_mc_addr_list = ixgbe_update_mc_addr_list_vf;
	hw->mac.ops.update_xcast_mode = ixgbevf_update_xcast_mode;
	hw->mac.ops.get_link_state = ixgbe_get_link_state_vf;
	hw->mac.ops.enable_mc = NULL;
	hw->mac.ops.disable_mc = NULL;
	hw->mac.ops.clear_vfta = NULL;
	hw->mac.ops.set_vfta = ixgbe_set_vfta_vf;
	hw->mac.ops.set_rlpml = ixgbevf_rlpml_set_vf;

	/* Flow Control */
	hw->mac.ops.fc_enable = ixgbe_dummy_handler_vf;
	hw->mac.ops.setup_fc = ixgbe_dummy_handler_vf;
	hw->mac.ops.fc_autoneg = ixgbe_dummy_void_handler_vf;

	hw->mac.max_tx_queues = 1;
	hw->mac.max_rx_queues = 1;

	hw->mbx.ops.init_params = ixgbe_init_mbx_params_vf;

	return IXGBE_SUCCESS;
}

/* ixgbe_virt_clr_reg - Set register to default (power on) state.
 * @hw: pointer to hardware structure
 */
static void ixgbe_virt_clr_reg(struct ixgbe_hw *hw)
{
	int i;
	uint32_t vfsrrctl;
	uint32_t vfdca_rxctrl;
	uint32_t vfdca_txctrl;

	/* VRSRRCTL default values (BSIZEPACKET = 2048, BSIZEHEADER = 256) */
	vfsrrctl = 0x100 << IXGBE_SRRCTL_BSIZEHDRSIZE_SHIFT;
	vfsrrctl |= 0x800 >> IXGBE_SRRCTL_BSIZEPKT_SHIFT;

	/* DCA_RXCTRL default value */
	vfdca_rxctrl = IXGBE_DCA_RXCTRL_DESC_RRO_EN |
		       IXGBE_DCA_RXCTRL_DATA_WRO_EN |
		       IXGBE_DCA_RXCTRL_HEAD_WRO_EN;

	/* DCA_TXCTRL default value */
	vfdca_txctrl = IXGBE_DCA_TXCTRL_DESC_RRO_EN |
		       IXGBE_DCA_TXCTRL_DESC_WRO_EN |
		       IXGBE_DCA_TXCTRL_DATA_RRO_EN;

	IXGBE_WRITE_REG(hw, IXGBE_VFPSRTYPE, 0);

	for (i = 0; i < 7; i++) {
		IXGBE_WRITE_REG(hw, IXGBE_VFRDH(i), 0);
		IXGBE_WRITE_REG(hw, IXGBE_VFRDT(i), 0);
		IXGBE_WRITE_REG(hw, IXGBE_VFRXDCTL(i), 0);
		IXGBE_WRITE_REG(hw, IXGBE_VFSRRCTL(i), vfsrrctl);
		IXGBE_WRITE_REG(hw, IXGBE_VFTDH(i), 0);
		IXGBE_WRITE_REG(hw, IXGBE_VFTDT(i), 0);
		IXGBE_WRITE_REG(hw, IXGBE_VFTXDCTL(i), 0);
		IXGBE_WRITE_REG(hw, IXGBE_VFTDWBAH(i), 0);
		IXGBE_WRITE_REG(hw, IXGBE_VFTDWBAL(i), 0);
		IXGBE_WRITE_REG(hw, IXGBE_VFDCA_RXCTRL(i), vfdca_rxctrl);
		IXGBE_WRITE_REG(hw, IXGBE_VFDCA_TXCTRL(i), vfdca_txctrl);
	}

	IXGBE_WRITE_FLUSH(hw);
}

/**
 * ixgbe_start_hw_vf - Prepare hardware for Tx/Rx
 * @hw: pointer to hardware structure
 *
 * Starts the hardware by filling the bus info structure and media type, clears
 * all on chip counters, initializes receive address registers, multicast
 * table, VLAN filter table, calls routine to set up link and flow control
 * settings, and leaves transmit and receive units disabled and uninitialized
 **/
int32_t ixgbe_start_hw_vf(struct ixgbe_hw *hw)
{
	/* Clear adapter stopped flag */
	hw->adapter_stopped = FALSE;

	return IXGBE_SUCCESS;
}

/**
 * ixgbe_init_hw_vf - virtual function hardware initialization
 * @hw: pointer to hardware structure
 *
 * Initialize the hardware by resetting the hardware and then starting
 * the hardware
 **/
int32_t ixgbe_init_hw_vf(struct ixgbe_hw *hw)
{
	int32_t status = hw->mac.ops.start_hw(hw);

	hw->mac.ops.get_mac_addr(hw, hw->mac.addr);

	return status;
}

/**
 * ixgbe_reset_hw_vf - Performs hardware reset
 * @hw: pointer to hardware structure
 *
 * Resets the hardware by resetting the transmit and receive units, masks and
 * clears all interrupts.
 **/
int32_t ixgbe_reset_hw_vf(struct ixgbe_hw *hw)
{
	struct ixgbe_mbx_info *mbx = &hw->mbx;
	uint32_t timeout = IXGBE_VF_INIT_TIMEOUT;
	int32_t ret_val = IXGBE_ERR_INVALID_MAC_ADDR;
	uint32_t msgbuf[IXGBE_VF_PERMADDR_MSG_LEN];
	uint8_t *addr = (uint8_t *)(&msgbuf[1]);

	DEBUGFUNC("ixgbevf_reset_hw_vf");

	/* Call adapter stop to disable tx/rx and clear interrupts */
	hw->mac.ops.stop_adapter(hw);

	/* reset the api version */
	hw->api_version = ixgbe_mbox_api_10;
	ixgbe_init_mbx_params_vf(hw);

	DEBUGOUT("Issuing a function level reset to MAC\n");

	IXGBE_VFWRITE_REG(hw, IXGBE_VFCTRL, IXGBE_CTRL_RST);
	IXGBE_WRITE_FLUSH(hw);

	msec_delay(50);

	/* we cannot reset while the RSTI / RSTD bits are asserted */
	while (!mbx->ops.check_for_rst(hw, 0) && timeout) {
		timeout--;
		usec_delay(5);
	}

	if (!timeout)
		return IXGBE_ERR_RESET_FAILED;

	/* Reset VF registers to initial values */
	ixgbe_virt_clr_reg(hw);

	/* mailbox timeout can now become active */
	mbx->timeout = IXGBE_VF_MBX_INIT_TIMEOUT;

	msgbuf[0] = IXGBE_VF_RESET;
	ixgbe_write_mbx(hw, msgbuf, 1, 0);

	msec_delay(10);

	/*
	 * set our "perm_addr" based on info provided by PF
	 * also set up the mc_filter_type which is piggy backed
	 * on the mac address in word 3
	 */
	ret_val = ixgbe_poll_mbx(hw, msgbuf,
				 IXGBE_VF_PERMADDR_MSG_LEN, 0);
	if (ret_val)
		return ret_val;

	if (msgbuf[0] != (IXGBE_VF_RESET | IXGBE_VT_MSGTYPE_SUCCESS) &&
	    msgbuf[0] != (IXGBE_VF_RESET | IXGBE_VT_MSGTYPE_FAILURE))
		return IXGBE_ERR_INVALID_MAC_ADDR;

	if (msgbuf[0] == (IXGBE_VF_RESET | IXGBE_VT_MSGTYPE_SUCCESS))
		memcpy(hw->mac.perm_addr, addr, IXGBE_ETH_LENGTH_OF_ADDRESS);

	hw->mac.mc_filter_type = msgbuf[IXGBE_VF_MC_TYPE_WORD];

	return ret_val;
}

/**
 * ixgbe_stop_adapter_vf - Generic stop Tx/Rx units
 * @hw: pointer to hardware structure
 *
 * Sets the adapter_stopped flag within ixgbe_hw struct. Clears interrupts,
 * disables transmit and receive units. The adapter_stopped flag is used by
 * the shared code and drivers to determine if the adapter is in a stopped
 * state and should not touch the hardware.
 **/
int32_t ixgbe_stop_adapter_vf(struct ixgbe_hw *hw)
{
	uint32_t reg_val;
	uint16_t i;

	/*
	 * Set the adapter_stopped flag so other driver functions stop touching
	 * the hardware
	 */
	hw->adapter_stopped = TRUE;

	/* Clear interrupt mask to stop from interrupts being generated */
	IXGBE_VFWRITE_REG(hw, IXGBE_VTEIMC, IXGBE_VF_IRQ_CLEAR_MASK);

	/* Clear any pending interrupts, flush previous writes */
	IXGBE_VFREAD_REG(hw, IXGBE_VTEICR);

	/* Disable the transmit unit.  Each queue must be disabled. */
	for (i = 0; i < hw->mac.max_tx_queues; i++)
		IXGBE_VFWRITE_REG(hw, IXGBE_VFTXDCTL(i), IXGBE_TXDCTL_SWFLSH);

	/* Disable the receive unit by stopping each queue */
	for (i = 0; i < hw->mac.max_rx_queues; i++) {
		reg_val = IXGBE_VFREAD_REG(hw, IXGBE_VFRXDCTL(i));
		reg_val &= ~IXGBE_RXDCTL_ENABLE;
		IXGBE_VFWRITE_REG(hw, IXGBE_VFRXDCTL(i), reg_val);
	}
	/* Clear packet split and pool config */
	IXGBE_WRITE_REG(hw, IXGBE_VFPSRTYPE, 0);

	/* flush all queues disables */
	IXGBE_WRITE_FLUSH(hw);
	msec_delay(2);

	return IXGBE_SUCCESS;
}

static int32_t ixgbevf_write_msg_read_ack(struct ixgbe_hw *hw, uint32_t *msg,
				      uint32_t *retmsg, uint16_t size)
{
	int32_t retval = ixgbe_write_mbx(hw, msg, size, 0);

	if (retval)
		return retval;

	return ixgbe_poll_mbx(hw, retmsg, size, 0);
}

/**
 * ixgbe_set_rar_vf - set device MAC address
 * @hw: pointer to hardware structure
 * @index: Receive address register to write
 * @addr: Address to put into receive address register
 * @vmdq: VMDq "set" or "pool" index
 * @enable_addr: set flag that address is active
 **/
int32_t ixgbe_set_rar_vf(struct ixgbe_hw *hw, uint32_t index, uint8_t *addr,
			 uint32_t vmdq, uint32_t enable_addr)
{
	uint32_t msgbuf[3];
	uint8_t *msg_addr = (uint8_t *)(&msgbuf[1]);
	int32_t ret_val;

	memset(msgbuf, 0, 12);
	msgbuf[0] = IXGBE_VF_SET_MAC_ADDR;
	memcpy(msg_addr, addr, 6);
	ret_val = ixgbevf_write_msg_read_ack(hw, msgbuf, msgbuf, 3);

	msgbuf[0] &= ~IXGBE_VT_MSGTYPE_CTS;

	/* if nacked the address was rejected, use "perm_addr" */
	if (!ret_val &&
	    (msgbuf[0] == (IXGBE_VF_SET_MAC_ADDR | IXGBE_VT_MSGTYPE_FAILURE))) {
		ixgbe_get_mac_addr_vf(hw, hw->mac.addr);
		return IXGBE_ERR_MBX;
	}

	return ret_val;
}

/**
 * ixgbe_update_mc_addr_list_vf - Update Multicast addresses
 * @hw: pointer to the HW structure
 * @mc_addr_list: array of multicast addresses to program
 * @mc_addr_count: number of multicast addresses to program
 * @next: caller supplied function to return next address in list
 * @clear: unused
 *
 * Updates the Multicast Table Array.
 **/
int32_t ixgbe_update_mc_addr_list_vf(struct ixgbe_hw *hw, uint8_t *mc_addr_list,
				     uint32_t mc_addr_count, ixgbe_mc_addr_itr next,
				     bool clear)
{
	uint32_t msgbuf[IXGBE_VFMAILBOX_SIZE];
	uint16_t *vector_list = (uint16_t *)&msgbuf[1];
	uint32_t vector;
	uint32_t cnt, i;
	uint32_t vmdq;

	DEBUGFUNC("ixgbe_update_mc_addr_list_vf");

	/* Each entry in the list uses 1 16 bit word.  We have 30
	 * 16 bit words available in our HW msg buffer (minus 1 for the
	 * msg type).  That's 30 hash values if we pack 'em right.  If
	 * there are more than 30 MC addresses to add then punt the
	 * extras for now and then add code to handle more than 30 later.
	 * It would be unusual for a server to request that many multi-cast
	 * addresses except for in large enterprise network environments.
	 */

	DEBUGOUT1("MC Addr Count = %d\n", mc_addr_count);

	cnt = (mc_addr_count > IXGBE_MAX_MULTICAST_ADDRESSES_VF) ? IXGBE_MAX_MULTICAST_ADDRESSES_VF : mc_addr_count;
	msgbuf[0] = IXGBE_VF_SET_MULTICAST;
	msgbuf[0] |= cnt << IXGBE_VT_MSGINFO_SHIFT;

	for (i = 0; i < cnt; i++) {
		vector = ixgbe_mta_vector(hw, next(hw, &mc_addr_list, &vmdq));
		DEBUGOUT1("Hash value = 0x%03X\n", vector);
		vector_list[i] = (uint16_t)vector;
	}

	return ixgbevf_write_msg_read_ack(hw, msgbuf, msgbuf, IXGBE_VFMAILBOX_SIZE);
}

/**
 * ixgbevf_update_xcast_mode - Update Multicast mode
 * @hw: pointer to the HW structure
 * @xcast_mode: new multicast mode
 *
 * Updates the Multicast Mode of VF.
 **/
int32_t ixgbevf_update_xcast_mode(struct ixgbe_hw *hw, int xcast_mode)
{
	uint32_t msgbuf[2];
	int32_t err;

	switch (hw->api_version) {
	case ixgbe_mbox_api_12:
		/* New modes were introduced in 1.3 version */
		if (xcast_mode > IXGBEVF_XCAST_MODE_ALLMULTI)
			return IXGBE_ERR_FEATURE_NOT_SUPPORTED;
		/* Fall through */
	case ixgbe_mbox_api_13:
	case ixgbe_mbox_api_15:
		break;
	default:
		return IXGBE_ERR_FEATURE_NOT_SUPPORTED;
	}

	msgbuf[0] = IXGBE_VF_UPDATE_XCAST_MODE;
	msgbuf[1] = xcast_mode;

	err = ixgbevf_write_msg_read_ack(hw, msgbuf, msgbuf, 2);
	if (err)
		return err;

	msgbuf[0] &= ~IXGBE_VT_MSGTYPE_CTS;
	if (msgbuf[0] == (IXGBE_VF_UPDATE_XCAST_MODE | IXGBE_VT_MSGTYPE_FAILURE))
		return IXGBE_ERR_FEATURE_NOT_SUPPORTED;
	return IXGBE_SUCCESS;
}

/**
 * ixgbe_get_link_state_vf - Get VF link state from PF
 * @hw: pointer to the HW structure
 * @link_state: link state storage
 *
 * Returns state of the operation error or success.
 **/
int32_t ixgbe_get_link_state_vf(struct ixgbe_hw *hw, bool *link_state)
{
	uint32_t msgbuf[2];
	int32_t err;
	int32_t ret_val;

	msgbuf[0] = IXGBE_VF_GET_LINK_STATE;
	msgbuf[1] = 0x0;

	err = ixgbevf_write_msg_read_ack(hw, msgbuf, msgbuf, 2);

	if (err || (msgbuf[0] & IXGBE_VT_MSGTYPE_FAILURE)) {
		ret_val = IXGBE_ERR_MBX;
	} else {
		ret_val = IXGBE_SUCCESS;
		*link_state = msgbuf[1];
	}

	return ret_val;
}

/**
 * ixgbe_set_vfta_vf - Set/Unset vlan filter table address
 * @hw: pointer to the HW structure
 * @vlan: 12 bit VLAN ID
 * @vind: unused by VF drivers
 * @vlan_on: if TRUE then set bit, else clear bit
 * @vlvf_bypass: boolean flag indicating updating default pool is okay
 *
 * Turn on/off specified VLAN in the VLAN filter table.
 **/
int32_t ixgbe_set_vfta_vf(struct ixgbe_hw *hw, uint32_t vlan, uint32_t vind,
		      bool vlan_on, bool vlvf_bypass)
{
	uint32_t msgbuf[2];
	int32_t ret_val;

	msgbuf[0] = IXGBE_VF_SET_VLAN;
	msgbuf[1] = vlan;
	/* Setting the 8 bit field MSG INFO to TRUE indicates "add" */
	msgbuf[0] |= vlan_on << IXGBE_VT_MSGINFO_SHIFT;

	ret_val = ixgbevf_write_msg_read_ack(hw, msgbuf, msgbuf, 2);
	if (!ret_val && (msgbuf[0] & IXGBE_VT_MSGTYPE_SUCCESS))
		return IXGBE_SUCCESS;

	return ret_val | (msgbuf[0] & IXGBE_VT_MSGTYPE_FAILURE);
}

/**
 * ixgbe_get_num_of_tx_queues_vf - Get number of TX queues
 * @hw: pointer to hardware structure
 *
 * Returns the number of transmit queues for the given adapter.
 **/
uint32_t ixgbe_get_num_of_tx_queues_vf(struct ixgbe_hw *hw)
{
	return IXGBE_VF_MAX_TX_QUEUES;
}

/**
 * ixgbe_get_num_of_rx_queues_vf - Get number of RX queues
 * @hw: pointer to hardware structure
 *
 * Returns the number of receive queues for the given adapter.
 **/
uint32_t ixgbe_get_num_of_rx_queues_vf(struct ixgbe_hw *hw)
{
	return IXGBE_VF_MAX_RX_QUEUES;
}

/**
 * ixgbe_get_mac_addr_vf - Read device MAC address
 * @hw: pointer to the HW structure
 * @mac_addr: the MAC address
 **/
int32_t ixgbe_get_mac_addr_vf(struct ixgbe_hw *hw, uint8_t *mac_addr)
{
	int i;

	for (i = 0; i < IXGBE_ETH_LENGTH_OF_ADDRESS; i++)
		mac_addr[i] = hw->mac.perm_addr[i];

	return IXGBE_SUCCESS;
}

int32_t ixgbevf_set_uc_addr_vf(struct ixgbe_hw *hw, uint32_t index, uint8_t *addr)
{
	uint32_t msgbuf[3], msgbuf_chk;
	uint8_t *msg_addr = (uint8_t *)(&msgbuf[1]);
	int32_t ret_val;

	memset(msgbuf, 0, sizeof(msgbuf));
	/*
	 * If index is one then this is the start of a new list and needs
	 * indication to the PF so it can do it's own list management.
	 * If it is zero then that tells the PF to just clear all of
	 * this VF's macvlans and there is no new list.
	 */
	msgbuf[0] |= index << IXGBE_VT_MSGINFO_SHIFT;
	msgbuf[0] |= IXGBE_VF_SET_MACVLAN;
	msgbuf_chk = msgbuf[0];
	if (addr)
		memcpy(msg_addr, addr, 6);

	ret_val = ixgbevf_write_msg_read_ack(hw, msgbuf, msgbuf, 3);
	if (!ret_val) {
		msgbuf[0] &= ~IXGBE_VT_MSGTYPE_CTS;

		if (msgbuf[0] == (msgbuf_chk | IXGBE_VT_MSGTYPE_FAILURE))
			return IXGBE_ERR_OUT_OF_MEM;
	}

	return ret_val;
}

/**
 * ixgbe_setup_mac_link_vf - Setup MAC link settings
 * @hw: pointer to hardware structure
 * @speed: new link speed
 * @autoneg_wait_to_complete: TRUE when waiting for completion is needed
 *
 * Set the link speed in the AUTOC register and restarts link.
 **/
int32_t ixgbe_setup_mac_link_vf(struct ixgbe_hw *hw, ixgbe_link_speed speed,
			    bool autoneg_wait_to_complete)
{
	return IXGBE_SUCCESS;
}

/**
 * ixgbe_check_mac_link_vf - Get link/speed status
 * @hw: pointer to hardware structure
 * @speed: pointer to link speed
 * @link_up: TRUE is link is up, FALSE otherwise
 * @autoneg_wait_to_complete: TRUE when waiting for completion is needed
 *
 * Reads the links register to determine if link is up and the current speed
 **/
int32_t ixgbe_check_mac_link_vf(struct ixgbe_hw *hw, ixgbe_link_speed *speed,
			    bool *link_up, bool autoneg_wait_to_complete)
{
	struct ixgbe_mbx_info *mbx = &hw->mbx;
	struct ixgbe_mac_info *mac = &hw->mac;
	int32_t ret_val = IXGBE_SUCCESS;
	uint32_t in_msg = 0;
	uint32_t links_reg;

	/* If we were hit with a reset drop the link */
	if (!mbx->ops.check_for_rst(hw, 0) || !mbx->timeout)
		mac->get_link_status = TRUE;

	if (!mac->get_link_status)
		goto out;

	/* if link status is down no point in checking to see if pf is up */
	links_reg = IXGBE_READ_REG(hw, IXGBE_VFLINKS);
	if (!(links_reg & IXGBE_LINKS_UP))
		goto out;

	/* for SFP+ modules and DA cables on 82599 it can take up to 500usecs
	 * before the link status is correct
	 */
	if (mac->type == ixgbe_mac_82599_vf) {
		int i;

		for (i = 0; i < 5; i++) {
			usec_delay(100);
			links_reg = IXGBE_READ_REG(hw, IXGBE_VFLINKS);

			if (!(links_reg & IXGBE_LINKS_UP))
				goto out;
		}
	}

	switch (links_reg & IXGBE_LINKS_SPEED_82599) {
	case IXGBE_LINKS_SPEED_10G_82599:
		*speed = IXGBE_LINK_SPEED_10GB_FULL;
		if (hw->mac.type >= ixgbe_mac_X550_vf) {
			if (links_reg & IXGBE_LINKS_SPEED_NON_STD)
				*speed = IXGBE_LINK_SPEED_2_5GB_FULL;
		}
		break;
	case IXGBE_LINKS_SPEED_1G_82599:
		*speed = IXGBE_LINK_SPEED_1GB_FULL;
		break;
	case IXGBE_LINKS_SPEED_100_82599:
		*speed = IXGBE_LINK_SPEED_100_FULL;
		if (hw->mac.type == ixgbe_mac_X550_vf) {
			if (links_reg & IXGBE_LINKS_SPEED_NON_STD)
				*speed = IXGBE_LINK_SPEED_5GB_FULL;
		}
		break;
	case IXGBE_LINKS_SPEED_10_X550EM_A:
		*speed = IXGBE_LINK_SPEED_UNKNOWN;
		/* Since Reserved in older MAC's */
		if (hw->mac.type >= ixgbe_mac_X550_vf)
			*speed = IXGBE_LINK_SPEED_10_FULL;
		break;
	default:
		*speed = IXGBE_LINK_SPEED_UNKNOWN;
	}

	/* if the read failed it could just be a mailbox collision, best wait
	 * until we are called again and don't report an error
	 */
	if (ixgbe_read_mbx(hw, &in_msg, 1, 0)) {
		if (hw->api_version >= ixgbe_mbox_api_15)
			mac->get_link_status = FALSE;
		goto out;
	}

	if (!(in_msg & IXGBE_VT_MSGTYPE_CTS)) {
		/* msg is not CTS and is NACK we must have lost CTS status */
		if (in_msg & IXGBE_VT_MSGTYPE_FAILURE)
			ret_val = IXGBE_ERR_MBX;
		goto out;
	}

	/* the pf is talking, if we timed out in the past we reinit */
	if (!mbx->timeout) {
		ret_val = IXGBE_ERR_TIMEOUT;
		goto out;
	}

	/* if we passed all the tests above then the link is up and we no
	 * longer need to check for link
	 */
	mac->get_link_status = FALSE;

out:
	*link_up = !mac->get_link_status;
	return ret_val;
}

/**
 * ixgbevf_rlpml_set_vf - Set the maximum receive packet length
 * @hw: pointer to the HW structure
 * @max_size: value to assign to max frame size
 **/
int32_t ixgbevf_rlpml_set_vf(struct ixgbe_hw *hw, uint16_t max_size)
{
	uint32_t msgbuf[2];
	int32_t retval;

	msgbuf[0] = IXGBE_VF_SET_LPE;
	msgbuf[1] = max_size;

	retval = ixgbevf_write_msg_read_ack(hw, msgbuf, msgbuf, 2);
	if (retval)
		return retval;
	if ((msgbuf[0] & IXGBE_VF_SET_LPE) &&
	    (msgbuf[0] & IXGBE_VT_MSGTYPE_FAILURE))
		return IXGBE_ERR_MBX;

	return 0;
}

/**
 * ixgbevf_negotiate_api_version - Negotiate supported API version
 * @hw: pointer to the HW structure
 * @api: integer containing requested API version
 **/
int ixgbevf_negotiate_api_version(struct ixgbe_hw *hw, int api)
{
	int err;
	uint32_t msg[3];

	/* Negotiate the mailbox API version */
	msg[0] = IXGBE_VF_API_NEGOTIATE;
	msg[1] = api;
	msg[2] = 0;

	err = ixgbevf_write_msg_read_ack(hw, msg, msg, 3);
	if (!err) {
		msg[0] &= ~IXGBE_VT_MSGTYPE_CTS;

		/* Store value and return 0 on success */
		if (msg[0] == (IXGBE_VF_API_NEGOTIATE | IXGBE_VT_MSGTYPE_SUCCESS)) {
			hw->api_version = api;
			return 0;
		}

		err = IXGBE_ERR_INVALID_ARGUMENT;
	}

	return err;
}

int ixgbevf_get_queues(struct ixgbe_hw *hw, unsigned int *num_tcs,
		       unsigned int *default_tc)
{
	int err;
	uint32_t msg[5];

	/* do nothing if API doesn't support ixgbevf_get_queues */
	switch (hw->api_version) {
	case ixgbe_mbox_api_11:
	case ixgbe_mbox_api_12:
	case ixgbe_mbox_api_13:
	case ixgbe_mbox_api_15:
		break;
	default:
		return 0;
	}

	/* Fetch queue configuration from the PF */
	msg[0] = IXGBE_VF_GET_QUEUES;
	msg[1] = msg[2] = msg[3] = msg[4] = 0;

	err = ixgbevf_write_msg_read_ack(hw, msg, msg, 5);
	if (!err) {
		msg[0] &= ~IXGBE_VT_MSGTYPE_CTS;

		/*
		 * if we didn't get a SUCCESS there must have been
		 * some sort of mailbox error so we should treat it
		 * as such
		 */
		if (msg[0] != (IXGBE_VF_GET_QUEUES | IXGBE_VT_MSGTYPE_SUCCESS))
			return IXGBE_ERR_MBX;

		/* record and validate values from message */
		hw->mac.max_tx_queues = msg[IXGBE_VF_TX_QUEUES];
		if (hw->mac.max_tx_queues == 0 ||
		    hw->mac.max_tx_queues > IXGBE_VF_MAX_TX_QUEUES)
			hw->mac.max_tx_queues = IXGBE_VF_MAX_TX_QUEUES;

		hw->mac.max_rx_queues = msg[IXGBE_VF_RX_QUEUES];
		if (hw->mac.max_rx_queues == 0 ||
		    hw->mac.max_rx_queues > IXGBE_VF_MAX_RX_QUEUES)
			hw->mac.max_rx_queues = IXGBE_VF_MAX_RX_QUEUES;

		*num_tcs = msg[IXGBE_VF_TRANS_VLAN];
		/* in case of unknown state assume we cannot tag frames */
		if (*num_tcs > hw->mac.max_rx_queues)
			*num_tcs = 1;

		*default_tc = msg[IXGBE_VF_DEF_QUEUE];
		/* default to queue 0 on out-of-bounds queue number */
		if (*default_tc >= hw->mac.max_tx_queues)
			*default_tc = 0;
	}

	return err;
}
