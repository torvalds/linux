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
/**
 *  e1000_calculate_checksum - Calculate checksum for buffer
 *  @buffer: pointer to EEPROM
 *  @length: size of EEPROM to calculate a checksum for
 *
 *  Calculates the checksum for some buffer on a specified length.  The
 *  checksum calculated is returned.
 **/
u8 e1000_calculate_checksum(u8 *buffer, u32 length)
{
	u32 i;
	u8 sum = 0;

	DEBUGFUNC("e1000_calculate_checksum");

	if (!buffer)
		return 0;

	for (i = 0; i < length; i++)
		sum += buffer[i];

	return (u8) (0 - sum);
}

/**
 *  e1000_mng_enable_host_if_generic - Checks host interface is enabled
 *  @hw: pointer to the HW structure
 *
 *  Returns E1000_success upon success, else E1000_ERR_HOST_INTERFACE_COMMAND
 *
 *  This function checks whether the HOST IF is enabled for command operation
 *  and also checks whether the previous command is completed.  It busy waits
 *  in case of previous command is not completed.
 **/
s32 e1000_mng_enable_host_if_generic(struct e1000_hw *hw)
{
	u32 hicr;
	u8 i;

	DEBUGFUNC("e1000_mng_enable_host_if_generic");

	if (!hw->mac.arc_subsystem_valid) {
		DEBUGOUT("ARC subsystem not valid.\n");
		return -E1000_ERR_HOST_INTERFACE_COMMAND;
	}

	/* Check that the host interface is enabled. */
	hicr = E1000_READ_REG(hw, E1000_HICR);
	if (!(hicr & E1000_HICR_EN)) {
		DEBUGOUT("E1000_HOST_EN bit disabled.\n");
		return -E1000_ERR_HOST_INTERFACE_COMMAND;
	}
	/* check the previous command is completed */
	for (i = 0; i < E1000_MNG_DHCP_COMMAND_TIMEOUT; i++) {
		hicr = E1000_READ_REG(hw, E1000_HICR);
		if (!(hicr & E1000_HICR_C))
			break;
		msec_delay_irq(1);
	}

	if (i == E1000_MNG_DHCP_COMMAND_TIMEOUT) {
		DEBUGOUT("Previous command timeout failed .\n");
		return -E1000_ERR_HOST_INTERFACE_COMMAND;
	}

	return E1000_SUCCESS;
}

/**
 *  e1000_check_mng_mode_generic - Generic check management mode
 *  @hw: pointer to the HW structure
 *
 *  Reads the firmware semaphore register and returns TRUE (>0) if
 *  manageability is enabled, else FALSE (0).
 **/
bool e1000_check_mng_mode_generic(struct e1000_hw *hw)
{
	u32 fwsm = E1000_READ_REG(hw, E1000_FWSM);

	DEBUGFUNC("e1000_check_mng_mode_generic");


	return (fwsm & E1000_FWSM_MODE_MASK) ==
		(E1000_MNG_IAMT_MODE << E1000_FWSM_MODE_SHIFT);
}

/**
 *  e1000_enable_tx_pkt_filtering_generic - Enable packet filtering on Tx
 *  @hw: pointer to the HW structure
 *
 *  Enables packet filtering on transmit packets if manageability is enabled
 *  and host interface is enabled.
 **/
