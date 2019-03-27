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

#ifndef _IXGBE_API_H_
#define _IXGBE_API_H_

#include "ixgbe_type.h"

void ixgbe_dcb_get_rtrup2tc(struct ixgbe_hw *hw, u8 *map);

s32 ixgbe_init_shared_code(struct ixgbe_hw *hw);

extern s32 ixgbe_init_ops_82598(struct ixgbe_hw *hw);
extern s32 ixgbe_init_ops_82599(struct ixgbe_hw *hw);
extern s32 ixgbe_init_ops_X540(struct ixgbe_hw *hw);
extern s32 ixgbe_init_ops_X550(struct ixgbe_hw *hw);
extern s32 ixgbe_init_ops_X550EM(struct ixgbe_hw *hw);
extern s32 ixgbe_init_ops_X550EM_x(struct ixgbe_hw *hw);
extern s32 ixgbe_init_ops_X550EM_a(struct ixgbe_hw *hw);

s32 ixgbe_set_mac_type(struct ixgbe_hw *hw);
s32 ixgbe_init_hw(struct ixgbe_hw *hw);
s32 ixgbe_reset_hw(struct ixgbe_hw *hw);
s32 ixgbe_start_hw(struct ixgbe_hw *hw);
void ixgbe_enable_relaxed_ordering(struct ixgbe_hw *hw);
s32 ixgbe_clear_hw_cntrs(struct ixgbe_hw *hw);
enum ixgbe_media_type ixgbe_get_media_type(struct ixgbe_hw *hw);
s32 ixgbe_get_mac_addr(struct ixgbe_hw *hw, u8 *mac_addr);
s32 ixgbe_get_bus_info(struct ixgbe_hw *hw);
u32 ixgbe_get_num_of_tx_queues(struct ixgbe_hw *hw);
u32 ixgbe_get_num_of_rx_queues(struct ixgbe_hw *hw);
s32 ixgbe_stop_adapter(struct ixgbe_hw *hw);
s32 ixgbe_read_pba_num(struct ixgbe_hw *hw, u32 *pba_num);
s32 ixgbe_read_pba_string(struct ixgbe_hw *hw, u8 *pba_num, u32 pba_num_size);

s32 ixgbe_identify_phy(struct ixgbe_hw *hw);
s32 ixgbe_reset_phy(struct ixgbe_hw *hw);
s32 ixgbe_read_phy_reg(struct ixgbe_hw *hw, u32 reg_addr, u32 device_type,
		       u16 *phy_data);
s32 ixgbe_write_phy_reg(struct ixgbe_hw *hw, u32 reg_addr, u32 device_type,
			u16 phy_data);

s32 ixgbe_setup_phy_link(struct ixgbe_hw *hw);
s32 ixgbe_setup_internal_phy(struct ixgbe_hw *hw);
s32 ixgbe_check_phy_link(struct ixgbe_hw *hw,
			 ixgbe_link_speed *speed,
			 bool *link_up);
s32 ixgbe_setup_phy_link_speed(struct ixgbe_hw *hw,
			       ixgbe_link_speed speed,
			       bool autoneg_wait_to_complete);
s32 ixgbe_set_phy_power(struct ixgbe_hw *, bool on);
void ixgbe_disable_tx_laser(struct ixgbe_hw *hw);
void ixgbe_enable_tx_laser(struct ixgbe_hw *hw);
void ixgbe_flap_tx_laser(struct ixgbe_hw *hw);
s32 ixgbe_setup_link(struct ixgbe_hw *hw, ixgbe_link_speed speed,
		     bool autoneg_wait_to_complete);
s32 ixgbe_setup_mac_link(struct ixgbe_hw *hw, ixgbe_link_speed speed,
			 bool autoneg_wait_to_complete);
s32 ixgbe_check_link(struct ixgbe_hw *hw, ixgbe_link_speed *speed,
		     bool *link_up, bool link_up_wait_to_complete);
s32 ixgbe_get_link_capabilities(struct ixgbe_hw *hw, ixgbe_link_speed *speed,
				bool *autoneg);
s32 ixgbe_led_on(struct ixgbe_hw *hw, u32 index);
s32 ixgbe_led_off(struct ixgbe_hw *hw, u32 index);
s32 ixgbe_blink_led_start(struct ixgbe_hw *hw, u32 index);
s32 ixgbe_blink_led_stop(struct ixgbe_hw *hw, u32 index);

s32 ixgbe_init_eeprom_params(struct ixgbe_hw *hw);
s32 ixgbe_write_eeprom(struct ixgbe_hw *hw, u16 offset, u16 data);
s32 ixgbe_write_eeprom_buffer(struct ixgbe_hw *hw, u16 offset,
			      u16 words, u16 *data);
s32 ixgbe_read_eeprom(struct ixgbe_hw *hw, u16 offset, u16 *data);
s32 ixgbe_read_eeprom_buffer(struct ixgbe_hw *hw, u16 offset,
			     u16 words, u16 *data);

