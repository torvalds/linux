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

#ifndef _E1000_MANAGE_H_
#define _E1000_MANAGE_H_

bool e1000_check_mng_mode_generic(struct e1000_hw *hw);
bool e1000_enable_tx_pkt_filtering_generic(struct e1000_hw *hw);
s32  e1000_mng_enable_host_if_generic(struct e1000_hw *hw);
s32  e1000_mng_host_if_write_generic(struct e1000_hw *hw, u8 *buffer,
				     u16 length, u16 offset, u8 *sum);
s32  e1000_mng_write_cmd_header_generic(struct e1000_hw *hw,
				     struct e1000_host_mng_command_header *hdr);
s32  e1000_mng_write_dhcp_info_generic(struct e1000_hw *hw,
				       u8 *buffer, u16 length);
bool e1000_enable_mng_pass_thru(struct e1000_hw *hw);
u8 e1000_calculate_checksum(u8 *buffer, u32 length);
s32 e1000_host_interface_command(struct e1000_hw *hw, u8 *buffer, u32 length);
s32 e1000_load_firmware(struct e1000_hw *hw, u8 *buffer, u32 length);

enum e1000_mng_mode {
	e1000_mng_mode_none = 0,
	e1000_mng_mode_asf,
	e1000_mng_mode_pt,
	e1000_mng_mode_ipmi,
	e1000_mng_mode_host_if_only
};

#define E1000_FACTPS_MNGCG			0x20000000

#define E1000_FWSM_MODE_MASK			0xE
#define E1000_FWSM_MODE_SHIFT			1
#define E1000_FWSM_FW_VALID			0x00008000
#define E1000_FWSM_HI_EN_ONLY_MODE		0x4

#define E1000_MNG_IAMT_MODE			0x3
#define E1000_MNG_DHCP_COOKIE_LENGTH		0x10
#define E1000_MNG_DHCP_COOKIE_OFFSET		0x6F0
#define E1000_MNG_DHCP_COMMAND_TIMEOUT		10
#define E1000_MNG_DHCP_TX_PAYLOAD_CMD		64
#define E1000_MNG_DHCP_COOKIE_STATUS_PARSING	0x1
#define E1000_MNG_DHCP_COOKIE_STATUS_VLAN	0x2

#define E1000_VFTA_ENTRY_SHIFT			5
#define E1000_VFTA_ENTRY_MASK			0x7F
#define E1000_VFTA_ENTRY_BIT_SHIFT_MASK		0x1F

#define E1000_HI_MAX_BLOCK_BYTE_LENGTH		1792 /* Num of bytes in range */
#define E1000_HI_MAX_BLOCK_DWORD_LENGTH		448 /* Num of dwords in range */
#define E1000_HI_COMMAND_TIMEOUT		500 /* Process HI cmd limit */
#define E1000_HI_FW_BASE_ADDRESS		0x10000
#define E1000_HI_FW_MAX_LENGTH			(64 * 1024) /* Num of bytes */
#define E1000_HI_FW_BLOCK_DWORD_LENGTH		256 /* Num of DWORDs per page */
#define E1000_HICR_MEMORY_BASE_EN		0x200 /* MB Enable bit - RO */
#define E1000_HICR_EN			0x01  /* Enable bit - RO */
/* Driver sets this bit when done to put command in RAM */
#define E1000_HICR_C			0x02
#define E1000_HICR_SV			0x04  /* Status Validity */
#define E1000_HICR_FW_RESET_ENABLE	0x40
#define E1000_HICR_FW_RESET		0x80

/* Intel(R) Active Management Technology signature */
#define E1000_IAMT_SIGNATURE		0x544D4149

#endif
