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


#include "e1000_api.h"


static s32 e1000_init_phy_params_vf(struct e1000_hw *hw);
static s32 e1000_init_nvm_params_vf(struct e1000_hw *hw);
static void e1000_release_vf(struct e1000_hw *hw);
static s32 e1000_acquire_vf(struct e1000_hw *hw);
static s32 e1000_setup_link_vf(struct e1000_hw *hw);
static s32 e1000_get_bus_info_pcie_vf(struct e1000_hw *hw);
static s32 e1000_init_mac_params_vf(struct e1000_hw *hw);
static s32 e1000_check_for_link_vf(struct e1000_hw *hw);
static s32 e1000_get_link_up_info_vf(struct e1000_hw *hw, u16 *speed,
				     u16 *duplex);
static s32 e1000_init_hw_vf(struct e1000_hw *hw);
static s32 e1000_reset_hw_vf(struct e1000_hw *hw);
static void e1000_update_mc_addr_list_vf(struct e1000_hw *hw, u8 *, u32);
static int  e1000_rar_set_vf(struct e1000_hw *, u8 *, u32);
static s32 e1000_read_mac_addr_vf(struct e1000_hw *);

/**
 *  e1000_init_phy_params_vf - Inits PHY params
 *  @hw: pointer to the HW structure
 *
 *  Doesn't do much - there's no PHY available to the VF.
 **/
static s32 e1000_init_phy_params_vf(struct e1000_hw *hw)
{
	DEBUGFUNC("e1000_init_phy_params_vf");
	hw->phy.type = e1000_phy_vf;
	hw->phy.ops.acquire = e1000_acquire_vf;
	hw->phy.ops.release = e1000_release_vf;

	return E1000_SUCCESS;
}

/**
 *  e1000_init_nvm_params_vf - Inits NVM params
 *  @hw: pointer to the HW structure
 *
 *  Doesn't do much - there's no NVM available to the VF.
 **/
static s32 e1000_init_nvm_params_vf(struct e1000_hw *hw)
{
	DEBUGFUNC("e1000_init_nvm_params_vf");
	hw->nvm.type = e1000_nvm_none;
	hw->nvm.ops.acquire = e1000_acquire_vf;
	hw->nvm.ops.release = e1000_release_vf;

	return E1000_SUCCESS;
}

/**
 *  e1000_init_mac_params_vf - Inits MAC params
 *  @hw: pointer to the HW structure
 **/
static s32 e1000_init_mac_params_vf(struct e1000_hw *hw)
{
	struct e1000_mac_info *mac = &hw->mac;

	DEBUGFUNC("e1000_init_mac_params_vf");

	/* Set media type */
	/*
	 * Virtual functions don't care what they're media type is as they
	 * have no direct access to the PHY, or the media.  That is handled
	 * by the physical function driver.
	 */
	hw->phy.media_type = e1000_media_type_unknown;

	/* No ASF features for the VF driver */
	mac->asf_firmware_present = FALSE;
	/* ARC subsystem not supported */
	mac->arc_subsystem_valid = FALSE;
	/* Disable adaptive IFS mode so the generic funcs don't do anything */
	mac->adaptive_ifs = FALSE;
	/* VF's have no MTA Registers - PF feature only */
	mac->mta_reg_count = 128;
	/* VF's have no access to RAR entries  */
	mac->rar_entry_count = 1;

	/* Function pointers */
	/* link setup */
	mac->ops.setup_link = e1000_setup_link_vf;
	/* bus type/speed/width */
	mac->ops.get_bus_info = e1000_get_bus_info_pcie_vf;
	/* reset */
	mac->ops.reset_hw = e1000_reset_hw_vf;
	/* hw initialization */
	mac->ops.init_hw = e1000_init_hw_vf;
	/* check for link */
	mac->ops.check_for_link = e1000_check_for_link_vf;
	/* link info */
	mac->ops.get_link_up_info = e1000_get_link_up_info_vf;
	/* multicast address update */
	mac->ops.update_mc_addr_list = e1000_update_mc_addr_list_vf;
	/* set mac address */
	mac->ops.rar_set = e1000_rar_set_vf;
	/* read mac address */
	mac->ops.read_mac_addr = e1000_read_mac_addr_vf;


	return E1000_SUCCESS;
}