bool e1000_enable_tx_pkt_filtering_generic(struct e1000_hw *hw)
{
	struct e1000_host_mng_dhcp_cookie *hdr = &hw->mng_cookie;
	u32 *buffer = (u32 *)&hw->mng_cookie;
	u32 offset;
	s32 ret_val, hdr_csum, csum;
	u8 i, len;

	DEBUGFUNC("e1000_enable_tx_pkt_filtering_generic");

	hw->mac.tx_pkt_filtering = TRUE;

	/* No manageability, no filtering */
	if (!hw->mac.ops.check_mng_mode(hw)) {
		hw->mac.tx_pkt_filtering = FALSE;
		return hw->mac.tx_pkt_filtering;
	}

	/* If we can't read from the host interface for whatever
	 * reason, disable filtering.
	 */
	ret_val = e1000_mng_enable_host_if_generic(hw);
	if (ret_val != E1000_SUCCESS) {
		hw->mac.tx_pkt_filtering = FALSE;
		return hw->mac.tx_pkt_filtering;
	}

	/* Read in the header.  Length and offset are in dwords. */
	len    = E1000_MNG_DHCP_COOKIE_LENGTH >> 2;
	offset = E1000_MNG_DHCP_COOKIE_OFFSET >> 2;
	for (i = 0; i < len; i++)
		*(buffer + i) = E1000_READ_REG_ARRAY_DWORD(hw, E1000_HOST_IF,
							   offset + i);
	hdr_csum = hdr->checksum;
	hdr->checksum = 0;
	csum = e1000_calculate_checksum((u8 *)hdr,
					E1000_MNG_DHCP_COOKIE_LENGTH);
	/* If either the checksums or signature don't match, then
	 * the cookie area isn't considered valid, in which case we
	 * take the safe route of assuming Tx filtering is enabled.
	 */
	if ((hdr_csum != csum) || (hdr->signature != E1000_IAMT_SIGNATURE)) {
		hw->mac.tx_pkt_filtering = TRUE;
		return hw->mac.tx_pkt_filtering;
	}

	/* Cookie area is valid, make the final check for filtering. */
	if (!(hdr->status & E1000_MNG_DHCP_COOKIE_STATUS_PARSING))
		hw->mac.tx_pkt_filtering = FALSE;

	return hw->mac.tx_pkt_filtering;
}

/**
 *  e1000_mng_write_cmd_header_generic - Writes manageability command header
 *  @hw: pointer to the HW structure
 *  @hdr: pointer to the host interface command header
 *
 *  Writes the command header after does the checksum calculation.
 **/
s32 e1000_mng_write_cmd_header_generic(struct e1000_hw *hw,
				      struct e1000_host_mng_command_header *hdr)
{
	u16 i, length = sizeof(struct e1000_host_mng_command_header);

	DEBUGFUNC("e1000_mng_write_cmd_header_generic");

	/* Write the whole command header structure with new checksum. */

	hdr->checksum = e1000_calculate_checksum((u8 *)hdr, length);

	length >>= 2;
	/* Write the relevant command block into the ram area. */
	for (i = 0; i < length; i++) {
		E1000_WRITE_REG_ARRAY_DWORD(hw, E1000_HOST_IF, i,
					    *((u32 *) hdr + i));
		E1000_WRITE_FLUSH(hw);
	}

	return E1000_SUCCESS;
}

/**
 *  e1000_mng_host_if_write_generic - Write to the manageability host interface
 *  @hw: pointer to the HW structure
 *  @buffer: pointer to the host interface buffer
 *  @length: size of the buffer
 *  @offset: location in the buffer to write to
 *  @sum: sum of the data (not checksum)
 *
 *  This function writes the buffer content at the offset given on the host if.
 *  It also does alignment considerations to do the writes in most efficient
 *  way.  Also fills up the sum of the buffer in *buffer parameter.
 **/
s32 e1000_mng_host_if_write_generic(struct e1000_hw *hw, u8 *buffer,
				    u16 length, u16 offset, u8 *sum)
{
	u8 *tmp;
	u8 *bufptr = buffer;
	u32 data = 0;
	u16 remaining, i, j, prev_bytes;

	DEBUGFUNC("e1000_mng_host_if_write_generic");

	/* sum = only sum of the data and it is not checksum */

	if (length == 0 || offset + length > E1000_HI_MAX_MNG_DATA_LENGTH)
		return -E1000_ERR_PARAM;

	tmp = (u8 *)&data;
	prev_bytes = offset & 0x3;
	offset >>= 2;

	if (prev_bytes) {
		data = E1000_READ_REG_ARRAY_DWORD(hw, E1000_HOST_IF, offset);
		for (j = prev_bytes; j < sizeof(u32); j++) {
			*(tmp + j) = *bufptr++;
			*sum += *(tmp + j);
		}
		E1000_WRITE_REG_ARRAY_DWORD(hw, E1000_HOST_IF, offset, data);
		length -= j - prev_bytes;
		offset++;
	}

	remaining = length & 0x3;
	length -= remaining;

	/* Calculate length in DWORDs */
	length >>= 2;

	/* The device driver writes the relevant command block into the
	 * ram area.
	 */
	for (i = 0; i < length; i++) {
		for (j = 0; j < sizeof(u32); j++) {
			*(tmp + j) = *bufptr++;
			*sum += *(tmp + j);
		}

		E1000_WRITE_REG_ARRAY_DWORD(hw, E1000_HOST_IF, offset + i,
					    data);
	}
	if (remaining) {
		for (j = 0; j < sizeof(u32); j++) {
			if (j < remaining)
				*(tmp + j) = *bufptr++;
			else
				*(tmp + j) = 0;

			*sum += *(tmp + j);
		}
		E1000_WRITE_REG_ARRAY_DWORD(hw, E1000_HOST_IF, offset + i,
					    data);
	}

	return E1000_SUCCESS;
}