s32 ixgbe_validate_eeprom_checksum(struct ixgbe_hw *hw, u16 *checksum_val);
s32 ixgbe_update_eeprom_checksum(struct ixgbe_hw *hw);

s32 ixgbe_insert_mac_addr(struct ixgbe_hw *hw, u8 *addr, u32 vmdq);
s32 ixgbe_set_rar(struct ixgbe_hw *hw, u32 index, u8 *addr, u32 vmdq,
		  u32 enable_addr);
s32 ixgbe_clear_rar(struct ixgbe_hw *hw, u32 index);
s32 ixgbe_set_vmdq(struct ixgbe_hw *hw, u32 rar, u32 vmdq);
s32 ixgbe_set_vmdq_san_mac(struct ixgbe_hw *hw, u32 vmdq);
s32 ixgbe_clear_vmdq(struct ixgbe_hw *hw, u32 rar, u32 vmdq);
s32 ixgbe_init_rx_addrs(struct ixgbe_hw *hw);
u32 ixgbe_get_num_rx_addrs(struct ixgbe_hw *hw);
s32 ixgbe_update_uc_addr_list(struct ixgbe_hw *hw, u8 *addr_list,
			      u32 addr_count, ixgbe_mc_addr_itr func);
s32 ixgbe_update_mc_addr_list(struct ixgbe_hw *hw, u8 *mc_addr_list,
			      u32 mc_addr_count, ixgbe_mc_addr_itr func,
			      bool clear);
void ixgbe_add_uc_addr(struct ixgbe_hw *hw, u8 *addr_list, u32 vmdq);
s32 ixgbe_enable_mc(struct ixgbe_hw *hw);
s32 ixgbe_disable_mc(struct ixgbe_hw *hw);
s32 ixgbe_clear_vfta(struct ixgbe_hw *hw);
s32 ixgbe_set_vfta(struct ixgbe_hw *hw, u32 vlan,
		   u32 vind, bool vlan_on, bool vlvf_bypass);
s32 ixgbe_set_vlvf(struct ixgbe_hw *hw, u32 vlan, u32 vind,
		   bool vlan_on, u32 *vfta_delta, u32 vfta,
		   bool vlvf_bypass);
s32 ixgbe_fc_enable(struct ixgbe_hw *hw);
s32 ixgbe_setup_fc(struct ixgbe_hw *hw);
s32 ixgbe_set_fw_drv_ver(struct ixgbe_hw *hw, u8 maj, u8 min, u8 build,
			 u8 ver, u16 len, char *driver_ver);
void ixgbe_set_mta(struct ixgbe_hw *hw, u8 *mc_addr);
s32 ixgbe_get_phy_firmware_version(struct ixgbe_hw *hw,
				   u16 *firmware_version);
s32 ixgbe_read_analog_reg8(struct ixgbe_hw *hw, u32 reg, u8 *val);
s32 ixgbe_write_analog_reg8(struct ixgbe_hw *hw, u32 reg, u8 val);
s32 ixgbe_init_uta_tables(struct ixgbe_hw *hw);
s32 ixgbe_read_i2c_eeprom(struct ixgbe_hw *hw, u8 byte_offset, u8 *eeprom_data);
u64 ixgbe_get_supported_physical_layer(struct ixgbe_hw *hw);
s32 ixgbe_enable_rx_dma(struct ixgbe_hw *hw, u32 regval);
s32 ixgbe_disable_sec_rx_path(struct ixgbe_hw *hw);
s32 ixgbe_enable_sec_rx_path(struct ixgbe_hw *hw);
s32 ixgbe_mng_fw_enabled(struct ixgbe_hw *hw);
s32 ixgbe_reinit_fdir_tables_82599(struct ixgbe_hw *hw);
s32 ixgbe_init_fdir_signature_82599(struct ixgbe_hw *hw, u32 fdirctrl);
s32 ixgbe_init_fdir_perfect_82599(struct ixgbe_hw *hw, u32 fdirctrl,
					bool cloud_mode);
void ixgbe_fdir_add_signature_filter_82599(struct ixgbe_hw *hw,
					   union ixgbe_atr_hash_dword input,
					   union ixgbe_atr_hash_dword common,
					   u8 queue);
s32 ixgbe_fdir_set_input_mask_82599(struct ixgbe_hw *hw,
				    union ixgbe_atr_input *input_mask, bool cloud_mode);
s32 ixgbe_fdir_write_perfect_filter_82599(struct ixgbe_hw *hw,
					  union ixgbe_atr_input *input,
					  u16 soft_id, u8 queue, bool cloud_mode);
s32 ixgbe_fdir_erase_perfect_filter_82599(struct ixgbe_hw *hw,
					  union ixgbe_atr_input *input,
					  u16 soft_id);
s32 ixgbe_fdir_add_perfect_filter_82599(struct ixgbe_hw *hw,
					union ixgbe_atr_input *input,
					union ixgbe_atr_input *mask,
					u16 soft_id,
					u8 queue,
					bool cloud_mode);