/**
 *  e1000_init_function_pointers_vf - Inits function pointers
 *  @hw: pointer to the HW structure
 **/
void e1000_init_function_pointers_vf(struct e1000_hw *hw)
{
	DEBUGFUNC("e1000_init_function_pointers_vf");

	hw->mac.ops.init_params = e1000_init_mac_params_vf;
	hw->nvm.ops.init_params = e1000_init_nvm_params_vf;
	hw->phy.ops.init_params = e1000_init_phy_params_vf;
	hw->mbx.ops.init_params = e1000_init_mbx_params_vf;
}

/**
 *  e1000_acquire_vf - Acquire rights to access PHY or NVM.
 *  @hw: pointer to the HW structure
 *
 *  There is no PHY or NVM so we want all attempts to acquire these to fail.
 *  In addition, the MAC registers to access PHY/NVM don't exist so we don't
 *  even want any SW to attempt to use them.
 **/
static s32 e1000_acquire_vf(struct e1000_hw E1000_UNUSEDARG *hw)
{
	return -E1000_ERR_PHY;
}

/**
 *  e1000_release_vf - Release PHY or NVM
 *  @hw: pointer to the HW structure
 *
 *  There is no PHY or NVM so we want all attempts to acquire these to fail.
 *  In addition, the MAC registers to access PHY/NVM don't exist so we don't
 *  even want any SW to attempt to use them.
 **/
static void e1000_release_vf(struct e1000_hw E1000_UNUSEDARG *hw)
{
	return;
}

/**
 *  e1000_setup_link_vf - Sets up link.
 *  @hw: pointer to the HW structure
 *
 *  Virtual functions cannot change link.
 **/
static s32 e1000_setup_link_vf(struct e1000_hw E1000_UNUSEDARG *hw)
{
	DEBUGFUNC("e1000_setup_link_vf");

	return E1000_SUCCESS;
}

/**
 *  e1000_get_bus_info_pcie_vf - Gets the bus info.
 *  @hw: pointer to the HW structure
 *
 *  Virtual functions are not really on their own bus.
 **/
static s32 e1000_get_bus_info_pcie_vf(struct e1000_hw *hw)
{
	struct e1000_bus_info *bus = &hw->bus;

	DEBUGFUNC("e1000_get_bus_info_pcie_vf");

	/* Do not set type PCI-E because we don't want disable master to run */
	bus->type = e1000_bus_type_reserved;
	bus->speed = e1000_bus_speed_2500;

	return 0;
}

/**
 *  e1000_get_link_up_info_vf - Gets link info.
 *  @hw: pointer to the HW structure
 *  @speed: pointer to 16 bit value to store link speed.
 *  @duplex: pointer to 16 bit value to store duplex.
 *
 *  Since we cannot read the PHY and get accurate link info, we must rely upon
 *  the status register's data which is often stale and inaccurate.
 **/
static s32 e1000_get_link_up_info_vf(struct e1000_hw *hw, u16 *speed,
				     u16 *duplex)
{
	s32 status;

	DEBUGFUNC("e1000_get_link_up_info_vf");

	status = E1000_READ_REG(hw, E1000_STATUS);
	if (status & E1000_STATUS_SPEED_1000) {
		*speed = SPEED_1000;
		DEBUGOUT("1000 Mbs, ");
	} else if (status & E1000_STATUS_SPEED_100) {
		*speed = SPEED_100;
		DEBUGOUT("100 Mbs, ");
	} else {
		*speed = SPEED_10;
		DEBUGOUT("10 Mbs, ");
	}

	if (status & E1000_STATUS_FD) {
		*duplex = FULL_DUPLEX;
		DEBUGOUT("Full Duplex\n");
	} else {
		*duplex = HALF_DUPLEX;
		DEBUGOUT("Half Duplex\n");
	}

	return E1000_SUCCESS;
}

/**
 *  e1000_reset_hw_vf - Resets the HW
 *  @hw: pointer to the HW structure
 *
 *  VF's provide a function level reset. This is done using bit 26 of ctrl_reg.
 *  This is all the reset we can perform on a VF.
 **/