/**
 *  e1000_mng_write_dhcp_info_generic - Writes DHCP info to host interface
 *  @hw: pointer to the HW structure
 *  @buffer: pointer to the host interface
 *  @length: size of the buffer
 *
 *  Writes the DHCP information to the host interface.
 **/
s32 e1000_mng_write_dhcp_info_generic(struct e1000_hw *hw, u8 *buffer,
				      u16 length)
{
	struct e1000_host_mng_command_header hdr;
	s32 ret_val;
	u32 hicr;

	DEBUGFUNC("e1000_mng_write_dhcp_info_generic");

	hdr.command_id = E1000_MNG_DHCP_TX_PAYLOAD_CMD;
	hdr.command_length = length;
	hdr.reserved1 = 0;
	hdr.reserved2 = 0;
	hdr.checksum = 0;

	/* Enable the host interface */
	ret_val = e1000_mng_enable_host_if_generic(hw);
	if (ret_val)
		return ret_val;

	/* Populate the host interface with the contents of "buffer". */
	ret_val = e1000_mng_host_if_write_generic(hw, buffer, length,
						  sizeof(hdr), &(hdr.checksum));
	if (ret_val)
		return ret_val;

	/* Write the manageability command header */
	ret_val = e1000_mng_write_cmd_header_generic(hw, &hdr);
	if (ret_val)
		return ret_val;

	/* Tell the ARC a new command is pending. */
	hicr = E1000_READ_REG(hw, E1000_HICR);
	E1000_WRITE_REG(hw, E1000_HICR, hicr | E1000_HICR_C);

	return E1000_SUCCESS;
}

/**
 *  e1000_enable_mng_pass_thru - Check if management passthrough is needed
 *  @hw: pointer to the HW structure
 *
 *  Verifies the hardware needs to leave interface enabled so that frames can
 *  be directed to and from the management interface.
 **/
bool e1000_enable_mng_pass_thru(struct e1000_hw *hw)
{
	u32 manc;
	u32 fwsm, factps;

	DEBUGFUNC("e1000_enable_mng_pass_thru");

	if (!hw->mac.asf_firmware_present)
		return FALSE;

	manc = E1000_READ_REG(hw, E1000_MANC);

	if (!(manc & E1000_MANC_RCV_TCO_EN))
		return FALSE;

	if (hw->mac.has_fwsm) {
		fwsm = E1000_READ_REG(hw, E1000_FWSM);
		factps = E1000_READ_REG(hw, E1000_FACTPS);

		if (!(factps & E1000_FACTPS_MNGCG) &&
		    ((fwsm & E1000_FWSM_MODE_MASK) ==
		     (e1000_mng_mode_pt << E1000_FWSM_MODE_SHIFT)))
			return TRUE;
	} else if ((hw->mac.type == e1000_82574) ||
		   (hw->mac.type == e1000_82583)) {
		u16 data;
		s32 ret_val;

		factps = E1000_READ_REG(hw, E1000_FACTPS);
		ret_val = e1000_read_nvm(hw, NVM_INIT_CONTROL2_REG, 1, &data);
		if (ret_val)
			return FALSE;

		if (!(factps & E1000_FACTPS_MNGCG) &&
		    ((data & E1000_NVM_INIT_CTRL2_MNGM) ==
		     (e1000_mng_mode_pt << 13)))
			return TRUE;
	} else if ((manc & E1000_MANC_SMBUS_EN) &&
		   !(manc & E1000_MANC_ASF_EN)) {
		return TRUE;
	}

	return FALSE;
}