void ixgbe_atr_compute_perfect_hash_82599(union ixgbe_atr_input *input,
					  union ixgbe_atr_input *mask);
u32 ixgbe_atr_compute_sig_hash_82599(union ixgbe_atr_hash_dword input,
				     union ixgbe_atr_hash_dword common);
bool ixgbe_verify_lesm_fw_enabled_82599(struct ixgbe_hw *hw);
s32 ixgbe_read_i2c_byte(struct ixgbe_hw *hw, u8 byte_offset, u8 dev_addr,
			u8 *data);
s32 ixgbe_read_i2c_byte_unlocked(struct ixgbe_hw *hw, u8 byte_offset,
				 u8 dev_addr, u8 *data);
s32 ixgbe_read_link(struct ixgbe_hw *hw, u8 addr, u16 reg, u16 *val);
s32 ixgbe_read_link_unlocked(struct ixgbe_hw *hw, u8 addr, u16 reg, u16 *val);
s32 ixgbe_write_i2c_byte(struct ixgbe_hw *hw, u8 byte_offset, u8 dev_addr,
			 u8 data);
void ixgbe_set_fdir_drop_queue_82599(struct ixgbe_hw *hw, u8 dropqueue);
s32 ixgbe_write_i2c_byte_unlocked(struct ixgbe_hw *hw, u8 byte_offset,
				  u8 dev_addr, u8 data);
s32 ixgbe_write_link(struct ixgbe_hw *hw, u8 addr, u16 reg, u16 val);
s32 ixgbe_write_link_unlocked(struct ixgbe_hw *hw, u8 addr, u16 reg, u16 val);
s32 ixgbe_write_i2c_eeprom(struct ixgbe_hw *hw, u8 byte_offset, u8 eeprom_data);
s32 ixgbe_get_san_mac_addr(struct ixgbe_hw *hw, u8 *san_mac_addr);
s32 ixgbe_set_san_mac_addr(struct ixgbe_hw *hw, u8 *san_mac_addr);
s32 ixgbe_get_device_caps(struct ixgbe_hw *hw, u16 *device_caps);
s32 ixgbe_acquire_swfw_semaphore(struct ixgbe_hw *hw, u32 mask);
void ixgbe_release_swfw_semaphore(struct ixgbe_hw *hw, u32 mask);
void ixgbe_init_swfw_semaphore(struct ixgbe_hw *hw);
s32 ixgbe_get_wwn_prefix(struct ixgbe_hw *hw, u16 *wwnn_prefix,
			 u16 *wwpn_prefix);
s32 ixgbe_get_fcoe_boot_status(struct ixgbe_hw *hw, u16 *bs);
s32 ixgbe_bypass_rw(struct ixgbe_hw *hw, u32 cmd, u32 *status);
s32 ixgbe_bypass_set(struct ixgbe_hw *hw, u32 cmd, u32 event, u32 action);
s32 ixgbe_bypass_rd_eep(struct ixgbe_hw *hw, u32 addr, u8 *value);
bool ixgbe_bypass_valid_rd(struct ixgbe_hw *hw, u32 in_reg, u32 out_reg);
s32 ixgbe_dmac_config(struct ixgbe_hw *hw);
s32 ixgbe_dmac_update_tcs(struct ixgbe_hw *hw);
s32 ixgbe_dmac_config_tcs(struct ixgbe_hw *hw);
s32 ixgbe_setup_eee(struct ixgbe_hw *hw, bool enable_eee);
void ixgbe_set_source_address_pruning(struct ixgbe_hw *hw, bool enable,
				      unsigned int vf);
void ixgbe_set_ethertype_anti_spoofing(struct ixgbe_hw *hw, bool enable,
				       int vf);
s32 ixgbe_read_iosf_sb_reg(struct ixgbe_hw *hw, u32 reg_addr,
			u32 device_type, u32 *phy_data);
s32 ixgbe_write_iosf_sb_reg(struct ixgbe_hw *hw, u32 reg_addr,
			u32 device_type, u32 phy_data);
void ixgbe_disable_mdd(struct ixgbe_hw *hw);
void ixgbe_enable_mdd(struct ixgbe_hw *hw);
void ixgbe_mdd_event(struct ixgbe_hw *hw, u32 *vf_bitmap);
void ixgbe_restore_mdd_vf(struct ixgbe_hw *hw, u32 vf);
s32 ixgbe_enter_lplu(struct ixgbe_hw *hw);
s32 ixgbe_handle_lasi(struct ixgbe_hw *hw);
void ixgbe_set_rate_select_speed(struct ixgbe_hw *hw, ixgbe_link_speed speed);
void ixgbe_disable_rx(struct ixgbe_hw *hw);
void ixgbe_enable_rx(struct ixgbe_hw *hw);
s32 ixgbe_negotiate_fc(struct ixgbe_hw *hw, u32 adv_reg, u32 lp_reg,
			u32 adv_sym, u32 adv_asm, u32 lp_sym, u32 lp_asm);

#endif /* _IXGBE_API_H_ */