static s32 e1000_reset_hw_vf(struct e1000_hw *hw)
{
	struct e1000_mbx_info *mbx = &hw->mbx;
	u32 timeout = E1000_VF_INIT_TIMEOUT;
	s32 ret_val = -E1000_ERR_MAC_INIT;
	u32 ctrl, msgbuf[3];
	u8 *addr = (u8 *)(&msgbuf[1]);

	DEBUGFUNC("e1000_reset_hw_vf");

	DEBUGOUT("Issuing a function level reset to MAC\n");
	ctrl = E1000_READ_REG(hw, E1000_CTRL);
	E1000_WRITE_REG(hw, E1000_CTRL, ctrl | E1000_CTRL_RST);

	/* we cannot reset while the RSTI / RSTD bits are asserted */
	while (!mbx->ops.check_for_rst(hw, 0) && timeout) {
		timeout--;
		usec_delay(5);
	}

	if (timeout) {
		/* mailbox timeout can now become active */
		mbx->timeout = E1000_VF_MBX_INIT_TIMEOUT;

		msgbuf[0] = E1000_VF_RESET;
		mbx->ops.write_posted(hw, msgbuf, 1, 0);

		msec_delay(10);

		/* set our "perm_addr" based on info provided by PF */
		ret_val = mbx->ops.read_posted(hw, msgbuf, 3, 0);
		if (!ret_val) {
			if (msgbuf[0] == (E1000_VF_RESET |
			    E1000_VT_MSGTYPE_ACK))
				memcpy(hw->mac.perm_addr, addr, 6);
			else
				ret_val = -E1000_ERR_MAC_INIT;
		}
	}

	return ret_val;
}

/**
 *  e1000_init_hw_vf - Inits the HW
 *  @hw: pointer to the HW structure
 *
 *  Not much to do here except clear the PF Reset indication if there is one.
 **/
static s32 e1000_init_hw_vf(struct e1000_hw *hw)
{
	DEBUGFUNC("e1000_init_hw_vf");

	/* attempt to set and restore our mac address */
	e1000_rar_set_vf(hw, hw->mac.addr, 0);

	return E1000_SUCCESS;
}

/**
 *  e1000_rar_set_vf - set device MAC address
 *  @hw: pointer to the HW structure
 *  @addr: pointer to the receive address
 *  @index receive address array register
 **/
static int e1000_rar_set_vf(struct e1000_hw *hw, u8 *addr,
			     u32 E1000_UNUSEDARG index)
{
	struct e1000_mbx_info *mbx = &hw->mbx;
	u32 msgbuf[3];
	u8 *msg_addr = (u8 *)(&msgbuf[1]);
	s32 ret_val;

	memset(msgbuf, 0, 12);
	msgbuf[0] = E1000_VF_SET_MAC_ADDR;
	memcpy(msg_addr, addr, 6);
	ret_val = mbx->ops.write_posted(hw, msgbuf, 3, 0);

	if (!ret_val)
		ret_val = mbx->ops.read_posted(hw, msgbuf, 3, 0);

	msgbuf[0] &= ~E1000_VT_MSGTYPE_CTS;

	/* if nacked the address was rejected, use "perm_addr" */
	if (!ret_val &&
	    (msgbuf[0] == (E1000_VF_SET_MAC_ADDR | E1000_VT_MSGTYPE_NACK)))
		e1000_read_mac_addr_vf(hw);

	return E1000_SUCCESS;
}

/**
 *  e1000_hash_mc_addr_vf - Generate a multicast hash value
 *  @hw: pointer to the HW structure
 *  @mc_addr: pointer to a multicast address
 *
 *  Generates a multicast address hash value which is used to determine
 *  the multicast filter table array address and new table value.
 **/