/**
 *  e1000_host_interface_command - Writes buffer to host interface
 *  @hw: pointer to the HW structure
 *  @buffer: contains a command to write
 *  @length: the byte length of the buffer, must be multiple of 4 bytes
 *
 *  Writes a buffer to the Host Interface.  Upon success, returns E1000_SUCCESS
 *  else returns E1000_ERR_HOST_INTERFACE_COMMAND.
 **/
s32 e1000_host_interface_command(struct e1000_hw *hw, u8 *buffer, u32 length)
{
	u32 hicr, i;

	DEBUGFUNC("e1000_host_interface_command");

	if (!(hw->mac.arc_subsystem_valid)) {
		DEBUGOUT("Hardware doesn't support host interface command.\n");
		return E1000_SUCCESS;
	}

	if (!hw->mac.asf_firmware_present) {
		DEBUGOUT("Firmware is not present.\n");
		return E1000_SUCCESS;
	}

	if (length == 0 || length & 0x3 ||
	    length > E1000_HI_MAX_BLOCK_BYTE_LENGTH) {
		DEBUGOUT("Buffer length failure.\n");
		return -E1000_ERR_HOST_INTERFACE_COMMAND;
	}

	/* Check that the host interface is enabled. */
	hicr = E1000_READ_REG(hw, E1000_HICR);
	if (!(hicr & E1000_HICR_EN)) {
		DEBUGOUT("E1000_HOST_EN bit disabled.\n");
		return -E1000_ERR_HOST_INTERFACE_COMMAND;
	}

	/* Calculate length in DWORDs */
	length >>= 2;

	/* The device driver writes the relevant command block
	 * into the ram area.
	 */
	for (i = 0; i < length; i++)
		E1000_WRITE_REG_ARRAY_DWORD(hw, E1000_HOST_IF, i,
					    *((u32 *)buffer + i));

	/* Setting this bit tells the ARC that a new command is pending. */
	E1000_WRITE_REG(hw, E1000_HICR, hicr | E1000_HICR_C);

	for (i = 0; i < E1000_HI_COMMAND_TIMEOUT; i++) {
		hicr = E1000_READ_REG(hw, E1000_HICR);
		if (!(hicr & E1000_HICR_C))
			break;
		msec_delay(1);
	}

	/* Check command successful completion. */
	if (i == E1000_HI_COMMAND_TIMEOUT ||
	    (!(E1000_READ_REG(hw, E1000_HICR) & E1000_HICR_SV))) {
		DEBUGOUT("Command has failed with no status valid.\n");
		return -E1000_ERR_HOST_INTERFACE_COMMAND;
	}

	for (i = 0; i < length; i++)
		*((u32 *)buffer + i) = E1000_READ_REG_ARRAY_DWORD(hw,
								  E1000_HOST_IF,
								  i);

	return E1000_SUCCESS;
}
/**
 *  e1000_load_firmware - Writes proxy FW code buffer to host interface
 *                        and execute.
 *  @hw: pointer to the HW structure
 *  @buffer: contains a firmware to write
 *  @length: the byte length of the buffer, must be multiple of 4 bytes
 *
 *  Upon success returns E1000_SUCCESS, returns E1000_ERR_CONFIG if not enabled
 *  in HW else returns E1000_ERR_HOST_INTERFACE_COMMAND.
 **/
