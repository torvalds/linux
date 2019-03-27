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

#ifndef _IXGBE_X550_H_
#define _IXGBE_X550_H_

#include "ixgbe_type.h"

s32 ixgbe_dmac_config_X550(struct ixgbe_hw *hw);
s32 ixgbe_dmac_config_tcs_X550(struct ixgbe_hw *hw);
s32 ixgbe_dmac_update_tcs_X550(struct ixgbe_hw *hw);

s32 ixgbe_get_bus_info_X550em(struct ixgbe_hw *hw);
s32 ixgbe_init_eeprom_params_X550(struct ixgbe_hw *hw);
s32 ixgbe_update_eeprom_checksum_X550(struct ixgbe_hw *hw);
s32 ixgbe_calc_eeprom_checksum_X550(struct ixgbe_hw *hw);
s32 ixgbe_calc_checksum_X550(struct ixgbe_hw *hw, u16 *buffer, u32 buffer_size);
s32 ixgbe_validate_eeprom_checksum_X550(struct ixgbe_hw *hw, u16 *checksum_val);
s32 ixgbe_update_flash_X550(struct ixgbe_hw *hw);
s32 ixgbe_write_ee_hostif_buffer_X550(struct ixgbe_hw *hw,
				      u16 offset, u16 words, u16 *data);
s32 ixgbe_write_ee_hostif_X550(struct ixgbe_hw *hw, u16 offset,
			       u16 data);
s32 ixgbe_read_ee_hostif_buffer_X550(struct ixgbe_hw *hw,
				     u16 offset, u16 words, u16 *data);
s32 ixgbe_read_ee_hostif_X550(struct ixgbe_hw *hw, u16 offset,
u16				*data);
s32 ixgbe_write_ee_hostif_data_X550(struct ixgbe_hw *hw, u16 offset,
				    u16 data);
void ixgbe_set_source_address_pruning_X550(struct ixgbe_hw *hw, bool enable,
					   unsigned int pool);
void ixgbe_set_ethertype_anti_spoofing_X550(struct ixgbe_hw *hw,
					    bool enable, int vf);
s32 ixgbe_write_iosf_sb_reg_x550(struct ixgbe_hw *hw, u32 reg_addr,
				 u32 device_type, u32 data);
s32 ixgbe_read_iosf_sb_reg_x550(struct ixgbe_hw *hw, u32 reg_addr,
	u32 device_type, u32 *data);
s32 ixgbe_set_fw_drv_ver_x550(struct ixgbe_hw *hw, u8 maj, u8 min,
			      u8 build, u8 ver, u16 len, const char *str);
s32 ixgbe_get_phy_token(struct ixgbe_hw *);
s32 ixgbe_put_phy_token(struct ixgbe_hw *);
s32 ixgbe_write_iosf_sb_reg_x550a(struct ixgbe_hw *hw, u32 reg_addr,
	u32 device_type, u32 data);
s32 ixgbe_read_iosf_sb_reg_x550a(struct ixgbe_hw *hw, u32 reg_addr,
	u32 device_type, u32 *data);
void ixgbe_disable_mdd_X550(struct ixgbe_hw *hw);
void ixgbe_enable_mdd_X550(struct ixgbe_hw *hw);
void ixgbe_mdd_event_X550(struct ixgbe_hw *hw, u32 *vf_bitmap);
void ixgbe_restore_mdd_vf_X550(struct ixgbe_hw *hw, u32 vf);
enum ixgbe_media_type ixgbe_get_media_type_X550em(struct ixgbe_hw *hw);
s32 ixgbe_setup_sfp_modules_X550em(struct ixgbe_hw *hw);
s32 ixgbe_get_link_capabilities_X550em(struct ixgbe_hw *hw,
				       ixgbe_link_speed *speed, bool *autoneg);
void ixgbe_init_mac_link_ops_X550em(struct ixgbe_hw *hw);
s32 ixgbe_reset_hw_X550em(struct ixgbe_hw *hw);
s32 ixgbe_init_phy_ops_X550em(struct ixgbe_hw *hw);
s32 ixgbe_setup_kr_x550em(struct ixgbe_hw *hw);
s32 ixgbe_init_ext_t_x550em(struct ixgbe_hw *hw);
s32 ixgbe_setup_internal_phy_t_x550em(struct ixgbe_hw *hw);
s32 ixgbe_setup_phy_loopback_x550em(struct ixgbe_hw *hw);
u64 ixgbe_get_supported_physical_layer_X550em(struct ixgbe_hw *hw);
void ixgbe_disable_rx_x550(struct ixgbe_hw *hw);
s32 ixgbe_get_lcd_t_x550em(struct ixgbe_hw *hw, ixgbe_link_speed *lcd_speed);
s32 ixgbe_enter_lplu_t_x550em(struct ixgbe_hw *hw);
s32 ixgbe_acquire_swfw_sync_X550em(struct ixgbe_hw *hw, u32 mask);
void ixgbe_release_swfw_sync_X550em(struct ixgbe_hw *hw, u32 mask);
s32 ixgbe_setup_fc_X550em(struct ixgbe_hw *hw);
s32 ixgbe_setup_mac_link_sfp_x550em(struct ixgbe_hw *hw,
				    ixgbe_link_speed speed,
				    bool autoneg_wait_to_complete);
s32 ixgbe_setup_mac_link_sfp_x550a(struct ixgbe_hw *hw,
				    ixgbe_link_speed speed,
				    bool autoneg_wait_to_complete);
s32 ixgbe_read_phy_reg_x550a(struct ixgbe_hw *hw, u32 reg_addr,
			       u32 device_type, u16 *phy_data);
s32 ixgbe_write_phy_reg_x550a(struct ixgbe_hw *hw, u32 reg_addr,
				u32 device_type, u16 phy_data);
s32 ixgbe_setup_fc_fiber_x550em_a(struct ixgbe_hw *hw);
s32 ixgbe_setup_fc_backplane_x550em_a(struct ixgbe_hw *hw);
s32 ixgbe_setup_fc_sgmii_x550em_a(struct ixgbe_hw *hw);
void ixgbe_fc_autoneg_fiber_x550em_a(struct ixgbe_hw *hw);
void ixgbe_fc_autoneg_backplane_x550em_a(struct ixgbe_hw *hw);
void ixgbe_fc_autoneg_sgmii_x550em_a(struct ixgbe_hw *hw);
s32 ixgbe_handle_lasi_ext_t_x550em(struct ixgbe_hw *hw);
s32 ixgbe_setup_mac_link_t_X550em(struct ixgbe_hw *hw,
				  ixgbe_link_speed speed,
				  bool autoneg_wait_to_complete);
s32 ixgbe_check_link_t_X550em(struct ixgbe_hw *hw, ixgbe_link_speed *speed,
			      bool *link_up, bool link_up_wait_to_complete);
s32 ixgbe_reset_phy_t_X550em(struct ixgbe_hw *hw);
s32 ixgbe_identify_sfp_module_X550em(struct ixgbe_hw *hw);
s32 ixgbe_led_on_t_X550em(struct ixgbe_hw *hw, u32 led_idx);
s32 ixgbe_led_off_t_X550em(struct ixgbe_hw *hw, u32 led_idx);
#endif /* _IXGBE_X550_H_ */