static u32 e1000_hash_mc_addr_vf(struct e1000_hw *hw, u8 *mc_addr)
{
	u32 hash_value, hash_mask;
	u8 bit_shift = 0;

	DEBUGFUNC("e1000_hash_mc_addr_generic");

	/* Register count multiplied by bits per register */
	hash_mask = (hw->mac.mta_reg_count * 32) - 1;

	/*
	 * The bit_shift is the number of left-shifts
	 * where 0xFF would still fall within the hash mask.
	 */
	while (hash_mask >> bit_shift != 0xFF)
		bit_shift++;

	hash_value = hash_mask & (((mc_addr[4] >> (8 - bit_shift)) |
				  (((u16) mc_addr[5]) << bit_shift)));

	return hash_value;
}

static void e1000_write_msg_read_ack(struct e1000_hw *hw,
				     u32 *msg, u16 size)
{
	struct e1000_mbx_info *mbx = &hw->mbx;
	u32 retmsg[E1000_VFMAILBOX_SIZE];
	s32 retval = mbx->ops.write_posted(hw, msg, size, 0);

	if (!retval)
		mbx->ops.read_posted(hw, retmsg, E1000_VFMAILBOX_SIZE, 0);
}

/**
 *  e1000_update_mc_addr_list_vf - Update Multicast addresses
 *  @hw: pointer to the HW structure
 *  @mc_addr_list: array of multicast addresses to program
 *  @mc_addr_count: number of multicast addresses to program
 *
 *  Updates the Multicast Table Array.
 *  The caller must have a packed mc_addr_list of multicast addresses.
 **/
void e1000_update_mc_addr_list_vf(struct e1000_hw *hw,
				  u8 *mc_addr_list, u32 mc_addr_count)
{
	u32 msgbuf[E1000_VFMAILBOX_SIZE];
	u16 *hash_list = (u16 *)&msgbuf[1];
	u32 hash_value;
	u32 i;

	DEBUGFUNC("e1000_update_mc_addr_list_vf");

	/* Each entry in the list uses 1 16 bit word.  We have 30
	 * 16 bit words available in our HW msg buffer (minus 1 for the
	 * msg type).  That's 30 hash values if we pack 'em right.  If
	 * there are more than 30 MC addresses to add then punt the
	 * extras for now and then add code to handle more than 30 later.
	 * It would be unusual for a server to request that many multi-cast
	 * addresses except for in large enterprise network environments.
	 */

	DEBUGOUT1("MC Addr Count = %d\n", mc_addr_count);

	if (mc_addr_count > 30) {
		msgbuf[0] |= E1000_VF_SET_MULTICAST_OVERFLOW;
		mc_addr_count = 30;
	}

	msgbuf[0] = E1000_VF_SET_MULTICAST;
	msgbuf[0] |= mc_addr_count << E1000_VT_MSGINFO_SHIFT;

	for (i = 0; i < mc_addr_count; i++) {
		hash_value = e1000_hash_mc_addr_vf(hw, mc_addr_list);
		DEBUGOUT1("Hash value = 0x%03X\n", hash_value);
		hash_list[i] = hash_value & 0x0FFF;
		mc_addr_list += ETH_ADDR_LEN;
	}

	e1000_write_msg_read_ack(hw, msgbuf, E1000_VFMAILBOX_SIZE);
}

/**
 *  e1000_vfta_set_vf - Set/Unset vlan filter table address
 *  @hw: pointer to the HW structure
 *  @vid: determines the vfta register and bit to set/unset
 *  @set: if TRUE then set bit, else clear bit
 **/
void e1000_vfta_set_vf(struct e1000_hw *hw, u16 vid, bool set)
{
	u32 msgbuf[2];

	msgbuf[0] = E1000_VF_SET_VLAN;
	msgbuf[1] = vid;
	/* Setting the 8 bit field MSG INFO to TRUE indicates "add" */
	if (set)
		msgbuf[0] |= E1000_VF_SET_VLAN_ADD;

	e1000_write_msg_read_ack(hw, msgbuf, 2);
}

/** e1000_rlpml_set_vf - Set the maximum receive packet length
 *  @hw: pointer to the HW structure
 *  @max_size: value to assign to max frame size
 **/
void e1000_rlpml_set_vf(struct e1000_hw *hw, u16 max_size)
{
	u32 msgbuf[2];

	msgbuf[0] = E1000_VF_SET_LPE;
	msgbuf[1] = max_size;

	e1000_write_msg_read_ack(hw, msgbuf, 2);
}