s32 e1000_load_firmware(struct e1000_hw *hw, u8 *buffer, u32 length)
{
	u32 hicr, hibba, fwsm, icr, i;

	DEBUGFUNC("e1000_load_firmware");

	if (hw->mac.type < e1000_i210) {
		DEBUGOUT("Hardware doesn't support loading FW by the driver\n");
		return -E1000_ERR_CONFIG;
	}

	/* Check that the host interface is enabled. */
	hicr = E1000_READ_REG(hw, E1000_HICR);
	if (!(hicr & E1000_HICR_EN)) {
		DEBUGOUT("E1000_HOST_EN bit disabled.\n");
		return -E1000_ERR_CONFIG;
	}
	if (!(hicr & E1000_HICR_MEMORY_BASE_EN)) {
		DEBUGOUT("E1000_HICR_MEMORY_BASE_EN bit disabled.\n");
		return -E1000_ERR_CONFIG;
	}

	if (length == 0 || length & 0x3 || length > E1000_HI_FW_MAX_LENGTH) {
		DEBUGOUT("Buffer length failure.\n");
		return -E1000_ERR_INVALID_ARGUMENT;
	}

	/* Clear notification from ROM-FW by reading ICR register */
	icr = E1000_READ_REG(hw, E1000_ICR_V2);

	/* Reset ROM-FW */
	hicr = E1000_READ_REG(hw, E1000_HICR);
	hicr |= E1000_HICR_FW_RESET_ENABLE;
	E1000_WRITE_REG(hw, E1000_HICR, hicr);
	hicr |= E1000_HICR_FW_RESET;
	E1000_WRITE_REG(hw, E1000_HICR, hicr);
	E1000_WRITE_FLUSH(hw);

	/* Wait till MAC notifies about its readiness after ROM-FW reset */
	for (i = 0; i < (E1000_HI_COMMAND_TIMEOUT * 2); i++) {
		icr = E1000_READ_REG(hw, E1000_ICR_V2);
		if (icr & E1000_ICR_MNG)
			break;
		msec_delay(1);
	}

	/* Check for timeout */
	if (i == E1000_HI_COMMAND_TIMEOUT) {
		DEBUGOUT("FW reset failed.\n");
		return -E1000_ERR_HOST_INTERFACE_COMMAND;
	}

	/* Wait till MAC is ready to accept new FW code */
	for (i = 0; i < E1000_HI_COMMAND_TIMEOUT; i++) {
		fwsm = E1000_READ_REG(hw, E1000_FWSM);
		if ((fwsm & E1000_FWSM_FW_VALID) &&
		    ((fwsm & E1000_FWSM_MODE_MASK) >> E1000_FWSM_MODE_SHIFT ==
		    E1000_FWSM_HI_EN_ONLY_MODE))
			break;
		msec_delay(1);
	}

	/* Check for timeout */
	if (i == E1000_HI_COMMAND_TIMEOUT) {
		DEBUGOUT("FW reset failed.\n");
		return -E1000_ERR_HOST_INTERFACE_COMMAND;
	}

	/* Calculate length in DWORDs */
	length >>= 2;

	/* The device driver writes the relevant FW code block
	 * into the ram area in DWORDs via 1kB ram addressing window.
	 */
	for (i = 0; i < length; i++) {
		if (!(i % E1000_HI_FW_BLOCK_DWORD_LENGTH)) {
			/* Point to correct 1kB ram window */
			hibba = E1000_HI_FW_BASE_ADDRESS +
				((E1000_HI_FW_BLOCK_DWORD_LENGTH << 2) *
				(i / E1000_HI_FW_BLOCK_DWORD_LENGTH));

			E1000_WRITE_REG(hw, E1000_HIBBA, hibba);
		}

		E1000_WRITE_REG_ARRAY_DWORD(hw, E1000_HOST_IF,
					    i % E1000_HI_FW_BLOCK_DWORD_LENGTH,
					    *((u32 *)buffer + i));
	}

	/* Setting this bit tells the ARC that a new FW is ready to execute. */
	hicr = E1000_READ_REG(hw, E1000_HICR);
	E1000_WRITE_REG(hw, E1000_HICR, hicr | E1000_HICR_C);

	for (i = 0; i < E1000_HI_COMMAND_TIMEOUT; i++) {
		hicr = E1000_READ_REG(hw, E1000_HICR);
		if (!(hicr & E1000_HICR_C))
			break;
		msec_delay(1);
	}

	/* Check for successful FW start. */
	if (i == E1000_HI_COMMAND_TIMEOUT) {
		DEBUGOUT("New FW did not start within timeout period.\n");
		return -E1000_ERR_HOST_INTERFACE_COMMAND;
	}

	return E1000_SUCCESS;
}


