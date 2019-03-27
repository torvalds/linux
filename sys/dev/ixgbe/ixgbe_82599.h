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

#ifndef _IXGBE_82599_H_
#define _IXGBE_82599_H_

s32 ixgbe_get_link_capabilities_82599(struct ixgbe_hw *hw,
				      ixgbe_link_speed *speed, bool *autoneg);
enum ixgbe_media_type ixgbe_get_media_type_82599(struct ixgbe_hw *hw);
void ixgbe_disable_tx_laser_multispeed_fiber(struct ixgbe_hw *hw);
void ixgbe_enable_tx_laser_multispeed_fiber(struct ixgbe_hw *hw);
void ixgbe_flap_tx_laser_multispeed_fiber(struct ixgbe_hw *hw);
void ixgbe_set_hard_rate_select_speed(struct ixgbe_hw *hw,
					ixgbe_link_speed speed);
s32 ixgbe_setup_mac_link_smartspeed(struct ixgbe_hw *hw,
				    ixgbe_link_speed speed,
				    bool autoneg_wait_to_complete);
s32 ixgbe_start_mac_link_82599(struct ixgbe_hw *hw,
			       bool autoneg_wait_to_complete);
s32 ixgbe_setup_mac_link_82599(struct ixgbe_hw *hw, ixgbe_link_speed speed,
			       bool autoneg_wait_to_complete);
s32 ixgbe_setup_sfp_modules_82599(struct ixgbe_hw *hw);
void ixgbe_init_mac_link_ops_82599(struct ixgbe_hw *hw);
s32 ixgbe_reset_hw_82599(struct ixgbe_hw *hw);
s32 ixgbe_read_analog_reg8_82599(struct ixgbe_hw *hw, u32 reg, u8 *val);
s32 ixgbe_write_analog_reg8_82599(struct ixgbe_hw *hw, u32 reg, u8 val);
s32 ixgbe_start_hw_82599(struct ixgbe_hw *hw);
s32 ixgbe_identify_phy_82599(struct ixgbe_hw *hw);
s32 ixgbe_init_phy_ops_82599(struct ixgbe_hw *hw);
u64 ixgbe_get_supported_physical_layer_82599(struct ixgbe_hw *hw);
s32 ixgbe_enable_rx_dma_82599(struct ixgbe_hw *hw, u32 regval);
s32 prot_autoc_read_82599(struct ixgbe_hw *hw, bool *locked, u32 *reg_val);
s32 prot_autoc_write_82599(struct ixgbe_hw *hw, u32 reg_val, bool locked);
#endif /* _IXGBE_82599_H_ */