/**
 *  e1000_promisc_set_vf - Set flags for Unicast or Multicast promisc
 *  @hw: pointer to the HW structure
 *  @uni: boolean indicating unicast promisc status
 *  @multi: boolean indicating multicast promisc status
 **/
s32 e1000_promisc_set_vf(struct e1000_hw *hw, enum e1000_promisc_type type)
{
	struct e1000_mbx_info *mbx = &hw->mbx;
	u32 msgbuf = E1000_VF_SET_PROMISC;
	s32 ret_val;

	switch (type) {
	case e1000_promisc_multicast:
		msgbuf |= E1000_VF_SET_PROMISC_MULTICAST;
		break;
	case e1000_promisc_enabled:
		msgbuf |= E1000_VF_SET_PROMISC_MULTICAST;
		/* FALLTHROUGH */
	case e1000_promisc_unicast:
		msgbuf |= E1000_VF_SET_PROMISC_UNICAST;
		/* FALLTHROUGH */
	case e1000_promisc_disabled:
		break;
	default:
		return -E1000_ERR_MAC_INIT;
	}

	 ret_val = mbx->ops.write_posted(hw, &msgbuf, 1, 0);

	if (!ret_val)
		ret_val = mbx->ops.read_posted(hw, &msgbuf, 1, 0);

	if (!ret_val && !(msgbuf & E1000_VT_MSGTYPE_ACK))
		ret_val = -E1000_ERR_MAC_INIT;

	return ret_val;
}

/**
 *  e1000_read_mac_addr_vf - Read device MAC address
 *  @hw: pointer to the HW structure
 **/
static s32 e1000_read_mac_addr_vf(struct e1000_hw *hw)
{
	int i;

	for (i = 0; i < ETH_ADDR_LEN; i++)
		hw->mac.addr[i] = hw->mac.perm_addr[i];

	return E1000_SUCCESS;
}

/**
 *  e1000_check_for_link_vf - Check for link for a virtual interface
 *  @hw: pointer to the HW structure
 *
 *  Checks to see if the underlying PF is still talking to the VF and
 *  if it is then it reports the link state to the hardware, otherwise
 *  it reports link down and returns an error.
 **/
static s32 e1000_check_for_link_vf(struct e1000_hw *hw)
{
	struct e1000_mbx_info *mbx = &hw->mbx;
	struct e1000_mac_info *mac = &hw->mac;
	s32 ret_val = E1000_SUCCESS;
	u32 in_msg = 0;

	DEBUGFUNC("e1000_check_for_link_vf");

	/*
	 * We only want to run this if there has been a rst asserted.
	 * in this case that could mean a link change, device reset,
	 * or a virtual function reset
	 */

	/* If we were hit with a reset or timeout drop the link */
	if (!mbx->ops.check_for_rst(hw, 0) || !mbx->timeout)
		mac->get_link_status = TRUE;

	if (!mac->get_link_status)
		goto out;

	/* if link status is down no point in checking to see if pf is up */
	if (!(E1000_READ_REG(hw, E1000_STATUS) & E1000_STATUS_LU))
		goto out;

	/* if the read failed it could just be a mailbox collision, best wait
	 * until we are called again and don't report an error */
	if (mbx->ops.read(hw, &in_msg, 1, 0))
		goto out;

	/* if incoming message isn't clear to send we are waiting on response */
	if (!(in_msg & E1000_VT_MSGTYPE_CTS)) {
		/* message is not CTS and is NACK we have lost CTS status */
		if (in_msg & E1000_VT_MSGTYPE_NACK)
			ret_val = -E1000_ERR_MAC_INIT;
		goto out;
	}

	/* at this point we know the PF is talking to us, check and see if
	 * we are still accepting timeout or if we had a timeout failure.
	 * if we failed then we will need to reinit */
	if (!mbx->timeout) {
		ret_val = -E1000_ERR_MAC_INIT;
		goto out;
	}

	/* if we passed all the tests above then the link is up and we no
	 * longer need to check for link */
	mac->get_link_status = FALSE;

out:
	return ret_val;
}

